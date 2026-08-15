/*----------------------
 | saturn_fade.h
 | Description: One brightness knob covering everything the player can see and
 |   hear, so the seams between the opening movie, the engine's intro and the
 |   title card can be crossed without a hard cut.
 |
 |   Deliberately not a palette dim. The movie is a VDP1 sprite and the engine
 |   is an NBG0 bitmap, and those two have nothing in common to scale -- but
 |   VDP2 applies its colour offset to both on the way out, after every other
 |   colour calculation, so one register pair fades whichever happens to be on
 |   screen. The audio half is the SCSP's master volume, which every sound on
 |   the machine passes through for the same reason.
 |
 |   That means a fade needs no cooperation from what is being faded: callers
 |   set a level and keep presenting whatever they were presenting.
 | Author: suinevere
 | Dependencies: stdint.h
 ----------------------*/
#ifndef SATURN_FADE_H
#define SATURN_FADE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | SAT_FADE_DARK / SAT_FADE_LIT
 | Description: The ends of the range sat_fade_set takes. Levels between them
 |   are linear in the VDP2 offset and in MVOL's sixteen steps.
 | Author: suinevere
 ----------------------*/
#define SAT_FADE_DARK 0
#define SAT_FADE_LIT  256

/*----------------------
 | sat_fade_init
 | Description: Registers colour offset A against NBG0 and the sprite layer and
 |   starts fully lit. Call once, after sat_video_init.
 |
 |   Nothing else in the port touches VDP2 colour offset. If something ever
 |   calls SRL's VDP2::<screen>::UseColorOffset, it rewrites both offset
 |   registrations from its own cached masks and would drop this one.
 | Author: suinevere
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void sat_fade_init(void);

/*----------------------
 | sat_fade_set
 | Description: Sets the current level. Takes effect on the next field for
 |   video and immediately for audio; callers that care about the two landing
 |   together should set the level and then sync.
 | Author: suinevere
 | Params: level -- SAT_FADE_DARK to SAT_FADE_LIT, clamped
 | Returns: N/A
 ----------------------*/
void sat_fade_set(int level);

/*----------------------
 | sat_fade_level
 | Description: The level last given to sat_fade_set.
 | Author: suinevere
 | Params: N/A
 | Returns: SAT_FADE_DARK to SAT_FADE_LIT
 ----------------------*/
int sat_fade_level(void);

/*----------------------
 | sat_fade_ramp
 | Description: Walks the level to a target one step per field, holding
 |   whatever is already on screen. For callers with nothing else to do while
 |   the fade runs; anything that must keep working during it -- a movie, which
 |   has to go on decoding -- should step sat_fade_set itself instead.
 | Author: suinevere
 | Params: target -- level to land on; frames -- fields to take getting there,
 |         0 or less snapping straight to target
 | Returns: N/A
 ----------------------*/
void sat_fade_ramp(int target, int frames);

#ifdef __cplusplus
}
#endif
#endif /* SATURN_FADE_H */
