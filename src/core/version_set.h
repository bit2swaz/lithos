/**
 * VersionSet Interface - Catalog of SST Versions
 * ==============================================
 *
 * A Version is an immutable snapshot of all SST files across levels. The
 * VersionSet owns the MANIFEST (log of VersionEdit deltas) and the linked list
 * of live Versions. Reference counting keeps older Versions alive while
 * iterators read them, enabling snapshot isolation without blocking writes.
 *
 * Roles:
 * - Manifest persistence: append VersionEdit deltas via LogAndApply.
 * - Snapshot publishing: build a new Version from (current + delta) and swap.
 * - Lifetime management: Ref/Unref protects Files and Versions from premature
 *   deletion while readers hold references.
 */

#ifndef LITHOS_CORE_VERSION_SET_H
#define LITHOS_CORE_VERSION_SET_H

#include "core/version_edit.h"
#include "core/table_cache.h"
#include "util/status.h"
#include "util/env.h"
#include "util/slice.h"
#include "core/log_writer.h"
#include "lithos/read_options.h"
#include "lithos/lookup_key.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Lithos_Version Lithos_Version;
typedef struct Lithos_VersionSet Lithos_VersionSet;
typedef struct Lithos_Compaction Lithos_Compaction;

struct Lithos_Version {
    Lithos_VersionSet* vset;
    Lithos_Version* next;
    Lithos_Version* prev;
    int refs; /* External references (iterators, VersionSet current) keep this alive. */
    /* Snapshot semantics: immutable, consistent view of all SSTables. */
    FileMetaData** files[kNumLevels]; /* Per-level ordered file lists forming a snapshot view. */
    size_t file_counts[kNumLevels];
    size_t file_caps[kNumLevels];
};

struct Lithos_VersionSet {
    char* dbname;
    Lithos_Version* current;
    Lithos_Version* dummy_versions;
    LogWriter* descriptor_log;
    Lithos_WritableFile* descriptor_file;
    uint64_t current_manifest_number;
    uint64_t next_file_number;
    TableCache* table_cache;
};

Lithos_VersionSet* VersionSet_Create(const char* dbname);
void VersionSet_Destroy(Lithos_VersionSet* set);
Status VersionSet_LogAndApply(Lithos_VersionSet* set, VersionEdit* edit);
uint64_t VersionSet_NewFileNumber(Lithos_VersionSet* set);
Lithos_Compaction* VersionSet_PickCompaction(Lithos_VersionSet* set);
bool VersionSet_NeedsCompaction(Lithos_VersionSet* set);
void Compaction_Destroy(Lithos_Compaction* c);

struct Lithos_Compaction {
    Lithos_VersionSet* vset;
    int level; /* Inputs are level and level+1 */
    FileMetaData** inputs[2];
    size_t input_count[2];
    bool trivial_move;
};

void Version_Ref(Lithos_Version* v);
void Version_Unref(Lithos_Version* v);
Status Version_Get(Lithos_Version* v,
                   const Lithos_ReadOptions* options,
                   LookupKey key,
                   Lithos_Slice* value,
                   bool* found,
                   bool* deleted);

#ifdef __cplusplus
}
#endif

#endif /* LITHOS_CORE_VERSION_SET_H */
