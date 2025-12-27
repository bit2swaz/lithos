/*
 * WAL Format: Physical Layout for Write-Ahead Logging
 * ==================================================
 * Defines the on-disk format for the Write-Ahead Log, ensuring durability
 * and crash recovery for uncommitted data.
 *
 * Big Picture: WAL Format = "Sequential Log for Crash Recovery"
 * ===========================================================
 * Databases must survive crashes without losing committed data. The WAL
 * provides a sequential log of all changes. On recovery, we replay the WAL
 * to restore the database to a consistent state.
 *
 * Where it fits: WAL is written before MemTable changes are committed.
 * It's the "write path" that ensures durability.
 *
 * Key Concepts:
 * - 32KB blocks: Aligns with OS page cache for efficient I/O.
 * - Record fragmentation: Large records split across blocks.
 * - Checksums: CRC32C protects against corruption.
 * - Types: Full, First, Middle, Last for fragmentation handling.
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
 * In practice, limited by (kBlockSize - kHeaderSize) = 32761 bytes per
 * fragment.
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
static inline int RecordType_IsValid(uint8_t type) { return type <= kLastType; }

#ifdef __cplusplus
}
#endif

#endif // LITHOS_LOG_FORMAT_H
