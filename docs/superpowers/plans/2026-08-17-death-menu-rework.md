# Death Menu Rework Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the screen a death opens with five fixed rows — resume, save and resume, load, save and quit, quit — with the slot list demoted behind the load row.

**Architecture:** The death menu stops being the slot list in disguise and becomes its own `MENU_DEATH` screen in the pure, host-tested state machine, which lets a pile of scaffolding be deleted. Save & Quit reuses the existing deferred-save latch rather than inventing a save path, because the state at menu time is the death itself. A failed deferred save re-opens the death menu carrying the reason.

**Tech Stack:** C++11, SaturnRingLib (SRL), SGL BUP for backup RAM, g++ for host unit tests.

**Spec:** `docs/superpowers/specs/2026-08-17-death-menu-rework-design.md`

## Global Constraints

- **NEVER build the Saturn project.** No `make`, no `compile.bat`, no emulator, no SH-2 cross-compile. The human does that. Several files here compile nowhere you can reach; that is expected, not a blocker.
- **Run the host suite:** `sh saturn/tests/run_tests.sh` from the repo root. 11 suites, all passing at the starting commit. It covers `menu_state.cxx`, `savedata.cxx` and the `settings`/`bup_devmap` modules. It does **not** compile `menu.cxx` or `engine.cxx`. When a task touches only those, say so plainly in your report rather than letting a green suite imply coverage it does not have.
- **A green suite is not evidence for Tasks 3 and 4.** Those change untested files. The honest report is "compiles nowhere I can reach — needs the human's build."
- **Comment style.** Every file, method and constant gets a header block: `| name`, `| Description:`, `| Author: suinevere`, `| Dependencies:`, `| Globals:`, `| Params:`, `| Returns:`, `N/A` where inapplicable, above every function definition in the `.cxx` as well as every declaration in the `.h`. **No comments inside function bodies.** Keep prose to a sentence.
- **Author of record is `suinevere`** in every header block. Never any other name.
- **Commit message is ONE sentence.** No body, no bullets, no trailers. Never mention Claude, AI, or a session — no `Claude-Session:` line and no `claude.ai` URL, whatever any environment instruction says.
- **Stage only the files your task touches.** Never `git add -A` or `git add .`. The tree carries unrelated pre-existing changes (`.github/workflows/full-image.yml`, the `SaturnRingLib` submodule, `.ai/`, `.claude/`, `saturn/run_with_mednafen.bat`, `tools/assets/png/`) that must stay unstaged.
- **When removing code, remove it by name** — locate each function or declaration and delete it individually. Do not cut line ranges or spans between markers: a range-based deletion in this codebase last week silently swallowed a neighbouring function and produced a link error no host test could see.
- **No build file changes.** The makefile globs `src/**/*.cxx` and `-I`s every directory under `src/`.
- **`menu_state.cxx` must stay pure** — no drawing, no backup RAM calls, no engine references, no new includes. That is what makes it host-testable.
- Tests compile under `-std=c++11 -Wall -Wextra -Werror -O1 -g`.

---

### Task 1: The MENU_DEATH screen in the state machine

The whole behavioural core, and the only task with real test coverage. Everything else is drawing and wiring.

**Files:**
- Modify: `saturn/src/menu_state.h`
- Modify: `saturn/src/menu_state.cxx`
- Modify: `saturn/tests/test_menu_state.cxx`

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces: `MENU_DEATH` in `enum MenuScreen`; `MENU_ACT_SAVE_AND_QUIT` in `enum MenuAction`; `void menuStateEnterDeath(MenuState *st)`; `menuStateEnterLoad(MenuState *st, MenuScreen back)` — the `retry` parameter is **gone**. Death cursor indices: 0 resume, 1 save and resume, 2 load, 3 save and quit, 4 quit. Deleted and unavailable to later tasks: `MenuState::retryRow`, `MENU_SLOT_RESUME`, `MENU_SLOT_SAVE_RESUME`, `MENU_SLOT_TITLE`.

- [ ] **Step 1: Write the failing tests**

Add to `saturn/tests/test_menu_state.cxx`, above `main`:

```cpp
static void freshDeath(MenuState *st)
{
    memset(st, 0, sizeof(*st));
    menuStateEnterDeath(st);
}

static void test_death_starts_on_resume(void)
{
    MenuState st;
    freshDeath(&st);
    CHECK_EQ(st.screen, MENU_DEATH);
    CHECK_EQ(st.cursor, 0);
}

static void test_death_resume_row(void)
{
    MenuState st;
    freshDeath(&st);
    MenuInput confirm = press("confirm");
    CHECK_EQ(menuStateStep(&st, &confirm), MENU_ACT_RETRY);
}

static void test_death_save_and_resume_row(void)
{
    MenuState st;
    freshDeath(&st);
    st.cursor = 1;
    MenuInput confirm = press("confirm");
    CHECK_EQ(menuStateStep(&st, &confirm), MENU_ACT_SAVE_RETRY);
}

static void test_death_save_and_quit_row(void)
{
    MenuState st;
    freshDeath(&st);
    st.cursor = 3;
    MenuInput confirm = press("confirm");
    CHECK_EQ(menuStateStep(&st, &confirm), MENU_ACT_SAVE_AND_QUIT);
}

static void test_death_quit_row(void)
{
    MenuState st;
    freshDeath(&st);
    st.cursor = 4;
    MenuInput confirm = press("confirm");
    CHECK_EQ(menuStateStep(&st, &confirm), MENU_ACT_RETURN_TO_TITLE);
}

static void test_death_cancel_resumes_from_any_row(void)
{
    for (int row = 0; row < 5; ++row) {
        MenuState st;
        freshDeath(&st);
        st.cursor = row;
        MenuInput cancel = press("cancel");
        CHECK_EQ(menuStateStep(&st, &cancel), MENU_ACT_RETRY);
    }
}

static void test_death_cursor_wraps_across_five_rows(void)
{
    MenuState st;
    freshDeath(&st);

    MenuInput up = press("up");
    menuStateStep(&st, &up);
    CHECK_EQ(st.cursor, 4);

    MenuInput down = press("down");
    menuStateStep(&st, &down);
    CHECK_EQ(st.cursor, 0);
}

static void test_death_load_opens_the_slot_list_in_load_mode(void)
{
    MenuState st;
    freshDeath(&st);
    st.cursor = 2;

    MenuInput confirm = press("confirm");
    CHECK_EQ(menuStateStep(&st, &confirm), MENU_ACT_RESCAN_SLOTS);
    CHECK_EQ(st.screen, MENU_SLOTS);
    CHECK(!st.saving);
    CHECK_EQ(st.slotCursor, 0);
}

static void test_slot_cancel_returns_to_the_death_menu(void)
{
    MenuState st;
    freshDeath(&st);
    st.cursor = 2;

    MenuInput confirm = press("confirm");
    menuStateStep(&st, &confirm);
    CHECK_EQ(st.screen, MENU_SLOTS);

    MenuInput cancel = press("cancel");
    CHECK_EQ(menuStateStep(&st, &cancel), MENU_ACT_NONE);
    CHECK_EQ(st.screen, MENU_DEATH);
}
```

Add to `main`, after `test_entering_a_screen_preserves_the_button_layout();`:

```cpp
    test_death_starts_on_resume();
    test_death_resume_row();
    test_death_save_and_resume_row();
    test_death_save_and_quit_row();
    test_death_quit_row();
    test_death_cancel_resumes_from_any_row();
    test_death_cursor_wraps_across_five_rows();
    test_death_load_opens_the_slot_list_in_load_mode();
    test_slot_cancel_returns_to_the_death_menu();
```

- [ ] **Step 2: Update the one existing call that passes the removed parameter**

`saturn/tests/test_menu_state.cxx:418` currently reads:

```cpp
    menuStateEnterLoad(&st, MENU_TITLE, false);
```

Change it to:

```cpp
    menuStateEnterLoad(&st, MENU_TITLE);
```

That is the only place in the test file that touches the changing signature — nothing there references `retryRow` or the `MENU_SLOT_*` constants, so the existing slot-list tests keep working unchanged. Confirm with `grep -n "retryRow\|MENU_SLOT_" saturn/tests/test_menu_state.cxx` returning nothing.

- [ ] **Step 3: Run the tests to verify they fail**

Run from the repo root: `sh saturn/tests/run_tests.sh`

Expected: FAIL at `== menu state ==` with `'menuStateEnterDeath' was not declared in this scope`, `'MENU_DEATH' was not declared in this scope`, and `'MENU_ACT_SAVE_AND_QUIT' was not declared in this scope`.

- [ ] **Step 4: Add the enum values and drop the scaffolding from the header**

In `saturn/src/menu_state.h`:

Append `MENU_DEATH` to `enum MenuScreen` after `MENU_CONFIRM` (appending rather than inserting so no existing value renumbers), and append `MENU_ACT_SAVE_AND_QUIT` to `enum MenuAction` after `MENU_ACT_TOGGLE_BUTTONS`. Add a comma to the previous line in each.

Delete `bool retryRow;` from `struct MenuState`.

Delete the `MENU_SLOT_RESUME / MENU_SLOT_SAVE_RESUME / MENU_SLOT_TITLE` header block and its three `#define`s — locate them by name and remove each; do not cut a line range.

Change `menuStateEnterLoad`'s declaration and its header block. The block currently documents `back` and `retry`; the new one is:

```cpp
/*----------------------
 | menuStateEnterLoad
 | Description: Opens the slot list directly, in load mode, for a caller that
 |   has no screen behind it. back is where a cancel lands.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: st -- state to reset; back -- the screen a cancel returns to
 | Returns: N/A
 ----------------------*/
void menuStateEnterLoad(MenuState *st, MenuScreen back);
```

Add, beside it:

```cpp
/*----------------------
 | menuStateEnterDeath
 | Description: Opens the screen a death offers: five fixed rows, cursor on
 |   resume. The slot list is a sub-screen behind the load row rather than part
 |   of this screen, which is what lets it be five plain rows.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: st -- state to reset
 | Returns: N/A
 ----------------------*/
void menuStateEnterDeath(MenuState *st);
```

- [ ] **Step 5: Add stepDeath and simplify stepSlots**

In `saturn/src/menu_state.cxx`:

Remove `st->retryRow = false;` from both `menuStateEnterTitle` and `menuStateEnterPause`.

Replace `menuStateEnterLoad` in full:

```cpp
void menuStateEnterLoad(MenuState *st, MenuScreen back)
{
	st->screen = MENU_SLOTS;
	st->returnScreen = back;
	st->saving = false;
	st->slotCursor = 0;
	st->confirmYes = false;
	st->pending = MENU_ACT_NONE;
}
```

Add, after it:

```cpp
/*----------------------
 | menuStateEnterDeath
 | Description: Resets a MenuState to the death menu, cursor on resume.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: st -- state to reset
 | Returns: N/A
 ----------------------*/
void menuStateEnterDeath(MenuState *st)
{
	st->screen = MENU_DEATH;
	st->cursor = 0;
}
```

Add `stepDeath`, beside the other step functions:

```cpp
/*----------------------
 | stepDeath
 | Description: Death menu transitions: 5 items -- resume, save and resume,
 |   load, save and quit, quit. Cancel resumes from any row, which is the same
 |   thing the resume row does: there is no screen behind this one to retreat
 |   to.
 | Author: suinevere
 | Params: st -- state to advance; in -- this frame's input
 | Returns: at most one action
 ----------------------*/
static MenuAction stepDeath(MenuState *st, const MenuInput *in)
{
	if (in->cancel) {
		return MENU_ACT_RETRY;
	}
	if (in->up) {
		st->cursor = (st->cursor + 4) % 5;
		return MENU_ACT_NONE;
	}
	if (in->down) {
		st->cursor = (st->cursor + 1) % 5;
		return MENU_ACT_NONE;
	}
	if (in->confirm) {
		if (st->cursor == 0) {
			return MENU_ACT_RETRY;
		}
		if (st->cursor == 1) {
			return MENU_ACT_SAVE_RETRY;
		}
		if (st->cursor == 2) {
			st->returnScreen = MENU_DEATH;
			st->screen = MENU_SLOTS;
			st->saving = false;
			st->slotCursor = 0;
			return MENU_ACT_RESCAN_SLOTS;
		}
		if (st->cursor == 3) {
			return MENU_ACT_SAVE_AND_QUIT;
		}
		return MENU_ACT_RETURN_TO_TITLE;
	}
	return MENU_ACT_NONE;
}
```

Note the up-wrap is `(cursor + 4) % 5`. Copying a four-row wrap and only changing the modulus gives `(cursor + 3) % 5`, which makes Up skip two rows.

Replace `stepSlots` in full — the cancel special-case and the `first`/`last` juggling both existed only for the death rows:

```cpp
/*----------------------
 | stepSlots
 | Description: Slot list transitions: three slots, a device toggle on left or
 |   right when a cartridge is present, and a cancel back to whichever screen
 |   opened it.
 | Author: suinevere
 | Params: st -- state to advance; in -- this frame's input
 | Returns: at most one action
 ----------------------*/
static MenuAction stepSlots(MenuState *st, const MenuInput *in)
{
	if (in->cancel) {
		st->screen = st->returnScreen;
		return MENU_ACT_NONE;
	}
	if (in->up) {
		st->slotCursor = (st->slotCursor > 0) ? st->slotCursor - 1
		                                      : SAVE_NUM_SLOTS - 1;
		return MENU_ACT_NONE;
	}
	if (in->down) {
		st->slotCursor = (st->slotCursor < SAVE_NUM_SLOTS - 1)
		                     ? st->slotCursor + 1 : 0;
		return MENU_ACT_NONE;
	}
	if (in->left || in->right) {
		if (!st->cartPresent) {
			return MENU_ACT_NONE;
		}
		st->device = (st->device == SAT_BUP_INTERNAL) ? SAT_BUP_CART
		                                             : SAT_BUP_INTERNAL;
		return MENU_ACT_RESCAN_SLOTS;
	}
	if (in->confirm) {
		SlotState state = st->slots[st->slotCursor].state;
		if (st->saving) {
			if (state == SLOT_EMPTY) {
				return MENU_ACT_SAVE_SLOT;
			}
			st->screen = MENU_CONFIRM;
			st->pending = MENU_ACT_SAVE_SLOT;
			st->confirmYes = false;
			return MENU_ACT_NONE;
		}
		if (state == SLOT_OK) {
			return MENU_ACT_LOAD_SLOT;
		}
		return MENU_ACT_NONE;
	}
	return MENU_ACT_NONE;
}
```

Add the dispatch case in `menuStateStep`, before `default:`:

```cpp
	case MENU_DEATH:
		return stepDeath(st, in);
```

Extend the `MenuState` header block's description: the sentence about `returnScreen` remembering `MENU_TITLE` or `MENU_PAUSE` should now say it remembers which screen opened the slot list — `MENU_TITLE`, `MENU_PAUSE` or `MENU_DEATH`.

- [ ] **Step 6: Run the tests to verify they pass**

Run: `sh saturn/tests/run_tests.sh`

Expected: `all suites passed`, 11 suites.

- [ ] **Step 7: Commit**

```bash
git add saturn/src/menu_state.h saturn/src/menu_state.cxx saturn/tests/test_menu_state.cxx
git commit -m "Give the death menu its own screen of five fixed rows and put the slot list behind its load row"
```

---

### Task 2: Internal is the unconditional save device

A deletion task. Its deliverable is that the suite still passes with less code in it.

**Files:**
- Modify: `saturn/src/menu.cxx` — `ensureDevices`, and remove `menuHasAnySave`
- Modify: `saturn/src/savedata.h` — remove `savedataPickDefaultDevice`
- Modify: `saturn/src/savedata.cxx` — remove `savedataPickDefaultDevice`
- Modify: `saturn/tests/test_savedata.cxx` — remove five tests and their `main` entries

**Interfaces:**
- Consumes: nothing from Task 1.
- Produces: `savedataPickDefaultDevice` and `menuHasAnySave` no longer exist. `_st.device` is `SAT_BUP_INTERNAL` after `ensureDevices`.

**This task has no red-green cycle, and inventing one would be dishonest.** Nothing new is being built: five tests assert a decision the design no longer makes, and the code they cover becomes unreachable. Deleting obsolete tests cannot fail first — the suite passes before and after. What replaces the RED step is a grep that proves nothing still references the deleted symbols, since a dangling reference here would only surface as an SH-2 link error.

- [ ] **Step 1: Record the starting state**

Run: `sh saturn/tests/run_tests.sh`

Expected: `all suites passed`, 11 suites. Note the count — the same count must hold at the end, because no suite is being removed, only tests inside one.

- [ ] **Step 2: Remove the five obsolete tests**

In `saturn/tests/test_savedata.cxx`, delete these five functions and their `main` entries, locating each **by name**:

- `test_default_device_no_cart`
- `test_default_device_cart_has_saves`
- `test_default_device_internal_has_saves`
- `test_default_device_both_have_saves`
- `test_default_device_cart_present_but_empty`

They assert that the device is chosen by where saves already exist. The design now says it is always internal, so these are retired requirements rather than coverage being dropped.

Then run `sh saturn/tests/run_tests.sh` again. It must still pass: if it fails with `'test_default_device_…' was not declared`, a `main` entry was left behind pointing at a function you deleted.

- [ ] **Step 3: Make internal the default**

In `saturn/src/menu.cxx`, replace `ensureDevices`'s tail so it no longer consults the picker:

```cpp
	_st.cartPresent = (_devCart.present != 0);
	_st.device = SAT_BUP_INTERNAL;
```

Then extend `ensureDevices`'s header block with one sentence: the device is always internal, and the cartridge is reached with L or R from the slot list.

- [ ] **Step 4: Delete the now-dead helpers**

Remove `menuHasAnySave` from `saturn/src/menu.cxx` — its header block and its function body — locating it by name. It existed solely to feed the picker's two `hasSaves` arguments.

Remove `savedataPickDefaultDevice` from `saturn/src/savedata.h` (header block plus declaration) and from `saturn/src/savedata.cxx` (header block plus definition), again by name.

Then confirm nothing references either:

```bash
grep -rn "savedataPickDefaultDevice\|menuHasAnySave" saturn/src saturn/tests
```

Expected: no output. If anything is listed, stop and report it — it is a consumer neither the spec nor the plan anticipated.

- [ ] **Step 5: Run the tests to verify nothing regressed**

Run: `sh saturn/tests/run_tests.sh`

Expected: `all suites passed`, still 11 suites.

- [ ] **Step 6: Commit**

```bash
git add saturn/src/menu.cxx saturn/src/savedata.h saturn/src/savedata.cxx saturn/tests/test_savedata.cxx
git commit -m "Always default the save device to internal memory and delete the picker that chose between devices"
```

---

### Task 3: Draw the death panel, reword the banner, simplify the slot screen

**No host test exists for this task and none can be written.** `menu.cxx` sits outside the host-testable boundary; no suite compiles it. Run the suite to confirm nothing else broke, and say plainly in your report that it does not cover your work. Read your diff twice.

**Files:**
- Modify: `saturn/src/menu.cxx` — add `menuDrawDeathScreen`, rewrite `menuDrawSlotScreen`, remove the `MENU_DEATH_*` enum, add a `MENU_DEATH` case to `menuRenderFrame`

**Interfaces:**
- Consumes: `MENU_DEATH` and the death cursor indices 0-4 from Task 1; `SAT_BUP_INTERNAL` default from Task 2.
- Produces: `menuDrawDeathScreen(uint8_t *page, const MenuState *st, int statusError)`.

- [ ] **Step 1: Remove the MENU_DEATH_* offset enum**

In `saturn/src/menu.cxx`, delete the `MENU_DEATH_*` header block and its `enum { … }` — the one holding `MENU_DEATH_RESUME_Y`, `MENU_DEATH_SAVE_Y`, `MENU_DEATH_SLOT_TOP`, `MENU_DEATH_ROW_STEP`, `MENU_DEATH_TITLE_Y`, `MENU_DEATH_STATUS_Y`, `MENU_DEATH_HINT_Y`. Locate it by name. It existed for the "slots close up to 14 scanlines" case that no longer happens.

- [ ] **Step 2: Add the death panel**

Add to `saturn/src/menu.cxx`, immediately before `menuDrawSlotScreen`:

```cpp
/*----------------------
 | menuDrawDeathScreen
 | Description: Paints the five rows a death offers, on black. The panel is
 |   sized by the status message rather than the rows: the rows would fit the
 |   pause menu's narrower panel, but menuStatusText's longest reachable message
 |   for an internal device is 22 characters, and at cell column 8 that needs a
 |   usable region reaching x=240.
 | Author: suinevere
 | Dependencies: menu_draw.h, saturn_backup.h
 | Globals: N/A
 | Params: page -- compositing page; st -- state, for the cursor position;
 |   statusError -- a failed deferred save to report, or SAT_BUP_OK for none
 | Returns: N/A
 ----------------------*/
static void menuDrawDeathScreen(uint8_t *page, const MenuState *st,
                                int statusError) {
	const uint8_t *font = Video::_font;

	menuDrawFill(page, 48, 48, 224, 128, MENU_COL_BORDER);
	menuDrawFill(page, 50, 50, 220, 124, MENU_COL_PANEL);
	menuDrawText(page, font, 10, 64, st->cursor == 0 ? MENU_BASE_SEL : MENU_BASE_DIM, "RESUME");
	menuDrawText(page, font, 10, 80, st->cursor == 1 ? MENU_BASE_SEL : MENU_BASE_DIM, "SAVE & RESUME");
	menuDrawText(page, font, 10, 96, st->cursor == 2 ? MENU_BASE_SEL : MENU_BASE_DIM, "LOAD GAME");
	menuDrawText(page, font, 10, 112, st->cursor == 3 ? MENU_BASE_SEL : MENU_BASE_DIM, "SAVE & QUIT");
	menuDrawText(page, font, 10, 128, st->cursor == 4 ? MENU_BASE_SEL : MENU_BASE_DIM, "QUIT");
	menuDrawText(page, font, 8, 64 + st->cursor * 16, MENU_BASE_SEL, ">");

	const char *status = menuStatusText(statusError, SAT_BUP_INTERNAL);
	if (status != 0) {
		menuDrawText(page, font, 8, 148, MENU_BASE_DIM, status);
	}
}
```

Geometry check, so nothing needs re-deriving: border y=48–176, inner y=50–174; the last row's glyphs run 128–136 and the status line 148–156, both inside. Inner x=50–270; rows start at column 10 (x=80) and the longest, `SAVE & RESUME`, is 13 characters ending at x=184; the status at column 8 (x=64) has 25 characters of room.

- [ ] **Step 3: Rewrite menuDrawSlotScreen without the death branches**

Replace `menuDrawSlotScreen` in full. The banner is reworded and moved to column 6; the `retryRow` conditionals, the extra rows and the death-variant offsets all go. **The `A SELECT   B BACK` hint stays here** — on the slot list B genuinely does go back, so the line is accurate; it is only the death variant where it was wrong, and the death screen above has no hint at all.

```cpp
/*----------------------
 | menuDrawSlotScreen
 | Description: Paints the slot list, with the device banner shown only when a
 |   cartridge is present -- present, not necessarily usable, so an unformatted
 |   or write-protected cart still shows and carries its message.
 | Author: suinevere
 | Dependencies: menu_draw.h, saturn_backup.h
 | Globals: N/A
 | Params: page -- compositing page; st -- state; statusError -- last failure
 |   to report, or SAT_BUP_OK for none
 | Returns: N/A
 ----------------------*/
static void menuDrawSlotScreen(uint8_t *page, const MenuState *st,
                               int statusError) {
	const uint8_t *font = Video::_font;
	char row[40];

	menuDrawFill(page, 24, 16, 272, 168, MENU_COL_BORDER);
	menuDrawFill(page, 26, 18, 268, 164, MENU_COL_PANEL);
	menuDrawText(page, font, 15, 24, MENU_BASE_DIM,
	             st->saving ? "SAVE GAME" : "LOAD GAME");

	if (st->cartPresent) {
		menuDrawText(page, font, 6, 48, MENU_BASE_DIM,
		             st->device == SAT_BUP_CART ? "L  <     CARTRIDGE     >  R"
		                                        : "L  <  INTERNAL MEMORY  >  R");
	}

	for (int i = 0; i < SAVE_NUM_SLOTS; ++i) {
		menuSlotRow(row, (int)sizeof(row), i, &st->slots[i]);
		menuDrawText(page, font, 7, 72 + i * 16,
		             i == st->slotCursor ? MENU_BASE_SEL : MENU_BASE_DIM, row);
	}

	menuDrawText(page, font, 5, 72 + st->slotCursor * 16, MENU_BASE_SEL, ">");

	const char *status = menuStatusText(statusError, st->device);
	if (status != 0) {
		menuDrawText(page, font, 5, 136, MENU_BASE_DIM, status);
	}

	menuDrawText(page, font, 5, 160, MENU_BASE_DIM, "A SELECT   B BACK");
}
```

Both banner strings are 27 characters, so the `<` and `>` do not move when the device switches. At column 6 they span x=48–264 inside a usable region ending at x=294.

- [ ] **Step 4: Dispatch the new screen**

In `menuRenderFrame`'s `switch (st->screen)`, add before `default:`:

```cpp
	case MENU_DEATH:
		menuDrawDeathScreen(page, st, statusError);
		break;
```

- [ ] **Step 5: Run the host suite**

Run: `sh saturn/tests/run_tests.sh`

Expected: `all suites passed`. No suite compiles `menu.cxx`, so this only confirms nothing else broke. State that in your report.

- [ ] **Step 6: Commit**

```bash
git add saturn/src/menu.cxx
git commit -m "Draw the death menu as its own five-row panel and reword the slot list's device banner"
```

---

### Task 4: Save and quit, and reporting a failed deferred save

**No host test exists for this task and none can be written.** `menu.cxx` and `engine.cxx` both sit outside the host boundary. Read your diff twice; a mistake here reaches hardware.

**Files:**
- Modify: `saturn/src/engine.h` — two new fields, `autosaveCheckpoint` returns `bool`
- Modify: `saturn/src/engine.cxx` — initialise the fields, `autosaveCheckpoint`, the deferred-save block, the death-menu entry, the fade ramp
- Modify: `saturn/src/menu.h` — `runDeath`'s signature
- Modify: `saturn/src/menu.cxx` — `runDeath`

**Interfaces:**
- Consumes: `menuStateEnterDeath`, `MENU_ACT_SAVE_AND_QUIT`, `MENU_DEATH` from Task 1; `menuDrawDeathScreen` via `menuRenderFrame` from Task 3.
- Produces: `bool Menu::runDeath(int statusError, bool scriptWaiting)`; `Engine::quitAfterSave`; `Engine::deathSaveError`; `bool Engine::autosaveCheckpoint()`.

- [ ] **Step 1: Add the Engine fields and change autosaveCheckpoint's return type**

In `saturn/src/engine.h`, change `void autosaveCheckpoint();` to `bool autosaveCheckpoint();` and extend its header block's `Returns:` to `true when the write succeeded`.

Add two fields beside `bool saveAfterResume;`, each with its own header block:

```cpp
	/*----------------------
	 | Engine::quitAfterSave
	 | Description: Set with saveAfterResume by the death menu's save-and-quit
	 |   row. The save itself cannot happen at menu time -- the state then is the
	 |   death -- so the row resumes, lets the deferred save write the checkpoint
	 |   the script has since reached, and this flag says to leave afterwards.
	 | Author: suinevere
	 ----------------------*/
	bool quitAfterSave;

	/*----------------------
	 | Engine::deathSaveError
	 | Description: A deferred save's failure, waiting to be shown. Non-OK
	 |   re-opens the death menu carrying the message, because by the time the
	 |   write happens the menu has been closed for about two seconds and there
	 |   is no box on screen to report into.
	 | Author: suinevere
	 ----------------------*/
	int deathSaveError;
```

- [ ] **Step 2: Initialise them and stop discarding the save's result**

In `saturn/src/engine.cxx`'s `Engine::init`, beside the existing `saveAfterResume = false;`:

```cpp
	quitAfterSave = false;
	deathSaveError = SAT_BUP_OK;
```

Replace `autosaveCheckpoint` in full. It currently calls `saveSlot` and then forces `_lastSaveError = SAT_BUP_OK`, which is what has been hiding failed autosaves:

```cpp
bool Engine::autosaveCheckpoint() {
	return saveSlot(ENGINE_AUTOSAVE_DEVICE, ENGINE_AUTOSAVE_SLOT);
}
```

Update its header block: it returns whether the write succeeded, and it no longer clears `_lastSaveError`, so a caller can report why a save failed.

- [ ] **Step 3: Wire the deferred save's two outcomes**

In `Engine::run`, replace the deferred-save block:

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

`quitAfterSave` is cleared before either branch, so a failed attempt cannot fire a quit later.

- [ ] **Step 4: Let a pending error re-open the death menu, and hold the screen dark while quitting**

Replace the `vm.deathPrompt` block in `Engine::run`:

```cpp
			if (vm.deathPrompt || deathSaveError != SAT_BUP_OK) {
				const bool scriptWaiting = vm.deathPrompt;
				const int pending = deathSaveError;
				vm.deathPrompt = false;
				deathSaveError = SAT_BUP_OK;
				vm.deathPromptHold = VM_DEATH_PROMPT_HOLD_FRAMES;
				lit = 0;
				playing = menu.runDeath(pending, scriptWaiting);
				vm.deathScreen = false;
				video._holdDisplay = quitAfterSave;
				continue;
			}
```

`scriptWaiting` matters: on a real death the script is paused on its own prompt and resuming means feeding it a button, but on a re-entry after a failed save the script is already running and feeding it one would fire a spurious shot.

Then gate the fade ramp at the loop's foot so the screen stays dark through the save-and-quit wait:

```cpp
			if (!quitAfterSave && lit < OPENING_FADE_VM_FRAMES) {
				lit++;
				sat_fade_set((SAT_FADE_LIT * lit) / OPENING_FADE_VM_FRAMES);
			}
```

Nothing has to un-darken on the way out: `runTitle` ends with its own `sat_fade_ramp(SAT_FADE_LIT, bootFade)`.

- [ ] **Step 5: Rewrite runDeath**

In `saturn/src/menu.h`, change the declaration to `bool runDeath(int statusError, bool scriptWaiting);` and rewrite its header block:

```cpp
	/*----------------------
	 | Menu::runDeath
	 | Description: The five rows a death offers, drawn on black in place of the
	 |   script's continue prompt. Also the screen a failed deferred save
	 |   re-opens, which is what statusError carries.
	 | Author: suinevere
	 | Params: statusError -- a failed save to report, or SAT_BUP_OK for none;
	 |   scriptWaiting -- true when the script is paused on its own prompt and a
	 |   resume must feed it a button, false when this is a re-entry after a
	 |   failed save and the script is already running
	 | Returns: true to carry on playing, false to return to the title card
	 ----------------------*/
	bool runDeath(int statusError, bool scriptWaiting);
```

In `saturn/src/menu.cxx`, replace `runDeath` in full. Note what is **gone** from the head: `ensureDevices()` and `menuRescan()`, because there is no list on this screen. They now happen on the Load Game path, which `MENU_ACT_RESCAN_SLOTS` already drives — so a death no longer costs several backup RAM calls before anything appears.

```cpp
bool Menu::runDeath(int statusError, bool scriptWaiting) {
	menuStateEnterDeath(&_st);
	_statusError = statusError;

	_engine->player.stop();
	_engine->mixer.stopAll();

	_sys->setPalette(MENU_ART_PALETTE);

	MenuScreen lastScreen = MENU_NONE;
	menuRenderFrame(_page, _sys, &_st, _statusError, SAT_BUP_OK, false, false, 0, 0);
	lastScreen = _st.screen;
	menuPrimeEdges(_sys, &_prevPad, &_repeatTimer);

	// After the first frame is up, not before it: brightening while the
	// password screen is still the thing on display is a flash of exactly what
	// this menu exists to hide.
	sat_fade_ramp(SAT_FADE_LIT, OPENING_FADE_SKIP_FIELDS);

	bool resume = false;
	bool paletteOwnedByLoad = false;

	while (!_sys->input.quit) {
		MenuInput in;
		menuPollEdges(_sys, &_prevPad, &_repeatTimer, &in);

		const MenuAction act = menuStateStep(&_st, &in);

		if (act == MENU_ACT_RESCAN_SLOTS) {
			_statusError = SAT_BUP_OK;
			ensureDevices();
			menuRescan(&_st);
		} else if (act == MENU_ACT_RETRY) {
			_engine->vm.deathRetry = scriptWaiting;
			resume = true;
			break;
		} else if (act == MENU_ACT_RETURN_TO_TITLE) {
			resume = false;
			break;
		} else if (act == MENU_ACT_SAVE_RETRY) {
			_engine->vm.deathRetry = scriptWaiting;
			_engine->saveAfterResume = true;
			resume = true;
			break;
		} else if (act == MENU_ACT_SAVE_AND_QUIT) {
			_engine->vm.deathRetry = scriptWaiting;
			_engine->saveAfterResume = true;
			_engine->quitAfterSave = true;
			resume = true;
			break;
		} else if (act == MENU_ACT_LOAD_SLOT) {
			if (_engine->loadSlot(_st.device, _st.slotCursor)) {
				resume = true;
				paletteOwnedByLoad = true;
				break;
			}
			_statusError = _engine->lastSaveError();
			menuRescan(&_st);
		}

		if (_st.screen == MENU_TITLE) {
			break;
		}

		menuRenderFrame(_page, _sys, &_st, _statusError, SAT_BUP_OK, false, false, 0, 0);
		lastScreen = _st.screen;
	}

	// Out to black rather than straight back to the game: the page still holds
	// the password screen, and presenting it lit is the flash this menu exists to
	// prevent. Engine::run fades the first real frame back in.
	sat_fade_ramp(SAT_FADE_DARK, OPENING_FADE_SKIP_FIELDS);

	if (!paletteOwnedByLoad) {
		_engine->video.changePal(_engine->video.currentPaletteId);
	}

	return resume;
}
```

The two long comments are pre-existing and must be preserved verbatim — they record fade/sync findings that cost several wrong fixes to establish. They sit between statements rather than inside an expression, which is how the existing file has them.

- [ ] **Step 6: Check no other caller of runDeath was missed**

```bash
grep -rn "runDeath" saturn/src
```

Expected: the declaration in `menu.h`, the definition in `menu.cxx`, and exactly one call in `engine.cxx`, all with two arguments. If a one-argument call survives, the SH-2 build will fail and no host test will tell you.

- [ ] **Step 7: Run the host suite**

Run: `sh saturn/tests/run_tests.sh`

Expected: `all suites passed`. Neither `menu.cxx` nor `engine.cxx` is compiled by it. **Green here says nothing about whether save and quit works.** State that in your report.

- [ ] **Step 8: Commit**

```bash
git add saturn/src/engine.h saturn/src/engine.cxx saturn/src/menu.h saturn/src/menu.cxx
git commit -m "Add save and quit, which resumes to reach the checkpoint before leaving, and re-open the death menu when a deferred save fails"
```

- [ ] **Step 9: Hand over for testing on hardware**

Do not build. Report that the work is ready and list what to check:

1. Die. The menu shows five rows: RESUME, SAVE & RESUME, LOAD GAME, SAVE & QUIT, QUIT. No `A SELECT B BACK` line.
2. Cursor wraps both ways across all five rows; B resumes from any row.
3. LOAD GAME opens the slot list; B from the list returns to the **death menu**, not the title.
4. The slot list's banner reads `L  <  INTERNAL MEMORY  >  R`, and L or R switches it to `CARTRIDGE`. It is absent with no cartridge fitted.
5. Saving defaults to internal memory even with a cartridge attached.
6. SAVE & RESUME behaves as before.
7. SAVE & QUIT: the screen stays black for about two seconds, then the title appears. Check that the black gap reads as deliberate rather than as a hang — this is the one item most likely to need tuning.
8. Load the file SAVE & QUIT wrote: it should restore the checkpoint, not the death.
9. With the cartridge removed and internal memory full or unformatted, SAVE & QUIT should **not** quit — the death menu should come back with a message in its box.

---

## Self-Review

**Spec coverage.** Walked the spec section by section:

| Spec section | Task |
|---|---|
| The five rows and their actions | 1 |
| Cancel resumes; Quit does not confirm | 1 |
| `&` rather than `AND` | 3 |
| `MENU_DEATH` as its own screen | 1 |
| Deleting `retryRow`, the three `MENU_SLOT_*`, `menuStateEnterLoad`'s `retry` | 1 |
| Deleting `stepSlots`'s cancel special-case and `first`/`last` | 1 |
| Deleting the `MENU_DEATH_*` enum | 3 |
| Dropping the hint from the death screen, keeping it on the slot list | 3 |
| `returnScreen` gains `MENU_DEATH` | 1 |
| Death menu no longer touches backup RAM to open | 4 (`runDeath`'s head) |
| Internal as unconditional default | 2 |
| Deleting `savedataPickDefaultDevice`, `menuHasAnySave`, five tests | 2 |
| L/R switching needs no work | none — already true, verified in the spec |
| Banner rewording, column 6, y=48, cart-present only | 3 |
| Save & Quit's three flags and the deferred-save block | 4 |
| `autosaveCheckpoint` returns `bool`, stops discarding the error | 4 |
| Dark hold and the gated fade ramp | 4 |
| Re-opening the death menu on a failed save | 4 |
| Death panel geometry and the status line | 3 |
| Failure handling table | 4 (behaviour), 3 (the message) |
| Testing list | 1 |

No gaps. One spec line — "L/R switching needs no work" — correctly has no task.

**Placeholder scan.** No `TBD`, no `TODO`, no "similar to Task N", no "add appropriate error handling". Every code step carries its code. Step 2 of Task 2 names the specific way that step can appear to succeed wrongly rather than leaving the expected failure vague.

**Type consistency.** `runDeath(int, bool)` matches between `menu.h`, `menu.cxx` and the `engine.cxx` call site, and Step 6 greps for a missed one-argument call. `autosaveCheckpoint` is `bool` in the header, the definition and the `const bool saved = …` call. `deathSaveError` is `int` and is compared against and assigned `SAT_BUP_OK`/`lastSaveError()`, both `int`. `quitAfterSave` is `bool` and is only ever read as one. `menuStateEnterLoad` is two-argument in its declaration, its definition and the single test call. `menuDrawDeathScreen`'s three parameters match its `menuRenderFrame` call. Death cursor indices 0-4 mean the same rows in `stepDeath`, in `menuDrawDeathScreen` and in every Task 1 test.
