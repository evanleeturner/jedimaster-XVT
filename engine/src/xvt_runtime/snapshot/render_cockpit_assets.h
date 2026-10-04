#ifndef XVT_RUNTIME_SNAPSHOT_RENDER_COCKPIT_ASSETS_H
#define XVT_RUNTIME_SNAPSHOT_RENDER_COCKPIT_ASSETS_H
#include "xvt_runtime/snapshot/render_snapshot.h"

/* Internal asset-observer state; recovered callers use render_assets.h. */
/* Clears the cockpit layout, leaving it invalid until xvt_render_assets_capture_cockpit, and every
 * panel, icon and LFD binding; raises the layout and resource generations. */
void xvt_render_cockpit_reset(void);
/* Drops every panel, icon and LFD binding to id, and the layout's panel asset if it is id.
 * Raises the resource generation when a panel or LFD binding was dropped. */
void xvt_render_cockpit_forget(uint64_t id);
/* Fills definition from the cockpit layout, panel bindings, the flight fonts
 * for the current resolution, and the beam and shield tables; a valid layout
 * first takes the live HUD element values. The copy has layout generation 0 and
 * element 127's warning timer zeroed, so equal content compares equal. */
void xvt_render_cockpit_capture_definition(
	struct xvt_cockpit_definition *definition);
/* xvt_render_assets_register_image for an LFD cockpit source; a match also needs the same
 * viewport. */
uint64_t
xvt_render_assets_register_cockpit(const void *owner, uint16_t handle,
				   const char *path,
				   const struct xvt_snap_rect *viewport);
#endif
