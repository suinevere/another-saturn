/*----------------------
 | opening.cxx
 | Description: The title opening: hands OPENING.CPK to the Cinepak player and
 |   lets any button skip straight to the title screen.
 |
 |   NBG0 is blanked before the movie starts rather than after it ends. The
 |   player switches the layer off for the duration and back on at close, so
 |   what is written here is what shows for the single field between the movie
 |   going away and the menu drawing its first title frame.
 |
 |   The fades run against sat_movie_step rather than sat_video_sync, because
 |   the movie owns the screen while it is open: the frame is a VDP1 sprite that
 |   has to be re-issued every field, and a field the player did not draw is a
 |   field with nothing on it. sat_movie_step goes on drawing after it starts
 |   reporting the movie finished, which is what lets the last frame be faded
 |   out rather than snapped away.
 | Author: suinevere
 | Dependencies: opening.h, menu_draw.h, menu_input.h, sys.h, saturn_movie.h,
 |   saturn_fade.h
 | Globals: s_hasPlayed
 ----------------------*/
#include "opening.h"
#include "menu_draw.h"
#include "menu_input.h"
#include "sys.h"
#include "saturn_movie.h"
#include "saturn_fade.h"

extern "C" {
#include <string.h>
}

/*----------------------
 | OPENING_MOVIE
 | Description: The movie on the disc, built by tools/mkopeningcpk.py. Upper
 |   case and 8.3: GFS_NameToId matches the ISO9660 name, not the source one.
 | Author: suinevere
 ----------------------*/
#define OPENING_MOVIE "OPENING.CPK"

/*----------------------
 | OPENING_USE_REFERENCE
 | Description: Diagnostic only, and not a fix: plays SEGA's own movie, sound and
 |   all. Set to 0 for normal behaviour.
 |
 |   REFAUD.CPK is SRL's SKYBL.CPK byte for byte -- same mono 32 kHz audio ours
 |   declares, at a higher data rate than ours, made by SEGA's own tools. It
 |   answers whether the movie audio skipping is something about the file we
 |   build or something about the way this port shares the SCSP with the sound
 |   driver, and nothing short of hearing it will.
 |
 |   If SEGA's audio is clean, the fault is in our encode -- most likely that
 |   ffmpeg dribbles fifty tiny audio chunks a second where SEGA front-loads
 |   half a second and then feeds quarter-second blocks. If SEGA's audio skips
 |   the same way, the file was never the problem and the fault is in the
 |   buffers, the shared SCSP, or the bus.
 | Author: suinevere
 ----------------------*/
#define OPENING_USE_REFERENCE 0

#if OPENING_USE_REFERENCE
#undef OPENING_MOVIE
#define OPENING_MOVIE "REFAUD.CPK"
#endif

/*----------------------
 | s_hasPlayed
 | Description: Whether openingPlay has run. The movie is a boot event and shows
 |   at most once a session -- nothing overrides this, including the title
 |   screen's attract, which loops the engine's introduction instead.
 | Author: suinevere
 ----------------------*/
static bool s_hasPlayed = false;

/*----------------------
 | openingAnyButton
 | Description: Reports whether any button menuPadMask reads is currently held.
 | Author: suinevere
 | Params: sys -- the system to poll
 | Returns: true if any of the four D-pad directions, L, R, confirm, cancel, or
 |          pause is held
 ----------------------*/
static bool openingAnyButton(System *sys)
{
	return menuInputBits(sys) != 0;
}

/*----------------------
 | openingFadeOut
 | Description: Takes the level to black over a number of fields while the movie
 |   goes on being drawn, so the picture darkens instead of vanishing.
 | Author: suinevere
 | Params: fields -- how long to take
 | Returns: N/A
 ----------------------*/
static void openingFadeOut(int fields)
{
	const int start = sat_fade_level();

	for (int i = 1; i <= fields; i++) {
		sat_fade_set(start - (start * i) / fields);
		sat_movie_step();
	}
}

/*----------------------
 | openingRun
 | Description: The player itself, kept separate from the once-per-boot guard so
 |   the guard reads as policy rather than as part of playback. Returns
 |   immediately if the movie cannot be opened, which leaves the caller a beat
 |   early rather than on a dead display. Finishes black whichever way it ends.
 | Author: suinevere
 | Params: sys -- for presentation and input; page -- MENU_PAGE_SIZE bytes,
 |         blanked so the layer behind the movie is black
 | Returns: OPENING_FINISHED, OPENING_SKIPPED or OPENING_NOT_PLAYED
 ----------------------*/
static int openingRun(System *sys, uint8_t *page)
{
	sat_fade_set(SAT_FADE_DARK);

	memset(page, 0, MENU_PAGE_SIZE);
	sys->updateDisplay(page);

	if (!sat_movie_open(OPENING_MOVIE)) {
		sat_fade_set(SAT_FADE_LIT);
		return OPENING_NOT_PLAYED;
	}

	bool skipped = false;
	int lit = 0;

	while (sat_movie_step()) {
		if (lit < OPENING_FADE_FIELDS) {
			lit++;
			sat_fade_set((SAT_FADE_LIT * lit) / OPENING_FADE_FIELDS);
		}

		sys->processEvents();

		if (openingAnyButton(sys)) {
			skipped = true;
			break;
		}
	}

	openingFadeOut(skipped ? OPENING_FADE_SKIP_FIELDS : OPENING_FADE_FIELDS);

	sat_movie_close();

	return skipped ? OPENING_SKIPPED : OPENING_FINISHED;
}

int openingPlay(System *sys, uint8_t *page)
{
	if (s_hasPlayed) {
		return OPENING_NOT_PLAYED;
	}
	s_hasPlayed = true;
	return openingRun(sys, page);
}
