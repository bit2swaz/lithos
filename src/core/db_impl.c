/* Minimal DB implementation: WAL + MemTable with crash recovery. */

#include "lithos.h"
#include "lithos/db.h"
#include "util/env.h"
#include "core/log_writer.h"
#include "core/log_reader.h"
#include "core/memtable.h"
#include "core/skiplist.h"
#include "lithos/read_options.h"
#include "lithos/lookup_key.h"
#include "core/table/table_builder.h"
#include "core/table/table.h"
#include "core/table/merger.h"
#include "util/coding.h"
#include "core/dbformat.h"
#include <pthread.h>
#include <sys/stat.h>
#include <string.h>
#include "core/version_set.h"
#include <stdlib.h>
#include <stdio.h>

typedef struct Lithos_Snapshot {
    SequenceNumber sequence;
    struct Lithos_Snapshot* prev;
    struct Lithos_Snapshot* next;
} Lithos_Snapshot;

typedef struct SnapshotList {
    Lithos_Snapshot head; /* sentinel node */
} SnapshotList;

struct Lithos_DB {
    Lithos_Options options;
    char* dbname;
    Lithos_MemTable* mem;
    Lithos_MemTable* imm;
    Lithos_WritableFile* logfile;
    LogWriter* log;
    char* imm_log_filename;
    SequenceNumber last_sequence;
    SnapshotList snapshots;
    SequenceNumber oldest_snapshot;
    pthread_mutex_t mu;
    pthread_cond_t bg_cv;
    bool bg_running;
    bool shutting_down;
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
    size_t n = strlen(dbname) + 9; /* "/" + wal.log + nul */
    char* buf = malloc(n);
    if (buf == NULL) return NULL;
    snprintf(buf, n, "%s/wal.log", dbname);
    return buf;
}

static char* ImmWalFileName(const char* dbname) {
    size_t n = strlen(dbname) + 13; /* "/" + wal.log.imm + nul */
    char* buf = malloc(n);
    if (buf == NULL) return NULL;
    snprintf(buf, n, "%s/wal.log.imm", dbname);
    return buf;
}

static char* TableFileName(const char* dbname, uint64_t number) {
    size_t needed = (size_t)snprintf(NULL, 0, "%s/%06llu.sst", dbname,
                                     (unsigned long long)number) + 1;
    char* buf = malloc(needed);
    if (buf == NULL) return NULL;
    snprintf(buf, needed, "%s/%06llu.sst", dbname, (unsigned long long)number);
    return buf;
}

static bool FileExists(const char* fname) {
    struct stat st;
    return stat(fname, &st) == 0;
}

static void DeleteFileQuietly(const char* fname) {
    if (fname == NULL) return;
    Status s = Env_DeleteFile(fname);
    Status_Free(s);
}

static void SnapshotList_Init(SnapshotList* list) {
    list->head.next = &list->head;
    list->head.prev = &list->head;
    list->head.sequence = 0;
}

static bool SnapshotList_Empty(const SnapshotList* list) {
    return list->head.next == &list->head;
}

static Lithos_Snapshot* SnapshotList_New(SnapshotList* list, SequenceNumber seq) {
    Lithos_Snapshot* s = (Lithos_Snapshot*)malloc(sizeof(Lithos_Snapshot));
    if (s == NULL) return NULL;
    s->sequence = seq;
    /* Insert at front: newest snapshots near head, tail holds oldest. */
    s->next = list->head.next;
    s->prev = &list->head;
    list->head.next->prev = s;
    list->head.next = s;
    if (list->head.prev == &list->head) {
        list->head.prev = s;
    }
    return s;
}

static void SnapshotList_Remove(SnapshotList* list, Lithos_Snapshot* s) {
    if (s == NULL || s == &list->head) return;
    if (s->next) s->next->prev = s->prev;
    if (s->prev) s->prev->next = s->next;
    if (list->head.prev == s) {
        list->head.prev = (s->prev != &list->head) ? s->prev : &list->head;
    }
    free(s);
}

static SequenceNumber SnapshotList_Oldest(const SnapshotList* list, SequenceNumber current_seq) {
    if (SnapshotList_Empty(list)) return current_seq + 1; /* No snapshots → allow full pruning. */
    return list->head.prev->sequence;
}

static void UpdateOldestSnapshot(Lithos_DB* db) {
    db->oldest_snapshot = SnapshotList_Oldest(&db->snapshots, db->last_sequence);
}

static Lithos_Slice EncodeInternalKey(Lithos_Slice user_key,
                                      SequenceNumber seq,
                                      ValueType type,
                                      char* scratch,
                                      size_t scratch_sz,
                                      char** heap_out) {
    if (heap_out) *heap_out = NULL;
    size_t needed = user_key.size + sizeof(uint64_t);
    char* buf = NULL;
    if (needed <= scratch_sz) {
        buf = scratch;
    } else {
        buf = (char*)malloc(needed);
        if (heap_out) *heap_out = buf;
        if (buf == NULL) return Slice_Empty();
    }

    memcpy(buf, user_key.data, user_key.size);
    EncodeFixed64(buf + user_key.size, PackSequenceAndType(seq, type));
    return Slice_Create(buf, needed);
}

static const size_t kWriteBufferSize = 4 * 1024 * 1024; /* 4MB buffer target */

static Lithos_Slice EntryInternalKey(const Lithos_Slice* encoded) {
    const char* p = encoded->data;
    const char* limit = p + encoded->size;
    uint32_t internal_key_size = 0;
    const char* after = GetVarint32Ptr(p, limit, &internal_key_size);
    if (after == NULL || after + internal_key_size > limit) {
        return Slice_Empty();
    }
    return Slice_Create(after, internal_key_size);
}

static Lithos_Slice EntryValue(const Lithos_Slice* encoded) {
    const char* p = encoded->data;
    const char* limit = p + encoded->size;
    uint32_t internal_key_size = 0;
    p = GetVarint32Ptr(p, limit, &internal_key_size);
    if (p == NULL || p + internal_key_size > limit) {
        return Slice_Empty();
    }
    p += internal_key_size;
    uint32_t value_size = 0;
    p = GetVarint32Ptr(p, limit, &value_size);
    if (p == NULL || p + value_size > limit) {
        return Slice_Empty();
    }
    return Slice_Create(p, value_size);
}

static void MaybeScheduleCompaction(Lithos_DB* db);
static void* BackgroundCall(void* arg);
static Status CompactMemTable(Lithos_DB* db);
static Status DoCompactionWork(Lithos_DB* db, Lithos_Compaction* c);

static Status WriteLevel0Table(Lithos_DB* db,
                               Lithos_MemTable* mem,
                               FileMetaData** meta_out,
                               char** fname_out) {
    Status s = Status_OK();

    uint64_t file_number = VersionSet_NewFileNumber(db->versions);
    char* fname = TableFileName(db->dbname, file_number);
    if (fname == NULL) {
        return Status_IOError("alloc table filename", NULL);
    }

    Lithos_WritableFile* file = NULL;
    s = Env_NewWritableFile(fname, &file);
    if (!Status_IsOK(s)) {
        free(fname);
        return s;
    }

    Lithos_TableBuilder* builder = TableBuilder_Create(&db->options, file);
    if (builder == NULL) {
        WritableFile_Close(file);
        free(fname);
        return Status_IOError("alloc table builder", NULL);
    }

    Lithos_Iterator* iter = MemTable_NewIterator(mem);
    if (iter == NULL) {
        TableBuilder_Destroy(builder);
        WritableFile_Close(file);
        free(fname);
        return Status_IOError("alloc memtable iter", NULL);
    }

    Iter_SeekToFirst(iter);

    bool has_key = false;
    Lithos_Slice smallest = Slice_Empty();
    Lithos_Slice largest = Slice_Empty();

    while (Iter_Valid(iter)) {
        const Lithos_Slice* entry = (const Lithos_Slice*)Iter_Key(iter);
        Lithos_Slice ikey = EntryInternalKey(entry);
        Lithos_Slice value = EntryValue(entry);
        if (ikey.data == NULL) {
            s = Status_Corruption("corrupted memtable key", NULL);
            break;
        }
        if (!has_key) {
            smallest = ikey;
            has_key = true;
        }
        largest = ikey;

        lithos_status_code add_status = TableBuilder_Add(builder, ikey, value);
        if (add_status != LITHOS_OK) {
            s = Status_IOError("table builder add", NULL);
            break;
        }

        Iter_Next(iter);
    }

    if (Status_IsOK(s)) {
        lithos_status_code finish_status = TableBuilder_Finish(builder);
        if (finish_status != LITHOS_OK) {
            s = Status_IOError("table builder finish", NULL);
        }
    }

    uint64_t file_size = TableBuilder_FileSize(builder);
    TableBuilder_Destroy(builder);
    Status close_status = WritableFile_Close(file);
    if (Status_IsOK(s) && !Status_IsOK(close_status)) {
        s = close_status;
    }

    Iter_Destroy(iter);

    if (!Status_IsOK(s)) {
        DeleteFileQuietly(fname);
        free(fname);
        return s;
    }

    FileMetaData* meta = FileMetaData_Create(file_number, file_size, smallest, largest);
    if (meta_out) {
        *meta_out = meta;
    }
    if (fname_out) {
        *fname_out = fname;
    } else {
        free(fname);
    }

    return Status_OK();
}

static Status CompactMemTable(Lithos_DB* db) {
    if (db->imm == NULL) {
        return Status_OK();
    }

    Lithos_MemTable* imm = db->imm;
    MemTable_Ref(imm);

    char* imm_log = db->imm_log_filename;
    db->imm_log_filename = NULL;

    pthread_mutex_unlock(&db->mu);

    FileMetaData* meta = NULL;
    char* fname = NULL;
    Status s = WriteLevel0Table(db, imm, &meta, &fname);

    pthread_mutex_lock(&db->mu);

    if (!Status_IsOK(s)) {
        if (db->imm_log_filename == NULL && imm_log != NULL) {
            db->imm_log_filename = imm_log;
        }
        MemTable_Unref(imm);
        return s;
    }

    VersionEdit edit;
    VersionEdit_Init(&edit);
    VersionEdit_AddFile(&edit, 0, meta->number, meta->file_size, meta->smallest, meta->largest);
    VersionEdit_SetNextFileNumber(&edit, db->versions->next_file_number);

    Status apply_status = VersionSet_LogAndApply(db->versions, &edit);
    VersionEdit_Clear(&edit);

    if (!Status_IsOK(apply_status)) {
        if (fname != NULL) {
            DeleteFileQuietly(fname);
            free(fname);
        }
        if (imm_log != NULL && db->imm_log_filename == NULL) {
            db->imm_log_filename = imm_log;
        }
        MemTable_Unref(imm);
        return apply_status;
    }

    MemTable_Unref(imm);
    db->imm = NULL;

    if (imm_log != NULL) {
        DeleteFileQuietly(imm_log);
        free(imm_log);
    }
    if (fname != NULL) {
        free(fname);
    }

    return Status_OK();
}

static bool UserKeyInFile(FileMetaData* f, Lithos_Slice user_key) {
    Lithos_Slice s = ExtractUserKey(f->smallest);
    Lithos_Slice l = ExtractUserKey(f->largest);
    return Slice_Compare(user_key, s) >= 0 && Slice_Compare(user_key, l) <= 0;
}

static bool KeyExistsInHigherLevels(Lithos_Compaction* c, Lithos_Slice user_key) {
    if (c == NULL || c->vset == NULL || c->vset->current == NULL) return false;
    Lithos_Version* v = c->vset->current;
    int level = c->level + 2; /* check levels above output */
    for (; level < kNumLevels; level++) {
        for (size_t i = 0; i < v->file_counts[level]; i++) {
            if (UserKeyInFile(v->files[level][i], user_key)) {
                return true;
            }
        }
    }
    return false;
}

static Status DoCompactionWork(Lithos_DB* db, Lithos_Compaction* c) {
    if (c == NULL) return Status_OK();

    /* Trivial move: just rewrite metadata without I/O. */
    if (c->trivial_move && c->input_count[0] == 1 && c->input_count[1] == 0) {
        VersionEdit edit;
        VersionEdit_Init(&edit);
        VersionEdit_DeleteFile(&edit, c->level, c->inputs[0][0]->number);
        VersionEdit_AddFile(&edit,
                            c->level + 1,
                            c->inputs[0][0]->number,
                            c->inputs[0][0]->file_size,
                            c->inputs[0][0]->smallest,
                            c->inputs[0][0]->largest);
        VersionEdit_SetNextFileNumber(&edit, db->versions->next_file_number);
        Status s = VersionSet_LogAndApply(db->versions, &edit);
        VersionEdit_Clear(&edit);
        return s;
    }

    /* Build child iterators */
    int total_inputs = (int)(c->input_count[0] + c->input_count[1]);
    if (total_inputs == 0) return Status_OK();

    Lithos_Iterator** child_iters = calloc((size_t)total_inputs, sizeof(Lithos_Iterator*));
    if (!child_iters) {
        free(child_iters);
        return Status_IOError("alloc compaction iters", NULL);
    }

    int idx = 0;
    Status s = Status_OK();
    for (int which = 0; which < 2 && Status_IsOK(s); which++) {
        for (size_t i = 0; i < c->input_count[which]; i++) {
            child_iters[idx] = TableCache_NewIterator(db->versions->table_cache,
                                                      c->inputs[which][i],
                                                      &db->options);
            if (child_iters[idx] == NULL) {
                s = Status_IOError("table cache iterator", NULL);
                break;
            }
            idx++;
        }
    }

    if (!Status_IsOK(s)) {
        for (int i = 0; i < idx; i++) {
            if (child_iters[i]) Lithos_Iter_Destroy(child_iters[i]);
        }
        free(child_iters);
        return s;
    }

    Lithos_Iterator* merge = NewMergingIterator(child_iters, total_inputs, InternalKeyComparator);
    if (merge == NULL) {
        for (int i = 0; i < total_inputs; i++) {
            if (child_iters[i]) Lithos_Iter_Destroy(child_iters[i]);
        }
        free(child_iters);
        return Status_IOError("alloc merging iterator", NULL);
    }

    Lithos_TableBuilder* builder = NULL;
    Lithos_WritableFile* outfile = NULL;
    FileMetaData* meta = NULL;
    char* outname = NULL;

    uint64_t file_number = VersionSet_NewFileNumber(db->versions);
    outname = TableFileName(db->dbname, file_number);
    if (outname == NULL) {
        s = Status_IOError("alloc table filename", NULL);
        goto done;
    }

    s = Env_NewWritableFile(outname, &outfile);
    if (!Status_IsOK(s)) goto done;

    builder = TableBuilder_Create(&db->options, outfile);
    if (builder == NULL) {
        s = Status_IOError("alloc table builder", NULL);
        goto done;
    }

    Lithos_Slice smallest = Slice_Empty();
    Lithos_Slice largest = Slice_Empty();
    uint64_t file_size = 0;
    bool has_key = false;
    Lithos_Slice last_user = Slice_Empty();
    char last_buf[256];
    SequenceNumber last_sequence_for_key = kMaxSequenceNumber;
    SequenceNumber smallest_snapshot = db->oldest_snapshot == 0 ? db->last_sequence + 1
                                                                : db->oldest_snapshot;

    Lithos_Iter_SeekToFirst(merge);
    while (Lithos_Iter_Valid(merge)) {
        Lithos_Slice ikey = Lithos_Iter_Key(merge);
        Lithos_Slice value = Lithos_Iter_Value(merge);

        ParsedInternalKey parsed;
        if (!ParseInternalKey(ikey, &parsed)) {
            s = Status_Corruption("bad internal key", NULL);
            break;
        }

        bool is_new_user = (last_user.data == NULL) || (Slice_Compare(last_user, parsed.user_key) != 0);
        if (is_new_user) {
            if (last_user.data != last_buf && last_user.data != NULL) free((void*)last_user.data);
            last_sequence_for_key = kMaxSequenceNumber;
        }

        bool drop = false;

        if (!is_new_user && last_sequence_for_key <= smallest_snapshot) {
            drop = true; /* Older than any needed snapshot; newer version already seen. */
        }

        if (!drop && parsed.type == kTypeDeletion && parsed.seq <= smallest_snapshot) {
            if (!KeyExistsInHigherLevels(c, parsed.user_key)) {
                drop = true;
            }
        }

        if (!drop) {
            if (!has_key) {
                smallest = ikey;
                has_key = true;
            }
            largest = ikey;
            lithos_status_code add = TableBuilder_Add(builder, ikey, value);
            if (add != LITHOS_OK) {
                s = Status_IOError("table builder add", NULL);
                break;
            }
        }

        if (is_new_user) {
            if (parsed.user_key.size <= sizeof(last_buf)) {
                memcpy(last_buf, parsed.user_key.data, parsed.user_key.size);
                last_user = Slice_Create(last_buf, parsed.user_key.size);
            } else {
                char* tmp = malloc(parsed.user_key.size);
                if (tmp != NULL) {
                    memcpy(tmp, parsed.user_key.data, parsed.user_key.size);
                    last_user = Slice_Create(tmp, parsed.user_key.size);
                } else {
                    last_user = Slice_Empty();
                }
            }
        }

        last_sequence_for_key = parsed.seq;

        Lithos_Iter_Next(merge);
    }

    if (Status_IsOK(s)) {
        lithos_status_code fin = TableBuilder_Finish(builder);
        if (fin != LITHOS_OK) {
            s = Status_IOError("table builder finish", NULL);
        }
    }

    if (builder) {
        file_size = TableBuilder_FileSize(builder);
        TableBuilder_Destroy(builder);
        builder = NULL;
    }

    if (outfile) {
        Status close_status = WritableFile_Close(outfile);
        if (Status_IsOK(s) && !Status_IsOK(close_status)) s = close_status;
    }

    if (Status_IsOK(s) && has_key) {
        meta = FileMetaData_Create(file_number, file_size, smallest, largest);
    }

done:
    if (last_user.data != last_buf && last_user.data != NULL) {
        free((void*)last_user.data);
    }

    if (!Status_IsOK(s)) {
        if (outfile) WritableFile_Close(outfile);
        if (outname) DeleteFileQuietly(outname);
    }

    /* Build and apply VersionEdit */
    if (Status_IsOK(s) && meta != NULL) {
        VersionEdit edit;
        VersionEdit_Init(&edit);
        for (size_t i = 0; i < c->input_count[0]; i++) {
            VersionEdit_DeleteFile(&edit, c->level, c->inputs[0][i]->number);
        }
        for (size_t i = 0; i < c->input_count[1]; i++) {
            VersionEdit_DeleteFile(&edit, c->level + 1, c->inputs[1][i]->number);
        }
        VersionEdit_AddFile(&edit, c->level + 1, meta->number, meta->file_size, meta->smallest, meta->largest);
        VersionEdit_SetNextFileNumber(&edit, db->versions->next_file_number);
        Status apply = VersionSet_LogAndApply(db->versions, &edit);
        VersionEdit_Clear(&edit);
        if (!Status_IsOK(apply)) {
            s = apply;
        }
    }

    /* Cleanup */
    Lithos_Iter_Destroy(merge);
    for (int i = 0; i < total_inputs; i++) {
        if (child_iters[i]) Lithos_Iter_Destroy(child_iters[i]);
    }
    free(child_iters);
    if (outname) free(outname);
    return s;
}

static void* BackgroundCall(void* arg) {
    Lithos_DB* db = (Lithos_DB*)arg;

    pthread_mutex_lock(&db->mu);
    if (db->imm != NULL) {
        (void)CompactMemTable(db);
    } else if (VersionSet_NeedsCompaction(db->versions)) {
        Lithos_Compaction* c = VersionSet_PickCompaction(db->versions);
        if (c != NULL) {
            (void)DoCompactionWork(db, c);
            Compaction_Destroy(c);
        }
    }
    db->bg_running = false;
    pthread_cond_broadcast(&db->bg_cv);
    pthread_mutex_unlock(&db->mu);

    return NULL;
}

static void MaybeScheduleCompaction(Lithos_DB* db) {
    if (db->shutting_down || db->bg_running) {
        return;
    }
    if (db->imm == NULL && !VersionSet_NeedsCompaction(db->versions)) {
        return;
    }

    db->bg_running = true;
    pthread_t t;
    int rc = pthread_create(&t, NULL, BackgroundCall, db);
    if (rc == 0) {
        pthread_detach(t);
    } else {
        db->bg_running = false;
        (void)BackgroundCall(db);
    }
}

static Status MakeRoomForWrite(Lithos_DB* db) {
    for (;;) {
        size_t usage = MemTable_ApproximateMemoryUsage(db->mem);
        if (usage < kWriteBufferSize) {
            return Status_OK();
        }

        if (db->imm != NULL) {
            MaybeScheduleCompaction(db);
            pthread_cond_wait(&db->bg_cv, &db->mu);
            continue;
        }

        Status flush_status = WritableFile_Flush(db->logfile);
        if (!Status_IsOK(flush_status)) {
            return flush_status;
        }
        Status sync_status = WritableFile_Sync(db->logfile);
        if (!Status_IsOK(sync_status)) {
            return sync_status;
        }

        LogWriter_Destroy(db->log);
        db->log = NULL;
        WritableFile_Close(db->logfile);
        db->logfile = NULL;

        char* walname = WalFileName(db->dbname);
        char* immwal = ImmWalFileName(db->dbname);
        if (walname == NULL || immwal == NULL) {
            free(walname);
            free(immwal);
            return Status_IOError("alloc wal name", NULL);
        }

        if (rename(walname, immwal) != 0) {
            free(walname);
            free(immwal);
            return Status_IOError("rotate wal", NULL);
        }

        db->imm_log_filename = immwal;
        free(walname);

        db->imm = db->mem;
        db->mem = MemTable_Create();
        if (db->mem == NULL) {
            return Status_IOError("alloc memtable", NULL);
        }

        char* newwal = WalFileName(db->dbname);
        if (newwal == NULL) {
            return Status_IOError("alloc wal name", NULL);
        }
        Status newfile = Env_NewWritableFile(newwal, &db->logfile);
        free(newwal);
        if (!Status_IsOK(newfile)) {
            return newfile;
        }
        db->log = LogWriter_Create(db->logfile);
        if (db->log == NULL) {
            return Status_IOError("alloc log writer", NULL);
        }

        MaybeScheduleCompaction(db);
        return Status_OK();
    }
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
            UpdateOldestSnapshot(db);
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
    db->imm_log_filename = NULL;
    db->last_sequence = 0;
    SnapshotList_Init(&db->snapshots);
    db->oldest_snapshot = db->last_sequence + 1;
    pthread_mutex_init(&db->mu, NULL);
    pthread_cond_init(&db->bg_cv, NULL);
    db->bg_running = false;
    db->shutting_down = false;

    db->versions = VersionSet_Create(name);

    char* walname = WalFileName(name);
    char* immwal = ImmWalFileName(name);
    if (walname == NULL || immwal == NULL) {
        free(walname);
        free(immwal);
        Lithos_DB_Close(db);
        return Status_IOError("alloc wal name", NULL);
    }

    Status s = Recover(db, immwal);
    if (Status_IsOK(s)) {
        DeleteFileQuietly(immwal);
    }
    free(immwal);
    if (!Status_IsOK(s)) {
        free(walname);
        Lithos_DB_Close(db);
        return s;
    }

    s = Recover(db, walname);
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

    pthread_mutex_lock(&db->mu);
    db->shutting_down = true;
    while (db->bg_running) {
        pthread_cond_wait(&db->bg_cv, &db->mu);
    }
    pthread_mutex_unlock(&db->mu);

    if (db->versions) {
        VersionSet_Destroy(db->versions);
    }
    if (db->log) {
        LogWriter_Destroy(db->log);
    }
    if (db->logfile) {
        WritableFile_Close(db->logfile);
    }
    if (db->imm_log_filename) {
        DeleteFileQuietly(db->imm_log_filename);
        free(db->imm_log_filename);
    }
    Lithos_Snapshot* snap = db->snapshots.head.next;
    while (snap != &db->snapshots.head) {
        Lithos_Snapshot* next = snap->next;
        free(snap);
        snap = next;
    }
    if (db->mem) {
        MemTable_Unref(db->mem);
    }
    if (db->imm) {
        MemTable_Unref(db->imm);
    }
    if (db->dbname) free(db->dbname);
    pthread_mutex_destroy(&db->mu);
    pthread_cond_destroy(&db->bg_cv);
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

    if (Status_IsOK(s)) {
        MaybeScheduleCompaction(db);
    }

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

Status Lithos_DB_Get(Lithos_DB* db, Lithos_Slice key, const Lithos_Snapshot* snapshot, char** value_out) {
    if (value_out) *value_out = NULL;
    if (db == NULL) {
        return Status_InvalidArgument("db");
    }
    Status s = Status_OK();

    pthread_mutex_lock(&db->mu);

    SequenceNumber snapshot_seq = snapshot ? snapshot->sequence : db->last_sequence;

    /* Active MemTable */
    bool found = MemTable_Get(db->mem, key, snapshot_seq, value_out, &s);
    if (found) {
        pthread_mutex_unlock(&db->mu);
        return s;
    }

    /* Immutable MemTable */
    if (db->imm != NULL) {
        found = MemTable_Get(db->imm, key, snapshot_seq, value_out, &s);
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
        char ikey_scratch[256];
        char* ikey_heap = NULL;
        Lithos_Slice internal_key = EncodeInternalKey(key, snapshot_seq, kTypeValue, ikey_scratch, sizeof(ikey_scratch), &ikey_heap);
        if (internal_key.data == NULL) {
            pthread_mutex_lock(&db->mu);
            Version_Unref(current);
            pthread_mutex_unlock(&db->mu);
            return Status_IOError("alloc internal key", NULL);
        }

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

        if (ikey_heap != NULL) free(ikey_heap);

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

const Lithos_Snapshot* Lithos_DB_GetSnapshot(Lithos_DB* db) {
    if (db == NULL) return NULL;
    pthread_mutex_lock(&db->mu);
    Lithos_Snapshot* snap = SnapshotList_New(&db->snapshots, db->last_sequence);
    UpdateOldestSnapshot(db);
    pthread_mutex_unlock(&db->mu);
    return snap;
}

void Lithos_DB_ReleaseSnapshot(Lithos_DB* db, const Lithos_Snapshot* snapshot) {
    if (db == NULL || snapshot == NULL) return;
    pthread_mutex_lock(&db->mu);
    SnapshotList_Remove(&db->snapshots, (Lithos_Snapshot*)snapshot);
    UpdateOldestSnapshot(db);
    pthread_mutex_unlock(&db->mu);
}

SequenceNumber Lithos_DB_LastSequence(Lithos_DB* db) {
    if (db == NULL) return 0;
    return db->last_sequence;
}

Status Lithos_Open(const char* dbpath, const Lithos_Options* options, Lithos_DB** db_out) {
    return Lithos_DB_Open(dbpath, options, db_out);
}

Status Lithos_Put(Lithos_DB* db, const char* key, const char* value) {
    if (db == NULL || key == NULL || value == NULL) {
        return Status_InvalidArgument("put args");
    }
    return Lithos_DB_Put(db, Slice_FromCString(key), Slice_FromCString(value));
}

Status Lithos_Get(Lithos_DB* db, const char* key, const Lithos_Snapshot* snapshot, char** value_out) {
    if (db == NULL || key == NULL || value_out == NULL) {
        return Status_InvalidArgument("get args");
    }
    return Lithos_DB_Get(db, Slice_FromCString(key), snapshot, value_out);
}

const Lithos_Snapshot* Lithos_GetSnapshot(Lithos_DB* db) {
    return Lithos_DB_GetSnapshot(db);
}

void Lithos_ReleaseSnapshot(Lithos_DB* db, const Lithos_Snapshot* snapshot) {
    Lithos_DB_ReleaseSnapshot(db, snapshot);
}

Status Lithos_Delete(Lithos_DB* db, const char* key) {
    if (db == NULL || key == NULL) {
        return Status_InvalidArgument("delete args");
    }
    return Lithos_DB_Delete(db, Slice_FromCString(key));
}

static void UnrefFiles(FileMetaData** files, size_t count) {
    if (!files) return;
    for (size_t i = 0; i < count; i++) {
        if (files[i]) FileMetaData_Unref(files[i]);
    }
}

Status Lithos_Scan(Lithos_DB* db, Lithos_ScanCallback cb, void* arg) {
    if (db == NULL || cb == NULL) {
        return Status_InvalidArgument("scan args");
    }

    Lithos_MemTable* mem = NULL;
    Lithos_MemTable* imm = NULL;
    Lithos_Version* current = NULL;
    TableCache* cache = NULL;
    Lithos_Options options = db->options;
    size_t total_files = 0;

    pthread_mutex_lock(&db->mu);
    if (db->mem) {
        mem = db->mem;
        MemTable_Ref(mem);
    }
    if (db->imm) {
        imm = db->imm;
        MemTable_Ref(imm);
    }
    if (db->versions) {
        current = db->versions->current;
        cache = db->versions->table_cache;
        if (current) {
            Version_Ref(current);
            for (int level = 0; level < kNumLevels; level++) {
                total_files += current->file_counts[level];
            }
        }
    }
    pthread_mutex_unlock(&db->mu);

    Status s = Status_OK();
    size_t total_iters = total_files;
    if (total_files == 0 && mem == NULL && imm == NULL) {
        goto cleanup;
    }

    Lithos_Iterator** iters = NULL;
    FileMetaData** file_refs = NULL;
    size_t idx = 0;
    size_t file_idx = 0;

    if (total_files > 0) {
        iters = calloc(total_iters, sizeof(Lithos_Iterator*));
        file_refs = calloc(total_files, sizeof(FileMetaData*));
        if (iters == NULL || file_refs == NULL) {
            s = Status_IOError("alloc scan iters", NULL);
            free(iters);
            free(file_refs);
            goto cleanup;
        }
    }

    if (current && cache && total_files > 0) {
        for (int level = 0; level < kNumLevels; level++) {
            for (size_t i = 0; i < current->file_counts[level]; i++) {
                FileMetaData* f = current->files[level][i];
                FileMetaData_Ref(f);
                file_refs[file_idx++] = f;
                Lithos_Iterator* titer = TableCache_NewIterator(cache, f, &options);
                if (titer == NULL) {
                    s = Status_IOError("table iter", NULL);
                    goto build_fail;
                }
                iters[idx++] = titer;
            }
        }
    }

    /* Emit MemTable keys first (newest data lives here). */
    if (mem) {
        Lithos_Iterator* mi = MemTable_NewIterator(mem);
        if (mi == NULL) {
            s = Status_IOError("mem iter", NULL);
            goto build_fail;
        }
        Iter_SeekToFirst(mi);
        while (Iter_Valid(mi)) {
            const Lithos_Slice* entry = (const Lithos_Slice*)Iter_Key(mi);
            Lithos_Slice ikey = EntryInternalKey(entry);
            Lithos_Slice value = EntryValue(entry);
            ParsedInternalKey parsed;
            if (!ParseInternalKey(ikey, &parsed)) {
                s = Status_Corruption("scan parse mem", NULL);
                break;
            }
            if (parsed.type != kTypeDeletion) {
                char* key_copy = malloc(parsed.user_key.size + 1);
                char* val_copy = malloc(value.size + 1);
                if (key_copy == NULL || val_copy == NULL) {
                    free(key_copy);
                    free(val_copy);
                    s = Status_IOError("scan alloc mem", NULL);
                    break;
                }
                memcpy(key_copy, parsed.user_key.data, parsed.user_key.size);
                key_copy[parsed.user_key.size] = '\0';
                memcpy(val_copy, value.data, value.size);
                val_copy[value.size] = '\0';
                cb(key_copy, val_copy, arg);
                free(key_copy);
                free(val_copy);
            }
            Iter_Next(mi);
        }
        Iter_Destroy(mi);
        if (!Status_IsOK(s)) goto build_fail;
    }

    if (imm) {
        Lithos_Iterator* mi = MemTable_NewIterator(imm);
        if (mi == NULL) {
            s = Status_IOError("imm iter", NULL);
            goto build_fail;
        }
        Iter_SeekToFirst(mi);
        while (Iter_Valid(mi)) {
            const Lithos_Slice* entry = (const Lithos_Slice*)Iter_Key(mi);
            Lithos_Slice ikey = EntryInternalKey(entry);
            Lithos_Slice value = EntryValue(entry);
            ParsedInternalKey parsed;
            if (!ParseInternalKey(ikey, &parsed)) {
                s = Status_Corruption("scan parse imm", NULL);
                break;
            }
            if (parsed.type != kTypeDeletion) {
                char* key_copy = malloc(parsed.user_key.size + 1);
                char* val_copy = malloc(value.size + 1);
                if (key_copy == NULL || val_copy == NULL) {
                    free(key_copy);
                    free(val_copy);
                    s = Status_IOError("scan alloc imm", NULL);
                    break;
                }
                memcpy(key_copy, parsed.user_key.data, parsed.user_key.size);
                key_copy[parsed.user_key.size] = '\0';
                memcpy(val_copy, value.data, value.size);
                val_copy[value.size] = '\0';
                cb(key_copy, val_copy, arg);
                free(key_copy);
                free(val_copy);
            }
            Iter_Next(mi);
        }
        Iter_Destroy(mi);
        if (!Status_IsOK(s)) goto build_fail;
    }

    if (total_files > 0) {
        Lithos_Iterator* merge = NewMergingIterator(iters, (int)total_iters, InternalKeyComparator);
        if (merge == NULL) {
            s = Status_IOError("merge iter", NULL);
            goto build_fail;
        }

        Lithos_Iter_SeekToFirst(merge);
        char* prev_key = NULL;
        size_t prev_len = 0;

        while (Lithos_Iter_Valid(merge)) {
            Lithos_Slice internal_key = Lithos_Iter_Key(merge);
            ParsedInternalKey parsed;
            if (!ParseInternalKey(internal_key, &parsed)) {
                s = Status_Corruption("scan parse table", NULL);
                break;
            }

            if (prev_key && prev_len == parsed.user_key.size &&
                memcmp(prev_key, parsed.user_key.data, prev_len) == 0) {
                Lithos_Iter_Next(merge);
                continue;
            }

            free(prev_key);
            prev_key = NULL;
            prev_len = 0;
            if (parsed.user_key.size > 0) {
                prev_key = malloc(parsed.user_key.size);
                if (prev_key) {
                    memcpy(prev_key, parsed.user_key.data, parsed.user_key.size);
                    prev_len = parsed.user_key.size;
                }
            }

            if (parsed.type == kTypeDeletion) {
                Lithos_Iter_Next(merge);
                continue;
            }

            Lithos_Slice value = Lithos_Iter_Value(merge);
            char* key_copy = malloc(parsed.user_key.size + 1);
            char* val_copy = malloc(value.size + 1);
            if (key_copy == NULL || val_copy == NULL) {
                free(key_copy);
                free(val_copy);
                s = Status_IOError("scan alloc table", NULL);
                free(prev_key);
                prev_key = NULL;
                prev_len = 0;
                break;
            }
            memcpy(key_copy, parsed.user_key.data, parsed.user_key.size);
            key_copy[parsed.user_key.size] = '\0';
            memcpy(val_copy, value.data, value.size);
            val_copy[value.size] = '\0';

            cb(key_copy, val_copy, arg);

            free(key_copy);
            free(val_copy);
            Lithos_Iter_Next(merge);
        }

        Status iter_status = Lithos_Iter_GetStatus(merge);
        if (Status_IsOK(s)) {
            s = iter_status;
        } else {
            Status_Free(iter_status);
        }

        free(prev_key);
        Lithos_Iter_Destroy(merge);
    }

    goto build_cleanup;

build_fail:
    for (size_t j = 0; j < idx; j++) {
        if (iters && iters[j]) Lithos_Iter_Destroy(iters[j]);
    }

build_cleanup:
    free(iters);
    UnrefFiles(file_refs, file_idx);
    free(file_refs);

cleanup:
    if (mem) MemTable_Unref(mem);
    if (imm) MemTable_Unref(imm);
    if (current) Version_Unref(current);
    return s;
}

void Lithos_Close(Lithos_DB* db) {
    Lithos_DB_Close(db);
}

void Lithos_Free(void* ptr) {
    free(ptr);
}
