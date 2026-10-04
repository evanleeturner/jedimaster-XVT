#ifndef XVT_RUNTIME_SNAPSHOT_COCKPIT_STATE_H
#define XVT_RUNTIME_SNAPSHOT_COCKPIT_STATE_H

#include "xvt/flight/hud/hud.h"
#include "xvt/flight/hud/mfd.h"
#include "xvt_runtime/snapshot/render_types.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
	XVT_HUD_PANEL_BINDINGS = 265,
	XVT_HUD_FONT_TIERS = 3,
	XVT_HUD_WEAPON_SLOTS = 16,
	XVT_HUD_ROWS_PER_SECTION = 96,
	XVT_HUD_PAGE_ROW_CAPACITY = 7 * XVT_HUD_ROWS_PER_SECTION,
	XVT_HUD_PAGE_GLYPH_CAPACITY = 8192
};

enum {
	XVT_HUD_MESSAGE_GLYPHS = 128,
	XVT_HUD_ALERT_LINE_GLYPHS = 256,
	XVT_HUD_LOADING_GLYPHS = 5 * 256,
	XVT_HUD_OVERLAY_GLYPHS = 3 * XVT_HUD_MESSAGE_GLYPHS +
				 3 * XVT_HUD_ALERT_LINE_GLYPHS +
				 XVT_HUD_LOADING_GLYPHS
};

typedef enum xvt_cockpit_phase {
	XVT_COCKPIT_WORLD_MARKERS,
	XVT_COCKPIT_BEFORE_CRT,
	XVT_COCKPIT_CRT,
	XVT_COCKPIT_AFTER_CRT,
	XVT_COCKPIT_ALERT
} xvt_cockpit_phase;

typedef enum xvt_cockpit_feature {
	XVT_COCKPIT_FEATURE_TARGET = 1 << 0,
	XVT_COCKPIT_FEATURE_LASER_CHARGE = 1 << 1,
	XVT_COCKPIT_FEATURE_LASER_SELECTION = 1 << 2,
	XVT_COCKPIT_FEATURE_WARHEADS = 1 << 3,
	XVT_COCKPIT_FEATURE_BEAM = 1 << 4,
	XVT_COCKPIT_FEATURE_SHIELDS = 1 << 5,
	XVT_COCKPIT_FEATURE_SPEED = 1 << 6,
	XVT_COCKPIT_FEATURE_FORE_RADAR = 1 << 7,
	XVT_COCKPIT_FEATURE_AFT_RADAR = 1 << 8,
	XVT_COCKPIT_FEATURE_LASER_POWER = 1 << 9,
	XVT_COCKPIT_FEATURE_ENGINE_POWER = 1 << 10,
	XVT_COCKPIT_FEATURE_SHIELD_POWER = 1 << 11,
	XVT_COCKPIT_FEATURE_BEAM_POWER = 1 << 12
} xvt_cockpit_feature;

typedef enum xvt_cockpit_alignment {
	XVT_COCKPIT_ALIGN_LEFT,
	XVT_COCKPIT_ALIGN_CENTER,
	XVT_COCKPIT_ALIGN_RIGHT
} xvt_cockpit_alignment;

typedef enum xvt_cockpit_font_tier {
	XVT_COCKPIT_FONT_SMALL,
	XVT_COCKPIT_FONT_MESSAGE,
	XVT_COCKPIT_FONT_INSTRUMENT
} xvt_cockpit_font_tier;

struct xvt_cockpit_glyph {
	uint64_t font_asset_id;
	struct xvt_snap_rect clip;
	uint32_t foreground_argb;
	uint32_t background_argb;
	uint32_t shadow_argb;
	int16_t x;
	int16_t y;
	uint16_t character;
	uint16_t advance;
	uint16_t height;
	uint8_t narrow;
	uint8_t shadow_enabled;
};

struct xvt_cockpit_page_row {
	uint32_t key;
	uint32_t background_argb;
	struct xvt_snap_rect bounds;
	uint16_t first_glyph;
	uint16_t glyph_count;
	uint8_t selected;
};

struct xvt_cockpit_page {
	uint64_t content_generation;
	uint16_t page_id;
	uint16_t layout_id;
	uint8_t visible;
	uint8_t focused;
	int16_t first_visible_row;
	int16_t selected_row;
	uint16_t total_rows;
	uint16_t first_store_row;
	uint16_t row_count;
	uint16_t first_glyph;
	uint16_t glyph_count;
	uint16_t header_glyph_count;
	struct xvt_snap_rect placement;
	struct xvt_snap_rect background_bounds;
	struct xvt_snap_rect border_bounds;
	uint32_t background_argb;
	uint32_t border_argb;
	uint16_t command_text_mode;
	uint16_t phase;
	int16_t original_state;
};

struct xvt_cockpit_page_store {
	uint16_t row_count;
	uint16_t glyph_count;
	struct xvt_cockpit_page_row rows[XVT_HUD_PAGE_ROW_CAPACITY];
	struct xvt_cockpit_glyph glyphs[XVT_HUD_PAGE_GLYPH_CAPACITY];
};

struct xvt_cockpit_asset_binding {
	uint64_t asset_id;
	uint32_t frame;
};

struct xvt_cockpit_font_binding {
	uint64_t asset_id;
	uint16_t line_height;
	uint16_t half_height;
};

struct xvt_cockpit_definition {
	uint64_t resource_generation;
	struct xvt_snap_cockpit_layout layout;
	struct xvt_cockpit_asset_binding panels[XVT_HUD_PANEL_BINDINGS];
	struct xvt_cockpit_font_binding fonts[XVT_HUD_FONT_TIERS];
	uint8_t beam_segment_colors[4];
	uint8_t shield_colors[11];
	int16_t beam_offsets[9][2];
};

struct xvt_cockpit_view {
	uint16_t screen_width;
	uint16_t screen_height;
	uint16_t hud_state;
	uint16_t instrument_base;
	uint16_t resource_descriptor;
	uint16_t viewport_descriptor;
	uint16_t panel_set;
	uint16_t active_page;
	uint16_t secondary_page;
	uint8_t mirrored;
	uint8_t map_active;
	uint8_t external_camera;
	uint8_t instruments_visible;
	uint8_t mission_ending;
	uint8_t awaiting_new_craft;
	uint8_t rebel_fighter;
	uint8_t laser_slots;
	struct xvt_snap_rect viewport;
	int16_t projection_offset_y;
};

/* Loaded-resource description is independent of the last presented cockpit. */
struct xvt_cockpit_resources {
	struct xvt_cockpit_definition definition;
	struct xvt_cockpit_view view;
	uint32_t installed_hud_features;
	uint32_t palette[256];
	uint32_t view_palette[28][64];
	uint8_t valid;
};

struct xvt_cockpit_number {
	int32_t value;
	struct xvt_snap_rect bounds;
	int16_t x;
	int16_t y;
	uint16_t minimum_digits;
	uint16_t field_width;
	uint8_t visible;
	uint8_t font_tier;
	uint8_t alignment;
	uint8_t foreground;
	uint8_t background;
	uint8_t shadow_enabled;
	uint8_t shadow_color;
	uint8_t phase;
	uint8_t clear_background;
	uint8_t trailing_space;
	uint8_t word_wrap;
	uint8_t clear_line;
	uint8_t narrow;
	uint8_t keyed;
	uint32_t color_key_argb;
};

typedef enum xvt_cockpit_number_id {
	XVT_COCKPIT_NUMBER_SPEED,
	XVT_COCKPIT_NUMBER_THROTTLE,
	XVT_COCKPIT_NUMBER_CLOCK_MINUTES,
	XVT_COCKPIT_NUMBER_CLOCK_SECONDS,
	XVT_COCKPIT_NUMBER_COUNTERMEASURES,
	XVT_COCKPIT_NUMBER_LAUNCHER_FIRST,
	XVT_COCKPIT_NUMBER_LAUNCHER_LAST =
		XVT_COCKPIT_NUMBER_LAUNCHER_FIRST + 3,
	XVT_COCKPIT_NUMBER_LASER_FIRST,
	XVT_COCKPIT_NUMBER_LASER_LAST =
		XVT_COCKPIT_NUMBER_LASER_FIRST + XVT_HUD_WEAPON_SLOTS - 1,
	XVT_COCKPIT_NUMBER_TARGET_SYSTEMS,
	XVT_COCKPIT_NUMBER_TARGET_SHIELDS,
	XVT_COCKPIT_NUMBER_TARGET_HULL,
	XVT_COCKPIT_NUMBER_TARGET_RANGE,
	XVT_COCKPIT_NUMBER_TARGET_RANGE_FRACTION,
	XVT_COCKPIT_NUMBER_ORDER_RANGE,
	XVT_COCKPIT_NUMBER_ORDER_RANGE_FRACTION,
	XVT_COCKPIT_NUMBER_ORDER_MINUTES,
	XVT_COCKPIT_NUMBER_ORDER_SECONDS,
	XVT_COCKPIT_NUMBER_COURSE_LEVEL,
	XVT_COCKPIT_NUMBER_COURSE_REMAINING,
	XVT_COCKPIT_NUMBER_COURSE_PASSED,
	XVT_COCKPIT_NUMBER_COURSE_TARGETS,
	XVT_COCKPIT_NUMBER_COURSE_SCORE,
	XVT_COCKPIT_NUMBER_COUNT
} xvt_cockpit_number_id;

struct xvt_cockpit_shield {
	uint8_t visible;
	uint8_t text_mode;
	uint8_t primary_level;
	uint8_t overcharge_level;
	uint8_t primary_color;
	uint8_t overcharge_color;
	uint8_t hit_flash;
};

struct xvt_cockpit_power_gauge {
	uint8_t visible;
	uint8_t filled;
	uint8_t segments;
	uint8_t rebel_fighter;
	int16_t step_y;
};

struct xvt_cockpit_indicator {
	/* No code reads color; every indicator is built with 0 in it. */
	uint8_t visible;
	uint8_t state;
	uint8_t color;
	uint8_t phase;
};

struct xvt_cockpit_caption {
	uint8_t visible;
	uint8_t font_tier;
	uint8_t foreground;
	uint8_t background;
	uint8_t alignment;
	uint8_t phase;
	char text[256];
};

typedef enum xvt_cockpit_text_field_id {
	XVT_COCKPIT_TEXT_TARGET_NAME,
	XVT_COCKPIT_TEXT_TARGET_CARGO,
	XVT_COCKPIT_TEXT_TARGET_DETAIL,
	XVT_COCKPIT_TEXT_CMD_RANGE,
	XVT_COCKPIT_TEXT_CMD_ORDERS_LABEL,
	XVT_COCKPIT_TEXT_CMD_ORDERS,
	XVT_COCKPIT_TEXT_CMD_TARGET_LABEL,
	XVT_COCKPIT_TEXT_CMD_TARGET,
	XVT_COCKPIT_TEXT_CMD_RANGE_LABEL,
	XVT_COCKPIT_TEXT_CMD_TIME_LABEL,
	XVT_COCKPIT_TEXT_CMD_TIME_UNKNOWN,
	XVT_COCKPIT_TEXT_CRAFT_STATUS,
	XVT_COCKPIT_TEXT_NETWORK_PING,
	XVT_COCKPIT_TEXT_NETWORK_LAG,
	XVT_COCKPIT_TEXT_MAP_FOLLOWING,
	XVT_COCKPIT_TEXT_MAP_TRACKING,
	XVT_COCKPIT_TEXT_RESOURCE_NAME,
	XVT_COCKPIT_TEXT_CRITICAL_WARNING,
	XVT_COCKPIT_TEXT_SHIELD_FORE,
	XVT_COCKPIT_TEXT_SHIELD_AFT,
	XVT_COCKPIT_TEXT_THROTTLE_LABEL,
	XVT_COCKPIT_TEXT_SPEED_LABEL,
	XVT_COCKPIT_TEXT_POWER_LABEL_FIRST,
	XVT_COCKPIT_TEXT_POWER_LABEL_LAST =
		XVT_COCKPIT_TEXT_POWER_LABEL_FIRST + 3,
	XVT_COCKPIT_TEXT_SHIELD_LABEL_FIRST,
	XVT_COCKPIT_TEXT_SHIELD_LABEL_LAST =
		XVT_COCKPIT_TEXT_SHIELD_LABEL_FIRST + 1,
	XVT_COCKPIT_TEXT_TARGET_SYSTEM_LABEL,
	XVT_COCKPIT_TEXT_TARGET_RANGE_LABEL,
	XVT_COCKPIT_TEXT_TARGET_SHIELD_LABEL,
	XVT_COCKPIT_TEXT_TARGET_HULL_LABEL,
	XVT_COCKPIT_TEXT_CMD_HEADER_FIRST,
	XVT_COCKPIT_TEXT_CMD_HEADER_LAST =
		XVT_COCKPIT_TEXT_CMD_HEADER_FIRST + 3,
	XVT_COCKPIT_TEXT_ARMAMENT_LABEL_FIRST,
	XVT_COCKPIT_TEXT_ARMAMENT_LABEL_LAST =
		XVT_COCKPIT_TEXT_ARMAMENT_LABEL_FIRST + 3,
	XVT_COCKPIT_TEXT_COURSE_LABEL_FIRST,
	XVT_COCKPIT_TEXT_COURSE_LABEL_LAST =
		XVT_COCKPIT_TEXT_COURSE_LABEL_FIRST + 4,
	XVT_COCKPIT_TEXT_CLOCK_SEPARATOR,
	XVT_COCKPIT_TEXT_THROTTLE_PERCENT,
	XVT_COCKPIT_TEXT_TARGET_RANGE_SEPARATOR,
	XVT_COCKPIT_TEXT_TARGET_SHIELD_PERCENT,
	XVT_COCKPIT_TEXT_TARGET_HULL_PERCENT,
	XVT_COCKPIT_TEXT_TARGET_SYSTEM_PERCENT,
	XVT_COCKPIT_TEXT_ORDER_RANGE_SEPARATOR,
	XVT_COCKPIT_TEXT_ORDER_TIME_SEPARATOR,
	XVT_COCKPIT_TEXT_FIELD_COUNT
} xvt_cockpit_text_field_id;

/* A named field owns the last text actually selected by the original HUD.
 * Bounds describe layout and clipping; they never refer to saved pixels. */
struct xvt_cockpit_text_field {
	uint64_t generation;
	struct xvt_cockpit_caption caption;
	struct xvt_snap_rect bounds;
	int16_t x;
	int16_t y;
	uint8_t shadow_enabled;
	uint8_t shadow_color;
	uint8_t lowercase;
	uint8_t word_wrap;
	uint8_t clear_background;
	uint8_t keyed;
	uint8_t clear_line;
	uint8_t narrow;
	uint32_t color_key_argb;
};

struct xvt_cockpit_systems {
	uint32_t installed;
	uint32_t working;
	uint32_t active_hud_features;
	uint32_t installed_hud_features;
	struct xvt_cockpit_shield shields[2];
	struct xvt_cockpit_power_gauge engine_power;
	struct xvt_cockpit_power_gauge laser_power;
	struct xvt_cockpit_power_gauge shield_power;
	struct xvt_cockpit_power_gauge beam_power;
	struct xvt_cockpit_indicator beam_enabled;
	struct xvt_cockpit_indicator sfoils;
	struct xvt_cockpit_indicator shield_distribution;
	struct xvt_cockpit_indicator countermeasure_active;
	struct xvt_cockpit_indicator threats[4];
	struct xvt_cockpit_indicator critical_warning;
	struct xvt_cockpit_indicator hull_indicator;
	struct xvt_cockpit_indicator feature_covers[13];
	struct xvt_cockpit_indicator unavailable_shields;
	struct xvt_cockpit_indicator unavailable_beam[2];
	struct xvt_cockpit_number countermeasure_count;
	uint8_t beam_visible;
	uint8_t beam_segments[9];
};

struct xvt_cockpit_weapon_slot {
	uint8_t visible;
	uint8_t bank;
	uint8_t hud_slot;
	uint8_t charge_band;
	uint8_t segments;
	uint8_t selection_state;
	uint8_t ready_state;
	uint8_t locked;
	uint8_t unused;
	uint8_t charge_visible;
	uint8_t selection_visible;
	uint8_t lock_visible;
	uint8_t empty_band;
	struct xvt_cockpit_number charge_percent;
};

struct xvt_cockpit_launcher {
	struct xvt_cockpit_number count;
	uint8_t visible;
	uint8_t bank;
	uint8_t weapon_slot;
	uint8_t selection;
};

struct xvt_cockpit_warhead_bank {
	uint16_t type;
	uint8_t visible;
	uint8_t selected;
};

struct xvt_cockpit_weapons {
	uint8_t slot_count;
	uint8_t selected_bank;
	struct xvt_cockpit_weapon_slot slots[XVT_HUD_WEAPON_SLOTS];
	struct xvt_cockpit_warhead_bank warheads[2];
	struct xvt_cockpit_launcher launchers[4];
	struct xvt_cockpit_indicator lock_indicator;
};

struct xvt_cockpit_target {
	struct xvt_snap_object_id object;
	uint16_t type;
	uint16_t component;
	uint8_t visible;
	uint8_t box_visible;
	struct xvt_cockpit_number hull;
	struct xvt_cockpit_number shields;
	struct xvt_cockpit_number systems;
	struct xvt_cockpit_number distance;
	struct xvt_cockpit_number distance_fraction;
	uint8_t panel_cover;
	uint8_t labels_visible;
	uint8_t cmd_mode;
	uint16_t cover_binding;
	struct xvt_cockpit_indicator armament[4];
	struct xvt_cockpit_number order_distance;
	struct xvt_cockpit_number order_distance_fraction;
	struct xvt_cockpit_number order_minutes;
	struct xvt_cockpit_number order_seconds;
};

struct xvt_cockpit_radar {
	uint8_t visible[2];
	uint8_t count[2];
	uint8_t marker_visible;
	uint8_t marker_side;
	int16_t marker_x;
	int16_t marker_y;
	struct xvt_snap_radar_blip blips[2][48];
	uint8_t coverage[2][48];
};

struct xvt_cockpit_readouts {
	struct xvt_cockpit_number speed;
	struct xvt_cockpit_number throttle;
	struct xvt_cockpit_number clock_minutes;
	struct xvt_cockpit_number clock_seconds;
};

struct xvt_cockpit_proving_grounds {
	uint8_t visible;
	struct xvt_snap_rect bounds;
	struct xvt_cockpit_number level;
	struct xvt_cockpit_number remaining;
	struct xvt_cockpit_number passed;
	struct xvt_cockpit_number targets;
	struct xvt_cockpit_number score;
};

typedef enum xvt_cockpit_message_id {
	XVT_COCKPIT_MESSAGE_READY,
	XVT_COCKPIT_MESSAGE_SYSTEM,
	XVT_COCKPIT_MESSAGE_FLIGHT_GROUP,
	XVT_COCKPIT_MESSAGE_COUNT
} xvt_cockpit_message_id;

struct xvt_cockpit_message {
	uint64_t generation;
	struct xvt_snap_rect placement;
	uint16_t message_id;
	uint16_t revealed_characters;
	uint16_t age_seconds;
	uint16_t timer_ticks;
	uint8_t visible;
	uint8_t sender_iff;
	uint8_t pane_type;
	uint8_t font_tier;
	uint16_t first_glyph;
	uint16_t glyph_count;
};

struct xvt_cockpit_messages {
	struct xvt_cockpit_message panes[XVT_COCKPIT_MESSAGE_COUNT];
};

struct xvt_cockpit_alert {
	uint64_t generation;
	struct xvt_snap_rect placement;
	uint8_t active;
	uint32_t border_argb;
	uint32_t row_background_argb[5];
	uint8_t line_visible[3];
	uint16_t first_glyph[3];
	uint16_t glyph_count[3];
};

struct xvt_cockpit_loading {
	uint64_t generation;
	uint64_t text_generation;
	struct xvt_snap_rect text_bounds;
	uint16_t first_glyph;
	uint16_t glyph_count;
	uint8_t text_visible;
	struct xvt_snap_rect progress_placement;
	uint16_t progress_step;
	uint16_t filled_width;
	uint8_t progress_visible;
	uint32_t foreground_argb;
	uint32_t background_argb;
};

struct xvt_cockpit_overlay_store {
	uint16_t glyph_count;
	struct xvt_cockpit_glyph glyphs[XVT_HUD_OVERLAY_GLYPHS];
};

struct xvt_cockpit_state {
	uint64_t presentation_serial;
	uint64_t definition_generation;
	uint64_t palette_generation;
	uint64_t artwork_generation;
	uint64_t instruments_generation;
	uint64_t radar_generation;
	uint64_t text_generation;
	uint64_t crt_generation;
	uint8_t valid;
	struct xvt_cockpit_view view;
	struct xvt_cockpit_definition definition;
	struct xvt_cockpit_systems systems;
	struct xvt_cockpit_weapons weapons;
	struct xvt_cockpit_target target;
	uint8_t mouse_stick_visible;
	int8_t mouse_stick_x;
	int8_t mouse_stick_y;
	struct xvt_cockpit_radar radar;
	struct xvt_cockpit_readouts readouts;
	struct xvt_cockpit_proving_grounds proving_grounds;
	struct xvt_cockpit_text_field text_fields[XVT_COCKPIT_TEXT_FIELD_COUNT];
	struct xvt_cockpit_page pages[MFD_PAGE_COUNT];
	struct xvt_cockpit_messages messages;
	struct xvt_cockpit_alert alert;
	struct xvt_cockpit_loading loading;
	struct xvt_snap_preview crt;
	uint32_t palette_argb[256];
	struct xvt_cockpit_page_store page_content;
	struct xvt_cockpit_overlay_store overlay_content;
};

#ifdef __cplusplus
}
#endif
#endif
