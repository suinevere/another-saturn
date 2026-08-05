# Title Opening Animation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the static title card with the Mega Drive opening from `images/genesis-opening.gif` — 398 frames at exactly 40 ms — streamed from disc, ending on a held final frame the interactive menu draws over.

**Architecture:** An offline Python encoder turns the GIF into one `OPENING.BIN` of run-length-coded XOR deltas plus a per-frame palette. On console, a player streams that file through a read-ahead ring buffer, applies one delta per frame with a fused decode-and-XOR, uploads the frame's palette, and paces playback with a repeating 2,2,2,3,3 vblank pattern. The final frame is separately baked into the binary as the title backdrop, so the menu has something to draw over without re-reading the disc.

**Tech Stack:** C++11 (SH-2 cross build via SaturnRingLib), Python 3 with Pillow for the encoder, g++ host binaries for unit tests.

## Global Constraints

- **Spec:** `docs/superpowers/specs/2026-08-04-title-opening-animation-design.md`. Every number in this plan is copied from it.
- **Comment style, codified in `~/.claude/CLAUDE.md`:** every method, constant and file gets a header block; **tests and generated files get a file header only**; no comments inside function bodies; **every `Description:` is ONE sentence**.
- **Commits:** exactly ONE sentence. No body, no bullets, no trailers. NEVER mention Claude, AI, or the session. Stage named files explicitly — never `git add -A`.
- **Author of record:** suinevere.
- **Host test flags:** `-std=c++11 -Wall -Wextra -Werror -O1 -g`. Warnings are errors.
- **`opening_codec` must stay free of engine, SRL and libc headers** — only `<stdint.h>`. That separation is what keeps it host-testable, exactly as `page_rle` and `menu_blit` are.
- Indentation: **tabs** in `saturn/src/`, **spaces** in `saturn/tests/`.
- The Saturn makefile globs `src/**/*.cxx`, so new source files need no build-file edit.
- **Build:** `saturn/compile.bat debug`. **Host tests:** `sh saturn/tests/run_tests.sh`. **Run:** `saturn/run_with_mednafen.bat`.
- Python is on PATH with **Pillow 12.2.0**; `Image.getdata()` is deprecated there — use `get_flattened_data()`.

## Measured values this plan depends on

| Value | Number |
|---|---|
| Frames encoded and played | 383 (indices 0–382) |
| Frames in the source GIF | 398 — frames 383–397 are a fade-to-black loop transition and are dropped |
| Frame duration | 40 ms, every frame |
| Source | 640×480, box-downsampled to 320×240, top 200 rows taken |
| Content extent | native rows 0–162; nothing below is non-black |
| Page | 320×200 4bpp, pitch 160, `MENU_PAGE_SIZE` 32000 |
| Stream total | ≤ 1.98 MB |
| Average frame | 5 223 bytes |
| Worst frame | 20 474 bytes |
| Keyframes | frames 0 and 382 only |

## Palette allocation (unchanged from the previous spec)

```
 0      black          1– 3  frozen-frame greys     4– 6  logo/bolt teals
 7      panel border   8–10  unselected text       11     spare
12–14   selected text (strobed)                    15     white
```

During the opening all sixteen entries belong to the frame's own palette; the menu is not drawn until playback ends, so nothing conflicts.

---

## File Structure

**Create:**

| Path | Responsibility |
|---|---|
| `saturn/src/opening_codec.h` / `.cxx` | Fused RLE-decode-and-XOR. Pure arithmetic, no dependencies. |
| `saturn/tests/test_opening_codec.cxx` | Host suite for the codec. |
| `tools/mkopening.py` | Encoder: GIF → `OPENING.BIN`, self-validating. |
| `saturn/src/opening.h` / `.cxx` | The player: CD streaming, ring buffer, pacing, palette, skip. |

**Modify:**

| Path | Change |
|---|---|
| `tools/mkmenuart.py` | Emit `MENU_ART_TITLE_BACKDROP`; drop `MENU_ART_LOGO`. |
| `saturn/src/menu_art.h` / `.cxx` | Same swap. |
| `saturn/src/menu.cxx` | `menuDrawTitleScreen` copies the backdrop; `runTitle` plays the opening first. |
| `saturn/src/system/saturn_platform.h` / `.cxx` | Add `sat_video_sync`. |
| `saturn/tests/run_tests.sh` | Register the `opening codec` suite. |
| `.gitignore` | Ignore `run_tests_opening` binaries. |

---

### Task 1: Opening codec

**Files:**
- Create: `saturn/src/opening_codec.h`, `saturn/src/opening_codec.cxx`
- Test: `saturn/tests/test_opening_codec.cxx`
- Modify: `saturn/tests/run_tests.sh`, `.gitignore`

**Interfaces:**
- Consumes: the `page_rle` wire format — a control byte with the high bit set introduces a run of `(c & 0x7F) + 1` copies of the next byte; otherwise it introduces `(c & 0x7F) + 1` literal bytes. `pageRleEncode(const uint8_t *src, int32_t srcLen, uint8_t *dst, int32_t dstCap)` is used by the round-trip test.
- Produces: `bool openingApplyDelta(uint8_t *page, const uint8_t *src, int32_t srcLen, int32_t pageLen)` — decodes the stream and XORs expanded bytes into `page`, returning true only when exactly `pageLen` bytes were produced.

- [ ] **Step 1: Write the failing test**

Create `saturn/tests/test_opening_codec.cxx`:

```cpp
/*----------------------
 | test_opening_codec.cxx
 | Description: Host unit tests for the opening's fused RLE-decode-and-XOR. The
 |   arithmetic is pure and runs off-target rather than being eyeballed on
 |   hardware.
 | Author: suinevere
 | Dependencies: opening_codec.h, page_rle.h
 ----------------------*/
#include <cstdio>
#include <cstdint>
#include <cstring>
#include "opening_codec.h"
#include "page_rle.h"

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

enum { PAGE = 32000 };

static uint8_t g_page[PAGE];
static uint8_t g_ref[PAGE];
static uint8_t g_enc[PAGE * 2];

static void test_zero_delta_leaves_page_untouched(void)
{
    for (int i = 0; i < PAGE; ++i) {
        g_page[i] = (uint8_t)(i * 7);
    }
    memcpy(g_ref, g_page, PAGE);

    static uint8_t zeros[PAGE];
    memset(zeros, 0, sizeof(zeros));
    const int32_t n = pageRleEncode(zeros, PAGE, g_enc, (int32_t)sizeof(g_enc));
    CHECK_EQ(n > 0, 1);

    CHECK_EQ(openingApplyDelta(g_page, g_enc, n, PAGE), 1);
    CHECK_EQ(memcmp(g_page, g_ref, PAGE), 0);
}

static void test_keyframe_onto_cleared_page(void)
{
    for (int i = 0; i < PAGE; ++i) {
        g_ref[i] = (uint8_t)((i * 31) ^ (i >> 5));
    }
    const int32_t n = pageRleEncode(g_ref, PAGE, g_enc, (int32_t)sizeof(g_enc));
    CHECK_EQ(n > 0, 1);

    memset(g_page, 0, PAGE);
    CHECK_EQ(openingApplyDelta(g_page, g_enc, n, PAGE), 1);
    CHECK_EQ(memcmp(g_page, g_ref, PAGE), 0);
}

static void test_delta_moves_one_frame_to_the_next(void)
{
    static uint8_t a[PAGE];
    static uint8_t b[PAGE];
    static uint8_t d[PAGE];

    for (int i = 0; i < PAGE; ++i) {
        a[i] = (uint8_t)(i & 0xFF);
        b[i] = (uint8_t)((i < 4000) ? (i & 0xFF) : ((i * 3) & 0xFF));
        d[i] = (uint8_t)(a[i] ^ b[i]);
    }

    const int32_t n = pageRleEncode(d, PAGE, g_enc, (int32_t)sizeof(g_enc));
    CHECK_EQ(n > 0, 1);

    memcpy(g_page, a, PAGE);
    CHECK_EQ(openingApplyDelta(g_page, g_enc, n, PAGE), 1);
    CHECK_EQ(memcmp(g_page, b, PAGE), 0);
}

static void test_short_literal_and_run_by_hand(void)
{
    memset(g_page, 0, PAGE);
    g_page[0] = 0x0F;
    g_page[1] = 0xF0;

    const uint8_t enc[] = { 0x01, 0xFF, 0x00, 0x82, 0xAA };
    CHECK_EQ(openingApplyDelta(g_page, enc, (int32_t)sizeof(enc), 5), 1);

    CHECK_EQ(g_page[0], 0xF0);
    CHECK_EQ(g_page[1], 0xF0);
    CHECK_EQ(g_page[2], 0xAA);
    CHECK_EQ(g_page[3], 0xAA);
    CHECK_EQ(g_page[4], 0xAA);
}

static void test_rejects_stream_that_overruns_the_page(void)
{
    struct Guarded {
        uint8_t page[3];
        uint8_t canary;
    } buf;

    memset(&buf, 0, sizeof(buf));
    buf.canary = 0x5A;

    const uint8_t enc[] = { 0x83, 0x11 };
    CHECK_EQ(openingApplyDelta(buf.page, enc, (int32_t)sizeof(enc), 3), 0);
    CHECK_EQ(buf.canary, 0x5A);
}

static void test_rejects_stream_that_underruns_the_page(void)
{
    memset(g_page, 0, PAGE);
    const uint8_t enc[] = { 0x81, 0x11 };
    CHECK_EQ(openingApplyDelta(g_page, enc, (int32_t)sizeof(enc), 9), 0);
}

static void test_rejects_truncated_stream(void)
{
    memset(g_page, 0, PAGE);
    const uint8_t runNoValue[] = { 0x80 };
    CHECK_EQ(openingApplyDelta(g_page, runNoValue, 1, 1), 0);

    const uint8_t litShort[] = { 0x03, 0x11, 0x22 };
    CHECK_EQ(openingApplyDelta(g_page, litShort, 3, 4), 0);
}

int main(void)
{
    test_zero_delta_leaves_page_untouched();
    test_keyframe_onto_cleared_page();
    test_delta_moves_one_frame_to_the_next();
    test_short_literal_and_run_by_hand();
    test_rejects_stream_that_overruns_the_page();
    test_rejects_stream_that_underruns_the_page();
    test_rejects_truncated_stream();

    if (g_fail != 0) {
        printf("%d check(s) failed\n", g_fail);
        return 1;
    }
    printf("opening codec: all checks passed\n");
    return 0;
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run:
```sh
cd saturn/tests && g++ -std=c++11 -Wall -Wextra -Werror -O1 -g -I../src \
    -o run_tests_opening test_opening_codec.cxx ../src/opening_codec.cxx ../src/page_rle.cxx
```
Expected: FAIL — `opening_codec.h: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `saturn/src/opening_codec.h`:

```cpp
/*----------------------
 | opening_codec.h
 | Description: Applies one run-length coded XOR delta to a 4bpp page, which is
 |   how the title opening streams 398 frames without ever holding two at once.
 | Author: suinevere
 | Dependencies: stdint.h
 ----------------------*/
#ifndef OPENING_CODEC_H
#define OPENING_CODEC_H

#include <stdint.h>

/*----------------------
 | openingApplyDelta
 | Description: Decodes a page_rle stream and XORs the expanded bytes into page,
 |   which makes a keyframe and a delta the same operation against different
 |   predecessors.
 | Author: suinevere
 | Params: page -- pageLen bytes, modified in place; src -- encoded bytes;
 |         srcLen -- how many; pageLen -- exact number of bytes the stream must
 |         produce
 | Returns: true when exactly pageLen bytes were produced, false otherwise
 ----------------------*/
bool openingApplyDelta(uint8_t *page, const uint8_t *src, int32_t srcLen,
                       int32_t pageLen);

#endif /* OPENING_CODEC_H */
```

- [ ] **Step 4: Write the implementation**

Create `saturn/src/opening_codec.cxx`:

```cpp
/*----------------------
 | opening_codec.cxx
 | Description: The fused decode-and-XOR. No engine headers: see opening_codec.h.
 | Author: suinevere
 | Dependencies: opening_codec.h
 ----------------------*/
#include "opening_codec.h"

/*----------------------
 | openingApplyDelta
 | Description: Decodes a page_rle stream and XORs the expanded bytes into page,
 |   which makes a keyframe and a delta the same operation against different
 |   predecessors.
 | Author: suinevere
 | Params: page -- pageLen bytes, modified in place; src -- encoded bytes;
 |         srcLen -- how many; pageLen -- exact number of bytes the stream must
 |         produce
 | Returns: true when exactly pageLen bytes were produced, false otherwise
 ----------------------*/
bool openingApplyDelta(uint8_t *page, const uint8_t *src, int32_t srcLen,
                       int32_t pageLen)
{
	int32_t si = 0;
	int32_t di = 0;

	while (si < srcLen) {
		const uint8_t ctl = src[si];
		const int32_t n = (int32_t)(ctl & 0x7F) + 1;
		si++;

		if (di + n > pageLen) {
			return false;
		}

		if ((ctl & 0x80) != 0) {
			if (si >= srcLen) {
				return false;
			}
			const uint8_t v = src[si];
			si++;
			for (int32_t k = 0; k < n; ++k) {
				page[di + k] = (uint8_t)(page[di + k] ^ v);
			}
		} else {
			if (si + n > srcLen) {
				return false;
			}
			for (int32_t k = 0; k < n; ++k) {
				page[di + k] = (uint8_t)(page[di + k] ^ src[si + k]);
			}
			si += n;
		}

		di += n;
	}

	return di == pageLen;
}
```

- [ ] **Step 5: Run the test to verify it passes**

Run:
```sh
cd saturn/tests && g++ -std=c++11 -Wall -Wextra -Werror -O1 -g -I../src \
    -o run_tests_opening test_opening_codec.cxx ../src/opening_codec.cxx ../src/page_rle.cxx \
    && ./run_tests_opening
```
Expected: PASS — `opening codec: all checks passed`.

- [ ] **Step 6: Register the suite**

In `saturn/tests/run_tests.sh`, insert after the `page rle` block:

```sh
echo "== opening codec =="
g++ $OWN_FLAGS -I../src \
    -o run_tests_opening test_opening_codec.cxx ../src/opening_codec.cxx ../src/page_rle.cxx
./run_tests_opening
```

In `.gitignore`, next to the other suite binaries:

```
saturn/tests/run_tests_opening
saturn/tests/run_tests_opening.exe
```

- [ ] **Step 7: Run the whole suite**

Run: `sh saturn/tests/run_tests.sh`
Expected: every suite passes, ending `all suites passed`.

- [ ] **Step 8: Commit**

```bash
git add saturn/src/opening_codec.h saturn/src/opening_codec.cxx \
        saturn/tests/test_opening_codec.cxx saturn/tests/run_tests.sh .gitignore
git commit -m "Add the fused delta codec the title opening streams through"
```

---

### Task 2: Encoder

**Files:**
- Create: `tools/mkopening.py`
- Produces (git-ignored): `saturn/cd/data/OPENING.BIN`

**Interfaces:**
- Consumes: `images/genesis-opening.gif`; the `page_rle` wire format from Task 1.
- Produces: `OPENING.BIN` in the layout below. Task 4's player reads it.

**File layout — all header fields little-endian:**

```
uint32 magic 'AWOP'  (bytes 'A','W','O','P' in that order)   offset  0
uint32 version = 1                                                   4
uint32 frameCount = 398                                              8
uint16 width  = 320                                                 12
uint16 height = 200                                                 14
uint32 offset[frameCount]                                           16
   ... frame N payload at offset[N]:
       uint8 palette[32]
       uint8 rle[]        runs to the next frame's offset, or EOF
```

Palette entries are the engine's format: `byte0 = R`, `byte1 = (G << 4) | B`, each channel 4 bits.

Frames 0 and 397 are encoded against an all-zero page (keyframes); every other frame is encoded against its predecessor.

- [ ] **Step 1: Write the encoder**

Create `tools/mkopening.py`:

```python
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

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "images", "genesis-opening.gif")
OUT = os.path.join(ROOT, "saturn", "cd", "data", "OPENING.BIN")

W, H = 320, 200
PAGE = (W // 2) * H          # 32000
NATIVE_H = 240               # 640x480 box-downsamples to 320x240
KEYFRAMES = None             # filled in once the frame count is known


def load_frames():
    """Box-downsample each GIF frame to 320x240 and take the top 200 rows."""
    im = Image.open(SRC)
    out = []
    for fr in ImageSequence.Iterator(im):
        rgb = fr.convert("RGB").resize((W, NATIVE_H), Image.BOX)
        out.append(rgb.crop((0, 0, W, H)))
    return out


def quantise(img):
    """16 colours, no dithering. Returns (indexed image, 32-byte palette)."""
    q = img.quantize(colors=16, method=Image.MEDIANCUT, dither=Image.NONE)
    raw = q.getpalette()[: 16 * 3]
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
```

- [ ] **Step 2: Generate the stream**

Run: `python tools/mkopening.py`

Expected: `frames 398`, then a size line, then `validated: all 398 frames decode exactly`.

The validation line is the real test — it decodes the encoder's own output and asserts every frame matches its quantised source byte for byte, so a packing bug fails here rather than on hardware.

- [ ] **Step 3: Check the numbers against the spec**

Confirm from the printed output:

- total is **at or below 1.98 MB** — the spec's figure is a conservative upper bound because it charged 2 bytes per run unconditionally, so a smaller number is expected and fine
- worst frame is in the region of **20 474 bytes**

If the worst frame is dramatically larger than 20 474, stop and report — the read-ahead in Task 4 is sized against that number.

- [ ] **Step 4: Confirm the file is git-ignored**

Run: `git status --short saturn/cd/data/`
Expected: no output. `.gitignore` already carries `saturn/cd/data/OPENING.BIN`.

- [ ] **Step 5: Commit**

```bash
git add tools/mkopening.py
git commit -m "Add the encoder that turns the opening animation into a streamable delta file"
```

---

### Task 3: Title backdrop replaces the logo

**Files:**
- Modify: `tools/mkmenuart.py`, `saturn/src/menu_art.h`, `saturn/src/menu_art.cxx` (regenerated), `saturn/src/menu.cxx`

**Interfaces:**
- Consumes: `images/genesis-opening.gif` final frame.
- Produces: `extern const uint8_t MENU_ART_TITLE_BACKDROP[32000];` — a full 320×200 4bpp page using only palette indices 0, 4, 5, 6 and 15.
- Removes: `MENU_ART_LOGO`.

**Why a plain array rather than a `MenuArt`:** the backdrop is opaque and exactly page-sized, so it is copied rather than blitted. `menuBlit4bpp` treats index 0 as transparent, which is wrong here — the black areas must overwrite whatever was on the page.

The final frame quantises onto those five slots at RMS 1.7 of 255, which is why it needs no palette entries of its own.

- [ ] **Step 1: Add the backdrop generator**

In `tools/mkmenuart.py`, add alongside the existing builders:

```python
BACKDROP_SLOTS = [(0, (0, 0, 0)), (4, (0x00, 0x48, 0x49)), (5, (0x25, 0x6C, 0x6E)),
                  (6, (0x49, 0x90, 0x93)), (15, (0xFF, 0xFF, 0xFF))]


def build_backdrop():
    """The opening's final frame, quantised onto the slots the logo already uses."""
    from PIL import ImageSequence
    src = Image.open(os.path.join(ROOT, "images", "genesis-opening.gif"))
    last = None
    for fr in ImageSequence.Iterator(src):
        last = fr.convert("RGB")
    img = last.resize((320, 240), Image.BOX).crop((0, 0, 320, 200))

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
```

- [ ] **Step 2: Emit it and drop the logo**

The backdrop must be emitted as a **public** `const uint8_t` array, not a `static` one, so `menu_art.h` can declare it `extern`. The existing `emit_array` writes `static const uint8_t <name>[...]`, so add a second emitter beside it rather than changing the one the other assets use:

```python
def emit_public_array(f, name, data):
    f.write("const uint8_t %s[%d] = {\n" % (name, len(data)))
    for i in range(0, len(data), 12):
        f.write("\t" + " ".join("0x%02X," % b for b in data[i:i + 12]) + "\n")
    f.write("};\n\n")
```

In `main()`, delete the `build_logo()` function, its `logo = build_logo()` call, its `emit_array(f, "s_logoBits", logo[0])` call, and the line writing `const MenuArt MENU_ART_LOGO = ...`. In their place:

```python
    backdrop = build_backdrop()
    ...
    emit_public_array(f, "MENU_ART_TITLE_BACKDROP", backdrop)
```

`build_backdrop` calls the tool's existing `quantise(im, colours)` helper. Confirm that helper exists with that name and signature before relying on it — if it differs, use whatever the file actually defines rather than adding a duplicate.

- [ ] **Step 3: Update the header**

In `saturn/src/menu_art.h`, delete the `MENU_ART_LOGO` declaration and its header block, and add:

```cpp
/*----------------------
 | MENU_ART_TITLE_BACKDROP
 | Description: The opening animation's final frame as a full 320x200 4bpp page,
 |   using only the palette slots the artwork ramp already owns so the menu can
 |   draw straight over it.
 | Author: suinevere
 ----------------------*/
extern const uint8_t MENU_ART_TITLE_BACKDROP[32000];
```

- [ ] **Step 4: Regenerate and find every logo reference**

Run:
```sh
python tools/mkmenuart.py
grep -rn "MENU_ART_LOGO" saturn/
```

Expected after Step 5: the grep returns nothing.

- [ ] **Step 5: Draw the backdrop instead of the logo**

In `saturn/src/menu.cxx`, replace the body of `menuDrawTitleScreen` so it copies the backdrop rather than clearing and blitting:

```cpp
/*----------------------
 | menuDrawTitleScreen
 | Description: Paints the title card over the opening's final frame, with the
 |   selected entry shown by ramp rather than a cursor glyph.
 | Author: suinevere
 | Params: page -- compositing page; st -- state, for the cursor position
 | Returns: N/A
 ----------------------*/
static void menuDrawTitleScreen(uint8_t *page, const MenuState *st)
{
	memcpy(page, MENU_ART_TITLE_BACKDROP, MENU_PAGE_SIZE);
	menuBlit2bpp(page, &MENU_ART_START_GAME, 84, 128,
	             st->cursor == 0 ? MENU_BASE_SEL : MENU_BASE_DIM);
	menuBlit2bpp(page, &MENU_ART_LOAD_GAME, 84, 152,
	             st->cursor == 1 ? MENU_BASE_SEL : MENU_BASE_DIM);
}
```

Then check no other site references `MENU_ART_LOGO` — in particular the bolt path in `titleAnimate` and `menuRenderTitleFrame`.

- [ ] **Step 6: Build and test**

Run:
```sh
sh saturn/tests/run_tests.sh
saturn/compile.bat debug
```
Expected: `all suites passed`, and the Saturn build produces `saturn/BuildDrop/Another World (USA).iso`. One pre-existing unrelated warning in `saturn_platform.cxx` is not yours.

- [ ] **Step 7: Verify the layout off-target**

The emulator cannot be observed from here. Instead decode the committed backdrop back to a PNG and look at it:

```sh
python - <<'PY'
import re
from PIL import Image
src = open("saturn/src/menu_art.cxx").read()
body = re.search(r"MENU_ART_TITLE_BACKDROP\[\d+\] = \{(.*?)\};", src, re.S).group(1)
v = [int(x, 16) for x in re.findall(r"0x([0-9A-F]{2})", body)]
cols = {0:(0,0,0), 4:(0,0x48,0x49), 5:(0x25,0x6C,0x6E), 6:(0x49,0x90,0x93), 15:(255,255,255)}
im = Image.new("RGB", (320, 200))
p = im.load()
for y in range(200):
    for x in range(320):
        b = v[y*160 + (x>>1)]
        p[x, y] = cols.get((b >> 4) if not (x & 1) else (b & 0xF), (255, 0, 255))
im.resize((640, 400), Image.NEAREST).save("backdrop-check.png")
print("wrote backdrop-check.png")
PY
```

**Read `backdrop-check.png`.** It must show the wordmark and the three copyright lines. Any magenta pixel means an index outside the five allowed slots leaked in — stop and report. Delete the PNG before committing.

- [ ] **Step 8: Commit**

```bash
rm -f backdrop-check.png
git add tools/mkmenuart.py saturn/src/menu_art.h saturn/src/menu_art.cxx saturn/src/menu.cxx
git commit -m "Replace the title logo with the opening's final frame as a full-page backdrop"
```

---

### Task 4: Player and integration

**Files:**
- Create: `saturn/src/opening.h`, `saturn/src/opening.cxx`
- Modify: `saturn/src/system/saturn_platform.h`, `saturn/src/system/saturn_platform.cxx`, `saturn/src/menu.cxx`

**Interfaces:**
- Consumes: `openingApplyDelta` (Task 1); `OPENING.BIN` (Task 2); `MENU_ART_TITLE_BACKDROP` (Task 3); `sat_cd_open(const char *name)`, `sat_cd_read(SatCdFile *file, int32_t pos, void *dst, int32_t size)`, `sat_cd_size`, `sat_cd_close`; `System::setPalette(const uint8_t *)`, `System::updateDisplay(const uint8_t *)`; `MENU_PAGE_SIZE` = 32000.
- Produces: `void openingPlay(System *sys, uint8_t *page);` and `void sat_video_sync(void);`

**Pacing:** 40 ms is 2.4 vblanks at 60 Hz. Holding frames for a repeating **2, 2, 2, 3, 3** pattern gives 12 vblanks per 5 frames — exactly 25 fps with no drift. `sat_video_present` already waits one vblank, so a hold of *k* needs *k−1* extra calls to `sat_video_sync`.

**Streaming:** the ring is filled completely before frame 0 — a stall of roughly 430 ms while the screen is black, which the first frames are anyway. After that each frame tops up by one 8 KB chunk when there is room, which stays ahead of the 5 223-byte average and lets the ring absorb the 20 474-byte peak.

- [ ] **Step 1: Add the vblank wait**

In `saturn/src/system/saturn_platform.h`, after `sat_video_present`:

```c
/*----------------------
 | sat_video_sync
 | Description: Waits one vblank without touching the framebuffer, so a caller can
 |   hold an already-presented frame for more than one field.
 | Author: suinevere
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void sat_video_sync(void);
```

In `saturn/src/system/saturn_platform.cxx`, beside `sat_video_present`:

```cpp
extern "C" void sat_video_sync(void)
{
    sat_audio_update();
    SRL::Core::Synchronize();
}
```

- [ ] **Step 2: Write the player header**

Create `saturn/src/opening.h`:

```cpp
/*----------------------
 | opening.h
 | Description: Streams the title opening from the disc and presents it at 25 fps,
 |   leaving the final frame on screen for the menu to draw over.
 | Author: suinevere
 | Dependencies: stdint.h
 ----------------------*/
#ifndef OPENING_H
#define OPENING_H

#include <stdint.h>

struct System;

/*----------------------
 | openingPlay
 | Description: Plays OPENING.BIN into page, returning early on any button press
 |   and returning immediately when the file is absent so a disc without it still
 |   reaches the menu.
 | Author: suinevere
 | Params: sys -- for palette upload, presentation and input; page --
 |         MENU_PAGE_SIZE bytes the animation decodes into
 | Returns: N/A
 ----------------------*/
void openingPlay(System *sys, uint8_t *page);

#endif /* OPENING_H */
```

- [ ] **Step 3: Write the player**

Create `saturn/src/opening.cxx`:

```cpp
/*----------------------
 | opening.cxx
 | Description: The streaming title-opening player: read-ahead, delta decode,
 |   per-frame palette and 25 fps pacing over a 60 Hz field rate.
 | Author: suinevere
 | Dependencies: opening.h, opening_codec.h, menu_draw.h, sys.h, saturn_cdfile.h,
 |   saturn_platform.h
 | Globals: s_ring, s_offsets
 ----------------------*/
#include "opening.h"
#include "opening_codec.h"
#include "menu_draw.h"
#include "sys.h"
#include "saturn_cdfile.h"
#include "saturn_platform.h"

extern "C" {
#include <string.h>
}

/*----------------------
 | OPENING_RING / OPENING_CHUNK / OPENING_MAX_FRAMES
 | Description: The read-ahead ring is 128 KB, about 25 frames at the 5223-byte
 |   average, topped up one 8 KB chunk per frame so it stays ahead of playback.
 | Author: suinevere
 ----------------------*/
enum {
	OPENING_RING       = 131072,
	OPENING_CHUNK      = 8192,
	OPENING_MAX_FRAMES = 512
};

/*----------------------
 | s_ring
 | Description: The read-ahead buffer, holding encoded bytes only.
 | Author: suinevere
 ----------------------*/
static uint8_t s_ring[OPENING_RING];

/*----------------------
 | s_offsets
 | Description: The file's per-frame offset table, read once at open.
 | Author: suinevere
 ----------------------*/
static uint32_t s_offsets[OPENING_MAX_FRAMES];

/*----------------------
 | OPENING_HOLD
 | Description: Vblanks each frame is held for, cycling 2,2,2,3,3 so five frames
 |   span twelve fields and land on exactly 25 fps.
 | Author: suinevere
 ----------------------*/
static const int OPENING_HOLD[5] = { 2, 2, 2, 3, 3 };

/*----------------------
 | openingReadU32
 | Description: Reads a little-endian 32-bit value from a byte buffer.
 | Author: suinevere
 | Params: p -- at least four readable bytes
 | Returns: the value
 ----------------------*/
static uint32_t openingReadU32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
	       ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

void openingPlay(System *sys, uint8_t *page)
{
	SatCdFile *f = sat_cd_open("OPENING.BIN");
	if (f == 0) {
		return;
	}

	uint8_t head[16];
	if (sat_cd_read(f, 0, head, 16) != 16 ||
	    head[0] != 'A' || head[1] != 'W' || head[2] != 'O' || head[3] != 'P') {
		sat_cd_close(f);
		return;
	}

	const uint32_t count = openingReadU32(head + 8);
	if (count == 0 || count > OPENING_MAX_FRAMES) {
		sat_cd_close(f);
		return;
	}

	const int32_t tableBytes = (int32_t)(count * 4);
	static uint8_t table[OPENING_MAX_FRAMES * 4];
	if (sat_cd_read(f, 16, table, tableBytes) != tableBytes) {
		sat_cd_close(f);
		return;
	}
	for (uint32_t i = 0; i < count; ++i) {
		s_offsets[i] = openingReadU32(table + i * 4);
	}

	const int32_t fileSize = sat_cd_size(f);
	int32_t ringPos = (int32_t)s_offsets[0];
	int32_t ringLen = sat_cd_read(f, ringPos, s_ring, OPENING_RING);
	if (ringLen <= 0) {
		sat_cd_close(f);
		return;
	}

	memset(page, 0, MENU_PAGE_SIZE);

	uint32_t i = 0;
	int hold = 0;
	bool skipped = false;

	while (i < count) {
		const int32_t start = (int32_t)s_offsets[i];
		const int32_t end = (i + 1 < count) ? (int32_t)s_offsets[i + 1] : fileSize;
		const int32_t need = end - start;

		if (start < ringPos || start + need > ringPos + ringLen) {
			ringPos = start;
			ringLen = sat_cd_read(f, ringPos, s_ring, OPENING_RING);
			if (ringLen < need) {
				break;
			}
		}

		const uint8_t *pay = s_ring + (start - ringPos);
		sys->setPalette(pay);
		if (!openingApplyDelta(page, pay + 32, need - 32, MENU_PAGE_SIZE)) {
			break;
		}

		sys->updateDisplay(page);
		for (int k = 1; k < OPENING_HOLD[hold]; ++k) {
			sat_video_sync();
		}
		hold = (hold + 1) % 5;

		sys->processEvents();
		if (!skipped && (sys->input.menuConfirm || sys->input.menuCancel ||
		                 sys->input.pause)) {
			skipped = true;
			memset(page, 0, MENU_PAGE_SIZE);
			i = count - 1;
			continue;
		}

		if (ringPos + ringLen < fileSize &&
		    (ringPos + ringLen) - (start + need) < OPENING_CHUNK) {
			const int32_t at = ringPos + ringLen;
			int32_t room = OPENING_RING - ringLen;
			if (room > OPENING_CHUNK) {
				room = OPENING_CHUNK;
			}
			if (room > 0) {
				const int32_t got = sat_cd_read(f, at, s_ring + ringLen, room);
				if (got > 0) {
					ringLen += got;
				}
			}
		}

		++i;
	}

	sat_cd_close(f);
}
```

- [ ] **Step 4: Play it from the title screen**

In `saturn/src/menu.cxx`, add the include beside the others:

```cpp
#include "opening.h"
```

and update the file header block's `Dependencies:` line to list `opening.h`.

In `Menu::runTitle`, play the opening before anything is drawn — replace the opening lines of the function so it reads:

```cpp
bool Menu::runTitle() {
	menuStateEnterTitle(&_st);
	_statusError = SAT_BUP_OK;

	openingPlay(_sys, _page);

	titleAnimate();
	menuRenderTitleFrame(_page, _sys, &_st, _boltIndex, _boltFrame);
	menuPrimeEdges(_sys, &_prevPad, &_repeatTimer);
```

The rest of the function is unchanged. `menuPrimeEdges` already runs after the opening, so the button used to skip cannot fall through into the menu as a fresh press.

- [ ] **Step 5: Build**

Run: `saturn/compile.bat debug`
Expected: builds clean, producing `saturn/BuildDrop/Another World (USA).iso`. One pre-existing unrelated warning in `saturn_platform.cxx` is not yours.

- [ ] **Step 6: Confirm the stream reached the disc image**

Run:
```sh
ls -la "saturn/cd/data/OPENING.BIN"
ls -la "saturn/BuildDrop/Another World (USA).iso"
```

Expected: `OPENING.BIN` is present at roughly 2 MB, and the ISO has grown by about that much over its previous ~1.9 MB. If the ISO did not grow, the build is not picking the file up from `saturn/cd/data/` and that must be reported rather than worked around.

- [ ] **Step 7: Run the host suite**

Run: `sh saturn/tests/run_tests.sh`
Expected: `all suites passed`.

- [ ] **Step 8: Reason about the pacing in the report**

You cannot observe an emulator. Do not claim a visual result. Instead state in your report, from the code:

- how many vblanks five consecutive frames occupy, and why that is exactly 25 fps
- that the ring is filled before the first frame is presented
- that a skip clears the page before applying the final keyframe, because a keyframe is a delta against zero

- [ ] **Step 9: Commit**

```bash
git add saturn/src/opening.h saturn/src/opening.cxx \
        saturn/src/system/saturn_platform.h saturn/src/system/saturn_platform.cxx \
        saturn/src/menu.cxx
git commit -m "Stream the Mega Drive opening from disc before the title menu appears"
```

---

## Notes for the reviewer

- **Task 2's validation step is the real test of the encoder.** It decodes its own output and asserts every one of the 398 frames matches its quantised source byte for byte. A packing or offset bug fails at generation time.
- **A keyframe is not a special case.** Because the operation is XOR, a keyframe is a delta against an all-zero page. The decoder has one path; the player clears the page first. That is why the skip path in Task 4 does `memset` before jumping to the last frame.
- **`sat_video_present` already waits a vblank.** A hold of *k* fields therefore costs *k−1* extra `sat_video_sync` calls, not *k*. Getting this wrong makes the animation run at 20 fps or 30 fps rather than 25.
- **The backdrop is copied, not blitted.** `menuBlit4bpp` treats index 0 as transparent, which would leave whatever was underneath showing through the black areas.
- **The 20 474-byte worst frame is the one risk that cannot be checked off-hardware.** If playback stutters on a real drive, the mitigations in the spec lower the peak rather than enlarging the ring.
- **A rejection test must be able to fail.** Task 1's overrun test originally reused the 32 000-byte `g_page` for a 3-byte `pageLen`, so deleting the bounds guard still returned false via the final `di == pageLen` tally and the test passed either way. It now writes into an exactly-sized buffer with a canary after it. The check that proves such a test works is to delete the guard, watch the test fail, and restore it.
- **Include paths carry no `system/` prefix.** `menu.cxx` already includes `saturn_platform.h` and `saturn_backup.h` unprefixed, so `src/system` is on the include path; `opening.cxx` follows that.
- **`sat_video_sync` is a plan-level addition, not in the spec.** The spec required exact 25 fps pacing without saying how; presenting the same page two or three times per frame would work but re-DMAs 32 000 bytes each time, so a wait that leaves the framebuffer alone is the cheaper way to satisfy it.
