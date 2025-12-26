/**
 * Lithos Storage Engine - Binary Encoding/Decoding Implementation
 * 
 * This file implements the low-level serialization primitives defined in
 * coding.h. All encoding is done using explicit bit manipulation to ensure
 * portability and avoid alignment issues.
 * 
 * CRITICAL DESIGN NOTE: Why We Don't Use memcpy()
 * ================================================
 * 
 * Naive Approach (WRONG on Big-Endian):
 *   uint32_t value = 0x12345678;
 *   memcpy(buffer, &value, 4);
 * 
 * Problem: On Little-Endian (x86), buffer = {78, 56, 34, 12} ✓
 *          On Big-Endian (POWER), buffer = {12, 34, 56, 78} ✗
 * 
 * Our Approach (Correct):
 *   buffer[0] = value & 0xFF;         // Always 0x78
 *   buffer[1] = (value >> 8) & 0xFF;  // Always 0x56
 *   buffer[2] = (value >> 16) & 0xFF; // Always 0x34
 *   buffer[3] = (value >> 24) & 0xFF; // Always 0x12
 * 
 * This produces the same byte sequence on ALL architectures.
 * 
 * 
 * VARINT ENCODING EXPLAINED:
 * ==========================
 * 
 * Goal: Store small numbers in fewer bytes.
 * 
 * Format: Base-128 (7 bits of data per byte)
 * - Bit 7 (MSB): Continuation flag (1 = more bytes, 0 = last byte)
 * - Bits 6-0: Data payload (7 bits)
 * 
 * Algorithm:
 * ----------
 * 1. Extract the lowest 7 bits of the value.
 * 2. If the remaining value > 0, set bit 7 to 1 (continuation).
 * 3. Write the byte.
 * 4. Shift value right by 7 bits.
 * 5. Repeat until value == 0.
 * 
 * Example 1: Encoding 300
 * -----------------------
 * 300 in binary: 0000 0001 0010 1100
 * 
 * Step 1: Extract bits 6-0: 010 1100 (0x2C)
 *         Remaining: 0000 0010 (not zero, so set continuation bit)
 *         Write: 1010 1100 (0xAC)
 * 
 * Step 2: Extract bits 6-0: 000 0010 (0x02)
 *         Remaining: 0 (done, no continuation bit)
 *         Write: 0000 0010 (0x02)
 * 
 * Result: [0xAC, 0x02] (2 bytes instead of 4)
 * 
 * Decoding:
 * ---------
 * Read bytes while MSB == 1. Accumulate 7-bit chunks:
 *   result = (byte0 & 0x7F) | ((byte1 & 0x7F) << 7) | ...
 * 
 * For 300:
 *   byte0 = 0xAC → data = 0x2C
 *   byte1 = 0x02 → data = 0x02
 *   result = 0x2C | (0x02 << 7) = 0x2C | 0x100 = 300 ✓
 * 
 * Author: Aditya (@bit2swaz)
 * Version: 2.0.0
 */

#include "util/coding.h"

/* ========== Fixed-Length Encoding Implementation ========== */

/**
 * EncodeFixed32 - Store a 32-bit value in Little-Endian format.
 * 
 * We manually extract each byte using bit masks and shifts.
 * This is portable across all architectures and avoids unaligned access.
 * 
 * Compiler Optimization: Modern compilers (GCC/Clang with -O2) recognize
 * this pattern and emit a single MOV instruction on x86 (zero overhead).
 */
void EncodeFixed32(char* dst, uint32_t value) {
    uint8_t* const buffer = (uint8_t*)dst;
    
    // Extract bytes from LSB to MSB
    buffer[0] = (uint8_t)(value);          // Bits 7-0
    buffer[1] = (uint8_t)(value >> 8);     // Bits 15-8
    buffer[2] = (uint8_t)(value >> 16);    // Bits 23-16
    buffer[3] = (uint8_t)(value >> 24);    // Bits 31-24
}

/**
 * EncodeFixed64 - Store a 64-bit value in Little-Endian format.
 */
void EncodeFixed64(char* dst, uint64_t value) {
    uint8_t* const buffer = (uint8_t*)dst;
    
    buffer[0] = (uint8_t)(value);
    buffer[1] = (uint8_t)(value >> 8);
    buffer[2] = (uint8_t)(value >> 16);
    buffer[3] = (uint8_t)(value >> 24);
    buffer[4] = (uint8_t)(value >> 32);
    buffer[5] = (uint8_t)(value >> 40);
    buffer[6] = (uint8_t)(value >> 48);
    buffer[7] = (uint8_t)(value >> 56);
}

/**
 * DecodeFixed32 - Read a 32-bit Little-Endian value.
 * 
 * We reconstruct the value by shifting each byte into position.
 * Cast to uint8_t prevents sign-extension issues.
 */
uint32_t DecodeFixed32(const char* ptr) {
    const uint8_t* const buffer = (const uint8_t*)ptr;
    
    // Combine bytes: LSB first
    return ((uint32_t)buffer[0])         |
           ((uint32_t)buffer[1] << 8)    |
           ((uint32_t)buffer[2] << 16)   |
           ((uint32_t)buffer[3] << 24);
}

/**
 * DecodeFixed64 - Read a 64-bit Little-Endian value.
 */
uint64_t DecodeFixed64(const char* ptr) {
    const uint8_t* const buffer = (const uint8_t*)ptr;
    
    return ((uint64_t)buffer[0])         |
           ((uint64_t)buffer[1] << 8)    |
           ((uint64_t)buffer[2] << 16)   |
           ((uint64_t)buffer[3] << 24)   |
           ((uint64_t)buffer[4] << 32)   |
           ((uint64_t)buffer[5] << 40)   |
           ((uint64_t)buffer[6] << 48)   |
           ((uint64_t)buffer[7] << 56);
}

/* ========== Variable-Length Encoding Implementation ========== */

/**
 * EncodeVarint32 - Encode a 32-bit value as a Base-128 Varint.
 * 
 * Algorithm:
 * 1. While value >= 128 (has bits beyond 7):
 *    - Write (value & 0x7F) | 0x80 (set continuation bit)
 *    - Shift value right by 7
 * 2. Write final byte (value & 0x7F) without continuation bit
 * 
 * Maximum Output: 5 bytes (32 bits / 7 bits per byte = 4.57, rounded up)
 */
char* EncodeVarint32(char* dst, uint32_t v) {
    uint8_t* ptr = (uint8_t*)dst;
    
    // Static assertion: Maximum varint32 size is 5 bytes
    // (32 bits of data requires ceil(32/7) = 5 bytes)
    static const int B = 128;  // Continuation flag threshold
    
    while (v >= B) {
        // Write low 7 bits and set continuation bit (bit 7) to say "more bytes follow".
        *ptr = (uint8_t)(v | B);
        v >>= 7;      // Drop the 7 bits we just wrote; keep encoding the rest.
        ptr++;        // Advance output pointer.
    }
    
    // Write the final byte (continuation bit clear because this is the last chunk).
    *ptr = (uint8_t)v;
    ptr++;
    
    return (char*)ptr;
}

/**
 * EncodeVarint64 - Encode a 64-bit value as a Base-128 Varint.
 * 
 * Maximum Output: 10 bytes (64 bits / 7 bits per byte = 9.14, rounded up)
 */
char* EncodeVarint64(char* dst, uint64_t v) {
    uint8_t* ptr = (uint8_t*)dst;
    static const uint64_t B = 128;
    
    while (v >= B) {
        *ptr = (uint8_t)(v | B); // Low 7 bits + continuation flag
        v >>= 7;                 // Keep encoding remaining bits
        ptr++;
    }
    
    *ptr = (uint8_t)v; // Last byte, flag cleared
    ptr++;
    
    return (char*)ptr;
}

/**
 * GetVarint32Ptr - Decode a Varint32 with bounds checking.
 * 
 * Safety Features:
 * - Stops if we hit 'limit' (prevents buffer overrun)
 * - Stops after 5 bytes (prevents infinite loop on corrupted data)
 * - Returns NULL on error
 * 
 * Decoding Algorithm:
 * -------------------
 * result = 0
 * shift = 0
 * for each byte b:
 *     result |= (b & 0x7F) << shift
 *     if (b & 0x80) == 0:  // No continuation bit
 *         return result
 *     shift += 7
 */
const char* GetVarint32Ptr(const char* p, const char* limit, uint32_t* value) {
    if (p >= limit) {
        return NULL;  // Empty input
    }
    
    uint32_t result = 0;
    uint32_t shift = 0;
    
    // Process up to 5 bytes (5 * 7 = 35 bits, enough for 32-bit value)
    for (int i = 0; i < 5; i++) {
        if (p >= limit) {
            return NULL;  // Incomplete varint
        }
        
        uint8_t byte = (uint8_t)(*p);
        p++;
        
        // Add the 7 data bits to the result
        if (byte < 128) {
            // Last byte (no continuation bit)
            result |= ((uint32_t)byte) << shift;  // Place these 7 bits at current shift position
            *value = result;
            return p;
        } else {
            // More bytes coming
            result |= ((uint32_t)(byte & 127)) << shift; // Mask off flag, keep data bits
            shift += 7;                                   // Next chunk will be 7 bits higher
        }
    }
    
    // If we get here, the varint is too long (>5 bytes for 32-bit)
    // This indicates corrupted data
    return NULL;
}

/**
 * GetVarint64Ptr - Decode a Varint64 with bounds checking.
 * 
 * Maximum: 10 bytes (10 * 7 = 70 bits, enough for 64-bit value)
 */
const char* GetVarint64Ptr(const char* p, const char* limit, uint64_t* value) {
    if (p >= limit) {
        return NULL;
    }
    
    uint64_t result = 0;
    uint32_t shift = 0;
    
    // Process up to 10 bytes
    for (int i = 0; i < 10; i++) {
        if (p >= limit) {
            return NULL;
        }
        
        uint8_t byte = (uint8_t)(*p);
        p++;
        
        if (byte < 128) {
            // Last byte
            result |= ((uint64_t)byte) << shift;  // Drop into place and finish
            *value = result;
            return p;
        } else {
            // More bytes coming
            result |= ((uint64_t)(byte & 127)) << shift; // Accumulate 7-bit payload
            shift += 7;                                  // Prepare for next chunk
        }
    }
    
    // Varint too long
    return NULL;
}

/**
 * VarintLength - Calculate the encoded length of a varint.
 * 
 * This is useful for pre-allocating buffers or calculating total size
 * before encoding.
 * 
 * Logic: Count how many 7-bit chunks are needed.
 * - 1 byte:  [0, 127]
 * - 2 bytes: [128, 16383]
 * - 3 bytes: [16384, 2097151]
 * - ...
 */
int VarintLength(uint64_t v) {
    int len = 1;
    
    while (v >= 128) {
        v >>= 7;
        len++;
    }
    
    return len;
}
