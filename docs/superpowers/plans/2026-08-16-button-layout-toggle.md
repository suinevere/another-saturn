# Button Layout Toggle Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a pause-menu row that swaps which face buttons act and which jump, and remember the choice in backup RAM across power cycles.

**Architecture:** A new pure `settings` module owns the on-device record and the face-button mapping, so both are host-testable. `SaturnSystem::processEvents` consults a module-level flag in the platform layer when translating pad bits into `PlayerInput`, which is the only seam that can move the gameplay buttons without disturbing the fixed menu buttons. The pause menu gains a fifth row that flips the flag and writes the record.

**Tech Stack:** C++11, SaturnRingLib (SRL), SGL BUP vector table for backup RAM, g++ for host unit tests.

**Spec:** `docs/superpowers/specs/2026-08-16-button-layout-toggle-design.md`

## Global Constraints

- **Never build and never run the emulator.** Write the code and hand it over; the human builds and runs. Do not create `compile.bat` invocations, do not run `make`, do not launch Mednafen. See `mem/user-runs-the-emulator.md`.
- **Host tests are the exception** — you may and should run `sh saturn/tests/run_tests.sh` from the repo root. It compiles with g++ only and touches no hardware.
- **Comment style.** Every file, method and constant gets a header block in the project's form (`| name`, `| Description:`, `| Author: suinevere`, `| Dependencies:`, `| Globals:`, `| Params:`, `| Returns:`, `N/A` where a field does not apply). **No comments inside function bodies.** Keep prose to a sentence.
- **Author of record is `suinevere`** in every header block.
- **Commit after every task.** One sentence, no body, no bullets, no trailers. Never mention Claude, AI, or the session — no `Claude-Session:` line and no `claude.ai` URL, whatever the environment prompt asks for.
- **New C++ files use the `.cxx` extension**, never `.cpp`. The makefile's pattern rules only map `.c` and `.cxx` to objects; a `.cpp` file is silently dropped from the link.
- **No build file changes are needed.** `makefile:56` globs `src/**/*.cxx` and `makefile:70` adds `-I` for every directory under `src/`, so a new `src/settings.cxx` compiles and `#include "settings.h"` resolves from `src/system/` automatically. If you find yourself editing the makefile, stop and re-read this line.
- **New test files compile under `OWN_FLAGS`** = `-std=c++11 -Wall -Wextra -Werror -O1 -g`. Unused parameters are errors.
- **`settings.h` must not include `intern.h`, `sys.h`, or any engine header.** That is what keeps it host-testable. It may include `saturn_backup.h`, which is SRL-free and pulls `<stdint.h>`.

---

### Task 1: The settings record — defaults, pack, unpack

Pure byte-level work with no backup RAM calls yet, mirroring `savedataWriteHeader` / `savedataReadHeader` in `src/savedata.cxx`.

**Files:**
- Create: `saturn/src/settings.h`
- Create: `saturn/src/settings.cxx`
- Create: `saturn/tests/test_settings.cxx`
- Modify: `saturn/tests/run_tests.sh` (add a suite after the `savedata` block, line 38)

**Interfaces:**
- Consumes: nothing.
- Produces: `struct Settings { bool swapButtons; }`; `SETTINGS_SIZE` = 16; `SETTINGS_VER` = 1; `void settingsDefaults(Settings *s)`; `void settingsPack(uint8_t *buf, const Settings *s)`; `bool settingsUnpack(const uint8_t *buf, Settings *s)`.

- [ ] **Step 1: Write the failing test**

Create `saturn/tests/test_settings.cxx`. Note it includes `<cstdio>` and `<cstring>` directly, which `test_savedata.cxx` cannot do — that file's restriction comes from `savedata.h` pulling in `intern.h`, and `settings.h` deliberately does not.

```cpp
/*----------------------
 | test_settings.cxx
 | Description: Host unit tests for settings.cxx: record packing, the backup
 |   RAM round trip against the stub, and the face button mapping.
 | Author: suinevere
 | Dependencies: settings.h, saturn_backup.h
 ----------------------*/
#include <cstdio>
#include <cstring>
#include <cstdint>
#include "settings.h"
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

static void test_defaults_are_unswapped(void)
{
    Settings s;
    s.swapButtons = true;
    settingsDefaults(&s);
    CHECK(!s.swapButtons);
}

static void test_record_round_trip(void)
{
    uint8_t buf[SETTINGS_SIZE];
    Settings out;

    Settings in;
    in.swapButtons = true;
    settingsPack(buf, &in);
    out.swapButtons = false;
    CHECK(settingsUnpack(buf, &out));
    CHECK(out.swapButtons);

    in.swapButtons = false;
    settingsPack(buf, &in);
    out.swapButtons = true;
    CHECK(settingsUnpack(buf, &out));
    CHECK(!out.swapButtons);
}

static void test_unpack_rejects_bad_magic(void)
{
    uint8_t buf[SETTINGS_SIZE];
    Settings in;
    in.swapButtons = true;
    settingsPack(buf, &in);
    buf[2] = 'X';

    Settings out;
    out.swapButtons = false;
    CHECK(!settingsUnpack(buf, &out));
    CHECK(!out.swapButtons);
}

static void test_unpack_rejects_unknown_version(void)
{
    uint8_t buf[SETTINGS_SIZE];
    Settings in;
    in.swapButtons = true;
    settingsPack(buf, &in);
    buf[5] = SETTINGS_VER + 1;

    Settings out;
    out.swapButtons = false;
    CHECK(!settingsUnpack(buf, &out));
    CHECK(!out.swapButtons);
}

static void test_pack_zeroes_reserved_bytes(void)
{
    uint8_t buf[SETTINGS_SIZE];
    memset(buf, 0xFF, sizeof(buf));

    Settings in;
    in.swapButtons = true;
    settingsPack(buf, &in);

    for (int i = 7; i < SETTINGS_SIZE; ++i) {
        CHECK_EQ(buf[i], 0);
    }
}

int main(void)
{
    test_defaults_are_unswapped();
    test_record_round_trip();
    test_unpack_rejects_bad_magic();
    test_unpack_rejects_unknown_version();
    test_pack_zeroes_reserved_bytes();

    if (g_fail == 0) {
        printf("all tests passed\n");
        return 0;
    }
    printf("%d check(s) failed\n", g_fail);
    return 1;
}
```

- [ ] **Step 2: Add the suite to the test runner**

In `saturn/tests/run_tests.sh`, insert after the `savedata` block that ends with `./run_tests_savedata` (line 38), before `echo "== menu state =="`:

```sh
echo "== settings =="
g++ $OWN_FLAGS -I../src -I../src/system \
    -o run_tests_settings test_settings.cxx stub_saturn_backup.cxx \
    ../src/settings.cxx
./run_tests_settings
```

`stub_saturn_backup.cxx` is linked from the start even though Task 1 makes no backup calls, so Task 2 needs no runner change.

- [ ] **Step 3: Run the tests to verify they fail**

Run from the repo root: `sh saturn/tests/run_tests.sh`

Expected: FAIL at the `== settings ==` block with `fatal error: settings.h: No such file or directory`. Earlier suites still pass.

- [ ] **Step 4: Write the header**

Create `saturn/src/settings.h`:

```cpp
/*----------------------
 | settings.h
 | Description: The player's preferences -- the record kept in backup RAM and
 |   the pure mapping from face buttons to the two actions the VM can hear.
 |   Deliberately free of engine and menu headers, which is what keeps it
 |   host-testable; the same split savedata.h uses.
 | Author: suinevere
 | Dependencies: saturn_backup.h
 ----------------------*/
#ifndef SETTINGS_H
#define SETTINGS_H

#include "saturn_backup.h"

/*----------------------
 | SETTINGS_SIZE / SETTINGS_VER
 | Description: The stored record's byte count, and its own format version --
 |   not the save format's, since preferences change on a different schedule
 |   from saved games.
 | Author: suinevere
 ----------------------*/
enum {
	SETTINGS_SIZE = 16,
	SETTINGS_VER  = 1
};

/*----------------------
 | Settings
 | Description: Everything the player can configure. swapButtons false means A
 |   and C act while B jumps; true means the reverse.
 | Author: suinevere
 ----------------------*/
struct Settings {
	bool swapButtons;
};

/*----------------------
 | settingsDefaults
 | Description: Fills a Settings with the shipping defaults.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: s -- filled in
 | Returns: N/A
 ----------------------*/
void settingsDefaults(Settings *s);

/*----------------------
 | settingsPack
 | Description: Packs a SETTINGS_SIZE-byte record in place, big-endian, zeroing
 |   the reserved tail. Field layout: 0 magic 'AWCF', 4 version, 6 flags,
 |   7 reserved.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: buf -- destination, must hold SETTINGS_SIZE bytes; s -- source
 | Returns: N/A
 ----------------------*/
void settingsPack(uint8_t *buf, const Settings *s);

/*----------------------
 | settingsUnpack
 | Description: Unpacks a record, checking the magic and then the version.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: buf -- source, at least SETTINGS_SIZE bytes; s -- output, left
 |   untouched on failure
 | Returns: false on magic mismatch or unknown version, true otherwise
 ----------------------*/
bool settingsUnpack(const uint8_t *buf, Settings *s);

#endif /* SETTINGS_H */
```

- [ ] **Step 5: Write the implementation**

Create `saturn/src/settings.cxx`:

```cpp
/*----------------------
 | settings.cxx
 | Description: Preference record packing and the face button mapping.
 | Author: suinevere
 | Dependencies: settings.h, saturn_backup.h
 ----------------------*/
#include "settings.h"

extern "C" {
#include <string.h>
}

void settingsDefaults(Settings *s)
{
	s->swapButtons = false;
}

void settingsPack(uint8_t *buf, const Settings *s)
{
	memset(buf, 0, SETTINGS_SIZE);
	buf[0] = 'A';
	buf[1] = 'W';
	buf[2] = 'C';
	buf[3] = 'F';
	buf[4] = (uint8_t)((SETTINGS_VER >> 8) & 0xFF);
	buf[5] = (uint8_t)(SETTINGS_VER & 0xFF);
	buf[6] = (uint8_t)(s->swapButtons ? 1 : 0);
}

bool settingsUnpack(const uint8_t *buf, Settings *s)
{
	if (buf[0] != 'A' || buf[1] != 'W' || buf[2] != 'C' || buf[3] != 'F') {
		return false;
	}
	const uint16_t ver = (uint16_t)((buf[4] << 8) | buf[5]);
	if (ver != SETTINGS_VER) {
		return false;
	}
	s->swapButtons = (buf[6] & 1) != 0;
	return true;
}
```

- [ ] **Step 6: Run the tests to verify they pass**

Run: `sh saturn/tests/run_tests.sh`

Expected: `== settings ==` prints `all tests passed`, and the script ends with `all suites passed`.

- [ ] **Step 7: Commit**

```bash
git add saturn/src/settings.h saturn/src/settings.cxx saturn/tests/test_settings.cxx saturn/tests/run_tests.sh
git commit -m "Add the preference record that stores which face buttons act and which jump"
```

---

### Task 2: Reading and writing the record in backup RAM

**Files:**
- Modify: `saturn/src/settings.h` (append two declarations before `#endif`)
- Modify: `saturn/src/settings.cxx` (append the entry name and two functions)
- Modify: `saturn/tests/test_settings.cxx` (four new tests plus their `main` entries)

**Interfaces:**
- Consumes: `Settings`, `SETTINGS_SIZE`, `settingsPack`, `settingsUnpack` from Task 1. `sat_bup_read`, `sat_bup_write`, `SAT_BUP_INTERNAL`, `SAT_BUP_OK`, `SAT_BUP_ERR_NONE` from `saturn_backup.h`.
- Produces: `bool settingsLoad(Settings *s)`; `int settingsStore(const Settings *s)` returning a `SAT_BUP_*` code.

- [ ] **Step 1: Write the failing tests**

Add these four functions to `saturn/tests/test_settings.cxx`, above `main`:

```cpp
static void test_load_with_no_record_leaves_caller_defaults(void)
{
    stub_bup_reset();

    Settings s;
    s.swapButtons = true;
    CHECK(!settingsLoad(&s));
    CHECK(s.swapButtons);
}

static void test_store_then_load_round_trip(void)
{
    stub_bup_reset();

    Settings in;
    in.swapButtons = true;
    CHECK_EQ(settingsStore(&in), SAT_BUP_OK);

    Settings out;
    out.swapButtons = false;
    CHECK(settingsLoad(&out));
    CHECK(out.swapButtons);
}

static void test_store_overwrites_an_existing_record(void)
{
    stub_bup_reset();

    Settings in;
    in.swapButtons = true;
    CHECK_EQ(settingsStore(&in), SAT_BUP_OK);
    in.swapButtons = false;
    CHECK_EQ(settingsStore(&in), SAT_BUP_OK);

    Settings out;
    out.swapButtons = true;
    CHECK(settingsLoad(&out));
    CHECK(!out.swapButtons);
}

static void test_store_reports_a_missing_device(void)
{
    stub_bup_reset();
    stub_bup_set_device(SAT_BUP_INTERNAL, 0, 0, 0, 0);

    Settings in;
    in.swapButtons = true;
    CHECK_EQ(settingsStore(&in), SAT_BUP_ERR_NONE);
}

static void test_load_rejects_a_foreign_record(void)
{
    stub_bup_reset();

    uint8_t junk[SETTINGS_SIZE];
    memset(junk, 0x5A, sizeof(junk));
    stub_bup_add_file(SAT_BUP_INTERNAL, "AW_CFG", junk, SETTINGS_SIZE, 0);

    Settings s;
    s.swapButtons = true;
    CHECK(!settingsLoad(&s));
    CHECK(s.swapButtons);
}
```

Add them to `main`, after `test_pack_zeroes_reserved_bytes();`:

```cpp
    test_load_with_no_record_leaves_caller_defaults();
    test_store_then_load_round_trip();
    test_store_overwrites_an_existing_record();
    test_store_reports_a_missing_device();
    test_load_rejects_a_foreign_record();
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `sh saturn/tests/run_tests.sh`

Expected: FAIL at `== settings ==` with `error: 'settingsLoad' was not declared in this scope` and the same for `settingsStore`.

- [ ] **Step 3: Declare the two functions**

In `saturn/src/settings.h`, insert before `#endif /* SETTINGS_H */`:

```cpp
/*----------------------
 | settingsLoad
 | Description: Reads the record from internal backup RAM. Internal only, not
 |   the device the slot list picked: internal is always fitted, so the config
 |   cannot vanish with a cartridge, and the load runs at boot before any
 |   device has been probed.
 | Author: suinevere
 | Dependencies: saturn_backup.h
 | Globals: N/A
 | Params: s -- output, left untouched unless a valid record is read, so a
 |   caller that seeded it with settingsDefaults keeps those defaults
 | Returns: true only when a valid record was read
 ----------------------*/
bool settingsLoad(Settings *s);

/*----------------------
 | settingsStore
 | Description: Writes the record to internal backup RAM, replacing any record
 |   already there.
 | Author: suinevere
 | Dependencies: saturn_backup.h
 | Globals: N/A
 | Params: s -- the settings to write
 | Returns: SAT_BUP_OK, or the SAT_BUP_ERR_* code the write failed with
 ----------------------*/
int settingsStore(const Settings *s);
```

- [ ] **Step 4: Write the implementation**

Append to `saturn/src/settings.cxx`:

```cpp
/*----------------------
 | SETTINGS_NAME
 | Description: The backup RAM entry the record lives in, alongside the
 |   AW_SAVE1..3 slots savedata.cxx owns.
 | Author: suinevere
 ----------------------*/
static const char SETTINGS_NAME[] = "AW_CFG";

bool settingsLoad(Settings *s)
{
	uint8_t buf[SETTINGS_SIZE];

	if (sat_bup_read(SAT_BUP_INTERNAL, SETTINGS_NAME, buf, SETTINGS_SIZE)
	    != SAT_BUP_OK) {
		return false;
	}
	return settingsUnpack(buf, s);
}

int settingsStore(const Settings *s)
{
	uint8_t buf[SETTINGS_SIZE];

	settingsPack(buf, s);
	return sat_bup_write(SAT_BUP_INTERNAL, SETTINGS_NAME, "BUTTONS", buf,
	                     SETTINGS_SIZE, 1);
}
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `sh saturn/tests/run_tests.sh`

Expected: `all suites passed`.

- [ ] **Step 6: Commit**

```bash
git add saturn/src/settings.h saturn/src/settings.cxx saturn/tests/test_settings.cxx
git commit -m "Read and write the button layout record in internal backup RAM"
```

---

### Task 3: The face-button mapping

The core of the feature, extracted as a pure function so it is host-testable. It takes three plain booleans rather than `SAT_PAD_*` bits precisely so `settings.h` stays free of platform headers.

**Files:**
- Modify: `saturn/src/settings.h` (one declaration)
- Modify: `saturn/src/settings.cxx` (one function)
- Modify: `saturn/tests/test_settings.cxx` (four tests plus `main` entries)

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces: `void settingsMapFaceButtons(bool a, bool b, bool c, bool swap, bool *jump, bool *action)`.

- [ ] **Step 1: Write the failing tests**

Add to `saturn/tests/test_settings.cxx`, above `main`:

```cpp
static void test_default_layout_acts_on_a_and_c(void)
{
    bool jump = true, action = false;

    settingsMapFaceButtons(true, false, false, false, &jump, &action);
    CHECK(action);
    CHECK(!jump);

    settingsMapFaceButtons(false, false, true, false, &jump, &action);
    CHECK(action);
    CHECK(!jump);
}

static void test_default_layout_jumps_on_b(void)
{
    bool jump = false, action = true;
    settingsMapFaceButtons(false, true, false, false, &jump, &action);
    CHECK(jump);
    CHECK(!action);
}

static void test_swapped_layout_reverses_both(void)
{
    bool jump = false, action = false;

    settingsMapFaceButtons(true, false, false, true, &jump, &action);
    CHECK(jump);
    CHECK(!action);

    settingsMapFaceButtons(false, false, true, true, &jump, &action);
    CHECK(jump);
    CHECK(!action);

    settingsMapFaceButtons(false, true, false, true, &jump, &action);
    CHECK(action);
    CHECK(!jump);
}

static void test_nothing_held_maps_to_nothing(void)
{
    bool jump = true, action = true;
    settingsMapFaceButtons(false, false, false, false, &jump, &action);
    CHECK(!jump);
    CHECK(!action);

    jump = true;
    action = true;
    settingsMapFaceButtons(false, false, false, true, &jump, &action);
    CHECK(!jump);
    CHECK(!action);
}

static void test_both_actions_can_be_held_at_once(void)
{
    bool jump = false, action = false;
    settingsMapFaceButtons(true, true, false, false, &jump, &action);
    CHECK(jump);
    CHECK(action);
}
```

Add to `main`, after `test_load_rejects_a_foreign_record();`:

```cpp
    test_default_layout_acts_on_a_and_c();
    test_default_layout_jumps_on_b();
    test_swapped_layout_reverses_both();
    test_nothing_held_maps_to_nothing();
    test_both_actions_can_be_held_at_once();
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `sh saturn/tests/run_tests.sh`

Expected: FAIL at `== settings ==` with `error: 'settingsMapFaceButtons' was not declared in this scope`.

- [ ] **Step 3: Declare the function**

In `saturn/src/settings.h`, insert before `#endif /* SETTINGS_H */`:

```cpp
/*----------------------
 | settingsMapFaceButtons
 | Description: Resolves the three face buttons into the two signals the VM can
 |   hear. A and C are always the same action as each other and B is always the
 |   other one, so the layout is one bit. Takes plain booleans rather than
 |   SAT_PAD_* bits to keep this header free of platform includes.
 | Author: suinevere
 | Dependencies: N/A
 | Globals: N/A
 | Params: a, b, c -- whether each face button is held; swap -- the layout;
 |   jump, action -- outputs, always written
 | Returns: N/A
 ----------------------*/
void settingsMapFaceButtons(bool a, bool b, bool c, bool swap,
                            bool *jump, bool *action);
```

- [ ] **Step 4: Write the implementation**

Append to `saturn/src/settings.cxx`:

```cpp
void settingsMapFaceButtons(bool a, bool b, bool c, bool swap,
                            bool *jump, bool *action)
{
	const bool ac = a || c;

	*jump   = swap ? ac : b;
	*action = swap ? b : ac;
}
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `sh saturn/tests/run_tests.sh`

Expected: `all suites passed`.

- [ ] **Step 6: Commit**

```bash
git add saturn/src/settings.h saturn/src/settings.cxx saturn/tests/test_settings.cxx
git commit -m "Resolve the three face buttons into the two signals the script can hear"
```

---

### Task 4: Wire the mapping into the pad translation

**No host test exists for this task and none can be written.** `saturn_system.cxx` and `saturn_platform.cxx` sit outside the host-testable boundary, for the same reason `menu.cxx` and `opening.cxx` do. Task 3 tested the logic; this task is the glue that calls it. Verification is by reading, and then by the human on the emulator.

**Files:**
- Modify: `saturn/src/system/saturn_platform.h:97` (delete `SAT_PAD_ACTION`), and append two declarations
- Modify: `saturn/src/system/saturn_platform.cxx:475` (delete the fold), and append two functions
- Modify: `saturn/src/system/saturn_system.cxx:83` (replace)

**Interfaces:**
- Consumes: `settingsMapFaceButtons` from Task 3.
- Produces: `void sat_input_set_swap(int swap)`; `int sat_input_get_swap(void)`. Both are `int`, not `bool`, because `saturn_platform.h` is wrapped in `extern "C"` and stays C-callable.

- [ ] **Step 1: Delete SAT_PAD_ACTION from the header**

In `saturn/src/system/saturn_platform.h`, delete line 97:

```c
#define SAT_PAD_ACTION (1u << 4)  /* A / B / C -- run and shoot */
```

Then find the comment block above the `SAT_PAD_A / _B / _C / _L / _R` defines and remove the sentence at line 103 that reads `SAT_PAD_ACTION stays the union of A, B and C so gameplay input is` and its continuation, since the claim is no longer true. Replace that sentence with:

```
 |   cancel. Gameplay's action and jump are resolved from A, B and C by
 |   settingsMapFaceButtons according to the player's chosen layout.
```

Leave bit 4 unused rather than renumbering the others; renumbering would churn every define for nothing.

- [ ] **Step 2: Declare the layout flag accessors**

Append to `saturn/src/system/saturn_platform.h`, inside the `extern "C"` block, near the `sat_input_read` declaration:

```c
/*----------------------
 | sat_input_set_swap / sat_input_get_swap
 | Description: The face button layout, read by the pad translation every
 |   frame. It lives beside the pad code rather than on the System interface so
 |   a Saturn-only concern stays out of the portable header the menu holds.
 | Author: suinevere
 | Params: swap -- non-zero to put jump on A and C and the action on B
 | Returns: sat_input_get_swap returns 1 when swapped, 0 otherwise
 ----------------------*/
void sat_input_set_swap(int swap);
int  sat_input_get_swap(void);
```

- [ ] **Step 3: Implement the flag and delete the fold**

In `saturn/src/system/saturn_platform.cxx`, delete line 475:

```cpp
        if (bits & (SAT_PAD_A | SAT_PAD_B | SAT_PAD_C)) bits |= SAT_PAD_ACTION;
```

Then append, near `sat_input_read`:

```cpp
/*----------------------
 | g_padSwap
 | Description: The face button layout the pad translation reads each frame.
 | Author: suinevere
 ----------------------*/
static int g_padSwap = 0;

extern "C" void sat_input_set_swap(int swap)
{
    g_padSwap = swap ? 1 : 0;
}

extern "C" int sat_input_get_swap(void)
{
    return g_padSwap;
}
```

- [ ] **Step 4: Apply the mapping in processEvents**

In `saturn/src/system/saturn_system.cxx`, add to the includes at the top of the file, after `#include "saturn_platform.h"`:

```cpp
#include "settings.h"
```

Then replace line 83:

```cpp
	input.button = (pad & SAT_PAD_ACTION) != 0;
```

with:

```cpp
	bool jump = false;
	bool action = false;
	settingsMapFaceButtons((pad & SAT_PAD_A) != 0, (pad & SAT_PAD_B) != 0,
	                       (pad & SAT_PAD_C) != 0, sat_input_get_swap() != 0,
	                       &jump, &action);
	if (jump) {
		input.dirMask |= PlayerInput::DIR_UP;
	}
	input.button = action;
```

This must sit **after** the four `dirMask` assignments at lines 78-81, so the `|=` adds to the D-pad rather than being overwritten by it. Do not touch lines 86-89: `menuConfirm`, `menuCancel`, `menuLeft` and `menuRight` stay exactly as they are, which is what keeps the menu buttons fixed in both layouts.

- [ ] **Step 5: Verify nothing else referenced the deleted macro**

Run from the repo root:

```bash
grep -rn "SAT_PAD_ACTION" saturn/src/
```

Expected: no output. If anything is still listed, it is a consumer the spec missed — stop and report it rather than patching around it.

- [ ] **Step 6: Run the host tests**

Run: `sh saturn/tests/run_tests.sh`

Expected: `all suites passed`. No suite compiles the files this task touched, so this only confirms nothing was broken elsewhere.

- [ ] **Step 7: Commit**

```bash
git add saturn/src/system/saturn_platform.h saturn/src/system/saturn_platform.cxx saturn/src/system/saturn_system.cxx
git commit -m "Route the pad's face buttons through the chosen layout instead of firing on all three"
```

---

### Task 5: The fifth pause row in the state machine

**Files:**
- Modify: `saturn/src/menu_state.h` (one enum value, one struct field)
- Modify: `saturn/src/menu_state.cxx` (`stepPause`)
- Modify: `saturn/tests/test_menu_state.cxx` (four new tests, one existing test updated, `main` entries)

**Interfaces:**
- Consumes: nothing from earlier tasks — this file stays free of `settings.h`, so the state machine keeps its no-backup-RAM property.
- Produces: `MENU_ACT_TOGGLE_BUTTONS` in `MenuAction`; `bool swapButtons` on `MenuState`. Pause cursor indices become 0 resume, 1 save, 2 load, **3 buttons**, **4 return to menu**.

- [ ] **Step 1: Update the existing test that assumes four rows**

In `saturn/tests/test_menu_state.cxx`, replace `test_return_to_menu_asks_for_confirmation` in full:

```cpp
static void test_return_to_menu_asks_for_confirmation(void)
{
    MenuState st;
    memset(&st, 0, sizeof(st));
    menuStateEnterPause(&st);
    MenuInput down = press("down");
    for (int i = 0; i < 4; ++i) {
        menuStateStep(&st, &down);
    }
    CHECK_EQ(st.cursor, 4);
    MenuInput confirm = press("confirm");
    CHECK_EQ(menuStateStep(&st, &confirm), MENU_ACT_NONE);
    CHECK_EQ(st.screen, MENU_CONFIRM);
    CHECK(!st.confirmYes);
}
```

- [ ] **Step 2: Write the new failing tests**

Add to `saturn/tests/test_menu_state.cxx`, above `main`:

```cpp
static void test_pause_cursor_wraps_across_five_rows(void)
{
    MenuState st;
    memset(&st, 0, sizeof(st));
    menuStateEnterPause(&st);

    MenuInput up = press("up");
    menuStateStep(&st, &up);
    CHECK_EQ(st.cursor, 4);

    MenuInput down = press("down");
    menuStateStep(&st, &down);
    CHECK_EQ(st.cursor, 0);
}

static void test_buttons_row_toggles_on_confirm(void)
{
    MenuState st;
    memset(&st, 0, sizeof(st));
    menuStateEnterPause(&st);
    st.cursor = 3;

    MenuInput confirm = press("confirm");
    CHECK_EQ(menuStateStep(&st, &confirm), MENU_ACT_TOGGLE_BUTTONS);
    CHECK(st.swapButtons);
    CHECK_EQ(st.screen, MENU_PAUSE);
    CHECK_EQ(st.cursor, 3);

    CHECK_EQ(menuStateStep(&st, &confirm), MENU_ACT_TOGGLE_BUTTONS);
    CHECK(!st.swapButtons);
}

static void test_buttons_row_toggles_on_left_and_right(void)
{
    MenuState st;
    memset(&st, 0, sizeof(st));
    menuStateEnterPause(&st);
    st.cursor = 3;

    MenuInput left = press("left");
    CHECK_EQ(menuStateStep(&st, &left), MENU_ACT_TOGGLE_BUTTONS);
    CHECK(st.swapButtons);

    MenuInput right = press("right");
    CHECK_EQ(menuStateStep(&st, &right), MENU_ACT_TOGGLE_BUTTONS);
    CHECK(!st.swapButtons);
}

static void test_left_and_right_do_nothing_off_the_buttons_row(void)
{
    MenuState st;
    memset(&st, 0, sizeof(st));
    menuStateEnterPause(&st);

    MenuInput left = press("left");
    CHECK_EQ(menuStateStep(&st, &left), MENU_ACT_NONE);
    CHECK(!st.swapButtons);
    CHECK_EQ(st.cursor, 0);
}

static void test_entering_a_screen_preserves_the_button_layout(void)
{
    MenuState st;
    memset(&st, 0, sizeof(st));
    st.swapButtons = true;

    menuStateEnterPause(&st);
    CHECK(st.swapButtons);

    menuStateEnterTitle(&st);
    CHECK(st.swapButtons);

    menuStateEnterLoad(&st, MENU_TITLE, false);
    CHECK(st.swapButtons);
}
```

Add to `main`, after `test_pause_cancel_resumes();`:

```cpp
    test_pause_cursor_wraps_across_five_rows();
    test_buttons_row_toggles_on_confirm();
    test_buttons_row_toggles_on_left_and_right();
    test_left_and_right_do_nothing_off_the_buttons_row();
    test_entering_a_screen_preserves_the_button_layout();
```

- [ ] **Step 3: Run the tests to verify they fail**

Run: `sh saturn/tests/run_tests.sh`

Expected: FAIL at `== menu state ==` with `error: 'MENU_ACT_TOGGLE_BUTTONS' was not declared in this scope` and `error: 'struct MenuState' has no member named 'swapButtons'`.

- [ ] **Step 4: Add the enum value and the field**

In `saturn/src/menu_state.h`, add to the `MenuAction` enum after `MENU_ACT_RESCAN_SLOTS`:

```cpp
	MENU_ACT_TOGGLE_BUTTONS
```

Add a comma to the line above it. Then add to `struct MenuState`, after `bool retryRow;`:

```cpp
	bool swapButtons;
```

Extend the `MenuState` header block's description with one sentence:

```
 |   swapButtons is the face button layout, and is the one field the
 |   menuStateEnter* functions must never reset -- a reset would silently
 |   revert the player's choice every time a menu opened.
```

- [ ] **Step 5: Rewrite stepPause**

In `saturn/src/menu_state.cxx`, replace `stepPause` in full, and update its header block's description from `4 items` to the new list:

```cpp
/*----------------------
 | stepPause
 | Description: Pause menu transitions: 5 items -- resume, save, load, the
 |   button layout, and return to title. Cancel and the pause button both
 |   resume immediately, whatever the cursor position. Left and right toggle
 |   the layout, but only while the cursor rests on its row.
 | Author: suinevere
 | Params: st -- state to advance; in -- this frame's input
 | Returns: at most one action
 ----------------------*/
static MenuAction stepPause(MenuState *st, const MenuInput *in)
{
	if (in->cancel || in->pause) {
		return MENU_ACT_RESUME;
	}
	if (in->up) {
		st->cursor = (st->cursor + 4) % 5;
		return MENU_ACT_NONE;
	}
	if (in->down) {
		st->cursor = (st->cursor + 1) % 5;
		return MENU_ACT_NONE;
	}
	if (st->cursor == 3 && (in->left || in->right)) {
		st->swapButtons = !st->swapButtons;
		return MENU_ACT_TOGGLE_BUTTONS;
	}
	if (in->confirm) {
		if (st->cursor == 0) {
			return MENU_ACT_RESUME;
		}
		if (st->cursor == 1 || st->cursor == 2) {
			st->returnScreen = MENU_PAUSE;
			st->screen = MENU_SLOTS;
			st->saving = (st->cursor == 1);
			st->slotCursor = 0;
			return MENU_ACT_RESCAN_SLOTS;
		}
		if (st->cursor == 3) {
			st->swapButtons = !st->swapButtons;
			return MENU_ACT_TOGGLE_BUTTONS;
		}
		st->screen = MENU_CONFIRM;
		st->pending = MENU_ACT_RETURN_TO_TITLE;
		st->confirmYes = false;
		return MENU_ACT_NONE;
	}
	return MENU_ACT_NONE;
}
```

- [ ] **Step 6: Run the tests to verify they pass**

Run: `sh saturn/tests/run_tests.sh`

Expected: `all suites passed`.

- [ ] **Step 7: Commit**

```bash
git add saturn/src/menu_state.h saturn/src/menu_state.cxx saturn/tests/test_menu_state.cxx
git commit -m "Give the pause menu a button layout row between load and return to menu"
```

---

### Task 6: Draw the row, wire the toggle, load the record at boot

**No host test exists for this task and none can be written.** `menu.cxx` and `engine.cxx` sit outside the host-testable boundary by design, and `test_menu_draw.cxx` makes no assertions about the pause screen. Every bug ever found in these files was found by reading or by running. Read the diff carefully, then hand it over.

**Files:**
- Modify: `saturn/src/menu.cxx` — `menuDrawPauseScreen` (line 439), its call in `menuRenderFrame` (line ~611), `Menu::init`, `Menu::runPause`
- Modify: `saturn/src/engine.cxx:164` — load the record at boot

**Interfaces:**
- Consumes: `Settings`, `settingsDefaults`, `settingsLoad`, `settingsStore` from Tasks 1-2; `sat_input_set_swap`, `sat_input_get_swap` from Task 4; `MENU_ACT_TOGGLE_BUTTONS`, `MenuState::swapButtons` from Task 5.
- Produces: nothing further tasks depend on.

- [ ] **Step 1: Add the include to menu.cxx**

At the top of `saturn/src/menu.cxx`, add alongside the existing includes:

```cpp
#include "settings.h"
```

Confirm `saturn_platform.h` is already included there — `menu.cxx` calls `sat_video_get_palette`, so it should be. If it is not, add it too.

- [ ] **Step 2: Grow the panel and add the two rows**

In `saturn/src/menu.cxx`, replace `menuDrawPauseScreen` in full. The panel grows from 96 to 112 tall to hold the fifth row at y=124 and the status line at y=140; the height is fixed rather than conditional so the panel does not jump when a write fails.

```cpp
/*----------------------
 | menuDrawPauseScreen
 | Description: Paints the pause panel over whatever is already in the page,
 |   which is the frozen frame remapped to monochrome. The layout row's two
 |   states are both 16 characters, against the 17 the panel allows at cell
 |   column 13 -- widen the panel before lengthening either string.
 | Author: suinevere
 | Params: page -- compositing page; st -- state, for the cursor position and
 |   the button layout; statusError -- non-OK when the layout failed to save
 | Returns: N/A
 ----------------------*/
static void menuDrawPauseScreen(uint8_t *page, const MenuState *st,
                                int statusError) {
	const uint8_t *font = Video::_font;

	menuDrawFill(page, 80, 48, 168, 112, MENU_COL_BORDER);
	menuDrawFill(page, 82, 50, 164, 108, MENU_COL_PANEL);
	menuDrawText(page, font, 13, 60, st->cursor == 0 ? MENU_BASE_SEL : MENU_BASE_DIM, "RESUME");
	menuDrawText(page, font, 13, 76, st->cursor == 1 ? MENU_BASE_SEL : MENU_BASE_DIM, "SAVE GAME");
	menuDrawText(page, font, 13, 92, st->cursor == 2 ? MENU_BASE_SEL : MENU_BASE_DIM, "LOAD GAME");
	menuDrawText(page, font, 13, 108, st->cursor == 3 ? MENU_BASE_SEL : MENU_BASE_DIM,
	             st->swapButtons ? "FIRE B  JUMP A/C" : "FIRE A/C  JUMP B");
	menuDrawText(page, font, 13, 124, st->cursor == 4 ? MENU_BASE_SEL : MENU_BASE_DIM, "RETURN TO MENU");
	menuDrawText(page, font, 11, 60 + st->cursor * 16, MENU_BASE_SEL, ">");

	if (statusError != SAT_BUP_OK) {
		menuDrawText(page, font, 13, 140, MENU_BASE_DIM, "NOT SAVED");
	}
}
```

The bare `NOT SAVED` is deliberate rather than the `menuStatusText(err, device)` helper used everywhere else: that helper's longest reachable message for an internal device, `BACKUP RAM UNFORMATTED`, is 22 characters against the panel's 17.

- [ ] **Step 3: Pass the status through**

In `menuRenderFrame`, in the `switch (st->screen)`, change:

```cpp
	case MENU_PAUSE:
		menuDrawPauseScreen(page, st);
		break;
```

to:

```cpp
	case MENU_PAUSE:
		menuDrawPauseScreen(page, st, statusError);
		break;
```

- [ ] **Step 4: Seed the menu's copy of the layout**

In `Menu::init`, after the existing body, add:

```cpp
	_st.swapButtons = sat_input_get_swap() != 0;
```

This reads the value `Engine::init` already loaded in Step 6, so the row shows the player's stored choice the first time they open the pause menu.

- [ ] **Step 5: Handle the toggle in runPause**

In `Menu::runPause`, add a branch to the action chain, immediately after the `MENU_ACT_RESCAN_SLOTS` branch:

```cpp
		} else if (act == MENU_ACT_TOGGLE_BUTTONS) {
			sat_input_set_swap(_st.swapButtons ? 1 : 0);
			Settings s;
			s.swapButtons = _st.swapButtons;
			_statusError = settingsStore(&s);
```

`sat_input_set_swap` runs before the write so the layout is live even when the write fails. The write is not deferred to the menu's exit: the failure has to be reportable, and by then there is no row left to draw under. `sat_bup_write` blocks, but the game is paused and the panel is static, so there is no animation for the stall to disturb.

- [ ] **Step 6: Load the record at boot**

In `saturn/src/engine.cxx`, add `#include "settings.h"` to the includes at the top of the file. Then replace lines 164-165:

```cpp
#ifdef __sh__
	sat_bup_init();
#endif
```

with:

```cpp
#ifdef __sh__
	sat_bup_init();
	{
		Settings s;
		settingsDefaults(&s);
		settingsLoad(&s);
		sat_input_set_swap(s.swapButtons ? 1 : 0);
	}
#endif
```

`settingsLoad`'s return value is deliberately ignored: it leaves `s` untouched on every failure path, so a missing or unreadable record simply keeps the defaults `settingsDefaults` just wrote. A first boot has no record and that is not an error.

- [ ] **Step 7: Run the host tests**

Run: `sh saturn/tests/run_tests.sh`

Expected: `all suites passed`. No suite compiles `menu.cxx` or `engine.cxx`, so this confirms only that nothing else broke. **Green here says nothing about whether B actually jumps.**

- [ ] **Step 8: Commit**

```bash
git add saturn/src/menu.cxx saturn/src/engine.cxx
git commit -m "Draw the button layout row on the pause menu and persist the choice when it changes"
```

- [ ] **Step 9: Hand over for testing on hardware**

Do not build. Report to the human that the work is ready and list what to check on the emulator:

1. Default layout: A and C fire, B jumps, D-pad Up still jumps.
2. Pause, move down to the fourth row, confirm — the row flips to `FIRE B  JUMP A/C`.
3. Resume: B now fires, A and C now jump, D-pad Up still jumps.
4. Left and right also flip the row; on the other four rows they do nothing.
5. Menu buttons are unchanged in both layouts: A and C confirm, B cancels.
6. Power cycle: the chosen layout is still in effect, and the row still shows it.
7. The panel is 16 pixels taller than before; check the fifth row and the bottom border sit comfortably over the frozen frame.

---

## Self-Review

**Spec coverage.** Walked the spec section by section:

| Spec section | Task |
|---|---|
| Two bindable actions, one bit | 1 |
| Record layout, magic, version, reserved | 1 |
| `settingsDefaults` / `Pack` / `Unpack` | 1 |
| `AW_CFG`, internal device only | 2 |
| `settingsLoad` / `settingsStore`, store returns `SAT_BUP_*` | 2 |
| The mapping table | 3 |
| `processEvents` is the seam; menu buttons untouched | 4 |
| `SAT_PAD_ACTION` removed | 4 |
| Row at index 3, `RETURN TO MENU` to 4, `% 4` → `% 5` | 5 |
| `MENU_ACT_TOGGLE_BUTTONS`, confirm and left/right | 5 |
| `swapButtons` preserved by the `Enter*` functions | 5 |
| Row text, 17-column budget, panel 96 → 112 | 6 |
| `NOT SAVED` status line | 6 |
| Immediate write, not deferred | 6 |
| Load at `engine.cxx:164` inside `#ifdef __sh__` | 6 |
| `Menu::init` seeds `swapButtons` | 6 |
| Test coverage listed in the spec | 1, 2, 3, 5 |
| Out of scope: X/Y/Z, title row, second preference | not implemented, correctly |

No gaps.

**Placeholder scan.** No `TBD`, no `TODO`, no "similar to Task N", no "add appropriate error handling". Every code step carries the actual code. The one judgement call left to the implementer — the exact wording of the `SAT_PAD_A / _B / _C` comment block in Task 4 Step 1 — is given as literal replacement text.

**Type consistency.** Checked across tasks: `settingsStore` returns `int` in its Task 2 declaration, its Task 2 implementation, and its Task 6 call site assigning to `_statusError` (which is `int`, `menu.h:33`). `settingsLoad` returns `bool` in all three places. `sat_input_set_swap` takes `int` in Task 4 and is called with `_st.swapButtons ? 1 : 0` and `s.swapButtons ? 1 : 0` in Task 6, never with a bare `bool`. `settingsMapFaceButtons`'s six parameters match between Task 3's declaration, implementation, tests, and Task 4's call. `MENU_ACT_TOGGLE_BUTTONS` is spelled identically in Tasks 5 and 6. Pause cursor index 3 means the layout row and 4 means return-to-menu in `stepPause`, in `menuDrawPauseScreen`, and in every test.
