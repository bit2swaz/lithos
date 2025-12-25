/**
 * log_writer.h - Write-Ahead Log Writer
 * 
 * Author: Aditya (@bit2swaz)
 * 
 * The LogWriter appends records to a WritableFile using the format
 * defined in log_format.h. It handles fragmentation transparently:
 * 
 * - Small records (< 32KB): Written as FULL type.
 * - Large records (> 32KB): Split into FIRST, MIDDLE*, LAST fragments.
 * 
 * Concurrency:
 * - Not thread-safe. Caller must serialize writes (e.g., via db_mutex).
 * 
 * Example Usage:
 * 
 *   Lithos_WritableFile* file;
 *   Env_NewWritableFile("000001.log", &file);
 *   
 *   LogWriter* writer = LogWriter_Create(file);
 *   
 *   Lithos_Slice record = Slice_FromCString("My data");
 *   Status s = LogWriter_AddRecord(writer, record);
 *   
 *   WritableFile_Sync(file);  // Durability
 *   LogWriter_Destroy(writer);
 */

#ifndef LITHOS_LOG_WRITER_H
#define LITHOS_LOG_WRITER_H

#include "util/status.h"
#include "util/slice.h"
#include "util/env.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * LogWriter - Opaque handle for the log writer.
 */
typedef struct LogWriter LogWriter;

/**
 * LogWriter_Create - Initialize a log writer.
 * 
 * @param dest: WritableFile to append records to.
 * 
 * Returns: LogWriter handle (caller must call LogWriter_Destroy).
 * 
 * Note: The writer assumes the file starts at offset 0. If resuming
 * a log, the caller must seek to the correct position first.
 */
LogWriter* LogWriter_Create(Lithos_WritableFile* dest);

/**
 * LogWriter_Destroy - Free the writer.
 * 
 * @param writer: LogWriter handle (can be NULL).
 * 
 * Note: Does NOT close the underlying file. Caller must call
 * WritableFile_Close() separately.
 */
void LogWriter_Destroy(LogWriter* writer);

/**
 * LogWriter_AddRecord - Append a record to the log.
 * 
 * @param writer: LogWriter handle.
 * @param slice: Data to write.
 * 
 * Returns: Status_OK() or Status_IOError().
 * 
 * Behavior:
 * - If the record fits in the current block: Writes as FULL.
 * - If the record spans multiple blocks: Fragments as FIRST/MIDDLE/LAST.
 * - Automatically handles block boundaries and zero-padding.
 * 
 * Durability:
 * - This function does NOT call Sync(). The caller must explicitly
 *   call WritableFile_Sync() to guarantee durability.
 * 
 * Example (Large Record):
 * 
 *   char big_data[70000];
 *   memset(big_data, 'X', sizeof(big_data));
 *   
 *   Lithos_Slice slice = {big_data, sizeof(big_data)};
 *   Status s = LogWriter_AddRecord(writer, slice);
 *   
 *   // This will create ~3 fragments: FIRST, MIDDLE, LAST
 */
Status LogWriter_AddRecord(LogWriter* writer, Lithos_Slice slice);

#ifdef __cplusplus
}
#endif

#endif // LITHOS_LOG_WRITER_H
