
#ifndef LITHOS_CORE_TABLE_BLOCK_H_
#define LITHOS_CORE_TABLE_BLOCK_H_

#include "core/dbformat.h"
#include "core/skiplist.h"
#include "lithos/iterator.h"
#include "util/slice.h"
#include "util/status.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  const char *data;
  size_t size;
  bool heap_allocated;
} Lithos_BlockContents;

typedef struct Lithos_Block Lithos_Block;

Lithos_Block *Block_Create(Lithos_BlockContents contents);

void Block_Destroy(Lithos_Block *block);

Lithos_Iterator *Block_NewIterator(Lithos_Block *block, Lithos_Comparator cmp);

uint32_t Block_GetRestartCount(const Lithos_Block *block);

#ifdef __cplusplus
}
#endif

#endif
