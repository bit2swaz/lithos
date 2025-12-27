
#ifndef LITHOS_CORE_SKIPLIST_H
#define LITHOS_CORE_SKIPLIST_H

#include <stdbool.h>
#include <stddef.h>

typedef struct Lithos_SkipList Lithos_SkipList;
typedef struct Lithos_Iterator Lithos_Iterator;
typedef struct Lithos_Arena Lithos_Arena;

typedef int (*Lithos_Comparator)(const void *a, const void *b);

Lithos_SkipList *SkipList_Create(Lithos_Comparator cmp, Lithos_Arena *arena);

void SkipList_Destroy(Lithos_SkipList *list);

void SkipList_Insert(Lithos_SkipList *list, const void *key);

bool SkipList_Contains(const Lithos_SkipList *list, const void *key);

Lithos_Iterator *SkipList_NewIterator(Lithos_SkipList *list);

void Iter_Destroy(Lithos_Iterator *iter);

bool Iter_Valid(const Lithos_Iterator *iter);

const void *Iter_Key(const Lithos_Iterator *iter);

void Iter_Next(Lithos_Iterator *iter);

void Iter_SeekToFirst(Lithos_Iterator *iter);

void Iter_Seek(Lithos_Iterator *iter, const void *target);

#endif
