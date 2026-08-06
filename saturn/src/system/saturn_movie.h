/*----------------------
 | saturn_movie.h
 | Description: The C face of Cinepak playback. saturn_movie.cxx implements it
 |   against SRL's CinepakPlayer, so callers never include <srl.hpp> -- the
 |   engine's headers wrap SGL's C headers in extern "C" (see intern.h) and
 |   mixing that with SRL's C++ headers in one translation unit is fragile.
 |   This is the same seam saturn_platform.h draws for video, input and time.
 |
 |   A movie owns the screen while it plays: NBG0 is switched off on open and
 |   back on at close, so the engine's bitmap layer cannot sit in front of the
 |   VDP1 sprite the frames are drawn as. Present nothing through
 |   sat_video_present between sat_movie_open and sat_movie_close.
 | Author: suinevere
 | Dependencies: stdint.h
 ----------------------*/
#ifndef SATURN_MOVIE_H
#define SATURN_MOVIE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | sat_movie_open
 | Description: Loads a Cinepak movie off the disc, reserves the VDP1 sprite it
 |   decodes into, hides NBG0 and starts playback. Call sat_movie_close when the
 |   caller is done with it, whether it finished or was skipped.
 | Author: suinevere
 | Params: file -- movie file name on the disc, 8.3 and upper case
 | Returns: 1 if playback started, 0 if the file, the buffers or the sprite
 |          could not be had -- in which case nothing was changed and
 |          sat_movie_close need not be called
 ----------------------*/
int sat_movie_open(const char *file);

/*----------------------
 | sat_movie_step
 | Description: Draws the current frame and waits one vblank, which is also what
 |   advances decoding: the player's task runs off SRL's before-sync event. Call
 |   in a loop until it returns 0.
 | Author: suinevere
 | Params: N/A
 | Returns: 1 while the movie is still playing, 0 once it has completed or if no
 |          movie is open
 ----------------------*/
int sat_movie_step(void);

/*----------------------
 | sat_movie_close
 | Description: Stops playback, releases the movie's buffers and shows NBG0
 |   again. Safe to call with no movie open. The VDP1 sprite is deliberately
 |   kept -- see the note in saturn_movie.cxx.
 | Author: suinevere
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void sat_movie_close(void);

#ifdef __cplusplus
}
#endif
#endif /* SATURN_MOVIE_H */
