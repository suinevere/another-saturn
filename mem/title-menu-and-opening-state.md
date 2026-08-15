---
name: title-menu-and-opening-state
description: STALE as of 2026-08-06 — superseded by [[opening-colour-pipeline]]. Kept for the human rulings and traps it records; its account of the opening encoder and of the open problems no longer holds.
metadata:
  type: project
---

> **STALE.** Superseded by [[opening-colour-pipeline]] at `5630d2d`. The opening encoder
> described below has been replaced, and both entries under "The open problem" were
> resolved by other means: the artifacts were the encoder's palette handling rather than
> the vblank ordering, and the stream now runs at 37 KB/s against the 128 KB/s that made
> smoothness a problem in the first place. The human rulings and the trap list below are
> still accurate.

Repo `C:\Users\saggl\CLionProjects\Another-Saturn`, branch `main`.
**HEAD == origin/main == `ec2619e`.** Everything is pushed.

Supersedes [[another-saturn-current-state]], which is stale — it describes commit
43ed0e5 from 2026-07-27 and still calls the build scripts "staged but uncommitted".

The human deliberately chose to work on `main` rather than a feature branch, and
pushed. Do not "fix" that.

## What shipped

**Title menu artwork.** Extracted Mega Drive wordmark, hand-authored chrome
`START GAME` / `LOAD GAME` strings, and a luminance-remapped freeze behind the pause
screens. The random lightning and the strobing selected row shipped with it but were
removed later: the card is now static, and selection reads from the base-8/base-12
ramp split alone.

**Streamed title opening.** 383 frames of the Mega Drive intro decoded from
`OPENING.BIN` at 25 fps, handing off to the menu.

Specs, plans and the full decision record live in the repo — read them rather than
re-deriving:

- `docs/superpowers/specs/2026-08-03-title-menu-sprites-design.md`
- `docs/superpowers/plans/2026-08-03-title-menu-sprites.md`
- `.superpowers/sdd/2026-08-03-title-menu-sprites/progress.md`
- `docs/superpowers/specs/2026-08-04-title-opening-animation-design.md`
- `docs/superpowers/plans/2026-08-04-title-opening-animation.md`
- `.superpowers/sdd/2026-08-04-title-opening-animation/progress.md`

The two SDD ledgers carry every human ruling and every deferred minor, including the
reasoning behind decisions that look arbitrary in the code: why playback stops at
frame 382, why the opening plays once per boot, why the backdrop is copied rather
than blitted. `.superpowers/` is git-ignored, so those ledgers are local only.

## The open problem

The opening runs, but the human reported it is **not smooth and shows artifacts**.
Two separate causes.

**Artifacts — fixed but NOT confirmed.** Commits `449b0f7` and `ec2619e` reordered
`sat_video_present` to wait for vblank *before* its writes, and made
`sat_video_set_palette` mark-pending so the CRAM write lands in the same blanking
window as the page DMA. Previously the 32000-byte DMA crossed the beam mid-scan and a
new palette could appear over the previous frame's pixels. Nobody has watched it
since. Running the ISO and comparing is the first thing worth doing.

**Smoothness — untouched, needs a design decision.** The per-frame budget is 40 ms;
the work is an 8 KB blocking `sat_cd_read` (~27 ms at a 2x drive's ~300 KB/s) plus
~7 ms decode plus the DMA. About 35 ms with no margin. This is arithmetic, not a bug:
a blocking disc read sits on the critical path. Fixing it means asynchronous reads or
fewer bytes per frame, which deserves a spec rather than a patch.

## Already ruled out — do not re-investigate

- **The stream is not corrupt.** `OPENING.BIN` was decoded offline: all 383 frames
  reconstruct to exactly 32000 bytes and render correctly.
- **Palette instability is not the artifact source.** Measured 1.8 of 16 slots moving
  per consecutive frame, and decoded-vs-source temporal change of only 1.20x. An
  earlier hypothesis blaming per-frame quantisation was tested and killed.
- **The ring buffer is fixed**, over two rounds: the read-ahead top-up was dead code
  (`room` permanently 0, no compaction), and the first fix for that crashed on the
  skip path via an unbounded `memmove` size. Final guard is
  `start > ringPos && start < ringPos + ringLen`.

## Traps that already bit

- `menuRenderFrame`'s `overlay == false` branch **looks dead but is not** — it is the
  non-overlay slot/confirm dispatch `runTitle` uses when browsing saves from the title
  screen. Deleting it re-introduces a Critical bug that shipped once already.
- **Nothing automated reaches `opening.cxx` or `menu.cxx`** — both sit outside the
  host-testable boundary by design. Green tests say nothing about playback, pacing,
  skip, or the menu handoff. Every bug found in those files was found by reading or by
  running, never by a test.
- A **keyframe is a delta against an all-zero page**, which is why the decoder has one
  code path and the player clears the page before applying one.
- `tools/opening_frames.py` single-sources the stop frame (382); both `mkopening.py`
  and `mkmenuart.py` import it. Do not reintroduce a second literal.

## Environment

Build invocation is covered in [[srl-build-system]], with one correction learned the
hard way: from PowerShell use `cmd.exe /c ".\compile.bat debug"` in the `saturn`
directory — the leading `.\` is required, and running it from Bash fails with
`Cannot create temporary file in C:\WINDOWS\: Permission denied` because the native
toolchain reads the Windows `%TMP%`, not Bash's. Exporting `TMPDIR` does not help.
`make` runs `clean` first, so a failed build leaves `BuildDrop/` empty.

Host tests: `sh saturn/tests/run_tests.sh` from the repo root, 10 suites.
Emulator: see [[user-runs-the-emulator]].

The Claude Code permission classifier intermittently blocks Agent dispatches — four
times across two sessions, always on review subagents. When it does, verify by reading
the code and record it as a *controller* check, which is weaker evidence than an
independent review.

`origin` URL is stale: GitHub reports the repo moved to
`https://github.com/suinevere/another-saturn.git` (lowercase). Pushes work by redirect.
