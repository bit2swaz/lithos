/**
 * log_format.h - Write-Ahead Log Physical Format
 * 
 * Author: Aditya (@bit2swaz)
 * 
 * The WAL is a sequence of 32KB blocks. Each block contains one or more records.
 * If a record doesn't fit in the remaining space of a block, it is fragmented
 * across multiple blocks using FIRST, MIDDLE, and LAST record types.
 * 
 * Physical Layout:
 * 
 *   [Block 0: 32KB]
 *   [Block 1: 32KB]
 *   [Block 2: 32KB]
 *   ...
 * 
 * Each record has a 7-byte header:
 * 
 *   | Checksum (4B) | Length (2B) | Type (1B) | Payload (N bytes) |
 * 
 * - Checksum: CRC32C of (Type byte + Payload). Little-Endian.
 * - Length: Payload length (0-65535). Little-Endian uint16_t.
 * - Type: Record type (Full, First, Middle, Last, Zero).
 * 
 * Fragmentation Example:
 * 
 *   User writes 70KB record:
 *   - Block 0: FIRST (32KB - 7 = 32761 bytes of payload)
 *   - Block 1: MIDDLE (32761 bytes)
 *   - Block 2: LAST (remaining ~4478 bytes)
 * 
 * Why 32KB?
 * - Aligns with OS page cache (typically 4KB pages, so 8 pages per block).
 * - Allows efficient sequential reads (modern SSDs prefer larger I/O).
 * - Balances between fragmentation overhead and read amplification.
 */

#ifndef LITHOS_LOG_FORMAT_H
#define LITHOS_LOG_FORMAT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Block size: 32KB
 * All WAL data is aligned to this boundary.
 */
#define kBlockSize 32768

/**
 * Header size: 7 bytes
 * 
 * Layout:
 *   uint32_t checksum  (4 bytes, Little-Endian)
 *   uint16_t length    (2 bytes, Little-Endian)
 *   uint8_t  type      (1 byte)
 */
#define kHeaderSize 7

/**
 * Maximum payload per record: 65535 bytes (uint16_t max).
 * In practice, limited by (kBlockSize - kHeaderSize) = 32761 bytes per fragment.
 */
#define kMaxRecordSize 65535

/**
 * RecordType - Indicates how the record fits in the block.
 * 
 * State Machine for Writer:
 * 1. If entire record fits: FULL.
 * 2. If record spans blocks: FIRST -> MIDDLE* -> LAST.
 * 
 * State Machine for Reader:
 * - FULL: Return immediately.
 * - FIRST: Start accumulating.
 * - MIDDLE: Continue accumulating.
 * - LAST: Finish accumulation and return.
 * - ZERO: Preallocated space (filler), skip.
 */
typedef enum {
    /**
     * kZeroType: Reserved for preallocated space.
     * 
     * Use Case: If a record doesn't fit in the remaining block space
     * (e.g., only 6 bytes left, but header needs 7), we fill the rest
     * with zeros and start fresh in the next block.
     */
    kZeroType = 0,
    
    /**
     * kFullType: The entire record fits in one block.
     * 
     * Most common case for small records (< 32KB).
     */
    kFullType = 1,
    
    /**
     * kFirstType: Start of a fragmented record.
     * 
     * Indicates that more fragments follow in subsequent blocks.
     */
    kFirstType = 2,
    
    /**
     * kMiddleType: Middle fragment of a large record.
     * 
     * There may be multiple MIDDLE fragments between FIRST and LAST.
     */
    kMiddleType = 3,
    
    /**
     * kLastType: Final fragment of a large record.
     * 
     * After reading this, the reader has the complete record.
     */
    kLastType = 4
} RecordType;

/**
 * Helper: Check if a RecordType is valid.
 */
static inline int RecordType_IsValid(uint8_t type) {
    return type <= kLastType;
}

#ifdef __cplusplus
}
#endif

#endif // LITHOS_LOG_FORMAT_H
