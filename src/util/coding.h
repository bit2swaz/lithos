
#ifndef LITHOS_UTIL_CODING_H
#define LITHOS_UTIL_CODING_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void EncodeFixed32(char *dst, uint32_t value);

void EncodeFixed64(char *dst, uint64_t value);

uint32_t DecodeFixed32(const char *ptr);

uint64_t DecodeFixed64(const char *ptr);

char *EncodeVarint32(char *dst, uint32_t v);

char *EncodeVarint64(char *dst, uint64_t v);

const char *GetVarint32Ptr(const char *p, const char *limit, uint32_t *value);

const char *GetVarint64Ptr(const char *p, const char *limit, uint64_t *value);

int VarintLength(uint64_t v);

#ifdef __cplusplus
}
#endif

#endif
