# design invariants

lithos relies on the following invariants to stay correct and diagnosable. changes must preserve or explicitly update these rules.

## state and durability
- wal-before-data: every mutation is appended to `wal.log` before it becomes visible in the active memtable.
- single active memtable: exactly one mutable memtable (`mem`) at a time; a flush swaps it into `imm` and creates a fresh `mem`.
- manifest is source of truth: `manifest` (versionedits) is the only durable catalog for sst ownership; in-memory `version` mirrors it.
- file numbers are monotonic: `versionset_newfilenumber` never reuses numbers; manifest replay must restore `next_file_number`.
- sequence numbers persist: `filemetadata.max_sequence` stores the highest sequence in each sst; db open scans all files to restore `last_sequence` and prevent reuse.

## memory and ownership
- arena ownership: each memtable owns its arena; refcounts guard shared views. flush completion drops both worker and db refs to release memory.
- filemetadata refs: every `filemetadata` is refcounted by versions (and temp users) and is unrefed on both success and failure paths.
- alignment: arena allocations are 8-byte aligned; skiplist nodes and encoded entries assume this.
- cache handles: `tablecache` handles must be released via `cache_release`/iterator cleanup paths.
- merge iterator ownership: `mergingiterator` owns its child iterators and destroys them on cleanup; callers must not destroy children separately.
- memtable ref symmetry: every `memtable_ref` must have a corresponding `memtable_unref`; `compactmemtable` refs for local work and unrefs twice on completion (once for local, once for db->imm).

## concurrency
- db mutex: all writes and version changes hold `db->mu`; readers take snapshots (`version_ref`) before releasing the mutex.
- background thread: at most one compaction/flush thread at a time; joins occur before shutdown and before re-launch to keep lsan clean.
- snapshots are monotonic: `snapshotlist` preserves order; `oldest_snapshot` is updated on add/remove.

## lsm and compaction
- l0 may overlap; l1+ do not: level-0 files can overlap; levels 1..n maintain disjoint, sorted ranges.
- drop rules: during compaction, entries older than `smallest_snapshot` or masked by deletions at higher levels are dropped.
- trivial move correctness: a trivial move only when exactly one input in level and none in level+1 overlap.

## io and format
- checksummed tables: reads verify block checksums; corruption surfaces as `lithos_corruption`/`lithos_io_error`.
- footer invariants: sst footer is fixed-length; index/metaindex handles must be valid and within file bounds.
- log format: wal record checksum covers type+payload; fragmentation rules must be preserved.

## testing and qa
- sanitizers clean: asan/ubsan builds for stress and fuzz must run leak/ub free while still detecting injected sst corruption.
- valgrind clean: `valgrind --leak-check=full` reports 0 bytes lost on `lithos_cli fill` smoke.
- internal key format: tests using `tablebuilder_add` or iterator seeks must supply internal keys (user key + 8-byte packed sequence/type), not raw user keys.
