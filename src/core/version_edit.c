
#include "core/version_edit.h"
#include "util/coding.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
  kTagLogNumber = 1,
  kTagPrevLogNumber = 2,
  kTagNextFileNumber = 3,
  kTagNewFile = 4,
  kTagDeletedFile = 5
} VersionEditTag;

static char *CopySlice(const Lithos_Slice src) {
  if (src.size == 0) {
    return NULL;
  }

  char *buf = malloc(src.size);
  if (buf == NULL) {
    return NULL;
  }
  memcpy(buf, src.data, src.size);
  return buf;
}

FileMetaData *FileMetaData_Create(uint64_t number, uint64_t file_size,
                                  Lithos_Slice smallest, Lithos_Slice largest,
                                  SequenceNumber max_sequence) {
  FileMetaData *m = malloc(sizeof(FileMetaData));
  if (m == NULL) {
    return NULL;
  }
  m->number = number;
  m->file_size = file_size;
  m->max_sequence = max_sequence;
  m->refs = 0;
  m->smallest_buf = CopySlice(smallest);
  m->largest_buf = CopySlice(largest);
  m->smallest = Slice_Create(m->smallest_buf, smallest.size);
  m->largest = Slice_Create(m->largest_buf, largest.size);
  return m;
}

void FileMetaData_Ref(FileMetaData *f) {
  if (f != NULL) {
    f->refs++;
  }
}

void FileMetaData_Unref(FileMetaData *f) {
  if (f == NULL) {
    return;
  }
  assert(f->refs > 0);
  f->refs--;
  if (f->refs == 0) {
    free(f->smallest_buf);
    free(f->largest_buf);
    free(f);
  }
}

static bool EnsureNewCapacity(NewFile **arr, size_t *cap, size_t needed) {
  if (*cap >= needed)
    return true;
  size_t new_cap = (*cap == 0) ? 4 : (*cap * 2);
  while (new_cap < needed)
    new_cap *= 2;
  NewFile *new_arr = realloc(*arr, new_cap * sizeof(NewFile));
  if (new_arr == NULL) {
    return false;
  }
  *arr = new_arr;
  *cap = new_cap;
  return true;
}

static bool EnsureDelCapacity(DeletedFile **arr, size_t *cap, size_t needed) {
  if (*cap >= needed)
    return true;
  size_t new_cap = (*cap == 0) ? 4 : (*cap * 2);
  while (new_cap < needed)
    new_cap *= 2;
  DeletedFile *new_arr = realloc(*arr, new_cap * sizeof(DeletedFile));
  if (new_arr == NULL) {
    return false;
  }
  *arr = new_arr;
  *cap = new_cap;
  return true;
}

void VersionEdit_Init(VersionEdit *edit) { memset(edit, 0, sizeof(*edit)); }

void VersionEdit_Clear(VersionEdit *edit) {
  for (size_t i = 0; i < edit->new_files_count; i++) {

    if (edit->new_files[i].file && edit->new_files[i].file->refs == 0) {
      free(edit->new_files[i].file->smallest_buf);
      free(edit->new_files[i].file->largest_buf);
      free(edit->new_files[i].file);
    }
  }
  free(edit->new_files);
  free(edit->deleted_files);
  memset(edit, 0, sizeof(*edit));
}

void VersionEdit_SetLogNumber(VersionEdit *edit, uint64_t num) {
  edit->has_log_number = true;
  edit->log_number = num;
}

void VersionEdit_SetPrevLogNumber(VersionEdit *edit, uint64_t num) {
  edit->has_prev_log_number = true;
  edit->prev_log_number = num;
}

void VersionEdit_SetNextFileNumber(VersionEdit *edit, uint64_t num) {
  edit->has_next_file_number = true;
  edit->next_file_number = num;
}

void VersionEdit_AddFile(VersionEdit *edit, int level, uint64_t number,
                         uint64_t file_size, Lithos_Slice smallest,
                         Lithos_Slice largest, SequenceNumber max_sequence) {
  if (!EnsureNewCapacity(&edit->new_files, &edit->new_files_cap,
                         edit->new_files_count + 1)) {
    return;
  }
  FileMetaData *meta =
      FileMetaData_Create(number, file_size, smallest, largest, max_sequence);
  if (meta == NULL) {
    return;
  }
  edit->new_files[edit->new_files_count].level = level;
  edit->new_files[edit->new_files_count].file = meta;
  edit->new_files_count++;
}

void VersionEdit_DeleteFile(VersionEdit *edit, int level, uint64_t number) {
  if (!EnsureDelCapacity(&edit->deleted_files, &edit->deleted_files_cap,
                         edit->deleted_files_count + 1)) {
    return;
  }
  edit->deleted_files[edit->deleted_files_count].level = level;
  edit->deleted_files[edit->deleted_files_count].number = number;
  edit->deleted_files_count++;
}

static void AppendVarint32(char **dst, size_t *cap, size_t *len, uint32_t v) {
  char buf[5];
  char *end = EncodeVarint32(buf, v);
  size_t n = (size_t)(end - buf);
  if (*len + n > *cap) {
    size_t new_cap = (*cap == 0) ? 64 : (*cap * 2);
    while (*len + n > new_cap)
      new_cap *= 2;
    *dst = realloc(*dst, new_cap);
    *cap = new_cap;
  }
  memcpy(*dst + *len, buf, n);
  *len += n;
}

static void AppendVarint64(char **dst, size_t *cap, size_t *len, uint64_t v) {
  char buf[10];
  char *end = EncodeVarint64(buf, v);
  size_t n = (size_t)(end - buf);
  if (*len + n > *cap) {
    size_t new_cap = (*cap == 0) ? 64 : (*cap * 2);
    while (*len + n > new_cap)
      new_cap *= 2;
    *dst = realloc(*dst, new_cap);
    *cap = new_cap;
  }
  memcpy(*dst + *len, buf, n);
  *len += n;
}

static void AppendSlice(char **dst, size_t *cap, size_t *len, Lithos_Slice s) {
  AppendVarint32(dst, cap, len, (uint32_t)s.size);
  if (*len + s.size > *cap) {
    size_t new_cap = (*cap == 0) ? 64 : (*cap * 2);
    while (*len + s.size > new_cap)
      new_cap *= 2;
    *dst = realloc(*dst, new_cap);
    *cap = new_cap;
  }
  memcpy(*dst + *len, s.data, s.size);
  *len += s.size;
}

Status VersionEdit_EncodeTo(VersionEdit *edit, Lithos_Slice *dst) {
  char *buf = NULL;
  size_t cap = 0;
  size_t len = 0;

  if (edit->has_log_number) {

    AppendVarint32(&buf, &cap, &len, kTagLogNumber);
    AppendVarint64(&buf, &cap, &len, edit->log_number);
  }
  if (edit->has_prev_log_number) {

    AppendVarint32(&buf, &cap, &len, kTagPrevLogNumber);
    AppendVarint64(&buf, &cap, &len, edit->prev_log_number);
  }
  if (edit->has_next_file_number) {

    AppendVarint32(&buf, &cap, &len, kTagNextFileNumber);
    AppendVarint64(&buf, &cap, &len, edit->next_file_number);
  }

  for (size_t i = 0; i < edit->deleted_files_count; i++) {

    AppendVarint32(&buf, &cap, &len, kTagDeletedFile);
    AppendVarint32(&buf, &cap, &len, (uint32_t)edit->deleted_files[i].level);
    AppendVarint64(&buf, &cap, &len, edit->deleted_files[i].number);
  }

  for (size_t i = 0; i < edit->new_files_count; i++) {
    NewFile *nf = &edit->new_files[i];

    AppendVarint32(&buf, &cap, &len, kTagNewFile);
    AppendVarint32(&buf, &cap, &len, (uint32_t)nf->level);
    AppendVarint64(&buf, &cap, &len, nf->file->number);
    AppendVarint64(&buf, &cap, &len, nf->file->file_size);
    AppendSlice(&buf, &cap, &len, nf->file->smallest);
    AppendSlice(&buf, &cap, &len, nf->file->largest);
    AppendVarint64(&buf, &cap, &len, nf->file->max_sequence);
  }

  *dst = Slice_Create(buf, len);
  return Status_OK();
}

static bool GetVarint32(const char **p, const char *limit, uint32_t *v) {
  const char *r = GetVarint32Ptr(*p, limit, v);
  if (r == NULL)
    return false;
  *p = r;
  return true;
}

static bool GetVarint64(const char **p, const char *limit, uint64_t *v) {
  const char *r = GetVarint64Ptr(*p, limit, v);
  if (r == NULL)
    return false;
  *p = r;
  return true;
}

static bool GetLengthPrefixedSlice(const char **p, const char *limit,
                                   Lithos_Slice *out) {
  uint32_t len = 0;
  if (!GetVarint32(p, limit, &len))
    return false;
  if ((size_t)(limit - *p) < len)
    return false;
  *out = Slice_Create(*p, len);
  *p += len;
  return true;
}

Status VersionEdit_DecodeFrom(VersionEdit *edit, Lithos_Slice src) {
  VersionEdit_Clear(edit);
  VersionEdit_Init(edit);

  const char *p = src.data;
  const char *limit = src.data + src.size;

  while (p < limit) {
    uint32_t tag = 0;
    if (!GetVarint32(&p, limit, &tag)) {
      return Status_Corruption("bad tag", NULL);
    }
    switch (tag) {
    case kTagLogNumber: {
      uint64_t v;
      if (!GetVarint64(&p, limit, &v))
        return Status_Corruption("bad log number", NULL);
      VersionEdit_SetLogNumber(edit, v);
      break;
    }
    case kTagPrevLogNumber: {
      uint64_t v;
      if (!GetVarint64(&p, limit, &v))
        return Status_Corruption("bad prev log", NULL);
      VersionEdit_SetPrevLogNumber(edit, v);
      break;
    }
    case kTagNextFileNumber: {
      uint64_t v;
      if (!GetVarint64(&p, limit, &v))
        return Status_Corruption("bad next file", NULL);
      VersionEdit_SetNextFileNumber(edit, v);
      break;
    }
    case kTagDeletedFile: {
      uint32_t level;
      uint64_t num;
      if (!GetVarint32(&p, limit, &level))
        return Status_Corruption("bad del level", NULL);
      if (!GetVarint64(&p, limit, &num))
        return Status_Corruption("bad del file", NULL);
      VersionEdit_DeleteFile(edit, (int)level, num);
      break;
    }
    case kTagNewFile: {
      uint32_t level;
      uint64_t num;
      uint64_t fsize;
      Lithos_Slice small;
      Lithos_Slice large;
      uint64_t max_seq = 0;
      if (!GetVarint32(&p, limit, &level))
        return Status_Corruption("bad new level", NULL);
      if (!GetVarint64(&p, limit, &num))
        return Status_Corruption("bad new file num", NULL);
      if (!GetVarint64(&p, limit, &fsize))
        return Status_Corruption("bad new file size", NULL);
      if (!GetLengthPrefixedSlice(&p, limit, &small))
        return Status_Corruption("bad smallest", NULL);
      if (!GetLengthPrefixedSlice(&p, limit, &large))
        return Status_Corruption("bad largest", NULL);
      if (!GetVarint64(&p, limit, &max_seq))
        return Status_Corruption("bad max sequence", NULL);
      VersionEdit_AddFile(edit, (int)level, num, fsize, small, large, max_seq);
      break;
    }
    default:
      return Status_Corruption("unknown tag", NULL);
    }
  }

  return Status_OK();
}
