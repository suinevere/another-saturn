"""
mkchromestrings.py
Description: Builds the two chrome menu strings as PNGs. Letters that appear in
  START or PASSWORD are cut from images/genesis.png; G, M, E and L appear in
  neither and are rasterised from solid block silhouettes, hollowed into the
  same double-line stroke style as the cut letters.
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


def rect_px(x0, y0, x1, y1):
    """Solid pixel set for an axis-aligned rectangle, corners inclusive."""
    return {(x, y) for x in range(x0, x1 + 1) for y in range(y0, y1 + 1)}


def thick_line_px(x0, y0, x1, y1, half_thick, w, h):
    """Solid pixel set for a flat-capped line of the given half-thickness.

    Used only for M's two diagonals: a distance-to-segment test with square
    stamps at each sampled point (the naive approach) fattens diagonal
    corners and rounded caps bulge past a shared endpoint, so this measures
    true perpendicular distance and clips the ends flat instead of round.
    """
    pts = set()
    dx, dy = x1 - x0, y1 - y0
    length_sq = dx * dx + dy * dy
    minx = max(0, int(min(x0, x1) - half_thick - 1))
    maxx = min(w - 1, int(max(x0, x1) + half_thick + 1))
    miny = max(0, int(min(y0, y1) - half_thick - 1))
    maxy = min(h - 1, int(max(y0, y1) + half_thick + 1))
    for y in range(miny, maxy + 1):
        for x in range(minx, maxx + 1):
            if length_sq == 0:
                continue
            t = ((x - x0) * dx + (y - y0) * dy) / length_sq
            if t < 0 or t > 1:
                continue
            projx, projy = x0 + t * dx, y0 + t * dy
            if (x - projx) ** 2 + (y - projy) ** 2 <= half_thick * half_thick:
                pts.add((x, y))
    return pts


# Solid silhouettes for the four letters the capture does not contain, in an
# 11x15 cell (13 wide for M), built from the same ~4px bar thickness as the
# cut letters' hollow tube strokes. draw_glyph fills these solid then keeps
# only the boundary, so a straight stem reads as two parallel rim lines with
# a gap between, matching T and R rather than a filled bar.
def glyph_silhouette(letter):
    if letter == "E":
        return (rect_px(0, 0, 3, 14) | rect_px(0, 0, 10, 3)
                 | rect_px(0, 5, 7, 8) | rect_px(0, 11, 10, 14))
    if letter == "L":
        return rect_px(0, 0, 3, 14) | rect_px(0, 11, 10, 14)
    if letter == "G":
        # A C-bracket (spine, top, bottom) plus a hook that reaches in from
        # the bottom-right without touching the spine, so the opening reads
        # as a gap rather than a third bar off the spine (which reads as E).
        return (rect_px(0, 0, 3, 14) | rect_px(0, 0, 10, 3) | rect_px(0, 11, 10, 14)
                 | rect_px(7, 8, 10, 14) | rect_px(5, 8, 10, 10))
    if letter == "M":
        legs = rect_px(0, 0, 3, 14) | rect_px(9, 0, 12, 14)
        peak = (thick_line_px(0, 0, 6, 6, 1.5, 13, CELL_H)
                 | thick_line_px(12, 0, 6, 6, 1.5, 13, CELL_H))
        return legs | peak
    raise KeyError(letter)


GLYPH_WIDTH = {"E": 11, "L": 11, "G": 11, "M": 13}


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


MIN_HOLE_AREA = 6


def background_components(w, h, filled):
    """Connected background regions, each tagged with whether it reaches the border.

    A straight stroke's own hollow interior is one component; so is a
    genuine enclosed counter (a real design hole, like G's hook cavity).
    Reachability from the border is what tells outside background apart
    from an enclosed one -- both get traced into the outline, unlike a
    stray one- or two-cell pocket sealed inside a thick junction, which is
    a quantisation artefact rather than a real hole and is dropped by the
    caller's area threshold instead of being rung in RIM.
    """
    background = {(x, y) for x in range(w) for y in range(h) if (x, y) not in filled}
    seen = set()
    comps = []
    for start in background:
        if start in seen:
            continue
        comp = set()
        stack = [start]
        seen.add(start)
        while stack:
            x, y = stack.pop()
            comp.add((x, y))
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                n = (x + dx, y + dy)
                if n in background and n not in seen:
                    seen.add(n)
                    stack.append(n)
        touches_border = any(x in (0, w - 1) or y in (0, h - 1) for x, y in comp)
        comps.append((comp, touches_border))
    return comps


def draw_glyph(letter):
    """Rasterise a solid silhouette as a hollow outline, matching the capture.

    The cut letters (T, R, O, D...) are not filled bars: each stroke is a
    solid block that is then traced along its own boundary, leaving the
    interior hollow -- a straight stem shows as two parallel rim lines with
    a gap between, not a solid tube. Trace the boundary of every background
    region big enough to be a real hole (border-reachable, or an enclosed
    counter at least MIN_HOLE_AREA cells); smaller enclosed pockets are a
    stamping artefact and are left as plain interior instead of a ring.
    """
    w = GLYPH_WIDTH[letter]
    filled = glyph_silhouette(letter)
    real_bg = set()
    for comp, touches_border in background_components(w, CELL_H, filled):
        if touches_border or len(comp) >= MIN_HOLE_AREA:
            real_bg |= comp

    g = Image.new("RGB", (w, CELL_H), BG)
    px = g.load()
    for (x, y) in filled:
        touches_real_bg = any(
            not (0 <= x + dx < w and 0 <= y + dy < CELL_H) or (x + dx, y + dy) in real_bg
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)))
        if touches_real_bg:
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
