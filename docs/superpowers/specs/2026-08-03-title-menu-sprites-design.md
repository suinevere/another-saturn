# Another World → Sega Saturn — Title Menu Artwork Design Spec

**Date:** 2026-08-03
**Status:** Approved, ready to plan
**Target engine:** SaturnRingLib (SRL)
**Supersedes nothing.** Extends `2026-08-01-title-and-save-menus-design.md`, which
built the menu state machine, drawing primitives and slot handling this spec dresses.

## Goal

Replace the placeholder title card — engine 8×8 font, two palette entries, no
artwork — with the Sega Mega Drive title screen: the chrome "ANOTHER WORLD"
wordmark, lightning that strikes at intervals, and a selected menu row that
pulses while the unselected one sits dark. Carry the same palette and the same
selection feedback into the pause and save/load screens.

## The constraint that shapes everything

`sat_video_present` (saturn_platform.cxx:233) DMAs a 320×200 4bpp page straight
into VDP2 VRAM. There is no sprite layer, no second plane, and no per-region
palette. **Everything visible at any instant shares one 16-entry palette.**

Two consequences drive the whole design:

1. Artwork is not composited by hardware. It is blitted into the same
   `s_menuPage` byte buffer the text already draws into, and packs two pixels
   per byte exactly as `menuDrawChar` does.
2. The pause menu overlays a frozen game frame whose pixels already use all
   sixteen indices. Today `menu.cxx` works around this by dimming the palette
   and drawing everything in index 15 (menu.cxx:44). That leaves no entries for
   artwork, which is why the pause screen currently cannot look like the title
   screen.

**Decision: remap the frozen frame instead of dimming the palette.** On pause
entry the frozen page is pushed through a lookup table that collapses the
sixteen game colours into indices 1–3 by luminance. The backdrop becomes a dark
monochrome freeze, and indices 4–15 come free for artwork. Cost is one pass over
32000 bytes, once, at pause entry.

`menuDrawDimPalette` and its tests are deleted — the remap replaces them and
they would otherwise be dead code.

## Source artwork

Both Mega Drive references are native captures, not photographs or upscales:

| File | Size | Colours |
|---|---|---|
| `images/genesis.png` | 256×224 | 8 |
| `images/genesis lightning 3.png` | 256×224 | 8 |

So the wordmark is **extracted, not recreated**. Cropped from `genesis.png` at
(33,53)–(240,114) it is 207×61 and uses four colours: black, `#004849`,
`#256C6E`, `#499093`. The bolt is cropped from `genesis lightning 3.png` at
(210,0)–(256,63), 46×63, adding white and three lighter teals.

`images/genesis-thunder.png` is a 301×210 resampled capture carrying 2197
colours. It is reference only and is not a source for any asset.

### Pixel aspect

A 256×224 image displayed at 4:3 has a pixel aspect of 1.167; a 320×200 page has
0.833. Pasted unstretched the wordmark reads squat. It is therefore scaled
**1.4× horizontally** — 207 → 290, height unchanged at 61.

The stretch is a Lanczos resample **re-quantised back onto the source's own four
colours** with dithering off. Nearest-neighbour was rejected: at 1.4× a 1px rim
lands on one or two columns depending on phase, so vertical strokes vary in
weight. Re-quantising keeps stroke weight even and still yields exactly four
colours.

## Palette — all sixteen entries

Engine format is two bytes per entry: `R = byte0 & 0x0F`, `G = (byte1 & 0xF0) >> 4`,
`B = byte1 & 0x0F`.

The channel is four bits, so a source colour and the colour actually displayed
differ slightly. Both are given below; the artwork is quantised against the
source colours and the hardware shows the displayed ones.

| Index | Source | Displayed | Bytes | Use |
|---|---|---|---|---|
| 0 | `#000000` | `#000000` | `00 00` | Background and panel fill |
| 1 | — | `#111122` | `01 12` | Frozen frame, darkest |
| 2 | — | `#222233` | `02 23` | Frozen frame, mid |
| 3 | — | `#333344` | `03 34` | Frozen frame, lightest |
| 4 | `#004849` | `#004444` | `00 44` | Logo and bolts, shade 1 |
| 5 | `#256C6E` | `#226666` | `02 66` | Logo and bolts, shade 2 |
| 6 | `#499093` | `#449999` | `04 99` | Logo and bolts, shade 3 |
| 7 | — | `#114444` | `01 44` | Panel border |
| 8 | — | `#002222` | `00 22` | Unselected text, shade 1 |
| 9 | — | `#113333` | `01 33` | Unselected text, shade 2 |
| 10 | — | `#224444` | `02 44` | Unselected text, shade 3 |
| 11 | — | — | `00 00` | Spare |
| 12 | — | `#00AABB` | `00 AB` | Selected text, shade 1 — **strobed** |
| 13 | — | `#55DDEE` | `05 DE` | Selected text, shade 2 — **strobed** |
| 14 | — | `#BBFFFF` | `0B FF` | Selected text, shade 3 — **strobed** |
| 15 | `#FFFFFF` | `#FFFFFF` | `0F FF` | Bolt core and the flash |

### Why the ramp is split three ways

Selection is a **base-index change, not a redraw**. Text bitmaps store a
*relative shade* of 0–3 where 0 means transparent; the blitter adds a base of 8
(unselected) or 12 (selected). Because the logo lives at base 4, rewriting
entries 12–14 to pulse the selected row leaves the logo untouched.

The pulse therefore costs **three palette writes per frame and no drawing at
all**, and moving the cursor redraws only the two rows whose base changed.

## Assets

| Symbol | Size | Format | Pitch × rows | Bytes |
|---|---|---|---|---|
| `MENU_ART_LOGO` | 290×61 | 4bpp absolute | 145 × 61 | 8845 |
| `MENU_ART_BOLT[0..2]` | 46×63 | 4bpp absolute | 23 × 63 | 4347 |
| `MENU_ART_START_GAME` | 152×15 | 2bpp relative | 38 × 15 | 570 |
| `MENU_ART_LOAD_GAME` | 152×15 | 2bpp relative | 38 × 15 | 570 |
| `MENU_ART_STROBE` | 16 levels × 3 entries | palette bytes | — | 96 |
| `MENU_ART_PALETTE` | 16 entries | palette bytes | — | 32 |

**Total ≈ 14.5 KB**, linked into the binary in Low Work RAM alongside the
existing 600 KB resource block.

Two formats, for one reason: the logo and bolts never change colour, so they
store absolute indices. The two strings must render both selected and
unselected, so they store shade and take a base.

Bolt 0 is the extracted bolt, drawn at x=268. Bolt 1 is bolt 0 mirrored
horizontally, drawn at x=6. Bolt 2 is bolt 0 mirrored and cropped to 40 rows,
drawn at x=90. All three are **baked at build time**; the runtime never mirrors.

### The two chrome strings are the only hand-authored art

`START` and `PASSWORD` in `genesis.png` supply the letterforms, but the face
interlocks: `START` yields four ink runs for five letters, `PASSWORD` six for
eight. Kerning is bespoke, so glyphs cannot be cut apart and recombined into
arbitrary words without breaking the joins.

Since only two fixed strings are ever needed, each is authored as a **whole
bitmap** — artwork, not typography — at final size, using the extracted
letterforms as the basis and hand-fixing the joins. They live at
`saturn/art/chrome_start_game.png` and `saturn/art/chrome_load_game.png`,
indexed to four colours.

This is why the chrome face appears on the title screen only. Extending it to
the pause and save screens would require a full A–Z and digits drawn in a face
whose kerning is per-word, and the save-slot rows need 28 columns where the
14px face gives 22. Those screens keep the engine 8×8 font, recoloured into the
same base-8 and base-12 ramps, so they still darken and pulse.

## Screen layouts

**Title** — logo at x=15, y=30. `START GAME` at y=128, `LOAD GAME` at y=152,
both centred. No `>` cursor: selection is carried by the base index and the
pulse, as on the reference screen. Entries 1–3 are unused here.

**Pause and save/load** — unchanged geometry from
`2026-08-01-title-and-save-menus-design.md`. The `>` cursor is kept, because the
slot rows are dense enough that brightness alone is ambiguous. Panel fill is
index 0, border index 7.

## Runtime behaviour

### Freeze remap

On pause entry, build a 16-entry map from the saved game palette:

```
y = (r * 77 + g * 151 + b * 28) >> 8      /* 4-bit channels, y in 0..15 */
map[i] = y >> 2                            /* 0..3 */
```

Expand to a 256-entry byte table `lut8[b] = (map[b >> 4] << 4) | map[b & 15]`, then
remap the page one byte at a time. Building the byte table first is what keeps
it to a single pass over 32000 bytes with no per-pixel shifting.

Game colours in the bottom quarter of the luminance range map to index 0, so the
darkest parts of the frozen frame become true black rather than a near-black
grey. That is intended: it stops a dark scene reading as noise behind the panel.

### Strobe

`MENU_ART_STROBE` holds entries 12–14 at sixteen brightness levels, from 55% to
100% of the values in the palette table. A frame counter walks the table as a
triangle wave over 90 frames — about 1.5 s at 60 Hz — and the three entries are
written each frame. No runtime multiply.

### Lightning

Idle for a random 120–300 frames, then pick a bolt and run three frames:

| Frame | Bolt | Palette |
|---|---|---|
| 0 | drawn | flash — entries 1–14 brightened toward white, clamped at 15 |
| 1 | drawn | half-way between flash and normal |
| 2 | drawn | normal |
| 3 | erased | normal |

Entry 0 stays black and 15 stays white throughout. The flash **overrides the
strobe** for its three frames — entries 12–14 are written from the flash values,
not the strobe table — and the strobe resumes on frame 3 from wherever its
counter had reached.

**Every bolt overlaps the logo.** Bolt 0 at x=268 crosses the wordmark's right
edge and the ™; bolt 2 at x=90 crosses its upper left. So erasing is not a fill:
the bolt rectangle is filled with index 0 and then `MENU_ART_LOGO` is re-blitted,
which `menuBlit4bpp` already clips. Re-blitting all 8845 bytes once every few
seconds is cheaper than tracking the intersection.

Randomness comes from a local 16-bit LFSR seeded from the frame counter, so no
libc dependency is added.

Lightning runs on the title screen only. The pause screen is silent and still.

## Code

**New:**

- `saturn/src/menu_art.h` / `menu_art.cxx` — generated asset data and the
  palette. Committed, never hand-edited.
- `saturn/src/menu_blit.h` / `menu_blit.cxx` — `menuBlit4bpp(page, art, x, y)`
  and `menuBlit2bpp(page, art, x, y, base)`. Shade 0 is transparent in both.
  Clipped to the page like `menuDrawFill`, and free of engine dependencies so
  they are host-testable the way `menu_draw` is.

**Changed:**

- `menu_draw` — add `menuFreezeRemap(page, srcPalette)`; `menuDrawText` and
  `menuDrawChar` take a base index in place of a literal colour. The engine font
  is 1bpp, so it has no shades of its own: it renders at **`base + 2`**, the mid
  entry of whichever ramp is selected. `test_menu_draw.cxx` is updated for the
  new signatures.
- `menu.cxx` — `menuDrawTitleScreen` blits logo, strings and bolt;
  `runTitle` drives the bolt timer and the strobe; `runPause` calls
  `menuFreezeRemap` and installs `MENU_ART_PALETTE` instead of dimming.
  `s_titlePalette` is deleted, replaced by `MENU_ART_PALETTE`.

**Deleted:**

- `menuDrawDimPalette` and its tests.

## Build tooling

`tools/mkmenuart.py` reads `images/genesis.png`, `images/genesis lightning 3.png`
and the two authored PNGs under `saturn/art/`, performs the crops, the 1.4×
resample, the re-quantisation, the bolt mirroring and the 4bpp/2bpp packing, and
emits `saturn/src/menu_art.cxx`.

**The generated file is committed.** The Saturn build never invokes Python. The
tool exists so the artwork stays editable and reviewable rather than becoming
un-modifiable hex, and so a change to a source PNG is one command away from a
rebuilt asset.

## Testing

A new host suite `saturn/tests/test_menu_art.cxx`, built as
`run_tests_menuart` alongside the existing suites, covering:

- `menuBlit4bpp` — nibble packing at even and odd x, shade-0 transparency,
  clipping past all four edges, negative x and y.
- `menuBlit2bpp` — the same, plus that base 8 and base 12 produce indices
  differing by exactly 4 for identical source art.
- `menuFreezeRemap` — every output nibble lands in 0..3; a palette of identical
  entries maps uniformly; both nibbles of each byte are remapped.
- Strobe table — sixteen levels, monotonic in brightness, endpoints match the
  55% and 100% values.

Both binaries go in `.gitignore` next to `run_tests_menudraw`.

The artwork itself is not unit-testable; it is verified by looking at the
composed title screen.

## Risk

The two chrome strings are the only genuinely hand-authored art, and the piece
most likely to need iteration to sit convincingly next to the extracted logo.
**Build them first** and review a composed title screen before the bolt timer,
strobe and freeze remap are wired up. If they cannot be made to match, the
fallback is the engine 8×8 font in the base-8/base-12 ramps for those two rows —
which costs the title screen its chrome lettering but nothing else in this spec.

## Out of scope

- Thunder audio to accompany the lightning. No sound asset exists for it and the
  SfxPlayer work to schedule one is unrelated to the artwork.
- The Amiga isometric wordmark in `images/another_world_01.png`, and the box art.
  Reference only.
- Any change to save format, slot handling, or the menu state machine.
