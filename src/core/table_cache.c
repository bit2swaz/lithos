/* Stub TableCache implementation. */

#include "core/table_cache.h"
#include <stdlib.h>

struct TableCache {
    int dummy;
};

TableCache* TableCache_Create(const char* dbname, size_t entries) {
    (void)dbname;
    (void)entries;
    TableCache* c = calloc(1, sizeof(TableCache));
    return c;
}

void TableCache_Destroy(TableCache* cache) {
    free(cache);
}

Status TableCache_Get(TableCache* cache,
                      FileMetaData* f,
                      Lithos_Slice internal_key,
                      Lithos_Slice* value_out,
                      bool* found,
                      bool* deleted) {
    (void)cache;
    (void)f;
    (void)internal_key;
    if (value_out) {
        value_out->data = NULL;
        value_out->size = 0;
    }
    if (found) *found = false;
    if (deleted) *deleted = false;
    return Status_NotFound(NULL);
}
