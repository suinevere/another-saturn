/*----------------------
 | menu_blit.cxx
 | Description: The two artwork blitters. No engine headers: see menu_blit.h.
 | Author: suinevere
 | Dependencies: menu_blit.h, menu_draw.h
 ----------------------*/
#include "menu_blit.h"
#include "menu_draw.h"

/*----------------------
 | menuBlitPixel
 | Description: Writes one 4-bit pixel into the page, choosing the nibble from
 |   the x parity the same way menuDrawFill does.
 | Author: suinevere
 | Params: page -- MENU_PAGE_SIZE bytes; x, y -- position, assumed on the page;
 |         value -- 4-bit palette index
 | Returns: N/A
 ----------------------*/
static void menuBlitPixel(uint8_t *page, int x, int y, uint8_t value)
{
	uint8_t *b = page + y * MENU_PAGE_PITCH + x / 2;
	if (x & 1) {
		*b = (*b & 0xF0) | value;
	} else {
		*b = (*b & 0x0F) | (uint8_t)(value << 4);
	}
}

void menuBlit4bpp(uint8_t *page, const MenuArt *art, int x, int y)
{
	const int pitch = (art->w + 1) >> 1;

	for (int j = 0; j < art->h; ++j) {
		const int py = y + j;
		if (py < 0 || py >= MENU_PAGE_H) {
			continue;
		}
		const uint8_t *row = art->bits + j * pitch;
		for (int i = 0; i < art->w; ++i) {
			const int px = x + i;
			if (px < 0 || px >= MENU_PAGE_W) {
				continue;
			}
			const uint8_t b = row[i >> 1];
			const uint8_t v = (i & 1) ? (uint8_t)(b & 0x0F) : (uint8_t)(b >> 4);
			if (v != 0) {
				menuBlitPixel(page, px, py, v);
			}
		}
	}
}

void menuBlit2bpp(uint8_t *page, const MenuArt *art, int x, int y, uint8_t base)
{
	const int pitch = (art->w + 3) >> 2;

	for (int j = 0; j < art->h; ++j) {
		const int py = y + j;
		if (py < 0 || py >= MENU_PAGE_H) {
			continue;
		}
		const uint8_t *row = art->bits + j * pitch;
		for (int i = 0; i < art->w; ++i) {
			const int px = x + i;
			if (px < 0 || px >= MENU_PAGE_W) {
				continue;
			}
			const int shift = 6 - ((i & 3) << 1);
			const uint8_t v = (uint8_t)((row[i >> 2] >> shift) & 0x03);
			if (v != 0) {
				menuBlitPixel(page, px, py, (uint8_t)(base + v - 1));
			}
		}
	}
}
