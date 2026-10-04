#include "xvt/net/flight_sync.h"
#ifdef XVT_MODERN
#include "xvt_runtime/input/flight_controls.h"
#include "xvt_runtime/runtime/flight_messages.h"
#include "xvt_runtime/runtime/flight_network.h"
#include "xvt_runtime/runtime/flight_sim.h"
#include "xvt_runtime/runtime/resync_task.h"
#include "xvt_runtime/timing/flight_timing.h"
#endif

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

enum { INPUT_FRAME_PREDICTED = 2 };

/* Frames held in each player's row of g_input_history, 0 to 450. Six writers;
 * chiefly flight_sync_insert_input_frame (xvt_flight_history_insert in the modern
 * build) and flight_sync_remove_input_history_frame. Flight start sets every
 * count to 0: flight_main_loop in the original build, xvt_flight_loading_globals
 * in the modern one. */
// GLOBAL: XVT 0x9A8DB0
int g_input_frame_count[8] = {0};
/* Each player's input frames in time stamp order, up to 450 per player, with
 * where each came from (input_source) and whether it still awaits relay to the
 * other players. Many writers; chiefly flight_sync_insert_input_frame and
 * flight_sync_remove_input_history_frame. */
// GLOBAL: XVT 0x9ED670
struct input_frame g_input_history[8][450] = {{{0}}};
/* Set to 1 by flight_sync_apply_resync_and_replay_world_messages as it loads a
 * resent world state; the next flight_sync_apply_world_message_packet, after
 * restoring that state, marks every live object's move vector and
 * orientation matrix for recomputing and sets it back to 0. Only the
 * original build sets or reads it. */
// GLOBAL: XVT 0x51BF40
int g_flight_net_dirty_all_object_transforms_after_restore = 0;
#ifndef XVT_MODERN
/* Bytes allocated for g_world_message_buffer. Only
 * flight_sync_buffer_world_message_packet writes it: it grows by 100 times the
 * size of a message that does not fit, and never shrinks. */
// GLOBAL: XVT 0x51BF48
static int g_world_message_buffer_capacity;
/* Bytes still free at the end of g_world_message_buffer. Lowered by
 * flight_sync_buffer_world_message_packet; set back to the capacity by
 * flight_sync_clear_buffered_world_messages and
 * flight_sync_replay_buffered_world_messages. */
// GLOBAL: XVT 0x51BF4C
static int g_world_message_buffer_bytes_free;
/* World messages held in g_world_message_buffer. Raised by
 * flight_sync_buffer_world_message_packet; flight_sync_replay_buffered_world_messages
 * counts it down to 0, and flight_sync_clear_buffered_world_messages sets 0. */
// GLOBAL: XVT 0x51BF50
static int g_world_message_buffered_count;
/* Memory handle of g_world_message_buffer; 0 until the first message is
 * buffered. flight_sync_buffer_world_message_packet replaces it with a larger
 * one, freeing the old, when a message does not fit; nothing else frees it. */
// GLOBAL: XVT 0x51BF54
static uint16_t g_world_message_buffer_handle = 0;
#endif
/* 1 when remote players' craft are drawn smoothed. Starts at 1; flight start
 * copies g_internet_play_enabled into it: flight_main_loop in the original
 * build, xvt_flight_loading_globals in the modern one. */
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
#ifndef XVT_MODERN
/* Locked memory of g_world_message_buffer_handle, where a client keeps the
 * server's world messages back to back while
 * g_flight_net_buffer_world_messages_until_checksum is 1, from a world checksum
 * until the checksum is confirmed or a resync replays them. NULL until the
 * first message is buffered. */
// GLOBAL: XVT 0x550B90
static uint8_t *g_world_message_buffer = NULL;
#endif

#ifndef XVT_MODERN
/* For every active remote player with any input frames, adds a predicted
 * frame at the last frame's time stamp plus predicted_frame_delta, with that
 * frame's two axes and no key or modifiers, marked predicted (input_source 2)
 * and not awaiting relay, where flight_sync_insert_input_frame accepts it. Does
 * nothing in internet play. Only the original build calls this. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x418500
void flight_sync_queue_predicted_remote_input_frames(int predicted_frame_delta)
{
	int player_idx;
	struct flight_input_frame_record input;

	if (g_internet_play_enabled != 0) {
		return;
	}
	memset(&input, 0, sizeof(input));
	for (player_idx = 0; player_idx < 8; ++player_idx) {
		int count;
		struct input_frame *last_frame;
		struct input_frame *predicted_frame;

		if (g_players[player_idx].participation_state == 0 ||
		    player_idx == g_local_player) {
			continue;
		}
		count = g_input_frame_count[player_idx];
		if (count == 0) {
			continue;
		}
		last_frame = &g_input_history[player_idx][count - 1];
		input.axis_x = last_frame->input.axis_x;
		input.axis_y = last_frame->input.axis_y;
		predicted_frame = flight_sync_insert_input_frame(
			player_idx,
			last_frame->timestamp + predicted_frame_delta, &input);
		if (predicted_frame != NULL) {
			predicted_frame->awaiting_relay = 0;
			predicted_frame->input_source = INPUT_FRAME_PREDICTED;
		}
	}
}
#endif

/* Removes every predicted frame (input_source 2, not awaiting relay) from the
 * input history of every active remote player; in internet play it does
 * nothing. Only the original build calls this. */
// FUNCTION: XVT 0x4185B0
void flight_sync_discard_all_predicted_input_frames(void)
{
	int player_index;

#ifndef XVT_MODERN
	if (g_internet_play_enabled != 0) {
		return;
	}
#endif

	for (player_index = 0; player_index < 8; ++player_index) {
		if (g_players[player_index].participation_state != 0 &&
		    player_index != g_local_player) {
			int frame_index;
			struct input_frame *frame;

			frame = g_input_history[player_index];
			frame_index = 0;
			while (g_input_frame_count[player_index] >
			       frame_index) {
				if (frame->awaiting_relay == 0 &&
				    frame->input_source ==
					    INPUT_FRAME_PREDICTED) {
					flight_sync_remove_input_history_frame(
						player_index, frame);
				} else {
					++frame;
					++frame_index;
				}
			}
		}
	}
}

/* Removes the predicted frames (input_source 2, not awaiting relay) from one
 * player's input history. Does nothing for an inactive player or the local
 * one, nor, in the original build, in internet play. */
// FUNCTION: XVT 0x418650
void flight_sync_discard_predicted_input_frames(int player_idx)
{
	int frame_index;
	struct input_frame *frame;

	if (
#ifndef XVT_MODERN
		g_internet_play_enabled != 0 ||
#endif
		g_players[player_idx].participation_state == 0 ||
		player_idx == g_local_player) {
		return;
	}

	frame_index = 0;
	frame = g_input_history[player_idx];
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
}

/* Removes the frame that frame points at from a player's input history,
 * moving later frames down one place and lowering g_input_frame_count. Does
 * nothing when the history is empty or the pointer lies before the player's
 * row; does not check that it lies among the frames in use. */
// FUNCTION: XVT 0x4186E0
void flight_sync_remove_input_history_frame(int player_idx,
					    struct input_frame *frame)
{
	int frame_count;
	int copy_index;
	struct input_frame *current;

	frame_count = g_input_frame_count[player_idx];
	if (frame_count != 0) {
		current = g_input_history[player_idx];
		if (current <= frame) {
			--frame_count;
			g_input_frame_count[player_idx] = frame_count;
			copy_index = 0;
			while (copy_index < g_input_frame_count[player_idx]) {
				if (current >= frame) {
					*current = current[1];
				}
				++copy_index;
				++current;
			}
		}
	}
}

/* Puts an input frame into a player's history, kept in time stamp order, and
 * returns it, or NULL when refused. The modern build hands the work to
 * xvt_flight_history_insert and, when the history is full under the network
 * timing (xvt_flight_timing_is_network125), calls
 * xvt_flight_network_request_recovery. In the original build a new time stamp is
 * inserted, refused when all 450 frames are in use; a frame with the same
 * time stamp is overwritten unless it came from the server (input_source 0)
 * or awaits relay, which refuses it. The frame gets input_source 1 and
 * awaiting_relay 0; callers may change them afterwards. */
// FUNCTION: XVT 0x418760
struct input_frame *
flight_sync_insert_input_frame(int player_idx, int timestamp,
			       const struct flight_input_frame_record *input)
{
#ifdef XVT_MODERN
	struct input_frame *inserted;
	xvt_input_insert_status status = xvt_flight_history_insert(
		(unsigned)player_idx, timestamp, input, &inserted);
	if (status == XVT_INPUT_FULL && xvt_flight_timing_is_network125()) {
		xvt_flight_network_request_recovery();
	}
	return inserted;
#else

	struct input_frame *array_end;
	int existing_timestamp;
	int frame_count;
	int frame_index;
	struct input_frame *frame;

	frame_index = 0;
	frame_count = g_input_frame_count[player_idx];
	frame = g_input_history[player_idx];
	array_end = &frame[frame_count];

	while (frame_index < frame_count && frame->timestamp < timestamp) {
		++frame_index;
		++frame;
	}
	existing_timestamp = frame->timestamp;
	if (existing_timestamp > timestamp || frame_index == frame_count) {
		if (frame_count == 450) {
			return NULL;
		}
		g_input_frame_count[player_idx] = frame_count + 1;
		if (array_end > frame) {
			frame_index = frame_count - frame_index;
			do {
				--frame_index;
				frame[frame_index + 1] = frame[frame_index];
			} while (frame_index != 0);
		}
	} else if (existing_timestamp == timestamp) {
		if (frame->input_source == 0) {
			return NULL;
		}
		if (frame->awaiting_relay == 1) {
			return NULL;
		}
	}
	frame->timestamp = timestamp;
	frame->input_source = 1;
	frame->awaiting_relay = 0;
	frame->input = *input;
	return frame;

#endif
}

/* Returns the last frame in a player's input history that still awaits relay
 * (awaiting_relay nonzero), or NULL when none does. */
// FUNCTION: XVT 0x418890
struct input_frame *flight_sync_find_last_unrelayed_input_frame(int player_idx)
{
	struct input_frame *frame;
	int frame_count;
	struct input_frame *result;

	frame = g_input_history[player_idx];
	frame_count = g_input_frame_count[player_idx];
	result = 0;
	while (frame_count > 0) {
		if (frame->awaiting_relay != 0) {
			result = frame;
		}
		++frame;
		--frame_count;
	}
	return result;
}

/* Marks all eight render samples and saved simulated poses invalid. */
// FUNCTION: XVT 0x418950
void flight_sync_reset_remote_player_render_smoothing(void)
{
	int player_index;

	for (player_index = 0; player_index < 8; ++player_index) {
		g_remote_player_render_samples[player_index].valid = 0;
		g_remote_player_saved_sim_poses[player_index].valid = 0;
	}
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
	int player_index;

	if (g_remote_player_render_smoothing_enabled == 0) {
		return;
	}

	player_index = 0;
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
	int player_index;
	struct object_record *object;
	int predicted_world_x;
	int predicted_world_y;
	int predicted_world_z;
	int position_delta_x;
	int position_delta_y;
	int position_delta_z;
	int elapsed_time;
	int prediction_distance;
	int rough_distance;
	int position_blend;
	int max_angle_change;
	int angle_difference;
	int signed_angle_difference;
	int candidate_angle;
	int candidate_difference;
	int blended_x;
	int blended_y;
	int blended_z;

	if (g_remote_player_render_smoothing_enabled == 0) {
		return;
	}

	for (player_index = 0; player_index < 8; ++player_index) {
		g_remote_player_saved_sim_poses[player_index].valid = 0;
		if (g_players[player_index].participation_state == 0 ||
		    g_players[player_index].object_index == -1) {
			continue;
		}

		object = &g_object_table[g_players[player_index].object_index];
		if (object->object_type == 0 || object->mobj == NULL ||
		    g_remote_player_render_samples[player_index].valid == 0 ||
		    player_index == g_local_player ||
		    g_remote_player_render_samples[player_index]
				    .object_signature !=
			    g_players[player_index].bound_object_signature) {
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

		predicted_world_x =
			g_remote_player_render_samples[player_index].world_x;
		predicted_world_y =
			g_remote_player_render_samples[player_index].world_y;
		predicted_world_z =
			g_remote_player_render_samples[player_index].world_z;

		elapsed_time = object->mobj->sim_state_timestamp -
			       g_remote_player_render_samples[player_index]
				       .sim_state_timestamp;
		if (elapsed_time < 0) {
			continue;
		}

		prediction_distance = 0;
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

		position_delta_x = object->world_x - predicted_world_x;
		position_delta_y = object->world_y - predicted_world_y;
		position_delta_z = object->world_z - predicted_world_z;
		rough_distance = collide_roughdistance3d(
			position_delta_x, position_delta_y, position_delta_z);
		prediction_distance *= 32;
		if (prediction_distance >= rough_distance &&
		    rough_distance != 0) {
			position_blend =
				(rough_distance << 14) / prediction_distance;
		} else {
			position_blend = 0x4000;
		}
		blended_x = math_mul_q15(position_blend, position_delta_x);
		blended_y = math_mul_q15(position_blend, position_delta_y);
		blended_z = math_mul_q15(position_blend, position_delta_z);
		predicted_world_x += blended_x;
		predicted_world_y += blended_y;
		predicted_world_z += blended_z;
		object->world_x = predicted_world_x;
		object->world_y = predicted_world_y;
		object->world_z = predicted_world_z;

		angle_difference =
			(int16_t)(object->roll -
				  g_remote_player_render_samples[player_index]
					  .roll);
		signed_angle_difference = angle_difference;
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
		max_angle_change =
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
	}
}

#ifndef XVT_MODERN
/* Applies one world message from the server. A client first copies it into
 * the replay buffer while g_flight_net_buffer_world_messages_until_checksum is 1.
 * The tick is word 1 without its top bit, which asks for a world checksum. A
 * tick not past g_server_tick_time is ignored; one that is not exactly
 * g_net_update_interval_ticks past it first empties the flight receive queue
 * (net_reliable_reset_recv_queue_state). It then drops predicted inputs, restores
 * the saved world state of g_server_tick_time (marking every live object's
 * transforms for recomputing when
 * g_flight_net_dirty_all_object_transforms_after_restore is set), inserts each
 * active player's inputs from the message as server frames (input_source 0),
 * runs the simulation to the tick, updates the cameras, flushes queued
 * sounds and saves the new state; g_game_time and g_server_tick_time become the
 * tick. When a checksum is asked for, it computes one, stores the tick in
 * g_flight_net_world_checksum_epoch, sends it to the host (a host also broadcasts
 * it), turns buffering on, snapshots the state and empties the buffer. Only
 * the original build calls this. */
// FUNCTION: XVT 0x418F80
void flight_sync_apply_world_message_packet(uint8_t *packet)
{
	enum {
		PLAYER_SLOT_COUNT = 8,
		FULL_TIMESTAMP_CODE = 127,
		SHORT_DELTA_CODE = 126,
		BYTE_DELTA_CODE = 125,
		KEY_PRESENT_FLAG = 0x80,
		DELTA_CODE_MASK = 0x7F,
		WORLD_CHECKSUM_FLAG = INT32_MIN,
		WORLD_TIMESTAMP_MASK = 0x7FFFFFFFu
	};

	uint32_t raw_packet_tick;
	int object_index;
	int player_index;
	int packet_tick;
	int checksum_requested;
	struct flight_input_frame_record input;

	if (net_session_is_local_host() == 0 &&
	    g_flight_net_buffer_world_messages_until_checksum == 1) {
		flight_sync_buffer_world_message_packet(packet);
	}

	raw_packet_tick = ((const uint32_t *)packet)[1];
	packet += 2 * sizeof(int);
	packet_tick = (int)(raw_packet_tick & WORLD_TIMESTAMP_MASK);
	checksum_requested = (int)(raw_packet_tick & WORLD_CHECKSUM_FLAG);
	if (packet_tick <= g_server_tick_time) {
		return;
	}

	if (packet_tick - g_net_update_interval_ticks != g_server_tick_time) {
		net_reliable_reset_recv_queue_state();
	}
	flight_sync_discard_all_predicted_input_frames();
	flight_restore_world_state();
	g_game_time = g_server_tick_time;

	if (g_flight_net_dirty_all_object_transforms_after_restore != 0) {
		for (object_index = 0;
		     object_index < g_region_main_object_slot_end;
		     ++object_index) {
			if (g_object_table[object_index].object_type != 0 &&
			    g_object_table[object_index].mobj != NULL) {
				g_object_table[object_index]
					.mobj->move_vector_dirty = 1;
				g_object_table[object_index]
					.mobj->orient_matrix_dirty = 1;
			}
		}
		g_flight_net_dirty_all_object_transforms_after_restore = 0;
	}

	{
		uint8_t *cursor;
		int remaining_player_blocks;

		cursor = packet;
		remaining_player_blocks = *cursor++;
		for (player_index = 0; player_index < PLAYER_SLOT_COUNT;
		     ++player_index) {
			int frame_count;

			if (g_players[player_index].participation_state == 0) {
				continue;
			}
			if (remaining_player_blocks == 0) {
				break;
			}

			--remaining_player_blocks;
			frame_count = *cursor++;
			while (frame_count > 0) {
				struct input_frame *inserted;
				int timestamp_code;
				int delta_code;
				int timestamp;

				timestamp_code = *cursor++;
				delta_code = timestamp_code & DELTA_CODE_MASK;
				if (delta_code == FULL_TIMESTAMP_CODE) {
					timestamp = *(const int *)cursor;
					cursor += sizeof(timestamp);
				} else if (delta_code == SHORT_DELTA_CODE) {
					timestamp = packet_tick -
						    *(const uint16_t *)cursor;
					cursor += sizeof(uint16_t);
				} else {
					timestamp = packet_tick;
					if (delta_code == BYTE_DELTA_CODE) {
						timestamp -= *cursor++;
					}
					timestamp -= delta_code;
				}

				if ((timestamp_code & KEY_PRESENT_FLAG) != 0) {
					input.key = *cursor++;
				} else {
					input.key = 0;
				}
				input.axis_x =
					(int8_t)(cursor[0] & (uint8_t)~1u);
				input.axis_y =
					(int8_t)(cursor[1] & (uint8_t)~1u);
				input.key_mods = cursor[1] & 1u;
				input.key_mods = (uint8_t)(input.key_mods << 1);
				input.key_mods |= cursor[0] & 1u;
				cursor += 2;

				inserted = flight_sync_insert_input_frame(
					player_index, timestamp, &input);
				if (inserted != NULL) {
					inserted->input_source = 0;
					inserted->awaiting_relay = 0;
				}
				--frame_count;
			}
		}
	}

	g_flight_sim_side_effects_suppressed = 0;
	flight_step_sim_to_time(packet_tick);
	for (player_index = 0; player_index < PLAYER_SLOT_COUNT;
	     ++player_index) {
		if (g_players[player_index].participation_state != 0 &&
		    player_index != g_local_player) {
			flight_view_update_player_camera(player_index);
		}
	}
	flight_view_update_player_camera(g_local_player);
	g_game_time = packet_tick;
	g_server_tick_time = packet_tick;
	sound_flush_queued_effects();
	flight_save_world_state();

	if (checksum_requested != 0) {
		int checksum_dword_count = (int)(sizeof(g_world_checksum) /
						 sizeof(g_world_checksum[0]));

		flight_checksum_world_state(0, 0);
		g_flight_net_world_checksum_epoch =
			(unsigned int)g_server_tick_time;
		if (net_session_is_local_host() != 0) {
			flight_net_broadcast_world_checksum(
				(const int *)g_world_checksum,
				(const int *)g_world_checksum_region_lengths,
				checksum_dword_count);
		}
		flight_net_send_world_checksum_to_host(
			(const int *)g_world_checksum,
			(const int *)g_world_checksum_region_lengths,
			checksum_dword_count);
		g_flight_net_buffer_world_messages_until_checksum = 1;
		flight_sync_snapshot_world_state_for_replay();
		flight_sync_clear_buffered_world_messages();
	}
}
#endif

/* Handles a player's world checksum for the epoch in
 * g_flight_net_world_checksum_epoch (word 1); other epochs, and players that
 * aborted or are inactive, are ignored. When the 16 region checksums from
 * another player differ from the local ones, the original build sends that
 * player the snapshot in g_world_state_dup_buffer
 * (flight_net_send_world_state_resync_to_player, then
 * flight_net_send_world_state_resync_apply_request when that succeeds); the modern
 * build starts sending it with xvt_resync_begin_send and returns. On the host it
 * then records the player in g_flight_net_world_checksum_peer_status as matched
 * (1) or not (2), and turns buffering off once every active player has
 * matched. In the modern build word 34 is a request code: code 1 is taken in
 * any epoch, and on the host, from another player, starts sending the live
 * world state instead. The original build does not check that the sender has
 * a player slot; the lookup then returns 8, past the 8-entry tables. */
// FUNCTION: XVT 0x4193C0
void flight_sync_handle_world_checksum_packet(int sender_dpid,
					      const int *packet)
{
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

	int all_peer_status;
	int checksum_mismatch;
	int local_world_state_size;
	int player_index;
	int remote_world_state_size;
	int sender_player_index;

	if ((unsigned int)packet[PACKET_EPOCH_INDEX] !=
		    g_flight_net_world_checksum_epoch
#ifdef XVT_MODERN
	    && packet[34] != XVT_CHECKSUM_REQUEST_STATE
#endif
	) {
		return;
	}

	sender_player_index = net_session_find_player_slot_by_dpid(sender_dpid);
	checksum_mismatch = 0;
#ifdef XVT_MODERN
	if ((unsigned)sender_player_index >= 8) {
		return;
	}
	if ((unsigned)packet[34] > 1) {
		return;
	}
	if (packet[34] == 1 && net_session_is_local_host() &&
	    sender_player_index != g_local_player) {
		xvt_resync_begin_send(sender_dpid, g_world_state_buffer,
				      g_world_state_size);
		return;
	}
#endif
	if (g_player_abort_flags[sender_player_index] != 0 ||
	    g_players[sender_player_index].participation_state == 0) {
		return;
	}

	if (g_local_player != sender_player_index) {
		const int *remote_checksums = &packet[PACKET_CHECKSUM_INDEX];
		const int *remote_region_lengths =
			&packet[PACKET_REGION_LENGTH_INDEX];

		local_world_state_size = 0;
		remote_world_state_size = 0;
		/* player_index is reused here as a checksum region index. */
		for (player_index = 0; player_index < CHECKSUM_REGION_COUNT;
		     ++player_index) {
			remote_world_state_size +=
				remote_region_lengths[player_index];
			local_world_state_size += (int)
				g_world_checksum_region_lengths[player_index];
			if (g_world_checksum[player_index] !=
			    (unsigned int)remote_checksums[player_index]) {
				checksum_mismatch = 1;
			}
		}
		(void)local_world_state_size;
		(void)remote_world_state_size;

		if (checksum_mismatch != 0) {
#ifdef XVT_MODERN
			xvt_resync_begin_send(sender_dpid,
					      g_world_state_dup_buffer,
					      g_world_state_dup_size);
			return;
#else
			if (flight_net_send_world_state_resync_to_player(
				    sender_dpid, g_world_state_dup_buffer,
				    g_world_state_dup_size) != 0) {
				flight_net_send_world_state_resync_apply_request(
					sender_dpid, g_world_state_dup_size);
			}
			checksum_mismatch = 1;
#endif
		}
	}

	if (net_session_is_local_host() == 0) {
		return;
	}

	g_flight_net_world_checksum_peer_status[sender_player_index] =
		PEER_STATUS_MISMATCHED;
	if (checksum_mismatch != 1) {
		g_flight_net_world_checksum_peer_status[sender_player_index] =
			PEER_STATUS_MATCHED;
	}

	all_peer_status = ALL_PEER_STATUS_BITS;
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
}

/* On a client, compares the 16 world checksum words the server sent for the
 * current epoch with the local ones; when all match, turns buffering off and
 * empties the world-message buffer. Ignored on the host and for any other
 * epoch. */
// FUNCTION: XVT 0x419510
void flight_sync_handle_server_checksum_packet(uint8_t *packet)
{
	uint32_t *packet_checksum;
	unsigned int *local_checksum;
	int checksum_mismatch;

	if (net_session_is_local_host() != 0 ||
	    ((uint32_t *)packet)[1] != g_flight_net_world_checksum_epoch) {
		return;
	}

	packet_checksum = (uint32_t *)packet + 2;
	checksum_mismatch = 0;
	local_checksum = g_world_checksum;
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
	}
}

/* Copies size bytes of a resent world state to offset in
 * g_world_state_dup_buffer, with no bounds check. Only the original build calls
 * this. */
// FUNCTION: XVT 0x419570
void flight_sync_copy_world_state_resync_chunk(const void *src, int offset,
					       unsigned int size)
{
	memcpy(&g_world_state_dup_buffer[offset], src, size);
}

#ifndef XVT_MODERN
#pragma intrinsic(memcpy)
#endif

#ifndef XVT_MODERN
/* Takes the world state a resync left in g_world_state_dup_buffer as the saved
 * state (g_world_state_buffer and g_world_state_size), sets g_server_tick_time to
 * server_tick_time and sets g_flight_net_dirty_all_object_transforms_after_restore.
 * Then it computes the world checksum and sends it to the host, snapshots
 * the state again, turns buffering off and replays the buffered world
 * messages. Only the original build calls this. */
// FUNCTION: XVT 0x4195A0
void flight_sync_apply_resync_and_replay_world_messages(
	unsigned int world_state_bytes, int server_tick_time)
{
	int checksum_dword_count;

	g_world_state_dup_size = (int)world_state_bytes;
	g_flight_net_dirty_all_object_transforms_after_restore = 1;
	memcpy(g_world_state_buffer, g_world_state_dup_buffer,
	       world_state_bytes);
	g_world_state_size = (unsigned int)g_world_state_dup_size;
	g_server_tick_time = server_tick_time;
	flight_checksum_world_state(0, 0);
	checksum_dword_count =
		(int)(sizeof(g_world_checksum) / sizeof(g_world_checksum[0]));
	flight_net_send_world_checksum_to_host(
		(const int *)g_world_checksum,
		(const int *)g_world_checksum_region_lengths,
		checksum_dword_count);
	flight_sync_snapshot_world_state_for_replay();
	g_flight_net_buffer_world_messages_until_checksum = 0;
	flight_sync_replay_buffered_world_messages();
}
#endif

/* Copies the saved world state (g_world_state_size bytes of
 * g_world_state_buffer) into g_world_state_dup_buffer and sets
 * g_world_state_dup_size: the copy a resync sends to a player whose checksum
 * differs. */
// FUNCTION: XVT 0x419620
void flight_sync_snapshot_world_state_for_replay(void)
{
	unsigned int snapshot_bytes;

	snapshot_bytes = g_world_state_size;
	memcpy(g_world_state_dup_buffer, g_world_state_buffer, snapshot_bytes);
	g_world_state_dup_size = (int)snapshot_bytes;
}

#ifndef XVT_MODERN
/* Appends one world message to g_world_message_buffer, growing the buffer by
 * 100 times the message's size when it does not fit (a failed allocation is
 * a fatal error). The size is the 9-byte header plus each player's inputs,
 * walked by their time codes. The walk counts the 4 bytes after code 127 but
 * not the 2 after code 126 or the 1 after code 125, which
 * flight_sync_apply_world_message_packet reads, so a message using those is
 * stored short. Only the original build calls this. */
// FUNCTION: XVT 0x419650
void flight_sync_buffer_world_message_packet(uint8_t *packet)
{
	uint16_t old_handle;
	int packet_size;
	uint8_t *packet_start;
	int player_block_count;

	packet_size = 9;
	packet_start = packet;
	player_block_count = packet[8];
	packet += 8;
	++packet;
	if (player_block_count > 0) {
		do {
			int frame_count;

			frame_count = *packet++;
			++packet_size;
			if (frame_count > 0) {
				do {
					int timestamp_code;

					timestamp_code = *packet++;
					++packet_size;
					if ((timestamp_code & 0x7F) == 0x7F) {
						packet += 4;
						packet_size += 4;
					}
					if ((timestamp_code & 0x80) != 0) {
						++packet;
						++packet_size;
					}
					packet += 2;
					packet_size += 2;
					--frame_count;
				} while (frame_count != 0);
			}
			--player_block_count;
		} while (player_block_count != 0);
	}

	if (g_world_message_buffer_bytes_free < packet_size) {
		unsigned int old_capacity;
		int growth;
		uint8_t *old_buffer;

		old_handle = g_world_message_buffer_handle;
		old_capacity = g_world_message_buffer_capacity;
		growth = 100 * packet_size;
		g_world_message_buffer_bytes_free += growth;
		g_world_message_buffer_capacity += growth;
		g_world_message_buffer_handle =
			memory_alloc_handle(g_world_message_buffer_capacity, 0);
		if (g_world_message_buffer_handle == 0) {
			fe_disk_io_fatal_error(
				FILE_ERROR_STR_NOT_ENOUGH_MEMORY);
		}
		g_world_message_buffer =
			memory_get_handle_block(g_world_message_buffer_handle);
		if (old_handle != 0) {
			old_buffer = memory_get_handle_block(old_handle);
			memcpy(g_world_message_buffer, old_buffer,
			       old_capacity);
			memory_handle_block_done_stub(old_handle);
			memory_free_handle(old_handle);
		}
	}

	memcpy(&g_world_message_buffer[g_world_message_buffer_capacity -
				       g_world_message_buffer_bytes_free],
	       packet_start, packet_size);
	g_world_message_buffer_bytes_free -= packet_size;
	++g_world_message_buffered_count;
}
#endif

/* Empties the world-message buffer: the modern build clears the replay queue
 * (xvt_flight_messages_clear); the original keeps its memory and resets the
 * count and the free space. */
// FUNCTION: XVT 0x4197B0
void flight_sync_clear_buffered_world_messages(void)
{
#ifdef XVT_MODERN
	xvt_flight_messages_clear(XVT_QUEUE_REPLAY);
#else
	g_world_message_buffered_count = 0;
	g_world_message_buffer_bytes_free = g_world_message_buffer_capacity;
#endif
}

#ifndef XVT_MODERN
/* Applies every buffered world message in order through
 * flight_sync_apply_world_message_packet, after clearing each one's checksum
 * request bit, then empties the buffer. It steps from one message to the
 * next with the same short size count as flight_sync_buffer_world_message_packet.
 * Only the original build calls this. */
// FUNCTION: XVT 0x4197D0
void flight_sync_replay_buffered_world_messages(void)
{
	enum {
		PACKET_PLAYER_COUNT_OFFSET = 2 * sizeof(int),
		FULL_TIMESTAMP_CODE = 0x7F,
		KEY_PRESENT_FLAG = 0x80,
		DELTA_CODE_MASK = 0x7F,
		INPUT_AXIS_BYTES = 2
	};

	int packet_offset;

	packet_offset = 0;
	while (g_world_message_buffered_count != 0) {
		uint8_t *cursor;
		uint8_t *packet;
		int player_sections_remaining;

		--g_world_message_buffered_count;
		packet = &g_world_message_buffer[packet_offset];
		packet_offset += PACKET_PLAYER_COUNT_OFFSET;
		cursor = packet + PACKET_PLAYER_COUNT_OFFSET;
		++packet_offset;
		player_sections_remaining = *cursor++;
		while (player_sections_remaining > 0) {
			int frame_header;
			int frames_remaining;

			frames_remaining = *cursor++;
			++packet_offset;
			while (frames_remaining > 0) {
				frame_header = *cursor++;
				++packet_offset;
				if ((frame_header & DELTA_CODE_MASK) ==
				    FULL_TIMESTAMP_CODE) {
					cursor += sizeof(uint32_t);
					packet_offset += sizeof(uint32_t);
				}
				if ((frame_header & KEY_PRESENT_FLAG) != 0) {
					++cursor;
					++packet_offset;
				}
				cursor += INPUT_AXIS_BYTES;
				packet_offset += INPUT_AXIS_BYTES;
				--frames_remaining;
			}
			--player_sections_remaining;
		}

		((uint32_t *)packet)[1] &= INT32_MAX;
		flight_sync_apply_world_message_packet(packet);
	}
	g_world_message_buffer_bytes_free = g_world_message_buffer_capacity;
}
#endif

/* Returns what sound_unused_four_arg_stub returns, 0. Nothing in the engine
 * calls this. */
// FUNCTION: XVT 0x419870
int flight_sync_unused_four_arg_forwarder(int arg1, int arg2, int arg3,
					  int arg4)
{
	return sound_unused_four_arg_stub(arg1, arg2, arg3, arg4);
}
