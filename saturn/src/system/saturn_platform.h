/*----------------------
 | saturn_platform.h
 | Description: The C face of everything Saturn-specific that the System backend
 |   needs: boot, video, input and time. saturn_system.cxx implements the engine's
 |   abstract System against this, so that file never includes <srl.hpp> -- the
 |   engine's headers wrap SGL's C headers in extern "C" (see intern.h) and mixing
 |   that with SRL's C++ headers in one translation unit is fragile. Everything
 |   here is implemented in saturn_platform.cxx, which includes only SRL.
 | Author: suinevere
 | Dependencies: stdint.h
 ----------------------*/
#ifndef SATURN_PLATFORM_H
#define SATURN_PLATFORM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | sat_boot_init
 | Description: Brings up SRL. MUST be the first thing main() does: malloc and
 |   operator new are routed onto SRL's TLSF arena (see saturn_compat.cxx), and
 |   that arena does not exist until SRL::Core::Initialize has run. Anything that
 |   allocates before this call faults.
 |
 |   It reasserts the machine state the IP hands over before it does any of that,
 |   so this program starts the same way whether the BIOS launched it or another
 |   program loaded it over itself and jumped to 0x06004000 -- see
 |   sat_boot_sanitize in the .cxx for what survives such a hand-over and why.
 | Author: suinevere
 ----------------------*/
void sat_boot_init(void);

/*----------------------
 | sat_video_init
 | Description: Configures NBG0 as a 320x200 16-colour bitmap layer and enables
 |   it. Call once, after sat_boot_init.
 | Author: suinevere
 ----------------------*/
void sat_video_init(void);

/*----------------------
 | sat_video_set_palette
 | Description: Caches the engine's 16-colour palette and marks it pending; the
 |   CRAM write happens at the next sat_video_present or sat_video_sync.
 | Author: suinevere
 | Params: colors -- 32 bytes, 16 entries of 2 bytes, 4 bits per channel:
 |   R = byte0 & 0x0F, G = (byte1 & 0xF0) >> 4, B = byte1 & 0x0F
 |
 | NOTE: do not name this parameter `pal`. SGL's sgl.h has `#define pal COL_32K`,
 | so a parameter by that name expands to `(2 - 1)` and the compiler reports the
 | resulting syntax error against sl_def.h, not against this file.
 ----------------------*/
void sat_video_set_palette(const uint8_t *colors);

/*----------------------
 | sat_video_get_palette
 | Description: Hands back the last palette given to sat_video_set_palette, in
 |   the same 4-bits-per-channel form. Lets the menu snapshot the game's live
 |   palette, install a dimmed copy behind the pause screen, and restore it.
 | Author: suinevere
 | Params: out -- destination, must hold 32 bytes
 ----------------------*/
void sat_video_get_palette(uint8_t *out);

/*----------------------
 | sat_video_present
 | Description: Waits for the next vblank, then flushes any pending palette and
 |   DMAs one finished engine page to the screen before the beam resumes.
 | Author: suinevere
 | Params: page -- 32,000 bytes, 320x200 at 4bpp, high nibble = left pixel
 ----------------------*/
void sat_video_present(const uint8_t *page);

/*----------------------
 | sat_video_sync
 | Description: Waits one vblank without touching the framebuffer, flushing any
 |   pending palette so a caller can hold an already-presented frame for more
 |   than one field without stranding it.
 | Author: suinevere
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void sat_video_sync(void);

/*----------------------
 | SAT_PAD_*
 | Description: Bits returned by sat_input_read. Bit 4 is deliberately vacant:
 |   it was SAT_PAD_ACTION, which went when the layout made the action a
 |   derived signal rather than a pad one, and the numbering below it was left
 |   alone rather than shifted. Not an off-by-one.
 | Author: suinevere
 ----------------------*/
#define SAT_PAD_UP     (1u << 0)
#define SAT_PAD_DOWN   (1u << 1)
#define SAT_PAD_LEFT   (1u << 2)
#define SAT_PAD_RIGHT  (1u << 3)
#define SAT_PAD_PAUSE  (1u << 5)  /* Start */

/*----------------------
 | SAT_PAD_A / _B / _C / _L / _R
 | Description: Individual buttons, for menus that must tell confirm from
 |   cancel. Gameplay's action and jump are resolved from A, B and C by
 |   settingsMapFaceButtons according to the player's chosen layout.
 | Author: suinevere
 ----------------------*/
#define SAT_PAD_A      (1u << 6)
#define SAT_PAD_B      (1u << 7)
#define SAT_PAD_C      (1u << 8)
#define SAT_PAD_L      (1u << 9)
#define SAT_PAD_R      (1u << 10)

/*----------------------
 | sat_input_read
 | Description: Reads pad 1 and returns the SAT_PAD_* bits currently held.
 | Author: suinevere
 ----------------------*/
uint32_t sat_input_read(void);

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

/*----------------------
 | sat_loading_tick
 | Description: What a blocking load calls between windows so the machine does
 |   not go deaf while the drive works. Runs the audio pump and latches whatever
 |   the pad is holding, which the next sat_input_read folds in -- so a button
 |   pressed and released inside a window is delivered rather than lost. Reads
 |   nothing back; the latch is invisible to callers.
 | Author: suinevere
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void sat_loading_tick(void);

/*----------------------
 | sat_time_ms
 | Description: Milliseconds since boot, counted from vblanks. The engine uses
 |   this only for frame pacing (vm.cxx), so vblank resolution is what it wants
 |   anyway.
 | Author: suinevere
 ----------------------*/
uint32_t sat_time_ms(void);

/*----------------------
 | sat_sleep_ms
 | Description: Waits roughly the requested milliseconds, synchronising each
 |   frame so the display keeps running while the engine idles.
 | Author: suinevere
 ----------------------*/
void sat_sleep_ms(uint32_t ms);

#ifdef __cplusplus
}
#endif
#endif /* SATURN_PLATFORM_H */
