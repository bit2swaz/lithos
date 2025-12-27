
#include "core/table/block.h"
#include "util/coding.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static void BlockIter_Next(void *state);

struct Lithos_Block {
  const char
      *data;
  size_t size;
  uint32_t restart_offset;

  uint32_t num_restarts;
  bool
      owned;
};

typedef struct {
  Lithos_Block *block;
  Lithos_Comparator
      cmp;

  uint32_t current_offset;
  uint32_t restart_index;

  char *key_buf;
  size_t key_capacity;
  size_t key_size;

  Lithos_Slice value;

  Status status;
  bool valid;
} BlockIterator;

static uint32_t Block_GetRestartOffset(const Lithos_Block *block,
                                       uint32_t index) {
  assert(index < block->num_restarts);
  const char *restart_ptr = block->data + block->restart_offset + index * 4;
  return DecodeFixed32(restart_ptr);
}

Lithos_Block *Block_Create(Lithos_BlockContents contents) {
  if (contents.size < sizeof(uint32_t)) {

    if (contents.heap_allocated) {
      free((void *)contents.data);
    }
    return NULL;
  }

  Lithos_Block *block = malloc(sizeof(Lithos_Block));
  if (!block) {
    if (contents.heap_allocated) {
      free((void *)contents.data);
    }
    return NULL;
  }

  block->num_restarts = DecodeFixed32(contents.data + contents.size - 4);

  uint32_t max_restarts_allowed = (contents.size - 4) / 4;
  if (block->num_restarts > max_restarts_allowed) {

    free(block);
    if (contents.heap_allocated) {
      free((void *)contents.data);
    }
    return NULL;
  }

  block->restart_offset = contents.size - (1 + block->num_restarts) * 4;
  block->data = contents.data;
  block->size = contents.size;
  block->owned = contents.heap_allocated;

  return block;
}

void Block_Destroy(Lithos_Block *block) {
  if (block) {
    if (block->owned) {
      free((void *)block->data);
    }
    free(block);
  }
}

uint32_t Block_GetRestartCount(const Lithos_Block *block) {
  return block->num_restarts;
}

static bool BlockIter_ParseEntry(BlockIterator *iter, uint32_t offset) {
  if (offset >= iter->block->restart_offset) {

    iter->valid = false;
    return false;
  }

  const char *p = iter->block->data + offset;
  const char *limit =
      iter->block->data +
      iter->block->restart_offset;

  uint64_t shared, non_shared, value_len;
  p = GetVarint64Ptr(p, limit, &shared);
  if (!p)
    goto corruption;
  p = GetVarint64Ptr(p, limit, &non_shared);
  if (!p)
    goto corruption;
  p = GetVarint64Ptr(p, limit, &value_len);
  if (!p)
    goto corruption;

  if (shared > iter->key_size)
    goto corruption;
  if (p + non_shared + value_len > limit)
    goto corruption;

  size_t total_key_size = shared + non_shared;
  if (total_key_size > iter->key_capacity) {

    iter->key_capacity = total_key_size * 2;
    iter->key_buf = realloc(iter->key_buf, iter->key_capacity);
    if (!iter->key_buf) {
      iter->status = Status_IOError("Out of memory", "");
      iter->valid = false;
      return false;
    }
  }

  memcpy(iter->key_buf + shared, p, non_shared);
  iter->key_size = total_key_size;
  p += non_shared;

  iter->value.data = p;
  iter->value.size = value_len;

  iter->current_offset = offset;
  iter->valid = true;
  return true;

corruption:
  iter->status = Status_Corruption("Block entry corrupted", "");
  iter->valid = false;
  return false;
}

static uint32_t BlockIter_SeekToRestartPoint(BlockIterator *iter,
                                             Lithos_Slice target) {

  uint32_t left = 0;
  uint32_t right = iter->block->num_restarts - 1;

  while (left < right) {
    uint32_t mid = (left + right + 1) / 2;
    uint32_t offset = Block_GetRestartOffset(iter->block, mid);

    if (!BlockIter_ParseEntry(iter, offset)) {
      return left;
    }

    Lithos_Slice mid_key = {iter->key_buf, iter->key_size};
    int cmp = Slice_Compare(mid_key, target);

    if (cmp < 0) {
      left = mid;
    } else {
      right = mid - 1;
    }
  }

  return left;
}

static bool BlockIter_Valid(void *state) {
  BlockIterator *iter = (BlockIterator *)state;
  return iter->valid;
}

static void BlockIter_SeekToFirst(void *state) {
  BlockIterator *iter = (BlockIterator *)state;

  if (iter->block->num_restarts == 0) {
    iter->valid = false;
    return;
  }

  uint32_t offset = Block_GetRestartOffset(iter->block, 0);
  iter->restart_index = 0;
  BlockIter_ParseEntry(iter, offset);
}

static void BlockIter_SeekToLast(void *state) {
  BlockIterator *iter = (BlockIterator *)state;

  if (iter->block->num_restarts == 0) {
    iter->valid = false;
    return;
  }

  iter->restart_index = iter->block->num_restarts - 1;
  uint32_t offset = Block_GetRestartOffset(iter->block, iter->restart_index);
  BlockIter_ParseEntry(iter, offset);

  while (iter->valid) {

    uint32_t next_offset =
        iter->current_offset + (iter->value.data + iter->value.size -
                                (iter->block->data + iter->current_offset));

    if (next_offset >= iter->block->restart_offset) {
      break;
    }

    if (!BlockIter_ParseEntry(iter, next_offset)) {
      break;
    }
  }
}

static void BlockIter_Seek(void *state, Lithos_Slice target) {
  BlockIterator *iter = (BlockIterator *)state;

  if (iter->block->num_restarts == 0) {
    iter->valid = false;
    return;
  }

  uint32_t restart_index = BlockIter_SeekToRestartPoint(iter, target);
  iter->restart_index = restart_index;

  uint32_t offset = Block_GetRestartOffset(iter->block, restart_index);
  BlockIter_ParseEntry(iter, offset);

  while (iter->valid) {
    Lithos_Slice current_key = {iter->key_buf, iter->key_size};
    int cmp = Slice_Compare(current_key, target);

    if (cmp >= 0) {
      return;
    }

    BlockIter_Next(iter);
  }
}

static void BlockIter_Next(void *state) {
  BlockIterator *iter = (BlockIterator *)state;

  if (!iter->valid) {
    return;
  }

  uint32_t next_offset = iter->current_offset;
  const char *p = iter->block->data + iter->current_offset;
  const char *limit = iter->block->data + iter->block->restart_offset;

  uint64_t shared, non_shared, value_len;
  p = GetVarint64Ptr(p, limit, &shared);
  p = GetVarint64Ptr(p, limit, &non_shared);
  p = GetVarint64Ptr(p, limit, &value_len);

  if (!p) {
    iter->valid = false;
    return;
  }

  next_offset = (p - iter->block->data) + non_shared + value_len;

  if (next_offset >= iter->block->restart_offset) {
    iter->valid = false;
    return;
  }

  if (iter->restart_index + 1 < iter->block->num_restarts) {
    uint32_t next_restart_offset =
        Block_GetRestartOffset(iter->block, iter->restart_index + 1);
    if (next_offset == next_restart_offset) {
      iter->restart_index++;
    }
  }

  BlockIter_ParseEntry(iter, next_offset);
}

static void BlockIter_Prev(void *state) {
  BlockIterator *iter = (BlockIterator *)state;

  (void)iter;
  iter->valid = false;
}

static Lithos_Slice BlockIter_Key(void *state) {
  BlockIterator *iter = (BlockIterator *)state;
  assert(iter->valid);
  Lithos_Slice key = {iter->key_buf, iter->key_size};
  return key;
}

static Lithos_Slice BlockIter_Value(void *state) {
  BlockIterator *iter = (BlockIterator *)state;
  assert(iter->valid);
  return iter->value;
}

static Status BlockIter_GetStatus(void *state) {
  BlockIterator *iter = (BlockIterator *)state;
  return iter->status;
}

static void BlockIter_Cleanup(void *state) {
  BlockIterator *iter = (BlockIterator *)state;
  free(iter->key_buf);
  free(iter);
}

static const Lithos_IteratorVTable block_iter_vtable = {
    .Valid = BlockIter_Valid,
    .SeekToFirst = BlockIter_SeekToFirst,
    .SeekToLast = BlockIter_SeekToLast,
    .Seek = BlockIter_Seek,
    .Next = BlockIter_Next,
    .Prev = BlockIter_Prev,
    .Key = BlockIter_Key,
    .Value = BlockIter_Value,
    .GetStatus = BlockIter_GetStatus,
    .Cleanup = BlockIter_Cleanup};

Lithos_Iterator *Block_NewIterator(Lithos_Block *block, Lithos_Comparator cmp) {
  BlockIterator *iter = calloc(1, sizeof(BlockIterator));
  if (!iter) {
    return NULL;
  }

  iter->block = block;
  iter->cmp = cmp;
  iter->status = Status_OK();
  iter->valid = false;
  iter->key_capacity = 256;
  iter->key_buf = malloc(iter->key_capacity);

  if (!iter->key_buf) {
    free(iter);
    return NULL;
  }

  Lithos_Iterator *result = malloc(sizeof(Lithos_Iterator));
  if (!result) {
    free(iter->key_buf);
    free(iter);
    return NULL;
  }

  result->vtable = &block_iter_vtable;
  result->state = iter;

  return result;
}
