/* Raw - Another World Interpreter
 * Copyright (C) 2004 Gregory Montoir
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#ifndef __ENGINE_H__
#define __ENGINE_H__

#include "intern.h"
#include "vm.h"
#include "mixer.h"
#include "sfxplayer.h"
#include "resource.h"
#include "video.h"

struct System;

/*----------------------
 | ENGINE_SAVE_ERR_TOO_LARGE
 | Description: lastSaveError() code for a save whose serialised state
 |   overflowed the SAVE_MAX_BYTES staging buffer before backup RAM was ever
 |   touched. Distinct from the SAT_BUP_* range (0-7, see saturn_backup.h) so
 |   the two error spaces cannot collide; this one is a programming error
 |   (save state too big), not a device condition, and must not be shown to
 |   the player as a full-device message.
 | Author: suinevere
 ----------------------*/
enum {
	ENGINE_SAVE_ERR_TOO_LARGE = 100
};

struct Engine {
	System *sys;
	VirtualMachine vm;
	Mixer mixer;
	Resource res;
	SfxPlayer player;
	Video video;
	const char *_dataDir, *_saveDir;
	int _lastSaveError;

	Engine(System *stub, const char *dataDir, const char *saveDir);
	~Engine();

	void run();
	void init();
	void finish();

	bool saveSlot(uint32_t device, int slot);
	bool loadSlot(uint32_t device, int slot);
	/*----------------------
	 | Engine::lastSaveError
	 | Description: Status of the most recent saveSlot/loadSlot call, for the
	 |   menu's status line.
	 | Author: suinevere
	 | Dependencies: saturn_backup.h
	 | Globals: N/A
	 | Params: N/A
	 | Returns: a SAT_BUP_* code (saturn_backup.h) or an ENGINE_SAVE_ERR_* code
	 ----------------------*/
	int lastSaveError() const { return _lastSaveError; }
	void startNewGame();

	/*----------------------
	 | Engine::runIntroAttract
	 | Description: Plays the game's own introduction as an attract, between the
	 |   opening movie and the title card. Runs GAME_PART2 the way gameplay
	 |   would, but stops at the part boundary instead of following it into
	 |   GAME_PART3, so the engine is never left running a game nobody chose.
	 |   Leaves the VM holding part 2's resources; whatever the player picks
	 |   next re-inits over them.
	 | Author: suinevere
	 | Dependencies: parts.h, menu_input.h
	 | Globals: N/A
	 | Params: N/A
	 | Returns: true if the intro ran to its own end, false if the player
	 |   skipped it or asked to quit
	 ----------------------*/
	bool runIntroAttract();
};

#endif
