#define _GNU_SOURCE
#include "lithos/db.h"
#include "lithos/write_batch.h"
#include "testharness.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>

void Run_DBTests(void);

static Lithos_DB* OpenTempDB(char* path_buf, size_t len) {
    snprintf(path_buf, len, "/tmp/lithos-dbtest-XXXXXX");
    char* dir = mkdtemp(path_buf);
    ASSERT_TRUE(dir != NULL);

    Lithos_DB* db = NULL;
    Status s = Lithos_DB_Open(dir, NULL, &db);
    ASSERT_OK(s);
    return db;
}

static void TestPutGet(void) {
    char path[64];
    Lithos_DB* db = OpenTempDB(path, sizeof(path));

    Status s = Lithos_DB_Put(db, Slice_FromCString("k1"), Slice_FromCString("v1"));
    ASSERT_OK(s);

    char* out = NULL;
    s = Lithos_DB_Get(db, Slice_FromCString("k1"), &out);
    ASSERT_OK(s);
    ASSERT_TRUE(out != NULL && strcmp(out, "v1") == 0);
    free(out);

    Lithos_DB_Close(db);
}

static void TestWriteBatch(void) {
    char path[64];
    Lithos_DB* db = OpenTempDB(path, sizeof(path));

    Lithos_WriteBatch* batch = WriteBatch_Create();
    ASSERT_TRUE(batch != NULL);
    ASSERT_OK(WriteBatch_Put(batch, Slice_FromCString("a"), Slice_FromCString("1")));
    ASSERT_OK(WriteBatch_Put(batch, Slice_FromCString("b"), Slice_FromCString("2")));
    ASSERT_OK(WriteBatch_Delete(batch, Slice_FromCString("a")));

    Lithos_WriteOptions wopt = Lithos_WriteOptions_Default();
    wopt.sync = true;
    Status s = Lithos_DB_Write(db, wopt, batch);
    ASSERT_OK(s);

    char* out = NULL;
    s = Lithos_DB_Get(db, Slice_FromCString("a"), &out);
    ASSERT_TRUE(Status_IsNotFound(s));
    Status_Free(s);

    s = Lithos_DB_Get(db, Slice_FromCString("b"), &out);
    ASSERT_OK(s);
    ASSERT_TRUE(out != NULL && strcmp(out, "2") == 0);
    free(out);

    WriteBatch_Destroy(batch);
    Lithos_DB_Close(db);
}

static void TestRecovery(void) {
    char path[64];
    Lithos_DB* db = OpenTempDB(path, sizeof(path));

    ASSERT_OK(Lithos_DB_Put(db, Slice_FromCString("foo"), Slice_FromCString("bar")));
    Lithos_DB_Close(db);

    Status s = Lithos_DB_Open(path, NULL, &db);
    ASSERT_OK(s);

    char* out = NULL;
    s = Lithos_DB_Get(db, Slice_FromCString("foo"), &out);
    ASSERT_OK(s);
    ASSERT_TRUE(out != NULL && strcmp(out, "bar") == 0);
    free(out);

    Lithos_DB_Close(db);
}

void Run_DBTests(void) {
    printf("Running DB tests...\n");
    TestPutGet();
    TestWriteBatch();
    TestRecovery();
}
