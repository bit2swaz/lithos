/*
 * Filter Blocks: Bloom Filters for Fast Negative Lookups
 * ======================================================
 * Each SSTable has a filter block containing Bloom filters - one per 2KB of
 * data blocks. This enables fast "key not present" checks to skip I/O.
 *
 * Big Picture: Filter Blocks = "Bloom Filter Arrays for SSTable Skipping"
 * =======================================================================
 * SSTables are large files. To avoid reading data blocks that don't contain
 * a key, we maintain probabilistic filters. Each filter covers ~2KB of data
 * and can definitively say "key is NOT in this range" (with false positives).
 * This reduces disk seeks by 90%+ for missing keys.
 *
 * Where it fits: Filter blocks are stored in SSTable metaindex, loaded on
 * table open. Readers check filters before loading data blocks from disk.
 *
 * Key Concepts:
 * - Bloom filters: Probabilistic set membership with no false negatives.
 * - Block-based: One filter per 2KB data chunk for granularity.
 * - Offset array: Fixed32 offsets to locate each filter in the block.
 */

#include "filter_block.h"
#include "util/coding.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/*
 * Filter generation parameters
 */
#define FILTER_BASE_LG 11           /* log2(2048) = 11 */
#define FILTER_BASE (1 << FILTER_BASE_LG)  /* 2KB */

/*
 * FilterBlockBuilder state - accumulates keys and builds filters.
 */
struct FilterBlockBuilder {
    const Lithos_FilterPolicy* policy;  // Bloom filter policy (hash functions, bits)
    
    /* Accumulated keys for current filter */
    Lithos_Slice* keys;          // Array of key slices (point into key_storage)
    size_t num_keys;             // Keys in current filter batch
    size_t keys_capacity;        // Allocated size of keys array
    
    /* Start offsets of each filter in result_ */
    uint32_t* filter_offsets;    // Byte offsets where each filter starts
    size_t num_filters;          // Number of filters built so far
    size_t filter_offsets_capacity; // Allocated size of offsets array
    
    /* Filter data */
    char* result;                // Concatenated filter data
    size_t result_len;           // Current length of result
    size_t result_capacity;      // Allocated capacity of result
    
    /* Temp storage for keys (data is copied) */
    char* key_storage;           // Backing store for key data
    size_t key_storage_len;      // Used bytes in key_storage
    size_t key_storage_capacity; // Allocated capacity of key_storage
};

FilterBlockBuilder* FilterBlockBuilder_Create(const Lithos_FilterPolicy* policy) {
    FilterBlockBuilder* builder = calloc(1, sizeof(FilterBlockBuilder));
    if (!builder) {
        return NULL;
    }
    
    builder->policy = policy;
    builder->keys_capacity = 64;
    builder->keys = malloc(builder->keys_capacity * sizeof(Lithos_Slice));
    builder->filter_offsets_capacity = 64;
    builder->filter_offsets = malloc(builder->filter_offsets_capacity * sizeof(uint32_t));
    builder->result_capacity = 4096;
    builder->result = malloc(builder->result_capacity);
    builder->key_storage_capacity = 4096;
    builder->key_storage = malloc(builder->key_storage_capacity);
    
    if (!builder->keys || !builder->filter_offsets || 
        !builder->result || !builder->key_storage) {
        FilterBlockBuilder_Destroy(builder);
        return NULL;
    }
    
    return builder;
}

void FilterBlockBuilder_Destroy(FilterBlockBuilder* builder) {
    if (builder) {
        free(builder->keys);
        free(builder->filter_offsets);
        free(builder->result);
        free(builder->key_storage);
        free(builder);
    }
}

/* Build a filter for the keys collected since the last boundary. */
static void GenerateFilter(FilterBlockBuilder* builder) {
    if (builder->num_keys == 0) {
        /* No keys, generate empty filter */
        if (builder->num_filters >= builder->filter_offsets_capacity) {
            builder->filter_offsets_capacity *= 2;
            builder->filter_offsets = realloc(builder->filter_offsets,
                                              builder->filter_offsets_capacity * sizeof(uint32_t));
        }
        builder->filter_offsets[builder->num_filters++] = (uint32_t)builder->result_len;
        return;
    }
    
    /* Record filter offset */
    if (builder->num_filters >= builder->filter_offsets_capacity) {
        builder->filter_offsets_capacity *= 2;
        builder->filter_offsets = realloc(builder->filter_offsets,
                                          builder->filter_offsets_capacity * sizeof(uint32_t));
    }
    builder->filter_offsets[builder->num_filters++] = (uint32_t)builder->result_len;
    
    /* Create filter */
    FilterPolicy_CreateFilter(builder->policy, builder->keys, (int)builder->num_keys,
                              &builder->result, &builder->result_len, &builder->result_capacity);
    
    /* Reset keys */
    builder->num_keys = 0;
    builder->key_storage_len = 0;
}

void FilterBlockBuilder_StartBlock(FilterBlockBuilder* builder, uint64_t block_offset) {
    /* Advance filter index when the data block crosses the next 2KB bucket. */
    uint64_t filter_index = block_offset / FILTER_BASE;
    
    /* Generate filters for all filter indices up to this one */
    while (filter_index > builder->num_filters) {
        GenerateFilter(builder);
    }
}

void FilterBlockBuilder_AddKey(FilterBlockBuilder* builder, Lithos_Slice key) {
    /* Ensure keys array has space */
    if (builder->num_keys >= builder->keys_capacity) {
        builder->keys_capacity *= 2;
        builder->keys = realloc(builder->keys, builder->keys_capacity * sizeof(Lithos_Slice));
    }
    
    /* Copy key bytes into contiguous storage to keep slices stable. */
    size_t needed = builder->key_storage_len + key.size;
    if (needed > builder->key_storage_capacity) {
        builder->key_storage_capacity = needed * 2;
        builder->key_storage = realloc(builder->key_storage, builder->key_storage_capacity);
    }
    
    memcpy(builder->key_storage + builder->key_storage_len, key.data, key.size);
    builder->keys[builder->num_keys].data = builder->key_storage + builder->key_storage_len;
    builder->keys[builder->num_keys].size = key.size;
    builder->key_storage_len += key.size;
    builder->num_keys++;
}

Lithos_Slice FilterBlockBuilder_Finish(FilterBlockBuilder* builder) {
    /* Flush any remaining keys into a final filter. */
    if (builder->num_keys > 0) {
        GenerateFilter(builder);
    }
    
    /* Append offsets array: one uint32 per filter. */
    size_t array_offset = builder->result_len;
    for (size_t i = 0; i < builder->num_filters; i++) {
        /* Ensure space */
        if (builder->result_len + 4 > builder->result_capacity) {
            builder->result_capacity = (builder->result_len + 4) * 2;
            builder->result = realloc(builder->result, builder->result_capacity);
        }
        
        EncodeFixed32(builder->result + builder->result_len, builder->filter_offsets[i]);
        builder->result_len += 4;
    }
    
    /* Append starting offset of the offsets array. */
    if (builder->result_len + 5 > builder->result_capacity) {
        builder->result_capacity = (builder->result_len + 5) * 2;
        builder->result = realloc(builder->result, builder->result_capacity);
    }
    
    EncodeFixed32(builder->result + builder->result_len, (uint32_t)array_offset);
    builder->result_len += 4;
    
    /* Append base_lg (log2 of FILTER_BASE). */
    builder->result[builder->result_len++] = FILTER_BASE_LG;
    
    Lithos_Slice result = { builder->result, builder->result_len };
    return result;
}

/* Reader: interprets the offset table and delegates matching to policy. */
struct FilterBlockReader {
    const Lithos_FilterPolicy* policy;
    const char* data;          /* Filter block contents */
    size_t size;
    size_t offset_base;        /* Offset of filter offset array */
    size_t num_filters;
    uint32_t base_lg;          /* log2(FILTER_BASE) */
};

FilterBlockReader* FilterBlockReader_Create(const Lithos_FilterPolicy* policy,
                                              Lithos_Slice contents) {
    size_t n = contents.size;
    if (n < 5) {
        return NULL;  /* Too short */
    }
    
    FilterBlockReader* reader = malloc(sizeof(FilterBlockReader));
    if (!reader) {
        return NULL;
    }
    
    reader->policy = policy;
    reader->data = contents.data;
    reader->size = n;
    reader->base_lg = (uint32_t)contents.data[n - 1];
    reader->offset_base = DecodeFixed32(contents.data + n - 5);
    
    if (reader->offset_base > n - 5) {
        free(reader);
        return NULL;  /* Malformed */
    }
    
    reader->num_filters = (n - 5 - reader->offset_base) / 4;
    
    return reader;
}

void FilterBlockReader_Destroy(FilterBlockReader* reader) {
    free(reader);
}

bool FilterBlockReader_KeyMayMatch(FilterBlockReader* reader,
                                    uint64_t block_offset,
                                    Lithos_Slice key) {
    /* Map block_offset → filter index → slice, then ask the policy. */
    uint64_t index = block_offset >> reader->base_lg;
    if (index < reader->num_filters) {
        /* Get filter offset */
        uint32_t start = DecodeFixed32(reader->data + reader->offset_base + index * 4);
        uint32_t limit;
        if (index + 1 < reader->num_filters) {
            limit = DecodeFixed32(reader->data + reader->offset_base + (index + 1) * 4);
        } else {
            limit = (uint32_t)reader->offset_base;
        }
        
        if (start <= limit && limit <= reader->offset_base) {
            Lithos_Slice filter = { reader->data + start, limit - start };
            return FilterPolicy_KeyMayMatch(reader->policy, key, filter);
        } else if (start == limit) {
            /* Empty filter */
            return false;
        }
    }
    
    return true;  /* Errors are treated as "key may match" */
}
