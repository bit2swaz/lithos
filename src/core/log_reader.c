
#include "log_reader.h"
#include "log_format.h"
#include "util/coding.h"
#include "util/crc32c.h"
#include "util/status.h"
#include <stdlib.h>
#include <string.h>

struct LogReader {
  Lithos_SequentialFile *file;
  bool checksum;

  char *buffer;
  size_t buffer_size;
  size_t buffer_offset;

  bool eof;
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

static bool ReadPhysicalRecord(LogReader *reader, Lithos_Slice *result,
                               RecordType *type) {
  while (true) {

    if (reader->buffer_offset >= reader->buffer_size) {
      if (reader->eof) {
        return false;
      }

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
        reader->eof = true;
      }

      if (block.size == 0) {
        return false;
      }
    }

    size_t remaining = reader->buffer_size - reader->buffer_offset;

    if (remaining < kHeaderSize) {

      reader->buffer_offset = reader->buffer_size;
      continue;
    }

    const char *header = reader->buffer + reader->buffer_offset;

    uint32_t crc = DecodeFixed32(header);
    uint16_t length = ((uint8_t)header[4]) | (((uint8_t)header[5]) << 8);
    uint8_t record_type = (uint8_t)header[6];

    if (!RecordType_IsValid(record_type)) {

      reader->buffer_offset = reader->buffer_size;
      continue;
    }

    if (kHeaderSize + (size_t)length > remaining) {

      reader->buffer_offset = reader->buffer_size;
      continue;
    }

    const char *payload = header + kHeaderSize;

    if (reader->checksum && record_type != kZeroType) {
      uint32_t expected_crc = crc32c_extend(0, header + 6, 1);
      expected_crc = crc32c_extend(expected_crc, payload, length);

      if (crc != expected_crc) {

        reader->buffer_offset += kHeaderSize + length;
        continue;
      }
    }

    reader->buffer_offset += kHeaderSize + length;

    if (record_type == kZeroType) {
      continue;
    }

    result->data = payload;
    result->size = length;
    *type = (RecordType)record_type;

    return true;
  }
}

bool LogReader_ReadRecord(LogReader *reader, Lithos_Slice *record,
                          char **scratch) {

  bool in_fragmented_record = false;
  char *fragment_buffer = NULL;
  size_t fragment_size = 0;
  size_t fragment_capacity = 0;

  while (true) {
    Lithos_Slice fragment;
    RecordType type;

    if (!ReadPhysicalRecord(reader, &fragment, &type)) {

      if (fragment_buffer != NULL) {
        free(fragment_buffer);
      }
      return false;
    }

    switch (type) {
    case kFullType:

      if (in_fragmented_record) {

        if (fragment_buffer != NULL) {
          free(fragment_buffer);
        }
        in_fragmented_record = false;
        fragment_buffer = NULL;
        fragment_size = 0;
        fragment_capacity = 0;
      }

      *record = fragment;
      return true;

    case kFirstType:

      if (in_fragmented_record) {

        if (fragment_buffer != NULL) {
          free(fragment_buffer);
        }
      }

      fragment_capacity = fragment.size * 2;
      fragment_buffer = (char *)malloc(fragment_capacity);
      if (fragment_buffer == NULL) {
        return false;
      }

      memcpy(fragment_buffer, fragment.data, fragment.size);
      fragment_size = fragment.size;
      in_fragmented_record = true;
      break;

    case kMiddleType:

      if (!in_fragmented_record) {

        continue;
      }

      if (fragment_size + fragment.size > fragment_capacity) {
        fragment_capacity = (fragment_size + fragment.size) * 2;
        char *new_buffer = (char *)realloc(fragment_buffer, fragment_capacity);
        if (new_buffer == NULL) {
          free(fragment_buffer);
          return false;
        }
        fragment_buffer = new_buffer;
      }

      memcpy(fragment_buffer + fragment_size, fragment.data, fragment.size);
      fragment_size += fragment.size;
      break;

    case kLastType:

      if (!in_fragmented_record) {

        continue;
      }

      if (fragment_size + fragment.size > fragment_capacity) {
        fragment_capacity = fragment_size + fragment.size;
        char *new_buffer = (char *)realloc(fragment_buffer, fragment_capacity);
        if (new_buffer == NULL) {
          free(fragment_buffer);
          return false;
        }
        fragment_buffer = new_buffer;
      }

      memcpy(fragment_buffer + fragment_size, fragment.data, fragment.size);
      fragment_size += fragment.size;

      record->data = fragment_buffer;
      record->size = fragment_size;

      if (*scratch != NULL) {
        free(*scratch);
      }
      *scratch = fragment_buffer;

      in_fragmented_record = false;
      return true;

    default:

      break;
    }
  }
}
