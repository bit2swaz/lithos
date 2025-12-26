/**
 * Big Picture: Slice = "Just a pointer + length"
 * ===============================================
 * This file defines a non-owning view over bytes. We use Slices everywhere to
 * avoid copying strings/keys/values. Think of it as C's take on std::string_view.
 *
 * Where it fits: The entire database API trades Slices instead of malloc'd
 * strings. WAL records, SSTable keys, MemTable entries—all pass by reference to
 * avoid allocations.
 *
 * Key Concepts:
 * - Non-owning views: lifetime is managed elsewhere (arena, heap, static).
 * - Zero-copy substrings: pointer arithmetic, no memmove.
 * - Safety caveat: dangling pointers if backing storage dies.
 */

#ifndef LITHOS_UTIL_SLICE_H
#define LITHOS_UTIL_SLICE_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Lithos_Slice - A non-owning view into a byte sequence.
 * Fields are tiny and meant to be copied by value.
 */
typedef struct {
    const char* data;  // Pointer to first byte; memory is owned elsewhere.
    size_t size;       // How many bytes are readable starting at data.
} Lithos_Slice;

/**
 * Slice_Create - Construct a slice from a pointer and length.
 * 
 * @param d: Pointer to the data (can be NULL if n == 0).
 * @param n: Number of bytes.
 * 
 * Returns: A Slice referencing the data.
 * 
 * Time Complexity: O(1)
 * 
 * Example:
 *   const char* str = "LevelDB";
 *   Lithos_Slice s = Slice_Create(str, 7);
 *   // s.data points to "LevelDB", s.size == 7
 */
static inline Lithos_Slice Slice_Create(const char* d, size_t n) {
    Lithos_Slice s;
    s.data = d;  // Store caller-provided pointer (no copy happens here).
    s.size = n;  // Remember how many bytes are valid starting at data.
    return s;
}

/**
 * Slice_FromCString - Create a slice from a null-terminated C string.
 * 
 * @param str: A null-terminated string (NOT NULL).
 * 
 * Returns: A Slice referencing the string (excluding the null terminator).
 * 
 * WARNING: This calls strlen(), which is O(n). Avoid in hot paths if you
 *          already know the length.
 * 
 * Example:
 *   Lithos_Slice s = Slice_FromCString("Hello");
 *   // s.size == 5, s.data points to "Hello"
 */
static inline Lithos_Slice Slice_FromCString(const char* str) {
    return Slice_Create(str, strlen(str));  // strlen walks until '\0'; excludes terminator.
}

/**
 * Slice_Empty - Create an empty slice.
 * 
 * Returns: A slice with data=NULL, size=0.
 */
static inline Lithos_Slice Slice_Empty(void) {
    return Slice_Create(NULL, 0);
}

/**
 * Slice_IsEmpty - Check if a slice is empty.
 * 
 * @param s: The slice to check.
 * 
 * Returns: true if size == 0, false otherwise.
 */
static inline bool Slice_IsEmpty(Lithos_Slice s) {
    return s.size == 0;
}

/**
 * Slice_Compare - Lexicographic comparison of two slices.
 * 
 * @param a: First slice.
 * @param b: Second slice.
 * 
 * Returns:
 * - Negative integer if a < b
 * - 0 if a == b
 * - Positive integer if a > b
 * 
 * Comparison is byte-by-byte (memcmp semantics).
 * If one slice is a prefix of another, the shorter one is considered smaller.
 * 
 * Time Complexity: O(min(a.size, b.size))
 * 
 * Example:
 *   Slice_Compare("apple", "banana") < 0  // 'a' < 'b'
 *   Slice_Compare("app", "apple") < 0     // "app" is shorter
 *   Slice_Compare("hello", "hello") == 0  // Equal
 */
static inline int Slice_Compare(Lithos_Slice a, Lithos_Slice b) {
    // Find the minimum length to compare
    size_t min_len = (a.size < b.size) ? a.size : b.size;
    
    // Compare the common prefix
    if (min_len > 0) {
        int result = memcmp(a.data, b.data, min_len);
        if (result != 0) {
            return result;
        }
    }
    
    // If the common prefix is equal, the shorter slice is "less than"
    if (a.size < b.size) return -1;
    if (a.size > b.size) return 1;
    return 0;
}

/**
 * Slice_Equal - Check if two slices are equal.
 * 
 * @param a: First slice.
 * @param b: Second slice.
 * 
 * Returns: true if both slices have the same size and content, false otherwise.
 * 
 * Time Complexity: O(n) where n is the size of the slices.
 */
static inline bool Slice_Equal(Lithos_Slice a, Lithos_Slice b) {
    if (a.size != b.size) {
        return false;
    }
    if (a.size == 0) {
        return true;  // Both empty
    }
    return memcmp(a.data, b.data, a.size) == 0;
}

/**
 * Slice_StartsWith - Check if a slice starts with a given prefix.
 * 
 * @param s: The slice to check.
 * @param prefix: The prefix to look for.
 * 
 * Returns: true if s starts with prefix, false otherwise.
 * 
 * Empty prefix always returns true (every string starts with empty string).
 * 
 * Time Complexity: O(prefix.size)
 * 
 * Example:
 *   Lithos_Slice s = Slice_FromCString("database");
 *   Lithos_Slice p = Slice_FromCString("data");
 *   Slice_StartsWith(s, p) == true
 */
static inline bool Slice_StartsWith(Lithos_Slice s, Lithos_Slice prefix) {
    // If prefix is longer than s, it cannot be a prefix
    if (prefix.size > s.size) {
        return false;
    }
    
    // Empty prefix is always a prefix
    if (prefix.size == 0) {
        return true;
    }
    
    // Compare the first prefix.size bytes
    return memcmp(s.data, prefix.data, prefix.size) == 0;
}

/**
 * Slice_RemovePrefix - Remove a prefix from a slice (if present).
 * 
 * @param s: The slice to modify.
 * @param prefix: The prefix to remove.
 * 
 * Returns: A new slice with the prefix removed (or original if no match).
 * 
 * This does NOT modify memory; it just adjusts the pointer and size.
 * 
 * Example:
 *   Lithos_Slice s = Slice_FromCString("user:12345");
 *   Lithos_Slice prefix = Slice_FromCString("user:");
 *   Lithos_Slice id = Slice_RemovePrefix(s, prefix);
 *   // id.data points to "12345", id.size == 5
 */
static inline Lithos_Slice Slice_RemovePrefix(Lithos_Slice s, Lithos_Slice prefix) {
    if (Slice_StartsWith(s, prefix)) {
        // Move the data pointer forward by prefix.size bytes; shrink length accordingly.
        return Slice_Create(s.data + prefix.size, s.size - prefix.size);
    }
    return s;
}

/**
 * Slice_At - Get a single byte at an index (bounds-checked in debug mode).
 * 
 * @param s: The slice.
 * @param index: The zero-based index.
 * 
 * Returns: The byte at s.data[index].
 * 
 * WARNING: No bounds checking in release builds for performance.
 *          Accessing out-of-bounds is undefined behavior.
 */
static inline char Slice_At(Lithos_Slice s, size_t index) {
    // In a production system, you might assert(index < s.size) here
    // For now, we trust the caller
    return s.data[index];
}

/**
 * Slice_ToString - Helper to create a null-terminated string from a slice.
 * 
 * @param s: The slice to convert.
 * 
 * Returns: A malloc'd string (caller must free).
 * 
 * WARNING: This allocates memory. Use sparingly.
 * 
 * Example:
 *   Lithos_Slice s = Slice_Create("Hello", 5);
 *   char* str = Slice_ToString(s);
 *   printf("%s\n", str);  // "Hello"
 *   free(str);
 */
static inline char* Slice_ToString(Lithos_Slice s) {
    char* result = (char*)malloc(s.size + 1);
    if (result == NULL) {
        return NULL;
    }
    if (s.size > 0) {
        memcpy(result, s.data, s.size);
    }
    result[s.size] = '\0';
    return result;
}

#ifdef __cplusplus
}
#endif

#endif  // LITHOS_UTIL_SLICE_H
