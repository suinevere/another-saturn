---
name: death-menu-and-loading-pump
description: Handoff at 4d92865 — the death screen is now the port's own menu rather than the script's access code prompt, plus the one fact that cost five wrong fixes to find.
metadata:
  type: project
---

> **PARTLY SUPERSEDED** by [[death-menu-rework-in-flight]], 2026-08-17. Still accurate and
> still load-bearing: everything under "The fact that cost five wrong fixes" — the access code
> screen is not a game part, the strings in `staticres.cxx` are the only signal, the order in
> which the script draws them, and the three invisible facts about `slColOffsetA`,
> `menuRenderFrame` and `sys->input.code`.
>
> No longer current: the commit and branch state below (`main` has moved on several times); the
> account of the death menu's rows, which are being replaced by resume / save and resume / load
> / save and quit / quit on `port/death-menu`; and the open item about `A SELECT  B BACK` being
> wrong, which that rework deletes. The open items about B and C of the agreed plan — holding
> the title card over the attract load and over Start Game — are still open and still unclaimed.

Handoff, 2026-08-16. `main` is `4d92865`, one squashed commit on top of `989fb9f`. The
pre-squash history (27 commits) is on the local branch **and** tag `pre-squash/2026-08-16`
and **nowhere else** — the force push removed it from the remote. Delete those refs only
once nobody wants the individual steps.

Companion entry: [[intro-load-seam]] for the load-seam measurements and fixes squashed into
the same commit. This entry is only the death menu, the loading pump, and what is left.

## The fact that cost five wrong fixes

**The access code screen is not a game part.** It is drawn and blitted from whatever part
the player died in, held on screen by the script's own pause slices. There is no part
switch, so `GAME_PART_LAST` / `res.currentPartId` never see it — four fixes aimed at that
changed nothing, and the fifth only half worked.

The signal is the strings themselves, in `saturn/src/staticres.cxx`: the checkpoint code
words `0x15E`–`0x174`, the header `0x13C`, and the prompt `0x13D`. `op_drawString` swallows
all of them. **Order matters and is not obvious: the script draws a code word and blits it
before it draws the header**, so hooking the header alone let the code through for a second.

Three more things that are invisible from the code:

- **`slColOffsetA` only reaches the hardware on a sync.** Setting the fade dark and then
  entering a blocking load means the fade lands *after* the load. Anything that darkens
  before a stall must sync — `sat_fade_ramp(target, n)` does, `sat_fade_set` does not.
- **`menuRenderFrame` presents through `sys->updateDisplay` directly**, bypassing
  `Video::updateDisplay`. That is why `Video::_holdDisplay` can stay on for the whole death
  sequence without blocking the menu drawn during it.
- **`sys->input.code` is held false by the Saturn backend** (`saturn_system.cxx`), so the
  password *entry* screen is unreachable and swallowing the code words costs nothing.

## What the death flow does now

`op_drawString` raises `deathScreen` (code word or header) then `deathPrompt` (prompt);
`Engine::run` darkens, syncs, holds the display, and opens `Menu::runDeath`. The menu is
drawn on black — not over the last frame, because by every moment the port can see, the
frame behind is already the screen being replaced. Rows: resume, save and resume, three
slots, return to title; cancel resumes. Resume feeds the script one frame of
`sys->input.button`, which is what its own prompt was waiting for, so the checkpoint is the
game's, not ours. `deathPromptHold` stops the prompt immediately re-triggering and also
times the deferred save.

## Open

- **Unverified: one frame of `sys->input.button` may not satisfy the script's wait.** It
  works in play, but nobody has read the bytecode. If a resume ever bounces back to the menu
  after ~2s that is the cause, and the fix is to hold the button for several frames.
- **B and C from the agreed plan are not done**: holding the title card over the attract
  load and over Start Game, so neither seam shows black. The loading pump they were to sit
  on is in. Start Game's own seam has never been measured.
- The 64 KB `CACHE_WINDOW_BYTES` is **untested under gameplay music** — see
  [[intro-load-seam]].
- `A SELECT  B BACK` on the death menu is now wrong; B resumes.
- Working tree carries changes that are not this work: `.github/workflows/full-image.yml`,
  the `SaturnRingLib` submodule, `.ai/`, `.claude/`, `tools/assets/png/`. Leave them.
- `origin` still points at the old capitalised URL; GitHub redirects with a warning.

## Suggested skills

- **`superpowers:systematic-debugging` or `diagnosing-bugs`** before any fix to a reported
  symptom here. This session's lesson is blunt: five fixes were built on one unverified
  premise. A symptom that gets *worse* after a fix indicts the fix's own mechanism, and two
  reports of "no change" mean stop theorising and instrument.
- **`superpowers:brainstorming`** before B and C — they change control flow across
  `menuRunAttract`, `Engine::run` and the CD layer.
- Read a Mednafen save state early rather than late. It named both hard bugs this session on
  the first read; the stack-walk method is in the auto-memory entry
  `mednafen-savestate-forensics`.

Related: [[intro-load-seam]], [[opening-sequence-and-fades]], [[user-runs-the-emulator]] —
**never build and never run the emulator**, hand the code over.
