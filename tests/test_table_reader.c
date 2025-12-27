
#include "all_tests.h"
#include "core/table/block.h"
#include "core/table/table.h"
#include "core/table/table_builder.h"
#include "testharness.h"
#include "util/env.h"
#include "util/status.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static const char *CreateTestTable(const char *filename, int num_entries) {
  unlink(filename);

  Lithos_WritableFile *file;
  Status s = Env_NewWritableFile(filename, &file);
  if (s.code != LITHOS_OK) {
    return "Failed to create file";
  }

  Lithos_Options options;
  Lithos_Options_InitDefault(&options);
  options.block_size = 1024;

  Lithos_TableBuilder *tb = TableBuilder_Create(&options, file);
  if (!tb) {
    WritableFile_Close(file);
    return "Failed to create TableBuilder";
  }

  for (int i = 0; i < num_entries; i++) {
    char key[64], value[128];
    snprintf(key, sizeof(key), "key%05d", i);
    snprintf(value, sizeof(value), "value%05d_some_padding_to_fill_space", i);

    Lithos_Slice k = Slice_FromCString(key);
    Lithos_Slice v = Slice_FromCString(value);
    lithos_status_code status = TableBuilder_Add(tb, k, v);
    if (status != LITHOS_OK) {
      TableBuilder_Destroy(tb);
      WritableFile_Close(file);
      return "Failed to add entry";
    }
  }

  lithos_status_code status = TableBuilder_Finish(tb);
  if (status != LITHOS_OK) {
    TableBuilder_Destroy(tb);
    WritableFile_Close(file);
    return "Failed to finish table";
  }

  TableBuilder_Destroy(tb);
  WritableFile_Close(file);
  return NULL;
}

static void Test_TableReader_Open(void) {
  printf("[TEST] TableReader Open                        ");

  const char *filename = "/tmp/lithos_test_reader_open.sst";
  const char *err = CreateTestTable(filename, 100);
  ASSERT_TRUE(err == NULL);

  Lithos_RandomAccessFile *file;
  Status s = Env_NewRandomAccessFile(filename, &file);
  ASSERT_TRUE(s.code == LITHOS_OK);

  FILE *fp = fopen(filename, "rb");
  fseek(fp, 0, SEEK_END);
  uint64_t file_size = ftell(fp);
  fclose(fp);

  Lithos_Options options;
  Lithos_Options_InitDefault(&options);

  Lithos_Table *table;
  s = Table_Open(&options, file, file_size, &table);
  ASSERT_TRUE(s.code == LITHOS_OK);
  ASSERT_TRUE(table != NULL);

  Table_Destroy(table);
  unlink(filename);

  printf("✓ (%d assertions)\n", test_passed);
}

static void Test_TableReader_SeekToFirst(void) {
  printf("[TEST] TableReader SeekToFirst                 ");

  const char *filename = "/tmp/lithos_test_reader_first.sst";
  const char *err = CreateTestTable(filename, 50);
  ASSERT_TRUE(err == NULL);

  Lithos_RandomAccessFile *file;
  Status s = Env_NewRandomAccessFile(filename, &file);
  ASSERT_TRUE(s.code == LITHOS_OK);

  FILE *fp = fopen(filename, "rb");
  fseek(fp, 0, SEEK_END);
  uint64_t file_size = ftell(fp);
  fclose(fp);

  Lithos_Options options;
  Lithos_Options_InitDefault(&options);

  Lithos_Table *table;
  s = Table_Open(&options, file, file_size, &table);
  ASSERT_TRUE(s.code == LITHOS_OK);

  Lithos_Iterator *iter = Table_NewIterator(table, &options);
  ASSERT_TRUE(iter != NULL);

  Lithos_Iter_SeekToFirst(iter);
  ASSERT_TRUE(Lithos_Iter_Valid(iter));

  Lithos_Slice key = Lithos_Iter_Key(iter);
  char expected_key[64];
  snprintf(expected_key, sizeof(expected_key), "key%05d", 0);
  ASSERT_TRUE(key.size == strlen(expected_key));
  ASSERT_TRUE(memcmp(key.data, expected_key, key.size) == 0);

  Lithos_Iter_Destroy(iter);
  Table_Destroy(table);
  unlink(filename);

  printf("✓ (%d assertions)\n", test_passed);
}

static void Test_TableReader_Seek(void) {
  printf("[TEST] TableReader Seek                        ");

  const char *filename = "/tmp/lithos_test_reader_seek.sst";
  const char *err = CreateTestTable(filename, 100);
  ASSERT_TRUE(err == NULL);

  Lithos_RandomAccessFile *file;
  Status s = Env_NewRandomAccessFile(filename, &file);
  ASSERT_TRUE(s.code == LITHOS_OK);

  FILE *fp = fopen(filename, "rb");
  fseek(fp, 0, SEEK_END);
  uint64_t file_size = ftell(fp);
  fclose(fp);

  Lithos_Options options;
  Lithos_Options_InitDefault(&options);

  Lithos_Table *table;
  s = Table_Open(&options, file, file_size, &table);
  ASSERT_TRUE(s.code == LITHOS_OK);

  Lithos_Iterator *iter = Table_NewIterator(table, &options);
  ASSERT_TRUE(iter != NULL);

  char target[64];
  snprintf(target, sizeof(target), "key%05d", 50);
  Lithos_Slice target_slice = Slice_FromCString(target);

  Lithos_Iter_Seek(iter, target_slice);
  ASSERT_TRUE(Lithos_Iter_Valid(iter));

  Lithos_Slice key = Lithos_Iter_Key(iter);
  ASSERT_TRUE(Slice_Compare(key, target_slice) == 0);

  Lithos_Slice value = Lithos_Iter_Value(iter);
  char expected_value[128];
  snprintf(expected_value, sizeof(expected_value),
           "value%05d_some_padding_to_fill_space", 50);
  ASSERT_TRUE(value.size == strlen(expected_value));
  ASSERT_TRUE(memcmp(value.data, expected_value, value.size) == 0);

  Lithos_Iter_Destroy(iter);
  Table_Destroy(table);
  unlink(filename);

  printf("✓ (%d assertions)\n", test_passed);
}

static void Test_TableReader_FullScan(void) {
  printf("[TEST] TableReader Full Scan                   ");

  const char *filename = "/tmp/lithos_test_reader_scan.sst";
  int num_entries = 100;
  const char *err = CreateTestTable(filename, num_entries);
  ASSERT_TRUE(err == NULL);

  Lithos_RandomAccessFile *file;
  Status s = Env_NewRandomAccessFile(filename, &file);
  ASSERT_TRUE(s.code == LITHOS_OK);

  FILE *fp = fopen(filename, "rb");
  fseek(fp, 0, SEEK_END);
  uint64_t file_size = ftell(fp);
  fclose(fp);

  Lithos_Options options;
  Lithos_Options_InitDefault(&options);

  Lithos_Table *table;
  s = Table_Open(&options, file, file_size, &table);
  ASSERT_TRUE(s.code == LITHOS_OK);

  Lithos_Iterator *iter = Table_NewIterator(table, &options);
  ASSERT_TRUE(iter != NULL);

  int count = 0;
  Lithos_Iter_SeekToFirst(iter);

  while (Lithos_Iter_Valid(iter)) {
    Lithos_Slice key = Lithos_Iter_Key(iter);

    char expected_key[64];
    snprintf(expected_key, sizeof(expected_key), "key%05d", count);
    ASSERT_TRUE(key.size == strlen(expected_key));
    ASSERT_TRUE(memcmp(key.data, expected_key, key.size) == 0);

    count++;
    Lithos_Iter_Next(iter);
  }

  ASSERT_TRUE(count == num_entries);

  s = Lithos_Iter_GetStatus(iter);
  ASSERT_TRUE(s.code == LITHOS_OK);

  Lithos_Iter_Destroy(iter);
  Table_Destroy(table);
  unlink(filename);

  printf("  Scanned %d entries\n", count);
  printf("  ✓ (%d assertions)\n", test_passed);
}

static void Test_TableReader_SeekMissing(void) {
  printf("[TEST] TableReader Seek Missing                ");

  const char *filename = "/tmp/lithos_test_reader_missing.sst";
  const char *err = CreateTestTable(filename, 100);
  ASSERT_TRUE(err == NULL);

  Lithos_RandomAccessFile *file;
  Status s = Env_NewRandomAccessFile(filename, &file);
  ASSERT_TRUE(s.code == LITHOS_OK);

  FILE *fp = fopen(filename, "rb");
  fseek(fp, 0, SEEK_END);
  uint64_t file_size = ftell(fp);
  fclose(fp);

  Lithos_Options options;
  Lithos_Options_InitDefault(&options);

  Lithos_Table *table;
  s = Table_Open(&options, file, file_size, &table);
  ASSERT_TRUE(s.code == LITHOS_OK);

  Lithos_Iterator *iter = Table_NewIterator(table, &options);
  ASSERT_TRUE(iter != NULL);

  char target[64];
  snprintf(target, sizeof(target), "key00049a");
  Lithos_Slice target_slice = Slice_FromCString(target);

  Lithos_Iter_Seek(iter, target_slice);

  if (Lithos_Iter_Valid(iter)) {
    Lithos_Slice key = Lithos_Iter_Key(iter);
    char expected[64];
    snprintf(expected, sizeof(expected), "key%05d", 50);
    ASSERT_TRUE(Slice_Compare(key, Slice_FromCString(expected)) == 0);
  }

  Lithos_Iter_Destroy(iter);
  Table_Destroy(table);
  unlink(filename);

  printf("✓ (%d assertions)\n", test_passed);
}

static void Test_TableReader_BlockBoundary(void) {
  printf("[TEST] TableReader Block Boundary              ");

  const char *filename = "/tmp/lithos_test_reader_boundary.sst";

  int num_entries = 200;
  const char *err = CreateTestTable(filename, num_entries);
  ASSERT_TRUE(err == NULL);

  Lithos_RandomAccessFile *file;
  Status s = Env_NewRandomAccessFile(filename, &file);
  ASSERT_TRUE(s.code == LITHOS_OK);

  FILE *fp = fopen(filename, "rb");
  fseek(fp, 0, SEEK_END);
  uint64_t file_size = ftell(fp);
  fclose(fp);

  Lithos_Options options;
  Lithos_Options_InitDefault(&options);
  options.block_size = 512;

  Lithos_Table *table;
  s = Table_Open(&options, file, file_size, &table);
  ASSERT_TRUE(s.code == LITHOS_OK);

  Lithos_Iterator *iter = Table_NewIterator(table, &options);
  ASSERT_TRUE(iter != NULL);

  int count = 0;
  Lithos_Iter_SeekToFirst(iter);

  while (Lithos_Iter_Valid(iter)) {
    count++;
    Lithos_Iter_Next(iter);
  }

  ASSERT_TRUE(count == num_entries);

  Lithos_Iter_Destroy(iter);
  Table_Destroy(table);
  unlink(filename);

  printf("  Crossed boundaries for %d entries\n", count);
  printf("  ✓ (%d assertions)\n", test_passed);
}

static void Test_TableReader_LargeTable(void) {
  printf("[TEST] TableReader Large Table (10K)           ");

  const char *filename = "/tmp/lithos_test_reader_large.sst";
  int num_entries = 10000;
  const char *err = CreateTestTable(filename, num_entries);
  ASSERT_TRUE(err == NULL);

  Lithos_RandomAccessFile *file;
  Status s = Env_NewRandomAccessFile(filename, &file);
  ASSERT_TRUE(s.code == LITHOS_OK);

  FILE *fp = fopen(filename, "rb");
  fseek(fp, 0, SEEK_END);
  uint64_t file_size = ftell(fp);
  fclose(fp);

  printf("  File size: %lu bytes\n", (unsigned long)file_size);

  Lithos_Options options;
  Lithos_Options_InitDefault(&options);

  Lithos_Table *table;
  s = Table_Open(&options, file, file_size, &table);
  ASSERT_TRUE(s.code == LITHOS_OK);

  Lithos_Iterator *iter = Table_NewIterator(table, &options);
  ASSERT_TRUE(iter != NULL);

  for (int i = 0; i < 100; i++) {
    int target_idx = (i * 97) % num_entries;
    char target[64];
    snprintf(target, sizeof(target), "key%05d", target_idx);

    Lithos_Iter_Seek(iter, Slice_FromCString(target));
    ASSERT_TRUE(Lithos_Iter_Valid(iter));

    Lithos_Slice key = Lithos_Iter_Key(iter);
    ASSERT_TRUE(Slice_Compare(key, Slice_FromCString(target)) == 0);
  }

  int count = 0;
  Lithos_Iter_SeekToFirst(iter);
  while (Lithos_Iter_Valid(iter)) {
    count++;
    Lithos_Iter_Next(iter);
  }

  ASSERT_TRUE(count == num_entries);

  printf("  Verified %d seeks + full scan\n", 100);

  Lithos_Iter_Destroy(iter);
  Table_Destroy(table);
  unlink(filename);

  printf("  ✓ (%d assertions)\n", test_passed);
}

void Run_TableReaderTests(void) {
  Test_TableReader_Open();
  Test_TableReader_SeekToFirst();
  Test_TableReader_Seek();
  Test_TableReader_FullScan();
  Test_TableReader_SeekMissing();
  Test_TableReader_BlockBoundary();
  Test_TableReader_LargeTable();
}
