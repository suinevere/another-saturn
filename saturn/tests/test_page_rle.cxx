/*----------------------
 | test_page_rle.cxx
 | Description: Host unit tests for the save's page codec. Round-trip fidelity
 |   is the whole contract here: a page that decodes to anything but its
 |   original bytes is a corrupted save, and the failure would only ever show
 |   up on hardware as a garbled frame.
 | Author: suinevere
 | Dependencies: page_rle.h
 ----------------------*/
#include <cstdint>
#include "page_rle.h"

extern "C" int printf(const char *, ...);

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

#define CHECK(cond)                                                           \
    do {                                                                      \
        if (!(cond)) {                                                        \
            g_fail++;                                                         \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);            \
        }                                                                     \
    } while (0)

enum { PAGE_BYTES = 32000 };

static uint8_t g_src[PAGE_BYTES];
static uint8_t g_enc[PAGE_BYTES * 2];
static uint8_t g_dec[PAGE_BYTES];

/*----------------------
 | roundTrip
 | Description: Encodes then decodes a buffer and checks every byte survived.
 | Author: suinevere
 | Params: len -- how many bytes of g_src to use
 | Returns: the encoded length, or -1 if encoding refused
 ----------------------*/
static int32_t roundTrip(int32_t len) {
    const int32_t enc = pageRleEncode(g_src, len, g_enc, (int32_t)sizeof(g_enc));
    if (enc < 0) {
        return -1;
    }
    for (int32_t i = 0; i < len; ++i) {
        g_dec[i] = 0xAA;
    }
    CHECK(pageRleDecode(g_enc, enc, g_dec, len));
    for (int32_t i = 0; i < len; ++i) {
        if (g_dec[i] != g_src[i]) {
            g_fail++;
            printf("FAIL round trip mismatch at %ld\n", (long)i);
            break;
        }
    }
    return enc;
}

static void test_all_one_value_compresses_hard(void) {
    for (int32_t i = 0; i < PAGE_BYTES; ++i) {
        g_src[i] = 0x00;
    }
    const int32_t enc = roundTrip(PAGE_BYTES);
    CHECK(enc > 0);
    CHECK(enc < 700);
}

static void test_flat_bands_round_trip(void) {
    for (int32_t i = 0; i < PAGE_BYTES; ++i) {
        g_src[i] = (uint8_t)((i / 160) & 0x0F);
    }
    const int32_t enc = roundTrip(PAGE_BYTES);
    CHECK(enc > 0);
    CHECK(enc < PAGE_BYTES / 4);
}

static void test_incompressible_still_round_trips(void) {
    uint32_t seed = 12345u;
    for (int32_t i = 0; i < PAGE_BYTES; ++i) {
        seed = seed * 1103515245u + 12345u;
        g_src[i] = (uint8_t)(seed >> 16);
    }
    const int32_t enc = roundTrip(PAGE_BYTES);
    CHECK(enc > 0);
}

static void test_alternating_bytes_round_trip(void) {
    for (int32_t i = 0; i < PAGE_BYTES; ++i) {
        g_src[i] = (uint8_t)((i & 1) ? 0xF0 : 0x0F);
    }
    const int32_t enc = roundTrip(PAGE_BYTES);
    CHECK(enc > 0);
}

static void test_run_longer_than_one_block(void) {
    for (int32_t i = 0; i < 400; ++i) {
        g_src[i] = 0x77;
    }
    const int32_t enc = roundTrip(400);
    CHECK(enc > 0);
}

static void test_encode_refuses_when_it_does_not_fit(void) {
    uint32_t seed = 999u;
    for (int32_t i = 0; i < PAGE_BYTES; ++i) {
        seed = seed * 1103515245u + 12345u;
        g_src[i] = (uint8_t)(seed >> 16);
    }
    uint8_t tiny[64];
    CHECK_EQ(pageRleEncode(g_src, PAGE_BYTES, tiny, (int32_t)sizeof(tiny)), -1);
}

static void test_decode_rejects_a_truncated_stream(void) {
    for (int32_t i = 0; i < 256; ++i) {
        g_src[i] = 0x11;
    }
    const int32_t enc = pageRleEncode(g_src, 256, g_enc, (int32_t)sizeof(g_enc));
    CHECK(enc > 0);
    CHECK(!pageRleDecode(g_enc, enc - 1, g_dec, 256));
}

static void test_decode_rejects_a_stream_that_overruns(void) {
    for (int32_t i = 0; i < 256; ++i) {
        g_src[i] = 0x22;
    }
    const int32_t enc = pageRleEncode(g_src, 256, g_enc, (int32_t)sizeof(g_enc));
    CHECK(enc > 0);
    CHECK(!pageRleDecode(g_enc, enc, g_dec, 128));
}

static void test_decode_rejects_a_short_output(void) {
    for (int32_t i = 0; i < 256; ++i) {
        g_src[i] = 0x33;
    }
    const int32_t enc = pageRleEncode(g_src, 256, g_enc, (int32_t)sizeof(g_enc));
    CHECK(enc > 0);
    CHECK(!pageRleDecode(g_enc, enc, g_dec, 512));
}

static void test_null_arguments_are_refused(void) {
    CHECK_EQ(pageRleEncode(0, 16, g_enc, 16), -1);
    CHECK_EQ(pageRleEncode(g_src, 16, 0, 16), -1);
    CHECK(!pageRleDecode(0, 16, g_dec, 16));
    CHECK(!pageRleDecode(g_enc, 16, 0, 16));
}

int main(void) {
    test_all_one_value_compresses_hard();
    test_flat_bands_round_trip();
    test_incompressible_still_round_trips();
    test_alternating_bytes_round_trip();
    test_run_longer_than_one_block();
    test_encode_refuses_when_it_does_not_fit();
    test_decode_rejects_a_truncated_stream();
    test_decode_rejects_a_stream_that_overruns();
    test_decode_rejects_a_short_output();
    test_null_arguments_are_refused();

    if (g_fail == 0) {
        printf("all tests passed\n");
        return 0;
    }
    printf("%d check(s) failed\n", g_fail);
    return 1;
}
