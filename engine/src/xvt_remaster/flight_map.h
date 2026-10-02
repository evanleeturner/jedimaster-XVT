#ifndef XVT_REMASTER_FLIGHT_MAP_H
#define XVT_REMASTER_FLIGHT_MAP_H
#include "xvt_remaster/flight.h"
#ifdef __cplusplus
extern "C" {
#endif
/* The in-flight map: the snapshot's map objects drawn as meshes or icons in two passes split by the grid
 * plane (the far side, the grid, then the near side), with boxes, altitude and movement lines, labels
 * and ranges overlaid, the HUD on top, and the result resolved through the flight pipeline without
 * bloom. Static state: one scene and composite. */

/* Renders the map for snapshot through view into a composite of view's viewport size. Objects go far to
 * near; those on the camera's side of the grid plane (map.grid_z) draw after the grid. A projected
 * object whose kind allows and whose type's max_extent is under a sixteenth of its depth draws as the
 * map icon (remapped for iff 3); otherwise as a mesh selected like the flight's (projectiles
 * roll-aligned and emissive) plus its map effects. A projected object also gets its box corners when
 * visible (sized from box_extent at the focal length, clamped), the order line to the map's endpoint
 * when it is the target, and, when its overlay is visible, the drop line to the grid and the movement
 * line (256 plus the lesser of 32 * speed and 32768, along move_x and move_y); the label and the range text
 * (value / 100 with two decimals) when visible. Lines are clipped at depth 1. Returns 0 when the scene
 * or composite cannot be made or any pass fails. */
int XvtFlightMap_Render(AeronCommandBuffer *cmd,
			const XvtRenderSnapshot *snapshot,
			const XvtRenderView *view);
/* Destroys the scene, the composite, the draw list and the mesh tables. */
void XvtFlightMap_Shutdown(void);
/* Creates the map scene and composite at width x height and the current MSAA when they differ,
 * preparing the scene's resources without flight post. Returns 0 on failure. */
int XvtFlightMap_PrepareResources(int width, int height);
#ifdef __cplusplus
}
#endif
#endif
