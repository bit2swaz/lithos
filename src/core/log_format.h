
#ifndef LITHOS_LOG_FORMAT_H
#define LITHOS_LOG_FORMAT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define kBlockSize 32768

#define kHeaderSize 7

#define kMaxRecordSize 65535

typedef enum {

  kZeroType = 0,

  kFullType = 1,

  kFirstType = 2,

  kMiddleType = 3,

  kLastType = 4
} RecordType;

static inline int RecordType_IsValid(uint8_t type) { return type <= kLastType; }

#ifdef __cplusplus
}
#endif

#endif
