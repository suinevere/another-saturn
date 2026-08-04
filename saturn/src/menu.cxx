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
 |   menu_art.h, savedata.h, saturn_backup.h, saturn_platform.h
 | Globals: s_menuPage
 ----------------------*/
#include "menu.h"
#include "engine.h"
#include "sys.h"
#include "video.h"
#include "menu_draw.h"
#include "menu_blit.h"
#include "menu_art.h"
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
	_frame = 0;
	_rng = 0xACE1;
	_boltTimer = 120;
	_boltFrame = -1;
	_boltIndex = 0;

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
 | Description: Paints the title card -- wordmark and two entry points, the
 |   selected one shown by ramp rather than a cursor glyph.
 | Author: suinevere
 | Params: page -- compositing page; st -- state, for the cursor position
 | Returns: N/A
 ----------------------*/
static void menuDrawTitleScreen(uint8_t *page, const MenuState *st) {
	memset(page, 0, MENU_PAGE_SIZE);
	menuBlit4bpp(page, &MENU_ART_LOGO, 15, 30);
	menuBlit2bpp(page, &MENU_ART_START_GAME, 84, 128,
	             st->cursor == 0 ? MENU_BASE_SEL : MENU_BASE_DIM);
	menuBlit2bpp(page, &MENU_ART_LOAD_GAME, 84, 152,
	             st->cursor == 1 ? MENU_BASE_SEL : MENU_BASE_DIM);
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
 | menuNextRandom
 | Description: One step of a 16-bit LFSR, used instead of libc's rand so the
 |   menu adds no dependency for bolt timing.
 | Author: suinevere
 | Params: state -- advanced in place
 | Returns: the new state
 ----------------------*/
static uint16_t menuNextRandom(uint16_t *state) {
	uint16_t x = *state;
	x ^= (uint16_t)(x << 7);
	x ^= (uint16_t)(x >> 9);
	x ^= (uint16_t)(x << 8);
	*state = x;
	return x;
}

/*----------------------
 | menuBoltX
 | Description: Where each bolt is drawn -- bolt 1 hangs off the left edge as
 |   bolt 0 mirrored, and bolt 2 is shorter and sits between them.
 | Author: suinevere
 | Params: index -- 0..MENU_ART_BOLT_COUNT - 1
 | Returns: the left edge in pixels
 ----------------------*/
static int menuBoltX(int index) {
	if (index == 0) {
		return 268;
	}
	return (index == 1) ? 6 : 90;
}

/*----------------------
 | MENU_STROBE_HOLD_FRAMES
 | Description: How many rendered frames each strobe table row holds, so a
 |   full triangle cycle spans (MENU_ART_STROBE_LEVELS * 2 - 2) rows times
 |   this many frames.
 | Author: suinevere
 ----------------------*/
enum {
	MENU_STROBE_HOLD_FRAMES = 3
};

/*----------------------
 | menuStrobeLevel
 | Description: Maps a frame counter to a strobe table row, holding each row
 |   for MENU_STROBE_HOLD_FRAMES frames while walking up and back down so the
 |   pulse has no seam.
 | Author: suinevere
 | Params: frame -- free-running frame counter
 | Returns: a row index, 0..MENU_ART_STROBE_LEVELS - 1
 ----------------------*/
static int menuStrobeLevel(int frame) {
	const int span = MENU_ART_STROBE_LEVELS * 2 - 2;
	int p = (frame / MENU_STROBE_HOLD_FRAMES) % span;
	if (p < 0) {
		p += span;
	}
	return (p < MENU_ART_STROBE_LEVELS) ? p : span - p;
}

/*----------------------
 | menuTitlePalette
 | Description: Builds the title screen's palette for one frame -- artwork
 |   entries as authored, entries 12..14 from the strobe table, and (while a
 |   bolt is on screen) every entry 1..14 pushed toward white, overriding the
 |   strobe for those three frames.
 | Author: suinevere
 | Params: out -- 32 bytes; frame -- frame counter, for the strobe phase;
 |         boltFrame -- 0, 1 or 2 while a bolt is lit, negative otherwise
 | Returns: N/A
 ----------------------*/
static void menuTitlePalette(uint8_t *out, int frame, int boltFrame) {
	memcpy(out, MENU_ART_PALETTE, 32);

	const uint8_t *row = MENU_ART_STROBE[menuStrobeLevel(frame)];
	for (int i = 0; i < 3; ++i) {
		out[(12 + i) * 2]     = row[i * 2];
		out[(12 + i) * 2 + 1] = row[i * 2 + 1];
	}

	if (boltFrame < 0 || boltFrame > 1) {
		return;
	}

	const int lift = (boltFrame == 0) ? 8 : 4;
	for (int i = 1; i < 15; ++i) {
		int r = (out[i * 2] & 0x0F) + lift;
		int g = ((out[i * 2 + 1] & 0xF0) >> 4) + lift;
		int b = (out[i * 2 + 1] & 0x0F) + lift;
		if (r > 15) r = 15;
		if (g > 15) g = 15;
		if (b > 15) b = 15;
		out[i * 2]     = (uint8_t)r;
		out[i * 2 + 1] = (uint8_t)((g << 4) | b);
	}
}

/*----------------------
 | menuRenderTitleFrame
 | Description: Draws the title screen, blits the current lightning bolt if
 |   one is lit, and presents.
 | Author: suinevere
 | Params: page -- compositing page; sys -- for the present call; st -- state,
 |   for the cursor position; boltIndex -- which bolt art to blit;
 |   boltFrame -- 0, 1 or 2 while a bolt is lit, negative otherwise
 | Returns: N/A
 ----------------------*/
static void menuRenderTitleFrame(uint8_t *page, System *sys,
                                 const MenuState *st, int boltIndex,
                                 int boltFrame) {
	menuDrawTitleScreen(page, st);
	if (boltFrame >= 0) {
		menuBlit4bpp(page, &MENU_ART_BOLT[boltIndex], menuBoltX(boltIndex), 0);
	}
	sys->updateDisplay(page);
}

void Menu::titleAnimate() {
	_frame++;

	if (_boltFrame >= 0) {
		_boltFrame++;
		if (_boltFrame >= 3) {
			_boltFrame = -1;
			_boltTimer = 120 + (int)(menuNextRandom(&_rng) % 181);
		}
	} else {
		_boltTimer--;
		if (_boltTimer <= 0) {
			_boltIndex = (int)(menuNextRandom(&_rng) % MENU_ART_BOLT_COUNT);
			_boltFrame = 0;
		}
	}

	uint8_t pal[32];
	menuTitlePalette(pal, _frame, _boltFrame);
	_sys->setPalette(pal);
}

bool Menu::runTitle() {
	menuStateEnterTitle(&_st);
	_statusError = SAT_BUP_OK;

	titleAnimate();
	menuRenderTitleFrame(_page, _sys, &_st, _boltIndex, _boltFrame);
	menuPrimeEdges(_sys, &_prevPad, &_repeatTimer);

	MenuScreen lastScreen = _st.screen;

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

		if (_st.screen == MENU_TITLE) {
			titleAnimate();
			menuRenderTitleFrame(_page, _sys, &_st, _boltIndex, _boltFrame);
		} else {
			if (lastScreen == MENU_TITLE) {
				_sys->setPalette(MENU_ART_PALETTE);
			}
			menuRenderFrame(_page, _sys, &_st, _statusError, false, false, 0, 0);
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
