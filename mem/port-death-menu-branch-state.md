---
name: port-death-menu-branch-state
description: Handoff — port/death-menu now carries the finished death menu rework plus a button remap, two save-overwrite fixes and a background-restore fix; the background is hardware-confirmed working, most of the rest is not, and the branch has never been integrated.
metadata:
  type: project
---

Handoff, 2026-08-21. Branch **`port/death-menu`**, cut from `main` at `7d24252`, now at
**`bb7e617`** — 20 commits. Working tree carries only the long-standing unrelated changes
(`.github/workflows/full-image.yml`, the `SaturnRingLib` submodule, `.ai/`, `.claude/`,
`Another World (Europe).md`, `tools/assets/png/`); nothing of this work is uncommitted.

Read `git log 7d24252..HEAD` for what changed — it is not restated here. Design documents:

- `docs/superpowers/specs/2026-08-17-death-menu-rework-design.md` and its plan alongside
- `docs/superpowers/specs/2026-08-16-button-layout-toggle-design.md` — **carries two correction
  blocks; read them before trusting anything in it about running**
- `.superpowers/sdd/2026-08-17-death-menu-rework/` — git-ignored ledger, briefs and reports for
  the four-task rework, including a fourteen-item hardware checklist in `task-4-report.md`.
  **Kept deliberately** past the point the SDD skill deletes it, because the hardware pass is
  still outstanding and those reports are the only record of what was checked.

## What is verified and what is not

**Hardware-confirmed working:** the save background restore. The human reported `BG 3 4255 OK` —
half-height frame, 4255 bytes, decoded, background visible after a load. See
[[save-background-restore]].

**Everything else on this branch is unverified.** The death menu rework (four tasks), the button
remap, the death panel height, and both save-overwrite fixes have had host tests and review but
no confirmed hardware pass. `menu.cxx` and `engine.cxx` are compiled by nothing on the host, so
the 11-suite green run says almost nothing about them. The fourteen-item checklist is the
outstanding work.

## Decisions waiting on the human

1. **Branch integration was never settled.** `superpowers:finishing-a-development-branch` ran, the
   suite was green, the menu was presented — merge to `main` locally / push and open a PR / keep
   as-is — and the human moved on to other bugs instead of answering. The base is `main`
   (`git merge-base main HEAD` = `7d24252`, confirmed).
2. **The `BG <kind> <bytes> <OK|NO>` line in the pause menu is debug UI** and is visible during
   normal play. It maps kind 2 to full height, 3 to half, 4 to quarter, 5 to eighth. One revert
   removes it; it is genuinely useful for spotting a scene that fell back further than expected.
3. **Half-height backgrounds may look coarse.** 4255 bytes at half implies roughly 8-9 KB at full
   against ~6.5 KB free, so full resolution is reachable with a better codec rather than a bigger
   save. `page_rle.cxx` is fully host-tested, so any codec change can be measured before a build.
   Only worth doing if the coarseness actually shows.

## Open questions the human's testing raised and nobody has answered

- **Does the jump button still run?** Before the remap they reported holding *jump* ran; after it,
  holding *attack* ran. Both can only be true if the script runs on a direction plus **either**
  the jump bit or the action bit. If so, C plus a direction still runs and needs the same
  treatment attack got. See [[run-is-the-jump-signal]].
- **Does D-pad Up still do anything unwanted?** `b955ace` left Up feeding `HERO_POS_UP_DOWN`
  (0xE5) as a plain direction while removing it from the jump bit, on the conservative assumption
  that some script reads it for something other than jumping. If Up still jumps, drop `upDir`
  from `inp_updatePlayer` entirely — a one-line change.
- **Which screen showed `SLOT ALREADY IN USE`?** If it appeared while the slot list was still up
  on a slot the list showed as *empty*, backup RAM held a file the scan missed — a third bug that
  was never chased. If it was an occupied slot, `44a1c66` explains it.

## The larger outstanding item, unchanged

**`../heart-of-the-saturn` still needs the death menu rework.** Only the backup-device bug half
was done there (`dd32ad1`, `8a280b9`, `a19aa66`). That port is **C, not C++** — `gcc -std=c99`,
`savedata.c`, `menus/menu_state.c` — and its death flow reloads the room without resetting the
VM, so a death costs the room rather than the playthrough. Read its actual `menu_state.c` before
assuming this plan transfers; it needs its own spec. Its `.another/` directory is a git-ignored
reference clone of Another-Saturn — never edit it.

Also still unresolved and cosmetic: `feature/publish-program-assets` sits at `5baf184`, ahead 2 /
behind 2 of its upstream, with the merge-or-PR question unanswered; and `main`'s configured
upstream is `origin/master`, which no longer exists, while `origin/main` does.

## How to work on this, learned the hard way this session

**Instrument before theorising.** Three rounds were lost to confident wrong diagnoses of the black
background, and each was ended by adding a readout rather than by reasoning. Nothing here compiles
or runs on the agent side, so a claim about behaviour that is not measured is a guess. Two traps
worth knowing: **slot 0 is `ENGINE_AUTOSAVE_SLOT`**, written during gameplay with no menu on
screen, so any status it sets cannot be displayed and "no warning appeared" there is not evidence;
and **this repo's specs are detailed and usually right, which makes the rare wrong one dangerous**
— the button-layout spec's "there is no run input" sent two sessions down the wrong path.

## Suggested skills

- **`superpowers:finishing-a-development-branch`** to close out the integration decision above,
  once the human has done a hardware pass.
- **`superpowers:systematic-debugging`** or **`diagnosing-bugs`** before any fix to a reported
  symptom here. It is what finally worked on the background, and the two bugs before it were
  botched by skipping it.
- **`superpowers:brainstorming`** before starting heart-of-the-saturn's death menu — different
  language, different conventions, different resume semantics, and it needs its own spec.
- **`superpowers:subagent-driven-development`** only if a new multi-task plan is written; the
  death-menu plan is finished and its ledger closed.

Supersedes the execution-position half of [[death-menu-rework-in-flight]], which is now stale on
branch state, commits and outstanding work. Its heart-of-the-saturn section, its branch-tangle
section and its two coverage traps remain accurate and are not repeated here.

Related: [[save-background-restore]], [[run-is-the-jump-signal]], [[dirmask-drives-the-menus]],
[[bup-device-index-is-not-unit-id]], [[never-build-suinevere-builds]], [[user-runs-the-emulator]],
[[suinevere-conventions]].
