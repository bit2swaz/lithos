
#ifndef LITHOS_CACHE_H
#define LITHOS_CACHE_H

#include "util/slice.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Lithos_Cache Lithos_Cache;
typedef struct Lithos_CacheHandle Lithos_CacheHandle;

typedef void (*CacheDeleter)(const Lithos_Slice *key, void *value);

Lithos_Cache *NewLRUCache(size_t capacity);

void Cache_Destroy(Lithos_Cache *cache);

Lithos_CacheHandle *Cache_Insert(Lithos_Cache *cache, Lithos_Slice key,
                                 void *value, size_t charge,
                                 CacheDeleter deleter);

Lithos_CacheHandle *Cache_Lookup(Lithos_Cache *cache, Lithos_Slice key);

void Cache_Release(Lithos_Cache *cache, Lithos_CacheHandle *handle);

void *Cache_Value(Lithos_CacheHandle *handle);

void Cache_Erase(Lithos_Cache *cache, Lithos_Slice key);

uint64_t Cache_NewId(Lithos_Cache *cache);

size_t Cache_TotalCharge(Lithos_Cache *cache);

#ifdef __cplusplus
}
#endif

#endif
