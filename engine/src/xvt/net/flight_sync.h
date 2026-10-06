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
extern int g_remote_player_render_smoothing_enabled;

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
void flight_sync_snapshot_world_state_for_replay(void);
void flight_sync_clear_buffered_world_messages(void);

#ifdef __cplusplus
}
#endif

#endif
