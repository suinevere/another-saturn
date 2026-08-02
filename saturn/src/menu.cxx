/*----------------------
 | menu.cxx
 | Description: The screens themselves -- title card, pause menu, slot list and
 |   confirm prompt -- plus the input edge detector and the palette handling
 |   that lets the pause menu sit over a dimmed copy of the frozen frame. The
 |   logic lives in menu_state.cxx and the pixels in menu_draw.cxx; this file is
 |   the glue that owns the page, talks to Engine, and reads backup RAM.
 | Author: suinevere
 | Dependencies: menu.h, engine.h, sys.h, video.h, menu_draw.h, savedata.h,
 |   saturn_backup.h, saturn_platform.h
 | Globals: s_menuPage
 ----------------------*/
#include "menu.h"
#include "engine.h"
#include "sys.h"
#include "video.h"
#include "menu_draw.h"
#include "savedata.h"
#include "saturn_backup.h"
#include "saturn_platform.h"

extern "C" {
#include <string.h>
}

/*----------------------
 | s_menuPage
 | Description: The menu's own compositing page. Deliberately a static rather
 |   than one of Video::_pages: the VM's pages must come through a pause
 |   untouched, so the menu never borrows one.
 | Author: suinevere
 ----------------------*/
static uint8_t s_menuPage[MENU_PAGE_SIZE];

/*----------------------
 | MENU_COL_TEXT / MENU_COL_PANEL
 | Description: Palette indices the menu draws with. Text is always index 15
 |   because the pause overlay dims every entry except that one, so a single
 |   bright index is the only colour that reads correctly over both the menu's
 |   own palette and a dimmed game palette. Selection is shown by the cursor
 |   glyph rather than a second colour, for the same reason.
 | Author: suinevere
 ----------------------*/
enum {
	MENU_COL_TEXT  = 15,
	MENU_COL_PANEL = 0
};

/*----------------------
 | MENU_REPEAT_DELAY / MENU_REPEAT_RATE
 | Description: D-pad auto-repeat, in frames: how long a direction must be held
 |   before it repeats, and how often it repeats after that.
 | Author: suinevere
 ----------------------*/
enum {
	MENU_REPEAT_DELAY = 20,
	MENU_REPEAT_RATE  = 4
};

/*----------------------
 | MENU_PAD_*
 | Description: One frame of menu input, collapsed to a bitmask so edges and
 |   auto-repeat can be computed with plain bit arithmetic.
 | Author: suinevere
 ----------------------*/
enum {
	MENU_PAD_UP      = 1 << 0,
	MENU_PAD_DOWN    = 1 << 1,
	MENU_PAD_LEFT    = 1 << 2,
	MENU_PAD_RIGHT   = 1 << 3,
	MENU_PAD_CONFIRM = 1 << 4,
	MENU_PAD_CANCEL  = 1 << 5,
	MENU_PAD_PAUSE   = 1 << 6,
	MENU_PAD_L       = 1 << 7,
	MENU_PAD_R       = 1 << 8,
	MENU_PAD_NAV     = MENU_PAD_UP | MENU_PAD_DOWN | MENU_PAD_LEFT | MENU_PAD_RIGHT,
	MENU_PAD_EDGE    = MENU_PAD_CONFIRM | MENU_PAD_CANCEL | MENU_PAD_PAUSE |
	                   MENU_PAD_L | MENU_PAD_R
};

/*----------------------
 | s_titlePalette
 | Description: The palette the title card installs, since no game part is
 |   loaded at that point and the engine has not set one. Two bytes per entry,
 |   four bits per channel: R = byte0 & 0x0F, G = (byte1 & 0xF0) >> 4,
 |   B = byte1 & 0x0F. Only index 0 (black) and index 15 (white) are used.
 | Author: suinevere
 ----------------------*/
static const uint8_t s_titlePalette[32] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0F, 0xFF
};

/*----------------------
 | menuAppendChar
 | Description: Appends one character to a bounded string builder, keeping the
 |   result NUL-terminated and dropping anything past the buffer.
 | Author: suinevere
 | Params: dst -- buffer; cap -- its size including the terminator; pos -- the
 |   write cursor, advanced in place; c -- the character
 | Returns: N/A
 ----------------------*/
static void menuAppendChar(char *dst, int cap, int *pos, char c) {
	if (*pos < cap - 1) {
		dst[*pos] = c;
		(*pos)++;
		dst[*pos] = 0;
	}
}

/*----------------------
 | menuAppendStr
 | Description: Appends a NUL-terminated string to a bounded string builder.
 | Author: suinevere
 | Params: dst -- buffer; cap -- its size including the terminator; pos -- the
 |   write cursor, advanced in place; s -- the string
 | Returns: N/A
 ----------------------*/
static void menuAppendStr(char *dst, int cap, int *pos, const char *s) {
	while (*s != 0) {
		menuAppendChar(dst, cap, pos, *s);
		++s;
	}
}

/*----------------------
 | menuAppendPad2
 | Description: Appends a zero-padded two-digit number. Written out rather than
 |   reaching for sprintf, which would pull stdio into a translation unit that
 |   has no other need of it.
 | Author: suinevere
 | Params: dst -- buffer; cap -- its size including the terminator; pos -- the
 |   write cursor, advanced in place; v -- the value, clamped to 0..99
 | Returns: N/A
 ----------------------*/
static void menuAppendPad2(char *dst, int cap, int *pos, int v) {
	if (v < 0) {
		v = 0;
	}
	if (v > 99) {
		v = 99;
	}
	menuAppendChar(dst, cap, pos, (char)('0' + (v / 10)));
	menuAppendChar(dst, cap, pos, (char)('0' + (v % 10)));
}

/*----------------------
 | menuStatusText
 | Description: Maps a save or load failure to the line shown under the slot
 |   list. ENGINE_SAVE_ERR_TOO_LARGE is deliberately not folded into the
 |   not-enough-space message: it means the engine's own state outgrew the
 |   staging buffer, so the device may be entirely empty and telling the player
 |   to free space would send them to delete saves that are not the cause.
 | Author: suinevere
 | Params: err -- a SAT_BUP_* or ENGINE_SAVE_ERR_* code; device -- which device
 |   the failure happened on, to word the unformatted case
 | Returns: the message, or NULL when there is nothing to report
 ----------------------*/
static const char *menuStatusText(int err, uint32_t device) {
	switch (err) {
	case SAT_BUP_OK:
		return 0;
	case ENGINE_SAVE_ERR_TOO_LARGE:
		return "SAVE STATE TOO LARGE";
	case SAT_BUP_ERR_NONE:
		return "NO BACKUP DEVICE";
	case SAT_BUP_ERR_UNFORMAT:
		return (device == SAT_BUP_CART) ? "CARTRIDGE UNFORMATTED"
		                                : "BACKUP RAM UNFORMATTED";
	case SAT_BUP_ERR_PROTECTED:
		return "CARTRIDGE WRITE PROTECTED";
	case SAT_BUP_ERR_NO_SPACE:
		return "NOT ENOUGH SPACE";
	case SAT_BUP_ERR_NOT_FOUND:
		return "SAVE NOT FOUND";
	case SAT_BUP_ERR_EXISTS:
		return "SLOT ALREADY IN USE";
	case SAT_BUP_ERR_BROKEN:
		return "SAVE DATA DAMAGED";
	default:
		return "SAVE FAILED";
	}
}

/*----------------------
 | menuSlotRow
 | Description: Builds one slot row for the list: index, chapter name and
 |   timestamp for a usable save, or a bracketed state for anything else.
 | Author: suinevere
 | Params: out -- buffer; cap -- its size including the terminator; slot -- the
 |   0-based index shown as 1-based; info -- what savedataProbe found
 | Returns: N/A
 ----------------------*/
static void menuSlotRow(char *out, int cap, int slot, const SlotInfo *info) {
	int pos = 0;
	out[0] = 0;

	menuAppendChar(out, cap, &pos, (char)('1' + slot));
	menuAppendStr(out, cap, &pos, "  ");

	if (info->state == SLOT_EMPTY) {
		menuAppendStr(out, cap, &pos, "- EMPTY -");
		return;
	}
	if (info->state == SLOT_DAMAGED) {
		menuAppendStr(out, cap, &pos, "- DAMAGED -");
		return;
	}
	if (info->state == SLOT_OLD_VERSION) {
		menuAppendStr(out, cap, &pos, "- OLD SAVE -");
		return;
	}

	const char *name = savedataChapterName(info->partId);
	int written = 0;
	while (name[written] != 0) {
		menuAppendChar(out, cap, &pos, name[written]);
		++written;
	}
	while (written < 15) {
		menuAppendChar(out, cap, &pos, ' ');
		++written;
	}

	int month = 0;
	int day = 0;
	int hour = 0;
	int minute = 0;
	sat_bup_date_split(info->date, &month, &day, &hour, &minute);

	menuAppendPad2(out, cap, &pos, month);
	menuAppendChar(out, cap, &pos, '/');
	menuAppendPad2(out, cap, &pos, day);
	menuAppendChar(out, cap, &pos, ' ');
	menuAppendPad2(out, cap, &pos, hour);
	menuAppendChar(out, cap, &pos, ':');
	menuAppendPad2(out, cap, &pos, minute);
}

/*----------------------
 | menuHasAnySave
 | Description: Whether a device holds at least one loadable save. This is what
 |   savedataPickDefaultDevice's hasSaves arguments mean -- it does not derive
 |   them itself, and a wrong answer here silently selects the wrong device.
 | Author: suinevere
 | Params: device -- SAT_BUP_INTERNAL or SAT_BUP_CART
 | Returns: 1 if any slot probes as SLOT_OK, 0 otherwise
 ----------------------*/
static int menuHasAnySave(uint32_t device) {
	for (int i = 0; i < SAVE_NUM_SLOTS; ++i) {
		SlotInfo info;
		if (savedataProbe(device, i, &info) == SLOT_OK) {
			return 1;
		}
	}
	return 0;
}

void Menu::init(Engine *e) {
	_engine = e;
	_sys = e->sys;
	_page = s_menuPage;
	_statusError = SAT_BUP_OK;
	_prevPad = 0;
	_repeatTimer = 0;
	_devicesProbed = false;

	memset(&_st, 0, sizeof(_st));
	memset(_savedPal, 0, sizeof(_savedPal));
	memset(_dimPal, 0, sizeof(_dimPal));
	memset(&_devInternal, 0, sizeof(_devInternal));
	memset(&_devCart, 0, sizeof(_devCart));

	_st.device = SAT_BUP_INTERNAL;
}

/*----------------------
 | Menu::ensureDevices
 | Description: Probes both backup devices and picks the starting one, once per
 |   run. Deliberately not done in init: this is the first code in the whole
 |   port that actually executes BIOS backup calls, and running it before the
 |   first frame is presented turns any fault there into a black screen with
 |   nothing to go on. Deferring it to the moment the slot list opens keeps the
 |   title card and Start Game working even if backup RAM does not.
 | Author: suinevere
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void Menu::ensureDevices() {
	if (_devicesProbed) {
		return;
	}
	_devicesProbed = true;

	sat_bup_probe(SAT_BUP_INTERNAL, &_devInternal);
	sat_bup_probe(SAT_BUP_CART, &_devCart);

	_st.cartPresent = (_devCart.present != 0);
	_st.device = savedataPickDefaultDevice(&_devInternal, &_devCart,
	                                       menuHasAnySave(SAT_BUP_INTERNAL),
	                                       _devCart.present
	                                           ? menuHasAnySave(SAT_BUP_CART)
	                                           : 0);
}

/*----------------------
 | menuPadMask
 | Description: Polls the pad and collapses one frame of it into a bitmask.
 |   Shoulder buttons get their own bits rather than folding into the D-pad's,
 |   so the slot list's device toggle can be edge-only while the directions
 |   auto-repeat.
 | Author: suinevere
 | Params: sys -- the system to poll
 | Returns: the MENU_PAD_* bits held this frame
 ----------------------*/
static uint32_t menuPadMask(System *sys) {
	sys->processEvents();

	uint32_t now = 0;
	if (sys->input.dirMask & PlayerInput::DIR_UP)    now |= MENU_PAD_UP;
	if (sys->input.dirMask & PlayerInput::DIR_DOWN)  now |= MENU_PAD_DOWN;
	if (sys->input.dirMask & PlayerInput::DIR_LEFT)  now |= MENU_PAD_LEFT;
	if (sys->input.dirMask & PlayerInput::DIR_RIGHT) now |= MENU_PAD_RIGHT;
	if (sys->input.menuLeft)  now |= MENU_PAD_L;
	if (sys->input.menuRight) now |= MENU_PAD_R;
	if (sys->input.menuConfirm) now |= MENU_PAD_CONFIRM;
	if (sys->input.menuCancel)  now |= MENU_PAD_CANCEL;
	if (sys->input.pause)       now |= MENU_PAD_PAUSE;

	return now;
}

/*----------------------
 | menuPrimeEdges
 | Description: Seeds the edge detector with whatever is held right now, so a
 |   button still down from the action that opened this screen is not read as a
 |   fresh press. Without this a menu entered by pressing Start sees Start as
 |   newly pressed on its first poll and closes itself before drawing once --
 |   PlayerInput is level-triggered, and a human press spans many frames.
 | Author: suinevere
 | Params: sys -- the system to poll; prevPad -- seeded in place; repeatTimer --
 |   reset in place
 | Returns: N/A
 ----------------------*/
static void menuPrimeEdges(System *sys, uint32_t *prevPad, int *repeatTimer) {
	*prevPad = menuPadMask(sys);
	*repeatTimer = 0;
}

/*----------------------
 | menuPollEdges
 | Description: Reads one frame of pad state and turns it into the
 |   edge-triggered form menuStateStep expects, applying auto-repeat to the
 |   directions only. PlayerInput is level-triggered by design, so this is the
 |   only place held buttons become presses.
 | Author: suinevere
 | Params: sys -- the system to poll; prevPad -- last frame's mask, updated in
 |   place; repeatTimer -- auto-repeat countdown, updated in place; out --
 |   filled with this frame's edges
 | Returns: N/A
 ----------------------*/
static void menuPollEdges(System *sys, uint32_t *prevPad, int *repeatTimer,
                          MenuInput *out) {
	const uint32_t now = menuPadMask(sys);

	const uint32_t nav = now & MENU_PAD_NAV;
	const uint32_t prevNav = *prevPad & MENU_PAD_NAV;
	uint32_t fired = nav & ~prevNav;

	if (fired != 0) {
		*repeatTimer = MENU_REPEAT_DELAY;
	} else if (nav != 0 && nav == prevNav) {
		(*repeatTimer)--;
		if (*repeatTimer <= 0) {
			fired = nav;
			*repeatTimer = MENU_REPEAT_RATE;
		}
	} else if (nav == 0) {
		*repeatTimer = 0;
	}

	fired |= now & ~(*prevPad) & MENU_PAD_EDGE;
	*prevPad = now;

	out->up      = (fired & MENU_PAD_UP) != 0;
	out->down    = (fired & MENU_PAD_DOWN) != 0;
	out->left    = (fired & (MENU_PAD_LEFT | MENU_PAD_L)) != 0;
	out->right   = (fired & (MENU_PAD_RIGHT | MENU_PAD_R)) != 0;
	out->confirm = (fired & MENU_PAD_CONFIRM) != 0;
	out->cancel  = (fired & MENU_PAD_CANCEL) != 0;
	out->pause   = (fired & MENU_PAD_PAUSE) != 0;
}

/*----------------------
 | menuRescan
 | Description: Re-probes the three slots on the state's current device. Called
 |   whenever menuStateStep returns MENU_ACT_RESCAN_SLOTS, which it does on
 |   entering the slot list and on every device toggle.
 | Author: suinevere
 | Params: st -- state whose slots array is refilled
 | Returns: N/A
 ----------------------*/
static void menuRescan(MenuState *st) {
	for (int i = 0; i < SAVE_NUM_SLOTS; ++i) {
		savedataProbe(st->device, i, &st->slots[i]);
	}
}

/*----------------------
 | menuDrawTitleScreen
 | Description: Paints the title card: the game name and the two entry points.
 | Author: suinevere
 | Params: page -- compositing page; st -- state, for the cursor position
 | Returns: N/A
 ----------------------*/
static void menuDrawTitleScreen(uint8_t *page, const MenuState *st) {
	const uint8_t *font = Video::_font;

	memset(page, 0, MENU_PAGE_SIZE);
	menuDrawText(page, font, 13, 56, MENU_COL_TEXT, "ANOTHER  WORLD");
	menuDrawText(page, font, 16, 104, MENU_COL_TEXT, "START GAME");
	menuDrawText(page, font, 16, 120, MENU_COL_TEXT, "LOAD GAME");
	menuDrawText(page, font, 14, 104 + st->cursor * 16, MENU_COL_TEXT, ">");
}

/*----------------------
 | menuDrawPauseScreen
 | Description: Paints the pause panel over whatever is already in the page,
 |   which is the dimmed frozen frame.
 | Author: suinevere
 | Params: page -- compositing page; st -- state, for the cursor position
 | Returns: N/A
 ----------------------*/
static void menuDrawPauseScreen(uint8_t *page, const MenuState *st) {
	const uint8_t *font = Video::_font;

	menuDrawFill(page, 80, 48, 168, 96, MENU_COL_PANEL);
	menuDrawText(page, font, 13, 60, MENU_COL_TEXT, "RESUME");
	menuDrawText(page, font, 13, 76, MENU_COL_TEXT, "SAVE GAME");
	menuDrawText(page, font, 13, 92, MENU_COL_TEXT, "LOAD GAME");
	menuDrawText(page, font, 13, 108, MENU_COL_TEXT, "RETURN TO MENU");
	menuDrawText(page, font, 11, 60 + st->cursor * 16, MENU_COL_TEXT, ">");
}

/*----------------------
 | menuDrawSlotScreen
 | Description: Paints the slot list, with the device row shown only when a
 |   cartridge is present -- present, not necessarily usable, so an unformatted
 |   or write-protected cart still shows and carries its message.
 | Author: suinevere
 | Params: page -- compositing page; st -- state; statusError -- last failure
 |   to report, or SAT_BUP_OK for none
 | Returns: N/A
 ----------------------*/
static void menuDrawSlotScreen(uint8_t *page, const MenuState *st,
                               int statusError) {
	const uint8_t *font = Video::_font;
	char row[40];

	menuDrawFill(page, 24, 16, 272, 168, MENU_COL_PANEL);
	menuDrawText(page, font, 15, 24, MENU_COL_TEXT,
	             st->saving ? "SAVE GAME" : "LOAD GAME");

	if (st->cartPresent) {
		menuDrawText(page, font, 12, 48, MENU_COL_TEXT,
		             st->device == SAT_BUP_CART ? "L <  CARTRIDGE  > R"
		                                        : "L <   INTERNAL  > R");
	}

	for (int i = 0; i < SAVE_NUM_SLOTS; ++i) {
		menuSlotRow(row, (int)sizeof(row), i, &st->slots[i]);
		menuDrawText(page, font, 7, 72 + i * 16, MENU_COL_TEXT, row);
	}
	menuDrawText(page, font, 5, 72 + st->slotCursor * 16, MENU_COL_TEXT, ">");

	const char *status = menuStatusText(statusError, st->device);
	if (status != 0) {
		menuDrawText(page, font, 5, 136, MENU_COL_TEXT, status);
	}

	menuDrawText(page, font, 5, 160, MENU_COL_TEXT, "A SELECT   B BACK");
}

/*----------------------
 | menuDrawConfirmScreen
 | Description: Paints the yes/no prompt over whatever is already in the page.
 |   The wording follows what the pending action would destroy.
 | Author: suinevere
 | Params: page -- compositing page; st -- state, for the pending action and
 |   the current yes/no choice
 | Returns: N/A
 ----------------------*/
static void menuDrawConfirmScreen(uint8_t *page, const MenuState *st) {
	const uint8_t *font = Video::_font;

	menuDrawFill(page, 40, 64, 240, 72, MENU_COL_PANEL);

	if (st->pending == MENU_ACT_RETURN_TO_TITLE) {
		menuDrawText(page, font, 7, 76, MENU_COL_TEXT, "RETURN TO MENU ?");
		menuDrawText(page, font, 7, 92, MENU_COL_TEXT, "PROGRESS WILL BE LOST");
	} else {
		char row[24];
		int pos = 0;
		row[0] = 0;
		menuAppendStr(row, (int)sizeof(row), &pos, "OVERWRITE SLOT ");
		menuAppendChar(row, (int)sizeof(row), &pos,
		               (char)('1' + st->slotCursor));
		menuAppendStr(row, (int)sizeof(row), &pos, " ?");
		menuDrawText(page, font, 7, 84, MENU_COL_TEXT, row);
	}

	menuDrawText(page, font, 14, 116, MENU_COL_TEXT, "YES");
	menuDrawText(page, font, 22, 116, MENU_COL_TEXT, "NO");
	menuDrawText(page, font, st->confirmYes ? 13 : 21, 116, MENU_COL_TEXT, ">");
}

/*----------------------
 | menuRenderFrame
 | Description: Redraws whichever screen the state is on and presents it. When
 |   overlaying, the backdrop is re-copied first: the VM is frozen while a menu
 |   is up, so its front page stays valid as the source and the menu needs no
 |   second full-page buffer of its own.
 | Author: suinevere
 | Params: page -- compositing page; sys -- for the present call; st -- state;
 |   statusError -- last failure to report; overlay -- true to composite over
 |   the frozen frame; backdrop -- that frame, or NULL when not overlaying
 | Returns: N/A
 ----------------------*/
static void menuRenderFrame(uint8_t *page, System *sys, const MenuState *st,
                            int statusError, bool overlay,
                            const uint8_t *backdrop) {
	if (overlay) {
		memcpy(page, backdrop, MENU_PAGE_SIZE);
	}

	switch (st->screen) {
	case MENU_TITLE:
		menuDrawTitleScreen(page, st);
		break;
	case MENU_PAUSE:
		menuDrawPauseScreen(page, st);
		break;
	case MENU_SLOTS:
		if (!overlay) {
			memset(page, 0, MENU_PAGE_SIZE);
		}
		menuDrawSlotScreen(page, st, statusError);
		break;
	case MENU_CONFIRM:
		if (!overlay) {
			memset(page, 0, MENU_PAGE_SIZE);
		}
		menuDrawSlotScreen(page, st, statusError);
		menuDrawConfirmScreen(page, st);
		break;
	default:
		break;
	}

	sys->updateDisplay(page);
}

bool Menu::runTitle() {
	menuStateEnterTitle(&_st);
	_statusError = SAT_BUP_OK;

	_sys->setPalette(s_titlePalette);
	menuRenderFrame(_page, _sys, &_st, _statusError, false, 0);
	menuPrimeEdges(_sys, &_prevPad, &_repeatTimer);

	while (!_sys->input.quit) {
		MenuInput in;
		menuPollEdges(_sys, &_prevPad, &_repeatTimer, &in);

		const MenuAction act = menuStateStep(&_st, &in);

		if (act == MENU_ACT_RESCAN_SLOTS) {
			_statusError = SAT_BUP_OK;
			ensureDevices();
			menuRescan(&_st);
		} else if (act == MENU_ACT_START_GAME) {
			_engine->startNewGame();
			return true;
		} else if (act == MENU_ACT_LOAD_SLOT) {
			if (_engine->loadSlot(_st.device, _st.slotCursor)) {
				return true;
			}
			_statusError = _engine->lastSaveError();
			menuRescan(&_st);
		}

		menuRenderFrame(_page, _sys, &_st, _statusError, false, 0);
	}

	return false;
}

bool Menu::runPause() {
	const uint8_t *backdrop = _engine->video._curPagePtr2;

	menuStateEnterPause(&_st);
	_statusError = SAT_BUP_OK;

	sat_video_get_palette(_savedPal);
	menuDrawDimPalette(_savedPal, _dimPal, MENU_COL_TEXT);
	_sys->setPalette(_dimPal);

	menuRenderFrame(_page, _sys, &_st, _statusError, true, backdrop);
	menuPrimeEdges(_sys, &_prevPad, &_repeatTimer);

	bool resume = true;
	bool paletteOwnedByLoad = false;

	while (!_sys->input.quit) {
		MenuInput in;
		menuPollEdges(_sys, &_prevPad, &_repeatTimer, &in);

		const MenuAction act = menuStateStep(&_st, &in);

		if (act == MENU_ACT_RESCAN_SLOTS) {
			_statusError = SAT_BUP_OK;
			ensureDevices();
			menuRescan(&_st);
		} else if (act == MENU_ACT_RESUME) {
			resume = true;
			break;
		} else if (act == MENU_ACT_RETURN_TO_TITLE) {
			resume = false;
			break;
		} else if (act == MENU_ACT_SAVE_SLOT) {
			_engine->saveSlot(_st.device, _st.slotCursor);
			_statusError = _engine->lastSaveError();
			menuRescan(&_st);
		} else if (act == MENU_ACT_LOAD_SLOT) {
			if (_engine->loadSlot(_st.device, _st.slotCursor)) {
				resume = true;
				paletteOwnedByLoad = true;
				break;
			}
			_statusError = _engine->lastSaveError();
			menuRescan(&_st);
		}

		menuRenderFrame(_page, _sys, &_st, _statusError, true, backdrop);
	}

	if (!paletteOwnedByLoad) {
		_sys->setPalette(_savedPal);
	}
	return resume;
}
