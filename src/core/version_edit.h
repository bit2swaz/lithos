/**
 * Version Edit - Metadata Delta
 * ------------------------------
 * A VersionEdit is a *diff* against the current Version state. Instead of
 * rewriting the full manifest snapshot, we log only the changes:
 *   - AddFile(level, number, bounds)
 *   - DeleteFile(level, number)
 *   - Bump manifest bookkeeping numbers (log_number, next_file_number, ...)
 *
 * The MANIFEST is an append-only journal of these edits. During recovery,
 * we replay every VersionEdit in order to reconstruct the latest Version.
 * This incremental approach keeps manifest writes small and makes the format
 * forward-compatible: new tagged fields can be added without breaking older
 * readers.
 */

#ifndef LITHOS_CORE_VERSION_EDIT_H
#define LITHOS_CORE_VERSION_EDIT_H

#include "util/status.h"
#include "util/slice.h"
#include "core/dbformat.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define kNumLevels 7

/* File metadata tracked by the VersionSet */
typedef struct FileMetaData {
    uint64_t number;
    uint64_t file_size;
    Lithos_Slice smallest;   /* Smallest internal key contained in the file */
    Lithos_Slice largest;    /* Largest internal key contained in the file */
    int refs;                /* Reference count; freed when it reaches zero */
    char* smallest_buf;      /* Owning buffers for the key bounds; copied so they outlive Arena sources */
    char* largest_buf;
} FileMetaData;

void FileMetaData_Ref(FileMetaData* f);
void FileMetaData_Unref(FileMetaData* f);
FileMetaData* FileMetaData_Create(uint64_t number,
                                  uint64_t file_size,
                                  Lithos_Slice smallest,
                                  Lithos_Slice largest);

/* Deleted file entry */
typedef struct DeletedFile {
    int level;
    uint64_t number;
} DeletedFile;

/* Added file entry */
typedef struct NewFile {
    int level;
    FileMetaData* file;
} NewFile;

/* VersionEdit - delta applied to the manifest */
typedef struct VersionEdit {
    bool has_log_number;
    bool has_prev_log_number;
    bool has_next_file_number;
    uint64_t log_number;
    uint64_t prev_log_number;
    uint64_t next_file_number;

    NewFile* new_files;
    size_t new_files_count;
    size_t new_files_cap;

    DeletedFile* deleted_files;
    size_t deleted_files_count;
    size_t deleted_files_cap;
} VersionEdit;

void VersionEdit_Init(VersionEdit* edit);
void VersionEdit_Clear(VersionEdit* edit);
void VersionEdit_SetLogNumber(VersionEdit* edit, uint64_t num);
void VersionEdit_SetPrevLogNumber(VersionEdit* edit, uint64_t num);
void VersionEdit_SetNextFileNumber(VersionEdit* edit, uint64_t num);
void VersionEdit_AddFile(VersionEdit* edit,
                         int level,
                         uint64_t number,
                         uint64_t file_size,
                         Lithos_Slice smallest,
                         Lithos_Slice largest);
void VersionEdit_DeleteFile(VersionEdit* edit, int level, uint64_t number);
Status VersionEdit_EncodeTo(VersionEdit* edit, Lithos_Slice* dst);
Status VersionEdit_DecodeFrom(VersionEdit* edit, Lithos_Slice src);

#ifdef __cplusplus
}
#endif

#endif /* LITHOS_CORE_VERSION_EDIT_H */
