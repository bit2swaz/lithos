/**
 * SkipList: Lock-Free Read, Locked-Write Concurrent Data Structure
 * =================================================================
 * 
 * A SkipList is a probabilistic alternative to balanced trees (AVL, Red-Black).
 * It provides O(log N) search, insert, and delete operations with simpler implementation.
 * 
 * Architecture:
 * - Multiple levels of linked lists (up to kMaxHeight = 12).
 * - Each node appears in Level 0, and with decreasing probability in higher levels.
 * - Level 0 contains all nodes in sorted order.
 * - Higher levels act as "express lanes" for faster search.
 * 
 * Concurrency Model (CRITICAL):
 * -----------------------------
 * **Writes:**
 *   - The caller MUST hold an external mutex (e.g., db_mutex).
 *   - Only ONE thread can call Insert() at a time.
 *   - We do NOT lock internally.
 * 
 * **Reads:**
 *   - Multiple threads can call Contains() or use Iterators CONCURRENTLY.
 *   - Reads are LOCK-FREE (no mutexes).
 *   - We use atomic operations to safely traverse the list.
 * 
 * Memory Ordering Semantics:
 * ---------------------------
 * - **Insert (Writer):** Uses `memory_order_release` when linking nodes.
 *   → Ensures all previous writes (node initialization) are visible to readers.
 * 
 * - **Contains/Iter (Readers):** Use `memory_order_acquire` when reading next pointers.
 *   → Ensures they see the fully initialized node published by the writer.
 * 
 * This is a "Publish-Subscribe" pattern:
 *   - Writer "publishes" a new node by storing its pointer with Release.
 *   - Readers "subscribe" by loading the pointer with Acquire.
 * 
 * Why Not Relaxed?
 * ----------------
 * `memory_order_relaxed` provides NO synchronization. A reader could see:
 *   - The new pointer BUT uninitialized node contents (data race).
 * Acquire-Release ensures:
 *   - If you see the pointer, you ALSO see the data it points to.
 * 
 * Memory Management:
 * ------------------
 * - All nodes are allocated from an Arena (bump-pointer allocator).
 * - No individual free() calls.
 * - When the MemTable is flushed, the entire Arena is destroyed.
 * 
 * API Design (Generic):
 * ---------------------
 * Since C lacks templates, we use `void*` keys and a Comparator function pointer.
 * The caller is responsible for key memory management (typically via Arena).
 * 
 * Author: Aditya (@bit2swaz)
 * Version: 2.0.0
 */

#ifndef LITHOS_CORE_SKIPLIST_H
#define LITHOS_CORE_SKIPLIST_H

#include <stdbool.h>
#include <stddef.h>

/* Forward Declarations */
typedef struct Lithos_SkipList Lithos_SkipList;
typedef struct Lithos_Iterator Lithos_Iterator;
typedef struct Lithos_Arena Lithos_Arena;

/**
 * Comparator Function Signature
 * ------------------------------
 * Returns:
 *   < 0 if a < b
 *   = 0 if a == b
 *   > 0 if a > b
 * 
 * Example (for strings):
 *   int StringComparator(const void* a, const void* b) {
 *       return strcmp((const char*)a, (const char*)b);
 *   }
 */
typedef int (*Lithos_Comparator)(const void* a, const void* b);

/**
 * Create a new SkipList.
 * 
 * @param cmp Comparator function for key ordering.
 * @param arena Arena for node allocations.
 * @return Pointer to the SkipList (never NULL on success).
 * 
 * Note: The SkipList itself is allocated on the heap, but all nodes come from the Arena.
 */
Lithos_SkipList* SkipList_Create(Lithos_Comparator cmp, Lithos_Arena* arena);

/**
 * Destroy a SkipList.
 * 
 * @param list The SkipList to destroy.
 * 
 * Note: This only frees the SkipList struct itself, not the nodes (Arena owns them).
 */
void SkipList_Destroy(Lithos_SkipList* list);

/**
 * Insert a key into the SkipList.
 * 
 * @param list The SkipList.
 * @param key The key to insert (must outlive the SkipList, typically Arena-allocated).
 * 
 * **CRITICAL:**
 *   - The caller MUST hold an external lock.
 *   - Only ONE thread can call this at a time.
 *   - Duplicate keys are allowed (useful for multi-versioning).
 * 
 * Complexity: O(log N) average case.
 */
void SkipList_Insert(Lithos_SkipList* list, const void* key);

/**
 * Check if a key exists in the SkipList.
 * 
 * @param list The SkipList.
 * @param key The key to search for.
 * @return true if found, false otherwise.
 * 
 * **CONCURRENCY:**
 *   - This is LOCK-FREE. Multiple threads can call this concurrently.
 *   - Safe to call while another thread is Inserting (as long as Insert is serialized).
 * 
 * Complexity: O(log N) average case.
 */
bool SkipList_Contains(const Lithos_SkipList* list, const void* key);

/**
 * Create an iterator for the SkipList.
 * 
 * @param list The SkipList.
 * @return Pointer to the iterator (caller must free with Iter_Destroy).
 * 
 * Note: The iterator captures a snapshot of the list at creation time.
 */
Lithos_Iterator* SkipList_NewIterator(Lithos_SkipList* list);

/**
 * Destroy an iterator.
 * 
 * @param iter The iterator to destroy.
 */
void Iter_Destroy(Lithos_Iterator* iter);

/**
 * Check if the iterator is valid (pointing to a real node).
 * 
 * @param iter The iterator.
 * @return true if valid, false if at end of list.
 */
bool Iter_Valid(const Lithos_Iterator* iter);

/**
 * Get the key at the iterator's current position.
 * 
 * @param iter The iterator.
 * @return Pointer to the key (NULL if invalid).
 * 
 * **IMPORTANT:** The pointer is valid as long as the Arena is alive.
 */
const void* Iter_Key(const Lithos_Iterator* iter);

/**
 * Move the iterator to the next node.
 * 
 * @param iter The iterator.
 * 
 * Precondition: Iter_Valid(iter) must be true.
 */
void Iter_Next(Lithos_Iterator* iter);

/**
 * Position the iterator at the first node.
 * 
 * @param iter The iterator.
 */
void Iter_SeekToFirst(Lithos_Iterator* iter);

/**
 * Position the iterator at the first node >= target.
 * 
 * @param iter The iterator.
 * @param target The key to seek to.
 * 
 * If no such key exists, the iterator is positioned at the end (Iter_Valid = false).
 */
void Iter_Seek(Lithos_Iterator* iter, const void* target);

#endif // LITHOS_CORE_SKIPLIST_H
