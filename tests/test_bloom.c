
#include "all_tests.h"
#include "core/table/filter_block.h"
#include "core/table/table.h"
#include "core/table/table_builder.h"
#include "lithos/filter_policy.h"
#include "testharness.h"
#include "util/env.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void Test_Bloom_Basic(void) {
  printf("[TEST] Bloom Filter Basic                      ");

  const Lithos_FilterPolicy *policy = NewBloomFilterPolicy(10);
  ASSERT_TRUE(policy != NULL);

  const char *keys_data[] = {"hello", "world", "foo", "bar"};
  Lithos_Slice keys[4];
  for (int i = 0; i < 4; i++) {
    keys[i] = Slice_FromCString(keys_data[i]);
  }

  char *filter = NULL;
  size_t filter_len = 0;
  size_t filter_capacity = 0;

  FilterPolicy_CreateFilter(policy, keys, 4, &filter, &filter_len,
                            &filter_capacity);
  ASSERT_TRUE(filter != NULL);
  ASSERT_TRUE(filter_len > 0);

  Lithos_Slice filter_slice = {filter, filter_len};

  for (int i = 0; i < 4; i++) {
    bool match = FilterPolicy_KeyMayMatch(policy, keys[i], filter_slice);
    ASSERT_TRUE(match);
  }

  Lithos_Slice absent1 = Slice_FromCString("missing");
  Lithos_Slice absent2 = Slice_FromCString("nothere");

  (void)FilterPolicy_KeyMayMatch(policy, absent1, filter_slice);
  (void)FilterPolicy_KeyMayMatch(policy, absent2, filter_slice);

  free(filter);
  FilterPolicy_Destroy(policy);

  printf("✓ (%d assertions)\n", test_passed);
}

static void Test_Bloom_FalsePositiveRate(void) {
  printf("[TEST] Bloom Filter False Positive Rate        ");

  const Lithos_FilterPolicy *policy =
      NewBloomFilterPolicy(10);
  ASSERT_TRUE(policy != NULL);

  const int num_keys = 100;
  Lithos_Slice *keys = malloc(num_keys * sizeof(Lithos_Slice));
  char **key_storage = malloc(num_keys * sizeof(char *));

  for (int i = 0; i < num_keys; i++) {
    key_storage[i] = malloc(32);
    snprintf(key_storage[i], 32, "key%05d", i);
    keys[i] = Slice_FromCString(key_storage[i]);
  }

  char *filter = NULL;
  size_t filter_len = 0;
  size_t filter_capacity = 0;

  FilterPolicy_CreateFilter(policy, keys, num_keys, &filter, &filter_len,
                            &filter_capacity);
  Lithos_Slice filter_slice = {filter, filter_len};

  for (int i = 0; i < num_keys; i++) {
    ASSERT_TRUE(FilterPolicy_KeyMayMatch(policy, keys[i], filter_slice));
  }

  int false_positives = 0;
  const int num_tests = 10000;

  for (int i = 0; i < num_tests; i++) {
    char test_key[32];
    snprintf(test_key, sizeof(test_key), "absent%05d", i + num_keys);
    Lithos_Slice test_slice = Slice_FromCString(test_key);

    if (FilterPolicy_KeyMayMatch(policy, test_slice, filter_slice)) {
      false_positives++;
    }
  }

  double fp_rate = (double)false_positives / num_tests;
  ASSERT_TRUE(fp_rate < 0.03);

  printf("  FP rate: %.2f%% (%d/%d)\n", fp_rate * 100, false_positives,
         num_tests);

  for (int i = 0; i < num_keys; i++) {
    free(key_storage[i]);
  }
  free(key_storage);
  free(keys);
  free(filter);
  FilterPolicy_Destroy(policy);

  printf("  ✓ (%d assertions)\n", test_passed);
}

static void Test_FilterBlock_BuilderReader(void) {
  printf("[TEST] Filter Block Builder/Reader             ");

  const Lithos_FilterPolicy *policy = NewBloomFilterPolicy(10);
  ASSERT_TRUE(policy != NULL);

  FilterBlockBuilder *builder = FilterBlockBuilder_Create(policy);
  ASSERT_TRUE(builder != NULL);

  FilterBlockBuilder_StartBlock(builder, 0);
  for (int i = 0; i < 10; i++) {
    char key[32];
    snprintf(key, sizeof(key), "block0_key%d", i);
    FilterBlockBuilder_AddKey(builder, Slice_FromCString(key));
  }

  FilterBlockBuilder_StartBlock(builder, 2048);
  for (int i = 0; i < 10; i++) {
    char key[32];
    snprintf(key, sizeof(key), "block1_key%d", i);
    FilterBlockBuilder_AddKey(builder, Slice_FromCString(key));
  }

  FilterBlockBuilder_StartBlock(builder, 4096);
  for (int i = 0; i < 10; i++) {
    char key[32];
    snprintf(key, sizeof(key), "block2_key%d", i);
    FilterBlockBuilder_AddKey(builder, Slice_FromCString(key));
  }

  Lithos_Slice filter_data = FilterBlockBuilder_Finish(builder);
  ASSERT_TRUE(filter_data.size > 0);

  FilterBlockReader *reader = FilterBlockReader_Create(policy, filter_data);
  ASSERT_TRUE(reader != NULL);

  for (int i = 0; i < 10; i++) {
    char key[32];
    snprintf(key, sizeof(key), "block0_key%d", i);
    bool match =
        FilterBlockReader_KeyMayMatch(reader, 0, Slice_FromCString(key));
    ASSERT_TRUE(match);
  }

  for (int i = 0; i < 10; i++) {
    char key[32];
    snprintf(key, sizeof(key), "block1_key%d", i);
    bool match =
        FilterBlockReader_KeyMayMatch(reader, 2048, Slice_FromCString(key));
    ASSERT_TRUE(match);
  }

  for (int i = 0; i < 10; i++) {
    char key[32];
    snprintf(key, sizeof(key), "block0_key%d", i);
    (void)FilterBlockReader_KeyMayMatch(reader, 2048, Slice_FromCString(key));
  }

  FilterBlockReader_Destroy(reader);
  FilterBlockBuilder_Destroy(builder);
  FilterPolicy_Destroy(policy);

  printf("✓ (%d assertions)\n", test_passed);
}

static void Test_Bloom_TableIntegration(void) {
  printf("[TEST] Bloom Table Integration                 ");

  const char *filename = "/tmp/lithos_test_bloom_table.sst";
  unlink(filename);

  const Lithos_FilterPolicy *policy = NewBloomFilterPolicy(10);
  ASSERT_TRUE(policy != NULL);

  Lithos_WritableFile *wfile;
  Status s = Env_NewWritableFile(filename, &wfile);
  ASSERT_TRUE(s.code == LITHOS_OK);

  Lithos_Options options;
  Lithos_Options_InitDefault(&options);
  options.block_size = 1024;
  options.filter_policy = policy;

  Lithos_TableBuilder *builder = TableBuilder_Create(&options, wfile);
  ASSERT_TRUE(builder != NULL);

  for (int i = 0; i < 100; i++) {
    char key[32], value[64];
    snprintf(key, sizeof(key), "key%05d", i);
    snprintf(value, sizeof(value), "value%05d", i);

    lithos_status_code status = TableBuilder_Add(
        builder, Slice_FromCString(key), Slice_FromCString(value));
    ASSERT_TRUE(status == LITHOS_OK);
  }

  lithos_status_code status = TableBuilder_Finish(builder);
  ASSERT_TRUE(status == LITHOS_OK);

  TableBuilder_Destroy(builder);
  WritableFile_Close(wfile);

  Lithos_RandomAccessFile *rfile;
  s = Env_NewRandomAccessFile(filename, &rfile);
  ASSERT_TRUE(s.code == LITHOS_OK);

  FILE *fp = fopen(filename, "rb");
  fseek(fp, 0, SEEK_END);
  uint64_t file_size = ftell(fp);
  fclose(fp);

  Lithos_Table *table;
  s = Table_Open(&options, rfile, file_size, &table);
  ASSERT_TRUE(s.code == LITHOS_OK);
  ASSERT_TRUE(table != NULL);

  Lithos_Iterator *iter = Table_NewIterator(table, &options);
  ASSERT_TRUE(iter != NULL);

  for (int i = 0; i < 100; i++) {
    char key[32];
    snprintf(key, sizeof(key), "key%05d", i);
    Lithos_Iter_Seek(iter, Slice_FromCString(key));
    ASSERT_TRUE(Lithos_Iter_Valid(iter));
  }

  Lithos_Iter_Destroy(iter);
  Table_Destroy(table);
  FilterPolicy_Destroy(policy);
  unlink(filename);

  printf("✓ (%d assertions)\n", test_passed);
}

void Run_BloomTests(void) {
  Test_Bloom_Basic();
  Test_Bloom_FalsePositiveRate();
  Test_FilterBlock_BuilderReader();
  Test_Bloom_TableIntegration();
}
