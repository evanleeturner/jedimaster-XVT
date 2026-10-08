#ifndef XVT_NET_FRONTEND_NET_SETUP_PACKETS_H
#define XVT_NET_FRONTEND_NET_SETUP_PACKETS_H

#include <stdint.h>

#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* What frontend_net_setup_packets.c, the mission setup packets, shares
 * with frontend_net_packets.c, the frontend's packet handler: the packets'
 * sizes and limits, the search for this player's roster entry, and the
 * switch the handler passes every packet it does not handle itself. */

enum {
	MAX_PLAYERS = 8,
	TEAM_COUNT = 10,
	PLAYER_NAME_COPY_SIZE = 13,
	ROSTER_PACKET_FIRST_PLAYER_WORD = 13,
	ROSTER_PACKET_COMPACT_FIRST_PLAYER_WORD = 2,
	ROSTER_OPTION_WORD_COUNT = 5,
	CHAT_LOG_CAPACITY = 1024,
	CHAT_LOG_CONTENT_LIMIT = 1022,
	CHAT_SYNC_CHUNK_SIZE = 400,
	FLIGHT_GROUP_ASSIGNMENT_COUNT = 80,
	MISSION_ASSIGNMENT_TEAM_BYTES = 320,
	MISSION_SETUP_PLAYER_LIMIT = 8,
	BEGIN_BUTTON_LOCKOUT_FRAMES = 24,
	MIN_PRESET_CRAFT_CATEGORY = 1,
	MAX_STANDARD_PRESET_CRAFT_CATEGORY = 2,
	SPECIAL_PRESET_CRAFT_CATEGORY = 3,
	SPECIAL_PRESET_CRAFT_OFFSET = 5,
};

void frontend_net_find_local_roster_entry(int *roster_index);
void frontend_net_process_setup_packet(int *packet_type, DPID sender_player_id,
				       int *payload, uint8_t *payload_bytes);

#ifdef __cplusplus
}
#endif

#endif
