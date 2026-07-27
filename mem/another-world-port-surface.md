---
name: another-world-port-surface
description: Where the Another World / raw engine has to be cut for the Saturn — the System abstract class is the single seam, plus the video/audio/file specifics.
metadata:
  type: project
---

The engine in `saturn/*.cpp|*.h` (Gregory Montoir's `raw`, cleaned up by Fabien
Sanglard) is 13 translation units: `bank, engine, file, main, mixer, parts, resource,
serializer, sfxplayer, staticres, sysImplementation, util, video` + `vm`.

**Why this matters:** unlike MojoZork (which needed three surgical edits), `raw` already
has a *designed* porting seam, so the [[zaturn-port-blueprint]] "leave the core alone,
add a thin SRL frontend" rule maps cleanly.

**How to apply:**

## The seam: `struct System` in `saturn/sys.h`

A pure-virtual class explicitly documented as "an abstract class so any find of system
can be plugged underneath". `sysImplementation.cpp` is the SDL2 implementation and is
**the only file that should be replaced** — write a `SaturnSystem : System` in its place.
The interface to satisfy:

```
init/destroy
setPalette(const uint8_t *buf)        // NUM_COLORS = 16, BYTE_PER_PIXEL = 3
updateDisplay(const uint8_t *buf)
processEvents / sleep(ms) / getTimeStamp
startAudio(AudioCallback, param) / stopAudio / getOutputSampleRate
addTimer(delay, TimerCallback, param) / removeTimer
createMutex / destroyMutex / lockMutex / unlockMutex
PlayerInput input;                    // dirMask, button, code, pause, quit,
                                      // lastChar, save, load, stateSlot
```

`processEvents` is where SRL pad reading goes (`SRL::Input::Digital`); the SDL2 version's
keyboard-only actions (`C` code entry, save/load slots, TAB scale) need a pad or on-screen
mapping. `MutexStack` (RAII wrapper in `sys.h`) exists because SDL2 ran audio on a thread —
on Saturn the mutex ops can very likely become no-ops once audio is driven from vblank.

## Video — decided 2026-07-27, spec written

`video.h`: `VID_PAGE_SIZE = 320 * 200 / 2` — **four 4-bit-per-pixel pages** of 320x200,
16-color palette, software polygon rasterizer.

**Decision: keep the software rasterizer untouched, blit its page to a VDP2
Paletted16 bitmap layer.** Full rationale in
`docs/superpowers/specs/2026-07-27-another-world-video-backend-design.md`. The short
version — the engine never touches hardware, it hands the backend a finished
framebuffer via exactly two calls (`sys->setPalette`, `sys->updateDisplay`). VDP1 is
wrong because `drawLineBlend` reads the framebuffer back, the four pages are
serialized VM state (page identity is in the savegame), and `fillPolygon`'s
`_interpTable` stepping *is* the game's visual signature.

Facts worth not re-deriving:
- 4bpp nibble order (high = left pixel) already matches VDP2, so **no nibble swap**.
- The palette is 16 x 2 bytes at **4 bits per channel** — `R = c1 & 0x0F`,
  `G = (c2 & 0xF0) >> 4`, `B = c2 & 0x0F`. The "565" comment in `sysImplementation.cxx`
  is wrong; read the code under it. 4-bit channels map to RGB555 by one shift each.
- VDP2 bitmaps are fixed-size, so 320x200 lives in a 512x256 layer: source pitch 160
  bytes vs destination stride 256, i.e. 200 per-line copies, not one memcpy.

## Audio

`mixer.cpp` (4-channel software mixer fed by `startAudio`'s callback) + `sfxplayer.cpp`
(module player driven by `addTimer`). Maps onto `SRL::Sound::Pcm`; `SRL::Sound::Cdda`
is available if music is moved to CD-DA.

## Libc / file surface (small — good news)

Whole-tree usage is only: `free`(34) `memset`(18) `assert`(11) `time`(4) `sprintf`(3)
`malloc`(3) `printf`(2) `memcpy`(2), and exactly one each of
`fopen/fread/fseek/fclose/fwrite/gzopen`. External includes are just `<SDL.h>` (in
`sysImplementation.cpp`) and `"zlib.h"` (in `file.cpp`).

- `file.cpp` is already abstracted (`File` class over stdio/gzip) → reimplement on
  `SRL::Cd::File`, and drop the zlib path if the data files are stored uncompressed.
- `malloc/free` route to `SRL::Memory::HighWorkRam` TLSF exactly as zaturn's
  `saturn_compat.cxx` does.
- `time()` for RNG seeding → SMPC clock / `SRL::Timer`, same substitution zaturn made.

## Game data

Needs `MEMLIST.BIN` + `BANK*` from the English PC DOS release, placed under
`saturn/cd/data/`. Non-open-source — must be gitignored, as zaturn does for `*.Z3`.
