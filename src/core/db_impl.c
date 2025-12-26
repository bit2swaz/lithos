/* Minimal DB implementation: WAL + MemTable with crash recovery. */

#include "lithos/db.h"
#include "util/env.h"
#include "core/log_writer.h"
#include "core/log_reader.h"
#include "core/memtable.h"
#include "lithos/read_options.h"
#include "lithos/lookup_key.h"
#include <pthread.h>
#include <sys/stat.h>
#include <string.h>
#include "core/version_set.h"
#include <stdlib.h>
#include <stdio.h>

struct Lithos_DB {
    Lithos_Options options;
    char* dbname;
    Lithos_MemTable* mem;
    Lithos_MemTable* imm;
    Lithos_WritableFile* logfile;
    LogWriter* log;
    SequenceNumber last_sequence;
    pthread_mutex_t mu;
    Lithos_VersionSet* versions;
};

static char* DupString(const char* src) {
    if (src == NULL) return NULL;
    size_t n = strlen(src);
    char* r = malloc(n + 1);
    if (r != NULL) memcpy(r, src, n + 1);
    return r;
}

static char* WalFileName(const char* dbname) {
    size_t n = strlen(dbname) + 8; /* "/" + wal.log + nul */
    char* buf = malloc(n);
    if (buf == NULL) return NULL;
    snprintf(buf, n, "%s/wal.log", dbname);
    return buf;
}

static bool FileExists(const char* fname) {
    struct stat st;
    return stat(fname, &st) == 0;
}

static Status MakeRoomForWrite(Lithos_DB* db) {
    (void)db;
    return Status_OK();
}

static Status AppendToLog(Lithos_DB* db, Lithos_WriteBatch* batch, Lithos_WriteOptions options) {
    Lithos_Slice record = {batch->rep, batch->size};
    Status s = LogWriter_AddRecord(db->log, record);
    if (!Status_IsOK(s)) {
        return s;
    }
    if (options.sync) {
        s = WritableFile_Flush(db->logfile);
        if (!Status_IsOK(s)) return s;
        s = WritableFile_Sync(db->logfile);
    }
    return s;
}

typedef struct {
    Lithos_MemTable* mem;
    SequenceNumber seq;
} MemtableInserter;

static Status HandlerPut(void* arg, Lithos_Slice key, Lithos_Slice value) {
    MemtableInserter* ctx = (MemtableInserter*)arg;
    MemTable_Add(ctx->mem, ctx->seq++, kTypeValue, key, value);
    return Status_OK();
}

static Status HandlerDelete(void* arg, Lithos_Slice key) {
    MemtableInserter* ctx = (MemtableInserter*)arg;
    MemTable_Add(ctx->mem, ctx->seq++, kTypeDeletion, key, Slice_Empty());
    return Status_OK();
}

static Status ApplyBatchToMem(Lithos_DB* db, Lithos_WriteBatch* batch) {
    MemtableInserter ctx = {db->mem, WriteBatch_Sequence(batch)};
    WriteBatchHandler handler = {.arg = &ctx, .Put = HandlerPut, .Delete = HandlerDelete};
    Status s = WriteBatch_Iterate(batch, &handler);
    if (Status_IsOK(s)) {
        int count = WriteBatch_Count(batch);
        if (count > 0) {
            db->last_sequence = ctx.seq - 1;
        }
    }
    return s;
}

static Status Recover(Lithos_DB* db, const char* walname) {
    if (!FileExists(walname)) {
        return Status_OK();
    }

    Lithos_SequentialFile* file = NULL;
    Status s = Env_NewSequentialFile(walname, &file);
    if (!Status_IsOK(s)) {
        return s;
    }

    LogReader* reader = LogReader_Create(file, true);
    if (reader == NULL) {
        SequentialFile_Close(file);
        return Status_IOError("alloc log reader", NULL);
    }

    Lithos_Slice record;
    char* scratch = NULL;
    while (LogReader_ReadRecord(reader, &record, &scratch)) {
        Lithos_WriteBatch batch = {0};
        batch.rep = (char*)record.data;
        batch.size = record.size;
        batch.capacity = record.size;
        Status apply = ApplyBatchToMem(db, &batch);
        if (!Status_IsOK(apply)) {
            s = apply;
            break;
        }
    }

    if (scratch) free(scratch);
    LogReader_Destroy(reader);
    SequentialFile_Close(file);
    return s;
}

Lithos_WriteOptions Lithos_WriteOptions_Default(void) {
    Lithos_WriteOptions opt;
    opt.sync = false;
    return opt;
}

Status Lithos_DB_Open(const char* name, const Lithos_Options* options, Lithos_DB** db_out) {
    if (name == NULL || db_out == NULL) {
        return Status_InvalidArgument("invalid args");
    }

    Lithos_Options opt = {0};
    if (options != NULL) {
        opt = *options;
    } else {
        Lithos_Options_InitDefault(&opt);
    }

    mkdir(name, 0755);

    Lithos_DB* db = calloc(1, sizeof(Lithos_DB));
    if (db == NULL) {
        return Status_IOError("alloc db", NULL);
    }
    db->options = opt;
    db->dbname = DupString(name);
    db->mem = MemTable_Create();
    db->imm = NULL;
    db->last_sequence = 0;
    pthread_mutex_init(&db->mu, NULL);

    db->versions = VersionSet_Create(name);

    char* walname = WalFileName(name);
    if (walname == NULL) {
        Lithos_DB_Close(db);
        return Status_IOError("alloc wal name", NULL);
    }

    Status s = Recover(db, walname);
    if (!Status_IsOK(s)) {
        free(walname);
        Lithos_DB_Close(db);
        return s;
    }

    s = Env_NewWritableFile(walname, &db->logfile);
    if (!Status_IsOK(s)) {
        free(walname);
        Lithos_DB_Close(db);
        return s;
    }
    db->log = LogWriter_Create(db->logfile);
    free(walname);
    if (db->log == NULL) {
        Lithos_DB_Close(db);
        return Status_IOError("alloc log writer", NULL);
    }

    *db_out = db;
    return Status_OK();
}

void Lithos_DB_Close(Lithos_DB* db) {
    if (db == NULL) return;
    if (db->versions) {
        VersionSet_Destroy(db->versions);
    }
    if (db->log) {
        LogWriter_Destroy(db->log);
    }
    if (db->logfile) {
        WritableFile_Close(db->logfile);
    }
    if (db->mem) {
        MemTable_Unref(db->mem);
    }
    if (db->imm) {
        MemTable_Unref(db->imm);
    }
    if (db->dbname) free(db->dbname);
    pthread_mutex_destroy(&db->mu);
    free(db);
}

Status Lithos_DB_Write(Lithos_DB* db, Lithos_WriteOptions options, Lithos_WriteBatch* batch) {
    if (db == NULL || batch == NULL) {
        return Status_InvalidArgument("db/write");
    }

    pthread_mutex_lock(&db->mu);

    Status s = MakeRoomForWrite(db);
    if (!Status_IsOK(s)) goto done;

    SequenceNumber start = db->last_sequence + 1;
    WriteBatch_SetSequence(batch, start);

    s = AppendToLog(db, batch, options);
    if (!Status_IsOK(s)) goto done;

    s = ApplyBatchToMem(db, batch);

 done:
    pthread_mutex_unlock(&db->mu);
    return s;
}

Status Lithos_DB_Put(Lithos_DB* db, Lithos_Slice key, Lithos_Slice value) {
    Lithos_WriteBatch* batch = WriteBatch_Create();
    if (batch == NULL) return Status_IOError("alloc batch", NULL);
    Status s = WriteBatch_Put(batch, key, value);
    if (Status_IsOK(s)) {
        s = Lithos_DB_Write(db, Lithos_WriteOptions_Default(), batch);
    }
    WriteBatch_Destroy(batch);
    return s;
}

Status Lithos_DB_Delete(Lithos_DB* db, Lithos_Slice key) {
    Lithos_WriteBatch* batch = WriteBatch_Create();
    if (batch == NULL) return Status_IOError("alloc batch", NULL);
    Status s = WriteBatch_Delete(batch, key);
    if (Status_IsOK(s)) {
        s = Lithos_DB_Write(db, Lithos_WriteOptions_Default(), batch);
    }
    WriteBatch_Destroy(batch);
    return s;
}

Status Lithos_DB_Get(Lithos_DB* db, Lithos_Slice key, char** value_out) {
    if (value_out) *value_out = NULL;
    if (db == NULL) {
        return Status_InvalidArgument("db");
    }
    Status s = Status_OK();

    pthread_mutex_lock(&db->mu);

    /* Active MemTable */
    bool found = MemTable_Get(db->mem, key, value_out, &s);
    if (found) {
        pthread_mutex_unlock(&db->mu);
        return s;
    }

    /* Immutable MemTable */
    if (db->imm != NULL) {
        found = MemTable_Get(db->imm, key, value_out, &s);
        if (found) {
            pthread_mutex_unlock(&db->mu);
            return s;
        }
    }

    /* Disk via VersionSet */
    Lithos_Version* current = NULL;
    if (db->versions) {
        current = db->versions->current;
        Version_Ref(current);
    }

    pthread_mutex_unlock(&db->mu);

    if (current) {
        Lithos_Slice internal_key = key; /* TODO: encode sequence/type for real SST lookup */
        LookupKey lk = LookupKey_Create(key, internal_key);
        bool disk_found = false;
        bool disk_deleted = false;
        Lithos_ReadOptions ro = Lithos_ReadOptions_Default();
        Lithos_Slice disk_value = Slice_Empty();
        s = Version_Get(current, &ro, lk, &disk_value, &disk_found, &disk_deleted);
        if (disk_deleted) {
            s = Status_NotFound(NULL);
        } else if (!disk_found && Status_IsNotFound(s)) {
            s = Status_NotFound(NULL);
        }

        pthread_mutex_lock(&db->mu);
        Version_Unref(current);
        pthread_mutex_unlock(&db->mu);

        if (disk_found && value_out != NULL && disk_value.size > 0) {
            char* copy = malloc(disk_value.size + 1);
            if (copy != NULL) {
                memcpy(copy, disk_value.data, disk_value.size);
                copy[disk_value.size] = '\0';
                *value_out = copy;
            }
        }
        if (disk_found || disk_deleted) {
            return s;
        }
    }

    return Status_NotFound(NULL);
}

SequenceNumber Lithos_DB_LastSequence(Lithos_DB* db) {
    if (db == NULL) return 0;
    return db->last_sequence;
}
