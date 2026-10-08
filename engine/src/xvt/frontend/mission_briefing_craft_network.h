#ifndef XVT_FRONTEND_MISSION_BRIEFING_CRAFT_NETWORK_H
#define XVT_FRONTEND_MISSION_BRIEFING_CRAFT_NETWORK_H

#ifdef __cplusplus
extern "C" {
#endif

/* The craft selection screen's network part: what
 * mission_briefing_craft_network.c shares with mission_briefing_craft.c,
 * which runs the screen. */

/* What a part of mission_briefing_craft_selection_update returns when it
 * did not end the frame; the frame's own returns are 0 or 1. */
enum { CRAFT_SELECTION_FRAME_GOES_ON = -1 };

enum { MAX_PLAYERS = 8 };

void mission_briefing_craft_send_loadout(void);
int mission_briefing_craft_handle_packet(int frame_counter);
int mission_briefing_broadcast_roster_and_assignments(void);
int mission_briefing_are_all_network_players_ready(void);

#ifdef __cplusplus
}
#endif

#endif
