/*----------------------
 | opening.cxx
 | Description: The title opening: hands OPENING.CPK to the Cinepak player and
 |   lets any button skip straight to the title screen.
 |
 |   NBG0 is blanked before the movie starts rather than after it ends. The
 |   player switches the layer off for the duration and back on at close, so
 |   what is written here is what shows for the single field between the movie
 |   going away and the menu drawing its first title frame.
 | Author: suinevere
 | Dependencies: opening.h, menu_draw.h, menu_input.h, sys.h, saturn_movie.h
 | Globals: s_hasPlayed
 ----------------------*/
#include "opening.h"
#include "menu_draw.h"
#include "menu_input.h"
#include "sys.h"
#include "saturn_movie.h"

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
 | Description: Diagnostic only, and not a fix: plays SEGA's own movie instead
 |   of ours. Set to 0 for normal behaviour.
 |
 |   REFTEST.CPK is SRL's SKYBL.CPK with the audio dropped and the video stream
 |   copied rather than re-encoded, so the Cinepak data in it is byte for byte
 |   what SEGA's tools produced. That splits a question this port has not been
 |   able to answer any other way. Playback dies inside cpk_VideoSampleCvid,
 |   SEGA's own decoder, and either the data it is being fed is at fault or the
 |   way this port sets the player up is.
 |
 |   If SEGA's own movie plays, the encoder is at fault and the difference is
 |   somewhere in what ffmpeg emits. If it dies in the same place, the file was
 |   never the problem and the fault is in the buffers, the memory or the
 |   VDP1 setup around it.
 | Author: suinevere
 ----------------------*/
#define OPENING_USE_REFERENCE 0

#if OPENING_USE_REFERENCE
#undef OPENING_MOVIE
#define OPENING_MOVIE "REFTEST.CPK"
#endif

/*----------------------
 | s_hasPlayed
 | Description: Whether openingPlay has run. It plays once per boot; the title
 |   screen's attract loop goes through openingReplay instead, which ignores this.
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
 | openingRun
 | Description: The player itself, with no once-per-boot guard, so the boot path
 |   and the attract loop share one body. Returns immediately and without having
 |   changed anything if the movie cannot be opened, which leaves the caller on
 |   the title screen a beat early rather than on a dead display.
 | Author: suinevere
 | Params: sys -- for presentation and input; page -- MENU_PAGE_SIZE bytes,
 |         blanked so the layer behind the movie is black
 | Returns: N/A
 ----------------------*/
static void openingRun(System *sys, uint8_t *page)
{
	memset(page, 0, MENU_PAGE_SIZE);
	sys->updateDisplay(page);

	if (!sat_movie_open(OPENING_MOVIE)) {
		return;
	}

	while (sat_movie_step()) {
		sys->processEvents();

		if (openingAnyButton(sys)) {
			break;
		}
	}

	sat_movie_close();
}

void openingPlay(System *sys, uint8_t *page)
{
	if (s_hasPlayed) {
		return;
	}
	s_hasPlayed = true;
	openingRun(sys, page);
}

void openingReplay(System *sys, uint8_t *page)
{
	s_hasPlayed = true;
	openingRun(sys, page);
}
