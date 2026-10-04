#include "xvt_runtime/runtime/network_browser.h"

#include <stdio.h>

#include "xvt/audio/frontend_sound.h"
#include "xvt/frontend/concourse.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/front_image.h"
#include "xvt/frontend/frontend.h"
#include "xvt/frontend/frontend_button.h"
#include "xvt/frontend/frontend_cursor.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/frontend_draw.h"
#include "xvt/frontend/frontend_mission.h"
#include "xvt/frontend/frontend_mouse.h"
#include "xvt/frontend/frontend_screen.h"
#include "xvt/frontend/frontend_scrollbar.h"
#include "xvt/frontend/frontend_string.h"
#include "xvt/frontend/frontend_text.h"
#include "xvt/frontend/pilot.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt/net/frontend_net.h"
#include "xvt_runtime/runtime/dialog_task.h"
#include "xvt_runtime/runtime/network_metadata.h"
#include "xvt_runtime/runtime/network_task.h"

int xvt_network_browser_draw_list(void)
{
	const AeronDplayDirectorySnapshot *snapshot =
		xvt_network_task_snapshot();
	int *scroll = xvt_network_task_scroll_offset();
	int mouse_x, mouse_y, clicked = -1;
	struct RECT rect = {88, 94, 416, 109}, column;
	frontend_text_draw_aligned_in_rect(
		12,
		frontend_string_get(
			FRONTSTR_016_GAME_NAME_PLAYERS_NEEDED_LAST_QUERY),
		&rect, 0, 1, 0xffff);
	frontend_cursor_get_pos(&mouse_x, &mouse_y);
	if (snapshot->room_count > 6) {
		frontend_draw_rect_assign(&rect, 421, 114, 430, 204);
		*scroll = frontend_scrollbar_draw(&rect, *scroll,
						  snapshot->room_count, 0, 5,
						  g_color_navy, 5);
		int maximum = (int)snapshot->room_count - 6;
		if (*scroll > maximum) {
			*scroll = maximum;
		}
		if (*scroll < 0) {
			*scroll = 0;
		}
	} else {
		*scroll = 0;
	}
	frontend_draw_rect_assign(&rect, 88, 114, 419, 128);
	for (int i = *scroll;
	     i < *scroll + 6 && (unsigned)i < snapshot->room_count; ++i) {
		const AeronDplayDirectoryRoom *room = &snapshot->rooms[i];
		int compatible = xvt_network_task_compatible(room);
		int free_slots =
			room->metadata.max_players - room->metadata.players;
		int color = !compatible		       ? g_color_gray
			    : !room->metadata.joinable ? g_color_red
			    : !free_slots	       ? g_color_yellow
						       : g_color_green2;
		if (i == xvt_network_task_selected_index()) {
			frontend_draw_rect(&rect, 0, 0, g_color_navy, 1);
		}
		if (room->metadata.password_required) {
			front_image_draw_sprite("key", rect.left - 6,
						rect.top + 1);
		}
		char name[32];
		xvt_network_metadata_from_utf8(name, sizeof(name),
					       room->metadata.name);
		column = rect;
		column.right = 279;
		struct RECT clip;
		frontend_display_get_screen_clip_rect(&clip);
		frontend_display_set_screen_clip_rect640x480(&column);
		frontend_text_draw_aligned_in_rect(12, name, &column, 0, 1,
						   color);
		frontend_display_set_screen_clip_rect640x480(&clip);
		column = rect;
		column.left = 285;
		snprintf(g_frontend_scratch_buffer,
			 sizeof(g_frontend_scratch_buffer), "%d", free_slots);
		frontend_text_draw_aligned_in_rect(
			12, g_frontend_scratch_buffer, &column, 0, 1, color);
		column.left = 374;
		frontend_format_seconds_to_clock_string(
			xvt_network_task_snapshot_age());
		frontend_text_draw_aligned_in_rect(
			12, g_frontend_scratch_buffer, &column, 0, 1, color);
		if (frontend_draw_point_in_rect(&rect, mouse_x, mouse_y)) {
			frontend_draw_rect_outline(&rect, 0, 0, g_color_green);
			if (frontend_mouse_get_left_click() ||
			    frontend_mouse_get_right_click()) {
				if (g_game_config.sfx_datapad_enabled) {
					frontend_sound_play_ui_sound(
						"jewelsound", 1, 0, 255,
						12 * g_game_config
								.sfx_datapad_volume,
						63);
				}
				clicked = i;
			}
		}
		frontend_draw_rect_offset_xy(&rect, 0, 15);
	}
	return clicked;
}

int xvt_network_browser_draw_roster(void)
{
	struct RECT rect = {88, 218, 430, 233}, clip;
	frontend_text_draw_aligned_in_rect(
		12, frontend_string_get(FRONTSTR_201_PLAYERS_IN_GAME), &rect, 0,
		1, 0xffff);
	const AeronDplayDirectoryRoom *room = xvt_network_task_selected_room();
	if (!room) {
		return 1;
	}
	frontend_draw_rect_assign(&rect, 88, 238, 258, 252);
	for (unsigned i = 0; i < room->metadata.players; ++i) {
		char name[32];
		unsigned rating = room->metadata.roster[i].rating;
		if (rating > PILOT_RATING_JEDI_MASTER) {
			rating = PILOT_RATING_TARGET_DRONE;
		}
		xvt_network_metadata_from_utf8(name, sizeof(name),
					       room->metadata.roster[i].name);
		frontend_display_get_screen_clip_rect(&clip);
		frontend_display_set_screen_clip_rect640x480(&rect);
		snprintf(g_frontend_scratch_buffer,
			 sizeof(g_frontend_scratch_buffer), "%c%s %c%s", 6,
			 frontend_string_get(FRONTSTR_154_DRONE + rating), 4,
			 name);
		frontend_text_draw_aligned_in_rect(
			12, g_frontend_scratch_buffer, &rect, 0, 1, 0xffff);
		frontend_display_set_screen_clip_rect640x480(&clip);
		if (i == 3) {
			frontend_draw_rect_assign(&rect, 259, 238, 430, 252);
		} else {
			frontend_draw_rect_offset_xy(&rect, 0, 15);
		}
	}
	return 1;
}

int xvt_network_browser_draw_mission(void)
{
	struct xvt_network_preview *preview = xvt_network_task_preview();
	struct RECT rect = {88, 309, 430, 324};
	const char *label = frontend_string_get(FRONTSTR_187_MISSION);
	if (preview->title[0]) {
		snprintf(g_frontend_scratch_buffer,
			 sizeof(g_frontend_scratch_buffer), "%s %c%s", label, 4,
			 preview->title);
		label = g_frontend_scratch_buffer;
	}
	frontend_text_draw_aligned_in_rect(12, label, &rect, 0, 1, 0xffff);
	if (!xvt_network_task_selected_room()) {
		return 1;
	}
	frontend_draw_rect_assign(&rect, 88, 328, 420, 426);
	unsigned lines = frontend_text_draw_wrapped(12, preview->text, &rect,
						    0xffff, 4, 4096) +
			 1;
	if (lines > 6) {
		frontend_draw_rect_assign(&rect, 421, 328, 430, 426);
		preview->scroll = frontend_scrollbar_draw(
			&rect, preview->scroll, lines, 0, 5, g_color_navy, 6);
		frontend_draw_rect_assign(&rect, 88, 328, 420, 426);
	} else {
		preview->scroll = 0;
		frontend_draw_rect_assign(&rect, 88, 328, 430, 426);
	}
	frontend_text_draw_wrapped(12, preview->text, &rect, 0xffff, 4,
				   preview->scroll);
	return 1;
}

static int xvt_network_browser_after_error(int result, int context)
{
	(void)result;
	(void)context;
	return 0;
}

int xvt_network_browser_screen(int frame_counter)
{
	struct RECT rect;
	if (!frame_counter) {
		frontend_cursor_set_pos(415, 121);
		g_frontend_skip_screen_entry_setup = 0;
		g_config_connection_type_editable = 0;
		g_frontend_game_session_in_progress = 0;
		front_image_register_resource_default("frontres\\joinback.bmp",
						      "background");
		frontend_display_lock_offscreen_surface();
		front_image_draw_sprite_opaque("background", 0, 0);
		front_image_draw_sprite("frame", 0, 0);
		front_image_draw_sprite(g_host_cd_available ? "allactive"
							    : "clientactive",
					0, 0);
		front_image_draw_sprite_translucent("chatbox", 0, 0);
		front_image_draw_sprite_translucent("joinoverlay", 0, 0);
		frontend_display_unlock_offscreen_surface(1);
		frontend_text_start_text_fade_in(20);
		xvt_network_task_open_browser();
		if (xvt_network_task_browser_error() ==
		    AERON_DPLAY_DIRECTORY_ERROR_NOT_CONFIGURED) {
			xvt_dialog_confirm(
				"The multiplayer directory is not configured.",
				"", "", frontend_string_get(FRONTSTR_523_OKAY),
				NULL, 0);
			return xvt_dialog_continue_with(
				xvt_network_browser_after_error, 0);
		}
	}
	const AeronDplayDirectoryRoom *selected =
		xvt_network_task_selected_room();
	char name[32] = {0};
	if (selected) {
		xvt_network_metadata_from_utf8(name, sizeof(name),
					       selected->metadata.name);
	}
	frontend_draw_rect_assign(&rect, 158, 52, 491, 68);
	frontend_text_draw_centered(12, name, &rect, 0xffff);
	int clicked = frontend_net_draw_join_game_list(frame_counter);
	if (clicked >= 0) {
		xvt_network_task_toggle_selection(clicked);
	}
	frontend_net_draw_join_game_player_roster();
	frontend_net_draw_join_game_mission_briefing();
	frontend_draw_rect_assign(&rect, 461, 117, 595, 398);
	const AeronDplayDirectorySnapshot *snapshot =
		xvt_network_task_snapshot();
	const char *status =
		xvt_network_task_browser_error()
			? "Directory unavailable. Press Refresh to try again."
		: snapshot->refresh.state == AERON_DPLAY_DIRECTORY_PENDING
			? "Refreshing games..."
		: !snapshot->room_count ? "No games found."
					: "";
	frontend_text_draw_wrapped(12, status, &rect, 0xffff, 2, 0);
	frontend_draw_rect_assign(&rect, 507, 452, 562, 464);
	frontend_text_draw_centered(12, "v. 2.0", &rect, 0xffff);
	frontend_draw_rect_assign(&rect, 200, 452, 436, 464);
	if (g_pilot_data.name[0]) {
		snprintf(g_frontend_scratch_buffer,
			 sizeof(g_frontend_scratch_buffer), "%c%s %c%s", 6,
			 g_pilot_data.rating_name, 1, g_pilot_data.name);
		frontend_text_draw_centered(12, g_frontend_scratch_buffer,
					    &rect, g_color_yellow);
		snprintf(g_frontend_scratch_buffer,
			 sizeof(g_frontend_scratch_buffer), "rebtiny%d",
			 (frame_counter % 32) >> 1);
		front_image_draw_sprite(g_frontend_scratch_buffer, 204, 453);
		snprintf(g_frontend_scratch_buffer,
			 sizeof(g_frontend_scratch_buffer), "imptiny%d",
			 (frame_counter % 32) >> 1);
		front_image_draw_sprite(g_frontend_scratch_buffer, 420, 453);
	}
	frontend_net_draw_join_game_sidebars_and_query_all();
	if (frontend_handle_common_screen_controls(0)) {
		return 1;
	}
	if (xvt_dialog_is_active()) {
		return 0;
	}
	if (g_game_config.help_on) {
		frontend_button_enable_overlay_text();
	}
	frontend_button_set_overlay_text(
		frontend_string_get(FRONTSTR_569_PREVIOUS));
	frontend_draw_rect_assign(&rect, 85, 447, 176, 471);
	if (frontend_button_handle_sprite_button(
		    &rect, "leaveup", "leavedown",
		    frontend_string_get(FRONTSTR_258_RETURN_TO_PILOT_RECORDS),
		    12, 0, 8, "buttonsound")) {
		g_frontend_mission_session_mode = FRONTEND_MISSION_SESSION_NONE;
		frontend_screen_set_callbacks(concourse_update, concourse_exit);
	} else if (xvt_network_task_can_join()) {
		frontend_draw_rect_assign(&rect, 8, 405, 71, 470);
		frontend_button_set_overlay_text(
			frontend_string_get(FRONTSTR_018_JOIN));
		if (frontend_button_handle_sprite_button(
			    &rect, "nextup", "nextdown",
			    frontend_string_get(FRONTSTR_018_JOIN), 12, 0, 7,
			    "flysound")) {
			xvt_network_task_begin(XVT_NETWORK_CONNECT);
		}
	}
	frontend_button_disable_overlay_text();
	return 0;
}
