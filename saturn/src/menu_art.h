/*----------------------
 | menu_art.h
 | Description: The menu's artwork, packed by tools/mkmenuart.py, with the title
 |   backdrop in absolute palette indices and the two strings in relative shades
 |   so they can render in either ramp.
 | Author: suinevere
 | Dependencies: menu_blit.h
 ----------------------*/
#ifndef MENU_ART_H
#define MENU_ART_H

#include "menu_blit.h"

/*----------------------
 | MENU_ART_TITLE_BACKDROP
 | Description: The Mega Drive title card as a full 320x200 4bpp page on slots
 |   0-6, 11 and 15 -- the ones the wordmark already owns plus the four
 |   MENU_ART_TITLE_PALETTE frees, leaving 7-10 and 12-14 clear for the menu to
 |   draw straight over it.
 | Author: suinevere
 ----------------------*/
extern const uint8_t MENU_ART_TITLE_BACKDROP[32000];

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

#endif /* MENU_ART_H */
