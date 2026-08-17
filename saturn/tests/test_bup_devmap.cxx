/*----------------------
 | test_bup_devmap.cxx
 | Description: Host unit tests for bup_devmap.cxx: naming the responding BIOS
 |   backup device indices. The first two cases are the two states measured on
 |   real hardware.
 | Author: suinevere
 | Dependencies: bup_devmap.h
 ----------------------*/
#include <cstdio>
#include "bup_devmap.h"

static int g_fail = 0;

#define CHECK_EQ(actual, expected)                                            \
    do {                                                                      \
        long long a_ = (long long)(actual);                                   \
        long long e_ = (long long)(expected);                                 \
        if (a_ != e_) {                                                       \
            g_fail++;                                                         \
            printf("FAIL %s:%d  %s\n  actual   = %lld\n  expected = %lld\n",  \
                   __FILE__, __LINE__, #actual, a_, e_);                      \
        }                                                                     \
    } while (0)

/* Measured with a cartridge fitted: BUP_Stat over indices 0..2 answered
   OK / OK / BUP_NON. */
static void test_cartridge_fitted(void)
{
    const int present[3] = { 1, 1, 0 };
    int internalIdx = -99, cartIdx = -99;

    bupDevmapResolve(present, 3, &internalIdx, &cartIdx);

    CHECK_EQ(internalIdx, 0);
    CHECK_EQ(cartIdx, 1);
}

/* Measured with no cartridge: OK / BUP_NON / BUP_NON. This is the case that
   used to fail outright -- the port addressed index 1 as its internal device,
   so with no cart there was no working save device at all. */
static void test_no_cartridge(void)
{
    const int present[3] = { 1, 0, 0 };
    int internalIdx = -99, cartIdx = -99;

    bupDevmapResolve(present, 3, &internalIdx, &cartIdx);

    CHECK_EQ(internalIdx, 0);
    CHECK_EQ(cartIdx, BUP_DEVMAP_NONE);
}

static void test_nothing_answers_still_names_an_internal(void)
{
    const int present[3] = { 0, 0, 0 };
    int internalIdx = -99, cartIdx = -99;

    bupDevmapResolve(present, 3, &internalIdx, &cartIdx);

    CHECK_EQ(internalIdx, 0);
    CHECK_EQ(cartIdx, BUP_DEVMAP_NONE);
}

/* The search does not assume the main unit is index 0 -- it takes the first
   index that answers, whichever that is. */
static void test_does_not_assume_index_zero(void)
{
    const int present[3] = { 0, 1, 1 };
    int internalIdx = -99, cartIdx = -99;

    bupDevmapResolve(present, 3, &internalIdx, &cartIdx);

    CHECK_EQ(internalIdx, 1);
    CHECK_EQ(cartIdx, 2);
}

static void test_skips_a_gap_between_responders(void)
{
    const int present[3] = { 1, 0, 1 };
    int internalIdx = -99, cartIdx = -99;

    bupDevmapResolve(present, 3, &internalIdx, &cartIdx);

    CHECK_EQ(internalIdx, 0);
    CHECK_EQ(cartIdx, 2);
}

static void test_a_third_responder_is_ignored(void)
{
    const int present[4] = { 1, 1, 1, 1 };
    int internalIdx = -99, cartIdx = -99;

    bupDevmapResolve(present, 4, &internalIdx, &cartIdx);

    CHECK_EQ(internalIdx, 0);
    CHECK_EQ(cartIdx, 1);
}

static void test_tolerates_a_short_table(void)
{
    const int present[1] = { 1 };
    int internalIdx = -99, cartIdx = -99;

    bupDevmapResolve(present, 1, &internalIdx, &cartIdx);

    CHECK_EQ(internalIdx, 0);
    CHECK_EQ(cartIdx, BUP_DEVMAP_NONE);
}

int main(void)
{
    test_cartridge_fitted();
    test_no_cartridge();
    test_nothing_answers_still_names_an_internal();
    test_does_not_assume_index_zero();
    test_skips_a_gap_between_responders();
    test_a_third_responder_is_ignored();
    test_tolerates_a_short_table();

    if (g_fail == 0) {
        printf("all tests passed\n");
        return 0;
    }
    printf("%d check(s) failed\n", g_fail);
    return 1;
}
