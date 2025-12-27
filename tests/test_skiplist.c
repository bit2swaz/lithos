#define _POSIX_C_SOURCE 200809L

#include "core/skiplist.h"
#include "testharness.h"
#include "util/arena.h"
#include <stdlib.h>
#include <string.h>

void Run_SkipListTests(void);

static int StringComparator(const void *a, const void *b) {
  return strcmp((const char *)a, (const char *)b);
}

static void Test_SkipList(void) {
  printf(COLOR_BLUE "[Test] SkipList Basic Operations\n" COLOR_RESET);

  Lithos_Arena *arena = Arena_Create();
  ASSERT_TRUE(arena != NULL);

  Lithos_SkipList *list = SkipList_Create(StringComparator, arena);
  ASSERT_TRUE(list != NULL);

  printf("  Inserting: '1', '5', '3', '9', '7'\n");

  char *key1 = strdup("1");
  char *key3 = strdup("3");
  char *key5 = strdup("5");
  char *key7 = strdup("7");
  char *key9 = strdup("9");

  SkipList_Insert(list, key1);
  SkipList_Insert(list, key5);
  SkipList_Insert(list, key3);
  SkipList_Insert(list, key9);
  SkipList_Insert(list, key7);

  printf("  Checking Contains('5'): ");
  if (SkipList_Contains(list, "5")) {
    printf(COLOR_GREEN "FOUND\n" COLOR_RESET);
  } else {
    printf(COLOR_RED "NOT FOUND\n" COLOR_RESET);
    ASSERT_TRUE(false);
  }

  printf("  Checking Contains('3'): ");
  if (SkipList_Contains(list, "3")) {
    printf(COLOR_GREEN "FOUND\n" COLOR_RESET);
  } else {
    printf(COLOR_RED "NOT FOUND\n" COLOR_RESET);
    ASSERT_TRUE(false);
  }

  printf("  Checking Contains('2'): ");
  if (!SkipList_Contains(list, "2")) {
    printf(COLOR_GREEN "NOT FOUND (correct)\n" COLOR_RESET);
  } else {
    printf(COLOR_RED "FOUND (incorrect)\n" COLOR_RESET);
    ASSERT_TRUE(false);
  }

  printf("  Checking Contains('8'): ");
  if (!SkipList_Contains(list, "8")) {
    printf(COLOR_GREEN "NOT FOUND (correct)\n" COLOR_RESET);
  } else {
    printf(COLOR_RED "FOUND (incorrect)\n" COLOR_RESET);
    ASSERT_TRUE(false);
  }

  printf("  Iterating from first:\n");
  Lithos_Iterator *iter = SkipList_NewIterator(list);
  ASSERT_TRUE(iter != NULL);

  Iter_SeekToFirst(iter);

  const char *expected[] = {"1", "3", "5", "7", "9"};
  int i = 0;

  printf("    ");
  while (Iter_Valid(iter)) {
    const char *key = (const char *)Iter_Key(iter);
    printf("%s ", key);

    if (strcmp(key, expected[i]) != 0) {
      printf(COLOR_RED "\n    ERROR: Expected '%s', got '%s'\n" COLOR_RESET,
             expected[i], key);
      ASSERT_TRUE(false);
    }

    Iter_Next(iter);
    i++;
  }
  printf("\n");

  if (i == 5) {
    printf("    " COLOR_GREEN "Order verified\n" COLOR_RESET);
  } else {
    printf("    " COLOR_RED "ERROR: Expected 5 keys, got %d\n" COLOR_RESET, i);
    ASSERT_TRUE(false);
  }

  printf("  Seeking to '5':\n");
  Iter_Seek(iter, "5");
  if (Iter_Valid(iter)) {
    const char *key = (const char *)Iter_Key(iter);
    if (strcmp(key, "5") == 0) {
      printf("    " COLOR_GREEN "Found '5'\n" COLOR_RESET);
    } else {
      printf("    " COLOR_RED "ERROR: Expected '5', got '%s'\n" COLOR_RESET,
             key);
      ASSERT_TRUE(false);
    }
  } else {
    printf("    " COLOR_RED "ERROR: Iterator invalid after Seek\n" COLOR_RESET);
    ASSERT_TRUE(false);
  }

  printf("  Seeking to '4' (not present, should find '5'):\n");
  Iter_Seek(iter, "4");
  if (Iter_Valid(iter)) {
    const char *key = (const char *)Iter_Key(iter);
    if (strcmp(key, "5") == 0) {
      printf("    " COLOR_GREEN "Found next higher '5'\n" COLOR_RESET);
    } else {
      printf("    " COLOR_RED "ERROR: Expected '5', got '%s'\n" COLOR_RESET,
             key);
      ASSERT_TRUE(false);
    }
  } else {
    printf("    " COLOR_RED "ERROR: Iterator invalid after Seek\n" COLOR_RESET);
    ASSERT_TRUE(false);
  }

  Iter_Destroy(iter);
  SkipList_Destroy(list);
  Arena_Destroy(arena);

  free(key1);
  free(key3);
  free(key5);
  free(key7);
  free(key9);
}

void Run_SkipListTests(void) { Test_SkipList(); }