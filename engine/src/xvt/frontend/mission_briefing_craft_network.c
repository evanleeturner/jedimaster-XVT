#include "xvt/frontend/mission_briefing_craft_network.h"

#include <stdint.h>
#include <string.h>

#include "xvt/assets/model_preview.h"
#include "xvt/frontend/concourse.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/frontend_dialog.h"
#include "xvt/frontend/frontend_flight.h"
#include "xvt/frontend/frontend_mission.h"
#include "xvt/frontend/frontend_mission_list.h"
#include "xvt/frontend/frontend_screen.h"
#include "xvt/frontend/frontend_string.h"
#include "xvt/frontend/mission_briefing_craft.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/net/frontend_net.h"
#include "xvt/net/frontend_net_packets.h"
#include "xvt/net/net.h"
#include "xvt/net/net_send.h"
#include "xvt_runtime/log/log.h"
#include "xvt_runtime/runtime/dialog_task.h"
#include "xvt_runtime/runtime/mission_dialogs.h"

/* Part of mission_briefing_craft_selection_update: sends everyone
 * NET_PACKET_CRAFT_LOADOUT with eight ints, the selected flight group's
 * optional_craft_category and the selected preset craft, craft, warhead,
 * beam and countermeasure options, wave count minus one and craft count. */
void mission_briefing_craft_send_loadout(void)
{
	g_frontend_net_packet_scratch.packet_type = NET_PACKET_CRAFT_LOADOUT;
	*(int *)&g_frontend_net_packet_scratch.payload[0] =
		g_frontend_mission
			.flight_groups
				[g_mission_setup_selected_flight_group_index]
			.optional_craft_category;
	*(int *)&g_frontend_net_packet_scratch.payload[sizeof(int)] =
		g_mission_setup_selected_preset_craft_option_index;
	*(int *)&g_frontend_net_packet_scratch.payload[2 * sizeof(int)] =
		g_mission_setup_selected_flight_group_craft_option_index;
	*(int *)&g_frontend_net_packet_scratch.payload[3 * sizeof(int)] =
		g_mission_setup_selected_warhead_option_index;
	*(int *)&g_frontend_net_packet_scratch.payload[4 * sizeof(int)] =
		g_mission_setup_selected_beam_option_index;
	*(int *)&g_frontend_net_packet_scratch.payload[5 * sizeof(int)] =
		g_mission_setup_selected_countermeasure_option_index;
	*(int *)&g_frontend_net_packet_scratch.payload[6 * sizeof(int)] =
		g_mission_setup_selected_wave_count_minus_one;
	*(int *)&g_frontend_net_packet_scratch.payload[7 * sizeof(int)] =
		g_mission_setup_selected_craft_count;
	net_send_packet_and_flush(0, &g_frontend_net_packet_scratch,
				  9 * sizeof(int));
}

/* mission_briefing_craft_selection_update's answer to
 * NET_PACKET_HOST_CANCELLED: shuts the session down; a client gets the cancel
 * dialog and returns xvt_dialog_continue_with's result, the host goes to the
 * concourse with g_frontend_mission_session_mode NONE and returns
 * CRAFT_SELECTION_FRAME_GOES_ON. */
static int mission_briefing_craft_on_host_cancelled(void)
{
	net_shutdown_direct_play_session();
	XVT_LOG_INFO("mission.setup_cancelled screen=\"craft\"");
	if (net_is_host() == 0) {
		frontend_dialog_show_confirm_dialog(
			frontend_string_get(
				FRONTSTR_631_THE_CURRENT_GAME_HAS_BEEN),
			frontend_string_get(FRONTSTR_632_CANCELLED_BY_THE_HOST),
			frontend_string_get(
				FRONTSTR_633_PLEASE_SELECT_ANOTHER_GAME),
			NULL, NULL);
		return xvt_dialog_continue_with(xvt_mission_dialogs_resume,
						XVT_MISSION_BRIEFING_CANCELLED);
	}
	if (net_is_host() != 0) {
		g_frontend_mission_session_mode = FRONTEND_MISSION_SESSION_NONE;
		frontend_screen_set_callbacks(
			concourse_update,
			(frontend_screen_exit_fn)concourse_exit);
	} else {
		g_frontend_skip_screen_entry_setup = 1;
		g_frontend_mission_session_mode =
			FRONTEND_MISSION_SESSION_NET_CLIENT;
		frontend_screen_set_callbacks(
			frontend_net_join_game_screen,
			(frontend_screen_exit_fn)
				frontend_mission_list_free_screen_resources);
	}
	return CRAFT_SELECTION_FRAME_GOES_ON;
}

/* mission_briefing_craft_selection_update's answer to NET_PACKET_STATE:
 * prunes the flight assignments and sends the loadout again. */
static void mission_briefing_craft_on_state(void)
{
	mission_setup_prune_flight_assignments();
	mission_briefing_craft_send_loadout();
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
}

/* mission_briefing_craft_selection_update's answer to another player's
 * NET_PACKET_BRIEFING_ENTERED: a host with host-only craft selection sends
 * its loadout again. */
static void mission_briefing_craft_on_briefing_entered(void)
{
	if (net_is_host() != 0 &&
	    g_game_config.craft_selection == CRAFT_SELECTION_HOST_ONLY &&
	    net_get_local_player_id() !=
		    g_frontend_net_packet_sender_player_id) {
		mission_briefing_craft_send_loadout();
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
}

/* mission_briefing_craft_selection_update's answer to
 * NET_PACKET_CRAFT_LOADOUT: a client with host-only craft selection loads the
 * preview model of the craft type mission_setup_get_craft_type(-1) gives. */
static void mission_briefing_craft_on_craft_loadout(void)
{
	int craft_type;

	if (g_game_config.craft_selection == CRAFT_SELECTION_HOST_ONLY &&
	    net_is_host() == 0) {
		craft_type = mission_setup_get_craft_type(-1);
		model_preview_load_model(
			g_ship_list[g_ship_type_to_ship_list_index[craft_type]]
				.model_file_name);
		model_preview_set_light_direction(-1, 0, 1);
		XVT_LOG_DEBUG("briefing.craft_preview craft=%d", craft_type);
	}
}

/* mission_briefing_craft_selection_update's answer to
 * NET_PACKET_RELEASE_FLIGHT_RESERVATION: lowers
 * g_mission_setup_reserved_player_count when player g_frontend_net_packet_arg0
 * is in g_mission_setup_reserved_player_ids, and moves the entries after its
 * slot down one. */
static void mission_briefing_craft_release_reservation(void)
{
	int slot_index;

	for (slot_index = 0; g_mission_setup_reserved_player_count > slot_index;
	     ++slot_index) {
		if (g_mission_setup_reserved_player_ids[slot_index] ==
		    g_frontend_net_packet_arg0) {
			--g_mission_setup_reserved_player_count;
			break;
		}
	}
	for (; slot_index < MAX_PLAYERS - 1; ++slot_index) {
		g_mission_setup_reserved_player_ids[slot_index] =
			g_mission_setup_reserved_player_ids[slot_index + 1];
	}
	g_mission_setup_reserved_player_ids[MAX_PLAYERS - 1] = 0;
	XVT_LOG_DEBUG(
		"mission.setup_reservation player=%u held=0 reserved=%d screen=\"craft\"",
		(unsigned)g_frontend_net_packet_arg0,
		g_mission_setup_reserved_player_count);
}

/* mission_briefing_craft_selection_update's answer to
 * NET_PACKET_FLIGHT_RESERVATION: adds player g_frontend_net_packet_arg0 to
 * g_mission_setup_reserved_player_ids when it is not there, with a warning
 * when g_mission_setup_reserved_player_count is MAX_PLAYERS or more. */
static void mission_briefing_craft_add_reservation(void)
{
	int slot_index;

	for (slot_index = 0; g_mission_setup_reserved_player_count > slot_index;
	     ++slot_index) {
		if (g_mission_setup_reserved_player_ids[slot_index] ==
		    g_frontend_net_packet_arg0) {
			break;
		}
	}
	if (slot_index == g_mission_setup_reserved_player_count) {
		if (g_mission_setup_reserved_player_count >= MAX_PLAYERS) {
			XVT_LOG_WARN(
				"briefing.reservations_full player=%u reserved=%d",
				(unsigned)g_frontend_net_packet_arg0,
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
}

/* mission_briefing_craft_selection_update's answer to
 * NET_PACKET_PILOT_RATING: sets the sender's g_mp_roster entry's pilot_rating
 * to g_frontend_net_packet_arg0. */
static void mission_briefing_craft_store_pilot_rating(void)
{
	int roster_index;

	for (roster_index = 0; roster_index < MAX_PLAYERS; ++roster_index) {
		if (g_mp_roster[roster_index].player_id ==
		    g_frontend_net_packet_sender_player_id) {
			g_mp_roster[roster_index].pilot_rating =
				g_frontend_net_packet_arg0;
			XVT_LOG_DEBUG(
				"mission.setup_rating_received player=%u rating=%d index=%d",
				(unsigned)
					g_frontend_net_packet_sender_player_id,
				(int)g_mp_roster[roster_index].pilot_rating,
				roster_index);
			break;
		}
	}
}

/* The network part of each frame of mission_briefing_craft_selection_update:
 * acts on the packet frontend_net_process_network_packets returns, as that
 * function's comment tells it; frame_counter is only logged. Returns what the
 * frame returns when the packet ends it, else CRAFT_SELECTION_FRAME_GOES_ON. */
int mission_briefing_craft_handle_packet(int frame_counter)
{
	int packet_type = frontend_net_process_network_packets();
	if (packet_type == NET_PACKET_HOST_CANCELLED) {
		int cancel_handled = mission_briefing_craft_on_host_cancelled();
		if (cancel_handled != CRAFT_SELECTION_FRAME_GOES_ON) {
			return cancel_handled;
		}
	} else if (packet_type == NET_PACKET_STATE) {
		mission_briefing_craft_on_state();
	} else if (packet_type == NET_PACKET_BRIEFING_ENTERED) {
		mission_briefing_craft_on_briefing_entered();
	} else if (packet_type == NET_PACKET_RETURN_TO_SETUP) {
		XVT_LOG_INFO("mission.setup_returned screen=\"craft\"");
		g_frontend_skip_screen_entry_setup = 1;
		frontend_screen_set_callbacks(
			mission_setup_update,
			(frontend_screen_exit_fn)mission_setup_exit);
		return 0;
	} else if (packet_type == NET_PACKET_LAUNCH_ROSTER_AND_ASSIGNMENTS) {
		XVT_LOG_DEBUG(
			"briefing.launch_received frame=%d ms=%d state=%d entered=%d",
			frame_counter, g_mission_briefing_launch_countdown_ms,
			(int)g_mission_briefing_launch_countdown_state,
			g_frontend_briefing_entered_count);
		frontend_mission_init_player_state();
		frontend_screen_set_callbacks(
			flight_loading_update_ready_screen, NULL);
		return 0;
	} else if (packet_type == NET_PACKET_CRAFT_LOADOUT) {
		mission_briefing_craft_on_craft_loadout();
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
	} else if (packet_type == NET_PACKET_RELEASE_FLIGHT_RESERVATION) {
		mission_briefing_craft_release_reservation();
	} else if (packet_type == NET_PACKET_FLIGHT_RESERVATION) {
		mission_briefing_craft_add_reservation();
	} else if (packet_type == NET_PACKET_PILOT_RATING) {
		mission_briefing_craft_store_pilot_rating();
	}
	return CRAFT_SELECTION_FRAME_GOES_ON;
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
