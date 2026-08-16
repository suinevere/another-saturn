/*----------------------
 | saturn_platform.cxx
 | Description: The SRL side of the port: SRL bring-up, the NBG0 bitmap layer the
 |   engine's framebuffer is pushed to, pad reading, and a vblank-counted clock.
 |   This is the only file besides saturn_cdfile.cxx that includes <srl.hpp>.
 |
 |   Video approach (see docs/superpowers/specs/2026-07-27-another-world-video-
 |   backend-design.md): the engine rasterizes in software into its own 320x200
 |   4bpp pages and hands one over finished. NBG0 is configured as a 16-colour
 |   bitmap and each frame is DMA'd in line by line. Nothing is drawn by the
 |   hardware; VDP1 is not used at all.
 | Author: suinevere
 | Dependencies: saturn_platform.h, SRL (Core, VDP2, CRAM, Input, Bitmap)
 ----------------------*/
#include <srl.hpp>
#include <sega_sys.h>
#include "saturn_platform.h"
#include "saturn_audio.h"

using namespace SRL::Types;

/*----------------------
 | SCREEN_W / SCREEN_H / PAGE_PITCH / VRAM_PITCH / SCREEN_TOP
 | Description: The engine's frame is 320x200 at 4bpp, so a source line is 160
 |   bytes. VDP2 bitmap layers are a fixed 512 pixels wide whatever the image is,
 |   which at 4bpp is a 256-byte stride -- source and destination pitches differ,
 |   which is why the blit is per-line rather than one copy. SCREEN_TOP centres
 |   200 lines in NTSC's 224, letterboxing 12 lines top and bottom rather than
 |   scaling (scaling a 16-colour image bands badly on this game's flat art).
 | Author: suinevere
 ----------------------*/
#define SCREEN_W     320
#define SCREEN_H     200
#define PAGE_PITCH   (SCREEN_W / 2)
#define VRAM_PITCH   (512 / 2)
#define SCREEN_TOP   12

/*----------------------
 | g_palette / g_paletteColors
 | Description: The 16 live colours, kept in work RAM so a BitmapInfo can point
 |   at them during setup. CRAM holds the copy the hardware actually reads.
 | Author: suinevere
 ----------------------*/
static HighColor g_paletteColors[16];
static SRL::Bitmap::Palette g_palette(g_paletteColors, 16);

/*----------------------
 | g_paletteRaw
 | Description: The last palette handed to sat_video_set_palette, kept in the
 |   engine's own 4-bits-per-channel form rather than the converted HighColor
 |   one, so sat_video_get_palette can return exactly what was written.
 | Author: suinevere
 ----------------------*/
static uint8_t g_paletteRaw[32];

/*----------------------
 | g_paletteDirty
 | Description: Set by sat_video_set_palette when g_paletteColors holds a
 |   conversion not yet written to CRAM; cleared by sat_video_flush_palette.
 | Author: suinevere
 ----------------------*/
static bool g_paletteDirty = false;

/*----------------------
 | g_vram
 | Description: Where NBG0's bitmap lives in VDP2 VRAM, captured after setup so
 |   each frame can be written straight there.
 | Author: suinevere
 ----------------------*/
static uint8_t *g_vram = nullptr;

/*----------------------
 | g_frames
 | Description: Vblank counter behind sat_time_ms.
 | Author: suinevere
 ----------------------*/
static volatile uint32_t g_frames = 0;

/*----------------------
 | AnotherWorldCanvas
 | Description: The IBitmap SRL wants at setup time, describing the engine's
 |   frame: 320x200, 16 colours. It is only used to establish the layer -- its
 |   data pointer is a blank page, because from then on frames are DMA'd in
 |   directly rather than going back through LoadBitmap (which would reallocate
 |   VRAM and the palette bank every time).
 | Author: suinevere
 ----------------------*/
class AnotherWorldCanvas : public SRL::Bitmap::IBitmap
{
private:
    uint8_t *data;

public:
    AnotherWorldCanvas(uint8_t *blank) : data(blank) { }

    uint8_t *GetData() override
    {
        return this->data;
    }

    SRL::Bitmap::BitmapInfo GetInfo() const override
    {
        return SRL::Bitmap::BitmapInfo(SCREEN_W, SCREEN_H, (SRL::Bitmap::Palette *)&g_palette);
    }
};

/*----------------------
 | onVblank
 | Description: Advances the frame counter. Registered with SRL::Core::OnVblank.
 |
 |   It used to clock the PCM stream driver and refill the audio ring as well.
 |   Neither exists now: the SCSP plays from its own memory and needs nothing
 |   per frame.
 | Author: suinevere
 ----------------------*/
static void onVblank()
{
    g_frames++;
}

/*----------------------
 | SAT_SMPC_COMREG / SAT_SMPC_SF / SAT_SMPC_SSHOFF / SAT_SMPC_TRIES
 | Description: The SMPC's command port, its busy flag, the code that powers the
 |   slave processor down, and how many polls to give it.
 |
 |   Written at the port rather than through slSlaveOffWait, because that reaches
 |   slRequestCommand, which takes a semaphore before doing anything and returns
 |   having done nothing when it cannot have it -- and SGL reads the pads through
 |   that same port every frame, so it routinely cannot.
 | Author: suinevere
 ----------------------*/
#define SAT_SMPC_COMREG (*(volatile uint8_t *)0x2010001Fu)
#define SAT_SMPC_SF     (*(volatile uint8_t *)0x20100063u)
#define SAT_SMPC_SSHOFF 0x03u
#define SAT_SMPC_TRIES  100000u

/*----------------------
 | SAT_UINT_FIRST / SAT_UINT_LAST
 | Description: The SCU interrupt vectors whose BIOS hooks are let go of on the
 |   way in. The hooks live at 0x06000900 + vector * 4, in the BIOS work area
 |   below 0x06004000 -- which is below this program, so a host that loaded us
 |   over itself cannot have overwritten them and they still name its handlers.
 |   slInitSystem lifts the interrupt mask partway through its own run and only
 |   re-hooks afterwards, so the first vblank inside that window is dispatched
 |   into whatever this image put at the host's addresses.
 | Author: suinevere
 ----------------------*/
#define SAT_UINT_FIRST 0x40u
#define SAT_UINT_LAST  0x5fu

/*----------------------
 | SAT_SCU_DSP_CTRL / SAT_DMAC_CHCR0 / SAT_DMAC_STRIDE / SAT_DMAC_DRCR0 /
 | SAT_DMAC_DMAOR / SAT_DIVU_CONT
 | Description: The devices smpsys.c's msh2PeriInit and scuDspInit quiet before
 |   the IP jumps to 0x06004000. Addresses are its, verbatim.
 | Author: suinevere
 ----------------------*/
/*----------------------
 | SAT_FRT_TIER / SAT_IPRB / SAT_IPRB_FRT_MASK
 | Description: The free-running timer's interrupt enables, and the priority
 |   register that gates them.
 |
 |   This is the one piece of inherited state SYS_SETUINT cannot reach.
 |   SRL::Timer::Init routes the FRT overflow to vector 0x66 and installs its
 |   handler by writing the SH-2 vector table at VBR + 0x66 * 4 directly, not
 |   through the BIOS's user-hook table -- so a host's handler address survives
 |   the hooks being cleared. The FRT itself keeps counting across the hand-over
 |   at priority 15, and SRL::Core::Initialize does not call Timer::Init until
 |   *after* Sound::Hardware::Initialize has read two files off the disc. Every
 |   overflow in that window lands on the host's address, which by then holds
 |   whatever this image put there.
 |
 |   Turning the source off is enough: Timer::Init disables, reconfigures,
 |   reinstalls and re-enables it, so nothing here has to guess at the vector.
 | Author: suinevere
 ----------------------*/
#define SAT_FRT_TIER      (*(volatile uint8_t *)0xFFFFFE10u)
#define SAT_IPRB          (*(volatile uint16_t *)0xFFFFFE60u)
#define SAT_IPRB_FRT_MASK 0xF0FFu

#define SAT_SCU_DSP_CTRL (*(volatile uint32_t *)0x25FE0080u)
#define SAT_DMAC_CHCR0   0xFFFFFF8Cu
#define SAT_DMAC_STRIDE  0x10u
#define SAT_DMAC_DRCR0   0xFFFFFE71u
#define SAT_DMAC_DMAOR   (*(volatile uint32_t *)0xFFFFFFB0u)
#define SAT_DIVU_CONT    (*(volatile uint32_t *)0xFFFFFFB8u)

/*----------------------
 | sat_boot_sanitize
 | Description: Puts the console back into the state the IP hands to a program at
 |   0x06004000, so this build starts the same way whether the BIOS launched it
 |   or another program loaded it over itself and jumped here.
 |
 |   Everything it touches is state that survives such a hand-over: the BIOS work
 |   area below 0x06004000, the second processor, the free-running timer, and the
 |   DMA and DSP engines, none of which live in the image a loader overwrites.
 |   smpsys.c does most of it -- the hooks at its lines 156-157, the rest in
 |   msh2PeriInit and scuDspInit -- and it is all a no-op on a cold boot, where
 |   the IP has just done it and the FRT is not raising interrupts yet.
 |
 |   The timer goes first because it is the only one of these that can fire on
 |   its own before the next statement runs.
 |
 |   Sound is left alone deliberately: SRL::Sound::Hardware::Initialize brackets
 |   itself with slSoundOffWait and slSoundOnWait, so the M68K is reset and
 |   restarted whatever state it arrives in, and the IP leaves it running.
 |
 |   What this cannot repair is a slave still executing the host's code while the
 |   host overwrote it -- by the time anything here runs, that has already
 |   happened. Halting it is a loader's job; halting it again here only bounds the
 |   damage.
 | Author: suinevere
 | Dependencies: sega_sys.h
 | Globals: N/A
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void sat_boot_sanitize(void)
{
    uint32_t spin;
    uint32_t i;

    SAT_FRT_TIER = 0u;
    SAT_IPRB = (uint16_t)(SAT_IPRB & SAT_IPRB_FRT_MASK);

    for (spin = 0; spin < SAT_SMPC_TRIES && (SAT_SMPC_SF & 1u) != 0u; spin++)
    {
    }

    if ((SAT_SMPC_SF & 1u) == 0u)
    {
        SAT_SMPC_SF = 1u;
        SAT_SMPC_COMREG = SAT_SMPC_SSHOFF;

        for (spin = 0; spin < SAT_SMPC_TRIES && (SAT_SMPC_SF & 1u) != 0u; spin++)
        {
        }
    }

    SAT_SCU_DSP_CTRL = 0u;

    for (i = 0u; i < 2u; i++)
    {
        volatile uint32_t *chcr =
            (volatile uint32_t *)(SAT_DMAC_CHCR0 + i * SAT_DMAC_STRIDE);

        (void)*chcr;
        *chcr = 0u;
        *(volatile uint8_t *)(SAT_DMAC_DRCR0 + i) = 0u;
    }

    (void)SAT_DMAC_DMAOR;
    SAT_DMAC_DMAOR = 0u;
    SAT_DIVU_CONT = 0u;

    for (i = SAT_UINT_FIRST; i <= SAT_UINT_LAST; i++)
    {
        SYS_SETUINT(i, 0);
    }

    SYS_SETSCUIM(0xFFFFFFFFu);
}

extern "C" void sat_boot_init(void)
{
    sat_boot_sanitize();

    // Black backdrop: the engine's 200 lines are centred in NTSC's 224, so the
    // 12 lines above and below are backdrop and want to read as letterboxing.
    SRL::Core::Initialize(HighColor(0, 0, 0));
    SRL::Core::OnVblank += onVblank;
}

extern "C" void sat_video_init(void)
{
    // A blank page just to give LoadBitmap something to establish the layer
    // from; freed immediately after, since real frames are DMA'd in later.
    // Taken from Low Work RAM rather than High: it is transient and never read
    // by anything time-critical, and allocating-then-freeing 32 KB in the fast
    // bank punches a hole that the 128 KB of video pages allocated moments
    // later cannot fit into, leaving High Work RAM fragmented for the rest of
    // the run. Nothing needs that hole; better not to make it.
    uint8_t *blank = (uint8_t *)SRL::Memory::LowWorkRam::Malloc(PAGE_PITCH * SCREEN_H);

    if (blank == nullptr)
    {
        SRL::Debug::Print(1, 1, "video: no room for setup page");
        return;
    }

    for (int32_t i = 0; i < PAGE_PITCH * SCREEN_H; i++)
    {
        blank[i] = 0;
    }

    for (int32_t i = 0; i < 16; i++)
    {
        g_paletteColors[i] = HighColor(0, 0, 0);
    }

    AnotherWorldCanvas canvas(blank);
    SRL::VDP2::NBG0::LoadBitmap((SRL::Bitmap::IBitmap *)&canvas);

    g_vram = (uint8_t *)SRL::VDP2::NBG0::GetCellAddress();

    SRL::VDP2::NBG0::SetPriority(SRL::VDP2::Priority::Layer2);
    // Negative Y scrolls the layer's contents down the screen, which is what
    // puts the 200-line image in the middle of NTSC's 224. SetPosition takes a
    // non-const reference, so this has to be a named variable.
    SRL::Math::Types::Vector2D origin(0.0, -12.0);
    SRL::VDP2::NBG0::SetPosition(origin);
    SRL::VDP2::NBG0::ScrollEnable();

    SRL::Memory::LowWorkRam::Free(blank);
}

extern "C" void sat_video_set_palette(const uint8_t *colors)
{
    if (colors == nullptr)
    {
        return;
    }

    // 4 bits per channel in, 5 bits per channel out. (The "565" comment in the
    // original SDL backend is wrong -- the code under it reads 4-4-4, which is
    // what this matches.)
    //
    // Widening is (v << 1) | (v >> 3), not a bare shift: replicating the top
    // bit into the vacated low bit is what makes 15 map to 31 rather than 30,
    // so full-intensity input reaches full intensity out and the ramp between
    // stays even.
    //
    // FromRGB555 rather than the HighColor(r,g,b) constructor, deliberately.
    // That constructor takes 8-bit components and shifts them down by 3
    // internally (srl_color.hpp:66), so handing it a 5-bit value costs another
    // 3 bits and caps the brightest colour at 3/31. That is exactly what made
    // the first frames render nearly black.
    for (int32_t i = 0; i < 32; i++)
    {
        g_paletteRaw[i] = colors[i];
    }

    for (int32_t i = 0; i < 16; i++)
    {
        const uint8_t c1 = colors[i * 2 + 0];
        const uint8_t c2 = colors[i * 2 + 1];
        const uint8_t r = (uint8_t)(c1 & 0x0F);
        const uint8_t g = (uint8_t)((c2 & 0xF0) >> 4);
        const uint8_t b = (uint8_t)(c2 & 0x0F);

        g_paletteColors[i] = HighColor::FromRGB555((uint8_t)((r << 1) | (r >> 3)),
                                                   (uint8_t)((g << 1) | (g >> 3)),
                                                   (uint8_t)((b << 1) | (b >> 3)));
    }

    // CRAM is not touched here -- writing it now would recolour whatever page
    // is still on screen from last frame. sat_video_present (or, failing
    // that, sat_video_sync) writes it during the vblank window instead.
    g_paletteDirty = true;
}

/*----------------------
 | sat_video_flush_palette
 | Description: Writes a pending palette to CRAM and clears the dirty flag; a
 |   no-op if nothing is pending.
 | Author: suinevere
 | Dependencies: SRL (VDP2)
 | Globals: g_paletteDirty, g_paletteColors
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void sat_video_flush_palette(void)
{
    if (!g_paletteDirty)
    {
        return;
    }

    if (SRL::VDP2::NBG0::TilePalette.GetData() != nullptr)
    {
        SRL::VDP2::NBG0::TilePalette.Load(g_paletteColors, 16);
    }

    g_paletteDirty = false;
}

/*----------------------
 | sat_video_get_palette
 | Description: Hands back the last 32 bytes given to sat_video_set_palette, in
 |   the same 4-bits-per-channel form. The menu needs the game's live palette so
 |   it can install a dimmed copy behind the pause screen and restore it on
 |   resume; caching it here keeps that read in the layer that already owns CRAM
 |   rather than making the system backend call up into the menu.
 | Author: suinevere
 | Globals: g_paletteRaw
 | Params: out -- destination, must hold 32 bytes
 | Returns: N/A
 ----------------------*/
extern "C" void sat_video_get_palette(uint8_t *out)
{
    if (out == nullptr)
    {
        return;
    }

    for (int32_t i = 0; i < 32; i++)
    {
        out[i] = g_paletteRaw[i];
    }
}

extern "C" void sat_video_present(const uint8_t *page)
{
    // The sequencer tick. It is no longer an audio pump -- the SCSP plays from
    // its own memory whatever the engine is doing -- but SfxPlayer's timers
    // still advance the music from here, and from the pump points in
    // Bank::unpack and sat_cd_open that keep them running during loads.
    sat_audio_update();

    SRL::Core::Synchronize();

    sat_video_flush_palette();

    if (page != nullptr && g_vram != nullptr)
    {
        const uint8_t *src = page;
        uint8_t *dst = g_vram;

        for (int32_t line = 0; line < SCREEN_H; line++)
        {
            slDMACopy((void *)src, (void *)dst, PAGE_PITCH);
            src += PAGE_PITCH;
            dst += VRAM_PITCH;
        }

        slDMAWait();
    }
}

extern "C" void sat_video_sync(void)
{
    sat_audio_update();
    SRL::Core::Synchronize();
    sat_video_flush_palette();
}

/*----------------------
 | g_loadLatch
 | Description: Pad bits seen by sat_loading_tick while a load had the CPU, held
 |   until the next sat_input_read hands them on.
 | Author: suinevere
 ----------------------*/
static uint32_t g_loadLatch = 0;

extern "C" void sat_loading_tick(void)
{
    sat_audio_update();
    g_loadLatch |= sat_input_read();
}

extern "C" uint32_t sat_input_read(void)
{
    uint32_t bits = 0;
    SRL::Input::Digital port0(0);

    if (port0.IsConnected())
    {
        if (port0.IsHeld(SRL::Input::Digital::Button::Up))    bits |= SAT_PAD_UP;
        if (port0.IsHeld(SRL::Input::Digital::Button::Down))  bits |= SAT_PAD_DOWN;
        if (port0.IsHeld(SRL::Input::Digital::Button::Left))  bits |= SAT_PAD_LEFT;
        if (port0.IsHeld(SRL::Input::Digital::Button::Right)) bits |= SAT_PAD_RIGHT;
        if (port0.IsHeld(SRL::Input::Digital::Button::A)) bits |= SAT_PAD_A;
        if (port0.IsHeld(SRL::Input::Digital::Button::B)) bits |= SAT_PAD_B;
        if (port0.IsHeld(SRL::Input::Digital::Button::C)) bits |= SAT_PAD_C;
        if (bits & (SAT_PAD_A | SAT_PAD_B | SAT_PAD_C)) bits |= SAT_PAD_ACTION;
        if (port0.IsHeld(SRL::Input::Digital::Button::L)) bits |= SAT_PAD_L;
        if (port0.IsHeld(SRL::Input::Digital::Button::R)) bits |= SAT_PAD_R;
        if (port0.IsHeld(SRL::Input::Digital::Button::START)) bits |= SAT_PAD_PAUSE;
    }

    bits |= g_loadLatch;
    g_loadLatch = 0;

    return bits;
}

extern "C" uint32_t sat_time_ms(void)
{
    // 60 frames/sec -> 50/3 ms per frame. Integer maths, no float, and close
    // enough for the engine's pacing which is itself frame-quantised.
    return (g_frames * 50u) / 3u;
}

extern "C" void sat_sleep_ms(uint32_t ms)
{
    const uint32_t until = sat_time_ms() + ms;

    while (sat_time_ms() < until)
    {
        // Keep the audio pump running while the engine idles. Without this a
        // sleep longer than one buffer would drop out, and the VM sleeps for
        // frame pacing constantly.
        sat_audio_update();
        SRL::Core::Synchronize();
    }
}
