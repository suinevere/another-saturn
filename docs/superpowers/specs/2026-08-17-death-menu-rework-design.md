# Another World → Sega Saturn — Death Menu Rework Design Spec

**Date:** 2026-08-17
**Status:** Awaiting review
**Target engine:** SaturnRingLib (SRL)
**Extends:** `2026-08-01-title-and-save-menus-design.md`, which built the slot list and the
backup RAM plumbing this spec reshapes.
**Depends on:** the backup device mapping fix (`f826ff1`). Before it, `SAT_BUP_INTERNAL`
addressed the cartridge, so "save to internal" did not mean what it says. This spec assumes
it now does.

## Goal

Replace the screen a death opens with five fixed rows — resume, save and resume, load,
save and quit, quit — and demote the slot list behind the load row.

## The rows

```
RESUME          -> MENU_ACT_RETRY
SAVE & RESUME   -> MENU_ACT_SAVE_RETRY
LOAD GAME       -> opens MENU_SLOTS in load mode, returnScreen = MENU_DEATH
SAVE & QUIT     -> MENU_ACT_SAVE_AND_QUIT   (new)
QUIT            -> MENU_ACT_RETURN_TO_TITLE
```

Cancel resumes, as it does today. **Quit does not ask for confirmation** — the row it
replaces (`RETURN TO TITLE`) does not either, and matching the screen being replaced beats
introducing a prompt nobody asked for. The pause menu's equivalent still confirms; that
inconsistency is deliberate and pre-existing.

`&` is used rather than `AND`: the font has an ampersand (glyph index 6 in
`Video::_font`, `staticres.cxx:73`). The existing `SAVE AND RESUME` changes to
`SAVE & RESUME` to match its new neighbour.

## The screen model, and what it deletes

Today the death menu **is** the slot list wearing a hat: `menuStateEnterLoad(&_st,
MENU_TITLE, true)` opens `MENU_SLOTS` with a `retryRow` flag, and the extra rows live at
negative cursor indices off the ends of the slot array. Every path that indexes
`st->slots` has to test for them first.

Demoting the list behind a row makes the death menu **its own screen**, `MENU_DEATH`, and
all of that scaffolding stops being needed:

| Deleted | Why it existed |
|---|---|
| `MenuState::retryRow` | Told `stepSlots` and the drawing whether to grow extra rows |
| `MENU_SLOT_RESUME` (−2), `MENU_SLOT_SAVE_RESUME` (−1), `MENU_SLOT_TITLE` (3) | Off-array cursor values for those rows |
| `retry` parameter on `menuStateEnterLoad` | Only the death menu ever passed true |
| `stepSlots`'s cancel special-case and its `first`/`last` juggling | Both existed only for the death rows |
| `MENU_DEATH_*` y-offset enum (`menu.cxx:482-490`) | Held the "slots close up to 14 scanlines" special case |
| The `A SELECT   B BACK` hint (`menu.cxx:547`) | Already wrong — B resumes, it does not go back. Recorded as an open defect in `mem/death-menu-and-loading-pump.md`. Five self-describing rows need no hint. |

`menuStateEnterLoad` becomes `menuStateEnterLoad(st, back)`, a new `menuStateEnterDeath(st)`
opens the fixed screen, and `returnScreen` gains `MENU_DEATH` as a valid target so cancel
from the slot list comes back to the death menu rather than the title.

`MENU_SLOTS` goes back to being a plain list, which is what the title and pause menus have
always wanted from it.

**A side effect worth having: the death menu no longer touches backup RAM to open.**
`runDeath` currently calls `ensureDevices()` and `menuRescan()` before it can draw, so a
death costs several BUP calls before anything appears. With no list on the screen, both move
to the Load Game path, where `MENU_ACT_RESCAN_SLOTS` already drives them.

## Device defaulting

**Internal is the unconditional default.** `ensureDevices` still probes both devices to set
`cartPresent`, but `_st.device` is simply `SAT_BUP_INTERNAL`.

That makes two things dead, and both are removed:

- `savedataPickDefaultDevice` (`savedata.h`/`.cxx`) — its only real caller is that one line.
- `menuHasAnySave` (`menu.cxx:267`) — it exists solely to feed the picker's `hasSaves`
  arguments.

**It also retires five tests** in `test_savedata.cxx`: `test_default_device_no_cart`,
`_cart_has_saves`, `_internal_has_saves`, `_both_have_saves`, `_cart_present_but_empty`.
These are not coverage being dropped — they assert a decision the design no longer makes.

**L/R switching needs no work.** `stepSlots`'s device-toggle branch sits above the confirm
branch and is not gated on `st->saving`, so it already works in both save and load modes,
and `menuInputBits` maps both the shoulder buttons and D-pad left/right onto
`menuLeft`/`menuRight`. It is gated on `cartPresent`, which is correct.

## The device banner

Reworded and widened, at cell column 6, y=48 — directly under the `SAVE GAME` / `LOAD GAME`
header at y=24, with the slots below at y=72. Both states pad to 27 characters so the
arrows do not move when the device switches:

```
L  <  INTERNAL MEMORY  >  R
L  <     CARTRIDGE     >  R
```

At column 6 that spans x=48–264 inside a panel whose usable region is x=26–294.

**It still draws only when a cartridge is present.** With no cart there is nothing to switch
to, and arrows that do nothing are a lie.

## Save & Quit

The state at the moment the death menu is up **is the death**. `Engine::run` already knows
this: the existing deferred save waits for `vm.deathPromptHold` to reach zero after the
script has carried the player back into play (`engine.cxx:110`,
`VM_DEATH_PROMPT_HOLD_FRAMES` = 120), with the comment "Saving any earlier stores the death
itself."

So Save & Quit does not get a save path of its own. It resumes invisibly, lets the existing
latch write the checkpoint, and then leaves:

```cpp
_engine->vm.deathRetry = true;
_engine->saveAfterResume = true;
_engine->quitAfterSave = true;     // new Engine flag
```

and in `Engine::run`:

```cpp
if (saveAfterResume && vm.deathPromptHold == 0) {
    saveAfterResume = false;
    const bool saved = autosaveCheckpoint();
    const bool wasQuitting = quitAfterSave;
    quitAfterSave = false;

    if (!saved) {
        deathSaveError = lastSaveError();
    } else if (wasQuitting) {
        playing = false;
        continue;
    }
}
```

`autosaveCheckpoint` changes from `void` to `bool`, returning what `saveSlot` returned.
`quitAfterSave` is cleared before either branch so a failed attempt cannot fire later.

The save is byte-for-byte the one Save & Resume writes: `ENGINE_AUTOSAVE_SLOT` 0 on
`ENGINE_AUTOSAVE_DEVICE`, overwriting without a prompt.

**Holding the screen dark for the wait.** Two one-line changes at the `deathPrompt` block:
`video._holdDisplay = quitAfterSave` instead of unconditionally false, and the `lit` ramp at
the loop's foot gated on `!quitAfterSave`. `runDeath` already ends on
`sat_fade_ramp(SAT_FADE_DARK, …)`, so the screen stays dark for the ~2 seconds the
checkpoint takes to settle. Nothing has to un-darken on the way out: `runTitle` ends with
its own `sat_fade_ramp(SAT_FADE_LIT, bootFade)`.

**A failed save cancels the quit.** `autosaveCheckpoint` currently discards the result — it
calls `saveSlot` and then forces `_lastSaveError = SAT_BUP_OK` (`engine.cxx:336-339`), so a
failed Save & Resume is already silent. That is tolerable when the player stays in the game
and can try again; it is not tolerable for Save & Quit, where they would leave believing
their progress was kept.

So `autosaveCheckpoint` stops discarding the error — it returns `saveSlot`'s result and
leaves `_lastSaveError` as `saveSlot` set it.

**On failure the death menu re-opens, carrying the message.** This applies to *both* save
rows, not just Save & Quit: the player asked for a save, it did not happen, and the menu is
where they can do something about it — resume anyway, or try again.

Re-opening rather than reporting in place is forced by the timing. The write happens ~120
frames after the menu closed, so there is no box on screen at that moment to draw into. Two
seconds is also early enough that the player has barely re-entered play, so the menu coming
back reads as part of the death sequence rather than as an interruption.

Mechanically, `Engine` gains an `int deathSaveError`, and the death-menu entry in
`Engine::run` is entered from either trigger — the script's prompt, or a pending save error:

```cpp
if (vm.deathPrompt || deathSaveError != SAT_BUP_OK) { ... }
```

`Menu::runDeath` takes the incoming error to display and `Engine::run` clears it after the
call. The existing block already clears `vm.deathScreen` and `video._holdDisplay` and
`continue`s, so the second trigger reuses all of it rather than duplicating the sequence.

This also un-silences Save & Resume's failures as a side effect, since the forced
`_lastSaveError = SAT_BUP_OK` is what hid them.

## Drawing

Border `(48, 48, 224, 128)`, inner `(50, 50, 220, 124)` — so x=48–272 with a usable region
ending at x=270, and y=48–176 ending at y=174. Rows at y = 64/80/96/112/128, caret at cell
column 8, row text at column 10, status line at y=148.

**The panel is sized by the status message, not by the rows.** The rows would fit the pause
menu's 168-wide panel easily — the longest, `SAVE & RESUME`, is 13 characters. But
`menuStatusText`'s longest reachable message for an internal device,
`BACKUP RAM UNFORMATTED`, is 22, and at column 8 (x=64) a 22-character line runs to x=240
against a usable edge of x=270. Making the death and pause panels visually identical was
tempting and is given up deliberately: the message has to fit.

The status line uses the existing `menuStatusText(err, SAT_BUP_INTERNAL)` rather than a
bespoke string, so a failed save reads the same here as it does on the slot list. It draws
only when the error is not `SAT_BUP_OK`; the panel's height is fixed either way, so it does
not jump when a message appears.

This replaces the current 272×168 panel, which was that large to hold six rows plus a slot
list.

The screen is still composed on black rather than over the frozen frame — `runDeath` passes
`overlay = false` — because by every moment the port can see, the frame behind is already
the screen being replaced.

## Failure handling

| Case | Behaviour |
|---|---|
| Save & Resume write fails | The death menu re-opens with the reason on its status line. Previously silent. |
| Save & Quit write fails | The quit is cancelled and the death menu re-opens with the reason. Progress is never lost to a failed write. |
| Load Game with an empty or damaged slot | Unchanged: `stepSlots` refuses and the status line reports it. |
| No backup device at all | The slot list reports `NO BACKUP DEVICE` through the existing `menuStatusText`. Reachable now that the device mapping is fixed — before it, a cartless machine had no working device. |

## Testing

`menu_state.cxx` stays pure, so the new screen is fully host-testable. `test_menu_state.cxx`
gains:

- one case per row for the five confirm actions
- the status line draws only when the incoming error is not `SAT_BUP_OK`
- cancel resumes from any cursor position
- the cursor wraps across five rows
- Load Game opens `MENU_SLOTS` in load mode with `returnScreen == MENU_DEATH`
- cancel from the slot list returns to `MENU_DEATH`, not `MENU_TITLE`

and its existing slot-list cases are updated for `retryRow`'s removal.

**Not covered, by design.** The Save & Quit timing, the dark hold, the panel geometry and
the banner all live in `menu.cxx` and `engine.cxx`, which no host suite compiles — the same
boundary that let the backup device bug survive for months. A green suite says nothing about
any of it. In particular the ~2 second black gap before the title needs a human to judge
that it reads as deliberate rather than as a hang.

## Explicitly out of scope

- **A confirmation prompt on Quit.** Matching the row being replaced.
- **The pause menu's rows.** Untouched.
- **Making `savedata.cxx`'s device defaulting configurable.** It is being deleted, not
  parameterised.
