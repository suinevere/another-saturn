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

extern "C" void sat_boot_init(void)
{
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
