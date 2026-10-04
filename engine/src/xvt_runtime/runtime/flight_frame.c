#include "xvt_runtime/runtime/flight_frame.h"

#include "xvt_runtime/input/flight_controls.h"
#include "xvt_runtime/log/log.h"
#include "xvt_runtime/runtime/flight_checkpoint.h"
#include "xvt_runtime/runtime/flight_internal.h"
#include "xvt_runtime/runtime/flight_messages.h"
#include "xvt_runtime/runtime/flight_network.h"
#include "xvt_runtime/runtime/flight_prediction.h"
#include "xvt_runtime/runtime/resync_task.h"
#include "xvt_runtime/snapshot/render_capture.h"
#include "xvt_runtime/timing/flight_timing.h"

enum {
	MINIMUM_FRAME_ADVANCE_TICKS = 4,
	FRAME_ADJUST_DIVISOR_SHIFT = 3,
	CLOCK_ADJUST_DIVISOR_SHIFT = 4,
	MAX_FINE_FRAME_ADJUSTMENT = 4,
	LAG_LEVEL_1_TICKS = 472,
	LAG_LEVEL_2_TICKS = 944,
	LAG_LEVEL_3_TICKS = 1416,
	HOST_TIMEOUT_TICKS = 7080,
	UPDATE_HISTOGRAM_BUCKETS = 20,
	LONG_UPDATE_TICKS = 8,
	PACKET_DROP_SCORE_STEP = 10,
	PACKET_DROP_LEVEL_2_SCORE = 10,
	PACKET_DROP_LEVEL_3_SCORE = 20,
};

enum { XVT_FRAME_WAIT, XVT_FRAME_ADVANCE };

static struct {
	int phase, saved_input_timestamp, frame_target_timestamp;
	int loop_start_timestamp, frame_start_timestamp;
} g_frame;

typedef enum xvt_confirmation_phase {
	XVT_CONFIRM_IDLE,
	XVT_CONFIRM_APPLY,
	XVT_CONFIRM_REPLAY,
	XVT_CONFIRM_REBUILD,
	XVT_CONFIRM_TERMINAL
} xvt_confirmation_phase;

static struct {
	struct xvt_flight_message message;
	xvt_confirmation_phase phase;
	int publish_floor, suspended, predicted_suspended;
	uint64_t iteration_start_us, deadline;
	unsigned steps;
	int budget_initialized;
} g_confirm;

extern uint32_t g_last_tick_time_ms;

/* Says why the frame loop ends and returns 1, the frame loop's "flight over". */
static int xvt_flight_frame_end(const char *reason)
{
	XVT_LOG_INFO("flight.frames_end reason=\"%s\" tick=%d", reason,
		     g_game_time);
	return 1;
}

uint64_t xvt_flight_time_delay_for_ticks(unsigned int ticks)
{
	uint64_t now = xvt_time_get_elapsed_us();
	uint32_t ms = (uint32_t)(now / 1000);
	uint32_t base = g_last_tick_time_ms ? g_last_tick_time_ms : ms;
	uint64_t elapsed = (uint64_t)(uint32_t)(ms - base) * 1000 + now % 1000;
	uint64_t required = (uint64_t)ticks * 4000;
	return elapsed < required ? required - elapsed : 0;
}

void xvt_flight_frame_begin(void)
{
	memset(&g_frame, 0, sizeof(g_frame));
	memset(&g_confirm, 0, sizeof g_confirm);
	if (xvt_flight_timing_is_network125()) {
		g_predicted_frame_delta = XVT_NETWORK_STEP_TICKS;
	}
	g_flight_last_step_target_timestamp = 0;
	g_last_local_replay_input_timestamp = 0;
	g_flight_sfx_side_effect_gate = 0;
	g_flight_prev_host_packet_drop_count = 0;
	g_flight_packet_drop_score = 0;
	memset(g_flight_update_duration_histogram, 0,
	       sizeof(g_flight_update_duration_histogram));
}

static int xvt_flight_frame_target(void)
{
	int frame_adjustment, frame_target_timestamp;
	if (xvt_flight_timing_is_unlocked()) {
		return g_input_timestamp;
	}
	if (g_flight_last_step_target_timestamp == 0) {
		frame_target_timestamp = g_input_timestamp;
	} else {
		frame_target_timestamp = g_flight_last_step_target_timestamp +
					 g_predicted_frame_delta;
		if ((unsigned int)frame_target_timestamp >=
		    (unsigned int)g_input_timestamp) {
			if ((unsigned int)frame_target_timestamp >
			    (unsigned int)g_input_timestamp) {
				frame_adjustment = frame_target_timestamp -
						   g_input_timestamp;
				if (frame_adjustment >
				    g_predicted_frame_delta >>
				    FRAME_ADJUST_DIVISOR_SHIFT) {
					frame_adjustment =
						g_predicted_frame_delta >>
						FRAME_ADJUST_DIVISOR_SHIFT;
				}
				if (frame_adjustment == 0) {
					frame_adjustment = 1;
				}
				if (frame_adjustment >
				    MAX_FINE_FRAME_ADJUSTMENT) {
					frame_adjustment =
						frame_target_timestamp -
						g_input_timestamp;
				}
				frame_target_timestamp -= frame_adjustment;
			}
		} else {
			frame_adjustment =
				g_input_timestamp - frame_target_timestamp;
			if (frame_adjustment > g_predicted_frame_delta >>
			    FRAME_ADJUST_DIVISOR_SHIFT) {
				frame_adjustment = g_predicted_frame_delta >>
						   FRAME_ADJUST_DIVISOR_SHIFT;
			}
			if (frame_adjustment == 0) {
				frame_adjustment = 1;
			}
			if (frame_adjustment > MAX_FINE_FRAME_ADJUSTMENT) {
				frame_adjustment = g_input_timestamp -
						   frame_target_timestamp;
			}
			frame_target_timestamp += frame_adjustment;
		}
	}
	if (frame_target_timestamp - g_game_time <
	    MINIMUM_FRAME_ADVANCE_TICKS) {
		frame_target_timestamp =
			g_game_time + MINIMUM_FRAME_ADVANCE_TICKS;
	}
	return frame_target_timestamp;
}

static void xvt_flight_frame_invalidate_remote_transforms(void)
{
	if (!xvt_flight_timing_is_network125() ||
	    !g_remote_player_render_smoothing_enabled) {
		return;
	}
	/* Changing to/from a smoothed pose also invalidates its derived transforms.
	 * Network125 predicts again before the next authoritative restore. */
	for (unsigned player = 0; player < XVT_FLIGHT_PLAYERS; ++player) {
		int slot = g_players[player].object_index;
		if (player == (unsigned)g_local_player ||
		    !g_players[player].participation_state || slot < 0 ||
		    slot >= g_region_main_object_slot_end) {
			continue;
		}
		struct object_record *object = &g_object_table[slot];
		if (!object->object_type || !object->mobj) {
			continue;
		}
		object->mobj->move_vector_dirty = 1;
		object->mobj->orient_matrix_dirty = 1;
	}
}

/* Sets the lag indicator (0 to 3) from how far local input time runs past the last server tick,
 * beyond the clock lead the host allows. */
static void xvt_flight_frame_update_lag_indicator(void)
{
	int lag_ticks;

	lag_ticks = g_input_timestamp - g_flight_net_clock_lead_ticks -
		    g_server_tick_time;
	if (lag_ticks < LAG_LEVEL_1_TICKS) {
		g_lag_indicator = 0;
	} else if (lag_ticks < LAG_LEVEL_2_TICKS) {
		g_lag_indicator = 1;
	} else if (lag_ticks < LAG_LEVEL_3_TICKS) {
		g_lag_indicator = 2;
	} else {
		g_lag_indicator = 3;
	}
}

/* Sets the packet drop indicator (0 to 3). A drop score would rise with each new host packet drop and
 * fall by one per frame, but only once a previous host drop count is recorded, and only that branch
 * records one; Begin clears the count, so the indicator stays 0, as in the original. */
static void xvt_flight_frame_update_packet_drop_indicator(void)
{
	if (g_flight_prev_host_packet_drop_count == 0) {
		g_packet_drop_indicator = 0;
	} else {
		int host_dplay_id;
		int host_drop_count;

		host_dplay_id = net_session_get_host_dplay_id();
		host_drop_count =
			net_reliable_get_peer_packet_drop_count_by_dpid(
				host_dplay_id);
		g_flight_packet_drop_score +=
			PACKET_DROP_SCORE_STEP *
			(host_drop_count -
			 g_flight_prev_host_packet_drop_count);
		if (g_flight_packet_drop_score == 0) {
			g_packet_drop_indicator = 0;
		} else if (g_flight_packet_drop_score <
			   PACKET_DROP_LEVEL_2_SCORE) {
			g_packet_drop_indicator = 1;
		} else if (g_flight_packet_drop_score <
			   PACKET_DROP_LEVEL_3_SCORE) {
			g_packet_drop_indicator = 2;
		} else {
			g_packet_drop_indicator = 3;
		}
		g_flight_prev_host_packet_drop_count = host_drop_count;
		if (g_flight_packet_drop_score != 0) {
			--g_flight_packet_drop_score;
		}
	}
}

/* Formats the update-time histogram into the mission debug buffer one row at a time, each row
 * overwriting the last: raw counts per bucket, then, once any update has been counted, each
 * bucket's share in percent. */
static void xvt_flight_frame_format_update_histogram(void)
{
	unsigned int histogram_total;
	int histogram_index;

	sprintf(g_mission_debug_buffer,
		"Raw  0:%2d  1:%2d  2:%2d  3:%2d  4:%2d  5:%2d  6:%2d  7:%2d  8:%2d   9:%2d\n",
		g_flight_update_duration_histogram[0],
		g_flight_update_duration_histogram[1],
		g_flight_update_duration_histogram[2],
		g_flight_update_duration_histogram[3],
		g_flight_update_duration_histogram[4],
		g_flight_update_duration_histogram[5],
		g_flight_update_duration_histogram[6],
		g_flight_update_duration_histogram[7],
		g_flight_update_duration_histogram[8],
		g_flight_update_duration_histogram[9]);
	sprintf(g_mission_debug_buffer,
		"Raw 10:%2d 11:%2d 12:%2d 13:%2d 14:%2d 15:%2d 16:%2d 17:%2d 18:%2d >18:%2d\n",
		g_flight_update_duration_histogram[10],
		g_flight_update_duration_histogram[11],
		g_flight_update_duration_histogram[12],
		g_flight_update_duration_histogram[13],
		g_flight_update_duration_histogram[14],
		g_flight_update_duration_histogram[15],
		g_flight_update_duration_histogram[16],
		g_flight_update_duration_histogram[17],
		g_flight_update_duration_histogram[18],
		g_flight_update_duration_histogram[19]);

	histogram_total = 0;
	for (histogram_index = 0; histogram_index < UPDATE_HISTOGRAM_BUCKETS;
	     ++histogram_index) {
		histogram_total +=
			g_flight_update_duration_histogram[histogram_index];
	}
	if (histogram_total != 0) {
		sprintf(g_mission_debug_buffer,
			"Pct  0:%2d  1:%2d  2:%2d  3:%2d  4:%2d  5:%2d  6:%2d  7:%2d  8:%2d   9:%2d\n",
			100 * g_flight_update_duration_histogram[0] /
				histogram_total,
			100 * g_flight_update_duration_histogram[1] /
				histogram_total,
			100 * g_flight_update_duration_histogram[2] /
				histogram_total,
			100 * g_flight_update_duration_histogram[3] /
				histogram_total,
			100 * g_flight_update_duration_histogram[4] /
				histogram_total,
			100 * g_flight_update_duration_histogram[5] /
				histogram_total,
			100 * g_flight_update_duration_histogram[6] /
				histogram_total,
			100 * g_flight_update_duration_histogram[7] /
				histogram_total,
			100 * g_flight_update_duration_histogram[8] /
				histogram_total,
			100 * g_flight_update_duration_histogram[9] /
				histogram_total);
		sprintf(g_mission_debug_buffer,
			"Pct 10:%2d 11:%2d 12:%2d 13:%2d 14:%2d 15:%2d 16:%2d 17:%2d 18:%2d >18:%2d\n",
			100 * g_flight_update_duration_histogram[10] /
				histogram_total,
			100 * g_flight_update_duration_histogram[11] /
				histogram_total,
			100 * g_flight_update_duration_histogram[12] /
				histogram_total,
			100 * g_flight_update_duration_histogram[13] /
				histogram_total,
			100 * g_flight_update_duration_histogram[14] /
				histogram_total,
			100 * g_flight_update_duration_histogram[15] /
				histogram_total,
			100 * g_flight_update_duration_histogram[16] /
				histogram_total,
			100 * g_flight_update_duration_histogram[17] /
				histogram_total,
			100 * g_flight_update_duration_histogram[18] /
				histogram_total,
			100 * g_flight_update_duration_histogram[19] /
				histogram_total);
	}
}

static void xvt_flight_frame_render(void)
{
	int update_ticks, render_ticks, loop_ticks, render_start_timestamp;
	char overlay_line[180];
	g_input_timestamp += time_consume_elapsed_ticks();
	update_ticks = g_input_timestamp - g_frame.frame_start_timestamp;
	g_input_timestamp += time_consume_elapsed_ticks();
	xvt_flight_frame_update_lag_indicator();
	xvt_flight_frame_update_packet_drop_indicator();

	render_start_timestamp = g_input_timestamp;
	xvt_render_capture_complete_network_world();
	flight_sync_apply_remote_player_render_smoothing();
	xvt_flight_frame_invalidate_remote_transforms();
	if (xvt_flight_timing_is_network125()) {
		g_flight_sfx_side_effect_gate = 1;
	}
	flight_view_render_frame();
	g_flight_sfx_side_effect_gate = 0;
	sound_flush_queued_effects();
	flight_sync_capture_samples_and_restore_poses();
	xvt_flight_frame_invalidate_remote_transforms();
	g_input_timestamp += time_consume_elapsed_ticks();
	render_ticks = g_input_timestamp - render_start_timestamp;
	loop_ticks = g_input_timestamp - g_frame.loop_start_timestamp;
	if (loop_ticks == 0) {
		loop_ticks = 1;
	}

	if (g_flight_conf_tick_counter_enabled == 0) {
		g_flight_tick_overlay_sample_count = 0;
		g_flight_tick_overlay_window_ticks = 0;
	} else {
		int ai_projectile_count;
		int player_projectile_count;
		int object_index;

		/* The overlay's sampling window is 944 ticks, the second lag level's value, not a lag level. */
		if (g_flight_tick_overlay_window_ticks > LAG_LEVEL_2_TICKS) {
			g_flight_tick_overlay_sample_count = 0;
			g_flight_tick_overlay_window_ticks = 0;
		}
		g_flight_tick_overlay_last_loop_ticks = loop_ticks;
		g_flight_tick_overlay_window_ticks += loop_ticks;
		++g_flight_tick_overlay_sample_count;
		/* The original formats this tick-counter line too and never shows it; nothing reads overlay_line. */
		sprintf(overlay_line,
			"R:%-2d U:%-2d N:%-2d O:%-2d T:%-2d FR:%-2d NOW:%-7dL:%-7dS:%-7dW:%-3dD:%-3dA%d\n",
			render_ticks, update_ticks, 0,
			loop_ticks - update_ticks - render_ticks, loop_ticks,
			SIMULATION_TICKS_PER_SECOND / loop_ticks,
			g_input_timestamp, g_game_time, g_server_tick_time,
			g_input_timestamp - g_server_tick_time,
			g_flight_net_clock_lead_ticks,
			g_flight_net_clock_adjust_accum_ticks);
		if (update_ticks < 0) {
			update_ticks = 0;
		}
		if (update_ticks > UPDATE_HISTOGRAM_BUCKETS - 1) {
			++g_flight_update_duration_histogram
				[UPDATE_HISTOGRAM_BUCKETS - 1];
		} else {
			++g_flight_update_duration_histogram[update_ticks];
		}
		xvt_flight_frame_format_update_histogram();

		ai_projectile_count = 0;
		player_projectile_count = 0;
		for (object_index = g_projectile_object_slot_start;
		     object_index < g_projectile_object_slot_end;
		     ++object_index) {
			if (g_object_table[object_index].object_type != 0) {
				if (g_object_table[object_index].genus_id ==
				    6) {
					++player_projectile_count;
				} else {
					++ai_projectile_count;
				}
			}
		}
		if (update_ticks >= LONG_UPDATE_TICKS) {
			sprintf(g_mission_debug_buffer,
				"****** Long Update: %d ***** Warp: %d  *****  Player:  %d *****  AI:  %d\n",
				update_ticks, 0, player_projectile_count,
				ai_projectile_count);
		}
	}
}

static void xvt_flight_frame_adjust_clock(void)
{
	int clock_adjustment;
	int cap = g_predicted_frame_delta >> FRAME_ADJUST_DIVISOR_SHIFT;
	if (cap < 1) {
		cap = 1;
	}
	char fell_behind_log_line[180];
	g_input_timestamp += time_consume_elapsed_ticks();
	if ((unsigned int)(g_server_tick_time +
			   g_flight_net_clock_lead_ticks) >=
	    (unsigned int)g_input_timestamp) {
		if ((unsigned int)(g_server_tick_time +
				   g_flight_net_clock_lead_ticks) >
		    (unsigned int)g_input_timestamp) {
			clock_adjustment = (g_server_tick_time +
					    g_flight_net_clock_lead_ticks -
					    g_input_timestamp) >>
					   CLOCK_ADJUST_DIVISOR_SHIFT;
			if (clock_adjustment == 0) {
				clock_adjustment = 1;
			}
			if (clock_adjustment > cap) {
				clock_adjustment = cap;
			}
			g_flight_net_clock_adjust_accum_ticks -=
				clock_adjustment;
			g_input_timestamp += clock_adjustment;
		}
	} else {
		clock_adjustment =
			(g_input_timestamp - g_flight_net_clock_lead_ticks -
			 g_server_tick_time) >>
			CLOCK_ADJUST_DIVISOR_SHIFT;
		if (clock_adjustment == 0) {
			clock_adjustment = 1;
		}
		if (clock_adjustment > cap) {
			clock_adjustment = cap;
		}
		g_flight_net_clock_adjust_accum_ticks += clock_adjustment;
		g_input_timestamp -= clock_adjustment;
	}

	if (g_server_tick_time > g_input_timestamp) {
		/* Formatted as in the original, never printed: the event logged is network.fell_behind below. */
		sprintf(fell_behind_log_line,
			"Fell Behind! tickcounter:%-7d serverticks:%-7d adjustment:%-4d\n",
			g_input_timestamp, g_server_tick_time,
			g_server_tick_time + g_flight_net_clock_lead_ticks -
				g_input_timestamp);
		clock_adjustment = g_server_tick_time +
				   g_flight_net_clock_lead_ticks -
				   g_input_timestamp;
		XVT_LOG_DEBUG(
			"network.fell_behind input=%d server=%d adjust=%d",
			g_input_timestamp, g_server_tick_time,
			clock_adjustment);
		g_input_timestamp += clock_adjustment;
		g_flight_net_clock_adjust_accum_ticks -= clock_adjustment;
	}
}

static void xvt_flight_frame_start_advance(void)
{
	g_frame.frame_target_timestamp = xvt_flight_frame_target();
	g_predicted_frame_delta = g_frame.frame_target_timestamp -
				  g_flight_last_step_target_timestamp;
	g_frame.saved_input_timestamp = g_input_timestamp;
	g_input_timestamp = g_frame.frame_target_timestamp;
	flight_net_sample_local_input();
	g_flight_sim_side_effects_suppressed = 0;
	g_net_update_interval_ticks = g_input_timestamp - g_game_time;
	g_frame.phase = XVT_FRAME_ADVANCE;
}

static int xvt_flight_frame_advance(void)
{
	if (!xvt_flight_sim_step_to_time(g_frame.frame_target_timestamp)) {
		return 0;
	}
	g_game_time = g_input_timestamp;
	g_server_tick_time = g_input_timestamp;
	g_flight_last_step_target_timestamp = g_input_timestamp;
	g_input_timestamp +=
		g_frame.saved_input_timestamp - g_frame.frame_target_timestamp;
	sound_flush_queued_effects();
	xvt_flight_frame_render();
	g_frame.phase = XVT_FRAME_WAIT;
	return 0;
}

static void xvt_flight_frame_network_budget(void)
{
	uint64_t now = xvt_time_get_elapsed_us();
	if (!g_confirm.budget_initialized ||
	    g_confirm.iteration_start_us != now) {
		g_confirm.iteration_start_us = now;
		g_confirm.steps = 0;
		g_confirm.deadline = 0;
		g_confirm.budget_initialized = 1;
		xvt_flight_network_begin_iteration();
	}
}

static int xvt_flight_frame_has_budget(void)
{
	if (!g_confirm.deadline) {
		g_confirm.deadline = Aeron_NowUs() + XVT_SIM_BUDGET_US;
	}
	return g_confirm.steps < XVT_SIM_STEPS_PER_ITERATION &&
	       Aeron_NowUs() < g_confirm.deadline;
}

static void xvt_flight_frame_checksum(void)
{
	flight_checksum_world_state(0, 0);
	g_flight_net_world_checksum_epoch = (unsigned)g_server_tick_time;
	if (xvt_log_enabled(AERON_LOG_DEBUG)) {
		enum {
			REGIONS = sizeof(g_world_checksum) /
				  sizeof(g_world_checksum[0])
		};

		char sums[REGIONS * 9 + 1], lengths[REGIONS * 9 + 1];
		xvt_log_format_hex_list(sums, sizeof sums, g_world_checksum,
					REGIONS);
		xvt_log_format_hex_list(lengths, sizeof lengths,
					g_world_checksum_region_lengths,
					REGIONS);
		XVT_LOG_DEBUG(
			"network.checksum tick=%d host=%d sums=\"%s\" lengths=\"%s\"",
			g_server_tick_time, net_session_is_local_host() != 0,
			sums, lengths);
	}
	if (net_session_is_local_host()) {
		flight_net_broadcast_world_checksum(
			(const int *)g_world_checksum,
			(const int *)g_world_checksum_region_lengths, 16);
	}
	flight_net_send_world_checksum_to_host(
		(const int *)g_world_checksum,
		(const int *)g_world_checksum_region_lengths, 16);
	g_flight_net_buffer_world_messages_until_checksum = 1;
	flight_sync_snapshot_world_state_for_replay();
	flight_sync_clear_buffered_world_messages();
}

static xvt_flight_replay_result xvt_flight_frame_confirm(xvt_flight_queue queue)
{
	if (g_confirm.phase != XVT_CONFIRM_APPLY &&
	    g_confirm.phase != XVT_CONFIRM_REPLAY) {
		if (!xvt_flight_messages_peek(queue, &g_confirm.message,
					      sizeof g_confirm.message)) {
			return XVT_REPLAY_IDLE;
		}
		xvt_flight_messages_pop(queue);
		int target = (int)(g_confirm.message.target_flags & INT32_MAX);
		if (target <= g_server_tick_time) {
			return XVT_REPLAY_ADVANCED;
		}
		if (target - g_server_tick_time != XVT_WORLD_MESSAGE_TICKS) {
			XVT_LOG_DEBUG(
				"network.confirm_gap target=%d confirmed=%d",
				target, g_server_tick_time);
			xvt_flight_network_request_recovery();
			return XVT_REPLAY_PENDING;
		}
		if (queue == XVT_QUEUE_PENDING &&
		    g_flight_net_buffer_world_messages_until_checksum &&
		    !xvt_flight_messages_enqueue(&g_confirm.message,
						 XVT_QUEUE_REPLAY)) {
			xvt_flight_network_request_recovery();
			return XVT_REPLAY_PENDING;
		}
		if (queue == XVT_QUEUE_PENDING) {
			g_confirm.publish_floor =
				xvt_render_capture_last_view_tick();
		}
		flight_restore_world_state();
		if (g_flight_mission_state.mission_end_pending) {
			g_confirm.phase = XVT_CONFIRM_TERMINAL;
			return XVT_REPLAY_TERMINAL;
		}
		g_game_time = g_server_tick_time;
		xvt_flight_timing_restore_network_tick(g_game_time);
		xvt_flight_history_restore_checkpoint();
		xvt_flight_checkpoint_apply_confirmed_mask(
			g_confirm.message.participant_mask);
		if (!xvt_flight_network_insert_world(&g_confirm.message)) {
			return XVT_REPLAY_PENDING;
		}
		g_confirm.phase = queue == XVT_QUEUE_REPLAY ? XVT_CONFIRM_REPLAY
							    : XVT_CONFIRM_APPLY;
	}
	int target = (int)(g_confirm.message.target_flags & INT32_MAX);
	g_flight_sim_side_effects_suppressed = 0;
	while (g_game_time < target && xvt_flight_frame_has_budget()) {
		xvt_flight_step_result result = xvt_flight_sim_step_to_time(
			g_game_time + XVT_NETWORK_STEP_TICKS);
		if (result == XVT_STEP_PENDING) {
			g_confirm.suspended = 1;
			return XVT_REPLAY_PENDING;
		}
		g_confirm.suspended = 0;
		++g_confirm.steps;
		if (result == XVT_STEP_TERMINAL ||
		    g_flight_mission_state.mission_end_pending) {
			g_confirm.phase = XVT_CONFIRM_TERMINAL;
			xvt_flight_messages_clear(XVT_QUEUE_PENDING);
			xvt_flight_messages_clear(XVT_QUEUE_REPLAY);
			return XVT_REPLAY_TERMINAL;
		}
		xvt_render_capture_check_network_correction();
	}
	if (g_game_time < target) {
		return XVT_REPLAY_PENDING;
	}
	for (unsigned i = 0; i < XVT_FLIGHT_PLAYERS; ++i) {
		if (g_players[i].participation_state &&
		    i != (unsigned)g_local_player) {
			flight_view_update_player_camera(i);
		}
	}
	flight_view_update_player_camera(g_local_player);
	g_server_tick_time = g_game_time;
	sound_flush_queued_effects();
	flight_save_world_state();
	XVT_LOG_DEBUG(
		"network.confirm tick=%d queue=\"%s\" mask=%02x records=%u checksum=%d",
		g_server_tick_time,
		queue == XVT_QUEUE_REPLAY ? "replay" : "pending",
		g_confirm.message.participant_mask, g_confirm.message.count,
		(g_confirm.message.target_flags & XVT_WORLD_CHECKSUM_FLAG) !=
			0);
	if (queue == XVT_QUEUE_PENDING &&
	    (g_confirm.message.target_flags & XVT_WORLD_CHECKSUM_FLAG)) {
		xvt_flight_frame_checksum();
	}
	g_confirm.phase = queue == XVT_QUEUE_REPLAY ? XVT_CONFIRM_IDLE
						    : XVT_CONFIRM_REBUILD;
	return XVT_REPLAY_ADVANCED;
}

xvt_flight_replay_result xvt_flight_frame_replay_buffered(void)
{
	xvt_flight_frame_network_budget();
	xvt_flight_replay_result result = XVT_REPLAY_ADVANCED;
	while (result == XVT_REPLAY_ADVANCED && xvt_flight_frame_has_budget()) {
		result = xvt_flight_frame_confirm(XVT_QUEUE_REPLAY);
	}
	return result;
}

void xvt_flight_frame_reset_replay(void)
{
	memset(&g_confirm, 0, sizeof g_confirm);
	xvt_flight_sim_reset();
}

static int xvt_flight_frame_network_update(void)
{
	if (g_confirm.phase == XVT_CONFIRM_TERMINAL) {
		return xvt_flight_frame_end("mission_ended");
	}
	xvt_flight_frame_network_budget();
	if (!g_confirm.suspended && !g_confirm.predicted_suspended) {
		int elapsed = time_consume_elapsed_ticks();
		if ((int64_t)g_input_timestamp + elapsed >= INT32_MAX - 258) {
			flight_net_broadcast_host_session_abort();
			return xvt_flight_frame_end("clock_limit");
		}
		g_input_timestamp += elapsed;
		if (!net_session_is_local_host()) {
			g_flight_net_host_timeout_elapsed_ticks += elapsed;
		}
		if (g_flight_net_host_timeout_elapsed_ticks >
		    HOST_TIMEOUT_TICKS) {
			XVT_LOG_WARN("network.host_timeout ticks=%d",
				     g_flight_net_host_timeout_elapsed_ticks);
			flight_net_broadcast_player_abort(g_local_player);
			return xvt_flight_frame_end("host_timeout");
		}
		xvt_flight_network_flush_input(g_input_timestamp);
		xvt_flight_network_flush_world();
		flight_net_process_incoming_packets();
		if (!g_players[g_local_player].participation_state ||
		    g_flight_net_host_abort_received) {
			return xvt_flight_frame_end(
				g_flight_net_host_abort_received
					? "host_abort"
					: "disconnected");
		}
		if (xvt_resync_is_active()) {
			return 0;
		}
	}
	if (g_confirm.predicted_suspended) {
		xvt_flight_step_result step = xvt_flight_sim_step_to_time(
			g_game_time + XVT_NETWORK_STEP_TICKS);
		if (step == XVT_STEP_PENDING) {
			return 0;
		}
		if (step == XVT_STEP_TERMINAL) {
			return xvt_flight_frame_end("mission_ended");
		}
		g_confirm.predicted_suspended = 0;
		++g_confirm.steps;
	}
	if (xvt_flight_network_needs_recovery()) {
		xvt_resync_service_recovery();
		return g_flight_mission_state.mission_end_pending
			       ? xvt_flight_frame_end("recovery")
			       : 0;
	}
	xvt_flight_replay_result result = XVT_REPLAY_ADVANCED;
	while (result == XVT_REPLAY_ADVANCED && xvt_flight_frame_has_budget()) {
		result = xvt_flight_frame_confirm(XVT_QUEUE_PENDING);
	}
	if (result == XVT_REPLAY_TERMINAL) {
		return xvt_flight_frame_end("mission_ended");
	}
	if (g_confirm.phase == XVT_CONFIRM_APPLY || result != XVT_REPLAY_IDLE) {
		return 0;
	}
	if (flight_recount_players_and_check_mission_end() &&
	    g_game_time == g_server_tick_time) {
		if (g_radio_message_backup_enabled) {
			msg_write_message_log_file();
			g_radio_message_backup_enabled = 0;
		}
		return xvt_flight_frame_end("mission_complete");
	}
	xvt_flight_frame_adjust_clock();
	if (g_input_timestamp >= INT32_MAX - 258) {
		flight_net_broadcast_host_session_abort();
		return xvt_flight_frame_end("clock_limit");
	}
	int target = g_input_timestamp & ~1;
	if (g_confirm.phase == XVT_CONFIRM_REBUILD &&
	    target < g_confirm.publish_floor) {
		target = g_confirm.publish_floor;
	}
	if (target > g_server_tick_time + XVT_PREDICTION_LEAD_TICKS) {
		target = g_server_tick_time + XVT_PREDICTION_LEAD_TICKS;
	}
	g_predicted_frame_delta = XVT_NETWORK_STEP_TICKS;
	while (g_game_time < target && xvt_flight_frame_has_budget()) {
		if (!xvt_flight_network_admit_input(g_game_time +
						    XVT_NETWORK_STEP_TICKS)) {
			break;
		}
		if (!xvt_flight_prediction_queue(g_game_time +
						 XVT_NETWORK_STEP_TICKS)) {
			break;
		}
		g_flight_sim_side_effects_suppressed = 1;
		xvt_flight_step_result step = xvt_flight_sim_step_to_time(
			g_game_time + XVT_NETWORK_STEP_TICKS);
		if (step == XVT_STEP_TERMINAL) {
			return xvt_flight_frame_end("mission_ended");
		}
		if (step == XVT_STEP_PENDING) {
			g_confirm.predicted_suspended = 1;
			return 0;
		}
		++g_confirm.steps;
		xvt_render_capture_check_network_correction();
	}
	g_flight_last_step_target_timestamp = g_game_time;
	xvt_flight_network_flush_input(g_input_timestamp);
	if (g_confirm.phase == XVT_CONFIRM_REBUILD &&
	    g_game_time < g_confirm.publish_floor) {
		return 0;
	}
	g_confirm.phase = XVT_CONFIRM_IDLE;
	g_frame.frame_start_timestamp = g_frame.loop_start_timestamp =
		g_input_timestamp;
	xvt_flight_frame_render();
	return 0;
}

int xvt_flight_frame_update(void)
{
	if (xvt_flight_timing_is_network125()) {
		return xvt_flight_frame_network_update();
	}
	if (xvt_flight_sim_is_paused() && !xvt_flight_sim_resume()) {
		return 0;
	}
	if (g_frame.phase == XVT_FRAME_ADVANCE) {
		return xvt_flight_frame_advance();
	}
	g_input_timestamp += time_consume_elapsed_ticks();
	g_frame.loop_start_timestamp = g_input_timestamp;
	if (g_input_timestamp - g_game_time <
	    (int)xvt_flight_timing_step_ticks()) {
		return 0;
	}
	if (flight_recount_players_and_check_mission_end()) {
		if (g_radio_message_backup_enabled) {
			msg_write_message_log_file();
			g_radio_message_backup_enabled = 0;
		}
		return xvt_flight_frame_end("mission_complete");
	}
	g_frame.frame_start_timestamp = g_input_timestamp;
	g_input_timestamp += time_consume_elapsed_ticks();
	xvt_flight_frame_start_advance();
	return xvt_flight_frame_advance();
}

uint64_t xvt_flight_frame_next_wake_delay_us(void)
{
	if (xvt_flight_timing_is_network125()) {
		int target = g_input_timestamp & ~1;
		if (target > g_server_tick_time + XVT_PREDICTION_LEAD_TICKS) {
			target = g_server_tick_time + XVT_PREDICTION_LEAD_TICKS;
		}
		if (g_confirm.phase == XVT_CONFIRM_APPLY ||
		    g_confirm.phase == XVT_CONFIRM_REPLAY ||
		    (g_confirm.phase == XVT_CONFIRM_REBUILD &&
		     g_game_time < g_confirm.publish_floor) ||
		    g_confirm.predicted_suspended ||
		    (g_game_time < target &&
		     !xvt_flight_network_needs_recovery())) {
			return 0;
		}
		uint64_t wake = xvt_flight_network_next_wake_delay_us(
			g_input_timestamp);
		if (!xvt_flight_network_needs_recovery() &&
		    g_game_time <
			    g_server_tick_time + XVT_PREDICTION_LEAD_TICKS) {
			int remaining = g_game_time + XVT_NETWORK_STEP_TICKS -
					g_input_timestamp;
			uint64_t step_delay_us =
				remaining > 0 ? xvt_flight_time_delay_for_ticks(
							(unsigned)remaining)
					      : 0;
			if (step_delay_us < wake) {
				wake = step_delay_us;
			}
		}
		return wake;
	}
	if (xvt_flight_sim_is_paused()) {
		return UINT64_MAX;
	}
	int remaining = (int)xvt_flight_timing_step_ticks() -
			(g_input_timestamp - g_game_time);
	return remaining > 0 ? xvt_flight_time_delay_for_ticks(
				       (unsigned int)remaining)
			     : 0;
}
