#include "xvt/flight/fediskio.h"

#ifdef XVT_MODERN
#include "xvt_runtime/snapshot/cockpit_messages.h"
#include "xvt_runtime/snapshot/render_assets.h"
#endif
#include <stdlib.h>
#include <string.h>

#include "xvt/assets/model_bounds.h"
#include "xvt/assets/model_mesh.h"
#include "xvt/assets/object_type.h"
#include "xvt/assets/opt_model.h"
#include "xvt/assets/string_table.h"
#include "xvt/audio/fsfx.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/flight_display.h"
#include "xvt/flight/flight_input.h"
#include "xvt/flight/flight_surface.h"
#include "xvt/flight/hud/flight_map.h"
#include "xvt/flight/hud/flight_text.h"
#include "xvt/flight/hud/hud.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/frontend_mission_list.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt/render/flight_palette.h"
#include "xvt/render/flight_sw.h"
#include "xvt/render/image_quantizer.h"
#include "xvt/render/render_list.h"
#include "xvt/render/render_scene.h"
#include "xvt/render/renderer.h"
#include "xvt/render/tex_level.h"
#include "xvt/util/memory.h"
#include "xvt_runtime/log/log_both_builds.h"

#ifndef XVT_MODERN
struct msvc42_file_prefix {
	/* Stream bytes before flags; nothing names them. */
	uint8_t reserved[12];
	int flags; /* Flag word; bit 0x20 set marks a failed stream. */
};
#endif

/* Path of the file fe_disk_io_open_global_stream opened or tried last: in the
 * original build the last path tried, in the modern one the path storage
 * resolved. Only that function writes it; the retry prompt,
 * fe_disk_io_close_global_stream (to delete a failed file) and fe_disk_io_fatal_error
 * read it. */
// GLOBAL: XVT 0x9D8A60
char g_file_name[256] = {0};
/* Locked memory of g_flight_scratch_screen_buffer_handle, one screen of pixels,
 * also where rotated sprites are drawn. Two functions write it:
 * fe_disk_io_init_global_buffers, which fills it with color 0x40, and
 * fe_disk_io_lock_global_buffers. */
// GLOBAL: XVT 0x9A8C34
uint8_t *g_flight_scratch_screen_buffer = NULL;
/* Copy of g_flight_aux_buffer, set by fe_disk_io_init_global_buffers and
 * fe_disk_io_lock_global_buffers. Nothing reads it. */
// GLOBAL: XVT 0x9D8C1C
uint8_t *g_flight_aux_buffer_mirror = NULL;
/* 1 when the last fe_disk_io_read_with_retry_prompt came up short (in the original
 * build only when the player gave up), else 0. Only that function writes it,
 * and nothing else reads it. */
// GLOBAL: XVT 0x9A20AC
uint16_t g_file_read_abort_flag = 0;
/* The disk, display and network status strings, indexed by disk_io_string_id.
 * string_table_load_game_strings points them into the loaded strings, after the
 * file error messages; this file and the flight network code read them. */
// GLOBAL: XVT 0xA609C0
char *g_str_disk_io_messages[32] = {0};
/* The file fe_disk_io_open_global_stream opened last, which callers read through
 * and fe_disk_io_close_global_stream closes; NULL after a failed open. Many
 * functions write it, chiefly fe_disk_io_open_global_stream; some put a stream they
 * kept back into it before closing it. */
// GLOBAL: XVT 0xA07BD8
xvt_file *g_stream = NULL;
/* "newpal.act", the palette file read at flight start (flight_main_loop,
 * xvt_flight_loading_palette) and when fe_disk_io_init_resources generates a
 * palette. */
// GLOBAL: XVT 0x5236B0
char g_flight_palette_resource_file_name[12] = {
	'n', 'e', 'w', 'p', 'a', 'l', '.', 'a', 'c', 't', '\0', '\0',
};
/* Nothing sets it, so it stays 0 and fe_disk_io_init_resources always clears
 * g_generate_mission_palette. */
// GLOBAL: XVT 0x527508
unsigned int g_palette_generation_enabled = 0;
/* The three resource list names, SPEC, SPEC2 and SPEC3, that
 * fe_disk_io_load_resources reads; the index is an object type's texture_group. */
// GLOBAL: XVT 0x527620
static char g_spec_list_prefixes[3][9] = {
	{'S', 'P', 'E', 'C', '\0', '\0', '\0', '\0', '\0'},
	{'S', 'P', 'E', 'C', '2', '\0', '\0', '\0', '\0'},
	{'S', 'P', 'E', 'C', '3', '\0', '\0', '\0', '\0'},
};
/* Memory handle of the warhead guidance pool, locked into
 * g_projectile_guidance_states; 0 when there is none. mission_init allocates it;
 * fe_disk_io_init_global_buffers, fe_disk_io_free_flight_resources and the modern
 * build's xvt_flight_loading_reset set it to 0. */
// GLOBAL: XVT 0x999400
uint16_t g_warhead_guidance_pool_handle = 0;
/* Memory handle of the craft record pool, locked into g_craft_data_pool_base; 0
 * when there is none. Set and cleared like g_warhead_guidance_pool_handle. */
// GLOBAL: XVT 0x999402
uint16_t g_craft_data_pool_handle = 0;
/* Memory handle of the mobile object pool, locked into g_mobile_object_pool_base;
 * 0 when there is none. Set and cleared like g_warhead_guidance_pool_handle. */
// GLOBAL: XVT 0x999404
uint16_t g_mobile_object_pool_handle = 0;
/* Memory handle of one screen of pixels, locked into g_flight_aux_buffer.
 * fe_disk_io_init_global_buffers allocates it and fe_disk_io_free_flight_resources
 * frees it; only the modern build sets it back to 0. */
// GLOBAL: XVT 0x999406
uint16_t g_flight_aux_buffer_handle = 0;
/* Memory handle of the character data pool, locked into
 * g_mobile_object_char_data_pool; 0 when there is none. Set and cleared like
 * g_warhead_guidance_pool_handle. */
// GLOBAL: XVT 0x999408
uint16_t g_mobile_object_char_data_handle = 0;
/* Memory handle of the object table, locked into g_object_table; 0 when there is
 * none. Set and cleared like g_warhead_guidance_pool_handle. */
// GLOBAL: XVT 0x99940A
uint16_t g_object_table_handle = 0;
/* Memory handle of the loaded strings: 32,000 bytes from
 * fe_disk_io_init_global_buffers, replaced by string_table_load_game_strings with a
 * larger one when strings.txt does not fit. Freed by
 * fe_disk_io_free_flight_resources. */
// GLOBAL: XVT 0x9D7670
uint16_t g_string_data_handle = 0;
/* Memory handle of the render object list, 296 entries, locked into
 * g_render_object_list_entries. Allocated by fe_disk_io_init_global_buffers, freed by
 * fe_disk_io_free_flight_resources. */
// GLOBAL: XVT 0x612B94
uint16_t g_render_object_list_handle = 0;
/* Memory handle of one screen of pixels, locked into
 * g_flight_scratch_screen_buffer. Allocated by fe_disk_io_init_global_buffers, freed
 * by fe_disk_io_free_flight_resources. */
// GLOBAL: XVT 0x612B98
uint16_t g_flight_scratch_screen_buffer_handle = 0;
/* Palette index for each 16-bit RGB565 color, 65,536 bytes. In 8-bit color
 * fe_disk_io_init_resources points g_active_rgb565_to_palette_index_lut at it and fills
 * it from an .inv file or builds it. */
// GLOBAL: XVT 0x612BA0
uint8_t g_rgb565_to_palette_index_lut[UINT16_MAX + 1u] = {0};
/* Memory handle of a 34,600-byte font, locked into g_flight_font_small_sw, which
 * holds MICRO48.FNT. Allocated by fe_disk_io_init_global_buffers, freed by
 * fe_disk_io_free_flight_resources. */
// GLOBAL: XVT 0x622BA0
uint16_t g_flight_small_font_handle = 0;
/* Memory handle of one screen of pixels, locked into g_flight_offscreen_buffer.
 * Allocated by fe_disk_io_init_global_buffers, freed by
 * fe_disk_io_free_flight_resources. */
// GLOBAL: XVT 0x622BA4
uint16_t g_flight_offscreen_buffer_handle = 0;
/* Memory handle of a 20,600-byte font, locked into g_flight_font_micro_sw, which
 * holds MICRO32.FNT. Allocated by fe_disk_io_init_global_buffers, freed by
 * fe_disk_io_free_flight_resources. */
// GLOBAL: XVT 0x622BA8
uint16_t g_flight_micro_font_handle = 0;
/* Memory handle of a 34,600-byte font, locked into g_flight_font_medium_sw, which
 * holds MICRO64.FNT at 640x480 and 480x360. Allocated by
 * fe_disk_io_init_global_buffers, freed by fe_disk_io_free_flight_resources. */
// GLOBAL: XVT 0x622BAC
uint16_t g_flight_medium_font_handle = 0;
/* Base rating fe_disk_io_commit_flight_results stores for each flight group of a
 * melee, by the group's AI level 0 to 6; a nonzero level adds the group index &
 * 3. */
// GLOBAL: XVT 0x527510
const int g_flight_group_rating_base_by_ai_level[7] = {3, 4, 4, 8, 12, 14, 0};
/* Promotion points the player needs at each rating to be promoted; also the 100
 * percent mark of next_promotion_percent. */
// GLOBAL: XVT 0x52752C
const int g_pilot_rating_promotion_point_thresholds[25] = {
	250,  500,  750,  1250, 1750,  2250,  2750,  3250, 3750,
	4250, 4750, 5250, 5750, 6250,  6500,  6500,  7000, 7250,
	7500, 7750, 8000, 9000, 10000, 11000, 11000,
};
/* Award for a melee or tournament by team count and placement: entry 3 * teams
 * - 4 + placement, for placements 1 to 3; 0 means none. */
// GLOBAL: XVT 0x5275A8
const uint8_t g_placement_award_levels[24] = {
	0, 0, 0, 5, 0, 0, 5, 0, 0, 4, 5, 0, 3, 4, 0, 2, 3, 5, 1, 2, 4, 1, 2, 3,
};
/* Winning scores for a multiplayer combat award: 50,000 or more gives award 1,
 * each lower step one more, and below 10,000 none. */
// GLOBAL: XVT 0x5275C4
const int g_mission_award_win_thresholds[5] = {50000, 40000, 30000, 20000,
					       10000};
/* Score steps for a training or single-player combat award: 50,000, 20,000 and
 * 0 give awards 1, 2 and 3, a lower score 4, before the difficulty adds 2
 * (easy) or 1 (medium). */
// GLOBAL: XVT 0x5275E4
const int g_mission_award_score_thresholds[3] = {50000, 20000, 0};

/* Records the flight just ended into the career record g_pilot_data, in memory
 * only, and returns 0; the two arguments are ignored. Does nothing for
 * MISSION_TYPE_SIMULATOR_1. Sorts the flight as training (training and
 * simulator 2), melee or combat. In combat, a team with no player flight group
 * can be credited with its primary goal when the players' team missed its own.
 * Outside a melee, players of a team that met its primary goal score 80 times
 * the point value of each craft their flight group still had to send, unless
 * its supply is unlimited or the player's craft mode is not
 * CRAFT_WAVES_DEFAULT. The local player's score (mission score plus team bonus)
 * and every kill, loss, shot and hit tally go into the faction, main and
 * last-mission statistics. Promotion points then raise or lower the rating
 * against g_pilot_rating_promotion_point_thresholds; below -2000 points demote.
 * Then per-player and per-flight-group kills, melee flight group ratings, team
 * results and network players' totals are copied; an award is worked out from
 * score, placement, difficulty and craft mode; mission bests, times,
 * completions and awards go into the single-player tables with one human
 * connected, else the multiplayer ones; and in a mission sequence the campaign,
 * melee tournament or battle standings, bests and medallions are updated. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x498400
int16_t fe_disk_io_commit_flight_results(int unused1, int unused2)
{
	enum {
		PLAYER_COUNT = sizeof(g_players) / sizeof(g_players[0]),
		TEAM_COUNT = sizeof(g_flight_mission_state.runtime
					    .team_goal_status) /
			     sizeof(g_flight_mission_state.runtime
					    .team_goal_status[0]),
		FLIGHT_GROUP_CAPACITY = sizeof(g_mission_flight_groups) /
					sizeof(g_mission_flight_groups[0]),
		OBJECT_TYPE_STAT_COUNT =
			sizeof(g_pilot_data.last_mission_stats
				       .kills_per_craft_per_mt[0]) /
			sizeof(g_pilot_data.last_mission_stats
				       .kills_per_craft_per_mt[0][0]),
		PLAYER_RATING_COUNT =
			sizeof(g_players[0]
				       .per_mission_kills
				       .kills_full_on_player_rating) /
			sizeof(g_players[0]
				       .per_mission_kills
				       .kills_full_on_player_rating[0]),
		AI_RATING_COUNT = sizeof(g_players[0]
						 .per_mission_kills
						 .kills_full_on_ai_rating) /
				  sizeof(g_players[0]
						 .per_mission_kills
						 .kills_full_on_ai_rating[0]),
		TEAM_KILL_STAT_FULL = 0,
		TEAM_KILL_STAT_SHARED = 1,
		TEAM_KILL_STAT_LOSSES = 3,
		TEAM_GOAL_PRIMARY = 0,
		TEAM_GOAL_PREVENT = 1,
		MISSION_STAT_TRAINING = 0,
		MISSION_STAT_MELEE = 1,
		MISSION_STAT_COMBAT = 2,
		FAILED_AWARD = 6,
		FIRST_PLACE = 1,
		MAX_STORED_AWARD = 5,
		PROMOTION_LOSS_LIMIT = -2000,
		MAX_PROMOTION_PERCENT = 100,
		UNLIMITED_WAVE_COUNT = 99,
		WAVE_REPLACEMENT_SCORE_FACTOR = 80,
	};

	(void)unused1;
	(void)unused2;

	int connected_human_count = 0;
	unsigned int player_idx;
	for (player_idx = 0; player_idx < PLAYER_COUNT; ++player_idx) {
		if (g_players[player_idx].network.direct_play_id != 0) {
			++connected_human_count;
		}
	}

	uint8_t active_team_fg_count[TEAM_COUNT];
	memset(active_team_fg_count, 0, sizeof(active_team_fg_count));
	unsigned int fg_idx;
	for (fg_idx = 0;
	     fg_idx < (unsigned int)g_mission_header.num_flight_groups;
	     ++fg_idx) {
		if (g_mission_flight_groups[fg_idx].fg.player_number != 0 &&
		    g_mission_fg_stats[fg_idx].has_arrived != 0) {
			++active_team_fg_count[g_mission_flight_groups[fg_idx]
						       .fg.team];
		}
	}
	unsigned int active_team_count = 0;
	unsigned int team_idx;
	for (team_idx = 0; team_idx < TEAM_COUNT; ++team_idx) {
		if (active_team_fg_count[team_idx] != 0) {
			++active_team_count;
		}
	}
	XVT_LOG_DEBUG(
		"results.flight_counted type=%d humans=%d teams=%u sequence=%d local=%d difficulty=%d waves=%d tick=%d",
		(int)g_mission_header.mission_type, connected_human_count,
		active_team_count, g_pilot_data.mission_sequence_active,
		g_local_player, (int)g_flight_mission_state.difficulty,
		(int)g_flight_mission_state.player_flight_group_wave_mode,
		g_game_time);

	if (g_mission_header.mission_type != MISSION_TYPE_SIMULATOR_1) {
		int stat_type;
		if (g_mission_header.mission_type == MISSION_TYPE_TRAINING ||
		    g_mission_header.mission_type == MISSION_TYPE_SIMULATOR_2) {
			stat_type = MISSION_STAT_TRAINING;
		} else if (g_mission_header.mission_type ==
			   MISSION_TYPE_MELEE) {
			stat_type = MISSION_STAT_MELEE;
		} else {
			stat_type = MISSION_STAT_COMBAT;
			int team1_player_fg_count = 0;
			int team0_player_fg_count = 0;
			for (fg_idx = 0;
			     fg_idx <
			     (unsigned int)g_mission_header.num_flight_groups;
			     ++fg_idx) {
				if (g_mission_flight_groups[fg_idx]
						    .fg.player_number != 0 &&
				    g_mission_flight_groups[fg_idx]
						    .player_owner_idx != -1) {
					if (g_mission_flight_groups[fg_idx]
						    .fg.team == 0) {
						++team0_player_fg_count;
					} else if (g_mission_flight_groups
							   [fg_idx]
								   .fg.team ==
						   1) {
						++team1_player_fg_count;
					}
				}
			}
			if (g_pilot_data.mission_sequence_active == 1) {
				if (team1_player_fg_count == 0) {
					if (g_flight_mission_state.runtime.team_goal_status
							    [0]
							    [TEAM_GOAL_PRIMARY] !=
						    1 &&
					    g_flight_mission_state.runtime.team_goal_status
							    [1]
							    [TEAM_GOAL_PRIMARY] ==
						    0) {
						g_flight_mission_state.runtime
							.team_goal_status
								[1]
								[TEAM_GOAL_PRIMARY] =
							1;
						XVT_LOG_DEBUG(
							"results.goal_credited team=1 other=0 why=\"sequence\" primary=%d prevent=%d",
							(int)g_flight_mission_state
								.runtime
								.team_goal_status
									[0]
									[TEAM_GOAL_PRIMARY],
							(int)g_flight_mission_state
								.runtime
								.team_goal_status
									[0]
									[TEAM_GOAL_PREVENT]);
					}
				} else if (
					team0_player_fg_count == 0 &&
					g_flight_mission_state.runtime.team_goal_status
							[1]
							[TEAM_GOAL_PRIMARY] !=
						1 &&
					g_flight_mission_state.runtime.team_goal_status
							[0]
							[TEAM_GOAL_PRIMARY] ==
						0) {
					g_flight_mission_state.runtime
						.team_goal_status
							[0][TEAM_GOAL_PRIMARY] =
						1;
					XVT_LOG_DEBUG(
						"results.goal_credited team=0 other=1 why=\"sequence\" primary=%d prevent=%d",
						(int)g_flight_mission_state
							.runtime
							.team_goal_status
								[1]
								[TEAM_GOAL_PRIMARY],
						(int)g_flight_mission_state
							.runtime
							.team_goal_status
								[1]
								[TEAM_GOAL_PREVENT]);
				}
			} else if (connected_human_count == 1) {
				if (team1_player_fg_count == 0) {
					if (g_flight_mission_state.runtime
						    .team_goal_status
							    [0]
							    [TEAM_GOAL_PRIMARY] !=
					    1) {
						g_flight_mission_state.runtime
							.team_goal_status
								[0]
								[TEAM_GOAL_PREVENT] =
							1;
						g_flight_mission_state.runtime
							.team_goal_status
								[1]
								[TEAM_GOAL_PRIMARY] =
							1;
						XVT_LOG_DEBUG(
							"results.goal_credited team=1 other=0 why=\"solo\" primary=%d prevent=%d",
							(int)g_flight_mission_state
								.runtime
								.team_goal_status
									[0]
									[TEAM_GOAL_PRIMARY],
							(int)g_flight_mission_state
								.runtime
								.team_goal_status
									[0]
									[TEAM_GOAL_PREVENT]);
					}
				} else if (
					team0_player_fg_count == 0 &&
					g_flight_mission_state.runtime.team_goal_status
							[1]
							[TEAM_GOAL_PRIMARY] !=
						1) {
					g_flight_mission_state.runtime
						.team_goal_status
							[1][TEAM_GOAL_PREVENT] =
						1;
					g_flight_mission_state.runtime
						.team_goal_status
							[0][TEAM_GOAL_PRIMARY] =
						1;
					XVT_LOG_DEBUG(
						"results.goal_credited team=0 other=1 why=\"solo\" primary=%d prevent=%d",
						(int)g_flight_mission_state
							.runtime
							.team_goal_status
								[1]
								[TEAM_GOAL_PRIMARY],
						(int)g_flight_mission_state
							.runtime
							.team_goal_status
								[1]
								[TEAM_GOAL_PREVENT]);
				}
			}
		}

		unsigned int network_idx;
		for (player_idx = 0; player_idx < PLAYER_COUNT; ++player_idx) {
			if (g_players[player_idx].network.direct_play_id == 0) {
				continue;
			}
			for (network_idx = 0; network_idx < PLAYER_COUNT;
			     ++network_idx) {
				if (g_pilot_data.network_players[network_idx]
					    .direct_play_id !=
				    g_players[player_idx]
					    .network.direct_play_id) {
					continue;
				}
				unsigned int player_fg_idx =
					g_players[network_idx]
						.bound_flight_group_idx;
				if (g_flight_mission_state.runtime.team_goal_status
						    [g_players[network_idx]
							     .team]
						    [TEAM_GOAL_PRIMARY] != 1 ||
				    stat_type == MISSION_STAT_MELEE ||
				    g_flight_mission_state
						    .player_flight_group_wave_mode !=
					    CRAFT_WAVES_DEFAULT ||
				    g_mission_flight_groups[player_fg_idx]
						    .fg.number_of_waves ==
					    UNLIMITED_WAVE_COUNT) {
					continue;
				}
				int remaining_craft_count =
					g_mission_flight_groups[player_fg_idx]
						.fg.number_of_craft *
					g_mission_fg_stats[player_fg_idx]
						.waves_remaining;
				unsigned int object_type =
					g_craft_type_to_object_type
						[g_mission_flight_groups
							 [player_fg_idx]
								 .fg
								 .craft_type];
				unsigned int model_index =
					get_model_index_from_type(object_type);
				g_players[network_idx]
					.mission_stats.mission_score +=
					WAVE_REPLACEMENT_SCORE_FACTOR *
					remaining_craft_count *
					g_model_defs[model_index]
						.craft_point_value;
				XVT_LOG_DEBUG(
					"results.wave_bonus slot=%u entry=%u fg=%u remaining=%d bonus=%d score=%d",
					player_idx, network_idx, player_fg_idx,
					remaining_craft_count,
					WAVE_REPLACEMENT_SCORE_FACTOR *
						remaining_craft_count *
						g_model_defs[model_index]
							.craft_point_value,
					g_players[network_idx]
						.mission_stats.mission_score);
			}
		}

		memset(&g_pilot_data.last_mission_stats, 0,
		       sizeof(g_pilot_data.last_mission_stats));
		int local_player_team =
			(uint16_t)g_players[g_local_player].team;
		int score =
			g_players[g_local_player].mission_stats.mission_score +
			g_flight_mission_state.runtime
				.team_scores[TEAM_SCORE_BONUS]
					    [local_player_team];
		++g_pilot_data.total_missions_played_count;
		++g_pilot_data
			  .faction_statistics[g_pilot_data.current_faction_id]
			  .total_missions_played_count;
		g_pilot_data.faction_statistics[g_pilot_data.current_faction_id]
			.stats.total_score_per_mt[stat_type] += score;
		g_pilot_data.main_stats.total_score_per_mt[stat_type] += score;
		if (g_pilot_data.mission_sequence_active == 0) {
			++g_pilot_data
				  .faction_statistics
					  [g_pilot_data.current_faction_id]
				  .stats
				  .standalone_missions_played_per_mt[stat_type];
			++g_pilot_data.main_stats
				  .standalone_missions_played_per_mt[stat_type];
		} else {
			++g_pilot_data
				  .faction_statistics
					  [g_pilot_data.current_faction_id]
				  .stats
				  .sequence_missions_played_per_mt[stat_type];
			++g_pilot_data.main_stats
				  .sequence_missions_played_per_mt[stat_type];
		}

		for (fg_idx = 0;
		     fg_idx < (unsigned int)g_mission_header.num_flight_groups;
		     ++fg_idx) {
			unsigned int object_type = g_craft_type_to_object_type
				[g_mission_flight_groups[fg_idx].fg.craft_type];
			if (object_type >= OBJECT_TYPE_STAT_COUNT) {
				continue;
			}
			uint16_t kills =
				g_players[g_local_player]
					.per_mission_kills
					.kills_full_on_flight_group[fg_idx];
			g_pilot_data
				.faction_statistics[g_pilot_data
							    .current_faction_id]
				.stats.total_kills_per_mt[stat_type] += kills;
			g_pilot_data.main_stats.total_kills_per_mt[stat_type] +=
				kills;
			g_pilot_data.last_mission_stats.total_kills_per_mt[0] +=
				kills;
			g_pilot_data
				.faction_statistics[g_pilot_data
							    .current_faction_id]
				.stats.kills_per_craft_per_mt[stat_type]
							     [object_type] +=
				kills;
			g_pilot_data.main_stats
				.kills_per_craft_per_mt[stat_type]
						       [object_type] += kills;
			g_pilot_data.last_mission_stats
				.kills_per_craft_per_mt[0][object_type] +=
				kills;
			kills = g_players[g_local_player]
					.per_mission_kills
					.kills_shared_on_flight_group[fg_idx];
			g_pilot_data
				.faction_statistics[g_pilot_data
							    .current_faction_id]
				.stats
				.kills_shared_per_craft_per_mt[stat_type]
							      [object_type] +=
				kills;
			g_pilot_data.main_stats
				.kills_shared_per_craft_per_mt[stat_type]
							      [object_type] +=
				kills;
			g_pilot_data.last_mission_stats
				.kills_shared_per_craft_per_mt[0]
							      [object_type] +=
				kills;
			kills = g_players[g_local_player]
					.per_mission_kills
					.kills_assist_on_flight_group[fg_idx];
			g_pilot_data
				.faction_statistics[g_pilot_data
							    .current_faction_id]
				.stats
				.kills_assists_per_craft_per_mt[stat_type]
							       [object_type] +=
				kills;
			g_pilot_data.main_stats
				.kills_assists_per_craft_per_mt[stat_type]
							       [object_type] +=
				kills;
			g_pilot_data.last_mission_stats
				.kills_assists_per_craft_per_mt[0]
							       [object_type] +=
				kills;
		}

		g_pilot_data.faction_statistics[g_pilot_data.current_faction_id]
			.stats.total_friendlies_killed_per_mt[stat_type] +=
			g_players[g_local_player]
				.per_mission_kills.friendlies_killed;
		g_pilot_data.main_stats
			.total_friendlies_killed_per_mt[stat_type] +=
			g_players[g_local_player]
				.per_mission_kills.friendlies_killed;
		g_pilot_data.last_mission_stats
			.total_friendlies_killed_per_mt[0] +=
			g_players[g_local_player]
				.per_mission_kills.friendlies_killed;
		for (unsigned int rating_idx = 0;
		     rating_idx < PLAYER_RATING_COUNT; ++rating_idx) {
			uint16_t value = g_players[g_local_player]
						 .per_mission_kills
						 .kills_full_on_player_rating
							 [rating_idx];
			g_pilot_data
				.faction_statistics[g_pilot_data
							    .current_faction_id]
				.stats.kills_full_on_player_rating_per_mt
					[stat_type][rating_idx] += value;
			g_pilot_data.main_stats
				.kills_full_on_player_rating_per_mt
					[stat_type][rating_idx] += value;
			g_pilot_data.last_mission_stats
				.kills_full_on_player_rating_per_mt
					[0][rating_idx] += value;
			value = g_players[g_local_player]
					.per_mission_kills
					.kills_shared_on_player_rating
						[rating_idx];
			g_pilot_data
				.faction_statistics[g_pilot_data
							    .current_faction_id]
				.stats.kills_shared_on_player_rating_per_mt
					[stat_type][rating_idx] += value;
			g_pilot_data.main_stats
				.kills_shared_on_player_rating_per_mt
					[stat_type][rating_idx] += value;
			g_pilot_data.last_mission_stats
				.kills_shared_on_player_rating_per_mt
					[0][rating_idx] += value;
			value = g_players[g_local_player]
					.per_mission_kills
					.kills_assist_on_player_rating
						[rating_idx];
			g_pilot_data
				.faction_statistics[g_pilot_data
							    .current_faction_id]
				.stats.kills_assist_on_player_rating_per_mt
					[stat_type][rating_idx] += value;
			g_pilot_data.main_stats
				.kills_assist_on_player_rating_per_mt
					[stat_type][rating_idx] += value;
			g_pilot_data.last_mission_stats
				.kills_assist_on_player_rating_per_mt
					[0][rating_idx] += value;
			value = g_players[g_local_player]
					.per_mission_kills
					.killed_by_player_rating[rating_idx];
			g_pilot_data
				.faction_statistics[g_pilot_data
							    .current_faction_id]
				.stats
				.killed_by_player_rating_per_mt[stat_type]
							       [rating_idx] +=
				value;
			g_pilot_data.main_stats
				.killed_by_player_rating_per_mt[stat_type]
							       [rating_idx] +=
				value;
			g_pilot_data.last_mission_stats
				.killed_by_player_rating_per_mt[0]
							       [rating_idx] +=
				value;
		}
		for (unsigned int ai_rating_idx = 0;
		     ai_rating_idx < AI_RATING_COUNT; ++ai_rating_idx) {
			uint16_t value =
				g_players[g_local_player]
					.per_mission_kills
					.kills_full_on_ai_rating[ai_rating_idx];
			g_pilot_data
				.faction_statistics[g_pilot_data
							    .current_faction_id]
				.stats.kills_full_on_ai_rating_per_mt
					[stat_type][ai_rating_idx] += value;
			g_pilot_data.main_stats.kills_full_on_ai_rating_per_mt
				[stat_type][ai_rating_idx] += value;
			g_pilot_data.last_mission_stats
				.kills_full_on_ai_rating_per_mt
					[0][ai_rating_idx] += value;
			value = g_players[g_local_player]
					.per_mission_kills
					.kills_shared_on_ai_rating
						[ai_rating_idx];
			g_pilot_data
				.faction_statistics[g_pilot_data
							    .current_faction_id]
				.stats.kills_shared_on_ai_rating_per_mt
					[stat_type][ai_rating_idx] += value;
			g_pilot_data.main_stats.kills_shared_on_ai_rating_per_mt
				[stat_type][ai_rating_idx] += value;
			g_pilot_data.last_mission_stats
				.kills_shared_on_ai_rating_per_mt
					[0][ai_rating_idx] += value;
			value = g_players[g_local_player]
					.per_mission_kills
					.kills_assist_on_ai_rating
						[ai_rating_idx];
			g_pilot_data
				.faction_statistics[g_pilot_data
							    .current_faction_id]
				.stats.kills_assist_on_ai_rating_per_mt
					[stat_type][ai_rating_idx] += value;
			g_pilot_data.main_stats.kills_assist_on_ai_rating_per_mt
				[stat_type][ai_rating_idx] += value;
			g_pilot_data.last_mission_stats
				.kills_assist_on_ai_rating_per_mt
					[0][ai_rating_idx] += value;
			value = g_players[g_local_player]
					.per_mission_kills
					.killed_by_ai_rating[ai_rating_idx];
			g_pilot_data
				.faction_statistics[g_pilot_data
							    .current_faction_id]
				.stats
				.killed_by_ai_rating_per_mt[stat_type]
							   [ai_rating_idx] +=
				value;
			g_pilot_data.main_stats
				.killed_by_ai_rating_per_mt[stat_type]
							   [ai_rating_idx] +=
				value;
			g_pilot_data.last_mission_stats
				.killed_by_ai_rating_per_mt[0][ai_rating_idx] +=
				value;
		}

		g_pilot_data.faction_statistics[g_pilot_data.current_faction_id]
			.stats.num_special_inspected_per_mt[stat_type] +=
			g_players[g_local_player]
				.per_mission_kills.num_special_inspected;
		g_pilot_data.main_stats
			.num_special_inspected_per_mt[stat_type] +=
			g_players[g_local_player]
				.per_mission_kills.num_special_inspected;
		g_pilot_data.last_mission_stats
			.num_special_inspected_per_mt[0] +=
			g_players[g_local_player]
				.per_mission_kills.num_special_inspected;
		g_pilot_data.faction_statistics[g_pilot_data.current_faction_id]
			.stats.energy_fired_per_mt[stat_type] +=
			g_players[g_local_player]
				.mission_stats.laser_shots_fired;
		g_pilot_data.main_stats.energy_fired_per_mt[stat_type] +=
			g_players[g_local_player]
				.mission_stats.laser_shots_fired;
		g_pilot_data.last_mission_stats.energy_fired_per_mt[0] +=
			g_players[g_local_player]
				.mission_stats.laser_shots_fired;
		g_pilot_data.faction_statistics[g_pilot_data.current_faction_id]
			.stats.energy_fired_per_mt[stat_type] +=
			g_players[g_local_player].mission_stats.ion_shots_fired;
		g_pilot_data.main_stats.energy_fired_per_mt[stat_type] +=
			g_players[g_local_player].mission_stats.ion_shots_fired;
		g_pilot_data.last_mission_stats.energy_fired_per_mt[0] +=
			g_players[g_local_player].mission_stats.ion_shots_fired;
		g_pilot_data.faction_statistics[g_pilot_data.current_faction_id]
			.stats.energy_hits_per_mt[stat_type] +=
			g_players[g_local_player]
				.mission_stats.laser_hits_scored;
		g_pilot_data.main_stats.energy_hits_per_mt[stat_type] +=
			g_players[g_local_player]
				.mission_stats.laser_hits_scored;
		g_pilot_data.last_mission_stats.energy_hits_per_mt[0] +=
			g_players[g_local_player]
				.mission_stats.laser_hits_scored;
		g_pilot_data.faction_statistics[g_pilot_data.current_faction_id]
			.stats.energy_hits_per_mt[stat_type] +=
			g_players[g_local_player].mission_stats.ion_hits_scored;
		g_pilot_data.main_stats.energy_hits_per_mt[stat_type] +=
			g_players[g_local_player].mission_stats.ion_hits_scored;
		g_pilot_data.last_mission_stats.energy_hits_per_mt[0] +=
			g_players[g_local_player].mission_stats.ion_hits_scored;
		g_pilot_data.faction_statistics[g_pilot_data.current_faction_id]
			.stats.warheads_fired_per_mt[stat_type] +=
			g_players[g_local_player].warheads_fired;
		g_pilot_data.main_stats.warheads_fired_per_mt[stat_type] +=
			g_players[g_local_player].warheads_fired;
		g_pilot_data.last_mission_stats.warheads_fired_per_mt[0] +=
			g_players[g_local_player].warheads_fired;
		g_pilot_data.faction_statistics[g_pilot_data.current_faction_id]
			.stats.warheads_hits_per_mt[stat_type] +=
			g_players[g_local_player]
				.per_mission_kills.warhead_hits;
		g_pilot_data.main_stats.warheads_hits_per_mt[stat_type] +=
			g_players[g_local_player]
				.per_mission_kills.warhead_hits;
		g_pilot_data.last_mission_stats.warheads_hits_per_mt[0] +=
			g_players[g_local_player]
				.per_mission_kills.warhead_hits;
		g_pilot_data.faction_statistics[g_pilot_data.current_faction_id]
			.stats.total_craft_losses_per_mt[stat_type] +=
			g_players[g_local_player]
				.per_mission_kills.total_craft_losses;
		g_pilot_data.main_stats.total_craft_losses_per_mt[stat_type] +=
			g_players[g_local_player]
				.per_mission_kills.total_craft_losses;
		g_pilot_data.last_mission_stats.total_craft_losses_per_mt[0] +=
			g_players[g_local_player]
				.per_mission_kills.total_craft_losses;
		g_pilot_data.faction_statistics[g_pilot_data.current_faction_id]
			.stats.losses_by_collisions_per_mt[stat_type] +=
			g_players[g_local_player]
				.per_mission_kills.losses_by_collisions;
		g_pilot_data.main_stats
			.losses_by_collisions_per_mt[stat_type] +=
			g_players[g_local_player]
				.per_mission_kills.losses_by_collisions;
		g_pilot_data.last_mission_stats
			.losses_by_collisions_per_mt[0] +=
			g_players[g_local_player]
				.per_mission_kills.losses_by_collisions;
		g_pilot_data.faction_statistics[g_pilot_data.current_faction_id]
			.stats.losses_by_starships_per_mt[stat_type] +=
			g_players[g_local_player]
				.per_mission_kills.losses_by_starships;
		g_pilot_data.main_stats.losses_by_starships_per_mt[stat_type] +=
			g_players[g_local_player]
				.per_mission_kills.losses_by_starships;
		g_pilot_data.last_mission_stats.losses_by_starships_per_mt[0] +=
			g_players[g_local_player]
				.per_mission_kills.losses_by_starships;
		g_pilot_data.faction_statistics[g_pilot_data.current_faction_id]
			.stats.losses_by_mines_per_mt[stat_type] +=
			g_players[g_local_player]
				.per_mission_kills.losses_by_mines;
		g_pilot_data.main_stats.losses_by_mines_per_mt[stat_type] +=
			g_players[g_local_player]
				.per_mission_kills.losses_by_mines;
		g_pilot_data.last_mission_stats.losses_by_mines_per_mt[0] +=
			g_players[g_local_player]
				.per_mission_kills.losses_by_mines;
		g_pilot_data.faction_statistics[g_pilot_data.current_faction_id]
			.total_score += score;
		g_pilot_data.total_score += score;
		g_pilot_data.mission_score = score;
		sprintf(g_mission_debug_buffer,
			"Promo points: %d    Worse Promo points: %d\n",
			g_players[g_local_player]
				.mission_stats.rating_promo_points,
			g_players[g_local_player]
				.mission_stats.worse_rating_promo_points);
		XVT_LOG_DEBUG(
			"results.pilot_tallies score=%d mission_score=%d bonus=%d kills=%d friendly=%d lasers=%d laser_hits=%d ions=%d ion_hits=%d warheads=%d warhead_hits=%d losses=%d collisions=%d starships=%d mines=%d inspected=%d promo=%d worse_promo=%d",
			score,
			g_players[g_local_player].mission_stats.mission_score,
			g_flight_mission_state.runtime
				.team_scores[TEAM_SCORE_BONUS]
					    [local_player_team],
			g_pilot_data.last_mission_stats.total_kills_per_mt[0],
			(int)g_players[g_local_player]
				.per_mission_kills.friendlies_killed,
			(int)g_players[g_local_player]
				.mission_stats.laser_shots_fired,
			(int)g_players[g_local_player]
				.mission_stats.laser_hits_scored,
			(int)g_players[g_local_player]
				.mission_stats.ion_shots_fired,
			(int)g_players[g_local_player]
				.mission_stats.ion_hits_scored,
			(int)g_players[g_local_player].warheads_fired,
			(int)g_players[g_local_player]
				.per_mission_kills.warhead_hits,
			(int)g_players[g_local_player]
				.per_mission_kills.total_craft_losses,
			(int)g_players[g_local_player]
				.per_mission_kills.losses_by_collisions,
			(int)g_players[g_local_player]
				.per_mission_kills.losses_by_starships,
			(int)g_players[g_local_player]
				.per_mission_kills.losses_by_mines,
			(int)g_players[g_local_player]
				.per_mission_kills.num_special_inspected,
			g_players[g_local_player]
				.mission_stats.rating_promo_points,
			g_players[g_local_player]
				.mission_stats.worse_rating_promo_points);

		g_pilot_data.promotion_delta = PILOT_PROMOTION_NONE;
		int promotion_threshold;
		if (stat_type == MISSION_STAT_TRAINING) {
			int maximum_training_rating =
				g_mission_header.mission_type ==
						MISSION_TYPE_SIMULATOR_2
					? PILOT_RATING_JEDI_MASTER
					: PILOT_RATING_OFFICER_1ST_CLASS;
			if ((unsigned int)g_pilot_data.rating <
			    (unsigned int)maximum_training_rating) {
				g_pilot_data.current_rating_promo_points +=
					g_players[g_local_player]
						.mission_stats
						.rating_promo_points;
				promotion_threshold =
					g_pilot_rating_promotion_point_thresholds
						[g_pilot_data.rating];
				if (g_pilot_data
					    .current_rating_worse_promo_points <
				    promotion_threshold / 2) {
					g_pilot_data
						.current_rating_worse_promo_points +=
						g_players[g_local_player]
							.mission_stats
							.worse_rating_promo_points;
					g_pilot_data
						.current_rating_promo_points +=
						g_players[g_local_player]
							.mission_stats
							.worse_rating_promo_points;
				}
				if (g_pilot_data.current_rating_promo_points >=
				    promotion_threshold) {
					g_pilot_data
						.current_rating_promo_points =
						0;
					g_pilot_data
						.current_rating_worse_promo_points =
						0;
					g_pilot_data.promotion_delta =
						PILOT_PROMOTION_PROMOTION;
					++g_pilot_data.rating;
					g_pilot_data.rating_achieved_on_mission
						[g_pilot_data.rating] =
						g_pilot_data
							.total_missions_played_count;
				}
				if (g_pilot_data.current_rating_promo_points >=
				    0) {
					g_pilot_data.next_promotion_percent =
						MAX_PROMOTION_PERCENT *
						g_pilot_data
							.current_rating_promo_points /
						g_pilot_rating_promotion_point_thresholds
							[g_pilot_data.rating];
					if (g_pilot_data
						    .next_promotion_percent >
					    MAX_PROMOTION_PERCENT) {
						g_pilot_data
							.next_promotion_percent =
							MAX_PROMOTION_PERCENT;
					}
				} else if (g_pilot_data.rating !=
					   PILOT_RATING_TARGET_DRONE) {
					g_pilot_data.next_promotion_percent =
						MAX_PROMOTION_PERCENT *
						g_pilot_data
							.current_rating_promo_points /
						-PROMOTION_LOSS_LIMIT;
					if (g_pilot_data
						    .next_promotion_percent <
					    -MAX_PROMOTION_PERCENT) {
						g_pilot_data
							.next_promotion_percent =
							-MAX_PROMOTION_PERCENT;
					}
				} else {
					g_pilot_data
						.current_rating_promo_points =
						0;
					g_pilot_data
						.current_rating_worse_promo_points =
						0;
					g_pilot_data.next_promotion_percent = 0;
				}
			} else if (g_players[g_local_player]
					   .mission_stats.rating_promo_points <
				   0) {
				g_pilot_data.current_rating_promo_points +=
					g_players[g_local_player]
						.mission_stats
						.rating_promo_points;
			}
			if (g_pilot_data.rating != PILOT_RATING_TARGET_DRONE &&
			    g_pilot_data.current_rating_promo_points <
				    PROMOTION_LOSS_LIMIT) {
				g_pilot_data.current_rating_promo_points = 0;
				g_pilot_data.current_rating_worse_promo_points =
					0;
				--g_pilot_data.rating;
				g_pilot_data.next_promotion_percent = 0;
				g_pilot_data.promotion_delta =
					PILOT_PROMOTION_DEMOTION;
				if ((unsigned int)g_pilot_data.rating <
				    PILOT_RATING_TRAINEE) {
					g_pilot_data.rating_achieved_on_mission
						[g_pilot_data.rating] =
						g_pilot_data
							.total_missions_played_count;
				}
			}
		} else if (stat_type == MISSION_STAT_MELEE ||
			   stat_type == MISSION_STAT_COMBAT) {
			if ((unsigned int)g_pilot_data.rating <
			    PILOT_RATING_JEDI_MASTER) {
				g_pilot_data.current_rating_promo_points +=
					g_players[g_local_player]
						.mission_stats
						.rating_promo_points;
				promotion_threshold =
					g_pilot_rating_promotion_point_thresholds
						[g_pilot_data.rating];
				if (g_pilot_data
					    .current_rating_worse_promo_points <
				    promotion_threshold / 2) {
					g_pilot_data
						.current_rating_worse_promo_points +=
						g_players[g_local_player]
							.mission_stats
							.worse_rating_promo_points;
					g_pilot_data
						.current_rating_promo_points +=
						g_players[g_local_player]
							.mission_stats
							.worse_rating_promo_points;
				}
				if (g_pilot_data.current_rating_promo_points >=
				    promotion_threshold) {
					g_pilot_data
						.current_rating_worse_promo_points =
						0;
					g_pilot_data
						.current_rating_promo_points -=
						promotion_threshold;
					g_pilot_data.promotion_delta =
						PILOT_PROMOTION_PROMOTION;
					++g_pilot_data.rating;
					g_pilot_data.rating_achieved_on_mission
						[g_pilot_data.rating] =
						g_pilot_data
							.total_missions_played_count;
				}
				if (g_pilot_data.current_rating_promo_points >=
				    0) {
					g_pilot_data.next_promotion_percent =
						MAX_PROMOTION_PERCENT *
						g_pilot_data
							.current_rating_promo_points /
						g_pilot_rating_promotion_point_thresholds
							[g_pilot_data.rating];
					if (g_pilot_data
						    .next_promotion_percent >
					    MAX_PROMOTION_PERCENT) {
						g_pilot_data
							.next_promotion_percent =
							MAX_PROMOTION_PERCENT;
					}
				} else if (g_pilot_data.rating !=
					   PILOT_RATING_TARGET_DRONE) {
					g_pilot_data.next_promotion_percent =
						MAX_PROMOTION_PERCENT *
						g_pilot_data
							.current_rating_promo_points /
						-PROMOTION_LOSS_LIMIT;
					if (g_pilot_data
						    .next_promotion_percent <
					    -MAX_PROMOTION_PERCENT) {
						g_pilot_data
							.next_promotion_percent =
							-MAX_PROMOTION_PERCENT;
					}
				} else {
					g_pilot_data
						.current_rating_promo_points =
						0;
					g_pilot_data
						.current_rating_worse_promo_points =
						0;
					g_pilot_data.next_promotion_percent = 0;
				}
			} else if (g_players[g_local_player]
					   .mission_stats.rating_promo_points <
				   0) {
				g_pilot_data.current_rating_promo_points +=
					g_players[g_local_player]
						.mission_stats
						.rating_promo_points;
			}
			if (g_pilot_data.rating != PILOT_RATING_TARGET_DRONE &&
			    g_pilot_data.current_rating_promo_points <
				    PROMOTION_LOSS_LIMIT) {
				g_pilot_data.current_rating_promo_points = 0;
				g_pilot_data.current_rating_worse_promo_points =
					0;
				--g_pilot_data.rating;
				g_pilot_data.next_promotion_percent = 0;
				g_pilot_data.promotion_delta =
					PILOT_PROMOTION_DEMOTION;
				if ((unsigned int)g_pilot_data.rating <
				    PILOT_RATING_TRAINEE) {
					g_pilot_data.rating_achieved_on_mission
						[g_pilot_data.rating] =
						g_pilot_data
							.total_missions_played_count;
				}
			}
		}
		XVT_LOG_DEBUG(
			"results.rating_updated rating=%d rank_change=%d promo=%d worse_promo=%d percent=%d missions=%d",
			(int)g_pilot_data.rating,
			(int)g_pilot_data.promotion_delta,
			g_pilot_data.current_rating_promo_points,
			g_pilot_data.current_rating_worse_promo_points,
			g_pilot_data.next_promotion_percent,
			g_pilot_data.total_missions_played_count);

		for (player_idx = 0; player_idx < PLAYER_COUNT; ++player_idx) {
			if (g_players[player_idx].network.direct_play_id == 0) {
				continue;
			}
			for (network_idx = 0; network_idx < PLAYER_COUNT;
			     ++network_idx) {
				if (g_pilot_data.network_players[network_idx]
					    .direct_play_id ==
				    g_players[player_idx]
					    .network.direct_play_id) {
					g_pilot_data.kills_full_on_player
						[network_idx] =
						g_players[g_local_player]
							.per_mission_kills
							.kills_full_on_player
								[player_idx];
					g_pilot_data.kills_shared_on_player
						[network_idx] =
						g_players[g_local_player]
							.per_mission_kills
							.kills_shared_on_player
								[player_idx];
					g_pilot_data.kills_full_from_player
						[network_idx] =
						g_players[g_local_player]
							.per_mission_kills
							.kills_full_from_player
								[player_idx];
					g_pilot_data.kills_shared_from_player
						[network_idx] =
						g_players[g_local_player]
							.per_mission_kills
							.kills_shared_from_player
								[player_idx];
					XVT_LOG_DEBUG(
						"results.kills_by_player slot=%u entry=%u player=%u kills=%d shared=%d by_kills=%d by_shared=%d",
						player_idx, network_idx,
						(unsigned)g_players[player_idx]
							.network.direct_play_id,
						g_pilot_data
							.kills_full_on_player
								[network_idx],
						g_pilot_data
							.kills_shared_on_player
								[network_idx],
						g_pilot_data
							.kills_full_from_player
								[network_idx],
						g_pilot_data
							.kills_shared_from_player
								[network_idx]);
					break;
				}
			}
		}
		for (fg_idx = 0;
		     fg_idx < (unsigned int)g_mission_header.num_flight_groups;
		     ++fg_idx) {
			g_pilot_data.kills_full_on_flight_group[fg_idx] =
				g_players[g_local_player]
					.per_mission_kills
					.kills_full_on_flight_group[fg_idx];
			g_pilot_data.kills_shared_on_flight_group[fg_idx] =
				g_players[g_local_player]
					.per_mission_kills
					.kills_shared_on_flight_group[fg_idx];
			g_pilot_data.kills_full_from_flight_group[fg_idx] =
				g_players[g_local_player]
					.per_mission_kills
					.kills_full_from_flight_group[fg_idx];
			g_pilot_data.kills_shared_from_flight_group[fg_idx] =
				g_players[g_local_player]
					.per_mission_kills
					.kills_shared_from_flight_group[fg_idx];
			if (g_mission_header.mission_type ==
				    MISSION_TYPE_MELEE &&
			    (g_pilot_data.mission_sequence_active != 1 ||
			     g_pilot_data.melee_tournament_sequence_state
					     .current_mission_index == 0)) {
				int group_ai = g_mission_flight_groups[fg_idx]
						       .fg.group_ai;
				int flight_group_rating =
					g_flight_group_rating_base_by_ai_level
						[group_ai];
				if (group_ai != 0) {
					flight_group_rating += fg_idx & 3;
				}
				g_pilot_data.flight_group_rating[fg_idx] =
					flight_group_rating;
				XVT_LOG_DEBUG(
					"results.fg_rating fg=%u ai=%d rating=%d",
					fg_idx, group_ai, flight_group_rating);
			}
		}

		for (team_idx = 0; team_idx < PLAYER_COUNT; ++team_idx) {
			struct pilot_team *team = &g_pilot_data.teams[team_idx];
			team->is_mission_completed =
				g_flight_mission_state.runtime.team_goal_status
						[team_idx][TEAM_GOAL_PRIMARY] ==
					1 &&
				g_flight_mission_state.runtime.team_goal_status
						[team_idx][TEAM_GOAL_PREVENT] !=
					1;
			team->mission_score =
				g_flight_mission_state.runtime
					.team_scores[TEAM_SCORE_BONUS]
						    [team_idx];
			team->mission_time =
				g_flight_mission_state.runtime
					.team_mission_completion_time_seconds
						[team_idx];
			team->kills =
				g_flight_mission_state.runtime
					.team_kill_stats[TEAM_KILL_STAT_FULL]
							[team_idx];
			team->kills_shared =
				g_flight_mission_state.runtime
					.team_kill_stats[TEAM_KILL_STAT_SHARED]
							[team_idx];
			team->losses =
				g_flight_mission_state.runtime
					.team_kill_stats[TEAM_KILL_STAT_LOSSES]
							[team_idx];
			if (stat_type == MISSION_STAT_MELEE) {
				team->mission_score +=
					g_flight_mission_state.runtime
						.team_scores[TEAM_SCORE_MISSION]
							    [team_idx];
			} else {
				for (player_idx = 0; player_idx < PLAYER_COUNT;
				     ++player_idx) {
					if (g_players[player_idx]
							    .network
							    .direct_play_id !=
						    0 &&
					    (uint16_t)g_players[player_idx]
							    .team == team_idx) {
						team->mission_score +=
							g_players[player_idx]
								.mission_stats
								.mission_score;
					}
				}
			}
			if (active_team_fg_count[team_idx] != 0 ||
			    team->is_mission_completed != 0) {
				XVT_LOG_INFO(
					"battle.team_result team=%u completed=%d primary=%d prevent=%d score=%d kills=%d shared=%d losses=%d seconds=%d tick=%d",
					team_idx, team->is_mission_completed,
					(int)g_flight_mission_state.runtime
						.team_goal_status
							[team_idx]
							[TEAM_GOAL_PRIMARY],
					(int)g_flight_mission_state.runtime
						.team_goal_status
							[team_idx]
							[TEAM_GOAL_PREVENT],
					team->mission_score, team->kills,
					team->kills_shared, team->losses,
					team->mission_time, g_game_time);
			}
		}
		for (player_idx = 0; player_idx < PLAYER_COUNT; ++player_idx) {
			if (g_players[player_idx].network.direct_play_id == 0) {
				continue;
			}
			for (network_idx = 0; network_idx < PLAYER_COUNT;
			     ++network_idx) {
				if (g_pilot_data.network_players[network_idx]
					    .direct_play_id ==
				    g_players[player_idx]
					    .network.direct_play_id) {
					for (fg_idx = 0;
					     fg_idx <
					     (unsigned int)g_mission_header
						     .num_flight_groups;
					     ++fg_idx) {
						g_pilot_data
							.network_players
								[network_idx]
							.kills +=
							g_players[player_idx]
								.per_mission_kills
								.kills_full_on_flight_group
									[fg_idx];
						g_pilot_data
							.network_players
								[network_idx]
							.kills_shared +=
							g_players[player_idx]
								.per_mission_kills
								.kills_shared_on_flight_group
									[fg_idx];
						g_pilot_data
							.network_players
								[network_idx]
							.kills_assist +=
							g_players[player_idx]
								.per_mission_kills
								.kills_assist_on_flight_group
									[fg_idx];
					}
					g_pilot_data
						.network_players[network_idx]
						.total_score =
						g_players[player_idx]
							.mission_stats
							.mission_score +
						g_flight_mission_state.runtime.team_scores
							[TEAM_SCORE_BONUS]
							[(uint16_t)g_players
								 [player_idx]
									 .team];
					g_pilot_data
						.network_players[network_idx]
						.total_losses =
						g_players[player_idx]
							.per_mission_kills
							.total_craft_losses;
					XVT_LOG_INFO(
						"battle.player_result slot=%u entry=%u player=%u team=%d fg=%u score=%d kills=%d shared=%d assists=%d losses=%d shots=%d hits=%d warheads=%d warhead_hits=%d has_left=%d tick=%d",
						player_idx, network_idx,
						(unsigned)g_pilot_data
							.network_players
								[network_idx]
							.direct_play_id,
						(int)g_players[player_idx].team,
						(unsigned)g_players[player_idx]
							.bound_flight_group_idx,
						g_pilot_data
							.network_players
								[network_idx]
							.total_score,
						g_pilot_data
							.network_players
								[network_idx]
							.kills,
						g_pilot_data
							.network_players
								[network_idx]
							.kills_shared,
						g_pilot_data
							.network_players
								[network_idx]
							.kills_assist,
						g_pilot_data
							.network_players
								[network_idx]
							.total_losses,
						g_players[player_idx]
								.mission_stats
								.laser_shots_fired +
							g_players[player_idx]
								.mission_stats
								.ion_shots_fired,
						g_players[player_idx]
								.mission_stats
								.laser_hits_scored +
							g_players[player_idx]
								.mission_stats
								.ion_hits_scored,
						(int)g_players[player_idx]
							.warheads_fired,
						(int)g_players[player_idx]
							.per_mission_kills
							.warhead_hits,
						g_pilot_data
							.network_players
								[network_idx]
							.has_left,
						g_game_time);
					break;
				}
			}
		}

		unsigned int award_threshold_idx;
		for (award_threshold_idx = 0;
		     award_threshold_idx <
		     sizeof(g_pilot_data.faction_statistics[0].mission_awards) /
			     sizeof(g_pilot_data.faction_statistics[0]
					    .mission_awards[0]);
		     ++award_threshold_idx) {
			g_pilot_data
				.faction_statistics[g_pilot_data
							    .current_faction_id]
				.mission_awards[award_threshold_idx] = 0;
		}
		int award = 0;
		int placement;
		int margin;
		if (stat_type == MISSION_STAT_TRAINING) {
			if (g_flight_mission_state.runtime.team_goal_status
					    [(uint16_t)g_players[g_local_player]
						     .team]
					    [TEAM_GOAL_PRIMARY] == 1 &&
			    g_flight_mission_state.runtime.team_goal_status
					    [(uint16_t)g_players[g_local_player]
						     .team]
					    [TEAM_GOAL_PREVENT] != 1) {
				award = 1;
				for (award_threshold_idx = 0;
				     award_threshold_idx <
				     sizeof(g_mission_award_score_thresholds) /
					     sizeof(g_mission_award_score_thresholds
							    [0]);
				     ++award_threshold_idx) {
					if (score >=
					    g_mission_award_score_thresholds
						    [award_threshold_idx]) {
						break;
					}
					++award;
				}
				if (g_flight_mission_state.difficulty ==
				    GAME_DIFFICULTY_EASY) {
					award += 2;
				} else if (g_flight_mission_state.difficulty ==
					   GAME_DIFFICULTY_MEDIUM) {
					++award;
				}
				if (g_flight_mission_state
						    .player_flight_group_wave_mode ==
					    CRAFT_WAVES_UNLIMITED &&
				    award < 5) {
					award = 5;
				}
				if (award > MAX_STORED_AWARD) {
					award = 0;
				}
			} else if (score <= 0) {
				award = FAILED_AWARD;
			}
			if (g_pilot_data.mission_sequence_active == 1 &&
			    (g_game_config.difficulty > GAME_DIFFICULTY_HARD ||
			     g_flight_mission_state
					     .player_flight_group_wave_mode ==
				     CRAFT_WAVES_UNLIMITED)) {
				award = FAILED_AWARD;
			}
			if (award != 0) {
				g_pilot_data
					.faction_statistics
						[g_pilot_data
							 .current_faction_id]
					.mission_awards[2] = award;
			}
		} else if (stat_type == MISSION_STAT_MELEE) {
			int better_team_count = 0;
			margin = 0;
			int player_team_score =
				g_flight_mission_state.runtime
					.team_scores[TEAM_SCORE_BONUS]
						    [local_player_team] +
				g_flight_mission_state.runtime
					.team_scores[TEAM_SCORE_MISSION]
						    [local_player_team];
			for (team_idx = 0; team_idx < TEAM_COUNT; ++team_idx) {
				if (team_idx ==
				    (unsigned int)local_player_team) {
					continue;
				}
				int has_opponent = 0;
				for (fg_idx = 0;
				     fg_idx < (unsigned int)g_mission_header
						      .num_flight_groups;
				     ++fg_idx) {
					if (g_mission_flight_groups[fg_idx]
							    .fg.team ==
						    team_idx &&
					    (g_mission_flight_groups[fg_idx]
							     .player_owner_idx !=
						     -1 ||
					     (g_mission_flight_groups[fg_idx]
							      .fg
							      .player_number !=
						      0 &&
					      g_flight_mission_state
							      .ai_opponents_enabled !=
						      0))) {
						has_opponent = 1;
					}
				}
				if (!has_opponent) {
					continue;
				}
				int opponent_score =
					g_flight_mission_state.runtime
						.team_scores[TEAM_SCORE_BONUS]
							    [team_idx] +
					g_flight_mission_state.runtime
						.team_scores[TEAM_SCORE_MISSION]
							    [team_idx];
				if (player_team_score < opponent_score) {
					++better_team_count;
				} else if (player_team_score - opponent_score <
						   margin ||
					   margin == 0) {
					margin = player_team_score -
						 opponent_score;
				}
			}
			placement = better_team_count + 1;
			if (active_team_count > 1 &&
			    connected_human_count > 1) {
				if ((unsigned int)placement ==
					    active_team_count &&
				    active_team_count >= 4) {
					award = FAILED_AWARD;
				} else if (score > 5000 && placement < 4) {
					award = g_placement_award_levels
						[3 * active_team_count - 4 +
						 placement];
				}
				if (award != 0 && award != FAILED_AWARD) {
					if (score < 1250) {
						award += 2;
						if (award > MAX_STORED_AWARD) {
							award = MAX_STORED_AWARD;
						}
					} else if (score < 2500) {
						++award;
						if (award > MAX_STORED_AWARD) {
							award = MAX_STORED_AWARD;
						}
					}
				}
				if (placement == FIRST_PLACE && award > 0 &&
				    award != FAILED_AWARD) {
					if (margin > 15000) {
						award -= 3;
					} else if (margin > 10000) {
						award -= 2;
					} else if (margin > 5000) {
						--award;
					}
					if (award < 1) {
						award = 1;
					}
				}
			} else {
				switch (g_flight_mission_state.difficulty) {
				case GAME_DIFFICULTY_EASY:
					if (placement == 1) {
						award = 5;
					} else if (placement > 4) {
						award = 6;
					}
					break;
				case GAME_DIFFICULTY_MEDIUM:
					if (placement == 1) {
						if (score <= 0) {
							award = 5;
						} else {
							award = 4;
							if (margin > 10000) {
								award = 2;
							} else if (margin >
								   5000) {
								award = 3;
							}
						}
					} else if (placement == 2 &&
						   active_team_count > 2) {
						award = 5;
					} else if ((unsigned int)placement ==
							   active_team_count ||
						   placement > 6) {
						award = 6;
					}
					break;
				case GAME_DIFFICULTY_HARD:
					if (placement == 1) {
						if (score <= 0) {
							award = 5;
						} else {
							award = 3;
							if (margin > 10000) {
								award = 1;
							} else if (margin >
								   5000) {
								award = 2;
							}
						}
					} else if (placement == 2) {
						if (active_team_count > 4) {
							award = 4;
						} else if (active_team_count >
							   2) {
							award = 5;
						}
					} else if (placement == 3 &&
						   active_team_count > 4) {
						award = 5;
					} else if (placement > 7) {
						award = 6;
					}
					break;
				}
			}
			if (award != 0) {
				g_pilot_data
					.faction_statistics
						[g_pilot_data
							 .current_faction_id]
					.mission_awards[0] = award;
			}
			sprintf(g_mission_debug_buffer,
				"Player's team score: %d   Place: %d   Margin: %d   Award: %d\n",
				player_team_score, placement, margin, award);
			XVT_LOG_DEBUG(
				"results.melee_placed team_score=%d rank=%d margin=%d award=%d teams=%u humans=%d",
				player_team_score, placement, margin, award,
				active_team_count, connected_human_count);
		} else if (stat_type == MISSION_STAT_COMBAT) {
			if (g_flight_mission_state.runtime.team_goal_status
					    [local_player_team]
					    [TEAM_GOAL_PRIMARY] == 2 ||
			    g_flight_mission_state.runtime.team_goal_status
					    [local_player_team]
					    [TEAM_GOAL_PREVENT] == 1) {
				award = FAILED_AWARD;
			} else if (connected_human_count == 1) {
				if (g_flight_mission_state.runtime
						    .team_goal_status
							    [local_player_team]
							    [TEAM_GOAL_PRIMARY] ==
					    1 &&
				    g_flight_mission_state.runtime
						    .team_goal_status
							    [local_player_team]
							    [TEAM_GOAL_PREVENT] !=
					    1) {
					award = 1;
					for (award_threshold_idx = 0;
					     award_threshold_idx <
					     sizeof(g_mission_award_score_thresholds) /
						     sizeof(g_mission_award_score_thresholds
								    [0]);
					     ++award_threshold_idx) {
						if (score >=
						    g_mission_award_score_thresholds
							    [award_threshold_idx]) {
							break;
						}
						++award;
					}
					if (g_flight_mission_state.difficulty ==
					    GAME_DIFFICULTY_EASY) {
						award += 2;
					} else if (g_flight_mission_state
							   .difficulty ==
						   GAME_DIFFICULTY_MEDIUM) {
						++award;
					}
					if (g_flight_mission_state
						    .player_flight_group_wave_mode ==
					    CRAFT_WAVES_UNLIMITED) {
						award = MAX_STORED_AWARD;
					}
					if (award >= FAILED_AWARD) {
						award = 0;
					}
				} else if (score <= 0) {
					award = FAILED_AWARD;
				}
			} else if (
				g_flight_mission_state.runtime.team_goal_status
						[local_player_team]
						[TEAM_GOAL_PRIMARY] == 1 &&
				g_flight_mission_state.runtime.team_goal_status
						[local_player_team]
						[TEAM_GOAL_PREVENT] != 1) {
				award = 1;
				for (award_threshold_idx = 0;
				     award_threshold_idx <= 4;
				     ++award_threshold_idx) {
					if (score >=
					    g_mission_award_win_thresholds
						    [award_threshold_idx]) {
						break;
					}
					++award;
				}
				if (g_flight_mission_state
					    .player_flight_group_wave_mode ==
				    CRAFT_WAVES_UNLIMITED) {
					award = MAX_STORED_AWARD;
				}
				if (award >= FAILED_AWARD) {
					award = 0;
				}
			} else if (score < -25000) {
				award = FAILED_AWARD;
			}
			if (award != 0) {
				g_pilot_data
					.faction_statistics
						[g_pilot_data
							 .current_faction_id]
					.mission_awards[2] = award;
			}
		}
		if (stat_type == MISSION_STAT_TRAINING &&
		    g_pilot_data.mission_sequence_active == 1 &&
		    (unsigned int)(g_pilot_data.mission_description_ids
					   [MISSION_DIRECTORY_TRAINING_EXERCISES] -
				   1) >=
			    sizeof(g_pilot_data.faction_statistics[0]
					   .sp_campaign_missions) /
				    sizeof(g_pilot_data.faction_statistics[0]
						   .sp_campaign_missions[0])) {
			XVT_LOG_WARN(
				"results.campaign_mission_invalid mission=%d entries=%u",
				(int)g_pilot_data.mission_description_ids
					[MISSION_DIRECTORY_TRAINING_EXERCISES],
				(unsigned)(sizeof(g_pilot_data
							  .faction_statistics[0]
							  .sp_campaign_missions) /
					   sizeof(g_pilot_data
							  .faction_statistics[0]
							  .sp_campaign_missions
								  [0])));
		}

		if (connected_human_count == 1) {
			switch (stat_type) {
			case MISSION_STAT_TRAINING: {
				unsigned int old_award;

				int mission_id =
					g_pilot_data.mission_description_ids
						[MISSION_DIRECTORY_TRAINING_EXERCISES];
				if (g_pilot_data.mission_sequence_active == 1) {
					++g_pilot_data
						  .faction_statistics
							  [g_pilot_data
								   .current_faction_id]
						  .sp_campaign_missions
							  [mission_id - 1]
						  .number_times_flown;
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.sp_campaign_missions
							[mission_id - 1]
						.campaign_id =
						g_pilot_data.mission_description_ids
							[MISSION_DIRECTORY_CAMPAIGNS];
					if (g_pilot_data
						    .teams[g_pilot_data.team]
						    .is_mission_completed !=
					    0) {
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.sp_campaign_missions
								[mission_id - 1]
							.is_completed = 1;
					}
					if (g_flight_mission_state
						    .player_flight_group_wave_mode !=
					    CRAFT_WAVES_UNLIMITED) {
						if (g_pilot_data
							    .faction_statistics
								    [g_pilot_data
									     .current_faction_id]
							    .sp_campaign_missions
								    [mission_id -
								     1]
							    .best_score <
						    (unsigned int)score) {
							g_pilot_data
								.faction_statistics
									[g_pilot_data
										 .current_faction_id]
								.sp_campaign_missions
									[mission_id -
									 1]
								.best_score =
								score;
						}
						if (g_flight_mission_state
								    .runtime
								    .team_mission_completion_time_seconds
									    [(uint16_t)g_players
										     [g_local_player]
											     .team] !=
							    0 &&
						    (g_flight_mission_state
								     .runtime
								     .team_mission_completion_time_seconds
									     [(uint16_t)g_players
										      [g_local_player]
											      .team] <
							     (unsigned int)g_pilot_data
								     .faction_statistics
									     [g_pilot_data
										      .current_faction_id]
								     .sp_campaign_missions
									     [mission_id -
									      1]
								     .best_time ||
						     g_pilot_data
								     .faction_statistics
									     [g_pilot_data
										      .current_faction_id]
								     .sp_campaign_missions
									     [mission_id -
									      1]
								     .best_time ==
							     0)) {
							g_pilot_data
								.faction_statistics
									[g_pilot_data
										 .current_faction_id]
								.sp_campaign_missions
									[mission_id -
									 1]
								.best_time =
								g_flight_mission_state
									.runtime
									.team_mission_completion_time_seconds
										[(uint16_t)g_players
											 [g_local_player]
												 .team];
						}
						if (g_pilot_data.teams[g_pilot_data
									       .team]
								    .is_mission_completed !=
							    0 &&
						    g_game_config.difficulty <=
							    GAME_DIFFICULTY_HARD) {
							g_pilot_data
								.faction_statistics
									[g_pilot_data
										 .current_faction_id]
								.sp_campaign_missions
									[mission_id -
									 1]
								.award_eligible =
								1;
						}
					}
					if (award != 0) {
						old_award =
							(unsigned int)g_pilot_data
								.faction_statistics
									[g_pilot_data
										 .current_faction_id]
								.sp_campaign_missions
									[mission_id -
									 1]
								.award_level;
						if (old_award > (unsigned int)
									award ||
						    old_award == 0) {
							if (old_award != 0 &&
							    g_pilot_data
									    .faction_statistics
										    [g_pilot_data
											     .current_faction_id]
									    .mission_evaluations
										    [old_award -
										     1] !=
								    0) {
								--g_pilot_data
									  .faction_statistics
										  [g_pilot_data
											   .current_faction_id]
									  .mission_evaluations
										  [old_award -
										   1];
							}
							g_pilot_data
								.faction_statistics
									[g_pilot_data
										 .current_faction_id]
								.sp_campaign_missions
									[mission_id -
									 1]
								.award_level =
								(int)award;
							++g_pilot_data
								  .faction_statistics
									  [g_pilot_data
										   .current_faction_id]
								  .mission_evaluations
									  [award -
									   1];
						}
					} else if (
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.sp_campaign_missions
								[mission_id - 1]
							.award_level ==
						FAILED_AWARD) {
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.sp_campaign_missions
								[mission_id - 1]
							.award_level = 0;
						if (g_pilot_data
							    .faction_statistics
								    [g_pilot_data
									     .current_faction_id]
							    .mission_evaluations
								    [FAILED_AWARD -
								     1] != 0) {
							--g_pilot_data
								  .faction_statistics
									  [g_pilot_data
										   .current_faction_id]
								  .mission_evaluations
									  [FAILED_AWARD -
									   1];
						}
					}
					XVT_LOG_DEBUG(
						"results.campaign_mission_record record=\"single\" mission=%d campaign=%d flown=%d completed=%d eligible=%d best=%u time=%d award=%d",
						mission_id,
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.sp_campaign_missions
								[mission_id - 1]
							.campaign_id,
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.sp_campaign_missions
								[mission_id - 1]
							.number_times_flown,
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.sp_campaign_missions
								[mission_id - 1]
							.is_completed,
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.sp_campaign_missions
								[mission_id - 1]
							.award_eligible,
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.sp_campaign_missions
								[mission_id - 1]
							.best_score,
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.sp_campaign_missions
								[mission_id - 1]
							.best_time,
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.sp_campaign_missions
								[mission_id - 1]
							.award_level);
				} else {
					++g_pilot_data
						  .faction_statistics
							  [g_pilot_data
								   .current_faction_id]
						  .sp_training_missions
							  [mission_id]
						  .number_times_flown;
					if (g_flight_mission_state.runtime.team_goal_status
						    [(uint16_t)g_players
							     [g_local_player]
								     .team]
						    [TEAM_GOAL_PRIMARY] == 1) {
						++g_pilot_data
							  .faction_statistics
								  [g_pilot_data
									   .current_faction_id]
							  .sp_training_missions
								  [mission_id]
							  .completed_count;
					}
					if (g_flight_mission_state.runtime.team_goal_status
							    [(uint16_t)g_players
								     [g_local_player]
									     .team]
							    [TEAM_GOAL_PRIMARY] ==
						    2 ||
					    g_flight_mission_state.runtime.team_goal_status
							    [(uint16_t)g_players
								     [g_local_player]
									     .team]
							    [TEAM_GOAL_PREVENT] ==
						    1) {
						++g_pilot_data
							  .faction_statistics
								  [g_pilot_data
									   .current_faction_id]
							  .sp_training_missions
								  [mission_id]
							  .failed_count;
					}
					if (g_flight_mission_state
						    .player_flight_group_wave_mode !=
					    CRAFT_WAVES_UNLIMITED) {
						if (g_pilot_data
							    .faction_statistics
								    [g_pilot_data
									     .current_faction_id]
							    .sp_training_missions
								    [mission_id]
							    .best_score <
						    score) {
							g_pilot_data
								.faction_statistics
									[g_pilot_data
										 .current_faction_id]
								.sp_training_missions
									[mission_id]
								.best_score =
								score;
						}
						if (g_flight_mission_state
								    .runtime
								    .team_mission_completion_time_seconds
									    [(uint16_t)g_players
										     [g_local_player]
											     .team] !=
							    0 &&
						    (g_flight_mission_state
								     .runtime
								     .team_mission_completion_time_seconds
									     [(uint16_t)g_players
										      [g_local_player]
											      .team] <
							     (unsigned int)g_pilot_data
								     .faction_statistics
									     [g_pilot_data
										      .current_faction_id]
								     .sp_training_missions
									     [mission_id]
								     .best_time ||
						     g_pilot_data
								     .faction_statistics
									     [g_pilot_data
										      .current_faction_id]
								     .sp_training_missions
									     [mission_id]
								     .best_time ==
							     0)) {
							g_pilot_data
								.faction_statistics
									[g_pilot_data
										 .current_faction_id]
								.sp_training_missions
									[mission_id]
								.best_time =
								g_flight_mission_state
									.runtime
									.team_mission_completion_time_seconds
										[(uint16_t)g_players
											 [g_local_player]
												 .team];
						}
					}
					if (award != 0) {
						old_award =
							(unsigned int)g_pilot_data
								.faction_statistics
									[g_pilot_data
										 .current_faction_id]
								.sp_training_missions
									[mission_id]
								.award_level;
						if (old_award > (unsigned int)
									award ||
						    old_award == 0) {
							if (old_award != 0 &&
							    g_pilot_data
									    .faction_statistics
										    [g_pilot_data
											     .current_faction_id]
									    .mission_evaluations
										    [old_award -
										     1] !=
								    0) {
								--g_pilot_data
									  .faction_statistics
										  [g_pilot_data
											   .current_faction_id]
									  .mission_evaluations
										  [old_award -
										   1];
							}
							g_pilot_data
								.faction_statistics
									[g_pilot_data
										 .current_faction_id]
								.sp_training_missions
									[mission_id]
								.award_level =
								(int)award;
							++g_pilot_data
								  .faction_statistics
									  [g_pilot_data
										   .current_faction_id]
								  .mission_evaluations
									  [award -
									   1];
						}
					} else if (
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.sp_training_missions
								[mission_id]
							.award_level ==
						FAILED_AWARD) {
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.sp_training_missions
								[mission_id]
							.award_level = 0;
						if (g_pilot_data
							    .faction_statistics
								    [g_pilot_data
									     .current_faction_id]
							    .mission_evaluations
								    [FAILED_AWARD -
								     1] != 0) {
							--g_pilot_data
								  .faction_statistics
									  [g_pilot_data
										   .current_faction_id]
								  .mission_evaluations
									  [FAILED_AWARD -
									   1];
						}
					}
					XVT_LOG_DEBUG(
						"results.mission_record kind=\"training\" mission=%d flown=%d completions=%d failures=%d best=%d time=%d best_rank=%d best_margin=%u award=%d",
						mission_id,
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.sp_training_missions
								[mission_id]
							.number_times_flown,
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.sp_training_missions
								[mission_id]
							.completed_count,
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.sp_training_missions
								[mission_id]
							.failed_count,
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.sp_training_missions
								[mission_id]
							.best_score,
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.sp_training_missions
								[mission_id]
							.best_time,
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.sp_training_missions
								[mission_id]
							.best_placement,
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.sp_training_missions
								[mission_id]
							.best_margin,
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.sp_training_missions
								[mission_id]
							.award_level);
				}
				break;
			}
			case MISSION_STAT_MELEE: {
				int mission_id =
					g_pilot_data.mission_description_ids
						[MISSION_DIRECTORY_MELEES];
				++g_pilot_data
					  .faction_statistics
						  [g_pilot_data
							   .current_faction_id]
					  .sp_melee_missions[mission_id]
					  .number_times_flown;
				if (g_flight_mission_state.runtime
					    .team_goal_status
						    [(uint16_t)g_players
							     [g_local_player]
								     .team]
						    [TEAM_GOAL_PRIMARY] == 1) {
					++g_pilot_data
						  .faction_statistics
							  [g_pilot_data
								   .current_faction_id]
						  .sp_melee_missions[mission_id]
						  .completed_count;
				}
				if (g_flight_mission_state.runtime.team_goal_status
						    [(uint16_t)g_players
							     [g_local_player]
								     .team]
						    [TEAM_GOAL_PRIMARY] == 2 ||
				    g_flight_mission_state.runtime.team_goal_status
						    [(uint16_t)g_players
							     [g_local_player]
								     .team]
						    [TEAM_GOAL_PREVENT] == 1) {
					++g_pilot_data
						  .faction_statistics
							  [g_pilot_data
								   .current_faction_id]
						  .sp_melee_missions[mission_id]
						  .failed_count;
				}
				if (g_pilot_data
					    .faction_statistics
						    [g_pilot_data
							     .current_faction_id]
					    .sp_melee_missions[mission_id]
					    .best_score < score) {
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.sp_melee_missions[mission_id]
						.best_score = score;
				}
				if (g_flight_mission_state.runtime.team_mission_completion_time_seconds
						    [(uint16_t)g_players
							     [g_local_player]
								     .team] !=
					    0 &&
				    (g_flight_mission_state.runtime.team_mission_completion_time_seconds
						     [(uint16_t)g_players
							      [g_local_player]
								      .team] <
					     (unsigned int)g_pilot_data
						     .faction_statistics
							     [g_pilot_data
								      .current_faction_id]
						     .sp_melee_missions
							     [mission_id]
						     .best_time ||
				     g_pilot_data.faction_statistics
						     [g_pilot_data
							      .current_faction_id]
							     .sp_melee_missions
								     [mission_id]
							     .best_time == 0)) {
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.sp_melee_missions[mission_id]
						.best_time =
						g_flight_mission_state.runtime.team_mission_completion_time_seconds
							[(uint16_t)g_players
								 [g_local_player]
									 .team];
				}
				if (placement != 0 &&
				    ((unsigned int)g_pilot_data
						     .faction_statistics
							     [g_pilot_data
								      .current_faction_id]
						     .sp_melee_missions
							     [mission_id]
						     .best_placement >
					     (unsigned int)placement ||
				     g_pilot_data.faction_statistics
						     [g_pilot_data
							      .current_faction_id]
							     .sp_melee_missions
								     [mission_id]
							     .best_placement ==
					     0)) {
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.sp_melee_missions[mission_id]
						.best_placement =
						(int)placement;
				}
				if (placement == FIRST_PLACE &&
				    g_pilot_data.faction_statistics
						    [g_pilot_data
							     .current_faction_id]
							    .sp_melee_missions
								    [mission_id]
							    .best_margin <
					    (unsigned int)margin &&
				    margin > 0) {
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.sp_melee_missions[mission_id]
						.best_margin = margin;
				}
				if (award != 0) {
					unsigned int old_award =
						(unsigned int)g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.sp_melee_missions
								[mission_id]
							.award_level;
					if (old_award > (unsigned int)award ||
					    old_award == 0) {
						if (old_award != 0 &&
						    g_pilot_data
								    .faction_statistics
									    [g_pilot_data
										     .current_faction_id]
								    .melee_plaques
									    [old_award -
									     1] !=
							    0) {
							--g_pilot_data
								  .faction_statistics
									  [g_pilot_data
										   .current_faction_id]
								  .melee_plaques
									  [old_award -
									   1];
						}
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.sp_melee_missions
								[mission_id]
							.award_level =
							(int)award;
						++g_pilot_data
							  .faction_statistics
								  [g_pilot_data
									   .current_faction_id]
							  .melee_plaques[award -
									 1];
					}
				} else if (
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.sp_melee_missions[mission_id]
						.award_level == FAILED_AWARD) {
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.sp_melee_missions[mission_id]
						.award_level = 0;
					if (g_pilot_data
						    .faction_statistics
							    [g_pilot_data
								     .current_faction_id]
						    .melee_plaques
							    [FAILED_AWARD -
							     1] != 0) {
						--g_pilot_data
							  .faction_statistics
								  [g_pilot_data
									   .current_faction_id]
							  .melee_plaques
								  [FAILED_AWARD -
								   1];
					}
				}
				XVT_LOG_DEBUG(
					"results.mission_record kind=\"melee\" mission=%d flown=%d completions=%d failures=%d best=%d time=%d best_rank=%d best_margin=%u award=%d",
					mission_id,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.sp_melee_missions[mission_id]
						.number_times_flown,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.sp_melee_missions[mission_id]
						.completed_count,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.sp_melee_missions[mission_id]
						.failed_count,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.sp_melee_missions[mission_id]
						.best_score,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.sp_melee_missions[mission_id]
						.best_time,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.sp_melee_missions[mission_id]
						.best_placement,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.sp_melee_missions[mission_id]
						.best_margin,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.sp_melee_missions[mission_id]
						.award_level);
				break;
			}
			case MISSION_STAT_COMBAT: {
				int mission_id =
					g_pilot_data.mission_description_ids
						[MISSION_DIRECTORY_COMBAT_ENGAGEMENTS];
				++g_pilot_data
					  .faction_statistics
						  [g_pilot_data
							   .current_faction_id]
					  .sp_combat_missions[mission_id]
					  .number_times_flown;
				if (g_flight_mission_state.runtime
					    .team_goal_status
						    [(uint16_t)g_players
							     [g_local_player]
								     .team]
						    [TEAM_GOAL_PRIMARY] == 1) {
					++g_pilot_data
						  .faction_statistics
							  [g_pilot_data
								   .current_faction_id]
						  .sp_combat_missions
							  [mission_id]
						  .completed_count;
				}
				if (g_flight_mission_state.runtime.team_goal_status
						    [(uint16_t)g_players
							     [g_local_player]
								     .team]
						    [TEAM_GOAL_PRIMARY] == 2 ||
				    g_flight_mission_state.runtime.team_goal_status
						    [(uint16_t)g_players
							     [g_local_player]
								     .team]
						    [TEAM_GOAL_PREVENT] == 1) {
					++g_pilot_data
						  .faction_statistics
							  [g_pilot_data
								   .current_faction_id]
						  .sp_combat_missions
							  [mission_id]
						  .failed_count;
				}
				if (g_flight_mission_state
					    .player_flight_group_wave_mode !=
				    CRAFT_WAVES_UNLIMITED) {
					if (g_pilot_data
						    .faction_statistics
							    [g_pilot_data
								     .current_faction_id]
						    .sp_combat_missions
							    [mission_id]
						    .best_score < score) {
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.sp_combat_missions
								[mission_id]
							.best_score = score;
					}
					if (g_flight_mission_state.runtime.team_mission_completion_time_seconds
							    [(uint16_t)g_players
								     [g_local_player]
									     .team] !=
						    0 &&
					    (g_flight_mission_state.runtime.team_mission_completion_time_seconds
							     [(uint16_t)g_players
								      [g_local_player]
									      .team] <
						     (unsigned int)g_pilot_data
							     .faction_statistics
								     [g_pilot_data
									      .current_faction_id]
							     .sp_combat_missions
								     [mission_id]
							     .best_time ||
					     g_pilot_data
							     .faction_statistics
								     [g_pilot_data
									      .current_faction_id]
							     .sp_combat_missions
								     [mission_id]
							     .best_time == 0)) {
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.sp_combat_missions
								[mission_id]
							.best_time =
							g_flight_mission_state
								.runtime
								.team_mission_completion_time_seconds
									[(uint16_t)g_players
										 [g_local_player]
											 .team];
					}
				}
				if (award != 0) {
					unsigned int old_award =
						(unsigned int)g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.sp_combat_missions
								[mission_id]
							.award_level;
					if (old_award > (unsigned int)award ||
					    old_award == 0) {
						if (old_award != 0 &&
						    g_pilot_data
								    .faction_statistics
									    [g_pilot_data
										     .current_faction_id]
								    .mission_evaluations
									    [old_award -
									     1] !=
							    0) {
							--g_pilot_data
								  .faction_statistics
									  [g_pilot_data
										   .current_faction_id]
								  .mission_evaluations
									  [old_award -
									   1];
						}
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.sp_combat_missions
								[mission_id]
							.award_level =
							(int)award;
						++g_pilot_data
							  .faction_statistics
								  [g_pilot_data
									   .current_faction_id]
							  .mission_evaluations
								  [award - 1];
					}
				} else if (
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.sp_combat_missions[mission_id]
						.award_level == FAILED_AWARD) {
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.sp_combat_missions[mission_id]
						.award_level = 0;
					if (g_pilot_data
						    .faction_statistics
							    [g_pilot_data
								     .current_faction_id]
						    .mission_evaluations
							    [FAILED_AWARD -
							     1] != 0) {
						--g_pilot_data
							  .faction_statistics
								  [g_pilot_data
									   .current_faction_id]
							  .mission_evaluations
								  [FAILED_AWARD -
								   1];
					}
				}
				XVT_LOG_DEBUG(
					"results.mission_record kind=\"combat\" mission=%d flown=%d completions=%d failures=%d best=%d time=%d best_rank=%d best_margin=%u award=%d",
					mission_id,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.sp_combat_missions[mission_id]
						.number_times_flown,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.sp_combat_missions[mission_id]
						.completed_count,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.sp_combat_missions[mission_id]
						.failed_count,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.sp_combat_missions[mission_id]
						.best_score,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.sp_combat_missions[mission_id]
						.best_time,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.sp_combat_missions[mission_id]
						.best_placement,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.sp_combat_missions[mission_id]
						.best_margin,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.sp_combat_missions[mission_id]
						.award_level);
				break;
			}
			}
		} else {
			switch (stat_type) {
			case MISSION_STAT_TRAINING: {
				unsigned int old_award;

				int mission_id =
					g_pilot_data.mission_description_ids
						[MISSION_DIRECTORY_TRAINING_EXERCISES];
				if (g_pilot_data.mission_sequence_active == 1) {
					++g_pilot_data
						  .faction_statistics
							  [g_pilot_data
								   .current_faction_id]
						  .mp_campaign_missions
							  [mission_id - 1]
						  .number_times_flown;
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.mp_campaign_missions
							[mission_id - 1]
						.campaign_id =
						g_pilot_data.mission_description_ids
							[MISSION_DIRECTORY_CAMPAIGNS];
					if (g_pilot_data
						    .teams[g_pilot_data.team]
						    .is_mission_completed !=
					    0) {
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.mp_campaign_missions
								[mission_id - 1]
							.is_completed = 1;
					}
					if (g_flight_mission_state
						    .player_flight_group_wave_mode !=
					    CRAFT_WAVES_UNLIMITED) {
						if (g_pilot_data
							    .faction_statistics
								    [g_pilot_data
									     .current_faction_id]
							    .mp_campaign_missions
								    [mission_id -
								     1]
							    .best_score <
						    (unsigned int)score) {
							g_pilot_data
								.faction_statistics
									[g_pilot_data
										 .current_faction_id]
								.mp_campaign_missions
									[mission_id -
									 1]
								.best_score =
								score;
						}
						if (g_flight_mission_state
								    .runtime
								    .team_mission_completion_time_seconds
									    [(uint16_t)g_players
										     [g_local_player]
											     .team] !=
							    0 &&
						    (g_flight_mission_state
								     .runtime
								     .team_mission_completion_time_seconds
									     [(uint16_t)g_players
										      [g_local_player]
											      .team] <
							     (unsigned int)g_pilot_data
								     .faction_statistics
									     [g_pilot_data
										      .current_faction_id]
								     .mp_campaign_missions
									     [mission_id -
									      1]
								     .best_time ||
						     g_pilot_data
								     .faction_statistics
									     [g_pilot_data
										      .current_faction_id]
								     .mp_campaign_missions
									     [mission_id -
									      1]
								     .best_time ==
							     0)) {
							g_pilot_data
								.faction_statistics
									[g_pilot_data
										 .current_faction_id]
								.mp_campaign_missions
									[mission_id -
									 1]
								.best_time =
								g_flight_mission_state
									.runtime
									.team_mission_completion_time_seconds
										[(uint16_t)g_players
											 [g_local_player]
												 .team];
						}
						if (g_pilot_data.teams[g_pilot_data
									       .team]
								    .is_mission_completed !=
							    0 &&
						    g_game_config.difficulty <=
							    GAME_DIFFICULTY_HARD) {
							g_pilot_data
								.faction_statistics
									[g_pilot_data
										 .current_faction_id]
								.mp_campaign_missions
									[mission_id -
									 1]
								.award_eligible =
								1;
						}
					}
					if (award != 0) {
						old_award =
							(unsigned int)g_pilot_data
								.faction_statistics
									[g_pilot_data
										 .current_faction_id]
								.mp_campaign_missions
									[mission_id -
									 1]
								.award_level;
						if (old_award > (unsigned int)
									award ||
						    old_award == 0) {
							if (old_award ==
								    FAILED_AWARD &&
							    g_pilot_data
									    .faction_statistics
										    [g_pilot_data
											     .current_faction_id]
									    .mission_evaluations
										    [FAILED_AWARD -
										     1] !=
								    0) {
								--g_pilot_data
									  .faction_statistics
										  [g_pilot_data
											   .current_faction_id]
									  .mission_evaluations
										  [FAILED_AWARD -
										   1];
							}
							g_pilot_data
								.faction_statistics
									[g_pilot_data
										 .current_faction_id]
								.mp_campaign_missions
									[mission_id -
									 1]
								.award_level =
								(int)award;
						}
						if (award != FAILED_AWARD ||
						    g_pilot_data
								    .faction_statistics
									    [g_pilot_data
										     .current_faction_id]
								    .mp_campaign_missions
									    [mission_id -
									     1]
								    .award_level ==
							    0) {
							++g_pilot_data
								  .faction_statistics
									  [g_pilot_data
										   .current_faction_id]
								  .mission_evaluations
									  [award -
									   1];
						}
					} else if (
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.mp_campaign_missions
								[mission_id - 1]
							.award_level ==
						FAILED_AWARD) {
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.mp_campaign_missions
								[mission_id - 1]
							.award_level = 0;
						if (g_pilot_data
							    .faction_statistics
								    [g_pilot_data
									     .current_faction_id]
							    .mission_evaluations
								    [FAILED_AWARD -
								     1] != 0) {
							--g_pilot_data
								  .faction_statistics
									  [g_pilot_data
										   .current_faction_id]
								  .mission_evaluations
									  [FAILED_AWARD -
									   1];
						}
					}
					XVT_LOG_DEBUG(
						"results.campaign_mission_record record=\"multi\" mission=%d campaign=%d flown=%d completed=%d eligible=%d best=%u time=%d award=%d",
						mission_id,
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.mp_campaign_missions
								[mission_id - 1]
							.campaign_id,
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.mp_campaign_missions
								[mission_id - 1]
							.number_times_flown,
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.mp_campaign_missions
								[mission_id - 1]
							.is_completed,
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.mp_campaign_missions
								[mission_id - 1]
							.award_eligible,
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.mp_campaign_missions
								[mission_id - 1]
							.best_score,
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.mp_campaign_missions
								[mission_id - 1]
							.best_time,
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.mp_campaign_missions
								[mission_id - 1]
							.award_level);
				} else {
					++g_pilot_data
						  .faction_statistics
							  [g_pilot_data
								   .current_faction_id]
						  .mp_training_missions
							  [mission_id]
						  .number_times_flown;
					if (g_flight_mission_state.runtime.team_goal_status
						    [(uint16_t)g_players
							     [g_local_player]
								     .team]
						    [TEAM_GOAL_PRIMARY] == 1) {
						++g_pilot_data
							  .faction_statistics
								  [g_pilot_data
									   .current_faction_id]
							  .mp_training_missions
								  [mission_id]
							  .completed_count;
					}
					if (g_flight_mission_state.runtime.team_goal_status
							    [(uint16_t)g_players
								     [g_local_player]
									     .team]
							    [TEAM_GOAL_PRIMARY] ==
						    2 ||
					    g_flight_mission_state.runtime.team_goal_status
							    [(uint16_t)g_players
								     [g_local_player]
									     .team]
							    [TEAM_GOAL_PREVENT] ==
						    1) {
						++g_pilot_data
							  .faction_statistics
								  [g_pilot_data
									   .current_faction_id]
							  .mp_training_missions
								  [mission_id]
							  .failed_count;
					}
					if (g_flight_mission_state
						    .player_flight_group_wave_mode !=
					    CRAFT_WAVES_UNLIMITED) {
						if (g_pilot_data
							    .faction_statistics
								    [g_pilot_data
									     .current_faction_id]
							    .mp_training_missions
								    [mission_id]
							    .best_score <
						    score) {
							g_pilot_data
								.faction_statistics
									[g_pilot_data
										 .current_faction_id]
								.mp_training_missions
									[mission_id]
								.best_score =
								score;
						}
						if (g_flight_mission_state
								    .runtime
								    .team_mission_completion_time_seconds
									    [(uint16_t)g_players
										     [g_local_player]
											     .team] !=
							    0 &&
						    (g_flight_mission_state
								     .runtime
								     .team_mission_completion_time_seconds
									     [(uint16_t)g_players
										      [g_local_player]
											      .team] <
							     (unsigned int)g_pilot_data
								     .faction_statistics
									     [g_pilot_data
										      .current_faction_id]
								     .mp_training_missions
									     [mission_id]
								     .best_time ||
						     g_pilot_data
								     .faction_statistics
									     [g_pilot_data
										      .current_faction_id]
								     .mp_training_missions
									     [mission_id]
								     .best_time ==
							     0)) {
							g_pilot_data
								.faction_statistics
									[g_pilot_data
										 .current_faction_id]
								.mp_training_missions
									[mission_id]
								.best_time =
								g_flight_mission_state
									.runtime
									.team_mission_completion_time_seconds
										[(uint16_t)g_players
											 [g_local_player]
												 .team];
						}
					}
					if (award != 0) {
						old_award =
							(unsigned int)g_pilot_data
								.faction_statistics
									[g_pilot_data
										 .current_faction_id]
								.mp_training_missions
									[mission_id]
								.award_level;
						if (old_award > (unsigned int)
									award ||
						    old_award == 0) {
							if (old_award ==
								    FAILED_AWARD &&
							    g_pilot_data
									    .faction_statistics
										    [g_pilot_data
											     .current_faction_id]
									    .mission_evaluations
										    [FAILED_AWARD -
										     1] !=
								    0) {
								--g_pilot_data
									  .faction_statistics
										  [g_pilot_data
											   .current_faction_id]
									  .mission_evaluations
										  [FAILED_AWARD -
										   1];
							}
							g_pilot_data
								.faction_statistics
									[g_pilot_data
										 .current_faction_id]
								.mp_training_missions
									[mission_id]
								.award_level =
								(int)award;
						}
						if (award != FAILED_AWARD ||
						    g_pilot_data
								    .faction_statistics
									    [g_pilot_data
										     .current_faction_id]
								    .mp_training_missions
									    [mission_id]
								    .award_level ==
							    0) {
							++g_pilot_data
								  .faction_statistics
									  [g_pilot_data
										   .current_faction_id]
								  .mission_evaluations
									  [award -
									   1];
						}
					} else if (
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.mp_training_missions
								[mission_id]
							.award_level ==
						FAILED_AWARD) {
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.mp_training_missions
								[mission_id]
							.award_level = 0;
						if (g_pilot_data
							    .faction_statistics
								    [g_pilot_data
									     .current_faction_id]
							    .mission_evaluations
								    [FAILED_AWARD -
								     1] != 0) {
							--g_pilot_data
								  .faction_statistics
									  [g_pilot_data
										   .current_faction_id]
								  .mission_evaluations
									  [FAILED_AWARD -
									   1];
						}
					}
					XVT_LOG_DEBUG(
						"results.multi_mission_record kind=\"training\" mission=%d flown=%d completions=%d failures=%d best=%d time=%d best_rank=%d best_margin=%u award=%d place1=%d place2=%d place3=%d",
						mission_id,
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.mp_training_missions
								[mission_id]
							.number_times_flown,
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.mp_training_missions
								[mission_id]
							.completed_count,
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.mp_training_missions
								[mission_id]
							.failed_count,
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.mp_training_missions
								[mission_id]
							.best_score,
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.mp_training_missions
								[mission_id]
							.best_time,
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.mp_training_missions
								[mission_id]
							.best_placement,
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.mp_training_missions
								[mission_id]
							.best_margin,
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.mp_training_missions
								[mission_id]
							.award_level,
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.mp_training_missions
								[mission_id]
							.first_place_count,
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.mp_training_missions
								[mission_id]
							.second_place_count,
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.mp_training_missions
								[mission_id]
							.third_place_count);
				}
				break;
			}
			case MISSION_STAT_MELEE: {
				int mission_id =
					g_pilot_data.mission_description_ids
						[MISSION_DIRECTORY_MELEES];
				++g_pilot_data
					  .faction_statistics
						  [g_pilot_data
							   .current_faction_id]
					  .mp_melee_missions[mission_id]
					  .number_times_flown;
				if (g_flight_mission_state.runtime
					    .team_goal_status
						    [(uint16_t)g_players
							     [g_local_player]
								     .team]
						    [TEAM_GOAL_PRIMARY] == 1) {
					++g_pilot_data
						  .faction_statistics
							  [g_pilot_data
								   .current_faction_id]
						  .mp_melee_missions[mission_id]
						  .completed_count;
				}
				if (g_flight_mission_state.runtime.team_goal_status
						    [(uint16_t)g_players
							     [g_local_player]
								     .team]
						    [TEAM_GOAL_PRIMARY] == 2 ||
				    g_flight_mission_state.runtime.team_goal_status
						    [(uint16_t)g_players
							     [g_local_player]
								     .team]
						    [TEAM_GOAL_PREVENT] == 1) {
					++g_pilot_data
						  .faction_statistics
							  [g_pilot_data
								   .current_faction_id]
						  .mp_melee_missions[mission_id]
						  .failed_count;
				}
				if (g_pilot_data
					    .faction_statistics
						    [g_pilot_data
							     .current_faction_id]
					    .mp_melee_missions[mission_id]
					    .best_score < score) {
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.mp_melee_missions[mission_id]
						.best_score = score;
				}
				if (g_flight_mission_state.runtime.team_mission_completion_time_seconds
						    [(uint16_t)g_players
							     [g_local_player]
								     .team] !=
					    0 &&
				    (g_flight_mission_state.runtime.team_mission_completion_time_seconds
						     [(uint16_t)g_players
							      [g_local_player]
								      .team] <
					     (unsigned int)g_pilot_data
						     .faction_statistics
							     [g_pilot_data
								      .current_faction_id]
						     .mp_melee_missions
							     [mission_id]
						     .best_time ||
				     g_pilot_data.faction_statistics
						     [g_pilot_data
							      .current_faction_id]
							     .mp_melee_missions
								     [mission_id]
							     .best_time == 0)) {
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.mp_melee_missions[mission_id]
						.best_time =
						g_flight_mission_state.runtime.team_mission_completion_time_seconds
							[(uint16_t)g_players
								 [g_local_player]
									 .team];
				}
				if (placement == 1) {
					++g_pilot_data
						  .faction_statistics
							  [g_pilot_data
								   .current_faction_id]
						  .mp_melee_missions[mission_id]
						  .first_place_count;
				} else if (placement == 2) {
					++g_pilot_data
						  .faction_statistics
							  [g_pilot_data
								   .current_faction_id]
						  .mp_melee_missions[mission_id]
						  .second_place_count;
				} else if (placement == 3) {
					++g_pilot_data
						  .faction_statistics
							  [g_pilot_data
								   .current_faction_id]
						  .mp_melee_missions[mission_id]
						  .third_place_count;
				}
				if (placement != 0 &&
				    ((unsigned int)g_pilot_data
						     .faction_statistics
							     [g_pilot_data
								      .current_faction_id]
						     .mp_melee_missions
							     [mission_id]
						     .best_placement >
					     (unsigned int)placement ||
				     g_pilot_data.faction_statistics
						     [g_pilot_data
							      .current_faction_id]
							     .mp_melee_missions
								     [mission_id]
							     .best_placement ==
					     0)) {
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.mp_melee_missions[mission_id]
						.best_placement =
						(int)placement;
				}
				if (placement == FIRST_PLACE &&
				    g_pilot_data.faction_statistics
						    [g_pilot_data
							     .current_faction_id]
							    .mp_melee_missions
								    [mission_id]
							    .best_margin <
					    (unsigned int)margin &&
				    margin > 0) {
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.mp_melee_missions[mission_id]
						.best_margin = margin;
				}
				if (award != 0) {
					unsigned int old_award =
						(unsigned int)g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.mp_melee_missions
								[mission_id]
							.award_level;
					if (old_award > (unsigned int)award ||
					    old_award == 0) {
						if (old_award == FAILED_AWARD &&
						    g_pilot_data
								    .faction_statistics
									    [g_pilot_data
										     .current_faction_id]
								    .melee_plaques
									    [FAILED_AWARD -
									     1] !=
							    0) {
							--g_pilot_data
								  .faction_statistics
									  [g_pilot_data
										   .current_faction_id]
								  .melee_plaques
									  [FAILED_AWARD -
									   1];
						}
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.mp_melee_missions
								[mission_id]
							.award_level =
							(int)award;
					}
					if (award != FAILED_AWARD ||
					    g_pilot_data
							    .faction_statistics
								    [g_pilot_data
									     .current_faction_id]
							    .mp_melee_missions
								    [mission_id]
							    .award_level == 0) {
						++g_pilot_data
							  .faction_statistics
								  [g_pilot_data
									   .current_faction_id]
							  .melee_plaques[award -
									 1];
					}
				} else if (
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.mp_melee_missions[mission_id]
						.award_level == FAILED_AWARD) {
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.mp_melee_missions[mission_id]
						.award_level = 0;
					if (g_pilot_data
						    .faction_statistics
							    [g_pilot_data
								     .current_faction_id]
						    .melee_plaques
							    [FAILED_AWARD -
							     1] != 0) {
						--g_pilot_data
							  .faction_statistics
								  [g_pilot_data
									   .current_faction_id]
							  .melee_plaques
								  [FAILED_AWARD -
								   1];
					}
				}
				XVT_LOG_DEBUG(
					"results.multi_mission_record kind=\"melee\" mission=%d flown=%d completions=%d failures=%d best=%d time=%d best_rank=%d best_margin=%u award=%d place1=%d place2=%d place3=%d",
					mission_id,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.mp_melee_missions[mission_id]
						.number_times_flown,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.mp_melee_missions[mission_id]
						.completed_count,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.mp_melee_missions[mission_id]
						.failed_count,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.mp_melee_missions[mission_id]
						.best_score,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.mp_melee_missions[mission_id]
						.best_time,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.mp_melee_missions[mission_id]
						.best_placement,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.mp_melee_missions[mission_id]
						.best_margin,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.mp_melee_missions[mission_id]
						.award_level,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.mp_melee_missions[mission_id]
						.first_place_count,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.mp_melee_missions[mission_id]
						.second_place_count,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.mp_melee_missions[mission_id]
						.third_place_count);
				break;
			}
			case MISSION_STAT_COMBAT: {
				int mission_id =
					g_pilot_data.mission_description_ids
						[MISSION_DIRECTORY_COMBAT_ENGAGEMENTS];
				++g_pilot_data
					  .faction_statistics
						  [g_pilot_data
							   .current_faction_id]
					  .mp_combat_missions[mission_id]
					  .number_times_flown;
				if (g_flight_mission_state.runtime
					    .team_goal_status
						    [(uint16_t)g_players
							     [g_local_player]
								     .team]
						    [TEAM_GOAL_PRIMARY] == 1) {
					++g_pilot_data
						  .faction_statistics
							  [g_pilot_data
								   .current_faction_id]
						  .mp_combat_missions
							  [mission_id]
						  .completed_count;
				}
				if (g_flight_mission_state.runtime.team_goal_status
						    [(uint16_t)g_players
							     [g_local_player]
								     .team]
						    [TEAM_GOAL_PRIMARY] == 2 ||
				    g_flight_mission_state.runtime.team_goal_status
						    [(uint16_t)g_players
							     [g_local_player]
								     .team]
						    [TEAM_GOAL_PREVENT] == 1) {
					++g_pilot_data
						  .faction_statistics
							  [g_pilot_data
								   .current_faction_id]
						  .mp_combat_missions
							  [mission_id]
						  .failed_count;
				}
				if (g_flight_mission_state
					    .player_flight_group_wave_mode !=
				    CRAFT_WAVES_UNLIMITED) {
					if (g_pilot_data
						    .faction_statistics
							    [g_pilot_data
								     .current_faction_id]
						    .mp_combat_missions
							    [mission_id]
						    .best_score < score) {
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.mp_combat_missions
								[mission_id]
							.best_score = score;
					}
					if (g_flight_mission_state.runtime.team_mission_completion_time_seconds
							    [(uint16_t)g_players
								     [g_local_player]
									     .team] !=
						    0 &&
					    (g_flight_mission_state.runtime.team_mission_completion_time_seconds
							     [(uint16_t)g_players
								      [g_local_player]
									      .team] <
						     (unsigned int)g_pilot_data
							     .faction_statistics
								     [g_pilot_data
									      .current_faction_id]
							     .mp_combat_missions
								     [mission_id]
							     .best_time ||
					     g_pilot_data
							     .faction_statistics
								     [g_pilot_data
									      .current_faction_id]
							     .mp_combat_missions
								     [mission_id]
							     .best_time == 0)) {
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.mp_combat_missions
								[mission_id]
							.best_time =
							g_flight_mission_state
								.runtime
								.team_mission_completion_time_seconds
									[(uint16_t)g_players
										 [g_local_player]
											 .team];
					}
				}
				if (award != 0) {
					unsigned int old_award =
						(unsigned int)g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.mp_combat_missions
								[mission_id]
							.award_level;
					if (old_award > (unsigned int)award ||
					    old_award == 0) {
						if (old_award == FAILED_AWARD &&
						    g_pilot_data
								    .faction_statistics
									    [g_pilot_data
										     .current_faction_id]
								    .mission_evaluations
									    [FAILED_AWARD -
									     1] !=
							    0) {
							--g_pilot_data
								  .faction_statistics
									  [g_pilot_data
										   .current_faction_id]
								  .mission_evaluations
									  [FAILED_AWARD -
									   1];
						}
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.mp_combat_missions
								[mission_id]
							.award_level =
							(int)award;
					}
					if (award != FAILED_AWARD ||
					    g_pilot_data
							    .faction_statistics
								    [g_pilot_data
									     .current_faction_id]
							    .mp_combat_missions
								    [mission_id]
							    .award_level == 0) {
						++g_pilot_data
							  .faction_statistics
								  [g_pilot_data
									   .current_faction_id]
							  .mission_evaluations
								  [award - 1];
					}
				} else if (
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.mp_combat_missions[mission_id]
						.award_level == FAILED_AWARD) {
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.mp_combat_missions[mission_id]
						.award_level = 0;
					if (g_pilot_data
						    .faction_statistics
							    [g_pilot_data
								     .current_faction_id]
						    .mission_evaluations
							    [FAILED_AWARD -
							     1] != 0) {
						--g_pilot_data
							  .faction_statistics
								  [g_pilot_data
									   .current_faction_id]
							  .mission_evaluations
								  [FAILED_AWARD -
								   1];
					}
				}
				XVT_LOG_DEBUG(
					"results.multi_mission_record kind=\"combat\" mission=%d flown=%d completions=%d failures=%d best=%d time=%d best_rank=%d best_margin=%u award=%d place1=%d place2=%d place3=%d",
					mission_id,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.mp_combat_missions[mission_id]
						.number_times_flown,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.mp_combat_missions[mission_id]
						.completed_count,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.mp_combat_missions[mission_id]
						.failed_count,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.mp_combat_missions[mission_id]
						.best_score,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.mp_combat_missions[mission_id]
						.best_time,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.mp_combat_missions[mission_id]
						.best_placement,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.mp_combat_missions[mission_id]
						.best_margin,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.mp_combat_missions[mission_id]
						.award_level,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.mp_combat_missions[mission_id]
						.first_place_count,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.mp_combat_missions[mission_id]
						.second_place_count,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.mp_combat_missions[mission_id]
						.third_place_count);
				break;
			}
			}
		}

		if (g_pilot_data.mission_sequence_active == 1 &&
		    stat_type == MISSION_STAT_TRAINING) {
			int campaign_id = g_pilot_data.mission_description_ids
						  [MISSION_DIRECTORY_CAMPAIGNS];
			if ((unsigned int)campaign_id >=
			    sizeof(g_pilot_data.faction_statistics[0]
					   .sp_campaigns) /
				    sizeof(g_pilot_data.faction_statistics[0]
						   .sp_campaigns[0])) {
				XVT_LOG_WARN(
					"results.sequence_id_invalid kind=\"campaign\" id=%d entries=%u",
					campaign_id,
					(unsigned)(sizeof(g_pilot_data
								  .faction_statistics
									  [0]
								  .sp_campaigns) /
						   sizeof(g_pilot_data
								  .faction_statistics
									  [0]
								  .sp_campaigns
									  [0])));
			}
			g_pilot_data.campaign_sequence_state
				.last_mission_completed =
				g_pilot_data.teams[g_pilot_data.team]
					.is_mission_completed;
			if (g_pilot_data.campaign_sequence_state
				    .last_mission_completed != 0) {
				if (g_pilot_data.campaign_sequence_state
					    .current_mission_index == 0) {
					g_pilot_data.campaign_sequence_state
						.cumulative_score =
						g_pilot_data.mission_score;
				} else {
					g_pilot_data.campaign_sequence_state
						.cumulative_score +=
						g_pilot_data.mission_score;
				}
			}
			if (g_pilot_data.campaign_sequence_state
				    .human_player_count < 2) {
				if (g_pilot_data.campaign_sequence_state
					    .current_mission_index == 0) {
					++g_pilot_data
						  .faction_statistics
							  [g_pilot_data
								   .current_faction_id]
						  .sp_campaigns[campaign_id]
						  .attempt_count;
				}
				if (g_pilot_data.campaign_sequence_state
					    .last_mission_completed != 0) {
					if ((unsigned int)g_pilot_data
						    .faction_statistics
							    [g_pilot_data
								     .current_faction_id]
						    .sp_campaigns[campaign_id]
						    .next_mission_index <
					    (unsigned int)(g_pilot_data
								   .campaign_sequence_state
								   .current_mission_index +
							   1)) {
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.sp_campaigns
								[campaign_id]
							.next_mission_index =
							g_pilot_data
								.campaign_sequence_state
								.current_mission_index +
							1;
					}
					if (g_pilot_data
						    .faction_statistics
							    [g_pilot_data
								     .current_faction_id]
						    .sp_campaigns[campaign_id]
						    .best_score <
					    g_pilot_data.campaign_sequence_state
						    .cumulative_score) {
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.sp_campaigns
								[campaign_id]
							.best_score =
							g_pilot_data
								.campaign_sequence_state
								.cumulative_score;
					}
					if (g_pilot_data.campaign_sequence_state
						    .mission_count ==
					    g_pilot_data.campaign_sequence_state
						    .current_mission_index) {
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.sp_campaigns
								[campaign_id]
							.is_finished = 1;
					}
				} else if (
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.sp_campaigns[campaign_id]
						.best_score <
					g_pilot_data.campaign_sequence_state
							.cumulative_score +
						g_pilot_data.mission_score) {
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.sp_campaigns[campaign_id]
						.best_score =
						g_pilot_data
							.campaign_sequence_state
							.cumulative_score +
						g_pilot_data.mission_score;
				}
				XVT_LOG_DEBUG(
					"results.campaign_record record=\"single\" campaign=%d index=%d missions=%d completed=%d score=%d attempts=%d next=%d best=%d finished=%d",
					campaign_id,
					(int)g_pilot_data
						.campaign_sequence_state
						.current_mission_index,
					(int)g_pilot_data
						.campaign_sequence_state
						.mission_count,
					(int)g_pilot_data
						.campaign_sequence_state
						.last_mission_completed,
					(int)g_pilot_data
						.campaign_sequence_state
						.cumulative_score,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.sp_campaigns[campaign_id]
						.attempt_count,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.sp_campaigns[campaign_id]
						.next_mission_index,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.sp_campaigns[campaign_id]
						.best_score,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.sp_campaigns[campaign_id]
						.is_finished);
			} else {
				if (g_pilot_data.campaign_sequence_state
					    .current_mission_index == 0) {
					++g_pilot_data
						  .faction_statistics
							  [g_pilot_data
								   .current_faction_id]
						  .mp_campaigns[campaign_id]
						  .attempt_count;
				}
				if (g_pilot_data.campaign_sequence_state
					    .last_mission_completed != 0) {
					if ((unsigned int)g_pilot_data
						    .faction_statistics
							    [g_pilot_data
								     .current_faction_id]
						    .mp_campaigns[campaign_id]
						    .next_mission_index <
					    (unsigned int)(g_pilot_data
								   .campaign_sequence_state
								   .current_mission_index +
							   1)) {
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.mp_campaigns
								[campaign_id]
							.next_mission_index =
							g_pilot_data
								.campaign_sequence_state
								.current_mission_index +
							1;
					}
					if (g_pilot_data
						    .faction_statistics
							    [g_pilot_data
								     .current_faction_id]
						    .mp_campaigns[campaign_id]
						    .best_score <
					    g_pilot_data.campaign_sequence_state
						    .cumulative_score) {
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.mp_campaigns
								[campaign_id]
							.best_score =
							g_pilot_data
								.campaign_sequence_state
								.cumulative_score;
					}
					if (g_pilot_data.campaign_sequence_state
						    .mission_count ==
					    g_pilot_data.campaign_sequence_state
						    .current_mission_index) {
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.mp_campaigns
								[campaign_id]
							.is_finished = 1;
					}
				} else if (
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.mp_campaigns[campaign_id]
						.best_score <
					g_pilot_data.campaign_sequence_state
							.cumulative_score +
						g_pilot_data.mission_score) {
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.mp_campaigns[campaign_id]
						.best_score =
						g_pilot_data
							.campaign_sequence_state
							.cumulative_score +
						g_pilot_data.mission_score;
				}
				XVT_LOG_DEBUG(
					"results.campaign_record record=\"multi\" campaign=%d index=%d missions=%d completed=%d score=%d attempts=%d next=%d best=%d finished=%d",
					campaign_id,
					(int)g_pilot_data
						.campaign_sequence_state
						.current_mission_index,
					(int)g_pilot_data
						.campaign_sequence_state
						.mission_count,
					(int)g_pilot_data
						.campaign_sequence_state
						.last_mission_completed,
					(int)g_pilot_data
						.campaign_sequence_state
						.cumulative_score,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.mp_campaigns[campaign_id]
						.attempt_count,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.mp_campaigns[campaign_id]
						.next_mission_index,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.mp_campaigns[campaign_id]
						.best_score,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.mp_campaigns[campaign_id]
						.is_finished);
			}
		}

		if (g_pilot_data.mission_sequence_active == 1 &&
		    stat_type == MISSION_STAT_MELEE) {
			int tournament_id =
				g_pilot_data.mission_description_ids
					[MISSION_DIRECTORY_TOURNAMENTS];
			if ((unsigned int)tournament_id >=
			    sizeof(g_pilot_data.faction_statistics[0]
					   .sp_tournaments) /
				    sizeof(g_pilot_data.faction_statistics[0]
						   .sp_tournaments[0])) {
				XVT_LOG_WARN(
					"results.sequence_id_invalid kind=\"tournament\" id=%d entries=%u",
					tournament_id,
					(unsigned)(sizeof(g_pilot_data
								  .faction_statistics
									  [0]
								  .sp_tournaments) /
						   sizeof(g_pilot_data
								  .faction_statistics
									  [0]
								  .sp_tournaments
									  [0])));
			}
			unsigned int participating_team_count =
				g_pilot_data.melee_tournament_sequence_state
					.participating_team_count;
			int team_mission_score;
#ifdef XVT_MODERN
			/* Inactive standings keep the previous team's score;
			 * the first one would read an uninitialized local. */
			team_mission_score = 0;
#endif
			for (team_idx = 0; team_idx < TEAM_COUNT; ++team_idx) {
				unsigned int mission_placement;

				if (g_pilot_data.melee_tournament_sequence_state
					    .team_standings[team_idx]
					    .ai_opponent_source_team_and_type_flag !=
				    -1) {
					team_mission_score =
						g_flight_mission_state.runtime
							.team_scores
								[TEAM_SCORE_BONUS]
								[team_idx] +
						g_flight_mission_state.runtime
							.team_scores
								[TEAM_SCORE_MISSION]
								[team_idx];
					unsigned int better_mission_team_count =
						0;
					/* The network player index is reused
					 * here as the other team compared with
					 * team_idx. */
					for (network_idx = 0;
					     network_idx < TEAM_COUNT;
					     ++network_idx) {
						if (network_idx != team_idx &&
						    g_pilot_data.melee_tournament_sequence_state
								    .team_standings
									    [network_idx]
								    .ai_opponent_source_team_and_type_flag !=
							    -1 &&
						    g_flight_mission_state.runtime
									    .team_scores
										    [TEAM_SCORE_BONUS]
										    [network_idx] +
								    g_flight_mission_state
									    .runtime
									    .team_scores
										    [TEAM_SCORE_MISSION]
										    [network_idx] >
							    team_mission_score) {
							++better_mission_team_count;
						}
					}
					mission_placement =
						better_mission_team_count + 1;
				} else {
					mission_placement = 0;
				}
				g_pilot_data.melee_tournament_sequence_state
					.team_standings[team_idx]
					.total_score += team_mission_score;
				if (mission_placement == 1) {
					++g_pilot_data
						  .melee_tournament_sequence_state
						  .team_standings[team_idx]
						  .first_place_count;
				} else if (mission_placement == 2) {
					++g_pilot_data
						  .melee_tournament_sequence_state
						  .team_standings[team_idx]
						  .second_place_count;
				} else if (mission_placement == 3) {
					++g_pilot_data
						  .melee_tournament_sequence_state
						  .team_standings[team_idx]
						  .third_place_count;
				}
				XVT_LOG_DEBUG(
					"results.standing_updated team=%u active=%d rank=%u score=%d total=%d place1=%d place2=%d place3=%d",
					team_idx,
					g_pilot_data.melee_tournament_sequence_state
							.team_standings
								[team_idx]
							.ai_opponent_source_team_and_type_flag !=
						-1,
					mission_placement, team_mission_score,
					g_pilot_data
						.melee_tournament_sequence_state
						.team_standings[team_idx]
						.total_score,
					g_pilot_data
						.melee_tournament_sequence_state
						.team_standings[team_idx]
						.first_place_count,
					g_pilot_data
						.melee_tournament_sequence_state
						.team_standings[team_idx]
						.second_place_count,
					g_pilot_data
						.melee_tournament_sequence_state
						.team_standings[team_idx]
						.third_place_count);
			}

			int better_team_count = 0;
			int overall_margin = 0;
			int local_team_total_score =
				g_pilot_data.melee_tournament_sequence_state
					.team_standings[(uint16_t)g_players
								[g_local_player]
									.team]
					.total_score;
			for (team_idx = 0; team_idx < TEAM_COUNT; ++team_idx) {
				if (team_idx !=
					    (uint16_t)g_players[g_local_player]
						    .team &&
				    g_pilot_data.melee_tournament_sequence_state
						    .team_standings[team_idx]
						    .ai_opponent_source_team_and_type_flag !=
					    -1) {
					if (local_team_total_score <
					    g_pilot_data
						    .melee_tournament_sequence_state
						    .team_standings[team_idx]
						    .total_score) {
						++better_team_count;
					} else if (
						local_team_total_score -
								g_pilot_data
									.melee_tournament_sequence_state
									.team_standings
										[team_idx]
									.total_score <
							overall_margin ||
						overall_margin == 0) {
						overall_margin =
							local_team_total_score -
							g_pilot_data
								.melee_tournament_sequence_state
								.team_standings
									[team_idx]
								.total_score;
					}
				}
			}
			int overall_placement = better_team_count + 1;
			int tournament_award = 0;
			if (local_team_total_score > 5000 &&
			    overall_placement < 4) {
				tournament_award = g_placement_award_levels
					[3 * participating_team_count - 4 +
					 overall_placement];
				if (tournament_award != 0) {
					if (g_pilot_data
						    .melee_tournament_sequence_state
						    .human_player_count < 3) {
						if (g_flight_mission_state
							    .difficulty !=
						    GAME_DIFFICULTY_HARD) {
							tournament_award += 2;
						} else {
							++tournament_award;
						}
						if (tournament_award >
						    MAX_STORED_AWARD) {
							tournament_award =
								MAX_STORED_AWARD;
						}
					} else if (
						g_pilot_data
							.melee_tournament_sequence_state
							.human_player_count <
						5) {
						if (g_flight_mission_state
							    .difficulty !=
						    GAME_DIFFICULTY_HARD) {
							++tournament_award;
						}
						if (tournament_award >
						    MAX_STORED_AWARD) {
							tournament_award =
								MAX_STORED_AWARD;
						}
					}
					if (overall_placement == FIRST_PLACE) {
						if (overall_margin > 20000) {
							tournament_award -= 2;
						} else if (overall_margin >
							   10000) {
							--tournament_award;
						}
						if (tournament_award < 1) {
							tournament_award = 1;
						}
					}
				}
			}
			if (tournament_award == 0 &&
			    participating_team_count >= 4 &&
			    participating_team_count ==
				    (unsigned int)overall_placement) {
				tournament_award = FAILED_AWARD;
			}
			XVT_LOG_DEBUG(
				"results.tournament_step tournament=%d index=%d missions=%d rank=%d margin=%d total=%d award=%d teams=%u humans=%u",
				tournament_id,
				g_pilot_data.melee_tournament_sequence_state
					.current_mission_index,
				g_pilot_data.melee_tournament_sequence_state
					.mission_count,
				overall_placement, overall_margin,
				local_team_total_score, tournament_award,
				participating_team_count,
				g_pilot_data.melee_tournament_sequence_state
					.human_player_count);

			if (g_pilot_data.melee_tournament_sequence_state
				    .human_player_count == 1) {
				if (g_pilot_data.melee_tournament_sequence_state
					    .current_mission_index == 0) {
					++g_pilot_data
						  .faction_statistics
							  [g_pilot_data
								   .current_faction_id]
						  .sp_tournaments[tournament_id]
						  .attempt_count;
				} else if (
					g_pilot_data.melee_tournament_sequence_state
							.mission_count -
						g_pilot_data
							.melee_tournament_sequence_state
							.current_mission_index ==
					1) {
					++g_pilot_data
						  .faction_statistics
							  [g_pilot_data
								   .current_faction_id]
						  .sp_tournaments[tournament_id]
						  .completed_count;
					if (overall_placement == 1) {
						++g_pilot_data
							  .faction_statistics
								  [g_pilot_data
									   .current_faction_id]
							  .sp_tournaments
								  [tournament_id]
							  .first_place_count;
					} else if (overall_placement == 2) {
						++g_pilot_data
							  .faction_statistics
								  [g_pilot_data
									   .current_faction_id]
							  .sp_tournaments
								  [tournament_id]
							  .second_place_count;
					} else if (overall_placement == 3) {
						++g_pilot_data
							  .faction_statistics
								  [g_pilot_data
									   .current_faction_id]
							  .sp_tournaments
								  [tournament_id]
							  .third_place_count;
					}
					if (g_pilot_data
						    .faction_statistics
							    [g_pilot_data
								     .current_faction_id]
						    .sp_tournaments
							    [tournament_id]
						    .best_score <
					    local_team_total_score) {
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.sp_tournaments
								[tournament_id]
							.best_score =
							local_team_total_score;
					}
					if ((unsigned int)g_pilot_data
							    .faction_statistics
								    [g_pilot_data
									     .current_faction_id]
							    .sp_tournaments
								    [tournament_id]
							    .best_placement >
						    (unsigned int)
							    overall_placement ||
					    g_pilot_data
							    .faction_statistics
								    [g_pilot_data
									     .current_faction_id]
							    .sp_tournaments
								    [tournament_id]
							    .best_placement ==
						    0) {
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.sp_tournaments
								[tournament_id]
							.best_placement =
							(int)overall_placement;
					}
					if (overall_placement == FIRST_PLACE &&
					    g_pilot_data
							    .faction_statistics
								    [g_pilot_data
									     .current_faction_id]
							    .sp_tournaments
								    [tournament_id]
							    .best_margin <
						    (unsigned int)
							    overall_margin &&
					    overall_margin > 0) {
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.sp_tournaments
								[tournament_id]
							.best_margin =
							overall_margin;
					}
					if (tournament_award != 0) {
						unsigned int old_award =
							(unsigned int)g_pilot_data
								.faction_statistics
									[g_pilot_data
										 .current_faction_id]
								.sp_tournaments
									[tournament_id]
								.award_level;
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.mission_awards[1] =
							(int)tournament_award;
						if (old_award >
							    (unsigned int)
								    tournament_award ||
						    old_award == 0) {
							if (old_award != 0 &&
							    g_pilot_data
									    .faction_statistics
										    [g_pilot_data
											     .current_faction_id]
									    .tournament_trophies
										    [old_award -
										     1] !=
								    0) {
								--g_pilot_data
									  .faction_statistics
										  [g_pilot_data
											   .current_faction_id]
									  .tournament_trophies
										  [old_award -
										   1];
							}
							g_pilot_data
								.faction_statistics
									[g_pilot_data
										 .current_faction_id]
								.sp_tournaments
									[tournament_id]
								.award_level = (int)
								tournament_award;
							++g_pilot_data
								  .faction_statistics
									  [g_pilot_data
										   .current_faction_id]
								  .tournament_trophies
									  [tournament_award -
									   1];
						}
					} else if (
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.sp_tournaments
								[tournament_id]
							.award_level ==
						FAILED_AWARD) {
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.sp_tournaments
								[tournament_id]
							.award_level = 0;
						if (g_pilot_data
							    .faction_statistics
								    [g_pilot_data
									     .current_faction_id]
							    .tournament_trophies
								    [FAILED_AWARD -
								     1] != 0) {
							--g_pilot_data
								  .faction_statistics
									  [g_pilot_data
										   .current_faction_id]
								  .tournament_trophies
									  [FAILED_AWARD -
									   1];
						}
					}
				}
				XVT_LOG_DEBUG(
					"results.tournament_record record=\"single\" tournament=%d attempts=%d completions=%d place1=%d place2=%d place3=%d best=%d best_rank=%d best_margin=%u award=%d",
					tournament_id,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.sp_tournaments[tournament_id]
						.attempt_count,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.sp_tournaments[tournament_id]
						.completed_count,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.sp_tournaments[tournament_id]
						.first_place_count,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.sp_tournaments[tournament_id]
						.second_place_count,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.sp_tournaments[tournament_id]
						.third_place_count,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.sp_tournaments[tournament_id]
						.best_score,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.sp_tournaments[tournament_id]
						.best_placement,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.sp_tournaments[tournament_id]
						.best_margin,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.sp_tournaments[tournament_id]
						.award_level);
			} else {
				if (g_pilot_data.melee_tournament_sequence_state
					    .current_mission_index == 0) {
					++g_pilot_data
						  .faction_statistics
							  [g_pilot_data
								   .current_faction_id]
						  .mp_tournaments[tournament_id]
						  .attempt_count;
				} else if (
					g_pilot_data.melee_tournament_sequence_state
							.mission_count -
						g_pilot_data
							.melee_tournament_sequence_state
							.current_mission_index ==
					1) {
					++g_pilot_data
						  .faction_statistics
							  [g_pilot_data
								   .current_faction_id]
						  .mp_tournaments[tournament_id]
						  .completed_count;
					if (overall_placement == 1) {
						++g_pilot_data
							  .faction_statistics
								  [g_pilot_data
									   .current_faction_id]
							  .mp_tournaments
								  [tournament_id]
							  .first_place_count;
					} else if (overall_placement == 2) {
						++g_pilot_data
							  .faction_statistics
								  [g_pilot_data
									   .current_faction_id]
							  .mp_tournaments
								  [tournament_id]
							  .second_place_count;
					} else if (overall_placement == 3) {
						++g_pilot_data
							  .faction_statistics
								  [g_pilot_data
									   .current_faction_id]
							  .mp_tournaments
								  [tournament_id]
							  .third_place_count;
					}
					if (g_pilot_data
						    .faction_statistics
							    [g_pilot_data
								     .current_faction_id]
						    .mp_tournaments
							    [tournament_id]
						    .best_score <
					    local_team_total_score) {
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.mp_tournaments
								[tournament_id]
							.best_score =
							local_team_total_score;
					}
					if ((unsigned int)g_pilot_data
							    .faction_statistics
								    [g_pilot_data
									     .current_faction_id]
							    .mp_tournaments
								    [tournament_id]
							    .best_placement >
						    (unsigned int)
							    overall_placement ||
					    g_pilot_data
							    .faction_statistics
								    [g_pilot_data
									     .current_faction_id]
							    .mp_tournaments
								    [tournament_id]
							    .best_placement ==
						    0) {
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.mp_tournaments
								[tournament_id]
							.best_placement =
							(int)overall_placement;
					}
					if (overall_placement == FIRST_PLACE &&
					    g_pilot_data
							    .faction_statistics
								    [g_pilot_data
									     .current_faction_id]
							    .mp_tournaments
								    [tournament_id]
							    .best_margin <
						    (unsigned int)
							    overall_margin &&
					    overall_margin > 0) {
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.mp_tournaments
								[tournament_id]
							.best_margin =
							overall_margin;
					}
					if (tournament_award != 0) {
						unsigned int old_award =
							(unsigned int)g_pilot_data
								.faction_statistics
									[g_pilot_data
										 .current_faction_id]
								.mp_tournaments
									[tournament_id]
								.award_level;
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.mission_awards[1] =
							(int)tournament_award;
						if (old_award >
							    (unsigned int)
								    tournament_award ||
						    old_award == 0) {
							if (old_award ==
								    FAILED_AWARD &&
							    g_pilot_data
									    .faction_statistics
										    [g_pilot_data
											     .current_faction_id]
									    .tournament_trophies
										    [FAILED_AWARD -
										     1] !=
								    0) {
								--g_pilot_data
									  .faction_statistics
										  [g_pilot_data
											   .current_faction_id]
									  .tournament_trophies
										  [FAILED_AWARD -
										   1];
							}
							g_pilot_data
								.faction_statistics
									[g_pilot_data
										 .current_faction_id]
								.mp_tournaments
									[tournament_id]
								.award_level = (int)
								tournament_award;
						}
						++g_pilot_data
							  .faction_statistics
								  [g_pilot_data
									   .current_faction_id]
							  .tournament_trophies
								  [tournament_award -
								   1];
					} else if (
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.mp_tournaments
								[tournament_id]
							.award_level ==
						FAILED_AWARD) {
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.mp_tournaments
								[tournament_id]
							.award_level = 0;
						if (g_pilot_data
							    .faction_statistics
								    [g_pilot_data
									     .current_faction_id]
							    .tournament_trophies
								    [FAILED_AWARD -
								     1] != 0) {
							--g_pilot_data
								  .faction_statistics
									  [g_pilot_data
										   .current_faction_id]
								  .tournament_trophies
									  [FAILED_AWARD -
									   1];
						}
					}
				}
				XVT_LOG_DEBUG(
					"results.tournament_record record=\"multi\" tournament=%d attempts=%d completions=%d place1=%d place2=%d place3=%d best=%d best_rank=%d best_margin=%u award=%d",
					tournament_id,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.mp_tournaments[tournament_id]
						.attempt_count,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.mp_tournaments[tournament_id]
						.completed_count,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.mp_tournaments[tournament_id]
						.first_place_count,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.mp_tournaments[tournament_id]
						.second_place_count,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.mp_tournaments[tournament_id]
						.third_place_count,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.mp_tournaments[tournament_id]
						.best_score,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.mp_tournaments[tournament_id]
						.best_placement,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.mp_tournaments[tournament_id]
						.best_margin,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.mp_tournaments[tournament_id]
						.award_level);
			}
		}

		if (g_pilot_data.mission_sequence_active == 1 &&
		    stat_type == MISSION_STAT_COMBAT) {
			int battle_id = g_pilot_data.mission_description_ids
						[MISSION_DIRECTORY_BATTLES];
			if ((unsigned int)battle_id >=
			    sizeof(g_pilot_data.faction_statistics[0]
					   .sp_battles) /
				    sizeof(g_pilot_data.faction_statistics[0]
						   .sp_battles[0])) {
				XVT_LOG_WARN(
					"results.sequence_id_invalid kind=\"battle\" id=%d entries=%u",
					battle_id,
					(unsigned)(sizeof(g_pilot_data
								  .faction_statistics
									  [0]
								  .sp_battles) /
						   sizeof(g_pilot_data
								  .faction_statistics
									  [0]
								  .sp_battles
									  [0])));
			}
			if (g_pilot_data.battle_sequence_state
				    .current_mission_index >=
			    sizeof(g_pilot_data.battle_sequence_state
					   .mission_results) /
				    sizeof(g_pilot_data.battle_sequence_state
						   .mission_results[0])) {
				XVT_LOG_WARN(
					"results.battle_index_invalid index=%u entries=%u",
					(unsigned)g_pilot_data
						.battle_sequence_state
						.current_mission_index,
					(unsigned)(sizeof(g_pilot_data
								  .battle_sequence_state
								  .mission_results) /
						   sizeof(g_pilot_data
								  .battle_sequence_state
								  .mission_results
									  [0])));
			}
			for (team_idx = 0; team_idx < TEAM_COUNT; ++team_idx) {
				if (g_flight_mission_state.runtime
						    .team_goal_status
							    [team_idx]
							    [TEAM_GOAL_PRIMARY] ==
					    1 &&
				    g_flight_mission_state.runtime
						    .team_goal_status
							    [team_idx]
							    [TEAM_GOAL_PREVENT] !=
					    1) {
					break;
				}
			}
			battle_mission_result mission_result;
			if (team_idx == TEAM_COUNT) {
				mission_result = BATTLE_MISSION_RESULT_DRAW;
			} else if (team_idx == 0) {
				mission_result =
					BATTLE_MISSION_RESULT_IMPERIAL_VICTORY;
			} else if (team_idx == 1) {
				mission_result =
					BATTLE_MISSION_RESULT_REBEL_VICTORY;
			} else {
				mission_result = BATTLE_MISSION_RESULT_DRAW;
			}
			int imperial_victories = 0;
			int rebel_victories = 0;
			g_pilot_data.battle_sequence_state.mission_results
				[g_pilot_data.battle_sequence_state
					 .current_mission_index] =
				mission_result;
			for (unsigned int result_idx = 0;
			     result_idx <= g_pilot_data.battle_sequence_state
						   .current_mission_index;
			     ++result_idx) {
				if (g_pilot_data.battle_sequence_state
					    .mission_results[result_idx] ==
				    BATTLE_MISSION_RESULT_IMPERIAL_VICTORY) {
					++imperial_victories;
				} else if (
					g_pilot_data.battle_sequence_state
						.mission_results[result_idx] ==
					BATTLE_MISSION_RESULT_REBEL_VICTORY) {
					++rebel_victories;
				}
			}
			int overall_winner;
			int victory_margin;
			if (imperial_victories ==
			    g_pilot_data.battle_sequence_state
				    .victories_needed) {
				overall_winner = 0;
				victory_margin =
					imperial_victories - rebel_victories;
			} else if (rebel_victories ==
				   g_pilot_data.battle_sequence_state
					   .victories_needed) {
				overall_winner = 1;
				victory_margin =
					rebel_victories - imperial_victories;
			} else {
				overall_winner = 2;
				victory_margin = 0;
			}
			int player_result = 0;
			if (overall_winner != 2) {
				if ((overall_winner == 0 &&
				     g_players[g_local_player].team == 0) ||
				    (overall_winner == 1 &&
				     g_players[g_local_player].team == 1)) {
					player_result = 1;
				} else {
					player_result = 2;
				}
			}
			unsigned int battle_award = 0;
			if (player_result != 0) {
				if (player_result == 2 && victory_margin >= 2) {
					battle_award = FAILED_AWARD;
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.mission_awards[3] =
						FAILED_AWARD;
				} else if (player_result == 1) {
					int enemy_players = 0;
					int allied_players = 0;
					for (player_idx = 0;
					     player_idx < PLAYER_COUNT;
					     ++player_idx) {
						if (g_players[player_idx]
							    .network
							    .direct_play_id !=
						    0) {
							if (g_players[g_local_player]
								    .team ==
							    g_players[player_idx]
								    .team) {
								++allied_players;
							} else {
								++enemy_players;
							}
						}
					}
					int player_balance =
						enemy_players - allied_players;
					if (player_balance > 0) {
						player_balance *= 2;
					}
					if (g_pilot_data.battle_sequence_state
							    .human_player_count ==
						    1 &&
					    g_flight_mission_state.difficulty ==
						    GAME_DIFFICULTY_HARD) {
						victory_margin *= 2;
					}
					int award_performance =
						victory_margin + player_balance;
					if (award_performance >= 5) {
						battle_award = 1;
					} else if (award_performance >= 4) {
						battle_award = 2;
					} else if (award_performance >= 3) {
						battle_award = 3;
					} else if (award_performance >= 2) {
						battle_award = 4;
					} else {
						battle_award = 5;
					}
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.mission_awards[3] =
						(int)battle_award;
				}
			}
			XVT_LOG_DEBUG(
				"results.battle_step battle=%d index=%u result=%d imperial=%d rebel=%d needed=%d winner=%d margin=%d outcome=%d award=%u",
				battle_id,
				(unsigned)g_pilot_data.battle_sequence_state
					.current_mission_index,
				(int)mission_result, imperial_victories,
				rebel_victories,
				g_pilot_data.battle_sequence_state
					.victories_needed,
				overall_winner, victory_margin, player_result,
				battle_award);

			if (g_pilot_data.battle_sequence_state
				    .current_mission_index == 0) {
				g_pilot_data.battle_sequence_state
					.cumulative_score =
					g_pilot_data.mission_score;
			} else {
				g_pilot_data.battle_sequence_state
					.cumulative_score +=
					g_pilot_data.mission_score;
			}
			if (g_pilot_data.battle_sequence_state
				    .human_player_count < 2) {
				if (g_pilot_data.battle_sequence_state
					    .current_mission_index == 0) {
					++g_pilot_data
						  .faction_statistics
							  [g_pilot_data
								   .current_faction_id]
						  .sp_battles[battle_id]
						  .attempt_count;
				}
				player_result = 0;
				if (overall_winner != 2) {
					if ((overall_winner == 0 &&
					     g_players[g_local_player].team ==
						     0) ||
					    (overall_winner == 1 &&
					     g_players[g_local_player].team ==
						     1)) {
						++g_pilot_data
							  .faction_statistics
								  [g_pilot_data
									   .current_faction_id]
							  .sp_battles[battle_id]
							  .victory_count;
						player_result = 1;
					} else {
						++g_pilot_data
							  .faction_statistics
								  [g_pilot_data
									   .current_faction_id]
							  .sp_battles[battle_id]
							  .defeat_count;
						player_result = 2;
					}
				}
				if (g_pilot_data.battle_sequence_state
					    .current_mission_index == 10) {
					++g_pilot_data
						  .faction_statistics
							  [g_pilot_data
								   .current_faction_id]
						  .sp_battles[battle_id]
						  .draw_count;
					player_result = 0;
				}
				if (g_pilot_data
					    .faction_statistics
						    [g_pilot_data
							     .current_faction_id]
					    .sp_battles[battle_id]
					    .best_score <
				    g_pilot_data.battle_sequence_state
					    .cumulative_score) {
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.sp_battles[battle_id]
						.best_score =
						g_pilot_data
							.battle_sequence_state
							.cumulative_score;
				}
				if (player_result == 1 &&
				    g_pilot_data.faction_statistics
						    [g_pilot_data
							     .current_faction_id]
							    .sp_battles
								    [battle_id]
							    .best_victory_margin <
					    (unsigned int)victory_margin) {
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.sp_battles[battle_id]
						.best_victory_margin =
						victory_margin;
				}
				if (battle_award != 0) {
					unsigned int old_award =
						(unsigned int)g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.sp_battles[battle_id]
							.award_level;
					if (old_award > battle_award ||
					    old_award == 0) {
						if (old_award != 0 &&
						    g_pilot_data
								    .faction_statistics
									    [g_pilot_data
										     .current_faction_id]
								    .battle_medallions
									    [old_award -
									     1] !=
							    0) {
							--g_pilot_data
								  .faction_statistics
									  [g_pilot_data
										   .current_faction_id]
								  .battle_medallions
									  [old_award -
									   1];
						}
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.sp_battles[battle_id]
							.award_level =
							(int)battle_award;
						++g_pilot_data
							  .faction_statistics
								  [g_pilot_data
									   .current_faction_id]
							  .battle_medallions
								  [battle_award -
								   1];
					}
				} else if (
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.sp_battles[battle_id]
						.award_level == FAILED_AWARD) {
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.sp_battles[battle_id]
						.award_level = 0;
					if (g_pilot_data
						    .faction_statistics
							    [g_pilot_data
								     .current_faction_id]
						    .battle_medallions
							    [FAILED_AWARD -
							     1] != 0) {
						--g_pilot_data
							  .faction_statistics
								  [g_pilot_data
									   .current_faction_id]
							  .battle_medallions
								  [FAILED_AWARD -
								   1];
					}
				}
				XVT_LOG_DEBUG(
					"results.battle_record record=\"single\" battle=%d attempts=%d victories=%d defeats=%d draws=%d best=%d best_margin=%u award=%d score=%d",
					battle_id,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.sp_battles[battle_id]
						.attempt_count,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.sp_battles[battle_id]
						.victory_count,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.sp_battles[battle_id]
						.defeat_count,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.sp_battles[battle_id]
						.draw_count,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.sp_battles[battle_id]
						.best_score,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.sp_battles[battle_id]
						.best_victory_margin,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.sp_battles[battle_id]
						.award_level,
					g_pilot_data.battle_sequence_state
						.cumulative_score);
			} else {
				if (g_pilot_data.battle_sequence_state
					    .current_mission_index == 0) {
					++g_pilot_data
						  .faction_statistics
							  [g_pilot_data
								   .current_faction_id]
						  .mp_battles[battle_id]
						  .attempt_count;
				}
				player_result = 0;
				if (overall_winner != 2) {
					if ((overall_winner == 0 &&
					     g_players[g_local_player].team ==
						     0) ||
					    (overall_winner == 1 &&
					     g_players[g_local_player].team ==
						     1)) {
						++g_pilot_data
							  .faction_statistics
								  [g_pilot_data
									   .current_faction_id]
							  .mp_battles[battle_id]
							  .victory_count;
						player_result = 1;
					} else {
						++g_pilot_data
							  .faction_statistics
								  [g_pilot_data
									   .current_faction_id]
							  .mp_battles[battle_id]
							  .defeat_count;
						player_result = 2;
					}
				}
				if (g_pilot_data.battle_sequence_state
					    .current_mission_index == 10) {
					++g_pilot_data
						  .faction_statistics
							  [g_pilot_data
								   .current_faction_id]
						  .mp_battles[battle_id]
						  .draw_count;
					player_result = 0;
				}
				if (g_pilot_data
					    .faction_statistics
						    [g_pilot_data
							     .current_faction_id]
					    .mp_battles[battle_id]
					    .best_score <
				    g_pilot_data.battle_sequence_state
					    .cumulative_score) {
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.mp_battles[battle_id]
						.best_score =
						g_pilot_data
							.battle_sequence_state
							.cumulative_score;
				}
				if (player_result == 1 &&
				    g_pilot_data.faction_statistics
						    [g_pilot_data
							     .current_faction_id]
							    .mp_battles
								    [battle_id]
							    .best_victory_margin <
					    (unsigned int)victory_margin) {
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.mp_battles[battle_id]
						.best_victory_margin =
						victory_margin;
				}
				if (battle_award != 0) {
					unsigned int old_award =
						(unsigned int)g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.mp_battles[battle_id]
							.award_level;
					if (old_award > battle_award ||
					    old_award == 0) {
						if (old_award == FAILED_AWARD &&
						    g_pilot_data
								    .faction_statistics
									    [g_pilot_data
										     .current_faction_id]
								    .battle_medallions
									    [FAILED_AWARD -
									     1] !=
							    0) {
							--g_pilot_data
								  .faction_statistics
									  [g_pilot_data
										   .current_faction_id]
								  .battle_medallions
									  [FAILED_AWARD -
									   1];
						}
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.mp_battles[battle_id]
							.award_level =
							(int)battle_award;
					}
					++g_pilot_data
						  .faction_statistics
							  [g_pilot_data
								   .current_faction_id]
						  .battle_medallions
							  [battle_award - 1];
				} else if (
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.mp_battles[battle_id]
						.award_level == FAILED_AWARD) {
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.mp_battles[battle_id]
						.award_level = 0;
					if (g_pilot_data
						    .faction_statistics
							    [g_pilot_data
								     .current_faction_id]
						    .battle_medallions
							    [FAILED_AWARD -
							     1] != 0) {
						--g_pilot_data
							  .faction_statistics
								  [g_pilot_data
									   .current_faction_id]
							  .battle_medallions
								  [FAILED_AWARD -
								   1];
					}
				}
				XVT_LOG_DEBUG(
					"results.battle_record record=\"multi\" battle=%d attempts=%d victories=%d defeats=%d draws=%d best=%d best_margin=%u award=%d score=%d",
					battle_id,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.mp_battles[battle_id]
						.attempt_count,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.mp_battles[battle_id]
						.victory_count,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.mp_battles[battle_id]
						.defeat_count,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.mp_battles[battle_id]
						.draw_count,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.mp_battles[battle_id]
						.best_score,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.mp_battles[battle_id]
						.best_victory_margin,
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.mp_battles[battle_id]
						.award_level,
					g_pilot_data.battle_sequence_state
						.cumulative_score);
			}
		}
		XVT_LOG_INFO(
			"results.committed type=%d team=%d completed=%d score=%d rating=%d rank_change=%d award=%d humans=%d",
			(int)g_mission_header.mission_type, local_player_team,
			g_flight_mission_state.runtime.team_goal_status
						[local_player_team]
						[TEAM_GOAL_PRIMARY] == 1 &&
				g_flight_mission_state.runtime.team_goal_status
						[local_player_team]
						[TEAM_GOAL_PREVENT] != 1,
			score, (int)g_pilot_data.rating,
			(int)g_pilot_data.promotion_delta, award,
			connected_human_count);
	} else {
		XVT_LOG_INFO("results.not_recorded type=%d",
			     (int)g_mission_header.mission_type);
	}
	XVT_LOG_INFO(
		"battle.mission_ended type=%d sequence=%d humans=%d teams=%u local=%d ended=%d tick=%d",
		(int)g_mission_header.mission_type,
		g_pilot_data.mission_sequence_active, connected_human_count,
		active_team_count, g_local_player,
		(int)g_flight_mission_state.mission_end_pending, g_game_time);
	return 0;
}

/* Reads a whole file into dst in 512-byte blocks and returns the bytes read as
 * a 16-bit count, which wraps for a file of 65,536 bytes or more. A missing
 * file is fatal. Opens and closes g_stream. Does not check the size of dst. */
// FUNCTION: XVT 0x49C3C0
uint16_t fe_disk_io_read_all_bytes_or_fatal(const char *file_name, void *dst)
{
	fe_disk_io_open_global_stream(file_name, "rb", 1, 0);
	xvt_file *stream = g_stream;
	if (stream == NULL) {
		fe_disk_io_fatal_error(FILE_ERROR_STR_FILE_MISSING);
#ifdef XVT_MODERN
		return 0;
#endif
	}
	uint16_t total_bytes = 0;
	uint8_t *output = dst;
	uint16_t bytes_read = 512;
	uint8_t buffer[512];
	while (bytes_read == 512) {
		bytes_read =
			(uint16_t)FILE_RAW_READ(buffer, 1, bytes_read, stream);
		uint16_t byte_index = 0;
		while (byte_index < bytes_read) {
			*output = buffer[byte_index];
			++output;
			++byte_index;
		}
		total_bytes += bytes_read;
	}
	fe_disk_io_close_global_stream(0);
	XVT_LOG_DEBUG("resources.file_read file=\"%s\" bytes=%u", file_name,
		      (unsigned)total_bytes);
	return total_bytes;
}

/* Sets up the flight's fixed buffers. Sets the pool handles and
 * g_flight_font_small_sw to 0 first, then allocates the strings (32,000 bytes),
 * three fonts, three screen-size buffers, HUD panel sprites, icon frames, the
 * message log and a 296-entry render list; a failed allocation is fatal. Loads
 * the game strings and the fonts for the resolution, locks the buffers, fills
 * the scratch buffer with color 0x40, clears the screen and draws the loading
 * line for the mission directory, with a warning when the resolution, pixel
 * depth or hardware 3D asked for is not the one in use. Then loads the flight
 * icons and, with sound effects on, SFXBLAST.LST. */
// FUNCTION: XVT 0x49C460
void fe_disk_io_init_global_buffers(void)
{
	enum {
		STRING_DATA_BUFFER_BYTES = 32000,
		SMALL_FONT_BUFFER_BYTES = 34600,
		MICRO_FONT_BUFFER_BYTES = 20600,
		MEDIUM_FONT_BUFFER_BYTES = 34600,
		HUD_PANEL_SPRITE_BUFFER_BYTES = 120000,
		FLIGHT_ICON_FRAME_POINTER_CAPACITY = 2010,
		FLIGHT_ICON_FRAME_DATA_BYTES = 31060,
		MESSAGE_LOG_BUFFER_BYTES = 32000,
		RENDER_OBJECT_LIST_CAPACITY = 296,
		SCRATCH_SCREEN_CLEAR_COLOR = 0x40,
		FLIGHT_TEXT_LOADING_COLOR = 0xfa,
		FLIGHT_TEXT_WARNING_COLOR = 0x36,
		BASE_FLIGHT_SFX_FIRST_SOUND_ID = 4,
	};

	int16_t allocation_failed = 0;
	g_object_table_handle = 0;
	g_mobile_object_pool_handle = 0;
	g_flight_font_small_sw = NULL;
	g_mobile_object_char_data_handle = 0;
	g_craft_data_pool_handle = 0;
	g_warhead_guidance_pool_handle = 0;
	g_string_data_handle = memory_alloc_handle(STRING_DATA_BUFFER_BYTES, 0);
	if (g_string_data_handle == 0) {
		XVT_LOG_DEBUG("resources.strings_alloc_failed bytes=%d",
			      (int)STRING_DATA_BUFFER_BYTES);
		fe_disk_io_fatal_error(FILE_ERROR_STR_NOT_ENOUGH_MEMORY);
#ifdef XVT_MODERN
		return;
#endif
	}

	g_flight_small_font_handle =
		memory_alloc_handle(SMALL_FONT_BUFFER_BYTES, 0);
	if (g_flight_small_font_handle == 0) {
		allocation_failed = 1;
	}
	g_flight_micro_font_handle =
		memory_alloc_handle(MICRO_FONT_BUFFER_BYTES, 0);
	if (g_flight_micro_font_handle == 0) {
		allocation_failed = 1;
	}
	g_flight_medium_font_handle =
		memory_alloc_handle(MEDIUM_FONT_BUFFER_BYTES, 0);
	if (g_flight_medium_font_handle == 0) {
		allocation_failed = 1;
	}
	if (allocation_failed != 0) {
		XVT_LOG_DEBUG(
			"resources.fonts_alloc_failed small=%u micro=%u medium=%u",
			(unsigned)g_flight_small_font_handle,
			(unsigned)g_flight_micro_font_handle,
			(unsigned)g_flight_medium_font_handle);
		fe_disk_io_fatal_error(FILE_ERROR_STR_NOT_ENOUGH_MEMORY);
#ifdef XVT_MODERN
		return;
#endif
	}

	g_flight_scratch_screen_buffer_handle = memory_alloc_handle(
		g_screen_height * (unsigned int)g_flight_bytes_per_pixel *
			g_screen_width,
		0);
	if (g_flight_scratch_screen_buffer_handle == 0) {
		allocation_failed = 1;
	}
	g_flight_aux_buffer_handle = memory_alloc_handle(
		g_screen_height * (unsigned int)g_flight_bytes_per_pixel *
			g_screen_width,
		0);
	if (g_flight_aux_buffer_handle == 0) {
		allocation_failed = 1;
	}
	g_flight_offscreen_buffer_handle = memory_alloc_handle(
		g_screen_height * (unsigned int)g_flight_bytes_per_pixel *
			g_screen_width,
		0);
	if (g_flight_offscreen_buffer_handle == 0) {
		allocation_failed = 1;
	}
	g_hud_panel_sprite_data_handle =
		memory_alloc_handle(HUD_PANEL_SPRITE_BUFFER_BYTES, 0);
	if (g_hud_panel_sprite_data_handle == 0) {
		allocation_failed = 1;
	}
	g_flight_icon_frames_handle = memory_alloc_handle(
		FLIGHT_ICON_FRAME_POINTER_CAPACITY *
				sizeof(g_flight_icon_frames[0]) +
			FLIGHT_ICON_FRAME_DATA_BYTES,
		0);
	if (g_flight_icon_frames_handle == 0) {
		allocation_failed = 1;
	}
	g_message_log_handle = memory_alloc_handle(MESSAGE_LOG_BUFFER_BYTES, 0);
	if (g_message_log_handle == 0) {
		allocation_failed = 1;
	}
	g_render_object_list_handle = memory_alloc_handle(
		RENDER_OBJECT_LIST_CAPACITY *
			sizeof(g_render_object_list_entries[0]),
		0);
	if (g_render_object_list_handle == 0) {
		allocation_failed = 1;
	}

	string_table_load_game_strings(1);
	g_flight_font_small_sw =
		memory_get_handle_block(g_flight_small_font_handle);
	g_flight_font_micro_sw =
		memory_get_handle_block(g_flight_micro_font_handle);
	g_flight_font_medium_sw =
		memory_get_handle_block(g_flight_medium_font_handle);
	switch (g_flight_resolution_mode) {
	case FLIGHT_RESOLUTION_320X240:
		fe_disk_io_read_all_bytes_or_fatal("MICRO32.FNT",
						   g_flight_font_micro_sw);
		fe_disk_io_read_all_bytes_or_fatal("MICRO48.FNT",
						   g_flight_font_small_sw);
		g_flight_icon_resource_path =
			g_flight_map_icons320x240_resource_path;
		break;
	case FLIGHT_RESOLUTION_640X480:
		fe_disk_io_read_all_bytes_or_fatal("MICRO32.FNT",
						   g_flight_font_micro_sw);
		fe_disk_io_read_all_bytes_or_fatal("MICRO48.FNT",
						   g_flight_font_small_sw);
		fe_disk_io_read_all_bytes_or_fatal("MICRO64.FNT",
						   g_flight_font_medium_sw);
		g_flight_icon_resource_path =
			g_flight_icons640x480_resource_path;
		break;
	case FLIGHT_RESOLUTION_480X360:
		fe_disk_io_read_all_bytes_or_fatal("MICRO32.FNT",
						   g_flight_font_micro_sw);
		fe_disk_io_read_all_bytes_or_fatal("MICRO48.FNT",
						   g_flight_font_small_sw);
		fe_disk_io_read_all_bytes_or_fatal("MICRO64.FNT",
						   g_flight_font_medium_sw);
		g_flight_icon_resource_path =
			g_flight_map_icons480x360_resource_path;
		break;
	}
#ifdef XVT_MODERN
	xvt_render_assets_register_flight_fonts();
#endif
	flight_text_set_font_tier(1);
	if (allocation_failed != 0) {
		XVT_LOG_DEBUG(
			"resources.buffers_alloc_failed screen_bytes=%u scratch=%u aux=%u offscreen=%u panels=%u icons=%u messages=%u render_list=%u",
			g_screen_height *
				(unsigned int)g_flight_bytes_per_pixel *
				g_screen_width,
			(unsigned)g_flight_scratch_screen_buffer_handle,
			(unsigned)g_flight_aux_buffer_handle,
			(unsigned)g_flight_offscreen_buffer_handle,
			(unsigned)g_hud_panel_sprite_data_handle,
			(unsigned)g_flight_icon_frames_handle,
			(unsigned)g_message_log_handle,
			(unsigned)g_render_object_list_handle);
		fe_disk_io_fatal_error(FILE_ERROR_STR_NOT_ENOUGH_MEMORY);
#ifdef XVT_MODERN
		return;
#endif
	}

	g_render_object_list_entries =
		memory_get_handle_block(g_render_object_list_handle);
	g_flight_scratch_screen_buffer =
		memory_get_handle_block(g_flight_scratch_screen_buffer_handle);
	memset(g_flight_scratch_screen_buffer, SCRATCH_SCREEN_CLEAR_COLOR,
	       g_screen_height * (unsigned int)g_flight_bytes_per_pixel *
		       g_screen_width);
	g_flight_offscreen_buffer =
		memory_get_handle_block(g_flight_offscreen_buffer_handle);
	flight_sw_set_rotated_sprite_dest_buffer(
		g_flight_scratch_screen_buffer);
	g_flight_aux_buffer =
		memory_get_handle_block(g_flight_aux_buffer_handle);
	g_flight_aux_buffer_mirror = g_flight_aux_buffer;

	flight_text_set_clip_rect(0, 0, (int16_t)g_screen_width,
				  (int16_t)g_screen_height);
	g_flight_text_bg_color = 0;
	g_flight_fill_clip_rect_fn();
	flight_text_set_clip_rect(0, 0, (int16_t)g_screen_width,
				  (int16_t)g_screen_height);
	g_flight_text_bg_color = 0;
	g_flight_text_color_index = FLIGHT_TEXT_LOADING_COLOR;
	g_flight_text_shadow_color = 0;
	g_flight_text_shadow_enabled = 0;
	flight_text_set_cursor(0, (g_screen_height >> 1) -
					  3 * g_flight_font_line_height);

	const char *loading_message;
	switch (g_pilot_data.mission_directory_id) {
	case MISSION_DIRECTORY_TRAINING_EXERCISES:
		if (g_pilot_data.mission_sequence_active == 1) {
			loading_message = g_str_disk_io_messages
				[DISK_IO_STR_ENTERING_CAMPAIGN];
		} else {
			loading_message = g_str_disk_io_messages
				[DISK_IO_STR_ENTERING_TRAINING];
		}
		break;
	case MISSION_DIRECTORY_MELEES:
	case MISSION_DIRECTORY_TOURNAMENTS:
		loading_message =
			g_str_disk_io_messages[DISK_IO_STR_ENTERING_MELEE];
		break;
	case MISSION_DIRECTORY_CAMPAIGNS:
		loading_message =
			g_str_disk_io_messages[DISK_IO_STR_ENTERING_CAMPAIGN];
		break;
	default:
		loading_message =
			g_str_disk_io_messages[DISK_IO_STR_ENTERING_COMBAT];
		break;
	}
#ifdef XVT_MODERN
	xvt_cockpit_messages_begin_loading_text();
#endif
	flight_text_draw_string_centered(loading_message);

	int requested_render_target_width = g_render_target_width;
	if (requested_render_target_width != g_display_mode_width) {
		XVT_LOG_WARN(
			"resources.display_fallback what=\"width\" asked=%d used=%d",
			requested_render_target_width, g_display_mode_width);
		g_flight_text_color_index = FLIGHT_TEXT_WARNING_COLOR;
		flight_text_set_cursor(0, (g_screen_height >>
					   1) + 3 * g_flight_font_line_height);
		const char *unsupported_resolution_message;
		switch (g_render_target_width) {
		case 320:
			unsupported_resolution_message = g_str_disk_io_messages
				[DISK_IO_STR_RES_320_NOT_SUPPORTED];
			break;
		case 512:
			unsupported_resolution_message = g_str_disk_io_messages
				[DISK_IO_STR_RES_512_NOT_SUPPORTED];
			break;
		case 640:
			unsupported_resolution_message = g_str_disk_io_messages
				[DISK_IO_STR_RES_640_NOT_SUPPORTED];
			break;
		default:
			unsupported_resolution_message = g_str_disk_io_messages
				[DISK_IO_STR_RES_NOT_SUPPORTED];
			break;
		}
		flight_text_draw_string_centered(
			unsupported_resolution_message);
		flight_text_set_cursor(
			0, (g_screen_height >> 1) +
				   4 * g_flight_font_line_height + 1);
		const char *fallback_resolution_message;
		switch (g_display_mode_width) {
		case 320:
			fallback_resolution_message = g_str_disk_io_messages
				[DISK_IO_STR_RES_320_USED_INSTEAD];
			break;
		case 512:
			fallback_resolution_message = g_str_disk_io_messages
				[DISK_IO_STR_RES_512_USED_INSTEAD];
			break;
		case 640:
			fallback_resolution_message = g_str_disk_io_messages
				[DISK_IO_STR_RES_640_USED_INSTEAD];
			break;
		default:
			fallback_resolution_message = g_str_disk_io_messages
				[DISK_IO_STR_NEXT_RES_USED_INSTEAD];
			break;
		}
		flight_text_draw_string_centered(fallback_resolution_message);
	}

	int requested_bytes_per_pixel = g_requested_flight_bytes_per_pixel;
	int active_bytes_per_pixel = g_flight_bytes_per_pixel;
	if (requested_bytes_per_pixel != active_bytes_per_pixel) {
		XVT_LOG_WARN(
			"resources.display_fallback what=\"color_depth\" asked=%d used=%d",
			requested_bytes_per_pixel, active_bytes_per_pixel);
		g_flight_text_color_index = FLIGHT_TEXT_WARNING_COLOR;
		flight_text_set_cursor(0, (g_screen_height >>
					   1) + 6 * g_flight_font_line_height);
		const char *pixel_format_message;
		if (g_flight_bytes_per_pixel == 2) {
			pixel_format_message =
				g_str_disk_io_messages[DISK_IO_STR_USING_16BPP];
		} else {
			pixel_format_message =
				g_str_disk_io_messages[DISK_IO_STR_USING_8BPP];
		}
		flight_text_draw_string_centered(pixel_format_message);
	}
	int requested_hardware3d = g_requested_flight_hardware3d;
	int active_hardware3d = g_use_hardware3d;
	if (requested_hardware3d != active_hardware3d) {
		XVT_LOG_WARN(
			"resources.display_fallback what=\"hardware_3d\" asked=%d used=%d",
			requested_hardware3d, active_hardware3d);
		g_flight_text_color_index = FLIGHT_TEXT_WARNING_COLOR;
		flight_text_set_cursor(0, (g_screen_height >>
					   1) + 8 * g_flight_font_line_height);
		flight_text_draw_string_centered(
			g_str_disk_io_messages
				[DISK_IO_STR_HARDWARE_3D_NOT_SUPPORTED]);
	}
#ifdef XVT_MODERN
	xvt_cockpit_messages_end_loading_text();
#endif

	g_hud_cockpit_resources_loaded = 0;
	g_flight_icon_frames =
		memory_get_handle_block(g_flight_icon_frames_handle);
	g_flight_icon_frame_count = flight_icon_load_frames(
		(char *)g_flight_icon_resource_path,
		(uint8_t *)g_flight_icon_frames +
			FLIGHT_ICON_FRAME_POINTER_CAPACITY *
				sizeof(g_flight_icon_frames[0]),
		g_flight_icon_frames);
	if (g_flight_conf_sfx_enabled != 0) {
		flight_surface_unlock();
		fsfx_reset_flight_sfx_state();
		char sound_list_path[40];
		strcpy(sound_list_path, "wave\\");
		strcat(sound_list_path, "SFXBLAST.LST");
		fsfx_load_sfx_list(sound_list_path,
				   BASE_FLIGHT_SFX_FIRST_SOUND_ID);
		flight_surface_lock();
	}
	XVT_LOG_DEBUG(
		"resources.buffers_ready width=%u height=%u bpp=%d mode=%d directory=%d icons=%d sfx=%d",
		g_screen_width, g_screen_height, g_flight_bytes_per_pixel,
		g_flight_resolution_mode,
		(int)g_pilot_data.mission_directory_id,
		g_flight_icon_frame_count, (int)g_flight_conf_sfx_enabled);
}

/* Unlocks the set pool handles, then the strings, render list, font and screen
 * buffer handles. When g_mobile_object_char_data_handle is set it unlocks
 * g_craft_data_pool_handle in its place, so the character data stays locked and
 * the craft pool is unlocked twice. */
// FUNCTION: XVT 0x49CBA0
void fe_disk_io_unlock_global_buffers(void)
{
	if (g_object_table_handle != 0) {
		memory_handle_block_done_stub(g_object_table_handle);
	}
	if (g_mobile_object_pool_handle != 0) {
		memory_handle_block_done_stub(g_mobile_object_pool_handle);
	}
	uint16_t craft_data_pool_handle = g_craft_data_pool_handle;
	if (g_mobile_object_char_data_handle != 0) {
		memory_handle_block_done_stub(g_craft_data_pool_handle);
		craft_data_pool_handle = g_craft_data_pool_handle;
	}
	if (craft_data_pool_handle != 0) {
		memory_handle_block_done_stub(craft_data_pool_handle);
	}
	if (g_warhead_guidance_pool_handle != 0) {
		memory_handle_block_done_stub(g_warhead_guidance_pool_handle);
	}
	memory_handle_block_done_stub(g_string_data_handle);
	memory_handle_block_done_stub(g_render_object_list_handle);
	memory_handle_block_done_stub(g_flight_small_font_handle);
	memory_handle_block_done_stub(g_flight_micro_font_handle);
	memory_handle_block_done_stub(g_flight_medium_font_handle);
	memory_handle_block_done_stub(g_flight_scratch_screen_buffer_handle);
	memory_handle_block_done_stub(g_flight_aux_buffer_handle);
	memory_handle_block_done_stub(g_flight_offscreen_buffer_handle);
}

/* Locks every set pool handle and points its base at the memory:
 * g_mobile_object_char_data_pool, g_craft_data_pool_base, g_projectile_guidance_states,
 * g_mobile_object_pool_base and g_object_table, then rebuilds the mobile object
 * links. Locks the render list, the fonts (choosing the glyph table for
 * g_flight_font_tier) and the three screen-size buffers, and sets
 * g_flight_aux_buffer_mirror. Its string_table_load_game_strings call, with 0, loads
 * nothing. */
// FUNCTION: XVT 0x49CC90
void fe_disk_io_lock_global_buffers(void)
{
	if (g_mobile_object_char_data_handle != 0) {
		g_mobile_object_char_data_pool = memory_get_handle_block(
			g_mobile_object_char_data_handle);
	}
	if (g_craft_data_pool_handle != 0) {
		g_craft_data_pool_base =
			memory_get_handle_block(g_craft_data_pool_handle);
	}
	if (g_warhead_guidance_pool_handle != 0) {
		g_projectile_guidance_states =
			memory_get_handle_block(g_warhead_guidance_pool_handle);
	}
	if (g_mobile_object_pool_handle != 0) {
		g_mobile_object_pool_base =
			memory_get_handle_block(g_mobile_object_pool_handle);
	}
	if (g_object_table_handle != 0) {
		g_object_table = memory_get_handle_block(g_object_table_handle);
		object_relink_mobile_object_pointers();
	}

	string_table_load_game_strings(0);
	g_render_object_list_entries =
		memory_get_handle_block(g_render_object_list_handle);
	g_flight_font_small_sw =
		memory_get_handle_block(g_flight_small_font_handle);
	g_flight_font_micro_sw =
		memory_get_handle_block(g_flight_micro_font_handle);
	g_flight_font_medium_sw =
		memory_get_handle_block(g_flight_medium_font_handle);
	if (g_flight_font_tier == 1) {
		g_flight_font_glyph_table_sw = g_flight_font_small_sw;
	} else if (g_flight_font_tier == 2) {
		g_flight_font_glyph_table_sw = g_flight_font_micro_sw;
	} else if (g_flight_font_tier == 0) {
		g_flight_font_glyph_table_sw = g_flight_font_medium_sw;
	}

	g_flight_scratch_screen_buffer =
		memory_get_handle_block(g_flight_scratch_screen_buffer_handle);
	g_flight_offscreen_buffer =
		memory_get_handle_block(g_flight_offscreen_buffer_handle);
	flight_sw_set_rotated_sprite_dest_buffer(
		g_flight_scratch_screen_buffer);
	g_flight_aux_buffer =
		memory_get_handle_block(g_flight_aux_buffer_handle);
	g_flight_aux_buffer_mirror = g_flight_aux_buffer;
}

/* Frees the flight's memory at its end: unlocks the buffers; frees the pool
 * handles and sets them to 0; unloads the sound effects; frees the fixed
 * buffers (the modern build skips handles of 0 and sets them to 0 after), the
 * 28 cockpit resources and each object type's model or texture once; clears
 * g_loaded_models and every type's resource_handle; and frees the render scene
 * buffers and the mission's override strings. The modern build first clears its
 * mission render assets. */
// FUNCTION: XVT 0x49CDF0
void fe_disk_io_free_flight_resources(void)
{
	XVT_LOG_DEBUG(
		"resources.flight_release objects=%u mobiles=%u chars=%u crafts=%u warheads=%u strings=%u",
		(unsigned)g_object_table_handle,
		(unsigned)g_mobile_object_pool_handle,
		(unsigned)g_mobile_object_char_data_handle,
		(unsigned)g_craft_data_pool_handle,
		(unsigned)g_warhead_guidance_pool_handle,
		(unsigned)g_string_data_handle);
#ifdef XVT_MODERN
	xvt_render_assets_clear_mission();
#endif
	fe_disk_io_unlock_global_buffers();
	if (g_object_table_handle != 0) {
		memory_free_handle(g_object_table_handle);
		g_object_table_handle = 0;
	}
	if (g_mobile_object_pool_handle != 0) {
		memory_free_handle(g_mobile_object_pool_handle);
		g_mobile_object_pool_handle = 0;
	}
	if (g_mobile_object_char_data_handle != 0) {
		memory_free_handle(g_mobile_object_char_data_handle);
		g_mobile_object_char_data_handle = 0;
	}
	if (g_craft_data_pool_handle != 0) {
		memory_free_handle(g_craft_data_pool_handle);
		g_craft_data_pool_handle = 0;
	}
	if (g_warhead_guidance_pool_handle != 0) {
		memory_free_handle(g_warhead_guidance_pool_handle);
		g_warhead_guidance_pool_handle = 0;
	}

	fsfx_unload_all_effects_thunk();
#ifdef XVT_MODERN
	if (g_string_data_handle) {
		memory_free_handle(g_string_data_handle);
	}
#else
	memory_free_handle(g_string_data_handle);
#endif
#ifdef XVT_MODERN
	if (g_render_object_list_handle) {
		memory_free_handle(g_render_object_list_handle);
	}
#else
	memory_free_handle(g_render_object_list_handle);
#endif
#ifdef XVT_MODERN
	if (g_flight_small_font_handle) {
		memory_free_handle(g_flight_small_font_handle);
	}
#else
	memory_free_handle(g_flight_small_font_handle);
#endif
#ifdef XVT_MODERN
	if (g_flight_micro_font_handle) {
		memory_free_handle(g_flight_micro_font_handle);
	}
#else
	memory_free_handle(g_flight_micro_font_handle);
#endif
#ifdef XVT_MODERN
	if (g_flight_medium_font_handle) {
		memory_free_handle(g_flight_medium_font_handle);
	}
#else
	memory_free_handle(g_flight_medium_font_handle);
#endif
#ifdef XVT_MODERN
	if (g_flight_scratch_screen_buffer_handle) {
		memory_free_handle(g_flight_scratch_screen_buffer_handle);
	}
#else
	memory_free_handle(g_flight_scratch_screen_buffer_handle);
#endif
#ifdef XVT_MODERN
	if (g_flight_aux_buffer_handle) {
		memory_free_handle(g_flight_aux_buffer_handle);
	}
#else
	memory_free_handle(g_flight_aux_buffer_handle);
#endif
#ifdef XVT_MODERN
	if (g_flight_offscreen_buffer_handle) {
		memory_free_handle(g_flight_offscreen_buffer_handle);
	}
#else
	memory_free_handle(g_flight_offscreen_buffer_handle);
#endif
#ifdef XVT_MODERN
	if (g_hud_panel_sprite_data_handle) {
		memory_free_handle(g_hud_panel_sprite_data_handle);
	}
#else
	memory_free_handle(g_hud_panel_sprite_data_handle);
#endif
#ifdef XVT_MODERN
	if (g_flight_icon_frames_handle) {
		memory_free_handle(g_flight_icon_frames_handle);
	}
#else
	memory_free_handle(g_flight_icon_frames_handle);
#endif
#ifdef XVT_MODERN
	if (g_message_log_handle) {
		memory_free_handle(g_message_log_handle);
	}
#else
	memory_free_handle(g_message_log_handle);
#endif
#ifdef XVT_MODERN
	g_string_data_handle = 0;
	g_render_object_list_handle = 0;
	g_flight_small_font_handle = 0;
	g_flight_micro_font_handle = 0;
	g_flight_medium_font_handle = 0;
	g_flight_scratch_screen_buffer_handle = 0;
	g_flight_aux_buffer_handle = 0;
	g_flight_offscreen_buffer_handle = 0;
	g_hud_panel_sprite_data_handle = 0;
	g_flight_icon_frames_handle = 0;
	g_message_log_handle = 0;
#endif

	for (int cockpit_resource_index = 0; cockpit_resource_index < 28;
	     ++cockpit_resource_index) {
		if (g_hud_cockpit_resources[cockpit_resource_index]
			    .memory_handle != 0) {
			memory_free_handle((uint16_t)g_hud_cockpit_resources
						   [cockpit_resource_index]
							   .memory_handle);
			g_hud_cockpit_resources[cockpit_resource_index]
				.memory_handle = 0;
		}
	}

	int model_type;
	int previous_model_type;
	for (model_type = 0; model_type < 201; ++model_type) {
		uint16_t texture_handle =
			g_object_type_table[model_type].resource_handle;
		if (texture_handle != 0) {
			for (previous_model_type = 0;
			     previous_model_type < model_type;
			     ++previous_model_type) {
				if (g_object_type_table[previous_model_type]
					    .resource_handle ==
				    texture_handle) {
					break;
				}
			}
			if (previous_model_type >= model_type) {
				memory_free_handle(texture_handle);
			}
		}
	}

	memset(g_loaded_models, 0, sizeof(g_loaded_models));
	for (model_type = 0; model_type < 201; ++model_type) {
		g_object_type_table[model_type].resource_handle = 0;
	}
	render_scene_free_buffers();
	mission_free_override_string_handles();
}

/* Loads the models and textures the mission's object types need. Reads the
 * lists SPEC, SPEC2 and SPEC3 under ivfiles, the 640 version at 640x480 and
 * 480x360, else the 320 one. Each nonblank line names the resource for the
 * object types whose texture_group is that list and resource_index that line,
 * counted from 0; it is loaded when such a type has asset flags 0x18 and is not
 * proving-grounds-only (0x40) outside the proving grounds. An OPT model (flag
 * 0x01) goes through opt_model_load_handle and fe_disk_io_build_model_def; a texture
 * level (0x02) is read whole, its palettes converted to the flight pixel depth.
 * Each type gets the handle in resource_handle and g_loaded_models. Runs with the
 * global buffers unlocked, relocking them after render_scene_allocate_buffers.
 * Does not check that a needed resource has one of the two load flags. */
// FUNCTION: XVT 0x49D010
void fe_disk_io_load_resources(void)
{
	enum {
		MODEL_RECORD_HAS_RESOURCE = 0x02,
		MODEL_ASSET_OPT = 0x01,
		MODEL_ASSET_TEX_LEVEL = 0x02,
		MODEL_ASSET_ACTIVE_MASK = 0x18,
		MODEL_ASSET_PROVING_GROUNDS_ONLY = 0x40,
	};

	uint16_t model_type;

	for (model_type = 0;
	     model_type < sizeof(g_loaded_models) / sizeof(g_loaded_models[0]);
	     ++model_type) {
		g_loaded_models[model_type] = 0;
		g_object_type_table[model_type].resource_handle = 0;
	}
	fe_disk_io_unlock_global_buffers();
	g_scene_edge_flags_capacity = 0;
	g_vertex_remap_capacity = 0;

	uint16_t resource_handle;
	int line_end_index;
	unsigned int resource_data_size;
	unsigned int palette_entry_count;
	char list_path[60];
	char resource_name[256];
	for (uint16_t spec_list_index = 0;
	     spec_list_index <
	     sizeof(g_spec_list_prefixes) / sizeof(g_spec_list_prefixes[0]);
	     ++spec_list_index) {
		strcpy(list_path, "ivfiles\\");
		int spec_list_group = spec_list_index;
		strcat(list_path, g_spec_list_prefixes[spec_list_index]);
		if (g_flight_resolution_mode == FLIGHT_RESOLUTION_640X480) {
			strcat(list_path, "640");
		} else if (g_flight_resolution_mode ==
			   FLIGHT_RESOLUTION_480X360) {
			strcat(list_path, "640");
		} else {
			strcat(list_path, "320");
		}
		strcat(list_path, ".LST");
		fe_disk_io_open_global_stream(list_path, "rb", 1, 0);
		xvt_file *list_stream = (xvt_file *)g_stream;
		uint16_t list_entry_index = 0;

		while (FILE_GETS(resource_name, sizeof(resource_name),
				 list_stream) != NULL) {
#ifdef XVT_MODERN
			for (line_end_index = 0;
			     resource_name[line_end_index] != '\0' &&
			     resource_name[line_end_index] != '\n';
			     ++line_end_index) {
#else
			for (line_end_index = 0;
			     resource_name[line_end_index] != '\n';
			     ++line_end_index) {
#endif
				if (resource_name[line_end_index] == '\r') {
					break;
				}
			}
			resource_name[line_end_index] = '\0';
			if (resource_name[0] == '\0') {
				continue;
			}

			++list_entry_index;
			uint8_t resource_needed = 0;
			uint8_t resource_asset_flags = 0;
			for (model_type = 0;
			     model_type <
			     sizeof(g_object_type_table) /
				     sizeof(g_object_type_table[0]);
			     ++model_type) {
				if ((g_object_type_table[model_type]
					     .record_flags &
				     MODEL_RECORD_HAS_RESOURCE) != 0 &&
				    g_object_type_table[model_type]
						    .texture_group ==
					    spec_list_group &&
				    g_object_type_table[model_type]
						    .resource_index ==
					    list_entry_index - 1) {
					uint8_t asset_flags =
						g_object_type_table[model_type]
							.asset_flags;

					if ((asset_flags &
					     MODEL_ASSET_ACTIVE_MASK) != 0 &&
					    ((asset_flags &
					      MODEL_ASSET_PROVING_GROUNDS_ONLY) ==
						     0 ||
					     g_flight_mission_state
							     .proving_grounds_mode_active !=
						     0)) {
						resource_needed = 1;
						resource_asset_flags =
							asset_flags;
					}
				}
			}
			if (resource_needed == 0) {
				continue;
			}

			if ((resource_asset_flags & MODEL_ASSET_OPT) != 0) {
				resource_handle =
					opt_model_load_handle(resource_name);
				if (resource_handle == 0) {
					XVT_LOG_WARN(
						"resources.model_missing file=\"%s\" list=%u index=%u",
						resource_name,
						(unsigned)spec_list_index,
						(unsigned)(list_entry_index -
							   1));
				} else {
					XVT_LOG_DEBUG(
						"resources.model_loaded file=\"%s\" list=%u index=%u handle=%u flags=%u",
						resource_name,
						(unsigned)spec_list_index,
						(unsigned)(list_entry_index -
							   1),
						(unsigned)resource_handle,
						(unsigned)resource_asset_flags);
				}
				memory_get_handle_block(resource_handle);
			} else if ((resource_asset_flags &
				    MODEL_ASSET_TEX_LEVEL) != 0) {
				fe_disk_io_open_global_stream(resource_name,
							      "rb", 1, 0);
				xvt_file *texture_stream = (xvt_file *)g_stream;
				fe_disk_io_read_with_retry_prompt(
					&resource_data_size,
					sizeof(resource_data_size), 1,
					texture_stream);
				fe_disk_io_read_with_retry_prompt(
					&palette_entry_count,
					sizeof(palette_entry_count), 1,
					texture_stream);
				resource_handle = memory_alloc_handle(
					resource_data_size +
						g_flight_bytes_per_pixel *
							palette_entry_count,
					0);
				if (resource_handle == 0) {
					fe_disk_io_fatal_error(
						FILE_ERROR_STR_NOT_ENOUGH_MEMORY);
#ifdef XVT_MODERN
					file_close(texture_stream);
					file_close(list_stream);
					g_stream = NULL;
					return;
#endif
				}
				unsigned int *texture_data =
					(unsigned int *)memory_get_handle_block(
						resource_handle);
				texture_data[0] = resource_data_size;
				texture_data[1] = palette_entry_count;
				fe_disk_io_read_with_retry_prompt(
					texture_data + 2,
					resource_data_size -
						2 * sizeof(texture_data[0]),
					1, texture_stream);
				g_stream = texture_stream;
				fe_disk_io_close_global_stream(0);
#ifdef XVT_MODERN
				xvt_render_assets_register_texture(
					resource_handle, resource_name);
#endif
				if (g_flight_bytes_per_pixel == 2) {
					tex_level_convert24_bpp_palettes_to16_bpp(
						texture_data);
				} else {
					tex_level_convert24_bpp_palettes_to8_bpp(
						texture_data);
				}
				XVT_LOG_DEBUG(
					"resources.texture_loaded file=\"%s\" list=%u index=%u handle=%u bytes=%u colors=%u",
					resource_name,
					(unsigned)spec_list_index,
					(unsigned)(list_entry_index - 1),
					(unsigned)resource_handle,
					resource_data_size,
					palette_entry_count);
			} else {
				XVT_LOG_WARN(
					"resources.resource_kind_unknown file=\"%s\" list=%u index=%u flags=%u",
					resource_name,
					(unsigned)spec_list_index,
					(unsigned)(list_entry_index - 1),
					(unsigned)resource_asset_flags);
			}

			for (model_type = 0;
			     model_type <
			     sizeof(g_object_type_table) /
				     sizeof(g_object_type_table[0]);
			     ++model_type) {
				if ((g_object_type_table[model_type]
					     .record_flags &
				     MODEL_RECORD_HAS_RESOURCE) != 0 &&
				    g_object_type_table[model_type]
						    .texture_group ==
					    spec_list_group &&
				    g_object_type_table[model_type]
						    .resource_index ==
					    list_entry_index - 1) {
					uint8_t asset_flags =
						g_object_type_table[model_type]
							.asset_flags;

					if ((asset_flags &
					     MODEL_ASSET_ACTIVE_MASK) != 0 &&
					    ((asset_flags &
					      MODEL_ASSET_PROVING_GROUNDS_ONLY) ==
						     0 ||
					     g_flight_mission_state
							     .proving_grounds_mode_active !=
						     0)) {
						g_object_type_table[model_type]
							.resource_handle =
							resource_handle;
						g_loaded_models[model_type] =
							resource_handle;
						XVT_LOG_DEBUG(
							"resources.type_bound type=%u handle=%u model=%u",
							(unsigned)model_type,
							(unsigned)
								resource_handle,
							(unsigned)g_object_type_table
								[model_type]
									.model_index);
#ifdef XVT_MODERN
						xvt_render_assets_bind_type(
							model_type,
							resource_handle);
#endif
						if ((resource_asset_flags &
						     MODEL_ASSET_OPT) != 0) {
							fe_disk_io_build_model_def(
								g_object_type_table
									[model_type]
										.model_index,
								model_type);
						}
					}
				}
			}
			memory_handle_block_done_stub(resource_handle);
		}

		g_stream = list_stream;
		fe_disk_io_close_global_stream(0);
		XVT_LOG_DEBUG("resources.list_read list=\"%s\" entries=%u",
			      list_path, (unsigned)list_entry_index);
	}

	render_scene_allocate_buffers();
	XVT_LOG_DEBUG("resources.scene_buffers edges=%d vertices=%d",
		      g_scene_edge_flags_capacity, g_vertex_remap_capacity);
	fe_disk_io_lock_global_buffers();
}

/* Loads the flight's palette data and resources, and returns the palette index
 * nearest the color (0, 0, 2), also stored in g_flight_transparent_color_index and
 * g_flight_background_color_index. In 8-bit color it loads the RGB565 lookup table
 * from the mission file's .inv twin, else newpal.inv, else builds it and writes
 * the .inv. Then loads the resources with g_loading_model set and builds the
 * mesh cache. Because g_palette_generation_enabled is always 0, it clears
 * g_generate_mission_palette, and its palette-generating branches, which would
 * write .pal and .inv files and load newpal.act, never run. */
// FUNCTION: XVT 0x49D440
unsigned int fe_disk_io_init_resources(void)
{
	enum {
		MISSION_EXTENSION_LENGTH = 3,
		FALLBACK_FILE_NAME_CAPACITY = 256,
		GENERATED_PALETTE_START = 64,
		GENERATED_PALETTE_COLOR_COUNT = 192,
		QUANTIZER_TREE_DEPTH = 8,
		PALETTE_COLOR_COUNT = 256,
		PALETTE_BYTE_COUNT = sizeof(g_sw_palette),
	};

	g_generate_mission_palette &= g_palette_generation_enabled;
	if (g_flight_bytes_per_pixel == 1) {
		g_active_rgb565_to_palette_index_lut =
			g_rgb565_to_palette_index_lut;
		unsigned int extension_offset = strlen(g_current_mission_file);
		uint8_t saved_extension0 =
			g_current_mission_file[extension_offset -
					       MISSION_EXTENSION_LENGTH];
		uint8_t saved_extension1 =
			g_current_mission_file[extension_offset -
					       MISSION_EXTENSION_LENGTH + 1];
		uint8_t saved_extension2 =
			g_current_mission_file[extension_offset - 1];
		extension_offset -= MISSION_EXTENSION_LENGTH;
		g_current_mission_file[extension_offset] = 'i';
		g_current_mission_file[extension_offset + 1] = 'n';
		g_current_mission_file[extension_offset + 2] = 'v';

		if (fe_disk_io_open_global_stream(g_current_mission_file, "rb",
						  0, 0) == 0) {
			char fallback_file_name[FALLBACK_FILE_NAME_CAPACITY];
			strcpy(fallback_file_name, "newpal.inv");
			if (fe_disk_io_open_global_stream(fallback_file_name,
							  "rb", 0, 0) == 0) {
				color_build_rgb565_to_palette_index_lut(
					g_active_rgb565_to_palette_index_lut,
					GENERATED_PALETTE_START,
					PALETTE_COLOR_COUNT);
				XVT_LOG_DEBUG(
					"resources.color_lookup_built file=\"%s\"",
					g_current_mission_file);
				if (fe_disk_io_open_global_stream(
					    g_current_mission_file, "wb", 0,
					    1) != 0) {
					/* Writes the whole 65,536-byte RGB565
					 * lookup table as 256 x 256 bytes. */
					FILE_RAW_WRITE(
						g_active_rgb565_to_palette_index_lut,
						PALETTE_COLOR_COUNT,
						PALETTE_COLOR_COUNT, g_stream);
					fe_disk_io_close_global_stream(1);
				} else {
					XVT_LOG_WARN(
						"resources.color_lookup_not_saved file=\"%s\"",
						g_current_mission_file);
				}
			} else {
				fe_disk_io_close_global_stream(0);
				fe_disk_io_read_all_bytes_or_fatal(
					fallback_file_name,
					g_active_rgb565_to_palette_index_lut);
			}
		} else {
			fe_disk_io_close_global_stream(0);
			fe_disk_io_read_all_bytes_or_fatal(
				g_current_mission_file,
				g_active_rgb565_to_palette_index_lut);
		}

		g_current_mission_file[extension_offset] = saved_extension0;
		g_current_mission_file[extension_offset + 1] = saved_extension1;
		g_current_mission_file[extension_offset + 2] = saved_extension2;
	}

	if (g_generate_mission_palette != 0 && g_flight_bytes_per_pixel == 1) {
		image_quantizer_begin_palette_collection(
			GENERATED_PALETTE_COLOR_COUNT, QUANTIZER_TREE_DEPTH);
	}
	g_loading_model = 1;
	fe_disk_io_load_resources();
	g_loading_model = 0;
	model_mesh_build_object_type_mesh_cache();

	unsigned int background_color_index;
	{
		if (g_generate_mission_palette != 0 &&
		    g_flight_bytes_per_pixel == 1) {
			image_quantizer_export_palette6_bit_and_destroy(
				GENERATED_PALETTE_COLOR_COUNT,
				QUANTIZER_TREE_DEPTH,
				(uint8_t *)&g_sw_palette
					[GENERATED_PALETTE_START]);
			unsigned int extension_offset =
				strlen(g_current_mission_file);
			uint8_t saved_extension0 = g_current_mission_file
				[extension_offset - MISSION_EXTENSION_LENGTH];
			uint8_t saved_extension1 = g_current_mission_file
				[extension_offset - MISSION_EXTENSION_LENGTH +
				 1];
			uint8_t saved_extension2 =
				g_current_mission_file[extension_offset - 1];
			extension_offset -= MISSION_EXTENSION_LENGTH;
			g_current_mission_file[extension_offset] = 'p';
			g_current_mission_file[extension_offset + 1] = 'a';
			g_current_mission_file[extension_offset + 2] = 'l';
			if (fe_disk_io_open_global_stream(
				    g_current_mission_file, "wb", 0, 1) != 0) {
				FILE_RAW_WRITE(
					&g_sw_palette[GENERATED_PALETTE_START],
					sizeof(g_sw_palette
						       [GENERATED_PALETTE_START]) *
						GENERATED_PALETTE_COLOR_COUNT,
					1, g_stream);
				fe_disk_io_close_global_stream(1);
			}

			g_active_rgb565_to_palette_index_lut =
				g_rgb565_to_palette_index_lut;
			color_build_rgb565_to_palette_index_lut(
				g_active_rgb565_to_palette_index_lut,
				GENERATED_PALETTE_START, PALETTE_COLOR_COUNT);
			g_current_mission_file[extension_offset] = 'i';
			g_current_mission_file[extension_offset + 1] = 'n';
			g_current_mission_file[extension_offset + 2] = 'v';
			if (fe_disk_io_open_global_stream(
				    g_current_mission_file, "wb", 0, 1) != 0) {
				/* Writes the whole 65,536-byte RGB565 lookup
				 * table as 256 x 256 bytes. */
				FILE_RAW_WRITE(
					g_active_rgb565_to_palette_index_lut,
					PALETTE_COLOR_COUNT,
					PALETTE_COLOR_COUNT, g_stream);
				fe_disk_io_close_global_stream(1);
			}
			g_current_mission_file[extension_offset] =
				saved_extension0;
			g_current_mission_file[extension_offset + 1] =
				saved_extension1;
			g_current_mission_file[extension_offset + 2] =
				saved_extension2;

			fe_disk_io_read_all_bytes_or_fatal(
				g_flight_palette_resource_file_name,
				g_flight_aux_buffer);
			for (int palette_offset = 0;
			     palette_offset < PALETTE_BYTE_COUNT / 2;
			     palette_offset += sizeof(struct rgb_triplet)) {
				uint8_t temporary_component =
					g_flight_aux_buffer[palette_offset] >>
					2;
				g_flight_aux_buffer[palette_offset] =
					g_flight_aux_buffer
						[PALETTE_BYTE_COUNT -
						 sizeof(struct rgb_triplet) -
						 palette_offset] >>
					2;
				g_flight_aux_buffer[PALETTE_BYTE_COUNT -
						    sizeof(struct rgb_triplet) -
						    palette_offset] =
					temporary_component;
				temporary_component =
					g_flight_aux_buffer[palette_offset +
							    1] >>
					2;
				g_flight_aux_buffer[palette_offset + 1] =
					g_flight_aux_buffer
						[PALETTE_BYTE_COUNT -
						 sizeof(struct rgb_triplet) -
						 palette_offset + 1] >>
					2;
				g_flight_aux_buffer[PALETTE_BYTE_COUNT -
						    sizeof(struct rgb_triplet) -
						    palette_offset + 1] =
					temporary_component;
				temporary_component =
					g_flight_aux_buffer[palette_offset +
							    2] >>
					2;
				g_flight_aux_buffer[palette_offset + 2] =
					g_flight_aux_buffer
						[PALETTE_BYTE_COUNT -
						 sizeof(struct rgb_triplet) -
						 palette_offset + 2] >>
					2;
				g_flight_aux_buffer[PALETTE_BYTE_COUNT -
						    sizeof(struct rgb_triplet) -
						    palette_offset + 2] =
					temporary_component;
			}
			g_flight_set_palette_range_fn(
				(struct rgb_triplet *)g_flight_aux_buffer, 0,
				PALETTE_COLOR_COUNT);
		}

		struct rgb_triplet target_rgb;
		target_rgb.r = 0;
		target_rgb.g = 0;
		target_rgb.b = 2;
		background_color_index = color_find_nearest_rgb_triplet_index(
			(const uint8_t *)&target_rgb,
			(const uint8_t *)g_sw_palette, 0, PALETTE_COLOR_COUNT);
	}
	g_flight_transparent_color_index = (uint8_t)background_color_index;
	g_flight_background_color_index = (uint8_t)background_color_index;
	XVT_LOG_INFO("resources.loaded bpp=%d mode=%d background=%u proving=%d",
		     g_flight_bytes_per_pixel, g_flight_resolution_mode,
		     background_color_index,
		     (int)g_flight_mission_state.proving_grounds_mode_active);
	return background_color_index;
}

/* Sets an object type's max_bounds_extent and half of it from its model bounds,
 * then, unless model_def_index is 0xFF, fills that model definition from the
 * loaded OPT model: bound sizes halved until each is at most 0x280, with the
 * shift; dock heights from the z bounds where the table left 0; hangar, dock
 * and primary hardpoint points from hardpoint types 25 to 31; up to two laser
 * groups and two warhead launchers from the other hardpoint types (weapon type
 * = hardpoint type - 120); then the 16 weapon slots, laser groups' hardpoints
 * first in mesh order, each turret's second hardpoint kept as the first's
 * alternate, then the launchers', with each group's first slot, last slot and
 * count. Object type 53's weapon points are halved. A group that starts with
 * all 16 slots taken is cleared. */
// FUNCTION: XVT 0x49D860
void fe_disk_io_build_model_def(uint8_t model_def_index,
				object_type_id object_type)
{
	g_object_type_table[(uint8_t)object_type].max_bounds_extent =
		model_bounds_get_max_extent((uint8_t)object_type);
	g_object_type_table[(uint8_t)object_type].half_bounds_extent =
		g_object_type_table[(uint8_t)object_type].max_bounds_extent >>
		1;
	if (model_def_index == 0xFF) {
		XVT_LOG_DEBUG("resources.model_extent type=%u extent=%d",
			      (unsigned)(uint8_t)object_type,
			      g_object_type_table[(uint8_t)object_type]
				      .max_bounds_extent);
		return;
	}

	unsigned int size_x = model_bounds_get_size_x((uint8_t)object_type);
	unsigned int size_y = model_bounds_get_size_y((uint8_t)object_type);
	unsigned int size_z = model_bounds_get_size_z((uint8_t)object_type);
	uint16_t bound_shift = 0;
	while (size_x > 0x280 || size_y > 0x280 || size_z > 0x280) {
		size_x >>= 1;
		size_y >>= 1;
		size_z >>= 1;
		++bound_shift;
	}
	g_model_defs[model_def_index].bound_size_shift = bound_shift;
	g_model_defs[model_def_index].bound_size_x = (int16_t)size_x;
	g_model_defs[model_def_index].bound_size_y = (int16_t)size_y;
	g_model_defs[model_def_index].bound_size_z = (int16_t)size_z;
	if (g_model_defs[model_def_index].dock_to_up[0] == 0) {
		g_model_defs[model_def_index].dock_to_up[0] =
			(int16_t)model_bounds_get_min_z((uint8_t)object_type);
		g_model_defs[model_def_index].dock_to_up[1] =
			(int16_t)model_bounds_get_min_z((uint8_t)object_type);
	}
	if (g_model_defs[model_def_index].dock_from_up[0] == 0) {
		g_model_defs[model_def_index].dock_from_up[0] =
			(int16_t)model_bounds_get_max_z((uint8_t)object_type);
		g_model_defs[model_def_index].dock_from_up[1] =
			(int16_t)model_bounds_get_max_z((uint8_t)object_type);
	}

	int mesh_count =
		model_mesh_get_object_type_mesh_count((uint8_t)object_type);
	uint16_t mesh_index;
	uint16_t hardpoint_index;
	int out_y;
	int out_z;
	mesh_component_type mesh_type;
	int hardpoint_count;
	int out_x;
	int hardpoint_type;
	int current_mesh_index;
	{
		for (mesh_index = 0; mesh_index < mesh_count; ++mesh_index) {
			current_mesh_index = mesh_index;
			hardpoint_count = model_mesh_count_hardpoints(
				(uint8_t)object_type, current_mesh_index);

			if (hardpoint_count == 0) {
				continue;
			}
			mesh_type = model_mesh_get_object_type_mesh_type(
				(uint8_t)object_type, current_mesh_index);
			for (hardpoint_index = 0;
			     hardpoint_index < hardpoint_count;
			     ++hardpoint_index) {
				int16_t handled = 0;
				model_mesh_get_hardpoint(
					(uint8_t)object_type,
					current_mesh_index, hardpoint_index,
					&hardpoint_type, &out_x, &out_y,
					&out_z);
				switch (hardpoint_type) {
				case 25:
					g_model_defs[model_def_index]
						.hangar_points.inside.side =
						out_x;
					g_model_defs[model_def_index]
						.hangar_points.inside.up =
						out_z;
					g_model_defs[model_def_index]
						.hangar_points.inside.forward =
						out_y;
					handled = 1;
					break;
				case 26:
					g_model_defs[model_def_index]
						.hangar_points.outside.side =
						out_x;
					g_model_defs[model_def_index]
						.hangar_points.outside.up =
						out_z;
					g_model_defs[model_def_index]
						.hangar_points.outside.forward =
						out_y;
					handled = 1;
					break;
				case 27:
					g_model_defs[model_def_index]
						.dock_from_up[1] =
						(int16_t)out_z;
					g_model_defs[model_def_index]
						.dock_forward = (int16_t)out_y;
					handled = 1;
					break;
				case 28:
					g_model_defs[model_def_index]
						.dock_from_up[0] =
						(int16_t)out_z;
					g_model_defs[model_def_index]
						.dock_forward = (int16_t)out_y;
					handled = 1;
					break;
				case 29:
					g_model_defs[model_def_index]
						.dock_to_up[1] = (int16_t)out_z;
					g_model_defs[model_def_index]
						.dock_forward = (int16_t)out_y;
					handled = 1;
					break;
				case 30:
					g_model_defs[model_def_index]
						.dock_to_up[0] = (int16_t)out_z;
					g_model_defs[model_def_index]
						.dock_forward = (int16_t)out_y;
					handled = 1;
					break;
				case 31:
					g_model_defs[model_def_index]
						.primary_hardpoint_z =
						(int16_t)out_z;
					g_model_defs[model_def_index]
						.primary_hardpoint_y =
						(int16_t)out_y;
					handled = 1;
					break;
				default:
					break;
				}
				if (handled != 0) {
					continue;
				}
				{
					uint8_t weapon_code =
						(uint8_t)(hardpoint_type - 120);
					uint16_t group_slot;

					for (group_slot = 0; group_slot < 2;
					     ++group_slot) {
						if (g_model_defs[model_def_index]
							    .laser_group_weapon_type
								    [group_slot] ==
						    weapon_code) {
							break;
						}
					}
					if (group_slot < 2) {
						continue;
					}
					for (group_slot = 0; group_slot < 2;
					     ++group_slot) {
						if (g_model_defs[model_def_index]
							    .warhead_launcher_type
								    [group_slot] ==
						    weapon_code) {
							break;
						}
					}
					if (group_slot < 2) {
						continue;
					}
					if (g_opt_hardpoint_weapon_group_kind_by_type
						    [hardpoint_type] == 1) {
						for (group_slot = 0;
						     group_slot < 2;
						     ++group_slot) {
							if (g_model_defs[model_def_index]
								    .laser_group_weapon_type
									    [group_slot] ==
							    0) {
								break;
							}
						}
						if (group_slot < 2) {
							g_model_defs[model_def_index]
								.laser_group_weapon_type
									[group_slot] =
								(uint8_t)(hardpoint_type -
									  120);
							if (mesh_type ==
								    MESH_COMPONENT_04_LASR_TUR ||
							    mesh_type ==
								    MESH_COMPONENT_21_ROTATING_LASR_TUR ||
							    mesh_type ==
								    MESH_COMPONENT_05_LASR_GUN ||
							    g_object_type_table[(uint8_t)
											object_type]
									    .genus_id ==
								    CRAFT_GENUS_FREIGHTER ||
							    g_object_type_table[(uint8_t)
											object_type]
									    .genus_id ==
								    CRAFT_GENUS_PLATFORM ||
							    g_object_type_table[(uint8_t)
											object_type]
									    .genus_id ==
								    CRAFT_GENUS_STARSHIP) {
								g_model_defs[model_def_index]
									.laser_group_mount_type
										[group_slot] =
									2;
							} else {
								g_model_defs[model_def_index]
									.laser_group_mount_type
										[group_slot] =
									(hardpoint_type ==
										 5 ||
									 hardpoint_type ==
										 16);
							}
						}
					} else if (
						g_opt_hardpoint_weapon_group_kind_by_type
							[hardpoint_type] == 2) {
						for (group_slot = 0;
						     group_slot < 2;
						     ++group_slot) {
							if (g_model_defs[model_def_index]
								    .warhead_launcher_type
									    [group_slot] ==
							    0) {
								break;
							}
						}
						if (group_slot < 2) {
							g_model_defs[model_def_index]
								.warhead_launcher_type
									[group_slot] =
								(uint8_t)(hardpoint_type -
									  120);
						}
					}
				}
			}
		}
	}

	/* Rebuild laser hardpoint slots in mesh order, preserving paired turret hardpoints. */
	uint8_t weapon_slot_count = 0;
	{
		uint16_t group_index;
		uint8_t wanted_hardpoint_type;

		uint8_t slot_start;
		for (group_index = 0; group_index < 2; ++group_index) {
			slot_start = (uint8_t)weapon_slot_count;

			if (weapon_slot_count == 16) {
				if (g_model_defs[model_def_index]
					    .laser_group_weapon_type
						    [group_index] != 0) {
					XVT_LOG_WARN(
						"resources.weapon_group_dropped type=%u def=%u kind=\"laser\" group=%u weapon=%u",
						(unsigned)(uint8_t)object_type,
						(unsigned)model_def_index,
						(unsigned)group_index,
						(unsigned)g_model_defs[model_def_index]
							.laser_group_weapon_type
								[group_index]);
				}
				g_model_defs[model_def_index]
					.laser_group_weapon_type[group_index] =
					0;
				g_model_defs[model_def_index]
					.laser_group_mount_type[group_index] =
					0;
				continue;
			}
			wanted_hardpoint_type =
				(uint8_t)(g_model_defs[model_def_index]
						  .laser_group_weapon_type
							  [group_index] +
					  120);
			/* current_mesh_index is reused here as the hardpoint
			 * type to match, not a mesh index. */
			current_mesh_index = wanted_hardpoint_type;
			for (mesh_index = 0; mesh_index < mesh_count;
			     ++mesh_index) {
				hardpoint_count = model_mesh_count_hardpoints(
					(uint8_t)object_type, mesh_index);
				if (hardpoint_count == 0) {
					continue;
				}
				uint16_t alternate_slot = 0xFF;
				mesh_type =
					model_mesh_get_object_type_mesh_type(
						(uint8_t)object_type,
						mesh_index);
				for (hardpoint_index = 0;
				     hardpoint_index < hardpoint_count;
				     ++hardpoint_index) {
					model_mesh_get_hardpoint(
						(uint8_t)object_type,
						mesh_index, hardpoint_index,
						&hardpoint_type, &out_x, &out_y,
						&out_z);
					if (hardpoint_type !=
					    current_mesh_index) {
						continue;
					}
					if (alternate_slot == 0xFF) {
						if ((uint8_t)object_type ==
						    53) {
							out_x >>= 1;
							out_y >>= 1;
							out_z >>= 1;
						}
						g_model_defs[model_def_index]
							.weapon_hardpoints
								[weapon_slot_count]
							.x = (int16_t)out_x;
						g_model_defs[model_def_index]
							.weapon_hardpoints
								[weapon_slot_count]
							.y = (int16_t)out_y;
						g_model_defs[model_def_index]
							.weapon_hardpoints
								[weapon_slot_count]
							.z = (int16_t)out_z;
						g_model_defs[model_def_index]
							.weapon_hardpoints
								[weapon_slot_count]
							.mesh_idx =
							(uint8_t)mesh_index;
						g_model_defs[model_def_index]
							.weapon_hardpoints
								[weapon_slot_count]
							.alternate_mesh_hardpoint_idx =
							0xFF;
						if (mesh_type ==
							    MESH_COMPONENT_04_LASR_TUR ||
						    mesh_type ==
							    MESH_COMPONENT_21_ROTATING_LASR_TUR) {
							alternate_slot =
								weapon_slot_count;
						}
						++weapon_slot_count;
						if (weapon_slot_count == 16) {
							break;
						}
					} else {
						g_model_defs[model_def_index]
							.weapon_hardpoints
								[alternate_slot]
							.alternate_mesh_hardpoint_idx =
							(uint8_t)model_mesh_get_hardpoint_index(
								(uint8_t)
									object_type,
								mesh_index,
								hardpoint_index);
						alternate_slot = 0xFF;
					}
				}
				if (weapon_slot_count == 16) {
					break;
				}
			}
			if (slot_start != weapon_slot_count) {
				g_model_defs[model_def_index]
					.laser_group_first_slot[group_index] =
					slot_start;
				g_model_defs[model_def_index]
					.laser_group_last_slot[group_index] =
					(uint8_t)(weapon_slot_count - 1);
				g_model_defs[model_def_index]
					.laser_group_slot_count[group_index] =
					(uint8_t)(weapon_slot_count -
						  slot_start);
			}
		}

		/* Rebuild warhead launcher slots after the laser slots. */
		for (group_index = 0; group_index < 2; ++group_index) {
			slot_start = (uint8_t)weapon_slot_count;

			if (weapon_slot_count == 16) {
				if (g_model_defs[model_def_index]
					    .warhead_launcher_type
						    [group_index] != 0) {
					XVT_LOG_WARN(
						"resources.weapon_group_dropped type=%u def=%u kind=\"warhead\" group=%u weapon=%u",
						(unsigned)(uint8_t)object_type,
						(unsigned)model_def_index,
						(unsigned)group_index,
						(unsigned)g_model_defs[model_def_index]
							.warhead_launcher_type
								[group_index]);
				}
				g_model_defs[model_def_index]
					.warhead_launcher_type[group_index] = 0;
				continue;
			}
			wanted_hardpoint_type =
				(uint8_t)(g_model_defs[model_def_index]
						  .warhead_launcher_type
							  [group_index] +
					  120);
			/* current_mesh_index is reused here as the hardpoint
			 * type to match, not a mesh index. */
			current_mesh_index = wanted_hardpoint_type;
			for (mesh_index = 0; mesh_index < mesh_count;
			     ++mesh_index) {
				hardpoint_count = model_mesh_count_hardpoints(
					(uint8_t)object_type, mesh_index);
				if (hardpoint_count == 0) {
					continue;
				}
				model_mesh_get_object_type_mesh_type(
					(uint8_t)object_type, mesh_index);
				for (hardpoint_index = 0;
				     hardpoint_index < hardpoint_count;
				     ++hardpoint_index) {
					model_mesh_get_hardpoint(
						(uint8_t)object_type,
						mesh_index, hardpoint_index,
						&hardpoint_type, &out_x, &out_y,
						&out_z);
					if (hardpoint_type !=
					    current_mesh_index) {
						continue;
					}
					if ((uint8_t)object_type == 53) {
						out_x >>= 1;
						out_y >>= 1;
						out_z >>= 1;
					}
					g_model_defs[model_def_index]
						.weapon_hardpoints
							[weapon_slot_count]
						.x = (int16_t)out_x;
					g_model_defs[model_def_index]
						.weapon_hardpoints
							[weapon_slot_count]
						.y = (int16_t)out_y;
					g_model_defs[model_def_index]
						.weapon_hardpoints
							[weapon_slot_count]
						.z = (int16_t)out_z;
					g_model_defs[model_def_index]
						.weapon_hardpoints
							[weapon_slot_count]
						.mesh_idx = (uint8_t)mesh_index;
					++weapon_slot_count;
					if (weapon_slot_count == 16) {
						break;
					}
				}
				if (weapon_slot_count == 16) {
					break;
				}
			}
			if (slot_start != weapon_slot_count) {
				g_model_defs[model_def_index]
					.warhead_launcher_first_slot
						[group_index] = slot_start;
				g_model_defs[model_def_index]
					.warhead_launcher_last_slot
						[group_index] =
					(uint8_t)(weapon_slot_count - 1);
				g_model_defs[model_def_index]
					.warhead_launcher_slot_count
						[group_index] =
					(uint8_t)(weapon_slot_count -
						  slot_start);
			}
		}
	}
	XVT_LOG_DEBUG(
		"resources.model_built type=%u def=%u extent=%d shift=%u size_x=%d size_y=%d size_z=%d slots=%u laser1=%u laser2=%u laser1_slots=%u laser2_slots=%u warhead1=%u warhead2=%u warhead1_slots=%u warhead2_slots=%u",
		(unsigned)(uint8_t)object_type, (unsigned)model_def_index,
		g_object_type_table[(uint8_t)object_type].max_bounds_extent,
		(unsigned)g_model_defs[model_def_index].bound_size_shift,
		(int)g_model_defs[model_def_index].bound_size_x,
		(int)g_model_defs[model_def_index].bound_size_y,
		(int)g_model_defs[model_def_index].bound_size_z,
		(unsigned)weapon_slot_count,
		(unsigned)g_model_defs[model_def_index]
			.laser_group_weapon_type[0],
		(unsigned)g_model_defs[model_def_index]
			.laser_group_weapon_type[1],
		(unsigned)g_model_defs[model_def_index]
			.laser_group_slot_count[0],
		(unsigned)g_model_defs[model_def_index]
			.laser_group_slot_count[1],
		(unsigned)g_model_defs[model_def_index]
			.warhead_launcher_type[0],
		(unsigned)g_model_defs[model_def_index]
			.warhead_launcher_type[1],
		(unsigned)g_model_defs[model_def_index]
			.warhead_launcher_slot_count[0],
		(unsigned)g_model_defs[model_def_index]
			.warhead_launcher_slot_count[1]);
}

#ifndef XVT_MODERN
/* Shows a two-line box mid-screen, g_file_name with the
 * DISK_IO_STR_RES_320_NOT_SUPPORTED string, then the
 * DISK_IO_STR_RES_512_NOT_SUPPORTED string, waits for a key and returns it,
 * restoring the screen strip and the text state. Only the original build calls
 * this. */
// FUNCTION: XVT 0x49E060
char fe_disk_io_show_retry_fail_prompt(void)
{
	int16_t saved_cursor_x = g_flight_cursor_x;
	int16_t saved_cursor_y = g_flight_cursor_y;
	int16_t saved_clip_left = g_flight_clip_left;
	int16_t saved_clip_top = g_flight_clip_top;
	int16_t saved_clip_right = g_flight_clip_right;
	int16_t saved_clip_bottom = g_flight_clip_bottom;
	int16_t saved_word_wrap = g_flight_word_wrap_enabled;
	int16_t saved_unused_state = g_flight_text_unused_state;
	uint8_t saved_text_color = g_flight_text_color_index;
	int16_t saved_clear_line_bg = g_flight_clear_line_bg_enabled;
	uint8_t saved_bg_color = g_flight_text_bg_color;
	uint8_t saved_shadow_color = g_flight_text_shadow_color;
	uint8_t saved_shadow_enabled = g_flight_text_shadow_enabled;
	uint8_t saved_font_tier = g_flight_font_tier;

	flight_surface_lock();
	flight_text_set_font_tier(1);
	int line_height = 4 * g_flight_font_line_height;
	uint8_t *saved_pixels = g_flight_scratch_screen_buffer +
				g_screen_width * g_flight_bytes_per_pixel *
					(g_screen_height - line_height - 1);
	g_flight_save_screen_rect_fn(saved_pixels, 0,
				     ((unsigned int)g_screen_height >> 1) -
					     2 * g_flight_font_line_height,
				     (int16_t)g_screen_width, line_height + 1);
	flight_text_set_clip_rect(
		(int16_t)((unsigned int)g_screen_width >> 4),
		(int16_t)(((unsigned int)g_screen_height >> 1) -
			  2 * g_flight_font_line_height),
		(int16_t)(g_screen_width - ((unsigned int)g_screen_width >> 4)),
		(int16_t)(((unsigned int)g_screen_height >> 1) +
			  2 * g_flight_font_line_height));
	g_flight_text_bg_color = 0xf9;
	g_flight_fill_clip_rect_fn();
	flight_text_set_clip_rect(
		(int16_t)(((unsigned int)g_screen_width >> 4) + 1),
		(int16_t)(((unsigned int)g_screen_height >> 1) -
			  2 * g_flight_font_line_height + 1),
		(int16_t)(g_screen_width - ((unsigned int)g_screen_width >> 4) -
			  1),
		(int16_t)(((unsigned int)g_screen_height >> 1) +
			  2 * g_flight_font_line_height - 1));
	g_flight_text_bg_color = 0;
	g_flight_fill_clip_rect_fn();
	g_flight_text_color_index = 0xf9;
	g_flight_text_shadow_color = 0;
	g_flight_text_shadow_enabled = 0;
	flight_text_set_cursor(0, ((unsigned int)g_screen_height >> 1) -
					  g_flight_font_line_height - 2);
	char str[256];
	strcpy(str, g_file_name);
	strcat(str, ": ");
	strcat(str, g_str_disk_io_messages[DISK_IO_STR_RES_320_NOT_SUPPORTED]);
	flight_text_draw_string_centered(str);
	flight_text_set_cursor(0, ((unsigned int)g_screen_height >> 1) + 2);
	flight_text_draw_string_centered(
		g_str_disk_io_messages[DISK_IO_STR_RES_512_NOT_SUPPORTED]);

	int saved_lock_count = flight_surface_get_lock_count();
	int remaining_locks = saved_lock_count;
	while (remaining_locks > 0) {
		flight_surface_unlock();
		--remaining_locks;
	}
	flight_display_blit_render_surface();
	flight_display_flip();
	char next_key = flight_input_get_next_key();
	while (saved_lock_count > 0) {
		flight_surface_lock();
		--saved_lock_count;
	}

	g_flight_restore_screen_rect_fn(saved_pixels, 0,
					((unsigned int)g_screen_height >> 1) -
						2 * g_flight_font_line_height,
					(int16_t)g_screen_width,
					4 * g_flight_font_line_height + 1);
	flight_text_set_font_tier(saved_font_tier);
	g_flight_cursor_x = saved_cursor_x;
	g_flight_cursor_y = saved_cursor_y;
	g_flight_clip_left = saved_clip_left;
	g_flight_clip_top = saved_clip_top;
	g_flight_clip_right = saved_clip_right;
	g_flight_clip_bottom = saved_clip_bottom;
	g_flight_word_wrap_enabled = saved_word_wrap;
	g_flight_text_unused_state = saved_unused_state;
	g_flight_text_color_index = saved_text_color;
	g_flight_clear_line_bg_enabled = saved_clear_line_bg;
	g_flight_text_bg_color = saved_bg_color;
	g_flight_text_shadow_color = saved_shadow_color;
	g_flight_text_shadow_enabled = saved_shadow_enabled;
	flight_surface_unlock();

	int current_lock_count = flight_surface_get_lock_count();
	remaining_locks = current_lock_count;
	while (remaining_locks > 0) {
		flight_surface_unlock();
		--remaining_locks;
	}
	flight_display_blit_render_surface();
	flight_display_flip();
	while (current_lock_count > 0) {
		flight_surface_lock();
		--current_lock_count;
	}
	return next_key;
}

/* Shows message and the press-a-key-to-exit string in a box mid-screen with the
 * surface locks released, waits for a key, restores the locks and text state
 * and returns the key. Only the original build calls this. */
// FUNCTION: XVT 0x49E420
int fe_disk_io_show_fatal_error_message_and_wait_key(const char *message)
{
	int16_t saved_cursor_x = g_flight_cursor_x;
	int16_t saved_cursor_y = g_flight_cursor_y;
	int16_t saved_clip_left = g_flight_clip_left;
	int16_t saved_clip_top = g_flight_clip_top;
	int16_t saved_clip_right = g_flight_clip_right;
	int16_t saved_clip_bottom = g_flight_clip_bottom;
	int16_t saved_word_wrap = g_flight_word_wrap_enabled;
	int16_t saved_unused_state = g_flight_text_unused_state;
	uint8_t saved_text_color = g_flight_text_color_index;
	int16_t saved_clear_line_bg = g_flight_clear_line_bg_enabled;
	uint8_t saved_bg_color = g_flight_text_bg_color;
	uint8_t saved_shadow_color = g_flight_text_shadow_color;
	uint8_t saved_shadow_enabled = g_flight_text_shadow_enabled;
	uint8_t saved_font_tier = g_flight_font_tier;

	int saved_lock_count = flight_surface_get_lock_count();
	int remaining_locks;
	if (saved_lock_count > 0) {
		remaining_locks = saved_lock_count;
		do {
			flight_surface_unlock();
			--remaining_locks;
		} while (remaining_locks != 0);
	}

	uint8_t saved_display_surfaces_active =
		g_flight_display_surfaces_active;
	g_flight_display_surfaces_active = 1;
	flight_surface_lock();
	flight_text_set_font_tier(1);
	flight_text_set_clip_rect(
		g_screen_width >> 4,
		(g_screen_height >> 1) - 2 * g_flight_font_line_height,
		g_screen_width - (g_screen_width >> 4),
		(g_screen_height >> 1) + 2 * g_flight_font_line_height);
	g_flight_text_bg_color = 0xF9;
	g_flight_fill_clip_rect_fn();
	flight_text_set_clip_rect(
		(g_screen_width >> 4) + 1,
		(g_screen_height >> 1) - 2 * g_flight_font_line_height + 1,
		g_screen_width - (g_screen_width >> 4) - 1,
		(g_screen_height >> 1) + 2 * g_flight_font_line_height - 1);
	g_flight_text_bg_color = 0;
	g_flight_fill_clip_rect_fn();
	int line_height = g_flight_font_line_height;
	g_flight_text_color_index = 0xF9;
	g_flight_text_shadow_color = 0;
	g_flight_text_shadow_enabled = 0;
	flight_text_set_cursor(0, (g_screen_height >> 1) - line_height - 2);
	char str[256];
	strcpy(str, message);
	flight_text_draw_string_centered(str);
	flight_text_set_cursor(0, (g_screen_height >> 1) + 2);
	flight_text_draw_string_centered(
		g_str_file_error_messages[FILE_ERROR_STR_PRESS_KEY_TO_EXIT]);
	flight_surface_unlock();
	flight_display_blit_render_surface();
	flight_display_flip();
	int8_t next_key = flight_input_get_next_key();

	g_flight_display_surfaces_active = saved_display_surfaces_active;
	if (saved_lock_count > 0) {
		do {
			flight_surface_lock();
			--saved_lock_count;
		} while (saved_lock_count != 0);
	}

	flight_text_set_font_tier(saved_font_tier);
	g_flight_cursor_x = saved_cursor_x;
	g_flight_cursor_y = saved_cursor_y;
	g_flight_clip_left = saved_clip_left;
	g_flight_clip_top = saved_clip_top;
	g_flight_clip_right = saved_clip_right;
	g_flight_clip_bottom = saved_clip_bottom;
	g_flight_word_wrap_enabled = saved_word_wrap;
	g_flight_text_unused_state = saved_unused_state;
	g_flight_text_color_index = saved_text_color;
	g_flight_clear_line_bg_enabled = saved_clear_line_bg;
	g_flight_text_bg_color = saved_bg_color;
	g_flight_text_shadow_color = saved_shadow_color;
	g_flight_text_shadow_enabled = saved_shadow_enabled;

	int current_lock_count = flight_surface_get_lock_count();
	if (current_lock_count > 0) {
		remaining_locks = current_lock_count;
		do {
			flight_surface_unlock();
			--remaining_locks;
		} while (remaining_locks != 0);
	}
	flight_display_blit_render_surface();
	flight_display_flip();
	if (current_lock_count > 0) {
		do {
			flight_surface_lock();
			--current_lock_count;
		} while (current_lock_count != 0);
	}
	return next_key;
}

#endif

/* Opens a file into g_stream and returns 1, or 0 with g_stream NULL. The modern
 * build opens through storage, records the resolved path in g_file_name and,
 * with prompt_on_fail, makes a failure fatal; it ignores location_mode. The
 * original build tries the name as given and under the install path (unless
 * location_mode is 2), then on the CD drive under BalanceOfPower and at its root
 * (unless location_mode is 1), four times each; with prompt_on_fail the retry
 * prompt repeats the search on R and ends fatally on F. */
// FUNCTION: XVT 0x49E720
int fe_disk_io_open_global_stream(const char *file_name, const char *mode,
				  int prompt_on_fail, int location_mode)
{
#ifdef XVT_MODERN
	(void)location_mode;
	g_stream = file_open(file_name, mode);
	xvt_storage_capture_global_stream();
	snprintf(g_file_name, sizeof(g_file_name), "%s",
		 xvt_storage_last_path());
	XVT_LOG_DEBUG(
		"resources.file_opened file=\"%s\" path=\"%s\" mode=\"%s\" opened=%d required=%d stream=%p",
		file_name, g_file_name, mode, g_stream != NULL, prompt_on_fail,
		(void *)g_stream);
	if (!g_stream && prompt_on_fail) {
		xvt_storage_fatal("Cannot open required file", 1);
	}
	return g_stream != NULL;
#else

	strcpy(g_file_name, file_name);
	while (1) {
		int attempts_remaining = 4;
		if (location_mode != 2) {
			while (attempts_remaining-- != 0) {
				g_stream = FILE_RAW_OPEN(file_name, mode);
				if (g_stream != NULL) {
					return 1;
				}
			}

			attempts_remaining = 4;
			strcpy(g_file_name, file_get_base_game_install_path());
			strcat(g_file_name, "\\");
			strcat(g_file_name, file_name);
			while (attempts_remaining-- != 0) {
				g_stream = FILE_RAW_OPEN(g_file_name, mode);
				if (g_stream != NULL) {
					return 1;
				}
			}
		}

		if (location_mode != 1) {
			strcpy(g_file_name, "D:\\BalanceOfPower\\");
			attempts_remaining = 4;
			g_file_name[0] = file_get_cd_drive_letter();
			strcat(g_file_name, file_name);
			while (attempts_remaining-- != 0) {
				g_stream = FILE_RAW_OPEN(g_file_name, mode);
				if (g_stream != NULL) {
					return 1;
				}
			}

			attempts_remaining = 4;
			strcpy(g_file_name, "D:\\");
			g_file_name[0] = file_get_cd_drive_letter();
			strcat(g_file_name, file_name);
			while (attempts_remaining-- != 0) {
				g_stream = FILE_RAW_OPEN(g_file_name, mode);
				if (g_stream != NULL) {
					return 1;
				}
			}
		}

		if (prompt_on_fail == 0) {
			break;
		}
		while (1) {
			char key = fe_disk_io_show_retry_fail_prompt();
			if (key == 'R' || key == 'r') {
				break;
			}
			if (key == 'F' || key == 'f') {
				fe_disk_io_fatal_error(
					FILE_ERROR_STR_FILE_MISSING);
				g_stream = NULL;
				return 0;
			}
		}
	}
	g_stream = NULL;
	return 0;

#endif
}

/* Closes g_stream and returns 1 when it failed, else 0. The modern build closes
 * through storage and sets g_stream to NULL. The original build fails a stream
 * whose flag word has bit 0x20 set without closing it, and after a failure
 * deletes g_file_name when remove_file_on_error is set; it leaves g_stream as it
 * was. */
// FUNCTION: XVT 0x49E9C0
int16_t fe_disk_io_close_global_stream(int16_t remove_file_on_error)
{
#ifdef XVT_MODERN
	int failed =
		xvt_storage_close_global_stream(g_stream, remove_file_on_error);
	if (failed != 0) {
		XVT_LOG_WARN(
			"resources.file_close_failed stream=%p path=\"%s\" remove=%d",
			(void *)g_stream, g_file_name,
			(int)remove_file_on_error);
	} else {
		XVT_LOG_DEBUG("resources.file_closed stream=%p",
			      (void *)g_stream);
	}
	g_stream = NULL;
	return (int16_t)failed;
#else

	int16_t close_error = 0;
	if ((((struct msvc42_file_prefix *)g_stream)->flags & 0x20) != 0 ||
	    FILE_RAW_CLOSE((xvt_file *)g_stream) == EOF) {
		close_error = 1;
	}

	if (remove_file_on_error != 0 && close_error != 0) {
		FILE_REMOVE(g_file_name);
	}

	return close_error;

#endif
}

/* Reads elem_count items into dst. The modern build reads once and makes a short
 * read fatal. The original build keeps reading until all arrive, asking after
 * 15 tries: R allows 5 more, F ends the program; it returns elem_count on
 * success. Both set g_file_read_abort_flag. */
// FUNCTION: XVT 0x49EA10
size_t fe_disk_io_read_with_retry_prompt(void *dst, size_t elem_size,
					 size_t elem_count, xvt_file *stream)
{
#ifdef XVT_MODERN
	size_t count = FILE_RAW_READ(dst, elem_size, elem_count, stream);
	g_file_read_abort_flag = count != elem_count;
	if (g_file_read_abort_flag) {
		XVT_LOG_ERROR(
			"resources.read_short path=\"%s\" size=%u wanted=%u got=%u",
			g_file_name, (unsigned)elem_size, (unsigned)elem_count,
			(unsigned)count);
		xvt_storage_fatal("Incomplete required file read", 1);
	}
	return count;
#else

	size_t requested_count = elem_count;
	uint8_t *output = dst;
	int retries_remaining = 15;
	while (1) {
		--retries_remaining;
		size_t read_count =
			FILE_RAW_READ(output, elem_size, elem_count, stream);
		output += elem_size * read_count;
		elem_count -= read_count;
		if (elem_count == 0) {
			break;
		}
		if (retries_remaining == 0) {
			while (elem_count != 0) {
				char key = fe_disk_io_show_retry_fail_prompt();
				if (key == 'R' || key == 'r') {
					break;
				}
				if (key == 'F' || key == 'f') {
					g_file_read_abort_flag = 1;
					fe_disk_io_fatal_error(
						FILE_ERROR_STR_FILE_MISSING);
					return 0;
				}
			}
			retries_remaining = 5;
		}
	}
	g_file_read_abort_flag = 0;
	return requested_count;

#endif
}

/* Ends the program on a file error. The modern build reports a fixed message
 * through storage. The original build, for an error code below 4 with the fonts
 * loaded, shows the message and waits for a key, then prints it (with
 * g_file_name for FILE_ERROR_STR_FILE_MISSING) and exits with -255 minus the
 * code; without fonts it prints nothing; for a code of 4 or more it prints a
 * buffer it never filled. */
// FUNCTION: XVT 0x49EB60
void fe_disk_io_fatal_error(file_error_string_id error_code)
{
#ifdef XVT_MODERN
	(void)error_code;
	XVT_LOG_ERROR("resources.fatal code=%d", (int)error_code);
	xvt_storage_fatal("Required resource could not be loaded", 1);
#else

	char message[128];

	if ((uint16_t)error_code < 4) {
		if (g_flight_font_small_sw == NULL) {
			message[0] = 0;
		} else {
			const char *error_message =
				g_str_file_error_messages[(uint16_t)error_code];
			fe_disk_io_show_fatal_error_message_and_wait_key(
				error_message);
			uint16_t i;
			for (i = 0; i < sizeof(message); ++i) {
				message[i] = error_message[i];
				if (message[i] == 0) {
					break;
				}
			}
			if (error_code == FILE_ERROR_STR_FILE_MISSING) {
				uint16_t j = 0;
				for (; i < sizeof(message); ++i) {
					message[i] = g_file_name[j++];
					if (message[i] == 0) {
						break;
					}
				}
				message[i++] = '\n';
				message[i] = 0;
			}
		}
	}
	file_print_fatal_message_and_exit(message, -255 - (uint16_t)error_code);

#endif
}

/* Ends the program with a message. The original build prints message to stderr,
 * using it as the format string, and exits with exitCode; the modern build
 * hands both to xvt_storage_fatal. */
// FUNCTION: XVT 0x4ACE60
void file_print_fatal_message_and_exit(const char *message, int exit_code)
{
#ifdef XVT_MODERN
	xvt_storage_fatal(message, exit_code);
#else

	fprintf(stderr, message);
	exit(exit_code);

#endif
}
