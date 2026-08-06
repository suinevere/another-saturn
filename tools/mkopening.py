"""
mkopening.py
Description: Builds saturn/cd/data/OPENING.BIN from images/genesis-opening.mp4 as
  run-length coded XOR deltas against one shared 16-colour palette. Run from the
  repository root. The output is git-ignored and regenerated on demand. Needs
  ffmpeg to decode the source; genesis-opening.gif is the same footage put
  through a 256-colour dither and is kept only for reference -- decoding from it
  doubled the count of stray single pixels along the letter strokes.
Author: suinevere
Usage: python tools/mkopening.py
"""
import os
import shutil
import struct
import subprocess
from PIL import Image
from opening_frames import LAST_FRAME

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "images", "genesis-opening.mp4")
OUT = os.path.join(ROOT, "saturn", "cd", "data", "OPENING.BIN")

W, H = 320, 200
PAGE = (W // 2) * H          # 32000
NATIVE_W, NATIVE_H = 640, 480
KEYFRAMES = None             # filled in once the frame count is known

# Where ffmpeg gets installed when it is not on PATH. Checked as a fallback so
# the tool works straight after a winget install without a shell restart.
FFMPEG_FALLBACKS = [
    os.path.join(os.environ.get("LOCALAPPDATA", ""), "Microsoft", "WinGet", "Packages",
                 "Gyan.FFmpeg_Microsoft.Winget.Source_8wekyb3d8bbwe",
                 "ffmpeg-9.0-full_build", "bin", "ffmpeg.exe"),
]


def find_ffmpeg():
    found = shutil.which("ffmpeg")
    if found:
        return found
    for path in FFMPEG_FALLBACKS:
        if path and os.path.exists(path):
            return path
    raise SystemExit("ffmpeg not found: put it on PATH, or add its location to "
                     "FFMPEG_FALLBACKS in tools/mkopening.py")


def decode(limit):
    """Yield the first `limit` frames of SRC as 640x480 RGB images."""
    proc = subprocess.Popen(
        [find_ffmpeg(), "-v", "error", "-i", SRC, "-frames:v", str(limit),
         "-f", "rawvideo", "-pix_fmt", "rgb24", "-"],
        stdout=subprocess.PIPE)
    size = NATIVE_W * NATIVE_H * 3
    try:
        for _ in range(limit):
            buf = bytearray()
            while len(buf) < size:
                chunk = proc.stdout.read(size - len(buf))
                if not chunk:
                    return
                buf += chunk
            yield Image.frombytes("RGB", (NATIVE_W, NATIVE_H), bytes(buf))
    finally:
        proc.stdout.close()
        proc.wait()

# The wordmark's strokes are one 640x480 pixel wide, so the 2:1 reduction
# averages each with the black beside it and halves its brightness; without the
# lift, neighbouring stroke pixels land in different slots and the stroke comes
# out dashed. Applied after the reduction, and after averaging, which is linear.
#
# Kept as light as the strokes allow, because any lift is a distortion of the
# source's own colour. Scored as mean per-pixel error against the source frame
# over 42 frames spread across the intro: 0.90 sits at 10.0 on lit pixels, where
# 0.80 is 15.1 and 0.55 was worse again, all of it showing up as mid-tones too
# bright and tints washed toward grey.
GAMMA = 0.90

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
# fitting the rest to the ink only is what keeps the lit states apart.
#
# The sum is measured after the lift, and it has to clear the noise floor without
# reaching the lit states: the source's darkest wordmark sums to 126 before the
# lift, so a cut much above this one deletes that card and leaves only its
# neutral highlights, which is how the blue went missing once already.
DARK_SUM = 60

PALETTE_STRIP = 60000
PALETTE_BULK_SLOTS = 13
PALETTE_RESERVE_SLOTS = 2
PALETTE_RESERVE_CHROMA = 30
PALETTE_RESERVE_BG = 20

# The wordmark's teal reads dull next to the menu's own ramp, which holds red at
# between 0.00 and 0.42 of green all the way up; the intro's teal drifts to 0.94
# at the bright end, because the source is a washed-out capture. Red is pulled
# down on teal-leaning ink until it reaches the ratio of the selected menu ramp's
# middle entry, 82/222. Applied only where the pixel already has chroma, so the
# white credits stay white -- their chroma measures 1.6 against the wordmark's 26
# to 51 -- and only where green and blue both lead, so the blue state is left
# alone. This is a deliberate departure from the source, and it moves per-pixel
# distance from it from 10.3 to 15.4 -- all of that in the direction asked for.
# A flat saturation boost was tried first and is the wrong tool: it drove the
# blue card past the source, to blue-minus-green +98 against its +54, while the
# teal still fell short.
TEAL_TARGET_RG = 0.37
TEAL_CHROMA_FLOOR = 10.0


def load_frames():
    """Box-downsample, average each run of frames showing one card, then lift.
    Averaging happens after the reduction because both are averages and the order
    does not matter; the lift is not linear, so it comes last."""
    flat = []
    for fr in decode(LAST_FRAME + 1):
        rgb = fr.resize((W, NATIVE_H // 2), Image.BOX).crop((0, 0, W, H))
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
        page = deepen_teal(Image.frombytes("RGB", (W, H), mean).point(curve * 3))
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


def deepen_teal(page):
    """Pull red down on teal-leaning ink so it reaches the menu ramp's ratio."""
    px = page.load()
    for y in range(H):
        for x in range(W):
            r, g, b = px[x, y]
            if max(r, g, b) - min(r, g, b) <= TEAL_CHROMA_FLOOR:
                continue
            if g <= r or b <= r:
                continue
            want = int(TEAL_TARGET_RG * max(g, b))
            if want < r:
                px[x, y] = (want, g, b)
    return page


def ink_histogram(pages, blue_only=False):
    """Weighted colour histogram of the ink, each page counting equally rather
    than each pixel so a page with little ink still counts. blue_only keeps just
    the chromatic ink where blue leads."""
    weighted = {}
    for page in pages:
        px = page.load()
        hist = {}
        for y in range(H):
            for x in range(W):
                c = px[x, y]
                if sum(c) <= DARK_SUM:
                    continue
                if blue_only and (c[2] - c[1] < PALETTE_RESERVE_BG or
                                  max(c) - min(c) < PALETTE_RESERVE_CHROMA):
                    continue
                hist[c] = hist.get(c, 0) + 1
        total = float(sum(hist.values())) or 1.0
        for c, n in hist.items():
            weighted[c] = weighted.get(c, 0.0) + n / total
    return weighted


def fit_colours(weighted, slots):
    """Median cut over a weighted histogram."""
    if not weighted or slots <= 0:
        return []
    scale = PALETTE_STRIP / sum(weighted.values())
    data = []
    for c, wt in weighted.items():
        data += [c] * max(1, int(wt * scale))
    strip = Image.new("RGB", (len(data), 1))
    strip.putdata(data)
    fitted = strip.quantize(colors=slots, method=Image.MEDIANCUT, dither=Image.NONE)
    raw = fitted.getpalette()[: slots * 3]
    raw += [0] * (slots * 3 - len(raw))
    return [(raw[i * 3], raw[i * 3 + 1], raw[i * 3 + 2]) for i in range(slots)]


def fill_unused(colours, weighted):
    """Drop entries that collapse onto each other once packed to four bits per
    channel, then spend the freed slots on whatever the histogram still
    represents worst. Two of the fitted colours landing in the same 4-bit cell
    otherwise costs a slot outright."""
    kept, seen = [], set()
    for c in colours:
        key = (c[0] >> 4, c[1] >> 4, c[2] >> 4)
        if key in seen:
            continue
        seen.add(key)
        kept.append(c)

    while len(kept) < 16 and weighted:
        best, at = None, None
        for c, wt in weighted.items():
            if (c[0] >> 4, c[1] >> 4, c[2] >> 4) in seen:
                continue
            far = min((c[0] - o[0]) ** 2 + (c[1] - o[1]) ** 2 + (c[2] - o[2]) ** 2 for o in kept)
            if best is None or far * wt > best:
                best, at = far * wt, c
        if at is None:
            break
        seen.add((at[0] >> 4, at[1] >> 4, at[2] >> 4))
        kept.append(at)
    return (kept + [(0, 0, 0)] * 16)[:16]


def build_palette(pages):
    """One palette for the whole intro: index 0 for the background, the bulk
    fitted to all the ink, and a reserve fitted to the most saturated of it.

    Fitting per frame was what made the transitions look unclean. Each fit landed
    on slightly different colours, so 133 of the 382 frame boundaries moved the
    palette by 17 RGB units on average and the whole picture shifted underneath
    the animation. Fitting once holds it still, and it halves the stream as well,
    because a pixel now only changes index when the picture changes.
    """
    bulk = ink_histogram(pages)
    colours = [(0, 0, 0)]
    colours += fit_colours(bulk, PALETTE_BULK_SLOTS)
    colours += fit_colours(ink_histogram(pages, blue_only=True), PALETTE_RESERVE_SLOTS)
    colours = fill_unused(colours, bulk)

    ref = Image.new("P", (1, 1))
    flat = []
    for c in colours:
        flat += list(c)
    ref.putpalette(flat + [0, 0, 0] * (256 - len(colours)))
    return ref, pack_palette(colours)


def quantise(img, ref):
    """Snap one frame onto the shared palette, background to index 0."""
    q = img.quantize(palette=ref, dither=Image.NONE)
    px = img.load()
    qp = q.load()
    for y in range(H):
        for x in range(W):
            if sum(px[x, y]) <= DARK_SUM:
                qp[x, y] = 0
    return q


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

    seen = set()
    distinct = []
    for f in frames:
        if id(f) not in seen:
            seen.add(id(f))
            distinct.append(f)
    ref, pal = build_palette(distinct)

    pages = [pack4(quantise(f, ref)) for f in frames]
    pals = [pal] * count

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
