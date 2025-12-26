#ifndef LITHOS_UTIL_COMPRESSION_H
#define LITHOS_UTIL_COMPRESSION_H

#include <stddef.h>
#include <stdbool.h>

/*
 * Simple Run-Length Encoding (RLE) compressor used when external
 * compression libraries are unavailable. Suitable for data with
 * repeated bytes; safe for arbitrary binary payloads.
 */

/* Compress src into dst using RLE. dst_len is in/out (capacity -> bytes written). */
bool Lithos_Compress(const char* src, size_t src_len, char* dst, size_t* dst_len);

/* Decompress RLE data from src into dst. dst_len is required output length. */
bool Lithos_Uncompress(const char* src, size_t src_len, char* dst, size_t dst_len);

#endif /* LITHOS_UTIL_COMPRESSION_H */
