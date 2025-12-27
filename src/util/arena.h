
#ifndef LITHOS_UTIL_ARENA_H
#define LITHOS_UTIL_ARENA_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Lithos_Arena Lithos_Arena;

Lithos_Arena *Arena_Create(void);

void Arena_Destroy(Lithos_Arena *arena);

char *Arena_Allocate(Lithos_Arena *arena, size_t bytes);

char *Arena_AllocateAligned(Lithos_Arena *arena, size_t bytes);

size_t Arena_MemoryUsage(Lithos_Arena *arena);

#ifdef __cplusplus
}
#endif

#endif
