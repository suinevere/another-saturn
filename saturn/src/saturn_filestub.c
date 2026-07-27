/*----------------------
 | saturn_filestub.c
 | Description: No-op stdio file operations. The Saturn has no writable host
 |   filesystem, so every file op fails. These back the stdFile File_impl in
 |   file.cxx, which is not the path a working build takes -- game data comes off
 |   the CD and saves belong in backup RAM. The stubs exist purely so that code
 |   compiles and links until a File_impl backed by SRL::Cd::File replaces it.
 |   Plain C with no SRL dependency, which is why it is not folded into
 |   saturn_compat.cxx.
 | Author: suinevere
 | Dependencies: saturn_compat.h (FILE, size_t)
 ----------------------*/
#include "saturn_compat.h"

/*----------------------
 | fopen / fclose / fread / fwrite / fseek / ftell / rewind
 | Description: Failing stdio stubs -- fopen returns NULL, reads/writes report 0
 |   bytes, fseek/ftell return -1, and fclose/rewind do nothing. stdFile checks
 |   fopen's result, so a NULL here surfaces as a clean "could not open" rather
 |   than a crash.
 | Author: suinevere
 ----------------------*/
FILE  *fopen(const char *path, const char *mode)           { (void)path; (void)mode; return (FILE *)0; }
int    fclose(FILE *s)                                     { (void)s; return 0; }
size_t fread(void *p, size_t sz, size_t n, FILE *s)        { (void)p; (void)sz; (void)n; (void)s; return 0; }
size_t fwrite(const void *p, size_t sz, size_t n, FILE *s) { (void)p; (void)sz; (void)n; (void)s; return 0; }
int    fseek(FILE *s, long off, int wh)                    { (void)s; (void)off; (void)wh; return -1; }
long   ftell(FILE *s)                                      { (void)s; return -1L; }
void   rewind(FILE *s)                                     { (void)s; }
