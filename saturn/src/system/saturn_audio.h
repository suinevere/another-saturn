/*----------------------
 | saturn_audio.h
 | Description: The C face of what is left of the Saturn audio backend now
 |   that output is programmed straight onto the SCSP (saturn_scsp.h): the
 |   sequencer timers SfxPlayer runs on, the pump that services them, and a
 |   sample-rate reporter kept only because sys.h declares
 |   getOutputSampleRate(). saturn_system.cxx implements the System audio
 |   methods against this, so that file never includes <srl.hpp> -- see the
 |   note in saturn_platform.h on why SGL's extern "C" headers and SRL's C++
 |   headers are kept out of the same translation unit.
 |
 |   Design: docs/superpowers/specs/2026-07-27-another-world-audio-backend-design.md
 | Author: suinevere
 | Dependencies: stdint.h
 ----------------------*/
#ifndef SATURN_AUDIO_H
#define SATURN_AUDIO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | SatAudioCallback / SatTimerCallback
 | Description: Deliberately identical to System::AudioCallback and
 |   System::TimerCallback in sys.h. They are redeclared rather than included
 |   because sys.h is C++ and this header is consumed from the SRL side.
 |
 |   SatAudioCallback is kept only because the System interface still passes
 |   one through to sat_audio_start -- sat_audio_start ignores it. There is no
 |   software mixer left to pull samples from it; the SCSP plays straight out
 |   of sound RAM. The timer callback is still live: it is SfxPlayer's
 |   eventsCallback, which returns the delay until it next wants to run, or 0
 |   to retire itself.
 | Author: suinevere
 ----------------------*/
typedef void (*SatAudioCallback)(void *param, uint8_t *stream, int len);
typedef uint32_t (*SatTimerCallback)(uint32_t delay, void *param);

/*----------------------
 | sat_audio_sample_rate
 | Description: Reports the SCSP's rate. Nothing derives playback pitch from
 |   this any more -- sat_scsp_play is given the note's frequency directly and
 |   scsp_voice_pitch turns it into an OCT/FNS word (saturn_audio.cxx states
 |   this plainly). It stays only because sys.h declares
 |   getOutputSampleRate() as part of the System interface. Must never
 |   return 0.
 | Author: suinevere
 ----------------------*/
uint32_t sat_audio_sample_rate(void);

/*----------------------
 | sat_audio_start
 | Description: Brings the SCSP backend up (sat_scsp_init). The callback
 |   parameter is accepted only to match the System interface -- it is
 |   ignored, since there is no software mixer left to pull samples from it.
 | Author: suinevere
 ----------------------*/
void sat_audio_start(SatAudioCallback callback, void *param);

/*----------------------
 | sat_audio_stop
 | Description: Silences the SCSP's slots (sat_scsp_shutdown).
 | Author: suinevere
 ----------------------*/
void sat_audio_stop(void);

/*----------------------
 | sat_audio_update
 | Description: The pump. Fires any sequencer timers that have come due.
 |   Must be called regularly -- once per frame is the design point -- because
 |   that is what keeps SfxPlayer's music advancing; it is not needed to keep
 |   audio itself flowing, since the SCSP plays on its own once a slot is
 |   keyed on. It runs both from sat_video_present and from inside
 |   sat_sleep_ms's wait loop so a long CD decode does not stall the music.
 | Author: suinevere
 ----------------------*/
void sat_audio_update(void);

/*----------------------
 | sat_timer_add / sat_timer_remove
 | Description: A periodic callback, used only by SfxPlayer to advance music
 |   patterns. Returns a non-zero id, or 0 if no slot was free.
 | Author: suinevere
 | Params: delay -- milliseconds until the first call
 ----------------------*/
int  sat_timer_add(uint32_t delay, SatTimerCallback callback, void *param);
void sat_timer_remove(int timerId);

#ifdef __cplusplus
}
#endif
#endif /* SATURN_AUDIO_H */
