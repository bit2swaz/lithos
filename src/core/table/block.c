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
 * - Two-level iteration: index block chooses data block, data block scans
 * entries.
 */

#include "core/table/block.h"
#include "util/coding.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

/* Forward declarations */
static void BlockIter_Next(void *state);

/*
 * Block structure - in-memory view after parsing the raw bytes.
 */
struct Lithos_Block {
  const char
      *data;   // Raw block data (owned elsewhere, e.g., cache or file buffer).
  size_t size; // Total block size including entries and restart array.
  uint32_t restart_offset; // Byte offset where restart array starts (from block
                           // start).
  uint32_t num_restarts;   // How many restart points (each is a uint32 offset).
  bool
      owned; // If true, we free data on destroy (e.g., from heap-alloc'd read).
};

/*
 * BlockIterator state - forward iterator that reconstructs keys.
 */
typedef struct {
  Lithos_Block *block; // The block we're iterating over.
  Lithos_Comparator
      cmp; // Comparator for binary search (usually InternalKeyComparator).

  /* Current position state */
  uint32_t current_offset; // Offset of current entry in block data.
  uint32_t restart_index;  // Which restart point covers the current entry.

  /* Current key buffer */
  char *key_buf;       // Reconstructed full key (malloc'd, grows as needed).
  size_t key_capacity; // Allocated size of key_buf.
  size_t key_size;     // Current length of reconstructed key.

  /* Current value */
  Lithos_Slice value; // Points directly into block data (no copy).

  Status status; // Error state (e.g., corruption detected).
  bool valid;    // True if positioned at a valid entry.
} BlockIterator;

/*
 * Helper: Get the offset of a restart point by index
 */
/* Helper: Fetch restart offset by index (fixed32 array at end of block). */
static uint32_t Block_GetRestartOffset(const Lithos_Block *block,
                                       uint32_t index) {
  assert(index < block->num_restarts);
  const char *restart_ptr = block->data + block->restart_offset + index * 4;
  return DecodeFixed32(restart_ptr);
}

/*
 * Block_Create: Parse raw block bytes into a structured Block object.
 * ===================================================================
 * Input: BlockContents (raw data buffer + size + ownership flag)
 * Output: Lithos_Block* (parsed block) or NULL on corruption/error
 * Intent: Validate the block footer (restart array + count) and create the
 *         in-memory representation. Takes ownership of the buffer if
 * heap_allocated. This is the entry point for turning file bytes into a
 * queryable block.
 */
Lithos_Block *Block_Create(Lithos_BlockContents contents) {
  if (contents.size < sizeof(uint32_t)) {
    /* Block too small to even contain restart count */
    if (contents.heap_allocated) {
      free((void *)contents.data);
    }
    return NULL;
  }

  Lithos_Block *block = malloc(sizeof(Lithos_Block));
  if (!block) {
    if (contents.heap_allocated) {
      free((void *)contents.data);
    }
    return NULL;
  }

  /* Restart count is stored in the last 4 bytes. */
  block->num_restarts = DecodeFixed32(contents.data + contents.size - 4);

  /* Restart array lives immediately before the count. */
  uint32_t max_restarts_allowed = (contents.size - 4) / 4;
  if (block->num_restarts > max_restarts_allowed) {
    /* Corrupted block - restart array would overflow the block */
    free(block);
    if (contents.heap_allocated) {
      free((void *)contents.data);
    }
    return NULL;
  }

  /* Calculate where restart array starts: size - 4*(num_restarts + 1) */
  block->restart_offset = contents.size - (1 + block->num_restarts) * 4;
  block->data = contents.data;
  block->size = contents.size;
  block->owned = contents.heap_allocated;

  return block;
}

/*
 * Block_Destroy: Clean up a Block object and its owned resources.
 * ============================================================
 * Input: Lithos_Block* (the block to destroy)
 * Output: void
 * Intent: Free the block struct and, if owned=true, the underlying data buffer.
 *         Safe to call with NULL (no-op).
 */
void Block_Destroy(Lithos_Block *block) {
  if (block) {
    if (block->owned) {
      free((void *)block->data);
    }
    free(block);
  }
}

uint32_t Block_GetRestartCount(const Lithos_Block *block) {
  return block->num_restarts;
}

/*
 * BlockIter_ParseEntry: Decode one compressed entry and update iterator state.
 * ========================================================================
 * Input: BlockIterator* (iterator to update), uint32_t (byte offset in block)
 * Output: bool (true=success, false=corruption)
 * Intent: Parse shared_len/non_shared_len/value_len from the entry, reconstruct
 *         the full key by combining previous key prefix + new suffix, and set
 *         the value slice. This is the core decompression logic.
 */
static bool BlockIter_ParseEntry(BlockIterator *iter, uint32_t offset) {
  if (offset >= iter->block->restart_offset) {
    /* Offset points into restart array or beyond - invalid entry */
    iter->valid = false;
    return false;
  }

  const char *p = iter->block->data + offset; // Current parse position
  const char *limit =
      iter->block->data +
      iter->block->restart_offset; // Don't read into restart array

  /* Decode the three varint lengths: shared_bytes, non_shared_bytes,
   * value_length */
  uint64_t shared, non_shared, value_len;
  p = GetVarint64Ptr(p, limit, &shared);
  if (!p)
    goto corruption; // Varint decode failed (corruption or end of data)
  p = GetVarint64Ptr(p, limit, &non_shared);
  if (!p)
    goto corruption;
  p = GetVarint64Ptr(p, limit, &value_len);
  if (!p)
    goto corruption;

  /* Sanity checks: shared bytes can't exceed current key, and data must fit */
  if (shared > iter->key_size)
    goto corruption; // Can't share more than we have
  if (p + non_shared + value_len > limit)
    goto corruption; // Would overflow block

  /* Reconstruct full key: keep first 'shared' bytes of prev key + 'non_shared'
   * new bytes */
  size_t total_key_size = shared + non_shared;
  if (total_key_size > iter->key_capacity) {
    /* Grow key buffer to fit (double size to amortize reallocs) */
    iter->key_capacity = total_key_size * 2;
    iter->key_buf = realloc(iter->key_buf, iter->key_capacity);
    if (!iter->key_buf) {
      iter->status = Status_IOError("Out of memory", "");
      iter->valid = false;
      return false;
    }
  }

  /* Copy the new key suffix after the shared prefix */
  memcpy(iter->key_buf + shared, p, non_shared);
  iter->key_size = total_key_size;
  p += non_shared; // Advance past key suffix

  /* Value is stored inline after key - just set slice to point into block */
  iter->value.data = p;
  iter->value.size = value_len;

  iter->current_offset = offset; // Remember where this entry starts
  iter->valid = true;
  return true;

corruption:
  iter->status = Status_Corruption("Block entry corrupted", "");
  iter->valid = false;
  return false;
}

/*
 * BlockIter_SeekToRestartPoint: Binary search restart points to find seek
 * start.
 * ========================================================================
 * Input: BlockIterator* (iterator), Lithos_Slice (target key to find)
 * Output: uint32_t (restart index where linear scan should begin)
 * Intent: Use binary search on restart points (which have uncompressed keys) to
 *         find the restart region containing or just before the target key.
 *         This enables O(log N) seeks followed by O(K) linear scan.
 */
static uint32_t BlockIter_SeekToRestartPoint(BlockIterator *iter,
                                             Lithos_Slice target) {
  /* Binary search restart points: find largest restart where key <= target */
  uint32_t left = 0;
  uint32_t right = iter->block->num_restarts - 1;

  while (left < right) {
    uint32_t mid = (left + right + 1) / 2; // Bias toward right for upper bound
    uint32_t offset = Block_GetRestartOffset(iter->block, mid);

    /* Parse the key at this restart point (restart keys are uncompressed) */
    if (!BlockIter_ParseEntry(iter, offset)) {
      return left; // Corrupted - return safe fallback
    }

    Lithos_Slice mid_key = {iter->key_buf, iter->key_size};
    int cmp = Slice_Compare(mid_key, target);

    if (cmp < 0) {
      left = mid; // mid key < target, so search right half
    } else {
      right = mid - 1; // mid key >= target, so search left half
    }
  }

  return left; // Left is the restart index to start linear scan from
}

/* ============================================================================
 * Iterator VTable Implementation
 * ============================================================================
 */

static bool BlockIter_Valid(void *state) {
  BlockIterator *iter = (BlockIterator *)state;
  return iter->valid;
}

/*
 * BlockIter_SeekToFirst: Position iterator at the first entry in the block.
 * ================================================================
 * Input: void* (BlockIterator state)
 * Output: void (iterator positioned at first entry or invalid)
 * Intent: Jump to the first restart point (offset 0) and parse the first entry.
 *         This is O(1) since restart points are at known offsets.
 */
static void BlockIter_SeekToFirst(void *state) {
  BlockIterator *iter = (BlockIterator *)state;

  if (iter->block->num_restarts == 0) {
    iter->valid = false; // Empty block has no entries
    return;
  }

  /* First restart point is always at offset 0 (first entry) */
  uint32_t offset = Block_GetRestartOffset(iter->block, 0);
  iter->restart_index = 0;
  BlockIter_ParseEntry(iter, offset); // Parse and position at first entry
}

/*
 * BlockIter_SeekToLast: Position iterator at the last entry in the block.
 * ================================================================
 * Input: void* (BlockIterator state)
 * Output: void (iterator positioned at last entry or invalid)
 * Intent: Start from the last restart point and scan forward to the final
 * entry. This requires parsing entries but gives us the true last key.
 */
static void BlockIter_SeekToLast(void *state) {
  BlockIterator *iter = (BlockIterator *)state;

  if (iter->block->num_restarts == 0) {
    iter->valid = false; // Empty block
    return;
  }

  /* Start from the last restart point (likely contains the last entries) */
  iter->restart_index = iter->block->num_restarts - 1;
  uint32_t offset = Block_GetRestartOffset(iter->block, iter->restart_index);
  BlockIter_ParseEntry(iter, offset);

  /* Scan forward from last restart to find the actual last entry */
  while (iter->valid) {
    /* Calculate next entry offset: current + (key_len + value_len + varints) */
    uint32_t next_offset =
        iter->current_offset + (iter->value.data + iter->value.size -
                                (iter->block->data + iter->current_offset));

    if (next_offset >= iter->block->restart_offset) {
      break; /* Next entry would be in restart array - we're at the end */
    }

    if (!BlockIter_ParseEntry(iter, next_offset)) {
      break; // Corrupted entry
    }
  }
}

/*
 * BlockIter_Seek: Position iterator at first key >= target using binary search.
 * ========================================================================
 * Input: void* (BlockIterator state), Lithos_Slice (target key)
 * Output: void (iterator positioned or invalid)
 * Intent: Use binary search on restart points to find the right region, then
 *         scan linearly to find the exact position. This gives O(log N) + O(K)
 *         seek performance where K is entries per restart block.
 */
static void BlockIter_Seek(void *state, Lithos_Slice target) {
  BlockIterator *iter = (BlockIterator *)state;

  if (iter->block->num_restarts == 0) {
    iter->valid = false; // Empty block
    return;
  }

  /* Phase 1: Binary search restart points to find starting region */
  uint32_t restart_index = BlockIter_SeekToRestartPoint(iter, target);
  iter->restart_index = restart_index;

  /* Start parsing from the chosen restart point */
  uint32_t offset = Block_GetRestartOffset(iter->block, restart_index);
  BlockIter_ParseEntry(iter, offset);

  /* Phase 2: Linear scan forward to find exact position */
  while (iter->valid) {
    Lithos_Slice current_key = {iter->key_buf, iter->key_size};
    int cmp = Slice_Compare(current_key, target);

    if (cmp >= 0) {
      return; /* Found key >= target - we're done */
    }

    BlockIter_Next(iter); // Advance to next entry
  }
}

/*
 * BlockIter_Next: Advance iterator to the next entry in the block.
 * ================================================================
 * Input: void* (BlockIterator state)
 * Output: void (iterator advanced or marked invalid)
 * Intent: Calculate the offset of the next entry by skipping over the current
 *         entry's data, then parse it. Update restart_index if we cross a
 * boundary.
 */
static void BlockIter_Next(void *state) {
  BlockIterator *iter = (BlockIterator *)state;

  if (!iter->valid) {
    return; // Can't advance from invalid position
  }

  /* Calculate offset of next entry by skipping current entry's data */
  uint32_t next_offset = iter->current_offset;
  const char *p = iter->block->data + iter->current_offset;
  const char *limit = iter->block->data + iter->block->restart_offset;

  /* Re-parse the varint lengths to skip: shared_len + non_shared_len +
   * value_len */
  uint64_t shared, non_shared, value_len;
  p = GetVarint64Ptr(p, limit, &shared);
  p = GetVarint64Ptr(p, limit, &non_shared);
  p = GetVarint64Ptr(p, limit, &value_len);

  if (!p) {
    iter->valid = false; // Corrupted varints
    return;
  }

  /* Next entry starts after: varints + key_suffix + value */
  next_offset = (p - iter->block->data) + non_shared + value_len;

  /* Check if we reached the end of the block */
  if (next_offset >= iter->block->restart_offset) {
    iter->valid = false; // No more entries
    return;
  }

  /* Update restart_index if we just crossed into a new restart region */
  if (iter->restart_index + 1 < iter->block->num_restarts) {
    uint32_t next_restart_offset =
        Block_GetRestartOffset(iter->block, iter->restart_index + 1);
    if (next_offset == next_restart_offset) {
      iter->restart_index++; // Now in next restart region
    }
  }

  BlockIter_ParseEntry(iter, next_offset); // Parse and position at next entry
}

static void BlockIter_Prev(void *state) {
  BlockIterator *iter = (BlockIterator *)state;

  /* TODO: Implement backward iteration properly */
  /* For now, just mark as invalid */
  (void)iter;
  iter->valid = false;
}

/*
 * BlockIter_Key: Return the current entry's key.
 * ================================================
 * Input: void* (BlockIterator state)
 * Output: Lithos_Slice (current key, valid only while iterator is positioned)
 * Intent: Provide access to the reconstructed key for the current entry.
 */
static Lithos_Slice BlockIter_Key(void *state) {
  BlockIterator *iter = (BlockIterator *)state;
  assert(iter->valid);
  Lithos_Slice key = {iter->key_buf, iter->key_size};
  return key;
}

/*
 * BlockIter_Value: Return the current entry's value.
 * ================================================
 * Input: void* (BlockIterator state)
 * Output: Lithos_Slice (current value, points directly into block data)
 * Intent: Provide access to the value for the current entry (no copy needed).
 */
static Lithos_Slice BlockIter_Value(void *state) {
  BlockIterator *iter = (BlockIterator *)state;
  assert(iter->valid);
  return iter->value;
}

/*
 * BlockIter_GetStatus: Return any error status from iterator operations.
 * ================================================================
 * Input: void* (BlockIterator state)
 * Output: Status (OK or error details)
 * Intent: Report corruption or allocation errors encountered during iteration.
 */
static Status BlockIter_GetStatus(void *state) {
  BlockIterator *iter = (BlockIterator *)state;
  return iter->status;
}

/*
 * BlockIter_Cleanup: Free iterator resources.
 * ================================================================
 * Input: void* (BlockIterator state)
 * Output: void
 * Intent: Release the key buffer and iterator struct. Called when iterator
 *         is destroyed.
 */
static void BlockIter_Cleanup(void *state) {
  BlockIterator *iter = (BlockIterator *)state;
  free(iter->key_buf);
  free(iter);
}

/* VTable definition */
static const Lithos_IteratorVTable block_iter_vtable = {
    .Valid = BlockIter_Valid,
    .SeekToFirst = BlockIter_SeekToFirst,
    .SeekToLast = BlockIter_SeekToLast,
    .Seek = BlockIter_Seek,
    .Next = BlockIter_Next,
    .Prev = BlockIter_Prev,
    .Key = BlockIter_Key,
    .Value = BlockIter_Value,
    .GetStatus = BlockIter_GetStatus,
    .Cleanup = BlockIter_Cleanup};

/*
 * Block_NewIterator: Create a forward iterator over a block's entries.
 * ============================================================
 * Input: Lithos_Block* (the block to iterate), Lithos_Comparator (for seeks)
 * Output: Lithos_Iterator* (generic iterator interface) or NULL on alloc
 * failure Intent: Allocate and initialize a BlockIterator that can scan entries
 * sequentially or seek to specific keys using binary search on restart points.
 */
Lithos_Iterator *Block_NewIterator(Lithos_Block *block, Lithos_Comparator cmp) {
  BlockIterator *iter = calloc(1, sizeof(BlockIterator));
  if (!iter) {
    return NULL;
  }

  iter->block = block;
  iter->cmp = cmp;
  iter->status = Status_OK();
  iter->valid = false;
  iter->key_capacity = 256;
  iter->key_buf = malloc(iter->key_capacity);

  if (!iter->key_buf) {
    free(iter);
    return NULL;
  }

  Lithos_Iterator *result = malloc(sizeof(Lithos_Iterator));
  if (!result) {
    free(iter->key_buf);
    free(iter);
    return NULL;
  }

  result->vtable = &block_iter_vtable;
  result->state = iter;

  return result;
}
