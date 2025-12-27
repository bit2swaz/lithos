
#include "lithos/cache.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#define NUM_SHARDS 16

#if defined(__GNUC__) && __GNUC__ >= 7
#define LITHOS_FALLTHROUGH __attribute__((fallthrough))
#else
#define LITHOS_FALLTHROUGH ((void)0)
#endif

static uint32_t MurmurHash(const void *key, size_t len, uint32_t seed) {
  const uint32_t m = 0x5bd1e995;
  const int r = 24;
  uint32_t h = seed ^ len;

  const unsigned char *data = (const unsigned char *)key;
  while (len >= 4) {
    uint32_t k = *(uint32_t *)data;
    k *= m;
    k ^= k >> r;
    k *= m;
    h *= m;
    h ^= k;
    data += 4;
    len -= 4;
  }

  switch (len) {
  case 3:
    h ^= data[2] << 16;
    LITHOS_FALLTHROUGH;
  case 2:
    h ^= data[1] << 8;
    LITHOS_FALLTHROUGH;
  case 1:
    h ^= data[0];
    h *= m;
  }

  h ^= h >> 13;
  h *= m;
  h ^= h >> 15;

  return h;
}

static uint32_t HashSlice(Lithos_Slice key) {
  return MurmurHash(key.data, key.size, 0);
}

typedef struct LRUHandle {
  void *value;
  CacheDeleter deleter;
  struct LRUHandle *next_hash;
  struct LRUHandle *next;
  struct LRUHandle *prev;
  size_t charge;
  size_t key_length;
  uint32_t refs;
  uint32_t hash;
  char key_data[1];
} LRUHandle;

static Lithos_Slice Handle_Key(const LRUHandle *h) {
  Lithos_Slice s = {h->key_data, h->key_length};
  return s;
}

typedef struct HandleTable {
  LRUHandle **list;
  uint32_t length;
  uint32_t elems;
} HandleTable;

static HandleTable *HandleTable_Create(void) {
  HandleTable *table = malloc(sizeof(HandleTable));
  table->length = 16;
  table->elems = 0;
  table->list = calloc(table->length, sizeof(LRUHandle *));
  return table;
}

static void HandleTable_Destroy(HandleTable *table) {
  free(table->list);
  free(table);
}

static LRUHandle *HandleTable_Lookup(HandleTable *table, Lithos_Slice key,
                                     uint32_t hash) {
  LRUHandle **ptr = &table->list[hash & (table->length - 1)];
  while (*ptr != NULL &&
         ((*ptr)->hash != hash || !Slice_Equal(Handle_Key(*ptr), key))) {
    ptr = &(*ptr)->next_hash;
  }
  return *ptr;
}

static LRUHandle *HandleTable_Remove(HandleTable *table, Lithos_Slice key,
                                     uint32_t hash) {
  LRUHandle **ptr = &table->list[hash & (table->length - 1)];
  LRUHandle *result = NULL;
  while (*ptr != NULL &&
         ((*ptr)->hash != hash || !Slice_Equal(Handle_Key(*ptr), key))) {
    ptr = &(*ptr)->next_hash;
  }
  if (*ptr != NULL) {
    result = *ptr;
    *ptr = result->next_hash;
    table->elems--;
  }
  return result;
}

static void HandleTable_Resize(HandleTable *table) {
  uint32_t new_length = table->length * 2;
  LRUHandle **new_list = calloc(new_length, sizeof(LRUHandle *));

  uint32_t count = 0;
  for (uint32_t i = 0; i < table->length; i++) {
    LRUHandle *h = table->list[i];
    while (h != NULL) {
      LRUHandle *next = h->next_hash;
      uint32_t hash = h->hash;
      LRUHandle **ptr = &new_list[hash & (new_length - 1)];
      h->next_hash = *ptr;
      *ptr = h;
      h = next;
      count++;
    }
  }

  assert(count == table->elems);
  free(table->list);
  table->list = new_list;
  table->length = new_length;
}

static LRUHandle *HandleTable_Insert(HandleTable *table, LRUHandle *h) {
  LRUHandle **ptr = &table->list[h->hash & (table->length - 1)];
  LRUHandle *old = NULL;

  while (*ptr != NULL && ((*ptr)->hash != h->hash ||
                          !Slice_Equal(Handle_Key(*ptr), Handle_Key(h)))) {
    ptr = &(*ptr)->next_hash;
  }

  if (*ptr != NULL) {

    old = *ptr;
    h->next_hash = old->next_hash;
    *ptr = h;
  } else {

    h->next_hash = table->list[h->hash & (table->length - 1)];
    table->list[h->hash & (table->length - 1)] = h;
    table->elems++;

    if (table->elems > table->length) {
      HandleTable_Resize(table);
    }
  }

  return old;
}

typedef struct LRUCache {
  pthread_mutex_t mutex;
  size_t capacity;
  size_t usage;

  LRUHandle lru;

  HandleTable *table;
} LRUCache;

static LRUCache *LRUCache_Create(size_t capacity) {
  LRUCache *cache = malloc(sizeof(LRUCache));
  pthread_mutex_init(&cache->mutex, NULL);
  cache->capacity = capacity;
  cache->usage = 0;
  cache->lru.next = &cache->lru;
  cache->lru.prev = &cache->lru;
  cache->table = HandleTable_Create();
  return cache;
}

static void LRU_Remove(LRUHandle *e) {
  e->next->prev = e->prev;
  e->prev->next = e->next;
}

static void LRU_Append(LRUHandle *list, LRUHandle *e) {
  e->next = list;
  e->prev = list->prev;
  e->prev->next = e;
  e->next->prev = e;
}

static void LRUCache_FinishErase(LRUCache *cache, LRUHandle *e) {
  if (e != NULL) {
    LRU_Remove(e);
    cache->usage -= e->charge;

    assert(e->refs > 0);
    e->refs--;
    if (e->refs == 0) {
      Lithos_Slice key = Handle_Key(e);
      if (e->deleter) {
        e->deleter(&key, e->value);
      }
      free(e);
    }
  }
}

static Lithos_CacheHandle *LRUCache_Lookup(LRUCache *cache, Lithos_Slice key,
                                           uint32_t hash) {
  pthread_mutex_lock(&cache->mutex);
  LRUHandle *e = HandleTable_Lookup(cache->table, key, hash);
  if (e != NULL) {
    e->refs++;

    LRU_Remove(e);
    LRU_Append(&cache->lru, e);
  }
  pthread_mutex_unlock(&cache->mutex);
  return (Lithos_CacheHandle *)e;
}

static Lithos_CacheHandle *LRUCache_Insert(LRUCache *cache, Lithos_Slice key,
                                           uint32_t hash, void *value,
                                           size_t charge,
                                           CacheDeleter deleter) {
  pthread_mutex_lock(&cache->mutex);

  LRUHandle *e = malloc(sizeof(LRUHandle) - 1 + key.size);
  e->value = value;
  e->deleter = deleter;
  e->charge = charge;
  e->key_length = key.size;
  e->hash = hash;
  e->refs = 2;
  memcpy(e->key_data, key.data, key.size);

  cache->usage += charge;

  LRUHandle *old = HandleTable_Insert(cache->table, e);

  LRU_Append(&cache->lru, e);

  if (old != NULL) {
    LRUCache_FinishErase(cache, old);
  }

  while (cache->usage > cache->capacity && cache->lru.next != &cache->lru) {
    LRUHandle *old_entry = cache->lru.next;
    if (old_entry->refs == 1) {

      LRUHandle *removed = HandleTable_Remove(
          cache->table, Handle_Key(old_entry), old_entry->hash);
      assert(removed == old_entry);
      LRUCache_FinishErase(cache, old_entry);
    } else {

      break;
    }
  }

  pthread_mutex_unlock(&cache->mutex);
  return (Lithos_CacheHandle *)e;
}

static void LRUCache_Release(LRUCache *cache, Lithos_CacheHandle *handle) {
  pthread_mutex_lock(&cache->mutex);
  LRUHandle *e = (LRUHandle *)handle;
  assert(e->refs > 0);
  e->refs--;

  pthread_mutex_unlock(&cache->mutex);
}

static void LRUCache_Erase(LRUCache *cache, Lithos_Slice key, uint32_t hash) {
  pthread_mutex_lock(&cache->mutex);
  LRUHandle *e = HandleTable_Remove(cache->table, key, hash);
  LRUCache_FinishErase(cache, e);
  pthread_mutex_unlock(&cache->mutex);
}

static size_t LRUCache_TotalCharge(LRUCache *cache) {
  pthread_mutex_lock(&cache->mutex);
  size_t total = cache->usage;
  pthread_mutex_unlock(&cache->mutex);
  return total;
}

static void LRUCache_Destroy(LRUCache *cache) {

  for (LRUHandle *e = cache->lru.next; e != &cache->lru;) {
    LRUHandle *next = e->next;
    assert(e->refs == 1);

    Lithos_Slice key = Handle_Key(e);
    if (e->deleter) {
      e->deleter(&key, e->value);
    }
    free(e);
    e = next;
  }
  HandleTable_Destroy(cache->table);
  pthread_mutex_destroy(&cache->mutex);
  free(cache);
}

typedef struct ShardedLRUCache {
  LRUCache *shards[NUM_SHARDS];
  atomic_uint_fast64_t last_id;
} ShardedLRUCache;

static uint32_t Shard(uint32_t hash) { return hash % NUM_SHARDS; }

Lithos_Cache *NewLRUCache(size_t capacity) {
  ShardedLRUCache *cache = malloc(sizeof(ShardedLRUCache));
  const size_t per_shard = (capacity + (NUM_SHARDS - 1)) / NUM_SHARDS;

  for (int i = 0; i < NUM_SHARDS; i++) {
    cache->shards[i] = LRUCache_Create(per_shard);
  }

  atomic_init(&cache->last_id, 0);

  return (Lithos_Cache *)cache;
}

void Cache_Destroy(Lithos_Cache *cache) {
  ShardedLRUCache *c = (ShardedLRUCache *)cache;
  for (int i = 0; i < NUM_SHARDS; i++) {
    LRUCache_Destroy(c->shards[i]);
  }
  free(c);
}

Lithos_CacheHandle *Cache_Insert(Lithos_Cache *cache, Lithos_Slice key,
                                 void *value, size_t charge,
                                 CacheDeleter deleter) {
  ShardedLRUCache *c = (ShardedLRUCache *)cache;
  const uint32_t hash = HashSlice(key);
  return LRUCache_Insert(c->shards[Shard(hash)], key, hash, value, charge,
                         deleter);
}

Lithos_CacheHandle *Cache_Lookup(Lithos_Cache *cache, Lithos_Slice key) {
  ShardedLRUCache *c = (ShardedLRUCache *)cache;
  const uint32_t hash = HashSlice(key);
  return LRUCache_Lookup(c->shards[Shard(hash)], key, hash);
}

void Cache_Release(Lithos_Cache *cache, Lithos_CacheHandle *handle) {
  if (handle == NULL)
    return;

  ShardedLRUCache *c = (ShardedLRUCache *)cache;
  LRUHandle *h = (LRUHandle *)handle;
  LRUCache_Release(c->shards[Shard(h->hash)], handle);
}

void *Cache_Value(Lithos_CacheHandle *handle) {
  return ((LRUHandle *)handle)->value;
}

void Cache_Erase(Lithos_Cache *cache, Lithos_Slice key) {
  ShardedLRUCache *c = (ShardedLRUCache *)cache;
  const uint32_t hash = HashSlice(key);
  LRUCache_Erase(c->shards[Shard(hash)], key, hash);
}

uint64_t Cache_NewId(Lithos_Cache *cache) {
  ShardedLRUCache *c = (ShardedLRUCache *)cache;
  return atomic_fetch_add(&c->last_id, 1);
}

size_t Cache_TotalCharge(Lithos_Cache *cache) {
  ShardedLRUCache *c = (ShardedLRUCache *)cache;
  size_t total = 0;
  for (int i = 0; i < NUM_SHARDS; i++) {
    total += LRUCache_TotalCharge(c->shards[i]);
  }
  return total;
}
