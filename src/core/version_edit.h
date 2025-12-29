
#ifndef LITHOS_CORE_VERSION_EDIT_H
#define LITHOS_CORE_VERSION_EDIT_H

#include "core/dbformat.h"
#include "util/slice.h"
#include "util/status.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define kNumLevels 7

typedef struct FileMetaData {
  uint64_t number;
  uint64_t file_size;
  Lithos_Slice smallest;
  Lithos_Slice largest;
  SequenceNumber max_sequence;  // Maximum sequence number in the file
  int refs;
  char *smallest_buf;

  char *largest_buf;
} FileMetaData;

void FileMetaData_Ref(FileMetaData *f);
void FileMetaData_Unref(FileMetaData *f);
FileMetaData *FileMetaData_Create(uint64_t number, uint64_t file_size,
                                  Lithos_Slice smallest, Lithos_Slice largest,
                                  SequenceNumber max_sequence);

typedef struct DeletedFile {
  int level;
  uint64_t number;
} DeletedFile;

typedef struct NewFile {
  int level;
  FileMetaData *file;
} NewFile;

typedef struct VersionEdit {
  bool has_log_number;
  bool has_prev_log_number;
  bool has_next_file_number;
  uint64_t log_number;
  uint64_t prev_log_number;
  uint64_t next_file_number;

  NewFile *new_files;
  size_t new_files_count;
  size_t new_files_cap;

  DeletedFile *deleted_files;
  size_t deleted_files_count;
  size_t deleted_files_cap;
} VersionEdit;

void VersionEdit_Init(VersionEdit *edit);
void VersionEdit_Clear(VersionEdit *edit);
void VersionEdit_SetLogNumber(VersionEdit *edit, uint64_t num);
void VersionEdit_SetPrevLogNumber(VersionEdit *edit, uint64_t num);
void VersionEdit_SetNextFileNumber(VersionEdit *edit, uint64_t num);
void VersionEdit_AddFile(VersionEdit *edit, int level, uint64_t number,
                         uint64_t file_size, Lithos_Slice smallest,
                         Lithos_Slice largest, SequenceNumber max_sequence);
void VersionEdit_DeleteFile(VersionEdit *edit, int level, uint64_t number);
Status VersionEdit_EncodeTo(VersionEdit *edit, Lithos_Slice *dst);
Status VersionEdit_DecodeFrom(VersionEdit *edit, Lithos_Slice src);

#ifdef __cplusplus
}
#endif

#endif
