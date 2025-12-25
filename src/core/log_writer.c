/**
 * log_writer.c - Write-Ahead Log Writer Implementation
 * 
 * Author: Aditya (@bit2swaz)
 * 
 * Key Implementation Details:
 * 
 * 1. **Block Alignment:** The writer tracks `block_offset` (0-32767).
 *    When a block fills up, it starts a new one.
 * 
 * 2. **Header Encoding:**
 *    - Checksum: CRC32C of (Type byte + Payload) in Little-Endian.
 *    - Length: uint16_t payload size in Little-Endian.
 *    - Type: Single byte (RecordType enum).
 * 
 * 3. **Fragmentation Logic:**
 *    - If record fits: Write as FULL.
 *    - If record spans blocks: Write FIRST, then loop MIDDLE, finally LAST.
 * 
 * 4. **Zero Padding:**
 *    - If remaining block space < kHeaderSize (7 bytes), fill with zeros.
 */

#include "log_writer.h"
#include "log_format.h"
#include "util/crc32c.h"
#include "util/coding.h"
#include <stdlib.h>
#include <string.h>

struct LogWriter {
    Lithos_WritableFile* dest;  // Output file
    int block_offset;            // Current position in block (0 to kBlockSize-1)
};

LogWriter* LogWriter_Create(Lithos_WritableFile* dest) {
    LogWriter* writer = (LogWriter*)malloc(sizeof(LogWriter));
    if (writer == NULL) {
        return NULL;
    }
    
    writer->dest = dest;
    writer->block_offset = 0;
    
    return writer;
}

void LogWriter_Destroy(LogWriter* writer) {
    if (writer != NULL) {
        free(writer);
    }
}

/**
 * EmitPhysicalRecord - Write a single physical record (header + payload).
 * 
 * @param writer: LogWriter handle.
 * @param type: RecordType (FULL, FIRST, MIDDLE, LAST).
 * @param ptr: Pointer to payload data.
 * @param length: Payload length (must fit in uint16_t).
 * 
 * Returns: Status_OK() or Status_IOError().
 * 
 * This function writes:
 *   [CRC32C (4B)] [Length (2B)] [Type (1B)] [Payload (length bytes)]
 */
static Status EmitPhysicalRecord(LogWriter* writer, RecordType type,
                                  const char* ptr, size_t length) {
    // Sanity check: payload must fit in uint16_t
    if (length > kMaxRecordSize) {
        return Status_InvalidArgument("Payload too large for single record");
    }
    
    // Build header in temporary buffer
    char header[kHeaderSize];
    
    // Encode Length (Little-Endian uint16_t)
    header[4] = (char)(length & 0xff);
    header[5] = (char)((length >> 8) & 0xff);
    
    // Encode Type
    header[6] = (char)type;
    
    // Compute CRC32C of (Type + Payload)
    // CRC includes the type byte to detect corruption in the type field
    uint32_t crc = crc32c_extend(0, header + 6, 1);  // Type byte
    crc = crc32c_extend(crc, ptr, length);           // Payload
    
    // Encode CRC (Little-Endian uint32_t)
    EncodeFixed32(header, crc);
    
    // Write header
    Lithos_Slice header_slice = {header, kHeaderSize};
    Status s = WritableFile_Append(writer->dest, header_slice);
    if (!Status_IsOK(s)) {
        return s;
    }
    
    // Write payload
    Lithos_Slice payload_slice = {ptr, length};
    s = WritableFile_Append(writer->dest, payload_slice);
    if (!Status_IsOK(s)) {
        return s;
    }
    
    // Update block offset
    writer->block_offset += kHeaderSize + (int)length;
    
    return Status_OK();
}

Status LogWriter_AddRecord(LogWriter* writer, Lithos_Slice slice) {
    const char* ptr = slice.data;
    size_t left = slice.size;
    Status s;
    
    // Loop until entire record is written
    bool begin = true;  // Is this the first fragment?
    
    do {
        // Calculate remaining space in current block
        const int leftover = kBlockSize - writer->block_offset;
        
        // If we don't have enough space for a header, pad and move to next block
        if (leftover < kHeaderSize) {
            // Fill rest of block with zeros
            if (leftover > 0) {
                char zeros[kHeaderSize] = {0};  // kHeaderSize is max leftover
                Lithos_Slice padding = {zeros, (size_t)leftover};
                s = WritableFile_Append(writer->dest, padding);
                if (!Status_IsOK(s)) {
                    return s;
                }
            }
            writer->block_offset = 0;
        }
        
        // Recalculate available space (we might have moved to a new block)
        const int avail = kBlockSize - writer->block_offset - kHeaderSize;
        
        // Calculate fragment size (how much of the record fits in this block)
        const size_t fragment_length = (left < (size_t)avail) ? left : (size_t)avail;
        
        // Determine record type
        RecordType type;
        const bool end = (left == fragment_length);  // Is this the last fragment?
        
        if (begin && end) {
            type = kFullType;
        } else if (begin) {
            type = kFirstType;
        } else if (end) {
            type = kLastType;
        } else {
            type = kMiddleType;
        }
        
        // Write the physical record
        s = EmitPhysicalRecord(writer, type, ptr, fragment_length);
        if (!Status_IsOK(s)) {
            return s;
        }
        
        // Move pointers
        ptr += fragment_length;
        left -= fragment_length;
        begin = false;
        
    } while (left > 0);
    
    return Status_OK();
}
