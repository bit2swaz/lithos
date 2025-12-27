
#ifndef LITHOS_ENV_H
#define LITHOS_ENV_H

#include "slice.h"
#include "status.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Lithos_WritableFile Lithos_WritableFile;

typedef struct Lithos_SequentialFile Lithos_SequentialFile;

typedef struct Lithos_RandomAccessFile Lithos_RandomAccessFile;

Status Env_NewWritableFile(const char *fname, Lithos_WritableFile **result);
Status Env_NewAppendableFile(const char *fname, Lithos_WritableFile **result);

Status WritableFile_Append(Lithos_WritableFile *f, Lithos_Slice data);

Status WritableFile_Flush(Lithos_WritableFile *f);

Status WritableFile_Sync(Lithos_WritableFile *f);

Status WritableFile_Close(Lithos_WritableFile *f);

Status Env_NewSequentialFile(const char *fname, Lithos_SequentialFile **result);

Status SequentialFile_Read(Lithos_SequentialFile *f, size_t n,
                           Lithos_Slice *result, char *scratch);

Status SequentialFile_Skip(Lithos_SequentialFile *f, size_t n);

Status SequentialFile_Close(Lithos_SequentialFile *f);

Status Env_NewRandomAccessFile(const char *fname,
                               Lithos_RandomAccessFile **result);

Status RandomAccessFile_Read(Lithos_RandomAccessFile *f, uint64_t offset,
                             size_t n, Lithos_Slice *result, char *scratch);

void RandomAccessFile_Close(Lithos_RandomAccessFile *f);

Status Env_FileExists(const char *fname);

Status Env_DeleteFile(const char *fname);

Status Env_GetFileSize(const char *fname, uint64_t *size);

#ifdef __cplusplus
}
#endif

#endif
