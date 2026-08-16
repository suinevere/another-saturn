/*----------------------
 | saturn_probe.h
 | Description: [DEBUG-a4f2] Throwaway instrumentation for the black seam
 |   between the opening and the introduction cinematic. Delete the whole
 |   module once the seam is understood -- grep DEBUG-a4f2.
 |
 |   Two readouts off one set of marks. Each mark stamps sat_time_ms into a
 |   table the title card prints afterwards, and tints the screen a colour of
 |   its own, so the stage that owns the seam is the colour left on screen
 |   longest. The tint is VDP2 colour offset A, the same register pair
 |   saturn_fade.h drives -- a positive offset lifts the black the seam is
 |   already showing, and the next sat_fade_set overwrites it, so the tint
 |   cleans up after itself.
 |
 |   Off-Saturn the marks compile to nothing, so the shared engine files this
 |   is called from still build for the host tests.
 | Author: suinevere
 | Dependencies: stdint.h
 ----------------------*/
#ifndef SATURN_PROBE_H
#define SATURN_PROBE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------
 | SatProbeTag
 | Description: [DEBUG-a4f2] The milestones through the seam, in the order they
 |   are expected to happen. Each carries a screen tint and a short name the
 |   overlay prints.
 | Author: suinevere
 ----------------------*/
enum SatProbeTag {
	SAT_PROBE_ATTRACT_ENTER = 0,
	SAT_PROBE_INVALIDATED,
	SAT_PROBE_BANK_OPENED,
	SAT_PROBE_BANK_READ,
	SAT_PROBE_BANK_UNPACKED,
	SAT_PROBE_PART_READY,
	SAT_PROBE_FIRST_FRAME,
	SAT_PROBE_TAG_COUNT
};

/*----------------------
 | SAT_PROBE_MAX_MARKS
 | Description: [DEBUG-a4f2] How many marks the table holds. GAME_PART2 loads
 |   three resources and each contributes an opened/read/unpacked triple, so
 |   eleven marks is the whole seam -- the rest is headroom for a part that
 |   loads more.
 | Author: suinevere
 ----------------------*/
#define SAT_PROBE_MAX_MARKS 16

#ifdef __sh__

/*----------------------
 | sat_probe_reset
 | Description: [DEBUG-a4f2] Empties the mark table. Call once at the top of
 |   the seam so a second pass of the attract does not append to the first.
 | Author: suinevere
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void sat_probe_reset(void);

/*----------------------
 | sat_probe_mark
 | Description: [DEBUG-a4f2] Stamps the current time against a tag and tints
 |   the screen that tag's colour. Silently drops the stamp once the table is
 |   full, but still tints -- the colour is the primary signal.
 | Author: suinevere
 | Params: tag -- one of SatProbeTag
 | Returns: N/A
 ----------------------*/
void sat_probe_mark(int tag);

/*----------------------
 | sat_probe_count
 | Description: [DEBUG-a4f2] How many marks the table holds.
 | Author: suinevere
 | Params: N/A
 | Returns: 0 to SAT_PROBE_MAX_MARKS
 ----------------------*/
int sat_probe_count(void);

/*----------------------
 | sat_probe_name
 | Description: [DEBUG-a4f2] The short printable name of a mark.
 | Author: suinevere
 | Params: index -- 0 to sat_probe_count() - 1
 | Returns: a static string, never null
 ----------------------*/
const char *sat_probe_name(int index);

/*----------------------
 | sat_probe_ms
 | Description: [DEBUG-a4f2] Milliseconds from the first mark to this one, so
 |   the table reads as elapsed time rather than as a boot clock.
 | Author: suinevere
 | Params: index -- 0 to sat_probe_count() - 1
 | Returns: elapsed milliseconds, 0 for an out of range index
 ----------------------*/
uint32_t sat_probe_ms(int index);

#else

#define sat_probe_reset()    ((void)0)
#define sat_probe_mark(tag)  ((void)(tag))
#define sat_probe_count()    (0)
#define sat_probe_name(i)    ((void)(i), "")
#define sat_probe_ms(i)      ((void)(i), (uint32_t)0)

#endif /* __sh__ */

#ifdef __cplusplus
}
#endif
#endif /* SATURN_PROBE_H */
