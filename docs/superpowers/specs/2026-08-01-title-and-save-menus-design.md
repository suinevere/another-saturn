# Another World → Sega Saturn — Title Card, Save/Load Menus and Pause Menu Design Spec

**Date:** 2026-08-01
**Status:** Approved, ready to plan
**Target engine:** SaturnRingLib (SRL)

## Goal

Give the port a front end: a title card that offers **Start Game** and **Load
Game**, a pause menu that offers **Resume**, **Save Game**, **Load Game** and
**Return to Menu**, and three save slots backed by Saturn backup RAM. When a
backup cartridge is present the player picks which device the slots live on.

## The constraint that shapes everything

`Engine::saveGameState` (engine.cxx:120) serialises the whole engine, and
`Video::saveOrLoad` (video.cxx:594) contributes four full video pages:

| Contributor | Bytes |
|---|---|
| `VirtualMachine::saveOrLoad` | 1408 |
| `Resource::saveOrLoad` | ~82 |
| `Video::saveOrLoad` — page mask and palette ids | 3 |
| `Video::saveOrLoad` — 4 × `VID_PAGE_SIZE` | **128000** |
| `SfxPlayer` + `Mixer` | ~40 |
| Header, as written today | 40 |
| **Total** | **~129.5 KB** |

Saturn internal backup RAM is 32 KB, of which roughly 29 KB is usable. A single
full slot does not fit, let alone three.

**Decision: drop the video pages from the save.** A lean save is ~1.5 KB, so
three slots occupy about 5 KB of 29 KB. The four alternatives considered —
RLE-compressing the pages, saving only the background page, requiring a backup
cartridge, and checkpoint-style part-and-variables saves — were all rejected:
the first three carry a soft or hard failure mode on a stock console, and the
last cannot restore mid-part state.

**Accepted cost:** immediately after a load, the video pages hold no content.
The scene background is whatever the VM next draws. The lean format cannot
recover a background that was painted by bytecode which has already run.

## Save format — version 3

`Serializer` already gates entries on `minVer`/`maxVer`: `saveEntries` writes an
entry only when `entry->maxVer == CUR_VER` (serializer.cxx:44) and `loadEntries`
reads one only when `_saveVer` falls in `[minVer, maxVer]` (serializer.cxx:74).
That mechanism does exactly what is needed here.

- `Serializer::CUR_VER` goes 2 → 3.
- In `Video::saveOrLoad`, the four `SE_ARRAY(_pages[n], ...)` entries are
  hand-written with `maxVer = 2` in place of the macro's `CUR_VER`.

No new save path, no branching in the serialiser, and a pre-existing version 2
save still reads its pages back. The page mask and palette ids keep the macro's
`CUR_VER`, so `_curPagePtr1/2/3` still restore correctly.

`Resource::saveOrLoad` re-reads banks from CD on load (resource.cxx:415), so no
asset bytes are stored. The palette resource comes back with them, which is why
`changePal(currentPaletteId)` at video.cxx:622 still works.

On a version 3 load, all four video pages are cleared to colour index 0 rather
than left holding the pre-load screen.

### On-disc layout of a slot

```
offset  size  field
0       4     'AWSV'
4       2     version (3)
6       2     reserved (0)
8       32    description text
40      2     part id            -- for the chapter name
42      4     BUP date stamp     -- for the timestamp
46      2     reserved
48      ...   serialised engine state
```

The part id and date stamp live in the header so the slot list can be built
without deserialising engine state. This grows the header from 40 bytes to 48;
a lean slot is therefore about 1580 bytes.

## Components

### `saturn/src/system/saturn_backup.h` / `.cxx`

A plain C interface over SGL's `BUP_*` vector table (`sega_bup.h`) and
`SRL::Types::DateTime::Now()`. Follows the `saturn_cdfile` pattern exactly: the
`.h` is C-only so engine translation units never include `<srl.hpp>`, and the
`.cxx` is the only place SRL and SGL meet.

```c
#define SAT_BUP_INTERNAL 1   /* BUP_MAIN_UNIT */
#define SAT_BUP_CART     2   /* BUP_CURTRIDGE */

typedef struct {
    int      present;
    int      formatted;
    int      writeProtected;
    uint32_t freeBytes;
} SatBupDev;

typedef struct {
    int      exists;
    uint32_t size;
    uint32_t date;      /* BUP date word */
} SatBupEntry;

void    sat_bup_init(void);
int     sat_bup_probe(uint32_t device, SatBupDev *out);
int     sat_bup_dir(uint32_t device, const char *name, SatBupEntry *out);
int     sat_bup_read(uint32_t device, const char *name, void *dst, int32_t size);
int     sat_bup_write(uint32_t device, const char *name, const char *comment,
                      const void *src, int32_t size, int overwrite);
int     sat_bup_delete(uint32_t device, const char *name);
uint32_t sat_bup_date_now(void);
void    sat_bup_date_split(uint32_t date, int *month, int *day, int *hour, int *min);
```

Every entry point takes the device explicitly. There is no implicit "current
device" anywhere in this layer.

`BUP_Init` takes `BupConfig tp[3]`, one config per device, so a single call
covers both. `sat_bup_probe` reports `present = 0` when `BUP_Stat` returns
`BUP_NON`, and `formatted = 0` when it returns `BUP_UNFORMAT`.

### `saturn/src/savedata.h` / `.cxx`

Slot naming, header read/write, and slot probing. Bridges `Engine` and
`saturn_backup`; contains no menu code and no SGL calls.

```c++
enum { SAVE_NUM_SLOTS = 3 };

enum SlotState {
    SLOT_EMPTY,
    SLOT_OK,
    SLOT_DAMAGED,      // BUP_BROKEN, or magic mismatch
    SLOT_OLD_VERSION   // version < 3
};

struct SlotInfo {
    SlotState state;
    uint16_t  partId;
    uint32_t  date;
};
```

BUP filenames are `AW_SAVE1` … `AW_SAVE3`, within the 11-byte BUP limit.

A `chapterName(partId)` table maps `GAME_PART2` … `GAME_PART10` to the names
shown in the slot list.

### `saturn/src/file.cxx` — memory-backed `File_impl`

`File` dispatches through a `File_impl` pointer (file.h:26), so a memory impl
lets the existing `saveOrLoad` machinery write straight into a BUP staging
buffer with no changes to `Engine::saveGameState` beyond where the stream comes
from.

```c++
bool File::openMemory(void *buf, uint32_t size, bool write);
```

Reads and writes past the end set the I/O error flag, which `Engine` already
checks (engine.cxx:141).

### `saturn/src/menu_draw.h` / `.cxx`

Drawing primitives against a raw 4bpp page buffer:

- `fillRect(buf, x, y, w, h, color)`
- `drawText(buf, x, y, color, const char *)` — built on `Video::drawChar`, which
  already takes a destination buffer (video.h:97)
- `dimPalette(const uint8_t *src, uint8_t *dst, uint8_t keepIndex)` — halves all
  16 entries and forces `keepIndex` to full white

Kept separate from `menu.cxx` so neither file grows past the size where it stops
being reviewable in one pass.

### `saturn/src/menu.h` / `.cxx`

The screen state machine: title card, pause menu, slot list, confirm prompt.
Owns one dedicated 32 KB page that is never one of `Video::_pages`, so the VM's
state is untouched while a menu is up.

## Rendering

**Title card.** No part is loaded, so there is no engine palette. The menu
clears its page to index 0 and writes its own 16-colour table directly through
`sys->setPalette`. Format is 4 bits per channel, two bytes per entry, per the
contract documented at saturn_platform.h:43.

```
+----------------------------------------+
|                                        |
|          ANOTHER  WORLD                |
|                                        |
|            > START GAME                |
|              LOAD GAME                 |
|                                        |
+----------------------------------------+
```

**Pause.** The last presented page is copied into the menu page. The game's
current palette is saved, and a dimmed copy is installed with index 15 forced to
full white. The menu panel is filled with index 0 — near-black once dimmed — and
text is drawn in index 15. Resuming reinstalls the saved palette.

Frozen-frame pixels that already used index 15 stay bright rather than dimming.
This is cosmetic and confined to the backdrop behind the panel.

```
+----------------------------------------+
| .::  (frozen frame, dimmed)  ::.       |
|      +------------------+              |
|      |  > RESUME        |              |
|      |    SAVE GAME     |              |
|      |    LOAD GAME     |              |
|      |    RETURN TO MENU|              |
|      +------------------+              |
+----------------------------------------+
```

**Slot list.** Three rows showing chapter name and timestamp. The device row
above them appears only when a cartridge is detected.

```
+----------------------------------------+
|              LOAD GAME                 |
|                                        |
|      L <  CARTRIDGE  > R               |
|      ----------------------            |
|  > 1  THE JAIL        08/01 21:14      |
|    2  - EMPTY -                        |
|    3  - EMPTY -                        |
|                                        |
|   A SELECT     B BACK                  |
+----------------------------------------+
```

## Backup device selection

The menu holds one session-scoped `_device`, shared by the Save and Load
screens.

- Both devices are probed once at menu init.
- Row visibility keys on `present`, not on usability: a cartridge that is
  unformatted or write-protected still shows its row, carrying the message from
  the error table. Only an absent cartridge hides the row and pins `_device` to
  internal. A stock console sees a plain three-slot screen.
- Default is whichever device already holds Another World saves. Both or
  neither → internal. A player who has been saving to a cartridge lands on it
  without touching the selector.
- L/R toggles the device. Slot info is re-probed on toggle, so the three rows
  always describe the device a confirm would write to.
- The choice is not persisted. Persisting it would require choosing a device to
  store it in, which is the question being answered.

## Input

`sat_input_read` currently merges A, B and C into `SAT_PAD_ACTION`
(saturn_platform.cxx:231), so menus cannot tell confirm from cancel.

- `SAT_PAD_A`, `SAT_PAD_B`, `SAT_PAD_C`, `SAT_PAD_L`, `SAT_PAD_R` are added.
- `SAT_PAD_ACTION` stays as the union of A, B and C, so gameplay input is
  unchanged.
- `PlayerInput` gains `menuConfirm`, `menuCancel`, `menuLeft`, `menuRight`.

`SRL::Input::Digital::Button` already exposes `L` and `R`. The engine never
reads the shoulders, so nothing in gameplay is affected.

Edge detection and D-pad auto-repeat live in `menu.cxx`: menus are driven by
transitions, and the engine's `PlayerInput` is level-triggered.

## Flow

`Engine::run` becomes a three-state loop.

```
APP_TITLE    START GAME -> vm.initForPart(GAME_PART2) -> APP_PLAYING
             LOAD GAME  -> slot list -> loadSlot()    -> APP_PLAYING

APP_PLAYING  vm.checkThreadRequests / inp_updatePlayer / processInput / hostFrame
             Start pressed -> snapshot page, dim palette -> APP_PAUSED

APP_PAUSED   RESUME         -> restore palette -> APP_PLAYING
             SAVE GAME      -> slot list -> confirm if occupied -> saveSlot()
                            -> back to the pause menu, stays APP_PAUSED
             LOAD GAME      -> slot list -> loadSlot()
                            -> restore palette -> APP_PLAYING
             RETURN TO MENU -> confirm -> vm.initForPart(GAME_PART2) -> APP_TITLE
```

The busy-wait pause at vm.cxx:642 is deleted. `inp_updatePlayer` sets a pause
request that `run` consumes on the next iteration.

`VirtualMachine::initForPart` is already re-entrant — it stops the player and
mixer and calls `res->setupPart` (vm.cxx:381) — so Return to Menu needs no
teardown of its own.

`Engine::processInput`'s quicksave and slot-cycling hooks (engine.cxx:97) are
removed; `sys->input.save`, `input.load` and `input.stateSlot` go with them, and
`Engine::_stateSlot` is replaced by the menu's slot selection.

The build already defines `BYPASS_PROTECTION` (saturn/makefile:49), so Start
Game begins at `GAME_PART2`, the intro, not the copy-protection wheel.

## Confirmations

One shared confirm widget, defaulting to **NO**, used for both destructive
actions:

```
+----------------------------------------+
|      +--------------------------+      |
|      |  OVERWRITE SLOT 2 ?      |      |
|      |      YES      > NO       |      |
|      +--------------------------+      |
+----------------------------------------+
```

- Overwriting an occupied slot.
- Return to Menu, worded `RETURN TO MENU ? UNSAVED PROGRESS WILL BE LOST`.

These are the only two paths that can destroy a run.

## Error handling

Slot-level failures are shown in place of the slot's label rather than in a
dialog:

| Condition | Shown |
|---|---|
| No such BUP file | `- EMPTY -` |
| `BUP_BROKEN`, or magic mismatch | `- DAMAGED -` |
| Header version < 3 | `- OLD SAVE -` |

No version 2 save can physically exist in backup RAM — at 129 KB it never fit —
so `- OLD SAVE -` is a guard against a future format change, not a case that
occurs today. The version 2 load path retained in `Video::saveOrLoad` exists for
the same reason, and is exercised only by the host tests.

Write failures surface as a single line under the slot list:

| Condition | Shown |
|---|---|
| `BUP_NOT_ENOUGH_MEMORY` | `NOT ENOUGH SPACE` |
| `BUP_WRITE_PROTECT` | `CARTRIDGE WRITE PROTECTED` |
| `BUP_UNFORMAT`, internal | `BACKUP RAM UNFORMATTED` |
| `BUP_UNFORMAT`, cartridge | `CARTRIDGE UNFORMATTED`, slots greyed |

The design never offers to format a device. Formatting would destroy other
games' saves, and the Saturn's own Backup Manager already does it with the
warnings that action deserves.

A `- DAMAGED -` or `- OLD SAVE -` slot can still be selected in the Save screen
and overwritten, behind the usual confirm. It cannot be selected in the Load
screen.

## Testing

`saturn/tests` compiles host-side (`run_tests.sh`), which covers everything
except BUP itself.

- **Lean save round-trip.** Save through `MemoryFile_impl`, mutate engine state,
  load, compare. Confirms the ver-3 state is sufficient to restore the VM.
- **Version gating.** A synthetic version 2 blob still loads its four pages; a
  version 3 blob skips them and leaves the pages at their cleared value.
- **Save size.** Assert a version 3 save is under 2 KB, so the three-slot budget
  cannot silently regress.
- **Device defaulting.** Pure logic over probe results, against a stubbed
  `saturn_backup`: cart absent, cart present and empty, cart present with saves,
  both with saves.
- **Menu state machine.** Synthetic input sequences drive title → slot list →
  confirm → back, asserting the resulting transitions and the selected slot.

BUP itself is verified on hardware and in emulation only. `saturn_backup` is
stubbed for the host build.

## Known risks

**BUP work buffer placement.** `BUP_Init` needs a work area, and the BIOS backup
library lives in Low Work RAM from `0x6000000`, which is also where the 600 KB
resource block sits. Verify there is no overlap before wiring it up; if there
is, move the work buffer to High Work RAM.

**Menu page allocation.** The 32 KB compositing page is one more High Work RAM
allocation alongside the existing video pages. Confirm the budget before adding
it.

**Post-load background.** The accepted cost of the lean format. Worth a
listening-and-looking pass on hardware to see how long a missing background
persists in practice for each part, since the answer varies by how soon the
bytecode next repaints.

## Out of scope

- Compressing or restoring video pages in the save.
- Formatting a backup device from within the game.
- Persisting the device choice across sessions.
- Reviving the stale root `CMakeLists.txt` host build, which references
  `src/*.cpp` files that no longer exist.
- Any change to the code/password entry screen.
