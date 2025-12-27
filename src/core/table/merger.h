
#ifndef LITHOS_CORE_TABLE_MERGER_H
#define LITHOS_CORE_TABLE_MERGER_H

#include "lithos/iterator.h"

#ifdef __cplusplus
extern "C" {
#endif

Lithos_Iterator *NewMergingIterator(Lithos_Iterator **children, int num,
                                    int (*comparator)(const void *,
                                                      const void *));

#ifdef __cplusplus
}
#endif

#endif
