/**
 * env_posix.c - POSIX Implementation of File System Abstraction
 * 
 * Author: Aditya (@bit2swaz)
 * 
 * This module wraps standard C library file I/O (<stdio.h>) and POSIX
 * system calls (<unistd.h>) to implement the Env interface.
 * 
 * Key Implementation Notes:
 * 1. **Buffering:** We use FILE* (stdio.h) for automatic buffering.
 * 2. **Durability:** WritableFile_Sync() calls fsync(2) to force disk writes.
 * 3. **Error Handling:** All functions check errno and convert to Status codes.
 * 
 * Why fsync() is Critical:
 * - fwrite() -> User-space buffer (libc)
 * - fflush() -> Kernel page cache (OS)
 * - fsync() -> Physical disk (hardware)
 * 
 * Without fsync(), a crash between fflush() and the OS background writeback
 * (typically 30 seconds on Linux) results in data loss.
 */

#define _POSIX_C_SOURCE 200809L  // Enable strdup and other POSIX functions

#include "env.h"
#include "status.h"
#include "slice.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>     // fsync, ftruncate
#include <sys/stat.h>   // stat, fstat
#include <sys/types.h>

// --- WritableFile Implementation ---

struct Lithos_WritableFile {
    FILE* fp;           // Standard C file handle
    char* filename;     // Stored for error messages
};

Status Env_NewWritableFile(const char* fname, Lithos_WritableFile** result) {
    // Open file in binary write mode (truncate if exists)
    FILE* fp = fopen(fname, "wb");
    if (fp == NULL) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Failed to open file: %s", fname);
        return Status_IOError(msg, strerror(errno));
    }
    
    // Allocate handle
    Lithos_WritableFile* f = (Lithos_WritableFile*)malloc(sizeof(Lithos_WritableFile));
    if (f == NULL) {
        fclose(fp);
        return Status_IOError("Out of memory", "");
    }
    
    f->fp = fp;
    f->filename = strdup(fname);
    *result = f;
    
    return Status_OK();
}

Status WritableFile_Append(Lithos_WritableFile* f, Lithos_Slice data) {
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

Status WritableFile_Flush(Lithos_WritableFile* f) {
    if (f == NULL || f->fp == NULL) {
        return Status_InvalidArgument("Invalid file handle");
    }
    
    /**
     * fflush() forces the user-space buffer (maintained by stdio.h) to be
     * written to the kernel's page cache. This makes the data visible to
     * other processes via read(), but does NOT guarantee disk persistence.
     * 
     * Use fflush_unlocked() if available (faster, but requires external locking).
     */
    if (fflush(f->fp) != 0) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Flush failed: %s", f->filename);
        return Status_IOError(msg, strerror(errno));
    }
    
    return Status_OK();
}

Status WritableFile_Sync(Lithos_WritableFile* f) {
    if (f == NULL || f->fp == NULL) {
        return Status_InvalidArgument("Invalid file handle");
    }
    
    /**
     * CRITICAL PATH FOR DURABILITY
     * 
     * Step 1: Flush user-space buffers to kernel.
     */
    Status s = WritableFile_Flush(f);
    if (!Status_IsOK(s)) {
        return s;
    }
    
    /**
     * Step 2: Force kernel page cache to disk using fsync(2).
     * 
     * fsync() blocks until the disk controller confirms the write is
     * physically persisted. On modern SSDs with power-loss protection,
     * this includes flushing the device's internal DRAM cache.
     * 
     * Latency Impact:
     * - HDD (7200 RPM): ~10ms (seek time + rotational latency)
     * - SATA SSD: ~1ms
     * - NVMe SSD: ~0.1ms
     * 
     * Without fsync(), the kernel typically schedules writes within 5-30 seconds
     * (see /proc/sys/vm/dirty_writeback_centisecs). A crash before the scheduled
     * writeback loses data.
     */
    int fd = fileno(f->fp);
    if (fsync(fd) != 0) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Sync failed: %s", f->filename);
        return Status_IOError(msg, strerror(errno));
    }
    
    return Status_OK();
}

Status WritableFile_Close(Lithos_WritableFile* f) {
    if (f == NULL) {
        return Status_OK();  // Idempotent close
    }
    
    Status s = Status_OK();
    
    if (f->fp != NULL) {
        /**
         * Note: fclose() does NOT call fsync(). If the caller needs durability,
         * they must call WritableFile_Sync() before Close().
         */
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

// --- SequentialFile Implementation ---

struct Lithos_SequentialFile {
    FILE* fp;
    char* filename;
};

Status Env_NewSequentialFile(const char* fname, Lithos_SequentialFile** result) {
    FILE* fp = fopen(fname, "rb");
    if (fp == NULL) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Failed to open file: %s", fname);
        return Status_IOError(msg, strerror(errno));
    }
    
    Lithos_SequentialFile* f = (Lithos_SequentialFile*)malloc(sizeof(Lithos_SequentialFile));
    if (f == NULL) {
        fclose(fp);
        return Status_IOError("Out of memory", "");
    }
    
    f->fp = fp;
    f->filename = strdup(fname);
    *result = f;
    
    return Status_OK();
}

Status SequentialFile_Read(Lithos_SequentialFile* f, size_t n, 
                            Lithos_Slice* result, char* scratch) {
    if (f == NULL || f->fp == NULL) {
        return Status_InvalidArgument("Invalid file handle");
    }
    
    /**
     * Read up to n bytes into scratch buffer.
     * fread() returns the number of bytes actually read, which may be less
     * than n if EOF is reached or an error occurs.
     */
    size_t bytes_read = fread(scratch, 1, n, f->fp);
    
    // Check for read error (distinguish from EOF)
    if (bytes_read < n && ferror(f->fp)) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Read failed: %s", f->filename);
        clearerr(f->fp);  // Reset error indicator
        return Status_IOError(msg, strerror(errno));
    }
    
    // Return Slice pointing to scratch buffer
    result->data = scratch;
    result->size = bytes_read;
    
    return Status_OK();
}

Status SequentialFile_Skip(Lithos_SequentialFile* f, size_t n) {
    if (f == NULL || f->fp == NULL) {
        return Status_InvalidArgument("Invalid file handle");
    }
    
    /**
     * fseek() is used to advance the file pointer without reading.
     * SEEK_CUR means "relative to current position".
     */
    if (fseek(f->fp, (long)n, SEEK_CUR) != 0) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Skip failed: %s", f->filename);
        return Status_IOError(msg, strerror(errno));
    }
    
    return Status_OK();
}

Status SequentialFile_Close(Lithos_SequentialFile* f) {
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

// --- Utility Functions ---

Status Env_FileExists(const char* fname) {
    struct stat st;
    if (stat(fname, &st) == 0) {
        return Status_OK();  // File exists
    }
    
    if (errno == ENOENT) {
        return Status_NotFound("File does not exist");
    }
    
    // Other error (permission denied, etc.)
    return Status_IOError("stat() failed", strerror(errno));
}

Status Env_DeleteFile(const char* fname) {
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

Status Env_GetFileSize(const char* fname, uint64_t* size) {
    struct stat st;
    if (stat(fname, &st) != 0) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Failed to get file size: %s", fname);
        return Status_IOError(msg, strerror(errno));
    }
    
    *size = (uint64_t)st.st_size;
    return Status_OK();
}
