/*
 * BlockBuilder: Compresses KV pairs into SSTable data blocks.
 * ===========================================================
 * Takes a stream of sorted (key, value) pairs and builds a compressed block
 * with prefix compression and restart points for binary search.
 *
 * Big Picture: BlockBuilder = "KV Compressor with Restart Points"
 * ===============================================================
 * Converts individual KV pairs into a compressed block format. Every 16 keys,
 * it adds a restart point (uncompressed key) so readers can binary search.
 * Between restarts, it compresses keys by sharing prefixes with the previous key.
 * The result is a dense block that supports O(log N) seeks with minimal decompression.
 *
 * Where it fits: BlockBuilder is used by TableBuilder to assemble data blocks
 * within SSTable files. Each block becomes a unit of I/O and caching.
 *
 * Key Concepts:
 * - Restart intervals: Every N keys, reset compression for binary search.
 * - Prefix compression: Only store the differing suffix of each key.
 * - Buffer growth: Double capacity when needed to amortize realloc costs.
 */

#ifndef LITHOS_BLOCK_BUILDER_H
#define LITHOS_BLOCK_BUILDER_H

#include "lithos/options.h"
#include "util/slice.h"
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Opaque BlockBuilder structure.
 * 
 * Accumulates key-value pairs with prefix compression.
 * Must call Finish() before reading the constructed block.
 */
typedef struct Lithos_BlockBuilder Lithos_BlockBuilder;

/**
 * Create a new BlockBuilder.
 * 
 * @param options Configuration (block_restart_interval, etc.)
 * @return New builder instance (caller must destroy)
 */
Lithos_BlockBuilder* BlockBuilder_Create(const Lithos_Options* options);

/**
 * Destroy the BlockBuilder and free all memory.
 * 
 * @param b BlockBuilder to destroy
 */
void BlockBuilder_Destroy(Lithos_BlockBuilder* b);

/**
 * Reset the builder to empty state (reuse buffer).
 * 
 * @param b BlockBuilder to reset
 */
void BlockBuilder_Reset(Lithos_BlockBuilder* b);

/*
 * BlockBuilder_Add: Add a key-value pair to the block with compression.
 * ================================================================
 * Input: BlockBuilder*, key slice, value slice
 * Output: void (appends to internal buffer)
 * Intent: Compress the key relative to the previous key (unless at restart),
 *         encode the entry format, and append to the block buffer. Adds restart
 *         points every N keys to enable binary search.
 * Preconditions: Keys must be added in sorted order; Finish() not called yet.
 */
void BlockBuilder_Add(Lithos_BlockBuilder* b, Lithos_Slice key, Lithos_Slice value);

/*
 * BlockBuilder_Finish: Complete the block by appending restart metadata.
 * ================================================================
 * Input: BlockBuilder* (builder with entries added)
 * Output: Lithos_Slice (points to completed block data)
 * Intent: Append the restart offset array and count to the buffer, marking
 *         the block as finished. Returns a slice to the complete block data.
 *         After this, no more Add() calls are allowed.
 */
Lithos_Slice BlockBuilder_Finish(Lithos_BlockBuilder* b);

/**
 * Estimate current size of the block (before Finish).
 * 
 * @param b BlockBuilder
 * @return Approximate size in bytes
 * 
 * Note: This is the buffer size + estimated restart array size.
 */
size_t BlockBuilder_CurrentSizeEstimate(Lithos_BlockBuilder* b);

/**
 * Check if the builder is empty.
 * 
 * @param b BlockBuilder
 * @return true if no keys have been added
 */
bool BlockBuilder_Empty(Lithos_BlockBuilder* b);

#ifdef __cplusplus
}
#endif

#endif /* LITHOS_BLOCK_BUILDER_H */
