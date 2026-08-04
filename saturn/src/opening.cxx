/*----------------------
 | opening.cxx
 | Description: The streaming title-opening player: read-ahead, delta decode,
 |   per-frame palette and 25 fps pacing over a 60 Hz field rate.
 | Author: suinevere
 | Dependencies: opening.h, opening_codec.h, menu_draw.h, sys.h, saturn_cdfile.h,
 |   saturn_platform.h
 | Globals: s_ring, s_offsets, s_table
 ----------------------*/
#include "opening.h"
#include "opening_codec.h"
#include "menu_draw.h"
#include "sys.h"
#include "saturn_cdfile.h"
#include "saturn_platform.h"

extern "C" {
#include <string.h>
}

/*----------------------
 | OPENING_RING / OPENING_CHUNK / OPENING_MAX_FRAMES
 | Description: The read-ahead ring is 128 KB, about 25 frames at the 5223-byte
 |   average, topped up one 8 KB chunk per frame so it stays ahead of playback.
 | Author: suinevere
 ----------------------*/
enum {
	OPENING_RING       = 131072,
	OPENING_CHUNK      = 8192,
	OPENING_MAX_FRAMES = 512
};

/*----------------------
 | s_ring
 | Description: The read-ahead buffer, holding encoded bytes only.
 | Author: suinevere
 ----------------------*/
static uint8_t s_ring[OPENING_RING];

/*----------------------
 | s_offsets
 | Description: The file's per-frame offset table, read once at open.
 | Author: suinevere
 ----------------------*/
static uint32_t s_offsets[OPENING_MAX_FRAMES];

/*----------------------
 | s_table
 | Description: Scratch buffer for the raw little-endian offset table read
 |   straight off the disc, before it is unpacked into s_offsets.
 | Author: suinevere
 ----------------------*/
static uint8_t s_table[OPENING_MAX_FRAMES * 4];

/*----------------------
 | OPENING_HOLD
 | Description: Vblanks each frame is held for, cycling 2,2,2,3,3 so five frames
 |   span twelve fields and land on exactly 25 fps.
 | Author: suinevere
 ----------------------*/
static const int OPENING_HOLD[5] = { 2, 2, 2, 3, 3 };

/*----------------------
 | openingReadU32
 | Description: Reads a little-endian 32-bit value from a byte buffer.
 | Author: suinevere
 | Params: p -- at least four readable bytes
 | Returns: the value
 ----------------------*/
static uint32_t openingReadU32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
	       ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

void openingPlay(System *sys, uint8_t *page)
{
	SatCdFile *f = sat_cd_open("OPENING.BIN");
	if (f == 0) {
		return;
	}

	uint8_t head[16];
	if (sat_cd_read(f, 0, head, 16) != 16 ||
	    head[0] != 'A' || head[1] != 'W' || head[2] != 'O' || head[3] != 'P') {
		sat_cd_close(f);
		return;
	}

	const uint32_t count = openingReadU32(head + 8);
	if (count == 0 || count > OPENING_MAX_FRAMES) {
		sat_cd_close(f);
		return;
	}

	const int32_t tableBytes = (int32_t)(count * 4);
	if (sat_cd_read(f, 16, s_table, tableBytes) != tableBytes) {
		sat_cd_close(f);
		return;
	}
	for (uint32_t i = 0; i < count; ++i) {
		s_offsets[i] = openingReadU32(s_table + i * 4);
	}

	const int32_t fileSize = sat_cd_size(f);
	int32_t ringPos = (int32_t)s_offsets[0];
	int32_t ringLen = sat_cd_read(f, ringPos, s_ring, OPENING_RING);
	if (ringLen <= 0) {
		sat_cd_close(f);
		return;
	}

	memset(page, 0, MENU_PAGE_SIZE);

	uint32_t i = 0;
	int hold = 0;
	bool skipped = false;

	while (i < count) {
		const int32_t start = (int32_t)s_offsets[i];
		const int32_t end = (i + 1 < count) ? (int32_t)s_offsets[i + 1] : fileSize;
		const int32_t need = end - start;

		if (start < ringPos || start + need > ringPos + ringLen) {
			ringPos = start;
			ringLen = sat_cd_read(f, ringPos, s_ring, OPENING_RING);
			if (ringLen < need) {
				break;
			}
		}

		const uint8_t *pay = s_ring + (start - ringPos);
		sys->setPalette(pay);
		if (!openingApplyDelta(page, pay + 32, need - 32, MENU_PAGE_SIZE)) {
			break;
		}

		sys->updateDisplay(page);
		for (int k = 1; k < OPENING_HOLD[hold]; ++k) {
			sat_video_sync();
		}
		hold = (hold + 1) % 5;

		sys->processEvents();
		if (!skipped && (sys->input.menuConfirm || sys->input.menuCancel ||
		                 sys->input.pause)) {
			skipped = true;
			memset(page, 0, MENU_PAGE_SIZE);
			i = count - 1;
			continue;
		}

		if (ringPos + ringLen < fileSize &&
		    (ringPos + ringLen) - (start + need) < OPENING_CHUNK) {
			const int32_t at = ringPos + ringLen;
			int32_t room = OPENING_RING - ringLen;
			if (room > OPENING_CHUNK) {
				room = OPENING_CHUNK;
			}
			if (room > 0) {
				const int32_t got = sat_cd_read(f, at, s_ring + ringLen, room);
				if (got > 0) {
					ringLen += got;
				}
			}
		}

		++i;
	}

	sat_cd_close(f);
}
