---
name: srl-build-system
description: How the zaturn/Another-Saturn SRL build actually works — makefile knobs, the dual-shell compile.bat, and why the project sits beside the SDK instead of inside it.
metadata:
  type: reference
---

The bootable disc is produced by the SH-2 cross-compiler makefile, **not** by CMake.
Entry point is `saturn/compile.bat debug|release|clean`.

## Non-standard SDK location (the key deviation)

A stock SaturnRingLib project lives at `SaturnRingLib/Projects/<name>` and reaches the
SDK via `../..`. This project lives at `<repo>/saturn` and reaches the sibling submodule
via `../SaturnRingLib`. Therefore:

- `makefile`: `SRL_INSTALL_ROOT ?= ../SaturnRingLib` (stock is `?= ../..`), then
  `SDK_ROOT = $(SRL_INSTALL_ROOT)/saturnringlib` and `include $(SDK_ROOT)/shared.mk`.
- `compile.bat`/`clean.bat` set `SRL_INSTALL_ROOT` and put the toolchain on `PATH`
  themselves using absolute `%~dp0` paths, then call `make` directly. They deliberately
  do **not** use the SDK's `make.bat` — its `SET COMPILER_DIR=%2` captures the
  surrounding quotes into `PATH` and breaks executable lookup.
- PATH prepended: `%~dp0..\SaturnRingLib\Compiler\{sh2eb-elf\bin, msys2\usr\bin, Other Utilities}`.

## compile.bat is a polyglot script

Line 1 begins `:;` — POSIX shells execute that line (and `exit`), while `cmd.exe`
treats `:;` as a label and falls through to the `@ECHO Off` block below. So the same
file runs as both `bash compile.bat` and `compile.bat`. `clean.bat` uses the same trick.

## makefile knobs at the top (per-project tuning)

```
SRL_MAX_TEXTURES, SRL_MODE (NTSC|PAL), SRL_HIGH_RES (480i), SRL_FRAMERATE,
SRL_MAX_CD_BACKGROUND_JOBS, SRL_MAX_CD_FILES, SRL_MAX_CD_RETRIES,
SRL_USE_SGL_SOUND_DRIVER, SRL_ENABLE_FREQ_ANALYSIS,
SGL_MAX_VERTICES / SGL_MAX_POLYGONS / SGL_MAX_EVENTS / SGL_MAX_WORKS,
CD_NAME      -> drives every BuildDrop artifact basename (.elf/.iso/.bin/.cue/.map)
                and the .cue's FILE line. Spaces/parens are safe: shared.mk only
                references BUILD_* inside recipes, always double-quoted.
BUILD_DROP = ./BuildDrop
```

Sources are found by `find src/ -name '*.c'` and `'*.cxx'`, so **engine sources must
live under `saturn/src/`**. Include paths come from
`SRL_CUSTOM_CCFLAGS += $(patsubst %,-I%,$(shell find src -type d))` — every subfolder
goes on `-I`, so bare `#include "foo.h"` resolves across folders with no path edits.

## Outputs

`saturn/BuildDrop/<CD_NAME>.{iso,bin,cue,elf,map}` — `.iso` is ISO9660 bootable,
`.bin` is MODE1/2352 raw for ODEs/burners. `shared.mk`'s `all:` target is
`clean-preserve-audio build`, so every invocation wipes and rebuilds those paths.

**Mednafen `M:S:F time … out of range` means stale `BuildDrop/`** — a process (emulator,
burner) held the old image open, the build appended instead of replacing, and track
offsets ran past the ~80-minute Red Book limit. Close it, `clean.bat`, rebuild. Not an
audio-track problem.

## Emulator

`run_with_mednafen.bat` expects a portable Mednafen at
`SaturnRingLib/emulators/mednafen/` with Saturn BIOS (`sega_101.bin`, `mpr-17933.bin`)
in its `firmware/` subfolder.

Related: [[saturn-build-gaps]], [[srl-cmake-indexing-target]].
