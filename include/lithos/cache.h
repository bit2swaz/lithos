/*
 * Block Cache Interface: LRU Cache for SSTable Data Blocks
 * =======================================================
 * Provides a sharded LRU cache for keeping frequently accessed SSTable
 * blocks in memory, reducing disk I/O.
 *
 * Big Picture: Block Cache = "Memory as Fast Disk Extension"
 * =========================================================
 * SSTables are large files on disk. Reading a 4KB block takes milliseconds.
 * The block cache keeps recently used blocks in RAM for instant access.
 * This is crucial for performance: hot data stays in memory, cold data
 * gets evicted to make room.
 *
 * Where it fits: SSTable readers check the cache before reading from disk.
 * The cache is shared across all open SSTables in the database.
 *
 * Key Concepts:
 * - Sharding: 16-way sharding reduces lock contention for concurrent access.
 * - LRU eviction: Least Recently Used blocks are evicted when cache is full.
 * - Reference counting: Handles can be held by multiple threads safely.
 * - Handle table: Hash table maps keys to cache entries.
 */

#ifndef LITHOS_CACHE_H
#define LITHOS_CACHE_H

#include "util/slice.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque types */
typedef struct Lithos_Cache Lithos_Cache;
typedef struct Lithos_CacheHandle Lithos_CacheHandle;

/*
 * Deleter function called when a cache entry is evicted or explicitly erased.
 * The caller should free the value and any associated resources.
 */
typedef void (*CacheDeleter)(const Lithos_Slice* key, void* value);

/*
 * Create a new LRU cache with the specified capacity (in bytes).
 * The cache is sharded internally to reduce lock contention.
 */
Lithos_Cache* NewLRUCache(size_t capacity);

/*
 * Destroy the cache and release all entries.
 * All handles must be released before calling this function.
 */
void Cache_Destroy(Lithos_Cache* cache);

/*
 * Insert a key-value pair into the cache.
 * 
 * Parameters:
 *   - cache: The cache instance
 *   - key: The cache key (will be copied internally)
 *   - value: Pointer to the cached value
 *   - charge: The memory usage of this entry (for capacity accounting)
 *   - deleter: Function to call when the entry is evicted
 * 
 * Returns:
 *   A handle to the inserted entry with refcount = 1.
 *   The caller must call Cache_Release() when done.
 * 
 * If an entry with the same key already exists, it is replaced.
 */
Lithos_CacheHandle* Cache_Insert(
    Lithos_Cache* cache,
    Lithos_Slice key,
    void* value,
    size_t charge,
    CacheDeleter deleter
);

/*
 * Lookup a key in the cache.
 * 
 * Returns:
 *   A handle to the entry with refcount incremented, or NULL if not found.
 *   The caller must call Cache_Release() when done.
 */
Lithos_CacheHandle* Cache_Lookup(Lithos_Cache* cache, Lithos_Slice key);

/*
 * Release a cache handle, decrementing its reference count.
 * When the refcount reaches 0, the entry may be evicted and the deleter called.
 */
void Cache_Release(Lithos_Cache* cache, Lithos_CacheHandle* handle);

/*
 * Get the value associated with a cache handle.
 * The handle must be valid (not yet released).
 */
void* Cache_Value(Lithos_CacheHandle* handle);

/*
 * Remove an entry from the cache.
 * If the entry is currently in use (refcount > 0), it will be marked
 * for deletion and removed when the last reference is released.
 */
void Cache_Erase(Lithos_Cache* cache, Lithos_Slice key);

/*
 * Generate a unique 64-bit ID for constructing cache keys.
 * Typically used to create per-file cache key prefixes.
 * 
 * Example:
 *   uint64_t cache_id = Cache_NewId(cache);
 *   // Then use: cache_id + block_offset as the cache key
 */
uint64_t Cache_NewId(Lithos_Cache* cache);

/*
 * Get the total charge of all entries in the cache.
 * Useful for debugging and monitoring.
 */
size_t Cache_TotalCharge(Lithos_Cache* cache);

#ifdef __cplusplus
}
#endif

#endif /* LITHOS_CACHE_H */
