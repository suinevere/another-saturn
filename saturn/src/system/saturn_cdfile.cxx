/*----------------------
 | saturn_cdfile.cxx
 | Description: Reads files off the CD via SRL::Cd::File, behind the C interface
 |   in saturn_cdfile.h. Every read goes through SRL's LoadBytes (GFS_Load),
 |   which is stateless, and through a sector-aligned bounce buffer in Low Work
 |   RAM. The reasons for both choices are documented on sat_cd_read below.
 | Author: suinevere
 | Dependencies: saturn_cdfile.h, SRL (Cd::File, Memory::LowWorkRam)
 ----------------------*/
#include <srl.hpp>
#include "saturn_cdfile.h"
#include "saturn_audio.h"
#include "saturn_platform.h"
#include "saturn_probe.h"

// SGL's <string.h> has no extern "C" guard of its own, so including it from a
// .cxx without this wrapper declares memcpy with C++ linkage and the call site
// emits a mangled symbol nothing defines. Same trap as intern.h documents.
extern "C" {
#include <string.h>
}

/*----------------------
 | SECTOR_BYTES / BOUNCE_SECTORS / BOUNCE_BYTES
 | Description: A CD data sector is 2048 bytes. The bounce buffer is 16 of them
 |   (32 KB): big enough that the largest bank (~209 KB) is served in seven
 |   LoadBytes calls rather than a hundred, small enough to sit comfortably in
 |   Low Work RAM. It is sized in whole sectors on purpose -- see sat_cd_read.
 | Author: suinevere
 ----------------------*/
#define SECTOR_BYTES    2048
#define BOUNCE_SECTORS  16
#define BOUNCE_BYTES    (SECTOR_BYTES * BOUNCE_SECTORS)

/*----------------------
 | CACHE_WINDOW_BYTES
 | Description: How much of a file the whole-file read pulls between audio
 |   pumps. Smaller than the bounce buffer on purpose: this is a time budget, not
 |   a space one.
 |
 |   The PCM ring holds 46 ms. A window has to be read in less than that or the
 |   ring drains while the drive works. 8 KB is about 55 ms on a single-speed
 |   drive and half that at 2x -- marginal on paper, but the drive reads ahead
 |   and the ring is refilled the instant each window lands, so the exposure is
 |   one window, not the whole file.
 |
 |   [DEBUG-a4f2] The claim this carried -- that the extra seeks cost nothing
 |   measurable -- is false, and by a wide margin. Measured on the introduction's
 |   seam: bank01, 209 KB, took 3900 ms through this loop. That is 26 windows at
 |   150 ms each, against the 27 ms a 2x drive needs to transfer 8 KB. The other
 |   120 ms is latency, about one revolution of the disc: LoadBytes takes a start
 |   sector and issues a fresh command per call, so every window waits for the
 |   disc to come back around. The loop runs at 54 KB/s, a sixth of the drive.
 |
 |   It held. At 32 KB the introduction's four bank prefetches took 4333 ms, and
 |   the model closes: about 600 KB in 19 calls is 2280 ms of latency on top of
 |   2000 ms of transfer. So the per-call cost is fixed and the window is worth
 |   raising again -- 64 KB halves the calls and should take that to roughly
 |   3200 ms. The floor is the 2000 ms of transfer, which no window size reaches.
 |
 |   [DEBUG-a4f2] This is above the size the audio pump was lowered to protect,
 |   and is knowingly provisional. It is safe on the seam being measured, where
 |   the fade holds MVOL at zero and there is nothing to pop, but a gameplay load
 |   with music under it is a different case and has not been retested. Settle
 |   the permanent size once the seam is understood.
 | Author: suinevere
 ----------------------*/
#define CACHE_WINDOW_BYTES (SECTOR_BYTES * 32)

/*----------------------
 | SatCdFile
 | Description: An open disc file: the SRL object plus its cached size.
 | Author: suinevere
 ----------------------*/
struct SatCdFile
{
    SRL::Cd::File *File;
    int32_t        Size;
    uint8_t       *Data;   /* cache_storage while this file holds it, else NULL */
};

/*----------------------
 | FILE_CACHE_MAX
 | Description: The largest file read wholly into memory at open time. The
 |   biggest bank is bank01 at 209,250 bytes and the whole set totals about
 |   1.15 MB, so this holds any single one of them with room to spare.
 |
 |   It comes out of High Work RAM, which is mostly empty now that the engine's
 |   600 KB resource block lives in Low (see sat_malloc_low): roughly 625 KB
 |   free against the 128 KB of video pages that share it.
 | Author: suinevere
 ----------------------*/
#define FILE_CACHE_MAX  (256 * 1024)

/*----------------------
 | Do not add a minimum size to the whole-file cache.
 | Description: Tried, and it froze the boot. A 32 KB floor was put here to stop
 |   memlist.bin claiming the buffer in front of the Cinepak player, on the
 |   reasoning that a small file is one window off the disc anyway. That reasoning
 |   confuses file size with access pattern. Resource::readEntries parses
 |   memlist.bin a byte at a time through File::readByte -- about 2900 reads --
 |   and every one of them became a GFS_Load with its own seek, which is minutes
 |   of black screen rather than the memcpys it had been.
 |
 |   Size does not predict how a file is read, and the small ones are read the
 |   most finely. The player's memory is taken back by sat_cd_cache_release
 |   instead, which is a statement about when the buffer is needed rather than a
 |   guess about which files deserve it.
 | Author: suinevere
 ----------------------*/

/*----------------------
 | g_bounce
 | Description: The shared staging buffer, allocated once from Low Work RAM on
 |   first use and never released. It lives in LWRAM deliberately: High Work RAM
 |   is where the engine's 600 KB resource block and 128 KB of video pages go,
 |   which is already about three quarters of that bank.
 | Author: suinevere
 ----------------------*/
static uint8_t *g_bounce = nullptr;

/*----------------------
 | bounce_buffer
 | Description: Returns the staging buffer, allocating it on first call.
 | Author: suinevere
 | Returns: the buffer, or NULL if Low Work RAM could not satisfy it
 ----------------------*/
static uint8_t *bounce_buffer()
{
    if (g_bounce == nullptr)
    {
        g_bounce = (uint8_t *)SRL::Memory::LowWorkRam::Malloc(BOUNCE_BYTES);
    }

    return g_bounce;
}

/*----------------------
 | g_storage
 | Description: The whole-file cache's one buffer, allocated once from High Work
 |   RAM on first use and never released.
 |
 |   It used to be a Malloc and a Free per open, which is how the introduction's
 |   seam ended up reading banks off the disc a window at a time: a 200 KB block
 |   churned on every bank switch fragments High Work RAM, the allocation
 |   eventually fails, and the failure is silent -- sat_cd_open falls back to
 |   windowed reads and says nothing. Measured at 2236 ms of the seam.
 |
 |   One permanent buffer cannot fail after the first call and cannot fragment
 |   anything. It costs FILE_CACHE_MAX of High Work RAM for the whole run, which
 |   is what the old code was taking transiently anyway.
 | Author: suinevere
 ----------------------*/
static uint8_t *g_storage = nullptr;

/*----------------------
 | cache_storage
 | Description: Returns the whole-file cache buffer, allocating it on first
 |   call. Only one file can occupy it, so callers must not ask for it while the
 |   cached handle is open -- see the g_cacheBusy test at the one call site.
 | Author: suinevere
 | Returns: the buffer, or NULL if High Work RAM could not satisfy it
 ----------------------*/
static uint8_t *cache_storage()
{
    if (g_storage == nullptr)
    {
        g_storage = (uint8_t *)SRL::Memory::HighWorkRam::Malloc(FILE_CACHE_MAX);
    }

    return g_storage;
}

/*----------------------
 | g_cache / g_cacheName / g_cacheBusy
 | Description: A one-entry cache of the most recently opened file, kept open
 |   after the engine closes it.
 |
 |   This exists because Bank::read (bank.cxx:28) constructs a File, opens it,
 |   reads one resource and closes it -- once per resource. Every one of those
 |   opens was a fresh SRL::Cd::File, and so a fresh GFS name lookup on the
 |   disc. A game part pulls dozens of resources out of the same handful of bank
 |   files, so nearly all of that work was re-done per resource for no benefit.
 |
 |   One entry is enough because the engine reads strictly one file at a time.
 |   If a second file is opened while the cached one is still in use, that
 |   second handle is simply not cached rather than aliased -- correctness does
 |   not depend on the cache hitting.
 | Author: suinevere
 ----------------------*/
static SatCdFile *g_cache     = nullptr;
static char       g_cacheName[32];
static bool       g_cacheBusy = false;


/*----------------------
 | normalize_name
 | Description: Turns the engine's idea of a filename into the name GFS will
 |   match on an ISO9660 disc: drop any directory part, upper-case the rest, and
 |   append a '.' when there is no extension. The engine asks for "memlist.bin"
 |   and "bank01" (bank.cxx builds that with sprintf("bank%02x")), which on the
 |   disc are "MEMLIST.BIN" and "BANK01." -- an extensionless file really does
 |   carry a trailing dot in its ISO9660 name, and GFS_NameToId will not find it
 |   otherwise. This cost the original Jo Engine port of this game the same
 |   discovery.
 | Author: suinevere
 | Params: name -- source name; out -- destination; outSize -- its capacity
 | Returns: true if a usable name was produced
 ----------------------*/
static bool normalize_name(const char *name, char *out, int32_t outSize)
{
    if (name == nullptr || out == nullptr || outSize < 2)
    {
        return false;
    }

    // Keep only what follows the last separator.
    const char *base = name;

    for (const char *p = name; *p != '\0'; p++)
    {
        if (*p == '/' || *p == '\\')
        {
            base = p + 1;
        }
    }

    int32_t n = 0;
    bool hasDot = false;

    // Leave room for a possible trailing '.' and the terminator.
    while (base[n] != '\0' && n < outSize - 2)
    {
        char c = base[n];

        if (c >= 'a' && c <= 'z')
        {
            c = (char)(c - 'a' + 'A');
        }

        if (c == '.')
        {
            hasDot = true;
        }

        out[n] = c;
        n++;
    }

    if (n == 0)
    {
        return false;
    }

    if (!hasDot)
    {
        out[n++] = '.';
    }

    out[n] = '\0';
    return true;
}

extern "C" SatCdFile *sat_cd_open(const char *name)
{
    char resolved[32];

    if (!normalize_name(name, resolved, (int32_t)sizeof(resolved)))
    {
        return nullptr;
    }

    // The common case by a wide margin: the same bank file the last resource
    // came out of. Reusing it skips a GFS name lookup on the disc.
    if (g_cache != nullptr && !g_cacheBusy && strcmp(g_cacheName, resolved) == 0)
    {
        g_cacheBusy = true;
        return g_cache;
    }

    SRL::Cd::File *file = new SRL::Cd::File(resolved);

    if (file == nullptr)
    {
        return nullptr;
    }

    if (!file->Exists())
    {
        delete file;
        return nullptr;
    }

    SatCdFile *handle = new SatCdFile();

    if (handle == nullptr)
    {
        delete file;
        return nullptr;
    }

    handle->File = file;
    handle->Size = (int32_t)file->Size.Bytes;
    handle->Data = nullptr;

    // Pull the whole file in one go if it will fit. This is the load-time fix:
    // GFS_Load takes its start sector as an argument and so seeks on every
    // call, and Bank::read asks for one resource at a time -- so a part that
    // pulls forty resources out of bank01 paid forty seeks for a file that can
    // be fetched once. It is fetched in a handful of sector-aligned windows
    // rather than literally one call -- see the loop -- but that is seven seeks
    // for the largest bank instead of one per resource, and every read after it
    // is a memcpy. CACHE_WINDOW_BYTES sets how much is read between audio pumps.
    if (handle->Size > 0 && handle->Size <= FILE_CACHE_MAX)
    {
        const int32_t rounded =
            (handle->Size + SECTOR_BYTES - 1) / SECTOR_BYTES * SECTOR_BYTES;

        uint8_t *data = g_cacheBusy ? nullptr : cache_storage();

        if (data != nullptr)
        {
            // In sector-aligned windows rather than one call, so the PCM ring
            // can be topped up between them. The banks are up to 209 KB and a
            // single-speed drive takes over a second over that, against a ring
            // holding 186 ms -- one monolithic read here was the gap and the
            // popping heard during loading. sat_cd_read had always pumped
            // between its windows; this path replaced it for cached files and
            // did not inherit that.
            //
            // Straight into data, no bounce buffer: every window starts on a
            // sector boundary and data is allocated sector-rounded, so GFS
            // reading in whole sectors cannot overshoot the allocation. That is
            // exactly the condition sat_cd_read cannot rely on, which is why it
            // needs one.
            int32_t loaded = 0;
            bool    ok     = true;

            while (loaded < rounded)
            {
                int32_t window = rounded - loaded;

                if (window > CACHE_WINDOW_BYTES)
                {
                    window = CACHE_WINDOW_BYTES;
                }

                if (file->LoadBytes((size_t)(loaded / SECTOR_BYTES), window,
                                    data + loaded) < 0)
                {
                    ok = false;
                    break;
                }

                loaded += window;

                // The full update, not sat_audio_pump: the sequencer's timers
                // have to keep running or the music freezes on whatever pattern
                // was playing when the load started. Safe here in a way it is
                // not inside Bank::unpack -- SfxPlayer only indexes
                // res->_memList (sfxplayer.cxx:51,85) and never loads anything,
                // so it cannot re-enter this function.
                sat_audio_update();
            }

            if (!ok)
            {
                // Not fatal: fall back to reading windows off the disc. The
                // buffer is not released -- it is permanent now, see
                // cache_storage.
            }
            else
            {
                handle->Data = data;
            }

            sat_probe_mark(SAT_PROBE_CACHE_FILL);
        }
    }

    // Take the cache slot if it is not in use. The previous occupant is closed
    // here rather than in sat_cd_close, which is what lets a handle outlive the
    // engine's close call.
    if (!g_cacheBusy)
    {
        if (g_cache != nullptr)
        {
            delete g_cache->File;
            delete g_cache;
        }

        g_cache = handle;
        strncpy(g_cacheName, resolved, sizeof(g_cacheName) - 1);
        g_cacheName[sizeof(g_cacheName) - 1] = '\0';
        g_cacheBusy = true;
    }

    return handle;
}

extern "C" void sat_cd_close(SatCdFile *file)
{
    if (file == nullptr)
    {
        return;
    }

    // The cached handle is deliberately left open: the next open is very likely
    // to ask for it again. It is released when something else claims the slot.
    if (file == g_cache)
    {
        g_cacheBusy = false;
        return;
    }

    // No Data to release: the whole-file buffer belongs to cache_storage and
    // outlives every handle that points at it.
    delete file->File;
    delete file;
}

extern "C" void sat_cd_cache_release(void)
{
    if (g_cacheBusy)
    {
        return;
    }

    if (g_cache != nullptr)
    {
        delete g_cache->File;
        delete g_cache;
        g_cache = nullptr;
        g_cacheName[0] = '\0';
    }

    if (g_storage != nullptr)
    {
        SRL::Memory::HighWorkRam::Free(g_storage);
        g_storage = nullptr;
    }
}

extern "C" int32_t sat_cd_size(SatCdFile *file)
{
    return file != nullptr ? file->Size : -1;
}


/*----------------------
 | sat_cd_read
 | Description: Reads an arbitrary byte range, in bounce-buffer-sized windows.
 |
 |   Why not SRL's Seek()/Read(): those keep a 5-sector work buffer and index
 |   into it with `readBytes % workBufferSize`, which is only the right offset
 |   while the buffer's start stays on a multiple of that size. Seek() refills
 |   the buffer starting at an arbitrary sector, so after any seek to an
 |   unaligned offset the two disagree and Read() returns data from the wrong
 |   place. Seek() also leaves the GFS pointer advanced past the sectors it
 |   prefetched, which desynchronises ReadSectors() as well. LoadBytes() wraps
 |   GFS_Load, which takes its start sector as an argument and keeps no state, so
 |   none of that applies.
 |
 |   Why everything lands in a bounce buffer first: GFS reads in whole sectors,
 |   so a request that is not a sector multiple can write past the length asked
 |   for. Staging into a buffer that is itself a whole number of sectors makes
 |   that overshoot land somewhere harmless instead of past the end of the
 |   caller's buffer. (The Jo Engine port read sector-rounded counts straight
 |   into the caller's pointer whenever the offset happened to be aligned, which
 |   is a latent overrun; it is not copied here.)
 | Author: suinevere
 ----------------------*/
extern "C" int32_t sat_cd_read(SatCdFile *file, int32_t pos, void *dst, int32_t size)
{
    if (file == nullptr || dst == nullptr || size <= 0)
    {
        return -1;
    }

    if (pos < 0 || pos >= file->Size)
    {
        return -1;
    }

    // A read running off the end delivers what exists rather than failing.
    if (pos + size > file->Size)
    {
        size = file->Size - pos;
    }

    // Whole file already in memory: no disc, no bounce, no seek.
    if (file->Data != nullptr)
    {
        memcpy(dst, file->Data + pos, (size_t)size);
        return size;
    }

    uint8_t *bounce = bounce_buffer();

    if (bounce == nullptr)
    {
        return -1;
    }

    uint8_t *out = (uint8_t *)dst;
    int32_t done = 0;

    while (done < size)
    {
        const int32_t at     = pos + done;
        const int32_t sector = at / SECTOR_BYTES;
        const int32_t skew   = at % SECTOR_BYTES;

        // Most we can serve from one window, given the sub-sector head.
        int32_t want = size - done;

        if (want > BOUNCE_BYTES - skew)
        {
            want = BOUNCE_BYTES - skew;
        }

        // Ask for whole sectors covering [skew, skew + want), but never for more
        // than the file actually holds -- GFS errors on a read past the end.
        int32_t bytes = ((skew + want) + SECTOR_BYTES - 1) / SECTOR_BYTES * SECTOR_BYTES;
        const int32_t available = file->Size - (sector * SECTOR_BYTES);

        if (bytes > available)
        {
            bytes = available;
        }

        if (file->File->LoadBytes((size_t)sector, bytes, bounce) < 0)
        {
            return done > 0 ? done : -1;
        }

        memcpy(out + done, bounce + skew, want);
        done += want;

        sat_probe_mark(SAT_PROBE_DISC_READ);

        // Loading is the longest the engine ever goes without reaching
        // sat_video_present, which is where audio is normally topped up. A
        // multi-second load with no pump underruns the PCM ring and comes out
        // as popping, so the ring is fed between windows here too.
        sat_audio_update();
    }

    return done;
}
