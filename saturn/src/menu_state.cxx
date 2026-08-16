/*----------------------
 | menu_state.cxx
 | Description: The title, pause, slot-list and confirm screen transitions.
 |   Pure logic: no drawing, no backup RAM calls, no engine references.
 | Author: suinevere
 | Dependencies: menu_state.h
 ----------------------*/
#include "menu_state.h"

/*----------------------
 | menuStateEnterTitle
 | Description: Resets a MenuState to the title card, cursor on start game.
 | Author: suinevere
 | Params: st -- state to reset
 | Returns: N/A
 ----------------------*/
void menuStateEnterTitle(MenuState *st)
{
	st->screen = MENU_TITLE;
	st->cursor = 0;
	st->retryRow = false;
}

/*----------------------
 | menuStateEnterPause
 | Description: Resets a MenuState to the pause menu, cursor on resume.
 | Author: suinevere
 | Params: st -- state to reset
 | Returns: N/A
 ----------------------*/
void menuStateEnterPause(MenuState *st)
{
	st->screen = MENU_PAUSE;
	st->cursor = 0;
	st->retryRow = false;
}

/*----------------------
 | stepTitle
 | Description: Title card transitions: 2 items, start game and load.
 | Author: suinevere
 | Params: st -- state to advance; in -- this frame's input
 | Returns: at most one action
 ----------------------*/
static MenuAction stepTitle(MenuState *st, const MenuInput *in)
{
	if (in->up || in->down) {
		st->cursor = 1 - st->cursor;
		return MENU_ACT_NONE;
	}
	if (in->confirm) {
		if (st->cursor == 0) {
			return MENU_ACT_START_GAME;
		}
		st->returnScreen = MENU_TITLE;
		st->screen = MENU_SLOTS;
		st->saving = false;
		st->slotCursor = 0;
		return MENU_ACT_RESCAN_SLOTS;
	}
	return MENU_ACT_NONE;
}

/*----------------------
 | stepPause
 | Description: Pause menu transitions: 5 items -- resume, save, load, the
 |   button layout, and return to title. Cancel and the pause button both
 |   resume immediately, whatever the cursor position. The layout row flips on
 |   confirm only: left and right auto-repeat, and each repeat would be another
 |   blocking backup-RAM write.
 | Author: suinevere
 | Params: st -- state to advance; in -- this frame's input
 | Returns: at most one action
 ----------------------*/
static MenuAction stepPause(MenuState *st, const MenuInput *in)
{
	if (in->cancel || in->pause) {
		return MENU_ACT_RESUME;
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
			return MENU_ACT_RESUME;
		}
		if (st->cursor == 1 || st->cursor == 2) {
			st->returnScreen = MENU_PAUSE;
			st->screen = MENU_SLOTS;
			st->saving = (st->cursor == 1);
			st->slotCursor = 0;
			return MENU_ACT_RESCAN_SLOTS;
		}
		if (st->cursor == 3) {
			st->swapButtons = !st->swapButtons;
			return MENU_ACT_TOGGLE_BUTTONS;
		}
		st->screen = MENU_CONFIRM;
		st->pending = MENU_ACT_RETURN_TO_TITLE;
		st->confirmYes = false;
		return MENU_ACT_NONE;
	}
	return MENU_ACT_NONE;
}

/*----------------------
 | menuStateEnterLoad
 | Description: Opens the slot list in load mode with no screen behind it, for
 |   the death prompt. returnScreen is set here rather than by the caller
 |   because it stays private to this file.
 | Author: suinevere
 | Params: st -- state to reset; back -- where a cancel lands; retry -- whether
 |   to offer the retry row
 | Returns: N/A
 ----------------------*/
void menuStateEnterLoad(MenuState *st, MenuScreen back, bool retry)
{
	st->screen = MENU_SLOTS;
	st->returnScreen = back;
	st->saving = false;
	st->slotCursor = retry ? MENU_SLOT_RESUME : 0;
	st->confirmYes = false;
	st->retryRow = retry;
	st->pending = MENU_ACT_NONE;
}

/*----------------------
 | stepSlots
 | Description: Slot-list transitions: row cursor, device toggle when a cart
 |   is present, cancel back to the opening screen, and confirm -- which
 |   saves or loads depending on st->saving and the selected slot's state.
 | Author: suinevere
 | Params: st -- state to advance; in -- this frame's input
 | Returns: at most one action
 ----------------------*/
static MenuAction stepSlots(MenuState *st, const MenuInput *in)
{
	// Backing out of the death menu is a resume, not a retreat -- there is no
	// screen behind it, and leaving the run is its own row now.
	if (in->cancel) {
		if (st->retryRow) {
			return MENU_ACT_RETRY;
		}
		st->screen = st->returnScreen;
		return MENU_ACT_NONE;
	}
	const int first = st->retryRow ? MENU_SLOT_RESUME : 0;
	const int last = st->retryRow ? MENU_SLOT_TITLE : SAVE_NUM_SLOTS - 1;
	if (in->up) {
		st->slotCursor = (st->slotCursor > first) ? st->slotCursor - 1 : last;
		return MENU_ACT_NONE;
	}
	if (in->down) {
		st->slotCursor = (st->slotCursor < last) ? st->slotCursor + 1 : first;
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
		if (st->slotCursor == MENU_SLOT_RESUME) {
			return MENU_ACT_RETRY;
		}
		if (st->slotCursor == MENU_SLOT_SAVE_RESUME) {
			return MENU_ACT_SAVE_RETRY;
		}
		if (st->slotCursor == MENU_SLOT_TITLE) {
			return MENU_ACT_RETURN_TO_TITLE;
		}
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

/*----------------------
 | stepConfirm
 | Description: Yes/no prompt transitions. confirmYes starts false whenever
 |   the screen is entered; left/right flip it, cancel and a "no" confirm
 |   both back out to the screen that asked, and a "yes" confirm carries out
 |   st->pending and lands on the screen the caller expects for it.
 | Author: suinevere
 | Params: st -- state to advance; in -- this frame's input
 | Returns: st->pending on a "yes" confirm, MENU_ACT_NONE otherwise
 ----------------------*/
static MenuAction stepConfirm(MenuState *st, const MenuInput *in)
{
	MenuScreen declineScreen =
	    (st->pending == MENU_ACT_RETURN_TO_TITLE) ? MENU_PAUSE : MENU_SLOTS;

	if (in->left || in->right) {
		st->confirmYes = !st->confirmYes;
		return MENU_ACT_NONE;
	}
	if (in->cancel) {
		st->screen = declineScreen;
		return MENU_ACT_NONE;
	}
	if (in->confirm) {
		if (!st->confirmYes) {
			st->screen = declineScreen;
			return MENU_ACT_NONE;
		}
		MenuAction action = st->pending;
		st->screen =
		    (action == MENU_ACT_RETURN_TO_TITLE) ? MENU_TITLE : MENU_PAUSE;
		return action;
	}
	return MENU_ACT_NONE;
}

/*----------------------
 | menuStateStep
 | Description: Advances the state machine by one edge-triggered input frame,
 |   dispatching to the current screen's handler.
 | Author: suinevere
 | Params: st -- state to advance; in -- this frame's edge-triggered input
 | Returns: at most one action for the caller to carry out
 ----------------------*/
MenuAction menuStateStep(MenuState *st, const MenuInput *in)
{
	switch (st->screen) {
	case MENU_TITLE:
		return stepTitle(st, in);
	case MENU_PAUSE:
		return stepPause(st, in);
	case MENU_SLOTS:
		return stepSlots(st, in);
	case MENU_CONFIRM:
		return stepConfirm(st, in);
	default:
		return MENU_ACT_NONE;
	}
}
