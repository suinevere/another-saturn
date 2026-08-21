/*----------------------
 | menu.h
 | Description: The menu shell: the title card, the pause menu, the slot list,
 |   the confirm prompt and the death screen, drawn into a page of its own and
 |   driven by menu_state's logic. This is the only menu file that touches the
 |   engine -- menu_state.cxx and menu_draw.cxx are deliberately free of it so
 |   their logic and pixel arithmetic stay host-testable.
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
 |   The settings write keeps its own error slot, so a failed save cannot paint
 |   NOT SAVED under the button-layout row.
 | Author: suinevere
 ----------------------*/
struct Menu {
	Engine *_engine;
	System *_sys;
	uint8_t *_page;
	char _loadDiag[20];
	MenuState _st;
	uint8_t _savedPal[32];
	int _statusError;
	int _settingsError;
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
