/**
 * Database Format Implementation
 * ===============================
 *
 * Implements the encoding and comparison logic for InternalKeys.
 *
 * Author: Aditya (@bit2swaz)
 * Version: 2.0.0
 */

#include "core/dbformat.h"
#include "util/coding.h"
#include <assert.h>
#include <string.h>

/* ============ Internal Key Comparator ============ */

/**
 * Compare two internal keys according to MVCC semantics.
 *
 * The keys are raw byte buffers from the SkipList.
 * Each key has the format:
 *   | user_key (variable) | seq+type (8 bytes, Little Endian) |
 *
 * Algorithm:
 * ----------
 * 1. Extract the user key portion (everything except last 8 bytes).
 * 2. Compare user keys lexicographically (ascending).
 * 3. If user keys are equal:
 *    a. Extract the packed seq+type (last 8 bytes).
 *    b. Compare sequence numbers (DESCENDING - higher seq comes first).
 *    c. If sequences are equal, compare types (kTypeValue < kTypeDeletion).
 *
 * Why This Ordering?
 * ------------------
 * - User key ascending: Standard key-value store semantics.
 * - Sequence descending: Newer versions appear first in iteration.
 *   This allows Get() to stop at the first match.
 * - Type ordering: If a key is Put and Deleted in the same compaction,
 *   the Value should appear before Deletion (though this is rare).
 *
 * Example Ordering:
 * -----------------
 * ["apple", seq=30, kTypeValue]
 * ["apple", seq=20, kTypeValue]
 * ["apple", seq=10, kTypeDeletion]
 * ["banana", seq=50, kTypeValue]
 * ["banana", seq=40, kTypeValue]
 *
 * Thread Safety:
 * --------------
 * This function is pure (no side effects) and can be called concurrently.
 */
int InternalKeyComparator(const void *a, const void *b) {
  // Cast to Lithos_Slice pointers (the SkipList stores Lithos_Slice* as keys)
  const Lithos_Slice *akey = (const Lithos_Slice *)a;
  const Lithos_Slice *bkey = (const Lithos_Slice *)b;

  // Sanity check: Internal keys must be at least 8 bytes (seq+type)
  assert(akey->size >= 8);
  assert(bkey->size >= 8);

  // Step 1: Extract user keys (everything except last 8 bytes)
  size_t auser_size = akey->size - 8;
  size_t buser_size = bkey->size - 8;

  // Step 2: Compare user keys (ascending, lexicographic)
  size_t min_len = (auser_size < buser_size) ? auser_size : buser_size;
  int cmp = memcmp(akey->data, bkey->data, min_len);

  if (cmp != 0) {
    return cmp; // Different user keys
  }

  // If one key is a prefix of the other, shorter key comes first
  if (auser_size != buser_size) {
    return (auser_size < buser_size) ? -1 : 1;
  }

  // Step 3: User keys are equal, compare sequence numbers (DESCENDING)
  // Extract the packed seq+type (last 8 bytes, Little Endian)
  const uint8_t *anum_ptr = (const uint8_t *)(akey->data + auser_size);
  const uint8_t *bnum_ptr = (const uint8_t *)(bkey->data + buser_size);

  uint64_t anum = DecodeFixed64((const char *)anum_ptr);
  uint64_t bnum = DecodeFixed64((const char *)bnum_ptr);

  // Extract sequence numbers (top 56 bits)
  SequenceNumber aseq = anum >> 8;
  SequenceNumber bseq = bnum >> 8;

  // Compare sequences (DESCENDING: higher seq comes first)
  if (aseq > bseq) {
    return -1; // a is newer, should come first
  } else if (aseq < bseq) {
    return 1; // b is newer, should come first
  }

  // Sequences are equal, compare types (bottom 8 bits)
  ValueType atype = (ValueType)(anum & 0xFF);
  ValueType btype = (ValueType)(bnum & 0xFF);

  // kTypeValue (0x01) < kTypeDeletion (0x00) is FALSE
  // Actually, kTypeDeletion (0x00) < kTypeValue (0x01)
  // But we want Values to appear before Deletions, so:
  // This intentionally inverts the natural enum ordering so the newest
  // value shadows a same-seq tombstone during iteration.
  if (atype < btype) {
    return 1; // a is Deletion, b is Value -> b comes first
  } else if (atype > btype) {
    return -1; // a is Value, b is Deletion -> a comes first
  }

  return 0; // Completely equal (should never happen in practice)
}

/* ============ Parsing ============ */

/**
 * Parse an internal key into its components.
 *
 * @param internal_key The encoded internal key (user_key + 8 bytes).
 * @param parsed Output: The parsed components.
 * @return true if valid, false if malformed.
 */
bool ParseInternalKey(Lithos_Slice internal_key, ParsedInternalKey *parsed) {
  if (internal_key.size < 8) {
    return false; // Too short to be valid
  }

  // Extract user key (all except last 8 bytes)
  parsed->user_key = Slice_Create(internal_key.data, internal_key.size - 8);

  // Extract packed seq+type (last 8 bytes)
  const uint8_t *num_ptr =
      (const uint8_t *)(internal_key.data + internal_key.size - 8);
  uint64_t packed = DecodeFixed64((const char *)num_ptr);

  // Unpack
  UnpackSequenceAndType(packed, &parsed->seq, &parsed->type);

  // Validate sequence number (must be <= kMaxSequenceNumber)
  if (parsed->seq > kMaxSequenceNumber) {
    return false;
  }

  // Validate type
  if (parsed->type != kTypeValue && parsed->type != kTypeDeletion) {
    return false;
  }

  return true;
}
