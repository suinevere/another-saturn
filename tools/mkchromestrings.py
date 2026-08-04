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

# Stroke skeletons for the four letters the capture does not contain, as
# centre-line segments in an 11x15 cell (13 wide for M). draw_glyph fills
# each segment solid then keeps only its boundary, so these read as hollow
# strokes like the cut letters rather than filled bars.
STROKES = {
    "E": (11, [((1, 1), (1, 13)), ((1, 1), (9, 1)),
               ((1, 7), (7, 7)), ((1, 13), (9, 13))]),
    "L": (11, [((1, 1), (1, 13)), ((1, 13), (9, 13))]),
    "G": (11, [((1, 1), (9, 1)), ((1, 1), (1, 13)), ((1, 13), (9, 13)),
               ((9, 7), (9, 13)), ((5, 7), (9, 7))]),
    "M": (13, [((1, 1), (1, 13)), ((11, 1), (11, 13)),
               ((1, 1), (6, 7)), ((11, 1), (6, 7))]),
}


PALETTE4 = (BG, DARK, MID, RIM)


def snap(colour):
    """Quantise a capture pixel onto the four logo colours by nearest match.

    START is the capture's selected menu row, so its rim strobes toward
    near-white; snapping folds that animation state back to the plain RIM.
    """
    if colour in PALETTE4:
        return colour
    return min(PALETTE4, key=lambda c: sum((a - b) ** 2 for a, b in zip(colour, c)))


def cut_glyph(src, letter):
    x0, x1, top, pad = CUTS[letter]
    g = Image.new("RGB", (x1 - x0 + 1, CELL_H), BG)
    band = src.crop((x0, top, x1 + 1, top + CELL_H - pad)).convert("RGB")
    band.putdata([snap(c) for c in band.get_flattened_data()])
    g.paste(band, (0, pad))
    return g


STROKE_HALF_WIDTH = 2


def draw_glyph(letter):
    """Rasterise a stroke skeleton as a hollow outline, matching the capture.

    The cut letters (T, R, O, D...) are not filled bars: each stroke is a
    solid block that is then traced along its own boundary, leaving the
    interior hollow -- a straight stem shows as two parallel rim lines with
    a gap between, not a solid tube. Fill each segment solid first, then
    keep only the pixels that touch background.
    """
    w, segs = STROKES[letter]
    filled = set()
    for (ax, ay), (bx, by) in segs:
        steps = max(abs(bx - ax), abs(by - ay))
        for s in range(steps + 1):
            cx = ax + (bx - ax) * s // steps
            cy = ay + (by - ay) * s // steps
            for dx in range(-STROKE_HALF_WIDTH, STROKE_HALF_WIDTH + 1):
                for dy in range(-STROKE_HALF_WIDTH, STROKE_HALF_WIDTH + 1):
                    x, y = cx + dx, cy + dy
                    if 0 <= x < w and 0 <= y < CELL_H:
                        filled.add((x, y))

    g = Image.new("RGB", (w, CELL_H), BG)
    px = g.load()
    for (x, y) in filled:
        touches_bg = any((x + dx, y + dy) not in filled
                          for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)))
        if touches_bg:
            px[x, y] = RIM
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
