#ifndef XVT_REMASTER_HUD_ASSETS_H
#define XVT_REMASTER_HUD_ASSETS_H

#include "aeron/scene/runtime_atlas.h"
#include "xvt_remaster/font.h"
#include "xvt_remaster/hud_layout.h"
#include "xvt_runtime/snapshot/render_snapshot.h"
/* Resident cockpit artwork per resource generation: for each enabled view, its
 * base panel (shared between views of the same asset) and the parts its
 * compiled layout requests, colorized through the view's palette, undithered
 * when the setting asks, and packed into atlases. A group is prepared pending,
 * committed after its upload is submitted, selected by matching a state and
 * layout, and retired when its source images leave the snapshot. */

/* atlas_frame: the part's frame in the parts atlas. monochrome, color: how
 * xvt_hud_draw_part tints it. */
struct xvt_hud_prepared_part {
	uint16_t atlas_frame;
	uint8_t monochrome;
	uint8_t color;
};

/* base, parts: the atlases. base_coverage: the base's covered rectangles. base_asset_id, palette,
 * requests: the identity Select matches against a state and layout. */
struct xvt_hud_asset_set {
	AeronRuntimeAtlas base;
	AeronRuntimeAtlas parts;
	AeronImageCoverage base_coverage;
	uint64_t base_asset_id;
	uint32_t palette[256];
	uint16_t part_count;
	struct xvt_hud_part_request requests[XVT_HUD_PART_CAPACITY];
	struct xvt_hud_prepared_part bindings[XVT_HUD_PART_CAPACITY];
};

/* Prepare the loaded view family during loading, after source synchronization.
 * Commit only after upload submission succeeds. Select never uploads or decodes.
 * Invalidate prepared draw lists before retiring their source-dependent groups. */
/* Builds the pending group for resources' generation. Returns 1 doing nothing
 * for invalid resources or a generation already committed; 0 while a pending
 * group exists, on an allocation failure, when a view fails to compile, a base
 * or part cannot be decoded or built, or the group's part capacity is exceeded.
 * Every enabled view with a base asset, and the full-screen view, gets a set;
 * equal requests share one atlas frame. A part is colorized by its request (its
 * palette color, white for monochrome, or the fade-shifted index), and, when
 * cockpit_undither is on, undithered unless monochrome; the base likewise. */
int xvt_hud_assets_prepare_resources(
	AeronCommandBuffer *cmd, const struct xvt_cockpit_resources *resources);
/* Whether a committed group holds generation. */
int xvt_hud_assets_has_resources(uint64_t generation);
/* Makes current the committed set whose base asset, part requests and palette
 * match state and layout. Selects the empty set, returning 1, when the layout
 * has neither base nor parts, the definition is invalid, or the instruments are
 * hidden by a loading screen or alert and there are no parts. Returns 0,
 * logging an error, when no group matches. */
int xvt_hud_assets_select(const struct xvt_cockpit_state *state,
			  const struct xvt_hud_layout *layout);
/* Releases every committed group whose base or part source images are gone from the snapshot. */
void xvt_hud_assets_retire(const struct xvt_render_snapshot *snapshot);
/* Commits the pending group, newest first. */
void xvt_hud_assets_commit(void);
/* Releases the pending group. */
void xvt_hud_assets_abort(void);
/* Releases everything. */
void xvt_hud_assets_shutdown(void);
/* The set the last Select made current, or NULL, also once its group was released. */
const struct xvt_hud_asset_set *xvt_hud_assets_current(void);
/* True when loaded cockpit artwork differs from the requested preparation option. */
/* 0 without a current snapshot with valid cockpit resources, or without a committed group for its
 * resource generation. */
int xvt_hud_assets_undither_pending(int requested);
/* Fonts retain the existing source-generation ownership in the image cache. */
/* xvt_remaster_assets_font(asset_id, 0). */
const struct xvt_font_atlas *xvt_hud_assets_find_font(uint64_t asset_id);

#endif
