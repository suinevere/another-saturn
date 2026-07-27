# Another World → Sega Saturn — Video Backend Design Spec

**Date:** 2026-07-27
**Status:** Draft, pending review
**Target engine:** SaturnRingLib (SRL)

## Goal

Put Another World's picture on screen: implement the display half of the `System`
interface (`setPalette`, `updateDisplay`) on Saturn hardware, so the engine that
already compiles and links for SH-2 can render its 320x200 16-colour output.

This is the one part of the port with no precedent in the zaturn blueprint — zaturn
was text-only via `SRL::Debug::Print` and never touched a bitmap layer.

## The finding that drives every decision

**The engine never draws to hardware. It hands the backend a finished framebuffer.**

`video.cxx` rasterizes entirely into plain memory: `Video::init` does a single
`malloc(4 * VID_PAGE_SIZE)` (4 pages x 32,000 bytes, 320x200 at 4bpp) and every
drawing primitive — `fillPolygon`, `drawLineN/P/Blend`, `drawChar`, `drawPoint`,
`copyPage` — reads and writes those bytes with the CPU. The only two calls that ever
leave the engine are:

```
Video::changePal(n)      -> sys->setPalette(p)       // 16 colours, 32 bytes
Video::updateDisplay(id) -> sys->updateDisplay(page) // one 32,000-byte 4bpp page
```

So the backend's entire job is: take a 320x200 4bpp buffer and 16 colours, and make
them visible. Nothing else about the renderer needs to change.

## Decision: VDP2 paletted-16 bitmap layer, software rasterizer kept as-is

**Recommended.** Keep `video.cxx` untouched and blit its page to an NBG bitmap layer
configured as `CRAM::TextureColorMode::Paletted16`, allocated via
`SRL::VDP2::VRAM::AutoAllocateBmp`.

Three properties of the engine make VDP1 the wrong tool, and they are not stylistic:

1. **`drawLineBlend` reads the framebuffer back.** The game's transparency effect
   (`video.cxx:341`) samples `_pages[0]` at the destination and blends against what
   is already there. VDP1 draws into a framebuffer the CPU cannot practically read
   mid-frame, so this effect has no VDP1 equivalent without a full readback.
2. **The four pages are VM state, not scratch.** `copyPage(src, dst, vscroll)` does
   whole-page copies with a vertical scroll offset, `updateDisplay(0xFF)` swaps two
   page *pointers*, and `saveOrLoad` serializes all four pages plus which pointer is
   which. Page identity is part of the savegame format. A VDP1 display list has no
   equivalent of "page 2 as it stood three frames ago".
3. **The rasterizer's fill rule is the game's look.** `fillPolygon` walks
   `_interpTable` (a precomputed division table) with the original's exact stepping.
   VDP1's quad rasterization would not reproduce it, and mismatches show up as
   shimmering edges on the game's large flat polygons.

Rewriting all of that onto VDP1 means replacing `video.cxx` wholesale — a different
project from porting one. The software rasterizer's cost is CPU time we are already
paying on any path.

### A convenient alignment

The engine packs its 4bpp pixels **high nibble = left pixel, low nibble = right**
(`sysImplementation.cxx:120-125` unpacks `>>4` then `&0xF`), which is the same order
VDP2 reads a 16-colour bitmap in. **No nibble swapping is needed** — the blit is a
straight byte copy.

## Architecture

| File | Language | Responsibility |
|------|----------|----------------|
| `src/video.cxx` | C++ | Unchanged. Software rasterizer, 4 pages, palette selection. |
| `src/system/saturn_video.cxx` | C++ | New. Owns the VDP2 bitmap layer: setup, palette upload, per-frame blit. |
| `src/system/saturn_video.h` | C++ | Its interface, called only from the `System` backend. |
| `src/sysImplementation.cxx` | C++ | Replaced by `src/system/saturn_system.cxx` — the `System` subclass, and the `stub` pointer the whole program links against. |

`main.cxx:46` declares `extern System *stub;` and today `sysImplementation.cxx:304`
defines it. That single pointer is the only unresolved symbol in the current build:
providing a Saturn `System` completes the link.

## Components

### The bitmap layer

VDP2 bitmap layers come in fixed sizes — 512x256 is the smallest that contains
320x200. At `Paletted16` that is `512 * 256 / 2` = **65,536 bytes of VDP2 VRAM**.
The 320x200 image occupies the top-left corner; the rest is never displayed.

Consequence: **source and destination pitches differ.** The engine's page is 160
bytes per line; the bitmap's stride is 256 bytes (512 pixels at 4bpp). The blit is
therefore 200 per-line copies of 160 bytes, not one `memcpy`.

Use NBG0 or NBG1 (only those two support bitmap mode). Note SRL's own warning in
`srl_vdp2.hpp`: when NBG1 is deeper than 8bpp NBG3 stops displaying, and NBG3 is
where `SRL::Debug::Print` goes by default. At 4bpp we are well under that, so debug
text stays available — worth keeping during bring-up given that
[the engine's diagnostic output is currently discarded](../../../mem/srl-libc-shadowing.md).

### `setPalette`

The incoming buffer is **16 colours x 2 bytes**, and despite the comment in
`sysImplementation.cxx` saying "565" the code below it reads **4 bits per channel**:
`R = c1 & 0x0F`, `G = (c2 & 0xF0) >> 4`, `B = c2 & 0x0F`.

4-bit channels map onto Saturn RGB555 by a single left shift per channel, so the
conversion is exact and cheap — no rounding, no lookup table:

```
HighColor(r << 1, g << 1, b << 1)   // or the packed form: 0x8000 | (b<<11) | (g<<6) | (r<<1)
```

16 entries land in one CRAM bank. The engine changes palette mid-scene (fades, flash
effects), so this must be cheap; it is 16 word writes.

### `updateDisplay` and frame pacing

The engine calls `updateDisplay` when *it* decides a frame is done, which is not tied
to vblank. Writing to VDP2 VRAM while the display is reading it tears. Two options,
in preference order:

1. **Double-buffer the bitmap** — two 64 KB allocations, blit into the back one and
   flip the layer's base address at vblank. Costs 128 KB of VDP2 VRAM total.
2. **Blit inside the vblank window** — no extra VRAM, but 32,000 bytes is more than
   a vblank comfortably absorbs by CPU, so this leans on DMA landing in budget.

Start with (1) if VRAM allows; it decouples engine timing from the display entirely.

### The blit itself

32,000 bytes per displayed frame. At 60 Hz that is ~1.9 MB/s sustained. A CPU copy
loop on a 28.6 MHz SH-2 is a real cost against the rasterizer's own budget, so use
**SCU DMA** (`srl_scu.hpp`) with one transfer per line, or a single transfer if the
destination pitch can be made to match. Measure before optimizing — the rasterizer,
not the blit, may well dominate.

## Memory

| Where | What | Size |
|-------|------|------|
| High Work RAM | 4 engine pages (`malloc` in `Video::init`) | 128,000 B |
| VDP2 VRAM | 512x256 Paletted16 bitmap | 65,536 B (x2 if double-buffered) |
| CRAM | 16 colours | 32 B |

128 KB of the 1 MB High Work RAM arena, before the resource loader's own allocations.
Worth confirming against `Resource`'s budget early — Another World's memory list
loads banks into the same heap.

## Resolution and aspect

The game is 320x200; NTSC output is 320x224. Recommend **letterboxing**: centre the
image with 12 blank lines top and bottom. It preserves the rasterizer's coordinate
space exactly, so no drawing code changes and no scaling artifacts appear on the
game's large flat colour areas. VDP2 vertical scroll positions it; the border is the
layer's backdrop colour.

Vertical stretch to 224 lines is possible via VDP2 scaling but resamples every line
of a 16-colour image, which on flat-shaded art produces visible banding on the
boundaries. Not recommended.

## Deferred / out of scope for this milestone

- **Audio** (`mixer.cxx`, `sfxplayer.cxx`) — separate spec, onto `SRL::Sound::Pcm`.
- **Input** (`processEvents`, `PlayerInput`) — separate spec, onto `SRL::Input`.
- **File I/O** — `File_impl` on `SRL::Cd::File`; currently the failing stubs in
  `saturn_filestub.c`, so nothing loads yet. Needed before anything renders at all.
- **Save/restore** to backup RAM.
- The `copyPage(const uint8_t *src)` bitplane path (background images stored as four
  bitplanes) is engine-side and already works; it needs no backend support.

## Risks

- **VRAM bank cycle patterns.** VDP2 bitmap layers have per-bank access-cycle
  requirements; `AutoAllocateBmp` handles the common cases but a 4bpp bitmap plus
  NBG3 debug text may contend. Symptom would be a corrupted or blank layer, not a
  crash.
- **Blit cost.** If DMA-per-line overhead dominates, fall back to fewer, larger
  transfers by matching pitches (padding the engine page to 256 bytes per line would
  cost 12,800 B per page but make the copy a single contiguous DMA). This changes
  `VID_PAGE_SIZE` and therefore the savegame layout, so decide before saves exist.
- **Nothing is visible yet.** With file I/O stubbed to fail, the first build that
  links will render whatever an empty page contains. Bring up CD loading alongside
  this, or the video backend cannot be tested at all.

## Acceptance

The title screen (part `GAME_PART_FIRST`) renders correctly in Mednafen, with the
palette matching the host build's output for the same frame.
