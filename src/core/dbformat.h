/**
 * Database Format Definitions
 * ============================
 *
 * This module defines the encoding for keys stored in the MemTable and
 * SSTables.
 *
 * The InternalKey Format:
 * -----------------------
 * User-visible keys are augmented with metadata to enable MVCC (Multi-Version
 * Concurrency Control) and distinguish between Put and Delete operations.
 *
 * Wire Format:
 *   | User Key (variable) | Sequence Number (7 bytes) | Type (1 byte) |
 *
 * Total: user_key.size + 8 bytes
 *
 * Sequence Number:
 * ----------------
 * A 56-bit monotonically increasing counter assigned to each write operation.
 * - Starts at 1 (0 is reserved for invalid/uninitialized).
 * - Incremented on every Put/Delete.
 * - Enables snapshot isolation: readers see data as of a specific sequence.
 * - Max value: 2^56 - 1 (72,057,594,037,927,935).
 *
 * Value Type:
 * -----------
 * 1 byte tag indicating the operation:
 * - kTypeValue (0x01): This key has a value (Put operation).
 * - kTypeDeletion (0x00): This key was deleted (Delete operation).
 *
 * Why Store Both Put and Delete?
 * -------------------------------
 * In an LSM tree, we cannot immediately remove a deleted key from Level 0.
 * Other levels might still contain older versions. We write a "tombstone"
 * (Deletion marker) that shadows older versions during compaction.
 *
 * Comparator Semantics (CRITICAL):
 * ---------------------------------
 * Keys in the SkipList are ordered as follows:
 * 1. User Key (ascending, lexicographic).
 * 2. Sequence Number (DESCENDING).
 * 3. Type (Value before Deletion, i.e., kTypeValue < kTypeDeletion).
 *
 * Why Descending Sequence?
 * -------------------------
 * When multiple versions exist (e.g., seq=10, seq=20, seq=30), we want
 * the NEWEST version (seq=30) to appear FIRST in iteration. This allows
 * Get() to stop at the first match, knowing it's the most recent.
 *
 * Example:
 * --------
 * User writes:
 *   Put("user", "Alice") at seq=10
 *   Put("user", "Bob") at seq=20
 *   Delete("user") at seq=30
 *
 * SkipList order:
 *   ["user", seq=30, kTypeDeletion]
 *   ["user", seq=20, kTypeValue] -> "Bob"
 *   ["user", seq=10, kTypeValue] -> "Alice"
 *
 * A reader at seq=25 would skip seq=30 (too new) and return "Bob" (seq=20).
 * A reader at seq=35 would see seq=30 (Deletion) and return NotFound.
 *
 * Encoding in MemTable:
 * ---------------------
 * Each entry in the SkipList is a contiguous byte buffer:
 *   | internal_key_size (Varint) | user_key | seq+type (8B) | value_size
 * (Varint) | value |
 *
 * This allows zero-copy reads and efficient Arena allocation.
 *
 * Author: Aditya (@bit2swaz)
 * Version: 2.0.0
 */

#ifndef LITHOS_CORE_DBFORMAT_H
#define LITHOS_CORE_DBFORMAT_H

#include "util/slice.h"
#include <stdbool.h>
#include <stdint.h>

/* ============ Type Definitions ============ */

/**
 * Sequence Number: Monotonically increasing write counter.
 *
 * Range: [1, 2^56 - 1]
 *
 * We use 56 bits (7 bytes) to pack into 8 bytes with ValueType.
 */
typedef uint64_t SequenceNumber;

/**
 * Maximum valid sequence number.
 *
 * 0x00FFFFFFFFFFFFFF = (2^56 - 1)
 */
#define kMaxSequenceNumber ((SequenceNumber)0x00FFFFFFFFFFFFFF)

/**
 * Value Type: Distinguishes Put from Delete operations.
 */
typedef enum {
  kTypeDeletion = 0x0, // Delete tombstone
  kTypeValue = 0x1     // Normal value
} ValueType;

/**
 * Parsed Internal Key
 *
 * This is the decoded representation of an InternalKey.
 * Used for comparisons and lookups.
 */
typedef struct {
  Lithos_Slice user_key; // User-visible key
  SequenceNumber seq;    // Sequence number (56 bits)
  ValueType type;        // Value type (8 bits)
} ParsedInternalKey;

/* ============ Encoding Functions ============ */

/**
 * Pack a sequence number and type into a single 64-bit value.
 *
 * Layout (Little Endian):
 *   Bytes 0-6: Sequence Number (56 bits)
 *   Byte 7: Value Type (8 bits)
 *
 * Formula: (seq << 8) | type
 *
 * Example:
 *   seq = 0x123456789ABC, type = kTypeValue (0x01)
 *   result = 0x01123456789ABC (when viewed as LE bytes: BC 9A 78 56 34 12 01)
 *
 * @param seq Sequence number (must be <= kMaxSequenceNumber).
 * @param type Value type (kTypeValue or kTypeDeletion).
 * @return Packed 64-bit value.
 */
static inline uint64_t PackSequenceAndType(SequenceNumber seq, ValueType type) {
  return (seq << 8) | type;
}

/**
 * Unpack a 64-bit value into sequence number and type.
 *
 * @param packed The packed 64-bit value.
 * @param seq Output: Sequence number.
 * @param type Output: Value type.
 */
static inline void UnpackSequenceAndType(uint64_t packed, SequenceNumber *seq,
                                         ValueType *type) {
  *seq = packed >> 8;
  *type = (ValueType)(packed & 0xFF);
}

/**
 * Extract the user key from an internal key.
 *
 * The internal key format in memory is:
 *   | user_key (n bytes) | seq+type (8 bytes) |
 *
 * This function returns a Slice pointing to the user key portion.
 *
 * @param internal_key The full internal key (user_key + 8 bytes).
 * @return Slice containing just the user key.
 */
static inline Lithos_Slice ExtractUserKey(Lithos_Slice internal_key) {
  if (internal_key.size < 8) {
    // Invalid internal key (too short)
    return Slice_Create(NULL, 0);
  }
  return Slice_Create(internal_key.data, internal_key.size - 8);
}

/* ============ Comparator ============ */

/**
 * Internal Key Comparator Function.
 *
 * This is used by the SkipList to order entries.
 *
 * Comparison Rules:
 * 1. Compare user keys (ascending, lexicographic).
 * 2. If user keys are equal, compare sequence numbers (DESCENDING).
 * 3. If sequence numbers are equal, compare types (kTypeValue < kTypeDeletion).
 *
 * Returns:
 *   < 0 if a < b
 *   = 0 if a == b
 *   > 0 if a > b
 *
 * @param a Pointer to the first internal key (as bytes).
 * @param b Pointer to the second internal key (as bytes).
 * @return Comparison result.
 *
 * Note: The keys are expected to be in the format:
 *   | user_key | seq+type (8 bytes) |
 */
int InternalKeyComparator(const void *a, const void *b);

/**
 * Parse an internal key from its encoded form.
 *
 * @param internal_key The encoded internal key.
 * @param parsed Output: Parsed components.
 * @return true if parsing succeeded, false if the key is malformed.
 */
bool ParseInternalKey(Lithos_Slice internal_key, ParsedInternalKey *parsed);

#endif // LITHOS_CORE_DBFORMAT_H
