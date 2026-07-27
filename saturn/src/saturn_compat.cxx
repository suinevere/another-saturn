/*----------------------
 | saturn_compat.cxx
 | Description: Routes the process-wide C heap (malloc/free/realloc) onto SRL's
 |   TLSF allocator in High Work RAM, and provides the diagnostic output sinks
 |   declared in saturn_compat.h. This heap is only valid AFTER
 |   SRL::Core::Initialize() runs at the top of main(), so no global or static
 |   object whose constructor allocates may be added to this build -- it would
 |   call this malloc before the SRL heap exists and fault.
 | Author: suinevere
 | Dependencies: saturn_compat.h, SRL (Memory::HighWorkRam)
 ----------------------*/
#include <srl.hpp>
#include "saturn_compat.h"

/*----------------------
 | malloc / free / realloc
 | Description: The standard C allocator, forwarded to SRL's High Work RAM TLSF
 |   arena. free ignores a NULL pointer.
 | Author: suinevere
 ----------------------*/
/*----------------------
 | inHighWorkRam
 | Description: Which SRL pool a pointer came from, decided by address. The
 |   Saturn maps High Work RAM at 0x06000000 and Low Work RAM at 0x00200000, and
 |   each is also visible through an uncached mirror 0x20000000 higher, so the
 |   cache-mode bits are masked off before comparing. This is what lets free()
 |   and realloc() stay pool-correct while malloc() is free to fall back.
 | Author: suinevere
 ----------------------*/
static bool inHighWorkRam(void *ptr)
{
    const uint32_t address = ((uint32_t)ptr) & 0x0FFFFFFF;
    return address >= 0x06000000;
}

/*----------------------
 | malloc / free / realloc
 | Description: The C allocator. Tries High Work RAM first and falls back to Low
 |   Work RAM when it cannot be satisfied.
 |
 |   Another World wants 600 KB for its resource block (resource.h:
 |   MEM_BLOCK_SIZE) plus 128 KB for four 320x200 4bpp video pages (video.cxx:
 |   Video::init), and the program itself already sits in High Work RAM -- 1 MB
 |   does not stretch to all of it. Low Work RAM is a second, separate megabyte
 |   that this port otherwise barely touches.
 |
 |   The two big allocations are placed deliberately rather than by fallback:
 |   see sat_malloc_low in saturn_compat.h for which goes where and why. This
 |   fallback is the safety net under everything else, so that a small
 |   allocation late in a full High Work RAM degrades to a slower pool instead
 |   of returning NULL into code that does not check -- which is how the first
 |   version of this presented: Video::init's malloc returned NULL and the
 |   memset immediately after faulted on it, a black screen stuck at boot step 4.
 |
 |   free ignores a NULL pointer. realloc stays within whichever pool the block
 |   already lives in rather than migrating it; nothing in this engine calls
 |   realloc, so it exists for completeness.
 | Author: suinevere
 ----------------------*/
extern "C" void *malloc(size_t size)
{
    void *ptr = SRL::Memory::HighWorkRam::Malloc(size);

    if (ptr == nullptr) {
        ptr = SRL::Memory::LowWorkRam::Malloc(size);
    }

    return ptr;
}

extern "C" void free(void *ptr)
{
    if (ptr != nullptr) {
        if (inHighWorkRam(ptr)) {
            SRL::Memory::HighWorkRam::Free(ptr);
        } else {
            SRL::Memory::LowWorkRam::Free(ptr);
        }
    }
}

extern "C" void *sat_malloc_low(size_t size)
{
    return SRL::Memory::LowWorkRam::Malloc(size);
}

extern "C" void *realloc(void *ptr, size_t size)
{
    if (ptr == nullptr) {
        return malloc(size);
    }

    if (inHighWorkRam(ptr)) {
        return SRL::Memory::HighWorkRam::Realloc(ptr, size);
    }

    return SRL::Memory::LowWorkRam::Realloc(ptr, size);
}

/*----------------------
 | stdout / stderr
 | Description: Opaque non-NULL stream handles. They are never dereferenced --
 |   they exist so `fprintf(stderr, ...)` in util.cxx has something to pass and
 |   so a stream argument can be told apart from NULL. The addresses are of a
 |   dummy object, not of any real FILE.
 | Author: suinevere
 ----------------------*/
static int g_stream_tags[2];
extern "C" FILE *stdout = (FILE *)&g_stream_tags[0];
extern "C" FILE *stderr = (FILE *)&g_stream_tags[1];

/*----------------------
 | vsnprintf
 | Description: Declared here rather than included: <stdio.h> resolves to SRL's
 |   dummy header, which declares nothing. The real vsnprintf links from newlib.
 | Author: suinevere
 ----------------------*/
extern "C" int vsnprintf(char *str, size_t size, const char *fmt, __builtin_va_list ap);

/*----------------------
 | DIAG_ROWS / DIAG_COLS / g_diagRow
 | Description: State for the on-screen diagnostic log: one message per row on
 |   SRL's debug text layer, wrapping back to the top at the bottom of the screen.
 |   NBG3 stays available for this because our bitmap layer is only 4bpp -- SRL
 |   drops NBG3 when NBG1 goes above 8bpp, which is not the case here.
 | Author: suinevere
 ----------------------*/
#define DIAG_ROWS 27
#define DIAG_COLS 40
static int g_diagRow = 0;

/*----------------------
 | diag_emit
 | Description: Formats one diagnostic line and prints it to the debug layer,
 |   trimming the trailing newline the engine's messages carry (it would render
 |   as a stray glyph).
 | Author: suinevere
 ----------------------*/
static void diag_emit(const char *fmt, __builtin_va_list ap)
{
    char line[DIAG_COLS + 1];
    vsnprintf(line, sizeof(line), fmt, ap);

    for (int i = 0; line[i] != '\0'; i++)
    {
        if (line[i] == '\n' || line[i] == '\r')
        {
            line[i] = '\0';
            break;
        }
    }

    SRL::Debug::Print(0, g_diagRow, "%s", line);
    g_diagRow++;

    if (g_diagRow >= DIAG_ROWS)
    {
        g_diagRow = 0;
    }
}

/*----------------------
 | printf / fprintf
 | Description: The engine's diagnostic channel, routed to the debug text layer.
 |   util.cxx's error() and warning() reach the screen through here, and error()
 |   halts immediately afterwards -- without this a fatal engine error is
 |   indistinguishable from a black screen, which is exactly how the first boot
 |   of this port presented. debug() also arrives here, but only when
 |   g_debugMask is set, which it is not by default.
 | Author: suinevere
 ----------------------*/
extern "C" int printf(const char *fmt, ...)
{
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    diag_emit(fmt, ap);
    __builtin_va_end(ap);
    return 0;
}

extern "C" int fprintf(FILE *stream, const char *fmt, ...)
{
    (void)stream;
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    diag_emit(fmt, ap);
    __builtin_va_end(ap);
    return 0;
}

/* fflush is a no-op macro in saturn_compat.h -- see the note there on why it
   must not be a symbol. */

/*----------------------
 | exit
 | Description: Halts the machine. There is nothing to exit to on a Saturn, so
 |   this parks in a Synchronize() loop forever, keeping the display alive and
 |   the watchdog fed rather than returning into undefined behaviour. util.cxx's
 |   error() ends here, so today a fatal engine error is an indistinguishable
 |   freeze -- see the printf TODO above; wiring the diagnostic channel to a
 |   visible console is what turns this into a usable error report.
 | Author: suinevere
 ----------------------*/
extern "C" void exit(int status)
{
    (void)status;
    while (true) {
        SRL::Core::Synchronize();
    }
}
