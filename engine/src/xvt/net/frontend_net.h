#ifndef XVT_NET_FRONTEND_NET_H
#define XVT_NET_FRONTEND_NET_H

#include <stdint.h>

#include "xvt/net/net.h"
#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
	FRONTEND_NET_PROTOCOL_VERSION =
#ifdef XVT_MODERN
		103
#else
		101
#endif
};

#pragma pack(push, 1)

struct frontend_net_packet_scratch {
	int packet_type;      /* NET_PACKET_ value naming the packet */
	uint8_t payload[508]; /* Body; its layout depends on packet_type */
};

#pragma pack(pop)
typedef char xvt_size_frontend_net_packet_scratch
	[(sizeof(struct frontend_net_packet_scratch) == 512) ? 1 : -1];

struct frontend_net_session_entry {
	char game_name[32]; /* Session name from DirectPlay */
	/* DirectPlay session GUID; a key of 0 marks an empty entry */
	struct net_session_guid session_guid;
	/* Players needed: 9 until probed, 0 if full or no answer came */
	unsigned int players_needed;
	/* GetTickCount ms of the last probe; 0 before any */
	unsigned int last_query_ms;
	/* Host's protocol version: this build's until probed, 0 if no answer */
	unsigned int version;
	uint8_t password_required; /* Nonzero when the game needs a password */
	uint8_t game_in_flight;	   /* 1 when the probe found the game flying */
};

extern int g_frontend_net_session_count;
extern int g_frontend_net_received_mission_description_id;
extern int g_frontend_net_received_mission_directory_id;
extern const unsigned int g_frontend_net_xvt_direct_play_app_guid[4];
extern struct frontend_net_packet_scratch g_frontend_net_packet_scratch;
extern struct frontend_net_session_entry g_frontend_net_session_list[32];
extern int g_frontend_net_session_list_scroll_offset;
extern int g_frontend_net_selected_session_idx;
extern int g_frontend_net_packet_sender_player_id;
extern int g_frontend_net_packet_arg0;
extern int g_frontend_net_reserving_player_id;
extern int g_host_game_start_pending;
extern int g_frontend_quick_start_launch_flag;
extern int g_frontend_net_probe_version;
extern int g_frontend_net_probe_players_needed;
extern int g_frontend_net_probe_password_required;
extern int g_frontend_net_probe_mission_elapsed_seconds;
extern int g_frontend_briefing_entered_count;
extern char g_frontend_chat_input_buffer[100];
extern char *g_frontend_chat_log_buffer;
extern int g_frontend_chat_log_used_bytes;
extern int g_frontend_chat_team_only;
extern int g_frontend_chat_scroll_offset;
extern char g_frontend_net_selected_game_name[32];

int frontend_net_draw_join_game_list(int frame_counter);
int frontend_net_join_game_screen(int frame_counter);
int frontend_net_await_join_admission_screen(int frame_counter);
int frontend_net_draw_join_game_mission_briefing(void);
int frontend_net_draw_join_game_player_roster(void);
int frontend_net_update_and_draw_chat_panel(int frame_counter);
int frontend_net_draw_join_game_sidebars_and_query_all(void);
int frontend_net_host_game_exit(int frame_counter);
int frontend_net_host_game_screen(int frame_counter);
int frontend_net_process_network_packets(void);

#ifndef XVT_MODERN
int frontend_net_connect_to_selected_game_screen(int frame_counter);
int frontend_net_make_session_guid_key(struct net_session_guid guid);
int frontend_net_refresh_session_list(void);
int frontend_net_compare_session_list_entries(
	const struct frontend_net_session_entry *lhs,
	const struct frontend_net_session_entry *rhs);
int frontend_net_sort_sessions(void);
int frontend_net_probe_all_sessions(void);
int frontend_net_probe_session_by_index(int session_idx);
#endif

#ifdef __cplusplus
}
#endif

#endif
