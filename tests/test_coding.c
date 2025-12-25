#define _POSIX_C_SOURCE 200809L

#include "testharness.h"
#include "util/status.h"
#include "util/slice.h"
#include "util/coding.h"
#include <string.h>

/* Test counters */
int test_passed = 0;
int test_failed = 0;

void Run_CodingTests(void);

static void Test_OKStatus(void) {
    printf(COLOR_BLUE "[TEST] OK Status\n" COLOR_RESET);

    Status s = Status_OK();

    // Verify predicates
    ASSERT_TRUE(Status_IsOK(s));
    ASSERT_TRUE(!Status_IsNotFound(s));
    ASSERT_TRUE(!Status_IsCorruption(s));
    ASSERT_TRUE(!Status_IsIOError(s));

    // Verify string representation
    const char* str = Status_ToString(s);
    ASSERT_TRUE(strcmp(str, "OK") == 0);
    printf("  Status: %s\n", str);

    // No need to call Status_Free on OK status (it's a no-op)
    Status_Free(s);
}

static void Test_NotFoundStatus(void) {
    printf(COLOR_BLUE "[TEST] NotFound Status\n" COLOR_RESET);

    // Test with custom message
    Status s = Status_NotFound("Key 'user:12345' not found");

    ASSERT_TRUE(!Status_IsOK(s));
    ASSERT_TRUE(Status_IsNotFound(s));

    const char* str = Status_ToString(s);
    printf("  Status: %s\n", str);
    ASSERT_TRUE(strstr(str, "user:12345") != NULL);

    // Must free non-OK statuses
    Status_Free(s);

    // Test with NULL message (should use default)
    Status s2 = Status_NotFound(NULL);
    const char* str2 = Status_ToString(s2);
    printf("  Default: %s\n", str2);
    Status_Free(s2);
}

static void Test_CorruptionStatus(void) {
    printf(COLOR_BLUE "[TEST] Corruption Status\n" COLOR_RESET);

    // Test with both messages
    Status s = Status_Corruption("SSTable footer invalid", "file=data_00042.sst");

    ASSERT_TRUE(!Status_IsOK(s));
    ASSERT_TRUE(Status_IsCorruption(s));

    const char* str = Status_ToString(s);
    printf("  Status: %s\n", str);

    // Verify both parts are present
    ASSERT_TRUE(strstr(str, "footer") != NULL);
    ASSERT_TRUE(strstr(str, "00042") != NULL);

    Status_Free(s);
}

static void Test_IOErrorStatus(void) {
    printf(COLOR_BLUE "[TEST] IOError Status\n" COLOR_RESET);

    Status s = Status_IOError("Failed to open WAL", "/data/lithos/000123.log");

    ASSERT_TRUE(!Status_IsOK(s));
    ASSERT_TRUE(Status_IsIOError(s));

    const char* str = Status_ToString(s);
    printf("  Status: %s\n", str);

    Status_Free(s);
}

static void Test_InvalidArgumentStatus(void) {
    printf(COLOR_BLUE "[TEST] InvalidArgument Status\n" COLOR_RESET);

    Status s = Status_InvalidArgument("Key size cannot exceed 4KB");

    ASSERT_TRUE(!Status_IsOK(s));
    ASSERT_TRUE(s.code == LITHOS_INVALID_ARGUMENT);

    const char* str = Status_ToString(s);
    printf("  Status: %s\n", str);

    Status_Free(s);
}

static void Test_StatusCopy(void) {
    printf(COLOR_BLUE "[TEST] Status Copy\n" COLOR_RESET);

    // Create original status
    Status original = Status_NotFound("Original key");

    // Copy it
    Status copy = Status_Copy(original);

    // Both should have the same code and message
    ASSERT_TRUE(original.code == copy.code);
    ASSERT_TRUE(strcmp(Status_ToString(original), Status_ToString(copy)) == 0);

    printf("  Original: %s\n", Status_ToString(original));
    printf("  Copy: %s\n", Status_ToString(copy));

    // Free both (they should be independent)
    Status_Free(original);
    Status_Free(copy);

    // Test copying OK status
    Status ok = Status_OK();
    Status ok_copy = Status_Copy(ok);
    ASSERT_TRUE(Status_IsOK(ok_copy));
}

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
    ASSERT_TRUE(Status_IsOK(s1));
    printf("  Success case: %s\n", Status_ToString(s1));

    // NotFound case
    Status s2 = SimulatedOperation(1);
    ASSERT_TRUE(!Status_IsOK(s2));
    printf("  Expected error: %s\n", Status_ToString(s2));
    Status_Free(s2);

    // Corruption case
    Status s3 = SimulatedOperation(2);
    ASSERT_TRUE(Status_IsCorruption(s3));
    printf("  Corruption detected: %s\n", Status_ToString(s3));
    Status_Free(s3);
}

static void Test_SliceBasics(void) {
    printf(COLOR_BLUE "[TEST] Slice Basics\n" COLOR_RESET);

    // Test 1: Create from C string
    Lithos_Slice s1 = Slice_FromCString("hello");
    ASSERT_EQ(s1.size, 5);
    ASSERT_TRUE(memcmp(s1.data, "hello", 5) == 0);
    printf("  Created slice: '%.*s' (size=%zu)\n", (int)s1.size, s1.data, s1.size);

    // Test 2: Create from buffer
    const char* buf = "world";
    Lithos_Slice s2 = Slice_Create(buf, 5);
    ASSERT_EQ(s2.size, 5);
    ASSERT_TRUE(memcmp(s2.data, "world", 5) == 0);

    // Test 3: Empty slice
    Lithos_Slice empty = Slice_Empty();
    ASSERT_EQ(empty.size, 0);

    // Test 4: Comparison
    ASSERT_TRUE(Slice_Compare(s1, s2) < 0); // "hello" < "world"
    ASSERT_TRUE(Slice_Compare(s2, s1) > 0);

    // Test 5: Equality
    Lithos_Slice s1_copy = Slice_FromCString("hello");
    ASSERT_TRUE(Slice_Equal(s1, s1_copy));
    ASSERT_TRUE(!Slice_Equal(s1, s2));
}

static void Test_SlicePrefix(void) {
    printf(COLOR_BLUE "[TEST] Slice Prefix Operations\n" COLOR_RESET);

    Lithos_Slice s = Slice_FromCString("prefix_suffix");

    // Test starts_with
    ASSERT_TRUE(Slice_StartsWith(s, Slice_FromCString("prefix")));
    ASSERT_TRUE(!Slice_StartsWith(s, Slice_FromCString("suffix")));

    // Test remove_prefix
    Lithos_Slice remaining = Slice_RemovePrefix(s, Slice_FromCString("prefix_"));
    ASSERT_EQ(remaining.size, 6);
    ASSERT_TRUE(memcmp(remaining.data, "suffix", 6) == 0);

    printf("  Original: '%.*s'\n", (int)s.size, s.data);
    printf("  After remove_prefix: '%.*s'\n", (int)remaining.size, remaining.data);
}

static void Test_Fixed32Encoding(void) {
    printf(COLOR_BLUE "[TEST] Fixed32 Encoding\n" COLOR_RESET);

    char buf[4];
    uint32_t original = 0x12345678;

    // Encode
    EncodeFixed32(buf, original);
    printf("  Encoded 0x%08x as: %02x %02x %02x %02x\n",
           original, (uint8_t)buf[0], (uint8_t)buf[1], (uint8_t)buf[2], (uint8_t)buf[3]);

    // Decode
    uint32_t decoded = DecodeFixed32(buf);
    ASSERT_EQ(decoded, original);
    printf("  Decoded back: 0x%08x\n", decoded);

    // Test endianness (should be little-endian)
    ASSERT_EQ((uint8_t)buf[0], 0x78);
    ASSERT_EQ((uint8_t)buf[1], 0x56);
    ASSERT_EQ((uint8_t)buf[2], 0x34);
    ASSERT_EQ((uint8_t)buf[3], 0x12);
}

static void Test_Fixed64Encoding(void) {
    printf(COLOR_BLUE "[TEST] Fixed64 Encoding\n" COLOR_RESET);

    char buf[8];
    uint64_t original = 0x123456789ABCDEF0ULL;

    // Encode
    EncodeFixed64(buf, original);
    printf("  Encoded 0x%016llx\n", (unsigned long long)original);

    // Decode
    uint64_t decoded = DecodeFixed64(buf);
    ASSERT_EQ(decoded, original);
    printf("  Decoded back: 0x%016llx\n", (unsigned long long)decoded);

    // Test endianness
    ASSERT_EQ((uint8_t)buf[0], 0xF0);
    ASSERT_EQ((uint8_t)buf[1], 0xDE);
    ASSERT_EQ((uint8_t)buf[2], 0xBC);
    ASSERT_EQ((uint8_t)buf[3], 0x9A);
    ASSERT_EQ((uint8_t)buf[4], 0x78);
    ASSERT_EQ((uint8_t)buf[5], 0x56);
    ASSERT_EQ((uint8_t)buf[6], 0x34);
    ASSERT_EQ((uint8_t)buf[7], 0x12);
}

static void Test_Varint32Encoding(void) {
    printf(COLOR_BLUE "[TEST] Varint32 Encoding\n" COLOR_RESET);

    char buf[5]; // Max 5 bytes for 32-bit
    uint32_t test_values[] = {0, 1, 127, 128, 255, 256, 65535, 0x7FFFFFFF};
    size_t num_tests = sizeof(test_values) / sizeof(test_values[0]);

    for (size_t i = 0; i < num_tests; i++) {
        uint32_t original = test_values[i];
        char* end = EncodeVarint32(buf, original);
        size_t encoded_size = end - buf;

        printf("  %u -> %zu bytes\n", original, encoded_size);

        // Decode
        const char* decode_ptr = buf;
        uint32_t decoded;
        const char* end_ptr = GetVarint32Ptr(decode_ptr, end, &decoded);
        bool success = (end_ptr != NULL);
        ASSERT_TRUE(success);
        ASSERT_EQ(decoded, original);
    }
}

static void Test_Varint64Encoding(void) {
    printf(COLOR_BLUE "[TEST] Varint64 Encoding\n" COLOR_RESET);

    char buf[10]; // Max 10 bytes for 64-bit
    uint64_t test_values[] = {0, 1, 127, 128, 255, 256, 65535, 0x7FFFFFFFFFFFFFFFULL};
    size_t num_tests = sizeof(test_values) / sizeof(test_values[0]);

    for (size_t i = 0; i < num_tests; i++) {
        uint64_t original = test_values[i];
        char* end = EncodeVarint64(buf, original);
        size_t encoded_size = end - buf;

        printf("  %llu -> %zu bytes\n", (unsigned long long)original, encoded_size);

        // Decode
        const char* decode_ptr = buf;
        uint64_t decoded;
        const char* end_ptr = GetVarint64Ptr(decode_ptr, end, &decoded);
        bool success = (end_ptr != NULL);
        ASSERT_TRUE(success);
        ASSERT_EQ(decoded, original);
    }
}

static void Test_VarintErrors(void) {
    printf(COLOR_BLUE "[TEST] Varint Error Handling\n" COLOR_RESET);

    // Test truncated varint
    uint8_t buf[10];
    buf[0] = 0x80; // Continuation bit set, but no more bytes
    const char* ptr = (const char*)buf;
    uint32_t decoded32;
    const char* end_ptr = GetVarint32Ptr(ptr, (const char*)buf + 1, &decoded32);
    bool success = (end_ptr != NULL);
    ASSERT_TRUE(!success);
    printf("  Truncated varint32 correctly rejected\n");

    // Test varint64 truncation
    uint64_t decoded64;
    ptr = (const char*)buf;
    end_ptr = GetVarint64Ptr(ptr, (const char*)buf + 1, &decoded64);
    success = (end_ptr != NULL);
    ASSERT_TRUE(!success);
    printf("  Truncated varint64 correctly rejected\n");

    // Test buffer overflow protection
    uint8_t small_buf[1];
    small_buf[0] = 0xFF;
    ptr = (const char*)small_buf;
    end_ptr = GetVarint32Ptr(ptr, (const char*)small_buf + 1, &decoded32);
    success = (end_ptr != NULL);
    ASSERT_TRUE(!success);
    printf("  Buffer overflow correctly prevented\n");
}

void Run_CodingTests(void) {
    Test_OKStatus();
    Test_NotFoundStatus();
    Test_CorruptionStatus();
    Test_IOErrorStatus();
    Test_InvalidArgumentStatus();
    Test_StatusCopy();
    Test_ErrorPropagation();
    Test_SliceBasics();
    Test_SlicePrefix();
    Test_Fixed32Encoding();
    Test_Fixed64Encoding();
    Test_Varint32Encoding();
    Test_Varint64Encoding();
    Test_VarintErrors();
}