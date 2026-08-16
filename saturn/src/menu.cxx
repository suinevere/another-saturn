/*----------------------
 | menu.cxx
 | Description: The screens themselves -- title card, pause menu, slot list and
 |   confirm prompt -- plus the input edge detector and the palette handling
 |   that lets the pause menu sit over the frozen frame remapped to monochrome.
 |   The logic lives in menu_state.cxx and the pixels in menu_draw.cxx; this
 |   file is the glue that owns the page, talks to Engine, and reads backup
 |   RAM.
 | Author: suinevere
 | Dependencies: menu.h, engine.h, sys.h, video.h, menu_draw.h, menu_blit.h,
 |   menu_art.h, menu_input.h, savedata.h, saturn_backup.h, saturn_platform.h,
 |   opening.h
 | Globals: s_menuPage
 ----------------------*/
#include "menu.h"
#include "engine.h"
#include "sys.h"
#include "video.h"
#include "menu_draw.h"
#include "menu_blit.h"
#include "menu_art.h"
#include "menu_input.h"
#include "savedata.h"
#include "saturn_backup.h"
#include "saturn_platform.h"
#include "saturn_fade.h"
#include "opening.h"
#include "saturn_probe.h"

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
 | MENU_BASE_DIM / MENU_BASE_SEL / MENU_COL_PANEL / MENU_COL_BORDER
 | Description: Palette indices the menu draws with; selection is a base index
 |   rather than a second colour, so unselected art renders at base 8 and
 |   selected art at base 12 with no separate copy.
 | Author: suinevere
 ----------------------*/
enum {
	MENU_BASE_DIM    = 8,
	MENU_BASE_SEL    = 12,
	MENU_COL_PANEL   = 0,
	MENU_COL_BORDER  = 7
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
 | MENU_TITLE_IDLE_FRAMES
 | Description: How long the title screen sits untouched before the attract
 |   starts the introduction again, in frames at 60 Hz -- fifteen seconds.
 |
 |   [DEBUG-a4f2] Temporarily 60 rather than 900. The seam under investigation
 |   is only reachable through this timer, and a fifteen second wait per
 |   attempt is most of the round trip. Restore to 900 with the rest of the
 |   instrumentation.
 | Author: suinevere
 ----------------------*/
enum {
	MENU_TITLE_IDLE_FRAMES = 60
};

/*----------------------
 | MENU_TITLE_START_Y / MENU_TITLE_ROW_STEP
 | Description: Where the two title entries sit, in scanlines. They live in the
 |   band the backdrop's credit block used to occupy -- the wordmark ends at
 |   row 130 and the generator clears rows 131 to 164, so the first entry starts
 |   below both.
 | Author: suinevere
 ----------------------*/
enum {
	MENU_TITLE_START_Y  = 136,
	MENU_TITLE_ROW_STEP = 24
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
	_idleFrames = 0;

	memset(&_st, 0, sizeof(_st));
	memset(_savedPal, 0, sizeof(_savedPal));
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
 |   auto-repeat. MENU_PAD_* mirrors menuInputBits' MENU_INPUT_* bit layout,
 |   so the raw signal test itself is shared rather than restated here.
 | Author: suinevere
 | Params: sys -- the system to poll
 | Returns: the MENU_PAD_* bits held this frame
 ----------------------*/
static uint32_t menuPadMask(System *sys) {
	sys->processEvents();
	return menuInputBits(sys);
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
 | Description: Paints the title card over the opening's final frame, with the
 |   selected entry shown by ramp rather than a cursor glyph.
 | Author: suinevere
 | Params: page -- compositing page; st -- state, for the cursor position
 | Returns: N/A
 ----------------------*/
static void menuDrawTitleScreen(uint8_t *page, const MenuState *st) {
	memcpy(page, MENU_ART_TITLE_BACKDROP, MENU_PAGE_SIZE);
	menuBlit2bpp(page, &MENU_ART_START_GAME, 84, MENU_TITLE_START_Y,
	             st->cursor == 0 ? MENU_BASE_SEL : MENU_BASE_DIM);
	menuBlit2bpp(page, &MENU_ART_LOAD_GAME, 84, MENU_TITLE_START_Y + MENU_TITLE_ROW_STEP,
	             st->cursor == 1 ? MENU_BASE_SEL : MENU_BASE_DIM);
}

/*----------------------
 | MENU_PROBE_MAX_ROWS / MENU_PROBE_TOP_Y / MENU_PROBE_ROW_STEP
 | Description: [DEBUG-a4f2] Where the seam's mark table prints over the title
 |   card. Fourteen rows at eleven scanlines from y=40 ends at 183, inside the
 |   page's 200 lines.
 | Author: suinevere
 ----------------------*/
enum {
	MENU_PROBE_MAX_ROWS = 14,
	MENU_PROBE_TOP_Y    = 40,
	MENU_PROBE_ROW_STEP = 11
};

/*----------------------
 | menuProbeLine
 | Description: [DEBUG-a4f2] Formats one mark as a padded name and a decimal
 |   millisecond count. Hand rolled rather than sprintf'd to keep the debug
 |   overlay from dragging formatting code into the link.
 | Author: suinevere
 | Params: out -- at least 20 bytes; name -- mark name; ms -- elapsed
 |         milliseconds
 | Returns: N/A
 ----------------------*/
static void menuProbeLine(char *out, const char *name, uint32_t ms) {
	int n = 0;

	while (*name != '\0' && n < 6) {
		out[n++] = *name++;
	}

	while (n < 7) {
		out[n++] = ' ';
	}

	char digits[10];
	int d = 0;

	if (ms == 0) {
		digits[d++] = '0';
	}

	while (ms != 0 && d < 10) {
		digits[d++] = (char)('0' + (ms % 10));
		ms /= 10;
	}

	while (d > 0) {
		out[n++] = digits[--d];
	}

	out[n] = '\0';
}

/*----------------------
 | menuDrawProbeOverlay
 | Description: [DEBUG-a4f2] Prints the seam's mark table over the title card,
 |   one line per milestone. Draws nothing until an attract has run, so the
 |   boot title card is untouched.
 | Author: suinevere
 | Params: page -- MENU_PAGE_SIZE bytes
 | Returns: N/A
 ----------------------*/
static void menuDrawProbeOverlay(uint8_t *page) {
	const uint8_t *font = Video::_font;
	const int count = sat_probe_count();

	for (int i = 0; i < count && i < MENU_PROBE_MAX_ROWS; i++) {
		char line[20];
		menuProbeLine(line, sat_probe_name(i), sat_probe_ms(i));
		menuDrawText(page, font, 2, MENU_PROBE_TOP_Y + i * MENU_PROBE_ROW_STEP,
		             MENU_BASE_SEL, line);
	}
}

/*----------------------
 | menuDrawPauseScreen
 | Description: Paints the pause panel over whatever is already in the page,
 |   which is the frozen frame remapped to monochrome.
 | Author: suinevere
 | Params: page -- compositing page; st -- state, for the cursor position
 | Returns: N/A
 ----------------------*/
static void menuDrawPauseScreen(uint8_t *page, const MenuState *st) {
	const uint8_t *font = Video::_font;

	menuDrawFill(page, 80, 48, 168, 96, MENU_COL_BORDER);
	menuDrawFill(page, 82, 50, 164, 92, MENU_COL_PANEL);
	menuDrawText(page, font, 13, 60, st->cursor == 0 ? MENU_BASE_SEL : MENU_BASE_DIM, "RESUME");
	menuDrawText(page, font, 13, 76, st->cursor == 1 ? MENU_BASE_SEL : MENU_BASE_DIM, "SAVE GAME");
	menuDrawText(page, font, 13, 92, st->cursor == 2 ? MENU_BASE_SEL : MENU_BASE_DIM, "LOAD GAME");
	menuDrawText(page, font, 13, 108, st->cursor == 3 ? MENU_BASE_SEL : MENU_BASE_DIM, "RETURN TO MENU");
	menuDrawText(page, font, 11, 60 + st->cursor * 16, MENU_BASE_SEL, ">");
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

	menuDrawFill(page, 24, 16, 272, 168, MENU_COL_BORDER);
	menuDrawFill(page, 26, 18, 268, 164, MENU_COL_PANEL);
	menuDrawText(page, font, 15, 24, MENU_BASE_DIM,
	             st->saving ? "SAVE GAME" : "LOAD GAME");

	if (st->cartPresent) {
		menuDrawText(page, font, 12, 48, MENU_BASE_DIM,
		             st->device == SAT_BUP_CART ? "L <  CARTRIDGE  > R"
		                                        : "L <   INTERNAL  > R");
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

	menuDrawFill(page, 40, 64, 240, 72, MENU_COL_BORDER);
	menuDrawFill(page, 42, 66, 236, 68, MENU_COL_PANEL);

	if (st->pending == MENU_ACT_RETURN_TO_TITLE) {
		menuDrawText(page, font, 7, 76, MENU_BASE_DIM, "RETURN TO MENU ?");
		menuDrawText(page, font, 7, 92, MENU_BASE_DIM, "PROGRESS WILL BE LOST");
	} else {
		char row[24];
		int pos = 0;
		row[0] = 0;
		menuAppendStr(row, (int)sizeof(row), &pos, "OVERWRITE SLOT ");
		menuAppendChar(row, (int)sizeof(row), &pos,
		               (char)('1' + st->slotCursor));
		menuAppendStr(row, (int)sizeof(row), &pos, " ?");
		menuDrawText(page, font, 7, 84, MENU_BASE_DIM, row);
	}

	menuDrawText(page, font, 14, 116, st->confirmYes ? MENU_BASE_SEL : MENU_BASE_DIM, "YES");
	menuDrawText(page, font, 22, 116, st->confirmYes ? MENU_BASE_DIM : MENU_BASE_SEL, "NO");
	menuDrawText(page, font, st->confirmYes ? 13 : 21, 116, MENU_BASE_SEL, ">");
}

/*----------------------
 | menuRenderFrame
 | Description: Redraws whichever screen the state is on and presents it. When
 |   overlaying, the backdrop is re-copied and remapped only when the caller
 |   says the panel geometry may have changed since the last frame -- the
 |   frozen frame and its palette are invariant for the whole pause session, so
 |   paying the 32000-byte remap on every unchanged frame buys nothing; each
 |   screen's draw function fills its own panel before drawing text, so
 |   skipping the recopy does not let text accumulate within a screen.
 | Author: suinevere
 | Params: page -- compositing page; sys -- for the present call; st -- state;
 |   statusError -- last failure to report; overlay -- true to composite over
 |   the frozen frame; refreshBackdrop -- true to re-copy and remap the
 |   backdrop this call, ignored when overlay is false; backdrop -- that frame,
 |   or NULL when not overlaying; freezePal -- the game palette the backdrop
 |   was drawn against, or NULL when not overlaying
 | Returns: N/A
 ----------------------*/
static void menuRenderFrame(uint8_t *page, System *sys, const MenuState *st,
                            int statusError, bool overlay, bool refreshBackdrop,
                            const uint8_t *backdrop, const uint8_t *freezePal) {
	if (overlay && refreshBackdrop) {
		memcpy(page, backdrop, MENU_PAGE_SIZE);
		menuFreezeRemap(page, freezePal);
	}

	switch (st->screen) {
	case MENU_TITLE:
		menuDrawTitleScreen(page, st);
		menuDrawProbeOverlay(page);
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

/*----------------------
 | menuRunAttract
 | Description: Everything that runs before the title card. The movie is a boot
 |   event and happens at most once in a session; the engine's own introduction
 |   is the attract proper and repeats until the player asks for the title card.
 |
 |   So this does not return when the introduction ends -- it starts it again.
 |   The only ways out are a button and a quit, which is what makes the title
 |   card something the player arrives at rather than something they wait for.
 |
 |   Always ends black, including when nothing ran at all: coming back to the
 |   title from a finished game leaves that game's last frame on screen, and it
 |   has to go somewhere.
 | Author: suinevere
 | Params: sys -- presentation and input; engine -- runs the introduction;
 |   page -- MENU_PAGE_SIZE bytes; replay -- true for the idle attract loop,
 |   which never shows the movie, false for the once-per-boot path
 | Returns: how many fields the title card's fade-in should take
 ----------------------*/
static int menuRunAttract(System *sys, Engine *engine, uint8_t *page, bool replay) {
	if (!replay) {
		const int movie = openingPlay(sys, page);

		if (movie == OPENING_SKIPPED) {
			return OPENING_FADE_SKIP_FIELDS;
		}

		if (movie == OPENING_NOT_PLAYED) {
			sat_fade_ramp(SAT_FADE_DARK, OPENING_FADE_FIELDS);
			return OPENING_FADE_FIELDS;
		}
	}

	while (engine->runIntroAttract()) {
	}

	// Reached only by a button or a quit, so the fade out of it was the short
	// one and the fade back in should match.
	return OPENING_FADE_SKIP_FIELDS;
}

bool Menu::runTitle() {
	menuStateEnterTitle(&_st);
	_statusError = SAT_BUP_OK;

	const int bootFade = menuRunAttract(_sys, _engine, _page, false);

	_sys->setPalette(MENU_ART_TITLE_PALETTE);
	menuRenderFrame(_page, _sys, &_st, _statusError, false, false, 0, 0);
	sat_fade_ramp(SAT_FADE_LIT, bootFade);
	menuPrimeEdges(_sys, &_prevPad, &_repeatTimer);

	MenuScreen lastScreen = _st.screen;
	_idleFrames = 0;

	while (!_sys->input.quit) {
		MenuInput in;
		menuPollEdges(_sys, &_prevPad, &_repeatTimer, &in);

		const MenuAction act = menuStateStep(&_st, &in);

		if (act == MENU_ACT_RESCAN_SLOTS) {
			_statusError = SAT_BUP_OK;
			ensureDevices();
			menuRescan(&_st);
		} else if (act == MENU_ACT_START_GAME) {
			sat_fade_ramp(SAT_FADE_DARK, OPENING_FADE_FIELDS);
			_engine->startNewGame();
			return true;
		} else if (act == MENU_ACT_LOAD_SLOT) {
			sat_fade_ramp(SAT_FADE_DARK, OPENING_FADE_FIELDS);
			if (_engine->loadSlot(_st.device, _st.slotCursor)) {
				return true;
			}
			sat_fade_ramp(SAT_FADE_LIT, OPENING_FADE_SKIP_FIELDS);
			_statusError = _engine->lastSaveError();
			menuRescan(&_st);
		}

		// menuPollEdges has already polled the pad this frame, so the raw held
		// bits are what _prevPad now holds -- reading them again would cost a
		// second poll and could straddle a release.
		if (_prevPad != 0 || _st.screen != lastScreen) {
			_idleFrames = 0;
		} else {
			_idleFrames++;
		}

		int fadeIn = 0;

		if (_st.screen == MENU_TITLE) {
			// The title palette is installed on edges rather than every frame:
			// the card no longer animates, so the only things that can have
			// replaced it are another screen and the attract replay.
			bool installPalette = (lastScreen != MENU_TITLE);
			if (_idleFrames >= MENU_TITLE_IDLE_FRAMES) {
				_idleFrames = 0;
				fadeIn = menuRunAttract(_sys, _engine, _page, true);
				menuPrimeEdges(_sys, &_prevPad, &_repeatTimer);
				installPalette = true;
			}
			if (installPalette) {
				_sys->setPalette(MENU_ART_TITLE_PALETTE);
			}
		} else if (lastScreen == MENU_TITLE) {
			_sys->setPalette(MENU_ART_PALETTE);
		}

		menuRenderFrame(_page, _sys, &_st, _statusError, false, false, 0, 0);

		// After the render, not before: the attract left the screen black and
		// this is the first field the title card is actually on it.
		if (fadeIn != 0) {
			sat_fade_ramp(SAT_FADE_LIT, fadeIn);
		}

		lastScreen = _st.screen;
	}

	return false;
}

bool Menu::runPause() {
	const uint8_t *backdrop = _engine->video._curPagePtr2;

	menuStateEnterPause(&_st);
	_statusError = SAT_BUP_OK;

	_engine->player.pause();
	_engine->mixer.stopAll();

	sat_video_get_palette(_savedPal);
	_sys->setPalette(MENU_ART_PALETTE);

	MenuScreen lastScreen = MENU_NONE;
	menuRenderFrame(_page, _sys, &_st, _statusError, true, true, backdrop, _savedPal);
	lastScreen = _st.screen;
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

		const bool refreshBackdrop = (_st.screen != lastScreen);
		menuRenderFrame(_page, _sys, &_st, _statusError, true, refreshBackdrop, backdrop, _savedPal);
		lastScreen = _st.screen;
	}

	if (!paletteOwnedByLoad) {
		_engine->video.changePal(_engine->video.currentPaletteId);
		_engine->player.resume();
	}
	if (resume) {
		_sys->updateDisplay(_engine->video._curPagePtr2);
	}
	return resume;
}
