/* TableCache backed by shared LRU Cache of open Table readers. */

#include "core/table_cache.h"
#include "core/dbformat.h"
#include "core/table/table.h"
#include "lithos/cache.h"
#include "util/coding.h"
#include "util/env.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  Lithos_Table *table;
} TableCacheEntry;

struct TableCache {
  char *dbname;
  Lithos_Cache *cache;
  Lithos_Options options; /* Options used when opening tables */
};

static void TableCache_Deleter(const Lithos_Slice *key, void *value) {
  (void)key;
  TableCacheEntry *entry = (TableCacheEntry *)value;
  if (entry) {
    if (entry->table) {
      Table_Destroy(entry->table);
    }
    free(entry);
  }
}

static Lithos_Slice CacheKey(uint64_t file_number, char *buf) {
  EncodeFixed64(buf, file_number);
  return Slice_Create(buf, sizeof(uint64_t));
}

static char *SSTFileName(const char *dbname, uint64_t number) {
  size_t needed = (size_t)snprintf(NULL, 0, "%s/%06llu.sst", dbname,
                                   (unsigned long long)number) +
                  1;
  char *buf = malloc(needed);
  if (buf == NULL)
    return NULL;
  snprintf(buf, needed, "%s/%06llu.sst", dbname, (unsigned long long)number);
  return buf;
}

static TableCacheEntry *TableCache_Open(TableCache *cache, FileMetaData *f) {
  if (cache == NULL || f == NULL)
    return NULL;
  char *fname = SSTFileName(cache->dbname, f->number);
  if (fname == NULL)
    return NULL;

  Lithos_RandomAccessFile *file = NULL;
  Status s = Env_NewRandomAccessFile(fname, &file);
  free(fname);
  if (!Status_IsOK(s))
    return NULL;

  Lithos_Table *table = NULL;
  s = Table_Open(&cache->options, file, f->file_size, &table);
  if (!Status_IsOK(s)) {
    return NULL;
  }

  TableCacheEntry *entry = calloc(1, sizeof(TableCacheEntry));
  if (entry == NULL) {
    Table_Destroy(table);
    return NULL;
  }
  entry->table = table;
  return entry;
}

TableCache *TableCache_Create(const char *dbname, size_t entries) {
  TableCache *c = calloc(1, sizeof(TableCache));
  if (c == NULL)
    return NULL;
  if (dbname) {
    size_t n = strlen(dbname);
    c->dbname = malloc(n + 1);
    if (c->dbname)
      memcpy(c->dbname, dbname, n + 1);
  }
  c->cache = NewLRUCache(entries > 0 ? entries : 1024);
  Lithos_Options_InitDefault(&c->options);
  return c;
}

void TableCache_Destroy(TableCache *cache) {
  if (cache == NULL)
    return;
  if (cache->cache) {
    Cache_Destroy(cache->cache);
  }
  free(cache->dbname);
  free(cache);
}

typedef struct {
  Lithos_Slice *value_out;
  bool *found;
  bool *deleted;
} TableSaverCtx;

static void TableCache_Saver(void *arg, Lithos_Slice ikey, Lithos_Slice val) {
  TableSaverCtx *ctx = (TableSaverCtx *)arg;
  if (ctx == NULL)
    return;
  ParsedInternalKey pik;
  if (!ParseInternalKey(ikey, &pik))
    return;

  if (pik.type == kTypeDeletion) {
    if (ctx->deleted)
      *ctx->deleted = true;
    if (ctx->found)
      *ctx->found = false;
  } else {
    if (ctx->found)
      *ctx->found = true;
    if (ctx->deleted)
      *ctx->deleted = false;
    if (ctx->value_out)
      *ctx->value_out = val;
  }
}

Status TableCache_Get(TableCache *cache, FileMetaData *f,
                      Lithos_Slice internal_key, Lithos_Slice *value_out,
                      bool *found, bool *deleted) {
  if (value_out)
    *value_out = Slice_Empty();
  if (found)
    *found = false;
  if (deleted)
    *deleted = false;
  if (cache == NULL || f == NULL)
    return Status_InvalidArgument("table_cache_get");

  char key_buf[sizeof(uint64_t)];
  Lithos_Slice key = CacheKey(f->number, key_buf);
  Lithos_CacheHandle *handle = Cache_Lookup(cache->cache, key);
  if (handle == NULL) {
    TableCacheEntry *entry = TableCache_Open(cache, f);
    if (entry == NULL) {
      return Status_IOError("open table", NULL);
    }
    handle = Cache_Insert(cache->cache, key, entry, sizeof(TableCacheEntry),
                          TableCache_Deleter);
  }

  TableCacheEntry *entry = (TableCacheEntry *)Cache_Value(handle);
  if (entry == NULL || entry->table == NULL) {
    Cache_Release(cache->cache, handle);
    return Status_Corruption("null table cache entry", NULL);
  }

  TableSaverCtx ctx = {value_out, found, deleted};
  Status s =
      Table_InternalGet(entry->table, internal_key, &ctx, TableCache_Saver);

  Cache_Release(cache->cache, handle);
  return s;
}

typedef struct {
  Lithos_Iterator *child;
  Lithos_Cache *cache;
  Lithos_CacheHandle *handle;
} CachedIterator;

static bool Cached_Valid(void *state) {
  CachedIterator *c = (CachedIterator *)state;
  return Lithos_Iter_Valid(c->child);
}
static void Cached_SeekToFirst(void *state) {
  CachedIterator *c = (CachedIterator *)state;
  Lithos_Iter_SeekToFirst(c->child);
}
static void Cached_SeekToLast(void *state) {
  CachedIterator *c = (CachedIterator *)state;
  Lithos_Iter_SeekToLast(c->child);
}
static void Cached_Seek(void *state, Lithos_Slice target) {
  CachedIterator *c = (CachedIterator *)state;
  Lithos_Iter_Seek(c->child, target);
}
static void Cached_Next(void *state) {
  CachedIterator *c = (CachedIterator *)state;
  Lithos_Iter_Next(c->child);
}
static void Cached_Prev(void *state) {
  CachedIterator *c = (CachedIterator *)state;
  Lithos_Iter_Prev(c->child);
}
static Lithos_Slice Cached_Key(void *state) {
  CachedIterator *c = (CachedIterator *)state;
  return Lithos_Iter_Key(c->child);
}
static Lithos_Slice Cached_Value(void *state) {
  CachedIterator *c = (CachedIterator *)state;
  return Lithos_Iter_Value(c->child);
}
static Status Cached_Status(void *state) {
  CachedIterator *c = (CachedIterator *)state;
  return Lithos_Iter_GetStatus(c->child);
}
static void Cached_Cleanup(void *state) {
  CachedIterator *c = (CachedIterator *)state;
  Lithos_Iter_Destroy(c->child);
  Cache_Release(c->cache, c->handle);
  free(c);
}

static const Lithos_IteratorVTable kCachedVTable = {
    .Valid = Cached_Valid,
    .SeekToFirst = Cached_SeekToFirst,
    .SeekToLast = Cached_SeekToLast,
    .Seek = Cached_Seek,
    .Next = Cached_Next,
    .Prev = Cached_Prev,
    .Key = Cached_Key,
    .Value = Cached_Value,
    .GetStatus = Cached_Status,
    .Cleanup = Cached_Cleanup,
};

Lithos_Iterator *TableCache_NewIterator(TableCache *cache, FileMetaData *f,
                                        const Lithos_Options *options) {
  if (cache == NULL || f == NULL)
    return NULL;

  char key_buf[sizeof(uint64_t)];
  Lithos_Slice key = CacheKey(f->number, key_buf);
  Lithos_CacheHandle *handle = Cache_Lookup(cache->cache, key);
  if (handle == NULL) {
    TableCacheEntry *entry = TableCache_Open(cache, f);
    if (entry == NULL)
      return NULL;
    handle = Cache_Insert(cache->cache, key, entry, sizeof(TableCacheEntry),
                          TableCache_Deleter);
  }

  TableCacheEntry *entry = (TableCacheEntry *)Cache_Value(handle);
  if (entry == NULL || entry->table == NULL) {
    Cache_Release(cache->cache, handle);
    return NULL;
  }

  Lithos_Iterator *child =
      Table_NewIterator(entry->table, options ? options : &cache->options);
  if (child == NULL) {
    Cache_Release(cache->cache, handle);
    return NULL;
  }

  CachedIterator *ci = calloc(1, sizeof(CachedIterator));
  if (ci == NULL) {
    Lithos_Iter_Destroy(child);
    Cache_Release(cache->cache, handle);
    return NULL;
  }
  ci->child = child;
  ci->cache = cache->cache;
  ci->handle = handle;

  Lithos_Iterator *it = malloc(sizeof(Lithos_Iterator));
  if (it == NULL) {
    Cached_Cleanup(ci);
    return NULL;
  }
  it->vtable = &kCachedVTable;
  it->state = ci;
  return it;
}
