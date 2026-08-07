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
 |
 |   Movies must be word aligned and must not replace a codebook with an empty
 |   one -- tools/mkopeningcpk.py says why at length. Neither is checked here,
 |   because neither is survivable: SEGA's decoder walks off and the SH-2 sits
 |   in the BIOS exception handler with interrupts masked, which looks like a
 |   frozen picture rather than a fault.
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
 | Description: The sound RAM a movie's audio streams through. Held here rather
 |   than left to SRL's defaults because saturn_scsp.cxx reserves this exact
 |   span -- SCSP_HEAP_BASE begins where it ends -- and the two constants have to
 |   be read together to see that they do not overlap.
 |
 |   MOVIE_PCM_SAMPLES is SAMPLES PER CHANNEL, not bytes (sgl_cpk.h:281), and
 |   must be 4096 times 1 to 16. At 4096*16 mono that is 65536 samples, 128 KB,
 |   which is the reservation, and 2.05 seconds of buffer at the 32 kHz the
 |   movies are encoded at.
 |
 |   Do not shrink it. CPK refills on half-buffer boundaries, so the buffer's
 |   half-life is how long a refill has to arrive in. At 4096*8 that was 0.51
 |   seconds and the audio dropped out on almost every one of them. Stereo would
 |   double the bytes for the same slack, which is the other reason the movies
 |   are mono.
 | Author: suinevere
 ----------------------*/
#define MOVIE_PCM_ADDR    ((uint16_t *)0x25A20000)
#define MOVIE_PCM_SAMPLES (4096 * 16)

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
