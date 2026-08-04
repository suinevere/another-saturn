/*----------------------
 | menu_blit.h
 | Description: Blits packed artwork into a raw 4bpp page, in two formats --
 |   absolute-index 4bpp for art that never changes colour and relative-shade
 |   2bpp for art that must render in more than one ramp -- with index 0
 |   transparent in both.
 | Author: suinevere
 | Dependencies: stdint.h
 ----------------------*/
#ifndef MENU_BLIT_H
#define MENU_BLIT_H

#include <stdint.h>

/*----------------------
 | MenuArt
 | Description: One packed bitmap. Row pitch is derived from w and the format,
 |   not stored: (w + 1) >> 1 for 4bpp, (w + 3) >> 2 for 2bpp. Rows are padded
 |   to a whole byte.
 | Author: suinevere
 ----------------------*/
struct MenuArt {
	const uint8_t *bits;
	int16_t w;
	int16_t h;
};

/*----------------------
 | menuBlit4bpp
 | Description: Draws a 4bpp bitmap whose values are absolute palette indices,
 |   clipped to the page. Index 0 is left as whatever was already there.
 | Author: suinevere
 | Params: page -- MENU_PAGE_SIZE bytes; art -- the bitmap; x, y -- top-left
 |         corner in pixels, may be negative or off the page
 | Returns: N/A
 ----------------------*/
void menuBlit4bpp(uint8_t *page, const MenuArt *art, int x, int y);

/*----------------------
 | menuBlit2bpp
 | Description: Draws a 2bpp bitmap whose values are shades 1..3, writing
 |   base + shade - 1, clipped to the page. Shade 0 is left untouched. This is
 |   how one piece of art renders both selected and unselected without a second
 |   copy: only the base changes.
 | Author: suinevere
 | Params: page -- MENU_PAGE_SIZE bytes; art -- the bitmap; x, y -- top-left
 |         corner in pixels; base -- palette index the shades are measured from
 | Returns: N/A
 ----------------------*/
void menuBlit2bpp(uint8_t *page, const MenuArt *art, int x, int y, uint8_t base);

#endif /* MENU_BLIT_H */
