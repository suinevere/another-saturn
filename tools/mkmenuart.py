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


BACKDROP_SLOTS = [(0, (0, 0, 0)), (4, (0x00, 0x48, 0x49)), (5, (0x25, 0x6C, 0x6E)),
                  (6, (0x49, 0x90, 0x93)), (15, (0xFF, 0xFF, 0xFF))]

BACKDROP_FRAME = 382  # last frame within 3% of peak ink before the GIF fades to black for its loop


def build_backdrop():
    """The opening's last played frame (BACKDROP_FRAME), quantised onto the slots the logo already uses."""
    from PIL import ImageSequence
    src = Image.open(os.path.join(ROOT, "images", "genesis-opening.gif"))
    target = None
    for i, fr in enumerate(ImageSequence.Iterator(src)):
        if i == BACKDROP_FRAME:
            target = fr.convert("RGB")
            break
    img = target.resize((320, 240), Image.BOX).crop((0, 0, 320, 200))

    cols = [c for _, c in BACKDROP_SLOTS]
    q = quantise(img, cols)
    px = q.load()
    idx = {c: i for i, c in BACKDROP_SLOTS}

    pitch = 160
    buf = bytearray(pitch * 200)
    for y in range(200):
        row = y * pitch
        for x in range(0, 320, 2):
            a = idx[px[x, y]]
            b = idx[px[x + 1, y]]
            buf[row + (x >> 1)] = ((a & 0xF) << 4) | (b & 0xF)
    return bytes(buf)


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
    backdrop = build_backdrop()
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

        f.write("const uint8_t MENU_ART_STROBE[MENU_ART_STROBE_LEVELS][6] = {\n")
        for row in strobe:
            f.write("\t{ " + " ".join("0x%02X," % v for v in row) + " },\n")
        f.write("};\n")

    print("wrote", OUT)


if __name__ == "__main__":
    main()
