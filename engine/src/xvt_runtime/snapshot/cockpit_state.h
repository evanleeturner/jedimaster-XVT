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
	XVT_HUD_VISIBLE_ROWS_PER_PAGE = 96,
	XVT_HUD_PAGE_ROW_CAPACITY = 7 * XVT_HUD_VISIBLE_ROWS_PER_PAGE,
	XVT_HUD_PAGE_GLYPH_CAPACITY = 8192
};

enum {
	XVT_HUD_MESSAGE_GLYPHS = 128,
	XVT_HUD_ALERT_LINE_GLYPHS = 256,
	XVT_HUD_LOADING_GLYPHS = 5 * 256,
	XVT_HUD_OVERLAY_GLYPHS =
		3 * XVT_HUD_MESSAGE_GLYPHS + 3 * XVT_HUD_ALERT_LINE_GLYPHS + XVT_HUD_LOADING_GLYPHS
};

typedef enum XvtCockpitPhase {
	XVT_COCKPIT_WORLD_MARKERS,
	XVT_COCKPIT_BEFORE_CRT,
	XVT_COCKPIT_CRT,
	XVT_COCKPIT_AFTER_CRT,
	XVT_COCKPIT_ALERT
} XvtCockpitPhase;

typedef enum XvtCockpitFeature {
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
} XvtCockpitFeature;

typedef enum XvtCockpitAlignment {
	XVT_COCKPIT_ALIGN_LEFT,
	XVT_COCKPIT_ALIGN_CENTER,
	XVT_COCKPIT_ALIGN_RIGHT
} XvtCockpitAlignment;

typedef enum XvtCockpitFontTier {
	XVT_COCKPIT_FONT_SMALL,
	XVT_COCKPIT_FONT_MESSAGE,
	XVT_COCKPIT_FONT_INSTRUMENT
} XvtCockpitFontTier;

typedef struct XvtCockpitGlyph {
	uint64_t font_asset_id;
	XvtSnapRect clip;
	uint32_t foreground_argb, background_argb, shadow_argb;
	int16_t x, y;
	uint16_t character, advance, height;
	uint8_t narrow, shadow_enabled;
} XvtCockpitGlyph;

typedef struct XvtCockpitPageRow {
	uint32_t key, background_argb;
	XvtSnapRect bounds;
	uint16_t first_glyph, glyph_count;
	uint8_t selected;
} XvtCockpitPageRow;

typedef struct XvtCockpitPage {
	uint64_t content_generation;
	uint16_t page_id, layout_id;
	uint8_t visible, focused;
	int16_t first_visible_row, selected_row;
	uint16_t total_rows, first_row, row_count;
	uint16_t first_glyph, glyph_count, header_glyph_count;
	XvtSnapRect placement, background_bounds, border_bounds;
	uint32_t background_argb, border_argb;
	uint16_t style, layer;
	int16_t original_state;
} XvtCockpitPage;

typedef struct XvtCockpitPageStore {
	uint16_t row_count, glyph_count;
	XvtCockpitPageRow rows[XVT_HUD_PAGE_ROW_CAPACITY];
	XvtCockpitGlyph glyphs[XVT_HUD_PAGE_GLYPH_CAPACITY];
} XvtCockpitPageStore;

typedef struct XvtCockpitAssetBinding {
	uint64_t asset_id;
	uint32_t frame;
} XvtCockpitAssetBinding;

typedef struct XvtCockpitFontBinding {
	uint64_t asset_id;
	uint16_t line_height, half_height;
} XvtCockpitFontBinding;

typedef struct XvtCockpitDefinition {
	uint64_t resource_generation;
	XvtSnapCockpitLayout layout;
	XvtCockpitAssetBinding panels[XVT_HUD_PANEL_BINDINGS];
	XvtCockpitFontBinding fonts[XVT_HUD_FONT_TIERS];
	uint8_t beam_fades[4], shield_colors[11];
	int16_t beam_offsets[9][2];
} XvtCockpitDefinition;

typedef struct XvtCockpitView {
	uint16_t screen_width, screen_height, hud_state, instrument_base;
	uint16_t resource_descriptor, viewport_descriptor, panel_set;
	uint16_t active_page, secondary_page;
	uint8_t mirrored, map_active, external_camera, instruments_visible;
	uint8_t mission_ending, region_session;
	uint8_t compact_instruments, laser_slots;
	XvtSnapRect viewport;
	int16_t projection_offset_y;
} XvtCockpitView;

/* Loaded-resource description is independent of the last presented cockpit. */
typedef struct XvtCockpitResources {
	XvtCockpitDefinition definition;
	XvtCockpitView view;
	uint32_t installed_hud_features;
	uint32_t palette[256], view_palette[28][64];
	uint8_t valid;
} XvtCockpitResources;

typedef struct XvtCockpitNumber {
	int32_t value;
	XvtSnapRect bounds;
	int16_t x, y;
	uint16_t minimum_digits, field_width;
	uint8_t visible, font_tier, alignment, foreground, background;
	uint8_t shadow_enabled, shadow_color, phase;
	uint8_t clear_background, trailing_space;
	uint8_t word_wrap, clear_line, narrow, keyed;
	uint32_t color_key_argb;
} XvtCockpitNumber;

typedef enum XvtCockpitNumberId {
	XVT_COCKPIT_NUMBER_SPEED,
	XVT_COCKPIT_NUMBER_THROTTLE,
	XVT_COCKPIT_NUMBER_CLOCK_MINUTES,
	XVT_COCKPIT_NUMBER_CLOCK_SECONDS,
	XVT_COCKPIT_NUMBER_COUNTERMEASURES,
	XVT_COCKPIT_NUMBER_LAUNCHER_FIRST,
	XVT_COCKPIT_NUMBER_LAUNCHER_LAST = XVT_COCKPIT_NUMBER_LAUNCHER_FIRST + 3,
	XVT_COCKPIT_NUMBER_LASER_FIRST,
	XVT_COCKPIT_NUMBER_LASER_LAST = XVT_COCKPIT_NUMBER_LASER_FIRST + XVT_HUD_WEAPON_SLOTS - 1,
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
} XvtCockpitNumberId;

typedef struct XvtCockpitShield {
	uint8_t visible, text_mode, primary_level, overcharge_level;
	uint8_t primary_color, overcharge_color, hit_flash;
} XvtCockpitShield;

typedef struct XvtCockpitPowerGauge {
	uint8_t visible, filled, segments, compact;
	int16_t step_y;
} XvtCockpitPowerGauge;

typedef struct XvtCockpitIndicator {
	uint8_t visible, state, color, phase;
} XvtCockpitIndicator;

typedef struct XvtCockpitCaption {
	uint8_t visible, font_tier, foreground, background, alignment, phase;
	char text[256];
} XvtCockpitCaption;

typedef enum XvtCockpitTextFieldId {
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
	XVT_COCKPIT_TEXT_POWER_LABEL_LAST = XVT_COCKPIT_TEXT_POWER_LABEL_FIRST + 3,
	XVT_COCKPIT_TEXT_SHIELD_LABEL_FIRST,
	XVT_COCKPIT_TEXT_SHIELD_LABEL_LAST = XVT_COCKPIT_TEXT_SHIELD_LABEL_FIRST + 1,
	XVT_COCKPIT_TEXT_TARGET_SYSTEM_LABEL,
	XVT_COCKPIT_TEXT_TARGET_RANGE_LABEL,
	XVT_COCKPIT_TEXT_TARGET_SHIELD_LABEL,
	XVT_COCKPIT_TEXT_TARGET_HULL_LABEL,
	XVT_COCKPIT_TEXT_CMD_HEADER_FIRST,
	XVT_COCKPIT_TEXT_CMD_HEADER_LAST = XVT_COCKPIT_TEXT_CMD_HEADER_FIRST + 3,
	XVT_COCKPIT_TEXT_ARMAMENT_LABEL_FIRST,
	XVT_COCKPIT_TEXT_ARMAMENT_LABEL_LAST = XVT_COCKPIT_TEXT_ARMAMENT_LABEL_FIRST + 3,
	XVT_COCKPIT_TEXT_COURSE_LABEL_FIRST,
	XVT_COCKPIT_TEXT_COURSE_LABEL_LAST = XVT_COCKPIT_TEXT_COURSE_LABEL_FIRST + 4,
	XVT_COCKPIT_TEXT_CLOCK_SEPARATOR,
	XVT_COCKPIT_TEXT_THROTTLE_PERCENT,
	XVT_COCKPIT_TEXT_TARGET_RANGE_SEPARATOR,
	XVT_COCKPIT_TEXT_TARGET_SHIELD_PERCENT,
	XVT_COCKPIT_TEXT_TARGET_HULL_PERCENT,
	XVT_COCKPIT_TEXT_TARGET_SYSTEM_PERCENT,
	XVT_COCKPIT_TEXT_ORDER_RANGE_SEPARATOR,
	XVT_COCKPIT_TEXT_ORDER_TIME_SEPARATOR,
	XVT_COCKPIT_TEXT_FIELD_COUNT
} XvtCockpitTextFieldId;

/* A named field owns the last text actually selected by the original HUD.
 * Bounds describe layout and clipping; they never refer to saved pixels. */
typedef struct XvtCockpitTextField {
	uint64_t generation;
	XvtCockpitCaption caption;
	XvtSnapRect bounds;
	int16_t x, y;
	uint8_t shadow_enabled, shadow_color, lowercase, word_wrap;
	uint8_t clear_background;
	uint8_t keyed;
	uint8_t clear_line, narrow;
	uint32_t color_key_argb;
} XvtCockpitTextField;

typedef struct XvtCockpitSystems {
	uint32_t installed, working, hud_features, installed_hud_features;
	XvtCockpitShield shields[2];
	XvtCockpitPowerGauge engine_power, laser_power, shield_power, beam_power;
	XvtCockpitIndicator beam_enabled, sfoils, shield_distribution, countermeasure_selected;
	XvtCockpitIndicator threats[4], critical_warning, hull_indicator;
	XvtCockpitIndicator feature_covers[13], unavailable_shields, unavailable_beam[2];
	XvtCockpitNumber countermeasure_count;
	uint8_t beam_visible, beam_segments[9];
} XvtCockpitSystems;

typedef struct XvtCockpitWeaponSlot {
	uint8_t visible, bank, hud_slot, charge_band, segments;
	uint8_t selected, ready, locked, color;
	uint8_t charge_visible, selection_visible, lock_visible, empty_band;
	XvtCockpitNumber charge_percent;
} XvtCockpitWeaponSlot;

typedef struct XvtCockpitLauncher {
	XvtCockpitNumber count;
	uint8_t visible, bank, weapon_slot, selection;
} XvtCockpitLauncher;

typedef struct XvtCockpitWarheadBank {
	uint16_t type;
	uint8_t visible, selected;
} XvtCockpitWarheadBank;

typedef struct XvtCockpitWeapons {
	uint8_t slot_count, selected_bank;
	XvtCockpitWeaponSlot slots[XVT_HUD_WEAPON_SLOTS];
	XvtCockpitWarheadBank warheads[2];
	XvtCockpitLauncher launchers[4];
	XvtCockpitIndicator lock_indicator;
} XvtCockpitWeapons;

typedef struct XvtCockpitTarget {
	XvtSnapObjectId object;
	uint16_t type, component;
	uint8_t visible, box_visible;
	XvtCockpitNumber hull, shields, systems, distance, distance_fraction;
	uint8_t panel_cover, labels_visible, cmd_mode;
	uint16_t cover_binding;
	XvtCockpitIndicator armament[4];
	XvtCockpitNumber order_distance, order_distance_fraction, order_minutes, order_seconds;
} XvtCockpitTarget;

typedef struct XvtCockpitRadar {
	uint8_t visible[2], count[2], marker_visible, marker_side;
	int16_t marker_x, marker_y;
	XvtSnapRadarBlip blips[2][48];
	uint8_t coverage[2][48];
} XvtCockpitRadar;

typedef struct XvtCockpitReadouts {
	XvtCockpitNumber speed, throttle, clock_minutes, clock_seconds;
} XvtCockpitReadouts;

typedef struct XvtCockpitProvingGrounds {
	uint8_t visible;
	XvtSnapRect bounds;
	XvtCockpitNumber level, remaining, passed, targets, score;
} XvtCockpitProvingGrounds;

typedef enum XvtCockpitMessageId {
	XVT_COCKPIT_MESSAGE_READY,
	XVT_COCKPIT_MESSAGE_SYSTEM,
	XVT_COCKPIT_MESSAGE_FLIGHT_GROUP,
	XVT_COCKPIT_MESSAGE_COUNT
} XvtCockpitMessageId;

typedef struct XvtCockpitMessage {
	uint64_t generation;
	XvtSnapRect placement;
	uint16_t message_id, revealed_characters, age_ticks, timer_ticks;
	uint8_t visible, sender_iff, pane_type, font_tier;
	uint16_t first_glyph, glyph_count;
} XvtCockpitMessage;

typedef struct XvtCockpitMessages {
	XvtCockpitMessage panes[XVT_COCKPIT_MESSAGE_COUNT];
} XvtCockpitMessages;

typedef struct XvtCockpitAlert {
	uint64_t generation;
	XvtSnapRect placement;
	uint8_t active;
	uint32_t border_argb, row_background_argb[5];
	uint8_t line_visible[3];
	uint16_t first_glyph[3], glyph_count[3];
} XvtCockpitAlert;

typedef struct XvtCockpitLoading {
	uint64_t generation;
	uint64_t text_generation;
	XvtSnapRect text_bounds;
	uint16_t first_glyph, glyph_count;
	uint8_t text_visible;
	XvtSnapRect placement;
	uint16_t progress_step;
	uint16_t filled_width;
	uint8_t visible;
	uint32_t foreground_argb, background_argb;
} XvtCockpitLoading;

typedef struct XvtCockpitOverlayStore {
	uint16_t glyph_count;
	XvtCockpitGlyph glyphs[XVT_HUD_OVERLAY_GLYPHS];
} XvtCockpitOverlayStore;

typedef struct XvtCockpitState {
	uint64_t presentation_serial, definition_generation, palette_generation;
	uint64_t artwork_generation, instruments_generation, radar_generation;
	uint64_t text_generation, crt_generation;
	uint8_t valid;
	XvtCockpitView view;
	XvtCockpitDefinition definition;
	XvtCockpitSystems systems;
	XvtCockpitWeapons weapons;
	XvtCockpitTarget target;
	uint8_t mouse_stick_visible;
	int8_t mouse_stick_x, mouse_stick_y;
	XvtCockpitRadar radar;
	XvtCockpitReadouts readouts;
	XvtCockpitProvingGrounds proving_grounds;
	XvtCockpitTextField text_fields[XVT_COCKPIT_TEXT_FIELD_COUNT];
	XvtCockpitPage pages[MFD_PAGE_COUNT];
	XvtCockpitMessages messages;
	XvtCockpitAlert alert;
	XvtCockpitLoading loading;
	XvtSnapPreview crt;
	uint32_t palette_argb[256];
	XvtCockpitPageStore page_content;
	XvtCockpitOverlayStore overlay_content;
} XvtCockpitState;

#ifdef __cplusplus
}
#endif
#endif
