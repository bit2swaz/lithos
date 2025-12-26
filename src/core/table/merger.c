/* Merging iterator: K-way merge over child iterators. */

#include "core/table/merger.h"
#include "core/dbformat.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>

typedef struct {
    Lithos_Iterator** children;
    int num;
    int current; /* index of child holding the smallest key */
    int (*cmp)(const void*, const void*);
    Status status;
} MergingIterator;

static void Merging_FindSmallest(MergingIterator* m) {
    m->current = -1;
    for (int i = 0; i < m->num; i++) {
        if (m->children[i] == NULL) continue;
        if (!Lithos_Iter_Valid(m->children[i])) continue;
        if (m->current == -1) {
            m->current = i;
        } else {
            Lithos_Slice a = Lithos_Iter_Key(m->children[i]);
            Lithos_Slice b = Lithos_Iter_Key(m->children[m->current]);
            if (m->cmp(&a, &b) < 0) {
                m->current = i;
            }
        }
    }
}

static bool Merging_Valid(void* state) {
    MergingIterator* m = (MergingIterator*)state;
    return m->current >= 0 && m->current < m->num &&
           Lithos_Iter_Valid(m->children[m->current]);
}

static void Merging_SeekToFirst(void* state) {
    MergingIterator* m = (MergingIterator*)state;
    for (int i = 0; i < m->num; i++) {
        if (m->children[i]) Lithos_Iter_SeekToFirst(m->children[i]);
    }
    Merging_FindSmallest(m);
}

static void Merging_SeekToLast(void* state) {
    MergingIterator* m = (MergingIterator*)state;
    for (int i = 0; i < m->num; i++) {
        if (m->children[i]) Lithos_Iter_SeekToLast(m->children[i]);
    }
    /* Choose max key */
    m->current = -1;
    for (int i = 0; i < m->num; i++) {
        if (m->children[i] && Lithos_Iter_Valid(m->children[i])) {
            if (m->current == -1) {
                m->current = i;
            } else {
                Lithos_Slice a = Lithos_Iter_Key(m->children[i]);
                Lithos_Slice b = Lithos_Iter_Key(m->children[m->current]);
                if (m->cmp(&a, &b) > 0) {
                    m->current = i;
                }
            }
        }
    }
}

static void Merging_Seek(void* state, Lithos_Slice target) {
    MergingIterator* m = (MergingIterator*)state;
    for (int i = 0; i < m->num; i++) {
        if (m->children[i]) Lithos_Iter_Seek(m->children[i], target);
    }
    Merging_FindSmallest(m);
}

static void Merging_Next(void* state) {
    MergingIterator* m = (MergingIterator*)state;
    assert(Merging_Valid(state));
    Lithos_Iter_Next(m->children[m->current]);
    Merging_FindSmallest(m);
}

static void Merging_Prev(void* state) {
    MergingIterator* m = (MergingIterator*)state;
    assert(Merging_Valid(state));
    Lithos_Iter_Prev(m->children[m->current]);
    /* After moving current child back, choose max key among valids. */
    m->current = -1;
    for (int i = 0; i < m->num; i++) {
        if (m->children[i] && Lithos_Iter_Valid(m->children[i])) {
            if (m->current == -1) {
                m->current = i;
            } else {
                Lithos_Slice a = Lithos_Iter_Key(m->children[i]);
                Lithos_Slice b = Lithos_Iter_Key(m->children[m->current]);
                if (m->cmp(&a, &b) > 0) {
                    m->current = i;
                }
            }
        }
    }
}

static Lithos_Slice Merging_Key(void* state) {
    MergingIterator* m = (MergingIterator*)state;
    assert(Merging_Valid(state));
    return Lithos_Iter_Key(m->children[m->current]);
}

static Lithos_Slice Merging_Value(void* state) {
    MergingIterator* m = (MergingIterator*)state;
    assert(Merging_Valid(state));
    return Lithos_Iter_Value(m->children[m->current]);
}

static Status Merging_GetStatus(void* state) {
    MergingIterator* m = (MergingIterator*)state;
    if (m->status.code != LITHOS_OK) return m->status;
    for (int i = 0; i < m->num; i++) {
        if (m->children[i]) {
            Status s = Lithos_Iter_GetStatus(m->children[i]);
            if (s.code != LITHOS_OK) return s;
        }
    }
    return Status_OK();
}

static void Merging_Cleanup(void* state) {
    MergingIterator* m = (MergingIterator*)state;
    for (int i = 0; i < m->num; i++) {
        if (m->children[i]) {
            Lithos_Iter_Destroy(m->children[i]);
        }
    }
    free(m->children);
    free(m);
}

static const Lithos_IteratorVTable kMergingVTable = {
    .Valid = Merging_Valid,
    .SeekToFirst = Merging_SeekToFirst,
    .SeekToLast = Merging_SeekToLast,
    .Seek = Merging_Seek,
    .Next = Merging_Next,
    .Prev = Merging_Prev,
    .Key = Merging_Key,
    .Value = Merging_Value,
    .GetStatus = Merging_GetStatus,
    .Cleanup = Merging_Cleanup,
};

Lithos_Iterator* NewMergingIterator(Lithos_Iterator** children,
                                    int num,
                                    int (*comparator)(const void*, const void*)) {
    if (num <= 0) return NULL;
    MergingIterator* m = calloc(1, sizeof(MergingIterator));
    if (!m) return NULL;
    m->children = calloc((size_t)num, sizeof(Lithos_Iterator*));
    if (!m->children) {
        free(m);
        return NULL;
    }
    for (int i = 0; i < num; i++) {
        m->children[i] = children[i];
    }
    m->num = num;
    m->current = -1;
    m->cmp = comparator ? comparator : InternalKeyComparator;
    m->status = Status_OK();

    Lithos_Iterator* it = malloc(sizeof(Lithos_Iterator));
    if (!it) {
        Merging_Cleanup(m);
        return NULL;
    }
    it->vtable = &kMergingVTable;
    it->state = m;
    Merging_FindSmallest(m);
    return it;
}
