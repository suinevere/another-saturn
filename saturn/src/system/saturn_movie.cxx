/*----------------------
 | saturn_movie.cxx
 | Description: The SRL side of Cinepak playback: the player, the VDP1 sprite its
 |   frames are decoded into, and the NBG0 hand-off around them.
 |
 |   This is the only thing in the port that uses VDP1. The engine rasterizes in
 |   software to an NBG0 bitmap (see saturn_platform.cxx) and never draws a
 |   sprite, so the two never contend -- but a VDP2 layer can sit in front of a
 |   sprite depending on priority, so NBG0 is switched off for the duration
 |   rather than trusting an ordering nobody has verified on hardware.
 |
 |   The player is heap-allocated rather than static: its constructor hooks
 |   SRL::Core::OnBeforeSync, and a static would run that before sat_boot_init
 |   has brought SRL's arena up.
 | Author: suinevere
 | Dependencies: saturn_movie.h, SRL (Core, CinepakPlayer, VDP1, VDP2, Scene2D)
 | Globals: g_player, g_sprite, g_spriteW, g_spriteH, g_completed
 ----------------------*/
#include <srl.hpp>
#include "saturn_movie.h"

using namespace SRL::Types;

/*----------------------
 | MOVIE_DEPTH
 | Description: Sprite Z for the movie. Any value works -- it only orders the
 |   sprite against other sprites, and the movie is the only one drawn.
 | Author: suinevere
 ----------------------*/
#define MOVIE_DEPTH 500.0

/*----------------------
 | MOVIE_PCM_ADDR / MOVIE_PCM_SAMPLES
 | Description: The sound RAM the movie's audio streams through. Held here
 |   rather than left to SRL's defaults because saturn_scsp.cxx reserves this
 |   exact span -- SCSP_HEAP_BASE starts where it ends -- and the two constants
 |   have to be read together to see that they do not overlap.
 |
 |   MOVIE_PCM_SAMPLES is SAMPLES PER CHANNEL, not bytes (sgl_cpk.h:281), and
 |   must be 4096 times 1 to 16. At 4096*8 mono that is 32768 samples, 64 KB,
 |   which is the reservation, and a little over a second of buffer at the
 |   32 kHz the movies are encoded at. SRL's default is 4096*16, which in
 |   stereo would be 256 KB and would run four times past the reservation into
 |   the sample heap.
 | Author: suinevere
 ----------------------*/
#define MOVIE_PCM_ADDR    ((uint16_t *)0x25A20000)
#define MOVIE_PCM_SAMPLES (4096 * 8)

/*----------------------
 | MOVIE_DIAGNOSTICS
 | Description: Draws playback state into the movie's own sprite as coloured
 |   bars. Set to 0 to remove it entirely.
 |
 |   It writes into the sprite rather than printing because there is nowhere to
 |   print to: SRL::ASCII is a replacement for slPrint and wants NBG0, which
 |   this port has configured as a bitmap, and SRL's emulator logger writes to
 |   0x24001000 for Kronos, which Mednafen does not read. The sprite is already
 |   on screen and needs no layer of its own.
 |
 |   Reading the bars, from the top of the picture:
 |     row 0   one white pixel, marching left to right, one step per call.
 |             Moving means the loop is still running and this is a stalled
 |             stream. Stopped means the CPU is not getting here at all and the
 |             picture is frozen because nothing is driving it.
 |     row 4   green, one pixel per decoded frame, wrapping. Stuck at one means
 |             only the preloaded frame ever arrived.
 |     row 8   blue, twenty pixels per CinepakPlayer::PlaybackStateEnum, so
 |             40 = Timer (playing), 100 = Completed, nothing = Stop.
 |     row 12  red, one pixel per unit of the last CPK error code, only if the
 |             player raised one. See the CPK_ERR_ values in sgl_cpk.h.
 | Author: suinevere
 ----------------------*/
#define MOVIE_DIAGNOSTICS 1

/*----------------------
 | g_player
 | Description: The live player, or null when nothing is open.
 | Author: suinevere
 ----------------------*/
static SRL::CinepakPlayer *g_player = nullptr;

/*----------------------
 | g_sprite / g_spriteW / g_spriteH
 | Description: The VDP1 texture frames are decoded into, and the size it was
 |   reserved at.
 |
 |   It is allocated on the first open and never released, because
 |   VDP1::TryAllocateTexture is a bump allocator with no free: every replay
 |   would take another 140 KB of the 512 KB sprite VRAM and the attract loop
 |   would run it dry in three passes. A second open at the same size reuses it;
 |   a different size is refused rather than leaking a second one.
 | Author: suinevere
 ----------------------*/
static int32_t g_sprite = -1;
static uint16_t g_spriteW = 0;
static uint16_t g_spriteH = 0;

/*----------------------
 | g_completed
 | Description: Set by the completion event so sat_movie_step can report the end
 |   of the movie without polling the player's status word.
 | Author: suinevere
 ----------------------*/
static bool g_completed = false;

#if MOVIE_DIAGNOSTICS
/*----------------------
 | g_steps / g_decoded / g_error
 | Description: Counters behind the diagnostic bars: calls to sat_movie_step,
 |   invocations of the frame event, and the last error the player raised.
 | Author: suinevere
 ----------------------*/
static uint32_t g_steps   = 0;
static uint32_t g_decoded = 0;
static int32_t  g_error   = 0;

/*----------------------
 | movieOnError
 | Description: Records the last CPK error code for the diagnostic bars. Bound
 |   to the player's static error event.
 | Author: suinevere
 | Globals: g_error
 | Params: code -- a CPK_ERR_ value from sgl_cpk.h
 | Returns: N/A
 ----------------------*/
static void movieOnError(int32_t code)
{
    g_error = code;
}

/*----------------------
 | movieBar
 | Description: Draws one horizontal run of pixels into the sprite.
 | Author: suinevere
 | Globals: g_sprite, g_spriteW
 | Params: row -- y in the sprite; from -- first x; length -- pixels, clamped
 |         to the sprite width; colour -- RGB555 with the opaque bit set
 | Returns: N/A
 ----------------------*/
static void movieBar(uint16_t row, uint16_t from, uint32_t length, uint16_t colour)
{
    uint16_t *pixels = (uint16_t *)SRL::VDP1::Textures[g_sprite].GetData();
    uint32_t x;

    if (pixels == nullptr)
    {
        return;
    }

    for (x = from; x < from + length && x < g_spriteW; x++)
    {
        pixels[(uint32_t)row * g_spriteW + x] = colour;
    }
}

/*----------------------
 | movieDiagnostics
 | Description: Repaints the diagnostic bars from the counters. Called once per
 |   step, before the sprite is queued, so a stalled stream leaves them on
 |   screen and a running one has them overwritten by each decoded frame.
 | Author: suinevere
 | Globals: g_steps, g_decoded, g_error, g_player
 | Params: N/A
 | Returns: N/A
 ----------------------*/
static void movieDiagnostics(void)
{
    const uint32_t status = (g_player != nullptr) ? (uint32_t)g_player->GetStatus() : 0;

    movieBar(0, (uint16_t)(g_steps % g_spriteW), 1, 0xFFFF);
    movieBar(4, 0, g_decoded % g_spriteW, 0x83E0);
    movieBar(8, 0, status * 20, 0xFC00);

    if (g_error != 0)
    {
        movieBar(12, 0, (uint32_t)(g_error < 0 ? -g_error : g_error), 0x801F);
    }

    g_steps++;
}
#endif

/*----------------------
 | movieFrameDecoded
 | Description: Copies a freshly decoded frame into the sprite's VRAM. Bound to
 |   the player's per-frame event, which fires before the sync in
 |   sat_movie_step.
 | Author: suinevere
 | Globals: g_sprite
 | Params: player -- the player that decoded the frame
 | Returns: N/A
 ----------------------*/
static void movieFrameDecoded(SRL::CinepakPlayer &player)
{
    if (g_sprite < 0)
    {
        return;
    }

    const auto size = player.GetResolution();
    const auto length = (size.Width * size.Height) << ((int)player.GetDepth() + 1);

    DMA_ScuMemCopy(SRL::VDP1::Textures[g_sprite].GetData(), player.GetFrameData(), length);

#if MOVIE_DIAGNOSTICS
    g_decoded++;
#endif
}

/*----------------------
 | movieCompleted
 | Description: Marks the movie finished. Bound to the player's completion event.
 |   Deliberately does not restart playback -- the caller decides what follows.
 | Author: suinevere
 | Globals: g_completed
 | Params: player -- unused
 | Returns: N/A
 ----------------------*/
static void movieCompleted(SRL::CinepakPlayer &player)
{
    (void)player;
    g_completed = true;
}

/*----------------------
 | movieClearSprite
 | Description: Blanks the sprite's VRAM so the first field after NBG0 goes away
 |   shows black rather than whatever was last in that VRAM.
 | Author: suinevere
 | Globals: g_sprite, g_spriteW, g_spriteH
 | Params: depth -- the player's colour depth, which sets the bytes per pixel
 | Returns: N/A
 ----------------------*/
static void movieClearSprite(const SRL::CinepakPlayer::ColorDepth depth)
{
    const size_t length = (g_spriteW * g_spriteH) << ((int)depth + 1);
    uint8_t *data = (uint8_t *)SRL::VDP1::Textures[g_sprite].GetData();

    for (size_t i = 0; i < length; i++)
    {
        data[i] = 0;
    }
}

extern "C" int sat_movie_open(const char *file)
{
    if (file == nullptr || g_player != nullptr)
    {
        return 0;
    }

    g_player = new SRL::CinepakPlayer();

    if (g_player == nullptr)
    {
        return 0;
    }

    g_player->OnFrame += movieFrameDecoded;
    g_player->OnCompleted += movieCompleted;

    // HWRAM for the decode buffer: SRL warns that a fullscreen movie stutters
    // if it decodes anywhere else. The 200 KB ring buffer stays in LWRAM, which
    // is where its default puts it and where there is room for it.
    SRL::CinepakPlayer::MovieDecodeParams params;
    params.DecodeBufferLocation = SRL::Memory::Zone::HWRam;
    params.PCMAddress = MOVIE_PCM_ADDR;
    params.PCMSize = MOVIE_PCM_SAMPLES;

    if (!g_player->LoadMovie(file, params))
    {
        delete g_player;
        g_player = nullptr;
        return 0;
    }

    const auto size = g_player->GetResolution();

    if (g_sprite < 0)
    {
        g_sprite = SRL::VDP1::TryAllocateTexture(size.Width, size.Height,
                                                 SRL::CRAM::TextureColorMode::RGB555, 0);

        if (g_sprite < 0)
        {
            g_player->UnloadMovie();
            delete g_player;
            g_player = nullptr;
            return 0;
        }

        g_spriteW = size.Width;
        g_spriteH = size.Height;
    }
    else if (size.Width != g_spriteW || size.Height != g_spriteH)
    {
        g_player->UnloadMovie();
        delete g_player;
        g_player = nullptr;
        return 0;
    }

    movieClearSprite(g_player->GetDepth());

    SRL::VDP2::NBG0::ScrollDisable();

#if MOVIE_DIAGNOSTICS
    static bool errorHooked = false;

    if (!errorHooked)
    {
        errorHooked = true;
        SRL::CinepakPlayer::OnError += movieOnError;
    }

    g_steps = 0;
    g_decoded = 0;
    g_error = 0;
#endif

    g_completed = false;
    g_player->Play();

    return 1;
}

extern "C" int sat_movie_step(void)
{
    if (g_player == nullptr)
    {
        return 0;
    }

#if MOVIE_DIAGNOSTICS
    movieDiagnostics();
#endif

    SRL::Scene2D::DrawSprite(
        g_sprite,
        SRL::Math::Vector3D(0.0, 0.0, MOVIE_DEPTH),
        SRL::Math::Vector2D(1.0, 1.0),
        SRL::Scene2D::ZoomPoint::Center);

    SRL::Core::Synchronize();

    return g_completed ? 0 : 1;
}

extern "C" void sat_movie_close(void)
{
    if (g_player == nullptr)
    {
        return;
    }

    g_player->Stop();
    g_player->UnloadMovie();
    delete g_player;
    g_player = nullptr;

    SRL::VDP2::NBG0::ScrollEnable();
}
