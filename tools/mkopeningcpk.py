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

  Mono at 32 kHz, which is what SRL's own known-good sample (SKYBL.CPK) uses.
  Not a free choice: CPK plays movie audio through the SGL 68000 driver and
  takes its playback clock from that driver, so anything the driver mishandles
  stops the picture as well as the sound. Stereo at 22 kHz was tried first and
  there is no evidence in the tree that SGL's PCM path takes stereo, so this
  matches the reference exactly rather than guessing. Mono also halves the
  sound RAM the buffer needs, which is what lets it fit the 64 KB
  saturn_scsp.cxx reserves for it.

  ffmpeg writes the FILM timebase as the frame rate (base_freq 25 here), where
  SEGA's own encoder wrote 600 with per-frame durations of 20. That difference
  is not reachable through ffmpeg's CLI. It has not caused trouble, but it is
  the first thing to suspect if playback timing ever looks wrong.
Author: suinevere
Usage: python tools/mkopeningcpk.py
"""
import os
import shutil
import subprocess

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "images", "Another World (Europe).avi")
OUT = os.path.join(ROOT, "saturn", "cd", "data", "OPENING.CPK")

DURATION = 31.5              # seconds of front matter, up to the fade to black
FPS = 25                     # exact 2:1 decimation of the 50 fps source
AUDIO_RATE = 32000           # matches SRL's known-good SKYBL.CPK
AUDIO_CHANNELS = 1

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


def encode():
    """Cut the front matter out of SRC and write it to OUT as Cinepak."""
    if not os.path.exists(SRC):
        raise SystemExit("source not found: %s" % SRC)

    os.makedirs(os.path.dirname(OUT), exist_ok=True)

    # pcm_s16be_planar, not pcm_s16be. The film_cpk muxer rejects the
    # interleaved variant outright with "Incompatible audio stream format".
    subprocess.check_call([
        find_ffmpeg(), "-v", "error", "-y",
        "-t", str(DURATION), "-i", SRC,
        "-c:v", "cinepak", "-r", str(FPS),
        "-c:a", "pcm_s16be_planar", "-ar", str(AUDIO_RATE), "-ac", str(AUDIO_CHANNELS),
        "-f", "film_cpk", OUT,
    ])

    size = os.path.getsize(OUT)
    print("wrote %s (%.1f MB, %.0f KB/s)" % (OUT, size / 1048576.0,
                                             size / 1024.0 / DURATION))


if __name__ == "__main__":
    encode()
