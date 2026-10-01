#ifndef XVT_REMASTER_COMPONENT_ANIMATION_H
#define XVT_REMASTER_COMPONENT_ANIMATION_H
#include "xvt_remaster/assets.h"
#include "xvt_runtime/snapshot/render_snapshot.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Smooths the component rotation angles of craft (S-foils, turning parts) between snapshots so an
 * unlocked flight draws between simulation steps: each component eases from its last target byte to the
 * new one, the shorter way around, over the 32 ticks after the component event that set it. One pose per
 * object slot, keyed by the object's signature, type and model; everything resets when the mission or
 * world changes. */

/* Forgets every pose and the frame last prepared. */
void XvtComponentAnimation_Reset(void);
/* Advances the poses to snapshot. Resets everything for a snapshot without a valid, unlocked flight, or
 * first when the mission or world generation changed; after that, a repeated flight_frame_serial
 * changes nothing. Otherwise, per craft object: its slot's pose restarts when stale (not prepared last
 * frame, another
 * signature, type or model, or an event serial that skipped); each component's angle is the eased value
 * between the last target and the new mesh_rotation byte, alpha = (view_time_ticks -
 * component_event_time) / 32 clamped to 0..1, except on a discontinuity (a restart, a changed
 * component_hp or component_state, or a target that changed with no new event), which snaps to the new
 * byte. An object whose angles moved gains a revision; Changed() reports whether any did. */
void XvtComponentAnimation_Prepare(const XvtRenderSnapshot* snapshot);
/* Whether the last Prepare of a new frame moved any angle. */
int XvtComponentAnimation_Changed(void);
/* The count of prepared frames in which slot's angles moved; 0 for a slot out of range or never posed. */
uint64_t XvtComponentAnimation_ObjectRevision(unsigned slot);
/* Fills output with object's component angles for drawing: its mesh_rotation bytes, with the smoothed
 * angle substituted for each live component (below asset's component count, state 0, hit points nonzero)
 * whose mesh type turns (11, 12, 13, 21, 23, 24 or 25) or is a foil of a craft with S-foils open (type
 * 20 on object types 1 and 4, type 7 on type 4, when sfoil_state bit 0 is set). The angles are the
 * current ones when snapshot is the frame last prepared, else the previous ones. Returns output, or
 * NULL with output untouched for a NULL argument, a locked flight, a slot out of range, or a slot whose
 * pose belongs to another object. */
const float* XvtComponentAnimation_Angles(const XvtRenderSnapshot* snapshot, const XvtSnapObject* object,
										  const XvtMeshAsset* asset, float output[XVT_SNAP_COMPONENTS]);
#ifdef __cplusplus
}
#endif

#endif
