/*----------------------
 | test_menu_art.cxx
 | Description: Host unit tests for the artwork blitters, the frozen-frame
 |   remap and the strobe table. All of it is arithmetic over a buffer, so it
 |   runs off-target rather than being eyeballed on hardware.
 | Author: suinevere
 | Dependencies: menu_blit.h, menu_draw.h
 ----------------------*/
#include <cstdio>
#include <cstdint>
#include <cstring>
#include "menu_blit.h"
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

static void setup(void)
{
    memset(g_page, 0, sizeof(g_page));
}

static uint8_t pixelAt(int x, int y)
{
    uint8_t b = g_page[y * MENU_PAGE_PITCH + x / 2];
    return (x & 1) ? (b & 0x0F) : (b >> 4);
}

/* 4 wide, 2 tall: row 0 = 4,5,6,0  row 1 = 0,6,5,4 */
static const uint8_t k4bppBits[] = { 0x45, 0x60, 0x06, 0x54 };
static const MenuArt k4bpp = { k4bppBits, 4, 2 };

/* 4 wide, 2 tall, 2bpp: row 0 = 1,2,3,0  row 1 = 0,3,2,1 */
static const uint8_t k2bppBits[] = { 0x6C, 0x39 };
static const MenuArt k2bpp = { k2bppBits, 4, 2 };

static void test_blit4_even_x(void)
{
    setup();
    menuBlit4bpp(g_page, &k4bpp, 10, 3);
    CHECK_EQ(pixelAt(10, 3), 4);
    CHECK_EQ(pixelAt(11, 3), 5);
    CHECK_EQ(pixelAt(12, 3), 6);
    CHECK_EQ(pixelAt(13, 3), 0);
    CHECK_EQ(pixelAt(11, 4), 6);
    CHECK_EQ(pixelAt(13, 4), 4);
}

static void test_blit4_odd_x(void)
{
    setup();
    menuBlit4bpp(g_page, &k4bpp, 11, 3);
    CHECK_EQ(pixelAt(11, 3), 4);
    CHECK_EQ(pixelAt(12, 3), 5);
    CHECK_EQ(pixelAt(13, 3), 6);
    CHECK_EQ(pixelAt(10, 3), 0);
}

static void test_blit4_shade0_is_transparent(void)
{
    setup();
    menuDrawFill(g_page, 0, 0, 320, 8, 9);
    menuBlit4bpp(g_page, &k4bpp, 10, 3);
    CHECK_EQ(pixelAt(13, 3), 9);
    CHECK_EQ(pixelAt(10, 4), 9);
    CHECK_EQ(pixelAt(10, 3), 4);
}

static void test_blit4_clips_all_edges(void)
{
    setup();
    menuBlit4bpp(g_page, &k4bpp, -2, -1);
    CHECK_EQ(pixelAt(0, 0), 5);
    CHECK_EQ(pixelAt(1, 0), 4);

    setup();
    menuBlit4bpp(g_page, &k4bpp, 318, 199);
    CHECK_EQ(pixelAt(318, 199), 4);
    CHECK_EQ(pixelAt(319, 199), 5);

    setup();
    menuBlit4bpp(g_page, &k4bpp, 400, 400);
    CHECK_EQ(g_page[0], 0);
}

static void test_blit2_applies_base(void)
{
    setup();
    menuBlit2bpp(g_page, &k2bpp, 10, 3, 8);
    CHECK_EQ(pixelAt(10, 3), 8);
    CHECK_EQ(pixelAt(11, 3), 9);
    CHECK_EQ(pixelAt(12, 3), 10);
    CHECK_EQ(pixelAt(13, 3), 0);
}

static void test_blit2_bases_differ_by_four(void)
{
    setup();
    menuBlit2bpp(g_page, &k2bpp, 10, 3, 8);
    uint8_t un0 = pixelAt(10, 3);
    uint8_t un2 = pixelAt(12, 3);

    setup();
    menuBlit2bpp(g_page, &k2bpp, 10, 3, 12);
    CHECK_EQ(pixelAt(10, 3) - un0, 4);
    CHECK_EQ(pixelAt(12, 3) - un2, 4);
}

static void test_blit2_shade0_is_transparent(void)
{
    setup();
    menuDrawFill(g_page, 0, 0, 320, 8, 9);
    menuBlit2bpp(g_page, &k2bpp, 10, 3, 12);
    CHECK_EQ(pixelAt(13, 3), 9);
    CHECK_EQ(pixelAt(10, 4), 9);
}

static void test_blit2_odd_x_and_clip(void)
{
    setup();
    menuBlit2bpp(g_page, &k2bpp, 11, 3, 12);
    CHECK_EQ(pixelAt(11, 3), 12);
    CHECK_EQ(pixelAt(12, 3), 13);

    setup();
    menuBlit2bpp(g_page, &k2bpp, -1, 0, 12);
    CHECK_EQ(pixelAt(0, 0), 13);
    CHECK_EQ(pixelAt(1, 0), 14);
}

int main(void)
{
    test_blit4_even_x();
    test_blit4_odd_x();
    test_blit4_shade0_is_transparent();
    test_blit4_clips_all_edges();
    test_blit2_applies_base();
    test_blit2_bases_differ_by_four();
    test_blit2_shade0_is_transparent();
    test_blit2_odd_x_and_clip();

    if (g_fail != 0) {
        printf("%d check(s) failed\n", g_fail);
        return 1;
    }
    printf("menu art: all checks passed\n");
    return 0;
}
