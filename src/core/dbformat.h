
#ifndef LITHOS_CORE_DBFORMAT_H
#define LITHOS_CORE_DBFORMAT_H

#include "util/slice.h"
#include <stdbool.h>
#include <stdint.h>

typedef uint64_t SequenceNumber;

#define kMaxSequenceNumber ((SequenceNumber)0x00FFFFFFFFFFFFFF)

typedef enum {
  kTypeDeletion = 0x0,
  kTypeValue = 0x1
} ValueType;

typedef struct {
  Lithos_Slice user_key;
  SequenceNumber seq;
  ValueType type;
} ParsedInternalKey;

static inline uint64_t PackSequenceAndType(SequenceNumber seq, ValueType type) {
  return (seq << 8) | type;
}

static inline void UnpackSequenceAndType(uint64_t packed, SequenceNumber *seq,
                                         ValueType *type) {
  *seq = packed >> 8;
  *type = (ValueType)(packed & 0xFF);
}

static inline Lithos_Slice ExtractUserKey(Lithos_Slice internal_key) {
  if (internal_key.size < 8) {

    return Slice_Create(NULL, 0);
  }
  return Slice_Create(internal_key.data, internal_key.size - 8);
}

int InternalKeyComparator(const void *a, const void *b);

bool ParseInternalKey(Lithos_Slice internal_key, ParsedInternalKey *parsed);

#endif
