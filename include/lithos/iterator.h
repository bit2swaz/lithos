
#ifndef LITHOS_ITERATOR_H
#define LITHOS_ITERATOR_H

#include "util/slice.h"
#include "util/status.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Lithos_Iterator Lithos_Iterator;

typedef struct {

  bool (*Valid)(void *state);

  void (*SeekToFirst)(void *state);

  void (*SeekToLast)(void *state);

  void (*Seek)(void *state, Lithos_Slice target);

  void (*Next)(void *state);

  void (*Prev)(void *state);

  Lithos_Slice (*Key)(void *state);

  Lithos_Slice (*Value)(void *state);

  Status (*GetStatus)(void *state);

  void (*Cleanup)(void *state);
} Lithos_IteratorVTable;

struct Lithos_Iterator {
  const Lithos_IteratorVTable *vtable;
  void *state;
};

static inline bool Lithos_Iter_Valid(Lithos_Iterator *iter) {
  return iter->vtable->Valid(iter->state);
}

static inline void Lithos_Iter_SeekToFirst(Lithos_Iterator *iter) {
  iter->vtable->SeekToFirst(iter->state);
}

static inline void Lithos_Iter_SeekToLast(Lithos_Iterator *iter) {
  iter->vtable->SeekToLast(iter->state);
}

static inline void Lithos_Iter_Seek(Lithos_Iterator *iter,
                                    Lithos_Slice target) {
  iter->vtable->Seek(iter->state, target);
}

static inline void Lithos_Iter_Next(Lithos_Iterator *iter) {
  iter->vtable->Next(iter->state);
}

static inline void Lithos_Iter_Prev(Lithos_Iterator *iter) {
  iter->vtable->Prev(iter->state);
}

static inline Lithos_Slice Lithos_Iter_Key(Lithos_Iterator *iter) {
  return iter->vtable->Key(iter->state);
}

static inline Lithos_Slice Lithos_Iter_Value(Lithos_Iterator *iter) {
  return iter->vtable->Value(iter->state);
}

static inline Status Lithos_Iter_GetStatus(Lithos_Iterator *iter) {
  return iter->vtable->GetStatus(iter->state);
}

static inline void Lithos_Iter_Destroy(Lithos_Iterator *iter) {
  if (iter) {
    iter->vtable->Cleanup(iter->state);
    free(iter);
  }
}

#ifdef __cplusplus
}
#endif

#endif
