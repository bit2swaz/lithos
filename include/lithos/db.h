/* Public database API for Lithos. */

#ifndef LITHOS_DB_H
#define LITHOS_DB_H

#include "lithos/options.h"
#include "lithos/write_batch.h"
#include "util/slice.h"
#include "util/status.h"
#include "core/dbformat.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Lithos_DB Lithos_DB;

typedef struct Lithos_WriteOptions {
    bool sync; /* If true, fsync WAL on every write. */
} Lithos_WriteOptions;

Lithos_WriteOptions Lithos_WriteOptions_Default(void);

Status Lithos_DB_Open(const char* name, const Lithos_Options* options, Lithos_DB** db_out);
void Lithos_DB_Close(Lithos_DB* db);
Status Lithos_DB_Put(Lithos_DB* db, Lithos_Slice key, Lithos_Slice value);
Status Lithos_DB_Delete(Lithos_DB* db, Lithos_Slice key);
Status Lithos_DB_Write(Lithos_DB* db, Lithos_WriteOptions options, Lithos_WriteBatch* batch);
Status Lithos_DB_Get(Lithos_DB* db, Lithos_Slice key, char** value_out);
SequenceNumber Lithos_DB_LastSequence(Lithos_DB* db);

#ifdef __cplusplus
}
#endif

#endif /* LITHOS_DB_H */
