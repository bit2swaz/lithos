
#ifndef LITHOS_CRC32C_H
#define LITHOS_CRC32C_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t crc32c_value(const char *data, size_t n);

uint32_t crc32c_extend(uint32_t crc, const char *data, size_t n);

#ifdef __cplusplus
}
#endif

#endif
