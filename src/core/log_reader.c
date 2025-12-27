/**
 * log_reader.c - Write-Ahead Log Reader Implementation
 *
 * Author: Aditya (@bit2swaz)
 *
 * Key Implementation Details:
 *
 * 1. **Block Buffering:** The reader maintains a 32KB buffer to minimize
 *    system calls. It reads one block at a time.
 *
 * 2. **Checksum Verification:** If enabled, CRC32C is computed and compared.
 *    Mismatches result in read failure.
 *
 * 3. **Fragmentation Assembly:**
 *    - State machine tracks whether we're in the middle of a fragmented record.
 *    - Fragments are accumulated in a dynamically growing buffer.
 *
 * 4. **EOF Handling:** When SequentialFile_Read returns < kBlockSize,
 *    we've reached the end of the file.
 */

#include "log_reader.h"
#include "log_format.h"
#include "util/coding.h"
#include "util/crc32c.h"
#include "util/status.h"
#include <stdlib.h>
#include <string.h>

struct LogReader {
  Lithos_SequentialFile *file; // Input file
  bool checksum;               // Verify checksums?

  char *buffer;         // Block buffer (kBlockSize bytes)
  size_t buffer_size;   // Bytes currently in buffer
  size_t buffer_offset; // Read position in buffer

  bool eof; // Have we hit end of file?
};

LogReader *LogReader_Create(Lithos_SequentialFile *file, bool checksum) {
  LogReader *reader = (LogReader *)malloc(sizeof(LogReader));
  if (reader == NULL) {
    return NULL;
  }

  reader->file = file;
  reader->checksum = checksum;
  reader->buffer = (char *)malloc(kBlockSize);
  reader->buffer_size = 0;
  reader->buffer_offset = 0;
  reader->eof = false;

  if (reader->buffer == NULL) {
    free(reader);
    return NULL;
  }

  return reader;
}

void LogReader_Destroy(LogReader *reader) {
  if (reader != NULL) {
    if (reader->buffer != NULL) {
      free(reader->buffer);
    }
    free(reader);
  }
}

/**
 * ReadPhysicalRecord - Read one physical record from the current block.
 *
 * @param reader: LogReader handle.
 * @param result: Output slice pointing to payload.
 * @param type: Output record type.
 *
 * Returns: true on success, false on EOF or error.
 *
 * The returned slice points directly into the reader's internal buffer.
 * It is valid until the next call to ReadPhysicalRecord.
 */
static bool ReadPhysicalRecord(LogReader *reader, Lithos_Slice *result,
                               RecordType *type) {
  while (true) {
    // If we've consumed the current block, read the next one
    if (reader->buffer_offset >= reader->buffer_size) {
      if (reader->eof) {
        return false; // No more data
      }

      // Read next block
      Lithos_Slice block;
      Status s =
          SequentialFile_Read(reader->file, kBlockSize, &block, reader->buffer);

      if (!Status_IsOK(s)) {
        reader->eof = true;
        return false;
      }

      reader->buffer_size = block.size;
      reader->buffer_offset = 0;

      if (block.size < kBlockSize) {
        reader->eof = true; // Partial block = EOF
      }

      if (block.size == 0) {
        return false; // Empty block
      }
    }

    // Calculate remaining bytes in current block
    size_t remaining = reader->buffer_size - reader->buffer_offset;

    // Need at least a header
    if (remaining < kHeaderSize) {
      // Treat as end of block (likely zero padding); hop to next block.
      reader->buffer_offset = reader->buffer_size;
      continue;
    }

    // Parse header
    const char *header = reader->buffer + reader->buffer_offset;

    uint32_t crc = DecodeFixed32(header);
    uint16_t length = ((uint8_t)header[4]) | (((uint8_t)header[5]) << 8);
    uint8_t record_type = (uint8_t)header[6];

    // Validate record type
    if (!RecordType_IsValid(record_type)) {
      // Corruption: invalid type
      reader->buffer_offset = reader->buffer_size; // Skip to next block
      continue;
    }

    // Check if we have enough data for the payload
    if (kHeaderSize + (size_t)length > remaining) {
      // Corruption: payload extends beyond block
      reader->buffer_offset = reader->buffer_size;
      continue;
    }

    // Extract payload slice within the block
    const char *payload = header + kHeaderSize;

    // Verify checksum if enabled
    if (reader->checksum && record_type != kZeroType) {
      uint32_t expected_crc = crc32c_extend(0, header + 6, 1); // Type byte
      expected_crc = crc32c_extend(expected_crc, payload, length);

      if (crc != expected_crc) {
        // Checksum mismatch
        reader->buffer_offset += kHeaderSize + length;
        continue; // Skip this record
      }
    }

    // Advance offset
    reader->buffer_offset += kHeaderSize + length;

    // Skip zero-type records (padding)
    if (record_type == kZeroType) {
      continue;
    }

    // Return the record
    result->data = payload;
    result->size = length;
    *type = (RecordType)record_type;

    return true;
  }
}

bool LogReader_ReadRecord(LogReader *reader, Lithos_Slice *record,
                          char **scratch) {
  // State for accumulating fragments
  // `scratch` acts as caller-owned buffer; we transfer ownership of the
  // assembled record here so the caller can reuse or free after use.
  bool in_fragmented_record = false;
  char *fragment_buffer = NULL;
  size_t fragment_size = 0;
  size_t fragment_capacity = 0;

  while (true) {
    Lithos_Slice fragment;
    RecordType type;

    if (!ReadPhysicalRecord(reader, &fragment, &type)) {
      // EOF or error
      if (fragment_buffer != NULL) {
        free(fragment_buffer);
      }
      return false;
    }

    switch (type) {
    case kFullType:
      // Complete record
      if (in_fragmented_record) {
        // Unexpected FULL in middle of fragmented record (corruption)
        if (fragment_buffer != NULL) {
          free(fragment_buffer);
        }
        in_fragmented_record = false;
        fragment_buffer = NULL;
        fragment_size = 0;
        fragment_capacity = 0;
      }

      // Return the record directly (zero-copy)
      *record = fragment;
      return true;

    case kFirstType:
      // Start of fragmented record
      if (in_fragmented_record) {
        // Unexpected FIRST in middle of fragmented record (corruption)
        if (fragment_buffer != NULL) {
          free(fragment_buffer);
        }
      }

      // Initialize accumulation buffer
      fragment_capacity = fragment.size * 2; // Heuristic initial size
      fragment_buffer = (char *)malloc(fragment_capacity);
      if (fragment_buffer == NULL) {
        return false; // Out of memory
      }

      memcpy(fragment_buffer, fragment.data, fragment.size);
      fragment_size = fragment.size;
      in_fragmented_record = true;
      break;

    case kMiddleType:
      // Middle of fragmented record
      if (!in_fragmented_record) {
        // Unexpected MIDDLE without FIRST (corruption)
        continue;
      }

      // Expand buffer if needed
      if (fragment_size + fragment.size > fragment_capacity) {
        fragment_capacity = (fragment_size + fragment.size) * 2;
        char *new_buffer = (char *)realloc(fragment_buffer, fragment_capacity);
        if (new_buffer == NULL) {
          free(fragment_buffer);
          return false; // Out of memory
        }
        fragment_buffer = new_buffer;
      }

      memcpy(fragment_buffer + fragment_size, fragment.data, fragment.size);
      fragment_size += fragment.size;
      break;

    case kLastType:
      // End of fragmented record
      if (!in_fragmented_record) {
        // Unexpected LAST without FIRST (corruption)
        continue;
      }

      // Expand buffer if needed
      if (fragment_size + fragment.size > fragment_capacity) {
        fragment_capacity = fragment_size + fragment.size;
        char *new_buffer = (char *)realloc(fragment_buffer, fragment_capacity);
        if (new_buffer == NULL) {
          free(fragment_buffer);
          return false; // Out of memory
        }
        fragment_buffer = new_buffer;
      }

      memcpy(fragment_buffer + fragment_size, fragment.data, fragment.size);
      fragment_size += fragment.size;

      // Return the complete record
      record->data = fragment_buffer;
      record->size = fragment_size;

      // Transfer ownership to caller's scratch buffer
      if (*scratch != NULL) {
        free(*scratch);
      }
      *scratch = fragment_buffer;

      in_fragmented_record = false;
      return true;

    default:
      // Should never reach here (RecordType_IsValid checks this)
      break;
    }
  }
}
