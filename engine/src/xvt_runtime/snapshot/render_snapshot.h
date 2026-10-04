#ifndef XVT_RUNTIME_SNAPSHOT_RENDER_SNAPSHOT_H
#define XVT_RUNTIME_SNAPSHOT_RENDER_SNAPSHOT_H

#include "xvt_runtime/snapshot/cockpit_state.h"
#include "xvt_runtime/snapshot/render_types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct xvt_snap_map_object {
	uint16_t object_index;
	uint16_t icon_frame;
	uint16_t icon_width;
	uint16_t icon_height;
	uint32_t label_offset;
	uint32_t label_color_argb;
	uint32_t line_color_argb;
	int32_t box_extent;
	int16_t move_x;
	int16_t move_y;
	uint16_t range_value;
	uint8_t render_kind;
	uint8_t cull_kind;
	uint8_t effective_iff;
	uint8_t label_visible;
	uint8_t movement_visible;
	uint8_t box_visible;
	uint8_t box_color;
	uint8_t overlay_visible;
	uint8_t range_visible;
};

enum { XVT_MAP_MODEL_OR_ICON, XVT_MAP_EFFECT };

struct xvt_snap_map {
	uint8_t active;
	uint8_t endpoint_valid;
	struct xvt_snap_object_id target;
	int32_t grid_z;
	int32_t order_endpoint[3];
	uint64_t icon_asset_id;
	uint64_t font_asset_id;
	uint32_t object_count;
	uint32_t label_bytes;
	struct xvt_snap_map_object objects[XVT_SNAP_OBJECTS];
	char labels[XVT_SNAP_LABEL_BYTES];
};

struct xvt_snap_sky {
	uint16_t star_grid_divisor;
	uint16_t checkpoint_slot;
	uint16_t craft_slot_end;
	uint8_t backdrop_enabled;
	uint8_t debris_enabled;
	uint8_t proving_grounds;
	uint8_t backdrop_types[64];
	uint8_t backdrop_directions[64];
	uint16_t direction_counts[6];
};

struct xvt_snap_streak {
	int32_t offset[3];
	int32_t half_width;
	uint16_t roll;
};

struct xvt_snap_hyperspace {
	uint8_t valid;
	uint8_t phase;
	uint8_t iff;
	uint32_t elapsed_ticks;
	uint32_t count;
	struct xvt_snap_streak streaks[1024];
};

struct xvt_snap_cursor {
	uint64_t asset_id;
	int32_t x;
	int32_t y;
	uint16_t width;
	uint16_t height;
	uint8_t visible;
};

struct xvt_render_snapshot {
	uint64_t snapshot_serial;
	uint64_t flight_frame_serial;
	uint64_t capture_host_us;
	uint64_t component_event_serial;
	int32_t component_event_time;
	uint8_t flight_unlocked;
	uint64_t presentation_serial;
	uint64_t frontend_generation;
	uint32_t presented_scene;
	uint16_t presented_target;
	uint8_t frontend_surfaces_released;
	uint64_t mission_generation;
	uint64_t world_generation;
	int32_t game_time_ticks;
	int32_t view_time_ticks;
	uint32_t scene_kind;
	uint32_t dropped_records;
	uint8_t flight_valid;
	uint8_t focused;
	uint8_t paused;
	uint8_t text_entry_active;
	struct xvt_snap_camera camera;
	struct xvt_snap_lighting lighting;
	struct xvt_cockpit_state cockpit;
	struct xvt_cockpit_resources cockpit_resources;
	struct xvt_snap_map map;
	struct xvt_snap_sky sky;
	struct xvt_snap_hyperspace hyperspace;
	struct xvt_snap_cursor cursor;
	uint32_t flight_palette_argb[256];
	int16_t fuselage_sequence[25];
	struct xvt_snap_type types[201];
	struct xvt_snap_object objects[XVT_SNAP_OBJECTS];
	uint32_t object_count;
	struct xvt_snap_target_box target_boxes[XVT_SNAP_TARGET_BOXES];
	uint32_t target_box_count;
	/* Ordered frontend/movie composition; flight HUD content is in cockpit. */
	struct xvt_snap_sprite sprites[XVT_SNAP_SPRITES];
	uint32_t sprite_count;
	struct xvt_snap_glyph glyphs[XVT_SNAP_GLYPHS];
	uint32_t glyph_count;
	struct xvt_snap_paint paint[XVT_SNAP_PAINTS];
	uint32_t paint_count;
	struct xvt_snap_copy_rect copies[XVT_SNAP_COPIES];
	uint32_t copy_count;
	struct xvt_snap_surface_event surface_events[XVT_SNAP_SURFACE_EVENTS];
	uint32_t surface_event_count;
	struct xvt_snap_preview previews[XVT_SNAP_PREVIEWS];
	uint32_t preview_count;
	uint64_t opt_asset_generation;
	uint64_t texture_asset_generation;
	uint64_t image_asset_generation;
	struct xvt_snap_opt_asset opt_assets[XVT_SNAP_ASSETS];
	uint32_t opt_asset_count;
	struct xvt_snap_texture_asset texture_assets[XVT_SNAP_TYPES];
	uint32_t texture_asset_count;
	struct xvt_snap_image_asset image_assets[XVT_SNAP_ASSETS];
	uint32_t image_asset_count;
};

/* Three snapshot slots rotate: the writer filled during a host frame, the current (last committed)
 * and the previous (committed before it). A commit publishes the writer as current and picks
 * as the next writer the slot that is neither current nor previous.
 * BeginFrame, SetSceneKind and Commit do nothing before Init or after Shutdown. */

/* The application initializes before port startup and shuts down after the
 * port and remaster. All access is confined to the host thread. */
/* Init clears all slots and initializes render assets, capture and frontend; a second call
 * before Shutdown does nothing. Shutdown shuts down render assets, resets capture and the scene
 * kind, and makes Current and Previous return NULL; slot contents are not cleared. */
void xvt_render_snapshot_init(void);
void xvt_render_snapshot_shutdown(void);
/* BeginFrame is idempotent while a frame is open (including paused-frame routing). */
/* It opens a frame on the writer slot: begins the asset and capture frames,
 * stamps the snapshot serial and scene kind, and zeroes the record counts and
 * the flight, camera, map, hyperspace and cursor flags. Other fields keep what
 * the slot last held. */
void xvt_render_snapshot_begin_frame(void);
/* Sets the scene kind for this and later frames; it persists until changed. */
void xvt_render_snapshot_set_scene_kind(xvt_scene_kind kind);
/* Closes the open frame; does nothing when none is open. Stamps game time, host time, focus
 * and pause, runs the capture, frontend and asset exports into the writer, then publishes it
 * and advances the snapshot serial. */
void xvt_render_snapshot_commit(int32_t game_time_ticks, int focused,
				int paused);
/* Views stay valid until the next commit; NULL before their first publication. */
const struct xvt_render_snapshot *xvt_render_snapshot_current(void);
const struct xvt_render_snapshot *xvt_render_snapshot_previous(void);

#ifdef __cplusplus
}
#endif

#endif
