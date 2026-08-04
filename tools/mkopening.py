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


def load_frames():
    """Box-downsample each GIF frame to 320x240 and take the top 200 rows, stopping at LAST_FRAME."""
    im = Image.open(SRC)
    out = []
    for i, fr in enumerate(ImageSequence.Iterator(im)):
        if i > LAST_FRAME:
            break
        rgb = fr.convert("RGB").resize((W, NATIVE_H), Image.BOX)
        out.append(rgb.crop((0, 0, W, H)))
    return out


def quantise(img):
    """16 colours, no dithering. Returns (indexed image, 32-byte palette)."""
    q = img.quantize(colors=16, method=Image.MEDIANCUT, dither=Image.NONE)
    raw = q.getpalette()[: 16 * 3]
    raw += [0] * (16 * 3 - len(raw))
    pal = bytearray(32)
    for i in range(16):
        r, g, b = raw[i * 3], raw[i * 3 + 1], raw[i * 3 + 2]
        pal[i * 2] = r >> 4
        pal[i * 2 + 1] = ((g >> 4) << 4) | (b >> 4)
    return q, bytes(pal)


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
