#ifndef XVT_FRONTEND_MISSION_SETUP_H
#define XVT_FRONTEND_MISSION_SETUP_H

#include <stdint.h>
#include <stdio.h>

#include "xvt/assets/file.h"
#include "xvt/assets/object_type.h"
#include "xvt/frontend/pilot.h"
#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Stored as uint8_t in the binary (IDB enum battle_length). */
typedef uint8_t battle_length;

enum {
	BATTLE_LENGTH_TWO_WINS = 0x0,
	BATTLE_LENGTH_THREE_WINS = 0x1,
	BATTLE_LENGTH_FOUR_WINS = 0x2,
};

/* Stored as uint8_t in the binary (IDB enum craft_selection_mode). */
typedef uint8_t craft_selection_mode;

enum {
	CRAFT_SELECTION_OFF = 0x0,
	CRAFT_SELECTION_ON = 0x1,
	CRAFT_SELECTION_HOST_ONLY = 0x2,
};

/* Stored as uint8_t in the binary (IDB enum craft_wave_mode). */
typedef uint8_t craft_wave_mode;

enum {
	CRAFT_WAVES_NONE = 0x0,
	CRAFT_WAVES_DEFAULT = 0x1,
	CRAFT_WAVES_UNLIMITED = 0x2,
};

/* Stored as uint8_t in the binary (IDB enum combat_balance_mode). */
typedef uint8_t combat_balance_mode;

enum {
	COMBAT_BALANCE_AUTOBALANCE = 0x0,
	COMBAT_BALANCE_FAVOR_IMPERIAL = 0x1,
	COMBAT_BALANCE_NEUTRAL = 0x2,
	COMBAT_BALANCE_FAVOR_REBEL = 0x3,
};

/* Stored as int8_t in the binary (IDB enum sequence_continuation_choice). */
typedef int8_t sequence_continuation_choice;

enum {
	SEQUENCE_RESTART = 0x0,
	SEQUENCE_CONTINUE = 0x1,
};

#pragma pack(push, 1)

struct mp_roster_entry {
	/* Player name. The local player's entry gets the pilot's name from the
	 * solo, concourse and host screens, or the session's player name from
	 * the network session; other entries get at most 13 characters copied
	 * from the network player list, or "No name" when it lacks the id. */
	char name[14];
	/* DirectPlay id of the player; 0 for an empty entry, 1 for the pilot in
	 * a solo game. */
	int player_id;
	/* Pilot rating; its name is string FRONTSTR_154_DRONE plus the
	 * rating. */
	pilot_rating pilot_rating;
	/* 0, or the chosen preset's craft: set by frontend_net_on_craft_loadout
	 * from a player's CRAFT_LOADOUT or copied from the host's roster
	 * packets by frontend_net_on_launch_roster and
	 * frontend_net_store_roster_loadouts, and in a solo game from the local
	 * choice by the briefing. */
	/* Nonzero exact craft type; zero selects the assigned flight group's
	 * base or optional craft. */
	int craft_type_override;
	/* The chosen flight group craft option minus 1, so -1 for the group's
	 * own craft; set as craft_type_override is. */
	/* Optional craft index; values above 9 fall back to the assigned flight
	 * group's base craft. */
	int craft_option_index;
	/* A value n above 0 is the flight group's optional warhead n - 1; set
	 * as craft_type_override is. */
	int warhead_option_index; ///< Warhead loadout option index; zero uses the mission default.
	/* A value n above 0 is the flight group's optional beam n - 1; set as
	 * craft_type_override is. */
	int beam_option_index; ///< Beam loadout option index; zero uses the mission default.
	/* A value n above 0 is the flight group's optional countermeasure
	 * n - 1; set as craft_type_override is. */
	/* Countermeasure loadout option index; zero uses the mission
	 * default. */
	int countermeasure_option_index;
};

#pragma pack(pop)
typedef char xvt_size_mp_roster_entry[(sizeof(struct mp_roster_entry) == 42)
					      ? 1
					      : -1];

struct mission_setup_player_assignments {
	/* Player id in each of the 8 slots of each team; slot 0 is the team's
	 * captain, 0 an empty slot. */
	int team_player_ids[10][8];
	/* Ids of the players who hold a team slot, 0 for an unused entry. Most
	 * writers put a player at its g_mp_roster index; a drop on the team
	 * screen takes the first empty entry. */
	int assigned_player_ids[8];
};

struct ship_list_entry {
	/* Model file named by the list, its last three characters lowercased to
	 * "opt"; at most 63 characters kept. */
	char model_file_name[64];
	/* Set by ship_list_load; the tech library and the briefing's craft
	 * screen read it. */
	/* craft_species value read from FRONTRES/frntspec.lst; stored as int by
	 * fscanf. */
	int craft_type;
};

typedef enum mission_setup_active_panel {
	MISSION_SETUP_PANEL_PLAYERS = 0x0,
	MISSION_SETUP_PANEL_SETTINGS = 0x1,
} mission_setup_active_panel;

typedef enum mission_setup_debrief_transition {
	MISSION_SETUP_DEBRIEF_TRANSITION_NONE = 0x0,
	MISSION_SETUP_DEBRIEF_TRANSITION_ENTER_CURRENT_MISSION = 0x1,
	MISSION_SETUP_DEBRIEF_TRANSITION_ADVANCE_MISSION_DIRECTORY = 0x2,
} mission_setup_debrief_transition;

struct melee_tournament_team_standings {
	/* -1 for a team outside the tournament's standings.
	 * frontend_mission_init_player_state sets -1 for every team at a melee
	 * sequence's first mission, then 0 for teams with a human player or,
	 * with AI opponents or in a solo game, with player flight groups. For a
	 * team the AI flies, mission_init stores the team whose player flight
	 * group it copies, with the top bit (INT32_MIN) set when that group's
	 * craft is a fighter object type or the Headhunter, and reads it back
	 * past a sequence's first mission. */
	int ai_opponent_source_team_and_type_flag;
	/* Team's points over the tournament: after each melee, fediskio.c adds
	 * its bonus and mission team scores. A team outside the standings is
	 * given the score last computed for a team before it instead (when
	 * there is none, 0). */
	int total_score;
	/* Melees in which no other team in the standings scored more than the
	 * team. */
	int first_place_count;
	/* Melees in which exactly one other team in the standings scored
	 * more. */
	int second_place_count;
	/* Melees in which exactly two other teams in the standings scored
	 * more. */
	int third_place_count;
};

struct melee_tournament_sequence_state {
	/* Nothing reads or writes it by name; it is cleared with the rest of
	 * the state. */
	int reserved[9];
	/* Step of the tournament being flown, from 0; the debriefing raises it
	 * by 1 for the next melee. */
	int current_mission_index;
	/* Melees in the tournament: the first line of its sequence file, or the
	 * host's mission start packet on a client. */
	int mission_count;
	/* Each team's standing, by team index. */
	struct melee_tournament_team_standings team_standings[10];
	/* Human players at the tournament's first melee, counted then by
	 * frontend_mission_init_player_state; the tournament award reads it. */
	unsigned int human_player_count;
	/* Teams in the standings: g_team_count with AI opponents or in a solo
	 * game, else the teams with a human player. Set by
	 * frontend_mission_init_player_state at the tournament's first melee. */
	int participating_team_count;
	/* In a melee, mission_init raises by one the AI skill (group_ai, while
	 * under its maximum) of the first player flight group no human flies on
	 * this team and on each following team, one team per boost: 3 on easy,
	 * 2 on medium, else 1. It picks a random team with player flight groups
	 * but no human, or takes team 0's player flight group count when there
	 * is none, and in a sequence keeps it here for the later melees. */
	int ai_boost_first_team;
};

typedef enum battle_mission_result {
	BATTLE_MISSION_RESULT_IMPERIAL_VICTORY = 0x0,
	BATTLE_MISSION_RESULT_REBEL_VICTORY = 0x1,
	BATTLE_MISSION_RESULT_DRAW = 0x2,
} battle_mission_result;

struct battle_sequence_state {
	/* Step of the battle being flown, from 0: raised by 1 when the battle
	 * goes on to its next mission, and lowered by 1 after a draw so that
	 * step is flown again. */
	uint32_t current_mission_index;
	/* Mission id of the combat engagement chosen for the current step.
	 * Nothing reads it. */
	int current_mission_id;
	/* Wins that end the battle: g_game_config.battle_length_index + 2, or the
	 * host's mission start packet on a client. */
	int victories_needed;
	/* Result of each step, written by fediskio.c after the mission:
	 * IMPERIAL_VICTORY when team 0 is the first team with its primary goals
	 * complete and its prevent goals not, REBEL_VICTORY when team 1 is,
	 * else DRAW. */
	battle_mission_result mission_results[10];
	/* The mission flown at each step of the sequence: its position in the
	 * sequence descriptor's mission list, which the repeat checks compare
	 * against. The multiplayer battle choice stores the mission id instead,
	 * and starting a combat engagement sequence stores a mission list index
	 * in the first slot. */
	/* Written by mission_setup_update's mission start,
	 * mission_setup_select_first_sequence_mission,
	 * mission_setup_select_next_sequence_mission, and the battle choice screen
	 * and its list. mission_setup_select_next_sequence_mission and
	 * mission_setup_battle_choice_build_list read the entries before
	 * current_mission_index to skip missions already flown, and after a draw
	 * the former reuses the drawn step's ordinal. */
	int mission_ordinals[10];
	/* Index in g_mission_list of the mission flown at each step. */
	int mission_list_indices[10];
	/* Human players in the latest mission, counted by
	 * frontend_mission_init_player_state. */
	unsigned int human_player_count;
	/* Score over the battle: fediskio.c sets it to the mission score after
	 * the first step and adds it after each later one. */
	int cumulative_score;
};

struct campaign_sequence_state {
	/* Nothing reads or writes it by name. */
	/* Unresolved campaign-sequence field; cleared and persisted with the
	 * full state. */
	int32_t unused00;
	/* Raised by 1 by the debriefing and when a saved campaign continues;
	 * mission_setup_select_next_sequence_mission lowers it by 1 when
	 * last_mission_completed is 0, so that mission is flown again. */
	int32_t current_mission_index; ///< Zero-based position of the current campaign mission.
	/* From mission_setup_select_first_sequence_mission, or the host's mission
	 * start packet on a client. */
	int32_t mission_count; ///< Mission count read from the selected campaign descriptor.
	/* Copied by fediskio.c from the pilot's team's is_mission_completed after
	 * each campaign mission. */
	/* Whether the just-finished campaign mission completed successfully. */
	int32_t last_mission_completed;
	/* Counted by frontend_mission_init_player_state for each mission. */
	int32_t human_player_count; ///< Human players participating in the campaign mission.
	/* fediskio.c sets it to the mission score after a completed first
	 * mission and adds the score of each later completed one. */
	int32_t cumulative_score; ///< Campaign score accumulated across completed missions.
};

struct battle_continuation {
	/* Nothing reads or writes it by name. */
	int32_t unused00; ///< Unresolved persisted battle-continuation field.
	/* g_game_config.random_seed when the debriefing saved the battle.
	 * Continuing does not put it back into g_game_config: a host sends it,
	 * and a client compares it with its own saved seed to keep or zero its
	 * cumulative score. */
	uint32_t
		random_seed; ///< Random seed used to reproduce mission selection.
	/* Set by the debriefing while the battle is unfinished (in a network
	 * game only on the host) and cleared when it ends or is not
	 * continued. */
	int32_t is_active; ///< Continuation slot contains an unfinished battle.
	/* g_game_config.battle_length_index when saved; put back into g_game_config
	 * when the battle is selected. */
	int32_t battle_length_index; ///< Configured battle-length selector.
	/* g_game_config.random_setup when saved; put back into g_game_config when
	 * the battle is selected. */
	int32_t random_setup; ///< Configured sequential, random, or player-choice selection mode.
	/* Copy of g_pilot_data.battle_sequence_state when saved, copied back when
	 * the battle continues. */
	struct battle_sequence_state
		sequence_state; ///< Saved battle sequence state.
};

struct campaign_continuation {
	/* Nothing reads or writes it by name. */
	int32_t unused00; ///< Unresolved persisted campaign-continuation field.
	/* g_game_config.random_seed when the debriefing saved the campaign.
	 * Continuing does not put it back into g_game_config: a host sends it,
	 * and a client compares it with the seed of its own entry (the campaign
	 * id plus 12) to keep or zero its cumulative score. */
	uint32_t
		random_seed; ///< Random seed used to reproduce mission selection.
	/* Set by the debriefing while the campaign is unfinished, cleared when
	 * it ends or is not continued; a client's own entry (the campaign id
	 * plus 12) is always saved with 0. */
	int32_t is_active; ///< Continuation slot contains an unfinished campaign.
	/* g_game_config.random_setup when saved; put back into g_game_config when
	 * the campaign is selected. */
	int32_t random_setup; ///< Configured sequential, random, or player-choice selection mode.
	/* Copy of g_pilot_data.campaign_sequence_state when saved, copied back
	 * when the campaign continues. */
	struct campaign_sequence_state
		sequence_state; ///< Saved campaign sequence state.
};

extern int g_team_player_flight_group_count[10];
extern int g_mission_setup_dragged_player_id;
extern int g_mission_setup_reserved_player_ids[8];
extern int g_mission_setup_reserved_player_count;
extern int g_mission_setup_team_assignment_skipped;
extern int g_mission_setup_countdown_clock_ms;
extern int g_mission_setup_launch_countdown_ms;
extern int g_mission_setup_countdown_previous_clock_ms;
extern int g_mission_setup_launch_signal_sent;
extern int g_team_count;
extern int g_mission_setup_last_broadcast_countdown_second;
extern int g_mission_setup_player_flight_group_indices[80];
extern int g_text_shade_ramps[5][8];
extern struct mission_setup_player_assignments
	g_mission_setup_player_assignments;
extern struct ship_list_entry *g_ship_list;
extern int g_ship_type_to_ship_list_index[18];
extern int g_ship_count;
extern int g_mission_setup_selected_flight_group_index;
extern int g_mission_setup_selected_flight_group_craft_option_index;
extern int g_mission_setup_selected_preset_craft_option_index;
extern int g_mission_setup_preset_craft_option_count;
extern int g_mission_setup_selected_warhead_option_index;
extern int g_mission_setup_selected_beam_option_index;
extern int g_mission_setup_selected_countermeasure_option_index;
extern int g_mission_setup_selected_wave_count_minus_one;
extern int g_mission_setup_selected_craft_count;
extern int g_mission_setup_flight_group_craft_option_count;
extern int g_mission_setup_warhead_option_count;
extern int g_mission_setup_beam_option_count;
extern int g_mission_setup_countermeasure_option_count;
extern const int g_warhead_type_map[11];
extern const int g_preset_craft_types[11];
extern const uint8_t g_craft_iff_counterpart[20];
extern unsigned int g_mission_count;
extern int g_selected_mission_list_index;
extern struct mission_list_entry *g_mission_list;
extern int g_frontend_game_session_in_progress;
extern int g_mission_setup_is_host;
extern int g_mission_setup_roster_authoritative;
extern int g_mission_setup_begin_button_lockout_frames;
extern int g_frontend_skip_screen_entry_setup;
extern const char *g_mission_directory_names[6];
extern struct mp_roster_entry g_mp_roster[8];
extern int g_mp_roster_ready_flags[8];
extern int g_battle_mission_list_count;
extern struct mission_list_entry *g_battle_mission_list;
extern int g_battle_choice_clock_ms;
extern int g_battle_choice_scroll_offset;
extern int g_battle_choice_remaining_ms;
extern int g_battle_choice_previous_clock_ms;
extern int g_battle_choice_row_count;
extern int g_battle_choice_last_sent_second;
extern int g_battle_choice_timeout_handled;
extern int g_mission_setup_use_combat_sim_pilot_state;
extern int g_mission_setup_mission_list_row_count;
extern int g_mission_setup_mission_list_scroll_offset;
extern int g_remote_battle_continuation_active;
extern int g_remote_battle_sequence_continuation_choice;
extern int g_remote_battle_last_completed_mission_index;
extern int g_remote_battle_rebel_victory_count;
extern int g_remote_battle_imperial_victory_count;
extern int g_mission_setup_selected_player_roster_index;
extern mission_setup_active_panel g_mission_setup_active_panel;
extern int g_mission_setup_last_host_broadcast_ms;
extern int g_local_pilot_network_player_index;
extern int g_mission_setup_show_description_panel;
extern mission_setup_debrief_transition g_mission_setup_debrief_transition;

int mission_setup_exit(int frame_counter);
int mission_setup_update(int frame_counter);
int mission_setup_draw_mission_type_controls(void);
void mission_setup_load_mission_list(int mission_directory_id);
void mission_setup_load_mission_desc_text(char *out_text4096);
int mission_setup_draw_mission_description(void);
int mission_setup_draw_player_roster(int frame_counter);
int mission_setup_broadcast_lobby_selection(void);
int mission_setup_send_lobby_state(int to_player_id);
int mission_setup_broadcast_ready_roster(int to_player_id);
int mission_setup_draw_mission_list(int frame_counter);
int mission_setup_select_first_sequence_mission(void);
int mission_setup_draw_game_settings(void);
int mission_setup_count_mission_list_entries(xvt_file *stream);
int mission_setup_draw_background(void);
int mission_setup_use_rebel_background(void);
void mission_setup_draw_craft_loadout(void);
void mission_setup_draw_player_loadouts(int frame_counter);
int mission_setup_update_craft_loadout(void);
void mission_setup_init_craft_loadout(void);
int mission_setup_get_warhead_type(int player_roster_index);
int mission_setup_get_beam_type(int player_roster_index);
int mission_setup_get_countermeasure_type(int player_roster_index);
int mission_setup_get_craft_type(int player_roster_index);
void ship_list_load(void);
int mission_setup_exit_next_mission(void);
int mission_setup_enter_next_mission(int frame_counter);
int mission_setup_prune_disconnected_players(void);
int mp_roster_compact_active_entries(void);
int mission_setup_select_next_sequence_mission(void);
int mission_setup_exit_current_mission(void);
int mission_setup_enter_current_mission(int frame_counter);
int mission_setup_team_assignment_update(int frame_counter);
int mission_setup_draw_unassigned_players(int frame_counter);
void mission_setup_update_team_counts(void);
void mission_setup_draw_team_assignments(int frame_counter);
void mission_setup_prune_team_assignments(void);
int mission_setup_is_team_assignment_valid(void);
int mission_setup_update_team_controls(void);
int mission_setup_randomize_team_assignments(void);
int mission_setup_clear_team_assignments(void);
int mission_setup_broadcast_team_assignments(void);
int mission_setup_try_continue_battle(void);
int mission_setup_try_continue_campaign(void);
int mission_setup_draw_team_mission_description(void);
int mission_setup_free_screen_resources(int frame_counter);
int mission_setup_flight_assignment_update(int frame_counter);
int mission_setup_draw_assignment_controls(void);
int mission_setup_draw_flight_assignments(int frame_counter);
void mission_setup_prune_flight_assignments(void);
int mission_setup_are_flight_assignments_complete(void);
int mission_setup_randomize_flight_assignments(void);
int mission_setup_clear_flight_assignments(void);
int mission_setup_fill_flight_assignments(void);
int mission_setup_draw_assigned_players(int frame_counter);
int mission_setup_draw_assignment_mission_description(void);
int mission_setup_battle_choice_exit(void);
int mission_setup_battle_choice_update(int frame_counter);
int mission_setup_battle_choice_draw_description(void);
int mission_setup_battle_choice_draw_roster(int frame_counter);
int mission_setup_battle_choice_build_list(void);
int mission_setup_battle_choice_draw_list(int frame_counter);

#ifdef __cplusplus
}
#endif

#endif
