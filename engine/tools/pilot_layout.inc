/* One row per member of the pilot record's structs, in declaration order,
 * for pilot_dump's generic printers. Written from the engine's struct
 * definitions; offsets and sizes come from the compiler. The base game's
 * three record structs are defined in pilot.c, not in a header, so they are
 * repeated here, packed, and checked against the sizes the engine asserts. A
 * member added to one of these structs must be added here as well. */
#pragma pack(push, 1)
struct pilot_xvt_stats {
	int total_score_per_mt[3];
	int standalone_missions_played_per_mt[3];
	int sequence_missions_played_per_mt[3];
	int total_kills_per_mt[3];
	int total_friendlies_killed_per_mt[3];
	int kills_per_craft_per_mt[3][88];
	int kills_shared_per_craft_per_mt[3][88];
	int kills_assists_per_craft_per_mt[3][88];
	int kills_full_on_player_rating_per_mt[3][25];
	int kills_shared_on_player_rating_per_mt[3][25];
	int kills_assist_on_player_rating_per_mt[3][25];
	int kills_full_on_ai_rating_per_mt[3][6];
	int kills_shared_on_ai_rating_per_mt[3][6];
	int kills_assist_on_ai_rating_per_mt[3][6];
	int num_special_inspected_per_mt[3];
	int energy_hits_per_mt[3];
	int energy_fired_per_mt[3];
	int warheads_hits_per_mt[3];
	int warheads_fired_per_mt[3];
	int total_craft_losses_per_mt[3];
	int losses_by_collisions_per_mt[3];
	int losses_by_starships_per_mt[3];
	int losses_by_mines_per_mt[3];
	int killed_by_player_rating_per_mt[3][25];
	int killed_by_ai_rating_per_mt[3][6];
};
struct pilot_xvt_faction {
	int total_missions_played_count;
	uint8_t selection_state[68];
	int melee_plaques[6];
	int tournament_trophies[6];
	int mission_evaluations[6];
	int battle_medallions[6];
	int mission_awards[4];
	uint8_t field_bc[16];
	int total_score;
	struct pilot_xvt_stats stats;
	uint8_t sp_training_data[3600];
	uint8_t sp_melee_data[9000];
	uint8_t sp_combat_data[9000];
	uint8_t mp_training_data[4800];
	uint8_t mp_melee_data[12000];
	uint8_t mp_combat_data[12000];
	uint8_t sp_tournament_data[1000];
	uint8_t mp_tournament_data[1100];
	uint8_t sp_battle_data[900];
	uint8_t mp_battle_data[1000];
};
struct pilot_xvt_record {
	char name[14];
	int total_score;
	int local_player_id;
	int launch_session_marker;
	int is_host;
	unsigned int num_human_players_last_mission;
	int session_mode;
	uint8_t xvt_record_combat_payload[320];
	uint8_t xvt_record_identity_payload[32];
	uint8_t xvt_record_object_payload[320];
	uint8_t legacy_rating_state[100];
	int current_rating_promo_points;
	int current_rating_worse_promo_points;
	pilot_promotion_delta promotion_delta;
	int next_promotion_percent;
	struct pilot_xvt_stats main_stats;
	uint8_t mission_sequence_state[3348];
	pilot_rating rating;
	int total_missions_played_count;
	int rating_achieved_on_mission[25];
	char rating_name[32];
	int mission_score;
	int kills_full_on_player[8];
	int kills_shared_on_player[8];
	int kills_full_on_flight_group[48];
	int kills_shared_on_flight_group[48];
	int kills_full_from_player[8];
	int kills_shared_from_player[8];
	int kills_full_from_flight_group[48];
	int kills_shared_from_flight_group[48];
	int flight_group_rating[48];
	struct pilot_xvt_stats last_mission_stats;
	struct pilot_network_player network_players[8];
	struct pilot_team teams[10];
	int current_faction_id;
	struct pilot_xvt_faction faction_statistics[4];
};
#pragma pack(pop)
_Static_assert(sizeof(struct pilot_data) == 296238,
	       "struct pilot_data is 296238 bytes");
_Static_assert(sizeof(struct pilot_faction) == 68064,
	       "struct pilot_faction is 68064 bytes");
_Static_assert(sizeof(struct pilot_xvt_stats) == 4824,
	       "struct pilot_xvt_stats is 4824 bytes");
_Static_assert(sizeof(struct pilot_xvt_faction) == 59428,
	       "struct pilot_xvt_faction is 59428 bytes");
_Static_assert(sizeof(struct pilot_xvt_record) == 253754,
	       "struct pilot_xvt_record is 253754 bytes");
_Static_assert(sizeof(struct pilot_team) == 28,
	       "struct pilot_team is 28 bytes");
#define MEMBER(s, f, kind, dims, sub)                                          \
	{#f, offsetof(struct s, f), sizeof(((struct s *)0)->f), kind, dims, sub}
enum layout_struct_id {
	LAYOUT_PILOT_NETWORK_PLAYER,
	LAYOUT_PILOT_TEAM,
	LAYOUT_PILOT_MISSION,
	LAYOUT_PILOT_MULTIPLAYER_MISSION,
	LAYOUT_PILOT_TOURNAMENT,
	LAYOUT_PILOT_MULTIPLAYER_TOURNAMENT,
	LAYOUT_PILOT_BATTLE,
	LAYOUT_PILOT_MULTIPLAYER_BATTLE,
	LAYOUT_PILOT_CAMPAIGN,
	LAYOUT_PILOT_CAMPAIGN_MISSION,
	LAYOUT_PILOT_STATS,
	LAYOUT_PILOT_FACTION,
	LAYOUT_MELEE_TOURNAMENT_TEAM_STANDINGS,
	LAYOUT_MELEE_TOURNAMENT_SEQUENCE_STATE,
	LAYOUT_BATTLE_SEQUENCE_STATE,
	LAYOUT_CAMPAIGN_SEQUENCE_STATE,
	LAYOUT_BATTLE_CONTINUATION,
	LAYOUT_CAMPAIGN_CONTINUATION,
	LAYOUT_PILOT_DATA,
	LAYOUT_PILOT_XVT_STATS,
	LAYOUT_PILOT_XVT_FACTION,
	LAYOUT_PILOT_XVT_RECORD,
};
static const struct layout_member g_members_pilot_network_player[] = {
	MEMBER(pilot_network_player, formal_name, "char", "[14]", -1),
	MEMBER(pilot_network_player, friendly_name, "char", "[14]", -1),
	MEMBER(pilot_network_player, flight_group_id, "i32", "-", -1),
	MEMBER(pilot_network_player, direct_play_id, "i32", "-", -1),
	MEMBER(pilot_network_player, rating, "i32", "-", -1),
	MEMBER(pilot_network_player, total_score, "i32", "-", -1),
	MEMBER(pilot_network_player, kills, "i32", "-", -1),
	MEMBER(pilot_network_player, kills_shared, "i32", "-", -1),
	MEMBER(pilot_network_player, craft_inspected, "i32", "-", -1),
	MEMBER(pilot_network_player, kills_assist, "i32", "-", -1),
	MEMBER(pilot_network_player, total_losses, "i32", "-", -1),
	MEMBER(pilot_network_player, craft_id, "i32", "-", -1),
	MEMBER(pilot_network_player, craft_option, "i32", "-", -1),
	MEMBER(pilot_network_player, warhead_option, "i32", "-", -1),
	MEMBER(pilot_network_player, beam_option, "i32", "-", -1),
	MEMBER(pilot_network_player, countermeasure_option, "i32", "-", -1),
	MEMBER(pilot_network_player, has_left, "i32", "-", -1),
};
static const struct layout_member g_members_pilot_team[] = {
	MEMBER(pilot_team, mission_score, "i32", "-", -1),
	MEMBER(pilot_team, is_mission_completed, "i32", "-", -1),
	MEMBER(pilot_team, unknown08, "i32", "-", -1),
	MEMBER(pilot_team, mission_time, "i32", "-", -1),
	MEMBER(pilot_team, kills, "i32", "-", -1),
	MEMBER(pilot_team, kills_shared, "i32", "-", -1),
	MEMBER(pilot_team, losses, "i32", "-", -1),
};
static const struct layout_member g_members_pilot_mission[] = {
	MEMBER(pilot_mission, number_times_flown, "i32", "-", -1),
	MEMBER(pilot_mission, completed_count, "i32", "-", -1),
	MEMBER(pilot_mission, failed_count, "i32", "-", -1),
	MEMBER(pilot_mission, best_score, "i32", "-", -1),
	MEMBER(pilot_mission, best_time, "i32", "-", -1),
	MEMBER(pilot_mission, best_placement, "i32", "-", -1),
	MEMBER(pilot_mission, award_level, "i32", "-", -1),
	MEMBER(pilot_mission, best_margin, "u32", "-", -1),
	MEMBER(pilot_mission, field20, "i32", "-", -1),
};
static const struct layout_member g_members_pilot_multiplayer_mission[] = {
	MEMBER(pilot_multiplayer_mission, number_times_flown, "i32", "-", -1),
	MEMBER(pilot_multiplayer_mission, first_place_count, "i32", "-", -1),
	MEMBER(pilot_multiplayer_mission, second_place_count, "i32", "-", -1),
	MEMBER(pilot_multiplayer_mission, third_place_count, "i32", "-", -1),
	MEMBER(pilot_multiplayer_mission, completed_count, "i32", "-", -1),
	MEMBER(pilot_multiplayer_mission, failed_count, "i32", "-", -1),
	MEMBER(pilot_multiplayer_mission, best_score, "i32", "-", -1),
	MEMBER(pilot_multiplayer_mission, best_time, "i32", "-", -1),
	MEMBER(pilot_multiplayer_mission, best_placement, "i32", "-", -1),
	MEMBER(pilot_multiplayer_mission, award_level, "i32", "-", -1),
	MEMBER(pilot_multiplayer_mission, best_margin, "u32", "-", -1),
	MEMBER(pilot_multiplayer_mission, field2c, "i32", "-", -1),
};
static const struct layout_member g_members_pilot_tournament[] = {
	MEMBER(pilot_tournament, attempt_count, "i32", "-", -1),
	MEMBER(pilot_tournament, completed_count, "i32", "-", -1),
	MEMBER(pilot_tournament, first_place_count, "i32", "-", -1),
	MEMBER(pilot_tournament, second_place_count, "i32", "-", -1),
	MEMBER(pilot_tournament, third_place_count, "i32", "-", -1),
	MEMBER(pilot_tournament, best_score, "i32", "-", -1),
	MEMBER(pilot_tournament, best_placement, "i32", "-", -1),
	MEMBER(pilot_tournament, award_level, "i32", "-", -1),
	MEMBER(pilot_tournament, best_margin, "u32", "-", -1),
	MEMBER(pilot_tournament, field24, "i32", "-", -1),
};
static const struct layout_member g_members_pilot_multiplayer_tournament[] = {
	MEMBER(pilot_multiplayer_tournament, attempt_count, "i32", "-", -1),
	MEMBER(pilot_multiplayer_tournament, completed_count, "i32", "-", -1),
	MEMBER(pilot_multiplayer_tournament, first_place_count, "i32", "-", -1),
	MEMBER(pilot_multiplayer_tournament, second_place_count, "i32", "-",
	       -1),
	MEMBER(pilot_multiplayer_tournament, third_place_count, "i32", "-", -1),
	MEMBER(pilot_multiplayer_tournament, best_score, "i32", "-", -1),
	MEMBER(pilot_multiplayer_tournament, best_placement, "i32", "-", -1),
	MEMBER(pilot_multiplayer_tournament, unused1c, "i32", "-", -1),
	MEMBER(pilot_multiplayer_tournament, award_level, "i32", "-", -1),
	MEMBER(pilot_multiplayer_tournament, best_margin, "u32", "-", -1),
	MEMBER(pilot_multiplayer_tournament, field28, "i32", "-", -1),
};
static const struct layout_member g_members_pilot_battle[] = {
	MEMBER(pilot_battle, attempt_count, "i32", "-", -1),
	MEMBER(pilot_battle, victory_count, "i32", "-", -1),
	MEMBER(pilot_battle, defeat_count, "i32", "-", -1),
	MEMBER(pilot_battle, draw_count, "i32", "-", -1),
	MEMBER(pilot_battle, best_score, "i32", "-", -1),
	MEMBER(pilot_battle, unused14, "i32", "-", -1),
	MEMBER(pilot_battle, award_level, "i32", "-", -1),
	MEMBER(pilot_battle, best_victory_margin, "u32", "-", -1),
	MEMBER(pilot_battle, field20, "i32", "-", -1),
};
static const struct layout_member g_members_pilot_multiplayer_battle[] = {
	MEMBER(pilot_multiplayer_battle, attempt_count, "i32", "-", -1),
	MEMBER(pilot_multiplayer_battle, victory_count, "i32", "-", -1),
	MEMBER(pilot_multiplayer_battle, defeat_count, "i32", "-", -1),
	MEMBER(pilot_multiplayer_battle, draw_count, "i32", "-", -1),
	MEMBER(pilot_multiplayer_battle, best_score, "i32", "-", -1),
	MEMBER(pilot_multiplayer_battle, unused14, "i32", "-", -1),
	MEMBER(pilot_multiplayer_battle, unused18, "i32", "-", -1),
	MEMBER(pilot_multiplayer_battle, award_level, "i32", "-", -1),
	MEMBER(pilot_multiplayer_battle, best_victory_margin, "u32", "-", -1),
	MEMBER(pilot_multiplayer_battle, unused24, "i32", "-", -1),
};
static const struct layout_member g_members_pilot_campaign[] = {
	MEMBER(pilot_campaign, attempt_count, "i32", "-", -1),
	MEMBER(pilot_campaign, next_mission_index, "i32", "-", -1),
	MEMBER(pilot_campaign, is_finished, "i32", "-", -1),
	MEMBER(pilot_campaign, best_score, "i32", "-", -1),
	MEMBER(pilot_campaign, unused10, "i32", "-", -1),
	MEMBER(pilot_campaign, unused14, "i32", "-", -1),
	MEMBER(pilot_campaign, unused18, "i32", "-", -1),
	MEMBER(pilot_campaign, unused1c, "i32", "-", -1),
	MEMBER(pilot_campaign, unused20, "i32", "-", -1),
};
static const struct layout_member g_members_pilot_campaign_mission[] = {
	MEMBER(pilot_campaign_mission, campaign_id, "i32", "-", -1),
	MEMBER(pilot_campaign_mission, number_times_flown, "i32", "-", -1),
	MEMBER(pilot_campaign_mission, award_eligible, "i32", "-", -1),
	MEMBER(pilot_campaign_mission, best_score, "u32", "-", -1),
	MEMBER(pilot_campaign_mission, award_level, "i32", "-", -1),
	MEMBER(pilot_campaign_mission, best_time, "i32", "-", -1),
	MEMBER(pilot_campaign_mission, is_completed, "i32", "-", -1),
	MEMBER(pilot_campaign_mission, unused1c, "i32", "-", -1),
};
static const struct layout_member g_members_pilot_stats[] = {
	MEMBER(pilot_stats, total_score_per_mt, "i32", "[3]", -1),
	MEMBER(pilot_stats, standalone_missions_played_per_mt, "i32", "[3]",
	       -1),
	MEMBER(pilot_stats, sequence_missions_played_per_mt, "i32", "[3]", -1),
	MEMBER(pilot_stats, total_kills_per_mt, "i32", "[3]", -1),
	MEMBER(pilot_stats, total_friendlies_killed_per_mt, "i32", "[3]", -1),
	MEMBER(pilot_stats, kills_per_craft_per_mt, "i32", "[3][100]", -1),
	MEMBER(pilot_stats, kills_shared_per_craft_per_mt, "i32", "[3][100]",
	       -1),
	MEMBER(pilot_stats, kills_assists_per_craft_per_mt, "i32", "[3][100]",
	       -1),
	MEMBER(pilot_stats, kills_full_on_player_rating_per_mt, "i32",
	       "[3][25]", -1),
	MEMBER(pilot_stats, kills_shared_on_player_rating_per_mt, "i32",
	       "[3][25]", -1),
	MEMBER(pilot_stats, kills_assist_on_player_rating_per_mt, "i32",
	       "[3][25]", -1),
	MEMBER(pilot_stats, kills_full_on_ai_rating_per_mt, "i32", "[3][6]",
	       -1),
	MEMBER(pilot_stats, kills_shared_on_ai_rating_per_mt, "i32", "[3][6]",
	       -1),
	MEMBER(pilot_stats, kills_assist_on_ai_rating_per_mt, "i32", "[3][6]",
	       -1),
	MEMBER(pilot_stats, num_special_inspected_per_mt, "i32", "[3]", -1),
	MEMBER(pilot_stats, energy_hits_per_mt, "i32", "[3]", -1),
	MEMBER(pilot_stats, energy_fired_per_mt, "i32", "[3]", -1),
	MEMBER(pilot_stats, warheads_hits_per_mt, "i32", "[3]", -1),
	MEMBER(pilot_stats, warheads_fired_per_mt, "i32", "[3]", -1),
	MEMBER(pilot_stats, total_craft_losses_per_mt, "i32", "[3]", -1),
	MEMBER(pilot_stats, losses_by_collisions_per_mt, "i32", "[3]", -1),
	MEMBER(pilot_stats, losses_by_starships_per_mt, "i32", "[3]", -1),
	MEMBER(pilot_stats, losses_by_mines_per_mt, "i32", "[3]", -1),
	MEMBER(pilot_stats, killed_by_player_rating_per_mt, "i32", "[3][25]",
	       -1),
	MEMBER(pilot_stats, killed_by_ai_rating_per_mt, "i32", "[3][6]", -1),
};
static const struct layout_member g_members_pilot_faction[] = {
	MEMBER(pilot_faction, total_missions_played_count, "i32", "-", -1),
	MEMBER(pilot_faction, team, "i32", "-", -1),
	MEMBER(pilot_faction, mission_directory_id, "i32", "-", -1),
	MEMBER(pilot_faction, mission_description_ids, "i32", "[6]", -1),
	MEMBER(pilot_faction, unused24, "u8", "[32]", -1),
	MEMBER(pilot_faction, mission_sequence_active, "i32", "-", -1),
	MEMBER(pilot_faction, saved_mission_description_id, "i32", "-", -1),
	MEMBER(pilot_faction, melee_plaques, "i32", "[6]", -1),
	MEMBER(pilot_faction, tournament_trophies, "i32", "[6]", -1),
	MEMBER(pilot_faction, mission_evaluations, "i32", "[6]", -1),
	MEMBER(pilot_faction, battle_medallions, "i32", "[6]", -1),
	MEMBER(pilot_faction, mission_awards, "i32", "[4]", -1),
	MEMBER(pilot_faction, field_bc, "u8", "[16]", -1),
	MEMBER(pilot_faction, total_score, "i32", "-", -1),
	MEMBER(pilot_faction, stats, "pilot_stats", "-", LAYOUT_PILOT_STATS),
	MEMBER(pilot_faction, field1558, "u8", "[4]", -1),
	MEMBER(pilot_faction, sp_training_missions, "pilot_mission", "[100]",
	       LAYOUT_PILOT_MISSION),
	MEMBER(pilot_faction, sp_melee_missions, "pilot_mission", "[250]",
	       LAYOUT_PILOT_MISSION),
	MEMBER(pilot_faction, sp_combat_missions, "pilot_mission", "[250]",
	       LAYOUT_PILOT_MISSION),
	MEMBER(pilot_faction, mp_training_missions, "pilot_multiplayer_mission",
	       "[100]", LAYOUT_PILOT_MULTIPLAYER_MISSION),
	MEMBER(pilot_faction, mp_melee_missions, "pilot_multiplayer_mission",
	       "[250]", LAYOUT_PILOT_MULTIPLAYER_MISSION),
	MEMBER(pilot_faction, mp_combat_missions, "pilot_multiplayer_mission",
	       "[250]", LAYOUT_PILOT_MULTIPLAYER_MISSION),
	MEMBER(pilot_faction, sp_tournaments, "pilot_tournament", "[25]",
	       LAYOUT_PILOT_TOURNAMENT),
	MEMBER(pilot_faction, mp_tournaments, "pilot_multiplayer_tournament",
	       "[25]", LAYOUT_PILOT_MULTIPLAYER_TOURNAMENT),
	MEMBER(pilot_faction, sp_battles, "pilot_battle", "[25]",
	       LAYOUT_PILOT_BATTLE),
	MEMBER(pilot_faction, mp_battles, "pilot_multiplayer_battle", "[25]",
	       LAYOUT_PILOT_MULTIPLAYER_BATTLE),
	MEMBER(pilot_faction, sp_campaigns, "pilot_campaign", "[25]",
	       LAYOUT_PILOT_CAMPAIGN),
	MEMBER(pilot_faction, mp_campaigns, "pilot_campaign", "[25]",
	       LAYOUT_PILOT_CAMPAIGN),
	MEMBER(pilot_faction, unused_f0e4, "u8", "[24]", -1),
	MEMBER(pilot_faction, cd_movie_check_counter, "u32", "-", -1),
	MEMBER(pilot_faction, sp_campaign_missions, "pilot_campaign_mission",
	       "[99]", LAYOUT_PILOT_CAMPAIGN_MISSION),
	MEMBER(pilot_faction, unused_fd60, "u8", "[32]", -1),
	MEMBER(pilot_faction, mp_campaign_missions, "pilot_campaign_mission",
	       "[99]", LAYOUT_PILOT_CAMPAIGN_MISSION),
};
static const struct layout_member g_members_melee_tournament_team_standings[] =
	{
		MEMBER(melee_tournament_team_standings,
		       ai_opponent_source_team_and_type_flag, "i32", "-", -1),
		MEMBER(melee_tournament_team_standings, total_score, "i32", "-",
		       -1),
		MEMBER(melee_tournament_team_standings, first_place_count,
		       "i32", "-", -1),
		MEMBER(melee_tournament_team_standings, second_place_count,
		       "i32", "-", -1),
		MEMBER(melee_tournament_team_standings, third_place_count,
		       "i32", "-", -1),
};
static const struct layout_member g_members_melee_tournament_sequence_state[] =
	{
		MEMBER(melee_tournament_sequence_state, reserved, "i32", "[9]",
		       -1),
		MEMBER(melee_tournament_sequence_state, current_mission_index,
		       "i32", "-", -1),
		MEMBER(melee_tournament_sequence_state, mission_count, "i32",
		       "-", -1),
		MEMBER(melee_tournament_sequence_state, team_standings,
		       "melee_tournament_team_standings", "[10]",
		       LAYOUT_MELEE_TOURNAMENT_TEAM_STANDINGS),
		MEMBER(melee_tournament_sequence_state, human_player_count,
		       "u32", "-", -1),
		MEMBER(melee_tournament_sequence_state,
		       participating_team_count, "i32", "-", -1),
		MEMBER(melee_tournament_sequence_state, ai_boost_first_team,
		       "i32", "-", -1),
};
static const struct layout_member g_members_battle_sequence_state[] = {
	MEMBER(battle_sequence_state, current_mission_index, "u32", "-", -1),
	MEMBER(battle_sequence_state, current_mission_id, "i32", "-", -1),
	MEMBER(battle_sequence_state, victories_needed, "i32", "-", -1),
	MEMBER(battle_sequence_state, mission_results, "i32", "[10]", -1),
	MEMBER(battle_sequence_state, mission_ordinals, "i32", "[10]", -1),
	MEMBER(battle_sequence_state, mission_list_indices, "i32", "[10]", -1),
	MEMBER(battle_sequence_state, human_player_count, "u32", "-", -1),
	MEMBER(battle_sequence_state, cumulative_score, "i32", "-", -1),
};
static const struct layout_member g_members_campaign_sequence_state[] = {
	MEMBER(campaign_sequence_state, unused00, "i32", "-", -1),
	MEMBER(campaign_sequence_state, current_mission_index, "i32", "-", -1),
	MEMBER(campaign_sequence_state, mission_count, "i32", "-", -1),
	MEMBER(campaign_sequence_state, last_mission_completed, "i32", "-", -1),
	MEMBER(campaign_sequence_state, human_player_count, "i32", "-", -1),
	MEMBER(campaign_sequence_state, cumulative_score, "i32", "-", -1),
};
static const struct layout_member g_members_battle_continuation[] = {
	MEMBER(battle_continuation, unused00, "i32", "-", -1),
	MEMBER(battle_continuation, random_seed, "u32", "-", -1),
	MEMBER(battle_continuation, is_active, "i32", "-", -1),
	MEMBER(battle_continuation, battle_length_index, "i32", "-", -1),
	MEMBER(battle_continuation, random_setup, "i32", "-", -1),
	MEMBER(battle_continuation, sequence_state, "battle_sequence_state",
	       "-", LAYOUT_BATTLE_SEQUENCE_STATE),
};
static const struct layout_member g_members_campaign_continuation[] = {
	MEMBER(campaign_continuation, unused00, "i32", "-", -1),
	MEMBER(campaign_continuation, random_seed, "u32", "-", -1),
	MEMBER(campaign_continuation, is_active, "i32", "-", -1),
	MEMBER(campaign_continuation, random_setup, "i32", "-", -1),
	MEMBER(campaign_continuation, sequence_state, "campaign_sequence_state",
	       "-", LAYOUT_CAMPAIGN_SEQUENCE_STATE),
};
static const struct layout_member g_members_pilot_data[] = {
	MEMBER(pilot_data, name, "char", "[14]", -1),
	MEMBER(pilot_data, total_score, "i32", "-", -1),
	MEMBER(pilot_data, local_player_id, "i32", "-", -1),
	MEMBER(pilot_data, launch_session_marker, "i32", "-", -1),
	MEMBER(pilot_data, is_host, "i32", "-", -1),
	MEMBER(pilot_data, num_human_players_last_mission, "u32", "-", -1),
	MEMBER(pilot_data, session_mode, "i32", "-", -1),
	MEMBER(pilot_data, xvt_record_payload, "u8", "[672]", -1),
	MEMBER(pilot_data, team, "i32", "-", -1),
	MEMBER(pilot_data, mission_directory_id, "i32", "-", -1),
	MEMBER(pilot_data, mission_description_ids, "i32", "[6]", -1),
	MEMBER(pilot_data, multiplayer_game_name, "char", "[32]", -1),
	MEMBER(pilot_data, multiplayer_host_name, "char", "[32]", -1),
	MEMBER(pilot_data, mission_sequence_active, "i32", "-", -1),
	MEMBER(pilot_data, saved_mission_description_id, "i32", "-", -1),
	MEMBER(pilot_data, current_rating_promo_points, "i32", "-", -1),
	MEMBER(pilot_data, current_rating_worse_promo_points, "i32", "-", -1),
	MEMBER(pilot_data, promotion_delta, "i32", "-", -1),
	MEMBER(pilot_data, next_promotion_percent, "i32", "-", -1),
	MEMBER(pilot_data, main_stats, "pilot_stats", "-", LAYOUT_PILOT_STATS),
	MEMBER(pilot_data, melee_tournament_sequence_state,
	       "melee_tournament_sequence_state", "-",
	       LAYOUT_MELEE_TOURNAMENT_SEQUENCE_STATE),
	MEMBER(pilot_data, battle_sequence_state, "battle_sequence_state", "-",
	       LAYOUT_BATTLE_SEQUENCE_STATE),
	MEMBER(pilot_data, rating, "i32", "-", -1),
	MEMBER(pilot_data, total_missions_played_count, "i32", "-", -1),
	MEMBER(pilot_data, rating_achieved_on_mission, "i32", "[25]", -1),
	MEMBER(pilot_data, rating_name, "char", "[32]", -1),
	MEMBER(pilot_data, mission_score, "i32", "-", -1),
	MEMBER(pilot_data, kills_full_on_player, "i32", "[8]", -1),
	MEMBER(pilot_data, kills_shared_on_player, "i32", "[8]", -1),
	MEMBER(pilot_data, kills_full_on_flight_group, "i32", "[48]", -1),
	MEMBER(pilot_data, kills_shared_on_flight_group, "i32", "[48]", -1),
	MEMBER(pilot_data, kills_full_from_player, "i32", "[8]", -1),
	MEMBER(pilot_data, kills_shared_from_player, "i32", "[8]", -1),
	MEMBER(pilot_data, kills_full_from_flight_group, "i32", "[48]", -1),
	MEMBER(pilot_data, kills_shared_from_flight_group, "i32", "[48]", -1),
	MEMBER(pilot_data, flight_group_rating, "i32", "[48]", -1),
	MEMBER(pilot_data, last_mission_stats, "pilot_stats", "-",
	       LAYOUT_PILOT_STATS),
	MEMBER(pilot_data, network_players, "pilot_network_player", "[8]",
	       LAYOUT_PILOT_NETWORK_PLAYER),
	MEMBER(pilot_data, teams, "pilot_team", "[10]", LAYOUT_PILOT_TEAM),
	MEMBER(pilot_data, current_faction_id, "i32", "-", -1),
	MEMBER(pilot_data, faction_statistics, "pilot_faction", "[4]",
	       LAYOUT_PILOT_FACTION),
	MEMBER(pilot_data, campaign_sequence_state, "campaign_sequence_state",
	       "-", LAYOUT_CAMPAIGN_SEQUENCE_STATE),
	MEMBER(pilot_data, sp_battle_continuations, "battle_continuation",
	       "[25]", LAYOUT_BATTLE_CONTINUATION),
	MEMBER(pilot_data, mp_battle_continuations, "battle_continuation",
	       "[25]", LAYOUT_BATTLE_CONTINUATION),
	MEMBER(pilot_data, sp_campaign_continuations, "campaign_continuation",
	       "[25]", LAYOUT_CAMPAIGN_CONTINUATION),
	MEMBER(pilot_data, mp_campaign_continuations, "campaign_continuation",
	       "[25]", LAYOUT_CAMPAIGN_CONTINUATION),
};
static const struct layout_member g_members_pilot_xvt_stats[] = {
	MEMBER(pilot_xvt_stats, total_score_per_mt, "i32", "[3]", -1),
	MEMBER(pilot_xvt_stats, standalone_missions_played_per_mt, "i32", "[3]",
	       -1),
	MEMBER(pilot_xvt_stats, sequence_missions_played_per_mt, "i32", "[3]",
	       -1),
	MEMBER(pilot_xvt_stats, total_kills_per_mt, "i32", "[3]", -1),
	MEMBER(pilot_xvt_stats, total_friendlies_killed_per_mt, "i32", "[3]",
	       -1),
	MEMBER(pilot_xvt_stats, kills_per_craft_per_mt, "i32", "[3][88]", -1),
	MEMBER(pilot_xvt_stats, kills_shared_per_craft_per_mt, "i32", "[3][88]",
	       -1),
	MEMBER(pilot_xvt_stats, kills_assists_per_craft_per_mt, "i32",
	       "[3][88]", -1),
	MEMBER(pilot_xvt_stats, kills_full_on_player_rating_per_mt, "i32",
	       "[3][25]", -1),
	MEMBER(pilot_xvt_stats, kills_shared_on_player_rating_per_mt, "i32",
	       "[3][25]", -1),
	MEMBER(pilot_xvt_stats, kills_assist_on_player_rating_per_mt, "i32",
	       "[3][25]", -1),
	MEMBER(pilot_xvt_stats, kills_full_on_ai_rating_per_mt, "i32", "[3][6]",
	       -1),
	MEMBER(pilot_xvt_stats, kills_shared_on_ai_rating_per_mt, "i32",
	       "[3][6]", -1),
	MEMBER(pilot_xvt_stats, kills_assist_on_ai_rating_per_mt, "i32",
	       "[3][6]", -1),
	MEMBER(pilot_xvt_stats, num_special_inspected_per_mt, "i32", "[3]", -1),
	MEMBER(pilot_xvt_stats, energy_hits_per_mt, "i32", "[3]", -1),
	MEMBER(pilot_xvt_stats, energy_fired_per_mt, "i32", "[3]", -1),
	MEMBER(pilot_xvt_stats, warheads_hits_per_mt, "i32", "[3]", -1),
	MEMBER(pilot_xvt_stats, warheads_fired_per_mt, "i32", "[3]", -1),
	MEMBER(pilot_xvt_stats, total_craft_losses_per_mt, "i32", "[3]", -1),
	MEMBER(pilot_xvt_stats, losses_by_collisions_per_mt, "i32", "[3]", -1),
	MEMBER(pilot_xvt_stats, losses_by_starships_per_mt, "i32", "[3]", -1),
	MEMBER(pilot_xvt_stats, losses_by_mines_per_mt, "i32", "[3]", -1),
	MEMBER(pilot_xvt_stats, killed_by_player_rating_per_mt, "i32",
	       "[3][25]", -1),
	MEMBER(pilot_xvt_stats, killed_by_ai_rating_per_mt, "i32", "[3][6]",
	       -1),
};
static const struct layout_member g_members_pilot_xvt_faction[] = {
	MEMBER(pilot_xvt_faction, total_missions_played_count, "i32", "-", -1),
	MEMBER(pilot_xvt_faction, selection_state, "u8", "[68]", -1),
	MEMBER(pilot_xvt_faction, melee_plaques, "i32", "[6]", -1),
	MEMBER(pilot_xvt_faction, tournament_trophies, "i32", "[6]", -1),
	MEMBER(pilot_xvt_faction, mission_evaluations, "i32", "[6]", -1),
	MEMBER(pilot_xvt_faction, battle_medallions, "i32", "[6]", -1),
	MEMBER(pilot_xvt_faction, mission_awards, "i32", "[4]", -1),
	MEMBER(pilot_xvt_faction, field_bc, "u8", "[16]", -1),
	MEMBER(pilot_xvt_faction, total_score, "i32", "-", -1),
	MEMBER(pilot_xvt_faction, stats, "pilot_xvt_stats", "-",
	       LAYOUT_PILOT_XVT_STATS),
	MEMBER(pilot_xvt_faction, sp_training_data, "u8", "[3600]", -1),
	MEMBER(pilot_xvt_faction, sp_melee_data, "u8", "[9000]", -1),
	MEMBER(pilot_xvt_faction, sp_combat_data, "u8", "[9000]", -1),
	MEMBER(pilot_xvt_faction, mp_training_data, "u8", "[4800]", -1),
	MEMBER(pilot_xvt_faction, mp_melee_data, "u8", "[12000]", -1),
	MEMBER(pilot_xvt_faction, mp_combat_data, "u8", "[12000]", -1),
	MEMBER(pilot_xvt_faction, sp_tournament_data, "u8", "[1000]", -1),
	MEMBER(pilot_xvt_faction, mp_tournament_data, "u8", "[1100]", -1),
	MEMBER(pilot_xvt_faction, sp_battle_data, "u8", "[900]", -1),
	MEMBER(pilot_xvt_faction, mp_battle_data, "u8", "[1000]", -1),
};
static const struct layout_member g_members_pilot_xvt_record[] = {
	MEMBER(pilot_xvt_record, name, "char", "[14]", -1),
	MEMBER(pilot_xvt_record, total_score, "i32", "-", -1),
	MEMBER(pilot_xvt_record, local_player_id, "i32", "-", -1),
	MEMBER(pilot_xvt_record, launch_session_marker, "i32", "-", -1),
	MEMBER(pilot_xvt_record, is_host, "i32", "-", -1),
	MEMBER(pilot_xvt_record, num_human_players_last_mission, "u32", "-",
	       -1),
	MEMBER(pilot_xvt_record, session_mode, "i32", "-", -1),
	MEMBER(pilot_xvt_record, xvt_record_combat_payload, "u8", "[320]", -1),
	MEMBER(pilot_xvt_record, xvt_record_identity_payload, "u8", "[32]", -1),
	MEMBER(pilot_xvt_record, xvt_record_object_payload, "u8", "[320]", -1),
	MEMBER(pilot_xvt_record, legacy_rating_state, "u8", "[100]", -1),
	MEMBER(pilot_xvt_record, current_rating_promo_points, "i32", "-", -1),
	MEMBER(pilot_xvt_record, current_rating_worse_promo_points, "i32", "-",
	       -1),
	MEMBER(pilot_xvt_record, promotion_delta, "i32", "-", -1),
	MEMBER(pilot_xvt_record, next_promotion_percent, "i32", "-", -1),
	MEMBER(pilot_xvt_record, main_stats, "pilot_xvt_stats", "-",
	       LAYOUT_PILOT_XVT_STATS),
	MEMBER(pilot_xvt_record, mission_sequence_state, "u8", "[3348]", -1),
	MEMBER(pilot_xvt_record, rating, "i32", "-", -1),
	MEMBER(pilot_xvt_record, total_missions_played_count, "i32", "-", -1),
	MEMBER(pilot_xvt_record, rating_achieved_on_mission, "i32", "[25]", -1),
	MEMBER(pilot_xvt_record, rating_name, "char", "[32]", -1),
	MEMBER(pilot_xvt_record, mission_score, "i32", "-", -1),
	MEMBER(pilot_xvt_record, kills_full_on_player, "i32", "[8]", -1),
	MEMBER(pilot_xvt_record, kills_shared_on_player, "i32", "[8]", -1),
	MEMBER(pilot_xvt_record, kills_full_on_flight_group, "i32", "[48]", -1),
	MEMBER(pilot_xvt_record, kills_shared_on_flight_group, "i32", "[48]",
	       -1),
	MEMBER(pilot_xvt_record, kills_full_from_player, "i32", "[8]", -1),
	MEMBER(pilot_xvt_record, kills_shared_from_player, "i32", "[8]", -1),
	MEMBER(pilot_xvt_record, kills_full_from_flight_group, "i32", "[48]",
	       -1),
	MEMBER(pilot_xvt_record, kills_shared_from_flight_group, "i32", "[48]",
	       -1),
	MEMBER(pilot_xvt_record, flight_group_rating, "i32", "[48]", -1),
	MEMBER(pilot_xvt_record, last_mission_stats, "pilot_xvt_stats", "-",
	       LAYOUT_PILOT_XVT_STATS),
	MEMBER(pilot_xvt_record, network_players, "pilot_network_player", "[8]",
	       LAYOUT_PILOT_NETWORK_PLAYER),
	MEMBER(pilot_xvt_record, teams, "pilot_team", "[10]",
	       LAYOUT_PILOT_TEAM),
	MEMBER(pilot_xvt_record, current_faction_id, "i32", "-", -1),
	MEMBER(pilot_xvt_record, faction_statistics, "pilot_xvt_faction", "[4]",
	       LAYOUT_PILOT_XVT_FACTION),
};
#undef MEMBER
static const struct layout_struct g_layout[] = {
	{"pilot_network_player", sizeof(struct pilot_network_player),
	 g_members_pilot_network_player,
	 COUNT_OF(g_members_pilot_network_player)},
	{"pilot_team", sizeof(struct pilot_team), g_members_pilot_team,
	 COUNT_OF(g_members_pilot_team)},
	{"pilot_mission", sizeof(struct pilot_mission), g_members_pilot_mission,
	 COUNT_OF(g_members_pilot_mission)},
	{"pilot_multiplayer_mission", sizeof(struct pilot_multiplayer_mission),
	 g_members_pilot_multiplayer_mission,
	 COUNT_OF(g_members_pilot_multiplayer_mission)},
	{"pilot_tournament", sizeof(struct pilot_tournament),
	 g_members_pilot_tournament, COUNT_OF(g_members_pilot_tournament)},
	{"pilot_multiplayer_tournament",
	 sizeof(struct pilot_multiplayer_tournament),
	 g_members_pilot_multiplayer_tournament,
	 COUNT_OF(g_members_pilot_multiplayer_tournament)},
	{"pilot_battle", sizeof(struct pilot_battle), g_members_pilot_battle,
	 COUNT_OF(g_members_pilot_battle)},
	{"pilot_multiplayer_battle", sizeof(struct pilot_multiplayer_battle),
	 g_members_pilot_multiplayer_battle,
	 COUNT_OF(g_members_pilot_multiplayer_battle)},
	{"pilot_campaign", sizeof(struct pilot_campaign),
	 g_members_pilot_campaign, COUNT_OF(g_members_pilot_campaign)},
	{"pilot_campaign_mission", sizeof(struct pilot_campaign_mission),
	 g_members_pilot_campaign_mission,
	 COUNT_OF(g_members_pilot_campaign_mission)},
	{"pilot_stats", sizeof(struct pilot_stats), g_members_pilot_stats,
	 COUNT_OF(g_members_pilot_stats)},
	{"pilot_faction", sizeof(struct pilot_faction), g_members_pilot_faction,
	 COUNT_OF(g_members_pilot_faction)},
	{"melee_tournament_team_standings",
	 sizeof(struct melee_tournament_team_standings),
	 g_members_melee_tournament_team_standings,
	 COUNT_OF(g_members_melee_tournament_team_standings)},
	{"melee_tournament_sequence_state",
	 sizeof(struct melee_tournament_sequence_state),
	 g_members_melee_tournament_sequence_state,
	 COUNT_OF(g_members_melee_tournament_sequence_state)},
	{"battle_sequence_state", sizeof(struct battle_sequence_state),
	 g_members_battle_sequence_state,
	 COUNT_OF(g_members_battle_sequence_state)},
	{"campaign_sequence_state", sizeof(struct campaign_sequence_state),
	 g_members_campaign_sequence_state,
	 COUNT_OF(g_members_campaign_sequence_state)},
	{"battle_continuation", sizeof(struct battle_continuation),
	 g_members_battle_continuation,
	 COUNT_OF(g_members_battle_continuation)},
	{"campaign_continuation", sizeof(struct campaign_continuation),
	 g_members_campaign_continuation,
	 COUNT_OF(g_members_campaign_continuation)},
	{"pilot_data", sizeof(struct pilot_data), g_members_pilot_data,
	 COUNT_OF(g_members_pilot_data)},
	{"pilot_xvt_stats", sizeof(struct pilot_xvt_stats),
	 g_members_pilot_xvt_stats, COUNT_OF(g_members_pilot_xvt_stats)},
	{"pilot_xvt_faction", sizeof(struct pilot_xvt_faction),
	 g_members_pilot_xvt_faction, COUNT_OF(g_members_pilot_xvt_faction)},
	{"pilot_xvt_record", sizeof(struct pilot_xvt_record),
	 g_members_pilot_xvt_record, COUNT_OF(g_members_pilot_xvt_record)},
};
