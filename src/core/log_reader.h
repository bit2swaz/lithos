
#ifndef LITHOS_LOG_READER_H
#define LITHOS_LOG_READER_H

#include "util/env.h"
#include "util/slice.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct LogReader LogReader;

LogReader *LogReader_Create(Lithos_SequentialFile *file, bool checksum);

void LogReader_Destroy(LogReader *reader);

bool LogReader_ReadRecord(LogReader *reader, Lithos_Slice *record,
                          char **scratch);

#ifdef __cplusplus
}
#endif

#endif
