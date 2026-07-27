/*----------------------
 | saturn_audio.h
 | Description: The C face of the Saturn audio backend: PCM output for the
 |   engine's software mixer, and the periodic timer that drives the music
 |   sequencer. saturn_system.cxx implements the System audio methods against
 |   this, so that file never includes <srl.hpp> -- see the note in
 |   saturn_platform.h on why SGL's extern "C" headers and SRL's C++ headers are
 |   kept out of the same translation unit.
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
 |   The audio callback is Mixer::mixCallback: it fills `len` bytes of 8-bit
 |   signed mono. The timer callback is SfxPlayer::eventsCallback: it returns the
 |   delay until it next wants to run, or 0 to retire itself.
 | Author: suinevere
 ----------------------*/
typedef void (*SatAudioCallback)(void *param, uint8_t *stream, int len);
typedef uint32_t (*SatTimerCallback)(uint32_t delay, void *param);

/*----------------------
 | sat_audio_sample_rate
 | Description: The rate the PCM channels actually play at. The engine divides by
 |   this to derive playback pitch (mixer.cxx:62), so this value and the hardware
 |   must agree or every sound is detuned. Must never return 0.
 | Author: suinevere
 ----------------------*/
uint32_t sat_audio_sample_rate(void);

/*----------------------
 | sat_audio_start
 | Description: Allocates the mix buffers and begins playback. The callback is
 |   pulled from once per buffer, from sat_audio_update. Failing to allocate is
 |   not fatal: audio simply stays silent and the game runs.
 | Author: suinevere
 ----------------------*/
void sat_audio_start(SatAudioCallback callback, void *param);

/*----------------------
 | sat_audio_stop
 | Description: Silences both PCM channels and releases the mix buffers.
 | Author: suinevere
 ----------------------*/
void sat_audio_stop(void);

/*----------------------
 | sat_audio_update
 | Description: The pump. Refills and re-arms the PCM buffers when a channel goes
 |   idle, and fires any timers that have come due. Must be called regularly --
 |   once per frame is the design point. Audio continuity is exactly as good as
 |   the rate at which this is called, which is why it runs both from
 |   sat_video_present and from inside sat_sleep_ms's wait loop.
 | Author: suinevere
 ----------------------*/
void sat_audio_update(void);

/*----------------------
 | sat_audio_vblank
 | Description: Tells the PCM driver a vblank happened. MUST be called exactly
 |   once per vblank and from nowhere else -- it is a clock, not a pump.
 |
 |   Kept separate from sat_audio_update for that reason: update is deliberately
 |   called from wherever the engine happens to be (render, sleep, CD reads, the
 |   unpack loop), which during a load is thousands of times a second. Feeding
 |   that rate to the driver as vblanks runs its timing far ahead of real time.
 | Author: suinevere
 ----------------------*/
void sat_audio_vblank(void);

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
