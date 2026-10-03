#ifndef XVT_RUNTIME_SNAPSHOT_RENDER_MAP_H
#define XVT_RUNTIME_SNAPSHOT_RENDER_MAP_H
#include "xvt_runtime/snapshot/render_snapshot.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Rebuilds map from the captured objects while the local player's map is open; otherwise
 * leaves it cleared and inactive. Keeps static objects of genus up to platform or mine to
 * satellite, and other objects below g_explosion_object_slot_end of genus up to platform or a
 * projectile, small debris or explosion. Each kept object gets its icon frame (once icons are
 * loaded), movement
 * direction, target-box state and colors, a flight-group label (skipped once the label space is
 * full) and, when the camera focus is a valid slot, its range capped at 9999. The current
 * target also sets map->target and, when it resolves, the order endpoint.
 * count must not exceed XVT_SNAP_OBJECTS and the local player index must be valid; neither is
 * checked. */
void xvt_render_map_capture(struct xvt_snap_map *map,
			    const struct xvt_snap_object *objects,
			    unsigned count);
#ifdef __cplusplus
}
#endif
#endif
