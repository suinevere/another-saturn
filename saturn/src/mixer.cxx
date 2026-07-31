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

#include "mixer.h"
#include "serializer.h"
#include "sys.h"
#include "saturn_scsp.h"

Mixer::Mixer(System *stub)
	: sys(stub) {
}

void Mixer::init() {
	memset(_channels, 0, sizeof(_channels));
	_mutex = sys->createMutex();
	sys->startAudio(0, this);
}

void Mixer::free() {
	stopAll();
	sys->stopAudio();
	sys->destroyMutex(_mutex);
}

// The mutexes are gone from these methods, and that is a consequence rather
// than an oversight. They were needed when Mixer::mix ran from the vblank
// interrupt to keep the ring fed; with the SCSP mixing there is no interrupt
// and no second context, so the System mutex no-ops are correct again. The
// field ordering in playChannel -- active last -- is left as it was: it costs
// nothing and the reason it was needed is worth keeping on the record.
/*----------------------
 | playChannel
 | Description: Hands a sample to an SCSP slot.
 |
 |   The MixerChannel fields are still written even though nothing reads them
 |   for playback: Mixer::saveOrLoad serialises them and the save format must
 |   not move. chunkPos and chunkInc are inert -- the hardware keeps its own
 |   position and derives pitch from freq directly.
 | Author: suinevere
 ----------------------*/
void Mixer::playChannel(uint8_t channel, const MixerChunk *mc, uint16_t freq, uint8_t volume) {
	debug(DBG_SND, "Mixer::playChannel(%d, %d, %d)", channel, freq, volume);
	assert(channel < AUDIO_NUM_CHANNELS);

	MixerChannel *ch = &_channels[channel];
	ch->volume = volume;
	ch->chunk = *mc;
	ch->chunkPos = 0;
	ch->chunkInc = 0;
	ch->active = true;

	sat_scsp_play(channel, mc->data, mc->len, mc->loopPos, mc->loopLen,
	              freq, volume);
}

void Mixer::stopChannel(uint8_t channel) {
	debug(DBG_SND, "Mixer::stopChannel(%d)", channel);
	assert(channel < AUDIO_NUM_CHANNELS);
	_channels[channel].active = false;
	sat_scsp_stop(channel);
}

void Mixer::setChannelVolume(uint8_t channel, uint8_t volume) {
	debug(DBG_SND, "Mixer::setChannelVolume(%d, %d)", channel, volume);
	assert(channel < AUDIO_NUM_CHANNELS);
	_channels[channel].volume = volume;
	sat_scsp_set_volume(channel, volume);
}

void Mixer::stopAll() {
	debug(DBG_SND, "Mixer::stopAll()");
	for (uint8_t i = 0; i < AUDIO_NUM_CHANNELS; ++i) {
		_channels[i].active = false;
	}
	sat_scsp_stop_all();
}

void Mixer::saveOrLoad(Serializer &ser) {
	sys->lockMutex(_mutex);
	for (int i = 0; i < AUDIO_NUM_CHANNELS; ++i) {
		MixerChannel *ch = &_channels[i];
		Serializer::Entry entries[] = {
			SE_INT(&ch->active, Serializer::SES_BOOL, VER(2)),
			SE_INT(&ch->volume, Serializer::SES_INT8, VER(2)),
			SE_INT(&ch->chunkPos, Serializer::SES_INT32, VER(2)),
			SE_INT(&ch->chunkInc, Serializer::SES_INT32, VER(2)),
			SE_PTR(&ch->chunk.data, VER(2)),
			SE_INT(&ch->chunk.len, Serializer::SES_INT16, VER(2)),
			SE_INT(&ch->chunk.loopPos, Serializer::SES_INT16, VER(2)),
			SE_INT(&ch->chunk.loopLen, Serializer::SES_INT16, VER(2)),
			SE_END()
		};
		ser.saveOrLoadEntries(entries);
	}

	// Load only. Whatever the channels were doing when the state was written,
	// the hardware is not doing it now. SfxPlayer serialises its own position
	// and re-issues note-ons within a tick or two; a half-finished sound effect
	// is better dropped than resumed from a position the SCSP cannot be told.
	// On save the four channels are still playing exactly what the player is
	// hearing -- stopping them here would silence a sustained note for real
	// until the sequencer's next note-on, for a quick-save that is supposed to
	// be transparent.
	if (ser._mode == Serializer::SM_LOAD) {
		sat_scsp_stop_all();
	}

	sys->unlockMutex(_mutex);
};
