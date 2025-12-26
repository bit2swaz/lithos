/**
 * Lithos Test Suite Main
 *
 * Entry point for running all Lithos tests.
 */

#include "all_tests.h"
#include "testharness.h"

void PrintTestSummary(void) {
    printf("\n" COLOR_BLUE "========================================\n" COLOR_RESET);
    printf(COLOR_BLUE "Test Summary: %d passed, %d failed\n" COLOR_RESET, test_passed, test_failed);
    if (test_failed == 0) {
        printf(COLOR_GREEN "All tests passed! ✓\n" COLOR_RESET);
    } else {
        printf(COLOR_RED "Some tests failed! ✗\n" COLOR_RESET);
    }
    printf(COLOR_BLUE "========================================\n" COLOR_RESET);
}

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    printf(COLOR_BLUE "Starting Lithos Test Suite...\n" COLOR_RESET);
    printf(COLOR_BLUE "========================================\n" COLOR_RESET);

    printf(COLOR_YELLOW "\n--- Coding Subsystem ---\n" COLOR_RESET);
    Run_CodingTests();

    printf(COLOR_YELLOW "\n--- Arena Subsystem ---\n" COLOR_RESET);
    Run_ArenaTests();

    printf(COLOR_YELLOW "\n--- SkipList Subsystem ---\n" COLOR_RESET);
    Run_SkipListTests();

    printf(COLOR_YELLOW "\n--- MemTable Subsystem ---\n" COLOR_RESET);
    Run_MemTableTests();

    printf(COLOR_YELLOW "\n--- WAL Subsystem ---\n" COLOR_RESET);
    Run_WALTests();

    printf(COLOR_YELLOW "\n--- BlockBuilder Subsystem ---\n" COLOR_RESET);
    Run_BlockBuilderTests();

    printf(COLOR_YELLOW "\n--- TableBuilder Subsystem ---\n" COLOR_RESET);
    Run_TableBuilderTests();

    printf(COLOR_YELLOW "\n--- TableReader Subsystem ---\n" COLOR_RESET);
    Run_TableReaderTests();

    printf(COLOR_YELLOW "\n--- Bloom Filter Subsystem ---\n" COLOR_RESET);
    Run_BloomTests();

    printf(COLOR_YELLOW "\n--- Block Cache Subsystem ---\n" COLOR_RESET);
    Run_CacheTests();

    printf(COLOR_YELLOW "\n--- VersionSet Subsystem ---\n" COLOR_RESET);
    Run_VersionSetTests();

    PrintTestSummary();

    return test_failed == 0 ? 0 : 1;
}