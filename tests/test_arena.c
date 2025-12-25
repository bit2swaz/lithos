#define _POSIX_C_SOURCE 200809L

#include "testharness.h"
#include "util/arena.h"
#include <string.h>
#include <stdint.h>

void Run_ArenaTests(void);

static void Test_ArenaBasics(void) {
    printf(COLOR_BLUE "[TEST] Arena Basics\n" COLOR_RESET);

    Lithos_Arena* arena = Arena_Create();
    ASSERT_TRUE(arena != NULL);

    // Test 1: Allocate an integer, write to it, read it back
    int* num = (int*)Arena_Allocate(arena, sizeof(int));
    ASSERT_TRUE(num != NULL);
    *num = 42;
    printf("  Allocated int: %d\n", *num);
    ASSERT_EQ(*num, 42);

    // Test 2: Allocate a string buffer
    char* buffer = Arena_Allocate(arena, 100);
    ASSERT_TRUE(buffer != NULL);
    strcpy(buffer, "Hello, Arena!");
    printf("  Allocated string: \"%s\"\n", buffer);
    ASSERT_TRUE(strcmp(buffer, "Hello, Arena!") == 0);

    // Test 3: Check memory usage
    size_t usage = Arena_MemoryUsage(arena);
    printf("  Memory usage: %zu bytes\n", usage);
    ASSERT_TRUE(usage >= 4096);  // Should have allocated at least one block

    Arena_Destroy(arena);
}

static void Test_ArenaAlignment(void) {
    printf(COLOR_BLUE "[TEST] Arena Alignment\n" COLOR_RESET);

    Lithos_Arena* arena = Arena_Create();
    ASSERT_TRUE(arena != NULL);

    // Allocate 1 byte to create misalignment
    char* byte = Arena_Allocate(arena, 1);
    ASSERT_TRUE(byte != NULL);
    *byte = (char)0xFF;

    // Now allocate an aligned pointer
    char* aligned = Arena_AllocateAligned(arena, 8);
    ASSERT_TRUE(aligned != NULL);

    // Check that the pointer is 8-byte aligned
    uintptr_t addr = (uintptr_t)aligned;
    uintptr_t misalignment = addr & 7;

    printf("  Unaligned byte at: %p\n", (void*)byte);
    printf("  Aligned pointer at: %p (misalignment: %zu)\n", (void*)aligned, misalignment);

    ASSERT_EQ(misalignment, 0);  // Must be perfectly aligned

    // Test with a struct that needs alignment
    struct TestStruct {
        uint64_t a;
        void* b;
        uint64_t c;
    };

    struct TestStruct* s = (struct TestStruct*)Arena_AllocateAligned(arena, sizeof(*s));
    ASSERT_TRUE(s != NULL);
    ASSERT_EQ(((uintptr_t)s & 7), 0);  // Struct pointer must be aligned

    s->a = 0x123456789ABCDEFULL;
    s->b = (void*)0xDEADBEEF;
    s->c = 0xFEDCBA987654321ULL;

    printf("  Allocated aligned struct: a=0x%llx, b=%p, c=0x%llx\n",
           (unsigned long long)s->a, s->b, (unsigned long long)s->c);

    Arena_Destroy(arena);
}

static void Test_ArenaStress(void) {
    printf(COLOR_BLUE "[TEST] Arena Stress Test\n" COLOR_RESET);

    Lithos_Arena* arena = Arena_Create();
    ASSERT_TRUE(arena != NULL);

    const int num_allocations = 10000;
    char** ptrs = (char**)malloc(num_allocations * sizeof(char*));
    ASSERT_TRUE(ptrs != NULL);

    // Allocate 10,000 small strings
    for (int i = 0; i < num_allocations; i++) {
        ptrs[i] = Arena_Allocate(arena, 32);
        ASSERT_TRUE(ptrs[i] != NULL);

        // Write a unique pattern to each allocation
        snprintf(ptrs[i], 32, "String_%d", i);
    }

    printf("  Allocated %d strings\n", num_allocations);

    // Verify a few random strings
    ASSERT_TRUE(strcmp(ptrs[0], "String_0") == 0);
    ASSERT_TRUE(strcmp(ptrs[100], "String_100") == 0);
    ASSERT_TRUE(strcmp(ptrs[9999], "String_9999") == 0);
    printf("  Verification: strings are intact\n");

    // Check memory usage
    size_t usage = Arena_MemoryUsage(arena);
    printf("  Memory usage: %zu bytes (%.2f MB)\n", usage, usage / (1024.0 * 1024.0));

    // Expected: ~10,000 * 32 = 320KB of data
    // Plus overhead from 4KB blocks
    // Should be less than 1MB
    ASSERT_TRUE(usage < 1024 * 1024);

    free(ptrs);
    Arena_Destroy(arena);
}

static void Test_ArenaLargeAllocation(void) {
    printf(COLOR_BLUE "[TEST] Arena Large Allocation\n" COLOR_RESET);

    Lithos_Arena* arena = Arena_Create();
    ASSERT_TRUE(arena != NULL);

    // Allocate a large buffer (2KB)
    const size_t large_size = 2048;
    char* large_buffer = Arena_Allocate(arena, large_size);
    ASSERT_TRUE(large_buffer != NULL);

    // Fill it with a pattern
    memset(large_buffer, 0xAB, large_size);

    // Verify the pattern
    for (size_t i = 0; i < large_size; i++) {
        ASSERT_EQ((unsigned char)large_buffer[i], 0xAB);
    }
    printf("  Large allocation (%zu bytes): verified\n", large_size);

    // Allocate a small buffer after the large one
    // This should use a different block (not waste space in the large block)
    char* small_buffer = Arena_Allocate(arena, 64);
    ASSERT_TRUE(small_buffer != NULL);
    strcpy(small_buffer, "Small after large");
    printf("  Small allocation after large: \"%s\"\n", small_buffer);

    // Check memory usage
    size_t usage = Arena_MemoryUsage(arena);
    printf("  Memory usage: %zu bytes\n", usage);

    // Should have allocated: 2048 (large) + 4096 (standard block for small)
    ASSERT_TRUE(usage >= 2048 + 4096);

    Arena_Destroy(arena);
}

static void Test_ArenaCleanup(void) {
    printf(COLOR_BLUE "[TEST] Arena Cleanup\n" COLOR_RESET);

    Lithos_Arena* arena = Arena_Create();
    ASSERT_TRUE(arena != NULL);

    // Allocate some memory
    for (int i = 0; i < 100; i++) {
        char* ptr = Arena_Allocate(arena, 128);
        ASSERT_TRUE(ptr != NULL);
        memset(ptr, i & 0xFF, 128);
    }

    size_t usage_before = Arena_MemoryUsage(arena);
    printf("  Allocated memory: %zu bytes\n", usage_before);

    // Destroy the arena
    Arena_Destroy(arena);
    printf("  Arena destroyed\n");

    // Test NULL safety
    Arena_Destroy(NULL);  // Should not crash
    printf("  NULL destroy: safe\n");
}

void Run_ArenaTests(void) {
    Test_ArenaBasics();
    Test_ArenaAlignment();
    Test_ArenaStress();
    Test_ArenaLargeAllocation();
    Test_ArenaCleanup();
}