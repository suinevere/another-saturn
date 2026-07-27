/*----------------------
 | saturn_new.cxx
 | Description: The global C++ allocation operators for the engine's translation
 |   units, routed onto malloc -- which saturn_compat.cxx points at SRL's High
 |   Work RAM TLSF arena, so there is still exactly one heap.
 |
 |   Why this is a file of its own, and why it must NOT include <srl.hpp>:
 |   SRL already defines these operators, but `inline`, in srl_memory.hpp. An
 |   inline definition is only emitted in translation units that actually
 |   include it, and the engine's files (file.cxx's `new stdFile`, main.cxx's
 |   engine allocation) include no SRL headers at all -- they emit a call to the
 |   standard _Znwj/_ZdlPv symbols, which nothing then defines, and the link
 |   fails. Defining them here supplies exactly those symbols. Putting these
 |   definitions in saturn_compat.cxx instead does not work: that file needs
 |   <srl.hpp> for the allocator, and the compiler rejects them as a redefinition
 |   of SRL's inline versions.
 |
 |   Same ordering constraint as malloc: the SRL heap does not exist until
 |   SRL::Core::Initialize() runs at the top of main(), so no global or static
 |   object whose constructor allocates may be added to this build.
 | Author: suinevere
 | Dependencies: saturn_compat.h (malloc/free)
 ----------------------*/
#include "saturn_compat.h"

/*----------------------
 | operator new / operator delete (and the [] and sized forms)
 | Description: Plain forwards to malloc/free. The sized delete overloads are
 |   what GCC 14 actually emits at the call site; they ignore the size because
 |   TLSF tracks block sizes itself. free() already tolerates a null pointer.
 | Author: suinevere
 ----------------------*/
void *operator new(size_t size)                     { return malloc(size); }
void *operator new[](size_t size)                   { return malloc(size); }
void  operator delete(void *ptr) noexcept           { free(ptr); }
void  operator delete[](void *ptr) noexcept         { free(ptr); }
void  operator delete(void *ptr, size_t) noexcept   { free(ptr); }
void  operator delete[](void *ptr, size_t) noexcept { free(ptr); }
