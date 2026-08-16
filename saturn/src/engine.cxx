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
#include "menu_input.h"
#include "opening.h"
#include "page_rle.h"
#include "saturn_fade.h"
#include "saturn_probe.h"

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
		bool pauseLatched = false;
		int lit = 0;
		while (playing && !sys->input.quit) {

			vm.checkThreadRequests();

			vm.inp_updatePlayer();

			if (!sys->input.pause) {
				pauseLatched = false;
			} else if (!pauseLatched) {
				pauseLatched = true;
				// A pause inside the opening fade would otherwise put the menu
				// up half lit and go on brightening underneath it.
				lit = OPENING_FADE_VM_FRAMES;
				sat_fade_set(SAT_FADE_LIT);
				playing = menu.runPause();
				continue;
			}

			vm.hostFrame();

			if (lit < OPENING_FADE_VM_FRAMES) {
				lit++;
				sat_fade_set((SAT_FADE_LIT * lit) / OPENING_FADE_VM_FRAMES);
			}
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
 |   saveSlot and loadSlot. Static because SAVE_MAX_BYTES on the stack is an 8 KB
 |   frame the SH-2 cannot afford; sharing it is safe because the engine
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

	const int32_t framePos = (int32_t)(SAVE_HEADER_SIZE + f.tell()) + 4;
	int32_t frameLen = pageRleEncode(video._curPagePtr2, Video::VID_PAGE_SIZE,
	                                 s_saveBuf + framePos,
	                                 (int32_t)sizeof(s_saveBuf) - framePos);
	if (frameLen < 0) {
		frameLen = 0;
	}
	f.writeUint16BE((uint16_t)(frameLen > 0 ? 1 : 0));
	f.writeUint16BE((uint16_t)frameLen);

	char name[12];
	savedataSlotName(slot, name);
	const int32_t total = (int32_t)(SAVE_HEADER_SIZE + f.tell()) + frameLen;
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

	const uint16_t hasFrame = f.readUint16BE();
	const uint16_t frameLen = f.readUint16BE();
	if (!f.ioErr() && hasFrame != 0 && frameLen != 0) {
		const int32_t framePos = (int32_t)(SAVE_HEADER_SIZE + f.tell());
		if (framePos + frameLen <= (int32_t)sizeof(s_saveBuf) &&
		    pageRleDecode(s_saveBuf + framePos, frameLen, video._pages[0],
		                  Video::VID_PAGE_SIZE)) {
			for (int i = 1; i < 4; ++i) {
				memcpy(video._pages[i], video._pages[0], Video::VID_PAGE_SIZE);
			}
		}
	}

	_lastSaveError = SAT_BUP_OK;
	return true;
}

/*----------------------
 | Engine::startNewGame
 | Description: Begins a fresh run at the first playable part, past the
 |   introduction. The introduction is what runIntroAttract has just finished
 |   playing in front of the title card, so starting a game at GAME_PART2 would
 |   run it a second time with the player waiting to play.
 |
 |   GAME_PART3 is where the introduction hands off of its own accord --
 |   parts.h annotates GAME_PART4 as the jail, which the game reaches after it,
 |   so GAME_PART3 is the arrival. Without BYPASS_PROTECTION the run still
 |   begins at the copy-protection wheel, which is a screen rather than a
 |   cinematic and is nobody's idea of a repeat.
 | Author: suinevere
 | Dependencies: parts.h
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void Engine::startNewGame() {
#ifdef BYPASS_PROTECTION
	vm.initForPart(GAME_PART3);
#else
	vm.initForPart(GAME_PART1);
#endif
}

bool Engine::runIntroAttract() {
	sat_fade_set(SAT_FADE_DARK);

	sat_probe_reset();
	sat_probe_mark(SAT_PROBE_ATTRACT_ENTER);

	vm.initForPart(GAME_PART2);

	sat_probe_mark(SAT_PROBE_PART_READY);

	res.requestedNextPart = 0;

	bool finished = false;
	bool framed = false;
	int lit = 0;

	while (!sys->input.quit) {
		// Tested before checkThreadRequests rather than after, because that
		// call is the thing that would follow the request into GAME_PART3.
		if (res.requestedNextPart != 0) {
			finished = true;
			break;
		}

		vm.checkThreadRequests();
		vm.inp_updatePlayer();

		if (menuInputBits(sys) != 0) {
			break;
		}

		vm.hostFrame();

		if (!framed) {
			framed = true;
			sat_probe_mark(SAT_PROBE_FIRST_FRAME);
		}

		if (lit < OPENING_FADE_VM_FRAMES) {
			lit++;
			sat_fade_set((SAT_FADE_LIT * lit) / OPENING_FADE_VM_FRAMES);
		}
	}

	res.requestedNextPart = 0;
	player.stop();
	mixer.stopAll();

	// Against the display rather than the VM: nothing is advancing the
	// cinematic now, so the last frame just has to be held and darkened.
	sat_fade_ramp(SAT_FADE_DARK,
	              finished ? OPENING_FADE_FIELDS : OPENING_FADE_SKIP_FIELDS);

	return finished;
}
