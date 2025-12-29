
#include "core/version_edit.h"
#include "core/version_set.h"
#include "testharness.h"
#include "util/slice.h"
#include <stdio.h>
#include <string.h>

void Run_VersionSetTests(void);

static void Test_EditSerialization(void) {
  printf("[TEST] VersionEdit Serialization              ");
  VersionEdit edit;
  VersionEdit_Init(&edit);
  VersionEdit_SetLogNumber(&edit, 7);
  VersionEdit_SetPrevLogNumber(&edit, 6);
  VersionEdit_SetNextFileNumber(&edit, 9);
  VersionEdit_AddFile(&edit, 0, 10, 123, Slice_FromCString("a"),
                      Slice_FromCString("z"), 100);
  VersionEdit_DeleteFile(&edit, 1, 5);

  Lithos_Slice encoded;
  Status s = VersionEdit_EncodeTo(&edit, &encoded);
  ASSERT_OK(s);

  VersionEdit decoded;
  VersionEdit_Init(&decoded);
  s = VersionEdit_DecodeFrom(&decoded, encoded);
  ASSERT_OK(s);

  ASSERT_TRUE(decoded.has_log_number);
  ASSERT_TRUE(decoded.log_number == 7);
  ASSERT_TRUE(decoded.has_prev_log_number);
  ASSERT_TRUE(decoded.prev_log_number == 6);
  ASSERT_TRUE(decoded.has_next_file_number);
  ASSERT_TRUE(decoded.next_file_number == 9);
  ASSERT_TRUE(decoded.new_files_count == 1);
  ASSERT_TRUE(decoded.new_files[0].level == 0);
  ASSERT_TRUE(decoded.new_files[0].file->number == 10);
  ASSERT_TRUE(decoded.new_files[0].file->file_size == 123);
  ASSERT_TRUE(decoded.deleted_files_count == 1);
  ASSERT_TRUE(decoded.deleted_files[0].level == 1);
  ASSERT_TRUE(decoded.deleted_files[0].number == 5);

  free((void *)encoded.data);
  VersionEdit_Clear(&edit);
  VersionEdit_Clear(&decoded);
  printf("\n");
}

static void Test_VersionSetApply(void) {
  printf("[TEST] VersionSet Apply                       ");
  VersionEdit edit1;
  VersionEdit_Init(&edit1);
  VersionEdit_AddFile(&edit1, 0, 10, 100, Slice_FromCString("a"),
                      Slice_FromCString("b"), 100);

  Lithos_VersionSet *vs = VersionSet_Create("/tmp/lithos_vs");
  ASSERT_TRUE(vs != NULL);

  Status s = VersionSet_LogAndApply(vs, &edit1);
  ASSERT_OK(s);
  ASSERT_TRUE(vs->current->file_counts[0] == 1);
  ASSERT_TRUE(vs->current->files[0][0]->number == 10);

  VersionEdit edit2;
  VersionEdit_Init(&edit2);
  VersionEdit_DeleteFile(&edit2, 0, 10);
  VersionEdit_AddFile(&edit2, 0, 12, 120, Slice_FromCString("c"),
                      Slice_FromCString("d"), 120);
  s = VersionSet_LogAndApply(vs, &edit2);
  ASSERT_OK(s);
  ASSERT_TRUE(vs->current->file_counts[0] == 1);
  ASSERT_TRUE(vs->current->files[0][0]->number == 12);

  VersionSet_Destroy(vs);
  VersionEdit_Clear(&edit1);
  VersionEdit_Clear(&edit2);
  printf("\n");
}

static void Test_VersionRefCounting(void) {
  printf("[TEST] Version Ref Counting                   ");
  Lithos_VersionSet *vs = VersionSet_Create("/tmp/lithos_vs_ref");
  ASSERT_TRUE(vs != NULL);

  VersionEdit e1;
  VersionEdit_Init(&e1);
  VersionEdit_AddFile(&e1, 0, 1, 10, Slice_FromCString("a"),
                      Slice_FromCString("b"), 10);
  ASSERT_OK(VersionSet_LogAndApply(vs, &e1));

  Lithos_Version *v1 = vs->current;
  Version_Ref(v1);

  VersionEdit e2;
  VersionEdit_Init(&e2);
  VersionEdit_AddFile(&e2, 0, 2, 20, Slice_FromCString("c"),
                      Slice_FromCString("d"), 20);
  ASSERT_OK(VersionSet_LogAndApply(vs, &e2));

  ASSERT_TRUE(v1->refs == 1);
  Version_Unref(v1);

  ASSERT_TRUE(vs->dummy_versions->next != v1);

  VersionSet_Destroy(vs);
  VersionEdit_Clear(&e1);
  VersionEdit_Clear(&e2);
  printf("\n");
}

void Run_VersionSetTests(void) {
  Test_EditSerialization();
  Test_VersionSetApply();
  Test_VersionRefCounting();
}
