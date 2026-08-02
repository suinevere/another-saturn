# Title Card, Save/Load Menus and Pause Menu — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a title card, a pause menu, and three save slots backed by Saturn backup RAM, with device selection when a backup cartridge is present.

**Architecture:** The save state drops the four 32 KB video pages via the serialiser's existing `maxVer` gating, shrinking a slot from ~129 KB to ~1.6 KB so three slots fit internal backup RAM. A C wrapper over SGL's `BUP_*` vector table handles storage; a pure state machine handles menu logic; a drawing layer composites into a dedicated 32 KB page that the VM never sees.

**Tech Stack:** C++11 (no RTTI, no exceptions), SaturnRingLib, SGL (`sega_bup.h`), host g++ for unit tests.

**Spec:** `docs/superpowers/specs/2026-08-01-title-and-save-menus-design.md`

## Global Constraints

- **File extensions:** new C++ files use `.cxx`, new C files use `.c`. `saturn/makefile` derives object names with `$(SOURCES:.c=.o)` then `:.cxx=.o` and defines pattern rules for only those two. A `.cpp` file is silently dropped from the link. Sources are auto-globbed from `src/`, so no makefile edit is needed to add a file.
- **Comment style (`CLAUDE.md`):** no comments inside functions. Every file, function and constant gets a header block in this form:
  ```c
  /*----------------------
   | name
   | Description: One sentence on what it does.
   | Author: suinevere
   | Dependencies: foo.h, bar.h
   | Globals: g_thing
   | Params: x -- what it is
   | Returns: N/A
   ----------------------*/
  ```
  Use `N/A` for fields that do not apply. Keep prose to a sentence.
- **Commits:** one sentence, no body, no bullets, no trailers. Never mention Claude, AI, or the session. Author of record is **suinevere**. Commit after every task.
- **SRL isolation:** engine translation units must never include `<srl.hpp>`. Anything Saturn-specific goes behind a plain C header, implemented in a `.cxx` that includes only SRL/SGL. This is the `saturn_cdfile` / `saturn_platform` pattern; follow it exactly.
- **Saturn build:** `saturn/compile.bat release` — run it from the repo root via cmd. Do NOT use `cd saturn && make`: the SH-2 toolchain lives at `SaturnRingLib/Compiler/sh2eb-elf/bin` and is not on `PATH`; `compile.bat` puts it there and sets `SRL_INSTALL_ROOT`. A bare `make` fails on the first SGL object with `sh2eb-elf-gcc.exe: command not found`, which looks like a missing toolchain but is not.
- **Host tests:** `sh saturn/tests/run_tests.sh` from the repo root. The suite needs `-DAUTO_DETECT_PLATFORM` on any translation unit that reaches `endian.h`, and must not `#include <cstdio>` — `intern.h` pulls in `saturn_compat.h`, which supplies its own `FILE` typedef and `printf` family and conflicts with the real header.
- **Save version:** `Serializer::CUR_VER` is `3` after Task 1. Slot files are named `AW_SAVE1`, `AW_SAVE2`, `AW_SAVE3`.
- **Device ids:** `SAT_BUP_INTERNAL = 1` (`BUP_MAIN_UNIT`), `SAT_BUP_CART = 2` (`BUP_CURTRIDGE`).
- **Slot count:** `SAVE_NUM_SLOTS = 3`.
- **Never offer to format a backup device.** Formatting destroys other games' saves.

---

## File Structure

| File | Responsibility | Task |
|---|---|---|
| `saturn/src/file.cxx` | add `memFile : File_impl` + `File::openMemory` | 1 |
| `saturn/src/file.h` | declare `File::openMemory` | 1 |
| `saturn/src/serializer.h` | `CUR_VER` 2 → 3 | 1 |
| `saturn/src/video.cxx` | pin the four page arrays to `maxVer = 2`; clear pages on ver-3 load | 1 |
| `saturn/tests/test_savefmt.cxx` | memory file + version gating tests | 1 |
| `saturn/tests/run_tests.sh` | per-suite build/run loop | 1 |
| `saturn/src/system/saturn_backup.h` | C interface: device probe, dir, read, write, delete, date | 2 |
| `saturn/src/system/saturn_backup.cxx` | SGL `BUP_*` + `SRL::Types::DateTime` implementation | 2 |
| `saturn/tests/stub_saturn_backup.cxx` | host stub with scriptable device/slot state | 2 |
| `saturn/tests/test_backup_date.cxx` | `sat_bup_date_split` tests | 2 |
| `saturn/src/savedata.h` | `SlotState`, `SlotInfo`, header layout, slot names, chapter names, device defaulting | 3 |
| `saturn/src/savedata.cxx` | implementation, no SGL and no menu code | 3 |
| `saturn/tests/test_savedata.cxx` | header round-trip, rejection cases, device defaulting matrix | 3 |
| `saturn/src/engine.h/.cxx` | `saveSlot` / `loadSlot` / `readSlotInfo`; drop quicksave hooks | 4 |
| `saturn/src/sys.h` | `PlayerInput` menu fields; drop `save`/`load`/`stateSlot` | 4, 5 |
| `saturn/src/system/saturn_platform.h/.cxx` | split A/B/C, add L/R | 5 |
| `saturn/src/system/saturn_system.cxx` | map the new pad bits into `PlayerInput` | 5 |
| `saturn/src/menu_state.h/.cxx` | pure screen state machine, zero I/O | 6 |
| `saturn/tests/test_menu_state.cxx` | transition tests | 6 |
| `saturn/src/menu_draw.h/.cxx` | fill, text, palette dim against a raw page + font pointer | 7 |
| `saturn/tests/test_menu_draw.cxx` | pixel and palette tests | 7 |
| `saturn/src/menu.h/.cxx` | screens, page ownership, engine glue | 8 |
| `saturn/src/vm.cxx` | delete the busy-wait pause at :642 | 8 |

`menu_state` is split from `menu` so the logic is host-testable with no engine dependency. `menu_draw` takes a font pointer rather than calling `Video::drawChar`, for the same reason.

---

### Task 1: Lean save format — memory file and version 3 gating

**Files:**
- Modify: `saturn/src/file.h:26-44`
- Modify: `saturn/src/file.cxx:32-41` (after `File_impl`), `saturn/src/file.cxx:170-208`
- Modify: `saturn/src/serializer.h:34-36`
- Modify: `saturn/src/video.cxx:606-624`
- Create: `saturn/tests/test_savefmt.cxx`
- Modify: `saturn/tests/run_tests.sh`

**Interfaces:**
- Consumes: nothing.
- Produces: `bool File::openMemory(void *buf, uint32_t size, bool write)` — installs a memory-backed impl on an existing `File`, returns false for a null buffer or zero size. `Serializer::CUR_VER == 3`.

- [ ] **Step 1: Rewrite the test runner to build suites separately**

Replace `saturn/tests/run_tests.sh` entirely:

```sh
#!/bin/sh
# Host unit tests. Nothing here touches hardware or SRL, which is the whole
# point: this is the logic that is cheap to get wrong and cheap to test
# off-target. Each suite is its own binary because their dependencies differ.
set -e
cd "$(dirname "$0")"

# Engine sources predate -Wextra and are compiled without -Werror; our own
# files are held to the stricter bar.
ENGINE_FLAGS="-std=c++11 -Wall -O1 -g"
OWN_FLAGS="-std=c++11 -Wall -Wextra -Werror -O1 -g"

echo "== scsp_voice =="
g++ $OWN_FLAGS -I../src/system \
    -o run_tests_scsp test_scsp_voice.cxx ../src/system/scsp_voice.cxx
./run_tests_scsp

echo "== savefmt =="
g++ $ENGINE_FLAGS -I../src -I../src/system \
    -o run_tests_savefmt test_savefmt.cxx \
    ../src/serializer.cxx ../src/file.cxx ../src/util.cxx
./run_tests_savefmt

echo "all suites passed"
```

- [ ] **Step 2: Write the failing test**

Create `saturn/tests/test_savefmt.cxx`:

```c++
/*----------------------
 | test_savefmt.cxx
 | Description: Host unit tests for the memory-backed File and for the
 |   serialiser version gating that drops video pages from a version 3 save.
 |   Built and run by run_tests.sh, never by the Saturn makefile -- that globs
 |   src/ only, so tests/ is excluded automatically.
 | Author: suinevere
 | Dependencies: file.h, serializer.h
 ----------------------*/
#include <cstdio>
#include <cstdint>
#include <cstring>
#include "file.h"
#include "serializer.h"

/* g_debugMask is NOT defined here on purpose: util.cxx:23 already defines it
   and is linked into this suite. Defining it again is a duplicate symbol. */

static int g_fail = 0;

#define CHECK_EQ(actual, expected)                                            \
    do {                                                                      \
        long long a_ = (long long)(actual);                                   \
        long long e_ = (long long)(expected);                                 \
        if (a_ != e_) {                                                       \
            g_fail++;                                                         \
            printf("FAIL %s:%d  %s\n  actual   = %lld\n  expected = %lld\n",  \
                   __FILE__, __LINE__, #actual, a_, e_);                      \
        }                                                                     \
    } while (0)

#define CHECK(cond)                                                           \
    do {                                                                      \
        if (!(cond)) {                                                        \
            g_fail++;                                                         \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);            \
        }                                                                     \
    } while (0)

static void test_memory_file_round_trip(void)
{
    uint8_t buf[64];
    memset(buf, 0, sizeof(buf));

    File w;
    CHECK(w.openMemory(buf, sizeof(buf), true));
    w.writeUint32BE(0x41575356);
    w.writeUint16BE(3);
    w.writeByte(0xAB);
    CHECK(!w.ioErr());

    CHECK_EQ(buf[0], 0x41);
    CHECK_EQ(buf[3], 0x56);
    CHECK_EQ(buf[5], 3);
    CHECK_EQ(buf[6], 0xAB);

    File r;
    CHECK(r.openMemory(buf, sizeof(buf), false));
    CHECK_EQ(r.readUint32BE(), 0x41575356);
    CHECK_EQ(r.readUint16BE(), 3);
    CHECK_EQ(r.readByte(), 0xAB);
    CHECK(!r.ioErr());
}

static void test_memory_file_overflow_sets_ioerr(void)
{
    uint8_t buf[4];
    File w;
    CHECK(w.openMemory(buf, sizeof(buf), true));
    w.writeUint32BE(0);
    CHECK(!w.ioErr());
    w.writeByte(1);
    CHECK(w.ioErr());
}

static void test_memory_file_rejects_bad_arguments(void)
{
    uint8_t buf[4];
    File a;
    CHECK(!a.openMemory(0, 4, true));
    File b;
    CHECK(!b.openMemory(buf, 0, true));
}

static void test_memory_file_seek(void)
{
    uint8_t buf[8];
    memset(buf, 0, sizeof(buf));
    File w;
    CHECK(w.openMemory(buf, sizeof(buf), true));
    w.seek(4);
    w.writeByte(0x7F);
    CHECK_EQ(buf[0], 0);
    CHECK_EQ(buf[4], 0x7F);
}

/* Mirrors Video::saveOrLoad: scalars carry the macro's CUR_VER, the page
   arrays are pinned to maxVer 2. */
static void buildVideoLikeEntries(Serializer::Entry *e, uint8_t *mask,
                                  uint8_t *pages, int pageSize)
{
    e[0].type = Serializer::SET_INT;
    e[0].size = Serializer::SES_INT8;
    e[0].n = 1;
    e[0].data = mask;
    e[0].minVer = 1;
    e[0].maxVer = Serializer::CUR_VER;

    e[1].type = Serializer::SET_ARRAY;
    e[1].size = Serializer::SES_INT8;
    e[1].n = pageSize;
    e[1].data = pages;
    e[1].minVer = 1;
    e[1].maxVer = 2;

    e[2].type = Serializer::SET_END;
    e[2].size = 0;
    e[2].n = 0;
    e[2].data = 0;
    e[2].minVer = 0;
    e[2].maxVer = 0;
}

static void test_cur_ver_is_three(void)
{
    CHECK_EQ(Serializer::CUR_VER, 3);
}

static void test_version_three_save_omits_pages(void)
{
    uint8_t buf[256];
    uint8_t mask = 0x1B;
    uint8_t pages[16];
    memset(pages, 0xEE, sizeof(pages));

    Serializer::Entry entries[3];
    buildVideoLikeEntries(entries, &mask, pages, sizeof(pages));

    File f;
    CHECK(f.openMemory(buf, sizeof(buf), true));
    Serializer s(&f, Serializer::SM_SAVE, 0);
    s.saveOrLoadEntries(entries);

    CHECK_EQ(s._bytesCount, 1);
}

static void test_version_three_load_leaves_pages_untouched(void)
{
    uint8_t buf[256];
    memset(buf, 0, sizeof(buf));
    buf[0] = 0x1B;

    uint8_t mask = 0;
    uint8_t pages[16];
    memset(pages, 0x11, sizeof(pages));

    Serializer::Entry entries[3];
    buildVideoLikeEntries(entries, &mask, pages, sizeof(pages));

    File f;
    CHECK(f.openMemory(buf, sizeof(buf), false));
    Serializer s(&f, Serializer::SM_LOAD, 0, 3);
    s.saveOrLoadEntries(entries);

    CHECK_EQ(mask, 0x1B);
    CHECK_EQ(pages[0], 0x11);
    CHECK_EQ(s._bytesCount, 1);
}

static void test_version_two_load_still_reads_pages(void)
{
    uint8_t buf[256];
    memset(buf, 0x5A, sizeof(buf));
    buf[0] = 0x1B;

    uint8_t mask = 0;
    uint8_t pages[16];
    memset(pages, 0, sizeof(pages));

    Serializer::Entry entries[3];
    buildVideoLikeEntries(entries, &mask, pages, sizeof(pages));

    File f;
    CHECK(f.openMemory(buf, sizeof(buf), false));
    Serializer s(&f, Serializer::SM_LOAD, 0, 2);
    s.saveOrLoadEntries(entries);

    CHECK_EQ(mask, 0x1B);
    CHECK_EQ(pages[0], 0x5A);
    CHECK_EQ(s._bytesCount, 17);
}

static void test_lean_state_fits_the_slot_budget(void)
{
    static uint8_t vmVariables[0x100 * 2];
    static uint8_t stackCalls[0x100 * 2];
    static uint8_t threadsData[0x40 * 2 * 2];
    static uint8_t channelActive[0x40 * 2];
    static uint8_t loadedList[64];
    uint8_t buf[4096];

    Serializer::Entry entries[] = {
        SE_ARRAY(vmVariables, 0x100, Serializer::SES_INT16, VER(1)),
        SE_ARRAY(stackCalls, 0x100, Serializer::SES_INT16, VER(1)),
        SE_ARRAY(threadsData, 0x40 * 2, Serializer::SES_INT16, VER(1)),
        SE_ARRAY(channelActive, 0x40 * 2, Serializer::SES_INT8, VER(1)),
        SE_ARRAY(loadedList, 64, Serializer::SES_INT8, VER(1)),
        SE_END()
    };

    File f;
    CHECK(f.openMemory(buf, sizeof(buf), true));
    Serializer s(&f, Serializer::SM_SAVE, 0);
    s.saveOrLoadEntries(entries);

    CHECK_EQ(s._bytesCount, 1472);
    CHECK(s._bytesCount + 48 < 2048);
}

int main(void)
{
    test_memory_file_round_trip();
    test_memory_file_overflow_sets_ioerr();
    test_memory_file_rejects_bad_arguments();
    test_memory_file_seek();
    test_cur_ver_is_three();
    test_version_three_save_omits_pages();
    test_version_three_load_leaves_pages_untouched();
    test_version_two_load_still_reads_pages();
    test_lean_state_fits_the_slot_budget();

    if (g_fail == 0) {
        printf("all tests passed\n");
        return 0;
    }
    printf("%d check(s) failed\n", g_fail);
    return 1;
}
```

- [ ] **Step 3: Run the tests to verify they fail**

Run: `sh saturn/tests/run_tests.sh`
Expected: FAIL — compile error, `'class File' has no member named 'openMemory'`.

- [ ] **Step 4: Declare `openMemory` in `file.h`**

Add after `bool open(...)` at `saturn/src/file.h:33`:

```c++
	/*----------------------
	 | openMemory
	 | Description: Points this File at a caller-owned buffer instead of a file,
	 |   so the serialiser can write a save into a staging buffer bound for
	 |   backup RAM. Replaces any impl already installed.
	 | Author: suinevere
	 | Params: buf -- the buffer; size -- its length; write -- true to write
	 | Returns: false for a null buffer or a zero size
	 ----------------------*/
	bool openMemory(void *buf, uint32_t size, bool write);
```

- [ ] **Step 5: Add the `memFile` impl in `file.cxx`**

Insert after the `File_impl` definition (ends `saturn/src/file.cxx:41`):

```c++
/*----------------------
 | memFile
 | Description: A File_impl over a caller-owned byte buffer. Reads and writes
 |   past the end set _ioErr rather than touching memory, which is what
 |   Engine's existing ioErr check already looks for.
 | Author: suinevere
 | Dependencies: N/A
 ----------------------*/
struct memFile : File_impl {
	uint8_t *_buf;
	uint32_t _size;
	uint32_t _pos;
	bool _write;

	memFile(uint8_t *buf, uint32_t size, bool write)
		: _buf(buf), _size(size), _pos(0), _write(write) {}

	bool open(const char *path, const char *mode) {
		(void)path;
		(void)mode;
		return true;
	}
	void close() {}
	void seek(int32_t off) {
		if (off >= 0 && (uint32_t)off <= _size) {
			_pos = (uint32_t)off;
		} else {
			_ioErr = true;
		}
	}
	void read(void *ptr, uint32_t size) {
		if (_pos + size > _size) {
			_ioErr = true;
			return;
		}
		memcpy(ptr, _buf + _pos, size);
		_pos += size;
	}
	void write(void *ptr, uint32_t size) {
		if (!_write || _pos + size > _size) {
			_ioErr = true;
			return;
		}
		memcpy(_buf + _pos, ptr, size);
		_pos += size;
	}
};
```

- [ ] **Step 6: Implement `File::openMemory`**

Insert after `File::open` (ends `saturn/src/file.cxx:208`):

```c++
/*----------------------
 | File::openMemory
 | Description: Swaps this File's impl for one backed by a caller-owned buffer.
 | Author: suinevere
 | Params: buf -- the buffer; size -- its length; write -- true to write
 | Returns: false for a null buffer or a zero size, leaving the old impl in place
 ----------------------*/
bool File::openMemory(void *buf, uint32_t size, bool write) {
	if (buf == 0 || size == 0) {
		return false;
	}
	_impl->close();
	delete _impl;
	_impl = new memFile((uint8_t *)buf, size, write);
	return true;
}
```

- [ ] **Step 7: Bump the save version**

In `saturn/src/serializer.h:34-36`, change `CUR_VER = 2` to:

```c++
	enum {
		CUR_VER = 3
	};
```

- [ ] **Step 8: Run the tests**

Run: `sh saturn/tests/run_tests.sh`
Expected: PASS — both suites, ending `all suites passed`.

- [ ] **Step 9: Pin the video page entries to version 2**

In `saturn/src/video.cxx:606-615`, replace the four `SE_ARRAY(_pages[n], ...)` lines with explicit entries carrying `maxVer = 2`. The `SE_ARRAY` macro cannot express this — it hardcodes `Serializer::CUR_VER` — so these four are written out:

```c++
	Serializer::Entry entries[] = {
		SE_INT(&currentPaletteId, Serializer::SES_INT8, VER(1)),
		SE_INT(&paletteIdRequested, Serializer::SES_INT8, VER(1)),
		SE_INT(&mask, Serializer::SES_INT8, VER(1)),
		{ Serializer::SET_ARRAY, Serializer::SES_INT8, Video::VID_PAGE_SIZE, _pages[0], 1, 2 },
		{ Serializer::SET_ARRAY, Serializer::SES_INT8, Video::VID_PAGE_SIZE, _pages[1], 1, 2 },
		{ Serializer::SET_ARRAY, Serializer::SES_INT8, Video::VID_PAGE_SIZE, _pages[2], 1, 2 },
		{ Serializer::SET_ARRAY, Serializer::SES_INT8, Video::VID_PAGE_SIZE, _pages[3], 1, 2 },
		SE_END()
	};
```

`Serializer::Entry::n` is `uint16_t` and `VID_PAGE_SIZE` is 32000, so it fits.

- [ ] **Step 10: Clear the pages on a version 3 load**

In `saturn/src/video.cxx`, in the `SM_LOAD` branch at :618, add the clear before the pointer fixup. The comment sits above the block, not inside the function body per `CLAUDE.md`, so express it as a one-line note attached to the existing function header instead — add this line to `Video::saveOrLoad`'s header block if it has one, otherwise write the code bare:

```c++
	if (ser._mode == Serializer::SM_LOAD) {
		if (ser._saveVer >= 3) {
			for (int i = 0; i < 4; ++i) {
				memset(_pages[i], 0, Video::VID_PAGE_SIZE);
			}
		}
		_curPagePtr1 = _pages[(mask >> 4) & 0x3];
		_curPagePtr2 = _pages[(mask >> 2) & 0x3];
		_curPagePtr3 = _pages[(mask >> 0) & 0x3];
		changePal(currentPaletteId);
	}
```

- [ ] **Step 11: Build for Saturn**

Run: `saturn/compile.bat release`
Expected: builds clean. `video.cxx` is not host-compilable, so this build plus the on-target check in Task 4 is its only verification — the entry-table mechanism itself is covered by Step 2's tests.

- [ ] **Step 12: Commit**

```bash
git add saturn/src/file.h saturn/src/file.cxx saturn/src/serializer.h saturn/src/video.cxx saturn/tests/test_savefmt.cxx saturn/tests/run_tests.sh
git commit -m "Drop video pages from the save with a version 3 format and add a memory-backed File"
```

---

### Task 2: Backup RAM wrapper

**Files:**
- Create: `saturn/src/system/saturn_backup.h`
- Create: `saturn/src/system/saturn_backup.cxx`
- Create: `saturn/tests/stub_saturn_backup.cxx`
- Create: `saturn/tests/test_backup_date.cxx`
- Modify: `saturn/tests/run_tests.sh`

**Interfaces:**
- Consumes: nothing.
- Produces: the full `sat_bup_*` C interface listed in Step 1, plus the host stub's control surface: `void stub_bup_reset(void)`, `void stub_bup_set_device(uint32_t device, int present, int formatted, int writeProtected, uint32_t freeBytes)`, `void stub_bup_add_file(uint32_t device, const char *name, const void *data, int32_t size, uint32_t date)`.

- [ ] **Step 1: Write the header**

Create `saturn/src/system/saturn_backup.h`:

```c
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
```

- [ ] **Step 2: Write the failing date test**

Create `saturn/tests/test_backup_date.cxx`:

```c++
/*----------------------
 | test_backup_date.cxx
 | Description: Host unit tests for sat_bup_date_split, the one piece of
 |   saturn_backup that is pure arithmetic and can run off-target. The rest of
 |   the file talks to the BIOS and is verified on hardware only.
 | Author: suinevere
 | Dependencies: saturn_backup.h
 ----------------------*/
#include <cstdio>
#include <cstdint>
#include "saturn_backup.h"

static int g_fail = 0;

#define CHECK_EQ(actual, expected)                                            \
    do {                                                                      \
        long long a_ = (long long)(actual);                                   \
        long long e_ = (long long)(expected);                                 \
        if (a_ != e_) {                                                       \
            g_fail++;                                                         \
            printf("FAIL %s:%d  %s\n  actual   = %lld\n  expected = %lld\n",  \
                   __FILE__, __LINE__, #actual, a_, e_);                      \
        }                                                                     \
    } while (0)

/* BUP packs a date as minutes since 1980-01-01 00:00. */
static uint32_t packed(int year, int month, int day, int hour, int min)
{
    static const int cum[12] = {0,31,59,90,120,151,181,212,243,273,304,334};
    int y = year - 1980;
    int days = y * 365 + (y + 3) / 4 + cum[month - 1] + (day - 1);
    if (month > 2 && (year % 4) == 0) {
        days += 1;
    }
    return (uint32_t)days * 1440u + (uint32_t)hour * 60u + (uint32_t)min;
}

static void test_epoch(void)
{
    int mo = -1, d = -1, h = -1, mi = -1;
    sat_bup_date_split(0, &mo, &d, &h, &mi);
    CHECK_EQ(mo, 1);
    CHECK_EQ(d, 1);
    CHECK_EQ(h, 0);
    CHECK_EQ(mi, 0);
}

static void test_known_stamp(void)
{
    int mo = 0, d = 0, h = 0, mi = 0;
    sat_bup_date_split(packed(2026, 8, 1, 21, 14), &mo, &d, &h, &mi);
    CHECK_EQ(mo, 8);
    CHECK_EQ(d, 1);
    CHECK_EQ(h, 21);
    CHECK_EQ(mi, 14);
}

static void test_leap_day(void)
{
    int mo = 0, d = 0, h = 0, mi = 0;
    sat_bup_date_split(packed(2024, 2, 29, 12, 0), &mo, &d, &h, &mi);
    CHECK_EQ(mo, 2);
    CHECK_EQ(d, 29);
    CHECK_EQ(h, 12);
}

static void test_null_outputs_are_safe(void)
{
    sat_bup_date_split(packed(2026, 8, 1, 21, 14), 0, 0, 0, 0);
}

int main(void)
{
    test_epoch();
    test_known_stamp();
    test_leap_day();
    test_null_outputs_are_safe();

    if (g_fail == 0) {
        printf("all tests passed\n");
        return 0;
    }
    printf("%d check(s) failed\n", g_fail);
    return 1;
}
```

- [ ] **Step 3: Write the host stub**

Create `saturn/tests/stub_saturn_backup.cxx`. It implements the whole interface in memory so later suites can drive it, and implements `sat_bup_date_split` for real so this suite exercises the shipping arithmetic:

```c++
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

void stub_bup_reset(void)
{
    memset(s_dev, 0, sizeof(s_dev));
    memset(s_files, 0, sizeof(s_files));
    s_dev[SAT_BUP_INTERNAL].present = 1;
    s_dev[SAT_BUP_INTERNAL].formatted = 1;
    s_dev[SAT_BUP_INTERNAL].freeBytes = 29000;
}

void stub_bup_set_device(uint32_t device, int present, int formatted,
                         int writeProtected, uint32_t freeBytes)
{
    s_dev[device].present = present;
    s_dev[device].formatted = formatted;
    s_dev[device].writeProtected = writeProtected;
    s_dev[device].freeBytes = freeBytes;
}

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

void sat_bup_init(void) {}

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

int sat_bup_read(uint32_t device, const char *name, void *dst, int32_t size)
{
    StubFile *f = stub_find(device, name);
    if (!f) {
        return SAT_BUP_ERR_NOT_FOUND;
    }
    memcpy(dst, f->data, f->size < size ? f->size : size);
    return SAT_BUP_OK;
}

int sat_bup_write(uint32_t device, const char *name, const char *comment,
                  const void *src, int32_t size, int overwrite)
{
    (void)comment;
    if (!s_dev[device].present) return SAT_BUP_ERR_NONE;
    if (!s_dev[device].formatted) return SAT_BUP_ERR_UNFORMAT;
    if (s_dev[device].writeProtected) return SAT_BUP_ERR_PROTECTED;
    if ((uint32_t)size > s_dev[device].freeBytes) return SAT_BUP_ERR_NO_SPACE;

    StubFile *f = stub_find(device, name);
    if (f && !overwrite) {
        return SAT_BUP_ERR_NO_SPACE;
    }
    if (f) {
        memcpy(f->data, src, size);
        f->size = size;
        return SAT_BUP_OK;
    }
    stub_bup_add_file(device, name, src, size, 0);
    return SAT_BUP_OK;
}

int sat_bup_delete(uint32_t device, const char *name)
{
    StubFile *f = stub_find(device, name);
    if (!f) {
        return SAT_BUP_ERR_NOT_FOUND;
    }
    f->used = 0;
    return SAT_BUP_OK;
}

uint32_t sat_bup_date_now(void) { return 0; }

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
```

- [ ] **Step 4: Add the suite to the runner**

Insert into `saturn/tests/run_tests.sh` before the final `echo`:

```sh
echo "== backup date =="
g++ $OWN_FLAGS -I../src/system \
    -o run_tests_bupdate test_backup_date.cxx stub_saturn_backup.cxx
./run_tests_bupdate
```

- [ ] **Step 5: Run the tests**

Run: `sh saturn/tests/run_tests.sh`
Expected: PASS. If `test_leap_day` fails, the year-loop and month-loop leap handling disagree — both use `(year % 4) == 0`, which is correct for 1980–2099, the only range the Saturn RTC covers.

- [ ] **Step 6: Implement the real wrapper**

Create `saturn/src/system/saturn_backup.cxx`. It includes only SRL and SGL, never engine headers. `sat_bup_date_split` is the same implementation as the stub's — copy it verbatim so both agree.

```c++
/*----------------------
 | saturn_backup.cxx
 | Description: The Saturn side of saturn_backup.h, over SGL's BUP vector table
 |   and SRL's RTC. The only file in the port that includes sega_bup.h.
 | Author: suinevere
 | Dependencies: srl.hpp, sega_bup.h, saturn_backup.h
 | Globals: s_bupWork, s_bupCfg
 ----------------------*/
#include <srl.hpp>
extern "C" {
#include "sega_bup.h"
}
#include "saturn_backup.h"

/*----------------------
 | s_bupWork
 | Description: Work area the BIOS backup library needs. Sized for the largest
 |   directory the port can create. Deliberately a static in High Work RAM
 |   rather than a Low Work RAM allocation: the engine's 600 KB resource block
 |   lives low, and the two must not overlap.
 | Author: suinevere
 ----------------------*/
static uint32_t s_bupWork[0x2000 / 4];

/*----------------------
 | s_bupCfg
 | Description: BUP_Init fills one config per device. Kept alive for the
 |   lifetime of the program because the library retains the pointer.
 | Author: suinevere
 ----------------------*/
static BupConfig s_bupCfg[3];

static int sat_bup_map_error(int32_t rc)
{
    switch (rc) {
    case BUP_NON:                  return SAT_BUP_ERR_NONE;
    case BUP_UNFORMAT:             return SAT_BUP_ERR_UNFORMAT;
    case BUP_WRITE_PROTECT:        return SAT_BUP_ERR_PROTECTED;
    case BUP_NOT_ENOUGH_MEMORY:    return SAT_BUP_ERR_NO_SPACE;
    case BUP_NOT_FOUND:            return SAT_BUP_ERR_NOT_FOUND;
    case BUP_BROKEN:               return SAT_BUP_ERR_BROKEN;
    default:                       return SAT_BUP_OK;
    }
}

extern "C" void sat_bup_init(void)
{
    BUP_Init((uint32_t *)BUP_LIB_ADDRESS, s_bupWork, s_bupCfg);
}
```

The remaining entry points, each with its own `CLAUDE.md` header block:

```c++
extern "C" int sat_bup_probe(uint32_t device, SatBupDev *out)
{
    BupStat st;
    memset(out, 0, sizeof(*out));

    int32_t rc = BUP_Stat(device, SAVE_MAX_BYTES, &st);
    if (rc == BUP_NON) {
        return SAT_BUP_ERR_NONE;
    }
    out->present = 1;
    if (rc == BUP_UNFORMAT) {
        return SAT_BUP_ERR_UNFORMAT;
    }
    out->formatted = 1;
    if (rc == BUP_WRITE_PROTECT) {
        out->writeProtected = 1;
        return SAT_BUP_ERR_PROTECTED;
    }
    out->freeBytes = st.freebyte;
    return SAT_BUP_OK;
}

extern "C" int sat_bup_dir(uint32_t device, const char *name, SatBupEntry *out)
{
    BupDir dir;
    memset(out, 0, sizeof(*out));
    memset(&dir, 0, sizeof(dir));

    int32_t rc = BUP_Dir(device, (uint8_t *)name, 1, &dir);
    if (rc <= 0) {
        return SAT_BUP_OK;
    }
    out->exists = 1;
    out->size = dir.datasize;
    out->date = dir.date;
    return SAT_BUP_OK;
}

extern "C" int sat_bup_read(uint32_t device, const char *name, void *dst,
                            int32_t size)
{
    (void)size;
    int32_t rc = BUP_Read(device, (uint8_t *)name, (uint8_t *)dst);
    return sat_bup_map_error(rc);
}

extern "C" int sat_bup_write(uint32_t device, const char *name,
                             const char *comment, const void *src,
                             int32_t size, int overwrite)
{
    BupDir dir;
    memset(&dir, 0, sizeof(dir));
    strncpy((char *)dir.filename, name, 11);
    strncpy((char *)dir.comment, comment, 10);
    dir.language = BUP_ENGLISH;
    dir.date = sat_bup_date_now();
    dir.datasize = (uint32_t)size;
    dir.blocksize = 0;

    int32_t rc = BUP_Write(device, &dir, (uint8_t *)src,
                           overwrite ? 1 : 0);
    return sat_bup_map_error(rc);
}

extern "C" int sat_bup_delete(uint32_t device, const char *name)
{
    return sat_bup_map_error(BUP_Delete(device, (uint8_t *)name));
}

extern "C" uint32_t sat_bup_date_now(void)
{
    SRL::Types::DateTime now = SRL::Types::DateTime::Now();
    BupDate d;
    d.year  = (uint8_t)(now.Year - 1980);
    d.month = (uint8_t)now.Month;
    d.day   = (uint8_t)now.Day;
    d.time  = (uint8_t)now.Hour;
    d.min   = (uint8_t)now.Minute;
    d.week  = 0;
    return BUP_SetDate(&d);
}
```

`sat_bup_date_split` is copied verbatim from `stub_saturn_backup.cxx` so the two agree — the stub's copy is the one the host tests prove correct.

Three things here must be checked against the headers rather than trusted from this plan, because the SGL definitions vary:

- `BUP_Init`'s first argument type differs between the `volatile` and non-`volatile` branches at `sega_bup.h:98-101`. Take the form from whichever branch is active.
- `BupStat`'s free-space field name (`freebyte` above) and `BupDir`'s field names (`filename`, `comment`, `language`, `date`, `datasize`, `blocksize`) come from `sega_bup.h:53-90`. Read that block and correct the names if they differ.
- `SRL::Types::DateTime`'s member names (`Year`, `Month`, `Day`, `Hour`, `Minute`) come from `SaturnRingLib/saturnringlib/srl_datetime.hpp`. Read it and correct if they differ. Note `BupDate::year` is an offset from 1980, not a full year.

`SAVE_MAX_BYTES` is used as the `BUP_Stat` datasize probe, so `saturn_backup.cxx` needs that constant. Define it locally as `2048` rather than including `savedata.h` — this layer must not depend on the layer above it.

- [ ] **Step 7: Build for Saturn**

Run: `saturn/compile.bat release`
Expected: builds clean. If `BUP_Init`'s first argument type mismatches, take the form from the active `#if` branch in `sega_bup.h:98-101`.

- [ ] **Step 8: Verify the work buffer does not collide**

This is the spec's first Known Risk, and the risk as originally worded had the two RAM banks backwards. `saturn_compat.cxx:23-24` is authoritative: `0x06000000` is **High** Work RAM, `0x00200000` is **Low**.

Measured: `s_bupWork` links to `0x06022484`–`0x06024484` and `s_bupCfg` to `0x06022478`, both `.bss` statics in High Work RAM. The 600 KB resource block is not a static — `Resource::allocMemBlock` takes it from `sat_malloc_low` (`saturn_compat.cxx:82-85`), which routes to `SRL::Memory::LowWorkRam`. Different physical 1 MB banks, so no overlap is possible at any offset. Resolved; no action needed.

Record the finding in the commit or in the task report — a later task depends on this being settled.

- [ ] **Step 9: Commit**

```bash
git add saturn/src/system/saturn_backup.h saturn/src/system/saturn_backup.cxx saturn/tests/stub_saturn_backup.cxx saturn/tests/test_backup_date.cxx saturn/tests/run_tests.sh
git commit -m "Add a backup RAM wrapper over SGL BUP with device probing and date stamps"
```

---

### Task 3: Save slot metadata

**Files:**
- Create: `saturn/src/savedata.h`, `saturn/src/savedata.cxx`
- Create: `saturn/tests/test_savedata.cxx`
- Modify: `saturn/tests/run_tests.sh`

**Interfaces:**
- Consumes: `saturn_backup.h` from Task 2.
- Produces:
  - `enum { SAVE_NUM_SLOTS = 3, SAVE_HEADER_SIZE = 48, SAVE_MAX_BYTES = 2048 }`
  - `enum SlotState { SLOT_EMPTY, SLOT_OK, SLOT_DAMAGED, SLOT_OLD_VERSION }`
  - `struct SlotInfo { SlotState state; uint16_t partId; uint32_t date; }`
  - `void savedataSlotName(int slot, char *out)` — writes `AW_SAVE1`..`AW_SAVE3`, `out` must hold 12 bytes
  - `void savedataWriteHeader(uint8_t *buf, uint16_t partId, uint32_t date)`
  - `bool savedataReadHeader(const uint8_t *buf, uint16_t *ver, uint16_t *partId, uint32_t *date)` — false on magic mismatch
  - `SlotState savedataProbe(uint32_t device, int slot, SlotInfo *out)`
  - `uint32_t savedataPickDefaultDevice(const SatBupDev *internal, const SatBupDev *cart, int internalHasSaves, int cartHasSaves)`
  - `const char *savedataChapterName(uint16_t partId)`

- [ ] **Step 1: Write the failing test**

Create `saturn/tests/test_savedata.cxx`:

```c++
/*----------------------
 | test_savedata.cxx
 | Description: Host unit tests for savedata.cxx: header packing, slot probing
 |   against the backup stub, and backup device defaulting.
 | Author: suinevere
 | Dependencies: savedata.h, saturn_backup.h
 ----------------------*/
#include <cstdio>
#include <cstdint>
#include <cstring>
#include "savedata.h"
#include "saturn_backup.h"

extern void stub_bup_reset(void);
extern void stub_bup_set_device(uint32_t device, int present, int formatted,
                                int writeProtected, uint32_t freeBytes);
extern void stub_bup_add_file(uint32_t device, const char *name,
                              const void *data, int32_t size, uint32_t date);

static int g_fail = 0;

#define CHECK_EQ(actual, expected)                                            \
    do {                                                                      \
        long long a_ = (long long)(actual);                                   \
        long long e_ = (long long)(expected);                                 \
        if (a_ != e_) {                                                       \
            g_fail++;                                                         \
            printf("FAIL %s:%d  %s\n  actual   = %lld\n  expected = %lld\n",  \
                   __FILE__, __LINE__, #actual, a_, e_);                      \
        }                                                                     \
    } while (0)

#define CHECK(cond)                                                           \
    do {                                                                      \
        if (!(cond)) {                                                        \
            g_fail++;                                                         \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);            \
        }                                                                     \
    } while (0)

static void test_slot_names(void)
{
    char n[12];
    savedataSlotName(0, n);
    CHECK(strcmp(n, "AW_SAVE1") == 0);
    savedataSlotName(2, n);
    CHECK(strcmp(n, "AW_SAVE3") == 0);
}

static void test_header_round_trip(void)
{
    uint8_t buf[SAVE_HEADER_SIZE];
    memset(buf, 0, sizeof(buf));
    savedataWriteHeader(buf, 0x3E83, 0x00112233);

    uint16_t ver = 0, part = 0;
    uint32_t date = 0;
    CHECK(savedataReadHeader(buf, &ver, &part, &date));
    CHECK_EQ(ver, 3);
    CHECK_EQ(part, 0x3E83);
    CHECK_EQ(date, 0x00112233);
}

static void test_header_rejects_bad_magic(void)
{
    uint8_t buf[SAVE_HEADER_SIZE];
    memset(buf, 0, sizeof(buf));
    savedataWriteHeader(buf, 0x3E83, 0);
    buf[1] = 'X';

    uint16_t ver = 0, part = 0;
    uint32_t date = 0;
    CHECK(!savedataReadHeader(buf, &ver, &part, &date));
}

static void test_probe_empty_slot(void)
{
    stub_bup_reset();
    SlotInfo info;
    CHECK_EQ(savedataProbe(SAT_BUP_INTERNAL, 0, &info), SLOT_EMPTY);
    CHECK_EQ(info.state, SLOT_EMPTY);
}

static void test_probe_good_slot(void)
{
    stub_bup_reset();
    uint8_t blob[SAVE_HEADER_SIZE];
    memset(blob, 0, sizeof(blob));
    savedataWriteHeader(blob, 0x3E83, 4242);
    stub_bup_add_file(SAT_BUP_INTERNAL, "AW_SAVE1", blob, sizeof(blob), 4242);

    SlotInfo info;
    CHECK_EQ(savedataProbe(SAT_BUP_INTERNAL, 0, &info), SLOT_OK);
    CHECK_EQ(info.partId, 0x3E83);
    CHECK_EQ(info.date, 4242);
}

static void test_probe_damaged_slot(void)
{
    stub_bup_reset();
    uint8_t blob[SAVE_HEADER_SIZE];
    memset(blob, 0xFF, sizeof(blob));
    stub_bup_add_file(SAT_BUP_INTERNAL, "AW_SAVE1", blob, sizeof(blob), 0);

    SlotInfo info;
    CHECK_EQ(savedataProbe(SAT_BUP_INTERNAL, 0, &info), SLOT_DAMAGED);
}

static void test_probe_old_version(void)
{
    stub_bup_reset();
    uint8_t blob[SAVE_HEADER_SIZE];
    memset(blob, 0, sizeof(blob));
    savedataWriteHeader(blob, 0x3E83, 0);
    blob[5] = 2;
    stub_bup_add_file(SAT_BUP_INTERNAL, "AW_SAVE1", blob, sizeof(blob), 0);

    SlotInfo info;
    CHECK_EQ(savedataProbe(SAT_BUP_INTERNAL, 0, &info), SLOT_OLD_VERSION);
}

static void test_default_device_no_cart(void)
{
    SatBupDev internal = {1, 1, 0, 29000};
    SatBupDev cart = {0, 0, 0, 0};
    CHECK_EQ(savedataPickDefaultDevice(&internal, &cart, 0, 0), SAT_BUP_INTERNAL);
}

static void test_default_device_cart_has_saves(void)
{
    SatBupDev internal = {1, 1, 0, 29000};
    SatBupDev cart = {1, 1, 0, 480000};
    CHECK_EQ(savedataPickDefaultDevice(&internal, &cart, 0, 1), SAT_BUP_CART);
}

static void test_default_device_internal_has_saves(void)
{
    SatBupDev internal = {1, 1, 0, 29000};
    SatBupDev cart = {1, 1, 0, 480000};
    CHECK_EQ(savedataPickDefaultDevice(&internal, &cart, 1, 0), SAT_BUP_INTERNAL);
}

static void test_default_device_both_have_saves(void)
{
    SatBupDev internal = {1, 1, 0, 29000};
    SatBupDev cart = {1, 1, 0, 480000};
    CHECK_EQ(savedataPickDefaultDevice(&internal, &cart, 1, 1), SAT_BUP_INTERNAL);
}

static void test_default_device_cart_present_but_empty(void)
{
    SatBupDev internal = {1, 1, 0, 29000};
    SatBupDev cart = {1, 1, 0, 480000};
    CHECK_EQ(savedataPickDefaultDevice(&internal, &cart, 0, 0), SAT_BUP_INTERNAL);
}

static void test_chapter_names(void)
{
    CHECK(savedataChapterName(0x3E81) != 0);
    CHECK(savedataChapterName(0x3E89) != 0);
    CHECK(strcmp(savedataChapterName(0x1234), "UNKNOWN") == 0);
}

int main(void)
{
    test_slot_names();
    test_header_round_trip();
    test_header_rejects_bad_magic();
    test_probe_empty_slot();
    test_probe_good_slot();
    test_probe_damaged_slot();
    test_probe_old_version();
    test_default_device_no_cart();
    test_default_device_cart_has_saves();
    test_default_device_internal_has_saves();
    test_default_device_both_have_saves();
    test_default_device_cart_present_but_empty();
    test_chapter_names();

    if (g_fail == 0) {
        printf("all tests passed\n");
        return 0;
    }
    printf("%d check(s) failed\n", g_fail);
    return 1;
}
```

- [ ] **Step 2: Add the suite to the runner**

Insert into `saturn/tests/run_tests.sh` before the final `echo`:

```sh
echo "== savedata =="
g++ $OWN_FLAGS -I../src -I../src/system \
    -o run_tests_savedata test_savedata.cxx stub_saturn_backup.cxx \
    ../src/savedata.cxx
./run_tests_savedata
```

- [ ] **Step 3: Run to verify it fails**

Run: `sh saturn/tests/run_tests.sh`
Expected: FAIL — `savedata.h: No such file or directory`.

- [ ] **Step 4: Write `savedata.h`**

Create `saturn/src/savedata.h` declaring exactly the interface listed in the Interfaces block above, each item with a `CLAUDE.md` header block. The header layout constants:

```c++
enum {
	SAVE_NUM_SLOTS   = 3,
	SAVE_HEADER_SIZE = 48,
	SAVE_MAX_BYTES   = 2048
};
```

Field offsets, matching the spec:

```
0   4   'AWSV'
4   2   version
6   2   reserved
8   32  description
40  2   part id
42  4   date
46  2   reserved
```

`savedata.h` must not include `srl.hpp` or `sega_bup.h`. It includes `saturn_backup.h` for `SatBupDev`, and `intern.h` for the integer types.

- [ ] **Step 5: Write `savedata.cxx`**

Implement all declared functions. Key behaviours the tests pin down:

- `savedataWriteHeader` writes big-endian, magic `'AWSV'`, version `Serializer::CUR_VER`, and zero-fills the description.
- `savedataReadHeader` returns false on magic mismatch and does not touch the outputs.
- `savedataProbe` calls `sat_bup_dir`; absent → `SLOT_EMPTY`. Otherwise it reads the **whole slot** into a `SAVE_MAX_BYTES` static buffer and parses the header out of it; read error or magic mismatch → `SLOT_DAMAGED`; version < 3 → `SLOT_OLD_VERSION`; otherwise `SLOT_OK` with `partId` and `date` filled.

  It must NOT read only `SAVE_HEADER_SIZE` bytes. `sat_bup_read` refuses when the stored `datasize` exceeds the destination capacity — a deliberate bound added in Task 2, because `BUP_Read` itself has no capacity parameter and would otherwise overrun. A real slot is ~1580 bytes, so a 48-byte read would be refused and every valid save would probe as `SLOT_DAMAGED`. The buffer is `static`, not a local: a 2 KB stack frame is too much for the SH-2's stack.
- `savedataPickDefaultDevice` returns `SAT_BUP_CART` only when the cart is present, formatted, has saves, and internal does not. Every other case returns `SAT_BUP_INTERNAL`.
- `savedataChapterName` covers `GAME_PART2` (`0x3E81`) through `GAME_PART10` (`0x3E89`) and returns `"UNKNOWN"` for anything else. Names must fit the slot row: at most 14 characters. Suggested set, adjust to taste: `INTRO`, `THE ARRIVAL`, `THE JAIL`, `THE ESCAPE`, `THE CAVERNS`, `THE BATHS`, `THE CITY`, `THE ARENA`, `THE FINAL`.

- [ ] **Step 6: Run the tests**

Run: `sh saturn/tests/run_tests.sh`
Expected: PASS — all four suites.

- [ ] **Step 7: Build for Saturn**

Run: `saturn/compile.bat release`
Expected: builds clean. `savedata.cxx` is picked up automatically by the `find src/` glob.

- [ ] **Step 8: Commit**

```bash
git add saturn/src/savedata.h saturn/src/savedata.cxx saturn/tests/test_savedata.cxx saturn/tests/run_tests.sh
git commit -m "Add save slot metadata with header packing, slot probing and device defaulting"
```

---

### Task 4: Engine save and load through backup RAM

**Files:**
- Modify: `saturn/src/engine.h:31-56`
- Modify: `saturn/src/engine.cxx:97-182`
- Modify: `saturn/src/sys.h:36-44`

**Interfaces:**
- Consumes: `File::openMemory` (Task 1), `savedata.h` (Task 3), `saturn_backup.h` (Task 2).
- Produces:
  - `bool Engine::saveSlot(uint32_t device, int slot)` — returns false on any backup error
  - `bool Engine::loadSlot(uint32_t device, int slot)`
  - `int Engine::lastSaveError() const { return _lastSaveError; }` — one of the `SAT_BUP_ERR_*` codes, for the menu's status line. Backed by a new `int _lastSaveError;` member on `Engine`, initialised to `SAT_BUP_OK` in the constructor's init list alongside the existing members at `engine.cxx:26-27`.
  - `void Engine::startNewGame()` — `vm.initForPart(GAME_PART2)`

- [ ] **Step 1: Replace the quicksave hooks**

Delete `Engine::processInput` (`saturn/src/engine.cxx:97-114`) entirely, and its declaration at `engine.h:51`. Remove the `processInput()` call from `Engine::run` (`engine.cxx:38`). Remove `_stateSlot` from `engine.h:43` and its initialiser at `engine.cxx:27`. Remove `MAX_SAVE_SLOTS` from `engine.h:32-34`.

In `saturn/src/sys.h:42-43`, delete `bool save, load;` and `int8_t stateSlot;`.

- [ ] **Step 2: Build to confirm nothing else referenced them**

Run: `saturn/compile.bat release`
Expected: builds clean. If a reference remains, it is in `saturn/host/sysImplementation.cxx`, which is not in the build — leave it alone.

- [ ] **Step 3: Replace `makeGameStateName` / `saveGameState` / `loadGameState`**

Rewrite `engine.cxx:116-182` as `saveSlot` and `loadSlot` against a staging buffer. The serialisation calls stay in the same order — that order is the save format.

```c++
/*----------------------
 | Engine::saveSlot
 | Description: Serialises the engine into a staging buffer and writes it to a
 |   backup RAM slot. The order of the saveOrLoad calls is the save format;
 |   do not reorder them.
 | Author: suinevere
 | Dependencies: savedata.h, saturn_backup.h, serializer.h
 | Globals: N/A
 | Params: device -- SAT_BUP_INTERNAL or SAT_BUP_CART; slot -- 0 to 2
 | Returns: false on a backup error, with lastSaveError set
 ----------------------*/
bool Engine::saveSlot(uint32_t device, int slot) {
	static uint8_t buf[SAVE_MAX_BYTES];
	memset(buf, 0, sizeof(buf));

	const uint32_t date = sat_bup_date_now();
	savedataWriteHeader(buf, res.currentPartId, date);

	File f;
	f.openMemory(buf + SAVE_HEADER_SIZE, sizeof(buf) - SAVE_HEADER_SIZE, true);
	Serializer s(&f, Serializer::SM_SAVE, res._memPtrStart);
	vm.saveOrLoad(s);
	res.saveOrLoad(s);
	video.saveOrLoad(s);
	player.saveOrLoad(s);
	mixer.saveOrLoad(s);

	if (f.ioErr()) {
		_lastSaveError = SAT_BUP_ERR_NO_SPACE;
		return false;
	}

	char name[12];
	savedataSlotName(slot, name);
	const int32_t total = (int32_t)(SAVE_HEADER_SIZE + s._bytesCount);
	_lastSaveError = sat_bup_write(device, name, "ANOTHERWLD", buf, total, 1);
	return _lastSaveError == SAT_BUP_OK;
}
```

`loadSlot` is the mirror: `sat_bup_read` into the same static buffer, `savedataReadHeader` to get the version, refuse anything but 3, then `openMemory` read-only at `SAVE_HEADER_SIZE` and run the same five `saveOrLoad` calls with `Serializer::SM_LOAD` and that version. Keep the `player.stop()` / `mixer.stopAll()` mute that `loadGameState` did first (`engine.cxx:161-162`).

The static buffer is deliberate: `SAVE_MAX_BYTES` on the stack would be a 2 KB frame, and this runs on the SH-2's modest stack.

- [ ] **Step 4: Add `startNewGame` and move part selection out of `init`**

In `Engine::init` (`engine.cxx:53-89`), delete the `vm.initForPart(part)` call and the `BYPASS_PROTECTION` block around it — starting a part is now the menu's decision. Add:

```c++
/*----------------------
 | Engine::startNewGame
 | Description: Begins a fresh run at the intro. BYPASS_PROTECTION selects the
 |   intro over the copy-protection wheel, which is unplayable without the
 |   physical code wheel.
 | Author: suinevere
 | Dependencies: parts.h
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void Engine::startNewGame() {
#ifdef BYPASS_PROTECTION
	vm.initForPart(GAME_PART2);
#else
	vm.initForPart(GAME_PART1);
#endif
}
```

Also add `sat_bup_init()` to `Engine::init`, after `sys->init` and before anything reads a slot. Guard it with `#ifdef __sh__` alongside the existing include convention in `main.cxx:22`.

Leave `Engine::run` alone for now — Task 8 rewrites it. To keep the build runnable between tasks, call `startNewGame()` at the end of `init`; Task 8 removes that call.

- [ ] **Step 5: Build and run on target**

Run: `saturn/compile.bat release`
Then boot `saturn/BuildDrop/` in Mednafen (`saturn/run_with_mednafen.bat`).
Expected: the game boots into the intro exactly as before. Nothing visible has changed yet — this step is confirming the refactor is inert.

- [ ] **Step 6: Commit**

```bash
git add saturn/src/engine.h saturn/src/engine.cxx saturn/src/sys.h
git commit -m "Move engine save and load onto backup RAM slots and drop the quicksave hooks"
```

---

### Task 5: Split the pad buttons

**Files:**
- Modify: `saturn/src/system/saturn_platform.h:66-71`
- Modify: `saturn/src/system/saturn_platform.cxx:220-238`
- Modify: `saturn/src/system/saturn_system.cxx:72-88`
- Modify: `saturn/src/sys.h:28-44`

**Interfaces:**
- Consumes: nothing.
- Produces: `SAT_PAD_A`, `SAT_PAD_B`, `SAT_PAD_C`, `SAT_PAD_L`, `SAT_PAD_R` bits from `sat_input_read`; `PlayerInput` fields `bool menuConfirm, menuCancel, menuLeft, menuRight`.

- [ ] **Step 1: Add the pad bits**

In `saturn/src/system/saturn_platform.h`, after the existing `SAT_PAD_PAUSE` at :71:

```c
/*----------------------
 | SAT_PAD_A / _B / _C / _L / _R
 | Description: Individual buttons, for menus that must tell confirm from
 |   cancel. SAT_PAD_ACTION stays the union of A, B and C so gameplay input is
 |   unchanged.
 | Author: suinevere
 ----------------------*/
#define SAT_PAD_A      (1u << 6)
#define SAT_PAD_B      (1u << 7)
#define SAT_PAD_C      (1u << 8)
#define SAT_PAD_L      (1u << 9)
#define SAT_PAD_R      (1u << 10)
```

- [ ] **Step 2: Read them**

In `saturn/src/system/saturn_platform.cxx`, replace the merged A/B/C block at :231-233:

```c++
        if (port0.IsHeld(SRL::Input::Digital::Button::A)) bits |= SAT_PAD_A;
        if (port0.IsHeld(SRL::Input::Digital::Button::B)) bits |= SAT_PAD_B;
        if (port0.IsHeld(SRL::Input::Digital::Button::C)) bits |= SAT_PAD_C;
        if (bits & (SAT_PAD_A | SAT_PAD_B | SAT_PAD_C)) bits |= SAT_PAD_ACTION;
        if (port0.IsHeld(SRL::Input::Digital::Button::L)) bits |= SAT_PAD_L;
        if (port0.IsHeld(SRL::Input::Digital::Button::R)) bits |= SAT_PAD_R;
```

- [ ] **Step 3: Add the `PlayerInput` fields**

In `saturn/src/sys.h`, inside `struct PlayerInput` after `bool pause;`:

```c++
	bool menuConfirm, menuCancel;
	bool menuLeft, menuRight;
```

- [ ] **Step 4: Map them**

In `saturn/src/system/saturn_system.cxx:72-88`, inside `processEvents`:

```c++
	input.menuConfirm = (pad & (SAT_PAD_A | SAT_PAD_C)) != 0;
	input.menuCancel  = (pad & SAT_PAD_B) != 0;
	input.menuLeft    = (pad & SAT_PAD_L) != 0;
	input.menuRight   = (pad & SAT_PAD_R) != 0;
```

- [ ] **Step 5: Build and check gameplay is unchanged**

Run: `saturn/compile.bat release`, then boot in Mednafen.
Expected: the intro plays and run/shoot still respond to A, B and C. `SAT_PAD_ACTION` is still the union, so nothing in `vm.cxx` sees a difference.

- [ ] **Step 6: Commit**

```bash
git add saturn/src/system/saturn_platform.h saturn/src/system/saturn_platform.cxx saturn/src/system/saturn_system.cxx saturn/src/sys.h
git commit -m "Split the pad A, B and C buttons and add the shoulders for menu navigation"
```

---

### Task 6: Menu state machine

**Files:**
- Create: `saturn/src/menu_state.h`, `saturn/src/menu_state.cxx`
- Create: `saturn/tests/test_menu_state.cxx`
- Modify: `saturn/tests/run_tests.sh`

This file is pure logic: no drawing, no backup calls, no engine references. That is what makes it testable, and it is the reason `menu_state` is separate from `menu`.

**Interfaces:**
- Consumes: `savedata.h` for `SlotInfo` and `SAVE_NUM_SLOTS` (Task 3).
- Produces:

```c++
enum MenuScreen {
	MENU_NONE,
	MENU_TITLE,
	MENU_PAUSE,
	MENU_SLOTS,
	MENU_CONFIRM
};

enum MenuAction {
	MENU_ACT_NONE,
	MENU_ACT_START_GAME,
	MENU_ACT_RESUME,
	MENU_ACT_SAVE_SLOT,
	MENU_ACT_LOAD_SLOT,
	MENU_ACT_RETURN_TO_TITLE,
	MENU_ACT_RESCAN_SLOTS
};

struct MenuInput {
	bool up, down, left, right, confirm, cancel, pause;
};

struct MenuState {
	MenuScreen screen;
	int cursor;
	int slotCursor;
	bool saving;
	uint32_t device;
	bool cartPresent;
	bool confirmYes;
	MenuAction pending;
	SlotInfo slots[SAVE_NUM_SLOTS];
};

void menuStateEnterTitle(MenuState *st);
void menuStateEnterPause(MenuState *st);
MenuAction menuStateStep(MenuState *st, const MenuInput *in);
```

`menuStateStep` takes edge-triggered input and returns at most one action per call. `MENU_ACT_RESCAN_SLOTS` means the caller must re-probe `st->device` and refill `st->slots`.

- [ ] **Step 1: Write the failing test**

Create `saturn/tests/test_menu_state.cxx`:

```c++
/*----------------------
 | test_menu_state.cxx
 | Description: Host unit tests for the menu screen state machine. It is pure
 |   logic by design, so every transition the player can reach is covered here
 |   rather than on hardware.
 | Author: suinevere
 | Dependencies: menu_state.h
 ----------------------*/
#include <cstdio>
#include <cstdint>
#include <cstring>
#include "menu_state.h"

static int g_fail = 0;

#define CHECK_EQ(actual, expected)                                            \
    do {                                                                      \
        long long a_ = (long long)(actual);                                   \
        long long e_ = (long long)(expected);                                 \
        if (a_ != e_) {                                                       \
            g_fail++;                                                         \
            printf("FAIL %s:%d  %s\n  actual   = %lld\n  expected = %lld\n",  \
                   __FILE__, __LINE__, #actual, a_, e_);                      \
        }                                                                     \
    } while (0)

#define CHECK(cond)                                                           \
    do {                                                                      \
        if (!(cond)) {                                                        \
            g_fail++;                                                         \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);            \
        }                                                                     \
    } while (0)

static MenuInput NONE;

static MenuInput press(const char *what)
{
    MenuInput in;
    memset(&in, 0, sizeof(in));
    if (strcmp(what, "up") == 0) in.up = true;
    if (strcmp(what, "down") == 0) in.down = true;
    if (strcmp(what, "left") == 0) in.left = true;
    if (strcmp(what, "right") == 0) in.right = true;
    if (strcmp(what, "confirm") == 0) in.confirm = true;
    if (strcmp(what, "cancel") == 0) in.cancel = true;
    if (strcmp(what, "pause") == 0) in.pause = true;
    return in;
}

static void freshTitle(MenuState *st)
{
    memset(st, 0, sizeof(*st));
    menuStateEnterTitle(st);
}

static void test_title_starts_on_start_game(void)
{
    MenuState st;
    freshTitle(&st);
    CHECK_EQ(st.screen, MENU_TITLE);
    CHECK_EQ(st.cursor, 0);
}

static void test_title_confirm_starts_game(void)
{
    MenuState st;
    freshTitle(&st);
    MenuInput in = press("confirm");
    CHECK_EQ(menuStateStep(&st, &in), MENU_ACT_START_GAME);
}

static void test_title_cursor_wraps(void)
{
    MenuState st;
    freshTitle(&st);
    MenuInput up = press("up");
    menuStateStep(&st, &up);
    CHECK_EQ(st.cursor, 1);
    MenuInput down = press("down");
    menuStateStep(&st, &down);
    CHECK_EQ(st.cursor, 0);
}

static void test_title_load_opens_slots_in_load_mode(void)
{
    MenuState st;
    freshTitle(&st);
    MenuInput down = press("down");
    menuStateStep(&st, &down);
    MenuInput confirm = press("confirm");
    CHECK_EQ(menuStateStep(&st, &confirm), MENU_ACT_RESCAN_SLOTS);
    CHECK_EQ(st.screen, MENU_SLOTS);
    CHECK(!st.saving);
}

static void test_pause_starts_on_resume(void)
{
    MenuState st;
    memset(&st, 0, sizeof(st));
    menuStateEnterPause(&st);
    CHECK_EQ(st.screen, MENU_PAUSE);
    CHECK_EQ(st.cursor, 0);
    MenuInput confirm = press("confirm");
    CHECK_EQ(menuStateStep(&st, &confirm), MENU_ACT_RESUME);
}

static void test_pause_button_resumes(void)
{
    MenuState st;
    memset(&st, 0, sizeof(st));
    menuStateEnterPause(&st);
    MenuInput in = press("pause");
    CHECK_EQ(menuStateStep(&st, &in), MENU_ACT_RESUME);
}

static void test_pause_cancel_resumes(void)
{
    MenuState st;
    memset(&st, 0, sizeof(st));
    menuStateEnterPause(&st);
    MenuInput in = press("cancel");
    CHECK_EQ(menuStateStep(&st, &in), MENU_ACT_RESUME);
}

static void test_return_to_menu_asks_for_confirmation(void)
{
    MenuState st;
    memset(&st, 0, sizeof(st));
    menuStateEnterPause(&st);
    MenuInput down = press("down");
    for (int i = 0; i < 3; ++i) {
        menuStateStep(&st, &down);
    }
    CHECK_EQ(st.cursor, 3);
    MenuInput confirm = press("confirm");
    CHECK_EQ(menuStateStep(&st, &confirm), MENU_ACT_NONE);
    CHECK_EQ(st.screen, MENU_CONFIRM);
    CHECK(!st.confirmYes);
}

static void test_confirm_defaults_to_no(void)
{
    MenuState st;
    memset(&st, 0, sizeof(st));
    menuStateEnterPause(&st);
    MenuInput down = press("down");
    for (int i = 0; i < 3; ++i) {
        menuStateStep(&st, &down);
    }
    MenuInput confirm = press("confirm");
    menuStateStep(&st, &confirm);
    CHECK_EQ(menuStateStep(&st, &confirm), MENU_ACT_NONE);
    CHECK_EQ(st.screen, MENU_PAUSE);
}

static void test_confirm_yes_returns_to_title(void)
{
    MenuState st;
    memset(&st, 0, sizeof(st));
    menuStateEnterPause(&st);
    MenuInput down = press("down");
    for (int i = 0; i < 3; ++i) {
        menuStateStep(&st, &down);
    }
    MenuInput confirm = press("confirm");
    menuStateStep(&st, &confirm);
    MenuInput left = press("left");
    menuStateStep(&st, &left);
    CHECK(st.confirmYes);
    CHECK_EQ(menuStateStep(&st, &confirm), MENU_ACT_RETURN_TO_TITLE);
}

static void test_save_over_empty_slot_needs_no_confirmation(void)
{
    MenuState st;
    memset(&st, 0, sizeof(st));
    menuStateEnterPause(&st);
    st.screen = MENU_SLOTS;
    st.saving = true;
    st.slots[0].state = SLOT_EMPTY;
    MenuInput confirm = press("confirm");
    CHECK_EQ(menuStateStep(&st, &confirm), MENU_ACT_SAVE_SLOT);
}

static void test_save_over_used_slot_asks_first(void)
{
    MenuState st;
    memset(&st, 0, sizeof(st));
    menuStateEnterPause(&st);
    st.screen = MENU_SLOTS;
    st.saving = true;
    st.slots[0].state = SLOT_OK;
    MenuInput confirm = press("confirm");
    CHECK_EQ(menuStateStep(&st, &confirm), MENU_ACT_NONE);
    CHECK_EQ(st.screen, MENU_CONFIRM);
    MenuInput left = press("left");
    menuStateStep(&st, &left);
    CHECK_EQ(menuStateStep(&st, &confirm), MENU_ACT_SAVE_SLOT);
}

static void test_load_refuses_empty_and_damaged_slots(void)
{
    MenuState st;
    memset(&st, 0, sizeof(st));
    menuStateEnterTitle(&st);
    st.screen = MENU_SLOTS;
    st.saving = false;

    st.slots[0].state = SLOT_EMPTY;
    MenuInput confirm = press("confirm");
    CHECK_EQ(menuStateStep(&st, &confirm), MENU_ACT_NONE);
    CHECK_EQ(st.screen, MENU_SLOTS);

    st.slots[0].state = SLOT_DAMAGED;
    CHECK_EQ(menuStateStep(&st, &confirm), MENU_ACT_NONE);

    st.slots[0].state = SLOT_OLD_VERSION;
    CHECK_EQ(menuStateStep(&st, &confirm), MENU_ACT_NONE);

    st.slots[0].state = SLOT_OK;
    CHECK_EQ(menuStateStep(&st, &confirm), MENU_ACT_LOAD_SLOT);
}

static void test_save_over_damaged_slot_is_allowed(void)
{
    MenuState st;
    memset(&st, 0, sizeof(st));
    menuStateEnterPause(&st);
    st.screen = MENU_SLOTS;
    st.saving = true;
    st.slots[0].state = SLOT_DAMAGED;
    MenuInput confirm = press("confirm");
    CHECK_EQ(menuStateStep(&st, &confirm), MENU_ACT_NONE);
    CHECK_EQ(st.screen, MENU_CONFIRM);
}

static void test_device_toggle_only_when_cart_present(void)
{
    MenuState st;
    memset(&st, 0, sizeof(st));
    menuStateEnterTitle(&st);
    st.screen = MENU_SLOTS;
    st.device = 1;
    st.cartPresent = false;

    MenuInput right = press("right");
    CHECK_EQ(menuStateStep(&st, &right), MENU_ACT_NONE);
    CHECK_EQ(st.device, 1);

    st.cartPresent = true;
    CHECK_EQ(menuStateStep(&st, &right), MENU_ACT_RESCAN_SLOTS);
    CHECK_EQ(st.device, 2);
    CHECK_EQ(menuStateStep(&st, &right), MENU_ACT_RESCAN_SLOTS);
    CHECK_EQ(st.device, 1);
}

static void test_slot_cancel_goes_back_to_the_opening_screen(void)
{
    MenuState st;
    freshTitle(&st);
    MenuInput down = press("down");
    menuStateStep(&st, &down);
    MenuInput confirm = press("confirm");
    menuStateStep(&st, &confirm);
    CHECK_EQ(st.screen, MENU_SLOTS);
    MenuInput cancel = press("cancel");
    menuStateStep(&st, &cancel);
    CHECK_EQ(st.screen, MENU_TITLE);

    MenuState p;
    memset(&p, 0, sizeof(p));
    menuStateEnterPause(&p);
    menuStateStep(&p, &down);
    menuStateStep(&p, &confirm);
    CHECK_EQ(p.screen, MENU_SLOTS);
    menuStateStep(&p, &cancel);
    CHECK_EQ(p.screen, MENU_PAUSE);
}

static void test_empty_input_does_nothing(void)
{
    MenuState st;
    freshTitle(&st);
    memset(&NONE, 0, sizeof(NONE));
    CHECK_EQ(menuStateStep(&st, &NONE), MENU_ACT_NONE);
    CHECK_EQ(st.cursor, 0);
    CHECK_EQ(st.screen, MENU_TITLE);
}

int main(void)
{
    test_title_starts_on_start_game();
    test_title_confirm_starts_game();
    test_title_cursor_wraps();
    test_title_load_opens_slots_in_load_mode();
    test_pause_starts_on_resume();
    test_pause_button_resumes();
    test_pause_cancel_resumes();
    test_return_to_menu_asks_for_confirmation();
    test_confirm_defaults_to_no();
    test_confirm_yes_returns_to_title();
    test_save_over_empty_slot_needs_no_confirmation();
    test_save_over_used_slot_asks_first();
    test_load_refuses_empty_and_damaged_slots();
    test_save_over_damaged_slot_is_allowed();
    test_device_toggle_only_when_cart_present();
    test_slot_cancel_goes_back_to_the_opening_screen();
    test_empty_input_does_nothing();

    if (g_fail == 0) {
        printf("all tests passed\n");
        return 0;
    }
    printf("%d check(s) failed\n", g_fail);
    return 1;
}
```

- [ ] **Step 2: Add the suite to the runner**

Insert into `saturn/tests/run_tests.sh` before the final `echo`:

```sh
echo "== menu state =="
g++ $OWN_FLAGS -I../src -I../src/system \
    -o run_tests_menustate test_menu_state.cxx stub_saturn_backup.cxx \
    ../src/menu_state.cxx ../src/savedata.cxx
./run_tests_menustate
```

- [ ] **Step 3: Run to verify it fails**

Run: `sh saturn/tests/run_tests.sh`
Expected: FAIL — `menu_state.h: No such file or directory`.

- [ ] **Step 4: Write `menu_state.h`**

Create it with exactly the declarations in the Interfaces block, each carrying a `CLAUDE.md` header block. It includes `savedata.h` and nothing else.

- [ ] **Step 5: Write `menu_state.cxx`**

Behaviours the tests pin down, all in `menuStateStep`:

- `MENU_TITLE`: 2 items, `up`/`down` wrap. Confirm on 0 → `MENU_ACT_START_GAME`. Confirm on 1 → screen becomes `MENU_SLOTS` with `saving = false`, returns `MENU_ACT_RESCAN_SLOTS`. Remember which screen opened the slot list so cancel returns there.
- `MENU_PAUSE`: 4 items, wrap. Confirm on 0, or `cancel`, or `pause` → `MENU_ACT_RESUME`. Confirm on 1 → `MENU_SLOTS` with `saving = true`, returns `MENU_ACT_RESCAN_SLOTS`. Confirm on 2 → `MENU_SLOTS` with `saving = false`, same return. Confirm on 3 → `MENU_CONFIRM` with `pending = MENU_ACT_RETURN_TO_TITLE` and `confirmYes = false`.
- `MENU_SLOTS`: `up`/`down` move `slotCursor` over 3 rows with wrap. `left`/`right` toggle `device` between `SAT_BUP_INTERNAL` and `SAT_BUP_CART`, but only when `cartPresent`; on a real toggle return `MENU_ACT_RESCAN_SLOTS`. `cancel` returns to the opening screen. Confirm when `saving`: `SLOT_EMPTY` → `MENU_ACT_SAVE_SLOT` immediately; anything else → `MENU_CONFIRM` with `pending = MENU_ACT_SAVE_SLOT`. Confirm when loading: only `SLOT_OK` yields `MENU_ACT_LOAD_SLOT`; every other state returns `MENU_ACT_NONE` and stays put.
- `MENU_CONFIRM`: `left`/`right` toggle `confirmYes`, which starts false. Confirm with `confirmYes` returns `pending` and leaves the screen the caller expects — `MENU_TITLE` for return-to-title, `MENU_PAUSE` for a save. Confirm without it, or `cancel`, returns to the previous screen and returns `MENU_ACT_NONE`.

Track the screen to return to in a private field on `MenuState`; add it to the struct in `menu_state.h` and it will be zeroed by the tests' `memset`.

- [ ] **Step 6: Run the tests**

Run: `sh saturn/tests/run_tests.sh`
Expected: PASS — all five suites.

- [ ] **Step 7: Build for Saturn**

Run: `saturn/compile.bat release`
Expected: builds clean.

- [ ] **Step 8: Commit**

```bash
git add saturn/src/menu_state.h saturn/src/menu_state.cxx saturn/tests/test_menu_state.cxx saturn/tests/run_tests.sh
git commit -m "Add the menu screen state machine with title, pause, slot and confirm screens"
```

---

### Task 7: Menu drawing primitives

**Files:**
- Create: `saturn/src/menu_draw.h`, `saturn/src/menu_draw.cxx`
- Create: `saturn/tests/test_menu_draw.cxx`
- Modify: `saturn/tests/run_tests.sh`

The font is a parameter, not a call into `Video`. That keeps this file free of engine dependencies so the pixel arithmetic can be tested off-target — the same reason `scsp_voice.cxx` is separate from `saturn_scsp.cxx`.

**Interfaces:**
- Consumes: nothing.
- Produces:

```c++
enum {
	MENU_PAGE_W     = 320,
	MENU_PAGE_H     = 200,
	MENU_PAGE_PITCH = 160,
	MENU_PAGE_SIZE  = 32000
};

void menuDrawFill(uint8_t *page, int x, int y, int w, int h, uint8_t color);
void menuDrawChar(uint8_t *page, const uint8_t *font, int cellX, int y, uint8_t color, char c);
void menuDrawText(uint8_t *page, const uint8_t *font, int cellX, int y, uint8_t color, const char *s);
void menuDrawDimPalette(const uint8_t *src, uint8_t *dst, int keepIndex);
```

`cellX` is in 8-pixel cells and `y` in scanlines, matching `Video::drawChar` (video.cxx:286-290). `menuDrawFill` takes `x` in pixels and clips to the page. `font` is 8 bytes per glyph starting at `' '`, the layout `Video::_font` uses.

- [ ] **Step 1: Write the failing test**

Create `saturn/tests/test_menu_draw.cxx`:

```c++
/*----------------------
 | test_menu_draw.cxx
 | Description: Host unit tests for the menu drawing primitives. The 4bpp
 |   packing and the palette dim are pure arithmetic over a buffer, so they run
 |   off-target rather than being eyeballed on hardware.
 | Author: suinevere
 | Dependencies: menu_draw.h
 ----------------------*/
#include <cstdio>
#include <cstdint>
#include <cstring>
#include "menu_draw.h"

static int g_fail = 0;

#define CHECK_EQ(actual, expected)                                            \
    do {                                                                      \
        long long a_ = (long long)(actual);                                   \
        long long e_ = (long long)(expected);                                 \
        if (a_ != e_) {                                                       \
            g_fail++;                                                         \
            printf("FAIL %s:%d  %s\n  actual   = %lld\n  expected = %lld\n",  \
                   __FILE__, __LINE__, #actual, a_, e_);                      \
        }                                                                     \
    } while (0)

static uint8_t g_page[MENU_PAGE_SIZE];

/* A font where every glyph is solid, so the packing is unambiguous. */
static uint8_t g_font[96 * 8];

static void setup(void)
{
    memset(g_page, 0, sizeof(g_page));
    memset(g_font, 0xFF, sizeof(g_font));
}

static uint8_t pixelAt(int x, int y)
{
    uint8_t b = g_page[y * MENU_PAGE_PITCH + x / 2];
    return (x & 1) ? (b & 0x0F) : (b >> 4);
}

static void test_fill_sets_both_nibbles(void)
{
    setup();
    menuDrawFill(g_page, 0, 0, 4, 1, 7);
    CHECK_EQ(g_page[0], 0x77);
    CHECK_EQ(g_page[1], 0x77);
    CHECK_EQ(g_page[2], 0x00);
}

static void test_fill_handles_an_odd_left_edge(void)
{
    setup();
    menuDrawFill(g_page, 1, 0, 2, 1, 5);
    CHECK_EQ(pixelAt(0, 0), 0);
    CHECK_EQ(pixelAt(1, 0), 5);
    CHECK_EQ(pixelAt(2, 0), 5);
    CHECK_EQ(pixelAt(3, 0), 0);
}

static void test_fill_clips_to_the_page(void)
{
    setup();
    menuDrawFill(g_page, -4, -4, 8, 8, 3);
    CHECK_EQ(pixelAt(0, 0), 3);
    menuDrawFill(g_page, MENU_PAGE_W - 2, MENU_PAGE_H - 2, 8, 8, 4);
    CHECK_EQ(pixelAt(MENU_PAGE_W - 1, MENU_PAGE_H - 1), 4);
}

static void test_char_writes_eight_by_eight(void)
{
    setup();
    menuDrawChar(g_page, g_font, 0, 0, 9, 'A');
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            CHECK_EQ(pixelAt(x, y), 9);
        }
    }
    CHECK_EQ(pixelAt(8, 0), 0);
    CHECK_EQ(pixelAt(0, 8), 0);
}

static void test_char_lands_on_the_right_cell(void)
{
    setup();
    menuDrawChar(g_page, g_font, 3, 16, 2, 'B');
    CHECK_EQ(pixelAt(24, 16), 2);
    CHECK_EQ(pixelAt(23, 16), 0);
}

static void test_text_advances_one_cell_per_character(void)
{
    setup();
    menuDrawText(g_page, g_font, 0, 0, 1, "AB");
    CHECK_EQ(pixelAt(0, 0), 1);
    CHECK_EQ(pixelAt(8, 0), 1);
    CHECK_EQ(pixelAt(16, 0), 0);
}

static void test_text_off_the_right_edge_is_dropped(void)
{
    setup();
    menuDrawText(g_page, g_font, 38, 0, 1, "ABCD");
    CHECK_EQ(pixelAt(312, 0), 1);
}

static void test_dim_halves_every_channel(void)
{
    uint8_t src[32];
    uint8_t dst[32];
    memset(src, 0, sizeof(src));
    src[0] = 0x0E;
    src[1] = 0xA6;

    menuDrawDimPalette(src, dst, -1);
    CHECK_EQ(dst[0] & 0x0F, 7);
    CHECK_EQ((dst[1] & 0xF0) >> 4, 5);
    CHECK_EQ(dst[1] & 0x0F, 3);
}

static void test_dim_keeps_the_text_index_bright(void)
{
    uint8_t src[32];
    uint8_t dst[32];
    memset(src, 0, sizeof(src));

    menuDrawDimPalette(src, dst, 15);
    CHECK_EQ(dst[30] & 0x0F, 15);
    CHECK_EQ((dst[31] & 0xF0) >> 4, 15);
    CHECK_EQ(dst[31] & 0x0F, 15);
    CHECK_EQ(dst[0] & 0x0F, 0);
}

int main(void)
{
    test_fill_sets_both_nibbles();
    test_fill_handles_an_odd_left_edge();
    test_fill_clips_to_the_page();
    test_char_writes_eight_by_eight();
    test_char_lands_on_the_right_cell();
    test_text_advances_one_cell_per_character();
    test_text_off_the_right_edge_is_dropped();
    test_dim_halves_every_channel();
    test_dim_keeps_the_text_index_bright();

    if (g_fail == 0) {
        printf("all tests passed\n");
        return 0;
    }
    printf("%d check(s) failed\n", g_fail);
    return 1;
}
```

- [ ] **Step 2: Add the suite to the runner**

Insert into `saturn/tests/run_tests.sh` before the final `echo`:

```sh
echo "== menu draw =="
g++ $OWN_FLAGS -I../src \
    -o run_tests_menudraw test_menu_draw.cxx ../src/menu_draw.cxx
./run_tests_menudraw
```

- [ ] **Step 3: Run to verify it fails**

Run: `sh saturn/tests/run_tests.sh`
Expected: FAIL — `menu_draw.h: No such file or directory`.

- [ ] **Step 4: Write `menu_draw.h` and `menu_draw.cxx`**

`menuDrawChar` is the same nibble-packing loop as `Video::drawChar` (video.cxx:285-313), with the font as a parameter and the `x <= 39 && y <= 192` bounds check kept. `menuDrawText` walks the string calling it, stopping at cell 40.

`menuDrawDimPalette` reads the 4-bit-per-channel format documented at `saturn_platform.h:43`: `R = byte0 & 0x0F`, `G = (byte1 & 0xF0) >> 4`, `B = byte1 & 0x0F`. It halves each channel with `>> 1`, and when `keepIndex` is 0–15 writes that entry as full white instead. A `keepIndex` outside 0–15 dims every entry.

`menu_draw.cxx` includes only `menu_draw.h`, `<stdint.h>` and `<string.h>`.

- [ ] **Step 5: Run the tests**

Run: `sh saturn/tests/run_tests.sh`
Expected: PASS — all six suites.

- [ ] **Step 6: Build for Saturn**

Run: `saturn/compile.bat release`
Expected: builds clean.

- [ ] **Step 7: Commit**

```bash
git add saturn/src/menu_draw.h saturn/src/menu_draw.cxx saturn/tests/test_menu_draw.cxx saturn/tests/run_tests.sh
git commit -m "Add menu drawing primitives for fills, text and palette dimming"
```

---

### Task 8: Menu screens and the three-state engine loop

**Files:**
- Create: `saturn/src/menu.h`, `saturn/src/menu.cxx`
- Modify: `saturn/src/engine.h`, `saturn/src/engine.cxx:30-44`
- Modify: `saturn/src/vm.cxx:640-653`

This is the only task with no new host tests: it is the glue, and everything underneath it is already covered. Its verification is on target.

**Interfaces:**
- Consumes: `menu_state.h` (6), `menu_draw.h` (7), `savedata.h` (3), `saturn_backup.h` (2), `Engine::saveSlot`/`loadSlot`/`startNewGame` (4), the `PlayerInput` menu fields (5).
- Produces: `struct Menu` with `void init(Engine *e)`, `bool runTitle()`, `bool runPause()` — each returns when the player has chosen to leave the menu, having already performed any save or load.

- [ ] **Step 1: Delete the busy-wait pause**

In `saturn/src/vm.cxx:640-653`, remove the `if (sys->input.pause) { ... }` block that spins waiting for the button to be pressed again. Leave `sys->input.pause` set; `Engine::run` consumes it.

- [ ] **Step 2: Write `menu.h`**

`Menu` owns:
- `uint8_t *_page` — the 32 KB compositing page, allocated once in `init`
- `uint8_t _savedPal[32]`, `uint8_t _dimPal[32]`
- `MenuState _st`
- `Engine *_engine`
- `int _statusError` — the last `SAT_BUP_ERR_*` to show under the slot list
- an input edge-detector: previous-frame bits plus a repeat counter for the D-pad

- [ ] **Step 3: Write `menu.cxx`**

Structure:
- `pollEdges()` calls `sys->processEvents()`, converts `PlayerInput` into a `MenuInput` of edges, and applies D-pad auto-repeat: first repeat after 20 frames, then every 4.
- `probeDevices()` calls `sat_bup_probe` for both devices, sets `_st.cartPresent`, and picks the starting device with `savedataPickDefaultDevice`, passing whether either device has any `SLOT_OK` slot.
- `rescan()` fills `_st.slots` with `savedataProbe` for the current device.
- `drawTitle()`, `drawPause()`, `drawSlots()`, `drawConfirm()` each clear or composite `_page`, draw with `menuDrawText` and `menuDrawFill`, then `sys->updateDisplay(_page)`.
- `runTitle()` installs the menu's own 16-colour palette via `sys->setPalette`, then loops: poll, step, draw, act. `MENU_ACT_START_GAME` returns true. `MENU_ACT_LOAD_SLOT` calls `_engine->loadSlot` and returns true on success, or sets `_statusError` and stays on failure. `MENU_ACT_RESCAN_SLOTS` calls `rescan()`.
- `runPause()` copies the last presented page into `_page`, builds `_dimPal` with `menuDrawDimPalette(_savedPal, _dimPal, 15)`, installs it, then loops the same way. On exit it reinstalls `_savedPal`. `MENU_ACT_SAVE_SLOT` calls `_engine->saveSlot`, sets `_statusError` from `_engine->lastSaveError()`, then `rescan()`. `MENU_ACT_RETURN_TO_TITLE` returns false; `MENU_ACT_RESUME` and a successful `MENU_ACT_LOAD_SLOT` return true.

The menu must keep its own copy of the palette the game last set. `System` has `setPalette` but no getter, so `Menu` records it: have `Video::changePal` be the only writer already, and snapshot in `runPause` from a copy the menu keeps — add a `uint8_t Menu::_lastGamePal[32]` updated by a small `Menu::notePalette(const uint8_t *)` that `SaturnSystem::setPalette` calls. Wire that through a file-static hook in `menu.cxx` rather than adding a member to `System`, so `sys.h` stays the upstream interface it is.

The slot row format, matching the spec's mockups:

```
  > 1  THE JAIL        08/01 21:14
    2  - EMPTY -
```

Chapter name from `savedataChapterName(info.partId)`, timestamp from `sat_bup_date_split(info.date, ...)`. Status strings for the error line: `NOT ENOUGH SPACE`, `CARTRIDGE WRITE PROTECTED`, `BACKUP RAM UNFORMATTED`, `CARTRIDGE UNFORMATTED`.

- [ ] **Step 4: Rewrite `Engine::run` as three states**

```c++
/*----------------------
 | Engine::run
 | Description: The top-level loop: title card, gameplay, pause menu. The VM
 |   only advances in the playing state; the menus own the frame while they are
 |   up.
 | Author: suinevere
 | Dependencies: menu.h
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void Engine::run() {
	Menu menu;
	menu.init(this);

	while (!sys->input.quit) {
		if (!menu.runTitle()) {
			continue;
		}

		bool playing = true;
		while (playing && !sys->input.quit) {
			vm.checkThreadRequests();
			vm.inp_updatePlayer();

			if (sys->input.pause) {
				sys->input.pause = false;
				playing = menu.runPause();
				continue;
			}

			vm.hostFrame();
		}
	}
}
```

`runTitle` returns true when a game is starting or has just been loaded; the `continue` on false exists only so a future title-screen exit path has somewhere to go. Remove the `startNewGame()` call that Task 4 left at the end of `init` — `runTitle` calls it now.

- [ ] **Step 5: Build**

Run: `saturn/compile.bat release`
Expected: builds clean.

- [ ] **Step 6: Verify on target**

Boot in Mednafen (`saturn/run_with_mednafen.bat`) and walk the whole feature:

1. Title card appears, cursor moves between START GAME and LOAD GAME.
2. START GAME plays the intro.
3. Start opens the pause menu over a dimmed frozen frame; the frame is intact on resume.
4. SAVE GAME into slot 1 succeeds; the slot list shows a chapter name and a plausible timestamp.
5. Overwriting slot 1 asks first, and NO is the default.
6. RETURN TO MENU asks first, and YES lands back on the title card.
7. LOAD GAME from the title card restores the run. Note how long the background is missing — this is the spec's third Known Risk and the number belongs in the task report.
8. With a backup cartridge configured in Mednafen, the device row appears and L/R switches it, with the slot rows changing to match.
9. With no cartridge, the device row is absent.

- [ ] **Step 7: Run the full host suite once more**

Run: `sh saturn/tests/run_tests.sh`
Expected: all six suites pass.

- [ ] **Step 8: Commit**

```bash
git add saturn/src/menu.h saturn/src/menu.cxx saturn/src/engine.h saturn/src/engine.cxx saturn/src/vm.cxx
git commit -m "Add the title card and pause menu and drive them from a three-state engine loop"
```

---

## Verification Summary

| Spec section | Task |
|---|---|
| Save format version 3, page gating | 1 |
| Page clear on load | 1 |
| `saturn_backup` interface, device probe | 2 |
| BUP work buffer risk | 2, Step 8 |
| `savedata`, slot names, chapter names | 3 |
| Header layout | 3 |
| Engine save/load, quicksave removal | 4 |
| Input A/B/C split, L/R | 5 |
| Menu state machine, confirmations | 6 |
| Title card, pause overlay, palette dim | 7, 8 |
| Device selection UI | 6 (logic), 8 (drawing) |
| Error strings | 3 (states), 8 (display) |
| Post-load background risk | 8, Step 6.7 |
