# Design Invariants

Lithos relies on the following invariants to stay correct and diagnosable. Changes must preserve or explicitly update these rules.

## State & Durability
- **WAL-before-data:** Every mutation is appended to `wal.log` before it becomes visible in the active MemTable.
- **Single active MemTable:** Exactly one mutable MemTable (`mem`) at a time; a flush swaps it into `imm` and creates a fresh `mem`.
- **Manifest is source of truth:** `MANIFEST` (VersionEdits) is the only durable catalog for SST ownership; in-memory `Version` mirrors it.
- **File numbers are monotonic:** `VersionSet_NewFileNumber` never reuses numbers; Manifest replay must restore `next_file_number`.

## Memory & Ownership
- **Arena ownership:** Each MemTable owns its Arena; refcounts guard shared views. Flush completion drops both worker and DB refs to release memory.
- **FileMetaData refs:** Every `FileMetaData` is refcounted by `Version`s (and temp users) and is unrefed on both success and failure paths.
- **Alignment:** Arena allocations are 8-byte aligned; SkipList nodes and encoded entries assume this.
- **Cache handles:** `TableCache` handles must be released via `Cache_Release`/iterator cleanup paths.

## Concurrency
- **DB mutex:** All writes and version changes hold `db->mu`; readers take snapshots (`Version_Ref`) before releasing the mutex.
- **Background thread:** At most one compaction/flush thread at a time; joins occur before shutdown and before re-launch to keep LSAN clean.
- **Snapshots are monotonic:** `SnapshotList` preserves order; `oldest_snapshot` is updated on add/remove.

## LSM / Compaction
- **L0 may overlap; L1+ do not:** Level-0 files can overlap; Levels 1..N maintain disjoint, sorted ranges.
- **Drop rules:** During compaction, entries older than `smallest_snapshot` or masked by deletions at higher levels are dropped.
- **Trivial move correctness:** A trivial move only when exactly one input in level and none in level+1 overlap.

## IO & Format
- **Checksummed tables:** Reads verify block checksums; corruption surfaces as `LITHOS_CORRUPTION`/`LITHOS_IO_ERROR`.
- **Footer invariants:** SST footer is fixed-length; index/metaindex handles must be valid and within file bounds.
- **Log format:** WAL record checksum covers type+payload; fragmentation rules must be preserved.

## Testing & QA
- **Sanitizers clean:** ASan/UBSan builds for stress and fuzz must run leak/UB free while still detecting injected SST corruption.
- **Valgrind clean:** `valgrind --leak-check=full` reports 0 bytes lost on `lithos_cli fill` smoke.
