/*----------------------
 | saturn_probe.cxx
 | Description: [DEBUG-a4f2] The per-stage totals and the screen tint behind
 |   saturn_probe.h. Throwaway -- delete with the rest of DEBUG-a4f2.
 |
 |   The tint goes through slColOffsetA directly rather than through
 |   sat_fade_set, because sat_fade_set drives SCSP master volume off the same
 |   level and a probe has no business muting the music. saturn_fade.h's own
 |   registration of NBG0ON | SPRON is what makes the offset reach the screen,
 |   so this only ever writes the value.
 | Author: suinevere
 | Dependencies: saturn_probe.h, saturn_platform.h, SRL (SGL's colour offset
 |   calls)
 | Globals: g_hits, g_totals, g_last, g_first, g_elapsed, g_stopped
 ----------------------*/
#include <srl.hpp>
#include "saturn_probe.h"
#include "saturn_platform.h"

/*----------------------
 | g_hits / g_totals
 | Description: [DEBUG-a4f2] Visits and accumulated milliseconds per stage,
 |   indexed by SatProbeTag.
 | Author: suinevere
 ----------------------*/
static uint32_t g_hits[SAT_PROBE_TAG_COUNT];
static uint32_t g_totals[SAT_PROBE_TAG_COUNT];

/*----------------------
 | g_last / g_first / g_elapsed / g_stopped
 | Description: [DEBUG-a4f2] The previous mark's timestamp, the first one, the
 |   frozen span between them, and whether marking has finished.
 | Author: suinevere
 ----------------------*/
static uint32_t g_last    = 0;
static uint32_t g_first   = 0;
static uint32_t g_elapsed = 0;
static bool     g_stopped = false;

/*----------------------
 | PROBE_TINTS
 | Description: [DEBUG-a4f2] The colour offset each stage lifts the black seam
 |   to, indexed by SatProbeTag. Red through magenta in stage order, so the
 |   seam reads as a progression rather than a set of unrelated flashes.
 | Author: suinevere
 ----------------------*/
static const int16_t PROBE_TINTS[SAT_PROBE_TAG_COUNT][3] = {
    { 255,   0,   0 },
    { 255, 128,   0 },
    { 255, 255, 255 },
    { 200, 100, 255 },
    { 255, 255,   0 },
    {   0, 255,   0 },
    {   0, 255, 255 },
    {   0,   0, 255 },
    { 255,   0, 255 }
};

/*----------------------
 | PROBE_NAMES
 | Description: [DEBUG-a4f2] Printable names, indexed by SatProbeTag. Kept to
 |   six characters so a name, a visit count and a millisecond total fit one
 |   line of the overlay.
 | Author: suinevere
 ----------------------*/
static const char *const PROBE_NAMES[SAT_PROBE_TAG_COUNT] = {
    "ENTER",
    "INVAL",
    "FILL",
    "DISC",
    "OPEN",
    "READ",
    "UNPACK",
    "PART",
    "FRAME"
};

extern "C" void sat_probe_reset(void)
{
    for (int i = 0; i < SAT_PROBE_TAG_COUNT; i++)
    {
        g_hits[i] = 0;
        g_totals[i] = 0;
    }

    g_last = sat_time_ms();
    g_first = g_last;
    g_elapsed = 0;
    g_stopped = false;
}

extern "C" void sat_probe_mark(int tag)
{
    if (g_stopped || tag < 0 || tag >= SAT_PROBE_TAG_COUNT)
    {
        return;
    }

    const uint32_t now = sat_time_ms();

    g_hits[tag]++;
    g_totals[tag] += now - g_last;
    g_last = now;
    g_elapsed = now - g_first;

    slColOffsetA(PROBE_TINTS[tag][0], PROBE_TINTS[tag][1], PROBE_TINTS[tag][2]);
}

extern "C" void sat_probe_stop(void)
{
    g_stopped = true;
}

extern "C" const char *sat_probe_name(int tag)
{
    if (tag < 0 || tag >= SAT_PROBE_TAG_COUNT)
    {
        return "";
    }

    return PROBE_NAMES[tag];
}

extern "C" uint32_t sat_probe_hits(int tag)
{
    if (tag < 0 || tag >= SAT_PROBE_TAG_COUNT)
    {
        return 0;
    }

    return g_hits[tag];
}

extern "C" uint32_t sat_probe_total(int tag)
{
    if (tag < 0 || tag >= SAT_PROBE_TAG_COUNT)
    {
        return 0;
    }

    return g_totals[tag];
}

extern "C" uint32_t sat_probe_elapsed(void)
{
    return g_elapsed;
}
