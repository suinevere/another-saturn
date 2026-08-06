/*----------------------
 | opening.h
 | Description: Streams the title opening from the disc and presents it at 25 fps,
 |   leaving the final frame on screen for the menu to draw over.
 | Author: suinevere
 | Dependencies: stdint.h
 ----------------------*/
#ifndef OPENING_H
#define OPENING_H

#include <stdint.h>

struct System;

/*----------------------
 | openingPlay
 | Description: Plays OPENING.BIN into page, returning early on any button press
 |   and returning immediately when the file is absent so a disc without it still
 |   reaches the menu.
 | Author: suinevere
 | Params: sys -- for palette upload, presentation and input; page --
 |         MENU_PAGE_SIZE bytes the animation decodes into
 | Returns: N/A
 ----------------------*/
void openingPlay(System *sys, uint8_t *page);

/*----------------------
 | openingReplay
 | Description: Plays it again regardless of whether it has already run, for the
 |   title screen's idle attract loop. Same early exit on a button press.
 | Author: suinevere
 | Params: sys -- for palette upload, presentation and input; page --
 |         MENU_PAGE_SIZE bytes the animation decodes into
 | Returns: N/A
 ----------------------*/
void openingReplay(System *sys, uint8_t *page);

#endif /* OPENING_H */
