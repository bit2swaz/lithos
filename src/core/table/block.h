/*
 * Data Blocks: Prefix-Compressed KV Storage
 * =========================================
 *
 * Each SSTable data block packs a run of sorted (key, value) pairs using
 * prefix compression. Restart points (every N keys) reset compression so we
 * can binary-search into the block without decompressing everything.
 *
 * Block layout (tail-first metadata):
 *   [entry 0][entry 1]...[entry M]
 *   [restart_0 offset (uint32)][restart_1 offset]...[restart_K offset]
 *   [num_restarts (uint32)]   <-- last 4 bytes of the block
 *
 * Entry layout:
 *   shared_len (varint)
 *   non_shared_len (varint)
 *   value_len (varint)
 *   key_suffix (non_shared_len bytes)
 *   value (value_len bytes)
 * Full key = prev_key[0:shared_len] + key_suffix
 *
 * Lookup strategy:
 * - Binary-search the restart offsets (shared_len=0 there) to find the right
 *   restart region, then scan forward linearly to the target key.
 */

/*
 * Big Picture: Data Blocks = "Compressed KV Runs with Binary Search"
 * ===================================================================
 * SSTables are split into data blocks to enable fast random access. Each block
 * contains hundreds of KV pairs with prefix compression (shared prefixes aren't
 * repeated). Restart points every 16 keys allow binary search: we jump to the
 * right restart, then scan forward. This gives O(log N) seeks with minimal
 * decompression overhead.
 *
 * Where it fits: Data blocks are the "meat" of SSTables. The index block maps
 * keys to block offsets, and readers load blocks on demand (with caching).
 *
 * Key Concepts:
 * - Prefix compression: saves space by reusing common key prefixes.
 * - Restart points: uncompressed keys for binary search entry points.
 * - Two-level iteration: index block chooses data block, data block scans entries.
 */

#ifndef LITHOS_CORE_TABLE_BLOCK_H_
#define LITHOS_CORE_TABLE_BLOCK_H_

#include "lithos/iterator.h"
#include "util/slice.h"
#include "util/status.h"
#include "core/dbformat.h"
#include "core/skiplist.h"  /* For Lithos_Comparator */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Lithos_BlockContents - Raw block data read from file.
 *
 * This is typically loaded into memory from disk and passed to Block_Create.
 */
typedef struct {
    const char* data;      // Block data (owned or borrowed)
    size_t size;           // Size of the block
    bool heap_allocated;   // If true, data is malloc'd and must be freed
} Lithos_BlockContents;

/*
 * Lithos_Block - Parsed block structure.
 *
 * Opaque to users. Internal representation includes:
 * - Pointer to the raw data
 * - Parsed restart array
 * - Block size
 */
typedef struct Lithos_Block Lithos_Block;

/*
 * Block_Create: Parse raw block bytes into a structured Block object.
 * ===================================================================
 * Input: BlockContents (raw data buffer + size + ownership flag)
 * Output: Lithos_Block* (parsed block) or NULL on corruption/error
 * Intent: Validate the block footer (restart array + count) and create the
 *         in-memory representation. Takes ownership of the buffer if heap_allocated.
 *         This is the entry point for turning file bytes into a queryable block.
 */
Lithos_Block* Block_Create(Lithos_BlockContents contents);

/*
 * Block_Destroy: Clean up a Block object and its owned resources.
 * ============================================================
 * Input: Lithos_Block* (the block to destroy)
 * Output: void
 * Intent: Free the block struct and, if owned=true, the underlying data buffer.
 *         Safe to call with NULL (no-op).
 */
void Block_Destroy(Lithos_Block* block);

/*
 * Block_NewIterator: Create a forward iterator over a block's entries.
 * ============================================================
 * Input: Lithos_Block* (the block to iterate), Lithos_Comparator (for seeks)
 * Output: Lithos_Iterator* (generic iterator interface) or NULL on alloc failure
 * Intent: Allocate and initialize a BlockIterator that can scan entries sequentially
 *         or seek to specific keys using binary search on restart points.
 */
Lithos_Iterator* Block_NewIterator(Lithos_Block* block, Lithos_Comparator cmp);

/*
 * Block_GetRestartCount - Get the number of restart points in the block.
 *
 * Useful for debugging and validation.
 */
uint32_t Block_GetRestartCount(const Lithos_Block* block);

#ifdef __cplusplus
}
#endif

#endif  // LITHOS_CORE_TABLE_BLOCK_H_
