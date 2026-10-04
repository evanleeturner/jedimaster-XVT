#include "xvt/frontend/frontend_mission.h"

#include <stdlib.h>
#include <string.h>

#include "xvt/assets/file.h"
#include "xvt/frontend/briefing_map.h"
#include "xvt/frontend/briefing_script.h"
#include "xvt/frontend/briefing_text.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/frontend_draw.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt/net/net.h"

/* The mission the frontend's setup, briefing and debriefing screens show, as
 * frontend_mission_load_current and frontend_mission_load_current_with_briefing
 * load it from its file. frontend_mission_init_for_briefing empties it to one
 * flight group with win_type 1. */
// GLOBAL: XVT 0xA91CC0
struct frontend_mission g_frontend_mission = {0};

/* Sets up the briefing map for the pilot's current mission:
 * frontend_mission_init_for_briefing, frontend_mission_load_current_with_briefing,
 * then briefing_script_reset_state. Returns 1 on every path. */
// FUNCTION: XVT 0x4F6860
int frontend_mission_load_for_briefing(void)
{
	frontend_mission_init_for_briefing();
	frontend_mission_load_current_with_briefing();
	briefing_script_reset_state();
	return 1;
}

/* Sets the briefing up empty: g_briefing_playback_active 1; the selected
 * flight group, map center and target center, g_active_briefing_index,
 * g_briefing_last_narrated_text_block_idx and g_briefing_text_page_number 0; scales
 * 32. Empties g_frontend_mission to one flight group with win_type 1 and puts in
 * the default script. Allocates the 32 label buffers of 40 bytes, 32 text
 * blocks of 320 bytes and 20 unused buffers of 1024 bytes, without freeing
 * earlier ones or checking the allocations. Turns off both text slots and
 * all markers and labels, and sets g_briefing_map_panel_rect to (0, 0) to (360,
 * 236). */
// FUNCTION: XVT 0x4F69B0
void frontend_mission_init_for_briefing(void)
{
	int16_t index;

	g_briefing_playback_active = 1;
	g_briefing_selected_mission_point14_flight_group_idx = 0;
	g_briefing_map_center.x = 0;
	g_briefing_map_center.y = 0;
	g_briefing_map_target_center.x = 0;
	g_briefing_map_target_center.y = 0;
	g_active_briefing_index = 0;
	g_briefing_map_scale.x = 32;
	g_briefing_last_narrated_text_block_idx = 0;
	g_briefing_map_scale.y = 32;
	g_briefing_map_target_scale.x = 32;
	g_briefing_map_target_scale.y = 32;
	g_briefing_text_page_number = 0;
	memset(&g_frontend_mission, 0, sizeof(g_frontend_mission));
	g_frontend_mission.flight_group_count = 1;
	g_frontend_mission.header.all_waypoints_shown = 0;
	g_frontend_mission.header.win_type = 1;
	briefing_script_init_default_script();
	briefing_script_reset_state();

	for (index = 0; index < 32; ++index) {
		g_briefing_map_label_texts[index] = malloc(40);
	}
	for (index = 0; index < 32; ++index) {
		g_briefing_text_blocks[index] = malloc(320);
	}
	for (index = 0; index < 20; ++index) {
		g_briefing_unused_buffers[index] = malloc(1024);
	}
	for (index = 0; index < 2; ++index) {
		g_briefing_text_slot_active[index] = 0;
	}
	for (index = 0; index < 8; ++index) {
		g_briefing_map_fg_marker_active[index] = 0;
	}
	for (index = 0; index < 8; ++index) {
		g_briefing_map_label_active[index] = 0;
	}
	frontend_draw_rect_assign(&g_briefing_map_panel_rect, 0, 0, 360, 236);
}

/* Loads the pilot's current mission into g_frontend_mission, with the
 * briefing for the pilot's team. Reloads the mission list of
 * g_pilot_data.mission_directory_id, sets g_selected_mission_list_index to the entry
 * whose mission_idx is the pilot's mission in that directory, and opens that
 * file in the directory's folder. Returns without loading when the file does
 * not open, and with only formatVersion set when that is not 12, 13 or 14.
 * Reads the counts, header and flight groups, each message into the slot the
 * file gives before it, each team's goals and each team record the file
 * flags present. Each of the 8 briefings that follow is the script, 10 team
 * flags, then 32 labels and 32 text blocks, each with a 16-bit length. Every
 * briefing flagged for g_pilot_data.team is kept, so a later one replaces an
 * earlier one: its script goes to g_briefing_script, its index to
 * g_active_briefing_index and its strings into g_briefing_map_label_texts and
 * g_briefing_text_blocks; the strings of the others are skipped. Checks no
 * count, slot or string length against the space it fills, and does not
 * check that the list loaded or held the pilot's mission: it then reads past
 * the list's end, or through a NULL list. */
// FUNCTION: XVT 0x4F6B80
void frontend_mission_load_current_with_briefing(void)
{
	enum {
		MISSION_FILE_PATH_CAPACITY = 256,
		BRIEFING_COUNT = 8,
		TEAM_COUNT = 10,
		BRIEFING_LABEL_COUNT = 32,
		BRIEFING_LABEL_CAPACITY = 40,
		BRIEFING_TEXT_COUNT = 32,
		BRIEFING_TEXT_CAPACITY = 320,
	};

	uint8_t team_uses_briefing;
	uint16_t indexed_record;
	uint16_t text_length;
	int load_briefing_text;
	int briefing_index;
	char file_name[MISSION_FILE_PATH_CAPACITY];
	struct frontend_briefing_script briefing_script;
	xvt_file *stream;
	int flight_group_index;
	int message_index;
	unsigned int team_index;
	int global_goal_index;
	int label_index;
	int text_index;
	char *briefing_text;
	struct global_goal *team_goals;

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

	sprintf(file_name, "%s\\%s",
		g_mission_directory_names[g_pilot_data.mission_directory_id],
		g_mission_list[g_selected_mission_list_index].file_name);
	stream = file_open(file_name, g_file_mode_read_binary);
	if (stream == NULL) {
		return;
	}

	memset(&g_frontend_mission, 0, sizeof(g_frontend_mission));
	file_read_word(stream, &g_frontend_mission.format_version);
	if (g_frontend_mission.format_version != 14 &&
	    g_frontend_mission.format_version != 13 &&
	    g_frontend_mission.format_version != 12) {
		file_close(stream);
		return;
	}

	file_read_word(stream, &g_frontend_mission.flight_group_count);
	file_read_word(stream, &g_frontend_mission.message_count);
	file_read_bytes(stream, &g_frontend_mission.header,
			sizeof(g_frontend_mission.header));
	for (flight_group_index = 0;
	     flight_group_index <
	     (int16_t)g_frontend_mission.flight_group_count;
	     ++flight_group_index) {
		file_read_bytes(
			stream,
			&g_frontend_mission.flight_groups[flight_group_index],
			sizeof(g_frontend_mission
				       .flight_groups[flight_group_index]));
	}

	/* indexed_record holds the 16-bit word read ahead of each record: the message's slot here, then each
	 * team's goal count, then whether a team record follows. */
	for (message_index = 0;
	     message_index < (int16_t)g_frontend_mission.message_count;
	     ++message_index) {
		file_read_word(stream, &indexed_record);
		file_read_bytes(
			stream,
			&g_frontend_mission.messages[(int16_t)indexed_record],
			sizeof(g_frontend_mission.messages[0]));
	}

	for (team_index = 0; team_index < TEAM_COUNT; ++team_index) {
		team_goals = g_frontend_mission.global_goals[team_index];
		file_read_word(stream, &indexed_record);
		for (global_goal_index = 0;
		     global_goal_index < (int16_t)indexed_record;
		     ++global_goal_index) {
			file_read_bytes(stream, &team_goals[global_goal_index],
					sizeof(team_goals[global_goal_index]));
		}
	}

	for (team_index = 0; team_index < TEAM_COUNT; ++team_index) {
		file_read_word(stream, &indexed_record);
		if (indexed_record != 0) {
			file_read_bytes(
				stream, &g_frontend_mission.teams[team_index],
				sizeof(g_frontend_mission.teams[team_index]));
		}
	}

	for (briefing_index = 0; briefing_index < BRIEFING_COUNT;
	     ++briefing_index) {
		load_briefing_text = 0;
		file_read_bytes(stream, &briefing_script,
				sizeof(briefing_script));
		for (team_index = 0; team_index < TEAM_COUNT; ++team_index) {
			file_read_byte(stream, &team_uses_briefing);
			if (g_pilot_data.team == (int)team_index &&
			    team_uses_briefing != 0) {
				g_briefing_script = briefing_script;
				load_briefing_text = 1;
				g_active_briefing_index = briefing_index;
			}
		}

		for (label_index = 0; label_index < BRIEFING_LABEL_COUNT;
		     ++label_index) {
			briefing_text = g_briefing_map_label_texts[label_index];
			if (load_briefing_text != 0) {
				memset(briefing_text, 0,
				       BRIEFING_LABEL_CAPACITY);
			}
			file_read_word(stream, &text_length);
			if (text_length != 0) {
				if (load_briefing_text != 0) {
					file_read_bytes(stream, briefing_text,
							(int16_t)text_length);
				} else {
					file_seek(stream, (int16_t)text_length,
						  SEEK_CUR);
				}
			}
			if (load_briefing_text != 0) {
				briefing_text[(int16_t)text_length] = '\0';
			}
		}

		for (text_index = 0; text_index < BRIEFING_TEXT_COUNT;
		     ++text_index) {
			briefing_text = g_briefing_text_blocks[text_index];
			if (load_briefing_text != 0) {
				memset(briefing_text, 0,
				       BRIEFING_TEXT_CAPACITY);
			}
			file_read_word(stream, &text_length);
			if (text_length != 0) {
				if (load_briefing_text != 0) {
					file_read_bytes(stream, briefing_text,
							(int16_t)text_length);
				} else {
					file_seek(stream, (int16_t)text_length,
						  SEEK_CUR);
				}
			}
			if (load_briefing_text != 0) {
				briefing_text[(int16_t)text_length] = '\0';
			}
		}
	}
	file_close(stream);
}

/* Loads a mission file into *out_mission the way
 * frontend_mission_load_current_with_briefing does, without the briefings. Leaves
 * *out_mission untouched when the file does not open, and holding only
 * formatVersion when that is not 12, 13 or 14. Checks no count or slot
 * against the arrays. */
// FUNCTION: XVT 0x4F6F30
void frontend_mission_load_file(const char *file_name,
				struct frontend_mission *out_mission)
{
	xvt_file *stream;
	int flight_group_index;
	struct xvt_flight_group *flight_group;
	int message_index;
	int global_goal_index;
	struct global_goal *team_goal;
	struct global_goal *global_goal;
	int team_index;
	uint16_t indexed_record;

	stream = file_open(file_name, "rb");
	if (stream != NULL) {
		memset(out_mission, 0, sizeof(*out_mission));
		file_read_word(stream, &out_mission->format_version);
		if (out_mission->format_version != 14 &&
		    out_mission->format_version != 13 &&
		    out_mission->format_version != 12) {
			file_close(stream);
			return;
		}

		file_read_word(stream, &out_mission->flight_group_count);
		file_read_word(stream, &out_mission->message_count);
		flight_group_index = 0;
		file_read_bytes(stream, &out_mission->header,
				sizeof(out_mission->header));
		if ((int16_t)out_mission->flight_group_count > 0) {
			flight_group = out_mission->flight_groups;
			do {
				++flight_group_index;
				file_read_bytes(stream, flight_group,
						sizeof(*flight_group));
				++flight_group;
			} while ((int16_t)out_mission->flight_group_count >
				 flight_group_index);
		}

		/* indexed_record holds the 16-bit word read ahead of each record: the message's slot here, then
		 * each team's goal count, then whether a team record follows. */
		for (message_index = 0;
		     message_index < (int16_t)out_mission->message_count;
		     ++message_index) {
			file_read_word(stream, &indexed_record);
			file_read_bytes(
				stream,
				&out_mission->messages[(int16_t)indexed_record],
				sizeof(out_mission->messages[0]));
		}

		team_index = 10;
		global_goal = &out_mission->global_goals[0][0];
		do {
			global_goal_index = 0;
			file_read_word(stream, &indexed_record);
			if ((int16_t)indexed_record > 0) {
				team_goal = global_goal;
				do {
					++global_goal_index;
					file_read_bytes(stream, team_goal,
							sizeof(*team_goal));
					++team_goal;
				} while ((int16_t)indexed_record >
					 global_goal_index);
			}
			global_goal += 7;
			--team_index;
		} while (team_index != 0);

		for (team_index = 0; team_index < 10; ++team_index) {
			file_read_word(stream, &indexed_record);
			if (indexed_record != 0) {
				file_read_bytes(
					stream, &out_mission->teams[team_index],
					sizeof(out_mission->teams[team_index]));
			}
		}

		file_close(stream);
	}
}

/* frontend_mission_load_current_with_briefing without the briefings: reloads the
 * directory's mission list, sets g_selected_mission_list_index and loads the
 * pilot's mission into g_frontend_mission. The modern build returns without
 * loading when the list did not load or lacks the pilot's mission; the
 * original build then reads past the list's end, or through a NULL list. */
// FUNCTION: XVT 0x4F70F0
void frontend_mission_load_current(void)
{
	char file_name[256];
	xvt_file *stream;
	int flight_group_index;
	int message_index;
	int team_index;
	int global_goal_index;
	uint16_t indexed_record;

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
#ifdef XVT_MODERN
	/* The caller selects an available entry when the saved selection is absent. */
	if (g_mission_list == NULL ||
	    (unsigned int)g_selected_mission_list_index >= g_mission_count) {
		return;
	}
#endif

	sprintf(file_name, "%s\\%s",
		g_mission_directory_names[g_pilot_data.mission_directory_id],
		g_mission_list[g_selected_mission_list_index].file_name);
	stream = file_open(file_name, g_file_mode_read_binary);
	if (stream == NULL) {
		return;
	}

	memset(&g_frontend_mission, 0, sizeof(g_frontend_mission));
	file_read_word(stream, &g_frontend_mission.format_version);
	if (g_frontend_mission.format_version != 14 &&
	    g_frontend_mission.format_version != 13 &&
	    g_frontend_mission.format_version != 12) {
		file_close(stream);
		return;
	}

	file_read_word(stream, &g_frontend_mission.flight_group_count);
	file_read_word(stream, &g_frontend_mission.message_count);
	file_read_bytes(stream, &g_frontend_mission.header,
			sizeof(g_frontend_mission.header));
	for (flight_group_index = 0;
	     flight_group_index <
	     (int16_t)g_frontend_mission.flight_group_count;
	     ++flight_group_index) {
		file_read_bytes(
			stream,
			&g_frontend_mission.flight_groups[flight_group_index],
			sizeof(g_frontend_mission
				       .flight_groups[flight_group_index]));
	}

	/* indexed_record holds the 16-bit word read ahead of each record: the message's slot here, then each
	 * team's goal count, then whether a team record follows. */
	for (message_index = 0;
	     message_index < (int16_t)g_frontend_mission.message_count;
	     ++message_index) {
		file_read_word(stream, &indexed_record);
		file_read_bytes(
			stream,
			&g_frontend_mission.messages[(int16_t)indexed_record],
			sizeof(g_frontend_mission.messages[0]));
	}

	for (team_index = 0; team_index < 10; ++team_index) {
		file_read_word(stream, &indexed_record);
		for (global_goal_index = 0;
		     global_goal_index < (int16_t)indexed_record;
		     ++global_goal_index) {
			file_read_bytes(
				stream,
				&g_frontend_mission
					 .global_goals[team_index]
						      [global_goal_index],
				sizeof(g_frontend_mission.global_goals[0][0]));
		}
	}

	for (team_index = 0; team_index < 10; ++team_index) {
		file_read_word(stream, &indexed_record);
		if (indexed_record != 0) {
			file_read_bytes(
				stream, &g_frontend_mission.teams[team_index],
				sizeof(g_frontend_mission.teams[team_index]));
		}
	}
	file_close(stream);
}

/* Sets up the pilot record's per-mission player state before a mission
 * flies. Clears g_mp_roster entries 1 to 7 in single player, or in a network
 * game the entries of players DirectPlay no longer lists. Zeroes the pilot's
 * mission score, kill tables, last_mission_stats and team results, and
 * g_local_pilot_network_player_index. Copies every roster player into
 * g_pilot_data.network_players at the same index: name, craft, rating, and the
 * craft, warhead, beam and countermeasure choices (the last three less 1),
 * with zeroed results and the flight group of the player's slot in
 * g_mission_setup_player_assignments; a craft choice whose optional craft is
 * CRAFT_SPECIES_UNKNOWN becomes -1. The local player's index goes to
 * g_local_pilot_network_player_index. With a sequence active, the player count
 * goes to battle_sequence_state for combat engagements and to
 * campaign_sequence_state for training exercises. At the first mission of a
 * melee sequence it also marks which teams take part (0 in
 * ai_opponent_source_team_and_type_flag, the rest -1) and stores the player and
 * team counts. g_pilot_data.current_faction_id becomes, in melees and
 * tournaments, 0 when the local player's craft is an X-wing, Y-wing, A-wing,
 * B-wing or Z-95 and else 1, and elsewhere the IFF of the local player's
 * flight group; in melees and tournaments the other side's faction_statistics
 * entry gets mission_sequence_active 0. The mission choice then goes into
 * faction_statistics entry 2 in a network game, which also sets entries 0 and
 * 1's mission_sequence_active to 0, else into the current faction's entry. Ends
 * with net_compact_reliable_peer_slots_for_roster. Does not check that the IFF is
 * under 4 before it picks a faction_statistics entry with it. */
// FUNCTION: XVT 0x4FAD00
void frontend_mission_init_player_state(void)
{
	int roster_index;
	int player_count;

	if (g_frontend_mission_session_mode ==
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		for (roster_index = 1; roster_index < 8; roster_index++) {
			memset(&g_mp_roster[roster_index], 0,
			       sizeof(g_mp_roster[roster_index]));
		}
	} else {
		for (roster_index = 0; roster_index < 8; roster_index++) {
			if (g_mp_roster[roster_index].player_id != 0 &&
			    net_find_player(
				    g_mp_roster[roster_index].player_id) ==
				    NULL) {
				memset(&g_mp_roster[roster_index], 0,
				       sizeof(g_mp_roster[roster_index]));
			}
		}
	}

	g_pilot_data.mission_score = 0;
	g_local_pilot_network_player_index = 0;
	memset(g_pilot_data.kills_full_on_player, 0,
	       sizeof(g_pilot_data.kills_full_on_player));
	memset(g_pilot_data.kills_shared_on_player, 0,
	       sizeof(g_pilot_data.kills_shared_on_player));
	memset(g_pilot_data.kills_full_on_flight_group, 0,
	       sizeof(g_pilot_data.kills_full_on_flight_group));
	memset(g_pilot_data.kills_shared_on_flight_group, 0,
	       sizeof(g_pilot_data.kills_shared_on_flight_group));
	memset(g_pilot_data.kills_full_from_player, 0,
	       sizeof(g_pilot_data.kills_full_from_player));
	memset(g_pilot_data.kills_shared_from_player, 0,
	       sizeof(g_pilot_data.kills_shared_from_player));
	memset(g_pilot_data.kills_full_from_flight_group, 0,
	       sizeof(g_pilot_data.kills_full_from_flight_group));
	memset(g_pilot_data.kills_shared_from_flight_group, 0,
	       sizeof(g_pilot_data.kills_shared_from_flight_group));
	memset(&g_pilot_data.last_mission_stats, 0,
	       sizeof(g_pilot_data.last_mission_stats));
	memset(g_pilot_data.teams, 0, sizeof(g_pilot_data.teams));
	player_count = 0;

	if (g_pilot_data.mission_directory_id == MISSION_DIRECTORY_MELEES &&
	    g_pilot_data.mission_sequence_active == 1 &&
	    g_pilot_data.melee_tournament_sequence_state
			    .current_mission_index == 0) {
		/* roster_index counts the ten teams here, not roster entries. */
		for (roster_index = 0; roster_index < 10; roster_index++) {
			g_pilot_data.melee_tournament_sequence_state
				.team_standings[roster_index]
				.ai_opponent_source_team_and_type_flag = -1;
		}
	}

	for (roster_index = 0; roster_index < 8; roster_index++) {
		if (g_mp_roster[roster_index].player_id != 0) {
			int player_id;
			int team_index;
			int assignment_slot;
			int flight_group_index;

			player_count++;
			memcpy(g_pilot_data.network_players[roster_index]
				       .friendly_name,
			       g_mp_roster[roster_index].name, 13);
			g_pilot_data.network_players[roster_index]
				.friendly_name[12] = '\0';
			g_pilot_data.network_players[roster_index].craft_id =
				g_mp_roster[roster_index].craft_type_override;
			g_pilot_data.network_players[roster_index]
				.craft_option =
				g_mp_roster[roster_index].craft_option_index;
			g_pilot_data.network_players[roster_index]
				.warhead_option =
				g_mp_roster[roster_index].warhead_option_index -
				1;
			g_pilot_data.network_players[roster_index].beam_option =
				g_mp_roster[roster_index].beam_option_index - 1;
			g_pilot_data.network_players[roster_index]
				.countermeasure_option =
				g_mp_roster[roster_index]
					.countermeasure_option_index -
				1;
			player_id = g_mp_roster[roster_index].player_id;
			g_pilot_data.network_players[roster_index]
				.direct_play_id = player_id;
			g_pilot_data.network_players[roster_index].rating =
				g_mp_roster[roster_index].pilot_rating;
			g_pilot_data.network_players[roster_index].total_score =
				0;
			g_pilot_data.network_players[roster_index].kills = 0;
			g_pilot_data.network_players[roster_index]
				.kills_shared = 0;
			g_pilot_data.network_players[roster_index]
				.craft_inspected = 0;
			g_pilot_data.network_players[roster_index]
				.kills_assist = 0;
			g_pilot_data.network_players[roster_index]
				.total_losses = 0;
			g_pilot_data.network_players[roster_index].has_left = 0;

			for (team_index = 0; team_index < 10; team_index++) {
				for (assignment_slot = 0; assignment_slot < 8;
				     assignment_slot++) {
					if (g_mission_setup_player_assignments
						    .team_player_ids
							    [team_index]
							    [assignment_slot] ==
					    player_id) {
						flight_group_index =
							g_mission_setup_player_flight_group_indices
								[team_index *
									 8 +
								 assignment_slot];
						g_pilot_data
							.network_players
								[roster_index]
							.flight_group_id =
							flight_group_index;
						if (g_pilot_data.network_players
								    [roster_index]
									    .craft_id ==
							    0 &&
						    g_pilot_data.network_players
								    [roster_index]
									    .craft_option !=
							    -1 &&
						    g_frontend_mission
								    .flight_groups
									    [flight_group_index]
								    .optional_craft
									    [g_pilot_data
										     .network_players
											     [roster_index]
										     .craft_option] ==
							    CRAFT_SPECIES_UNKNOWN) {
							g_pilot_data
								.network_players
									[roster_index]
								.craft_option =
								-1;
						}
					}
				}
			}
			if (net_get_local_player_id() == player_id) {
				g_local_pilot_network_player_index =
					roster_index;
			}
		}
	}

	if (g_pilot_data.mission_directory_id ==
		    MISSION_DIRECTORY_COMBAT_ENGAGEMENTS &&
	    g_pilot_data.mission_sequence_active == 1) {
		g_pilot_data.battle_sequence_state.human_player_count =
			player_count;
	}
	if (g_pilot_data.mission_directory_id ==
		    MISSION_DIRECTORY_TRAINING_EXERCISES &&
	    g_pilot_data.mission_sequence_active == 1) {
		g_pilot_data.campaign_sequence_state.human_player_count =
			player_count;
	}

	if (g_pilot_data.mission_directory_id == MISSION_DIRECTORY_MELEES &&
	    g_pilot_data.mission_sequence_active == 1 &&
	    g_pilot_data.melee_tournament_sequence_state
			    .current_mission_index == 0) {
		int assignment_slot;
		int team_index;

		for (team_index = 0; team_index < g_team_count; team_index++) {
			for (assignment_slot = 0;
			     assignment_slot <
			     g_team_player_flight_group_count[team_index];
			     assignment_slot++) {
				if (g_mission_setup_player_assignments
					    .team_player_ids[team_index]
							    [assignment_slot] !=
				    0) {
					break;
				}
			}
			if (assignment_slot <
			    g_team_player_flight_group_count[team_index]) {
				g_pilot_data.melee_tournament_sequence_state
					.team_standings[team_index]
					.ai_opponent_source_team_and_type_flag =
					0;
			}
		}
		if ((g_game_config.ai_opponents == 1 ||
		     g_frontend_mission_session_mode ==
			     FRONTEND_MISSION_SESSION_SINGLEPLAYER) &&
		    (int16_t)g_frontend_mission.flight_group_count > 0) {
			int flight_group_index;
			for (flight_group_index = 0;
			     flight_group_index <
			     (int16_t)g_frontend_mission.flight_group_count;
			     flight_group_index++) {
				if (g_frontend_mission
					    .flight_groups[flight_group_index]
					    .player_number != 0) {
					int team =
						g_frontend_mission
							.flight_groups
								[flight_group_index]
							.team;
					g_pilot_data
						.melee_tournament_sequence_state
						.team_standings[team]
						.ai_opponent_source_team_and_type_flag =
						0;
				}
			}
		}
		g_pilot_data.melee_tournament_sequence_state
			.human_player_count = player_count;
		if (g_frontend_mission_session_mode ==
			    FRONTEND_MISSION_SESSION_SINGLEPLAYER ||
		    g_game_config.ai_opponents != 0) {
			g_pilot_data.melee_tournament_sequence_state
				.participating_team_count = g_team_count;
		} else {
			int participating_teams = 0;
			for (team_index = 0; team_index < g_team_count;
			     team_index++) {
				for (assignment_slot = 0;
				     assignment_slot <
				     g_team_player_flight_group_count
					     [team_index];
				     assignment_slot++) {
					if (g_mission_setup_player_assignments
						    .team_player_ids
							    [team_index]
							    [assignment_slot] !=
					    0) {
						break;
					}
				}
				if (assignment_slot <
				    g_team_player_flight_group_count
					    [team_index]) {
					participating_teams++;
					g_pilot_data
						.melee_tournament_sequence_state
						.team_standings[team_index]
						.ai_opponent_source_team_and_type_flag =
						0;
				}
			}
			g_pilot_data.melee_tournament_sequence_state
				.participating_team_count = participating_teams;
		}
	}

	if (g_pilot_data.mission_directory_id == MISSION_DIRECTORY_MELEES ||
	    g_pilot_data.mission_directory_id ==
		    MISSION_DIRECTORY_TOURNAMENTS) {
		int flight_group_id =
			g_pilot_data
				.network_players
					[g_local_pilot_network_player_index]
				.flight_group_id;
		int craft_id =
			g_pilot_data
				.network_players
					[g_local_pilot_network_player_index]
				.craft_id;
		if (craft_id == 0) {
			int craft_option =
				g_pilot_data
					.network_players
						[g_local_pilot_network_player_index]
					.craft_option;
			if (craft_option == -1 || craft_option >= 10) {
				craft_id =
					g_frontend_mission
						.flight_groups[flight_group_id]
						.craft_type;
			} else {
				craft_id =
					g_frontend_mission
						.flight_groups[flight_group_id]
						.optional_craft[craft_option];
			}
		}
		if (craft_id >= 1 && (craft_id <= 4 || craft_id == 14)) {
			g_pilot_data.current_faction_id = 0;
			g_pilot_data.faction_statistics[1]
				.mission_sequence_active = 0;
		} else {
			g_pilot_data.current_faction_id = 1;
			g_pilot_data.faction_statistics[0]
				.mission_sequence_active = 0;
		}
	} else {
		int flight_group_id =
			g_pilot_data
				.network_players
					[g_local_pilot_network_player_index]
				.flight_group_id;
		g_pilot_data.current_faction_id =
			g_frontend_mission.flight_groups[flight_group_id].iff;
	}

	if (g_frontend_mission_session_mode !=
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		g_pilot_data.faction_statistics[2].team = g_pilot_data.team;
		g_pilot_data.faction_statistics[2].mission_directory_id =
			g_pilot_data.mission_directory_id;
		memcpy(g_pilot_data.faction_statistics[2]
			       .mission_description_ids,
		       g_pilot_data.mission_description_ids,
		       sizeof(g_pilot_data.faction_statistics[2]
				      .mission_description_ids));
		g_pilot_data.faction_statistics[2].mission_sequence_active =
			g_pilot_data.mission_sequence_active;
		g_pilot_data.faction_statistics[2]
			.saved_mission_description_id =
			g_pilot_data.saved_mission_description_id;
		g_pilot_data.faction_statistics[0].mission_sequence_active = 0;
		g_pilot_data.faction_statistics[1].mission_sequence_active = 0;
	} else {
		g_pilot_data.faction_statistics[g_pilot_data.current_faction_id]
			.team = g_pilot_data.team;
		g_pilot_data.faction_statistics[g_pilot_data.current_faction_id]
			.mission_directory_id =
			g_pilot_data.mission_directory_id;
		if (g_pilot_data.mission_directory_id ==
		    MISSION_DIRECTORY_TRAINING_EXERCISES) {
			g_pilot_data
				.faction_statistics[g_pilot_data
							    .current_faction_id]
				.mission_description_ids[0] =
				g_pilot_data.mission_description_ids[0];
		} else if (g_pilot_data.mission_directory_id ==
				   MISSION_DIRECTORY_MELEES ||
			   g_pilot_data.mission_directory_id ==
				   MISSION_DIRECTORY_TOURNAMENTS) {
			g_pilot_data
				.faction_statistics[g_pilot_data
							    .current_faction_id]
				.mission_description_ids[1] =
				g_pilot_data.mission_description_ids[1];
			g_pilot_data
				.faction_statistics[g_pilot_data
							    .current_faction_id]
				.mission_description_ids[2] =
				g_pilot_data.mission_description_ids[2];
		} else {
			g_pilot_data
				.faction_statistics[g_pilot_data
							    .current_faction_id]
				.mission_description_ids[3] =
				g_pilot_data.mission_description_ids[3];
			g_pilot_data
				.faction_statistics[g_pilot_data
							    .current_faction_id]
				.mission_description_ids[4] =
				g_pilot_data.mission_description_ids[4];
		}
		g_pilot_data.faction_statistics[g_pilot_data.current_faction_id]
			.mission_sequence_active =
			g_pilot_data.mission_sequence_active;
		g_pilot_data.faction_statistics[2].mission_sequence_active = 0;
		g_pilot_data.faction_statistics[g_pilot_data.current_faction_id]
			.saved_mission_description_id =
			g_pilot_data.saved_mission_description_id;
	}
	net_compact_reliable_peer_slots_for_roster();
}
