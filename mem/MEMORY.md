# Memory index — Another-Saturn

- [zaturn port blueprint](zaturn-port-blueprint.md) — the reference repo being carbon-copied: layout, submodule strategy, porting philosophy, SRL APIs used.
- [Another-Saturn current state](another-saturn-current-state.md) — what commit 43ed0e5 already did, and what is staged but unadapted.
- [SRL build system](srl-build-system.md) — makefile knobs, the polyglot compile.bat, why the project sits beside the SDK, BuildDrop gotchas.
- [Saturn build gaps](saturn-build-gaps.md) — build status: scripts adapted, all 15 TUs compile for SH-2, one symbol left to link.
- [SRL libc shadowing](srl-libc-shadowing.md) — the five include-path/linkage traps that break ordinary C++ under SaturnRingLib, and the fix for each.
- [Another World port surface](another-world-port-surface.md) — `struct System` is the single seam; video/audio/file/libc specifics to reimplement.
- [SRL CMake indexing target](srl-cmake-indexing-target.md) — the IDE-only CMakeLists and its two non-obvious traps.
- [Suinevere conventions](suinevere-conventions.md) — banner comment format and the spec→plan→code cadence.
- [User runs the emulator](user-runs-the-emulator.md) — never launch Mednafen from a tool call; build the disc and hand it over.
- [Mednafen BIOS location](mednafen-bios-location.md) — it reads from ~/.mednafen/firmware, and where to find its real logs.
