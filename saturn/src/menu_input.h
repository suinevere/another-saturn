/*----------------------
 | menu_input.h
 | Description: The raw "which menu button is held" test shared by the title
 |   menu and the opening player, so the nine signals are enumerated once.
 | Author: suinevere
 | Dependencies: sys.h
 ----------------------*/
#ifndef MENU_INPUT_H
#define MENU_INPUT_H

#include "sys.h"

/*----------------------
 | MENU_INPUT_*
 | Description: One bit per menu button signal -- the four D-pad directions,
 |   L, R, confirm, cancel and pause.
 | Author: suinevere
 ----------------------*/
enum {
	MENU_INPUT_UP      = 1 << 0,
	MENU_INPUT_DOWN    = 1 << 1,
	MENU_INPUT_LEFT    = 1 << 2,
	MENU_INPUT_RIGHT   = 1 << 3,
	MENU_INPUT_CONFIRM = 1 << 4,
	MENU_INPUT_CANCEL  = 1 << 5,
	MENU_INPUT_PAUSE   = 1 << 6,
	MENU_INPUT_L       = 1 << 7,
	MENU_INPUT_R       = 1 << 8
};

/*----------------------
 | menuInputBits
 | Description: Reads the nine menu button signals into one bitmask. Does not
 |   call processEvents; callers poll first.
 | Author: suinevere
 | Params: sys -- the system to read
 | Returns: the MENU_INPUT_* bits held this frame
 ----------------------*/
inline uint32_t menuInputBits(System *sys)
{
	uint32_t now = 0;
	if (sys->input.dirMask & PlayerInput::DIR_UP)    now |= MENU_INPUT_UP;
	if (sys->input.dirMask & PlayerInput::DIR_DOWN)  now |= MENU_INPUT_DOWN;
	if (sys->input.dirMask & PlayerInput::DIR_LEFT)  now |= MENU_INPUT_LEFT;
	if (sys->input.dirMask & PlayerInput::DIR_RIGHT) now |= MENU_INPUT_RIGHT;
	if (sys->input.menuLeft)    now |= MENU_INPUT_L;
	if (sys->input.menuRight)   now |= MENU_INPUT_R;
	if (sys->input.menuConfirm) now |= MENU_INPUT_CONFIRM;
	if (sys->input.menuCancel)  now |= MENU_INPUT_CANCEL;
	if (sys->input.pause)       now |= MENU_INPUT_PAUSE;
	return now;
}

#endif /* MENU_INPUT_H */
