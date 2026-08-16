/*----------------------
 | saturn_probe.cxx
 | Description: [DEBUG-a4f2] The mark table and the screen tint behind
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
 | Globals: g_marks, g_count
 ----------------------*/
#include <srl.hpp>
#include "saturn_probe.h"
#include "saturn_platform.h"

/*----------------------
 | ProbeMark
 | Description: [DEBUG-a4f2] One stamped milestone.
 | Author: suinevere
 ----------------------*/
struct ProbeMark
{
    int      Tag;
    uint32_t Ms;
};

/*----------------------
 | g_marks / g_count
 | Description: [DEBUG-a4f2] The table and how much of it is filled.
 | Author: suinevere
 ----------------------*/
static ProbeMark g_marks[SAT_PROBE_MAX_MARKS];
static int       g_count = 0;

/*----------------------
 | PROBE_TINTS
 | Description: [DEBUG-a4f2] The colour offset each tag lifts the black seam
 |   to, indexed by SatProbeTag. Red through magenta in milestone order, so the
 |   seam reads as a progression rather than a set of unrelated flashes.
 | Author: suinevere
 ----------------------*/
static const int16_t PROBE_TINTS[SAT_PROBE_TAG_COUNT][3] = {
    { 255,   0,   0 },
    { 255, 128,   0 },
    { 255, 255,   0 },
    {   0, 255,   0 },
    {   0, 255, 255 },
    {   0,   0, 255 },
    { 255,   0, 255 }
};

/*----------------------
 | PROBE_NAMES
 | Description: [DEBUG-a4f2] Printable names, indexed by SatProbeTag. Kept to
 |   six characters so a name and a five digit millisecond count fit one line
 |   of the overlay.
 | Author: suinevere
 ----------------------*/
static const char *const PROBE_NAMES[SAT_PROBE_TAG_COUNT] = {
    "ENTER",
    "INVAL",
    "OPEN",
    "READ",
    "UNPACK",
    "PART",
    "FRAME"
};

extern "C" void sat_probe_reset(void)
{
    g_count = 0;
}

extern "C" void sat_probe_mark(int tag)
{
    if (tag < 0 || tag >= SAT_PROBE_TAG_COUNT)
    {
        return;
    }

    if (g_count < SAT_PROBE_MAX_MARKS)
    {
        g_marks[g_count].Tag = tag;
        g_marks[g_count].Ms = sat_time_ms();
        g_count++;
    }

    slColOffsetA(PROBE_TINTS[tag][0], PROBE_TINTS[tag][1], PROBE_TINTS[tag][2]);
}

extern "C" int sat_probe_count(void)
{
    return g_count;
}

extern "C" const char *sat_probe_name(int index)
{
    if (index < 0 || index >= g_count)
    {
        return "";
    }

    return PROBE_NAMES[g_marks[index].Tag];
}

extern "C" uint32_t sat_probe_ms(int index)
{
    if (index < 0 || index >= g_count)
    {
        return 0;
    }

    return g_marks[index].Ms - g_marks[0].Ms;
}
