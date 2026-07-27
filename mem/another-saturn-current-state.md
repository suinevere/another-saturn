---
name: another-saturn-current-state
description: What has already been done in the Another-Saturn repo as of 2026-07-27 — commit 43ed0e5 "Initial Saturn Port" and the staged build scripts.
metadata:
  type: project
---

Repo: `C:\Users\saggl\CLionProjects\Another-Saturn`, remote
`https://github.com/suinevere/Another-Saturn.git`. Branch `main` (note: `origin/HEAD`
still points at `master`; upstream history is Fabien Sanglard's `raw` / Another World
re-implementation by Gregory Montoir).

Goal: port the Another World VM to Sega Saturn by carbon-copying
[[zaturn-port-blueprint]].

## Done (commit 43ed0e5 "Initial Saturn Port", 2026-07-27)

- `SaturnRingLib` added as a submodule, pinned at `344c58c` (`0.9.2-107-g344c58c`),
  url `git@github.com:ReyeMe/SaturnRingLib.git` (zaturn uses the https form).
  **Already checked out and populated locally**, including
  `SaturnRingLib/Compiler/{sh2eb-elf,msys2,linux,mac}` and
  `SaturnRingLib/tools/bin/win` (iso2raw) — the toolchain is installed, no
  `setup_compiler.bat` run needed.
- All engine sources **moved `src/` → `saturn/`** (flat, at `saturn/` root — NOT yet
  into `saturn/src/` where the zaturn makefile expects them). See [[another-world-port-surface]].
- `saturn/cd/data/` seeded with the SRL disc skeleton: `0.bin`, `ABS.TXT`, `BIB.TXT`,
  `CPY.TXT`, `SDDRVS.DAT`, plus `saturn/cd/music/.gitignore`.
- `.idea/` (CLion) committed.

## Staged but uncommitted (as of 2026-07-27)

- `saturn/{makefile,compile.bat,clean.bat,run_with_mednafen.bat}` — copied from zaturn,
  then **adapted and verified to drive the SH-2 toolchain**. `compile-netbin.bat` deleted.
- Engine sources moved again: `saturn/*.cpp|*.h` → `saturn/src/*.cxx|*.h`.
- Root `.gitignore` extended with Saturn rules.

Details and the two engine blockers that surfaced on the first real compile are in
[[saturn-build-gaps]].

## Not yet copied from zaturn

Root `CMakeLists.txt` is still the original SDL2 `raw` host build, not the SRL
IDE-indexing target. No `docs/` (specs/plans), no `tools/` asset pipeline, no
`tests/`, no root `.gitignore` rules for Saturn build output.
