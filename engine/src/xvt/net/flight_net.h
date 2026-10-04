#ifndef XVT_NET_FLIGHT_NET_H
#define XVT_NET_FLIGHT_NET_H

#include <stdint.h>

#include "xvt/net/net.h"
#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

extern int g_flight_net_world_message_turn_timestamp;
extern int g_flight_net_small_session_player_threshold;
extern int g_flight_net_last_sent_world_message_timestamp;
extern int g_flight_net_checksum_request_accum_ticks;
extern int g_flight_net_sent_world_message_count;
extern int g_flight_net_received_world_message_count;
extern int g_net_update_interval_ticks;
extern int g_server_tick_time;
extern int g_flight_net_clock_adjust_accum_ticks;
extern int g_flight_net_clock_probe_timestamp;
extern int g_flight_net_host_timeout_elapsed_ticks;

#pragma pack(push, 1)

struct flight_net_scratch_packet {
	int packet_type;	 /* NET_PACKET_ value naming the packet */
	int payload_dwords[127]; /* Body; its layout depends on packet_type */
};

#pragma pack(pop)
typedef char xvt_size_flight_net_scratch_packet
	[(sizeof(struct flight_net_scratch_packet) == 512) ? 1 : -1];

#pragma pack(push, 1)

struct flight_net_input_batch_packet {
	int packet_type;     /* Always NET_PACKET_INPUT_BATCH */
	uint8_t frame_count; /* Input records in the batch */
	/* The encoded records. Never named in code: flight_net_sample_local_input
	 * writes them through a byte offset from the packet start. */
	uint8_t stream_bytes[507];
};

#pragma pack(pop)
typedef char xvt_size_flight_net_input_batch_packet
	[(sizeof(struct flight_net_input_batch_packet) == 512) ? 1 : -1];

#pragma pack(push, 1)

struct flight_net_world_state_chunk_packet {
	int packet_type;    /* Always NET_PACKET_RESYNC_CHUNK */
	int checksum_epoch; /* Receivers drop other epochs */
	int chunk_index;    /* Slot 0-15 in the batch; the ack echoes it */
	/* Records, each a flight_net_world_state_chunk_record_header and its bytes,
	 * ended by a world_offset of -1 */
	uint8_t payload[500];
};

#pragma pack(pop)
typedef char xvt_size_flight_net_world_state_chunk_packet
	[(sizeof(struct flight_net_world_state_chunk_packet) == 512) ? 1 : -1];

#pragma pack(push, 1)

struct flight_net_world_state_chunk_record_header {
	int world_offset; /* Byte offset in the world state; -1 ends */
	int data_size;	  /* Bytes that follow this header */
};

#pragma pack(pop)
typedef char xvt_size_flight_net_world_state_chunk_record_header
	[(sizeof(struct flight_net_world_state_chunk_record_header) == 8) ? 1
									  : -1];

extern int g_player_abort_flags[8];
extern int g_input_timestamp;
extern int g_flight_net_host_abort_received;
extern int g_flight_net_world_checksum_peer_status[8];
extern int g_player_connected[8];
extern struct flight_input_frame_record g_current_input_frame;
extern struct flight_net_scratch_packet g_flight_net_scratch_packet;
extern int g_flight_net_last_input_timestamp_by_player[8];
extern int g_flight_net_peer_silence_ticks[8];
extern int g_last_sent_input_timestamp;
extern int g_last_keyframe_time;
extern int g_flight_net_last_input_batch_send_time;
extern struct flight_net_input_batch_packet g_flight_net_input_batch_packet;
extern int g_flight_net_input_batch_len;
extern int g_flight_net_input_batch_interval_ticks;
extern int g_flight_net_world_state_ack_received_flag;
extern int g_flight_net_world_state_chunk_acked[16];
extern int g_flight_net_local_resync_checksums[126];
extern int g_flight_net_remote_resync_checksums[126];
extern struct flight_net_world_state_chunk_packet
	g_flight_net_world_state_chunk_packets[16];
extern int g_flight_net_remote_resync_checksums_received_flag;
extern int g_flight_net_resync_player_dplay_id;
extern int g_flight_net_recovery_ui_active;
extern int g_flight_net_pending_ack_count;

char *flight_net_resolve_resync_player_name(void);
int flight_net_sync_player_options_and_taunts(void);
int flight_net_wait_for_mission_start(void);
int flight_net_send_clock_probe_to_host(void);
int flight_net_broadcast_still_loading_pulse(void);
int flight_net_broadcast_host_session_abort(void);
int flight_net_send_still_loading_pulse(void);
int flight_net_broadcast_player_disconnected(int player_slot);
int flight_net_broadcast_player_abort(int player_slot);
int flight_net_find_pilot_network_player_index(int player_idx);
void flight_net_mark_pilot_network_player_left(int player_slot);
void flight_net_process_incoming_packets(void);
int32_t flight_net_sample_local_input(void);
void flight_net_reset_world_message_schedule(void);
int flight_net_take_world_message_turn(int input_timestamp);
void flight_net_broadcast_world_message(int input_timestamp);
int flight_net_send_world_checksum_to_host(const int *world_checksum,
					   const int *region_lengths,
					   int checksum_dword_count);
int flight_net_broadcast_world_checksum(const int *world_checksum,
					const int *region_lengths,
					int checksum_dword_count);
void flight_net_send_world_state_resync_apply_request(int direct_play_id,
						      int world_state_size);
int flight_net_send_world_state_resync_to_player(int direct_play_id,
						 uint8_t *world_state,
						 int world_state_size);
int flight_net_wait_for_world_state_chunk_acks(int direct_play_id,
					       int chunk_count);
void flight_net_handle_world_state_resync_packet(const int *packet);

#ifdef __cplusplus
}
#endif

#endif
