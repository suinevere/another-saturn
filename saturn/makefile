# Configuration
SRL_MAX_TEXTURES = 100          # Number of VDP1 texture slots
SRL_MODE = NTSC                 # Valid options are PAL or NTSC
SRL_HIGH_RES = 0                # 480i mode
SRL_FRAMERATE = 1               # Framerate control (0=dynamic, 1=< 60/value)
SRL_MAX_CD_BACKGROUND_JOBS = 1  # Maximum number of files GFS can open at once
SRL_MAX_CD_FILES = 256          # Maximum number of files on a CD
SRL_MAX_CD_RETRIES = 5          # Number of times to retry on unsuccessful read

# Sound driver specific configuration
SRL_USE_SGL_SOUND_DRIVER = 1    # Set to 1 if you want to use SGL sound driver, this will copy necessary files into the CD folder
SRL_ENABLE_FREQ_ANALYSIS = 0    # Set to 1 if you want to enable frequency analysis for CD audio, this will load a DSP program into effect slot 1, SGL sound driver must be enabled

# SGL configuration
SGL_MAX_VERTICES = 2500         # Number of vertices that can be used
SGL_MAX_POLYGONS = 1500         # Number of polygons that can be used
SGL_MAX_EVENTS = 1             	# Number of events that can be used
SGL_MAX_WORKS = 1             	# Number of works that can be used

# Disk name. Drives every BuildDrop artifact basename (.elf/.iso/.bin/.cue/.map)
# and the cue's FILE line, so the build output is release-named as-is.
# Safe despite the spaces/parens: shared.mk never uses BUILD_* as a rule target,
# only inside recipes where each reference is already double-quoted.
CD_NAME = Another World (USA)

# Directory build will be placed into
BUILD_DROP = ./BuildDrop

# SRL installation directory
SRL_INSTALL_ROOT ?= ../SaturnRingLib

# Find all .c and .cxx files.
#
# The extensions are not a style choice: shared.mk derives object names with
# $(SOURCES:.c=.o) then :.cxx=.o, and defines pattern rules for only
# `%.o : %.c` and `%.o : %.cxx`. A .cpp file would neither map to an object
# name nor have a rule to build one, so it would be dropped from the link
# without an error. The engine's upstream .cpp files were renamed .cxx when
# they moved into src/ for exactly this reason -- keep new C++ files on .cxx
# and new C files on .c.
SOURCES  = $(patsubst ./%,%,$(shell find src/ -name '*.c'))
SOURCES += $(patsubst ./%,%,$(shell find src/ -name '*.cxx'))

# BYPASS_PROTECTION starts the game at GAME_PART2 (the intro) instead of
# GAME_PART1, which is the original copy-protection wheel screen -- unplayable
# without the physical code wheel. The old host CMake build defined this; the
# engine's default without it (engine.cxx: `uint16_t part = GAME_PART1`) is the
# protection screen, so leaving it undefined looks like a broken boot.
SRL_CUSTOM_CCFLAGS += -DBYPASS_PROTECTION

# src/ is flat today but will be split into per-concern subfolders as the
# Saturn backend grows. Put src/ and every subfolder on the include path so the
# engine's bare "#include \"foo.h\"" keep resolving across folders without
# per-file path edits. SRL_CUSTOM_CCFLAGS is appended to CCFLAGS by shared.mk
# for both C and C++ TUs.
SRL_CUSTOM_CCFLAGS += $(patsubst %,-I%,$(shell find src -type d))

# Include shared makefile
SDK_ROOT = $(SRL_INSTALL_ROOT)/saturnringlib
include $(SDK_ROOT)/shared.mk

# SEGA's PCM stream driver, for gapless audio streaming. Appended AFTER the
# include on purpose: shared.mk assigns LIBS with `=` (line 14) and would
# discard anything set before it, while the link recipe expands LIBS lazily --
# so appending here lands the library after the objects that reference it and
# inside the COFF bracket the other SGL libraries sit in.
#
# The SDK ships LIBPCM.A but does not link it, and nothing in SaturnRingLib
# uses it. See saturn_audio.cxx for why the port needs it: slPCMOn is one-shot
# and cannot be driven gaplessly from frame-polled code.
LIBS += $(SGLLDIR)/LIBPCM.A
