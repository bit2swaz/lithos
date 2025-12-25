/**
 * MemTable Implementation
 * =======================
 * 
 * This file implements the in-memory write buffer with full MVCC support.
 * 
 * Key Design Decisions:
 * ---------------------
 * 1. **Encoding Format:**
 *    Each entry in the SkipList is a self-contained byte buffer:
 *    | internal_key_size (Varint) | user_key | seq+type (8B) | value_size (Varint) | value |
 * 
 *    This allows:
 *    - Zero-copy comparisons (the SkipList comparator works directly on buffers).
 *    - Efficient Arena allocation (single contiguous buffer per entry).
 *    - Easy serialization to WAL/SSTable (same format).
 * 
 * 2. **Lookup Strategy:**
 *    To find a key, we construct a "seek target" with the user key and
 *    kMaxSequenceNumber. The SkipList's comparator will find the first
 *    entry >= this target, which is the most recent version.
 * 
 * 3. **Memory Ownership:**
 *    - The MemTable owns the Arena.
 *    - All encoded buffers live in the Arena.
 *    - The SkipList stores pointers into Arena memory.
 *    - When ref_count hits 0, everything is freed at once.
 * 
 * Author: Aditya (@bit2swaz)
 * Version: 2.0.0
 */

#include "core/memtable.h"
#include "core/dbformat.h"
#include "core/skiplist.h"
#include "util/arena.h"
#include "util/coding.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* ============ MemTable Structure ============ */

struct Lithos_MemTable {
    Lithos_SkipList* table;     // The underlying SkipList
    Lithos_Arena* arena;        // Arena for all allocations
    int refs;                   // Reference count
};

/* ============ Helper Functions ============ */

/**
 * Encode a MemTable entry into Arena memory.
 * 
 * Format:
 *   | internal_key_size (Varint) | user_key | seq+type (8B) | value_size (Varint) | value |
 * 
 * Where:
 *   internal_key_size = user_key.size + 8
 * 
 * Returns a Slice pointing to the encoded buffer (owned by Arena).
 * This Slice is what gets inserted into the SkipList.
 * 
 * Example:
 * --------
 * user_key = "apple" (5 bytes)
 * seq = 10, type = kTypeValue
 * value = "red" (3 bytes)
 * 
 * Encoded:
 *   0x0D (internal_key_size = 13 = 5 + 8, Varint = 1 byte)
 *   "apple" (5 bytes)
 *   0x0A00000001 (seq=10, type=1, Fixed64 LE = 8 bytes)
 *   0x03 (value_size = 3, Varint = 1 byte)
 *   "red" (3 bytes)
 * 
 * Total: 1 + 5 + 8 + 1 + 3 = 18 bytes
 */
static Lithos_Slice* EncodeMemTableEntry(Lithos_Arena* arena, Lithos_Slice key, SequenceNumber seq,
                                    ValueType type, Lithos_Slice value) {
    // Calculate sizes
    size_t internal_key_size = key.size + 8;
    
    // Calculate total encoded size
    size_t needed = VarintLength(internal_key_size) +
                    key.size +
                    8 +  // seq + type
                    VarintLength(value.size) +
                    value.size;
    
    // Allocate from Arena
    char* buf = Arena_Allocate(arena, needed);
    char* p = buf;
    
    // Encode internal_key_size
    p = EncodeVarint32(p, (uint32_t)internal_key_size);
    
    // Copy user key
    memcpy(p, key.data, key.size);
    p += key.size;
    
    // Encode seq + type (8 bytes, Little Endian)
    uint64_t packed = PackSequenceAndType(seq, type);
    EncodeFixed64(p, packed);
    p += 8;
    
    // Encode value_size
    p = EncodeVarint32(p, (uint32_t)value.size);
    
    // Copy value
    if (value.size > 0) {
        memcpy(p, value.data, value.size);
        p += value.size;
    }
    
    // Verify we used exactly the space we allocated
    assert((size_t)(p - buf) == needed);
    
    // Allocate a Lithos_Slice wrapper and return it
    // The SkipList will store this Lithos_Slice* as the "key"
    Lithos_Slice* slice = (Lithos_Slice*)Arena_Allocate(arena, sizeof(Lithos_Slice));
    *slice = Slice_Create(buf, needed);
    return slice;
}

/**
 * Decode the internal key from an encoded MemTable entry.
 * 
 * This extracts the "user_key + seq+type" portion.
 * 
 * Returns a Slice pointing into the encoded buffer (no copy).
 */
static Lithos_Slice DecodeInternalKey(const Lithos_Slice* encoded) {
    const char* p = encoded->data;
    const char* limit = p + encoded->size;
    
    // Decode internal_key_size
    uint32_t internal_key_size;
    const char* after = GetVarint32Ptr(p, limit, &internal_key_size);
    if (after == NULL) {
        return Slice_Create(NULL, 0); // Corrupted
    }
    
    // Internal key starts after the varint
    return Slice_Create(after, internal_key_size);
}

/**
 * Decode the value from an encoded MemTable entry.
 * 
 * Returns a Slice pointing into the encoded buffer (no copy).
 */
static Lithos_Slice DecodeValue(const Lithos_Slice* encoded) {
    const char* p = encoded->data;
    const char* limit = p + encoded->size;
    
    // Decode internal_key_size
    uint32_t internal_key_size;
    p = GetVarint32Ptr(p, limit, &internal_key_size);
    if (p == NULL) {
        return Slice_Create(NULL, 0); // Corrupted
    }
    
    // Skip internal key
    p += internal_key_size;
    if (p >= limit) {
        return Slice_Create(NULL, 0); // Corrupted
    }
    
    // Decode value_size
    uint32_t value_size;
    p = GetVarint32Ptr(p, limit, &value_size);
    if (p == NULL || p + value_size > limit) {
        return Slice_Create(NULL, 0); // Corrupted
    }
    
    return Slice_Create(p, value_size);
}

/* ============ MemTable Comparator ============ */

/**
 * Comparator for MemTable entries.
 * 
 * The SkipList stores Slice* pointers. Each Slice points to an encoded entry.
 * We need to extract the internal key portion and compare using InternalKeyComparator.
 */
static int MemTableKeyComparator(const void* a, const void* b) {
    const Lithos_Slice* aslice = (const Lithos_Slice*)a;
    const Lithos_Slice* bslice = (const Lithos_Slice*)b;
    
    // Decode internal keys
    Lithos_Slice akey = DecodeInternalKey(aslice);
    Lithos_Slice bkey = DecodeInternalKey(bslice);
    
    // Use the InternalKeyComparator
    return InternalKeyComparator(&akey, &bkey);
}

/* ============ Public API Implementation ============ */

Lithos_MemTable* MemTable_Create(void) {
    Lithos_MemTable* mem = (Lithos_MemTable*)malloc(sizeof(Lithos_MemTable));
    if (mem == NULL) {
        return NULL;
    }
    
    mem->arena = Arena_Create();
    if (mem->arena == NULL) {
        free(mem);
        return NULL;
    }
    
    mem->table = SkipList_Create(MemTableKeyComparator, mem->arena);
    if (mem->table == NULL) {
        Arena_Destroy(mem->arena);
        free(mem);
        return NULL;
    }
    
    mem->refs = 1; // Start with one reference
    
    return mem;
}

void MemTable_Ref(Lithos_MemTable* mem) {
    assert(mem != NULL);
    assert(mem->refs > 0);
    mem->refs++;
}

void MemTable_Unref(Lithos_MemTable* mem) {
    if (mem == NULL) {
        return;
    }
    
    assert(mem->refs > 0);
    mem->refs--;
    
    if (mem->refs == 0) {
        // Destroy everything
        SkipList_Destroy(mem->table);
        Arena_Destroy(mem->arena);
        free(mem);
    }
}

void MemTable_Add(Lithos_MemTable* mem, SequenceNumber seq, ValueType type,
                  Lithos_Slice key, Lithos_Slice value) {
    assert(mem != NULL);
    assert(seq <= kMaxSequenceNumber);
    
    // For deletions, we store an empty value
    if (type == kTypeDeletion) {
        value = Slice_Create("", 0);
    }
    
    // Encode the entry
    Lithos_Slice* encoded = EncodeMemTableEntry(mem->arena, key, seq, type, value);
    
    // Insert into SkipList
    SkipList_Insert(mem->table, encoded);
}

bool MemTable_Get(Lithos_MemTable* mem, Lithos_Slice key, char** value_out, Status* s) {
    assert(mem != NULL);
    assert(value_out != NULL);
    assert(s != NULL);
    
    // Construct a lookup key: user_key + kMaxSequenceNumber + kTypeValue
    // This will seek to the first entry >= (user_key, max_seq), which is
    // the most recent version of user_key.
    
    // We need to encode this lookup key in the same format
    size_t internal_key_size = key.size + 8;
    size_t needed = VarintLength(internal_key_size) + internal_key_size;
    
    // Allocate temporary buffer on stack (should be small)
    char lookup_buf[256];
    char* buf;
    
    if (needed <= sizeof(lookup_buf)) {
        buf = lookup_buf;
    } else {
        buf = (char*)malloc(needed);
        if (buf == NULL) {
            *s = Status_IOError("Out of memory", "");
            return false;
        }
    }
    
    // Encode the lookup key
    char* p = buf;
    p = EncodeVarint32(p, (uint32_t)internal_key_size);
    memcpy(p, key.data, key.size);
    p += key.size;
    
    // Pack kMaxSequenceNumber + kTypeValue
    uint64_t packed = PackSequenceAndType(kMaxSequenceNumber, kTypeValue);
    EncodeFixed64(p, packed);
    
    Lithos_Slice lookup_slice = Slice_Create(buf, needed);
    
    // Create an iterator and seek to the lookup key
    Lithos_Iterator* iter = SkipList_NewIterator(mem->table);
    if (iter == NULL) {
        if (buf != lookup_buf) free(buf);
        *s = Status_IOError("Out of memory", "");
        return false;
    }
    
    Iter_Seek(iter, &lookup_slice);
    
    bool found = false;
    
    if (Iter_Valid(iter)) {
        // Get the found entry
        const Lithos_Slice* entry = (const Lithos_Slice*)Iter_Key(iter);
        
        // Decode its internal key
        Lithos_Slice ikey = DecodeInternalKey(entry);
        
        // Extract user key
        Lithos_Slice found_user_key = ExtractUserKey(ikey);
        
        // Check if it matches our search key
        if (Slice_Compare(found_user_key, key) == 0) {
            // Match! Parse the internal key to get type and seq
            ParsedInternalKey parsed;
            if (ParseInternalKey(ikey, &parsed)) {
                if (parsed.type == kTypeValue) {
                    // Found a value
                    Lithos_Slice val = DecodeValue(entry);
                    
                    // Allocate and copy the value
                    *value_out = (char*)malloc(val.size + 1);
                    if (*value_out != NULL) {
                        memcpy(*value_out, val.data, val.size);
                        (*value_out)[val.size] = '\0'; // Null-terminate for convenience
                        *s = Status_OK();
                        found = true;
                    } else {
                        *s = Status_IOError("Out of memory", "");
                        found = false;
                    }
                } else {
                    // Type is kTypeDeletion
                    *s = Status_NotFound("Key deleted");
                    found = true; // We found a definitive answer (deletion)
                }
            } else {
                // Corrupted internal key
                *s = Status_Corruption("Corrupted internal key", "");
                found = false;
            }
        }
        // If user keys don't match, the key is not in the MemTable
    }
    
    Iter_Destroy(iter);
    if (buf != lookup_buf) {
        free(buf);
    }
    
    return found;
}

size_t MemTable_ApproximateMemoryUsage(const Lithos_MemTable* mem) {
    assert(mem != NULL);
    return Arena_MemoryUsage(mem->arena);
}
