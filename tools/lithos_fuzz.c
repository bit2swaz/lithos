#include "core/table/format.h"
#include "core/table/table.h"
#include "lithos.h"
#include "util/env.h"
#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

static void usage(const char *prog) {
  fprintf(stderr, "Usage: %s <db_path>\n", prog);
}

static bool ensure_dir(const char *path) {
  struct stat st;
  if (stat(path, &st) == 0) {
    return S_ISDIR(st.st_mode);
  }
  return mkdir(path, 0755) == 0;
}

static bool has_sst_suffix(const char *name) {
  size_t n = strlen(name);
  return n > 4 && strcmp(name + n - 4, ".sst") == 0;
}

static bool find_latest_sst(const char *dbpath, char *out, size_t out_sz) {
  DIR *dir = opendir(dbpath);
  if (!dir)
    return false;

  unsigned long long best = 0;
  bool found = false;
  struct dirent *ent;
  while ((ent = readdir(dir)) != NULL) {
    if (!has_sst_suffix(ent->d_name))
      continue;
    char *end = NULL;
    unsigned long long num = strtoull(ent->d_name, &end, 10);
    if (end == ent->d_name)
      continue;
    if (num >= best) {
      best = num;
      if (snprintf(out, out_sz, "%s/%s", dbpath, ent->d_name) < (int)out_sz) {
        found = true;
      }
    }
  }

  closedir(dir);
  return found;
}

static bool flip_bit_at(const char *path, long offset) {
  FILE *f = fopen(path, "rb+");
  if (!f)
    return false;

  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return false;
  }
  long sz = ftell(f);
  if (sz <= 8 || offset < 0 || offset >= sz) {
    fclose(f);
    return false;
  }

  if (fseek(f, offset, SEEK_SET) != 0) {
    fclose(f);
    return false;
  }

  int byte = fgetc(f);
  if (byte == EOF) {
    fclose(f);
    return false;
  }

  uint8_t b = (uint8_t)byte;
  uint8_t bit = (uint8_t)(1u << (rand() % 8));
  b ^= bit;

  if (fseek(f, offset, SEEK_SET) != 0) {
    fclose(f);
    return false;
  }
  fputc(b, f);
  fflush(f);
  fclose(f);
  return true;
}

static bool flip_index_block_bit(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return false;

  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return false;
  }
  long sz = ftell(f);
  if (sz < LITHOS_FOOTER_ENCODED_LENGTH) {
    fclose(f);
    return false;
  }

  if (fseek(f, sz - LITHOS_FOOTER_ENCODED_LENGTH, SEEK_SET) != 0) {
    fclose(f);
    return false;
  }

  char footer_buf[LITHOS_FOOTER_ENCODED_LENGTH];
  size_t n = fread(footer_buf, 1, LITHOS_FOOTER_ENCODED_LENGTH, f);
  fclose(f);
  if (n != LITHOS_FOOTER_ENCODED_LENGTH) {
    return false;
  }

  Lithos_Footer footer;
  if (Footer_Decode(&footer, footer_buf) != LITHOS_OK) {
    fprintf(stderr, "Footer decode failed\n");
    return false;
  }
  if (!BlockHandle_IsValid(&footer.index_handle)) {
    fprintf(stderr, "Index handle invalid\n");
    return false;
  }

  uint64_t trailer_start =
      footer.index_handle.offset + footer.index_handle.size;
  uint64_t target = trailer_start + 1;

  if (target >= (uint64_t)sz) {
    fprintf(stderr, "Target beyond file: target=%llu size=%ld\n",
            (unsigned long long)target, sz);
    return false;
  }

  if (!flip_bit_at(path, (long)target)) {
    fprintf(stderr, "Failed to flip bit at %llu\n", (unsigned long long)target);
    return false;
  }

  fprintf(stderr, "Flipped index block trailer at offset=%llu\n",
          (unsigned long long)target);
  return true;
}

static bool flip_random_bit(const char *path) {
  if (flip_index_block_bit(path)) {
    return true;
  }

  FILE *f = fopen(path, "rb");
  if (!f)
    return false;
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return false;
  }
  long sz = ftell(f);
  fclose(f);
  if (sz <= 8) {
    return false;
  }

  long offset = sz - (LITHOS_FOOTER_ENCODED_LENGTH + 8);
  if (offset < 0)
    offset = sz / 2;
  return flip_bit_at(path, offset);
}

static bool detect_corruption_via_table(const char *sst_path,
                                        const Lithos_Options *opt) {
  struct stat st;
  if (stat(sst_path, &st) != 0) {
    fprintf(stderr, "stat(%s) failed: %s\n", sst_path, strerror(errno));
    return false;
  }

  Lithos_RandomAccessFile *file = NULL;
  Status s = Env_NewRandomAccessFile(sst_path, &file);
  if (!Status_IsOK(s)) {
    fprintf(stderr, "Env_NewRandomAccessFile(%s) failed: %s\n", sst_path,
            Status_ToString(s));
    Status_Free(s);
    return false;
  }

  Lithos_Table *table = NULL;
  Status ts = Table_Open(opt, file, (uint64_t)st.st_size, &table);
  if (Status_IsCorruption(ts) || Status_IsIOError(ts)) {
    printf("Detected corruption via direct Table_Open: %s\n",
           Status_ToString(ts));
    Status_Free(ts);
    RandomAccessFile_Close(file);
    return true;
  }

  if (Status_IsOK(ts)) {
    Table_Destroy(table);
  } else {
    Status_Free(ts);
    RandomAccessFile_Close(file);
  }

  return false;
}

int main(int argc, char **argv) {
  if (argc != 2) {
    usage(argv[0]);
    return 1;
  }

  const char *dbpath = argv[1];
  if (!ensure_dir(dbpath)) {
    fprintf(stderr, "Failed to ensure db directory %s: %s\n", dbpath,
            strerror(errno));
    return 1;
  }

  srand((unsigned)time(NULL));

  Lithos_Options opt;
  Lithos_Options_InitDefault(&opt);

  Lithos_DB *db = NULL;
  Status s = Lithos_Open(dbpath, &opt, &db);
  if (!Status_IsOK(s)) {
    fprintf(stderr, "open: %s\n", Status_ToString(s));
    Status_Free(s);
    return 1;
  }

  s = Lithos_Put(db, "integrity_test", "ok");
  if (!Status_IsOK(s)) {
    fprintf(stderr, "put: %s\n", Status_ToString(s));
    Status_Free(s);
    Lithos_Close(db);
    return 1;
  }

  const int filler_count = 8000;
  const int filler_size = 512;
  char *filler = malloc((size_t)filler_size + 1);
  if (filler == NULL) {
    fprintf(stderr, "alloc filler failed\n");
    Lithos_Close(db);
    return 1;
  }
  for (int i = 0; i < filler_size; i++) {
    filler[i] = 'A' + (i % 26);
  }
  filler[filler_size] = '\0';

  for (int i = 0; i < filler_count; i++) {
    char keybuf[32];
    snprintf(keybuf, sizeof(keybuf), "pad_%08d", i);
    s = Lithos_Put(db, keybuf, filler);
    if (!Status_IsOK(s)) {
      fprintf(stderr, "filler put failed at %d: %s\n", i, Status_ToString(s));
      Status_Free(s);
      free(filler);
      Lithos_Close(db);
      return 1;
    }
  }
  free(filler);

  Lithos_Close(db);

  char sst_path[512];
  if (!find_latest_sst(dbpath, sst_path, sizeof(sst_path))) {
    fprintf(stderr, "No SST file found in %s\n", dbpath);
    return 1;
  }

  if (!flip_random_bit(sst_path)) {
    fprintf(stderr, "Failed to corrupt SST %s\n", sst_path);
    return 1;
  }

  if (detect_corruption_via_table(sst_path, &opt)) {
    return 0;
  }

  db = NULL;
  s = Lithos_Open(dbpath, &opt, &db);
  if (!Status_IsOK(s)) {
    fprintf(stderr, "reopen: %s\n", Status_ToString(s));
    Status_Free(s);
    return 0;
  }

  char *val = NULL;
  s = Lithos_Get(db, "integrity_test", NULL, &val);
  if (Status_IsOK(s)) {
    printf("Unexpected success: value=%s\n", val ? val : "<null>");
    Lithos_Free(val);
    Lithos_Close(db);
    return 1;
  }

  if (Status_IsCorruption(s)) {
    printf("Detected corruption as expected: %s\n", Status_ToString(s));
    Status_Free(s);
    Lithos_Close(db);
    return 0;
  }

  if (Status_IsIOError(s)) {
    printf("Detected I/O error as expected: %s\n", Status_ToString(s));
    Status_Free(s);
    Lithos_Close(db);
    return 0;
  }

  if (Status_IsNotFound(s)) {
    printf("Key not found after corruption (treated as failure to detect "
           "corruption)\n");
    Status_Free(s);
    Lithos_Close(db);
    return 1;
  }

  printf("Unexpected status: %s\n", Status_ToString(s));
  Status_Free(s);
  Lithos_Close(db);
  return 1;
}
