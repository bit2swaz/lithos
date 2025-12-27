
#define _POSIX_C_SOURCE 200809L

#include "env.h"
#include "slice.h"
#include "status.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

struct Lithos_WritableFile {
  FILE *fp;
  char *filename;
};

Status Env_NewWritableFile(const char *fname, Lithos_WritableFile **result) {

  FILE *fp = fopen(fname, "wb");
  if (fp == NULL) {
    char msg[256];
    snprintf(msg, sizeof(msg), "Failed to open file: %s", fname);
    return Status_IOError(msg, strerror(errno));
  }

  Lithos_WritableFile *f =
      (Lithos_WritableFile *)malloc(sizeof(Lithos_WritableFile));
  if (f == NULL) {
    fclose(fp);
    return Status_IOError("Out of memory", "");
  }

  f->fp = fp;
  f->filename = strdup(fname);
  *result = f;

  return Status_OK();
}

Status Env_NewAppendableFile(const char *fname,
                             Lithos_WritableFile **result) {

  FILE *fp = fopen(fname, "ab");
  if (fp == NULL) {
    char msg[256];
    snprintf(msg, sizeof(msg), "Failed to open file: %s", fname);
    return Status_IOError(msg, strerror(errno));
  }

  Lithos_WritableFile *f =
      (Lithos_WritableFile *)malloc(sizeof(Lithos_WritableFile));
  if (f == NULL) {
    fclose(fp);
    return Status_IOError("Out of memory", "");
  }

  f->fp = fp;
  f->filename = strdup(fname);
  *result = f;

  return Status_OK();
}

Status WritableFile_Append(Lithos_WritableFile *f, Lithos_Slice data) {
  if (f == NULL || f->fp == NULL) {
    return Status_InvalidArgument("Invalid file handle");
  }

  size_t written = fwrite(data.data, 1, data.size, f->fp);
  if (written != data.size) {
    char msg[256];
    snprintf(msg, sizeof(msg), "Write failed: %s", f->filename);
    return Status_IOError(msg, strerror(errno));
  }

  return Status_OK();
}

Status WritableFile_Flush(Lithos_WritableFile *f) {
  if (f == NULL || f->fp == NULL) {
    return Status_InvalidArgument("Invalid file handle");
  }

  if (fflush(f->fp) != 0) {
    char msg[256];
    snprintf(msg, sizeof(msg), "Flush failed: %s", f->filename);
    return Status_IOError(msg, strerror(errno));
  }

  return Status_OK();
}

Status WritableFile_Sync(Lithos_WritableFile *f) {
  if (f == NULL || f->fp == NULL) {
    return Status_InvalidArgument("Invalid file handle");
  }

  Status s = WritableFile_Flush(f);
  if (!Status_IsOK(s)) {
    return s;
  }

  int fd = fileno(f->fp);
  if (fsync(fd) != 0) {
    char msg[256];
    snprintf(msg, sizeof(msg), "Sync failed: %s", f->filename);
    return Status_IOError(msg, strerror(errno));
  }

  return Status_OK();
}

Status WritableFile_Close(Lithos_WritableFile *f) {
  if (f == NULL) {
    return Status_OK();
  }

  Status s = Status_OK();

  if (f->fp != NULL) {

    if (fclose(f->fp) != 0) {
      char msg[256];
      snprintf(msg, sizeof(msg), "Close failed: %s", f->filename);
      s = Status_IOError(msg, strerror(errno));
    }
  }

  if (f->filename != NULL) {
    free(f->filename);
  }
  free(f);

  return s;
}

struct Lithos_SequentialFile {
  FILE *fp;
  char *filename;
};

Status Env_NewSequentialFile(const char *fname,
                             Lithos_SequentialFile **result) {
  FILE *fp = fopen(fname, "rb");
  if (fp == NULL) {
    char msg[256];
    snprintf(msg, sizeof(msg), "Failed to open file: %s", fname);
    return Status_IOError(msg, strerror(errno));
  }

  Lithos_SequentialFile *f =
      (Lithos_SequentialFile *)malloc(sizeof(Lithos_SequentialFile));
  if (f == NULL) {
    fclose(fp);
    return Status_IOError("Out of memory", "");
  }

  f->fp = fp;
  f->filename = strdup(fname);
  *result = f;

  return Status_OK();
}

Status SequentialFile_Read(Lithos_SequentialFile *f, size_t n,
                           Lithos_Slice *result, char *scratch) {
  if (f == NULL || f->fp == NULL) {
    return Status_InvalidArgument("Invalid file handle");
  }

  size_t bytes_read = fread(scratch, 1, n, f->fp);

  if (bytes_read < n && ferror(f->fp)) {
    char msg[256];
    snprintf(msg, sizeof(msg), "Read failed: %s", f->filename);
    clearerr(f->fp);
    return Status_IOError(msg, strerror(errno));
  }

  result->data = scratch;
  result->size = bytes_read;

  return Status_OK();
}

Status SequentialFile_Skip(Lithos_SequentialFile *f, size_t n) {
  if (f == NULL || f->fp == NULL) {
    return Status_InvalidArgument("Invalid file handle");
  }

  if (fseek(f->fp, (long)n, SEEK_CUR) != 0) {
    char msg[256];
    snprintf(msg, sizeof(msg), "Skip failed: %s", f->filename);
    return Status_IOError(msg, strerror(errno));
  }

  return Status_OK();
}

Status SequentialFile_Close(Lithos_SequentialFile *f) {
  if (f == NULL) {
    return Status_OK();
  }

  Status s = Status_OK();

  if (f->fp != NULL) {
    if (fclose(f->fp) != 0) {
      char msg[256];
      snprintf(msg, sizeof(msg), "Close failed: %s", f->filename);
      s = Status_IOError(msg, strerror(errno));
    }
  }

  if (f->filename != NULL) {
    free(f->filename);
  }
  free(f);

  return s;
}

struct Lithos_RandomAccessFile {
  FILE *fp;
  char *filename;
};

Status Env_NewRandomAccessFile(const char *fname,
                               Lithos_RandomAccessFile **result) {
  FILE *fp = fopen(fname, "rb");
  if (fp == NULL) {
    char msg[256];
    snprintf(msg, sizeof(msg), "Failed to open file: %s", fname);
    return Status_IOError(msg, strerror(errno));
  }

  Lithos_RandomAccessFile *f =
      (Lithos_RandomAccessFile *)malloc(sizeof(Lithos_RandomAccessFile));
  if (f == NULL) {
    fclose(fp);
    return Status_IOError("Out of memory", "");
  }

  f->fp = fp;
  f->filename = strdup(fname);
  if (f->filename == NULL) {
    free(f);
    fclose(fp);
    return Status_IOError("Out of memory", "");
  }

  *result = f;
  return Status_OK();
}

Status RandomAccessFile_Read(Lithos_RandomAccessFile *f, uint64_t offset,
                             size_t n, Lithos_Slice *result, char *scratch) {

  if (fseek(f->fp, offset, SEEK_SET) != 0) {
    char msg[256];
    snprintf(msg, sizeof(msg), "Seek failed: %s", f->filename);
    return Status_IOError(msg, strerror(errno));
  }

  size_t bytes_read = fread(scratch, 1, n, f->fp);

  if (bytes_read < n) {
    if (feof(f->fp)) {

      result->data = scratch;
      result->size = bytes_read;
      return Status_OK();
    }
    if (ferror(f->fp)) {
      char msg[256];
      snprintf(msg, sizeof(msg), "Read failed: %s", f->filename);
      return Status_IOError(msg, strerror(errno));
    }
  }

  result->data = scratch;
  result->size = bytes_read;
  return Status_OK();
}

void RandomAccessFile_Close(Lithos_RandomAccessFile *f) {
  if (f == NULL) {
    return;
  }

  if (f->fp != NULL) {
    fclose(f->fp);
  }

  if (f->filename != NULL) {
    free(f->filename);
  }

  free(f);
}

Status Env_FileExists(const char *fname) {
  struct stat st;
  if (stat(fname, &st) == 0) {
    return Status_OK();
  }

  if (errno == ENOENT) {
    return Status_NotFound("File does not exist");
  }

  return Status_IOError("stat() failed", strerror(errno));
}

Status Env_DeleteFile(const char *fname) {
  if (remove(fname) != 0) {
    if (errno == ENOENT) {
      return Status_NotFound("File does not exist");
    }
    char msg[256];
    snprintf(msg, sizeof(msg), "Failed to delete file: %s", fname);
    return Status_IOError(msg, strerror(errno));
  }

  return Status_OK();
}

Status Env_GetFileSize(const char *fname, uint64_t *size) {
  struct stat st;
  if (stat(fname, &st) != 0) {
    char msg[256];
    snprintf(msg, sizeof(msg), "Failed to get file size: %s", fname);
    return Status_IOError(msg, strerror(errno));
  }

  *size = (uint64_t)st.st_size;
  return Status_OK();
}
