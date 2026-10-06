#include "xvt/frontend/tech_library.h"

#include <stdlib.h>
#include <string.h>

#include "xvt/assets/file.h"
#include "xvt/assets/model_preview.h"
#include "xvt/flight/craft.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/front_image.h"
#include "xvt/frontend/frontend.h"
#include "xvt/frontend/frontend_button.h"
#include "xvt/frontend/frontend_cursor.h"
#include "xvt/frontend/frontend_dialog.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/frontend_draw.h"
#include "xvt/frontend/frontend_mission.h"
#include "xvt/frontend/frontend_mouse.h"
#include "xvt/frontend/frontend_screen.h"
#include "xvt/frontend/frontend_string.h"
#include "xvt/frontend/frontend_text.h"
#include "xvt/frontend/mission_briefing.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt/input/keyboard.h"
#include "xvt/net/net.h"
#include "xvt_runtime/log/log.h"
#include "xvt_runtime/runtime/dialog_task.h"

/* Heap table of 93 craft descriptions from specdesc.txt, by craft_species value
 * minus 1. tech_library_load_spec_text_table loads it on the craft database's
 * first frame; closing the database, by its Done button or its own button among
 * the common screen controls, frees it and sets NULL, as does
 * xvt_frontend_task_shutdown. */
// GLOBAL: XVT 0xAA6110
struct tech_library_spec_text *g_tech_library_spec_text_table = NULL;

/* Ratings of the craft shown, from build_craft_tech_stats; zeroed and rebuilt on
 * the craft database's first frame and at each change of craft. */
// GLOBAL: XVT 0x665D38
struct craft_tech_stats g_tech_library_craft_stats = {0};
/* Degrees the model turns each frame a rotate button is held: 5. */
// GLOBAL: XVT 0x5182EC
static const float g_tech_library_rotation_step_degrees = 5.0f;
/* A full turn, 360 degrees: a rising angle that reaches it wraps to 0. */
// GLOBAL: XVT 0x518300
static const double g_tech_library_rotation_full_turn_degrees = 360.0;
/* Index in g_ship_list of the craft shown: 0 on the first frame, stepped with
 * wraparound by the Previous craft and Next craft buttons. */
// GLOBAL: XVT 0x665D28
int g_tech_library_selected_ship_list_idx = 0;
/* Light direction x for the model preview, -1, 0 or 1. Set to 1 on the first
 * frame; each press of Change lighting lowers x, and past -1 sets it back to
 * 1 and lowers y the same way, then z. */
// GLOBAL: XVT 0x665D2C
int g_tech_library_light_x = 0;
/* Up-axis angle of the model preview in degrees; set to 0 on the first frame
 * and never changed. */
// GLOBAL: XVT 0x665D30
static float g_tech_library_preview_angle_d = 0.0f;
/* Model yaw in degrees, 225 on the first frame. Holding a mouse button on
 * Rotate X lowers it (left) or raises it (right) by 5 a frame; a value at or
 * under 0 becomes 360, one at or over 360 becomes 0. */
// GLOBAL: XVT 0x665D64
float g_tech_library_preview_yaw_deg = 0.0f;
/* Light direction y, stepped with g_tech_library_light_x. */
// GLOBAL: XVT 0x665D68
int g_tech_library_light_y = 0;
/* Light direction z, stepped with g_tech_library_light_x. */
// GLOBAL: XVT 0x665D6C
int g_tech_library_light_z = 0;
/* Model roll in degrees; set to 0 on the first frame and never changed. */
// GLOBAL: XVT 0x665D70
static float g_tech_library_preview_roll_deg = 0.0f;
/* Model pitch in degrees, 110 on the first frame; Rotate Y changes it the way
 * Rotate X changes g_tech_library_preview_yaw_deg. */
// GLOBAL: XVT 0x665D74
float g_tech_library_preview_pitch_deg = 0.0f;

/* Update function of the craft database, a screen the common screen controls
 * push. On frame 0 it sets the cursor, selection, light and angles to their
 * starting values, loads the ship list and the spec text table, saves the model
 * preview's state while a briefing is active, builds the first craft's ratings
 * and loads its model, raised to world y 100 for a TIE Interceptor or TIE
 * Bomber, and draws frontres\review.bmp with its frame and overlays into the
 * offscreen surface. Every frame it renders the model at the current angles in
 * (280, 107) to (606, 433), draws the title, the spec panel and the pilot
 * banner, and returns 1 when frontend_handle_common_screen_controls(3) returns
 * 1. Then it handles the model controls and the Done button. Done, a network
 * dismiss packet, or as network host a nonzero
 * net_poll_for_player_created_or_backlog closes the screen: it frees the
 * background and spec table, pops the screen, and frees g_ship_list, or with a
 * briefing active restores the preview state instead. Returns 0 on every other
 * path; it stops there while a dialog is up. */
// FUNCTION: XVT 0x4E96B0
int tech_library_update(int frame_counter)
{
	enum {
		TECH_LIBRARY_SCREEN_CONTEXT = 3,
		TECH_LIBRARY_VIEWPORT_LEFT = 280,
		TECH_LIBRARY_VIEWPORT_TOP = 107,
		TECH_LIBRARY_VIEWPORT_RIGHT = 606,
		TECH_LIBRARY_VIEWPORT_BOTTOM = 433,
		TECH_LIBRARY_TITLE_LEFT = 84,
		TECH_LIBRARY_TITLE_TOP = 90,
		TECH_LIBRARY_TITLE_RIGHT = 604,
		TECH_LIBRARY_TITLE_BOTTOM = 106,
		TECH_LIBRARY_DONE_LEFT = 85,
		TECH_LIBRARY_DONE_TOP = 447,
		TECH_LIBRARY_DONE_RIGHT = 176,
		TECH_LIBRARY_DONE_BOTTOM = 471,
		PILOT_BANNER_LEFT = 200,
		PILOT_BANNER_TOP = 452,
		PILOT_BANNER_RIGHT = 436,
		PILOT_BANNER_BOTTOM = 464,
		PILOT_BANNER_REBEL_X = 204,
		PILOT_BANNER_IMPERIAL_X = 420,
		PILOT_BANNER_ICON_Y = 453,
		PILOT_BANNER_ANIMATION_PERIOD_FRAMES = 32,
		DEFAULT_CURSOR_X = 32,
		DEFAULT_CURSOR_Y = 319,
		DEFAULT_PREVIEW_PITCH_DEGREES = 110,
		DEFAULT_PREVIEW_YAW_DEGREES = 225,
		SPECIAL_CRAFT_WORLD_Y = 100,
		BUTTON_FONT_SIZE = 12,
		TITLE_FONT_SIZE = 15,
		DONE_BUTTON_HELD_SLOT = 8,
	};

	if (frame_counter == 0) {
		frontend_cursor_set_pos(DEFAULT_CURSOR_X, DEFAULT_CURSOR_Y);
		g_tech_library_selected_ship_list_idx = 0;
		g_tech_library_preview_roll_deg = 0.0f;
		g_tech_library_light_x = 1;
		g_tech_library_light_y = 1;
		g_tech_library_light_z = 1;
		g_tech_library_preview_angle_d = 0.0f;
		g_tech_library_preview_pitch_deg =
			(float)DEFAULT_PREVIEW_PITCH_DEGREES;
		g_tech_library_preview_yaw_deg =
			(float)DEFAULT_PREVIEW_YAW_DEGREES;
		ship_list_load();
		tech_library_load_spec_text_table();
		if (g_ship_list == NULL || g_ship_count <= 0) {
			XVT_LOG_ERROR("tech.ship_list_empty count=%d listed=%d",
				      g_ship_count, g_ship_list != NULL);
		}
		if (g_mission_briefing_craft_selection_active != 0) {
			model_preview_save_state();
		}
		memset(&g_tech_library_craft_stats, 0,
		       sizeof(g_tech_library_craft_stats));
		g_tech_library_craft_stats.craft_type =
			g_ship_list[g_tech_library_selected_ship_list_idx]
				.craft_type;
		build_craft_tech_stats(&g_tech_library_craft_stats);
		if (g_tech_library_craft_stats.craft_type > 93) {
			XVT_LOG_WARN("tech.spec_index_over craft=%d index=%d",
				     g_tech_library_craft_stats.craft_type,
				     g_tech_library_selected_ship_list_idx);
		}
		model_preview_load_model(
			g_ship_list[g_tech_library_selected_ship_list_idx]
				.model_file_name);
		XVT_LOG_DEBUG(
			"tech.craft_shown by=\"open\" index=%d count=%d craft=%d model=\"%.63s\"",
			g_tech_library_selected_ship_list_idx, g_ship_count,
			g_tech_library_craft_stats.craft_type,
			g_ship_list[g_tech_library_selected_ship_list_idx]
				.model_file_name);
		model_preview_set_light_direction(g_tech_library_light_x,
						  g_tech_library_light_y,
						  g_tech_library_light_z);
		int selected_craft_type =
			g_ship_list[g_tech_library_selected_ship_list_idx]
				.craft_type;
		if (selected_craft_type == CRAFT_SPECIES_TIE_INTERCEPTOR ||
		    selected_craft_type == CRAFT_SPECIES_TIE_BOMBER) {
			model_preview_set_object_world_position(
				0, SPECIAL_CRAFT_WORLD_Y, 0);
		} else {
			model_preview_set_object_world_position(0, 0, 0);
		}
		model_preview_set_node_switch_index(0);
		front_image_register_resource_default("frontres\\review.bmp",
						      "backreview");
		frontend_display_lock_offscreen_surface();
		front_image_draw_sprite_opaque("backreview", 0, 0);
		front_image_draw_sprite("frame", 0, 0);
		front_image_draw_sprite("alloff", 0, 0);
		front_image_draw_sprite_translucent("configoverlay", 0, 0);
		frontend_display_unlock_offscreen_surface(1);
		frontend_text_start_text_fade_in(20);
		XVT_LOG_INFO("tech.opened ships=%d from_briefing=%d spec=%d",
			     g_ship_count,
			     g_mission_briefing_craft_selection_active,
			     g_tech_library_spec_text_table != NULL);
	}

	struct RECT rect;
	frontend_draw_rect_assign(
		&rect, TECH_LIBRARY_VIEWPORT_LEFT, TECH_LIBRARY_VIEWPORT_TOP,
		TECH_LIBRARY_VIEWPORT_RIGHT, TECH_LIBRARY_VIEWPORT_BOTTOM);
	model_preview_set_object_euler_degrees(g_tech_library_preview_pitch_deg,
					       g_tech_library_preview_yaw_deg,
					       g_tech_library_preview_roll_deg);
	model_preview_set_object_up_axis_angle_degrees(
		g_tech_library_preview_angle_d);
	model_preview_render_viewport(rect.left, rect.top,
				      rect.right - rect.left + 1,
				      rect.bottom - rect.top + 1, NULL);
	frontend_draw_rect_assign(
		&rect, TECH_LIBRARY_TITLE_LEFT, TECH_LIBRARY_TITLE_TOP,
		TECH_LIBRARY_TITLE_RIGHT, TECH_LIBRARY_TITLE_BOTTOM);
	frontend_text_draw_centered(
		TITLE_FONT_SIZE,
		frontend_string_get(FRONTSTR_001_CRAFT_DATABASE), &rect,
		0xFFFF);
	tech_library_draw_craft_spec_panel();

	frontend_draw_rect_assign(&rect, PILOT_BANNER_LEFT, PILOT_BANNER_TOP,
				  PILOT_BANNER_RIGHT, PILOT_BANNER_BOTTOM);
	if (g_pilot_data.name[0] != '\0') {
		sprintf(g_frontend_scratch_buffer, "%c%s %c%s", 6,
			g_pilot_data.rating_name, 1, g_pilot_data.name);
		frontend_text_draw_centered(BUTTON_FONT_SIZE,
					    g_frontend_scratch_buffer, &rect,
					    g_color_yellow);
		int animation_frame =
			frame_counter % PILOT_BANNER_ANIMATION_PERIOD_FRAMES;
		animation_frame >>= 1;
		sprintf(g_frontend_scratch_buffer, "rebtiny%d",
			animation_frame);
		front_image_draw_sprite(g_frontend_scratch_buffer,
					PILOT_BANNER_REBEL_X,
					PILOT_BANNER_ICON_Y);
		sprintf(g_frontend_scratch_buffer, "imptiny%d",
			animation_frame);
		front_image_draw_sprite(g_frontend_scratch_buffer,
					PILOT_BANNER_IMPERIAL_X,
					PILOT_BANNER_ICON_Y);
	}

	if (frontend_handle_common_screen_controls(
		    TECH_LIBRARY_SCREEN_CONTEXT) == 1) {
		return 1;
	}
	if (xvt_dialog_is_active()) {
		return 0;
	}
	tech_library_update_model_controls();
	frontend_draw_rect_assign(
		&rect, TECH_LIBRARY_DONE_LEFT, TECH_LIBRARY_DONE_TOP,
		TECH_LIBRARY_DONE_RIGHT, TECH_LIBRARY_DONE_BOTTOM);
	if (g_game_config.help_on != 0) {
		frontend_button_enable_overlay_text();
		frontend_button_set_overlay_text(
			frontend_string_get(FRONTSTR_206_DONE));
	}
	int button_pressed = frontend_button_handle_sprite_button(
		&rect, "leaveup", "leavedown",
		frontend_string_get(FRONTSTR_206_DONE), BUTTON_FONT_SIZE, 0,
		DONE_BUTTON_HELD_SLOT, "buttonsound");
	frontend_button_disable_overlay_text();
	button_pressed |= frontend_dialog_has_network_dismiss_packet();
	if (g_frontend_mission_session_mode ==
	    FRONTEND_MISSION_SESSION_NET_HOST) {
		button_pressed |= net_poll_for_player_created_or_backlog();
	}
	if (button_pressed != 0) {
		XVT_LOG_DEBUG("tech.closed by=\"done\" from_briefing=%d",
			      g_mission_briefing_craft_selection_active);
		front_image_free_resource_by_name("backreview");
		keyboard_flush_char_buffer();
		frontend_screen_pop_state();
		if (g_mission_briefing_craft_selection_active == 0) {
			if (g_ship_list != NULL) {
				free(g_ship_list);
				g_ship_list = NULL;
			}
		} else {
			model_preview_restore_state();
		}
		if (g_tech_library_spec_text_table != NULL) {
			free(g_tech_library_spec_text_table);
			g_tech_library_spec_text_table = NULL;
		}
		frontend_text_stop_text_fade();
	}
	return 0;
}

/* Handles the craft database's left-hand buttons. Marks navigation slots 0
 * to 2 and 5 to 6 as selected while the mouse is on them with a button down
 * or clicked, and draws the eight slots. Change lighting steps the light
 * direction (g_tech_library_light_x); holding a mouse button on Rotate X or
 * Rotate Y turns the model's yaw or pitch; Previous craft and Next craft
 * step g_tech_library_selected_ship_list_idx with wraparound, rebuild
 * g_tech_library_craft_stats and load the model, raised to world y 100 for a TIE
 * Interceptor or TIE Bomber. Returns 1. The light steps reach (0, 0, 0), a
 * direction with no length, which model_preview_set_light_direction divides by. */
// FUNCTION: XVT 0x4E9AF0
int tech_library_update_model_controls(void)
{
	enum {
		NAVIGATION_SLOT_COUNT = 8,
		TOP_BUTTON_COUNT = 3,
		BOTTOM_BUTTON_FIRST = 5,
		BOTTOM_BUTTON_END = 7,
		BUTTON_SPACING = 28,
		BUTTON_FONT_SIZE = 12,
		LIGHTING_HELD_SLOT = 13,
		ROTATE_X_HELD_SLOT = 12,
		ROTATE_Y_HELD_SLOT = 11,
		PREVIOUS_CRAFT_HELD_SLOT = 17,
		NEXT_CRAFT_HELD_SLOT = 16,
		SPECIAL_CRAFT_WORLD_Y = 100,
	};

	int mouse_y;
	int mouse_x;

	frontend_cursor_get_pos(&mouse_x, &mouse_y);
	struct RECT rect;
	frontend_draw_rect_assign(&rect, 22, 114, 42, 138);
	frontend_cursor_get_pos(&mouse_x, &mouse_y);
	frontend_navigation_slot_state slot_states[NAVIGATION_SLOT_COUNT] = {
		FRONTEND_NAVIGATION_SLOT_ACTIVE,
		FRONTEND_NAVIGATION_SLOT_ACTIVE,
		FRONTEND_NAVIGATION_SLOT_ACTIVE,
		FRONTEND_NAVIGATION_SLOT_INACTIVE,
		FRONTEND_NAVIGATION_SLOT_INACTIVE,
		FRONTEND_NAVIGATION_SLOT_ACTIVE,
		FRONTEND_NAVIGATION_SLOT_ACTIVE,
		FRONTEND_NAVIGATION_SLOT_INACTIVE,
	};
	int slot_index;
	for (slot_index = 0; slot_index < TOP_BUTTON_COUNT; ++slot_index) {
		if (slot_states[slot_index] !=
			    FRONTEND_NAVIGATION_SLOT_INACTIVE &&
		    frontend_draw_point_in_rect(&rect, mouse_x, mouse_y) != 0 &&
		    (frontend_mouse_get_left_down() != 0 ||
		     frontend_mouse_get_right_down() != 0 ||
		     frontend_mouse_get_left_click() != 0 ||
		     frontend_mouse_get_right_click() != 0)) {
			slot_states[slot_index] =
				FRONTEND_NAVIGATION_SLOT_SELECTED;
		}
		frontend_draw_rect_offset_xy(&rect, 0, BUTTON_SPACING);
	}

	frontend_draw_rect_assign(&rect, 22, 306, 42, 330);
	for (slot_index = BOTTOM_BUTTON_FIRST; slot_index < BOTTOM_BUTTON_END;
	     ++slot_index) {
		if (slot_states[slot_index] !=
			    FRONTEND_NAVIGATION_SLOT_INACTIVE &&
		    frontend_draw_point_in_rect(&rect, mouse_x, mouse_y) != 0 &&
		    (frontend_mouse_get_left_down() != 0 ||
		     frontend_mouse_get_right_down() != 0 ||
		     frontend_mouse_get_left_click() != 0 ||
		     frontend_mouse_get_right_click() != 0)) {
			slot_states[slot_index] =
				FRONTEND_NAVIGATION_SLOT_SELECTED;
		}
		frontend_draw_rect_offset_xy(&rect, 0, BUTTON_SPACING);
	}
	frontend_button_draw_eight_slot_navigation_state(slot_states);

	frontend_draw_rect_assign(&rect, 22, 170, 42, 194);
	if (frontend_button_handle_sprite_button(
		    &rect, "review3u", "review3d",
		    frontend_string_get(FRONTSTR_294_CHANGE_LIGHTING),
		    BUTTON_FONT_SIZE, 0, LIGHTING_HELD_SLOT,
		    "jewelsound") != 0) {
		--g_tech_library_light_x;
		if (g_tech_library_light_x == -2) {
			--g_tech_library_light_y;
			g_tech_library_light_x = 1;
			if (g_tech_library_light_y == -2) {
				g_tech_library_light_y = 1;
				--g_tech_library_light_z;
				if (g_tech_library_light_z == -2) {
					g_tech_library_light_z = 1;
				}
			}
		}
		XVT_LOG_DEBUG("tech.light_changed x=%d y=%d z=%d",
			      g_tech_library_light_x, g_tech_library_light_y,
			      g_tech_library_light_z);
		if (g_tech_library_light_x == 0 &&
		    g_tech_library_light_y == 0 &&
		    g_tech_library_light_z == 0) {
			XVT_LOG_WARN("tech.light_zero");
		}
		model_preview_set_light_direction(g_tech_library_light_x,
						  g_tech_library_light_y,
						  g_tech_library_light_z);
	}

	frontend_draw_rect_offset_xy(&rect, 0, -BUTTON_SPACING);
	frontend_button_handle_sprite_button(
		&rect, "review2u", "review2d",
		frontend_string_get(FRONTSTR_292_ROTATE_X), BUTTON_FONT_SIZE, 0,
		ROTATE_X_HELD_SLOT, "jewelsound");
	if (frontend_draw_point_in_rect(&rect, mouse_x, mouse_y) != 0) {
		if (frontend_mouse_get_left_down() != 0) {
			g_tech_library_preview_yaw_deg -=
				g_tech_library_rotation_step_degrees;
			if (g_tech_library_preview_yaw_deg <= 0.0) {
				g_tech_library_preview_yaw_deg = 360.0f;
			}
		} else if (frontend_mouse_get_right_down() != 0) {
			g_tech_library_preview_yaw_deg +=
				g_tech_library_rotation_step_degrees;
			if (g_tech_library_preview_yaw_deg >=
			    g_tech_library_rotation_full_turn_degrees) {
				g_tech_library_preview_yaw_deg = 0.0f;
			}
		}
	}

	frontend_draw_rect_offset_xy(&rect, 0, -BUTTON_SPACING);
	frontend_button_handle_sprite_button(
		&rect, "review1u", "review1d",
		frontend_string_get(FRONTSTR_293_ROTATE_Y), BUTTON_FONT_SIZE, 0,
		ROTATE_Y_HELD_SLOT, "jewelsound");
	if (frontend_draw_point_in_rect(&rect, mouse_x, mouse_y) != 0) {
		if (frontend_mouse_get_left_down() != 0) {
			g_tech_library_preview_pitch_deg -=
				g_tech_library_rotation_step_degrees;
			if (g_tech_library_preview_pitch_deg <= 0.0) {
				g_tech_library_preview_pitch_deg = 360.0f;
			}
		} else if (frontend_mouse_get_right_down() != 0) {
			g_tech_library_preview_pitch_deg +=
				g_tech_library_rotation_step_degrees;
			if (g_tech_library_preview_pitch_deg >=
			    g_tech_library_rotation_full_turn_degrees) {
				g_tech_library_preview_pitch_deg = 0.0f;
			}
		}
	}

	frontend_draw_rect_assign(&rect, 22, 334, 42, 358);
	int selected_craft_type;
	if (frontend_button_handle_sprite_button(
		    &rect, "review7u", "review7d",
		    frontend_string_get(FRONTSTR_296_PREVIOUS_CRAFT),
		    BUTTON_FONT_SIZE, 0, PREVIOUS_CRAFT_HELD_SLOT,
		    "jewelsound") != 0) {
		--g_tech_library_selected_ship_list_idx;
		if (g_tech_library_selected_ship_list_idx < 0) {
			g_tech_library_selected_ship_list_idx =
				g_ship_count - 1;
		}
		memset(&g_tech_library_craft_stats, 0,
		       sizeof(g_tech_library_craft_stats));
		g_tech_library_craft_stats.craft_type =
			g_ship_list[g_tech_library_selected_ship_list_idx]
				.craft_type;
		build_craft_tech_stats(&g_tech_library_craft_stats);
		if (g_tech_library_craft_stats.craft_type > 93) {
			XVT_LOG_WARN("tech.spec_index_over craft=%d index=%d",
				     g_tech_library_craft_stats.craft_type,
				     g_tech_library_selected_ship_list_idx);
		}
		model_preview_load_model(
			g_ship_list[g_tech_library_selected_ship_list_idx]
				.model_file_name);
		XVT_LOG_DEBUG(
			"tech.craft_shown by=\"previous\" index=%d count=%d craft=%d model=\"%.63s\"",
			g_tech_library_selected_ship_list_idx, g_ship_count,
			g_tech_library_craft_stats.craft_type,
			g_ship_list[g_tech_library_selected_ship_list_idx]
				.model_file_name);
		selected_craft_type =
			g_ship_list[g_tech_library_selected_ship_list_idx]
				.craft_type;
		if (selected_craft_type == CRAFT_SPECIES_TIE_INTERCEPTOR ||
		    selected_craft_type == CRAFT_SPECIES_TIE_BOMBER) {
			model_preview_set_object_world_position(
				0, SPECIAL_CRAFT_WORLD_Y, 0);
		} else {
			model_preview_set_object_world_position(0, 0, 0);
		}
	}

	frontend_draw_rect_offset_xy(&rect, 0, -BUTTON_SPACING);
	if (frontend_button_handle_sprite_button(
		    &rect, "review6u", "review6d",
		    frontend_string_get(FRONTSTR_295_NEXT_CRAFT),
		    BUTTON_FONT_SIZE, 0, NEXT_CRAFT_HELD_SLOT,
		    "jewelsound") != 0) {
		++g_tech_library_selected_ship_list_idx;
		if (g_ship_count <= g_tech_library_selected_ship_list_idx) {
			g_tech_library_selected_ship_list_idx = 0;
		}
		memset(&g_tech_library_craft_stats, 0,
		       sizeof(g_tech_library_craft_stats));
		g_tech_library_craft_stats.craft_type =
			g_ship_list[g_tech_library_selected_ship_list_idx]
				.craft_type;
		build_craft_tech_stats(&g_tech_library_craft_stats);
		if (g_tech_library_craft_stats.craft_type > 93) {
			XVT_LOG_WARN("tech.spec_index_over craft=%d index=%d",
				     g_tech_library_craft_stats.craft_type,
				     g_tech_library_selected_ship_list_idx);
		}
		model_preview_load_model(
			g_ship_list[g_tech_library_selected_ship_list_idx]
				.model_file_name);
		XVT_LOG_DEBUG(
			"tech.craft_shown by=\"next\" index=%d count=%d craft=%d model=\"%.63s\"",
			g_tech_library_selected_ship_list_idx, g_ship_count,
			g_tech_library_craft_stats.craft_type,
			g_ship_list[g_tech_library_selected_ship_list_idx]
				.model_file_name);
		selected_craft_type =
			g_ship_list[g_tech_library_selected_ship_list_idx]
				.craft_type;
		if (selected_craft_type != CRAFT_SPECIES_TIE_INTERCEPTOR &&
		    selected_craft_type != CRAFT_SPECIES_TIE_BOMBER) {
			model_preview_set_object_world_position(0, 0, 0);
			return 1;
		}
		model_preview_set_object_world_position(
			0, SPECIAL_CRAFT_WORLD_Y, 0);
	}
	return 1;
}

/* Draws the spec panel for the craft in g_tech_library_craft_stats, from its entry
 * in g_tech_library_spec_text_table (craft type minus 1, at least 0), in font 12:
 * the name at (88, 111), then from 30 pixels lower, in rows 15 apart, the
 * designation by genus, manufacturer, users, and the description under a yellow
 * heading. Then, for a starfighter, speed, acceleration, maneuverability, guns
 * and warhead load; for any genus but mine and satellite, the model's size in
 * meters, or kilometers at 1000 meters and over, and the crew; and for all,
 * shield and hull ratings. Returns 1. Checks neither that the table is loaded
 * nor that the entry is under 93. */
// FUNCTION: XVT 0x4EA090
int tech_library_draw_craft_spec_panel(void)
{
	struct RECT rect;

	frontend_draw_rect_assign(&rect, 88, 111, 332, 125);
	int craft_spec_index = g_tech_library_craft_stats.craft_type - 1;
	if (craft_spec_index < 0) {
		craft_spec_index = 0;
	}
	sprintf(g_frontend_scratch_buffer, "%s",
		g_tech_library_spec_text_table[craft_spec_index].craft_name);
	frontend_text_draw_aligned_in_rect(12, g_frontend_scratch_buffer, &rect,
					   0, 1, 0xFFFF);

	frontend_draw_rect_offset_xy(&rect, 0, 30);
	const char *label = frontend_string_get(
		(frontend_string_id)(FRONTSTR_496_STARFIGHTER +
				     g_tech_library_craft_stats.genus_id));
	sprintf(g_frontend_scratch_buffer, "%c%s %c%s", 4,
		frontend_string_get(FRONTSTR_483_DESIGNATION), 1, label);
	frontend_text_draw_aligned_in_rect(12, g_frontend_scratch_buffer, &rect,
					   0, 1, 0xFFFF);

	frontend_draw_rect_offset_xy(&rect, 0, 15);
	sprintf(g_frontend_scratch_buffer, "%c%s %c%s", 4,
		frontend_string_get(FRONTSTR_484_MANUFACTURER), 1,
		g_tech_library_spec_text_table[craft_spec_index].manufacturer);
	frontend_text_draw_aligned_in_rect(12, g_frontend_scratch_buffer, &rect,
					   0, 1, 0xFFFF);

	frontend_draw_rect_offset_xy(&rect, 0, 15);
	sprintf(g_frontend_scratch_buffer, "%c%s %c%s", 4,
		frontend_string_get(FRONTSTR_485_IN_USE_BY), 1,
		g_tech_library_spec_text_table[craft_spec_index].in_use_by);
	frontend_text_draw_aligned_in_rect(12, g_frontend_scratch_buffer, &rect,
					   0, 1, 0xFFFF);

	frontend_draw_rect_offset_xy(&rect, 0, 15);
	frontend_text_draw_aligned_in_rect(
		12, frontend_string_get(FRONTSTR_486_SPECIAL_CHARACTERISTICS),
		&rect, 0, 1, g_color_yellow);
	frontend_draw_rect_offset_xy(&rect, 0, 15);
	struct RECT description_rect;
	frontend_draw_rect_copy(&description_rect, &rect);
	description_rect.bottom = description_rect.top + 120;
	description_rect.left += 20;
	int text_lines = frontend_text_draw_wrapped(
		12,
		g_tech_library_spec_text_table[craft_spec_index].description,
		&description_rect, 0xFFFF, 3, 0);
	frontend_draw_rect_offset_xy(&rect, 0, 5 * (3 * text_lines + 6));

	if (g_tech_library_craft_stats.genus_id == CRAFT_GENUS_STARFIGHTER) {
		sprintf(g_frontend_scratch_buffer, "%c%s %c%d %s", 4,
			frontend_string_get(FRONTSTR_487_SPEED), 1,
			g_tech_library_craft_stats.speed_rating,
			frontend_string_get(FRONTSTR_513_MGLT));
		frontend_text_draw_aligned_in_rect(
			12, g_frontend_scratch_buffer, &rect, 0, 1, 0xFFFF);
		frontend_draw_rect_offset_xy(&rect, 0, 15);
		sprintf(g_frontend_scratch_buffer, "%c%s %c%d %s", 4,
			frontend_string_get(FRONTSTR_488_ACCELERATION), 1,
			g_tech_library_craft_stats.acceleration_rating,
			frontend_string_get(FRONTSTR_514_MGLT_SECOND));
		frontend_text_draw_aligned_in_rect(
			12, g_frontend_scratch_buffer, &rect, 0, 1, 0xFFFF);
		frontend_draw_rect_offset_xy(&rect, 0, 15);
		sprintf(g_frontend_scratch_buffer, "%c%s %c%d %s", 4,
			frontend_string_get(
				FRONTSTR_489_MANUEVERABILITY_RATING),
			1, g_tech_library_craft_stats.maneuver_rating,
			frontend_string_get(FRONTSTR_714_DPF));
		frontend_text_draw_aligned_in_rect(
			12, g_frontend_scratch_buffer, &rect, 0, 1, 0xFFFF);
		frontend_draw_rect_offset_xy(&rect, 0, 15);
		sprintf(g_frontend_scratch_buffer, "%c%s %c", 4,
			frontend_string_get(FRONTSTR_490_LASERS), 1);
		if (g_tech_library_craft_stats.laser_count != 0) {
			strcat(g_frontend_scratch_buffer,
			       frontend_string_get((
				       frontend_string_id)(g_tech_library_craft_stats
								   .laser_count +
							   517)));
			strcat(g_frontend_scratch_buffer, " ");
			strcat(g_frontend_scratch_buffer,
			       frontend_string_get(FRONTSTR_516_LASERS));
			if (g_tech_library_craft_stats.ion_count != 0) {
				strcat(g_frontend_scratch_buffer, " ");
				strcat(g_frontend_scratch_buffer,
				       frontend_string_get(FRONTSTR_515_AND));
				strcat(g_frontend_scratch_buffer, " ");
			}
		}
		if (g_tech_library_craft_stats.ion_count != 0) {
			strcat(g_frontend_scratch_buffer,
			       frontend_string_get((
				       frontend_string_id)(g_tech_library_craft_stats
								   .ion_count +
							   517)));
			strcat(g_frontend_scratch_buffer, " ");
			strcat(g_frontend_scratch_buffer,
			       frontend_string_get(FRONTSTR_517_ION_CANNONS));
		}
		frontend_text_draw_aligned_in_rect(
			12, g_frontend_scratch_buffer, &rect, 0, 1, 0xFFFF);
		frontend_draw_rect_offset_xy(&rect, 0, 15);
		sprintf(g_frontend_scratch_buffer, "%c%s %c%d", 4,
			frontend_string_get(
				FRONTSTR_491_STD_COMBAT_WARHEAD_LOAD),
			1, g_tech_library_craft_stats.warhead_rating);
		frontend_text_draw_aligned_in_rect(
			12, g_frontend_scratch_buffer, &rect, 0, 1, 0xFFFF);
		frontend_draw_rect_offset_xy(&rect, 0, 15);
	} else if (g_tech_library_craft_stats.genus_id != CRAFT_GENUS_MINE &&
		   g_tech_library_craft_stats.genus_id !=
			   CRAFT_GENUS_SATELLITE) {
		const double displayed_size =
			(double)model_preview_get_displayed_size_meters();
		const double size_value = displayed_size >= 1000.0
						  ? displayed_size * 0.001
						  : displayed_size;
		const frontend_string_id size_unit =
			displayed_size >= 1000.0 ? FRONTSTR_522_KM
						 : FRONTSTR_712_METERS;

		sprintf(g_frontend_scratch_buffer, "%c%s %c%.1f %s", 4,
			frontend_string_get(FRONTSTR_492_SIZE), 1, size_value,
			frontend_string_get(size_unit));
		frontend_text_draw_aligned_in_rect(
			12, g_frontend_scratch_buffer, &rect, 0, 1, 0xFFFF);
		frontend_draw_rect_offset_xy(&rect, 0, 15);
		sprintf(g_frontend_scratch_buffer, "%c%s %c%s", 4,
			frontend_string_get(FRONTSTR_493_CREW), 1,
			g_tech_library_spec_text_table[craft_spec_index].crew);
		frontend_text_draw_aligned_in_rect(
			12, g_frontend_scratch_buffer, &rect, 0, 1, 0xFFFF);
		frontend_draw_rect_offset_xy(&rect, 0, 15);
	}

	sprintf(g_frontend_scratch_buffer, "%c%s %c%d %s", 4,
		frontend_string_get(FRONTSTR_494_SHIELD_RATING), 1,
		g_tech_library_craft_stats.shield_rating,
		frontend_string_get(FRONTSTR_715_SBD));
	frontend_text_draw_aligned_in_rect(12, g_frontend_scratch_buffer, &rect,
					   0, 1, 0xFFFF);
	frontend_draw_rect_offset_xy(&rect, 0, 15);
	sprintf(g_frontend_scratch_buffer, "%c%s %c%d %s", 4,
		frontend_string_get(FRONTSTR_495_HULL_RATING), 1,
		g_tech_library_craft_stats.hull_rating,
		frontend_string_get(FRONTSTR_713_RU));
	frontend_text_draw_aligned_in_rect(12, g_frontend_scratch_buffer, &rect,
					   0, 1, 0xFFFF);
	frontend_draw_rect_offset_xy(&rect, 0, 15);
	return 1;
}

/* Loads specdesc.txt into a new 93-entry g_tech_library_spec_text_table,
 * freeing the old one. Each entry is five lines, skipping lines that start with
 * "//": name, manufacturer, users, description and crew, each cut to 255
 * characters, without its newline, and copied with strncpy, which leaves no
 * terminator when a line fills its field. Returns 0, keeping the old table,
 * when the file does not open, and 0 when the allocation fails; 1 when the file
 * ends before 93 entries. After all 93, it returns what file_close returns. */
// FUNCTION: XVT 0x4EA8C0
int tech_library_load_spec_text_table(void)
{
	xvt_file *stream = file_open("specdesc.txt", "r");
	if (stream == NULL) {
		XVT_LOG_ERROR("tech.spec_open_failed file=\"specdesc.txt\"");
		return 0;
	}

	if (g_tech_library_spec_text_table != NULL) {
		free(g_tech_library_spec_text_table);
		g_tech_library_spec_text_table = NULL;
	}

	g_tech_library_spec_text_table =
		(struct tech_library_spec_text *)malloc(
			sizeof(*g_tech_library_spec_text_table) * 93u);
	if (g_tech_library_spec_text_table == NULL) {
		XVT_LOG_ERROR(
			"tech.spec_alloc_failed bytes=%u",
			(unsigned)(sizeof(*g_tech_library_spec_text_table) *
				   93u));
		file_close(stream);
		return 0;
	}

	memset(g_tech_library_spec_text_table, 0,
	       sizeof(*g_tech_library_spec_text_table) * 93u);
	int field_index;
	size_t length;
	for (int entry_index = 0; entry_index < 93; ++entry_index) {
		field_index = 0;
		while (field_index < 5) {
			do {
				if (FILE_GETS(g_frontend_scratch_buffer,
					      sizeof(g_frontend_scratch_buffer),
					      stream) == NULL) {
					XVT_LOG_DEBUG(
						"tech.spec_loaded entries=%d",
						entry_index);
					if (field_index != 0 &&
					    (field_index > 1 ||
					     g_tech_library_spec_text_table
							     [entry_index]
								     .craft_name
									     [0] !=
						     '\0')) {
						XVT_LOG_WARN(
							"tech.spec_entry_cut entry=%d field=%d",
							entry_index,
							field_index);
					}
					file_close(stream);
					return 1;
				}
				g_frontend_scratch_buffer[255] = '\0';
			} while (g_frontend_scratch_buffer[0] == '/' &&
				 g_frontend_scratch_buffer[1] == '/');

			length = strlen(g_frontend_scratch_buffer);
			if (g_frontend_scratch_buffer[length - 1] == '\n') {
				g_frontend_scratch_buffer[length - 1] = '\0';
			}

			switch (field_index) {
			case 0:
				strncpy(g_tech_library_spec_text_table
						[entry_index]
							.craft_name,
					g_frontend_scratch_buffer, 64u);
				break;
			case 1:
				strncpy(g_tech_library_spec_text_table
						[entry_index]
							.manufacturer,
					g_frontend_scratch_buffer, 64u);
				break;
			case 2:
				strncpy(g_tech_library_spec_text_table
						[entry_index]
							.in_use_by,
					g_frontend_scratch_buffer, 64u);
				break;
			case 3:
				strncpy(g_tech_library_spec_text_table
						[entry_index]
							.description,
					g_frontend_scratch_buffer, 256u);
				break;
			case 4:
				strncpy(g_tech_library_spec_text_table
						[entry_index]
							.crew,
					g_frontend_scratch_buffer, 64u);
				break;
			}
			++field_index;
		}
	}
	XVT_LOG_DEBUG("tech.spec_loaded entries=%d", 93);

	return file_close(stream);
}
