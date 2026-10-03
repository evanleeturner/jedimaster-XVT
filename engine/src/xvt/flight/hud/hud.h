#ifndef XVT_FLIGHT_HUD_HUD_H
#define XVT_FLIGHT_HUD_HUD_H

#include "xvt/flight/hud/msg.h"
#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// One HUD layout is a set of 144 instrument records loaded from the cockpit .INT files.
enum hud_instrument_table_dimensions {
	HUD_INSTRUMENT_SET_COUNT = 3,
	HUD_INSTRUMENTS_PER_SET = 144,
	HUD_INSTRUMENT_COUNT =
		HUD_INSTRUMENT_SET_COUNT * HUD_INSTRUMENTS_PER_SET,
};

enum { HUD_SHIELD_TEXT_COLOR_OFFSET = 10 };

enum hud_instrument_set_base_index {
	HUD_COCKPIT_INSTRUMENT_BASE_INDEX = 0,
	HUD_ONLY_VIEW_INSTRUMENT_BASE_INDEX = HUD_INSTRUMENTS_PER_SET,
	HUD_CRAFT_LIST_INSTRUMENT_BASE_INDEX = 2 * HUD_INSTRUMENTS_PER_SET,
};

typedef enum hud_view_state {
	HUD_VIEW_FORWARD = 0,
	HUD_VIEW_FULL_SCREEN = 18,
	HUD_VIEW_HUD_ONLY = 19,
	HUD_VIEW_TARGET_CAMERA = 20,
	HUD_VIEW_CRAFT_LIST = 21,
} hud_view_state;

enum hud_mfd_element_index {
	HUD_MFD_MESSAGE_LOG_ELEMENT = 117,
	HUD_MFD_CRAFT_LIST_ELEMENT = 130,
	HUD_MFD_MAP_OR_COMMAND_ELEMENT = 131,
	HUD_MFD_GOALS_ELEMENT = 132,
	HUD_MFD_DAMAGE_ELEMENT = 133,
	HUD_MFD_SCOREBOARD_ELEMENT = 134,
};

struct hud_cockpit_resource_descriptor {
	/* 0 when the view is not offered; below 0x80 the view uses its own
	 * image, loaded at start when 1; 0x80 plus n uses view n's image and
	 * 0xC0 plus n view n's image mirrored. */
	uint8_t resource_ref;
	/* Base name of the view's .LFD image file; the craft list view's also
	 * names its .INT and .PNL files. */
	char lfd_name[9];
	uint16_t viewport_origin_x; /* Flight viewport's left edge in pixels. */
	uint16_t viewport_origin_y; /* Flight viewport's top edge in pixels. */
	uint16_t viewport_width;    /* Flight viewport's width in pixels. */
	uint16_t viewport_height;   /* Flight viewport's height in pixels. */
	int16_t projection_offset_y; /* Copied to g_proj_offset_y for the view. */
	/* Name drawn at layout 49 when the view uses image 17. */
	char display_name[16];
};

struct hud_element_layout {
	uint16_t x; /* Left edge in pixels. */
	uint16_t y; /* Top edge in pixels. */
	/* Widget-specific .INT payload: the first panel sprite of a sprite widget, the digit count of a number
	 * widget, the target inset's width, or the warning line's background color. Label widgets skip a 0
	 * and use their short text at 4 or less. */
	uint16_t selector; /* 0 hides labels; layout 396's set at run time. */
	/* Sprite widgets: the blit's transparent color. Text widgets: the text
	 * or background color. Laser charge bars run right to left when it is
	 * not 0, shield widgets show percent text at 0xFFFF, and the target
	 * inset's is its height and the camera distance divisor. */
	uint16_t color_index_or_widget_param;
	/* Width of a text field's clip in pixels, or the fade amount of a faded
	 * sprite; layouts 104 to 107 hold label widths stored at run time. */
	uint16_t clip_width;
	/* In layout 127, a counter of the critical warning's steps at run
	 * time. */
	int16_t clip_height_or_foreground_color; ///< Widget-specific .INT payload: clip height or foreground text color.
};

struct hud_cockpit_resource {
	/* Handle of the memory holding the entries; 0 when not loaded at
	 * start. */
	int16_t memory_handle;
	/* Cockpit image, viewport span mask, palette (from 2 bytes in). */
	uint8_t *entries[3];
};

struct radar_ellipse_clamp_limit {
	/* Largest sideways offset of a blip, in pixels, in this entry's
	 * 443-unit angle step. */
	uint8_t x_limit;
	/* Largest vertical offset of a blip, in pixels, in this angle step. */
	uint8_t y_limit;
};

extern struct radar_ellipse_clamp_limit g_radar_ellipse_clamp320x240_preset[37];
extern struct radar_ellipse_clamp_limit g_radar_ellipse_clamp_table[37];
extern int g_radar_ellipse_clamp_cached_resolution_mode;
extern int16_t radarx;
extern int16_t radary;
extern uint16_t g_hud_panel_sprite_data_handle;
extern uint16_t g_flight_icon_frames_handle;
extern uint16_t g_message_log_handle;
extern struct hud_cockpit_resource g_hud_cockpit_resources[28];
extern struct hud_cockpit_resource_descriptor
	g_hud_cockpit_resource_descriptors[28];
extern char g_hud_cockpit_resource_path[32];
extern char g_hud_cockpit_base_path[32];
extern struct hud_panel_sprite_file_info g_hud_panel_sprite_file_info;
extern uint8_t g_hud_panel_set_id;
extern uint8_t g_hud_loaded_panel_set_id;
extern uint8_t g_flight_display_rebuild_pending;
extern uint8_t *g_hud_cockpit_resource_write_cursor;
extern int g_hud_cockpit_resources_loaded;
extern int16_t g_hud_cached_target_object_idx;
extern const char *g_str_waypoint_names[14];
extern const char *g_str_mesh_component_names[33];
extern const char *g_str_cmd_threat_display_text[18];
extern const uint8_t g_message_text_prefix_color_codes[16];
extern const uint8_t g_message_sender_iff_color_codes[8];
extern const char *g_str_threat_display_text[4];
extern const uint8_t g_hud_shield_colors[22];
extern uint8_t g_last_shield_damage_side;
extern uint8_t g_flight_conf_tick_counter_enabled;
extern int g_flight_tick_overlay_last_loop_ticks;
extern int g_flight_tick_overlay_window_ticks;
extern int g_flight_tick_overlay_sample_count;
extern int g_packet_drop_indicator;
extern int g_lag_indicator;
extern int g_target_description_message_id;
extern struct hud_in_flight_message_record g_system_message_pane;
extern struct hud_in_flight_message_record g_flight_group_message_pane;
extern struct hud_in_flight_message_record g_ready_message_pane_queue[11];
extern uint8_t g_ready_message_queue_count;
extern int g_radio_message_backup_enabled;
extern uint16_t g_replay_view_mode;
extern int g_system_message_display_enabled;

struct hud_beam_segment_offset {
	uint16_t x; /* Pixels right of layout 51. */
	uint16_t y; /* Pixels below layout 51. */
};

struct hud_radar_blip_point {
	uint16_t x;	/* Screen x in pixels. */
	uint16_t y;	/* Screen y in pixels. */
	uint16_t color; /* Palette index. */
};

extern uint16_t g_radar_blip_color;
extern struct hud_radar_blip_point *g_radar_fore_draw_blips;
extern uint16_t g_radar_fore_blip_count;
extern struct hud_radar_blip_point *g_radar_aft_draw_blips;
extern uint16_t g_radar_aft_blip_count;
extern struct hud_radar_blip_point *g_radar_fore_erase_blips;
extern struct hud_radar_blip_point *g_radar_aft_erase_blips;
extern uint16_t g_radar_fore_prev_blip_count;
extern uint16_t g_radar_aft_prev_blip_count;
extern uint8_t g_radar_blip_buffer_parity;
extern uint8_t g_radar_target_marker_background_saved;
extern uint16_t g_radar_target_marker_restore_x;
extern uint16_t g_radar_target_marker_restore_y;
extern uint16_t g_radar_target_marker_draw_x;
extern uint16_t g_radar_target_marker_draw_y;

struct hud_panel_sprite_file_info {
	char base_name[9]; /* Base name of the cockpit's .PNL file. */
	/* With sprite_count_addend, the number of sprites read from it. */
	uint8_t sprite_count;
	/* Added to sprite_count for the number of sprites read. */
	uint8_t sprite_count_addend;
};

typedef enum cmd_threat_string_id {
	CMD_THREAT_STR_DIST = 0x0,
	CMD_THREAT_STR_SHD = 0x1,
	CMD_THREAT_STR_HULL = 0x2,
	CMD_THREAT_STR_SYS = 0x3,
	CMD_THREAT_STR_TRG = 0x4,
	CMD_THREAT_STR_NO_CARGO = 0x5,
	CMD_THREAT_STR_THIS_CRAFT = 0x6,
	CMD_THREAT_STR_CURRENT_ORDERS = 0x7,
	CMD_THREAT_STR_NONE = 0x8,
	CMD_THREAT_STR_CURRENT_TARGET = 0x9,
	CMD_THREAT_STR_CURRENT_DESTINATION = 0xA,
	CMD_THREAT_STR_DISTANCE_FROM_TARGET = 0xB,
	CMD_THREAT_STR_DISTANCE_TO_DESTINATION = 0xC,
	CMD_THREAT_STR_TIME_REMAINING = 0xD,
	CMD_THREAT_STR_TIME_TO_TARGET = 0xE,
	CMD_THREAT_STR_TIME_TO_DESTINATION = 0xF,
	CMD_THREAT_STR_D = 0x10,
	CMD_THREAT_STR_L = 0x11,
} cmd_threat_string_id;

typedef enum threat_display_string_id {
	THREAT_DISPLAY_STR_LASER = 0x0,
	THREAT_DISPLAY_STR_ION = 0x1,
	THREAT_DISPLAY_STR_WARHEAD = 0x2,
	THREAT_DISPLAY_STR_BEAM = 0x3,
} threat_display_string_id;

/* Stored as int32_t in the binary (IDB enum cockpit_overlay_string_id). */
typedef int32_t cockpit_overlay_string_id;

enum {
	COCKPIT_OVERLAY_STR_SPD = 0x0,
	COCKPIT_OVERLAY_STR_SPEED = 0x1,
	COCKPIT_OVERLAY_STR_THTL = 0x2,
	COCKPIT_OVERLAY_STR_THROTTLE = 0x3,
	COCKPIT_OVERLAY_STR_L = 0x4,
	COCKPIT_OVERLAY_STR_S = 0x5,
	COCKPIT_OVERLAY_STR_E = 0x6,
	COCKPIT_OVERLAY_STR_B = 0x7,
	COCKPIT_OVERLAY_STR_PLAYER = 0x8,
	COCKPIT_OVERLAY_STR_TEAM = 0x9,
	COCKPIT_OVERLAY_STR_SCORE = 0xA,
	COCKPIT_OVERLAY_STR_KILLS = 0xB,
	COCKPIT_OVERLAY_STR_CRAFT = 0xC,
	COCKPIT_OVERLAY_STR_TARGET = 0xD,
	COCKPIT_OVERLAY_STR_ORDERS = 0xE,
	COCKPIT_OVERLAY_STR_INSPECT = 0xF,
	COCKPIT_OVERLAY_STR_DESTROY = 0x10,
	COCKPIT_OVERLAY_STR_DISABLE = 0x11,
	COCKPIT_OVERLAY_STR_ATTACK = 0x12,
	COCKPIT_OVERLAY_STR_CAPTURE = 0x13,
	COCKPIT_OVERLAY_STR_BOARD = 0x14,
	COCKPIT_OVERLAY_STR_BONUS = 0x15,
	COCKPIT_OVERLAY_STR_PENALTY = 0x16,
	COCKPIT_OVERLAY_STR_F = 0x17,
	COCKPIT_OVERLAY_STR_R = 0x18,
	COCKPIT_OVERLAY_STR_EJECT = 0x19,
	COCKPIT_OVERLAY_STR_LARRY = 0x1A,
	COCKPIT_OVERLAY_STR_PETER = 0x1B,
	COCKPIT_OVERLAY_STR_ALBERT = 0x1C,
	COCKPIT_OVERLAY_STR_BRAD = 0x1D,
	COCKPIT_OVERLAY_STR_BUCKY = 0x1E,
	COCKPIT_OVERLAY_STR_JIM = 0x1F,
	COCKPIT_OVERLAY_STR_JAMES = 0x20,
	COCKPIT_OVERLAY_STR_MARK_W = 0x21,
	COCKPIT_OVERLAY_STR_MARK_S = 0x22,
	COCKPIT_OVERLAY_STR_WODEN = 0x23,
	COCKPIT_OVERLAY_STR_MAX = 0x24,
	COCKPIT_OVERLAY_STR_BILL = 0x25,
	COCKPIT_OVERLAY_STR_JOHN = 0x26,
	COCKPIT_OVERLAY_STR_KO = 0x27,
};

extern struct hud_element_layout g_hud_element_layouts[HUD_INSTRUMENT_COUNT];
extern int16_t g_hud_element_state_cache[HUD_INSTRUMENT_COUNT];
extern uint8_t *g_hud_panel_sprite_data_by_index[265];
extern uint8_t *g_hud_panel_sprite_data_write_cursor;
extern uint16_t g_hud_instrument_set_base_index;
extern uint16_t g_mfd_mission_scoreboard_blit_width;
extern uint16_t g_mfd_mission_scoreboard_blit_source_y;
extern uint16_t g_mfd_mission_scoreboard_blit_height;
extern uint16_t g_mfd_mission_scoreboard_blit_source_x;
extern uint16_t g_mfd_map_blit_source_y;
extern uint16_t g_mfd_map_blit_width;
extern uint16_t g_mfd_map_blit_height;
extern uint16_t g_mfd_map_blit_source_x;
extern uint16_t g_mfd_craft_list_blit_height;
extern uint16_t g_mfd_craft_list_blit_source_x;
extern uint16_t g_mfd_craft_list_blit_source_y;
extern uint16_t g_mfd_craft_list_blit_width;
extern uint16_t g_mfd_goals_blit_height;
extern uint16_t g_mfd_damage_blit_source_x;
extern uint16_t g_mfd_goals_blit_source_x;
extern uint16_t g_mfd_damage_blit_height;
extern uint16_t g_mfd_goals_blit_width;
extern uint16_t g_mfd_damage_blit_width;
extern uint16_t g_mfd_damage_blit_source_y;
extern uint16_t g_mfd_goals_blit_source_y;
extern uint8_t g_hud_full_redraw_in_progress;
extern int g_ready_message_pane_left;
extern int g_ready_message_pane_top;
extern int g_ready_message_pane_right;
extern int g_ready_message_pane_bottom;
extern uint8_t g_hud_beam_segment_color_by_charge_step[4];
extern const struct hud_beam_segment_offset
	g_hud_beam_segment_offsets480x360[9];
extern const struct hud_beam_segment_offset
	g_hud_beam_segment_offsets320x240[9];
extern const char g_three_digit_width_text[4];
extern const char g_mission_clock_minutes_width_text[4];
extern const uint8_t g_lfd_palette_resource_type_tag[4];
extern uint8_t g_target_lock_active;
extern uint8_t g_hud_craft_list_inset_span_mask[480];
extern uint8_t g_hud_cockpit_inset_span_mask[480];
extern uint8_t g_hud_only_view_inset_span_mask[480];
extern const char *g_str_cockpit_overlay_text[40];

void hud_draw_box_overlay_hw(int x, int y, int width, int height, int color_idx,
			     int depth);
int hud_set_hud_view_state(int hud_view_state, int player_idx);
void hud_init_hud(int player_idx);
void hud_render_hud(int player_idx);
void hud_draw_hud_target_inset_if_enabled(int player_index);
void hud_draw_static_cockpit_text(uint16_t player_idx);
void hud_init_hud_end_stub(int player_idx);
void hud_update_hud(void);
void hud_update_hud_only_view(void);
void hud_draw_map_view_overlay(void);
void hud_update_cmd_text(void);
void hud_draw_radar_blips(void);
void hud_add_blip_to_radar(int16_t obj_idx);
void hud_update_targeting_computer_display(void);
void hud_format_object_display_name(uint16_t object_ref, int16_t display_flags);
int hud_mission_fg_get_craft_number_if_shown(int flight_group_idx,
					     const struct craft_data *craft);
void hud_draw_target_distance(int polar_distance);
void hud_update_targeting_lock_indicator(void);
void hud_draw_laser_cannon_indicators(void);
void hud_update_warhead_cnt(void);
void hud_output_warhead_count(uint16_t warhead_slot_idx, uint16_t display_slot,
			      uint16_t warhead_bank);
void hud_draw_shield_strength2d(void);
void hud_draw_beam_strength2d(void);
void hud_update_speed_percent(void);
void hud_update_throttle_percent(void);
void hud_update_mission_clock_display(void);
void hud_draw_power_settings2d(void);
void hud_draw_cached_segmented_bar(uint16_t filled_count, uint16_t element_idx,
				   uint16_t segment_count, int16_t y_step);
void hud_update_threat_indicators(int hud_mode);
void hud_update_critical_hull_shield_warning(void);
void hud_update_countermeasure_status(void);
void hud_clear_unavailable_craft_system_indicators(void);
void hud_update_craft_system_status_indicators(void);
void hud_draw_cmd_target_details(void);
void hud_draw_cmd_target_status_indicators(void);
void hud_draw_cached_sprite_element(unsigned int element_idx,
				    unsigned int state);
void hud_draw_cached_faded_sprite_element(uint16_t element_idx, int16_t state,
					  int16_t fade);
void hud_draw_cached_numeric_element(uint16_t element_idx, int16_t value,
				     uint16_t min_digits);
void hud_load_cockpit_resources(void);
void hud_load_cockpit_interface_file(const char *base_path);
int16_t hud_load_auxiliary_cockpit_interface_file(void);
void hud_force_player_view_state(int hud_view_state, int player_idx);
void hud_rebuild_display_for_view_state(int hud_view_state, int player_idx);
void hud_load_cockpit_lfd_entries(const char *lfd_name, uint8_t **out_entries,
				  unsigned int entry_count);
void hud_load_cockpit_sprite_resources(unsigned int model_index);
void hud_reload_cockpit_interface_file(void);
void hud_update_mfd_pages(void);
void hud_blit_software_mfd_pages(void);
void hud_update3d_crt(uint16_t screen_x, uint16_t screen_y, uint16_t width,
		      uint16_t height, int16_t refresh_span_mask);
void hud_draw_component_marker_box(int x, int y, int width, int height,
				   uint8_t color_idx);
void hud_point_camera(uint16_t target_idx, int16_t use_hud_layout_scale,
		      int player_idx);
void hud_reset_flight_message_panes(int force_expire_active_messages);
void hud_shift_ready_message_queue_for_replacement(void);
void hud_advance_ready_message_queue(void);
void hud_show_flight_message_pane(int16_t pane_type);
void hud_setup_ready_message_pane_text(void);
void hud_finish_flight_message_pane(int16_t pane_type, char last_char);
void hud_update_flight_message_panes(void);
void hud_clear_ready_message_queue(void);
void hud_advance_flight_message_pane_timers(void);
void hud_draw_craft_name_fps_and_network_status(void);
uint16_t hud_get_system_message_pane_state(void);
void hud_blit_software_hud_text_panes(void);
uint16_t hud_measure_flight_message_pane_text(int16_t pane_type);
void hud_draw_depth_tested_box_corners(int x, int y, int width, int height,
				       int color_idx, int depth);
int16_t hud_load_panel_sprite_records(const char *file_name,
				      uint16_t first_sprite_index,
				      int16_t sprite_count,
				      uint16_t records_to_skip);
int flight_icon_load_frames(char *file_name, uint8_t *data_buffer,
			    uint8_t **frame_pointers);

#ifdef __cplusplus
}
#endif

#endif
