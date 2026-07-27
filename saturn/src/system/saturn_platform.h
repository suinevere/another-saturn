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
 | Description: Uploads the engine's 16-colour palette to CRAM.
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
 | sat_video_present
 | Description: Pushes one finished engine page to the screen and waits for the
 |   next frame.
 | Author: suinevere
 | Params: page -- 32,000 bytes, 320x200 at 4bpp, high nibble = left pixel
 ----------------------*/
void sat_video_present(const uint8_t *page);

/*----------------------
 | SAT_PAD_*
 | Description: Bits returned by sat_input_read.
 | Author: suinevere
 ----------------------*/
#define SAT_PAD_UP     (1u << 0)
#define SAT_PAD_DOWN   (1u << 1)
#define SAT_PAD_LEFT   (1u << 2)
#define SAT_PAD_RIGHT  (1u << 3)
#define SAT_PAD_ACTION (1u << 4)  /* A / B / C -- run and shoot */
#define SAT_PAD_PAUSE  (1u << 5)  /* Start */

/*----------------------
 | sat_input_read
 | Description: Reads pad 1 and returns the SAT_PAD_* bits currently held.
 | Author: suinevere
 ----------------------*/
uint32_t sat_input_read(void);

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
