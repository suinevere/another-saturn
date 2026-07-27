/*----------------------
 | saturn_compat.h
 | Description: The C-runtime shims the engine needs on Saturn that the SRL dummy
 |   headers omit. SaturnRingLib puts modules/dummy on the include path AHEAD of
 |   newlib, and its <stdio.h> is nothing but `#define printf(...) ((void)0)` --
 |   no FILE, no fopen, no fprintf. So <cstdio> cannot be included at all here:
 |   libstdc++'s version fails on `using ::FILE;` before it ever reaches the
 |   engine's code. intern.h includes this header in its place.
 |   The heap and the diagnostic sinks are defined in saturn_compat.cxx (routed
 |   onto SRL); the file stubs in saturn_filestub.c.
 | Author: suinevere
 | Dependencies: stddef.h, stdarg.h
 ----------------------*/
#ifndef SATURN_COMPAT_H
#define SATURN_COMPAT_H
#include <stddef.h>
#include <stdarg.h>

/*----------------------
 | SEEK_SET / SEEK_CUR / SEEK_END
 | Description: The fseek whence constants the SRL dummy <stdio.h> omits. file.cxx
 |   passes SEEK_SET.
 | Author: suinevere
 ----------------------*/
#ifndef SEEK_SET
#define SEEK_SET 0
#endif
#ifndef SEEK_CUR
#define SEEK_CUR 1
#endif
#ifndef SEEK_END
#define SEEK_END 2
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | FILE + fopen/fclose/fread/fwrite/fseek/ftell/rewind
 | Description: A minimal FILE type and an always-failing file API, so file.cxx's
 |   stdFile compiles and links on Saturn. The Saturn has no writable host
 |   filesystem: game data is read from CD and saves belong in backup RAM, so
 |   every one of these fails by design (stubs in saturn_filestub.c). They exist
 |   to keep the abstraction intact until a File_impl backed by SRL::Cd::File
 |   replaces stdFile -- nothing should be reaching them on a working build.
 | Author: suinevere
 ----------------------*/
typedef struct AW_FILE FILE;
FILE  *fopen(const char *path, const char *mode);
int    fclose(FILE *stream);
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);
int    fseek(FILE *stream, long offset, int whence);
long   ftell(FILE *stream);
void   rewind(FILE *stream);

/*----------------------
 | printf / fprintf / fflush / sprintf / vsprintf / snprintf / stdout / stderr
 | Description: The formatted-output surface util.cxx's debug/warning/error use.
 |   sprintf/vsprintf/snprintf are real (they link from newlib and are used to
 |   build strings, not to write to a stream). printf/fprintf are the engine's
 |   diagnostic channel; on Saturn there is no stdout to write to, so
 |   saturn_compat.cxx discards them for now. stdout/stderr are opaque non-NULL
 |   handles that exist only so `fprintf(stderr, ...)` has something to pass.
 |   TODO: point these at an on-screen console once the video backend lands --
 |   until then the engine's warnings and errors are invisible, which will make
 |   early bring-up harder to debug than it needs to be.
 | Author: suinevere
 ----------------------*/
extern FILE *stdout;
extern FILE *stderr;

/*----------------------
 | fflush
 | Description: Compiled away, deliberately as a macro rather than a function.
 |   Defining an fflush() symbol here collides at link time: newlib really does
 |   provide one, and something in newlib's own stdio pulls that object in, so
 |   two strong definitions meet ("multiple definition of `fflush'"). Making it
 |   a macro means util.cxx's fflush(stdout) disappears at the call site instead,
 |   and newlib's version is never called with one of our fake FILE handles.
 | Author: suinevere
 ----------------------*/
#define fflush(stream) ((void)0)
int  printf(const char *fmt, ...);
int  fprintf(FILE *stream, const char *fmt, ...);
int  sprintf(char *str, const char *fmt, ...);
int  snprintf(char *str, size_t size, const char *fmt, ...);
int  vsprintf(char *str, const char *fmt, va_list ap);

/*----------------------
 | malloc / free / realloc
 | Description: The C heap, defined in saturn_compat.cxx onto SRL's HighWorkRam
 |   TLSF arena. Declared here because newlib's own malloc needs an sbrk the
 |   Saturn build does not have (shared.mk links -specs=nosys.specs), so the
 |   engine's allocations must go through SRL instead.
 | Author: suinevere
 ----------------------*/
void  *malloc(size_t size);
void   free(void *ptr);
void  *realloc(void *ptr, size_t size);

/*----------------------
 | sat_malloc_low
 | Description: Allocates from Low Work RAM specifically, rather than taking
 |   whatever malloc() happens to have room for. free() works on the result --
 |   it dispatches on the pointer's address, so no matching sat_free_low exists.
 |
 |   This is here because the two big allocations in this engine cannot share a
 |   megabyte and must not be left to a fallback to sort out. Video::init wants
 |   128 KB of framebuffer pages that the software rasterizer writes pixel by
 |   pixel, and Resource::allocMemBlock wants 600 KB that is filled once from CD
 |   and then read sequentially. High Work RAM is the faster bank and there is
 |   only about 750 KB of it left after the program; putting the 600 KB block
 |   there leaves roughly 11 KB for everything else, which is not survivable.
 |   So the pages stay high and the resource block goes low, on purpose.
 | Author: suinevere
 ----------------------*/
void  *sat_malloc_low(size_t size);

/*----------------------
 | exit
 | Description: Halts. There is no OS to return an exit status to, so the Saturn
 |   implementation (saturn_compat.cxx) parks in a Synchronize() loop forever
 |   rather than unwinding. Declared here because SGL ships its own <stdlib.h>
 |   (modules/sgl/INC, ahead of newlib on the include path) offering only
 |   atoi/atol/abs/qsort -- no exit and no malloc. util.cxx's error() calls this.
 |   Marked noreturn so callers of error() are understood not to continue.
 | Author: suinevere
 ----------------------*/
void   exit(int status) __attribute__((noreturn));

#ifdef __cplusplus
}
#endif
#endif /* SATURN_COMPAT_H */
