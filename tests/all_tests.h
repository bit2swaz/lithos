/**
 * Lithos Test Suite Declarations
 *
 * Public entry points for all test modules.
 */

#ifndef LITHOS_ALL_TESTS_H
#define LITHOS_ALL_TESTS_H

/* Test module entry points */
void Run_CodingTests(void);
void Run_ArenaTests(void);
void Run_SkipListTests(void);
void Run_MemTableTests(void);
void Run_WALTests(void);
void Run_BlockBuilderTests(void);
void Run_TableBuilderTests(void);
void Run_TableReaderTests(void);
void Run_BloomTests(void);
void Run_CacheTests(void);
void Run_VersionSetTests(void);

#endif /* LITHOS_ALL_TESTS_H */