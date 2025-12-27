/*
 * Bloom Filter: Probabilistic Set Membership with No False Negatives
 * ==================================================================
 * Implements space-efficient probabilistic data structure for fast negative
 * lookups. Used in SSTable filter blocks to skip data blocks that don't contain
 * a key.
 *
 * Big Picture: Bloom Filters = "Fast 'No' Answers for Set Membership"
 * ===================================================================
 * Traditional sets require O(N) space. Bloom filters use O(K) space with
 * controlled false positive rate. For databases, this means we can quickly
 * say "key is definitely NOT in this SSTable block" without reading from disk.
 * False positives are OK (we just read an extra block), but false negatives
 * are not (we'd miss the key).
 *
 * Where it fits: Each SSTable has a filter block with one Bloom filter per
 * 2KB of data. Readers check filters before loading data blocks, reducing
 * I/O by 90%+ for missing keys.
 *
 * Key Concepts:
 * - Hash functions: Multiple independent hashes per key.
 * - Bit array: Set bits at hash positions, check if all are set.
 * - False positives: "Might contain" vs "definitely doesn't contain".
 * - Double hashing: Generate K hashes from just 2 base hashes.
 */

#include "lithos/filter_policy.h"
#include "util/coding.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*
 * BloomFilterPolicy state
 */
typedef struct {
  int bits_per_key;
  int k; /* Number of hash functions (probes) */
} BloomState;

/*
 * BloomHash: Fast, deterministic hash function for Bloom filters.
 * ============================================================
 * Input: Lithos_Slice (key bytes)
 * Output: uint32_t (32-bit hash value)
 * Intent: Generate high-quality hash values for Bloom filter bit positions.
 *         Uses MurmurHash-inspired algorithm for good distribution and speed.
 *         This is the base hash; double hashing generates K hashes from two
 * bases.
 */
static uint32_t BloomHash(Lithos_Slice key) {
  const uint32_t m = 0xc6a4a793; // Mixing constant
  const uint32_t r = 24;         // Bit rotation amount
  const char *data = key.data;
  size_t n = key.size;

  uint32_t h = 0xbc9f1d34 ^ (n * m); // Initialize with seed XOR length*m

  /* Process 4 bytes at a time for speed */
  while (n >= 4) {
    uint32_t w = DecodeFixed32(data); // Read 4 bytes as little-endian uint32
    data += 4;
    n -= 4;
    h += w;         // Mix in the word
    h *= m;         // Multiply by mixing constant
    h ^= (h >> 16); // XOR with rotated version
  }

  /* Handle remaining 1-3 bytes */
  switch (n) {
  case 3:
    h += (unsigned char)(data[2]) << 16; // Mix in byte 2
                                         /* fall through */
  case 2:
    h += (unsigned char)(data[1]) << 8; // Mix in byte 1
                                        /* fall through */
  case 1:
    h += (unsigned char)(data[0]); // Mix in byte 0
    h *= m;
    h ^= (h >> r); // Final mixing
    break;
  }

  return h;
}

/*
 * FilterPolicy VTable implementation for Bloom filters
 */
static const char *Bloom_Name(const Lithos_FilterPolicy *policy) {
  (void)policy;
  return "lithos.BuiltinBloomFilter";
}

/*
 * Bloom_CreateFilter: Build a Bloom filter from a set of keys.
 * ===========================================================
 * Input: policy, keys array, key count, dst buffer (with len/capacity)
 * Output: void (appends filter to dst buffer)
 * Intent: Create a Bloom filter that will return true for any of the input
 *         keys. Uses double hashing to generate K bit positions per key.
 *         The filter is appended to the dst buffer for storage.
 */
static void Bloom_CreateFilter(const Lithos_FilterPolicy *policy,
                               const Lithos_Slice *keys, int n, char **dst,
                               size_t *dst_len, size_t *dst_capacity) {
  BloomState *state = (BloomState *)policy->state;

  /* Compute bloom filter size (in bits and bytes) */
  size_t bits = n * state->bits_per_key;

  /* Enforce minimum size */
  if (bits < 64) {
    bits = 64;
  }

  size_t bytes = (bits + 7) / 8;
  bits = bytes * 8; /* Round up to byte boundary */

  /* Ensure dst has enough space */
  size_t old_size = *dst_len;
  size_t new_size = old_size + bytes + 1; /* +1 for k value */

  if (new_size > *dst_capacity) {
    size_t new_capacity = *dst_capacity * 2;
    if (new_capacity < new_size) {
      new_capacity = new_size;
    }
    char *new_dst = realloc(*dst, new_capacity);
    if (!new_dst) {
      return; /* Out of memory - caller must check */
    }
    *dst = new_dst;
    *dst_capacity = new_capacity;
  }

  /* Initialize filter bits to zero */
  char *array = *dst + old_size;
  memset(array, 0, bytes);

  /* Add each key to the filter */
  for (int i = 0; i < n; i++) {
    uint32_t h = BloomHash(keys[i]); // Base hash for this key
    const uint32_t delta =
        (h >> 17) | (h << 15); // Second hash for double hashing

    /* Set k bits using double hashing: h, h+delta, h+2*delta, ... */
    for (int j = 0; j < state->k; j++) {
      const uint32_t bitpos = h % bits;         // Map hash to bit position
      array[bitpos / 8] |= (1 << (bitpos % 8)); // Set the bit
      h += delta;                               // Next hash in the sequence
    }
  }

  /* Append k value to the end (needed for KeyMayMatch) */
  array[bytes] = (char)state->k;
  *dst_len = new_size;
}

/*
 * Bloom_KeyMayMatch: Check if a key might be in the filter.
 * =========================================================
 * Input: policy, key to check, filter data slice
 * Output: bool (true = "might contain", false = "definitely doesn't contain")
 * Intent: Test if all K hash bits are set for the key. False means the key
 *         is definitely not in the original set. True means it might be (or
 *         it's a false positive).
 */
static bool Bloom_KeyMayMatch(const Lithos_FilterPolicy *policy,
                              Lithos_Slice key, Lithos_Slice filter) {
  (void)policy;

  size_t len = filter.size;
  if (len < 2) {
    return false; /* Malformed filter */
  }

  const char *array = filter.data;
  const size_t bits = (len - 1) * 8; // Total bits in filter (excluding k byte)

  /* Extract k from the last byte */
  const int k = array[len - 1];
  if (k > 30) {
    /* Reserved for potentially new encodings - conservatively return true */
    return true;
  }

  /* Check if all k bits are set for this key */
  uint32_t h = BloomHash(key);
  const uint32_t delta = (h >> 17) | (h << 15);

  for (int j = 0; j < k; j++) {
    const uint32_t bitpos = h % bits;
    if ((array[bitpos / 8] & (1 << (bitpos % 8))) == 0) {
      return false; /* At least one bit unset - definitely not present */
    }
    h += delta; // Check next hash position
  }

  return true; /* All bits set - might be present (or false positive) */
}

static const Lithos_FilterPolicyVTable bloom_vtable = {
    .Name = Bloom_Name,
    .CreateFilter = Bloom_CreateFilter,
    .KeyMayMatch = Bloom_KeyMayMatch};

/*
 * Public API
 */
const Lithos_FilterPolicy *NewBloomFilterPolicy(int bits_per_key) {
  Lithos_FilterPolicy *policy = malloc(sizeof(Lithos_FilterPolicy));
  if (!policy) {
    return NULL;
  }

  BloomState *state = malloc(sizeof(BloomState));
  if (!state) {
    free(policy);
    return NULL;
  }

  state->bits_per_key = bits_per_key;
  /* Optimal k = 0.69 * bits_per_key */
  state->k = (int)(bits_per_key * 0.69);
  if (state->k < 1)
    state->k = 1;
  if (state->k > 30)
    state->k = 30;

  policy->vtable = &bloom_vtable;
  policy->state = state;

  return policy;
}

void FilterPolicy_Destroy(const Lithos_FilterPolicy *policy) {
  if (policy) {
    free(policy->state);
    free((void *)policy);
  }
}
