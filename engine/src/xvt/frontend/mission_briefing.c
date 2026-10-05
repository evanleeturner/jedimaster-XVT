#include "xvt/frontend/mission_briefing.h"
#ifdef XVT_MODERN
#include "xvt_runtime/runtime/frontend_cleanup.h"
#endif
#ifdef XVT_MODERN
#include "xvt_runtime/runtime/dialog_task.h"
#include "xvt_runtime/runtime/mission_dialogs.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xvt/assets/model_preview.h"
#include "xvt/frontend/briefing_map.h"
#include "xvt/frontend/briefing_text.h"
#include "xvt/frontend/concourse.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/front_image.h"
#include "xvt/frontend/frontend.h"
#include "xvt/frontend/frontend_button.h"
#include "xvt/frontend/frontend_cursor.h"
#include "xvt/frontend/frontend_dialog.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/frontend_draw.h"
#include "xvt/frontend/frontend_flight.h"
#include "xvt/frontend/frontend_mission.h"
#include "xvt/frontend/frontend_mission_list.h"
#include "xvt/frontend/frontend_mouse.h"
#include "xvt/frontend/frontend_screen.h"
#include "xvt/frontend/frontend_string.h"
#include "xvt/frontend/frontend_text.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt/net/frontend_net.h"
#include "xvt/net/net.h"
#include "xvt/util/time.h"
#include "xvt_runtime/log/log_both_builds.h"

/* 1 while the craft selection screen of mission_briefing_craft_selection_update is
 * current: set on its first frame, set to 0 by
 * mission_briefing_craft_selection_exit. While it is 1, tech_library_update and
 * frontend_handle_common_screen_controls save and restore the model preview around
 * the tech library instead of freeing g_ship_list.
 * movie_draw_multiplayer_sync_status takes it as the local player's waiting flag
 * when the local player is not in g_movie_multiplayer_sync_players. */
// GLOBAL: XVT 0xAA60D0
int g_mission_briefing_craft_selection_active = 0;
/* Which side's background the craft selection screen loaded as
 * "background": REBEL for craftsr.bmp (single player) or craftmr.bmp
 * (network play), IMPERIAL for craftsi.bmp or craftmi.bmp. Set by
 * mission_briefing_craft_selection_update on its first frame; in a melee or
 * tournament mission_setup_update_craft_loadout changes it, and the background,
 * when the chosen craft changes side. Nothing resets it on exit. */
// GLOBAL: XVT 0x665D7C
mission_briefing_craft_screen_faction g_mission_briefing_craft_screen_faction =
	MISSION_BRIEFING_CRAFT_SCREEN_REBEL;
/* Per g_mp_roster entry, 1 once that player pressed Ready on the craft
 * selection screen. 4 functions write it: frontend_net_process_network_packets
 * sets an entry to 1 on the player's NET_PACKET_PLAYER_READY, to 0 on
 * NET_PACKET_PLAYER_UNREADY, and clears all of them when a roster player
 * leaves and, on the host, when a player is unavailable; mission_setup_update
 * and mission_debrief_update clear entries; mission_briefing_craft_selection_update
 * sets entry 0 when a single player flies. */
// GLOBAL: XVT 0xA91CA0
int g_mp_roster_ready_flags[8] = {0};
/* The kind of mission session the frontend runs:
 * FRONTEND_MISSION_SESSION_NONE (0), _SINGLEPLAYER (2), _NET_CLIENT (3) or
 * _NET_HOST (4). Many functions write it, chiefly concourse_update,
 * pilot_create_new, pilot_load_from_path, the FrontendNet join, connect and host
 * screens, the MissionSetup screens and the modern build's network and
 * mission dialogs. Most screens test it to tell single player from network
 * play. */
// GLOBAL: XVT 0xB69E30
frontend_mission_session_mode g_frontend_mission_session_mode =
	FRONTEND_MISSION_SESSION_NONE;
/* 1 when the mission is a melee in which no team has more than one player
 * flight group, so the flight assignment screen was skipped. Only
 * mission_briefing_craft_selection_update writes and reads it: it sets it on its
 * first frame, and in single player its back button then goes to the team
 * assignment screen, or to mission_setup_update when
 * g_mission_setup_team_assignment_skipped is set, instead of the flight assignment
 * screen. */
// GLOBAL: XVT 0x52C6D4
static int g_briefing_skip_player_assignment = 0;
/* GetTickCount value, in milliseconds, read for the launch countdown's
 * latest step. Only mission_briefing_craft_selection_update uses it: it sets it on
 * its first frame, when the countdown starts and on every frame while it runs.
 */
// GLOBAL: XVT 0x665D78
static int g_mission_briefing_now_ms = 0;
/* The network launch countdown's state. mission_briefing_craft_selection_update
 * sets IDLE on its first frame, ACTIVE on the first frame in network play on
 * which g_frontend_briefing_entered_count is at least net_count_ready_players(), and
 * EXPIRED when g_mission_briefing_launch_countdown_ms goes under 0. Only that
 * function uses it. */
// GLOBAL: XVT 0x665D80
static mission_briefing_launch_countdown_state
	g_mission_briefing_launch_countdown_state =
		MISSION_BRIEFING_COUNTDOWN_IDLE;
/* Milliseconds left before the craft selection screen launches the network
 * mission. Only mission_briefing_craft_selection_update writes it: 60000 on its
 * first frame; while the countdown is ACTIVE each frame subtracts the
 * GetTickCount milliseconds since g_mission_briefing_last_update_ms; a
 * NET_PACKET_BRIEFING_COUNTDOWN packet whose value is lower replaces it, in
 * any state; set to 0 when it goes under 0. */
// GLOBAL: XVT 0x665D84
static int g_mission_briefing_launch_countdown_ms = 0;
/* Nothing reads it; mission_briefing_craft_selection_update sets it to 0 on its
 * first frame. */
// GLOBAL: XVT 0x665D88
static int g_mission_briefing_unused_state = 0;
/* GetTickCount value, in milliseconds, when the launch countdown last
 * stepped; each step subtracts the time since then from
 * g_mission_briefing_launch_countdown_ms. Only mission_briefing_craft_selection_update
 * uses it: it sets it on its first frame, when the countdown starts and after
 * each step that does not expire. */
// GLOBAL: XVT 0x665D8C
static int g_mission_briefing_last_update_ms = 0;
/* Whole seconds of the countdown, g_mission_briefing_launch_countdown_ms / 1000,
 * in the host's last NET_PACKET_BRIEFING_COUNTDOWN; set to 60 when the
 * countdown starts, on every machine. The host sends a new packet on each
 * frame where the countdown's whole seconds differ from it. Only
 * mission_briefing_craft_selection_update uses it. */
// GLOBAL: XVT 0x665D90
static int g_mission_briefing_last_countdown_second_sent = 0;

/* Leaves the craft selection screen: frees g_mission_list, g_mission_text and
 * g_ship_list and sets each to NULL, sets the preview model's node switch to
 * 0, sets g_mission_briefing_craft_selection_active to 0, frees the "background"
 * image, resets the scrollable controls and clears the mouse input gate.
 * Returns 0; frame_counter is ignored. */
// FUNCTION: XVT 0x4EAA90
int mission_briefing_craft_selection_exit(int frame_counter)
{
	(void)frame_counter;

	XVT_LOG_DEBUG("briefing.craft_closed list=%d text=%d ship_list=%d",
		      g_mission_list != NULL, g_mission_text != NULL,
		      g_ship_list != NULL);
	if (g_mission_list != NULL) {
		free(g_mission_list);
		g_mission_list = NULL;
	}
	if (g_mission_text != NULL) {
		free(g_mission_text);
		g_mission_text = NULL;
	}
	if (g_ship_list != NULL) {
		free(g_ship_list);
		g_ship_list = NULL;
	}
	model_preview_set_node_switch_index(0);
	g_mission_briefing_craft_selection_active = 0;
	front_image_free_resource_by_name("background");
	frontend_reset_scrollable_controls();
	frontend_mouse_clear_input_gate();
	return 0;
}

/* Runs one frame of the craft selection screen, which the "go to craft
 * selection" button of mission_setup_flight_assignment_update's screen leads to;
 * the briefing map is on that screen. The screen shows the craft and armaments
 * of the selected flight group and, in network play, the other players'
 * loadouts, the chat and a launch countdown. On its first frame (frame_counter
 * 0) it sets g_mission_briefing_craft_selection_active to 1,
 * g_mission_briefing_unused_state to 0, the countdown state IDLE and
 * g_briefing_skip_player_assignment; sets g_selected_mission_list_index to the
 * pilot's mission in g_mission_list (g_mission_count when it is not there); and
 * loads the loadout, ship list and preview model. A single-player quick start
 * then does what the Fly button does. Otherwise it picks the background and
 * g_mission_briefing_craft_screen_faction; in network play it sends everyone
 * NET_PACKET_CRAFT_LOADOUT, unless craft selection is host-only on a client
 * outside a training sequence at GAME_DIFFICULTY_EASY_CHEAT, and
 * NET_PACKET_BRIEFING_ENTERED; and it sets the countdown to 60000 ms. Each
 * frame in network play it acts on the packet frontend_net_process_network_packets
 * returns. A host cancel shuts the session down and sends the host to the
 * concourse with g_frontend_mission_session_mode NONE, a client, after a dialog,
 * to the join screen as NET_CLIENT. A state packet prunes the flight
 * assignments and resends the loadout; another player's
 * NET_PACKET_BRIEFING_ENTERED makes a host with host-only craft selection
 * resend its loadout; a lower countdown packet replaces the countdown; flight
 * reservations update g_mission_setup_reserved_player_ids and
 * g_mission_setup_reserved_player_count; a pilot rating updates the sender's
 * g_mp_roster entry. The single-player Fly button fills g_mp_roster[0] and
 * g_mp_roster_ready_flags[0] from the selections and goes to
 * flight_loading_update_ready_screen. In network play a host with host-only craft
 * selection gets Fly once g_frontend_briefing_entered_count reaches
 * net_count_ready_players(); otherwise each player's Ready and Reconfigure
 * buttons send NET_PACKET_PLAYER_READY and _UNREADY. The host broadcasts the
 * launch (mission_briefing_broadcast_roster_and_assignments) when
 * mission_briefing_are_all_network_players_ready returns 1 or the countdown expires,
 * and sends NET_PACKET_BRIEFING_COUNTDOWN when its whole seconds change. After
 * a confirm dialog the network back button makes the host send
 * NET_PACKET_RETURN_TO_SETUP and a client leave for the join screen. Writes
 * g_frontend_net_packet_scratch and g_frontend_skip_screen_entry_setup. Returns 0
 * after a quick start, a return-to-setup or launch packet, the single-player
 * back or Fly button, and when the countdown expires; the modern build returns
 * xvt_dialog_continue_with's result once it opens a dialog; otherwise 1 when
 * frontend_handle_common_screen_controls(1) returns 1 (the player confirmed
 * quitting the game), else 0. Does not check g_mission_list for NULL or
 * g_selected_mission_list_index against g_mission_count when it draws the title, or
 * that the local player is in g_mp_roster before reading its ready flag. */
// FUNCTION: XVT 0x4EAB20
int mission_briefing_craft_selection_update(int frame_counter)
{
	enum {
		MAX_PLAYERS = 8,
		LAUNCH_COUNTDOWN_MS = 60000,
	};

	int craft_type;

	if (frame_counter == 0) {
		frontend_cursor_set_pos(37, 445);
		g_mission_briefing_unused_state = 0;
		g_briefing_skip_player_assignment = 0;
		g_mission_briefing_launch_countdown_state =
			MISSION_BRIEFING_COUNTDOWN_IDLE;
		g_mission_briefing_craft_selection_active = 1;

		if (g_pilot_data.mission_directory_id ==
		    MISSION_DIRECTORY_MELEES) {
			int team_index;
			for (team_index = 0; team_index < g_team_count;
			     ++team_index) {
				if (g_team_player_flight_group_count
					    [team_index] > 1) {
					break;
				}
			}
			if (team_index == g_team_count) {
				g_briefing_skip_player_assignment = 1;
			}
		}

		mission_setup_load_mission_list(
			g_pilot_data.mission_directory_id);
		if (g_mission_list != NULL) {
			g_selected_mission_list_index = 0;
			if (g_mission_count > 0) {
				do {
					if (g_mission_list
						    [g_selected_mission_list_index]
							    .mission_idx ==
					    g_pilot_data.mission_description_ids
						    [g_pilot_data
							     .mission_directory_id]) {
						break;
					}
					++g_selected_mission_list_index;
				} while (g_mission_count >
					 (unsigned int)
						 g_selected_mission_list_index);
			}
		}
		if (g_mission_list == NULL ||
		    g_mission_count <=
			    (unsigned int)g_selected_mission_list_index) {
			XVT_LOG_WARN(
				"briefing.craft_mission_unlisted directory=%d mission=%d listed=%d count=%u index=%d",
				(int)g_pilot_data.mission_directory_id,
				(int)g_pilot_data.mission_description_ids
					[g_pilot_data.mission_directory_id],
				g_mission_list != NULL, g_mission_count,
				g_selected_mission_list_index);
		}

		mission_setup_init_craft_loadout();
		ship_list_load();
		craft_type = mission_setup_get_craft_type(-1);
		model_preview_load_model(
			g_ship_list[g_ship_type_to_ship_list_index[craft_type]]
				.model_file_name);
		model_preview_set_node_switch_index(
			g_frontend_mission
				.flight_groups
					[g_mission_setup_selected_flight_group_index]
				.markings);
		model_preview_set_light_direction(-1, 0, 1);
		model_preview_set_object_up_axis_angle_degrees(0.0f);
		XVT_LOG_DEBUG(
			"briefing.craft_setup fg=%d craft=%d markings=%d skip=%d index=%d missions=%u selection=%d quick=%d",
			g_mission_setup_selected_flight_group_index, craft_type,
			(int)g_frontend_mission
				.flight_groups
					[g_mission_setup_selected_flight_group_index]
				.markings,
			g_briefing_skip_player_assignment,
			g_selected_mission_list_index, g_mission_count,
			(int)g_game_config.craft_selection,
			g_frontend_quick_start_launch_flag);

		if (g_frontend_mission_session_mode ==
			    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
		    g_frontend_quick_start_launch_flag == 1) {
			if (g_mission_setup_selected_preset_craft_option_index ==
			    0) {
				g_mp_roster[0].craft_type_override = 0;
			} else {
				g_mp_roster[0].craft_type_override =
					mission_setup_get_craft_type(-1);
			}
			g_mp_roster[0].pilot_rating = g_pilot_data.rating;
			g_mp_roster[0].warhead_option_index =
				g_mission_setup_selected_warhead_option_index;
			g_mp_roster[0].beam_option_index =
				g_mission_setup_selected_beam_option_index;
			g_mp_roster_ready_flags[0] = 1;
			g_mp_roster[0].craft_option_index =
				g_mission_setup_selected_flight_group_craft_option_index -
				1;
			g_mp_roster[0].countermeasure_option_index =
				g_mission_setup_selected_countermeasure_option_index;
			XVT_LOG_DEBUG(
				"briefing.craft_quick_start fg=%d craft=%d option=%d",
				g_mission_setup_selected_flight_group_index,
				g_mp_roster[0].craft_type_override,
				g_mp_roster[0].craft_option_index);
			frontend_mission_init_player_state();
			frontend_screen_set_callbacks(
				flight_loading_update_ready_screen, NULL);
			return 0;
		}

		if (g_frontend_mission_session_mode ==
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
			if (g_pilot_data.mission_directory_id ==
				    MISSION_DIRECTORY_MELEES ||
			    g_pilot_data.mission_directory_id ==
				    MISSION_DIRECTORY_TOURNAMENTS) {
				craft_type = mission_setup_get_craft_type(-1);
				if (craft_type < 1 ||
				    (craft_type > 4 && craft_type != 14)) {
					g_mission_briefing_craft_screen_faction =
						MISSION_BRIEFING_CRAFT_SCREEN_IMPERIAL;
					front_image_register_resource_default(
						"frontres\\craftsi.bmp",
						"background");
				} else {
					g_mission_briefing_craft_screen_faction =
						MISSION_BRIEFING_CRAFT_SCREEN_REBEL;
					front_image_register_resource_default(
						"frontres\\craftsr.bmp",
						"background");
				}
			} else if (g_pilot_data.current_faction_id == 0) {
				g_mission_briefing_craft_screen_faction =
					MISSION_BRIEFING_CRAFT_SCREEN_REBEL;
				front_image_register_resource_default(
					"frontres\\craftsr.bmp", "background");
			} else {
				g_mission_briefing_craft_screen_faction =
					MISSION_BRIEFING_CRAFT_SCREEN_IMPERIAL;
				front_image_register_resource_default(
					"frontres\\craftsi.bmp", "background");
			}
		} else if (g_pilot_data.mission_directory_id ==
				   MISSION_DIRECTORY_COMBAT_ENGAGEMENTS ||
			   g_pilot_data.mission_directory_id ==
				   MISSION_DIRECTORY_BATTLES) {
			if (g_pilot_data.team == 0) {
				g_mission_briefing_craft_screen_faction =
					MISSION_BRIEFING_CRAFT_SCREEN_IMPERIAL;
				front_image_register_resource_default(
					"frontres\\craftmi.bmp", "background");
			} else {
				g_mission_briefing_craft_screen_faction =
					MISSION_BRIEFING_CRAFT_SCREEN_REBEL;
				front_image_register_resource_default(
					"frontres\\craftmr.bmp", "background");
			}
		} else {
			craft_type = mission_setup_get_craft_type(-1);
			if (craft_type >= 1 &&
			    (craft_type <= 4 || craft_type == 14)) {
				g_mission_briefing_craft_screen_faction =
					MISSION_BRIEFING_CRAFT_SCREEN_REBEL;
				front_image_register_resource_default(
					"frontres\\craftmr.bmp", "background");
			} else {
				g_mission_briefing_craft_screen_faction =
					MISSION_BRIEFING_CRAFT_SCREEN_IMPERIAL;
				front_image_register_resource_default(
					"frontres\\craftmi.bmp", "background");
			}
		}

		frontend_display_lock_offscreen_surface();
		front_image_draw_sprite_opaque("background", 0, 0);
		front_image_draw_sprite("frame", 0, 0);
		if (g_host_cd_available != 0) {
			front_image_draw_sprite("allactive", 0, 0);
		} else {
			front_image_draw_sprite("clientactive", 0, 0);
		}
		if (g_frontend_mission_session_mode !=
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
			front_image_draw_sprite_translucent("chatbox", 0, 0);
		}
		front_image_draw_sprite_translucent("regoverlay", 0, 0);
		frontend_display_unlock_offscreen_surface(1);
		frontend_text_start_text_fade_in(20);

		if (g_frontend_mission_session_mode !=
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
			if ((g_pilot_data.mission_directory_id ==
				     MISSION_DIRECTORY_TRAINING_EXERCISES &&
			     g_pilot_data.mission_sequence_active == 1 &&
			     g_game_config.difficulty ==
				     GAME_DIFFICULTY_EASY_CHEAT) ||
			    g_game_config.craft_selection !=
				    CRAFT_SELECTION_HOST_ONLY ||
			    net_is_host() != 0) {
				g_frontend_net_packet_scratch.packet_type =
					NET_PACKET_CRAFT_LOADOUT;
				*(int *)&g_frontend_net_packet_scratch
					 .payload[0] =
					g_frontend_mission
						.flight_groups
							[g_mission_setup_selected_flight_group_index]
						.optional_craft_category;
				*(int *)&g_frontend_net_packet_scratch
					 .payload[sizeof(int)] =
					g_mission_setup_selected_preset_craft_option_index;
				*(int *)&g_frontend_net_packet_scratch
					 .payload[2 * sizeof(int)] =
					g_mission_setup_selected_flight_group_craft_option_index;
				*(int *)&g_frontend_net_packet_scratch
					 .payload[3 * sizeof(int)] =
					g_mission_setup_selected_warhead_option_index;
				*(int *)&g_frontend_net_packet_scratch
					 .payload[4 * sizeof(int)] =
					g_mission_setup_selected_beam_option_index;
				*(int *)&g_frontend_net_packet_scratch
					 .payload[5 * sizeof(int)] =
					g_mission_setup_selected_countermeasure_option_index;
				*(int *)&g_frontend_net_packet_scratch
					 .payload[6 * sizeof(int)] =
					g_mission_setup_selected_wave_count_minus_one;
				*(int *)&g_frontend_net_packet_scratch
					 .payload[7 * sizeof(int)] =
					g_mission_setup_selected_craft_count;
				net_send_packet_and_flush(
					0, &g_frontend_net_packet_scratch,
					9 * sizeof(int));
				XVT_LOG_DEBUG(
					"briefing.loadout_sent by=\"entry\" category=%d preset=%d option=%d warhead=%d beam=%d countermeasure=%d waves=%d count=%d",
					(int)g_frontend_mission
						.flight_groups
							[g_mission_setup_selected_flight_group_index]
						.optional_craft_category,
					g_mission_setup_selected_preset_craft_option_index,
					g_mission_setup_selected_flight_group_craft_option_index,
					g_mission_setup_selected_warhead_option_index,
					g_mission_setup_selected_beam_option_index,
					g_mission_setup_selected_countermeasure_option_index,
					g_mission_setup_selected_wave_count_minus_one,
					g_mission_setup_selected_craft_count);
			}
			g_frontend_net_packet_scratch.packet_type =
				NET_PACKET_BRIEFING_ENTERED;
			net_send_packet_and_flush(
				0, &g_frontend_net_packet_scratch,
				sizeof(g_frontend_net_packet_scratch
					       .packet_type));
		}
		g_mission_briefing_launch_countdown_ms = LAUNCH_COUNTDOWN_MS;
		g_mission_briefing_now_ms = GetTickCount();
		g_mission_briefing_last_update_ms = g_mission_briefing_now_ms;
		XVT_LOG_INFO(
			"briefing.craft_opened fg=%d craft=%d faction=%d mode=%d",
			g_mission_setup_selected_flight_group_index, craft_type,
			(int)g_mission_briefing_craft_screen_faction,
			(int)g_frontend_mission_session_mode);
	}

	struct RECT rect;
	frontend_draw_rect_assign(&rect, 158, 52, 491, 68);
	struct RECT saved_clip_rect;
	frontend_display_get_screen_clip_rect(&saved_clip_rect);
	frontend_display_set_screen_clip_rect640x480(&rect);
	sprintf(g_frontend_scratch_buffer, "%c%s", 4,
		g_mission_list[g_selected_mission_list_index].description);
	for (int text_index = (int)strlen(g_frontend_scratch_buffer) - 1;
	     text_index > 0; --text_index) {
		if (g_frontend_scratch_buffer[text_index] == '(') {
			g_frontend_scratch_buffer[text_index] = '\0';
			break;
		}
	}
	frontend_text_draw_centered(12, g_frontend_scratch_buffer, &rect,
				    0xFFFF);
	frontend_display_set_screen_clip_rect640x480(&saved_clip_rect);
	int craft_selectable = 0;
	int armament_selectable_count = 0;

	int configuration_allowed = 1;
	if (g_pilot_data.mission_directory_id ==
		    MISSION_DIRECTORY_TRAINING_EXERCISES &&
	    g_pilot_data.mission_sequence_active == 1 &&
	    g_game_config.difficulty != GAME_DIFFICULTY_EASY_CHEAT) {
		configuration_allowed = 0;
	}
	if (configuration_allowed != 0) {
		if (g_mission_setup_flight_group_craft_option_count > 1 ||
		    g_mission_setup_preset_craft_option_count > 1) {
			craft_selectable = 1;
		}
		if (g_mission_setup_warhead_option_count > 1) {
			armament_selectable_count = 1;
		}
		if (g_mission_setup_beam_option_count > 1) {
			craft_type = mission_setup_get_craft_type(-1);
			if (craft_type < 1 ||
			    (craft_type > 5 && craft_type != 14)) {
				++armament_selectable_count;
			}
		}
		if (g_mission_setup_countermeasure_option_count > 1) {
			++armament_selectable_count;
		}
	}
	if (frame_counter == 0) {
		XVT_LOG_DEBUG(
			"briefing.craft_choices allowed=%d craft_choice=%d armaments=%d",
			configuration_allowed, craft_selectable,
			armament_selectable_count);
	}

	frontend_draw_rect_assign(&rect, 84, 90, 434, 108);
	if (craft_selectable != 0 || armament_selectable_count != 0) {
		sprintf(g_frontend_scratch_buffer, "%s %s %c%s",
			frontend_string_get(FRONTSTR_263_CRAFT_CONFIGURATION),
			frontend_string_get(FRONTSTR_604_FOR_FLIGHT_GROUP), 4,
			g_frontend_mission
				.flight_groups
					[g_mission_setup_selected_flight_group_index]
				.name);
	} else {
		sprintf(g_frontend_scratch_buffer, "%s %s %c%s",
			frontend_string_get(FRONTSTR_606_CRAFT_REVIEW),
			frontend_string_get(FRONTSTR_604_FOR_FLIGHT_GROUP), 4,
			g_frontend_mission
				.flight_groups
					[g_mission_setup_selected_flight_group_index]
				.name);
	}
	frontend_text_draw_centered(15, g_frontend_scratch_buffer, &rect,
				    0xFFFF);

	int roster_index;
	if (g_frontend_mission_session_mode !=
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		int packet_type = frontend_net_process_network_packets();
		int slot_index;
		if (packet_type == NET_PACKET_HOST_CANCELLED) {
			net_shutdown_direct_play_session();
			XVT_LOG_INFO(
				"mission.setup_cancelled screen=\"craft\"");
			if (net_is_host() == 0) {
				frontend_dialog_show_confirm_dialog(
					frontend_string_get(
						FRONTSTR_631_THE_CURRENT_GAME_HAS_BEEN),
					frontend_string_get(
						FRONTSTR_632_CANCELLED_BY_THE_HOST),
					frontend_string_get(
						FRONTSTR_633_PLEASE_SELECT_ANOTHER_GAME),
					NULL, NULL);
#ifdef XVT_MODERN
				return xvt_dialog_continue_with(
					xvt_mission_dialogs_resume,
					XVT_MISSION_BRIEFING_CANCELLED);
#endif
			}
			if (net_is_host() != 0) {
				g_frontend_mission_session_mode =
					FRONTEND_MISSION_SESSION_NONE;
				frontend_screen_set_callbacks(
					concourse_update,
					(frontend_screen_exit_fn)
						concourse_exit);
			} else {
				g_frontend_skip_screen_entry_setup = 1;
				g_frontend_mission_session_mode =
					FRONTEND_MISSION_SESSION_NET_CLIENT;
				frontend_screen_set_callbacks(
					frontend_net_join_game_screen,
					(frontend_screen_exit_fn)
						frontend_mission_list_free_screen_resources);
			}
		} else if (packet_type == NET_PACKET_STATE) {
			mission_setup_prune_flight_assignments();
			g_frontend_net_packet_scratch.packet_type =
				NET_PACKET_CRAFT_LOADOUT;
			*(int *)&g_frontend_net_packet_scratch.payload[0] =
				g_frontend_mission
					.flight_groups
						[g_mission_setup_selected_flight_group_index]
					.optional_craft_category;
			*(int *)&g_frontend_net_packet_scratch
				 .payload[sizeof(int)] =
				g_mission_setup_selected_preset_craft_option_index;
			*(int *)&g_frontend_net_packet_scratch
				 .payload[2 * sizeof(int)] =
				g_mission_setup_selected_flight_group_craft_option_index;
			*(int *)&g_frontend_net_packet_scratch
				 .payload[3 * sizeof(int)] =
				g_mission_setup_selected_warhead_option_index;
			*(int *)&g_frontend_net_packet_scratch
				 .payload[4 * sizeof(int)] =
				g_mission_setup_selected_beam_option_index;
			*(int *)&g_frontend_net_packet_scratch
				 .payload[5 * sizeof(int)] =
				g_mission_setup_selected_countermeasure_option_index;
			*(int *)&g_frontend_net_packet_scratch
				 .payload[6 * sizeof(int)] =
				g_mission_setup_selected_wave_count_minus_one;
			*(int *)&g_frontend_net_packet_scratch
				 .payload[7 * sizeof(int)] =
				g_mission_setup_selected_craft_count;
			net_send_packet_and_flush(
				0, &g_frontend_net_packet_scratch,
				9 * sizeof(int));
			XVT_LOG_DEBUG(
				"briefing.loadout_sent by=\"state\" category=%d preset=%d option=%d warhead=%d beam=%d countermeasure=%d waves=%d count=%d",
				(int)g_frontend_mission
					.flight_groups
						[g_mission_setup_selected_flight_group_index]
					.optional_craft_category,
				g_mission_setup_selected_preset_craft_option_index,
				g_mission_setup_selected_flight_group_craft_option_index,
				g_mission_setup_selected_warhead_option_index,
				g_mission_setup_selected_beam_option_index,
				g_mission_setup_selected_countermeasure_option_index,
				g_mission_setup_selected_wave_count_minus_one,
				g_mission_setup_selected_craft_count);
		} else if (packet_type == NET_PACKET_BRIEFING_ENTERED) {
			if (net_is_host() != 0 &&
			    g_game_config.craft_selection ==
				    CRAFT_SELECTION_HOST_ONLY &&
			    net_get_local_player_id() !=
				    g_frontend_net_packet_sender_player_id) {
				g_frontend_net_packet_scratch.packet_type =
					NET_PACKET_CRAFT_LOADOUT;
				*(int *)&g_frontend_net_packet_scratch
					 .payload[0] =
					g_frontend_mission
						.flight_groups
							[g_mission_setup_selected_flight_group_index]
						.optional_craft_category;
				*(int *)&g_frontend_net_packet_scratch
					 .payload[sizeof(int)] =
					g_mission_setup_selected_preset_craft_option_index;
				*(int *)&g_frontend_net_packet_scratch
					 .payload[2 * sizeof(int)] =
					g_mission_setup_selected_flight_group_craft_option_index;
				*(int *)&g_frontend_net_packet_scratch
					 .payload[3 * sizeof(int)] =
					g_mission_setup_selected_warhead_option_index;
				*(int *)&g_frontend_net_packet_scratch
					 .payload[4 * sizeof(int)] =
					g_mission_setup_selected_beam_option_index;
				*(int *)&g_frontend_net_packet_scratch
					 .payload[5 * sizeof(int)] =
					g_mission_setup_selected_countermeasure_option_index;
				*(int *)&g_frontend_net_packet_scratch
					 .payload[6 * sizeof(int)] =
					g_mission_setup_selected_wave_count_minus_one;
				*(int *)&g_frontend_net_packet_scratch
					 .payload[7 * sizeof(int)] =
					g_mission_setup_selected_craft_count;
				net_send_packet_and_flush(
					0, &g_frontend_net_packet_scratch,
					9 * sizeof(int));
				XVT_LOG_DEBUG(
					"briefing.loadout_sent by=\"player_entered\" category=%d preset=%d option=%d warhead=%d beam=%d countermeasure=%d waves=%d count=%d",
					(int)g_frontend_mission
						.flight_groups
							[g_mission_setup_selected_flight_group_index]
						.optional_craft_category,
					g_mission_setup_selected_preset_craft_option_index,
					g_mission_setup_selected_flight_group_craft_option_index,
					g_mission_setup_selected_warhead_option_index,
					g_mission_setup_selected_beam_option_index,
					g_mission_setup_selected_countermeasure_option_index,
					g_mission_setup_selected_wave_count_minus_one,
					g_mission_setup_selected_craft_count);
			}
		} else if (packet_type == NET_PACKET_RETURN_TO_SETUP) {
			XVT_LOG_INFO("mission.setup_returned screen=\"craft\"");
			g_frontend_skip_screen_entry_setup = 1;
			frontend_screen_set_callbacks(
				mission_setup_update,
				(frontend_screen_exit_fn)mission_setup_exit);
			return 0;
		} else if (packet_type ==
			   NET_PACKET_LAUNCH_ROSTER_AND_ASSIGNMENTS) {
			XVT_LOG_DEBUG(
				"briefing.launch_received frame=%d ms=%d state=%d entered=%d",
				frame_counter,
				g_mission_briefing_launch_countdown_ms,
				(int)g_mission_briefing_launch_countdown_state,
				g_frontend_briefing_entered_count);
			frontend_mission_init_player_state();
			frontend_screen_set_callbacks(
				flight_loading_update_ready_screen, NULL);
			return 0;
		} else if (packet_type == NET_PACKET_CRAFT_LOADOUT) {
			if (g_game_config.craft_selection ==
				    CRAFT_SELECTION_HOST_ONLY &&
			    net_is_host() == 0) {
				craft_type = mission_setup_get_craft_type(-1);
				model_preview_load_model(
					g_ship_list
						[g_ship_type_to_ship_list_index
							 [craft_type]]
							.model_file_name);
				model_preview_set_light_direction(-1, 0, 1);
				XVT_LOG_DEBUG("briefing.craft_preview craft=%d",
					      craft_type);
			}
		} else if (packet_type == NET_PACKET_BRIEFING_COUNTDOWN) {
			int packet_countdown_ms = g_frontend_net_packet_arg0;
			if (packet_countdown_ms <
			    g_mission_briefing_launch_countdown_ms) {
				XVT_LOG_DEBUG(
					"briefing.countdown_lowered ms=%d previous=%d",
					packet_countdown_ms,
					g_mission_briefing_launch_countdown_ms);
				g_mission_briefing_launch_countdown_ms =
					packet_countdown_ms;
			}
		} else if (packet_type ==
			   NET_PACKET_RELEASE_FLIGHT_RESERVATION) {
			for (slot_index = 0;
			     g_mission_setup_reserved_player_count > slot_index;
			     ++slot_index) {
				if (g_mission_setup_reserved_player_ids
					    [slot_index] ==
				    g_frontend_net_packet_arg0) {
					--g_mission_setup_reserved_player_count;
					break;
				}
			}
			for (; slot_index < MAX_PLAYERS - 1; ++slot_index) {
				g_mission_setup_reserved_player_ids[slot_index] =
					g_mission_setup_reserved_player_ids
						[slot_index + 1];
			}
			g_mission_setup_reserved_player_ids[MAX_PLAYERS - 1] =
				0;
			XVT_LOG_DEBUG(
				"mission.setup_reservation player=%u held=0 reserved=%d screen=\"craft\"",
				(unsigned)g_frontend_net_packet_arg0,
				g_mission_setup_reserved_player_count);
		} else if (packet_type == NET_PACKET_FLIGHT_RESERVATION) {
			for (slot_index = 0;
			     g_mission_setup_reserved_player_count > slot_index;
			     ++slot_index) {
				if (g_mission_setup_reserved_player_ids
					    [slot_index] ==
				    g_frontend_net_packet_arg0) {
					break;
				}
			}
			if (slot_index ==
			    g_mission_setup_reserved_player_count) {
				if (g_mission_setup_reserved_player_count >=
				    MAX_PLAYERS) {
					XVT_LOG_WARN(
						"briefing.reservations_full player=%u reserved=%d",
						(unsigned)
							g_frontend_net_packet_arg0,
						g_mission_setup_reserved_player_count);
				}
				g_mission_setup_reserved_player_ids
					[g_mission_setup_reserved_player_count++] =
						g_frontend_net_packet_arg0;
				XVT_LOG_DEBUG(
					"mission.setup_reservation player=%u held=1 reserved=%d screen=\"craft\"",
					(unsigned)g_frontend_net_packet_arg0,
					g_mission_setup_reserved_player_count);
			}
		} else if (packet_type == NET_PACKET_PILOT_RATING) {
			for (roster_index = 0; roster_index < MAX_PLAYERS;
			     ++roster_index) {
				if (g_mp_roster[roster_index].player_id ==
				    g_frontend_net_packet_sender_player_id) {
					g_mp_roster[roster_index].pilot_rating =
						g_frontend_net_packet_arg0;
					XVT_LOG_DEBUG(
						"mission.setup_rating_received player=%u rating=%d index=%d",
						(unsigned)
							g_frontend_net_packet_sender_player_id,
						(int)g_mp_roster[roster_index]
							.pilot_rating,
						roster_index);
					break;
				}
			}
		}
	}

	mission_setup_draw_craft_loadout();
	if (g_frontend_mission_session_mode !=
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		mission_setup_draw_player_loadouts(frame_counter);
		frontend_net_update_and_draw_chat_panel(frame_counter);
	}

	frontend_draw_rect_assign(&rect, 84, 419, 430, 434);
	if (g_frontend_mission_session_mode ==
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER ||
	    g_game_config.craft_selection != CRAFT_SELECTION_OFF) {
		if (craft_selectable != 0 || armament_selectable_count != 0) {
			if (g_frontend_mission_session_mode ==
				    FRONTEND_MISSION_SESSION_SINGLEPLAYER ||
			    g_game_config.craft_selection ==
				    CRAFT_SELECTION_ON) {
				strcpy(g_frontend_scratch_buffer,
				       frontend_string_get(
					       FRONTSTR_600_SELECT_YOUR));
			} else if (net_is_host() != 0) {
				strcpy(g_frontend_scratch_buffer,
				       frontend_string_get(
					       FRONTSTR_601_SELECT_EVERYBODY_S));
			} else {
				strcpy(g_frontend_scratch_buffer,
				       frontend_string_get(
					       FRONTSTR_602_HOST_IS_SELECTING));
			}
			if (craft_selectable != 0) {
				strcat(g_frontend_scratch_buffer, " ");
				strcat(g_frontend_scratch_buffer,
				       frontend_string_get(FRONTSTR_609_CRAFT));
			}
			if (armament_selectable_count != 0) {
				if (craft_selectable != 0) {
					strcat(g_frontend_scratch_buffer, " ");
					strcat(g_frontend_scratch_buffer,
					       frontend_string_get(
						       FRONTSTR_515_AND));
				}
				strcat(g_frontend_scratch_buffer, " ");
				strcat(g_frontend_scratch_buffer,
				       frontend_string_get(
					       FRONTSTR_607_ARMAMENTS));
			}
			strcat(g_frontend_scratch_buffer, ".");
			frontend_text_draw_centered(12,
						    g_frontend_scratch_buffer,
						    &rect, g_color_green);
		} else {
			frontend_text_draw_centered(
				12,
				frontend_string_get(
					FRONTSTR_608_REVIEW_YOUR_CRAFT_AND_ARMAMENTS),
				&rect, g_color_red);
		}
	} else {
		frontend_text_draw_centered(
			12,
			frontend_string_get(
				FRONTSTR_603_CRAFT_SELECTION_IS_DISABLED),
			&rect, g_color_red);
	}

	frontend_draw_rect_assign(&rect, 200, 452, 436, 464);
	if (g_pilot_data.name[0] != '\0') {
		sprintf(g_frontend_scratch_buffer, "%c%s %c%s", 6,
			g_pilot_data.rating_name, 1, g_pilot_data.name);
		frontend_text_draw_centered(12, g_frontend_scratch_buffer,
					    &rect, g_color_yellow);
		if (g_pilot_data.mission_directory_id ==
			    MISSION_DIRECTORY_TRAINING_EXERCISES ||
		    g_pilot_data.mission_directory_id ==
			    MISSION_DIRECTORY_MELEES ||
		    g_pilot_data.mission_directory_id ==
			    MISSION_DIRECTORY_TOURNAMENTS) {
			craft_type = mission_setup_get_craft_type(-1);
			if (craft_type >= 1 &&
			    (craft_type <= 4 || craft_type == 14)) {
				sprintf(g_frontend_scratch_buffer, "rebtiny%d",
					(frame_counter % 32) >> 1);
			} else {
				sprintf(g_frontend_scratch_buffer, "imptiny%d",
					(frame_counter % 32) >> 1);
			}
		} else if (g_pilot_data.team == 0) {
			sprintf(g_frontend_scratch_buffer, "imptiny%d",
				(frame_counter % 32) >> 1);
		} else {
			sprintf(g_frontend_scratch_buffer, "rebtiny%d",
				(frame_counter % 32) >> 1);
		}
		front_image_draw_sprite(g_frontend_scratch_buffer, 204, 453);
		front_image_draw_sprite(g_frontend_scratch_buffer, 420, 453);
	}

	if (g_frontend_mission_session_mode !=
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		frontend_draw_rect_assign(&rect, 507, 452, 562, 464);
		if (net_count_ready_players() <=
		    g_frontend_briefing_entered_count) {
			if (g_mission_briefing_launch_countdown_state ==
			    MISSION_BRIEFING_COUNTDOWN_IDLE) {
				g_mission_briefing_now_ms = GetTickCount();
				g_mission_briefing_last_update_ms =
					g_mission_briefing_now_ms;
				g_mission_briefing_last_countdown_second_sent =
					LAUNCH_COUNTDOWN_MS / 1000;
				g_mission_briefing_launch_countdown_state =
					MISSION_BRIEFING_COUNTDOWN_ACTIVE;
				XVT_LOG_INFO(
					"briefing.countdown_started entered=%d ms=%d",
					g_frontend_briefing_entered_count,
					g_mission_briefing_launch_countdown_ms);
			} else {
				frontend_format_seconds_to_clock_string(
					g_mission_briefing_launch_countdown_ms /
					1000);
				frontend_text_draw_centered(
					12, g_frontend_scratch_buffer, &rect,
					0xFFFF);
			}
		}
	}

	frontend_draw_rect_assign(&rect, 85, 447, 176, 471);
	if (g_game_config.help_on != 0) {
		frontend_button_enable_overlay_text();
	}
	if (g_frontend_mission_session_mode ==
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		int action_triggered;
		if (g_briefing_skip_player_assignment != 0) {
			if (g_mission_setup_team_assignment_skipped != 0) {
				frontend_button_set_overlay_text(
					frontend_string_get(
						FRONTSTR_216_ABORT));
				action_triggered = frontend_button_handle_sprite_button(
					&rect, "leaveup", "leavedown",
					frontend_string_get(
						FRONTSTR_260_RETURN_TO_SELECT_MISSION),
					12, 0, 8, "buttonsound");
				if (action_triggered != 0) {
					action_triggered = frontend_dialog_show_confirm_dialog(
						frontend_string_get(
							FRONTSTR_752_ARE_YOU_SURE_YOU_WANT_TO),
						frontend_string_get(
							FRONTSTR_753_RESTART_THE_GAME_AND_RETURN),
						frontend_string_get(
							FRONTSTR_754_TO_SELECT_MISSION),
						frontend_string_get(
							FRONTSTR_523_OKAY),
						frontend_string_get(
							FRONTSTR_019_CANCEL));
					XVT_LOG_DEBUG(
						"mission.setup_confirm_asked screen=\"craft\" action=\"restart\"");
#ifdef XVT_MODERN
					return xvt_dialog_continue_with(
						xvt_mission_dialogs_resume,
						XVT_MISSION_SOLO_BACK_TO_SETUP);
#endif
				}
			} else {
				frontend_button_set_overlay_text(
					frontend_string_get(
						FRONTSTR_569_PREVIOUS));
				action_triggered = frontend_button_handle_sprite_button(
					&rect, "leaveup", "leavedown",
					frontend_string_get(
						FRONTSTR_261_RETURN_TO_SELECT_TEAMS),
					12, 0, 8, "buttonsound");
			}
		} else {
			frontend_button_set_overlay_text(
				frontend_string_get(FRONTSTR_569_PREVIOUS));
			action_triggered = frontend_button_handle_sprite_button(
				&rect, "leaveup", "leavedown",
				frontend_string_get(
					FRONTSTR_262_RETURN_TO_BRIEFING_AND_PILOT_ASSIGNMENT),
				12, 0, 8, "buttonsound");
		}
		if (action_triggered != 0) {
			if (g_briefing_skip_player_assignment != 0) {
				if (g_mission_setup_team_assignment_skipped !=
				    0) {
					g_frontend_skip_screen_entry_setup = 1;
					frontend_screen_set_callbacks(
						mission_setup_update,
						(frontend_screen_exit_fn)
							mission_setup_exit);
				} else {
					XVT_LOG_INFO(
						"briefing.craft_left reason=\"back_to_teams\"");
					frontend_screen_set_callbacks(
						mission_setup_team_assignment_update,

#ifdef XVT_MODERN
						xvt_frontend_cleanup_mission_resources
#else
						(frontend_screen_exit_fn)
							frontend_mission_list_free_screen_resources_and_clear_input_gate
#endif
					);
				}
				return 0;
			}
			XVT_LOG_INFO(
				"briefing.craft_left reason=\"back_to_flights\"");
			frontend_screen_set_callbacks(
				mission_setup_flight_assignment_update,
				mission_setup_free_screen_resources);
			return 0;
		}
	} else if (net_is_host() != 0) {
		frontend_button_set_overlay_text(
			frontend_string_get(FRONTSTR_668_RESTART));

#ifdef XVT_MODERN
		if (frontend_button_handle_sprite_button(
			    &rect, "leaveup", "leavedown",
			    frontend_string_get(
				    FRONTSTR_260_RETURN_TO_SELECT_MISSION),
			    12, 0, 8, "buttonsound") != 0) {
			frontend_dialog_show_confirm_dialog(
				frontend_string_get(
					FRONTSTR_752_ARE_YOU_SURE_YOU_WANT_TO),
				frontend_string_get(
					FRONTSTR_753_RESTART_THE_GAME_AND_RETURN),
				frontend_string_get(
					FRONTSTR_754_TO_SELECT_MISSION),
				frontend_string_get(FRONTSTR_523_OKAY),
				frontend_string_get(FRONTSTR_019_CANCEL));
			XVT_LOG_DEBUG(
				"mission.setup_confirm_asked screen=\"craft\" action=\"restart\"");
			return xvt_dialog_continue_with(
				xvt_mission_dialogs_resume,
				XVT_MISSION_HOST_RESTART);
		}
#else
		if (frontend_button_handle_sprite_button(
			    &rect, "leaveup", "leavedown",
			    frontend_string_get(
				    FRONTSTR_260_RETURN_TO_SELECT_MISSION),
			    12, 0, 8, "buttonsound") != 0 &&
		    frontend_dialog_show_confirm_dialog(
			    frontend_string_get(
				    FRONTSTR_752_ARE_YOU_SURE_YOU_WANT_TO),
			    frontend_string_get(
				    FRONTSTR_753_RESTART_THE_GAME_AND_RETURN),
			    frontend_string_get(FRONTSTR_754_TO_SELECT_MISSION),
			    frontend_string_get(FRONTSTR_523_OKAY),
			    frontend_string_get(FRONTSTR_019_CANCEL)) != 0) {
			g_frontend_net_packet_scratch.packet_type =
				NET_PACKET_RETURN_TO_SETUP;
			net_send_packet_and_flush(
				0, &g_frontend_net_packet_scratch,
				sizeof(g_frontend_net_packet_scratch
					       .packet_type));
		}
#endif

	} else {
		frontend_button_set_overlay_text(
			frontend_string_get(FRONTSTR_204_LEAVE));

#ifdef XVT_MODERN
		if (frontend_button_handle_sprite_button(
			    &rect, "leaveup", "leavedown",
			    frontend_string_get(FRONTSTR_204_LEAVE), 12, 0, 8,
			    "buttonsound") != 0) {
			frontend_dialog_show_confirm_dialog(
				frontend_string_get(
					FRONTSTR_555_YOU_ARE_CURRENTLY_IN_A_GAME_SESSION),
				frontend_string_get(
					FRONTSTR_556_ARE_YOU_SURE_YOU_WANT_TO_QUIT),
				frontend_string_get(
					FRONTSTR_557_SPACE_TRANSLATION_PLACEHOLDER),
				frontend_string_get(FRONTSTR_523_OKAY),
				frontend_string_get(FRONTSTR_019_CANCEL));
			XVT_LOG_DEBUG(
				"mission.setup_confirm_asked screen=\"craft\" action=\"leave\"");
			return xvt_dialog_continue_with(
				xvt_mission_dialogs_resume,
				XVT_MISSION_CLIENT_LEAVE);
		}
#else
		if (frontend_button_handle_sprite_button(
			    &rect, "leaveup", "leavedown",
			    frontend_string_get(FRONTSTR_204_LEAVE), 12, 0, 8,
			    "buttonsound") != 0 &&
		    frontend_dialog_show_confirm_dialog(
			    frontend_string_get(
				    FRONTSTR_555_YOU_ARE_CURRENTLY_IN_A_GAME_SESSION),
			    frontend_string_get(
				    FRONTSTR_556_ARE_YOU_SURE_YOU_WANT_TO_QUIT),
			    frontend_string_get(
				    FRONTSTR_557_SPACE_TRANSLATION_PLACEHOLDER),
			    frontend_string_get(FRONTSTR_523_OKAY),
			    frontend_string_get(FRONTSTR_019_CANCEL)) != 0) {
			g_frontend_skip_screen_entry_setup = 1;
			g_frontend_net_packet_scratch.packet_type =
				NET_PACKET_PLAYER_LEFT;
			net_send_packet_and_flush(
				net_get_host_player_id(),
				&g_frontend_net_packet_scratch,
				sizeof(g_frontend_net_packet_scratch
					       .packet_type));
			net_shutdown_direct_play_session();
			frontend_screen_set_callbacks(
				frontend_net_join_game_screen,
				(frontend_screen_exit_fn)
					frontend_mission_list_free_screen_resources);
		}
#endif
	}

	frontend_draw_rect_assign(&rect, 8, 405, 71, 470);
	if (g_frontend_mission_session_mode ==
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		frontend_button_set_overlay_text(
			frontend_string_get(FRONTSTR_200_FLY));
		if (frontend_button_handle_sprite_button(
			    &rect, "flyup", "flydown",
			    frontend_string_get(FRONTSTR_200_FLY), 12, 0, 7,
			    "flysound") != 0 &&
		    g_frontend_mission_session_mode ==
			    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
			if (g_mission_setup_selected_preset_craft_option_index ==
			    0) {
				g_mp_roster[0].craft_type_override = 0;
			} else {
				g_mp_roster[0].craft_type_override =
					mission_setup_get_craft_type(-1);
			}
			g_mp_roster[0].pilot_rating = g_pilot_data.rating;
			g_mp_roster[0].warhead_option_index =
				g_mission_setup_selected_warhead_option_index;
			g_mp_roster[0].beam_option_index =
				g_mission_setup_selected_beam_option_index;
			g_mp_roster_ready_flags[0] = 1;
			g_mp_roster[0].craft_option_index =
				g_mission_setup_selected_flight_group_craft_option_index -
				1;
			g_mp_roster[0].countermeasure_option_index =
				g_mission_setup_selected_countermeasure_option_index;
			XVT_LOG_INFO(
				"briefing.craft_confirmed fg=%d craft=%d option=%d",
				g_mission_setup_selected_flight_group_index,
				g_mp_roster[0].craft_type_override,
				g_mp_roster[0].craft_option_index);
			frontend_mission_init_player_state();
			frontend_screen_set_callbacks(
				flight_loading_update_ready_screen, NULL);
			frontend_button_disable_overlay_text();
			return 0;
		}
	} else {
		if (g_game_config.craft_selection ==
		    CRAFT_SELECTION_HOST_ONLY) {
			if (net_is_host() != 0 &&
			    net_count_ready_players() <=
				    g_frontend_briefing_entered_count) {
				frontend_button_set_overlay_text(
					frontend_string_get(FRONTSTR_200_FLY));
				if (frontend_button_handle_sprite_button(
					    &rect, "flyup", "flydown",
					    frontend_string_get(
						    FRONTSTR_200_FLY),
					    12, 0, 7, "flysound") != 0) {
					XVT_LOG_DEBUG(
						"briefing.roster_sent by=\"fly\" entered=%d ms=%d",
						g_frontend_briefing_entered_count,
						g_mission_briefing_launch_countdown_ms);
					mission_briefing_broadcast_roster_and_assignments();
				}
			}
		}
		if (g_game_config.craft_selection !=
		    CRAFT_SELECTION_HOST_ONLY) {
			for (roster_index = 0; roster_index < MAX_PLAYERS;
			     ++roster_index) {
				if (net_get_local_player_id() ==
				    g_mp_roster[roster_index].player_id) {
					break;
				}
			}
			if (g_mp_roster_ready_flags[roster_index] != 0) {
				frontend_button_set_overlay_text(
					frontend_string_get(
						FRONTSTR_575_RECONFIGURE));
				if (frontend_button_handle_sprite_button(
					    &rect, "flydown", "flyup",
					    frontend_string_get(
						    FRONTSTR_575_RECONFIGURE),
					    12, 0, 7, "flysound") != 0) {
					g_frontend_net_packet_scratch
						.packet_type =
						NET_PACKET_PLAYER_UNREADY;
					net_send_packet_and_flush(
						0,
						&g_frontend_net_packet_scratch,
						sizeof(g_frontend_net_packet_scratch
							       .packet_type));
					XVT_LOG_INFO(
						"briefing.ready_sent ready=0 entry=%d",
						roster_index);
				}
			} else {
				frontend_button_set_overlay_text(
					frontend_string_get(
						FRONTSTR_574_READY));
				if (frontend_button_handle_sprite_button(
					    &rect, "flyup", "flydown",
					    frontend_string_get(
						    FRONTSTR_574_READY),
					    12, 0, 7, "flysound") != 0) {
					g_frontend_net_packet_scratch
						.packet_type =
						NET_PACKET_PLAYER_READY;
					net_send_packet_and_flush(
						0,
						&g_frontend_net_packet_scratch,
						sizeof(g_frontend_net_packet_scratch
							       .packet_type));
					XVT_LOG_INFO(
						"briefing.ready_sent ready=1 entry=%d",
						roster_index);
				}
			}
		}
	}

	frontend_button_disable_overlay_text();
	if (g_frontend_mission_session_mode !=
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
	    net_is_host() != 0 &&
	    mission_briefing_are_all_network_players_ready() != 0) {
		XVT_LOG_DEBUG(
			"briefing.roster_sent by=\"all_ready\" entered=%d ms=%d",
			g_frontend_briefing_entered_count,
			g_mission_briefing_launch_countdown_ms);
		mission_briefing_broadcast_roster_and_assignments();
	}
	if (g_frontend_mission_session_mode !=
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
	    g_mission_briefing_launch_countdown_state ==
		    MISSION_BRIEFING_COUNTDOWN_ACTIVE) {
		g_mission_briefing_now_ms = GetTickCount();
		g_mission_briefing_launch_countdown_ms +=
			g_mission_briefing_last_update_ms -
			g_mission_briefing_now_ms;
		if (net_is_host() != 0 &&
		    g_mission_briefing_last_countdown_second_sent !=
			    g_mission_briefing_launch_countdown_ms / 1000) {
			g_mission_briefing_last_countdown_second_sent =
				g_mission_briefing_launch_countdown_ms / 1000;
			*(int *)g_frontend_net_packet_scratch.payload =
				g_mission_briefing_launch_countdown_ms;
			g_frontend_net_packet_scratch.packet_type =
				NET_PACKET_BRIEFING_COUNTDOWN;
			net_send_packet_and_flush(
				0, &g_frontend_net_packet_scratch,
				2 * sizeof(int));
			XVT_LOG_DEBUG("briefing.countdown_sent ms=%d",
				      g_mission_briefing_launch_countdown_ms);
		}
		if (g_mission_briefing_launch_countdown_ms < 0) {
			XVT_LOG_INFO("briefing.countdown_expired ms=%d mode=%d",
				     g_mission_briefing_launch_countdown_ms,
				     (int)g_frontend_mission_session_mode);
			g_mission_briefing_launch_countdown_ms = 0;
			g_mission_briefing_launch_countdown_state =
				MISSION_BRIEFING_COUNTDOWN_EXPIRED;
			if (net_is_host() != 0) {
				XVT_LOG_DEBUG(
					"briefing.roster_sent by=\"countdown\" entered=%d ms=%d",
					g_frontend_briefing_entered_count,
					g_mission_briefing_launch_countdown_ms);
				mission_briefing_broadcast_roster_and_assignments();
			}
			return 0;
		}
		g_mission_briefing_last_update_ms = g_mission_briefing_now_ms;
	}

	mission_setup_update_craft_loadout();
	return frontend_handle_common_screen_controls(1) == 1;
}

/* Sends everyone, the host included (player id 0), the
 * NET_PACKET_LAUNCH_ROSTER_AND_ASSIGNMENTS packet that starts the mission:
 * for each of the 8 g_mp_roster entries five ints (craft_type_override,
 * craft_option_index, warhead_option_index, beam_option_index and
 * countermeasure_option_index), then the 80 entries of
 * g_mission_setup_player_flight_group_indices, one byte each: 244 bytes with the
 * type word. Writes g_frontend_net_packet_scratch. Returns 1. Does not check
 * that an index fits in a byte. */
// FUNCTION: XVT 0x4EEC10
int mission_briefing_broadcast_roster_and_assignments(void)
{
	enum { PLAYER_SLOTS_PER_TEAM = 8 };

	unsigned int packet_dword_index = 1;
	g_frontend_net_packet_scratch.packet_type =
		NET_PACKET_LAUNCH_ROSTER_AND_ASSIGNMENTS;
	for (unsigned int roster_index = 0;
	     roster_index < sizeof(g_mp_roster) / sizeof(g_mp_roster[0]);
	     ++roster_index) {
		memcpy(g_frontend_net_packet_scratch.payload +
			       (packet_dword_index - 1) * sizeof(int),
		       &g_mp_roster[roster_index].craft_type_override,
		       sizeof(g_mp_roster[roster_index].craft_type_override));
		++packet_dword_index;
		memcpy(g_frontend_net_packet_scratch.payload +
			       (packet_dword_index - 1) * sizeof(int),
		       &g_mp_roster[roster_index].craft_option_index,
		       sizeof(g_mp_roster[roster_index].craft_option_index));
		++packet_dword_index;
		memcpy(g_frontend_net_packet_scratch.payload +
			       (packet_dword_index - 1) * sizeof(int),
		       &g_mp_roster[roster_index].warhead_option_index,
		       sizeof(g_mp_roster[roster_index].warhead_option_index));
		++packet_dword_index;
		memcpy(g_frontend_net_packet_scratch.payload +
			       (packet_dword_index - 1) * sizeof(int),
		       &g_mp_roster[roster_index].beam_option_index,
		       sizeof(g_mp_roster[roster_index].beam_option_index));
		++packet_dword_index;
		memcpy(g_frontend_net_packet_scratch.payload +
			       (packet_dword_index - 1) * sizeof(int),
		       &g_mp_roster[roster_index].countermeasure_option_index,
		       sizeof(g_mp_roster[roster_index]
				      .countermeasure_option_index));
		++packet_dword_index;
	}
	uint8_t *output = g_frontend_net_packet_scratch.payload +
			  (packet_dword_index - 1) * sizeof(int);
	int assignment_index = 0;
	for (unsigned int team_index = 0;
	     team_index <
	     sizeof(g_mission_setup_player_flight_group_indices) /
		     (PLAYER_SLOTS_PER_TEAM *
		      sizeof(g_mission_setup_player_flight_group_indices[0]));
	     ++team_index) {
		for (unsigned int player_index = 0;
		     player_index < PLAYER_SLOTS_PER_TEAM; ++player_index) {
			*output = (uint8_t)
				g_mission_setup_player_flight_group_indices
					[assignment_index];
			++output;
			++assignment_index;
		}
	}
	net_send_packet_and_flush(
		0, &g_frontend_net_packet_scratch,
		packet_dword_index * sizeof(int) +
			sizeof(g_mission_setup_player_flight_group_indices) /
				sizeof(g_mission_setup_player_flight_group_indices
					       [0]));
	return 1;
}

/* Passes the cursor, moved one pixel up and left, to
 * briefing_map_select_flight_group_at_cursor, which picks
 * g_briefing_selected_mission_point14_flight_group_idx by it. Returns 0 without
 * doing so when suppress_input is nonzero, else that function's result, 1.
 * The rectangles it passes, each inset by one pixel, and the button states
 * are ignored there, so the choice follows the cursor with or without a
 * button down. */
// FUNCTION: XVT 0x4F68C0
int16_t mission_briefing_handle_map_mouse_input(
	const struct RECT *viewport_rect, const struct RECT *clip_rect,
	int16_t suppress_input, int left_down, int right_down, int16_t mouse_x,
	int16_t mouse_y)
{
	struct RECT inset_viewport_rect;

	frontend_draw_rect_copy(&inset_viewport_rect, viewport_rect);
	frontend_draw_rect_inset_xy(&inset_viewport_rect, 1, 1);
	struct RECT inset_clip_rect;
	frontend_draw_rect_copy(&inset_clip_rect, clip_rect);
	frontend_draw_rect_inset_xy(&inset_clip_rect, 1, 1);
	if (suppress_input != 0) {
		return 0;
	}
	return briefing_map_select_flight_group_at_cursor(
		&inset_viewport_rect, &inset_clip_rect, left_down, right_down,
		(int16_t)(mouse_x - 1), (int16_t)(mouse_y - 1));
}

/* Draws the briefing map panel through
 * briefing_map_draw_viewport_and_selection, on copies of the two rectangles, and
 * returns its result, which is always 1; highlight_phase is ignored there. */
// FUNCTION: XVT 0x4F6970
int16_t mission_briefing_draw_map_viewport(const struct RECT *viewport_rect,
					   const struct RECT *clip_rect,
					   int16_t highlight_phase)
{
	struct RECT viewport_copy;

	frontend_draw_rect_copy(&viewport_copy, viewport_rect);
	struct RECT clip_copy;
	frontend_draw_rect_copy(&clip_copy, clip_rect);
	return briefing_map_draw_viewport_and_selection(
		&viewport_copy, &clip_copy, highlight_phase);
}

/* Returns 0 when g_frontend_mission_session_mode is single player. Otherwise
 * returns 1 when the count of nonzero g_mp_roster_ready_flags equals
 * net_count_ready_players(), the session players whose ready flag is 1, else
 * 0. It compares counts only, not which players they are. */
// FUNCTION: XVT 0x4FACC0
int mission_briefing_are_all_network_players_ready(void)
{
	int ready_flag_count = 0;
	if (g_frontend_mission_session_mode ==
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		return 0;
	}
	int ready_player_count = net_count_ready_players();
	for (int ready_flag_index = 0; ready_flag_index < 8;
	     ++ready_flag_index) {
		if (g_mp_roster_ready_flags[ready_flag_index] != 0) {
			++ready_flag_count;
		}
	}
	if (ready_flag_count == ready_player_count) {
		XVT_LOG_DEBUG("briefing.all_ready players=%d",
			      ready_player_count);
	}
	ready_flag_count -= ready_player_count;
	return ready_flag_count == 0;
}
