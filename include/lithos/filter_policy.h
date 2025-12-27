/*
 * Filter Policy Interface: Pluggable Bloom Filters for SSTables
 * ============================================================
 * Defines the abstraction for probabilistic filters that enable fast
 * negative lookups in SSTable blocks.
 *
 * Big Picture: Filter Policy = "Strategy Pattern for Fast 'No' Answers"
 * ====================================================================
 * SSTable blocks contain thousands of keys. To avoid reading blocks that
 * don't contain a key, we use probabilistic filters. The FilterPolicy
 * interface allows different implementations (Bloom, Cuckoo, etc.) to be
 * plugged in seamlessly.
 *
 * Where it fits: Filter policies are used by TableBuilder to create filters
 * and by Table readers to check if keys might exist before disk access.
 *
 * Key Concepts:
 * - Strategy pattern: Different filter algorithms can be swapped.
 * - Probabilistic: "Might contain" vs "definitely doesn't contain".
 * - Pluggable: Users can provide custom filter implementations.
 * - Block-based: Filters cover ranges of data for granularity.
 */

#ifndef LITHOS_FILTER_POLICY_H
#define LITHOS_FILTER_POLICY_H

#include "util/slice.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Opaque filter policy type.
 * Implementations provide concrete strategies for filtering.
 */
typedef struct Lithos_FilterPolicy Lithos_FilterPolicy;

/*
 * FilterPolicy VTable - Strategy Pattern
 *
 * This struct defines the interface that all filter implementations
 * must provide. Similar to our Iterator VTable pattern.
 */
typedef struct {
  /*
   * Name - Returns human-readable name of the filter.
   * Used for debugging and to verify filter compatibility when opening files.
   */
  const char *(*Name)(const Lithos_FilterPolicy *policy);

  /*
   * CreateFilter - Build a filter from a list of keys.
   *
   * Parameters:
   *   policy - The filter policy instance
   *   keys   - Array of key slices
   *   n      - Number of keys in the array
   *   dst    - Output buffer to append filter data to
   *
   * The filter is appended to *dst, which may contain existing data.
   * This allows multiple filters to be concatenated.
   */
  void (*CreateFilter)(const Lithos_FilterPolicy *policy,
                       const Lithos_Slice *keys, int n, char **dst,
                       size_t *dst_len, size_t *dst_capacity);

  /*
   * KeyMayMatch - Check if a key might be in the filter.
   *
   * Parameters:
   *   policy - The filter policy instance
   *   key    - The key to check
   *   filter - The filter data (as created by CreateFilter)
   *
   * Returns:
   *   true  - Key MIGHT be in the data set (could be false positive)
   *   false - Key is DEFINITELY NOT in the data set (no false negatives)
   *
   * Critical Property: This must never return false for a key that
   * was passed to CreateFilter. False positives are acceptable,
   * false negatives are not.
   */
  bool (*KeyMayMatch)(const Lithos_FilterPolicy *policy, Lithos_Slice key,
                      Lithos_Slice filter);
} Lithos_FilterPolicyVTable;

/*
 * Concrete FilterPolicy instance
 */
struct Lithos_FilterPolicy {
  const Lithos_FilterPolicyVTable *vtable;
  void *state; /* Implementation-specific data */
};

/*
 * NewBloomFilterPolicy - Create a Bloom filter policy.
 *
 * Parameters:
 *   bits_per_key - Number of bits to use per key in the filter.
 *                  Higher values reduce false positive rate but increase
 *                  filter size.
 *
 * Recommended values:
 *   10 bits/key -> ~1% false positive rate
 *   16 bits/key -> ~0.1% false positive rate
 *   20 bits/key -> ~0.01% false positive rate
 *
 * Returns: A new FilterPolicy instance that must be freed with
 *          FilterPolicy_Destroy() when done.
 *
 * Implementation Note:
 *   The optimal number of hash functions k = (bits/key) * ln(2) ≈ 0.69 *
 * bits/key
 */
const Lithos_FilterPolicy *NewBloomFilterPolicy(int bits_per_key);

/*
 * FilterPolicy_Destroy - Free a filter policy instance.
 */
void FilterPolicy_Destroy(const Lithos_FilterPolicy *policy);

/*
 * Helper wrappers for cleaner calling syntax
 */
static inline const char *FilterPolicy_Name(const Lithos_FilterPolicy *policy) {
  return policy->vtable->Name(policy);
}

static inline void FilterPolicy_CreateFilter(const Lithos_FilterPolicy *policy,
                                             const Lithos_Slice *keys, int n,
                                             char **dst, size_t *dst_len,
                                             size_t *dst_capacity) {
  policy->vtable->CreateFilter(policy, keys, n, dst, dst_len, dst_capacity);
}

static inline bool FilterPolicy_KeyMayMatch(const Lithos_FilterPolicy *policy,
                                            Lithos_Slice key,
                                            Lithos_Slice filter) {
  return policy->vtable->KeyMayMatch(policy, key, filter);
}

#ifdef __cplusplus
}
#endif

#endif /* LITHOS_FILTER_POLICY_H */
