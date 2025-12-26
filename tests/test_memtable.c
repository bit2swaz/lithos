#define _POSIX_C_SOURCE 200809L

#include "testharness.h"
#include "core/memtable.h"
#include "core/dbformat.h"
#include <string.h>
#include <stdlib.h>

void Run_MemTableTests(void);

static void Test_MemTable(void) {
    printf(COLOR_BLUE "[Test] MemTable MVCC Operations\n" COLOR_RESET);

    // Create a MemTable
    Lithos_MemTable* mem = MemTable_Create();
    ASSERT_TRUE(mem != NULL);

    Status s;
    char* value = NULL;

    // Test 1: Basic Put and Get
    printf("  Test 1: Basic Put/Get\n");
    Lithos_Slice key1 = Slice_Create("key1", 4);
    Lithos_Slice val1 = Slice_Create("value1", 6);

    MemTable_Add(mem, 10, kTypeValue, key1, val1);

    bool found = MemTable_Get(mem, key1, kMaxSequenceNumber, &value, &s);
    if (found && Status_IsOK(s)) {
        if (strcmp(value, "value1") == 0) {
            printf("    Get('key1') = '%s'\n", value);
        } else {
            printf("    " COLOR_RED "ERROR: Expected 'value1', got '%s'\n" COLOR_RESET, value);
            ASSERT_TRUE(false);
        }
        free(value);
        value = NULL;
    } else {
        printf("    " COLOR_RED "ERROR: Key not found or error\n" COLOR_RESET);
        ASSERT_TRUE(false);
    }


    // Test 2: Multi-version (newer version shadows older)
    printf("  Test 2: Multi-Version (Newer Shadows Older)\n");
    Lithos_Slice val2 = Slice_Create("value2_newer", 12);

    MemTable_Add(mem, 20, kTypeValue, key1, val2);

    found = MemTable_Get(mem, key1, kMaxSequenceNumber, &value, &s);
    if (found && Status_IsOK(s)) {
        if (strcmp(value, "value2_newer") == 0) {
            printf("    Get('key1') = '%s'\n", value);
        } else {
            printf("    " COLOR_RED "ERROR: Expected 'value2_newer', got '%s'\n" COLOR_RESET, value);
            ASSERT_TRUE(false);
        }
        free(value);
        value = NULL;
    } else {
        printf("    " COLOR_RED "ERROR: Key not found or error\n" COLOR_RESET);
        ASSERT_TRUE(false);
    }


    // Test 3: Delete operation (tombstone)
    printf("  Test 3: Delete Operation\n");
    Lithos_Slice empty = Slice_Create("", 0);

    MemTable_Add(mem, 30, kTypeDeletion, key1, empty);

    found = MemTable_Get(mem, key1, kMaxSequenceNumber, &value, &s);
    if (found) {
        if (!Status_IsOK(s)) {
            printf("    Get('key1') after delete: " COLOR_GREEN "NotFound\n" COLOR_RESET);
        } else {
            printf("    " COLOR_RED "ERROR: Expected NotFound, but got value: '%s'\n" COLOR_RESET, value);
            free(value);
            ASSERT_TRUE(false);
        }
    } else {
        printf("    " COLOR_RED "ERROR: Should have found deletion marker\n" COLOR_RESET);
        ASSERT_TRUE(false);
    }


    // Test 4: Multiple keys
    printf("  Test 4: Multiple Keys\n");
    Lithos_Slice key2 = Slice_Create("apple", 5);
    Lithos_Slice key3 = Slice_Create("banana", 6);
    Lithos_Slice key4 = Slice_Create("cherry", 6);

    Lithos_Slice val_apple = Slice_Create("red", 3);
    Lithos_Slice val_banana = Slice_Create("yellow", 6);
    Lithos_Slice val_cherry = Slice_Create("dark_red", 8);

    MemTable_Add(mem, 40, kTypeValue, key2, val_apple);
    MemTable_Add(mem, 50, kTypeValue, key3, val_banana);
    MemTable_Add(mem, 60, kTypeValue, key4, val_cherry);

    // Verify apple
    found = MemTable_Get(mem, key2, kMaxSequenceNumber, &value, &s);
    if (found && Status_IsOK(s) && strcmp(value, "red") == 0) {
        printf("    Get('apple') = 'red'\n");
        free(value);
        value = NULL;
    } else {
        printf("    " COLOR_RED "ERROR: apple test failed\n" COLOR_RESET);
        ASSERT_TRUE(false);
    }

    // Verify banana
    found = MemTable_Get(mem, key3, kMaxSequenceNumber, &value, &s);
    if (found && Status_IsOK(s) && strcmp(value, "yellow") == 0) {
        printf("    Get('banana') = 'yellow'\n");
        free(value);
        value = NULL;
    } else {
        printf("    " COLOR_RED "ERROR: banana test failed\n" COLOR_RESET);
        ASSERT_TRUE(false);
    }

    // Verify cherry
    found = MemTable_Get(mem, key4, kMaxSequenceNumber, &value, &s);
    if (found && Status_IsOK(s) && strcmp(value, "dark_red") == 0) {
        printf("    Get('cherry') = 'dark_red'\n");
        free(value);
        value = NULL;
    } else {
        printf("    " COLOR_RED "ERROR: cherry test failed\n" COLOR_RESET);
        ASSERT_TRUE(false);
    }

    // Test 5: Non-existent key
    printf("  Test 5: Non-Existent Key\n");
    Lithos_Slice key_missing = Slice_Create("missing", 7);

    found = MemTable_Get(mem, key_missing, kMaxSequenceNumber, &value, &s);
    if (!found) {
        printf("    Get('missing'): " COLOR_GREEN "Not in MemTable\n" COLOR_RESET);
    } else {
        printf("    " COLOR_RED "ERROR: Found non-existent key\n" COLOR_RESET);
        if (value != NULL) free(value);
        ASSERT_TRUE(false);
    }

    // Test 6: Reference counting
    printf("  Test 6: Reference Counting\n");
    size_t initial_mem = MemTable_ApproximateMemoryUsage(mem);
    printf("    Initial memory: %zu bytes\n", initial_mem);

    MemTable_Ref(mem);  // Increment ref count
    printf("    Ref count incremented\n");

    MemTable_Unref(mem); // Decrement (should not destroy)
    printf("    Ref count decremented (still alive)\n");

    MemTable_Unref(mem); // Final decrement (should destroy)
    printf("    Final unref: " COLOR_GREEN "MemTable destroyed\n" COLOR_RESET);
}

void Run_MemTableTests(void) {
    Test_MemTable();
}