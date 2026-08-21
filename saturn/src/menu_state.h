/*----------------------
 | menu_state.h
 | Description: Pure logic for the title, pause, slot-list, confirm and death
 |   screens -- no drawing, no backup RAM calls, no engine references. That is
 |   what makes it host-testable; menu.cxx drives it with real input and turns
 |   its actions into drawing and savedata/backup calls.
 | Author: suinevere
 | Dependencies: savedata.h
 ----------------------*/
#ifndef MENU_STATE_H
#define MENU_STATE_H

#include "savedata.h"

/*----------------------
 | MenuScreen
 | Description: Which screen menuStateStep is currently driving.
 | Author: suinevere
 ----------------------*/
enum MenuScreen {
	MENU_NONE,
	MENU_TITLE,
	MENU_PAUSE,
	MENU_SLOTS,
	MENU_CONFIRM,
	MENU_DEATH
};

/*----------------------
 | MenuAction
 | Description: What menuStateStep asks the caller to do as a result of one
 |   step. At most one is returned per call.
 | Author: suinevere
 ----------------------*/
enum MenuAction {
	MENU_ACT_NONE,
	MENU_ACT_START_GAME,
	MENU_ACT_RESUME,
	MENU_ACT_SAVE_SLOT,
	MENU_ACT_LOAD_SLOT,
	MENU_ACT_RETURN_TO_TITLE,
	MENU_ACT_RETRY,
	MENU_ACT_SAVE_RETRY,
	MENU_ACT_RESCAN_SLOTS,
	MENU_ACT_TOGGLE_BUTTONS,
	MENU_ACT_SAVE_AND_QUIT
};

/*----------------------
 | MenuInput
 | Description: One frame of edge-triggered input. The caller (menu.cxx) owns
 |   edge detection and D-pad auto-repeat; each field here is true only on the
 |   call where the button became pressed.
 | Author: suinevere
 ----------------------*/
struct MenuInput {
	bool up, down, left, right, confirm, cancel, pause;
};

/*----------------------
 | MenuState
 | Description: All state the menu screens need between calls. returnScreen
 |   is private to menu_state.cxx: it remembers which screen opened the slot
 |   list (MENU_TITLE, MENU_PAUSE or MENU_DEATH) so a cancel out of MENU_SLOTS
 |   goes back to the right place. Callers must not read or write it. swapButtons
 |   is the face button layout, and is the one field the menuStateEnter*
 |   functions must never reset -- a reset would silently revert the player's
 |   choice every time a menu opened.
 | Author: suinevere
 ----------------------*/
struct MenuState {
	MenuScreen screen;
	int cursor;
	int slotCursor;
	bool saving;
	uint32_t device;
	bool cartPresent;
	bool confirmYes;
	bool swapButtons;
	MenuAction pending;
	SlotInfo slots[SAVE_NUM_SLOTS];

	MenuScreen returnScreen;
};

/*----------------------
 | menuStateEnterTitle
 | Description: Resets a MenuState to the title card, cursor on start game.
 | Author: suinevere
 | Params: st -- state to reset
 | Returns: N/A
 ----------------------*/
void menuStateEnterTitle(MenuState *st);

/*----------------------
 | menuStateEnterPause
 | Description: Resets a MenuState to the pause menu, cursor on resume.
 | Author: suinevere
 | Params: st -- state to reset
 | Returns: N/A
 ----------------------*/
void menuStateEnterPause(MenuState *st);

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

/*----------------------
 | menuStateStep
 | Description: Advances the state machine by one edge-triggered input frame.
 |   MENU_ACT_RESCAN_SLOTS means the caller must re-probe st->device and
 |   refill st->slots before the next call.
 | Author: suinevere
 | Params: st -- state to advance; in -- this frame's edge-triggered input
 | Returns: at most one action for the caller to carry out
 ----------------------*/
MenuAction menuStateStep(MenuState *st, const MenuInput *in);

#endif /* MENU_STATE_H */
