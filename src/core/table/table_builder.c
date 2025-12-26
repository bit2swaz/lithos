/*
 * SSTable Builder: Multi-Block File Assembly with Compression & Filters
 * ===================================================================
 * Orchestrates the construction of complete SSTable files from sorted KV streams.
 * Manages data blocks, filters, index, and footer in the correct sequence.
 *
 * Big Picture: Table Builder = "SSTable Assembly Line"
 * ===================================================
 * Takes a sorted stream of key-value pairs and builds a complete SSTable file
 * with all its components: compressed data blocks, Bloom filters, index block,
 * metaindex, and navigation footer. This is the "write path" counterpart to
 * the Table reader.
 *
 * Where it fits: Table builders are used during compaction and memtable flushes
 * to create new SSTable files. They ensure proper block boundaries, compression,
 * and metadata for efficient future reads.
 *
 * Key Concepts:
 * - Block flushing: When data blocks fill up, flush them and add to index.
 * - Filter building: Accumulate keys for Bloom filters every 2KB of data.
 * - Index construction: Map key ranges to data block locations.
 * - Footer writing: Fixed 48-byte trailer with index/metaindex pointers.
 */

#include "core/table/table_builder.h"
#include "core/table/block_builder.h"
#include "core/table/filter_block.h"
#include "core/table/format.h"
#include "util/coding.h"
#include "util/crc32c.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/*
 * Block trailer format (appended after block data):
 * - Type (1 byte): Compression type (0 = no compression)
 * - CRC32C (4 bytes): Checksum of (Type + Data)
 */
#define BLOCK_TRAILER_SIZE 5

struct Lithos_TableBuilder {
    Lithos_Options options;            // Configuration
    Lithos_WritableFile* file;         // Output file
    uint64_t offset;                   // Current write position in file
    lithos_status_code status;              // First error encountered
    Lithos_BlockBuilder* data_block;   // Current data block being built
    Lithos_BlockBuilder* index_block;  // Index block (maps keys -> block offsets)
    FilterBlockBuilder* filter_block;  // Filter block (Bloom filters)
    char* last_key;                    // Last key added (for index generation)
    size_t last_key_size;
    size_t last_key_capacity;
    uint64_t num_entries;              // Total key-value pairs added
    bool closed;                       // True after Finish() or Abandon()
    
    /* Pending index entry state */
    bool pending_index_entry;          // True if we need to add index entry
    Lithos_BlockHandle pending_handle; // Handle of the data block just flushed
};

/*
 * Helper: Write a block to file with compression and checksum.
 * 
 * Format written to file:
 *   [Block Data] [Type:1] [CRC32:4]
 * 
 * Parameters:
 *   tb     - Table builder
 *   block  - Finished block builder
 *   handle - Output: receives offset and size of the block data (excludes trailer)
 * 
 * Returns: LITHOS_OK on success
 */
static lithos_status_code WriteBlock(Lithos_TableBuilder* tb,
                                 Lithos_BlockBuilder* block,
                                 Lithos_BlockHandle* handle) {
    assert(!tb->closed);
    
    /* Get the finished block content */
    Lithos_Slice block_contents = BlockBuilder_Finish(block);
    
    /* For now, no compression (Type = 0) */
    /* Future: Add Snappy/Zstd compression here */
    const char* raw_data = block_contents.data;
    size_t raw_size = block_contents.size;
    uint8_t type = 0;  // kNoCompression
    
    /* Set handle to point to the block data (NOT including trailer) */
    handle->offset = tb->offset;
    handle->size = raw_size;
    
    /* Write block data */
    Status s = WritableFile_Append(tb->file, block_contents);
    if (s.code != LITHOS_OK) {
        return s.code;
    }
    
    /* Prepare trailer: [Type:1] [CRC32:4] */
    char trailer[BLOCK_TRAILER_SIZE];
    trailer[0] = (char)type;
    
    /* Calculate CRC32C of (Type + Data) */
    uint32_t crc = crc32c_extend(0, trailer, 1);  // Type byte
    crc = crc32c_extend(crc, raw_data, raw_size);  // Data
    EncodeFixed32(trailer + 1, crc);
    
    /* Write trailer */
    Lithos_Slice trailer_slice = {trailer, BLOCK_TRAILER_SIZE};
    s = WritableFile_Append(tb->file, trailer_slice);
    if (s.code != LITHOS_OK) {
        return s.code;
    }
    
    /* Update file offset */
    tb->offset += raw_size + BLOCK_TRAILER_SIZE;
    
    return LITHOS_OK;
}

/*
 * Helper: Flush the current data block to file.
 * 
 * This does NOT immediately write an index entry. Instead, it sets
 * pending_index_entry = true. The index entry is written when we know
 * the first key of the NEXT data block (or at Finish time).
 * 
 * Why wait? We want the index key to be a separator between blocks.
 * The optimal separator is one that is >= last key of block N and
 * < first key of block N+1.
 */
static lithos_status_code FlushDataBlock(Lithos_TableBuilder* tb) {
    assert(!tb->closed);
    assert(!BlockBuilder_Empty(tb->data_block));
    
    if (tb->status != LITHOS_OK) {
        return tb->status;
    }
    
    /* Notify filter builder about the starting file offset of this block. */
    if (tb->filter_block != NULL) {
        FilterBlockBuilder_StartBlock(tb->filter_block, tb->offset);
    }
    
    /* Write the data block to the file and record its handle. */
    lithos_status_code s = WriteBlock(tb, tb->data_block, &tb->pending_handle);
    if (s != LITHOS_OK) {
        tb->status = s;
        return s;
    }
    
    /* Defer writing the index entry until we know the next block's first key. */
    tb->pending_index_entry = true;
    
    /* Flush to OS buffers; fsync policy is controlled by WritableFile impl. */
    Status sync_status = WritableFile_Flush(tb->file);
    if (sync_status.code != LITHOS_OK) {
        tb->status = sync_status.code;
        return sync_status.code;
    }
    
    /* Reset data block for next batch of keys */
    BlockBuilder_Reset(tb->data_block);
    
    return LITHOS_OK;
}

/*
 * Helper: Add the pending index entry to the index block.
 * 
 * The index entry maps: last_key -> BlockHandle of the data block
 * The BlockHandle is encoded as Varint64(offset) + Varint64(size).
 */
static void AddIndexEntry(Lithos_TableBuilder* tb) {
    if (!tb->pending_index_entry) {
        return;
    }
    
    assert(tb->last_key != NULL);
    assert(tb->last_key_size > 0);
    
    /* Encode the BlockHandle (offset + size as varints). */
    char handle_encoding[20];  // Max 20 bytes for 2 Varint64s
    size_t handle_len = BlockHandle_Encode(&tb->pending_handle, handle_encoding);
    
    Lithos_Slice key = {tb->last_key, tb->last_key_size};
    Lithos_Slice value = {handle_encoding, handle_len};
    
    BlockBuilder_Add(tb->index_block, key, value);
    tb->pending_index_entry = false;
}

Lithos_TableBuilder* TableBuilder_Create(const Lithos_Options* options,
                                         Lithos_WritableFile* file) {
    Lithos_TableBuilder* tb = malloc(sizeof(Lithos_TableBuilder));
    if (!tb) {
        return NULL;
    }
    
    tb->options = *options;
    tb->file = file;
    tb->offset = 0;
    tb->status = LITHOS_OK;
    tb->data_block = BlockBuilder_Create(&tb->options);
    tb->index_block = BlockBuilder_Create(&tb->options);
    tb->filter_block = NULL;
    if (options->filter_policy != NULL) {
        tb->filter_block = FilterBlockBuilder_Create(options->filter_policy);
    }
    tb->last_key = NULL;
    tb->last_key_size = 0;
    tb->last_key_capacity = 0;
    tb->num_entries = 0;
    tb->closed = false;
    tb->pending_index_entry = false;
    BlockHandle_Init(&tb->pending_handle);
    
    if (!tb->data_block || !tb->index_block) {
        if (tb->data_block) BlockBuilder_Destroy(tb->data_block);
        if (tb->index_block) BlockBuilder_Destroy(tb->index_block);
        if (tb->filter_block) FilterBlockBuilder_Destroy(tb->filter_block);
        free(tb);
        return NULL;
    }
    
    return tb;
}

void TableBuilder_Destroy(Lithos_TableBuilder* tb) {
    if (tb->data_block) {
        BlockBuilder_Destroy(tb->data_block);
    }
    if (tb->index_block) {
        BlockBuilder_Destroy(tb->index_block);
    }
    if (tb->filter_block) {
        FilterBlockBuilder_Destroy(tb->filter_block);
    }
    free(tb->last_key);
    free(tb);
}

lithos_status_code TableBuilder_Add(Lithos_TableBuilder* tb,
                                Lithos_Slice key,
                                Lithos_Slice value) {
    if (tb->closed) {
        return LITHOS_INVALID_ARGUMENT;
    }
    
    if (tb->status != LITHOS_OK) {
        return tb->status;
    }
    
    if (tb->num_entries > 0) {
        /* Verify keys are added in sorted order */
        Lithos_Slice last = {tb->last_key, tb->last_key_size};
        assert(Slice_Compare(last, key) < 0);
    }
    
    /* If we have a pending index entry from the previous data block,
     * add it now that we know the first key of the next block. */
    if (tb->pending_index_entry) {
        AddIndexEntry(tb);
    }
    
    /* Save the key for potential index entry */
    if (key.size > tb->last_key_capacity) {
        tb->last_key_capacity = key.size * 2;
        tb->last_key = realloc(tb->last_key, tb->last_key_capacity);
        if (!tb->last_key) {
            tb->status = LITHOS_IO_ERROR;
            return tb->status;
        }
    }
    memcpy(tb->last_key, key.data, key.size);
    tb->last_key_size = key.size;
    
    /* Append to current data block builder. */
    tb->num_entries++;
    BlockBuilder_Add(tb->data_block, key, value);
    
    /* Add key to filter block before we potentially flush the block. */
    if (tb->filter_block != NULL) {
        FilterBlockBuilder_AddKey(tb->filter_block, key);
    }
    
    /* If block hits target size, flush and begin a new one. */
    size_t estimated_size = BlockBuilder_CurrentSizeEstimate(tb->data_block);
    if (estimated_size >= tb->options.block_size) {
        lithos_status_code s = FlushDataBlock(tb);
        if (s != LITHOS_OK) {
            tb->status = s;
            return s;
        }
    }
    
    return LITHOS_OK;
}

lithos_status_code TableBuilder_Finish(Lithos_TableBuilder* tb) {
    if (tb->closed) {
        return tb->status;
    }
    
    if (tb->status != LITHOS_OK) {
        tb->closed = true;
        return tb->status;
    }
    
    /* Flush any buffered KV pairs into a final data block. */
    if (!BlockBuilder_Empty(tb->data_block)) {
        lithos_status_code s = FlushDataBlock(tb);
        if (s != LITHOS_OK) {
            tb->status = s;
            tb->closed = true;
            return s;
        }
    }
    
    /* Add the final index entry now that no more data blocks follow. */
    if (tb->pending_index_entry) {
        AddIndexEntry(tb);
    }
    
    /* Emit filter block (Bloom filters) if configured. */
    Lithos_BlockHandle filter_handle;
    BlockHandle_Init(&filter_handle);
    
    if (tb->filter_block != NULL) {
        Lithos_Slice filter_contents = FilterBlockBuilder_Finish(tb->filter_block);
        
        /* Write filter block to file */
        Status filter_status = WritableFile_Append(tb->file, filter_contents);
        if (filter_status.code != LITHOS_OK) {
            tb->status = filter_status.code;
            tb->closed = true;
            return filter_status.code;
        }
        
        /* Calculate CRC and write trailer */
        char trailer[BLOCK_TRAILER_SIZE];
        trailer[0] = 0;  // No compression
        uint32_t crc = crc32c_extend(0, trailer, 1);
        crc = crc32c_extend(crc, filter_contents.data, filter_contents.size);
        EncodeFixed32(trailer + 1, crc);
        
        Lithos_Slice trailer_slice = {trailer, BLOCK_TRAILER_SIZE};
        filter_status = WritableFile_Append(tb->file, trailer_slice);
        if (filter_status.code != LITHOS_OK) {
            tb->status = filter_status.code;
            tb->closed = true;
            return filter_status.code;
        }
        
        filter_handle.offset = tb->offset;
        filter_handle.size = filter_contents.size;
        tb->offset += filter_contents.size + BLOCK_TRAILER_SIZE;
    }
    
    /* Metaindex block anchors optional metadata (currently only filters). */
    Lithos_BlockHandle metaindex_handle;
    Lithos_BlockBuilder* metaindex_block = BlockBuilder_Create(&tb->options);
    if (!metaindex_block) {
        tb->status = LITHOS_IO_ERROR;
        tb->closed = true;
        return tb->status;
    }
    
    /* Add filter block handle so readers can find it by name. */
    if (tb->filter_block != NULL) {
        const char* filter_name = "filter.lithos.builtin";
        Lithos_Slice filter_key = {filter_name, strlen(filter_name)};
        
        char handle_encoding[20];
        size_t handle_len = BlockHandle_Encode(&filter_handle, handle_encoding);
        Lithos_Slice handle_value = {handle_encoding, handle_len};
        
        BlockBuilder_Add(metaindex_block, filter_key, handle_value);
    }
    
    BlockBuilder_Finish(metaindex_block);
    lithos_status_code s = WriteBlock(tb, metaindex_block, &metaindex_handle);
    BlockBuilder_Destroy(metaindex_block);
    
    if (s != LITHOS_OK) {
        tb->status = s;
        tb->closed = true;
        return s;
    }
    
    /* Index block maps last keys → BlockHandle for every data block. */
    Lithos_BlockHandle index_handle;
    s = WriteBlock(tb, tb->index_block, &index_handle);
    if (s != LITHOS_OK) {
        tb->status = s;
        tb->closed = true;
        return s;
    }
    
    /* Footer points to metaindex + index; fixed-size for easy seek-from-end. */
    Lithos_Footer footer;
    Footer_Init(&footer);
    footer.metaindex_handle = metaindex_handle;
    footer.index_handle = index_handle;
    
    char footer_buf[LITHOS_FOOTER_ENCODED_LENGTH];
    Footer_Encode(&footer, footer_buf);
    
    Lithos_Slice footer_slice = {footer_buf, LITHOS_FOOTER_ENCODED_LENGTH};
    Status footer_status = WritableFile_Append(tb->file, footer_slice);
    if (footer_status.code != LITHOS_OK) {
        tb->status = footer_status.code;
        tb->closed = true;
        return footer_status.code;
    }
    
    tb->offset += LITHOS_FOOTER_ENCODED_LENGTH;
    
    /* Sync to disk */
    Status sync_status = WritableFile_Sync(tb->file);
    if (sync_status.code != LITHOS_OK) {
        tb->status = sync_status.code;
    }
    
    tb->closed = true;
    return tb->status;
}

void TableBuilder_Abandon(Lithos_TableBuilder* tb) {
    tb->closed = true;
}

uint64_t TableBuilder_FileSize(const Lithos_TableBuilder* tb) {
    return tb->offset;
}

lithos_status_code TableBuilder_Status(const Lithos_TableBuilder* tb) {
    return tb->status;
}

uint64_t TableBuilder_NumEntries(const Lithos_TableBuilder* tb) {
    return tb->num_entries;
}
