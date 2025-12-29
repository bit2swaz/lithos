
#ifndef LITHOS_CORE_VERSION_SET_H
#define LITHOS_CORE_VERSION_SET_H

#include "core/log_writer.h"
#include "core/table_cache.h"
#include "core/version_edit.h"
#include "lithos/lookup_key.h"
#include "lithos/read_options.h"
#include "util/env.h"
#include "util/slice.h"
#include "util/status.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Lithos_Version Lithos_Version;
typedef struct Lithos_VersionSet Lithos_VersionSet;
typedef struct Lithos_Compaction Lithos_Compaction;

struct Lithos_Version {
  Lithos_VersionSet *vset;
  Lithos_Version *next;
  Lithos_Version *prev;
  int refs;

  FileMetaData **files[kNumLevels];

  size_t file_counts[kNumLevels];
  size_t file_caps[kNumLevels];
};

struct Lithos_VersionSet {
  char *dbname;
  Lithos_Version *current;
  Lithos_Version *dummy_versions;
  LogWriter *descriptor_log;
  Lithos_WritableFile *descriptor_file;
  uint64_t current_manifest_number;
  uint64_t next_file_number;
  TableCache *table_cache;
};

Lithos_VersionSet *VersionSet_Create(const char *dbname);
Lithos_VersionSet *VersionSet_Recover(const char *dbname);
void VersionSet_Destroy(Lithos_VersionSet *set);
Status VersionSet_LogAndApply(Lithos_VersionSet *set, VersionEdit *edit);
uint64_t VersionSet_NewFileNumber(Lithos_VersionSet *set);
Lithos_Compaction *VersionSet_PickCompaction(Lithos_VersionSet *set);
bool VersionSet_NeedsCompaction(Lithos_VersionSet *set);
void Compaction_Destroy(Lithos_Compaction *c);

struct Lithos_Compaction {
  Lithos_VersionSet *vset;
  int level;
  FileMetaData **inputs[2];
  size_t input_count[2];
  bool trivial_move;
};

void Version_Ref(Lithos_Version *v);
void Version_Unref(Lithos_Version *v);
Status Version_Get(Lithos_Version *v, const Lithos_ReadOptions *options,
                   LookupKey key, Lithos_Slice *value, bool *found,
                   bool *deleted);

#ifdef __cplusplus
}
#endif

#endif
