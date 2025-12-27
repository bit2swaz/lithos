
#ifndef LITHOS_CORE_TABLE_TABLE_H_
#define LITHOS_CORE_TABLE_TABLE_H_

#include "lithos/iterator.h"
#include "lithos/options.h"
#include "util/env.h"
#include "util/status.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Lithos_Table Lithos_Table;

Status Table_Open(const Lithos_Options *options, Lithos_RandomAccessFile *file,
                  uint64_t file_size, Lithos_Table **table);

void Table_Destroy(Lithos_Table *table);

Lithos_Iterator *Table_NewIterator(Lithos_Table *table,
                                   const Lithos_Options *options);

Status Table_InternalGet(Lithos_Table *table, Lithos_Slice key, void *arg,
                         void (*saver)(void *arg, Lithos_Slice key,
                                       Lithos_Slice value));

#ifdef __cplusplus
}
#endif

#endif
