
#ifndef LITHOS_CORE_TABLE_TABLE_BUILDER_H_
#define LITHOS_CORE_TABLE_TABLE_BUILDER_H_

#include "lithos/options.h"
#include "util/env.h"
#include "util/slice.h"
#include "util/status.h"

typedef struct Lithos_TableBuilder Lithos_TableBuilder;

Lithos_TableBuilder *TableBuilder_Create(const Lithos_Options *options,
                                         Lithos_WritableFile *file);

void TableBuilder_Destroy(Lithos_TableBuilder *tb);

lithos_status_code TableBuilder_Add(Lithos_TableBuilder *tb, Lithos_Slice key,
                                    Lithos_Slice value);

lithos_status_code TableBuilder_Finish(Lithos_TableBuilder *tb);

void TableBuilder_Abandon(Lithos_TableBuilder *tb);

uint64_t TableBuilder_FileSize(const Lithos_TableBuilder *tb);

lithos_status_code TableBuilder_Status(const Lithos_TableBuilder *tb);

uint64_t TableBuilder_NumEntries(const Lithos_TableBuilder *tb);

#endif
