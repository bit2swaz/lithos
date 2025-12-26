/* Public one-stop header for Lithos. */
#ifndef LITHOS_H
#define LITHOS_H

#include <stddef.h>
#include <stdbool.h>
#include "lithos/options.h"
#include "lithos/db.h"
#include "util/status.h"
#include "util/slice.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Lithos_DB Lithos_DB;
typedef struct Lithos_Snapshot Lithos_Snapshot;

typedef void (*Lithos_ScanCallback)(const char* key, const char* value, void* arg);

Status Lithos_Open(const char* dbpath, const Lithos_Options* options, Lithos_DB** db_out);
Status Lithos_Put(Lithos_DB* db, const char* key, const char* value);
Status Lithos_Get(Lithos_DB* db, const char* key, const Lithos_Snapshot* snapshot, char** value_out);
const Lithos_Snapshot* Lithos_GetSnapshot(Lithos_DB* db);
void Lithos_ReleaseSnapshot(Lithos_DB* db, const Lithos_Snapshot* snapshot);
Status Lithos_Delete(Lithos_DB* db, const char* key);
Status Lithos_Scan(Lithos_DB* db, Lithos_ScanCallback cb, void* arg);
void Lithos_Close(Lithos_DB* db);
void Lithos_Free(void* ptr);

#ifdef __cplusplus
}
#endif

#endif /* LITHOS_H */
