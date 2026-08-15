/*----------------------
 | saturn_system.cxx
 | Description: The Saturn implementation of the engine's abstract System, and
 |   the definition of `stub` -- the single pointer main.cxx links against. This
 |   replaces sysImplementation.cxx (the SDL2 backend, kept under saturn/host/
 |   for reference), which is the whole substitution the port turns on.
 |
 |   It deliberately includes no SRL headers: everything platform-specific goes
 |   through the plain C interface in saturn_platform.h, so the engine's
 |   extern "C"-wrapped SGL headers and SRL's C++ headers never meet here.
 |
 |   Audio goes the same way, through saturn_audio.h: the engine's software mixer
 |   is pulled from on the main loop and its samples pushed to the SCSP.
 | Author: suinevere
 | Dependencies: sys.h, saturn_platform.h, saturn_audio.h
 ----------------------*/
#include "sys.h"
#include "saturn_platform.h"
#include "saturn_audio.h"
#include "saturn_fade.h"

/*----------------------
 | SaturnSystem
 | Description: System on Saturn hardware. Display goes to the NBG0 bitmap layer,
 |   input comes from pad 1, timing from the vblank counter.
 | Author: suinevere
 ----------------------*/
struct SaturnSystem : System {
	virtual ~SaturnSystem() {}

	virtual void init(const char *title);
	virtual void destroy();

	virtual void setPalette(const uint8_t *buf);
	virtual void updateDisplay(const uint8_t *src);

	virtual void processEvents();
	virtual void sleep(uint32_t duration);
	virtual uint32_t getTimeStamp();

	virtual void startAudio(AudioCallback callback, void *param);
	virtual void stopAudio();
	virtual uint32_t getOutputSampleRate();

	virtual int addTimer(uint32_t delay, TimerCallback callback, void *param);
	virtual void removeTimer(int timerId);

	virtual void *createMutex();
	virtual void destroyMutex(void *mutex);
	virtual void lockMutex(void *mutex);
	virtual void unlockMutex(void *mutex);
};

void SaturnSystem::init(const char *title) {
	(void)title; // no window to name
	memset(&input, 0, sizeof(input));
	sat_video_init();
	sat_fade_init();
}

void SaturnSystem::destroy() {
	// Nothing to tear down: there is no OS to return resources to, and main()
	// never actually reaches here on hardware.
}

void SaturnSystem::setPalette(const uint8_t *buf) {
	sat_video_set_palette(buf);
}

void SaturnSystem::updateDisplay(const uint8_t *src) {
	sat_video_present(src);
}

void SaturnSystem::processEvents() {
	const uint32_t pad = sat_input_read();

	input.dirMask = 0;
	if (pad & SAT_PAD_UP)    input.dirMask |= PlayerInput::DIR_UP;
	if (pad & SAT_PAD_DOWN)  input.dirMask |= PlayerInput::DIR_DOWN;
	if (pad & SAT_PAD_LEFT)  input.dirMask |= PlayerInput::DIR_LEFT;
	if (pad & SAT_PAD_RIGHT) input.dirMask |= PlayerInput::DIR_RIGHT;

	input.button = (pad & SAT_PAD_ACTION) != 0;
	input.pause  = (pad & SAT_PAD_PAUSE) != 0;

	input.menuConfirm = (pad & (SAT_PAD_A | SAT_PAD_C)) != 0;
	input.menuCancel  = (pad & SAT_PAD_B) != 0;
	input.menuLeft    = (pad & SAT_PAD_L) != 0;
	input.menuRight   = (pad & SAT_PAD_R) != 0;

	// No keyboard: the code-entry screen and quitting have no pad equivalent
	// yet, and there is nowhere to quit *to* on a console anyway.
	input.quit = false;
	input.code = false;
}

void SaturnSystem::sleep(uint32_t duration) {
	sat_sleep_ms(duration);
}

uint32_t SaturnSystem::getTimeStamp() {
	return sat_time_ms();
}

/*----------------------
 | startAudio
 | Description: The callback argument is ignored: since the SCSP does the
 |   mixing there is nothing to pull samples from. It stays in the signature
 |   because sys.h:67 is the engine's own interface and this is the only
 |   backend that does not need it.
 | Author: suinevere
 ----------------------*/
void SaturnSystem::startAudio(AudioCallback callback, void *param) {
	(void)callback;
	(void)param;
	sat_audio_start(0, 0);
}

void SaturnSystem::stopAudio() {
	sat_audio_stop();
}

/*----------------------
 | getOutputSampleRate
 | Description: The rate the PCM channels actually run at. This must stay
 |   non-zero and must match the hardware: Mixer::playChannel computes
 |   `(freq << 8) / getOutputSampleRate()` (mixer.cxx:62) to derive playback
 |   pitch, so a zero is a divide-by-zero the moment a sound starts, and a value
 |   that disagrees with the hardware detunes everything.
 | Author: suinevere
 ----------------------*/
uint32_t SaturnSystem::getOutputSampleRate() {
	return sat_audio_sample_rate();
}

/*----------------------
 | addTimer / removeTimer
 | Description: The periodic callback SfxPlayer uses to advance music patterns.
 |   Serviced from sat_audio_update, on the main loop.
 | Author: suinevere
 ----------------------*/
int SaturnSystem::addTimer(uint32_t delay, TimerCallback callback, void *param) {
	return sat_timer_add(delay, (SatTimerCallback)callback, param);
}

void SaturnSystem::removeTimer(int timerId) {
	sat_timer_remove(timerId);
}

/*----------------------
 | createMutex / destroyMutex / lockMutex / unlockMutex
 | Description: No-ops, and correct as such rather than merely harmless.
 |
 |   These exist because the SDL backend ran the mixer on its own thread and had
 |   to guard the channel state against the VM. This port has no audio thread
 |   and no mixer running from an interrupt at all any more: Mixer::playChannel
 |   writes SCSP slot registers directly, on the main loop, and the SCSP then
 |   plays on its own without anything here servicing it. There is no second
 |   context to race with. Do not "fix" these into real locks -- there is
 |   nothing to lock, and the reason is structural, not an oversight.
 |
 |   Note this is exactly what would change if playback were ever driven from
 |   the vblank ISR again -- e.g. to reprogram slots more precisely than the
 |   main loop's timing allows: Mixer::playChannel on the main loop would then
 |   race that interrupt, and these would have to become real critical
 |   sections.
 |
 |   createMutex returns a non-NULL token because callers keep and pass it back.
 | Author: suinevere
 ----------------------*/
static int s_mutexToken;

void *SaturnSystem::createMutex() {
	return &s_mutexToken;
}

void SaturnSystem::destroyMutex(void *mutex) {
	(void)mutex;
}

void SaturnSystem::lockMutex(void *mutex) {
	(void)mutex;
}

void SaturnSystem::unlockMutex(void *mutex) {
	(void)mutex;
}

/*----------------------
 | sysImplementation / stub
 | Description: The single System instance and the pointer main.cxx declares
 |   `extern`. Deliberately a plain static object with a trivial constructor: a
 |   global whose constructor allocated would run before SRL's heap exists (see
 |   saturn_compat.cxx) and fault.
 | Author: suinevere
 ----------------------*/
static SaturnSystem sysImplementation;
System *stub = &sysImplementation;
