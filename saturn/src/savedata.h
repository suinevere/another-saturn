/*----------------------
 | savedata.h
 | Description: Save slot metadata between the engine and saturn_backup.h's
 |   raw BUP wrapper -- slot naming, save header packing, slot probing, and
 |   chapter names. Must not include srl.hpp or sega_bup.h so it stays safe to
 |   include from any engine translation unit.
 | Author: suinevere
 | Dependencies: saturn_backup.h, intern.h
 ----------------------*/
#ifndef SAVEDATA_H
#define SAVEDATA_H

#include "saturn_backup.h"
#include "intern.h"

/*----------------------
 | SAVE_NUM_SLOTS / SAVE_HEADER_SIZE / SAVE_MAX_BYTES
 | Description: Slot count, packed header size in bytes, and the capacity a
 |   probe buffer must have to hold any valid save whole.
 | Author: suinevere
 ----------------------*/
enum {
	SAVE_NUM_SLOTS   = 3,
	SAVE_HEADER_SIZE = 48,
	SAVE_MAX_BYTES   = 8192
};

/*----------------------
 | SAVE_FRAME_NONE / SAVE_FRAME_RLE / SAVE_FRAME_DELTA
 | Description: Which codec the saved background frame uses, stored in the
 |   field that used to be a plain has-a-frame flag. Old saves wrote 1 and are
 |   still read with the plain run-length decoder; new ones write 2.
 | Author: suinevere
 ----------------------*/
enum {
	SAVE_FRAME_NONE     = 0,
	SAVE_FRAME_RLE      = 1,
	SAVE_FRAME_DELTA    = 2,
	SAVE_FRAME_DELTA_H2 = 3,
	SAVE_FRAME_DELTA_H4 = 4,
	SAVE_FRAME_DELTA_H8 = 5
};

/*----------------------
 | SAVE_FRAME_ROW_STEP
 | Description: The scanline step each delta frame kind was encoded with, so
 |   load can pass the encoder's own value back to the decoder.
 | Author: suinevere
 | Params: kind -- a SAVE_FRAME_* value
 | Returns: rows per kept scanline, or 0 when the kind carries no delta frame
 ----------------------*/
inline int savedataFrameRowStep(int kind)
{
	if (kind == SAVE_FRAME_DELTA) {
		return 1;
	}
	if (kind == SAVE_FRAME_DELTA_H2) {
		return 2;
	}
	if (kind == SAVE_FRAME_DELTA_H4) {
		return 4;
	}
	if (kind == SAVE_FRAME_DELTA_H8) {
		return 8;
	}
	return 0;
}

/*----------------------
 | SlotState
 | Description: What savedataProbe found in one backup RAM slot.
 | Author: suinevere
 ----------------------*/
enum SlotState {
	SLOT_EMPTY,
	SLOT_OK,
	SLOT_DAMAGED,
	SLOT_OLD_VERSION
};

/*----------------------
 | SlotInfo
 | Description: What the slot list menu shows for one slot.
 | Author: suinevere
 ----------------------*/
struct SlotInfo {
	SlotState state;
	uint16_t  partId;
	uint32_t  date;
};

/*----------------------
 | savedataSlotName
 | Description: Builds the BUP filename for a slot index.
 | Author: suinevere
 | Params: slot -- 0-based slot index; out -- destination, must hold 12 bytes
 | Returns: N/A
 ----------------------*/
void savedataSlotName(int slot, char *out);

/*----------------------
 | savedataWriteHeader
 | Description: Packs a SAVE_HEADER_SIZE-byte save header in place, big-endian,
 |   with the current save format version and a zero-filled description.
 | Author: suinevere
 | Params: buf -- destination, must hold SAVE_HEADER_SIZE bytes; partId -- the
 |   game part the save was made in; date -- packed BUP date
 | Returns: N/A
 ----------------------*/
void savedataWriteHeader(uint8_t *buf, uint16_t partId, uint32_t date);

/*----------------------
 | savedataReadHeader
 | Description: Unpacks a save header, checking the magic first.
 | Author: suinevere
 | Params: buf -- source, at least SAVE_HEADER_SIZE bytes; ver, partId, date --
 |   outputs, left untouched on failure
 | Returns: false on magic mismatch, true otherwise
 ----------------------*/
bool savedataReadHeader(const uint8_t *buf, uint16_t *ver, uint16_t *partId,
                        uint32_t *date);

/*----------------------
 | savedataProbe
 | Description: Reads a whole slot and classifies it for the slot list menu.
 | Author: suinevere
 | Params: device -- SAT_BUP_INTERNAL or SAT_BUP_CART; slot -- 0-based slot
 |   index; out -- filled in
 | Returns: the same state written to out->state
 ----------------------*/
SlotState savedataProbe(uint32_t device, int slot, SlotInfo *out);

/*----------------------
 | savedataChapterName
 | Description: Maps a game part id to the name the slot row shows.
 | Author: suinevere
 | Params: partId -- a GAME_PART* value
 | Returns: a name of at most 14 characters, or "UNKNOWN" if partId is not a
 |   reachable part (this covers GAME_PART1, the unreachable protection wheel)
 ----------------------*/
const char *savedataChapterName(uint16_t partId);

#endif /* SAVEDATA_H */
