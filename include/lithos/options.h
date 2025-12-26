/*
 * Database Options: Configuration for Performance Tuning
 * =====================================================
 * Defines the configuration parameters that control Lithos behavior,
 * allowing users to tune performance trade-offs for their workload.
 *
 * Big Picture: Options = "Tuning Knobs for Database Behavior"
 * =========================================================
 * Databases have many performance trade-offs: memory vs speed, compression
 * vs CPU, etc. Options allow users to customize these for their specific
 * use case. Sensible defaults work for most applications.
 *
 * Where it fits: Options are passed to DB open/create and affect all
 * components (block size, compression, caching, filtering, etc.).
 *
 * Key Concepts:
 * - Block restart interval: How often to reset prefix compression (16 keys).
 * - Block size: SSTable block size (4KB - matches OS page size).
 * - Comparator: Custom key ordering (defaults to bytewise).
 * - Filter policy: Bloom filters for fast negative lookups.
 * - Block cache: LRU cache for hot SSTable blocks.
 */

#ifndef LITHOS_OPTIONS_H
#define LITHOS_OPTIONS_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declare Comparator (defined in dbformat.h) */
typedef struct Comparator Comparator;

/* Forward declare FilterPolicy (defined in filter_policy.h) */
typedef struct Lithos_FilterPolicy Lithos_FilterPolicy;

/* Forward declare Cache (defined in cache.h) */
typedef struct Lithos_Cache Lithos_Cache;

/**
 * Database Options
 * ================
 * 
 * Configuration struct passed to various subsystems.
 * 
 * Fields:
 * -------
 * - block_restart_interval: Number of keys between restart points.
 *   Lower = faster binary search, higher = better compression.
 *   Default: 16 keys.
 * 
 * - block_size: Target size (bytes) for each data block before flushing.
 *   Larger = better compression, smaller = less memory overhead.
 *   Default: 4096 bytes (4KB).
 * 
 * - comparator: Key comparison function. If NULL, uses default bytewise.
 * 
 * - filter_policy: Probabilistic filter for read optimization. If NULL, no filters.
 *   Typical: NewBloomFilterPolicy(10) for ~1% false positive rate.
 * 
 * - block_cache: Shared block cache for uncompressed data blocks. If NULL, no caching.
 *   Typical: NewLRUCache(8 * 1024 * 1024) for 8MB cache.
 */
typedef struct Lithos_Options {
    size_t block_restart_interval;
    size_t block_size;
    const Comparator* comparator;
    const Lithos_FilterPolicy* filter_policy;
    Lithos_Cache* block_cache;
    bool compression_enabled;
} Lithos_Options;

/**
 * Initialize options with sensible defaults.
 * 
 * @param opt Output: Options struct to initialize.
 * 
 * Defaults:
 * ---------
 * - block_cache = NULL (No caching)
 * - block_restart_interval = 16 (Every 16 keys, reset prefix compression)
 * - block_size = 4096 (4KB blocks)
 * - comparator = NULL (Will use bytewise default)
 * - filter_policy = NULL (No filtering)
 */
void Lithos_Options_InitDefault(Lithos_Options* opt);

#ifdef __cplusplus
}
#endif

#endif /* LITHOS_OPTIONS_H */
