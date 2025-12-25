/**
 * log_reader.h - Write-Ahead Log Reader
 * 
 * Author: Aditya (@bit2swaz)
 * 
 * The LogReader sequentially reads records from a log file, handling
 * fragmentation and checksum verification transparently.
 * 
 * Behavior:
 * - FULL records: Returned immediately.
 * - Fragmented records: Reassembled from FIRST, MIDDLE*, LAST.
 * - Corruption: Detected via CRC32C mismatches.
 * 
 * Concurrency:
 * - Not thread-safe. Single reader per file.
 * 
 * Example Usage:
 * 
 *   Lithos_SequentialFile* file;
 *   Env_NewSequentialFile("000001.log", &file);
 *   
 *   LogReader* reader = LogReader_Create(file, true);  // Enable checksum
 *   
 *   Lithos_Slice record;
 *   char* scratch = NULL;
 *   
 *   while (LogReader_ReadRecord(reader, &record, &scratch)) {
 *       printf("Read record: %.*s\n", (int)record.size, record.data);
 *   }
 *   
 *   if (scratch) free(scratch);
 *   LogReader_Destroy(reader);
 */

#ifndef LITHOS_LOG_READER_H
#define LITHOS_LOG_READER_H

#include "util/slice.h"
#include "util/env.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * LogReader - Opaque handle for the log reader.
 */
typedef struct LogReader LogReader;

/**
 * LogReader_Create - Initialize a log reader.
 * 
 * @param file: SequentialFile to read from (must be opened).
 * @param checksum: If true, verify CRC32C checksums (recommended).
 * 
 * Returns: LogReader handle (caller must call LogReader_Destroy).
 */
LogReader* LogReader_Create(Lithos_SequentialFile* file, bool checksum);

/**
 * LogReader_Destroy - Free the reader.
 * 
 * @param reader: LogReader handle (can be NULL).
 * 
 * Note: Does NOT close the underlying file. Caller must call
 * SequentialFile_Close() separately.
 */
void LogReader_Destroy(LogReader* reader);

/**
 * LogReader_ReadRecord - Read the next logical record.
 * 
 * @param reader: LogReader handle.
 * @param record: Output slice pointing to the record data.
 * @param scratch: Pointer to dynamically allocated buffer (will be reallocated if needed).
 * 
 * Returns: true on success, false on EOF or corruption.
 * 
 * Memory Management:
 * - For FULL records: `record` may point directly into internal buffers (zero-copy).
 * - For fragmented records: `record` points to `*scratch`, which is malloc'd.
 * - Caller MUST free `*scratch` after all reads are done.
 * 
 * Corruption Handling:
 * - If checksum verification fails, returns false.
 * - Caller can check if EOF via subsequent reads returning false.
 * 
 * Example:
 * 
 *   char* scratch = NULL;
 *   Lithos_Slice record;
 *   
 *   while (LogReader_ReadRecord(reader, &record, &scratch)) {
 *       // Process record.data (size = record.size)
 *   }
 *   
 *   free(scratch);  // Clean up
 */
bool LogReader_ReadRecord(LogReader* reader, Lithos_Slice* record, char** scratch);

#ifdef __cplusplus
}
#endif

#endif // LITHOS_LOG_READER_H
