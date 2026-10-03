#ifndef XVT_RUNTIME_SNAPSHOT_COCKPIT_READOUTS_H
#define XVT_RUNTIME_SNAPSHOT_COCKPIT_READOUTS_H
#include "xvt_runtime/snapshot/cockpit_state.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Cockpit numeric readouts and the target panel. The recovered HUD code records numbers and
 * target state while it draws; CopyState copies them into cockpit state, each shown only where
 * its instrument is visible. */

/* Clears every number, the target (to no object) and the course. */
void XvtCockpitReadouts_Reset(void);
/* Starts a HUD update: the target counts as not updated and the course as hidden until they
 * are recorded again. */
void XvtCockpitReadouts_BeginUpdate(void);
/* Records number id at the live text cursor with the live clip, font and colors; width is the
 * field width and digits the minimum digit count. Values keep 16 bits, except the course score.
 * A 16-bit value of 0xFFFF is drawn in the '@' color with no shadow. Out-of-range ids are
 * ignored. */
void XvtCockpitReadouts_RecordNumber(XvtCockpitNumberId id, unsigned value,
				     unsigned width, unsigned digits);
/* RecordNumber for the readout at HUD element binding (speed, throttle, or the target's
 * systems, range, range fraction, shields or hull), with the element's selector as the field
 * width and the background cleared. Other bindings are ignored. */
void XvtCockpitReadouts_RecordCachedNumber(unsigned binding, unsigned value,
					   unsigned digits);
/* Starts the target panel for the local player's current target, in command mode when cmd is
 * set. A change of target or mode first clears the target, its numbers from TARGET_SYSTEMS
 * through ORDER_SECONDS and its text fields; with cmd set, such a change also resets HUD cache
 * entries 102 and 103 so the command-mode target shield and hull percentages are drawn again. The
 * panel is then marked visible and updated. */
void XvtCockpitReadouts_BeginTarget(int cmd);
/* Hides the target panel unless it shows a cover, and clears the target text fields. */
void XvtCockpitReadouts_HideTarget(void);
/* Shows the target panel as the cover image for HUD binding, with its labels hidden. */
void XvtCockpitReadouts_RecordTargetCover(unsigned binding);
/* Records armament indicator index 0 to 3; other indices are ignored. */
void XvtCockpitReadouts_RecordArmament(unsigned index, unsigned value);
/* Hide the order range or order time numbers and their separator field. */
void XvtCockpitReadouts_ClearOrderRange(void);
void XvtCockpitReadouts_ClearOrderTime(void);
/* Hide, or copy out, the count of launcher 0 to 3; other launchers are ignored. */
void XvtCockpitReadouts_ClearLauncher(unsigned launcher);
void XvtCockpitReadouts_CopyLauncher(struct XvtCockpitNumber *number,
				     unsigned launcher);
/* Shows the proving-grounds course panel at these bounds for this update. */
void XvtCockpitReadouts_RecordCourse(int x, int y, int width, int height);
/* Copies the course, readouts, countermeasures, launcher and laser counts and the target panel
 * into state, replacing state->target. A number stays visible only when it was recorded and
 * state already shows its instrument; course numbers follow the course. The target shows only
 * when updated since BeginUpdate and instruments are visible. Its values need the panel
 * uncovered; range and systems show only outside command mode, order range and time only in it. Reads the
 * visibility flags XvtCockpitInstruments_Build sets, so it runs after Build. */
void XvtCockpitReadouts_CopyState(struct XvtCockpitState *state);
#ifdef __cplusplus
}
#endif
#endif
