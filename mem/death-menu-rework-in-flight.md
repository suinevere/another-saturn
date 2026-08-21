---
name: death-menu-rework-in-flight
description: PARTLY STALE (2026-08-21) -- branch state, commits and outstanding work are superseded by [[port-death-menu-branch-state]]. The heart-of-the-saturn section, the branch tangle and the two coverage traps are still accurate.
metadata:
  type: project
---

> **PARTLY STALE, 2026-08-21.** Everything below about where execution stands, which commits exist and what is outstanding has been overtaken -- see [[port-death-menu-branch-state]]. The heart-of-the-saturn section, the branch situation and the two coverage traps below remain correct and are not repeated there.

Updated 2026-08-20. This entry began as a mid-execution handoff paused between Tasks 1 and 2;
all four tasks are now done and it has been rewritten rather than left to disagree with itself.
Everything below is state and reasoning **not** already in the spec, the plan, or the commits:

- Spec: `docs/superpowers/specs/2026-08-17-death-menu-rework-design.md`
- Plan: `docs/superpowers/plans/2026-08-17-death-menu-rework.md`
- SDD ledger, briefs and reports: `.superpowers/sdd/2026-08-17-death-menu-rework/` — **git-ignored**,
  so a clone or `git clean -fdx` destroys it. Kept deliberately past the point the skill would
  delete it, because the hardware run has not happened yet and those reports are the only record
  of what was checked. Delete after that passes.

## Where it stands

Branch **`port/death-menu`**, cut from `main` at `7d24252`, now at **`53dd3f9`** — seven commits.
All four tasks complete and reviewed clean, plus one fix round on Task 4 and one final fix wave.

**Nothing here has ever been compiled.** Not once, by anyone, at any point. Three of the four
changed source files — `menu.cxx`, `engine.cxx` and their headers — are compiled by nothing on
the host, and the human does all building. The 11-suite host run is green throughout and is
evidence for Tasks 1 and 2 only. The SH-2 transient the plan accepted is closed: no reference to
`retryRow`, `MENU_SLOT_*`, `MENU_DEATH_*` or a one-argument `runDeath` survives.

**Next action is the human's:** build, then work the hardware checklist in
`.superpowers/sdd/2026-08-17-death-menu-rework/task-4-report.md` — nine items from the plan plus
five the final review added, covering visual state, which is where its Critical lived.

## The defect worth remembering

The final whole-branch review caught a Critical that all four per-task reviews structurally could
not: `menuRenderFrame`'s new `MENU_DEATH` case never cleared the compositing page, so the panel
composited over whatever `s_menuPage` last held — the title artwork on a first death, the slot
list's larger panel after backing out of LOAD. `MENU_SLOTS` and `MENU_CONFIRM` both clear;
`MENU_TITLE` memcpys the whole page; `MENU_PAUSE` is an overlay. `MENU_DEATH` was the only
non-overlay case doing neither, and it was a regression this branch introduced, since `runDeath`
previously rendered as `MENU_SLOTS` and inherited that clear.

The shape of it is the lesson. Task 1 built the screen, Task 3 drew it, Task 4 wired it, and the
clear belonged to none of their briefs. No host test could see it, and each scoped review saw only
its own slice. **On this codebase, a whole-branch review is not a formality — it is the only pass
that can catch anything living between two tasks in an uncompiled file.**

## Parked, with reasons, so they are not re-litigated

- `menuStateEnterLoad` has no production caller and survives only because a test calls it. The
  plan kept it deliberately; deleting public API plus its test at the end of a branch is a worse
  trade than carrying it.
- `Menu::_devInternal` is write-only and its `sat_bup_probe` call is redundant. Left alone
  specifically to avoid editing device-probing code in the last commit before a hardware run —
  this session's earlier backup-device bug came from exactly that area. Costs one wasted probe.
- `stepTitle`/`stepPause`/`stepSlots`/`stepDeath`, and `saveSlot`/`loadSlot`/`runDeath`/`runPause`
  in the headers, omit `Dependencies:` and `Globals:`. A family-wide mechanical sweep for later;
  the standing rule is **match the immediate family**, never create a one-off.

## The larger outstanding item: heart-of-the-saturn's death menu

The request was to apply **both** changes to `../heart-of-the-saturn`. Only the bug half is done
there (`dd32ad1`, `8a280b9`, `a19aa66`). **The death menu rework has not been started and is still
owed.** It is not a copy-paste job: that port is **C, not C++** — `gcc -std=c99`, `savedata.c`,
`menus/menu_state.c`, `.c` test stubs — and its death flow differs, reloading the room without
resetting the VM so "a death costs the room rather than the playthrough". Read its actual
`menu_state.c` before assuming this plan's structure transfers. Its `.another/` directory is a
git-ignored reference clone of Another-Saturn; never edit it.

## The branch situation, still unresolved and the human's call

`feature/publish-program-assets` sits at `5baf184`, ahead 2 / behind 2 of its upstream, and the
merge-or-PR question for it was never answered. `main`'s configured upstream is `origin/master`,
which no longer exists, while `origin/main` does.

## Two coverage traps this work hit, both plan defects rather than implementation defects

1. **A wrap-only navigation test pins nothing.** Pressing Up from row 0 expecting 4 and Down
   expecting 0 is passed exactly by an implementation of `up` as `cursor = 4` and `down` as
   `cursor = 0`, while row-specific tests that assign `st.cursor` directly never navigate. Fixed
   by `879d61b`, which walks Down asserting each position in turn.
2. **A sentinel seeded to a type's zero value proves nothing.** Seeding an output to `false` and
   asserting it is still `false` after an expected failure passes whether the callee left it alone
   or zeroed it. Seed the opposite value.

Related: [[death-menu-and-loading-pump]] for the death flow this reworks and the fact that the
access code screen is not a game part; [[bup-device-index-is-not-unit-id]] for the device mapping
fix this depends on; [[dirmask-drives-the-menus]] for why `dirMask` must stay the D-pad;
[[user-runs-the-emulator]] and [[never-build-suinevere-builds]] for the build boundary;
[[suinevere-conventions]] for the comment format and the spec-then-plan cadence.
