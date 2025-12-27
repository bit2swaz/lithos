
#include "lithos/filter_policy.h"
#include "util/coding.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(__GNUC__) && __GNUC__ >= 7
#define LITHOS_FALLTHROUGH __attribute__((fallthrough))
#else
#define LITHOS_FALLTHROUGH ((void)0)
#endif

typedef struct {
  int bits_per_key;
  int k;
} BloomState;

static uint32_t BloomHash(Lithos_Slice key) {
  const uint32_t m = 0xc6a4a793;
  const uint32_t r = 24;
  const char *data = key.data;
  size_t n = key.size;

  uint32_t h = 0xbc9f1d34 ^ (n * m);

  while (n >= 4) {
    uint32_t w = DecodeFixed32(data);
    data += 4;
    n -= 4;
    h += w;
    h *= m;
    h ^= (h >> 16);
  }

  switch (n) {
  case 3:
    h += (unsigned char)(data[2]) << 16;
    LITHOS_FALLTHROUGH;

  case 2:
    h += (unsigned char)(data[1]) << 8;
    LITHOS_FALLTHROUGH;

  case 1:
    h += (unsigned char)(data[0]);
    h *= m;
    h ^= (h >> r);
    break;
  }

  return h;
}

static const char *Bloom_Name(const Lithos_FilterPolicy *policy) {
  (void)policy;
  return "lithos.BuiltinBloomFilter";
}

static void Bloom_CreateFilter(const Lithos_FilterPolicy *policy,
                               const Lithos_Slice *keys, int n, char **dst,
                               size_t *dst_len, size_t *dst_capacity) {
  BloomState *state = (BloomState *)policy->state;

  size_t bits = n * state->bits_per_key;

  if (bits < 64) {
    bits = 64;
  }

  size_t bytes = (bits + 7) / 8;
  bits = bytes * 8;

  size_t old_size = *dst_len;
  size_t new_size = old_size + bytes + 1;

  if (new_size > *dst_capacity) {
    size_t new_capacity = *dst_capacity * 2;
    if (new_capacity < new_size) {
      new_capacity = new_size;
    }
    char *new_dst = realloc(*dst, new_capacity);
    if (!new_dst) {
      return;
    }
    *dst = new_dst;
    *dst_capacity = new_capacity;
  }

  char *array = *dst + old_size;
  memset(array, 0, bytes);

  for (int i = 0; i < n; i++) {
    uint32_t h = BloomHash(keys[i]);
    const uint32_t delta =
        (h >> 17) | (h << 15);

    for (int j = 0; j < state->k; j++) {
      const uint32_t bitpos = h % bits;
      array[bitpos / 8] |= (1 << (bitpos % 8));
      h += delta;
    }
  }

  array[bytes] = (char)state->k;
  *dst_len = new_size;
}

static bool Bloom_KeyMayMatch(const Lithos_FilterPolicy *policy,
                              Lithos_Slice key, Lithos_Slice filter) {
  (void)policy;

  size_t len = filter.size;
  if (len < 2) {
    return false;
  }

  const char *array = filter.data;
  const size_t bits = (len - 1) * 8;

  const int k = array[len - 1];
  if (k > 30) {

    return true;
  }

  uint32_t h = BloomHash(key);
  const uint32_t delta = (h >> 17) | (h << 15);

  for (int j = 0; j < k; j++) {
    const uint32_t bitpos = h % bits;
    if ((array[bitpos / 8] & (1 << (bitpos % 8))) == 0) {
      return false;
    }
    h += delta;
  }

  return true;
}

static const Lithos_FilterPolicyVTable bloom_vtable = {
    .Name = Bloom_Name,
    .CreateFilter = Bloom_CreateFilter,
    .KeyMayMatch = Bloom_KeyMayMatch};

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
