/*----------------------
 | savedata.h
 | Description: Save slot metadata between the engine and saturn_backup.h's
 |   raw BUP wrapper -- slot naming, save header packing, slot probing,
 |   chapter names, and backup device defaulting. Must not include srl.hpp or
 |   sega_bup.h so it stays safe to include from any engine translation unit.
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
	SAVE_MAX_BYTES   = 2048
};

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
 | savedataPickDefaultDevice
 | Description: Chooses which backup device the save menus should open on.
 | Author: suinevere
 | Params: internal, cart -- probe results for each device; internalHasSaves,
 |   cartHasSaves -- non-zero if that device already holds an AW_SAVE* file
 | Returns: SAT_BUP_CART only when the cart is present, formatted, and holds
 |   saves while internal does not; SAT_BUP_INTERNAL otherwise
 ----------------------*/
uint32_t savedataPickDefaultDevice(const SatBupDev *internal,
                                   const SatBupDev *cart, int internalHasSaves,
                                   int cartHasSaves);

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
