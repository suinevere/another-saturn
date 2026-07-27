/*----------------------
 | saturn_audio.cxx
 | Description: PCM output for the engine's software mixer, plus the periodic
 |   timer that drives the music sequencer. One of the three files that include
 |   <srl.hpp> (with saturn_platform.cxx and saturn_cdfile.cxx).
 |
 |   The engine mixes in software and hands over finished 8-bit mono samples;
 |   nothing here synthesises anything. They arrive UNSIGNED -- Mixer::mix biases
 |   by +128 on its way out, for SDL's benefit -- and the SCSP wants signed, so
 |   this file undoes that. Two buffers alternate across two PCM channels: while
 |   one plays, the other is already mixed and waiting.
 |
 |   Design, including why hardware mixing was rejected and what the known
 |   weaknesses are:
 |   docs/superpowers/specs/2026-07-27-another-world-audio-backend-design.md
 | Author: suinevere
 | Dependencies: saturn_audio.h, saturn_platform.h, SRL (Memory), SGL (slPCM*)
 ----------------------*/
#include <srl.hpp>
#include "saturn_audio.h"
#include "saturn_platform.h"

/*----------------------
 | sega_pcm.h
 | Description: SEGA's PCM stream driver, from LIBPCM.A. The SDK ships both but
 |   links neither -- see the LIBS line in saturn/makefile -- and nothing in
 |   SaturnRingLib references it.
 |
 |   Included after <srl.hpp> deliberately: SGL's headers define bare lowercase
 |   macros (sgl.h has `#define pal COL_32K`) and pulling them in first would
 |   rewrite tokens inside SRL's C++ headers.
 | Author: suinevere
 ----------------------*/
extern "C" {
#include <sega_pcm.h>
}

/*----------------------
 | SAMPLE_RATE
 | Description: What the PCM channels play at, and what the engine is told so its
 |   pitch maths agrees (mixer.cxx:62).
 |
 |   44100, and the reason is latency rather than fidelity. The driver's staging
 |   buffer is fixed in BYTES -- pcm_size is already at the minimum the driver
 |   accepts, 4096*1 is refused with PCM_ERR_ILL_SIZE_PCMBUF -- so the only way to
 |   make it represent less time is to consume it faster. It is 8192 bytes, which
 |   is 372 ms at 11025 and 186 ms at 44100, and it dominates everything else in
 |   the budget. This is the SCSP's ceiling, so it is also the end of this lever.
 |
 |   The game's samples are Amiga-era and around 8-11 kHz, so this buys no
 |   fidelity whatsoever. It is bought entirely with SH-2 time: Mixer::mix
 |   interpolates, scales and clamps four channels per output sample, and that
 |   work scales linearly with this number.
 |
 |   44100 was tried once before and the popping came back, which is why the rate
 |   went to 32000 in between. That was refill cadence rather than mixer
 |   throughput: refills were tied to the engine finishing a frame, so one slow
 |   frame emptied a ring holding only 46 ms. pumpOutput runs from vblank now and
 |   that cause is gone.
 |
 |   Fallbacks in order, if popping returns: RING_BYTES to 4096 first, costing
 |   47 ms and keeping this rate, then 32000, then 22050 which measured 465 ms
 |   and was never in doubt.
 | Author: suinevere
 ----------------------*/
#define SAMPLE_RATE     44100

/*----------------------
 | BUFFER_SAMPLES
 | Description: 0x900, and NOT a free choice: slPCMOn refuses to play a buffer
 |   shorter than 0x900 bytes, which at 8-bit mono is 2304 samples. That is
 |   209 ms at 11025 Hz, and it is where this backend's ~209 ms latency comes
 |   from. The obvious design -- one-frame buffers swapped every vblank -- is not
 |   available because of this floor.
 | Author: suinevere
 ----------------------*/
#define BUFFER_SAMPLES  0x900
#define BUFFER_COUNT    2

/*----------------------
 | UNCACHED
 | Description: The SH-2 writes through cache, but the sound driver DMAs the
 |   buffer straight out of RAM. Mixing through the uncached mirror of the buffer
 |   means the samples are in RAM by the time the DMA reads them, with no
 |   explicit cache flush. Everything at 0x20000000 and above is the uncached
 |   view of the same memory.
 | Author: suinevere
 ----------------------*/
#define UNCACHED(p)     ((uint8_t *)(((uint32_t)(p)) | 0x20000000u))

/*----------------------
 | g_pcm
 | Description: One PCM control block per buffer. Field order is
 |   mode, channel, level, pan, pitch, then the four effect sends.
 |
 |   The channel numbers step by two because SCSP slots are allocated in pairs;
 |   this mirrors how SRL lays out its own (private) channel table. Mono 8-bit
 |   is what the engine's mixer produces.
 | Author: suinevere
 ----------------------*/
static PCM g_pcm[BUFFER_COUNT] = {
    { _Mono | _PCM8Bit, 0, 127, 0, 0, 0, 0, 0, 0 },
    { _Mono | _PCM8Bit, 2, 127, 0, 0, 0, 0, 0, 0 }
};

static uint8_t         *g_buffer[BUFFER_COUNT] = { nullptr, nullptr };
static SatAudioCallback g_callback             = nullptr;
static void            *g_callbackParam        = nullptr;
static int32_t          g_playing              = -1;
static bool             g_running              = false;

/*----------------------
 | AudioMode / g_mode
 | Description: Which output path is live. Streaming is what we want; the
 |   alternating-buffer path is the fallback, kept because the streaming driver
 |   is undocumented here and may simply refuse to start. Falling back means
 |   audio is stuttery rather than absent, which is where this port already was.
 | Author: suinevere
 ----------------------*/
enum AudioMode
{
    MODE_SILENT = 0,
    MODE_STREAM,
    MODE_BUFFERS
};

static AudioMode g_mode = MODE_SILENT;

/*----------------------
 | RING_BYTES
 | Description: The work-RAM ring the driver consumes from and we produce into.
 |   2048 bytes is 2048 mono samples, so 46 ms at 44100.
 |
 |   This was doubled to 4096 to chase popping that sounded like the level being
 |   cut. It is back down because that popping turned out to be the loop restart
 |   bug in Mixer::mix, not starvation -- content dependent, at note changes, on
 |   looping samples only -- so the larger ring was buying latency and nothing
 |   else. Put it back to 4096 if popping returns; that is the first fallback and
 |   costs 47 ms.
 |
 |   1024 is the floor regardless: it is the driver's own task granularity, the
 |   size GetWriteBuf hands back, so a ring that size leaves no room to be ahead
 |   of it at all.
 |
 |   This IS latency, contrary to what this comment used to claim. streamUpdate
 |   fills every byte the driver reports free, so the ring sits full and a sample
 |   written now is heard a whole ring later -- plus however much the driver holds
 |   staged in sound RAM behind it.
 |
 |   The floor is not zero: the ring has to cover the longest stretch the engine
 |   can go without calling sat_audio_update. Every such stretch is now pumped --
 |   the decode loop in Bank::unpack and the whole-file read in sat_cd_open were
 |   the two that were not -- so this can be small. If popping returns, it is
 |   because something is blocking for longer than 93 ms without pumping; find
 |   that rather than raising this first.
 | Author: suinevere
 ----------------------*/
#define RING_BYTES      4096

/*----------------------
 | AUDIO_TEST_MODE
 | Description: TEMPORARY diagnostic switch.
 |     0 -- normal, the engine's mixer feeds the driver
 |     1 -- pure silence
 |     2 -- a square wave generated here, ignoring the engine entirely
 |   Set back to 0 once the noise is understood.
 | Author: suinevere
 ----------------------*/
#define AUDIO_TEST_MODE 0

/*----------------------
 | g_monoTemp
 | Description: Staging for one call's worth of the mixer's mono output, before
 |   it is duplicated into interleaved stereo. Plain static storage rather than
 |   an allocation: it must exist before the first call and never fails.
 | Author: suinevere
 ----------------------*/
static uint8_t g_monoTemp[4096];

/*----------------------
 | PCM_LEVEL
 | Description: Playback level, [0..7], applied by the SCSP rather than in
 |   software.
 |
 |   There used to be a software divide by 4 here as well. It was added while
 |   every sample was wrapping to full scale -- see the sign note in
 |   streamUpdate -- so it was compensating for a bug, and with that fixed it
 |   only made the mix quiet. It also cost two bits of an already 8-bit signal,
 |   which is audible as grain on quiet passages, so attenuating in software is
 |   the wrong place for it regardless of the amount.
 |
 |   Level lives here instead, where it is free and lossless. Raise toward 7 if
 |   the mix is still quiet; the mixer sums four voices and clamps
 |   (mixer.cxx addclamp), so it has no headroom of its own to give and the
 |   ceiling is whatever stays clean.
 | Author: suinevere
 ----------------------*/
#define PCM_LEVEL 3

/*----------------------
 | TONE_HALF_PERIOD
 | Description: Samples per half cycle of the test square wave. 12 at 11025 Hz
 |   is about 460 Hz -- comfortably mid-range, and a square wave is chosen
 |   because its shape survives being misread: interpreted as 16-bit or as
 |   stereo it stops being a clean tone in an obvious way.
 | Author: suinevere
 ----------------------*/
#define TONE_HALF_PERIOD 12

/*----------------------
 | PCM_SOUND_OFFSET / PCM_SOUND_SAMPLES
 | Description: Where in sound RAM the driver stages samples, and how many it
 |   stages (per channel, and the driver only accepts 4096*1 .. 4096*16).
 |
 |   The offset is the one genuinely unverified number in this file. Sound RAM
 |   is 512 KB at 0x25A00000. BOOTSND.MAP -- the area map SRL feeds to SND_Init
 |   -- is 82 bytes of 8-byte records terminated by 0xFFFF, and every area it
 |   declares lives low, the highest ending around 0x48000. So the top of the
 |   bank is the safest place available for this, but "safest available" is not
 |   the same as "documented free": no SDK sample uses this driver and the
 |   header's comments are Shift-JIS mojibake. If audio misbehaves in a way that
 |   looks like driver corruption rather than silence, suspect this constant
 |   first.
 | Author: suinevere
 ----------------------*/
/*----------------------
 | PCM_SOUND_OFFSET
 | Description: Where in sound RAM the driver stages samples, taken from the
 |   area map rather than guessed at.
 |
 |   BOOTSND.MAP -- the map SRL hands to SND_Init -- is ten 8-byte records
 |   terminated by 0xFFFF, each [u16 id][u16 addr][u32 size]. That reading is
 |   not a guess: the declared areas tile exactly, 0x3000+0x1000 = 0x4000,
 |   0x4000+0x2000 = 0x6000, and the 0x2x01 group runs 0x6000 to 0x8600 in
 |   0x600 steps. Ten records agreeing is not coincidence.
 |
 |   The map declares id 0x3005 at 0xa000, size 0x20000 -- a 128 KB sub-area of
 |   the 0x0101 user region at 0x8600. Staging PCM is what such a declaration is
 |   for, and 0x4000 of it covers this buffer's two banks of 8192 samples.
 |
 |   The previous value was 0x70000, chosen as "the top of the bank, above
 |   everything the map mentions". That is outside every declared area, which is
 |   not the same thing as free -- and a driver writing its staging buffer
 |   somewhere the sound system does not consider PCM memory is a good way to
 |   get audio with a constant noise floor under it.
 | Author: suinevere
 ----------------------*/
#define PCM_SOUND_OFFSET   0xa000

/*----------------------
 | PCM_SOUND_SAMPLES
 | Description: 4096*2, the smallest the driver accepts.
 |
 |   PcmCreatePara's own comment claims the range is 4096*1..4096*16. That is
 |   wrong: PCM_ERR_ILL_SIZE_PCMBUF documents it as 4096*2..4096*16, and the
 |   error code is the authority since it is what the driver actually raises.
 |   The first attempt at this passed 4096*1 on the struct comment's word.
 | Author: suinevere
 ----------------------*/
#define PCM_SOUND_SAMPLES  (4096 * 2)

static PcmWork  g_pcmWork;
static PcmHn    g_pcmHandle = nullptr;
static int8_t  *g_ring      = nullptr;

/*----------------------
 | Rate measurement
 | Description: TEMPORARY. How many bytes a second the driver actually takes.
 |
 |   Audio lags the picture by about two seconds, but ring (4096 bytes) plus
 |   staging (8192) at the assumed 11025 frames a second is only 557 ms. The
 |   model is wrong by a factor of three or four and both of its inputs are
 |   assumptions, not measurements:
 |
 |     - the driver may consume 11025 BYTES a second rather than frames. The
 |       stream is interleaved stereo at 2 bytes a frame, so that is half the
 |       rate assumed here -- double the latency, and the music an octave low.
 |     - pcm_size is 4096*2, in units the header does not state. If samples
 |       rather than bytes, staging is twice what is assumed.
 |
 |   In steady state the ring stays full, so production equals consumption and
 |   bytes written per second IS the driver's rate. That one number separates
 |   the two cases:
 |
 |     ~11025 -> bytes-per-second, so 5512 frames a second: everything plays at
 |               half speed and every buffer lasts twice as long as intended
 |     ~22050 -> frames-per-second as assumed, the rate is right, and the
 |               latency is buffer depth to be found elsewhere
 | Author: suinevere
 ----------------------*/
static uint32_t g_rateAccum = 0;
static uint32_t g_rateTick  = 0;
static uint32_t g_rateShown = 0;

/*----------------------
 | g_underruns
 | Description: TEMPORARY. Times the driver reported the whole ring free.
 |
 |   The pops are now regular -- about every half second -- while the measured
 |   byte rate matches what is declared, so production is keeping up on average.
 |   That leaves two causes needing opposite fixes:
 |
 |     starvation      the ring empties briefly on a worst-case gap. A bigger
 |                     ring fixes it, at the cost of latency.
 |     driver-internal a discontinuity at the staging buffer's wrap, roughly
 |                     8192 bytes and so a quarter second at this rate. A bigger
 |                     ring does nothing for it.
 |
 |   If the driver ever says the entire ring is free, it has consumed everything
 |   written and run dry: that is starvation, and this counter climbs. If it
 |   stays at zero while the popping continues, the ring is never empty and the
 |   fault is past it.
 | Author: suinevere
 ----------------------*/
static uint32_t g_maxFree = 0;

/*----------------------
 | g_mixClips
 | Description: TEMPORARY. Samples that hit the clamp inside Mixer::mix, defined
 |   in mixer.cxx. Shown per second next to the dry count so the two candidate
 |   causes of content-dependent crackle can be told apart in one run:
 |
 |     dry climbing, clip near 0   starvation. The mixer cannot keep up at this
 |                                 rate when several channels are busy. Lower
 |                                 SAMPLE_RATE.
 |     clip large, dry 0           clipping. Four channels summed into 8 bits
 |                                 saturate. Rate is irrelevant; the answer is
 |                                 headroom in the mix.
 |     both                        both, and they are independent.
 | Author: suinevere
 ----------------------*/
extern "C" uint32_t g_mixClips;

static uint32_t g_freeShown = 0;

/*----------------------
 | TimerSlot / g_timers
 | Description: Only SfxPlayer uses timers, and only one at a time, but a small
 |   fixed array costs nothing and avoids special-casing a single slot.
 | Author: suinevere
 ----------------------*/
#define MAX_TIMERS 4

struct TimerSlot
{
    bool             used;
    uint32_t         delay;
    uint32_t         due;
    SatTimerCallback callback;
    void            *param;
};

static TimerSlot g_timers[MAX_TIMERS];

/*----------------------
 | calcPitch
 | Description: Encodes a sample rate as the SCSP's octave/FNS pitch word.
 |
 |   This reproduces SGL's PCM_CALC_* macros rather than calling them: those
 |   macros index SRL's LogTable, which is a private member of SRL::Sound::Pcm
 |   and so unreachable from here. The table is just floor(log2(n)), which is
 |   cheaper to recompute than to duplicate.
 | Author: suinevere
 ----------------------*/
static uint16_t calcPitch(uint32_t sampleRate)
{
    const uint32_t ratio = 44100u / (sampleRate + 1u);

    int32_t octave = 0;
    while ((ratio >> (octave + 1)) != 0)
    {
        octave++;
    }

    const int32_t shiftFreq = 44100 >> octave;
    const int32_t fns = (((int32_t)sampleRate - shiftFreq) << 10) / shiftFreq;

    return (uint16_t)((((-octave) & 0x0F) << 11) | (fns & 0x03FF));
}

/*----------------------
 | mixInto
 | Description: Pulls one buffer's worth of samples from the engine's mixer.
 |   Writes through the uncached mirror so the driver's DMA sees them.
 | Author: suinevere
 ----------------------*/
static void mixInto(int32_t index)
{
    if (g_callback != nullptr && g_buffer[index] != nullptr)
    {
        uint8_t *out = UNCACHED(g_buffer[index]);

        g_callback(g_callbackParam, out, BUFFER_SAMPLES);

        // Same +128 bias to undo as on the streaming path -- see the note in
        // streamUpdate. This path is the fallback and normally never runs, but
        // leaving it reading unsigned data as signed would mean a failure to
        // start the stream driver degraded into distortion rather than into
        // plain stuttery audio, which defeats the point of having a fallback.
        for (int32_t i = 0; i < BUFFER_SAMPLES; i++)
        {
            out[i] = (uint8_t)(int8_t)((int)out[i] - 128);
        }
    }
}

/*----------------------
 | armBuffer
 | Description: Starts one buffer playing on its own PCM channel.
 | Author: suinevere
 ----------------------*/
static void armBuffer(int32_t index)
{
    g_pcm[index].mode  = _Mono | _PCM8Bit;
    g_pcm[index].pitch = calcPitch(SAMPLE_RATE);
    g_pcm[index].level = 127;
    g_pcm[index].pan   = 0;

    slPCMOn(&g_pcm[index], (void *)g_buffer[index], BUFFER_SAMPLES);
    g_playing = index;
}

/*----------------------
 | streamStart
 | Description: Brings up the LIBPCM stream driver. Returns false if any step
 |   refuses, leaving nothing allocated so the caller can fall back cleanly.
 |
 |   The handle is a "memory file" one: the driver treats our ring as the file
 |   it is playing, and we keep rewriting it ahead of the read cursor. There is
 |   no header on that data, hence PCM_FILE_TYPE_NO_HEADER and an explicitly
 |   supplied PcmInfo.
 | Author: suinevere
 ----------------------*/
/*----------------------
 | streamDiag
 | Description: TEMPORARY. Reports which step of the driver bring-up refused and
 |   what the driver says about it, then holds it long enough to read. There is
 |   no other channel here: the fallback makes a failure sound identical to the
 |   previous build, which is good for the player and useless for diagnosis.
 |   Remove once streaming is trusted.
 | Author: suinevere
 ----------------------*/
static void streamDiag(const char *step)
{
    SRL::Debug::Print(1, 22, "PCM %s failed", step);
    SRL::Debug::Print(1, 23, "PCM_GetErr = %08X", (unsigned int)PCM_GetErr());

    for (int32_t i = 0; i < 180; i++)
    {
        SRL::Core::Synchronize();
    }
}

static bool streamStart(void)
{
    if (!PCM_Init())
    {
        streamDiag("Init");
        return false;
    }

    g_ring = (int8_t *)SRL::Memory::LowWorkRam::Malloc(RING_BYTES);

    if (g_ring == nullptr)
    {
        streamDiag("ring alloc");
        return false;
    }

    for (int32_t i = 0; i < RING_BYTES; i++)
    {
        UNCACHED(g_ring)[i] = 0;
    }

    PcmCreatePara para;
    para.work      = &g_pcmWork;
    para.ring_addr = g_ring;
    para.ring_size = RING_BYTES;
    para.pcm_addr  = (int8_t *)(SoundRAM + PCM_SOUND_OFFSET);
    para.pcm_size  = PCM_SOUND_SAMPLES;

    g_pcmHandle = PCM_CreateMemHandle(&para);

    if (g_pcmHandle == nullptr)
    {
        streamDiag("CreateMemHandle");
        SRL::Memory::LowWorkRam::Free(g_ring);
        g_ring = nullptr;
        return false;
    }

    // The engine's mixer produces a stream with no end. There is no honest file
    // length to give, so these are set large enough that the driver never
    // reaches them; playback is stopped explicitly, not by running out.
    PcmInfo info;
    info.file_type        = PCM_FILE_TYPE_NO_HEADER;
    // channel = 1 is what governs; the driver was measured taking exactly
    // sampling_rate bytes per second, which is mono. data_type only describes
    // how a stereo stream is interleaved, so it has nothing to say here -- it is
    // left at the enum's first value rather than given meaning it does not have.
    info.data_type        = PCM_DATA_TYPE_LRLRLR;
    // Large enough that the driver never reaches the end -- 0x40000000 samples
    // is around 27 hours at 11 kHz -- but not INT32_MAX, which the driver's own
    // running sample counters would overflow on the first addition.
    info.file_size        = 0x40000000;
    info.channel          = 1;
    info.sampling_bit     = 8;
    info.sampling_rate    = SAMPLE_RATE;
    info.sample_file      = 0x40000000;
    info.compression_type = 0;

    PCM_SetInfo(g_pcmHandle, &info);

    // Tell the driver not to look for a header. The data we supply is raw
    // mixer output with nothing in front of it, so left to itself the driver
    // parses the first samples as an AIFF header and plays from wherever that
    // lands it. There is no setter for this flag, but the header's own
    // accessors (PCM_HN_CNT_LOOP and friends) document the dereference: a
    // PcmHn is a pointer to a PcmWork pointer.
    (*(PcmWork **)g_pcmHandle)->status.ignore_header = (PcmFlag)1;
    (*(PcmWork **)g_pcmHandle)->status.need_ci       = (PcmFlag)0;

    PCM_Start(g_pcmHandle);

    // Level and pan go in AFTER Start, followed by PCM_ChangePcmPara to commit
    // them. Setting them before Start had no audible effect at all, which fits:
    // these live in the handle's para block and the driver picks that block up
    // when playback begins, so a pre-Start write is just overwritten.
    //
    // PcmPara documents pcm_level as [0..7] -- a coarse 3-bit scale, nothing
    // like the [0..127] the slPCM path used -- and pcm_pan as [0..31], so 16 is
    // centre. PCM_LEVEL carries the reasoning for the number.
    PCM_SetVolume(g_pcmHandle, PCM_LEVEL);
    PCM_SetPan(g_pcmHandle, 16);
    PCM_ChangePcmPara(g_pcmHandle);

    return true;
}

/*----------------------
 | streamUpdate
 | Description: Services the driver and tops the ring up. PCM_GetWriteBuf hands
 |   back where to write and how much room is contiguous; we mix exactly that
 |   much and tell the driver what we produced.
 |
 |   This is where the gaplessness comes from: the driver is reading
 |   continuously and never stops, so there is no boundary for silence to fall
 |   into. Nothing here has to be timed against a frame.
 | Author: suinevere
 ----------------------*/
static void streamUpdate(void)
{
    // NOT PCM_VblIn here. That is a vblank notification and belongs in the
    // vblank handler, called exactly once per frame -- see sat_audio_vblank.
    // This function is deliberately called from wherever the engine happens to
    // be (render, sleep, CD reads, the unpack loop), which is thousands of
    // times a second during a load; telling the driver that many vblanks had
    // passed ran its timing far ahead of real time.
    PCM_Task(g_pcmHandle);

    // Keep asking until the driver stops offering room, rather than taking the
    // first chunk and leaving. GetWriteBuf hands back only what is contiguous,
    // and the driver accounts in 1024-byte tasks, so a single call routinely
    // returns 1024 while the ring has more free than that. Filling once per
    // pump therefore left the ring part empty however often it was pumped,
    // which is what made 46 ms too shallow to absorb any jitter.
    //
    // Bounded, because this also runs in the vblank interrupt: at most the
    // whole ring, and it stops as soon as the driver says it is full.
    for (int32_t pass = 0; pass < (int32_t)(RING_BYTES / 1024) + 1; pass++)
    {
        int32_t freeSize  = 0;
        int32_t freeTotal = 0;
        uint32_t *write = PCM_GetWriteBuf(g_pcmHandle, &freeSize, &freeTotal);

        // TEMPORARY. How empty the ring ever gets, which is what the old
        // boolean could not say: it tested for the whole ring being free, and
        // the driver never reports that, so it read 1 while the ring was in
        // fact oscillating around empty. Near RING_BYTES means starvation.
        if (pass == 0 && freeTotal > (int32_t)g_maxFree)
        {
            g_maxFree = (uint32_t)freeTotal;
        }

        if (write == nullptr || freeSize <= 0 || g_callback == nullptr)
        {
            break;
        }

        // One byte in, one sample out. The driver consumes this buffer as MONO
        // at info.sampling_rate: measured directly, it takes sampling_rate
        // BYTES per second, quantised to its 1024-byte task size.
        //
        // There was a stereo duplication here, writing each mixer sample twice.
        // It was added on the theory that the driver read the stream as
        // interleaved stereo and so decimated a mono feed into harsh broadband
        // noise. The theory was wrong -- that noise was the signed/unsigned
        // mismatch fixed in 8c7b2e4 -- and the duplication made things worse in
        // a way that hid behind it: at a fixed byte rate, two bytes per mixer
        // sample means every sound played at half speed, an octave low, and
        // every buffer lasted twice as long as intended. That was most of the
        // two-second lag behind the picture.
        int32_t frames = freeSize;

        if (frames > (int32_t)sizeof(g_monoTemp))
        {
            frames = (int32_t)sizeof(g_monoTemp);
        }

        g_callback(g_callbackParam, g_monoTemp, (int)frames);

        uint8_t *out = UNCACHED(write);

        for (int32_t i = 0; i < frames; i++)
        {
            // The mixer hands back UNSIGNED 8-bit, not signed: Mixer::mix ends
            // by adding 128 to every sample, a workaround for SDL hanging on
            // signed 8-bit under PulseAudio (mixer.cxx:143). The SCSP wants
            // signed, so the bias has to come back off here.
            //
            // Reading the biased stream as signed was the distortion. Silence,
            // unsigned 128, became -128 -- full scale negative -- and every
            // sample above 127 wrapped to the bottom of the range, folding the
            // waveform in half at its midpoint. That is heard as harsh
            // broadband buzz tracking the music, and it also explains the
            // loudness, since wrapping puts energy at full scale whatever the
            // real signal was doing.
            //
            // It survived a square wave test because that tone, 100 and 156, is
            // symmetric about 128 and so looks identical under both readings.
            out[i] = (uint8_t)(int8_t)((int)g_monoTemp[i] - 128);
        }

        PCM_NotifyWriteSize(g_pcmHandle, frames);
        g_rateAccum += (uint32_t)frames;   /* TEMPORARY */
    }
}

/*----------------------
 | streamStop
 | Description: Stops playback and releases the driver and the ring.
 | Author: suinevere
 ----------------------*/
static void streamStop(void)
{
    if (g_pcmHandle != nullptr)
    {
        PCM_Stop(g_pcmHandle);
        PCM_DestroyMemHandle(g_pcmHandle);
        g_pcmHandle = nullptr;
    }

    PCM_Finish();

    if (g_ring != nullptr)
    {
        SRL::Memory::LowWorkRam::Free(g_ring);
        g_ring = nullptr;
    }
}

/*----------------------
 | g_pumping / pumpOutput
 | Description: Refills the driver, from either the main loop or the vblank
 |   interrupt.
 |
 |   Pumping from vblank is what makes the ring survive a slow frame. Everything
 |   else that tops it up runs when the engine reaches a particular point -- the
 |   end of a rendered frame, a CD window, a step of the decode loop -- so a
 |   frame that takes longer than the ring holds starves it however large the
 |   ring is. That is why the popping tracked the video. A vblank arrives every
 |   16.7 ms whatever the main loop is doing, including while it sits blocked
 |   inside a GFS read.
 |
 |   The guard stops the interrupt re-entering a refill already in progress on
 |   the main loop, which would have two callers inside PCM_Task and Mixer::mix
 |   at once. Both can pass the test in the two-instruction window between it and
 |   the store, but that is harmless: the interrupt runs to completion before the
 |   main loop resumes, so the two refills are sequential rather than
 |   overlapping.
 |
 |   Mixing from an interrupt also needs Mixer::playChannel to publish a channel
 |   only once it is fully described -- see the ordering note in mixer.cxx.
 | Author: suinevere
 ----------------------*/
static volatile bool g_pumping = false;

static void pumpOutput(void)
{
    if (g_pumping || !g_running)
    {
        return;
    }

    g_pumping = true;

    if (g_mode == MODE_STREAM)
    {
        // Bounded work: the driver hands back at most one task's worth of space
        // per call, 1024 bytes, so this cannot sit in the interrupt mixing an
        // entire ring.
        streamUpdate();
    }
    else if (g_playing >= 0 && !slPCMStat(&g_pcm[g_playing]))
    {
        // slPCMStat reports true while a channel is still busy.
        const int32_t finished = g_playing;
        const int32_t next     = finished ^ 1;

        // Arm before mixing, never after: the gap between one buffer ending and
        // the next starting is silence, so nothing slow belongs in front of it.
        armBuffer(next);
        mixInto(finished);
    }

    g_pumping = false;
}

extern "C" void sat_audio_vblank(void)
{
    if (g_running && g_mode == MODE_STREAM)
    {
        PCM_VblIn();

        // The refill that does not depend on the engine reaching anywhere.
        pumpOutput();

        // TEMPORARY. Once a second, from the one place that runs exactly once a
        // frame. See the note on g_rateAccum for how to read it.
        const uint32_t now = sat_time_ms();

        if (now - g_rateTick >= 1000)
        {
            g_rateShown = g_rateAccum;
            g_freeShown = g_maxFree;
            g_rateAccum = 0;
            g_maxFree   = 0;
            g_mixClips  = 0;
            g_rateTick  = now;
        }

        SRL::Debug::Print(1, 20, "RATE %d      ", (int)g_rateShown);
        SRL::Debug::Print(1, 21, "EMPTY %d/%d  ",
                          (int)g_freeShown, (int)RING_BYTES);
    }
}

extern "C" uint32_t sat_audio_sample_rate(void)
{
    return SAMPLE_RATE;
}

extern "C" void sat_audio_start(SatAudioCallback callback, void *param)
{
    if (g_running)
    {
        return;
    }

    g_callback      = callback;
    g_callbackParam = param;

    // Streaming first. It is the only path that produces continuous audio: the
    // fallback below can only start a new buffer at a frame boundary, so a
    // slice of silence lands between every buffer and the result stutters.
    if (streamStart())
    {
        g_running = true;
        g_mode    = MODE_STREAM;
        return;
    }

    for (int32_t i = 0; i < BUFFER_COUNT; i++)
    {
        // Low Work RAM: these are small, but High Work RAM is the bank the
        // rasterizer's video pages live in and there is no reason to crowd it
        // for something the CPU touches once per 209 ms.
        g_buffer[i] = (uint8_t *)SRL::Memory::LowWorkRam::Malloc(BUFFER_SAMPLES);

        if (g_buffer[i] == nullptr)
        {
            // Audio is not worth failing a working build over. Release whatever
            // was taken and stay silent.
            for (int32_t j = 0; j < BUFFER_COUNT; j++)
            {
                if (g_buffer[j] != nullptr)
                {
                    SRL::Memory::LowWorkRam::Free(g_buffer[j]);
                    g_buffer[j] = nullptr;
                }
            }

            g_callback = nullptr;
            return;
        }
    }

    g_running = true;
    g_mode    = MODE_BUFFERS;

    // Prime both: one to play now, one already mixed and waiting so the swap in
    // sat_audio_update costs nothing but the slPCMOn call.
    mixInto(0);
    mixInto(1);
    armBuffer(0);
}

extern "C" void sat_audio_stop(void)
{
    if (!g_running)
    {
        return;
    }

    if (g_mode == MODE_STREAM)
    {
        streamStop();
    }
    else
    {
        for (int32_t i = 0; i < BUFFER_COUNT; i++)
        {
            slPCMOff(&g_pcm[i]);

            if (g_buffer[i] != nullptr)
            {
                SRL::Memory::LowWorkRam::Free(g_buffer[i]);
                g_buffer[i] = nullptr;
            }
        }
    }

    g_callback = nullptr;
    g_playing  = -1;
    g_running  = false;
    g_mode     = MODE_SILENT;
}

extern "C" void sat_audio_update(void)
{
    // Timers first: the music sequencer queues notes onto mixer channels, so
    // running it before the mix means a note started this tick is audible in
    // the buffer mixed this tick rather than the next one.
    const uint32_t now = sat_time_ms();

    for (int32_t i = 0; i < MAX_TIMERS; i++)
    {
        TimerSlot *slot = &g_timers[i];

        if (slot->used && now >= slot->due)
        {
            const uint32_t next = slot->callback(slot->delay, slot->param);

            if (next == 0)
            {
                // Returning 0 retires the timer, matching the SDL semantics
                // SfxPlayer was written against.
                slot->used = false;
            }
            else
            {
                slot->delay = next;
                slot->due   = now + next;
            }
        }
    }

    pumpOutput();
}

extern "C" int sat_timer_add(uint32_t delay, SatTimerCallback callback, void *param)
{
    if (callback == nullptr)
    {
        return 0;
    }

    for (int32_t i = 0; i < MAX_TIMERS; i++)
    {
        if (!g_timers[i].used)
        {
            g_timers[i].used     = true;
            g_timers[i].delay    = delay;
            g_timers[i].due      = sat_time_ms() + delay;
            g_timers[i].callback = callback;
            g_timers[i].param    = param;
            return i + 1;
        }
    }

    return 0;
}

extern "C" void sat_timer_remove(int timerId)
{
    const int32_t index = timerId - 1;

    if (index >= 0 && index < MAX_TIMERS)
    {
        g_timers[index].used = false;
    }
}
