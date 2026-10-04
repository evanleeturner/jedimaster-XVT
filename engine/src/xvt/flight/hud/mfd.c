#include "xvt/flight/hud/mfd.h"
#ifdef XVT_MODERN
#include "xvt_runtime/snapshot/cockpit_messages.h"
#include "xvt_runtime/snapshot/cockpit_pages.h"
#endif

#include <stdio.h>
#include <string.h>

#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/flight_input.h"
#include "xvt/flight/hud/flight_text.h"
#include "xvt/flight/hud/hud.h"
#include "xvt/flight/hud/msg.h"
#include "xvt/flight/mission/goals.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt/math/math2.h"
#include "xvt/net/net_session.h"
#include "xvt/render/flight_palette.h"
#include "xvt/render/flight_sw.h"
#include "xvt/render/renderer.h"
#include "xvt/util/memory.h"

/* The MFD page that has the keyboard focus, an mfd_page_id, MFD_PAGE_NONE when
 * none: the up and down keys scroll it and it gets the bright border. Many
 * functions write it, chiefly mfd_toggle_page, the page draw functions (which
 * pass it to g_mfd_secondary_page as their page closes),
 * hud_rebuild_display_for_view_state and damage_display_mfd_page. Flight start sets
 * MFD_PAGE_NONE: flight_main_loop in the original build,
 * xvt_flight_loading_mission_setup in the modern one. */
// GLOBAL: XVT 0x521548
uint16_t g_mfd_active_page = MFD_PAGE_NONE;
/* The page that becomes active when the active one closes, MFD_PAGE_NONE
 * when none; written alongside g_mfd_active_page by the same functions. */
// GLOBAL: XVT 0x52154C
uint16_t g_mfd_secondary_page = MFD_PAGE_NONE;
/* g_mfd_active_page as hud_rebuild_display_for_view_state saved it on leaving the
 * forward or HUD-only view, put back when that view returns. 3 functions
 * write it: hud_rebuild_display_for_view_state, and at flight start
 * flight_main_loop in the original build and xvt_flight_loading_mission_setup in
 * the modern one. */
// GLOBAL: XVT 0x521554
uint16_t g_mfd_saved_active_page = MFD_PAGE_NONE;
/* g_mfd_secondary_page saved with g_mfd_saved_active_page, by the same 3
 * functions. */
// GLOBAL: XVT 0x521558
uint16_t g_mfd_saved_secondary_page = MFD_PAGE_NONE;
/* g_mfd_page_states saved with g_mfd_saved_active_page, by the same 3
 * functions. */
// GLOBAL: XVT 0xA08330
int16_t g_saved_mfd_page_states[MFD_PAGE_COUNT] = {MFD_PAGE_STATE_CLOSED};
/* Per page, an mfd_page_state: closed, open, closing (the next draw clears it)
 * or reopened. Many functions write it, chiefly mfd_toggle_page,
 * mfd_draw_craft_list_page, hud_rebuild_display_for_view_state and
 * damage_display_mfd_page; flight start sets every entry closed. */
// GLOBAL: XVT 0xA08BA0
int16_t g_mfd_page_states[MFD_PAGE_COUNT] = {MFD_PAGE_STATE_CLOSED};
/* Lines the message log page is scrolled down, 0 at the newest. Only
 * mfd_draw_message_log_page writes it, setting 0 when the page state
 * changes. */
// GLOBAL: XVT 0x622BBC
static uint16_t g_mfd_message_log_scroll_offset = 0;
/* 1 when the message log page must redraw; only mfd_draw_message_log_page
 * writes it, setting 0 after each draw. */
// GLOBAL: XVT 0x622BB0
static uint16_t g_mfd_message_log_redraw = 0;
/* g_message_log_total_count at the message log page's last draw; only
 * mfd_draw_message_log_page writes it, to tell when the log grew. */
// GLOBAL: XVT 0x622BB8
static unsigned int g_mfd_message_log_last_draw_total_count = 0;
/* The local player's map_camera_state when the map help page last drew its
 * text; only mfd_draw_map_help_page writes it. */
// GLOBAL: XVT 0x5569F0
uint16_t g_mfd_map_help_camera_state_cache = 0;
/* First row the scoreboard page shows; only mfd_draw_mission_scoreboard_page
 * writes it, 0 when it redraws the header. */
// GLOBAL: XVT 0x5569E0
int16_t g_mfd_mission_scoreboard_first_visible_row = 0;
/* g_active_flight_player_count when the scoreboard pane was last sized, 0 after
 * it closes; only mfd_draw_mission_scoreboard_page writes it. */
// GLOBAL: XVT 0x5569E4
int16_t g_mfd_mission_scoreboard_last_player_count = 0;
/* Right edge of the scoreboard pane at its last sizing, 0 after it closes;
 * only mfd_draw_mission_scoreboard_page writes it. */
// GLOBAL: XVT 0x5569E8
int16_t g_mfd_mission_scoreboard_last_width = 0;
/* Rows in the craft list at its last full draw; only mfd_draw_craft_list_page
 * writes it. */
// GLOBAL: XVT 0x5569D8
uint16_t g_mfd_craft_list_cached_row_count = 0;
/* First row shown, for the friendly (0) and hostile (1) craft lists; only
 * mfd_draw_craft_list_page writes it. */
// GLOBAL: XVT 0x5569EC
int16_t g_mfd_craft_list_top_row_by_mode[2] = {0};
/* Lines each goals-page section takes, sections 0 to 3; recounted on every
 * call of mfd_draw_mission_goals_page, its only writer. Entries 4 to 7 stay
 * 0. */
// GLOBAL: XVT 0x9A7A10
static int g_mfd_goals_line_counts[8] = {0};
/* The local team's prevent status at the goals page's last draw, 0xFF after
 * a reset; only mfd_draw_mission_goals_page writes it. */
// GLOBAL: XVT 0x5507F8
static uint8_t g_mfd_goals_cached_secondary_status = 0;
/* Lines the goals page drew last time; only mfd_draw_mission_goals_page writes
 * it. */
// GLOBAL: XVT 0x5507FC
static int g_mfd_goals_current_total_lines = 0;
/* Sum of g_mfd_goals_line_counts at the goals page's last check; only
 * mfd_draw_mission_goals_page writes it. */
// GLOBAL: XVT 0x550800
static int g_mfd_goals_cached_total_goal_lines = 0;
/* 1 when the goals page must redraw; only mfd_draw_mission_goals_page writes
 * it, setting 0 at the end of each draw. */
// GLOBAL: XVT 0x550804
static uint16_t g_mfd_goals_redraw_needed = 0;
/* g_mfd_goals_line_counts at the goals page's last draw; only
 * mfd_draw_mission_goals_page writes it. */
// GLOBAL: XVT 0x550808
static int g_mfd_goals_cached_line_counts[8] = {0};
/* Set to 0 by mfd_draw_mission_goals_page when the page opens; nothing reads
 * it. */
// GLOBAL: XVT 0x550828
static uint16_t g_mfd_goals_unused_state = 0;
/* The local team's primary status at the goals page's last draw, 0xFF after
 * a reset; only mfd_draw_mission_goals_page writes it. */
// GLOBAL: XVT 0x550830
static uint8_t g_mfd_goals_cached_primary_status = 0;
/* Goals page scroll position, in lines; only mfd_draw_mission_goals_page
 * writes it. */
// GLOBAL: XVT 0x550838
static int g_mfd_goals_current_scroll_top = 0;
/* Per goals-page section and goal kind (primary, prevent, bonus), the goal
 * state the section lists: a flight group goal's goal_state, or a nonzero
 * team_global_goal_state of a global goal, must equal it. Its low 2 bits also
 * pick the goal text slot, and it is the goal status passed to
 * goals_outputgoal, except 5 for prevent goals in section 2. Read only by
 * mfd_draw_mission_goals_page. */
// GLOBAL: XVT 0x51BE38
static uint16_t g_mfd_goals_display_state_by_section_type[4][3] = {
	{2, 1, 1},
	{4, 0, 0},
	{0, 4, 0},
	{1, 0, 1},
};
/* A second global goal state each section accepts, as
 * g_mfd_goals_display_state_by_section_type; read only by
 * mfd_draw_mission_goals_page. */
// GLOBAL: XVT 0x51BE50
static uint16_t g_mfd_goals_count_alt_state_by_section_type[4][3] = {
	{2, 1, 1},
	{4, 0, 0},
	{0, 4, 0},
	{4, 0, 1},
};
/* Per section and goal kind, the mission_evaluate_condition result bits (1
 * met, 2 failed, 4 undecided) for which a global goal trigger is listed.
 * Read only by mfd_draw_mission_goals_page. */
// GLOBAL: XVT 0x51BE68
static uint16_t g_mfd_goals_condition_mask_by_section_type[4][3] = {
	{2, 1, 1},
	{6, 0, 0},
	{0, 6, 0},
	{1, 0, 1},
};
/* Per team, 0 to 9, the color letter flight_text_set_color takes for its craft
 * in the craft lists. Read only by mfd_draw_craft_list_page. */
// GLOBAL: XVT 0x523F80
const char g_mfd_craft_list_team_color_codes[11] = "CJNRFCJNRF";
/* Map view help texts from strings.txt, filled by
 * string_table_load_game_strings; read by mfd_draw_map_help_page and
 * hud_draw_map_view_overlay. */
// GLOBAL: XVT 0xA0A790
const char *g_str_map_room_text[20] = {0};
/* Thank-you lines the map help page shows for action keys 0xA8 to 0xAD in
 * 640x480; entry 9 is NULL. Read only by mfd_draw_map_help_page. */
// GLOBAL: XVT 0x523F90
const char *g_mfd_developer_credits_lines[47] = {
	"Special Thanks to:",
	"  Wendy, Frank and Stephanie Post;",
	"  Eric & Krisitin Johnston; Mick & Sarah Foley;",
	"  Jake Hoelter; Grace Hoppin; Hernan Espinoza;",
	"  Johnathan, Jennifer, Jade & Jordan Huggins;",
	"  The San Francisco School of Circus Arts;",
	"  Sarah Steben; Sam Payne; Sandra Feusi;",
	"  Sandrine Deplanque and especially Mr. Lu Yi!",
	"        - Brad",
	0,
	"  David Owen @ ILMphoto; Ben Scott Pye;",
	"  Contesa; the DarkLight guys; Duff;",
	"  Francis Dunnery; the Scott clan;",
	"  Sarah Munday; Adeus Ayrton;",
	"        - Mark S.",
	"  I'd like to thank Debby McLeod",
	"        - Jim",
	" ",
	"  To Billy B.",
	"        - James",
	" ",
	"  I'd like to thank my wonderful girlfriend Eva",
	"        -  Albert",
	"  My father who gave me the sky",
	"  My mother who gave me the song",
	"  My wife Laurie who gave me the strength",
	"  My son Nicholas who gave me a future",
	"  And Jim McLeod and Rick Stienbach",
	"  who put a pencil in my hand and told me",
	"  to use it.",
	"        - Bucky",
	"  Mom, Dad, Joe & Jen; I love you guys!",
	"  Greg Fox; for a lifetime of friendship.",
	"  The best guys: Bill, Chris, Eddie, Jeff,",
	"  Jim, Mark, Mike, Peter, Rick, Thad, and",
	"      the CRS Liberty Crew!",
	"  Saturn Padua a best friend and roommate.",
	"  I couldn't have done it without your support!",
	"        - Bill",
	"  I wish to thank my wife Maria for all her",
	"  patience, love and support during the creation",
	"  of this game. A newlywed wife shouldn't have to",
	"  return from her honeymoon to see her husband",
	"  disappear into 'crunch mode' for five months!",
	"  I'd also like to thank Larry for giving me this",
	"  great job ... now can I have a month off?",
	"         -  David",
};

/* Draws the goals page into the offscreen buffer, in the map view's pane when
 * the local player's map is shown. A line for the local team's outcome
 * (victory, loss, draw or unresolved, from runtime.team_goal_status) heads four
 * sections, failed objectives, objectives to accomplish, conditions to prevent
 * and completed objectives, each listing the team's global goal triggers and
 * flight group goals whose state the section's entries in
 * g_mfd_goals_display_state_by_section_type, g_mfd_goals_count_alt_state_by_section_type and
 * g_mfd_goals_condition_mask_by_section_type select; bonus goals show their points,
 * and goal text from the file replaces the generated line. It counts each
 * section's lines into g_mfd_goals_line_counts, scrolls with the up and down keys
 * while the page is active, and redraws only when those counts, the team's
 * primary or prevent status or the scroll change, or every 472 ticks. When the
 * page state turns to closing it clears the page, makes the secondary page
 * active, and returns. It makes itself the active page when none is and the map
 * is not shown, and draws the active or secondary border. Always returns 0.
 * Writes g_mfd_goals_line_counts and the other g_mfdGoals globals,
 * g_mfd_active_page, g_mfd_secondary_page, g_flight_cursor_x and the local player's
 * mission_goals_refresh_timer. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x413C80
int16_t mfd_draw_mission_goals_page(void)
{
	enum {
		SECTION_COUNT = 4,
		GOAL_TYPE_MAX = 2,
		FLIGHT_GROUP_GOAL_COUNT = 8,
		TRIGGER_PAIR_COUNT = 2,
		TRIGGER_COUNT_PER_PAIR = 2,
		TEAM_NONE = 10,
		MISSION_GOALS_REFRESH_TICKS = 472,
		SCREEN_WIDTH = 320,
		SCREEN_HEIGHT = 200,
		COLOR_MAP_BACKGROUND = 0x34,
		COLOR_NORMAL_TEXT = 0x43,
		COLOR_ACTIVE_PAGE = 0x46,
		COLOR_FAILURE = 0x4A,
		COLOR_UNRESOLVED = 0x4E,
		COLOR_SUCCESS = 0x52,
		COCKPIT_OVERLAY_STR_BONUS = 21,
		COCKPIT_OVERLAY_STR_PENALTY = 22,
	};

	int player_team;
	int total_goal_lines;
	int scroll_top;
	int line_index;
	int16_t section_idx;
	uint16_t cursor_y;
	uint16_t goal_type;
	int16_t bottom;
	int16_t top;
	int16_t left;
	int16_t right;
	int16_t line_step;
	int16_t pair_idx;
	int16_t last_visible_line;
	int16_t trigger_idx;
	uint16_t goal_idx;
	int16_t flight_group_idx;
	int16_t status_title;
	unsigned int status_color;
	char text[80];

	if (g_players[g_local_player].map_camera_state != 0) {
		left = g_mfd_map_blit_source_x + 2;
		right = g_mfd_map_blit_source_x + g_mfd_map_blit_width - 2;
		top = g_mfd_map_blit_source_y + 2;
		bottom = g_mfd_map_blit_source_y + g_mfd_map_blit_height - 2;
	} else {
		left = g_mfd_goals_blit_source_x + 2;
		right = g_mfd_goals_blit_source_x + g_mfd_goals_blit_width - 2;
		top = g_mfd_goals_blit_source_y + 2;
		bottom =
			g_mfd_goals_blit_source_y + g_mfd_goals_blit_height - 2;
	}

#ifdef XVT_MODERN
	xvt_cockpit_pages_set_origin(MFD_PAGE_GOALS, left - 2, top - 2);
#endif
	total_goal_lines = 0;
	player_team = (uint16_t)g_players[g_local_player].team;
	for (section_idx = 0; section_idx < SECTION_COUNT; ++section_idx) {
		g_mfd_goals_line_counts[section_idx] = 0;
		for (goal_type = 0; goal_type <= GOAL_TYPE_MAX; ++goal_type) {
			uint16_t global_state =
				g_flight_mission_state.runtime
					.team_global_goal_state[player_team]
							       [goal_type];
			uint16_t display_state;

			if (global_state != 0 &&
			    (global_state ==
				     g_mfd_goals_display_state_by_section_type
					     [section_idx][goal_type] ||
			     global_state ==
				     g_mfd_goals_count_alt_state_by_section_type
					     [section_idx][goal_type])) {
				for (pair_idx = 0;
				     pair_idx < TRIGGER_PAIR_COUNT;
				     ++pair_idx) {
					int team_or_variable = TEAM_NONE;

					for (trigger_idx = 0;
					     trigger_idx <
					     TRIGGER_COUNT_PER_PAIR;
					     ++trigger_idx) {
						uint16_t condition =
							g_mission_global_goals
								[player_team]
								[goal_type]
									.trigger_pairs
										[pair_idx]
									.triggers
										[trigger_idx]
									.condition;
						uint16_t variable_type =
							g_mission_global_goals
								[player_team]
								[goal_type]
									.trigger_pairs
										[pair_idx]
									.triggers
										[trigger_idx]
									.variable_type;
						uint16_t variable =
							g_mission_global_goals
								[player_team]
								[goal_type]
									.trigger_pairs
										[pair_idx]
									.triggers
										[trigger_idx]
									.variable;
						uint16_t amount =
							(uint8_t)g_mission_global_goals
								[player_team]
								[goal_type]
									.trigger_pairs
										[pair_idx]
									.triggers
										[trigger_idx]
									.amount;

						if (condition ==
							    MISSION_COND_NO_CONDITION ||
						    condition ==
							    MISSION_COND_NEVER ||
						    condition ==
							    MISSION_COND_ALWAYS_TRUE) {
							continue;
						}
						if (g_mission_global_goals[player_team][goal_type]
								    .trigger_pairs
									    [pair_idx]
								    .triggers
									    [trigger_idx +
									     1]
								    .condition ==
							    MISSION_COND_NO_CONDITION &&
						    g_mission_global_goals[player_team][goal_type]
								    .trigger_pairs
									    [pair_idx]
								    .triggers
									    [trigger_idx +
									     1]
								    .variable_type ==
							    GOAL_TARGET_TEAM) {
							team_or_variable =
								g_mission_global_goals
									[player_team]
									[goal_type]
										.trigger_pairs
											[pair_idx]
										.triggers
											[trigger_idx +
											 1]
										.variable;
						}
						if (((uint16_t)mission_evaluate_condition(
							     condition,
							     variable_type,
							     variable, amount,
							     0,
							     team_or_variable) &
						     g_mfd_goals_condition_mask_by_section_type
							     [section_idx]
							     [goal_type]) !=
						    0) {
							++g_mfd_goals_line_counts
								[section_idx];
							if (goal_type == 2) {
								++g_mfd_goals_line_counts
									[section_idx];
							}
						}
					}
				}
			}

			display_state =
				g_mfd_goals_display_state_by_section_type
					[section_idx][goal_type];
			for (goal_idx = 0; goal_idx < FLIGHT_GROUP_GOAL_COUNT;
			     ++goal_idx) {
				for (flight_group_idx = 0;
				     flight_group_idx <
				     g_mission_header.num_flight_groups;
				     ++flight_group_idx) {
					if (g_mission_flight_groups[flight_group_idx]
							    .fg.goals[goal_idx]
							    .enabled_teams
								    [player_team] ==
						    0 ||
					    g_mission_flight_groups
							    [flight_group_idx]
								    .fg
								    .goals[goal_idx]
								    .goal_kind !=
						    goal_type ||
					    g_mission_fg_stats[flight_group_idx]
							    .goal_state
								    [8 * player_team +
								     goal_idx] !=
						    display_state) {
						continue;
					}
					if (goal_type == 2) {
						if (section_idx ==
							    GOAL_TITLE_STR_FAILED_OBJECTIVES &&
						    250 * g_mission_flight_groups[flight_group_idx]
									    .fg
									    .goals[goal_idx]
									    .points <
							    0) {
							g_mfd_goals_line_counts
								[section_idx] +=
								2;
						} else if (
							section_idx ==
								GOAL_TITLE_STR_COMPLETED_OBJECTIVES &&
							250 * g_mission_flight_groups[flight_group_idx]
										.fg
										.goals[goal_idx]
										.points >=
								0) {
							g_mfd_goals_line_counts
								[section_idx] +=
								2;
						}
					} else {
						++g_mfd_goals_line_counts
							[section_idx];
					}
				}
			}
			if (g_mfd_goals_line_counts[section_idx] != 0) {
				++g_mfd_goals_line_counts[section_idx];
			}
			total_goal_lines +=
				g_mfd_goals_line_counts[section_idx];
		}
	}

	flight_sw_set_render_target(g_flight_offscreen_buffer, g_screen_width,
				    g_screen_height,
				    g_flight_bytes_per_pixel * g_screen_width);
	if (g_hud_element_state_cache[g_hud_instrument_set_base_index +
				      HUD_MFD_GOALS_ELEMENT] !=
	    (uint16_t)g_mfd_page_states[MFD_PAGE_GOALS]) {
		if (g_players[g_local_player].map_camera_state != 0) {
			flight_text_set_background_color(COLOR_MAP_BACKGROUND);
		} else {
			flight_text_set_background_color(
				g_flight_transparent_color_index);
		}
		flight_text_set_clip_rect(left - 2, top - 2, right + 2,
					  bottom + 2);
		g_flight_fill_clip_rect_fn();
		if (g_mfd_page_states[MFD_PAGE_GOALS] ==
		    MFD_PAGE_STATE_CLOSING) {
#ifdef XVT_MODERN
			xvt_cockpit_pages_clear(MFD_PAGE_GOALS);
#endif
			if (g_mfd_active_page == MFD_PAGE_GOALS) {
				g_mfd_active_page = g_mfd_secondary_page;
				g_mfd_secondary_page =
					mfd_find_secondary_open_page();
			}
			flight_sw_set_render_target(NULL, SCREEN_WIDTH,
						    SCREEN_HEIGHT, 0);
			return 0;
		}
	}

	flight_text_set_clip_rect(left, top, right, bottom);
	flight_text_set_word_wrap(1);
	flight_text_set_clear_line_background(1);
	flight_text_set_font_tier(0);
	if (g_players[g_local_player].map_camera_state != 0) {
		flight_text_set_background_color(COLOR_MAP_BACKGROUND);
	} else {
		flight_text_set_background_color(
			g_flight_transparent_color_index);
	}
	line_step = g_flight_font_line_height + 2;
	if (g_hud_element_state_cache[g_hud_instrument_set_base_index +
				      HUD_MFD_GOALS_ELEMENT] !=
	    (uint16_t)g_mfd_page_states[MFD_PAGE_GOALS]) {
		scroll_top = 0;
		g_mfd_goals_unused_state = 0;
		g_mfd_goals_cached_total_goal_lines = 0;
		g_mfd_goals_current_total_lines = 0;
		g_player_flight_transient_timers[g_local_player]
			.mission_goals_refresh_timer =
			MISSION_GOALS_REFRESH_TICKS;
		g_mfd_goals_redraw_needed = 1;
		g_mfd_goals_cached_primary_status = -1;
		g_mfd_goals_cached_secondary_status = -1;
	} else {
		scroll_top = g_mfd_goals_current_scroll_top;
		if (g_mfd_active_page == MFD_PAGE_GOALS) {
			switch (g_current_action_key) {
			case FLIGHT_KEY_UP:
				if (scroll_top > 0) {
					g_mfd_goals_redraw_needed = 1;
					--scroll_top;
				}
				break;
			case FLIGHT_KEY_DOWN:
				if ((top - bottom) / line_step +
					    g_mfd_goals_current_total_lines >=
				    scroll_top) {
					g_mfd_goals_redraw_needed = 1;
					++scroll_top;
				}
				break;
			}
		}
		if (total_goal_lines != g_mfd_goals_cached_total_goal_lines) {
			g_mfd_goals_redraw_needed = 1;
			g_mfd_goals_cached_total_goal_lines = total_goal_lines;
		}
		for (section_idx = 0; section_idx < 8; ++section_idx) {
			if (g_mfd_goals_cached_line_counts[section_idx] !=
			    g_mfd_goals_line_counts[section_idx]) {
				g_mfd_goals_redraw_needed = 1;
				break;
			}
		}
		if (g_flight_mission_state.runtime.team_goal_status
				    [(uint16_t)g_players[g_local_player].team]
				    [0] != g_mfd_goals_cached_primary_status ||
		    g_flight_mission_state.runtime.team_goal_status
				    [(uint16_t)g_players[g_local_player].team]
				    [1] !=
			    g_mfd_goals_cached_secondary_status) {
			g_mfd_goals_redraw_needed = 1;
		}
	}

	if ((int16_t)g_player_flight_transient_timers[g_local_player]
		    .mission_goals_refresh_timer <= 0) {
		g_mfd_goals_redraw_needed = 1;
		g_player_flight_transient_timers[g_local_player]
			.mission_goals_refresh_timer =
			MISSION_GOALS_REFRESH_TICKS;
	}
	g_mfd_goals_current_scroll_top = scroll_top;
	last_visible_line = scroll_top + (bottom - top) / line_step - 1;
	cursor_y = top;

	if (g_mfd_goals_redraw_needed != 0) {
#ifdef XVT_MODERN
		xvt_cockpit_pages_clear(MFD_PAGE_GOALS);
		xvt_cockpit_pages_begin_section(MFD_PAGE_GOALS,
						XVT_COCKPIT_PAGE_BODY);
#endif
		line_index = 0;
		flight_text_set_clip_rect(left - 2, top - 2, right + 2,
					  bottom + 2);
#ifdef XVT_MODERN
		xvt_cockpit_pages_record_background(MFD_PAGE_GOALS);
#endif
		g_flight_fill_clip_rect_fn();
		flight_text_set_clip_rect(left, top, right, bottom);
		status_title = GOAL_TITLE_STR_MISSION_OUTCOME;
		flight_text_set_scratch(g_str_goal_titles[status_title]);
		switch (g_flight_mission_state.runtime.team_goal_status
				[(uint16_t)g_players[g_local_player].team][0]) {
		case 0:
			switch (g_flight_mission_state.runtime.team_goal_status
					[(uint16_t)g_players[g_local_player]
						 .team][1]) {
			case 0:
			case 2:
				status_title = GOAL_TITLE_STR_UNRESOLVED;
				status_color = COLOR_UNRESOLVED;
				break;
			case 1:
				status_title = GOAL_TITLE_STR_LOSS;
				status_color = COLOR_FAILURE;
				break;
			}
			break;
		case 1:
			switch (g_flight_mission_state.runtime.team_goal_status
					[(uint16_t)g_players[g_local_player]
						 .team][1]) {
			case 0:
			case 2:
				status_title = GOAL_TITLE_STR_VICTORY;
				status_color = COLOR_SUCCESS;
				break;
			case 1:
				status_title = GOAL_TITLE_STR_DRAW;
				status_color = COLOR_ACTIVE_PAGE;
				break;
			}
			break;
		case 2:
			status_title = GOAL_TITLE_STR_LOSS;
			status_color = COLOR_FAILURE;
			break;
		}
		flight_text_append_scratch_char(' ');
		flight_text_append_scratch_string(
			g_str_goal_titles[status_title]);
		flight_text_set_cursor(left, cursor_y);
		flight_text_set_color(status_color);
		flight_text_draw_string_centered(g_flight_text_scratch_buffer);
		cursor_y += line_step;

		for (section_idx = 0; section_idx < SECTION_COUNT;
		     ++section_idx) {
			uint8_t title_pending;

			flight_text_set_color(
				g_goal_title_color_by_index[section_idx]);
			title_pending = 1;
			for (goal_type = 0; goal_type <= GOAL_TYPE_MAX;
			     ++goal_type) {
				uint16_t global_state;
				uint16_t display_state;
				uint8_t bonus_prefix_width;

				if (g_mfd_goals_line_counts[section_idx] == 0) {
					continue;
				}
				bonus_prefix_width = 0;
				global_state = g_flight_mission_state.runtime
						       .team_global_goal_state
							       [player_team]
							       [goal_type];
				if (global_state != 0 &&
				    (global_state ==
					     g_mfd_goals_display_state_by_section_type
						     [section_idx][goal_type] ||
				     global_state ==
					     g_mfd_goals_count_alt_state_by_section_type
						     [section_idx]
						     [goal_type])) {
					int16_t trigger_ordinal = 0;

					for (pair_idx = 0;
					     pair_idx < TRIGGER_PAIR_COUNT;
					     ++pair_idx) {
						int team_or_variable =
							TEAM_NONE;

						for (trigger_idx = 0;
						     trigger_idx <
						     TRIGGER_COUNT_PER_PAIR;
						     ++trigger_idx,
						    ++trigger_ordinal) {
							uint16_t condition =
								g_mission_global_goals
									[player_team]
									[goal_type]
										.trigger_pairs
											[pair_idx]
										.triggers
											[trigger_idx]
										.condition;
							uint16_t variable_type =
								g_mission_global_goals
									[player_team]
									[goal_type]
										.trigger_pairs
											[pair_idx]
										.triggers
											[trigger_idx]
										.variable_type;
							uint16_t variable =
								g_mission_global_goals
									[player_team]
									[goal_type]
										.trigger_pairs
											[pair_idx]
										.triggers
											[trigger_idx]
										.variable;
							uint16_t amount =
								(uint8_t)g_mission_global_goals
									[player_team]
									[goal_type]
										.trigger_pairs
											[pair_idx]
										.triggers
											[trigger_idx]
										.amount;
							uint16_t goal_status;
							uint16_t drawn_height;

							if (condition ==
								    MISSION_COND_NO_CONDITION ||
							    condition ==
								    MISSION_COND_NEVER ||
							    condition ==
								    MISSION_COND_ALWAYS_TRUE) {
								continue;
							}
							if (g_mission_global_goals[player_team][goal_type]
									    .trigger_pairs
										    [pair_idx]
									    .triggers
										    [trigger_idx +
										     1]
									    .condition ==
								    MISSION_COND_NO_CONDITION &&
							    g_mission_global_goals[player_team][goal_type]
									    .trigger_pairs
										    [pair_idx]
									    .triggers
										    [trigger_idx +
										     1]
									    .variable_type ==
								    GOAL_TARGET_TEAM) {
								team_or_variable =
									g_mission_global_goals
										[player_team]
										[goal_type]
											.trigger_pairs
												[pair_idx]
											.triggers
												[trigger_idx +
												 1]
											.variable;
							}
							if (((uint16_t)mission_evaluate_condition(
								     condition,
								     variable_type,
								     variable,
								     amount, 0,
								     team_or_variable) &
							     g_mfd_goals_condition_mask_by_section_type
								     [section_idx]
								     [goal_type]) ==
							    0) {
								continue;
							}

							if (goal_type == 2 &&
							    bonus_prefix_width ==
								    0) {
								int points =
									250 *
									g_mission_global_goals
										[player_team]
										[goal_type]
											.raw_points;
								if ((section_idx ==
									     GOAL_TITLE_STR_FAILED_OBJECTIVES &&
								     points >=
									     0) ||
								    (section_idx ==
									     GOAL_TITLE_STR_COMPLETED_OBJECTIVES &&
								     points <
									     0)) {
									pair_idx =
										TRIGGER_PAIR_COUNT;
									break;
								}
								if (title_pending !=
								    0) {
									if (line_index >=
										    g_mfd_goals_current_scroll_top &&
									    line_index <
										    last_visible_line) {
										flight_text_set_cursor(
											left,
											cursor_y);
										flight_text_draw_string_centered(
											g_str_goal_titles
												[section_idx]);
										cursor_y +=
											line_step;
									}
									title_pending =
										0;
									++line_index;
								}
								flight_text_set_cursor(
									left,
									cursor_y);
								sprintf(text,
									"(%s %ld) ",
									g_str_cockpit_overlay_text
										[COCKPIT_OVERLAY_STR_BONUS +
										 (points <
										  0)],
									(long)points);
								flight_text_set_color(
									points < 0
										? COLOR_FAILURE
										: COLOR_SUCCESS);
								flight_text_draw_string(
									text);
								flight_text_set_color(
									g_goal_title_color_by_index
										[section_idx]);
								bonus_prefix_width =
									flight_text_measure_string_width(
										text);
							}

							if (title_pending !=
							    0) {
								if (line_index >=
									    g_mfd_goals_current_scroll_top &&
								    line_index <
									    last_visible_line) {
									flight_text_set_cursor(
										left,
										cursor_y);
									flight_text_draw_string_centered(
										g_str_goal_titles
											[section_idx]);
									cursor_y +=
										line_step;
								}
								title_pending =
									0;
								++line_index;
							}
							drawn_height = 0;
							if (line_index >=
								    g_mfd_goals_current_scroll_top &&
							    line_index <=
								    last_visible_line) {
								const char *override_text =
									NULL;

								if (g_global_goal_override_string_handles
									    [player_team]
									    [goal_type]
									    [trigger_ordinal]
									    [g_mfd_goals_display_state_by_section_type
										     [section_idx]
										     [goal_type] &
									     3] !=
								    0) {
									override_text = (const char *)memory_get_handle_block(
										g_global_goal_override_string_handles
											[player_team]
											[goal_type]
											[trigger_ordinal]
											[g_mfd_goals_display_state_by_section_type
												 [section_idx]
												 [goal_type] &
											 3]);
								}
								flight_text_set_cursor(
									left,
									cursor_y);
								goal_status =
									section_idx == GOAL_TITLE_STR_CONDITIONS_TO_PREVENT &&
											goal_type ==
												1
										? 5
										: g_mfd_goals_display_state_by_section_type
											  [section_idx]
											  [goal_type];
								if (goal_type ==
								    2) {
									g_flight_cursor_x +=
										bonus_prefix_width;
								}
								if (override_text !=
								    NULL) {
									flight_text_draw_string(
										override_text);
									if (g_flight_mission_state
											    .runtime
											    .global_goal_trigger_counts
												    [1]
												    [player_team]
												    [goal_type]
												    [trigger_ordinal] <=
										    1 ||
									    condition ==
										    MISSION_COND_BOARDED ||
									    condition ==
										    MISSION_COND_DOCKED ||
									    condition ==
										    MISSION_COND_COMPLETED_MISSION ||
									    condition ==
										    MISSION_COND_NOT_BOARDED ||
									    condition ==
										    MISSION_COND_FAILED_MISSION ||
									    condition ==
										    MISSION_COND_NOT_DOCKED) {
										g_flight_draw_char_fn(
											'\n');
										drawn_height = flight_text_get_wrap_height_for_string(
											override_text);
										cursor_y +=
											drawn_height +
											g_flight_font_line_height +
											2;
									} else {
										sprintf(text,
											" (%ld%%)",
											(long)(100 *
											       g_flight_mission_state
												       .runtime
												       .global_goal_trigger_counts
													       [0]
													       [player_team]
													       [goal_type]
													       [trigger_ordinal] /
											       g_flight_mission_state
												       .runtime
												       .global_goal_trigger_counts
													       [1]
													       [player_team]
													       [goal_type]
													       [trigger_ordinal]));
										if (section_idx ==
											    GOAL_TITLE_STR_FAILED_OBJECTIVES ||
										    section_idx ==
											    GOAL_TITLE_STR_CONDITIONS_TO_PREVENT) {
											flight_text_set_color(
												COLOR_FAILURE);
										} else if (
											section_idx ==
												GOAL_TITLE_STR_COMPLETED_OBJECTIVES ||
											section_idx ==
												GOAL_TITLE_STR_OBJECTIVES_TO_ACCOMPLISH) {
											flight_text_set_color(
												COLOR_SUCCESS);
										} else {
											flight_text_set_color(
												COLOR_NORMAL_TEXT);
										}
										flight_text_set_scratch(
											override_text);
										flight_text_append_scratch_string(
											text);
										flight_text_draw_string(
											text);
										flight_text_set_color(
											g_goal_title_color_by_index
												[section_idx]);
										g_flight_draw_char_fn(
											'\n');
										drawn_height = flight_text_get_wrap_height_for_string(
											g_flight_text_scratch_buffer);
										cursor_y +=
											drawn_height +
											g_flight_font_line_height +
											2;
									}
								} else {
									int percent_complete;

									if (g_flight_mission_state
											    .runtime
											    .global_goal_trigger_counts
												    [1]
												    [player_team]
												    [goal_type]
												    [trigger_ordinal] <=
										    1 ||
									    condition ==
										    MISSION_COND_BOARDED ||
									    condition ==
										    MISSION_COND_DOCKED ||
									    condition ==
										    MISSION_COND_COMPLETED_MISSION ||
									    condition ==
										    MISSION_COND_NOT_BOARDED ||
									    condition ==
										    MISSION_COND_FAILED_MISSION ||
									    condition ==
										    MISSION_COND_NOT_DOCKED) {
										percent_complete =
											-1;
									} else {
										percent_complete =
											100 *
											g_flight_mission_state
												.runtime
												.global_goal_trigger_counts
													[0]
													[player_team]
													[goal_type]
													[trigger_ordinal] /
											g_flight_mission_state
												.runtime
												.global_goal_trigger_counts
													[1]
													[player_team]
													[goal_type]
													[trigger_ordinal];
									}
									drawn_height = goals_outputgoal(
										variable,
										condition,
										variable_type,
										goal_status,
										amount,
										0,
										override_text,
										percent_complete,
										section_idx);
									cursor_y +=
										drawn_height;
									if (drawn_height <=
									    g_flight_font_line_height +
										    2) {
										drawn_height =
											0;
									}
								}
								if (override_text !=
								    NULL) {
									memory_handle_block_done_stub(
										g_global_goal_override_string_handles
											[player_team]
											[goal_type]
											[trigger_ordinal]
											[g_mfd_goals_display_state_by_section_type
												 [section_idx]
												 [goal_type] &
											 3]);
								}
							}
							if (drawn_height != 0) {
								line_index += 2;
							} else {
								++line_index;
							}
						}
					}
				}

				display_state =
					g_mfd_goals_display_state_by_section_type
						[section_idx][goal_type];
				for (goal_idx = 0;
				     goal_idx < FLIGHT_GROUP_GOAL_COUNT;
				     ++goal_idx) {
					for (flight_group_idx = 0;
					     flight_group_idx <
					     g_mission_header.num_flight_groups;
					     ++flight_group_idx) {
						uint16_t drawn_height;
						uint16_t goal_status;
						uint16_t event_condition;
						uint16_t amount_op;
						uint16_t time_limit;

						if (g_mission_flight_groups[flight_group_idx]
								    .fg
								    .goals[goal_idx]
								    .enabled_teams
									    [player_team] ==
							    0 ||
						    g_mission_fg_stats[flight_group_idx]
								    .arrival_enabled ==
							    0 ||
						    g_mission_flight_groups[flight_group_idx]
								    .fg
								    .goals[goal_idx]
								    .goal_kind !=
							    goal_type ||
						    g_mission_fg_stats[flight_group_idx]
								    .goal_state
									    [8 * player_team +
									     goal_idx] !=
							    display_state) {
							continue;
						}
						event_condition =
							g_mission_flight_groups[flight_group_idx]
								.fg
								.goals[goal_idx]
								.event_condition;
						if (event_condition ==
							    MISSION_COND_NEVER ||
						    event_condition ==
							    MISSION_COND_ALWAYS_TRUE) {
							continue;
						}
						if (goal_type == 2) {
							int points =
								250 *
								g_mission_flight_groups[flight_group_idx]
									.fg
									.goals[goal_idx]
									.points;

							if ((section_idx ==
								     GOAL_TITLE_STR_FAILED_OBJECTIVES &&
							     points >= 0) ||
							    (section_idx ==
								     GOAL_TITLE_STR_COMPLETED_OBJECTIVES &&
							     points < 0)) {
								continue;
							}
						}
						if (title_pending != 0) {
							if (line_index >=
								    g_mfd_goals_current_scroll_top &&
							    line_index <
								    last_visible_line) {
								flight_text_set_cursor(
									left,
									cursor_y);
								flight_text_draw_string_centered(
									g_str_goal_titles
										[section_idx]);
								cursor_y +=
									line_step;
							}
							title_pending = 0;
							++line_index;
						}
						drawn_height = 0;
						amount_op =
							(uint8_t)g_mission_flight_groups
								[flight_group_idx]
									.fg
									.goals[goal_idx]
									.amount;
						time_limit =
							g_mission_flight_groups
								[flight_group_idx]
									.fg
									.goals[goal_idx]
									.time_limit5s;
						if (line_index >=
							    g_mfd_goals_current_scroll_top &&
						    line_index <
							    last_visible_line) {
							const char
								*override_text =
									NULL;

							if (g_mission_fg_override_string_handles
								    [flight_group_idx]
								    [goal_idx]
								    [g_mfd_goals_display_state_by_section_type
									     [section_idx]
									     [goal_type] &
								     3] != 0) {
								override_text = (const char *)memory_get_handle_block(
									g_mission_fg_override_string_handles
										[flight_group_idx]
										[goal_idx]
										[g_mfd_goals_display_state_by_section_type
											 [section_idx]
											 [goal_type] &
										 3]);
							}
							flight_text_set_cursor(
								left, cursor_y);
							goal_status =
								section_idx == GOAL_TITLE_STR_CONDITIONS_TO_PREVENT &&
										goal_type ==
											1
									? 5
									: g_mfd_goals_display_state_by_section_type
										  [section_idx]
										  [goal_type];
							if (goal_type == 2) {
								int points =
									250 *
									g_mission_flight_groups[flight_group_idx]
										.fg
										.goals[goal_idx]
										.points;

								sprintf(text,
									"(%s %ld) ",
									g_str_cockpit_overlay_text
										[COCKPIT_OVERLAY_STR_BONUS +
										 (points <
										  0)],
									(long)points);
								flight_text_set_color(
									points < 0
										? COLOR_FAILURE
										: COLOR_SUCCESS);
								flight_text_draw_string(
									text);
								flight_text_set_color(
									g_goal_title_color_by_index
										[section_idx]);
							}
							if (override_text !=
							    NULL) {
								flight_text_draw_string(
									override_text);
								g_flight_draw_char_fn(
									'\n');
								drawn_height = flight_text_get_wrap_height_for_string(
									override_text);
								cursor_y +=
									drawn_height +
									g_flight_font_line_height +
									2;
							} else {
								drawn_height = goals_outputgoal(
									flight_group_idx,
									event_condition,
									GOAL_TARGET_FLIGHT_GROUP,
									goal_status,
									amount_op,
									time_limit,
									override_text,
									-1,
									section_idx);
								cursor_y +=
									drawn_height;
								if (drawn_height <=
								    g_flight_font_line_height +
									    2) {
									drawn_height =
										0;
								}
							}
							if (override_text !=
							    NULL) {
								memory_handle_block_done_stub(
									g_mission_fg_override_string_handles
										[flight_group_idx]
										[goal_idx]
										[g_mfd_goals_display_state_by_section_type
											 [section_idx]
											 [goal_type] &
										 3]);
							}
						}
						if (drawn_height != 0) {
							line_index += 2;
						} else {
							++line_index;
						}
					}
				}
			}
		}

#ifdef XVT_MODERN
		xvt_cockpit_pages_end_section();
#endif
		g_mfd_goals_current_total_lines = line_index;
		for (section_idx = 0; section_idx < 8; ++section_idx) {
			g_mfd_goals_cached_line_counts[section_idx] =
				g_mfd_goals_line_counts[section_idx];
		}
		g_mfd_goals_cached_primary_status =
			g_flight_mission_state.runtime.team_goal_status
				[(uint16_t)g_players[g_local_player].team][0];
		g_mfd_goals_cached_secondary_status =
			g_flight_mission_state.runtime.team_goal_status
				[(uint16_t)g_players[g_local_player].team][1];
	}

	if (g_players[g_local_player].map_camera_state == 0) {
		bottom = g_mfd_goals_blit_source_y +
			 line_step * (g_mfd_goals_current_total_lines + 1);
		if (bottom >
		    g_mfd_goals_blit_source_y + g_mfd_goals_blit_height - 2) {
			bottom = g_mfd_goals_blit_source_y +
				 g_mfd_goals_blit_height - 2;
		}
	}
	if (g_players[g_local_player].map_camera_state == 0 &&
	    g_mfd_active_page == MFD_PAGE_NONE) {
		g_mfd_active_page = MFD_PAGE_GOALS;
	}
	if (g_mfd_active_page == MFD_PAGE_GOALS) {
		flight_text_set_background_color(COLOR_ACTIVE_PAGE);
		flight_text_set_clip_rect(left - 2, top - 2, right + 2,
					  bottom + 2);
#ifdef XVT_MODERN
		xvt_cockpit_pages_record_border(MFD_PAGE_GOALS);
#endif
		g_flight_fill_rect_clipped_fn(left - 2, top - 2, right + 2,
					      bottom + 2, 1);
	} else if (g_mfd_secondary_page == MFD_PAGE_GOALS) {
		if (g_players[g_local_player].map_camera_state != 0) {
			flight_text_set_background_color(COLOR_MAP_BACKGROUND);
		} else {
			flight_text_set_background_color(
				g_flight_transparent_color_index);
		}
		flight_text_set_clip_rect(left - 2, top - 2, right + 2,
					  bottom + 2);
#ifdef XVT_MODERN
		xvt_cockpit_pages_record_border(MFD_PAGE_GOALS);
#endif
		g_flight_fill_rect_clipped_fn(left - 2, top - 2, right + 2,
					      bottom + 2, 1);
	}
#ifdef XVT_MODERN
	xvt_cockpit_pages_record_scroll(MFD_PAGE_GOALS,
					g_mfd_goals_current_scroll_top,
					g_mfd_goals_current_total_lines, -1);
#endif
	g_mfd_goals_redraw_needed = 0;
	flight_text_set_word_wrap(0);
	flight_sw_set_render_target(NULL, SCREEN_WIDTH, SCREEN_HEIGHT, 0);
	return 0;
}

/* Draws the scoreboard page into the offscreen buffer, in the map view's pane
 * when the local player's map is shown. In a melee mission it lists each team
 * with an arrived player group, sorted by bonus plus mission score, by the name
 * of its one player or its team name, with its place in a tournament sequence
 * (shown for places 1 to 3 and the local team), its score and its full and, in
 * brackets, shared kills; otherwise it lists each connected player, sorted by
 * mission score plus team bonus score, with the kills summed over flight
 * groups. The pane is as wide as the longest name plus the score and kills
 * columns. In 320x240 with the map shown and the page active, the up and down
 * keys scroll it. It clears the pane when its width, the player count or the
 * page state changed; when the page is closing it makes the secondary page
 * active, resets the cached width and count, and returns. It makes itself the
 * active page when none is and the map is not shown. Writes
 * g_mfd_mission_scoreboard_first_visible_row, g_mfd_mission_scoreboard_last_width,
 * g_mfd_mission_scoreboard_last_player_count, g_mfd_active_page and
 * g_mfd_secondary_page. In the original build, with no team or player to list, it
 * reads the entry before its order array. */
// FUNCTION: XVT 0x44BC90
void mfd_draw_mission_scoreboard_page(void)
{
	enum {
		PLAYER_COUNT = 8,
		TEAM_COUNT = 10,
		DEFAULT_SCREEN_WIDTH = 320,
		DEFAULT_SCREEN_HEIGHT = 200,
		MAX_LOW_RES_PLAYER_ROWS = 7,
		SCORE_DIGIT_COLUMNS = 6,
		TEAM_KILL_STAT_FULL = 0,
		TEAM_KILL_STAT_SHARED = 1,
		COLOR_MFD_BACKGROUND = 0x2C,
		COLOR_MAP_BACKGROUND = 0x34,
		COLOR_NORMAL_TEXT = 0x43,
		COLOR_ACTIVE_PAGE = 0x46,
		COLOR_LOCAL_ENTRY = 0x4E,
		COLOR_WINNING_ENTRY = 0x52,
	};

	int team_count;

	struct scoreboard_scratch {
		int participating_team_count;
		int order[TEAM_COUNT];
		int player_fg_count_by_team[TEAM_COUNT];
		int owned_fg_count_by_team[TEAM_COUNT];
		char text[80];
		int team_placements[TEAM_COUNT];
	} scratch;

	int16_t pane_width;
	uint16_t space_width;
	int16_t score_column_x;
	uint16_t line_step;
	int16_t left;
	int16_t top;
	int16_t right;
	int16_t bottom_y;
	int16_t row_y;
	int16_t last_visible_exclusive;
	int16_t row;
	int entry_count;
	int16_t player_idx;
	int16_t needs_redraw;
	int16_t shared_kill_count;

	flight_sw_set_render_target(g_flight_offscreen_buffer, g_screen_width,
				    g_screen_height,
				    g_screen_width * g_flight_bytes_per_pixel);
	flight_text_set_font_tier(0);
	pane_width = 0;
	needs_redraw = 0;
	for (player_idx = 0; player_idx < PLAYER_COUNT; ++player_idx) {
		if (g_players[player_idx].participation_state != 0) {
			int16_t name_width;

			flight_text_set_scratch(
				net_session_get_player_name(player_idx));
			name_width = (int16_t)flight_text_measure_string_width(
				g_flight_text_scratch_buffer);
			if (pane_width < name_width) {
				pane_width = name_width;
			}
		}
	}
	if (g_mission_header.mission_type == MISSION_TYPE_MELEE &&
	    g_pilot_data.mission_sequence_active == 1) {
		pane_width = (int16_t)(pane_width +
				       flight_text_measure_string_width("(0)"));
	}
	{
		int16_t header_width;

		header_width = (int16_t)flight_text_measure_string_width(
			g_str_cockpit_overlay_text[COCKPIT_OVERLAY_STR_PLAYER]);
		if (pane_width < header_width) {
			pane_width = header_width;
		}
	}
	flight_text_set_scratch(
		g_str_cockpit_overlay_text[COCKPIT_OVERLAY_STR_SCORE]);
	flight_text_append_scratch_string("          ");
	flight_text_append_scratch_string(
		g_str_cockpit_overlay_text[COCKPIT_OVERLAY_STR_KILLS]);
	space_width = flight_text_measure_string_width("  ");
	score_column_x = (int16_t)(pane_width + space_width);
	pane_width =
		(int16_t)(pane_width + flight_text_measure_string_width(
					       g_flight_text_scratch_buffer));
	if (g_mission_header.mission_type == MISSION_TYPE_MELEE) {
		int fg_idx;
		int team;

		memset(scratch.team_placements, 0,
		       sizeof(scratch.team_placements));
		memset(scratch.owned_fg_count_by_team, 0,
		       sizeof(scratch.owned_fg_count_by_team));
		memset(scratch.player_fg_count_by_team, 0,
		       sizeof(scratch.player_fg_count_by_team));
		for (fg_idx = 0;
		     fg_idx < (int16_t)g_mission_header.num_flight_groups;
		     ++fg_idx) {
			if (g_mission_flight_groups[fg_idx].fg.player_number !=
				    0 &&
			    g_mission_fg_stats[fg_idx].has_arrived != 0) {
				++scratch.player_fg_count_by_team
					  [g_mission_flight_groups[fg_idx]
						   .fg.team];
			}
			if (g_mission_flight_groups[fg_idx].player_owner_idx !=
				    -1 &&
			    g_mission_fg_stats[fg_idx].has_arrived != 0) {
				++scratch.owned_fg_count_by_team
					  [g_mission_flight_groups[fg_idx]
						   .fg.team];
			}
		}
		team_count = 0;
		for (team = 0; team < TEAM_COUNT; ++team) {
			if (scratch.player_fg_count_by_team[team] != 0) {
				scratch.order[team_count++] = team;
			}
		}
#ifdef XVT_MODERN
		if (team_count > 0)
#endif
		{
			for (row = (int16_t)team_count; row < TEAM_COUNT;
			     ++row) {
				scratch.order[row] =
					scratch.order[team_count - 1];
			}
		}
		entry_count = team_count;
		if (g_pilot_data.mission_sequence_active == 1) {
			scratch.participating_team_count =
				g_pilot_data.melee_tournament_sequence_state
					.participating_team_count;
			for (row = 0; row < scratch.participating_team_count;
			     ++row) {
				int placement;
				int16_t other_team;
				int score;

				placement = 0;
				score = g_flight_mission_state.runtime
						.team_scores[TEAM_SCORE_MISSION]
							    [row] +
					g_flight_mission_state.runtime
						.team_scores[TEAM_SCORE_BONUS]
							    [row] +
					g_pilot_data
						.melee_tournament_sequence_state
						.team_standings[row]
						.total_score;
				for (other_team = 0;
				     other_team <
				     scratch.participating_team_count;
				     ++other_team) {
					if (other_team != row &&
					    g_flight_mission_state.runtime.team_scores
								    [TEAM_SCORE_MISSION]
								    [other_team] +
							    g_flight_mission_state
								    .runtime
								    .team_scores
									    [TEAM_SCORE_BONUS]
									    [other_team] +
							    g_pilot_data
								    .melee_tournament_sequence_state
								    .team_standings
									    [other_team]
								    .total_score >
						    score) {
						++placement;
					}
				}
				scratch.team_placements[row] = placement + 1;
			}
		}
	} else {
		entry_count = g_active_flight_player_count;
	}
	if (entry_count == 0) {
		entry_count = 1;
	}

	if (g_players[g_local_player].map_camera_state != 0) {
		left = (int16_t)(g_mfd_map_blit_source_x + 2);
		top = (int16_t)(g_mfd_map_blit_source_y + 2);
		right = (int16_t)(g_mfd_map_blit_source_x +
				  g_mfd_map_blit_width - 2);
		bottom_y = (int16_t)(g_mfd_map_blit_source_y +
				     g_mfd_map_blit_height - 2);
	} else {
		top = (int16_t)(g_mfd_mission_scoreboard_blit_source_y + 2);
		++entry_count;
		left = (int16_t)(g_mfd_mission_scoreboard_blit_source_x + 2);
		right = (int16_t)(left + pane_width);
		bottom_y = (int16_t)((int16_t)entry_count *
					     (g_flight_font_line_height + 1) +
				     top + 2);
		if (g_mfd_mission_scoreboard_last_width != right ||
		    g_mfd_mission_scoreboard_last_player_count !=
			    g_active_flight_player_count) {
			flight_text_set_background_color(
				g_flight_transparent_color_index);
			if (g_mfd_mission_scoreboard_last_player_count != 0) {
				bottom_y =
					(int16_t)((g_mfd_mission_scoreboard_last_player_count +
						   1) * (g_flight_font_line_height +
							 1) +
						  top + 2);
			}
			if (g_mfd_mission_scoreboard_last_width != 0) {
				right = (int16_t)
					g_mfd_mission_scoreboard_last_width;
			}
			flight_text_set_clip_rect(
				(int16_t)(left - 2), (int16_t)(top - 2),
				(int16_t)(right + 2), (int16_t)(bottom_y + 2));
			g_flight_fill_clip_rect_fn();
			right = (int16_t)(left + pane_width);
			bottom_y =
				(int16_t)((int16_t)entry_count *
						  (g_flight_font_line_height +
						   1) +
					  top + 2);
			needs_redraw = 1;
		}
	}

#ifdef XVT_MODERN
	xvt_cockpit_pages_set_origin(MFD_PAGE_SCOREBOARD, left - 2, top - 2);
#endif
	row_y = top;
	line_step = (uint16_t)(g_flight_font_line_height + 1);
	if (g_hud_element_state_cache[g_hud_instrument_set_base_index +
				      HUD_MFD_SCOREBOARD_ELEMENT] !=
		    g_mfd_page_states[MFD_PAGE_SCOREBOARD] ||
	    needs_redraw != 0) {
		if (g_players[g_local_player].map_camera_state != 0) {
			flight_text_set_background_color(COLOR_MAP_BACKGROUND);
		} else if (g_mfd_page_states[MFD_PAGE_SCOREBOARD] ==
				   MFD_PAGE_STATE_CLOSING ||
			   g_players[g_local_player]
					   .view_state.hud_state_live !=
				   HUD_VIEW_FORWARD) {
			flight_text_set_background_color(
				g_flight_transparent_color_index);
		} else {
			flight_text_set_background_color(COLOR_MFD_BACKGROUND);
		}
		flight_text_set_clip_rect(
			(int16_t)(left - 2), (int16_t)(top - 2),
			(int16_t)(right + 2), (int16_t)(bottom_y + 2));
#ifdef XVT_MODERN
		xvt_cockpit_pages_clear(MFD_PAGE_SCOREBOARD);
		xvt_cockpit_pages_record_background(MFD_PAGE_SCOREBOARD);
#endif
		g_flight_fill_clip_rect_fn();
		if (g_mfd_page_states[MFD_PAGE_SCOREBOARD] ==
		    MFD_PAGE_STATE_CLOSING) {
#ifdef XVT_MODERN
			xvt_cockpit_pages_clear(MFD_PAGE_SCOREBOARD);
#endif
			if (g_mfd_active_page == MFD_PAGE_SCOREBOARD) {
				g_mfd_active_page = g_mfd_secondary_page;
				g_mfd_secondary_page =
					mfd_find_secondary_open_page();
			}
			g_mfd_mission_scoreboard_last_width = 0;
			g_mfd_mission_scoreboard_last_player_count = 0;
			flight_sw_set_render_target(NULL, DEFAULT_SCREEN_WIDTH,
						    DEFAULT_SCREEN_HEIGHT, 0);
			return;
		}
		if (g_players[g_local_player].map_camera_state != 0) {
			flight_text_set_background_color(COLOR_MAP_BACKGROUND);
		} else if (g_players[g_local_player]
				   .view_state.hud_state_live ==
			   HUD_VIEW_FORWARD) {
			flight_text_set_background_color(COLOR_MFD_BACKGROUND);
		} else {
			flight_text_set_background_color(
				g_flight_transparent_color_index);
		}
#ifdef XVT_MODERN
		xvt_cockpit_pages_begin_section(MFD_PAGE_SCOREBOARD,
						XVT_COCKPIT_PAGE_HEADER);
#endif
		flight_text_set_color(COLOR_ACTIVE_PAGE);
		flight_text_set_cursor(left, top);
		if (g_mission_header.mission_type == MISSION_TYPE_MELEE &&
		    scratch.player_fg_count_by_team
				    [(uint16_t)g_players[g_local_player].team] >
			    1) {
			flight_text_draw_string(
				g_str_cockpit_overlay_text
					[COCKPIT_OVERLAY_STR_TEAM]);
		} else {
			flight_text_draw_string(
				g_str_cockpit_overlay_text
					[COCKPIT_OVERLAY_STR_PLAYER]);
		}
		flight_text_set_scratch(
			g_str_cockpit_overlay_text[COCKPIT_OVERLAY_STR_SCORE]);
		flight_text_append_scratch_string("     ");
		flight_text_append_scratch_string(
			g_str_cockpit_overlay_text[COCKPIT_OVERLAY_STR_KILLS]);
		flight_text_draw_string_right_aligned(
			g_flight_text_scratch_buffer);
#ifdef XVT_MODERN
		xvt_cockpit_pages_end_section();
#endif
		g_mfd_mission_scoreboard_first_visible_row = 0;
		g_mfd_mission_scoreboard_last_width = right;
		g_mfd_mission_scoreboard_last_player_count =
			g_active_flight_player_count;
	}

	row_y = (int16_t)(row_y + line_step + 1);
	if (g_players[g_local_player].map_camera_state == 0 &&
	    g_mfd_active_page == MFD_PAGE_NONE) {
		g_mfd_active_page = MFD_PAGE_SCOREBOARD;
	}
	if (g_mfd_active_page == MFD_PAGE_SCOREBOARD) {
		flight_text_set_background_color(COLOR_ACTIVE_PAGE);
		flight_text_set_clip_rect(
			(int16_t)(left - 2), (int16_t)(top - 2),
			(int16_t)(right + 2), (int16_t)(bottom_y + 2));
#ifdef XVT_MODERN
		xvt_cockpit_pages_record_border(MFD_PAGE_SCOREBOARD);
#endif
		g_flight_fill_rect_clipped_fn(
			(uint16_t)(left - 2), (uint16_t)(top - 2),
			(uint16_t)(right + 2), (uint16_t)(bottom_y + 2), 1);
	} else {
		if (g_mfd_secondary_page == MFD_PAGE_SCOREBOARD) {
			if (g_players[g_local_player].map_camera_state != 0) {
				flight_text_set_background_color(
					COLOR_MAP_BACKGROUND);
			} else if (g_players[g_local_player]
					   .view_state.hud_state_live !=
				   HUD_VIEW_FORWARD) {
				flight_text_set_background_color(
					g_flight_transparent_color_index);
			} else {
				flight_text_set_background_color(
					COLOR_LOCAL_ENTRY);
			}
			flight_text_set_clip_rect(
				(int16_t)(left - 2), (int16_t)(top - 2),
				(int16_t)(right + 2), (int16_t)(bottom_y + 2));
#ifdef XVT_MODERN
			xvt_cockpit_pages_record_border(MFD_PAGE_SCOREBOARD);
#endif
			g_flight_fill_rect_clipped_fn(
				(uint16_t)(left - 2), (uint16_t)(top - 2),
				(uint16_t)(right + 2), (uint16_t)(bottom_y + 2),
				1);
		} else {
			if (g_players[g_local_player]
				    .view_state.hud_state_live ==
			    HUD_VIEW_FORWARD) {
				flight_text_set_background_color(
					COLOR_LOCAL_ENTRY);
				flight_text_set_clip_rect(
					(int16_t)(left - 2), (int16_t)(top - 2),
					(int16_t)(right + 2),
					(int16_t)(bottom_y + 2));
#ifdef XVT_MODERN
				xvt_cockpit_pages_record_border(
					MFD_PAGE_SCOREBOARD);
#endif
				g_flight_fill_rect_clipped_fn(
					(uint16_t)(left - 2),
					(uint16_t)(top - 2),
					(uint16_t)(right + 2),
					(uint16_t)(bottom_y + 2), 1);
			}
		}
	}

	flight_text_set_clip_rect(left, row_y, right, bottom_y);
	if (g_players[g_local_player].map_camera_state != 0) {
		flight_text_set_background_color(COLOR_MAP_BACKGROUND);
	} else if (g_players[g_local_player].view_state.hud_state_live ==
		   HUD_VIEW_FORWARD) {
		flight_text_set_background_color(COLOR_MFD_BACKGROUND);
	} else {
		flight_text_set_background_color(
			g_flight_transparent_color_index);
	}
	flight_text_set_color(
		g_hud_element_layouts[g_hud_instrument_set_base_index +
				      HUD_MFD_SCOREBOARD_ELEMENT]
			.color_index_or_widget_param);
	if (g_flight_resolution_mode == FLIGHT_RESOLUTION_320X240 &&
	    g_players[g_local_player].map_camera_state != 0 &&
	    g_mfd_active_page == MFD_PAGE_SCOREBOARD) {
		switch (g_current_action_key) {
		case FLIGHT_KEY_UP:
			if (g_mfd_mission_scoreboard_first_visible_row > 0) {
				--g_mfd_mission_scoreboard_first_visible_row;
			}
			break;
		case FLIGHT_KEY_DOWN:
			if (g_mission_header.mission_type ==
			    MISSION_TYPE_MELEE) {
				if ((top - bottom_y) / line_step + team_count >=
				    g_mfd_mission_scoreboard_first_visible_row) {
					++g_mfd_mission_scoreboard_first_visible_row;
				}
			} else if (
				g_active_flight_player_count >
					MAX_LOW_RES_PLAYER_ROWS &&
				g_mfd_mission_scoreboard_last_player_count +
						(top - bottom_y) / line_step >=
					g_mfd_mission_scoreboard_first_visible_row) {
				++g_mfd_mission_scoreboard_first_visible_row;
			}
			break;
		}
	}
	last_visible_exclusive =
		(int16_t)(g_mfd_mission_scoreboard_first_visible_row +
			  (bottom_y - top) / line_step - 1);
#ifdef XVT_MODERN
	xvt_cockpit_pages_record_scroll(
		MFD_PAGE_SCOREBOARD, g_mfd_mission_scoreboard_first_visible_row,
		g_players[g_local_player].map_camera_state != 0
			? entry_count
			: entry_count - 1,
		-1);
	xvt_cockpit_pages_begin_section(MFD_PAGE_SCOREBOARD,
					XVT_COCKPIT_PAGE_BODY);
#endif

	if (g_mission_header.mission_type == MISSION_TYPE_MELEE) {
		if (team_count > 1) {
			int16_t swapped;

			do {
				swapped = 0;
				for (row = 0; row < team_count - 1; ++row) {
					int team;
					int next_team;

					team = scratch.order[row];
					if (team < TEAM_COUNT) {
						next_team =
							scratch.order[row + 1];
						if (g_flight_mission_state
								    .runtime
								    .team_scores
									    [TEAM_SCORE_BONUS]
									    [next_team] +
							    g_flight_mission_state
								    .runtime
								    .team_scores
									    [TEAM_SCORE_MISSION]
									    [next_team] >
						    g_flight_mission_state
								    .runtime
								    .team_scores
									    [TEAM_SCORE_BONUS]
									    [team] +
							    g_flight_mission_state
								    .runtime
								    .team_scores
									    [TEAM_SCORE_MISSION]
									    [team]) {
							int16_t previous_team;

							previous_team =
								scratch.order
									[row];
							scratch.order[row] =
								scratch.order
									[row +
									 1];
							scratch.order[row + 1] =
								previous_team;
							swapped = 1;
							break;
						}
					}
				}
			} while (swapped != 0);
		}
		for (row = 0; row < team_count; ++row) {
			int team;

			if (row >= g_mfd_mission_scoreboard_first_visible_row &&
			    row < last_visible_exclusive &&
			    scratch.order[row] < TEAM_COUNT) {
				int marker;

				team = scratch.order[row];
				if (scratch.player_fg_count_by_team[team] ==
					    1 &&
				    scratch.owned_fg_count_by_team[team] == 1) {
					int fg_idx;

					for (fg_idx = 0;
					     fg_idx <
					     (int16_t)g_mission_header
						     .num_flight_groups;
					     ++fg_idx) {
						if (g_mission_flight_groups[fg_idx]
								    .player_owner_idx !=
							    -1 &&
						    g_mission_flight_groups[fg_idx]
								    .fg.team ==
							    team) {
							flight_text_set_scratch(net_session_get_player_name(
								g_mission_flight_groups
									[fg_idx]
										.player_owner_idx));
							break;
						}
					}
					if (fg_idx ==
					    (int16_t)g_mission_header
						    .num_flight_groups) {
						flight_text_set_scratch(
							g_mission_teams[team]
								.name);
					}
				} else {
					flight_text_set_scratch(
						g_mission_teams[team].name);
				}
				marker = scratch.team_placements[team];
				if (marker != 0 &&
				    (marker <= 3 ||
				     team == (uint16_t)g_players[g_local_player]
						     .team)) {
					flight_text_append_scratch_char(' ');
					flight_text_append_scratch_char('(');
					flight_text_append_scratch_char(
						(char)(marker + '0'));
					flight_text_append_scratch_char(')');
				}
				flight_text_set_clip_rect(
					left, row_y, right,
					(int16_t)(row_y + line_step));
				g_flight_fill_clip_rect_fn();
#ifdef XVT_MODERN
				xvt_cockpit_pages_record_row(
					(uint32_t)team,
					team == (uint16_t)g_players
							[g_local_player]
								.team);
#endif
				if (team ==
				    (uint16_t)g_players[g_local_player].team) {
					flight_text_set_color(
						COLOR_LOCAL_ENTRY);
				} else if (marker == 1) {
					flight_text_set_color(
						COLOR_WINNING_ENTRY);
				} else {
					flight_text_set_color(
						COLOR_NORMAL_TEXT);
				}
				flight_text_set_cursor(left, (uint16_t)row_y);
				flight_text_draw_string(
					g_flight_text_scratch_buffer);
				g_flight_draw_char_fn('\n');
				flight_text_format_scratch_int(
					g_flight_mission_state.runtime
						.team_scores[TEAM_SCORE_BONUS]
							    [team] +
					g_flight_mission_state.runtime
						.team_scores[TEAM_SCORE_MISSION]
							    [team]);
				strcpy(scratch.text,
				       g_flight_text_scratch_buffer);
				strcat(scratch.text, "    ");
				shared_kill_count =
					(int16_t)g_flight_mission_state.runtime
						.team_kill_stats
							[TEAM_KILL_STAT_SHARED]
							[team];
				flight_text_format_scratch_int(
					(int16_t)g_flight_mission_state.runtime
						.team_kill_stats
							[TEAM_KILL_STAT_FULL]
							[team]);
				strcat(scratch.text,
				       g_flight_text_scratch_buffer);
				strcat(scratch.text, "(");
				flight_text_format_scratch_int(
					shared_kill_count);
				strcat(scratch.text,
				       g_flight_text_scratch_buffer);
				strcat(scratch.text, ")");
				flight_text_set_cursor(left, (uint16_t)row_y);
				flight_text_draw_string_right_aligned(
					scratch.text);
				row_y = (int16_t)(row_y + line_step);
			}
		}
	} else {
		int16_t connected_count;

		connected_count = 0;
		for (player_idx = 0; player_idx < PLAYER_COUNT; ++player_idx) {
			if (g_players[player_idx].participation_state == 1 ||
			    g_players[player_idx].participation_state == 2) {
				scratch.order[connected_count++] = player_idx;
			}
		}
#ifdef XVT_MODERN
		if (connected_count > 0)
#endif
		{
			for (row = connected_count; row < PLAYER_COUNT; ++row) {
				scratch.order[row] =
					scratch.order[connected_count - 1];
			}
		}
		if (connected_count > 1) {
			int16_t swapped;

			do {
				swapped = 0;
				for (row = 0; row < connected_count - 1;
				     ++row) {
					int player;
					int next_player;

					player = scratch.order[row];
					if (player < PLAYER_COUNT) {
						next_player =
							scratch.order[row + 1];
						if (g_players[next_player]
								    .mission_stats
								    .mission_score +
							    g_flight_mission_state
								    .runtime
								    .team_scores
									    [TEAM_SCORE_BONUS]
									    [(uint16_t)g_players
										     [next_player]
											     .team] >
						    g_players[player]
								    .mission_stats
								    .mission_score +
							    g_flight_mission_state
								    .runtime
								    .team_scores
									    [TEAM_SCORE_BONUS]
									    [(uint16_t)g_players[player]
										     .team]) {
							int16_t previous_player;

							previous_player =
								scratch.order
									[row];
							scratch.order[row] =
								scratch.order
									[row +
									 1];
							scratch.order[row + 1] =
								previous_player;
							swapped = 1;
							break;
						}
					}
				}
			} while (swapped != 0);
		}
		for (row = 0; row < connected_count; ++row) {
			int player;

			if (row >= g_mfd_mission_scoreboard_first_visible_row &&
			    row < last_visible_exclusive &&
			    scratch.order[row] < PLAYER_COUNT) {
				int16_t digits;
				int16_t x_offset;
				int16_t full_kill_count;
				int16_t fg_idx;
				int16_t row_bottom_y;

				player = scratch.order[row];
				flight_text_set_scratch(
					net_session_get_player_name(player));
				row_bottom_y = (int16_t)(row_y + line_step);
				flight_text_set_clip_rect(left, row_y, right,
							  row_bottom_y);
				g_flight_fill_clip_rect_fn();
#ifdef XVT_MODERN
				xvt_cockpit_pages_record_row(
					(uint32_t)player,
					player == g_local_player);
#endif
				if (player == g_local_player) {
					flight_text_set_color(
						COLOR_LOCAL_ENTRY);
				} else {
					flight_text_set_color(
						COLOR_NORMAL_TEXT);
				}
				flight_text_set_cursor(left, (uint16_t)row_y);
				flight_text_draw_string(
					g_flight_text_scratch_buffer);
				g_flight_draw_char_fn('\n');
				digits = (int16_t)flight_text_format_scratch_int(
					g_players[player]
						.mission_stats.mission_score +
					g_flight_mission_state.runtime
						.team_scores
							[TEAM_SCORE_BONUS]
							[(uint16_t)g_players
								 [player]
									 .team]);
				x_offset = 0;
				if (digits < SCORE_DIGIT_COLUMNS) {
					x_offset =
						(int16_t)(space_width *
							  (SCORE_DIGIT_COLUMNS -
							   digits));
				}
				flight_text_set_cursor(left + x_offset +
							       score_column_x,
						       (uint16_t)row_y);
				strcpy(scratch.text,
				       g_flight_text_scratch_buffer);
				strcat(scratch.text, "    ");
				full_kill_count = 0;
				shared_kill_count = 0;
				for (fg_idx = 0;
				     fg_idx < (int16_t)g_mission_header
						      .num_flight_groups;
				     ++fg_idx) {
					full_kill_count =
						(int16_t)(full_kill_count +
							  g_players[player]
								  .per_mission_kills
								  .kills_full_on_flight_group
									  [fg_idx]);
					shared_kill_count =
						(int16_t)(shared_kill_count +
							  g_players[player]
								  .per_mission_kills
								  .kills_shared_on_flight_group
									  [fg_idx]);
				}
				flight_text_format_scratch_int(full_kill_count);
				strcat(scratch.text,
				       g_flight_text_scratch_buffer);
				strcat(scratch.text, "(");
				flight_text_format_scratch_int(
					shared_kill_count);
				strcat(scratch.text,
				       g_flight_text_scratch_buffer);
				strcat(scratch.text, ")");
				flight_text_set_cursor(left, (uint16_t)row_y);
				flight_text_draw_string_right_aligned(
					scratch.text);
				row_y = row_bottom_y;
			}
		}
	}
#ifdef XVT_MODERN
	xvt_cockpit_pages_end_section();
#endif
	flight_sw_set_render_target(NULL, DEFAULT_SCREEN_WIDTH,
				    DEFAULT_SCREEN_HEIGHT, 0);
}

/* Draws the friendly craft list or, with show_hostile_craft 1, the hostile one
 * (page MFD_PAGE_FRIENDLY_CRAFT minus show_hostile_craft) into the offscreen
 * buffer. A dead targeting computer on the local player's craft closes the page
 * with a message, and an empty list closes it. The friendly list holds the
 * craft of the player's own flight group and of every group on a team allied
 * with the player's; the hostile list every craft in the active region on a
 * team not allied with it; craft breaking up or exploding are left out, and
 * rows are sorted by team. Each row shows the craft's name in its team's
 * g_mfd_craft_list_team_color_codes color, its shield and hull percentages colored
 * by level (20 and 50 percent), its target (its player's current target or its
 * AI target; not for the friendly list in the 320x240 map view) and, for
 * hostile craft above 320x240, the goal word from
 * mfd_get_flight_group_goal_status_string_id. The up and down keys scroll it while it
 * is active; it redraws when the row count changes or every 472 ticks. When the
 * page is closing it makes the secondary page active and returns. Writes
 * g_mfd_craft_list_top_row_by_mode, g_mfd_craft_list_cached_row_count, g_mfd_page_states,
 * g_mfd_active_page, g_mfd_secondary_page, g_msg_arg_table, g_flight_text_scratch_buffer
 * and the local player's mfd_craft_list_refresh_timer. */
// FUNCTION: XVT 0x44CE40
void mfd_draw_craft_list_page(uint16_t show_hostile_craft)
{
	enum {
		MAX_FLIGHT_GROUPS = 48,
		DEFAULT_SCREEN_WIDTH = 320,
		DEFAULT_SCREEN_HEIGHT = 200,
		TARGET_NONE_AI = 255,
		DISPLAY_NAME_FLAGS = 7,
		REFRESH_TICKS = 472,
		COLOR_MFD_BACKGROUND = 0x2C,
		COLOR_MAP_BACKGROUND = 0x34,
		COLOR_NORMAL_TEXT = 0x43,
		COLOR_HEALTHY = 0x46,
		COLOR_DAMAGED = 0x4A,
		COLOR_MEDIUM = 0x4E,
		HEALTH_CRITICAL_PERCENT = 20,
		HEALTH_GOOD_PERCENT = 50,
		PERCENTAGE_SCALE = 655,
		HEALTH_DIGIT_WIDTH = 3,
		CRAFT_NAME_COLUMN_WIDTH = 11,
		ORDERS_COLUMN_OFFSET = 17,
		STATUS_COLUMN_OFFSET = 15,
	};

	uint16_t craft_rows[MAX_FLIGHT_GROUPS];
	int local_player;
	uint16_t player_team;
	uint16_t page_index;
	uint16_t layout_index;
	uint16_t row_count;
	int row_total;
	int flight_group_idx;
	int player_object_idx;
	int row;
	int other_row;
	uint16_t line_step;
	int16_t pane_left;
	uint16_t pane_top;
	int16_t pane_right;
	int16_t pane_bottom;
	uint16_t row_y;
	int character_width;
	uint16_t needs_redraw;
	uint16_t last_visible_row;
	uint16_t last_team;
	int16_t *page_state;

	flight_sw_set_render_target(g_flight_offscreen_buffer, g_screen_width,
				    g_screen_height,
				    g_screen_width * g_flight_bytes_per_pixel);
	flight_text_set_font_tier(0);
	local_player = g_local_player;
	craft_rows[0] = g_players[local_player].bound_flight_group_idx;
	player_team = (uint16_t)g_players[local_player].team;
	page_index = (uint16_t)(MFD_PAGE_FRIENDLY_CRAFT - show_hostile_craft);
	layout_index =
		(uint16_t)(HUD_MFD_CRAFT_LIST_ELEMENT + show_hostile_craft);
	player_object_idx = g_players[local_player].object_index;
	do {
		if (player_object_idx != -1 &&
		    g_object_table[player_object_idx].mobj->p_craft->system_health
				    [DAMAGE_SYSTEM_04_TARGETING_COMPUTER] ==
			    0) {
			page_state = &g_mfd_page_states[page_index];
			if (*page_state != MFD_PAGE_STATE_CLOSING) {
				g_msg_arg_table[0] =
					IFMSG_096_TARGETING_COMPUTER;
				g_msg_arg_table[1] =
					IFMSG_087_DAMAGED_AND_INOPERATIVE;
				*page_state = MFD_PAGE_STATE_CLOSING;
				msg_emit_in_flight_message(
					IFMSG_086_ARG_SYSTEM_IS_ARG,
					g_local_player);
				break;
			}
		}

		{
			uint16_t friendly_flight_groups[MAX_FLIGHT_GROUPS];
			uint16_t flight_group_count;

			friendly_flight_groups[0] = craft_rows[0];
			flight_group_count = 1;
			if (show_hostile_craft == 1) {
				int object_idx;

				row_count = 0;
				for (object_idx =
					     g_active_region_object_slot_start;
				     object_idx <
				     g_active_region_craft_object_slot_end;
				     ++object_idx) {
					struct craft_data *craft;
					uint16_t team;

					if (g_object_table[object_idx]
							    .object_type != 0 &&
					    g_object_table[object_idx].mobj !=
						    NULL &&
					    g_object_table[object_idx]
							    .mobj->p_craft !=
						    NULL) {
						team = g_object_table
							       [object_idx]
								       .mobj
								       ->team;
						if (g_object_table[object_idx]
								    .mobj
								    ->team !=
							    player_team &&
						    g_mission_teams[player_team]
								    .allies[team] ==
							    0) {
							craft = g_object_table[object_idx]
									.mobj
									->p_craft;
							if (craft->object_kind !=
								    CRAFT_OBJECT_KIND_BREAKING_UP &&
							    craft->object_kind !=
								    CRAFT_OBJECT_KIND_EXPLODING) {
								craft_rows[row_count++] =
									(uint16_t)(object_idx |
										   (team
										    << 8));
							}
						}
					}
				}
			} else {
				for (flight_group_idx = 0;
				     flight_group_idx <
				     g_mission_header.num_flight_groups;
				     ++flight_group_idx) {
					int team;
					int is_hostile;

					team = g_mission_flight_groups
						       [flight_group_idx]
							       .fg.team;
					is_hostile =
						team == player_team
							? 0
							: g_mission_teams[player_team]
									  .allies[team] <
								  1;
					if (!is_hostile &&
					    flight_group_idx != craft_rows[0]) {
						friendly_flight_groups
							[flight_group_count++] =
								(uint16_t)
									flight_group_idx;
					}
				}
				row_count = 0;
				for (flight_group_idx = 0;
				     flight_group_idx < flight_group_count;
				     ++flight_group_idx) {
					int friendly_flight_group;
					int object_idx;

					friendly_flight_group =
						friendly_flight_groups
							[flight_group_idx];
					for (object_idx =
						     g_active_region_object_slot_start;
					     object_idx <
					     g_active_region_craft_object_slot_end;
					     ++object_idx) {
						struct mobile_object
							*mobile_object;
						struct craft_data *craft;

						if (g_object_table[object_idx]
								    .flight_group_idx ==
							    friendly_flight_group &&
						    g_object_table[object_idx]
								    .object_type !=
							    0) {
							mobile_object =
								g_object_table[object_idx]
									.mobj;
							if (mobile_object !=
							    NULL) {
								craft = mobile_object
										->p_craft;
								if (craft !=
									    NULL &&
								    craft->object_kind !=
									    CRAFT_OBJECT_KIND_BREAKING_UP &&
								    craft->object_kind !=
									    CRAFT_OBJECT_KIND_EXPLODING) {
									craft_rows[row_count++] =
										(uint16_t)(object_idx |
											   ((uint16_t)mobile_object
												    ->team
											    << 8));
								}
							}
						}
					}
				}
			}
		}
		row_total = row_count;
		row = 0;
		do {
			for (other_row = row + 1; other_row < row_count;
			     ++other_row) {
				int current_team;
				int other_team;
				int current_row;
				uint16_t saved_row;

				current_row = craft_rows[row];
				current_team = current_row & ~0xFF;
				other_team = (uint16_t)(craft_rows[other_row] &
							0xFF00);
				if (other_team < current_team) {
					saved_row = current_row;
					craft_rows[row] = craft_rows[other_row];
					craft_rows[other_row] = saved_row;
					other_row = row + 1;
				}
			}
			++row;
		} while (row < row_count);
		if (row_count == 0) {
			page_state = &g_mfd_page_states[page_index];
			if (*page_state != MFD_PAGE_STATE_CLOSING) {
				if (*page_state != MFD_PAGE_STATE_CLOSED) {
					*page_state = MFD_PAGE_STATE_CLOSING;
				} else {
					*page_state = MFD_PAGE_STATE_CLOSED;
				}
				break;
			}
		}

		line_step = (uint16_t)(g_flight_font_line_height + 2);
		if (show_hostile_craft == 1 &&
		    g_players[local_player].map_camera_state != 0) {
			pane_left = (int16_t)(g_mfd_map_blit_source_x + 2);
			pane_top = (int16_t)(g_mfd_map_blit_source_y + 2);
			pane_right = (int16_t)(g_mfd_map_blit_source_x +
					       g_mfd_map_blit_width - 2);
			pane_bottom = (int16_t)(g_mfd_map_blit_height +
						g_mfd_map_blit_source_y - 2);
		} else {
			pane_left =
				(int16_t)(g_mfd_craft_list_blit_source_x + 2);
			pane_top =
				(int16_t)(g_mfd_craft_list_blit_source_y + 2);
			pane_right = (int16_t)(g_mfd_craft_list_blit_source_x +
					       g_mfd_craft_list_blit_width - 2);
			if (g_mfd_page_states[page_index] ==
				    MFD_PAGE_STATE_CLOSING ||
			    g_players[local_player].map_camera_state != 0) {
				pane_bottom =
					(int16_t)(g_mfd_craft_list_blit_height +
						  g_mfd_craft_list_blit_source_y -
						  2);
			} else {
				pane_bottom =
					(int16_t)(g_mfd_craft_list_blit_source_y +
						  line_step * (row_count + 1));
				if (pane_bottom >
					    g_mfd_craft_list_blit_height +
						    g_mfd_craft_list_blit_source_y -
						    2 ||
				    pane_bottom <=
					    g_mfd_craft_list_blit_source_y) {
					pane_bottom =
						(int16_t)(g_mfd_craft_list_blit_height +
							  g_mfd_craft_list_blit_source_y -
							  2);
				}
			}
		}
		if (g_players[local_player].map_camera_state == 0 &&
		    g_mfd_active_page == MFD_PAGE_NONE) {
			g_mfd_active_page = (int16_t)page_index;
		}

#ifdef XVT_MODERN
		xvt_cockpit_pages_set_origin(page_index, pane_left - 2,
					     pane_top - 2);
#endif
		page_state = &g_mfd_page_states[page_index];
		if (g_hud_element_state_cache[g_hud_instrument_set_base_index +
					      layout_index] != *page_state) {
			int16_t clear_bottom;

			if (g_players[local_player].map_camera_state != 0) {
				flight_text_set_background_color(
					COLOR_MAP_BACKGROUND);
			} else if (*page_state == MFD_PAGE_STATE_CLOSING ||
				   g_players[local_player]
						   .view_state.hud_state_live !=
					   HUD_VIEW_FORWARD) {
				flight_text_set_background_color(
					g_flight_transparent_color_index);
			} else {
				flight_text_set_background_color(
					COLOR_MFD_BACKGROUND);
			}
			if (g_players[g_local_player].map_camera_state != 0) {
				if (show_hostile_craft == 1) {
					clear_bottom =
						(int16_t)(g_mfd_map_blit_height +
							  g_mfd_map_blit_source_y -
							  2);
				} else {
					clear_bottom =
						(int16_t)(g_mfd_craft_list_blit_height +
							  g_mfd_craft_list_blit_source_y -
							  2);
				}
			} else {
				clear_bottom = pane_bottom;
			}
			flight_text_set_clip_rect((int16_t)(pane_left - 2),
						  (int16_t)(pane_top - 2),
						  (int16_t)(pane_right + 2),
						  (int16_t)(clear_bottom + 2));
			g_flight_fill_clip_rect_fn();
			flight_text_set_clip_rect((int16_t)(pane_left - 2),
						  (int16_t)(pane_top - 2),
						  (int16_t)(pane_right + 2),
						  (int16_t)(pane_bottom + 2));
			if (*page_state == MFD_PAGE_STATE_CLOSING) {
#ifdef XVT_MODERN
				xvt_cockpit_pages_clear(page_index);
#endif
				if (g_mfd_active_page == page_index) {
					g_mfd_active_page =
						g_mfd_secondary_page;
					g_mfd_secondary_page =
						mfd_find_secondary_open_page();
				}
				flight_sw_set_render_target(
					NULL, DEFAULT_SCREEN_WIDTH,
					DEFAULT_SCREEN_HEIGHT, 0);
				return;
			}
#ifdef XVT_MODERN
			xvt_cockpit_pages_clear(page_index);
			xvt_cockpit_pages_record_background(page_index);
			xvt_cockpit_pages_begin_section(
				page_index, XVT_COCKPIT_PAGE_HEADER);
#endif
			flight_text_set_clear_line_background(1);
			flight_text_set_color(COLOR_HEALTHY);
			flight_text_set_font_tier(0);
			flight_text_set_clip_rect(pane_left, pane_top,
						  pane_right, pane_bottom);
			if (g_players[g_local_player].map_camera_state != 0) {
				flight_text_set_background_color(
					COLOR_MAP_BACKGROUND);
			} else if (g_players[g_local_player]
					   .view_state.hud_state_live ==
				   HUD_VIEW_FORWARD) {
				flight_text_set_background_color(
					COLOR_MFD_BACKGROUND);
			} else {
				flight_text_set_background_color(
					g_flight_transparent_color_index);
			}
			character_width = g_flight_font_digit_width;
			flight_text_set_cursor(pane_left, pane_top);
			flight_text_draw_string(
				g_str_cockpit_overlay_text
					[COCKPIT_OVERLAY_STR_CRAFT]);
			{
				int16_t health_header_x;

				health_header_x = character_width;
				health_header_x =
					(int16_t)(health_header_x *
							  CRAFT_NAME_COLUMN_WIDTH +
						  pane_left);
				flight_text_set_scratch(
					g_str_cmd_threat_display_text[1]);
				flight_text_append_scratch_string("/");
				flight_text_append_scratch_string(
					g_str_cmd_threat_display_text[2]);
				flight_text_set_cursor(health_header_x,
						       pane_top);
			}
			flight_text_draw_string(g_flight_text_scratch_buffer);
			{
				int16_t target_column_x;

				target_column_x = (int16_t)(g_flight_cursor_x +
							    character_width);
				if (show_hostile_craft == 1) {
					flight_text_set_cursor(target_column_x,
							       pane_top);
					flight_text_draw_string(
						g_str_cockpit_overlay_text
							[COCKPIT_OVERLAY_STR_TARGET]);
					if (g_flight_resolution_mode !=
					    FLIGHT_RESOLUTION_320X240) {
						int16_t orders_column_x;

						orders_column_x =
							character_width;
						orders_column_x =
							(int16_t)(orders_column_x *
									  ORDERS_COLUMN_OFFSET +
								  target_column_x);
						flight_text_set_cursor(
							orders_column_x,
							pane_top);
						flight_text_draw_string(
							g_str_cockpit_overlay_text
								[COCKPIT_OVERLAY_STR_ORDERS]);
					}
				} else if (g_players[g_local_player]
							   .map_camera_state ==
						   0 ||
					   g_flight_resolution_mode !=
						   FLIGHT_RESOLUTION_320X240) {
					flight_text_set_cursor(target_column_x,
							       pane_top);
					flight_text_draw_string_right_aligned(
						g_str_cockpit_overlay_text
							[COCKPIT_OVERLAY_STR_TARGET]);
				}
			}
#ifdef XVT_MODERN
			xvt_cockpit_pages_end_section();
#endif
			needs_redraw = 1;
			row_y = (int16_t)(pane_top + line_step);
			g_mfd_craft_list_top_row_by_mode[show_hostile_craft] =
				0;
			g_player_flight_transient_timers[g_local_player]
				.mfd_craft_list_refresh_timer = REFRESH_TICKS;
			flight_text_set_clip_rect(pane_left, row_y, pane_right,
						  pane_bottom);
			g_mfd_craft_list_cached_row_count = row_count;
		} else {
			row_y = (int16_t)(pane_top + line_step);
			character_width = g_flight_font_digit_width;
			flight_text_set_clear_line_background(1);
			flight_text_set_color(COLOR_HEALTHY);
			flight_text_set_font_tier(0);
			flight_text_set_clip_rect(pane_left, pane_top,
						  pane_right, pane_bottom);
			if (g_players[g_local_player].map_camera_state != 0) {
				flight_text_set_background_color(
					COLOR_MAP_BACKGROUND);
			} else if (g_players[g_local_player]
					   .view_state.hud_state_live ==
				   HUD_VIEW_FORWARD) {
				flight_text_set_background_color(
					COLOR_MFD_BACKGROUND);
			} else {
				flight_text_set_background_color(
					g_flight_transparent_color_index);
			}
			if (row_count != g_mfd_craft_list_cached_row_count) {
				int16_t clear_bottom;

				if (g_players[g_local_player]
					    .view_state.hud_state_live ==
				    HUD_VIEW_FORWARD) {
					flight_text_set_background_color(
						g_flight_transparent_color_index);
				}
				if (show_hostile_craft == 1 &&
				    g_players[g_local_player]
						    .map_camera_state != 0) {
					clear_bottom =
						(int16_t)(g_mfd_map_blit_height +
							  g_mfd_map_blit_source_y -
							  2);
				} else {
					clear_bottom =
						(int16_t)(g_mfd_craft_list_blit_height +
							  g_mfd_craft_list_blit_source_y -
							  2);
				}
				flight_text_set_clip_rect(
					(int16_t)(pane_left - 2), row_y,
					(int16_t)(pane_right + 2),
					(int16_t)(clear_bottom + 2));
				g_flight_fill_clip_rect_fn();
				if (g_players[g_local_player]
					    .view_state.hud_state_live ==
				    HUD_VIEW_FORWARD) {
					int16_t new_bottom;

					flight_text_set_background_color(
						COLOR_MFD_BACKGROUND);
					new_bottom =
						(int16_t)(row_y +
							  line_step *
								  row_count);
					if (new_bottom >
					    g_mfd_craft_list_blit_height +
						    g_mfd_craft_list_blit_source_y -
						    2) {
						new_bottom = pane_bottom;
					}
					flight_text_set_clip_rect(
						(int16_t)(pane_left - 2), row_y,
						(int16_t)(pane_right + 2),
						(int16_t)(new_bottom + 2));
					g_flight_fill_clip_rect_fn();
				}
				needs_redraw = 1;
				g_mfd_craft_list_cached_row_count = row_count;
			} else {
				needs_redraw = 0;
			}
		}

		if (g_mfd_active_page == page_index) {
			int16_t border_top;
			flight_text_set_background_color(COLOR_HEALTHY);
			border_top = (int16_t)(pane_top - 2);
			flight_text_set_clip_rect((int16_t)(pane_left - 2),
						  border_top,
						  (int16_t)(pane_right + 2),
						  (int16_t)(pane_bottom + 2));
#ifdef XVT_MODERN
			xvt_cockpit_pages_record_border(page_index);
#endif
			g_flight_fill_rect_clipped_fn(
				(uint16_t)(pane_left - 2), (uint16_t)border_top,
				(uint16_t)(pane_right + 2),
				(uint16_t)(pane_bottom + 2), 1);
		} else if (g_mfd_secondary_page == page_index) {
			int16_t border_top;
			if (g_players[g_local_player].map_camera_state != 0) {
				flight_text_set_background_color(
					COLOR_MAP_BACKGROUND);
			} else if (g_players[g_local_player]
					   .view_state.hud_state_live ==
				   HUD_VIEW_FORWARD) {
				flight_text_set_background_color(COLOR_MEDIUM);
			} else {
				flight_text_set_background_color(
					g_flight_transparent_color_index);
			}
			border_top = (int16_t)(pane_top - 2);
			flight_text_set_clip_rect((int16_t)(pane_left - 2),
						  border_top,
						  (int16_t)(pane_right + 2),
						  (int16_t)(pane_bottom + 2));
#ifdef XVT_MODERN
			xvt_cockpit_pages_record_border(page_index);
#endif
			g_flight_fill_rect_clipped_fn(
				(uint16_t)(pane_left - 2), (uint16_t)border_top,
				(uint16_t)(pane_right + 2),
				(uint16_t)(pane_bottom + 2), 1);
		} else if (g_players[g_local_player]
				   .view_state.hud_state_live ==
			   HUD_VIEW_FORWARD) {
			int16_t border_top;

			flight_text_set_background_color(COLOR_MEDIUM);
			border_top = (int16_t)(pane_top - 2);
			flight_text_set_clip_rect((int16_t)(pane_left - 2),
						  border_top,
						  (int16_t)(pane_right + 2),
						  (int16_t)(pane_bottom + 2));
#ifdef XVT_MODERN
			xvt_cockpit_pages_record_border(page_index);
#endif
			g_flight_fill_rect_clipped_fn(
				(uint16_t)(pane_left - 2), (uint16_t)border_top,
				(uint16_t)(pane_right + 2),
				(uint16_t)(pane_bottom + 2), 1);
		}
		if (g_players[g_local_player].map_camera_state != 0) {
			flight_text_set_background_color(COLOR_MAP_BACKGROUND);
		} else if (g_players[g_local_player]
				   .view_state.hud_state_live ==
			   HUD_VIEW_FORWARD) {
			flight_text_set_background_color(COLOR_MFD_BACKGROUND);
		} else {
			flight_text_set_background_color(
				g_flight_transparent_color_index);
		}

		if (g_mfd_active_page == page_index) {
			unsigned int action_key;

			action_key = g_current_action_key;
			switch (action_key) {
			case FLIGHT_KEY_UP:
				if (g_mfd_craft_list_top_row_by_mode
					    [show_hostile_craft] != 0) {
					--g_mfd_craft_list_top_row_by_mode
						[show_hostile_craft];
					needs_redraw = 1;
				}
				break;
			case FLIGHT_KEY_DOWN:
				if (row_count > 1 &&
				    row_count + (row_y - pane_bottom) /
							    line_step >=
					    (uint16_t)g_mfd_craft_list_top_row_by_mode
						    [show_hostile_craft]) {
					++g_mfd_craft_list_top_row_by_mode
						[show_hostile_craft];
					needs_redraw = 1;
				}
				break;
			}
		}
		if ((int16_t)g_player_flight_transient_timers[g_local_player]
			    .mfd_craft_list_refresh_timer <= 0) {
			needs_redraw = 1;
			g_player_flight_transient_timers[g_local_player]
				.mfd_craft_list_refresh_timer = REFRESH_TICKS;
		}
		last_visible_row = (int16_t)(g_mfd_craft_list_top_row_by_mode
						     [show_hostile_craft] +
					     (pane_bottom - row_y) / line_step);
		if (row_count < (uint16_t)last_visible_row) {
			if (g_mfd_craft_list_top_row_by_mode
				    [show_hostile_craft] != 0) {
				--g_mfd_craft_list_top_row_by_mode
					[show_hostile_craft];
			}
			last_visible_row =
				(int16_t)(g_mfd_craft_list_top_row_by_mode
						  [show_hostile_craft] +
					  (pane_bottom - row_y) / line_step);
		}

#ifdef XVT_MODERN
		xvt_cockpit_pages_record_scroll(
			page_index,
			g_mfd_craft_list_top_row_by_mode[show_hostile_craft],
			row_count, -1);
#endif
		last_team = UINT16_MAX;
		if (needs_redraw != 0) {
			const uint16_t *craft_row;

#ifdef XVT_MODERN
			xvt_cockpit_pages_begin_section(page_index,
							XVT_COCKPIT_PAGE_BODY);
#endif
			flight_text_set_clip_rect(pane_left, row_y, pane_right,
						  pane_bottom);
			g_flight_fill_clip_rect_fn();
			row = 0;
			if (row_count != 0) {
				craft_row = craft_rows;
				do {
					uint16_t packed_row;
					uint16_t team;
					uint16_t listed_object_idx;
					const char *status_color;

					flight_text_set_color(
						COLOR_NORMAL_TEXT);
					if (row < (uint16_t)g_mfd_craft_list_top_row_by_mode
							    [show_hostile_craft] ||
					    row > (uint16_t)last_visible_row) {
						continue;
					}
					packed_row = *craft_row;
					listed_object_idx =
						(uint16_t)(packed_row & 0xFF);
					team = (uint16_t)(packed_row >> 8);
					status_color =
						&g_mfd_craft_list_team_color_codes
							[team];
					flight_text_set_color(
						(uint8_t)*status_color);
					if (last_team != team &&
					    row == (uint16_t)g_mfd_craft_list_top_row_by_mode
							    [show_hostile_craft]) {
						last_team = team;
						if (g_players[g_local_player]
							    .map_camera_state !=
						    0) {
							flight_sw_set_render_target(
								NULL,
								DEFAULT_SCREEN_WIDTH,
								DEFAULT_SCREEN_HEIGHT,
								0);
							flight_text_set_background_color(
								COLOR_MFD_BACKGROUND);
							flight_text_set_background_color(
								COLOR_MAP_BACKGROUND);
							flight_sw_set_render_target(
								g_flight_offscreen_buffer,
								g_screen_width,
								g_screen_height,
								g_screen_width *
									g_flight_bytes_per_pixel);
						}
						flight_text_set_clip_rect(
							pane_left, row_y,
							pane_right,
							pane_bottom);
					}
					mfd_build_scratch_craft_list_name(
						listed_object_idx);
					flight_text_set_cursor(pane_left,
							       row_y);
					g_flight_text_scratch_buffer[9] = 0;
					flight_text_draw_string(
						g_flight_text_scratch_buffer);
					{
						int16_t health_column_x;
						int16_t status_column_base_x;
						struct object_record *object;
						struct mobile_object
							*mobile_object;

						health_column_x =
							(int16_t)(pane_left +
								  CRAFT_NAME_COLUMN_WIDTH *
									  character_width);
						status_column_base_x =
							health_column_x;
						object =
							&g_object_table
								[listed_object_idx];
						mobile_object = object->mobj;
						if (mobile_object != NULL) {
							struct craft_data
								*craft;
							int hull_percent;

							craft = mobile_object
									->p_craft;
							if (craft != NULL &&
							    craft->object_kind !=
								    CRAFT_OBJECT_KIND_BREAKING_UP &&
							    craft->object_kind !=
								    CRAFT_OBJECT_KIND_EXPLODING) {
								int shield_average;
								unsigned int
									max_shield;
								int shield_percent;
								int percentage;

								shield_average =
									(craft->shield_energy
										 [0] +
									 craft->shield_energy
										 [1]) /
									2;
								max_shield = (unsigned int)
									craft_get_object_max_shield(
										listed_object_idx);
								if (shield_average !=
									    0 &&
								    max_shield !=
									    0) {
									percentage = (uint16_t)math2_longratio_q16(
										(unsigned int)
											shield_average,
										max_shield);
									shield_percent =
										2 *
										(percentage /
										 PERCENTAGE_SCALE);
								} else {
									shield_percent =
										0;
								}
								if (shield_percent <=
								    HEALTH_CRITICAL_PERCENT) {
									flight_text_set_color(
										COLOR_DAMAGED);
								} else if (
									shield_percent <=
									HEALTH_GOOD_PERCENT) {
									flight_text_set_color(
										COLOR_MEDIUM);
								} else {
									flight_text_set_color(
										COLOR_HEALTHY);
								}
								flight_text_set_cursor(
									health_column_x +
										character_width,
									row_y);
								flight_text_draw_decimal_number(
									shield_percent,
									HEALTH_DIGIT_WIDTH,
									1);
								flight_text_set_color(
									(uint8_t)*status_color);
								g_flight_draw_char_fn(
									'/');
							}
							hull_percent = 0;
							if (craft->object_kind !=
								    CRAFT_OBJECT_KIND_BREAKING_UP &&
							    craft->object_kind !=
								    CRAFT_OBJECT_KIND_EXPLODING) {
								if (craft->hull_damage >
								    craft->hull_max) {
									hull_percent =
										1;
								} else {
									hull_percent = (uint16_t)math2_longratio_q16(
										craft->hull_max -
											craft->hull_damage,
										craft->hull_max);
									hull_percent =
										(uint16_t)
											hull_percent /
										PERCENTAGE_SCALE;
									if (hull_percent ==
									    0) {
										hull_percent =
											100;
									}
								}
							}
							if (hull_percent != 0) {
								int digit_count;

								digit_count = (uint16_t)
									flight_text_format_scratch_int(
										hull_percent);
								flight_text_set_cursor(
									g_flight_cursor_x +
										(HEALTH_DIGIT_WIDTH -
										 digit_count) *
											character_width,
									row_y);
								if (hull_percent <=
								    HEALTH_CRITICAL_PERCENT) {
									flight_text_set_color(
										COLOR_DAMAGED);
								} else if (
									hull_percent <=
									HEALTH_GOOD_PERCENT) {
									flight_text_set_color(
										COLOR_MEDIUM);
								} else {
									flight_text_set_color(
										COLOR_HEALTHY);
								}
								flight_text_draw_string(
									g_flight_text_scratch_buffer);
							}
							flight_text_set_color((
								uint8_t)*status_color);
							{
								int16_t target_column_x;

								target_column_x =
									(int16_t)(g_flight_cursor_x +
										  2 * character_width);
								status_column_base_x =
									target_column_x;
								if (g_players[g_local_player]
										    .map_camera_state ==
									    0 ||
								    show_hostile_craft !=
									    0 ||
								    g_flight_resolution_mode !=
									    FLIGHT_RESOLUTION_320X240) {
									flight_text_set_cursor(
										target_column_x,
										row_y);
									if (g_object_table[listed_object_idx]
										    .player_owner_idx !=
									    -1) {
										uint16_t
											target_object_idx;

										target_object_idx =
											g_players[g_object_table[listed_object_idx]
													  .player_owner_idx]
												.current_target_object_idx;
										if (target_object_idx !=
											    UINT16_MAX &&
										    target_object_idx <
											    g_active_region_craft_object_slot_end &&
										    g_object_table[target_object_idx]
												    .mobj !=
											    NULL) {
											hud_format_object_display_name(
												(uint16_t)
													target_object_idx,
												DISPLAY_NAME_FLAGS);
										} else {
											flight_text_set_scratch(
												g_str_mesh_component_names
													[32]);
										}
									} else {
										uint16_t
											target_object_idx;

										target_object_idx =
											(uint16_t)craft
												->ai_controller
												.target_obj_idx;
										if (target_object_idx !=
											    TARGET_NONE_AI &&
										    target_object_idx !=
											    UINT16_MAX &&
										    target_object_idx <
											    0x8000) {
											hud_format_object_display_name(
												(uint16_t)
													target_object_idx,
												DISPLAY_NAME_FLAGS);
										} else {
											flight_text_set_scratch(
												g_str_mesh_component_names
													[32]);
										}
									}
									if (show_hostile_craft !=
										    0 ||
									    g_flight_resolution_mode ==
										    FLIGHT_RESOLUTION_320X240 ||
									    g_players[g_local_player]
											    .map_camera_state !=
										    0) {
										flight_text_draw_string(
											g_flight_text_scratch_buffer);
									} else {
										flight_text_draw_string_right_aligned(
											g_flight_text_scratch_buffer);
									}
								}
							}
						}
						if (show_hostile_craft != 0 &&
						    g_flight_resolution_mode !=
							    FLIGHT_RESOLUTION_320X240) {
							int16_t status_column_x;
							unsigned int
								status_string_id;

							status_column_x =
								character_width;
							status_column_x =
								(int16_t)(status_column_x *
										  STATUS_COLUMN_OFFSET +
									  status_column_base_x);
							flight_text_set_cursor(
								status_column_x,
								row_y);
							status_string_id = (uint16_t)
								mfd_get_flight_group_goal_status_string_id(
									listed_object_idx);
							if (status_string_id !=
							    FG_GOAL_STATUS_STR_NONE) {
								flight_text_draw_string(
									g_str_cockpit_overlay_text
										[status_string_id]);
							} else {
								flight_text_draw_string(
									g_str_mesh_component_names
										[32]);
							}
						}
					}
					row_y = (uint16_t)(row_y + line_step);
				} while (++craft_row, ++row < row_total);
			}
		}
#ifdef XVT_MODERN
		xvt_cockpit_pages_end_section();
#endif
	} while (0);
	flight_sw_set_render_target(NULL, DEFAULT_SCREEN_WIDTH,
				    DEFAULT_SCREEN_HEIGHT, 0);
}

/* Puts an object's list name in g_flight_text_scratch_buffer: for a craft, its
 * flight group's name and, when hud_mission_fg_get_craft_number_if_shown gives
 * one, a space and its number in one or two digits; for any other object an
 * empty string. */
// FUNCTION: XVT 0x44E0A0
void mfd_build_scratch_craft_list_name(uint16_t object_idx)
{
	struct object_record *object;
	struct mobile_object *mobile_object;
	struct craft_data *craft;
	int flight_group_idx;
	uint16_t craft_number;
	uint16_t tens_digit;
	uint16_t ones_digit;

	g_flight_text_scratch_buffer[0] = '\0';
	object = &g_object_table[object_idx];
	mobile_object = object->mobj;
	if (mobile_object != 0 && mobile_object->family == 0) {
		craft = mobile_object->p_craft;
		flight_group_idx = object->flight_group_idx;
		flight_text_append_scratch_string(
			g_mission_flight_groups[flight_group_idx].fg.name);
		craft_number =
			(uint16_t)hud_mission_fg_get_craft_number_if_shown(
				flight_group_idx, craft);
		if (craft_number != 0) {
			flight_text_append_scratch_char(' ');
			if (craft_number >= 10) {
				tens_digit = craft_number / 10;
				ones_digit = craft_number % 10;
				flight_text_append_scratch_char(
					(char)(tens_digit + '0'));
				flight_text_append_scratch_char(
					(char)(ones_digit + '0'));
			} else {
				flight_text_append_scratch_char(
					(char)(craft_number + '0'));
			}
		}
	}
}

/* Picks the goal word for an object in the hostile craft list, an
 * FG_GOAL_STATUS_STR_ value: from the local team's pending primary goals on
 * the object's flight group, and from those triggers of the team's global
 * goal 0 that match that group. Inspect comes first while the
 * team has not identified the craft; capture or board on a moving craft also
 * asks to disable it; with an all-special-cargo goal only the special cargo
 * craft gets capture, board, disable or destroy. Then capture, board,
 * destroy, attack, else FG_GOAL_STATUS_STR_NONE, which is also the answer for
 * a projectile slot. */
// FUNCTION: XVT 0x44E170
int16_t mfd_get_flight_group_goal_status_string_id(uint16_t object_index)
{
	unsigned int goal_index;
	struct flight_group_goal *goal;
	int16_t *player_team;
	unsigned int global_trigger_index;
	struct mission_trigger_pair *trigger_pair;
	struct mission_trigger *trigger;
	int event_condition;
	unsigned int flight_group_idx;
	int inspect_active;
	int disable_active;
	int capture_active;
	int board_active;
	struct craft_data *craft;
	int destroy_active;
	int special_cargo_only;
	int attack_active;

	if (g_projectile_object_slot_start <= object_index &&
	    g_projectile_object_slot_end > object_index) {
		return FG_GOAL_STATUS_STR_NONE;
	}
	if (g_active_region_craft_object_slot_end > object_index) {
		craft = g_object_table[object_index].mobj->p_craft;
	} else {
		craft = NULL;
	}

	inspect_active = 0;
	destroy_active = 0;
	disable_active = 0;
	attack_active = 0;
	capture_active = 0;
	board_active = 0;
	flight_group_idx = g_object_table[object_index].flight_group_idx;
	special_cargo_only = 0;
	player_team = &g_players[g_local_player].team;

	for (goal_index = 0; goal_index < 8; ++goal_index) {
		goal = &g_mission_flight_groups[flight_group_idx]
				.fg.goals[goal_index];
		if (goal->enabled_teams[(uint16_t)*player_team] != 0 &&
		    goal->goal_kind == 0 &&
		    g_mission_fg_stats[flight_group_idx]
				    .goal_state[8 * (uint16_t)*player_team +
						goal_index] == 4) {
			if (goal->amount == GOAL_AMT_ALL_SPECIAL_CARGO) {
				if (g_mission_fg_stats[flight_group_idx].special_cargo_outcome
					    [FLIGHT_GROUP_OUTCOME_INSPECTED] ==
				    0) {
					inspect_active = 1;
				}
				special_cargo_only = 1;
			}
			event_condition = goal->event_condition;
			if (event_condition == 2) {
				destroy_active = 1;
			} else if (event_condition == 3) {
				attack_active = 1;
			} else if (event_condition == 8) {
				disable_active = 1;
			} else if (event_condition == 5) {
				inspect_active = 1;
			} else if (event_condition == 4) {
				capture_active = 1;
			} else if (event_condition == 6) {
				board_active = 1;
			}
		}
	}

	for (global_trigger_index = 0; global_trigger_index < 4;
	     ++global_trigger_index) {
		if (global_trigger_index < 2) {
			trigger_pair = &g_mission_global_goals[(
				uint16_t)*player_team][0]
						.trigger_pairs[0];
		} else {
			trigger_pair = &g_mission_global_goals[(
				uint16_t)*player_team][0]
						.trigger_pairs[1];
		}
		if ((global_trigger_index & 1) == 0) {
			trigger = &trigger_pair->triggers[0];
		} else {
			trigger = &trigger_pair->triggers[1];
		}
		event_condition = trigger->condition;
		if (event_condition != 10 &&
		    mission_flight_group_matches_trigger_variable(
			    flight_group_idx, trigger->variable_type,
			    trigger->variable)) {
			if (event_condition == 2) {
				destroy_active = 1;
			} else if (event_condition == 3) {
				attack_active = 1;
			} else if (event_condition == 8) {
				disable_active = 1;
			} else if (event_condition == 5) {
				inspect_active = 1;
			} else if (event_condition == 4) {
				capture_active = 1;
			} else if (event_condition == 6) {
				board_active = 1;
			}
		}
	}

	if (g_active_region_craft_object_slot_end > object_index) {
		if (inspect_active != 0 &&
		    craft->identified_order_by_team[(uint16_t)*player_team] !=
			    0) {
			inspect_active = 0;
		}
		if (capture_active != 0 || board_active != 0) {
			if (g_object_table[object_index].mobj->speed != 0) {
				disable_active = 1;
			}
			if (special_cargo_only != 0 &&
			    g_mission_flight_groups[flight_group_idx]
					    .fg.special_cargo_craft !=
				    craft->craft_ordinal) {
				capture_active = 0;
				board_active = 0;
			}
		}
		if (disable_active != 0 && special_cargo_only != 0 &&
		    g_mission_flight_groups[flight_group_idx]
				    .fg.special_cargo_craft !=
			    craft->craft_ordinal) {
			disable_active = 0;
		}
		if (destroy_active != 0 && special_cargo_only != 0 &&
		    g_mission_flight_groups[flight_group_idx]
				    .fg.special_cargo_craft !=
			    craft->craft_ordinal) {
			destroy_active = 0;
		}
	}

	if (inspect_active != 0) {
		return FG_GOAL_STATUS_STR_INSPECT;
	}
	if (disable_active != 0) {
		if (capture_active != 0) {
			return FG_GOAL_STATUS_STR_CAPTURE;
		}
		return board_active == 0 ? FG_GOAL_STATUS_STR_DISABLE
					 : FG_GOAL_STATUS_STR_BOARD;
	}
	if (capture_active != 0) {
		return FG_GOAL_STATUS_STR_CAPTURE;
	}
	if (board_active != 0) {
		return FG_GOAL_STATUS_STR_BOARD;
	}
	if (destroy_active != 0) {
		return FG_GOAL_STATUS_STR_DESTROY;
	}
	return attack_active == 0 ? FG_GOAL_STATUS_STR_NONE
				  : FG_GOAL_STATUS_STR_ATTACK;
}

/* Draws the map help page in the map view's pane: nine rows of
 * g_str_map_room_text (rows 1 to 4 in two colors) when the page is first drawn
 * or the local player's map camera state changes. In 640x480, with the map
 * shown and this page active, action keys 0xA8 to 0xAD show a block of
 * g_mfd_developer_credits_lines instead. It makes itself the active page when
 * none is and draws the active or secondary border; when the page is closing
 * it clears it, makes the secondary page active, and returns. Writes
 * g_mfd_active_page, g_mfd_secondary_page and g_mfd_map_help_camera_state_cache. */
// FUNCTION: XVT 0x44E5A0
void mfd_draw_map_help_page(void)
{
	enum {
		MFD_BACKGROUND_COLOR = 0x34,
		MFD_ACTIVE_BACKGROUND_COLOR = 0x46,
		MFD_TEXT_COLOR = 0x43,
		MFD_HIGHLIGHT_COLOR = 0x4A,
		MFD_CREDIT_COLOR = 0x4E,
		MFD_DEFAULT_WIDTH = 320,
		MFD_DEFAULT_HEIGHT = 200
	};

	int16_t left;
	int16_t border_top;
	int16_t bottom;
	int16_t top;
	int16_t right;
	int16_t column;
	int16_t line_step;
	int16_t text_mode;
	int first_credit;
	int last_credit;
	int local_player;
	int row;
	int row_start;
	uint16_t active_page;
	int pitch_bytes;

	pitch_bytes = g_flight_bytes_per_pixel;
	pitch_bytes *= g_screen_width;
	flight_sw_set_render_target(g_flight_offscreen_buffer, g_screen_width,
				    g_screen_height, pitch_bytes);
	flight_text_set_font_tier(0);
	left = (int16_t)(g_mfd_map_blit_source_x + 2);
	right = (int16_t)(g_mfd_map_blit_width + g_mfd_map_blit_source_x - 2);
	top = (int16_t)(g_mfd_map_blit_source_y + 2);
	bottom = (int16_t)(g_mfd_map_blit_source_y + g_mfd_map_blit_height - 2);
#ifdef XVT_MODERN
	xvt_cockpit_pages_set_origin(MFD_PAGE_MAP_HELP, left - 2, top - 2);
#endif
	active_page = g_mfd_active_page;
	if (active_page == MFD_PAGE_NONE) {
		active_page = MFD_PAGE_MAP_HELP;
	}
	g_mfd_active_page = active_page;
	do {
		if (active_page == MFD_PAGE_MAP_HELP) {
			flight_text_set_background_color(
				MFD_ACTIVE_BACKGROUND_COLOR);
		} else {
			if (g_mfd_secondary_page != MFD_PAGE_MAP_HELP) {
				break;
			}
			flight_text_set_background_color(MFD_BACKGROUND_COLOR);
		}
		border_top = (int16_t)(top - 2);
		flight_text_set_clip_rect(left - 2, border_top, right + 2,
					  bottom + 2);
#ifdef XVT_MODERN
		xvt_cockpit_pages_record_border(MFD_PAGE_MAP_HELP);
#endif
		g_flight_fill_rect_clipped_fn(
			(uint16_t)(left - 2), (uint16_t)border_top,
			(uint16_t)(right + 2), (uint16_t)(bottom + 2), 1);
	} while (0);

	if (g_hud_element_state_cache[g_hud_instrument_set_base_index +
				      HUD_MFD_MAP_OR_COMMAND_ELEMENT] !=
	    g_mfd_page_states[MFD_PAGE_MAP_HELP]) {
		flight_text_set_background_color(MFD_BACKGROUND_COLOR);
		flight_text_set_clip_rect(left - 2, top - 2, right + 2,
					  bottom + 2);
#ifdef XVT_MODERN
		xvt_cockpit_pages_clear(MFD_PAGE_MAP_HELP);
		xvt_cockpit_pages_record_background(MFD_PAGE_MAP_HELP);
#endif
		g_flight_fill_clip_rect_fn();
		if (g_mfd_page_states[MFD_PAGE_MAP_HELP] ==
		    MFD_PAGE_STATE_CLOSING) {
#ifdef XVT_MODERN
			xvt_cockpit_pages_clear(MFD_PAGE_MAP_HELP);
#endif
			if (g_mfd_active_page == MFD_PAGE_MAP_HELP) {
				g_mfd_active_page = g_mfd_secondary_page;
				g_mfd_secondary_page =
					mfd_find_secondary_open_page();
			}
			flight_sw_set_render_target(NULL, MFD_DEFAULT_WIDTH,
						    MFD_DEFAULT_HEIGHT, 0);
			return;
		}
		flight_text_set_clear_line_background(1);
		flight_text_set_color(MFD_TEXT_COLOR);
		flight_text_set_font_tier(0);
		flight_text_set_clip_rect(left, top, right, bottom);
		line_step = (int16_t)(g_flight_font_line_height + 2);
		flight_text_set_clip_rect(left, top, right, bottom);
		local_player = g_local_player;
		g_mfd_map_help_camera_state_cache =
			g_players[local_player].map_camera_state;
		text_mode = 1;
	} else {
		line_step = (int16_t)(g_flight_font_line_height + 2);
		flight_text_set_clear_line_background(1);
		flight_text_set_color(MFD_TEXT_COLOR);
		flight_text_set_font_tier(0);
		flight_text_set_clip_rect(left, top, right, bottom);
		flight_text_set_background_color(MFD_BACKGROUND_COLOR);
		local_player = g_local_player;
		if (g_mfd_map_help_camera_state_cache !=
		    g_players[local_player].map_camera_state) {
			g_mfd_map_help_camera_state_cache =
				g_players[local_player].map_camera_state;
			text_mode = 1;
		} else {
			text_mode = 0;
		}
	}

	if (g_flight_resolution_mode == FLIGHT_RESOLUTION_640X480 &&
	    g_players[local_player].map_camera_state != 0 &&
	    g_mfd_active_page == MFD_PAGE_MAP_HELP) {
		switch (g_current_action_key) {
		case 0xA8:
			text_mode = 3;
			first_credit = 21;
			last_credit = 29;
			break;
		case 0xA9:
			text_mode = 3;
			first_credit = 0;
			last_credit = 5;
			break;
		case 0xAA:
			text_mode = 2;
			first_credit = 0;
			last_credit = 9;
			break;
		case 0xAB:
			text_mode = 3;
			first_credit = 5;
			last_credit = 13;
			break;
		case 0xAC:
			text_mode = 3;
			first_credit = 29;
			last_credit = 37;
			break;
		case 0xAD:
			text_mode = 3;
			first_credit = 13;
			last_credit = 21;
			break;
		default:
			break;
		}
	}

#ifdef XVT_MODERN
	if (text_mode != 0) {
		xvt_cockpit_pages_record_mode(MFD_PAGE_MAP_HELP, text_mode);
		xvt_cockpit_pages_begin_section(MFD_PAGE_MAP_HELP,
						XVT_COCKPIT_PAGE_BODY);
	}
#endif
	if (text_mode == 1) {
#ifdef XVT_MODERN
		xvt_cockpit_pages_record_scroll(MFD_PAGE_MAP_HELP, 0, 9, -1);
#endif
		row = 0;
		flight_text_set_word_wrap(0);
		g_flight_fill_clip_rect_fn();
		while (row < 9) {
			row_start = row;
			flight_text_set_cursor(left, (uint16_t)top);
			flight_text_set_scratch(g_str_map_room_text[row_start]);
			if (row_start > 0 && row_start < 5) {
				flight_text_append_scratch_string(
					g_str_map_room_text[row_start + 1]);
				flight_text_draw_string(
					g_str_map_room_text[row_start]);
				flight_text_set_color(MFD_HIGHLIGHT_COLOR);
				flight_text_draw_string(
					g_str_map_room_text[row_start + 1]);
				flight_text_set_color(MFD_TEXT_COLOR);
			} else {
				flight_text_draw_string(
					g_str_map_room_text[row_start]);
			}
			if (g_flight_resolution_mode ==
			    FLIGHT_RESOLUTION_320X240) {
				int16_t width;
				width = (int16_t)(flight_text_measure_string_width(
							  g_flight_text_scratch_buffer) +
						  1);
				if (width < 16) {
					width = 16;
				}
				column = (int16_t)(left + width);
			} else {
				column =
					(int16_t)(left +
						  flight_text_measure_string_width(
							  g_str_map_room_text
								  [8]) +
						  5);
			}
			flight_text_set_cursor(column, (uint16_t)top);
			if (row_start > 0 && row_start < 5) {
				flight_text_draw_string(
					g_str_map_room_text[row_start + 9]);
				++row;
				flight_text_set_color(MFD_HIGHLIGHT_COLOR);
				flight_text_draw_string(
					g_str_map_room_text[row_start + 10]);
				flight_text_set_color(MFD_TEXT_COLOR);
			} else {
				flight_text_draw_string(
					g_str_map_room_text[row_start + 9]);
			}
			top = (int16_t)(top + line_step);
			++row;
		}
	} else if (text_mode == 2) {
		int credit_index;

#ifdef XVT_MODERN
		xvt_cockpit_pages_record_scroll(MFD_PAGE_MAP_HELP, first_credit,
						last_credit - first_credit, -1);
#endif
		g_flight_fill_clip_rect_fn();
		flight_text_set_color(MFD_CREDIT_COLOR);
		for (credit_index = first_credit; credit_index < last_credit;
		     ++credit_index) {
			flight_text_set_cursor(left, (uint16_t)top);
			flight_text_draw_string(
				g_mfd_developer_credits_lines[credit_index]);
			top = (int16_t)(top + line_step);
		}
	} else if (text_mode == 3) {
		int credit_index;

#ifdef XVT_MODERN
		xvt_cockpit_pages_record_scroll(MFD_PAGE_MAP_HELP, first_credit,
						1 + last_credit - first_credit,
						-1);
#endif
		g_flight_fill_clip_rect_fn();
		flight_text_set_color(MFD_CREDIT_COLOR);
		for (credit_index = 0; credit_index < 1; ++credit_index) {
			flight_text_set_cursor(left, (uint16_t)top);
			flight_text_draw_string(
				g_mfd_developer_credits_lines[credit_index]);
			top = (int16_t)(top + line_step);
		}
		for (credit_index = first_credit; credit_index < last_credit;
		     ++credit_index) {
			flight_text_set_cursor(left, (uint16_t)top);
			flight_text_draw_string(
				g_mfd_developer_credits_lines[credit_index +
							      10]);
			top = (int16_t)(top + line_step);
		}
	}

#ifdef XVT_MODERN
	xvt_cockpit_pages_end_section();
#endif
	flight_sw_set_render_target(NULL, MFD_DEFAULT_WIDTH, MFD_DEFAULT_HEIGHT,
				    0);
}

/* Opens or closes an MFD page for the local player. Outside the map view, a
 * closed page opens as the active page and the old active page becomes the
 * secondary one; any other state turns to closing. When neither craft list
 * is then closed, the list page does not name turns to closing, and
 * g_mfd_secondary_page goes back to its earlier value unless that was a craft
 * list; when page was being closed, that earlier value is read unset. In the
 * map view, a page other than the message log first closes every other open
 * page but the friendly craft list and the message log; then an open page
 * turns to closing, or a closed one opens as the active page. Writes
 * g_mfd_page_states, g_mfd_active_page and g_mfd_secondary_page. */
// FUNCTION: XVT 0x4803F0
void mfd_toggle_page(uint16_t page)
{
	int16_t *page_state;
	int16_t previous_secondary_page;
	uint16_t other_page;

	if (g_players[g_local_player].map_camera_state == 0) {
		page_state = &g_mfd_page_states[page];
		if (*page_state != MFD_PAGE_STATE_CLOSED) {
			*page_state = MFD_PAGE_STATE_CLOSING;
		} else {
			int16_t previous_active_page;

			previous_active_page = g_mfd_active_page;
			*page_state = MFD_PAGE_STATE_OPEN;
			previous_secondary_page = g_mfd_secondary_page;
			g_mfd_secondary_page = previous_active_page;
			g_mfd_active_page = page;
		}
		if (g_mfd_page_states[MFD_PAGE_FRIENDLY_CRAFT] !=
			    MFD_PAGE_STATE_CLOSED &&
		    g_mfd_page_states[MFD_PAGE_HOSTILE_CRAFT] !=
			    MFD_PAGE_STATE_CLOSED) {
			if (page == MFD_PAGE_FRIENDLY_CRAFT) {
				g_mfd_page_states[MFD_PAGE_HOSTILE_CRAFT] =
					MFD_PAGE_STATE_CLOSING;
			} else {
				g_mfd_page_states[MFD_PAGE_FRIENDLY_CRAFT] =
					MFD_PAGE_STATE_CLOSING;
			}
			if (previous_secondary_page !=
				    MFD_PAGE_FRIENDLY_CRAFT &&
			    previous_secondary_page != MFD_PAGE_HOSTILE_CRAFT) {
				g_mfd_secondary_page = previous_secondary_page;
			}
		}
		return;
	}

	if (page != MFD_PAGE_MESSAGE_LOG) {
		for (other_page = MFD_PAGE_SCOREBOARD;
		     other_page < MFD_PAGE_COUNT; ++other_page) {
			if (page != other_page &&
			    other_page != MFD_PAGE_FRIENDLY_CRAFT &&
			    other_page != MFD_PAGE_MESSAGE_LOG &&
			    g_mfd_page_states[other_page] !=
				    MFD_PAGE_STATE_CLOSED) {
				g_mfd_page_states[other_page] =
					MFD_PAGE_STATE_CLOSING;
			}
		}
	}
	page_state = &g_mfd_page_states[page];
	if (*page_state != MFD_PAGE_STATE_CLOSED) {
		*page_state = MFD_PAGE_STATE_CLOSING;
		return;
	}
	*page_state = MFD_PAGE_STATE_OPEN;
	g_mfd_secondary_page = g_mfd_active_page;
	g_mfd_active_page = page;
}

/* Returns the first page, from MFD_PAGE_SCOREBOARD up, that is not closed
 * and is not g_mfd_active_page, or -1 when there is none. */
// FUNCTION: XVT 0x480530
int16_t mfd_find_secondary_open_page(void)
{
	uint16_t active_page;
	uint16_t page;

	active_page = g_mfd_active_page;
	for (page = MFD_PAGE_SCOREBOARD; page < MFD_PAGE_COUNT; page++) {
		if (g_mfd_page_states[page] != MFD_PAGE_STATE_CLOSED &&
		    active_page != page) {
			return (int16_t)page;
		}
	}
	return -1;
}

/* Draws the message log page in the ready-message pane, the newest message
 * first from the scroll offset: each in its prefix code's color (its sender
 * IFF's color for code 2), '[' and ']' switching the color of the text between
 * them, a period added unless it ends in '?', '!', ':' or a space, and its
 * mission time right-aligned, with hours only when not 0. While the page is
 * active, action keys 0xA6 and 0xA7 scroll one line and 0xAC and 0xAD four; it
 * redraws when scrolled or when the log grows. When the page state turns to
 * closing it clears the pane, makes the secondary page active, and returns
 * cursor_y, which the modern build sets to 0 and the original leaves unset;
 * otherwise it returns the last display offset the pane covers. Writes
 * g_mfd_message_log_scroll_offset, g_mfd_message_log_redraw,
 * g_mfd_message_log_last_draw_total_count, g_message_log_records,
 * g_flight_text_shadow_enabled, g_flight_text_color_index, g_mfd_active_page and
 * g_mfd_secondary_page. */
// FUNCTION: XVT 0x49EC40
int16_t mfd_draw_message_log_page(void)
{
	int16_t left;
	int16_t top;
	int16_t right;
	int16_t bottom;
	int16_t line_height;
	int16_t cursor_y;
	int16_t display_offset;
	int16_t last_display_offset;
	int16_t max_display_offset;
	int16_t log_record_count;
	unsigned int pitch;
	int record_index;
	uint8_t prefix_code;
	uint8_t ch;
	char *text;

#ifdef XVT_MODERN
	ch = 0;
	cursor_y = 0;
#endif

	left = (int16_t)(g_ready_message_pane_left + 2);
	top = (int16_t)(g_ready_message_pane_top + 2);
	right = (int16_t)(g_ready_message_pane_right - 2);
	bottom = (int16_t)(g_ready_message_pane_bottom - 2);
	pitch = g_flight_bytes_per_pixel * g_screen_width;
	flight_sw_set_render_target(g_flight_offscreen_buffer, g_screen_width,
				    g_screen_height, (int)pitch);
	if (g_hud_element_state_cache[g_hud_instrument_set_base_index +
				      HUD_MFD_MESSAGE_LOG_ELEMENT] !=
	    g_mfd_page_states[MFD_PAGE_MESSAGE_LOG]) {
		flight_text_set_background_color(
			g_flight_transparent_color_index);
		flight_text_set_clip_rect(g_ready_message_pane_left,
					  g_ready_message_pane_top,
					  g_ready_message_pane_right,
					  g_ready_message_pane_bottom);
#ifdef XVT_MODERN
		xvt_cockpit_messages_clear(XVT_COCKPIT_MESSAGE_READY);
#endif
		g_flight_fill_clip_rect_fn();
		if (g_mfd_page_states[MFD_PAGE_MESSAGE_LOG] ==
		    MFD_PAGE_STATE_CLOSING) {
#ifdef XVT_MODERN
			xvt_cockpit_pages_clear(MFD_PAGE_MESSAGE_LOG);
#endif
			if (g_mfd_active_page == MFD_PAGE_MESSAGE_LOG) {
				g_mfd_active_page = g_mfd_secondary_page;
				g_mfd_secondary_page =
					mfd_find_secondary_open_page();
			}
			flight_sw_set_render_target(NULL, 320, 200, 0);
			return cursor_y;
		}
	}

	flight_text_set_clip_rect(left, top, right, bottom);
	flight_text_set_word_wrap(1);
	flight_text_set_clear_line_background(1);
	flight_text_set_font_tier(0);
#ifdef XVT_MODERN
	xvt_cockpit_pages_set_origin(
		MFD_PAGE_MESSAGE_LOG,
		g_ready_message_pane_left -
			(g_flight_player_count >= 1
				 ? 2 * g_flight_font_digit_width
				 : 0),
		g_ready_message_pane_top);
#endif
	flight_text_set_background_color(g_flight_transparent_color_index);
	g_flight_text_shadow_enabled = 1;
	line_height = (int16_t)(g_flight_font_line_height + 2);
	flight_text_set_color(0x43);
	log_record_count = (int16_t)g_message_log_write_index;
	g_message_log_records =
		(struct hud_in_flight_message_record *)memory_get_handle_block(
			g_message_log_handle);
	memory_handle_block_done_stub(g_message_log_handle);
	cursor_y = top;

	if (g_hud_element_state_cache[g_hud_instrument_set_base_index +
				      HUD_MFD_MESSAGE_LOG_ELEMENT] !=
	    g_mfd_page_states[MFD_PAGE_MESSAGE_LOG]) {
		g_mfd_message_log_scroll_offset = 0;
		g_mfd_message_log_last_draw_total_count = 0;
		g_mfd_message_log_redraw = 1;
		g_flight_fill_clip_rect_fn();
	} else {
		if (g_mfd_active_page == MFD_PAGE_MESSAGE_LOG) {
			switch (g_current_action_key) {
			case 0xA6:
				if (g_mfd_message_log_scroll_offset > 0) {
					--g_mfd_message_log_scroll_offset;
					g_mfd_message_log_redraw = 1;
				}
				break;
			case 0xA7:
				max_display_offset = g_message_log_wrapped != 0
							     ? 300
							     : log_record_count;
				if ((top - bottom) / line_height +
					    max_display_offset >=
				    g_mfd_message_log_scroll_offset) {
					++g_mfd_message_log_scroll_offset;
					g_mfd_message_log_redraw = 1;
				}
				break;
			case 0xAC:
				if (g_mfd_message_log_scroll_offset > 4) {
					g_mfd_message_log_scroll_offset -= 4;
					g_mfd_message_log_redraw = 1;
				}
				break;
			case 0xAD:
				max_display_offset = g_message_log_wrapped != 0
							     ? 300
							     : log_record_count;
				if (max_display_offset +
					    (top - bottom) / line_height - 4 >=
				    g_mfd_message_log_scroll_offset) {
					g_mfd_message_log_scroll_offset += 4;
					g_mfd_message_log_redraw = 1;
				}
				break;
			default:
				break;
			}
		}
		if (g_message_log_total_count !=
		    g_mfd_message_log_last_draw_total_count) {
			g_mfd_message_log_redraw = 1;
		}
	}

	last_display_offset = (int16_t)((bottom - top) / line_height +
					g_mfd_message_log_scroll_offset);
	if (g_mfd_message_log_redraw != 0) {
#ifdef XVT_MODERN
		xvt_cockpit_pages_record_background(MFD_PAGE_MESSAGE_LOG);
		xvt_cockpit_pages_begin_section(MFD_PAGE_MESSAGE_LOG,
						XVT_COCKPIT_PAGE_BODY);
#endif
		g_flight_fill_clip_rect_fn();
		for (display_offset = 0; display_offset <= last_display_offset;
		     ++display_offset) {
			if (display_offset >= g_mfd_message_log_scroll_offset &&
			    display_offset < 300) {
				flight_text_set_cursor(left, cursor_y);
				record_index = mfd_get_message_log_record_index(
					display_offset);
				if (record_index >= 0) {
					text = g_message_log_records
						       [record_index]
							       .text;
					prefix_code = (uint8_t)text[0];
					if (prefix_code < 9) {
						flight_text_set_color(
							g_message_text_prefix_color_codes
								[prefix_code]);
						++text;
						if (prefix_code == 1) {
							if ((uint8_t)text[0] >=
								    '0' &&
							    (uint8_t)text[0] <=
								    '3') {
								flight_text_set_color(
									g_message_text_prefix_color_codes
										[(uint8_t)text
											 [0] -
										 '(']);
								++text;
							}
						} else if (prefix_code == 2) {
							flight_text_set_color(
								g_message_sender_iff_color_codes
									[g_message_log_records[record_index]
										 .sender_iff]);
						}
					} else {
						flight_text_set_color(0x42);
					}
					while (*text != '\0') {
						if (*text == '[') {
							if (g_flight_text_color_index ==
							    0xD4) {
								--g_flight_text_color_index;
							} else {
								++g_flight_text_color_index;
							}
						} else if (*text == ']') {
							if (g_flight_text_color_index ==
							    0xD3) {
								++g_flight_text_color_index;
							} else {
								--g_flight_text_color_index;
							}
						} else {
							g_flight_draw_char_fn(
								(uint8_t)*text);
						}
						ch = (uint8_t)*text;
						++text;
					}
					if (ch != '?' && ch != '!' &&
					    ch != ':' && ch != ' ') {
						g_flight_draw_char_fn('.');
					}
					flight_text_set_background_color(
						g_flight_transparent_color_index);
					g_flight_draw_char_fn('\n');
					flight_text_set_color(0x42);
					flight_text_set_background_color(
						g_flight_transparent_color_index);
					if (g_message_log_records[record_index]
						    .clock_hour != 0) {
						flight_text_set_cursor(
							right - flight_text_measure_string_width(
									"00:00:00 "),
							cursor_y);
						flight_text_draw_decimal_number(
							g_message_log_records
								[record_index]
									.clock_hour,
							2, 1);
						g_flight_draw_char_fn(':');
						flight_text_draw_decimal_number(
							g_message_log_records
								[record_index]
									.clock_minute,
							2, 2);
					} else {
						flight_text_set_cursor(
							right - flight_text_measure_string_width(
									"00:00 "),
							cursor_y);
						flight_text_draw_decimal_number(
							g_message_log_records
								[record_index]
									.clock_minute,
							2, 1);
					}
					g_flight_draw_char_fn(':');
					flight_text_draw_decimal_number(
						g_message_log_records
							[record_index]
								.clock_second,
						2, 2);
					g_flight_draw_char_fn(' ');
					cursor_y = (int16_t)(cursor_y +
							     line_height);
				}
			}
		}
	}

#ifdef XVT_MODERN
	xvt_cockpit_pages_end_section();
	xvt_cockpit_pages_record_scroll(
		MFD_PAGE_MESSAGE_LOG, g_mfd_message_log_scroll_offset,
		g_message_log_wrapped ? 300 : log_record_count, -1);
#endif
	flight_text_set_word_wrap(0);
	g_mfd_message_log_redraw = 0;
	if (g_players[g_local_player].map_camera_state == 0 &&
	    g_mfd_active_page == MFD_PAGE_NONE) {
		g_mfd_active_page = MFD_PAGE_MESSAGE_LOG;
	}
	if (g_mfd_active_page == MFD_PAGE_MESSAGE_LOG) {
		flight_text_set_background_color(0x46);
		flight_text_set_clip_rect(left - 2, top - 2, right + 2,
					  bottom + 2);
#ifdef XVT_MODERN
		xvt_cockpit_pages_record_border(MFD_PAGE_MESSAGE_LOG);
#endif
		g_flight_fill_rect_clipped_fn(left - 2, top - 2, right + 2,
					      bottom + 2, 1);
	} else if (g_mfd_secondary_page == MFD_PAGE_MESSAGE_LOG) {
		flight_text_set_background_color(
			g_flight_transparent_color_index);
		flight_text_set_clip_rect(left - 2, top - 2, right + 2,
					  bottom + 2);
#ifdef XVT_MODERN
		xvt_cockpit_pages_record_border(MFD_PAGE_MESSAGE_LOG);
#endif
		g_flight_fill_rect_clipped_fn(left - 2, top - 2, right + 2,
					      bottom + 2, 1);
	}
	flight_sw_set_render_target(NULL, 320, 200, 0);
	g_mfd_message_log_last_draw_total_count = g_message_log_total_count;
	return last_display_offset;
}

/* Maps a display offset, 0 for the newest message, to its index in the
 * 300-entry message log: while 300 or fewer were logged,
 * g_message_log_total_count - display_offset - 1, negative past the oldest; after
 * that, counting back from g_message_log_write_index and wrapping at 300. Does
 * not check that display_offset is below 300. */
// FUNCTION: XVT 0x49F330
int mfd_get_message_log_record_index(int display_offset)
{
	int record_index;

	if (g_message_log_total_count <= 300) {
		return g_message_log_total_count - display_offset - 1;
	}
	record_index = g_message_log_write_index - display_offset - 1;
	if (record_index < 0) {
		record_index += 300;
	}
	return record_index;
}
