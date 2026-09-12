/*
 * tinytest — a test harness in one header.
 *
 * Why not Unity, Catch2 or CppUTest? Because the host suite for this library
 * has one job: run on any machine with a C compiler, with no network, no
 * submodule and no package manager. Sixty lines of macros buy that, and the
 * ESP-IDF side still uses Unity for on-target tests, where it is already part
 * of the toolchain.
 *
 * Usage:
 *
 *   TT_TEST(free_heap_is_reported) {
 *       TT_ASSERT_STR_EQ("device_heap_free_bytes", name);
 *   }
 *
 *   int main(void) {
 *       TT_RUN(free_heap_is_reported);
 *       return TT_SUMMARY();
 *   }
 *
 * Copyright (c) 2026 Opsvex SpA. SPDX-License-Identifier: Apache-2.0
 */

#ifndef TINYTEST_H
#define TINYTEST_H

#include <math.h>
#include <stdio.h>
#include <string.h>

static int tt_failures = 0;
static int tt_checks = 0;
static int tt_tests = 0;
static int tt_before = 0;
static const char *tt_current = "";

#define TT_TEST(name) static void name(void)

#define TT_RUN(fn)                                \
    do {                                          \
        tt_current = #fn;                         \
        tt_before = tt_failures;                  \
        tt_tests++;                               \
        fn();                                     \
        if (tt_failures == tt_before) {           \
            printf("  ok   %s\n", tt_current);    \
        }                                         \
    } while (0)

#define TT_FAILF(...)                                                             \
    do {                                                                          \
        tt_failures++;                                                            \
        printf("  FAIL %s (%s:%d)\n       ", tt_current, __FILE__, __LINE__);      \
        printf(__VA_ARGS__);                                                      \
        printf("\n");                                                             \
    } while (0)

#define TT_ASSERT(cond)                      \
    do {                                     \
        tt_checks++;                         \
        if (!(cond)) {                       \
            TT_FAILF("expected: %s", #cond); \
        }                                    \
    } while (0)

#define TT_ASSERT_STR_EQ(expected, actual)                                              \
    do {                                                                                \
        tt_checks++;                                                                    \
        const char *tt_e = (expected);                                                  \
        const char *tt_a = (actual);                                                    \
        if (tt_e == NULL || tt_a == NULL || strcmp(tt_e, tt_a) != 0) {                  \
            TT_FAILF("expected \"%s\"\n       actual   \"%s\"", tt_e ? tt_e : "(null)", \
                     tt_a ? tt_a : "(null)");                                           \
        }                                                                               \
    } while (0)

#define TT_ASSERT_CONTAINS(haystack, needle)                                                  \
    do {                                                                                      \
        tt_checks++;                                                                          \
        const char *tt_h = (haystack);                                                        \
        const char *tt_n = (needle);                                                          \
        if (tt_h == NULL || tt_n == NULL || strstr(tt_h, tt_n) == NULL) {                     \
            TT_FAILF("expected to contain \"%s\"\n       in: %s", tt_n ? tt_n : "(null)",     \
                     tt_h ? tt_h : "(null)");                                                 \
        }                                                                                     \
    } while (0)

#define TT_ASSERT_NOT_CONTAINS(haystack, needle)                                    \
    do {                                                                            \
        tt_checks++;                                                                \
        const char *tt_h = (haystack);                                              \
        const char *tt_n = (needle);                                                \
        if (tt_h != NULL && tt_n != NULL && strstr(tt_h, tt_n) != NULL) {            \
            TT_FAILF("expected NOT to contain \"%s\"\n       in: %s", tt_n, tt_h);   \
        }                                                                           \
    } while (0)

#define TT_ASSERT_EQ_INT(expected, actual)                      \
    do {                                                        \
        tt_checks++;                                            \
        long long tt_e = (long long)(expected);                 \
        long long tt_a = (long long)(actual);                   \
        if (tt_e != tt_a) {                                     \
            TT_FAILF("expected %lld, actual %lld", tt_e, tt_a); \
        }                                                       \
    } while (0)

#define TT_ASSERT_NEAR(expected, actual, tolerance)                                     \
    do {                                                                                \
        tt_checks++;                                                                    \
        double tt_e = (double)(expected);                                               \
        double tt_a = (double)(actual);                                                 \
        if (fabs(tt_e - tt_a) > (double)(tolerance)) {                                  \
            TT_FAILF("expected %g +/- %g, actual %g", tt_e, (double)(tolerance), tt_a); \
        }                                                                               \
    } while (0)

#define TT_SUMMARY()                                                              \
    (printf("\n%s: %d test(s), %d check(s), %d failure(s)\n",                      \
            tt_failures == 0 ? "PASS" : "FAIL", tt_tests, tt_checks, tt_failures), \
     tt_failures == 0 ? 0 : 1)

#endif /* TINYTEST_H */
