---
name: srl-cmake-indexing-target
description: zaturn's root CMakeLists.txt is an IDE-indexing-only target (never a real build) — and the two non-obvious traps it documents.
metadata:
  type: reference
---

zaturn's root `CMakeLists.txt` exists **purely so CLion can resolve `#include`s** across
`saturn/src/` and the SaturnRingLib submodule. The real build is the SH-2 makefile
([[srl-build-system]]). Another-Saturn's root `CMakeLists.txt` is still the upstream SDL2
`raw` build and should be replaced with this pattern.

Two hard-won details are baked into it, both worth preserving verbatim:

1. **It must be `add_library(<name> OBJECT EXCLUDE_FROM_ALL ${sources})`, not
   `add_custom_target(… SOURCES …)`.** CMake's File API — what CLion reads for include
   resolution — only emits a *compile group* (the real per-target include-path/flags set)
   for targets it considers genuinely compilable. `add_custom_target` SOURCES are IDE-tree
   entries with no compile group, which showed every file as having an empty search path.
   `OBJECT` never links; `EXCLUDE_FROM_ALL` keeps it out of "Build Project".

2. **Add only the host's own bundled compiler headers.** `SaturnRingLib/Compiler` ships
   prebuilt toolchains for several hosts (`sh2eb-elf/` for Windows, `mac/`, `linux/`);
   only the one `setup_compiler.bat` installed has real content. Adding all three shadows
   this host's `<stdint.h>`/`<string>` with another platform's copy and breaks resolution.
   Select by `if(WIN32)/elseif(APPLE)/else()`.

Include paths mirror the makefile: every directory under `saturn/src` (globbed, not
hand-listed) plus `SaturnRingLib/saturnringlib`, `modules/dummy`, `modules/SaturnMathPP`,
`modules/sgl/INC`, `modules/tlsf`. Source globs use `CONFIGURE_DEPENDS` so the list can't
drift when files move.
