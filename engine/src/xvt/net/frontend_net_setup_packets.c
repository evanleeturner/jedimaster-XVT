#include "xvt/net/frontend_net_setup_packets.h"

#include <stdint.h>
#include <string.h>

#include "xvt/audio/frontend_sound.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/frontend_mission_list.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt/net/frontend_net.h"
#include "xvt/net/net.h"
#include "xvt_runtime/log/log.h"

/* Part of the frontend's packet handling: sets *roster_index to the first
 * entry of g_mp_roster whose player_id is not 0 and is this player's id, or
 * to MAX_PLAYERS when there is none. */
void frontend_net_find_local_roster_entry(int *roster_index)
{
	for (*roster_index = 0; *roster_index < MAX_PLAYERS; ++*roster_index) {
		if (g_mp_roster[*roster_index].player_id != 0 &&
		    g_mp_roster[*roster_index].player_id ==
			    net_get_local_player_id()) {
			break;
		}
	}
}

/* Part of frontend_net_process_setup_packet for a FRONTEND_MISSION_START:
 * stores the mission directory, the sequence state and the mission from
 * payload in g_pilot_data, loads the directory's mission list and sets
 * g_selected_mission_list_index to the mission in it. */
static void frontend_net_on_mission_start(int *payload)
{
	g_pilot_data.mission_directory_id = (mission_directory_id)payload[1];
	g_pilot_data.mission_sequence_active = payload[2];
	if (g_pilot_data.mission_sequence_active == 1) {
		g_pilot_data.saved_mission_description_id =
			g_pilot_data.mission_description_ids
				[g_pilot_data.mission_directory_id];
		if (g_pilot_data.mission_directory_id ==
		    MISSION_DIRECTORY_MELEES) {
			g_pilot_data.melee_tournament_sequence_state
				.mission_count = payload[3];
		} else if (g_pilot_data.mission_directory_id ==
			   MISSION_DIRECTORY_COMBAT_ENGAGEMENTS) {
			g_pilot_data.battle_sequence_state.victories_needed =
				payload[3];
		} else {
			g_pilot_data.campaign_sequence_state.mission_count =
				payload[3];
		}
	}
	g_pilot_data
		.mission_description_ids[g_pilot_data.mission_directory_id] =
		payload[0];
	mission_setup_load_mission_list(g_pilot_data.mission_directory_id);
	if (g_mission_list != NULL) {
		g_selected_mission_list_index = 0;
		while ((unsigned int)g_selected_mission_list_index <
			       g_mission_count &&
		       g_mission_list[g_selected_mission_list_index]
				       .mission_idx !=
			       g_pilot_data.mission_description_ids
				       [g_pilot_data.mission_directory_id]) {
			++g_selected_mission_list_index;
		}
	}
	XVT_LOG_DEBUG(
		"network.mission_start directory=%d mission=%d sequence=%d count=%d listed=%d missions=%u index=%d",
		(int)g_pilot_data.mission_directory_id, payload[0],
		g_pilot_data.mission_sequence_active,
		g_pilot_data.mission_sequence_active == 1 ? payload[3] : 0,
		g_mission_list != NULL, (unsigned)g_mission_count,
		g_selected_mission_list_index);
}

/* Part of frontend_net_process_setup_packet for the next and replay
 * mission packets: stores payload[0] in g_game_config.random_seed and logs
 * it. */
static void frontend_net_store_seed(int *payload)
{
	g_game_config.random_seed = payload[0];
	XVT_LOG_DEBUG("network.seed_received seed=%u",
		      (unsigned)g_game_config.random_seed);
}

/* Part of frontend_net_process_setup_packet for a TEAM_ASSIGNMENT to
 * team 10 on a client: takes player payload[0] out of the assigned players
 * of g_mission_setup_player_assignments and out of each team, moving the
 * players after it up one place. */
static void frontend_net_remove_team_player(int *payload)
{
	int player_index;
	int team_index;
	int slot_index;
	int source_slot;

	for (player_index = 0; player_index < MAX_PLAYERS; ++player_index) {
		if (g_mission_setup_player_assignments
			    .assigned_player_ids[player_index] == payload[0]) {
			g_mission_setup_player_assignments
				.assigned_player_ids[player_index] = 0;
			break;
		}
	}
	for (team_index = 0; team_index < TEAM_COUNT; ++team_index) {
		for (slot_index = 0; slot_index < MAX_PLAYERS; ++slot_index) {
			if (g_mission_setup_player_assignments
				    .team_player_ids[team_index][slot_index] ==
			    payload[0]) {
				for (source_slot = slot_index + 1;
				     source_slot < MAX_PLAYERS; ++source_slot) {
					g_mission_setup_player_assignments
						.team_player_ids[team_index]
								[source_slot -
								 1] =
						g_mission_setup_player_assignments
							.team_player_ids
								[team_index]
								[source_slot];
				}
				g_mission_setup_player_assignments
					.team_player_ids[team_index]
							[MAX_PLAYERS - 1] = 0;
				break;
			}
		}
	}
}

/* Part of frontend_net_process_setup_packet for a TEAM_ASSIGNMENT on a
 * client: plays the slot sound when this player is in g_mp_roster, adds
 * player payload[0] to the assigned players and, when it is not on team
 * payload[1] yet, puts it there at place payload[2]. */
static void frontend_net_add_team_player(int *payload)
{
	int roster_index;
	int player_index;
	int slot_index;
	int source_slot;
	int destination_slot;

	frontend_net_find_local_roster_entry(&roster_index);
	if (roster_index < MAX_PLAYERS &&
	    g_game_config.sfx_datapad_enabled != 0) {
		frontend_sound_play_ui_sound(
			"slotsound", 1, 0, 255,
			12 * g_game_config.sfx_datapad_volume, 63);
	}
	for (player_index = 0; player_index < MAX_PLAYERS; ++player_index) {
		if (g_mission_setup_player_assignments
			    .assigned_player_ids[player_index] == payload[0]) {
			break;
		}
	}
	if (player_index == MAX_PLAYERS) {
		for (player_index = 0; player_index < MAX_PLAYERS;
		     ++player_index) {
			if (g_mission_setup_player_assignments
				    .assigned_player_ids[player_index] == 0) {
				g_mission_setup_player_assignments
					.assigned_player_ids[player_index] =
					payload[0];
				break;
			}
		}
	}
	for (slot_index = 0; slot_index < MAX_PLAYERS; ++slot_index) {
		if (g_mission_setup_player_assignments
			    .team_player_ids[payload[1]][slot_index] ==
		    payload[0]) {
			break;
		}
	}
	if (slot_index == MAX_PLAYERS) {
		destination_slot = payload[2];
		for (source_slot = MAX_PLAYERS - 1;
		     source_slot > destination_slot; --source_slot) {
			g_mission_setup_player_assignments
				.team_player_ids[payload[1]][source_slot] =
				g_mission_setup_player_assignments
					.team_player_ids[payload[1]]
							[source_slot - 1];
		}
		g_mission_setup_player_assignments
			.team_player_ids[payload[1]][destination_slot] =
			payload[0];
	}
}

/* Part of frontend_net_process_setup_packet for a
 * FLIGHT_ASSIGNMENT_NOTIFY: plays the slot sound when payload[2] is not -1,
 * datapad sound is on and this player is on team payload[0]. */
static void frontend_net_play_slot_sound(int *payload)
{
	int slot_index;

	if (payload[2] != -1 && g_game_config.sfx_datapad_enabled != 0) {
		for (slot_index = 0; slot_index < MAX_PLAYERS; ++slot_index) {
			if (g_mission_setup_player_assignments
				    .team_player_ids[payload[0]][slot_index] ==
			    net_get_local_player_id()) {
				break;
			}
		}
		if (slot_index != MAX_PLAYERS) {
			frontend_sound_play_ui_sound(
				"slotsound", 1, 0, 255,
				12 * g_game_config.sfx_datapad_volume, 63);
		}
	}
}

/* Part of frontend_net_process_setup_packet for a FLIGHT_ASSIGNMENT:
 * stores flight group payload[2] for team payload[0], place payload[1], in
 * g_mission_setup_player_flight_group_indices. */
static void frontend_net_store_flight_group(int *payload)
{
	XVT_LOG_DEBUG("network.flight_assignment team=%d place=%d group=%d",
		      payload[0], payload[1], payload[2]);
	g_mission_setup_player_flight_group_indices[payload[0] * MAX_PLAYERS +
						    payload[1]] = payload[2];
}

/* Part of frontend_net_process_setup_packet for a PLAYER_READY: sets
 * sender_player_id's entry of g_mp_roster_ready_flags to 1. */
static void frontend_net_on_player_ready(DPID sender_player_id)
{
	int roster_index;

	for (roster_index = 0; roster_index < MAX_PLAYERS; ++roster_index) {
		if ((DPID)g_mp_roster[roster_index].player_id ==
		    sender_player_id) {
			g_mp_roster_ready_flags[roster_index] = 1;
			break;
		}
	}
	XVT_LOG_DEBUG("network.ready_changed player=%u ready=1 index=%d",
		      (unsigned)sender_player_id, roster_index);
}

/* Part of frontend_net_process_setup_packet for a PLAYER_UNREADY: sets
 * sender_player_id's entry of g_mp_roster_ready_flags to 0. */
static void frontend_net_on_player_unready(DPID sender_player_id)
{
	int roster_index;

	for (roster_index = 0; roster_index < MAX_PLAYERS; ++roster_index) {
		if ((DPID)g_mp_roster[roster_index].player_id ==
		    sender_player_id) {
			g_mp_roster_ready_flags[roster_index] = 0;
			break;
		}
	}
	XVT_LOG_DEBUG("network.ready_changed player=%u ready=0 index=%d",
		      (unsigned)sender_player_id, roster_index);
}

/* Part of frontend_net_process_setup_packet for a
 * CLEAR_TEAM_ASSIGNMENTS: zeroes the first MISSION_ASSIGNMENT_TEAM_BYTES
 * bytes of g_mission_setup_player_assignments and its assigned players. */
static void frontend_net_clear_team_tables(void)
{
	memset(&g_mission_setup_player_assignments, 0,
	       MISSION_ASSIGNMENT_TEAM_BYTES);
	memset(g_mission_setup_player_assignments.assigned_player_ids, 0,
	       sizeof(g_mission_setup_player_assignments.assigned_player_ids));
}

/* Part of frontend_net_process_setup_packet for a TEAM_ASSIGNMENTS:
 * copies the team tables and then the assigned players from payload into
 * g_mission_setup_player_assignments. */
static void frontend_net_store_team_tables(int *payload)
{
	memcpy(g_mission_setup_player_assignments.team_player_ids, payload,
	       sizeof(g_mission_setup_player_assignments.team_player_ids));
	memcpy(g_mission_setup_player_assignments.assigned_player_ids,
	       &payload[sizeof(g_mission_setup_player_assignments
				       .team_player_ids) /
			sizeof(payload[0])],
	       sizeof(g_mission_setup_player_assignments.assigned_player_ids));
}

/* Part of frontend_net_process_setup_packet for a
 * CLEAR_FLIGHT_ASSIGNMENTS: fills team payload[0]'s MAX_PLAYERS entries of
 * g_mission_setup_player_flight_group_indices with 0xFF bytes. */
static void frontend_net_clear_flight_groups(int *payload)
{
	memset(&g_mission_setup_player_flight_group_indices[payload[0] *
							    MAX_PLAYERS],
	       0xFF,
	       MAX_PLAYERS *
		       sizeof(g_mission_setup_player_flight_group_indices[0]));
	XVT_LOG_DEBUG("network.flight_groups_cleared team=%d", payload[0]);
}

/* Part of frontend_net_process_setup_packet for a FLIGHT_ASSIGNMENTS:
 * copies team payload[0]'s MAX_PLAYERS entries of
 * g_mission_setup_player_flight_group_indices from payload[1] on. */
static void frontend_net_store_flight_groups(int *payload)
{
	memcpy(&g_mission_setup_player_flight_group_indices[payload[0] *
							    MAX_PLAYERS],
	       &payload[1],
	       MAX_PLAYERS *
		       sizeof(g_mission_setup_player_flight_group_indices[0]));
	XVT_LOG_DEBUG(
		"network.flight_groups team=%d groups=\"%d,%d,%d,%d,%d,%d,%d,%d\"",
		payload[0], payload[1], payload[2], payload[3], payload[4],
		payload[5], payload[6], payload[7], payload[8]);
}

/* Part of frontend_net_process_setup_packet for a MISSION_CHOICE:
 * stores payload[0] in g_frontend_net_packet_arg0. */
static void frontend_net_store_mission_choice(int *payload)
{
	g_frontend_net_packet_arg0 = payload[0];
	XVT_LOG_DEBUG("network.setup_value kind=\"mission_choice\" value=%d",
		      g_frontend_net_packet_arg0);
}

/* Part of frontend_net_process_setup_packet for a BRIEFING_COUNTDOWN
 * or ASSIGNMENT_COUNTDOWN: stores payload[0] in g_frontend_net_packet_arg0.
 */
static void frontend_net_store_countdown(int *payload)
{
	g_frontend_net_packet_arg0 = payload[0];
	XVT_LOG_DEBUG("network.setup_value kind=\"countdown\" value=%d",
		      g_frontend_net_packet_arg0);
}

/* Part of frontend_net_process_setup_packet for a
 * RELEASE_TEAM_RESERVATION or RELEASE_FLIGHT_RESERVATION: stores payload[0]
 * in g_frontend_net_packet_arg0. */
static void frontend_net_store_released_reservation(int *payload)
{
	g_frontend_net_packet_arg0 = payload[0];
	XVT_LOG_DEBUG(
		"network.setup_value kind=\"released_reservation\" value=%d",
		g_frontend_net_packet_arg0);
}

/* Part of frontend_net_process_setup_packet for a GAME_OPTIONS: when
 * this player is in g_mp_roster, copies the game options in payload into
 * g_game_config; logs them either way. */
static void frontend_net_on_game_options(int *payload, uint8_t *payload_bytes)
{
	int roster_index;

	frontend_net_find_local_roster_entry(&roster_index);
	if (roster_index < MAX_PLAYERS) {
		g_game_config.difficulty = payload_bytes[0];
		g_game_config.collisions = payload_bytes[4];
		g_game_config.craft_jumping = payload_bytes[8];
		g_game_config.random_setup = payload_bytes[12];
		g_game_config.battle_length_index = payload_bytes[16];
		g_game_config.in_progress_join = payload_bytes[24];
		g_game_config.craft_selection = payload_bytes[28];
		g_game_config.locate_players = payload_bytes[32];
		g_game_config.craft_waves = payload_bytes[36];
		g_game_config.mission_time_limit = payload_bytes[40];
		g_game_config.last_team_time_limit_minutes = payload_bytes[44];
		g_game_config.random_seed = payload[12];
		g_game_config.internet_play = payload_bytes[52];
		g_game_config.ai_opponents = payload_bytes[56];
		g_game_config.server_update_rate = payload_bytes[60];
		g_game_config.combat_balance = payload_bytes[64];
		g_game_config.continue_battle_or_campaign = payload_bytes[68];
	}
	XVT_LOG_DEBUG(
		"network.game_options applied=%d difficulty=%u collisions=%u jumping=%u random=%u length=%u join=%u craft=%u locate=%u waves=%u time_limit=%u team_limit=%u seed=%u internet=%u ai=%u rate=%u balance=%u continue=%u",
		roster_index < MAX_PLAYERS, (unsigned)payload_bytes[0],
		(unsigned)payload_bytes[4], (unsigned)payload_bytes[8],
		(unsigned)payload_bytes[12], (unsigned)payload_bytes[16],
		(unsigned)payload_bytes[24], (unsigned)payload_bytes[28],
		(unsigned)payload_bytes[32], (unsigned)payload_bytes[36],
		(unsigned)payload_bytes[40], (unsigned)payload_bytes[44],
		(unsigned)payload[12], (unsigned)payload_bytes[52],
		(unsigned)payload_bytes[56], (unsigned)payload_bytes[60],
		(unsigned)payload_bytes[64], (unsigned)payload_bytes[68]);
}

/* Part of frontend_net_process_setup_packet for a
 * LAUNCH_ROSTER_AND_ASSIGNMENTS: stores each g_mp_roster entry's five
 * loadout words from payload, then the flight group bytes after them in
 * g_mission_setup_player_flight_group_indices. */
static void frontend_net_on_launch_roster(int *payload)
{
	int packet_word_index;
	int roster_index;
	int assignment_index;

	packet_word_index = 0;
	for (roster_index = 0; roster_index < MAX_PLAYERS; ++roster_index) {
		g_mp_roster[roster_index].craft_type_override =
			payload[packet_word_index++];
		g_mp_roster[roster_index].craft_option_index =
			payload[packet_word_index++];
		g_mp_roster[roster_index].warhead_option_index =
			payload[packet_word_index++];
		g_mp_roster[roster_index].beam_option_index =
			payload[packet_word_index++];
		g_mp_roster[roster_index].countermeasure_option_index =
			payload[packet_word_index++];
		XVT_LOG_DEBUG(
			"network.loadout index=%d craft=%d option=%d warhead=%d beam=%d countermeasure=%d",
			roster_index,
			g_mp_roster[roster_index].craft_type_override,
			g_mp_roster[roster_index].craft_option_index,
			g_mp_roster[roster_index].warhead_option_index,
			g_mp_roster[roster_index].beam_option_index,
			g_mp_roster[roster_index].countermeasure_option_index);
	}
	for (assignment_index = 0;
	     assignment_index < FLIGHT_GROUP_ASSIGNMENT_COUNT;
	     ++assignment_index) {
		g_mission_setup_player_flight_group_indices[assignment_index] =
			((uint8_t *)&payload[packet_word_index])
				[assignment_index];
	}
}

/* Part of frontend_net_process_setup_packet for a LOADOUT_ROSTER:
 * stores each g_mp_roster entry's five loadout words from payload. */
static void frontend_net_store_roster_loadouts(int *payload)
{
	int packet_word_index;
	int roster_index;

	packet_word_index = 0;
	for (roster_index = 0; roster_index < MAX_PLAYERS; ++roster_index) {
		g_mp_roster[roster_index].craft_type_override =
			payload[packet_word_index++];
		g_mp_roster[roster_index].craft_option_index =
			payload[packet_word_index++];
		g_mp_roster[roster_index].warhead_option_index =
			payload[packet_word_index++];
		g_mp_roster[roster_index].beam_option_index =
			payload[packet_word_index++];
		g_mp_roster[roster_index].countermeasure_option_index =
			payload[packet_word_index++];
		XVT_LOG_DEBUG(
			"network.loadout index=%d craft=%d option=%d warhead=%d beam=%d countermeasure=%d",
			roster_index,
			g_mp_roster[roster_index].craft_type_override,
			g_mp_roster[roster_index].craft_option_index,
			g_mp_roster[roster_index].warhead_option_index,
			g_mp_roster[roster_index].beam_option_index,
			g_mp_roster[roster_index].countermeasure_option_index);
	}
}

/* Part of frontend_net_process_setup_packet for a LOBBY_SELECTION, and
 * for a LOADOUT_ROSTER after its loadouts: a client stores the latency and
 * the packet, drop and retry counts of each player payload lists. */
static void frontend_net_store_link_statistics(int *payload)
{
	int packet_word_index;
	int stats_count;

	stats_count = payload[12];
	packet_word_index = 13;
	while (stats_count > 0) {
		if (net_is_host() == 0) {
			XVT_LOG_DEBUG(
				"network.player_stats player=%u latency=%d packets=%d drops=%d retries=%d",
				(unsigned)payload[packet_word_index],
				payload[packet_word_index + 2],
				payload[packet_word_index + 3],
				payload[packet_word_index + 4],
				payload[packet_word_index + 5]);
			net_set_player_latency_ms(
				payload[packet_word_index],
				payload[packet_word_index + 2]);
			net_set_player_packet_count(
				payload[packet_word_index],
				payload[packet_word_index + 3]);
			net_set_player_packet_drop_count(
				payload[packet_word_index],
				payload[packet_word_index + 4]);
			net_set_player_packet_retry_count(
				payload[packet_word_index],
				payload[packet_word_index + 5]);
		}
		packet_word_index += 6;
		--stats_count;
	}
}

/* Part of frontend_net_process_setup_packet for a CRAFT_LOADOUT: stores
 * the loadout in payload in sender_player_id's g_mp_roster entry, or in
 * every entry when craft selection is host-only, and then also in the
 * g_mission_setup_selected globals. */
static void frontend_net_on_craft_loadout(DPID sender_player_id, int *payload)
{
	int roster_index;
	int craft_option;

	XVT_LOG_DEBUG(
		"network.craft_loadout player=%u category=%d craft=%d option=%d warhead=%d beam=%d countermeasure=%d waves=%d count=%d",
		(unsigned)sender_player_id, payload[0], payload[1], payload[2],
		payload[3], payload[4], payload[5], payload[6], payload[7]);
	for (roster_index = 0; roster_index < MAX_PLAYERS; ++roster_index) {
		if ((DPID)g_mp_roster[roster_index].player_id !=
			    sender_player_id &&
		    g_game_config.craft_selection !=
			    CRAFT_SELECTION_HOST_ONLY) {
			continue;
		}
		craft_option = payload[1];
		if (craft_option != 0) {
			if ((unsigned int)payload[0] <
			    MIN_PRESET_CRAFT_CATEGORY) {
				g_mp_roster[roster_index].craft_type_override =
					0;
			} else if ((unsigned int)payload[0] <=
				   MAX_STANDARD_PRESET_CRAFT_CATEGORY) {
				g_mp_roster[roster_index].craft_type_override =
					g_preset_craft_types[craft_option];
			} else if (payload[0] ==
				   SPECIAL_PRESET_CRAFT_CATEGORY) {
				g_mp_roster[roster_index].craft_type_override =
					g_preset_craft_types
						[craft_option +
						 SPECIAL_PRESET_CRAFT_OFFSET];
			} else {
				g_mp_roster[roster_index].craft_type_override =
					0;
			}
		} else {
			g_mp_roster[roster_index].craft_type_override = 0;
		}
		g_mp_roster[roster_index].craft_option_index = payload[2] - 1;
		g_mp_roster[roster_index].warhead_option_index = payload[3];
		g_mp_roster[roster_index].beam_option_index = payload[4];
		g_mp_roster[roster_index].countermeasure_option_index =
			payload[5];
		if (g_game_config.craft_selection ==
		    CRAFT_SELECTION_HOST_ONLY) {
			g_mission_setup_selected_preset_craft_option_index =
				payload[1];
			g_mission_setup_selected_flight_group_craft_option_index =
				payload[2];
			g_mission_setup_selected_warhead_option_index =
				payload[3];
			g_mission_setup_selected_beam_option_index = payload[4];
			g_mission_setup_selected_countermeasure_option_index =
				payload[5];
			g_mission_setup_selected_wave_count_minus_one =
				payload[6];
			g_mission_setup_selected_craft_count = payload[7];
		}
	}
}

/* Part of frontend_net_process_setup_packet for a BRIEFING_ENTERED:
 * counts one more player in g_frontend_briefing_entered_count. */
static void frontend_net_count_briefing_arrival(DPID sender_player_id)
{
	++g_frontend_briefing_entered_count;
	XVT_LOG_DEBUG("network.briefing_entered player=%u count=%d",
		      (unsigned)sender_player_id,
		      g_frontend_briefing_entered_count);
}

/* Part of frontend_net_process_setup_packet for a TEAM_RESERVATION or
 * FLIGHT_RESERVATION: stores payload[0] in g_frontend_net_packet_arg0 and
 * payload[1] in g_frontend_net_reserving_player_id. */
static void frontend_net_store_reservation(int *payload)
{
	g_frontend_net_packet_arg0 = payload[0];
	g_frontend_net_reserving_player_id = payload[1];
	XVT_LOG_DEBUG("network.reservation player=%u holder=%u",
		      (unsigned)g_frontend_net_packet_arg0,
		      (unsigned)g_frontend_net_reserving_player_id);
}

/* Part of frontend_net_process_setup_packet for a PILOT_RATING: stores
 * payload[0] in g_frontend_net_packet_arg0. */
static void frontend_net_store_pilot_rating(int *payload)
{
	g_frontend_net_packet_arg0 = payload[0];
	XVT_LOG_DEBUG("network.setup_value kind=\"pilot_rating\" value=%d",
		      g_frontend_net_packet_arg0);
}

/* Part of frontend_net_process_setup_packet for a BATTLE_PROGRESS:
 * stores payload's five words in the g_remote_battle globals. */
static void frontend_net_store_battle_progress(int *payload)
{
	g_remote_battle_continuation_active = payload[0];
	g_remote_battle_sequence_continuation_choice = payload[1];
	g_remote_battle_last_completed_mission_index = payload[2];
	g_remote_battle_rebel_victory_count = payload[3];
	g_remote_battle_imperial_victory_count = payload[4];
	XVT_LOG_DEBUG(
		"network.battle_progress active=%d choice=%d last=%d rebel=%d imperial=%d",
		g_remote_battle_continuation_active,
		g_remote_battle_sequence_continuation_choice,
		g_remote_battle_last_completed_mission_index,
		g_remote_battle_rebel_victory_count,
		g_remote_battle_imperial_victory_count);
}

/* Part of frontend_net_process_setup_packet for a
 * SUBMIT_MISSION_CHOICE: sends everyone a MISSION_CHOICE of payload[0],
 * built in g_frontend_net_packet_scratch. */
static void frontend_net_relay_mission_choice(DPID sender_player_id,
					      int *payload)
{
	g_frontend_net_packet_scratch.packet_type = NET_PACKET_MISSION_CHOICE;
	*(int *)&g_frontend_net_packet_scratch.payload[0] = payload[0];
	net_send_packet_and_flush(0, &g_frontend_net_packet_scratch,
				  2 * sizeof(int));
	XVT_LOG_DEBUG("network.mission_choice_relayed player=%u choice=%d",
		      (unsigned)sender_player_id, payload[0]);
}

/* Part of frontend_net_process_network_packets: acts on one packet of the
 * mission setup, briefing and launch screens (assignments, ready flags, game
 * options, loadouts, seeds, countdowns, reservations, battle progress, the
 * mission choice relay) as that function's comment says, and sets
 * *packet_type to NET_PACKET_NONE when the type is unknown. */
void frontend_net_process_setup_packet(int *packet_type, DPID sender_player_id,
				       int *payload, uint8_t *payload_bytes)
{
	switch (*packet_type) {
	case NET_PACKET_TEAM_ASSIGNMENTS_READY:
		memcpy(&g_mission_setup_player_assignments, payload,
		       MISSION_ASSIGNMENT_TEAM_BYTES);
		break;
	case NET_PACKET_FRONTEND_MISSION_START:
		frontend_net_on_mission_start(payload);
		break;
	case NET_PACKET_NEXT_TOURNAMENT_MISSION:
	case NET_PACKET_NEXT_BATTLE_MISSION:
	case NET_PACKET_REPLAY_CURRENT_MISSION:
	case NET_PACKET_NEXT_CAMPAIGN_MISSION:
	case NET_PACKET_REPLAY_CAMPAIGN_MISSION:
		frontend_net_store_seed(payload);
		break;
	case NET_PACKET_TEAM_ASSIGNMENT:
		XVT_LOG_DEBUG(
			"network.team_assignment player=%u team=%d place=%d",
			(unsigned)payload[0], payload[1], payload[2]);
		if (net_is_host() != 0) {
			break;
		}
		if (payload[1] == 10) {
			frontend_net_remove_team_player(payload);
			break;
		}
		frontend_net_add_team_player(payload);
		break;
	case NET_PACKET_FLIGHT_ASSIGNMENT_NOTIFY:
		frontend_net_play_slot_sound(payload);
		/* Continue with the shared flight-group assignment. */
	case NET_PACKET_FLIGHT_ASSIGNMENT:
		frontend_net_store_flight_group(payload);
		break;
	case NET_PACKET_PLAYER_READY:
		frontend_net_on_player_ready(sender_player_id);
		break;
	case NET_PACKET_PLAYER_UNREADY:
		frontend_net_on_player_unready(sender_player_id);
		break;
	case NET_PACKET_RETURN_TO_SETUP:
		g_frontend_skip_screen_entry_setup = 1;
		break;
	case NET_PACKET_CLEAR_TEAM_ASSIGNMENTS:
		frontend_net_clear_team_tables();
		break;
	case NET_PACKET_TEAM_ASSIGNMENTS:
		frontend_net_store_team_tables(payload);
		break;
	case NET_PACKET_CLEAR_FLIGHT_ASSIGNMENTS:
		frontend_net_clear_flight_groups(payload);
		break;
	case NET_PACKET_FLIGHT_ASSIGNMENTS:
		frontend_net_store_flight_groups(payload);
		break;
	case NET_PACKET_MISSION_CHOICE:
		frontend_net_store_mission_choice(payload);
		break;
	case NET_PACKET_BRIEFING_COUNTDOWN:
	case NET_PACKET_ASSIGNMENT_COUNTDOWN:
		frontend_net_store_countdown(payload);
		break;
	case NET_PACKET_RETURN_TO_MISSION_SELECTION:
		break;
	case NET_PACKET_RELEASE_TEAM_RESERVATION:
	case NET_PACKET_RELEASE_FLIGHT_RESERVATION:
		frontend_net_store_released_reservation(payload);
		break;
	case NET_PACKET_GAME_OPTIONS:
		frontend_net_on_game_options(payload, payload_bytes);
		break;
	case NET_PACKET_REPLAY_MISSION:
		frontend_net_store_seed(payload);
		break;
	case NET_PACKET_LAUNCH_ROSTER_AND_ASSIGNMENTS:
		frontend_net_on_launch_roster(payload);
		break;
	case NET_PACKET_LOADOUT_ROSTER:
		frontend_net_store_roster_loadouts(payload);
		/* Continue with the network statistics carried by this packet. */
	case NET_PACKET_LOBBY_SELECTION:
		frontend_net_store_link_statistics(payload);
		break;
	case NET_PACKET_CRAFT_LOADOUT:
		frontend_net_on_craft_loadout(sender_player_id, payload);
		break;
	case NET_PACKET_BRIEFING_ENTERED:
		frontend_net_count_briefing_arrival(sender_player_id);
		break;
	case NET_PACKET_TEAM_RESERVATION:
	case NET_PACKET_FLIGHT_RESERVATION:
		frontend_net_store_reservation(payload);
		break;
	case NET_PACKET_PILOT_RATING:
		frontend_net_store_pilot_rating(payload);
		break;
	case NET_PACKET_BATTLE_PROGRESS:
		frontend_net_store_battle_progress(payload);
		break;
	case NET_PACKET_SUBMIT_MISSION_CHOICE:
		frontend_net_relay_mission_choice(sender_player_id, payload);
		break;
	default:
		XVT_LOG_DEBUG("network.packet_unknown type=%d", *packet_type);
		*packet_type = NET_PACKET_NONE;
		break;
	}
}
