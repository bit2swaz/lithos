
#ifndef LITHOS_READ_OPTIONS_H
#define LITHOS_READ_OPTIONS_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Lithos_ReadOptions {
  const void *snapshot;
} Lithos_ReadOptions;

static inline Lithos_ReadOptions Lithos_ReadOptions_Default(void) {
  Lithos_ReadOptions opt;
  opt.snapshot = NULL;
  return opt;
}

#ifdef __cplusplus
}
#endif

#endif
