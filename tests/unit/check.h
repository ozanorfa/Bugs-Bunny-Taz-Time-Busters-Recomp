/* Minimal test macros for the hermetic C tests (no framework dependency). */
#ifndef BBT_CHECK_H
#define BBT_CHECK_H

#include <stdio.h>

extern int g_checks;
extern int g_failures;
extern const char* g_current_test;

#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(cond)) {                                                           \
            ++g_failures;                                                        \
            fprintf(stderr, "%s:%d: [%s] CHECK failed: %s\n", __FILE__, __LINE__,  \
                    g_current_test, #cond);                                      \
        }                                                                        \
    } while (0)

#define CHECK_EQ(a, b)                                                           \
    do {                                                                         \
        const long long va_ = (long long)(a), vb_ = (long long)(b);              \
        ++g_checks;                                                              \
        if (va_ != vb_) {                                                        \
            ++g_failures;                                                        \
            fprintf(stderr, "%s:%d: [%s] CHECK_EQ failed: %s == %s (%lld vs %lld)\n", \
                    __FILE__, __LINE__, g_current_test, #a, #b, va_, vb_);       \
        }                                                                        \
    } while (0)

/* Stops the current test function when a precondition fails. */
#define REQUIRE(cond)                                                            \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(cond)) {                                                           \
            ++g_failures;                                                        \
            fprintf(stderr, "%s:%d: [%s] REQUIRE failed: %s\n", __FILE__,       \
                    __LINE__, g_current_test, #cond);                            \
            return;                                                              \
        }                                                                        \
    } while (0)

typedef void (*TestFn)(void);
typedef struct TestCase {
    const char* name;
    TestFn fn;
} TestCase;

/* Each test file exports its table. */
extern const TestCase kBouzeTests[];
extern const int kBouzeTestCount;
extern const TestCase kDonorTests[];
extern const int kDonorTestCount;
extern const TestCase kDonorTexTests[];
extern const int kDonorTexTestCount;
extern const TestCase kCloneResTests[];
extern const int kCloneResTestCount;
extern const TestCase kRetargetTests[];
extern const int kRetargetTestCount;
extern const TestCase kModBlocksTests[];
extern const int kModBlocksTestCount;

#endif /* BBT_CHECK_H */
