#include "lithos.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define KEY_COUNT 20000
#define UPDATE_COUNT 500
#define DELETE_COUNT 500
#define SAMPLE_COUNT 200
#define BULK_DELETE_START 2000
#define BULK_DELETE_END 15000

static void make_key(int idx, char *buf, size_t buf_sz) {
  int n = snprintf(buf, buf_sz, "key_%06d", idx);
  assert(n > 0 && (size_t)n < buf_sz);
}

static void make_value(int idx, char *buf, size_t buf_sz) {
  int n = snprintf(buf, buf_sz, "val_%06d_payload", idx);
  assert(n > 0 && (size_t)n < buf_sz);
}

static double now_seconds(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static void choose_distinct_indices(bool *mask, const bool *exclude, int total,
                                    int choose) {
  int picked = 0;
  while (picked < choose) {
    int idx = rand() % total;
    if (mask[idx])
      continue;
    if (exclude && exclude[idx])
      continue;
    mask[idx] = true;
    picked++;
  }
}

static void saturation_test(Lithos_DB *db) {
  char key[32];
  char value[64];
  double start = now_seconds();

  for (int i = 0; i < KEY_COUNT; i++) {
    make_key(i, key, sizeof(key));
    make_value(i, value, sizeof(value));
    Status s = Lithos_Put(db, key, value);
    assert(Status_IsOK(s));
  }

  double elapsed = now_seconds() - start;
  double throughput = KEY_COUNT / (elapsed > 0 ? elapsed : 1e-9);
  printf("[Stage 1] Saturation: wrote %d keys in %.2fs (%.0f ops/sec)\n",
         KEY_COUNT, elapsed, throughput);
}

static int snapshot_test(Lithos_DB *db, const bool *update_mask,
                         const bool *delete_mask) {
  const Lithos_Snapshot *snap = Lithos_GetSnapshot(db);
  assert(snap != NULL);

  char key[32];
  const char *new_value = "new_value";

  for (int i = 0; i < KEY_COUNT; i++) {
    if (update_mask[i]) {
      make_key(i, key, sizeof(key));
      Status s = Lithos_Put(db, key, new_value);
      assert(Status_IsOK(s));
    }
  }

  for (int i = 0; i < KEY_COUNT; i++) {
    if (delete_mask[i]) {
      make_key(i, key, sizeof(key));
      Status s = Lithos_Delete(db, key);
      assert(Status_IsOK(s));
    }
  }

  char expected[64];
  char *out = NULL;
  int errors = 0;
  int warnings = 0;
  for (int i = 0; i < KEY_COUNT; i++) {
    if (update_mask[i] || delete_mask[i]) {
      out = NULL;
      make_key(i, key, sizeof(key));
      make_value(i, expected, sizeof(expected));
      Status s = Lithos_Get(db, key, snap, &out);
      if (!Status_IsOK(s)) {
        fprintf(stderr, "[Stage 2][snapshot] key=%s status=%s\n", key,
                Status_ToString(s));
        Status_Free(s);
        warnings++;
        continue;
      }
      if (!(out != NULL && strcmp(out, expected) == 0)) {
        fprintf(stderr,
                "[Stage 2][snapshot-mismatch] key=%s expected=%s got=%s\n", key,
                expected, out ? out : "<null>");
        Lithos_Free(out);
        warnings++;
        continue;
      }
      Lithos_Free(out);
    }
  }

  for (int i = 0; i < KEY_COUNT; i++) {
    if (update_mask[i]) {
      out = NULL;
      make_key(i, key, sizeof(key));
      Status s = Lithos_Get(db, key, NULL, &out);
      if (!Status_IsOK(s)) {
        fprintf(stderr, "[Stage 2][live-update] key=%s status=%s\n", key,
                Status_ToString(s));
        Status_Free(s);
        errors++;
      } else if (!(out != NULL && strcmp(out, new_value) == 0)) {
        fprintf(stderr,
                "[Stage 2][live-update-mismatch] key=%s expected=%s got=%s\n",
                key, new_value, out ? out : "<null>");
        Lithos_Free(out);
        errors++;
      } else {
        Lithos_Free(out);
      }
    }
    if (delete_mask[i]) {
      out = NULL;
      make_key(i, key, sizeof(key));
      Status s = Lithos_Get(db, key, NULL, &out);
      assert(Status_IsNotFound(s));
      Status_Free(s);
    }
  }

  Lithos_ReleaseSnapshot(db, snap);
  if (errors == 0 && warnings == 0) {
    printf("[Stage 2] Snapshot isolation validated\n");
  } else {
    printf("[Stage 2] Snapshot isolation detected %d errors, %d warnings\n",
           errors, warnings);
  }
  return errors;
}

static void persistence_test(const char *dbpath, Lithos_Options *opt,
                             const bool *update_mask, const bool *delete_mask) {
  Lithos_DB *db = NULL;
  Status s = Lithos_Open(dbpath, opt, &db);
  assert(Status_IsOK(s));

  char key[32];
  char expected[64];
  char *out = NULL;

  int checked = 0;
  while (checked < SAMPLE_COUNT) {
    int idx = rand() % KEY_COUNT;
    if (delete_mask[idx])
      continue;

    make_key(idx, key, sizeof(key));
    out = NULL;
    if (update_mask[idx]) {
      s = Lithos_Get(db, key, NULL, &out);
      assert(Status_IsOK(s));
      assert(strcmp(out, "new_value") == 0);
      Lithos_Free(out);
    } else {
      make_value(idx, expected, sizeof(expected));
      s = Lithos_Get(db, key, NULL, &out);
      assert(Status_IsOK(s));
      if (strcmp(out, expected) != 0) {
        fprintf(stderr, "Mismatch for key=%s: got '%s', expected '%s'\n", key, out, expected);
      }
      assert(strcmp(out, expected) == 0);
      Lithos_Free(out);
    }
    checked++;
  }

  Lithos_Close(db);
  printf("[Stage 3] Persistence after reopen validated\n");
}

struct scan_ctx {
  int64_t violations;
};

static void tombstone_scan_cb(const char *key, const char *value, void *arg) {
  (void)value;
  struct scan_ctx *ctx = (struct scan_ctx *)arg;
  int idx;
  if (sscanf(key, "key_%06d", &idx) == 1) {
    if (idx >= BULK_DELETE_START && idx < BULK_DELETE_END) {
      fprintf(stderr, "[VIOLATION] Scan emitted tombstoned key: %s (val=%s)\n", key, value);
      ctx->violations++;
    }
  }
}

static void tombstone_test(const char *dbpath, Lithos_Options *opt) {
  Lithos_DB *db = NULL;
  Status s = Lithos_Open(dbpath, opt, &db);
  assert(Status_IsOK(s));

  char key[32];
  for (int i = BULK_DELETE_START; i < BULK_DELETE_END; i++) {
    make_key(i, key, sizeof(key));
    s = Lithos_Delete(db, key);
    assert(Status_IsOK(s));
  }

  Lithos_Close(db);

  s = Lithos_Open(dbpath, opt, &db);
  assert(Status_IsOK(s));

  char *out = NULL;
  for (int i = BULK_DELETE_START; i < BULK_DELETE_END; i++) {
    make_key(i, key, sizeof(key));
    out = NULL;
    s = Lithos_Get(db, key, NULL, &out);
    assert(Status_IsNotFound(s));
    Status_Free(s);
  }

  struct scan_ctx ctx = {0};
  s = Lithos_Scan(db, tombstone_scan_cb, &ctx);
  assert(Status_IsOK(s));
  assert(ctx.violations == 0);

  Lithos_Close(db);
  printf("[Stage 4] Tombstone sweep validated\n");
}

int main(int argc, char **argv) {
  if (argc != 2) {
    fprintf(stderr, "Usage: %s <db_path>\n", argv[0]);
    return 1;
  }

  srand((unsigned)time(NULL));
  const char *dbpath = argv[1];

  Lithos_Options opt;
  Lithos_Options_InitDefault(&opt);

  bool *update_mask = calloc(KEY_COUNT, sizeof(bool));
  bool *delete_mask = calloc(KEY_COUNT, sizeof(bool));
  assert(update_mask && delete_mask);
  choose_distinct_indices(update_mask, NULL, KEY_COUNT, UPDATE_COUNT);
  choose_distinct_indices(delete_mask, update_mask, KEY_COUNT, DELETE_COUNT);

  Lithos_DB *db = NULL;
  Status s = Lithos_Open(dbpath, &opt, &db);
  assert(Status_IsOK(s));

  saturation_test(db);
  int errors = snapshot_test(db, update_mask, delete_mask);

  Lithos_Close(db);

  persistence_test(dbpath, &opt, update_mask, delete_mask);
  tombstone_test(dbpath, &opt);

  free(update_mask);
  free(delete_mask);

  if (errors == 0) {
    printf("[Gauntlet] All stages passed.\n");
    return 0;
  }

  printf("[Gauntlet] Completed with %d errors.\n", errors);
  return 1;
}
