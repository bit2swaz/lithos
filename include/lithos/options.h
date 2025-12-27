
#ifndef LITHOS_OPTIONS_H
#define LITHOS_OPTIONS_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Comparator Comparator;

typedef struct Lithos_FilterPolicy Lithos_FilterPolicy;

typedef struct Lithos_Cache Lithos_Cache;

typedef struct Lithos_Options {
  size_t block_restart_interval;
  size_t block_size;
  const Comparator *comparator;
  const Lithos_FilterPolicy *filter_policy;
  Lithos_Cache *block_cache;
  bool compression_enabled;
} Lithos_Options;

void Lithos_Options_InitDefault(Lithos_Options *opt);

#ifdef __cplusplus
}
#endif

#endif
