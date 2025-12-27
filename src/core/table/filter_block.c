
#include "filter_block.h"
#include "util/coding.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define FILTER_BASE_LG 11
#define FILTER_BASE (1 << FILTER_BASE_LG)

struct FilterBlockBuilder {
  const Lithos_FilterPolicy
      *policy;

  Lithos_Slice *keys;
  size_t num_keys;
  size_t keys_capacity;

  uint32_t *filter_offsets;
  size_t num_filters;
  size_t filter_offsets_capacity;

  char *result;
  size_t result_len;
  size_t result_capacity;

  char *key_storage;
  size_t key_storage_len;
  size_t key_storage_capacity;
};

FilterBlockBuilder *
FilterBlockBuilder_Create(const Lithos_FilterPolicy *policy) {
  FilterBlockBuilder *builder = calloc(1, sizeof(FilterBlockBuilder));
  if (!builder) {
    return NULL;
  }

  builder->policy = policy;
  builder->keys_capacity = 64;
  builder->keys = malloc(builder->keys_capacity * sizeof(Lithos_Slice));
  builder->filter_offsets_capacity = 64;
  builder->filter_offsets =
      malloc(builder->filter_offsets_capacity * sizeof(uint32_t));
  builder->result_capacity = 4096;
  builder->result = malloc(builder->result_capacity);
  builder->key_storage_capacity = 4096;
  builder->key_storage = malloc(builder->key_storage_capacity);

  if (!builder->keys || !builder->filter_offsets || !builder->result ||
      !builder->key_storage) {
    FilterBlockBuilder_Destroy(builder);
    return NULL;
  }

  return builder;
}

void FilterBlockBuilder_Destroy(FilterBlockBuilder *builder) {
  if (builder) {
    free(builder->keys);
    free(builder->filter_offsets);
    free(builder->result);
    free(builder->key_storage);
    free(builder);
  }
}

static void GenerateFilter(FilterBlockBuilder *builder) {
  if (builder->num_keys == 0) {

    if (builder->num_filters >= builder->filter_offsets_capacity) {
      builder->filter_offsets_capacity *= 2;
      builder->filter_offsets =
          realloc(builder->filter_offsets,
                  builder->filter_offsets_capacity * sizeof(uint32_t));
    }
    builder->filter_offsets[builder->num_filters++] =
        (uint32_t)builder->result_len;
    return;
  }

  if (builder->num_filters >= builder->filter_offsets_capacity) {
    builder->filter_offsets_capacity *= 2;
    builder->filter_offsets =
        realloc(builder->filter_offsets,
                builder->filter_offsets_capacity * sizeof(uint32_t));
  }
  builder->filter_offsets[builder->num_filters++] =
      (uint32_t)builder->result_len;

  FilterPolicy_CreateFilter(builder->policy, builder->keys,
                            (int)builder->num_keys, &builder->result,
                            &builder->result_len, &builder->result_capacity);

  builder->num_keys = 0;
  builder->key_storage_len = 0;
}

void FilterBlockBuilder_StartBlock(FilterBlockBuilder *builder,
                                   uint64_t block_offset) {

  uint64_t filter_index = block_offset / FILTER_BASE;

  while (filter_index > builder->num_filters) {
    GenerateFilter(builder);
  }
}

void FilterBlockBuilder_AddKey(FilterBlockBuilder *builder, Lithos_Slice key) {

  if (builder->num_keys >= builder->keys_capacity) {
    builder->keys_capacity *= 2;
    builder->keys =
        realloc(builder->keys, builder->keys_capacity * sizeof(Lithos_Slice));
  }

  size_t needed = builder->key_storage_len + key.size;
  if (needed > builder->key_storage_capacity) {
    builder->key_storage_capacity = needed * 2;
    builder->key_storage =
        realloc(builder->key_storage, builder->key_storage_capacity);
  }

  memcpy(builder->key_storage + builder->key_storage_len, key.data, key.size);
  builder->keys[builder->num_keys].data =
      builder->key_storage + builder->key_storage_len;
  builder->keys[builder->num_keys].size = key.size;
  builder->key_storage_len += key.size;
  builder->num_keys++;
}

Lithos_Slice FilterBlockBuilder_Finish(FilterBlockBuilder *builder) {

  if (builder->num_keys > 0) {
    GenerateFilter(builder);
  }

  size_t array_offset = builder->result_len;
  for (size_t i = 0; i < builder->num_filters; i++) {

    if (builder->result_len + 4 > builder->result_capacity) {
      builder->result_capacity = (builder->result_len + 4) * 2;
      builder->result = realloc(builder->result, builder->result_capacity);
    }

    EncodeFixed32(builder->result + builder->result_len,
                  builder->filter_offsets[i]);
    builder->result_len += 4;
  }

  if (builder->result_len + 5 > builder->result_capacity) {
    builder->result_capacity = (builder->result_len + 5) * 2;
    builder->result = realloc(builder->result, builder->result_capacity);
  }

  EncodeFixed32(builder->result + builder->result_len, (uint32_t)array_offset);
  builder->result_len += 4;

  builder->result[builder->result_len++] = FILTER_BASE_LG;

  Lithos_Slice result = {builder->result, builder->result_len};
  return result;
}

struct FilterBlockReader {
  const Lithos_FilterPolicy *policy;
  const char *data;
  size_t size;
  size_t offset_base;
  size_t num_filters;
  uint32_t base_lg;
};

FilterBlockReader *FilterBlockReader_Create(const Lithos_FilterPolicy *policy,
                                            Lithos_Slice contents) {
  size_t n = contents.size;
  if (n < 5) {
    return NULL;
  }

  FilterBlockReader *reader = malloc(sizeof(FilterBlockReader));
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
    return NULL;
  }

  reader->num_filters = (n - 5 - reader->offset_base) / 4;

  return reader;
}

void FilterBlockReader_Destroy(FilterBlockReader *reader) { free(reader); }

bool FilterBlockReader_KeyMayMatch(FilterBlockReader *reader,
                                   uint64_t block_offset, Lithos_Slice key) {

  uint64_t index = block_offset >> reader->base_lg;
  if (index < reader->num_filters) {

    uint32_t start =
        DecodeFixed32(reader->data + reader->offset_base + index * 4);
    uint32_t limit;
    if (index + 1 < reader->num_filters) {
      limit =
          DecodeFixed32(reader->data + reader->offset_base + (index + 1) * 4);
    } else {
      limit = (uint32_t)reader->offset_base;
    }

    if (start <= limit && limit <= reader->offset_base) {
      Lithos_Slice filter = {reader->data + start, limit - start};
      return FilterPolicy_KeyMayMatch(reader->policy, key, filter);
    } else if (start == limit) {

      return false;
    }
  }

  return true;
}
