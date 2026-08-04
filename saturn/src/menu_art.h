/*----------------------
 | menu_art.h
 | Description: The menu's artwork, packed by tools/mkmenuart.py. The logo and
 |   bolts carry absolute palette indices because they never change colour; the
 |   two strings carry relative shades because they render in both the selected
 |   and the unselected ramp.
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

extern const MenuArt MENU_ART_LOGO;
extern const MenuArt MENU_ART_BOLT[MENU_ART_BOLT_COUNT];
extern const MenuArt MENU_ART_START_GAME;
extern const MenuArt MENU_ART_LOAD_GAME;

/*----------------------
 | MENU_ART_PALETTE
 | Description: All sixteen entries, two bytes each: R = byte0 & 0x0F,
 |   G = (byte1 & 0xF0) >> 4, B = byte1 & 0x0F.
 | Author: suinevere
 ----------------------*/
extern const uint8_t MENU_ART_PALETTE[32];

/*----------------------
 | MENU_ART_STROBE
 | Description: Entries 12, 13 and 14 at sixteen brightness levels, six bytes
 |   per level. Level 0 is 55% of MENU_ART_PALETTE, level 15 is full.
 | Author: suinevere
 ----------------------*/
extern const uint8_t MENU_ART_STROBE[MENU_ART_STROBE_LEVELS][6];

#endif /* MENU_ART_H */
