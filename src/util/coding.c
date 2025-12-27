
#include "util/coding.h"

void EncodeFixed32(char *dst, uint32_t value) {
  uint8_t *const buffer = (uint8_t *)dst;

  buffer[0] = (uint8_t)(value);
  buffer[1] = (uint8_t)(value >> 8);
  buffer[2] = (uint8_t)(value >> 16);
  buffer[3] = (uint8_t)(value >> 24);
}

void EncodeFixed64(char *dst, uint64_t value) {
  uint8_t *const buffer = (uint8_t *)dst;

  buffer[0] = (uint8_t)(value);
  buffer[1] = (uint8_t)(value >> 8);
  buffer[2] = (uint8_t)(value >> 16);
  buffer[3] = (uint8_t)(value >> 24);
  buffer[4] = (uint8_t)(value >> 32);
  buffer[5] = (uint8_t)(value >> 40);
  buffer[6] = (uint8_t)(value >> 48);
  buffer[7] = (uint8_t)(value >> 56);
}

uint32_t DecodeFixed32(const char *ptr) {
  const uint8_t *const buffer = (const uint8_t *)ptr;

  return ((uint32_t)buffer[0]) | ((uint32_t)buffer[1] << 8) |
         ((uint32_t)buffer[2] << 16) | ((uint32_t)buffer[3] << 24);
}

uint64_t DecodeFixed64(const char *ptr) {
  const uint8_t *const buffer = (const uint8_t *)ptr;

  return ((uint64_t)buffer[0]) | ((uint64_t)buffer[1] << 8) |
         ((uint64_t)buffer[2] << 16) | ((uint64_t)buffer[3] << 24) |
         ((uint64_t)buffer[4] << 32) | ((uint64_t)buffer[5] << 40) |
         ((uint64_t)buffer[6] << 48) | ((uint64_t)buffer[7] << 56);
}

char *EncodeVarint32(char *dst, uint32_t v) {
  uint8_t *ptr = (uint8_t *)dst;

  static const int B = 128;

  while (v >= B) {

    *ptr = (uint8_t)(v | B);
    v >>= 7;
    ptr++;
  }

  *ptr = (uint8_t)v;
  ptr++;

  return (char *)ptr;
}

char *EncodeVarint64(char *dst, uint64_t v) {
  uint8_t *ptr = (uint8_t *)dst;
  static const uint64_t B = 128;

  while (v >= B) {
    *ptr = (uint8_t)(v | B);
    v >>= 7;
    ptr++;
  }

  *ptr = (uint8_t)v;
  ptr++;

  return (char *)ptr;
}

const char *GetVarint32Ptr(const char *p, const char *limit, uint32_t *value) {
  if (p >= limit) {
    return NULL;
  }

  uint32_t result = 0;
  uint32_t shift = 0;

  for (int i = 0; i < 5; i++) {
    if (p >= limit) {
      return NULL;
    }

    uint8_t byte = (uint8_t)(*p);
    p++;

    if (byte < 128) {

      result |= ((uint32_t)byte)
                << shift;
      *value = result;
      return p;
    } else {

      result |= ((uint32_t)(byte & 127))
                << shift;
      shift += 7;
    }
  }

  return NULL;
}

const char *GetVarint64Ptr(const char *p, const char *limit, uint64_t *value) {
  if (p >= limit) {
    return NULL;
  }

  uint64_t result = 0;
  uint32_t shift = 0;

  for (int i = 0; i < 10; i++) {
    if (p >= limit) {
      return NULL;
    }

    uint8_t byte = (uint8_t)(*p);
    p++;

    if (byte < 128) {

      result |= ((uint64_t)byte) << shift;
      *value = result;
      return p;
    } else {

      result |= ((uint64_t)(byte & 127)) << shift;
      shift += 7;
    }
  }

  return NULL;
}

int VarintLength(uint64_t v) {
  int len = 1;

  while (v >= 128) {
    v >>= 7;
    len++;
  }

  return len;
}
