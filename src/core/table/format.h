
#ifndef LITHOS_CORE_TABLE_FORMAT_H_
#define LITHOS_CORE_TABLE_FORMAT_H_

#include "util/slice.h"
#include "util/status.h"
#include <stdbool.h>
#include <stdint.h>

#define LITHOS_COMPRESSION_NONE 0
#define LITHOS_COMPRESSION_RLE 1

typedef struct {
  uint64_t offset;
  uint64_t size;
} Lithos_BlockHandle;

void BlockHandle_Init(Lithos_BlockHandle *h);

bool BlockHandle_IsValid(const Lithos_BlockHandle *h);

size_t BlockHandle_Encode(const Lithos_BlockHandle *h, char *dst);

lithos_status_code BlockHandle_Decode(Lithos_BlockHandle *h, const char **src,
                                      const char *limit);

typedef struct {
  Lithos_BlockHandle metaindex_handle;
  Lithos_BlockHandle index_handle;
} Lithos_Footer;

#define LITHOS_FOOTER_ENCODED_LENGTH 48
#define LITHOS_TABLE_MAGIC_NUMBER 0xdb4775248b80fb57ULL

void Footer_Init(Lithos_Footer *f);

void Footer_Encode(const Lithos_Footer *f, char *dst);

lithos_status_code Footer_Decode(Lithos_Footer *f, const char *src);

#endif
