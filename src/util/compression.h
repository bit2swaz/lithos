#ifndef LITHOS_UTIL_COMPRESSION_H
#define LITHOS_UTIL_COMPRESSION_H

#include <stdbool.h>
#include <stddef.h>

bool Lithos_Compress(const char *src, size_t src_len, char *dst,
                     size_t *dst_len);

bool Lithos_Uncompress(const char *src, size_t src_len, char *dst,
                       size_t dst_len);

#endif
