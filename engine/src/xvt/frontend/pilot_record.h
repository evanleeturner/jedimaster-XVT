#ifndef XVT_FRONTEND_PILOT_RECORD_H
#define XVT_FRONTEND_PILOT_RECORD_H

#include <stdint.h>

#include "xvt/frontend/frontend_mission_list.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/frontend/pilot.h"
#include "xvt/util/win32.h"
#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

extern int g_pilot_record_page;
extern struct POINT g_pilot_rating_icon_pos[25];
extern int g_campaign_singleplayer_award_count;
extern int g_campaign_medal_scroll_offset;
extern int g_campaign_singleplayer_award_flags[];
extern int g_campaign_multiplayer_award_count;
extern int g_campaign_multiplayer_award_flags[];
extern int g_campaign_medal_entry_count;
extern int g_cutscene_viewer_scroll_row;
extern int g_cutscene_viewer_total_rows;
extern char g_pilot_record_name_input[14];
extern int g_pilot_list_scroll_offset;

#pragma pack(push, 1)

struct pilot_network_player {
	/* Nothing writes it by name, so it holds what the pilot file held, or 0
	 * once mission_setup_update clears the entry. xvt_launch_task_queue
	 * copies the local player's entry's into the flight command line. */
	char formal_name[14];
	/* The player's name from g_mp_roster, cut to 12 characters, set at
	 * launch by frontend_mission_init_player_state; the debriefing shows
	 * it. */
	char friendly_name[14];
	/* Flight group the player flies, from
	 * g_mission_setup_player_flight_group_indices at launch. */
	int flight_group_id;
	/* The player's DirectPlay id, 0 for an empty entry; set at launch, set
	 * to 0 by mission_setup_prune_disconnected_players. */
	int direct_play_id;
	int rating; /* The player's pilot rating at launch, from g_mp_roster. */
	/* The player's mission score plus its team's bonus score, set by
	 * fe_disk_io_commit_flight_results; 0 at launch and when the next mission
	 * or a replay starts. */
	int total_score;
	/* The player's full kills of the mission's flight groups, summed by
	 * fe_disk_io_commit_flight_results; 0 at launch, next mission and
	 * replay. */
	int kills;
	int kills_shared; /* The player's shared kills, summed like kills. */
	/* Only ever set to 0, at launch, next mission and replay; nothing reads
	 * it. */
	int craft_inspected;
	int kills_assist; /* The player's kill assists, summed like kills. */
	/* Craft the player lost in the mission
	 * (per_mission_kills.total_craft_losses), set by
	 * fe_disk_io_commit_flight_results. */
	int total_losses;
	/* Craft type the player chose, from g_mp_roster craft_type_override; 0
	 * keeps the flight group's own craft. mission_init applies it. */
	int craft_id;
	/* Index into the flight group's optional_craft chosen, or -1 for none;
	 * frontend_mission_init_player_state sets -1 when that entry is
	 * CRAFT_SPECIES_UNKNOWN. mission_init applies it. */
	int craft_option;
	/* Index into the flight group's optional_warheads: the roster's
	 * warhead_option_index minus 1; -1 keeps the flight group's own. */
	int warhead_option;
	/* Optional beam index: the roster's beam_option_index minus 1; -1 keeps
	 * the flight group's own. */
	int beam_option;
	/* Optional countermeasure index: the roster's countermeasure_option_index
	 * minus 1; -1 keeps the flight group's own. */
	int countermeasure_option;
	/* 1 once the player left the flight, set by
	 * flight_net_mark_pilot_network_player_left; 0 at launch, next mission and
	 * replay. The debriefing grays the player's name. */
	int has_left;
};

#pragma pack(pop)
typedef char xvt_size_pilot_network_player
	[(sizeof(struct pilot_network_player) == 88) ? 1 : -1];

#pragma pack(push, 1)

struct pilot_team {
	/* The team's score in the last mission: its bonus score plus, in a
	 * melee, its mission score, else its network players' mission scores.
	 * Set by fe_disk_io_commit_flight_results for teams 0 to 7. */
	int mission_score;
	/* 1 when the team's primary goal status is 1 and its prevent goal
	 * status is not 1, else 0. */
	int is_mission_completed;
	/* No code reads or writes it by name; mission_setup_enter_current_mission
	 * uses the address of teams[2]'s as the end of a walk over
	 * network_players. */
	int unknown08;
	/* The team's team_mission_completion_time_seconds from the flight, in
	 * seconds. */
	int mission_time;
	int kills; /* The team's full kills in the mission (team_kill_stats). */
	/* The team's shared kills in the mission (team_kill_stats). */
	int kills_shared;
	/* The team's craft losses in the mission (team_kill_stats). */
	int losses;
};

#pragma pack(pop)
typedef char xvt_size_pilot_team[(sizeof(struct pilot_team) == 28) ? 1 : -1];

#pragma pack(push, 1)

struct pilot_mission {
	/* Times the mission was flown, counted up by
	 * fe_disk_io_commit_flight_results; the mission achievements page lists the
	 * mission once it is not 0. */
	int number_times_flown;
	/* Flights whose primary goal status was 1, counted up by
	 * fe_disk_io_commit_flight_results; nothing reads it by name. */
	int completed_count;
	/* Flights whose primary goal status was 2 or prevent goal status 1,
	 * counted up by fe_disk_io_commit_flight_results; nothing reads it by
	 * name. */
	int failed_count;
	/* Highest score of a flight; training and combat missions count only
	 * flights whose flight group's waves are not CRAFT_WAVES_UNLIMITED. */
	int best_score;
	/* Shortest nonzero completion time of the player's team, in seconds, 0
	 * until one is recorded; training and combat missions count only
	 * flights whose flight group's waves are not CRAFT_WAVES_UNLIMITED. */
	int best_time;
	/* Best finish of a melee, 1 for first; 0 until one is recorded and for
	 * other mission types. */
	int best_placement;
	/* Best award won, 1 the best to 5, or 6 for a failed one: an award
	 * replaces it when lower or when the field is 0. On a replacement
	 * fe_disk_io_commit_flight_results moves one of the faction's
	 * mission_evaluations or melee_plaques counts from the old level to the
	 * new. A flight with no award clears a 6 and takes it off that
	 * count. */
	int award_level;
	/* Biggest lead over the closest other team when a melee finished first;
	 * only fe_disk_io_commit_flight_results reads it, to compare. */
	unsigned int best_margin;
	/* No code reads or writes it by name; the base-game record copies in
	 * pilot.c start the next history block at this field of the last entry
	 * (see field1558 in pilot_faction). */
	int field20;
};

#pragma pack(pop)
typedef char
	xvt_size_pilot_mission[(sizeof(struct pilot_mission) == 36) ? 1 : -1];

#pragma pack(push, 1)

struct pilot_multiplayer_mission {
	/* Times the mission was flown in multiplayer, counted up by
	 * fe_disk_io_commit_flight_results; the mission achievements page lists the
	 * mission once it is not 0. */
	int number_times_flown;
	/* Melee flights finished first, counted up by
	 * fe_disk_io_commit_flight_results; nothing reads it by name. */
	int first_place_count;
	/* Melee flights finished second, counted up by
	 * fe_disk_io_commit_flight_results; nothing reads it by name. */
	int second_place_count;
	/* Melee flights finished third, counted up by
	 * fe_disk_io_commit_flight_results; nothing reads it by name. */
	int third_place_count;
	/* Flights whose primary goal status was 1, counted up by
	 * fe_disk_io_commit_flight_results; nothing reads it by name. */
	int completed_count;
	/* Flights whose primary goal status was 2 or prevent goal status 1,
	 * counted up by fe_disk_io_commit_flight_results; nothing reads it by
	 * name. */
	int failed_count;
	/* Highest score of a flight; training and combat missions count only
	 * flights whose flight group's waves are not CRAFT_WAVES_UNLIMITED. */
	int best_score;
	/* Shortest nonzero completion time of the player's team, in seconds, 0
	 * until one is recorded; training and combat missions count only
	 * flights whose flight group's waves are not CRAFT_WAVES_UNLIMITED. */
	int best_time;
	/* Best finish of a melee, 1 for first; 0 until one is recorded and for
	 * other mission types. */
	int best_placement;
	/* Best award won, 1 the best to 5, or 6 for a failed one: an award
	 * replaces it when lower or when the field is 0. Every award other than
	 * 6 a flight wins adds one to the faction's mission_evaluations or
	 * melee_plaques count, replaced or not, and a replaced 6 comes off it. A
	 * flight with no award clears a 6 and takes it off that count. */
	int award_level;
	/* Biggest lead over the closest other team when a melee finished first;
	 * only fe_disk_io_commit_flight_results reads it, to compare. */
	unsigned int best_margin;
	/* No code reads or writes it by name; the base-game record copies in
	 * pilot.c start the next history block at this field of the last entry
	 * (see field1558 in pilot_faction). */
	int field2c;
};

#pragma pack(pop)
typedef char xvt_size_pilot_multiplayer_mission
	[(sizeof(struct pilot_multiplayer_mission) == 48) ? 1 : -1];

#pragma pack(push, 1)

struct pilot_tournament {
	/* Tournaments started: fe_disk_io_commit_flight_results counts it up when
	 * it commits a tournament's first mission (current_mission_index 0).
	 * Single-player tables take a tournament with one human player. */
	int attempt_count;
	/* Tournaments finished: counted up when fe_disk_io_commit_flight_results
	 * commits the last mission of one (mission_count - current_mission_index
	 * is 1) other than its first; nothing reads it by name. */
	int completed_count;
	/* Tournaments finished first overall, counted with completed_count;
	 * nothing reads it by name. */
	int first_place_count;
	/* Tournaments finished second overall, counted with completed_count;
	 * nothing reads it by name. */
	int second_place_count;
	/* Tournaments finished third overall, counted with completed_count;
	 * nothing reads it by name. */
	int third_place_count;
	/* Highest total score of the local team at a tournament's end. */
	int best_score;
	/* Best overall finish at a tournament's end, 1 for first; 0 until one
	 * is recorded. */
	int best_placement;
	/* Best award won, 1 the best to 5, or 6 for a failed one: an award
	 * replaces it when lower or when the field is 0. On a replacement
	 * fe_disk_io_commit_flight_results moves one of the faction's
	 * tournament_trophies counts from the old level to the new. A flight
	 * with no award clears a 6 and takes it off that count. */
	int award_level;
	/* Biggest overall lead when a tournament finished first; only
	 * fe_disk_io_commit_flight_results reads it, to compare. */
	unsigned int best_margin;
	/* No code reads or writes it by name; the base-game record copies in
	 * pilot.c start the next history block at this field of the last entry
	 * (see field1558 in pilot_faction). */
	int field24;
};

#pragma pack(pop)
typedef char xvt_size_pilot_tournament[(sizeof(struct pilot_tournament) == 40)
					       ? 1
					       : -1];

#pragma pack(push, 1)

struct pilot_multiplayer_tournament {
	/* Tournaments started: fe_disk_io_commit_flight_results counts it up when
	 * it commits a tournament's first mission (current_mission_index 0).
	 * Multiplayer tables take a tournament with any other number of human
	 * players. */
	int attempt_count;
	/* Tournaments finished: counted up when fe_disk_io_commit_flight_results
	 * commits the last mission of one (mission_count - current_mission_index
	 * is 1) other than its first; nothing reads it by name. */
	int completed_count;
	/* Tournaments finished first overall, counted with completed_count;
	 * nothing reads it by name. */
	int first_place_count;
	/* Tournaments finished second overall, counted with completed_count;
	 * nothing reads it by name. */
	int second_place_count;
	/* Tournaments finished third overall, counted with completed_count;
	 * nothing reads it by name. */
	int third_place_count;
	/* Highest total score of the local team at a tournament's end. */
	int best_score;
	/* Best overall finish at a tournament's end, 1 for first; 0 until one
	 * is recorded. */
	int best_placement;
	/* No code reads or writes it by name; it is saved with the pilot
	 * file. */
	int unused1c;
	/* Best award won, 1 the best to 5, or 6 for a failed one: an award
	 * replaces it when lower or when the field is 0. Every award a flight
	 * wins adds one to the faction's tournament_trophies count, replaced or
	 * not, and a replaced 6 comes off it. A flight with no award clears a 6
	 * and takes it off that count. */
	int award_level;
	/* Biggest overall lead when a tournament finished first; only
	 * fe_disk_io_commit_flight_results reads it, to compare. */
	unsigned int best_margin;
	/* No code reads or writes it by name; the base-game record copies in
	 * pilot.c start the next history block at this field of the last entry
	 * (see field1558 in pilot_faction). */
	int field28;
};

#pragma pack(pop)
typedef char xvt_size_pilot_multiplayer_tournament
	[(sizeof(struct pilot_multiplayer_tournament) == 44) ? 1 : -1];

#pragma pack(push, 1)

struct pilot_battle {
	/* Battles started: fe_disk_io_commit_flight_results counts it up when it
	 * commits a battle's first mission (current_mission_index 0).
	 * Single-player tables take a battle with fewer than 2 human
	 * players. */
	int attempt_count;
	/* Flights committed after which the local player's side (team 0
	 * Imperial, 1 Rebel) had victories_needed wins;
	 * fe_disk_io_commit_flight_results counts it up; the mission achievements
	 * page tests it. */
	int victory_count;
	/* Flights committed after which the other side had victories_needed
	 * wins; fe_disk_io_commit_flight_results counts it up; nothing reads it by
	 * name. */
	int defeat_count;
	/* fe_disk_io_commit_flight_results counts it up when it commits a flight
	 * whose current_mission_index is 10, also when it counted a victory or
	 * defeat for that flight; nothing reads it by name. */
	/* Battles reaching the sequence mission limit without either side
	 * winning. */
	int draw_count;
	/* Highest cumulative battle score (battle_sequence_state.cumulative_score)
	 * after a flight. */
	int best_score;
	/* No code reads or writes it by name; it is saved with the pilot
	 * file. */
	int unused14;
	/* Best award won, 1 the best to 5, or 6 for a failed one: an award
	 * replaces it when lower or when the field is 0. On a replacement
	 * fe_disk_io_commit_flight_results moves one of the faction's
	 * battle_medallions counts from the old level to the new. A flight with
	 * no award clears a 6 and takes it off that count. */
	int award_level;
	/* Biggest margin of a won battle: the winner's victories less the
	 * loser's, doubled for one human player at hard difficulty. */
	unsigned int best_victory_margin;
	/* No code reads or writes it by name; the base-game record copies in
	 * pilot.c start the next history block at this field of the last entry
	 * (see field1558 in pilot_faction). */
	int field20;
};

#pragma pack(pop)
typedef char
	xvt_size_pilot_battle[(sizeof(struct pilot_battle) == 36) ? 1 : -1];

#pragma pack(push, 1)

struct pilot_multiplayer_battle {
	/* Battles started: fe_disk_io_commit_flight_results counts it up when it
	 * commits a battle's first mission (current_mission_index 0). Multiplayer
	 * tables take a battle with 2 or more human players. */
	int attempt_count;
	/* Flights committed after which the local player's side (team 0
	 * Imperial, 1 Rebel) had victories_needed wins;
	 * fe_disk_io_commit_flight_results counts it up; the mission achievements
	 * page tests it. */
	int victory_count;
	/* Flights committed after which the other side had victories_needed
	 * wins; fe_disk_io_commit_flight_results counts it up; nothing reads it by
	 * name. */
	int defeat_count;
	/* fe_disk_io_commit_flight_results counts it up when it commits a flight
	 * whose current_mission_index is 10, also when it counted a victory or
	 * defeat for that flight; nothing reads it by name. */
	/* Battles reaching the sequence mission limit without either side
	 * winning. */
	int draw_count;
	/* Highest cumulative battle score (battle_sequence_state.cumulative_score)
	 * after a flight. */
	int best_score;
	/* No code reads or writes it by name; it is saved with the pilot
	 * file. */
	int unused14;
	/* No code reads or writes it by name; it is saved with the pilot
	 * file. */
	int unused18;
	/* Best award won, 1 the best to 5, or 6 for a failed one: an award
	 * replaces it when lower or when the field is 0. Every award a flight
	 * wins adds one to the faction's battle_medallions count, replaced or
	 * not, and a replaced 6 comes off it. A flight with no award clears a 6
	 * and takes it off that count. */
	int award_level;
	/* Biggest margin of a won battle: the winner's victories less the
	 * loser's, doubled for one human player at hard difficulty. */
	unsigned int best_victory_margin;
	/* No code reads or writes it by name; it is saved with the pilot
	 * file. */
	int unused24;
};

#pragma pack(pop)
typedef char xvt_size_pilot_multiplayer_battle
	[(sizeof(struct pilot_multiplayer_battle) == 40) ? 1 : -1];

#pragma pack(push, 1)

struct pilot_campaign {
	/* Campaigns started: fe_disk_io_commit_flight_results counts it up when it
	 * commits a campaign's first mission (current_mission_index 0), in
	 * sp_campaigns with fewer than 2 human players, else in mp_campaigns. */
	int attempt_count;
	/* One past the furthest mission completed in the campaign: raised to
	 * current_mission_index + 1 when a mission is completed; the achievements
	 * page shows it plus 1. */
	int next_mission_index;
	/* 1 once fe_disk_io_commit_flight_results commits a completed mission that
	 * leaves current_mission_index equal to mission_count;
	 * mission_debrief_update also sets the single-player entry after a won
	 * campaign's last mission. */
	int is_finished;
	/* Highest campaign score: cumulative_score after a completed mission, or
	 * cumulative_score plus the mission's score after a failed one. */
	int best_score;
	/* No code reads or writes it by name; it is saved with the pilot
	 * file. */
	int unused10;
	/* No code reads or writes it by name; it is saved with the pilot
	 * file. */
	int unused14;
	/* No code reads or writes it by name; it is saved with the pilot
	 * file. */
	int unused18;
	/* No code reads or writes it by name; it is saved with the pilot
	 * file. */
	int unused1c;
	/* No code reads or writes it by name; it is saved with the pilot
	 * file. */
	int unused20;
};

#pragma pack(pop)
typedef char
	xvt_size_pilot_campaign[(sizeof(struct pilot_campaign) == 36) ? 1 : -1];

#pragma pack(push, 1)

struct pilot_campaign_mission {
	/* Campaign the mission was last flown in, by its id in the campaign
	 * list (mission_description_ids[MISSION_DIRECTORY_CAMPAIGNS]); the medals
	 * and achievements pages match it. */
	int campaign_id;
	/* Times the mission was flown in a campaign, counted up by
	 * fe_disk_io_commit_flight_results. mission_setup_load_mission_list marks a
	 * campaign mission entry unavailable while the single-player count is
	 * 0, or in network play while factions 0 and 1 both have a multiplayer
	 * count of 0. */
	int number_times_flown;
	/* 1 once the mission was completed with g_game_config.difficulty no
	 * higher than GAME_DIFFICULTY_HARD and waves not CRAFT_WAVES_UNLIMITED;
	 * the campaign medals page draws its award. */
	int award_eligible;
	/* Highest score of a flight with waves not CRAFT_WAVES_UNLIMITED; only
	 * fe_disk_io_commit_flight_results reads it, to compare. */
	unsigned int best_score;
	/* Best award won, 1 the best to 5, or 6 for a failed one: an award
	 * replaces it when lower or when the field is 0. On a replacement in
	 * sp_campaign_missions fe_disk_io_commit_flight_results moves one of the
	 * faction's mission_evaluations counts from the old level to the new; in
	 * mp_campaign_missions every award other than 6 a flight wins adds one to
	 * that count, replaced or not, and a replaced 6 comes off it. A flight
	 * with no award clears a 6 and takes it off that count. */
	int award_level;
	/* Shortest nonzero completion time of the player's team, in seconds;
	 * only fe_disk_io_commit_flight_results reads it, to compare. */
	int best_time;
	/* 1 once a flight of the mission was completed; unlocks the cutscenes
	 * shown after it. */
	int is_completed;
	/* No code reads or writes it by name; it is saved with the pilot
	 * file. */
	int unused1c;
};

#pragma pack(pop)
typedef char xvt_size_pilot_campaign_mission
	[(sizeof(struct pilot_campaign_mission) == 32) ? 1 : -1];

#pragma pack(push, 1)

/* Accumulated combat statistics indexed by the game's three mission types. */
struct pilot_stats {
	int total_score_per_mt[3]; ///< Accumulated score by mission type (3).
	int standalone_missions_played_per_mt
		[3]; ///< Standalone missions played by mission type (3).
	int sequence_missions_played_per_mt
		[3]; ///< Sequence missions played by mission type (3).
	int total_kills_per_mt[3]; ///< Full kills by mission type (3).
	int total_friendlies_killed_per_mt
		[3]; ///< Friendly kills by mission type (3).
	int kills_per_craft_per_mt
		[3][100]; ///< Full kills by mission type and craft type (100).
	int kills_shared_per_craft_per_mt
		[3]
		[100]; ///< Shared kills by mission type and craft type (100).
	int kills_assists_per_craft_per_mt
		[3]
		[100]; ///< Kill assists by mission type and craft type (100).
	int kills_full_on_player_rating_per_mt
		[3]
		[25]; ///< Full kills by mission type and victim player rating (25).
	int kills_shared_on_player_rating_per_mt
		[3]
		[25]; ///< Shared kills by mission type and victim player rating (25).
	int kills_assist_on_player_rating_per_mt
		[3]
		[25]; ///< Kill assists by mission type and victim player rating (25).
	int kills_full_on_ai_rating_per_mt
		[3]
		[6]; ///< Full kills by mission type and victim AI rating (6).
	int kills_shared_on_ai_rating_per_mt
		[3]
		[6]; ///< Shared kills by mission type and victim AI rating (6).
	int kills_assist_on_ai_rating_per_mt
		[3]
		[6]; ///< Kill assists by mission type and victim AI rating (6).
	int num_special_inspected_per_mt
		[3]; ///< Special-object inspections by mission type (3).
	int energy_hits_per_mt
		[3]; ///< Combined laser and ion hits by mission type (3).
	int energy_fired_per_mt
		[3]; ///< Combined laser and ion shots fired by mission type (3).
	int warheads_hits_per_mt[3];  ///< Warhead hits by mission type (3).
	int warheads_fired_per_mt[3]; ///< Warheads fired by mission type (3).
	int total_craft_losses_per_mt
		[3]; ///< Total craft losses by mission type (3).
	int losses_by_collisions_per_mt
		[3]; ///< Collision losses by mission type (3).
	int losses_by_starships_per_mt
		[3]; ///< Losses to starships by mission type (3).
	int losses_by_mines_per_mt[3]; ///< Losses to mines by mission type (3).
	int killed_by_player_rating_per_mt
		[3]
		[25]; ///< Deaths by mission type and opposing player rating (25).
	int killed_by_ai_rating_per_mt
		[3][6]; ///< Deaths by mission type and opposing AI rating (6).
};

#pragma pack(pop)
typedef char
	xvt_size_pilot_stats[(sizeof(struct pilot_stats) == 5256) ? 1 : -1];

#pragma pack(push, 1)

struct pilot_faction {
	int total_missions_played_count; ///< Total missions played for this faction.
	/* Team last chosen for this faction's missions, saved from g_pilot_data
	 * when a mission launches (frontend_mission_init_player_state, entry 2 in
	 * network play) and by the mission setup screens, and copied back into
	 * g_pilot_data when this faction or the pilot is selected. */
	int team;
	/* Mission directory last chosen for this faction, saved and restored
	 * like team. */
	mission_directory_id mission_directory_id;
	/* Mission or sequence last chosen in each directory, saved and restored
	 * like team; a single-player launch saves only the entries of the
	 * current directory's kind. */
	int mission_description_ids[6];
	/* No code reads or writes it by name; it is saved with the pilot
	 * file. */
	uint8_t unused24[32];
	/* Persisted active-sequence flag mirrored to
	 * pilot_data.mission_sequence_active. */
	int mission_sequence_active;
	/* pilot_data.saved_mission_description_id for this faction, saved and
	 * restored like team. */
	int saved_mission_description_id;
	int melee_plaques[6]; ///< Melee plaque counts by award level 1-6.
	int tournament_trophies
		[6]; ///< Tournament trophy counts by award level 1-6.
	int mission_evaluations
		[6]; ///< Mission evaluation counts by award level 1-6.
	int battle_medallions
		[6]; ///< Battle medallion counts by award level 1-6.
	int mission_awards
		[4]; ///< Current mission award levels: melee, tournament, evaluation, battle.
	/* pilot_load_xvt_record and pilot_write_xvt_record copy it from and to the
	 * base-game record; nothing else reads it. */
	uint8_t field_bc[16];
	int total_score; ///< Overall score for this faction.
	struct pilot_stats
		stats; ///< Accumulated combat statistics for this faction.
	/* pilot_load_xvt_record and pilot_write_xvt_record copy the base-game
	 * record's history blocks from and to 4 bytes before each array below:
	 * the first block from here, each later one from the last field of the
	 * array before it. So in that record each history entry starts with the
	 * 4 bytes this layout gives to the end of the entry before it, and
	 * mp_battles[24].field24 is not copied. No code reads these words by
	 * name. */
	uint8_t field1558[4];
	struct pilot_mission sp_training_missions
		[100]; ///< Single-player training history (100 records).
	struct pilot_mission sp_melee_missions
		[250]; ///< Single-player melee history (250 records).
	struct pilot_mission sp_combat_missions
		[250]; ///< Single-player combat history (250 records).
	struct pilot_multiplayer_mission mp_training_missions
		[100]; ///< Multiplayer training history (100 records).
	struct pilot_multiplayer_mission mp_melee_missions
		[250]; ///< Multiplayer melee history (250 records).
	struct pilot_multiplayer_mission mp_combat_missions
		[250]; ///< Multiplayer combat history (250 records).
	struct pilot_tournament sp_tournaments
		[25]; ///< Single-player tournament history (25 records).
	struct pilot_multiplayer_tournament mp_tournaments
		[25]; ///< Multiplayer tournament history (25 records).
	struct pilot_battle
		sp_battles[25]; ///< Single-player battle history (25 records).
	struct pilot_multiplayer_battle
		mp_battles[25]; ///< Multiplayer battle history (25 records).
	struct pilot_campaign sp_campaigns
		[25]; ///< Single-player campaign history (25 records).
	struct pilot_campaign mp_campaigns
		[25]; ///< Multiplayer campaign history (25 records).
	/* No code reads or writes it by name; it is saved with the pilot
	 * file. */
	/* Unresolved bytes between multiplayer campaign history and the CD
	 * movie-check counter. */
	uint8_t unused_f0e4[24];
	/* Nothing uses it now; the 1997 concourse kept the count of its CD
	 * movie checks in entry 0's. */
	uint32_t
		cd_movie_check_counter; ///< Concourse CD movie-check retry counter.
	/* Single-player campaign mission history indexed by one-based mission
	 * ID 1-99. */
	struct pilot_campaign_mission sp_campaign_missions[99];
	/* No code reads or writes it by name; it is saved with the pilot
	 * file. */
	uint8_t unused_fd60
		[32]; ///< Unresolved bytes preceding multiplayer campaign mission history.
	/* Multiplayer campaign mission history indexed by one-based mission ID
	 * 1-99. */
	struct pilot_campaign_mission mp_campaign_missions[99];
};

#pragma pack(pop)
typedef char xvt_size_pilot_faction[(sizeof(struct pilot_faction) == 68064)
					    ? 1
					    : -1];

#pragma pack(push, 1)

struct pilot_data {
	/* The pilot's name; empty when no pilot is loaded. pilot_create_new sets
	 * it; loading a pilot file fills it. */
	char name[14];
	/* The pilot's score over every mission, raised by each mission's score
	 * in fe_disk_io_commit_flight_results; the pilot statistics page shows
	 * it. */
	int total_score;
	/* The local player's DirectPlay id, recorded by mission_debrief_update
	 * when a next or replayed mission starts; only pilot_write_xvt_record
	 * reads it, into the base-game record. */
	int local_player_id;
	/* Set to 1 by mission_debrief_update when a next or replayed mission
	 * starts, and by mission_setup_try_continue_battle and
	 * mission_setup_try_continue_campaign; only pilot_write_xvt_record reads it,
	 * into the base-game record. */
	int launch_session_marker;
	/* net_is_host(), or 1 in single player, recorded by
	 * mission_debrief_update when a next or replayed mission starts; only
	 * pilot_write_xvt_record reads it, into the base-game record. */
	int is_host;
	/* Human players in the mission being launched:
	 * flight_loading_update_ready_screen sets the ready player count before
	 * each flight, and mission_debrief_update sets 1 in single player or
	 * net_count_ready_players() when a next or replayed mission starts. The
	 * flight code reads it for that flight's rules, such as the time limit,
	 * AI balance and dynamic music. */
	unsigned int num_human_players_last_mission;
	/* g_frontend_mission_session_mode, recorded by mission_debrief_update when
	 * a next or replayed mission starts; only pilot_write_xvt_record reads
	 * it, into the base-game record. */
	int session_mode;
	/* Opaque 672-byte payload round-tripped by the XvT-compatible
	 * pilot-record reader and writer. */
	uint8_t xvt_record_payload[672];
	/* Team the pilot flies for in the mission being set up. Many functions
	 * write it, chiefly mission_setup_team_assignment_update and the other
	 * mission setup screens; selecting a pilot or a faction restores it
	 * from faction_statistics. */
	int team;
	/* Mission directory being played (a MISSION_DIRECTORY_ value). While a
	 * tournament, battle or campaign is played it holds the directory its
	 * missions come from: melees, combat engagements or training exercises.
	 * Many functions write it, chiefly the MissionSetup screens and
	 * sequence functions. */
	mission_directory_id mission_directory_id;
	/* Selected mission or sequence descriptor ID for each of the six
	 * mission_directory_id values. */
	int32_t mission_description_ids[6];
	/* Name of the network game. The host screen edits it (an empty one
	 * becomes the pilot's name plus FRONTSTR_470_S_GAME);
	 * frontend_net_join_game_screen copies the joined session's and
	 * frontend_net_process_network_packets the host's lobby state's.
	 * pilot_create_new sets that default. */
	char multiplayer_game_name[32];
	/* Game name the pilot last hosted under: frontend_net_host_game_screen
	 * copies it into multiplayer_game_name when the host screen opens and
	 * saves the name back when it hosts. Starts as the default game
	 * name. */
	char multiplayer_host_name[32];
	/* 1 while a tournament, battle or campaign is played, else 0. Many
	 * functions write it, chiefly the MissionSetup screens and sequence
	 * functions; selecting a pilot or a faction restores it from
	 * faction_statistics. */
	int mission_sequence_active;
	/* The entry of mission_description_ids for the directory a tournament,
	 * battle or campaign plays its missions from (melees, combat
	 * engagements or training exercises), saved when the sequence's first
	 * or next mission is selected, or on a client when the host's mission
	 * start arrives, before that mission's id replaces the entry.
	 * mission_setup_update, while a sequence is active, and
	 * mission_setup_enter_next_mission copy it back into that entry. Saved and
	 * restored with faction_statistics like mission_sequence_active. */
	int saved_mission_description_id;
	/* Promotion points earned at the current rating. Below the cap, Officer
	 * 1st Class for training missions and Jedi Master for SIMULATOR_2
	 * training, melee and combat, fe_disk_io_commit_flight_results adds each
	 * flight's rating_promo_points and, while current_rating_worse_promo_points
	 * is under half the rating's threshold, its worse_rating_promo_points; at
	 * the cap only negative points count. Reaching
	 * g_pilot_rating_promotion_point_thresholds[rating] promotes the pilot
	 * (training then sets 0, melee and combat subtract the threshold).
	 * Below -2000 (PROMOTION_LOSS_LIMIT) a pilot above target drone is
	 * demoted and it is set to 0. */
	int current_rating_promo_points;
	/* Worse-rating promotion points counted at the current rating, also
	 * added to current_rating_promo_points; fe_disk_io_commit_flight_results adds
	 * a flight's only while this is under half the rating's threshold, and
	 * sets 0 on a promotion or demotion. */
	int current_rating_worse_promo_points;
	/* Persisted signed rank change: -1 demotion, 0 unchanged, +1
	 * promotion. */
	pilot_promotion_delta promotion_delta;
	/* Progress to the next rating after the last mission: 100 *
	 * current_rating_promo_points / the rating's threshold, at most 100, or
	 * for negative points 100 * points / 2000, at least -100; 0 after a
	 * demotion, and for a target drone with negative points. The pilot
	 * statistics page shows it below Jedi Master. */
	int next_promotion_percent;
	struct pilot_stats main_stats; ///< Pilot combat statistics.
	/* Progress and team standings of the tournament being played. Many
	 * functions write it, chiefly the MissionSetup sequence functions,
	 * fe_disk_io_commit_flight_results and mission_debrief_update. */
	struct melee_tournament_sequence_state melee_tournament_sequence_state;
	/* Progress and mission results of the battle being played. Many
	 * functions write it, chiefly the MissionSetup sequence functions,
	 * fe_disk_io_commit_flight_results and mission_debrief_update. */
	struct battle_sequence_state battle_sequence_state;
	/* The pilot's rating, PILOT_RATING_TARGET_DRONE (0) to
	 * PILOT_RATING_JEDI_MASTER (24); a new pilot starts as a trainee.
	 * fe_disk_io_commit_flight_results raises and lowers it. */
	pilot_rating rating;
	/* Missions the pilot has flown, counted up by
	 * fe_disk_io_commit_flight_results; recorded in rating_achieved_on_mission. */
	int total_missions_played_count;
	/* Per rating, total_missions_played_count when
	 * fe_disk_io_commit_flight_results promoted the pilot to it, or demoted the
	 * pilot to target drone or ground crew; the pilot rating page shows
	 * it. */
	int rating_achieved_on_mission[25];
	/* Name of the pilot's rating: the trainee string for a new pilot; after
	 * each flight xvt_launch_task_complete sets it from rating before
	 * saving the pilot. Many screens show it. */
	char rating_name[32];
	/* The local player's score in the last mission, its mission score plus
	 * its team's bonus score, set by fe_disk_io_commit_flight_results; 0 at
	 * launch. */
	int mission_score;
	/* Per network player slot, the local player's full kills of that player
	 * in the last mission, copied by fe_disk_io_commit_flight_results; cleared
	 * at launch and when a mission is replayed or flown again. */
	int kills_full_on_player[8];
	/* Per network player slot, the local player's shared kills of that
	 * player in the last mission; cleared at launch and when a mission is
	 * replayed or flown again. */
	int kills_shared_on_player[8];
	/* Per flight group, the local player's full kills of it in the last
	 * mission, copied by fe_disk_io_commit_flight_results; cleared at launch
	 * and when a mission is replayed or flown again. */
	int kills_full_on_flight_group[48];
	/* Per flight group, the local player's shared kills of it in the last
	 * mission; cleared at launch and when a mission is replayed or flown
	 * again. */
	int kills_shared_on_flight_group[48];
	/* Per network player slot, that player's full kills of the local player
	 * in the last mission; cleared at launch and when a mission is replayed
	 * or flown again. */
	int kills_full_from_player[8];
	/* Per network player slot, that player's shared kills of the local
	 * player in the last mission; cleared at launch and when a mission is
	 * replayed or flown again. */
	int kills_shared_from_player[8];
	/* Per flight group, its full kills of the local player in the last
	 * mission; cleared at launch and when a mission is replayed or flown
	 * again. */
	int kills_full_from_flight_group[48];
	/* Per flight group, its shared kills of the local player in the last
	 * mission; cleared at launch and when a mission is replayed or flown
	 * again. */
	int kills_shared_from_flight_group[48];
	/* Per flight group, the rating its AI pilots are shown with in a melee:
	 * g_flight_group_rating_base_by_ai_level of its AI level plus, for a nonzero
	 * level, the flight group index & 3. fe_disk_io_commit_flight_results sets
	 * it for a melee outside a sequence or on a tournament's first
	 * mission. */
	int flight_group_rating[48];
	/* Statistics of the most recent mission, shown on the debriefing. */
	struct pilot_stats last_mission_stats;
	struct pilot_network_player
		network_players[8];  ///< Persisted network-player results (8).
	struct pilot_team teams[10]; ///< Persisted team results (10).
	/* 0 Rebel, 1 Imperial, as the pilot record's faction buttons set it. At
	 * launch frontend_mission_init_player_state sets it: in a melee or
	 * tournament 0 when the local player's craft is type 1 to 4 or 14, else
	 * 1; otherwise the IFF of the local player's flight group. Entry 2 of
	 * faction_statistics keeps network play's selections. */
	int current_faction_id; ///< Selected faction-statistics record.
	struct pilot_faction faction_statistics
		[4]; ///< Per-faction pilot records (4 records, 0x109E0 bytes each).
	struct campaign_sequence_state
		campaign_sequence_state; ///< Runtime state for an active campaign sequence.
	struct battle_continuation sp_battle_continuations
		[25]; ///< Saved single-player battle continuation slots indexed by battle ID.
	struct battle_continuation mp_battle_continuations
		[25]; ///< Saved multiplayer battle continuation slots indexed by battle ID.
	/* Saved single-player campaign continuation slots indexed by campaign
	 * ID. */
	struct campaign_continuation sp_campaign_continuations[25];
	/* Saved multiplayer campaign continuation slots; client state uses the
	 * ID+12 partition. */
	struct campaign_continuation mp_campaign_continuations[25];
};

#pragma pack(pop)
typedef char
	xvt_size_pilot_data[(sizeof(struct pilot_data) == 296238) ? 1 : -1];

extern struct pilot_data g_pilot_data;
extern unsigned int g_campaign_award_sprite_count;
extern struct campaign_award_sprite_entry *g_campaign_award_sprites;
extern int g_pilot_stats_assists[3];
extern int g_pilot_stats_player_kills[3];
extern int g_pilot_stats_losses_to_non_players[3];
extern int g_pilot_achievements_scroll_offset;
extern int g_pilot_sp_training_history_count;
extern int g_pilot_sp_melee_history_count;
extern int g_pilot_sp_combat_history_count;
extern int g_pilot_sp_tournament_history_count;
extern int g_pilot_sp_battle_history_count;
extern int g_pilot_sp_campaign_history_row_count;
extern int g_pilot_mp_training_history_count;
extern int g_pilot_mp_melee_history_count;
extern int g_pilot_mp_combat_history_count;
extern int g_pilot_mp_tournament_history_count;
extern int g_pilot_mp_battle_history_count;
extern int g_pilot_stats_has_craft_kills_by_type;
extern int g_pilot_stats_row_has_data;
extern int g_pilot_stats_has_losses_to_players_by_rank;
extern int g_pilot_statistics_scroll_offset;
extern int g_pilot_stats_non_player_kills_shared[3];
extern int g_pilot_stats_total_kills_shared[3];
extern int g_pilot_stats_non_player_kills[3];
extern int g_pilot_stats_has_player_kills_by_rating;
extern int g_pilot_stats_player_kills_shared[3];
extern int g_pilot_record_page_row_count;
extern int g_pilot_stats_losses_to_players[3];
extern int g_pilot_mp_campaign_history_row_count;

int pilot_record_update_pilot_selection_panel(int frame_counter);
int pilot_record_draw_pilot_list(const struct RECT *bounds,
				 int first_visible_index);
int pilot_record_rebuild_pilot_list(int *selected_index);
int pilot_record_draw_pilot_statistics_page(void);
int pilot_record_draw_mission_achievements_page(void);
int pilot_record_draw_cutscene_viewer_page(void);
int pilot_record_draw_campaign_medals_page(void);
int pilot_record_draw_pilot_awards_page(void);
int pilot_record_draw_pilot_rating_page(void);
int pilot_record_update_navigation_controls(void);
int pilot_record_redraw_background(void);
int pilot_record_load_campaign_award_sprite_table(const char *file_name);

#ifdef __cplusplus
}
#endif

#endif
