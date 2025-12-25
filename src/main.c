/**
 * Lithos Storage Engine - Test Program
 * 
 * This program verifies that the core utility subsystems work correctly:
 * - Status: Error handling and propagation
 * - Slice: String views and operations
 * - Coding: Binary encoding/decoding (Fixed and Varint)
 * - Arena: Memory allocator for efficient small object allocation
 * 
 * Test Coverage:
 * - Status error codes and messages
 * - Slice comparison and prefix operations
 * - Fixed32/64 encoding in Little-Endian format
 * - Varint32/64 encoding with space efficiency
 * - Arena bump-pointer allocation and alignment
 * 
 * Expected Output:
 * All tests should pass, demonstrating correct implementation of
 * the foundational utilities.
 * 
 * Author: Aditya (@bit2swaz)
 */

#include "lithos/lithos_status.h"
#include "util/status.h"
#include "util/slice.h"
#include "util/coding.h"
#include "util/arena.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <stdint.h>

/* ANSI color codes for pretty output */
#define COLOR_GREEN "\033[0;32m"
#define COLOR_RED "\033[0;31m"
#define COLOR_YELLOW "\033[0;33m"
#define COLOR_BLUE "\033[0;34m"
#define COLOR_RESET "\033[0m"

/**
 * Test Case 1: OK Status
 * Verifies that OK statuses work correctly and require no cleanup.
 */
static void Test_OKStatus(void) {
    printf(COLOR_BLUE "[TEST] OK Status\n" COLOR_RESET);
    
    Status s = Status_OK();
    
    // Verify predicates
    assert(Status_IsOK(s));
    assert(!Status_IsNotFound(s));
    assert(!Status_IsCorruption(s));
    assert(!Status_IsIOError(s));
    
    // Verify string representation
    const char* str = Status_ToString(s);
    assert(strcmp(str, "OK") == 0);
    printf("  Status: %s\n", str);
    
    // No need to call Status_Free on OK status (it's a no-op)
    Status_Free(s);
    
    printf(COLOR_GREEN "  [PASS]\n" COLOR_RESET);
}

/**
 * Test Case 2: NotFound Status
 * Verifies that NotFound errors carry custom messages.
 */
static void Test_NotFoundStatus(void) {
    printf(COLOR_BLUE "[TEST] NotFound Status\n" COLOR_RESET);
    
    // Test with custom message
    Status s = Status_NotFound("Key 'user:12345' not found");
    
    assert(!Status_IsOK(s));
    assert(Status_IsNotFound(s));
    
    const char* str = Status_ToString(s);
    printf("  Status: %s\n", str);
    assert(strstr(str, "user:12345") != NULL);
    
    // Must free non-OK statuses
    Status_Free(s);
    
    // Test with NULL message (should use default)
    Status s2 = Status_NotFound(NULL);
    const char* str2 = Status_ToString(s2);
    printf("  Default: %s\n", str2);
    Status_Free(s2);
    
    printf(COLOR_GREEN "  [PASS]\n" COLOR_RESET);
}

/**
 * Test Case 3: Corruption Status
 * Verifies that Corruption errors can concatenate two messages.
 */
static void Test_CorruptionStatus(void) {
    printf(COLOR_BLUE "[TEST] Corruption Status\n" COLOR_RESET);
    
    // Test with both messages
    Status s = Status_Corruption("SSTable footer invalid", "file=data_00042.sst");
    
    assert(!Status_IsOK(s));
    assert(Status_IsCorruption(s));
    
    const char* str = Status_ToString(s);
    printf("  Status: %s\n", str);
    
    // Verify both parts are present
    assert(strstr(str, "footer") != NULL);
    assert(strstr(str, "00042") != NULL);
    
    Status_Free(s);
    
    printf(COLOR_GREEN "  [PASS]\n" COLOR_RESET);
}

/**
 * Test Case 4: IOError Status
 * Verifies that I/O errors provide context.
 */
static void Test_IOErrorStatus(void) {
    printf(COLOR_BLUE "[TEST] IOError Status\n" COLOR_RESET);
    
    Status s = Status_IOError("Failed to open WAL", "/data/lithos/000123.log");
    
    assert(!Status_IsOK(s));
    assert(Status_IsIOError(s));
    
    const char* str = Status_ToString(s);
    printf("  Status: %s\n", str);
    
    Status_Free(s);
    
    printf(COLOR_GREEN "  [PASS]\n" COLOR_RESET);
}

/**
 * Test Case 5: InvalidArgument Status
 * Verifies that invalid argument errors work.
 */
static void Test_InvalidArgumentStatus(void) {
    printf(COLOR_BLUE "[TEST] InvalidArgument Status\n" COLOR_RESET);
    
    Status s = Status_InvalidArgument("Key size cannot exceed 4KB");
    
    assert(!Status_IsOK(s));
    assert(s.code == LITHOS_INVALID_ARGUMENT);
    
    const char* str = Status_ToString(s);
    printf("  Status: %s\n", str);
    
    Status_Free(s);
    
    printf(COLOR_GREEN "  [PASS]\n" COLOR_RESET);
}

/**
 * Test Case 6: Status Copy
 * Verifies that Status_Copy creates independent copies.
 */
static void Test_StatusCopy(void) {
    printf(COLOR_BLUE "[TEST] Status Copy\n" COLOR_RESET);
    
    // Create original status
    Status original = Status_NotFound("Original key");
    
    // Copy it
    Status copy = Status_Copy(original);
    
    // Both should have the same code and message
    assert(original.code == copy.code);
    assert(strcmp(Status_ToString(original), Status_ToString(copy)) == 0);
    
    printf("  Original: %s\n", Status_ToString(original));
    printf("  Copy: %s\n", Status_ToString(copy));
    
    // Free both (they should be independent)
    Status_Free(original);
    Status_Free(copy);
    
    // Test copying OK status
    Status ok = Status_OK();
    Status ok_copy = Status_Copy(ok);
    assert(Status_IsOK(ok_copy));
    
    printf(COLOR_GREEN "  [PASS]\n" COLOR_RESET);
}

/**
 * Demonstrate typical error handling pattern in Lithos code.
 * 
 * This simulates a function that might fail and shows how to
 * propagate the error up the call stack.
 */
static Status SimulatedOperation(int should_fail) {
    if (should_fail == 1) {
        return Status_NotFound("Simulated key not found");
    } else if (should_fail == 2) {
        return Status_Corruption("Simulated corruption", "offset=1024");
    }
    return Status_OK();
}

static void Test_ErrorPropagation(void) {
    printf(COLOR_BLUE "[TEST] Error Propagation Pattern\n" COLOR_RESET);
    
    // Success case
    Status s1 = SimulatedOperation(0);
    if (!Status_IsOK(s1)) {
        printf(COLOR_RED "  Unexpected error: %s\n" COLOR_RESET, Status_ToString(s1));
        Status_Free(s1);
        return;
    }
    printf("  Success case: %s\n", Status_ToString(s1));
    
    // NotFound case
    Status s2 = SimulatedOperation(1);
    if (!Status_IsOK(s2)) {
        printf("  Expected error: %s\n", Status_ToString(s2));
        Status_Free(s2);
    }
    
    // Corruption case
    Status s3 = SimulatedOperation(2);
    if (Status_IsCorruption(s3)) {
        printf("  Corruption detected: %s\n", Status_ToString(s3));
        Status_Free(s3);
    }
    
    printf(COLOR_GREEN "  [PASS]\n" COLOR_RESET);
}

/* ========== Slice Tests ========== */

/**
 * Test Case: Slice Creation and Comparison
 * Verifies that slices can be created, compared, and checked for equality.
 */
static void Test_SliceBasics(void) {
    printf(COLOR_BLUE "[TEST] Slice Basics\n" COLOR_RESET);
    
    // Create slices from string literals
    Lithos_Slice s1 = Slice_FromCString("apple");
    Lithos_Slice s2 = Slice_FromCString("banana");
    Lithos_Slice s3 = Slice_FromCString("apple");
    
    printf("  s1: \"%.*s\" (size=%zu)\n", (int)s1.size, s1.data, s1.size);
    printf("  s2: \"%.*s\" (size=%zu)\n", (int)s2.size, s2.data, s2.size);
    
    // Test comparison
    assert(Slice_Compare(s1, s2) < 0);   // "apple" < "banana"
    assert(Slice_Compare(s2, s1) > 0);   // "banana" > "apple"
    assert(Slice_Compare(s1, s3) == 0);  // "apple" == "apple"
    
    // Test equality
    assert(Slice_Equal(s1, s3));
    assert(!Slice_Equal(s1, s2));
    
    // Test empty slice
    Lithos_Slice empty = Slice_Empty();
    assert(Slice_IsEmpty(empty));
    assert(empty.size == 0);
    
    printf(COLOR_GREEN "  [PASS]\n" COLOR_RESET);
}

/**
 * Test Case: Slice Prefix Operations
 * Verifies StartsWith and RemovePrefix functionality.
 */
static void Test_SlicePrefix(void) {
    printf(COLOR_BLUE "[TEST] Slice Prefix Operations\n" COLOR_RESET);
    
    Lithos_Slice s = Slice_FromCString("user:12345");
    Lithos_Slice prefix = Slice_FromCString("user:");
    Lithos_Slice wrong_prefix = Slice_FromCString("admin:");
    
    // Test StartsWith
    assert(Slice_StartsWith(s, prefix));
    assert(!Slice_StartsWith(s, wrong_prefix));
    
    // Test RemovePrefix
    Lithos_Slice id = Slice_RemovePrefix(s, prefix);
    printf("  Original: \"%.*s\"\n", (int)s.size, s.data);
    printf("  After removing 'user:': \"%.*s\"\n", (int)id.size, id.data);
    
    assert(id.size == 5);
    assert(memcmp(id.data, "12345", 5) == 0);
    
    // Removing non-existent prefix should return original
    Lithos_Slice unchanged = Slice_RemovePrefix(s, wrong_prefix);
    assert(Slice_Equal(unchanged, s));
    
    printf(COLOR_GREEN "  [PASS]\n" COLOR_RESET);
}

/* ========== Coding Tests ========== */

/**
 * Test Case: Fixed32 Encoding
 * Verifies Little-Endian encoding of 32-bit integers.
 */
static void Test_Fixed32Encoding(void) {
    printf(COLOR_BLUE "[TEST] Fixed32 Encoding\n" COLOR_RESET);
    
    // Test the classic example: 0xdeadbeef
    char buf[4];
    uint32_t original = 0xdeadbeef;
    
    EncodeFixed32(buf, original);
    
    // Verify byte order (Little-Endian)
    printf("  Encoded 0x%08x as bytes: ", original);
    for (int i = 0; i < 4; i++) {
        printf("%02x ", (unsigned char)buf[i]);
    }
    printf("\n");
    
    // Expected: ef be ad de (LSB first)
    assert((unsigned char)buf[0] == 0xef);
    assert((unsigned char)buf[1] == 0xbe);
    assert((unsigned char)buf[2] == 0xad);
    assert((unsigned char)buf[3] == 0xde);
    
    // Decode and verify round-trip
    uint32_t decoded = DecodeFixed32(buf);
    printf("  Decoded back to: 0x%08x\n", decoded);
    assert(decoded == original);
    
    // Test edge cases
    EncodeFixed32(buf, 0);
    assert(DecodeFixed32(buf) == 0);
    
    EncodeFixed32(buf, 0xFFFFFFFF);
    assert(DecodeFixed32(buf) == 0xFFFFFFFF);
    
    printf(COLOR_GREEN "  [PASS]\n" COLOR_RESET);
}

/**
 * Test Case: Fixed64 Encoding
 * Verifies Little-Endian encoding of 64-bit integers.
 */
static void Test_Fixed64Encoding(void) {
    printf(COLOR_BLUE "[TEST] Fixed64 Encoding\n" COLOR_RESET);
    
    char buf[8];
    uint64_t original = 0x0123456789abcdefULL;
    
    EncodeFixed64(buf, original);
    
    printf("  Encoded 0x%016llx\n", (unsigned long long)original);
    
    // Verify round-trip
    uint64_t decoded = DecodeFixed64(buf);
    assert(decoded == original);
    printf("  Decoded: 0x%016llx ✓\n", (unsigned long long)decoded);
    
    printf(COLOR_GREEN "  [PASS]\n" COLOR_RESET);
}

/**
 * Test Case: Varint32 Encoding
 * Verifies variable-length encoding saves space for small numbers.
 */
static void Test_Varint32Encoding(void) {
    printf(COLOR_BLUE "[TEST] Varint32 Encoding\n" COLOR_RESET);
    
    char buf[10];
    
    // Test Case 1: Small number (1 byte)
    char* p1 = EncodeVarint32(buf, 1);
    size_t len1 = p1 - buf;
    printf("  Encoded 1: %zu byte(s) ", len1);
    for (size_t i = 0; i < len1; i++) {
        printf("%02x ", (unsigned char)buf[i]);
    }
    printf("\n");
    assert(len1 == 1);
    
    uint32_t val1;
    const char* end1 = GetVarint32Ptr(buf, buf + len1, &val1);
    assert(end1 != NULL);
    assert(val1 == 1);
    
    // Test Case 2: Medium number (2 bytes)
    // 300 = 0x12C = 0b100101100
    // Encoding: [AC 02] = [10101100 00000010]
    //   Byte 0: 0x2C (bits 6-0) | 0x80 (continuation) = 0xAC
    //   Byte 1: 0x02 (bits 13-7)
    char* p2 = EncodeVarint32(buf, 300);
    size_t len2 = p2 - buf;
    printf("  Encoded 300: %zu byte(s) ", len2);
    for (size_t i = 0; i < len2; i++) {
        printf("%02x ", (unsigned char)buf[i]);
    }
    printf("\n");
    assert(len2 == 2);
    assert((unsigned char)buf[0] == 0xAC);  // 10101100
    assert((unsigned char)buf[1] == 0x02);  // 00000010
    
    uint32_t val2;
    const char* end2 = GetVarint32Ptr(buf, buf + len2, &val2);
    assert(end2 != NULL);
    assert(val2 == 300);
    printf("  Decoded: %u ✓\n", val2);
    
    // Test Case 3: Large number (5 bytes)
    char* p3 = EncodeVarint32(buf, 0xFFFFFFFF);
    size_t len3 = p3 - buf;
    printf("  Encoded 0xFFFFFFFF: %zu byte(s)\n", len3);
    assert(len3 == 5);  // Maximum for 32-bit
    
    uint32_t val3;
    const char* end3 = GetVarint32Ptr(buf, buf + len3, &val3);
    assert(end3 != NULL);
    assert(val3 == 0xFFFFFFFF);
    
    printf(COLOR_GREEN "  [PASS]\n" COLOR_RESET);
}

/**
 * Test Case: Varint64 Encoding
 * Verifies 64-bit varint encoding.
 */
static void Test_Varint64Encoding(void) {
    printf(COLOR_BLUE "[TEST] Varint64 Encoding\n" COLOR_RESET);
    
    char buf[10];
    
    // Test a large 64-bit number
    uint64_t original = 0x123456789ABCDEFULL;
    char* p = EncodeVarint64(buf, original);
    size_t len = p - buf;
    
    printf("  Encoded 0x%llx: %zu byte(s)\n", (unsigned long long)original, len);
    
    uint64_t decoded;
    const char* end = GetVarint64Ptr(buf, buf + len, &decoded);
    assert(end != NULL);
    assert(decoded == original);
    printf("  Decoded: 0x%llx ✓\n", (unsigned long long)decoded);
    
    // Test VarintLength helper
    int expected_len = VarintLength(original);
    printf("  VarintLength(%llu) = %d (actual: %zu)\n", 
           (unsigned long long)original, expected_len, len);
    assert((size_t)expected_len == len);
    
    printf(COLOR_GREEN "  [PASS]\n" COLOR_RESET);
}

/**
 * Test Case: Varint Error Handling
 * Verifies that decoding handles truncated/malformed input.
 */
static void Test_VarintErrors(void) {
    printf(COLOR_BLUE "[TEST] Varint Error Handling\n" COLOR_RESET);
    
    // Test 1: Truncated varint (missing bytes)
    unsigned char buf1[] = {0x80};  // Continuation bit set, but no next byte
    uint32_t val1;
    const char* end1 = GetVarint32Ptr((const char*)buf1, (const char*)buf1 + 1, &val1);
    assert(end1 == NULL);  // Should fail
    printf("  Truncated varint: correctly rejected ✓\n");
    
    // Test 2: Empty buffer
    uint32_t val2;
    const char* end2 = GetVarint32Ptr((const char*)buf1, (const char*)buf1, &val2);
    assert(end2 == NULL);
    printf("  Empty buffer: correctly rejected ✓\n");
    
    // Test 3: Valid varint with exact limit
    char buf3[5];
    char* p3 = EncodeVarint32(buf3, 1000);
    uint32_t val3;
    const char* end3 = GetVarint32Ptr(buf3, p3, &val3);
    assert(end3 != NULL);
    assert(val3 == 1000);
    printf("  Valid varint at limit: correctly decoded ✓\n");
    
    printf(COLOR_GREEN "  [PASS]\n" COLOR_RESET);
}

/* ========== Arena Tests ========== */

/**
 * Test Case: Basic Arena Allocation
 * Verifies that we can allocate memory and use it.
 */
static void Test_ArenaBasics(void) {
    printf(COLOR_BLUE "[TEST] Arena Basics\n" COLOR_RESET);
    
    Lithos_Arena* arena = Arena_Create();
    assert(arena != NULL);
    
    // Test 1: Allocate an integer, write to it, read it back
    int* num = (int*)Arena_Allocate(arena, sizeof(int));
    assert(num != NULL);
    *num = 42;
    printf("  Allocated int: %d ✓\n", *num);
    assert(*num == 42);
    
    // Test 2: Allocate a string buffer
    char* buffer = Arena_Allocate(arena, 100);
    assert(buffer != NULL);
    strcpy(buffer, "Hello, Arena!");
    printf("  Allocated string: \"%s\" ✓\n", buffer);
    assert(strcmp(buffer, "Hello, Arena!") == 0);
    
    // Test 3: Check memory usage
    size_t usage = Arena_MemoryUsage(arena);
    printf("  Memory usage: %zu bytes\n", usage);
    assert(usage >= 4096);  // Should have allocated at least one block
    
    Arena_Destroy(arena);
    printf(COLOR_GREEN "  [PASS]\n" COLOR_RESET);
}

/**
 * Test Case: Arena Alignment
 * Verifies that AllocateAligned returns 8-byte aligned pointers.
 */
static void Test_ArenaAlignment(void) {
    printf(COLOR_BLUE "[TEST] Arena Alignment\n" COLOR_RESET);
    
    Lithos_Arena* arena = Arena_Create();
    assert(arena != NULL);
    
    // Allocate 1 byte to create misalignment
    char* byte = Arena_Allocate(arena, 1);
    assert(byte != NULL);
    *byte = (char)0xFF;
    
    // Now allocate an aligned pointer
    char* aligned = Arena_AllocateAligned(arena, 8);
    assert(aligned != NULL);
    
    // Check that the pointer is 8-byte aligned
    uintptr_t addr = (uintptr_t)aligned;
    uintptr_t misalignment = addr & 7;
    
    printf("  Unaligned byte at: %p\n", (void*)byte);
    printf("  Aligned pointer at: %p (misalignment: %zu)\n", (void*)aligned, misalignment);
    
    assert(misalignment == 0);  // Must be perfectly aligned
    
    // Test with a struct that needs alignment
    struct TestStruct {
        uint64_t a;
        void* b;
        uint64_t c;
    };
    
    struct TestStruct* s = (struct TestStruct*)Arena_AllocateAligned(arena, sizeof(*s));
    assert(s != NULL);
    assert(((uintptr_t)s & 7) == 0);  // Struct pointer must be aligned
    
    s->a = 0x123456789ABCDEFULL;
    s->b = (void*)0xDEADBEEF;
    s->c = 0xFEDCBA987654321ULL;
    
    printf("  Allocated aligned struct: a=0x%llx, b=%p, c=0x%llx ✓\n",
           (unsigned long long)s->a, s->b, (unsigned long long)s->c);
    
    Arena_Destroy(arena);
    printf(COLOR_GREEN "  [PASS]\n" COLOR_RESET);
}

/**
 * Test Case: Arena Stress Test
 * Allocates many small objects to verify the arena scales properly.
 */
static void Test_ArenaStress(void) {
    printf(COLOR_BLUE "[TEST] Arena Stress Test\n" COLOR_RESET);
    
    Lithos_Arena* arena = Arena_Create();
    assert(arena != NULL);
    
    const int num_allocations = 10000;
    char** ptrs = (char**)malloc(num_allocations * sizeof(char*));
    assert(ptrs != NULL);
    
    // Allocate 10,000 small strings
    for (int i = 0; i < num_allocations; i++) {
        ptrs[i] = Arena_Allocate(arena, 32);
        assert(ptrs[i] != NULL);
        
        // Write a unique pattern to each allocation
        snprintf(ptrs[i], 32, "String_%d", i);
    }
    
    printf("  Allocated %d strings\n", num_allocations);
    
    // Verify a few random strings
    assert(strcmp(ptrs[0], "String_0") == 0);
    assert(strcmp(ptrs[100], "String_100") == 0);
    assert(strcmp(ptrs[9999], "String_9999") == 0);
    printf("  Verification: strings are intact ✓\n");
    
    // Check memory usage
    size_t usage = Arena_MemoryUsage(arena);
    printf("  Memory usage: %zu bytes (%.2f MB)\n", usage, usage / (1024.0 * 1024.0));
    
    // Expected: ~10,000 * 32 = 320KB of data
    // Plus overhead from 4KB blocks
    // Should be less than 1MB
    assert(usage < 1024 * 1024);
    
    free(ptrs);
    Arena_Destroy(arena);
    printf(COLOR_GREEN "  [PASS]\n" COLOR_RESET);
}

/**
 * Test Case: Arena Large Allocation
 * Verifies that large allocations (>1KB) are handled correctly.
 */
static void Test_ArenaLargeAllocation(void) {
    printf(COLOR_BLUE "[TEST] Arena Large Allocation\n" COLOR_RESET);
    
    Lithos_Arena* arena = Arena_Create();
    assert(arena != NULL);
    
    // Allocate a large buffer (2KB)
    const size_t large_size = 2048;
    char* large_buffer = Arena_Allocate(arena, large_size);
    assert(large_buffer != NULL);
    
    // Fill it with a pattern
    memset(large_buffer, 0xAB, large_size);
    
    // Verify the pattern
    for (size_t i = 0; i < large_size; i++) {
        assert((unsigned char)large_buffer[i] == 0xAB);
    }
    printf("  Large allocation (%zu bytes): verified ✓\n", large_size);
    
    // Allocate a small buffer after the large one
    // This should use a different block (not waste space in the large block)
    char* small_buffer = Arena_Allocate(arena, 64);
    assert(small_buffer != NULL);
    strcpy(small_buffer, "Small after large");
    printf("  Small allocation after large: \"%s\" ✓\n", small_buffer);
    
    // Check memory usage
    size_t usage = Arena_MemoryUsage(arena);
    printf("  Memory usage: %zu bytes\n", usage);
    
    // Should have allocated: 2048 (large) + 4096 (standard block for small)
    assert(usage >= 2048 + 4096);
    
    Arena_Destroy(arena);
    printf(COLOR_GREEN "  [PASS]\n" COLOR_RESET);
}

/**
 * Test Case: Arena Cleanup
 * Verifies that Arena_Destroy properly frees all memory.
 */
static void Test_ArenaCleanup(void) {
    printf(COLOR_BLUE "[TEST] Arena Cleanup\n" COLOR_RESET);
    
    Lithos_Arena* arena = Arena_Create();
    assert(arena != NULL);
    
    // Allocate some memory
    for (int i = 0; i < 100; i++) {
        char* ptr = Arena_Allocate(arena, 128);
        assert(ptr != NULL);
        memset(ptr, i & 0xFF, 128);
    }
    
    size_t usage_before = Arena_MemoryUsage(arena);
    printf("  Allocated memory: %zu bytes\n", usage_before);
    
    // Destroy the arena
    Arena_Destroy(arena);
    printf("  Arena destroyed ✓\n");
    
    // Test NULL safety
    Arena_Destroy(NULL);  // Should not crash
    printf("  NULL destroy: safe ✓\n");
    
    printf(COLOR_GREEN "  [PASS]\n" COLOR_RESET);
}

/**
 * Main Entry Point
 * Runs all test cases and reports results.
 */
int main(void) {
    printf("\n");
    printf("========================================\n");
    printf("  Lithos Storage Engine - Test Suite\n");
    printf("========================================\n");
    printf("\n");
    
    // Status Tests
    printf(COLOR_YELLOW "--- Status Subsystem ---\n" COLOR_RESET);
    printf("\n");
    
    Test_OKStatus();
    printf("\n");
    
    Test_NotFoundStatus();
    printf("\n");
    
    Test_CorruptionStatus();
    printf("\n");
    
    Test_IOErrorStatus();
    printf("\n");
    
    Test_InvalidArgumentStatus();
    printf("\n");
    
    Test_StatusCopy();
    printf("\n");
    
    Test_ErrorPropagation();
    printf("\n");
    
    // Slice Tests
    printf(COLOR_YELLOW "--- Slice Subsystem ---\n" COLOR_RESET);
    printf("\n");
    
    Test_SliceBasics();
    printf("\n");
    
    Test_SlicePrefix();
    printf("\n");
    
    // Coding Tests
    printf(COLOR_YELLOW "--- Coding Subsystem ---\n" COLOR_RESET);
    printf("\n");
    
    Test_Fixed32Encoding();
    printf("\n");
    
    Test_Fixed64Encoding();
    printf("\n");
    
    Test_Varint32Encoding();
    printf("\n");
    
    Test_Varint64Encoding();
    printf("\n");
    
    Test_VarintErrors();
    printf("\n");
    
    // Arena Tests
    printf(COLOR_YELLOW "--- Arena Subsystem ---\n" COLOR_RESET);
    printf("\n");
    
    Test_ArenaBasics();
    printf("\n");
    
    Test_ArenaAlignment();
    printf("\n");
    
    Test_ArenaStress();
    printf("\n");
    
    Test_ArenaLargeAllocation();
    printf("\n");
    
    Test_ArenaCleanup();
    printf("\n");
    
    printf("========================================\n");
    printf(COLOR_GREEN "  All Tests Passed! ✓\n" COLOR_RESET);
    printf("========================================\n");
    printf("\n");
    
    printf("Core utilities verified:\n");
    printf("  ✓ Status: Error handling\n");
    printf("  ✓ Slice: String views\n");
    printf("  ✓ Coding: Binary serialization\n");
    printf("  ✓ Arena: Memory allocator\n");
    printf("\n");
    printf("Ready for Phase 2: In-Memory Storage (SkipList & MemTable).\n");
    printf("\n");
    
    return 0;
}
