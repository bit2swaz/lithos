
#include "core/table/table.h"
#include "core/table/block.h"
#include "core/table/filter_block.h"
#include "core/table/format.h"
#include "core/dbformat.h"
#include "lithos/cache.h"
#include "util/coding.h"
#include "util/compression.h"
#include "util/crc32c.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Lithos_Table {
  Lithos_Options options;
  Lithos_RandomAccessFile *file;
  uint64_t file_size;
  uint64_t cache_id;
  Lithos_Block *index_block;
  Lithos_BlockHandle metaindex_handle;
  FilterBlockReader *filter;
  const char
      *filter_data;
};

static Status Table_ReadBlock(Lithos_RandomAccessFile *file,
                              const Lithos_BlockHandle *handle,
                              Lithos_BlockContents *result) {

  size_t total_size = handle->size + 5;
  char *buffer = malloc(total_size);
  if (!buffer) {
    return Status_IOError("Out of memory", "");
  }

  Lithos_Slice read_result;
  Status s = RandomAccessFile_Read(file, handle->offset, total_size,
                                   &read_result, buffer);
  if (s.code != LITHOS_OK) {
    free(buffer);
    return s;
  }

  if (read_result.size != total_size) {
    free(buffer);
    return Status_Corruption("Truncated block read", "");
  }

  const char *data = buffer;
  uint8_t type = (uint8_t)data[handle->size];
  uint32_t stored_crc = DecodeFixed32(data + handle->size + 1);

  uint32_t actual_crc = crc32c_extend(0, (char *)&type, 1);
  actual_crc = crc32c_extend(actual_crc, data, handle->size);

  if (actual_crc != stored_crc) {
    free(buffer);
    return Status_Corruption("Block checksum mismatch", "");
  }

  if (type == LITHOS_COMPRESSION_NONE) {

    result->data = buffer;
    result->size = handle->size;
    result->heap_allocated = true;
    return Status_OK();
  }

  if (type == LITHOS_COMPRESSION_RLE) {
    if (handle->size < 4) {
      free(buffer);
      return Status_Corruption("Compressed block too small", "");
    }

    uint32_t uncompressed_len = DecodeFixed32(data);
    char *out = (char *)malloc(uncompressed_len);
    if (!out) {
      free(buffer);
      return Status_IOError("Out of memory", "");
    }

    bool ok =
        Lithos_Uncompress(data + 4, handle->size - 4, out, uncompressed_len);
    free(buffer);
    if (!ok) {
      free(out);
      return Status_Corruption("Failed to decompress block", "");
    }

    result->data = out;
    result->size = uncompressed_len;
    result->heap_allocated = true;
    return Status_OK();
  }

  free(buffer);
  return Status_Corruption("Unknown compression type", "");
}

static void EncodeBlockCacheKey(char *buffer, uint64_t cache_id,
                                uint64_t offset) {
  EncodeFixed64(buffer, cache_id);
  EncodeFixed64(buffer + 8, offset);
}

static void DeleteCachedBlock(const Lithos_Slice *key, void *value) {
  (void)key;
  Lithos_Block *block = (Lithos_Block *)value;
  Block_Destroy(block);
}

static Status Table_ReadBlockCached(Lithos_Table *table,
                                    const Lithos_BlockHandle *handle,
                                    Lithos_Block **result,
                                    Lithos_CacheHandle **cache_handle) {
  *cache_handle = NULL;

  if (table->options.block_cache != NULL) {
    char cache_key_buf[16];
    EncodeBlockCacheKey(cache_key_buf, table->cache_id, handle->offset);
    Lithos_Slice cache_key = {cache_key_buf, 16};

    Lithos_CacheHandle *h = Cache_Lookup(table->options.block_cache, cache_key);
    if (h != NULL) {

      *result = (Lithos_Block *)Cache_Value(h);
      *cache_handle = h;
      return Status_OK();
    }
  }

  Lithos_BlockContents contents;
  Status s = Table_ReadBlock(table->file, handle, &contents);
  if (s.code != LITHOS_OK) {
    return s;
  }

  Lithos_Block *block = Block_Create(contents);
  if (block == NULL) {
    if (contents.heap_allocated) {
      free((void *)contents.data);
    }
    return Status_Corruption("Could not parse block", "");
  }

  if (table->options.block_cache != NULL) {
    char cache_key_buf[16];
    EncodeBlockCacheKey(cache_key_buf, table->cache_id, handle->offset);
    Lithos_Slice cache_key = {cache_key_buf, 16};

    size_t charge = contents.size + 256;

    Lithos_CacheHandle *h = Cache_Insert(table->options.block_cache, cache_key,
                                         block, charge, DeleteCachedBlock);

    *cache_handle = h;
  }

  *result = block;
  return Status_OK();
}

Status Table_Open(const Lithos_Options *options, Lithos_RandomAccessFile *file,
                  uint64_t file_size, Lithos_Table **table) {
  if (file_size < LITHOS_FOOTER_ENCODED_LENGTH) {
    return Status_Corruption("File too short to be an SSTable", "");
  }

  char footer_buf[LITHOS_FOOTER_ENCODED_LENGTH];
  Lithos_Slice footer_input;
  Status s = RandomAccessFile_Read(
      file, file_size - LITHOS_FOOTER_ENCODED_LENGTH,
      LITHOS_FOOTER_ENCODED_LENGTH, &footer_input, footer_buf);
  if (s.code != LITHOS_OK) {
    return s;
  }

  if (footer_input.size != LITHOS_FOOTER_ENCODED_LENGTH) {
    return Status_Corruption("Failed to read footer", "");
  }

  Lithos_Footer footer;
  lithos_status_code decode_status = Footer_Decode(&footer, footer_buf);
  if (decode_status != LITHOS_OK) {
    return Status_Corruption("Invalid footer", "");
  }

  Lithos_BlockContents index_contents;
  s = Table_ReadBlock(file, &footer.index_handle, &index_contents);
  if (s.code != LITHOS_OK) {
    return s;
  }

  Lithos_Block *index_block = Block_Create(index_contents);
  if (!index_block) {
    return Status_Corruption("Failed to parse index block", "");
  }

  Lithos_Table *t = malloc(sizeof(Lithos_Table));
  if (!t) {
    Block_Destroy(index_block);
    return Status_IOError("Out of memory", "");
  }

  t->options = *options;
  t->file = file;
  t->file_size = file_size;
  t->cache_id =
      (options->block_cache != NULL) ? Cache_NewId(options->block_cache) : 0;
  t->index_block = index_block;
  t->metaindex_handle = footer.metaindex_handle;
  t->filter = NULL;
  t->filter_data = NULL;

  if (options->filter_policy != NULL) {

    Lithos_BlockContents metaindex_contents;
    s = Table_ReadBlock(file, &footer.metaindex_handle, &metaindex_contents);
    if (s.code == LITHOS_OK) {
      Lithos_Block *metaindex_block = Block_Create(metaindex_contents);
      if (metaindex_block) {

        Lithos_Iterator *meta_iter =
            Block_NewIterator(metaindex_block, &InternalKeyComparator);
        const char *filter_name = "filter.lithos.builtin";
        Lithos_Slice filter_key = {filter_name, strlen(filter_name)};

        Lithos_Iter_Seek(meta_iter, filter_key);
        if (Lithos_Iter_Valid(meta_iter)) {
          Lithos_Slice found_key = Lithos_Iter_Key(meta_iter);
          if (Slice_Compare(found_key, filter_key) == 0) {

            Lithos_Slice handle_value = Lithos_Iter_Value(meta_iter);
            Lithos_BlockHandle filter_handle;
            const char *p = handle_value.data;
            const char *limit = p + handle_value.size;

            if (BlockHandle_Decode(&filter_handle, &p, limit) == LITHOS_OK) {

              Lithos_BlockContents filter_contents;
              s = Table_ReadBlock(file, &filter_handle, &filter_contents);
              if (s.code == LITHOS_OK) {
                Lithos_Slice filter_data = {filter_contents.data,
                                            filter_contents.size};
                FilterBlockReader *reader = FilterBlockReader_Create(
                    options->filter_policy, filter_data);
                if (reader != NULL) {
                  t->filter = reader;
                  if (filter_contents.heap_allocated) {
                    t->filter_data = filter_contents.data;
                  }
                } else if (filter_contents.heap_allocated) {
                  free((void *)filter_contents.data);
                }
              }
            }
          }
        }

        Lithos_Iter_Destroy(meta_iter);
        Block_Destroy(metaindex_block);
      }
    }
  }

  *table = t;
  return Status_OK();
}

void Table_Destroy(Lithos_Table *table) {
  if (table) {
    Block_Destroy(table->index_block);
    FilterBlockReader_Destroy(table->filter);
    if (table->filter_data) {
      free((void *)table->filter_data);
    }
    RandomAccessFile_Close(table->file);
    free(table);
  }
}

typedef struct {
  Lithos_Table *table;
  Lithos_Iterator *index_iter;
  Lithos_Iterator *data_iter;
  Lithos_CacheHandle *data_cache_handle;
  Lithos_Block *data_block;
  Status status;

  Lithos_BlockHandle data_block_handle;
  bool have_data_block;
} TwoLevelIterator;

static void TwoLevel_InitDataBlock(TwoLevelIterator *iter) {

  if (!Lithos_Iter_Valid(iter->index_iter)) {

    if (iter->data_iter) {
      Lithos_Iter_Destroy(iter->data_iter);
      iter->data_iter = NULL;
    }
    if (iter->data_cache_handle) {
      Cache_Release(iter->table->options.block_cache, iter->data_cache_handle);
      iter->data_cache_handle = NULL;
    }
    iter->have_data_block = false;
    return;
  }

  Lithos_Slice handle_value = Lithos_Iter_Value(iter->index_iter);
  const char *p = handle_value.data;
  const char *limit = p + handle_value.size;

  lithos_status_code decode_status =
      BlockHandle_Decode(&iter->data_block_handle, &p, limit);
  if (decode_status != LITHOS_OK) {
    iter->status = Status_Corruption("Bad data block handle", "");
    iter->have_data_block = false;
    return;
  }

  if (iter->data_iter) {
    Lithos_Iter_Destroy(iter->data_iter);
    iter->data_iter = NULL;
  }
  if (iter->data_cache_handle) {
    Cache_Release(iter->table->options.block_cache, iter->data_cache_handle);
    iter->data_cache_handle = NULL;
  } else if (iter->data_block) {
    Block_Destroy(iter->data_block);
    iter->data_block = NULL;
  }

  Lithos_Block *data_block = NULL;
  Lithos_CacheHandle *cache_handle = NULL;
  Status s = Table_ReadBlockCached(iter->table, &iter->data_block_handle,
                                   &data_block, &cache_handle);
  if (s.code != LITHOS_OK) {
    iter->status = s;
    iter->have_data_block = false;
    return;
  }

  iter->data_cache_handle = cache_handle;
  iter->data_block = data_block;

  iter->data_iter = Block_NewIterator(data_block, InternalKeyComparator);
  iter->have_data_block = true;

}

static void TwoLevel_SkipEmptyDataBlocksForward(TwoLevelIterator *iter) {

  while (!iter->data_iter || !Lithos_Iter_Valid(iter->data_iter)) {
    if (!Lithos_Iter_Valid(iter->index_iter)) {

      if (iter->data_iter) {
        Lithos_Iter_Destroy(iter->data_iter);
        iter->data_iter = NULL;
      }
      return;
    }

    Lithos_Iter_Next(iter->index_iter);
    TwoLevel_InitDataBlock(iter);

    if (iter->data_iter) {
      Lithos_Iter_SeekToFirst(iter->data_iter);
    }
  }
}

static bool TwoLevel_Valid(void *state) {
  TwoLevelIterator *iter = (TwoLevelIterator *)state;
  return iter->data_iter && Lithos_Iter_Valid(iter->data_iter);
}

static void TwoLevel_SeekToFirst(void *state) {
  TwoLevelIterator *iter = (TwoLevelIterator *)state;

  Lithos_Iter_SeekToFirst(iter->index_iter);
  TwoLevel_InitDataBlock(iter);

  if (iter->data_iter) {
    Lithos_Iter_SeekToFirst(iter->data_iter);
  }

  TwoLevel_SkipEmptyDataBlocksForward(iter);
}

static void TwoLevel_SeekToLast(void *state) {
  TwoLevelIterator *iter = (TwoLevelIterator *)state;

  Lithos_Iter_SeekToLast(iter->index_iter);
  TwoLevel_InitDataBlock(iter);

  if (iter->data_iter) {
    Lithos_Iter_SeekToLast(iter->data_iter);
  }

}

static void TwoLevel_Seek(void *state, Lithos_Slice target) {
  TwoLevelIterator *iter = (TwoLevelIterator *)state;

  Lithos_Iter_Seek(iter->index_iter, target);
  TwoLevel_InitDataBlock(iter);

  if (iter->data_iter) {
    Lithos_Iter_Seek(iter->data_iter, target);
  }

  TwoLevel_SkipEmptyDataBlocksForward(iter);
}

static void TwoLevel_Next(void *state) {
  TwoLevelIterator *iter = (TwoLevelIterator *)state;
  assert(TwoLevel_Valid(state));

  Lithos_Iter_Next(iter->data_iter);
  TwoLevel_SkipEmptyDataBlocksForward(iter);
}

static void TwoLevel_Prev(void *state) {
  TwoLevelIterator *iter = (TwoLevelIterator *)state;
  assert(TwoLevel_Valid(state));

  Lithos_Iter_Prev(iter->data_iter);

  while (!Lithos_Iter_Valid(iter->data_iter)) {
    Lithos_Iter_Prev(iter->index_iter);

    if (!Lithos_Iter_Valid(iter->index_iter)) {

      if (iter->data_iter) {
        Lithos_Iter_Destroy(iter->data_iter);
        iter->data_iter = NULL;
      }
      return;
    }

    TwoLevel_InitDataBlock(iter);
    if (iter->data_iter) {
      Lithos_Iter_SeekToLast(iter->data_iter);
    }
  }
}

static Lithos_Slice TwoLevel_Key(void *state) {
  TwoLevelIterator *iter = (TwoLevelIterator *)state;
  assert(TwoLevel_Valid(state));
  return Lithos_Iter_Key(iter->data_iter);
}

static Lithos_Slice TwoLevel_Value(void *state) {
  TwoLevelIterator *iter = (TwoLevelIterator *)state;
  assert(TwoLevel_Valid(state));
  return Lithos_Iter_Value(iter->data_iter);
}

static Status TwoLevel_GetStatus(void *state) {
  TwoLevelIterator *iter = (TwoLevelIterator *)state;

  if (iter->status.code != LITHOS_OK) {
    return iter->status;
  }

  Status index_status = Lithos_Iter_GetStatus(iter->index_iter);
  if (index_status.code != LITHOS_OK) {
    return index_status;
  }

  if (iter->data_iter) {
    return Lithos_Iter_GetStatus(iter->data_iter);
  }

  return Status_OK();
}

static void TwoLevel_Cleanup(void *state) {
  TwoLevelIterator *iter = (TwoLevelIterator *)state;

  if (iter->index_iter) {
    Lithos_Iter_Destroy(iter->index_iter);
  }

  if (iter->data_iter) {
    Lithos_Iter_Destroy(iter->data_iter);
  }

  if (iter->data_cache_handle) {
    Cache_Release(iter->table->options.block_cache, iter->data_cache_handle);
  } else if (iter->data_block) {
    Block_Destroy(iter->data_block);
  }

  free(iter);
}

static const Lithos_IteratorVTable two_level_vtable = {
    .Valid = TwoLevel_Valid,
    .SeekToFirst = TwoLevel_SeekToFirst,
    .SeekToLast = TwoLevel_SeekToLast,
    .Seek = TwoLevel_Seek,
    .Next = TwoLevel_Next,
    .Prev = TwoLevel_Prev,
    .Key = TwoLevel_Key,
    .Value = TwoLevel_Value,
    .GetStatus = TwoLevel_GetStatus,
    .Cleanup = TwoLevel_Cleanup};

Lithos_Iterator *Table_NewIterator(Lithos_Table *table,
                                   const Lithos_Options *options) {
  (void)options;

  Lithos_Iterator *index_iter =
      Block_NewIterator(table->index_block, InternalKeyComparator);
  if (!index_iter) {
    return NULL;
  }

  TwoLevelIterator *iter = calloc(1, sizeof(TwoLevelIterator));
  if (!iter) {
    Lithos_Iter_Destroy(index_iter);
    return NULL;
  }

  iter->table = table;
  iter->index_iter = index_iter;
  iter->data_iter = NULL;
  iter->data_block = NULL;
  iter->status = Status_OK();
  iter->have_data_block = false;

  Lithos_Iterator *result = malloc(sizeof(Lithos_Iterator));
  if (!result) {
    Lithos_Iter_Destroy(index_iter);
    free(iter);
    return NULL;
  }

  result->vtable = &two_level_vtable;
  result->state = iter;

  return result;
}

Status Table_InternalGet(Lithos_Table *table, Lithos_Slice key, void *arg,
                         void (*saver)(void *arg, Lithos_Slice key,
                                       Lithos_Slice value)) {
  Lithos_Iterator *iter = Table_NewIterator(table, &table->options);
  if (!iter) {
    return Status_IOError("Failed to create iterator", "");
  }

  Lithos_Iter_Seek(iter, key);

  if (Lithos_Iter_Valid(iter)) {
    Lithos_Slice found_key = Lithos_Iter_Key(iter);
    Lithos_Slice found_user = ExtractUserKey(found_key);
    Lithos_Slice target_user = ExtractUserKey(key);

    if (Slice_Compare(found_user, target_user) == 0) {
      Lithos_Slice value = Lithos_Iter_Value(iter);
      saver(arg, found_key, value);
    }
  }

  Status s = Lithos_Iter_GetStatus(iter);
  Lithos_Iter_Destroy(iter);

  return s;
}
