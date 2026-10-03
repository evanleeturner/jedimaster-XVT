#include "xvt/frontend/config.h"
#ifdef XVT_MODERN
#include "xvt_runtime/runtime/cd_task.h"
#include "xvt_runtime/runtime/dialog_task.h"
#include "xvt_runtime/runtime/frontend_actions.h"
#include "xvt_runtime/runtime/port.h"
#endif
#ifdef XVT_MODERN
#include "xvt_runtime/config/config.h"
#endif
#include "xvt/assets/file.h"
#include "xvt/audio/cd_audio.h"
#include "xvt/audio/frontend_sound.h"
#include "xvt/frontend/concourse.h"
#include "xvt/frontend/credits.h"
#include "xvt/frontend/front_image.h"
#include "xvt/frontend/frontend.h"
#include "xvt/frontend/frontend_button.h"
#include "xvt/frontend/frontend_cursor.h"
#include "xvt/frontend/frontend_dialog.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/frontend_draw.h"
#include "xvt/frontend/frontend_joystick.h"
#include "xvt/frontend/frontend_mission.h"
#include "xvt/frontend/frontend_mouse.h"
#include "xvt/frontend/frontend_screen.h"
#include "xvt/frontend/frontend_scrollbar.h"
#include "xvt/frontend/frontend_string.h"
#include "xvt/frontend/frontend_text.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt/input/keyboard.h"
#include "xvt/net/frontend_net.h"
#include "xvt/net/net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The joystick action list read from joystick.txt: each entry's action code,
 * name and description. config_load_joystick_action_dictionary fills it when the
 * config screen opens. */
// GLOBAL: XVT 0xBB2820
struct joystick_entry g_joystick_entries[128] = {0};
/* Entries in g_joystick_entries; config_load_joystick_action_dictionary sets it. */
// GLOBAL: XVT 0xBB72A0
int g_joystick_entry_count = 0;
/* First action shown in the joystick page's list of keys: 0 when the config
 * screen opens, then set by the list's scrollbar and by picking an action from
 * the keyboard. */
// GLOBAL: XVT 0x664E88
int g_config_joystick_action_scroll_offset = 0;
/* First row shown in the joystick page's button list: 0 when the config screen
 * opens, then set by the list's scrollbar and moved to a pressed button or hat
 * direction that is out of view. */
// GLOBAL: XVT 0x664E8C
int g_config_joystick_button_scroll_offset = 0;
/* Button being remapped: 0 to 15 for joystick buttons 1 to 16, 16 to 19 for the
 * hat directions. 0 when the config screen opens; set by picking a row or
 * pressing a button or hat direction. */
// GLOBAL: XVT 0x664E90
int g_config_selected_joystick_button_index = 0;
/* Index in g_joystick_entries of the action the selected button is mapped to, as
 * the joystick page shows it. */
// GLOBAL: XVT 0x664E94
int g_config_selected_joystick_action_index = 0;

/* The game's settings. config_load fills them from defaults and the config file
 * and config_write saves them; the config pages, the mission setup screen, the
 * host's game options packets and the command line change them, and many
 * functions read them. */
// GLOBAL: XVT 0xBB72B0
struct game_config g_game_config = {0};

/* 1 while a config page should also draw its controls' fixed images into the
 * offscreen background: config_options_datapad_update sets 1 on its first frame
 * and 0 after every frame, and the page buttons set 1 when they switch
 * pages. */
// GLOBAL: XVT 0x664E98
int g_config_draw_static_control_background = 0;
/* 1 while the network page lets the connection type change outside single
 * player. The concourse's first frame sets 1; mission setup, the network
 * screens in frontend_net.c and the modern build's network browser set it as
 * they start or leave a session. */
// GLOBAL: XVT 0xB69CE0
int g_config_connection_type_editable = 0;
/* Config page shown: 0 network, 1 single-player video, 2 multiplayer video, 3
 * sound, 4 joystick, 5 taunts. 0 when the config screen opens; the page buttons
 * change it. */
// GLOBAL: XVT 0x664E9C
int g_config_current_page = 0;

/* Keywords of the config file's lines; config_load numbers each by its place
 * here. They name the last pilot, the video settings of both sets, network,
 * sound, 32 joystick buttons (only the first 20 are read), the game options,
 * password, help, taunts and the 3D hardware settings. An empty string ends the
 * list. */
// GLOBAL: XVT 0x52A4C8
char *g_config_keywords[] = {"lastpilot",
			     "backdrop1",
			     "stardensity1",
			     "debris1",
			     "locallights1",
			     "specular1",
			     "diffuse1",
			     "dither1",
			     "textureres1",
			     "mipmap1",
			     "lod1",
			     "screenres1",
			     "windowsize1",
			     "bpp1",
			     "brightness1",
			     "backdrop2",
			     "stardensity2",
			     "debris2",
			     "locallights2",
			     "specular2",
			     "diffuse2",
			     "dither2",
			     "textureres2",
			     "mipmap2",
			     "lod2",
			     "screenres2",
			     "windowsize2",
			     "bpp2",
			     "brightness2",
			     "networktype",
			     "phonenumber",
			     "ipaddress",
			     "sfx_exterior",
			     "sfx_interior",
			     "sfx_engine",
			     "sfx_datapad",
			     "voice_pilot",
			     "voice_tactical_officer",
			     "voice_commander",
			     "voice_special",
			     "music",
			     "sfx_datapad_volume",
			     "sfx_exterior_volume",
			     "sfx_interior_volume",
			     "sfx_engine_volume",
			     "voice_volume",
			     "music_volume",
			     "joybutton1",
			     "joybutton2",
			     "joybutton3",
			     "joybutton4",
			     "joybutton5",
			     "joybutton6",
			     "joybutton7",
			     "joybutton8",
			     "joybutton9",
			     "joybutton10",
			     "joybutton11",
			     "joybutton12",
			     "joybutton13",
			     "joybutton14",
			     "joybutton15",
			     "joybutton16",
			     "joybutton17",
			     "joybutton18",
			     "joybutton19",
			     "joybutton20",
			     "joybutton21",
			     "joybutton22",
			     "joybutton23",
			     "joybutton24",
			     "joybutton25",
			     "joybutton26",
			     "joybutton27",
			     "joybutton28",
			     "joybutton29",
			     "joybutton30",
			     "joybutton31",
			     "joybutton32",
			     "difficulty",
			     "collisions",
			     "craft_jumping",
			     "random_setup",
			     "handicapping",
			     "require_password",
			     "in_progress_join",
			     "craft_selection",
			     "locate_players",
			     "craft_waves",
			     "mission_time_limit",
			     "last_time_limit",
			     "random_seed",
			     "password",
			     "async_flag",
			     "ai_opponents",
			     "help_on",
			     "datapad_music",
			     "server_update_rate",
			     "combat_balance",
			     "taunt1",
			     "taunt2",
			     "taunt3",
			     "taunt4",
			     "use_3d_hardware1",
			     "bilinear1",
			     "use_3d_hardware2",
			     "bilinear2",
			     ""};

/* Update function of the config screen, which the common screen controls push
 * over the whole screen. On frame 0 it sets the cursor, saves and clears the
 * scrollbar focus list, opens the network page, loads joystick.txt and selects
 * button 0's action, and draws frontres\configb.bmp with its frame and overlays
 * into the offscreen surface. Every frame it draws the current page; on every
 * page but the joystick page it then locks and unlocks the offscreen surface
 * (flag 1) while g_config_draw_static_control_background is set. Then it draws the
 * pilot banner, clears that flag, handles the page buttons and Restore
 * defaults, and returns 1 when frontend_handle_common_screen_controls(2) returns
 * 1. Done, a network dismiss packet, or as network host a nonzero
 * net_poll_for_player_created_or_backlog closes the screen: it writes the config,
 * pops the screen, restores the scrollbar focus list and, as network host,
 * sends the game options packet. Returns 0 on every other path; the modern
 * build stops there while a dialog is up. */
// FUNCTION: XVT 0x4B7F30
int config_options_datapad_update(int frame_counter)
{
	enum {
		CONFIG_PAGE_NETWORK = 0,
		CONFIG_PAGE_SINGLEPLAYER_VIDEO = 1,
		CONFIG_PAGE_MULTIPLAYER_VIDEO = 2,
		CONFIG_PAGE_SOUND = 3,
		CONFIG_PAGE_JOYSTICK = 4,
		CONFIG_PAGE_TAUNTS = 5,
		CONFIG_SCREEN_CONTEXT = 2,
		CONFIG_PACKET_SIZE = 19 * sizeof(int),
		PILOT_BANNER_ANIMATION_PERIOD_FRAMES = 32,
	};

	int joystick_entry_index;
	int animation_frame;
	int dismiss_requested;
	struct RECT rect;

	if (frame_counter == 0) {
		frontend_cursor_set_pos(32, 127);
		frontend_scrollbar_save_state();
		frontend_reset_scrollable_controls();
		g_config_joystick_action_scroll_offset = 0;
		g_config_joystick_button_scroll_offset = 0;
		g_config_current_page = CONFIG_PAGE_NETWORK;
		g_config_selected_joystick_button_index = 0;
		g_config_draw_static_control_background = 1;
		keyboard_flush_char_buffer();
		config_load_joystick_action_dictionary();
		for (joystick_entry_index = 0;
		     joystick_entry_index < g_joystick_entry_count;
		     ++joystick_entry_index) {
			if (g_joystick_entries[joystick_entry_index]
				    .action_code ==
			    g_game_config.joy_buttons
				    [g_config_selected_joystick_button_index]) {
				g_config_selected_joystick_action_index =
					joystick_entry_index;
			}
		}
		front_image_register_resource_default("frontres\\configb.bmp",
						      "backconfig");
		frontend_display_lock_offscreen_surface();
		front_image_draw_sprite_opaque("backconfig", 0, 0);
		front_image_draw_sprite("frame", 0, 0);
		front_image_draw_sprite("alloff", 0, 0);
		frontend_draw_rect_assign(&rect, 84, 107, 604, 433);
		front_image_draw_sprite_translucent("configoverlay", 0, 0);
		frontend_display_unlock_offscreen_surface(1);
		frontend_text_start_text_fade_in(20);
	}

	switch (g_config_current_page) {
	case CONFIG_PAGE_NETWORK:
		config_network_options_screen();
		if (g_config_draw_static_control_background != 0) {
			frontend_display_lock_offscreen_surface();
			frontend_display_unlock_offscreen_surface(1);
		}
		break;
	case CONFIG_PAGE_SINGLEPLAYER_VIDEO:
		config_draw_video_option_rows();
		if (g_config_draw_static_control_background != 0) {
			frontend_display_lock_offscreen_surface();
			frontend_display_unlock_offscreen_surface(1);
		}
		break;
	case CONFIG_PAGE_MULTIPLAYER_VIDEO:
		config_draw_video_option_rows();
		if (g_config_draw_static_control_background != 0) {
			frontend_display_lock_offscreen_surface();
			frontend_display_unlock_offscreen_surface(1);
		}
		break;
	case CONFIG_PAGE_SOUND:
		config_sound_options_screen();
		if (g_config_draw_static_control_background != 0) {
			frontend_display_lock_offscreen_surface();
			frontend_display_unlock_offscreen_surface(1);
		}
		break;
	case CONFIG_PAGE_JOYSTICK:
		config_joystick_remap_screen();
		break;
	case CONFIG_PAGE_TAUNTS:
		config_draw_custom_taunts_page();
		if (g_config_draw_static_control_background != 0) {
			frontend_display_lock_offscreen_surface();
			frontend_display_unlock_offscreen_surface(1);
		}
		break;
	}

	frontend_draw_rect_assign(&rect, 200, 452, 436, 464);
	if (g_pilot_data.name[0] != '\0') {
		sprintf(g_frontend_scratch_buffer, "%c%s %c%s", 6,
			g_pilot_data.rating_name, 1, g_pilot_data.name);
		frontend_text_draw_centered(12, g_frontend_scratch_buffer,
					    &rect, g_color_yellow);
		animation_frame =
			frame_counter % PILOT_BANNER_ANIMATION_PERIOD_FRAMES;
		animation_frame >>= 1;
		sprintf(g_frontend_scratch_buffer, "rebtiny%d",
			animation_frame);
		front_image_draw_sprite(g_frontend_scratch_buffer, 204, 453);
		sprintf(g_frontend_scratch_buffer, "imptiny%d",
			animation_frame);
		front_image_draw_sprite(g_frontend_scratch_buffer, 420, 453);
	}

	g_config_draw_static_control_background = 0;
	config_update_navigation_and_restore_defaults();
#ifdef XVT_MODERN
	if (xvt_dialog_is_active()) {
		return 0;
	}
#endif
	if (frontend_handle_common_screen_controls(CONFIG_SCREEN_CONTEXT) ==
	    1) {
		return 1;
	}
#ifdef XVT_MODERN
	if (xvt_dialog_is_active()) {
		return 0;
	}
#endif

	frontend_draw_rect_assign(&rect, 85, 447, 176, 471);
	if (g_game_config.help_on != 0) {
		frontend_button_enable_overlay_text();
	}
	frontend_button_set_overlay_text(
		frontend_string_get(FRONTSTR_206_DONE));
	dismiss_requested = frontend_button_handle_sprite_button(
		&rect, "leaveup", "leavedown",
		frontend_string_get(FRONTSTR_206_DONE), 12, 0, 8,
		"buttonsound");
	frontend_button_disable_overlay_text();
	dismiss_requested |= frontend_dialog_has_network_dismiss_packet();
	if (g_frontend_mission_session_mode ==
	    FRONTEND_MISSION_SESSION_NET_HOST) {
		dismiss_requested |= net_poll_for_player_created_or_backlog();
	}
	if (dismiss_requested != 0) {
		config_write();
		g_active_text_field_id = 0;
		keyboard_flush_char_buffer();
		frontend_screen_pop_state();
		frontend_mouse_clear_input_gate();
		front_image_free_resource_by_name("backconfig");
		frontend_text_stop_text_fade();
		frontend_scrollbar_restore_state();
		if (g_frontend_mission_session_mode ==
		    FRONTEND_MISSION_SESSION_NET_HOST) {
			g_frontend_net_packet_scratch.packet_type =
				NET_PACKET_GAME_OPTIONS;
			*(int *)&g_frontend_net_packet_scratch.payload[0] =
				(uint8_t)g_game_config.difficulty;
			*(int *)&g_frontend_net_packet_scratch.payload[4] =
				g_game_config.collisions;
			*(int *)&g_frontend_net_packet_scratch.payload[8] =
				g_game_config.craft_jumping;
			*(int *)&g_frontend_net_packet_scratch.payload[12] =
				g_game_config.random_setup;
			*(int *)&g_frontend_net_packet_scratch.payload[16] =
				(uint8_t)g_game_config.battle_length_index;
			*(int *)&g_frontend_net_packet_scratch.payload[20] =
				g_game_config.require_password;
			*(int *)&g_frontend_net_packet_scratch.payload[24] =
				g_game_config.in_progress_join;
			*(int *)&g_frontend_net_packet_scratch.payload[28] =
				(uint8_t)g_game_config.craft_selection;
			*(int *)&g_frontend_net_packet_scratch.payload[32] =
				g_game_config.locate_players;
			*(int *)&g_frontend_net_packet_scratch.payload[36] =
				(uint8_t)g_game_config.craft_waves;
			*(int *)&g_frontend_net_packet_scratch.payload[40] =
				g_game_config.mission_time_limit;
			*(int *)&g_frontend_net_packet_scratch.payload[44] =
				g_game_config.last_team_time_limit_minutes;
			*(int *)&g_frontend_net_packet_scratch.payload[48] =
				rand();
			*(int *)&g_frontend_net_packet_scratch.payload[52] =
				g_game_config.internet_play;
			*(int *)&g_frontend_net_packet_scratch.payload[56] =
				g_game_config.ai_opponents;
			*(int *)&g_frontend_net_packet_scratch.payload[60] =
				g_game_config.server_update_rate;
			*(int *)&g_frontend_net_packet_scratch.payload[64] =
				(uint8_t)g_game_config.combat_balance;
			*(int *)&g_frontend_net_packet_scratch.payload[68] =
				(uint8_t)g_game_config
					.continue_battle_or_campaign;
			net_send_packet_and_flush(
				0, &g_frontend_net_packet_scratch,
				CONFIG_PACKET_SIZE);
		}
	}
	return 0;
}

/* Draws the video page for settings set g_config_current_page - 1: the
 * single-player title on page 1, the multiplayer title otherwise, then the
 * sixteen option rows. Does not check that a video page is shown. */
// FUNCTION: XVT 0x4B83A0
void config_draw_video_option_rows(void)
{
	struct RECT rect;

	frontend_draw_rect_assign(&rect, 84, 90, 604, 106);
	frontend_text_draw_centered(
		15,
		g_config_current_page == 1
			? frontend_string_get(
				  FRONTSTR_219_SINGLE_PLAYER_FLIGHT_ENGINE_OPTIONS)
			: frontend_string_get(
				  FRONTSTR_641_MULTIPLAYER_FLIGHT_ENGINE_OPTIONS),
		&rect, 0xFFFF);

	config_draw_screen_resolution_option_row(g_config_current_page - 1);
	config_draw_window_size_option_row(g_config_current_page - 1);
	config_draw_bits_per_pixel_option_row(g_config_current_page - 1);
	config_draw_brightness_option_row(g_config_current_page - 1);
	config_draw_debris_option_row(g_config_current_page - 1);
	config_draw_backdrop_option_row(g_config_current_page - 1);
	config_draw_star_density_option_row(g_config_current_page - 1);
	config_draw_level_of_detail_option_row(g_config_current_page - 1);
	config_draw_texture_resolution_option_row(g_config_current_page - 1);
	config_draw_dither_option_row(g_config_current_page - 1);
	config_draw_mipmap_option_row(g_config_current_page - 1);
	config_draw_local_lights_option_row(g_config_current_page - 1);
	config_draw_specular_option_row(g_config_current_page - 1);
	config_draw_diffuse_lighting_option_row(g_config_current_page - 1);
	config_draw_use3d_hardware_option_row(g_config_current_page - 1);
	config_draw_bilinear_option_row(g_config_current_page - 1);
}

/* Draws the screen resolution row; a new choice also becomes the window
 * size. */
// FUNCTION: XVT 0x4B84E0
void config_draw_screen_resolution_option_row(int is_multiplayer)
{
	struct RECT rect;
	int previous_screen_resolution;

	frontend_draw_rect_assign(&rect, 88, 111, 332, 125);
	frontend_text_draw_aligned_in_rect(
		12, frontend_string_get(FRONTSTR_220_SCREEN_RESOLUTION), &rect,
		0, 1, 0xFFFF);
	frontend_draw_rect_offset_xy(&rect, 0, 15);
	previous_screen_resolution = g_game_config.screen_res[is_multiplayer];
	config_draw_three_choice_option(
		&g_game_config.screen_res[is_multiplayer], &rect,
		FRONTSTR_221_320_X_240);
	if (g_game_config.screen_res[is_multiplayer] !=
	    previous_screen_resolution) {
		g_game_config.window_size[is_multiplayer] =
			g_game_config.screen_res[is_multiplayer];
	}
}

/* Draws the window size row, then lowers the window size to the screen
 * resolution when it is above it. */
// FUNCTION: XVT 0x4B8570
void config_draw_window_size_option_row(int config_index)
{
	struct RECT rect;

	frontend_draw_rect_assign(&rect, 88, 160, 332, 174);
	frontend_text_draw_aligned_in_rect(
		12, frontend_string_get(FRONTSTR_224_WINDOW_SIZE), &rect, 0, 1,
		0xFFFF);
	frontend_draw_rect_offset_xy(&rect, 0, 15);
	config_draw_three_choice_option(
		&g_game_config.window_size[config_index], &rect,
		FRONTSTR_225_320_X_240);
	if (g_game_config.window_size[config_index] >
	    g_game_config.screen_res[config_index]) {
		g_game_config.window_size[config_index] =
			g_game_config.screen_res[config_index];
	}
}

/* Draws the colors row: read-only, with a gray label, while 3D hardware is
 * on. */
// FUNCTION: XVT 0x4B8600
void config_draw_bits_per_pixel_option_row(int config_index)
{
	struct RECT rect;

	frontend_draw_rect_assign(&rect, 88, 209, 332, 223);
	if (g_game_config.use3d_hardware[config_index] != 0) {
		frontend_text_draw_aligned_in_rect(
			12, frontend_string_get(FRONTSTR_232_NUMBER_OF_COLORS),
			&rect, 0, 1, g_color_gray);
		frontend_draw_rect_offset_xy(&rect, 0, 15);
		config_draw_two_choice_option_read_only(
			&g_game_config.color_depth_choice[config_index], &rect,
			FRONTSTR_233_256);
	} else {
		frontend_text_draw_aligned_in_rect(
			12, frontend_string_get(FRONTSTR_232_NUMBER_OF_COLORS),
			&rect, 0, 1, 0xFFFF);
		frontend_draw_rect_offset_xy(&rect, 0, 15);
		config_draw_two_choice_option(
			&g_game_config.color_depth_choice[config_index], &rect,
			FRONTSTR_233_256);
	}
}

/* Draws the brightness row: a slider of 8 positions. */
// FUNCTION: XVT 0x4B86E0
void config_draw_brightness_option_row(int config_index)
{
	struct RECT rect;

	frontend_draw_rect_assign(&rect, 88, 243, 332, 257);
	frontend_text_draw_aligned_in_rect(
		12, frontend_string_get(FRONTSTR_255_BRIGHTNESS), &rect, 0, 1,
		0xFFFF);
	frontend_draw_rect_offset_xy(&rect, 0, 15);
	config_draw_option_slider(&g_game_config.brightness[config_index],
				  &rect, 8, FRONTSTR_256_DIM, 1);
}

/* Draws the space debris row. */
// FUNCTION: XVT 0x4B8760
void config_draw_debris_option_row(int config_index)
{
	struct RECT rect;

	frontend_draw_rect_assign(&rect, 88, 292, 332, 306);
	frontend_text_draw_aligned_in_rect(
		12, frontend_string_get(FRONTSTR_238_SPACE_DEBRIS), &rect, 0, 1,
		0xFFFF);
	frontend_draw_rect_offset_xy(&rect, 0, 15);
	config_draw_two_choice_option(&g_game_config.debris[config_index],
				      &rect, FRONTSTR_236_OFF);
}

/* Draws the backdrop row. */
// FUNCTION: XVT 0x4B87E0
void config_draw_backdrop_option_row(int config_index)
{
	struct RECT rect;

	frontend_draw_rect_assign(&rect, 88, 326, 332, 340);
	frontend_text_draw_aligned_in_rect(
		12, frontend_string_get(FRONTSTR_235_BACKDROP), &rect, 0, 1,
		0xFFFF);
	frontend_draw_rect_offset_xy(&rect, 0, 15);
	config_draw_two_choice_option(&g_game_config.backdrop[config_index],
				      &rect, FRONTSTR_236_OFF);
}

/* Draws the star field density row. */
// FUNCTION: XVT 0x4B8860
void config_draw_star_density_option_row(int config_index)
{
	struct RECT rect;

	frontend_draw_rect_assign(&rect, 88, 360, 332, 374);
	frontend_text_draw_aligned_in_rect(
		12, frontend_string_get(FRONTSTR_228_STARFIELD_DENSITY), &rect,
		0, 1, 0xFFFF);
	frontend_draw_rect_offset_xy(&rect, 0, 15);
	config_draw_three_choice_option(
		&g_game_config.star_density[config_index], &rect,
		FRONTSTR_229_LOW);
}

/* Draws the low detail models row: a slider of 20 positions over lod. */
// FUNCTION: XVT 0x4B88E0
void config_draw_level_of_detail_option_row(int config_index)
{
	struct RECT rect;

	frontend_draw_rect_assign(&rect, 356, 111, 600, 125);
	frontend_text_draw_aligned_in_rect(
		12, frontend_string_get(FRONTSTR_251_USE_LOW_DETAIL_MODELS),
		&rect, 0, 1, 0xFFFF);
	frontend_draw_rect_offset_xy(&rect, 0, 15);
	config_draw_option_slider(&g_game_config.lod[config_index], &rect, 20,
				  FRONTSTR_252_NEAR, 1);
}

/* Draws the texture resolution row. */
// FUNCTION: XVT 0x4B8960
void config_draw_texture_resolution_option_row(int config_index)
{
	struct RECT rect;

	frontend_draw_rect_assign(&rect, 356, 155, 600, 169);
	frontend_text_draw_aligned_in_rect(
		12, frontend_string_get(FRONTSTR_243_TEXTURE_RESOLUTION), &rect,
		0, 1, 0xFFFF);
	frontend_draw_rect_offset_xy(&rect, 0, 15);
	config_draw_three_choice_option(
		&g_game_config.texture_res[config_index], &rect,
		FRONTSTR_244_LOW);
}

/* Draws the dithering row. */
// FUNCTION: XVT 0x4B89E0
void config_draw_dither_option_row(int config_index)
{
	struct RECT rect;

	frontend_draw_rect_assign(&rect, 356, 199, 600, 213);
	frontend_text_draw_aligned_in_rect(
		12, frontend_string_get(FRONTSTR_242_DITHERING), &rect, 0, 1,
		0xFFFF);
	frontend_draw_rect_offset_xy(&rect, 0, 15);
	config_draw_two_choice_option(&g_game_config.dither[config_index],
				      &rect, FRONTSTR_236_OFF);
}

/* Draws the mip-mapping row: a slider of 20 positions with FRONTSTR_250_OPTIMAL
 * centered in yellow below it. */
// FUNCTION: XVT 0x4B8A60
void config_draw_mipmap_option_row(int config_index)
{
	struct RECT rect;

	frontend_draw_rect_assign(&rect, 356, 228, 600, 242);
	frontend_text_draw_aligned_in_rect(
		12, frontend_string_get(FRONTSTR_247_MIP_MAPPING), &rect, 0, 1,
		0xFFFF);
	frontend_draw_rect_offset_xy(&rect, 0, 15);
	config_draw_option_slider(&g_game_config.mipmap[config_index], &rect,
				  20, FRONTSTR_248_BLURRY, 1);
	frontend_draw_rect_offset_xy(&rect, 0, 15);
	frontend_text_draw_centered(12,
				    frontend_string_get(FRONTSTR_250_OPTIMAL),
				    &rect, g_color_yellow);
}

/* Draws the local light source row. */
// FUNCTION: XVT 0x4B8B20
void config_draw_local_lights_option_row(int config_index)
{
	struct RECT rect;

	frontend_draw_rect_assign(&rect, 356, 272, 600, 286);
	frontend_text_draw_aligned_in_rect(
		12, frontend_string_get(FRONTSTR_239_LOCAL_LIGHT_SOURCE), &rect,
		0, 1, 0xFFFF);
	frontend_draw_rect_offset_xy(&rect, 0, 15);
	config_draw_two_choice_option(&g_game_config.local_lights[config_index],
				      &rect, FRONTSTR_236_OFF);
}

/* Draws the specular highlights row: dimmed, with a gray label, while 3D
 * hardware is on. */
// FUNCTION: XVT 0x4B8BA0
void config_draw_specular_option_row(int config_index)
{
	struct RECT rect;

	frontend_draw_rect_assign(&rect, 356, 301, 600, 315);
	if (g_game_config.use3d_hardware[config_index] != 0) {
		frontend_text_draw_aligned_in_rect(
			12,
			frontend_string_get(FRONTSTR_240_SPECULAR_HIGHLIGHTS),
			&rect, 0, 1, g_color_gray);
		frontend_draw_rect_offset_xy(&rect, 0, 15);
		config_draw_two_choice_option_dimmed(
			&g_game_config.specular[config_index], &rect,
			FRONTSTR_236_OFF);
	} else {
		frontend_text_draw_aligned_in_rect(
			12,
			frontend_string_get(FRONTSTR_240_SPECULAR_HIGHLIGHTS),
			&rect, 0, 1, 0xFFFF);
		frontend_draw_rect_offset_xy(&rect, 0, 15);
		config_draw_two_choice_option(
			&g_game_config.specular[config_index], &rect,
			FRONTSTR_236_OFF);
	}
}

/* Draws the diffuse lighting row. */
// FUNCTION: XVT 0x4B8C80
void config_draw_diffuse_lighting_option_row(int config_index)
{
	struct RECT rect;

	frontend_draw_rect_assign(&rect, 356, 330, 600, 344);
	frontend_text_draw_aligned_in_rect(
		12, frontend_string_get(FRONTSTR_241_DIFFUSE_LIGHTING), &rect,
		0, 1, 0xFFFF);
	frontend_draw_rect_offset_xy(&rect, 0, 15);
	config_draw_two_choice_option(&g_game_config.diffuse[config_index],
				      &rect, FRONTSTR_236_OFF);
}

/* Draws the 3D hardware row; turning it on sets the colors choice to 1. */
// FUNCTION: XVT 0x4B8D00
void config_draw_use3d_hardware_option_row(int config_index)
{
	struct RECT rect;
	int previous_use3d_hardware;

	frontend_draw_rect_assign(&rect, 356, 359, 600, 373);
	frontend_text_draw_aligned_in_rect(
		12, frontend_string_get(FRONTSTR_802_3D_HARDWARE), &rect, 0, 1,
		0xFFFF);
	frontend_draw_rect_offset_xy(&rect, 0, 15);
	previous_use3d_hardware = g_game_config.use3d_hardware[config_index];
	config_draw_two_choice_option(
		&g_game_config.use3d_hardware[config_index], &rect,
		FRONTSTR_236_OFF);
	if (g_game_config.use3d_hardware[config_index] !=
		    previous_use3d_hardware &&
	    g_game_config.use3d_hardware[config_index] != 0) {
		g_game_config.color_depth_choice[config_index] = 1;
	}
}

/* Draws the bilinear filtering row: dimmed, with a gray label, while 3D
 * hardware is off. */
// FUNCTION: XVT 0x4B8DA0
void config_draw_bilinear_option_row(int config_index)
{
	struct RECT rect;

	frontend_draw_rect_assign(&rect, 356, 388, 600, 402);
	if (g_game_config.use3d_hardware[config_index] != 0) {
		frontend_text_draw_aligned_in_rect(
			12,
			frontend_string_get(FRONTSTR_803_BILINEAR_FILTERING),
			&rect, 0, 1, 0xFFFF);
		frontend_draw_rect_offset_xy(&rect, 0, 15);
		config_draw_two_choice_option(
			&g_game_config.bilinear[config_index], &rect,
			FRONTSTR_236_OFF);
	} else {
		frontend_text_draw_aligned_in_rect(
			12,
			frontend_string_get(FRONTSTR_803_BILINEAR_FILTERING),
			&rect, 0, 1, g_color_gray);
		frontend_draw_rect_offset_xy(&rect, 0, 15);
		config_draw_two_choice_option_dimmed(
			&g_game_config.bilinear[config_index], &rect,
			FRONTSTR_236_OFF);
	}
}

/* The two-choice control of config_draw_two_choice_option_impl with a translucent
 * marker, taking clicks. */
// FUNCTION: XVT 0x4B8E80
void config_draw_two_choice_option_dimmed(uint8_t *value,
					  const struct RECT *rect,
					  frontend_string_id value_base_str_id)
{
	config_draw_two_choice_option_impl(value, rect, value_base_str_id, 1,
					   0);
}

/* The two-choice control of config_draw_two_choice_option_impl with an opaque
 * marker, taking clicks. */
// FUNCTION: XVT 0x4B8EA0
void config_draw_two_choice_option(uint8_t *value, const struct RECT *rect,
				   frontend_string_id value_base_str_id)
{
	config_draw_two_choice_option_impl(value, rect, value_base_str_id, 0,
					   0);
}

/* The two-choice control of config_draw_two_choice_option_impl with a translucent
 * marker, ignoring clicks. */
// FUNCTION: XVT 0x4B8EC0
void config_draw_two_choice_option_read_only(
	uint8_t *value, const struct RECT *rect,
	frontend_string_id value_base_str_id)
{
	config_draw_two_choice_option_impl(value, rect, value_base_str_id, 1,
					   1);
}

/* Draws a two-choice control at rect: slots for choice 0 and choice 1, 130
 * pixels apart, each with its label (strings value_base_str_id and value_base_str_id
 * + 1) in yellow to the right, and the "3conbtn" marker on the chosen one,
 * translucent when translucent_selection is set. While
 * g_config_draw_static_control_background is set it also draws the slot images into
 * the offscreen background. Unless disable_input is set, a click on a slot
 * stores its number in *value, playing "configsound" when that changes it and
 * frontend sounds are on. */
// FUNCTION: XVT 0x4B8EE0
void config_draw_two_choice_option_impl(uint8_t *value, const struct RECT *rect,
					frontend_string_id value_base_str_id,
					int translucent_selection,
					int disable_input)
{
	struct RECT option_rect;
	struct RECT sprite_rect;
	int cursor_x;
	int cursor_y;
	int option_index;
	int label_color;

	frontend_cursor_get_pos(&cursor_x, &cursor_y);
	front_image_get_resource_rect("offslot", &sprite_rect);
	option_index = 0;
	frontend_draw_rect_copy(&option_rect, rect);
	do {
		option_rect.right = option_rect.left + sprite_rect.right -
				    sprite_rect.left + 1;
		if (g_config_draw_static_control_background != 0) {
			frontend_display_lock_offscreen_surface();
			if (option_index == 0) {
				front_image_draw_sprite_translucent(
					"offslot", option_rect.left,
					option_rect.top);
			} else {
				front_image_draw_sprite_translucent(
					"onslot", option_rect.left,
					option_rect.top);
			}
			frontend_display_unlock_offscreen_surface(0);
			if (option_index == 0) {
				front_image_draw_sprite_translucent(
					"offslot", option_rect.left,
					option_rect.top);
			} else {
				front_image_draw_sprite_translucent(
					"onslot", option_rect.left,
					option_rect.top);
			}
		}

		if (disable_input == 0 &&
		    frontend_draw_point_in_rect(&option_rect, cursor_x,
						cursor_y) &&
		    (frontend_mouse_get_left_click() != 0 ||
		     frontend_mouse_get_right_click() != 0)) {
			if (*value != option_index &&
			    g_game_config.sfx_datapad_enabled != 0) {
				frontend_sound_play_ui_sound(
					"configsound", 1, 0, 255,
					12 * g_game_config.sfx_datapad_volume,
					63);
			}
			*value = (uint8_t)option_index;
		}
		if (*value == option_index) {
			if (translucent_selection != 0) {
				front_image_draw_sprite_translucent(
					"3conbtn", option_rect.left,
					option_rect.top);
			} else {
				front_image_draw_sprite("3conbtn",
							option_rect.left,
							option_rect.top);
			}
		}
		label_color = g_color_yellow;
		++option_index;
		option_rect.left = option_rect.right + 5;
		option_rect.right = option_rect.left + 100;
		frontend_text_draw_aligned_in_rect(
			12,
			frontend_string_get(value_base_str_id + option_index -
					    1),
			&option_rect, 0, 1, label_color);
		option_rect.left = rect->left + 130;
	} while (option_index < 2);
}

/* Draws a three-choice control at rect: the "3conbar" bar with marker positions
 * at its left end, middle and right end for choices 0, 1 and 2, the labels
 * value_base_str_id to value_base_str_id + 2 below them in yellow, and the "3conbtn"
 * marker on the chosen one. A click on another position stores it in *value,
 * playing "configsound" when frontend sounds are on. While
 * g_config_draw_static_control_background is set it also draws the bar into the
 * offscreen background. */
// FUNCTION: XVT 0x4B9090
void config_draw_three_choice_option(uint8_t *value, const struct RECT *rect,
				     frontend_string_id value_base_str_id)
{
	struct RECT option_rect;
	struct RECT sprite_rect;
	int cursor_x;
	int cursor_y;
	int button_width;
	int right;

	if (g_config_draw_static_control_background != 0) {
		frontend_display_lock_offscreen_surface();
		front_image_draw_sprite_translucent("3conbar", rect->left,
						    rect->top);
		frontend_display_unlock_offscreen_surface(0);
		front_image_draw_sprite_translucent("3conbar", rect->left,
						    rect->top);
	}

	frontend_cursor_get_pos(&cursor_x, &cursor_y);
	front_image_get_resource_rect("3conbtn", &sprite_rect);
	button_width = sprite_rect.right - sprite_rect.left + 1;
	front_image_get_resource_rect("3conbar", &sprite_rect);
	frontend_draw_rect_offset_xy(&sprite_rect, rect->left, rect->top);
	frontend_draw_rect_copy(&option_rect, rect);

	option_rect.right = option_rect.left + button_width;
	if (*value == 0) {
		front_image_draw_sprite("3conbtn", option_rect.left,
					option_rect.top);
	} else if ((frontend_mouse_get_left_click() != 0 ||
		    frontend_mouse_get_right_click() != 0) &&
		   frontend_draw_point_in_rect(&option_rect, cursor_x,
					       cursor_y)) {
		if (*value != 0 && g_game_config.sfx_datapad_enabled != 0) {
			frontend_sound_play_ui_sound(
				"configsound", 1, 0, 255,
				12 * g_game_config.sfx_datapad_volume, 63);
		}
		*value = 0;
	}
	frontend_draw_rect_offset_xy(&option_rect, 0, 15);
	frontend_text_draw_aligned_in_rect(
		12, frontend_string_get(value_base_str_id), &option_rect, 0, 1,
		g_color_yellow);
	frontend_draw_rect_offset_xy(&option_rect, 0, -15);

	option_rect.left =
		sprite_rect.left +
		((sprite_rect.right - button_width - sprite_rect.left) >> 1) +
		1;
	option_rect.right = option_rect.left + button_width;
	if (*value == 1) {
		front_image_draw_sprite("3conbtn", option_rect.left,
					option_rect.top);
	} else if ((frontend_mouse_get_left_click() != 0 ||
		    frontend_mouse_get_right_click() != 0) &&
		   frontend_draw_point_in_rect(&option_rect, cursor_x,
					       cursor_y)) {
		if (*value != 1 && g_game_config.sfx_datapad_enabled != 0) {
			frontend_sound_play_ui_sound(
				"configsound", 1, 0, 255,
				12 * g_game_config.sfx_datapad_volume, 63);
		}
		*value = 1;
	}
	frontend_draw_rect_offset_xy(&option_rect, 0, 15);
	frontend_text_draw_centered(12,
				    frontend_string_get(value_base_str_id + 1),
				    &option_rect, g_color_yellow);
	frontend_draw_rect_offset_xy(&option_rect, 0, -15);

	option_rect.right = sprite_rect.right;
	option_rect.left = sprite_rect.right - button_width + 1;
	if (*value == 2) {
		front_image_draw_sprite("3conbtn", option_rect.left,
					option_rect.top);
	} else if ((frontend_mouse_get_left_click() != 0 ||
		    frontend_mouse_get_right_click() != 0) &&
		   frontend_draw_point_in_rect(&option_rect, cursor_x,
					       cursor_y)) {
		if (*value != 2 && g_game_config.sfx_datapad_enabled != 0) {
			frontend_sound_play_ui_sound(
				"configsound", 1, 0, 255,
				12 * g_game_config.sfx_datapad_volume, 63);
		}
		*value = 2;
	}
	frontend_draw_rect_offset_xy(&option_rect, 0, 15);
	right = sprite_rect.right;
	option_rect.left =
		right -
		frontend_text_measure_width(
			frontend_string_get(value_base_str_id + 2), 12) +
		1;
	frontend_text_draw_aligned_in_rect(
		12, frontend_string_get(value_base_str_id + 2), &option_rect, 0,
		1, g_color_yellow);
}

/* Draws a slider of value_count positions across rect and lets a held mouse
 * button move it. The "conhandle" handle sits at *value times the step, the
 * width divided by value_count - 1; range_label_id and range_label_id + 1 label the
 * two ends below it in yellow. With a button held, the cursor within half a
 * step of the right end sets value_count - 1, of the left end 0, and otherwise
 * the step band under it sets 1 to value_count - 2. With play_sound_on_change set
 * and frontend sounds on, a change plays "configsound". Moves rect 2 pixels
 * right while drawing and back on every path. */
// FUNCTION: XVT 0x4B93F0
void config_draw_option_slider(uint8_t *value, struct RECT *rect,
			       int value_count,
			       frontend_string_id range_label_id,
			       int play_sound_on_change)
{
	struct RECT option_rect;
	int cursor_y;
	int cursor_x;
	float step_size;
	struct RECT handle_rect;
	double step_size_as_double;
	int slider_width;
	int selected_value;
	int half_step_width;
	int option_index;
	int integer_step_width;

	slider_width = rect->right - rect->left + 1;
	selected_value = *value;
	step_size = (float)((double)slider_width / (double)(value_count - 1));
	if (g_config_draw_static_control_background != 0) {
		frontend_display_lock_offscreen_surface();
		front_image_draw_sprite_translucent("conbar", rect->left,
						    rect->top + 5);
		frontend_display_unlock_offscreen_surface(0);
		front_image_draw_sprite_translucent("conbar", rect->left,
						    rect->top + 5);
	}

	frontend_draw_rect_copy(&option_rect, rect);
	frontend_draw_rect_offset_xy(&option_rect, 0, 15);
	option_rect.right = option_rect.left + slider_width / 2 - 1;
	frontend_text_draw_aligned_in_rect(12,
					   frontend_string_get(range_label_id),
					   &option_rect, 0, 1, g_color_yellow);
	frontend_draw_rect_offset_xy(&option_rect, slider_width / 2, 0);
	option_rect.left = option_rect.right -
			   frontend_text_measure_width(
				   frontend_string_get(range_label_id + 1), 12);
	frontend_text_draw_aligned_in_rect(
		12, frontend_string_get(range_label_id + 1), &option_rect, 1, 1,
		g_color_yellow);

	frontend_draw_rect_offset_xy(rect, 2, 0);
	front_image_get_resource_rect("conhandle", &handle_rect);
	frontend_draw_rect_offset_xy(
		&handle_rect, (handle_rect.left - handle_rect.right - 1) >> 1,
		0);
	frontend_draw_rect_offset_xy(&handle_rect,
				     (int)(selected_value * step_size), 0);
	front_image_draw_sprite("conhandle", handle_rect.left + rect->left,
				handle_rect.top + rect->top);

	if (frontend_mouse_get_left_down() == 0 &&
	    frontend_mouse_get_right_down() == 0) {
		frontend_draw_rect_offset_xy(rect, -2, 0);
		return;
	}

	frontend_cursor_get_pos(&cursor_x, &cursor_y);
	frontend_draw_rect_copy(&option_rect, rect);
	step_size_as_double = step_size;
	half_step_width = (int)(step_size * 0.5);
	option_rect.left = option_rect.right - half_step_width;
	if (frontend_draw_point_in_rect(&option_rect, cursor_x, cursor_y)) {
		if (play_sound_on_change != 0 && *value != value_count - 1 &&
		    g_game_config.sfx_datapad_enabled != 0) {
			frontend_sound_play_ui_sound(
				"configsound", 1, 0, 255,
				12 * g_game_config.sfx_datapad_volume, 63);
		}
		*value = (uint8_t)(value_count - 1);
		frontend_draw_rect_offset_xy(rect, -2, 0);
		return;
	}

	frontend_draw_rect_copy(&option_rect, rect);
	option_rect.right = option_rect.left + half_step_width;
	if (frontend_draw_point_in_rect(&option_rect, cursor_x, cursor_y)) {
		if (play_sound_on_change != 0 && *value != 0 &&
		    g_game_config.sfx_datapad_enabled != 0) {
			frontend_sound_play_ui_sound(
				"configsound", 1, 0, 255,
				12 * g_game_config.sfx_datapad_volume, 63);
		}
		*value = 0;
		frontend_draw_rect_offset_xy(rect, -2, 0);
		return;
	}

	option_index = 0;
	option_rect.left = option_rect.right;
	option_rect.right += (int)step_size_as_double;
	integer_step_width = (int)step_size_as_double;
	if (value_count - 2 <= 0) {
		frontend_draw_rect_offset_xy(rect, -2, 0);
		return;
	}
	while (!frontend_draw_point_in_rect(&option_rect, cursor_x, cursor_y)) {
		++option_index;
		frontend_draw_rect_offset_xy(&option_rect, integer_step_width,
					     0);
		if (option_index >= value_count - 2) {
			frontend_draw_rect_offset_xy(rect, -2, 0);
			return;
		}
	}
	if (play_sound_on_change != 0 && *value != option_index + 1 &&
	    g_game_config.sfx_datapad_enabled != 0) {
		frontend_sound_play_ui_sound(
			"configsound", 1, 0, 255,
			12 * g_game_config.sfx_datapad_volume, 63);
	}
	*value = (uint8_t)(option_index + 1);
	frontend_draw_rect_offset_xy(rect, -2, 0);
}

/* Loads the settings into g_game_config. Fills in the defaults first: in both
 * video sets the on/off options on, star density, texture resolution, screen
 * and window size 2, mip-mapping and detail 10, brightness 2, 256 colors, 3D
 * hardware as frontend_display_is_secondary_direct_draw_active says; IPX; every
 * sound and message on, at volume 9 and music volume 5; medium difficulty;
 * server update rate 8; mission time limit 255; help on; default actions for
 * joystick buttons 1 to 10, as many as the joystick has; and the four default
 * taunts. The modern build then applies config.yaml through xvt_config_apply and
 * stops with a fatal error when that fails. The original build reads
 * config2.cfg, or else the base game's config.cfg, as lines of a keyword from
 * g_config_keywords, a space and a value; it ignores unknown keywords and
 * joystick buttons 21 to 32. Button actions from config.cfg in 124 to 229 are
 * raised by 4. Checks no value's range. */
// FUNCTION: XVT 0x4B97E0
void config_load(void)
{
	int legacy_joy_button_loaded[20];
	int button_count;
	int config_index;
#ifndef XVT_MODERN
	xvt_file *stream;
	int loaded_legacy_config;
	char *value;
	int character_index;
	int keyword_index;
	int matched_keyword_index;
#endif

	memset(legacy_joy_button_loaded, 0, sizeof(legacy_joy_button_loaded));
	memset(&g_game_config, 0, sizeof(g_game_config));
	for (config_index = 0; config_index < 2; ++config_index) {
		g_game_config.backdrop[config_index] = 1;
		g_game_config.star_density[config_index] = 2;
		g_game_config.debris[config_index] = 1;
		g_game_config.local_lights[config_index] = 1;
		g_game_config.specular[config_index] = 1;
		g_game_config.diffuse[config_index] = 1;
		g_game_config.dither[config_index] = 1;
		g_game_config.texture_res[config_index] = 2;
		g_game_config.mipmap[config_index] = 10;
		g_game_config.lod[config_index] = 10;
		g_game_config.screen_res[config_index] = 2;
		g_game_config.window_size[config_index] = 2;
		g_game_config.color_depth_choice[config_index] = 0;
		g_game_config.brightness[config_index] = 2;
		g_game_config.use3d_hardware[config_index] =
			frontend_display_is_secondary_direct_draw_active();
		g_game_config.bilinear[config_index] = 1;
	}
	g_game_config.network_type = 0;
	g_game_config.sfx_exterior_enabled = 1;
	g_game_config.sfx_interior_enabled = 1;
	g_game_config.sfx_engine_enabled = 1;
	g_game_config.sfx_datapad_enabled = 1;
	g_game_config.voice_pilot_level = 2;
	g_game_config.voice_tactical_officer_level = 2;
	g_game_config.voice_commander_enabled = 1;
	g_game_config.voice_special_enabled = 1;
	g_game_config.music_enabled = 1;
	g_game_config.datapad_music_enabled = 1;
	g_game_config.sfx_datapad_volume = 9;
	g_game_config.sfx_exterior_volume = 9;
	g_game_config.sfx_interior_volume = 9;
	g_game_config.sfx_engine_volume = 9;
	g_game_config.voice_volume = 9;
	g_game_config.difficulty = GAME_DIFFICULTY_MEDIUM;
	g_game_config.collisions = 1;
	g_game_config.craft_jumping = 1;
	g_game_config.random_setup = 0;
	g_game_config.battle_length_index = BATTLE_LENGTH_THREE_WINS;
	g_game_config.require_password = 0;
	g_game_config.in_progress_join = 0;
	g_game_config.craft_selection = CRAFT_SELECTION_ON;
	g_game_config.locate_players = 1;
	g_game_config.craft_waves = CRAFT_WAVES_DEFAULT;
	g_game_config.last_team_time_limit_minutes = 1;
	g_game_config.random_seed = 0;
	g_game_config.internet_play = 0;
	g_game_config.ai_opponents = 0;
	g_game_config.help_on = 1;
	g_game_config.combat_balance = COMBAT_BALANCE_AUTOBALANCE;
	g_game_config.continue_battle_or_campaign = SEQUENCE_CONTINUE;
	g_game_config.music_volume = 5;
	g_game_config.mission_time_limit = UINT8_MAX;
	g_game_config.server_update_rate = 8;

	/* config_index counted the single-player and multiplayer settings sets above; from here it is a
	 * joystick button index, here and in the legacy button conversion at the end. */
	button_count = joystick_get_button_count(0);
	for (config_index = 0; config_index < button_count && config_index < 16;
	     ++config_index) {
		switch (config_index) {
		case 0:
			g_game_config.joy_buttons[config_index] = (uint8_t)-100;
			break;
		case 1:
			g_game_config.joy_buttons[config_index] = (uint8_t)-99;
			break;
		case 2:
			g_game_config.joy_buttons[config_index] = 114;
			break;
		case 3:
			g_game_config.joy_buttons[config_index] = 46;
			break;
		case 4:
			g_game_config.joy_buttons[config_index] = 101;
			break;
		case 5:
			g_game_config.joy_buttons[config_index] = 105;
			break;
		case 6:
			g_game_config.joy_buttons[config_index] = 91;
			break;
		case 7:
			g_game_config.joy_buttons[config_index] = 8;
			break;
		case 8:
			g_game_config.joy_buttons[config_index] = 13;
			break;
		case 9:
			g_game_config.joy_buttons[config_index] = 93;
			break;
		}
	}

	strcpy(g_game_config.taunts[0],
	       frontend_string_get(FRONTSTR_790_STAY_ON_TARGET));
	strcpy(g_game_config.taunts[1],
	       frontend_string_get(FRONTSTR_791_I_CAN_T_SHAKE_HIM));
	strcpy(g_game_config.taunts[2],
	       frontend_string_get(FRONTSTR_792_HE_S_HISTORY));
	strcpy(g_game_config.taunts[3],
	       frontend_string_get(FRONTSTR_793_WOOHOO));

#ifdef XVT_MODERN
	{
		char error[512];
		if (!xvt_config_apply(&g_game_config, error, sizeof(error))) {
			xvt_storage_fatal(error, 1);
		}
	}
	return;
#else
	stream = file_open("config2.cfg", "r");
	loaded_legacy_config = 0;
	if (stream == NULL) {
		file_change_to_base_game_install_path();
		stream = file_open("config.cfg", "r");
		file_change_to_install_path();
		loaded_legacy_config = 1;
	}
	if (stream == NULL) {
		return;
	}

	for (;;) {
		if (FILE_GETS(g_frontend_scratch_buffer, 256, stream) == NULL) {
			break;
		}
		if (g_frontend_scratch_buffer
			    [strlen(g_frontend_scratch_buffer) - 1] == '\n') {
			g_frontend_scratch_buffer
				[strlen(g_frontend_scratch_buffer) - 1] = '\0';
		}

		value = NULL;
		character_index = 0;
		if ((int)strlen(g_frontend_scratch_buffer) > 0) {
			do {
				if (g_frontend_scratch_buffer
					    [character_index] == ' ') {
					g_frontend_scratch_buffer
						[character_index] = '\0';
					value = &g_frontend_scratch_buffer
							[character_index + 1];
					break;
				}
				++character_index;
			} while ((int)strlen(g_frontend_scratch_buffer) >
				 character_index);
		}
		if (value == NULL) {
			continue;
		}

		keyword_index = 0;
		matched_keyword_index = -1;
		while (*g_config_keywords[keyword_index] != '\0') {
			if (strcmp(g_config_keywords[keyword_index],
				   g_frontend_scratch_buffer) == 0) {
				matched_keyword_index = keyword_index;
				break;
			}
			++keyword_index;
		}
		if (matched_keyword_index == -1) {
			continue;
		}

		switch (matched_keyword_index) {
		case 0:
			memcpy(g_game_config.last_pilot_name, value,
			       sizeof(g_game_config.last_pilot_name));
			break;
		case 1:
			g_game_config.backdrop[0] = (uint8_t)atoi(value);
			break;
		case 2:
			g_game_config.star_density[0] = (uint8_t)atoi(value);
			break;
		case 3:
			g_game_config.debris[0] = (uint8_t)atoi(value);
			break;
		case 4:
			g_game_config.local_lights[0] = (uint8_t)atoi(value);
			break;
		case 5:
			g_game_config.specular[0] = (uint8_t)atoi(value);
			break;
		case 6:
			g_game_config.diffuse[0] = (uint8_t)atoi(value);
			break;
		case 7:
			g_game_config.dither[0] = (uint8_t)atoi(value);
			break;
		case 8:
			g_game_config.texture_res[0] = (uint8_t)atoi(value);
			break;
		case 9:
			g_game_config.mipmap[0] = (uint8_t)atoi(value);
			break;
		case 10:
			g_game_config.lod[0] = (uint8_t)atoi(value);
			break;
		case 11:
			g_game_config.screen_res[0] = (uint8_t)atoi(value);
			break;
		case 12:
			g_game_config.window_size[0] = (uint8_t)atoi(value);
			break;
		case 13:
			g_game_config.color_depth_choice[0] =
				(uint8_t)atoi(value);
			break;
		case 14:
			g_game_config.brightness[0] = (uint8_t)atoi(value);
			break;
		case 15:
			g_game_config.backdrop[1] = (uint8_t)atoi(value);
			break;
		case 16:
			g_game_config.star_density[1] = (uint8_t)atoi(value);
			break;
		case 17:
			g_game_config.debris[1] = (uint8_t)atoi(value);
			break;
		case 18:
			g_game_config.local_lights[1] = (uint8_t)atoi(value);
			break;
		case 19:
			g_game_config.specular[1] = (uint8_t)atoi(value);
			break;
		case 20:
			g_game_config.diffuse[1] = (uint8_t)atoi(value);
			break;
		case 21:
			g_game_config.dither[1] = (uint8_t)atoi(value);
			break;
		case 22:
			g_game_config.texture_res[1] = (uint8_t)atoi(value);
			break;
		case 23:
			g_game_config.mipmap[1] = (uint8_t)atoi(value);
			break;
		case 24:
			g_game_config.lod[1] = (uint8_t)atoi(value);
			break;
		case 25:
			g_game_config.screen_res[1] = (uint8_t)atoi(value);
			break;
		case 26:
			g_game_config.window_size[1] = (uint8_t)atoi(value);
			break;
		case 27:
			g_game_config.color_depth_choice[1] =
				(uint8_t)atoi(value);
			break;
		case 28:
			g_game_config.brightness[1] = (uint8_t)atoi(value);
			break;
		case 29:
			g_game_config.network_type = (uint8_t)atoi(value);
			break;
		case 30:
			memcpy(g_game_config.phone_number, value,
			       sizeof(g_game_config.phone_number));
			break;
		case 31:
			memcpy(g_game_config.ip_address, value,
			       sizeof(g_game_config.ip_address));
			break;
		case 32:
			g_game_config.sfx_exterior_enabled =
				(uint8_t)atoi(value);
			break;
		case 33:
			g_game_config.sfx_interior_enabled =
				(uint8_t)atoi(value);
			break;
		case 34:
			g_game_config.sfx_engine_enabled = (uint8_t)atoi(value);
			break;
		case 35:
			g_game_config.sfx_datapad_enabled =
				(uint8_t)atoi(value);
			break;
		case 36:
			g_game_config.voice_pilot_level = (uint8_t)atoi(value);
			break;
		case 37:
			g_game_config.voice_tactical_officer_level =
				(uint8_t)atoi(value);
			break;
		case 38:
			g_game_config.voice_commander_enabled =
				(uint8_t)atoi(value);
			break;
		case 39:
			g_game_config.voice_special_enabled =
				(uint8_t)atoi(value);
			break;
		case 40:
			g_game_config.music_enabled = (uint8_t)atoi(value);
			break;
		case 41:
			g_game_config.sfx_datapad_volume = (uint8_t)atoi(value);
			break;
		case 42:
			g_game_config.sfx_exterior_volume =
				(uint8_t)atoi(value);
			break;
		case 43:
			g_game_config.sfx_interior_volume =
				(uint8_t)atoi(value);
			break;
		case 44:
			g_game_config.sfx_engine_volume = (uint8_t)atoi(value);
			break;
		case 45:
			g_game_config.voice_volume = (uint8_t)atoi(value);
			break;
		case 46:
			g_game_config.music_volume = (uint8_t)atoi(value);
			break;
		case 47:
		case 48:
		case 49:
		case 50:
		case 51:
		case 52:
		case 53:
		case 54:
		case 55:
		case 56:
		case 57:
		case 58:
		case 59:
		case 60:
		case 61:
		case 62:
		case 63:
		case 64:
		case 65:
		case 66:
			g_game_config.joy_buttons[matched_keyword_index - 47] =
				(uint8_t)atoi(value);
			if (loaded_legacy_config != 0) {
				legacy_joy_button_loaded[matched_keyword_index -
							 47] = 1;
			}
			break;
		case 79:
			g_game_config.difficulty = (game_difficulty)atoi(value);
			break;
		case 80:
			g_game_config.collisions = (uint8_t)atoi(value);
			break;
		case 81:
			g_game_config.craft_jumping = (uint8_t)atoi(value);
			break;
		case 82:
			g_game_config.random_setup = (uint8_t)atoi(value);
			break;
		case 83:
			g_game_config.battle_length_index =
				(battle_length)atoi(value);
			break;
		case 84:
			g_game_config.require_password = (uint8_t)atoi(value);
			break;
		case 85:
			g_game_config.in_progress_join = (uint8_t)atoi(value);
			break;
		case 86:
			g_game_config.craft_selection =
				(craft_selection_mode)atoi(value);
			break;
		case 87:
			g_game_config.locate_players = (uint8_t)atoi(value);
			break;
		case 88:
			g_game_config.craft_waves =
				(craft_wave_mode)atoi(value);
			break;
		case 89:
			g_game_config.mission_time_limit = (uint8_t)atoi(value);
			break;
		case 90:
			g_game_config.last_team_time_limit_minutes =
				(uint8_t)atoi(value);
			break;
		case 91:
			g_game_config.random_seed = (unsigned int)atoi(value);
			break;
		case 92:
			memcpy(g_game_config.password, value,
			       sizeof(g_game_config.password));
			break;
		case 93:
			g_game_config.internet_play = (uint8_t)atoi(value);
			break;
		case 94:
			g_game_config.ai_opponents = (uint8_t)atoi(value);
			break;
		case 95:
			g_game_config.help_on = (uint8_t)atoi(value);
			break;
		case 96:
			g_game_config.datapad_music_enabled =
				(uint8_t)atoi(value);
			break;
		case 97:
			g_game_config.server_update_rate = (uint8_t)atoi(value);
			break;
		case 98:
			g_game_config.combat_balance =
				(combat_balance_mode)atoi(value);
			break;
		case 99:
		case 100:
		case 101:
		case 102:
			memcpy(g_game_config.taunts[matched_keyword_index - 99],
			       value, sizeof(g_game_config.taunts[0]));
			break;
		case 103:
			g_game_config.use3d_hardware[0] = (uint8_t)atoi(value);
			break;
		case 104:
			g_game_config.bilinear[0] = (uint8_t)atoi(value);
			break;
		case 105:
			g_game_config.use3d_hardware[1] = (uint8_t)atoi(value);
			break;
		case 106:
			g_game_config.bilinear[1] = (uint8_t)atoi(value);
			break;
		}
	}

	file_close(stream);
	if (loaded_legacy_config != 0) {
		for (config_index = 0; config_index < 20; ++config_index) {
			if (legacy_joy_button_loaded[config_index] != 0 &&
			    g_game_config.joy_buttons[config_index] >= 124 &&
			    g_game_config.joy_buttons[config_index] <= 229) {
				g_game_config.joy_buttons[config_index] += 4;
			}
		}
	}
#endif
}

/* Saves g_game_config, with the current pilot as the last pilot. The modern
 * build writes config.yaml through xvt_config_write and stops with a fatal error
 * when that fails. The original build writes config2.cfg, one keyword and value
 * per line, every setting but continue_battle_or_campaign; it writes nothing when
 * the file does not open. */
// FUNCTION: XVT 0x4BA3C0
void config_write(void)
{
#ifdef XVT_MODERN
	char error[512];
	snprintf(g_game_config.last_pilot_name,
		 sizeof(g_game_config.last_pilot_name), "%s",
		 g_pilot_data.name);
	if (!xvt_config_write(&g_game_config, error, sizeof(error))) {
		xvt_storage_fatal(error, 1);
	}
#else

	xvt_file *stream;
	int config_index;
	int option_number;

	strncpy(g_game_config.last_pilot_name, g_pilot_data.name,
		sizeof(g_game_config.last_pilot_name) - 1);
	g_game_config
		.last_pilot_name[sizeof(g_game_config.last_pilot_name) - 1] =
		'\0';
	stream = file_open("config2.cfg", "w");
	if (stream == NULL) {
		return;
	}

	FILE_PRINTF(stream, "lastpilot %s\n", g_pilot_data.name);
	for (config_index = 0;
	     config_index < (int)(sizeof(g_game_config.backdrop) /
				  sizeof(g_game_config.backdrop[0]));
	     ++config_index) {
		option_number = config_index + 1;
		FILE_PRINTF(stream, "backdrop%d %d\n", option_number,
			    g_game_config.backdrop[config_index]);
		FILE_PRINTF(stream, "stardensity%d %d\n", option_number,
			    g_game_config.star_density[config_index]);
		FILE_PRINTF(stream, "debris%d %d\n", option_number,
			    g_game_config.debris[config_index]);
		FILE_PRINTF(stream, "locallights%d %d\n", option_number,
			    g_game_config.local_lights[config_index]);
		FILE_PRINTF(stream, "specular%d %d\n", option_number,
			    g_game_config.specular[config_index]);
		FILE_PRINTF(stream, "diffuse%d %d\n", option_number,
			    g_game_config.diffuse[config_index]);
		FILE_PRINTF(stream, "dither%d %d\n", option_number,
			    g_game_config.dither[config_index]);
		FILE_PRINTF(stream, "textureres%d %d\n", option_number,
			    g_game_config.texture_res[config_index]);
		FILE_PRINTF(stream, "mipmap%d %d\n", option_number,
			    g_game_config.mipmap[config_index]);
		FILE_PRINTF(stream, "lod%d %d\n", option_number,
			    g_game_config.lod[config_index]);
		FILE_PRINTF(stream, "screenres%d %d\n", option_number,
			    g_game_config.screen_res[config_index]);
		FILE_PRINTF(stream, "windowsize%d %d\n", option_number,
			    g_game_config.window_size[config_index]);
		FILE_PRINTF(stream, "bpp%d %d\n", option_number,
			    g_game_config.color_depth_choice[config_index]);
		FILE_PRINTF(stream, "brightness%d %d\n", option_number,
			    g_game_config.brightness[config_index]);
		FILE_PRINTF(stream, "use_3d_hardware%d %d\n", option_number,
			    g_game_config.use3d_hardware[config_index]);
		FILE_PRINTF(stream, "bilinear%d %d\n", option_number,
			    g_game_config.bilinear[config_index]);
	}

	FILE_PRINTF(stream, "networktype %d\n", g_game_config.network_type);
	FILE_PRINTF(stream, "phonenumber %s\n", g_game_config.phone_number);
	FILE_PRINTF(stream, "ipaddress %s\n", g_game_config.ip_address);
	FILE_PRINTF(stream, "server_update_rate %d\n",
		    g_game_config.server_update_rate);
	FILE_PRINTF(stream, "sfx_exterior %d\n",
		    g_game_config.sfx_exterior_enabled);
	FILE_PRINTF(stream, "sfx_interior %d\n",
		    g_game_config.sfx_interior_enabled);
	FILE_PRINTF(stream, "sfx_engine %d\n",
		    g_game_config.sfx_engine_enabled);
	FILE_PRINTF(stream, "sfx_datapad %d\n",
		    g_game_config.sfx_datapad_enabled);
	FILE_PRINTF(stream, "voice_pilot %d\n",
		    g_game_config.voice_pilot_level);
	FILE_PRINTF(stream, "voice_tactical_officer %d\n",
		    g_game_config.voice_tactical_officer_level);
	FILE_PRINTF(stream, "voice_commander %d\n",
		    g_game_config.voice_commander_enabled);
	FILE_PRINTF(stream, "voice_special %d\n",
		    g_game_config.voice_special_enabled);
	FILE_PRINTF(stream, "music %d\n", g_game_config.music_enabled);
	FILE_PRINTF(stream, "sfx_datapad_volume %d\n",
		    g_game_config.sfx_datapad_volume);
	FILE_PRINTF(stream, "sfx_exterior_volume %d\n",
		    g_game_config.sfx_exterior_volume);
	FILE_PRINTF(stream, "sfx_interior_volume %d\n",
		    g_game_config.sfx_interior_volume);
	FILE_PRINTF(stream, "sfx_engine_volume %d\n",
		    g_game_config.sfx_engine_volume);
	FILE_PRINTF(stream, "voice_volume %d\n", g_game_config.voice_volume);
	FILE_PRINTF(stream, "music_volume %d\n", g_game_config.music_volume);
	FILE_PRINTF(stream, "datapad_music %d\n",
		    g_game_config.datapad_music_enabled);
	FILE_PRINTF(stream, "joybutton1 %d\n", g_game_config.joy_buttons[0]);
	FILE_PRINTF(stream, "joybutton2 %d\n", g_game_config.joy_buttons[1]);
	FILE_PRINTF(stream, "joybutton3 %d\n", g_game_config.joy_buttons[2]);
	FILE_PRINTF(stream, "joybutton4 %d\n", g_game_config.joy_buttons[3]);
	FILE_PRINTF(stream, "joybutton5 %d\n", g_game_config.joy_buttons[4]);
	FILE_PRINTF(stream, "joybutton6 %d\n", g_game_config.joy_buttons[5]);
	FILE_PRINTF(stream, "joybutton7 %d\n", g_game_config.joy_buttons[6]);
	FILE_PRINTF(stream, "joybutton8 %d\n", g_game_config.joy_buttons[7]);
	FILE_PRINTF(stream, "joybutton9 %d\n", g_game_config.joy_buttons[8]);
	FILE_PRINTF(stream, "joybutton10 %d\n", g_game_config.joy_buttons[9]);
	FILE_PRINTF(stream, "joybutton11 %d\n", g_game_config.joy_buttons[10]);
	FILE_PRINTF(stream, "joybutton12 %d\n", g_game_config.joy_buttons[11]);
	FILE_PRINTF(stream, "joybutton13 %d\n", g_game_config.joy_buttons[12]);
	FILE_PRINTF(stream, "joybutton14 %d\n", g_game_config.joy_buttons[13]);
	FILE_PRINTF(stream, "joybutton15 %d\n", g_game_config.joy_buttons[14]);
	FILE_PRINTF(stream, "joybutton16 %d\n", g_game_config.joy_buttons[15]);
	FILE_PRINTF(stream, "joybutton17 %d\n", g_game_config.joy_buttons[16]);
	FILE_PRINTF(stream, "joybutton18 %d\n", g_game_config.joy_buttons[17]);
	FILE_PRINTF(stream, "joybutton19 %d\n", g_game_config.joy_buttons[18]);
	FILE_PRINTF(stream, "joybutton20 %d\n", g_game_config.joy_buttons[19]);
	FILE_PRINTF(stream, "difficulty %d\n", g_game_config.difficulty);
	FILE_PRINTF(stream, "collisions %d\n", g_game_config.collisions);
	FILE_PRINTF(stream, "craft_jumping %d\n", g_game_config.craft_jumping);
	FILE_PRINTF(stream, "random_setup %d\n", g_game_config.random_setup);
	FILE_PRINTF(stream, "handicapping %d\n",
		    g_game_config.battle_length_index);
	FILE_PRINTF(stream, "require_password %d\n",
		    g_game_config.require_password);
	FILE_PRINTF(stream, "in_progress_join %d\n",
		    g_game_config.in_progress_join);
	FILE_PRINTF(stream, "craft_selection %d\n",
		    g_game_config.craft_selection);
	FILE_PRINTF(stream, "locate_players %d\n",
		    g_game_config.locate_players);
	FILE_PRINTF(stream, "craft_waves %d\n", g_game_config.craft_waves);
	FILE_PRINTF(stream, "mission_time_limit %d\n",
		    g_game_config.mission_time_limit);
	FILE_PRINTF(stream, "last_time_limit %d\n",
		    g_game_config.last_team_time_limit_minutes);
	FILE_PRINTF(stream, "random_seed %d\n", g_game_config.random_seed);
	FILE_PRINTF(stream, "password %s\n", g_game_config.password);
	FILE_PRINTF(stream, "async_flag %d\n", g_game_config.internet_play);
	FILE_PRINTF(stream, "ai_opponents %d\n", g_game_config.ai_opponents);
	FILE_PRINTF(stream, "help_on %d\n", g_game_config.help_on);
	FILE_PRINTF(stream, "combat_balance %d\n",
		    g_game_config.combat_balance);
	FILE_PRINTF(stream, "taunt1 %s\n", g_game_config.taunts[0]);
	FILE_PRINTF(stream, "taunt2 %s\n", g_game_config.taunts[1]);
	FILE_PRINTF(stream, "taunt3 %s\n", g_game_config.taunts[2]);
	FILE_PRINTF(stream, "taunt4 %s\n", g_game_config.taunts[3]);

	file_close(stream);

#endif
}

/* Draws the config screen's eight page lights and handles its page buttons and
 * Restore defaults; returns 1, or 0 while the modern build's confirm dialog is
 * up. Restore defaults, hidden on the network page during a network session,
 * asks first, then resets the current page: the network update rate to 8 and
 * the connection to TCP/IP with internet play in the modern build or IPX
 * without it in the original; a video set to its defaults, 3D hardware as
 * frontend_display_is_secondary_direct_draw_active says for single player and off
 * for multiplayer; the sounds, starting CD track 7 when frontend music was off;
 * the joystick buttons; or the taunts. A page button switches pages, redraws
 * the background into the offscreen surface and sets
 * g_config_draw_static_control_background. In the modern build the joystick button
 * calls xvt_port_request_settings instead, so it never shows the joystick
 * page. */
// FUNCTION: XVT 0x4BAAF0
int config_update_navigation_and_restore_defaults(void)
{
	enum {
		CONFIG_PAGE_NETWORK = 0,
		CONFIG_PAGE_SINGLEPLAYER_VIDEO = 1,
		CONFIG_PAGE_MULTIPLAYER_VIDEO = 2,
		CONFIG_PAGE_SOUND = 3,
		CONFIG_PAGE_JOYSTICK = 4,
		CONFIG_PAGE_TAUNTS = 5,
		CONFIG_NAVIGATION_SLOT_COUNT = 8,
		CONFIG_NAVIGATION_PAGE_SLOT_COUNT = 5,
		CONFIG_NAVIGATION_RESTORE_SLOT = 5,
		CONFIG_NAVIGATION_UNUSED_SLOT = 6,
		CONFIG_NAVIGATION_TAUNTS_SLOT = 7,
		CONFIG_BUTTON_LEFT = 22,
		CONFIG_BUTTON_RIGHT = 42,
		CONFIG_RESTORE_BUTTON_TOP = 306,
		CONFIG_RESTORE_BUTTON_BOTTOM = 330,
		CONFIG_TAUNTS_BUTTON_TOP = 254,
		CONFIG_TAUNTS_BUTTON_BOTTOM = 278,
		CONFIG_JOYSTICK_BUTTON_TOP = 226,
		CONFIG_JOYSTICK_BUTTON_BOTTOM = 250,
		CONFIG_BUTTON_VERTICAL_STEP = 28,
		CONFIG_CONTENT_LEFT = 84,
		CONFIG_CONTENT_TOP = 107,
		CONFIG_CONTENT_RIGHT = 604,
		CONFIG_CONTENT_BOTTOM = 433,
		CONFIG_JOYSTICK_SHADE_LEFT = 330,
		CONFIG_JOYSTICK_SHADE_TOP = 111,
		CONFIG_JOYSTICK_SHADE_RIGHT = 590,
		CONFIG_JOYSTICK_SHADE_BOTTOM = 415,
		CONFIG_JOYSTICK_SHADE_BLUE = 0x40,
		CONFIG_BUTTON_FONT_SIZE = 12,
		CONFIG_RESTORE_HELD_SLOT = 16,
		CONFIG_TAUNTS_HELD_SLOT = 18,
		CONFIG_JOYSTICK_HELD_SLOT = 15,
		CONFIG_SOUND_HELD_SLOT = 14,
		CONFIG_MULTIPLAYER_VIDEO_HELD_SLOT = 13,
		CONFIG_SINGLEPLAYER_VIDEO_HELD_SLOT = 12,
		CONFIG_NETWORK_HELD_SLOT = 11,
		CONFIG_DEFAULT_SERVER_UPDATE_RATE = 8,
		CONFIG_DEFAULT_DISABLED = 0,
		CONFIG_DEFAULT_256_COLORS = 0,
		CONFIG_DEFAULT_ENABLED = 1,
		CONFIG_DEFAULT_THREE_CHOICE_MIDDLE = 1,
		CONFIG_DEFAULT_THREE_CHOICE_HIGH = 2,
		CONFIG_DEFAULT_BRIGHTNESS = 2,
		CONFIG_DEFAULT_VIDEO_DENSITY = 2,
		CONFIG_DEFAULT_VIDEO_QUALITY = 10,
		CONFIG_DEFAULT_SOUND_VOLUME = 9,
		CONFIG_DEFAULT_MUSIC_VOLUME = 5,
		CONFIG_DEFAULT_JOYSTICK_BUTTON_LIMIT = 16,
		CONFIG_DEFAULT_DATAPAD_MUSIC_VOLUME = 0x8E38,
		CONFIG_DEFAULT_DATAPAD_MUSIC_TRACK = 7
	};

	struct {
		struct RECT rect; /* Area of the button being handled. */
		/* State of each page light: the five pages, restore, an unused
		 * slot and taunts. */
		frontend_navigation_slot_state
			navigation_slot_states[CONFIG_NAVIGATION_SLOT_COUNT];
		int cursor_y; /* Cursor y. */
		int cursor_x; /* Cursor x. */
	} ui;

	int joystick_button_count;
	int joystick_button_index;
	int joystick_action_index;
	int current_page;
	int previous_datapad_music_enabled;
	uint8_t selected_joystick_action_code;

	ui.navigation_slot_states[0] = FRONTEND_NAVIGATION_SLOT_ACTIVE;
	ui.navigation_slot_states[1] = FRONTEND_NAVIGATION_SLOT_ACTIVE;
	ui.navigation_slot_states[2] = FRONTEND_NAVIGATION_SLOT_ACTIVE;
	ui.navigation_slot_states[3] = FRONTEND_NAVIGATION_SLOT_ACTIVE;
	ui.navigation_slot_states[4] = FRONTEND_NAVIGATION_SLOT_ACTIVE;
	current_page = g_config_current_page;
	if (current_page != CONFIG_PAGE_NETWORK ||
	    (g_frontend_mission_session_mode !=
		     FRONTEND_MISSION_SESSION_NET_HOST &&
	     g_frontend_mission_session_mode !=
		     FRONTEND_MISSION_SESSION_NET_CLIENT)) {
		ui.navigation_slot_states[CONFIG_NAVIGATION_RESTORE_SLOT] =
			FRONTEND_NAVIGATION_SLOT_ACTIVE;
	} else {
		ui.navigation_slot_states[CONFIG_NAVIGATION_RESTORE_SLOT] =
			FRONTEND_NAVIGATION_SLOT_INACTIVE;
	}
	ui.navigation_slot_states[CONFIG_NAVIGATION_UNUSED_SLOT] =
		FRONTEND_NAVIGATION_SLOT_INACTIVE;
	ui.navigation_slot_states[CONFIG_NAVIGATION_TAUNTS_SLOT] =
		FRONTEND_NAVIGATION_SLOT_ACTIVE;
	if (current_page == CONFIG_PAGE_TAUNTS) {
		ui.navigation_slot_states[CONFIG_NAVIGATION_TAUNTS_SLOT] =
			FRONTEND_NAVIGATION_SLOT_SELECTED;
	} else {
		ui.navigation_slot_states[current_page] =
			FRONTEND_NAVIGATION_SLOT_SELECTED;
	}

	frontend_draw_rect_assign(
		&ui.rect, CONFIG_BUTTON_LEFT, CONFIG_RESTORE_BUTTON_TOP,
		CONFIG_BUTTON_RIGHT, CONFIG_RESTORE_BUTTON_BOTTOM);
	frontend_cursor_get_pos(&ui.cursor_x, &ui.cursor_y);
	if (ui.navigation_slot_states[CONFIG_NAVIGATION_RESTORE_SLOT] !=
		    FRONTEND_NAVIGATION_SLOT_INACTIVE &&
	    frontend_draw_point_in_rect(&ui.rect, ui.cursor_x, ui.cursor_y) &&
	    (frontend_mouse_get_left_down() != 0 ||
	     frontend_mouse_get_right_down() != 0 ||
	     frontend_mouse_get_left_click() != 0 ||
	     frontend_mouse_get_right_click() != 0)) {
		ui.navigation_slot_states[CONFIG_NAVIGATION_RESTORE_SLOT] =
			FRONTEND_NAVIGATION_SLOT_SELECTED;
	}
	frontend_button_draw_eight_slot_navigation_state(
		ui.navigation_slot_states);

	if (ui.navigation_slot_states[CONFIG_NAVIGATION_RESTORE_SLOT] !=
	    FRONTEND_NAVIGATION_SLOT_INACTIVE) {
		frontend_draw_rect_assign(
			&ui.rect, CONFIG_BUTTON_LEFT, CONFIG_RESTORE_BUTTON_TOP,
			CONFIG_BUTTON_RIGHT, CONFIG_RESTORE_BUTTON_BOTTOM);
#ifdef XVT_MODERN
		if (xvt_frontend_action_trigger(
			    XVT_ACTION_OWNER_CONFIG, 1,
			    frontend_button_handle_sprite_button(
				    &ui.rect, "config6u", "config6d",
				    frontend_string_get(
					    FRONTSTR_420_RESTORE_DEFAULTS),
				    CONFIG_BUTTON_FONT_SIZE, 0,
				    CONFIG_RESTORE_HELD_SLOT, "jewelsound"))) {
			int result = frontend_dialog_show_confirm_dialog(
				frontend_string_get(
					FRONTSTR_672_RESTORING_DEFAULTS_WILL_ERASE_ANY_CHANGES),
				frontend_string_get(
					FRONTSTR_673_YOU_HAVE_MADE_TO_THESE_SETTINGS),
				frontend_string_get(
					FRONTSTR_674_ARE_YOU_SURE_YOU_WANT_TO_DO_THIS),
				frontend_string_get(FRONTSTR_523_OKAY),
				frontend_string_get(FRONTSTR_019_CANCEL));
			if (result == XVT_DIALOG_PENDING) {
				return 0;
			}
			xvt_frontend_action_finish(XVT_ACTION_OWNER_CONFIG);
			if (result) {
#else
		if (frontend_button_handle_sprite_button(
			    &ui.rect, "config6u", "config6d",
			    frontend_string_get(FRONTSTR_420_RESTORE_DEFAULTS),
			    CONFIG_BUTTON_FONT_SIZE, 0,
			    CONFIG_RESTORE_HELD_SLOT, "jewelsound") != 0 &&
		    frontend_dialog_show_confirm_dialog(
			    frontend_string_get(
				    FRONTSTR_672_RESTORING_DEFAULTS_WILL_ERASE_ANY_CHANGES),
			    frontend_string_get(
				    FRONTSTR_673_YOU_HAVE_MADE_TO_THESE_SETTINGS),
			    frontend_string_get(
				    FRONTSTR_674_ARE_YOU_SURE_YOU_WANT_TO_DO_THIS),
			    frontend_string_get(FRONTSTR_523_OKAY),
			    frontend_string_get(FRONTSTR_019_CANCEL)) != 0) {
#endif
				switch (g_config_current_page) {
				case CONFIG_PAGE_NETWORK:
					g_game_config.server_update_rate =
						CONFIG_DEFAULT_SERVER_UPDATE_RATE;
#ifdef XVT_MODERN
					g_game_config.network_type =
						NET_TRANSPORT_TCPIP;
					g_game_config.internet_play =
						CONFIG_DEFAULT_ENABLED;
#else
				g_game_config.network_type = NET_TRANSPORT_IPX;
				g_game_config.internet_play =
					CONFIG_DEFAULT_DISABLED;
#endif
					break;
				case CONFIG_PAGE_SINGLEPLAYER_VIDEO:
					g_game_config.backdrop[0] =
						CONFIG_DEFAULT_ENABLED;
					g_game_config.star_density[0] =
						CONFIG_DEFAULT_VIDEO_DENSITY;
					g_game_config.debris[0] =
						CONFIG_DEFAULT_ENABLED;
					g_game_config.local_lights[0] =
						CONFIG_DEFAULT_ENABLED;
					g_game_config.specular[0] =
						CONFIG_DEFAULT_ENABLED;
					g_game_config.diffuse[0] =
						CONFIG_DEFAULT_ENABLED;
					g_game_config.dither[0] =
						CONFIG_DEFAULT_ENABLED;
					g_game_config.texture_res[0] =
						CONFIG_DEFAULT_THREE_CHOICE_HIGH;
					g_game_config.mipmap[0] =
						CONFIG_DEFAULT_VIDEO_QUALITY;
					g_game_config.lod[0] =
						CONFIG_DEFAULT_VIDEO_QUALITY;
					g_game_config.screen_res[0] =
						CONFIG_DEFAULT_THREE_CHOICE_HIGH;
					g_game_config.window_size[0] =
						CONFIG_DEFAULT_THREE_CHOICE_HIGH;
					g_game_config.brightness[0] =
						CONFIG_DEFAULT_BRIGHTNESS;
					g_game_config.color_depth_choice[0] =
						CONFIG_DEFAULT_256_COLORS;
					g_game_config.use3d_hardware[0] =
						frontend_display_is_secondary_direct_draw_active();
					g_game_config.bilinear[0] =
						CONFIG_DEFAULT_ENABLED;
					break;
				case CONFIG_PAGE_MULTIPLAYER_VIDEO:
					g_game_config.backdrop[1] =
						CONFIG_DEFAULT_ENABLED;
					g_game_config.star_density[1] =
						CONFIG_DEFAULT_VIDEO_DENSITY;
					g_game_config.debris[1] =
						CONFIG_DEFAULT_ENABLED;
					g_game_config.local_lights[1] =
						CONFIG_DEFAULT_ENABLED;
					g_game_config.specular[1] =
						CONFIG_DEFAULT_ENABLED;
					g_game_config.diffuse[1] =
						CONFIG_DEFAULT_ENABLED;
					g_game_config.dither[1] =
						CONFIG_DEFAULT_ENABLED;
					g_game_config.texture_res[1] =
						CONFIG_DEFAULT_THREE_CHOICE_HIGH;
					g_game_config.mipmap[1] =
						CONFIG_DEFAULT_VIDEO_QUALITY;
					g_game_config.lod[1] =
						CONFIG_DEFAULT_VIDEO_QUALITY;
					g_game_config.screen_res[1] =
						CONFIG_DEFAULT_THREE_CHOICE_HIGH;
					g_game_config.window_size[1] =
						CONFIG_DEFAULT_THREE_CHOICE_HIGH;
					g_game_config.color_depth_choice[1] =
						CONFIG_DEFAULT_256_COLORS;
					g_game_config.brightness[1] =
						CONFIG_DEFAULT_BRIGHTNESS;
					g_game_config.use3d_hardware[1] =
						CONFIG_DEFAULT_DISABLED;
					g_game_config.bilinear[1] =
						CONFIG_DEFAULT_ENABLED;
					break;
				case CONFIG_PAGE_SOUND:
					previous_datapad_music_enabled =
						g_game_config
							.datapad_music_enabled;
					g_game_config.sfx_exterior_enabled =
						CONFIG_DEFAULT_ENABLED;
					g_game_config.sfx_interior_enabled =
						CONFIG_DEFAULT_ENABLED;
					g_game_config.sfx_engine_enabled =
						CONFIG_DEFAULT_ENABLED;
					g_game_config.sfx_datapad_enabled =
						CONFIG_DEFAULT_ENABLED;
					g_game_config.voice_pilot_level =
						CONFIG_DEFAULT_THREE_CHOICE_HIGH;
					g_game_config
						.voice_tactical_officer_level =
						CONFIG_DEFAULT_THREE_CHOICE_HIGH;
					g_game_config.voice_commander_enabled =
						CONFIG_DEFAULT_ENABLED;
					g_game_config.voice_special_enabled =
						CONFIG_DEFAULT_ENABLED;
					g_game_config.music_enabled =
						CONFIG_DEFAULT_ENABLED;
					g_game_config.datapad_music_enabled =
						CONFIG_DEFAULT_ENABLED;
					g_game_config.sfx_datapad_volume =
						CONFIG_DEFAULT_SOUND_VOLUME;
					g_game_config.sfx_exterior_volume =
						CONFIG_DEFAULT_SOUND_VOLUME;
					g_game_config.sfx_interior_volume =
						CONFIG_DEFAULT_SOUND_VOLUME;
					g_game_config.sfx_engine_volume =
						CONFIG_DEFAULT_SOUND_VOLUME;
					g_game_config.voice_volume =
						CONFIG_DEFAULT_SOUND_VOLUME;
					g_game_config.music_volume =
						CONFIG_DEFAULT_MUSIC_VOLUME;
					if (previous_datapad_music_enabled !=
					    CONFIG_DEFAULT_ENABLED) {
						cd_audio_set_aux_volume(
							CONFIG_DEFAULT_DATAPAD_MUSIC_VOLUME);
						cd_audio_play_track_from_time(
							CONFIG_DEFAULT_DATAPAD_MUSIC_TRACK,
							0, 0);
					}
					break;
				case CONFIG_PAGE_JOYSTICK:
					memset(g_game_config.joy_buttons, 0,
					       sizeof(g_game_config
							      .joy_buttons));
					joystick_button_count =
						joystick_get_button_count(0);
					for (joystick_button_index = 0;
					     joystick_button_index <
						     joystick_button_count &&
					     joystick_button_index <
						     CONFIG_DEFAULT_JOYSTICK_BUTTON_LIMIT;
					     ++joystick_button_index) {
						switch (joystick_button_index) {
						case 0:
							g_game_config.joy_buttons
								[joystick_button_index] =
								(uint8_t)-100;
							break;
						case 1:
							g_game_config.joy_buttons
								[joystick_button_index] =
								(uint8_t)-99;
							break;
						case 2:
							g_game_config.joy_buttons
								[joystick_button_index] =
								114;
							break;
						case 3:
							g_game_config.joy_buttons
								[joystick_button_index] =
								46;
							break;
						case 4:
							g_game_config.joy_buttons
								[joystick_button_index] =
								101;
							break;
						case 5:
							g_game_config.joy_buttons
								[joystick_button_index] =
								105;
							break;
						case 6:
							g_game_config.joy_buttons
								[joystick_button_index] =
								91;
							break;
						case 7:
							g_game_config.joy_buttons
								[joystick_button_index] =
								8;
							break;
						case 8:
							g_game_config.joy_buttons
								[joystick_button_index] =
								13;
							break;
						case 9:
							g_game_config.joy_buttons
								[joystick_button_index] =
								93;
							break;
						}
					}
					joystick_action_index = 0;
					if (g_joystick_entry_count > 0) {
						selected_joystick_action_code =
							g_game_config.joy_buttons
								[g_config_selected_joystick_button_index];
						do {
							if (g_joystick_entries
								    [joystick_action_index]
									    .action_code ==
							    selected_joystick_action_code) {
								g_config_selected_joystick_action_index =
									joystick_action_index;
							}
							++joystick_action_index;
						} while (
							joystick_action_index <
							g_joystick_entry_count);
					}
					break;
				case CONFIG_PAGE_TAUNTS:
					strcpy(g_game_config.taunts[0],
					       frontend_string_get(
						       FRONTSTR_790_STAY_ON_TARGET));
					strcpy(g_game_config.taunts[1],
					       frontend_string_get(
						       FRONTSTR_791_I_CAN_T_SHAKE_HIM));
					strcpy(g_game_config.taunts[2],
					       frontend_string_get(
						       FRONTSTR_792_HE_S_HISTORY));
					strcpy(g_game_config.taunts[3],
					       frontend_string_get(
						       FRONTSTR_793_WOOHOO));
					break;
				}
			}
#ifdef XVT_MODERN
		}
#endif
	}

	frontend_draw_rect_assign(&ui.rect, CONFIG_BUTTON_LEFT,
				  CONFIG_TAUNTS_BUTTON_TOP, CONFIG_BUTTON_RIGHT,
				  CONFIG_TAUNTS_BUTTON_BOTTOM);
	if (g_config_current_page == CONFIG_PAGE_TAUNTS) {
		frontend_button_draw_sprite_and_tooltip(
			&ui.rect, "config8d",
			frontend_string_get(FRONTSTR_794_CUSTOM_TAUNTS),
			CONFIG_BUTTON_FONT_SIZE, 0);
	} else if (frontend_button_handle_sprite_button(
			   &ui.rect, "config8u", "config8u",
			   frontend_string_get(FRONTSTR_794_CUSTOM_TAUNTS),
			   CONFIG_BUTTON_FONT_SIZE, 0, CONFIG_TAUNTS_HELD_SLOT,
			   "jewelsound") != 0) {
		g_config_draw_static_control_background = 1;
		g_config_current_page = CONFIG_PAGE_TAUNTS;
		g_active_text_field_id = 0;
		frontend_display_lock_offscreen_surface();
		front_image_draw_sprite_opaque("backconfig", 0, 0);
		front_image_draw_sprite("frame", 0, 0);
		front_image_draw_sprite("alloff", 0, 0);
		frontend_draw_rect_assign(
			&ui.rect, CONFIG_CONTENT_LEFT, CONFIG_CONTENT_TOP,
			CONFIG_CONTENT_RIGHT, CONFIG_CONTENT_BOTTOM);
		front_image_draw_sprite_translucent("configoverlay", 0, 0);
		frontend_display_unlock_offscreen_surface(1);
	}

	frontend_draw_rect_assign(
		&ui.rect, CONFIG_BUTTON_LEFT, CONFIG_JOYSTICK_BUTTON_TOP,
		CONFIG_BUTTON_RIGHT, CONFIG_JOYSTICK_BUTTON_BOTTOM);
#ifdef XVT_MODERN
	if (frontend_button_handle_sprite_button(
		    &ui.rect, "config5u", "config5u", "OpenXvT Settings",
		    CONFIG_BUTTON_FONT_SIZE, 0, CONFIG_JOYSTICK_HELD_SLOT,
		    "jewelsound")) {
		xvt_port_request_settings();
	}
#else
	if (g_config_current_page == CONFIG_PAGE_JOYSTICK) {
		frontend_button_draw_sprite_and_tooltip(
			&ui.rect, "config5d",
			frontend_string_get(FRONTSTR_415_JOYSTICK_OPTIONS),
			CONFIG_BUTTON_FONT_SIZE, 0);
	} else if (frontend_button_handle_sprite_button(
			   &ui.rect, "config5u", "config5u",
			   frontend_string_get(FRONTSTR_415_JOYSTICK_OPTIONS),
			   CONFIG_BUTTON_FONT_SIZE, 0,
			   CONFIG_JOYSTICK_HELD_SLOT, "jewelsound") != 0) {
		g_config_draw_static_control_background = 1;
		g_config_current_page = CONFIG_PAGE_JOYSTICK;
		frontend_display_lock_offscreen_surface();
		front_image_draw_sprite_opaque("backconfig", 0, 0);
		front_image_draw_sprite("frame", 0, 0);
		front_image_draw_sprite("alloff", 0, 0);
		frontend_draw_rect_assign(
			&ui.rect, CONFIG_CONTENT_LEFT, CONFIG_CONTENT_TOP,
			CONFIG_CONTENT_RIGHT, CONFIG_CONTENT_BOTTOM);
		front_image_draw_sprite_translucent("configoverlay", 0, 0);
		frontend_draw_rect_assign(&ui.rect, CONFIG_JOYSTICK_SHADE_LEFT,
					  CONFIG_JOYSTICK_SHADE_TOP,
					  CONFIG_JOYSTICK_SHADE_RIGHT,
					  CONFIG_JOYSTICK_SHADE_BOTTOM);
		frontend_draw_fill_rect_translucent(
			&ui.rect, 0, 0,
			frontend_display_pack_rgb(0, 0,
						  CONFIG_JOYSTICK_SHADE_BLUE));
		frontend_display_unlock_offscreen_surface(1);
	}
#endif

	frontend_draw_rect_offset_xy(&ui.rect, 0, -CONFIG_BUTTON_VERTICAL_STEP);
	if (g_config_current_page == CONFIG_PAGE_SOUND) {
		frontend_button_draw_sprite_and_tooltip(
			&ui.rect, "config4d",
			frontend_string_get(FRONTSTR_310_SOUND_OPTIONS),
			CONFIG_BUTTON_FONT_SIZE, 0);
	} else if (frontend_button_handle_sprite_button(
			   &ui.rect, "config4u", "config4u",
			   frontend_string_get(FRONTSTR_310_SOUND_OPTIONS),
			   CONFIG_BUTTON_FONT_SIZE, 0, CONFIG_SOUND_HELD_SLOT,
			   "jewelsound") != 0) {
		g_config_draw_static_control_background = 1;
		g_config_current_page = CONFIG_PAGE_SOUND;
		frontend_display_lock_offscreen_surface();
		front_image_draw_sprite_opaque("backconfig", 0, 0);
		front_image_draw_sprite("frame", 0, 0);
		front_image_draw_sprite("alloff", 0, 0);
		frontend_draw_rect_assign(
			&ui.rect, CONFIG_CONTENT_LEFT, CONFIG_CONTENT_TOP,
			CONFIG_CONTENT_RIGHT, CONFIG_CONTENT_BOTTOM);
		front_image_draw_sprite_translucent("configoverlay", 0, 0);
		frontend_display_unlock_offscreen_surface(1);
	}

	frontend_draw_rect_offset_xy(&ui.rect, 0, -CONFIG_BUTTON_VERTICAL_STEP);
	if (g_config_current_page == CONFIG_PAGE_MULTIPLAYER_VIDEO) {
		frontend_button_draw_sprite_and_tooltip(
			&ui.rect, "config3d",
			frontend_string_get(
				FRONTSTR_641_MULTIPLAYER_FLIGHT_ENGINE_OPTIONS),
			CONFIG_BUTTON_FONT_SIZE, 0);
	} else if (
		frontend_button_handle_sprite_button(
			&ui.rect, "config3u", "config3u",
			frontend_string_get(
				FRONTSTR_641_MULTIPLAYER_FLIGHT_ENGINE_OPTIONS),
			CONFIG_BUTTON_FONT_SIZE, 0,
			CONFIG_MULTIPLAYER_VIDEO_HELD_SLOT,
			"jewelsound") != 0) {
		g_config_draw_static_control_background = 1;
		g_config_current_page = CONFIG_PAGE_MULTIPLAYER_VIDEO;
		frontend_display_lock_offscreen_surface();
		front_image_draw_sprite_opaque("backconfig", 0, 0);
		front_image_draw_sprite("frame", 0, 0);
		front_image_draw_sprite("alloff", 0, 0);
		frontend_draw_rect_assign(
			&ui.rect, CONFIG_CONTENT_LEFT, CONFIG_CONTENT_TOP,
			CONFIG_CONTENT_RIGHT, CONFIG_CONTENT_BOTTOM);
		front_image_draw_sprite_translucent("configoverlay", 0, 0);
		frontend_display_unlock_offscreen_surface(1);
	}

	frontend_draw_rect_offset_xy(&ui.rect, 0, -CONFIG_BUTTON_VERTICAL_STEP);
	if (g_config_current_page == CONFIG_PAGE_SINGLEPLAYER_VIDEO) {
		frontend_button_draw_sprite_and_tooltip(
			&ui.rect, "config2d",
			frontend_string_get(
				FRONTSTR_219_SINGLE_PLAYER_FLIGHT_ENGINE_OPTIONS),
			CONFIG_BUTTON_FONT_SIZE, 0);
	} else if (
		frontend_button_handle_sprite_button(
			&ui.rect, "config2u", "config2u",
			frontend_string_get(
				FRONTSTR_219_SINGLE_PLAYER_FLIGHT_ENGINE_OPTIONS),
			CONFIG_BUTTON_FONT_SIZE, 0,
			CONFIG_SINGLEPLAYER_VIDEO_HELD_SLOT,
			"jewelsound") != 0) {
		g_config_draw_static_control_background = 1;
		g_config_current_page = CONFIG_PAGE_SINGLEPLAYER_VIDEO;
		frontend_display_lock_offscreen_surface();
		front_image_draw_sprite_opaque("backconfig", 0, 0);
		front_image_draw_sprite("frame", 0, 0);
		front_image_draw_sprite("alloff", 0, 0);
		frontend_draw_rect_assign(
			&ui.rect, CONFIG_CONTENT_LEFT, CONFIG_CONTENT_TOP,
			CONFIG_CONTENT_RIGHT, CONFIG_CONTENT_BOTTOM);
		front_image_draw_sprite_translucent("configoverlay", 0, 0);
		frontend_display_unlock_offscreen_surface(1);
	}

	frontend_draw_rect_offset_xy(&ui.rect, 0, -CONFIG_BUTTON_VERTICAL_STEP);
	if (g_config_current_page == CONFIG_PAGE_NETWORK) {
		frontend_button_draw_sprite_and_tooltip(
			&ui.rect, "config1d",
			frontend_string_get(
				FRONTSTR_304_MULTIPLAYER_CONNECTION_OPTIONS),
			CONFIG_BUTTON_FONT_SIZE, 0);
	} else if (frontend_button_handle_sprite_button(
			   &ui.rect, "config1u", "config1u",
			   frontend_string_get(
				   FRONTSTR_304_MULTIPLAYER_CONNECTION_OPTIONS),
			   CONFIG_BUTTON_FONT_SIZE, 0, CONFIG_NETWORK_HELD_SLOT,
			   "jewelsound") != 0) {
		keyboard_flush_char_buffer();
		g_config_current_page = CONFIG_PAGE_NETWORK;
		g_active_text_field_id = 0;
		g_config_draw_static_control_background = 1;
		frontend_display_lock_offscreen_surface();
		front_image_draw_sprite_opaque("backconfig", 0, 0);
		front_image_draw_sprite("frame", 0, 0);
		front_image_draw_sprite("alloff", 0, 0);
		frontend_draw_rect_assign(
			&ui.rect, CONFIG_CONTENT_LEFT, CONFIG_CONTENT_TOP,
			CONFIG_CONTENT_RIGHT, CONFIG_CONTENT_BOTTOM);
		front_image_draw_sprite_translucent("configoverlay", 0, 0);
		frontend_display_unlock_offscreen_surface(1);
	}

	return 1;
}

/* Draws the network page. The original build first offers the connection type
 * (IPX, TCP/IP with the address field, direct modem with the phone field,
 * direct serial), which changes only while g_config_connection_type_editable is
 * set or in single player; picking a different type turns internet play on for
 * TCP/IP and off for the others. Both builds then draw the password field and
 * the host options, internet play and the server update rate (4, 6 or 8 as low,
 * medium or high), read-only for a network client. Enter or Tab in a field
 * moves g_active_text_field_id to the next field. */
// FUNCTION: XVT 0x4BB680
void config_network_options_screen(void)
{
	enum {
		TITLE_LEFT = 84,
		TITLE_TOP = 90,
		TITLE_RIGHT = 604,
		TITLE_BOTTOM = 106,
		LABEL_LEFT = 88,
		LABEL_TOP = 111,
		LABEL_RIGHT = 332,
		LABEL_BOTTOM = 125,
		FONT_LABEL = 12,
		FONT_TITLE = 15,
		FIELD_WIDTH = 200
	};

	struct RECT rect;
	struct RECT source_rect;
	int label_width;
#ifndef XVT_MODERN
	int cursor_x;
	int cursor_y;
	int field_label_width;
	int button_width;
#endif
	uint8_t selected_option;

	frontend_draw_rect_assign(&source_rect, TITLE_LEFT, TITLE_TOP,
				  TITLE_RIGHT, TITLE_BOTTOM);
	frontend_text_draw_centered(
		FONT_TITLE,
		frontend_string_get(
			FRONTSTR_304_MULTIPLAYER_CONNECTION_OPTIONS),
		&source_rect, 0xFFFF);
#ifdef XVT_MODERN
	frontend_draw_rect_assign(&source_rect, LABEL_LEFT, LABEL_TOP,
				  LABEL_RIGHT, LABEL_BOTTOM);
#else
	label_width = frontend_text_measure_width(
		frontend_string_get(FRONTSTR_308_IP_ADDRESS_NAME), FONT_LABEL);
	field_label_width = frontend_text_measure_width(
		frontend_string_get(FRONTSTR_309_PHONE_NUMBER), FONT_LABEL);
	if (field_label_width < label_width) {
		field_label_width = label_width;
	}
	front_image_get_resource_rect("offslot", &rect);
	button_width = rect.right - rect.left + 1;
	frontend_cursor_get_pos(&cursor_x, &cursor_y);
	frontend_draw_rect_assign(&source_rect, LABEL_LEFT, LABEL_TOP,
				  LABEL_RIGHT, LABEL_BOTTOM);
	if (g_config_connection_type_editable == 0 &&
	    g_frontend_mission_session_mode !=
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		frontend_text_draw_aligned_in_rect(
			FONT_LABEL,
			frontend_string_get(
				FRONTSTR_692_CONNECTION_TYPE_YOU_CANNOT_CHANGE_THESE_OPTIONS_WHILE_HOSTING_OR_JOINING_A_NETWORK_GAME),
			&source_rect, 0, 1, 0xFFFF);
	} else {
		frontend_text_draw_aligned_in_rect(
			FONT_LABEL,
			frontend_string_get(
				FRONTSTR_477_SELECT_CONNECTION_TYPE),
			&source_rect, 0, 1, 0xFFFF);
	}
	frontend_draw_rect_offset_xy(&source_rect, 0, 20);
	source_rect.right = source_rect.left + 127;
	frontend_draw_rect_copy(&rect, &source_rect);
	rect.right = rect.left + button_width;
	if (g_config_draw_static_control_background != 0 &&
	    (g_config_connection_type_editable != 0 ||
	     g_frontend_mission_session_mode ==
		     FRONTEND_MISSION_SESSION_SINGLEPLAYER)) {
		frontend_display_lock_offscreen_surface();
		front_image_draw_sprite_translucent("offslot", rect.left,
						    rect.top);
		frontend_display_unlock_offscreen_surface(0);
		front_image_draw_sprite_translucent("offslot", rect.left,
						    rect.top);
	}
	if (frontend_draw_point_in_rect(&rect, cursor_x, cursor_y) &&
	    (g_config_connection_type_editable != 0 ||
	     g_frontend_mission_session_mode ==
		     FRONTEND_MISSION_SESSION_SINGLEPLAYER) &&
	    (frontend_mouse_get_left_click() != 0 ||
	     frontend_mouse_get_right_click() != 0)) {
		if (g_game_config.network_type != NET_TRANSPORT_IPX) {
			if (g_game_config.sfx_datapad_enabled != 0) {
				frontend_sound_play_ui_sound(
					"configsound", 1, 0, 255,
					12 * g_game_config.sfx_datapad_volume,
					63);
			}
			if (g_game_config.network_type != NET_TRANSPORT_IPX) {
				g_game_config.internet_play = 0;
			}
		}
		g_game_config.network_type = NET_TRANSPORT_IPX;
	}
	if (g_game_config.network_type == NET_TRANSPORT_IPX) {
		front_image_draw_sprite("3conbtn", rect.left, rect.top);
	}
	rect.left = rect.right + 5;
	rect.right = rect.left + 100;
	frontend_text_draw_aligned_in_rect(
		FONT_LABEL, frontend_string_get(FRONTSTR_305_IPX), &rect, 0, 1,
		g_color_yellow);

	frontend_draw_rect_offset_xy(&source_rect, 0, 25);
	source_rect.right = source_rect.left + 127;
	frontend_draw_rect_copy(&rect, &source_rect);
	rect.right = rect.left + button_width;
	if (g_config_draw_static_control_background != 0 &&
	    (g_config_connection_type_editable != 0 ||
	     g_frontend_mission_session_mode ==
		     FRONTEND_MISSION_SESSION_SINGLEPLAYER)) {
		frontend_display_lock_offscreen_surface();
		front_image_draw_sprite_translucent("offslot", rect.left,
						    rect.top);
		frontend_display_unlock_offscreen_surface(0);
		front_image_draw_sprite_translucent("offslot", rect.left,
						    rect.top);
	}
	if (frontend_draw_point_in_rect(&rect, cursor_x, cursor_y) &&
	    (g_config_connection_type_editable != 0 ||
	     g_frontend_mission_session_mode ==
		     FRONTEND_MISSION_SESSION_SINGLEPLAYER) &&
	    (frontend_mouse_get_left_click() != 0 ||
	     frontend_mouse_get_right_click() != 0)) {
		if (g_game_config.network_type != NET_TRANSPORT_TCPIP) {
			if (g_game_config.sfx_datapad_enabled != 0) {
				frontend_sound_play_ui_sound(
					"configsound", 1, 0, 255,
					12 * g_game_config.sfx_datapad_volume,
					63);
			}
			if (g_game_config.network_type != NET_TRANSPORT_TCPIP) {
				g_game_config.internet_play = 1;
			}
		}
		g_game_config.network_type = NET_TRANSPORT_TCPIP;
	}
	if (g_game_config.network_type == NET_TRANSPORT_TCPIP) {
		front_image_draw_sprite("3conbtn", rect.left, rect.top);
	}
	rect.left = rect.right + 5;
	rect.right = rect.left + 100;
	frontend_text_draw_aligned_in_rect(
		FONT_LABEL, frontend_string_get(FRONTSTR_306_TCP_IP), &rect, 0,
		1, g_color_yellow);
	frontend_draw_rect_offset_xy(&rect, 100, 0);
	frontend_text_draw_aligned_in_rect(
		FONT_LABEL, frontend_string_get(FRONTSTR_308_IP_ADDRESS_NAME),
		&rect, 0, 1, g_color_yellow);
	rect.left += field_label_width + 15;
	rect.right = rect.left + FIELD_WIDTH;
	frontend_draw_rect_inset_xy(&rect, 0, -2);
	frontend_draw_fill_rect_translucent(&rect, 0, 0,
					    g_editable_field_background_color);
	if (frontend_text_handle_editable_field(&rect, g_game_config.ip_address,
						64, 0, FONT_LABEL, NULL) != 0) {
		g_active_text_field_id = 1;
	}

	frontend_draw_rect_offset_xy(&source_rect, 0, 25);
	source_rect.right = source_rect.left + 127;
	frontend_draw_rect_copy(&rect, &source_rect);
	rect.right = rect.left + button_width;
	if (g_config_draw_static_control_background != 0 &&
	    (g_config_connection_type_editable != 0 ||
	     g_frontend_mission_session_mode ==
		     FRONTEND_MISSION_SESSION_SINGLEPLAYER)) {
		frontend_display_lock_offscreen_surface();
		front_image_draw_sprite_translucent("offslot", rect.left,
						    rect.top);
		frontend_display_unlock_offscreen_surface(0);
		front_image_draw_sprite_translucent("offslot", rect.left,
						    rect.top);
	}
	if (frontend_draw_point_in_rect(&rect, cursor_x, cursor_y) &&
	    (g_config_connection_type_editable != 0 ||
	     g_frontend_mission_session_mode ==
		     FRONTEND_MISSION_SESSION_SINGLEPLAYER) &&
	    (frontend_mouse_get_left_click() != 0 ||
	     frontend_mouse_get_right_click() != 0)) {
		if (g_game_config.network_type != NET_TRANSPORT_MODEM) {
			if (g_game_config.sfx_datapad_enabled != 0) {
				frontend_sound_play_ui_sound(
					"configsound", 1, 0, 255,
					12 * g_game_config.sfx_datapad_volume,
					63);
			}
			if (g_game_config.network_type != NET_TRANSPORT_MODEM) {
				g_game_config.internet_play = 0;
			}
		}
		g_game_config.network_type = NET_TRANSPORT_MODEM;
	}
	if (g_game_config.network_type == NET_TRANSPORT_MODEM) {
		front_image_draw_sprite("3conbtn", rect.left, rect.top);
	}
	rect.left = rect.right + 5;
	rect.right = rect.left + 100;
	frontend_text_draw_aligned_in_rect(
		FONT_LABEL, frontend_string_get(FRONTSTR_307_DIRECT_MODEM),
		&rect, 0, 1, g_color_yellow);
	frontend_draw_rect_offset_xy(&rect, 100, 0);
	frontend_text_draw_aligned_in_rect(
		FONT_LABEL, frontend_string_get(FRONTSTR_309_PHONE_NUMBER),
		&rect, 0, 1, g_color_yellow);
	rect.left += field_label_width + 15;
	rect.right = rect.left + FIELD_WIDTH;
	frontend_draw_rect_inset_xy(&rect, 0, -2);
	frontend_draw_fill_rect_translucent(&rect, 0, 0,
					    g_editable_field_background_color);
	if (frontend_text_handle_editable_field(&rect,
						g_game_config.phone_number, 64,
						1, FONT_LABEL, NULL) != 0) {
		g_active_text_field_id = 2;
	}

	frontend_draw_rect_offset_xy(&source_rect, 0, 25);
	source_rect.right = source_rect.left + 127;
	frontend_draw_rect_copy(&rect, &source_rect);
	/* From here button_width holds the serial slot's right edge, not a width. */
	button_width += rect.left;
	rect.right = button_width;
	if (g_config_draw_static_control_background != 0 &&
	    (g_config_connection_type_editable != 0 ||
	     g_frontend_mission_session_mode ==
		     FRONTEND_MISSION_SESSION_SINGLEPLAYER)) {
		frontend_display_lock_offscreen_surface();
		front_image_draw_sprite_translucent("offslot", rect.left,
						    rect.top);
		frontend_display_unlock_offscreen_surface(0);
		front_image_draw_sprite_translucent("offslot", rect.left,
						    rect.top);
	}
	if (frontend_draw_point_in_rect(&rect, cursor_x, cursor_y) &&
	    (g_config_connection_type_editable != 0 ||
	     g_frontend_mission_session_mode ==
		     FRONTEND_MISSION_SESSION_SINGLEPLAYER) &&
	    (frontend_mouse_get_left_click() != 0 ||
	     frontend_mouse_get_right_click() != 0)) {
		if (g_game_config.network_type != NET_TRANSPORT_SERIAL) {
			if (g_game_config.sfx_datapad_enabled != 0) {
				frontend_sound_play_ui_sound(
					"configsound", 1, 0, 255,
					12 * g_game_config.sfx_datapad_volume,
					63);
			}
			if (g_game_config.network_type !=
			    NET_TRANSPORT_SERIAL) {
				g_game_config.internet_play = 0;
			}
		}
		g_game_config.network_type = NET_TRANSPORT_SERIAL;
	}
	if (g_game_config.network_type == NET_TRANSPORT_SERIAL) {
		front_image_draw_sprite("3conbtn", rect.left, rect.top);
	}
	rect.left = rect.right + 5;
	rect.right = rect.left + 100;
	frontend_text_draw_aligned_in_rect(
		FONT_LABEL, frontend_string_get(FRONTSTR_449_DIRECT_SERIAL),
		&rect, 0, 1, g_color_yellow);

#endif
	frontend_draw_rect_offset_xy(&source_rect, 0, 35);
	frontend_text_draw_aligned_in_rect(
		FONT_LABEL,
		frontend_string_get(FRONTSTR_478_GENERAL_CONNECTION_OPTIONS),
		&source_rect, 0, 1, 0xFFFF);
	frontend_draw_rect_offset_xy(&source_rect, 0, 20);
	frontend_draw_rect_copy(&rect, &source_rect);
	frontend_text_draw_aligned_in_rect(
		FONT_LABEL,
		frontend_string_get(FRONTSTR_482_GAME_SESSION_PASSWORD), &rect,
		0, 1, g_color_yellow);
	label_width = frontend_text_measure_width(
		frontend_string_get(FRONTSTR_482_GAME_SESSION_PASSWORD),
		FONT_LABEL);
	rect.left += label_width + 15;
	rect.right = rect.left + FIELD_WIDTH;
	frontend_draw_rect_inset_xy(&rect, 0, -2);
	frontend_draw_fill_rect_translucent(&rect, 0, 0,
					    g_editable_field_background_color);
	if (frontend_text_handle_editable_field(&rect, g_game_config.password,
						16, 2, FONT_LABEL, NULL) != 0) {
		g_active_text_field_id = 0;
	}

	frontend_draw_rect_offset_xy(&source_rect, 0, 35);
	if (g_frontend_mission_session_mode ==
	    FRONTEND_MISSION_SESSION_NET_CLIENT) {
		frontend_text_draw_aligned_in_rect(
			FONT_LABEL,
			frontend_string_get(
				FRONTSTR_747_HOST_OPTIONS_YOU_ARE_A_CLIENT_YOU_CANNOT_CHANGE_THESE_SETTINGS),
			&source_rect, 0, 1, 0xFFFF);
		frontend_draw_rect_offset_xy(&source_rect, 0, 20);
		frontend_text_draw_aligned_in_rect(
			FONT_LABEL,
			frontend_string_get(
				FRONTSTR_479_PLAYING_OVER_THE_INTERNET),
			&source_rect, 0, 1, g_color_yellow);
		frontend_draw_rect_offset_xy(&source_rect, 0, 17);
		config_draw_two_choice_option_read_only_opaque(
			&g_game_config.internet_play, &source_rect,
			FRONTSTR_480_NO);
		frontend_draw_rect_offset_xy(&source_rect, 0, 20);
		frontend_text_draw_aligned_in_rect(
			FONT_LABEL,
			frontend_string_get(
				FRONTSTR_743_HOST_SERVER_UPDATE_RATE),
			&source_rect, 0, 1, g_color_yellow);
		frontend_draw_rect_offset_xy(&source_rect, 0, 17);
		selected_option =
			(uint8_t)((g_game_config.server_update_rate >> 1) - 2);
		config_draw_three_choice_option_read_only(
			&selected_option, &source_rect, FRONTSTR_744_LOW);
	} else {
		frontend_text_draw_aligned_in_rect(
			FONT_LABEL,
			frontend_string_get(FRONTSTR_742_HOST_SERVER_OPTIONS),
			&source_rect, 0, 1, 0xFFFF);
		frontend_draw_rect_offset_xy(&source_rect, 0, 20);
		frontend_text_draw_aligned_in_rect(
			FONT_LABEL,
			frontend_string_get(
				FRONTSTR_479_PLAYING_OVER_THE_INTERNET),
			&source_rect, 0, 1, g_color_yellow);
		frontend_draw_rect_offset_xy(&source_rect, 0, 17);
		config_draw_two_choice_option(&g_game_config.internet_play,
					      &source_rect, FRONTSTR_480_NO);
		frontend_draw_rect_offset_xy(&source_rect, 0, 20);
		frontend_text_draw_aligned_in_rect(
			FONT_LABEL,
			frontend_string_get(
				FRONTSTR_743_HOST_SERVER_UPDATE_RATE),
			&source_rect, 0, 1, g_color_yellow);
		frontend_draw_rect_offset_xy(&source_rect, 0, 17);
		selected_option =
			(uint8_t)((g_game_config.server_update_rate >> 1) - 2);
		config_draw_three_choice_option(&selected_option, &source_rect,
						FRONTSTR_744_LOW);
		g_game_config.server_update_rate =
			(uint8_t)(2 * selected_option + 4);
	}
}

/* The two-choice control of config_draw_two_choice_option_impl with an opaque
 * marker, taking no clicks; config_draw_two_choice_option_read_only takes none
 * either but draws the marker translucent. The network page uses it for a
 * client's internet play setting. */
// FUNCTION: XVT 0x4BC1C0
void config_draw_two_choice_option_read_only_opaque(
	const uint8_t *value, const struct RECT *rect,
	frontend_string_id value_base_str_id)
{
	int option_index;
	struct RECT slot_rect;
	struct RECT sprite_rect;
	int cursor_x;
	int cursor_y;

	frontend_cursor_get_pos(&cursor_x, &cursor_y);
	front_image_get_resource_rect("offslot", &sprite_rect);
	frontend_draw_rect_copy(&slot_rect, rect);
	for (option_index = 0; option_index < 2; ++option_index) {
		slot_rect.right = slot_rect.left + sprite_rect.right -
				  sprite_rect.left + 1;
		if (g_config_draw_static_control_background != 0) {
			frontend_display_lock_offscreen_surface();
			if (option_index == 0) {
				front_image_draw_sprite_translucent(
					"offslot", slot_rect.left,
					slot_rect.top);
			} else {
				front_image_draw_sprite_translucent(
					"onslot", slot_rect.left,
					slot_rect.top);
			}
			frontend_display_unlock_offscreen_surface(0);
			if (option_index == 0) {
				front_image_draw_sprite_translucent(
					"offslot", slot_rect.left,
					slot_rect.top);
			} else {
				front_image_draw_sprite_translucent(
					"onslot", slot_rect.left,
					slot_rect.top);
			}
		}

		if (*value == option_index) {
			front_image_draw_sprite("3conbtn", slot_rect.left,
						slot_rect.top);
		}
		slot_rect.left = slot_rect.right + 5;
		slot_rect.right = slot_rect.left + 100;
		frontend_text_draw_aligned_in_rect(
			12,
			frontend_string_get(value_base_str_id + option_index),
			&slot_rect, 0, 1, g_color_yellow);
		slot_rect.left = rect->left + 130;
	}
}

/* The three-choice control of config_draw_three_choice_option, taking no
 * clicks. */
// FUNCTION: XVT 0x4BC2F0
void config_draw_three_choice_option_read_only(
	const uint8_t *selected_option, const struct RECT *bar_rect,
	frontend_string_id first_option_string_id)
{
	struct RECT option_rect;
	struct RECT sprite_rect;
	int cursor_x;
	int cursor_y;
	int button_width;
	int right;

	if (g_config_draw_static_control_background != 0) {
		frontend_display_lock_offscreen_surface();
		front_image_draw_sprite_translucent("3conbar", bar_rect->left,
						    bar_rect->top);
		frontend_display_unlock_offscreen_surface(0);
		front_image_draw_sprite_translucent("3conbar", bar_rect->left,
						    bar_rect->top);
	}

	frontend_cursor_get_pos(&cursor_x, &cursor_y);
	front_image_get_resource_rect("3conbtn", &sprite_rect);
	button_width = sprite_rect.right - sprite_rect.left + 1;
	front_image_get_resource_rect("3conbar", &sprite_rect);
	frontend_draw_rect_offset_xy(&sprite_rect, bar_rect->left,
				     bar_rect->top);
	frontend_draw_rect_copy(&option_rect, bar_rect);

	option_rect.right = option_rect.left + button_width;
	if (*selected_option == 0) {
		front_image_draw_sprite("3conbtn", option_rect.left,
					option_rect.top);
	}
	frontend_draw_rect_offset_xy(&option_rect, 0, 15);
	frontend_text_draw_aligned_in_rect(
		12, frontend_string_get(first_option_string_id), &option_rect,
		0, 1, g_color_yellow);
	frontend_draw_rect_offset_xy(&option_rect, 0, -15);

	option_rect.left =
		sprite_rect.left +
		((sprite_rect.right - button_width - sprite_rect.left) >> 1) +
		1;
	option_rect.right = option_rect.left + button_width;
	if (*selected_option == 1) {
		front_image_draw_sprite("3conbtn", option_rect.left,
					option_rect.top);
	}
	frontend_draw_rect_offset_xy(&option_rect, 0, 15);
	frontend_text_draw_centered(
		12, frontend_string_get(first_option_string_id + 1),
		&option_rect, g_color_yellow);
	frontend_draw_rect_offset_xy(&option_rect, 0, -15);

	option_rect.right = sprite_rect.right;
	option_rect.left = sprite_rect.right - button_width + 1;
	if (*selected_option == 2) {
		front_image_draw_sprite("3conbtn", option_rect.left,
					option_rect.top);
	}
	frontend_draw_rect_offset_xy(&option_rect, 0, 15);
	right = sprite_rect.right;
	option_rect.left =
		right -
		frontend_text_measure_width(
			frontend_string_get(first_option_string_id + 2), 12) +
		1;
	frontend_text_draw_aligned_in_rect(
		12, frontend_string_get(first_option_string_id + 2),
		&option_rect, 0, 1, g_color_yellow);
}

/* Draws the sound page: switches and 10-position volume sliders for frontend,
 * exterior, cockpit and engine sounds; the pilot and tactical officer message
 * levels; commander and special mission messages; voice volume; frontend music;
 * and flight music with its volume. A moved volume slider plays "configsound"
 * at the new volume when frontend sounds are on. Turning frontend music on
 * plays CD track 7 at the music volume, off stops it, and a music volume change
 * sets the CD volume while frontend music is on. */
// FUNCTION: XVT 0x4BC520
void config_sound_options_screen(void)
{
	struct RECT rect;
	int previous_volume;
	int previous_datapad_music;

	frontend_draw_rect_assign(&rect, 84, 90, 604, 106);
	frontend_text_draw_centered(
		15, frontend_string_get(FRONTSTR_310_SOUND_OPTIONS), &rect,
		0xFFFF);

	frontend_draw_rect_assign(&rect, 88, 111, 332, 125);
	frontend_text_draw_aligned_in_rect(
		12, frontend_string_get(FRONTSTR_408_DATAPAD_SFX), &rect, 0, 1,
		0xFFFF);
	frontend_draw_rect_offset_xy(&rect, 0, 15);
	config_draw_two_choice_option(&g_game_config.sfx_datapad_enabled, &rect,
				      FRONTSTR_236_OFF);
	frontend_draw_rect_offset_xy(&rect, 0, 15);
	previous_volume = g_game_config.sfx_datapad_volume;
	config_draw_option_slider(&g_game_config.sfx_datapad_volume, &rect, 10,
				  FRONTSTR_403_VOLUME_LOW, 0);
	if (g_game_config.sfx_datapad_volume != previous_volume &&
	    g_game_config.sfx_datapad_enabled != 0) {
		frontend_sound_play_ui_sound(
			"configsound", 1, 0, 255,
			12 * g_game_config.sfx_datapad_volume, 63);
	}
	frontend_draw_rect_offset_xy(&rect, 0, 55);

	frontend_text_draw_aligned_in_rect(
		12,
		frontend_string_get(FRONTSTR_405_FLIGHT_ENGINE_EXTERIOR_SFX),
		&rect, 0, 1, 0xFFFF);
	frontend_draw_rect_offset_xy(&rect, 0, 15);
	config_draw_two_choice_option(&g_game_config.sfx_exterior_enabled,
				      &rect, FRONTSTR_236_OFF);
	frontend_draw_rect_offset_xy(&rect, 0, 15);
	previous_volume = g_game_config.sfx_exterior_volume;
	config_draw_option_slider(&g_game_config.sfx_exterior_volume, &rect, 10,
				  FRONTSTR_403_VOLUME_LOW, 0);
	if (g_game_config.sfx_exterior_volume != previous_volume &&
	    g_game_config.sfx_datapad_enabled != 0) {
		frontend_sound_play_ui_sound(
			"configsound", 1, 0, 255,
			12 * g_game_config.sfx_exterior_volume, 63);
	}
	frontend_draw_rect_offset_xy(&rect, 0, 55);

	frontend_text_draw_aligned_in_rect(
		12, frontend_string_get(FRONTSTR_406_COCKPIT_INTERIOR_SFX),
		&rect, 0, 1, 0xFFFF);
	frontend_draw_rect_offset_xy(&rect, 0, 15);
	config_draw_two_choice_option(&g_game_config.sfx_interior_enabled,
				      &rect, FRONTSTR_236_OFF);
	frontend_draw_rect_offset_xy(&rect, 0, 15);
	previous_volume = g_game_config.sfx_interior_volume;
	config_draw_option_slider(&g_game_config.sfx_interior_volume, &rect, 10,
				  FRONTSTR_403_VOLUME_LOW, 0);
	if (g_game_config.sfx_interior_volume != previous_volume &&
	    g_game_config.sfx_datapad_enabled != 0) {
		frontend_sound_play_ui_sound(
			"configsound", 1, 0, 255,
			12 * g_game_config.sfx_interior_volume, 63);
	}
	frontend_draw_rect_offset_xy(&rect, 0, 55);

	frontend_text_draw_aligned_in_rect(
		12, frontend_string_get(FRONTSTR_407_ENGINE_SOUND), &rect, 0, 1,
		0xFFFF);
	frontend_draw_rect_offset_xy(&rect, 0, 15);
	config_draw_two_choice_option(&g_game_config.sfx_engine_enabled, &rect,
				      FRONTSTR_236_OFF);
	frontend_draw_rect_offset_xy(&rect, 0, 15);
	previous_volume = g_game_config.sfx_engine_volume;
	config_draw_option_slider(&g_game_config.sfx_engine_volume, &rect, 10,
				  FRONTSTR_403_VOLUME_LOW, 0);
	if (g_game_config.sfx_engine_volume != previous_volume &&
	    g_game_config.sfx_datapad_enabled != 0) {
		frontend_sound_play_ui_sound(
			"configsound", 1, 0, 255,
			12 * g_game_config.sfx_engine_volume, 63);
	}

	frontend_draw_rect_assign(&rect, 356, 111, 600, 125);
	frontend_text_draw_aligned_in_rect(
		12, frontend_string_get(FRONTSTR_409_PILOT_MESSAGES), &rect, 0,
		1, 0xFFFF);
	frontend_draw_rect_offset_xy(&rect, 0, 15);
	config_draw_three_choice_option(&g_game_config.voice_pilot_level, &rect,
					FRONTSTR_400_OFF);
	frontend_draw_rect_offset_xy(&rect, 0, 35);
	frontend_text_draw_aligned_in_rect(
		12, frontend_string_get(FRONTSTR_410_TACTICAL_OFFICER_MESSAGES),
		&rect, 0, 1, 0xFFFF);
	frontend_draw_rect_offset_xy(&rect, 0, 15);
	config_draw_three_choice_option(
		&g_game_config.voice_tactical_officer_level, &rect,
		FRONTSTR_400_OFF);
	frontend_draw_rect_offset_xy(&rect, 0, 35);
	frontend_text_draw_aligned_in_rect(
		12, frontend_string_get(FRONTSTR_411_COMMANDER_MESSAGES), &rect,
		0, 1, 0xFFFF);
	frontend_draw_rect_offset_xy(&rect, 0, 15);
	config_draw_two_choice_option(&g_game_config.voice_commander_enabled,
				      &rect, FRONTSTR_236_OFF);
	frontend_draw_rect_offset_xy(&rect, 0, 20);
	frontend_text_draw_aligned_in_rect(
		12, frontend_string_get(FRONTSTR_412_SPECIAL_MISSION_MESSAGES),
		&rect, 0, 1, 0xFFFF);
	frontend_draw_rect_offset_xy(&rect, 0, 15);
	config_draw_two_choice_option(&g_game_config.voice_special_enabled,
				      &rect, FRONTSTR_236_OFF);
	frontend_draw_rect_offset_xy(&rect, 0, 25);
	frontend_text_draw_aligned_in_rect(
		12, frontend_string_get(FRONTSTR_414_VOICE_VOLUME), &rect, 0, 1,
		0xFFFF);
	frontend_draw_rect_offset_xy(&rect, 0, 15);
	previous_volume = g_game_config.voice_volume;
	config_draw_option_slider(&g_game_config.voice_volume, &rect, 10,
				  FRONTSTR_403_VOLUME_LOW, 0);
	if (g_game_config.voice_volume != previous_volume &&
	    g_game_config.sfx_datapad_enabled != 0) {
		frontend_sound_play_ui_sound("configsound", 1, 0, 255,
					     12 * g_game_config.voice_volume,
					     63);
	}

	frontend_draw_rect_offset_xy(&rect, 0, 35);
	previous_datapad_music = g_game_config.datapad_music_enabled;
	frontend_text_draw_aligned_in_rect(
		12, frontend_string_get(FRONTSTR_735_DATAPAD_MUSIC), &rect, 0,
		1, 0xFFFF);
	frontend_draw_rect_offset_xy(&rect, 0, 15);
	config_draw_two_choice_option(&g_game_config.datapad_music_enabled,
				      &rect, FRONTSTR_236_OFF);
	if (g_game_config.datapad_music_enabled != previous_datapad_music) {
		if (g_game_config.datapad_music_enabled != 0) {
			cd_audio_set_aux_volume(0xFFFF *
						g_game_config.music_volume / 9);
			cd_audio_play_track_from_time(7, 0, 0);
		} else {
			cd_audio_stop_current_track();
		}
	}
	frontend_draw_rect_offset_xy(&rect, 0, 15);
	frontend_text_draw_aligned_in_rect(
		12, frontend_string_get(FRONTSTR_413_FLIGHT_ENGINE_MUSIC),
		&rect, 0, 1, 0xFFFF);
	frontend_draw_rect_offset_xy(&rect, 0, 15);
	config_draw_two_choice_option(&g_game_config.music_enabled, &rect,
				      FRONTSTR_236_OFF);
	previous_volume = g_game_config.music_volume;
	frontend_draw_rect_offset_xy(&rect, 0, 15);
	config_draw_option_slider(&g_game_config.music_volume, &rect, 10,
				  FRONTSTR_403_VOLUME_LOW, 1);
	if (g_game_config.music_volume != previous_volume &&
	    g_game_config.datapad_music_enabled != 0) {
		cd_audio_set_aux_volume(0xFFFF * g_game_config.music_volume /
					9);
	}
}

/* Draws the joystick page and remaps buttons; only the original build shows
 * this page. Shows the selected button or hat direction and its action. A
 * pressed joystick button (1 to 16) or hat direction selects itself and scrolls
 * into view. The left list shows each button, and the four hat directions when
 * the joystick has a hat, with its action; a click selects that row. The right
 * list shows every action; a click maps the selected button to it, and so does
 * a key from config_read_joystick_action_picker_key that matches an action's
 * code. */
// FUNCTION: XVT 0x4BCC10
void config_joystick_remap_screen(void)
{
	struct RECT rect;
	int pressed_button;
	int pov_direction;
	int cursor_x;
	int cursor_y;
	int button_count;
	int remappable_row_count;

	frontend_cursor_get_pos(&cursor_x, &cursor_y);
	frontend_draw_rect_assign(&rect, 84, 90, 604, 106);
	frontend_text_draw_centered(
		15, frontend_string_get(FRONTSTR_415_JOYSTICK_OPTIONS), &rect,
		0xFFFF);

	frontend_draw_rect_assign(&rect, 88, 111, 322, 125);
	{
		int selected_button = g_config_selected_joystick_button_index;
		if (selected_button >= 16) {
			sprintf(g_frontend_scratch_buffer, "%c%s %s %c%s", 4,
				frontend_string_get(FRONTSTR_646_JOYSTICK_POV),
				frontend_string_get(FRONTSTR_647_UP +
						    selected_button - 16),
				1,
				frontend_string_get(
					FRONTSTR_417_CURRENTLY_MAPPED_TO));
		} else {
			sprintf(g_frontend_scratch_buffer, "%c%s %d %c%s", 4,
				frontend_string_get(
					FRONTSTR_416_JOYSTICK_BUTTON),
				selected_button + 1, 1,
				frontend_string_get(
					FRONTSTR_417_CURRENTLY_MAPPED_TO));
		}
	}
	frontend_text_draw_aligned_in_rect(12, g_frontend_scratch_buffer, &rect,
					   0, 1, 0xFFFF);
	frontend_draw_rect_offset_xy(&rect, 0, 15);
	if (g_joystick_entry_count != 0) {
		int selected_action = g_config_selected_joystick_action_index;
		if (selected_action != 0) {
			sprintf(g_frontend_scratch_buffer, "%c%s: %c%s", 2,
				g_joystick_entries[selected_action].name, 1,
				g_joystick_entries[selected_action]
					.description);
		} else {
			sprintf(g_frontend_scratch_buffer, "%c%s", 2,
				g_joystick_entries[selected_action].name);
		}
		frontend_text_draw_aligned_in_rect(
			12, g_frontend_scratch_buffer, &rect, 0, 1, 0xFFFF);
	}

	frontend_draw_rect_offset_xy(&rect, 0, 30);
	frontend_text_draw_aligned_in_rect(
		12,
		frontend_string_get(
			FRONTSTR_653_SELECT_A_BUTTON_BY_PICKING_FROM),
		&rect, 0, 1, g_color_green);
	frontend_draw_rect_offset_xy(&rect, 0, 15);
	frontend_text_draw_aligned_in_rect(
		12,
		frontend_string_get(
			FRONTSTR_654_THE_LIST_BELOW_OR_PRESSING_ONE_OF),
		&rect, 0, 1, g_color_green);
	frontend_draw_rect_offset_xy(&rect, 0, 15);
	frontend_text_draw_aligned_in_rect(
		12,
		frontend_string_get(
			FRONTSTR_655_YOUR_JOYSTICK_BUTTONS_THEN_PICK),
		&rect, 0, 1, g_color_green);
	frontend_draw_rect_offset_xy(&rect, 0, 15);
	frontend_text_draw_aligned_in_rect(
		12,
		frontend_string_get(
			FRONTSTR_656_FROM_THE_LIST_OF_AVAILABLE_KEYS),
		&rect, 0, 1, g_color_green);
	frontend_draw_rect_offset_xy(&rect, 0, 15);
	frontend_text_draw_aligned_in_rect(
		12, frontend_string_get(FRONTSTR_657_TO_REMAP), &rect, 0, 1,
		g_color_green);

	{
		pov_direction = joystick_get_pov_direction(0);
		pressed_button = joystick_get_first_pressed_button(0);
		if ((pressed_button != -1 && pressed_button < 16) ||
		    pov_direction != 0) {
			if (pressed_button != -1) {
				int scroll_offset;
				if (g_config_selected_joystick_button_index !=
					    pressed_button &&
				    g_game_config.sfx_datapad_enabled != 0) {
					frontend_sound_play_ui_sound(
						"configsound", 1, 0, 255,
						12 * g_game_config
								.sfx_datapad_volume,
						63);
				}
				scroll_offset =
					g_config_joystick_button_scroll_offset;
				g_config_selected_joystick_button_index =
					pressed_button;
				if (pressed_button < scroll_offset ||
				    pressed_button - scroll_offset >= 10) {
					g_config_joystick_button_scroll_offset =
						pressed_button;
				}
			} else if (pov_direction != 0) {
				if (pov_direction - g_config_selected_joystick_button_index !=
					    -15 &&
				    g_game_config.sfx_datapad_enabled != 0) {
					frontend_sound_play_ui_sound(
						"configsound", 1, 0, 255,
						12 * g_game_config
								.sfx_datapad_volume,
						63);
				}
				g_config_selected_joystick_button_index =
					pov_direction + 15;
				{
					int pov_list_index =
						pov_direction +
						joystick_get_button_count(0) -
						1;
					int scroll_offset =
						g_config_joystick_button_scroll_offset;
					if (pov_list_index < scroll_offset ||
					    pov_list_index - scroll_offset >=
						    10) {
						g_config_joystick_button_scroll_offset =
							pov_list_index;
					}
				}
			}
			{
				int entry_index = 0;
				int entry_count = g_joystick_entry_count;
				if (entry_count > entry_index) {
					struct joystick_entry *entry =
						g_joystick_entries;
					uint8_t action_code =
						g_game_config.joy_buttons
							[g_config_selected_joystick_button_index];
					do {
						if (entry->action_code ==
						    action_code) {
							g_config_selected_joystick_action_index =
								entry_index;
						}
						++entry;
						++entry_index;
					} while (entry_index < entry_count);
				}
			}
		}
	}

	button_count = joystick_get_button_count(0);
	remappable_row_count = button_count;
	if (joystick_has_pov(0) != 0) {
		remappable_row_count += 4;
	}

	frontend_draw_rect_assign(&rect, 88, 246, 322, 260);
	frontend_text_draw_centered(
		12, frontend_string_get(FRONTSTR_652_REMAPPABLE_BUTTONS), &rect,
		g_color_yellow);
	if (remappable_row_count > 10) {
		frontend_draw_rect_assign(&rect, 313, 261, 322, 415);
		g_config_joystick_button_scroll_offset =
			frontend_scrollbar_draw(
				&rect, g_config_joystick_button_scroll_offset,
				remappable_row_count, 0, 5,
				(unsigned int)g_color_navy, 1);
		frontend_draw_rect_assign(&rect, 88, 261, 312, 415);
	} else {
		frontend_draw_rect_assign(&rect, 88, 261, 322, 415);
	}
	frontend_draw_rect(&rect, 0, 0, 0xFFFF, 0);
	frontend_draw_rect_inset_xy(&rect, 2, 2);
	frontend_draw_rect(&rect, 0, 0, 0, 0);
	rect.bottom = rect.top + 14;
	frontend_draw_rect_inset_xy(&rect, 2, 0);

	{
		int list_row_index;
		int entry_count;
		for (list_row_index = g_config_joystick_button_scroll_offset;
		     list_row_index <
		     g_config_joystick_button_scroll_offset + 10;
		     ++list_row_index) {
			int action_index;
			uint16_t color;
			entry_count = g_joystick_entry_count;
			if (list_row_index >= remappable_row_count) {
				break;
			}
			action_index = 0;
			if (entry_count > 0) {
				struct joystick_entry *entries =
					g_joystick_entries;
				for (; action_index < entry_count;
				     ++action_index) {
					if (list_row_index < button_count) {
						if (g_game_config.joy_buttons
							    [list_row_index] ==
						    entries[action_index]
							    .action_code) {
							break;
						}
					} else if (g_game_config.joy_buttons
							   [list_row_index -
							    button_count +
							    16] ==
						   entries[action_index]
							   .action_code) {
						break;
					}
				}
			}
			if (action_index < entry_count) {
				if (frontend_draw_point_in_rect(
					    &rect, cursor_x, cursor_y) != 0) {
					frontend_draw_rect_outline(
						&rect, 0, 0, g_color_green);
					if (frontend_mouse_get_left_click() !=
						    0 ||
					    frontend_mouse_get_right_click() !=
						    0) {
						if (list_row_index <
						    button_count) {
							if (g_config_selected_joystick_button_index !=
								    list_row_index &&
							    g_game_config.sfx_datapad_enabled !=
								    0) {
								frontend_sound_play_ui_sound(
									"configsound",
									1, 0,
									255,
									12 * g_game_config
											.sfx_datapad_volume,
									63);
							}
							g_config_selected_joystick_button_index =
								list_row_index;
						} else {
							if (list_row_index +
									    button_count -
									    g_config_selected_joystick_button_index !=
								    16 &&
							    g_game_config.sfx_datapad_enabled !=
								    0) {
								frontend_sound_play_ui_sound(
									"configsound",
									1, 0,
									255,
									12 * g_game_config
											.sfx_datapad_volume,
									63);
							}
							g_config_selected_joystick_button_index =
								list_row_index -
								button_count +
								16;
						}
						g_config_selected_joystick_action_index =
							action_index;
					}
				}
				if (list_row_index < button_count) {
					color = 0xFFFF;
					if (g_config_selected_joystick_button_index ==
					    list_row_index) {
						color = (uint16_t)
							g_color_yellow;
					}
					sprintf(g_frontend_scratch_buffer,
						"%s %d: %c%s",
						frontend_string_get(
							FRONTSTR_416_JOYSTICK_BUTTON),
						list_row_index + 1, 2,
						g_joystick_entries[action_index]
							.name);
					frontend_text_draw_aligned_in_rect(
						12, g_frontend_scratch_buffer,
						&rect, 0, 1, color);
				} else {
					color = 0xFFFF;
					if (list_row_index - button_count -
						    g_config_selected_joystick_button_index ==
					    -16) {
						color = (uint16_t)
							g_color_yellow;
					}
					sprintf(g_frontend_scratch_buffer,
						"%s %s: %c%s",
						frontend_string_get(
							FRONTSTR_646_JOYSTICK_POV),
						frontend_string_get(
							FRONTSTR_647_UP +
							list_row_index -
							button_count),
						2,
						g_joystick_entries[action_index]
							.name);
					frontend_text_draw_aligned_in_rect(
						12, g_frontend_scratch_buffer,
						&rect, 0, 1, color);
				}
			}
			frontend_draw_rect_offset_xy(&rect, 0, 15);
		}
	}

	frontend_draw_rect_assign(&rect, 330, 111, 590, 125);
	frontend_text_draw_centered(
		12, frontend_string_get(FRONTSTR_651_AVAILABLE_KEYS), &rect,
		g_color_yellow);
	frontend_draw_rect_assign(&rect, 591, 126, 600, 415);
	g_config_joystick_action_scroll_offset = frontend_scrollbar_draw(
		&rect, g_config_joystick_action_scroll_offset,
		g_joystick_entry_count, 0, 5, (unsigned int)g_color_navy, 2);
	frontend_draw_rect_assign(&rect, 330, 126, 590, 415);
	frontend_draw_rect(&rect, 0, 0, 0xFFFF, 0);
	frontend_draw_rect_inset_xy(&rect, 2, 2);
	frontend_draw_rect(&rect, 0, 0, 0, 0);
	rect.bottom = rect.top + 14;
	frontend_draw_rect_inset_xy(&rect, 2, 0);
	{
		int action_index;
		for (action_index = g_config_joystick_action_scroll_offset;
		     action_index <
			     g_config_joystick_action_scroll_offset + 19 &&
		     action_index < g_joystick_entry_count;
		     ++action_index) {
			struct joystick_entry *entry =
				&g_joystick_entries[action_index];
			if (frontend_draw_point_in_rect(&rect, cursor_x,
							cursor_y) != 0) {
				frontend_draw_rect_outline(&rect, 0, 0,
							   g_color_green);
				if (frontend_mouse_get_left_click() != 0 ||
				    frontend_mouse_get_right_click() != 0) {
					if (action_index !=
						    g_config_selected_joystick_action_index &&
					    g_game_config.sfx_datapad_enabled !=
						    0) {
						frontend_sound_play_ui_sound(
							"settingsound", 1, 0,
							255,
							12 * g_game_config
									.sfx_datapad_volume,
							63);
					}
					{
						uint8_t selected_action_code =
							entry->action_code;
						g_config_selected_joystick_action_index =
							action_index;
						g_game_config.joy_buttons
							[g_config_selected_joystick_button_index] =
							selected_action_code;
					}
				}
			}
			sprintf(g_frontend_scratch_buffer, "%c%s: %c%s", 2,
				entry->name, 1, entry->description);
			{
				int text_color = g_color_yellow;
				if (action_index !=
				    g_config_selected_joystick_action_index) {
					text_color = 0xFFFF;
				}
				frontend_text_draw_aligned_in_rect(
					12, g_frontend_scratch_buffer, &rect, 0,
					1, text_color);
			}
			frontend_draw_rect_offset_xy(&rect, 0, 15);
		}
	}

	{
		struct joystick_entry *entry;
		int entry_index = 0;
		uint8_t action_code = config_read_joystick_action_picker_key();
		int entry_count = g_joystick_entry_count;
		if (entry_index < entry_count) {
			entry = g_joystick_entries;
			{
				int selected_button =
					g_config_selected_joystick_button_index;
				do {
					if (action_code != 0 &&
					    entry->action_code == action_code) {
						if (entry_index !=
							    g_config_selected_joystick_action_index &&
						    g_game_config.sfx_datapad_enabled !=
							    0) {
							frontend_sound_play_ui_sound(
								"settingsound",
								1, 0, 255,
								12 * g_game_config
										.sfx_datapad_volume,
								63);
							entry_count =
								g_joystick_entry_count;
							selected_button =
								g_config_selected_joystick_button_index;
						}
						{
							uint8_t selected_action_code =
								entry->action_code;
							g_config_joystick_action_scroll_offset =
								entry_index;
							g_config_selected_joystick_action_index =
								entry_index;
							g_game_config.joy_buttons
								[selected_button] =
								selected_action_code;
						}
					}
					++entry;
					++entry_index;
				} while (entry_index < entry_count);
			}
		}
	}
}

/* Reads joystick.txt into g_joystick_entries: one action per line of up to 127
 * characters, a decimal code, a space, a name up to the next space, a space and
 * the description. Returns 0, keeping the old list, when the file does not
 * open, else 1. Checks neither the 128-entry limit nor the 20-byte name, and a
 * line without a second space takes its description from past its end. */
// FUNCTION: XVT 0x4BD5F0
int config_load_joystick_action_dictionary(void)
{
	xvt_file *stream;
	char *cursor;
	int token_length;
	char token[256];
	char current_character;
	int entry_index;

	stream = file_open("joystick.txt", "r");
	if (stream == NULL) {
		return 0;
	}
	g_joystick_entry_count = 0;
	for (;;) {
		cursor = FILE_GETS(g_frontend_scratch_buffer, 128, stream);
		if (cursor == NULL) {
			break;
		}
		if (g_frontend_scratch_buffer
			    [strlen(g_frontend_scratch_buffer) - 1] == '\n') {
			g_frontend_scratch_buffer
				[strlen(g_frontend_scratch_buffer) - 1] = '\0';
		}

		token_length = 0;
		while (*cursor != ' ') {
			current_character = *cursor;
			if (current_character == '\0' || token_length >= 256) {
				break;
			}
			token[token_length++] = *cursor++;
		}
		token[token_length] = '\0';
		++cursor;
		g_joystick_entries[g_joystick_entry_count].action_code =
			atoi(token);

		token_length = 0;
		while (*cursor != ' ') {
			current_character = *cursor;
			if (current_character == '\0' || token_length >= 256) {
				break;
			}
			token[token_length++] = *cursor++;
		}
		token[token_length] = '\0';
		entry_index = g_joystick_entry_count;
		strcpy(g_joystick_entries[g_joystick_entry_count].name, token);
		strcpy(g_joystick_entries[entry_index].description, cursor + 1);
		++g_joystick_entry_count;
	}
	file_close(stream);
	return 1;
}

/* Returns the action code of a key pressed for mapping, or 0 for none: a typed
 * character from the queue as it is; else with Alt held, A to Z as 128 to 153
 * and 0 to 9 as 154 to 163; with Shift held, F1 to F12 as 207 to 218; otherwise
 * F1 to F12 as 195 to 206, and the arrow, editing, lock and number pad keys as
 * fixed codes from 164 to 194. */
// FUNCTION: XVT 0x4BD770
uint8_t config_read_joystick_action_picker_key(void)
{
	uint8_t key_code;
	int is_key_down;
	int key_index;

	key_code = keyboard_dequeue_char();
	if (key_code != 0) {
		return key_code;
	}

	if (keyboard_is_key_down(0x12)) {
		for (key_index = 0; key_index < 26; ++key_index) {
			if (keyboard_is_key_down(key_index + 65)) {
				return key_index + 0x80;
			}
		}
		for (key_index = 0; key_index < 10; ++key_index) {
			is_key_down = keyboard_is_key_down(key_index + 48);
			if (is_key_down) {
				return key_index - 102;
			}
		}
		return is_key_down;
	}

	if (keyboard_is_key_down(0x10)) {
		for (key_index = 0; key_index < 12; ++key_index) {
			is_key_down = keyboard_is_key_down(key_index + 112);
			if (is_key_down) {
				return key_index - 49;
			}
		}
		return is_key_down;
	}

	for (key_index = 0; key_index < 12; ++key_index) {
		if (keyboard_is_key_down(key_index + 112)) {
			return key_index - 61;
		}
	}
	if (keyboard_is_key_down(0x25)) {
		return -92;
	}
	if (keyboard_is_key_down(0x27)) {
		return -91;
	}
	if (keyboard_is_key_down(0x26)) {
		return -90;
	}
	if (keyboard_is_key_down(0x28)) {
		return -89;
	}
	if (keyboard_is_key_down(0x2D)) {
		return -88;
	}
	if (keyboard_is_key_down(0x2E)) {
		return -87;
	}
	if (keyboard_is_key_down(0x24)) {
		return -86;
	}
	if (keyboard_is_key_down(0x23)) {
		return -85;
	}
	if (keyboard_is_key_down(0x21)) {
		return -84;
	}
	if (keyboard_is_key_down(0x22)) {
		return -83;
	}
	if (keyboard_is_key_down(0x2C)) {
		return -82;
	}
	if (keyboard_is_key_down(0x91)) {
		return -81;
	}
	if (keyboard_is_key_down(0x14)) {
		return -79;
	}
	if (keyboard_is_key_down(0x60)) {
		return -78;
	}
	if (keyboard_is_key_down(0x61)) {
		return -77;
	}
	if (keyboard_is_key_down(0x62)) {
		return -76;
	}
	if (keyboard_is_key_down(0x63)) {
		return -75;
	}
	if (keyboard_is_key_down(0x64)) {
		return -74;
	}
	if (keyboard_is_key_down(0x65)) {
		return -73;
	}
	if (keyboard_is_key_down(0x66)) {
		return -72;
	}
	if (keyboard_is_key_down(0x67)) {
		return -71;
	}
	if (keyboard_is_key_down(0x68)) {
		return -70;
	}
	if (keyboard_is_key_down(0x69)) {
		return -69;
	}
	if (keyboard_is_key_down(0x90)) {
		return -68;
	}
	if (keyboard_is_key_down(0x6A)) {
		return -66;
	}
	if (keyboard_is_key_down(0x6B)) {
		return -64;
	}
	if (keyboard_is_key_down(0x6D)) {
		return -65;
	}
	if (keyboard_is_key_down(0x6E)) {
		return -62;
	}
	is_key_down = keyboard_is_key_down(0x6F);
	if (is_key_down != 0) {
		key_code = -67;
	} else {
		key_code = is_key_down;
	}
	return key_code;
}

/* Draws the taunts page: the four taunts as fields of up to 46 characters,
 * labeled FRONTSTR_795_TAUNT with their number. Enter or Tab in a field moves
 * g_active_text_field_id to the next field, after the fourth to 0. */
// FUNCTION: XVT 0x4BDA30
void config_draw_custom_taunts_page(void)
{
	struct RECT rect;
	int widest_label;
	int label_index;
	int label_width;
	int text_y;
	int field_index;
	char (*taunt)[70];

	frontend_draw_rect_assign(&rect, 84, 90, 604, 106);
	widest_label = 0;
	label_index = 0;
	frontend_text_draw_centered(
		15, frontend_string_get(FRONTSTR_794_CUSTOM_TAUNTS), &rect,
		0xFFFF);
	do {
		++label_index;
		sprintf(g_frontend_scratch_buffer, "%s%d",
			frontend_string_get(FRONTSTR_795_TAUNT), label_index);
		label_width = frontend_text_measure_width(
			g_frontend_scratch_buffer, 12);
		if (label_width > widest_label) {
			widest_label = label_width;
		}
	} while (label_index < 4);

	text_y = 126;
	field_index = 0;
	taunt = g_game_config.taunts;
	frontend_draw_rect_assign(&rect, widest_label + 93, 126, 600, 146);
	do {
		label_index = field_index + 1;
		sprintf(g_frontend_scratch_buffer, "%s%d",
			frontend_string_get(FRONTSTR_795_TAUNT), label_index);
		frontend_text_draw(12, g_frontend_scratch_buffer, 88, text_y,
				   0xFFFF);
		frontend_draw_fill_rect_translucent(
			&rect, 0, 0, g_editable_field_background_color);
		if (frontend_text_handle_editable_field(
			    &rect, *taunt, 46, field_index, 12, NULL)) {
			g_active_text_field_id = label_index;
			if (label_index >= 4) {
				g_active_text_field_id = 0;
			}
		}
		text_y += 35;
		++taunt;
		field_index = label_index;
		frontend_draw_rect_offset_xy(&rect, 0, 35);
	} while (taunt < g_game_config.taunts + 4);
}

/* Update function of the credits screen. On frame 0 it resets the credits
 * globals, opens credits.txt and reads the first page, going to the concourse
 * when there is none, and starts a 200-frame text fade. Holding Shift, Alt and
 * F12 draws a hidden photo for pages 0, 1, 2 and 4. When a logo changes it
 * redraws the background and logos into the offscreen surface. It draws both
 * pages of text, 19 pixels a line in font 15, the newest fading in. When a
 * page's time is up it reads the next one and fades it in, turning off the CD
 * loop after the last. After the last page's time, or on a click, Esc, Enter or
 * Space, it clears the offscreen surface (on a key or click the back buffer
 * too), fades the CD music over 2000 ms and sets g_credits_exit_pending; the next
 * frame goes to the concourse, in the modern build once the fade is over.
 * Returns 0. */
// FUNCTION: XVT 0x4FB670
int credits_update_screen(int frame_counter)
{
	int out_page_duration_frames;
	int line_index;
	int key;
	int logo_id;

	if (frame_counter == 0) {
		keyboard_flush_char_buffer();
		if (g_frontend_credits_file != NULL) {
			file_close(g_frontend_credits_file);
		}
		g_credits_current_text_color = (uint16_t)-1;
		g_credits_exit_pending = 0;
		g_credits_page_index = 0;
		g_credits_text_x[0] = 0;
		g_credits_text_y[1] = 0;
		g_credits_logo_id[0] = 0;
		g_credits_logo_y[0] = 0;
		g_credits_logo_x[0] = 0;
		g_credits_logo_id[1] = 0;
		g_credits_logo_y[1] = 0;
		g_credits_logo_x[1] = 0;
		g_frontend_credits_file = file_open("credits.txt", "rt");
		g_credits_has_more_pages = 0;
		g_credits_prev_logo_id[0] = 0;
		g_credits_prev_logo_y[0] = 0;
		g_credits_prev_logo_x[0] = 0;
		g_credits_prev_logo_id[1] = 0;
		g_credits_prev_logo_y[1] = 0;
		g_credits_prev_logo_x[1] = 0;
		if (credits_parse_next_page(&g_credits_buffer_idx,
					    &g_credits_has_more_pages,
					    &g_credits_page_end_frame,
					    &g_credits_text_fade_frames) == 0) {
			frontend_screen_set_callbacks(concourse_update,
						      concourse_exit);
			return 0;
		}
		frontend_text_start_text_fade_in(200);
	}
	if (g_credits_exit_pending != 0) {
#ifdef XVT_MODERN
		if (xvt_cd_task_is_fading()) {
			return 0;
		}
#endif
		frontend_screen_set_callbacks(concourse_update, concourse_exit);
		return 0;
	}

	if (keyboard_is_key_down(0x10) && keyboard_is_key_down(0x12) &&
	    keyboard_is_key_down(0x7B)) {
		switch (g_credits_page_index) {
		case 0:
			front_image_draw_sprite("comp01", 160, 157);
			break;
		case 1:
			front_image_draw_sprite("artists", 160, 163);
			break;
		case 2:
			front_image_draw_sprite("testers", 160, 152);
			break;
		case 4:
			front_image_draw_sprite("lakota", 160, 143);
			break;
		default:
			break;
		}
	}

	if (g_credits_prev_logo_id[0] != g_credits_logo_id[0] ||
	    g_credits_prev_logo_x[0] != g_credits_logo_x[0] ||
	    g_credits_prev_logo_y[0] != g_credits_logo_y[0] ||
	    g_credits_prev_logo_id[1] != g_credits_logo_id[1] ||
	    g_credits_prev_logo_x[1] != g_credits_logo_x[1] ||
	    g_credits_prev_logo_y[1] != g_credits_logo_y[1]) {
		g_credits_prev_logo_id[0] = g_credits_logo_id[0];
		g_credits_prev_logo_x[0] = g_credits_logo_x[0];
		g_credits_prev_logo_y[0] = g_credits_logo_y[0];
		g_credits_prev_logo_id[1] = g_credits_logo_id[1];
		g_credits_prev_logo_x[1] = g_credits_logo_x[1];
		g_credits_prev_logo_y[1] = g_credits_logo_y[1];
		frontend_display_lock_offscreen_surface();
		front_image_draw_sprite_opaque("background", 0, 0);
		logo_id = g_credits_logo_id[0];
		switch (logo_id) {
		case 1:
			front_image_draw_sprite_translucent(
				"totallylogo", g_credits_logo_x[0],
				g_credits_logo_y[0]);
			break;
		case 2:
			front_image_draw_sprite_translucent(
				"leclogo", g_credits_logo_x[0],
				g_credits_logo_y[0]);
			break;
		default:
			break;
		}
		logo_id = g_credits_logo_id[1];
		switch (logo_id) {
		case 1:
			front_image_draw_sprite_translucent(
				"totallylogo", g_credits_logo_x[1],
				g_credits_logo_y[1]);
			break;
		case 2:
			front_image_draw_sprite_translucent(
				"leclogo", g_credits_logo_x[1],
				g_credits_logo_y[1]);
			break;
		default:
			break;
		}
		frontend_display_unlock_offscreen_surface(1);
	}

	if ((g_credits_buffer_idx & 1) != 0) {
		frontend_text_suspend_text_fade();
	}
	for (line_index = 0; line_index < 32; ++line_index) {
		frontend_text_draw(15, g_credits_text_lines[0][line_index],
				   g_credits_text_x[0],
				   g_credits_text_y[0] + 19 * line_index,
				   g_credits_text_colors[0][line_index]);
	}
	if ((g_credits_buffer_idx & 1) != 0) {
		frontend_text_resume_text_fade();
	} else {
		frontend_text_suspend_text_fade();
	}
	for (line_index = 0; line_index < 32; ++line_index) {
		frontend_text_draw(15, g_credits_text_lines[1][line_index],
				   g_credits_text_x[1],
				   g_credits_text_y[1] + 19 * line_index,
				   g_credits_text_colors[1][line_index]);
	}
	frontend_text_resume_text_fade();

	if (g_credits_has_more_pages != 0) {
		if (g_credits_page_end_frame <= frame_counter) {
			credits_parse_next_page(&g_credits_buffer_idx,
						&g_credits_has_more_pages,
						&out_page_duration_frames,
						&g_credits_text_fade_frames);
			g_credits_page_end_frame += out_page_duration_frames;
			frontend_text_start_text_fade_in(
				g_credits_text_fade_frames);
			++g_credits_page_index;
			if (g_credits_has_more_pages == 0) {
				cd_audio_disable_loop_current_track();
			}
		}
	} else if (g_credits_page_end_frame <= frame_counter) {
		g_credits_exit_pending = 1;
		frontend_display_clear_offscreen_surface();
		cd_audio_fade_aux_volume(0x8000, 0x1000, 2000);
	}

	key = (uint8_t)keyboard_dequeue_char();
	if (frontend_mouse_get_left_click() != 0 ||
	    frontend_mouse_get_right_click() != 0 || key == 27 || key == 13 ||
	    key == 32) {
		g_credits_exit_pending = 1;
		frontend_display_clear_offscreen_surface();
		frontend_display_clear_back_buffer();
		cd_audio_fade_aux_volume(0x8000, 0x1000, 2000);
	}
	return 0;
}
