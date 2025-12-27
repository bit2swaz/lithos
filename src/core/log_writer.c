
#include "log_writer.h"
#include "log_format.h"
#include "util/coding.h"
#include "util/crc32c.h"
#include <stdlib.h>
#include <string.h>

struct LogWriter {
  Lithos_WritableFile *dest;
  int block_offset;
};

LogWriter *LogWriter_Create(Lithos_WritableFile *dest) {
  LogWriter *writer = (LogWriter *)malloc(sizeof(LogWriter));
  if (writer == NULL) {
    return NULL;
  }

  writer->dest = dest;
  writer->block_offset = 0;

  return writer;
}

void LogWriter_Destroy(LogWriter *writer) {
  if (writer != NULL) {
    free(writer);
  }
}

static Status EmitPhysicalRecord(LogWriter *writer, RecordType type,
                                 const char *ptr, size_t length) {

  if (length > kMaxRecordSize) {
    return Status_InvalidArgument("Payload too large for single record");
  }

  char header[kHeaderSize];

  header[4] = (char)(length & 0xff);
  header[5] = (char)((length >> 8) & 0xff);

  header[6] = (char)type;

  uint32_t crc = crc32c_extend(0, header + 6, 1);
  crc = crc32c_extend(crc, ptr, length);

  EncodeFixed32(header, crc);

  Lithos_Slice header_slice = {header, kHeaderSize};
  Status s = WritableFile_Append(writer->dest, header_slice);
  if (!Status_IsOK(s)) {
    return s;
  }

  Lithos_Slice payload_slice = {ptr, length};
  s = WritableFile_Append(writer->dest, payload_slice);
  if (!Status_IsOK(s)) {
    return s;
  }

  writer->block_offset += kHeaderSize + (int)length;

  return Status_OK();
}

Status LogWriter_AddRecord(LogWriter *writer, Lithos_Slice slice) {
  const char *ptr = slice.data;
  size_t left = slice.size;
  Status s;

  bool begin = true;

  do {

    const int leftover = kBlockSize - writer->block_offset;

    if (leftover < kHeaderSize) {

      if (leftover > 0) {
        char zeros[kHeaderSize] = {0};
        Lithos_Slice padding = {zeros, (size_t)leftover};
        s = WritableFile_Append(writer->dest, padding);
        if (!Status_IsOK(s)) {
          return s;
        }
      }
      writer->block_offset = 0;
    }

    const int avail = kBlockSize - writer->block_offset - kHeaderSize;

    const size_t fragment_length =
        (left < (size_t)avail) ? left : (size_t)avail;

    RecordType type;
    const bool end = (left == fragment_length);

    if (begin && end) {
      type = kFullType;
    } else if (begin) {
      type = kFirstType;
    } else if (end) {
      type = kLastType;
    } else {
      type = kMiddleType;
    }

    s = EmitPhysicalRecord(writer, type, ptr, fragment_length);
    if (!Status_IsOK(s)) {
      return s;
    }

    ptr += fragment_length;
    left -= fragment_length;
    begin = false;

  } while (left > 0);

  return Status_OK();
}
