
#ifndef LITHOS_CORE_MEMTABLE_H
#define LITHOS_CORE_MEMTABLE_H

#include "core/dbformat.h"
#include "util/slice.h"
#include "util/status.h"
#include <stdbool.h>
#include <stddef.h>

typedef struct Lithos_MemTable Lithos_MemTable;
typedef struct Lithos_Iterator Lithos_Iterator;

Lithos_MemTable *MemTable_Create(void);

void MemTable_Ref(Lithos_MemTable *mem);

void MemTable_Unref(Lithos_MemTable *mem);

void MemTable_Add(Lithos_MemTable *mem, SequenceNumber seq, ValueType type,
                  Lithos_Slice key, Lithos_Slice value);

bool MemTable_Get(Lithos_MemTable *mem, Lithos_Slice key,
                  SequenceNumber snapshot_seq, char **value_out, Status *s);

size_t MemTable_ApproximateMemoryUsage(const Lithos_MemTable *mem);

Lithos_Iterator *MemTable_NewIterator(Lithos_MemTable *mem);

#endif
