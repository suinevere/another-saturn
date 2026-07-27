---
name: srl-libc-shadowing
description: The five libc/linkage traps in a SaturnRingLib build that break ordinary C++ engine code, and the fix for each.
metadata:
  type: reference
---

Porting existing C/C++ to SaturnRingLib means fighting the include path before you
fight the hardware. All five of these hit Another-Saturn on the first build
(2026-07-27) and cost more time than anything Saturn-specific. Handled by
`saturn/src/saturn_compat.{h,cxx}`, `saturn_filestub.c`, `saturn_new.cxx`.

**Why:** `shared.mk` puts `modules/dummy`, `modules/sgl/INC` and `modules/danny/INC`
on `-I` *ahead* of the toolchain's newlib, so `<stdio.h>`, `<stdlib.h>`, `<string.h>`
and `<assert.h>` are not the headers you think they are.

**How to apply:**

1. **`<cstdio>` cannot be included at all.** `modules/dummy/stdio.h` is nothing but
   `#define printf(...) ((void)0)` — no `FILE`, no `fopen`. libstdc++'s `<cstdio>`
   then dies on `using ::FILE;`. Include a shim declaring the FILE API instead.
   (`modules/dummy/assert.h` likewise compiles `assert()` out to `((void)0)`.)
2. **Prefer C headers over the `<cXXX>` wrappers everywhere.** libstdc++'s wrappers
   hoist the *entire* standard set into `std::` and hard-fail on anything the libc
   omits — `<cstring>` dies on `strcoll`/`strerror`/`strtok`/`strxfrm` even though
   nothing uses them. `<string.h>` declares only what is really there.
3. **SGL ships its own `stdlib.h` and `string.h`** in `modules/sgl/INC`. Its
   `string.h` is complete enough (`strlen`/`strcpy`/`memcpy`/`memset`/…), but its
   `stdlib.h` offers only `atoi`/`atol`/`abs`/`qsort` — **no `malloc`, no `exit`**.
   Both must come from the shim: `malloc` onto `SRL::Memory::HighWorkRam` (newlib's
   needs an `sbrk` that `-specs=nosys.specs` doesn't provide), and `exit` as a
   `while(true) SRL::Core::Synchronize();` halt.
4. **SGL's headers have no `extern "C"` guard.** Including them from a `.cxx`
   declares `strlen`/`memset`/`memcpy` with C++ linkage, so every call site emits a
   mangled `memset(void*, int, unsigned int)` that no SGL library defines, and the
   link fails across nearly every object. Wrap the includes yourself:
   `extern "C" { #include <string.h> #include <stdlib.h> }`.
5. **Global `operator new`/`delete` are `inline` in `srl_memory.hpp`.** An inline
   definition is only emitted in TUs that include it, so engine files that include
   no SRL header emit calls to `_Znwj`/`_ZdlPv` that nothing defines. Define the
   operators in a TU that **does not** include `<srl.hpp>` (else the compiler
   rejects them as redefinitions of SRL's inline versions) and forward to `malloc`.

Bonus trap: **do not define `fflush`.** newlib really has one and its own stdio pulls
that object in, giving "multiple definition of `fflush'". Make it a no-op *macro* so
the call site vanishes instead. `printf`/`fprintf` are safe to define — nothing in
newlib pulls those in when your definition already resolves them.

Constraint that falls out of all this: because `malloc` and `operator new` both route
to an SRL arena that does not exist until `SRL::Core::Initialize()` runs at the top of
`main()`, **no global or static object whose constructor allocates may exist** in the
build. See [[another-world-port-surface]].
