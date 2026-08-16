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

#include "bank.h"
#include "file.h"
#include "resource.h"

#ifdef __sh__
#include "system/saturn_audio.h"
#endif



Bank::Bank(const char *dataDir)
	: _dataDir(dataDir) {
}

bool Bank::read(const MemEntry *me, uint8_t *buf) {

	bool ret = false;
	char bankName[10];
	sprintf(bankName, "bank%02x", me->bankId);
	File f;

	if (!f.open(bankName, _dataDir))
		error("Bank::read() unable to open '%s'", bankName);


	f.seek(me->bankOffset);

	// Depending if the resource is packed or not we
	// can read directly or unpack it.
	if (me->packedSize == me->size) {
		f.read(buf, me->packedSize);
		ret = true;
	} else {
		f.read(buf, me->packedSize);
		_startBuf = buf;
		_iBuf = buf + me->packedSize - 4;
		ret = unpack();
	}


	return ret;
}

void Bank::decUnk1(uint8_t numChunks, uint8_t addCount) {
	uint16_t count = getCode(numChunks) + addCount + 1;
	debug(DBG_BANK, "Bank::decUnk1(%d, %d) count=%d", numChunks, addCount, count);
	_unpCtx.datasize -= count;
	while (count--) {
		assert(_oBuf >= _iBuf && _oBuf >= _startBuf);
		*_oBuf = (uint8_t)getCode(8);
		--_oBuf;
	}
}

/*
   Note from fab: This look like run-length encoding.
*/
void Bank::decUnk2(uint8_t numChunks) {
	uint16_t i = getCode(numChunks);
	uint16_t count = _unpCtx.size + 1;
	debug(DBG_BANK, "Bank::decUnk2(%d) i=%d count=%d", numChunks, i, count);
	_unpCtx.datasize -= count;
	while (count--) {
		assert(_oBuf >= _iBuf && _oBuf >= _startBuf);
		*_oBuf = *(_oBuf + i);
		--_oBuf;
	}
}

/*
	Most resource in the banks are compacted.
*/
bool Bank::unpack() {
	_unpCtx.size = 0;
	_unpCtx.datasize = READ_BE_UINT32(_iBuf); _iBuf -= 4;
	_oBuf = _startBuf + _unpCtx.datasize - 1;
	_unpCtx.crc = READ_BE_UINT32(_iBuf); _iBuf -= 4;
	_unpCtx.chk = READ_BE_UINT32(_iBuf); _iBuf -= 4;
	_unpCtx.crc ^= _unpCtx.chk;

	// A decompression is the longest the engine ever goes without reaching
	// sat_audio_update, so without this pump the music sequencer stalls and
	// falls behind during loading -- audibly, since a part change is exactly
	// when a big resource gets decoded.
	//
	// This was removed once, on the theory that the SCSP reads sample data out
	// of the very block being rewritten here. That theory does not hold: the
	// SCSP never reads work RAM at all -- it plays out of its own sound RAM,
	// and a sample only lands there via sat_scsp_play's upload, which happens
	// once, up front, when the sequencer starts the note. Nothing here can
	// race that upload. The real constraint is narrower: don't let the
	// sequencer start a NEW note out of a resource unpack() has only
	// half-written, which is safe because:
	//
	//   - Loads append. resource.cxx:195,216 place each resource at
	//     _scriptCurPtr and then advance it, so a load never lands on a sound
	//     that is already playing.
	//   - The one place the block rewinds is Resource::invalidateAll, reached
	//     through setupPart -- and initForPart calls mixer->stopAll() first
	//     (vm.cxx:384, before 389). Nothing is playing when it happens.
	//   - unpack itself writes only within its own resource, backwards from
	//     _startBuf + datasize - 1.
	//
	// What calling sat_audio_update here actually buys is keeping SfxPlayer's
	// timers advancing: a note already sounding on the SCSP needs no feeding
	// and plays on regardless -- there is no ring to run dry -- but the
	// sequencer itself stops queuing new notes the moment nothing pumps its
	// timer, so without this call the music falls silent for the length of the
	// decode.
	//
	// Running the sequencer here is safe. It reads a resource only through
	// me->bufPtr and only when me->state is MEMENTRY_STATE_LOADED
	// (sfxplayer.cxx:53,86), and resource.cxx calls readBank at :209 before
	// setting either at :214-215. The entry being decompressed is therefore
	// invisible to the sequencer for the whole of this function.
	//
	// Throttled because the loop body decodes only a few bytes per pass, and the
	// driver call is far more expensive than the decode it would be interleaved
	// with. 256 passes is still hundreds of pumps across a large resource.
#ifdef __sh__
	uint16_t pumpCountdown = 256;
#endif
	do {
#ifdef __sh__
		if (--pumpCountdown == 0) {
			pumpCountdown = 256;
			sat_audio_update();
		}
#endif
		if (!nextChunk()) {
			_unpCtx.size = 1;
			if (!nextChunk()) {
				decUnk1(3, 0);
			} else {
				decUnk2(8);
			}
		} else {
			uint16_t c = getCode(2);
			if (c == 3) {
				decUnk1(8, 8);
			} else {
				if (c < 2) {
					_unpCtx.size = c + 2;
					decUnk2(c + 9);
				} else {
					_unpCtx.size = getCode(8);
					decUnk2(12);
				}
			}
		}
	} while (_unpCtx.datasize > 0);
	return (_unpCtx.crc == 0);
}

uint16_t Bank::getCode(uint8_t numChunks) {
	uint16_t c = 0;
	while (numChunks--) {
		c <<= 1;
		if (nextChunk()) {
			c |= 1;
		}			
	}
	return c;
}

bool Bank::nextChunk() {
	bool CF = rcr(false);
	if (_unpCtx.chk == 0) {
		assert(_iBuf >= _startBuf);
		_unpCtx.chk = READ_BE_UINT32(_iBuf); _iBuf -= 4;
		_unpCtx.crc ^= _unpCtx.chk;
		CF = rcr(true);
	}
	return CF;
}

bool Bank::rcr(bool CF) {
	bool rCF = (_unpCtx.chk & 1);
	_unpCtx.chk >>= 1;
	if (CF) _unpCtx.chk |= 0x80000000;
	return rCF;
}
