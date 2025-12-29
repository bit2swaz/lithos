
#include "core/table/table_builder.h"
#include "core/table/block_builder.h"
#include "core/table/filter_block.h"
#include "core/table/format.h"
#include "core/dbformat.h"
#include "util/coding.h"
#include "util/compression.h"
#include "util/crc32c.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define BLOCK_TRAILER_SIZE 5

struct Lithos_TableBuilder {
  Lithos_Options options;
  Lithos_WritableFile *file;
  uint64_t offset;
  lithos_status_code status;
  Lithos_BlockBuilder *data_block;
  Lithos_BlockBuilder *index_block;
  FilterBlockBuilder *filter_block;
  char *last_key;
  size_t last_key_size;
  size_t last_key_capacity;
  uint64_t num_entries;
  bool closed;

  bool pending_index_entry;
  Lithos_BlockHandle pending_handle;
};

static lithos_status_code WriteBlock(Lithos_TableBuilder *tb,
                                     Lithos_BlockBuilder *block,
                                     Lithos_BlockHandle *handle) {
  assert(!tb->closed);

  Lithos_Slice block_contents = BlockBuilder_Finish(block);
  const char *raw_data = block_contents.data;
  size_t raw_size = block_contents.size;
  uint8_t type = LITHOS_COMPRESSION_NONE;

  const char *data_to_write = raw_data;
  size_t data_size = raw_size;
  char *compressed_buf = NULL;
  if (tb->options.compression_enabled && raw_size > 0) {

    size_t cap = raw_size * 3 + 4;
    compressed_buf = (char *)malloc(cap);
    if (compressed_buf != NULL) {
      size_t comp_len = cap - 4;
      bool ok =
          Lithos_Compress(raw_data, raw_size, compressed_buf + 4, &comp_len);
      if (ok) {
        EncodeFixed32(compressed_buf, (uint32_t)raw_size);
        data_to_write = compressed_buf;
        data_size = comp_len + 4;
        type = LITHOS_COMPRESSION_RLE;
      } else {
        free(compressed_buf);
        compressed_buf = NULL;
      }
    }
  }

  handle->offset = tb->offset;
  handle->size = data_size;

  Lithos_Slice payload = {data_to_write, data_size};
  Status s = WritableFile_Append(tb->file, payload);
  if (s.code != LITHOS_OK) {
    if (compressed_buf)
      free(compressed_buf);
    return s.code;
  }

  char trailer[BLOCK_TRAILER_SIZE];
  trailer[0] = (char)type;

  uint32_t crc = crc32c_extend(0, trailer, 1);
  crc = crc32c_extend(crc, data_to_write, data_size);
  EncodeFixed32(trailer + 1, crc);

  Lithos_Slice trailer_slice = {trailer, BLOCK_TRAILER_SIZE};
  s = WritableFile_Append(tb->file, trailer_slice);
  if (s.code != LITHOS_OK) {
    if (compressed_buf)
      free(compressed_buf);
    return s.code;
  }

  tb->offset += data_size + BLOCK_TRAILER_SIZE;

  if (compressed_buf) {
    free(compressed_buf);
  }

  return LITHOS_OK;
}

static lithos_status_code FlushDataBlock(Lithos_TableBuilder *tb) {
  assert(!tb->closed);
  assert(!BlockBuilder_Empty(tb->data_block));

  if (tb->status != LITHOS_OK) {
    return tb->status;
  }

  if (tb->filter_block != NULL) {
    FilterBlockBuilder_StartBlock(tb->filter_block, tb->offset);
  }

  lithos_status_code s = WriteBlock(tb, tb->data_block, &tb->pending_handle);
  if (s != LITHOS_OK) {
    tb->status = s;
    return s;
  }

  tb->pending_index_entry = true;

  Status sync_status = WritableFile_Flush(tb->file);
  if (sync_status.code != LITHOS_OK) {
    tb->status = sync_status.code;
    return sync_status.code;
  }

  BlockBuilder_Reset(tb->data_block);

  return LITHOS_OK;
}

static void AddIndexEntry(Lithos_TableBuilder *tb) {
  if (!tb->pending_index_entry) {
    return;
  }

  assert(tb->last_key != NULL);
  assert(tb->last_key_size > 0);

  char handle_encoding[20];
  size_t handle_len = BlockHandle_Encode(&tb->pending_handle, handle_encoding);

  Lithos_Slice key = {tb->last_key, tb->last_key_size};
  Lithos_Slice value = {handle_encoding, handle_len};

  BlockBuilder_Add(tb->index_block, key, value);
  tb->pending_index_entry = false;
}

Lithos_TableBuilder *TableBuilder_Create(const Lithos_Options *options,
                                         Lithos_WritableFile *file) {
  Lithos_TableBuilder *tb = malloc(sizeof(Lithos_TableBuilder));
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
    if (tb->data_block)
      BlockBuilder_Destroy(tb->data_block);
    if (tb->index_block)
      BlockBuilder_Destroy(tb->index_block);
    if (tb->filter_block)
      FilterBlockBuilder_Destroy(tb->filter_block);
    free(tb);
    return NULL;
  }

  return tb;
}

void TableBuilder_Destroy(Lithos_TableBuilder *tb) {
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

lithos_status_code TableBuilder_Add(Lithos_TableBuilder *tb, Lithos_Slice key,
                                    Lithos_Slice value) {
  if (tb->closed) {
    return LITHOS_INVALID_ARGUMENT;
  }

  if (tb->status != LITHOS_OK) {
    return tb->status;
  }

  if (tb->num_entries > 0) {

    Lithos_Slice last = {tb->last_key, tb->last_key_size};
    assert(InternalKeyComparator(&last, &key) < 0);
  }

  if (tb->pending_index_entry) {
    AddIndexEntry(tb);
  }

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

  tb->num_entries++;
  BlockBuilder_Add(tb->data_block, key, value);

  if (tb->filter_block != NULL) {
    FilterBlockBuilder_AddKey(tb->filter_block, key);
  }

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

lithos_status_code TableBuilder_Finish(Lithos_TableBuilder *tb) {
  if (tb->closed) {
    return tb->status;
  }

  if (tb->status != LITHOS_OK) {
    tb->closed = true;
    return tb->status;
  }

  if (!BlockBuilder_Empty(tb->data_block)) {
    lithos_status_code s = FlushDataBlock(tb);
    if (s != LITHOS_OK) {
      tb->status = s;
      tb->closed = true;
      return s;
    }
  }

  if (tb->pending_index_entry) {
    AddIndexEntry(tb);
  }

  Lithos_BlockHandle filter_handle;
  BlockHandle_Init(&filter_handle);

  if (tb->filter_block != NULL) {
    Lithos_Slice filter_contents = FilterBlockBuilder_Finish(tb->filter_block);

    Status filter_status = WritableFile_Append(tb->file, filter_contents);
    if (filter_status.code != LITHOS_OK) {
      tb->status = filter_status.code;
      tb->closed = true;
      return filter_status.code;
    }

    char trailer[BLOCK_TRAILER_SIZE];
    trailer[0] = 0;
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

  Lithos_BlockHandle metaindex_handle;
  Lithos_BlockBuilder *metaindex_block = BlockBuilder_Create(&tb->options);
  if (!metaindex_block) {
    tb->status = LITHOS_IO_ERROR;
    tb->closed = true;
    return tb->status;
  }

  if (tb->filter_block != NULL) {
    const char *filter_name = "filter.lithos.builtin";
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

  Lithos_BlockHandle index_handle;
  s = WriteBlock(tb, tb->index_block, &index_handle);
  if (s != LITHOS_OK) {
    tb->status = s;
    tb->closed = true;
    return s;
  }

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

  Status sync_status = WritableFile_Sync(tb->file);
  if (sync_status.code != LITHOS_OK) {
    tb->status = sync_status.code;
  }

  tb->closed = true;
  return tb->status;
}

void TableBuilder_Abandon(Lithos_TableBuilder *tb) { tb->closed = true; }

uint64_t TableBuilder_FileSize(const Lithos_TableBuilder *tb) {
  return tb->offset;
}

lithos_status_code TableBuilder_Status(const Lithos_TableBuilder *tb) {
  return tb->status;
}

uint64_t TableBuilder_NumEntries(const Lithos_TableBuilder *tb) {
  return tb->num_entries;
}
