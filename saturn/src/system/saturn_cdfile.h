/*----------------------
 | saturn_cdfile.h
 | Description: A small C interface for reading files off the CD, backed by
 |   SRL::Cd::File. It exists so file.cxx can gain a Saturn File_impl without
 |   pulling <srl.hpp> into an engine translation unit -- the engine's headers
 |   wrap SGL's C headers in extern "C" (see intern.h) and mixing the two include
 |   orders is fragile.
 |
 |   The interface is deliberately STATELESS about position: the caller passes an
 |   absolute byte offset to every read. SRL's own stateful API (Seek + Read)
 |   cannot be used for this engine's access pattern -- see the note on
 |   sat_cd_read in the .cxx.
 | Author: suinevere
 | Dependencies: stdint.h
 ----------------------*/
#ifndef SATURN_CDFILE_H
#define SATURN_CDFILE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | SatCdFile
 | Description: An opaque open file on the disc. Created by sat_cd_open, released
 |   by sat_cd_close.
 | Author: suinevere
 ----------------------*/
typedef struct SatCdFile SatCdFile;

/*----------------------
 | sat_cd_open
 | Description: Opens a file on the disc by name. The name is normalised for
 |   ISO9660 first: any directory part is dropped, the rest is upper-cased, and a
 |   trailing '.' is appended when the name has no extension. That last step is
 |   not cosmetic -- an extensionless file like "bank01" is recorded on the disc
 |   as "BANK01." and GFS_NameToId will not match it without the dot.
 | Author: suinevere
 | Params: name -- the engine's name for the file, any case, with or without a
 |   directory prefix ("memlist.bin", "game/bank01", ...)
 | Returns: a handle, or NULL if the file is not on the disc
 ----------------------*/
SatCdFile *sat_cd_open(const char *name);

/*----------------------
 | sat_cd_close
 | Description: Closes a handle from sat_cd_open. Ignores NULL.
 | Author: suinevere
 ----------------------*/
void sat_cd_close(SatCdFile *file);

/*----------------------
 | sat_cd_size
 | Description: The file's size in bytes.
 | Author: suinevere
 | Returns: the size, or -1 for a NULL handle
 ----------------------*/
int32_t sat_cd_size(SatCdFile *file);

/*----------------------
 | sat_cd_read
 | Description: Reads size bytes from an absolute byte offset into dst. Handles
 |   offsets that are not sector-aligned, which the engine's bank reads never are.
 |   A request running past the end of the file is clamped rather than refused.
 | Author: suinevere
 | Params: file -- handle; pos -- absolute byte offset; dst -- destination;
 |   size -- bytes wanted
 | Returns: bytes actually read, or -1 on error
 ----------------------*/
int32_t sat_cd_read(SatCdFile *file, int32_t pos, void *dst, int32_t size);

/*----------------------
 | sat_cd_cache_release
 | Description: Hands the whole-file cache's High Work RAM back, so something
 |   with a large one-off appetite can have it. That is the Cinepak player,
 |   whose decode buffer wants 143 KB of the same bank and which cannot share
 |   with a 256 KB cache standing in front of it.
 |
 |   Safe to call at any time and cheap when there is nothing to release. Does
 |   nothing while a cached file is still open, since that would pull the buffer
 |   out from under a live handle -- callers with a movie to start have no bank
 |   open, so this cannot silently fail for them. The buffer comes back on the
 |   next open big enough to want it.
 | Author: suinevere
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void sat_cd_cache_release(void);

#ifdef __cplusplus
}
#endif
#endif /* SATURN_CDFILE_H */
