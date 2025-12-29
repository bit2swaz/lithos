
#include "all_tests.h"
#include "core/dbformat.h"
#include "core/table/format.h"
#include "core/table/table.h"
#include "core/table/table_builder.h"
#include "testharness.h"
#include "util/coding.h"
#include "util/env.h"
#include "util/status.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static Lithos_Slice MakeInternalKey(char *buf, size_t buf_size,
                                    const char *user_key, SequenceNumber seq) {
  size_t ukey_len = strlen(user_key);
  if (ukey_len + 8 > buf_size) {
    return (Lithos_Slice){NULL, 0};
  }
  memcpy(buf, user_key, ukey_len);
  uint64_t packed = PackSequenceAndType(seq, kTypeValue);
  EncodeFixed64(buf + ukey_len, packed);
  return (Lithos_Slice){buf, ukey_len + 8};
}

static void Test_TableBuilder_Empty(void) {
  printf("[TEST] TableBuilder Empty                      ");

  const char *filename = "/tmp/lithos_test_empty.sst";
  unlink(filename);

  Lithos_WritableFile *file;
  Status s = Env_NewWritableFile(filename, &file);
  ASSERT_TRUE(s.code == LITHOS_OK);

  Lithos_Options options;
  Lithos_Options_InitDefault(&options);

  Lithos_TableBuilder *tb = TableBuilder_Create(&options, file);
  ASSERT_TRUE(tb != NULL);

  lithos_status_code status = TableBuilder_Finish(tb);
  ASSERT_TRUE(status == LITHOS_OK);

  uint64_t file_size = TableBuilder_FileSize(tb);
  ASSERT_TRUE(file_size > 0);

  TableBuilder_Destroy(tb);
  WritableFile_Close(file);

  FILE *fp = fopen(filename, "rb");
  ASSERT_TRUE(fp != NULL);

  fseek(fp, 0, SEEK_END);
  long actual_size = ftell(fp);
  ASSERT_TRUE((uint64_t)actual_size == file_size);

  fseek(fp, -LITHOS_FOOTER_ENCODED_LENGTH, SEEK_END);
  char footer_buf[LITHOS_FOOTER_ENCODED_LENGTH];
  size_t n = fread(footer_buf, 1, LITHOS_FOOTER_ENCODED_LENGTH, fp);
  ASSERT_TRUE(n == LITHOS_FOOTER_ENCODED_LENGTH);

  uint64_t magic = DecodeFixed64(footer_buf + LITHOS_FOOTER_ENCODED_LENGTH - 8);
  ASSERT_TRUE(magic == LITHOS_TABLE_MAGIC_NUMBER);

  fclose(fp);
  unlink(filename);

  printf("✓ (%d assertions)\n", test_passed);
}

static void Test_TableBuilder_SingleEntry(void) {
  printf("[TEST] TableBuilder Single Entry               ");

  const char *filename = "/tmp/lithos_test_single.sst";
  unlink(filename);

  Lithos_WritableFile *file;
  Status s = Env_NewWritableFile(filename, &file);
  ASSERT_TRUE(s.code == LITHOS_OK);

  Lithos_Options options;
  Lithos_Options_InitDefault(&options);

  Lithos_TableBuilder *tb = TableBuilder_Create(&options, file);
  ASSERT_TRUE(tb != NULL);

  char keybuf[64];
  Lithos_Slice key = MakeInternalKey(keybuf, sizeof(keybuf), "testkey", 1);
  Lithos_Slice value = Slice_FromCString("testvalue");
  lithos_status_code status = TableBuilder_Add(tb, key, value);
  ASSERT_TRUE(status == LITHOS_OK);

  ASSERT_TRUE(TableBuilder_NumEntries(tb) == 1);

  status = TableBuilder_Finish(tb);
  ASSERT_TRUE(status == LITHOS_OK);

  uint64_t file_size = TableBuilder_FileSize(tb);
  ASSERT_TRUE(file_size > 0);

  TableBuilder_Destroy(tb);
  WritableFile_Close(file);

  unlink(filename);

  printf("✓ (%d assertions)\n", test_passed);
}

static void Test_TableBuilder_MultipleEntries(void) {
  printf("[TEST] TableBuilder Multiple Entries           ");

  const char *filename = "/tmp/lithos_test_multi.sst";
  unlink(filename);

  Lithos_WritableFile *file;
  Status s = Env_NewWritableFile(filename, &file);
  ASSERT_TRUE(s.code == LITHOS_OK);

  Lithos_Options options;
  Lithos_Options_InitDefault(&options);

  Lithos_TableBuilder *tb = TableBuilder_Create(&options, file);
  ASSERT_TRUE(tb != NULL);

  for (int i = 0; i < 100; i++) {
    char userkey[64], value[64], keybuf[80];
    snprintf(userkey, sizeof(userkey), "key%05d", i);
    snprintf(value, sizeof(value), "value%05d", i);

    Lithos_Slice k = MakeInternalKey(keybuf, sizeof(keybuf), userkey, 100 - i);
    Lithos_Slice v = Slice_FromCString(value);
    lithos_status_code status = TableBuilder_Add(tb, k, v);
    ASSERT_TRUE(status == LITHOS_OK);
  }

  ASSERT_TRUE(TableBuilder_NumEntries(tb) == 100);

  lithos_status_code status = TableBuilder_Finish(tb);
  ASSERT_TRUE(status == LITHOS_OK);

  uint64_t file_size = TableBuilder_FileSize(tb);
  ASSERT_TRUE(file_size > 0);

  TableBuilder_Destroy(tb);
  WritableFile_Close(file);

  unlink(filename);

  printf("✓ (%d assertions)\n", test_passed);
}

static void Test_TableBuilder_MultipleBlocks(void) {
  printf("[TEST] TableBuilder Multiple Blocks            ");

  const char *filename = "/tmp/lithos_test_blocks.sst";
  unlink(filename);

  Lithos_WritableFile *file;
  Status s = Env_NewWritableFile(filename, &file);
  ASSERT_TRUE(s.code == LITHOS_OK);

  Lithos_Options options;
  Lithos_Options_InitDefault(&options);
  options.block_size = 1024;

  Lithos_TableBuilder *tb = TableBuilder_Create(&options, file);
  ASSERT_TRUE(tb != NULL);

  char large_value[512];
  memset(large_value, 'v', sizeof(large_value));
  large_value[511] = '\0';

  int num_keys = 50;
  for (int i = 0; i < num_keys; i++) {
    char userkey[64], keybuf[80];
    snprintf(userkey, sizeof(userkey), "bigkey%05d", i);

    Lithos_Slice k = MakeInternalKey(keybuf, sizeof(keybuf), userkey, 100 - i);
    Lithos_Slice v = {large_value, 512};
    lithos_status_code status = TableBuilder_Add(tb, k, v);
    ASSERT_TRUE(status == LITHOS_OK);
  }

  ASSERT_TRUE(TableBuilder_NumEntries(tb) == (uint64_t)num_keys);

  lithos_status_code status = TableBuilder_Finish(tb);
  ASSERT_TRUE(status == LITHOS_OK);

  uint64_t file_size = TableBuilder_FileSize(tb);
  ASSERT_TRUE(file_size >
              10000);

  printf("  File size: %lu bytes\n", (unsigned long)file_size);

  TableBuilder_Destroy(tb);
  WritableFile_Close(file);

  unlink(filename);

  printf("  ✓ (%d assertions)\n", test_passed);
}

static void Test_TableBuilder_LargeTable(void) {
  printf("[TEST] TableBuilder Large Table (10K entries)  ");

  const char *filename = "/tmp/lithos_test_large.sst";
  unlink(filename);

  Lithos_WritableFile *file;
  Status s = Env_NewWritableFile(filename, &file);
  ASSERT_TRUE(s.code == LITHOS_OK);

  Lithos_Options options;
  Lithos_Options_InitDefault(&options);

  Lithos_TableBuilder *tb = TableBuilder_Create(&options, file);
  ASSERT_TRUE(tb != NULL);

  int num_entries = 10000;
  for (int i = 0; i < num_entries; i++) {
    char userkey[64], value[128], keybuf[80];
    snprintf(userkey, sizeof(userkey), "largekey%08d", i);
    snprintf(value, sizeof(value),
             "This is test value number %d with some extra padding", i);

    Lithos_Slice k = MakeInternalKey(keybuf, sizeof(keybuf), userkey, 20000 - i);
    Lithos_Slice v = Slice_FromCString(value);
    lithos_status_code status = TableBuilder_Add(tb, k, v);
    ASSERT_TRUE(status == LITHOS_OK);
  }

  ASSERT_TRUE(TableBuilder_NumEntries(tb) == (uint64_t)num_entries);

  lithos_status_code status = TableBuilder_Finish(tb);
  ASSERT_TRUE(status == LITHOS_OK);

  uint64_t file_size = TableBuilder_FileSize(tb);
  ASSERT_TRUE(file_size > 100000);

  printf("  File size: %lu bytes, Entries: %lu\n", (unsigned long)file_size,
         (unsigned long)TableBuilder_NumEntries(tb));

  TableBuilder_Destroy(tb);
  WritableFile_Close(file);

  FILE *fp = fopen(filename, "rb");
  ASSERT_TRUE(fp != NULL);

  fseek(fp, -LITHOS_FOOTER_ENCODED_LENGTH, SEEK_END);
  char footer_buf[LITHOS_FOOTER_ENCODED_LENGTH];
  size_t n = fread(footer_buf, 1, LITHOS_FOOTER_ENCODED_LENGTH, fp);
  ASSERT_TRUE(n == LITHOS_FOOTER_ENCODED_LENGTH);

  Lithos_Footer footer;
  lithos_status_code decode_status = Footer_Decode(&footer, footer_buf);
  ASSERT_TRUE(decode_status == LITHOS_OK);
  ASSERT_TRUE(BlockHandle_IsValid(&footer.index_handle));
  ASSERT_TRUE(BlockHandle_IsValid(&footer.metaindex_handle));

  printf("  Index offset: %lu, size: %lu\n",
         (unsigned long)footer.index_handle.offset,
         (unsigned long)footer.index_handle.size);

  fclose(fp);
  unlink(filename);

  printf("  ✓ (%d assertions)\n", test_passed);
}

static void Test_TableBuilder_Compression(void) {
  printf("[TEST] TableBuilder Compression (RLE)         ");

  const char *filename = "/tmp/lithos_test_compression.sst";
  unlink(filename);

  Lithos_WritableFile *file;
  Status s = Env_NewWritableFile(filename, &file);
  ASSERT_TRUE(s.code == LITHOS_OK);

  Lithos_Options options;
  Lithos_Options_InitDefault(&options);
  options.compression_enabled = true;
  options.block_size =
      512;

  Lithos_TableBuilder *tb = TableBuilder_Create(&options, file);
  ASSERT_TRUE(tb != NULL);

  const int num_entries = 40;
  char value[128];
  memset(value, 'Z', sizeof(value));
  value[sizeof(value) - 1] = '\0';

  for (int i = 0; i < num_entries; i++) {
    char userkey[64], keybuf[80];
    snprintf(userkey, sizeof(userkey), "ckey%05d", i);
    Lithos_Slice k = MakeInternalKey(keybuf, sizeof(keybuf), userkey, 1000 - i);
    Lithos_Slice v = {value, strlen(value)};
    lithos_status_code status = TableBuilder_Add(tb, k, v);
    ASSERT_TRUE(status == LITHOS_OK);
  }

  lithos_status_code finish_status = TableBuilder_Finish(tb);
  ASSERT_TRUE(finish_status == LITHOS_OK);
  TableBuilder_Destroy(tb);
  WritableFile_Close(file);

  Lithos_RandomAccessFile *ra_file;
  s = Env_NewRandomAccessFile(filename, &ra_file);
  ASSERT_TRUE(s.code == LITHOS_OK);

  FILE *fp = fopen(filename, "rb");
  ASSERT_TRUE(fp != NULL);
  fseek(fp, 0, SEEK_END);
  uint64_t file_size = ftell(fp);
  fclose(fp);

  Lithos_Table *table;
  Lithos_Options read_opts;
  Lithos_Options_InitDefault(&read_opts);
  Status open_status = Table_Open(&read_opts, ra_file, file_size, &table);
  ASSERT_TRUE(open_status.code == LITHOS_OK);

  Lithos_Iterator *iter = Table_NewIterator(table, &read_opts);
  ASSERT_TRUE(iter != NULL);
  Lithos_Iter_SeekToFirst(iter);

  for (int i = 0; i < num_entries; i++) {
    ASSERT_TRUE(Lithos_Iter_Valid(iter));
    Lithos_Slice key = Lithos_Iter_Key(iter);
    Lithos_Slice val = Lithos_Iter_Value(iter);
    char expected_key[64];
    snprintf(expected_key, sizeof(expected_key), "ckey%05d", i);
    size_t ukey_len = strlen(expected_key);
    ASSERT_TRUE(key.size == ukey_len + 8);
    ASSERT_TRUE(memcmp(key.data, expected_key, ukey_len) == 0);
    ASSERT_TRUE(val.size == strlen(value));
    ASSERT_TRUE(memcmp(val.data, value, val.size) == 0);
    Lithos_Iter_Next(iter);
  }
  ASSERT_TRUE(!Lithos_Iter_Valid(iter));

  Lithos_Iter_Destroy(iter);
  Table_Destroy(table);
  unlink(filename);

  printf("  ✓ (%d assertions)\n", test_passed);
}

void Run_TableBuilderTests(void) {
  Test_TableBuilder_Empty();
  Test_TableBuilder_SingleEntry();
  Test_TableBuilder_MultipleEntries();
  Test_TableBuilder_MultipleBlocks();
  Test_TableBuilder_LargeTable();
  Test_TableBuilder_Compression();
}
