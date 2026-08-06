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
from opening_frames import LAST_FRAME

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

# Slots the backdrop may use. 0, 4-6 and 15 it shares with the wordmark and the
# bolts; 1-3 and 11 are free on the title screen only -- 1-3 are the pause
# screen's freeze ramp, so the backdrop's values for them go in a title-only
# copy of the palette rather than displacing them everywhere.
BACKDROP_FIXED_SLOTS = [0, 4, 5, 6, 15]
BACKDROP_FREE_SLOTS = [1, 2, 3, 11]

# The capture's credit block lands exactly where the menu entries go. Clearing
# it here rather than in the player keeps drawing the title a straight memcpy.
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
    """The opening's last card on the nine slots the title screen can spare.

    The frame comes from the opening encoder's own pipeline rather than a second
    copy of it, so what the title holds is byte-identical in tone to the last
    frame the intro played and the handoff is invisible. It also retires a
    duplicate frame matcher that was scoring on a mean difference and, because
    these frames are mostly unchanging black, pulling in twenty-two cards that
    were not the last one at all.
    """
    from mkopening import DARK_SUM, load_frames
    img = load_frames()[LAST_FRAME]

    px = img.load()
    hist = {}
    for y in range(200):
        for x in range(320):
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

    pitch = 160
    buf = bytearray(pitch * 200)
    for y in range(200):
        if BACKDROP_CLEAR_ROWS[0] <= y < BACKDROP_CLEAR_ROWS[1]:
            continue
        row = y * pitch
        for x in range(0, 320, 2):
            buf[row + (x >> 1)] = ((at(x, y) & 0xF) << 4) | (at(x + 1, y) & 0xF)
    return bytes(buf), dict(zip(BACKDROP_FREE_SLOTS, (narrow8(c) for c in free)))


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
    backdrop, titleRamp = build_backdrop()
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

        for i, b in enumerate(bolts):
            emit_array(f, "s_boltBits%d" % i, b[0])
        emit_array(f, "s_startGameBits", strings[0][0])
        emit_array(f, "s_loadGameBits", strings[1][0])
        emit_public_array(f, "MENU_ART_TITLE_BACKDROP", backdrop)

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

        f.write("const uint8_t MENU_ART_TITLE_PALETTE[32] = {\n")
        for i, r, g, b in PALETTE:
            if i in titleRamp:
                r, g, b = titleRamp[i]
            f.write("\t0x%02X, 0x%02X,\n" % (r, (g << 4) | b))
        f.write("};\n\n")

        f.write("const uint8_t MENU_ART_STROBE[MENU_ART_STROBE_LEVELS][6] = {\n")
        for row in strobe:
            f.write("\t{ " + " ".join("0x%02X," % v for v in row) + " },\n")
        f.write("};\n")

    print("wrote", OUT)


if __name__ == "__main__":
    main()
