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

  Video only. CPK routes movie audio through the SGL 68000 sound driver, and
  saturn_scsp.cxx stands that driver down at boot because this port programs
  the SCSP directly and the two cannot both own the chip. Leaving the driver up
  so the player could use it hung the machine about half a second in, inside
  the vblank handler where CPK_VblIn feeds the driver its PCM. A movie with no
  audio track sets play_pcm to 0 and is clocked off CPK_VblIn alone
  (sgl_cpk.h:333,340), which needs no driver and no sound RAM.

  Sound for the opening, if it is ever wanted, has to come from the port's own
  SCSP path rather than from the movie file.

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

    subprocess.check_call([
        find_ffmpeg(), "-v", "error", "-y",
        "-t", str(DURATION), "-i", SRC,
        "-c:v", "cinepak", "-r", str(FPS), "-an",
        "-f", "film_cpk", OUT,
    ])

    size = os.path.getsize(OUT)
    print("wrote %s (%.1f MB, %.0f KB/s)" % (OUT, size / 1048576.0,
                                             size / 1024.0 / DURATION))


if __name__ == "__main__":
    encode()
