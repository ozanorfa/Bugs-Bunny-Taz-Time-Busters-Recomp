/*
 * Hermetic unit tests of the third-character mod's host-side logic
 * (src/mods/bouze.c, donor.c, donor_tex.c, clone_res.c) on synthetic data built in the
 * tests. Nothing here reads the disc.
 *
 *   bbt_unit_tests                 run every test
 *   bbt_unit_tests <substring>     run the tests whose name contains it
 *   bbt_unit_tests --list          list the tests
 *   bbt_unit_tests --dump-spin <path> [--dump-streams <dir>]
 *                                  write synthetic data for the Python
 *                                  tests (tests/test_unit_tools.py)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "synth.h"

int g_checks;
int g_failures;
const char* g_current_test = "";

typedef struct Suite {
    const char* name;
    const TestCase* cases;
    const int* count;
} Suite;

static const Suite kSuites[] = {
    {"bouze", kBouzeTests, &kBouzeTestCount},
    {"donor", kDonorTests, &kDonorTestCount},
    {"donor_tex", kDonorTexTests, &kDonorTexTestCount},
    {"clone_res", kCloneResTests, &kCloneResTestCount},
};

static int run(const char* filter)
{
    int ran = 0, failed_tests = 0;
    for (size_t s = 0; s < sizeof(kSuites) / sizeof(kSuites[0]); ++s) {
        for (int i = 0; i < *kSuites[s].count; ++i) {
            char full[128];
            snprintf(full, sizeof full, "%s.%s", kSuites[s].name, kSuites[s].cases[i].name);
            if (filter && !strstr(full, filter))
                continue;
            g_current_test = full;
            const int before = g_failures;
            kSuites[s].cases[i].fn();
            ++ran;
            if (g_failures != before) {
                ++failed_tests;
                printf("FAIL  %s\n", full);
            } else {
                printf("ok    %s\n", full);
            }
        }
    }
    printf("\n%d tests, %d checks, %d failed checks, %d failed tests\n", ran, g_checks,
           g_failures, failed_tests);
    if (ran == 0) {
        fprintf(stderr, "no test matches '%s'\n", filter ? filter : "");
        return 1;
    }
    return failed_tests ? 1 : 0;
}

int main(int argc, char** argv)
{
    if (argc >= 2 && strcmp(argv[1], "--list") == 0) {
        for (size_t s = 0; s < sizeof(kSuites) / sizeof(kSuites[0]); ++s)
            for (int i = 0; i < *kSuites[s].count; ++i)
                printf("%s.%s\n", kSuites[s].name, kSuites[s].cases[i].name);
        return 0;
    }
    if (argc >= 3 && strcmp(argv[1], "--dump-spin") == 0) {
        const char* streams = (argc >= 5 && strcmp(argv[3], "--dump-streams") == 0) ? argv[4] : NULL;
        return synth_dump_for_python(argv[2], streams) ? 0 : 1;
    }
    return run(argc >= 2 ? argv[1] : NULL);
}
