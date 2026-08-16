---
name: user-runs-the-emulator
description: Suinevere builds and runs everything themselves — never invoke compile.bat, make, or Mednafen from a tool call; write the code and hand it over.
metadata:
  type: feedback
---

**Do not build and do not run Mednafen.** Write the code changes, say what changed and
what to look for, and stop. Suinevere builds the disc and runs it.

Two separate instructions, a fortnight apart, both emphatic:

- 2026-07-27, on the emulator: *"Pass off all runs of mednafen to me!!!"*
- 2026-08-16, on the build: *"I build, you don't EVER EVER BUILD."*

**Why:** screen-scraping the emulator is unreliable and was actively misleading — three
captures in a row came back black while the game was in fact booting and rendering
correctly. Suinevere is sitting in front of the screen and can read it instantly and
accurately. Automated capture wasted a long stretch of a session chasing a bug that did
not exist. The build half is theirs for the same reason: the loop is theirs end to end,
and an agent-side build only adds a slow, redundant step to it.

**How to apply:**
- Never run `saturn/compile.bat`, never run `make` for a real target, never launch the
  emulator.
- Syntax-checking is still mine and is expected: `-fsyntax-only` with the flags from
  `make -n src/<file>.o`. That produces no artifacts and catches a broken handoff before
  it costs them a build. The compiler is at `SaturnRingLib/Compiler/sh2eb-elf/bin/`, not
  on PATH.
- Hand off with what changed and what to look for, then stop. Do not ask permission to
  build; do not offer to build.
- Ask what they see rather than inferring it. **When they report an observation, take it
  as accurate** — stated 2026-08-16: *"I've always tested correctly."*
- Treat a screenshot that contradicts other evidence as a suspect observation, not a
  finding.

Related: [[mednafen-bios-location]], [[srl-build-system]].
