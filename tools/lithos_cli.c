#include "lithos.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

static void Usage(const char* prog) {
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  %s <db_path> put <key> <value>\n", prog);
    fprintf(stderr, "  %s <db_path> get <key>\n", prog);
    fprintf(stderr, "  %s <db_path> del <key>\n", prog);
    fprintf(stderr, "  %s <db_path> scan\n", prog);
    fprintf(stderr, "  %s <db_path> fill <count> <value_size>\n", prog);
    fprintf(stderr, "  %s <db_path> bench <count> <value_size>\n", prog);
}

static void PrintStatus(const char* action, Status s) {
    if (!Status_IsOK(s)) {
        fprintf(stderr, "%s: %s\n", action, Status_ToString(s));
        Status_Free(s);
    }
}

static void ScanPrinter(const char* key, const char* value, void* arg) {
    (void)arg;
    printf("%s\t%s\n", key, value);
}

int main(int argc, char** argv) {
    if (argc < 3) {
        Usage(argv[0]);
        return 1;
    }

    const char* dbpath = argv[1];
    const char* cmd = argv[2];

    Lithos_Options opt;
    Lithos_Options_InitDefault(&opt);

    Lithos_DB* db = NULL;
    Status s = Lithos_Open(dbpath, &opt, &db);
    if (!Status_IsOK(s)) {
        fprintf(stderr, "open: %s\n", Status_ToString(s));
        Status_Free(s);
        return 1;
    }

    int rc = 0;

    if (strcmp(cmd, "put") == 0) {
        if (argc != 5) {
            Usage(argv[0]);
            rc = 1;
            goto done;
        }
        s = Lithos_Put(db, argv[3], argv[4]);
        if (!Status_IsOK(s)) {
            PrintStatus("put", s);
            rc = 1;
        }
    } else if (strcmp(cmd, "get") == 0) {
        if (argc != 4) {
            Usage(argv[0]);
            rc = 1;
            goto done;
        }
        char* value = NULL;
        s = Lithos_Get(db, argv[3], NULL, &value);
        if (Status_IsOK(s)) {
            printf("%s\n", value ? value : "");
            Lithos_Free(value);
        } else if (Status_IsNotFound(s)) {
            printf("NOT_FOUND\n");
            Status_Free(s);
        } else {
            PrintStatus("get", s);
            rc = 1;
        }
    } else if (strcmp(cmd, "del") == 0) {
        if (argc != 4) {
            Usage(argv[0]);
            rc = 1;
            goto done;
        }
        s = Lithos_Delete(db, argv[3]);
        if (!Status_IsOK(s)) {
            PrintStatus("delete", s);
            rc = 1;
        }
    } else if (strcmp(cmd, "scan") == 0) {
        if (argc != 3) {
            Usage(argv[0]);
            rc = 1;
            goto done;
        }
        s = Lithos_Scan(db, ScanPrinter, NULL);
        if (!Status_IsOK(s)) {
            PrintStatus("scan", s);
            rc = 1;
        }
    } else if (strcmp(cmd, "fill") == 0) {
        if (argc != 5) {
            Usage(argv[0]);
            rc = 1;
            goto done;
        }

        char* end = NULL;
        long long count = strtoll(argv[3], &end, 10);
        if (end == argv[3] || count <= 0) {
            fprintf(stderr, "fill: invalid count\n");
            rc = 1;
            goto done;
        }
        end = NULL;
        long long value_size = strtoll(argv[4], &end, 10);
        if (end == argv[4] || value_size <= 0) {
            fprintf(stderr, "fill: invalid value_size\n");
            rc = 1;
            goto done;
        }

        const char alphabet[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
        const size_t alpha_len = sizeof(alphabet) - 1;

        char* valbuf = malloc((size_t)value_size + 1);
        if (valbuf == NULL) {
            fprintf(stderr, "fill: alloc buffer failed\n");
            rc = 1;
            goto done;
        }

        srand((unsigned)time(NULL));

        for (long long i = 0; i < count; i++) {
            char keybuf[32];
            int n = snprintf(keybuf, sizeof(keybuf), "fill_%010lld", i);
            if (n < 0 || (size_t)n >= sizeof(keybuf)) {
                fprintf(stderr, "fill: key formatting failed at %lld\n", i);
                rc = 1;
                break;
            }

            for (long long v = 0; v < value_size; v++) {
                valbuf[v] = alphabet[rand() % alpha_len];
            }
            valbuf[value_size] = '\0';

            s = Lithos_Put(db, keybuf, valbuf);
            if (!Status_IsOK(s)) {
                PrintStatus("fill", s);
                rc = 1;
                break;
            }
        }

        free(valbuf);
        if (rc == 0) {
            printf("filled %lld keys of size %lld\n", count, value_size);
        }
    } else if (strcmp(cmd, "bench") == 0) {
        if (argc != 5) {
            Usage(argv[0]);
            rc = 1;
            goto done;
        }

        char* end = NULL;
        long long count = strtoll(argv[3], &end, 10);
        if (end == argv[3] || count <= 0) {
            fprintf(stderr, "bench: invalid count\n");
            rc = 1;
            goto done;
        }
        end = NULL;
        long long value_size = strtoll(argv[4], &end, 10);
        if (end == argv[4] || value_size <= 0) {
            fprintf(stderr, "bench: invalid value_size\n");
            rc = 1;
            goto done;
        }

        const char alphabet[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
        const size_t alpha_len = sizeof(alphabet) - 1;

        char* valbuf = malloc((size_t)value_size + 1);
        char* getbuf = NULL;
        if (valbuf == NULL) {
            fprintf(stderr, "bench: alloc buffer failed\n");
            rc = 1;
            goto done;
        }

        srand((unsigned)time(NULL));

        struct timeval t0, t1;
        gettimeofday(&t0, NULL);
        for (long long i = 0; i < count; i++) {
            char keybuf[32];
            int n = snprintf(keybuf, sizeof(keybuf), "key_%010lld", i);
            if (n < 0 || (size_t)n >= sizeof(keybuf)) {
                fprintf(stderr, "bench: key formatting failed at %lld\n", i);
                rc = 1;
                break;
            }

            for (long long v = 0; v < value_size; v++) {
                valbuf[v] = alphabet[rand() % alpha_len];
            }
            valbuf[value_size] = '\0';

            s = Lithos_Put(db, keybuf, valbuf);
            if (!Status_IsOK(s)) {
                PrintStatus("bench-put", s);
                rc = 1;
                break;
            }
        }
        gettimeofday(&t1, NULL);
        double write_sec = (t1.tv_sec - t0.tv_sec) + (t1.tv_usec - t0.tv_usec) / 1e6;
        if (rc == 0) {
            double write_ops = count / (write_sec > 0 ? write_sec : 1e-9);
            double write_mb = (count * value_size) / (1024.0 * 1024.0);
            double write_mb_sec = write_mb / (write_sec > 0 ? write_sec : 1e-9);
            printf("Write: %lld entries in %.3fs (%.0f ops/sec, %.2f MB/sec)\n",
                   count, write_sec, write_ops, write_mb_sec);
        }

        if (rc == 0) {
            gettimeofday(&t0, NULL);
            for (long long i = 0; i < count; i++) {
                long long idx = rand() % count;
                char keybuf[32];
                int n = snprintf(keybuf, sizeof(keybuf), "key_%010lld", idx);
                if (n < 0 || (size_t)n >= sizeof(keybuf)) {
                    fprintf(stderr, "bench: key formatting failed at read %lld\n", i);
                    rc = 1;
                    break;
                }

                s = Lithos_Get(db, keybuf, NULL, &getbuf);
                if (Status_IsOK(s)) {
                    Lithos_Free(getbuf);
                } else if (Status_IsNotFound(s)) {
                    Status_Free(s);
                } else {
                    PrintStatus("bench-get", s);
                    rc = 1;
                    break;
                }
            }
            gettimeofday(&t1, NULL);
            double read_sec = (t1.tv_sec - t0.tv_sec) + (t1.tv_usec - t0.tv_usec) / 1e6;
            double read_ops = count / (read_sec > 0 ? read_sec : 1e-9);
            double read_mb = (count * value_size) / (1024.0 * 1024.0);
            double read_mb_sec = read_mb / (read_sec > 0 ? read_sec : 1e-9);
            if (rc == 0) {
                printf("Read: %lld entries in %.3fs (%.0f ops/sec, %.2f MB/sec)\n",
                       count, read_sec, read_ops, read_mb_sec);
            }
        }

        free(valbuf);
    } else {
        Usage(argv[0]);
        rc = 1;
    }

done:
    Lithos_Close(db);
    return rc;
}
