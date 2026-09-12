#ifndef XVT_RUNTIME_SNAPSHOT_RENDER_TYPES_H
#define XVT_RUNTIME_SNAPSHOT_RENDER_TYPES_H

#include "xvt/assets/object_genus.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Port-owned observation records. All arrays and strings belong to their slot. */
#define XVT_SNAP_OBJECTS 1664
#define XVT_SNAP_COMPONENTS 50
#define XVT_SNAP_TYPES 201
#define XVT_SNAP_MAP_ICON_FRAMES 2010
#define XVT_SNAP_SEQUENCE 32
#define XVT_SNAP_INSTRUMENTS 432
#define XVT_SNAP_STREAKS 1024
#define XVT_SNAP_BACKDROPS 64
#define XVT_SNAP_SPRITES 4096
#define XVT_SNAP_PAINT 24576
#define XVT_SNAP_GLYPHS 8192
#define XVT_SNAP_PREVIEWS 4
#define XVT_SNAP_SURFACE_EVENTS 64
#define XVT_SNAP_COPIES 256
#define XVT_SNAP_TARGET_BOXES 512
#define XVT_SNAP_ASSETS 1024
#define XVT_SNAP_LABEL_BYTES 65535
#define XVT_SNAP_PATH 1024

/* Runtime object-type IDs, distinct from species and model-definition indices. */
enum {
	XVT_SNAP_TYPE_B_WING = 4,
	XVT_SNAP_TYPE_COMPONENT = 89,
	XVT_SNAP_TYPE_EXPLOSION_FIRST = 127,
	XVT_SNAP_TYPE_EXPLOSION_LAST = 130,
	XVT_SNAP_TYPE_SMALL_EXPLOSION = 131,
	XVT_SNAP_TYPE_COMPONENT_FOLLOWUP = 132
};

enum { XVT_SNAP_HYPERSPACE_NONE, XVT_SNAP_HYPERSPACE_STARTING, XVT_SNAP_HYPERSPACE_TRANSITION };

enum { XVT_SNAP_TEXTURE_FRAME_BIT = 0x8000 };

enum { XVT_SNAP_INVALID_TEXTURE_FRAME = 0xff00, XVT_SNAP_HYPERSPACE_STREAK_END = 0x213 };

enum { XVT_SNAP_MESH_HULL = 1, XVT_SNAP_MESH_FUSELAGE = 3 };

typedef enum XvtSceneKind {
	XVT_SCENE_NONE = 0,
	XVT_SCENE_FRONTEND,
	XVT_SCENE_LOADING,
	XVT_SCENE_FLIGHT,
	XVT_SCENE_FRONTEND_MODAL,
	XVT_SCENE_MOVIE
} XvtSceneKind;

typedef struct XvtSnapRect {
	int32_t x, y, width, height;
} XvtSnapRect;

typedef struct XvtSnapObjectId {
	uint16_t slot, signature;
} XvtSnapObjectId;

typedef struct XvtSnapCamera {
	int32_t world_pos[3];
	float rows[9]; /* Precise render camera, with Q15 fallback for other camera writers. */
	XvtSnapRect viewport;
	int32_t center_x, center_y, projection_offset_y;
	uint16_t screen_width, screen_height, aspect_y_q16;
	uint8_t perspective_shift, valid;
	XvtSnapObjectId player, focus;
	uint16_t view_pitch, view_yaw, view_roll, view_angle_d;
	int16_t hud_aim_x, hud_aim_y;
	uint16_t external, replay_view;
	uint8_t map_mode, hyperspace_phase, hud_state;
} XvtSnapCamera;

typedef struct XvtSnapObject {
	XvtSnapObjectId id;
	uint8_t object_type, genus, flight_group, slot_class;
	int32_t world_pos[3], prev_world_pos[3], player_owner;
	uint16_t yaw, pitch, roll, type_specific_word;
	uint8_t type_specific[2];
	uint8_t has_mobile, has_craft, orient_dirty, move_dirty;
	uint8_t state, iff, team, node_switch, source_type, light_scale;
	uint16_t source_slot, speed;
	int16_t cached_rows_q15[9], move_q15[3];
	uint8_t component_state[50], mesh_rotation[50], component_hp[50];
	uint8_t sfoil_state;
	uint8_t object_kind;
	uint16_t working_subsystems, installed_subsystems;
	uint16_t throttle, engine_output;
	int16_t max_speed;
	uint8_t laser_redirect, shield_redirect, beam_level;
} XvtSnapObject;

enum { XVT_SLOT_MAIN, XVT_SLOT_LOCAL_TRANSIENT, XVT_SLOT_STATIC };

typedef struct XvtSnapType {
	uint64_t model_asset_id, texture_asset_id;
	int32_t max_extent, half_extent;
	uint8_t record_flags, asset_flags, flags, model_index;
	int8_t family;
	uint8_t genus, texture_group, resource_entry;
	uint8_t sequence_count, remap_count;
	int16_t sequence[32];
	uint8_t remap[16];
} XvtSnapType;

typedef struct XvtSnapLighting {
	int32_t direction_q15[3];
	int32_t local_lights_level, directional_enabled;
} XvtSnapLighting;

typedef struct XvtSnapOptAsset {
	uint64_t id;
	uint16_t public_handle;
	char path[XVT_SNAP_PATH];
} XvtSnapOptAsset;

typedef struct XvtSnapTextureAsset {
	uint64_t id;
	uint16_t public_handle, model_type;
	char path[XVT_SNAP_PATH];
} XvtSnapTextureAsset;

typedef enum XvtSnapImageKind {
	XVT_IMAGE_BMP = 1,
	XVT_IMAGE_LFD,
	XVT_IMAGE_PNL,
	XVT_IMAGE_ICO,
	XVT_IMAGE_ABP,
	XVT_IMAGE_MICRO_FNT,
	XVT_IMAGE_BUILTIN_CURSOR
} XvtSnapImageKind;

typedef struct XvtSnapImageAsset {
	uint64_t id;
	uint32_t kind;
	char path[XVT_SNAP_PATH];
	uint32_t first_record, record_count;
	uint16_t font_point_size;
	uint8_t font_row_bytes, make_palette;
	/* Captured with an LFD load; remains valid after cockpit layout replacement. */
	XvtSnapRect cockpit_viewport;
} XvtSnapImageAsset;

enum { XVT_TARGET_FRONT_BACK, XVT_TARGET_FRONT_OFFSCREEN, XVT_TARGET_FRONT_BACKUP, XVT_TARGET_FLIGHT_MAIN };

enum {
	XVT_TARGET_FRONT_PRESENTED = 6,
	XVT_TARGET_FRONT_MOVIE,
	XVT_TARGET_FRONT_CURSOR,
	XVT_TARGET_FRONT_SAVED_FIRST = 48,
	XVT_TARGET_FRONT_SAVED_COUNT = 10
};

enum { XVT_SCOPE_WORLD = 255 };

enum { XVT_PAINT_FILL, XVT_PAINT_LINE, XVT_PAINT_FRAME, XVT_PAINT_TRANSLUCENT };

enum {
	XVT_SPRITE_FRONT_KEYED,
	XVT_SPRITE_FRONT_OPAQUE,
	XVT_SPRITE_FRONT_TINTED,
	XVT_SPRITE_FRONT_TRANSLUCENT
};

enum { XVT_SURFACE_CLEAR, XVT_SURFACE_PRESENT, XVT_SURFACE_RESET };

enum { XVT_SCOPE_FRONTEND, XVT_SCOPE_COCKPIT, XVT_SCOPE_MAP, XVT_SCOPE_CRT };

typedef struct XvtSnapDrawHeader {
	uint32_t z_order;
	uint16_t target, scope;
	XvtSnapRect clip;
} XvtSnapDrawHeader;

typedef struct XvtSnapSprite {
	XvtSnapDrawHeader draw;
	uint64_t asset_id;
	uint32_t frame;
	XvtSnapRect source, destination;
	uint32_t kind, tint_color;
} XvtSnapSprite;

typedef struct XvtSnapGlyph {
	XvtSnapDrawHeader draw;
	uint64_t font_asset_id;
	int32_t x, y;
	uint32_t foreground_argb, background_argb, shadow_argb;
	uint16_t character, advance;
	uint8_t narrow, shadow_enabled;
	uint8_t background_enabled;
} XvtSnapGlyph;

typedef struct XvtSnapPaint {
	XvtSnapDrawHeader draw;
	uint32_t kind, color_argb;
	int32_t x0, y0, x1, y1;
} XvtSnapPaint;

typedef struct XvtSnapSurfaceEvent {
	uint32_t z_order;
	uint16_t kind, target, source_target;
	XvtSnapRect rect;
	uint32_t color_argb, save_id;
} XvtSnapSurfaceEvent;

typedef struct XvtSnapCopyRect {
	XvtSnapDrawHeader draw;
	uint16_t source_target;
	XvtSnapRect source, destination;
} XvtSnapCopyRect;

typedef struct XvtSnapRadarBlip {
	XvtSnapObjectId object;
	int16_t x, y;
	uint16_t color_index;
	uint8_t targeted;
} XvtSnapRadarBlip;

typedef struct XvtSnapTargetBox {
	XvtSnapObjectId object;
	uint16_t component, color_index;
	uint8_t layer;
	int32_t extent;
	int32_t world_pos[3];
} XvtSnapTargetBox;

typedef struct XvtSnapPreview {
	XvtSnapDrawHeader draw;
	uint64_t opt_asset_id;
	XvtSnapCamera camera;
	XvtSnapLighting lighting;
	XvtSnapObjectId object;
	XvtSnapRect destination;
	float view_pos[3], view_orient[9];
	/* Frontend OPT normalization is applied to original model vertices at load. */
	float model_scale;
	uint16_t component, node_switch;
	uint8_t mask_index, valid;
	uint8_t component_marker_valid;
	int32_t component_marker_world[3];
} XvtSnapPreview;

typedef struct XvtSnapCockpitDescriptor {
	uint64_t lfd_asset_id;
	uint8_t enabled;
	char lfd_name[10], display_name[17];
	XvtSnapRect viewport;
	int16_t projection_offset_y;
} XvtSnapCockpitDescriptor;

typedef struct XvtSnapHudElement {
	uint16_t x, y, selector, color_index, clip_width;
	int16_t clip_height_or_foreground;
} XvtSnapHudElement;

typedef struct XvtSnapCockpitLayout {
	uint64_t generation, panel_asset_id;
	uint8_t valid;
	XvtSnapCockpitDescriptor descriptors[28];
	XvtSnapHudElement elements[432];
	uint16_t mask_bytes[3];
	uint8_t masks[3][480];
	char panel_basename[10];
	uint8_t sprite_count, sprite_count_addend;
} XvtSnapCockpitLayout;

#ifdef __cplusplus
}
#endif
#endif
