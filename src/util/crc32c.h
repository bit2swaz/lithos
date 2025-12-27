/**
 * crc32c.h - CRC32C (Castagnoli) Checksum Implementation
 *
 * Author: Aditya (@bit2swaz)
 *
 * CRC32C (Castagnoli Polynomial: 0x1EDC6F41) is the standard checksum for
 * modern storage systems (iSCSI, SCTP, Btrfs, LevelDB, RocksDB).
 *
 * Why Castagnoli vs IEEE 802.3?
 * - IEEE (0x04C11DB7) was designed for Ethernet (detecting bit flips in
 * transmission).
 * - Castagnoli has superior error detection for burst errors common in storage
 * media (e.g., disk sector corruption, flash page failures).
 * - SSE4.2 and ARM8 CPUs provide hardware acceleration for CRC32C via
 * intrinsics.
 *
 * This implementation uses a software lookup table (256 entries).
 * Future: Detect CPU capabilities and switch to hardware accelerated version.
 */

#ifndef LITHOS_CRC32C_H
#define LITHOS_CRC32C_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * crc32c_value - Compute CRC32C checksum of data.
 *
 * @param data: Pointer to data buffer.
 * @param n: Number of bytes.
 *
 * Returns: 32-bit CRC checksum.
 *
 * Example:
 *   uint32_t crc = crc32c_value("hello", 5);
 */
uint32_t crc32c_value(const char *data, size_t n);

/**
 * crc32c_extend - Incrementally update an existing CRC.
 *
 * @param crc: Previous CRC value (or 0 for initial call).
 * @param data: Additional data to include.
 * @param n: Number of bytes.
 *
 * Returns: Updated CRC checksum.
 *
 * Use Case: Streaming computation over multiple blocks.
 * Example:
 *   uint32_t crc = 0;
 *   crc = crc32c_extend(crc, "Part1", 5);
 *   crc = crc32c_extend(crc, "Part2", 5);
 */
uint32_t crc32c_extend(uint32_t crc, const char *data, size_t n);

#ifdef __cplusplus
}
#endif

#endif // LITHOS_CRC32C_H
