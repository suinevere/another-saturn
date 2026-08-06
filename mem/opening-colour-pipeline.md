---
name: opening-colour-pipeline
description: Session handoff as of 2026-08-06 at 5630d2d — the opening encoder was rebuilt around one shared palette from the source MP4, the title card shares it, and the one open item is a navy fringe that trades against stroke dashing.
metadata:
  type: project
---

Repo `C:\Users\saggl\CLionProjects\Another-Saturn`, branch `main`, **HEAD `5630d2d`**.
**Five commits are unpushed** (`dcba45d..5630d2d`); everything before that is on origin.

Supersedes [[title-menu-and-opening-state]], which is now stale — it describes `ec2619e`
and calls the artifacts fix "unconfirmed" and smoothness "untouched". Both were resolved
by different means than it predicts, and the encoder it describes no longer exists.

Fifteen commits, `353a083..5630d2d`. Read the messages rather than a summary here; each
one names what it changed and why. The reasoning that is *not* in them is below.

## What the work was

Two things shipped earlier — the title menu and the streamed opening — looked wrong on
hardware. This session was one long loop of the human reporting a visual defect, me
measuring it against the source, and adjusting `tools/mkopening.py` (and
`tools/mkmenuart.py`, which now shares its pipeline). Also two behaviour changes in
`saturn/src/menu.cxx` and `saturn/src/opening.cxx`: an idle attract loop and calmer
lightning.

The generated artefacts are `saturn/cd/data/OPENING.BIN` (git-ignored, regenerate on
demand) and `saturn/src/menu_art.cxx` (committed). Both need regenerating after any tool
change, and the human then rebuilds.

## The one open item

**A navy fringe on dark stroke edges.** Source `(14,20,21)` — nearly black, faintly teal
— renders as `(16,16,49)`, a navy. 7.22% of dark non-blue pixels do this.

It is structural, not a tuning miss. The blue card's ink *is* dark, so the slots reserved
for it sit in the same luminance band (roughly 10 to 40) as every other card's dark
edges, and one shared 16-colour palette with nearest-colour matching cannot tell them
apart. Verified it is **not** the reserve slot count: 2 and 3 give identical bleed.

One lever removes it — `DARK_SUM` 60 to 100, which drops those pixels to black outright.
That is arguably more faithful, but it takes the stroke-dashing measure from 0.011 to
0.019, and dashed letters were the human's original complaint, so it was left alone
deliberately. **This is the human's call to make, not a defect to go fix.**

The real way out is per-scene palettes, which reintroduces palette changes at scene
boundaries. That is a design decision, not a tweak — see the shimmer note below for why
palette changes were removed in the first place.

## What the numbers mean, and how to measure honestly

Several metrics in this area are actively misleading. Every one below cost an iteration.

- **A mean difference is useless on these frames.** They are five-sixths unchanging
  black, so swapping the bolt barely moves the mean. A mean-based run detector collapsed
  383 frames into 19 distinct images, and the same flaw in the title backdrop's matcher
  had it averaging in 22 cards that were not the last one. Count *changed samples*
  instead — indifferent to how much of the frame is background.
- **Clustering a 16-colour image is unstable.** A 5-cluster median cut over our own
  output moved enough between runs to point at parameter noise. Score per-pixel against
  the source frame instead; that is stable and is what "the colours are wrong" means.
- **Full-resolution reference numbers are not comparable to ours.** The screenshots are
  640x480; our page is a 2:1 box reduction, which averages saturated stroke pixels with
  the black beside them. The same logo reads blue-minus-green +47 at full res and +27
  through the reduction. Measure both sides through the identical reduction or the target
  is fiction.
- **`PALETTE_STRIP` is not neutral.** Every colour is floored at one replica, so a larger
  strip gives common colours proportionally more weight and suppresses rare ones. It was
  worth several units of error on the dim states.

## Traps that already bit

- **Population-weighted fitting always drops the rare hues.** The blue is ~1000 pixels on
  9 of 117 distinct pages; the tinted highlights are similarly thin. Both needed reserved
  slots, and both were silently lost twice — first to a chroma-only reserve that deepened
  teal then qualified for, then to a "blue leads green" test that teal satisfies by a
  point or two on far more pixels. A reserve needs a *large* lead to be safe.
- **The slot budget was over and clipped in silence.** 1 + 13 + 3 = 17 into 16 entries,
  dropping a fitted colour with no error. The counts must sum to exactly 16.
- **The MP4 is temporally noisier than the GIF** (wobble 0.171 against 0.096, h264
  inter-frame artifacts) but carries 8228 distinct colours per frame against 687. It only
  wins because run-averaging already cancels temporal noise. Checked before switching, not
  assumed.
- **`images/01. Title Screen.flac` is audio.** The human believed they had added the
  video; the MP4 came from `~/Downloads` and is now `images/genesis-opening.mp4`.
- **ffmpeg is installed but not on PATH** (winget package dir; `mkopening.py` has a
  fallback constant for it). `imageio_ffmpeg` is **not** installed, so `imageio` cannot
  read MP4 — the tool shells out to ffmpeg directly.
- **A `git checkout` on the tool reverted uncommitted work** and the highlight reserve had
  to be redone. Commit before experimenting on a file.
- **A sweep run from the scratchpad directory** silently edited nothing (relative path
  missed) and printed three identical rows as if they were three configurations. Assert
  the edit landed.

## Environment and conventions

Build invocation, BuildDrop gotchas: [[srl-build-system]]. Emulator: the human runs it,
never a tool call — [[user-runs-the-emulator]]. Both were reaffirmed this session: **do
not run `compile.bat`**. I ran it four times before being corrected, and because it opens
with `rm -f` on the ISO and `cd/data/0.bin`, an overlapping run hands Mednafen a
half-written disc. To check a source change without touching build outputs, use
`-fsyntax-only` with the SH-2 compiler and the flags from the build log.

Host tests: `sh saturn/tests/run_tests.sh` from the repo root, 10 suites. Nothing
automated reaches `opening.cxx` or `menu.cxx`; `test_menu_art.cxx` covers the generated
palette's invariants and is the only guard on the art pipeline.

Analysis scripts from this session are in the session scratchpad under
`%LOCALAPPDATA%\Temp\claude\C--Users-saggl-CLionProjects-Another-Saturn\`. The reusable
ones are `mae.py` (per-pixel scoring against the source), `refpull2.py` (pull reference
colours from a screenshot and find its source frame), and `pixely.py` (raggedness and
palette stability). They are throwaway and not worth preserving beyond the next session.

## Suggested skills

- **`superpowers:systematic-debugging`** for any new visual defect. It was used for the
  original black-holes bug and the discipline of measuring before changing is what caught
  every one of the misleading metrics above. Reaching for a fix first is what produced the
  three interpretations that had to be thrown away.
- **`superpowers:verification-before-completion`** before reporting anything fixed. Three
  separate times a change measured as landed and had not: the deepening that was never
  called, the sweep that edited nothing, the palette entries that collided after the 4-bit
  pack.
- **`superpowers:brainstorming`** if the human wants the navy fringe solved properly. It
  needs a design conversation about per-scene palettes versus the shimmer that removing
  them fixed, not another parameter sweep.
- **`code-review`** over `353a083..HEAD` before pushing the five unpushed commits. The
  tool grew a lot of interacting constants and no human has read the diff.
