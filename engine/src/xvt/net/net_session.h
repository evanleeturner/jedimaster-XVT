#ifndef XVT_NET_NET_SESSION_H
#define XVT_NET_NET_SESSION_H

#include <stdint.h>

#include "xvt/net/net.h"
#include "xvt/net/net_reliable.h"
#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 1)

struct session_player_info {
	/* DirectPlay long name; the formal name in a solo flight */
	char long_name[16];
	char player_name[16]; /* Short name; pilot_name if solo */
	int direct_play_id;   /* DirectPlay player id */
	int active_flag;      /* 1 while the player is in the session */
};

#pragma pack(pop)
typedef char xvt_size_session_player_info
	[(sizeof(struct session_player_info) == 40) ? 1 : -1];

#pragma pack(push, 1)

struct net_session_scratch_packet {
	int packet_type;	 /* NET_PACKET_ value naming the packet */
	int payload_dwords[127]; /* Body; its layout depends on packet_type */
};

#pragma pack(pop)
typedef char xvt_size_net_session_scratch_packet
	[(sizeof(struct net_session_scratch_packet) == 512) ? 1 : -1];

#pragma pack(push, 1)

struct net_session_scratch_state {
	int packet_type;	 /* NET_PACKET_ value naming the packet */
	int payload_dwords[127]; /* Body; its layout depends on packet_type */
	/* Zeroed by net_session_reset_for_flight; nothing reads it */
	int trailing_state;
};

#pragma pack(pop)
typedef char xvt_size_net_session_scratch_state
	[(sizeof(struct net_session_scratch_state) == 516) ? 1 : -1];

/* DirectPlay flight-session state is reset as one block by the original game. */
#pragma pack(push, 1)

struct net_session_state {
	/* Never named; only net_session_reset_for_flight's clear sets it */
	uint32_t reserved_state0;
	/* Taken from the frontend; NULL when there is none or a startup wait
	 * timed out, and then sends are only queued for this player */
	IDirectPlay2A *dplay_interface;
	/* Never named; only net_session_reset_for_flight's clear sets it */
	uint32_t reserved_state1;
	/* Transport given to net_session_init_game_session; nothing reads it */
	network_transport_type network_type;
	/* DirectPlay application GUID from the frontend; nothing reads it */
	GUID app_guid;
	/* GUID of the joined session, from the frontend; nothing reads it */
	GUID instance_guid;
	/* DirectPlay group of the players; sends to it use the group channel */
	int group_dplay_id;
	int local_is_host; /* Nonzero when this player hosts */
	int host_dplay_id; /* The host's DirectPlay id */
	int player_count;  /* Entries in players */
	/* Set to 0 by net_session_pump_incoming_packets; nothing reads it */
	int receive_pump_state;
	/* Never named; only net_session_reset_for_flight's clear sets it */
	uint8_t reserved_session_state[32];
	/* Only ever set to 0. When nonzero, net_session_receive_packet gives up
	 * a gap a fixed time after one NACK: 3,000 ms, or 40,000 ms for world
	 * messages. */
	int reliable_use_fixed_resend_timeouts;
	/* This player's own entry, from the frontend */
	struct session_player_info local_player_info;
	struct session_player_info players[8]; /* The roster */
	/* Queue of player entries; only functions nothing calls use it */
	struct session_player_info player_info_queue[8];
	/* Next sequence, 0-127, for packets to all players */
	uint32_t broadcast_seq_counter;
	/* Type byte and body of the last packet to all players, sent again
	 * behind the next one */
	uint8_t broadcast_payload[512];
	int broadcast_payload_length; /* Bytes used in broadcast_payload */
	/* While 1, the next packet to all carries a NOP, not a copy */
	int broadcast_piggyback_empty;
	/* Next sequence, 0-127, for the group channel, which also carries
	 * internet-play inputs */
	uint32_t group_seq_counter;
	/* Type byte and body of the last group-channel packet, sent again
	 * behind the next one */
	uint8_t group_payload[512];
	int group_payload_length; /* Bytes used in group_payload */
	/* While 1, the next group packet carries a NOP, not a copy */
	int group_piggyback_empty;
	/* Per-peer delivery state: sequences, saved copy, activity, counts */
	struct net_reliable_peer_slot reliable_peer_slots[40];
	/* Never named in code. It follows reliable_peer_slots, so
	 * net_session_send_packet's unchecked use of slot 40, when all 40 slots
	 * are taken, lands here. */
	struct net_reliable_peer_slot reliable_peer_overflow_slot;
	unsigned int reliable_peer_slot_count; /* reliable_peer_slots in use */
	int player_info_queue_count; /* Entries in player_info_queue */
	/* Copy of the packet net_session_receive_packet last returned; callers
	 * get a pointer into it */
	struct net_queued_packet recv_scratch_packet;
};

#pragma pack(pop)
typedef char xvt_size_net_session_state
	[(sizeof(struct net_session_state) == 25652 + sizeof(void *)) ? 1 : -1];

extern struct net_session_state g_net_session;

extern struct net_session_scratch_state g_net_session_scratch_packet;
extern int g_net_session_sent_history_write_index;
extern int g_net_session_sent_world_message_write_index;
extern struct net_queued_packet g_net_session_sent_history[128];
extern struct net_queued_packet g_net_session_sent_world_message_history[256];

int net_session_init_game_session(const char *formal_name,
				  const char *pilot_name, int is_host,
				  const char *mp_game_name,
				  network_transport_type network_type,
				  int num_human_players, int in_progress_launch,
				  const char *connection_address);
int net_session_shutdown(void);
int net_session_enumerate_players(void);
int AERON_DXAPI net_session_enum_players_callback(DPID dplay_id,
						  uint32_t player_type,
						  const DPNAME *name_info,
						  uint32_t flags,
						  void *context);
/* Only contiguous retransmissions advance the channel receive watermark. */
static inline void net_session_advance_received_sequence(int *received_sequence,
							 unsigned int sequence)
{
	unsigned int next_sequence = (unsigned int)*received_sequence + 1;
	if (next_sequence > 127) {
		next_sequence = 0;
	}
	if (next_sequence == sequence) {
		*received_sequence = next_sequence;
	}
}

void net_session_pump_incoming_packets(void);
int net_session_broadcast_packet_to_players(unsigned int *payload,
					    int payload_size);
int net_session_send_packet(int direct_play_id, unsigned int *payload,
			    signed int payload_size);
int net_session_send_sequenced_game_packet(int dest_dplay_id,
					   uint8_t packet_class,
					   uint8_t sequence,
					   const unsigned int *packet,
					   unsigned int packet_size);
struct session_player_info *net_session_get_player_roster(int *out_count);
int net_session_get_player_count(void);
int net_session_is_local_host(void);
int *net_session_receive_game_packet(int *out_sender_dpid,
				     int *out_payload_size);
int net_session_handle_direct_play_system_message(int packet_opcode,
						  const int *packet);
void *net_session_receive_packet(int *out_sender_dpid, int *out_payload_size);
int net_session_send_compact_game_packet(int direct_play_id,
					 unsigned int *payload,
					 int payload_size, ...);
int net_session_find_player_slot_by_dpid(int dpid);
int net_session_get_host_dplay_id(void);
int net_session_get_local_dplay_id(void);
char *net_session_get_player_name(int player_slot);
int net_session_add_player_to_group(
	const struct session_player_info *player_info);
int net_session_count_active_players(void);
int net_session_remove_player_from_group(int player_dplay_id);
int net_session_stub_return_true(void);
int net_session_get_fixed_payload_size(int packet_type);
int net_session_send_reliable_keepalives(void);

#ifdef __cplusplus
}
#endif

#endif
