
#include "lithos/options.h"
#include <stddef.h>

void Lithos_Options_InitDefault(Lithos_Options *opt) {
  opt->block_restart_interval = 16;
  opt->block_size = 4096;
  opt->comparator = NULL;
  opt->filter_policy = NULL;
  opt->block_cache = NULL;
  opt->compression_enabled = false;
}
