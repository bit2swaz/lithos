#define _POSIX_C_SOURCE 200809L

#include "core/log_reader.h"
#include "core/log_writer.h"
#include "testharness.h"
#include "util/crc32c.h"
#include "util/env.h"
#include "util/slice.h"
#include <stdlib.h>
#include <string.h>

void Run_WALTests(void);

static void Test_DurabilityUtils(void) {
  printf(COLOR_BLUE
         "[Test] Durability Utils (CRC32C & File I/O)\n" COLOR_RESET);

  const char *test_data = "hello";
  uint32_t crc = crc32c_value(test_data, 5);

  printf("  CRC32C('%s') = 0x%08x\n", test_data, crc);

  ASSERT_EQ(crc, 0x9a71bb4c);
  printf(COLOR_GREEN "    Checksum verified\n" COLOR_RESET);

  uint32_t crc_inc = 0;
  crc_inc = crc32c_extend(crc_inc, "hel", 3);
  crc_inc = crc32c_extend(crc_inc, "lo", 2);

  ASSERT_EQ(crc_inc, crc);
  printf(COLOR_GREEN "    Incremental CRC verified\n" COLOR_RESET);

  const char *test_filename = "test_wal.log";
  Lithos_WritableFile *wf = NULL;

  Status s = Env_NewWritableFile(test_filename, &wf);
  ASSERT_OK(s);
  printf("  Created file: %s\n", test_filename);

  Lithos_Slice part1 = Slice_FromCString("Part1");
  s = WritableFile_Append(wf, part1);
  ASSERT_OK(s);
  printf("  Appended: 'Part1'\n");

  Lithos_Slice part2 = Slice_FromCString("Part2");
  s = WritableFile_Append(wf, part2);
  ASSERT_OK(s);
  printf("  Appended: 'Part2'\n");

  s = WritableFile_Flush(wf);
  ASSERT_OK(s);
  printf("  Flushed to disk\n");

  s = WritableFile_Close(wf);
  ASSERT_OK(s);
  printf("  File closed\n");

  Lithos_SequentialFile *rf = NULL;
  s = Env_NewSequentialFile(test_filename, &rf);
  ASSERT_OK(s);
  printf("  Opened for reading\n");

  char buffer[100];
  Lithos_Slice read_slice;
  s = SequentialFile_Read(rf, 10, &read_slice, buffer);
  ASSERT_OK(s);
  ASSERT_EQ(read_slice.size, 10);

  ASSERT_TRUE(memcmp(read_slice.data, "Part1Part2", 10) == 0);
  printf("  Read back: '%.*s'\n", (int)read_slice.size, read_slice.data);

  s = SequentialFile_Close(rf);
  ASSERT_OK(s);

  s = Env_DeleteFile(test_filename);
  ASSERT_OK(s);
  printf("  Test file cleaned up\n");
}

static void Test_LogFragmentation(void) {
  printf(COLOR_BLUE "[Test] WAL Fragmentation & Reassembly\n" COLOR_RESET);

  const char *test_filename = "test_wal_frag.log";

  Lithos_WritableFile *wfile = NULL;
  Status s = Env_NewWritableFile(test_filename, &wfile);
  ASSERT_OK(s);

  LogWriter *writer = LogWriter_Create(wfile);
  ASSERT_TRUE(writer != NULL);
  printf("  Created LogWriter\n");

  const char *small_data = "tiny";
  Lithos_Slice small_slice = Slice_FromCString(small_data);
  s = LogWriter_AddRecord(writer, small_slice);
  ASSERT_OK(s);
  printf("  Wrote small record: '%s' (%zu bytes)\n", small_data,
         small_slice.size);

  const size_t large_size = 70000;
  char *large_data = (char *)malloc(large_size);
  ASSERT_TRUE(large_data != NULL);

  for (size_t i = 0; i < large_size; i++) {
    large_data[i] = (char)('A' + (i % 26));
  }

  Lithos_Slice large_slice = {large_data, large_size};
  s = LogWriter_AddRecord(writer, large_slice);
  ASSERT_OK(s);
  printf("  Wrote large record: %zu bytes " COLOR_GREEN
         "(fragmented)" COLOR_RESET "\n",
         large_size);

  const char *small_data2 = "after_big";
  Lithos_Slice small_slice2 = Slice_FromCString(small_data2);
  s = LogWriter_AddRecord(writer, small_slice2);
  ASSERT_OK(s);
  printf("  Wrote second small record: '%s'\n", small_data2);

  s = WritableFile_Sync(wfile);
  ASSERT_OK(s);

  LogWriter_Destroy(writer);
  WritableFile_Close(wfile);
  printf(COLOR_GREEN "  Writer synced and closed\n" COLOR_RESET);

  Lithos_SequentialFile *rfile = NULL;
  s = Env_NewSequentialFile(test_filename, &rfile);
  ASSERT_OK(s);

  LogReader *reader =
      LogReader_Create(rfile, true);
  ASSERT_TRUE(reader != NULL);
  printf("  Created LogReader\n");

  char *scratch = NULL;
  Lithos_Slice record;

  ASSERT_TRUE(LogReader_ReadRecord(reader, &record, &scratch));

  ASSERT_EQ(record.size, strlen(small_data));
  ASSERT_TRUE(memcmp(record.data, small_data, record.size) == 0);
  printf("  Read small record: '%.*s'\n", (int)record.size, record.data);

  ASSERT_TRUE(LogReader_ReadRecord(reader, &record, &scratch));

  ASSERT_EQ(record.size, large_size);

  bool pattern_ok = true;
  for (size_t i = 0; i < large_size; i++) {
    if (record.data[i] != (char)('A' + (i % 26))) {
      pattern_ok = false;
      break;
    }
  }

  ASSERT_TRUE(pattern_ok);
  printf("  Read large record: %zu bytes " COLOR_GREEN
         "(reassembled, verified)\n" COLOR_RESET,
         record.size);

  ASSERT_TRUE(LogReader_ReadRecord(reader, &record, &scratch));

  ASSERT_EQ(record.size, strlen(small_data2));
  ASSERT_TRUE(memcmp(record.data, small_data2, record.size) == 0);
  printf("  Read second small record: '%.*s'\n", (int)record.size, record.data);

  ASSERT_TRUE(!LogReader_ReadRecord(reader, &record, &scratch));
  printf(COLOR_GREEN "  EOF reached as expected\n" COLOR_RESET);

  if (scratch != NULL) {
    free(scratch);
  }
  free(large_data);
  LogReader_Destroy(reader);
  SequentialFile_Close(rfile);

  s = Env_DeleteFile(test_filename);
  ASSERT_OK(s);
}

void Run_WALTests(void) {
  Test_DurabilityUtils();
  Test_LogFragmentation();
}