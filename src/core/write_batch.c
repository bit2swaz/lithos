/* WriteBatch implementation: encode/decode batched writes for WAL + memtable. */

#include "lithos/write_batch.h"
#include "util/coding.h"
#include <stdlib.h>
#include <string.h>

static const size_t kWriteBatchHeader = 12; /* 8 bytes sequence + 4 bytes count */

static void EnsureCapacity(Lithos_WriteBatch* batch, size_t needed) {
    if (batch->capacity >= needed) return;
    size_t new_cap = batch->capacity == 0 ? 32 : batch->capacity * 2;
    while (new_cap < needed) new_cap *= 2;
    batch->rep = realloc(batch->rep, new_cap);
    batch->capacity = new_cap;
}

Lithos_WriteBatch* WriteBatch_Create(void) {
    Lithos_WriteBatch* b = calloc(1, sizeof(Lithos_WriteBatch));
    if (b == NULL) return NULL;
    b->capacity = 32;
    b->rep = calloc(1, b->capacity);
    if (b->rep == NULL) {
        free(b);
        return NULL;
    }
    b->size = kWriteBatchHeader;
    return b;
}

void WriteBatch_Destroy(Lithos_WriteBatch* batch) {
    if (batch == NULL) return;
    free(batch->rep);
    free(batch);
}

void WriteBatch_Clear(Lithos_WriteBatch* batch) {
    if (batch == NULL) return;
    if (batch->rep != NULL && batch->capacity >= kWriteBatchHeader) {
        memset(batch->rep, 0, kWriteBatchHeader);
    }
    batch->size = kWriteBatchHeader;
}

void WriteBatch_SetSequence(Lithos_WriteBatch* batch, uint64_t seq) {
    if (batch == NULL || batch->rep == NULL) return;
    EncodeFixed64(batch->rep, seq);
}

uint64_t WriteBatch_Sequence(const Lithos_WriteBatch* batch) {
    if (batch == NULL || batch->rep == NULL || batch->size < kWriteBatchHeader) return 0;
    return DecodeFixed64(batch->rep);
}

static void SetCount(Lithos_WriteBatch* batch, uint32_t count) {
    EncodeFixed32(batch->rep + 8, count);
}

int WriteBatch_Count(const Lithos_WriteBatch* batch) {
    if (batch == NULL || batch->rep == NULL || batch->size < kWriteBatchHeader) return 0;
    return (int)DecodeFixed32(batch->rep + 8);
}

static Status AppendRecord(Lithos_WriteBatch* batch, ValueType type, Lithos_Slice key, Lithos_Slice value) {
    if (batch == NULL || batch->rep == NULL) {
        return Status_InvalidArgument("batch");
    }

    size_t needed = 1 + VarintLength(key.size) + key.size;
    if (type == kTypeValue) {
        needed += VarintLength(value.size) + value.size;
    }
    EnsureCapacity(batch, batch->size + needed);

    char* p = batch->rep + batch->size;
    *p++ = (char)type;

    p = EncodeVarint32(p, (uint32_t)key.size);
    memcpy(p, key.data, key.size);
    p += key.size;

    if (type == kTypeValue) {
        p = EncodeVarint32(p, (uint32_t)value.size);
        if (value.size > 0) {
            memcpy(p, value.data, value.size);
            p += value.size;
        }
    }

    batch->size = (size_t)(p - batch->rep);
    uint32_t count = (uint32_t)WriteBatch_Count(batch) + 1;
    SetCount(batch, count);
    return Status_OK();
}

Status WriteBatch_Put(Lithos_WriteBatch* batch, Lithos_Slice key, Lithos_Slice value) {
    return AppendRecord(batch, kTypeValue, key, value);
}

Status WriteBatch_Delete(Lithos_WriteBatch* batch, Lithos_Slice key) {
    return AppendRecord(batch, kTypeDeletion, key, Slice_Empty());
}

Status WriteBatch_Append(Lithos_WriteBatch* dst, const Lithos_WriteBatch* src) {
    if (dst == NULL || src == NULL || dst->rep == NULL || src->rep == NULL) {
        return Status_InvalidArgument("batch");
    }
    if (src->size < kWriteBatchHeader) {
        return Status_InvalidArgument("src batch too small");
    }
    size_t payload = src->size - kWriteBatchHeader;
    EnsureCapacity(dst, dst->size + payload);
    memcpy(dst->rep + dst->size, src->rep + kWriteBatchHeader, payload);
    dst->size += payload;
    uint32_t total = (uint32_t)WriteBatch_Count(dst) + (uint32_t)WriteBatch_Count(src);
    SetCount(dst, total);
    return Status_OK();
}

static Status IterateBytes(const char* data, size_t size, WriteBatchHandler* handler) {
    if (data == NULL || size < kWriteBatchHeader) {
        return Status_Corruption("batch too small", NULL);
    }
    const char* p = data + kWriteBatchHeader;
    const char* limit = data + size;
    uint32_t seen = 0;

    while (p < limit) {
        ValueType tag = (ValueType)(uint8_t)(*p++);
        if (tag != kTypeValue && tag != kTypeDeletion) {
            return Status_Corruption("unknown tag", NULL);
        }

        uint32_t key_len;
        const char* key_ptr = GetVarint32Ptr(p, limit, &key_len);
        if (key_ptr == NULL || key_ptr + key_len > limit) {
            return Status_Corruption("bad key length", NULL);
        }
        Lithos_Slice key = Slice_Create(key_ptr, key_len);
        p = key_ptr + key_len;

        Lithos_Slice value = Slice_Empty();
        if (tag == kTypeValue) {
            uint32_t val_len;
            const char* val_ptr = GetVarint32Ptr(p, limit, &val_len);
            if (val_ptr == NULL || val_ptr + val_len > limit) {
                return Status_Corruption("bad value length", NULL);
            }
            value = Slice_Create(val_ptr, val_len);
            p = val_ptr + val_len;
        }

        Status s = Status_OK();
        if (tag == kTypeValue && handler->Put) {
            s = handler->Put(handler->arg, key, value);
        } else if (tag == kTypeDeletion && handler->Delete) {
            s = handler->Delete(handler->arg, key);
        }
        if (!Status_IsOK(s)) {
            return s;
        }
        seen++;
    }

    uint32_t expected = DecodeFixed32(data + 8);
    if (seen != expected) {
        return Status_Corruption("write count mismatch", NULL);
    }
    return Status_OK();
}

Status WriteBatch_Iterate(const Lithos_WriteBatch* batch, WriteBatchHandler* handler) {
    if (batch == NULL || handler == NULL) {
        return Status_InvalidArgument("batch");
    }
    return IterateBytes(batch->rep, batch->size, handler);
}
