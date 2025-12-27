
#ifndef LITHOS_STATUS_H
#define LITHOS_STATUS_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {

  LITHOS_OK = 0,

  LITHOS_NOT_FOUND = 1,

  LITHOS_CORRUPTION = 2,

  LITHOS_IO_ERROR = 3,

  LITHOS_INVALID_ARGUMENT = 4
} lithos_status_code;

#ifdef __cplusplus
}
#endif

#endif
