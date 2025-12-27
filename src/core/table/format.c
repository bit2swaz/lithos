
#include "core/table/format.h"
#include "util/coding.h"
#include <string.h>

#define INVALID_OFFSET (~(uint64_t)0)

void BlockHandle_Init(Lithos_BlockHandle *h) {
  h->offset = INVALID_OFFSET;
  h->size = INVALID_OFFSET;
}

bool BlockHandle_IsValid(const Lithos_BlockHandle *h) {
  return h->offset != INVALID_OFFSET;
}

size_t BlockHandle_Encode(const Lithos_BlockHandle *h, char *dst) {

  char *p = dst;
  p = EncodeVarint64(p, h->offset);
  p = EncodeVarint64(p, h->size);
  return (size_t)(p - dst);
}

lithos_status_code BlockHandle_Decode(Lithos_BlockHandle *h, const char **src,
                                      const char *limit) {
  const char *p = *src;
  p = GetVarint64Ptr(p, limit, &h->offset);
  if (p == NULL)
    return LITHOS_CORRUPTION;

  p = GetVarint64Ptr(p, limit, &h->size);
  if (p == NULL)
    return LITHOS_CORRUPTION;

  *src = p;
  return LITHOS_OK;
}

void Footer_Init(Lithos_Footer *f) {
  BlockHandle_Init(&f->metaindex_handle);
  BlockHandle_Init(&f->index_handle);
}

void Footer_Encode(const Lithos_Footer *f, char *dst) {
  char *p = dst;

  p += BlockHandle_Encode(&f->metaindex_handle, p);

  p += BlockHandle_Encode(&f->index_handle, p);

  size_t used = (size_t)(p - dst);
  size_t padding = LITHOS_FOOTER_ENCODED_LENGTH - 8 - used;
  memset(p, 0, padding);
  p += padding;

  EncodeFixed64(p, LITHOS_TABLE_MAGIC_NUMBER);
}

lithos_status_code Footer_Decode(Lithos_Footer *f, const char *src) {
  const char *p = src;
  const char *magic_ptr = src + (LITHOS_FOOTER_ENCODED_LENGTH - 8);
  const char *limit = magic_ptr;

  uint64_t magic = DecodeFixed64(magic_ptr);
  if (magic != LITHOS_TABLE_MAGIC_NUMBER) {
    return LITHOS_CORRUPTION;
  }

  lithos_status_code s = BlockHandle_Decode(&f->metaindex_handle, &p, limit);
  if (s != LITHOS_OK) {
    return s;
  }

  s = BlockHandle_Decode(&f->index_handle, &p, limit);
  if (s != LITHOS_OK) {
    return s;
  }

  return LITHOS_OK;
}
