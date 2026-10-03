#ifndef XVT_RUNTIME_SNAPSHOT_RENDER_SNAPSHOT_H
#define XVT_RUNTIME_SNAPSHOT_RENDER_SNAPSHOT_H

#include "xvt_runtime/snapshot/cockpit_state.h"
#include "xvt_runtime/snapshot/render_types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct XvtSnapMapObject {
	uint16_t object_index, icon_frame;
	uint16_t icon_width, icon_height;
	uint32_t label_offset;
	uint32_t label_color_argb;
	uint32_t line_color_argb;
	int32_t box_extent;
	int16_t move_x, move_y;
	uint16_t range_value;
	uint8_t render_kind, cull_kind, effective_iff;
	uint8_t label_visible, movement_visible, box_visible, box_color;
	uint8_t overlay_visible, range_visible;
};

enum { XVT_MAP_MODEL_OR_ICON, XVT_MAP_EFFECT };

struct XvtSnapMap {
	uint8_t active, endpoint_valid;
	struct XvtSnapObjectId target;
	int32_t grid_z, order_endpoint[3];
	uint64_t icon_asset_id;
	uint64_t font_asset_id;
	uint32_t object_count, label_bytes;
	struct XvtSnapMapObject objects[XVT_SNAP_OBJECTS];
	char labels[XVT_SNAP_LABEL_BYTES];
};

struct XvtSnapSky {
	uint16_t star_grid_divisor;
	uint16_t checkpoint_slot, craft_slot_end;
	uint8_t backdrop_enabled, debris_enabled, proving_grounds;
	uint8_t backdrop_types[64], backdrop_directions[64];
	uint16_t direction_counts[6];
};

struct XvtSnapStreak {
	int32_t offset[3], half_width;
	uint16_t roll;
};

struct XvtSnapHyperspace {
	uint8_t valid, phase, iff;
	uint32_t elapsed_ticks, count;
	struct XvtSnapStreak streaks[1024];
};

struct XvtSnapCursor {
	uint64_t asset_id;
	int32_t x, y;
	uint16_t width, height;
	uint8_t visible;
};

struct XvtRenderSnapshot {
	uint64_t snapshot_serial, flight_frame_serial, capture_host_us;
	uint64_t component_event_serial;
	int32_t component_event_time;
	uint8_t flight_unlocked;
	uint64_t presentation_serial, frontend_generation;
	uint32_t presented_scene;
	uint16_t presented_target;
	uint8_t frontend_surfaces_released;
	uint64_t mission_generation, world_generation;
	int32_t game_time_ticks, view_time_ticks;
	uint32_t scene_kind, dropped_records;
	uint8_t flight_valid, focused, paused, text_entry_active;
	struct XvtSnapCamera camera;
	struct XvtSnapLighting lighting;
	struct XvtCockpitState cockpit;
	struct XvtCockpitResources cockpit_resources;
	struct XvtSnapMap map;
	struct XvtSnapSky sky;
	struct XvtSnapHyperspace hyperspace;
	struct XvtSnapCursor cursor;
	uint32_t flight_palette_argb[256];
	int16_t fuselage_sequence[25];
	struct XvtSnapType types[201];
	struct XvtSnapObject objects[XVT_SNAP_OBJECTS];
	uint32_t object_count;
	struct XvtSnapTargetBox target_boxes[XVT_SNAP_TARGET_BOXES];
	uint32_t target_box_count;
	/* Ordered frontend/movie composition; flight HUD content is in cockpit. */
	struct XvtSnapSprite sprites[XVT_SNAP_SPRITES];
	uint32_t sprite_count;
	struct XvtSnapGlyph glyphs[XVT_SNAP_GLYPHS];
	uint32_t glyph_count;
	struct XvtSnapPaint paint[XVT_SNAP_PAINTS];
	uint32_t paint_count;
	struct XvtSnapCopyRect copies[XVT_SNAP_COPIES];
	uint32_t copy_count;
	struct XvtSnapSurfaceEvent surface_events[XVT_SNAP_SURFACE_EVENTS];
	uint32_t surface_event_count;
	struct XvtSnapPreview previews[XVT_SNAP_PREVIEWS];
	uint32_t preview_count;
	uint64_t opt_asset_generation, texture_asset_generation;
	uint64_t image_asset_generation;
	struct XvtSnapOptAsset opt_assets[XVT_SNAP_ASSETS];
	uint32_t opt_asset_count;
	struct XvtSnapTextureAsset texture_assets[XVT_SNAP_TYPES];
	uint32_t texture_asset_count;
	struct XvtSnapImageAsset image_assets[XVT_SNAP_ASSETS];
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
void XvtRenderSnapshot_Init(void);
void XvtRenderSnapshot_Shutdown(void);
/* BeginFrame is idempotent while a frame is open (including paused-frame routing). */
/* It opens a frame on the writer slot: begins the asset and capture frames, stamps the snapshot serial
 * and scene kind, and zeroes the record counts and the flight, camera, map, hyperspace and
 * cursor flags. Other fields keep what the slot last held. */
void XvtRenderSnapshot_BeginFrame(void);
/* Sets the scene kind for this and later frames; it persists until changed. */
void XvtRenderSnapshot_SetSceneKind(XvtSceneKind kind);
/* Closes the open frame; does nothing when none is open. Stamps game time, host time, focus
 * and pause, runs the capture, frontend and asset exports into the writer, then publishes it
 * and advances the snapshot serial. */
void XvtRenderSnapshot_Commit(int32_t game_time_ticks, int focused, int paused);
/* Views stay valid until the next commit; NULL before their first publication. */
const struct XvtRenderSnapshot *XvtRenderSnapshot_Current(void);
const struct XvtRenderSnapshot *XvtRenderSnapshot_Previous(void);

#ifdef __cplusplus
}
#endif

#endif
