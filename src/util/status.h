
#ifndef LITHOS_UTIL_STATUS_H
#define LITHOS_UTIL_STATUS_H

#include "lithos/lithos_status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  lithos_status_code
      code;
  const char
      *state;
} Status;

Status Status_OK(void);

Status Status_NotFound(const char *msg);

Status Status_Corruption(const char *msg, const char *msg2);

Status Status_IOError(const char *msg, const char *msg2);

Status Status_InvalidArgument(const char *msg);

bool Status_IsOK(Status s);

bool Status_IsNotFound(Status s);

bool Status_IsCorruption(Status s);

bool Status_IsIOError(Status s);

const char *Status_ToString(Status s);

void Status_Free(Status s);

Status Status_Copy(Status s);

#ifdef __cplusplus
}
#endif

#endif
