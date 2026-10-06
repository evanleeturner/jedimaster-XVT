#include "xvt/frontend/mission_debrief.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xvt/assets/file.h"
#include "xvt/frontend/briefing_text.h"
#include "xvt/frontend/concourse.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/cutscene.h"
#include "xvt/frontend/front_image.h"
#include "xvt/frontend/frontend.h"
#include "xvt/frontend/frontend_button.h"
#include "xvt/frontend/frontend_cursor.h"
#include "xvt/frontend/frontend_dialog.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/frontend_draw.h"
#include "xvt/frontend/frontend_flight.h"
#include "xvt/frontend/frontend_mission.h"
#include "xvt/frontend/frontend_mouse.h"
#include "xvt/frontend/frontend_scrollbar.h"
#include "xvt/frontend/frontend_string.h"
#include "xvt/frontend/frontend_text.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt/input/keyboard.h"
#include "xvt/net/flight_net.h"
#include "xvt/net/frontend_net.h"
#include "xvt/net/net.h"
#include "xvt/util/time.h"
#include "xvt_runtime/log/log.h"
#include "xvt_runtime/runtime/campaign_task.h"
#include "xvt_runtime/runtime/dialog_task.h"
#include "xvt_runtime/runtime/frontend_cleanup.h"
#include "xvt_runtime/runtime/mission_dialogs.h"

/* 1 asks mission_debrief_draw_player_statistics_page to recount its rows and
 * totals and scroll back to the top; that page sets it to 0 when it does.
 * mission_debrief_update sets it on its first frame and
 * mission_debrief_draw_tab_bar on every tab change. */
// GLOBAL: XVT 0x66D918
static int g_debrief_stats_page_needs_rebuild = 0;
/* 1 when the debriefing finds the local player among the network players
 * that left the game (has_left set in g_pilot_data.network_players). Set to 0
 * and then worked out on the debriefing's first frame, by
 * mission_debrief_update in the original build and xvt_campaign_task_enter_debrief
 * in the modern one. While it is 1, mission_debrief_update draws a
 * "disconnecting" notice on frames 0 and 1, shuts the session down and says
 * so on frame 1, and offers Done instead of Disconnect. */
// GLOBAL: XVT 0x52D1C8
int g_debrief_disconnected_from_net_game = 0;
/* Nothing in this build reads this flag. */
/* -1 at start; mission_debrief_update sets it to 0 on its first frame and to
 * 1 when NET_PACKET_SESSION_CANCELLED or NET_PACKET_TEAM_ASSIGNMENTS_READY
 * arrives. */
// GLOBAL: XVT 0x66D9B8
static int g_debrief_session_cancel_or_teams_ready_received = -1;
/* The debriefing page shown: 0 the mission overview, 1 the player
 * statistics, 2 the sequence's page (the campaign text, the tournament
 * summary or the battle summary), drawn only while
 * g_pilot_data.mission_sequence_active is 1. mission_debrief_update picks it on
 * its first frame from the mission type and sequence; mission_debrief_draw_tab_bar
 * changes it when a tab is clicked. */
// GLOBAL: XVT 0x66D9E0
static int g_debrief_tab = 0;
/* Mission list description of the tournament, battle or campaign being
 * played, shown by the tournament and battle summary pages and by
 * mission_setup_battle_choice_update. mission_debrief_update empties it on its
 * first frame and, in a sequence, copies the entry it finds in the
 * sequence's mission list; mission_setup_battle_choice_build_list copies the
 * chosen battle's. */
// GLOBAL: XVT 0xA91B90
char g_mission_sequence_description[256] = {0};
/* Per team id 0 to 7, 1 when the tournament standings entry's
 * ai_opponent_source_team_and_type_flag is not -1. Only mission_debrief_prepare
 * writes it, for melees and tournaments; it clears all 10 entries first, so
 * entries 8 and 9 stay 0. */
// GLOBAL: XVT 0x66D8A0
int g_debrief_team_in_standings[10] = {0};
/* Per team id, 1 when no network player flies for the team but a player
 * flight group of a melee or tournament does, counted when AI opponents are
 * on, in single player, or with one human. Only mission_debrief_prepare writes
 * it; the overview and tournament summary list such a team's flight groups
 * by their rating. */
// GLOBAL: XVT 0x66D8D8
int g_debrief_team_has_only_ai_pilots[10] = {0};
/* Position of the local pilot's team (g_pilot_data.team) in
 * g_debrief_sorted_team_ids, found by a search that also needs
 * g_debrief_team_has_player set at that position, though that array is indexed
 * by team id; 0 when the search finds none. Only mission_debrief_prepare
 * writes it. The player statistics page shows it as the place, 1st on, in a
 * network melee. */
// GLOBAL: XVT 0x66D900
int g_debrief_local_team_rank_index = 0;
/* Teams with a network player, plus, in a melee or tournament with AI
 * opponents, in single player or with one human, the teams with only AI
 * player flight groups. Only mission_debrief_prepare writes it; the player
 * statistics page shows it as the count of teams or pilots in a network
 * melee. */
// GLOBAL: XVT 0x66D904
int g_debrief_active_team_count = 0;
/* Team ids with g_debrief_team_has_player set, in the overview's order, then -1.
 * mission_debrief_prepare fills it by insertion: a team moves ahead of one
 * with a lower teams[].mission_score and, outside melees and tournaments,
 * also ahead of one that did not complete the mission when it did. */
// GLOBAL: XVT 0x66D920
int g_debrief_sorted_team_ids[10] = {0};
/* Per team id, 1 when a network player flies for the team or, in a melee or
 * tournament with AI opponents, in single player or with one human, when a
 * player flight group is on it. Only mission_debrief_prepare writes it. */
// GLOBAL: XVT 0x66D968
int g_debrief_team_has_player[10] = {0};
/* Slots of g_pilot_data.network_players with a direct_play_id, in the
 * overview's order, then -1. mission_debrief_prepare fills it by insertion: a
 * player moves ahead of one whose team did not complete the mission when its
 * team did, or ahead of one with a lower total_score unless its team did not
 * complete and the other's did. */
// GLOBAL: XVT 0x66D990
int g_debrief_sorted_player_ids[8] = {0};
/* 1 when, in network play, the last mission's kills_full_on_player or
 * kills_shared_on_player has a nonzero entry, so the player statistics page
 * shows its player-kills-by-rank section. That page sets it when it rebuilds;
 * nothing else writes it. */
// GLOBAL: XVT 0x66D9B0
int g_debrief_has_player_kills_by_rating = 0;
/* The overview's "killed" list: whom the local player killed, then -1.
 * Entries under 8 are slots of g_pilot_data.network_players, in order of
 * kills_full_on_player, highest first. In a melee or tournament each player
 * flight group no network player flies is then inserted as 8 plus its
 * index, ahead of any entry with fewer full kills, or as many full kills and
 * fewer shared ones. mission_debrief_prepare fills it by insertion and drops
 * whatever is pushed past the 8th entry. */
// GLOBAL: XVT 0x66D9C0
int g_debrief_kills_on_combatant_ids[8] = {0};
/* The overview's "killed by" list: who killed the local player, built like
 * g_debrief_kills_on_combatant_ids from kills_full_from_player and, in a melee or
 * tournament, the flight groups' kills_full_from_flight_group and
 * kills_shared_from_flight_group. Only mission_debrief_prepare writes it. */
// GLOBAL: XVT 0x66D9F8
int g_debrief_kills_from_combatant_ids[8] = {0};
/* Team ids in tournament standings order, then -1: in a melee or tournament
 * mission_debrief_prepare inserts each team with g_debrief_team_in_standings set
 * ahead of any with a lower melee_tournament_sequence_state.team_standings[]
 * total_score, filling at most 8 entries; otherwise all stay -1. The
 * tournament summary and mission_debrief_update's choice of background read
 * it. */
// GLOBAL: XVT 0x66DA18
int g_debrief_standings_team_ids[10] = {0};
/* Shared kills on human pilots in the last mission, summed over the victims'
 * ratings in last_mission_stats; only entry 0 is used. Reset and summed by
 * mission_debrief_draw_player_statistics_page when it rebuilds. */
// GLOBAL: XVT 0x66DA40
int g_debrief_player_kills_shared_total[3] = {0};
/* 1 when the mission is a melee in which no team has more than one player
 * flight group, so the overview and the tournament summary rank pilots
 * rather than teams and the player statistics page says pilots. Only
 * mission_debrief_prepare writes it. */
// GLOBAL: XVT 0x66DA4C
int g_debrief_rank_by_pilot = 0;
/* Shared kills in the last mission, summed over craft types from
 * last_mission_stats, which keeps that mission in row 0 whatever its type; only
 * entry 0 is used. Reset and summed by mission_debrief_draw_player_statistics_page
 * when it rebuilds. */
// GLOBAL: XVT 0x66D8C8
static int g_debrief_kills_shared_total[4] = {0};
/* Kill assists in the last mission, summed over craft types from
 * last_mission_stats; only entry 0 is used. Reset and summed by
 * mission_debrief_draw_player_statistics_page when it rebuilds;
 * mission_debrief_update sets entry 3 to 0. */
// GLOBAL: XVT 0x66D908
static int g_debrief_assists_total[4] = {0};
/* Full kills on human pilots in the last mission, summed over their ratings
 * from last_mission_stats; only entry 0 is used. Reset and summed by
 * mission_debrief_draw_player_statistics_page when it rebuilds. */
// GLOBAL: XVT 0x66D948
static int g_debrief_player_kills_full_total[4] = {0};
/* Full kills on AI pilots in the last mission, summed over their 6 ratings from
 * last_mission_stats; only entry 0 is used. Reset and summed by
 * mission_debrief_draw_player_statistics_page when it rebuilds. */
// GLOBAL: XVT 0x66D958
static int g_debrief_non_player_kills_full_total[4] = {0};
/* 1 when the last mission has a full or shared kill of any craft type, so
 * the player statistics page shows its craft-kills-by-type section. Only
 * that page writes it, when it rebuilds. */
// GLOBAL: XVT 0x66D9B4
static int g_debrief_has_craft_kills_by_type_section = 0;
/* 1 when, in network play, the last mission's kills_full_from_player or
 * kills_shared_from_player has a nonzero entry, so the player statistics page
 * shows its losses-to-players section. Only that page writes it, when it
 * rebuilds. */
// GLOBAL: XVT 0x66D9E4
static int g_debrief_has_losses_from_players_section = 0;
/* Shared kills on AI pilots in the last mission, summed over their 6
 * ratings in last_mission_stats; only entry 0 is used. Reset and summed by
 * mission_debrief_draw_player_statistics_page when it rebuilds. */
// GLOBAL: XVT 0x66D9E8
static int g_debrief_non_player_kills_shared_total[3] = {0};
/* Scratch flag of mission_debrief_draw_player_statistics_page, its only user:
 * 1 when the craft type being looked at has a kill, and while drawing the
 * awards, 1 once the "Award" label is drawn. */
// GLOBAL: XVT 0x66D9F4
static int g_debrief_craft_kill_row_has_data = 0;
/* Times AI pilots killed the local player in the last mission, summed over
 * their 6 ratings from last_mission_stats.killed_by_ai_rating_per_mt. Reset and
 * summed by mission_debrief_draw_player_statistics_page when it rebuilds. */
// GLOBAL: XVT 0x66DA50
static int g_debrief_losses_to_non_player_pilots_total[1] = {0};
/* Times human pilots killed the local player in the last mission, summed
 * over their 25 ratings from last_mission_stats.killed_by_player_rating_per_mt. Reset
 * and summed by mission_debrief_draw_player_statistics_page when it rebuilds. */
// GLOBAL: XVT 0x66DA60
static int g_debrief_losses_to_player_pilots_total[1] = {0};
/* First row shown on the player statistics page, which shows 21 rows. Only
 * mission_debrief_draw_player_statistics_page writes it: 0 when it rebuilds,
 * then the scroll bar's position when the page has more than 21 rows. */
// GLOBAL: XVT 0x66DA6C
static int g_debrief_player_stats_scroll_row = 0;
/* Rows of the player statistics page, counted when it rebuilds; the scroll
 * bar appears above 21. Only mission_debrief_draw_player_statistics_page writes
 * it. */
// GLOBAL: XVT 0x66DA70
static int g_debrief_player_stats_row_count = 0;

/* Leaves the debriefing: frees g_mission_list and g_mission_text and sets
 * each to NULL, frees the "background" image, resets the scrollable controls
 * and clears the mouse input gate. Returns 0; frame_counter is ignored. */
// FUNCTION: XVT 0x4FE100
int mission_debrief_exit(int frame_counter)
{
	(void)frame_counter;
	XVT_LOG_DEBUG("debrief.closed list=%d text=%d", g_mission_list != NULL,
		      g_mission_text != NULL);

	if (g_mission_list != NULL) {
		free(g_mission_list);
		g_mission_list = NULL;
	}
	if (g_mission_text != NULL) {
		free(g_mission_text);
		g_mission_text = NULL;
	}
	front_image_free_resource_by_name("background");
	frontend_reset_scrollable_controls();
	frontend_mouse_clear_input_gate();
	return 0;
}

/* Runs one frame of the debriefing screen shown after a mission. On its first
 * frame (frame_counter 0) it shows the cursor and, after a completed campaign
 * mission, plays its cutscene; a network player for whom
 * cutscene_play_for_current_mission_phase returns 0 then tells the host it left and
 * goes to the concourse. It allocates the 4096-byte g_mission_text for a
 * campaign, zeroes g_frontend_chat_team_only and g_frontend_first_visible_line, sets
 * g_debrief_disconnected_from_net_game, clears the session ready flags of network
 * players who left, refreshes the session roster and, in network play after a
 * promotion or demotion, sets the local player's DirectPlay long name to one
 * character, the new rating plus 1. The modern build does that part through
 * xvt_campaign_task_enter_debrief and returns 0 until it returns 1. It places
 * the cursor, calls
 * mission_debrief_prepare, picks g_debrief_tab, clears g_mp_roster_ready_flags,
 * g_mission_sequence_description and g_debrief_session_cancel_or_teams_ready_received
 * and sets g_debrief_stats_page_needs_rebuild. In a sequence it fills
 * g_mission_sequence_description and saves the sequence state in the battle or
 * campaign continuation slot of g_pilot_data for a later resume (active for a
 * single player or the host; a client's campaign state goes in slot index + 12,
 * inactive), or marks the slot inactive when the sequence ended. It loads
 * g_mission_list, sets g_selected_mission_list_index, reads a campaign mission's
 * text with mission_debrief_read_outcome_text, marks the network players ready,
 * picks the background by mission type and outcome, sends the lobby state in
 * network play and draws the frame. Every frame, except frames 0 and 1 for a
 * disconnected player, it draws the mission title, acts on network packets,
 * draws the g_debrief_tab page, the pilot's rating and name, the tab bar, the
 * shared controls and two buttons. A next-mission or replay packet advances the
 * sequence's current_mission_index where it applies, records the session in
 * g_pilot_data (launch_session_marker 1, local_player_id, is_host,
 * num_human_players_last_mission, session_mode) and goes to
 * mission_setup_enter_next_mission or mission_setup_enter_current_mission;
 * NET_PACKET_REPLAY_MISSION clears the last mission's results and goes to
 * flight loading; a host cancel or a return to mission selection leaves for the
 * concourse or mission setup; a NET_PACKET_PLAYER_READY clears its sender's
 * g_mp_roster_ready_flags entry. The left button lets a client disconnect, after a
 * confirm dialog unless it was disconnected already, and lets a single player
 * or the host abort a sequence, after a confirm dialog, or pick a new mission;
 * for them, the debriefing of a won campaign's last mission also sets
 * is_finished in the faction's sp_campaigns entry. The right button continues or
 * reflies a sequence, and outside one flies the mission again: directly in
 * single player, by a packet to everyone from the host. Returns 1 when
 * frontend_handle_common_screen_controls(4) returns 1 (the player confirmed
 * quitting the game); otherwise 0, except that the modern build returns
 * xvt_dialog_continue_with's result once it opens a dialog. Does not check that
 * the local player is among g_pilot_data.network_players, or g_mission_list and
 * g_selected_mission_list_index, before using them; a network client's battle
 * check reads battle_result_counts, which only the host and a single player
 * fill. */
// FUNCTION: XVT 0x4FE160
int mission_debrief_update(int frame_counter)
{
	enum {
		PLAYER_COUNT = 8,
		BATTLE_RESULT_COUNT = 3,
		MISSION_TEXT_BUFFER_SIZE = 0x1000,
		TEXT_CODE_LABEL = 4,
		TEXT_CODE_RATING = 6,
		WHITE_COLOR = 0xFFFF,
		NETWORK_DISCONNECT_FRAME = 1,
		NETWORK_MESSAGE_FRAME_COUNT = 2,
	};

	int show_sequence_continue_button = 0;
	int local_network_player_index = 0;
	int battle_result_counts[BATTLE_RESULT_COUNT];
	struct RECT rect;
	struct RECT saved_clip_rect;

	if (frame_counter == 0) {
		if (xvt_campaign_task_enter_debrief() != 1) {
			return 0;
		}

		{
			int use_exit_cursor = 1;

			if (g_pilot_data.mission_sequence_active != 0 &&
			    (net_is_host() != 0 ||
			     g_frontend_mission_session_mode ==
				     FRONTEND_MISSION_SESSION_SINGLEPLAYER)) {
				use_exit_cursor = 0;
				if (g_pilot_data.mission_directory_id ==
				    MISSION_DIRECTORY_MELEES) {
					if (g_pilot_data.melee_tournament_sequence_state
							    .mission_count -
						    g_pilot_data
							    .melee_tournament_sequence_state
							    .current_mission_index ==
					    1) {
						use_exit_cursor = 1;
					}
				} else if (
					g_pilot_data.mission_directory_id ==
					MISSION_DIRECTORY_COMBAT_ENGAGEMENTS) {
					int mission_index;

					battle_result_counts
						[BATTLE_MISSION_RESULT_IMPERIAL_VICTORY] =
							0;
					battle_result_counts
						[BATTLE_MISSION_RESULT_REBEL_VICTORY] =
							0;
					battle_result_counts
						[BATTLE_MISSION_RESULT_DRAW] =
							0;
					for (mission_index = 0;
					     mission_index <=
					     (int)g_pilot_data
						     .battle_sequence_state
						     .current_mission_index;
					     ++mission_index) {
						++battle_result_counts
							[g_pilot_data
								 .battle_sequence_state
								 .mission_results
									 [mission_index]];
					}
					XVT_LOG_DEBUG(
						"debrief.battle_counted imperial=%d rebel=%d draws=%d index=%d needed=%d",
						battle_result_counts
							[BATTLE_MISSION_RESULT_IMPERIAL_VICTORY],
						battle_result_counts
							[BATTLE_MISSION_RESULT_REBEL_VICTORY],
						battle_result_counts
							[BATTLE_MISSION_RESULT_DRAW],
						(int)g_pilot_data
							.battle_sequence_state
							.current_mission_index,
						g_pilot_data
							.battle_sequence_state
							.victories_needed);
					if (g_pilot_data.battle_sequence_state
							    .victories_needed ==
						    battle_result_counts
							    [BATTLE_MISSION_RESULT_IMPERIAL_VICTORY] ||
					    g_pilot_data.battle_sequence_state
							    .victories_needed ==
						    battle_result_counts
							    [BATTLE_MISSION_RESULT_REBEL_VICTORY]) {
						use_exit_cursor = 1;
					}
				} else if (
					g_pilot_data.campaign_sequence_state
							.mission_count -
						g_pilot_data
							.campaign_sequence_state
							.current_mission_index ==
					1) {
					use_exit_cursor = 1;
				}
			}
			if (use_exit_cursor != 0) {
				frontend_cursor_set_pos(149, 463);
			} else {
				frontend_cursor_set_pos(37, 445);
			}
			XVT_LOG_DEBUG("debrief.cursor_placed exit=%d",
				      use_exit_cursor);
		}

		mission_debrief_prepare();
		switch (g_pilot_data.mission_directory_id) {
		case MISSION_DIRECTORY_TRAINING_EXERCISES:
			if (g_pilot_data.mission_sequence_active == 0) {
				g_debrief_tab = 1;
			} else {
				g_debrief_tab = 2;
			}
			break;
		case MISSION_DIRECTORY_MELEES:
			if (g_pilot_data.mission_sequence_active != 0 &&
			    g_pilot_data.melee_tournament_sequence_state
						    .mission_count -
					    g_pilot_data
						    .melee_tournament_sequence_state
						    .current_mission_index ==
				    1) {
				g_debrief_tab = 2;
			} else {
				g_debrief_tab = 0;
			}
			break;
		case MISSION_DIRECTORY_TOURNAMENTS:
			if (g_pilot_data.melee_tournament_sequence_state
					    .mission_count -
				    g_pilot_data.melee_tournament_sequence_state
					    .current_mission_index ==
			    1) {
				g_debrief_tab = 2;
			} else {
				g_debrief_tab = 0;
			}
			break;
		case MISSION_DIRECTORY_COMBAT_ENGAGEMENTS:
			if (g_pilot_data.mission_sequence_active == 0) {
				g_debrief_tab = 1;
			} else {
				g_debrief_tab = 2;
			}
			break;
		case MISSION_DIRECTORY_BATTLES:
			g_debrief_tab = 2;
			break;
		case MISSION_DIRECTORY_CAMPAIGNS:
			g_debrief_tab = 1;
			break;
		default:
			XVT_LOG_WARN("debrief.page_kept directory=%d page=%d",
				     (int)g_pilot_data.mission_directory_id,
				     g_debrief_tab);
			break;
		}
		memset(g_mp_roster_ready_flags, 0,
		       sizeof(g_mp_roster_ready_flags));
		/* Only element 0 of this array is summed and drawn; element 3,
		 * cleared here, is not read anywhere. */
		g_debrief_assists_total[3] = 0;
		g_debrief_session_cancel_or_teams_ready_received = 0;
		g_debrief_stats_page_needs_rebuild = 1;
		g_mission_sequence_description[0] = 0;

		if (g_pilot_data.mission_sequence_active == 1) {
			if (g_pilot_data.mission_directory_id ==
			    MISSION_DIRECTORY_TRAINING_EXERCISES) {
				g_pilot_data.mission_directory_id =
					MISSION_DIRECTORY_CAMPAIGNS;
			} else {
				++g_pilot_data.mission_directory_id;
			}
			mission_setup_load_mission_list(
				g_pilot_data.mission_directory_id);
			if (g_mission_list != NULL) {
				g_selected_mission_list_index = 0;
				while ((unsigned int)g_selected_mission_list_index <
					       g_mission_count &&
				       g_mission_list[g_selected_mission_list_index]
						       .mission_idx !=
					       g_pilot_data.mission_description_ids
						       [g_pilot_data
								.mission_directory_id]) {
					++g_selected_mission_list_index;
				}
				XVT_LOG_DEBUG(
					"debrief.mission_listed directory=%d mission=%d index=%d missions=%u",
					(int)g_pilot_data.mission_directory_id,
					(int)g_pilot_data.mission_description_ids
						[g_pilot_data
							 .mission_directory_id],
					g_selected_mission_list_index,
					g_mission_count);
				if ((unsigned int)
					    g_selected_mission_list_index <
				    g_mission_count) {
					strcpy(g_mission_sequence_description,
					       g_mission_list
						       [g_selected_mission_list_index]
							       .description);
				}
			}
			if (g_mission_list == NULL) {
				XVT_LOG_ERROR(
					"debrief.list_missing directory=%d",
					(int)g_pilot_data.mission_directory_id);
			}
			if (g_mission_list != NULL &&
			    (unsigned int)g_selected_mission_list_index >=
				    g_mission_count) {
				XVT_LOG_WARN(
					"debrief.not_listed directory=%d mission=%d missions=%u",
					(int)g_pilot_data.mission_directory_id,
					(int)g_pilot_data.mission_description_ids
						[g_pilot_data
							 .mission_directory_id],
					g_mission_count);
			}

			if (g_pilot_data.mission_directory_id ==
			    MISSION_DIRECTORY_BATTLES) {
				if (g_pilot_data.battle_sequence_state
						    .victories_needed !=
					    battle_result_counts
						    [BATTLE_MISSION_RESULT_IMPERIAL_VICTORY] &&
				    g_pilot_data.battle_sequence_state
						    .victories_needed !=
					    battle_result_counts
						    [BATTLE_MISSION_RESULT_REBEL_VICTORY]) {
					if (g_frontend_mission_session_mode ==
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
						g_pilot_data
							.sp_battle_continuations
								[g_mission_list[g_selected_mission_list_index]
									 .mission_idx]
							.is_active = 1;
						g_pilot_data
							.sp_battle_continuations
								[g_mission_list[g_selected_mission_list_index]
									 .mission_idx]
							.random_seed =
							g_game_config
								.random_seed;
						g_pilot_data
							.sp_battle_continuations
								[g_mission_list[g_selected_mission_list_index]
									 .mission_idx]
							.battle_length_index =
							(uint8_t)g_game_config
								.battle_length_index;
						g_pilot_data
							.sp_battle_continuations
								[g_mission_list[g_selected_mission_list_index]
									 .mission_idx]
							.random_setup =
							g_game_config
								.random_setup;
						memcpy(&g_pilot_data
								.sp_battle_continuations
									[g_mission_list[g_selected_mission_list_index]
										 .mission_idx]
								.sequence_state,
						       &g_pilot_data
								.battle_sequence_state,
						       sizeof(g_pilot_data
								      .sp_battle_continuations
									      [0]
								      .sequence_state));
						XVT_LOG_INFO(
							"debrief.progress_recorded kind=\"battle\" mission=%d index=%d score=%d role=\"solo\" active=%d seed=%u",
							g_mission_list
								[g_selected_mission_list_index]
									.mission_idx,
							(int)g_pilot_data
								.battle_sequence_state
								.current_mission_index,
							g_pilot_data
								.battle_sequence_state
								.cumulative_score,
							(int)g_pilot_data
								.sp_battle_continuations
									[g_mission_list[g_selected_mission_list_index]
										 .mission_idx]
								.is_active,
							(unsigned)g_game_config
								.random_seed);
					} else {
						g_pilot_data
							.mp_battle_continuations
								[g_mission_list[g_selected_mission_list_index]
									 .mission_idx]
							.is_active =
							net_is_host() != 0;
						g_pilot_data
							.mp_battle_continuations
								[g_mission_list[g_selected_mission_list_index]
									 .mission_idx]
							.random_seed =
							g_game_config
								.random_seed;
						g_pilot_data
							.mp_battle_continuations
								[g_mission_list[g_selected_mission_list_index]
									 .mission_idx]
							.battle_length_index =
							(uint8_t)g_game_config
								.battle_length_index;
						g_pilot_data
							.mp_battle_continuations
								[g_mission_list[g_selected_mission_list_index]
									 .mission_idx]
							.random_setup =
							g_game_config
								.random_setup;
						memcpy(&g_pilot_data
								.mp_battle_continuations
									[g_mission_list[g_selected_mission_list_index]
										 .mission_idx]
								.sequence_state,
						       &g_pilot_data
								.battle_sequence_state,
						       sizeof(g_pilot_data
								      .mp_battle_continuations
									      [0]
								      .sequence_state));
						XVT_LOG_INFO(
							"debrief.progress_recorded kind=\"battle\" mission=%d index=%d score=%d role=\"network\" active=%d seed=%u",
							g_mission_list
								[g_selected_mission_list_index]
									.mission_idx,
							(int)g_pilot_data
								.battle_sequence_state
								.current_mission_index,
							g_pilot_data
								.battle_sequence_state
								.cumulative_score,
							(int)g_pilot_data
								.mp_battle_continuations
									[g_mission_list[g_selected_mission_list_index]
										 .mission_idx]
								.is_active,
							(unsigned)g_game_config
								.random_seed);
					}
				} else if (
					g_frontend_mission_session_mode ==
					FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
					g_pilot_data
						.sp_battle_continuations
							[g_mission_list
								 [g_selected_mission_list_index]
									 .mission_idx]
						.is_active = 0;
					XVT_LOG_INFO(
						"debrief.progress_ended kind=\"battle\" mission=%d index=%d score=%d role=\"solo\"",
						g_mission_list
							[g_selected_mission_list_index]
								.mission_idx,
						(int)g_pilot_data
							.battle_sequence_state
							.current_mission_index,
						g_pilot_data
							.battle_sequence_state
							.cumulative_score);
				} else {
					g_pilot_data
						.mp_battle_continuations
							[g_mission_list
								 [g_selected_mission_list_index]
									 .mission_idx]
						.is_active = 0;
					XVT_LOG_INFO(
						"debrief.progress_ended kind=\"battle\" mission=%d index=%d score=%d role=\"network\"",
						g_mission_list
							[g_selected_mission_list_index]
								.mission_idx,
						(int)g_pilot_data
							.battle_sequence_state
							.current_mission_index,
						g_pilot_data
							.battle_sequence_state
							.cumulative_score);
				}
			} else if (g_pilot_data.mission_directory_id ==
				   MISSION_DIRECTORY_CAMPAIGNS) {
				if (g_pilot_data.campaign_sequence_state
							    .mission_count -
						    g_pilot_data
							    .campaign_sequence_state
							    .current_mission_index !=
					    1 ||
				    g_pilot_data.campaign_sequence_state
						    .last_mission_completed !=
					    1) {
					if (g_frontend_mission_session_mode ==
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
						g_pilot_data
							.sp_campaign_continuations
								[g_mission_list[g_selected_mission_list_index]
									 .mission_idx]
							.is_active = 1;
						g_pilot_data
							.sp_campaign_continuations
								[g_mission_list[g_selected_mission_list_index]
									 .mission_idx]
							.random_seed =
							g_game_config
								.random_seed;
						g_pilot_data
							.sp_campaign_continuations
								[g_mission_list[g_selected_mission_list_index]
									 .mission_idx]
							.random_setup =
							g_game_config
								.random_setup;
						memcpy(&g_pilot_data
								.sp_campaign_continuations
									[g_mission_list[g_selected_mission_list_index]
										 .mission_idx]
								.sequence_state,
						       &g_pilot_data
								.campaign_sequence_state,
						       sizeof(g_pilot_data
								      .sp_campaign_continuations
									      [0]
								      .sequence_state));
						XVT_LOG_INFO(
							"debrief.progress_recorded kind=\"campaign\" mission=%d index=%d score=%d role=\"solo\" active=%d seed=%u",
							g_mission_list
								[g_selected_mission_list_index]
									.mission_idx,
							(int)g_pilot_data
								.campaign_sequence_state
								.current_mission_index,
							(int)g_pilot_data
								.campaign_sequence_state
								.cumulative_score,
							(int)g_pilot_data
								.sp_campaign_continuations
									[g_mission_list[g_selected_mission_list_index]
										 .mission_idx]
								.is_active,
							(unsigned)g_game_config
								.random_seed);
					} else if (net_is_host() != 0) {
						g_pilot_data
							.mp_campaign_continuations
								[g_mission_list[g_selected_mission_list_index]
									 .mission_idx]
							.is_active = 1;
						g_pilot_data
							.mp_campaign_continuations
								[g_mission_list[g_selected_mission_list_index]
									 .mission_idx]
							.random_seed =
							g_game_config
								.random_seed;
						g_pilot_data
							.mp_campaign_continuations
								[g_mission_list[g_selected_mission_list_index]
									 .mission_idx]
							.random_setup =
							g_game_config
								.random_setup;
						memcpy(&g_pilot_data
								.mp_campaign_continuations
									[g_mission_list[g_selected_mission_list_index]
										 .mission_idx]
								.sequence_state,
						       &g_pilot_data
								.campaign_sequence_state,
						       sizeof(g_pilot_data
								      .mp_campaign_continuations
									      [0]
								      .sequence_state));
						XVT_LOG_INFO(
							"debrief.progress_recorded kind=\"campaign\" mission=%d index=%d score=%d role=\"host\" active=%d seed=%u",
							g_mission_list
								[g_selected_mission_list_index]
									.mission_idx,
							(int)g_pilot_data
								.campaign_sequence_state
								.current_mission_index,
							(int)g_pilot_data
								.campaign_sequence_state
								.cumulative_score,
							(int)g_pilot_data
								.mp_campaign_continuations
									[g_mission_list[g_selected_mission_list_index]
										 .mission_idx]
								.is_active,
							(unsigned)g_game_config
								.random_seed);
					} else {
						g_pilot_data
							.mp_campaign_continuations
								[g_mission_list[g_selected_mission_list_index]
									 .mission_idx +
								 12]
							.is_active = 0;
						g_pilot_data
							.mp_campaign_continuations
								[g_mission_list[g_selected_mission_list_index]
									 .mission_idx +
								 12]
							.random_seed =
							g_game_config
								.random_seed;
						g_pilot_data
							.mp_campaign_continuations
								[g_mission_list[g_selected_mission_list_index]
									 .mission_idx +
								 12]
							.random_setup =
							g_game_config
								.random_setup;
						memcpy(&g_pilot_data
								.mp_campaign_continuations
									[g_mission_list[g_selected_mission_list_index]
										 .mission_idx +
									 12]
								.sequence_state,
						       &g_pilot_data
								.campaign_sequence_state,
						       sizeof(g_pilot_data
								      .mp_campaign_continuations
									      [0]
								      .sequence_state));
						XVT_LOG_INFO(
							"debrief.progress_recorded kind=\"campaign\" mission=%d index=%d score=%d role=\"client\" active=%d seed=%u",
							g_mission_list
								[g_selected_mission_list_index]
									.mission_idx,
							(int)g_pilot_data
								.campaign_sequence_state
								.current_mission_index,
							(int)g_pilot_data
								.campaign_sequence_state
								.cumulative_score,
							(int)g_pilot_data
								.mp_campaign_continuations
									[g_mission_list[g_selected_mission_list_index]
										 .mission_idx +
									 12]
								.is_active,
							(unsigned)g_game_config
								.random_seed);
					}
				} else if (
					g_frontend_mission_session_mode ==
					FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
					g_pilot_data
						.sp_campaign_continuations
							[g_mission_list
								 [g_selected_mission_list_index]
									 .mission_idx]
						.is_active = 0;
					XVT_LOG_INFO(
						"debrief.progress_ended kind=\"campaign\" mission=%d index=%d score=%d role=\"solo\"",
						g_mission_list
							[g_selected_mission_list_index]
								.mission_idx,
						(int)g_pilot_data
							.campaign_sequence_state
							.current_mission_index,
						(int)g_pilot_data
							.campaign_sequence_state
							.cumulative_score);
				} else if (net_is_host() != 0) {
					g_pilot_data
						.mp_campaign_continuations
							[g_mission_list
								 [g_selected_mission_list_index]
									 .mission_idx]
						.is_active = 0;
					XVT_LOG_INFO(
						"debrief.progress_ended kind=\"campaign\" mission=%d index=%d score=%d role=\"host\"",
						g_mission_list
							[g_selected_mission_list_index]
								.mission_idx,
						(int)g_pilot_data
							.campaign_sequence_state
							.current_mission_index,
						(int)g_pilot_data
							.campaign_sequence_state
							.cumulative_score);
				} else {
					g_pilot_data
						.mp_campaign_continuations
							[g_mission_list[g_selected_mission_list_index]
								 .mission_idx +
							 12]
						.is_active = 0;
					XVT_LOG_INFO(
						"debrief.progress_ended kind=\"campaign\" mission=%d index=%d score=%d role=\"client\"",
						g_mission_list
							[g_selected_mission_list_index]
								.mission_idx,
						(int)g_pilot_data
							.campaign_sequence_state
							.current_mission_index,
						(int)g_pilot_data
							.campaign_sequence_state
							.cumulative_score);
				}
			}

			if (g_pilot_data.mission_directory_id ==
			    MISSION_DIRECTORY_CAMPAIGNS) {
				g_pilot_data.mission_directory_id =
					MISSION_DIRECTORY_TRAINING_EXERCISES;
			} else {
				--g_pilot_data.mission_directory_id;
			}
		}

		mission_setup_load_mission_list(
			g_pilot_data.mission_directory_id);
		if (g_mission_list != NULL) {
			g_selected_mission_list_index = 0;
			while ((unsigned int)g_selected_mission_list_index <
				       g_mission_count &&
			       g_mission_list[g_selected_mission_list_index]
					       .mission_idx !=
				       g_pilot_data.mission_description_ids
					       [g_pilot_data
							.mission_directory_id]) {
				++g_selected_mission_list_index;
			}
			XVT_LOG_DEBUG(
				"debrief.mission_listed directory=%d mission=%d index=%d missions=%u",
				(int)g_pilot_data.mission_directory_id,
				(int)g_pilot_data.mission_description_ids
					[g_pilot_data.mission_directory_id],
				g_selected_mission_list_index, g_mission_count);
		}
		if (g_mission_list == NULL) {
			XVT_LOG_ERROR("debrief.list_missing directory=%d",
				      (int)g_pilot_data.mission_directory_id);
		}
		if (g_mission_list != NULL &&
		    (unsigned int)g_selected_mission_list_index >=
			    g_mission_count) {
			XVT_LOG_WARN(
				"debrief.not_listed directory=%d mission=%d missions=%u",
				(int)g_pilot_data.mission_directory_id,
				(int)g_pilot_data.mission_description_ids
					[g_pilot_data.mission_directory_id],
				g_mission_count);
		}
		if (g_pilot_data.mission_directory_id ==
			    MISSION_DIRECTORY_TRAINING_EXERCISES &&
		    g_pilot_data.mission_sequence_active == 1) {
			if (g_mission_text == NULL) {
				XVT_LOG_ERROR(
					"debrief.text_alloc_failed bytes=4096");
			}
			mission_debrief_read_outcome_text(
				g_mission_text,
				g_pilot_data.campaign_sequence_state
					.last_mission_completed);
		}
		mission_debrief_mark_network_players_ready();
		/* Only here does local_network_player_index hold the local
		 * player's slot; the other loops in this function use it to
		 * walk all eight network players. */
		local_network_player_index = 0;
		if (g_frontend_mission_session_mode !=
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
			for (; local_network_player_index < PLAYER_COUNT;
			     ++local_network_player_index) {
				if (net_get_local_player_id() ==
				    g_pilot_data
					    .network_players
						    [local_network_player_index]
					    .direct_play_id) {
					break;
				}
			}
		}
		XVT_LOG_DEBUG(
			"debrief.local_pilot entry=%d fg=%d",
			local_network_player_index,
			local_network_player_index < PLAYER_COUNT
				? g_pilot_data
					  .network_players
						  [local_network_player_index]
					  .flight_group_id
				: -1);
		if (local_network_player_index == PLAYER_COUNT) {
			XVT_LOG_WARN("debrief.local_pilot_missing mode=%d",
				     (int)g_frontend_mission_session_mode);
		}

		if (g_pilot_data.mission_directory_id ==
		    MISSION_DIRECTORY_TRAINING_EXERCISES) {
			int player_flight_group =
				g_pilot_data
					.network_players
						[local_network_player_index]
					.flight_group_id;

			if (g_pilot_data.mission_sequence_active == 1) {
				if (g_pilot_data.campaign_sequence_state
					    .last_mission_completed != 0) {
					if (g_frontend_mission
						    .flight_groups
							    [player_flight_group]
						    .iff != 0) {
						front_image_register_resource_default(
							"frontres\\debcawi.bmp",
							"background");
					} else {
						front_image_register_resource_default(
							"frontres\\debcawr.bmp",
							"background");
					}
				} else {
					if (g_frontend_mission
						    .flight_groups
							    [player_flight_group]
						    .iff != 0) {
						front_image_register_resource_default(
							"frontres\\debcali.bmp",
							"background");
					} else {
						front_image_register_resource_default(
							"frontres\\debcalr.bmp",
							"background");
					}
				}
			} else {
				int evaluation =
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.mission_awards[2];

				if (evaluation != 0) {
					evaluation = (evaluation == 6) + 1;
				}
				XVT_LOG_DEBUG(
					"debrief.evaluation award=%d result=%d",
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.mission_awards[2],
					evaluation);
				if (evaluation == 1) {
					if (g_frontend_mission
						    .flight_groups
							    [player_flight_group]
						    .iff != 0) {
						front_image_register_resource_default(
							"frontres\\debtrwi.bmp",
							"background");
					} else {
						front_image_register_resource_default(
							"frontres\\debtrwr.bmp",
							"background");
					}
				} else if (evaluation == 2) {
					if (g_frontend_mission
						    .flight_groups
							    [player_flight_group]
						    .iff != 0) {
						front_image_register_resource_default(
							"frontres\\debtrbi.bmp",
							"background");
					} else {
						front_image_register_resource_default(
							"frontres\\debtrbr.bmp",
							"background");
					}
				} else {
					if (g_frontend_mission
						    .flight_groups
							    [player_flight_group]
						    .iff != 0) {
						front_image_register_resource_default(
							"frontres\\debtrli.bmp",
							"background");
					} else {
						front_image_register_resource_default(
							"frontres\\debtrlr.bmp",
							"background");
					}
				}
			}
		} else if (g_pilot_data.mission_directory_id ==
			   MISSION_DIRECTORY_MELEES) {
			int tournament_result = 0;
			int melee_result = 0;

			if (g_pilot_data.mission_sequence_active == 1 &&
			    g_pilot_data.melee_tournament_sequence_state
						    .mission_count -
					    g_pilot_data
						    .melee_tournament_sequence_state
						    .current_mission_index ==
				    1) {
				int standing_index;

				for (standing_index = 0; standing_index < 9;
				     ++standing_index) {
					int team = g_debrief_standings_team_ids
						[standing_index];

					if (team == -1) {
						break;
					}
					if (g_pilot_data.team == team) {
						tournament_result = 1;
					}
					if (g_pilot_data
						    .melee_tournament_sequence_state
						    .team_standings
							    [g_debrief_standings_team_ids
								     [standing_index +
								      1]]
						    .total_score <
					    g_pilot_data
						    .melee_tournament_sequence_state
						    .team_standings[team]
						    .total_score) {
						break;
					}
				}
			}
			if (g_pilot_data
				    .faction_statistics
					    [g_pilot_data.current_faction_id]
				    .mission_awards[1] != 0) {
				tournament_result =
					(g_pilot_data
						 .faction_statistics
							 [g_pilot_data
								  .current_faction_id]
						 .mission_awards[1] == 6) +
					1;
			}
			if (g_pilot_data
				    .faction_statistics
					    [g_pilot_data.current_faction_id]
				    .mission_awards[0] != 0) {
				melee_result =
					(g_pilot_data
						 .faction_statistics
							 [g_pilot_data
								  .current_faction_id]
						 .mission_awards[0] == 6) +
					1;
			}
			XVT_LOG_DEBUG(
				"debrief.melee_result tournament=%d melee=%d",
				tournament_result, melee_result);
			if (tournament_result == 1) {
				if (g_pilot_data.current_faction_id != 0) {
					front_image_register_resource_default(
						"frontres\\debtwi.bmp",
						"background");
				} else {
					front_image_register_resource_default(
						"frontres\\debtwr.bmp",
						"background");
				}
			} else if (tournament_result == 2) {
				if (g_pilot_data.current_faction_id != 0) {
					front_image_register_resource_default(
						"frontres\\debtbi.bmp",
						"background");
				} else {
					front_image_register_resource_default(
						"frontres\\debtbr.bmp",
						"background");
				}
			} else if (melee_result == 1) {
				if (g_pilot_data.current_faction_id != 0) {
					front_image_register_resource_default(
						"frontres\\debmwi.bmp",
						"background");
				} else {
					front_image_register_resource_default(
						"frontres\\debmwr.bmp",
						"background");
				}
			} else if (melee_result == 2) {
				if (g_pilot_data.current_faction_id != 0) {
					front_image_register_resource_default(
						"frontres\\debmbi.bmp",
						"background");
				} else {
					front_image_register_resource_default(
						"frontres\\debmbr.bmp",
						"background");
				}
			} else {
				if (g_pilot_data.current_faction_id != 0) {
					front_image_register_resource_default(
						"frontres\\debmli.bmp",
						"background");
				} else {
					front_image_register_resource_default(
						"frontres\\debmlr.bmp",
						"background");
				}
			}
		} else {
			if (g_pilot_data.teams[1].is_mission_completed !=
			    g_pilot_data.teams[0].is_mission_completed) {
				int player_flight_group =
					g_pilot_data
						.network_players
							[local_network_player_index]
						.flight_group_id;

				if (g_pilot_data.teams[0]
					    .is_mission_completed != 1) {
					if (g_frontend_mission
						    .flight_groups
							    [player_flight_group]
						    .team != 0) {
						if (g_pilot_data
							    .mission_sequence_active !=
						    0) {
							front_image_register_resource_default(
								"frontres\\debbwr.bmp",
								"background");
						} else {
							front_image_register_resource_default(
								"frontres\\debcwr.bmp",
								"background");
						}
					} else if (
						g_pilot_data
							.mission_sequence_active !=
						0) {
						front_image_register_resource_default(
							"frontres\\debbli.bmp",
							"background");
					} else {
						front_image_register_resource_default(
							"frontres\\debcli.bmp",
							"background");
					}
				} else if (g_frontend_mission
						   .flight_groups
							   [player_flight_group]
						   .team == 0) {
					if (g_pilot_data
						    .mission_sequence_active !=
					    0) {
						front_image_register_resource_default(
							"frontres\\debbwi.bmp",
							"background");
					} else {
						front_image_register_resource_default(
							"frontres\\debcwi.bmp",
							"background");
					}
				} else if (g_pilot_data
						   .mission_sequence_active !=
					   0) {
					front_image_register_resource_default(
						"frontres\\debblr.bmp",
						"background");
				} else {
					front_image_register_resource_default(
						"frontres\\debclr.bmp",
						"background");
				}
			} else {
				if (g_frontend_mission_session_mode !=
				    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
					int player_flight_group =
						g_pilot_data
							.network_players
								[local_network_player_index]
							.flight_group_id;

					if (g_frontend_mission
						    .flight_groups
							    [player_flight_group]
						    .team == 0) {
						if (g_pilot_data
							    .mission_sequence_active !=
						    0) {
							front_image_register_resource_default(
								"frontres\\debbti.bmp",
								"background");
						} else {
							front_image_register_resource_default(
								"frontres\\debcti.bmp",
								"background");
						}
					} else if (
						g_pilot_data
							.mission_sequence_active !=
						0) {
						front_image_register_resource_default(
							"frontres\\debbtr.bmp",
							"background");
					} else {
						front_image_register_resource_default(
							"frontres\\debctr.bmp",
							"background");
					}
				} else if (
					g_pilot_data.teams[0]
							.is_mission_completed !=
						0 ||
					g_pilot_data.teams[1]
							.is_mission_completed !=
						0) {
					int player_flight_group =
						g_pilot_data
							.network_players
								[local_network_player_index]
							.flight_group_id;

					if (g_frontend_mission
						    .flight_groups
							    [player_flight_group]
						    .team == 0) {
						if (g_pilot_data
							    .mission_sequence_active !=
						    0) {
							front_image_register_resource_default(
								"frontres\\debbti.bmp",
								"background");
						} else {
							front_image_register_resource_default(
								"frontres\\debcti.bmp",
								"background");
						}
					} else if (
						g_pilot_data
							.mission_sequence_active !=
						0) {
						front_image_register_resource_default(
							"frontres\\debbtr.bmp",
							"background");
					} else {
						front_image_register_resource_default(
							"frontres\\debctr.bmp",
							"background");
					}
				} else if (
					g_frontend_mission
						.flight_groups
							[g_pilot_data
								 .network_players
									 [local_network_player_index]
								 .flight_group_id]
						.team != 0) {
					if (g_pilot_data
						    .mission_sequence_active ==
					    0) {
						front_image_register_resource_default(
							"frontres\\debclr.bmp",
							"background");
					} else {
						front_image_register_resource_default(
							"frontres\\debbtr.bmp",
							"background");
					}
				} else if (g_pilot_data
						   .mission_sequence_active !=
					   0) {
					front_image_register_resource_default(
						"frontres\\debbti.bmp",
						"background");
				} else {
					front_image_register_resource_default(
						"frontres\\debcli.bmp",
						"background");
				}
			}
		}

		if (g_frontend_mission_session_mode !=
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
			mission_setup_send_lobby_state(0);
		}
		frontend_display_lock_offscreen_surface();
		front_image_draw_sprite_opaque("background", 0, 0);
		front_image_draw_sprite("frame", 0, 0);
		front_image_draw_sprite("alloff", 0, 0);
		front_image_draw_sprite_translucent("regoverlay", 0, 0);
		if (g_frontend_mission_session_mode !=
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
			front_image_draw_sprite_translucent("chatbox", 0, 0);
		}
		frontend_display_unlock_offscreen_surface(1);
		frontend_text_start_text_fade_in(20);
		XVT_LOG_INFO(
			"debrief.opened directory=%d mission=%d sequence=%d mode=%d page=%d disconnected=%d",
			(int)g_pilot_data.mission_directory_id,
			(int)g_pilot_data.mission_description_ids
				[g_pilot_data.mission_directory_id],
			g_pilot_data.mission_sequence_active,
			(int)g_frontend_mission_session_mode, g_debrief_tab,
			g_debrief_disconnected_from_net_game);
		XVT_LOG_DEBUG(
			"debrief.outcome team=%d completed=\"%d,%d\" campaign=%d awards=\"%d,%d,%d,%d\" promotion=%d rating=%d score=%d",
			g_pilot_data.team,
			g_pilot_data.teams[0].is_mission_completed,
			g_pilot_data.teams[1].is_mission_completed,
			g_pilot_data.mission_directory_id ==
						MISSION_DIRECTORY_TRAINING_EXERCISES &&
					g_pilot_data.mission_sequence_active ==
						1
				? (int)g_pilot_data.campaign_sequence_state
					  .last_mission_completed
				: -1,
			g_pilot_data
				.faction_statistics[g_pilot_data
							    .current_faction_id]
				.mission_awards[0],
			g_pilot_data
				.faction_statistics[g_pilot_data
							    .current_faction_id]
				.mission_awards[1],
			g_pilot_data
				.faction_statistics[g_pilot_data
							    .current_faction_id]
				.mission_awards[2],
			g_pilot_data
				.faction_statistics[g_pilot_data
							    .current_faction_id]
				.mission_awards[3],
			(int)g_pilot_data.promotion_delta,
			(int)g_pilot_data.rating, g_pilot_data.mission_score);
	}

	if (g_debrief_disconnected_from_net_game == 0 ||
	    frame_counter >= NETWORK_MESSAGE_FRAME_COUNT) {
		int network_event;
		int title_index;

		frontend_draw_rect_assign(&rect, 158, 52, 491, 68);
		frontend_display_get_screen_clip_rect(&saved_clip_rect);
		frontend_display_set_screen_clip_rect640x480(&rect);
		sprintf(g_frontend_scratch_buffer, "%c%s", TEXT_CODE_LABEL,
			g_mission_list[g_selected_mission_list_index]
				.description);
		title_index = (int)strlen(g_frontend_scratch_buffer) - 1;
		while (title_index != 0 &&
		       g_frontend_scratch_buffer[title_index] != '(') {
			--title_index;
		}
		if (title_index != 0) {
			g_frontend_scratch_buffer[title_index] = 0;
		}
		frontend_text_draw_centered(12, g_frontend_scratch_buffer,
					    &rect, WHITE_COLOR);
		frontend_display_set_screen_clip_rect640x480(&saved_clip_rect);

		if (g_frontend_mission_session_mode !=
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
			network_event = frontend_net_process_network_packets();
			if (network_event == NET_PACKET_HOST_CANCELLED) {
				XVT_LOG_INFO(
					"debrief.left to=\"concourse\" by=\"host_cancelled\"");
				net_shutdown_direct_play_session();
				frontend_screen_set_callbacks(concourse_update,
							      concourse_exit);
				return 0;
			} else if (network_event == NET_PACKET_STATE) {
				mission_setup_prune_flight_assignments();
			} else if (network_event ==
				   NET_PACKET_RETURN_TO_SETUP) {
				XVT_LOG_DEBUG(
					"debrief.packet_ignored kind=\"return_to_setup\"");
				return 0;
			} else if (network_event ==
				   NET_PACKET_NEXT_TOURNAMENT_MISSION) {
				int local_player_id;

				++g_pilot_data.melee_tournament_sequence_state
					  .current_mission_index;
				local_player_id = net_get_local_player_id();
				g_pilot_data.launch_session_marker = 1;
				g_pilot_data.local_player_id = local_player_id;
				g_pilot_data.is_host = net_is_host();
				g_pilot_data.num_human_players_last_mission =
					net_count_ready_players();
				g_pilot_data.session_mode =
					g_frontend_mission_session_mode;
				XVT_LOG_INFO(
					"debrief.next_mission kind=\"tournament\" index=%d by=\"host\" players=%u",
					g_pilot_data
						.melee_tournament_sequence_state
						.current_mission_index,
					g_pilot_data
						.num_human_players_last_mission);
				frontend_screen_set_callbacks(
					mission_setup_enter_next_mission,

					xvt_frontend_cleanup_next_mission);
				return 0;
			} else if (network_event ==
				   NET_PACKET_NEXT_BATTLE_MISSION) {
				int local_player_id;

				++g_pilot_data.battle_sequence_state
					  .current_mission_index;
				local_player_id = net_get_local_player_id();
				g_pilot_data.launch_session_marker = 1;
				g_pilot_data.local_player_id = local_player_id;
				g_pilot_data.is_host = net_is_host();
				g_pilot_data.num_human_players_last_mission =
					net_count_ready_players();
				g_pilot_data.session_mode =
					g_frontend_mission_session_mode;
				XVT_LOG_INFO(
					"debrief.next_mission kind=\"battle\" index=%d by=\"host\" players=%u",
					(int)g_pilot_data.battle_sequence_state
						.current_mission_index,
					g_pilot_data
						.num_human_players_last_mission);
				frontend_screen_set_callbacks(
					mission_setup_enter_next_mission,

					xvt_frontend_cleanup_next_mission);
				return 0;
			} else if (network_event ==
				   NET_PACKET_NEXT_CAMPAIGN_MISSION) {
				int local_player_id;

				++g_pilot_data.campaign_sequence_state
					  .current_mission_index;
				local_player_id = net_get_local_player_id();
				g_pilot_data.launch_session_marker = 1;
				g_pilot_data.local_player_id = local_player_id;
				g_pilot_data.is_host = net_is_host();
				g_pilot_data.num_human_players_last_mission =
					net_count_ready_players();
				g_mission_setup_debrief_transition =
					MISSION_SETUP_DEBRIEF_TRANSITION_ADVANCE_MISSION_DIRECTORY;
				g_frontend_quick_start_launch_flag = 0;
				g_pilot_data.session_mode =
					g_frontend_mission_session_mode;
				XVT_LOG_INFO(
					"debrief.next_mission kind=\"campaign\" index=%d by=\"host\" players=%u",
					(int)g_pilot_data
						.campaign_sequence_state
						.current_mission_index,
					g_pilot_data
						.num_human_players_last_mission);
				frontend_screen_set_callbacks(
					mission_setup_enter_next_mission,

					xvt_frontend_cleanup_next_mission);
			} else if (network_event ==
				   NET_PACKET_REPLAY_CURRENT_MISSION) {
				int local_player_id = net_get_local_player_id();

				g_pilot_data.launch_session_marker = 1;
				g_pilot_data.local_player_id = local_player_id;
				g_pilot_data.is_host = net_is_host();
				g_pilot_data.num_human_players_last_mission =
					net_count_ready_players();
				g_pilot_data.session_mode =
					g_frontend_mission_session_mode;
				XVT_LOG_INFO(
					"debrief.fly_again kind=\"battle\" by=\"host\"");
				frontend_screen_set_callbacks(
					mission_setup_enter_current_mission,

					xvt_frontend_cleanup_current_mission);
				return 0;
			} else if (network_event ==
				   NET_PACKET_REPLAY_CAMPAIGN_MISSION) {
				g_pilot_data.local_player_id =
					net_get_local_player_id();
				g_pilot_data.launch_session_marker = 1;
				g_pilot_data.is_host = net_is_host();
				g_pilot_data.num_human_players_last_mission =
					net_count_ready_players();
				g_pilot_data.session_mode =
					g_frontend_mission_session_mode;
				g_mission_setup_debrief_transition =
					MISSION_SETUP_DEBRIEF_TRANSITION_ENTER_CURRENT_MISSION;
				g_frontend_skip_screen_entry_setup = 1;
				XVT_LOG_INFO(
					"debrief.fly_again kind=\"campaign\" by=\"host\"");
				frontend_screen_set_callbacks(
					mission_setup_enter_current_mission,

					xvt_frontend_cleanup_current_mission);
			} else if (network_event == NET_PACKET_REPLAY_MISSION) {
				memset(g_pilot_data.kills_full_on_player, 0,
				       sizeof(g_pilot_data
						      .kills_full_on_player));
				memset(g_pilot_data.kills_shared_on_player, 0,
				       sizeof(g_pilot_data
						      .kills_shared_on_player));
				memset(g_pilot_data.kills_full_on_flight_group,
				       0,
				       sizeof(g_pilot_data
						      .kills_full_on_flight_group));
				memset(g_pilot_data
					       .kills_shared_on_flight_group,
				       0,
				       sizeof(g_pilot_data
						      .kills_shared_on_flight_group));
				memset(g_pilot_data.kills_full_from_player, 0,
				       sizeof(g_pilot_data
						      .kills_full_from_player));
				memset(g_pilot_data.kills_shared_from_player, 0,
				       sizeof(g_pilot_data
						      .kills_shared_from_player));
				memset(g_pilot_data
					       .kills_full_from_flight_group,
				       0,
				       sizeof(g_pilot_data
						      .kills_full_from_flight_group));
				memset(g_pilot_data
					       .kills_shared_from_flight_group,
				       0,
				       sizeof(g_pilot_data
						      .kills_shared_from_flight_group));
				memset(&g_pilot_data.last_mission_stats, 0,
				       sizeof(g_pilot_data.last_mission_stats));
				memset(g_pilot_data.teams, 0,
				       sizeof(g_pilot_data.teams));
				for (local_network_player_index = 0;
				     local_network_player_index < PLAYER_COUNT;
				     ++local_network_player_index) {
					struct pilot_network_player *network_player =
						&g_pilot_data.network_players
							 [local_network_player_index];

					network_player->total_score = 0;
					network_player->kills = 0;
					network_player->kills_shared = 0;
					network_player->craft_inspected = 0;
					network_player->kills_assist = 0;
					network_player->total_losses = 0;
					network_player->has_left = 0;
				}
				XVT_LOG_INFO(
					"debrief.fly_again kind=\"mission\" by=\"host\"");
				mission_setup_prune_disconnected_players();
				mission_setup_prune_team_assignments();
				frontend_screen_set_callbacks(
					flight_loading_update_ready_screen,
					NULL);
				return 0;
			} else if (network_event ==
				   NET_PACKET_SESSION_CANCELLED) {
				XVT_LOG_DEBUG(
					"debrief.packet_ignored kind=\"session_cancelled\"");
				g_debrief_session_cancel_or_teams_ready_received =
					1;
			} else if (network_event ==
				   NET_PACKET_TEAM_ASSIGNMENTS_READY) {
				XVT_LOG_DEBUG(
					"debrief.packet_ignored kind=\"team_assignments\"");
				g_debrief_session_cancel_or_teams_ready_received =
					1;
			} else if (network_event == NET_PACKET_PLAYER_READY) {
				for (local_network_player_index = 0;
				     local_network_player_index < PLAYER_COUNT;
				     ++local_network_player_index) {
					if (g_mp_roster
						    [local_network_player_index]
							    .player_id ==
					    g_frontend_net_packet_sender_player_id) {
						g_mp_roster_ready_flags
							[local_network_player_index] =
								0;
						XVT_LOG_DEBUG(
							"debrief.ready_cleared player=%u index=%d",
							(unsigned)
								g_frontend_net_packet_sender_player_id,
							local_network_player_index);
						break;
					}
				}
			} else if (network_event ==
				   NET_PACKET_RETURN_TO_MISSION_SELECTION) {
				XVT_LOG_INFO(
					"debrief.left to=\"mission_setup\" by=\"host\"");
				g_frontend_skip_screen_entry_setup = 0;
				g_frontend_quick_start_launch_flag = 0;
				g_frontend_game_session_in_progress = 0;
				g_mission_setup_roster_authoritative = 0;
				mp_roster_compact_active_entries();
				frontend_screen_set_callbacks(
					mission_setup_update,
					mission_setup_exit);
				return 0;
			}
			frontend_net_update_and_draw_chat_panel(frame_counter);
		}

		if (g_debrief_tab == 0) {
			mission_debrief_draw_mission_overview_page(
				frame_counter);
		} else if (g_debrief_tab == 1) {
			mission_debrief_draw_player_statistics_page();
		} else if (g_debrief_tab == 2 &&
			   g_pilot_data.mission_sequence_active == 1) {
			if (g_pilot_data.mission_directory_id ==
			    MISSION_DIRECTORY_TRAINING_EXERCISES) {
				mission_debrief_draw_narrative_text_page();
			} else if (g_pilot_data.mission_directory_id ==
				   MISSION_DIRECTORY_MELEES) {
				mission_debrief_draw_tournament_summary_page(
					frame_counter);
			} else if (g_pilot_data.mission_directory_id ==
				   MISSION_DIRECTORY_COMBAT_ENGAGEMENTS) {
				mission_debrief_draw_battle_summary_page();
			}
		}

		frontend_draw_rect_assign(&rect, 200, 452, 436, 464);
		if (g_pilot_data.name[0] != 0) {
			sprintf(g_frontend_scratch_buffer, "%c%s %c%s",
				TEXT_CODE_RATING, g_pilot_data.rating_name, 1,
				g_pilot_data.name);
			frontend_text_draw_centered(12,
						    g_frontend_scratch_buffer,
						    &rect, g_color_yellow);
			if (g_pilot_data.current_faction_id != 0) {
				sprintf(g_frontend_scratch_buffer, "imptiny%d",
					(frame_counter % 32) >> 1);
			} else {
				sprintf(g_frontend_scratch_buffer, "rebtiny%d",
					(frame_counter % 32) >> 1);
			}
			front_image_draw_sprite(g_frontend_scratch_buffer, 204,
						453);
			front_image_draw_sprite(g_frontend_scratch_buffer, 420,
						453);
		}
		mission_debrief_draw_tab_bar();
		if (frontend_handle_common_screen_controls(4) == 1) {
			XVT_LOG_INFO("debrief.left to=\"quit\" by=\"player\"");
			return 1;
		}
		if (xvt_dialog_is_active()) {
			return 0;
		}

		frontend_draw_rect_assign(&rect, 85, 447, 176, 471);
		if (g_game_config.help_on != 0) {
			frontend_button_enable_overlay_text();
		}
		if (g_pilot_data.mission_sequence_active == 0) {
			if (net_is_host() == 0 &&
			    g_frontend_mission_session_mode !=
				    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
				int disconnect_accepted;

				if (g_debrief_disconnected_from_net_game != 0) {
					frontend_button_set_overlay_text(
						frontend_string_get(
							FRONTSTR_206_DONE));
					disconnect_accepted =
						frontend_button_handle_sprite_button(
							&rect, "leaveup",
							"leavedown",
							frontend_string_get(
								FRONTSTR_206_DONE),
							12, 0, 8,
							"buttonsound");
				} else {
					frontend_button_set_overlay_text(
						frontend_string_get(
							FRONTSTR_701_DISCONNECT));
					if (frontend_button_handle_sprite_button(
						    &rect, "leaveup",
						    "leavedown",
						    frontend_string_get(
							    FRONTSTR_702_DISCONNECT_FROM_GAME_SESSION),
						    12, 0, 8,
						    "buttonsound") == 0) {
						disconnect_accepted = 0;
					} else {
						disconnect_accepted = frontend_dialog_show_confirm_dialog(
							frontend_string_get(
								FRONTSTR_555_YOU_ARE_CURRENTLY_IN_A_GAME_SESSION),
							frontend_string_get(
								FRONTSTR_556_ARE_YOU_SURE_YOU_WANT_TO_QUIT),
							frontend_string_get(
								FRONTSTR_557_SPACE_TRANSLATION_PLACEHOLDER),
							frontend_string_get(
								FRONTSTR_523_OKAY),
							frontend_string_get(
								FRONTSTR_019_CANCEL));
						XVT_LOG_DEBUG(
							"mission.setup_confirm_asked screen=\"debrief\" action=\"leave\"");
						return xvt_dialog_continue_with(
							xvt_mission_dialogs_resume,
							XVT_MISSION_DEBRIEF_CLIENT_LEAVE);
					}
				}
				if (disconnect_accepted != 0) {
					XVT_LOG_INFO(
						"debrief.left to=\"concourse\" by=\"%s\"",
						g_debrief_disconnected_from_net_game !=
								0
							? "done"
							: "confirmed");
					g_frontend_net_packet_scratch
						.packet_type =
						NET_PACKET_PLAYER_LEFT;
					net_send_packet_and_flush(
						net_get_host_player_id(),
						&g_frontend_net_packet_scratch,
						sizeof(g_frontend_net_packet_scratch
							       .packet_type));
					net_shutdown_direct_play_session();
					frontend_button_disable_overlay_text();
					frontend_screen_set_callbacks(
						concourse_update,
						concourse_exit);
				}
			} else {
				frontend_button_set_overlay_text(
					frontend_string_get(
						FRONTSTR_700_NEW_MISSION));
				if (frontend_button_handle_sprite_button(
					    &rect, "leaveup", "leavedown",
					    frontend_string_get(
						    FRONTSTR_260_RETURN_TO_SELECT_MISSION),
					    12, 0, 8, "buttonsound") != 0) {
					if (g_frontend_mission_session_mode !=
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
						g_frontend_net_packet_scratch
							.packet_type =
							NET_PACKET_RETURN_TO_MISSION_SELECTION;
						net_send_packet_and_flush(
							0,
							&g_frontend_net_packet_scratch,
							sizeof(g_frontend_net_packet_scratch
								       .packet_type));
						XVT_LOG_DEBUG(
							"debrief.host_asked request=\"new_mission\" seed=0");
					} else {
						XVT_LOG_INFO(
							"debrief.left to=\"mission_setup\" by=\"solo\"");
						frontend_button_disable_overlay_text();
						g_frontend_skip_screen_entry_setup =
							0;
						g_frontend_quick_start_launch_flag =
							0;
						g_frontend_game_session_in_progress =
							0;
						g_mission_setup_roster_authoritative =
							0;
						frontend_screen_set_callbacks(
							mission_setup_update,
							mission_setup_exit);
					}
				}
			}
		} else if (net_is_host() == 0 &&
			   g_frontend_mission_session_mode !=
				   FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
			int disconnect_accepted;
			const char *tooltip_text;

			if (g_debrief_disconnected_from_net_game != 0) {
				frontend_button_set_overlay_text(
					frontend_string_get(FRONTSTR_206_DONE));
				tooltip_text =
					frontend_string_get(FRONTSTR_206_DONE);
			} else {
				frontend_button_set_overlay_text(
					frontend_string_get(
						FRONTSTR_701_DISCONNECT));
				tooltip_text = frontend_string_get(
					FRONTSTR_702_DISCONNECT_FROM_GAME_SESSION);
			}
			disconnect_accepted =
				frontend_button_handle_sprite_button(
					&rect, "leaveup", "leavedown",
					tooltip_text, 12, 0, 8, "buttonsound");
			if (disconnect_accepted != 0) {
				if (g_debrief_disconnected_from_net_game == 0) {
					disconnect_accepted = frontend_dialog_show_confirm_dialog(
						frontend_string_get(
							FRONTSTR_555_YOU_ARE_CURRENTLY_IN_A_GAME_SESSION),
						frontend_string_get(
							FRONTSTR_556_ARE_YOU_SURE_YOU_WANT_TO_QUIT),
						frontend_string_get(
							FRONTSTR_557_SPACE_TRANSLATION_PLACEHOLDER),
						frontend_string_get(
							FRONTSTR_523_OKAY),
						frontend_string_get(
							FRONTSTR_019_CANCEL));
					XVT_LOG_DEBUG(
						"mission.setup_confirm_asked screen=\"debrief\" action=\"leave\"");
					return xvt_dialog_continue_with(
						xvt_mission_dialogs_resume,
						XVT_MISSION_DEBRIEF_CLIENT_LEAVE);
				}
				if (disconnect_accepted != 0) {
					XVT_LOG_INFO(
						"debrief.left to=\"concourse\" by=\"%s\"",
						g_debrief_disconnected_from_net_game !=
								0
							? "done"
							: "confirmed");
					g_frontend_net_packet_scratch
						.packet_type =
						NET_PACKET_PLAYER_LEFT;
					net_send_packet_and_flush(
						net_get_host_player_id(),
						&g_frontend_net_packet_scratch,
						sizeof(g_frontend_net_packet_scratch
							       .packet_type));
					net_shutdown_direct_play_session();
					frontend_button_disable_overlay_text();
					frontend_screen_set_callbacks(
						concourse_update,
						concourse_exit);
				}
			}
		} else if (g_pilot_data.mission_directory_id ==
			   MISSION_DIRECTORY_MELEES) {
			if (g_pilot_data.melee_tournament_sequence_state
					    .mission_count -
				    g_pilot_data.melee_tournament_sequence_state
					    .current_mission_index ==
			    1) {
				frontend_button_set_overlay_text(
					frontend_string_get(
						FRONTSTR_700_NEW_MISSION));
				if (frontend_button_handle_sprite_button(
					    &rect, "leaveup", "leavedown",
					    frontend_string_get(
						    FRONTSTR_260_RETURN_TO_SELECT_MISSION),
					    12, 0, 8, "buttonsound") != 0) {
					if (g_frontend_mission_session_mode !=
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
						g_frontend_net_packet_scratch
							.packet_type =
							NET_PACKET_RETURN_TO_MISSION_SELECTION;
						net_send_packet_and_flush(
							0,
							&g_frontend_net_packet_scratch,
							sizeof(g_frontend_net_packet_scratch
								       .packet_type));
						XVT_LOG_DEBUG(
							"debrief.host_asked request=\"new_mission\" seed=0");
					} else {
						XVT_LOG_INFO(
							"debrief.left to=\"mission_setup\" by=\"solo\"");
						frontend_button_disable_overlay_text();
						g_frontend_skip_screen_entry_setup =
							0;
						g_frontend_quick_start_launch_flag =
							0;
						g_frontend_game_session_in_progress =
							0;
						g_mission_setup_roster_authoritative =
							0;
						frontend_screen_set_callbacks(
							mission_setup_update,
							mission_setup_exit);
					}
				}
			} else {
				show_sequence_continue_button = 1;
				frontend_button_set_overlay_text(
					frontend_string_get(
						FRONTSTR_216_ABORT));
				if (frontend_button_handle_sprite_button(
					    &rect, "leaveup", "leavedown",
					    frontend_string_get(
						    FRONTSTR_391_ABORT_TOURNAMENT),
					    12, 0, 8, "buttonsound") != 0) {
					if (g_frontend_mission_session_mode !=
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {

						{
							frontend_dialog_show_confirm_dialog(
								frontend_string_get(
									FRONTSTR_558_YOU_ARE_CURRENTLY_HOSTING_A_GAME_SESSION),
								frontend_string_get(
									FRONTSTR_559_IF_YOU_QUIT_THE_GAME_WILL_BE_ABORTED),
								frontend_string_get(
									FRONTSTR_560_ARE_YOU_SURE_YOU_WANT_TO_QUIT),
								frontend_string_get(
									FRONTSTR_523_OKAY),
								frontend_string_get(
									FRONTSTR_019_CANCEL));
							XVT_LOG_DEBUG(
								"mission.setup_confirm_asked screen=\"debrief\" action=\"end_game\"");
							return xvt_dialog_continue_with(
								xvt_mission_dialogs_resume,
								XVT_MISSION_DEBRIEF_HOST_ABORT);
						}

					} else {
						frontend_dialog_show_confirm_dialog(
							frontend_string_get(
								FRONTSTR_678_YOU_ARE_CURRENTLY_PLAYING_A_TOURNAMENT),
							frontend_string_get(
								FRONTSTR_679_ARE_YOU_SURE_YOU_WANT_TO),
							frontend_string_get(
								FRONTSTR_680_TERMINATE_THIS_TOURNAMENT),
							frontend_string_get(
								FRONTSTR_523_OKAY),
							frontend_string_get(
								FRONTSTR_019_CANCEL));
						XVT_LOG_DEBUG(
							"mission.setup_confirm_asked screen=\"debrief\" action=\"end_tournament\"");
						return xvt_dialog_continue_with(
							xvt_mission_dialogs_resume,
							XVT_MISSION_DEBRIEF_SOLO_ABORT);
					}
				}
			}
		} else if (g_pilot_data.mission_directory_id ==
			   MISSION_DIRECTORY_COMBAT_ENGAGEMENTS) {
			int mission_index;

			battle_result_counts
				[BATTLE_MISSION_RESULT_IMPERIAL_VICTORY] = 0;
			battle_result_counts
				[BATTLE_MISSION_RESULT_REBEL_VICTORY] = 0;
			battle_result_counts[BATTLE_MISSION_RESULT_DRAW] = 0;
			for (mission_index = 0;
			     mission_index <=
			     (int)g_pilot_data.battle_sequence_state
				     .current_mission_index;
			     ++mission_index) {
				++battle_result_counts
					[g_pilot_data.battle_sequence_state
						 .mission_results
							 [mission_index]];
			}
			if (g_pilot_data.battle_sequence_state
					    .victories_needed !=
				    battle_result_counts
					    [BATTLE_MISSION_RESULT_IMPERIAL_VICTORY] &&
			    g_pilot_data.battle_sequence_state
					    .victories_needed !=
				    battle_result_counts
					    [BATTLE_MISSION_RESULT_REBEL_VICTORY]) {
				show_sequence_continue_button = 1;
				frontend_button_set_overlay_text(
					frontend_string_get(
						FRONTSTR_216_ABORT));
				if (frontend_button_handle_sprite_button(
					    &rect, "leaveup", "leavedown",
					    frontend_string_get(
						    FRONTSTR_392_ABORT_BATTLE),
					    12, 0, 8, "buttonsound") != 0) {
					if (g_frontend_mission_session_mode !=
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {

						{
							frontend_dialog_show_confirm_dialog(
								frontend_string_get(
									FRONTSTR_558_YOU_ARE_CURRENTLY_HOSTING_A_GAME_SESSION),
								frontend_string_get(
									FRONTSTR_559_IF_YOU_QUIT_THE_GAME_WILL_BE_ABORTED),
								frontend_string_get(
									FRONTSTR_560_ARE_YOU_SURE_YOU_WANT_TO_QUIT),
								frontend_string_get(
									FRONTSTR_523_OKAY),
								frontend_string_get(
									FRONTSTR_019_CANCEL));
							XVT_LOG_DEBUG(
								"mission.setup_confirm_asked screen=\"debrief\" action=\"end_game\"");
							return xvt_dialog_continue_with(
								xvt_mission_dialogs_resume,
								XVT_MISSION_DEBRIEF_HOST_ABORT);
						}

					} else {
						frontend_dialog_show_confirm_dialog(
							frontend_string_get(
								FRONTSTR_681_YOU_ARE_CURRENTLY_PLAYING_A_BATTLE),
							frontend_string_get(
								FRONTSTR_682_ARE_YOU_SURE_YOU_WANT_TO),
							frontend_string_get(
								FRONTSTR_683_TERMINATE_THIS_BATTLE),
							frontend_string_get(
								FRONTSTR_523_OKAY),
							frontend_string_get(
								FRONTSTR_019_CANCEL));
						XVT_LOG_DEBUG(
							"mission.setup_confirm_asked screen=\"debrief\" action=\"end_battle\"");
						return xvt_dialog_continue_with(
							xvt_mission_dialogs_resume,
							XVT_MISSION_DEBRIEF_SOLO_ABORT_CLEAR_ROSTER);
					}
				}
			} else {
				frontend_button_set_overlay_text(
					frontend_string_get(
						FRONTSTR_700_NEW_MISSION));
				if (frontend_button_handle_sprite_button(
					    &rect, "leaveup", "leavedown",
					    frontend_string_get(
						    FRONTSTR_260_RETURN_TO_SELECT_MISSION),
					    12, 0, 8, "buttonsound") != 0) {
					if (g_frontend_mission_session_mode !=
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
						g_frontend_net_packet_scratch
							.packet_type =
							NET_PACKET_RETURN_TO_MISSION_SELECTION;
						net_send_packet_and_flush(
							0,
							&g_frontend_net_packet_scratch,
							sizeof(g_frontend_net_packet_scratch
								       .packet_type));
						XVT_LOG_DEBUG(
							"debrief.host_asked request=\"new_mission\" seed=0");
					} else {
						XVT_LOG_INFO(
							"debrief.left to=\"mission_setup\" by=\"solo\"");
						frontend_button_disable_overlay_text();
						g_frontend_skip_screen_entry_setup =
							0;
						g_frontend_quick_start_launch_flag =
							0;
						g_frontend_game_session_in_progress =
							0;
						g_mission_setup_roster_authoritative =
							0;
						frontend_screen_set_callbacks(
							mission_setup_update,
							mission_setup_exit);
					}
				}
			}
		} else {
			if (g_pilot_data.campaign_sequence_state
					    .last_mission_completed == 0 ||
			    g_pilot_data.campaign_sequence_state
						    .current_mission_index -
					    g_pilot_data.campaign_sequence_state
						    .mission_count !=
				    -1) {
				show_sequence_continue_button = 1;
				frontend_button_set_overlay_text(
					frontend_string_get(
						FRONTSTR_216_ABORT));
				if (frontend_button_handle_sprite_button(
					    &rect, "leaveup", "leavedown",
					    frontend_string_get(
						    FRONTSTR_782_ABORT_CAMPAIGN),
					    12, 0, 8, "buttonsound") != 0) {
					if (g_frontend_mission_session_mode !=
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {

						{
							frontend_dialog_show_confirm_dialog(
								frontend_string_get(
									FRONTSTR_558_YOU_ARE_CURRENTLY_HOSTING_A_GAME_SESSION),
								frontend_string_get(
									FRONTSTR_559_IF_YOU_QUIT_THE_GAME_WILL_BE_ABORTED),
								frontend_string_get(
									FRONTSTR_560_ARE_YOU_SURE_YOU_WANT_TO_QUIT),
								frontend_string_get(
									FRONTSTR_523_OKAY),
								frontend_string_get(
									FRONTSTR_019_CANCEL));
							XVT_LOG_DEBUG(
								"mission.setup_confirm_asked screen=\"debrief\" action=\"end_game\"");
							return xvt_dialog_continue_with(
								xvt_mission_dialogs_resume,
								XVT_MISSION_DEBRIEF_HOST_ABORT);
						}

					} else {
						frontend_dialog_show_confirm_dialog(
							frontend_string_get(
								FRONTSTR_779_YOU_ARE_CURRENTLY_PLAYING_A_CAMPAIGN),
							frontend_string_get(
								FRONTSTR_780_ARE_YOU_SURE_YOU_WANT_TO),
							frontend_string_get(
								FRONTSTR_781_TERMINATE_THIS_CAMPAIGN),
							frontend_string_get(
								FRONTSTR_523_OKAY),
							frontend_string_get(
								FRONTSTR_019_CANCEL));
						XVT_LOG_DEBUG(
							"mission.setup_confirm_asked screen=\"debrief\" action=\"end_campaign\"");
						return xvt_dialog_continue_with(
							xvt_mission_dialogs_resume,
							XVT_MISSION_DEBRIEF_SOLO_ABORT_CLEAR_ROSTER);
					}
				}
			} else {
				if (g_pilot_data
					    .faction_statistics
						    [g_pilot_data
							     .current_faction_id]
					    .sp_campaigns
						    [g_pilot_data.mission_description_ids
							     [MISSION_DIRECTORY_CAMPAIGNS]]
					    .is_finished == 0) {
					XVT_LOG_INFO(
						"debrief.campaign_finished mission=%d faction=%d",
						(int)g_pilot_data.mission_description_ids
							[MISSION_DIRECTORY_CAMPAIGNS],
						g_pilot_data
							.current_faction_id);
				}
				g_pilot_data
					.faction_statistics
						[g_pilot_data
							 .current_faction_id]
					.sp_campaigns
						[g_pilot_data.mission_description_ids
							 [MISSION_DIRECTORY_CAMPAIGNS]]
					.is_finished = 1;
				frontend_button_set_overlay_text(
					frontend_string_get(
						FRONTSTR_700_NEW_MISSION));
				if (frontend_button_handle_sprite_button(
					    &rect, "leaveup", "leavedown",
					    frontend_string_get(
						    FRONTSTR_260_RETURN_TO_SELECT_MISSION),
					    12, 0, 8, "buttonsound") != 0) {
					if (g_frontend_mission_session_mode !=
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
						g_frontend_net_packet_scratch
							.packet_type =
							NET_PACKET_RETURN_TO_MISSION_SELECTION;
						net_send_packet_and_flush(
							0,
							&g_frontend_net_packet_scratch,
							sizeof(g_frontend_net_packet_scratch
								       .packet_type));
						XVT_LOG_DEBUG(
							"debrief.host_asked request=\"new_mission\" seed=0");
					} else {
						XVT_LOG_INFO(
							"debrief.left to=\"mission_setup\" by=\"solo\"");
						frontend_button_disable_overlay_text();
						g_frontend_skip_screen_entry_setup =
							0;
						g_frontend_quick_start_launch_flag =
							0;
						g_frontend_game_session_in_progress =
							0;
						g_mission_setup_roster_authoritative =
							0;
						frontend_screen_set_callbacks(
							mission_setup_update,
							mission_setup_exit);
					}
				}
			}
		}

		if (show_sequence_continue_button != 0) {
			frontend_draw_rect_assign(&rect, 8, 405, 71, 470);
			if (g_frontend_mission_session_mode ==
			    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
				if (g_pilot_data.mission_directory_id ==
				    MISSION_DIRECTORY_MELEES) {
					frontend_button_set_overlay_text(
						frontend_string_get(
							FRONTSTR_666_CONTINUE));
					if (frontend_button_handle_sprite_button(
						    &rect, "flyup", "flydown",
						    frontend_string_get(
							    FRONTSTR_317_CONTINUE_TOURNAMENT),
						    12, 0, 7,
						    "flysound") != 0) {
						++g_pilot_data
							  .melee_tournament_sequence_state
							  .current_mission_index;
						g_pilot_data.local_player_id =
							net_get_local_player_id();
						g_pilot_data
							.launch_session_marker =
							1;
						g_pilot_data.is_host = 1;
						g_pilot_data
							.num_human_players_last_mission =
							1;
						g_pilot_data.session_mode =
							g_frontend_mission_session_mode;
						XVT_LOG_INFO(
							"debrief.next_mission kind=\"tournament\" index=%d by=\"solo\" players=%u",
							g_pilot_data
								.melee_tournament_sequence_state
								.current_mission_index,
							g_pilot_data
								.num_human_players_last_mission);
						frontend_button_disable_overlay_text();
						frontend_screen_set_callbacks(
							mission_setup_enter_next_mission,

							xvt_frontend_cleanup_next_mission);
					}
				} else if (
					g_pilot_data.mission_directory_id ==
					MISSION_DIRECTORY_COMBAT_ENGAGEMENTS) {
					if (g_pilot_data.battle_sequence_state.mission_results
						    [g_pilot_data
							     .battle_sequence_state
							     .current_mission_index] ==
					    BATTLE_MISSION_RESULT_DRAW) {
						frontend_button_set_overlay_text(
							frontend_string_get(
								FRONTSTR_711_REFLY));
						if (frontend_button_handle_sprite_button(
							    &rect, "flyup",
							    "flydown",
							    frontend_string_get(
								    FRONTSTR_710_REFLY_BATTLE_MISSION),
							    12, 0, 7,
							    "flysound") != 0) {
							g_pilot_data
								.local_player_id =
								net_get_local_player_id();
							g_pilot_data
								.session_mode =
								g_frontend_mission_session_mode;
							g_pilot_data
								.launch_session_marker =
								1;
							g_pilot_data.is_host =
								1;
							g_pilot_data
								.num_human_players_last_mission =
								1;
							XVT_LOG_INFO(
								"debrief.fly_again kind=\"battle\" by=\"solo\"");
							frontend_button_disable_overlay_text();
							frontend_screen_set_callbacks(
								mission_setup_enter_current_mission,

								xvt_frontend_cleanup_current_mission);
						}
					} else {
						frontend_button_set_overlay_text(
							frontend_string_get(
								FRONTSTR_666_CONTINUE));
						if (frontend_button_handle_sprite_button(
							    &rect, "flyup",
							    "flydown",
							    frontend_string_get(
								    FRONTSTR_316_CONTINUE_BATTLE),
							    12, 0, 7,
							    "flysound") != 0) {
							++g_pilot_data
								  .battle_sequence_state
								  .current_mission_index;
							g_pilot_data
								.local_player_id =
								net_get_local_player_id();
							g_pilot_data
								.session_mode =
								g_frontend_mission_session_mode;
							g_pilot_data
								.launch_session_marker =
								1;
							g_pilot_data.is_host =
								1;
							g_pilot_data
								.num_human_players_last_mission =
								1;
							XVT_LOG_INFO(
								"debrief.next_mission kind=\"battle\" index=%d by=\"solo\" players=%u",
								(int)g_pilot_data
									.battle_sequence_state
									.current_mission_index,
								g_pilot_data
									.num_human_players_last_mission);
							frontend_button_disable_overlay_text();
							g_frontend_quick_start_launch_flag =
								0;
							frontend_screen_set_callbacks(
								mission_setup_enter_next_mission,

								xvt_frontend_cleanup_next_mission);
						}
					}
				} else if (g_pilot_data.campaign_sequence_state
						   .last_mission_completed !=
					   0) {
					frontend_button_set_overlay_text(
						frontend_string_get(
							FRONTSTR_666_CONTINUE));
					if (frontend_button_handle_sprite_button(
						    &rect, "flyup", "flydown",
						    frontend_string_get(
							    FRONTSTR_784_CONTINUE_CAMPAIGN),
						    12, 0, 7,
						    "flysound") != 0) {
						++g_pilot_data
							  .campaign_sequence_state
							  .current_mission_index;
						g_pilot_data.local_player_id =
							net_get_local_player_id();
						g_pilot_data.session_mode =
							g_frontend_mission_session_mode;
						g_pilot_data
							.launch_session_marker =
							1;
						g_pilot_data.is_host = 1;
						g_pilot_data
							.num_human_players_last_mission =
							1;
						XVT_LOG_INFO(
							"debrief.next_mission kind=\"campaign\" index=%d by=\"solo\" players=%u",
							(int)g_pilot_data
								.campaign_sequence_state
								.current_mission_index,
							g_pilot_data
								.num_human_players_last_mission);
						frontend_button_disable_overlay_text();
						g_mission_setup_debrief_transition =
							MISSION_SETUP_DEBRIEF_TRANSITION_ADVANCE_MISSION_DIRECTORY;
						g_frontend_quick_start_launch_flag =
							0;
						frontend_screen_set_callbacks(
							mission_setup_enter_next_mission,

							xvt_frontend_cleanup_next_mission);
					}
				} else {
					frontend_button_set_overlay_text(
						frontend_string_get(
							FRONTSTR_711_REFLY));
					if (frontend_button_handle_sprite_button(
						    &rect, "flyup", "flydown",
						    frontend_string_get(
							    FRONTSTR_783_REFLY_CAMPAIGN_MISSION),
						    12, 0, 7,
						    "flysound") != 0) {
						g_pilot_data.local_player_id =
							net_get_local_player_id();
						g_pilot_data
							.launch_session_marker =
							1;
						g_pilot_data.is_host = 1;
						g_pilot_data
							.num_human_players_last_mission =
							1;
						g_pilot_data.session_mode =
							g_frontend_mission_session_mode;
						XVT_LOG_INFO(
							"debrief.fly_again kind=\"campaign\" by=\"solo\"");
						frontend_button_disable_overlay_text();
						g_frontend_skip_screen_entry_setup =
							1;
						g_mission_setup_debrief_transition =
							MISSION_SETUP_DEBRIEF_TRANSITION_ENTER_CURRENT_MISSION;
						frontend_screen_set_callbacks(
							mission_setup_enter_current_mission,

							xvt_frontend_cleanup_current_mission);
					}
				}
			} else if (g_pilot_data.mission_directory_id ==
				   MISSION_DIRECTORY_MELEES) {
				frontend_button_set_overlay_text(
					frontend_string_get(
						FRONTSTR_666_CONTINUE));
				if (frontend_button_handle_sprite_button(
					    &rect, "flyup", "flydown",
					    frontend_string_get(
						    FRONTSTR_317_CONTINUE_TOURNAMENT),
					    12, 0, 7, "flysound") != 0) {
					uint32_t packet_timestamp;

					g_frontend_net_packet_scratch
						.packet_type =
						NET_PACKET_NEXT_TOURNAMENT_MISSION;
					packet_timestamp = GetTickCount();
					memcpy(g_frontend_net_packet_scratch
						       .payload,
					       &packet_timestamp,
					       sizeof(packet_timestamp));
					net_send_packet_and_flush(
						0,
						&g_frontend_net_packet_scratch,
						sizeof(g_frontend_net_packet_scratch
							       .packet_type) +
							sizeof(packet_timestamp));
					XVT_LOG_DEBUG(
						"debrief.host_asked request=\"next_tournament\" seed=%u",
						(unsigned)packet_timestamp);
				}
			} else if (g_pilot_data.mission_directory_id ==
				   MISSION_DIRECTORY_COMBAT_ENGAGEMENTS) {
				if (g_pilot_data.battle_sequence_state.mission_results
					    [g_pilot_data.battle_sequence_state
						     .current_mission_index] ==
				    BATTLE_MISSION_RESULT_DRAW) {
					frontend_button_set_overlay_text(
						frontend_string_get(
							FRONTSTR_711_REFLY));
					if (frontend_button_handle_sprite_button(
						    &rect, "flyup", "flydown",
						    frontend_string_get(
							    FRONTSTR_710_REFLY_BATTLE_MISSION),
						    12, 0, 7,
						    "flysound") != 0) {
						uint32_t packet_timestamp;

						g_frontend_net_packet_scratch
							.packet_type =
							NET_PACKET_REPLAY_CURRENT_MISSION;
						packet_timestamp =
							GetTickCount();
						memcpy(g_frontend_net_packet_scratch
							       .payload,
						       &packet_timestamp,
						       sizeof(packet_timestamp));
						net_send_packet_and_flush(
							0,
							&g_frontend_net_packet_scratch,
							sizeof(g_frontend_net_packet_scratch
								       .packet_type) +
								sizeof(packet_timestamp));
						XVT_LOG_DEBUG(
							"debrief.host_asked request=\"replay_battle\" seed=%u",
							(unsigned)
								packet_timestamp);
					}
				} else {
					frontend_button_set_overlay_text(
						frontend_string_get(
							FRONTSTR_666_CONTINUE));
					if (frontend_button_handle_sprite_button(
						    &rect, "flyup", "flydown",
						    frontend_string_get(
							    FRONTSTR_316_CONTINUE_BATTLE),
						    12, 0, 7,
						    "flysound") != 0) {
						uint32_t packet_timestamp;

						g_frontend_net_packet_scratch
							.packet_type =
							NET_PACKET_NEXT_BATTLE_MISSION;
						packet_timestamp =
							GetTickCount();
						memcpy(g_frontend_net_packet_scratch
							       .payload,
						       &packet_timestamp,
						       sizeof(packet_timestamp));
						net_send_packet_and_flush(
							0,
							&g_frontend_net_packet_scratch,
							sizeof(g_frontend_net_packet_scratch
								       .packet_type) +
								sizeof(packet_timestamp));
						XVT_LOG_DEBUG(
							"debrief.host_asked request=\"next_battle\" seed=%u",
							(unsigned)
								packet_timestamp);
					}
				}
			} else if (g_pilot_data.campaign_sequence_state
					   .last_mission_completed != 0) {
				frontend_button_set_overlay_text(
					frontend_string_get(
						FRONTSTR_666_CONTINUE));
				if (frontend_button_handle_sprite_button(
					    &rect, "flyup", "flydown",
					    frontend_string_get(
						    FRONTSTR_784_CONTINUE_CAMPAIGN),
					    12, 0, 7, "flysound") != 0) {
					uint32_t packet_timestamp;

					g_frontend_net_packet_scratch
						.packet_type =
						NET_PACKET_NEXT_CAMPAIGN_MISSION;
					packet_timestamp = GetTickCount();
					memcpy(g_frontend_net_packet_scratch
						       .payload,
					       &packet_timestamp,
					       sizeof(packet_timestamp));
					net_send_packet_and_flush(
						0,
						&g_frontend_net_packet_scratch,
						sizeof(g_frontend_net_packet_scratch
							       .packet_type) +
							sizeof(packet_timestamp));
					XVT_LOG_DEBUG(
						"debrief.host_asked request=\"next_campaign\" seed=%u",
						(unsigned)packet_timestamp);
				}
			} else {
				frontend_button_set_overlay_text(
					frontend_string_get(
						FRONTSTR_711_REFLY));
				if (frontend_button_handle_sprite_button(
					    &rect, "flyup", "flydown",
					    frontend_string_get(
						    FRONTSTR_783_REFLY_CAMPAIGN_MISSION),
					    12, 0, 7, "flysound") != 0) {
					uint32_t packet_timestamp;

					g_frontend_net_packet_scratch
						.packet_type =
						NET_PACKET_REPLAY_CAMPAIGN_MISSION;
					packet_timestamp = GetTickCount();
					memcpy(g_frontend_net_packet_scratch
						       .payload,
					       &packet_timestamp,
					       sizeof(packet_timestamp));
					net_send_packet_and_flush(
						0,
						&g_frontend_net_packet_scratch,
						sizeof(g_frontend_net_packet_scratch
							       .packet_type) +
							sizeof(packet_timestamp));
					XVT_LOG_DEBUG(
						"debrief.host_asked request=\"replay_campaign\" seed=%u",
						(unsigned)packet_timestamp);
				}
			}
		} else if (g_pilot_data.mission_sequence_active == 0) {
			frontend_draw_rect_assign(&rect, 8, 405, 71, 470);
			if (net_is_host() != 0 ||
			    g_frontend_mission_session_mode ==
				    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
				frontend_button_set_overlay_text(
					frontend_string_get(
						FRONTSTR_476_FLY_AGAIN));
				if (frontend_button_handle_sprite_button(
					    &rect, "flyup", "flydown",
					    frontend_string_get(
						    FRONTSTR_476_FLY_AGAIN),
					    12, 0, 7, "flysound") != 0) {
					if (g_frontend_mission_session_mode ==
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
						g_pilot_data.is_host = 1;
						g_pilot_data
							.num_human_players_last_mission =
							1;
						g_pilot_data.session_mode =
							FRONTEND_MISSION_SESSION_SINGLEPLAYER;
						memset(g_pilot_data
							       .kills_full_on_player,
						       0,
						       sizeof(g_pilot_data
								      .kills_full_on_player));
						memset(g_pilot_data
							       .kills_shared_on_player,
						       0,
						       sizeof(g_pilot_data
								      .kills_shared_on_player));
						memset(g_pilot_data
							       .kills_full_on_flight_group,
						       0,
						       sizeof(g_pilot_data
								      .kills_full_on_flight_group));
						memset(g_pilot_data
							       .kills_shared_on_flight_group,
						       0,
						       sizeof(g_pilot_data
								      .kills_shared_on_flight_group));
						memset(g_pilot_data
							       .kills_full_from_player,
						       0,
						       sizeof(g_pilot_data
								      .kills_full_from_player));
						memset(g_pilot_data
							       .kills_shared_from_player,
						       0,
						       sizeof(g_pilot_data
								      .kills_shared_from_player));
						memset(g_pilot_data
							       .kills_full_from_flight_group,
						       0,
						       sizeof(g_pilot_data
								      .kills_full_from_flight_group));
						memset(g_pilot_data
							       .kills_shared_from_flight_group,
						       0,
						       sizeof(g_pilot_data
								      .kills_shared_from_flight_group));
						memset(&g_pilot_data
								.last_mission_stats,
						       0,
						       sizeof(g_pilot_data
								      .last_mission_stats));
						memset(g_pilot_data.teams, 0,
						       sizeof(g_pilot_data
								      .teams));
						for (local_network_player_index =
							     0;
						     local_network_player_index <
						     PLAYER_COUNT;
						     ++local_network_player_index) {
							struct pilot_network_player *network_player =
								&g_pilot_data.network_players
									 [local_network_player_index];

							network_player
								->total_score =
								0;
							network_player->kills =
								0;
							network_player
								->kills_shared =
								0;
							network_player
								->craft_inspected =
								0;
							network_player
								->kills_assist =
								0;
							network_player
								->total_losses =
								0;
							network_player
								->has_left = 0;
						}
						XVT_LOG_INFO(
							"debrief.fly_again kind=\"mission\" by=\"solo\"");
						frontend_screen_set_callbacks(
							flight_loading_update_ready_screen,
							NULL);
					} else {
						uint32_t packet_timestamp;

						g_frontend_net_packet_scratch
							.packet_type =
							NET_PACKET_REPLAY_MISSION;
						packet_timestamp =
							GetTickCount();
						memcpy(g_frontend_net_packet_scratch
							       .payload,
						       &packet_timestamp,
						       sizeof(packet_timestamp));
						net_send_packet_and_flush(
							0,
							&g_frontend_net_packet_scratch,
							sizeof(g_frontend_net_packet_scratch
								       .packet_type) +
								sizeof(packet_timestamp));
						XVT_LOG_DEBUG(
							"debrief.host_asked request=\"replay_mission\" seed=%u",
							(unsigned)
								packet_timestamp);
					}
				}
			}
		}
		frontend_button_disable_overlay_text();
	} else {
		frontend_text_stop_text_fade();
		frontend_draw_rect_assign(&rect, 84, 107, 434, 433);
		frontend_text_draw_centered(
			15,
			frontend_string_get(
				FRONTSTR_748_DISCONNECTING_FROM_NETWORK_PLEASE_WAIT),
			&rect, WHITE_COLOR);
	}

	if (g_debrief_disconnected_from_net_game != 0 &&
	    frame_counter == NETWORK_DISCONNECT_FRAME) {
		net_shutdown_direct_play_session();
		XVT_LOG_INFO("debrief.disconnected");
		sprintf(g_frontend_scratch_buffer, "%s.",
			g_pilot_data.multiplayer_game_name);
		frontend_dialog_show_confirm_dialog(
			frontend_string_get(
				FRONTSTR_733_YOU_HAVE_BEEN_DISCONNECTED_FROM),
			g_frontend_scratch_buffer, NULL, NULL, NULL);
		return xvt_dialog_continue_with(xvt_mission_dialogs_resume,
						XVT_MISSION_NOTICE);
	} else if (g_flight_net_host_abort_received != 0 &&
		   frame_counter == NETWORK_DISCONNECT_FRAME &&
		   net_is_host() == 0) {
		XVT_LOG_INFO("debrief.host_aborted");
		frontend_dialog_show_confirm_dialog(
			frontend_string_get(
				FRONTSTR_737_THE_HOST_ABORTED_THE_MISSION),
			frontend_string_get(
				FRONTSTR_738_HOWEVER_YOU_ARE_STILL_CONNECTED),
			frontend_string_get(
				FRONTSTR_739_TO_THE_CURRENT_GAME_SESSION),
			NULL, NULL);
		return xvt_dialog_continue_with(xvt_mission_dialogs_resume,
						XVT_MISSION_NOTICE);
	}
	return 0;
}

/* Draws the mission overview page: a title (the mission overview, or the
 * tournament's, battle's or campaign's progress, or done), then score, kills
 * (full and shared) and deaths for the teams in g_debrief_sorted_team_ids. Teams
 * with equal mission_score share a place, and places 1 to 3 get another text
 * color. Under each team come its network players in g_debrief_sorted_player_ids
 * order, gray and bracketed once they left, the local player in pulsing
 * colors; with g_debrief_rank_by_pilot set, each pilot's row carries the place
 * and no team row is drawn. A team of only AI pilots lists its player flight
 * groups by rating, as do a melee's or tournament's player flight groups no
 * human flies. Then the "killed" and "killed by" lists from
 * g_debrief_kills_on_combatant_ids and g_debrief_kills_from_combatant_ids, full kills
 * with shared ones in parentheses. Holding Alt, Shift and Ctrl draws the
 * "lh2" sprite. Returns 1. */
// FUNCTION: XVT 0x500650
int mission_debrief_draw_mission_overview_page(int frame_counter)
{
	enum {
		PLAYER_COUNT = 8,
		TEAM_COUNT = 10,
		FLIGHT_GROUP_COMBATANT_ID_BASE = PLAYER_COUNT,
		BATTLE_RESULT_COUNT = 3,
		IMPERIAL_TEAM_ID = 0,
		REBEL_TEAM_ID = 1,
		TEXT_FONT_SIZE = 12,
		TITLE_FONT_SIZE = 15,
		TITLE_LEFT = 84,
		TITLE_TOP = 90,
		TITLE_RIGHT = 434,
		TITLE_BOTTOM = 106,
		HEADER_Y = 114,
		FIRST_ROW_Y = 134,
		TEAM_NAME_X = 88,
		PLAYER_NAME_X = 103,
		SCORE_X = 228,
		KILLS_X = 298,
		DEATHS_X = 363,
		KILLED_NAME_RIGHT = 206,
		KILLED_VALUE_X = 208,
		KILLED_BY_NAME_X = 259,
		KILLED_BY_NAME_RIGHT = 377,
		KILLED_BY_VALUE_X = 379,
		ROW_HEIGHT = 15,
		ROW_CLIP_HEIGHT = 14,
		PULSE_PERIOD = 24,
		PULSE_PAIR_MASK = ~1,
		PULSE_PAIR_SHIFT = 1,
		TEXT_CODE_VALUE = 1,
		TEXT_CODE_PLACED = 2,
		TEXT_CODE_LABEL = 4,
		TEXT_CODE_RATING = 6,
		WHITE_COLOR = 0xFFFF,
	};

	if (keyboard_is_key_down(0x12) && keyboard_is_key_down(0x10) &&
	    keyboard_is_key_down(0x11)) {
		front_image_draw_sprite("lh2", 184, 362);
	}
	struct RECT rect;
	frontend_draw_rect_assign(&rect, TITLE_LEFT, TITLE_TOP, TITLE_RIGHT,
				  TITLE_BOTTOM);
	if (g_pilot_data.mission_sequence_active == 0) {
		frontend_text_draw_centered(
			TITLE_FONT_SIZE,
			frontend_string_get(FRONTSTR_311_MISSION_OVERVIEW),
			&rect, WHITE_COLOR);
	} else {
		if (g_pilot_data.mission_directory_id ==
		    MISSION_DIRECTORY_MELEES) {
			if (g_pilot_data.melee_tournament_sequence_state
					    .mission_count -
				    g_pilot_data.melee_tournament_sequence_state
					    .current_mission_index ==
			    1) {
				sprintf(g_frontend_scratch_buffer, "%c%s %c%s",
					TEXT_CODE_VALUE,
					frontend_string_get(
						FRONTSTR_687_TOURNAMENT_MISSION_OVERVIEW),
					TEXT_CODE_LABEL,
					frontend_string_get(FRONTSTR_206_DONE));
			} else {
				sprintf(g_frontend_scratch_buffer,
					"%c%s %c%d %s %d %s", TEXT_CODE_VALUE,
					frontend_string_get(
						FRONTSTR_687_TOURNAMENT_MISSION_OVERVIEW),
					TEXT_CODE_LABEL,
					g_pilot_data.melee_tournament_sequence_state
							.current_mission_index +
						1,
					frontend_string_get(FRONTSTR_335_OF),
					g_pilot_data
						.melee_tournament_sequence_state
						.mission_count,
					frontend_string_get(
						FRONTSTR_340_MISSIONS));
			}
		} else if (g_pilot_data.mission_directory_id ==
			   MISSION_DIRECTORY_COMBAT_ENGAGEMENTS) {
			int mission_result_counts[BATTLE_RESULT_COUNT];

			mission_result_counts
				[BATTLE_MISSION_RESULT_IMPERIAL_VICTORY] = 0;
			mission_result_counts
				[BATTLE_MISSION_RESULT_REBEL_VICTORY] = 0;
			mission_result_counts[BATTLE_MISSION_RESULT_DRAW] = 0;
			for (int mission_index = 0;
			     mission_index <=
			     (int)g_pilot_data.battle_sequence_state
				     .current_mission_index;
			     ++mission_index) {
				++mission_result_counts
					[g_pilot_data.battle_sequence_state
						 .mission_results
							 [mission_index]];
			}
			int player_team_result;
			switch (g_pilot_data.team) {
			case IMPERIAL_TEAM_ID:
				player_team_result = 0;
				break;
			case REBEL_TEAM_ID:
				player_team_result = 1;
				break;
			default:
				player_team_result = 0;
				break;
			}
			if (mission_result_counts
					    [BATTLE_MISSION_RESULT_IMPERIAL_VICTORY] ==
				    g_pilot_data.battle_sequence_state
					    .victories_needed ||
			    mission_result_counts
					    [BATTLE_MISSION_RESULT_REBEL_VICTORY] ==
				    g_pilot_data.battle_sequence_state
					    .victories_needed) {
				sprintf(g_frontend_scratch_buffer, "%c%s %c%s",
					TEXT_CODE_VALUE,
					frontend_string_get(
						FRONTSTR_689_BATTLE_MISSION_OVERVIEW),
					TEXT_CODE_LABEL,
					frontend_string_get(FRONTSTR_206_DONE));
			} else {
				sprintf(g_frontend_scratch_buffer,
					"%c%s %c%d %s %d %s", TEXT_CODE_VALUE,
					frontend_string_get(
						FRONTSTR_689_BATTLE_MISSION_OVERVIEW),
					TEXT_CODE_LABEL,
					mission_result_counts
						[player_team_result],
					frontend_string_get(FRONTSTR_335_OF),
					g_pilot_data.battle_sequence_state
						.victories_needed,
					frontend_string_get(
						FRONTSTR_690_VICTORIES_NEEDED));
			}
		} else if (g_pilot_data.campaign_sequence_state.mission_count ==
				   g_pilot_data.campaign_sequence_state
					   .current_mission_index &&
			   g_pilot_data.campaign_sequence_state
					   .last_mission_completed == 1) {
			sprintf(g_frontend_scratch_buffer, "%c%s %c%s",
				TEXT_CODE_VALUE,
				frontend_string_get(
					FRONTSTR_785_CAMPAIGN_OVERVIEW),
				TEXT_CODE_LABEL,
				frontend_string_get(FRONTSTR_206_DONE));
		} else {
			sprintf(g_frontend_scratch_buffer, "%c%s %c%s%d",
				TEXT_CODE_VALUE,
				frontend_string_get(
					FRONTSTR_785_CAMPAIGN_OVERVIEW),
				TEXT_CODE_LABEL,
				frontend_string_get(FRONTSTR_344_MISSION),
				g_pilot_data.campaign_sequence_state
						.current_mission_index +
					1);
		}
		frontend_text_draw_centered(TITLE_FONT_SIZE,
					    g_frontend_scratch_buffer, &rect,
					    WHITE_COLOR);
	}

	frontend_text_draw(TEXT_FONT_SIZE,
			   frontend_string_get(FRONTSTR_330_SCORE), SCORE_X,
			   HEADER_Y, g_color_yellow);
	frontend_text_draw(TEXT_FONT_SIZE,
			   frontend_string_get(FRONTSTR_336_KILLS), KILLS_X,
			   HEADER_Y, g_color_yellow);
	frontend_text_draw(TEXT_FONT_SIZE,
			   frontend_string_get(FRONTSTR_539_DEATHS), DEATHS_X,
			   HEADER_Y, g_color_yellow);

	int text_y = FIRST_ROW_Y;
	int place = 0;
	int previous_score =
		g_pilot_data.teams[g_debrief_sorted_team_ids[0]].mission_score;
	uint16_t placement_code;
	int text_x;
	struct RECT saved_clip_rect;
	for (int team_position = 0; team_position < TEAM_COUNT;
	     ++team_position) {

		text_x = TEAM_NAME_X;
		if (g_debrief_sorted_team_ids[team_position] == -1) {
			break;
		}
		if (g_debrief_team_has_player
			    [g_debrief_sorted_team_ids[team_position]] != 0) {
			if (g_pilot_data
				    .teams[g_debrief_sorted_team_ids
						   [team_position]]
				    .mission_score != previous_score) {
				place = team_position;
				previous_score =
					g_pilot_data
						.teams[g_debrief_sorted_team_ids
							       [team_position]]
						.mission_score;
			}
			if (place < 3) {
				placement_code = TEXT_CODE_PLACED;
			} else {
				placement_code = TEXT_CODE_VALUE;
			}
			if (g_debrief_rank_by_pilot == 0) {
				if (g_pilot_data.mission_directory_id ==
					    MISSION_DIRECTORY_MELEES ||
				    g_pilot_data.mission_directory_id ==
					    MISSION_DIRECTORY_TOURNAMENTS) {
					sprintf(g_frontend_scratch_buffer,
						"%c%d. %c%s", placement_code,
						place + 1, TEXT_CODE_VALUE,
						g_frontend_mission
							.teams[g_debrief_sorted_team_ids
								       [team_position]]
							.name);
					frontend_text_draw(
						TEXT_FONT_SIZE,
						g_frontend_scratch_buffer,
						TEAM_NAME_X, text_y,
						WHITE_COLOR);
					sprintf(g_frontend_scratch_buffer, "%d",
						g_pilot_data
							.teams[g_debrief_sorted_team_ids
								       [team_position]]
							.mission_score);
					frontend_text_draw(
						TEXT_FONT_SIZE,
						g_frontend_scratch_buffer,
						SCORE_X, text_y, WHITE_COLOR);
					sprintf(g_frontend_scratch_buffer,
						"%d (%d)",
						g_pilot_data
							.teams[g_debrief_sorted_team_ids
								       [team_position]]
							.kills,
						g_pilot_data
							.teams[g_debrief_sorted_team_ids
								       [team_position]]
							.kills_shared);
					frontend_text_draw(
						TEXT_FONT_SIZE,
						g_frontend_scratch_buffer,
						KILLS_X, text_y, WHITE_COLOR);
					sprintf(g_frontend_scratch_buffer, "%d",
						g_pilot_data
							.teams[g_debrief_sorted_team_ids
								       [team_position]]
							.losses);
					frontend_text_draw(
						TEXT_FONT_SIZE,
						g_frontend_scratch_buffer,
						DEATHS_X, text_y, WHITE_COLOR);
					text_x = PLAYER_NAME_X;
					text_y += ROW_HEIGHT;
				} else if (g_debrief_team_has_only_ai_pilots
						   [g_debrief_sorted_team_ids
							    [team_position]] !=
					   0) {
					sprintf(g_frontend_scratch_buffer,
						"%c%d. %c%s", placement_code,
						place + 1, TEXT_CODE_VALUE,
						g_frontend_mission
							.teams[g_debrief_sorted_team_ids
								       [team_position]]
							.name);
					frontend_text_draw(
						TEXT_FONT_SIZE,
						g_frontend_scratch_buffer,
						TEAM_NAME_X, text_y,
						WHITE_COLOR);
					sprintf(g_frontend_scratch_buffer, "%d",
						g_pilot_data
							.teams[g_debrief_sorted_team_ids
								       [team_position]]
							.mission_score);
					frontend_text_draw(
						TEXT_FONT_SIZE,
						g_frontend_scratch_buffer,
						SCORE_X, text_y, WHITE_COLOR);
					sprintf(g_frontend_scratch_buffer,
						"%d (%d)",
						g_pilot_data
							.teams[g_debrief_sorted_team_ids
								       [team_position]]
							.kills,
						g_pilot_data
							.teams[g_debrief_sorted_team_ids
								       [team_position]]
							.kills_shared);
					frontend_text_draw(
						TEXT_FONT_SIZE,
						g_frontend_scratch_buffer,
						KILLS_X, text_y, WHITE_COLOR);
					sprintf(g_frontend_scratch_buffer, "%d",
						g_pilot_data
							.teams[g_debrief_sorted_team_ids
								       [team_position]]
							.losses);
					frontend_text_draw(
						TEXT_FONT_SIZE,
						g_frontend_scratch_buffer,
						DEATHS_X, text_y, WHITE_COLOR);
					text_y += ROW_HEIGHT;
					text_x = PLAYER_NAME_X;
				} else {
					sprintf(g_frontend_scratch_buffer,
						"%c%d. %c%s", placement_code,
						place + 1, TEXT_CODE_VALUE,
						g_frontend_mission
							.teams[g_debrief_sorted_team_ids
								       [team_position]]
							.name);
					frontend_text_draw(
						TEXT_FONT_SIZE,
						g_frontend_scratch_buffer,
						TEAM_NAME_X, text_y,
						WHITE_COLOR);
					text_y += ROW_HEIGHT;
					text_x = PLAYER_NAME_X;
				}
			} else if (g_debrief_team_has_only_ai_pilots
					   [g_debrief_sorted_team_ids
						    [team_position]] != 0) {
				for (int flight_group_index = 0;
				     flight_group_index <
				     (int16_t)g_frontend_mission
					     .flight_group_count;
				     ++flight_group_index) {
					if (g_frontend_mission
							    .flight_groups
								    [flight_group_index]
							    .player_number !=
						    0 &&
					    g_frontend_mission
							    .flight_groups
								    [flight_group_index]
							    .team ==
						    g_debrief_sorted_team_ids
							    [team_position]) {
						sprintf(g_frontend_scratch_buffer,
							"%c%d. %c%s %c%s",
							placement_code,
							place + 1,
							TEXT_CODE_RATING,
							frontend_string_get((
								frontend_string_id)(FRONTSTR_154_DRONE +
										    g_pilot_data
											    .flight_group_rating
												    [flight_group_index])),
							TEXT_CODE_VALUE,
							g_frontend_mission
								.flight_groups
									[flight_group_index]
								.name);
						frontend_text_draw(
							TEXT_FONT_SIZE,
							g_frontend_scratch_buffer,
							text_x, text_y,
							WHITE_COLOR);
						sprintf(g_frontend_scratch_buffer,
							"%d",
							g_pilot_data
								.teams[g_debrief_sorted_team_ids
									       [team_position]]
								.mission_score);
						frontend_text_draw(
							TEXT_FONT_SIZE,
							g_frontend_scratch_buffer,
							SCORE_X, text_y,
							WHITE_COLOR);
						sprintf(g_frontend_scratch_buffer,
							"%d (%d)",
							g_pilot_data
								.teams[g_debrief_sorted_team_ids
									       [team_position]]
								.kills,
							g_pilot_data
								.teams[g_debrief_sorted_team_ids
									       [team_position]]
								.kills_shared);
						frontend_text_draw(
							TEXT_FONT_SIZE,
							g_frontend_scratch_buffer,
							KILLS_X, text_y,
							WHITE_COLOR);
						text_x = DEATHS_X;
						sprintf(g_frontend_scratch_buffer,
							"%d",
							g_pilot_data
								.teams[g_debrief_sorted_team_ids
									       [team_position]]
								.losses);
						frontend_text_draw(
							TEXT_FONT_SIZE,
							g_frontend_scratch_buffer,
							text_x, text_y,
							WHITE_COLOR);
						text_y += ROW_HEIGHT;
					}
				}
			}

			if (g_debrief_team_has_only_ai_pilots
				    [g_debrief_sorted_team_ids
					     [team_position]] == 0) {
				for (int sorted_player_index = 0;
				     sorted_player_index < PLAYER_COUNT;
				     ++sorted_player_index) {
					int player_index =
						g_debrief_sorted_player_ids
							[sorted_player_index];
					if (player_index == -1) {
						break;
					}
					if (g_frontend_mission
						    .flight_groups
							    [g_pilot_data
								     .network_players
									     [player_index]
								     .flight_group_id]
						    .team !=
					    g_debrief_sorted_team_ids
						    [team_position]) {
						continue;
					}

					frontend_draw_rect_assign(
						&rect, text_x, text_y, 225,
						text_y + ROW_CLIP_HEIGHT);
					frontend_display_get_screen_clip_rect(
						&saved_clip_rect);
					frontend_display_set_screen_clip_rect640x480(
						&rect);
					if (g_debrief_rank_by_pilot == 0) {
						if (g_pilot_data
							    .network_players
								    [player_index]
							    .has_left != 0) {
							sprintf(g_frontend_scratch_buffer,
								"[%s %s]",
								frontend_string_get((
									frontend_string_id)(FRONTSTR_154_DRONE +
											    g_pilot_data
												    .network_players
													    [player_index]
												    .rating)),
								g_pilot_data
									.network_players
										[player_index]
									.friendly_name);
						} else {
							sprintf(g_frontend_scratch_buffer,
								"%c%s %c%s",
								TEXT_CODE_RATING,
								frontend_string_get((
									frontend_string_id)(FRONTSTR_154_DRONE +
											    g_pilot_data
												    .network_players
													    [player_index]
												    .rating)),
								TEXT_CODE_VALUE,
								g_pilot_data
									.network_players
										[player_index]
									.friendly_name);
						}
					} else if (
						g_pilot_data
							.network_players
								[player_index]
							.has_left != 0) {
						sprintf(g_frontend_scratch_buffer,
							"%c%d. %c[%s %s]",
							placement_code,
							place + 1,
							TEXT_CODE_VALUE,
							frontend_string_get((
								frontend_string_id)(FRONTSTR_154_DRONE +
										    g_pilot_data
											    .network_players
												    [player_index]
											    .rating)),
							g_pilot_data
								.network_players
									[player_index]
								.friendly_name);
					} else {
						sprintf(g_frontend_scratch_buffer,
							"%c%d. %c%s %c%s",
							placement_code,
							place + 1,
							TEXT_CODE_RATING,
							frontend_string_get((
								frontend_string_id)(FRONTSTR_154_DRONE +
										    g_pilot_data
											    .network_players
												    [player_index]
											    .rating)),
							TEXT_CODE_VALUE,
							g_pilot_data
								.network_players
									[player_index]
								.friendly_name);
					}

					if (g_pilot_data
						    .network_players
							    [player_index]
						    .has_left != 0) {
						frontend_text_draw(
							TEXT_FONT_SIZE,
							g_frontend_scratch_buffer,
							text_x, text_y,
							g_color_gray);
					} else {
						int local_player_id =
							net_get_local_player_id();
						int color = g_color_yellow;
						if (local_player_id ==
							    g_pilot_data
								    .network_players
									    [player_index]
								    .direct_play_id ||
						    g_frontend_mission_session_mode ==
							    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
							frontend_text_draw(
								TEXT_FONT_SIZE,
								g_frontend_scratch_buffer,
								text_x, text_y,
								g_pulse_color_ramp
									[((frame_counter %
									   PULSE_PERIOD) &
									  PULSE_PAIR_MASK) >>
									 PULSE_PAIR_SHIFT]);
						} else {
							frontend_text_draw(
								TEXT_FONT_SIZE,
								g_frontend_scratch_buffer,
								text_x, text_y,
								color);
						}
					}
					frontend_display_set_screen_clip_rect640x480(
						&saved_clip_rect);

					if ((g_pilot_data.mission_directory_id ==
						     MISSION_DIRECTORY_MELEES ||
					     g_pilot_data.mission_directory_id ==
						     MISSION_DIRECTORY_TOURNAMENTS) &&
					    g_debrief_rank_by_pilot != 0) {
						sprintf(g_frontend_scratch_buffer,
							"%d",
							g_pilot_data
								.teams[g_debrief_sorted_team_ids
									       [team_position]]
								.mission_score);
					} else {
						sprintf(g_frontend_scratch_buffer,
							"%d",
							g_pilot_data
								.network_players
									[player_index]
								.total_score);
					}
					frontend_text_draw(
						TEXT_FONT_SIZE,
						g_frontend_scratch_buffer,
						SCORE_X, text_y, WHITE_COLOR);
					if (g_debrief_rank_by_pilot != 0) {
						sprintf(g_frontend_scratch_buffer,
							"%d (%d)",
							g_pilot_data
								.teams[g_debrief_sorted_team_ids
									       [team_position]]
								.kills,
							g_pilot_data
								.teams[g_debrief_sorted_team_ids
									       [team_position]]
								.kills_shared);
						frontend_text_draw(
							TEXT_FONT_SIZE,
							g_frontend_scratch_buffer,
							KILLS_X, text_y,
							WHITE_COLOR);
						sprintf(g_frontend_scratch_buffer,
							"%d",
							g_pilot_data
								.teams[g_debrief_sorted_team_ids
									       [team_position]]
								.losses);
					} else {
						sprintf(g_frontend_scratch_buffer,
							"%d (%d)",
							g_pilot_data
								.network_players
									[player_index]
								.kills,
							g_pilot_data
								.network_players
									[player_index]
								.kills_shared);
						frontend_text_draw(
							TEXT_FONT_SIZE,
							g_frontend_scratch_buffer,
							KILLS_X, text_y,
							WHITE_COLOR);
						sprintf(g_frontend_scratch_buffer,
							"%d",
							g_pilot_data
								.network_players
									[player_index]
								.total_losses);
					}
					frontend_text_draw(
						TEXT_FONT_SIZE,
						g_frontend_scratch_buffer,
						DEATHS_X, text_y, WHITE_COLOR);
					text_y += ROW_HEIGHT;
					text_x = g_debrief_rank_by_pilot == 0
							 ? PLAYER_NAME_X
							 : TEAM_NAME_X;
				}
			}

			if ((g_pilot_data.mission_directory_id ==
				     MISSION_DIRECTORY_MELEES ||
			     g_pilot_data.mission_directory_id ==
				     MISSION_DIRECTORY_TOURNAMENTS) &&
			    g_debrief_rank_by_pilot == 0) {
				for (int flight_group_index = 0;
				     flight_group_index <
				     (int16_t)g_frontend_mission
					     .flight_group_count;
				     ++flight_group_index) {
					if (g_frontend_mission
							    .flight_groups
								    [flight_group_index]
							    .player_number ==
						    0 ||
					    g_frontend_mission
							    .flight_groups
								    [flight_group_index]
							    .team !=
						    g_debrief_sorted_team_ids
							    [team_position]) {
						continue;
					}
					int player_index;
					for (player_index = 0;
					     player_index < PLAYER_COUNT;
					     ++player_index) {
						if (g_pilot_data.network_players
								    [player_index]
									    .direct_play_id !=
							    0 &&
						    g_pilot_data.network_players
								    [player_index]
									    .flight_group_id ==
							    flight_group_index) {
							break;
						}
					}
					if (player_index == PLAYER_COUNT) {
						sprintf(g_frontend_scratch_buffer,
							"%c%s %c%s",
							TEXT_CODE_RATING,
							frontend_string_get((
								frontend_string_id)(FRONTSTR_154_DRONE +
										    g_pilot_data
											    .flight_group_rating
												    [flight_group_index])),
							TEXT_CODE_VALUE,
							g_frontend_mission
								.flight_groups
									[flight_group_index]
								.name);
						frontend_text_draw(
							TEXT_FONT_SIZE,
							g_frontend_scratch_buffer,
							text_x, text_y,
							WHITE_COLOR);
						text_y += ROW_HEIGHT;
					}
				}
			}
		}
	}

	{
		int killed_header_y = text_y + ROW_HEIGHT;
		frontend_text_draw(TEXT_FONT_SIZE,
				   frontend_string_get(FRONTSTR_542_KILLED),
				   text_x, killed_header_y, g_color_yellow);
		int killed_row_y = killed_header_y + ROW_HEIGHT;
		int kill_index;
		int combatant_id;
		for (kill_index = 0; kill_index < PLAYER_COUNT; ++kill_index) {

			combatant_id =
				g_debrief_kills_on_combatant_ids[kill_index];
			if (combatant_id == -1) {
				break;
			}
			if (combatant_id < FLIGHT_GROUP_COMBATANT_ID_BASE) {
				if (net_get_local_player_id() ==
					    g_pilot_data
						    .network_players
							    [combatant_id]
						    .direct_play_id ||
				    (g_pilot_data.kills_full_on_player
						     [combatant_id] == 0 &&
				     g_pilot_data.kills_shared_on_player
						     [combatant_id] == 0)) {
					continue;
				}
				frontend_draw_rect_assign(
					&rect, TEAM_NAME_X, killed_row_y,
					KILLED_NAME_RIGHT,
					killed_row_y + ROW_CLIP_HEIGHT);
				frontend_display_get_screen_clip_rect(
					&saved_clip_rect);
				frontend_display_set_screen_clip_rect640x480(
					&rect);
				if (g_pilot_data.network_players[combatant_id]
					    .has_left != 0) {
					sprintf(g_frontend_scratch_buffer,
						"[%s %s]",
						frontend_string_get((
							frontend_string_id)(FRONTSTR_154_DRONE +
									    g_pilot_data
										    .network_players
											    [combatant_id]
										    .rating)),
						g_pilot_data
							.network_players
								[combatant_id]
							.friendly_name);
					frontend_text_draw(
						TEXT_FONT_SIZE,
						g_frontend_scratch_buffer,
						TEAM_NAME_X, killed_row_y,
						g_color_gray);
				} else {
					sprintf(g_frontend_scratch_buffer,
						"%c%s %c%s", TEXT_CODE_RATING,
						frontend_string_get((
							frontend_string_id)(FRONTSTR_154_DRONE +
									    g_pilot_data
										    .network_players
											    [combatant_id]
										    .rating)),
						TEXT_CODE_LABEL,
						g_pilot_data
							.network_players
								[combatant_id]
							.friendly_name);
					frontend_text_draw(
						TEXT_FONT_SIZE,
						g_frontend_scratch_buffer,
						TEAM_NAME_X, killed_row_y,
						WHITE_COLOR);
				}
				frontend_display_set_screen_clip_rect640x480(
					&saved_clip_rect);
				sprintf(g_frontend_scratch_buffer, "%d(%d)",
					g_pilot_data.kills_full_on_player
						[combatant_id],
					g_pilot_data.kills_shared_on_player
						[combatant_id]);
			} else {
				int flight_group_index =
					combatant_id -
					FLIGHT_GROUP_COMBATANT_ID_BASE;
				if (g_debrief_team_has_only_ai_pilots
						    [g_frontend_mission
							     .flight_groups
								     [flight_group_index]
							     .team] == 0 ||
				    (g_pilot_data.kills_full_on_flight_group
						     [flight_group_index] ==
					     0 &&
				     g_pilot_data.kills_shared_on_flight_group
						     [flight_group_index] ==
					     0)) {
					continue;
				}
				sprintf(g_frontend_scratch_buffer, "%c%s %c%s",
					TEXT_CODE_RATING,
					frontend_string_get((
						frontend_string_id)(FRONTSTR_154_DRONE +
								    g_pilot_data.flight_group_rating
									    [flight_group_index])),
					TEXT_CODE_LABEL,
					g_frontend_mission
						.flight_groups
							[flight_group_index]
						.name);
				frontend_text_draw(TEXT_FONT_SIZE,
						   g_frontend_scratch_buffer,
						   TEAM_NAME_X, killed_row_y,
						   WHITE_COLOR);
				sprintf(g_frontend_scratch_buffer, "%d(%d)",
					g_pilot_data.kills_full_on_flight_group
						[flight_group_index],
					g_pilot_data
						.kills_shared_on_flight_group
							[flight_group_index]);
			}
			frontend_text_draw(
				TEXT_FONT_SIZE, g_frontend_scratch_buffer,
				KILLED_VALUE_X, killed_row_y, WHITE_COLOR);
			killed_row_y += ROW_HEIGHT;
		}

		frontend_text_draw(TEXT_FONT_SIZE,
				   frontend_string_get(FRONTSTR_541_KILLED_BY),
				   KILLED_BY_NAME_X, killed_header_y,
				   g_color_yellow);
		int killed_by_row_y = killed_header_y + ROW_HEIGHT;
		for (kill_index = 0; kill_index < PLAYER_COUNT; ++kill_index) {

			/* Like the Killed list, this walks the list sorted by
			 * kills made, as the original does.
			 * g_debrief_kills_from_combatant_ids, sorted by kills
			 * taken, is built in mission_debrief_prepare but never
			 * read. */
			combatant_id =
				g_debrief_kills_on_combatant_ids[kill_index];
			if (combatant_id == -1) {
				break;
			}
			if (combatant_id < FLIGHT_GROUP_COMBATANT_ID_BASE) {
				if (net_get_local_player_id() ==
					    g_pilot_data
						    .network_players
							    [combatant_id]
						    .direct_play_id ||
				    (g_pilot_data.kills_full_from_player
						     [combatant_id] == 0 &&
				     g_pilot_data.kills_shared_from_player
						     [combatant_id] == 0)) {
					continue;
				}
				frontend_draw_rect_assign(
					&rect, KILLED_BY_NAME_X,
					killed_by_row_y, KILLED_BY_NAME_RIGHT,
					killed_by_row_y + ROW_CLIP_HEIGHT);
				frontend_display_get_screen_clip_rect(
					&saved_clip_rect);
				frontend_display_set_screen_clip_rect640x480(
					&rect);
				if (g_pilot_data.network_players[combatant_id]
					    .has_left != 0) {
					sprintf(g_frontend_scratch_buffer,
						"[%s %s]",
						frontend_string_get((
							frontend_string_id)(FRONTSTR_154_DRONE +
									    g_pilot_data
										    .network_players
											    [combatant_id]
										    .rating)),
						g_pilot_data
							.network_players
								[combatant_id]
							.friendly_name);
					frontend_text_draw(
						TEXT_FONT_SIZE,
						g_frontend_scratch_buffer,
						KILLED_BY_NAME_X,
						killed_by_row_y, g_color_gray);
				} else {
					sprintf(g_frontend_scratch_buffer,
						"%c%s %c%s", TEXT_CODE_RATING,
						frontend_string_get((
							frontend_string_id)(FRONTSTR_154_DRONE +
									    g_pilot_data
										    .network_players
											    [combatant_id]
										    .rating)),
						TEXT_CODE_LABEL,
						g_pilot_data
							.network_players
								[combatant_id]
							.friendly_name);
					frontend_text_draw(
						TEXT_FONT_SIZE,
						g_frontend_scratch_buffer,
						KILLED_BY_NAME_X,
						killed_by_row_y, WHITE_COLOR);
				}
				frontend_display_set_screen_clip_rect640x480(
					&saved_clip_rect);
				sprintf(g_frontend_scratch_buffer, "%d(%d)",
					g_pilot_data.kills_full_from_player
						[combatant_id],
					g_pilot_data.kills_shared_from_player
						[combatant_id]);
			} else {
				int flight_group_index =
					combatant_id -
					FLIGHT_GROUP_COMBATANT_ID_BASE;
				if (g_debrief_team_has_only_ai_pilots
						    [g_frontend_mission
							     .flight_groups
								     [flight_group_index]
							     .team] == 0 ||
				    (g_pilot_data.kills_full_from_flight_group
						     [flight_group_index] ==
					     0 &&
				     g_pilot_data.kills_shared_from_flight_group
						     [flight_group_index] ==
					     0)) {
					continue;
				}
				sprintf(g_frontend_scratch_buffer, "%c%s %c%s",
					TEXT_CODE_RATING,
					frontend_string_get((
						frontend_string_id)(FRONTSTR_154_DRONE +
								    g_pilot_data.flight_group_rating
									    [flight_group_index])),
					TEXT_CODE_LABEL,
					g_frontend_mission
						.flight_groups
							[flight_group_index]
						.name);
				frontend_text_draw(TEXT_FONT_SIZE,
						   g_frontend_scratch_buffer,
						   KILLED_BY_NAME_X,
						   killed_by_row_y,
						   WHITE_COLOR);
				sprintf(g_frontend_scratch_buffer, "%d(%d)",
					g_pilot_data
						.kills_full_from_flight_group
							[flight_group_index],
					g_pilot_data
						.kills_shared_from_flight_group
							[flight_group_index]);
			}
			frontend_text_draw(TEXT_FONT_SIZE,
					   g_frontend_scratch_buffer,
					   KILLED_BY_VALUE_X, killed_by_row_y,
					   WHITE_COLOR);
			killed_by_row_y += ROW_HEIGHT;
		}
	}
	return 1;
}

/* Draws the player statistics page: 21 rows from
 * g_debrief_player_stats_scroll_row, with a scroll bar when there are more,
 * covering the place in a network melee, the mission result and time, score,
 * promotion, awards, kills, assists, hidden cargo found, laser and warhead
 * accuracy, kills by victim rank and by craft type, and losses, from the last
 * mission's fields of g_pilot_data. When g_debrief_stats_page_needs_rebuild is set
 * it first clears it, scrolls to row 0, counts the rows into
 * g_debrief_player_stats_row_count, sets the section flags and sums the
 * g_debrief totals. Returns 0, having drawn only the title, when in network
 * play the local player is not among g_pilot_data.network_players; else 1. */
// FUNCTION: XVT 0x501710
int mission_debrief_draw_player_statistics_page(void)
{
	enum {
		PLAYER_COUNT = 8,
		CRAFT_TYPE_COUNT = 100,
		PLAYER_RATING_COUNT = 25,
		AI_RATING_COUNT = 6,
		MISSION_TYPE = 0,
		MISSION_TYPE_COUNT = 1,
		AWARD_COUNT = 4,
		VISIBLE_ROW_COUNT = 21,
		SCROLL_PAGE_STEP = 5,
		TEXT_FONT_SIZE = 12,
		TITLE_FONT_SIZE = 15,
		TEXT_X = 88,
		VALUE_X = 268,
		FIRST_ROW_Y = 111,
		ROW_HEIGHT = 15,
		TEXT_CODE_VALUE = 1,
		TEXT_CODE_LABEL = 4,
		TEXT_CODE_RATING = 6,
		WHITE_COLOR = 0xFFFF,
		HIGHEST_RATING = PLAYER_RATING_COUNT - 1,
	};

	struct RECT rect;

	int row = 0;
	frontend_draw_rect_assign(&rect, 84, 90, 434, 106);
	frontend_text_draw_centered(
		TITLE_FONT_SIZE,
		frontend_string_get(FRONTSTR_312_PLAYER_STATISTICS), &rect,
		WHITE_COLOR);

	int local_player_index;
	if (g_frontend_mission_session_mode ==
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		local_player_index = 0;
	} else {
		local_player_index = 0;
		for (local_player_index = 0; local_player_index < PLAYER_COUNT;
		     ++local_player_index) {
			if (net_get_local_player_id() ==
			    g_pilot_data.network_players[local_player_index]
				    .direct_play_id) {
				break;
			}
		}
	}
	if (local_player_index == PLAYER_COUNT) {
		XVT_LOG_DEBUG(
			"debrief.stats_local_missing mode=%d ids=\"%u,%u,%u,%u,%u,%u,%u,%u\"",
			(int)g_frontend_mission_session_mode,
			(unsigned)g_pilot_data.network_players[0]
				.direct_play_id,
			(unsigned)g_pilot_data.network_players[1]
				.direct_play_id,
			(unsigned)g_pilot_data.network_players[2]
				.direct_play_id,
			(unsigned)g_pilot_data.network_players[3]
				.direct_play_id,
			(unsigned)g_pilot_data.network_players[4]
				.direct_play_id,
			(unsigned)g_pilot_data.network_players[5]
				.direct_play_id,
			(unsigned)g_pilot_data.network_players[6]
				.direct_play_id,
			(unsigned)g_pilot_data.network_players[7]
				.direct_play_id);
		return 0;
	}

	int award_index;
	int craft_type;
	int player_index;
	int rating;
	int mission_type;
	if (g_debrief_stats_page_needs_rebuild != 0) {
		int row_count = 13;
		g_debrief_player_stats_scroll_row = 0;
		g_debrief_stats_page_needs_rebuild = 0;
		if (g_frontend_mission.header.goals_unimportant == 0 &&
		    g_pilot_data.mission_directory_id !=
			    MISSION_DIRECTORY_MELEES) {
			g_debrief_player_stats_row_count = row_count;
			if (g_pilot_data.mission_directory_id !=
			    MISSION_DIRECTORY_TOURNAMENTS) {
				++row_count;
			}
		}
		if (g_frontend_mission_session_mode !=
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
			row_count += 5;
		}
		if (g_pilot_data.mission_directory_id ==
		    MISSION_DIRECTORY_MELEES) {
			++row_count;
		}
		if (g_pilot_data.promotion_delta != PILOT_PROMOTION_NONE) {
			++row_count;
		}
		for (award_index = 0; award_index < AWARD_COUNT;
		     ++award_index) {
			if (g_pilot_data
				    .faction_statistics
					    [g_pilot_data.current_faction_id]
				    .mission_awards[award_index] != 0) {
				++row_count;
			}
		}

		g_debrief_losses_to_non_player_pilots_total[MISSION_TYPE] = 0;
		g_debrief_losses_to_player_pilots_total[MISSION_TYPE] = 0;
		g_debrief_non_player_kills_shared_total[MISSION_TYPE] = 0;
		g_debrief_non_player_kills_full_total[MISSION_TYPE] = 0;
		g_debrief_player_kills_shared_total[MISSION_TYPE] = 0;
		g_debrief_player_kills_full_total[MISSION_TYPE] = 0;
		g_debrief_kills_shared_total[MISSION_TYPE] = 0;
		g_debrief_assists_total[MISSION_TYPE] = 0;
		g_debrief_has_craft_kills_by_type_section = 0;
		for (craft_type = 0; craft_type < CRAFT_TYPE_COUNT;
		     ++craft_type) {
			g_debrief_craft_kill_row_has_data = 0;
			for (mission_type = 0;
			     mission_type < MISSION_TYPE_COUNT;
			     ++mission_type) {
				if (g_pilot_data.last_mission_stats
						    .kills_per_craft_per_mt
							    [mission_type]
							    [craft_type] != 0 ||
				    g_pilot_data.last_mission_stats
						    .kills_shared_per_craft_per_mt
							    [mission_type]
							    [craft_type] != 0) {
					g_debrief_craft_kill_row_has_data = 1;
					g_debrief_has_craft_kills_by_type_section =
						1;
				}
			}
			if (g_debrief_craft_kill_row_has_data != 0) {
				++row_count;
			}
		}
		if (g_debrief_has_craft_kills_by_type_section != 0) {
			row_count += 2;
		}

		g_debrief_has_player_kills_by_rating = 0;
		if (g_frontend_mission_session_mode !=
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
			for (player_index = 0; player_index < PLAYER_COUNT;
			     ++player_index) {
				if (g_pilot_data.kills_full_on_player
						    [player_index] != 0 ||
				    g_pilot_data.kills_shared_on_player
						    [player_index] != 0) {
					g_debrief_has_player_kills_by_rating =
						1;
					++row_count;
				}
			}
			if (g_debrief_has_player_kills_by_rating != 0) {
				row_count += 2;
			}
		}

		g_debrief_player_stats_row_count = row_count;
		g_debrief_has_losses_from_players_section = 0;
		if (g_frontend_mission_session_mode !=
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
			for (player_index = 0; player_index < PLAYER_COUNT;
			     ++player_index) {
				if (g_pilot_data.kills_full_from_player
						    [player_index] != 0 ||
				    g_pilot_data.kills_shared_from_player
						    [player_index] != 0) {
					g_debrief_has_losses_from_players_section =
						1;
					++row_count;
				}
			}
			g_debrief_player_stats_row_count = row_count;
			if (g_debrief_has_losses_from_players_section != 0) {
				g_debrief_player_stats_row_count += 2;
			}
		}

		for (mission_type = 0; mission_type < MISSION_TYPE_COUNT;
		     ++mission_type) {
			for (craft_type = 0; craft_type < CRAFT_TYPE_COUNT;
			     ++craft_type) {
				int assist_count =
					g_pilot_data.last_mission_stats
						.kills_assists_per_craft_per_mt
							[mission_type]
							[craft_type];
				int shared_kill_count =
					g_pilot_data.last_mission_stats
						.kills_shared_per_craft_per_mt
							[mission_type]
							[craft_type];
				g_debrief_assists_total[mission_type] +=
					assist_count;
				g_debrief_kills_shared_total[mission_type] +=
					shared_kill_count;
			}
		}
		for (mission_type = 0; mission_type < MISSION_TYPE_COUNT;
		     ++mission_type) {
			for (rating = 0; rating < PLAYER_RATING_COUNT;
			     ++rating) {
				int full_kill_count =
					g_pilot_data.last_mission_stats
						.kills_full_on_player_rating_per_mt
							[mission_type][rating];
				int shared_kill_count =
					g_pilot_data.last_mission_stats
						.kills_shared_on_player_rating_per_mt
							[mission_type][rating];
				g_debrief_player_kills_full_total
					[mission_type] += full_kill_count;
				g_debrief_player_kills_shared_total
					[mission_type] += shared_kill_count;
			}
		}
		for (mission_type = 0; mission_type < MISSION_TYPE_COUNT;
		     ++mission_type) {
			for (rating = 0; rating < AI_RATING_COUNT; ++rating) {
				int full_kill_count =
					g_pilot_data.last_mission_stats
						.kills_full_on_ai_rating_per_mt
							[mission_type][rating];
				int shared_kill_count =
					g_pilot_data.last_mission_stats
						.kills_shared_on_ai_rating_per_mt
							[mission_type][rating];
				g_debrief_non_player_kills_full_total
					[mission_type] += full_kill_count;
				g_debrief_non_player_kills_shared_total
					[mission_type] += shared_kill_count;
			}
		}
		for (mission_type = 0; mission_type < MISSION_TYPE_COUNT;
		     ++mission_type) {
			for (rating = 0; rating < PLAYER_RATING_COUNT;
			     ++rating) {
				g_debrief_losses_to_player_pilots_total
					[mission_type] +=
					g_pilot_data.last_mission_stats
						.killed_by_player_rating_per_mt
							[mission_type][rating];
			}
		}
		for (mission_type = 0; mission_type < MISSION_TYPE_COUNT;
		     ++mission_type) {
			for (rating = 0; rating < AI_RATING_COUNT; ++rating) {
				g_debrief_losses_to_non_player_pilots_total
					[mission_type] +=
					g_pilot_data.last_mission_stats
						.killed_by_ai_rating_per_mt
							[mission_type][rating];
			}
		}
		XVT_LOG_DEBUG(
			"debrief.stats_counted entry=%d rows=%d kills=%d shared=%d assists=%d player_kills=%d player_shared=%d ai_kills=%d ai_shared=%d losses=%d lost_to_players=%d lost_to_ai=%d by_type=%d by_rank=%d from_players=%d",
			local_player_index, g_debrief_player_stats_row_count,
			g_pilot_data.last_mission_stats
				.total_kills_per_mt[MISSION_TYPE],
			g_debrief_kills_shared_total[MISSION_TYPE],
			g_debrief_assists_total[MISSION_TYPE],
			g_debrief_player_kills_full_total[MISSION_TYPE],
			g_debrief_player_kills_shared_total[MISSION_TYPE],
			g_debrief_non_player_kills_full_total[MISSION_TYPE],
			g_debrief_non_player_kills_shared_total[MISSION_TYPE],
			g_pilot_data.last_mission_stats
				.total_craft_losses_per_mt[MISSION_TYPE],
			g_debrief_losses_to_player_pilots_total[MISSION_TYPE],
			g_debrief_losses_to_non_player_pilots_total
				[MISSION_TYPE],
			g_debrief_has_craft_kills_by_type_section,
			g_debrief_has_player_kills_by_rating,
			g_debrief_has_losses_from_players_section);
	}

	frontend_draw_rect_assign(&rect, 425, 107, 434, 433);
	int total_rows = g_debrief_player_stats_row_count;
	int scroll_row;
	if (total_rows > VISIBLE_ROW_COUNT) {
		scroll_row = frontend_scrollbar_draw(
			&rect, g_debrief_player_stats_scroll_row, total_rows, 0,
			SCROLL_PAGE_STEP, g_color_navy, 10);
	} else {
		scroll_row = g_debrief_player_stats_scroll_row;
	}
	if (scroll_row != g_debrief_player_stats_scroll_row) {
		XVT_LOG_DEBUG(
			"debrief.stats_scrolled row=%d previous=%d rows=%d",
			scroll_row, g_debrief_player_stats_scroll_row,
			total_rows);
	}
	g_debrief_player_stats_scroll_row = scroll_row;

	int text_y = FIRST_ROW_Y;

	if (g_frontend_mission_session_mode !=
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
	    g_pilot_data.mission_directory_id == MISSION_DIRECTORY_MELEES) {
		if (row >= g_debrief_player_stats_scroll_row &&
		    row - g_debrief_player_stats_scroll_row <
			    VISIBLE_ROW_COUNT) {
			const char *ranking_group;

			if (g_debrief_rank_by_pilot != 0) {
				ranking_group = frontend_string_get(
					FRONTSTR_368_PILOTS);
			} else {
				ranking_group =
					frontend_string_get(FRONTSTR_691_TEAMS);
			}
			sprintf(g_frontend_scratch_buffer,
				"%c%s %c%s %c%s %c%d %c%s", TEXT_CODE_LABEL,
				frontend_string_get(FRONTSTR_367_PLACE),
				TEXT_CODE_VALUE,
				frontend_string_get((
					frontend_string_id)(FRONTSTR_318_1ST +
							    g_debrief_local_team_rank_index)),
				TEXT_CODE_LABEL,
				frontend_string_get(FRONTSTR_335_OF),
				TEXT_CODE_VALUE, g_debrief_active_team_count,
				TEXT_CODE_LABEL, ranking_group);
			frontend_text_draw(TEXT_FONT_SIZE,
					   g_frontend_scratch_buffer, TEXT_X,
					   text_y, WHITE_COLOR);
			text_y += ROW_HEIGHT;
		}
		++row;
	}

	if (g_frontend_mission.header.goals_unimportant == 0 &&
	    g_pilot_data.mission_directory_id != MISSION_DIRECTORY_MELEES &&
	    g_pilot_data.mission_directory_id !=
		    MISSION_DIRECTORY_TOURNAMENTS) {
		if (row >= g_debrief_player_stats_scroll_row &&
		    row - g_debrief_player_stats_scroll_row <
			    VISIBLE_ROW_COUNT) {
			int player_team_result =
				g_pilot_data.teams[g_pilot_data.team]
					.is_mission_completed;
			int opposing_team_result;
			if (g_pilot_data.mission_directory_id ==
			    MISSION_DIRECTORY_TRAINING_EXERCISES) {
				opposing_team_result = player_team_result == 0;
			} else {
				opposing_team_result =
					g_pilot_data
						.teams[g_pilot_data.team ^ 1]
						.is_mission_completed;
			}
			const char *result_text;
			if (opposing_team_result == player_team_result) {
				result_text =
					frontend_string_get(FRONTSTR_347_DRAW);
				sprintf(g_frontend_scratch_buffer, "%c%s %c%s",
					TEXT_CODE_LABEL,
					frontend_string_get(
						FRONTSTR_369_RESULT),
					TEXT_CODE_VALUE, result_text);
			} else if (player_team_result == 1) {
				frontend_format_seconds_to_clock_string(
					g_pilot_data
						.teams[g_frontend_mission
							       .flight_groups
								       [g_pilot_data
										.network_players
											[local_player_index]
										.flight_group_id]
							       .team]
						.mission_time);
				char mission_time_text[20];
				strcpy(mission_time_text,
				       g_frontend_scratch_buffer);
				sprintf(g_frontend_scratch_buffer,
					"%c%s %c%s %s.", TEXT_CODE_LABEL,
					frontend_string_get(
						FRONTSTR_369_RESULT),
					TEXT_CODE_VALUE,
					frontend_string_get(
						FRONTSTR_370_COMPLETED_MISSION_IN),
					mission_time_text);
			} else {
				result_text = frontend_string_get(
					FRONTSTR_371_FAILED_MISSION);
				sprintf(g_frontend_scratch_buffer, "%c%s %c%s",
					TEXT_CODE_LABEL,
					frontend_string_get(
						FRONTSTR_369_RESULT),
					TEXT_CODE_VALUE, result_text);
			}
			frontend_text_draw(TEXT_FONT_SIZE,
					   g_frontend_scratch_buffer, TEXT_X,
					   text_y, WHITE_COLOR);
			text_y += ROW_HEIGHT;
		}
		++row;
	}

	if (row >= g_debrief_player_stats_scroll_row &&
	    row - g_debrief_player_stats_scroll_row < VISIBLE_ROW_COUNT) {
		sprintf(g_frontend_scratch_buffer, "%c%s %c%d", TEXT_CODE_LABEL,
			frontend_string_get(FRONTSTR_333_MISSION_SCORE),
			TEXT_CODE_VALUE,
			g_pilot_data.network_players[local_player_index]
				.total_score);
		frontend_text_draw(TEXT_FONT_SIZE, g_frontend_scratch_buffer,
				   TEXT_X, text_y, WHITE_COLOR);
		text_y += ROW_HEIGHT;
	}
	++row;

	if (g_pilot_data.promotion_delta != PILOT_PROMOTION_NONE) {
		if (row >= g_debrief_player_stats_scroll_row &&
		    row - g_debrief_player_stats_scroll_row <
			    VISIBLE_ROW_COUNT) {
			sprintf(g_frontend_scratch_buffer, "rank%d",
				g_pilot_data.rating);
			front_image_draw_sprite(g_frontend_scratch_buffer,
						TEXT_X, text_y + 1);
			sprintf(g_frontend_scratch_buffer, "%c%s %c%s",
				TEXT_CODE_LABEL,
				frontend_string_get((
					frontend_string_id)(FRONTSTR_375_NO_PROMOTION +
							    g_pilot_data
								    .promotion_delta)),
				TEXT_CODE_RATING,
				frontend_string_get(
					(frontend_string_id)(122 +
							     g_pilot_data
								     .rating)));
			frontend_text_draw(TEXT_FONT_SIZE,
					   g_frontend_scratch_buffer, 145,
					   text_y, WHITE_COLOR);
			text_y += ROW_HEIGHT;
		}
		++row;
	}

	/* g_debrief_craft_kill_row_has_data is reused here to record whether
	 * the Award label has been drawn. */
	g_debrief_craft_kill_row_has_data = 0;
	for (award_index = 0; award_index < AWARD_COUNT; ++award_index) {
		if (g_pilot_data
			    .faction_statistics[g_pilot_data.current_faction_id]
			    .mission_awards[award_index] == 0) {
			continue;
		}
		if (row >= g_debrief_player_stats_scroll_row &&
		    row - g_debrief_player_stats_scroll_row <
			    VISIBLE_ROW_COUNT) {
			if (g_debrief_craft_kill_row_has_data == 0) {
				frontend_text_draw(
					TEXT_FONT_SIZE,
					frontend_string_get(FRONTSTR_377_AWARD),
					TEXT_X, text_y, g_color_yellow);
				g_debrief_craft_kill_row_has_data = 1;
			}
			int award_text_width =
				frontend_text_measure_width(
					frontend_string_get(FRONTSTR_377_AWARD),
					TEXT_FONT_SIZE) +
				10;
			if (award_index == 2) {
				if (g_pilot_data.current_faction_id == 0) {
					sprintf(g_frontend_scratch_buffer,
						"rcitlvl%d",
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.mission_awards
								[award_index]);
				} else {
					sprintf(g_frontend_scratch_buffer,
						"citlvl%d",
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.mission_awards
								[award_index]);
				}
			} else {
				sprintf(g_frontend_scratch_buffer, "medlvl%d",
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.mission_awards[award_index]);
			}
			front_image_draw_sprite(g_frontend_scratch_buffer,
						award_text_width + TEXT_X,
						text_y);
			frontend_string_id award_level_string;
			if (award_index == 2) {
				award_level_string =
					(frontend_string_id)(659 +
							     g_pilot_data
								     .faction_statistics
									     [g_pilot_data
										      .current_faction_id]
								     .mission_awards
									     [award_index]);
			} else {
				award_level_string =
					(frontend_string_id)(381 +
							     g_pilot_data
								     .faction_statistics
									     [g_pilot_data
										      .current_faction_id]
								     .mission_awards
									     [award_index]);
			}
			sprintf(g_frontend_scratch_buffer, "%s - %s",
				frontend_string_get((
					frontend_string_id)(FRONTSTR_378_MELEE_PLAQUE +
							    award_index)),
				frontend_string_get(award_level_string));
			frontend_text_draw(
				TEXT_FONT_SIZE, g_frontend_scratch_buffer,
				award_text_width + 128, text_y, WHITE_COLOR);
			text_y += ROW_HEIGHT;
		}
		++row;
	}

	if (row >= g_debrief_player_stats_scroll_row &&
	    row - g_debrief_player_stats_scroll_row < VISIBLE_ROW_COUNT) {
		text_y += ROW_HEIGHT;
	}
	++row;
	if (row >= g_debrief_player_stats_scroll_row &&
	    row - g_debrief_player_stats_scroll_row < VISIBLE_ROW_COUNT) {
		frontend_text_draw(
			TEXT_FONT_SIZE,
			frontend_string_get(FRONTSTR_350_SUMMARY_OF_KILLS),
			TEXT_X, text_y, g_color_yellow);
		text_y += ROW_HEIGHT;
	}
	++row;
	if (row >= g_debrief_player_stats_scroll_row &&
	    row - g_debrief_player_stats_scroll_row < VISIBLE_ROW_COUNT) {
		frontend_text_draw(
			TEXT_FONT_SIZE,
			frontend_string_get(FRONTSTR_297_TOTAL_KILLS), TEXT_X,
			text_y, g_color_yellow);
		sprintf(g_frontend_scratch_buffer, "%d (%d)",
			g_pilot_data.last_mission_stats
				.total_kills_per_mt[MISSION_TYPE],
			g_debrief_kills_shared_total[MISSION_TYPE]);
		frontend_text_draw(TEXT_FONT_SIZE, g_frontend_scratch_buffer,
				   VALUE_X, text_y, WHITE_COLOR);
		text_y += ROW_HEIGHT;
	}
	++row;

	if (g_frontend_mission_session_mode !=
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		if (row >= g_debrief_player_stats_scroll_row &&
		    row - g_debrief_player_stats_scroll_row <
			    VISIBLE_ROW_COUNT) {
			frontend_text_draw(
				TEXT_FONT_SIZE,
				frontend_string_get(FRONTSTR_351_PLAYER_KILLS),
				TEXT_X, text_y, g_color_yellow);
			if (g_pilot_data.mission_directory_id ==
			    MISSION_DIRECTORY_TRAINING_EXERCISES) {
				sprintf(g_frontend_scratch_buffer, "----");
			} else {
				sprintf(g_frontend_scratch_buffer, "%d (%d)",
					g_debrief_player_kills_full_total
						[MISSION_TYPE],
					g_debrief_player_kills_shared_total
						[MISSION_TYPE]);
			}
			frontend_text_draw(TEXT_FONT_SIZE,
					   g_frontend_scratch_buffer, VALUE_X,
					   text_y, WHITE_COLOR);
			text_y += ROW_HEIGHT;
		}
		++row;
		if (row >= g_debrief_player_stats_scroll_row &&
		    row - g_debrief_player_stats_scroll_row <
			    VISIBLE_ROW_COUNT) {
			frontend_text_draw(
				TEXT_FONT_SIZE,
				frontend_string_get(
					FRONTSTR_352_NON_PLAYER_KILLS),
				TEXT_X, text_y, g_color_yellow);
			sprintf(g_frontend_scratch_buffer, "%d (%d)",
				g_debrief_non_player_kills_full_total
					[MISSION_TYPE],
				g_debrief_non_player_kills_shared_total
					[MISSION_TYPE]);
			frontend_text_draw(TEXT_FONT_SIZE,
					   g_frontend_scratch_buffer, VALUE_X,
					   text_y, WHITE_COLOR);
			text_y += ROW_HEIGHT;
		}
		++row;
	}

	if (row >= g_debrief_player_stats_scroll_row &&
	    row - g_debrief_player_stats_scroll_row < VISIBLE_ROW_COUNT) {
		frontend_text_draw(TEXT_FONT_SIZE,
				   frontend_string_get(FRONTSTR_353_ASSISTS),
				   TEXT_X, text_y, g_color_yellow);
		sprintf(g_frontend_scratch_buffer, "%d",
			g_debrief_assists_total[MISSION_TYPE]);
		frontend_text_draw(TEXT_FONT_SIZE, g_frontend_scratch_buffer,
				   VALUE_X, text_y, WHITE_COLOR);
		text_y += ROW_HEIGHT;
	}
	++row;
	if (row >= g_debrief_player_stats_scroll_row &&
	    row - g_debrief_player_stats_scroll_row < VISIBLE_ROW_COUNT) {
		frontend_text_draw(
			TEXT_FONT_SIZE,
			frontend_string_get(FRONTSTR_354_HIDDEN_CARGO_FOUND),
			TEXT_X, text_y, g_color_yellow);
		sprintf(g_frontend_scratch_buffer, "%d",
			g_pilot_data.last_mission_stats
				.num_special_inspected_per_mt[MISSION_TYPE]);
		frontend_text_draw(TEXT_FONT_SIZE, g_frontend_scratch_buffer,
				   VALUE_X, text_y, WHITE_COLOR);
		text_y += ROW_HEIGHT;
	}
	++row;
	if (row >= g_debrief_player_stats_scroll_row &&
	    row - g_debrief_player_stats_scroll_row < VISIBLE_ROW_COUNT) {
		frontend_text_draw(
			TEXT_FONT_SIZE,
			frontend_string_get(FRONTSTR_355_LASER_ACCURACY),
			TEXT_X, text_y, g_color_yellow);
		unsigned int accuracy = 0;
		if (g_pilot_data.last_mission_stats
			    .energy_fired_per_mt[MISSION_TYPE] != 0) {
			accuracy =
				(unsigned int)(100 *
					       g_pilot_data.last_mission_stats
						       .energy_hits_per_mt
							       [MISSION_TYPE]) /
				(unsigned int)g_pilot_data.last_mission_stats
					.energy_fired_per_mt[MISSION_TYPE];
		}
		sprintf(g_frontend_scratch_buffer, "%d%%", accuracy);
		frontend_text_draw(TEXT_FONT_SIZE, g_frontend_scratch_buffer,
				   VALUE_X, text_y, WHITE_COLOR);
		text_y += ROW_HEIGHT;
	}
	++row;
	if (row >= g_debrief_player_stats_scroll_row &&
	    row - g_debrief_player_stats_scroll_row < VISIBLE_ROW_COUNT) {
		frontend_text_draw(
			TEXT_FONT_SIZE,
			frontend_string_get(FRONTSTR_356_WARHEAD_ACCURACY),
			TEXT_X, text_y, g_color_yellow);
		unsigned int accuracy = 0;
		if (g_pilot_data.last_mission_stats
			    .warheads_fired_per_mt[MISSION_TYPE] != 0) {
			accuracy =
				(unsigned int)(100 *
					       g_pilot_data.last_mission_stats
						       .warheads_hits_per_mt
							       [MISSION_TYPE]) /
				(unsigned int)g_pilot_data.last_mission_stats
					.warheads_fired_per_mt[MISSION_TYPE];
		}
		sprintf(g_frontend_scratch_buffer, "%d%%", accuracy);
		frontend_text_draw(TEXT_FONT_SIZE, g_frontend_scratch_buffer,
				   VALUE_X, text_y, WHITE_COLOR);
		text_y += ROW_HEIGHT;
	}
	++row;

	if (g_debrief_has_player_kills_by_rating != 0) {
		if (row >= g_debrief_player_stats_scroll_row &&
		    row - g_debrief_player_stats_scroll_row <
			    VISIBLE_ROW_COUNT) {
			text_y += ROW_HEIGHT;
		}
		++row;
		if (row >= g_debrief_player_stats_scroll_row &&
		    row - g_debrief_player_stats_scroll_row <
			    VISIBLE_ROW_COUNT) {
			frontend_text_draw(
				TEXT_FONT_SIZE,
				frontend_string_get(
					FRONTSTR_357_PLAYER_KILLS_BY_RANK),
				TEXT_X, text_y, g_color_yellow);
			text_y += ROW_HEIGHT;
		}
		++row;
		for (rating = HIGHEST_RATING; rating >= 0; --rating) {
			for (player_index = 0; player_index < PLAYER_COUNT;
			     ++player_index) {
				if (g_pilot_data.network_players[player_index]
						    .rating != rating ||
				    (g_pilot_data.kills_full_on_player
						     [player_index] == 0 &&
				     g_pilot_data.kills_shared_on_player
						     [player_index] == 0)) {
					continue;
				}
				if (row >= g_debrief_player_stats_scroll_row &&
				    row - g_debrief_player_stats_scroll_row <
					    VISIBLE_ROW_COUNT) {
					sprintf(g_frontend_scratch_buffer,
						"%c%s %c%s", TEXT_CODE_RATING,
						frontend_string_get((
							frontend_string_id)(FRONTSTR_154_DRONE +
									    rating)),
						TEXT_CODE_VALUE,
						g_pilot_data
							.network_players
								[player_index]
							.friendly_name);
					frontend_text_draw(
						TEXT_FONT_SIZE,
						g_frontend_scratch_buffer,
						TEXT_X, text_y, g_color_yellow);
					sprintf(g_frontend_scratch_buffer,
						"%d(%d)",
						g_pilot_data
							.kills_full_on_player
								[player_index],
						g_pilot_data
							.kills_shared_on_player
								[player_index]);
					frontend_text_draw(
						TEXT_FONT_SIZE,
						g_frontend_scratch_buffer,
						VALUE_X, text_y, WHITE_COLOR);
					text_y += ROW_HEIGHT;
				}
				++row;
			}
		}
	}

	if (g_debrief_has_craft_kills_by_type_section != 0) {
		if (row >= g_debrief_player_stats_scroll_row &&
		    row - g_debrief_player_stats_scroll_row <
			    VISIBLE_ROW_COUNT) {
			text_y += ROW_HEIGHT;
		}
		++row;
		if (row >= g_debrief_player_stats_scroll_row &&
		    row - g_debrief_player_stats_scroll_row <
			    VISIBLE_ROW_COUNT) {
			frontend_text_draw(
				TEXT_FONT_SIZE,
				frontend_string_get(
					FRONTSTR_358_CRAFT_KILLS_BY_TYPE),
				TEXT_X, text_y, g_color_yellow);
			text_y += ROW_HEIGHT;
		}
		++row;
	}
	for (craft_type = 0; craft_type < CRAFT_TYPE_COUNT; ++craft_type) {
		g_debrief_craft_kill_row_has_data = 0;
		for (mission_type = 0; mission_type < MISSION_TYPE_COUNT;
		     ++mission_type) {
			if (g_pilot_data.last_mission_stats
					    .kills_per_craft_per_mt
						    [mission_type]
						    [craft_type] != 0 ||
			    g_pilot_data.last_mission_stats
					    .kills_shared_per_craft_per_mt
						    [mission_type]
						    [craft_type] != 0) {
				g_debrief_craft_kill_row_has_data = 1;
			}
		}
		if (g_debrief_craft_kill_row_has_data == 0) {
			continue;
		}
		if (row >= g_debrief_player_stats_scroll_row &&
		    row - g_debrief_player_stats_scroll_row <
			    VISIBLE_ROW_COUNT) {
			frontend_text_draw(
				TEXT_FONT_SIZE,
				frontend_string_get(
					(frontend_string_id)(21 + craft_type)),
				TEXT_X, text_y, g_color_red);
			sprintf(g_frontend_scratch_buffer, "%d (%d)",
				g_pilot_data.last_mission_stats
					.kills_per_craft_per_mt[MISSION_TYPE]
							       [craft_type],
				g_pilot_data.last_mission_stats
					.kills_shared_per_craft_per_mt
						[MISSION_TYPE][craft_type]);
			frontend_text_draw(TEXT_FONT_SIZE,
					   g_frontend_scratch_buffer, VALUE_X,
					   text_y, WHITE_COLOR);
			text_y += ROW_HEIGHT;
		}
		++row;
	}

	if (row >= g_debrief_player_stats_scroll_row &&
	    row - g_debrief_player_stats_scroll_row < VISIBLE_ROW_COUNT) {
		text_y += ROW_HEIGHT;
	}
	++row;
	if (row >= g_debrief_player_stats_scroll_row &&
	    row - g_debrief_player_stats_scroll_row < VISIBLE_ROW_COUNT) {
		frontend_text_draw(
			TEXT_FONT_SIZE,
			frontend_string_get(FRONTSTR_360_TOTAL_LOSSES), TEXT_X,
			text_y, g_color_yellow);
		text_y += ROW_HEIGHT;
	}
	++row;
	if (row >= g_debrief_player_stats_scroll_row &&
	    row - g_debrief_player_stats_scroll_row < VISIBLE_ROW_COUNT) {
		frontend_text_draw(
			TEXT_FONT_SIZE,
			frontend_string_get(FRONTSTR_361_TOTAL_CRAFT_LOSSES),
			TEXT_X, text_y, g_color_yellow);
		sprintf(g_frontend_scratch_buffer, "%d",
			g_pilot_data.last_mission_stats
				.total_craft_losses_per_mt[MISSION_TYPE]);
		frontend_text_draw(TEXT_FONT_SIZE, g_frontend_scratch_buffer,
				   VALUE_X, text_y, WHITE_COLOR);
		text_y += ROW_HEIGHT;
	}
	++row;

	if (g_frontend_mission_session_mode !=
	    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		if (row >= g_debrief_player_stats_scroll_row &&
		    row - g_debrief_player_stats_scroll_row <
			    VISIBLE_ROW_COUNT) {
			frontend_text_draw(
				TEXT_FONT_SIZE,
				frontend_string_get(
					FRONTSTR_362_TO_PLAYER_PILOTS),
				TEXT_X, text_y, g_color_yellow);
			sprintf(g_frontend_scratch_buffer, "%d",
				g_debrief_losses_to_player_pilots_total
					[MISSION_TYPE]);
			frontend_text_draw(TEXT_FONT_SIZE,
					   g_frontend_scratch_buffer, VALUE_X,
					   text_y, WHITE_COLOR);
			text_y += ROW_HEIGHT;
		}
		++row;
		if (row >= g_debrief_player_stats_scroll_row &&
		    row - g_debrief_player_stats_scroll_row <
			    VISIBLE_ROW_COUNT) {
			frontend_text_draw(
				TEXT_FONT_SIZE,
				frontend_string_get(
					FRONTSTR_363_TO_NON_PLAYER_PILOTS),
				TEXT_X, text_y, g_color_yellow);
			sprintf(g_frontend_scratch_buffer, "%d",
				g_debrief_losses_to_non_player_pilots_total
					[MISSION_TYPE]);
			frontend_text_draw(TEXT_FONT_SIZE,
					   g_frontend_scratch_buffer, VALUE_X,
					   text_y, WHITE_COLOR);
			text_y += ROW_HEIGHT;
		}
		++row;
	}

	if (row >= g_debrief_player_stats_scroll_row &&
	    row - g_debrief_player_stats_scroll_row < VISIBLE_ROW_COUNT) {
		frontend_text_draw(
			TEXT_FONT_SIZE,
			frontend_string_get(FRONTSTR_364_TO_STARSHIPS), TEXT_X,
			text_y, g_color_yellow);
		sprintf(g_frontend_scratch_buffer, "%d",
			g_pilot_data.last_mission_stats
				.losses_by_starships_per_mt[MISSION_TYPE]);
		frontend_text_draw(TEXT_FONT_SIZE, g_frontend_scratch_buffer,
				   VALUE_X, text_y, WHITE_COLOR);
		text_y += ROW_HEIGHT;
	}
	++row;
	if (row >= g_debrief_player_stats_scroll_row &&
	    row - g_debrief_player_stats_scroll_row < VISIBLE_ROW_COUNT) {
		frontend_text_draw(TEXT_FONT_SIZE,
				   frontend_string_get(FRONTSTR_365_TO_MINES),
				   TEXT_X, text_y, g_color_yellow);
		sprintf(g_frontend_scratch_buffer, "%d",
			g_pilot_data.last_mission_stats
				.losses_by_mines_per_mt[MISSION_TYPE]);
		frontend_text_draw(TEXT_FONT_SIZE, g_frontend_scratch_buffer,
				   VALUE_X, text_y, WHITE_COLOR);
		text_y += ROW_HEIGHT;
	}
	++row;
	if (row >= g_debrief_player_stats_scroll_row &&
	    row - g_debrief_player_stats_scroll_row < VISIBLE_ROW_COUNT) {
		frontend_text_draw(
			TEXT_FONT_SIZE,
			frontend_string_get(FRONTSTR_366_FROM_COLLISIONS),
			TEXT_X, text_y, g_color_yellow);
		sprintf(g_frontend_scratch_buffer, "%d",
			g_pilot_data.last_mission_stats
				.losses_by_collisions_per_mt[MISSION_TYPE]);
		frontend_text_draw(TEXT_FONT_SIZE, g_frontend_scratch_buffer,
				   VALUE_X, text_y, WHITE_COLOR);
		text_y += ROW_HEIGHT;
	}
	++row;

	if (g_debrief_has_losses_from_players_section != 0) {
		if (row >= g_debrief_player_stats_scroll_row &&
		    row - g_debrief_player_stats_scroll_row <
			    VISIBLE_ROW_COUNT) {
			text_y += ROW_HEIGHT;
		}
		++row;
		if (row >= g_debrief_player_stats_scroll_row &&
		    row - g_debrief_player_stats_scroll_row <
			    VISIBLE_ROW_COUNT) {
			frontend_text_draw(
				TEXT_FONT_SIZE,
				frontend_string_get(
					FRONTSTR_388_LOSSES_FROM_PLAYERS),
				TEXT_X, text_y, g_color_yellow);
			text_y += ROW_HEIGHT;
		}
		++row;
		for (rating = HIGHEST_RATING; rating >= 0; --rating) {
			for (player_index = 0; player_index < PLAYER_COUNT;
			     ++player_index) {
				if (g_pilot_data.network_players[player_index]
						    .rating != rating ||
				    (g_pilot_data.kills_full_from_player
						     [player_index] == 0 &&
				     g_pilot_data.kills_shared_from_player
						     [player_index] == 0)) {
					continue;
				}
				if (row >= g_debrief_player_stats_scroll_row &&
				    row - g_debrief_player_stats_scroll_row <
					    VISIBLE_ROW_COUNT) {
					sprintf(g_frontend_scratch_buffer,
						"%c%s %c%s", TEXT_CODE_RATING,
						frontend_string_get((
							frontend_string_id)(FRONTSTR_154_DRONE +
									    rating)),
						TEXT_CODE_VALUE,
						g_pilot_data
							.network_players
								[player_index]
							.friendly_name);
					frontend_text_draw(
						TEXT_FONT_SIZE,
						g_frontend_scratch_buffer,
						TEXT_X, text_y, g_color_yellow);
					sprintf(g_frontend_scratch_buffer,
						"%d(%d)",
						g_pilot_data
							.kills_full_from_player
								[player_index],
						g_pilot_data
							.kills_shared_from_player
								[player_index]);
					frontend_text_draw(
						TEXT_FONT_SIZE,
						g_frontend_scratch_buffer,
						VALUE_X, text_y, WHITE_COLOR);
					text_y += ROW_HEIGHT;
				}
				++row;
			}
		}
	}

	return 1;
}

/* Draws the battle summary page: the battle's description
 * (g_mission_sequence_description); when a side has victories_needed wins, that
 * side's victory and the faction's battle medallion award, if any, else the
 * victories needed; the Imperial and Rebel win counts; and each mission
 * played, at most 10, with its description and result. Holding Alt, Shift and
 * F9 draws the "pl2" sprite. Returns 1. Does not check g_mission_list for
 * NULL or the stored mission list indices against g_mission_count. */
// FUNCTION: XVT 0x502E20
int mission_debrief_draw_battle_summary_page(void)
{
	if (keyboard_is_key_down(0x12) && keyboard_is_key_down(0x10) &&
	    keyboard_is_key_down(0x78)) {
		front_image_draw_sprite("pl2", 175, 342);
	}
	struct RECT rect;
	frontend_draw_rect_assign(&rect, 84, 90, 434, 106);
	frontend_text_draw_centered(
		15, frontend_string_get(FRONTSTR_314_BATTLE_SUMMARY), &rect,
		0xFFFF);
	frontend_draw_rect_assign(&rect, 84, 110, 434, 115);
	frontend_text_draw_centered(12, g_mission_sequence_description, &rect,
				    g_color_yellow);
	frontend_draw_rect_offset_xy(&rect, 0, 20);

	int imperial_victories = 0;
	int rebel_victories = 0;
	/* The original UI treats the persisted mission index as an unsigned bound. */
	unsigned int mission_index;
	for (mission_index = 0;
	     mission_index <=
		     g_pilot_data.battle_sequence_state.current_mission_index &&
	     mission_index < 10;
	     ++mission_index) {
		switch (g_pilot_data.battle_sequence_state
				.mission_results[mission_index]) {
		case BATTLE_MISSION_RESULT_IMPERIAL_VICTORY:
			++imperial_victories;
			break;
		case BATTLE_MISSION_RESULT_REBEL_VICTORY:
			++rebel_victories;
			break;
		default:
			break;
		}
	}

	int victories_needed =
		g_pilot_data.battle_sequence_state.victories_needed;
	struct RECT resource_rect;
	int award_text_width;
	int award_x;
	char resource_name[52];
	if (imperial_victories == victories_needed) {
		sprintf(g_frontend_scratch_buffer, "%c%s: %c%s", 4,
			frontend_string_get(FRONTSTR_709_BATTLE_COMPLETED), 5,
			frontend_string_get(FRONTSTR_345_IMPERIAL_VICTORY));
		frontend_text_draw_centered(12, g_frontend_scratch_buffer,
					    &rect, 0xFFFF);
		if (g_pilot_data
			    .faction_statistics[g_pilot_data.current_faction_id]
			    .mission_awards[3] != 0) {
			frontend_draw_rect_offset_xy(&rect, 0, 15);
			sprintf(g_frontend_scratch_buffer, "%s %s - %s",
				frontend_string_get(FRONTSTR_377_AWARD),
				frontend_string_get(
					FRONTSTR_381_BATTLE_MEDALLION),
				frontend_string_get((
					frontend_string_id)(g_pilot_data
								    .faction_statistics
									    [g_pilot_data
										     .current_faction_id]
								    .mission_awards
									    [3] +
							    381)));
			award_text_width =
				frontend_text_measure_width(
					g_frontend_scratch_buffer, 12) +
				10;
			sprintf(resource_name, "medlvl%d",
				g_pilot_data
					.faction_statistics
						[g_pilot_data
							 .current_faction_id]
					.mission_awards[3]);
			front_image_get_resource_rect(resource_name,
						      &resource_rect);
			award_x = rect.left + ((rect.right - rect.left) >> 1) -
				  ((unsigned int)(resource_rect.right +
						  award_text_width -
						  resource_rect.left + 11) >>
				   1);
			frontend_text_draw(12, g_frontend_scratch_buffer,
					   award_x, rect.top, 0xFFFF);
			front_image_draw_sprite(resource_name,
						award_x + award_text_width + 10,
						rect.top);
		}
	} else if (rebel_victories == victories_needed) {
		sprintf(g_frontend_scratch_buffer, "%c%s: %c%s", 4,
			frontend_string_get(FRONTSTR_709_BATTLE_COMPLETED), 3,
			frontend_string_get(FRONTSTR_346_REBEL_VICTORY));
		frontend_text_draw_centered(12, g_frontend_scratch_buffer,
					    &rect, 0xFFFF);
		if (g_pilot_data
			    .faction_statistics[g_pilot_data.current_faction_id]
			    .mission_awards[3] != 0) {
			frontend_draw_rect_offset_xy(&rect, 0, 15);
			sprintf(g_frontend_scratch_buffer, "%s %s - %s",
				frontend_string_get(FRONTSTR_377_AWARD),
				frontend_string_get(
					FRONTSTR_381_BATTLE_MEDALLION),
				frontend_string_get((
					frontend_string_id)(g_pilot_data
								    .faction_statistics
									    [g_pilot_data
										     .current_faction_id]
								    .mission_awards
									    [3] +
							    381)));
			award_text_width =
				frontend_text_measure_width(
					g_frontend_scratch_buffer, 12) +
				10;
			sprintf(resource_name, "medlvl%d",
				g_pilot_data
					.faction_statistics
						[g_pilot_data
							 .current_faction_id]
					.mission_awards[3]);
			front_image_get_resource_rect(resource_name,
						      &resource_rect);
			award_x = rect.left + ((rect.right - rect.left) >> 1) -
				  ((unsigned int)(resource_rect.right +
						  award_text_width -
						  resource_rect.left + 11) >>
				   1);
			frontend_text_draw(12, g_frontend_scratch_buffer,
					   award_x, rect.top, 0xFFFF);
			front_image_draw_sprite(resource_name,
						award_x + award_text_width + 10,
						rect.top);
		}
	} else {
		int display_victories_needed =
			g_pilot_data.battle_sequence_state.victories_needed;
		sprintf(g_frontend_scratch_buffer, "%c%s %c%d", 4,
			frontend_string_get(
				FRONTSTR_341_VICTORIES_NEEDED_TO_WIN),
			1, display_victories_needed);
		frontend_text_draw_centered(12, g_frontend_scratch_buffer,
					    &rect, 0xFFFF);
	}

	frontend_draw_rect_offset_xy(&rect, 0, 20);
	sprintf(g_frontend_scratch_buffer, "%c%s %c%d   %c%s %c%d", 5,
		frontend_string_get(FRONTSTR_342_IMPERIAL_VICTORIES), 1,
		imperial_victories, 3,
		frontend_string_get(FRONTSTR_343_REBEL_VICTORIES), 1,
		rebel_victories);
	frontend_text_draw_centered(12, g_frontend_scratch_buffer, &rect,
				    0xFFFF);
	frontend_draw_rect_offset_xy(&rect, 0, 20);

	unsigned char result_color = (unsigned char)resource_name[0];
	for (mission_index = 0;
	     mission_index <=
		     g_pilot_data.battle_sequence_state.current_mission_index &&
	     mission_index < 10;
	     ++mission_index) {
		switch (g_pilot_data.battle_sequence_state
				.mission_results[mission_index]) {
		case BATTLE_MISSION_RESULT_IMPERIAL_VICTORY:
			result_color = 5;
			break;
		case BATTLE_MISSION_RESULT_REBEL_VICTORY:
			result_color = 3;
			break;
		case BATTLE_MISSION_RESULT_DRAW:
			result_color = 1;
			break;
		}
		sprintf(g_frontend_scratch_buffer, "%c%s", 4,
			g_mission_list
				[g_pilot_data.battle_sequence_state
					 .mission_list_indices[mission_index]]
					.description);
		frontend_text_draw_centered(12, g_frontend_scratch_buffer,
					    &rect, 0xFFFF);
		frontend_draw_rect_offset_xy(&rect, 0, 15);
		sprintf(g_frontend_scratch_buffer, "%c%s", result_color,
			frontend_string_get((
				frontend_string_id)(g_pilot_data
							    .battle_sequence_state
							    .mission_results
								    [mission_index] +
						    345)));
		frontend_text_draw_centered(12, g_frontend_scratch_buffer,
					    &rect, 0xFFFF);
		frontend_draw_rect_offset_xy(&rect, 0, 15);
	}
	return 1;
}

/* Draws the tournament summary page: the tournament's description; after its
 * last mission, the winners, every team or pilot tied at the top total score
 * (at most 9 rows), the local one in pulsing colors with its tournament trophy
 * award; then the score totals after current_mission_index + 1 of mission_count
 * missions, for each team in g_debrief_standings_team_ids order, with its total
 * score and its first, second and third place counts, and its pilots or AI
 * flight groups. Returns 1. */
// FUNCTION: XVT 0x503400
int mission_debrief_draw_tournament_summary_page(int frame_counter)
{
	enum {
		PLAYER_COUNT = 8,
		MAX_WINNER_ROWS = 9,
		PULSE_PERIOD = 24,
		TITLE_FONT_SIZE = 15,
		TEXT_FONT_SIZE = 12,
		TITLE_LEFT = 84,
		TITLE_TOP = 90,
		TITLE_RIGHT = 434,
		TITLE_BOTTOM = 106,
		DESCRIPTION_TOP = 110,
		DESCRIPTION_BOTTOM = 115,
		CONTENT_LEFT = 88,
		PLAYER_INDENT_LEFT = 103,
		SCORE_COLUMN_X = 238,
		FIRST_PLACE_COLUMN_X = 308,
		SECOND_PLACE_COLUMN_X = 348,
		THIRD_PLACE_COLUMN_X = 388,
		NAME_COLUMN_WIDTH = 148,
		SCORE_COLUMN_OFFSET = 150,
		PLACEMENT_COLUMN_OFFSET = 220,
		PLACEMENT_COLUMN_SPACING = 40,
		LINE_HEIGHT = 15,
		WINNER_LINE_HEIGHT = 17,
		AWARD_GAP = 10,
		AWARD_LAYOUT_PADDING = 11,
		TEXT_CODE_VALUE = 1,
		TEXT_CODE_WINNER = 2,
		TEXT_CODE_LABEL = 4,
		TEXT_CODE_RATING = 6,
	};

	struct RECT rect;

	frontend_draw_rect_assign(&rect, TITLE_LEFT, TITLE_TOP, TITLE_RIGHT,
				  TITLE_BOTTOM);
	frontend_text_draw_centered(
		TITLE_FONT_SIZE,
		frontend_string_get(FRONTSTR_315_TOURNAMENT_SUMMARY), &rect,
		0xFFFF);
	frontend_draw_rect_assign(&rect, TITLE_LEFT, DESCRIPTION_TOP,
				  TITLE_RIGHT, DESCRIPTION_BOTTOM);
	frontend_text_draw_centered(TEXT_FONT_SIZE,
				    g_mission_sequence_description, &rect,
				    g_color_yellow);
	frontend_draw_rect_offset_xy(&rect, 0, 20);

	int standing_index;
	if (g_pilot_data.melee_tournament_sequence_state.mission_count -
		    g_pilot_data.melee_tournament_sequence_state
			    .current_mission_index ==
	    1) {
		struct RECT resource_rect;
		char resource_name[52];
		if (g_debrief_rank_by_pilot != 0) {
			for (standing_index = 0;
			     standing_index < MAX_WINNER_ROWS;
			     ++standing_index) {
				int team_id = g_debrief_standings_team_ids
					[standing_index];

				if (team_id == -1) {
					break;
				}
				if (g_debrief_team_has_only_ai_pilots
					    [team_id] == 0) {
					int player_index;

					for (player_index = 0;
					     player_index < PLAYER_COUNT;
					     ++player_index) {
						struct pilot_network_player *player =
							&g_pilot_data.network_players
								 [player_index];

						if (player->direct_play_id !=
							    0 &&
						    g_frontend_mission
								    .flight_groups
									    [player->flight_group_id]
								    .team ==
							    team_id) {
							break;
						}
					}
					if (player_index != PLAYER_COUNT) {
						/* The rating shown is that of
						 * the network player whose slot
						 * number equals the leading
						 * team's id, as in the
						 * original; the winner's name
						 * comes from player_index. */
						int leading_team_id =
							g_debrief_standings_team_ids
								[0];

						sprintf(g_frontend_scratch_buffer,
							"%c%s: %c%s %c%s",
							TEXT_CODE_WINNER,
							frontend_string_get(
								FRONTSTR_686_TOURNAMENT_WINNER),
							TEXT_CODE_RATING,
							frontend_string_get((
								frontend_string_id)(FRONTSTR_154_DRONE +
										    g_pilot_data
											    .network_players
												    [leading_team_id]
											    .rating)),
							TEXT_CODE_VALUE,
							g_pilot_data
								.network_players
									[player_index]
								.friendly_name);
					}
					if (player_index == PLAYER_COUNT) {
						sprintf(g_frontend_scratch_buffer,
							"%c%s: %c%s",
							TEXT_CODE_WINNER,
							frontend_string_get(
								FRONTSTR_686_TOURNAMENT_WINNER),
							TEXT_CODE_VALUE,
							g_frontend_mission
								.teams[team_id]
								.name);
						frontend_text_draw_centered(
							TITLE_FONT_SIZE,
							g_frontend_scratch_buffer,
							&rect, 0xFFFF);
						frontend_draw_rect_offset_xy(
							&rect, 0,
							WINNER_LINE_HEIGHT);
					} else {
						int color =
							team_id == g_pilot_data.team
								? g_pulse_color_ramp
									  [((frame_counter %
									     PULSE_PERIOD) &
									    ~1) >>
									   1]
								: g_color_yellow;
						frontend_text_draw_centered(
							TITLE_FONT_SIZE,
							g_frontend_scratch_buffer,
							&rect, color);
						frontend_draw_rect_offset_xy(
							&rect, 0,
							WINNER_LINE_HEIGHT);
						if (team_id ==
						    g_pilot_data.team) {
							int award_level =
								g_pilot_data
									.faction_statistics
										[g_pilot_data
											 .current_faction_id]
									.mission_awards
										[1];

							if (award_level != 0) {
								sprintf(g_frontend_scratch_buffer,
									"%s %s - %s",
									frontend_string_get(
										FRONTSTR_377_AWARD),
									frontend_string_get(
										FRONTSTR_379_TOURNAMENT_TROPHY),
									frontend_string_get(
										(frontend_string_id)(FRONTSTR_381_BATTLE_MEDALLION +
												     award_level)));
								int award_text_width =
									frontend_text_measure_width(
										g_frontend_scratch_buffer,
										TEXT_FONT_SIZE) +
									AWARD_GAP;
								sprintf(resource_name,
									"medlvl%d",
									g_pilot_data
										.faction_statistics
											[g_pilot_data
												 .current_faction_id]
										.mission_awards
											[1]);
								front_image_get_resource_rect(
									resource_name,
									&resource_rect);
								int award_x =
									rect.left +
									((rect.right -
									  rect.left) >>
									 1) -
									((award_text_width +
									  resource_rect
										  .right -
									  resource_rect
										  .left +
									  AWARD_LAYOUT_PADDING) >>
									 1);
								frontend_text_draw(
									TEXT_FONT_SIZE,
									g_frontend_scratch_buffer,
									award_x,
									rect.top,
									0xFFFF);
								front_image_draw_sprite(
									resource_name,
									award_x +
										award_text_width +
										AWARD_GAP,
									rect.top);
								frontend_draw_rect_offset_xy(
									&rect,
									0,
									LINE_HEIGHT);
							}
						}
					}
				} else {
					for (int flight_group_index = 0;
					     flight_group_index <
					     (int16_t)g_frontend_mission
						     .flight_group_count;
					     ++flight_group_index) {
						if (g_frontend_mission
								    .flight_groups
									    [flight_group_index]
								    .player_number !=
							    0 &&
						    g_frontend_mission
								    .flight_groups
									    [flight_group_index]
								    .team ==
							    team_id) {
							sprintf(g_frontend_scratch_buffer,
								"%c%s: %c%s %c%s",
								TEXT_CODE_WINNER,
								frontend_string_get(
									FRONTSTR_686_TOURNAMENT_WINNER),
								TEXT_CODE_RATING,
								frontend_string_get((
									frontend_string_id)(FRONTSTR_154_DRONE +
											    g_pilot_data
												    .flight_group_rating
													    [flight_group_index])),
								TEXT_CODE_VALUE,
								g_frontend_mission
									.flight_groups
										[flight_group_index]
									.name);
							frontend_text_draw_centered(
								TITLE_FONT_SIZE,
								g_frontend_scratch_buffer,
								&rect, 0xFFFF);
							frontend_draw_rect_offset_xy(
								&rect, 0,
								WINNER_LINE_HEIGHT);
						}
					}
				}

				if (g_debrief_standings_team_ids
						    [standing_index + 1] ==
					    -1 ||
				    g_pilot_data.melee_tournament_sequence_state
						    .team_standings
							    [g_debrief_standings_team_ids
								     [standing_index +
								      1]]
						    .total_score <
					    g_pilot_data
						    .melee_tournament_sequence_state
						    .team_standings[team_id]
						    .total_score) {
					break;
				}
			}
		} else {
			for (standing_index = 0;
			     standing_index < MAX_WINNER_ROWS;
			     ++standing_index) {
				int team_id = g_debrief_standings_team_ids
					[standing_index];

				if (team_id == -1) {
					break;
				}
				sprintf(g_frontend_scratch_buffer, "%c%s: %c%s",
					TEXT_CODE_WINNER,
					frontend_string_get(
						FRONTSTR_686_TOURNAMENT_WINNER),
					TEXT_CODE_VALUE,
					g_frontend_mission.teams[team_id].name);
				frontend_text_draw_centered(
					TITLE_FONT_SIZE,
					g_frontend_scratch_buffer, &rect,
					0xFFFF);
				frontend_draw_rect_offset_xy(
					&rect, 0, WINNER_LINE_HEIGHT);
				if (team_id == g_pilot_data.team) {
					int award_level =
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.mission_awards[1];

					if (award_level != 0) {
						sprintf(g_frontend_scratch_buffer,
							"%s %s - %s",
							frontend_string_get(
								FRONTSTR_377_AWARD),
							frontend_string_get(
								FRONTSTR_379_TOURNAMENT_TROPHY),
							frontend_string_get((
								frontend_string_id)(FRONTSTR_381_BATTLE_MEDALLION +
										    award_level)));
						int award_text_width =
							frontend_text_measure_width(
								g_frontend_scratch_buffer,
								TEXT_FONT_SIZE) +
							AWARD_GAP;
						sprintf(resource_name,
							"medlvl%d",
							g_pilot_data
								.faction_statistics
									[g_pilot_data
										 .current_faction_id]
								.mission_awards
									[1]);
						front_image_get_resource_rect(
							resource_name,
							&resource_rect);
						int award_x =
							rect.left +
							((rect.right -
							  rect.left) >>
							 1) -
							((award_text_width +
							  resource_rect.right -
							  resource_rect.left +
							  AWARD_LAYOUT_PADDING) >>
							 1);
						frontend_text_draw(
							TEXT_FONT_SIZE,
							g_frontend_scratch_buffer,
							award_x, rect.top,
							0xFFFF);
						front_image_draw_sprite(
							resource_name,
							award_x +
								award_text_width +
								AWARD_GAP,
							rect.top);
						frontend_draw_rect_offset_xy(
							&rect, 0, LINE_HEIGHT);
					}
				}

				if (g_debrief_standings_team_ids
						    [standing_index + 1] ==
					    -1 ||
				    g_pilot_data.melee_tournament_sequence_state
						    .team_standings
							    [g_debrief_standings_team_ids
								     [standing_index +
								      1]]
						    .total_score <
					    g_pilot_data
						    .melee_tournament_sequence_state
						    .team_standings[team_id]
						    .total_score) {
					break;
				}
			}
		}
	}

	frontend_draw_rect_offset_xy(&rect, 0, 8);
	frontend_text_draw_centered(
		TEXT_FONT_SIZE,
		frontend_string_get(FRONTSTR_338_TOURNAMENT_SCORE_TOTALS),
		&rect, g_color_yellow);
	frontend_draw_rect_offset_xy(&rect, 0, LINE_HEIGHT);
	sprintf(g_frontend_scratch_buffer, "%c%s %c%d%c %s %c%d%c %s",
		TEXT_CODE_LABEL, frontend_string_get(FRONTSTR_339_AFTER),
		TEXT_CODE_VALUE,
		g_pilot_data.melee_tournament_sequence_state
				.current_mission_index +
			1,
		TEXT_CODE_LABEL, frontend_string_get(FRONTSTR_335_OF),
		TEXT_CODE_VALUE,
		g_pilot_data.melee_tournament_sequence_state.mission_count,
		TEXT_CODE_LABEL, frontend_string_get(FRONTSTR_340_MISSIONS));
	frontend_text_draw_centered(TEXT_FONT_SIZE, g_frontend_scratch_buffer,
				    &rect, 0xFFFF);
	int text_y = rect.top + LINE_HEIGHT;
	frontend_text_draw(TEXT_FONT_SIZE,
			   frontend_string_get(FRONTSTR_328_TEAM), CONTENT_LEFT,
			   text_y, g_color_yellow);
	frontend_text_draw(TEXT_FONT_SIZE,
			   frontend_string_get(FRONTSTR_699_TOTAL_SCORE),
			   SCORE_COLUMN_X, text_y, g_color_yellow);
	frontend_text_draw(TEXT_FONT_SIZE,
			   frontend_string_get(FRONTSTR_318_1ST),
			   FIRST_PLACE_COLUMN_X, text_y, g_color_yellow);
	frontend_text_draw(TEXT_FONT_SIZE,
			   frontend_string_get(FRONTSTR_319_2ND),
			   SECOND_PLACE_COLUMN_X, text_y, g_color_yellow);
	frontend_text_draw(TEXT_FONT_SIZE,
			   frontend_string_get(FRONTSTR_320_3RD),
			   THIRD_PLACE_COLUMN_X, text_y, g_color_yellow);
	text_y += LINE_HEIGHT;

	int rank_start = 0;
	int standings_position = 0;
	int previous_score = g_pilot_data.teams[g_debrief_standings_team_ids[0]]
				     .mission_score;
	struct RECT player_name_clip_rect;
	struct RECT saved_clip_rect;
	for (standing_index = 0; standing_index < PLAYER_COUNT;
	     ++standing_index) {
		if (g_debrief_standings_team_ids[standing_index] == -1) {
			break;
		}
		/* g_debrief_team_in_standings is filled by team id but read
		 * here by standing position, as in the original. */
		if (g_debrief_team_in_standings[standing_index] != 0) {
			if (g_pilot_data.melee_tournament_sequence_state
				    .team_standings[g_debrief_standings_team_ids
							    [standing_index]]
				    .total_score != previous_score) {
				previous_score =
					g_pilot_data
						.melee_tournament_sequence_state
						.team_standings
							[g_debrief_standings_team_ids
								 [standing_index]]
						.total_score;
				rank_start = standings_position;
			}
			int placement_code = TEXT_CODE_WINNER;
			if (rank_start >= 3) {
				placement_code = TEXT_CODE_VALUE;
			}
			int text_x = CONTENT_LEFT;
			if (g_debrief_rank_by_pilot == 0) {
				sprintf(g_frontend_scratch_buffer, "%c%d. %c%s",
					placement_code, rank_start + 1,
					TEXT_CODE_VALUE,
					g_frontend_mission
						.teams[g_debrief_standings_team_ids
							       [standing_index]]
						.name);
				frontend_text_draw(TEXT_FONT_SIZE,
						   g_frontend_scratch_buffer,
						   CONTENT_LEFT, text_y,
						   0xFFFF);
				sprintf(g_frontend_scratch_buffer, "%d",
					g_pilot_data
						.melee_tournament_sequence_state
						.team_standings
							[g_debrief_standings_team_ids
								 [standing_index]]
						.total_score);
				frontend_text_draw(TEXT_FONT_SIZE,
						   g_frontend_scratch_buffer,
						   SCORE_COLUMN_X, text_y,
						   0xFFFF);
				if (g_pilot_data.melee_tournament_sequence_state
					    .team_standings
						    [g_debrief_standings_team_ids
							     [standing_index]]
					    .first_place_count == 0) {
					sprintf(g_frontend_scratch_buffer,
						"---");
				} else {
					sprintf(g_frontend_scratch_buffer, "%d",
						g_pilot_data
							.melee_tournament_sequence_state
							.team_standings
								[g_debrief_standings_team_ids
									 [standing_index]]
							.first_place_count);
				}
				frontend_text_draw(TEXT_FONT_SIZE,
						   g_frontend_scratch_buffer,
						   FIRST_PLACE_COLUMN_X, text_y,
						   0xFFFF);
				if (g_pilot_data.melee_tournament_sequence_state
					    .team_standings
						    [g_debrief_standings_team_ids
							     [standing_index]]
					    .second_place_count == 0) {
					sprintf(g_frontend_scratch_buffer,
						"---");
				} else {
					sprintf(g_frontend_scratch_buffer, "%d",
						g_pilot_data
							.melee_tournament_sequence_state
							.team_standings
								[g_debrief_standings_team_ids
									 [standing_index]]
							.second_place_count);
				}
				frontend_text_draw(TEXT_FONT_SIZE,
						   g_frontend_scratch_buffer,
						   SECOND_PLACE_COLUMN_X,
						   text_y, 0xFFFF);
				if (g_pilot_data.melee_tournament_sequence_state
					    .team_standings
						    [g_debrief_standings_team_ids
							     [standing_index]]
					    .third_place_count == 0) {
					sprintf(g_frontend_scratch_buffer,
						"---");
				} else {
					sprintf(g_frontend_scratch_buffer, "%d",
						g_pilot_data
							.melee_tournament_sequence_state
							.team_standings
								[g_debrief_standings_team_ids
									 [standing_index]]
							.third_place_count);
				}
				frontend_text_draw(TEXT_FONT_SIZE,
						   g_frontend_scratch_buffer,
						   THIRD_PLACE_COLUMN_X, text_y,
						   0xFFFF);
				text_y += LINE_HEIGHT;
				text_x = PLAYER_INDENT_LEFT;
			} else if (g_debrief_team_has_only_ai_pilots
					   [g_debrief_standings_team_ids
						    [standing_index]] != 0) {
				for (int flight_group_index = 0;
				     flight_group_index <
				     (int16_t)g_frontend_mission
					     .flight_group_count;
				     ++flight_group_index) {
					if (g_frontend_mission
							    .flight_groups
								    [flight_group_index]
							    .player_number !=
						    0 &&
					    g_frontend_mission
							    .flight_groups
								    [flight_group_index]
							    .team ==
						    g_debrief_standings_team_ids
							    [standing_index]) {
						sprintf(g_frontend_scratch_buffer,
							"%c%d. %c%s %c%s",
							placement_code,
							rank_start + 1,
							TEXT_CODE_RATING,
							frontend_string_get((
								frontend_string_id)(FRONTSTR_154_DRONE +
										    g_pilot_data
											    .flight_group_rating
												    [flight_group_index])),
							TEXT_CODE_VALUE,
							g_frontend_mission
								.flight_groups
									[flight_group_index]
								.name);
						frontend_text_draw(
							TEXT_FONT_SIZE,
							g_frontend_scratch_buffer,
							text_x, text_y, 0xFFFF);
						sprintf(g_frontend_scratch_buffer,
							"%d",
							g_pilot_data
								.melee_tournament_sequence_state
								.team_standings
									[g_debrief_standings_team_ids
										 [standing_index]]
								.total_score);
						text_x += SCORE_COLUMN_OFFSET;
						frontend_text_draw(
							TEXT_FONT_SIZE,
							g_frontend_scratch_buffer,
							text_x, text_y, 0xFFFF);
						text_x +=
							PLACEMENT_COLUMN_OFFSET -
							SCORE_COLUMN_OFFSET;
						if (g_pilot_data
							    .melee_tournament_sequence_state
							    .team_standings
								    [g_debrief_standings_team_ids
									     [standing_index]]
							    .first_place_count ==
						    0) {
							sprintf(g_frontend_scratch_buffer,
								"---");
						} else {
							sprintf(g_frontend_scratch_buffer,
								"%d",
								g_pilot_data
									.melee_tournament_sequence_state
									.team_standings
										[g_debrief_standings_team_ids
											 [standing_index]]
									.first_place_count);
						}
						frontend_text_draw(
							TEXT_FONT_SIZE,
							g_frontend_scratch_buffer,
							text_x, text_y, 0xFFFF);
						text_x +=
							PLACEMENT_COLUMN_SPACING;
						if (g_pilot_data
							    .melee_tournament_sequence_state
							    .team_standings
								    [g_debrief_standings_team_ids
									     [standing_index]]
							    .second_place_count ==
						    0) {
							sprintf(g_frontend_scratch_buffer,
								"---");
						} else {
							sprintf(g_frontend_scratch_buffer,
								"%d",
								g_pilot_data
									.melee_tournament_sequence_state
									.team_standings
										[g_debrief_standings_team_ids
											 [standing_index]]
									.second_place_count);
						}
						frontend_text_draw(
							TEXT_FONT_SIZE,
							g_frontend_scratch_buffer,
							text_x, text_y, 0xFFFF);
						text_x +=
							PLACEMENT_COLUMN_SPACING;
						if (g_pilot_data
							    .melee_tournament_sequence_state
							    .team_standings
								    [g_debrief_standings_team_ids
									     [standing_index]]
							    .third_place_count ==
						    0) {
							sprintf(g_frontend_scratch_buffer,
								"---");
						} else {
							sprintf(g_frontend_scratch_buffer,
								"%d",
								g_pilot_data
									.melee_tournament_sequence_state
									.team_standings
										[g_debrief_standings_team_ids
											 [standing_index]]
									.third_place_count);
						}
						frontend_text_draw(
							TEXT_FONT_SIZE,
							g_frontend_scratch_buffer,
							text_x, text_y, 0xFFFF);
						text_y += LINE_HEIGHT;
					}
				}
			}

			if (g_debrief_team_has_only_ai_pilots
				    [g_debrief_standings_team_ids
					     [standing_index]] == 0) {
				for (int sorted_player_index = 0;
				     sorted_player_index < PLAYER_COUNT;
				     ++sorted_player_index) {
					int player_index =
						g_debrief_sorted_player_ids
							[sorted_player_index];

					if (player_index == -1) {
						break;
					}
					if (g_frontend_mission
						    .flight_groups
							    [g_pilot_data
								     .network_players
									     [player_index]
								     .flight_group_id]
						    .team ==
					    g_debrief_standings_team_ids
						    [standing_index]) {
						int color;

						if (g_debrief_rank_by_pilot ==
						    0) {
							sprintf(g_frontend_scratch_buffer,
								"%c%s %c%s",
								TEXT_CODE_RATING,
								frontend_string_get((
									frontend_string_id)(FRONTSTR_154_DRONE +
											    g_pilot_data
												    .network_players
													    [player_index]
												    .rating)),
								TEXT_CODE_VALUE,
								g_pilot_data
									.network_players
										[player_index]
									.friendly_name);
							color = net_get_local_player_id() ==
											g_pilot_data
												.network_players
													[player_index]
												.direct_play_id ||
										g_frontend_mission_session_mode ==
											FRONTEND_MISSION_SESSION_SINGLEPLAYER
									? g_pulse_color_ramp
										  [((frame_counter %
										     PULSE_PERIOD) &
										    ~1) >>
										   1]
									: g_color_yellow;
						} else {
							sprintf(g_frontend_scratch_buffer,
								"%c%d. %c%s %c%s",
								placement_code,
								rank_start + 1,
								TEXT_CODE_RATING,
								frontend_string_get((
									frontend_string_id)(FRONTSTR_154_DRONE +
											    g_pilot_data
												    .network_players
													    [player_index]
												    .rating)),
								TEXT_CODE_VALUE,
								g_pilot_data
									.network_players
										[player_index]
									.friendly_name);
							frontend_draw_rect_assign(
								&player_name_clip_rect,
								text_x, text_y,
								text_x +
									NAME_COLUMN_WIDTH,
								text_y +
									LINE_HEIGHT);
							frontend_display_get_screen_clip_rect(
								&saved_clip_rect);
							frontend_display_set_screen_clip_rect640x480(
								&player_name_clip_rect);
							color = net_get_local_player_id() ==
											g_pilot_data
												.network_players
													[player_index]
												.direct_play_id ||
										g_frontend_mission_session_mode ==
											FRONTEND_MISSION_SESSION_SINGLEPLAYER
									? g_pulse_color_ramp
										  [((frame_counter %
										     PULSE_PERIOD) &
										    ~1) >>
										   1]
									: g_color_yellow;
							frontend_text_draw(
								TEXT_FONT_SIZE,
								g_frontend_scratch_buffer,
								text_x, text_y,
								color);
							frontend_display_set_screen_clip_rect640x480(
								&saved_clip_rect);
							sprintf(g_frontend_scratch_buffer,
								"%d",
								g_pilot_data
									.melee_tournament_sequence_state
									.team_standings
										[g_debrief_standings_team_ids
											 [standing_index]]
									.total_score);
							text_x +=
								SCORE_COLUMN_OFFSET;
							frontend_text_draw(
								TEXT_FONT_SIZE,
								g_frontend_scratch_buffer,
								text_x, text_y,
								0xFFFF);
							text_x +=
								PLACEMENT_COLUMN_OFFSET -
								SCORE_COLUMN_OFFSET;
							if (g_pilot_data
								    .melee_tournament_sequence_state
								    .team_standings
									    [g_debrief_standings_team_ids
										     [standing_index]]
								    .first_place_count ==
							    0) {
								sprintf(g_frontend_scratch_buffer,
									"---");
							} else {
								sprintf(g_frontend_scratch_buffer,
									"%d",
									g_pilot_data
										.melee_tournament_sequence_state
										.team_standings
											[g_debrief_standings_team_ids
												 [standing_index]]
										.first_place_count);
							}
							frontend_text_draw(
								TEXT_FONT_SIZE,
								g_frontend_scratch_buffer,
								text_x, text_y,
								0xFFFF);
							text_x +=
								PLACEMENT_COLUMN_SPACING;
							if (g_pilot_data
								    .melee_tournament_sequence_state
								    .team_standings
									    [g_debrief_standings_team_ids
										     [standing_index]]
								    .second_place_count ==
							    0) {
								sprintf(g_frontend_scratch_buffer,
									"---");
							} else {
								sprintf(g_frontend_scratch_buffer,
									"%d",
									g_pilot_data
										.melee_tournament_sequence_state
										.team_standings
											[g_debrief_standings_team_ids
												 [standing_index]]
										.second_place_count);
							}
							frontend_text_draw(
								TEXT_FONT_SIZE,
								g_frontend_scratch_buffer,
								text_x, text_y,
								0xFFFF);
							text_x +=
								PLACEMENT_COLUMN_SPACING;
							if (g_pilot_data
								    .melee_tournament_sequence_state
								    .team_standings
									    [g_debrief_standings_team_ids
										     [standing_index]]
								    .third_place_count ==
							    0) {
								sprintf(g_frontend_scratch_buffer,
									"---");
							} else {
								sprintf(g_frontend_scratch_buffer,
									"%d",
									g_pilot_data
										.melee_tournament_sequence_state
										.team_standings
											[g_debrief_standings_team_ids
												 [standing_index]]
										.third_place_count);
							}
							color = 0xFFFF;
						}
						frontend_text_draw(
							TEXT_FONT_SIZE,
							g_frontend_scratch_buffer,
							text_x, text_y, color);
						text_y += LINE_HEIGHT;
						text_x =
							g_debrief_rank_by_pilot ==
									0
								? PLAYER_INDENT_LEFT
								: CONTENT_LEFT;
					}
				}
			}

			/* g_debrief_team_has_player is filled by team id but
			 * read here by standing position, as in the
			 * original. */
			if (g_debrief_team_has_player[standing_index] != 0 &&
			    g_debrief_rank_by_pilot == 0) {
				for (int flight_group_index = 0;
				     flight_group_index <
				     (int16_t)g_frontend_mission
					     .flight_group_count;
				     ++flight_group_index) {
					if (g_frontend_mission
							    .flight_groups
								    [flight_group_index]
							    .player_number !=
						    0 &&
					    g_frontend_mission
							    .flight_groups
								    [flight_group_index]
							    .team ==
						    g_debrief_standings_team_ids
							    [standing_index]) {
						int player_index;

						for (player_index = 0;
						     player_index <
						     PLAYER_COUNT;
						     ++player_index) {
							if (g_pilot_data.network_players
									    [player_index]
										    .flight_group_id ==
								    flight_group_index &&
							    g_pilot_data.network_players
									    [player_index]
										    .direct_play_id !=
								    0) {
								break;
							}
						}
						if (player_index ==
						    PLAYER_COUNT) {
							sprintf(g_frontend_scratch_buffer,
								"%c%s %c%s",
								TEXT_CODE_RATING,
								frontend_string_get((
									frontend_string_id)(FRONTSTR_154_DRONE +
											    g_pilot_data
												    .flight_group_rating
													    [flight_group_index])),
								TEXT_CODE_VALUE,
								g_frontend_mission
									.flight_groups
										[flight_group_index]
									.name);
							frontend_text_draw(
								TEXT_FONT_SIZE,
								g_frontend_scratch_buffer,
								text_x, text_y,
								0xFFFF);
							text_y += LINE_HEIGHT;
						}
					}
				}
			}
		}
		++standings_position;
	}
	return 1;
}

/* Draws the debriefing's tabs and switches g_debrief_tab when one is clicked,
 * also setting g_debrief_stats_page_needs_rebuild. Tab 1, player statistics, is
 * always there; tab 0, the overview, in network play or a melee or
 * tournament; tab 2 while g_pilot_data.mission_sequence_active is 1 in a
 * campaign (training directory), tournament (melee directory) or battle
 * (combat engagement directory). Returns 1. */
// FUNCTION: XVT 0x504360
int mission_debrief_draw_tab_bar(void)
{
	frontend_navigation_slot_state slot_states[8];

	slot_states[0] =
		g_frontend_mission_session_mode !=
			FRONTEND_MISSION_SESSION_SINGLEPLAYER ||
		g_pilot_data.mission_directory_id == MISSION_DIRECTORY_MELEES ||
		g_pilot_data.mission_directory_id ==
			MISSION_DIRECTORY_TOURNAMENTS;
	slot_states[1] = FRONTEND_NAVIGATION_SLOT_ACTIVE;
	slot_states[2] = g_pilot_data.mission_sequence_active == 1 &&
			 (g_pilot_data.mission_directory_id ==
				  MISSION_DIRECTORY_TRAINING_EXERCISES ||
			  g_pilot_data.mission_directory_id ==
				  MISSION_DIRECTORY_MELEES ||
			  g_pilot_data.mission_directory_id ==
				  MISSION_DIRECTORY_COMBAT_ENGAGEMENTS);
	slot_states[3] = FRONTEND_NAVIGATION_SLOT_INACTIVE;
	slot_states[4] = FRONTEND_NAVIGATION_SLOT_INACTIVE;
	slot_states[5] = FRONTEND_NAVIGATION_SLOT_INACTIVE;
	slot_states[6] = FRONTEND_NAVIGATION_SLOT_INACTIVE;
	slot_states[7] = FRONTEND_NAVIGATION_SLOT_INACTIVE;
	slot_states[g_debrief_tab] = FRONTEND_NAVIGATION_SLOT_SELECTED;
	frontend_button_draw_eight_slot_navigation_state(slot_states);

	struct RECT rect;
	frontend_draw_rect_assign(&rect, 22, 170, 42, 194);
	if (g_debrief_tab == 2) {
		if (g_pilot_data.mission_directory_id ==
		    MISSION_DIRECTORY_TRAINING_EXERCISES) {
			frontend_button_draw_sprite_and_tooltip(
				&rect, "deb4d",
				frontend_string_get(
					FRONTSTR_822_MISSION_DEBRIEFING),
				12, 0);
		} else if (g_pilot_data.mission_directory_id ==
			   MISSION_DIRECTORY_MELEES) {
			frontend_button_draw_sprite_and_tooltip(
				&rect, "debrief4d",
				frontend_string_get(
					FRONTSTR_315_TOURNAMENT_SUMMARY),
				12, 0);
		} else if (g_pilot_data.mission_directory_id ==
			   MISSION_DIRECTORY_COMBAT_ENGAGEMENTS) {
			frontend_button_draw_sprite_and_tooltip(
				&rect, "debrief4d",
				frontend_string_get(
					FRONTSTR_314_BATTLE_SUMMARY),
				12, 0);
		}
	} else if (g_pilot_data.mission_sequence_active == 1) {
		if (g_pilot_data.mission_directory_id ==
		    MISSION_DIRECTORY_TRAINING_EXERCISES) {
			if (frontend_button_handle_sprite_button(
				    &rect, "deb4u", "deb4u",
				    frontend_string_get(
					    FRONTSTR_822_MISSION_DEBRIEFING),
				    12, 0, 14, "jewelsound")) {
				XVT_LOG_DEBUG(
					"debrief.page_changed page=2 previous=%d",
					g_debrief_tab);
				g_debrief_stats_page_needs_rebuild = 1;
				g_debrief_tab = 2;
			}
		} else if (g_pilot_data.mission_directory_id ==
			   MISSION_DIRECTORY_MELEES) {
			if (frontend_button_handle_sprite_button(
				    &rect, "debrief4u", "debrief4u",
				    frontend_string_get(
					    FRONTSTR_315_TOURNAMENT_SUMMARY),
				    12, 0, 14, "jewelsound")) {
				XVT_LOG_DEBUG(
					"debrief.page_changed page=2 previous=%d",
					g_debrief_tab);
				g_debrief_stats_page_needs_rebuild = 1;
				g_debrief_tab = 2;
			}
		} else if (g_pilot_data.mission_directory_id ==
				   MISSION_DIRECTORY_COMBAT_ENGAGEMENTS &&
			   frontend_button_handle_sprite_button(
				   &rect, "debrief4u", "debrief4u",
				   frontend_string_get(
					   FRONTSTR_314_BATTLE_SUMMARY),
				   12, 0, 14, "jewelsound")) {
			XVT_LOG_DEBUG("debrief.page_changed page=2 previous=%d",
				      g_debrief_tab);
			g_debrief_stats_page_needs_rebuild = 1;
			g_debrief_tab = 2;
		}
	}

	frontend_draw_rect_offset_xy(&rect, 0, -28);
	if (g_debrief_tab == 1) {
		frontend_button_draw_sprite_and_tooltip(
			&rect, "debrief2d",
			frontend_string_get(FRONTSTR_312_PLAYER_STATISTICS), 12,
			0);
	} else if (frontend_button_handle_sprite_button(
			   &rect, "debrief2u", "debrief2u",
			   frontend_string_get(FRONTSTR_312_PLAYER_STATISTICS),
			   12, 0, 12, "jewelsound")) {
		XVT_LOG_DEBUG("debrief.page_changed page=1 previous=%d",
			      g_debrief_tab);
		g_debrief_stats_page_needs_rebuild = 1;
		g_debrief_tab = 1;
	}

	frontend_draw_rect_offset_xy(&rect, 0, -28);
	if (slot_states[0] != FRONTEND_NAVIGATION_SLOT_INACTIVE) {
		if (g_debrief_tab == 0) {
			frontend_button_draw_sprite_and_tooltip(
				&rect, "debrief1d",
				frontend_string_get(
					FRONTSTR_311_MISSION_OVERVIEW),
				12, 0);
		} else if (frontend_button_handle_sprite_button(
				   &rect, "debrief1u", "debrief1u",
				   frontend_string_get(
					   FRONTSTR_311_MISSION_OVERVIEW),
				   12, 0, 11, "jewelsound")) {
			XVT_LOG_DEBUG("debrief.page_changed page=0 previous=%d",
				      g_debrief_tab);
			g_debrief_stats_page_needs_rebuild = 1;
			g_debrief_tab = 0;
		}
	}
	return 1;
}

/* Marks every network player of g_pilot_data.network_players with a
 * direct_play_id ready in the session roster (net_mark_player_ready_no_lock).
 * Returns 1. */
// FUNCTION: XVT 0x5046D0
int mission_debrief_mark_network_players_ready(void)
{
	for (int player_index = 0; player_index < 8; ++player_index) {
		if (g_pilot_data.network_players[player_index].direct_play_id !=
		    0) {
			XVT_LOG_DEBUG(
				"debrief.player_readied entry=%d player=%u has_left=%d",
				player_index,
				(unsigned)g_pilot_data
					.network_players[player_index]
					.direct_play_id,
				g_pilot_data.network_players[player_index]
					.has_left);
			net_mark_player_ready_no_lock(
				g_pilot_data.network_players[player_index]
					.direct_play_id);
		}
	}
	return 1;
}

/* Works out the debriefing's standings and lists from g_pilot_data and
 * g_frontend_mission: g_debrief_team_has_player, g_debrief_team_has_only_ai_pilots,
 * g_debrief_active_team_count, g_debrief_team_in_standings,
 * g_debrief_sorted_team_ids, g_debrief_local_team_rank_index,
 * g_debrief_standings_team_ids, g_debrief_sorted_player_ids,
 * g_debrief_kills_on_combatant_ids, g_debrief_kills_from_combatant_ids and
 * g_debrief_rank_by_pilot; each global's comment says how. Returns 1. Does not
 * check a network player's flight_group_id before indexing the flight
 * groups. */
// FUNCTION: XVT 0x504700
int mission_debrief_prepare(void)
{
	memset(g_debrief_team_has_player, 0, sizeof(g_debrief_team_has_player));
	memset(g_debrief_team_in_standings, 0,
	       sizeof(g_debrief_team_in_standings));
	memset(g_debrief_team_has_only_ai_pilots, 0,
	       sizeof(g_debrief_team_has_only_ai_pilots));
	int active_team_count = 0;
	int human_player_count = 0;
	g_debrief_local_team_rank_index = active_team_count;
	/* team_index walks three kinds of slot in this function: network
	 * players (network_players), flight groups (flight_groups, ranked as
	 * team_index + 8), and teams. */
	unsigned int team_index;
	for (team_index = 0; team_index < 8; ++team_index) {
		if (g_pilot_data.network_players[team_index].direct_play_id !=
		    0) {
			int team = g_frontend_mission
					   .flight_groups
						   [g_pilot_data
							    .network_players
								    [team_index]
							    .flight_group_id]
					   .team;
			++human_player_count;
			if (g_debrief_team_has_player[team] == 0) {
				++active_team_count;
			}
			g_debrief_team_has_player[team] = 1;
			XVT_LOG_DEBUG(
				"debrief.player_counted entry=%d player=%u fg=%d team=%d rating=%d score=%d kills=%d shared=%d assists=%d losses=%d has_left=%d teams=%d humans=%d",
				(int)team_index,
				(unsigned)g_pilot_data
					.network_players[team_index]
					.direct_play_id,
				g_pilot_data.network_players[team_index]
					.flight_group_id,
				team,
				g_pilot_data.network_players[team_index].rating,
				g_pilot_data.network_players[team_index]
					.total_score,
				g_pilot_data.network_players[team_index].kills,
				g_pilot_data.network_players[team_index]
					.kills_shared,
				g_pilot_data.network_players[team_index]
					.kills_assist,
				g_pilot_data.network_players[team_index]
					.total_losses,
				g_pilot_data.network_players[team_index]
					.has_left,
				active_team_count, human_player_count);
		}
	}
	g_debrief_active_team_count = active_team_count;
	if (g_pilot_data.mission_directory_id == MISSION_DIRECTORY_MELEES ||
	    g_pilot_data.mission_directory_id ==
		    MISSION_DIRECTORY_TOURNAMENTS) {
		if (g_game_config.ai_opponents != 0 ||
		    g_frontend_mission_session_mode ==
			    FRONTEND_MISSION_SESSION_SINGLEPLAYER ||
		    human_player_count == 1) {
			int flight_groups_remaining =
				(short)g_frontend_mission.flight_group_count;

			for (team_index = 0; flight_groups_remaining != 0;
			     ++team_index, --flight_groups_remaining) {
				if (g_frontend_mission.flight_groups[team_index]
					    .player_number != 0) {
					int team = g_frontend_mission
							   .flight_groups
								   [team_index]
							   .team;
					if (g_debrief_team_has_player[team] ==
					    0) {
						++active_team_count;
						g_debrief_team_has_only_ai_pilots
							[team] = 1;
						XVT_LOG_DEBUG(
							"debrief.ai_team_counted fg=%d team=%d teams=%d",
							(int)team_index, team,
							active_team_count);
					}
					g_debrief_team_has_player[team] = 1;
				}
				g_debrief_active_team_count = active_team_count;
			}
		}
		for (team_index = 0; team_index < 8; ++team_index) {
			g_debrief_team_in_standings[team_index] =
				g_pilot_data.melee_tournament_sequence_state
					.team_standings[team_index]
					.ai_opponent_source_team_and_type_flag !=
				-1;
		}
	}

	memset(g_debrief_standings_team_ids, 0xFF,
	       sizeof(g_debrief_standings_team_ids));
	memset(g_debrief_sorted_team_ids, 0xFF,
	       sizeof(g_debrief_sorted_team_ids));
	memset(g_debrief_kills_from_combatant_ids, 0xFF,
	       sizeof(g_debrief_kills_from_combatant_ids));
	memset(g_debrief_kills_on_combatant_ids, 0xFF,
	       sizeof(g_debrief_kills_on_combatant_ids));
	memset(g_debrief_sorted_player_ids, 0xFF,
	       sizeof(g_debrief_sorted_player_ids));

	int sort_index;
	int existing;
	int candidate;
	for (team_index = 0; team_index < 10; ++team_index) {
		candidate = team_index;
		if (g_debrief_team_has_player[team_index] == 0) {
			continue;
		}
		XVT_LOG_DEBUG(
			"debrief.team_result team=%d completed=%d score=%d kills=%d shared=%d losses=%d seconds=%d ai_only=%d in_standings=%d",
			(int)team_index,
			g_pilot_data.teams[team_index].is_mission_completed,
			g_pilot_data.teams[team_index].mission_score,
			g_pilot_data.teams[team_index].kills,
			g_pilot_data.teams[team_index].kills_shared,
			g_pilot_data.teams[team_index].losses,
			g_pilot_data.teams[team_index].mission_time,
			g_debrief_team_has_only_ai_pilots[team_index],
			g_debrief_team_in_standings[team_index]);
		int inserted = 0;
		for (sort_index = 0; sort_index < 10; ++sort_index) {
			existing = g_debrief_sorted_team_ids[sort_index];
			if (existing == -1) {
				g_debrief_sorted_team_ids[sort_index] =
					candidate;
				inserted = 1;
				break;
			}
			if (g_pilot_data.mission_directory_id ==
				    MISSION_DIRECTORY_MELEES ||
			    g_pilot_data.mission_directory_id ==
				    MISSION_DIRECTORY_TOURNAMENTS) {
				if (!(g_pilot_data.teams[candidate]
					      .mission_score <=
				      g_pilot_data.teams[existing]
					      .mission_score)) {
					g_debrief_sorted_team_ids[sort_index] =
						candidate;
					candidate = existing;
				}
			} else {
				if ((g_pilot_data.teams[existing]
						     .is_mission_completed ==
					     0 &&
				     g_pilot_data.teams[candidate]
						     .is_mission_completed ==
					     1) ||
				    !(g_pilot_data.teams[candidate]
					      .mission_score <=
				      g_pilot_data.teams[existing]
					      .mission_score)) {
					g_debrief_sorted_team_ids[sort_index] =
						candidate;
					candidate = existing;
				}
			}
		}
		(void)inserted;
	}
	for (sort_index = 0; sort_index < 10; ++sort_index) {
		/* g_debrief_team_has_player is filled by team id but read here
		 * by sorted position, as in the original. */
		if (g_debrief_team_has_player[sort_index] != 0 &&
		    g_debrief_sorted_team_ids[sort_index] ==
			    g_pilot_data.team) {
			g_debrief_local_team_rank_index = sort_index;
			break;
		}
	}
	if (sort_index == 10 &&
	    g_frontend_mission_session_mode !=
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
	    g_pilot_data.mission_directory_id == MISSION_DIRECTORY_MELEES) {
		XVT_LOG_WARN("debrief.local_place_missing team=%d teams=%d",
			     g_pilot_data.team, active_team_count);
	}

	if (g_pilot_data.mission_directory_id == MISSION_DIRECTORY_MELEES ||
	    g_pilot_data.mission_directory_id ==
		    MISSION_DIRECTORY_TOURNAMENTS) {
		for (team_index = 0; team_index < 10; ++team_index) {
			candidate = team_index;
			if (g_debrief_team_in_standings[team_index] == 0) {
				continue;
			}
			XVT_LOG_DEBUG(
				"debrief.standing_counted team=%d total=%d place1=%d place2=%d place3=%d ai_source=%d",
				(int)team_index,
				g_pilot_data.melee_tournament_sequence_state
					.team_standings[team_index]
					.total_score,
				g_pilot_data.melee_tournament_sequence_state
					.team_standings[team_index]
					.first_place_count,
				g_pilot_data.melee_tournament_sequence_state
					.team_standings[team_index]
					.second_place_count,
				g_pilot_data.melee_tournament_sequence_state
					.team_standings[team_index]
					.third_place_count,
				g_pilot_data.melee_tournament_sequence_state
					.team_standings[team_index]
					.ai_opponent_source_team_and_type_flag);
			for (sort_index = 0; sort_index < 8; ++sort_index) {
				existing = g_debrief_standings_team_ids
					[sort_index];
				if (existing == -1) {
					g_debrief_standings_team_ids
						[sort_index] = candidate;
					break;
				}
				if (!(g_pilot_data
					      .melee_tournament_sequence_state
					      .team_standings[candidate]
					      .total_score <=
				      g_pilot_data
					      .melee_tournament_sequence_state
					      .team_standings[existing]
					      .total_score)) {
					g_debrief_standings_team_ids
						[sort_index] = candidate;
					candidate = existing;
				}
			}
		}
	}

	for (team_index = 0; team_index < 8; ++team_index) {
		candidate = team_index;
		if (g_pilot_data.network_players[team_index].direct_play_id ==
		    0) {
			continue;
		}
		for (sort_index = 0; sort_index < 8; ++sort_index) {
			existing = g_debrief_sorted_player_ids[sort_index];
			if (existing == -1) {
				g_debrief_sorted_player_ids[sort_index] =
					candidate;
				break;
			}
			{
				int candidate_completed =
					g_pilot_data
						.teams[g_frontend_mission
							       .flight_groups
								       [g_pilot_data
										.network_players
											[candidate]
										.flight_group_id]
							       .team]
						.is_mission_completed;
				if ((candidate_completed == 1 &&
				     g_pilot_data.teams
						     [g_frontend_mission
							      .flight_groups
								      [g_pilot_data
									       .network_players
										       [existing]
									       .flight_group_id]
							      .team]
							     .is_mission_completed ==
					     0) ||
				    ((candidate_completed != 0 ||
				      g_pilot_data.teams
						      [g_frontend_mission
							       .flight_groups
								       [g_pilot_data
										.network_players
											[existing]
										.flight_group_id]
							       .team]
							      .is_mission_completed !=
					      1) &&
				     g_pilot_data.network_players[candidate]
						     .total_score >
					     g_pilot_data
						     .network_players[existing]
						     .total_score)) {
					g_debrief_sorted_player_ids
						[sort_index] = candidate;
					candidate = existing;
				}
			}
		}
	}
	for (team_index = 0; team_index < 8; ++team_index) {
		candidate = team_index;
		if (g_pilot_data.network_players[team_index].direct_play_id ==
		    0) {
			continue;
		}
		for (sort_index = 0; sort_index < 8; ++sort_index) {
			existing = g_debrief_kills_on_combatant_ids[sort_index];
			if (existing == -1) {
				g_debrief_kills_on_combatant_ids[sort_index] =
					candidate;
				break;
			}
			if (!((unsigned int)g_pilot_data
				      .kills_full_on_player[candidate] <=
			      (unsigned int)g_pilot_data
				      .kills_full_on_player[existing])) {
				g_debrief_kills_on_combatant_ids[sort_index] =
					candidate;
				candidate = existing;
			}
		}
	}
	if (g_pilot_data.mission_directory_id == MISSION_DIRECTORY_MELEES ||
	    g_pilot_data.mission_directory_id ==
		    MISSION_DIRECTORY_TOURNAMENTS) {
		unsigned int flight_group_count =
			(short)g_frontend_mission.flight_group_count;

		for (team_index = 0; team_index < flight_group_count;
		     ++team_index) {
			unsigned int combatant_id = team_index + 8;
			if (g_frontend_mission.flight_groups[team_index]
				    .player_number == 0) {
				continue;
			}
			int player_index;
			for (player_index = 0; player_index < 8;
			     ++player_index) {
				if (g_pilot_data.network_players[player_index]
						    .direct_play_id != 0 &&
				    (unsigned int)g_pilot_data
						    .network_players
							    [player_index]
						    .flight_group_id ==
					    team_index) {
					break;
				}
			}
			if (player_index != 8) {
				continue;
			}
			for (sort_index = 0; sort_index < 8; ++sort_index) {
				existing = g_debrief_kills_on_combatant_ids
					[sort_index];
				if (existing == -1) {
					g_debrief_kills_on_combatant_ids
						[sort_index] = combatant_id;
					break;
				}
				{
					unsigned int candidate_full;
					unsigned int candidate_shared;

					if (combatant_id < 8) {
						candidate_full =
							g_pilot_data.kills_full_on_player
								[combatant_id];
						candidate_shared =
							g_pilot_data.kills_shared_on_player
								[combatant_id];
					} else {
						candidate_full =
							g_pilot_data.kills_full_on_flight_group
								[combatant_id -
								 8];
						candidate_shared =
							g_pilot_data.kills_shared_on_flight_group
								[combatant_id -
								 8];
					}
					unsigned int existing_full;
					unsigned int existing_shared;
					if (existing < 8) {
						existing_full =
							g_pilot_data.kills_full_on_player
								[existing];
						existing_shared =
							g_pilot_data.kills_shared_on_player
								[existing];
					} else {
						existing_full =
							g_pilot_data.kills_full_on_flight_group
								[existing - 8];
						existing_shared =
							g_pilot_data.kills_shared_on_flight_group
								[existing - 8];
					}
					if (existing_full < candidate_full ||
					    (existing_full == candidate_full &&
					     existing_shared <
						     candidate_shared)) {
						g_debrief_kills_on_combatant_ids
							[sort_index] =
								combatant_id;
						combatant_id = existing;
					}
				}
			}
			if (sort_index == 8) {
				XVT_LOG_DEBUG(
					"debrief.kill_list_full list=\"killed\" fg=%d combatant=%d",
					(int)team_index, (int)combatant_id);
			}
		}
	}
	for (team_index = 0; team_index < 8; ++team_index) {
		candidate = team_index;
		if (g_pilot_data.network_players[team_index].direct_play_id ==
		    0) {
			continue;
		}
		for (sort_index = 0; sort_index < 8; ++sort_index) {
			existing =
				g_debrief_kills_from_combatant_ids[sort_index];
			if (existing == -1) {
				g_debrief_kills_from_combatant_ids[sort_index] =
					candidate;
				break;
			}
			if (!((unsigned int)g_pilot_data
				      .kills_full_from_player[candidate] <=
			      (unsigned int)g_pilot_data
				      .kills_full_from_player[existing])) {
				g_debrief_kills_from_combatant_ids[sort_index] =
					candidate;
				candidate = existing;
			}
		}
	}

	if (g_pilot_data.mission_directory_id == MISSION_DIRECTORY_MELEES ||
	    g_pilot_data.mission_directory_id ==
		    MISSION_DIRECTORY_TOURNAMENTS) {
		unsigned int flight_group_count =
			(short)g_frontend_mission.flight_group_count;

		for (team_index = 0; team_index < flight_group_count;
		     ++team_index) {
			unsigned int combatant_id = team_index + 8;
			if (g_frontend_mission.flight_groups[team_index]
				    .player_number == 0) {
				continue;
			}
			int player_index;
			for (player_index = 0; player_index < 8;
			     ++player_index) {
				if (g_pilot_data.network_players[player_index]
						    .direct_play_id != 0 &&
				    (unsigned int)g_pilot_data
						    .network_players
							    [player_index]
						    .flight_group_id ==
					    team_index) {
					break;
				}
			}
			if (player_index != 8) {
				continue;
			}
			for (sort_index = 0; sort_index < 8; ++sort_index) {
				existing = g_debrief_kills_from_combatant_ids
					[sort_index];
				if (existing == -1) {
					g_debrief_kills_from_combatant_ids
						[sort_index] = combatant_id;
					break;
				}
				{
					unsigned int candidate_full;
					unsigned int candidate_shared;

					if (combatant_id < 8) {
						candidate_full =
							g_pilot_data.kills_full_from_player
								[combatant_id];
						candidate_shared =
							g_pilot_data.kills_shared_from_player
								[combatant_id];
					} else {
						candidate_full =
							g_pilot_data.kills_full_from_flight_group
								[combatant_id -
								 8];
						candidate_shared =
							g_pilot_data.kills_shared_from_flight_group
								[combatant_id -
								 8];
					}
					unsigned int existing_full;
					unsigned int existing_shared;
					if (existing < 8) {
						existing_full =
							g_pilot_data.kills_full_from_player
								[existing];
						existing_shared =
							g_pilot_data.kills_shared_from_player
								[existing];
					} else {
						existing_full =
							g_pilot_data.kills_full_from_flight_group
								[existing - 8];
						existing_shared =
							g_pilot_data.kills_shared_from_flight_group
								[existing - 8];
					}
					if (existing_full < candidate_full ||
					    (existing_full == candidate_full &&
					     existing_shared <
						     candidate_shared)) {
						g_debrief_kills_from_combatant_ids
							[sort_index] =
								combatant_id;
						combatant_id = existing;
					}
				}
			}
			if (sort_index == 8) {
				XVT_LOG_DEBUG(
					"debrief.kill_list_full list=\"killed_by\" fg=%d combatant=%d",
					(int)team_index, (int)combatant_id);
			}
		}
	}

	g_debrief_rank_by_pilot = 0;
	if (g_pilot_data.mission_directory_id == MISSION_DIRECTORY_MELEES) {
		for (team_index = 0; team_index < (unsigned int)g_team_count;
		     ++team_index) {
			if (g_team_player_flight_group_count[team_index] > 1) {
				break;
			}
		}
		if (team_index == (unsigned int)g_team_count) {
			g_debrief_rank_by_pilot = 1;
		}
	}
	XVT_LOG_DEBUG(
		"debrief.order team_order=\"%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\" standings_order=\"%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\" player_order=\"%d,%d,%d,%d,%d,%d,%d,%d\" standing_flags=\"%d,%d,%d,%d,%d,%d,%d,%d\" position=%d by_pilot=%d ai_opponents=%d mode=%d groups=%d",
		g_debrief_sorted_team_ids[0], g_debrief_sorted_team_ids[1],
		g_debrief_sorted_team_ids[2], g_debrief_sorted_team_ids[3],
		g_debrief_sorted_team_ids[4], g_debrief_sorted_team_ids[5],
		g_debrief_sorted_team_ids[6], g_debrief_sorted_team_ids[7],
		g_debrief_sorted_team_ids[8], g_debrief_sorted_team_ids[9],
		g_debrief_standings_team_ids[0],
		g_debrief_standings_team_ids[1],
		g_debrief_standings_team_ids[2],
		g_debrief_standings_team_ids[3],
		g_debrief_standings_team_ids[4],
		g_debrief_standings_team_ids[5],
		g_debrief_standings_team_ids[6],
		g_debrief_standings_team_ids[7],
		g_debrief_standings_team_ids[8],
		g_debrief_standings_team_ids[9], g_debrief_sorted_player_ids[0],
		g_debrief_sorted_player_ids[1], g_debrief_sorted_player_ids[2],
		g_debrief_sorted_player_ids[3], g_debrief_sorted_player_ids[4],
		g_debrief_sorted_player_ids[5], g_debrief_sorted_player_ids[6],
		g_debrief_sorted_player_ids[7], g_debrief_team_in_standings[0],
		g_debrief_team_in_standings[1], g_debrief_team_in_standings[2],
		g_debrief_team_in_standings[3], g_debrief_team_in_standings[4],
		g_debrief_team_in_standings[5], g_debrief_team_in_standings[6],
		g_debrief_team_in_standings[7], g_debrief_local_team_rank_index,
		g_debrief_rank_by_pilot, (int)g_game_config.ai_opponents,
		(int)g_frontend_mission_session_mode,
		(int)(int16_t)g_frontend_mission.flight_group_count);
	XVT_LOG_DEBUG(
		"debrief.kill_order killed=\"%d,%d,%d,%d,%d,%d,%d,%d\" killed_by=\"%d,%d,%d,%d,%d,%d,%d,%d\"",
		g_debrief_kills_on_combatant_ids[0],
		g_debrief_kills_on_combatant_ids[1],
		g_debrief_kills_on_combatant_ids[2],
		g_debrief_kills_on_combatant_ids[3],
		g_debrief_kills_on_combatant_ids[4],
		g_debrief_kills_on_combatant_ids[5],
		g_debrief_kills_on_combatant_ids[6],
		g_debrief_kills_on_combatant_ids[7],
		g_debrief_kills_from_combatant_ids[0],
		g_debrief_kills_from_combatant_ids[1],
		g_debrief_kills_from_combatant_ids[2],
		g_debrief_kills_from_combatant_ids[3],
		g_debrief_kills_from_combatant_ids[4],
		g_debrief_kills_from_combatant_ids[5],
		g_debrief_kills_from_combatant_ids[6],
		g_debrief_kills_from_combatant_ids[7]);
	XVT_LOG_INFO(
		"debrief.results directory=%d teams=%d humans=%d top=%d team=%d completed=%d score=%d",
		(int)g_pilot_data.mission_directory_id, active_team_count,
		human_player_count, g_debrief_sorted_team_ids[0],
		g_pilot_data.team,
		g_pilot_data.teams[g_pilot_data.team].is_mission_completed,
		g_pilot_data.teams[g_pilot_data.team].mission_score);
	XVT_LOG_INFO(
		"debrief.pilot_result score=%d rank_change=%d rating=%d faction=%d plaque=%d trophy=%d evaluation=%d medallion=%d",
		g_pilot_data.mission_score, (int)g_pilot_data.promotion_delta,
		(int)g_pilot_data.rating, g_pilot_data.current_faction_id,
		g_pilot_data.faction_statistics[g_pilot_data.current_faction_id]
			.mission_awards[0],
		g_pilot_data.faction_statistics[g_pilot_data.current_faction_id]
			.mission_awards[1],
		g_pilot_data.faction_statistics[g_pilot_data.current_faction_id]
			.mission_awards[2],
		g_pilot_data.faction_statistics[g_pilot_data.current_faction_id]
			.mission_awards[3]);
	return 1;
}

/* Fills out_results, 4096 bytes, with the pilot's current mission file's text
 * for a completed mission, when use_win_text is nonzero, or else for one not
 * completed. Returns at once when out_results is NULL. Otherwise it clears
 * out_results, finds the mission of g_pilot_data.mission_description_ids in
 * g_mission_list and opens its file; unless the directory is tournaments,
 * battles or campaigns, a file whose first word is 14 gives the 4096 bytes
 * starting 12288 bytes before its end when use_win_text is nonzero, else 8192
 * bytes before it, the last byte set to 0. out_results stays empty when the
 * mission is not in the list, the file does not open, or the directory or the
 * word rules the read out. Does not check g_mission_list for NULL or the read's
 * result. */
// FUNCTION: XVT 0x504E30
void mission_debrief_read_outcome_text(char *out_results, int use_win_text)
{
	if (out_results == NULL) {
		XVT_LOG_ERROR(
			"debrief.outcome_text_no_buffer directory=%d mission=%d",
			(int)g_pilot_data.mission_directory_id,
			(int)g_pilot_data.mission_description_ids
				[g_pilot_data.mission_directory_id]);
		return;
	}

	memset(out_results, 0, 4096);
	unsigned int mission_list_index = 0;
	if (g_mission_count > 0) {
		do {
			if (g_mission_list[mission_list_index].mission_idx ==
			    g_pilot_data.mission_description_ids
				    [g_pilot_data.mission_directory_id]) {
				break;
			}
			++mission_list_index;
			if (g_mission_count <= mission_list_index) {
				break;
			}
		} while (1);
	}
	if (g_mission_count <= mission_list_index) {
		XVT_LOG_WARN(
			"debrief.outcome_text_unlisted directory=%d mission=%d missions=%u",
			(int)g_pilot_data.mission_directory_id,
			(int)g_pilot_data.mission_description_ids
				[g_pilot_data.mission_directory_id],
			g_mission_count);
		return;
	}

	sprintf(g_frontend_scratch_buffer, "%s\\%s",
		g_mission_directory_names[g_pilot_data.mission_directory_id],
		g_mission_list[mission_list_index].file_name);
	xvt_file *stream = file_open(g_frontend_scratch_buffer, "rb");
	if (stream == NULL) {
		XVT_LOG_ERROR("debrief.outcome_text_open_failed file=\"%s\"",
			      g_frontend_scratch_buffer);
		return;
	}
	if (g_pilot_data.mission_directory_id !=
		    MISSION_DIRECTORY_TOURNAMENTS &&
	    g_pilot_data.mission_directory_id != MISSION_DIRECTORY_BATTLES &&
	    g_pilot_data.mission_directory_id != MISSION_DIRECTORY_CAMPAIGNS) {
		uint16_t mission_version;
		file_read_word(stream, &mission_version);
		if (mission_version == 14) {
			if (use_win_text != 0) {
				file_seek(stream, -12288, SEEK_END);
			} else {
				file_seek(stream, -8192, SEEK_END);
			}
			file_read_bytes(stream, out_results, 4096);
			out_results[4095] = 0;
			XVT_LOG_DEBUG(
				"debrief.outcome_text_read file=\"%s\" win=%d offset=%d",
				g_frontend_scratch_buffer, use_win_text,
				use_win_text != 0 ? -12288 : -8192);
		} else {
			XVT_LOG_DEBUG(
				"debrief.outcome_text_skipped file=\"%s\" version=%u",
				g_frontend_scratch_buffer,
				(unsigned)mission_version);
		}
	}
	file_close(stream);
}

/* Draws the debriefing text page: a title (tournament, battle or campaign
 * debriefing in a sequence, else mission debriefing) and g_mission_text
 * wrapped below it. A first frontend_text_draw_wrapped pass from line 4096,
 * plus 1, gives the line count; above 20, a scroll bar sets
 * g_frontend_first_visible_line. Returns 0 without drawing when g_mission_text is
 * NULL, else 1. */
// FUNCTION: XVT 0x504F50
int mission_debrief_draw_narrative_text_page(void)
{
	if (g_mission_text == NULL) {
		return 0;
	}

	struct RECT rect;
	frontend_draw_rect_assign(&rect, 88, 90, 430, 106);
	if (g_pilot_data.mission_directory_id == MISSION_DIRECTORY_MELEES &&
	    g_pilot_data.mission_sequence_active == 1) {
		frontend_text_draw_centered(
			15,
			frontend_string_get(FRONTSTR_823_TOURNAMENT_DEBRIEFING),
			&rect, 0xFFFF);
	} else if (g_pilot_data.mission_directory_id ==
			   MISSION_DIRECTORY_COMBAT_ENGAGEMENTS &&
		   g_pilot_data.mission_sequence_active == 1) {
		frontend_text_draw_centered(
			15, frontend_string_get(FRONTSTR_824_BATTLE_DEBRIEFING),
			&rect, 0xFFFF);
	} else if (g_pilot_data.mission_directory_id ==
			   MISSION_DIRECTORY_TRAINING_EXERCISES &&
		   g_pilot_data.mission_sequence_active == 1) {
		frontend_text_draw_centered(
			15,
			frontend_string_get(FRONTSTR_825_CAMPAIGN_DEBRIEFING),
			&rect, 0xFFFF);
	} else {
		frontend_text_draw_centered(
			15,
			frontend_string_get(FRONTSTR_822_MISSION_DEBRIEFING),
			&rect, 0xFFFF);
	}

	frontend_draw_rect_assign(&rect, 88, 111, 420, 431);
	int line_count = frontend_text_draw_wrapped(12, g_mission_text, &rect,
						    0xFFFF, 4, 4096) +
			 1;
	if (line_count > 20) {
		frontend_draw_rect_assign(&rect, 421, 111, 430, 431);
		g_frontend_first_visible_line = frontend_scrollbar_draw(
			&rect, g_frontend_first_visible_line, line_count, 0, 5,
			(unsigned int)g_color_navy, 9);
		frontend_draw_rect_assign(&rect, 88, 111, 420, 431);
	} else {
		frontend_draw_rect_assign(&rect, 88, 111, 430, 431);
	}
	frontend_text_draw_wrapped(12, g_mission_text, &rect, 0xFFFF, 4,
				   g_frontend_first_visible_line);
	return 1;
}
