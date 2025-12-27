/*
 * table_builder.h - SSTable File Construction API
 *
 * Assembles sorted key-value pairs into a complete SSTable file with:
 * - Data Blocks (with prefix compression)
 * - Optional Filter Block (Bloom filters)
 * - Index Block (for binary search)
 * - Footer (for file navigation)
 *
 * Usage:
 *   1. Create builder with output file
 *   2. Add keys in SORTED order
 *   3. Call Finish() to write index and footer
 *   4. Destroy builder
 *
 * Thread Safety: External synchronization required
 */

#ifndef LITHOS_CORE_TABLE_TABLE_BUILDER_H_
#define LITHOS_CORE_TABLE_TABLE_BUILDER_H_

#include "lithos/options.h"
#include "util/env.h"
#include "util/slice.h"
#include "util/status.h"

typedef struct Lithos_TableBuilder Lithos_TableBuilder;

/*
 * Create a new TableBuilder that writes to the given file.
 *
 * The file must remain valid until the builder is destroyed.
 *
 * Parameters:
 *   options - Configuration (block size, restart interval, etc.)
 *   file    - Writable file to output SSTable data
 *
 * Returns: Pointer to new builder, or NULL on allocation failure
 */
Lithos_TableBuilder *TableBuilder_Create(const Lithos_Options *options,
                                         Lithos_WritableFile *file);

/*
 * Destroy the builder and free all resources.
 *
 * Note: Does NOT close the file. The caller remains responsible for the file.
 */
void TableBuilder_Destroy(Lithos_TableBuilder *tb);

/*
 * Add a key-value pair to the table.
 *
 * REQUIREMENT: Keys must be added in STRICTLY INCREASING order.
 * Violating this requirement results in undefined behavior.
 *
 * When the current data block exceeds options.block_size, it is automatically
 * flushed to the file and a new block is started.
 *
 * Parameters:
 *   key   - User key (must not be empty)
 *   value - Associated value (can be empty)
 *
 * Returns: LITHOS_OK on success, error status on I/O failure
 */
lithos_status_code TableBuilder_Add(Lithos_TableBuilder *tb, Lithos_Slice key,
                                    Lithos_Slice value);

/*
 * Finalize the table by writing the index block and footer.
 *
 * Steps:
 * 1. Flush any remaining data in the current data block
 * 2. Write the index block (maps last keys -> block offsets)
 * 3. Write the metaindex block (anchors filter block if present)
 * 4. Write the footer (points to index and metaindex blocks)
 *
 * After Finish(), no more Add() calls are allowed.
 *
 * Returns: LITHOS_OK on success, error status on I/O failure
 */
lithos_status_code TableBuilder_Finish(Lithos_TableBuilder *tb);

/*
 * Abandon the table construction without writing footer.
 *
 * Use when an error occurs during Add() and you want to discard the partial
 * file. The file should be deleted by the caller.
 */
void TableBuilder_Abandon(Lithos_TableBuilder *tb);

/*
 * Get the current file size (number of bytes written so far).
 *
 * Valid at any time during construction.
 */
uint64_t TableBuilder_FileSize(const Lithos_TableBuilder *tb);

/*
 * Get the current status of the builder.
 *
 * Once an error occurs, all subsequent operations become no-ops.
 */
lithos_status_code TableBuilder_Status(const Lithos_TableBuilder *tb);

/*
 * Get the number of entries added so far.
 */
uint64_t TableBuilder_NumEntries(const Lithos_TableBuilder *tb);

#endif // LITHOS_CORE_TABLE_TABLE_BUILDER_H_
