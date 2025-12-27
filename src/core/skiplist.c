
#include "core/skiplist.h"
#include "util/arena.h"
#include <assert.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#define SKIPLIST_MAX_HEIGHT 12

#define SKIPLIST_BRANCHING 4

typedef struct SkipList_Node {
  const void *key;
  _Atomic(uintptr_t)
      next[];
} Node;

struct Lithos_SkipList {
  Node *head;
  _Atomic int max_height;
  Lithos_Comparator compare;
  Lithos_Arena *arena;
  unsigned int rnd_seed;
};

struct Lithos_Iterator {
  const Lithos_SkipList *list;
  Node *node;
};

static unsigned int Random_Next(unsigned int *seed) {
  *seed = *seed * 1103515245u + 12345u;
  return *seed;
}

static int RandomHeight(unsigned int *seed) {
  int height = 1;
  while (height < SKIPLIST_MAX_HEIGHT &&
         (Random_Next(seed) % SKIPLIST_BRANCHING == 0)) {
    height++;
  }
  return height;
}

static Node *NewNode(const void *key, int height, Lithos_Arena *arena) {

  size_t node_size = sizeof(Node) + height * sizeof(_Atomic(uintptr_t));

  Node *node = (Node *)Arena_Allocate(arena, node_size);
  node->key = key;

  for (int i = 0; i < height; i++) {
    atomic_init(&node->next[i], (uintptr_t)NULL);
  }

  return node;
}

static inline Node *GetNext(Node *node, int level) {
  uintptr_t ptr =
      atomic_load_explicit(&node->next[level], memory_order_acquire);
  return (Node *)ptr;
}

static inline void SetNext(Node *node, int level, Node *next_node) {
  atomic_store_explicit(&node->next[level], (uintptr_t)next_node,
                        memory_order_release);
}

static Node *FindGreaterOrEqual(const Lithos_SkipList *list, const void *key,
                                Node **prev) {
  Node *x = list->head;
  int level = atomic_load_explicit(&list->max_height, memory_order_acquire) - 1;

  while (true) {
    Node *next = GetNext(x, level);

    if (next != NULL && list->compare(next->key, key) < 0) {
      x = next;
    } else {

      if (prev != NULL) {
        prev[level] = x;
      }

      if (level == 0) {
        return next;
      }
      level--;
    }
  }
}

Lithos_SkipList *SkipList_Create(Lithos_Comparator cmp, Lithos_Arena *arena) {
  assert(cmp != NULL);
  assert(arena != NULL);

  Lithos_SkipList *list = (Lithos_SkipList *)malloc(sizeof(Lithos_SkipList));
  if (list == NULL) {
    return NULL;
  }

  list->compare = cmp;
  list->arena = arena;
  list->rnd_seed = 0xdeadbeef;
  atomic_init(&list->max_height, 1);

  list->head = NewNode(NULL, SKIPLIST_MAX_HEIGHT, arena);

  return list;
}

void SkipList_Destroy(Lithos_SkipList *list) {
  if (list == NULL) {
    return;
  }

  free(list);
}

void SkipList_Insert(Lithos_SkipList *list, const void *key) {
  assert(list != NULL);
  assert(key != NULL);

  Node *prev[SKIPLIST_MAX_HEIGHT];
  Node *x = FindGreaterOrEqual(list, key, prev);

  int height = RandomHeight(&list->rnd_seed);

  int max_height =
      atomic_load_explicit(&list->max_height, memory_order_relaxed);
  if (height > max_height) {
    for (int i = max_height; i < height; i++) {
      prev[i] = list->head;
    }

    atomic_store_explicit(&list->max_height, height, memory_order_release);
  }

  x = NewNode(key, height, list->arena);

  for (int i = 0; i < height; i++) {

    Node *next = GetNext(prev[i], i);
    SetNext(x, i, next);

    SetNext(prev[i], i, x);
  }
}

bool SkipList_Contains(const Lithos_SkipList *list, const void *key) {
  assert(list != NULL);
  assert(key != NULL);

  Node *x = FindGreaterOrEqual(list, key, NULL);
  if (x != NULL && list->compare(x->key, key) == 0) {
    return true;
  }
  return false;
}

Lithos_Iterator *SkipList_NewIterator(Lithos_SkipList *list) {
  assert(list != NULL);

  Lithos_Iterator *iter = (Lithos_Iterator *)malloc(sizeof(Lithos_Iterator));
  if (iter == NULL) {
    return NULL;
  }

  iter->list = list;
  iter->node = NULL;
  return iter;
}

void Iter_Destroy(Lithos_Iterator *iter) {
  if (iter != NULL) {
    free(iter);
  }
}

bool Iter_Valid(const Lithos_Iterator *iter) {
  assert(iter != NULL);
  return iter->node != NULL;
}

const void *Iter_Key(const Lithos_Iterator *iter) {
  assert(iter != NULL);
  if (iter->node == NULL) {
    return NULL;
  }
  return iter->node->key;
}

void Iter_Next(Lithos_Iterator *iter) {
  assert(iter != NULL);
  assert(iter->node != NULL);

  iter->node = GetNext(iter->node, 0);
}

void Iter_SeekToFirst(Lithos_Iterator *iter) {
  assert(iter != NULL);

  iter->node = GetNext(iter->list->head, 0);
}

void Iter_Seek(Lithos_Iterator *iter, const void *target) {
  assert(iter != NULL);
  assert(target != NULL);

  iter->node = FindGreaterOrEqual(iter->list, target, NULL);
}
