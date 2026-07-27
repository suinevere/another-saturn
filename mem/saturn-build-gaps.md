---
name: saturn-build-gaps
description: Build status as of 2026-07-27 — scripts adapted, all 15 engine TUs compile for SH-2, and the whole program links except the one symbol that is the port's substitution point.
metadata:
  type: project
---

**Why:** records how far the build actually gets, so the state isn't re-derived. The
original list of six script-level gaps is resolved; what remains is the port itself.

**How to apply:**

## Build scripts (done)

- Engine sources `git mv`'d `saturn/*.cpp|*.h` → `saturn/src/*.cxx|*.h` (flat).
  **The `.cpp` → `.cxx` rename is forced by the SDK:** `shared.mk` derives objects via
  `$(SOURCES:.c=.o)` then `:.cxx=.o` and has pattern rules for only `%.c` and `%.cxx`.
  A `.cpp` maps to no object name and no rule, so it drops out of the link silently.
- `makefile`: dropped the `NETBIN` block; `CD_NAME = Another World (USA)`.
- `compile.bat`: dropped both netbin passes and the `CALL ..\tools\assets\pvms.bat`.
  `compile-netbin.bat` deleted; `run_with_mednafen.bat` retargeted.
- Root `.gitignore` gained Saturn build output and the non-free `BANK*`/`MEMLIST.BIN`.
- `saturn/cd/data/{0.bin,SDDRVS.DAT}` untracked — they are build outputs that
  `make clean` deletes and the build regenerates.

## Engine compile blockers (done)

- **endian.h** had no SH-2 case, *and* its whole detection block sat behind
  `AUTO_DETECT_PLATFORM`, which only the old host CMake build defined — so the
  `#error` fired regardless. SH-2 is now detected before and independently of that
  gate. It selects `SYS_BIG_ENDIAN` **plus a new `SYS_NO_UNALIGNED_ACCESS`**, which
  forces the byte-assembling `READ_BE_*` path. This is load-bearing: the SH-2 raises
  an address error on unaligned word/long loads, and `Ptr::fetchWord` (intern.h:49)
  reads words from a byte-stepped bytecode pointer while `sfxplayer.cxx` walks packed
  pattern data the same way, so odd addresses are routine. The "native" direct-load
  path would fault almost immediately.
- **libc/linkage shims** — `saturn_compat.{h,cxx}`, `saturn_filestub.c`,
  `saturn_new.cxx`. The five traps behind these are in [[srl-libc-shadowing]].
- **zlib** guarded behind `USE_ZLIB` (off on Saturn) in `file.cxx`. Another World's
  data files are in the game's own packed format; nothing needs gzip.

## Status: it boots and runs (2026-07-27)

The disc builds and the game runs on Saturn. CD loading, the palette, and the VDP2
blit are all confirmed working by Suinevere watching the emulator.

Delivered since:
- `src/system/saturn_cdfile.{h,cxx}` — CD reads on `SRL::Cd::File`.
- `src/system/saturn_platform.{h,cxx}` — SRL bring-up, NBG0 bitmap, pad, vblank clock.
- `src/system/saturn_system.cxx` — `SaturnSystem : System` and the `stub` definition.
- `sysImplementation.cxx` (SDL2) moved to `saturn/host/`, out of the build.
- `-DBYPASS_PROTECTION` added to the makefile: without it `engine.cxx` starts at
  `GAME_PART1`, the copy-protection wheel screen, which is unplayable without the
  physical wheel. `GAME_PART2` (0x3E81) is the intro.
- `printf`/`fprintf` now render to SRL's debug text layer, so `error()`/`warning()`
  are visible instead of being a silent halt.

Still stubbed: audio (`startAudio` no-op, `getOutputSampleRate` returns 22050 to avoid
a divide-by-zero in `Mixer::playChannel`), `addTimer` (so no music), saves, and the
keyboard-only actions (code entry, quit).

See [[another-world-port-surface]] and [[srl-libc-shadowing]].
