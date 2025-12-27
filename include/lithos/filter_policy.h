
#ifndef LITHOS_FILTER_POLICY_H
#define LITHOS_FILTER_POLICY_H

#include "util/slice.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Lithos_FilterPolicy Lithos_FilterPolicy;

typedef struct {

  const char *(*Name)(const Lithos_FilterPolicy *policy);

  void (*CreateFilter)(const Lithos_FilterPolicy *policy,
                       const Lithos_Slice *keys, int n, char **dst,
                       size_t *dst_len, size_t *dst_capacity);

  bool (*KeyMayMatch)(const Lithos_FilterPolicy *policy, Lithos_Slice key,
                      Lithos_Slice filter);
} Lithos_FilterPolicyVTable;

struct Lithos_FilterPolicy {
  const Lithos_FilterPolicyVTable *vtable;
  void *state;
};

const Lithos_FilterPolicy *NewBloomFilterPolicy(int bits_per_key);

void FilterPolicy_Destroy(const Lithos_FilterPolicy *policy);

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

#endif
