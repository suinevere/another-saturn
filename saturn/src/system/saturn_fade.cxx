/*----------------------
 | saturn_fade.cxx
 | Description: The VDP2 and SCSP writes behind saturn_fade.h.
 | Author: suinevere
 | Dependencies: saturn_fade.h, saturn_platform.h, saturn_scsp.h, SRL (SGL's
 |   colour offset calls)
 | Globals: g_level
 ----------------------*/
#include <srl.hpp>
#include "saturn_fade.h"
#include "saturn_platform.h"
#include "saturn_scsp.h"

/*----------------------
 | FADE_OFFSET_FLOOR
 | Description: The colour offset that takes a full-brightness pixel to black.
 |   VDP2 offsets are signed nine-bit and clamp at 255, and the layers being
 |   faded are eight-bit per channel once VDP2 has widened them, so subtracting
 |   255 is enough to floor any pixel.
 | Author: suinevere
 ----------------------*/
#define FADE_OFFSET_FLOOR 255

/*----------------------
 | FADE_MVOL_MAX
 | Description: MVOL's full-scale value. The SCSP's master volume has sixteen
 |   steps against the video side's 257, so an audio fade is coarser than the
 |   picture it accompanies -- which is the right way round, since a stepped
 |   picture reads as a fault and a stepped fade-out does not.
 | Author: suinevere
 ----------------------*/
#define FADE_MVOL_MAX 15

/*----------------------
 | g_level
 | Description: The level last set, so a ramp knows where it is starting from
 |   and callers can read it back.
 | Author: suinevere
 ----------------------*/
static int g_level = SAT_FADE_LIT;

extern "C" void sat_fade_init(void)
{
    // Both layers on offset A: NBG0 is everything the engine and the menu
    // draw, SPR is the movie. Nothing uses offset B.
    slColOffsetAUse(NBG0ON | SPRON);
    sat_fade_set(SAT_FADE_LIT);
}

extern "C" void sat_fade_set(int level)
{
    if (level < SAT_FADE_DARK)
    {
        level = SAT_FADE_DARK;
    }
    else if (level > SAT_FADE_LIT)
    {
        level = SAT_FADE_LIT;
    }

    g_level = level;

    const int16_t offset =
        (int16_t)(-((FADE_OFFSET_FLOOR * (SAT_FADE_LIT - level)) / SAT_FADE_LIT));

    slColOffsetA(offset, offset, offset);
    sat_scsp_set_master((uint8_t)((level * FADE_MVOL_MAX) / SAT_FADE_LIT));
}

extern "C" int sat_fade_level(void)
{
    return g_level;
}

extern "C" void sat_fade_ramp(int target, int frames)
{
    if (frames <= 0)
    {
        sat_fade_set(target);
        sat_video_sync();
        return;
    }

    const int start = g_level;

    for (int i = 1; i <= frames; i++)
    {
        sat_fade_set(start + ((target - start) * i) / frames);
        sat_video_sync();
    }
}
