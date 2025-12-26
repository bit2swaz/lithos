/**
 * MemTable: In-Memory Write Buffer with MVCC Support
 * ===================================================
 * 
 * The MemTable is the first landing place for all writes in Lithos.
 * It wraps a SkipList with InternalKey encoding to support:
 * 
 * 1. **Multi-Version Concurrency Control (MVCC):**
 *    Multiple versions of the same key can coexist, each tagged with a
 *    sequence number. Readers see a consistent snapshot without blocking writers.
 * 
 * 2. **Delete Operations:**
 *    Deletes are encoded as tombstones (kTypeDeletion) rather than removing
 *    the key immediately. This is necessary because older versions might exist
 *    in lower levels of the LSM tree.
 * 
 * 3. **Efficient Memory Management:**
 *    All data is allocated from a single Arena. When the MemTable is flushed
 *    to disk, the entire Arena is destroyed in one operation.
 * 
 * Architecture:
 * -------------
 * 
 *   User Key "apple" at seq=10:
 *   ┌─────────────────────────────────────────────────────────────┐
 *   │ VarintLen(13) | "apple" (5B) | 0x0A00000001 (8B) | Val... │
 *   └─────────────────────────────────────────────────────────────┘
 *        ↑                ↑                ↑                ↑
 *    Internal Key Size  User Key    Packed Seq+Type     Value
 * 
 * The SkipList stores pointers to these encoded buffers.
 * 
 * Concurrency Model:
 * ------------------
 * - **Writes:** External mutex required (typically db_mutex).
 * - **Reads:** Lock-free (the SkipList supports concurrent reads).
 * - **Lifecycle:** Reference counting ensures the MemTable stays alive
 *   while any reader is using it.
 * 
 * Reference Counting:
 * -------------------
 * - Created with ref_count = 1.
 * - MemTable_Ref() increments.
 * - MemTable_Unref() decrements. When 0, destroys the table and Arena.
 * 
 * This allows the MemTable to be:
 * - Moved to "Immutable" state while writes go to a new MemTable.
 * - Flushed to disk in the background.
 * - Kept alive for iterators even after flushing completes.
 * 
 * Author: Aditya (@bit2swaz)
 * Version: 2.0.0
 */

#ifndef LITHOS_CORE_MEMTABLE_H
#define LITHOS_CORE_MEMTABLE_H

#include "core/dbformat.h"
#include "util/slice.h"
#include "util/status.h"
#include <stddef.h>
#include <stdbool.h>

/* ============ Opaque Type ============ */

/**
 * MemTable: Opaque structure for the in-memory write buffer.
 */
typedef struct Lithos_MemTable Lithos_MemTable;
typedef struct Lithos_Iterator Lithos_Iterator;

/* ============ Lifecycle Functions ============ */

/**
 * Create a new MemTable.
 * 
 * The MemTable is created with ref_count = 1.
 * 
 * @return Pointer to the MemTable (never NULL on success).
 * 
 * Note: The caller must eventually call MemTable_Unref() to release resources.
 */
Lithos_MemTable* MemTable_Create(void);

/**
 * Increment the reference count.
 * 
 * Use this when:
 * - Storing the MemTable in a snapshot.
 * - Passing the MemTable to an iterator.
 * - Moving the MemTable to "Immutable" state.
 * 
 * @param mem The MemTable.
 * 
 * Thread Safety: NOT thread-safe. Caller must hold db_mutex.
 */
void MemTable_Ref(Lithos_MemTable* mem);

/**
 * Decrement the reference count.
 * 
 * If the count reaches 0, the MemTable and its Arena are destroyed.
 * 
 * @param mem The MemTable.
 * 
 * Thread Safety: NOT thread-safe. Caller must hold db_mutex.
 */
void MemTable_Unref(Lithos_MemTable* mem);

/* ============ Write Operations ============ */

/**
 * Add a key-value pair to the MemTable.
 * 
 * The entry is encoded as:
 *   | internal_key_size (Varint) | user_key | seq+type (8B) | value_size (Varint) | value |
 * 
 * @param mem The MemTable.
 * @param seq Sequence number for this write (monotonically increasing).
 * @param type Value type (kTypeValue for Put, kTypeDeletion for Delete).
 * @param key User key.
 * @param value Value (ignored if type == kTypeDeletion).
 * 
 * Thread Safety: Caller must hold db_mutex (single writer).
 */
void MemTable_Add(Lithos_MemTable* mem, SequenceNumber seq, ValueType type,
                  Lithos_Slice key, Lithos_Slice value);

/* ============ Read Operations ============ */

/**
 * Look up a key in the MemTable.
 * 
 * This searches for the most recent version of the key (highest sequence number).
 * 
 * Behavior:
 * ---------
 * - If found and type == kTypeValue:
 *   → Allocates and returns the value in *value_out (caller must free).
 *   → Sets *s = Status_OK().
 *   → Returns true.
 * 
 * - If found and type == kTypeDeletion:
 *   → Sets *s = Status_NotFound("Key deleted").
 *   → Returns true (indicating we found a definitive answer).
 * 
 * - If not found:
 *   → Returns false (caller should check lower levels).
 * 
 * @param mem The MemTable.
 * @param key User key to look up.
 * @param snapshot_seq Sequence upper bound (inclusive). Versions newer than this are ignored.
 * @param value_out Output: Pointer to allocated value (if found and kTypeValue).
 * @param s Output: Status (OK, NotFound, or error).
 * @return true if a definitive answer was found (value or deletion),
 *         false if the key is not present (caller should check SSTables).
 * 
 * Thread Safety: Lock-free. Can be called concurrently with writes.
 */
bool MemTable_Get(Lithos_MemTable* mem, Lithos_Slice key, SequenceNumber snapshot_seq,
                  char** value_out, Status* s);

/**
 * Get approximate memory usage in bytes.
 * 
 * This includes:
 * - Arena memory usage.
 * - SkipList overhead (negligible, allocated from Arena).
 * 
 * @param mem The MemTable.
 * @return Approximate memory usage in bytes.
 */
size_t MemTable_ApproximateMemoryUsage(const Lithos_MemTable* mem);

/* Create a SkipList iterator over the encoded entries. Caller owns the result. */
Lithos_Iterator* MemTable_NewIterator(Lithos_MemTable* mem);

#endif // LITHOS_CORE_MEMTABLE_H
