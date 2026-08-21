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

enum {
    PAGE_BYTES = 32000,
    PAGE_STRIDE = 160
};

/*----------------------
 | SAVE_FRAME_BUDGET
 | Description: Roughly what a save has left for the background frame once the
 |   48-byte header and the serialised VM, resource, video, player and mixer
 |   state have taken their share of SAVE_MAX_BYTES. Held here as a plain
 |   number so this suite stays free of engine headers.
 | Author: suinevere
 ----------------------*/
enum { SAVE_FRAME_BUDGET = 6500 };

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

/*----------------------
 | deltaRoundTrip
 | Description: Encodes then decodes a buffer with row differencing and checks
 |   every byte survived.
 | Author: suinevere
 | Params: len -- how many bytes of g_src to use; stride -- bytes per row
 | Returns: the encoded length, or -1 if encoding refused
 ----------------------*/
static int32_t deltaRoundTrip(int32_t len, int32_t stride, int32_t rowStep) {
    const int32_t enc = pageDeltaEncode(g_src, len, stride, rowStep, g_enc,
                                        (int32_t)sizeof(g_enc));
    if (enc < 0) {
        return -1;
    }
    for (int32_t i = 0; i < len; ++i) {
        g_dec[i] = 0xAA;
    }
    CHECK(pageDeltaDecode(g_enc, enc, g_dec, len, stride, rowStep));
    for (int32_t i = 0; i < len; ++i) {
        const uint8_t want = g_src[(i / stride) / rowStep * rowStep * stride +
                                   (i % stride)];
        if (g_dec[i] != want) {
            g_fail++;
            printf("FAIL delta round trip mismatch at %ld\n", (long)i);
            break;
        }
    }
    return enc;
}

/*----------------------
 | buildPolygonArtPage
 | Description: Fills g_src with something shaped like an Another World screen:
 |   flat horizontal spans whose edges drift a little each row, a dithered band
 |   that alternates every other pixel pair, and long identical runs of rows.
 |   Not the real thing, but it has the property that matters -- consecutive
 |   scanlines mostly repeat.
 | Author: suinevere
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void buildPolygonArtPage(void) {
    for (int row = 0; row < 200; ++row) {
        uint8_t *p = g_src + row * PAGE_STRIDE;
        const int edge = 40 + ((row / 7) % 11);
        const int edge2 = 90 + ((row / 13) % 5);

        for (int i = 0; i < PAGE_STRIDE; ++i) {
            uint8_t v;
            if (i < edge) {
                v = 0x11;
            } else if (i < edge2) {
                v = 0x77;
            } else if (row > 150) {
                v = (uint8_t)(((i & 1) != 0) ? 0x35 : 0x53);
            } else {
                v = 0xCC;
            }
            p[i] = v;
        }
    }
}

static void test_delta_round_trips_on_polygon_art(void) {
    buildPolygonArtPage();
    const int32_t enc = deltaRoundTrip(PAGE_BYTES, PAGE_STRIDE, 1);
    CHECK(enc > 0);
}

static void test_delta_beats_plain_rle_on_polygon_art(void) {
    buildPolygonArtPage();

    const int32_t plain =
        pageRleEncode(g_src, PAGE_BYTES, g_enc, (int32_t)sizeof(g_enc));
    const int32_t delta = pageDeltaEncode(g_src, PAGE_BYTES, PAGE_STRIDE, 1,
                                          g_enc, (int32_t)sizeof(g_enc));

    printf("  polygon art: plain rle = %ld bytes, row delta = %ld bytes\n",
           (long)plain, (long)delta);

    CHECK(plain > 0);
    CHECK(delta > 0);
    CHECK(delta < plain);
    CHECK(delta <= SAVE_FRAME_BUDGET);
}

static void test_delta_round_trips_on_incompressible_data(void) {
    uint32_t seed = 0x1234567u;
    for (int32_t i = 0; i < PAGE_BYTES; ++i) {
        seed = seed * 1103515245u + 12345u;
        g_src[i] = (uint8_t)(seed >> 16);
    }
    const int32_t enc = deltaRoundTrip(PAGE_BYTES, PAGE_STRIDE, 1);
    CHECK(enc != 0);
}

static void test_delta_round_trips_when_rows_repeat_exactly(void) {
    for (int row = 0; row < 200; ++row) {
        for (int i = 0; i < PAGE_STRIDE; ++i) {
            g_src[row * PAGE_STRIDE + i] = (uint8_t)(i & 0x3F);
        }
    }
    const int32_t enc = deltaRoundTrip(PAGE_BYTES, PAGE_STRIDE, 1);
    CHECK(enc > 0);
    printf("  identical rows: row delta = %ld bytes\n", (long)enc);
    CHECK(enc < 1200);
}

/*----------------------
 | buildVerticallyCoherentPage
 | Description: Fills g_src with a page that is busy across each row but nearly
 |   unchanged down the screen -- fine vertical detail, the case plain
 |   horizontal run-length coding cannot compress at all and row differencing
 |   handles trivially.
 | Author: suinevere
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void buildVerticallyCoherentPage(void) {
    uint8_t rowPattern[PAGE_STRIDE];
    uint32_t seed = 0x9E3779B9u;
    for (int i = 0; i < PAGE_STRIDE; ++i) {
        seed = seed * 1103515245u + 12345u;
        rowPattern[i] = (uint8_t)(seed >> 16);
    }
    for (int row = 0; row < 200; ++row) {
        uint8_t *p = g_src + row * PAGE_STRIDE;
        for (int i = 0; i < PAGE_STRIDE; ++i) {
            p[i] = rowPattern[i];
        }
    }
}

static void test_delta_rescues_a_page_plain_rle_cannot_fit(void) {
    buildVerticallyCoherentPage();

    const int32_t plain =
        pageRleEncode(g_src, PAGE_BYTES, g_enc, (int32_t)sizeof(g_enc));
    const int32_t delta = pageDeltaEncode(g_src, PAGE_BYTES, PAGE_STRIDE, 1,
                                          g_enc, (int32_t)sizeof(g_enc));

    printf("  vertical detail: plain rle = %ld bytes, row delta = %ld bytes\n",
           (long)plain, (long)delta);

    CHECK(plain > SAVE_FRAME_BUDGET);
    CHECK(delta > 0);
    CHECK(delta <= SAVE_FRAME_BUDGET);
    CHECK_EQ(deltaRoundTrip(PAGE_BYTES, PAGE_STRIDE, 1), delta);
}

/*----------------------
 | buildBusyBandedPage
 | Description: Fills g_src with rows of flat spans whose widths and colours
 |   change on every scanline. Compressible across a row and not down the
 |   screen, so full-resolution differencing overruns a save's budget and
 |   dropping scanlines is the only thing that brings it back inside.
 | Author: suinevere
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void buildBusyBandedPage(void) {
    uint32_t seed = 0xC0FFEEu;
    for (int row = 0; row < 200; ++row) {
        uint8_t *p = g_src + row * PAGE_STRIDE;
        int i = 0;
        while (i < PAGE_STRIDE) {
            seed = seed * 1103515245u + 12345u;
            int run = 2 + (int)((seed >> 16) % 4);
            const uint8_t v = (uint8_t)((seed >> 8) & 0xFF);
            while (run-- > 0 && i < PAGE_STRIDE) {
                p[i++] = v;
            }
        }
    }
}

static void test_dropping_scanlines_rescues_a_page_that_will_not_fit(void) {
    buildBusyBandedPage();

    const int32_t full = pageDeltaEncode(g_src, PAGE_BYTES, PAGE_STRIDE, 1,
                                         g_enc, (int32_t)sizeof(g_enc));
    const int32_t half = pageDeltaEncode(g_src, PAGE_BYTES, PAGE_STRIDE, 2,
                                         g_enc, (int32_t)sizeof(g_enc));
    const int32_t quarter = pageDeltaEncode(g_src, PAGE_BYTES, PAGE_STRIDE, 4,
                                            g_enc, (int32_t)sizeof(g_enc));

    const int32_t eighth = pageDeltaEncode(g_src, PAGE_BYTES, PAGE_STRIDE, 8,
                                           g_enc, (int32_t)sizeof(g_enc));

    printf("  busy bands: full = %ld, half = %ld, quarter = %ld, "
           "eighth = %ld bytes\n",
           (long)full, (long)half, (long)quarter, (long)eighth);

    CHECK(full > SAVE_FRAME_BUDGET);
    CHECK(eighth > 0);
    CHECK(eighth <= SAVE_FRAME_BUDGET);
    CHECK(eighth < quarter);
    CHECK(quarter < half);
    CHECK(half < full);
}

static void test_eighth_height_fits_even_incompressible_noise(void) {
    uint32_t seed = 0xDEADBEEFu;
    for (int32_t i = 0; i < PAGE_BYTES; ++i) {
        seed = seed * 1103515245u + 12345u;
        g_src[i] = (uint8_t)(seed >> 16);
    }

    const int32_t enc = deltaRoundTrip(PAGE_BYTES, PAGE_STRIDE, 8);
    printf("  worst case noise: eighth = %ld bytes\n", (long)enc);
    CHECK(enc > 0);
    CHECK(enc <= SAVE_FRAME_BUDGET);
}

static void test_half_height_repeats_the_kept_scanline(void) {
    buildPolygonArtPage();
    const int32_t enc = deltaRoundTrip(PAGE_BYTES, PAGE_STRIDE, 2);
    CHECK(enc > 0);

    for (int i = 0; i < PAGE_STRIDE; ++i) {
        CHECK_EQ(g_dec[1 * PAGE_STRIDE + i], g_src[0 * PAGE_STRIDE + i]);
        CHECK_EQ(g_dec[2 * PAGE_STRIDE + i], g_src[2 * PAGE_STRIDE + i]);
    }
}

static void test_quarter_height_round_trips(void) {
    buildPolygonArtPage();
    CHECK(deltaRoundTrip(PAGE_BYTES, PAGE_STRIDE, 4) > 0);
}

static void test_delta_refuses_a_non_positive_stride(void) {
    g_src[0] = 1;
    CHECK_EQ(pageDeltaEncode(g_src, 16, 0, 1, g_enc, (int32_t)sizeof(g_enc)), -1);
    CHECK_EQ(pageDeltaEncode(g_src, 16, -4, 1, g_enc, (int32_t)sizeof(g_enc)), -1);
    CHECK(!pageDeltaDecode(g_enc, 4, g_dec, 16, 0, 1));
    CHECK(!pageDeltaDecode(g_enc, 4, g_dec, 16, -4, 1));
}

static void test_delta_decode_rejects_a_truncated_stream(void) {
    for (int32_t i = 0; i < 512; ++i) {
        g_src[i] = (uint8_t)(i / 64);
    }
    const int32_t enc =
        pageDeltaEncode(g_src, 512, 64, 1, g_enc, (int32_t)sizeof(g_enc));
    CHECK(enc > 1);
    CHECK(!pageDeltaDecode(g_enc, enc - 1, g_dec, 512, 64, 1));
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
    test_delta_round_trips_on_polygon_art();
    test_delta_beats_plain_rle_on_polygon_art();
    test_delta_round_trips_on_incompressible_data();
    test_delta_round_trips_when_rows_repeat_exactly();
    test_delta_rescues_a_page_plain_rle_cannot_fit();
    test_dropping_scanlines_rescues_a_page_that_will_not_fit();
    test_eighth_height_fits_even_incompressible_noise();
    test_half_height_repeats_the_kept_scanline();
    test_quarter_height_round_trips();
    test_delta_refuses_a_non_positive_stride();
    test_delta_decode_rejects_a_truncated_stream();

    if (g_fail == 0) {
        printf("all tests passed\n");
        return 0;
    }
    printf("%d check(s) failed\n", g_fail);
    return 1;
}
