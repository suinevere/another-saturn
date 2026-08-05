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
#include "menu_art.h"

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

static void test_strobe_endpoints(void)
{
    CHECK_EQ(MENU_ART_STROBE[15][0], MENU_ART_PALETTE[12 * 2]);
    CHECK_EQ(MENU_ART_STROBE[15][1], MENU_ART_PALETTE[12 * 2 + 1]);
    CHECK_EQ(MENU_ART_STROBE[15][2], MENU_ART_PALETTE[13 * 2]);
    CHECK_EQ(MENU_ART_STROBE[15][3], MENU_ART_PALETTE[13 * 2 + 1]);
    CHECK_EQ(MENU_ART_STROBE[15][4], MENU_ART_PALETTE[14 * 2]);
    CHECK_EQ(MENU_ART_STROBE[15][5], MENU_ART_PALETTE[14 * 2 + 1]);

    CHECK_EQ(MENU_ART_STROBE[0][0], 0x00);
    CHECK_EQ(MENU_ART_STROBE[0][1], 0x66);
    CHECK_EQ(MENU_ART_STROBE[0][2], 0x03);
    CHECK_EQ(MENU_ART_STROBE[0][3], 0x78);
    CHECK_EQ(MENU_ART_STROBE[0][4], 0x06);
    CHECK_EQ(MENU_ART_STROBE[0][5], 0x88);
}

static void test_strobe_is_monotonic(void)
{
    for (int e = 0; e < 3; ++e) {
        for (int l = 1; l < MENU_ART_STROBE_LEVELS; ++l) {
            const uint8_t *prev = MENU_ART_STROBE[l - 1];
            const uint8_t *cur  = MENU_ART_STROBE[l];
            const int pg = (prev[e * 2 + 1] & 0xF0) >> 4;
            const int cg = (cur[e * 2 + 1] & 0xF0) >> 4;
            CHECK_EQ(cg >= pg, 1);
        }
    }
}

static void test_palette_has_sixteen_entries(void)
{
    CHECK_EQ(MENU_ART_PALETTE[0], 0x00);
    CHECK_EQ(MENU_ART_PALETTE[1], 0x00);
    CHECK_EQ(MENU_ART_PALETTE[4 * 2], 0x00);
    CHECK_EQ(MENU_ART_PALETTE[4 * 2 + 1], 0x44);
    CHECK_EQ(MENU_ART_PALETTE[15 * 2], 0x0F);
    CHECK_EQ(MENU_ART_PALETTE[15 * 2 + 1], 0xFF);
}

static void test_bolt_dimensions(void)
{
    CHECK_EQ(MENU_ART_BOLT[0].w, 46);
    CHECK_EQ(MENU_ART_BOLT[0].h, 63);
    CHECK_EQ(MENU_ART_BOLT[2].h, 40);
}

static void test_freeze_remap_bounds_every_nibble(void)
{
    setup();
    for (int i = 0; i < MENU_PAGE_SIZE; ++i) {
        g_page[i] = (uint8_t)(i & 0xFF);
    }

    uint8_t pal[32];
    for (int i = 0; i < 16; ++i) {
        pal[i * 2]     = (uint8_t)i;
        pal[i * 2 + 1] = (uint8_t)((i << 4) | i);
    }

    menuFreezeRemap(g_page, pal);

    for (int i = 0; i < MENU_PAGE_SIZE; ++i) {
        CHECK_EQ((g_page[i] >> 4) <= 3, 1);
        CHECK_EQ((g_page[i] & 0x0F) <= 3, 1);
    }
}

static void test_freeze_remap_uniform_palette_is_uniform(void)
{
    setup();
    memset(g_page, 0xAB, MENU_PAGE_SIZE);

    uint8_t pal[32];
    for (int i = 0; i < 16; ++i) {
        pal[i * 2]     = 0x0F;
        pal[i * 2 + 1] = 0xFF;
    }

    menuFreezeRemap(g_page, pal);
    CHECK_EQ(g_page[0], 0x33);
    CHECK_EQ(g_page[MENU_PAGE_SIZE - 1], 0x33);
}

static void test_freeze_remap_darkest_becomes_black(void)
{
    setup();
    memset(g_page, 0x00, MENU_PAGE_SIZE);

    uint8_t pal[32];
    memset(pal, 0, sizeof(pal));
    pal[0] = 0x00;
    pal[1] = 0x00;

    menuFreezeRemap(g_page, pal);
    CHECK_EQ(g_page[0], 0x00);
}

static void test_freeze_remap_touches_both_nibbles(void)
{
    setup();
    uint8_t pal[32];
    memset(pal, 0, sizeof(pal));
    pal[15 * 2]     = 0x0F;
    pal[15 * 2 + 1] = 0xFF;

    g_page[0] = 0xF0;
    menuFreezeRemap(g_page, pal);
    CHECK_EQ(g_page[0] >> 4, 3);
    CHECK_EQ(g_page[0] & 0x0F, 0);
}

static void test_backdrop_uses_only_logo_palette(void)
{
    /* 0, 4-6 and 15 the backdrop shares with the wordmark and the bolts; 1-3
       and 11 are its own, free while the title screen is up. 7-10 and 12-14
       must stay clear: the menu draws START GAME and LOAD GAME at those bases
       straight over the backdrop. */
    const uint8_t allowed[] = { 0, 1, 2, 3, 4, 5, 6, 11, 15 };

    for (int i = 0; i < MENU_PAGE_SIZE; ++i) {
        uint8_t hi = MENU_ART_TITLE_BACKDROP[i] >> 4;
        uint8_t lo = MENU_ART_TITLE_BACKDROP[i] & 0x0F;

        bool hiOk = false;
        bool loOk = false;
        for (int a = 0; a < (int)(sizeof(allowed) / sizeof(allowed[0])); ++a) {
            if (hi == allowed[a]) hiOk = true;
            if (lo == allowed[a]) loOk = true;
        }

        CHECK_EQ(hiOk, true);
        CHECK_EQ(loOk, true);
    }
}

static int sumEntry(const uint8_t *pal, int i)
{
    return (pal[i * 2] & 0x0F) + ((pal[i * 2 + 1] & 0xF0) >> 4) + (pal[i * 2 + 1] & 0x0F);
}

static void test_title_palette_only_moves_the_backdrop_ramp(void)
{
    /* Entries 1-3 double as the pause screen's freeze ramp, so the backdrop's
       values for them must live in the title copy and nowhere else. 7-10 and
       12-14 are the bases the menu entries draw at and must match exactly. */
    for (int i = 0; i < 16; ++i) {
        const bool ownedByBackdrop = (i >= 1 && i <= 3) || i == 11;
        if (ownedByBackdrop) {
            continue;
        }
        CHECK_EQ(MENU_ART_TITLE_PALETTE[i * 2], MENU_ART_PALETTE[i * 2]);
        CHECK_EQ(MENU_ART_TITLE_PALETTE[i * 2 + 1], MENU_ART_PALETTE[i * 2 + 1]);
    }
}

static void test_title_palette_ramp_climbs(void)
{
    /* The ramp exists to fill the gap the shared slots leave, so it has to stay
       ordered: a flat or inverted one puts the holes back. */
    const uint8_t ramp[] = { 1, 2, 3, 11 };

    for (int i = 1; i < (int)(sizeof(ramp) / sizeof(ramp[0])); ++i) {
        CHECK_EQ(sumEntry(MENU_ART_TITLE_PALETTE, ramp[i]) >
                 sumEntry(MENU_ART_TITLE_PALETTE, ramp[i - 1]), 1);
    }
}

static void test_backdrop_clears_the_menu_band(void)
{
    /* The capture's credit block sat exactly where the menu entries go; the
       generator clears rows 131-164 so they have somewhere to land. */
    for (int y = 131; y < 165; ++y) {
        for (int x = 0; x < MENU_PAGE_PITCH; ++x) {
            CHECK_EQ(MENU_ART_TITLE_BACKDROP[y * MENU_PAGE_PITCH + x], 0x00);
        }
    }
}

static void test_text_renders_at_base_plus_two(void)
{
    static uint8_t font[96 * 8];
    memset(font, 0xFF, sizeof(font));

    setup();
    menuDrawText(g_page, font, 0, 0, 8, "A");
    CHECK_EQ(pixelAt(0, 0), 10);

    setup();
    menuDrawText(g_page, font, 0, 0, 12, "A");
    CHECK_EQ(pixelAt(0, 0), 14);
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
    test_strobe_endpoints();
    test_strobe_is_monotonic();
    test_palette_has_sixteen_entries();
    test_bolt_dimensions();
    test_freeze_remap_bounds_every_nibble();
    test_freeze_remap_uniform_palette_is_uniform();
    test_freeze_remap_darkest_becomes_black();
    test_freeze_remap_touches_both_nibbles();
    test_backdrop_uses_only_logo_palette();
    test_backdrop_clears_the_menu_band();
    test_title_palette_only_moves_the_backdrop_ramp();
    test_title_palette_ramp_climbs();
    test_text_renders_at_base_plus_two();

    if (g_fail != 0) {
        printf("%d check(s) failed\n", g_fail);
        return 1;
    }
    printf("menu art: all checks passed\n");
    return 0;
}
