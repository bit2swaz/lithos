
#ifndef LITHOS_BLOCK_BUILDER_H
#define LITHOS_BLOCK_BUILDER_H

#include "lithos/options.h"
#include "util/slice.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Lithos_BlockBuilder Lithos_BlockBuilder;

Lithos_BlockBuilder *BlockBuilder_Create(const Lithos_Options *options);

void BlockBuilder_Destroy(Lithos_BlockBuilder *b);

void BlockBuilder_Reset(Lithos_BlockBuilder *b);

void BlockBuilder_Add(Lithos_BlockBuilder *b, Lithos_Slice key,
                      Lithos_Slice value);

Lithos_Slice BlockBuilder_Finish(Lithos_BlockBuilder *b);

size_t BlockBuilder_CurrentSizeEstimate(Lithos_BlockBuilder *b);

bool BlockBuilder_Empty(Lithos_BlockBuilder *b);

#ifdef __cplusplus
}
#endif

#endif
