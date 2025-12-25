/**
 * SkipList Implementation: Probabilistic Concurrent Data Structure
 * ==================================================================
 * 
 * This file implements a lock-free read, locked-write SkipList.
 * 
 * Key Insights:
 * -------------
 * 1. **Node Height Distribution:**
 *    Each node is assigned a random height H where P(H=k) = 1/(2^k).
 *    - 50% of nodes have height 1 (appear only in Level 0).
 *    - 25% of nodes have height 2.
 *    - 12.5% of nodes have height 3.
 *    - ...and so on up to kMaxHeight.
 * 
 * 2. **Search Path:**
 *    Start at the highest level and move forward until the next node > target.
 *    Then drop down one level and repeat.
 *    This "drill-down" approach gives O(log N) expected time.
 * 
 * 3. **Insertion:**
 *    Find the insertion position at each level (tracking predecessors).
 *    Create a new node with random height.
 *    Link it into all levels <= its height (atomically at Level 0, sequentially above).
 * 
 * 4. **Concurrency Guarantee:**
 *    - Writers use Release semantics when publishing nodes.
 *    - Readers use Acquire semantics when loading node pointers.
 *    - This ensures a "happens-before" relationship: if a reader sees a pointer,
 *      it also sees all writes that happened before the Release.
 * 
 * Visual Example:
 * ---------------
 * After inserting keys 1, 5, 9, 12, 20, 25, the structure might look like:
 * 
 * Level 3:  head ----------------> 12 ----------------------> NULL
 * Level 2:  head --------> 5 ----> 12 -------> 20 ---------> NULL
 * Level 1:  head --> 1 -> 5 -> 9 -> 12 -> 20 -> 25 -------> NULL
 * Level 0:  head -> 1 -> 5 -> 9 -> 12 -> 20 -> 25 -------> NULL
 * 
 * To search for 18:
 *   1. Start at Level 3, head. Next is 12. 12 < 18, so advance to node 12.
 *   2. At node 12, Level 3, next is NULL. Drop to Level 2.
 *   3. At node 12, Level 2, next is 20. 20 > 18, so drop to Level 1.
 *   4. At node 12, Level 1, next is 20. 20 > 18, so drop to Level 0.
 *   5. At node 12, Level 0, next is 20. 20 > 18. Not found.
 * 
 * Author: Aditya (@bit2swaz)
 * Version: 2.0.0
 */

#include "core/skiplist.h"
#include "util/arena.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

/* ============ Constants ============ */

/**
 * Maximum height of the SkipList.
 * 
 * With N elements and max height H:
 *   Expected search time: O(log N) where the base is 1/p (here p=0.5, so base 2).
 * 
 * For 1 million elements:
 *   log2(1,000,000) ≈ 20 levels would be ideal.
 * 
 * LevelDB uses 12 as a practical tradeoff (supports ~4096 elements efficiently).
 * We follow this convention.
 */
#define SKIPLIST_MAX_HEIGHT 12

/**
 * Probability factor for height generation.
 * 
 * We use 4 to get P(height increase) = 1/4 (instead of 1/2).
 * This reduces the number of higher-level nodes, saving memory.
 */
#define SKIPLIST_BRANCHING 4

/* ============ Data Structures ============ */

/**
 * SkipList Node
 * 
 * **CRITICAL DETAIL:** The `next` array is a Flexible Array Member.
 * - Each node has a different height, so we allocate extra space for `next[]`.
 * - Syntax: `sizeof(Node) + height * sizeof(atomic_uintptr_t)`.
 * 
 * **Atomic Type:** We use `atomic_uintptr_t` (integer type) because:
 *   - C11 `_Atomic(T*)` has implementation-defined behavior for pointers.
 *   - `uintptr_t` is guaranteed to hold a pointer value.
 *   - We cast between `Node*` and `uintptr_t` as needed.
 */
typedef struct SkipList_Node {
    const void* key;                 // User key (NOT owned, typically Arena-allocated)
    _Atomic(uintptr_t) next[];       // Flexible array of atomic pointers (one per level)
} Node;

/**
 * SkipList Structure
 */
struct Lithos_SkipList {
    Node* head;                      // Sentinel node (no key, all next[] point to first real node)
    _Atomic int max_height;          // Current maximum height (grows over time)
    Lithos_Comparator compare;       // User-provided comparator
    Lithos_Arena* arena;             // Arena for node allocations
    unsigned int rnd_seed;           // Seed for random height generation (NOT thread-safe)
};

/**
 * Iterator Structure
 * 
 * Note: We keep a pointer to the SkipList to access the comparator.
 */
struct Lithos_Iterator {
    const Lithos_SkipList* list;    // Parent SkipList
    Node* node;                      // Current node (NULL if invalid)
};

/* ============ Helper Functions ============ */

/**
 * Simple Linear Congruential Generator (LCG) for random numbers.
 * 
 * Formula: next = (a * prev + c) mod m
 * 
 * We use constants from Numerical Recipes:
 *   a = 1103515245
 *   c = 12345
 *   m = 2^32 (implicit via uint32_t overflow)
 * 
 * This is NOT cryptographically secure, but sufficient for height generation.
 */
static unsigned int Random_Next(unsigned int* seed) {
    *seed = *seed * 1103515245u + 12345u;
    return *seed;
}

/**
 * Generate a random height for a new node.
 * 
 * Algorithm:
 *   - Start at height 1.
 *   - Flip a biased coin (1/SKIPLIST_BRANCHING probability).
 *   - If heads, increase height and repeat.
 *   - Stop when tails or max height reached.
 * 
 * This gives a geometric distribution: P(height = k) = (1/B)^(k-1) * (1 - 1/B)
 * where B = SKIPLIST_BRANCHING.
 */
static int RandomHeight(unsigned int* seed) {
    int height = 1;
    while (height < SKIPLIST_MAX_HEIGHT && (Random_Next(seed) % SKIPLIST_BRANCHING == 0)) {
        height++;
    }
    return height;
}

/**
 * Allocate a new node with the given height.
 * 
 * Memory Layout:
 *   [Node struct: key]
 *   [next[0]]
 *   [next[1]]
 *   [next[2]]
 *   ...
 *   [next[height-1]]
 * 
 * All next[] pointers are initialized to NULL (0).
 * 
 * Note: Flexible array members have zero size, so we allocate space for all height pointers.
 */
static Node* NewNode(const void* key, int height, Lithos_Arena* arena) {
    // Calculate size: base struct + height pointers
    // The flexible array member `next[]` has zero size in sizeof(Node)
    size_t node_size = sizeof(Node) + height * sizeof(_Atomic(uintptr_t));
    
    Node* node = (Node*)Arena_Allocate(arena, node_size);
    node->key = key;
    
    // Initialize all next[] pointers to NULL
    for (int i = 0; i < height; i++) {
        atomic_init(&node->next[i], (uintptr_t)NULL);
    }
    
    return node;
}

/**
 * Load a next pointer at a given level with Acquire semantics.
 * 
 * **Why Acquire?**
 * This ensures that if we see a non-NULL pointer, we also see all writes
 * that happened before the corresponding Release store.
 * 
 * In particular, we see the fully initialized `key` field of the target node.
 */
static inline Node* GetNext(Node* node, int level) {
    uintptr_t ptr = atomic_load_explicit(&node->next[level], memory_order_acquire);
    return (Node*)ptr;
}

/**
 * Store a next pointer at a given level with Release semantics.
 * 
 * **Why Release?**
 * This ensures that all previous writes (e.g., initializing the new node)
 * are visible to any thread that loads this pointer with Acquire.
 * 
 * This is the "publish" operation in the Publish-Subscribe pattern.
 */
static inline void SetNext(Node* node, int level, Node* next_node) {
    atomic_store_explicit(&node->next[level], (uintptr_t)next_node, memory_order_release);
}

/**
 * Find the node whose key is >= target, or NULL if no such node exists.
 * 
 * Also fills `prev[]` with the predecessor at each level.
 * This is used by Insert to know where to link the new node.
 * 
 * Algorithm:
 *   1. Start at the highest level of the head node.
 *   2. Move forward while next->key < target.
 *   3. Drop down one level and repeat.
 *   4. At Level 0, return the node found.
 */
static Node* FindGreaterOrEqual(const Lithos_SkipList* list, const void* key, Node** prev) {
    Node* x = list->head;
    int level = atomic_load_explicit(&list->max_height, memory_order_acquire) - 1;
    
    while (true) {
        Node* next = GetNext(x, level);
        
        // If next exists and next->key < key, advance forward
        if (next != NULL && list->compare(next->key, key) < 0) {
            x = next;
        } else {
            // next is NULL or next->key >= key
            // Save predecessor at this level
            if (prev != NULL) {
                prev[level] = x;
            }
            
            // Drop down to the next level
            if (level == 0) {
                return next; // Found the node or end of list
            }
            level--;
        }
    }
}

/* ============ Public API Implementation ============ */

Lithos_SkipList* SkipList_Create(Lithos_Comparator cmp, Lithos_Arena* arena) {
    assert(cmp != NULL);
    assert(arena != NULL);
    
    // Allocate the SkipList struct on the heap (NOT from Arena, as it outlives flush cycles)
    Lithos_SkipList* list = (Lithos_SkipList*)malloc(sizeof(Lithos_SkipList));
    if (list == NULL) {
        return NULL;
    }
    
    list->compare = cmp;
    list->arena = arena;
    list->rnd_seed = 0xdeadbeef; // Arbitrary seed
    atomic_init(&list->max_height, 1);
    
    // Create the head sentinel node with max height
    list->head = NewNode(NULL, SKIPLIST_MAX_HEIGHT, arena);
    
    return list;
}

void SkipList_Destroy(Lithos_SkipList* list) {
    if (list == NULL) {
        return;
    }
    
    // Note: We do NOT free individual nodes. The Arena owns them.
    // We only free the SkipList struct itself.
    free(list);
}

void SkipList_Insert(Lithos_SkipList* list, const void* key) {
    assert(list != NULL);
    assert(key != NULL);
    
    // Step 1: Find predecessors at each level
    Node* prev[SKIPLIST_MAX_HEIGHT];
    Node* x = FindGreaterOrEqual(list, key, prev);
    
    // Note: We allow duplicate keys (useful for MVCC with sequence numbers).
    // The comparator can break ties by comparing sequence numbers.
    
    // Step 2: Generate random height for the new node
    int height = RandomHeight(&list->rnd_seed);
    
    // Step 3: If new height exceeds current max_height, update predecessors
    int max_height = atomic_load_explicit(&list->max_height, memory_order_relaxed);
    if (height > max_height) {
        for (int i = max_height; i < height; i++) {
            prev[i] = list->head; // Head is the predecessor for new levels
        }
        
        // Update max_height atomically with Release
        // (Ensures the new node is visible before we expose the new height)
        atomic_store_explicit(&list->max_height, height, memory_order_release);
    }
    
    // Step 4: Create the new node
    x = NewNode(key, height, list->arena);
    
    // Step 5: Link the new node at all levels
    // CRITICAL: We link from bottom to top to maintain consistency.
    //   - Readers always traverse from top to bottom.
    //   - If we linked top-first, a reader might skip the node at lower levels.
    for (int i = 0; i < height; i++) {
        // At this point:
        //   - prev[i] is the node before the insertion position.
        //   - prev[i]->next[i] currently points to the node after insertion position.
        
        // Step 5a: Point new node to successor
        Node* next = GetNext(prev[i], i);
        SetNext(x, i, next);
        
        // Step 5b: Point predecessor to new node (the "publish" operation)
        // This uses Release to ensure x is fully initialized before any reader sees it.
        SetNext(prev[i], i, x);
    }
}

bool SkipList_Contains(const Lithos_SkipList* list, const void* key) {
    assert(list != NULL);
    assert(key != NULL);
    
    Node* x = FindGreaterOrEqual(list, key, NULL);
    if (x != NULL && list->compare(x->key, key) == 0) {
        return true;
    }
    return false;
}

/* ============ Iterator Implementation ============ */

Lithos_Iterator* SkipList_NewIterator(Lithos_SkipList* list) {
    assert(list != NULL);
    
    Lithos_Iterator* iter = (Lithos_Iterator*)malloc(sizeof(Lithos_Iterator));
    if (iter == NULL) {
        return NULL;
    }
    
    iter->list = list;
    iter->node = NULL; // Invalid by default
    return iter;
}

void Iter_Destroy(Lithos_Iterator* iter) {
    if (iter != NULL) {
        free(iter);
    }
}

bool Iter_Valid(const Lithos_Iterator* iter) {
    assert(iter != NULL);
    return iter->node != NULL;
}

const void* Iter_Key(const Lithos_Iterator* iter) {
    assert(iter != NULL);
    if (iter->node == NULL) {
        return NULL;
    }
    return iter->node->key;
}

void Iter_Next(Lithos_Iterator* iter) {
    assert(iter != NULL);
    assert(iter->node != NULL); // Must be valid
    
    // Move to the next node at Level 0
    iter->node = GetNext(iter->node, 0);
}

void Iter_SeekToFirst(Lithos_Iterator* iter) {
    assert(iter != NULL);
    
    // The first real node is head->next[0]
    iter->node = GetNext(iter->list->head, 0);
}

void Iter_Seek(Lithos_Iterator* iter, const void* target) {
    assert(iter != NULL);
    assert(target != NULL);
    
    // Find the first node >= target
    iter->node = FindGreaterOrEqual(iter->list, target, NULL);
}

/* ============ End of Implementation ============ */
