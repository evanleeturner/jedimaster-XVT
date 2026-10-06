#ifndef XVT_NET_FLIGHT_SYNC_H
#define XVT_NET_FLIGHT_SYNC_H

#include <stdint.h>

#include "xvt/flight/flight_input.h"
#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

extern int g_input_frame_count[8];
extern struct input_frame g_input_history[8][450];
extern int g_flight_net_dirty_all_object_transforms_after_restore;
extern int g_remote_player_render_smoothing_enabled;

void flight_sync_queue_predicted_remote_input_frames(int predicted_frame_delta);
void flight_sync_discard_all_predicted_input_frames(void);
void flight_sync_discard_predicted_input_frames(int player_idx);
void flight_sync_remove_input_history_frame(int player_idx,
					    const struct input_frame *frame);
struct input_frame *
flight_sync_insert_input_frame(int player_idx, int timestamp,
			       const struct flight_input_frame_record *input);
struct input_frame *flight_sync_find_last_unrelayed_input_frame(int player_idx);
void flight_sync_reset_remote_player_render_smoothing(void);
void flight_sync_capture_samples_and_restore_poses(void);
void flight_sync_apply_remote_player_render_smoothing(void);
void flight_sync_handle_world_checksum_packet(int sender_dpid,
					      const int *packet);
void flight_sync_handle_server_checksum_packet(uint8_t *packet);
void flight_sync_copy_world_state_resync_chunk(const void *src, int offset,
					       unsigned int size);
void flight_sync_apply_resync_and_replay_world_messages(
	unsigned int world_state_bytes, int server_tick_time);
void flight_sync_snapshot_world_state_for_replay(void);
void flight_sync_clear_buffered_world_messages(void);
int flight_sync_unused_four_arg_forwarder(int arg1, int arg2, int arg3,
					  int arg4);

#ifdef __cplusplus
}
#endif

#endif
