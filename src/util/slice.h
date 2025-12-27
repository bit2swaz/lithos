
#ifndef LITHOS_UTIL_SLICE_H
#define LITHOS_UTIL_SLICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  const char *data;
  size_t size;
} Lithos_Slice;

static inline Lithos_Slice Slice_Create(const char *d, size_t n) {
  Lithos_Slice s;
  s.data = d;
  s.size = n;
  return s;
}

static inline Lithos_Slice Slice_FromCString(const char *str) {
  return Slice_Create(
      str, strlen(str));
}

static inline Lithos_Slice Slice_Empty(void) { return Slice_Create(NULL, 0); }

static inline bool Slice_IsEmpty(Lithos_Slice s) { return s.size == 0; }

static inline int Slice_Compare(Lithos_Slice a, Lithos_Slice b) {

  size_t min_len = (a.size < b.size) ? a.size : b.size;

  if (min_len > 0) {
    int result = memcmp(a.data, b.data, min_len);
    if (result != 0) {
      return result;
    }
  }

  if (a.size < b.size)
    return -1;
  if (a.size > b.size)
    return 1;
  return 0;
}

static inline bool Slice_Equal(Lithos_Slice a, Lithos_Slice b) {
  if (a.size != b.size) {
    return false;
  }
  if (a.size == 0) {
    return true;
  }
  return memcmp(a.data, b.data, a.size) == 0;
}

static inline bool Slice_StartsWith(Lithos_Slice s, Lithos_Slice prefix) {

  if (prefix.size > s.size) {
    return false;
  }

  if (prefix.size == 0) {
    return true;
  }

  return memcmp(s.data, prefix.data, prefix.size) == 0;
}

static inline Lithos_Slice Slice_RemovePrefix(Lithos_Slice s,
                                              Lithos_Slice prefix) {
  if (Slice_StartsWith(s, prefix)) {

    return Slice_Create(s.data + prefix.size, s.size - prefix.size);
  }
  return s;
}

static inline char Slice_At(Lithos_Slice s, size_t index) {

  return s.data[index];
}

static inline char *Slice_ToString(Lithos_Slice s) {
  char *result = (char *)malloc(s.size + 1);
  if (result == NULL) {
    return NULL;
  }
  if (s.size > 0) {
    memcpy(result, s.data, s.size);
  }
  result[s.size] = '\0';
  return result;
}

#ifdef __cplusplus
}
#endif

#endif
