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

#include "engine.h"
#include "file.h"
#include "serializer.h"
#include "sys.h"
#include "parts.h"
#include "savedata.h"
#include "menu.h"

Engine::Engine(System *paramSys, const char *dataDir, const char *saveDir)
	: sys(paramSys), vm(&mixer, &res, &player, &video, sys), mixer(sys), res(&video, dataDir),
	player(&mixer, &res, sys), video(&res, sys), _dataDir(dataDir), _saveDir(saveDir), _lastSaveError(SAT_BUP_OK) {
}

/*----------------------
 | Engine::run
 | Description: The top-level loop: title card, gameplay, pause menu. The VM
 |   only advances in the playing state; a menu owns the frame while it is up.
 | Author: suinevere
 | Dependencies: menu.h
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void Engine::run() {

	static Menu menu;
	menu.init(this);

	while (!sys->input.quit) {

		if (!menu.runTitle()) {
			continue;
		}

		bool playing = true;
		while (playing && !sys->input.quit) {

			vm.checkThreadRequests();

			vm.inp_updatePlayer();

			if (sys->input.pause) {
				sys->input.pause = false;
				playing = menu.runPause();
				continue;
			}

			vm.hostFrame();
		}
	}


}

Engine::~Engine(){

	finish();
	sys->destroy();
}


void Engine::init() {


	//Init system
	sys->init("Out Of This World");

#ifdef __sh__
	sat_bup_init();
#endif

	video.init();

	res.allocMemBlock();

	res.readEntries();

	vm.init();

	mixer.init();

	player.init();
}

void Engine::finish() {
	player.free();
	mixer.free();
	res.freeMemBlock();
}

/*----------------------
 | s_saveBuf
 | Description: Staging buffer for one save's worth of bytes, shared by
 |   saveSlot and loadSlot. Static because SAVE_MAX_BYTES on the stack is a
 |   2 KB frame the SH-2 cannot afford; sharing it is safe because the engine
 |   is single-threaded and neither function re-enters.
 | Author: suinevere
 ----------------------*/
static uint8_t s_saveBuf[SAVE_MAX_BYTES];

/*----------------------
 | Engine::saveSlot
 | Description: Serialises the engine into the staging buffer and writes it to
 |   a backup RAM slot. The order of the saveOrLoad calls is the save format;
 |   do not reorder them. The payload length comes from the stream's write
 |   position, not Serializer::_bytesCount -- Mixer::saveOrLoad calls
 |   saveOrLoadEntries once per audio channel, and _bytesCount is reset at the
 |   top of every such call, so it only ever holds the last channel's size.
 | Author: suinevere
 | Dependencies: savedata.h, saturn_backup.h, serializer.h
 | Globals: s_saveBuf
 | Params: device -- SAT_BUP_INTERNAL or SAT_BUP_CART; slot -- 0 to 2
 | Returns: false on a backup error or an oversized save, with lastSaveError
 |   set to a SAT_BUP_* code or ENGINE_SAVE_ERR_TOO_LARGE respectively
 ----------------------*/
bool Engine::saveSlot(uint32_t device, int slot) {
	memset(s_saveBuf, 0, sizeof(s_saveBuf));

	const uint32_t date = sat_bup_date_now();
	savedataWriteHeader(s_saveBuf, res.currentPartId, date);

	File f;
	f.openMemory(s_saveBuf + SAVE_HEADER_SIZE, sizeof(s_saveBuf) - SAVE_HEADER_SIZE, true);
	Serializer s(&f, Serializer::SM_SAVE, res._memPtrStart);
	vm.saveOrLoad(s);
	res.saveOrLoad(s);
	video.saveOrLoad(s);
	player.saveOrLoad(s);
	mixer.saveOrLoad(s);

	if (f.ioErr()) {
		_lastSaveError = ENGINE_SAVE_ERR_TOO_LARGE;
		return false;
	}

	char name[12];
	savedataSlotName(slot, name);
	const int32_t total = (int32_t)(SAVE_HEADER_SIZE + f.tell());
	_lastSaveError = sat_bup_write(device, name, "ANOTHERWLD", s_saveBuf, total, 1);
	return _lastSaveError == SAT_BUP_OK;
}

/*----------------------
 | Engine::loadSlot
 | Description: Reads a backup RAM slot into the staging buffer and
 |   deserialises it into the engine. Mutes audio before touching state, and
 |   refuses anything but the current save format version.
 | Author: suinevere
 | Dependencies: savedata.h, saturn_backup.h, serializer.h
 | Globals: s_saveBuf
 | Params: device -- SAT_BUP_INTERNAL or SAT_BUP_CART; slot -- 0 to 2
 | Returns: false on a backup error, a version mismatch, or an I/O error, with
 |   lastSaveError set
 ----------------------*/
bool Engine::loadSlot(uint32_t device, int slot) {
	char name[12];
	savedataSlotName(slot, name);
	_lastSaveError = sat_bup_read(device, name, s_saveBuf, sizeof(s_saveBuf));
	if (_lastSaveError != SAT_BUP_OK) {
		return false;
	}

	uint16_t ver, partId;
	uint32_t date;
	if (!savedataReadHeader(s_saveBuf, &ver, &partId, &date) || ver != Serializer::CUR_VER) {
		_lastSaveError = SAT_BUP_ERR_BROKEN;
		return false;
	}

	player.stop();
	mixer.stopAll();

	File f;
	f.openMemory(s_saveBuf + SAVE_HEADER_SIZE, sizeof(s_saveBuf) - SAVE_HEADER_SIZE, false);
	Serializer s(&f, Serializer::SM_LOAD, res._memPtrStart, ver);
	vm.saveOrLoad(s);
	res.saveOrLoad(s);
	video.saveOrLoad(s);
	player.saveOrLoad(s);
	mixer.saveOrLoad(s);

	if (f.ioErr()) {
		_lastSaveError = SAT_BUP_ERR_BROKEN;
		return false;
	}

	_lastSaveError = SAT_BUP_OK;
	return true;
}

/*----------------------
 | Engine::startNewGame
 | Description: Begins a fresh run at the intro. BYPASS_PROTECTION selects the
 |   intro over the copy-protection wheel, which is unplayable without the
 |   physical code wheel.
 | Author: suinevere
 | Dependencies: parts.h
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void Engine::startNewGame() {
#ifdef BYPASS_PROTECTION
	vm.initForPart(GAME_PART2);
#else
	vm.initForPart(GAME_PART1);
#endif
}
