/*----------------------
 | saturn_scsp.cxx
 | Description: Programs the SCSP's slots directly, bypassing SGL entirely.
 |
 |   This exists because SGL's slPCMOn has no loop point -- the PCM struct
 |   (sl_def.h:1402) carries mode, channel, level, pan, pitch and effect sends
 |   and nothing else -- so hardware mixing was ruled out when this backend was
 |   first designed. The slot registers underneath it do have LSA, LEA and
 |   LPCTL, which is what makes MOD-style sustained instruments possible and
 |   the software mixer unnecessary.
 |
 |   One of the files that includes <srl.hpp>.
 |   Registers: docs/scsp-registers.md
 | Author: suinevere
 | Dependencies: saturn_scsp.h, scsp_voice.h, SRL
 ----------------------*/
#include <srl.hpp>
#include "saturn_scsp.h"
#include "scsp_voice.h"

/*----------------------
 | SCSP_SLOT_BASE / SCSP_COMMON / SCSP_SOUND_RAM
 | Description: The three address bases everything here is expressed against.
 |   Slot n occupies 0x20 bytes from SCSP_SLOT_BASE.
 |
 |   Only one of these is corroborated by anything in the tree: sega_snd.h:142
 |   puts MCIRE at 0x25b0042e, which fixes the common block at 0x25B00400.
 | Author: suinevere
 ----------------------*/
#define SCSP_SLOT_BASE   0x25B00000u
#define SCSP_COMMON      0x25B00400u
#define SCSP_SOUND_RAM   0x25A00000u

#define SCSP_REG(a)      (*(volatile uint16_t *)(a))
#define SCSP_SLOT(n, o)  SCSP_REG(SCSP_SLOT_BASE + ((uint32_t)(n) * 0x20u) + (o))

/*----------------------
 | SCSP_HEAP_BASE / SCSP_HEAP_LIMIT
 | Description: The span of sound RAM samples are uploaded into: 0x030000 to
 |   0x080000, 320 KB of the bank's 512.
 |
 |   The low 128 KB is left alone because it holds the SGL driver image and the
 |   areas BOOTSND.MAP declares.
 |
 |   The next 64 KB is left alone for the Cinepak player's PCM buffer, which
 |   SRL places at 0x25A20000 -- sound RAM offset 0x020000, which is where this
 |   heap used to start. They overlapped exactly. See saturn_movie.cxx, which
 |   sizes the buffer to fit the reservation rather than the other way round.
 | Author: suinevere
 ----------------------*/
#define SCSP_HEAP_BASE   0x030000u
#define SCSP_HEAP_LIMIT  0x080000u

#define SCSP_CHANNELS    4

/*----------------------
 | Slot ping-pong
 | Description: Each engine channel owns TWO SCSP slots and alternates between
 |   them on every note-on. Channel c uses slots c*2 and c*2+1; g_slot[c] says
 |   which one is sounding.
 |
 |   One slot per channel does not work, and the reason is timing rather than
 |   theory. Retriggering meant key off, reprogram, key on -- three writes a few
 |   microseconds apart -- but the SCSP services a slot once per sample period,
 |   about 22.7 us at 44.1 kHz, and a key-on that arrives while the envelope is
 |   still in release is dropped. That is heard as notes cut off, and it gets
 |   worse the busier the music is because note-ons come closer together.
 |
 |   Alternating removes the race instead of trying to win it: the new note goes
 |   to a slot that is already idle, so nothing has to have finished first. The
 |   outgoing slot is keyed off AFTER the new one starts, so its release covers
 |   the seam rather than leaving a gap.
 |
 |   Eight of the SCSP's 32 slots, taken from the top. The 68000 driver runs --
 |   the Cinepak player's audio needs it -- so its allocator is competing for
 |   slots, and it hands them out from slot 0 upward. Starting at 24 puts this
 |   backend as far from it as the slot file allows.
 |
 |   SCSP_SLOT_FIRST is the one knob: if the driver is ever heard stealing a
 |   voice, move it, and nothing else has to change.
 | Author: suinevere
 ----------------------*/
#define SCSP_SLOTS_PER_CHANNEL 2
#define SCSP_SLOT_FIRST        24
#define SCSP_SLOT_A(ch)        ((uint8_t)(SCSP_SLOT_FIRST + (ch) * SCSP_SLOTS_PER_CHANNEL))
#define SCSP_SLOT_B(ch)        ((uint8_t)(SCSP_SLOT_FIRST + (ch) * SCSP_SLOTS_PER_CHANNEL + 1))
#define SCSP_TOTAL_SLOTS       (SCSP_CHANNELS * SCSP_SLOTS_PER_CHANNEL)

static uint8_t g_slot[SCSP_CHANNELS] = { 0, 0, 0, 0 };

/*----------------------
 | Envelope settings
 | Description: The SCSP's envelope generator is set to do nothing: instant
 |   attack, no decay, hold until key-off, fastest release.
 |
 |   That matches what the software mixer did, which was to play the sample as
 |   it is -- Another World's instruments carry their own shape and the engine
 |   changes level by writing volume, not by asking for a ramp.
 |
 |   RR is the knob to reach for if key-off clicks: 31 is the fastest release
 |   available and so the most abrupt.
 | Author: suinevere
 ----------------------*/
#define SCSP_AR   31
#define SCSP_D1R  0
#define SCSP_D2R  0
#define SCSP_DL   0
#define SCSP_RR   31

static ScspCache g_cache;
static uint32_t  g_active  = 0;   /* bitmask of slots keyed on */
static uint32_t  g_uploads = 0;

/*----------------------
 | slotKeyOff / slotKeyOn
 | Description: KYONB is a per-slot request and KYONEX is the global commit
 |   that applies every pending request at once, so both halves are needed and
 |   the commit is written through the slot being changed.
 | Author: suinevere
 ----------------------*/
static void slotKeyOff(uint8_t slot)
{
    const uint16_t w = SCSP_SLOT(slot, 0x00);

    SCSP_SLOT(slot, 0x00) = (uint16_t)((w & ~0x0800u) | 0x1000u);
    g_active &= ~(1u << slot);
}

static void slotKeyOn(uint8_t slot)
{
    const uint16_t w = SCSP_SLOT(slot, 0x00);

    SCSP_SLOT(slot, 0x00) = (uint16_t)(w | 0x0800u | 0x1000u);
    g_active |= (1u << slot);
}

/*----------------------
 | uploadToSoundRam
 | Description: Copies a sample across, 16 bits at a time.
 |
 |   Word-at-a-time because the SCSP's RAM is 16-bit; byte writes to it are at
 |   best slow and are documented as unreliable on some revisions. The source
 |   is only guaranteed byte-aligned -- MixerChunk::data is the resource block
 |   plus 8 -- so the two bytes are assembled before the store rather than read
 |   as a uint16_t.
 | Author: suinevere
 ----------------------*/
static void uploadToSoundRam(uint32_t offset, const uint8_t *src, uint32_t bytes)
{
    volatile uint16_t *dst = (volatile uint16_t *)(SCSP_SOUND_RAM + offset);
    uint32_t           i;

    for (i = 0; i + 1u < bytes; i += 2u)
    {
        *dst++ = (uint16_t)(((uint16_t)src[i] << 8) | src[i + 1u]);
    }

    if (i < bytes)
    {
        *dst = (uint16_t)((uint16_t)src[i] << 8);
    }

    g_uploads++;
}

void sat_scsp_init(void)
{
    uint8_t n;

    /* The SGL 68000 driver is deliberately left running. This used to call
       slSoundOffWait to stand it down and take the whole sound block, on the
       reasoning that nothing was left relying on it. The Cinepak player is:
       it feeds movie audio to that driver and reads its playback clock back,
       so with the 68000 in reset the opening froze on its first frame and
       looped one PCM buffer forever. Sharing the chip is the price of movie
       sound -- the slots move up instead, and the sample heap moves above the
       player's PCM buffer. */

    scsp_cache_init(&g_cache, SCSP_HEAP_BASE, SCSP_HEAP_LIMIT);
    g_active  = 0;
    g_uploads = 0;

    /* Master volume, full. Per-note attenuation is TL. */
    SCSP_REG(SCSP_COMMON + 0x00) =
        (uint16_t)((SCSP_REG(SCSP_COMMON + 0x00) & ~0x000Fu) | 0x000Fu);

    /* Every slot both halves of the ping-pong can land on, not just one per
       channel -- see the Slot ping-pong note. */
    for (n = SCSP_SLOT_FIRST; n < SCSP_SLOT_FIRST + SCSP_TOTAL_SLOTS; n++)
    {
        slotKeyOff(n);

        SCSP_SLOT(n, 0x08) = (uint16_t)(((uint16_t)SCSP_D2R << 11) |
                                        ((uint16_t)SCSP_D1R << 6)  |
                                        (uint16_t)SCSP_AR);
        SCSP_SLOT(n, 0x0A) = (uint16_t)(((uint16_t)SCSP_DL << 5) |
                                        (uint16_t)SCSP_RR);
        SCSP_SLOT(n, 0x0C) = 0x00FF;   /* TL silent until a note sets it */

        /* Nothing zeroes slot registers on the way in, so whatever the boot
           ROM or the driver last left in MDL/MDXSL/MDYSL and the LFO block is
           still there. Left-over modulation or vibrato on a slot this backend
           then keys on would be heard as FM garbage, not silence. */
        SCSP_SLOT(n, 0x0E) = 0;        /* MDL/MDXSL/MDYSL -- no FM modulation */
        SCSP_SLOT(n, 0x12) = 0;        /* LFORE/LFOF/PLFOWS/PLFOS/ALFOWS/ALFOS off */

        /* DISDL 7 -- full direct send -- and DIPAN 0, centred. The engine is
           mono; there is nothing to pan. */
        SCSP_SLOT(n, 0x16) = (uint16_t)(7u << 13);
    }

    for (n = 0; n < SCSP_CHANNELS; n++)
    {
        g_slot[n] = SCSP_SLOT_A(n);
    }
}

void sat_scsp_shutdown(void)
{
    sat_scsp_stop_all();
}

void sat_scsp_stop(uint8_t channel)
{
    if (channel < SCSP_CHANNELS)
    {
        /* Both halves: the idle one is normally already off, but after a
           retrigger its release may still be running and stopChannel means
           stop, not fade. */
        slotKeyOff(SCSP_SLOT_A(channel));
        slotKeyOff(SCSP_SLOT_B(channel));
    }
}

void sat_scsp_stop_all(void)
{
    uint8_t n;

    for (n = SCSP_SLOT_FIRST; n < SCSP_SLOT_FIRST + SCSP_TOTAL_SLOTS; n++)
    {
        slotKeyOff(n);
    }
}

void sat_scsp_set_volume(uint8_t channel, uint8_t volume)
{
    uint8_t slot;

    if (channel >= SCSP_CHANNELS)
    {
        return;
    }

    /* Only the sounding half. Writing both would re-level a slot that is still
       releasing the previous note and make the seam audible. */
    slot = g_slot[channel];

    SCSP_SLOT(slot, 0x0C) =
        (uint16_t)((SCSP_SLOT(slot, 0x0C) & ~0x00FFu) |
                   scsp_voice_tl(volume));
}

void sat_scsp_flush_samples(void)
{
    /* Silence first, then rewind. A slot reads straight out of the heap, so
       the other order hands a sounding note's memory to the next upload. */
    sat_scsp_stop_all();
    scsp_cache_reset(&g_cache);
}

void sat_scsp_play(uint8_t channel, const uint8_t *data, uint16_t len,
                   uint16_t loopPos, uint16_t loopLen,
                   uint16_t freq, uint8_t volume)
{
    ScspVoicePoints points;
    ScspCacheResult result;
    uint32_t        offset = 0;
    uint8_t         previous;
    uint8_t         slot;

    if (channel >= SCSP_CHANNELS || data == 0)
    {
        return;
    }

    result = scsp_cache_acquire(&g_cache, data, len, loopLen, &offset);

    if (result == SCSP_CACHE_TOO_BIG)
    {
        /* Nothing sensible to play. Leave the channel as it was rather than
           keying on a slot pointed at nothing. */
        return;
    }

    if (result == SCSP_CACHE_MISS_AFTER_RESET)
    {
        /* The heap was emptied to make room, so everything currently sounding
           is reading memory that is about to be overwritten. */
        sat_scsp_stop_all();
    }

    if (result != SCSP_CACHE_HIT)
    {
        uploadToSoundRam(offset, data, scsp_voice_upload_bytes(len, loopLen));
    }

    if (!scsp_voice_points(offset, len, loopPos, loopLen, &points))
    {
        /* The upload and cache reservation above already happened, but there
           is nothing sensible to key on. Returning here without touching the
           channel would leave whatever was already sounding on it -- a
           looping note would drone on until something else stops the channel
           -- so key it off explicitly instead of falling through silently.

           g_slot is deliberately left alone: the channel keeps pointing at the
           half it was already using, so the next successful note-on alternates
           from there as usual. */
        slotKeyOff(g_slot[channel]);
        return;
    }

    /* The other half of this channel's pair. It is idle, or at worst releasing
       a note from two note-ons ago, so nothing has to finish before it can be
       programmed -- which is the whole point. See the Slot ping-pong note. */
    previous = g_slot[channel];
    slot     = (previous == SCSP_SLOT_A(channel)) ? SCSP_SLOT_B(channel)
                                                  : SCSP_SLOT_A(channel);

    SCSP_SLOT(slot, 0x00) =
        (uint16_t)((points.loop ? (1u << 5) : 0u) |   /* LPCTL forward loop */
                   (1u << 4) |                        /* PCM8B, 8-bit signed */
                   ((points.sa >> 16) & 0x000Fu));
    SCSP_SLOT(slot, 0x02) = (uint16_t)(points.sa & 0xFFFFu);
    SCSP_SLOT(slot, 0x04) = points.lsa;
    SCSP_SLOT(slot, 0x06) = points.lea;
    SCSP_SLOT(slot, 0x10) = scsp_voice_pitch(freq);
    SCSP_SLOT(slot, 0x0C) = (uint16_t)((SCSP_SLOT(slot, 0x0C) & ~0x00FFu) |
                                       scsp_voice_tl(volume));

    /* On before off, and both before publishing g_slot.
       On-then-off because the reverse leaves a gap: the outgoing note's release
       covers the seam instead. Publishing g_slot last means setChannelVolume
       cannot land on the new slot while it is still half-programmed. */
    slotKeyOn(slot);
    slotKeyOff(previous);

    g_slot[channel] = slot;
}

uint32_t sat_scsp_debug_active(void)     { return g_active; }
uint32_t sat_scsp_debug_heap_used(void)  { return scsp_cache_used_bytes(&g_cache); }
uint32_t sat_scsp_debug_cache_used(void) { return scsp_cache_used_entries(&g_cache); }

uint32_t sat_scsp_debug_uploads(void)
{
    const uint32_t n = g_uploads;

    g_uploads = 0;
    return n;
}
