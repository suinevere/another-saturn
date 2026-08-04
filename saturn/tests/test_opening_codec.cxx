/*----------------------
 | test_opening_codec.cxx
 | Description: Host unit tests for the opening's fused RLE-decode-and-XOR. The
 |   arithmetic is pure and runs off-target rather than being eyeballed on
 |   hardware.
 | Author: suinevere
 | Dependencies: opening_codec.h, page_rle.h
 ----------------------*/
#include <cstdio>
#include <cstdint>
#include <cstring>
#include "opening_codec.h"
#include "page_rle.h"

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

enum { PAGE = 32000 };

static uint8_t g_page[PAGE];
static uint8_t g_ref[PAGE];
static uint8_t g_enc[PAGE * 2];

static void test_zero_delta_leaves_page_untouched(void)
{
    for (int i = 0; i < PAGE; ++i) {
        g_page[i] = (uint8_t)(i * 7);
    }
    memcpy(g_ref, g_page, PAGE);

    static uint8_t zeros[PAGE];
    memset(zeros, 0, sizeof(zeros));
    const int32_t n = pageRleEncode(zeros, PAGE, g_enc, (int32_t)sizeof(g_enc));
    CHECK_EQ(n > 0, 1);

    CHECK_EQ(openingApplyDelta(g_page, g_enc, n, PAGE), 1);
    CHECK_EQ(memcmp(g_page, g_ref, PAGE), 0);
}

static void test_keyframe_onto_cleared_page(void)
{
    for (int i = 0; i < PAGE; ++i) {
        g_ref[i] = (uint8_t)((i * 31) ^ (i >> 5));
    }
    const int32_t n = pageRleEncode(g_ref, PAGE, g_enc, (int32_t)sizeof(g_enc));
    CHECK_EQ(n > 0, 1);

    memset(g_page, 0, PAGE);
    CHECK_EQ(openingApplyDelta(g_page, g_enc, n, PAGE), 1);
    CHECK_EQ(memcmp(g_page, g_ref, PAGE), 0);
}

static void test_delta_moves_one_frame_to_the_next(void)
{
    static uint8_t a[PAGE];
    static uint8_t b[PAGE];
    static uint8_t d[PAGE];

    for (int i = 0; i < PAGE; ++i) {
        a[i] = (uint8_t)(i & 0xFF);
        b[i] = (uint8_t)((i < 4000) ? (i & 0xFF) : ((i * 3) & 0xFF));
        d[i] = (uint8_t)(a[i] ^ b[i]);
    }

    const int32_t n = pageRleEncode(d, PAGE, g_enc, (int32_t)sizeof(g_enc));
    CHECK_EQ(n > 0, 1);

    memcpy(g_page, a, PAGE);
    CHECK_EQ(openingApplyDelta(g_page, g_enc, n, PAGE), 1);
    CHECK_EQ(memcmp(g_page, b, PAGE), 0);
}

static void test_short_literal_and_run_by_hand(void)
{
    memset(g_page, 0, PAGE);
    g_page[0] = 0x0F;
    g_page[1] = 0xF0;

    const uint8_t enc[] = {
        0x01, 0xFF, 0x00,
        0x82, 0xAA
    };
    CHECK_EQ(openingApplyDelta(g_page, enc, (int32_t)sizeof(enc), 5), 1);

    CHECK_EQ(g_page[0], 0xF0);
    CHECK_EQ(g_page[1], 0xF0);
    CHECK_EQ(g_page[2], 0xAA);
    CHECK_EQ(g_page[3], 0xAA);
    CHECK_EQ(g_page[4], 0xAA);
}

static void test_rejects_stream_that_overruns_the_page(void)
{
    uint8_t page[4];
    memset(page, 0, 4);
    page[3] = 0xCC;
    const uint8_t enc[] = { 0x83, 0x11 };
    CHECK_EQ(openingApplyDelta(page, enc, (int32_t)sizeof(enc), 3), 0);
    CHECK_EQ(page[3], 0xCC);
}

static void test_rejects_stream_that_underruns_the_page(void)
{
    uint8_t page[9];
    memset(page, 0, 9);
    const uint8_t enc[] = { 0x81, 0x11 };
    CHECK_EQ(openingApplyDelta(page, enc, (int32_t)sizeof(enc), 9), 0);
}

static void test_rejects_truncated_stream(void)
{
    memset(g_page, 0, PAGE);
    const uint8_t runNoValue[] = { 0x80 };
    CHECK_EQ(openingApplyDelta(g_page, runNoValue, 1, 1), 0);

    const uint8_t litShort[] = { 0x03, 0x11, 0x22 };
    CHECK_EQ(openingApplyDelta(g_page, litShort, 3, 4), 0);
}

int main(void)
{
    test_zero_delta_leaves_page_untouched();
    test_keyframe_onto_cleared_page();
    test_delta_moves_one_frame_to_the_next();
    test_short_literal_and_run_by_hand();
    test_rejects_stream_that_overruns_the_page();
    test_rejects_stream_that_underruns_the_page();
    test_rejects_truncated_stream();

    if (g_fail != 0) {
        printf("%d check(s) failed\n", g_fail);
        return 1;
    }
    printf("opening codec: all checks passed\n");
    return 0;
}
