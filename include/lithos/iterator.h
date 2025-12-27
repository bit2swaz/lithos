/*
 * Iterator Interface: Polymorphic Traversal of Database Structures
 * ===============================================================
 * Provides a uniform abstraction for iterating over all data structures
 * in Lithos, from MemTables to SSTable blocks.
 *
 * Big Picture: Iterator = "Unified Traversal Interface for All Data"
 * =================================================================
 * Databases have many data structures: SkipLists, compressed blocks, indexes.
 * The Iterator interface provides a common way to traverse them all, enabling
 * algorithms like merging during compaction to work generically.
 *
 * Where it fits: Iterators are used everywhere data needs to be traversed:
 * reads, compactions, background operations. The VTable pattern enables
 * polymorphism in C.
 *
 * Key Concepts:
 * - VTable pattern: Function pointers for polymorphism (C's inheritance).
 * - Unified interface: Seek, Next, Key, Value work on all data structures.
 * - Composition: TwoLevelIterator combines index + data iterators.
 * - Merging: K-way merge iterators combine multiple sources.
 */

#ifndef LITHOS_ITERATOR_H
#define LITHOS_ITERATOR_H

#include "util/slice.h"
#include "util/status.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Forward declaration of the iterator structure.
 * The actual implementation is opaque to users.
 */
typedef struct Lithos_Iterator Lithos_Iterator;

/*
 * Iterator Virtual Function Table
 *
 * Each function operates on opaque state (void*) which points to
 * the concrete iterator's internal data.
 *
 * Ownership: The iterator owns its state. State is freed in Cleanup().
 */
typedef struct {
  /*
   * Valid - Check if the iterator points to a valid entry.
   *
   * Returns: true if positioned at a valid key-value pair
   *          false if before first element, after last, or error occurred
   */
  bool (*Valid)(void *state);

  /*
   * SeekToFirst - Position at the first entry in the source.
   */
  void (*SeekToFirst)(void *state);

  /*
   * SeekToLast - Position at the last entry in the source.
   */
  void (*SeekToLast)(void *state);

  /*
   * Seek - Position at the first entry with key >= target.
   *
   * If no such entry exists, Valid() returns false.
   */
  void (*Seek)(void *state, Lithos_Slice target);

  /*
   * Next - Advance to the next entry.
   *
   * REQUIRES: Valid() == true
   */
  void (*Next)(void *state);

  /*
   * Prev - Move to the previous entry (reverse iteration).
   *
   * REQUIRES: Valid() == true
   * NOTE: Not all iterators support backward iteration.
   */
  void (*Prev)(void *state);

  /*
   * Key - Return the key of the current entry.
   *
   * REQUIRES: Valid() == true
   * Returns: Slice pointing to the key (valid until next operation)
   */
  Lithos_Slice (*Key)(void *state);

  /*
   * Value - Return the value of the current entry.
   *
   * REQUIRES: Valid() == true
   * Returns: Slice pointing to the value (valid until next operation)
   */
  Lithos_Slice (*Value)(void *state);

  /*
   * GetStatus - Return any error encountered during iteration.
   *
   * Returns: Status object (may be OK or error)
   */
  Status (*GetStatus)(void *state);

  /*
   * Cleanup - Free all resources owned by the iterator.
   *
   * After calling this, the iterator must not be used.
   */
  void (*Cleanup)(void *state);
} Lithos_IteratorVTable;

/*
 * Lithos_Iterator - Concrete iterator instance.
 *
 * Layout:
 * - vtable: Function pointers for operations
 * - state: Opaque pointer to implementation-specific data
 */
struct Lithos_Iterator {
  const Lithos_IteratorVTable *vtable;
  void *state;
};

/*
 * =============================================================================
 * Public API - Wrapper functions for convenience
 * =============================================================================
 */

/*
 * Check if iterator is positioned at a valid entry.
 */
static inline bool Lithos_Iter_Valid(Lithos_Iterator *iter) {
  return iter->vtable->Valid(iter->state);
}

/*
 * Position at the first entry.
 */
static inline void Lithos_Iter_SeekToFirst(Lithos_Iterator *iter) {
  iter->vtable->SeekToFirst(iter->state);
}

/*
 * Position at the last entry.
 */
static inline void Lithos_Iter_SeekToLast(Lithos_Iterator *iter) {
  iter->vtable->SeekToLast(iter->state);
}

/*
 * Position at the first entry with key >= target.
 */
static inline void Lithos_Iter_Seek(Lithos_Iterator *iter,
                                    Lithos_Slice target) {
  iter->vtable->Seek(iter->state, target);
}

/*
 * Advance to the next entry.
 */
static inline void Lithos_Iter_Next(Lithos_Iterator *iter) {
  iter->vtable->Next(iter->state);
}

/*
 * Move to the previous entry.
 */
static inline void Lithos_Iter_Prev(Lithos_Iterator *iter) {
  iter->vtable->Prev(iter->state);
}

/*
 * Get the key of the current entry.
 */
static inline Lithos_Slice Lithos_Iter_Key(Lithos_Iterator *iter) {
  return iter->vtable->Key(iter->state);
}

/*
 * Get the value of the current entry.
 */
static inline Lithos_Slice Lithos_Iter_Value(Lithos_Iterator *iter) {
  return iter->vtable->Value(iter->state);
}

/*
 * Get the status of the iterator.
 */
static inline Status Lithos_Iter_GetStatus(Lithos_Iterator *iter) {
  return iter->vtable->GetStatus(iter->state);
}

/*
 * Destroy the iterator and free all resources.
 */
static inline void Lithos_Iter_Destroy(Lithos_Iterator *iter) {
  if (iter) {
    iter->vtable->Cleanup(iter->state);
    free(iter);
  }
}

#ifdef __cplusplus
}
#endif

#endif // LITHOS_ITERATOR_H
