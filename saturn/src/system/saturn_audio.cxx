/*----------------------
 | saturn_audio.cxx
 | Description: What is left once output moved to the SCSP: the sequencer
 |   timers and the pump that services them. One of the three files that
 |   include <srl.hpp> (with saturn_platform.cxx and saturn_cdfile.cxx).
 |
 |   SfxPlayer no longer produces samples for anything here to mix -- it drives
 |   sat_scsp_play directly -- but it still runs on a millisecond timer, and
 |   that timer still needs a heartbeat from somewhere the engine calls
 |   regularly. sat_audio_update is that heartbeat: it fires due timers and
 |   nothing else. Ad-hoc diagnosis of the SCSP backend itself goes through the
 |   sat_scsp_debug_* accessors (saturn_scsp.h) instead of anything living here.
 |
 |   Design, including why hardware mixing was rejected and what the known
 |   weaknesses are:
 |   docs/superpowers/specs/2026-07-27-another-world-audio-backend-design.md
 | Author: suinevere
 | Dependencies: saturn_audio.h, saturn_platform.h, saturn_scsp.h
 ----------------------*/
#include <srl.hpp>
#include "saturn_audio.h"
#include "saturn_platform.h"
#include "saturn_scsp.h"

static bool g_running = false;

/*----------------------
 | TimerSlot / g_timers
 | Description: Only SfxPlayer uses timers, and only one at a time, but a small
 |   fixed array costs nothing and avoids special-casing a single slot.
 | Author: suinevere
 ----------------------*/
#define MAX_TIMERS 4

struct TimerSlot
{
    bool             used;
    uint32_t         delay;
    uint32_t         due;
    SatTimerCallback callback;
    void            *param;
};

static TimerSlot g_timers[MAX_TIMERS];

/*----------------------
 | sat_audio_sample_rate
 | Description: The SCSP's own rate. Nothing derives pitch from this any more:
 |   sat_scsp_play is given the note's frequency directly and scsp_voice_pitch
 |   turns it into an OCT/FNS word. It stays because sys.h:70 declares
 |   getOutputSampleRate and the engine's interface is not ours to change, and
 |   it must stay non-zero because Mixer used to divide by it.
 | Author: suinevere
 ----------------------*/
extern "C" uint32_t sat_audio_sample_rate(void)
{
    return 44100;
}

extern "C" void sat_audio_start(SatAudioCallback callback, void *param)
{
    (void)callback;
    (void)param;

    if (g_running)
    {
        return;
    }

    sat_scsp_init();

    g_running = true;
}

extern "C" void sat_audio_stop(void)
{
    if (!g_running)
    {
        return;
    }

    sat_scsp_shutdown();

    g_running = false;
}

extern "C" void sat_audio_update(void)
{
    // Timers first: the music sequencer queues notes onto SCSP slots, so
    // running it before anything else means a note started this tick is
    // audible as soon as possible rather than a tick late.
    const uint32_t now = sat_time_ms();

    for (int32_t i = 0; i < MAX_TIMERS; i++)
    {
        TimerSlot *slot = &g_timers[i];

        if (slot->used && now >= slot->due)
        {
            const uint32_t next = slot->callback(slot->delay, slot->param);

            if (next == 0)
            {
                // Returning 0 retires the timer, matching the SDL semantics
                // SfxPlayer was written against.
                slot->used = false;
            }
            else
            {
                slot->delay = next;
                slot->due   = now + next;
            }
        }
    }
}

extern "C" int sat_timer_add(uint32_t delay, SatTimerCallback callback, void *param)
{
    if (callback == nullptr)
    {
        return 0;
    }

    for (int32_t i = 0; i < MAX_TIMERS; i++)
    {
        if (!g_timers[i].used)
        {
            g_timers[i].used     = true;
            g_timers[i].delay    = delay;
            g_timers[i].due      = sat_time_ms() + delay;
            g_timers[i].callback = callback;
            g_timers[i].param    = param;
            return i + 1;
        }
    }

    return 0;
}

extern "C" void sat_timer_remove(int timerId)
{
    const int32_t index = timerId - 1;

    if (index >= 0 && index < MAX_TIMERS)
    {
        g_timers[index].used = false;
    }
}
