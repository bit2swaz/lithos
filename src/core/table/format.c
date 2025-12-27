/*
 * SSTable Format Primitives: Block Handles and Footer Encoding
 * ===========================================================
 * Provides encoding/decoding for BlockHandle (offset+size) and Footer
 * structures. These are the navigation primitives that make SSTables
 * seekable without scanning.
 *
 * Big Picture: Format Primitives = "SSTable Navigation Beacons"
 * ============================================================
 * SSTables are binary files with multiple blocks. To find the index or
 * metaindex blocks quickly, we store their locations in a fixed-size footer
 * at the end of the file. BlockHandles encode offset+size as varints for
 * compact representation.
 *
 * Where it fits: Footer is the last 48 bytes of every SSTable. Readers decode
 * it first to locate the index block, then use index to find data blocks.
 *
 * Key Concepts:
 * - BlockHandle: Varint-encoded (offset, size) pair pointing to a block.
 * - Footer: Fixed 48-byte structure with metaindex and index handles.
 * - Varint encoding: Compact representation of large offsets/sizes.
 */

#include "core/table/format.h"
#include "util/coding.h"
#include <string.h>

/* Maximum offset value to indicate "not set" */
#define INVALID_OFFSET (~(uint64_t)0)

void BlockHandle_Init(Lithos_BlockHandle *h) {
  h->offset = INVALID_OFFSET;
  h->size = INVALID_OFFSET;
}

bool BlockHandle_IsValid(const Lithos_BlockHandle *h) {
  return h->offset != INVALID_OFFSET;
}

size_t BlockHandle_Encode(const Lithos_BlockHandle *h, char *dst) {
  /* Varint64(offset) + Varint64(size); caller ensures dst has >=20 bytes. */
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

  /* Encode metaindex handle */
  p += BlockHandle_Encode(&f->metaindex_handle, p);

  /* Encode index handle */
  p += BlockHandle_Encode(&f->index_handle, p);

  /* Pad to byte 40 (48 total minus 8-byte magic) with zeros. */
  size_t used = (size_t)(p - dst);
  size_t padding = LITHOS_FOOTER_ENCODED_LENGTH - 8 - used;
  memset(p, 0, padding);
  p += padding;

  /* Magic number anchors the footer and guards against format confusion. */
  EncodeFixed64(p, LITHOS_TABLE_MAGIC_NUMBER);
}

lithos_status_code Footer_Decode(Lithos_Footer *f, const char *src) {
  const char *p = src;
  const char *magic_ptr = src + (LITHOS_FOOTER_ENCODED_LENGTH - 8);
  const char *limit = magic_ptr;

  /* Verify magic number first */
  uint64_t magic = DecodeFixed64(magic_ptr);
  if (magic != LITHOS_TABLE_MAGIC_NUMBER) {
    return LITHOS_CORRUPTION;
  }

  /* Decode handles */
  lithos_status_code s = BlockHandle_Decode(&f->metaindex_handle, &p, limit);
  if (s != LITHOS_OK) {
    return s;
  }

  s = BlockHandle_Decode(&f->index_handle, &p, limit);
  if (s != LITHOS_OK) {
    return s;
  }

  /* Skip padding - we don't validate it, just skip to magic */
  return LITHOS_OK;
}
