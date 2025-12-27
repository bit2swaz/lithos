
#ifndef LITHOS_CORE_TABLE_FILTER_BLOCK_H_
#define LITHOS_CORE_TABLE_FILTER_BLOCK_H_

#include "lithos/filter_policy.h"
#include "util/slice.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FilterBlockBuilder FilterBlockBuilder;
typedef struct FilterBlockReader FilterBlockReader;

FilterBlockBuilder *
FilterBlockBuilder_Create(const Lithos_FilterPolicy *policy);

void FilterBlockBuilder_Destroy(FilterBlockBuilder *builder);

void FilterBlockBuilder_StartBlock(FilterBlockBuilder *builder,
                                   uint64_t block_offset);

void FilterBlockBuilder_AddKey(FilterBlockBuilder *builder, Lithos_Slice key);

Lithos_Slice FilterBlockBuilder_Finish(FilterBlockBuilder *builder);

FilterBlockReader *FilterBlockReader_Create(const Lithos_FilterPolicy *policy,
                                            Lithos_Slice contents);

void FilterBlockReader_Destroy(FilterBlockReader *reader);

bool FilterBlockReader_KeyMayMatch(FilterBlockReader *reader,
                                   uint64_t block_offset, Lithos_Slice key);

#ifdef __cplusplus
}
#endif

#endif
