#include "xvt/frontend/pilot_record.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "xvt/assets/file.h"
#include "xvt/audio/cd_audio.h"
#include "xvt/audio/frontend_sound.h"
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
#include "xvt/frontend/frontend_file_list.h"
#include "xvt/frontend/frontend_mission.h"
#include "xvt/frontend/frontend_mouse.h"
#include "xvt/frontend/frontend_scrollbar.h"
#include "xvt/frontend/frontend_string.h"
#include "xvt/frontend/frontend_text.h"
#include "xvt/frontend/mission_debrief.h"
#include "xvt/frontend/movie.h"
#include "xvt/input/keyboard.h"
#include "xvt_runtime/log/log_both_builds.h"
#include "xvt_runtime/runtime/dialog_task.h"
#include "xvt_runtime/runtime/frontend_actions.h"
#include "xvt_runtime/runtime/frontend_movies.h"

enum {
	CAMPAIGN_AWARD_FLAG_COUNT = 16,
	CAMPAIGN_AWARD_VISIBLE_FLAG_COUNT = 15,
	CUTSCENE_VIEWER_ROW_HEIGHT = 15,
	CUTSCENE_VIEWER_VISIBLE_ROW_COUNT = 21,
	PILOT_LIST_VISIBLE_COUNT = 13,
	PILOT_LIST_PAGE_STEP = 5,
	PILOT_LIST_SCROLLBAR_CONTROL_ID = 4,
	PILOT_NAME_COMPARE_LENGTH = 12,
	PILOT_NAME_MAX_CHARS = 13,
	PILOT_UI_SOUND_PRIORITY = 255,
	PILOT_UI_SOUND_VOLUME_SCALE = 12,
	PILOT_UI_SOUND_CENTER_PAN = 63,
	PILOT_DELETE_VIRTUAL_KEY = 0x2E,
	MISSION_ACHIEVEMENT_ROW_HEIGHT = 15,
	MISSION_ACHIEVEMENT_VISIBLE_ROWS = 21,
	MISSION_ACHIEVEMENT_TEXT_X = 125,
	MISSION_ACHIEVEMENT_TEXT_RIGHT = 303,
	MISSION_ACHIEVEMENT_SCORE_X = 323,
	MISSION_ACHIEVEMENT_DETAIL_X = 373,
	MISSION_ACHIEVEMENT_HEADER_X = 88,
	MISSION_ACHIEVEMENT_FIRST_Y = 111,
	MISSION_ACHIEVEMENT_FONT_SIZE = 12,
	MISSION_ACHIEVEMENT_TITLE_FONT_SIZE = 15,
	MISSION_ACHIEVEMENT_SCROLL_PAGE_STEP = 5,
	MISSION_ACHIEVEMENT_SCROLL_CONTROL_ID = 3,
	MISSION_ACHIEVEMENT_AWARD_RIGHT = 125,
	MISSION_ACHIEVEMENT_AWARD_HEIGHT = 14,
	MISSION_ACHIEVEMENT_TEXT_COLOR = 0xFFFF,
	MISSION_ACHIEVEMENT_TITLE_LEFT = 84,
	MISSION_ACHIEVEMENT_TITLE_TOP = 90,
	MISSION_ACHIEVEMENT_TITLE_RIGHT = 404,
	MISSION_ACHIEVEMENT_TITLE_BOTTOM = 106,
	MISSION_ACHIEVEMENT_SCROLL_LEFT = 425,
	MISSION_ACHIEVEMENT_SCROLL_TOP = 107,
	MISSION_ACHIEVEMENT_SCROLL_RIGHT = 434,
	MISSION_ACHIEVEMENT_SCROLL_BOTTOM = 433,
	MISSION_ACHIEVEMENT_RATING_TEXT_CODE = 6,
	MISSION_ACHIEVEMENT_NAME_TEXT_CODE = 4,
	MISSION_ACHIEVEMENT_TEXT_TRUNCATED_FLAG = 4,
	MISSION_ACHIEVEMENT_PLACEMENT_STRING_OFFSET = 317,
	MISSION_ACHIEVEMENT_MEDAL_TOOLTIP_OFFSET = 381,
	MISSION_ACHIEVEMENT_CITATION_TOOLTIP_OFFSET = 659,
};

/* Page of the pilot record shown on the concourse: 0 pilot statistics, 1 pilot
 * awards, 2 pilot rating, 3 mission achievements, 4 campaign medals, 5
 * cutscenes. concourse_update sets 0 when it starts;
 * pilot_record_update_navigation_controls changes it when a page button is
 * clicked. */
// GLOBAL: XVT 0xB6A2B0
int g_pilot_record_page = 0;
/* The loaded pilot's whole record, the pilot file's contents, and the
 * frontend's working state for the mission being set up, flown and debriefed.
 * frontend_load_resources zeroes it; the pilot code loads, creates and saves it;
 * many functions write it, chiefly fe_disk_io_commit_flight_results with each
 * mission's results. An empty name means no pilot is loaded. */
// GLOBAL: XVT 0xB6A2E0
struct pilot_data g_pilot_data;
/* Records read into g_campaign_award_sprites. Set by
 * pilot_record_load_campaign_award_sprite_table; set to 0 when game_main or, in the
 * modern build, xvt_frontend_task_shutdown frees the table. */
// GLOBAL: XVT 0xB69CC8
unsigned int g_campaign_award_sprite_count = 0;
/* Heap table of campaign medal sprites read from frontres\campawds.lst, one
 * record per campaign. pilot_record_load_campaign_award_sprite_table allocates it
 * for the count on the file's first line; game_main or, in the modern build,
 * xvt_frontend_task_shutdown frees it. NULL until loaded or when loading failed;
 * the campaign medals page then draws only its title. */
// GLOBAL: XVT 0xB69CD8
struct campaign_award_sprite_entry *g_campaign_award_sprites = NULL;
/* Single-player mission awards of the last campaign
 * pilot_record_draw_campaign_medals_page looked at when it rebuilt: the campaign's
 * missions flown that are award eligible. That page draws the single-player
 * award sprites only when it is not 0. */
// GLOBAL: XVT 0x664EC8
int g_campaign_singleplayer_award_count = 0;
/* Which campaign the campaign medals page shows, counting the attempted
 * campaigns that have a medal record; 0 when the page rebuilds, then the scroll
 * bar's position when there is more than one. */
// GLOBAL: XVT 0x664ED0
int g_campaign_medal_scroll_offset = 0;
/* Per mission position within a campaign, 1 when that single-player campaign
 * mission was flown and is award eligible; the campaign medals page draws the
 * matching sprites of singleplayer_mission_award_sprite_names for positions 0 to
 * 14. pilot_record_draw_campaign_medals_page clears it once per rebuild and then
 * sets flags for every campaign in turn without clearing between them. */
// GLOBAL: XVT 0x664EE8
int g_campaign_singleplayer_award_flags[CAMPAIGN_AWARD_FLAG_COUNT] = {0};
/* Multiplayer counterpart of g_campaign_singleplayer_award_count, from
 * mp_campaign_missions. */
// GLOBAL: XVT 0x664F28
int g_campaign_multiplayer_award_count = 0;
/* Multiplayer counterpart of g_campaign_singleplayer_award_flags, from
 * mp_campaign_missions, drawn with multiplayer_mission_award_sprite_names. */
// GLOBAL: XVT 0x664F60
int g_campaign_multiplayer_award_flags[CAMPAIGN_AWARD_FLAG_COUNT] = {0};
/* Campaigns the current faction attempted, single player or multiplayer, that
 * have a record in g_campaign_award_sprites; the campaign medals page shows one
 * at a time. Counted by pilot_record_draw_campaign_medals_page when it rebuilds. */
// GLOBAL: XVT 0x664FBC
int g_campaign_medal_entry_count = 0;
/* First 15-pixel row shown on the cutscene page; 0 when
 * pilot_record_draw_cutscene_viewer_page rebuilds, then the scroll bar's position
 * when there are more than 21 rows. */
// GLOBAL: XVT 0x664EAC
int g_cutscene_viewer_scroll_row = 0;
/* Tournaments the current faction attempted in single player: entries of
 * g_pilot_record_tournament_mission_list whose sp_tournaments entry has an
 * attempt_count not 0; the rows of that section of the mission achievements
 * page. -1 at start; pilot_record_draw_mission_achievements_page sets it to 0 and
 * counts it when it rebuilds. */
// GLOBAL: XVT 0x664EA0
int g_pilot_sp_tournament_history_count = -1;
/* Combat engagements the current faction flew in multiplayer: entries of
 * g_pilot_record_multiplayer_combat_mission_list whose mp_combat_missions entry has a
 * number_times_flown not 0; the rows of that section of the mission achievements
 * page. -1 at start; pilot_record_draw_mission_achievements_page sets it to 0 and
 * counts it when it rebuilds. */
// GLOBAL: XVT 0x664EA4
int g_pilot_mp_combat_history_count = -1;
/* Tournaments the current faction attempted in multiplayer: entries of
 * g_pilot_record_tournament_mission_list whose mp_tournaments entry has an
 * attempt_count not 0; the rows of that section of the mission achievements
 * page. -1 at start; pilot_record_draw_mission_achievements_page sets it to 0 and
 * counts it when it rebuilds. */
// GLOBAL: XVT 0x664EA8
int g_pilot_mp_tournament_history_count = -1;
/* Training missions the current faction flew in multiplayer: entries of
 * g_pilot_record_multiplayer_training_mission_list whose mp_training_missions entry
 * has a number_times_flown not 0; the rows of that section of the mission
 * achievements page. -1 at start; pilot_record_draw_mission_achievements_page sets
 * it to 0 and counts it when it rebuilds. */
// GLOBAL: XVT 0x664EB0
int g_pilot_mp_training_history_count = -1;
/* Melees the current faction flew in multiplayer: entries of
 * g_pilot_record_melee_mission_list whose mp_melee_missions entry has a
 * number_times_flown not 0; the rows of that section of the mission achievements
 * page. -1 at start; pilot_record_draw_mission_achievements_page sets it to 0 and
 * counts it when it rebuilds. */
// GLOBAL: XVT 0x664ECC
int g_pilot_mp_melee_history_count = -1;
/* Rows of the cutscene page, counted when pilot_record_draw_cutscene_viewer_page
 * rebuilds: 2 for each campaign title, then for each unlocked cutscene 1 for
 * its title plus its thumbnail's height + 18 pixels, divided by 15. */
// GLOBAL: XVT 0x664FB8
int g_cutscene_viewer_total_rows = 0;
/* Pilot name being typed in the roster panel's name field, at most 13
 * characters and a terminating 0. pilot_record_update_pilot_selection_panel clears
 * it on its first frame and after it uses a name or a pilot is clicked. */
// GLOBAL: XVT 0x664FA8
char g_pilot_record_name_input[14] = {0};
/* First pilot shown in the roster panel's 13-row list. Set by its scroll bar
 * when there are more than 13 pilots, by pilot_record_rebuild_pilot_list to the
 * current pilot's position when the panel opens or makes a pilot with more than
 * 13, and to 0 after Delete Pilot. */
// GLOBAL: XVT 0x52B0E0
int g_pilot_list_scroll_offset = 0;
/* Kill assists of the current faction's career, per mission type (0 exercise, 1
 * melee, 2 combat), summed over craft types. -1 at start;
 * pilot_record_draw_pilot_statistics_page sets each to 0 and sums them when it
 * rebuilds. */
// GLOBAL: XVT 0x664EB8
int g_pilot_stats_assists[3] = {-1, -1, -1};
/* Full kills on human pilots in the current faction's career, per mission type,
 * summed over the victims' 25 ratings. -1 at start;
 * pilot_record_draw_pilot_statistics_page sets each to 0 and sums them when it
 * rebuilds. */
// GLOBAL: XVT 0x664ED8
int g_pilot_stats_player_kills[3] = {-1, -1, -1};
/* Times AI pilots killed the pilot in the current faction's career, per mission
 * type, summed over their 6 ratings. -1 at start;
 * pilot_record_draw_pilot_statistics_page sets each to 0 and sums them when it
 * rebuilds. */
// GLOBAL: XVT 0x664F30
int g_pilot_stats_losses_to_non_players[3] = {-1, -1, -1};
/* First row shown on the mission achievements page, which shows 21. -1 at
 * start; pilot_record_draw_mission_achievements_page sets 0 when it rebuilds, then
 * the scroll bar's position when there are more than 21 rows. */
// GLOBAL: XVT 0x664F3C
int g_pilot_achievements_scroll_offset = -1;
/* Training missions the current faction flew in single player: entries of
 * g_pilot_record_singleplayer_training_mission_list whose sp_training_missions entry
 * has a number_times_flown not 0; the rows of that section of the mission
 * achievements page. -1 at start; pilot_record_draw_mission_achievements_page sets
 * it to 0 and counts it when it rebuilds. */
// GLOBAL: XVT 0x664F44
int g_pilot_sp_training_history_count = -1;
/* Melees the current faction flew in single player: entries of
 * g_pilot_record_melee_mission_list whose sp_melee_missions entry has a
 * number_times_flown not 0; the rows of that section of the mission achievements
 * page. -1 at start; pilot_record_draw_mission_achievements_page sets it to 0 and
 * counts it when it rebuilds. */
// GLOBAL: XVT 0x664F48
int g_pilot_sp_melee_history_count = -1;
/* Combat engagements the current faction flew in single player: entries of
 * g_pilot_record_singleplayer_combat_mission_list whose sp_combat_missions entry has a
 * number_times_flown not 0; the rows of that section of the mission achievements
 * page. -1 at start; pilot_record_draw_mission_achievements_page sets it to 0 and
 * counts it when it rebuilds. */
// GLOBAL: XVT 0x664F4C
int g_pilot_sp_combat_history_count = -1;
/* Battles the current faction attempted in multiplayer: entries of
 * g_battle_mission_list whose mp_battles entry has an attempt_count not 0; the rows
 * of that section of the mission achievements page. -1 at start;
 * pilot_record_draw_mission_achievements_page sets it to 0 and counts it when it
 * rebuilds. */
// GLOBAL: XVT 0x664F50
int g_pilot_mp_battle_history_count = -1;
/* 1 when the current faction's career has a full or shared kill of any craft
 * type, so the pilot statistics page shows its craft-kills-by-type section. -1
 * at start; only pilot_record_draw_pilot_statistics_page writes it, when it
 * rebuilds. */
// GLOBAL: XVT 0x664F40
int g_pilot_stats_has_craft_kills_by_type = -1;
/* Scratch flag of pilot_record_draw_pilot_statistics_page, its only user: 1 when
 * the craft type or rating being looked at has a count in any mission type. -1
 * at start. */
// GLOBAL: XVT 0x664F54
int g_pilot_stats_row_has_data = -1;
/* 1 when human pilots of any rating killed the pilot in the current faction's
 * career, so the pilot statistics page shows its losses-to-players-by-rank
 * section. -1 at start; only pilot_record_draw_pilot_statistics_page writes it,
 * when it rebuilds. */
// GLOBAL: XVT 0x664F58
int g_pilot_stats_has_losses_to_players_by_rank = -1;
/* First row shown on the pilot statistics page, which shows 21. -1 at start;
 * pilot_record_draw_pilot_statistics_page sets 0 when it rebuilds, then the scroll
 * bar's position when there are more than 21 rows. */
// GLOBAL: XVT 0x664F5C
int g_pilot_statistics_scroll_offset = -1;
/* Shared kills on AI pilots in the current faction's career, per mission type,
 * summed over their 6 ratings. -1 at start; pilot_record_draw_pilot_statistics_page
 * sets each to 0 and sums them when it rebuilds. */
// GLOBAL: XVT 0x664FC0
int g_pilot_stats_non_player_kills_shared[3] = {-1, -1, -1};
/* Shared kills of the current faction's career, per mission type, summed over
 * craft types. -1 at start; pilot_record_draw_pilot_statistics_page sets each to 0
 * and sums them when it rebuilds. */
// GLOBAL: XVT 0x664FD0
int g_pilot_stats_total_kills_shared[3] = {-1, -1, -1};
/* Full kills on AI pilots in the current faction's career, per mission type,
 * summed over their 6 ratings. -1 at start; pilot_record_draw_pilot_statistics_page
 * sets each to 0 and sums them when it rebuilds. */
// GLOBAL: XVT 0x664FE0
int g_pilot_stats_non_player_kills[3] = {-1, -1, -1};
/* 1 when the current faction's career has a full or shared kill on a human
 * pilot of any rating, so the pilot statistics page shows its
 * player-kills-by-rank section. -1 at start; only
 * pilot_record_draw_pilot_statistics_page writes it, when it rebuilds. */
// GLOBAL: XVT 0x664FEC
int g_pilot_stats_has_player_kills_by_rating = -1;
/* Shared kills on human pilots in the current faction's career, per mission
 * type, summed over the victims' 25 ratings. -1 at start;
 * pilot_record_draw_pilot_statistics_page sets each to 0 and sums them when it
 * rebuilds. */
// GLOBAL: XVT 0x664FF0
int g_pilot_stats_player_kills_shared[3] = {-1, -1, -1};
/* Rows of the pilot record page last rebuilt, for its scroll bar: set by
 * pilot_record_draw_pilot_statistics_page (25, 26 below Jedi Master, plus its
 * sections) and by pilot_record_draw_mission_achievements_page (each history's
 * entries plus 2 header rows per section and a blank row between sections). -1
 * at start. */
// GLOBAL: XVT 0x665000
int g_pilot_record_page_row_count = -1;
/* Rows of the single-player campaign section of the mission achievements page:
 * campaigns of g_pilot_record_singleplayer_campaign_mission_list with an sp_campaigns
 * attempt_count not 0, plus missions of the single-player training list whose
 * sp_campaign_missions entry (mission id - 1) has a number_times_flown not 0. -1 at
 * start; pilot_record_draw_mission_achievements_page sets it to 0 and counts it
 * when it rebuilds. */
// GLOBAL: XVT 0x664FA0
int g_pilot_sp_campaign_history_row_count = -1;
/* Battles the current faction attempted in single player: entries of
 * g_battle_mission_list whose sp_battles entry has an attempt_count not 0; the rows
 * of that section of the mission achievements page. -1 at start;
 * pilot_record_draw_mission_achievements_page sets it to 0 and counts it when it
 * rebuilds. */
// GLOBAL: XVT 0x664FFC
int g_pilot_sp_battle_history_count = -1;
/* Times human pilots killed the pilot in the current faction's career, per
 * mission type, summed over their 25 ratings. -1 at start;
 * pilot_record_draw_pilot_statistics_page sets each to 0 and sums them when it
 * rebuilds. */
// GLOBAL: XVT 0x665008
int g_pilot_stats_losses_to_players[3] = {-1, -1, -1};
/* Multiplayer counterpart of g_pilot_sp_campaign_history_row_count: campaigns of
 * g_pilot_record_multiplayer_campaign_mission_list with an mp_campaigns attempt_count
 * not 0, plus missions of the multiplayer training list whose
 * mp_campaign_missions entry has a number_times_flown not 0. */
// GLOBAL: XVT 0x665014
int g_pilot_mp_campaign_history_row_count = -1;
/* Screen position of each rating's rank sprite (rank0 to rank24) on the pilot
 * rating page, by rating from target drone (0) to Jedi Master (24). */
// GLOBAL: XVT 0x52B018
struct POINT g_pilot_rating_icon_pos[25] = {
	{364, 397}, {364, 381}, {364, 365}, {364, 349}, {197, 397},
	{197, 381}, {197, 365}, {197, 349}, {364, 322}, {364, 306},
	{364, 290}, {364, 274}, {197, 322}, {197, 306}, {197, 290},
	{197, 274}, {364, 247}, {364, 231}, {364, 215}, {364, 199},
	{197, 247}, {197, 231}, {197, 215}, {197, 199}, {187, 123}};

/* Runs one frame of the concourse's pilot roster panel: the list of saved
 * pilots, a name field and the Delete Pilot button. On its first frame
 * (frame_counter 0) it flushes the keyboard, clears g_pilot_record_name_input and
 * rebuilds the list, scrolled to the current pilot when there are more than 13.
 * Clicking a pilot other than the current one (names compared case-blind over
 * 12 characters) loads it with pilot_load_from_path and, when that succeeds,
 * copies its current faction's team, mission directory, mission ids and
 * sequence fields into g_pilot_data; any click then rebuilds the list, redraws
 * the background and sets g_pilot_record_pages_need_rebuild. A name finished in the
 * field loads the listed pilot of that name, compared case-blind, or else
 * pilot_create_new makes a new pilot of that name; that also sets
 * g_pilot_record_pages_need_rebuild. With no saved pilots it asks for a name in a
 * dialog. Delete Pilot, or the Delete key, with a pilot loaded asks for
 * confirmation, calls pilot_delete_current when confirmed and sets
 * g_pilot_list_scroll_offset to 0. A scroll bar sets g_pilot_list_scroll_offset when
 * there are more than 13 pilots. Returns 1. The modern build runs the dialogs
 * as pending actions, returning 1 while one is open, and calls xvt_storage_fatal
 * when the list is missing or a pilot cannot be saved or deleted; only it
 * rebuilds the list after a delete. In the original build, which never marks a
 * match, a typed name that matches a pilot loads it and then also creates a new
 * pilot of that name. Does not check g_pilot_file_list for NULL in the original
 * build. */
// FUNCTION: XVT 0x4BEA50
int pilot_record_update_pilot_selection_panel(int frame_counter)
{
	struct RECT rect;
	struct frontend_file_list_node *node;
	int selected_index;
	int pilot_index;
	int accepted;

	if (xvt_frontend_action_pending(XVT_ACTION_OWNER_PILOT) == 2) {
		if (!xvt_dialog_take_result(&accepted)) {
			return 1;
		}
		xvt_frontend_action_finish(XVT_ACTION_OWNER_PILOT);
		XVT_LOG_DEBUG("pilot.delete_answered accepted=%d", accepted);
		if (accepted && !pilot_delete_current()) {
			xvt_storage_fatal("Cannot delete the selected pilot",
					  1);
		}
		g_pilot_list_scroll_offset = 0;
		pilot_record_rebuild_pilot_list(&selected_index);
		pilot_record_redraw_background();
		g_pilot_record_pages_need_rebuild = 1;
		return 1;
	}
	if (xvt_frontend_action_pending(XVT_ACTION_OWNER_PILOT) == 1) {
		selected_index = frontend_dialog_prompt_for_pilot_name(
			g_pilot_record_name_input);
		if (selected_index == XVT_DIALOG_PENDING) {
			return 1;
		}
		xvt_frontend_action_finish(XVT_ACTION_OWNER_PILOT);
		XVT_LOG_DEBUG("pilot.name_answered result=%d entered=%d",
			      selected_index,
			      g_pilot_record_name_input[0] != '\0');
	} else {
		frontend_draw_rect_assign(&rect, 451, 90, 605, 106);
		frontend_text_draw_centered(
			12, frontend_string_get(FRONTSTR_463_PILOT_ROSTER),
			&rect, 0xFFFF);
		if (frame_counter == 0) {
			keyboard_flush_char_buffer();
			memset(g_pilot_record_name_input, 0,
			       sizeof(g_pilot_record_name_input));
			if (g_pilot_file_list != NULL &&
			    g_pilot_file_list->count >
				    PILOT_LIST_VISIBLE_COUNT) {
				pilot_record_rebuild_pilot_list(
					&g_pilot_list_scroll_offset);
			} else {
				pilot_record_rebuild_pilot_list(
					&selected_index);
			}
		}

		if (g_pilot_file_list != NULL &&
		    g_pilot_file_list->count > PILOT_LIST_VISIBLE_COUNT) {
			frontend_draw_rect_assign(&rect, 596, 117, 605, 320);
			g_pilot_list_scroll_offset = frontend_scrollbar_draw(
				&rect, g_pilot_list_scroll_offset,
				g_pilot_file_list->count, 0,
				PILOT_LIST_PAGE_STEP, g_color_navy,
				PILOT_LIST_SCROLLBAR_CONTROL_ID);
		}

		frontend_draw_rect_assign(&rect, 461, 117, 595, 320);
		/* selected_index holds the clicked pilot row plus one here, or 0. */
		selected_index = pilot_record_draw_pilot_list(
			&rect, g_pilot_list_scroll_offset);
		/* accepted holds a name comparison in this block: nonzero means
		 * the clicked pilot is not the current one. */
		if (selected_index != 0) {
			accepted = strncasecmp(
				g_pilot_data.name,
				g_pilot_list_display_names[selected_index - 1],
				PILOT_NAME_COMPARE_LENGTH);
			XVT_LOG_DEBUG("pilot.list_clicked index=%d other=%d",
				      selected_index - 1, accepted != 0);
			if (accepted != 0) {
				node = g_pilot_file_list->head;
				pilot_index = selected_index - 1;
				for (; pilot_index > 0; --pilot_index) {
					node = node->next;
				}
				if (pilot_load_from_path(node->path) != 0) {
					if (g_game_config.sfx_datapad_enabled !=
					    0) {
						frontend_sound_play_ui_sound(
							"logonsound", 1, 0,
							PILOT_UI_SOUND_PRIORITY,
							PILOT_UI_SOUND_VOLUME_SCALE *
								g_game_config
									.sfx_datapad_volume,
							PILOT_UI_SOUND_CENTER_PAN);
					}
					g_pilot_data.team =
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.team;
					g_pilot_data.mission_directory_id =
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.mission_directory_id;
					memcpy(g_pilot_data
						       .mission_description_ids,
					       g_pilot_data
						       .faction_statistics
							       [g_pilot_data
									.current_faction_id]
						       .mission_description_ids,
					       sizeof(g_pilot_data
							      .mission_description_ids));
					g_pilot_data.mission_sequence_active =
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.mission_sequence_active;
					g_pilot_data
						.saved_mission_description_id =
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.saved_mission_description_id;
					XVT_LOG_INFO(
						"pilot.loaded by=\"list\" index=%d rating=%d faction=%d missions=%d score=%d",
						selected_index - 1,
						(int)g_pilot_data.rating,
						g_pilot_data.current_faction_id,
						g_pilot_data
							.total_missions_played_count,
						g_pilot_data.total_score);
				} else {
					XVT_LOG_WARN(
						"pilot.select_failed by=\"list\" index=%d partial=%d",
						selected_index - 1,
						g_pilot_data.name[0] != '\0');
				}
			}
			memset(g_pilot_record_name_input, 0,
			       sizeof(g_pilot_record_name_input));
			pilot_record_rebuild_pilot_list(&selected_index);
			pilot_record_redraw_background();
			g_pilot_record_pages_need_rebuild = 1;
		}

		if (!g_pilot_file_list) {
			xvt_storage_fatal("Cannot enumerate saved pilots", 1);
			return 1;
		}
		if (g_pilot_file_list->count == 0) {
			if (frame_counter == 0) {
				xvt_frontend_action_trigger(
					XVT_ACTION_OWNER_PILOT, 1, 1);
				XVT_LOG_DEBUG("pilot.name_prompted");
				frontend_dialog_prompt_for_pilot_name(
					g_pilot_record_name_input);
				return 1;
			}
			frontend_draw_rect_assign(&rect, 461, 321, 595, 340);
			selected_index = frontend_text_handle_editable_field(
				&rect, g_pilot_record_name_input,
				PILOT_NAME_MAX_CHARS, 0, 12, "\\*$~|:<>?/\t\"");
		} else {
			frontend_draw_rect_assign(&rect, 461, 321, 595, 340);
			selected_index = frontend_text_handle_editable_field(
				&rect, g_pilot_record_name_input,
				PILOT_NAME_MAX_CHARS, 0, 12, "\\*$~|:<>?/\t\"");
		}
	}
	/* selected_index says here whether the name entry was finished; it is
	 * then reused to say whether the name matched an existing pilot, which
	 * only the modern build sets. */
	if (selected_index != 0 && g_pilot_record_name_input[0] != '\0') {
		selected_index = 0;
		g_pilot_record_pages_need_rebuild = 1;
		if (g_pilot_file_list != NULL) {
			pilot_index = 0;
			node = g_pilot_file_list->head;
			if (g_pilot_list_display_names != NULL &&
			    g_pilot_file_list->count > 0) {
				/* accepted holds a name comparison in this
				 * loop: 0 means the typed name matches this
				 * pilot. */
				for (; pilot_index < g_pilot_file_list->count;
				     ++pilot_index) {
					accepted = strcasecmp(
						g_pilot_list_display_names
							[pilot_index],
						g_pilot_record_name_input);
					if (accepted == 0) {
						selected_index = 1;
						if (pilot_load_from_path(
							    node->path) != 0) {
							if (g_game_config
								    .sfx_datapad_enabled !=
							    0) {
								frontend_sound_play_ui_sound(
									"logonsound",
									1, 0,
									PILOT_UI_SOUND_PRIORITY,
									PILOT_UI_SOUND_VOLUME_SCALE *
										g_game_config
											.sfx_datapad_volume,
									PILOT_UI_SOUND_CENTER_PAN);
							}
							g_pilot_data.team =
								g_pilot_data
									.faction_statistics
										[g_pilot_data
											 .current_faction_id]
									.team;
							g_pilot_data
								.mission_directory_id =
								g_pilot_data
									.faction_statistics
										[g_pilot_data
											 .current_faction_id]
									.mission_directory_id;
							memcpy(g_pilot_data
								       .mission_description_ids,
							       g_pilot_data
								       .faction_statistics
									       [g_pilot_data
											.current_faction_id]
								       .mission_description_ids,
							       sizeof(g_pilot_data
									      .mission_description_ids));
							g_pilot_data
								.mission_sequence_active =
								g_pilot_data
									.faction_statistics
										[g_pilot_data
											 .current_faction_id]
									.mission_sequence_active;
							g_pilot_data
								.saved_mission_description_id =
								g_pilot_data
									.faction_statistics
										[g_pilot_data
											 .current_faction_id]
									.saved_mission_description_id;
							XVT_LOG_INFO(
								"pilot.loaded by=\"typed\" index=%d rating=%d faction=%d missions=%d score=%d",
								pilot_index,
								(int)g_pilot_data
									.rating,
								g_pilot_data
									.current_faction_id,
								g_pilot_data
									.total_missions_played_count,
								g_pilot_data
									.total_score);
						} else {
							XVT_LOG_WARN(
								"pilot.select_failed by=\"typed\" index=%d partial=%d",
								pilot_index,
								g_pilot_data.name
										[0] !=
									'\0');
						}
						break;
					}
					node = node->next;
				}
			}
		}
		XVT_LOG_DEBUG("pilot.name_matched matched=%d index=%d count=%d",
			      selected_index,
			      g_pilot_file_list != NULL ? pilot_index : -1,
			      g_pilot_file_list != NULL
				      ? g_pilot_file_list->count
				      : -1);
		if (selected_index == 0) {
			if (g_game_config.sfx_datapad_enabled != 0) {
				frontend_sound_play_ui_sound(
					"logonsound", 1, 0,
					PILOT_UI_SOUND_PRIORITY,
					PILOT_UI_SOUND_VOLUME_SCALE *
						g_game_config
							.sfx_datapad_volume,
					PILOT_UI_SOUND_CENTER_PAN);
			}
			if (!pilot_create_new(g_pilot_record_name_input)) {
				xvt_storage_fatal("Cannot save the new pilot",
						  1);
				return 1;
			}
			if (g_pilot_file_list != NULL &&
			    g_pilot_file_list->count >
				    PILOT_LIST_VISIBLE_COUNT) {
				pilot_record_rebuild_pilot_list(
					&g_pilot_list_scroll_offset);
			} else {
				pilot_record_rebuild_pilot_list(
					&selected_index);
			}
		}
		memset(g_pilot_record_name_input, 0,
		       sizeof(g_pilot_record_name_input));
		pilot_record_redraw_background();
	}

	frontend_draw_rect_assign(&rect, 451, 358, 605, 378);
	/* selected_index now holds the Delete Pilot button's result. */
	selected_index = frontend_button_handle_text_button(
		&rect, frontend_string_get(FRONTSTR_464_DELETE_PILOT), 15,
		0xFFFF, 20, "buttonsound");
	if ((selected_index != 0 ||
	     keyboard_is_key_down(PILOT_DELETE_VIRTUAL_KEY)) &&
	    g_pilot_data.name[0] != '\0') {
		XVT_LOG_DEBUG("pilot.delete_asked from_button=%d",
			      selected_index != 0);
		xvt_frontend_action_trigger(XVT_ACTION_OWNER_PILOT, 2, 1);
		frontend_dialog_show_confirm_dialog(
			frontend_string_get(FRONTSTR_527_ARE_YOU_SURE_YOU_WANT),
			frontend_string_get(FRONTSTR_528_TO_DELETE_THIS_PILOT),
			NULL, frontend_string_get(FRONTSTR_529_YES),
			frontend_string_get(FRONTSTR_530_NO));
		return 1;
	}

	return 1;
}

/* Draws up to 13 pilot names of g_pilot_list_display_names from first_visible_index
 * in bounds, 15 pixels apart, the current pilot's name (compared case-blind) in
 * yellow and the row under the cursor filled translucent green. Returns the
 * clicked row's pilot index plus 1 on a left or right click, else 0; also 0,
 * drawing nothing, when g_pilot_file_list or g_pilot_list_display_names is NULL. */
// FUNCTION: XVT 0x4BEF80
int pilot_record_draw_pilot_list(const struct RECT *bounds,
				 int first_visible_index)
{
	if (g_pilot_file_list == NULL) {
		return 0;
	}

	struct RECT rect;
	frontend_draw_rect_copy(&rect, bounds);
	int mouse_x;
	int mouse_y;
	frontend_cursor_get_pos(&mouse_x, &mouse_y);
	rect.bottom = rect.top + 14;
	int count = g_pilot_file_list->count;
	int selected_index = 0;
	struct RECT previous_clip_rect;
	if (g_pilot_list_display_names != NULL) {
		int first_index = first_visible_index;
		int pilot_index = first_index;
		if (pilot_index < first_index + 13) {
			int display_name_index = first_index;
			do {
				if (pilot_index >= count) {
					break;
				}
				if (frontend_draw_point_in_rect(&rect, mouse_x,
								mouse_y)) {
					frontend_draw_fill_rect_translucent(
						&rect, 0, 0, g_color_green);
					if (frontend_mouse_get_left_click() ||
					    frontend_mouse_get_right_click()) {
						selected_index =
							pilot_index + 1;
					}
				}
				frontend_display_get_screen_clip_rect(
					&previous_clip_rect);
				frontend_display_set_screen_clip_rect640x480(
					&rect);
				if (strcasecmp(g_pilot_list_display_names
						       [display_name_index],
					       g_pilot_data.name) == 0) {
					frontend_text_draw_aligned_in_rect(
						12,
						g_pilot_list_display_names
							[display_name_index],
						&rect, 0, 1, g_color_yellow);
				} else {
					frontend_text_draw_aligned_in_rect(
						12,
						g_pilot_list_display_names
							[display_name_index],
						&rect, 0, 1, 0xFFFF);
				}
				++pilot_index;
				++display_name_index;
				frontend_display_set_screen_clip_rect640x480(
					&previous_clip_rect);
				frontend_draw_rect_offset_xy(&rect, 0, 15);
			} while (pilot_index < first_index + 13);
		}
	}

	return selected_index;
}

/* Rebuilds the roster: frees g_pilot_list_display_names and g_pilot_file_list, lists
 * the *.plt files of the base game's install folder into g_pilot_file_list,
 * sorted, and reads the first 12 bytes of each, the pilot's name, into a
 * 14-byte slot of a new g_pilot_list_display_names. Stores the position of the
 * pilot named like g_pilot_data.name, compared case-blind, in *selected_index,
 * which it leaves alone when there is none. Returns 1, or 0 when the file list
 * cannot be built, there are no pilots, or, in the modern build, the names
 * cannot be allocated. A file that does not open is skipped, so the names after
 * it no longer sit at their files' positions in g_pilot_file_list. */
// FUNCTION: XVT 0x4BF100
int pilot_record_rebuild_pilot_list(int *selected_index)
{
	if (g_pilot_list_display_names != NULL) {
		free(g_pilot_list_display_names);
		g_pilot_list_display_names = NULL;
	}
	if (g_pilot_file_list != NULL) {
		frontend_file_list_free(g_pilot_file_list);
		g_pilot_file_list = NULL;
	}
	file_change_to_base_game_install_path();
	g_pilot_file_list = frontend_file_list_build_sorted("*.plt");
	file_change_to_install_path();
	if (g_pilot_file_list == NULL) {
		XVT_LOG_WARN("pilot.list_failed by=\"roster\" kind=\"record\"");
		return 0;
	}

	struct frontend_file_list_node *node = g_pilot_file_list->head;
	if (g_pilot_file_list->count > 0) {
		g_pilot_list_display_names = (char (*)[14])malloc(
			sizeof(*g_pilot_list_display_names) *
			g_pilot_file_list->count);
		if (!g_pilot_list_display_names) {
			XVT_LOG_ERROR("pilot.names_unallocated count=%d",
				      g_pilot_file_list->count);
			return 0;
		}
		memset(g_pilot_list_display_names, 0,
		       sizeof(*g_pilot_list_display_names) *
			       g_pilot_file_list->count);
	} else {
		g_pilot_list_display_names = NULL;
		XVT_LOG_DEBUG("pilot.list_rebuilt count=%d names=%d",
			      g_pilot_file_list->count, 0);
	}
	if (g_pilot_list_display_names != NULL) {
		int pilot_index = 0;
		if (node != NULL) {
			int display_offset = 0;
			do {
				xvt_file *stream = file_open(node->path, "rb");

				if (stream != NULL) {
					file_read_bytes(
						stream,
						(char *)g_pilot_list_display_names +
							display_offset,
						12);
					((char *)g_pilot_list_display_names)
						[display_offset + 12] = '\0';
					if (strcasecmp(
						    (char *)g_pilot_list_display_names +
							    display_offset,
						    g_pilot_data.name) == 0) {
						*selected_index = pilot_index;
					}
					file_close(stream);
					display_offset += 14;
					++pilot_index;
				} else {
					XVT_LOG_WARN(
						"pilot.list_file_unreadable read=%d",
						pilot_index);
				}
				node = node->next;
			} while (node != NULL);
		}
		XVT_LOG_DEBUG("pilot.list_rebuilt count=%d names=%d",
			      g_pilot_file_list->count, pilot_index);
		return 1;
	}

	return 0;
}

/* Draws the pilot statistics page for the current faction's career,
 * g_pilot_data.faction_statistics[current_faction_id].stats: total score, rating
 * and, below Jedi Master, the promotion percentage; then, in columns for
 * exercise, melee and combat (mission types 0, 1 and 2), kills, player and
 * non-player kills, assists, hidden cargo found, laser and warhead accuracy,
 * kills by victim rank and by craft type, averages per mission, and losses by
 * cause and to players by rank. 21 rows show from
 * g_pilot_statistics_scroll_offset, with a scroll bar when there are more. When
 * g_pilot_record_pages_need_rebuild is set it first clears it, scrolls to row 0,
 * counts the rows into g_pilot_record_page_row_count and sums the g_pilotStats
 * totals and flags. Returns 0, having drawn only the title, when no pilot is
 * loaded (empty name); else 1. */
// FUNCTION: XVT 0x4C0D70
int pilot_record_draw_pilot_statistics_page(void)
{
	enum {
		MISSION_TYPE_COUNT = 3,
		CRAFT_TYPE_COUNT = 100,
		PLAYER_RATING_COUNT = 25,
		AI_RATING_COUNT = 6,
		VISIBLE_ROW_COUNT = 21,
		ROW_HEIGHT = 15,
		TEXT_X = 88,
		EXERCISE_X = 218,
		MELEE_X = 288,
		COMBAT_X = 358,
		FIRST_ROW_Y = 111,
		TEXT_FONT_SIZE = 12,
		TITLE_FONT_SIZE = 15,
		TEXT_COLOR_WHITE = 0xFFFF,
		SCROLLBAR_PAGE_STEP = 5,
		SCROLLBAR_CONTROL_ID = 3,
		PILOT_RATING_STRING_BASE = 122,
		CRAFT_NAME_STRING_BASE = 21,
	};

	struct RECT rect;

	frontend_draw_rect_assign(&rect, 84, 90, 434, 106);
	if (g_pilot_data.name[0] == '\0') {
		sprintf(g_frontend_scratch_buffer, "%s",
			frontend_string_get(FRONTSTR_007_PILOT_STATISTICS));
	} else {
		sprintf(g_frontend_scratch_buffer, "%s: %c%s %c%s",
			frontend_string_get(FRONTSTR_007_PILOT_STATISTICS), 6,
			g_pilot_data.rating_name, 4, g_pilot_data.name);
	}
	frontend_text_draw_centered(TITLE_FONT_SIZE, g_frontend_scratch_buffer,
				    &rect, TEXT_COLOR_WHITE);
	if (g_pilot_data.name[0] == '\0') {
		return 0;
	}

	int mission_type;
	int craft_type;
	int rating;
	if (g_pilot_record_pages_need_rebuild != 0) {
		g_pilot_statistics_scroll_offset = 0;
		g_pilot_record_pages_need_rebuild = 0;
		g_pilot_record_page_row_count = 25;
		if ((unsigned int)g_pilot_data.rating <
		    PILOT_RATING_JEDI_MASTER) {
			g_pilot_record_page_row_count = 26;
		}

		g_pilot_stats_losses_to_non_players[0] = 0;
		g_pilot_stats_losses_to_players[0] = 0;
		g_pilot_stats_losses_to_non_players[1] = 0;
		g_pilot_stats_losses_to_players[1] = 0;
		g_pilot_stats_losses_to_non_players[2] = 0;
		g_pilot_stats_losses_to_players[2] = 0;
		g_pilot_stats_non_player_kills_shared[0] = 0;
		g_pilot_stats_non_player_kills_shared[1] = 0;
		g_pilot_stats_non_player_kills[0] = 0;
		g_pilot_stats_non_player_kills_shared[2] = 0;
		g_pilot_stats_non_player_kills[1] = 0;
		g_pilot_stats_non_player_kills[2] = 0;
		g_pilot_stats_player_kills_shared[0] = 0;
		g_pilot_stats_player_kills[0] = 0;
		g_pilot_stats_player_kills_shared[1] = 0;
		g_pilot_stats_player_kills[1] = 0;
		g_pilot_stats_player_kills_shared[2] = 0;
		g_pilot_stats_player_kills[2] = 0;
		g_pilot_stats_total_kills_shared[0] = 0;
		g_pilot_stats_total_kills_shared[1] = 0;
		g_pilot_stats_total_kills_shared[2] = 0;
		g_pilot_stats_assists[0] = 0;
		g_pilot_stats_assists[1] = 0;
		g_pilot_stats_assists[2] = 0;

		g_pilot_stats_has_craft_kills_by_type = 0;
		for (craft_type = 0; craft_type < CRAFT_TYPE_COUNT;
		     ++craft_type) {
			g_pilot_stats_row_has_data = 0;
			for (mission_type = 0;
			     mission_type < MISSION_TYPE_COUNT;
			     ++mission_type) {
				if (g_pilot_data.faction_statistics
						    [g_pilot_data
							     .current_faction_id]
							    .stats
							    .kills_per_craft_per_mt
								    [mission_type]
								    [craft_type] !=
					    0 ||
				    g_pilot_data.faction_statistics
						    [g_pilot_data
							     .current_faction_id]
							    .stats
							    .kills_shared_per_craft_per_mt
								    [mission_type]
								    [craft_type] !=
					    0) {
					g_pilot_stats_row_has_data = 1;
					g_pilot_stats_has_craft_kills_by_type =
						1;
				}
			}
			if (g_pilot_stats_row_has_data != 0) {
				++g_pilot_record_page_row_count;
			}
		}
		if (g_pilot_stats_has_craft_kills_by_type != 0) {
			g_pilot_record_page_row_count += 2;
		}

		g_pilot_stats_has_player_kills_by_rating = 0;
		for (rating = 0; rating < PLAYER_RATING_COUNT; ++rating) {
			g_pilot_stats_row_has_data = 0;
			for (mission_type = 0;
			     mission_type < MISSION_TYPE_COUNT;
			     ++mission_type) {
				if (g_pilot_data.faction_statistics
						    [g_pilot_data
							     .current_faction_id]
							    .stats
							    .kills_full_on_player_rating_per_mt
								    [mission_type]
								    [rating] !=
					    0 ||
				    g_pilot_data.faction_statistics
						    [g_pilot_data
							     .current_faction_id]
							    .stats
							    .kills_shared_on_player_rating_per_mt
								    [mission_type]
								    [rating] !=
					    0) {
					g_pilot_stats_row_has_data = 1;
					g_pilot_stats_has_player_kills_by_rating =
						1;
				}
			}
			if (g_pilot_stats_row_has_data != 0) {
				++g_pilot_record_page_row_count;
			}
		}
		if (g_pilot_stats_has_player_kills_by_rating != 0) {
			g_pilot_record_page_row_count += 2;
		}

		g_pilot_stats_has_losses_to_players_by_rank = 0;
		for (rating = 0; rating < PLAYER_RATING_COUNT; ++rating) {
			g_pilot_stats_row_has_data = 0;
			for (mission_type = 0;
			     mission_type < MISSION_TYPE_COUNT;
			     ++mission_type) {
				if (g_pilot_data
					    .faction_statistics
						    [g_pilot_data
							     .current_faction_id]
					    .stats
					    .killed_by_player_rating_per_mt
						    [mission_type][rating] !=
				    0) {
					g_pilot_stats_row_has_data = 1;
					g_pilot_stats_has_losses_to_players_by_rank =
						1;
				}
			}
			if (g_pilot_stats_row_has_data != 0) {
				++g_pilot_record_page_row_count;
			}
		}
		if (g_pilot_stats_has_losses_to_players_by_rank != 0) {
			g_pilot_record_page_row_count += 2;
		}

		for (mission_type = 0; mission_type < MISSION_TYPE_COUNT;
		     ++mission_type) {
			for (craft_type = 0; craft_type < CRAFT_TYPE_COUNT;
			     ++craft_type) {
				g_pilot_stats_assists[mission_type] +=
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.stats
						.kills_assists_per_craft_per_mt
							[mission_type]
							[craft_type];
				g_pilot_stats_total_kills_shared
					[mission_type] +=
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.stats
						.kills_shared_per_craft_per_mt
							[mission_type]
							[craft_type];
			}
		}
		for (mission_type = 0; mission_type < MISSION_TYPE_COUNT;
		     ++mission_type) {
			for (rating = 0; rating < PLAYER_RATING_COUNT;
			     ++rating) {
				g_pilot_stats_player_kills[mission_type] +=
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.stats
						.kills_full_on_player_rating_per_mt
							[mission_type][rating];
				g_pilot_stats_player_kills_shared
					[mission_type] +=
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.stats
						.kills_shared_on_player_rating_per_mt
							[mission_type][rating];
			}
		}
		for (mission_type = 0; mission_type < MISSION_TYPE_COUNT;
		     ++mission_type) {
			for (rating = 0; rating < AI_RATING_COUNT; ++rating) {
				g_pilot_stats_non_player_kills[mission_type] +=
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.stats
						.kills_full_on_ai_rating_per_mt
							[mission_type][rating];
				g_pilot_stats_non_player_kills_shared
					[mission_type] +=
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.stats
						.kills_shared_on_ai_rating_per_mt
							[mission_type][rating];
			}
		}
		for (mission_type = 0; mission_type < MISSION_TYPE_COUNT;
		     ++mission_type) {
			for (rating = 0; rating < PLAYER_RATING_COUNT;
			     ++rating) {
				g_pilot_stats_losses_to_players[mission_type] +=
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.stats
						.killed_by_player_rating_per_mt
							[mission_type][rating];
			}
		}
		for (mission_type = 0; mission_type < MISSION_TYPE_COUNT;
		     ++mission_type) {
			for (rating = 0; rating < AI_RATING_COUNT; ++rating) {
				g_pilot_stats_losses_to_non_players
					[mission_type] +=
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.stats
						.killed_by_ai_rating_per_mt
							[mission_type][rating];
			}
		}
		XVT_LOG_DEBUG(
			"pilot.stats_counted faction=%d rows=%d craft=%d players=%d losses=%d",
			g_pilot_data.current_faction_id,
			g_pilot_record_page_row_count,
			g_pilot_stats_has_craft_kills_by_type,
			g_pilot_stats_has_player_kills_by_rating,
			g_pilot_stats_has_losses_to_players_by_rank);
	}

	frontend_draw_rect_assign(&rect, 425, 107, 434, 433);
	if (g_pilot_record_page_row_count > VISIBLE_ROW_COUNT) {
		g_pilot_statistics_scroll_offset = frontend_scrollbar_draw(
			&rect, g_pilot_statistics_scroll_offset,
			g_pilot_record_page_row_count, 0, SCROLLBAR_PAGE_STEP,
			g_color_navy, SCROLLBAR_CONTROL_ID);
	}
	int row = 0;
	int y = FIRST_ROW_Y;

	if (row >= g_pilot_statistics_scroll_offset &&
	    row - g_pilot_statistics_scroll_offset < VISIBLE_ROW_COUNT) {
		sprintf(g_frontend_scratch_buffer, "%c%s: %c%d", 4,
			frontend_string_get(FRONTSTR_011_TOTAL_SCORE), 1,
			g_pilot_data.total_score);
		frontend_text_draw(TEXT_FONT_SIZE, g_frontend_scratch_buffer,
				   TEXT_X, y, TEXT_COLOR_WHITE);
		y += ROW_HEIGHT;
	}
	++row;
	if (row >= g_pilot_statistics_scroll_offset &&
	    row - g_pilot_statistics_scroll_offset < VISIBLE_ROW_COUNT) {
		sprintf(g_frontend_scratch_buffer, "%c%s %c%s", 4,
			frontend_string_get(FRONTSTR_348_PILOT_RATING), 6,
			frontend_string_get((
				frontend_string_id)(g_pilot_data.rating +
						    PILOT_RATING_STRING_BASE)));
		frontend_text_draw(TEXT_FONT_SIZE, g_frontend_scratch_buffer,
				   TEXT_X, y, TEXT_COLOR_WHITE);
		y += ROW_HEIGHT;
	}
	++row;
	if ((unsigned int)g_pilot_data.rating < PILOT_RATING_JEDI_MASTER) {
		if (row >= g_pilot_statistics_scroll_offset &&
		    row - g_pilot_statistics_scroll_offset <
			    VISIBLE_ROW_COUNT) {
			sprintf(g_frontend_scratch_buffer, "%c%s %c%d%%", 4,
				frontend_string_get(
					FRONTSTR_349_ADVANCEMENT_TOWARD_NEXT_PROMOTION),
				1, g_pilot_data.next_promotion_percent);
			frontend_text_draw(TEXT_FONT_SIZE,
					   g_frontend_scratch_buffer, TEXT_X, y,
					   TEXT_COLOR_WHITE);
			y += ROW_HEIGHT;
		}
		++row;
	}
	if (row >= g_pilot_statistics_scroll_offset &&
	    row - g_pilot_statistics_scroll_offset < VISIBLE_ROW_COUNT) {
		y += ROW_HEIGHT;
	}
	++row;

	if (row >= g_pilot_statistics_scroll_offset &&
	    row - g_pilot_statistics_scroll_offset < VISIBLE_ROW_COUNT) {
		frontend_text_draw(
			TEXT_FONT_SIZE,
			frontend_string_get(FRONTSTR_350_SUMMARY_OF_KILLS),
			TEXT_X, y, g_color_yellow);
		frontend_text_draw(TEXT_FONT_SIZE,
				   frontend_string_get(FRONTSTR_189_EXERCISE),
				   EXERCISE_X, y, g_color_yellow);
		frontend_text_draw(TEXT_FONT_SIZE,
				   frontend_string_get(FRONTSTR_013_MELEE),
				   MELEE_X, y, g_color_yellow);
		frontend_text_draw(TEXT_FONT_SIZE,
				   frontend_string_get(FRONTSTR_014_COMBAT),
				   COMBAT_X, y, g_color_yellow);
		y += ROW_HEIGHT;
	}
	++row;

	if (row >= g_pilot_statistics_scroll_offset &&
	    row - g_pilot_statistics_scroll_offset < VISIBLE_ROW_COUNT) {
		frontend_text_draw(
			TEXT_FONT_SIZE,
			frontend_string_get(FRONTSTR_297_TOTAL_KILLS), TEXT_X,
			y, g_color_yellow);
		if (g_pilot_data.faction_statistics[g_pilot_data
							    .current_faction_id]
				    .stats.total_kills_per_mt[0] != 0 ||
		    g_pilot_stats_total_kills_shared[0] != 0) {
			sprintf(g_frontend_scratch_buffer, "%d (%d)",
				g_pilot_data
					.faction_statistics
						[g_pilot_data
							 .current_faction_id]
					.stats.total_kills_per_mt[0],
				g_pilot_stats_total_kills_shared[0]);
		} else {
			sprintf(g_frontend_scratch_buffer, "----");
		}
		frontend_text_draw(TEXT_FONT_SIZE, g_frontend_scratch_buffer,
				   EXERCISE_X, y, TEXT_COLOR_WHITE);
		if (g_pilot_data.faction_statistics[g_pilot_data
							    .current_faction_id]
				    .stats.total_kills_per_mt[1] != 0 ||
		    g_pilot_stats_total_kills_shared[1] != 0) {
			sprintf(g_frontend_scratch_buffer, "%d (%d)",
				g_pilot_data
					.faction_statistics
						[g_pilot_data
							 .current_faction_id]
					.stats.total_kills_per_mt[1],
				g_pilot_stats_total_kills_shared[1]);
		} else {
			sprintf(g_frontend_scratch_buffer, "----");
		}
		frontend_text_draw(TEXT_FONT_SIZE, g_frontend_scratch_buffer,
				   MELEE_X, y, TEXT_COLOR_WHITE);
		if (g_pilot_data.faction_statistics[g_pilot_data
							    .current_faction_id]
				    .stats.total_kills_per_mt[2] != 0 ||
		    g_pilot_stats_total_kills_shared[2] != 0) {
			sprintf(g_frontend_scratch_buffer, "%d (%d)",
				g_pilot_data
					.faction_statistics
						[g_pilot_data
							 .current_faction_id]
					.stats.total_kills_per_mt[2],
				g_pilot_stats_total_kills_shared[2]);
		} else {
			sprintf(g_frontend_scratch_buffer, "----");
		}
		frontend_text_draw(TEXT_FONT_SIZE, g_frontend_scratch_buffer,
				   COMBAT_X, y, TEXT_COLOR_WHITE);
		y += ROW_HEIGHT;
	}
	++row;

	if (row >= g_pilot_statistics_scroll_offset &&
	    row - g_pilot_statistics_scroll_offset < VISIBLE_ROW_COUNT) {
		frontend_text_draw(
			TEXT_FONT_SIZE,
			frontend_string_get(FRONTSTR_351_PLAYER_KILLS), TEXT_X,
			y, g_color_yellow);
		sprintf(g_frontend_scratch_buffer, "----");
		frontend_text_draw(TEXT_FONT_SIZE, g_frontend_scratch_buffer,
				   EXERCISE_X, y, TEXT_COLOR_WHITE);
		if (g_pilot_stats_player_kills[1] != 0 ||
		    g_pilot_stats_player_kills_shared[1] != 0) {
			sprintf(g_frontend_scratch_buffer, "%d (%d)",
				g_pilot_stats_player_kills[1],
				g_pilot_stats_player_kills_shared[1]);
		} else {
			sprintf(g_frontend_scratch_buffer, "----");
		}
		frontend_text_draw(TEXT_FONT_SIZE, g_frontend_scratch_buffer,
				   MELEE_X, y, TEXT_COLOR_WHITE);
		if (g_pilot_stats_player_kills[2] != 0 ||
		    g_pilot_stats_player_kills_shared[2] != 0) {
			sprintf(g_frontend_scratch_buffer, "%d (%d)",
				g_pilot_stats_player_kills[2],
				g_pilot_stats_player_kills_shared[2]);
		} else {
			sprintf(g_frontend_scratch_buffer, "----");
		}
		frontend_text_draw(TEXT_FONT_SIZE, g_frontend_scratch_buffer,
				   COMBAT_X, y, TEXT_COLOR_WHITE);
		y += ROW_HEIGHT;
	}
	++row;

	if (row >= g_pilot_statistics_scroll_offset &&
	    row - g_pilot_statistics_scroll_offset < VISIBLE_ROW_COUNT) {
		frontend_text_draw(
			TEXT_FONT_SIZE,
			frontend_string_get(FRONTSTR_352_NON_PLAYER_KILLS),
			TEXT_X, y, g_color_yellow);
		if (g_pilot_stats_non_player_kills[0] != 0 ||
		    g_pilot_stats_non_player_kills_shared[0] != 0) {
			sprintf(g_frontend_scratch_buffer, "%d (%d)",
				g_pilot_stats_non_player_kills[0],
				g_pilot_stats_non_player_kills_shared[0]);
		} else {
			sprintf(g_frontend_scratch_buffer, "----");
		}
		frontend_text_draw(TEXT_FONT_SIZE, g_frontend_scratch_buffer,
				   EXERCISE_X, y, TEXT_COLOR_WHITE);
		if (g_pilot_stats_non_player_kills[1] != 0 ||
		    g_pilot_stats_non_player_kills_shared[1] != 0) {
			sprintf(g_frontend_scratch_buffer, "%d (%d)",
				g_pilot_stats_non_player_kills[1],
				g_pilot_stats_non_player_kills_shared[1]);
		} else {
			sprintf(g_frontend_scratch_buffer, "----");
		}
		frontend_text_draw(TEXT_FONT_SIZE, g_frontend_scratch_buffer,
				   MELEE_X, y, TEXT_COLOR_WHITE);
		if (g_pilot_stats_non_player_kills[2] != 0 ||
		    g_pilot_stats_non_player_kills_shared[2] != 0) {
			sprintf(g_frontend_scratch_buffer, "%d (%d)",
				g_pilot_stats_non_player_kills[2],
				g_pilot_stats_non_player_kills_shared[2]);
		} else {
			sprintf(g_frontend_scratch_buffer, "----");
		}
		frontend_text_draw(TEXT_FONT_SIZE, g_frontend_scratch_buffer,
				   COMBAT_X, y, TEXT_COLOR_WHITE);
		y += ROW_HEIGHT;
	}
	++row;

	if (row >= g_pilot_statistics_scroll_offset &&
	    row - g_pilot_statistics_scroll_offset < VISIBLE_ROW_COUNT) {
		frontend_text_draw(TEXT_FONT_SIZE,
				   frontend_string_get(FRONTSTR_353_ASSISTS),
				   TEXT_X, y, g_color_yellow);
		if (g_pilot_stats_assists[0] == 0) {
			sprintf(g_frontend_scratch_buffer, "----");
		} else {
			sprintf(g_frontend_scratch_buffer, "%d",
				g_pilot_stats_assists[0]);
		}
		frontend_text_draw(TEXT_FONT_SIZE, g_frontend_scratch_buffer,
				   EXERCISE_X, y, TEXT_COLOR_WHITE);
		if (g_pilot_stats_assists[1] == 0) {
			sprintf(g_frontend_scratch_buffer, "----");
		} else {
			sprintf(g_frontend_scratch_buffer, "%d",
				g_pilot_stats_assists[1]);
		}
		frontend_text_draw(TEXT_FONT_SIZE, g_frontend_scratch_buffer,
				   MELEE_X, y, TEXT_COLOR_WHITE);
		if (g_pilot_stats_assists[2] == 0) {
			sprintf(g_frontend_scratch_buffer, "----");
		} else {
			sprintf(g_frontend_scratch_buffer, "%d",
				g_pilot_stats_assists[2]);
		}
		frontend_text_draw(TEXT_FONT_SIZE, g_frontend_scratch_buffer,
				   COMBAT_X, y, TEXT_COLOR_WHITE);
		y += ROW_HEIGHT;
	}
	++row;

	int value;
	if (row >= g_pilot_statistics_scroll_offset &&
	    row - g_pilot_statistics_scroll_offset < VISIBLE_ROW_COUNT) {
		frontend_text_draw(
			TEXT_FONT_SIZE,
			frontend_string_get(FRONTSTR_354_HIDDEN_CARGO_FOUND),
			TEXT_X, y, g_color_yellow);
		value = g_pilot_data
				.faction_statistics[g_pilot_data
							    .current_faction_id]
				.stats.num_special_inspected_per_mt[0];
		if (value == 0) {
			sprintf(g_frontend_scratch_buffer, "----");
		} else {
			sprintf(g_frontend_scratch_buffer, "%d", value);
		}
		frontend_text_draw(TEXT_FONT_SIZE, g_frontend_scratch_buffer,
				   EXERCISE_X, y, TEXT_COLOR_WHITE);
		value = g_pilot_data
				.faction_statistics[g_pilot_data
							    .current_faction_id]
				.stats.num_special_inspected_per_mt[1];
		if (value == 0) {
			sprintf(g_frontend_scratch_buffer, "----");
		} else {
			sprintf(g_frontend_scratch_buffer, "%d", value);
		}
		frontend_text_draw(TEXT_FONT_SIZE, g_frontend_scratch_buffer,
				   MELEE_X, y, TEXT_COLOR_WHITE);
		value = g_pilot_data
				.faction_statistics[g_pilot_data
							    .current_faction_id]
				.stats.num_special_inspected_per_mt[2];
		if (value == 0) {
			sprintf(g_frontend_scratch_buffer, "----");
		} else {
			sprintf(g_frontend_scratch_buffer, "%d", value);
		}
		frontend_text_draw(TEXT_FONT_SIZE, g_frontend_scratch_buffer,
				   COMBAT_X, y, TEXT_COLOR_WHITE);
		y += ROW_HEIGHT;
	}
	++row;

	unsigned int shots_fired;
	if (row >= g_pilot_statistics_scroll_offset &&
	    row - g_pilot_statistics_scroll_offset < VISIBLE_ROW_COUNT) {
		frontend_text_draw(
			TEXT_FONT_SIZE,
			frontend_string_get(FRONTSTR_355_LASER_ACCURACY),
			TEXT_X, y, g_color_yellow);
		shots_fired =
			(unsigned int)g_pilot_data
				.faction_statistics[g_pilot_data
							    .current_faction_id]
				.stats.energy_fired_per_mt[0];
		if (shots_fired == 0) {
			sprintf(g_frontend_scratch_buffer, "----");
		} else {
			sprintf(g_frontend_scratch_buffer, "%d%%",
				100 *
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.stats.energy_hits_per_mt[0] /
					shots_fired);
		}
		frontend_text_draw(TEXT_FONT_SIZE, g_frontend_scratch_buffer,
				   EXERCISE_X, y, TEXT_COLOR_WHITE);
		shots_fired =
			(unsigned int)g_pilot_data
				.faction_statistics[g_pilot_data
							    .current_faction_id]
				.stats.energy_fired_per_mt[1];
		if (shots_fired == 0) {
			sprintf(g_frontend_scratch_buffer, "----");
		} else {
			sprintf(g_frontend_scratch_buffer, "%d%%",
				100 *
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.stats.energy_hits_per_mt[1] /
					shots_fired);
		}
		frontend_text_draw(TEXT_FONT_SIZE, g_frontend_scratch_buffer,
				   MELEE_X, y, TEXT_COLOR_WHITE);
		shots_fired =
			(unsigned int)g_pilot_data
				.faction_statistics[g_pilot_data
							    .current_faction_id]
				.stats.energy_fired_per_mt[2];
		if (shots_fired == 0) {
			sprintf(g_frontend_scratch_buffer, "----");
		} else {
			sprintf(g_frontend_scratch_buffer, "%d%%",
				100 *
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.stats.energy_hits_per_mt[2] /
					shots_fired);
		}
		frontend_text_draw(TEXT_FONT_SIZE, g_frontend_scratch_buffer,
				   COMBAT_X, y, TEXT_COLOR_WHITE);
		y += ROW_HEIGHT;
	}
	++row;

	if (row >= g_pilot_statistics_scroll_offset &&
	    row - g_pilot_statistics_scroll_offset < VISIBLE_ROW_COUNT) {
		frontend_text_draw(
			TEXT_FONT_SIZE,
			frontend_string_get(FRONTSTR_356_WARHEAD_ACCURACY),
			TEXT_X, y, g_color_yellow);
		shots_fired =
			(unsigned int)g_pilot_data
				.faction_statistics[g_pilot_data
							    .current_faction_id]
				.stats.warheads_fired_per_mt[0];
		if (shots_fired == 0) {
			sprintf(g_frontend_scratch_buffer, "----");
		} else {
			sprintf(g_frontend_scratch_buffer, "%d%%",
				100 *
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.stats.warheads_hits_per_mt[0] /
					shots_fired);
		}
		frontend_text_draw(TEXT_FONT_SIZE, g_frontend_scratch_buffer,
				   EXERCISE_X, y, TEXT_COLOR_WHITE);
		shots_fired =
			(unsigned int)g_pilot_data
				.faction_statistics[g_pilot_data
							    .current_faction_id]
				.stats.warheads_fired_per_mt[1];
		if (shots_fired == 0) {
			sprintf(g_frontend_scratch_buffer, "----");
		} else {
			sprintf(g_frontend_scratch_buffer, "%d%%",
				100 *
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.stats.warheads_hits_per_mt[1] /
					shots_fired);
		}
		frontend_text_draw(TEXT_FONT_SIZE, g_frontend_scratch_buffer,
				   MELEE_X, y, TEXT_COLOR_WHITE);
		shots_fired =
			(unsigned int)g_pilot_data
				.faction_statistics[g_pilot_data
							    .current_faction_id]
				.stats.warheads_fired_per_mt[2];
		if (shots_fired == 0) {
			sprintf(g_frontend_scratch_buffer, "----");
		} else {
			sprintf(g_frontend_scratch_buffer, "%d%%",
				100 *
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.stats.warheads_hits_per_mt[2] /
					shots_fired);
		}
		frontend_text_draw(TEXT_FONT_SIZE, g_frontend_scratch_buffer,
				   COMBAT_X, y, TEXT_COLOR_WHITE);
		y += ROW_HEIGHT;
	}
	++row;

	if (g_pilot_stats_has_player_kills_by_rating != 0) {
		if (row >= g_pilot_statistics_scroll_offset &&
		    row - g_pilot_statistics_scroll_offset <
			    VISIBLE_ROW_COUNT) {
			y += ROW_HEIGHT;
		}
		++row;
		if (row >= g_pilot_statistics_scroll_offset &&
		    row - g_pilot_statistics_scroll_offset <
			    VISIBLE_ROW_COUNT) {
			frontend_text_draw(
				TEXT_FONT_SIZE,
				frontend_string_get(
					FRONTSTR_357_PLAYER_KILLS_BY_RANK),
				TEXT_X, y, g_color_yellow);
			frontend_text_draw(
				TEXT_FONT_SIZE,
				frontend_string_get(FRONTSTR_013_MELEE),
				MELEE_X, y, g_color_yellow);
			frontend_text_draw(
				TEXT_FONT_SIZE,
				frontend_string_get(FRONTSTR_014_COMBAT),
				COMBAT_X, y, g_color_yellow);
			y += ROW_HEIGHT;
		}
		++row;
	}
	for (rating = PLAYER_RATING_COUNT - 1; rating >= 0; --rating) {
		if (g_pilot_data.faction_statistics[g_pilot_data
							    .current_faction_id]
				    .stats.kills_full_on_player_rating_per_mt
					    [1][rating] != 0 ||
		    g_pilot_data.faction_statistics[g_pilot_data
							    .current_faction_id]
				    .stats.kills_shared_on_player_rating_per_mt
					    [1][rating] != 0 ||
		    g_pilot_data.faction_statistics[g_pilot_data
							    .current_faction_id]
				    .stats.kills_full_on_player_rating_per_mt
					    [2][rating] != 0 ||
		    g_pilot_data.faction_statistics[g_pilot_data
							    .current_faction_id]
				    .stats.kills_shared_on_player_rating_per_mt
					    [2][rating] != 0) {
			if (row >= g_pilot_statistics_scroll_offset &&
			    row - g_pilot_statistics_scroll_offset <
				    VISIBLE_ROW_COUNT) {
				sprintf(g_frontend_scratch_buffer, "%c%s", 6,
					frontend_string_get((
						frontend_string_id)(rating +
								    PILOT_RATING_STRING_BASE)));
				frontend_text_draw(TEXT_FONT_SIZE,
						   g_frontend_scratch_buffer,
						   TEXT_X, y, g_color_yellow);
				if (g_pilot_data.faction_statistics
						    [g_pilot_data
							     .current_faction_id]
							    .stats
							    .kills_full_on_player_rating_per_mt
								    [1]
								    [rating] ==
					    0 &&
				    g_pilot_data.faction_statistics
						    [g_pilot_data
							     .current_faction_id]
							    .stats
							    .kills_shared_on_player_rating_per_mt
								    [1]
								    [rating] ==
					    0) {
					sprintf(g_frontend_scratch_buffer,
						"----");
				} else {
					sprintf(g_frontend_scratch_buffer,
						"%d (%d)",
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.stats
							.kills_full_on_player_rating_per_mt
								[1][rating],
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.stats
							.kills_shared_on_player_rating_per_mt
								[1][rating]);
				}
				frontend_text_draw(TEXT_FONT_SIZE,
						   g_frontend_scratch_buffer,
						   MELEE_X, y,
						   TEXT_COLOR_WHITE);
				if (g_pilot_data.faction_statistics
						    [g_pilot_data
							     .current_faction_id]
							    .stats
							    .kills_full_on_player_rating_per_mt
								    [2]
								    [rating] ==
					    0 &&
				    g_pilot_data.faction_statistics
						    [g_pilot_data
							     .current_faction_id]
							    .stats
							    .kills_shared_on_player_rating_per_mt
								    [2]
								    [rating] ==
					    0) {
					sprintf(g_frontend_scratch_buffer,
						"----");
				} else {
					sprintf(g_frontend_scratch_buffer,
						"%d (%d)",
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.stats
							.kills_full_on_player_rating_per_mt
								[2][rating],
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.stats
							.kills_shared_on_player_rating_per_mt
								[2][rating]);
				}
				frontend_text_draw(TEXT_FONT_SIZE,
						   g_frontend_scratch_buffer,
						   COMBAT_X, y,
						   TEXT_COLOR_WHITE);
				y += ROW_HEIGHT;
			}
			++row;
		}
	}

	if (g_pilot_stats_has_craft_kills_by_type != 0) {
		if (row >= g_pilot_statistics_scroll_offset &&
		    row - g_pilot_statistics_scroll_offset <
			    VISIBLE_ROW_COUNT) {
			y += ROW_HEIGHT;
		}
		++row;
		if (row >= g_pilot_statistics_scroll_offset &&
		    row - g_pilot_statistics_scroll_offset <
			    VISIBLE_ROW_COUNT) {
			frontend_text_draw(
				TEXT_FONT_SIZE,
				frontend_string_get(
					FRONTSTR_358_CRAFT_KILLS_BY_TYPE),
				TEXT_X, y, g_color_yellow);
			frontend_text_draw(
				TEXT_FONT_SIZE,
				frontend_string_get(FRONTSTR_189_EXERCISE),
				EXERCISE_X, y, g_color_yellow);
			frontend_text_draw(
				TEXT_FONT_SIZE,
				frontend_string_get(FRONTSTR_013_MELEE),
				MELEE_X, y, g_color_yellow);
			frontend_text_draw(
				TEXT_FONT_SIZE,
				frontend_string_get(FRONTSTR_014_COMBAT),
				COMBAT_X, y, g_color_yellow);
			y += ROW_HEIGHT;
		}
		++row;
	}
	for (craft_type = 0; craft_type < CRAFT_TYPE_COUNT; ++craft_type) {
		g_pilot_stats_row_has_data = 0;
		for (mission_type = 0; mission_type < MISSION_TYPE_COUNT;
		     ++mission_type) {
			if (g_pilot_data.faction_statistics
					    [g_pilot_data.current_faction_id]
						    .stats
						    .kills_per_craft_per_mt
							    [mission_type]
							    [craft_type] != 0 ||
			    g_pilot_data
					    .faction_statistics
						    [g_pilot_data
							     .current_faction_id]
					    .stats.kills_shared_per_craft_per_mt
						    [mission_type]
						    [craft_type] != 0) {
				g_pilot_stats_row_has_data = 1;
			}
		}
		if (g_pilot_stats_row_has_data != 0) {
			if (row >= g_pilot_statistics_scroll_offset &&
			    row - g_pilot_statistics_scroll_offset <
				    VISIBLE_ROW_COUNT) {
				frontend_text_draw(
					TEXT_FONT_SIZE,
					frontend_string_get((
						frontend_string_id)(craft_type +
								    CRAFT_NAME_STRING_BASE)),
					TEXT_X, y, g_color_red);
				int shared_value =
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.stats
						.kills_shared_per_craft_per_mt
							[0][craft_type];
				value = g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.stats.kills_per_craft_per_mt
							[0][craft_type];
				if (value == 0 && shared_value == 0) {
					sprintf(g_frontend_scratch_buffer,
						"----");
				} else {
					sprintf(g_frontend_scratch_buffer,
						"%d (%d)", value, shared_value);
				}
				frontend_text_draw(TEXT_FONT_SIZE,
						   g_frontend_scratch_buffer,
						   EXERCISE_X, y,
						   TEXT_COLOR_WHITE);
				shared_value =
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.stats
						.kills_shared_per_craft_per_mt
							[1][craft_type];
				value = g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.stats.kills_per_craft_per_mt
							[1][craft_type];
				if (value == 0 && shared_value == 0) {
					sprintf(g_frontend_scratch_buffer,
						"----");
				} else {
					sprintf(g_frontend_scratch_buffer,
						"%d (%d)", value, shared_value);
				}
				frontend_text_draw(TEXT_FONT_SIZE,
						   g_frontend_scratch_buffer,
						   MELEE_X, y,
						   TEXT_COLOR_WHITE);
				shared_value =
					g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.stats
						.kills_shared_per_craft_per_mt
							[2][craft_type];
				value = g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.stats.kills_per_craft_per_mt
							[2][craft_type];
				if (value == 0 && shared_value == 0) {
					sprintf(g_frontend_scratch_buffer,
						"----");
				} else {
					sprintf(g_frontend_scratch_buffer,
						"%d (%d)", value, shared_value);
				}
				frontend_text_draw(TEXT_FONT_SIZE,
						   g_frontend_scratch_buffer,
						   COMBAT_X, y,
						   TEXT_COLOR_WHITE);
				y += ROW_HEIGHT;
			}
			++row;
		}
	}

	if (row >= g_pilot_statistics_scroll_offset &&
	    row - g_pilot_statistics_scroll_offset < VISIBLE_ROW_COUNT) {
		y += ROW_HEIGHT;
	}
	++row;
	if (row >= g_pilot_statistics_scroll_offset &&
	    row - g_pilot_statistics_scroll_offset < VISIBLE_ROW_COUNT) {
		frontend_text_draw(
			TEXT_FONT_SIZE,
			frontend_string_get(FRONTSTR_359_AVGS_PER_MISSION),
			TEXT_X, y, g_color_yellow);
		frontend_text_draw(TEXT_FONT_SIZE,
				   frontend_string_get(FRONTSTR_189_EXERCISE),
				   EXERCISE_X, y, g_color_yellow);
		frontend_text_draw(TEXT_FONT_SIZE,
				   frontend_string_get(FRONTSTR_013_MELEE),
				   MELEE_X, y, g_color_yellow);
		frontend_text_draw(TEXT_FONT_SIZE,
				   frontend_string_get(FRONTSTR_014_COMBAT),
				   COMBAT_X, y, g_color_yellow);
		y += ROW_HEIGHT;
	}
	++row;
	int exercise_mission_count =
		g_pilot_data.faction_statistics[g_pilot_data.current_faction_id]
			.stats.standalone_missions_played_per_mt[0] +
		g_pilot_data.faction_statistics[g_pilot_data.current_faction_id]
			.stats.sequence_missions_played_per_mt[0];
	if (exercise_mission_count == 0) {
		exercise_mission_count = 1;
	}
	int melee_mission_count =
		g_pilot_data.faction_statistics[g_pilot_data.current_faction_id]
			.stats.standalone_missions_played_per_mt[1] +
		g_pilot_data.faction_statistics[g_pilot_data.current_faction_id]
			.stats.sequence_missions_played_per_mt[1];
	if (melee_mission_count == 0) {
		melee_mission_count = 1;
	}
	int combat_mission_count =
		g_pilot_data.faction_statistics[g_pilot_data.current_faction_id]
			.stats.standalone_missions_played_per_mt[2] +
		g_pilot_data.faction_statistics[g_pilot_data.current_faction_id]
			.stats.sequence_missions_played_per_mt[2];
	if (combat_mission_count == 0) {
		combat_mission_count = 1;
	}

	float shared_average;
	if (row >= g_pilot_statistics_scroll_offset &&
	    row - g_pilot_statistics_scroll_offset < VISIBLE_ROW_COUNT) {
		frontend_text_draw(
			TEXT_FONT_SIZE,
			frontend_string_get(FRONTSTR_297_TOTAL_KILLS), TEXT_X,
			y, g_color_yellow);
		shared_average = (double)g_pilot_stats_total_kills_shared[0] /
				 (double)exercise_mission_count;
		if ((double)(unsigned int)g_pilot_data
					    .faction_statistics
						    [g_pilot_data
							     .current_faction_id]
					    .stats.total_kills_per_mt[0] /
				    (double)exercise_mission_count !=
			    0.0 ||
		    shared_average != 0.0f) {
			sprintf(g_frontend_scratch_buffer, "%.1f (%.1f)",
				(double)(unsigned int)g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.stats.total_kills_per_mt[0] /
					(double)exercise_mission_count,
				(double)shared_average);
		} else {
			sprintf(g_frontend_scratch_buffer, "----");
		}
		frontend_text_draw(TEXT_FONT_SIZE, g_frontend_scratch_buffer,
				   EXERCISE_X, y, TEXT_COLOR_WHITE);
		shared_average = (double)g_pilot_stats_total_kills_shared[1] /
				 (double)melee_mission_count;
		if ((double)(unsigned int)g_pilot_data
					    .faction_statistics
						    [g_pilot_data
							     .current_faction_id]
					    .stats.total_kills_per_mt[1] /
				    (double)melee_mission_count !=
			    0.0 ||
		    shared_average != 0.0f) {
			sprintf(g_frontend_scratch_buffer, "%.1f (%.1f)",
				(double)(unsigned int)g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.stats.total_kills_per_mt[1] /
					(double)melee_mission_count,
				(double)shared_average);
		} else {
			sprintf(g_frontend_scratch_buffer, "----");
		}
		frontend_text_draw(TEXT_FONT_SIZE, g_frontend_scratch_buffer,
				   MELEE_X, y, TEXT_COLOR_WHITE);
		shared_average = (double)g_pilot_stats_total_kills_shared[2] /
				 (double)combat_mission_count;
		if ((double)(unsigned int)g_pilot_data
					    .faction_statistics
						    [g_pilot_data
							     .current_faction_id]
					    .stats.total_kills_per_mt[2] /
				    (double)combat_mission_count !=
			    0.0 ||
		    shared_average != 0.0f) {
			sprintf(g_frontend_scratch_buffer, "%.1f (%.1f)",
				(double)(unsigned int)g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.stats.total_kills_per_mt[2] /
					(double)combat_mission_count,
				(double)shared_average);
		} else {
			sprintf(g_frontend_scratch_buffer, "----");
		}
		frontend_text_draw(TEXT_FONT_SIZE, g_frontend_scratch_buffer,
				   COMBAT_X, y, TEXT_COLOR_WHITE);
		y += ROW_HEIGHT;
	}
	++row;
	if (row >= g_pilot_statistics_scroll_offset &&
	    row - g_pilot_statistics_scroll_offset < VISIBLE_ROW_COUNT) {
		frontend_text_draw(
			TEXT_FONT_SIZE,
			frontend_string_get(FRONTSTR_351_PLAYER_KILLS), TEXT_X,
			y, g_color_yellow);
		sprintf(g_frontend_scratch_buffer, "----");
		frontend_text_draw(TEXT_FONT_SIZE, g_frontend_scratch_buffer,
				   EXERCISE_X, y, TEXT_COLOR_WHITE);
		shared_average = (double)g_pilot_stats_player_kills_shared[1] /
				 (double)melee_mission_count;
		if ((double)g_pilot_stats_player_kills[1] /
				    (double)melee_mission_count !=
			    0.0 ||
		    shared_average != 0.0f) {
			sprintf(g_frontend_scratch_buffer, "%.1f (%.1f)",
				(double)g_pilot_stats_player_kills[1] /
					(double)melee_mission_count,
				(double)shared_average);
		} else {
			sprintf(g_frontend_scratch_buffer, "----");
		}
		frontend_text_draw(TEXT_FONT_SIZE, g_frontend_scratch_buffer,
				   MELEE_X, y, TEXT_COLOR_WHITE);
		shared_average = (double)g_pilot_stats_player_kills_shared[2] /
				 (double)combat_mission_count;
		if ((double)g_pilot_stats_player_kills[2] /
				    (double)combat_mission_count !=
			    0.0 ||
		    shared_average != 0.0f) {
			sprintf(g_frontend_scratch_buffer, "%.1f (%.1f)",
				(double)g_pilot_stats_player_kills[2] /
					(double)combat_mission_count,
				(double)shared_average);
		} else {
			sprintf(g_frontend_scratch_buffer, "----");
		}
		frontend_text_draw(TEXT_FONT_SIZE, g_frontend_scratch_buffer,
				   COMBAT_X, y, TEXT_COLOR_WHITE);
		y += ROW_HEIGHT;
	}
	++row;
	if (row >= g_pilot_statistics_scroll_offset &&
	    row - g_pilot_statistics_scroll_offset < VISIBLE_ROW_COUNT) {
		frontend_text_draw(
			TEXT_FONT_SIZE,
			frontend_string_get(FRONTSTR_352_NON_PLAYER_KILLS),
			TEXT_X, y, g_color_yellow);
		shared_average =
			(double)g_pilot_stats_non_player_kills_shared[0] /
			(double)exercise_mission_count;
		if ((double)g_pilot_stats_non_player_kills[0] /
				    (double)exercise_mission_count !=
			    0.0 ||
		    shared_average != 0.0f) {
			sprintf(g_frontend_scratch_buffer, "%.1f (%.1f)",
				(double)g_pilot_stats_non_player_kills[0] /
					(double)exercise_mission_count,
				(double)shared_average);
		} else {
			sprintf(g_frontend_scratch_buffer, "----");
		}
		frontend_text_draw(TEXT_FONT_SIZE, g_frontend_scratch_buffer,
				   EXERCISE_X, y, TEXT_COLOR_WHITE);
		shared_average =
			(double)g_pilot_stats_non_player_kills_shared[1] /
			(double)melee_mission_count;
		if ((double)g_pilot_stats_non_player_kills[1] /
				    (double)melee_mission_count !=
			    0.0 ||
		    shared_average != 0.0f) {
			sprintf(g_frontend_scratch_buffer, "%.1f (%.1f)",
				(double)g_pilot_stats_non_player_kills[1] /
					(double)melee_mission_count,
				(double)shared_average);
		} else {
			sprintf(g_frontend_scratch_buffer, "----");
		}
		frontend_text_draw(TEXT_FONT_SIZE, g_frontend_scratch_buffer,
				   MELEE_X, y, TEXT_COLOR_WHITE);
		shared_average =
			(double)g_pilot_stats_non_player_kills_shared[2] /
			(double)combat_mission_count;
		if ((double)g_pilot_stats_non_player_kills[2] /
				    (double)combat_mission_count !=
			    0.0 ||
		    shared_average != 0.0f) {
			sprintf(g_frontend_scratch_buffer, "%.1f (%.1f)",
				(double)g_pilot_stats_non_player_kills[2] /
					(double)combat_mission_count,
				(double)shared_average);
		} else {
			sprintf(g_frontend_scratch_buffer, "----");
		}
		frontend_text_draw(TEXT_FONT_SIZE, g_frontend_scratch_buffer,
				   COMBAT_X, y, TEXT_COLOR_WHITE);
		y += ROW_HEIGHT;
	}
	++row;
	if (row >= g_pilot_statistics_scroll_offset &&
	    row - g_pilot_statistics_scroll_offset < VISIBLE_ROW_COUNT) {
		frontend_text_draw(TEXT_FONT_SIZE,
				   frontend_string_get(FRONTSTR_353_ASSISTS),
				   TEXT_X, y, g_color_yellow);
		if ((double)g_pilot_stats_assists[0] /
			    (double)exercise_mission_count ==
		    0.0) {
			sprintf(g_frontend_scratch_buffer, "----");
		} else {
			sprintf(g_frontend_scratch_buffer, "%.1f",
				(double)g_pilot_stats_assists[0] /
					(double)exercise_mission_count);
		}
		frontend_text_draw(TEXT_FONT_SIZE, g_frontend_scratch_buffer,
				   EXERCISE_X, y, TEXT_COLOR_WHITE);
		if ((double)g_pilot_stats_assists[1] /
			    (double)melee_mission_count ==
		    0.0) {
			sprintf(g_frontend_scratch_buffer, "----");
		} else {
			sprintf(g_frontend_scratch_buffer, "%.1f",
				(double)g_pilot_stats_assists[1] /
					(double)melee_mission_count);
		}
		frontend_text_draw(TEXT_FONT_SIZE, g_frontend_scratch_buffer,
				   MELEE_X, y, TEXT_COLOR_WHITE);
		if ((double)g_pilot_stats_assists[2] /
			    (double)combat_mission_count ==
		    0.0) {
			sprintf(g_frontend_scratch_buffer, "----");
		} else {
			sprintf(g_frontend_scratch_buffer, "%.1f",
				(double)g_pilot_stats_assists[2] /
					(double)combat_mission_count);
		}
		frontend_text_draw(TEXT_FONT_SIZE, g_frontend_scratch_buffer,
				   COMBAT_X, y, TEXT_COLOR_WHITE);
		y += ROW_HEIGHT;
	}
	++row;

	if (row >= g_pilot_statistics_scroll_offset &&
	    row - g_pilot_statistics_scroll_offset < VISIBLE_ROW_COUNT) {
		y += ROW_HEIGHT;
	}
	++row;
	if (row >= g_pilot_statistics_scroll_offset &&
	    row - g_pilot_statistics_scroll_offset < VISIBLE_ROW_COUNT) {
		frontend_text_draw(
			TEXT_FONT_SIZE,
			frontend_string_get(FRONTSTR_360_TOTAL_LOSSES), TEXT_X,
			y, g_color_yellow);
		frontend_text_draw(TEXT_FONT_SIZE,
				   frontend_string_get(FRONTSTR_189_EXERCISE),
				   EXERCISE_X, y, g_color_yellow);
		frontend_text_draw(TEXT_FONT_SIZE,
				   frontend_string_get(FRONTSTR_013_MELEE),
				   MELEE_X, y, g_color_yellow);
		frontend_text_draw(TEXT_FONT_SIZE,
				   frontend_string_get(FRONTSTR_014_COMBAT),
				   COMBAT_X, y, g_color_yellow);
		y += ROW_HEIGHT;
	}
	++row;

	if (row >= g_pilot_statistics_scroll_offset &&
	    row - g_pilot_statistics_scroll_offset < VISIBLE_ROW_COUNT) {
		frontend_text_draw(
			TEXT_FONT_SIZE,
			frontend_string_get(FRONTSTR_361_TOTAL_CRAFT_LOSSES),
			TEXT_X, y, g_color_yellow);
		if (g_pilot_data
			    .faction_statistics[g_pilot_data.current_faction_id]
			    .stats.total_craft_losses_per_mt[0] == 0) {
			sprintf(g_frontend_scratch_buffer, "----");
		} else {
			sprintf(g_frontend_scratch_buffer, "%d",
				g_pilot_data
					.faction_statistics
						[g_pilot_data
							 .current_faction_id]
					.stats.total_craft_losses_per_mt[0]);
		}
		frontend_text_draw(TEXT_FONT_SIZE, g_frontend_scratch_buffer,
				   EXERCISE_X, y, TEXT_COLOR_WHITE);
		value = g_pilot_data
				.faction_statistics[g_pilot_data
							    .current_faction_id]
				.stats.total_craft_losses_per_mt[1];
		if (value == 0) {
			sprintf(g_frontend_scratch_buffer, "----");
		} else {
			sprintf(g_frontend_scratch_buffer, "%d", value);
		}
		frontend_text_draw(TEXT_FONT_SIZE, g_frontend_scratch_buffer,
				   MELEE_X, y, TEXT_COLOR_WHITE);
		value = g_pilot_data
				.faction_statistics[g_pilot_data
							    .current_faction_id]
				.stats.total_craft_losses_per_mt[2];
		if (value == 0) {
			sprintf(g_frontend_scratch_buffer, "----");
		} else {
			sprintf(g_frontend_scratch_buffer, "%d", value);
		}
		frontend_text_draw(TEXT_FONT_SIZE, g_frontend_scratch_buffer,
				   COMBAT_X, y, TEXT_COLOR_WHITE);
		y += ROW_HEIGHT;
	}
	++row;
	if (row >= g_pilot_statistics_scroll_offset &&
	    row - g_pilot_statistics_scroll_offset < VISIBLE_ROW_COUNT) {
		frontend_text_draw(
			TEXT_FONT_SIZE,
			frontend_string_get(FRONTSTR_362_TO_PLAYER_PILOTS),
			TEXT_X, y, g_color_yellow);
		if (g_pilot_stats_losses_to_players[0] == 0) {
			sprintf(g_frontend_scratch_buffer, "----");
		} else {
			sprintf(g_frontend_scratch_buffer, "%d",
				g_pilot_stats_losses_to_players[0]);
		}
		frontend_text_draw(TEXT_FONT_SIZE, g_frontend_scratch_buffer,
				   EXERCISE_X, y, TEXT_COLOR_WHITE);
		if (g_pilot_stats_losses_to_players[1] == 0) {
			sprintf(g_frontend_scratch_buffer, "----");
		} else {
			sprintf(g_frontend_scratch_buffer, "%d",
				g_pilot_stats_losses_to_players[1]);
		}
		frontend_text_draw(TEXT_FONT_SIZE, g_frontend_scratch_buffer,
				   MELEE_X, y, TEXT_COLOR_WHITE);
		if (g_pilot_stats_losses_to_players[2] == 0) {
			sprintf(g_frontend_scratch_buffer, "----");
		} else {
			sprintf(g_frontend_scratch_buffer, "%d",
				g_pilot_stats_losses_to_players[2]);
		}
		frontend_text_draw(TEXT_FONT_SIZE, g_frontend_scratch_buffer,
				   COMBAT_X, y, TEXT_COLOR_WHITE);
		y += ROW_HEIGHT;
	}
	++row;
	if (row >= g_pilot_statistics_scroll_offset &&
	    row - g_pilot_statistics_scroll_offset < VISIBLE_ROW_COUNT) {
		frontend_text_draw(
			TEXT_FONT_SIZE,
			frontend_string_get(FRONTSTR_363_TO_NON_PLAYER_PILOTS),
			TEXT_X, y, g_color_yellow);
		if (g_pilot_stats_losses_to_non_players[0] == 0) {
			sprintf(g_frontend_scratch_buffer, "----");
		} else {
			sprintf(g_frontend_scratch_buffer, "%d",
				g_pilot_stats_losses_to_non_players[0]);
		}
		frontend_text_draw(TEXT_FONT_SIZE, g_frontend_scratch_buffer,
				   EXERCISE_X, y, TEXT_COLOR_WHITE);
		if (g_pilot_stats_losses_to_non_players[1] == 0) {
			sprintf(g_frontend_scratch_buffer, "----");
		} else {
			sprintf(g_frontend_scratch_buffer, "%d",
				g_pilot_stats_losses_to_non_players[1]);
		}
		frontend_text_draw(TEXT_FONT_SIZE, g_frontend_scratch_buffer,
				   MELEE_X, y, TEXT_COLOR_WHITE);
		if (g_pilot_stats_losses_to_non_players[2] == 0) {
			sprintf(g_frontend_scratch_buffer, "----");
		} else {
			sprintf(g_frontend_scratch_buffer, "%d",
				g_pilot_stats_losses_to_non_players[2]);
		}
		frontend_text_draw(TEXT_FONT_SIZE, g_frontend_scratch_buffer,
				   COMBAT_X, y, TEXT_COLOR_WHITE);
		y += ROW_HEIGHT;
	}
	++row;

	if (row >= g_pilot_statistics_scroll_offset &&
	    row - g_pilot_statistics_scroll_offset < VISIBLE_ROW_COUNT) {
		frontend_text_draw(
			TEXT_FONT_SIZE,
			frontend_string_get(FRONTSTR_364_TO_STARSHIPS), TEXT_X,
			y, g_color_yellow);
		value = g_pilot_data
				.faction_statistics[g_pilot_data
							    .current_faction_id]
				.stats.losses_by_starships_per_mt[0];
		if (value == 0) {
			sprintf(g_frontend_scratch_buffer, "----");
		} else {
			sprintf(g_frontend_scratch_buffer, "%d", value);
		}
		frontend_text_draw(TEXT_FONT_SIZE, g_frontend_scratch_buffer,
				   EXERCISE_X, y, TEXT_COLOR_WHITE);
		value = g_pilot_data
				.faction_statistics[g_pilot_data
							    .current_faction_id]
				.stats.losses_by_starships_per_mt[1];
		if (value == 0) {
			sprintf(g_frontend_scratch_buffer, "----");
		} else {
			sprintf(g_frontend_scratch_buffer, "%d", value);
		}
		frontend_text_draw(TEXT_FONT_SIZE, g_frontend_scratch_buffer,
				   MELEE_X, y, TEXT_COLOR_WHITE);
		value = g_pilot_data
				.faction_statistics[g_pilot_data
							    .current_faction_id]
				.stats.losses_by_starships_per_mt[2];
		if (value == 0) {
			sprintf(g_frontend_scratch_buffer, "----");
		} else {
			sprintf(g_frontend_scratch_buffer, "%d", value);
		}
		frontend_text_draw(TEXT_FONT_SIZE, g_frontend_scratch_buffer,
				   COMBAT_X, y, TEXT_COLOR_WHITE);
		y += ROW_HEIGHT;
	}
	++row;
	if (row >= g_pilot_statistics_scroll_offset &&
	    row - g_pilot_statistics_scroll_offset < VISIBLE_ROW_COUNT) {
		frontend_text_draw(TEXT_FONT_SIZE,
				   frontend_string_get(FRONTSTR_365_TO_MINES),
				   TEXT_X, y, g_color_yellow);
		value = g_pilot_data
				.faction_statistics[g_pilot_data
							    .current_faction_id]
				.stats.losses_by_mines_per_mt[0];
		if (value == 0) {
			sprintf(g_frontend_scratch_buffer, "----");
		} else {
			sprintf(g_frontend_scratch_buffer, "%d", value);
		}
		frontend_text_draw(TEXT_FONT_SIZE, g_frontend_scratch_buffer,
				   EXERCISE_X, y, TEXT_COLOR_WHITE);
		value = g_pilot_data
				.faction_statistics[g_pilot_data
							    .current_faction_id]
				.stats.losses_by_mines_per_mt[1];
		if (value == 0) {
			sprintf(g_frontend_scratch_buffer, "----");
		} else {
			sprintf(g_frontend_scratch_buffer, "%d", value);
		}
		frontend_text_draw(TEXT_FONT_SIZE, g_frontend_scratch_buffer,
				   MELEE_X, y, TEXT_COLOR_WHITE);
		value = g_pilot_data
				.faction_statistics[g_pilot_data
							    .current_faction_id]
				.stats.losses_by_mines_per_mt[2];
		if (value == 0) {
			sprintf(g_frontend_scratch_buffer, "----");
		} else {
			sprintf(g_frontend_scratch_buffer, "%d", value);
		}
		frontend_text_draw(TEXT_FONT_SIZE, g_frontend_scratch_buffer,
				   COMBAT_X, y, TEXT_COLOR_WHITE);
		y += ROW_HEIGHT;
	}
	++row;
	if (row >= g_pilot_statistics_scroll_offset &&
	    row - g_pilot_statistics_scroll_offset < VISIBLE_ROW_COUNT) {
		frontend_text_draw(
			TEXT_FONT_SIZE,
			frontend_string_get(FRONTSTR_366_FROM_COLLISIONS),
			TEXT_X, y, g_color_yellow);
		value = g_pilot_data
				.faction_statistics[g_pilot_data
							    .current_faction_id]
				.stats.losses_by_collisions_per_mt[0];
		if (value == 0) {
			sprintf(g_frontend_scratch_buffer, "----");
		} else {
			sprintf(g_frontend_scratch_buffer, "%d", value);
		}
		frontend_text_draw(TEXT_FONT_SIZE, g_frontend_scratch_buffer,
				   EXERCISE_X, y, TEXT_COLOR_WHITE);
		value = g_pilot_data
				.faction_statistics[g_pilot_data
							    .current_faction_id]
				.stats.losses_by_collisions_per_mt[1];
		if (value == 0) {
			sprintf(g_frontend_scratch_buffer, "----");
		} else {
			sprintf(g_frontend_scratch_buffer, "%d", value);
		}
		frontend_text_draw(TEXT_FONT_SIZE, g_frontend_scratch_buffer,
				   MELEE_X, y, TEXT_COLOR_WHITE);
		value = g_pilot_data
				.faction_statistics[g_pilot_data
							    .current_faction_id]
				.stats.losses_by_collisions_per_mt[2];
		if (value == 0) {
			sprintf(g_frontend_scratch_buffer, "----");
		} else {
			sprintf(g_frontend_scratch_buffer, "%d", value);
		}
		frontend_text_draw(TEXT_FONT_SIZE, g_frontend_scratch_buffer,
				   COMBAT_X, y, TEXT_COLOR_WHITE);
		y += ROW_HEIGHT;
	}
	++row;

	if (g_pilot_stats_has_losses_to_players_by_rank != 0) {
		if (row >= g_pilot_statistics_scroll_offset &&
		    row - g_pilot_statistics_scroll_offset <
			    VISIBLE_ROW_COUNT) {
			y += ROW_HEIGHT;
		}
		++row;
		if (row >= g_pilot_statistics_scroll_offset &&
		    row - g_pilot_statistics_scroll_offset <
			    VISIBLE_ROW_COUNT) {
			frontend_text_draw(
				TEXT_FONT_SIZE,
				frontend_string_get(
					FRONTSTR_534_LOSSES_TO_PLAYERS_BY_RANK),
				TEXT_X, y, g_color_yellow);
			frontend_text_draw(
				TEXT_FONT_SIZE,
				frontend_string_get(FRONTSTR_013_MELEE),
				MELEE_X, y, g_color_yellow);
			frontend_text_draw(
				TEXT_FONT_SIZE,
				frontend_string_get(FRONTSTR_014_COMBAT),
				COMBAT_X, y, g_color_yellow);
			y += ROW_HEIGHT;
		}
		++row;
	}
	for (rating = PLAYER_RATING_COUNT - 1; rating >= 0; --rating) {
		if (g_pilot_data.faction_statistics[g_pilot_data
							    .current_faction_id]
				    .stats
				    .killed_by_player_rating_per_mt[1]
								   [rating] !=
			    0 ||
		    g_pilot_data.faction_statistics[g_pilot_data
							    .current_faction_id]
				    .stats
				    .killed_by_player_rating_per_mt[2]
								   [rating] !=
			    0) {
			if (row >= g_pilot_statistics_scroll_offset &&
			    row - g_pilot_statistics_scroll_offset <
				    VISIBLE_ROW_COUNT) {
				sprintf(g_frontend_scratch_buffer, "%c%s", 6,
					frontend_string_get((
						frontend_string_id)(rating +
								    PILOT_RATING_STRING_BASE)));
				frontend_text_draw(TEXT_FONT_SIZE,
						   g_frontend_scratch_buffer,
						   TEXT_X, y, g_color_yellow);
				value = g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.stats
						.killed_by_player_rating_per_mt
							[1][rating];
				if (value == 0) {
					sprintf(g_frontend_scratch_buffer,
						"----");
				} else {
					sprintf(g_frontend_scratch_buffer, "%d",
						value);
				}
				frontend_text_draw(TEXT_FONT_SIZE,
						   g_frontend_scratch_buffer,
						   MELEE_X, y,
						   TEXT_COLOR_WHITE);
				value = g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.stats
						.killed_by_player_rating_per_mt
							[2][rating];
				if (value == 0) {
					sprintf(g_frontend_scratch_buffer,
						"----");
				} else {
					sprintf(g_frontend_scratch_buffer, "%d",
						value);
				}
				frontend_text_draw(TEXT_FONT_SIZE,
						   g_frontend_scratch_buffer,
						   COMBAT_X, y,
						   TEXT_COLOR_WHITE);
				y += ROW_HEIGHT;
			}
			++row;
		}
	}
	return 1;
}

/* Draws the mission achievements page: for single player, then multiplayer, the
 * training, melee, tournament, combat, battle and campaign histories of the
 * current faction, one row per mission flown or attempted with its description,
 * cut short with dots when too long, its best score and its best time, best
 * finish, best victory margin or campaign progress; a campaign's flown missions
 * follow it in gray. A second pass draws, beside each row with an award level,
 * its sprite and tooltip: a citation for training, combat and campaign
 * missions, a medal for melees, tournaments and battles. 21 rows show from
 * g_pilot_achievements_scroll_offset, with a scroll bar when there are more. When
 * g_pilot_record_pages_need_rebuild is set it first frees and reloads every
 * directory's mission list into the g_pilotRecord lists and
 * g_battle_mission_list, the multiplayer training, combat and campaign lists with
 * g_frontend_mission_session_mode set to NET_HOST and then left at NONE, cuts each
 * description at its last '(' in place, clears the flag and counts each
 * section's rows into the history counts and g_pilot_record_page_row_count. Returns
 * 0, having drawn only the title, when no pilot is loaded; else 1. */
// FUNCTION: XVT 0x4C3590
int pilot_record_draw_mission_achievements_page(void)
{
	struct RECT rect;

	frontend_draw_rect_assign(&rect, MISSION_ACHIEVEMENT_TITLE_LEFT,
				  MISSION_ACHIEVEMENT_TITLE_TOP,
				  MISSION_ACHIEVEMENT_TITLE_RIGHT,
				  MISSION_ACHIEVEMENT_TITLE_BOTTOM);
	if (g_pilot_data.name[0] == '\0') {
		sprintf(g_frontend_scratch_buffer, "%s",
			frontend_string_get(FRONTSTR_010_MISSION_ACHIEVEMENTS));
	} else {
		sprintf(g_frontend_scratch_buffer, "%s: %c%s %c%s",
			frontend_string_get(FRONTSTR_010_MISSION_ACHIEVEMENTS),
			MISSION_ACHIEVEMENT_RATING_TEXT_CODE,
			g_pilot_data.rating_name,
			MISSION_ACHIEVEMENT_NAME_TEXT_CODE, g_pilot_data.name);
	}
	frontend_text_draw_centered(MISSION_ACHIEVEMENT_TITLE_FONT_SIZE,
				    g_frontend_scratch_buffer, &rect,
				    MISSION_ACHIEVEMENT_TEXT_COLOR);
	if (g_pilot_data.name[0] == '\0') {
		return 0;
	}

	int list_index;
	int award_id;
	int row;
	int has_previous;
	if (g_pilot_record_pages_need_rebuild != 0) {
		if (g_pilot_record_singleplayer_training_mission_list != NULL) {
			free(g_pilot_record_singleplayer_training_mission_list);
			g_pilot_record_singleplayer_training_mission_list =
				NULL;
		}
		if (g_pilot_record_multiplayer_training_mission_list != NULL) {
			free(g_pilot_record_multiplayer_training_mission_list);
			g_pilot_record_multiplayer_training_mission_list = NULL;
		}
		if (g_pilot_record_melee_mission_list != NULL) {
			free(g_pilot_record_melee_mission_list);
			g_pilot_record_melee_mission_list = NULL;
		}
		if (g_pilot_record_tournament_mission_list != NULL) {
			free(g_pilot_record_tournament_mission_list);
			g_pilot_record_tournament_mission_list = NULL;
		}
		if (g_pilot_record_singleplayer_combat_mission_list != NULL) {
			free(g_pilot_record_singleplayer_combat_mission_list);
			g_pilot_record_singleplayer_combat_mission_list = NULL;
		}
		if (g_pilot_record_multiplayer_combat_mission_list != NULL) {
			free(g_pilot_record_multiplayer_combat_mission_list);
			g_pilot_record_multiplayer_combat_mission_list = NULL;
		}
		if (g_battle_mission_list != NULL) {
			free(g_battle_mission_list);
			g_battle_mission_list = NULL;
		}
		if (g_pilot_record_singleplayer_campaign_mission_list != NULL) {
			free(g_pilot_record_singleplayer_campaign_mission_list);
			g_pilot_record_singleplayer_campaign_mission_list =
				NULL;
		}
		if (g_pilot_record_multiplayer_campaign_mission_list != NULL) {
			free(g_pilot_record_multiplayer_campaign_mission_list);
			g_pilot_record_multiplayer_campaign_mission_list = NULL;
		}
		mission_setup_load_mission_list(
			MISSION_DIRECTORY_TRAINING_EXERCISES);
		g_pilot_record_singleplayer_training_mission_list =
			g_mission_list;
		g_pilot_record_singleplayer_training_mission_count =
			g_mission_count;
		g_mission_list = NULL;
		for (list_index = 0;
		     list_index <
		     g_pilot_record_singleplayer_training_mission_count;
		     ++list_index) {
			/* award_id is a character position in these trim loops:
			 * each mission description is cut at its last '('. */
			award_id =
				(int)strlen(
					g_pilot_record_singleplayer_training_mission_list
						[list_index]
							.description) -
				1;
			for (; award_id > 0; --award_id) {
				if (g_pilot_record_singleplayer_training_mission_list
					    [list_index]
						    .description[award_id] ==
				    '(') {
					g_pilot_record_singleplayer_training_mission_list
						[list_index]
							.description[award_id] =
						'\0';
					break;
				}
			}
		}
		g_frontend_mission_session_mode =
			FRONTEND_MISSION_SESSION_NET_HOST;
		mission_setup_load_mission_list(
			MISSION_DIRECTORY_TRAINING_EXERCISES);
		g_frontend_mission_session_mode = FRONTEND_MISSION_SESSION_NONE;
		g_pilot_record_multiplayer_training_mission_list =
			g_mission_list;
		g_pilot_record_multiplayer_training_mission_count =
			g_mission_count;
		g_mission_list = NULL;
		for (list_index = 0;
		     list_index <
		     g_pilot_record_multiplayer_training_mission_count;
		     ++list_index) {
			award_id =
				(int)strlen(
					g_pilot_record_multiplayer_training_mission_list
						[list_index]
							.description) -
				1;
			for (; award_id > 0; --award_id) {
				if (g_pilot_record_multiplayer_training_mission_list
					    [list_index]
						    .description[award_id] ==
				    '(') {
					g_pilot_record_multiplayer_training_mission_list
						[list_index]
							.description[award_id] =
						'\0';
					break;
				}
			}
		}
		mission_setup_load_mission_list(MISSION_DIRECTORY_MELEES);
		g_pilot_record_melee_mission_list = g_mission_list;
		g_pilot_record_melee_mission_count = g_mission_count;
		g_mission_list = NULL;
		for (list_index = 0;
		     list_index < g_pilot_record_melee_mission_count;
		     ++list_index) {
			award_id = (int)strlen(g_pilot_record_melee_mission_list
						       [list_index]
							       .description) -
				   1;
			for (; award_id > 0; --award_id) {
				if (g_pilot_record_melee_mission_list
					    [list_index]
						    .description[award_id] ==
				    '(') {
					g_pilot_record_melee_mission_list
						[list_index]
							.description[award_id] =
						'\0';
					break;
				}
			}
		}
		mission_setup_load_mission_list(MISSION_DIRECTORY_TOURNAMENTS);
		g_pilot_record_tournament_mission_list = g_mission_list;
		g_pilot_record_tournament_mission_count = g_mission_count;
		g_mission_list = NULL;
		for (list_index = 0;
		     list_index < g_pilot_record_tournament_mission_count;
		     ++list_index) {
			award_id =
				(int)strlen(
					g_pilot_record_tournament_mission_list
						[list_index]
							.description) -
				1;
			for (; award_id > 0; --award_id) {
				if (g_pilot_record_tournament_mission_list
					    [list_index]
						    .description[award_id] ==
				    '(') {
					g_pilot_record_tournament_mission_list
						[list_index]
							.description[award_id] =
						'\0';
					break;
				}
			}
		}
		mission_setup_load_mission_list(
			MISSION_DIRECTORY_COMBAT_ENGAGEMENTS);
		g_pilot_record_singleplayer_combat_mission_list =
			g_mission_list;
		g_pilot_record_singleplayer_combat_mission_count =
			g_mission_count;
		g_mission_list = NULL;
		for (list_index = 0;
		     list_index <
		     g_pilot_record_singleplayer_combat_mission_count;
		     ++list_index) {
			award_id =
				(int)strlen(
					g_pilot_record_singleplayer_combat_mission_list
						[list_index]
							.description) -
				1;
			for (; award_id > 0; --award_id) {
				if (g_pilot_record_singleplayer_combat_mission_list
					    [list_index]
						    .description[award_id] ==
				    '(') {
					g_pilot_record_singleplayer_combat_mission_list
						[list_index]
							.description[award_id] =
						'\0';
					break;
				}
			}
		}
		g_frontend_mission_session_mode =
			FRONTEND_MISSION_SESSION_NET_HOST;
		mission_setup_load_mission_list(
			MISSION_DIRECTORY_COMBAT_ENGAGEMENTS);
		g_frontend_mission_session_mode = FRONTEND_MISSION_SESSION_NONE;
		g_pilot_record_multiplayer_combat_mission_list = g_mission_list;
		g_pilot_record_multiplayer_combat_mission_count =
			g_mission_count;
		g_mission_list = NULL;
		for (list_index = 0;
		     list_index <
		     g_pilot_record_multiplayer_combat_mission_count;
		     ++list_index) {
			award_id =
				(int)strlen(
					g_pilot_record_multiplayer_combat_mission_list
						[list_index]
							.description) -
				1;
			for (; award_id > 0; --award_id) {
				if (g_pilot_record_multiplayer_combat_mission_list
					    [list_index]
						    .description[award_id] ==
				    '(') {
					g_pilot_record_multiplayer_combat_mission_list
						[list_index]
							.description[award_id] =
						'\0';
					break;
				}
			}
		}
		mission_setup_load_mission_list(MISSION_DIRECTORY_BATTLES);
		g_battle_mission_list = g_mission_list;
		g_battle_mission_list_count = g_mission_count;
		g_mission_list = NULL;
		for (list_index = 0; list_index < g_battle_mission_list_count;
		     ++list_index) {
			award_id = (int)strlen(g_battle_mission_list[list_index]
						       .description) -
				   1;
			for (; award_id > 0; --award_id) {
				if (g_battle_mission_list[list_index]
					    .description[award_id] == '(') {
					g_battle_mission_list[list_index]
						.description[award_id] = '\0';
					break;
				}
			}
		}
		mission_setup_load_mission_list(MISSION_DIRECTORY_CAMPAIGNS);
		g_pilot_record_singleplayer_campaign_mission_list =
			g_mission_list;
		g_pilot_record_singleplayer_campaign_mission_count =
			g_mission_count;
		g_mission_list = NULL;
		for (list_index = 0;
		     list_index <
		     g_pilot_record_singleplayer_campaign_mission_count;
		     ++list_index) {
			award_id =
				(int)strlen(
					g_pilot_record_singleplayer_campaign_mission_list
						[list_index]
							.description) -
				1;
			for (; award_id > 0; --award_id) {
				if (g_pilot_record_singleplayer_campaign_mission_list
					    [list_index]
						    .description[award_id] ==
				    '(') {
					g_pilot_record_singleplayer_campaign_mission_list
						[list_index]
							.description[award_id] =
						'\0';
					break;
				}
			}
		}
		g_frontend_mission_session_mode =
			FRONTEND_MISSION_SESSION_NET_HOST;
		mission_setup_load_mission_list(MISSION_DIRECTORY_CAMPAIGNS);
		g_frontend_mission_session_mode = FRONTEND_MISSION_SESSION_NONE;
		g_pilot_record_multiplayer_campaign_mission_list =
			g_mission_list;
		g_pilot_record_multiplayer_campaign_mission_count =
			g_mission_count;
		g_mission_list = NULL;
		for (list_index = 0;
		     list_index <
		     g_pilot_record_multiplayer_campaign_mission_count;
		     ++list_index) {
			award_id =
				(int)strlen(
					g_pilot_record_multiplayer_campaign_mission_list
						[list_index]
							.description) -
				1;
			for (; award_id > 0; --award_id) {
				if (g_pilot_record_multiplayer_campaign_mission_list
					    [list_index]
						    .description[award_id] ==
				    '(') {
					g_pilot_record_multiplayer_campaign_mission_list
						[list_index]
							.description[award_id] =
						'\0';
					break;
				}
			}
		}
		g_pilot_achievements_scroll_offset = 0;
		g_pilot_record_pages_need_rebuild = 0;
		g_pilot_record_page_row_count = 0;
		g_pilot_sp_campaign_history_row_count = 0;
		g_pilot_sp_battle_history_count = 0;
		g_pilot_sp_tournament_history_count = 0;
		g_pilot_sp_training_history_count = 0;
		g_pilot_sp_combat_history_count = 0;
		g_pilot_sp_melee_history_count = 0;
		g_pilot_mp_campaign_history_row_count = 0;
		g_pilot_mp_battle_history_count = 0;
		g_pilot_mp_tournament_history_count = 0;
		g_pilot_mp_training_history_count = 0;
		g_pilot_mp_combat_history_count = 0;
		g_pilot_mp_melee_history_count = 0;

		for (list_index = 0;
		     list_index <
		     g_pilot_record_singleplayer_training_mission_count;
		     ++list_index) {
			if (g_pilot_data
				    .faction_statistics
					    [g_pilot_data.current_faction_id]
				    .sp_training_missions
					    [g_pilot_record_singleplayer_training_mission_list
						     [list_index]
							     .mission_idx]
				    .number_times_flown != 0) {
				++g_pilot_sp_training_history_count;
			}
		}
		for (list_index = 0;
		     list_index <
		     g_pilot_record_multiplayer_training_mission_count;
		     ++list_index) {
			if (g_pilot_data
				    .faction_statistics
					    [g_pilot_data.current_faction_id]
				    .mp_training_missions
					    [g_pilot_record_multiplayer_training_mission_list
						     [list_index]
							     .mission_idx]
				    .number_times_flown != 0) {
				++g_pilot_mp_training_history_count;
			}
		}
		for (list_index = 0;
		     list_index < g_pilot_record_melee_mission_count;
		     ++list_index) {
			if (g_pilot_data
				    .faction_statistics
					    [g_pilot_data.current_faction_id]
				    .sp_melee_missions
					    [g_pilot_record_melee_mission_list
						     [list_index]
							     .mission_idx]
				    .number_times_flown != 0) {
				++g_pilot_sp_melee_history_count;
			}
			if (g_pilot_data
				    .faction_statistics
					    [g_pilot_data.current_faction_id]
				    .mp_melee_missions
					    [g_pilot_record_melee_mission_list
						     [list_index]
							     .mission_idx]
				    .number_times_flown != 0) {
				++g_pilot_mp_melee_history_count;
			}
		}
		for (list_index = 0;
		     list_index <
		     g_pilot_record_singleplayer_combat_mission_count;
		     ++list_index) {
			if (g_pilot_data
				    .faction_statistics
					    [g_pilot_data.current_faction_id]
				    .sp_combat_missions
					    [g_pilot_record_singleplayer_combat_mission_list
						     [list_index]
							     .mission_idx]
				    .number_times_flown != 0) {
				++g_pilot_sp_combat_history_count;
			}
		}
		for (list_index = 0;
		     list_index <
		     g_pilot_record_multiplayer_combat_mission_count;
		     ++list_index) {
			if (g_pilot_data
				    .faction_statistics
					    [g_pilot_data.current_faction_id]
				    .mp_combat_missions
					    [g_pilot_record_multiplayer_combat_mission_list
						     [list_index]
							     .mission_idx]
				    .number_times_flown != 0) {
				++g_pilot_mp_combat_history_count;
			}
		}
		for (list_index = 0;
		     list_index < g_pilot_record_tournament_mission_count;
		     ++list_index) {
			if (g_pilot_data
				    .faction_statistics
					    [g_pilot_data.current_faction_id]
				    .sp_tournaments
					    [g_pilot_record_tournament_mission_list
						     [list_index]
							     .mission_idx]
				    .attempt_count != 0) {
				++g_pilot_sp_tournament_history_count;
			}
			if (g_pilot_data
				    .faction_statistics
					    [g_pilot_data.current_faction_id]
				    .mp_tournaments
					    [g_pilot_record_tournament_mission_list
						     [list_index]
							     .mission_idx]
				    .attempt_count != 0) {
				++g_pilot_mp_tournament_history_count;
			}
		}
		for (list_index = 0; list_index < g_battle_mission_list_count;
		     ++list_index) {
			if (g_pilot_data
				    .faction_statistics
					    [g_pilot_data.current_faction_id]
				    .sp_battles
					    [g_battle_mission_list[list_index]
						     .mission_idx]
				    .attempt_count != 0) {
				++g_pilot_sp_battle_history_count;
			}
			if (g_pilot_data
				    .faction_statistics
					    [g_pilot_data.current_faction_id]
				    .mp_battles
					    [g_battle_mission_list[list_index]
						     .mission_idx]
				    .attempt_count != 0) {
				++g_pilot_mp_battle_history_count;
			}
		}
		for (list_index = 0;
		     list_index <
		     g_pilot_record_singleplayer_campaign_mission_count;
		     ++list_index) {
			if (g_pilot_data
				    .faction_statistics
					    [g_pilot_data.current_faction_id]
				    .sp_campaigns
					    [g_pilot_record_singleplayer_campaign_mission_list
						     [list_index]
							     .mission_idx]
				    .attempt_count != 0) {
				++g_pilot_sp_campaign_history_row_count;
			}
		}
		for (list_index = 0;
		     list_index <
		     g_pilot_record_singleplayer_training_mission_count;
		     ++list_index) {
			if (g_pilot_data
				    .faction_statistics
					    [g_pilot_data.current_faction_id]
				    .sp_campaign_missions
					    [g_pilot_record_singleplayer_training_mission_list
						     [list_index]
							     .mission_idx -
					     1]
				    .number_times_flown != 0) {
				++g_pilot_sp_campaign_history_row_count;
			}
		}
		for (list_index = 0;
		     list_index <
		     g_pilot_record_multiplayer_campaign_mission_count;
		     ++list_index) {
			if (g_pilot_data
				    .faction_statistics
					    [g_pilot_data.current_faction_id]
				    .mp_campaigns
					    [g_pilot_record_multiplayer_campaign_mission_list
						     [list_index]
							     .mission_idx]
				    .attempt_count != 0) {
				++g_pilot_mp_campaign_history_row_count;
			}
		}
		for (list_index = 0;
		     list_index <
		     g_pilot_record_multiplayer_training_mission_count;
		     ++list_index) {
			if (g_pilot_data
				    .faction_statistics
					    [g_pilot_data.current_faction_id]
				    .mp_campaign_missions
					    [g_pilot_record_multiplayer_training_mission_list
						     [list_index]
							     .mission_idx -
					     1]
				    .number_times_flown != 0) {
				++g_pilot_mp_campaign_history_row_count;
			}
		}
		row = g_pilot_sp_battle_history_count;
		row += g_pilot_sp_tournament_history_count;
		row += g_pilot_mp_campaign_history_row_count;
		row += g_pilot_sp_training_history_count;
		row += g_pilot_mp_battle_history_count;
		row += g_pilot_sp_combat_history_count;
		row += g_pilot_sp_melee_history_count;
		row += g_pilot_mp_tournament_history_count;
		row += g_pilot_mp_training_history_count;
		row += g_pilot_mp_combat_history_count;
		row += g_pilot_mp_melee_history_count;
		row += g_pilot_sp_campaign_history_row_count;
		g_pilot_record_page_row_count = row;
		has_previous = 0;
		if (g_pilot_sp_training_history_count != 0) {
			g_pilot_record_page_row_count += 2;
			has_previous = 1;
		}
		if (g_pilot_sp_melee_history_count != 0) {
			g_pilot_record_page_row_count += 2;
			if (has_previous == 1) {
				++g_pilot_record_page_row_count;
			}
			has_previous = 1;
		}
		if (g_pilot_sp_combat_history_count != 0) {
			g_pilot_record_page_row_count += 2;
			if (has_previous == 1) {
				++g_pilot_record_page_row_count;
			}
			has_previous = 1;
		}
		if (g_pilot_sp_tournament_history_count != 0) {
			g_pilot_record_page_row_count += 2;
			if (has_previous == 1) {
				++g_pilot_record_page_row_count;
			}
			has_previous = 1;
		}
		if (g_pilot_sp_battle_history_count != 0) {
			g_pilot_record_page_row_count += 2;
			if (has_previous == 1) {
				++g_pilot_record_page_row_count;
			}
			has_previous = 1;
		}
		if (g_pilot_sp_campaign_history_row_count != 0) {
			g_pilot_record_page_row_count += 2;
			if (has_previous == 1) {
				++g_pilot_record_page_row_count;
			}
			has_previous = 1;
		}
		if (g_pilot_mp_training_history_count != 0) {
			g_pilot_record_page_row_count += 2;
			if (has_previous == 1) {
				++g_pilot_record_page_row_count;
			}
			has_previous = 1;
		}
		if (g_pilot_mp_melee_history_count != 0) {
			g_pilot_record_page_row_count += 2;
			if (has_previous == 1) {
				++g_pilot_record_page_row_count;
			}
			has_previous = 1;
		}
		if (g_pilot_mp_combat_history_count != 0) {
			g_pilot_record_page_row_count += 2;
			if (has_previous == 1) {
				++g_pilot_record_page_row_count;
			}
			has_previous = 1;
		}
		if (g_pilot_mp_tournament_history_count != 0) {
			g_pilot_record_page_row_count += 2;
			if (has_previous == 1) {
				++g_pilot_record_page_row_count;
			}
			has_previous = 1;
		}
		if (g_pilot_mp_battle_history_count != 0) {
			g_pilot_record_page_row_count += 2;
			if (has_previous == 0) {
				has_previous = 1;
				++g_pilot_record_page_row_count;
			}
		}
		if (g_pilot_mp_campaign_history_row_count != 0) {
			g_pilot_record_page_row_count += 2;
			if (has_previous == 1) {
				++g_pilot_record_page_row_count;
			}
		}
		XVT_LOG_DEBUG(
			"pilot.achievements_counted faction=%d rows=%d solo_histories=\"%d,%d,%d,%d,%d,%d\" network_histories=\"%d,%d,%d,%d,%d,%d\"",
			g_pilot_data.current_faction_id,
			g_pilot_record_page_row_count,
			g_pilot_sp_training_history_count,
			g_pilot_sp_melee_history_count,
			g_pilot_sp_combat_history_count,
			g_pilot_sp_tournament_history_count,
			g_pilot_sp_battle_history_count,
			g_pilot_sp_campaign_history_row_count,
			g_pilot_mp_training_history_count,
			g_pilot_mp_melee_history_count,
			g_pilot_mp_combat_history_count,
			g_pilot_mp_tournament_history_count,
			g_pilot_mp_battle_history_count,
			g_pilot_mp_campaign_history_row_count);
	}

	frontend_draw_rect_assign(&rect, MISSION_ACHIEVEMENT_SCROLL_LEFT,
				  MISSION_ACHIEVEMENT_SCROLL_TOP,
				  MISSION_ACHIEVEMENT_SCROLL_RIGHT,
				  MISSION_ACHIEVEMENT_SCROLL_BOTTOM);
	if (g_pilot_record_page_row_count > MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
		g_pilot_achievements_scroll_offset = frontend_scrollbar_draw(
			&rect, g_pilot_achievements_scroll_offset,
			g_pilot_record_page_row_count, 0,
			MISSION_ACHIEVEMENT_SCROLL_PAGE_STEP, g_color_navy,
			MISSION_ACHIEVEMENT_SCROLL_CONTROL_ID);
	}

	row = 0;
	int y = MISSION_ACHIEVEMENT_FIRST_Y;
	has_previous = 0;
	struct RECT previous_clip_rect;
	int draw_flags;
	if (g_pilot_sp_training_history_count != 0) {
		if (has_previous != 0) {
			if (g_pilot_achievements_scroll_offset <= row &&
			    row - g_pilot_achievements_scroll_offset <
				    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
				y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
			}
			++row;
		}
		has_previous = 1;
		if (g_pilot_achievements_scroll_offset <= row &&
		    row - g_pilot_achievements_scroll_offset <
			    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			frontend_text_draw(
				MISSION_ACHIEVEMENT_FONT_SIZE,
				frontend_string_get(FRONTSTR_451_SINGLE_PLAYER),
				MISSION_ACHIEVEMENT_HEADER_X, y,
				g_color_yellow);
			frontend_text_draw(
				MISSION_ACHIEVEMENT_FONT_SIZE,
				frontend_string_get(FRONTSTR_459_BEST),
				MISSION_ACHIEVEMENT_SCORE_X, y, g_color_yellow);
			frontend_text_draw(
				MISSION_ACHIEVEMENT_FONT_SIZE,
				frontend_string_get(FRONTSTR_459_BEST),
				MISSION_ACHIEVEMENT_DETAIL_X, y,
				g_color_yellow);
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		if (g_pilot_achievements_scroll_offset <= row &&
		    row - g_pilot_achievements_scroll_offset <
			    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			frontend_text_draw(
				MISSION_ACHIEVEMENT_FONT_SIZE,
				frontend_string_get(
					FRONTSTR_453_TRAINING_HISTORY),
				MISSION_ACHIEVEMENT_HEADER_X, y,
				g_color_yellow);
			frontend_text_draw(
				MISSION_ACHIEVEMENT_FONT_SIZE,
				frontend_string_get(FRONTSTR_460_SCORE),
				MISSION_ACHIEVEMENT_SCORE_X, y, g_color_yellow);
			frontend_text_draw(
				MISSION_ACHIEVEMENT_FONT_SIZE,
				frontend_string_get(FRONTSTR_461_TIME),
				MISSION_ACHIEVEMENT_DETAIL_X, y,
				g_color_yellow);
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		for (list_index = 0;
		     list_index <
		     g_pilot_record_singleplayer_training_mission_count;
		     ++list_index) {
			if (g_pilot_data
				    .faction_statistics
					    [g_pilot_data.current_faction_id]
				    .sp_training_missions
					    [g_pilot_record_singleplayer_training_mission_list
						     [list_index]
							     .mission_idx]
				    .number_times_flown != 0) {
				if (g_pilot_achievements_scroll_offset <= row &&
				    row - g_pilot_achievements_scroll_offset <
					    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
					frontend_display_get_screen_clip_rect(
						&previous_clip_rect);
					frontend_draw_rect_assign(
						&rect,
						MISSION_ACHIEVEMENT_TEXT_X, y,
						MISSION_ACHIEVEMENT_TEXT_RIGHT,
						y + MISSION_ACHIEVEMENT_ROW_HEIGHT);
					frontend_display_set_screen_clip_rect640x480(
						&rect);
					draw_flags = frontend_text_draw(
						MISSION_ACHIEVEMENT_FONT_SIZE,
						g_pilot_record_singleplayer_training_mission_list
							[list_index]
								.description,
						MISSION_ACHIEVEMENT_TEXT_X, y,
						MISSION_ACHIEVEMENT_TEXT_COLOR);
					frontend_display_set_screen_clip_rect640x480(
						&previous_clip_rect);
					if ((draw_flags &
					     MISSION_ACHIEVEMENT_TEXT_TRUNCATED_FLAG) !=
					    0) {
						frontend_text_draw(
							MISSION_ACHIEVEMENT_FONT_SIZE,
							"....",
							MISSION_ACHIEVEMENT_TEXT_RIGHT,
							y,
							MISSION_ACHIEVEMENT_TEXT_COLOR);
					}
					sprintf(g_frontend_scratch_buffer, "%d",
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.sp_training_missions
								[g_pilot_record_singleplayer_training_mission_list
									 [list_index]
										 .mission_idx]
							.best_score);
					frontend_text_draw(
						MISSION_ACHIEVEMENT_FONT_SIZE,
						g_frontend_scratch_buffer,
						MISSION_ACHIEVEMENT_SCORE_X, y,
						MISSION_ACHIEVEMENT_TEXT_COLOR);
					if (g_pilot_data
						    .faction_statistics
							    [g_pilot_data
								     .current_faction_id]
						    .sp_training_missions
							    [g_pilot_record_singleplayer_training_mission_list
								     [list_index]
									     .mission_idx]
						    .best_time == 0) {
						strcpy(g_frontend_scratch_buffer,
						       frontend_string_get(
							       FRONTSTR_535_DASH_PLACEHOLDER));
					} else {
						frontend_format_seconds_to_clock_string(
							g_pilot_data
								.faction_statistics
									[g_pilot_data
										 .current_faction_id]
								.sp_training_missions
									[g_pilot_record_singleplayer_training_mission_list
										 [list_index]
											 .mission_idx]
								.best_time);
					}
					frontend_text_draw(
						MISSION_ACHIEVEMENT_FONT_SIZE,
						g_frontend_scratch_buffer,
						MISSION_ACHIEVEMENT_DETAIL_X, y,
						MISSION_ACHIEVEMENT_TEXT_COLOR);
					y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
				}
				++row;
			}
		}
	}
	if (g_pilot_sp_melee_history_count != 0) {
		if (has_previous != 0) {
			if (g_pilot_achievements_scroll_offset <= row &&
			    row - g_pilot_achievements_scroll_offset <
				    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
				y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
			}
			++row;
		}
		has_previous = 1;
		if (g_pilot_achievements_scroll_offset <= row &&
		    row - g_pilot_achievements_scroll_offset <
			    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			frontend_text_draw(
				MISSION_ACHIEVEMENT_FONT_SIZE,
				frontend_string_get(FRONTSTR_451_SINGLE_PLAYER),
				MISSION_ACHIEVEMENT_HEADER_X, y,
				g_color_yellow);
			frontend_text_draw(
				MISSION_ACHIEVEMENT_FONT_SIZE,
				frontend_string_get(FRONTSTR_459_BEST),
				MISSION_ACHIEVEMENT_SCORE_X, y, g_color_yellow);
			frontend_text_draw(
				MISSION_ACHIEVEMENT_FONT_SIZE,
				frontend_string_get(FRONTSTR_459_BEST),
				MISSION_ACHIEVEMENT_DETAIL_X, y,
				g_color_yellow);
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		if (g_pilot_achievements_scroll_offset <= row &&
		    row - g_pilot_achievements_scroll_offset <
			    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			frontend_text_draw(
				MISSION_ACHIEVEMENT_FONT_SIZE,
				frontend_string_get(FRONTSTR_454_MELEE_HISTORY),
				MISSION_ACHIEVEMENT_HEADER_X, y,
				g_color_yellow);
			frontend_text_draw(
				MISSION_ACHIEVEMENT_FONT_SIZE,
				frontend_string_get(FRONTSTR_460_SCORE),
				MISSION_ACHIEVEMENT_SCORE_X, y, g_color_yellow);
			frontend_text_draw(
				MISSION_ACHIEVEMENT_FONT_SIZE,
				frontend_string_get(FRONTSTR_462_FINISH),
				MISSION_ACHIEVEMENT_DETAIL_X, y,
				g_color_yellow);
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		for (list_index = 0;
		     list_index < g_pilot_record_melee_mission_count;
		     ++list_index) {
			if (g_pilot_data
				    .faction_statistics
					    [g_pilot_data.current_faction_id]
				    .sp_melee_missions
					    [g_pilot_record_melee_mission_list
						     [list_index]
							     .mission_idx]
				    .number_times_flown != 0) {
				if (g_pilot_achievements_scroll_offset <= row &&
				    row - g_pilot_achievements_scroll_offset <
					    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
					frontend_display_get_screen_clip_rect(
						&previous_clip_rect);
					frontend_draw_rect_assign(
						&rect,
						MISSION_ACHIEVEMENT_TEXT_X, y,
						MISSION_ACHIEVEMENT_TEXT_RIGHT,
						y + MISSION_ACHIEVEMENT_ROW_HEIGHT);
					frontend_display_set_screen_clip_rect640x480(
						&rect);
					draw_flags = frontend_text_draw(
						MISSION_ACHIEVEMENT_FONT_SIZE,
						g_pilot_record_melee_mission_list
							[list_index]
								.description,
						MISSION_ACHIEVEMENT_TEXT_X, y,
						MISSION_ACHIEVEMENT_TEXT_COLOR);
					frontend_display_set_screen_clip_rect640x480(
						&previous_clip_rect);
					if ((draw_flags &
					     MISSION_ACHIEVEMENT_TEXT_TRUNCATED_FLAG) !=
					    0) {
						frontend_text_draw(
							MISSION_ACHIEVEMENT_FONT_SIZE,
							"....",
							MISSION_ACHIEVEMENT_TEXT_RIGHT,
							y,
							MISSION_ACHIEVEMENT_TEXT_COLOR);
					}
					sprintf(g_frontend_scratch_buffer, "%d",
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.sp_melee_missions
								[g_pilot_record_melee_mission_list
									 [list_index]
										 .mission_idx]
							.best_score);
					frontend_text_draw(
						MISSION_ACHIEVEMENT_FONT_SIZE,
						g_frontend_scratch_buffer,
						MISSION_ACHIEVEMENT_SCORE_X, y,
						MISSION_ACHIEVEMENT_TEXT_COLOR);
					if (g_pilot_data
						    .faction_statistics
							    [g_pilot_data
								     .current_faction_id]
						    .sp_melee_missions
							    [g_pilot_record_melee_mission_list
								     [list_index]
									     .mission_idx]
						    .best_placement != 0) {
						sprintf(g_frontend_scratch_buffer,
							"%s",
							frontend_string_get((
								frontend_string_id)(g_pilot_data
											    .faction_statistics
												    [g_pilot_data
													     .current_faction_id]
											    .sp_melee_missions
												    [g_pilot_record_melee_mission_list
													     [list_index]
														     .mission_idx]
											    .best_placement +
										    317)));
					} else {
						sprintf(g_frontend_scratch_buffer,
							"---");
					}
					frontend_text_draw(
						MISSION_ACHIEVEMENT_FONT_SIZE,
						g_frontend_scratch_buffer,
						MISSION_ACHIEVEMENT_DETAIL_X, y,
						MISSION_ACHIEVEMENT_TEXT_COLOR);
					y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
				}
				++row;
			}
		}
	}
	if (g_pilot_sp_tournament_history_count != 0) {
		if (has_previous != 0) {
			if (g_pilot_achievements_scroll_offset <= row &&
			    row - g_pilot_achievements_scroll_offset <
				    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
				y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
			}
			++row;
		}
		has_previous = 1;
		if (g_pilot_achievements_scroll_offset <= row &&
		    row - g_pilot_achievements_scroll_offset <
			    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			frontend_text_draw(
				MISSION_ACHIEVEMENT_FONT_SIZE,
				frontend_string_get(FRONTSTR_451_SINGLE_PLAYER),
				MISSION_ACHIEVEMENT_HEADER_X, y,
				g_color_yellow);
			frontend_text_draw(
				MISSION_ACHIEVEMENT_FONT_SIZE,
				frontend_string_get(FRONTSTR_459_BEST),
				MISSION_ACHIEVEMENT_SCORE_X, y, g_color_yellow);
			frontend_text_draw(
				MISSION_ACHIEVEMENT_FONT_SIZE,
				frontend_string_get(FRONTSTR_459_BEST),
				MISSION_ACHIEVEMENT_DETAIL_X, y,
				g_color_yellow);
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		if (g_pilot_achievements_scroll_offset <= row &&
		    row - g_pilot_achievements_scroll_offset <
			    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			frontend_text_draw(
				MISSION_ACHIEVEMENT_FONT_SIZE,
				frontend_string_get(
					FRONTSTR_456_TOURNAMENT_HISTORY),
				MISSION_ACHIEVEMENT_HEADER_X, y,
				g_color_yellow);
			frontend_text_draw(
				MISSION_ACHIEVEMENT_FONT_SIZE,
				frontend_string_get(FRONTSTR_460_SCORE),
				MISSION_ACHIEVEMENT_SCORE_X, y, g_color_yellow);
			frontend_text_draw(
				MISSION_ACHIEVEMENT_FONT_SIZE,
				frontend_string_get(FRONTSTR_462_FINISH),
				MISSION_ACHIEVEMENT_DETAIL_X, y,
				g_color_yellow);
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		for (list_index = 0;
		     list_index < g_pilot_record_tournament_mission_count;
		     ++list_index) {
			if (g_pilot_data
				    .faction_statistics
					    [g_pilot_data.current_faction_id]
				    .sp_tournaments
					    [g_pilot_record_tournament_mission_list
						     [list_index]
							     .mission_idx]
				    .attempt_count != 0) {
				if (g_pilot_achievements_scroll_offset <= row &&
				    row - g_pilot_achievements_scroll_offset <
					    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
					frontend_display_get_screen_clip_rect(
						&previous_clip_rect);
					frontend_draw_rect_assign(
						&rect,
						MISSION_ACHIEVEMENT_TEXT_X, y,
						MISSION_ACHIEVEMENT_TEXT_RIGHT,
						y + MISSION_ACHIEVEMENT_ROW_HEIGHT);
					frontend_display_set_screen_clip_rect640x480(
						&rect);
					draw_flags = frontend_text_draw(
						MISSION_ACHIEVEMENT_FONT_SIZE,
						g_pilot_record_tournament_mission_list
							[list_index]
								.description,
						MISSION_ACHIEVEMENT_TEXT_X, y,
						MISSION_ACHIEVEMENT_TEXT_COLOR);
					frontend_display_set_screen_clip_rect640x480(
						&previous_clip_rect);
					if ((draw_flags &
					     MISSION_ACHIEVEMENT_TEXT_TRUNCATED_FLAG) !=
					    0) {
						frontend_text_draw(
							MISSION_ACHIEVEMENT_FONT_SIZE,
							"...",
							MISSION_ACHIEVEMENT_TEXT_RIGHT,
							y,
							MISSION_ACHIEVEMENT_TEXT_COLOR);
					}
					sprintf(g_frontend_scratch_buffer, "%d",
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.sp_tournaments
								[g_pilot_record_tournament_mission_list
									 [list_index]
										 .mission_idx]
							.best_score);
					frontend_text_draw(
						MISSION_ACHIEVEMENT_FONT_SIZE,
						g_frontend_scratch_buffer,
						MISSION_ACHIEVEMENT_SCORE_X, y,
						MISSION_ACHIEVEMENT_TEXT_COLOR);
					if (g_pilot_data
						    .faction_statistics
							    [g_pilot_data
								     .current_faction_id]
						    .sp_tournaments
							    [g_pilot_record_tournament_mission_list
								     [list_index]
									     .mission_idx]
						    .best_placement != 0) {
						sprintf(g_frontend_scratch_buffer,
							"%s",
							frontend_string_get((
								frontend_string_id)(g_pilot_data
											    .faction_statistics
												    [g_pilot_data
													     .current_faction_id]
											    .sp_tournaments
												    [g_pilot_record_tournament_mission_list
													     [list_index]
														     .mission_idx]
											    .best_placement +
										    317)));
					} else {
						sprintf(g_frontend_scratch_buffer,
							"---");
					}
					frontend_text_draw(
						MISSION_ACHIEVEMENT_FONT_SIZE,
						g_frontend_scratch_buffer,
						MISSION_ACHIEVEMENT_DETAIL_X, y,
						MISSION_ACHIEVEMENT_TEXT_COLOR);
					y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
				}
				++row;
			}
		}
	}
	if (g_pilot_sp_combat_history_count != 0) {
		if (has_previous != 0) {
			if (g_pilot_achievements_scroll_offset <= row &&
			    row - g_pilot_achievements_scroll_offset <
				    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
				y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
			}
			++row;
		}
		has_previous = 1;
		if (g_pilot_achievements_scroll_offset <= row &&
		    row - g_pilot_achievements_scroll_offset <
			    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			frontend_text_draw(
				MISSION_ACHIEVEMENT_FONT_SIZE,
				frontend_string_get(FRONTSTR_451_SINGLE_PLAYER),
				MISSION_ACHIEVEMENT_HEADER_X, y,
				g_color_yellow);
			frontend_text_draw(
				MISSION_ACHIEVEMENT_FONT_SIZE,
				frontend_string_get(FRONTSTR_459_BEST),
				MISSION_ACHIEVEMENT_SCORE_X, y, g_color_yellow);
			frontend_text_draw(
				MISSION_ACHIEVEMENT_FONT_SIZE,
				frontend_string_get(FRONTSTR_459_BEST),
				MISSION_ACHIEVEMENT_DETAIL_X, y,
				g_color_yellow);
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		if (g_pilot_achievements_scroll_offset <= row &&
		    row - g_pilot_achievements_scroll_offset <
			    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			frontend_text_draw(MISSION_ACHIEVEMENT_FONT_SIZE,
					   frontend_string_get(
						   FRONTSTR_455_COMBAT_HISTORY),
					   MISSION_ACHIEVEMENT_HEADER_X, y,
					   g_color_yellow);
			frontend_text_draw(
				MISSION_ACHIEVEMENT_FONT_SIZE,
				frontend_string_get(FRONTSTR_460_SCORE),
				MISSION_ACHIEVEMENT_SCORE_X, y, g_color_yellow);
			frontend_text_draw(
				MISSION_ACHIEVEMENT_FONT_SIZE,
				frontend_string_get(FRONTSTR_461_TIME),
				MISSION_ACHIEVEMENT_DETAIL_X, y,
				g_color_yellow);
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		for (list_index = 0;
		     list_index <
		     g_pilot_record_singleplayer_combat_mission_count;
		     ++list_index) {
			if (g_pilot_data
				    .faction_statistics
					    [g_pilot_data.current_faction_id]
				    .sp_combat_missions
					    [g_pilot_record_singleplayer_combat_mission_list
						     [list_index]
							     .mission_idx]
				    .number_times_flown != 0) {
				if (g_pilot_achievements_scroll_offset <= row &&
				    row - g_pilot_achievements_scroll_offset <
					    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
					frontend_display_get_screen_clip_rect(
						&previous_clip_rect);
					frontend_draw_rect_assign(
						&rect,
						MISSION_ACHIEVEMENT_TEXT_X, y,
						MISSION_ACHIEVEMENT_TEXT_RIGHT,
						y + MISSION_ACHIEVEMENT_ROW_HEIGHT);
					frontend_display_set_screen_clip_rect640x480(
						&rect);
					draw_flags = frontend_text_draw(
						MISSION_ACHIEVEMENT_FONT_SIZE,
						g_pilot_record_singleplayer_combat_mission_list
							[list_index]
								.description,
						MISSION_ACHIEVEMENT_TEXT_X, y,
						MISSION_ACHIEVEMENT_TEXT_COLOR);
					frontend_display_set_screen_clip_rect640x480(
						&previous_clip_rect);
					if ((draw_flags &
					     MISSION_ACHIEVEMENT_TEXT_TRUNCATED_FLAG) !=
					    0) {
						frontend_text_draw(
							MISSION_ACHIEVEMENT_FONT_SIZE,
							"...",
							MISSION_ACHIEVEMENT_TEXT_RIGHT,
							y,
							MISSION_ACHIEVEMENT_TEXT_COLOR);
					}
					sprintf(g_frontend_scratch_buffer, "%d",
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.sp_combat_missions
								[g_pilot_record_singleplayer_combat_mission_list
									 [list_index]
										 .mission_idx]
							.best_score);
					frontend_text_draw(
						MISSION_ACHIEVEMENT_FONT_SIZE,
						g_frontend_scratch_buffer,
						MISSION_ACHIEVEMENT_SCORE_X, y,
						MISSION_ACHIEVEMENT_TEXT_COLOR);
					if (g_pilot_data
						    .faction_statistics
							    [g_pilot_data
								     .current_faction_id]
						    .sp_combat_missions
							    [g_pilot_record_singleplayer_combat_mission_list
								     [list_index]
									     .mission_idx]
						    .best_time == 0) {
						strcpy(g_frontend_scratch_buffer,
						       frontend_string_get(
							       FRONTSTR_535_DASH_PLACEHOLDER));
					} else {
						frontend_format_seconds_to_clock_string(
							g_pilot_data
								.faction_statistics
									[g_pilot_data
										 .current_faction_id]
								.sp_combat_missions
									[g_pilot_record_singleplayer_combat_mission_list
										 [list_index]
											 .mission_idx]
								.best_time);
					}
					frontend_text_draw(
						MISSION_ACHIEVEMENT_FONT_SIZE,
						g_frontend_scratch_buffer,
						MISSION_ACHIEVEMENT_DETAIL_X, y,
						MISSION_ACHIEVEMENT_TEXT_COLOR);
					y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
				}
				++row;
			}
		}
	}
	if (g_pilot_sp_battle_history_count != 0) {
		if (has_previous != 0) {
			if (g_pilot_achievements_scroll_offset <= row &&
			    row - g_pilot_achievements_scroll_offset <
				    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
				y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
			}
			++row;
		}
		has_previous = 1;
		if (g_pilot_achievements_scroll_offset <= row &&
		    row - g_pilot_achievements_scroll_offset <
			    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			frontend_text_draw(
				MISSION_ACHIEVEMENT_FONT_SIZE,
				frontend_string_get(FRONTSTR_451_SINGLE_PLAYER),
				MISSION_ACHIEVEMENT_HEADER_X, y,
				g_color_yellow);
			frontend_text_draw(
				MISSION_ACHIEVEMENT_FONT_SIZE,
				frontend_string_get(FRONTSTR_459_BEST),
				MISSION_ACHIEVEMENT_SCORE_X, y, g_color_yellow);
			frontend_text_draw(
				MISSION_ACHIEVEMENT_FONT_SIZE,
				frontend_string_get(FRONTSTR_459_BEST),
				MISSION_ACHIEVEMENT_DETAIL_X, y,
				g_color_yellow);
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		if (g_pilot_achievements_scroll_offset <= row &&
		    row - g_pilot_achievements_scroll_offset <
			    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			frontend_text_draw(MISSION_ACHIEVEMENT_FONT_SIZE,
					   frontend_string_get(
						   FRONTSTR_457_BATTLE_HISTORY),
					   MISSION_ACHIEVEMENT_HEADER_X, y,
					   g_color_yellow);
			frontend_text_draw(
				MISSION_ACHIEVEMENT_FONT_SIZE,
				frontend_string_get(FRONTSTR_460_SCORE),
				MISSION_ACHIEVEMENT_SCORE_X, y, g_color_yellow);
			frontend_text_draw(
				MISSION_ACHIEVEMENT_FONT_SIZE,
				frontend_string_get(FRONTSTR_718_MARGIN),
				MISSION_ACHIEVEMENT_DETAIL_X, y,
				g_color_yellow);
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		for (list_index = 0; list_index < g_battle_mission_list_count;
		     ++list_index) {
			if (g_pilot_data
				    .faction_statistics
					    [g_pilot_data.current_faction_id]
				    .sp_battles
					    [g_battle_mission_list[list_index]
						     .mission_idx]
				    .attempt_count != 0) {
				if (g_pilot_achievements_scroll_offset <= row &&
				    row - g_pilot_achievements_scroll_offset <
					    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
					frontend_display_get_screen_clip_rect(
						&previous_clip_rect);
					frontend_draw_rect_assign(
						&rect,
						MISSION_ACHIEVEMENT_TEXT_X, y,
						MISSION_ACHIEVEMENT_TEXT_RIGHT,
						y + MISSION_ACHIEVEMENT_ROW_HEIGHT);
					frontend_display_set_screen_clip_rect640x480(
						&rect);
					draw_flags = frontend_text_draw(
						MISSION_ACHIEVEMENT_FONT_SIZE,
						g_battle_mission_list
							[list_index]
								.description,
						MISSION_ACHIEVEMENT_TEXT_X, y,
						MISSION_ACHIEVEMENT_TEXT_COLOR);
					frontend_display_set_screen_clip_rect640x480(
						&previous_clip_rect);
					if ((draw_flags &
					     MISSION_ACHIEVEMENT_TEXT_TRUNCATED_FLAG) !=
					    0) {
						frontend_text_draw(
							MISSION_ACHIEVEMENT_FONT_SIZE,
							"...",
							MISSION_ACHIEVEMENT_TEXT_RIGHT,
							y,
							MISSION_ACHIEVEMENT_TEXT_COLOR);
					}
					sprintf(g_frontend_scratch_buffer, "%d",
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.sp_battles
								[g_battle_mission_list
									 [list_index]
										 .mission_idx]
							.best_score);
					frontend_text_draw(
						MISSION_ACHIEVEMENT_FONT_SIZE,
						g_frontend_scratch_buffer,
						MISSION_ACHIEVEMENT_SCORE_X, y,
						MISSION_ACHIEVEMENT_TEXT_COLOR);
					if (g_pilot_data
						    .faction_statistics
							    [g_pilot_data
								     .current_faction_id]
						    .sp_battles
							    [g_battle_mission_list
								     [list_index]
									     .mission_idx]
						    .victory_count != 0) {
						sprintf(g_frontend_scratch_buffer,
							"%d %s",
							g_pilot_data
								.faction_statistics
									[g_pilot_data
										 .current_faction_id]
								.sp_battles
									[g_battle_mission_list
										 [list_index]
											 .mission_idx]
								.best_victory_margin,
							frontend_string_get(
								FRONTSTR_719_VICTS));
					} else {
						sprintf(g_frontend_scratch_buffer,
							"---");
					}
					frontend_text_draw(
						MISSION_ACHIEVEMENT_FONT_SIZE,
						g_frontend_scratch_buffer,
						MISSION_ACHIEVEMENT_DETAIL_X, y,
						MISSION_ACHIEVEMENT_TEXT_COLOR);
					y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
				}
				++row;
			}
		}
	}
	int child_list_index;
	if (g_pilot_sp_campaign_history_row_count != 0) {
		if (has_previous != 0) {
			if (g_pilot_achievements_scroll_offset <= row &&
			    row - g_pilot_achievements_scroll_offset <
				    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
				y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
			}
			++row;
		}
		has_previous = 1;
		if (g_pilot_achievements_scroll_offset <= row &&
		    row - g_pilot_achievements_scroll_offset <
			    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			frontend_text_draw(
				MISSION_ACHIEVEMENT_FONT_SIZE,
				frontend_string_get(FRONTSTR_451_SINGLE_PLAYER),
				MISSION_ACHIEVEMENT_HEADER_X, y,
				g_color_yellow);
			frontend_text_draw(
				MISSION_ACHIEVEMENT_FONT_SIZE,
				frontend_string_get(FRONTSTR_459_BEST),
				MISSION_ACHIEVEMENT_SCORE_X, y, g_color_yellow);
			frontend_text_draw(
				MISSION_ACHIEVEMENT_FONT_SIZE,
				frontend_string_get(FRONTSTR_459_BEST),
				MISSION_ACHIEVEMENT_DETAIL_X, y,
				g_color_yellow);
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		if (g_pilot_achievements_scroll_offset <= row &&
		    row - g_pilot_achievements_scroll_offset <
			    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			frontend_text_draw(
				MISSION_ACHIEVEMENT_FONT_SIZE,
				frontend_string_get(
					FRONTSTR_458_CAMPAIGN_HISTORY),
				MISSION_ACHIEVEMENT_HEADER_X, y,
				g_color_yellow);
			frontend_text_draw(
				MISSION_ACHIEVEMENT_FONT_SIZE,
				frontend_string_get(FRONTSTR_460_SCORE),
				MISSION_ACHIEVEMENT_SCORE_X, y, g_color_yellow);
			frontend_text_draw(
				MISSION_ACHIEVEMENT_FONT_SIZE,
				frontend_string_get(FRONTSTR_787_PROGRESS),
				MISSION_ACHIEVEMENT_DETAIL_X, y,
				g_color_yellow);
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		for (list_index = 0;
		     list_index <
		     g_pilot_record_singleplayer_campaign_mission_count;
		     ++list_index) {
			if (g_pilot_data
				    .faction_statistics
					    [g_pilot_data.current_faction_id]
				    .sp_campaigns
					    [g_pilot_record_singleplayer_campaign_mission_list
						     [list_index]
							     .mission_idx]
				    .attempt_count != 0) {
				if (g_pilot_achievements_scroll_offset <= row &&
				    row - g_pilot_achievements_scroll_offset <
					    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
					frontend_display_get_screen_clip_rect(
						&previous_clip_rect);
					frontend_draw_rect_assign(
						&rect,
						MISSION_ACHIEVEMENT_TEXT_X, y,
						MISSION_ACHIEVEMENT_TEXT_RIGHT,
						y + MISSION_ACHIEVEMENT_ROW_HEIGHT);
					frontend_display_set_screen_clip_rect640x480(
						&rect);
					draw_flags = frontend_text_draw(
						MISSION_ACHIEVEMENT_FONT_SIZE,
						g_pilot_record_singleplayer_campaign_mission_list
							[list_index]
								.description,
						MISSION_ACHIEVEMENT_TEXT_X, y,
						MISSION_ACHIEVEMENT_TEXT_COLOR);
					frontend_display_set_screen_clip_rect640x480(
						&previous_clip_rect);
					if ((draw_flags &
					     MISSION_ACHIEVEMENT_TEXT_TRUNCATED_FLAG) !=
					    0) {
						frontend_text_draw(
							MISSION_ACHIEVEMENT_FONT_SIZE,
							"...",
							MISSION_ACHIEVEMENT_TEXT_RIGHT,
							y,
							MISSION_ACHIEVEMENT_TEXT_COLOR);
					}
					sprintf(g_frontend_scratch_buffer, "%d",
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.sp_campaigns
								[g_pilot_record_singleplayer_campaign_mission_list
									 [list_index]
										 .mission_idx]
							.best_score);
					frontend_text_draw(
						MISSION_ACHIEVEMENT_FONT_SIZE,
						g_frontend_scratch_buffer,
						MISSION_ACHIEVEMENT_SCORE_X, y,
						MISSION_ACHIEVEMENT_TEXT_COLOR);
					if (g_pilot_data
						    .faction_statistics
							    [g_pilot_data
								     .current_faction_id]
						    .sp_campaigns
							    [g_pilot_record_singleplayer_campaign_mission_list
								     [list_index]
									     .mission_idx]
						    .is_finished != 0) {
						strcpy(g_frontend_scratch_buffer,
						       frontend_string_get(
							       FRONTSTR_789_FINISHED));
					} else {
						sprintf(g_frontend_scratch_buffer,
							"%s %d",
							frontend_string_get(
								FRONTSTR_788_MIS),
							g_pilot_data
									.faction_statistics
										[g_pilot_data
											 .current_faction_id]
									.sp_campaigns
										[g_pilot_record_singleplayer_campaign_mission_list
											 [list_index]
												 .mission_idx]
									.next_mission_index +
								1);
					}
					frontend_text_draw(
						MISSION_ACHIEVEMENT_FONT_SIZE,
						g_frontend_scratch_buffer,
						MISSION_ACHIEVEMENT_DETAIL_X, y,
						MISSION_ACHIEVEMENT_TEXT_COLOR);
					y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
				}
				++row;
				for (child_list_index = 0;
				     child_list_index <
				     g_pilot_record_singleplayer_training_mission_count;
				     ++child_list_index) {
					/* award_id holds a mission id here,
					 * used to index the pilot's campaign
					 * mission records. */
					award_id =
						g_pilot_record_singleplayer_training_mission_list
							[child_list_index]
								.mission_idx;
					if (g_pilot_data
							    .faction_statistics
								    [g_pilot_data
									     .current_faction_id]
							    .sp_campaign_missions
								    [award_id -
								     1]
							    .number_times_flown !=
						    0 &&
					    g_pilot_data
							    .faction_statistics
								    [g_pilot_data
									     .current_faction_id]
							    .sp_campaign_missions
								    [award_id -
								     1]
							    .campaign_id ==
						    g_pilot_record_singleplayer_campaign_mission_list
							    [list_index]
								    .mission_idx) {
						if (g_pilot_achievements_scroll_offset <=
							    row &&
						    row - g_pilot_achievements_scroll_offset <
							    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
							frontend_display_get_screen_clip_rect(
								&previous_clip_rect);
							frontend_draw_rect_assign(
								&rect,
								MISSION_ACHIEVEMENT_TEXT_X,
								y,
								MISSION_ACHIEVEMENT_TEXT_RIGHT,
								y + MISSION_ACHIEVEMENT_ROW_HEIGHT);
							frontend_display_set_screen_clip_rect640x480(
								&rect);
							draw_flags = frontend_text_draw(
								MISSION_ACHIEVEMENT_FONT_SIZE,
								g_pilot_record_singleplayer_training_mission_list
									[child_list_index]
										.description,
								MISSION_ACHIEVEMENT_TEXT_X,
								y,
								g_color_gray);
							frontend_display_set_screen_clip_rect640x480(
								&previous_clip_rect);
							if ((draw_flags &
							     MISSION_ACHIEVEMENT_TEXT_TRUNCATED_FLAG) !=
							    0) {
								frontend_text_draw(
									MISSION_ACHIEVEMENT_FONT_SIZE,
									"...",
									MISSION_ACHIEVEMENT_TEXT_RIGHT,
									y,
									g_color_gray);
							}
							y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
						}
						++row;
					}
				}
			}
		}
	}
	if (g_pilot_mp_training_history_count != 0) {
		if (has_previous != 0) {
			if (g_pilot_achievements_scroll_offset <= row &&
			    row - g_pilot_achievements_scroll_offset <
				    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
				y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
			}
			++row;
		}
		has_previous = 1;
		if (g_pilot_achievements_scroll_offset <= row &&
		    row - g_pilot_achievements_scroll_offset <
			    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			frontend_text_draw(
				MISSION_ACHIEVEMENT_FONT_SIZE,
				frontend_string_get(FRONTSTR_452_MULTIPLAYER),
				MISSION_ACHIEVEMENT_HEADER_X, y,
				g_color_yellow);
			frontend_text_draw(
				MISSION_ACHIEVEMENT_FONT_SIZE,
				frontend_string_get(FRONTSTR_459_BEST),
				MISSION_ACHIEVEMENT_SCORE_X, y, g_color_yellow);
			frontend_text_draw(
				MISSION_ACHIEVEMENT_FONT_SIZE,
				frontend_string_get(FRONTSTR_459_BEST),
				MISSION_ACHIEVEMENT_DETAIL_X, y,
				g_color_yellow);
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		if (g_pilot_achievements_scroll_offset <= row &&
		    row - g_pilot_achievements_scroll_offset <
			    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			frontend_text_draw(
				MISSION_ACHIEVEMENT_FONT_SIZE,
				frontend_string_get(
					FRONTSTR_453_TRAINING_HISTORY),
				MISSION_ACHIEVEMENT_HEADER_X, y,
				g_color_yellow);
			frontend_text_draw(
				MISSION_ACHIEVEMENT_FONT_SIZE,
				frontend_string_get(FRONTSTR_460_SCORE),
				MISSION_ACHIEVEMENT_SCORE_X, y, g_color_yellow);
			frontend_text_draw(
				MISSION_ACHIEVEMENT_FONT_SIZE,
				frontend_string_get(FRONTSTR_461_TIME),
				MISSION_ACHIEVEMENT_DETAIL_X, y,
				g_color_yellow);
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		for (list_index = 0;
		     list_index <
		     g_pilot_record_multiplayer_training_mission_count;
		     ++list_index) {
			if (g_pilot_data
				    .faction_statistics
					    [g_pilot_data.current_faction_id]
				    .mp_training_missions
					    [g_pilot_record_multiplayer_training_mission_list
						     [list_index]
							     .mission_idx]
				    .number_times_flown != 0) {
				if (g_pilot_achievements_scroll_offset <= row &&
				    row - g_pilot_achievements_scroll_offset <
					    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
					frontend_display_get_screen_clip_rect(
						&previous_clip_rect);
					frontend_draw_rect_assign(
						&rect,
						MISSION_ACHIEVEMENT_TEXT_X, y,
						MISSION_ACHIEVEMENT_TEXT_RIGHT,
						y + MISSION_ACHIEVEMENT_ROW_HEIGHT);
					frontend_display_set_screen_clip_rect640x480(
						&rect);
					draw_flags = frontend_text_draw(
						MISSION_ACHIEVEMENT_FONT_SIZE,
						g_pilot_record_multiplayer_training_mission_list
							[list_index]
								.description,
						MISSION_ACHIEVEMENT_TEXT_X, y,
						MISSION_ACHIEVEMENT_TEXT_COLOR);
					frontend_display_set_screen_clip_rect640x480(
						&previous_clip_rect);
					if ((draw_flags &
					     MISSION_ACHIEVEMENT_TEXT_TRUNCATED_FLAG) !=
					    0) {
						frontend_text_draw(
							MISSION_ACHIEVEMENT_FONT_SIZE,
							"....",
							MISSION_ACHIEVEMENT_TEXT_RIGHT,
							y,
							MISSION_ACHIEVEMENT_TEXT_COLOR);
					}
					sprintf(g_frontend_scratch_buffer, "%d",
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.mp_training_missions
								[g_pilot_record_multiplayer_training_mission_list
									 [list_index]
										 .mission_idx]
							.best_score);
					frontend_text_draw(
						MISSION_ACHIEVEMENT_FONT_SIZE,
						g_frontend_scratch_buffer,
						MISSION_ACHIEVEMENT_SCORE_X, y,
						MISSION_ACHIEVEMENT_TEXT_COLOR);
					if (g_pilot_data
						    .faction_statistics
							    [g_pilot_data
								     .current_faction_id]
						    .mp_training_missions
							    [g_pilot_record_multiplayer_training_mission_list
								     [list_index]
									     .mission_idx]
						    .best_time == 0) {
						strcpy(g_frontend_scratch_buffer,
						       frontend_string_get(
							       FRONTSTR_535_DASH_PLACEHOLDER));
					} else {
						frontend_format_seconds_to_clock_string(
							g_pilot_data
								.faction_statistics
									[g_pilot_data
										 .current_faction_id]
								.mp_training_missions
									[g_pilot_record_multiplayer_training_mission_list
										 [list_index]
											 .mission_idx]
								.best_time);
					}
					frontend_text_draw(
						MISSION_ACHIEVEMENT_FONT_SIZE,
						g_frontend_scratch_buffer,
						MISSION_ACHIEVEMENT_DETAIL_X, y,
						MISSION_ACHIEVEMENT_TEXT_COLOR);
					y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
				}
				++row;
			}
		}
	}
	if (g_pilot_mp_melee_history_count != 0) {
		if (has_previous != 0) {
			if (g_pilot_achievements_scroll_offset <= row &&
			    row - g_pilot_achievements_scroll_offset <
				    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
				y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
			}
			++row;
		}
		has_previous = 1;
		if (g_pilot_achievements_scroll_offset <= row &&
		    row - g_pilot_achievements_scroll_offset <
			    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			frontend_text_draw(
				MISSION_ACHIEVEMENT_FONT_SIZE,
				frontend_string_get(FRONTSTR_452_MULTIPLAYER),
				MISSION_ACHIEVEMENT_HEADER_X, y,
				g_color_yellow);
			frontend_text_draw(
				MISSION_ACHIEVEMENT_FONT_SIZE,
				frontend_string_get(FRONTSTR_459_BEST),
				MISSION_ACHIEVEMENT_SCORE_X, y, g_color_yellow);
			frontend_text_draw(
				MISSION_ACHIEVEMENT_FONT_SIZE,
				frontend_string_get(FRONTSTR_459_BEST),
				MISSION_ACHIEVEMENT_DETAIL_X, y,
				g_color_yellow);
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		if (g_pilot_achievements_scroll_offset <= row &&
		    row - g_pilot_achievements_scroll_offset <
			    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			frontend_text_draw(
				MISSION_ACHIEVEMENT_FONT_SIZE,
				frontend_string_get(FRONTSTR_454_MELEE_HISTORY),
				MISSION_ACHIEVEMENT_HEADER_X, y,
				g_color_yellow);
			frontend_text_draw(
				MISSION_ACHIEVEMENT_FONT_SIZE,
				frontend_string_get(FRONTSTR_460_SCORE),
				MISSION_ACHIEVEMENT_SCORE_X, y, g_color_yellow);
			frontend_text_draw(
				MISSION_ACHIEVEMENT_FONT_SIZE,
				frontend_string_get(FRONTSTR_462_FINISH),
				MISSION_ACHIEVEMENT_DETAIL_X, y,
				g_color_yellow);
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		for (list_index = 0;
		     list_index < g_pilot_record_melee_mission_count;
		     ++list_index) {
			if (g_pilot_data
				    .faction_statistics
					    [g_pilot_data.current_faction_id]
				    .mp_melee_missions
					    [g_pilot_record_melee_mission_list
						     [list_index]
							     .mission_idx]
				    .number_times_flown != 0) {
				if (g_pilot_achievements_scroll_offset <= row &&
				    row - g_pilot_achievements_scroll_offset <
					    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
					frontend_display_get_screen_clip_rect(
						&previous_clip_rect);
					frontend_draw_rect_assign(
						&rect,
						MISSION_ACHIEVEMENT_TEXT_X, y,
						MISSION_ACHIEVEMENT_TEXT_RIGHT,
						y + MISSION_ACHIEVEMENT_ROW_HEIGHT);
					frontend_display_set_screen_clip_rect640x480(
						&rect);
					draw_flags = frontend_text_draw(
						MISSION_ACHIEVEMENT_FONT_SIZE,
						g_pilot_record_melee_mission_list
							[list_index]
								.description,
						MISSION_ACHIEVEMENT_TEXT_X, y,
						MISSION_ACHIEVEMENT_TEXT_COLOR);
					frontend_display_set_screen_clip_rect640x480(
						&previous_clip_rect);
					if ((draw_flags &
					     MISSION_ACHIEVEMENT_TEXT_TRUNCATED_FLAG) !=
					    0) {
						frontend_text_draw(
							MISSION_ACHIEVEMENT_FONT_SIZE,
							"....",
							MISSION_ACHIEVEMENT_TEXT_RIGHT,
							y,
							MISSION_ACHIEVEMENT_TEXT_COLOR);
					}
					sprintf(g_frontend_scratch_buffer, "%d",
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.mp_melee_missions
								[g_pilot_record_melee_mission_list
									 [list_index]
										 .mission_idx]
							.best_score);
					frontend_text_draw(
						MISSION_ACHIEVEMENT_FONT_SIZE,
						g_frontend_scratch_buffer,
						MISSION_ACHIEVEMENT_SCORE_X, y,
						MISSION_ACHIEVEMENT_TEXT_COLOR);
					if (g_pilot_data
						    .faction_statistics
							    [g_pilot_data
								     .current_faction_id]
						    .mp_melee_missions
							    [g_pilot_record_melee_mission_list
								     [list_index]
									     .mission_idx]
						    .best_placement != 0) {
						sprintf(g_frontend_scratch_buffer,
							"%s",
							frontend_string_get((
								frontend_string_id)(g_pilot_data
											    .faction_statistics
												    [g_pilot_data
													     .current_faction_id]
											    .mp_melee_missions
												    [g_pilot_record_melee_mission_list
													     [list_index]
														     .mission_idx]
											    .best_placement +
										    317)));
					} else {
						sprintf(g_frontend_scratch_buffer,
							"---");
					}
					frontend_text_draw(
						MISSION_ACHIEVEMENT_FONT_SIZE,
						g_frontend_scratch_buffer,
						MISSION_ACHIEVEMENT_DETAIL_X, y,
						MISSION_ACHIEVEMENT_TEXT_COLOR);
					y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
				}
				++row;
			}
		}
	}
	if (g_pilot_mp_tournament_history_count != 0) {
		if (has_previous != 0) {
			if (g_pilot_achievements_scroll_offset <= row &&
			    row - g_pilot_achievements_scroll_offset <
				    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
				y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
			}
			++row;
		}
		has_previous = 1;
		if (g_pilot_achievements_scroll_offset <= row &&
		    row - g_pilot_achievements_scroll_offset <
			    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			frontend_text_draw(
				MISSION_ACHIEVEMENT_FONT_SIZE,
				frontend_string_get(FRONTSTR_452_MULTIPLAYER),
				MISSION_ACHIEVEMENT_HEADER_X, y,
				g_color_yellow);
			frontend_text_draw(
				MISSION_ACHIEVEMENT_FONT_SIZE,
				frontend_string_get(FRONTSTR_459_BEST),
				MISSION_ACHIEVEMENT_SCORE_X, y, g_color_yellow);
			frontend_text_draw(
				MISSION_ACHIEVEMENT_FONT_SIZE,
				frontend_string_get(FRONTSTR_459_BEST),
				MISSION_ACHIEVEMENT_DETAIL_X, y,
				g_color_yellow);
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		if (g_pilot_achievements_scroll_offset <= row &&
		    row - g_pilot_achievements_scroll_offset <
			    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			frontend_text_draw(
				MISSION_ACHIEVEMENT_FONT_SIZE,
				frontend_string_get(
					FRONTSTR_456_TOURNAMENT_HISTORY),
				MISSION_ACHIEVEMENT_HEADER_X, y,
				g_color_yellow);
			frontend_text_draw(
				MISSION_ACHIEVEMENT_FONT_SIZE,
				frontend_string_get(FRONTSTR_460_SCORE),
				MISSION_ACHIEVEMENT_SCORE_X, y, g_color_yellow);
			frontend_text_draw(
				MISSION_ACHIEVEMENT_FONT_SIZE,
				frontend_string_get(FRONTSTR_462_FINISH),
				MISSION_ACHIEVEMENT_DETAIL_X, y,
				g_color_yellow);
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		for (list_index = 0;
		     list_index < g_pilot_record_tournament_mission_count;
		     ++list_index) {
			if (g_pilot_data
				    .faction_statistics
					    [g_pilot_data.current_faction_id]
				    .mp_tournaments
					    [g_pilot_record_tournament_mission_list
						     [list_index]
							     .mission_idx]
				    .attempt_count != 0) {
				if (g_pilot_achievements_scroll_offset <= row &&
				    row - g_pilot_achievements_scroll_offset <
					    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
					frontend_display_get_screen_clip_rect(
						&previous_clip_rect);
					frontend_draw_rect_assign(
						&rect,
						MISSION_ACHIEVEMENT_TEXT_X, y,
						MISSION_ACHIEVEMENT_TEXT_RIGHT,
						y + MISSION_ACHIEVEMENT_ROW_HEIGHT);
					frontend_display_set_screen_clip_rect640x480(
						&rect);
					draw_flags = frontend_text_draw(
						MISSION_ACHIEVEMENT_FONT_SIZE,
						g_pilot_record_tournament_mission_list
							[list_index]
								.description,
						MISSION_ACHIEVEMENT_TEXT_X, y,
						MISSION_ACHIEVEMENT_TEXT_COLOR);
					frontend_display_set_screen_clip_rect640x480(
						&previous_clip_rect);
					if ((draw_flags &
					     MISSION_ACHIEVEMENT_TEXT_TRUNCATED_FLAG) !=
					    0) {
						frontend_text_draw(
							MISSION_ACHIEVEMENT_FONT_SIZE,
							"...",
							MISSION_ACHIEVEMENT_TEXT_RIGHT,
							y,
							MISSION_ACHIEVEMENT_TEXT_COLOR);
					}
					sprintf(g_frontend_scratch_buffer, "%d",
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.mp_tournaments
								[g_pilot_record_tournament_mission_list
									 [list_index]
										 .mission_idx]
							.best_score);
					frontend_text_draw(
						MISSION_ACHIEVEMENT_FONT_SIZE,
						g_frontend_scratch_buffer,
						MISSION_ACHIEVEMENT_SCORE_X, y,
						MISSION_ACHIEVEMENT_TEXT_COLOR);
					if (g_pilot_data
						    .faction_statistics
							    [g_pilot_data
								     .current_faction_id]
						    .mp_tournaments
							    [g_pilot_record_tournament_mission_list
								     [list_index]
									     .mission_idx]
						    .best_placement != 0) {
						sprintf(g_frontend_scratch_buffer,
							"%s",
							frontend_string_get((
								frontend_string_id)(g_pilot_data
											    .faction_statistics
												    [g_pilot_data
													     .current_faction_id]
											    .mp_tournaments
												    [g_pilot_record_tournament_mission_list
													     [list_index]
														     .mission_idx]
											    .best_placement +
										    317)));
					} else {
						sprintf(g_frontend_scratch_buffer,
							"---");
					}
					frontend_text_draw(
						MISSION_ACHIEVEMENT_FONT_SIZE,
						g_frontend_scratch_buffer,
						MISSION_ACHIEVEMENT_DETAIL_X, y,
						MISSION_ACHIEVEMENT_TEXT_COLOR);
					y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
				}
				++row;
			}
		}
	}
	if (g_pilot_mp_combat_history_count != 0) {
		if (has_previous != 0) {
			if (g_pilot_achievements_scroll_offset <= row &&
			    row - g_pilot_achievements_scroll_offset <
				    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
				y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
			}
			++row;
		}
		has_previous = 1;
		if (g_pilot_achievements_scroll_offset <= row &&
		    row - g_pilot_achievements_scroll_offset <
			    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			frontend_text_draw(
				MISSION_ACHIEVEMENT_FONT_SIZE,
				frontend_string_get(FRONTSTR_452_MULTIPLAYER),
				MISSION_ACHIEVEMENT_HEADER_X, y,
				g_color_yellow);
			frontend_text_draw(
				MISSION_ACHIEVEMENT_FONT_SIZE,
				frontend_string_get(FRONTSTR_459_BEST),
				MISSION_ACHIEVEMENT_SCORE_X, y, g_color_yellow);
			frontend_text_draw(
				MISSION_ACHIEVEMENT_FONT_SIZE,
				frontend_string_get(FRONTSTR_459_BEST),
				MISSION_ACHIEVEMENT_DETAIL_X, y,
				g_color_yellow);
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		if (g_pilot_achievements_scroll_offset <= row &&
		    row - g_pilot_achievements_scroll_offset <
			    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			frontend_text_draw(MISSION_ACHIEVEMENT_FONT_SIZE,
					   frontend_string_get(
						   FRONTSTR_455_COMBAT_HISTORY),
					   MISSION_ACHIEVEMENT_HEADER_X, y,
					   g_color_yellow);
			frontend_text_draw(
				MISSION_ACHIEVEMENT_FONT_SIZE,
				frontend_string_get(FRONTSTR_460_SCORE),
				MISSION_ACHIEVEMENT_SCORE_X, y, g_color_yellow);
			frontend_text_draw(
				MISSION_ACHIEVEMENT_FONT_SIZE,
				frontend_string_get(FRONTSTR_461_TIME),
				MISSION_ACHIEVEMENT_DETAIL_X, y,
				g_color_yellow);
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		for (list_index = 0;
		     list_index <
		     g_pilot_record_multiplayer_combat_mission_count;
		     ++list_index) {
			if (g_pilot_data
				    .faction_statistics
					    [g_pilot_data.current_faction_id]
				    .mp_combat_missions
					    [g_pilot_record_multiplayer_combat_mission_list
						     [list_index]
							     .mission_idx]
				    .number_times_flown != 0) {
				if (g_pilot_achievements_scroll_offset <= row &&
				    row - g_pilot_achievements_scroll_offset <
					    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
					frontend_display_get_screen_clip_rect(
						&previous_clip_rect);
					frontend_draw_rect_assign(
						&rect,
						MISSION_ACHIEVEMENT_TEXT_X, y,
						MISSION_ACHIEVEMENT_TEXT_RIGHT,
						y + MISSION_ACHIEVEMENT_ROW_HEIGHT);
					frontend_display_set_screen_clip_rect640x480(
						&rect);
					draw_flags = frontend_text_draw(
						MISSION_ACHIEVEMENT_FONT_SIZE,
						g_pilot_record_multiplayer_combat_mission_list
							[list_index]
								.description,
						MISSION_ACHIEVEMENT_TEXT_X, y,
						MISSION_ACHIEVEMENT_TEXT_COLOR);
					frontend_display_set_screen_clip_rect640x480(
						&previous_clip_rect);
					if ((draw_flags &
					     MISSION_ACHIEVEMENT_TEXT_TRUNCATED_FLAG) !=
					    0) {
						frontend_text_draw(
							MISSION_ACHIEVEMENT_FONT_SIZE,
							"...",
							MISSION_ACHIEVEMENT_TEXT_RIGHT,
							y,
							MISSION_ACHIEVEMENT_TEXT_COLOR);
					}
					sprintf(g_frontend_scratch_buffer, "%d",
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.mp_combat_missions
								[g_pilot_record_multiplayer_combat_mission_list
									 [list_index]
										 .mission_idx]
							.best_score);
					frontend_text_draw(
						MISSION_ACHIEVEMENT_FONT_SIZE,
						g_frontend_scratch_buffer,
						MISSION_ACHIEVEMENT_SCORE_X, y,
						MISSION_ACHIEVEMENT_TEXT_COLOR);
					if (g_pilot_data
						    .faction_statistics
							    [g_pilot_data
								     .current_faction_id]
						    .mp_combat_missions
							    [g_pilot_record_multiplayer_combat_mission_list
								     [list_index]
									     .mission_idx]
						    .best_time == 0) {
						strcpy(g_frontend_scratch_buffer,
						       frontend_string_get(
							       FRONTSTR_535_DASH_PLACEHOLDER));
					} else {
						frontend_format_seconds_to_clock_string(
							g_pilot_data
								.faction_statistics
									[g_pilot_data
										 .current_faction_id]
								.mp_combat_missions
									[g_pilot_record_multiplayer_combat_mission_list
										 [list_index]
											 .mission_idx]
								.best_time);
					}
					frontend_text_draw(
						MISSION_ACHIEVEMENT_FONT_SIZE,
						g_frontend_scratch_buffer,
						MISSION_ACHIEVEMENT_DETAIL_X, y,
						MISSION_ACHIEVEMENT_TEXT_COLOR);
					y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
				}
				++row;
			}
		}
	}
	if (g_pilot_mp_battle_history_count != 0) {
		if (has_previous != 0) {
			if (g_pilot_achievements_scroll_offset <= row &&
			    row - g_pilot_achievements_scroll_offset <
				    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
				y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
			}
			++row;
		}
		has_previous = 1;
		if (g_pilot_achievements_scroll_offset <= row &&
		    row - g_pilot_achievements_scroll_offset <
			    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			frontend_text_draw(
				MISSION_ACHIEVEMENT_FONT_SIZE,
				frontend_string_get(FRONTSTR_452_MULTIPLAYER),
				MISSION_ACHIEVEMENT_HEADER_X, y,
				g_color_yellow);
			frontend_text_draw(
				MISSION_ACHIEVEMENT_FONT_SIZE,
				frontend_string_get(FRONTSTR_459_BEST),
				MISSION_ACHIEVEMENT_SCORE_X, y, g_color_yellow);
			frontend_text_draw(
				MISSION_ACHIEVEMENT_FONT_SIZE,
				frontend_string_get(FRONTSTR_459_BEST),
				MISSION_ACHIEVEMENT_DETAIL_X, y,
				g_color_yellow);
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		if (g_pilot_achievements_scroll_offset <= row &&
		    row - g_pilot_achievements_scroll_offset <
			    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			frontend_text_draw(MISSION_ACHIEVEMENT_FONT_SIZE,
					   frontend_string_get(
						   FRONTSTR_457_BATTLE_HISTORY),
					   MISSION_ACHIEVEMENT_HEADER_X, y,
					   g_color_yellow);
			frontend_text_draw(
				MISSION_ACHIEVEMENT_FONT_SIZE,
				frontend_string_get(FRONTSTR_460_SCORE),
				MISSION_ACHIEVEMENT_SCORE_X, y, g_color_yellow);
			frontend_text_draw(
				MISSION_ACHIEVEMENT_FONT_SIZE,
				frontend_string_get(FRONTSTR_718_MARGIN),
				MISSION_ACHIEVEMENT_DETAIL_X, y,
				g_color_yellow);
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		for (list_index = 0; list_index < g_battle_mission_list_count;
		     ++list_index) {
			if (g_pilot_data
				    .faction_statistics
					    [g_pilot_data.current_faction_id]
				    .mp_battles
					    [g_battle_mission_list[list_index]
						     .mission_idx]
				    .attempt_count != 0) {
				if (g_pilot_achievements_scroll_offset <= row &&
				    row - g_pilot_achievements_scroll_offset <
					    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
					frontend_display_get_screen_clip_rect(
						&previous_clip_rect);
					frontend_draw_rect_assign(
						&rect,
						MISSION_ACHIEVEMENT_TEXT_X, y,
						MISSION_ACHIEVEMENT_TEXT_RIGHT,
						y + MISSION_ACHIEVEMENT_ROW_HEIGHT);
					frontend_display_set_screen_clip_rect640x480(
						&rect);
					draw_flags = frontend_text_draw(
						MISSION_ACHIEVEMENT_FONT_SIZE,
						g_battle_mission_list
							[list_index]
								.description,
						MISSION_ACHIEVEMENT_TEXT_X, y,
						MISSION_ACHIEVEMENT_TEXT_COLOR);
					frontend_display_set_screen_clip_rect640x480(
						&previous_clip_rect);
					if ((draw_flags &
					     MISSION_ACHIEVEMENT_TEXT_TRUNCATED_FLAG) !=
					    0) {
						frontend_text_draw(
							MISSION_ACHIEVEMENT_FONT_SIZE,
							"...",
							MISSION_ACHIEVEMENT_TEXT_RIGHT,
							y,
							MISSION_ACHIEVEMENT_TEXT_COLOR);
					}
					sprintf(g_frontend_scratch_buffer, "%d",
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.mp_battles
								[g_battle_mission_list
									 [list_index]
										 .mission_idx]
							.best_score);
					frontend_text_draw(
						MISSION_ACHIEVEMENT_FONT_SIZE,
						g_frontend_scratch_buffer,
						MISSION_ACHIEVEMENT_SCORE_X, y,
						MISSION_ACHIEVEMENT_TEXT_COLOR);
					if (g_pilot_data
						    .faction_statistics
							    [g_pilot_data
								     .current_faction_id]
						    .mp_battles
							    [g_battle_mission_list
								     [list_index]
									     .mission_idx]
						    .victory_count != 0) {
						sprintf(g_frontend_scratch_buffer,
							"%d %s",
							g_pilot_data
								.faction_statistics
									[g_pilot_data
										 .current_faction_id]
								.mp_battles
									[g_battle_mission_list
										 [list_index]
											 .mission_idx]
								.best_victory_margin,
							frontend_string_get(
								FRONTSTR_719_VICTS));
					} else {
						sprintf(g_frontend_scratch_buffer,
							"---");
					}
					frontend_text_draw(
						MISSION_ACHIEVEMENT_FONT_SIZE,
						g_frontend_scratch_buffer,
						MISSION_ACHIEVEMENT_DETAIL_X, y,
						MISSION_ACHIEVEMENT_TEXT_COLOR);
					y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
				}
				++row;
			}
		}
	}
	if (g_pilot_mp_campaign_history_row_count != 0) {
		if (has_previous != 0) {
			if (g_pilot_achievements_scroll_offset <= row &&
			    row - g_pilot_achievements_scroll_offset <
				    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
				y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
			}
			++row;
		}
		has_previous = 1;
		if (g_pilot_achievements_scroll_offset <= row &&
		    row - g_pilot_achievements_scroll_offset <
			    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			frontend_text_draw(
				MISSION_ACHIEVEMENT_FONT_SIZE,
				frontend_string_get(FRONTSTR_452_MULTIPLAYER),
				MISSION_ACHIEVEMENT_HEADER_X, y,
				g_color_yellow);
			frontend_text_draw(
				MISSION_ACHIEVEMENT_FONT_SIZE,
				frontend_string_get(FRONTSTR_459_BEST),
				MISSION_ACHIEVEMENT_SCORE_X, y, g_color_yellow);
			frontend_text_draw(
				MISSION_ACHIEVEMENT_FONT_SIZE,
				frontend_string_get(FRONTSTR_459_BEST),
				MISSION_ACHIEVEMENT_DETAIL_X, y,
				g_color_yellow);
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		if (g_pilot_achievements_scroll_offset <= row &&
		    row - g_pilot_achievements_scroll_offset <
			    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			frontend_text_draw(
				MISSION_ACHIEVEMENT_FONT_SIZE,
				frontend_string_get(
					FRONTSTR_458_CAMPAIGN_HISTORY),
				MISSION_ACHIEVEMENT_HEADER_X, y,
				g_color_yellow);
			frontend_text_draw(
				MISSION_ACHIEVEMENT_FONT_SIZE,
				frontend_string_get(FRONTSTR_460_SCORE),
				MISSION_ACHIEVEMENT_SCORE_X, y, g_color_yellow);
			frontend_text_draw(
				MISSION_ACHIEVEMENT_FONT_SIZE,
				frontend_string_get(FRONTSTR_787_PROGRESS),
				MISSION_ACHIEVEMENT_DETAIL_X, y,
				g_color_yellow);
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		for (list_index = 0;
		     list_index <
		     g_pilot_record_multiplayer_campaign_mission_count;
		     ++list_index) {
			if (g_pilot_data
				    .faction_statistics
					    [g_pilot_data.current_faction_id]
				    .mp_campaigns
					    [g_pilot_record_multiplayer_campaign_mission_list
						     [list_index]
							     .mission_idx]
				    .attempt_count != 0) {
				if (g_pilot_achievements_scroll_offset <= row &&
				    row - g_pilot_achievements_scroll_offset <
					    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
					frontend_display_get_screen_clip_rect(
						&previous_clip_rect);
					frontend_draw_rect_assign(
						&rect,
						MISSION_ACHIEVEMENT_TEXT_X, y,
						MISSION_ACHIEVEMENT_TEXT_RIGHT,
						y + MISSION_ACHIEVEMENT_ROW_HEIGHT);
					frontend_display_set_screen_clip_rect640x480(
						&rect);
					draw_flags = frontend_text_draw(
						MISSION_ACHIEVEMENT_FONT_SIZE,
						g_pilot_record_multiplayer_campaign_mission_list
							[list_index]
								.description,
						MISSION_ACHIEVEMENT_TEXT_X, y,
						MISSION_ACHIEVEMENT_TEXT_COLOR);
					frontend_display_set_screen_clip_rect640x480(
						&previous_clip_rect);
					if ((draw_flags &
					     MISSION_ACHIEVEMENT_TEXT_TRUNCATED_FLAG) !=
					    0) {
						frontend_text_draw(
							MISSION_ACHIEVEMENT_FONT_SIZE,
							"...",
							MISSION_ACHIEVEMENT_TEXT_RIGHT,
							y,
							MISSION_ACHIEVEMENT_TEXT_COLOR);
					}
					sprintf(g_frontend_scratch_buffer, "%d",
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.mp_campaigns
								[g_pilot_record_multiplayer_campaign_mission_list
									 [list_index]
										 .mission_idx]
							.best_score);
					frontend_text_draw(
						MISSION_ACHIEVEMENT_FONT_SIZE,
						g_frontend_scratch_buffer,
						MISSION_ACHIEVEMENT_SCORE_X, y,
						MISSION_ACHIEVEMENT_TEXT_COLOR);
					if (g_pilot_data
						    .faction_statistics
							    [g_pilot_data
								     .current_faction_id]
						    .mp_campaigns
							    [g_pilot_record_multiplayer_campaign_mission_list
								     [list_index]
									     .mission_idx]
						    .is_finished != 0) {
						strcpy(g_frontend_scratch_buffer,
						       frontend_string_get(
							       FRONTSTR_789_FINISHED));
					} else {
						sprintf(g_frontend_scratch_buffer,
							"%s %d",
							frontend_string_get(
								FRONTSTR_788_MIS),
							g_pilot_data
									.faction_statistics
										[g_pilot_data
											 .current_faction_id]
									.mp_campaigns
										[g_pilot_record_multiplayer_campaign_mission_list
											 [list_index]
												 .mission_idx]
									.next_mission_index +
								1);
					}
					frontend_text_draw(
						MISSION_ACHIEVEMENT_FONT_SIZE,
						g_frontend_scratch_buffer,
						MISSION_ACHIEVEMENT_DETAIL_X, y,
						MISSION_ACHIEVEMENT_TEXT_COLOR);
					y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
				}
				++row;
				for (child_list_index = 0;
				     child_list_index <
				     g_pilot_record_multiplayer_training_mission_count;
				     ++child_list_index) {
					award_id =
						g_pilot_record_multiplayer_training_mission_list
							[child_list_index]
								.mission_idx;
					if (g_pilot_data
							    .faction_statistics
								    [g_pilot_data
									     .current_faction_id]
							    .mp_campaign_missions
								    [award_id -
								     1]
							    .number_times_flown !=
						    0 &&
					    g_pilot_data
							    .faction_statistics
								    [g_pilot_data
									     .current_faction_id]
							    .mp_campaign_missions
								    [award_id -
								     1]
							    .campaign_id ==
						    g_pilot_record_multiplayer_campaign_mission_list
							    [list_index]
								    .mission_idx) {
						if (g_pilot_achievements_scroll_offset <=
							    row &&
						    row - g_pilot_achievements_scroll_offset <
							    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
							frontend_display_get_screen_clip_rect(
								&previous_clip_rect);
							frontend_draw_rect_assign(
								&rect,
								MISSION_ACHIEVEMENT_TEXT_X,
								y,
								MISSION_ACHIEVEMENT_TEXT_RIGHT,
								y + MISSION_ACHIEVEMENT_ROW_HEIGHT);
							frontend_display_set_screen_clip_rect640x480(
								&rect);
							draw_flags = frontend_text_draw(
								MISSION_ACHIEVEMENT_FONT_SIZE,
								g_pilot_record_multiplayer_training_mission_list
									[child_list_index]
										.description,
								MISSION_ACHIEVEMENT_TEXT_X,
								y,
								g_color_gray);
							frontend_display_set_screen_clip_rect640x480(
								&previous_clip_rect);
							if ((draw_flags &
							     MISSION_ACHIEVEMENT_TEXT_TRUNCATED_FLAG) !=
							    0) {
								frontend_text_draw(
									MISSION_ACHIEVEMENT_FONT_SIZE,
									"...",
									MISSION_ACHIEVEMENT_TEXT_RIGHT,
									y,
									g_color_gray);
							}
							y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
						}
						++row;
					}
				}
			}
		}
	}

	row = 0;
	y = MISSION_ACHIEVEMENT_FIRST_Y;
	has_previous = 0;
	const char *award_sprite_format;
	if (g_pilot_sp_training_history_count != 0) {
		if (has_previous != 0) {
			if (g_pilot_achievements_scroll_offset <= row &&
			    row - g_pilot_achievements_scroll_offset <
				    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
				y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
			}
			++row;
		}
		has_previous = 1;
		if (g_pilot_achievements_scroll_offset <= row &&
		    row - g_pilot_achievements_scroll_offset <
			    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		if (g_pilot_achievements_scroll_offset <= row &&
		    row - g_pilot_achievements_scroll_offset <
			    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		for (list_index = 0;
		     list_index <
		     g_pilot_record_singleplayer_training_mission_count;
		     ++list_index) {
			if (g_pilot_data
				    .faction_statistics
					    [g_pilot_data.current_faction_id]
				    .sp_training_missions
					    [g_pilot_record_singleplayer_training_mission_list
						     [list_index]
							     .mission_idx]
				    .number_times_flown != 0) {
				if (g_pilot_achievements_scroll_offset <= row &&
				    row - g_pilot_achievements_scroll_offset <
					    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
					award_id =
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.sp_training_missions
								[g_pilot_record_singleplayer_training_mission_list
									 [list_index]
										 .mission_idx]
							.award_level;
					if (award_id != 0) {
						frontend_draw_rect_assign(
							&rect,
							MISSION_ACHIEVEMENT_HEADER_X,
							y,
							MISSION_ACHIEVEMENT_AWARD_RIGHT,
							y + MISSION_ACHIEVEMENT_AWARD_HEIGHT);
						if (g_pilot_data
							    .current_faction_id !=
						    0) {
							award_sprite_format =
								"citlvl%d";
						} else {
							award_sprite_format =
								"rcitlvl%d";
						}
						sprintf(g_frontend_scratch_buffer,
							award_sprite_format,
							award_id);
						front_image_draw_sprite(
							g_frontend_scratch_buffer,
							MISSION_ACHIEVEMENT_HEADER_X,
							y);
						frontend_button_draw_sprite_and_tooltip(
							&rect, NULL,
							frontend_string_get((
								frontend_string_id)(award_id +
										    MISSION_ACHIEVEMENT_CITATION_TOOLTIP_OFFSET)),
							MISSION_ACHIEVEMENT_FONT_SIZE,
							MISSION_ACHIEVEMENT_TEXT_COLOR);
					}
					y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
				}
				++row;
			}
		}
	}
	if (g_pilot_sp_melee_history_count != 0) {
		if (has_previous != 0) {
			if (g_pilot_achievements_scroll_offset <= row &&
			    row - g_pilot_achievements_scroll_offset <
				    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
				y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
			}
			++row;
		}
		has_previous = 1;
		if (g_pilot_achievements_scroll_offset <= row &&
		    row - g_pilot_achievements_scroll_offset <
			    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		if (g_pilot_achievements_scroll_offset <= row &&
		    row - g_pilot_achievements_scroll_offset <
			    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		for (list_index = 0;
		     list_index < g_pilot_record_melee_mission_count;
		     ++list_index) {
			if (g_pilot_data
				    .faction_statistics
					    [g_pilot_data.current_faction_id]
				    .sp_melee_missions
					    [g_pilot_record_melee_mission_list
						     [list_index]
							     .mission_idx]
				    .number_times_flown != 0) {
				if (g_pilot_achievements_scroll_offset <= row &&
				    row - g_pilot_achievements_scroll_offset <
					    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
					award_id =
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.sp_melee_missions
								[g_pilot_record_melee_mission_list
									 [list_index]
										 .mission_idx]
							.award_level;
					if (award_id != 0) {
						frontend_draw_rect_assign(
							&rect,
							MISSION_ACHIEVEMENT_HEADER_X,
							y,
							MISSION_ACHIEVEMENT_AWARD_RIGHT,
							y + MISSION_ACHIEVEMENT_AWARD_HEIGHT);
						sprintf(g_frontend_scratch_buffer,
							"medlvl%d", award_id);
						front_image_draw_sprite(
							g_frontend_scratch_buffer,
							MISSION_ACHIEVEMENT_HEADER_X,
							y);
						frontend_button_draw_sprite_and_tooltip(
							&rect, NULL,
							frontend_string_get((
								frontend_string_id)(award_id +
										    MISSION_ACHIEVEMENT_MEDAL_TOOLTIP_OFFSET)),
							MISSION_ACHIEVEMENT_FONT_SIZE,
							MISSION_ACHIEVEMENT_TEXT_COLOR);
					}
					y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
				}
				++row;
			}
		}
	}
	if (g_pilot_sp_tournament_history_count != 0) {
		if (has_previous != 0) {
			if (g_pilot_achievements_scroll_offset <= row &&
			    row - g_pilot_achievements_scroll_offset <
				    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
				y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
			}
			++row;
		}
		has_previous = 1;
		if (g_pilot_achievements_scroll_offset <= row &&
		    row - g_pilot_achievements_scroll_offset <
			    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		if (g_pilot_achievements_scroll_offset <= row &&
		    row - g_pilot_achievements_scroll_offset <
			    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		for (list_index = 0;
		     list_index < g_pilot_record_tournament_mission_count;
		     ++list_index) {
			if (g_pilot_data
				    .faction_statistics
					    [g_pilot_data.current_faction_id]
				    .sp_tournaments
					    [g_pilot_record_tournament_mission_list
						     [list_index]
							     .mission_idx]
				    .attempt_count != 0) {
				if (g_pilot_achievements_scroll_offset <= row &&
				    row - g_pilot_achievements_scroll_offset <
					    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
					award_id =
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.sp_tournaments
								[g_pilot_record_tournament_mission_list
									 [list_index]
										 .mission_idx]
							.award_level;
					if (award_id != 0) {
						frontend_draw_rect_assign(
							&rect,
							MISSION_ACHIEVEMENT_HEADER_X,
							y,
							MISSION_ACHIEVEMENT_AWARD_RIGHT,
							y + MISSION_ACHIEVEMENT_AWARD_HEIGHT);
						sprintf(g_frontend_scratch_buffer,
							"medlvl%d", award_id);
						front_image_draw_sprite(
							g_frontend_scratch_buffer,
							MISSION_ACHIEVEMENT_HEADER_X,
							y);
						frontend_button_draw_sprite_and_tooltip(
							&rect, NULL,
							frontend_string_get((
								frontend_string_id)(award_id +
										    MISSION_ACHIEVEMENT_MEDAL_TOOLTIP_OFFSET)),
							MISSION_ACHIEVEMENT_FONT_SIZE,
							MISSION_ACHIEVEMENT_TEXT_COLOR);
					}
					y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
				}
				++row;
			}
		}
	}
	if (g_pilot_sp_combat_history_count != 0) {
		if (has_previous != 0) {
			if (g_pilot_achievements_scroll_offset <= row &&
			    row - g_pilot_achievements_scroll_offset <
				    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
				y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
			}
			++row;
		}
		has_previous = 1;
		if (g_pilot_achievements_scroll_offset <= row &&
		    row - g_pilot_achievements_scroll_offset <
			    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		if (g_pilot_achievements_scroll_offset <= row &&
		    row - g_pilot_achievements_scroll_offset <
			    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		for (list_index = 0;
		     list_index <
		     g_pilot_record_singleplayer_combat_mission_count;
		     ++list_index) {
			if (g_pilot_data
				    .faction_statistics
					    [g_pilot_data.current_faction_id]
				    .sp_combat_missions
					    [g_pilot_record_singleplayer_combat_mission_list
						     [list_index]
							     .mission_idx]
				    .number_times_flown != 0) {
				if (g_pilot_achievements_scroll_offset <= row &&
				    row - g_pilot_achievements_scroll_offset <
					    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
					award_id =
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.sp_combat_missions
								[g_pilot_record_singleplayer_combat_mission_list
									 [list_index]
										 .mission_idx]
							.award_level;
					if (award_id != 0) {
						frontend_draw_rect_assign(
							&rect,
							MISSION_ACHIEVEMENT_HEADER_X,
							y,
							MISSION_ACHIEVEMENT_AWARD_RIGHT,
							y + MISSION_ACHIEVEMENT_AWARD_HEIGHT);
						if (g_pilot_data
							    .current_faction_id !=
						    0) {
							award_sprite_format =
								"citlvl%d";
						} else {
							award_sprite_format =
								"rcitlvl%d";
						}
						sprintf(g_frontend_scratch_buffer,
							award_sprite_format,
							award_id);
						front_image_draw_sprite(
							g_frontend_scratch_buffer,
							MISSION_ACHIEVEMENT_HEADER_X,
							y);
						frontend_button_draw_sprite_and_tooltip(
							&rect, NULL,
							frontend_string_get((
								frontend_string_id)(award_id +
										    MISSION_ACHIEVEMENT_CITATION_TOOLTIP_OFFSET)),
							MISSION_ACHIEVEMENT_FONT_SIZE,
							MISSION_ACHIEVEMENT_TEXT_COLOR);
					}
					y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
				}
				++row;
			}
		}
	}
	if (g_pilot_sp_battle_history_count != 0) {
		if (has_previous != 0) {
			if (g_pilot_achievements_scroll_offset <= row &&
			    row - g_pilot_achievements_scroll_offset <
				    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
				y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
			}
			++row;
		}
		has_previous = 1;
		if (g_pilot_achievements_scroll_offset <= row &&
		    row - g_pilot_achievements_scroll_offset <
			    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		if (g_pilot_achievements_scroll_offset <= row &&
		    row - g_pilot_achievements_scroll_offset <
			    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		for (list_index = 0; list_index < g_battle_mission_list_count;
		     ++list_index) {
			if (g_pilot_data
				    .faction_statistics
					    [g_pilot_data.current_faction_id]
				    .sp_battles
					    [g_battle_mission_list[list_index]
						     .mission_idx]
				    .attempt_count != 0) {
				if (g_pilot_achievements_scroll_offset <= row &&
				    row - g_pilot_achievements_scroll_offset <
					    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
					award_id =
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.sp_battles
								[g_battle_mission_list
									 [list_index]
										 .mission_idx]
							.award_level;
					if (award_id != 0) {
						frontend_draw_rect_assign(
							&rect,
							MISSION_ACHIEVEMENT_HEADER_X,
							y,
							MISSION_ACHIEVEMENT_AWARD_RIGHT,
							y + MISSION_ACHIEVEMENT_AWARD_HEIGHT);
						sprintf(g_frontend_scratch_buffer,
							"medlvl%d", award_id);
						front_image_draw_sprite(
							g_frontend_scratch_buffer,
							MISSION_ACHIEVEMENT_HEADER_X,
							y);
						frontend_button_draw_sprite_and_tooltip(
							&rect, NULL,
							frontend_string_get((
								frontend_string_id)(award_id +
										    MISSION_ACHIEVEMENT_MEDAL_TOOLTIP_OFFSET)),
							MISSION_ACHIEVEMENT_FONT_SIZE,
							MISSION_ACHIEVEMENT_TEXT_COLOR);
					}
					y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
				}
				++row;
			}
		}
	}
	if (g_pilot_sp_campaign_history_row_count != 0) {
		if (has_previous != 0) {
			if (g_pilot_achievements_scroll_offset <= row &&
			    row - g_pilot_achievements_scroll_offset <
				    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
				y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
			}
			++row;
		}
		has_previous = 1;
		if (g_pilot_achievements_scroll_offset <= row &&
		    row - g_pilot_achievements_scroll_offset <
			    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		if (g_pilot_achievements_scroll_offset <= row &&
		    row - g_pilot_achievements_scroll_offset <
			    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		for (list_index = 0;
		     list_index <
		     g_pilot_record_singleplayer_campaign_mission_count;
		     ++list_index) {
			if (g_pilot_data
				    .faction_statistics
					    [g_pilot_data.current_faction_id]
				    .sp_campaigns
					    [g_pilot_record_singleplayer_campaign_mission_list
						     [list_index]
							     .mission_idx]
				    .attempt_count != 0) {
				if (g_pilot_achievements_scroll_offset <= row &&
				    row - g_pilot_achievements_scroll_offset <
					    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
					y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
				}
				++row;
				for (child_list_index = 0;
				     child_list_index <
				     g_pilot_record_singleplayer_training_mission_count;
				     ++child_list_index) {
					award_id =
						g_pilot_record_singleplayer_training_mission_list
							[child_list_index]
								.mission_idx;
					if (g_pilot_data
							    .faction_statistics
								    [g_pilot_data
									     .current_faction_id]
							    .sp_campaign_missions
								    [award_id -
								     1]
							    .number_times_flown !=
						    0 &&
					    g_pilot_data
							    .faction_statistics
								    [g_pilot_data
									     .current_faction_id]
							    .sp_campaign_missions
								    [award_id -
								     1]
							    .campaign_id ==
						    g_pilot_record_singleplayer_campaign_mission_list
							    [list_index]
								    .mission_idx) {
						if (g_pilot_achievements_scroll_offset <=
							    row &&
						    row - g_pilot_achievements_scroll_offset <
							    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
							/* This statement turns
							 * award_id from the
							 * mission id into that
							 * mission's award
							 * level. */
							award_id =
								g_pilot_data
									.faction_statistics
										[g_pilot_data
											 .current_faction_id]
									.sp_campaign_missions
										[award_id -
										 1]
									.award_level;
							if (award_id != 0) {
								frontend_draw_rect_assign(
									&rect,
									MISSION_ACHIEVEMENT_HEADER_X,
									y,
									MISSION_ACHIEVEMENT_AWARD_RIGHT,
									y + MISSION_ACHIEVEMENT_AWARD_HEIGHT);
								if (g_pilot_data
									    .current_faction_id !=
								    0) {
									award_sprite_format =
										"citlvl%d";
								} else {
									award_sprite_format =
										"rcitlvl%d";
								}
								sprintf(g_frontend_scratch_buffer,
									award_sprite_format,
									award_id);
								front_image_draw_sprite(
									g_frontend_scratch_buffer,
									MISSION_ACHIEVEMENT_HEADER_X,
									y);
								frontend_button_draw_sprite_and_tooltip(
									&rect,
									NULL,
									frontend_string_get(
										(frontend_string_id)(award_id +
												     659)),
									MISSION_ACHIEVEMENT_FONT_SIZE,
									MISSION_ACHIEVEMENT_TEXT_COLOR);
							}
							y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
						}
						++row;
					}
				}
			}
		}
	}
	if (g_pilot_mp_training_history_count != 0) {
		if (has_previous != 0) {
			if (g_pilot_achievements_scroll_offset <= row &&
			    row - g_pilot_achievements_scroll_offset <
				    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
				y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
			}
			++row;
		}
		has_previous = 1;
		if (g_pilot_achievements_scroll_offset <= row &&
		    row - g_pilot_achievements_scroll_offset <
			    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		if (g_pilot_achievements_scroll_offset <= row &&
		    row - g_pilot_achievements_scroll_offset <
			    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		for (list_index = 0;
		     list_index <
		     g_pilot_record_multiplayer_training_mission_count;
		     ++list_index) {
			if (g_pilot_data
				    .faction_statistics
					    [g_pilot_data.current_faction_id]
				    .mp_training_missions
					    [g_pilot_record_multiplayer_training_mission_list
						     [list_index]
							     .mission_idx]
				    .number_times_flown != 0) {
				if (g_pilot_achievements_scroll_offset <= row &&
				    row - g_pilot_achievements_scroll_offset <
					    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
					award_id =
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.mp_training_missions
								[g_pilot_record_multiplayer_training_mission_list
									 [list_index]
										 .mission_idx]
							.award_level;
					if (award_id != 0) {
						frontend_draw_rect_assign(
							&rect,
							MISSION_ACHIEVEMENT_HEADER_X,
							y,
							MISSION_ACHIEVEMENT_AWARD_RIGHT,
							y + MISSION_ACHIEVEMENT_AWARD_HEIGHT);
						if (g_pilot_data
							    .current_faction_id !=
						    0) {
							award_sprite_format =
								"citlvl%d";
						} else {
							award_sprite_format =
								"rcitlvl%d";
						}
						sprintf(g_frontend_scratch_buffer,
							award_sprite_format,
							award_id);
						front_image_draw_sprite(
							g_frontend_scratch_buffer,
							MISSION_ACHIEVEMENT_HEADER_X,
							y);
						frontend_button_draw_sprite_and_tooltip(
							&rect, NULL,
							frontend_string_get((
								frontend_string_id)(award_id +
										    MISSION_ACHIEVEMENT_CITATION_TOOLTIP_OFFSET)),
							MISSION_ACHIEVEMENT_FONT_SIZE,
							MISSION_ACHIEVEMENT_TEXT_COLOR);
					}
					y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
				}
				++row;
			}
		}
	}
	if (g_pilot_mp_melee_history_count != 0) {
		if (has_previous != 0) {
			if (g_pilot_achievements_scroll_offset <= row &&
			    row - g_pilot_achievements_scroll_offset <
				    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
				y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
			}
			++row;
		}
		has_previous = 1;
		if (g_pilot_achievements_scroll_offset <= row &&
		    row - g_pilot_achievements_scroll_offset <
			    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		if (g_pilot_achievements_scroll_offset <= row &&
		    row - g_pilot_achievements_scroll_offset <
			    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		for (list_index = 0;
		     list_index < g_pilot_record_melee_mission_count;
		     ++list_index) {
			if (g_pilot_data
				    .faction_statistics
					    [g_pilot_data.current_faction_id]
				    .mp_melee_missions
					    [g_pilot_record_melee_mission_list
						     [list_index]
							     .mission_idx]
				    .number_times_flown != 0) {
				if (g_pilot_achievements_scroll_offset <= row &&
				    row - g_pilot_achievements_scroll_offset <
					    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
					award_id =
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.mp_melee_missions
								[g_pilot_record_melee_mission_list
									 [list_index]
										 .mission_idx]
							.award_level;
					if (award_id != 0) {
						frontend_draw_rect_assign(
							&rect,
							MISSION_ACHIEVEMENT_HEADER_X,
							y,
							MISSION_ACHIEVEMENT_AWARD_RIGHT,
							y + MISSION_ACHIEVEMENT_AWARD_HEIGHT);
						sprintf(g_frontend_scratch_buffer,
							"medlvl%d", award_id);
						front_image_draw_sprite(
							g_frontend_scratch_buffer,
							MISSION_ACHIEVEMENT_HEADER_X,
							y);
						frontend_button_draw_sprite_and_tooltip(
							&rect, NULL,
							frontend_string_get((
								frontend_string_id)(award_id +
										    MISSION_ACHIEVEMENT_MEDAL_TOOLTIP_OFFSET)),
							MISSION_ACHIEVEMENT_FONT_SIZE,
							MISSION_ACHIEVEMENT_TEXT_COLOR);
					}
					y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
				}
				++row;
			}
		}
	}
	if (g_pilot_mp_tournament_history_count != 0) {
		if (has_previous != 0) {
			if (g_pilot_achievements_scroll_offset <= row &&
			    row - g_pilot_achievements_scroll_offset <
				    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
				y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
			}
			++row;
		}
		has_previous = 1;
		if (g_pilot_achievements_scroll_offset <= row &&
		    row - g_pilot_achievements_scroll_offset <
			    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		if (g_pilot_achievements_scroll_offset <= row &&
		    row - g_pilot_achievements_scroll_offset <
			    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		for (list_index = 0;
		     list_index < g_pilot_record_tournament_mission_count;
		     ++list_index) {
			if (g_pilot_data
				    .faction_statistics
					    [g_pilot_data.current_faction_id]
				    .mp_tournaments
					    [g_pilot_record_tournament_mission_list
						     [list_index]
							     .mission_idx]
				    .attempt_count != 0) {
				if (g_pilot_achievements_scroll_offset <= row &&
				    row - g_pilot_achievements_scroll_offset <
					    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
					award_id =
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.mp_tournaments
								[g_pilot_record_tournament_mission_list
									 [list_index]
										 .mission_idx]
							.award_level;
					if (award_id != 0) {
						frontend_draw_rect_assign(
							&rect,
							MISSION_ACHIEVEMENT_HEADER_X,
							y,
							MISSION_ACHIEVEMENT_AWARD_RIGHT,
							y + MISSION_ACHIEVEMENT_AWARD_HEIGHT);
						sprintf(g_frontend_scratch_buffer,
							"medlvl%d", award_id);
						front_image_draw_sprite(
							g_frontend_scratch_buffer,
							MISSION_ACHIEVEMENT_HEADER_X,
							y);
						frontend_button_draw_sprite_and_tooltip(
							&rect, NULL,
							frontend_string_get((
								frontend_string_id)(award_id +
										    MISSION_ACHIEVEMENT_MEDAL_TOOLTIP_OFFSET)),
							MISSION_ACHIEVEMENT_FONT_SIZE,
							MISSION_ACHIEVEMENT_TEXT_COLOR);
					}
					y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
				}
				++row;
			}
		}
	}
	if (g_pilot_mp_combat_history_count != 0) {
		if (has_previous != 0) {
			if (g_pilot_achievements_scroll_offset <= row &&
			    row - g_pilot_achievements_scroll_offset <
				    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
				y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
			}
			++row;
		}
		has_previous = 1;
		if (g_pilot_achievements_scroll_offset <= row &&
		    row - g_pilot_achievements_scroll_offset <
			    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		if (g_pilot_achievements_scroll_offset <= row &&
		    row - g_pilot_achievements_scroll_offset <
			    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		for (list_index = 0;
		     list_index <
		     g_pilot_record_multiplayer_combat_mission_count;
		     ++list_index) {
			if (g_pilot_data
				    .faction_statistics
					    [g_pilot_data.current_faction_id]
				    .mp_combat_missions
					    [g_pilot_record_multiplayer_combat_mission_list
						     [list_index]
							     .mission_idx]
				    .number_times_flown != 0) {
				if (g_pilot_achievements_scroll_offset <= row &&
				    row - g_pilot_achievements_scroll_offset <
					    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
					award_id =
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.mp_combat_missions
								[g_pilot_record_multiplayer_combat_mission_list
									 [list_index]
										 .mission_idx]
							.award_level;
					if (award_id != 0) {
						frontend_draw_rect_assign(
							&rect,
							MISSION_ACHIEVEMENT_HEADER_X,
							y,
							MISSION_ACHIEVEMENT_AWARD_RIGHT,
							y + MISSION_ACHIEVEMENT_AWARD_HEIGHT);
						if (g_pilot_data
							    .current_faction_id !=
						    0) {
							award_sprite_format =
								"citlvl%d";
						} else {
							award_sprite_format =
								"rcitlvl%d";
						}
						sprintf(g_frontend_scratch_buffer,
							award_sprite_format,
							award_id);
						front_image_draw_sprite(
							g_frontend_scratch_buffer,
							MISSION_ACHIEVEMENT_HEADER_X,
							y);
						frontend_button_draw_sprite_and_tooltip(
							&rect, NULL,
							frontend_string_get((
								frontend_string_id)(award_id +
										    MISSION_ACHIEVEMENT_CITATION_TOOLTIP_OFFSET)),
							MISSION_ACHIEVEMENT_FONT_SIZE,
							MISSION_ACHIEVEMENT_TEXT_COLOR);
					}
					y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
				}
				++row;
			}
		}
	}
	if (g_pilot_mp_battle_history_count != 0) {
		if (has_previous != 0) {
			if (g_pilot_achievements_scroll_offset <= row &&
			    row - g_pilot_achievements_scroll_offset <
				    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
				y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
			}
			++row;
		}
		has_previous = 1;
		if (g_pilot_achievements_scroll_offset <= row &&
		    row - g_pilot_achievements_scroll_offset <
			    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		if (g_pilot_achievements_scroll_offset <= row &&
		    row - g_pilot_achievements_scroll_offset <
			    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		for (list_index = 0; list_index < g_battle_mission_list_count;
		     ++list_index) {
			if (g_pilot_data
				    .faction_statistics
					    [g_pilot_data.current_faction_id]
				    .mp_battles
					    [g_battle_mission_list[list_index]
						     .mission_idx]
				    .attempt_count != 0) {
				if (g_pilot_achievements_scroll_offset <= row &&
				    row - g_pilot_achievements_scroll_offset <
					    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
					award_id =
						g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.mp_battles
								[g_battle_mission_list
									 [list_index]
										 .mission_idx]
							.award_level;
					if (award_id != 0) {
						frontend_draw_rect_assign(
							&rect,
							MISSION_ACHIEVEMENT_HEADER_X,
							y,
							MISSION_ACHIEVEMENT_AWARD_RIGHT,
							y + MISSION_ACHIEVEMENT_AWARD_HEIGHT);
						sprintf(g_frontend_scratch_buffer,
							"medlvl%d", award_id);
						front_image_draw_sprite(
							g_frontend_scratch_buffer,
							MISSION_ACHIEVEMENT_HEADER_X,
							y);
						frontend_button_draw_sprite_and_tooltip(
							&rect, NULL,
							frontend_string_get((
								frontend_string_id)(award_id +
										    MISSION_ACHIEVEMENT_MEDAL_TOOLTIP_OFFSET)),
							MISSION_ACHIEVEMENT_FONT_SIZE,
							MISSION_ACHIEVEMENT_TEXT_COLOR);
					}
					y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
				}
				++row;
			}
		}
	}
	if (g_pilot_mp_campaign_history_row_count != 0) {
		if (has_previous != 0) {
			if (g_pilot_achievements_scroll_offset <= row &&
			    row - g_pilot_achievements_scroll_offset <
				    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
				y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
			}
			++row;
		}
		has_previous = 1;
		if (g_pilot_achievements_scroll_offset <= row &&
		    row - g_pilot_achievements_scroll_offset <
			    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		if (g_pilot_achievements_scroll_offset <= row &&
		    row - g_pilot_achievements_scroll_offset <
			    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
			y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
		}
		++row;
		for (list_index = 0;
		     list_index <
		     g_pilot_record_multiplayer_campaign_mission_count;
		     ++list_index) {
			if (g_pilot_data
				    .faction_statistics
					    [g_pilot_data.current_faction_id]
				    .mp_campaigns
					    [g_pilot_record_multiplayer_campaign_mission_list
						     [list_index]
							     .mission_idx]
				    .attempt_count != 0) {
				if (g_pilot_achievements_scroll_offset <= row &&
				    row - g_pilot_achievements_scroll_offset <
					    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
					y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
				}
				++row;
				for (child_list_index = 0;
				     child_list_index <
				     g_pilot_record_multiplayer_training_mission_count;
				     ++child_list_index) {
					award_id =
						g_pilot_record_multiplayer_training_mission_list
							[child_list_index]
								.mission_idx;
					if (g_pilot_data
							    .faction_statistics
								    [g_pilot_data
									     .current_faction_id]
							    .mp_campaign_missions
								    [award_id -
								     1]
							    .number_times_flown !=
						    0 &&
					    g_pilot_data
							    .faction_statistics
								    [g_pilot_data
									     .current_faction_id]
							    .mp_campaign_missions
								    [award_id -
								     1]
							    .campaign_id ==
						    g_pilot_record_multiplayer_campaign_mission_list
							    [list_index]
								    .mission_idx) {
						if (g_pilot_achievements_scroll_offset <=
							    row &&
						    row - g_pilot_achievements_scroll_offset <
							    MISSION_ACHIEVEMENT_VISIBLE_ROWS) {
							award_id =
								g_pilot_data
									.faction_statistics
										[g_pilot_data
											 .current_faction_id]
									.mp_campaign_missions
										[award_id -
										 1]
									.award_level;
							if (award_id != 0) {
								frontend_draw_rect_assign(
									&rect,
									MISSION_ACHIEVEMENT_HEADER_X,
									y,
									MISSION_ACHIEVEMENT_AWARD_RIGHT,
									y + MISSION_ACHIEVEMENT_AWARD_HEIGHT);
								if (g_pilot_data
									    .current_faction_id !=
								    0) {
									award_sprite_format =
										"citlvl%d";
								} else {
									award_sprite_format =
										"rcitlvl%d";
								}
								sprintf(g_frontend_scratch_buffer,
									award_sprite_format,
									award_id);
								front_image_draw_sprite(
									g_frontend_scratch_buffer,
									MISSION_ACHIEVEMENT_HEADER_X,
									y);
								frontend_button_draw_sprite_and_tooltip(
									&rect,
									NULL,
									frontend_string_get(
										(frontend_string_id)(award_id +
												     659)),
									MISSION_ACHIEVEMENT_FONT_SIZE,
									MISSION_ACHIEVEMENT_TEXT_COLOR);
							}
							y += MISSION_ACHIEVEMENT_ROW_HEIGHT;
						}
						++row;
					}
				}
			}
		}
	}
	return 1;
}

/* Draws the cutscene page: for each campaign the current faction attempted,
 * single player or multiplayer, its title and each of its cutscenes the pilot
 * has unlocked, with a thumbnail: one not marked play_after_debriefing once its
 * campaign mission was flown, one marked so once that mission was completed. It
 * stops drawing once y passes 433. Clicking a thumbnail suspends CD audio,
 * clears the screen, plays the cutscene's movie, redraws the background and
 * resumes the audio; the modern build returns 1 at once when
 * xvt_frontend_movies_play_viewer returns nonzero. When
 * g_pilot_record_pages_need_rebuild is set it first reloads the campaign list into
 * g_pilot_record_singleplayer_campaign_mission_list, cuts each description at its
 * last '(', counts g_cutscene_viewer_total_rows, sets g_cutscene_viewer_scroll_row to
 * 0 and clears the flag; a scroll bar sets the row past 21 rows. Returns 0 when
 * no pilot is loaded or g_cutscene_table is NULL, else 1. */
// FUNCTION: XVT 0x4C7CC0
int pilot_record_draw_cutscene_viewer_page(void)
{
	int mouse_x;
	int mouse_y;

	frontend_cursor_get_pos(&mouse_x, &mouse_y);
	struct RECT text_rect;
	frontend_draw_rect_assign(&text_rect, 84, 90, 404, 106);
	if (g_pilot_data.name[0] == '\0') {
		sprintf(g_frontend_scratch_buffer, "%s",
			frontend_string_get(FRONTSTR_796_VIEW_CUTSCENES));
	} else {
		sprintf(g_frontend_scratch_buffer, "%s: %c%s %c%s",
			frontend_string_get(FRONTSTR_796_VIEW_CUTSCENES), 6,
			g_pilot_data.rating_name, 4, g_pilot_data.name);
	}
	frontend_text_draw_centered(15, g_frontend_scratch_buffer, &text_rect,
				    0xFFFF);
	if (g_pilot_data.name[0] == '\0') {
		return 0;
	}
	if (g_cutscene_table == NULL) {
		return 0;
	}

	struct RECT rect;
	int campaign_index;
	unsigned int cutscene_index;
	int has_campaign_cutscene;
	if (g_pilot_record_pages_need_rebuild != 0) {
		if (g_pilot_record_singleplayer_campaign_mission_list != NULL) {
			free(g_pilot_record_singleplayer_campaign_mission_list);
			g_pilot_record_singleplayer_campaign_mission_list =
				NULL;
		}
		mission_setup_load_mission_list(MISSION_DIRECTORY_CAMPAIGNS);
		g_pilot_record_singleplayer_campaign_mission_list =
			g_mission_list;
		g_pilot_record_singleplayer_campaign_mission_count =
			g_mission_count;
		g_mission_list = NULL;
		for (campaign_index = 0;
		     campaign_index <
		     g_pilot_record_singleplayer_campaign_mission_count;
		     ++campaign_index) {
			unsigned int search_index =
				(unsigned int)strlen(
					g_pilot_record_singleplayer_campaign_mission_list
						[campaign_index]
							.description) +
				1;
			search_index -= 2;
			if (g_pilot_record_singleplayer_campaign_mission_list
				    [campaign_index]
					    .description[0] == '\0') {
				XVT_LOG_WARN(
					"pilot.campaign_title_empty page=\"cutscenes\" index=%d",
					campaign_index);
			}
			if (search_index != 0) {
				struct mission_list_entry *campaign_entry =
					&g_pilot_record_singleplayer_campaign_mission_list
						[campaign_index];
				do {
					if (campaign_entry->description
						    [search_index] == '(') {
						campaign_entry->description
							[search_index] = '\0';
						break;
					}
					--search_index;
				} while (search_index != 0);
			}
		}

		g_cutscene_viewer_scroll_row = 0;
		g_cutscene_viewer_total_rows = 0;
		for (campaign_index = 0;
		     campaign_index <
		     g_pilot_record_singleplayer_campaign_mission_count;
		     ++campaign_index) {
			if (g_pilot_data
					    .faction_statistics
						    [g_pilot_data
							     .current_faction_id]
					    .sp_campaigns
						    [g_pilot_record_singleplayer_campaign_mission_list
							     [campaign_index]
								     .mission_idx]
					    .attempt_count == 0 &&
			    g_pilot_data
					    .faction_statistics
						    [g_pilot_data
							     .current_faction_id]
					    .mp_campaigns
						    [g_pilot_record_singleplayer_campaign_mission_list
							     [campaign_index]
								     .mission_idx]
					    .attempt_count == 0) {
				continue;
			}
			has_campaign_cutscene = 0;
			for (cutscene_index = 0;
			     cutscene_index < (unsigned int)g_cutscene_count;
			     ++cutscene_index) {
				if (g_pilot_record_singleplayer_campaign_mission_list
					    [campaign_index]
						    .mission_idx !=
				    g_cutscene_table[cutscene_index]
					    .campaign_id) {
					continue;
				}
				if (g_cutscene_table[cutscene_index]
					    .play_after_debriefing == 0) {
					if (g_pilot_data
							    .faction_statistics
								    [g_pilot_data
									     .current_faction_id]
							    .sp_campaign_missions
								    [g_cutscene_table[cutscene_index]
									     .campaign_mission_id -
								     1]
							    .number_times_flown ==
						    0 &&
					    g_pilot_data
							    .faction_statistics
								    [g_pilot_data
									     .current_faction_id]
							    .mp_campaign_missions
								    [g_cutscene_table[cutscene_index]
									     .campaign_mission_id -
								     1]
							    .number_times_flown ==
						    0) {
						continue;
					}
				} else if (
					g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.sp_campaign_missions
								[g_cutscene_table[cutscene_index]
									 .campaign_mission_id -
								 1]
							.is_completed == 0 &&
					g_pilot_data
							.faction_statistics
								[g_pilot_data
									 .current_faction_id]
							.mp_campaign_missions
								[g_cutscene_table[cutscene_index]
									 .campaign_mission_id -
								 1]
							.is_completed == 0) {
					continue;
				}
				if (has_campaign_cutscene == 0) {
					has_campaign_cutscene = 1;
					g_cutscene_viewer_total_rows += 2;
				}
				++g_cutscene_viewer_total_rows;
				front_image_get_resource_rect(
					g_cutscene_table[cutscene_index]
						.thumbnail_sprite,
					&rect);
				g_cutscene_viewer_total_rows +=
					(unsigned int)(rect.bottom - rect.top +
						       CUTSCENE_VIEWER_ROW_HEIGHT +
						       3) /
					CUTSCENE_VIEWER_ROW_HEIGHT;
			}
		}
		g_pilot_record_pages_need_rebuild = 0;
		XVT_LOG_DEBUG(
			"pilot.cutscenes_counted faction=%d campaigns=%d rows=%d cutscenes=%d",
			g_pilot_data.current_faction_id,
			g_pilot_record_singleplayer_campaign_mission_count,
			g_cutscene_viewer_total_rows, g_cutscene_count);
	}

	frontend_draw_rect_assign(&text_rect, 425, 107, 434, 433);
	if ((unsigned int)g_cutscene_viewer_total_rows >
	    CUTSCENE_VIEWER_VISIBLE_ROW_COUNT) {
		g_cutscene_viewer_scroll_row = frontend_scrollbar_draw(
			&text_rect, g_cutscene_viewer_scroll_row,
			g_cutscene_viewer_total_rows, 0, 5,
			(unsigned int)g_color_navy, 11);
	}
	struct RECT previous_clip_rect;
	frontend_display_get_screen_clip_rect(&previous_clip_rect);
	frontend_draw_rect_assign(&text_rect, 88, 111, 424, 433);
	frontend_display_set_screen_clip_rect640x480(&text_rect);
	int selected_cutscene = 0;
	int y = 111 - CUTSCENE_VIEWER_ROW_HEIGHT * g_cutscene_viewer_scroll_row;
	frontend_draw_rect_assign(&text_rect, 88, y, 424,
				  y + CUTSCENE_VIEWER_ROW_HEIGHT - 1);
	for (campaign_index = 0;
	     campaign_index <
	     g_pilot_record_singleplayer_campaign_mission_count;
	     ++campaign_index) {
		if (g_pilot_data.faction_statistics[g_pilot_data
							    .current_faction_id]
				    .sp_campaigns
					    [g_pilot_record_singleplayer_campaign_mission_list
						     [campaign_index]
							     .mission_idx]
				    .attempt_count == 0 &&
		    g_pilot_data.faction_statistics[g_pilot_data
							    .current_faction_id]
				    .mp_campaigns
					    [g_pilot_record_singleplayer_campaign_mission_list
						     [campaign_index]
							     .mission_idx]
				    .attempt_count == 0) {
			continue;
		}
		has_campaign_cutscene = 0;
		for (cutscene_index = 0;
		     cutscene_index < (unsigned int)g_cutscene_count;
		     ++cutscene_index) {
			if (g_pilot_record_singleplayer_campaign_mission_list
				    [campaign_index]
					    .mission_idx !=
			    g_cutscene_table[cutscene_index].campaign_id) {
				continue;
			}
			if (g_cutscene_table[cutscene_index]
				    .play_after_debriefing == 0) {
				if (g_pilot_data
						    .faction_statistics
							    [g_pilot_data
								     .current_faction_id]
						    .sp_campaign_missions
							    [g_cutscene_table[cutscene_index]
								     .campaign_mission_id -
							     1]
						    .number_times_flown == 0 &&
				    g_pilot_data
						    .faction_statistics
							    [g_pilot_data
								     .current_faction_id]
						    .mp_campaign_missions
							    [g_cutscene_table[cutscene_index]
								     .campaign_mission_id -
							     1]
						    .number_times_flown == 0) {
					continue;
				}
			} else if (
				g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.sp_campaign_missions
							[g_cutscene_table[cutscene_index]
								 .campaign_mission_id -
							 1]
						.is_completed == 0 &&
				g_pilot_data
						.faction_statistics
							[g_pilot_data
								 .current_faction_id]
						.mp_campaign_missions
							[g_cutscene_table[cutscene_index]
								 .campaign_mission_id -
							 1]
						.is_completed == 0) {
				continue;
			}
			if (has_campaign_cutscene == 0) {
				has_campaign_cutscene = 1;
				frontend_text_draw_centered(
					12,
					g_pilot_record_singleplayer_campaign_mission_list
						[campaign_index]
							.description,
					&text_rect, g_color_yellow);
				y += 30;
				frontend_draw_rect_offset_xy(&text_rect, 0, 30);
			}
			frontend_text_draw_centered(
				12,
				g_cutscene_table[cutscene_index].description,
				&text_rect, 0xFFFF);
			y += CUTSCENE_VIEWER_ROW_HEIGHT;
			frontend_draw_rect_offset_xy(
				&text_rect, 0, CUTSCENE_VIEWER_ROW_HEIGHT);
			front_image_get_resource_rect(
				g_cutscene_table[cutscene_index]
					.thumbnail_sprite,
				&rect);
			int thumbnail_x =
				256 -
				((unsigned int)(rect.right - rect.left + 1) >>
				 1);
			int thumbnail_height =
				CUTSCENE_VIEWER_ROW_HEIGHT *
				((unsigned int)(rect.bottom - rect.top +
						CUTSCENE_VIEWER_ROW_HEIGHT +
						3) /
				 CUTSCENE_VIEWER_ROW_HEIGHT);
			front_image_draw_sprite_opaque(
				g_cutscene_table[cutscene_index]
					.thumbnail_sprite,
				thumbnail_x, y + 3);
			frontend_draw_rect_offset_xy(&rect, thumbnail_x, y + 3);
			if (frontend_draw_point_in_rect(&rect, mouse_x,
							mouse_y)) {
				frontend_draw_rect_outline(&rect, 0, 0,
							   g_color_green);
				if (frontend_button_handle_sprite_button(
					    &rect, "", "", NULL, 15, 0xFFFF, 36,
					    "jewelsound") != 0) {
					selected_cutscene =
						(int)cutscene_index + 1;
				}
			}
			y += thumbnail_height;
			frontend_draw_rect_offset_xy(&text_rect, 0,
						     thumbnail_height);
			if (y > 433) {
				break;
			}
		}
		if (y > 433) {
			break;
		}
	}
	frontend_display_set_screen_clip_rect640x480(&previous_clip_rect);
	if (selected_cutscene != 0) {
		XVT_LOG_DEBUG(
			"pilot.cutscene_chosen cutscene=%d campaign=%d",
			selected_cutscene - 1,
			g_cutscene_table[selected_cutscene - 1].campaign_id);
		cd_audio_suspend_playback();
		frontend_display_disable_offscreen_restore();
		frontend_display_unlock_back_buffer();
		frontend_display_clear_back_buffer();
		frontend_display_present_frame();
		frontend_display_clear_back_buffer();
		if (xvt_frontend_movies_play_viewer(
			    g_cutscene_table[selected_cutscene - 1]
				    .movie_name)) {
			return 1;
		}
		frontend_display_clear_back_buffer();
		frontend_display_present_frame();
		frontend_display_clear_back_buffer();
		frontend_display_enable_offscreen_restore();
		pilot_record_redraw_background();
		cd_audio_request_resume_playback();
	}
	return 1;
}

/* Draws the campaign medals page: one campaign at a time, at
 * g_campaign_medal_scroll_offset among the campaigns the current faction attempted
 * that have a record in g_campaign_award_sprites, with its title, its main medal
 * sprite centered on (256, 279), and on it the mission award sprites of
 * positions 0 to 14 set in g_campaign_singleplayer_award_flags and
 * g_campaign_multiplayer_award_flags. When g_pilot_record_pages_need_rebuild is set it
 * first reloads the campaign list and the single-player and multiplayer
 * training lists, the last with g_frontend_mission_session_mode set to NET_HOST
 * and then left at NONE, cuts the campaign descriptions at their last '(',
 * rebuilds the award flags and counts and g_campaign_medal_entry_count, sets
 * g_campaign_medal_scroll_offset to 0 and clears the flag. Returns 0 when no pilot
 * is loaded or g_campaign_award_sprites is NULL, else 1. The flags are not
 * cleared between campaigns, so the medal shown carries the award positions of
 * every attempted campaign that has a record, and the counts are those of the
 * last such campaign. */
// FUNCTION: XVT 0x4C83A0
int pilot_record_draw_campaign_medals_page(void)
{
	struct RECT rect;

	frontend_draw_rect_assign(&rect, 84, 90, 404, 106);
	if (g_pilot_data.name[0] == '\0') {
		sprintf(g_frontend_scratch_buffer, "%s",
			frontend_string_get(FRONTSTR_826_CAMPAIGN_MEDALS));
	} else {
		sprintf(g_frontend_scratch_buffer, "%s: %c%s %c%s",
			frontend_string_get(FRONTSTR_826_CAMPAIGN_MEDALS), 6,
			g_pilot_data.rating_name, 4, g_pilot_data.name);
	}
	frontend_text_draw_centered(15, g_frontend_scratch_buffer, &rect,
				    0xFFFF);
	if (g_pilot_data.name[0] == '\0') {
		return 0;
	}
	if (g_campaign_award_sprites == NULL) {
		return 0;
	}

	int *campaign_id;
	unsigned int campaign_index;
	unsigned int training_mission_index;
	unsigned int award_sprite_index;
	unsigned int award_index;
	int campaign_mission_id;
	if (g_pilot_record_pages_need_rebuild != 0) {
		if (g_pilot_record_singleplayer_campaign_mission_list != NULL) {
			free(g_pilot_record_singleplayer_campaign_mission_list);
			g_pilot_record_singleplayer_campaign_mission_list =
				NULL;
		}
		if (g_pilot_record_singleplayer_training_mission_list != NULL) {
			free(g_pilot_record_singleplayer_training_mission_list);
			g_pilot_record_singleplayer_training_mission_list =
				NULL;
		}
		if (g_pilot_record_multiplayer_training_mission_list != NULL) {
			free(g_pilot_record_multiplayer_training_mission_list);
			g_pilot_record_multiplayer_training_mission_list = NULL;
		}

		mission_setup_load_mission_list(MISSION_DIRECTORY_CAMPAIGNS);
		g_pilot_record_singleplayer_campaign_mission_list =
			g_mission_list;
		g_pilot_record_singleplayer_campaign_mission_count =
			g_mission_count;
		g_mission_list = NULL;
		mission_setup_load_mission_list(
			MISSION_DIRECTORY_TRAINING_EXERCISES);
		g_pilot_record_singleplayer_training_mission_list =
			g_mission_list;
		g_pilot_record_singleplayer_training_mission_count =
			g_mission_count;
		g_frontend_mission_session_mode =
			FRONTEND_MISSION_SESSION_NET_HOST;
		g_mission_list = NULL;
		mission_setup_load_mission_list(
			MISSION_DIRECTORY_TRAINING_EXERCISES);
		g_frontend_mission_session_mode = FRONTEND_MISSION_SESSION_NONE;
		g_pilot_record_multiplayer_training_mission_list =
			g_mission_list;
		g_pilot_record_multiplayer_training_mission_count =
			g_mission_count;
		g_mission_list = NULL;

		memset(g_campaign_singleplayer_award_flags, 0,
		       sizeof(g_campaign_singleplayer_award_flags));
		memset(g_campaign_multiplayer_award_flags, 0,
		       sizeof(g_campaign_multiplayer_award_flags));
		campaign_index = 0;
		if (campaign_index <
		    g_pilot_record_singleplayer_campaign_mission_count) {
			do {
				unsigned int search_index =
					(unsigned int)strlen(
						g_pilot_record_singleplayer_campaign_mission_list
							[campaign_index]
								.description) +
					1;
				search_index -= 2;
				if (g_pilot_record_singleplayer_campaign_mission_list
					    [campaign_index]
						    .description[0] == '\0') {
					XVT_LOG_WARN(
						"pilot.campaign_title_empty page=\"medals\" index=%d",
						(int)campaign_index);
				}
				if (search_index != 0) {
					struct mission_list_entry *campaign_entry =
						&g_pilot_record_singleplayer_campaign_mission_list
							[campaign_index];
					do {
						if (campaign_entry->description
							    [search_index] ==
						    '(') {
							campaign_entry->description
								[search_index] =
								'\0';
							break;
						}
						--search_index;
					} while (search_index != 0);
				}
				++campaign_index;
			} while (
				campaign_index <
				g_pilot_record_singleplayer_campaign_mission_count);
		}

		g_campaign_medal_scroll_offset = 0;
		g_campaign_medal_entry_count = 0;
		unsigned int remaining_campaign_count =
			g_pilot_record_singleplayer_campaign_mission_count;
		campaign_index = 0;
		if (remaining_campaign_count) {
			do {
				campaign_id =
					&g_pilot_record_singleplayer_campaign_mission_list
						 [campaign_index]
							 .mission_idx;
				if (g_pilot_data.faction_statistics
						    [g_pilot_data
							     .current_faction_id]
							    .sp_campaigns
								    [*campaign_id]
							    .attempt_count !=
					    0 ||
				    g_pilot_data.faction_statistics
						    [g_pilot_data
							     .current_faction_id]
							    .mp_campaigns
								    [*campaign_id]
							    .attempt_count !=
					    0) {
					for (award_sprite_index = 0;
					     award_sprite_index <
					     g_campaign_award_sprite_count;
					     ++award_sprite_index) {
						if (g_campaign_award_sprites
							    [award_sprite_index]
								    .campaign_id ==
						    *campaign_id) {
							break;
						}
					}
					if (award_sprite_index !=
					    g_campaign_award_sprite_count) {
						award_index = 0;
						g_campaign_singleplayer_award_count =
							0;
						for (training_mission_index = 0;
						     training_mission_index <
						     g_pilot_record_singleplayer_training_mission_count;
						     ++training_mission_index) {
							campaign_mission_id =
								g_pilot_record_singleplayer_training_mission_list
									[training_mission_index]
										.mission_idx;
							if (g_pilot_data
									    .faction_statistics
										    [g_pilot_data
											     .current_faction_id]
									    .sp_campaign_missions
										    [campaign_mission_id -
										     1]
									    .campaign_id ==
								    *campaign_id &&
							    g_pilot_data
									    .faction_statistics
										    [g_pilot_data
											     .current_faction_id]
									    .sp_campaign_missions
										    [campaign_mission_id -
										     1]
									    .number_times_flown !=
								    0) {
								if (g_pilot_data
									    .faction_statistics
										    [g_pilot_data
											     .current_faction_id]
									    .sp_campaign_missions
										    [campaign_mission_id -
										     1]
									    .award_eligible !=
								    0) {
									g_campaign_singleplayer_award_flags
										[award_index] =
											1;
									++g_campaign_singleplayer_award_count;
								}
								++award_index;
								if (award_index >=
								    sizeof(g_campaign_singleplayer_award_flags) /
									    sizeof(g_campaign_singleplayer_award_flags
											   [0])) {
									break;
								}
							}
						}

						award_index = 0;
						g_campaign_multiplayer_award_count =
							0;
						for (training_mission_index = 0;
						     training_mission_index <
						     g_pilot_record_multiplayer_training_mission_count;
						     ++training_mission_index) {
							campaign_mission_id =
								g_pilot_record_multiplayer_training_mission_list
									[training_mission_index]
										.mission_idx;
							if (g_pilot_data
									    .faction_statistics
										    [g_pilot_data
											     .current_faction_id]
									    .mp_campaign_missions
										    [campaign_mission_id -
										     1]
									    .campaign_id ==
								    *campaign_id &&
							    g_pilot_data
									    .faction_statistics
										    [g_pilot_data
											     .current_faction_id]
									    .mp_campaign_missions
										    [campaign_mission_id -
										     1]
									    .number_times_flown !=
								    0) {
								if (g_pilot_data
									    .faction_statistics
										    [g_pilot_data
											     .current_faction_id]
									    .mp_campaign_missions
										    [campaign_mission_id -
										     1]
									    .award_eligible !=
								    0) {
									g_campaign_multiplayer_award_flags
										[award_index] =
											1;
									++g_campaign_multiplayer_award_count;
								}
								++award_index;
								if (award_index >=
								    sizeof(g_campaign_multiplayer_award_flags) /
									    sizeof(g_campaign_multiplayer_award_flags
											   [0])) {
									break;
								}
							}
						}
						++g_campaign_medal_entry_count;
					}
				}
				++campaign_index;
			} while (--remaining_campaign_count != 0);
		}
		g_pilot_record_pages_need_rebuild = 0;
		XVT_LOG_DEBUG(
			"pilot.medals_counted faction=%d campaigns=%d entries=%d solo_awards=%d network_awards=%d",
			g_pilot_data.current_faction_id,
			g_pilot_record_singleplayer_campaign_mission_count,
			g_campaign_medal_entry_count,
			g_campaign_singleplayer_award_count,
			g_campaign_multiplayer_award_count);
	}

	frontend_draw_rect_assign(&rect, 425, 107, 434, 433);
	if ((unsigned int)g_campaign_medal_entry_count > 1) {
		g_campaign_medal_scroll_offset = frontend_scrollbar_draw(
			&rect, g_campaign_medal_scroll_offset,
			g_campaign_medal_entry_count, 0, 5,
			(unsigned int)g_color_navy, 12);
	}

	int eligible_campaign_index = 0;
	campaign_index = 0;
	struct RECT award_rect;
	if (g_pilot_record_singleplayer_campaign_mission_count != 0) {
		do {
			campaign_id =
				&g_pilot_record_singleplayer_campaign_mission_list
					 [campaign_index]
						 .mission_idx;
			if (g_pilot_data.faction_statistics
					    [g_pilot_data.current_faction_id]
						    .sp_campaigns[*campaign_id]
						    .attempt_count != 0 ||
			    g_pilot_data.faction_statistics
					    [g_pilot_data.current_faction_id]
						    .mp_campaigns[*campaign_id]
						    .attempt_count != 0) {
				for (award_sprite_index = 0;
				     award_sprite_index <
				     g_campaign_award_sprite_count;
				     ++award_sprite_index) {
					if (g_campaign_award_sprites
						    [award_sprite_index]
							    .campaign_id ==
					    *campaign_id) {
						break;
					}
				}
				if (award_sprite_index !=
				    g_campaign_award_sprite_count) {
					if (eligible_campaign_index ==
					    g_campaign_medal_scroll_offset) {
						frontend_draw_rect_assign(
							&rect, 88, 111, 424,
							125);
						frontend_text_draw_centered(
							12,
							g_pilot_record_singleplayer_campaign_mission_list
								[campaign_index]
									.description,
							&rect, g_color_yellow);
						front_image_get_resource_rect(
							g_campaign_award_sprites
								[award_sprite_index]
									.main_award_sprite_name,
							&award_rect);
						int award_x =
							256 -
							((award_rect.right -
							  award_rect.left +
							  1) >>
							 1);
						int award_y =
							279 -
							((award_rect.bottom -
							  award_rect.top + 1) >>
							 1);
						front_image_draw_sprite(
							g_campaign_award_sprites
								[award_sprite_index]
									.main_award_sprite_name,
							award_x, award_y);
						if (g_campaign_singleplayer_award_count !=
						    0) {
							for (award_index = 0;
							     award_index <
							     CAMPAIGN_AWARD_VISIBLE_FLAG_COUNT;
							     ++award_index) {
								if (g_campaign_singleplayer_award_flags
									    [award_index] !=
								    0) {
									front_image_draw_sprite(
										g_campaign_award_sprites[award_sprite_index]
											.singleplayer_mission_award_sprite_names
												[award_index],
										award_x,
										award_y);
								}
							}
						}
						if (g_campaign_multiplayer_award_count !=
						    0) {
							for (award_index = 0;
							     award_index <
							     CAMPAIGN_AWARD_VISIBLE_FLAG_COUNT;
							     ++award_index) {
								if (g_campaign_multiplayer_award_flags
									    [award_index] !=
								    0) {
									front_image_draw_sprite(
										g_campaign_award_sprites[award_sprite_index]
											.multiplayer_mission_award_sprite_names
												[award_index],
										award_x,
										award_y);
								}
							}
						}
						break;
					}
					++eligible_campaign_index;
				}
			}
			++campaign_index;
		} while (campaign_index <
			 g_pilot_record_singleplayer_campaign_mission_count);
	}
	return 1;
}

/* Draws the pilot awards page for the current faction: for tournament trophies,
 * melee plaques, battle medallions and mission evaluations, each of award
 * levels 1 to 6 the pilot holds, as its sprite and count with a tooltip naming
 * the level. Returns 0, having drawn only the title, when no pilot is loaded;
 * else 1. */
// FUNCTION: XVT 0x4C8990
int pilot_record_draw_pilot_awards_page(void)
{
	struct RECT rect;

	frontend_draw_rect_assign(&rect, 84, 90, 404, 106);
	if (g_pilot_data.name[0] == '\0') {
		sprintf(g_frontend_scratch_buffer, "%s",
			frontend_string_get(FRONTSTR_009_PILOT_AWARDS));
	} else {
		sprintf(g_frontend_scratch_buffer, "%s: %c%s %c%s",
			frontend_string_get(FRONTSTR_009_PILOT_AWARDS), 6,
			g_pilot_data.rating_name, 4, g_pilot_data.name);
	}
	frontend_text_draw_centered(15, g_frontend_scratch_buffer, &rect,
				    0xFFFF);
	if (g_pilot_data.name[0] == '\0') {
		return 0;
	}
	frontend_draw_rect_assign(&rect, 268, 118, 424, 132);
	frontend_text_draw_centered(
		12, frontend_string_get(FRONTSTR_379_TOURNAMENT_TROPHY), &rect,
		0xFFFF);
	int y = 133;
	frontend_draw_rect_assign(&rect, 353, 133, 387, 146);
	int award_index;
	for (award_index = 0; award_index < 6; ++award_index) {
		if (g_pilot_data
			    .faction_statistics[g_pilot_data.current_faction_id]
			    .tournament_trophies[award_index] != 0) {
			sprintf(g_frontend_scratch_buffer, "medlvl%d",
				(int)award_index + 1);
			front_image_draw_sprite(g_frontend_scratch_buffer, 353,
						y);
			sprintf(g_frontend_scratch_buffer, "%d",
				g_pilot_data
					.faction_statistics
						[g_pilot_data
							 .current_faction_id]
					.tournament_trophies[award_index]);
			frontend_text_draw(10, g_frontend_scratch_buffer, 403,
					   y + 1, 0xFFFF);
		}
		y += 22;
		frontend_draw_rect_offset_xy(&rect, 0, 22);
	}

	frontend_draw_rect_assign(&rect, 97, 118, 253, 132);
	frontend_text_draw_centered(
		12, frontend_string_get(FRONTSTR_378_MELEE_PLAQUE), &rect,
		0xFFFF);
	y = 133;
	frontend_draw_rect_assign(&rect, 182, 133, 216, 146);
	for (award_index = 0; award_index < 6; ++award_index) {
		if (g_pilot_data
			    .faction_statistics[g_pilot_data.current_faction_id]
			    .melee_plaques[award_index] != 0) {
			sprintf(g_frontend_scratch_buffer, "medlvl%d",
				(int)award_index + 1);
			front_image_draw_sprite(g_frontend_scratch_buffer, 182,
						y);
			sprintf(g_frontend_scratch_buffer, "%d",
				g_pilot_data
					.faction_statistics
						[g_pilot_data
							 .current_faction_id]
					.melee_plaques[award_index]);
			frontend_text_draw(10, g_frontend_scratch_buffer, 232,
					   y + 1, 0xFFFF);
		}
		y += 22;
		frontend_draw_rect_offset_xy(&rect, 0, 22);
	}

	frontend_draw_rect_assign(&rect, 268, 269, 424, 283);
	frontend_text_draw_centered(
		12, frontend_string_get(FRONTSTR_381_BATTLE_MEDALLION), &rect,
		0xFFFF);
	y = 284;
	frontend_draw_rect_assign(&rect, 353, 284, 387, 297);
	int faction_id;
	for (award_index = 0; award_index < 6; ++award_index) {
		if (g_pilot_data
			    .faction_statistics[g_pilot_data.current_faction_id]
			    .battle_medallions[award_index] != 0) {
			if (g_pilot_data.current_faction_id == 0) {
				faction_id = g_pilot_data.current_faction_id;
			}
			sprintf(g_frontend_scratch_buffer, "medlvl%d",
				(int)award_index + 1);
			front_image_draw_sprite(g_frontend_scratch_buffer, 353,
						y);
			faction_id = g_pilot_data.current_faction_id;
			sprintf(g_frontend_scratch_buffer, "%d",
				g_pilot_data.faction_statistics[faction_id]
					.battle_medallions[award_index]);
			frontend_text_draw(10, g_frontend_scratch_buffer, 403,
					   y + 1, 0xFFFF);
		}
		y += 22;
		frontend_draw_rect_offset_xy(&rect, 0, 22);
	}

	frontend_draw_rect_assign(&rect, 97, 269, 253, 283);
	frontend_text_draw_centered(
		12, frontend_string_get(FRONTSTR_380_MISSION_EVALUATION), &rect,
		0xFFFF);
	y = 284;
	frontend_draw_rect_assign(&rect, 182, 284, 216, 297);
	for (award_index = 0; award_index < 6; ++award_index) {
		if (g_pilot_data
			    .faction_statistics[g_pilot_data.current_faction_id]
			    .mission_evaluations[award_index] != 0) {
			if (g_pilot_data.current_faction_id == 0) {
				sprintf(g_frontend_scratch_buffer, "rcitlvl%d",
					(int)award_index + 1);
			} else {
				sprintf(g_frontend_scratch_buffer, "citlvl%d",
					(int)award_index + 1);
			}
			front_image_draw_sprite(g_frontend_scratch_buffer, 182,
						y);
			sprintf(g_frontend_scratch_buffer, "%d",
				g_pilot_data
					.faction_statistics
						[g_pilot_data
							 .current_faction_id]
					.mission_evaluations[award_index]);
			frontend_text_draw(10, g_frontend_scratch_buffer, 232,
					   y + 1, 0xFFFF);
		}
		y += 22;
		frontend_draw_rect_offset_xy(&rect, 0, 22);
	}

	frontend_draw_rect_assign(&rect, 353, 133, 387, 146);
	for (award_index = 0; award_index < 6; ++award_index) {
		if (g_pilot_data
			    .faction_statistics[g_pilot_data.current_faction_id]
			    .tournament_trophies[award_index] != 0) {
			frontend_button_draw_sprite_and_tooltip(
				&rect, NULL,
				frontend_string_get(
					(frontend_string_id)(FRONTSTR_382_GOLD +
							     award_index)),
				12, 0xFFFF);
		}
		frontend_draw_rect_offset_xy(&rect, 0, 22);
	}
	frontend_draw_rect_assign(&rect, 182, 133, 216, 146);
	for (award_index = 0; award_index < 6; ++award_index) {
		if (g_pilot_data
			    .faction_statistics[g_pilot_data.current_faction_id]
			    .melee_plaques[award_index] != 0) {
			frontend_button_draw_sprite_and_tooltip(
				&rect, NULL,
				frontend_string_get(
					(frontend_string_id)(FRONTSTR_382_GOLD +
							     award_index)),
				12, 0xFFFF);
		}
		frontend_draw_rect_offset_xy(&rect, 0, 22);
	}
	frontend_draw_rect_assign(&rect, 353, 284, 387, 297);
	for (award_index = 0; award_index < 6; ++award_index) {
		faction_id = g_pilot_data.current_faction_id;
		if (g_pilot_data.faction_statistics[faction_id]
			    .battle_medallions[award_index] != 0) {
			frontend_button_draw_sprite_and_tooltip(
				&rect, NULL,
				frontend_string_get(
					(frontend_string_id)(FRONTSTR_382_GOLD +
							     award_index)),
				12, 0xFFFF);
		}
		frontend_draw_rect_offset_xy(&rect, 0, 22);
	}
	frontend_draw_rect_assign(&rect, 182, 284, 216, 297);
	for (award_index = 0; award_index < 6; ++award_index) {
		faction_id = g_pilot_data.current_faction_id;
		if (g_pilot_data.faction_statistics[faction_id]
			    .mission_evaluations[award_index] != 0) {
			frontend_button_draw_sprite_and_tooltip(
				&rect, NULL,
				frontend_string_get((
					frontend_string_id)(FRONTSTR_660_TOP_PERFORMANCE +
							    award_index)),
				12, 0xFFFF);
		}
		frontend_draw_rect_offset_xy(&rect, 0, 22);
	}
	return 1;
}

/* Draws the pilot rating page: the class insignia the pilot's rating has
 * reached, from cadet to Jedi Master; the rank sprite of every rating from
 * trainee up to the pilot's, and of target drone and ground crew when their
 * rating_achieved_on_mission is not 0, at g_pilot_rating_icon_pos, each with a
 * tooltip giving its mission number from rating_achieved_on_mission. Returns 0,
 * having drawn only the title, when no pilot is loaded; else 1. */
// FUNCTION: XVT 0x4C90C0
int pilot_record_draw_pilot_rating_page(void)
{
	int mouse_x;
	int mouse_y;

	frontend_cursor_get_pos(&mouse_x, &mouse_y);
	struct RECT rect;
	frontend_draw_rect_assign(&rect, 84, 90, 404, 106);
	if (g_pilot_data.name[0] == '\0') {
		sprintf(g_frontend_scratch_buffer, "%s",
			frontend_string_get(FRONTSTR_008_PILOT_RATING));
	} else {
		sprintf(g_frontend_scratch_buffer, "%s: %c%s %c%s",
			frontend_string_get(FRONTSTR_008_PILOT_RATING), 6,
			g_pilot_data.rating_name, 4, g_pilot_data.name);
	}
	frontend_text_draw_centered(15, g_frontend_scratch_buffer, &rect,
				    0xFFFF);
	if (g_pilot_data.name[0] == '\0') {
		return 0;
	}
	if ((unsigned)g_pilot_data.rating >= PILOT_RATING_FLIGHT_CADET) {
		front_image_draw_sprite("cadet", 270, 349);
		frontend_draw_rect_assign(&rect, 270, 334, 415, 348);
		frontend_text_draw_centered(
			12, frontend_string_get(FRONTSTR_393_CADET), &rect,
			0xFFFF);
	}
	if ((unsigned)g_pilot_data.rating >= PILOT_RATING_OFFICER_4TH_CLASS) {
		front_image_draw_sprite("officer", 103, 349);
		frontend_draw_rect_assign(&rect, 103, 334, 248, 348);
		frontend_text_draw_centered(
			12, frontend_string_get(FRONTSTR_394_OFFICER), &rect,
			0xFFFF);
	}
	if ((unsigned)g_pilot_data.rating >= PILOT_RATING_VETERAN_4TH_GRADE) {
		front_image_draw_sprite("veteran", 270, 274);
		frontend_draw_rect_assign(&rect, 270, 259, 415, 273);
		frontend_text_draw_centered(
			12, frontend_string_get(FRONTSTR_395_VETERAN), &rect,
			0xFFFF);
	}
	if ((unsigned)g_pilot_data.rating >= PILOT_RATING_ACE_4TH_LEVEL) {
		front_image_draw_sprite("ace", 103, 274);
		frontend_draw_rect_assign(&rect, 103, 259, 248, 273);
		frontend_text_draw_centered(
			12, frontend_string_get(FRONTSTR_396_ACE), &rect,
			0xFFFF);
	}
	if ((unsigned)g_pilot_data.rating >= PILOT_RATING_TOP_ACE_4TH_ORDER) {
		front_image_draw_sprite("topace", 270, 199);
		frontend_draw_rect_assign(&rect, 270, 184, 415, 198);
		frontend_text_draw_centered(
			12, frontend_string_get(FRONTSTR_397_TOP_ACE), &rect,
			0xFFFF);
	}
	if ((unsigned)g_pilot_data.rating >= PILOT_RATING_JEDI_4TH_DEGREE) {
		front_image_draw_sprite("master", 103, 199);
		frontend_draw_rect_assign(&rect, 103, 184, 248, 199);
		frontend_text_draw_centered(
			12, frontend_string_get(FRONTSTR_398_JEDI), &rect,
			0xFFFF);
	}
	if ((unsigned)g_pilot_data.rating >= PILOT_RATING_JEDI_MASTER) {
		frontend_draw_rect_assign(&rect, 187, 107, 323, 121);
		frontend_text_draw_centered(
			12, frontend_string_get(FRONTSTR_399_JEDI_MASTER),
			&rect, 0xFFFF);
	}
	if (g_pilot_data.rating_achieved_on_mission[0] != 0) {
		front_image_draw_sprite("rank0", g_pilot_rating_icon_pos[0].x,
					g_pilot_rating_icon_pos[0].y);
		frontend_draw_rect_assign(&rect, g_pilot_rating_icon_pos[0].x,
					  g_pilot_rating_icon_pos[0].y,
					  g_pilot_rating_icon_pos[0].x + 51,
					  g_pilot_rating_icon_pos[0].y + 11);
		if (frontend_draw_point_in_rect(&rect, mouse_x, mouse_y)) {
			sprintf(g_frontend_scratch_buffer, "%s %s %d",
				frontend_string_get(FRONTSTR_122_TARGET_DRONE),
				frontend_string_get(
					FRONTSTR_644_ACHIEVED_ON_MISSION),
				g_pilot_data.rating_achieved_on_mission[0]);
			frontend_button_draw_sprite_and_tooltip(
				&rect, NULL, g_frontend_scratch_buffer, 12,
				0xFFFF);
		}
	}
	if (g_pilot_data.rating_achieved_on_mission[1] != 0) {
		front_image_draw_sprite("rank1", g_pilot_rating_icon_pos[1].x,
					g_pilot_rating_icon_pos[1].y);
		frontend_draw_rect_assign(&rect, g_pilot_rating_icon_pos[1].x,
					  g_pilot_rating_icon_pos[1].y,
					  g_pilot_rating_icon_pos[1].x + 51,
					  g_pilot_rating_icon_pos[1].y + 11);
		if (frontend_draw_point_in_rect(&rect, mouse_x, mouse_y)) {
			sprintf(g_frontend_scratch_buffer, "%s %s %d",
				frontend_string_get(FRONTSTR_122_TARGET_DRONE +
						    1),
				frontend_string_get(
					FRONTSTR_644_ACHIEVED_ON_MISSION),
				g_pilot_data.rating_achieved_on_mission[1]);
			frontend_button_draw_sprite_and_tooltip(
				&rect, NULL, g_frontend_scratch_buffer, 12,
				0xFFFF);
		}
	}
	for (int rating_index = PILOT_RATING_TRAINEE;
	     (unsigned)rating_index <= (unsigned)g_pilot_data.rating;
	     ++rating_index) {
		struct POINT *icon = &g_pilot_rating_icon_pos[rating_index];
		sprintf(g_frontend_scratch_buffer, "rank%d", rating_index);
		front_image_draw_sprite(g_frontend_scratch_buffer, icon->x,
					icon->y);
		if ((unsigned)rating_index >= 24) {
			frontend_draw_rect_assign(&rect, icon->x, icon->y,
						  icon->x + 138, icon->y + 59);
		} else {
			frontend_draw_rect_assign(&rect, icon->x, icon->y,
						  icon->x + 51, icon->y + 11);
		}
		if (frontend_draw_point_in_rect(&rect, mouse_x, mouse_y)) {
			sprintf(g_frontend_scratch_buffer, "%s %s %d",
				frontend_string_get(FRONTSTR_122_TARGET_DRONE +
						    rating_index),
				frontend_string_get(
					FRONTSTR_644_ACHIEVED_ON_MISSION),
				g_pilot_data.rating_achieved_on_mission
					[rating_index]);
			frontend_button_draw_sprite_and_tooltip(
				&rect, NULL, g_frontend_scratch_buffer, 12,
				0xFFFF);
		}
	}
	return 1;
}

/* Draws the pilot record's buttons: one per page and the Rebel and Imperial
 * pilot buttons, with the current page and faction selected. A page button sets
 * g_pilot_record_page; a faction button sets g_pilot_data.current_faction_id to 0
 * (Rebel) or 1 (Imperial) and copies that faction's team, mission directory,
 * mission ids and sequence fields into g_pilot_data. Either sets
 * g_pilot_record_pages_need_rebuild and redraws the background. Returns 1. */
// FUNCTION: XVT 0x4C9660
int pilot_record_update_navigation_controls(void)
{
	frontend_navigation_slot_state slot_states[8] = {
		FRONTEND_NAVIGATION_SLOT_ACTIVE,
		FRONTEND_NAVIGATION_SLOT_ACTIVE,
		FRONTEND_NAVIGATION_SLOT_ACTIVE,
		FRONTEND_NAVIGATION_SLOT_ACTIVE,
		FRONTEND_NAVIGATION_SLOT_ACTIVE,
		FRONTEND_NAVIGATION_SLOT_ACTIVE,
		FRONTEND_NAVIGATION_SLOT_ACTIVE,
		FRONTEND_NAVIGATION_SLOT_ACTIVE,
	};

	if (g_pilot_record_page == 5) {
		slot_states[7] = FRONTEND_NAVIGATION_SLOT_SELECTED;
	} else {
		slot_states[g_pilot_record_page] =
			FRONTEND_NAVIGATION_SLOT_SELECTED;
	}
	slot_states[5 + g_pilot_data.current_faction_id] =
		FRONTEND_NAVIGATION_SLOT_SELECTED;
	frontend_button_draw_eight_slot_navigation_state(slot_states);

	struct RECT rect;
	frontend_draw_rect_assign(&rect, 22, 334, 42, 358);
	if (g_pilot_data.current_faction_id == 1) {
		frontend_button_draw_sprite_and_tooltip(
			&rect, "reg7d",
			frontend_string_get(FRONTSTR_643_IMPERIAL_PILOT), 12,
			0);
	} else if (frontend_button_handle_sprite_button(
			   &rect, "reg7u", "reg7u",
			   frontend_string_get(FRONTSTR_643_IMPERIAL_PILOT), 12,
			   0, 17, "jewelsound")) {
		g_pilot_record_pages_need_rebuild = 1;
		g_pilot_data.current_faction_id = 1;
		g_pilot_data.team = g_pilot_data.faction_statistics[1].team;
		g_pilot_data.mission_directory_id =
			g_pilot_data.faction_statistics[1].mission_directory_id;
		memcpy(g_pilot_data.mission_description_ids,
		       g_pilot_data.faction_statistics[1]
			       .mission_description_ids,
		       sizeof(g_pilot_data.mission_description_ids));
		g_pilot_data.mission_sequence_active =
			g_pilot_data.faction_statistics[1]
				.mission_sequence_active;
		g_pilot_data.saved_mission_description_id =
			g_pilot_data.faction_statistics[1]
				.saved_mission_description_id;
		XVT_LOG_INFO(
			"pilot.faction_chosen faction=%d team=%d directory=%d",
			g_pilot_data.current_faction_id, g_pilot_data.team,
			(int)g_pilot_data.mission_directory_id);
		pilot_record_redraw_background();
	}

	frontend_draw_rect_offset_xy(&rect, 0, -28);
	if (g_pilot_data.current_faction_id != 0) {
		if (frontend_button_handle_sprite_button(
			    &rect, "reg6u", "reg6u",
			    frontend_string_get(FRONTSTR_642_REBEL_PILOT), 12,
			    0, 16, "jewelsound")) {
			g_pilot_record_pages_need_rebuild = 1;
			g_pilot_data.current_faction_id = 0;
			g_pilot_data.team =
				g_pilot_data.faction_statistics[0].team;
			g_pilot_data.mission_directory_id =
				g_pilot_data.faction_statistics[0]
					.mission_directory_id;
			memcpy(g_pilot_data.mission_description_ids,
			       g_pilot_data.faction_statistics[0]
				       .mission_description_ids,
			       sizeof(g_pilot_data.mission_description_ids));
			g_pilot_data.mission_sequence_active =
				g_pilot_data.faction_statistics[0]
					.mission_sequence_active;
			g_pilot_data.saved_mission_description_id =
				g_pilot_data.faction_statistics[0]
					.saved_mission_description_id;
			XVT_LOG_INFO(
				"pilot.faction_chosen faction=%d team=%d directory=%d",
				g_pilot_data.current_faction_id,
				g_pilot_data.team,
				(int)g_pilot_data.mission_directory_id);
			pilot_record_redraw_background();
		}
	} else {
		frontend_button_draw_sprite_and_tooltip(
			&rect, "reg6d",
			frontend_string_get(FRONTSTR_642_REBEL_PILOT), 12, 0);
	}

	frontend_draw_rect_assign(&rect, 22, 254, 42, 278);
	if (g_pilot_record_page == 5) {
		frontend_button_draw_sprite_and_tooltip(
			&rect, "reg8d",
			frontend_string_get(FRONTSTR_796_VIEW_CUTSCENES), 12,
			0);
	} else if (frontend_button_handle_sprite_button(
			   &rect, "reg8u", "reg8u",
			   frontend_string_get(FRONTSTR_796_VIEW_CUTSCENES), 12,
			   0, 18, "jewelsound")) {
		g_pilot_record_pages_need_rebuild = 1;
		g_pilot_record_page = 5;
		XVT_LOG_DEBUG("pilot.page_chosen page=%d", g_pilot_record_page);
		pilot_record_redraw_background();
	}

	frontend_draw_rect_offset_xy(&rect, 0, -28);
	if (g_pilot_record_page == 4) {
		frontend_button_draw_sprite_and_tooltip(
			&rect, "reg5d",
			frontend_string_get(FRONTSTR_826_CAMPAIGN_MEDALS), 12,
			0);
	} else if (frontend_button_handle_sprite_button(
			   &rect, "reg5u", "reg5u",
			   frontend_string_get(FRONTSTR_826_CAMPAIGN_MEDALS),
			   12, 0, 15, "jewelsound")) {
		g_pilot_record_pages_need_rebuild = 1;
		g_pilot_record_page = 4;
		XVT_LOG_DEBUG("pilot.page_chosen page=%d", g_pilot_record_page);
		pilot_record_redraw_background();
	}

	frontend_draw_rect_offset_xy(&rect, 0, -28);
	if (g_pilot_record_page == 3) {
		frontend_button_draw_sprite_and_tooltip(
			&rect, "reg4d",
			frontend_string_get(FRONTSTR_010_MISSION_ACHIEVEMENTS),
			12, 0);
	} else if (frontend_button_handle_sprite_button(
			   &rect, "reg4u", "reg4u",
			   frontend_string_get(
				   FRONTSTR_010_MISSION_ACHIEVEMENTS),
			   12, 0, 14, "jewelsound")) {
		g_pilot_record_pages_need_rebuild = 1;
		g_pilot_record_page = 3;
		XVT_LOG_DEBUG("pilot.page_chosen page=%d", g_pilot_record_page);
		pilot_record_redraw_background();
	}

	frontend_draw_rect_offset_xy(&rect, 0, -28);
	if (g_pilot_record_page == 2) {
		frontend_button_draw_sprite_and_tooltip(
			&rect, "reg3d",
			frontend_string_get(FRONTSTR_008_PILOT_RATING), 12, 0);
	} else if (frontend_button_handle_sprite_button(
			   &rect, "reg3u", "reg3u",
			   frontend_string_get(FRONTSTR_008_PILOT_RATING), 12,
			   0, 13, "jewelsound")) {
		g_pilot_record_pages_need_rebuild = 1;
		g_pilot_record_page = 2;
		XVT_LOG_DEBUG("pilot.page_chosen page=%d", g_pilot_record_page);
		pilot_record_redraw_background();
	}

	frontend_draw_rect_offset_xy(&rect, 0, -28);
	if (g_pilot_record_page == 1) {
		frontend_button_draw_sprite_and_tooltip(
			&rect, "reg2d",
			frontend_string_get(FRONTSTR_009_PILOT_AWARDS), 12, 0);
	} else if (frontend_button_handle_sprite_button(
			   &rect, "reg2u", "reg2u",
			   frontend_string_get(FRONTSTR_009_PILOT_AWARDS), 12,
			   0, 12, "jewelsound")) {
		g_pilot_record_pages_need_rebuild = 1;
		g_pilot_record_page = 1;
		XVT_LOG_DEBUG("pilot.page_chosen page=%d", g_pilot_record_page);
		pilot_record_redraw_background();
	}

	frontend_draw_rect_offset_xy(&rect, 0, -28);
	if (g_pilot_record_page != 0) {
		if (frontend_button_handle_sprite_button(
			    &rect, "reg1u", "reg1u",
			    frontend_string_get(FRONTSTR_007_PILOT_STATISTICS),
			    12, 0, 11, "jewelsound")) {
			g_pilot_record_pages_need_rebuild = 1;
			g_pilot_record_page = 0;
			XVT_LOG_DEBUG("pilot.page_chosen page=%d",
				      g_pilot_record_page);
			pilot_record_redraw_background();
		}
	} else {
		frontend_button_draw_sprite_and_tooltip(
			&rect, "reg1d",
			frontend_string_get(FRONTSTR_007_PILOT_STATISTICS), 12,
			0);
	}
	return 1;
}

/* Redraws the pilot record's background on the offscreen surface: background0
 * for faction 0, else background1, the frame, allactive or clientactive by
 * g_host_cd_available, the name overlay and the current page's overlay, none on
 * the cutscene page, with the faction's award sprites on the awards page.
 * Returns 1. */
// FUNCTION: XVT 0x4CAF60
int pilot_record_redraw_background(void)
{
	if ((unsigned)g_pilot_data.current_faction_id > 1) {
		XVT_LOG_WARN("pilot.faction_unexpected faction=%d",
			     g_pilot_data.current_faction_id);
	}
	frontend_display_clear_offscreen_surface();
	frontend_display_lock_offscreen_surface();
	if (g_pilot_data.current_faction_id == 0) {
		front_image_draw_sprite_opaque("background0", 0, 0);
	} else {
		front_image_draw_sprite_opaque("background1", 0, 0);
	}
	front_image_draw_sprite("frame", 0, 0);
	if (g_host_cd_available != 0) {
		front_image_draw_sprite("allactive", 0, 0);
	} else {
		front_image_draw_sprite("clientactive", 0, 0);
	}
	front_image_draw_sprite_translucent("nameoverlay", 0, 0);

	switch (g_pilot_record_page) {
	case 2:
		front_image_draw_sprite_translucent("promotionoverlay", 0, 0);
		break;
	case 1:
		front_image_draw_sprite_translucent("awardoverlay", 0, 0);
		if (g_pilot_data.current_faction_id == 0) {
			front_image_draw_sprite("rebplaque", 0, 0);
			front_image_draw_sprite("rebmedal", 0, 0);
			front_image_draw_sprite("rebtrophy", 0, 0);
			front_image_draw_sprite("rebcitation", 0, 0);
		} else {
			front_image_draw_sprite("impplaque", 0, 0);
			front_image_draw_sprite("impmedal", 0, 0);
			front_image_draw_sprite("imptrophy", 0, 0);
			front_image_draw_sprite("impcitation", 0, 0);
		}
		break;
	case 0:
	case 3:
	case 4:
		front_image_draw_sprite_translucent("regoverlay", 0, 0);
		break;
	default:
		break;
	}

	frontend_display_unlock_offscreen_surface(1);
	return 1;
}

/* Loads the campaign medal sprite table from fileName into
 * g_campaign_award_sprites, freeing any old one. The first line gives the record
 * count to allocate. Each record is a campaign id line, the main sprite name
 * and 15 multiplayer then 15 single-player mission award sprite names, one per
 * line, lines starting with // skipped; names are cut to 31 characters and the
 * 16th slots stay empty. g_campaign_award_sprite_count counts the complete
 * records. Returns 0 when the file does not open, its first line cannot be read
 * or the allocation fails, which the original build returns from with the file
 * left open; else 1, also when the file ends early, a record cut short getting
 * campaign_id 0. In the original build the record loop stops at 15 records
 * rather than at the count read, and nothing checks that the records fit the
 * allocation; the modern build stops at the count. */
// FUNCTION: XVT 0x4CC060
int pilot_record_load_campaign_award_sprite_table(const char *file_name)
{
	unsigned int record_capacity;

	if (g_campaign_award_sprites != NULL) {
		free(g_campaign_award_sprites);
		g_campaign_award_sprites = NULL;
	}
	xvt_file *stream = file_open(file_name, "r");
	if (stream == NULL) {
		XVT_LOG_WARN("pilot.awards_table_missing file=\"%s\"",
			     file_name);
		return 0;
	}
	if (FILE_GETS(g_frontend_scratch_buffer, 255, stream) == NULL) {
		XVT_LOG_WARN("pilot.awards_table_empty file=\"%s\"", file_name);
		file_close(stream);
		return 0;
	}
	unsigned int record_count_or_index =
		(unsigned int)atoi(g_frontend_scratch_buffer);
	record_capacity = record_count_or_index;
	g_campaign_award_sprites = (struct campaign_award_sprite_entry *)malloc(
		sizeof(*g_campaign_award_sprites) * record_count_or_index);
	if (g_campaign_award_sprites == NULL) {
		file_close(stream);
		XVT_LOG_ERROR("pilot.awards_table_unallocated count=%u",
			      record_count_or_index);
		return 0;
	}
	memset(g_campaign_award_sprites, 0,
	       sizeof(*g_campaign_award_sprites) * record_count_or_index);
	g_campaign_award_sprite_count = 0;

	char *line;
	unsigned int sprite_name_byte_offset;
	while (g_campaign_award_sprite_count < record_capacity) {
		do {
			line = FILE_GETS(g_frontend_scratch_buffer, 255,
					 stream);
			if (line == NULL) {
				file_close(stream);
				XVT_LOG_WARN(
					"pilot.awards_table_short records=%u part=\"id\"",
					g_campaign_award_sprite_count);
				return 1;
			}
		} while (line[0] == '/' && line[1] == '/');

		if (g_frontend_scratch_buffer
			    [strlen(g_frontend_scratch_buffer) - 1] == '\n') {
			g_frontend_scratch_buffer
				[strlen(g_frontend_scratch_buffer) - 1] = '\0';
		}
		g_campaign_award_sprites[g_campaign_award_sprite_count]
			.campaign_id = atoi(g_frontend_scratch_buffer);

		do {
			line = FILE_GETS(g_frontend_scratch_buffer, 255,
					 stream);
			if (line == NULL) {
				g_campaign_award_sprites
					[g_campaign_award_sprite_count]
						.campaign_id = 0;
				file_close(stream);
				XVT_LOG_WARN(
					"pilot.awards_table_short records=%u part=\"main\"",
					g_campaign_award_sprite_count);
				return 1;
			}
		} while (line[0] == '/' && line[1] == '/');

		if (g_frontend_scratch_buffer
			    [strlen(g_frontend_scratch_buffer) - 1] == '\n') {
			g_frontend_scratch_buffer
				[strlen(g_frontend_scratch_buffer) - 1] = '\0';
		}
		memcpy(g_campaign_award_sprites[g_campaign_award_sprite_count]
			       .main_award_sprite_name,
		       g_frontend_scratch_buffer,
		       sizeof(g_campaign_award_sprites
				      [g_campaign_award_sprite_count]
					      .main_award_sprite_name));
		g_campaign_award_sprites[g_campaign_award_sprite_count]
			.main_award_sprite_name[31] = '\0';

		sprite_name_byte_offset = 0;
		while (sprite_name_byte_offset < 15 * 32) {
			do {
				line = FILE_GETS(g_frontend_scratch_buffer, 255,
						 stream);
				if (line == NULL) {
					g_campaign_award_sprites
						[g_campaign_award_sprite_count]
							.campaign_id = 0;
					file_close(stream);
					XVT_LOG_WARN(
						"pilot.awards_table_short records=%u part=\"network\"",
						g_campaign_award_sprite_count);
					return 1;
				}
			} while (line[0] == '/' && line[1] == '/');

			if (g_frontend_scratch_buffer
				    [strlen(g_frontend_scratch_buffer) - 1] ==
			    '\n') {
				g_frontend_scratch_buffer
					[strlen(g_frontend_scratch_buffer) -
					 1] = '\0';
			}
			memcpy((char *)g_campaign_award_sprites
					       [g_campaign_award_sprite_count]
						       .multiplayer_mission_award_sprite_names +
				       sprite_name_byte_offset,
			       g_frontend_scratch_buffer,
			       sizeof(g_campaign_award_sprites
					      [g_campaign_award_sprite_count]
						      .multiplayer_mission_award_sprite_names
							      [0]));
			*((char *)g_campaign_award_sprites
				  [g_campaign_award_sprite_count]
					  .multiplayer_mission_award_sprite_names +
			  sprite_name_byte_offset + 31) = '\0';
			sprite_name_byte_offset += 32;
		}

		/* From here record_count_or_index counts the single-player
		 * sprite names read, so in the original build the record loop
		 * stops at 15 records instead of the file's count; the modern
		 * build keeps the count in record_capacity. */
		record_count_or_index = 0;
		sprite_name_byte_offset = 0;
		while (sprite_name_byte_offset < 15 * 32) {
			do {
				line = FILE_GETS(g_frontend_scratch_buffer, 255,
						 stream);
				if (line == NULL) {
					g_campaign_award_sprites
						[g_campaign_award_sprite_count]
							.campaign_id = 0;
					file_close(stream);
					XVT_LOG_WARN(
						"pilot.awards_table_short records=%u part=\"solo\"",
						g_campaign_award_sprite_count);
					return 1;
				}
			} while (line[0] == '/' && line[1] == '/');

			if (g_frontend_scratch_buffer
				    [strlen(g_frontend_scratch_buffer) - 1] ==
			    '\n') {
				g_frontend_scratch_buffer
					[strlen(g_frontend_scratch_buffer) -
					 1] = '\0';
			}
			memcpy((char *)g_campaign_award_sprites
					       [g_campaign_award_sprite_count]
						       .singleplayer_mission_award_sprite_names +
				       sprite_name_byte_offset,
			       g_frontend_scratch_buffer,
			       sizeof(g_campaign_award_sprites
					      [g_campaign_award_sprite_count]
						      .singleplayer_mission_award_sprite_names
							      [0]));
			*((char *)g_campaign_award_sprites
				  [g_campaign_award_sprite_count]
					  .singleplayer_mission_award_sprite_names +
			  sprite_name_byte_offset + 31) = '\0';
			sprite_name_byte_offset += 32;
			++record_count_or_index;
		}
		++g_campaign_award_sprite_count;
	}
	XVT_LOG_DEBUG("pilot.awards_table_loaded records=%u",
		      g_campaign_award_sprite_count);

	file_close(stream);
	return 1;
}
