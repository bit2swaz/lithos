#include "util/compression.h"
#include <string.h>

#define LITHOS_RLE_MARKER 0xFF

bool Lithos_Compress(const char *src, size_t src_len, char *dst,
                     size_t *dst_len) {
  if (src == NULL || dst == NULL || dst_len == NULL) {
    return false;
  }

  size_t capacity = *dst_len;
  size_t out = 0;

  size_t i = 0;
  while (i < src_len) {
    unsigned char byte = (unsigned char)src[i];

    size_t run = 1;
    while (i + run < src_len && (unsigned char)src[i + run] == byte &&
           run < 255) {
      run++;
    }

    bool encode_run = (byte == LITHOS_RLE_MARKER) || (run >= 4);
    if (encode_run) {
      if (out + 3 > capacity) {
        return false;
      }
      dst[out++] = (char)LITHOS_RLE_MARKER;
      dst[out++] = (char)run;
      dst[out++] = (char)byte;
    } else {
      if (out + run > capacity) {
        return false;
      }
      memcpy(dst + out, src + i, run);
      out += run;
    }

    i += run;
  }

  *dst_len = out;
  return true;
}

bool Lithos_Uncompress(const char *src, size_t src_len, char *dst,
                       size_t dst_len) {
  if (src == NULL || dst == NULL) {
    return false;
  }

  size_t in = 0;
  size_t out = 0;

  while (in < src_len) {
    unsigned char byte = (unsigned char)src[in++];
    if (byte == LITHOS_RLE_MARKER) {
      if (in + 1 >= src_len) {
        return false;
      }
      unsigned char count = (unsigned char)src[in++];
      unsigned char value = (unsigned char)src[in++];
      if (count == 0) {
        return false;
      }
      if (out + count > dst_len) {
        return false;
      }
      memset(dst + out, value, count);
      out += count;
    } else {
      if (out + 1 > dst_len) {
        return false;
      }
      dst[out++] = (char)byte;
    }
  }

  return out == dst_len;
}
