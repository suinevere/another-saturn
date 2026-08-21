# Another World → Sega Saturn — Button Layout Toggle Design Spec

**Date:** 2026-08-16
**Status:** Awaiting review
**Target engine:** SaturnRingLib (SRL)
**Extends:** `2026-08-01-title-and-save-menus-design.md`, which built the pause menu,
the slot list and the backup RAM plumbing this spec adds a row and a record to.

## Goal

Let the player choose which face buttons act and which jump, from a row on the pause
menu, and remember the choice across power cycles.

## What the engine can actually hear

`VirtualMachine::inp_updatePlayer` (`saturn/src/vm.cxx:628`) is the whole input surface
between the port and the bytecode. It writes five VM variables and derives every one of
them from two things: `sys->input.dirMask` and the single boolean `sys->input.button`.

| VM variable | Source |
|---|---|
| `HERO_POS_UP_DOWN` (0xE5) | `DIR_UP` / `DIR_DOWN` |
| `HERO_ACTION` (0xFA) | `input.button` |
| `HERO_POS_JUMP_DOWN` (0xFB) | `DIR_UP` / `DIR_DOWN` |
| `HERO_POS_LEFT_RIGHT` (0xFC) | `DIR_LEFT` / `DIR_RIGHT` |
| `HERO_POS_MASK` (0xFD), `HERO_ACTION_POS_MASK` (0xFE) | bitwise union of the above |

> **Corrected 2026-08-20, twice — read the whole note before trusting anything here about
> running.** The paragraph below is wrong: it claims there is no run input. There is, and it
> is the **action bit**. `HERO_ACTION_POS_MASK` (0xFE) is a union of the direction bits, the
> jump bit (8) and the action bit (0x80); with the action bit set *and* a left/right bit set
> in that same mask, the script runs. That is why holding attack ran, which the human
> reported from play as "attack/shoot is the button you hold to run, and it sucks".
>
> A first correction to this spec claimed run and jump were one signal and could not be bound
> separately. **That was also wrong.** They are separable by shaping what the VM is told:
> attack suppresses left/right so the action bit never sits beside a direction, and run raises
> the action bit only while a direction is held. Implemented in `a2f4478`.
>
> Still open: the human reported the *jump* button running before the remap and *attack*
> running after it, which is only consistent if the script runs on a direction plus either
> bit. Only play can settle that. See `run-is-the-jump-signal` in memory. The original text is
> kept below because the mistake is instructive.

**There is no run input.** Whatever distinguishes walking from running, the script
decides it; nothing in the bytecode's interface exposes a pace. So the port has exactly
two bindable actions:

- **Action** — `input.button`. Tap kicks, hold runs and charges.
- **Jump** — `DIR_UP`.

That is why this is a two-state toggle and not a rebinding screen. A general
press-a-button-to-bind interface would have only two reachable outcomes.

## The setting

One bit. A and C are always the same action as each other; B is always the other one.

| | `input.button` | `input.jump` |
|---|---|---|
| Default | A \| C | **B** |
| Swapped | B | **A \| C** |

The VM jumps on `(dirMask & DIR_UP) || input.jump`, so D-pad Up jumps in both layouts
without the face buttons ever entering `dirMask`. See "Where the swap is applied" for why
that separation is load-bearing rather than stylistic.

**This changes how the game plays today.** `SAT_PAD_ACTION` is currently `A | B | C`, so
all three face buttons fire. After this change B jumps instead, in the default layout.
That is intended.

## Where the swap is applied

> **This section was wrong in the first three revisions and the error was severe.** It is
> corrected below; the history is kept because the mistake is instructive.
>
> The original analysis said `processEvents` could move `button` and `DIR_UP` "without
> disturbing `menuConfirm` and `menuCancel`". That is true of those two *fields* and false
> of the *menu*. `PlayerInput` carries dedicated `menuConfirm`, `menuCancel`, `menuLeft`
> and `menuRight` — but **no menu direction fields**. `menuInputBits`
> (`saturn/src/menu_input.h:42-45`) reads the gameplay `dirMask` for all four menu
> directions. Folding jump into `dirMask` therefore made the jump button navigate menus.
> In the swapped layout jump is A|C, which is also confirm, and every step function tests
> `up` before `confirm` — so confirm became unreachable, **the title card could not be
> confirmed, and the game could not be started at all.** Unrecoverably, since the flag
> lives in battery-backed RAM and the only unswap UI needs a running game.
>
> The seam analysis checked the wrong four fields. The real shape of the problem is that
> **`dirMask` has two consumers, the VM and the menu, and only the VM wanted the jump bit
> folded in.**

`SaturnSystem::processEvents` (`saturn/src/system/saturn_system.cxx`) is still the seam —
it is the single place pad bits become `PlayerInput` — but it must **not** touch
`dirMask`. `dirMask` means "the D-pad", exactly, and both consumers already assume that.

Jump gets its own abstract signal instead:

- **`PlayerInput` gains `bool jump`**, beside the `bool button` that is already there.
  It is an action, not a pad concept, so it costs the engine interface nothing.
- **`processEvents`** writes `input.jump` and leaves `dirMask` as the four raw D-pad bits.
- **`inp_updatePlayer`** (`saturn/src/vm.cxx`) computes
  `const bool up = (dirMask & DIR_UP) || sys->input.jump;` once and reads it at both of
  its former `DIR_UP` tests.
- **`menu_input.h` is untouched**, and can never see a face button as a direction again.

The two rejected alternatives, and one that was rejected on review:

- **In `sat_input_read`** — swapping A↔B at the platform layer swaps menu confirm and
  cancel along with them.
- **Giving the menu its own `menuDirMask`** — works, but adds a field every `System`
  implementation must remember to populate. There is a second one,
  `SDLStub` in `saturn/host/sysImplementation.cxx`; it is unbuilt today, and a field it
  silently leaves zero is the same class of bug being fixed here.
- **In `inp_updatePlayer` alone** — originally rejected on the grounds that "the VM would
  have to learn about pads". That objection was wrong: `input.jump` is an abstract action
  exactly like the `input.button` the VM already reads, and the final design does consume
  it there.

The resolution itself is a pure function in the settings module rather than inline
arithmetic, so the core of the feature sits inside the host test boundary:

```
void settingsMapFaceButtons(bool a, bool b, bool c, bool swap,
                            bool *jump, bool *action);
```

It takes plain booleans rather than `SAT_PAD_*` bits, which keeps `settings.h` free of
platform headers. `saturn_system.cxx:83` becomes:

```
bool jump = false;
bool action = false;
settingsMapFaceButtons((pad & SAT_PAD_A) != 0, (pad & SAT_PAD_B) != 0,
                       (pad & SAT_PAD_C) != 0, sat_input_get_swap() != 0,
                       &jump, &action);
input.jump = jump;
input.button = action;
```

`dirMask`, `menuConfirm`, `menuCancel`, `menuLeft`, `menuRight` and `pause` are all
untouched by this block.

`SAT_PAD_ACTION` becomes dead and is removed, along with the fold at
`saturn_platform.cxx:475` that produces it. `saturn_system.cxx:83` was its only consumer.

The runtime flag is `sat_input_set_swap()` / `sat_input_get_swap()` in
`saturn_platform.h` / `.cxx`, a module-level bool. It lives with the other pad code, which
keeps a Saturn-only concern out of the portable `System` interface that `menu.cxx` holds
as a `System *`.

## Storage

**New file pair `saturn/src/settings.h` / `settings.cxx`**, flat, beside `savedata.cxx`,
matching the repository's existing layout. It makes its own backup RAM calls exactly as
`savedata.cxx` does — that is what keeps it host-testable, since
`tests/stub_saturn_backup.cxx` already stubs `sat_bup_dir`, `sat_bup_read` and
`sat_bup_write`.

**Record — 16 bytes, big-endian, magic first, mirroring `savedataWriteHeader`:**

| Offset | Field |
|---|---|
| 0–3 | magic `'AWCF'` |
| 4–5 | `SETTINGS_VER` = 1 |
| 6 | flags — bit 0 = swap buttons |
| 7–15 | reserved, zero |

Its own version constant rather than `Serializer::CUR_VER`: preferences evolve
independently of the save format. The reserved bytes exist so a second preference later
does not invalidate a config already written to someone's console.

**Interface**, following savedata's shape — magic checked first, outputs untouched on
failure:

```
void settingsDefaults(Settings *s);
void settingsPack(uint8_t *buf, const Settings *s);
bool settingsUnpack(const uint8_t *buf, Settings *s);
bool settingsLoad(Settings *s);
int  settingsStore(const Settings *s);
```

`settingsStore` returns a `SAT_BUP_*` code rather than a bool, so it can be assigned
straight to an `int` status field on `Menu`. `settingsLoad` stays boolean: absent, corrupt
and wrong-version all mean the same thing to the caller, which is "use the defaults".

That field is **`Menu::_settingsError`, its own slot — not the existing `_statusError`**.
Sharing one slot was the original design and review rejected it: `_statusError` also
carries save and load failures, so failing a save and cancelling back to the pause screen
painted `NOT SAVED` under the layout row, which a player reads as "my layout didn't save"
when nothing about the layout failed.

**Entry name `AW_CFG`, internal device only** — not the picked default the slot list uses
via `savedataPickDefaultDevice`. Internal backup RAM is always fitted, so the config
cannot vanish when a cart is pulled, and the load happens at boot before the menu has
probed any device.

**Load site:** `Engine::init()`, immediately after the existing `sat_bup_init()` at
`engine.cxx:164`, inside the same `#ifdef __sh__`. A failed or absent load leaves the
defaults, so the game is playable before a config has ever been written.

## The pause row

The new row cannot go at the end — that would place a settings row below
`RETURN TO MENU`. It goes at index 3, and `RETURN TO MENU` moves to 4. `stepPause`'s
`% 4` becomes `% 5`.

```
RESUME            y=60
SAVE GAME         y=76
LOAD GAME         y=92
FIRE A/C  JUMP B  y=108     <- new
RETURN TO MENU    y=124
```

The row is self-describing rather than carrying a `BUTTONS` label, because a label alone
does not tell the player which way it is currently set:

```
FIRE A/C  JUMP B
FIRE B  JUMP A/C
```

**The budget is the panel, not the screen.** `menuDrawPauseScreen` fills a border at
`(80, 48, 168, 96)` and an inner panel at `(82, 50, 164, 92)`, so the usable region ends
at x=246. Rows start at cell column 13, which is x=104, leaving **17 characters**, not the
27 the 320-pixel screen would suggest. Both states above are 16, ending at x=232.

Column alignment between the two states is deliberately not attempted: padding `JUMP` to a
fixed column costs 19 characters and overflows the panel.

**The panel grows from 96 to 112 tall** — border `(80, 48, 168, 112)`, inner
`(82, 50, 164, 108)` — to hold the fifth row at y=124 and the status line at y=140. The
height is fixed rather than conditional on there being a status to show, so the panel does
not jump when a write fails.

**`menu_state.cxx`** gains `MENU_ACT_TOGGLE_BUTTONS`, returned by confirm on row 3 —
**confirm only**. A new `bool swapButtons` on `MenuState` is flipped inside
`menuStateStep`, which keeps the flip host-testable.

Left and right were originally specified to toggle the row too, on the reasoning that a
value row reads as something you nudge. Review killed it: LEFT and RIGHT are in
`MENU_PAD_NAV` and auto-repeat every `MENU_REPEAT_RATE` frames, so holding a direction on
row 3 flipped the value and fired a blocking `sat_bup_write` roughly fifteen times a
second. Confirm is edge-only and cannot repeat. A two-state row does not need the
affordance enough to be worth a write storm.

**Trap, stated here so it is not discovered later: `menuStateEnterTitle`,
`menuStateEnterPause` and `menuStateEnterLoad` must not reset `swapButtons`.** They reset
neighbouring fields, and a reset here would silently revert the player's setting every
time they opened a menu. `Menu::init` seeds it once from the loaded config.

## Writing

Toggling calls `sat_input_set_swap` immediately, so the change is live the moment play
resumes, and `settingsStore` runs on the same press.

Deferring the write to the pause menu's exit was considered and rejected: the failure
report below has to be visible, and by the time the menu is closing there is no row left
to draw it under. `sat_bup_write` blocks, but the game is paused and the pause screen is
static, so there is no frame being animated for the stall to disturb. Internal backup RAM
is battery-backed SRAM rather than flash, so repeated toggling carries no wear cost.

The fade/sync hazard recorded in `mem/death-menu-and-loading-pump.md` does not apply.
Nothing is darkening around this write, so there is no `slColOffsetA` set for a blocking
call to land behind.

## Failure handling

| Case | Behaviour |
|---|---|
| No config entry on first boot | Defaults, silently. Not an error. |
| Load fails — bad magic, unknown version, read error | Defaults. Nothing is overwritten unless the player toggles, at which point the record is rewritten at `SETTINGS_VER`. |
| Store fails — no space, dead battery | The setting still applies for this session; a `NOT SAVED` status line draws at y=140. |

The status line is the bare words `NOT SAVED` rather than the existing
`menuStatusText(err, device)` mapping. That helper is reused everywhere else and would be
the obvious choice, but its longest reachable message for an internal device,
`BACKUP RAM UNFORMATTED`, is 22 characters against the panel's 17. The precise reason is
recoverable from the save screen; the pause panel only has room to be honest that it
didn't stick. `settingsStore`'s return code is still kept in full in `_settingsError`, so
a future wider panel can show the detail without changing anything but the drawing.

## Testing

**New `tests/test_settings.cxx`**, with a suite added to `tests/run_tests.sh`:

- pack/unpack round-trip
- magic rejection leaves outputs untouched
- unknown version rejected
- reserved bytes zeroed by `settingsPack`
- `settingsLoad` with no entry present returns false and leaves defaults intact
- store-then-load round trip through `stub_saturn_backup.cxx`
- `settingsMapFaceButtons` in both layouts, with nothing held, and with both actions held

**`test_menu_state.cxx`** gains a toggle-by-confirm case, a case asserting left and right
do **not** toggle the row, a case asserting they do nothing off it either, and a case
asserting all three `menuStateEnter*` functions preserve `swapButtons`. Its existing
pause-navigation cases are updated for five rows and the moved `RETURN TO MENU` index —
four of them, not one: `test_return_to_menu_asks_for_confirmation`,
`test_confirm_defaults_to_no`, `test_confirm_yes_returns_to_title` and
`test_confirm_cancel_declines_return_to_title` all navigated by pressing down three times
to reach the confirm-triggering row.

**`test_menu_draw.cxx`** needs no change: it makes no assertions about the pause screen.

**Not covered, by design.** The `processEvents` glue, the drawing, and the `Engine::init`
load sit outside the host-testable boundary for the same reason `menu.cxx` and
`opening.cxx` do. Factoring the resolution into `settingsMapFaceButtons` pulls the logic
inside the boundary and leaves only the wiring outside, but green tests will still say
nothing about whether B actually jumps. That needs the emulator, and per
`mem/user-runs-the-emulator.md` the human runs it.

## Explicitly out of scope

- **X, Y, Z, L and R as bindable buttons.** `sat_input_read` does not poll X/Y/Z at all
  today. With only two actions to bind and A/C locked together, there is nothing for them
  to select.
- **A title-menu row.** Title rows are 2bpp bitmap art (`MENU_ART_START_GAME`, generated by
  `tools/mkmenuart.py`), so a row there means authoring new chrome lettering. The setting
  persists, so the player sets it once, ever.
- **Any second preference.** The record reserves room for one; nothing else is specified.
