/**
 * Lithos Storage Engine - Binary Encoding/Decoding
 *
 * This module provides low-level serialization primitives for writing
 * structured data to disk in a portable, compact format.
 *
 * Two Encoding Families:
 * ----------------------
 *
 * 1. **Fixed-Length Encoding:**
 *    - Always occupies the same number of bytes (4 or 8).
 *    - Used when random access is required (e.g., file offsets in Footer).
 *    - Strict Little-Endian format (LSB first).
 *
 * 2. **Variable-Length Encoding (Varint):**
 *    - Uses 1-5 bytes for 32-bit, 1-10 bytes for 64-bit integers.
 *    - Saves space for small numbers (e.g., 300 uses 2 bytes vs 4).
 *    - Used for lengths, counts, and sequence numbers.
 *
 * Endianness Strategy:
 * --------------------
 * We enforce Little-Endian on-disk format because:
 * - x86/x64 and ARM (Android/iOS) are Little-Endian (>99% of devices).
 * - Consistent format across machines allows data portability.
 * - On Big-Endian machines (POWER, SPARC), we byte-swap in software.
 *
 * We do NOT use `memcpy(&value, ptr, 4)` because:
 * - It assumes the machine's native endianness.
 * - It can cause unaligned access faults on ARM.
 * - Explicit bit-shifting is clearer and portable.
 *
 * Author: Aditya (@bit2swaz)
 * Version: 2.0.0
 */

#ifndef LITHOS_UTIL_CODING_H
#define LITHOS_UTIL_CODING_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========== Fixed-Length Encoding ========== */

/**
 * EncodeFixed32 - Encode a 32-bit integer in Little-Endian format.
 *
 * @param dst: Destination buffer (must have at least 4 bytes).
 * @param value: The 32-bit value to encode.
 *
 * Wire Format: [Byte0] [Byte1] [Byte2] [Byte3]
 *              where Byte0 = (value & 0xFF), Byte1 = (value >> 8) & 0xFF, ...
 *
 * Example:
 *   EncodeFixed32(buf, 0x12345678)
 *   buf[0] = 0x78, buf[1] = 0x56, buf[2] = 0x34, buf[3] = 0x12
 */
void EncodeFixed32(char *dst, uint32_t value);

/**
 * EncodeFixed64 - Encode a 64-bit integer in Little-Endian format.
 *
 * @param dst: Destination buffer (must have at least 8 bytes).
 * @param value: The 64-bit value to encode.
 *
 * Wire Format: 8 bytes, LSB first.
 */
void EncodeFixed64(char *dst, uint64_t value);

/**
 * DecodeFixed32 - Decode a 32-bit integer from Little-Endian format.
 *
 * @param ptr: Source buffer (must have at least 4 bytes).
 *
 * Returns: The decoded 32-bit value.
 *
 * Example:
 *   Given buf = {0xef, 0xbe, 0xad, 0xde}
 *   DecodeFixed32(buf) == 0xdeadbeef
 */
uint32_t DecodeFixed32(const char *ptr);

/**
 * DecodeFixed64 - Decode a 64-bit integer from Little-Endian format.
 *
 * @param ptr: Source buffer (must have at least 8 bytes).
 *
 * Returns: The decoded 64-bit value.
 */
uint64_t DecodeFixed64(const char *ptr);

/* ========== Variable-Length Encoding (Varint) ========== */

/**
 * EncodeVarint32 - Encode a 32-bit integer using Base-128 Varint.
 *
 * @param dst: Destination buffer (must have at least 5 bytes for worst case).
 * @param v: The value to encode.
 *
 * Returns: Pointer to the byte AFTER the last written byte.
 *
 * Format:
 * - Each byte stores 7 bits of data.
 * - The MSB (bit 7) is 1 if more bytes follow, 0 for the last byte.
 * - Data is encoded LSB-first (little-endian order of 7-bit chunks).
 *
 * Size:
 * - [0, 127]: 1 byte
 * - [128, 16383]: 2 bytes
 * - [16384, 2097151]: 3 bytes
 * - [2097152, 268435455]: 4 bytes
 * - [268435456, 4294967295]: 5 bytes
 *
 * Example:
 *   EncodeVarint32(buf, 300)
 *   buf[0] = 0xAC (10101100: MSB=1, data=0x2C)
 *   buf[1] = 0x02 (00000010: MSB=0, data=0x02)
 *   Returns: buf + 2
 */
char *EncodeVarint32(char *dst, uint32_t v);

/**
 * EncodeVarint64 - Encode a 64-bit integer using Base-128 Varint.
 *
 * @param dst: Destination buffer (must have at least 10 bytes for worst case).
 * @param v: The value to encode.
 *
 * Returns: Pointer to the byte AFTER the last written byte.
 *
 * Size: 1-10 bytes depending on magnitude.
 */
char *EncodeVarint64(char *dst, uint64_t v);

/**
 * GetVarint32Ptr - Decode a Varint32 from a buffer (with bounds checking).
 *
 * @param p: Start of the encoded varint.
 * @param limit: One byte past the end of the valid buffer.
 * @param value: Output parameter for the decoded value.
 *
 * Returns: Pointer to the byte AFTER the varint, or NULL if:
 * - The varint is incomplete (hit 'limit' before MSB=0).
 * - The varint is malformed (more than 5 bytes).
 *
 * Example:
 *   const char* end = GetVarint32Ptr(buf, buf + len, &result);
 *   if (end == NULL) {
 *       // Decoding failed
 *   }
 */
const char *GetVarint32Ptr(const char *p, const char *limit, uint32_t *value);

/**
 * GetVarint64Ptr - Decode a Varint64 from a buffer (with bounds checking).
 *
 * @param p: Start of the encoded varint.
 * @param limit: One byte past the end of the valid buffer.
 * @param value: Output parameter for the decoded value.
 *
 * Returns: Pointer to the byte AFTER the varint, or NULL on error.
 */
const char *GetVarint64Ptr(const char *p, const char *limit, uint64_t *value);

/**
 * VarintLength - Calculate the number of bytes needed for a Varint32.
 *
 * @param v: The value to encode.
 *
 * Returns: Number of bytes (1-5).
 *
 * Useful for pre-allocating buffers.
 */
int VarintLength(uint64_t v);

#ifdef __cplusplus
}
#endif

#endif // LITHOS_UTIL_CODING_H
