/**
 * Lithos Storage Engine - Arena Memory Allocator Implementation
 * 
 * This file implements the bump-pointer arena allocator defined in arena.h.
 * 
 * MEMORY LAYOUT EXAMPLE:
 * ======================
 * 
 * Arena State:
 *   blocks = [Block0, Block1, Block2]
 *   alloc_ptr = Block2 + 156
 *   alloc_bytes_remaining = 3940
 * 
 * Block0: [4096 bytes, fully used]
 * Block1: [4096 bytes, fully used]
 * Block2: [156 bytes used | 3940 bytes free]
 *          ^alloc_ptr points here
 * 
 * On next allocation (64 bytes):
 *   1. Check: 64 <= 3940? YES
 *   2. result = alloc_ptr (Block2 + 156)
 *   3. alloc_ptr += 64 (now Block2 + 220)
 *   4. alloc_bytes_remaining -= 64 (now 3876)
 *   5. return result
 * 
 * ALIGNMENT MATH EXPLAINED:
 * =========================
 * 
 * Problem: We have a pointer `ptr` and want to advance it to the next
 *          8-byte aligned address.
 * 
 * Example: ptr = 0x1003 (binary: ...0001 0000 0011)
 * 
 * Step 1: Calculate current offset from alignment boundary
 *   current_mod = (uintptr_t)ptr & 7
 *               = 0x1003 & 0x7
 *               = 0x0003 (last 3 bits)
 *               = 3
 * 
 * Step 2: Calculate padding needed
 *   needed = (8 - current_mod) & 7
 *          = (8 - 3) & 7
 *          = 5 & 7
 *          = 5
 * 
 * Step 3: Add padding to pointer
 *   aligned_ptr = ptr + 5
 *               = 0x1003 + 5
 *               = 0x1008
 * 
 * Verification: 0x1008 & 7 = 0 ✓ (perfectly aligned)
 * 
 * Edge Case: If ptr is already aligned (e.g., 0x1008):
 *   current_mod = 0x1008 & 7 = 0
 *   needed = (8 - 0) & 7 = 8 & 7 = 0 (no padding needed)
 * 
 * Why the `& 7` in step 2?
 * Without it: (8 - 0) = 8, but we don't need to add 8 to an aligned pointer!
 * With it: (8 - 0) & 7 = 0, correctly indicates no padding needed.
 * 
 * Author: Aditya (@bit2swaz)
 * Version: 2.0.0
 */

#include "util/arena.h"
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <assert.h>

/* Constants */
static const size_t kBlockSize = 4096;             // Allocate blocks in 4KB chunks
static const size_t kAlignment = 8;                // 8-byte alignment for slices/nodes

/* Forward declarations */
static char* Arena_AllocateFallbackSlow(Lithos_Arena* arena, size_t bytes);

/**
 * Lithos_Arena - Internal structure definition.
 * 
 * This struct is opaque to users (they only see the typedef in arena.h).
 */
struct Lithos_Arena {
    /* Current allocation state */
    char* alloc_ptr;              // Pointer to the next free byte inside the current block.
    size_t alloc_bytes_remaining; // How many bytes remain in the current block.
    
    /* Block management */
    char** blocks;                // Growable list of all allocated blocks (for bulk free).
    size_t blocks_count;          // How many blocks are currently stored.
    size_t blocks_capacity;       // Current capacity of the blocks pointer array.
    
    /* Statistics */
    _Atomic size_t memory_usage;  // Total bytes handed out by malloc (atomic for stats).
};

/**
 * Helper: Allocate a new memory block from the system.
 * 
 * @param size: Size of the block to allocate.
 * 
 * Returns: Pointer to the allocated block, or NULL on failure.
 */
static char* AllocateNewBlock(size_t size) {
    char* block = (char*)malloc(size);
    return block;
}

/**
 * Helper: Add a block pointer to the blocks array.
 * 
 * This function handles dynamic resizing of the blocks array if needed.
 * 
 * @param arena: The arena to add the block to.
 * @param block: The block pointer to add.
 * 
 * Returns: 0 on success, -1 on allocation failure.
 */
static int AddBlock(Lithos_Arena* arena, char* block) {
    // Check if we need to resize the blocks array
    if (arena->blocks_count >= arena->blocks_capacity) {
        // Double the capacity (or initialize to 16 if starting from 0)
        size_t new_capacity = arena->blocks_capacity == 0 ? 16 : arena->blocks_capacity * 2;
        
        // Reallocate the blocks array
        char** new_blocks = (char**)realloc(arena->blocks, new_capacity * sizeof(char*));
        if (new_blocks == NULL) {
            // Realloc failed, but arena->blocks is still valid
            return -1;
        }
        
        arena->blocks = new_blocks;
        arena->blocks_capacity = new_capacity;
    }
    
    // Add the block to the array
    arena->blocks[arena->blocks_count] = block;
    arena->blocks_count++;
    
    return 0;
}

/**
 * Helper: Allocate a new block and make it the current allocation target.
 * 
 * @param arena: The arena.
 * @param block_bytes: Size of the block to allocate.
 * 
 * Returns: Pointer to the block, or NULL on failure.
 */
static char* AllocateFallback(Lithos_Arena* arena, size_t block_bytes) {
    char* block = AllocateNewBlock(block_bytes);
    if (block == NULL) {
        return NULL;
    }
    
    // Add to blocks list
    if (AddBlock(arena, block) != 0) {
        // Failed to add to list, free the block
        free(block);
        return NULL;
    }
    
    // Update memory usage atomically
    atomic_fetch_add_explicit(&arena->memory_usage, block_bytes, memory_order_relaxed);
    
    return block;
}

/* ========== Public API Implementation ========== */

Lithos_Arena* Arena_Create(void) {
    Lithos_Arena* arena = (Lithos_Arena*)malloc(sizeof(Lithos_Arena));
    if (arena == NULL) {
        return NULL;
    }
    
    // Initialize to empty state
    arena->alloc_ptr = NULL;              // No current block yet
    arena->alloc_bytes_remaining = 0;     // Nothing available until first block alloc
    arena->blocks = NULL;                 // No block array yet
    arena->blocks_count = 0;              // No blocks tracked yet
    arena->blocks_capacity = 0;           // Capacity will grow on first add
    atomic_init(&arena->memory_usage, 0);
    
    return arena;
}

void Arena_Destroy(Lithos_Arena* arena) {
    if (arena == NULL) {
        return;
    }
    
    // Free all allocated blocks
    for (size_t i = 0; i < arena->blocks_count; i++) {
        free(arena->blocks[i]);
    }
    
    // Free the blocks array itself
    free(arena->blocks);
    
    // Free the arena structure
    free(arena);
}

static inline char* AlignPtr(char* p) {
    uintptr_t addr = (uintptr_t)p;
    uintptr_t aligned = (addr + (kAlignment - 1)) & ~(uintptr_t)(kAlignment - 1);
    return (char*)aligned;
}

char* Arena_Allocate(Lithos_Arena* arena, size_t bytes) {
    // Validate input
    assert(arena != NULL);
    assert(bytes > 0);
    
    char* aligned = AlignPtr(arena->alloc_ptr);
    size_t padding = (size_t)(aligned - arena->alloc_ptr);
    
    if (bytes + padding <= arena->alloc_bytes_remaining) {
        // Fast path: allocation fits in current block with alignment padding
        char* result = aligned;
        arena->alloc_ptr = result + bytes;
        arena->alloc_bytes_remaining -= (bytes + padding);
        return result;
    }
    
    // Slow path: need to allocate a new block
    return Arena_AllocateFallbackSlow(arena, bytes);
}

/**
 * Arena_AllocateFallbackSlow - Handle allocation when current block is full.
 * 
 * Strategy:
 * 1. If bytes > 1024 (large allocation):
 *    - Allocate a dedicated block of exactly 'bytes'
 *    - Do NOT make it the new current block (would waste space)
 *    - Keep the current block as-is for future small allocations
 * 
 * 2. If bytes <= 1024 (normal allocation):
 *    - Allocate a new kBlockSize (4KB) block
 *    - Make it the current block
 *    - Satisfy the allocation from it
 * 
 * @param arena: The arena.
 * @param bytes: Number of bytes to allocate.
 * 
 * Returns: Pointer to allocated memory, or NULL on failure.
 */
char* Arena_AllocateFallbackSlow(Lithos_Arena* arena, size_t bytes) {
    size_t block_bytes;
    if (bytes > kBlockSize / 4) {
        /* Large allocation: dedicated block with alignment headroom. */
        block_bytes = bytes + kAlignment;
        char* block = AllocateFallback(arena, block_bytes);
        if (block == NULL) return NULL;
        char* result = AlignPtr(block);
        return result;
    }
    
    /* Normal case: allocate a new standard block with alignment headroom. */
    block_bytes = kBlockSize + kAlignment;
    char* new_block = AllocateFallback(arena, block_bytes);
    if (new_block == NULL) {
        return NULL;
    }
    
    char* aligned = AlignPtr(new_block);
    arena->alloc_ptr = aligned + bytes;  // Next free byte after the chunk we return
    size_t used = (size_t)(arena->alloc_ptr - new_block);
    arena->alloc_bytes_remaining = block_bytes - used;
    
    return aligned;
}

char* Arena_AllocateAligned(Lithos_Arena* arena, size_t bytes) {
    // Validate input
    assert(arena != NULL);
    assert(bytes > 0);
    
    // Calculate alignment padding needed
    // We want to align to 8 bytes (sizeof(void*) on 64-bit, sizeof(uint64_t))
    const size_t align = 8;
    
    // Calculate current misalignment
    // Example: if alloc_ptr = 0x1003, then current_mod = 3
    uintptr_t current_mod = (uintptr_t)arena->alloc_ptr & (align - 1); // Low bits reveal misalignment
    
    // Calculate padding needed to reach next alignment boundary
    // If already aligned (current_mod == 0), needed will be 0
    // If misaligned (e.g., current_mod == 3), needed will be (8 - 3) = 5
    size_t needed = (align - current_mod) & (align - 1); // If already aligned, this becomes 0
    
    // Calculate total size needed (padding + actual allocation)
    size_t total_bytes = needed + bytes; // Padding + user payload
    
    // Check if it fits in current block
    char* result;
    if (total_bytes <= arena->alloc_bytes_remaining) {
        // Fast path: fits in current block
        result = arena->alloc_ptr + needed;  // Skip padding to reach aligned address
        arena->alloc_ptr += total_bytes;     // Consume padding + payload in one bump
        arena->alloc_bytes_remaining -= total_bytes; // Update remaining space
    } else {
        // Slow path: need new block
        // First, try to allocate the full size (padding + bytes)
        result = Arena_AllocateFallbackSlow(arena, bytes); // May return unaligned pointer
        if (result == NULL) {
            return NULL;
        }
        
        // The new block might not be aligned, so we need to check again
        current_mod = (uintptr_t)result & (align - 1);
        if (current_mod != 0) {
            // Still not aligned (rare but possible)
            // Allocate again with padding
            needed = (align - current_mod) & (align - 1);
            result = Arena_Allocate(arena, bytes + needed) + needed; // Grab extra padding then skip it
        }
    }
    
    // Verify alignment (debug build)
    assert(((uintptr_t)result & (align - 1)) == 0);
    
    return result;
}

size_t Arena_MemoryUsage(Lithos_Arena* arena) {
    assert(arena != NULL);
    
    // Atomic load of memory_usage
    // memory_order_relaxed is sufficient because we don't need synchronization
    // with other operations, just an approximate value
    return atomic_load_explicit(&arena->memory_usage, memory_order_relaxed);
}
