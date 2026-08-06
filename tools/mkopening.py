"""
mkopening.py
Description: Builds saturn/cd/data/OPENING.BIN from images/genesis-opening.gif as
  run-length coded XOR deltas with a per-frame 16-colour palette. Run from the
  repository root. The output is git-ignored and regenerated on demand.
Author: suinevere
Usage: python tools/mkopening.py
"""
import os
import struct
from PIL import Image, ImageSequence
from opening_frames import LAST_FRAME

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "images", "genesis-opening.gif")
OUT = os.path.join(ROOT, "saturn", "cd", "data", "OPENING.BIN")

W, H = 320, 200
PAGE = (W // 2) * H          # 32000
NATIVE_H = 240               # 640x480 box-downsamples to 320x240
KEYFRAMES = None             # filled in once the frame count is known

# The wordmark's strokes are one 640x480 pixel wide, so the 2:1 reduction
# averages each with the black beside it and halves its brightness; without the
# lift, neighbouring stroke pixels land in different slots and the stroke comes
# out dashed. Applied after the reduction, and after pooling, which is linear.
GAMMA = 0.55

# The GIF is a capture and its dither noise survives the reduction as speckle
# along those strokes. A spatial filter removes it but softens the strokes with
# it -- measurably, a 3x3 median cost about 15% of the mean edge gradient. The
# source holds most cards still for several frames instead (the median
# consecutive-frame difference is 0.08 per byte), so the run is several samples
# of one card: averaging the run cancels the noise and touches no edge.
#
# The whole run gets that one average rather than each frame getting its own
# sliding window. A window that shifts by one every frame gives every page a
# slightly different average, so the deltas stop being empty and the stream
# doubles; sharing the average makes the pages inside a run identical, which
# costs two bytes each.
#
# A run boundary is the fraction of sampled bytes that moved by more than
# RUN_NOISE, not a mean difference. A mean is useless here: the frame is
# five-sixths unchanging black, so swapping the bolt for a different one barely
# shifts it -- at a mean threshold the whole intro collapsed into 19 distinct
# images. Counting changed samples instead is indifferent to how much of the
# frame is background. At these values the longest run is four frames, which is
# what the source actually holds a card for.
RUN_NOISE = 24
RUN_CHANGED = 0.0005
RUN_STRIDE = 7

# Median cut allocates by population, and these frames are five-sixths black, so
# it spends four to six of sixteen slots on duplicates of black and leaves the
# lit end of the ramp with almost nothing. Holding index 0 for the background and
# fitting the other fifteen to the ink only is what keeps the three lit states --
# blue, green, and the lightning flash -- distinct from each other.
#
# The sum is measured after the lift, which is why the number is high: the lift
# takes a raw channel of 8 to 38, so anything that looks like a low threshold
# admits the capture's whole noise floor and scatters it across the background as
# lit dots. This is the level at which the background goes properly black with
# the strokes still solid.
DARK_SUM = 220


def load_frames():
    """Box-downsample, average each run of frames showing one card, then lift.
    Averaging happens after the reduction because both are averages and the order
    does not matter; the lift is not linear, so it comes last."""
    im = Image.open(SRC)
    flat = []
    for i, fr in enumerate(ImageSequence.Iterator(im)):
        if i > LAST_FRAME:
            break
        rgb = fr.convert("RGB").resize((W, NATIVE_H), Image.BOX).crop((0, 0, W, H))
        flat.append(rgb.tobytes())

    at = list(range(0, len(flat[0]), RUN_STRIDE))
    runs = [[0]]
    for i in range(1, len(flat)):
        head = flat[runs[-1][0]]
        cur = flat[i]
        moved = sum(1 for k in at if abs(cur[k] - head[k]) > RUN_NOISE)
        if moved < RUN_CHANGED * len(at):
            runs[-1].append(i)
        else:
            runs.append([i])

    curve = [min(255, int(255.0 * (v / 255.0) ** GAMMA + 0.5)) for v in range(256)]
    out = [None] * len(flat)
    for run in runs:
        if len(run) == 1:
            mean = flat[run[0]]
        else:
            acc = [0] * len(flat[0])
            for i in run:
                d = flat[i]
                for k in range(len(acc)):
                    acc[k] += d[k]
            mean = bytes(v // len(run) for v in acc)
        page = Image.frombytes("RGB", (W, H), mean).point(curve * 3)
        for i in run:
            out[i] = page

    longest = max(len(r) for r in runs)
    print("%d frames in %d runs, mean %.1f, longest %d"
          % (len(flat), len(runs), len(flat) / float(len(runs)), longest))
    return out


def pack_palette(colours):
    pal = bytearray(32)
    for i, (r, g, b) in enumerate(colours[:16]):
        pal[i * 2] = r >> 4
        pal[i * 2 + 1] = ((g >> 4) << 4) | (b >> 4)
    return bytes(pal)


def quantise(img):
    """Index 0 for the background, fifteen fitted to the ink. No dithering.
    Returns (indexed image, 32-byte palette)."""
    px = img.load()
    ink = [px[x, y] for y in range(H) for x in range(W) if sum(px[x, y]) > DARK_SUM]
    if not ink:
        ink = [(0, 0, 0)]

    strip = Image.new("RGB", (len(ink), 1))
    strip.putdata(ink)
    fitted = strip.quantize(colors=15, method=Image.MEDIANCUT, dither=Image.NONE)
    raw = fitted.getpalette()[: 15 * 3]
    raw += [0] * (15 * 3 - len(raw))
    colours = [(0, 0, 0)] + [(raw[i * 3], raw[i * 3 + 1], raw[i * 3 + 2]) for i in range(15)]

    ref = Image.new("P", (1, 1))
    flat = []
    for c in colours:
        flat += list(c)
    ref.putpalette(flat + [0, 0, 0] * (256 - len(colours)))
    return img.quantize(palette=ref, dither=Image.NONE), pack_palette(colours)


def pack4(q):
    """Pack an indexed image to 4bpp, high nibble is the left pixel."""
    px = q.load()
    pitch = W // 2
    buf = bytearray(pitch * H)
    for y in range(H):
        row = y * pitch
        for x in range(0, W, 2):
            buf[row + (x >> 1)] = ((px[x, y] & 0xF) << 4) | (px[x + 1, y] & 0xF)
    return bytes(buf)


def rle_encode(src):
    """page_rle format: high bit set -> run of (c&0x7F)+1 of the next byte,
    otherwise (c&0x7F)+1 literal bytes."""
    out = bytearray()
    i = 0
    n = len(src)
    while i < n:
        run = 1
        while i + run < n and src[i + run] == src[i] and run < 128:
            run += 1
        if run >= 3:
            out.append(0x80 | (run - 1))
            out.append(src[i])
            i += run
            continue
        start = i
        lit = 0
        while i < n and lit < 128:
            r = 1
            while i + r < n and src[i + r] == src[i] and r < 3:
                r += 1
            if r >= 3:
                break
            i += 1
            lit += 1
        out.append(lit - 1)
        out += src[start:start + lit]
    return bytes(out)


def rle_decode(src, dstlen):
    """Reference decoder, used only to validate the encoder."""
    out = bytearray()
    i = 0
    n = len(src)
    while i < n:
        c = src[i]
        i += 1
        cnt = (c & 0x7F) + 1
        if c & 0x80:
            out += bytes([src[i]]) * cnt
            i += 1
        else:
            out += src[i:i + cnt]
            i += cnt
    assert len(out) == dstlen, "decoded %d, expected %d" % (len(out), dstlen)
    return bytes(out)


def main():
    frames = load_frames()
    count = len(frames)
    keys = {0, count - 1}
    print("frames", count)

    pages = []
    pals = []
    for f in frames:
        q, pal = quantise(f)
        pages.append(pack4(q))
        pals.append(pal)

    zero = bytes(PAGE)
    payloads = []
    for i, page in enumerate(pages):
        prev = zero if i in keys else pages[i - 1]
        delta = bytes(a ^ b for a, b in zip(prev, page))
        payloads.append(pals[i] + rle_encode(delta))

    header = struct.pack("<4sIIHH", b"AWOP", 1, count, W, H)
    table_bytes = 4 * count
    base = len(header) + table_bytes
    offsets = []
    pos = base
    for p in payloads:
        offsets.append(pos)
        pos += len(p)

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "wb") as f:
        f.write(header)
        for o in offsets:
            f.write(struct.pack("<I", o))
        for p in payloads:
            f.write(p)

    total = pos
    sizes = [len(p) - 32 for p in payloads]
    print("wrote %s" % OUT)
    print("total %.2f MB   avg %d B/frame   worst %d B/frame"
          % (total / 1048576.0, sum(sizes) // count, max(sizes)))
    print("stream rate at 25 fps: %.0f KB/s" % (25 * (sum(sizes) / count) / 1024))

    print("validating...")
    cur = bytearray(PAGE)
    for i, p in enumerate(payloads):
        if i in keys:
            cur = bytearray(PAGE)
        delta = rle_decode(p[32:], PAGE)
        for k in range(PAGE):
            cur[k] ^= delta[k]
        assert bytes(cur) == pages[i], "frame %d mismatch" % i
    print("validated: all %d frames decode exactly" % count)


if __name__ == "__main__":
    main()
