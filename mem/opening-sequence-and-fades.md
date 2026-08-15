---
name: opening-sequence-and-fades
description: The boot sequence is movie then VM intro attract then title card, with every seam faded through one VDP2/SCSP knob rather than per-layer dimming.
metadata:
  type: project
---

The sequencing lives in `menuRunAttract` in `saturn/src/menu.cxx` — not in `opening.cxx`,
which cannot see the Engine, and not in `engine.cxx`, which should not own presentation
policy.

    boot ──▶ OPENING.CPK ──▶ ┌─▶ GAME_PART2 intro ─┐ ──[button]──▶ title card
                             └───── loops ─────────┘                    │
                                     ▲                                  │
                                     └────────── 15s idle ──────────────┘

**The movie is a boot event, the introduction is the attract.** `openingPlay` shows
OPENING.CPK at most once a session and there is no replay entry point at all — whether
the player sat through it or pressed a button to get past it, it never comes back. Ten
megabytes streamed off the disc is not something to put in front of somebody every
fifteen seconds.

**The introduction loops rather than running once.** `menuRunAttract` spins on
`Engine::runIntroAttract` until it returns false, which it only does on a button or a
quit. So the title card is somewhere the player arrives, never somewhere they wait for —
a machine left alone on a cold boot plays the introduction forever. The title card's own
fifteen-second idle (`MENU_TITLE_IDLE_FRAMES`, 900 fields) starts that loop again.

## The fade knob

`saturn/src/system/saturn_fade.{h,cxx}`. **Not a palette dim**, and that is the whole
point: the movie is a VDP1 sprite and everything else is an NBG0 bitmap, and the two have
no palette in common. VDP2's colour offset A is applied to `NBG0ON | SPRON` after all
other colour calculation, so one register pair fades whichever is on screen. The audio
half is SCSP `MVOL` (`sat_scsp_set_master`), which the engine's voices, the SGL driver's
voices and the Cinepak player's PCM all pass through.

So nothing being faded has to cooperate, or even know. Callers set a level and go on
presenting whatever they were presenting.

Nothing else in the port touches VDP2 colour offset. If something ever calls SRL's
`VDP2::<screen>::UseColorOffset`, it rewrites both offset registrations from its own
cached masks and silently drops this one.

## The two clocks

Fades are counted in **display fields** everywhere except inside the VM, where they are
counted in **VM frames** — a VM frame is one to five fields depending on what the
cinematic asked for, so `sat_fade_ramp` is wrong there and `OPENING_FADE_VM_FRAMES`
exists instead. Same reason the gameplay fade-in in `Engine::run` counts `hostFrame`
calls.

During a movie the fade must step against `sat_movie_step`, never `sat_video_sync`: the
frame is a sprite that has to be re-issued every field, so a field the player did not
draw is a blank field. `sat_movie_step` keeps drawing after it starts reporting the movie
finished, which is what lets the last frame be faded out instead of snapped away.

## Invariants worth keeping

- **Every stage ends black.** Including the stage that did not run — returning to the
  title from a finished game leaves that game's last frame up, and `menuRunAttract` fades
  it out itself. Whatever draws next fades up from black, so there is one contract. Each
  pass of the introduction loop fades out and back in, which is what gives the loop a
  seam instead of a jump cut.
- **A button during the movie goes to the title card, not into the loop.** It short
  circuits on `OPENING_SKIPPED` before the loop is reached. Somebody pressing a button
  during the opening wants the menu, not two minutes of cutscene.
- **Start Game begins at GAME_PART3, not GAME_PART2.** The introduction is the attract,
  so a new game starts past it rather than replaying it while the player waits to play.
  This is unconditional: a player who skipped the attract skips the introduction with it.
  Making it conditional means remembering in `Engine` whether the attract ran to its end
  and branching in `startNewGame`.
- **`runIntroAttract` must test `res.requestedNextPart` before `vm.checkThreadRequests`,**
  not after. That call is precisely the thing that would follow the request into
  GAME_PART3 and leave the engine running a game nobody chose.
- Menu sub-screens (slot list, confirm) deliberately do **not** fade. Navigation should
  be instant; the fades are for the presentation seams.

Related: [[opening-cinepak-playback]] for the movie itself and the bitstream invariants,
[[title-menu-and-opening-state]] for the older title card work.
