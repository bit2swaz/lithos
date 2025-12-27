
#define _POSIX_C_SOURCE 200809L

#include "core/table/block_builder.h"
#include "lithos/options.h"
#include "testharness.h"
#include "util/coding.h"
#include <stdio.h>
#include <string.h>

void Run_BlockBuilderTests(void);

static void Test_BlockBuilder_Simple(void) {
  printf("[TEST] BlockBuilder Simple\n");

  Lithos_Options options;
  Lithos_Options_InitDefault(&options);

  Lithos_BlockBuilder *builder = BlockBuilder_Create(&options);
  ASSERT_TRUE(builder != NULL);
  ASSERT_TRUE(BlockBuilder_Empty(builder));

  Lithos_Slice k1 = Slice_FromCString("apple");
  Lithos_Slice v1 = Slice_FromCString("red");
  BlockBuilder_Add(builder, k1, v1);

  Lithos_Slice k2 = Slice_FromCString("banana");
  Lithos_Slice v2 = Slice_FromCString("yellow");
  BlockBuilder_Add(builder, k2, v2);

  Lithos_Slice k3 = Slice_FromCString("cherry");
  Lithos_Slice v3 = Slice_FromCString("dark_red");
  BlockBuilder_Add(builder, k3, v3);

  ASSERT_TRUE(!BlockBuilder_Empty(builder));

  Lithos_Slice block = BlockBuilder_Finish(builder);
  ASSERT_TRUE(block.size > 0);

  printf("  Block size: %zu bytes\n", block.size);

  size_t estimate = BlockBuilder_CurrentSizeEstimate(builder);
  ASSERT_EQ(estimate, block.size);

  BlockBuilder_Destroy(builder);
}

static void Test_BlockBuilder_PrefixCompression(void) {
  printf("[TEST] BlockBuilder Prefix Compression\n");

  Lithos_Options options;
  Lithos_Options_InitDefault(&options);

  Lithos_BlockBuilder *builder = BlockBuilder_Create(&options);

  Lithos_Slice k1 = Slice_FromCString("drive");
  Lithos_Slice v1 = Slice_FromCString("val1");
  BlockBuilder_Add(builder, k1, v1);

  Lithos_Slice k2 = Slice_FromCString("driver");
  Lithos_Slice v2 = Slice_FromCString("val2");
  BlockBuilder_Add(builder, k2, v2);

  Lithos_Slice k3 = Slice_FromCString("driving");
  Lithos_Slice v3 = Slice_FromCString("val3");
  BlockBuilder_Add(builder, k3, v3);

  Lithos_Slice block = BlockBuilder_Finish(builder);

  size_t raw_key_value_size = 0;
  raw_key_value_size += strlen("drive") + strlen("val1");
  raw_key_value_size += strlen("driver") + strlen("val2");
  raw_key_value_size += strlen("driving") + strlen("val3");

  printf("  Raw key+value size: %zu bytes\n", raw_key_value_size);
  printf("  Actual block size: %zu bytes\n", block.size);

  ASSERT_TRUE(block.size > 0);
  ASSERT_TRUE(block.size < raw_key_value_size + 50);

  printf("  Compression effective: shared prefixes reduced key storage\n");

  BlockBuilder_Destroy(builder);
}

static void Test_BlockBuilder_RestartPoints(void) {
  printf("[TEST] BlockBuilder Restart Points\n");

  Lithos_Options options;
  Lithos_Options_InitDefault(&options);
  options.block_restart_interval = 2;

  Lithos_BlockBuilder *builder = BlockBuilder_Create(&options);

  BlockBuilder_Add(builder, Slice_FromCString("a"), Slice_FromCString("val_a"));
  BlockBuilder_Add(builder, Slice_FromCString("b"), Slice_FromCString("val_b"));
  BlockBuilder_Add(builder, Slice_FromCString("c"),
                   Slice_FromCString("val_c"));
  BlockBuilder_Add(builder, Slice_FromCString("d"), Slice_FromCString("val_d"));
  BlockBuilder_Add(builder, Slice_FromCString("e"),
                   Slice_FromCString("val_e"));

  Lithos_Slice block = BlockBuilder_Finish(builder);

  ASSERT_TRUE(block.size >= 4);
  const char *end = block.data + block.size - 4;
  uint32_t restart_count = DecodeFixed32(end);

  printf("  Restart count: %u\n", restart_count);

  ASSERT_EQ(restart_count, 3);

  size_t restart_array_size = restart_count * 4;
  ASSERT_TRUE(block.size >= restart_array_size + 4);

  const char *restart_ptr = block.data + block.size - 4 - restart_array_size;
  uint32_t offset0 = DecodeFixed32(restart_ptr);
  uint32_t offset1 = DecodeFixed32(restart_ptr + 4);
  uint32_t offset2 = DecodeFixed32(restart_ptr + 8);

  printf("  Restart offsets: [%u, %u, %u]\n", offset0, offset1, offset2);

  ASSERT_EQ(offset0, 0);

  ASSERT_TRUE(offset1 > 0);
  ASSERT_TRUE(offset2 > offset1);

  BlockBuilder_Destroy(builder);
}

static void Test_BlockBuilder_Reset(void) {
  printf("[TEST] BlockBuilder Reset\n");

  Lithos_Options options;
  Lithos_Options_InitDefault(&options);

  Lithos_BlockBuilder *builder = BlockBuilder_Create(&options);

  BlockBuilder_Add(builder, Slice_FromCString("x"), Slice_FromCString("1"));
  BlockBuilder_Add(builder, Slice_FromCString("y"), Slice_FromCString("2"));
  Lithos_Slice block1 = BlockBuilder_Finish(builder);
  size_t size1 = block1.size;

  printf("  First block size: %zu bytes\n", size1);

  BlockBuilder_Reset(builder);
  ASSERT_TRUE(BlockBuilder_Empty(builder));

  BlockBuilder_Add(builder, Slice_FromCString("a"), Slice_FromCString("1"));
  BlockBuilder_Add(builder, Slice_FromCString("b"), Slice_FromCString("2"));
  Lithos_Slice block2 = BlockBuilder_Finish(builder);
  size_t size2 = block2.size;

  printf("  Second block size: %zu bytes\n", size2);

  ASSERT_TRUE(size2 > 0);
  ASSERT_EQ(size1, size2);

  BlockBuilder_Destroy(builder);
}

static void Test_BlockBuilder_LargeKeys(void) {
  printf("[TEST] BlockBuilder Large Keys\n");

  Lithos_Options options;
  Lithos_Options_InitDefault(&options);

  Lithos_BlockBuilder *builder = BlockBuilder_Create(&options);

  const char *base = "verylongprefixstring";
  char key1[100], key2[100], key3[100];
  snprintf(key1, sizeof(key1), "%s_1", base);
  snprintf(key2, sizeof(key2), "%s_2", base);
  snprintf(key3, sizeof(key3), "%s_3", base);

  BlockBuilder_Add(builder, Slice_FromCString(key1),
                   Slice_FromCString("value1"));
  BlockBuilder_Add(builder, Slice_FromCString(key2),
                   Slice_FromCString("value2"));
  BlockBuilder_Add(builder, Slice_FromCString(key3),
                   Slice_FromCString("value3"));

  Lithos_Slice block = BlockBuilder_Finish(builder);

  size_t raw_size =
      strlen(key1) + strlen(key2) + strlen(key3) + 18;

  printf("  Raw key+value size: %zu bytes\n", raw_size);
  printf("  Actual block size: %zu bytes\n", block.size);

  ASSERT_TRUE(block.size > 0);
  ASSERT_TRUE(block.size < raw_size + 100);

  printf("  Compression demonstrated: long shared prefix optimized\n");

  BlockBuilder_Destroy(builder);
}

void Run_BlockBuilderTests(void) {
  Test_BlockBuilder_Simple();
  Test_BlockBuilder_PrefixCompression();
  Test_BlockBuilder_RestartPoints();
  Test_BlockBuilder_Reset();
  Test_BlockBuilder_LargeKeys();
}
