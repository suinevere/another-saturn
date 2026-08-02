/*----------------------
 | saturn_backup.h
 | Description: A small C interface for Saturn backup RAM, backed by SGL's BUP
 |   vector table. It exists so savedata.cxx can read and write saves without
 |   pulling <srl.hpp> into an engine translation unit -- the engine's headers
 |   wrap SGL's C headers in extern "C" (see intern.h) and mixing the two
 |   include orders is fragile. Same shape as saturn_cdfile.h.
 |
 |   Every entry point takes the device explicitly. There is deliberately no
 |   implicit "current device" here: the menu owns that choice.
 | Author: suinevere
 | Dependencies: stdint.h
 ----------------------*/
#ifndef SATURN_BACKUP_H
#define SATURN_BACKUP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | SAT_BUP_INTERNAL / SAT_BUP_CART
 | Description: Device ids, matching SGL's BUP_MAIN_UNIT and BUP_CURTRIDGE.
 | Author: suinevere
 ----------------------*/
#define SAT_BUP_INTERNAL 1
#define SAT_BUP_CART     2

/*----------------------
 | SAT_BUP_*
 | Description: Return codes. Distinct from SGL's so callers need not include
 |   sega_bup.h.
 | Author: suinevere
 ----------------------*/
#define SAT_BUP_OK              0
#define SAT_BUP_ERR_NONE        1  /* device absent */
#define SAT_BUP_ERR_UNFORMAT    2
#define SAT_BUP_ERR_PROTECTED   3
#define SAT_BUP_ERR_NO_SPACE    4
#define SAT_BUP_ERR_NOT_FOUND   5
#define SAT_BUP_ERR_BROKEN      6

/*----------------------
 | SatBupDev
 | Description: What sat_bup_probe found on one device.
 | Author: suinevere
 ----------------------*/
typedef struct {
    int      present;
    int      formatted;
    int      writeProtected;
    uint32_t freeBytes;
} SatBupDev;

/*----------------------
 | SatBupEntry
 | Description: What sat_bup_dir found for one filename.
 | Author: suinevere
 ----------------------*/
typedef struct {
    int      exists;
    uint32_t size;
    uint32_t date;
} SatBupEntry;

/*----------------------
 | sat_bup_init
 | Description: Brings up the BIOS backup library. Call once, after
 |   sat_boot_init and before any other sat_bup_* call.
 | Author: suinevere
 ----------------------*/
void sat_bup_init(void);

/*----------------------
 | sat_bup_probe
 | Description: Reports whether a device is present, formatted, writable, and
 |   how much room it has left.
 | Author: suinevere
 | Params: device -- SAT_BUP_INTERNAL or SAT_BUP_CART; out -- filled in
 | Returns: SAT_BUP_OK, or an error code with out zeroed
 ----------------------*/
int sat_bup_probe(uint32_t device, SatBupDev *out);

/*----------------------
 | sat_bup_dir
 | Description: Looks a save up by name without reading its contents.
 | Author: suinevere
 | Params: device -- device id; name -- BUP filename; out -- filled in
 | Returns: SAT_BUP_OK whether or not the file exists; check out->exists.
 |   An error code means the lookup itself failed.
 ----------------------*/
int sat_bup_dir(uint32_t device, const char *name, SatBupEntry *out);

/*----------------------
 | sat_bup_read
 | Description: Reads a whole save into dst.
 | Author: suinevere
 | Params: device -- device id; name -- BUP filename; dst -- destination;
 |   size -- capacity of dst
 | Returns: SAT_BUP_OK, or SAT_BUP_ERR_NOT_FOUND / SAT_BUP_ERR_BROKEN
 ----------------------*/
int sat_bup_read(uint32_t device, const char *name, void *dst, int32_t size);

/*----------------------
 | sat_bup_write
 | Description: Writes a save, stamping it with the current RTC time.
 | Author: suinevere
 | Params: device -- device id; name -- BUP filename; comment -- up to 10
 |   characters shown by the Saturn's Backup Manager; src -- the bytes;
 |   size -- how many; overwrite -- non-zero to replace an existing file
 | Returns: SAT_BUP_OK, or SAT_BUP_ERR_NO_SPACE / _PROTECTED / _UNFORMAT
 ----------------------*/
int sat_bup_write(uint32_t device, const char *name, const char *comment,
                  const void *src, int32_t size, int overwrite);

/*----------------------
 | sat_bup_delete
 | Description: Removes a save.
 | Author: suinevere
 | Params: device -- device id; name -- BUP filename
 | Returns: SAT_BUP_OK, or SAT_BUP_ERR_NOT_FOUND
 ----------------------*/
int sat_bup_delete(uint32_t device, const char *name);

/*----------------------
 | sat_bup_date_now
 | Description: The current RTC time as a BUP date word.
 | Author: suinevere
 | Returns: the packed word, or 0 if the clock is unreadable
 ----------------------*/
uint32_t sat_bup_date_now(void);

/*----------------------
 | sat_bup_date_split
 | Description: Unpacks a BUP date word into the fields the slot list shows.
 |   Pure arithmetic, and the only part of this file the host tests exercise.
 | Author: suinevere
 | Params: date -- packed word; month, day, hour, min -- outputs, any may be NULL
 | Returns: N/A
 ----------------------*/
void sat_bup_date_split(uint32_t date, int *month, int *day, int *hour, int *min);

#ifdef __cplusplus
}
#endif
#endif /* SATURN_BACKUP_H */
