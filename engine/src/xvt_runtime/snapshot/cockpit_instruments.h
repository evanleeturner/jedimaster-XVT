#ifndef XVT_RUNTIME_SNAPSHOT_COCKPIT_INSTRUMENTS_H
#define XVT_RUNTIME_SNAPSHOT_COCKPIT_INSTRUMENTS_H

#include "xvt_runtime/snapshot/cockpit_state.h"

#ifdef __cplusplus
extern "C" {
#endif
/* Cockpit instruments for the local player. The recovered HUD code records laser locks,
 * threat levels and radar blips while it draws; Build combines them with the player's craft
 * into the systems, weapons, radar and readout parts of cockpit state. */

/* For the local player only: forgets the recorded locks, threats and radar and begins a
 * readouts update. Any other player is ignored. */
void XvtCockpitInstruments_BeginUpdate(int player);
/* Records a slot's laser lock state; slots from XVT_HUD_WEAPON_SLOTS up are ignored. */
void XvtCockpitInstruments_RecordLaserLock(unsigned slot, unsigned state);
/* Records the attack, laser, beam and warhead threat levels. */
void XvtCockpitInstruments_RecordThreats(unsigned attack, unsigned laser, unsigned beam, unsigned warhead);
/* Records blip index (0 to 47) on the fore or aft scope, relative to that scope's anchor; a
 * blip on the local player's current target also sets the target marker. The scope's count
 * becomes index + 1, except that index 47 leaves it at 47, so a 48th blip is stored but not
 * counted. Other indices are ignored; object is not range-checked. */
void XvtCockpitInstruments_RecordRadar(int object, int front, int index, int x, int y, int color);
/* Sets each counted blip's coverage from the drawn radar points: the low two color bits at one
 * byte per pixel, otherwise whether the point was drawn. */
void XvtCockpitInstruments_CompleteRadar(void);
/* Clears state's systems, weapons, target, radar and readouts, then fills systems, weapons,
 * radar and readouts from the local player's craft and the recorded values; they stay cleared
 * when the player has no craft in a main slot. The system masks and, in the forward or HUD-only
 * view, the feature covers are built whether or not instruments are visible; the rest only
 * while they are, and all but the warnings only in those views with the map closed. Also sets
 * view's rebel_fighter and laser_slots. Reads state->view, which must be filled first. */
void XvtCockpitInstruments_Build(XvtCockpitState* state);
#ifdef __cplusplus
}
#endif
#endif
