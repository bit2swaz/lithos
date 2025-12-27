
#include "core/table/block_builder.h"
#include "util/coding.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct Lithos_BlockBuilder {
  const Lithos_Options *options;

  char *buffer;
  size_t buffer_size;
  size_t buffer_capacity;

  uint32_t *restarts;
  size_t restarts_count;
  size_t restarts_capacity;

  int counter;
  bool finished;

  char *last_key;
  size_t last_key_size;
  size_t last_key_capacity;
};

static void Buffer_Append(Lithos_BlockBuilder *b, const char *data,
                          size_t size) {

  if (b->buffer_size + size > b->buffer_capacity) {
    size_t new_capacity = b->buffer_capacity * 2;
    if (new_capacity < b->buffer_size + size) {
      new_capacity =
          b->buffer_size + size;
    }
    b->buffer = (char *)realloc(b->buffer, new_capacity);
    assert(b->buffer != NULL);
    b->buffer_capacity = new_capacity;
  }

  memcpy(b->buffer + b->buffer_size, data, size);
  b->buffer_size += size;
}

static void Buffer_AppendVarint32(Lithos_BlockBuilder *b, uint32_t value) {
  char buf[5];
  char *ptr = EncodeVarint32(buf, value);
  Buffer_Append(b, buf, ptr - buf);
}

static void AddRestartPoint(Lithos_BlockBuilder *b) {

  if (b->restarts_count >= b->restarts_capacity) {
    size_t new_capacity =
        (b->restarts_capacity == 0) ? 8 : (b->restarts_capacity * 2);
    b->restarts =
        (uint32_t *)realloc(b->restarts, new_capacity * sizeof(uint32_t));
    assert(b->restarts != NULL);
    b->restarts_capacity = new_capacity;
  }

  b->restarts[b->restarts_count++] = (uint32_t)b->buffer_size;
}

static void UpdateLastKey(Lithos_BlockBuilder *b, const char *key,
                          size_t key_size) {

  if (key_size > b->last_key_capacity) {
    size_t new_capacity = key_size * 2;
    b->last_key = (char *)realloc(b->last_key, new_capacity);
    assert(b->last_key != NULL);
    b->last_key_capacity = new_capacity;
  }

  memcpy(b->last_key, key, key_size);
  b->last_key_size = key_size;
}

Lithos_BlockBuilder *BlockBuilder_Create(const Lithos_Options *options) {
  Lithos_BlockBuilder *b =
      (Lithos_BlockBuilder *)malloc(sizeof(Lithos_BlockBuilder));
  assert(b != NULL);

  b->options = options;

  b->buffer_capacity = 1024;
  b->buffer = (char *)malloc(b->buffer_capacity);
  assert(b->buffer != NULL);
  b->buffer_size = 0;

  b->restarts_capacity = 8;
  b->restarts = (uint32_t *)malloc(b->restarts_capacity * sizeof(uint32_t));
  assert(b->restarts != NULL);
  b->restarts_count = 0;

  b->counter = 0;
  b->finished = false;

  b->last_key_capacity = 64;
  b->last_key = (char *)malloc(b->last_key_capacity);
  assert(b->last_key != NULL);
  b->last_key_size = 0;

  AddRestartPoint(b);

  return b;
}

void BlockBuilder_Destroy(Lithos_BlockBuilder *b) {
  if (b == NULL)
    return;

  free(b->buffer);
  free(b->restarts);
  free(b->last_key);
  free(b);
}

void BlockBuilder_Reset(Lithos_BlockBuilder *b) {

  b->buffer_size = 0;
  b->restarts_count = 0;
  b->counter = 0;
  b->finished = false;
  b->last_key_size = 0;

  AddRestartPoint(b);
}

void BlockBuilder_Add(Lithos_BlockBuilder *b, Lithos_Slice key,
                      Lithos_Slice value) {
  assert(!b->finished);
  assert(b->counter <= (int)b->options->block_restart_interval);

  size_t shared = 0;

  if (b->counter >= (int)b->options->block_restart_interval) {

    AddRestartPoint(b);
    b->counter = 0;
  } else {

    const size_t min_length =
        (key.size < b->last_key_size) ? key.size : b->last_key_size;
    while (shared < min_length && b->last_key[shared] == key.data[shared]) {
      shared++;
    }
  }

  const size_t non_shared = key.size - shared;

  Buffer_AppendVarint32(b, (uint32_t)shared);
  Buffer_AppendVarint32(b, (uint32_t)non_shared);
  Buffer_AppendVarint32(b, (uint32_t)value.size);

  Buffer_Append(b, key.data + shared,
                non_shared);
  Buffer_Append(b, value.data, value.size);

  UpdateLastKey(b, key.data, key.size);

  b->counter++;
}

Lithos_Slice BlockBuilder_Finish(Lithos_BlockBuilder *b) {

  for (size_t i = 0; i < b->restarts_count; i++) {
    char buf[4];
    EncodeFixed32(buf, b->restarts[i]);
    Buffer_Append(b, buf, 4);
  }

  char count_buf[4];
  EncodeFixed32(count_buf, (uint32_t)b->restarts_count);
  Buffer_Append(b, count_buf, 4);

  b->finished = true;

  Lithos_Slice result;
  result.data = b->buffer;
  result.size = b->buffer_size;
  return result;
}

size_t BlockBuilder_CurrentSizeEstimate(Lithos_BlockBuilder *b) {

  if (b->finished) {
    return b->buffer_size;
  }

  return b->buffer_size + (b->restarts_count * 4) + 4;
}

bool BlockBuilder_Empty(Lithos_BlockBuilder *b) { return b->buffer_size == 0; }
