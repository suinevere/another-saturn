# Another World → Sega Saturn — Title Opening Animation Design Spec

**Date:** 2026-08-04
**Status:** Approved, ready to plan
**Target engine:** SaturnRingLib (SRL)
**Extends:** `2026-08-03-title-menu-sprites-design.md`, which built the palette
allocation, the chrome strings, the strobe and the lightning this spec hands off to.

## Goal

Replace the static title card with an exact replication of the Mega Drive opening
in `images/genesis-opening.gif` — 398 frames at 40 ms each, 15.92 seconds —
streamed from disc, ending on the wordmark and copyright lines, at which point the
interactive menu appears over the held final frame.

## The source

| Property | Value |
|---|---|
| Dimensions | 640×480 |
| Frames | 398 |
| Frame duration | 40 ms, every frame, no exceptions |
| Total run time | 15.92 s |
| Unique colours, whole sequence | 74 |
| Unique colours, worst single frame | 67 |
| File size | 2 350 343 bytes |

Two measurements shape everything below.

**The content occupies only rows 0–162 of the 240-row native frame.** Everything
below row 162 is black at every threshold tested (12, 40 and 80 of 765). The page
is 320×200, so the top 200 native rows contain the entire animation with 37 rows to
spare. **There is no vertical crop and no vertical scaling** — the replication is
exact in both axes. Horizontally 640→320 is a clean halve.

The GIF is *not* a clean 2× upscale: at frame 90, 16 206 of 76 800 2×2 blocks are
non-uniform, so it was resampled at some point in its history. A box downsample by
2 is the correct inverse and is what the encoder uses.

## The one compromise

`sat_video_present` (saturn_platform.cxx:233) DMAs a 320×200 **4bpp** page — sixteen
colours. The source needs 74. Byte-exact colour is therefore unreachable.

**Decision: a per-frame 16-colour palette.** Each frame carries its own sixteen
colours, written to CRAM as that frame is presented. Measured quantisation error is
**RMS 6.7 of 255 on average, worst-frame RMS 9.1** — the fades and the lightning
band cleanly because no palette has to serve more than one frame.

The two alternatives were rejected. A single shared palette across all 398 frames
has to span black to full brightness and visibly bands the fades. Reconfiguring
VDP2 to an 8bpp bitmap would reproduce all 74 colours exactly, but roughly doubles
the stream to ~4 MB and ~256 KB/s — close enough to the drive ceiling to put the
whole feature at risk — and adds a bitmap reconfiguration on the path into the game.

Palette cost is negligible: 32 bytes per frame, 12 736 bytes total.

## Storage and streaming

Frames are encoded as **XOR deltas against the previous frame, run-length coded**.

| Measure | Value |
|---|---|
| Total | 1.98 MB |
| Average frame | 5 223 bytes |
| Worst frame | 20 474 bytes |
| Demand at 25 fps | 128 KB/s average |

Those figures come from an encoder that charges 2 bytes per run unconditionally.
The real wire format packs up to 128 literals behind one control byte, so **1.98 MB
is a conservative upper bound** and the shipped file should be smaller.

A 2× drive delivers roughly 300 KB/s, so the average leaves 2.3× headroom. The
worst frame does not: 20 474 bytes inside a 40 ms budget would need ~500 KB/s. So
the player never reads on the frame it is displaying. A **read-ahead ring buffer of
128 KB** — about 25 frames of slack at the average rate — is filled ahead of
playback, and decoding always draws from RAM.

### Wire format

The RLE is the existing `page_rle` format, unchanged: a control byte with the high
bit set introduces a run of `(c & 0x7F) + 1` copies of the next byte; otherwise it
introduces `(c & 0x7F) + 1` literal bytes. The encoder may therefore be validated
against `pageRleDecode`.

`saturn/cd/data/OPENING.BIN`:

All header fields are little-endian, matching the SH-2 build's byte order as the
engine already assumes elsewhere.

```
header    uint32 magic 'AWOP'        offset  0
          uint32 version = 1                 4
          uint32 frameCount = 398            8
          uint16 width  = 320               12
          uint16 height = 200               14
          uint32 offset[frameCount]         16   absolute byte offset per frame
frame N   uint8  palette[32]
          uint8  rle[]                            to the next frame's offset
```

**A keyframe is simply a delta against an all-zero page.** Because the operation is
XOR, a full frame and a delta are the same thing encoded against different
predecessors — so the decoder needs no special case at all. The player clears the
page before applying a keyframe and leaves it alone before applying a delta.

**Frames 0 and 397 are keyframes.** The last one is what makes skipping cheap: the
player clears the page, seeks to frame 397 and applies one delta, rather than
replaying 397 of them. No other keyframes are needed — the stream is otherwise
played strictly forward.

The ring buffer holds **encoded** bytes. Decoding happens once per frame, straight
from the ring into the page.

The file is **generated and git-ignored**, like the BANK files. `images/genesis-opening.gif`
is the committed source and `tools/mkopening.py` is the committed generator.

## Playback

### Pacing

40 ms is 2.4 vblanks at NTSC 60 Hz — not an integer. Holding each frame for a
repeating **2, 2, 2, 3, 3** pattern gives 12 vblanks per 5 frames, which is exactly
25 fps with no accumulated drift. The pattern index advances once per decoded frame.

### Per-frame work

1. Take the next frame's bytes from the ring buffer.
2. Write its 32 palette bytes to CRAM.
3. Decode the RLE and XOR the expanded bytes into the page in one fused pass — no
   32 KB scratch buffer.
4. Present, then hold for this frame's share of the 2,2,2,3,3 pattern.

The fused decode is roughly 32 000 byte operations, about 190 k cycles, near 17% of
the ~1.12 M cycles available in 40 ms at 28 MHz. The page DMA that follows already
happens every frame today.

### Skipping

Any button press clears the page, seeks to the final keyframe, applies it, and
proceeds directly to the handoff. Input is polled every frame through the existing
`menuPadMask`. Edge detection is not required — the opening responds to a button
being held as readily as to a fresh press, which is the behaviour a player skipping
an intro expects.

## Handoff to the menu

The final frame's content is the wordmark plus three copyright lines on black — the
same teal family the existing artwork already uses. Quantised onto the five palette
slots the logo occupies (`0`, `4`, `5`, `6`, `15`) it measures **RMS 1.7 of 255**.

So the handoff costs nothing structurally. On completion the player installs
`MENU_ART_PALETTE` and the title screen draws a new baked asset:

**`MENU_ART_TITLE_BACKDROP`** — the final frame at 320×200 4bpp, quantised onto
`{0, 4, 5, 6, 15}`, generated by `tools/mkmenuart.py` from the GIF's last frame.

This asset **replaces `MENU_ART_LOGO`**, which becomes dead: the backdrop contains
the same wordmark and adds the copyright lines. `menuDrawTitleScreen` becomes a copy
of the backdrop followed by the two chrome strings, which is cheaper than today's
clear-then-blit. `MENU_ART_LOGO` and its generator path are deleted.

The backdrop is stored raw at 4bpp: 160 × 200 = **32 000 bytes**. Removing the
8 845-byte logo makes the net growth in the binary about **23 KB**, taking the total
baked artwork from roughly 14.5 KB to 37.5 KB. That is comfortable against the
600 KB resource block already resident in Low Work RAM.

Everything else built in the previous spec keeps working untouched: the strobe on
entries 12–14, the random bolts at 4–6 plus 15, the panel border at 7, and the
frozen-frame greys at 1–3. The palette allocation does not change.

During the opening itself all sixteen entries belong to the frame's own palette.
There is no conflict because the menu is not drawn until the animation ends.

## Code

**New:**

- `tools/mkopening.py` — box-downsamples 640×480 → 320×240, takes the top 200 rows,
  quantises each frame to 16 colours, XORs against the previous frame, RLE-codes the
  result, and writes `OPENING.BIN`. Validates its own output by decoding the whole
  stream back and asserting each frame matches the quantised source exactly.
- `saturn/src/opening_codec.h` / `.cxx` —
  `bool openingApplyDelta(uint8_t *page, const uint8_t *src, int32_t srcLen, int32_t pageLen)`,
  the fused RLE-decode-and-XOR. It follows `pageRleDecode`'s contract exactly:
  returns true only when the stream produced exactly `pageLen` bytes, and rejects
  any stream that would run past either end rather than writing what it can. No
  engine, SRL or libc dependency, so it is host-testable exactly as `menu_blit` and
  `page_rle` are.
- `saturn/src/opening.h` / `.cxx` — the player: file handle, ring buffer, read-ahead,
  pacing, palette upload, skip. The only file here that touches hardware.
- `saturn/tests/test_opening_codec.cxx` — new host suite `run_tests_opening`.

**Changed:**

- `tools/mkmenuart.py` — emits `MENU_ART_TITLE_BACKDROP`, drops `MENU_ART_LOGO`.
- `saturn/src/menu_art.h` / `.cxx` — same swap.
- `saturn/src/menu.cxx` — `runTitle` plays the opening before entering its loop;
  `menuDrawTitleScreen` draws the backdrop instead of clearing and blitting the logo.
- `saturn/tests/run_tests.sh`, `.gitignore` — register the new suite and ignore
  `saturn/cd/data/OPENING.BIN`.

## Testing

Host suite `run_tests_opening` covers `openingApplyDelta`:

- A delta of all zeroes leaves the page byte-identical.
- A known short delta produces the expected page, verified byte by byte.
- Round-trip against `pageRleEncode`: encode a random buffer, apply it to a zeroed
  page, and confirm the page equals the buffer.
- A stream that would run past the end of the page is rejected rather than writing
  what it can, matching `pageRleDecode`'s contract.
- A truncated stream is rejected.

The encoder validates itself: `mkopening.py` decodes its own output and asserts an
exact match against the quantised source frames, so a packing bug fails at build
time rather than on hardware.

Streaming, pacing and CD behaviour cannot be host-tested. They are verified by
running the disc.

## Risks

**The 20 474-byte worst frame is the one number that cannot be verified off
hardware.** If the 128 KB read-ahead proves insufficient on a real drive, the
mitigation is to lower the peak rather than enlarge the buffer: raise the number of
keyframes so no single delta spans a full scene change, or drop the peak frames to a
shared palette so their deltas shrink.

Secondary: the opening reads continuously from disc for 15.92 seconds at boot,
before any game data is loaded. Nothing else competes for the drive during that
window, which is why the average has 2.3× headroom, but it is the first sustained
streaming this port has done.

## Out of scope

- **Thunder audio.** The GIF is silent, and scoring the opening is separate work.
- Any change to the pause, save/load or confirm screens.
- Any change to the palette allocation, the strobe, or the lightning system.
