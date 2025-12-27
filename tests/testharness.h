/**
 * Lithos Test Harness
 *
 * Lightweight unit testing framework for Lithos.
 * Provides assertion macros with colorized output and test summary.
 */

#ifndef LITHOS_TEST_HARNESS_H
#define LITHOS_TEST_HARNESS_H

#include "util/status.h"
#include <stdio.h>
#include <stdlib.h>

/* ANSI color codes */
#define COLOR_RESET "\033[0m"
#define COLOR_GREEN "\033[0;32m"
#define COLOR_RED "\033[0;31m"
#define COLOR_YELLOW "\033[0;33m"
#define COLOR_BLUE "\033[0;34m"

/* Test counters */
extern int test_passed;
extern int test_failed;

/* Assertion macros */
#define ASSERT_TRUE(cond)                                                      \
  do {                                                                         \
    if (cond) {                                                                \
      printf(COLOR_GREEN "  ✓ %s\n" COLOR_RESET, #cond);                       \
      test_passed++;                                                           \
    } else {                                                                   \
      printf(COLOR_RED "  ✗ %s\n" COLOR_RESET, #cond);                         \
      test_failed++;                                                           \
      exit(1); /* Fail fast */                                                 \
    }                                                                          \
  } while (0)

#define ASSERT_EQ(a, b)                                                        \
  do {                                                                         \
    if ((a) == (b)) {                                                          \
      printf(COLOR_GREEN "  ✓ %s == %s\n" COLOR_RESET, #a, #b);                \
      test_passed++;                                                           \
    } else {                                                                   \
      printf(COLOR_RED "  ✗ %s != %s (%lld vs %lld)\n" COLOR_RESET, #a, #b,    \
             (long long)(a), (long long)(b));                                  \
      test_failed++;                                                           \
      exit(1);                                                                 \
    }                                                                          \
  } while (0)

#define ASSERT_OK(status)                                                      \
  do {                                                                         \
    if (Status_IsOK(status)) {                                                 \
      printf(COLOR_GREEN "  ✓ Status OK\n" COLOR_RESET);                       \
      test_passed++;                                                           \
    } else {                                                                   \
      printf(COLOR_RED "  ✗ Status Error: %s\n" COLOR_RESET,                   \
             Status_ToString(status));                                         \
      test_failed++;                                                           \
      exit(1);                                                                 \
    }                                                                          \
  } while (0)

/* Test summary */
void PrintTestSummary(void);

#endif /* LITHOS_TEST_HARNESS_H */