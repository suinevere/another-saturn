/* Raw - Another World Interpreter
 * Copyright (C) 2004 Gregory Montoir
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#ifndef __INTERN_H__
#define __INTERN_H__

// saturn_compat.h stands in for <cstdio>: SaturnRingLib puts modules/dummy on
// the include path ahead of newlib, and its <stdio.h> declares nothing but a
// no-op printf macro, so libstdc++'s <cstdio> fails on `using ::FILE;`. The
// shim declares the FILE API, the printf family and the heap instead.
// <cstring> and <cstdlib> are not shadowed and come from newlib as normal;
// <cassert> resolves to modules/dummy/assert.h, where assert() is compiled out
// entirely -- the engine's ~11 asserts are inert on Saturn.
#include "saturn_compat.h"
// The C headers, not <cstring>/<cstdlib>/<cassert>. libstdc++'s C++ wrappers
// hoist the *entire* standard set into namespace std and hard-fail on anything
// the underlying libc omits -- <cstring> dies on strcoll/strerror/strtok/strxfrm,
// none of which this engine uses.
//
// Note these do NOT resolve to newlib: SGL ships its own <string.h> and
// <stdlib.h> in modules/sgl/INC, which sits ahead of newlib on the include
// path. SGL's string.h covers everything the engine needs (strlen/strcpy/
// memcpy/memset/...), but its stdlib.h offers only atoi/atol/abs/qsort -- no
// malloc and no exit, which is why saturn_compat.h above declares those.
// <assert.h> resolves to modules/dummy/assert.h, where assert() is compiled out
// entirely, so the engine's ~11 asserts are inert on Saturn.
//
// The extern "C" wrapper is required, not defensive. SGL's headers are pure C
// with no `#ifdef __cplusplus` guard of their own, so including them from a C++
// TU declares strlen/memset/memcpy/... with C++ linkage and every call site
// emits a mangled symbol (`memset(void*, int, unsigned int)`) that nothing in
// the SGL libraries defines. The link then fails across nearly every object.
#ifdef __cplusplus
extern "C" {
#endif
#include <string.h>
#include <stdlib.h>
#ifdef __cplusplus
}
#endif
#include <assert.h>

#include "endian.h"
#include "util.h"

#define MAX(x,y) ((x)>(y)?(x):(y))
#define MIN(x,y) ((x)<(y)?(x):(y))
#define ARRAYSIZE(a) (sizeof(a)/sizeof(a[0]))

template<typename T>
inline void SWAP(T &a, T &b) {
	T tmp = a; 
	a = b;
	b = tmp;
}

struct Ptr {
	uint8_t *pc;
	
	uint8_t fetchByte() {
		return *pc++;
	}
	
	uint16_t fetchWord() {
		uint16_t i = READ_BE_UINT16(pc);
		pc += 2;
		return i;
	}
};

struct Point {
	int16_t x, y;

	Point() : x(0), y(0) {}
	Point(int16_t xx, int16_t yy) : x(xx), y(yy) {}
	Point(const Point &p) : x(p.x), y(p.y) {}
};

#endif
