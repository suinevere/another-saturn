---
name: user-runs-the-emulator
description: Suinevere runs Mednafen themselves — never launch it from a tool call; build the disc and hand it over.
metadata:
  type: feedback
---

**Do not run Mednafen.** Build the disc, then tell Suinevere it is ready and let them
launch it. Stated directly on 2026-07-27: *"Pass off all runs of mednafen to me!!!"*

**Why:** screen-scraping the emulator is unreliable and was actively misleading — three
captures in a row came back black while the game was in fact booting and rendering
correctly. Suinevere is sitting in front of the screen and can read it instantly and
accurately. Automated capture wasted a long stretch of the session chasing a bug that
did not exist, and nearly sent me debugging a working video backend.

**How to apply:**
- Build with `saturn/compile.bat debug`, confirm `BuildDrop/<CD_NAME>.cue` exists, and
  report what changed and what to look for.
- Ask what they see rather than inferring it.
- The same caution applies generally: treat a screenshot that contradicts other evidence
  as a suspect observation, not a finding.

Related: [[mednafen-bios-location]].
