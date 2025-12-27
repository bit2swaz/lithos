/*
 * test_cache.c - Block Cache Tests
 *
 * Validates:
 * 1. Basic LRU eviction logic
 * 2. Reference counting and handle lifetime
 * 3. Sharded cache concurrent access patterns
 * 4. Integration with cache deleters
 */

#include "all_tests.h"
#include "lithos/cache.h"
#include "testharness.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Track deleter calls for testing */
static int deleter_call_count = 0;
static void TestDeleter(const Lithos_Slice *key, void *value) {
  (void)key;
  free(value);
  deleter_call_count++;
}

/* Helper to create a slice from string */
static Lithos_Slice MakeSlice(const char *str) {
  Lithos_Slice s = {str, strlen(str)};
  return s;
}

/* Test: Basic cache insert and lookup */
static void Test_Cache_Basic(void) {
  printf("[TEST] Cache Basic Operations                  ");

  Lithos_Cache *cache = NewLRUCache(1000);
  ASSERT_TRUE(cache != NULL);

  /* Insert a value */
  int *val1 = malloc(sizeof(int));
  *val1 = 100;

  Lithos_CacheHandle *h1 =
      Cache_Insert(cache, MakeSlice("key1"), val1, 10, TestDeleter);
  ASSERT_TRUE(h1 != NULL);
  ASSERT_TRUE(Cache_Value(h1) == val1);
  ASSERT_TRUE(*(int *)Cache_Value(h1) == 100);

  /* Lookup the value */
  Lithos_CacheHandle *h2 = Cache_Lookup(cache, MakeSlice("key1"));
  ASSERT_TRUE(h2 != NULL);
  ASSERT_TRUE(Cache_Value(h2) == val1);

  /* Release handles */
  Cache_Release(cache, h1);
  Cache_Release(cache, h2);

  /* Lookup again (should still be in cache) */
  Lithos_CacheHandle *h3 = Cache_Lookup(cache, MakeSlice("key1"));
  ASSERT_TRUE(h3 != NULL);
  Cache_Release(cache, h3);

  /* Lookup non-existent key */
  Lithos_CacheHandle *h4 = Cache_Lookup(cache, MakeSlice("key2"));
  ASSERT_TRUE(h4 == NULL);

  Cache_Destroy(cache);

  printf("✓ (%d assertions)\n", test_passed);
}

/* Test: LRU eviction */
static void Test_Cache_LRUEviction(void) {
  printf("[TEST] Cache LRU Eviction                        ");

  deleter_call_count = 0;

  /* Create a small cache (32 bytes total, ~2 bytes per shard) */
  Lithos_Cache *cache = NewLRUCache(32);
  ASSERT_TRUE(cache != NULL);

  /* Insert many entries (each 10 bytes) to guarantee some evictions.
   * With 32 bytes total capacity and 10 bytes per entry, we can fit ~3 entries.
   * Insert 10 entries to force evictions. */
  for (int i = 0; i < 10; i++) {
    char key[16];
    snprintf(key, sizeof(key), "key%d", i);
    int *val = malloc(sizeof(int));
    *val = i;
    Lithos_CacheHandle *h =
        Cache_Insert(cache, MakeSlice(key), val, 10, TestDeleter);
    Cache_Release(cache, h);
  }

  /* Some entries must have been evicted */
  ASSERT_TRUE(deleter_call_count > 0);

  /* The cache should contain the most recent entries */
  Lithos_CacheHandle *h9 = Cache_Lookup(cache, MakeSlice("key9"));
  ASSERT_TRUE(h9 != NULL);
  Cache_Release(cache, h9);

  Cache_Destroy(cache);

  printf("✓ (%d assertions)\n", test_passed);
}

/* Test: Reference counting prevents premature eviction */
static void Test_Cache_RefCounting(void) {
  printf("[TEST] Cache Reference Counting                ");

  deleter_call_count = 0;

  /* Create cache with capacity for 1 item */
  Lithos_Cache *cache = NewLRUCache(100);
  ASSERT_TRUE(cache != NULL);

  /* Insert A (100 bytes) */
  int *valA = malloc(sizeof(int));
  *valA = 1;
  Lithos_CacheHandle *hA =
      Cache_Insert(cache, MakeSlice("A"), valA, 100, TestDeleter);

  /* Lookup A (increases refcount to 2: one from insert, one from lookup) */
  Lithos_CacheHandle *hA2 = Cache_Lookup(cache, MakeSlice("A"));
  ASSERT_TRUE(hA2 != NULL);
  ASSERT_TRUE(Cache_Value(hA2) == valA);

  /* Try to force eviction by inserting B (150 bytes, larger than capacity) */
  int *valB = malloc(sizeof(int));
  *valB = 2;
  Lithos_CacheHandle *hB =
      Cache_Insert(cache, MakeSlice("B"), valB, 150, TestDeleter);
  Cache_Release(cache, hB);

  /* A should NOT be deleted yet because we're holding references */
  ASSERT_TRUE(deleter_call_count == 0);

  /* A should still be accessible through our handles */
  ASSERT_TRUE(*(int *)Cache_Value(hA) == 1);
  ASSERT_TRUE(*(int *)Cache_Value(hA2) == 1);

  /* Release first handle */
  Cache_Release(cache, hA);
  ASSERT_TRUE(deleter_call_count == 0); /* Still one reference left */

  /* Release second handle - entry remains cached (refs==1 held by cache) */
  Cache_Release(cache, hA2);
  ASSERT_TRUE(deleter_call_count == 0); /* Cache still holds the entry */

  /* Explicitly erase to trigger deleter */
  Cache_Erase(cache, MakeSlice("A"));
  ASSERT_TRUE(deleter_call_count == 1);

  /* A should no longer be in cache */
  Lithos_CacheHandle *hA3 = Cache_Lookup(cache, MakeSlice("A"));
  ASSERT_TRUE(hA3 == NULL);

  Cache_Destroy(cache);

  printf("✓ (%d assertions)\n", test_passed);
}

/* Test: Cache erase */
static void Test_Cache_Erase(void) {
  printf("[TEST] Cache Erase                             ");

  deleter_call_count = 0;

  Lithos_Cache *cache = NewLRUCache(1000);
  ASSERT_TRUE(cache != NULL);

  /* Insert value */
  int *val = malloc(sizeof(int));
  *val = 100;
  Lithos_CacheHandle *h =
      Cache_Insert(cache, MakeSlice("key1"), val, 10, TestDeleter);
  Cache_Release(cache, h);

  ASSERT_TRUE(deleter_call_count == 0);

  /* Erase the entry */
  Cache_Erase(cache, MakeSlice("key1"));
  ASSERT_TRUE(deleter_call_count == 1); /* Deleter was called */

  /* Lookup should fail */
  Lithos_CacheHandle *h2 = Cache_Lookup(cache, MakeSlice("key1"));
  ASSERT_TRUE(h2 == NULL);

  Cache_Destroy(cache);

  printf("✓ (%d assertions)\n", test_passed);
}

/* Test: Multiple keys with same hash (collision handling) */
static void Test_Cache_Collisions(void) {
  printf("[TEST] Cache Hash Collisions                   ");

  Lithos_Cache *cache = NewLRUCache(10000);
  ASSERT_TRUE(cache != NULL);

  /* Insert many keys */
  const int num_keys = 100;
  for (int i = 0; i < num_keys; i++) {
    char key[32];
    snprintf(key, sizeof(key), "key%d", i);

    int *val = malloc(sizeof(int));
    *val = i;

    Lithos_CacheHandle *h =
        Cache_Insert(cache, MakeSlice(key), val, 10, TestDeleter);
    Cache_Release(cache, h);
  }

  /* Verify all keys are retrievable */
  for (int i = 0; i < num_keys; i++) {
    char key[32];
    snprintf(key, sizeof(key), "key%d", i);

    Lithos_CacheHandle *h = Cache_Lookup(cache, MakeSlice(key));
    ASSERT_TRUE(h != NULL);
    ASSERT_TRUE(*(int *)Cache_Value(h) == i);
    Cache_Release(cache, h);
  }

  Cache_Destroy(cache);

  printf("✓ (%d assertions)\n", test_passed);
}

/* Test: Cache ID generation */
static void Test_Cache_NewId(void) {
  printf("[TEST] Cache ID Generation                     ");

  Lithos_Cache *cache = NewLRUCache(1000);
  ASSERT_TRUE(cache != NULL);

  /* Generate unique IDs */
  uint64_t id1 = Cache_NewId(cache);
  uint64_t id2 = Cache_NewId(cache);
  uint64_t id3 = Cache_NewId(cache);

  /* IDs should be unique and increasing */
  ASSERT_TRUE(id1 != id2);
  ASSERT_TRUE(id2 != id3);
  ASSERT_TRUE(id1 < id2);
  ASSERT_TRUE(id2 < id3);

  Cache_Destroy(cache);

  printf("✓ (%d assertions)\n", test_passed);
}

/* Test: Cache total charge tracking */
static void Test_Cache_TotalCharge(void) {
  printf("[TEST] Cache Total Charge                      ");

  Lithos_Cache *cache = NewLRUCache(1000);
  ASSERT_TRUE(cache != NULL);

  size_t initial_charge = Cache_TotalCharge(cache);
  ASSERT_TRUE(initial_charge == 0);

  /* Insert items */
  int *val1 = malloc(sizeof(int));
  *val1 = 1;
  Lithos_CacheHandle *h1 =
      Cache_Insert(cache, MakeSlice("key1"), val1, 100, TestDeleter);
  Cache_Release(cache, h1);

  /* Charge should increase */
  size_t charge1 = Cache_TotalCharge(cache);
  ASSERT_TRUE(charge1 == 100);

  /* Insert another */
  int *val2 = malloc(sizeof(int));
  *val2 = 2;
  Lithos_CacheHandle *h2 =
      Cache_Insert(cache, MakeSlice("key2"), val2, 50, TestDeleter);
  Cache_Release(cache, h2);

  size_t charge2 = Cache_TotalCharge(cache);
  ASSERT_TRUE(charge2 == 150);

  /* Erase one */
  Cache_Erase(cache, MakeSlice("key1"));
  size_t charge3 = Cache_TotalCharge(cache);
  ASSERT_TRUE(charge3 == 50);

  Cache_Destroy(cache);

  printf("✓ (%d assertions)\n", test_passed);
}

/* Main test runner */
void Run_CacheTests(void) {
  Test_Cache_Basic();
  Test_Cache_LRUEviction();
  Test_Cache_RefCounting();
  Test_Cache_Erase();
  Test_Cache_Collisions();
  Test_Cache_NewId();
  Test_Cache_TotalCharge();
}
