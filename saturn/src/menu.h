/*----------------------
 | menu.h
 | Description: The menu shell: the title card, the pause menu, the slot list
 |   and the confirm prompt, drawn into a page of its own and driven by
 |   menu_state's logic. This is the only menu file that touches the engine --
 |   menu_state.cxx and menu_draw.cxx are deliberately free of it so their logic
 |   and pixel arithmetic stay host-testable.
 | Author: suinevere
 | Dependencies: menu_state.h, saturn_backup.h
 ----------------------*/
#ifndef MENU_H
#define MENU_H

#include "menu_state.h"
#include "saturn_backup.h"

struct Engine;
struct System;

/*----------------------
 | Menu
 | Description: Owns the compositing page (never one of Video's, since the
 |   VM's pages must survive a pause untouched), the palette snapshot and the
 |   input edge detector, and turns menu_state's actions into engine calls.
 | Author: suinevere
 ----------------------*/
struct Menu {
	Engine *_engine;
	System *_sys;
	uint8_t *_page;
	MenuState _st;
	uint8_t _savedPal[32];
	int _statusError;
	uint32_t _prevPad;
	int _repeatTimer;
	bool _devicesProbed;
	SatBupDev _devInternal;
	SatBupDev _devCart;
	int _idleFrames;

	/*----------------------
	 | Menu::ensureDevices
	 | Description: Probes both backup devices and picks the starting one, once
	 |   per run, on first entry to the slot list.
	 | Author: suinevere
	 | Params: N/A
	 | Returns: N/A
	 ----------------------*/
	void ensureDevices();

	/*----------------------
	 | Menu::init
	 | Description: Binds the menu to an engine, claims the compositing page and
	 |   probes both backup devices once.
	 | Author: suinevere
	 | Params: e -- the engine whose state the menu saves, loads and restarts
	 | Returns: N/A
	 ----------------------*/
	void init(Engine *e);

	/*----------------------
	 | Menu::runTitle
	 | Description: Runs the title card until the player starts a new game or
	 |   loads one, performing that action before returning. Installs the menu's
	 |   own palette, since no game part is loaded and there is none yet.
	 | Author: suinevere
	 | Params: N/A
	 | Returns: true once a game is running; false only if the system asked to
	 |   quit before one was chosen
	 ----------------------*/
	bool runTitle();

	/*----------------------
	 | Menu::runPause
	 | Description: Runs the pause menu over the frozen frame remapped to a
	 |   monochrome ramp, restoring the game's palette before it returns.
	 | Author: suinevere
	 | Params: N/A
	 | Returns: true to resume play, false to go back to the title card
	 ----------------------*/
	bool runPause();
};

#endif /* MENU_H */
