/*----------------------
 | bup_devmap.h
 | Description: Resolves which BIOS backup device index is the main unit and
 |   which is the cartridge, from whether each index answered a probe. Kept free
 |   of sega_bup.h and srl.hpp so it stays host-testable -- saturn_backup.cxx
 |   owns the SGL includes, and this is the one decision in it worth proving
 |   off-target.
 |
 |   Two readings of the BUP API were tried and measured wrong before this one,
 |   both recorded here so neither is attempted again:
 |
 |   1. SGL's BUP_MAIN_UNIT (1) and BUP_CURTRIDGE (2) are NOT the device
 |      argument. That argument indexes the BupConfig table BUP_Init is given,
 |      so passing the unit ids reached the cartridge and an empty slot -- every
 |      save landed on the cart and the cart itself probed as absent.
 |   2. BupConfig is not a per-device descriptor either. On hardware every entry
 |      comes back unit_id 2, partition 2, including the entry for a slot that
 |      holds nothing, so nothing in that table distinguishes one device from
 |      another.
 |
 |   What does work is asking each index whether it is there. Measured on real
 |   hardware, BUP_Stat over indices 0..2 returns OK/OK/BUP_NON with a cartridge
 |   fitted and OK/BUP_NON/BUP_NON without one.
 | Author: suinevere
 | Dependencies: N/A
 ----------------------*/
#ifndef BUP_DEVMAP_H
#define BUP_DEVMAP_H

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | BUP_DEVMAP_NONE
 | Description: Returned for a logical device the machine does not have.
 | Author: suinevere
 ----------------------*/
#define BUP_DEVMAP_NONE (-1)

/*----------------------
 | bupDevmapResolve
 | Description: Names the responding device indices: the first is the main unit,
 |   the next is the cartridge.
 |
 |   The main unit always resolves to some index, because internal backup RAM is
 |   soldered in -- if nothing answered at all, index 0 is still reported, and
 |   the individual sat_bup_* calls report the failure honestly rather than this
 |   layer hiding a machine with no working device behind BUP_DEVMAP_NONE.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: present -- one flag per device index, non-zero if that index answered
 |   a probe; count -- how many indices; internalIdx, cartIdx -- outputs, always
 |   written; cartIdx is BUP_DEVMAP_NONE when only one index answered
 | Returns: N/A
 ----------------------*/
void bupDevmapResolve(const int *present, int count,
                      int *internalIdx, int *cartIdx);

#ifdef __cplusplus
}
#endif

#endif /* BUP_DEVMAP_H */
