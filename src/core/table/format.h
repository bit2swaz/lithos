/*
 * format.h - SSTable Format Structures
 *
 * Defines the on-disk format primitives for navigating SSTable files.
 * These structures allow efficient random access without scanning the entire
 * file.
 *
 * Key Insight:
 * By storing the Footer at a FIXED position (FileSize - 48 bytes), we can:
 * 1. Read the Footer first
 * 2. Extract the Index Block location
 * 3. Binary search the Index Block to find Data Blocks
 * 4. Avoid scanning the entire file sequentially
 */

#ifndef LITHOS_CORE_TABLE_FORMAT_H_
#define LITHOS_CORE_TABLE_FORMAT_H_

#include "util/slice.h"
#include "util/status.h"
#include <stdbool.h>
#include <stdint.h>

/* Compression types for block trailers */
#define LITHOS_COMPRESSION_NONE 0
#define LITHOS_COMPRESSION_RLE 1

/*
 * BlockHandle: A pointer to a block within an SSTable file.
 *
 * Encodes the offset and size of a block using Varint64 for space efficiency.
 * Maximum encoded length is 20 bytes (10 bytes per Varint64 worst case).
 */
typedef struct {
  uint64_t offset; // Byte offset from start of file
  uint64_t size;   // Size of the block in bytes
} Lithos_BlockHandle;

/*
 * Initialize a BlockHandle to invalid state.
 */
void BlockHandle_Init(Lithos_BlockHandle *h);

/*
 * Check if a BlockHandle has been set (valid).
 */
bool BlockHandle_IsValid(const Lithos_BlockHandle *h);

/*
 * Encode a BlockHandle into the destination buffer.
 *
 * Format: Varint64(offset) + Varint64(size)
 *
 * Returns: Number of bytes written (max 20)
 */
size_t BlockHandle_Encode(const Lithos_BlockHandle *h, char *dst);

/*
 * Decode a BlockHandle from the source buffer.
 *
 * Returns: LITHOS_OK on success, LITHOS_CORRUPTION on invalid encoding
 * Updates *h and advances *src past the encoded data.
 */
lithos_status_code BlockHandle_Decode(Lithos_BlockHandle *h, const char **src,
                                      const char *limit);

/*
 * Footer: Fixed-size trailer at the end of every SSTable file.
 *
 * Position: Always at (FileSize - kFooterEncodedLength)
 *
 * Layout:
 *   [MetaIndex BlockHandle] (Varint64 offset + Varint64 size, max 20 bytes)
 *   [Index BlockHandle]     (Varint64 offset + Varint64 size, max 20 bytes)
 *   [Padding]               (Zero-filled to align to 40 bytes)
 *   [Magic Number]          (8 bytes: 0xdb4775248b80fb57)
 *   Total: 48 bytes
 *
 * The Magic Number allows quick validation that this is a valid SSTable.
 * The MetaIndex points to metadata (Bloom filters, stats).
 * The Index points to the Data Block index.
 */
typedef struct {
  Lithos_BlockHandle metaindex_handle; // Points to the MetaIndex Block
  Lithos_BlockHandle index_handle;     // Points to the Index Block
} Lithos_Footer;

/* Constants */
#define LITHOS_FOOTER_ENCODED_LENGTH 48
#define LITHOS_TABLE_MAGIC_NUMBER 0xdb4775248b80fb57ULL

/*
 * Initialize a Footer to invalid state.
 */
void Footer_Init(Lithos_Footer *f);

/*
 * Encode a Footer into exactly 48 bytes.
 *
 * Why 48 bytes?
 * - 2 BlockHandles * 20 bytes (max) = 40 bytes
 * - 8 bytes for Magic Number
 * - We pad to 48 for alignment and fixed-size access
 *
 * This fixed size is CRITICAL: it allows reading the Footer by seeking to
 * (FileSize - 48) without knowing anything else about the file structure.
 */
void Footer_Encode(const Lithos_Footer *f, char *dst);

/*
 * Decode a Footer from exactly 48 bytes.
 *
 * Returns: LITHOS_OK on success
 *          LITHOS_CORRUPTION if magic number doesn't match
 */
lithos_status_code Footer_Decode(Lithos_Footer *f, const char *src);

#endif // LITHOS_CORE_TABLE_FORMAT_H_
