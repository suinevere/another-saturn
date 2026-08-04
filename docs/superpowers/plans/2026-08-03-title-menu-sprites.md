# Title Menu Artwork Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the placeholder title card with the Mega Drive title screen — extracted chrome wordmark, lightning that strikes at intervals, and a pulsing selected row — and carry the same palette and selection feedback into the pause and save/load screens.

**Architecture:** Artwork is packed into 4bpp (absolute palette index) or 2bpp (relative shade plus a base index) byte arrays and blitted into the existing `s_menuPage` buffer, because `sat_video_present` DMAs one 320×200 4bpp page to VDP2 and there is no sprite layer. Selection is a base-index change rather than a redraw, so pulsing the selected row costs three palette writes per frame. The pause screen frees its palette entries by remapping the frozen game frame down to three greys.

**Tech Stack:** C++11 (SH-2 cross build via SaturnRingLib), Python 3 with Pillow for the offline asset tool, g++ host binaries for unit tests.

## Global Constraints

- **Spec:** `docs/superpowers/specs/2026-08-03-title-menu-sprites-design.md`. Every dimension, coordinate and palette byte in this plan is copied from it.
- **Comment style** (`CLAUDE.md`): no comments inside function bodies. Every file, function and constant gets the `/*---- | name | Description: | Author: suinevere | Dependencies: | Globals: | Params: | Returns: ----*/` header block. `N/A` for fields that do not apply. Keep prose to a sentence.
- **Commits:** one sentence, no body, no bullets, no trailers. Never mention Claude, AI, or the session. Stage named files — never `git add -A`.
- **Author of record:** suinevere.
- **Host test flags:** new files are held to `OWN_FLAGS` = `-std=c++11 -Wall -Wextra -Werror -O1 -g`. Warnings are errors; unused parameters will fail the build.
- **No engine headers in `menu_blit`, `menu_draw` or `menu_art`.** No SGL, no SRL, no libc beyond `<stdint.h>`. That separation is what keeps them host-testable.
- **The Saturn makefile globs `src/**/*.cxx`** (`saturn/makefile:42`), so new source files need no build-file edit.
- **Page geometry:** `MENU_PAGE_W` 320, `MENU_PAGE_H` 200, `MENU_PAGE_PITCH` 160, `MENU_PAGE_SIZE` 32000. High nibble is the left pixel.
- **Build:** `saturn/compile.bat debug`. **Host tests:** `sh saturn/tests/run_tests.sh`. **Run:** `saturn/run_with_mednafen.bat`.

---

## File Structure

**Create:**

| Path | Responsibility |
|---|---|
| `saturn/src/menu_blit.h` / `.cxx` | `MenuArt` struct and the two blitters. Pure buffer arithmetic, no dependencies. |
| `saturn/src/menu_art.h` | Declares the generated assets. Hand-written. |
| `saturn/src/menu_art.cxx` | Generated asset bytes. **Never hand-edited.** |
| `tools/mkmenuart.py` | Reads references and authored PNGs, emits `menu_art.cxx`. |
| `tools/mkchromestrings.py` | Builds the two chrome string PNGs from extracted glyphs plus four drawn ones. |
| `saturn/art/chrome_start_game.png` | Authored string art, 152×15, four colours. Source of truth once generated. |
| `saturn/art/chrome_load_game.png` | Same. |
| `saturn/tests/test_menu_art.cxx` | Host suite for the blitters, the freeze remap and the strobe table. |

**Modify:**

| Path | Change |
|---|---|
| `saturn/src/menu_draw.h` / `.cxx` | Add `menuFreezeRemap`; `menuDrawChar`/`menuDrawText` take a base index; delete `menuDrawDimPalette`. |
| `saturn/tests/test_menu_draw.cxx` | Update for new signatures; drop dim-palette tests. |
| `saturn/src/menu.h` | Drop `_dimPal`; add title-screen animation state. |
| `saturn/src/menu.cxx` | Blit artwork, drive strobe and lightning, remap the frozen frame. |
| `saturn/tests/run_tests.sh` | Add the `menu art` suite. |
| `.gitignore` | Ignore `run_tests_menuart` binaries. |

---

### Task 1: Blitters

**Files:**
- Create: `saturn/src/menu_blit.h`, `saturn/src/menu_blit.cxx`
- Test: `saturn/tests/test_menu_art.cxx`
- Modify: `saturn/tests/run_tests.sh`, `.gitignore`

**Interfaces:**
- Consumes: `MENU_PAGE_*` from `menu_draw.h`.
- Produces: `struct MenuArt { const uint8_t *bits; int16_t w; int16_t h; };`,
  `void menuBlit4bpp(uint8_t *page, const MenuArt *art, int x, int y)`,
  `void menuBlit2bpp(uint8_t *page, const MenuArt *art, int x, int y, uint8_t base)`.
  4bpp row pitch is `(w + 1) >> 1`; 2bpp row pitch is `(w + 3) >> 2`. Value 0 is
  transparent in both. In 2bpp a stored shade `v` in 1..3 writes index `base + v - 1`.

- [ ] **Step 1: Write the failing test**

Create `saturn/tests/test_menu_art.cxx`:

```cpp
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
```

- [ ] **Step 2: Run the test to verify it fails**

Run:
```sh
cd saturn/tests && g++ -std=c++11 -Wall -Wextra -Werror -O1 -g -I../src \
    -o run_tests_menuart test_menu_art.cxx ../src/menu_draw.cxx ../src/menu_blit.cxx
```
Expected: FAIL — `menu_blit.h: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `saturn/src/menu_blit.h`:

```cpp
/*----------------------
 | menu_blit.h
 | Description: Blits packed artwork into a raw 4bpp page. Two formats: 4bpp
 |   holding absolute palette indices for art that never changes colour, and
 |   2bpp holding a relative shade for art that must render in more than one
 |   ramp. Index 0 is transparent in both. No engine dependency, so the packing
 |   arithmetic is host-testable the same way menu_draw.h is.
 | Author: suinevere
 | Dependencies: stdint.h
 ----------------------*/
#ifndef MENU_BLIT_H
#define MENU_BLIT_H

#include <stdint.h>

/*----------------------
 | MenuArt
 | Description: One packed bitmap. Row pitch is derived from w and the format,
 |   not stored: (w + 1) >> 1 for 4bpp, (w + 3) >> 2 for 2bpp. Rows are padded
 |   to a whole byte.
 | Author: suinevere
 ----------------------*/
struct MenuArt {
	const uint8_t *bits;
	int16_t w;
	int16_t h;
};

/*----------------------
 | menuBlit4bpp
 | Description: Draws a 4bpp bitmap whose values are absolute palette indices,
 |   clipped to the page. Index 0 is left as whatever was already there.
 | Author: suinevere
 | Params: page -- MENU_PAGE_SIZE bytes; art -- the bitmap; x, y -- top-left
 |         corner in pixels, may be negative or off the page
 | Returns: N/A
 ----------------------*/
void menuBlit4bpp(uint8_t *page, const MenuArt *art, int x, int y);

/*----------------------
 | menuBlit2bpp
 | Description: Draws a 2bpp bitmap whose values are shades 1..3, writing
 |   base + shade - 1, clipped to the page. Shade 0 is left untouched. This is
 |   how one piece of art renders both selected and unselected without a second
 |   copy: only the base changes.
 | Author: suinevere
 | Params: page -- MENU_PAGE_SIZE bytes; art -- the bitmap; x, y -- top-left
 |         corner in pixels; base -- palette index the shades are measured from
 | Returns: N/A
 ----------------------*/
void menuBlit2bpp(uint8_t *page, const MenuArt *art, int x, int y, uint8_t base);

#endif /* MENU_BLIT_H */
```

- [ ] **Step 4: Write the implementation**

Create `saturn/src/menu_blit.cxx`:

```cpp
/*----------------------
 | menu_blit.cxx
 | Description: The two artwork blitters. No engine headers: see menu_blit.h.
 | Author: suinevere
 | Dependencies: menu_blit.h, menu_draw.h
 ----------------------*/
#include "menu_blit.h"
#include "menu_draw.h"

/*----------------------
 | menuBlitPixel
 | Description: Writes one 4-bit pixel into the page, choosing the nibble from
 |   the x parity the same way menuDrawFill does.
 | Author: suinevere
 | Params: page -- MENU_PAGE_SIZE bytes; x, y -- position, assumed on the page;
 |         value -- 4-bit palette index
 | Returns: N/A
 ----------------------*/
static void menuBlitPixel(uint8_t *page, int x, int y, uint8_t value)
{
	uint8_t *b = page + y * MENU_PAGE_PITCH + x / 2;
	if (x & 1) {
		*b = (*b & 0xF0) | value;
	} else {
		*b = (*b & 0x0F) | (uint8_t)(value << 4);
	}
}

void menuBlit4bpp(uint8_t *page, const MenuArt *art, int x, int y)
{
	const int pitch = (art->w + 1) >> 1;

	for (int j = 0; j < art->h; ++j) {
		const int py = y + j;
		if (py < 0 || py >= MENU_PAGE_H) {
			continue;
		}
		const uint8_t *row = art->bits + j * pitch;
		for (int i = 0; i < art->w; ++i) {
			const int px = x + i;
			if (px < 0 || px >= MENU_PAGE_W) {
				continue;
			}
			const uint8_t b = row[i >> 1];
			const uint8_t v = (i & 1) ? (uint8_t)(b & 0x0F) : (uint8_t)(b >> 4);
			if (v != 0) {
				menuBlitPixel(page, px, py, v);
			}
		}
	}
}

void menuBlit2bpp(uint8_t *page, const MenuArt *art, int x, int y, uint8_t base)
{
	const int pitch = (art->w + 3) >> 2;

	for (int j = 0; j < art->h; ++j) {
		const int py = y + j;
		if (py < 0 || py >= MENU_PAGE_H) {
			continue;
		}
		const uint8_t *row = art->bits + j * pitch;
		for (int i = 0; i < art->w; ++i) {
			const int px = x + i;
			if (px < 0 || px >= MENU_PAGE_W) {
				continue;
			}
			const int shift = 6 - ((i & 3) << 1);
			const uint8_t v = (uint8_t)((row[i >> 2] >> shift) & 0x03);
			if (v != 0) {
				menuBlitPixel(page, px, py, (uint8_t)(base + v - 1));
			}
		}
	}
}
```

- [ ] **Step 5: Run the test to verify it passes**

Run:
```sh
cd saturn/tests && g++ -std=c++11 -Wall -Wextra -Werror -O1 -g -I../src \
    -o run_tests_menuart test_menu_art.cxx ../src/menu_draw.cxx ../src/menu_blit.cxx \
    && ./run_tests_menuart
```
Expected: PASS — `menu art: all checks passed`.

- [ ] **Step 6: Register the suite**

In `saturn/tests/run_tests.sh`, insert after the `menu draw` block and before `page rle`:

```sh
echo "== menu art =="
g++ $OWN_FLAGS -I../src \
    -o run_tests_menuart test_menu_art.cxx ../src/menu_draw.cxx ../src/menu_blit.cxx
./run_tests_menuart
```

In `.gitignore`, next to the other suite binaries:

```
saturn/tests/run_tests_menuart
saturn/tests/run_tests_menuart.exe
```

- [ ] **Step 7: Run the whole suite**

Run: `sh saturn/tests/run_tests.sh`
Expected: every suite passes, ending `all suites passed`.

- [ ] **Step 8: Commit**

```bash
git add saturn/src/menu_blit.h saturn/src/menu_blit.cxx \
        saturn/tests/test_menu_art.cxx saturn/tests/run_tests.sh .gitignore
git commit -m "Add 4bpp and 2bpp artwork blitters for the menu page"
```

---

### Task 2: Asset tool — palette, strobe table, logo, bolts

**Files:**
- Create: `tools/mkmenuart.py`, `saturn/src/menu_art.h`, `saturn/src/menu_art.cxx` (generated)
- Modify: `saturn/tests/run_tests.sh`

**Interfaces:**
- Consumes: `MenuArt` from Task 1.
- Produces: `MENU_ART_LOGO`, `MENU_ART_BOLT[3]` (`const MenuArt`), `MENU_ART_PALETTE[32]`,
  `MENU_ART_STROBE[16][6]` (16 levels × 3 entries × 2 bytes), and the count
  `MENU_ART_BOLT_COUNT = 3`, all `extern` from `menu_art.h`.

**Source geometry, copied from the spec:**
- Logo: `images/genesis.png`, crop (33,53)–(240,114) → 207×61, Lanczos to 290×61, re-quantised to its own four colours with dithering off.
- Bolt 0: `images/genesis lightning 3.png`, crop (210,0)–(256,63) → 46×63. Bolt 1 is bolt 0 mirrored horizontally. Bolt 2 is bolt 0 mirrored and cropped to the top 40 rows.

- [ ] **Step 1: Write the tool**

Create `tools/mkmenuart.py`:

```python
"""
mkmenuart.py
Description: Builds saturn/src/menu_art.cxx from the Mega Drive reference
  captures and the authored chrome strings. Run from the repository root.
  The generated file is committed; the Saturn build never invokes Python.
Author: suinevere
Usage: python tools/mkmenuart.py
"""
import os
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "saturn", "src", "menu_art.cxx")

# Palette entries 4-6: the wordmark's own colours, straight from the capture.
LOGO_COLOURS = [(0x00, 0x48, 0x49), (0x25, 0x6C, 0x6E), (0x49, 0x90, 0x93)]

# (index, r, g, b) in 4-bit channels. Matches the spec's palette table.
PALETTE = [
    (0,  0,  0,  0), (1,  1,  1,  2), (2,  2,  2,  3), (3,  3,  3,  4),
    (4,  0,  4,  4), (5,  2,  6,  6), (6,  4,  9,  9), (7,  1,  4,  4),
    (8,  0,  2,  2), (9,  1,  3,  3), (10, 2,  4,  4), (11, 0,  0,  0),
    (12, 0, 10, 11), (13, 5, 13, 14), (14, 11, 15, 15), (15, 15, 15, 15),
]

STROBE_ENTRIES = [12, 13, 14]
STROBE_LEVELS = 16
STROBE_FLOOR = 0.55


def quantise(im, colours):
    """Snap an RGB image onto an exact colour list, no dithering."""
    pal = Image.new("P", (1, 1))
    flat = []
    for c in colours:
        flat += list(c)
    flat += [0, 0, 0] * (256 - len(colours))
    pal.putpalette(flat)
    return im.convert("RGB").quantize(palette=pal, dither=Image.NONE).convert("RGB")


def pack4(im, index_of):
    """Pack an RGB image to 4bpp, one absolute palette index per pixel."""
    px = im.load()
    w, h = im.size
    pitch = (w + 1) >> 1
    out = bytearray(pitch * h)
    for y in range(h):
        for x in range(w):
            v = index_of(px[x, y])
            if v == 0:
                continue
            o = y * pitch + (x >> 1)
            if x & 1:
                out[o] = (out[o] & 0xF0) | v
            else:
                out[o] = (out[o] & 0x0F) | (v << 4)
    return bytes(out), w, h


def pack2(im, shade_of):
    """Pack an RGB image to 2bpp, one shade 0-3 per pixel."""
    px = im.load()
    w, h = im.size
    pitch = (w + 3) >> 2
    out = bytearray(pitch * h)
    for y in range(h):
        for x in range(w):
            v = shade_of(px[x, y])
            if v == 0:
                continue
            o = y * pitch + (x >> 2)
            out[o] |= v << (6 - ((x & 3) << 1))
    return bytes(out), w, h


def logo_index(c):
    if c == (0, 0, 0):
        return 0
    return 4 + LOGO_COLOURS.index(c)


def emit_array(f, name, data):
    f.write("static const uint8_t %s[%d] = {\n" % (name, len(data)))
    for i in range(0, len(data), 12):
        f.write("\t" + " ".join("0x%02X," % b for b in data[i:i + 12]) + "\n")
    f.write("};\n\n")


def build_logo():
    src = Image.open(os.path.join(ROOT, "images", "genesis.png")).convert("RGB")
    logo = src.crop((33, 53, 240, 114))
    stretched = logo.resize((290, 61), Image.LANCZOS)
    return pack4(quantise(stretched, [(0, 0, 0)] + LOGO_COLOURS), logo_index)


def build_bolts():
    src = Image.open(os.path.join(ROOT, "images", "genesis lightning 3.png")).convert("RGB")
    bolt = src.crop((210, 0, 256, 63))
    variants = [bolt,
                bolt.transpose(Image.FLIP_LEFT_RIGHT),
                bolt.transpose(Image.FLIP_LEFT_RIGHT).crop((0, 0, 46, 40))]

    ramp = sorted(set(bolt.get_flattened_data()), key=sum)
    ramp = [c for c in ramp if c != (0, 0, 0)]

    def bolt_index(c):
        if c == (0, 0, 0):
            return 0
        rank = ramp.index(c)
        return 15 if rank == len(ramp) - 1 else 4 + min(rank, 2)

    return [pack4(quantise(v, [(0, 0, 0)] + ramp), bolt_index) for v in variants]


def build_strings():
    out = []
    for name in ("chrome_start_game", "chrome_load_game"):
        path = os.path.join(ROOT, "saturn", "art", name + ".png")
        im = Image.open(path).convert("RGB")
        cols = [(0, 0, 0)] + LOGO_COLOURS

        def shade(c):
            return cols.index(c)

        out.append(pack2(quantise(im, cols), shade))
    return out


def build_strobe():
    rows = []
    for level in range(STROBE_LEVELS):
        scale = STROBE_FLOOR + (1.0 - STROBE_FLOOR) * level / (STROBE_LEVELS - 1)
        row = []
        for idx in STROBE_ENTRIES:
            _, r, g, b = PALETTE[idx]
            r = min(15, int(r * scale + 0.5))
            g = min(15, int(g * scale + 0.5))
            b = min(15, int(b * scale + 0.5))
            row += [r, (g << 4) | b]
        rows.append(row)
    return rows


def main():
    logo = build_logo()
    bolts = build_bolts()
    strings = build_strings()
    strobe = build_strobe()

    with open(OUT, "w") as f:
        f.write("/*----------------------\n")
        f.write(" | menu_art.cxx\n")
        f.write(" | Description: Generated by tools/mkmenuart.py. Do not edit by hand:\n")
        f.write(" |   change the source art or the tool and regenerate.\n")
        f.write(" | Author: suinevere\n")
        f.write(" | Dependencies: menu_art.h\n")
        f.write(" ----------------------*/\n")
        f.write('#include "menu_art.h"\n\n')

        emit_array(f, "s_logoBits", logo[0])
        for i, b in enumerate(bolts):
            emit_array(f, "s_boltBits%d" % i, b[0])
        emit_array(f, "s_startGameBits", strings[0][0])
        emit_array(f, "s_loadGameBits", strings[1][0])

        f.write("const MenuArt MENU_ART_LOGO = { s_logoBits, %d, %d };\n\n" % (logo[1], logo[2]))
        f.write("const MenuArt MENU_ART_BOLT[MENU_ART_BOLT_COUNT] = {\n")
        for i, b in enumerate(bolts):
            f.write("\t{ s_boltBits%d, %d, %d },\n" % (i, b[1], b[2]))
        f.write("};\n\n")
        f.write("const MenuArt MENU_ART_START_GAME = { s_startGameBits, %d, %d };\n"
                % (strings[0][1], strings[0][2]))
        f.write("const MenuArt MENU_ART_LOAD_GAME = { s_loadGameBits, %d, %d };\n\n"
                % (strings[1][1], strings[1][2]))

        f.write("const uint8_t MENU_ART_PALETTE[32] = {\n")
        for _, r, g, b in PALETTE:
            f.write("\t0x%02X, 0x%02X,\n" % (r, (g << 4) | b))
        f.write("};\n\n")

        f.write("const uint8_t MENU_ART_STROBE[MENU_ART_STROBE_LEVELS][6] = {\n")
        for row in strobe:
            f.write("\t{ " + " ".join("0x%02X," % v for v in row) + " },\n")
        f.write("};\n")

    print("wrote", OUT)


if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Write the header**

Create `saturn/src/menu_art.h`:

```cpp
/*----------------------
 | menu_art.h
 | Description: The menu's artwork, packed by tools/mkmenuart.py. The logo and
 |   bolts carry absolute palette indices because they never change colour; the
 |   two strings carry relative shades because they render in both the selected
 |   and the unselected ramp.
 | Author: suinevere
 | Dependencies: menu_blit.h
 ----------------------*/
#ifndef MENU_ART_H
#define MENU_ART_H

#include "menu_blit.h"

/*----------------------
 | MENU_ART_BOLT_COUNT / MENU_ART_STROBE_LEVELS
 | Description: How many lightning bolts the title screen chooses between, and
 |   how many brightness steps the selected-row strobe walks.
 | Author: suinevere
 ----------------------*/
enum {
	MENU_ART_BOLT_COUNT   = 3,
	MENU_ART_STROBE_LEVELS = 16
};

extern const MenuArt MENU_ART_LOGO;
extern const MenuArt MENU_ART_BOLT[MENU_ART_BOLT_COUNT];
extern const MenuArt MENU_ART_START_GAME;
extern const MenuArt MENU_ART_LOAD_GAME;

/*----------------------
 | MENU_ART_PALETTE
 | Description: All sixteen entries, two bytes each: R = byte0 & 0x0F,
 |   G = (byte1 & 0xF0) >> 4, B = byte1 & 0x0F.
 | Author: suinevere
 ----------------------*/
extern const uint8_t MENU_ART_PALETTE[32];

/*----------------------
 | MENU_ART_STROBE
 | Description: Entries 12, 13 and 14 at sixteen brightness levels, six bytes
 |   per level. Level 0 is 55% of MENU_ART_PALETTE, level 15 is full.
 | Author: suinevere
 ----------------------*/
extern const uint8_t MENU_ART_STROBE[MENU_ART_STROBE_LEVELS][6];

#endif /* MENU_ART_H */
```

- [ ] **Step 3: Stub the two string PNGs so the tool runs**

Task 3 authors these properly. For now create placeholders so the generator has inputs:

```sh
mkdir -p saturn/art
python -c "from PIL import Image; Image.new('RGB',(152,15),(0,0,0)).save('saturn/art/chrome_start_game.png'); Image.new('RGB',(152,15),(0,0,0)).save('saturn/art/chrome_load_game.png')"
```

- [ ] **Step 4: Generate and verify the output**

Run: `python tools/mkmenuart.py`
Expected: `wrote .../saturn/src/menu_art.cxx`.

Then check the dimensions landed as the spec says:
```sh
grep -n "MENU_ART_LOGO = " saturn/src/menu_art.cxx
grep -n "s_logoBits\[" saturn/src/menu_art.cxx
```
Expected: `{ s_logoBits, 290, 61 }` and an array length of `8845` (145 × 61).

- [ ] **Step 5: Write the strobe table test**

Append to `saturn/tests/test_menu_art.cxx`, before `main`:

```cpp
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

static void test_logo_dimensions(void)
{
    CHECK_EQ(MENU_ART_LOGO.w, 290);
    CHECK_EQ(MENU_ART_LOGO.h, 61);
    CHECK_EQ(MENU_ART_BOLT[0].w, 46);
    CHECK_EQ(MENU_ART_BOLT[0].h, 63);
    CHECK_EQ(MENU_ART_BOLT[2].h, 40);
}
```

Add `#include "menu_art.h"` to the test's includes, and the calls in `main`:

```cpp
    test_strobe_endpoints();
    test_strobe_is_monotonic();
    test_palette_has_sixteen_entries();
    test_logo_dimensions();
```

Update the suite's compile line in `saturn/tests/run_tests.sh` to link the
generated data:

```sh
echo "== menu art =="
g++ $OWN_FLAGS -I../src \
    -o run_tests_menuart test_menu_art.cxx ../src/menu_draw.cxx \
    ../src/menu_blit.cxx ../src/menu_art.cxx
./run_tests_menuart
```

- [ ] **Step 6: Run the suite**

Run: `sh saturn/tests/run_tests.sh`
Expected: `all suites passed`. A failure in `test_strobe_endpoints` means
`STROBE_FLOOR` or the rounding in `build_strobe` drifted from the spec's 55%.

- [ ] **Step 7: Commit**

```bash
git add tools/mkmenuart.py saturn/src/menu_art.h saturn/src/menu_art.cxx \
        saturn/tests/test_menu_art.cxx saturn/tests/run_tests.sh \
        saturn/art/chrome_start_game.png saturn/art/chrome_load_game.png
git commit -m "Extract the title wordmark and lightning bolts into generated menu artwork"
```

---

### Task 3: The two chrome strings

**Files:**
- Create: `tools/mkchromestrings.py`
- Modify: `saturn/art/chrome_start_game.png`, `saturn/art/chrome_load_game.png`, `saturn/src/menu_art.cxx` (regenerated)

**Interfaces:**
- Consumes: nothing from earlier tasks — it writes PNGs that Task 2's tool reads.
- Produces: two 152×15 PNGs in the logo's four colours, `START GAME` and `LOAD GAME`, each horizontally centred on its canvas so both blit at the same x.

**Why this task exists and why it is here.** The face interlocks: `START` yields four
ink runs for five letters, `PASSWORD` six for eight. Single glyphs can be cut for
S, T, R, O, D and W; A must be cut out of a merged pair; G, M, E and L do not
appear in either word and are drawn. This is the only hand-authored art in the
plan and the piece most likely to need iteration, so it is built before anything
depends on it. **The PNGs are the source of truth once generated** — they may be
retouched by hand afterwards without rerunning this tool.

- [ ] **Step 1: Verify the glyph cut points**

The spec's segmentation gives ink runs, not letters. Confirm the cuts before
trusting them:

```sh
python - <<'PY'
from PIL import Image
im = Image.open("images/genesis.png").convert("RGB")
for label, y0, y1 in (("START", 137, 151), ("PASSWORD", 156, 171)):
    band = im.crop((0, y0, 256, y1))
    px = band.load()
    cols = [any(sum(px[x, y]) > 24 for y in range(band.height)) for x in range(256)]
    runs, s = [], None
    for x, c in enumerate(cols):
        if c and s is None:
            s = x
        elif not c and s is not None:
            runs.append((s, x - 1)); s = None
    print(label, runs)
PY
```
Expected: `START [(98,112),(115,138),(143,153),(156,167)]` and
`PASSWORD [(77,99),(101,129),(133,146),(150,161),(165,175),(180,191)]`.
If the numbers differ, use the printed ones — the cut table below is derived from them.

- [ ] **Step 2: Write the string builder**

Create `tools/mkchromestrings.py`:

```python
"""
mkchromestrings.py
Description: Builds the two chrome menu strings as PNGs. Letters that appear in
  START or PASSWORD are cut from images/genesis.png; G, M, E and L appear in
  neither and are rasterised from stroke segments in the same blocky style.
  Run from the repository root. Output may be retouched by hand afterwards.
Author: suinevere
Usage: python tools/mkchromestrings.py
"""
import os
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ART = os.path.join(ROOT, "saturn", "art")

DARK, MID, RIM = (0x00, 0x48, 0x49), (0x25, 0x6C, 0x6E), (0x49, 0x90, 0x93)
BG = (0, 0, 0)

CELL_H = 15
CANVAS = (152, 15)
ADVANCE = 2          # blank columns between glyphs
SPACE_W = 8

# Letters cut straight out of the capture: (x0, x1 inclusive, band top).
# START sits on a 14-row band, PASSWORD on a 15-row band; START glyphs are
# padded one row at the top so every cell is CELL_H tall.
CUTS = {
    "S": (98, 112, 137, 1),
    "T": (156, 167, 137, 1),
    "A": (127, 138, 137, 1),   # right half of the merged TA run
    "R": (165, 175, 156, 0),
    "O": (150, 161, 156, 0),
    "D": (180, 191, 156, 0),
}

# Stroke skeletons for the four letters the capture does not contain. Each
# segment is a 3px-wide run between two centre points in an 11x15 cell (13 wide
# for M). Edges of the stroke take RIM, the middle takes DARK.
STROKES = {
    "E": (11, [((1, 1), (1, 13)), ((1, 1), (9, 1)),
               ((1, 7), (7, 7)), ((1, 13), (9, 13))]),
    "L": (11, [((1, 1), (1, 13)), ((1, 13), (9, 13))]),
    "G": (11, [((1, 1), (9, 1)), ((1, 1), (1, 13)), ((1, 13), (9, 13)),
               ((9, 7), (9, 13)), ((5, 7), (9, 7))]),
    "M": (13, [((1, 1), (1, 13)), ((11, 1), (11, 13)),
               ((1, 1), (6, 7)), ((11, 1), (6, 7))]),
}


def cut_glyph(src, letter):
    x0, x1, top, pad = CUTS[letter]
    g = Image.new("RGB", (x1 - x0 + 1, CELL_H), BG)
    band = src.crop((x0, top, x1 + 1, top + CELL_H - pad))
    g.paste(band, (0, pad))
    return g


def draw_glyph(letter):
    w, segs = STROKES[letter]
    g = Image.new("RGB", (w, CELL_H), BG)
    px = g.load()
    core = set()
    body = set()
    for (ax, ay), (bx, by) in segs:
        steps = max(abs(bx - ax), abs(by - ay))
        for s in range(steps + 1):
            cx = ax + (bx - ax) * s // steps
            cy = ay + (by - ay) * s // steps
            core.add((cx, cy))
            for dx in (-1, 0, 1):
                for dy in (-1, 0, 1):
                    body.add((cx + dx, cy + dy))
    for (x, y) in body:
        if 0 <= x < w and 0 <= y < CELL_H:
            px[x, y] = DARK if (x, y) in core else RIM
    for (x, y) in body:
        if not (0 <= x < w and 0 <= y < CELL_H):
            continue
        if (x, y) in core:
            continue
        neighbours = sum(1 for dx in (-1, 0, 1) for dy in (-1, 0, 1)
                         if (x + dx, y + dy) in core)
        if neighbours >= 4:
            px[x, y] = MID
    return g


def glyph(src, letter):
    return cut_glyph(src, letter) if letter in CUTS else draw_glyph(letter)


def build(src, text):
    glyphs = []
    width = 0
    for ch in text:
        if ch == " ":
            glyphs.append(None)
            width += SPACE_W
            continue
        g = glyph(src, ch)
        glyphs.append(g)
        width += g.width + ADVANCE
    width -= ADVANCE

    canvas = Image.new("RGB", CANVAS, BG)
    x = (CANVAS[0] - width) // 2
    for g in glyphs:
        if g is None:
            x += SPACE_W
            continue
        canvas.paste(g, (x, 0))
        x += g.width + ADVANCE
    return canvas


def main():
    src = Image.open(os.path.join(ROOT, "images", "genesis.png")).convert("RGB")
    os.makedirs(ART, exist_ok=True)
    build(src, "START GAME").save(os.path.join(ART, "chrome_start_game.png"))
    build(src, "LOAD GAME").save(os.path.join(ART, "chrome_load_game.png"))
    print("wrote", ART)


if __name__ == "__main__":
    main()
```

- [ ] **Step 3: Generate the strings**

Run: `python tools/mkchromestrings.py`
Expected: `wrote .../saturn/art`. Both PNGs are 152×15.

Check the palette came out clean:
```sh
python -c "
from PIL import Image
for n in ('chrome_start_game','chrome_load_game'):
    im=Image.open('saturn/art/%s.png'%n).convert('RGB')
    print(n, im.size, sorted(set(im.get_flattened_data())))"
```
Expected: each reports exactly the four colours `(0,0,0)`, `(0,72,73)`, `(37,108,110)`, `(73,144,147)` and no others. Any fifth colour means a cut box caught a neighbouring letter — narrow it and rerun.

- [ ] **Step 4: Compose a review image**

This is the visual gate the spec's risk section calls for. Build a full title screen and look at it:

```sh
python - <<'PY'
from PIL import Image
src = Image.open("images/genesis.png").convert("RGB")
lit = Image.open("images/genesis lightning 3.png").convert("RGB")

def key(dst, s, x, y):
    sp, dp = s.load(), dst.load()
    for j in range(s.height):
        for i in range(s.width):
            c = sp[i, j]
            if c != (0, 0, 0) and 0 <= x + i < dst.width and 0 <= y + j < dst.height:
                dp[x + i, y + j] = c

page = Image.new("RGB", (320, 200), (0, 0, 0))
key(page, src.crop((33, 53, 240, 114)).resize((290, 61), Image.LANCZOS), 15, 30)
key(page, lit.crop((210, 0, 256, 63)), 268, 0)
key(page, Image.open("saturn/art/chrome_start_game.png").convert("RGB"), 84, 128)
key(page, Image.open("saturn/art/chrome_load_game.png").convert("RGB"), 84, 152)
page.resize((960, 600), Image.NEAREST).save("saturn/art/preview_title.png")
print("wrote saturn/art/preview_title.png")
PY
```

**Stop and look at `saturn/art/preview_title.png`.** The four drawn letters must sit convincingly beside the six cut ones — same stroke weight, same rim brightness, same cap height. If they do not, adjust `STROKES` and rerun steps 3 and 4 before continuing. If they cannot be made to match, the spec's fallback applies: drop `MENU_ART_START_GAME` and `MENU_ART_LOAD_GAME` and render those two rows with the engine font in Task 5, which costs the title screen its chrome lettering and nothing else.

`preview_title.png` is a review artifact — delete it before committing.

- [ ] **Step 5: Regenerate the packed assets**

Run: `python tools/mkmenuart.py`

Confirm the string arrays are no longer all zero:
```sh
python - <<'PY'
import re
src = open("saturn/src/menu_art.cxx").read()
for name in ("s_startGameBits", "s_loadGameBits"):
    body = re.search(r"%s\[\d+\] = \{(.*?)\};" % name, src, re.S).group(1)
    vals = [int(v, 16) for v in re.findall(r"0x([0-9A-F]{2})", body)]
    print(name, "bytes", len(vals), "non-zero", sum(1 for v in vals if v))
PY
```
Expected: 570 bytes each (38 × 15) and a non-zero count in the hundreds. A
non-zero count of 0 means the PNGs did not regenerate.

- [ ] **Step 6: Run the host suite**

Run: `sh saturn/tests/run_tests.sh`
Expected: `all suites passed`.

- [ ] **Step 7: Commit**

```bash
rm -f saturn/art/preview_title.png
git add tools/mkchromestrings.py saturn/art/chrome_start_game.png \
        saturn/art/chrome_load_game.png saturn/src/menu_art.cxx
git commit -m "Author the chrome START GAME and LOAD GAME strings from the reference letterforms"
```

---

### Task 4: Base-index text and the frozen-frame remap

**Files:**
- Modify: `saturn/src/menu_draw.h`, `saturn/src/menu_draw.cxx`, `saturn/tests/test_menu_draw.cxx`, `saturn/tests/test_menu_art.cxx`, `saturn/tests/run_tests.sh`

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces: `menuDrawChar(page, font, cellX, y, base, c)` and
  `menuDrawText(page, font, cellX, y, base, s)` — the parameter is now a ramp
  base, and the glyph renders at `base + 2`.
  `void menuFreezeRemap(uint8_t *page, const uint8_t *srcPalette)`.
  `menuDrawDimPalette` is removed.

- [ ] **Step 1: Write the failing tests**

Append to `saturn/tests/test_menu_art.cxx`, before `main`:

```cpp
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
```

Add the calls in `main`:

```cpp
    test_freeze_remap_bounds_every_nibble();
    test_freeze_remap_uniform_palette_is_uniform();
    test_freeze_remap_darkest_becomes_black();
    test_freeze_remap_touches_both_nibbles();
    test_text_renders_at_base_plus_two();
```

- [ ] **Step 2: Run to verify it fails**

Run: `sh saturn/tests/run_tests.sh`
Expected: FAIL — `menuFreezeRemap` was not declared in this scope.

- [ ] **Step 3: Update the header**

In `saturn/src/menu_draw.h`, replace the `menuDrawChar`, `menuDrawText` and
`menuDrawDimPalette` declarations with:

```cpp
/*----------------------
 | menuDrawChar
 | Description: Draws one glyph, packing two pixels per byte the same way
 |   Video::drawChar does. cellX is in 8-pixel cells and y is in scanlines, not
 |   pixels; out-of-range cells or rows are silently dropped.
 | Author: suinevere
 | Params: page -- MENU_PAGE_SIZE bytes; font -- 8 bytes per glyph starting at
 |         ' '; cellX -- column, 0..39; y -- row in scanlines, 0..192;
 |         base -- ramp base, the glyph renders at base + 2; c -- the glyph
 | Returns: N/A
 ----------------------*/
void menuDrawChar(uint8_t *page, const uint8_t *font, int cellX, int y, uint8_t base, char c);

/*----------------------
 | menuDrawText
 | Description: Draws a string one glyph per cell, left to right, stopping at
 |   cell 40 rather than wrapping.
 | Author: suinevere
 | Params: page -- MENU_PAGE_SIZE bytes; font -- glyph table, see menuDrawChar;
 |         cellX -- starting column; y -- row in scanlines; base -- ramp base;
 |         s -- NUL-terminated string
 | Returns: N/A
 ----------------------*/
void menuDrawText(uint8_t *page, const uint8_t *font, int cellX, int y, uint8_t base, const char *s);

/*----------------------
 | menuFreezeRemap
 | Description: Collapses a frozen game frame onto palette indices 0..3 by
 |   luminance, in place. This is what frees entries 4..15 for artwork while a
 |   menu sits over a paused game: the backdrop stops needing the game's own
 |   sixteen colours. Colours in the darkest quarter map to 0, so a dark scene
 |   goes true black rather than reading as noise behind the panel.
 | Author: suinevere
 | Params: page -- MENU_PAGE_SIZE bytes, remapped in place; srcPalette --
 |         32 bytes, the game palette the page was drawn against
 | Returns: N/A
 ----------------------*/
void menuFreezeRemap(uint8_t *page, const uint8_t *srcPalette);
```

- [ ] **Step 4: Update the implementation**

In `saturn/src/menu_draw.cxx`, change `menuDrawChar`'s signature to take
`uint8_t base` and derive the colour once at the top of the body:

```cpp
void menuDrawChar(uint8_t *page, const uint8_t *font, int cellX, int y, uint8_t base, char c)
{
	if (cellX < 0 || cellX > 39 || y < 0 || y > 192) {
		return;
	}

	const uint8_t color = (uint8_t)(base + 2);
	const uint8_t *ft = font + (c - ' ') * 8;
	uint8_t *p = page + cellX * 4 + y * MENU_PAGE_PITCH;
```

The rest of the body is unchanged. Change `menuDrawText` to match:

```cpp
void menuDrawText(uint8_t *page, const uint8_t *font, int cellX, int y, uint8_t base, const char *s)
{
	int cx = cellX;
	while (*s != '\0' && cx < 40) {
		menuDrawChar(page, font, cx, y, base, *s);
		++cx;
		++s;
	}
}
```

Delete `menuDrawDimPalette` entirely and add, with the header block from Step 3:

```cpp
void menuFreezeRemap(uint8_t *page, const uint8_t *srcPalette)
{
	uint8_t map[16];
	for (int i = 0; i < 16; ++i) {
		const uint8_t b0 = srcPalette[i * 2];
		const uint8_t b1 = srcPalette[i * 2 + 1];
		const int r = b0 & 0x0F;
		const int g = (b1 & 0xF0) >> 4;
		const int b = b1 & 0x0F;
		const int y = (r * 77 + g * 151 + b * 28) >> 8;
		map[i] = (uint8_t)(y >> 2);
	}

	uint8_t lut8[256];
	for (int i = 0; i < 256; ++i) {
		lut8[i] = (uint8_t)((map[i >> 4] << 4) | map[i & 0x0F]);
	}

	for (int i = 0; i < MENU_PAGE_SIZE; ++i) {
		page[i] = lut8[page[i]];
	}
}
```

- [ ] **Step 5: Update the existing menu_draw tests**

In `saturn/tests/test_menu_draw.cxx`:

1. Delete every test whose name mentions the dim palette, and its call in `main`.
2. Update the file header block's Description to drop "and the palette dim".
3. Every surviving `menuDrawChar`/`menuDrawText` call passes a literal that used
   to mean a colour and now means a base. Leave the argument alone and raise the
   expected value by 2, so the test documents the new meaning. For example a test
   that reads:

```cpp
    menuDrawText(g_page, g_font, 0, 0, 7, "A");
    CHECK_EQ(pixelAt(0, 0), 7);
```

becomes:

```cpp
    menuDrawText(g_page, g_font, 0, 0, 7, "A");
    CHECK_EQ(pixelAt(0, 0), 9);
```

Work through the file mechanically: every `CHECK_EQ` comparing a pixel or a
packed byte against the value passed as the fifth argument needs that constant
raised by 2. A packed byte like `0x77` becomes `0x99`.

- [ ] **Step 6: Run to verify it passes**

Run: `sh saturn/tests/run_tests.sh`
Expected: `all suites passed`.

- [ ] **Step 7: Commit**

```bash
git add saturn/src/menu_draw.h saturn/src/menu_draw.cxx \
        saturn/tests/test_menu_draw.cxx saturn/tests/test_menu_art.cxx
git commit -m "Draw menu text from a ramp base and remap the frozen frame instead of dimming the palette"
```

---

### Task 5: Title screen artwork

**Files:**
- Modify: `saturn/src/menu.h`, `saturn/src/menu.cxx`

**Interfaces:**
- Consumes: `MENU_ART_LOGO`, `MENU_ART_START_GAME`, `MENU_ART_LOAD_GAME`,
  `MENU_ART_PALETTE` (Task 2); `menuBlit4bpp`, `menuBlit2bpp` (Task 1);
  `menuDrawText` with a base (Task 4).
- Produces: `menuDrawTitleScreen` now blits artwork. `MENU_BASE_DIM = 8`,
  `MENU_BASE_SEL = 12`, `MENU_COL_PANEL = 0`, `MENU_COL_BORDER = 7` replace
  `MENU_COL_TEXT`.

**Layout, from the spec:** logo at x=15, y=30. `START GAME` at y=128,
`LOAD GAME` at y=152, both at x=84 because each canvas is 152 wide and centred.
No `>` cursor on the title screen — selection is the base index and the pulse.

- [ ] **Step 1: Replace the colour constants**

In `saturn/src/menu.cxx`, replace the `MENU_COL_TEXT` / `MENU_COL_PANEL` block with:

```cpp
/*----------------------
 | MENU_BASE_DIM / MENU_BASE_SEL / MENU_COL_PANEL / MENU_COL_BORDER
 | Description: Palette indices the menu draws with. Selection is a base index
 |   rather than a second colour: the same artwork renders unselected at base 8
 |   and selected at base 12, and only entries 12..14 are rewritten to pulse, so
 |   the logo at base 4 stays still.
 | Author: suinevere
 ----------------------*/
enum {
	MENU_BASE_DIM    = 8,
	MENU_BASE_SEL    = 12,
	MENU_COL_PANEL   = 0,
	MENU_COL_BORDER  = 7
};
```

- [ ] **Step 2: Delete the placeholder palette**

Remove `s_titlePalette` and its header block entirely — `MENU_ART_PALETTE`
replaces it.

- [ ] **Step 3: Add the includes**

At the top of `saturn/src/menu.cxx`, after `#include "menu_draw.h"`:

```cpp
#include "menu_blit.h"
#include "menu_art.h"
```

Update the file's header block `Dependencies:` line to list `menu_blit.h` and
`menu_art.h`.

- [ ] **Step 4: Rewrite the title screen**

Replace `menuDrawTitleScreen` with:

```cpp
/*----------------------
 | menuDrawTitleScreen
 | Description: Paints the title card: the wordmark and the two entry points.
 |   The selected row is drawn from the strobe ramp rather than marked with a
 |   cursor glyph, which is how the Mega Drive screen shows it.
 | Author: suinevere
 | Params: page -- compositing page; st -- state, for the cursor position
 | Returns: N/A
 ----------------------*/
static void menuDrawTitleScreen(uint8_t *page, const MenuState *st)
{
	memset(page, 0, MENU_PAGE_SIZE);
	menuBlit4bpp(page, &MENU_ART_LOGO, 15, 30);
	menuBlit2bpp(page, &MENU_ART_START_GAME, 84, 128,
	             st->cursor == 0 ? MENU_BASE_SEL : MENU_BASE_DIM);
	menuBlit2bpp(page, &MENU_ART_LOAD_GAME, 84, 152,
	             st->cursor == 1 ? MENU_BASE_SEL : MENU_BASE_DIM);
}
```

- [ ] **Step 5: Point the remaining screens at the new bases**

In `menuDrawPauseScreen`, `menuDrawSlotScreen` and `menuDrawConfirmScreen`,
replace every `MENU_COL_TEXT` argument to `menuDrawText` with `MENU_BASE_DIM`,
except the row that carries the cursor, which takes `MENU_BASE_SEL`. Concretely,
in `menuDrawPauseScreen`:

```cpp
	menuDrawFill(page, 80, 48, 168, 96, MENU_COL_PANEL);
	menuDrawText(page, font, 13, 60, st->cursor == 0 ? MENU_BASE_SEL : MENU_BASE_DIM, "RESUME");
	menuDrawText(page, font, 13, 76, st->cursor == 1 ? MENU_BASE_SEL : MENU_BASE_DIM, "SAVE GAME");
	menuDrawText(page, font, 13, 92, st->cursor == 2 ? MENU_BASE_SEL : MENU_BASE_DIM, "LOAD GAME");
	menuDrawText(page, font, 13, 108, st->cursor == 3 ? MENU_BASE_SEL : MENU_BASE_DIM, "RETURN TO MENU");
	menuDrawText(page, font, 11, 60 + st->cursor * 16, MENU_BASE_SEL, ">");
```

In `menuDrawSlotScreen`, the slot rows take `i == st->slotCursor ? MENU_BASE_SEL
: MENU_BASE_DIM`; the title, device row, status line and hint line take
`MENU_BASE_DIM`; the `>` takes `MENU_BASE_SEL`. In `menuDrawConfirmScreen`, the
prompt lines take `MENU_BASE_DIM`, `YES` takes `st->confirmYes ? MENU_BASE_SEL :
MENU_BASE_DIM`, `NO` the inverse, and the `>` takes `MENU_BASE_SEL`.

- [ ] **Step 6: Install the artwork palette on the title screen**

In `Menu::runTitle`, replace `_sys->setPalette(s_titlePalette);` with:

```cpp
	_sys->setPalette(MENU_ART_PALETTE);
```

- [ ] **Step 7: Build for Saturn**

Run: `saturn/compile.bat debug`
Expected: builds clean, producing `saturn/BuildDrop/Another World (USA).iso`.

- [ ] **Step 8: Look at it**

Run: `saturn/run_with_mednafen.bat`
Expected: the title screen shows the chrome wordmark, `START GAME` bright and
`LOAD GAME` dark, and the D-pad swaps which one is bright.

- [ ] **Step 9: Run the host suite**

Run: `sh saturn/tests/run_tests.sh`
Expected: `all suites passed`.

- [ ] **Step 10: Commit**

```bash
git add saturn/src/menu.cxx saturn/src/menu.h
git commit -m "Draw the title card from the extracted wordmark and chrome strings"
```

---

### Task 6: Strobe and lightning

**Files:**
- Modify: `saturn/src/menu.h`, `saturn/src/menu.cxx`

**Interfaces:**
- Consumes: `MENU_ART_STROBE`, `MENU_ART_BOLT`, `MENU_ART_BOLT_COUNT`,
  `MENU_ART_PALETTE`, `MENU_ART_LOGO`.
- Produces: `Menu::_frame`, `Menu::_rng`, `Menu::_boltTimer`, `Menu::_boltFrame`,
  `Menu::_boltIndex`, and the file-local helpers `menuStrobeLevel`,
  `menuNextRandom`, `menuTitleAnimate`.

**Behaviour, from the spec.** The strobe walks `MENU_ART_STROBE` as a triangle
wave over 90 frames. Lightning idles a random 120–300 frames, then draws a bolt
for three frames while the palette steps flash → half → normal, and erases on
frame 3. The flash overrides the strobe for its three frames. Every bolt overlaps
the wordmark, so erasing fills the bolt rectangle with index 0 and re-blits the
logo.

- [ ] **Step 1: Add the animation state**

In `saturn/src/menu.h`, add to `struct Menu` after `_repeatTimer`:

```cpp
	int _frame;
	uint16_t _rng;
	int _boltTimer;
	int _boltFrame;
	int _boltIndex;
```

Update the struct's header block Description to mention that it also owns the
title screen's animation counters.

- [ ] **Step 2: Initialise them**

In `Menu::init` in `saturn/src/menu.cxx`, after `_devicesProbed = false;`:

```cpp
	_frame = 0;
	_rng = 0xACE1;
	_boltTimer = 120;
	_boltFrame = -1;
	_boltIndex = 0;
```

- [ ] **Step 3: Add the helpers**

In `saturn/src/menu.cxx`, before `Menu::runTitle`:

```cpp
/*----------------------
 | menuNextRandom
 | Description: One step of a 16-bit LFSR. Local rather than libc's rand so the
 |   menu adds no dependency for the sake of bolt timing.
 | Author: suinevere
 | Params: state -- advanced in place
 | Returns: the new state
 ----------------------*/
static uint16_t menuNextRandom(uint16_t *state)
{
	uint16_t x = *state;
	x ^= (uint16_t)(x << 7);
	x ^= (uint16_t)(x >> 9);
	x ^= (uint16_t)(x << 8);
	*state = x;
	return x;
}

/*----------------------
 | menuBoltX
 | Description: Where each bolt is drawn. Bolt 1 is bolt 0 mirrored, so it hangs
 |   off the left edge; bolt 2 is shorter and sits between them.
 | Author: suinevere
 | Params: index -- 0..MENU_ART_BOLT_COUNT - 1
 | Returns: the left edge in pixels
 ----------------------*/
static int menuBoltX(int index)
{
	if (index == 0) {
		return 268;
	}
	return (index == 1) ? 6 : 90;
}

/*----------------------
 | menuStrobeLevel
 | Description: Maps a frame counter to a strobe table row, walking up and back
 |   down so the pulse has no seam.
 | Author: suinevere
 | Params: frame -- free-running frame counter
 | Returns: a row index, 0..MENU_ART_STROBE_LEVELS - 1
 ----------------------*/
static int menuStrobeLevel(int frame)
{
	const int span = MENU_ART_STROBE_LEVELS * 2 - 2;
	int p = frame % span;
	if (p < 0) {
		p += span;
	}
	return (p < MENU_ART_STROBE_LEVELS) ? p : span - p;
}

/*----------------------
 | menuTitlePalette
 | Description: Builds the title screen's palette for one frame: the artwork
 |   entries as authored, entries 12..14 from the strobe table, and every entry
 |   from 1 to 14 pushed toward white while a bolt is on screen. The flash wins
 |   over the strobe for the three frames it lasts.
 | Author: suinevere
 | Params: out -- 32 bytes; frame -- frame counter, for the strobe phase;
 |         boltFrame -- 0, 1 or 2 while a bolt is lit, negative otherwise
 | Returns: N/A
 ----------------------*/
static void menuTitlePalette(uint8_t *out, int frame, int boltFrame)
{
	memcpy(out, MENU_ART_PALETTE, 32);

	const uint8_t *row = MENU_ART_STROBE[menuStrobeLevel(frame)];
	for (int i = 0; i < 3; ++i) {
		out[(12 + i) * 2]     = row[i * 2];
		out[(12 + i) * 2 + 1] = row[i * 2 + 1];
	}

	if (boltFrame < 0 || boltFrame > 1) {
		return;
	}

	const int lift = (boltFrame == 0) ? 8 : 4;
	for (int i = 1; i < 15; ++i) {
		int r = (out[i * 2] & 0x0F) + lift;
		int g = ((out[i * 2 + 1] & 0xF0) >> 4) + lift;
		int b = (out[i * 2 + 1] & 0x0F) + lift;
		if (r > 15) r = 15;
		if (g > 15) g = 15;
		if (b > 15) b = 15;
		out[i * 2]     = (uint8_t)r;
		out[i * 2 + 1] = (uint8_t)((g << 4) | b);
	}
}
```

- [ ] **Step 4: Drive the bolt from runTitle**

Add to `Menu` in `menu.h` a method declaration with its header block:

```cpp
	/*----------------------
	 | Menu::titleAnimate
	 | Description: Advances the strobe and the lightning by one frame and
	 |   installs the palette they imply. Draws or erases the bolt as its three
	 |   frames come and go.
	 | Author: suinevere
	 | Params: N/A
	 | Returns: N/A
	 ----------------------*/
	void titleAnimate();
```

And in `menu.cxx`, before `Menu::runTitle`:

```cpp
void Menu::titleAnimate()
{
	_frame++;

	if (_boltFrame >= 0) {
		_boltFrame++;
		if (_boltFrame >= 3) {
			const MenuArt *b = &MENU_ART_BOLT[_boltIndex];
			menuDrawFill(_page, menuBoltX(_boltIndex), 0, b->w, b->h, MENU_COL_PANEL);
			menuBlit4bpp(_page, &MENU_ART_LOGO, 15, 30);
			_boltFrame = -1;
			_boltTimer = 120 + (int)(menuNextRandom(&_rng) % 181);
		}
	} else {
		_boltTimer--;
		if (_boltTimer <= 0) {
			_boltIndex = (int)(menuNextRandom(&_rng) % MENU_ART_BOLT_COUNT);
			_boltFrame = 0;
			menuBlit4bpp(_page, &MENU_ART_BOLT[_boltIndex], menuBoltX(_boltIndex), 0);
		}
	}

	uint8_t pal[32];
	menuTitlePalette(pal, _frame, _boltFrame);
	_sys->setPalette(pal);
}
```

- [ ] **Step 5: Call it from the title loop**

In `Menu::runTitle`, the loop currently ends with `menuRenderFrame(...)`. The
bolt is drawn into `_page` by `titleAnimate` but `menuDrawTitleScreen` clears the
page every frame, so the order matters: call `titleAnimate` **after** the render.
Replace the tail of the loop body with:

```cpp
		menuRenderFrame(_page, _sys, &_st, _statusError, false, 0);
		titleAnimate();
	}
```

Because `menuRenderFrame` calls `sys->updateDisplay`, the bolt drawn by
`titleAnimate` appears on the following frame — which is correct, and keeps the
erase-and-relogo path from fighting the full clear.

- [ ] **Step 6: Build**

Run: `saturn/compile.bat debug`
Expected: builds clean.

- [ ] **Step 7: Look at it**

Run: `saturn/run_with_mednafen.bat`
Expected: the selected row breathes over roughly 1.5 s; every 2–5 s a bolt appears
for three frames with a whole-screen brighten, then vanishes leaving the wordmark
intact. Watch several strikes to confirm all three bolt positions appear and none
leaves a hole in the logo.

- [ ] **Step 8: Run the host suite**

Run: `sh saturn/tests/run_tests.sh`
Expected: `all suites passed`.

- [ ] **Step 9: Commit**

```bash
git add saturn/src/menu.h saturn/src/menu.cxx
git commit -m "Pulse the selected title row and strike lightning on a random timer"
```

---

### Task 7: Pause and save screens

**Files:**
- Modify: `saturn/src/menu.h`, `saturn/src/menu.cxx`

**Interfaces:**
- Consumes: `menuFreezeRemap` (Task 4), `MENU_ART_PALETTE` (Task 2), the base
  constants (Task 5).
- Produces: `Menu::_dimPal` removed; `runPause` installs `MENU_ART_PALETTE`.

- [ ] **Step 1: Drop the dim palette member**

In `saturn/src/menu.h`, delete `uint8_t _dimPal[32];`. `_savedPal` stays — the
game palette must still be restored on the way out.

In `Menu::init`, delete `memset(_dimPal, 0, sizeof(_dimPal));`.

- [ ] **Step 2: Remap the frozen frame on pause entry**

In `Menu::runPause`, replace:

```cpp
	sat_video_get_palette(_savedPal);
	menuDrawDimPalette(_savedPal, _dimPal, MENU_COL_TEXT);
	_sys->setPalette(_dimPal);
```

with:

```cpp
	sat_video_get_palette(_savedPal);
	memcpy(_page, backdrop, MENU_PAGE_SIZE);
	menuFreezeRemap(_page, _savedPal);
	_sys->setPalette(MENU_ART_PALETTE);
```

- [ ] **Step 3: Composite from the remapped page, not the live backdrop**

`menuRenderFrame` re-copies `backdrop` over `_page` on every overlay frame, which
would undo the remap. Change the overlay branch to remap what it copies. In
`menuRenderFrame`, replace:

```cpp
	if (overlay) {
		memcpy(page, backdrop, MENU_PAGE_SIZE);
	}
```

with:

```cpp
	if (overlay) {
		memcpy(page, backdrop, MENU_PAGE_SIZE);
		menuFreezeRemap(page, freezePal);
	}
```

and add `const uint8_t *freezePal` as the last parameter of `menuRenderFrame`,
documented in its header block as "the game palette the backdrop was drawn
against, or NULL when not overlaying". Pass `_savedPal` from `runPause` and `0`
from `runTitle`.

- [ ] **Step 4: Draw the panel border**

In `menuDrawPauseScreen`, `menuDrawSlotScreen` and `menuDrawConfirmScreen`, draw
a border by filling one rectangle in `MENU_COL_BORDER` and insetting the panel
fill by two pixels. For the pause panel:

```cpp
	menuDrawFill(page, 80, 48, 168, 96, MENU_COL_BORDER);
	menuDrawFill(page, 82, 50, 164, 92, MENU_COL_PANEL);
```

For the slot panel in `menuDrawSlotScreen`:

```cpp
	menuDrawFill(page, 24, 16, 272, 168, MENU_COL_BORDER);
	menuDrawFill(page, 26, 18, 268, 164, MENU_COL_PANEL);
```

For the confirm panel in `menuDrawConfirmScreen`:

```cpp
	menuDrawFill(page, 40, 64, 240, 72, MENU_COL_BORDER);
	menuDrawFill(page, 42, 66, 236, 68, MENU_COL_PANEL);
```

- [ ] **Step 5: Build**

Run: `saturn/compile.bat debug`
Expected: builds clean.

- [ ] **Step 6: Look at it**

Run: `saturn/run_with_mednafen.bat`
Start a game, pause it, and check: the frozen frame behind the panel is a dark
monochrome freeze rather than the game's colours; the panel has a teal border;
the selected row pulses and the others sit dark; opening Save Game and moving
between slots moves the bright row. Resume and confirm the game's own colours
come back.

- [ ] **Step 7: Run the host suite**

Run: `sh saturn/tests/run_tests.sh`
Expected: `all suites passed`.

- [ ] **Step 8: Commit**

```bash
git add saturn/src/menu.h saturn/src/menu.cxx
git commit -m "Remap the frozen frame so the pause and slot screens share the title palette"
```

---

## Notes for the reviewer

- **The visual gate in Task 3 Step 4 is the one that matters.** Four of the ten
  letters in `START GAME` / `LOAD GAME` are drawn rather than extracted. If they
  do not sit convincingly beside the cut ones, take the spec's fallback — engine
  font in the base-8/base-12 ramps for those two rows — rather than shipping
  lettering that reads as two different faces.
- **Task 6 Step 5 has a subtle ordering requirement.** `menuDrawTitleScreen`
  clears the whole page, so `titleAnimate` must run after the render, not before,
  or the bolt is erased the instant it is drawn.
- **Task 7 Step 3 is easy to miss.** `menuRenderFrame` re-copies the backdrop
  every overlay frame; without the remap moving inside that copy, the frozen
  frame reverts to the game's palette indices on the second frame of the pause
  menu and the panel is drawn over a colour-corrupted backdrop.
