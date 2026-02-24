/*
 * test_framework.h - 简单的 C 测试框架
 */

#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 测试统计 */
static int test_passed = 0;
static int test_failed = 0;
static const char *current_test = NULL;

/* 颜色输出 */
#define COLOR_GREEN "\033[32m"
#define COLOR_RED   "\033[31m"
#define COLOR_RESET "\033[0m"

/* 测试宏 */
#define TEST(name) \
    static void test_##name(void); \
    static void register_##name(void) { \
        (void)0; \
    } \
    static void test_##name(void)

#define RUN_TEST(name) \
    do { \
        current_test = #name; \
        printf("  Running: %s ... ", #name); \
        fflush(stdout); \
        test_##name(); \
        printf(COLOR_GREEN "PASS" COLOR_RESET "\n"); \
        test_passed++; \
    } while(0)

#define ASSERT(condition) \
    do { \
        if (!(condition)) { \
            printf(COLOR_RED "FAIL" COLOR_RESET "\n"); \
            printf("    Assertion failed: %s\n", #condition); \
            printf("    at %s:%d\n", __FILE__, __LINE__); \
            test_failed++; \
            return; \
        } \
    } while(0)

#define ASSERT_EQ(a, b) \
    do { \
        if ((a) != (b)) { \
            printf(COLOR_RED "FAIL" COLOR_RESET "\n"); \
            printf("    Expected %s == %s\n", #a, #b); \
            printf("    Got %ld != %ld\n", (long)(a), (long)(b)); \
            printf("    at %s:%d\n", __FILE__, __LINE__); \
            test_failed++; \
            return; \
        } \
    } while(0)

#define ASSERT_NEQ(a, b) \
    do { \
        if ((a) == (b)) { \
            printf(COLOR_RED "FAIL" COLOR_RESET "\n"); \
            printf("    Expected %s != %s\n", #a, #b); \
            printf("    Got %ld == %ld\n", (long)(a), (long)(b)); \
            printf("    at %s:%d\n", __FILE__, __LINE__); \
            test_failed++; \
            return; \
        } \
    } while(0)

#define ASSERT_GT(a, b) \
    do { \
        if ((a) <= (b)) { \
            printf(COLOR_RED "FAIL" COLOR_RESET "\n"); \
            printf("    Expected %s > %s\n", #a, #b); \
            printf("    Got %ld <= %ld\n", (long)(a), (long)(b)); \
            printf("    at %s:%d\n", __FILE__, __LINE__); \
            test_failed++; \
            return; \
        } \
    } while(0)

#define ASSERT_GE(a, b) \
    do { \
        if ((a) < (b)) { \
            printf(COLOR_RED "FAIL" COLOR_RESET "\n"); \
            printf("    Expected %s >= %s\n", #a, #b); \
            printf("    Got %ld < %ld\n", (long)(a), (long)(b)); \
            printf("    at %s:%d\n", __FILE__, __LINE__); \
            test_failed++; \
            return; \
        } \
    } while(0)

#define ASSERT_LT(a, b) \
    do { \
        if ((a) >= (b)) { \
            printf(COLOR_RED "FAIL" COLOR_RESET "\n"); \
            printf("    Expected %s < %s\n", #a, #b); \
            printf("    Got %ld >= %ld\n", (long)(a), (long)(b)); \
            printf("    at %s:%d\n", __FILE__, __LINE__); \
            test_failed++; \
            return; \
        } \
    } while(0)

#define ASSERT_LE(a, b) \
    do { \
        if ((a) > (b)) { \
            printf(COLOR_RED "FAIL" COLOR_RESET "\n"); \
            printf("    Expected %s <= %s\n", #a, #b); \
            printf("    Got %ld > %ld\n", (long)(a), (long)(b)); \
            printf("    at %s:%d\n", __FILE__, __LINE__); \
            test_failed++; \
            return; \
        } \
    } while(0)

#define ASSERT_NULL(ptr) ASSERT((ptr) == NULL)
#define ASSERT_NOT_NULL(ptr) ASSERT((ptr) != NULL)

#define TEST_SUMMARY() \
    do { \
        printf("\n========================================\n"); \
        if (test_failed == 0) { \
            printf(COLOR_GREEN "All tests passed! (%d/%d)" COLOR_RESET "\n", \
                   test_passed, test_passed + test_failed); \
        } else { \
            printf(COLOR_RED "Some tests failed: %d/%d" COLOR_RESET "\n", \
                   test_failed, test_passed + test_failed); \
        } \
        printf("========================================\n"); \
    } while(0)

#endif /* TEST_FRAMEWORK_H */
