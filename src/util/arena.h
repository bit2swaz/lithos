/**
 * Lithos Storage Engine - Arena Memory Allocator
 *
 * The Arena is a region-based memory allocator that eliminates the overhead
 * of individual malloc/free calls for millions of small objects.
 *
 * Problem Statement:
 * ------------------
 * In an LSM-tree, the MemTable (implemented as a SkipList) contains millions
 * of nodes. Each node is ~40-80 bytes. Calling malloc() for each node causes:
 * 1. Per-allocation overhead (~16 bytes of metadata per malloc on Linux)
 * 2. System call overhead (malloc may call brk/mmap)
 * 3. Memory fragmentation (small holes between allocations)
 *
 * Solution: The Arena
 * -------------------
 * - Allocate memory in large "blocks" (e.g., 4KB at a time)
 * - Distribute pointers from within these blocks using bump-pointer allocation
 * - When a block is full, allocate a new one
 * - Deallocate ALL memory at once when the Arena is destroyed (no individual
 * frees)
 *
 * Performance Benefits:
 * ---------------------
 * - ~10x fewer system calls (1 malloc per 4KB vs 1 per 64 bytes)
 * - Zero per-object metadata overhead
 * - Excellent cache locality (objects are contiguous)
 * - O(1) allocation (just increment a pointer)
 *
 * Lifetime:
 * ---------
 * The Arena lives as long as the MemTable. When the MemTable is flushed to
 * disk (converted to an SSTable), the entire Arena is destroyed at once.
 *
 * Concurrency:
 * ------------
 * The Arena itself is NOT thread-safe. Callers must serialize allocations
 * (e.g., via a mutex in the MemTable). This is acceptable because writes
 * to the MemTable are already serialized.
 *
 * Author: Aditya (@bit2swaz)
 * Version: 2.0.0
 */

#ifndef LITHOS_UTIL_ARENA_H
#define LITHOS_UTIL_ARENA_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Lithos_Arena - Opaque handle to an arena allocator.
 *
 * The internal structure is hidden to allow implementation changes without
 * breaking API compatibility.
 */
typedef struct Lithos_Arena Lithos_Arena;

/**
 * Arena_Create - Create a new arena allocator.
 *
 * Returns: A pointer to a newly created Arena, or NULL on allocation failure.
 *
 * Initial State: The arena starts with no allocated blocks. The first
 *                allocation will trigger the first block allocation.
 *
 * Usage Pattern:
 *   Lithos_Arena* arena = Arena_Create();
 *   // ... perform allocations ...
 *   Arena_Destroy(arena);
 */
Lithos_Arena *Arena_Create(void);

/**
 * Arena_Destroy - Destroy an arena and free all its memory.
 *
 * @param arena: The arena to destroy (can be NULL for convenience).
 *
 * Behavior:
 * - Frees all memory blocks allocated by the arena
 * - Frees the arena structure itself
 * - Does NOT call destructors or free individual objects
 *
 * After this call, ALL pointers obtained from Arena_Allocate are invalid.
 * Accessing them is undefined behavior (use-after-free).
 *
 * Thread Safety: The caller must ensure no other threads are using the arena.
 */
void Arena_Destroy(Lithos_Arena *arena);

/**
 * Arena_Allocate - Allocate memory from the arena.
 *
 * @param arena: The arena to allocate from (must NOT be NULL).
 * @param bytes: Number of bytes to allocate (must be > 0).
 *
 * Returns: A pointer to the allocated memory, or NULL on allocation failure.
 *
 * Alignment: The returned pointer has NO guaranteed alignment. It may be
 *            aligned to 1 byte. Use Arena_AllocateAligned for structs.
 *
 * Lifetime: The memory is valid until Arena_Destroy is called.
 *
 * Implementation: Uses bump-pointer allocation from the current block.
 *                 If the block is full, allocates a new one.
 *
 * Example:
 *   char* buffer = Arena_Allocate(arena, 100);
 *   if (buffer) {
 *       memcpy(buffer, data, 100);
 *   }
 */
char *Arena_Allocate(Lithos_Arena *arena, size_t bytes);

/**
 * Arena_AllocateAligned - Allocate memory with 8-byte alignment.
 *
 * @param arena: The arena to allocate from (must NOT be NULL).
 * @param bytes: Number of bytes to allocate (must be > 0).
 *
 * Returns: A pointer aligned to an 8-byte boundary, or NULL on failure.
 *
 * Alignment Guarantee: ((uintptr_t)returned_ptr % 8) == 0
 *
 * Why 8 bytes?
 * - On 64-bit systems, pointers and uint64_t are 8 bytes
 * - Unaligned access to these types causes:
 *   * Performance penalty (extra memory cycles) on x86
 *   * Crash (SIGBUS) on ARM/SPARC
 * - Aligning to 8 bytes prevents both issues
 *
 * Use Cases:
 * - Allocating structs containing pointers or uint64_t
 * - SkipList nodes (contain multiple pointers)
 *
 * Example:
 *   struct Node {
 *       uint64_t key;
 *       struct Node* next;
 *   };
 *   struct Node* node = (struct Node*)Arena_AllocateAligned(arena,
 * sizeof(*node));
 */
char *Arena_AllocateAligned(Lithos_Arena *arena, size_t bytes);

/**
 * Arena_MemoryUsage - Get total memory allocated by the arena.
 *
 * @param arena: The arena to query (must NOT be NULL).
 *
 * Returns: Total bytes allocated from the system (not bytes requested by user).
 *
 * This includes:
 * - All memory blocks allocated from malloc
 * - Internal arena bookkeeping structures
 *
 * Use Case: Tracking MemTable size to decide when to flush to disk.
 *
 * Thread Safety: Uses atomic load, safe to call concurrently.
 *
 * Example:
 *   if (Arena_MemoryUsage(arena) > 4*1024*1024) {
 *       // MemTable exceeded 4MB, trigger flush
 *   }
 */
size_t Arena_MemoryUsage(Lithos_Arena *arena);

#ifdef __cplusplus
}
#endif

#endif // LITHOS_UTIL_ARENA_H
