# architecture

this document provides a technical deep-dive into lithos internals for contributors and advanced users.

## overview

lithos is a leveldb-compatible lsm-tree key-value store implemented in c11. it provides:

- persistent, crash-consistent storage via write-ahead logging
- high write throughput via in-memory buffering with background compaction
- mvcc (multi-version concurrency control) via snapshots
- efficient reads via bloom filters and block caching

## directory structure

```
lithos/
├── include/           # public headers
│   ├── lithos.h       # main api entry point
│   └── lithos/        # submodule headers
├── src/
│   ├── core/          # core database logic
│   │   ├── db_impl.c  # database implementation
│   │   ├── memtable.c # in-memory skiplist
│   │   ├── skiplist.c # lock-free skiplist
│   │   ├── version_set.c  # mvcc and file management
│   │   └── table/     # sstable implementation
│   └── util/          # utilities (arena, cache, coding, etc.)
├── tests/             # unit tests
├── tools/             # cli, stress test, fuzz test
└── docs/              # documentation
```

## key data structures

### internal key format

all keys in lithos are stored as "internal keys" which combine the user key with versioning information:

```
+------------------+------------------+
|    user key      |   8-byte trailer |
+------------------+------------------+
                   |
                   v
    +-----------------------------------+
    | sequence number (56 bits) | type (8 bits) |
    +-----------------------------------+
```

- **sequence number**: 56-bit monotonically increasing counter assigned to each write
- **type**: `ktypevalue` (1) for puts, `ktypedeletion` (0) for deletes

the `internalkeycomparator` sorts by:
1. user key (ascending, bytewise)
2. sequence number (descending - newer versions first)
3. type (descending)

### filemetadata

metadata for each sst file stored in the manifest:

```c
typedef struct filemetadata {
    int refs;              // reference count
    uint64_t number;       // unique file number
    uint64_t file_size;    // file size in bytes
    lithos_slice smallest; // smallest internal key
    lithos_slice largest;  // largest internal key
    sequencenumber max_sequence;  // highest sequence in file (for recovery)
} filemetadata;
```

the `max_sequence` field was added in v1.0.1 to support proper sequence number recovery on database open.

### memtable

in-memory sorted buffer backed by a skiplist:

- arena-allocated for fast allocation and bulk deallocation
- lock-free reads with mutex-protected writes
- swapped to immutable (`imm`) when full, then flushed to l0

### version and versionset

mvcc is implemented via reference-counted `version` objects:

- each `version` represents a consistent view of the database
- `versionset` manages the current version and version history
- snapshots hold references to versions to prevent deletion

## write path

```
client put/delete
       │
       v
┌─────────────────┐
│   writebatch    │  (group mutations atomically)
└────────┬────────┘
         │
    ┌────┴────┐
    v         v
┌───────┐ ┌──────────┐
│  wal  │ │ memtable │
└───────┘ └────┬─────┘
               │ (when full)
               v
        ┌──────────────┐
        │  immutable   │
        │   memtable   │
        └──────┬───────┘
               │ (background flush)
               v
        ┌──────────────┐
        │   l0 sst     │
        └──────────────┘
```

### sequence number assignment

1. each `writebatch` is assigned a sequence number range
2. individual operations within the batch get consecutive sequences
3. on db open, `last_sequence` is recovered by scanning all sst `max_sequence` values

## read path

```
client get(key, snapshot)
       │
       v
┌─────────────────┐
│    memtable     │ ──miss──┐
└────────┬────────┘         │
         │found             │
         v                  v
      return         ┌──────────────┐
                     │   immutable  │ ──miss──┐
                     │   memtable   │         │
                     └──────┬───────┘         │
                            │found            │
                            v                 v
                         return        ┌────────────┐
                                       │ tablecache │
                                       │  (l0→ln)   │
                                       └────────────┘
```

### bloom filter optimization

before reading data blocks, the bloom filter is checked:
- if key is definitely not in file, skip disk i/o
- false positive rate ~1% with 10 bits per key

## compaction

### leveled strategy

- **l0**: files may overlap (created directly from memtable flush)
- **l1+**: files have disjoint key ranges, sorted

### compaction algorithm

```
1. score levels:
   - l0: num_files / 4
   - l1+: total_bytes / level_limit

2. pick highest-scoring level as input

3. select input files:
   - from level n: one file (or all overlapping in l0)
   - from level n+1: all files overlapping input key range

4. merge using k-way merge iterator:
   - drop tombstones if no higher-level overlap
   - drop old versions if below smallest snapshot

5. output new sst files

6. atomic commit via versionedit to manifest
```

### iterator ownership

**critical invariant**: the merge iterator owns its child iterators.

```c
// correct: let merge iterator own children
lithos_iterator* merge = newmergingiterator(children, n);
// ... use merge iterator ...
iter_destroy(merge);  // destroys children too

// wrong: double-free
iter_destroy(merge);
for (int i = 0; i < n; i++) {
    iter_destroy(children[i]);  // already freed!
}
```

## memory management

### reference counting

both `filemetadata` and `memtable` use reference counting:

```c
// filemetadata
filemetadata_ref(meta);   // increment
filemetadata_unref(meta); // decrement, free if zero

// memtable
memtable_ref(mem);   // increment
memtable_unref(mem); // decrement, free if zero
```

### compactmemtable ref flow

```
1. db->imm holds memtable (refs = 1)
2. compactmemtable:
   a. memtable_ref(imm)        → refs = 2
   b. write to sst
   c. memtable_unref(imm)      → refs = 1 (release local ref)
   d. memtable_unref(imm)      → refs = 0 (release db->imm ref)
   e. db->imm = null
```

### arena allocator

memtable uses an arena for efficient memory management:

- allocates in 4kb blocks
- bump-pointer allocation (o(1))
- all memory freed when arena is destroyed
- 8-byte alignment for all allocations

## sstable format

```
┌─────────────────────────┐
│     data block 0        │
├─────────────────────────┤
│     data block 1        │
├─────────────────────────┤
│         ...             │
├─────────────────────────┤
│     filter block        │ (bloom filters)
├─────────────────────────┤
│   meta index block      │ (points to filter)
├─────────────────────────┤
│     index block         │ (key → block offset)
├─────────────────────────┤
│     footer (48 bytes)   │ (magic + handles)
└─────────────────────────┘
```

### data block format (prefix compressed)

```
for each key-value:
  [sharedlen varint] [nonsharedlen varint] [valuelen varint]
  [key suffix (nonsharedlen bytes)]
  [value (valuelen bytes)]

block trailer:
  [restarts array] [numrestarts (4 bytes)] [compressiontype (1 byte)]
```

## concurrency model

- **single writer**: all writes hold `db->mu` mutex
- **lock-free reads**: readers take snapshots and read without locks
- **background thread**: at most one compaction thread at a time

```
write:   lock(mu) → append wal → insert memtable → unlock(mu)
read:    ref(version) → search memtable/ssts → unref(version)
compact: lock(mu) → pick files → unlock(mu) → merge → lock(mu) → apply → unlock(mu)
```

## recovery

on database open:

1. **manifest replay**: restore file metadata and version state
2. **sequence recovery**: scan all sst `max_sequence` to restore `last_sequence`
3. **wal replay**: replay any unflushed wal records to memtable

```c
// sequence recovery (v1.0.1+)
for (level = 0; level < knumlevels; level++) {
    for (file in level) {
        if (file->max_sequence > recovered_seq) {
            recovered_seq = file->max_sequence;
        }
    }
}
db->last_sequence = recovered_seq;
```
