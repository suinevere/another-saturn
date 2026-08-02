/*----------------------
 | stub_saturn_backup.cxx
 | Description: A host stand-in for saturn_backup.cxx. Devices and saves live
 |   in arrays that tests set up directly, so savedata and menu logic can be
 |   exercised off-target. sat_bup_date_split is the real implementation, not a
 |   stub -- it is pure arithmetic and worth testing for real.
 | Author: suinevere
 | Dependencies: saturn_backup.h
 ----------------------*/
#include <cstring>
#include "saturn_backup.h"

#define STUB_MAX_FILES 8
#define STUB_MAX_BYTES 4096

typedef struct {
    char     name[12];
    uint8_t  data[STUB_MAX_BYTES];
    int32_t  size;
    uint32_t date;
    int      used;
} StubFile;

static SatBupDev s_dev[3];
static StubFile  s_files[3][STUB_MAX_FILES];

/*----------------------
 | stub_bup_reset
 | Description: Clears all stub devices and files back to a known baseline.
 | Author: suinevere
 | Globals: s_dev, s_files
 | Returns: N/A
 ----------------------*/
void stub_bup_reset(void)
{
    memset(s_dev, 0, sizeof(s_dev));
    memset(s_files, 0, sizeof(s_files));
    s_dev[SAT_BUP_INTERNAL].present = 1;
    s_dev[SAT_BUP_INTERNAL].formatted = 1;
    s_dev[SAT_BUP_INTERNAL].freeBytes = 29000;
}

/*----------------------
 | stub_bup_set_device
 | Description: Configures one stub device's presence, format, and free space.
 | Author: suinevere
 | Globals: s_dev
 | Params: device -- device id; present, formatted, writeProtected -- flags;
 |   freeBytes -- reported free space
 | Returns: N/A
 ----------------------*/
void stub_bup_set_device(uint32_t device, int present, int formatted,
                         int writeProtected, uint32_t freeBytes)
{
    s_dev[device].present = present;
    s_dev[device].formatted = formatted;
    s_dev[device].writeProtected = writeProtected;
    s_dev[device].freeBytes = freeBytes;
}

/*----------------------
 | stub_find
 | Description: Looks up a stub file by device and name.
 | Author: suinevere
 | Globals: s_files
 | Params: device -- device id; name -- filename to match
 | Returns: the matching StubFile, or NULL
 ----------------------*/
static StubFile *stub_find(uint32_t device, const char *name)
{
    for (int i = 0; i < STUB_MAX_FILES; ++i) {
        if (s_files[device][i].used &&
            strcmp(s_files[device][i].name, name) == 0) {
            return &s_files[device][i];
        }
    }
    return 0;
}

/*----------------------
 | stub_bup_add_file
 | Description: Injects a file into a stub device's directory for a test to
 |   find, bypassing sat_bup_write.
 | Author: suinevere
 | Globals: s_files
 | Params: device -- device id; name -- filename; data -- bytes; size -- byte
 |   count; date -- packed BUP date to store
 | Returns: N/A
 ----------------------*/
void stub_bup_add_file(uint32_t device, const char *name, const void *data,
                       int32_t size, uint32_t date)
{
    for (int i = 0; i < STUB_MAX_FILES; ++i) {
        if (!s_files[device][i].used) {
            s_files[device][i].used = 1;
            strncpy(s_files[device][i].name, name, 11);
            s_files[device][i].name[11] = 0;
            memcpy(s_files[device][i].data, data, size);
            s_files[device][i].size = size;
            s_files[device][i].date = date;
            return;
        }
    }
}

/*----------------------
 | sat_bup_init
 | Description: No-op on the host; the stub needs no bring-up.
 | Author: suinevere
 | Returns: N/A
 ----------------------*/
void sat_bup_init(void) {}

/*----------------------
 | sat_bup_probe
 | Description: Reports the stub device state stub_bup_set_device configured.
 | Author: suinevere
 | Globals: s_dev
 | Params: device -- device id; out -- filled in
 | Returns: SAT_BUP_OK, or an error code with out zeroed
 ----------------------*/
int sat_bup_probe(uint32_t device, SatBupDev *out)
{
    *out = s_dev[device];
    if (!out->present) {
        return SAT_BUP_ERR_NONE;
    }
    if (!out->formatted) {
        return SAT_BUP_ERR_UNFORMAT;
    }
    return SAT_BUP_OK;
}

/*----------------------
 | sat_bup_dir
 | Description: Looks a stub file up by name without reading its contents.
 | Author: suinevere
 | Params: device -- device id; name -- filename; out -- filled in
 | Returns: SAT_BUP_OK always; check out->exists
 ----------------------*/
int sat_bup_dir(uint32_t device, const char *name, SatBupEntry *out)
{
    StubFile *f = stub_find(device, name);
    memset(out, 0, sizeof(*out));
    if (f) {
        out->exists = 1;
        out->size = (uint32_t)f->size;
        out->date = f->date;
    }
    return SAT_BUP_OK;
}

/*----------------------
 | sat_bup_read
 | Description: Reads a stub file's bytes into dst. Refuses rather than
 |   truncates when the stored file is bigger than dst, matching the real
 |   wrapper's BUP_Dir-then-BUP_Read behaviour instead of masking it.
 | Author: suinevere
 | Params: device -- device id; name -- filename; dst -- destination;
 |   size -- capacity of dst
 | Returns: SAT_BUP_OK, or SAT_BUP_ERR_NOT_FOUND / SAT_BUP_ERR_BROKEN (when
 |   the stored file is larger than size)
 ----------------------*/
int sat_bup_read(uint32_t device, const char *name, void *dst, int32_t size)
{
    StubFile *f = stub_find(device, name);
    if (!f) {
        return SAT_BUP_ERR_NOT_FOUND;
    }
    if (f->size > size) {
        return SAT_BUP_ERR_BROKEN;
    }
    memcpy(dst, f->data, f->size);
    return SAT_BUP_OK;
}

/*----------------------
 | sat_bup_write
 | Description: Writes or overwrites a stub file, enforcing the same presence,
 |   format, protection, and space rules the real BIOS would. Refusing a file
 |   that already exists when overwrite=0 reports SAT_BUP_ERR_EXISTS, matching
 |   the real wrapper's BUP_FOUND mapping -- not SAT_BUP_ERR_NO_SPACE, which
 |   would be a lie about why the write was refused. size is also bounded
 |   against STUB_MAX_BYTES so an oversized write cannot overrun a StubFile's
 |   fixed-size data array.
 | Author: suinevere
 | Globals: s_dev
 | Params: device -- device id; name -- filename; comment -- unused by the
 |   stub; src -- bytes; size -- how many; overwrite -- non-zero to replace
 | Returns: SAT_BUP_OK, or SAT_BUP_ERR_EXISTS / _NO_SPACE / _PROTECTED /
 |   _UNFORMAT / _NONE
 ----------------------*/
int sat_bup_write(uint32_t device, const char *name, const char *comment,
                  const void *src, int32_t size, int overwrite)
{
    (void)comment;
    if (!s_dev[device].present) return SAT_BUP_ERR_NONE;
    if (!s_dev[device].formatted) return SAT_BUP_ERR_UNFORMAT;
    if (s_dev[device].writeProtected) return SAT_BUP_ERR_PROTECTED;
    if ((uint32_t)size > s_dev[device].freeBytes) return SAT_BUP_ERR_NO_SPACE;
    if (size > STUB_MAX_BYTES) return SAT_BUP_ERR_NO_SPACE;

    StubFile *f = stub_find(device, name);
    if (f && !overwrite) {
        return SAT_BUP_ERR_EXISTS;
    }
    if (f) {
        memcpy(f->data, src, size);
        f->size = size;
        return SAT_BUP_OK;
    }
    stub_bup_add_file(device, name, src, size, 0);
    return SAT_BUP_OK;
}

/*----------------------
 | sat_bup_delete
 | Description: Removes a stub file.
 | Author: suinevere
 | Params: device -- device id; name -- filename
 | Returns: SAT_BUP_OK, or SAT_BUP_ERR_NOT_FOUND
 ----------------------*/
int sat_bup_delete(uint32_t device, const char *name)
{
    StubFile *f = stub_find(device, name);
    if (!f) {
        return SAT_BUP_ERR_NOT_FOUND;
    }
    f->used = 0;
    return SAT_BUP_OK;
}

/*----------------------
 | sat_bup_date_now
 | Description: The stub has no RTC, so it always reports the epoch.
 | Author: suinevere
 | Returns: 0
 ----------------------*/
uint32_t sat_bup_date_now(void) { return 0; }

/*----------------------
 | sat_bup_date_split
 | Description: Unpacks a BUP date word into calendar fields. The real
 |   implementation, not a stub -- saturn_backup.cxx copies this verbatim.
 | Author: suinevere
 | Params: date -- packed word; month, day, hour, min -- outputs, any may be
 |   NULL
 | Returns: N/A
 ----------------------*/
void sat_bup_date_split(uint32_t date, int *month, int *day, int *hour, int *min)
{
    static const int len[12] = {31,28,31,30,31,30,31,31,30,31,30,31};

    uint32_t days = date / 1440u;
    uint32_t rem = date % 1440u;

    int year = 1980;
    for (;;) {
        int inYear = ((year % 4) == 0) ? 366 : 365;
        if (days < (uint32_t)inYear) {
            break;
        }
        days -= (uint32_t)inYear;
        year++;
    }

    int mo = 0;
    for (;;) {
        int inMonth = len[mo];
        if (mo == 1 && (year % 4) == 0) {
            inMonth = 29;
        }
        if (days < (uint32_t)inMonth) {
            break;
        }
        days -= (uint32_t)inMonth;
        mo++;
    }

    if (month) *month = mo + 1;
    if (day)   *day = (int)days + 1;
    if (hour)  *hour = (int)(rem / 60u);
    if (min)   *min = (int)(rem % 60u);
}
