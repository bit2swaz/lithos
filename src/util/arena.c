
#include "util/arena.h"
#include <assert.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

static const size_t kBlockSize = 4096;
static const size_t kAlignment = 8;

static char *Arena_AllocateFallbackSlow(Lithos_Arena *arena, size_t bytes);

struct Lithos_Arena {

  char *alloc_ptr;
  size_t alloc_bytes_remaining;

  char **blocks;
  size_t blocks_count;
  size_t blocks_capacity;

  _Atomic size_t
      memory_usage;
};

static char *AllocateNewBlock(size_t size) {
  char *block = (char *)malloc(size);
  return block;
}

static int AddBlock(Lithos_Arena *arena, char *block) {

  if (arena->blocks_count >= arena->blocks_capacity) {

    size_t new_capacity =
        arena->blocks_capacity == 0 ? 16 : arena->blocks_capacity * 2;

    char **new_blocks =
        (char **)realloc(arena->blocks, new_capacity * sizeof(char *));
    if (new_blocks == NULL) {

      return -1;
    }

    arena->blocks = new_blocks;
    arena->blocks_capacity = new_capacity;
  }

  arena->blocks[arena->blocks_count] = block;
  arena->blocks_count++;

  return 0;
}

static char *AllocateFallback(Lithos_Arena *arena, size_t block_bytes) {
  char *block = AllocateNewBlock(block_bytes);
  if (block == NULL) {
    return NULL;
  }

  if (AddBlock(arena, block) != 0) {

    free(block);
    return NULL;
  }

  atomic_fetch_add_explicit(&arena->memory_usage, block_bytes,
                            memory_order_relaxed);

  return block;
}

Lithos_Arena *Arena_Create(void) {
  Lithos_Arena *arena = (Lithos_Arena *)malloc(sizeof(Lithos_Arena));
  if (arena == NULL) {
    return NULL;
  }

  arena->alloc_ptr = NULL;
  arena->alloc_bytes_remaining = 0;
  arena->blocks = NULL;
  arena->blocks_count = 0;
  arena->blocks_capacity = 0;
  atomic_init(&arena->memory_usage, 0);

  return arena;
}

void Arena_Destroy(Lithos_Arena *arena) {
  if (arena == NULL) {
    return;
  }

  for (size_t i = 0; i < arena->blocks_count; i++) {
    free(arena->blocks[i]);
  }

  free(arena->blocks);

  free(arena);
}

static inline char *AlignPtr(char *p) {
  uintptr_t addr = (uintptr_t)p;
  uintptr_t aligned = (addr + (kAlignment - 1)) & ~(uintptr_t)(kAlignment - 1);
  return (char *)aligned;
}

char *Arena_Allocate(Lithos_Arena *arena, size_t bytes) {

  assert(arena != NULL);
  assert(bytes > 0);

  char *aligned = AlignPtr(arena->alloc_ptr);
  size_t padding = (size_t)(aligned - arena->alloc_ptr);

  if (bytes + padding <= arena->alloc_bytes_remaining) {

    char *result = aligned;
    arena->alloc_ptr = result + bytes;
    arena->alloc_bytes_remaining -= (bytes + padding);
    return result;
  }

  return Arena_AllocateFallbackSlow(arena, bytes);
}

char *Arena_AllocateFallbackSlow(Lithos_Arena *arena, size_t bytes) {
  size_t block_bytes;
  if (bytes > kBlockSize / 4) {

    block_bytes = bytes + kAlignment;
    char *block = AllocateFallback(arena, block_bytes);
    if (block == NULL)
      return NULL;
    char *result = AlignPtr(block);
    return result;
  }

  block_bytes = kBlockSize + kAlignment;
  char *new_block = AllocateFallback(arena, block_bytes);
  if (new_block == NULL) {
    return NULL;
  }

  char *aligned = AlignPtr(new_block);
  arena->alloc_ptr =
      aligned + bytes;
  size_t used = (size_t)(arena->alloc_ptr - new_block);
  arena->alloc_bytes_remaining = block_bytes - used;

  return aligned;
}

char *Arena_AllocateAligned(Lithos_Arena *arena, size_t bytes) {

  assert(arena != NULL);
  assert(bytes > 0);

  const size_t align = 8;

  uintptr_t current_mod =
      (uintptr_t)arena->alloc_ptr & (align - 1);

  size_t needed =
      (align - current_mod) & (align - 1);

  size_t total_bytes = needed + bytes;

  char *result;
  if (total_bytes <= arena->alloc_bytes_remaining) {

    result = arena->alloc_ptr + needed;
    arena->alloc_ptr += total_bytes;
    arena->alloc_bytes_remaining -= total_bytes;
  } else {

    result = Arena_AllocateFallbackSlow(arena,
                                        bytes);
    if (result == NULL) {
      return NULL;
    }

    current_mod = (uintptr_t)result & (align - 1);
    if (current_mod != 0) {

      needed = (align - current_mod) & (align - 1);
      result = Arena_Allocate(arena, bytes + needed) +
               needed;
    }
  }

  assert(((uintptr_t)result & (align - 1)) == 0);

  return result;
}

size_t Arena_MemoryUsage(Lithos_Arena *arena) {
  assert(arena != NULL);

  return atomic_load_explicit(&arena->memory_usage, memory_order_relaxed);
}
