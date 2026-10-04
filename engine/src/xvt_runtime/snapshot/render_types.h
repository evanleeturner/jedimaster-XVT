#ifndef XVT_RUNTIME_SNAPSHOT_RENDER_TYPES_H
#define XVT_RUNTIME_SNAPSHOT_RENDER_TYPES_H

#include <stdint.h>

#include "xvt/assets/object_genus.h"

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
#define XVT_SNAP_PAINTS 24576
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

enum {
	XVT_SNAP_HYPERSPACE_NONE,
	XVT_SNAP_HYPERSPACE_STARTING,
	XVT_SNAP_HYPERSPACE_TRANSITION
};

enum { XVT_SNAP_TEXTURE_FRAME_BIT = 0x8000 };

enum {
	XVT_SNAP_INVALID_TEXTURE_FRAME = 0xff00,
	XVT_SNAP_HYPERSPACE_STREAK_END = 0x213
};

enum { XVT_SNAP_MESH_HULL = 1, XVT_SNAP_MESH_FUSELAGE = 3 };

typedef enum xvt_scene_kind {
	XVT_SCENE_NONE = 0,
	XVT_SCENE_FRONTEND,
	XVT_SCENE_LOADING,
	XVT_SCENE_FLIGHT,
	XVT_SCENE_FRONTEND_MODAL,
	XVT_SCENE_MOVIE
} xvt_scene_kind;

struct xvt_snap_rect {
	int32_t x;
	int32_t y;
	int32_t width;
	int32_t height;
};

struct xvt_snap_object_id {
	uint16_t slot;
	uint16_t signature;
};

struct xvt_snap_camera {
	int32_t world_pos[3];
	float rows
		[9]; /* Precise render camera, with Q15 fallback for other camera writers. */
	struct xvt_snap_rect viewport;
	int32_t center_x;
	int32_t center_y;
	int32_t projection_offset_y;
	uint16_t screen_width;
	uint16_t screen_height;
	uint16_t aspect_y_q16;
	uint8_t perspective_shift;
	uint8_t valid;
	struct xvt_snap_object_id player;
	struct xvt_snap_object_id focus;
	uint16_t view_pitch;
	uint16_t view_yaw;
	uint16_t view_roll;
	uint16_t view_up_axis_angle;
	int16_t hud_aim_x;
	int16_t hud_aim_y;
	uint16_t external;
	uint16_t replay_view;
	uint8_t map_mode;
	uint8_t hyperspace_phase;
	uint8_t hud_state;
};

struct xvt_snap_object {
	struct xvt_snap_object_id id;
	uint8_t object_type;
	uint8_t genus;
	uint8_t flight_group;
	uint8_t slot_class;
	int32_t world_pos[3];
	int32_t prev_world_pos[3];
	int32_t player_owner;
	uint16_t yaw;
	uint16_t pitch;
	uint16_t roll;
	uint16_t type_specific_word;
	uint8_t type_specific[2];
	uint8_t has_mobile;
	uint8_t has_craft;
	uint8_t orient_dirty;
	uint8_t move_dirty;
	uint8_t family;
	uint8_t iff;
	uint8_t team;
	uint8_t node_switch;
	uint8_t source_type;
	uint8_t light_scale;
	uint16_t source_slot;
	uint16_t speed;
	int16_t cached_rows_q15[9];
	int16_t move_q15[3];
	uint8_t component_state[50];
	uint8_t mesh_rotation[50];
	uint8_t component_hp[50];
	uint8_t sfoil_state;
	uint8_t object_kind;
	uint16_t working_subsystems;
	uint16_t installed_subsystems;
	uint16_t throttle;
	uint16_t overdrive_off;
	int16_t max_speed;
	uint8_t laser_recharge_level;
	uint8_t shield_recharge_level;
	uint8_t beam_recharge_level;
};

enum { XVT_SLOT_MAIN, XVT_SLOT_LOCAL_TRANSIENT, XVT_SLOT_STATIC };

struct xvt_snap_type {
	uint64_t model_asset_id;
	uint64_t texture_asset_id;
	int32_t max_extent;
	int32_t half_extent;
	uint8_t record_flags;
	uint8_t asset_flags;
	uint8_t behavior_flags;
	uint8_t model_index;
	int8_t family;
	uint8_t genus;
	uint8_t texture_group;
	uint8_t resource_entry;
	uint8_t sequence_count;
	uint8_t remap_count;
	int16_t sequence[32];
	uint8_t remap[16];
};

struct xvt_snap_lighting {
	int32_t direction_q15[3];
	int32_t local_lights_level;
	int32_t directional_enabled;
};

struct xvt_snap_opt_asset {
	uint64_t id;
	uint16_t classic_handle;
	char path[XVT_SNAP_PATH];
};

struct xvt_snap_texture_asset {
	uint64_t id;
	uint16_t classic_handle;
	uint16_t model_type;
	char path[XVT_SNAP_PATH];
};

typedef enum xvt_snap_image_kind {
	XVT_IMAGE_BMP = 1,
	XVT_IMAGE_LFD,
	XVT_IMAGE_PNL,
	XVT_IMAGE_ICO,
	XVT_IMAGE_ABP,
	XVT_IMAGE_MICRO_FNT,
	XVT_IMAGE_BUILTIN_CURSOR
} xvt_snap_image_kind;

struct xvt_snap_image_asset {
	uint64_t id;
	uint32_t kind;
	char path[XVT_SNAP_PATH];
	uint32_t first_record;
	uint32_t record_count;
	uint16_t font_point_size;
	uint8_t font_row_bytes;
	uint8_t make_palette;
	/* Captured with an LFD load; remains valid after cockpit layout replacement. */
	struct xvt_snap_rect cockpit_viewport;
};

enum {
	XVT_TARGET_FRONT_BACK,
	XVT_TARGET_FRONT_OFFSCREEN,
	XVT_TARGET_FRONT_BACKUP,
	XVT_TARGET_FLIGHT_MAIN
};

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

struct xvt_snap_draw_header {
	uint32_t z_order;
	uint16_t target;
	uint16_t scope;
	struct xvt_snap_rect clip;
};

struct xvt_snap_sprite {
	struct xvt_snap_draw_header draw;
	uint64_t asset_id;
	uint32_t frame;
	struct xvt_snap_rect source;
	struct xvt_snap_rect destination;
	uint32_t kind;
	uint32_t tint_color;
};

struct xvt_snap_glyph {
	struct xvt_snap_draw_header draw;
	uint64_t font_asset_id;
	int32_t x;
	int32_t y;
	uint32_t foreground_argb;
	uint32_t background_argb;
	uint32_t shadow_argb;
	uint16_t character;
	uint16_t advance;
	uint8_t narrow;
	uint8_t shadow_enabled;
	uint8_t background_enabled;
};

struct xvt_snap_paint {
	struct xvt_snap_draw_header draw;
	uint32_t kind;
	uint32_t color_argb;
	int32_t x0;
	int32_t y0;
	int32_t x1;
	int32_t y1;
};

struct xvt_snap_surface_event {
	uint32_t z_order;
	uint16_t kind;
	uint16_t target;
	uint16_t source_target;
	struct xvt_snap_rect rect;
	uint32_t color_argb;
	uint32_t save_id;
};

struct xvt_snap_copy_rect {
	struct xvt_snap_draw_header draw;
	uint16_t source_target;
	struct xvt_snap_rect source;
	struct xvt_snap_rect destination;
};

struct xvt_snap_radar_blip {
	struct xvt_snap_object_id object;
	int16_t x;
	int16_t y;
	uint16_t color_index;
	uint8_t targeted;
};

struct xvt_snap_target_box {
	struct xvt_snap_object_id object;
	uint16_t component;
	uint16_t color_index;
	uint8_t scope;
	int32_t extent;
	int32_t world_pos[3];
};

struct xvt_snap_preview {
	struct xvt_snap_draw_header draw;
	uint64_t opt_asset_id;
	struct xvt_snap_camera camera;
	struct xvt_snap_lighting lighting;
	struct xvt_snap_object_id object;
	struct xvt_snap_rect destination;
	float view_pos[3];
	float view_orient[9];
	/* Frontend OPT normalization is applied to original model vertices at load. */
	float model_scale;
	uint16_t component;
	uint16_t node_switch;
	uint8_t mask_index;
	uint8_t valid;
	uint8_t component_marker_valid;
	int32_t component_marker_world[3];
};

struct xvt_snap_cockpit_descriptor {
	uint64_t lfd_asset_id;
	uint8_t enabled;
	char lfd_name[10];
	char display_name[17];
	struct xvt_snap_rect viewport;
	int16_t projection_offset_y;
};

struct xvt_snap_hud_element {
	uint16_t x;
	uint16_t y;
	uint16_t selector;
	uint16_t color_index;
	uint16_t clip_width;
	int16_t clip_height_or_foreground;
};

struct xvt_snap_cockpit_layout {
	uint64_t generation;
	uint64_t panel_asset_id;
	uint8_t valid;
	struct xvt_snap_cockpit_descriptor descriptors[28];
	struct xvt_snap_hud_element elements[432];
	uint16_t mask_bytes[3];
	uint8_t masks[3][480];
	char panel_basename[10];
	uint8_t sprite_count;
	uint8_t sprite_count_addend;
};

#ifdef __cplusplus
}
#endif
#endif
