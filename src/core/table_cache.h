
#ifndef LITHOS_CORE_TABLE_CACHE_H
#define LITHOS_CORE_TABLE_CACHE_H

#include "core/version_edit.h"
#include "lithos/iterator.h"
#include "lithos/options.h"
#include "util/slice.h"
#include "util/status.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TableCache TableCache;

TableCache *TableCache_Create(const char *dbname, size_t entries);
void TableCache_Destroy(TableCache *cache);

Status TableCache_Get(TableCache *cache, FileMetaData *f,
                      Lithos_Slice internal_key, Lithos_Slice *value_out,
                      bool *found, bool *deleted);

Lithos_Iterator *TableCache_NewIterator(TableCache *cache, FileMetaData *f,
                                        const Lithos_Options *options);

#ifdef __cplusplus
}
#endif

#endif
