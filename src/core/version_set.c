
#include "core/version_set.h"
#include "core/version_edit.h"
#include "core/log_reader.h"
#include "util/coding.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static double MaxBytesForLevel(int level) {
  if (level <= 0)
    return 10 * 1024 * 1024.0;
  double base = 10 * 1024 * 1024.0;
  double bytes = base;
  for (int i = 1; i < level; i++) {
    bytes *= 10.0;
  }
  return bytes;
}

static bool Overlaps(FileMetaData *f, Lithos_Slice user_key) {
  Lithos_Slice s = ExtractUserKey(f->smallest);
  Lithos_Slice l = ExtractUserKey(f->largest);
  return Slice_Compare(user_key, s) >= 0 && Slice_Compare(user_key, l) <= 0;
}

static bool FilesOverlap(FileMetaData *a, FileMetaData *b) {
  Lithos_Slice a_s = ExtractUserKey(a->smallest);
  Lithos_Slice a_l = ExtractUserKey(a->largest);
  Lithos_Slice b_s = ExtractUserKey(b->smallest);
  Lithos_Slice b_l = ExtractUserKey(b->largest);
  return !(Slice_Compare(a_l, b_s) < 0 || Slice_Compare(b_l, a_s) < 0);
}

static Lithos_Version *Version_New(Lithos_VersionSet *set) {
  Lithos_Version *v = calloc(1, sizeof(Lithos_Version));
  if (v == NULL)
    return NULL;
  v->vset = set;
  v->next = v->prev = NULL;
  v->refs = 0;
  return v;
}

static void VersionLink(Lithos_VersionSet *set, Lithos_Version *v) {

  v->next = set->dummy_versions->next;
  v->prev = set->dummy_versions;
  set->dummy_versions->next->prev = v;
  set->dummy_versions->next = v;
}

static void VersionUnlink(Lithos_Version *v) {

  v->prev->next = v->next;
  v->next->prev = v->prev;
  v->next = v->prev = NULL;
}

static char *DupString(const char *s) {
  size_t n = strlen(s);
  char *r = malloc(n + 1);
  if (r == NULL)
    return NULL;
  memcpy(r, s, n + 1);
  return r;
}

static void Version_AddFile(Lithos_Version *v, int level, FileMetaData *f) {
  if (level < 0 || level >= kNumLevels)
    return;
  if (v->file_caps[level] == v->file_counts[level]) {
    size_t new_cap = (v->file_caps[level] == 0) ? 4 : v->file_caps[level] * 2;
    FileMetaData **nf =
        realloc(v->files[level], new_cap * sizeof(FileMetaData *));
    if (nf == NULL)
      return;
    v->files[level] = nf;
    v->file_caps[level] = new_cap;
  }
  v->files[level][v->file_counts[level]++] = f;
  FileMetaData_Ref(f);
}

static void Version_RemoveFile(Lithos_Version *v, int level, uint64_t number) {
  if (level < 0 || level >= kNumLevels)
    return;
  size_t idx = 0;
  while (idx < v->file_counts[level]) {
    if (v->files[level][idx]->number == number) {
      FileMetaData_Unref(v->files[level][idx]);
      for (size_t j = idx + 1; j < v->file_counts[level]; j++) {
        v->files[level][j - 1] = v->files[level][j];
      }
      v->file_counts[level]--;
      return;
    }
    idx++;
  }
}

static Status Version_BuildFromEdit(Lithos_VersionSet *set,
                                    Lithos_Version *base, VersionEdit *edit,
                                    Lithos_Version **out) {

  Lithos_Version *v = Version_New(set);
  if (v == NULL) {
    return Status_IOError("alloc version", NULL);
  }

  for (int level = 0; level < kNumLevels; level++) {
    for (size_t i = 0; i < base->file_counts[level]; i++) {
      Version_AddFile(v, level, base->files[level][i]);
    }
  }

  for (size_t i = 0; i < edit->deleted_files_count; i++) {
    Version_RemoveFile(v, edit->deleted_files[i].level,
                       edit->deleted_files[i].number);
  }

  for (size_t i = 0; i < edit->new_files_count; i++) {
    Version_AddFile(v, edit->new_files[i].level, edit->new_files[i].file);
  }

  *out = v;
  return Status_OK();
}

Lithos_VersionSet *VersionSet_Create(const char *dbname) {

  Lithos_VersionSet *set = calloc(1, sizeof(Lithos_VersionSet));
  if (set == NULL)
    return NULL;
  set->dbname = DupString(dbname);
  set->current_manifest_number = 1;
  set->next_file_number = 2;
  set->table_cache = TableCache_Create(dbname, 1024);

  set->dummy_versions = Version_New(set);
  set->dummy_versions->next = set->dummy_versions->prev = set->dummy_versions;

  set->current = Version_New(set);
  set->current->next = set->current->prev = NULL;
  Version_Ref(
      set->current);
  VersionLink(set, set->current);

  if (set->dbname != NULL && set->dbname[0] != '\0') {
    mkdir(set->dbname, 0755);
  }

  char manifest_name[512];
  if (set->dbname != NULL && set->dbname[0] != '\0') {
    snprintf(manifest_name, sizeof(manifest_name), "%s/MANIFEST-%06llu",
             set->dbname, (unsigned long long)set->current_manifest_number);
  } else {
    snprintf(manifest_name, sizeof(manifest_name), "MANIFEST-%06llu",
             (unsigned long long)set->current_manifest_number);
  }
  Status s = Env_NewAppendableFile(manifest_name, &set->descriptor_file);
  if (!Status_IsOK(s)) {
    VersionSet_Destroy(set);
    return NULL;
  }
  set->descriptor_log = LogWriter_Create(set->descriptor_file);
  return set;
}

Lithos_VersionSet *VersionSet_Recover(const char *dbname) {
  char manifest_name[512];
  snprintf(manifest_name, sizeof(manifest_name), "%s/MANIFEST-000001", dbname);
  
  Lithos_SequentialFile *file = NULL;
  Status s = Env_NewSequentialFile(manifest_name, &file);
  if (!Status_IsOK(s)) {
    Status_Free(s);
    return VersionSet_Create(dbname);
  }
  
  Lithos_VersionSet *set = calloc(1, sizeof(Lithos_VersionSet));
  if (set == NULL) {
    SequentialFile_Close(file);
    return NULL;
  }
  
  set->dbname = DupString(dbname);
  set->current_manifest_number = 1;
  set->next_file_number = 2;
  set->table_cache = TableCache_Create(dbname, 1024);
  
  set->dummy_versions = Version_New(set);
  set->dummy_versions->next = set->dummy_versions->prev = set->dummy_versions;
  
  set->current = Version_New(set);
  set->current->next = set->current->prev = NULL;
  Version_Ref(set->current);
  VersionLink(set, set->current);
  
  LogReader *reader = LogReader_Create(file, true);
  if (reader == NULL) {
    SequentialFile_Close(file);
    VersionSet_Destroy(set);
    return NULL;
  }
  
  Lithos_Slice record;
  char *scratch = NULL;
  int edit_count = 0;
  
  while (LogReader_ReadRecord(reader, &record, &scratch)) {
    VersionEdit edit;
    VersionEdit_Init(&edit);
    
    Status decode_s = VersionEdit_DecodeFrom(&edit, record);
    if (!Status_IsOK(decode_s)) {
      Status_Free(decode_s);
      VersionEdit_Clear(&edit);
      break;
    }
    
    if (edit.has_next_file_number && edit.next_file_number > set->next_file_number) {
      set->next_file_number = edit.next_file_number;
    }
    
    Lithos_Version *new_version = NULL;
    Status build_s = Version_BuildFromEdit(set, set->current, &edit, &new_version);
    if (!Status_IsOK(build_s)) {
      Status_Free(build_s);
      VersionEdit_Clear(&edit);
      break;
    }
    
    Version_Ref(new_version);
    VersionLink(set, new_version);
    
    Lithos_Version *old = set->current;
    set->current = new_version;
    Version_Unref(old);
    
    edit_count++;
    
    for (size_t i = 0; i < edit.new_files_count; i++) {
      edit.new_files[i].file = NULL;
    }
    edit.new_files_count = 0;
    edit.deleted_files_count = 0;
    
    VersionEdit_Clear(&edit);
  }
  
  if (scratch) {
    free(scratch);
  }
  LogReader_Destroy(reader);
  SequentialFile_Close(file);
  
  Status open_s = Env_NewAppendableFile(manifest_name, &set->descriptor_file);
  if (!Status_IsOK(open_s)) {
    Status_Free(open_s);
    VersionSet_Destroy(set);
    return NULL;
  }
  
  set->descriptor_log = LogWriter_Create(set->descriptor_file);
  return set;
}

static void Version_Destroy(Lithos_Version *v) {

  for (int level = 0; level < kNumLevels; level++) {
    for (size_t i = 0; i < v->file_counts[level]; i++) {
      FileMetaData_Unref(v->files[level][i]);
    }
    free(v->files[level]);
  }
  free(v);
}

void VersionSet_Destroy(Lithos_VersionSet *set) {
  if (set == NULL)
    return;
  if (set->table_cache) {
    TableCache_Destroy(set->table_cache);
  }
  Lithos_Version *v = set->dummy_versions->next;
  while (v != set->dummy_versions) {
    Lithos_Version *next = v->next;
    Version_Destroy(v);
    v = next;
  }
  Version_Destroy(set->dummy_versions);
  if (set->descriptor_log) {
    LogWriter_Destroy(set->descriptor_log);
  }
  if (set->descriptor_file) {
    WritableFile_Close(set->descriptor_file);
  }
  free(set->dbname);
  free(set);
}

uint64_t VersionSet_NewFileNumber(Lithos_VersionSet *set) {
  return set->next_file_number++;
}

Status VersionSet_LogAndApply(Lithos_VersionSet *set, VersionEdit *edit) {
  if (edit->has_next_file_number) {
    set->next_file_number = edit->next_file_number;
  }

  Lithos_Version *new_version = NULL;

  Status s = Version_BuildFromEdit(set, set->current, edit, &new_version);
  if (!Status_IsOK(s)) {
    return s;
  }

  Lithos_Slice record;

  s = VersionEdit_EncodeTo(edit, &record);
  if (!Status_IsOK(s)) {
    Version_Destroy(new_version);
    return s;
  }

  s = LogWriter_AddRecord(set->descriptor_log, record);
  if (!Status_IsOK(s)) {
    free((void *)record.data);
    Version_Destroy(new_version);
    return s;
  }
  
  WritableFile_Flush(set->descriptor_file);
  WritableFile_Sync(set->descriptor_file);

  free((void *)record.data);

  Version_Ref(new_version);
  VersionLink(set, new_version);

  Lithos_Version *old = set->current;
  set->current = new_version;
  Version_Unref(
      old);

  for (size_t i = 0; i < edit->new_files_count; i++) {
    edit->new_files[i].file = NULL;
  }
  edit->new_files_count = 0;
  edit->deleted_files_count = 0;

  return Status_OK();
}

void Version_Ref(Lithos_Version *v) {
  if (v != NULL) {
    v->refs++;
  }
}

void Version_Unref(Lithos_Version *v) {
  if (v == NULL)
    return;
  assert(v->refs > 0);
  v->refs--;
  if (v->refs == 0) {
    VersionUnlink(v);
    Version_Destroy(v);
  }
}

static void Compaction_AddInput(Lithos_Compaction *c, int which,
                                FileMetaData *f) {
  size_t cap = c->input_count[which] == 0 ? 4 : c->input_count[which] * 2;
  if (c->input_count[which] >= cap)
    cap = c->input_count[which] + 1;
  FileMetaData **nf = realloc(c->inputs[which], cap * sizeof(FileMetaData *));
  if (nf == NULL)
    return;
  c->inputs[which] = nf;
  c->inputs[which][c->input_count[which]++] = f;
  FileMetaData_Ref(f);
}

bool VersionSet_NeedsCompaction(Lithos_VersionSet *set) {
  if (set == NULL || set->current == NULL)
    return false;
  Lithos_Version *v = set->current;
  double best = 0.0;
  for (int level = 0; level < kNumLevels; level++) {
    double score = 0.0;
    if (level == 0) {
      score = (double)v->file_counts[level] / 4.0;
    } else {
      uint64_t total = 0;
      for (size_t i = 0; i < v->file_counts[level]; i++)
        total += v->files[level][i]->file_size;
      double max_bytes = MaxBytesForLevel(level);
      score = max_bytes > 0 ? (double)total / max_bytes : 0.0;
    }
    if (score > best)
      best = score;
  }
  return best >= 1.0;
}

Lithos_Compaction *VersionSet_PickCompaction(Lithos_VersionSet *set) {
  if (set == NULL || set->current == NULL)
    return NULL;
  Lithos_Version *v = set->current;

  int best_level = -1;
  double best_score = 0.0;
  for (int level = 0; level < kNumLevels; level++) {
    double score = 0.0;
    if (level == 0) {
      score = (double)v->file_counts[level] / 4.0;
    } else {
      uint64_t total = 0;
      for (size_t i = 0; i < v->file_counts[level]; i++)
        total += v->files[level][i]->file_size;
      double max_bytes = MaxBytesForLevel(level);
      score = max_bytes > 0 ? (double)total / max_bytes : 0.0;
    }
    if (score > best_score) {
      best_score = score;
      best_level = level;
    }
  }

  if (best_level < 0 || best_score < 1.0 || best_level >= kNumLevels - 1) {
    return NULL;
  }

  Lithos_Compaction *c = calloc(1, sizeof(Lithos_Compaction));
  if (c == NULL)
    return NULL;
  c->vset = set;
  c->level = best_level;

  if (v->file_counts[best_level] > 0) {

    Compaction_AddInput(c, 0, v->files[best_level][0]);
  }

  if (c->input_count[0] > 0) {
    FileMetaData *f = c->inputs[0][0];
    Lithos_Slice small = ExtractUserKey(f->smallest);
    Lithos_Slice large = ExtractUserKey(f->largest);
    int next_level = best_level + 1;
    size_t overlaps = 0;
    for (size_t i = 0; i < v->file_counts[next_level]; i++) {
      FileMetaData *cand = v->files[next_level][i];
      if (FilesOverlap(f, cand) || Overlaps(cand, small) ||
          Overlaps(cand, large)) {
        overlaps++;
        Compaction_AddInput(c, 1, cand);
      }
    }
    if (overlaps == 0) {
      c->trivial_move = true;
    }
  }

  return c;
}

void Compaction_Destroy(Lithos_Compaction *c) {
  if (c == NULL)
    return;
  for (int i = 0; i < 2; i++) {
    for (size_t j = 0; j < c->input_count[i]; j++) {
      FileMetaData_Unref(c->inputs[i][j]);
    }
    free(c->inputs[i]);
  }
  free(c);
}

Status Version_Get(Lithos_Version *v, const Lithos_ReadOptions *options,
                   LookupKey key, Lithos_Slice *value, bool *found,
                   bool *deleted) {
  (void)options;
  if (found)
    *found = false;
  if (deleted)
    *deleted = false;
  if (v == NULL || v->vset == NULL) {
    return Status_NotFound(NULL);
  }

  TableCache *cache = v->vset->table_cache;

  for (int level = 0; level < kNumLevels; level++) {
    size_t count = v->file_counts[level];
    if (count == 0)
      continue;

    if (level == 0) {
      for (int i = (int)count - 1; i >= 0; i--) {
        FileMetaData *f = v->files[level][i];
        Lithos_Slice smallest_user = ExtractUserKey(f->smallest);
        Lithos_Slice largest_user = ExtractUserKey(f->largest);
        
        if (Slice_Compare(key.user_key, smallest_user) < 0) {
          continue;
        }
        if (Slice_Compare(key.user_key, largest_user) > 0) {
          continue;
        }
        
        bool file_found = false;
        bool file_deleted = false;
        Status s = TableCache_Get(cache, f, key.internal_key, value,
                                  &file_found, &file_deleted);
        if (!Status_IsOK(s))
          return s;
        if (file_found || file_deleted) {
          if (found)
            *found = file_found;
          if (deleted)
            *deleted = file_deleted;
          return Status_OK();
        }
      }
    } else {
      size_t left = 0, right = count;
      while (left < right) {
        size_t mid = (left + right) / 2;
        Lithos_Slice mid_smallest =
            ExtractUserKey(v->files[level][mid]->smallest);
        if (Slice_Compare(mid_smallest, key.user_key) <= 0) {
          left = mid + 1;
        } else {
          right = mid;
        }
      }
      if (left == 0)
        continue;
      FileMetaData *f = v->files[level][left - 1];
      Lithos_Slice smallest_user = ExtractUserKey(f->smallest);
      Lithos_Slice largest_user = ExtractUserKey(f->largest);
      if (Slice_Compare(key.user_key, smallest_user) < 0)
        continue;
      if (Slice_Compare(key.user_key, largest_user) > 0)
        continue;

      bool file_found = false;
      bool file_deleted = false;
      Status s = TableCache_Get(cache, f, key.internal_key, value, &file_found,
                                &file_deleted);
      if (!Status_IsOK(s))
        return s;
      if (file_found || file_deleted) {
        if (found)
          *found = file_found;
        if (deleted)
          *deleted = file_deleted;
        return Status_OK();
      }
    }
  }

  return Status_NotFound(NULL);
}
