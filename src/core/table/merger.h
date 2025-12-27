/* Merging iterator: K-way merge over child iterators using a comparator. */

#ifndef LITHOS_CORE_TABLE_MERGER_H
#define LITHOS_CORE_TABLE_MERGER_H

#include "lithos/iterator.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * NewMergingIterator
 * ------------------
 * Build a single sorted view over `num` child iterators. Keys are ordered by
 * the supplied comparator (same semantics as InternalKeyComparator).
 * Ownership: the merging iterator takes ownership of child iterators and will
 * destroy them during Cleanup.
 */
Lithos_Iterator *NewMergingIterator(Lithos_Iterator **children, int num,
                                    int (*comparator)(const void *,
                                                      const void *));

#ifdef __cplusplus
}
#endif

#endif /* LITHOS_CORE_TABLE_MERGER_H */
