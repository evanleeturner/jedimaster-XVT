#include "xvt/net/flight_sync.h"

#include <string.h>

#include "xvt/audio/sound.h"
#include "xvt/flight/fediskio.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/flight_view.h"
#include "xvt/flight/fview.h"
#include "xvt/flight/object/collide.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/math/math.h"
#include "xvt/net/flight_net.h"
#include "xvt/net/net_reliable.h"
#include "xvt/net/net_session.h"
#include "xvt/util/memory.h"
#include "xvt_runtime/input/flight_controls.h"
#include "xvt_runtime/log/log.h"
#include "xvt_runtime/runtime/flight_messages.h"
#include "xvt_runtime/runtime/flight_network.h"
#include "xvt_runtime/runtime/flight_sim.h"
#include "xvt_runtime/runtime/resync_task.h"
#include "xvt_runtime/timing/flight_timing.h"

enum { INPUT_FRAME_PREDICTED = 2 };

/* Frames held in each player's row of g_input_history, 0 to 450. Five writers:
 * flight_sync_remove_input_history_frame, xvt_flight_history_insert,
 * xvt_flight_history_restore_checkpoint, xvt_flight_history_recover and
 * xvt_flight_loading_globals, which sets every count to 0 at flight start. */
// GLOBAL: XVT 0x9A8DB0
int g_input_frame_count[8] = {0};
/* Each player's input frames in time stamp order, up to 450 per player, with
 * where each came from (input_source) and whether it still awaits relay to the
 * other players. Many writers; chiefly flight_sync_insert_input_frame and
 * flight_sync_remove_input_history_frame. */
// GLOBAL: XVT 0x9ED670
struct input_frame g_input_history[8][450] = {{{0}}};
/* 1 when remote players' craft are drawn smoothed. Starts at 1; flight start
 * copies g_internet_play_enabled into it (xvt_flight_loading_globals). */
// GLOBAL: XVT 0x523430
int g_remote_player_render_smoothing_enabled = 1;
/* Per player, the pose and motion of the remote craft as last drawn, taken by
 * flight_sync_capture_samples_and_restore_poses after each frame is drawn;
 * flight_sync_apply_remote_player_render_smoothing predicts the next drawn pose
 * from it. flight_sync_reset_remote_player_render_smoothing marks all invalid. */
// GLOBAL: XVT 0x550888
struct remote_player_render_sample g_remote_player_render_samples[8];
/* Per player, the simulated pose flight_sync_apply_remote_player_render_smoothing
 * saves before it moves the craft to its drawn pose;
 * flight_sync_capture_samples_and_restore_poses puts it back after drawing. */
// GLOBAL: XVT 0x550A08
struct remote_player_saved_sim_pose g_remote_player_saved_sim_poses[8];

/* Removes the predicted frames (input_source 2, not awaiting relay) from one
 * player's input history. Does nothing for an inactive player or the local
 * one. */
// FUNCTION: XVT 0x418650
void flight_sync_discard_predicted_input_frames(int player_idx)
{
	if (g_players[player_idx].participation_state == 0 ||
	    player_idx == g_local_player) {
		return;
	}

	int frame_index = 0;
	struct input_frame *frame = g_input_history[player_idx];
	while (frame_index < g_input_frame_count[player_idx]) {
		if (frame->awaiting_relay == 0 &&
		    frame->input_source == INPUT_FRAME_PREDICTED) {
			flight_sync_remove_input_history_frame(player_idx,
							       frame);
		} else {
			++frame;
			++frame_index;
		}
	}
	XVT_LOG_DEBUG("network.predictions_discarded slot=%d left=%d",
		      player_idx, g_input_frame_count[player_idx]);
}

/* Removes the frame that frame points at from a player's input history,
 * moving later frames down one place and lowering g_input_frame_count. Does
 * nothing when the history is empty or the pointer lies before the player's
 * row; does not check that it lies among the frames in use. */
// FUNCTION: XVT 0x4186E0
void flight_sync_remove_input_history_frame(int player_idx,
					    const struct input_frame *frame)
{
	int frame_count = g_input_frame_count[player_idx];
	if (frame_count != 0) {
		struct input_frame *current = g_input_history[player_idx];
		if (current <= frame) {
			if (frame >= current + frame_count) {
				XVT_LOG_WARN(
					"network.history_remove_fault slot=%d count=%d reason=\"%s\"",
					player_idx, frame_count, "past_end");
			}
			--frame_count;
			g_input_frame_count[player_idx] = frame_count;
			XVT_LOG_DEBUG(
				"network.input_removed slot=%d tick=%d source=\"%s\" left=%d",
				player_idx, frame->timestamp,
				frame->input_source == 0 ? "host"
				: frame->input_source == INPUT_FRAME_PREDICTED
					? "predicted"
					: "real",
				frame_count);
			int copy_index = 0;
			while (copy_index < g_input_frame_count[player_idx]) {
				if (current >= frame) {
					*current = current[1];
				}
				++copy_index;
				++current;
			}
		} else {
			XVT_LOG_WARN(
				"network.history_remove_fault slot=%d count=%d reason=\"%s\"",
				player_idx, frame_count, "before_row");
		}
	} else {
		XVT_LOG_WARN(
			"network.history_remove_fault slot=%d count=%d reason=\"%s\"",
			player_idx, frame_count, "empty");
	}
}

/* Puts an input frame into a player's history, kept in time stamp order, and
 * returns it, or NULL when refused. Hands the work to xvt_flight_history_insert
 * and, when the history is full under the network timing
 * (xvt_flight_timing_is_network125), calls xvt_flight_network_request_recovery.
 * A new time stamp is inserted, refused when all 450 frames are in use; a frame
 * with the same time stamp is overwritten unless it came from the server
 * (input_source 0) or awaits relay, which refuses it. The frame gets
 * input_source 1 and awaiting_relay 0; callers may change them afterwards. */
// FUNCTION: XVT 0x418760
struct input_frame *
flight_sync_insert_input_frame(int player_idx, int timestamp,
			       const struct flight_input_frame_record *input)
{
	struct input_frame *inserted;
	xvt_input_insert_status status = xvt_flight_history_insert(
		(unsigned)player_idx, timestamp, input, &inserted);
	XVT_LOG_DEBUG("network.input_filed slot=%d tick=%d result=\"%s\"",
		      player_idx, timestamp,
		      status == XVT_INPUT_INSERTED    ? "inserted"
		      : status == XVT_INPUT_DUPLICATE ? "duplicate"
		      : status == XVT_INPUT_FULL      ? "full"
		      : status == XVT_INPUT_INVALID   ? "invalid"
						      : "conflict");
	if (status == XVT_INPUT_FULL && xvt_flight_timing_is_network125()) {
		xvt_flight_network_request_recovery();
	} else if (status == XVT_INPUT_FULL || status == XVT_INPUT_INVALID) {
		XVT_LOG_WARN(
			"network.history_refused slot=%d tick=%d result=\"%s\"",
			player_idx, timestamp,
			status == XVT_INPUT_FULL ? "full" : "invalid");
	}
	return inserted;
}

/* Returns the last frame in a player's input history that still awaits relay
 * (awaiting_relay nonzero), or NULL when none does. */
// FUNCTION: XVT 0x418890
struct input_frame *flight_sync_find_last_unrelayed_input_frame(int player_idx)
{
	struct input_frame *frame = g_input_history[player_idx];
	int frame_count = g_input_frame_count[player_idx];
	struct input_frame *result = 0;
	while (frame_count > 0) {
		if (frame->awaiting_relay != 0) {
			result = frame;
		}
		++frame;
		--frame_count;
	}
	XVT_LOG_DEBUG("network.unrelayed_input slot=%d tick=%d frames=%d",
		      player_idx, result ? result->timestamp : -1,
		      g_input_frame_count[player_idx]);
	return result;
}

/* Marks all eight render samples and saved simulated poses invalid. */
// FUNCTION: XVT 0x418950
void flight_sync_reset_remote_player_render_smoothing(void)
{
	for (int player_index = 0; player_index < 8; ++player_index) {
		g_remote_player_render_samples[player_index].valid = 0;
		g_remote_player_saved_sim_poses[player_index].valid = 0;
	}
	XVT_LOG_DEBUG("network.smoothing_reset");
}

/* Runs after a frame is drawn. For each active remote player whose craft
 * exists it records in g_remote_player_render_samples the position and angles
 * the craft was drawn at, the change in each angle since the last sample
 * (none when there was no valid sample), and its move vector, speed and
 * simulation time stamp, recomputing the move vector first when it is
 * stale. It then puts back the simulated pose that
 * flight_sync_apply_remote_player_render_smoothing saved. Every other player's
 * sample becomes invalid. Does nothing when
 * g_remote_player_render_smoothing_enabled is 0. */
// FUNCTION: XVT 0x418970
void flight_sync_capture_samples_and_restore_poses(void)
{
	if (g_remote_player_render_smoothing_enabled == 0) {
		return;
	}

	int player_index = 0;
	do {
		struct player_data *player = &g_players[player_index];
		int sample_was_valid =
			g_remote_player_render_samples[player_index].valid;
		g_remote_player_render_samples[player_index].valid = 0;
		if (g_players[player_index].participation_state != 0 &&
		    g_local_player != player_index &&
		    player->object_index != -1) {
			struct object_record *object =
				&g_object_table[player->object_index];
			if (object->object_type != 0 && object->mobj != NULL) {
				if (sample_was_valid == 0) {
					g_remote_player_render_samples
						[player_index]
							.roll = object->roll;
					g_remote_player_render_samples
						[player_index]
							.pitch = object->pitch;
					g_remote_player_render_samples
						[player_index]
							.yaw = object->yaw;
				}

				g_remote_player_render_samples[player_index]
					.valid = 1;
				g_remote_player_render_samples[player_index]
					.object_signature =
					object->object_signature;
				g_remote_player_render_samples[player_index]
					.world_x = object->world_x;
				g_remote_player_render_samples[player_index]
					.world_y = object->world_y;
				g_remote_player_render_samples[player_index]
					.world_z = object->world_z;
				g_remote_player_render_samples[player_index]
					.roll_delta =
					object->roll -
					(uint16_t)g_remote_player_render_samples
						[player_index]
							.roll;
				g_remote_player_render_samples[player_index]
					.pitch_delta =
					object->pitch -
					(uint16_t)g_remote_player_render_samples
						[player_index]
							.pitch;
				g_remote_player_render_samples[player_index]
					.yaw_delta =
					object->yaw -
					(uint16_t)g_remote_player_render_samples
						[player_index]
							.yaw;
				g_remote_player_render_samples[player_index]
					.roll = object->roll;
				g_remote_player_render_samples[player_index]
					.pitch = object->pitch;
				g_remote_player_render_samples[player_index]
					.yaw = object->yaw;

				if (object->mobj->move_vector_dirty != 0) {
					fview_calcrotatemove(object->pitch,
							     object->yaw,
							     object);
				}
				g_remote_player_render_samples[player_index]
					.move_x = object->mobj->move_x;
				g_remote_player_render_samples[player_index]
					.move_y = object->mobj->move_y;
				g_remote_player_render_samples[player_index]
					.move_z = object->mobj->move_z;
				g_remote_player_render_samples[player_index]
					.speed_magnitude = object->mobj->speed;
				g_remote_player_render_samples[player_index]
					.sim_state_timestamp =
					object->mobj->sim_state_timestamp;

				if (g_remote_player_saved_sim_poses
					    [player_index]
						    .valid != 0) {
					object->roll =
						g_remote_player_saved_sim_poses
							[player_index]
								.roll;
					object->pitch =
						g_remote_player_saved_sim_poses
							[player_index]
								.pitch;
					object->yaw =
						g_remote_player_saved_sim_poses
							[player_index]
								.yaw;
					object->world_x =
						g_remote_player_saved_sim_poses
							[player_index]
								.world_x;
					object->world_y =
						g_remote_player_saved_sim_poses
							[player_index]
								.world_y;
					object->world_z =
						g_remote_player_saved_sim_poses
							[player_index]
								.world_z;
				}
				XVT_LOG_DEBUG(
					"network.smoothing_sample slot=%d x=%d y=%d z=%d roll=%u pitch=%u yaw=%u turn_roll=%d turn_pitch=%d turn_yaw=%d mx=%d my=%d mz=%d speed=%u time=%d restored=%d",
					player_index,
					g_remote_player_render_samples
						[player_index]
							.world_x,
					g_remote_player_render_samples
						[player_index]
							.world_y,
					g_remote_player_render_samples
						[player_index]
							.world_z,
					(unsigned)(uint16_t)
						g_remote_player_render_samples
							[player_index]
								.roll,
					(unsigned)(uint16_t)
						g_remote_player_render_samples
							[player_index]
								.pitch,
					(unsigned)(uint16_t)
						g_remote_player_render_samples
							[player_index]
								.yaw,
					g_remote_player_render_samples
						[player_index]
							.roll_delta,
					g_remote_player_render_samples
						[player_index]
							.pitch_delta,
					g_remote_player_render_samples
						[player_index]
							.yaw_delta,
					(int)g_remote_player_render_samples
						[player_index]
							.move_x,
					(int)g_remote_player_render_samples
						[player_index]
							.move_y,
					(int)g_remote_player_render_samples
						[player_index]
							.move_z,
					(unsigned)g_remote_player_render_samples
						[player_index]
							.speed_magnitude,
					(int)g_remote_player_render_samples
						[player_index]
							.sim_state_timestamp,
					g_remote_player_saved_sim_poses
						[player_index]
							.valid);
			}
		}
		++player_index;
	} while (player_index < 8);
}

/* Runs before a frame is drawn. Moves each active remote player's craft from
 * its simulated pose to a smoothed one, after saving the simulated pose in
 * g_remote_player_saved_sim_poses for flight_sync_capture_samples_and_restore_poses to
 * put back. Leaves a craft as simulated when its sample is invalid or belongs
 * to another object, or when its simulation time stamp is older than the
 * sample's. The position is projected from the sampled one along the sampled
 * move vector, by a distance that grows with the sampled speed and the
 * simulation time since the sample, then moved toward the simulated position
 * by half the gap, or a smaller share when the gap is within 32 times that
 * distance. An angle that moved against the sampled turn is held at the
 * sampled angle. The step that compares the change with max_angle_change sets
 * each angle to a value equal to itself modulo 65,536, so it changes
 * nothing. Does nothing when g_remote_player_render_smoothing_enabled is 0. */
// FUNCTION: XVT 0x418B70
void flight_sync_apply_remote_player_render_smoothing(void)
{
	if (g_remote_player_render_smoothing_enabled == 0) {
		return;
	}

	int position_blend;
	int candidate_angle;
	int candidate_difference;
	for (int player_index = 0; player_index < 8; ++player_index) {
		g_remote_player_saved_sim_poses[player_index].valid = 0;
		if (g_players[player_index].participation_state == 0 ||
		    g_players[player_index].object_index == -1) {
			continue;
		}

		struct object_record *object =
			&g_object_table[g_players[player_index].object_index];
		if (object->object_type == 0 || object->mobj == NULL ||
		    g_remote_player_render_samples[player_index].valid == 0 ||
		    player_index == g_local_player ||
		    g_remote_player_render_samples[player_index]
				    .object_signature !=
			    g_players[player_index].bound_object_signature) {
			if (player_index != g_local_player) {
				XVT_LOG_DEBUG(
					"network.smoothing_skipped slot=%d reason=\"%s\"",
					player_index,
					object->object_type == 0 ||
							object->mobj == NULL
						? "no_craft"
					: g_remote_player_render_samples
								[player_index]
									.valid ==
							0
						? "no_sample"
						: "other_craft");
			}
			continue;
		}

		g_remote_player_saved_sim_poses[player_index].roll =
			object->roll;
		g_remote_player_saved_sim_poses[player_index].pitch =
			object->pitch;
		g_remote_player_saved_sim_poses[player_index].yaw = object->yaw;
		g_remote_player_saved_sim_poses[player_index].world_x =
			object->world_x;
		g_remote_player_saved_sim_poses[player_index].world_y =
			object->world_y;
		g_remote_player_saved_sim_poses[player_index].world_z =
			object->world_z;
		g_remote_player_saved_sim_poses[player_index].valid = 1;

		int predicted_world_x =
			g_remote_player_render_samples[player_index].world_x;
		int predicted_world_y =
			g_remote_player_render_samples[player_index].world_y;
		int predicted_world_z =
			g_remote_player_render_samples[player_index].world_z;

		int elapsed_time = object->mobj->sim_state_timestamp -
				   g_remote_player_render_samples[player_index]
					   .sim_state_timestamp;
		if (elapsed_time < 0) {
			XVT_LOG_DEBUG(
				"network.smoothing_held slot=%d elapsed=%d",
				player_index, elapsed_time);
			continue;
		}

		int prediction_distance = 0;
		if (elapsed_time > 0 &&
		    g_remote_player_render_samples[player_index]
				    .speed_magnitude != 0) {
			prediction_distance =
				elapsed_time *
				((4660 * g_remote_player_render_samples
						  [player_index]
							  .speed_magnitude +
				  128) >>
				 8) /
				SIMULATION_TICKS_PER_SECOND;
			predicted_world_x += math_mul_q15(
				g_remote_player_render_samples[player_index]
					.move_x,
				prediction_distance);
			predicted_world_y += math_mul_q15(
				g_remote_player_render_samples[player_index]
					.move_y,
				prediction_distance);
			predicted_world_z += math_mul_q15(
				g_remote_player_render_samples[player_index]
					.move_z,
				prediction_distance);
		}

		int position_delta_x = object->world_x - predicted_world_x;
		int position_delta_y = object->world_y - predicted_world_y;
		int position_delta_z = object->world_z - predicted_world_z;
		int rough_distance = collide_roughdistance3d(
			position_delta_x, position_delta_y, position_delta_z);
		prediction_distance *= 32;
		if (prediction_distance >= rough_distance &&
		    rough_distance != 0) {
			position_blend =
				(rough_distance << 14) / prediction_distance;
		} else {
			position_blend = 0x4000;
		}
		int blended_x = math_mul_q15(position_blend, position_delta_x);
		int blended_y = math_mul_q15(position_blend, position_delta_y);
		int blended_z = math_mul_q15(position_blend, position_delta_z);
		predicted_world_x += blended_x;
		predicted_world_y += blended_y;
		predicted_world_z += blended_z;
		object->world_x = predicted_world_x;
		object->world_y = predicted_world_y;
		object->world_z = predicted_world_z;

		int angle_difference =
			(int16_t)(object->roll -
				  g_remote_player_render_samples[player_index]
					  .roll);
		int signed_angle_difference = angle_difference;
		if (g_remote_player_render_samples[player_index].roll_delta >
		    0) {
			if (angle_difference < 0) {
				object->roll = g_remote_player_render_samples
						       [player_index]
							       .roll;
				angle_difference = 0;
				signed_angle_difference = 0;
			}
		} else if (g_remote_player_render_samples[player_index]
				   .roll_delta < 0) {
			if (signed_angle_difference > 0) {
				object->roll = g_remote_player_render_samples
						       [player_index]
							       .roll;
				angle_difference = 0;
				signed_angle_difference = 0;
			}
		}
		if (angle_difference < 0) {
			angle_difference = -angle_difference;
		}
		int max_angle_change =
			6144 * elapsed_time / SIMULATION_TICKS_PER_SECOND;
		if (angle_difference > max_angle_change) {
			candidate_angle =
				g_remote_player_render_samples[player_index]
					.roll +
				signed_angle_difference;
			candidate_difference = object->roll - candidate_angle;
			if (candidate_difference < 0) {
				candidate_difference = -candidate_difference;
			}
			if (8 * max_angle_change > candidate_difference) {
				object->roll = candidate_angle;
			}
		}

		angle_difference =
			(int16_t)(object->pitch -
				  g_remote_player_render_samples[player_index]
					  .pitch);
		signed_angle_difference = angle_difference;
		if (g_remote_player_render_samples[player_index].pitch_delta >
		    0) {
			if (angle_difference < 0) {
				object->pitch = g_remote_player_render_samples
							[player_index]
								.pitch;
				angle_difference = 0;
				signed_angle_difference = 0;
			}
		} else if (g_remote_player_render_samples[player_index]
				   .pitch_delta < 0) {
			if (signed_angle_difference > 0) {
				object->pitch = g_remote_player_render_samples
							[player_index]
								.pitch;
				angle_difference = 0;
				signed_angle_difference = 0;
			}
		}
		if (angle_difference < 0) {
			angle_difference = -angle_difference;
		}
		if (angle_difference > max_angle_change) {
			candidate_angle =
				g_remote_player_render_samples[player_index]
					.pitch +
				signed_angle_difference;
			candidate_difference = object->pitch - candidate_angle;
			if (candidate_difference < 0) {
				candidate_difference = -candidate_difference;
			}
			if (8 * max_angle_change > candidate_difference) {
				object->pitch = candidate_angle;
			}
		}

		angle_difference =
			(int16_t)(object->yaw -
				  g_remote_player_render_samples[player_index]
					  .yaw);
		signed_angle_difference = angle_difference;
		if (g_remote_player_render_samples[player_index].yaw_delta >
		    0) {
			if (angle_difference < 0) {
				object->yaw = g_remote_player_render_samples
						      [player_index]
							      .yaw;
				angle_difference = 0;
				signed_angle_difference = 0;
			}
		} else if (g_remote_player_render_samples[player_index]
				   .yaw_delta < 0) {
			if (signed_angle_difference > 0) {
				object->yaw = g_remote_player_render_samples
						      [player_index]
							      .yaw;
				angle_difference = 0;
				signed_angle_difference = 0;
			}
		}
		if (angle_difference < 0) {
			angle_difference = -angle_difference;
		}
		if (angle_difference > max_angle_change) {
			candidate_angle =
				g_remote_player_render_samples[player_index]
					.yaw +
				signed_angle_difference;
			candidate_difference = object->yaw - candidate_angle;
			if (candidate_difference < 0) {
				candidate_difference = -candidate_difference;
			}
			if (8 * max_angle_change > candidate_difference) {
				object->yaw = candidate_angle;
			}
		}
		XVT_LOG_DEBUG(
			"network.smoothing_applied slot=%d elapsed=%d reach=%d gap=%d share=%d x=%d y=%d z=%d sx=%d sy=%d sz=%d roll=%u pitch=%u yaw=%u max_turn=%d",
			player_index, elapsed_time, prediction_distance,
			rough_distance, position_blend, object->world_x,
			object->world_y, object->world_z,
			g_remote_player_saved_sim_poses[player_index].world_x,
			g_remote_player_saved_sim_poses[player_index].world_y,
			g_remote_player_saved_sim_poses[player_index].world_z,
			(unsigned)object->roll, (unsigned)object->pitch,
			(unsigned)object->yaw, max_angle_change);
	}
}

enum {
	PACKET_EPOCH_INDEX = 1,
	PACKET_CHECKSUM_INDEX = 2,
	CHECKSUM_REGION_COUNT = 16,
	PACKET_REGION_LENGTH_INDEX =
		PACKET_CHECKSUM_INDEX + CHECKSUM_REGION_COUNT,
	PEER_STATUS_MISMATCHED = 2,
	PEER_STATUS_MATCHED = 1,
	ALL_PEER_STATUS_BITS = 3
};

/* Part of flight_sync_handle_world_checksum_packet: compares the 16 region
 * checksums in packet with the local ones and sets *checksum_mismatch to 1
 * when any differs. Adds up both sides' region lengths in
 * *local_world_state_size and *remote_world_state_size for the mismatch
 * warning. */
static void flight_sync_compare_world_checksums(const int *packet,
						int *local_world_state_size,
						int *remote_world_state_size,
						int *checksum_mismatch)
{
	int player_index;

	const int *remote_checksums = &packet[PACKET_CHECKSUM_INDEX];
	const int *remote_region_lengths = &packet[PACKET_REGION_LENGTH_INDEX];

	*local_world_state_size = 0;
	*remote_world_state_size = 0;
	/* player_index is reused here as a checksum region index. */
	for (player_index = 0; player_index < CHECKSUM_REGION_COUNT;
	     ++player_index) {
		*remote_world_state_size += remote_region_lengths[player_index];
		*local_world_state_size +=
			(int)g_world_checksum_region_lengths[player_index];
		if (g_world_checksum[player_index] !=
		    (unsigned int)remote_checksums[player_index]) {
			*checksum_mismatch = 1;
		}
	}
	(void)*local_world_state_size;
	(void)*remote_world_state_size;
}

/* Part of flight_sync_handle_world_checksum_packet on the host: records
 * sender_player_index in g_flight_net_world_checksum_peer_status as matched
 * (1), or not (2) when checksum_mismatch is 1, turns buffering off once
 * every active player has matched, and logs it with packet's epoch. */
static void flight_sync_record_checksum_status(int sender_player_index,
					       int checksum_mismatch,
					       const int *packet)
{
	int player_index;

	g_flight_net_world_checksum_peer_status[sender_player_index] =
		PEER_STATUS_MISMATCHED;
	if (checksum_mismatch != 1) {
		g_flight_net_world_checksum_peer_status[sender_player_index] =
			PEER_STATUS_MATCHED;
	}

	int all_peer_status = ALL_PEER_STATUS_BITS;
	for (player_index = 0;
	     player_index < (int)(sizeof(g_players) / sizeof(g_players[0]));
	     ++player_index) {
		if (g_players[player_index].participation_state != 0) {
			all_peer_status &=
				g_flight_net_world_checksum_peer_status
					[player_index];
		}
	}
	if ((all_peer_status & PEER_STATUS_MATCHED) != 0) {
		g_flight_net_buffer_world_messages_until_checksum = 0;
	}
	XVT_LOG_DEBUG(
		"network.checksum_recorded slot=%d epoch=%u status=%d all=%d buffering=%d",
		sender_player_index, (unsigned)packet[PACKET_EPOCH_INDEX],
		g_flight_net_world_checksum_peer_status[sender_player_index],
		all_peer_status,
		g_flight_net_buffer_world_messages_until_checksum);
}

/* Handles a player's world checksum for the epoch in
 * g_flight_net_world_checksum_epoch (word 1); other epochs, and players that
 * aborted or are inactive, are ignored. When the 16 region checksums from
 * another player differ from the local ones, it starts sending that player the
 * snapshot in g_world_state_dup_buffer with xvt_resync_begin_send and returns.
 * On the host it then records the player in
 * g_flight_net_world_checksum_peer_status as matched (1) or not (2), and turns
 * buffering off once every active player has matched. Word 34 is a request
 * code: code 1 is taken in any epoch, and on the host, from another player,
 * starts sending the live world state instead. A sender with no player slot is
 * rejected, as is a request code above 1. */
// FUNCTION: XVT 0x4193C0
void flight_sync_handle_world_checksum_packet(int sender_dpid,
					      const int *packet)
{
	if ((unsigned int)packet[PACKET_EPOCH_INDEX] !=
		    g_flight_net_world_checksum_epoch &&
	    packet[34] != XVT_CHECKSUM_REQUEST_STATE) {
		XVT_LOG_DEBUG("network.checksum_stale epoch=%u expected=%u",
			      (unsigned)packet[PACKET_EPOCH_INDEX],
			      g_flight_net_world_checksum_epoch);
		return;
	}

	int sender_player_index =
		net_session_find_player_slot_by_dpid(sender_dpid);
	int checksum_mismatch = 0;
	if ((unsigned)sender_player_index >= 8) {
		XVT_LOG_WARN(
			"network.checksum_rejected slot=%d request=%d reason=\"%s\"",
			sender_player_index, packet[34], "sender");
		return;
	}
	if ((unsigned)packet[34] > 1) {
		XVT_LOG_WARN(
			"network.checksum_rejected slot=%d request=%d reason=\"%s\"",
			sender_player_index, packet[34], "request");
		return;
	}
	if (packet[34] == 1 && net_session_is_local_host() &&
	    sender_player_index != g_local_player) {
		XVT_LOG_DEBUG("network.state_requested slot=%d epoch=%u",
			      sender_player_index,
			      (unsigned)packet[PACKET_EPOCH_INDEX]);
		xvt_resync_begin_send(sender_dpid, g_world_state_buffer,
				      g_world_state_size);
		return;
	}
	if (g_player_abort_flags[sender_player_index] != 0 ||
	    g_players[sender_player_index].participation_state == 0) {
		XVT_LOG_DEBUG("network.checksum_ignored slot=%d reason=\"%s\"",
			      sender_player_index,
			      g_player_abort_flags[sender_player_index] != 0
				      ? "left"
				      : "inactive");
		return;
	}

	if (g_local_player != sender_player_index) {
		int local_world_state_size;
		int remote_world_state_size;
		flight_sync_compare_world_checksums(
			packet, &local_world_state_size,
			&remote_world_state_size, &checksum_mismatch);

		if (checksum_mismatch != 0) {
			XVT_LOG_WARN(
				"network.checksum_mismatch slot=%d epoch=%u bytes=%d peer_bytes=%d",
				sender_player_index,
				(unsigned)packet[PACKET_EPOCH_INDEX],
				local_world_state_size,
				remote_world_state_size);
			xvt_resync_begin_send(sender_dpid,
					      g_world_state_dup_buffer,
					      g_world_state_dup_size);
			return;
		}
	}

	if (net_session_is_local_host() == 0) {
		XVT_LOG_DEBUG(
			"network.checksum_matched slot=%d epoch=%u own=%d",
			sender_player_index,
			(unsigned)packet[PACKET_EPOCH_INDEX],
			sender_player_index == g_local_player);
		return;
	}

	flight_sync_record_checksum_status(sender_player_index,
					   checksum_mismatch, packet);
}

/* On a client, compares the 16 world checksum words the server sent for the
 * current epoch with the local ones; when all match, turns buffering off and
 * empties the world-message buffer. Ignored on the host and for any other
 * epoch. */
// FUNCTION: XVT 0x419510
void flight_sync_handle_server_checksum_packet(uint8_t *packet)
{
	if (net_session_is_local_host() != 0 ||
	    ((uint32_t *)packet)[1] != g_flight_net_world_checksum_epoch) {
		XVT_LOG_DEBUG(
			"network.server_checksum_ignored epoch=%u expected=%u host=%d",
			(unsigned)((const uint32_t *)packet)[1],
			g_flight_net_world_checksum_epoch,
			g_net_session.local_is_host != 0);
		return;
	}

	uint32_t *packet_checksum = (uint32_t *)packet + 2;
	int checksum_mismatch = 0;
	unsigned int *local_checksum = g_world_checksum;
	do {
		if (*local_checksum != *packet_checksum) {
			checksum_mismatch = 1;
		}
		++local_checksum;
		++packet_checksum;
	} while (local_checksum < g_world_checksum + 16);

	if (checksum_mismatch == 0) {
		g_flight_net_buffer_world_messages_until_checksum = 0;
		flight_sync_clear_buffered_world_messages();
	} else {
		XVT_LOG_WARN("network.server_checksum_mismatch epoch=%u",
			     g_flight_net_world_checksum_epoch);
	}
	XVT_LOG_DEBUG("network.server_checksum epoch=%u match=%d buffering=%d",
		      g_flight_net_world_checksum_epoch, checksum_mismatch == 0,
		      g_flight_net_buffer_world_messages_until_checksum);
}

/* Copies the saved world state (g_world_state_size bytes of
 * g_world_state_buffer) into g_world_state_dup_buffer and sets
 * g_world_state_dup_size: the copy a resync sends to a player whose checksum
 * differs. */
// FUNCTION: XVT 0x419620
void flight_sync_snapshot_world_state_for_replay(void)
{
	unsigned int snapshot_bytes = g_world_state_size;
	memcpy(g_world_state_dup_buffer, g_world_state_buffer, snapshot_bytes);
	g_world_state_dup_size = (int)snapshot_bytes;
}

/* Empties the world-message buffer by clearing the replay queue
 * (xvt_flight_messages_clear). */
// FUNCTION: XVT 0x4197B0
void flight_sync_clear_buffered_world_messages(void)
{
	xvt_flight_messages_clear(XVT_QUEUE_REPLAY);
	XVT_LOG_DEBUG("network.replay_cleared");
}
