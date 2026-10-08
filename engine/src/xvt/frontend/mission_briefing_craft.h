#ifndef XVT_FRONTEND_MISSION_BRIEFING_CRAFT_H
#define XVT_FRONTEND_MISSION_BRIEFING_CRAFT_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum mission_briefing_craft_screen_faction {
	MISSION_BRIEFING_CRAFT_SCREEN_REBEL = 0x0,
	MISSION_BRIEFING_CRAFT_SCREEN_IMPERIAL = 0x1,
} mission_briefing_craft_screen_faction;

typedef enum mission_briefing_launch_countdown_state {
	MISSION_BRIEFING_COUNTDOWN_IDLE = 0x0,
	MISSION_BRIEFING_COUNTDOWN_ACTIVE = 0x1,
	MISSION_BRIEFING_COUNTDOWN_EXPIRED = 0x2,
} mission_briefing_launch_countdown_state;

extern int g_mission_briefing_craft_selection_active;
extern mission_briefing_craft_screen_faction
	g_mission_briefing_craft_screen_faction;

int mission_briefing_craft_selection_exit(int frame_counter);
int mission_briefing_craft_selection_update(int frame_counter);
int mission_briefing_broadcast_roster_and_assignments(void);
int mission_briefing_are_all_network_players_ready(void);

#ifdef __cplusplus
}
#endif

#endif
