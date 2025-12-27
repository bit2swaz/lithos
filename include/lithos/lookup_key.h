
#ifndef LITHOS_LOOKUP_KEY_H
#define LITHOS_LOOKUP_KEY_H

#include "core/dbformat.h"
#include "util/slice.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct LookupKey {
  Lithos_Slice user_key;
  Lithos_Slice internal_key;
} LookupKey;

static inline LookupKey LookupKey_Create(Lithos_Slice user_key,
                                         Lithos_Slice internal_key) {
  LookupKey lk;
  lk.user_key = user_key;
  lk.internal_key = internal_key;
  return lk;
}

#ifdef __cplusplus
}
#endif

#endif
