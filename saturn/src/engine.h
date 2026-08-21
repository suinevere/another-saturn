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
	ENGINE_SAVE_ERR_TOO_LARGE = 100,
	ENGINE_SAVE_WARN_NO_BACKGROUND = 101
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
	bool _lastSaveNoBackground;
	int _lastLoadFrameKind;
	int _lastLoadFrameLen;
	bool _lastLoadFrameOk;

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
	/*----------------------
	 | Engine::autosaveCheckpoint
	 | Description: Writes the run to ENGINE_AUTOSAVE_SLOT. Called as a part
	 |   begins, which is the only moment the engine holds a state worth
	 |   restoring -- a save taken when the player dies restores them into their
	 |   own death.
	 |
	 |   Failures are reported, not swallowed: the return value tells the caller
	 |   whether the write succeeded, and the deferred-save path in Engine::run
	 |   uses it to re-open the death menu with the reason.
	 | Author: suinevere
	 | Params: N/A
	 | Returns: true when the write succeeded
	 ----------------------*/
	bool autosaveCheckpoint();

	/*----------------------
	 | Engine::lastSaveDroppedBackground
	 | Description: Whether the last save wrote its state but had no room left
	 |   for the background image. The save is still loadable; it just restores
	 |   to a black screen, which is invisible until the player loads it and is
	 |   why it is reported rather than silently accepted.
	 | Author: suinevere
	 | Params: N/A
	 | Returns: true when the background was dropped
	 ----------------------*/
	bool lastSaveDroppedBackground() const { return _lastSaveNoBackground; }

	/*----------------------
	 | Engine::lastLoadFrameKind / lastLoadFrameLen / lastLoadFrameOk
	 | Description: What the last loadSlot found where the background image
	 |   should be: the codec tag, its byte count, and whether it decoded.
	 |   Diagnostic only -- the background restore has never worked and these
	 |   are what tell a black screen apart from an absent frame, a refused
	 |   decode, and a decode that succeeded and was then overdrawn.
	 | Author: suinevere
	 | Params: N/A
	 | Returns: the recorded value
	 ----------------------*/
	int lastLoadFrameKind() const { return _lastLoadFrameKind; }
	int lastLoadFrameLen() const { return _lastLoadFrameLen; }
	bool lastLoadFrameOk() const { return _lastLoadFrameOk; }

	/*----------------------
	 | Engine::saveAfterResume
	 | Description: Set by the death menu's "SAVE AND RESUME" and honoured once
	 |   the retry has carried the script back into play. The save has to wait:
	 |   taken while the prompt is still up it would store the death, which
	 |   restores the player straight back into it.
	 | Author: suinevere
	 ----------------------*/
	bool saveAfterResume;

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
