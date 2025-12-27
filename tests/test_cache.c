
#include "all_tests.h"
#include "lithos/cache.h"
#include "testharness.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int deleter_call_count = 0;
static void TestDeleter(const Lithos_Slice *key, void *value) {
  (void)key;
  free(value);
  deleter_call_count++;
}

static Lithos_Slice MakeSlice(const char *str) {
  Lithos_Slice s = {str, strlen(str)};
  return s;
}

static void Test_Cache_Basic(void) {
  printf("[TEST] Cache Basic Operations                  ");

  Lithos_Cache *cache = NewLRUCache(1000);
  ASSERT_TRUE(cache != NULL);

  int *val1 = malloc(sizeof(int));
  *val1 = 100;

  Lithos_CacheHandle *h1 =
      Cache_Insert(cache, MakeSlice("key1"), val1, 10, TestDeleter);
  ASSERT_TRUE(h1 != NULL);
  ASSERT_TRUE(Cache_Value(h1) == val1);
  ASSERT_TRUE(*(int *)Cache_Value(h1) == 100);

  Lithos_CacheHandle *h2 = Cache_Lookup(cache, MakeSlice("key1"));
  ASSERT_TRUE(h2 != NULL);
  ASSERT_TRUE(Cache_Value(h2) == val1);

  Cache_Release(cache, h1);
  Cache_Release(cache, h2);

  Lithos_CacheHandle *h3 = Cache_Lookup(cache, MakeSlice("key1"));
  ASSERT_TRUE(h3 != NULL);
  Cache_Release(cache, h3);

  Lithos_CacheHandle *h4 = Cache_Lookup(cache, MakeSlice("key2"));
  ASSERT_TRUE(h4 == NULL);

  Cache_Destroy(cache);

  printf("✓ (%d assertions)\n", test_passed);
}

static void Test_Cache_LRUEviction(void) {
  printf("[TEST] Cache LRU Eviction                        ");

  deleter_call_count = 0;

  Lithos_Cache *cache = NewLRUCache(32);
  ASSERT_TRUE(cache != NULL);

  for (int i = 0; i < 10; i++) {
    char key[16];
    snprintf(key, sizeof(key), "key%d", i);
    int *val = malloc(sizeof(int));
    *val = i;
    Lithos_CacheHandle *h =
        Cache_Insert(cache, MakeSlice(key), val, 10, TestDeleter);
    Cache_Release(cache, h);
  }

  ASSERT_TRUE(deleter_call_count > 0);

  Lithos_CacheHandle *h9 = Cache_Lookup(cache, MakeSlice("key9"));
  ASSERT_TRUE(h9 != NULL);
  Cache_Release(cache, h9);

  Cache_Destroy(cache);

  printf("✓ (%d assertions)\n", test_passed);
}

static void Test_Cache_RefCounting(void) {
  printf("[TEST] Cache Reference Counting                ");

  deleter_call_count = 0;

  Lithos_Cache *cache = NewLRUCache(100);
  ASSERT_TRUE(cache != NULL);

  int *valA = malloc(sizeof(int));
  *valA = 1;
  Lithos_CacheHandle *hA =
      Cache_Insert(cache, MakeSlice("A"), valA, 100, TestDeleter);

  Lithos_CacheHandle *hA2 = Cache_Lookup(cache, MakeSlice("A"));
  ASSERT_TRUE(hA2 != NULL);
  ASSERT_TRUE(Cache_Value(hA2) == valA);

  int *valB = malloc(sizeof(int));
  *valB = 2;
  Lithos_CacheHandle *hB =
      Cache_Insert(cache, MakeSlice("B"), valB, 150, TestDeleter);
  Cache_Release(cache, hB);

  ASSERT_TRUE(deleter_call_count == 0);

  ASSERT_TRUE(*(int *)Cache_Value(hA) == 1);
  ASSERT_TRUE(*(int *)Cache_Value(hA2) == 1);

  Cache_Release(cache, hA);
  ASSERT_TRUE(deleter_call_count == 0);

  Cache_Release(cache, hA2);
  ASSERT_TRUE(deleter_call_count == 0);

  Cache_Erase(cache, MakeSlice("A"));
  ASSERT_TRUE(deleter_call_count == 1);

  Lithos_CacheHandle *hA3 = Cache_Lookup(cache, MakeSlice("A"));
  ASSERT_TRUE(hA3 == NULL);

  Cache_Destroy(cache);

  printf("✓ (%d assertions)\n", test_passed);
}

static void Test_Cache_Erase(void) {
  printf("[TEST] Cache Erase                             ");

  deleter_call_count = 0;

  Lithos_Cache *cache = NewLRUCache(1000);
  ASSERT_TRUE(cache != NULL);

  int *val = malloc(sizeof(int));
  *val = 100;
  Lithos_CacheHandle *h =
      Cache_Insert(cache, MakeSlice("key1"), val, 10, TestDeleter);
  Cache_Release(cache, h);

  ASSERT_TRUE(deleter_call_count == 0);

  Cache_Erase(cache, MakeSlice("key1"));
  ASSERT_TRUE(deleter_call_count == 1);

  Lithos_CacheHandle *h2 = Cache_Lookup(cache, MakeSlice("key1"));
  ASSERT_TRUE(h2 == NULL);

  Cache_Destroy(cache);

  printf("✓ (%d assertions)\n", test_passed);
}

static void Test_Cache_Collisions(void) {
  printf("[TEST] Cache Hash Collisions                   ");

  Lithos_Cache *cache = NewLRUCache(10000);
  ASSERT_TRUE(cache != NULL);

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

static void Test_Cache_NewId(void) {
  printf("[TEST] Cache ID Generation                     ");

  Lithos_Cache *cache = NewLRUCache(1000);
  ASSERT_TRUE(cache != NULL);

  uint64_t id1 = Cache_NewId(cache);
  uint64_t id2 = Cache_NewId(cache);
  uint64_t id3 = Cache_NewId(cache);

  ASSERT_TRUE(id1 != id2);
  ASSERT_TRUE(id2 != id3);
  ASSERT_TRUE(id1 < id2);
  ASSERT_TRUE(id2 < id3);

  Cache_Destroy(cache);

  printf("✓ (%d assertions)\n", test_passed);
}

static void Test_Cache_TotalCharge(void) {
  printf("[TEST] Cache Total Charge                      ");

  Lithos_Cache *cache = NewLRUCache(1000);
  ASSERT_TRUE(cache != NULL);

  size_t initial_charge = Cache_TotalCharge(cache);
  ASSERT_TRUE(initial_charge == 0);

  int *val1 = malloc(sizeof(int));
  *val1 = 1;
  Lithos_CacheHandle *h1 =
      Cache_Insert(cache, MakeSlice("key1"), val1, 100, TestDeleter);
  Cache_Release(cache, h1);

  size_t charge1 = Cache_TotalCharge(cache);
  ASSERT_TRUE(charge1 == 100);

  int *val2 = malloc(sizeof(int));
  *val2 = 2;
  Lithos_CacheHandle *h2 =
      Cache_Insert(cache, MakeSlice("key2"), val2, 50, TestDeleter);
  Cache_Release(cache, h2);

  size_t charge2 = Cache_TotalCharge(cache);
  ASSERT_TRUE(charge2 == 150);

  Cache_Erase(cache, MakeSlice("key1"));
  size_t charge3 = Cache_TotalCharge(cache);
  ASSERT_TRUE(charge3 == 50);

  Cache_Destroy(cache);

  printf("✓ (%d assertions)\n", test_passed);
}

void Run_CacheTests(void) {
  Test_Cache_Basic();
  Test_Cache_LRUEviction();
  Test_Cache_RefCounting();
  Test_Cache_Erase();
  Test_Cache_Collisions();
  Test_Cache_NewId();
  Test_Cache_TotalCharge();
}
