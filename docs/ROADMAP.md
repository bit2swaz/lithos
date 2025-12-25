### **Phase 1: The Bedrock (Memory & Basic Types)**

*Goal: Build the foundational utilities. Since we avoid standard `malloc` for data nodes, the Arena is critical.*

#### **Mini-Phase 1.1: Project Skeleton & Status Codes**

**Objective:** Set up the directory structure, build system (Makefile/CMake), and the `Status` type used everywhere for error propagation.
**Context:** We need a robust way to return errors (e.g., `LITHOS_NOT_FOUND`) without C++ exceptions.

**Prompt for AI IDE:**

> Act as a Senior C Systems Engineer.
> **Task:** Initialize the "Lithos-Titan" project structure.
> 1. Create a `include/lithos.h` with the `lithos_status` enum from the PRD.
> 2. Create a `src/util/status.h` and `status.c` that wraps these codes into a struct allowing for detailed error messages (e.g., `struct Status { code, message }`).
> 3. Create a `Makefile` with strict flags: `-Wall -Werror -std=c11 -pthread -O2`.
>
>
> **Constraint:** Do not use C++ features.
> **Documentation:** Add comments explaining how we check the status in a C-style workflow (e.g., `if (!s.ok())`).

#### **Mini-Phase 1.2: The Slice & Coding**

**Objective:** Create `Slice` (a pointer+length string view) and `Coding` (Varint64/32 encoding) to interact with the disk format.
**Context:** The PRD specifies "Varint64" for lengths. We need efficient encoding/decoding functions.

**Prompt for AI IDE:**

> **Task:** Implement `Slice` and `Coding` utilities.
> 1. **`src/util/slice.h`**: A struct `{ const char* data; size_t size; }` with helper methods (starts_with, compare).
> 2. **`src/util/coding.h`**: Implement `PutFixed32`, `PutFixed64`, `PutVarint32`, `PutVarint64`.
> 3. Implement the corresponding `Get...` decoding functions.
>
>
> **Crucial:** Add a comment block explaining **why** we use Varints (space saving) vs Fixed (random access) and how the Little Endian format is enforced on Big Endian machines (if applicable).

#### **Mini-Phase 1.3: The Arena Allocator**

**Objective:** Implement the bump-pointer allocator described in the PRD (3.1).
**Context:** This is the most critical memory component. It must align pointers to 8 bytes and allocate in 4KB blocks.

**Prompt for AI IDE:**

> **Task:** Implement the `Arena` allocator defined in PRD Section 3.1.
> **Requirements:**
> 1. Struct `Arena` with `char* alloc_ptr`, `size_t alloc_bytes_remaining`, and a vector/list of allocated memory blocks.
> 2. Function `char* Arena_Allocate(Arena* arena, size_t bytes)`:
> * If `bytes` fits in current block -> return ptr and bump `alloc_ptr`.
> * If not -> `malloc` a new 4KB block, store it in the vector, and allocate from there.
>
>
> 3. Ensure **8-byte alignment** for all returned pointers.
> 4. `Arena_MemoryUsage()` atomic counter.
>
>
> **Explanation:** Comment heavily on *why* we don't free individual pointers and how the alignment bitwise math works.

---

### **Phase 2: In-Memory Storage (SkipList & MemTable)**

*Goal: Implement the `MemTable` where data first lands.*

#### **Mini-Phase 2.1: The Concurrent SkipList**

**Objective:** A lock-free read, locked-write SkipList.
**Context:** This replaces `std::map`. It must support `Insert` and `Contains`.

**Prompt for AI IDE:**

> **Task:** Implement a template-like SkipList in C.
> **Specs:**
> 1. Nodes are allocated using the `Arena` from Phase 1.
> 2. Max height: 12.
> 3. **Concurrency:**
> * Writes use an external mutex (don't implement the mutex inside the list, just the atomic node pointers).
> * Reads must use `atomic_load` (memory_order_acquire).
> * Node `next` pointers must be `_Atomic`.
>
>
>
>
> **Teaching:** Explain the "Memory Barrier" concept in the comments. Why do we need `memory_order_release` on insert and `memory_order_acquire` on read?

#### **Mini-Phase 2.2: The MemTable Wrapper**

**Objective:** Wrap the SkipList with key encoding (InternalKey).
**Context:** Users provide "Key". We store "Key + SequenceNumber + Type".

**Prompt for AI IDE:**

> **Task:** Implement `MemTable` (PRD 3.2).
> 1. Define the "Internal Key" format: `| User Key (n) | SeqNum (7 bytes) | Type (1 byte) |`.
> 2. Implement a `KeyComparator` that compares User Keys ascending, but Sequence Numbers **descending** (so newer versions appear first).
> 3. `MemTable_Add(sequence, type, key, value)`: Encode key, insert into SkipList.
> 4. `MemTable_Get(key, value)`: Search SkipList.
>
>
> **Note:** Use the `Arena` for all node allocations. Implement Reference Counting for the MemTable.

---

### **Phase 3: Durability (The WAL)**

*Goal: Ensure data survives a crash. This involves file I/O.*

#### **Mini-Phase 3.1: File Abstraction & CRC32C**

**Objective:** Abstract OS file operations and implement checksums.

**Prompt for AI IDE:**

> **Task:** System Interfaces.
> 1. Implement `crc32c.h/c`.
> 2. Implement `Env` (Environment) abstraction:
> * `WritableFile`: Wraps `fopen` or `open`.
> * `SequentialFile`: For reading logs.
> * `RandomAccessFile`: For reading SSTables (pread).
>
>
>
>
> **Requirement:** Use strict POSIX calls (`fsync`, `fdatasync`) to ensure flush to disk. Explain the difference between `fflush` (userspace) and `fsync` (kernel/disk).

#### **Mini-Phase 3.2: Log Writer & Reader**

**Objective:** The 32KB block format described in PRD 4.1.

**Prompt for AI IDE:**

> **Task:** Implement `log_writer.c` and `log_reader.c`.
> **Specs (PRD 4.1):**
> * Block Size: 32KB.
> * Record Header: `CheckSum (4) | Length (2) | Type (1)`.
>
>
> **Logic:**
> * **Writer:** If a record > remaining block space, fragment it into `FIRST`, `MIDDLE`, `LAST` types.
> * **Reader:** Read blocks, verify Checksums, reassemble fragmented records.
>
>
> **Explanation:** Add comments walking through a scenario where a record is split across 3 blocks.

---

### **Phase 4: Immutable Storage (SSTables)**

*Goal: Write data to sorted files on disk. This is complex logic.*

#### **Mini-Phase 4.1: Block Builder**

**Objective:** Build the Data Blocks with prefix compression (PRD 4.2.A).

**Prompt for AI IDE:**

> **Task:** Implement `BlockBuilder`.
> **Logic:**
> 1. Keep track of `last_key` added.
> 2. On `Add(key, value)`:
> * Calculate shared prefix length with `last_key`.
> * Write `shared_len | non_shared_len | value_len | key_suffix | value`.
>
>
> 3. Update `last_key`.
> 4. Periodically (every 16 keys) reset prefix compression (restart point) to allow binary search.
>
>

#### **Mini-Phase 4.2: Table Builder**

**Objective:** Assemble blocks into a full SSTable file.

**Prompt for AI IDE:**

> **Task:** Implement `TableBuilder`.
> **Flow:**
> 1. Buffer data into `BlockBuilder`.
> 2. When block > 4KB, `Flush()` to file.
> 3. Compute Bloom Filter (PRD 4.2.B) for the block.
> 4. Create an "Index Block" entry: `LastKeyInBlock -> BlockOffset`.
> 5. **Finish:** Write Filter Block, Index Block, and Footer (Magic Number).
>
>
> **Detail:** Explain how the Footer allows us to open the file by reading the *end* of the file first.

#### **Mini-Phase 4.3: Table Reader**

**Objective:** Read and query SSTables.

**Prompt for AI IDE:**

> **Task:** Implement `TableReader`.
> 1. Read Footer (last 48 bytes).
> 2. Read Index Block.
> 3. `NewIterator()`: Returns an iterator that:
> * Binary searches the Index Block to find the target Data Block.
> * Loads the Data Block.
> * Binary searches inside the Data Block.
>
>
>
>

---

### **Phase 5: The VersionSet (MVCC Metadata)**

*Goal: Track which files belong to "current" state.*

#### **Mini-Phase 5.1: VersionEdit & Manifest**

**Objective:** Serialize metadata changes (e.g., "Deleted File X", "Added File Y").

**Prompt for AI IDE:**

> **Task:** Implement `VersionEdit` and `VersionSet`.
> 1. `VersionEdit`: A struct recording changes (new files, deleted files, log number, last sequence).
> 2. Encode `VersionEdit` to binary (similar to WAL).
> 3. `VersionSet`:
> * Manages a linked list of `Version` objects.
> * `LogAndApply(edit)`:
> * Create `new_version` based on `current_version` + `edit`.
> * Append `edit` to the `MANIFEST` file.
> * Update `current_version` pointer.
>
>
>
>
>
>
> **Concept:** Explain how this enables Snapshot Isolation (old iterators hold a ref to an old Version).

---

### **Phase 6: The Engine Core (DBImpl)**

*Goal: Tie it all together.*

#### **Mini-Phase 6.1: The Write Path**

**Objective:** `Put` -> WAL -> MemTable.

**Prompt for AI IDE:**

> **Task:** Implement `DBImpl::Write`.
> 1. Lock `mutex`.
> 2. Add record to `WriteBatch`.
> 3. Append `WriteBatch` to `WAL`.
> 4. Insert `WriteBatch` into `MemTable`.
> 5. If `MemTable` > 4MB:
> * Move to `ImmutableMemTable`.
> * Create new `MemTable`.
> * Trigger background compaction (placeholder).
>
>
> 6. Unlock `mutex`.
>
>

#### **Mini-Phase 6.2: The Read Path**

**Objective:** `Get` -> Mem -> Imm -> Version(SSTs).

**Prompt for AI IDE:**

> **Task:** Implement `DBImpl::Get`.
> 1. Acquire `Version` (increment refcount).
> 2. Check `MemTable`.
> 3. Check `ImmutableMemTable`.
> 4. Search SSTables in `Version` (Level 0 first, then L1-L6).
> 5. Release `Version`.
>
>
> **Optimization:** Explain why we search L0 files in reverse chronological order (newest file first).

---

### **Phase 7: Compaction (The Engine Room)**

*Goal: Background maintenance.*

#### **Mini-Phase 7.1: Background Thread & Logic**

**Objective:** Move data from L0 -> L1.

**Prompt for AI IDE:**

> **Task:** Implement Background Compaction.
> 1. `MaybeScheduleCompaction()`: Check if L0 > 4 files.
> 2. `BackgroundCall()`:
> * Pick inputs (L0 files overlapping with L1).
> * `DoCompactionWork()`:
> * Iterate over all input files (MergingIterator).
> * Discard overwritten keys (shadowed by newer sequence numbers).
> * Feed valid keys into `TableBuilder`.
>
>
> * Install new files via `VersionEdit`.
> * Delete old files.
>
>
>
>

---
