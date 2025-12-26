/*
 * Database Options: Configuration Knobs for Tuning Lithos
 * =======================================================
 * Provides default configuration values and initialization for all Lithos
 * tuning parameters.
 *
 * Big Picture: Configuration = "Tuning Knobs for Performance Trade-offs"
 * =====================================================================
 * Databases have many performance trade-offs: memory vs speed, compression
 * vs CPU, etc. Options allow users to tune these for their workload.
 * Sensible defaults work for most cases, but experts can optimize further.
 *
 * Where it fits: Options are passed to DB open/create and affect all
 * components (block size, compression, caching, etc.).
 *
 * Key Concepts:
 * - Block restart interval: How often to reset prefix compression (16 keys).
 * - Block size: SSTable block size (4KB - matches OS page size).
 * - Filter policy: Bloom filter configuration for fast negative lookups.
 * - Block cache: LRU cache size for hot SSTable blocks.
 */

#include "lithos/options.h"
#include <stddef.h>

void Lithos_Options_InitDefault(Lithos_Options* opt) {
    opt->block_restart_interval = 16;   // Reset prefix compression every 16 keys
    opt->block_size = 4096;              // 4KB blocks (standard page size)
    opt->comparator = NULL;              // Will use default bytewise comparator
    opt->filter_policy = NULL;           // No filtering by default
    opt->block_cache = NULL;             // No caching by default
    opt->compression_enabled = false;    // Compression off by default
}
