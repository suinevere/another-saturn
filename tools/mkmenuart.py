"""
mkmenuart.py
Description: Builds saturn/src/menu_art.cxx from the Mega Drive title capture
  and the authored chrome strings. Run from the repository root. The generated
  file is committed; the Saturn build never invokes Python.
Author: suinevere
Usage: python tools/mkmenuart.py
"""
import os
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "saturn", "src", "menu_art.cxx")
TITLE = os.path.join(ROOT, "tools", "assets", "png",
                     "Another World Title (Europe).png")

# Palette entries 4-6: the wordmark's own colours, straight from the capture.
LOGO_COLOURS = [(0x00, 0x48, 0x49), (0x25, 0x6C, 0x6E), (0x49, 0x90, 0x93)]

# (index, r, g, b) in 4-bit channels. Matches the spec's palette table.
PALETTE = [
    (0,  0,  0,  0), (1,  1,  1,  2), (2,  2,  2,  3), (3,  3,  3,  4),
    (4,  0,  4,  4), (5,  2,  6,  6), (6,  4,  9,  9), (7,  1,  4,  4),
    (8,  0,  2,  2), (9,  1,  3,  3), (10, 2,  4,  4), (11, 0,  0,  0),
    (12, 0, 10, 11), (13, 5, 13, 14), (14, 11, 15, 15), (15, 15, 15, 15),
]

# Slots the backdrop may use. 0, 4-6 and 15 it shares with the wordmark; 1-3
# and 11 are free on the title screen only -- 1-3 are the pause screen's freeze
# ramp, so the backdrop's values for them go in a title-only copy of the palette
# rather than displacing them everywhere.
BACKDROP_FIXED_SLOTS = [0, 4, 5, 6, 15]
BACKDROP_FREE_SLOTS = [1, 2, 3, 11]

# One NBG0 page, matching MENU_PAGE_* in saturn/src/menu_draw.h.
PAGE_W, PAGE_H, PAGE_PITCH = 320, 200, 160

# The Mega Drive frame is 320x224 and NBG0 is 320x200, so twelve rows come off
# each edge. Both are card border here: the wordmark spans rows 53-113 of the
# capture and lands at 41-101 on the page, well clear of the menu band.
TITLE_CROP_TOP = 12

# Channel sum at or below which a pixel is the card's black rather than art,
# and goes to slot 0. Capture noise sits a little above pure black.
DARK_SUM = 60

# The menu entries are blitted over the backdrop, so the rows they occupy are
# forced clear rather than trusted to be. Clearing here rather than in the
# player keeps drawing the title a straight memcpy.
BACKDROP_CLEAR_ROWS = (131, 165)


def quantise(im, colours):
    """Snap an RGB image onto an exact colour list, no dithering."""
    pal = Image.new("P", (1, 1))
    flat = []
    for c in colours:
        flat += list(c)
    flat += [0, 0, 0] * (256 - len(colours))
    pal.putpalette(flat)
    return im.convert("RGB").quantize(palette=pal, dither=Image.NONE).convert("RGB")


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


def emit_array(f, name, data):
    f.write("static const uint8_t %s[%d] = {\n" % (name, len(data)))
    for i in range(0, len(data), 12):
        f.write("\t" + " ".join("0x%02X," % b for b in data[i:i + 12]) + "\n")
    f.write("};\n\n")


def emit_public_array(f, name, data):
    f.write("const uint8_t %s[%d] = {\n" % (name, len(data)))
    for i in range(0, len(data), 12):
        f.write("\t" + " ".join("0x%02X," % b for b in data[i:i + 12]) + "\n")
    f.write("};\n\n")


def widen4(v):
    """A 4-bit channel as saturn_platform widens it on the way into CRAM."""
    f = (v << 1) | (v >> 3)
    return (f << 3) | (f >> 2)


def narrow8(c):
    """8-bit RGB back to the 4-bit triple the palette table stores."""
    return tuple(min(15, max(0, int(round(v / 255.0 * 15)))) for v in c)


def slot_rgb(index):
    """What palette slot `index` actually reaches the screen as."""
    _, r, g, b = PALETTE[index]
    return (widen4(r), widen4(g), widen4(b))


def nearest(colour, table):
    """Index into `table` of the entry closest to `colour` in RGB."""
    best, at = None, 0
    for i, c in enumerate(table):
        d = (colour[0] - c[0]) ** 2 + (colour[1] - c[1]) ** 2 + (colour[2] - c[2]) ** 2
        if best is None or d < best:
            best, at = d, i
    return at


def fit_free_slots(hist, fixed):
    """k-means over the free slots with the shared ones frozen, on the 4-bit grid.

    The five shared slots leave wide luminance gaps -- nothing between the
    brightest wordmark teal and white, nothing between black and the darkest --
    and the frame lands in both, so nearest-colour snapping punched black
    through the chrome strokes' cores and the credit line's small glyphs.
    Seeds are spread evenly across the range rather than over the pixel
    distribution, which is 84% near-black and would put every seed in the dark.
    """
    n = len(BACKDROP_FREE_SLOTS)
    free = [tuple(widen4(v) for v in narrow8(((i + 1) * 255 // (n + 1),) * 3))
            for i in range(n)]

    for _ in range(32):
        sums = [[0, 0, 0, 0] for _ in range(n)]
        for colour, count in hist:
            at = nearest(colour, fixed + free)
            if at < len(fixed):
                continue
            s = sums[at - len(fixed)]
            for k in range(3):
                s[k] += colour[k] * count
            s[3] += count
        moved = list(free)
        for j, s in enumerate(sums):
            if s[3] == 0:
                continue
            mean = tuple(s[k] // s[3] for k in range(3))
            moved[j] = tuple(widen4(v) for v in narrow8(mean))
        if moved == free:
            break
        free = moved
    return free


def build_backdrop():
    """The Mega Drive title card on the nine slots the title screen can spare.

    A still capture rather than a frame lifted out of the opening video. The
    card the movie ends on is a fade, so pulling the backdrop from it meant
    reproducing the encoder's whole decimate-average-lift pipeline here just to
    land on one frame, and tying the title screen's colour to a stop frame
    number that the movie's own length could move underneath it.
    """
    img = Image.open(TITLE).convert("RGB").crop(
        (0, TITLE_CROP_TOP, PAGE_W, TITLE_CROP_TOP + PAGE_H))

    px = img.load()
    hist = {}
    for y in range(PAGE_H):
        for x in range(PAGE_W):
            c = px[x, y]
            if sum(c) > DARK_SUM:
                hist[c] = hist.get(c, 0) + 1
    hist = list(hist.items())

    fixed = [slot_rgb(i) for i in BACKDROP_FIXED_SLOTS]
    free = fit_free_slots(hist, fixed)

    slots = BACKDROP_FIXED_SLOTS + BACKDROP_FREE_SLOTS
    lut = {c: slots[nearest(c, fixed + free)] for c, _ in hist}

    def at(x, y):
        c = px[x, y]
        return lut[c] if sum(c) > DARK_SUM else 0

    buf = bytearray(PAGE_PITCH * PAGE_H)
    for y in range(PAGE_H):
        if BACKDROP_CLEAR_ROWS[0] <= y < BACKDROP_CLEAR_ROWS[1]:
            continue
        row = y * PAGE_PITCH
        for x in range(0, PAGE_W, 2):
            buf[row + (x >> 1)] = ((at(x, y) & 0xF) << 4) | (at(x + 1, y) & 0xF)
    return bytes(buf), dict(zip(BACKDROP_FREE_SLOTS, (narrow8(c) for c in free)))


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


def main():
    backdrop, titleRamp = build_backdrop()
    strings = build_strings()

    with open(OUT, "w") as f:
        f.write("/*----------------------\n")
        f.write(" | menu_art.cxx\n")
        f.write(" | Description: Generated by tools/mkmenuart.py. Do not edit by hand:\n")
        f.write(" |   change the source art or the tool and regenerate.\n")
        f.write(" | Author: suinevere\n")
        f.write(" | Dependencies: menu_art.h\n")
        f.write(" ----------------------*/\n")
        f.write('#include "menu_art.h"\n\n')

        emit_array(f, "s_startGameBits", strings[0][0])
        emit_array(f, "s_loadGameBits", strings[1][0])
        emit_public_array(f, "MENU_ART_TITLE_BACKDROP", backdrop)

        f.write("const MenuArt MENU_ART_START_GAME = { s_startGameBits, %d, %d };\n"
                % (strings[0][1], strings[0][2]))
        f.write("const MenuArt MENU_ART_LOAD_GAME = { s_loadGameBits, %d, %d };\n\n"
                % (strings[1][1], strings[1][2]))

        f.write("const uint8_t MENU_ART_PALETTE[32] = {\n")
        for _, r, g, b in PALETTE:
            f.write("\t0x%02X, 0x%02X,\n" % (r, (g << 4) | b))
        f.write("};\n\n")

        f.write("const uint8_t MENU_ART_TITLE_PALETTE[32] = {\n")
        for i, r, g, b in PALETTE:
            if i in titleRamp:
                r, g, b = titleRamp[i]
            f.write("\t0x%02X, 0x%02X,\n" % (r, (g << 4) | b))
        f.write("};\n")

    print("wrote", OUT)


if __name__ == "__main__":
    main()
