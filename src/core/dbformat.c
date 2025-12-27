
#include "core/dbformat.h"
#include "util/coding.h"
#include <assert.h>
#include <string.h>

int InternalKeyComparator(const void *a, const void *b) {

  const Lithos_Slice *akey = (const Lithos_Slice *)a;
  const Lithos_Slice *bkey = (const Lithos_Slice *)b;

  assert(akey->size >= 8);
  assert(bkey->size >= 8);

  size_t auser_size = akey->size - 8;
  size_t buser_size = bkey->size - 8;

  size_t min_len = (auser_size < buser_size) ? auser_size : buser_size;
  int cmp = memcmp(akey->data, bkey->data, min_len);

  if (cmp != 0) {
    return cmp;
  }

  if (auser_size != buser_size) {
    return (auser_size < buser_size) ? -1 : 1;
  }

  const uint8_t *anum_ptr = (const uint8_t *)(akey->data + auser_size);
  const uint8_t *bnum_ptr = (const uint8_t *)(bkey->data + buser_size);

  uint64_t anum = DecodeFixed64((const char *)anum_ptr);
  uint64_t bnum = DecodeFixed64((const char *)bnum_ptr);

  SequenceNumber aseq = anum >> 8;
  SequenceNumber bseq = bnum >> 8;

  if (aseq > bseq) {
    return -1;
  } else if (aseq < bseq) {
    return 1;
  }

  ValueType atype = (ValueType)(anum & 0xFF);
  ValueType btype = (ValueType)(bnum & 0xFF);

  if (atype < btype) {
    return 1;
  } else if (atype > btype) {
    return -1;
  }

  return 0;
}

bool ParseInternalKey(Lithos_Slice internal_key, ParsedInternalKey *parsed) {
  if (internal_key.size < 8) {
    return false;
  }

  parsed->user_key = Slice_Create(internal_key.data, internal_key.size - 8);

  const uint8_t *num_ptr =
      (const uint8_t *)(internal_key.data + internal_key.size - 8);
  uint64_t packed = DecodeFixed64((const char *)num_ptr);

  UnpackSequenceAndType(packed, &parsed->seq, &parsed->type);

  if (parsed->seq > kMaxSequenceNumber) {
    return false;
  }

  if (parsed->type != kTypeValue && parsed->type != kTypeDeletion) {
    return false;
  }

  return true;
}
