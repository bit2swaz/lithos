/*
 * table.h - SSTable Reader Interface
 *
 * Provides read access to SSTable files created by TableBuilder.
 * Manages the file lifecycle, caches the index block, and provides
 * iterator access to the data.
 *
 * Reading Strategy:
 * 1. Open: Read footer, parse index block, optionally load filter block
 * 2. Seek: Binary search index to find the right data block
 * 3. Load: Read data block (or fetch from block cache if enabled)
 * 4. Iterate: Scan within the data block
 *
 * The Table owns the file handle and index block.
 * Iterators load data blocks on demand.
 */

#ifndef LITHOS_CORE_TABLE_TABLE_H_
#define LITHOS_CORE_TABLE_TABLE_H_

#include "lithos/iterator.h"
#include "lithos/options.h"
#include "util/status.h"
#include "util/env.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Lithos_Table - Opaque SSTable file handle.
 *
 * Represents an opened SSTable with parsed footer and index.
 */
typedef struct Lithos_Table Lithos_Table;

/*
 * Table_Open - Open an SSTable file for reading.
 *
 * Process:
 * 1. Read the last 48 bytes (Footer)
 * 2. Verify magic number
 * 3. Read and parse the Index Block
 * 4. Store file handle and index for future reads
 *
 * Parameters:
 *   options   - Read options (cache, verify checksums, etc.)
 *   file      - Opened file handle (Table takes ownership)
 *   file_size - Size of the file in bytes
 *   table     - Output: pointer to opened table
 *
 * Returns: Status (OK on success, CORRUPTION/IO_ERROR on failure)
 *
 * Ownership: Table takes ownership of 'file' and will close it on destroy.
 */
Status Table_Open(const Lithos_Options* options,
                  Lithos_RandomAccessFile* file,
                  uint64_t file_size,
                  Lithos_Table** table);

/*
 * Table_Destroy - Close the table and free all resources.
 *
 * Closes the file handle, frees the index block, and releases memory.
 */
void Table_Destroy(Lithos_Table* table);

/*
 * Table_NewIterator - Create an iterator over the table.
 *
 * Returns a TwoLevelIterator that:
 * - Level 1: Iterates over the index block (key -> BlockHandle)
 * - Level 2: For each index entry, loads and iterates the data block
 *
 * The iterator automatically handles block boundaries:
 * - When a data block is exhausted, it advances to the next index entry
 * - Loads the corresponding data block
 * - Continues iteration
 *
 * Parameters:
 *   table   - The table to iterate over
 *   options - Read options
 *
 * Returns: Newly allocated iterator (caller must destroy)
 */
Lithos_Iterator* Table_NewIterator(Lithos_Table* table, const Lithos_Options* options);

/*
 * Table_InternalGet - Internal method for point lookups.
 *
 * This is used by the DB layer. It seeks the iterator to the key and
 * checks if it matches.
 *
 * Parameters:
 *   table - The table to search
 *   key   - Key to look up
 *   arg   - Opaque pointer passed to saver function
 *   saver - Callback function invoked if key is found
 *
 * The saver function signature:
 *   void saver(void* arg, Slice key, Slice value);
 *
 * Returns: Status indicating success or error
 */
Status Table_InternalGet(Lithos_Table* table,
                         Lithos_Slice key,
                         void* arg,
                         void (*saver)(void* arg, Lithos_Slice key, Lithos_Slice value));

#ifdef __cplusplus
}
#endif

#endif  // LITHOS_CORE_TABLE_TABLE_H_
