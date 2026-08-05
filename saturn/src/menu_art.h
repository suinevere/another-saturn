/*----------------------
 | menu_art.h
 | Description: The menu's artwork, packed by tools/mkmenuart.py, with the title
 |   backdrop and bolts in absolute palette indices and the two strings in
 |   relative shades so they can render in either ramp.
 | Author: suinevere
 | Dependencies: menu_blit.h
 ----------------------*/
#ifndef MENU_ART_H
#define MENU_ART_H

#include "menu_blit.h"

/*----------------------
 | MENU_ART_BOLT_COUNT / MENU_ART_STROBE_LEVELS
 | Description: How many lightning bolts the title screen chooses between, and
 |   how many brightness steps the selected-row strobe walks.
 | Author: suinevere
 ----------------------*/
enum {
	MENU_ART_BOLT_COUNT   = 3,
	MENU_ART_STROBE_LEVELS = 16
};

/*----------------------
 | MENU_ART_TITLE_BACKDROP
 | Description: The last frame the opening plays, before its loop fade to black,
 |   as a full 320x200 4bpp page on slots 0-6, 11 and 15 -- the ones the
 |   wordmark and the bolts already own plus the four MENU_ART_TITLE_PALETTE
 |   frees, leaving 7-10 and 12-14 clear for the menu to draw straight over it.
 | Author: suinevere
 ----------------------*/
extern const uint8_t MENU_ART_TITLE_BACKDROP[32000];

/*----------------------
 | MENU_ART_BOLT
 | Description: The three lightning bolt variants the title screen chooses
 |   between: bolt 1 is bolt 0 mirrored horizontally, bolt 2 is that mirror
 |   cropped to its top 40 rows.
 | Author: suinevere
 ----------------------*/
extern const MenuArt MENU_ART_BOLT[MENU_ART_BOLT_COUNT];

/*----------------------
 | MENU_ART_START_GAME
 | Description: The "start game" chrome string, drawn in whichever ramp the
 |   caller's base index selects.
 | Author: suinevere
 ----------------------*/
extern const MenuArt MENU_ART_START_GAME;

/*----------------------
 | MENU_ART_LOAD_GAME
 | Description: The "load game" chrome string, drawn in whichever ramp the
 |   caller's base index selects.
 | Author: suinevere
 ----------------------*/
extern const MenuArt MENU_ART_LOAD_GAME;

/*----------------------
 | MENU_ART_PALETTE
 | Description: All sixteen entries, two bytes each: R = byte0 & 0x0F,
 |   G = (byte1 & 0xF0) >> 4, B = byte1 & 0x0F.
 | Author: suinevere
 ----------------------*/
extern const uint8_t MENU_ART_PALETTE[32];

/*----------------------
 | MENU_ART_TITLE_PALETTE
 | Description: MENU_ART_PALETTE with entries 1, 2, 3 and 11 replaced by the
 |   backdrop's own ramp. Those four are free while the title screen is up, but
 |   1-3 are the pause screen's freeze ramp, so the title takes a copy rather
 |   than moving them for every screen.
 | Author: suinevere
 ----------------------*/
extern const uint8_t MENU_ART_TITLE_PALETTE[32];

/*----------------------
 | MENU_ART_STROBE
 | Description: Entries 12, 13 and 14 at sixteen brightness levels, six bytes
 |   per level. Level 0 is 55% of MENU_ART_PALETTE, level 15 is full.
 | Author: suinevere
 ----------------------*/
extern const uint8_t MENU_ART_STROBE[MENU_ART_STROBE_LEVELS][6];

#endif /* MENU_ART_H */
