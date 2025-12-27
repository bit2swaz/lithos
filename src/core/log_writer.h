
#ifndef LITHOS_LOG_WRITER_H
#define LITHOS_LOG_WRITER_H

#include "util/env.h"
#include "util/slice.h"
#include "util/status.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct LogWriter LogWriter;

LogWriter *LogWriter_Create(Lithos_WritableFile *dest);

void LogWriter_Destroy(LogWriter *writer);

Status LogWriter_AddRecord(LogWriter *writer, Lithos_Slice slice);

#ifdef __cplusplus
}
#endif

#endif
