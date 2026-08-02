/*----------------------
 | test_menu_draw.cxx
 | Description: Host unit tests for the menu drawing primitives. The 4bpp
 |   packing and the palette dim are pure arithmetic over a buffer, so they run
 |   off-target rather than being eyeballed on hardware.
 | Author: suinevere
 | Dependencies: menu_draw.h
 ----------------------*/
#include <cstdio>
#include <cstdint>
#include <cstring>
#include "menu_draw.h"

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

static uint8_t g_page[MENU_PAGE_SIZE];

/* A font where every glyph is solid, so the packing is unambiguous. */
static uint8_t g_font[96 * 8];

static void setup(void)
{
    memset(g_page, 0, sizeof(g_page));
    memset(g_font, 0xFF, sizeof(g_font));
}

static uint8_t pixelAt(int x, int y)
{
    uint8_t b = g_page[y * MENU_PAGE_PITCH + x / 2];
    return (x & 1) ? (b & 0x0F) : (b >> 4);
}

static void test_fill_sets_both_nibbles(void)
{
    setup();
    menuDrawFill(g_page, 0, 0, 4, 1, 7);
    CHECK_EQ(g_page[0], 0x77);
    CHECK_EQ(g_page[1], 0x77);
    CHECK_EQ(g_page[2], 0x00);
}

static void test_fill_handles_an_odd_left_edge(void)
{
    setup();
    menuDrawFill(g_page, 1, 0, 2, 1, 5);
    CHECK_EQ(pixelAt(0, 0), 0);
    CHECK_EQ(pixelAt(1, 0), 5);
    CHECK_EQ(pixelAt(2, 0), 5);
    CHECK_EQ(pixelAt(3, 0), 0);
}

static void test_fill_clips_to_the_page(void)
{
    setup();
    menuDrawFill(g_page, -4, -4, 8, 8, 3);
    CHECK_EQ(pixelAt(0, 0), 3);
    menuDrawFill(g_page, MENU_PAGE_W - 2, MENU_PAGE_H - 2, 8, 8, 4);
    CHECK_EQ(pixelAt(MENU_PAGE_W - 1, MENU_PAGE_H - 1), 4);
}

static void test_char_writes_eight_by_eight(void)
{
    setup();
    menuDrawChar(g_page, g_font, 0, 0, 9, 'A');
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            CHECK_EQ(pixelAt(x, y), 9);
        }
    }
    CHECK_EQ(pixelAt(8, 0), 0);
    CHECK_EQ(pixelAt(0, 8), 0);
}

static void test_char_lands_on_the_right_cell(void)
{
    setup();
    menuDrawChar(g_page, g_font, 3, 16, 2, 'B');
    CHECK_EQ(pixelAt(24, 16), 2);
    CHECK_EQ(pixelAt(23, 16), 0);
}

/* Zeroes the font instead of filling it solid, so a test can set one
   asymmetric row and tell left from right and high nibble from low. */
static void setupAsymmetricFont(void)
{
    memset(g_page, 0, sizeof(g_page));
    memset(g_font, 0, sizeof(g_font));
}

static int pageIsAllZero(void)
{
    for (int i = 0; i < MENU_PAGE_SIZE; ++i) {
        if (g_page[i] != 0) {
            return 0;
        }
    }
    return 1;
}

static void test_char_bit7_maps_to_leftmost_pixel(void)
{
    setupAsymmetricFont();
    g_font[('A' - ' ') * 8] = 0x80;
    menuDrawChar(g_page, g_font, 0, 0, 9, 'A');
    CHECK_EQ(pixelAt(0, 0), 9);
    for (int x = 1; x < 8; ++x) {
        CHECK_EQ(pixelAt(x, 0), 0);
    }
}

static void test_char_nibble_boundary_within_a_byte(void)
{
    setupAsymmetricFont();
    g_font[('A' - ' ') * 8] = 0xC0;
    menuDrawChar(g_page, g_font, 0, 0, 9, 'A');
    CHECK_EQ(pixelAt(0, 0), 9);
    CHECK_EQ(pixelAt(1, 0), 9);
    for (int x = 2; x < 8; ++x) {
        CHECK_EQ(pixelAt(x, 0), 0);
    }
}

static void test_char_right_side_bits_land_on_the_right(void)
{
    setupAsymmetricFont();
    g_font[('A' - ' ') * 8] = 0x03;
    menuDrawChar(g_page, g_font, 0, 0, 9, 'A');
    for (int x = 0; x < 6; ++x) {
        CHECK_EQ(pixelAt(x, 0), 0);
    }
    CHECK_EQ(pixelAt(6, 0), 9);
    CHECK_EQ(pixelAt(7, 0), 9);
}

static void test_char_negative_cellx_leaves_page_unmodified(void)
{
    setup();
    menuDrawChar(g_page, g_font, -1, 0, 9, 'A');
    CHECK_EQ(pageIsAllZero(), 1);
}

static void test_char_negative_y_leaves_page_unmodified(void)
{
    setup();
    menuDrawChar(g_page, g_font, 0, -1, 9, 'A');
    CHECK_EQ(pageIsAllZero(), 1);
}

static void test_text_advances_one_cell_per_character(void)
{
    setup();
    menuDrawText(g_page, g_font, 0, 0, 1, "AB");
    CHECK_EQ(pixelAt(0, 0), 1);
    CHECK_EQ(pixelAt(8, 0), 1);
    CHECK_EQ(pixelAt(16, 0), 0);
}

static void test_text_off_the_right_edge_is_dropped(void)
{
    setup();
    menuDrawText(g_page, g_font, 38, 0, 1, "ABCD");
    CHECK_EQ(pixelAt(312, 0), 1);
}

static void test_dim_halves_every_channel(void)
{
    uint8_t src[32];
    uint8_t dst[32];
    memset(src, 0, sizeof(src));
    src[0] = 0x0E;
    src[1] = 0xA6;

    menuDrawDimPalette(src, dst, -1);
    CHECK_EQ(dst[0] & 0x0F, 7);
    CHECK_EQ((dst[1] & 0xF0) >> 4, 5);
    CHECK_EQ(dst[1] & 0x0F, 3);
}

static void test_dim_keeps_the_text_index_bright(void)
{
    uint8_t src[32];
    uint8_t dst[32];
    memset(src, 0, sizeof(src));

    menuDrawDimPalette(src, dst, 15);
    CHECK_EQ(dst[30] & 0x0F, 15);
    CHECK_EQ((dst[31] & 0xF0) >> 4, 15);
    CHECK_EQ(dst[31] & 0x0F, 15);
    CHECK_EQ(dst[0] & 0x0F, 0);
}

int main(void)
{
    test_fill_sets_both_nibbles();
    test_fill_handles_an_odd_left_edge();
    test_fill_clips_to_the_page();
    test_char_writes_eight_by_eight();
    test_char_lands_on_the_right_cell();
    test_char_bit7_maps_to_leftmost_pixel();
    test_char_nibble_boundary_within_a_byte();
    test_char_right_side_bits_land_on_the_right();
    test_char_negative_cellx_leaves_page_unmodified();
    test_char_negative_y_leaves_page_unmodified();
    test_text_advances_one_cell_per_character();
    test_text_off_the_right_edge_is_dropped();
    test_dim_halves_every_channel();
    test_dim_keeps_the_text_index_bright();

    if (g_fail == 0) {
        printf("all tests passed\n");
        return 0;
    }
    printf("%d check(s) failed\n", g_fail);
    return 1;
}
