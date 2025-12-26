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

#include "core/table/block_builder.h"
#include "util/coding.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/*
 * BlockBuilder Internal Structure
 */
struct Lithos_BlockBuilder {
    const Lithos_Options* options;  // Configuration (restart interval, etc.)
    
    /* Data Buffer (the actual block content being built) */
    char* buffer;              // Growing buffer for compressed entries
    size_t buffer_size;        // Current used bytes in buffer
    size_t buffer_capacity;    // Allocated capacity of buffer
    
    /* Restart Points Array */
    uint32_t* restarts;        // Array of entry offsets where compression resets
    size_t restarts_count;     // Number of restart points added
    size_t restarts_capacity;  // Allocated capacity of restarts array
    
    /* State */
    int counter;               // Keys added since last restart (0 to restart_interval-1)
    bool finished;             // Has Finish() been called? (prevents further adds)
    
    /* Last Key (for prefix compression) */
    char* last_key;            // Copy of previous key for shared prefix calculation
    size_t last_key_size;      // Length of last_key
    size_t last_key_capacity;  // Allocated capacity of last_key
};

/* ============ Helper: Buffer Management ============ */

/*
 * Buffer_Append: Append data to the block buffer, growing if needed.
 * ================================================================
 * Input: BlockBuilder*, data buffer, size
 * Output: void (buffer grows internally)
 * Intent: Add bytes to the block content. Uses doubling strategy to amortize
 *         realloc costs. Called for every key suffix, value, and metadata.
 */
static void Buffer_Append(Lithos_BlockBuilder* b, const char* data, size_t size) {
    // Check if we need to grow the buffer
    if (b->buffer_size + size > b->buffer_capacity) {
        size_t new_capacity = b->buffer_capacity * 2;  // Double capacity
        if (new_capacity < b->buffer_size + size) {
            new_capacity = b->buffer_size + size;  // Or just enough if doubling isn't sufficient
        }
        b->buffer = (char*)realloc(b->buffer, new_capacity);
        assert(b->buffer != NULL);  // In production, handle realloc failure
        b->buffer_capacity = new_capacity;
    }
    
    // Append the data
    memcpy(b->buffer + b->buffer_size, data, size);
    b->buffer_size += size;
}

/*
 * Buffer_AppendVarint32: Encode and append a 32-bit varint.
 * =========================================================
 * Input: BlockBuilder*, uint32_t value
 * Output: void
 * Intent: Encode the value as a variable-length integer and append to buffer.
 *         Used for shared_len, non_shared_len, value_len in entries.
 */
static void Buffer_AppendVarint32(Lithos_BlockBuilder* b, uint32_t value) {
    char buf[5];
    char* ptr = EncodeVarint32(buf, value);
    Buffer_Append(b, buf, ptr - buf);
}

/**
 * Add a restart point (record current buffer offset).
 */
static void AddRestartPoint(Lithos_BlockBuilder* b) {
    // Grow restarts array if needed
    if (b->restarts_count >= b->restarts_capacity) {
        size_t new_capacity = (b->restarts_capacity == 0) ? 8 : (b->restarts_capacity * 2);
        b->restarts = (uint32_t*)realloc(b->restarts, new_capacity * sizeof(uint32_t));
        assert(b->restarts != NULL);
        b->restarts_capacity = new_capacity;
    }
    
    // Record the current offset
    b->restarts[b->restarts_count++] = (uint32_t)b->buffer_size;
}

/**
 * Update last_key (deep copy).
 */
static void UpdateLastKey(Lithos_BlockBuilder* b, const char* key, size_t key_size) {
    // Resize last_key buffer if needed
    if (key_size > b->last_key_capacity) {
        size_t new_capacity = key_size * 2;
        b->last_key = (char*)realloc(b->last_key, new_capacity);
        assert(b->last_key != NULL);
        b->last_key_capacity = new_capacity;
    }
    
    // Copy the key
    memcpy(b->last_key, key, key_size);
    b->last_key_size = key_size;
}

/* ============ Public API ============ */

/*
 * BlockBuilder_Create: Allocate and initialize a new block builder.
 * ============================================================
 * Input: const Lithos_Options* (configuration with restart interval, etc.)
 * Output: Lithos_BlockBuilder* (ready to accept KV pairs) or NULL on alloc failure
 * Intent: Set up the builder with initial buffer capacities and empty state.
 *         The builder starts ready to accept the first key (which becomes a restart).
 */
Lithos_BlockBuilder* BlockBuilder_Create(const Lithos_Options* options) {
    Lithos_BlockBuilder* b = (Lithos_BlockBuilder*)malloc(sizeof(Lithos_BlockBuilder));
    assert(b != NULL);
    
    b->options = options;
    
    // Initialize buffer (starts small, grows as we add entries)
    b->buffer_capacity = 1024; // Start with 1KB
    b->buffer = (char*)malloc(b->buffer_capacity);
    assert(b->buffer != NULL);
    b->buffer_size = 0;
    
    // Initialize restarts array (grows as we add restart points)
    b->restarts_capacity = 8;
    b->restarts = (uint32_t*)malloc(b->restarts_capacity * sizeof(uint32_t));
    assert(b->restarts != NULL);
    b->restarts_count = 0;
    
    // Initialize state
    b->counter = 0;  // No keys added yet
    b->finished = false;  // Not finished, can add keys
    
    // Initialize last_key buffer (for prefix compression)
    b->last_key_capacity = 64;
    b->last_key = (char*)malloc(b->last_key_capacity);
    assert(b->last_key != NULL);
    b->last_key_size = 0;
    
    // Add the first restart point (offset 0)
    AddRestartPoint(b);
    
    return b;
}

void BlockBuilder_Destroy(Lithos_BlockBuilder* b) {
    if (b == NULL) return;
    
    free(b->buffer);
    free(b->restarts);
    free(b->last_key);
    free(b);
}

void BlockBuilder_Reset(Lithos_BlockBuilder* b) {
    /* Reuse buffers; keep allocations to amortize over multiple blocks. */
    b->buffer_size = 0;
    b->restarts_count = 0;
    b->counter = 0;
    b->finished = false;
    b->last_key_size = 0;
    
    // Add the first restart point
    AddRestartPoint(b);
}

/*
 * BlockBuilder_Add: Add a key-value pair to the block with compression.
 * ================================================================
 * Input: BlockBuilder*, key slice, value slice
 * Output: void (appends to internal buffer)
 * Intent: Compress the key relative to the previous key (unless at restart),
 *         encode the entry format, and append to the block buffer. Adds restart
 *         points every N keys to enable binary search.
 */
void BlockBuilder_Add(Lithos_BlockBuilder* b, Lithos_Slice key, Lithos_Slice value) {
    assert(!b->finished); // Cannot add after Finish()
    assert(b->counter <= (int)b->options->block_restart_interval);
    
    size_t shared = 0;  // Bytes of shared prefix with previous key
    
    // Check if we need a restart point (every N keys)
    if (b->counter >= (int)b->options->block_restart_interval) {
        // Add restart point and reset compression (shared = 0)
        AddRestartPoint(b);
        b->counter = 0;
    } else {
        // Calculate shared prefix with last_key
        const size_t min_length = (key.size < b->last_key_size) ? key.size : b->last_key_size;
        while (shared < min_length && b->last_key[shared] == key.data[shared]) {
            shared++;  // Count matching prefix bytes
        }
    }
    
    const size_t non_shared = key.size - shared;  // Bytes that differ
    
    // Encode entry header: shared_len | non_shared_len | value_len (all varints)
    Buffer_AppendVarint32(b, (uint32_t)shared);
    Buffer_AppendVarint32(b, (uint32_t)non_shared);
    Buffer_AppendVarint32(b, (uint32_t)value.size);
    
    // Append the actual data: key_suffix | value
    Buffer_Append(b, key.data + shared, non_shared);  // Only the differing part of key
    Buffer_Append(b, value.data, value.size);
    
    // Update last_key for next compression calculation
    UpdateLastKey(b, key.data, key.size);
    
    // Increment counter toward next restart
    b->counter++;
}

/*
 * BlockBuilder_Finish: Complete the block by appending restart metadata.
 * ================================================================
 * Input: BlockBuilder* (builder with entries added)
 * Output: Lithos_Slice (points to completed block data)
 * Intent: Append the restart offset array and count to the buffer, marking
 *         the block as finished. Returns a slice to the complete block data.
 *         After this, no more Add() calls are allowed.
 */
Lithos_Slice BlockBuilder_Finish(Lithos_BlockBuilder* b) {
    // Append all restart offsets as Fixed32 array (4 bytes each)
    for (size_t i = 0; i < b->restarts_count; i++) {
        char buf[4];
        EncodeFixed32(buf, b->restarts[i]);
        Buffer_Append(b, buf, 4);
    }
    
    // Append restart count as Fixed32 (last 4 bytes of block)
    char count_buf[4];
    EncodeFixed32(count_buf, (uint32_t)b->restarts_count);
    Buffer_Append(b, count_buf, 4);
    
    b->finished = true;  // No more additions allowed
    
    /* Note: Returned slice aliases internal buffer; valid until Reset/Destroy. */
    
    // Return slice pointing to the complete block
    Lithos_Slice result;
    result.data = b->buffer;
    result.size = b->buffer_size;
    return result;
}

size_t BlockBuilder_CurrentSizeEstimate(Lithos_BlockBuilder* b) {
    // If finished, return exact size
    if (b->finished) {
        return b->buffer_size;
    }
    
    // Otherwise: current buffer + estimated restart overhead
    // (4 bytes per restart + 4 bytes for count)
    return b->buffer_size + (b->restarts_count * 4) + 4;
}

bool BlockBuilder_Empty(Lithos_BlockBuilder* b) {
    return b->buffer_size == 0;
}
