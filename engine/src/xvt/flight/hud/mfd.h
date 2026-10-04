#ifndef XVT_FLIGHT_HUD_MFD_H
#define XVT_FLIGHT_HUD_MFD_H

#include <stdint.h>

#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

enum mfd_page_id {
	MFD_PAGE_SCOREBOARD = 0,
	MFD_PAGE_GOALS = 1,
	MFD_PAGE_MESSAGE_LOG = 2,
	MFD_PAGE_DAMAGE = 3,
	MFD_PAGE_HOSTILE_CRAFT = 4,
	MFD_PAGE_FRIENDLY_CRAFT = 5,
	MFD_PAGE_MAP_HELP = 6,
	MFD_PAGE_UNUSED = 7,
	MFD_PAGE_COUNT = 8,
	MFD_PAGE_NONE = UINT16_MAX,
};

enum mfd_page_state {
	MFD_PAGE_STATE_CLOSING = -2,
	MFD_PAGE_STATE_REOPENED =
		-1, ///< Open state produced when a display rebuild cancels a pending close.
	MFD_PAGE_STATE_CLOSED = 0,
	MFD_PAGE_STATE_OPEN = 1,
};

extern uint16_t g_mfd_active_page;
extern uint16_t g_mfd_secondary_page;
extern int16_t g_mfd_page_states[MFD_PAGE_COUNT];
extern uint16_t g_mfd_saved_active_page;
extern uint16_t g_mfd_saved_secondary_page;
extern int16_t g_saved_mfd_page_states[MFD_PAGE_COUNT];
extern uint16_t g_mfd_map_help_camera_state_cache;
extern int16_t g_mfd_mission_scoreboard_first_visible_row;
extern int16_t g_mfd_mission_scoreboard_last_player_count;
extern int16_t g_mfd_mission_scoreboard_last_width;
extern uint16_t g_mfd_craft_list_cached_row_count;
extern int16_t g_mfd_craft_list_top_row_by_mode[2];
extern const char g_mfd_craft_list_team_color_codes[11];
extern const char *g_str_map_room_text[20];
extern const char *g_mfd_developer_credits_lines[47];

int16_t mfd_draw_mission_goals_page(void);
void mfd_draw_mission_scoreboard_page(void);
void mfd_draw_craft_list_page(uint16_t show_hostile_craft);
void mfd_build_scratch_craft_list_name(uint16_t object_idx);
int16_t mfd_get_flight_group_goal_status_string_id(uint16_t object_index);
void mfd_draw_map_help_page(void);
void mfd_toggle_page(uint16_t page);
int16_t mfd_find_secondary_open_page(void);
int16_t mfd_draw_message_log_page(void);
int mfd_get_message_log_record_index(int display_offset);

#ifdef __cplusplus
}
#endif

#endif
