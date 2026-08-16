/*----------------------
 | saturn_probe.h
 | Description: [DEBUG-a4f2] Throwaway instrumentation for the black seam
 |   between the opening and the introduction cinematic. Delete the whole
 |   module once the seam is understood -- grep DEBUG-a4f2.
 |
 |   Totals rather than a list of marks. The first pass listed every mark and
 |   ran off the end of its table four seconds into a fifteen second seam,
 |   because the introduction goes on loading long after setupPart returns. A
 |   mark now adds the time since the previous one to its stage's running
 |   total, so the table is a fixed seven rows however many resources the part
 |   pulls.
 |
 |   Two readouts off the same marks. Each mark tints the screen its stage's
 |   colour -- VDP2 colour offset A, the register pair saturn_fade.h drives, a
 |   positive offset lifting the black the seam already shows -- and the title
 |   card prints the totals afterwards.
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
 | Description: [DEBUG-a4f2] The stages of the seam. A mark closes the stage it
 |   names: SAT_PROBE_BANK_OPENED means the time since the previous mark was
 |   spent inside f.open, and so on. SAT_PROBE_ATTRACT_ENTER opens the
 |   measurement and closes nothing.
 |
 |   SAT_PROBE_CACHE_FILL is marked from inside sat_cd_open and only when a
 |   whole-file prefetch actually went to the disc, so its hit count is the
 |   number of bank re-reads the one-entry name cache did not absorb. What is
 |   left against SAT_PROBE_BANK_OPENED after it is open overhead that is not
 |   the prefetch.
 |
 |   SAT_PROBE_DISC_READ is marked per window from sat_cd_read's uncached path,
 |   which only runs when a file has no whole-file cache behind it. Zero hits
 |   means every read was served from memory, and whatever SAT_PROBE_BANK_READ
 |   still costs is the copy itself rather than the disc.
 | Author: suinevere
 ----------------------*/
enum SatProbeTag {
	SAT_PROBE_ATTRACT_ENTER = 0,
	SAT_PROBE_INVALIDATED,
	SAT_PROBE_CACHE_FILL,
	SAT_PROBE_DISC_READ,
	SAT_PROBE_BANK_OPENED,
	SAT_PROBE_BANK_READ,
	SAT_PROBE_BANK_UNPACKED,
	SAT_PROBE_PART_READY,
	SAT_PROBE_VM_FRAME,
	SAT_PROBE_TAG_COUNT
};

#ifdef __sh__

/*----------------------
 | sat_probe_reset
 | Description: [DEBUG-a4f2] Zeroes every total and starts the clock. Call once
 |   at the top of the seam.
 | Author: suinevere
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void sat_probe_reset(void);

/*----------------------
 | sat_probe_mark
 | Description: [DEBUG-a4f2] Charges the time since the previous mark to a
 |   stage, counts the visit, and tints the screen that stage's colour. Does
 |   nothing once sat_probe_stop has run, so the totals describe the seam and
 |   not the cinematic that follows it.
 | Author: suinevere
 | Params: tag -- one of SatProbeTag
 | Returns: N/A
 ----------------------*/
void sat_probe_mark(int tag);

/*----------------------
 | sat_probe_stop
 | Description: [DEBUG-a4f2] Freezes the totals. Called when the introduction's
 |   fade-in completes, which is the moment the seam is over from where the
 |   player is sitting.
 | Author: suinevere
 | Params: N/A
 | Returns: N/A
 ----------------------*/
void sat_probe_stop(void);

/*----------------------
 | sat_probe_name
 | Description: [DEBUG-a4f2] The short printable name of a stage.
 | Author: suinevere
 | Params: tag -- one of SatProbeTag
 | Returns: a static string, never null
 ----------------------*/
const char *sat_probe_name(int tag);

/*----------------------
 | sat_probe_hits
 | Description: [DEBUG-a4f2] How many times a stage was entered.
 | Author: suinevere
 | Params: tag -- one of SatProbeTag
 | Returns: the visit count, 0 for an out of range tag
 ----------------------*/
uint32_t sat_probe_hits(int tag);

/*----------------------
 | sat_probe_total
 | Description: [DEBUG-a4f2] Milliseconds charged to a stage across the seam.
 | Author: suinevere
 | Params: tag -- one of SatProbeTag
 | Returns: the total, 0 for an out of range tag
 ----------------------*/
uint32_t sat_probe_total(int tag);

/*----------------------
 | sat_probe_elapsed
 | Description: [DEBUG-a4f2] The whole seam, first mark to last, so the rows
 |   can be checked against something that does not depend on them summing.
 | Author: suinevere
 | Params: N/A
 | Returns: milliseconds
 ----------------------*/
uint32_t sat_probe_elapsed(void);

#else

#define sat_probe_reset()    ((void)0)
#define sat_probe_mark(tag)  ((void)(tag))
#define sat_probe_stop()     ((void)0)
#define sat_probe_name(tag)  ((void)(tag), "")
#define sat_probe_hits(tag)  ((void)(tag), (uint32_t)0)
#define sat_probe_total(tag) ((void)(tag), (uint32_t)0)
#define sat_probe_elapsed()  ((uint32_t)0)

#endif /* __sh__ */

#ifdef __cplusplus
}
#endif
#endif /* SATURN_PROBE_H */
