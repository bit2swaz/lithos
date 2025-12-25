/**
 * env.h - File System Abstraction Layer
 * 
 * Author: Aditya (@bit2swaz)
 * 
 * This module provides OS-agnostic file I/O interfaces.
 * The POSIX implementation (env_posix.c) wraps standard <stdio.h> functions.
 * Future implementations could support Windows (HANDLE) or custom userspace I/O.
 * 
 * Key Design Decisions:
 * 1. **Opaque Handles:** File structs are opaque to prevent direct manipulation.
 * 2. **Status Returns:** All operations return Status for consistent error handling.
 * 3. **Explicit Sync:** Flush vs Sync distinction enforces durability guarantees.
 * 
 * File Types:
 * - WritableFile: Sequential writes (WAL, SSTable creation).
 * - SequentialFile: Sequential reads (Log recovery, SSTable scanning).
 * - RandomAccessFile: Random reads (SSTable block queries) [Future].
 */

#ifndef LITHOS_ENV_H
#define LITHOS_ENV_H

#include "status.h"
#include "slice.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// --- Opaque File Type Declarations ---

/**
 * Lithos_WritableFile - Handle for sequential write operations.
 * Implementation is hidden in env_posix.c.
 */
typedef struct Lithos_WritableFile Lithos_WritableFile;

/**
 * Lithos_SequentialFile - Handle for sequential read operations.
 */
typedef struct Lithos_SequentialFile Lithos_SequentialFile;

/**
 * Lithos_RandomAccessFile - Handle for random-access read operations.
 * (Not implemented in Phase 3, reserved for SSTable reads in Phase 4).
 */
typedef struct Lithos_RandomAccessFile Lithos_RandomAccessFile;

// --- Writable File Operations ---

/**
 * Env_NewWritableFile - Open or create a file for writing.
 * 
 * @param fname: Path to file (will be created or truncated).
 * @param result: Output pointer to WritableFile handle.
 * 
 * Returns: Status_OK() on success.
 * Caller must call WritableFile_Close() to release resources.
 * 
 * Example:
 *   Lithos_WritableFile* f;
 *   Status s = Env_NewWritableFile("wal_00001.log", &f);
 *   if (!Status_IsOK(&s)) { ... }
 */
Status Env_NewWritableFile(const char* fname, Lithos_WritableFile** result);

/**
 * WritableFile_Append - Write data to the end of the file.
 * 
 * @param f: WritableFile handle.
 * @param data: Slice containing data to write.
 * 
 * Returns: Status_OK() or Status_IOError().
 * 
 * Note: Data is buffered in user-space until Flush() or Sync().
 */
Status WritableFile_Append(Lithos_WritableFile* f, Lithos_Slice data);

/**
 * WritableFile_Flush - Flush user-space buffers to kernel.
 * 
 * @param f: WritableFile handle.
 * 
 * Returns: Status_OK() or Status_IOError().
 * 
 * WARNING: This does NOT guarantee data is on disk. The OS page cache
 * may still hold the data. Use WritableFile_Sync() for durability.
 */
Status WritableFile_Flush(Lithos_WritableFile* f);

/**
 * WritableFile_Sync - Force kernel buffers to persistent storage.
 * 
 * @param f: WritableFile handle.
 * 
 * Returns: Status_OK() or Status_IOError().
 * 
 * CRITICAL: This calls fsync(2), which guarantees the data has reached
 * the physical disk (or SSD). This is the ONLY way to ensure durability
 * for Write-Ahead Logs and MANIFEST files.
 * 
 * Latency: ~1-10ms on HDDs, ~0.1-1ms on NVMe SSDs.
 * 
 * Explanation:
 * - fflush() writes from user-space buffer -> OS kernel page cache.
 * - fsync() writes from kernel page cache -> physical disk platters/NAND.
 * 
 * Without fsync(), a power failure or kernel panic results in data loss.
 */
Status WritableFile_Sync(Lithos_WritableFile* f);

/**
 * WritableFile_Close - Close the file and release resources.
 * 
 * @param f: WritableFile handle (can be NULL).
 * 
 * Returns: Status_OK() or Status_IOError().
 * 
 * NOTE: This does NOT call Sync(). If durability is required, call
 * WritableFile_Sync() explicitly before Close().
 */
Status WritableFile_Close(Lithos_WritableFile* f);

// --- Sequential File Operations ---

/**
 * Env_NewSequentialFile - Open a file for sequential reading.
 * 
 * @param fname: Path to file.
 * @param result: Output pointer to SequentialFile handle.
 * 
 * Returns: Status_OK() or Status_IOError() if file doesn't exist.
 * Caller must call SequentialFile_Close() to release resources.
 */
Status Env_NewSequentialFile(const char* fname, Lithos_SequentialFile** result);

/**
 * SequentialFile_Read - Read up to n bytes from the file.
 * 
 * @param f: SequentialFile handle.
 * @param n: Maximum bytes to read.
 * @param result: Output Slice pointing to the read data.
 * @param scratch: Buffer of size >= n bytes for storing data.
 * 
 * Returns: Status_OK() on success. result->size may be < n at EOF.
 * 
 * Behavior:
 * - On success: result points to scratch, and result->size = bytes read.
 * - On EOF: result->size < n, but Status is OK.
 * - On error: Status_IOError().
 * 
 * Zero-Copy Optimization (Not implemented yet):
 *   In theory, if the OS read buffer is contiguous, result could point
 *   directly to it (avoiding memcpy to scratch). Current impl always copies.
 */
Status SequentialFile_Read(Lithos_SequentialFile* f, size_t n, 
                            Lithos_Slice* result, char* scratch);

/**
 * SequentialFile_Skip - Advance the file position by n bytes.
 * 
 * @param f: SequentialFile handle.
 * @param n: Number of bytes to skip.
 * 
 * Returns: Status_OK() or Status_IOError().
 * 
 * Use Case: Skipping over known regions in WAL recovery.
 */
Status SequentialFile_Skip(Lithos_SequentialFile* f, size_t n);

/**
 * SequentialFile_Close - Close the file.
 * 
 * @param f: SequentialFile handle (can be NULL).
 * 
 * Returns: Status_OK().
 */
Status SequentialFile_Close(Lithos_SequentialFile* f);

// --- Utility Operations ---

/**
 * Env_FileExists - Check if a file exists.
 * 
 * @param fname: Path to file.
 * 
 * Returns: Status_OK() if file exists, Status_NotFound() otherwise.
 * 
 * Use Case: Checking for CURRENT file during database open.
 */
Status Env_FileExists(const char* fname);

/**
 * Env_DeleteFile - Delete a file.
 * 
 * @param fname: Path to file.
 * 
 * Returns: Status_OK() or Status_IOError().
 * 
 * Use Case: Removing obsolete SSTable files after compaction.
 */
Status Env_DeleteFile(const char* fname);

/**
 * Env_GetFileSize - Get the size of a file.
 * 
 * @param fname: Path to file.
 * @param size: Output file size in bytes.
 * 
 * Returns: Status_OK() or Status_IOError().
 */
Status Env_GetFileSize(const char* fname, uint64_t* size);

#ifdef __cplusplus
}
#endif

#endif // LITHOS_ENV_H
