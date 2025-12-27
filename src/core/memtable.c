
#include "core/memtable.h"
#include "core/dbformat.h"
#include "core/skiplist.h"
#include "util/arena.h"
#include "util/coding.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct Lithos_MemTable {
  Lithos_SkipList *table;
  Lithos_Arena *arena;
  int refs;
};

static Lithos_Slice *EncodeMemTableEntry(Lithos_Arena *arena, Lithos_Slice key,
                                         SequenceNumber seq, ValueType type,
                                         Lithos_Slice value) {

  size_t internal_key_size = key.size + 8;

  size_t needed = VarintLength(internal_key_size) + key.size + 8 +
                  VarintLength(value.size) + value.size;

  char *buf = Arena_Allocate(arena, needed);
  char *p = buf;

  p = EncodeVarint32(p, (uint32_t)internal_key_size);

  memcpy(p, key.data, key.size);
  p += key.size;

  uint64_t packed = PackSequenceAndType(seq, type);
  EncodeFixed64(p, packed);
  p += 8;

  p = EncodeVarint32(p, (uint32_t)value.size);

  if (value.size > 0) {
    memcpy(p, value.data, value.size);
    p += value.size;
  }

  assert((size_t)(p - buf) == needed);

  Lithos_Slice *slice =
      (Lithos_Slice *)Arena_Allocate(arena, sizeof(Lithos_Slice));
  *slice = Slice_Create(buf, needed);
  return slice;
}

static Lithos_Slice DecodeInternalKey(const Lithos_Slice *encoded) {
  const char *p = encoded->data;
  const char *limit = p + encoded->size;

  uint32_t internal_key_size;
  const char *after = GetVarint32Ptr(p, limit, &internal_key_size);
  if (after == NULL) {
    return Slice_Create(NULL, 0);
  }

  return Slice_Create(after, internal_key_size);
}

static Lithos_Slice DecodeValue(const Lithos_Slice *encoded) {
  const char *p = encoded->data;
  const char *limit = p + encoded->size;

  uint32_t internal_key_size;
  p = GetVarint32Ptr(p, limit, &internal_key_size);
  if (p == NULL) {
    return Slice_Create(NULL, 0);
  }

  p += internal_key_size;
  if (p >= limit) {
    return Slice_Create(NULL, 0);
  }

  uint32_t value_size;
  p = GetVarint32Ptr(p, limit, &value_size);
  if (p == NULL || p + value_size > limit) {
    return Slice_Create(NULL, 0);
  }

  return Slice_Create(p, value_size);
}

static int MemTableKeyComparator(const void *a, const void *b) {
  const Lithos_Slice *aslice = (const Lithos_Slice *)a;
  const Lithos_Slice *bslice = (const Lithos_Slice *)b;

  Lithos_Slice akey = DecodeInternalKey(aslice);
  Lithos_Slice bkey = DecodeInternalKey(bslice);

  return InternalKeyComparator(&akey, &bkey);
}

Lithos_MemTable *MemTable_Create(void) {
  Lithos_MemTable *mem = (Lithos_MemTable *)malloc(sizeof(Lithos_MemTable));
  if (mem == NULL) {
    return NULL;
  }

  mem->arena = Arena_Create();
  if (mem->arena == NULL) {
    free(mem);
    return NULL;
  }

  mem->table = SkipList_Create(MemTableKeyComparator, mem->arena);
  if (mem->table == NULL) {
    Arena_Destroy(mem->arena);
    free(mem);
    return NULL;
  }

  mem->refs = 1;

  return mem;
}

void MemTable_Ref(Lithos_MemTable *mem) {
  assert(mem != NULL);
  assert(mem->refs > 0);
  mem->refs++;
}

void MemTable_Unref(Lithos_MemTable *mem) {
  if (mem == NULL) {
    return;
  }

  assert(mem->refs > 0);
  mem->refs--;

  if (mem->refs == 0) {

    SkipList_Destroy(mem->table);
    Arena_Destroy(mem->arena);
    free(mem);
  }
}

void MemTable_Add(Lithos_MemTable *mem, SequenceNumber seq, ValueType type,
                  Lithos_Slice key, Lithos_Slice value) {
  assert(mem != NULL);
  assert(seq <= kMaxSequenceNumber);

  if (type == kTypeDeletion) {
    value = Slice_Create("", 0);
  }

  Lithos_Slice *encoded =
      EncodeMemTableEntry(mem->arena, key, seq, type, value);

  SkipList_Insert(mem->table, encoded);
}

bool MemTable_Get(Lithos_MemTable *mem, Lithos_Slice key,
                  SequenceNumber snapshot_seq, char **value_out, Status *s) {
  assert(mem != NULL);
  assert(value_out != NULL);
  assert(s != NULL);

  size_t internal_key_size = key.size + 8;
  size_t needed = VarintLength(internal_key_size) + internal_key_size;

  char lookup_buf[256];
  char *buf;

  if (needed <= sizeof(lookup_buf)) {
    buf = lookup_buf;
  } else {

    buf = (char *)malloc(needed);
    if (buf == NULL) {
      *s = Status_IOError("Out of memory", "");
      return false;
    }
  }

  char *p = buf;
  p = EncodeVarint32(p, (uint32_t)internal_key_size);
  memcpy(p, key.data, key.size);
  p += key.size;

  SequenceNumber seq = snapshot_seq == 0 ? kMaxSequenceNumber : snapshot_seq;
  uint64_t packed = PackSequenceAndType(seq, kTypeValue);
  EncodeFixed64(p, packed);

  Lithos_Slice lookup_slice = Slice_Create(buf, needed);

  Lithos_Iterator *iter = SkipList_NewIterator(mem->table);
  if (iter == NULL) {
    if (buf != lookup_buf)
      free(buf);
    *s = Status_IOError("Out of memory", "");
    return false;
  }

  Iter_Seek(iter, &lookup_slice);

  bool found = false;

  if (Iter_Valid(iter)) {

    const Lithos_Slice *entry = (const Lithos_Slice *)Iter_Key(iter);

    Lithos_Slice ikey = DecodeInternalKey(entry);

    Lithos_Slice found_user_key = ExtractUserKey(ikey);

    if (Slice_Compare(found_user_key, key) == 0) {

      ParsedInternalKey parsed;
      if (ParseInternalKey(ikey, &parsed)) {
        if (parsed.type == kTypeValue) {

          Lithos_Slice val = DecodeValue(entry);

          *value_out = (char *)malloc(val.size + 1);
          if (*value_out != NULL) {
            memcpy(*value_out, val.data, val.size);
            (*value_out)[val.size] = '\0';
            *s = Status_OK();
            found = true;
          } else {
            *s = Status_IOError("Out of memory", "");
            found = false;
          }
        } else {

          *s = Status_NotFound(NULL);
          found = true;
        }
      } else {

        *s = Status_Corruption("Corrupted internal key", "");
        found = false;
      }
    }

  }

  Iter_Destroy(iter);
  if (buf != lookup_buf) {
    free(buf);
  }

  return found;
}

size_t MemTable_ApproximateMemoryUsage(const Lithos_MemTable *mem) {
  assert(mem != NULL);
  return Arena_MemoryUsage(mem->arena);
}

Lithos_Iterator *MemTable_NewIterator(Lithos_MemTable *mem) {
  assert(mem != NULL);
  return SkipList_NewIterator(mem->table);
}
