#ifndef XVT_FRONTEND_MISSION_DEBRIEF_H
#define XVT_FRONTEND_MISSION_DEBRIEF_H

#include <stdint.h>

#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

extern int g_debrief_disconnected_from_net_game;

struct campaign_award_sprite_entry {
	/* Campaign list id the record belongs to, from its first line; 0 for a
	 * record the file's end cut short. */
	int campaign_id;
	/* Sprite of the campaign's medal, which the campaign medals page
	 * centers on (256, 279); cut to 31 characters. */
	char main_award_sprite_name[32];
	/* Sprites drawn over the medal for single-player award positions 0 to
	 * 14, from the record's last 15 lines; the 16th stays empty. */
	char singleplayer_mission_award_sprite_names[16][32];
	/* Sprites drawn over the medal for multiplayer award positions 0 to 14,
	 * from the 15 lines before the single-player ones; the 16th stays
	 * empty. */
	char multiplayer_mission_award_sprite_names[16][32];
};

extern int g_debrief_team_in_standings[10];
extern int g_debrief_team_has_only_ai_pilots[10];
extern int g_debrief_local_team_rank_index;
extern int g_debrief_active_team_count;
extern int g_debrief_sorted_team_ids[10];
extern int g_debrief_team_has_player[10];
extern int g_debrief_sorted_player_ids[8];
extern int g_debrief_has_player_kills_by_rating;
extern int g_debrief_kills_on_combatant_ids[8];
extern int g_debrief_kills_from_combatant_ids[8];
extern int g_debrief_standings_team_ids[10];
extern int g_debrief_player_kills_shared_total[3];
extern int g_debrief_rank_by_pilot;

int mission_debrief_exit(int frame_counter);
int mission_debrief_update(int frame_counter);
int mission_debrief_draw_mission_overview_page(int frame_counter);
int mission_debrief_draw_player_statistics_page(void);
int mission_debrief_draw_battle_summary_page(void);
int mission_debrief_draw_tournament_summary_page(int frame_counter);
int mission_debrief_draw_tab_bar(void);
int mission_debrief_mark_network_players_ready(void);
int mission_debrief_prepare(void);
void mission_debrief_read_outcome_text(char *out_results, int use_win_text);
int mission_debrief_draw_narrative_text_page(void);

#ifdef __cplusplus
}
#endif

#endif
