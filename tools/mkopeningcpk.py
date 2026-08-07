"""
mkopeningcpk.py
Description: Builds saturn/cd/data/OPENING.CPK from the Mega Drive capture in
  images/, as Cinepak video in a Sega FILM container -- the format SRL's
  CinepakPlayer reads. Run from the repository root. The output is git-ignored
  and regenerated on demand.

  The capture runs from cold boot to the first room; only the front matter is
  wanted. SEGA holds to 5s, Virgin to 10s, Delphine to 15s, the wordmark and its
  lightning to 30s, and the fade lands on black by 31s. The diary text starts at
  32.5s and is cut: the engine draws that itself from bank data.

  25 fps, not 30. The source is 50 fps PAL, so 25 is an exact 2:1 decimation --
  every kept frame is a real source frame. 30 would need an uneven 5:3 pulldown
  and would judder the lightning against its own duplicated frames.

  Mono at 32 kHz, matching SRL's own SKYBL.CPK exactly. Not a free choice:
  movie audio goes through the SGL 68000 driver, and there is no evidence in
  the tree that its PCM path takes stereo. Mono also halves the sound RAM the
  buffer needs, which is what lets it fit the 64 KB saturn_scsp.cxx reserves
  at offset 0x020000.

  The opening was briefly encoded silent, on the belief that movie audio could
  not work because bringing the sound driver up hung the machine. That was a
  misread: those builds crashed in cpk_VideoSampleCvid, the video decoder, on
  the odd-chunk alignment bug that was present the whole time and would have
  crashed with the driver either way. The audio path was in fact running -- the
  symptom was one PCM buffer looping, which is a buffer that was filled once
  and never refilled because the CPU had died decoding video.

  The output is word aligned afterwards by align(), and that is the thing that
  actually makes it play. ffmpeg leaves vector chunks at odd lengths; SEGA's
  decoder reads the stream with 16-bit loads, and the SH-2 address errors on
  the first unaligned one. The file crashed on its very first strip, 6153
  bytes. SRL's own SKYBL.CPK has not one odd chunk, strip, frame or sample
  offset anywhere in it.

  skip_empty_cb is the one that stops SEGA's decoder crashing, and it is not
  optional. Left at its default, ffmpeg emits a zero-entry FULL codebook chunk
  (0x2000 or 0x2200, four bytes, no payload) inside inter strips -- its help
  text calls this keeping the vintage MacOS decoder happy. SEGA's decoder takes
  it literally, empties the codebook, and then the inter vectors in the same
  strip index into nothing. The second frame of the movie did exactly this, and
  cpk_VideoSampleCvid faulted on it every run: one frame on screen, the SCSP
  looping its last buffer, the SH-2 parked in the BIOS exception handler.
  SRL's own SKYBL.CPK never puts a full codebook in an inter strip at all -- it
  uses partial updates (0x2100/0x2300), which ffmpeg cannot emit.

  Keep every frame's chunk list free of four-byte 0x2000/0x2200 entries if this
  is ever re-tuned. That is the invariant, not the flag.

  The strip count is pinned to a constant 2 and that is load bearing. ffmpeg
  picks a strip count per frame by default (min_strips 1, max_strips 3), so it
  emits a mix of 1 and 2 strip frames. SEGA's decoder does not survive the
  count changing mid stream: inter frames reference the previous frame's
  per-strip codebooks, and when the strips are renumbered underneath it,
  cpk_VideoSampleCvid walks off into an address error and the SH-2 sits in the
  BIOS exception handler with interrupts masked. That looks exactly like a
  freeze -- one frame on screen, the SCSP looping its last buffer, no CPK error
  ever raised, because the CPU is gone rather than stuck. SRL's own SKYBL.CPK
  is a constant 2 strips, so that is what this matches.

  ffmpeg writes the FILM timebase as the frame rate (base_freq 25 here), where
  SEGA's own encoder wrote 600 with per-frame durations of 20. That difference
  is not reachable through ffmpeg's CLI. It has not caused trouble, but it is
  the first thing to suspect if playback timing ever looks wrong.
Author: suinevere
Usage: python tools/mkopeningcpk.py
"""
import os
import shutil
import struct
import subprocess

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "images", "Another World (Europe).avi")
OUT = os.path.join(ROOT, "saturn", "cd", "data", "OPENING.CPK")

DURATION = 31.5              # seconds of front matter, up to the fade to black
FPS = 25                     # exact 2:1 decimation of the 50 fps source
STRIPS = 2                   # constant, matching SKYBL.CPK -- see the note above
AUDIO_RATE = 32000           # matches SKYBL.CPK; the SGL PCM path is mono
AUDIO_CHANNELS = 1
AUDIO_BLOCK = 16000          # bytes per audio packet, 0.25s -- SKYBL's block size
AUDIO_LEAD = 0.5             # seconds of audio handed over before the video needs it

# Where ffmpeg gets installed when it is not on PATH. Checked as a fallback so
# the tool works straight after a winget install without a shell restart.
FFMPEG_FALLBACKS = [
    os.path.join(os.environ.get("LOCALAPPDATA", ""), "Microsoft", "WinGet", "Packages",
                 "Gyan.FFmpeg_Microsoft.Winget.Source_8wekyb3d8bbwe",
                 "ffmpeg-9.0-full_build", "bin", "ffmpeg.exe"),
]


def find_ffmpeg():
    found = shutil.which("ffmpeg")
    if found:
        return found
    for path in FFMPEG_FALLBACKS:
        if path and os.path.exists(path):
            return path
    raise SystemExit("ffmpeg not found: put it on PATH, or add its location to "
                     "FFMPEG_FALLBACKS in tools/mkopeningcpk.py")


def align_frame(frame):
    """Pad every chunk in one Cinepak frame to an even length.

    The frame's own 24-bit length field is eight less than the frame really
    is -- both ffmpeg and SEGA's encoder write it that way, and the strips run
    to the sample length the STAB gives. Trusting that field as the end of the
    data silently eats the last chunk of every frame, so the sample length is
    what bounds the walk and the field is rewritten to match on the way out.
    """
    strips = struct.unpack(">H", frame[8:10])[0]
    body = bytearray()
    pos = 12

    for _ in range(strips):
        if pos + 12 > len(frame):
            break
        sid, strip_size, y0, x0, y1, x1 = struct.unpack(">HHHHHH", frame[pos:pos + 12])
        chunks = bytearray()
        cursor = pos + 12
        end = min(pos + strip_size, len(frame))

        while cursor + 4 <= end:
            cid, chunk_size = struct.unpack(">HH", frame[cursor:cursor + 4])
            if chunk_size < 4:
                raise SystemExit("chunk %04X claims %d bytes" % (cid, chunk_size))
            data = frame[cursor + 4:min(cursor + chunk_size, end)]
            if len(data) & 1:
                data += b"\x00"
            chunks += struct.pack(">HH", cid, len(data) + 4) + data
            cursor += chunk_size

        if cursor != end:
            raise SystemExit("strip %04X did not consume exactly: stopped at %d "
                             "of %d" % (sid, cursor, end))

        body += struct.pack(">HHHHHH", sid, len(chunks) + 12, y0, x0, y1, x1) + chunks
        pos += strip_size

    if pos != len(frame):
        raise SystemExit("frame did not consume exactly: %d of %d" % (pos, len(frame)))

    total = len(body) + 12
    field = total - 8
    head = bytearray(frame[:12])
    head[1] = (field >> 16) & 0xFF
    head[2] = (field >> 8) & 0xFF
    head[3] = field & 0xFF
    return bytes(head) + bytes(body)


def align():
    """Rewrite OUT so every chunk, strip, frame and sample offset is even.

    SEGA's decoder reads the stream with 16-bit loads and the SH-2 raises an
    address error on an unaligned one, so a single odd-sized chunk anywhere
    kills the machine the moment the decoder steps past it. ffmpeg's Cinepak
    encoder does not pad, and its own decoder does not care. Only the vector
    chunks are ever odd -- a codebook is always 4 + 6n bytes -- and their
    trailing bytes are never read, because the decoder consumes exactly as many
    blocks as the strip geometry calls for. So a pad byte is invisible to it.
    """
    d = open(OUT, "rb").read()
    header_len = struct.unpack(">I", d[4:8])[0]
    header = bytearray(d[:header_len])

    off = 16
    stab = None
    while off < header_len:
        tag = header[off:off + 4]
        size = struct.unpack(">I", header[off + 4:off + 8])[0]
        if tag == b"STAB":
            _, count = struct.unpack(">II", header[off + 8:off + 16])
            stab = off + 16
            break
        off += size

    if stab is None:
        raise SystemExit("no STAB chunk in %s" % OUT)

    body = bytearray()
    entries = []

    for i in range(count):
        pos = stab + i * 16
        offset, length, info1, info2 = struct.unpack(">IIII", header[pos:pos + 16])
        sample = d[header_len + offset:header_len + offset + length]
        if info1 != 0xFFFFFFFF and len(sample) >= 24:
            sample = align_frame(sample)
        if len(body) & 1:
            body += b"\x00"
        entries.append((len(body), len(sample), info1, info2))
        body += sample

    for i, entry in enumerate(entries):
        struct.pack_into(">IIII", header, stab + i * 16, *entry)

    with open(OUT, "wb") as f:
        f.write(bytes(header))
        f.write(bytes(body))


def remux_audio():
    """Re-lay the audio in SEGA's shape: big blocks, running ahead of the video.

    ffmpeg emits one small audio packet per half video frame -- fifty a second,
    1280 bytes each -- and puts the first video frame ahead of any of them, so
    the PCM buffer is never more than a hair in front of what the SCSP is
    playing. SRL's own SKYBL.CPK hands over 32000 bytes, half a second, before
    a single video frame and then feeds quarter-second blocks. Ours skipped
    twice a second and sounded detuned until it did the same; SEGA's file plays
    clean in this port untouched, so the layout is the difference.

    Nothing about the audio itself changes. The samples are concatenated in
    presentation order and re-cut, so the PCM is byte for byte what it was.
    ffmpeg has no option for any of this -- frame_size is rejected by the PCM
    encoders and max_interleave_delta changes nothing here.
    """
    d = open(OUT, "rb").read()
    header_len = struct.unpack(">I", d[4:8])[0]

    off = 16
    fdsc = None
    stab = None
    while off < header_len:
        tag = d[off:off + 4]
        size = struct.unpack(">I", d[off + 4:off + 8])[0]
        if tag == b"FDSC":
            fdsc = d[off:off + size]
        elif tag == b"STAB":
            base_freq, count = struct.unpack(">II", d[off + 8:off + 16])
            stab = off + 16
        off += size

    if fdsc is None or stab is None:
        raise SystemExit("%s is missing FDSC or STAB" % OUT)

    video = []
    audio = bytearray()

    for i in range(count):
        offset, length, info1, info2 = struct.unpack(">IIII", d[stab + i * 16:stab + 16 + i * 16])
        payload = d[header_len + offset:header_len + offset + length]
        if info1 == 0xFFFFFFFF:
            audio += payload
        else:
            video.append((info1, info2, payload))

    if not audio:
        return

    bytes_per_second = AUDIO_RATE * 2 * AUDIO_CHANNELS
    blocks = [bytes(audio[i:i + AUDIO_BLOCK]) for i in range(0, len(audio), AUDIO_BLOCK)]
    block_ticks = (AUDIO_BLOCK / float(bytes_per_second)) * base_freq
    lead_ticks = AUDIO_LEAD * base_freq

    ordered = []
    next_block = 0

    for info1, info2, payload in video:
        timestamp = info1 & 0x7FFFFFFF
        while next_block < len(blocks) and (next_block * block_ticks) - lead_ticks <= timestamp:
            ordered.append((0xFFFFFFFF, 1, blocks[next_block]))
            next_block += 1
        ordered.append((info1, info2, payload))

    while next_block < len(blocks):
        ordered.append((0xFFFFFFFF, 1, blocks[next_block]))
        next_block += 1

    body = bytearray()
    entries = []
    for info1, info2, payload in ordered:
        if len(body) & 1:
            body += b"\x00"
        entries.append((len(body), len(payload), info1, info2))
        body += payload

    table = bytearray()
    for entry in entries:
        table += struct.pack(">IIII", *entry)

    stab_bytes = b"STAB" + struct.pack(">I", 16 + len(table)) + \
                 struct.pack(">II", base_freq, len(entries)) + bytes(table)

    new_header_len = 16 + len(fdsc) + len(stab_bytes)
    head = bytearray(d[:16])
    struct.pack_into(">I", head, 4, new_header_len)

    with open(OUT, "wb") as f:
        f.write(bytes(head))
        f.write(fdsc)
        f.write(stab_bytes)
        f.write(bytes(body))

    print("remuxed audio: %d packets of %d bytes, %.2fs primed ahead of video"
          % (len(blocks), AUDIO_BLOCK, AUDIO_LEAD))


def verify():
    """Fail loudly on the two bitstream shapes SEGA's decoder cannot survive."""
    d = open(OUT, "rb").read()
    header_len = struct.unpack(">I", d[4:8])[0]
    off = 16
    table = None

    while off < header_len:
        tag = d[off:off + 4]
        size = struct.unpack(">I", d[off + 4:off + 8])[0]
        if tag == b"STAB":
            _, count = struct.unpack(">II", d[off + 8:off + 16])
            table = (off + 16, count)
            break
        off += size

    if table is None:
        raise SystemExit("no STAB chunk in %s" % OUT)

    start, count = table
    empty = 0
    odd = 0
    strip_counts = set()

    for i in range(count):
        offset, length, info1, _ = struct.unpack(">IIII", d[start + i * 16:start + 16 + i * 16])
        if offset & 1 or length & 1:
            odd += 1
        if info1 == 0xFFFFFFFF:
            continue
        frame = d[header_len + offset:header_len + offset + length]
        if len(frame) < 24:
            continue
        strips = struct.unpack(">H", frame[8:10])[0]
        strip_counts.add(strips)
        pos = 12
        for _ in range(strips):
            if pos + 12 > len(frame):
                break
            _, strip_size = struct.unpack(">HH", frame[pos:pos + 4])
            if strip_size & 1:
                odd += 1
            cursor = pos + 12
            end = min(pos + strip_size, len(frame))
            while cursor + 4 <= end:
                chunk, chunk_size = struct.unpack(">HH", frame[cursor:cursor + 4])
                if chunk_size < 4:
                    break
                if chunk_size & 1:
                    odd += 1
                if chunk in (0x2000, 0x2200) and chunk_size == 4:
                    empty += 1
                cursor += chunk_size
            pos += strip_size

    if odd:
        raise SystemExit("%s has %d odd-length chunks, strips or samples. The "
                         "SH-2 address errors on the first unaligned 16-bit read "
                         "SEGA's decoder makes. Did align() run?" % (OUT, odd))

    if empty:
        raise SystemExit("%s has %d empty full-codebook chunks; SEGA's decoder "
                         "faults on these. Is -skip_empty_cb still set?" % (OUT, empty))

    if strip_counts != {STRIPS}:
        raise SystemExit("%s varies its strip count (%s); SEGA's decoder faults "
                         "when it changes mid stream." % (OUT, sorted(strip_counts)))

    print("verified: constant %d strips, everything word aligned, "
          "no empty codebook chunks" % STRIPS)


def encode():
    """Cut the front matter out of SRC and write it to OUT as Cinepak."""
    if not os.path.exists(SRC):
        raise SystemExit("source not found: %s" % SRC)

    os.makedirs(os.path.dirname(OUT), exist_ok=True)

    subprocess.check_call([
        find_ffmpeg(), "-v", "error", "-y",
        "-t", str(DURATION), "-i", SRC,
        "-c:v", "cinepak", "-r", str(FPS),
        "-min_strips", str(STRIPS), "-max_strips", str(STRIPS),
        "-skip_empty_cb", "1",
        # pcm_s16be_planar, not pcm_s16be. The film_cpk muxer rejects the
        # interleaved variant outright with "Incompatible audio stream format".
        "-c:a", "pcm_s16be_planar", "-ar", str(AUDIO_RATE), "-ac", str(AUDIO_CHANNELS),
        "-f", "film_cpk", OUT,
    ])

    align()
    remux_audio()
    verify()

    size = os.path.getsize(OUT)
    print("wrote %s (%.1f MB, %.0f KB/s)" % (OUT, size / 1048576.0,
                                             size / 1024.0 / DURATION))


if __name__ == "__main__":
    encode()
