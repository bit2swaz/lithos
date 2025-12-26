/*
 * test_table_builder.c - Comprehensive TableBuilder Tests
 *
 * Validates SSTable file construction including:
 * - Data block flushing at 4KB boundaries
 * - Index block generation
 * - Footer with magic number
 * - File format integrity
 */

#include "all_tests.h"
#include "testharness.h"
#include "core/table/table_builder.h"
#include "core/table/format.h"
#include "util/coding.h"
#include "util/env.h"
#include "util/status.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* Test: Create empty table */
static void Test_TableBuilder_Empty(void) {
    printf("[TEST] TableBuilder Empty                      ");
    
    const char* filename = "/tmp/lithos_test_empty.sst";
    unlink(filename);  // Clean up if exists
    
    Lithos_WritableFile* file;
    Status s = Env_NewWritableFile(filename, &file);
    ASSERT_TRUE(s.code == LITHOS_OK);
    
    Lithos_Options options;
    Lithos_Options_InitDefault(&options);
    
    Lithos_TableBuilder* tb = TableBuilder_Create(&options, file);
    ASSERT_TRUE(tb != NULL);
    
    /* Finish without adding any entries */
    lithos_status_code status = TableBuilder_Finish(tb);
    ASSERT_TRUE(status == LITHOS_OK);
    
    uint64_t file_size = TableBuilder_FileSize(tb);
    ASSERT_TRUE(file_size > 0);
    
    TableBuilder_Destroy(tb);
    WritableFile_Close(file);
    
    /* Verify footer at end of file */
    FILE* fp = fopen(filename, "rb");
    ASSERT_TRUE(fp != NULL);
    
    fseek(fp, 0, SEEK_END);
    long actual_size = ftell(fp);
    ASSERT_TRUE((uint64_t)actual_size == file_size);
    
    /* Read footer (last 48 bytes) */
    fseek(fp, -LITHOS_FOOTER_ENCODED_LENGTH, SEEK_END);
    char footer_buf[LITHOS_FOOTER_ENCODED_LENGTH];
    size_t n = fread(footer_buf, 1, LITHOS_FOOTER_ENCODED_LENGTH, fp);
    ASSERT_TRUE(n == LITHOS_FOOTER_ENCODED_LENGTH);
    
    /* Verify magic number */
    uint64_t magic = DecodeFixed64(footer_buf + LITHOS_FOOTER_ENCODED_LENGTH - 8);
    ASSERT_TRUE(magic == LITHOS_TABLE_MAGIC_NUMBER);
    
    fclose(fp);
    unlink(filename);
    
    printf("✓ (%d assertions)\n", test_passed);
}

/* Test: Add single entry */
static void Test_TableBuilder_SingleEntry(void) {
    printf("[TEST] TableBuilder Single Entry               ");
    
    const char* filename = "/tmp/lithos_test_single.sst";
    unlink(filename);
    
    Lithos_WritableFile* file;
    Status s = Env_NewWritableFile(filename, &file);
    ASSERT_TRUE(s.code == LITHOS_OK);
    
    Lithos_Options options;
    Lithos_Options_InitDefault(&options);
    
    Lithos_TableBuilder* tb = TableBuilder_Create(&options, file);
    ASSERT_TRUE(tb != NULL);
    
    Lithos_Slice key = Slice_FromCString("testkey");
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

/* Test: Multiple entries in single block */
static void Test_TableBuilder_MultipleEntries(void) {
    printf("[TEST] TableBuilder Multiple Entries           ");
    
    const char* filename = "/tmp/lithos_test_multi.sst";
    unlink(filename);
    
    Lithos_WritableFile* file;
    Status s = Env_NewWritableFile(filename, &file);
    ASSERT_TRUE(s.code == LITHOS_OK);
    
    Lithos_Options options;
    Lithos_Options_InitDefault(&options);
    
    Lithos_TableBuilder* tb = TableBuilder_Create(&options, file);
    ASSERT_TRUE(tb != NULL);
    
    /* Add 100 sorted entries */
    for (int i = 0; i < 100; i++) {
        char key[64], value[64];
        snprintf(key, sizeof(key), "key%05d", i);
        snprintf(value, sizeof(value), "value%05d", i);
        
        Lithos_Slice k = Slice_FromCString(key);
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

/* Test: Force multiple data blocks */
static void Test_TableBuilder_MultipleBlocks(void) {
    printf("[TEST] TableBuilder Multiple Blocks            ");
    
    const char* filename = "/tmp/lithos_test_blocks.sst";
    unlink(filename);
    
    Lithos_WritableFile* file;
    Status s = Env_NewWritableFile(filename, &file);
    ASSERT_TRUE(s.code == LITHOS_OK);
    
    Lithos_Options options;
    Lithos_Options_InitDefault(&options);
    options.block_size = 1024;  // Small blocks to force multiple flushes
    
    Lithos_TableBuilder* tb = TableBuilder_Create(&options, file);
    ASSERT_TRUE(tb != NULL);
    
    /* Add entries with large values to fill blocks quickly */
    char large_value[512];
    memset(large_value, 'v', sizeof(large_value));
    large_value[511] = '\0';
    
    int num_keys = 50;
    for (int i = 0; i < num_keys; i++) {
        char key[64];
        snprintf(key, sizeof(key), "bigkey%05d", i);
        
        Lithos_Slice k = Slice_FromCString(key);
        Lithos_Slice v = {large_value, 512};
        lithos_status_code status = TableBuilder_Add(tb, k, v);
        ASSERT_TRUE(status == LITHOS_OK);
    }
    
    ASSERT_TRUE(TableBuilder_NumEntries(tb) == (uint64_t)num_keys);
    
    lithos_status_code status = TableBuilder_Finish(tb);
    ASSERT_TRUE(status == LITHOS_OK);
    
    uint64_t file_size = TableBuilder_FileSize(tb);
    ASSERT_TRUE(file_size > 10000);  // Should be significant with 50 * 512-byte values
    
    printf("  File size: %lu bytes\n", (unsigned long)file_size);
    
    TableBuilder_Destroy(tb);
    WritableFile_Close(file);
    
    unlink(filename);
    
    printf("  ✓ (%d assertions)\n", test_passed);
}

/* Test: Large table with many entries */
static void Test_TableBuilder_LargeTable(void) {
    printf("[TEST] TableBuilder Large Table (10K entries)  ");
    
    const char* filename = "/tmp/lithos_test_large.sst";
    unlink(filename);
    
    Lithos_WritableFile* file;
    Status s = Env_NewWritableFile(filename, &file);
    ASSERT_TRUE(s.code == LITHOS_OK);
    
    Lithos_Options options;
    Lithos_Options_InitDefault(&options);
    
    Lithos_TableBuilder* tb = TableBuilder_Create(&options, file);
    ASSERT_TRUE(tb != NULL);
    
    /* Add 10,000 entries */
    int num_entries = 10000;
    for (int i = 0; i < num_entries; i++) {
        char key[64], value[128];
        snprintf(key, sizeof(key), "largekey%08d", i);
        snprintf(value, sizeof(value), "This is test value number %d with some extra padding", i);
        
        Lithos_Slice k = Slice_FromCString(key);
        Lithos_Slice v = Slice_FromCString(value);
        lithos_status_code status = TableBuilder_Add(tb, k, v);
        ASSERT_TRUE(status == LITHOS_OK);
    }
    
    ASSERT_TRUE(TableBuilder_NumEntries(tb) == (uint64_t)num_entries);
    
    lithos_status_code status = TableBuilder_Finish(tb);
    ASSERT_TRUE(status == LITHOS_OK);
    
    uint64_t file_size = TableBuilder_FileSize(tb);
    ASSERT_TRUE(file_size > 100000);  // Should be > 100KB
    
    printf("  File size: %lu bytes, Entries: %lu\n",
           (unsigned long)file_size,
           (unsigned long)TableBuilder_NumEntries(tb));
    
    TableBuilder_Destroy(tb);
    WritableFile_Close(file);
    
    /* Verify footer with magic number */
    FILE* fp = fopen(filename, "rb");
    ASSERT_TRUE(fp != NULL);
    
    fseek(fp, -LITHOS_FOOTER_ENCODED_LENGTH, SEEK_END);
    char footer_buf[LITHOS_FOOTER_ENCODED_LENGTH];
    size_t n = fread(footer_buf, 1, LITHOS_FOOTER_ENCODED_LENGTH, fp);
    ASSERT_TRUE(n == LITHOS_FOOTER_ENCODED_LENGTH);
    
    /* Decode and verify footer */
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

/* Main test runner for TableBuilder */
void Run_TableBuilderTests(void) {
    Test_TableBuilder_Empty();
    Test_TableBuilder_SingleEntry();
    Test_TableBuilder_MultipleEntries();
    Test_TableBuilder_MultipleBlocks();
    Test_TableBuilder_LargeTable();
}
