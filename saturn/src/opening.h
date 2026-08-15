/*----------------------
 | opening.h
 | Description: Streams the title opening from the disc and presents it at 25 fps,
 |   fading in at the start and out at the end so the movie never cuts against
 |   what follows it. Also carries the timings the rest of the run-up to the
 |   title card fades at, since the movie, the engine's introduction and the
 |   title card have to agree on them to read as one sequence.
 | Author: suinevere
 | Dependencies: stdint.h
 ----------------------*/
#ifndef OPENING_H
#define OPENING_H

#include <stdint.h>

struct System;

/*----------------------
 | OPENING_FADE_FIELDS / OPENING_FADE_SKIP_FIELDS
 | Description: How long a fade takes in display fields at 60 Hz: half a second
 |   normally, a tenth when the player has asked to skip. The skip fade is short
 |   enough to read as getting out of the way rather than as an effect, but not
 |   a cut -- cutting from a lit movie to a lit title card is the jarring thing
 |   the fades exist to avoid, and it is no less jarring for being asked for.
 | Author: suinevere
 ----------------------*/
enum {
	OPENING_FADE_FIELDS      = 30,
	OPENING_FADE_SKIP_FIELDS = 6
};

/*----------------------
 | OPENING_FADE_VM_FRAMES
 | Description: The same fade-in, counted in VM frames, for the engine's
 |   introduction. A VM frame is one to five fields depending on what the
 |   cinematic asked for, so this cannot be a field count -- the intro advances
 |   on its own clock and the fade has to advance with it.
 | Author: suinevere
 ----------------------*/
enum {
	OPENING_FADE_VM_FRAMES = 10
};

/*----------------------
 | OPENING_NOT_PLAYED / OPENING_FINISHED / OPENING_SKIPPED
 | Description: How a play attempt ended. NOT_PLAYED covers both the movie
 |   having already run this boot and there being no movie on the disc, because
 |   the caller wants the same thing in either case: go straight to the title
 |   card without an attract in front of it.
 | Author: suinevere
 ----------------------*/
enum {
	OPENING_NOT_PLAYED = 0,
	OPENING_FINISHED,
	OPENING_SKIPPED
};

/*----------------------
 | openingPlay
 | Description: Plays OPENING.CPK once per boot and never again, whether the
 |   player sat through it or pressed a button to get past it. Returns
 |   immediately when the file is absent, so a disc without it still reaches the
 |   menu. Leaves the screen faded to black when it played, for whatever comes
 |   next to fade up from.
 |
 |   There is deliberately no replay: the attract that repeats is the engine's
 |   own introduction, and a two-minute disc-streamed movie is not something to
 |   put in front of somebody every fifteen seconds.
 | Author: suinevere
 | Params: sys -- for presentation and input; page -- MENU_PAGE_SIZE bytes,
 |         blanked so the layer behind the movie is black
 | Returns: OPENING_FINISHED, OPENING_SKIPPED or OPENING_NOT_PLAYED
 ----------------------*/
int openingPlay(System *sys, uint8_t *page);

#endif /* OPENING_H */
