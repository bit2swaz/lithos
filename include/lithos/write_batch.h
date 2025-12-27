
#ifndef LITHOS_WRITE_BATCH_H
#define LITHOS_WRITE_BATCH_H

#include "core/dbformat.h"
#include "util/slice.h"
#include "util/status.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Lithos_WriteBatch {
  char *rep;
  size_t size;
  size_t capacity;
} Lithos_WriteBatch;

typedef struct WriteBatchHandler {
  void *arg;
  Status (*Put)(void *arg, Lithos_Slice key, Lithos_Slice value);
  Status (*Delete)(void *arg, Lithos_Slice key);
} WriteBatchHandler;

Lithos_WriteBatch *WriteBatch_Create(void);
void WriteBatch_Destroy(Lithos_WriteBatch *batch);
void WriteBatch_Clear(Lithos_WriteBatch *batch);
void WriteBatch_SetSequence(Lithos_WriteBatch *batch, uint64_t seq);
uint64_t WriteBatch_Sequence(const Lithos_WriteBatch *batch);
int WriteBatch_Count(const Lithos_WriteBatch *batch);
Status WriteBatch_Put(Lithos_WriteBatch *batch, Lithos_Slice key,
                      Lithos_Slice value);
Status WriteBatch_Delete(Lithos_WriteBatch *batch, Lithos_Slice key);
Status WriteBatch_Append(Lithos_WriteBatch *dst, const Lithos_WriteBatch *src);
Status WriteBatch_Iterate(const Lithos_WriteBatch *batch,
                          WriteBatchHandler *handler);

#ifdef __cplusplus
}
#endif

#endif
