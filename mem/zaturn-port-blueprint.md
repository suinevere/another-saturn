---
name: zaturn-port-blueprint
description: The zaturn (MojoZork→Saturn) repo is the reference blueprint Another-Saturn is carbon-copying; layout, submodule strategy, and porting philosophy.
metadata:
  type: reference
---

`C:\Users\saggl\CLionProjects\zaturn` is Suinevere's Sega Saturn port of icculus's
MojoZork Z-Machine, built on **SaturnRingLib (SRL)**. Another-Saturn copies its
approach for the Another World / raw engine. See [[another-saturn-current-state]].

## Repo layout being copied

```
<repo>/
├── README.md            long-form: prerequisites, clone, toolchain, build, run, release
├── .gitignore           ignores BuildDrop/, *.o/.elf/.iso/.cue, SaturnRingLib/Compiler/
├── .gitmodules          SaturnRingLib only
├── SaturnRingLib/       git submodule → ReyeMe/SaturnRingLib (1.3 GB SDK, pinned commit)
├── CMakeLists.txt       IDE-indexing ONLY (see [[srl-cmake-indexing-target]])
├── saturn/              the port itself
│   ├── src/             per-concern subfolders: engine/ video/ sound/ net/ input/ menu/ system/
│   ├── tests/           host-side unit tests built with plain gcc (no Saturn needed)
│   ├── cd/data/         disc contents (0.bin, ABS/BIB/CPY.TXT, SDDRVS.DAT/.TSK, game assets)
│   ├── makefile         SRL config + SOURCES glob + `include $(SDK_ROOT)/shared.mk`
│   ├── compile.bat      dual-shell (sh + cmd) build script
│   ├── clean.bat
│   └── run_with_mednafen.bat
├── docker/              server-side hosting (zaturn-specific, N/A for Another World)
├── docs/
│   ├── fork-setup/SETUP.md   the literal "reshape a repo into this layout" recipe
│   └── superpowers/{specs,plans}/  YYYY-MM-DD-<topic>-design.md + matching plan
└── tools/               Python/bat asset pipeline (PNG→TGA, audio→PCM), own .venv
```

## Porting philosophy (from docs/superpowers/specs/2026-07-03-mojozork-saturn-port-design.md)

- **Keep the upstream engine core nearly unmodified.** MojoZork got exactly three
  edits, all reusing hooks upstream already had (function pointers + an `#if
  !defined(...)` platform guard). Everything Saturn-specific lives in new files.
- **A thin C++ SRL frontend wraps a C core.** SRL's makefile globs both `*.c` and
  `*.cxx`; the C core links to `main.cxx` via `extern "C"` declarations collected in
  a `*_glue.h`.
- **SRL was chosen over Jo Engine** because `main()` owns an explicit
  `while(1){ … SRL::Core::Synchronize(); }` loop — synchronous engines map directly,
  no callback/event-machine refactor.
- **Every spec is written before the plan, and the plan before the code**, under
  `docs/superpowers/specs/` and `docs/superpowers/plans/`, date-prefixed.

## SRL APIs the port leans on

`SRL::Core::{Initialize,Synchronize,OnVblank}`, `SRL::Debug::Print(col,row,fmt,…)`,
`SRL::VDP`, `SRL::Cd::{File,TableOfContents}`, `SRL::Memory::{HighWorkRam,LowWorkRam}::
{Malloc,Free,Realloc}` (TLSF), `SRL::Bitmap::{Palette,BitmapInfo,IBitmap}`,
`SRL::Types::HighColor` (RGB555), `SRL::Input::{Digital,Analog,Management::GetRawData}`,
`SRL::Sound::{Pcm,Cdda}`.

Compiler shims live in `src/engine/saturn_compat.{h,cxx}` — the SRL dummy headers omit
`malloc/free/realloc/strdup`, `SEEK_*`, and a `FILE` API, so the port declares them and
routes the heap onto `SRL::Memory::HighWorkRam` TLSF. Always-failing `fopen/fread/…`
stubs in `saturn_filestub.c` exist purely so never-executed stdio paths still link.
