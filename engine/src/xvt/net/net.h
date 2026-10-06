#ifndef XVT_NET_NET_H
#define XVT_NET_NET_H

#include <stddef.h>
#include <stdint.h>

#include "aeron/compat/dplay.h"
#include "aeron/compat/win_types.h"
#include "xvt/util/win32.h"
#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 1)

/* A lobby player: an entry of the roster g_front_state.net_players, where entry
 * 0 is the local player, or the local player's copy kept in
 * net_runtime_local_player. */
struct net_player_info {
	/* DirectPlay long name; the lobby puts the player's rating plus one in
	 * its first byte. */
	char long_name[16];
	char player_name[16]; /* DirectPlay short name: the player's name. */
	/* Id DirectPlay gave the player; 0 in an unused entry. */
	DPID player_id;
	/* 1 when marked ready (net_set_player_ready, net_mark_player_ready_no_lock);
	 * net_refresh_player_roster carries it over. */
	int ready_flag;
};

#pragma pack(pop)
typedef char xvt_size_net_player_info[(sizeof(struct net_player_info) == 40)
					      ? 1
					      : -1];

/* Link figures for one player in g_net_player_connection_stats. On the host they
 * come from the player's keepalive acks; on the other players, from the
 * totals the host sends in its lobby packets. */
struct net_player_connection_stats {
	int player_id; /* The player's DirectPlay id; 0 marks a free entry. */
	uint32_t latency_total_ms; /* Sum of the latency samples, in ms. */
	int packet_count;	   /* Delivered-packet count reported. */
	int packet_drop_count;	   /* Drop count reported. */
	int packet_retry_count;	   /* Retry count reported. */
	/* Samples summed in latency_total_ms, which divided by this gives the
	 * average. */
	uint32_t latency_sample_count;
};

typedef enum network_transport_type {
	NET_TRANSPORT_IPX = 0x0,
	NET_TRANSPORT_TCPIP = 0x1,
	NET_TRANSPORT_MODEM = 0x2,
	NET_TRANSPORT_SERIAL = 0x3,
} network_transport_type;

/* Game opcodes carried inside DirectPlay messages. System notifications use DPSYS_*.
 * Phase-specific names distinguish overlapping frontend and flight payloads. */
enum net_packet_type {
	NET_PACKET_NONE = 0, /* No application event; not transmitted. */
	/* Shared reliability and handshake. */
	NET_PACKET_WORLD_NACK = 51,
	NET_PACKET_PONG = 52,
	NET_PACKET_PING = 53,
	NET_PACKET_KEEPALIVE_ACK = 54,
	NET_PACKET_KEEPALIVE = 55,
	NET_PACKET_NACK = 56,
	NET_PACKET_NOP = 57,
	NET_PACKET_SEQUENCE_STATUS = 59,

	/* Flight. */
	NET_PACKET_REMOTE_INPUT = 1,
	NET_PACKET_WORLD_MESSAGE = 2,
	NET_PACKET_PLAYER_DISCONNECTED = 3,
	NET_PACKET_WORLD_CHECKSUM = 4,
	NET_PACKET_SESSION_ABORT = 8,
	NET_PACKET_STARTUP_READY = 13,
	NET_PACKET_ROSTER_COUNT = 14,
	NET_PACKET_ROSTER_ENTRY = 15,
	NET_PACKET_RESYNC_CHUNK_ACK = 16,
	NET_PACKET_PLAYER_OPTIONS = 17,
	NET_PACKET_PLAYER_OPTIONS_ROSTER = 18,
	NET_PACKET_FLIGHT_MISSION_START = 19,
	NET_PACKET_MISSION_LOADING_READY = 20,
	NET_PACKET_PLAYER_ABORT = 21,
	NET_PACKET_INPUT_BATCH = 22,
	NET_PACKET_RESYNC_NOTICE = 23,
	NET_PACKET_SERVER_CHECKSUM = 24,
	NET_PACKET_ACK = 27,
	NET_PACKET_CLOCK_LEAD = 28,
	NET_PACKET_STILL_LOADING = 29,
	NET_PACKET_CLOCK_PROBE = 30,
	NET_PACKET_CLOCK_PROBE_REPLY = 31,
	NET_PACKET_PLAYER_TAUNTS = 32,
	NET_PACKET_FLIGHT_SESSION_STATUS = 58,
	NET_PACKET_RESYNC_CHECKSUMS = 60,
	NET_PACKET_RESYNC_REQUEST = 61,
	NET_PACKET_RESYNC_APPLY = 62,
	NET_PACKET_RESYNC_CHUNK = 63,

	/* Frontend. */
	NET_PACKET_FRONTEND_GAME_STARTED = 58,
	NET_PACKET_PROBE_REQUEST = 64,
	NET_PACKET_STATE = 65,
	NET_PACKET_JOIN_REQUEST = 66,
	NET_PACKET_GAME_FULL = 68,
	NET_PACKET_PLAYER_ADMITTED = 69,
	NET_PACKET_HOST_CANCELLED = 70,
	NET_PACKET_PLAYER_LEFT = 71,
	NET_PACKET_TEAM_ASSIGNMENTS_READY = 72,
	NET_PACKET_FRONTEND_MISSION_START = 73,
	NET_PACKET_FRONTEND_OPCODE_74 =
		74, /* Probed by the legacy dialog-dismiss path. */
	NET_PACKET_NEXT_TOURNAMENT_MISSION = 75,
	NET_PACKET_NEXT_BATTLE_MISSION = 76,
	NET_PACKET_CHAT = 77,
	NET_PACKET_TEAM_ASSIGNMENT = 79,
	NET_PACKET_FLIGHT_ASSIGNMENT_NOTIFY = 80,
	NET_PACKET_PLAYER_READY = 81,
	NET_PACKET_PLAYER_UNREADY = 82,
	NET_PACKET_RETURN_TO_SETUP = 83,
	NET_PACKET_CLEAR_TEAM_ASSIGNMENTS = 84,
	NET_PACKET_TEAM_ASSIGNMENTS = 85,
	NET_PACKET_CLEAR_FLIGHT_ASSIGNMENTS = 86,
	NET_PACKET_FLIGHT_ASSIGNMENTS = 87,
	NET_PACKET_MISSION_CHOICE = 88,
	NET_PACKET_GAME_OPTIONS = 89,
	NET_PACKET_REPLAY_MISSION = 90,
	NET_PACKET_PLAYER_KICKED = 91,
	NET_PACKET_SESSION_CANCELLED = 92,
	NET_PACKET_VERSION_MISMATCH = 94,
	NET_PACKET_PASSWORD_REQUIRED = 95,
	NET_PACKET_ROSTER_LOCKED = 96,
	NET_PACKET_LAUNCH_ROSTER_AND_ASSIGNMENTS = 97,
	NET_PACKET_CRAFT_LOADOUT = 98,
	NET_PACKET_BRIEFING_ENTERED = 99,
	NET_PACKET_BRIEFING_COUNTDOWN = 100,
	NET_PACKET_ASSIGNMENT_COUNTDOWN = 101,
	NET_PACKET_RETURN_TO_MISSION_SELECTION = 102,
	NET_PACKET_RELEASE_TEAM_RESERVATION = 103,
	NET_PACKET_TEAM_RESERVATION = 104,
	NET_PACKET_PLAYER_UNAVAILABLE = 105,
	NET_PACKET_RELEASE_FLIGHT_RESERVATION = 106,
	NET_PACKET_FLIGHT_RESERVATION = 107,
	NET_PACKET_CHAT_SYNC_REQUEST = 108,
	NET_PACKET_CHAT_SYNC_CHUNK = 109,
	NET_PACKET_FLIGHT_ASSIGNMENT = 110,
	NET_PACKET_LOBBY_SELECTION = 111,
	NET_PACKET_FLIGHT_ASSIGNMENTS_READY = 112,
	NET_PACKET_LOADOUT_ROSTER = 113,
	NET_PACKET_PROBE_RESPONSE = 114,
	NET_PACKET_READY_ROSTER = 116,
	NET_PACKET_PILOT_RATING = 117,
	NET_PACKET_REPLAY_CURRENT_MISSION = 118,
	NET_PACKET_BATTLE_CONTINUATION = 119,
	NET_PACKET_BATTLE_PROGRESS = 120,
	NET_PACKET_NEXT_CAMPAIGN_MISSION = 121,
	NET_PACKET_REPLAY_CAMPAIGN_MISSION = 122,
	NET_PACKET_CAMPAIGN_CONTINUATION = 123,
	NET_PACKET_MOVIE_SYNC = 124,
	NET_PACKET_SUBMIT_MISSION_CHOICE = 125,
};

/* GUID layout with the final eight bytes grouped for session key arithmetic. */
struct net_session_guid {
	unsigned int data1;    /* GUID bytes 0 to 3. */
	unsigned short data2;  /* GUID bytes 4 and 5. */
	unsigned short data3;  /* GUID bytes 6 and 7. */
	unsigned int data4[2]; /* GUID bytes 8 to 15, as two words. */
};

typedef char xvt_size_net_session_guid[(sizeof(struct net_session_guid) == 16)
					       ? 1
					       : -1];

struct net_session_enum_entry {
	char session_name[32]; /* Session name, cut to 31 characters. */
	/* The session's DirectPlay instance GUID. */
	struct net_session_guid session_guid;
};

extern struct net_player_connection_stats g_net_player_connection_stats[40];
/* drift-ok: camelcase -- DirectPlay's interface id */
extern const GUID IID_IDirectPlay2A;
extern network_transport_type g_net_active_transport_type;

void net_shutdown_direct_play_session_for_quit(void);
void net_shutdown_direct_play_session(void);
int net_shutdown_direct_play_session_ex(int suppress_restart,
					int wait_for_handshake_acks);
int net_refresh_player_roster(void);
int AERON_DXAPI net_enum_players_callback(DPID player_id, uint32_t player_type,
					  const DPNAME *name_desc,
					  uint32_t flags, void *context);
int net_create_direct_play_player(const char *long_player_info,
				  const char *short_player_name);
const GUID *
net_get_direct_play_service_provider_guid(network_transport_type network_type);
void net_pump_incoming_packets(void);
int net_send_packet_and_flush(int to_player_id, const void *packet,
			      unsigned int packet_size);
int net_send_packet_internal(int to_player_id, const void *packet,
			     unsigned int packet_size);
int net_send_direct_play_packet(int dest_player_id, const void *packet,
				int packet_size, int unused_send_mode);
int net_send_sequenced_direct_play_packet(int dest_player_id, int packet_class,
					  int sequence_id, const void *packet,
					  unsigned int packet_size);
struct net_player_info *net_get_player_roster(int *out_count);
int net_did_ready_player_leave_this_frame(void);
int net_is_host(void);
int net_poll_for_packet_type_or_backlog(int packet_type);
int net_poll_for_player_created_or_backlog(void);
int *net_get_next_app_packet(DPID *out_sender_id, uint32_t *out_packet_size);
void net_handle_direct_play_system_message(int packet_type,
					   const void *packet_data);
void *net_dequeue_incoming_packet(DPID *out_sender_id,
				  uint32_t *out_packet_size);
int *net_wait_for_app_packet(DPID *out_sender_id, uint32_t *out_packet_size,
			     int timeout_seconds);
int net_get_host_player_id(void);
int net_get_local_player_id(void);
void net_mark_player_ready_no_lock(int player_id);
void net_clear_player_ready_flag(int player_id);
int net_is_player_ready(int player_id);
struct net_player_info *net_find_player(int player_id);
int net_set_player_ready(int player_id);
void net_clear_player_ready_flag_with_lock_guard(int player_id);
int net_count_ready_players(void);
void net_clear_player_ready_flags(void);
int net_session_import_runtime_state(
	void **dplay_interface_out, GUID *app_guid_out, GUID *session_guid_out,
	int32_t *group_id_out, int *host_player_id_out,
	struct net_player_info *local_player_info_out,
	struct net_queued_packet *recv_queue_out, int32_t *recv_queue_read_out,
	int *recv_queue_count_out, int *recv_queue_write_out,
	struct net_reliable_peer_slot *reliable_peer_slots_out,
	uint32_t *reliable_peer_slot_count_out,
	uint32_t *broadcast_seq_counter_out, char *broadcast_payload_out,
	int *broadcast_payload_length_out, int *broadcast_piggyback_empty_out,
	uint32_t *group_seq_counter_out, char *group_payload_out,
	int *group_payload_length_out, int *group_piggyback_empty_out,
	struct net_queued_packet *sent_history_out,
	int *sent_history_write_index_out);
int net_session_export_runtime_state(
	void **dplay_interface, const void *app_guid, const void *session_guid,
	const int *group_id, const int *host_player_id,
	const void *local_player_info,
	const struct net_queued_packet *recv_queue_entries,
	const int *recv_queue_read, const int *recv_queue_count,
	const int *recv_queue_write,
	const struct net_reliable_peer_slot *reliable_peer_slots,
	const int *reliable_peer_slot_count, const int *broadcast_seq_counter,
	const void *broadcast_payload, const int *broadcast_payload_length,
	const int *broadcast_piggyback_empty, const int *group_seq_counter,
	const void *group_payload, const int *group_payload_length,
	const int *group_piggyback_empty,
	const struct net_queued_packet *sent_history,
	const int *sent_history_write_index,
	struct net_queued_packet *sent_world_message_history,
	const int *sent_world_message_write_index);
int net_compact_reliable_peer_slots_for_roster(void);
int net_send_sequence_keepalives(void);
int net_check_and_record_incoming_sequence(int player_id, int sequence_id,
					   int use_channel0, int use_channel2);
int net_find_queued_sequenced_packet(int unused_queue_index, int sequence_id,
				     int use_channel0, int use_channel2,
				     int peer_slot_index);
int net_remove_incoming_packet_at_index(unsigned int queue_index);
unsigned int net_get_average_latency_ms(int player_id);
int net_set_player_latency_ms(int player_id, int latency_ms);
int net_set_player_name_with_lock_guard(unsigned int player_id,
					const char *long_name,
					const char *short_name);
int net_refresh_player_roster_with_lock_guard(void);
unsigned int net_find_or_create_peer_slot(int direct_play_id);
int net_get_packet_drop_rate_basis_points(int player_id);
int net_drop_silent_peers(void);
int net_get_player_packet_count(int player_id);
int net_get_player_packet_drop_count(int player_id);
int net_get_player_packet_retry_count(int player_id);
int net_set_player_packet_count(int player_id, int packet_count);
int net_set_player_packet_drop_count(int player_id, int packet_drop_count);
int net_set_player_packet_retry_count(int player_id, int packet_retry_count);
int net_wait_for_shutdown_handshake_acks(void);

#ifdef __cplusplus
}
#endif

#endif
