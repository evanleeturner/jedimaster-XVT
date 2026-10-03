#include "xvt_runtime/input/flight_controls.h"
#include "xvt_runtime/log/log.h"
#include "xvt_runtime/runtime/flight_checkpoint.h"
#include "xvt_runtime/runtime/flight_internal.h"
#include "xvt_runtime/runtime/flight_prediction.h"
#include "xvt_runtime/runtime/flight_protocol.h"
#include "xvt_runtime/runtime/port.h"
#include "xvt_runtime/snapshot/render_capture.h"
#include "xvt_runtime/timing/flight_timing.h"
#include "xvt_runtime/timing/reference_motion.h"

static struct {
	int paused, player_step_pending, replay_pending, step_pending,
		zero_step;
	int target, advance_target, step_game_time, player, frame_index,
		frame_iteration, frame_count;
	int saved_elapsed, saved_sim_steps_per_second, saved_game_time;
} g_sim;

void xvt_flight_sim_reset(void)
{
	memset(&g_sim, 0, sizeof(g_sim));
	xvt_flight_prediction_reset();
}

int xvt_flight_sim_is_paused(void) { return g_sim.paused; }

int xvt_flight_sim_resume(void)
{
	if (!g_sim.paused) {
		return 1;
	}
	if (flight_input_read(-2) != FLIGHT_KEY_ALT_P) {
		return 0;
	}
	xvt_flight_controls_recover();
	time_consume_elapsed_ticks();
	msg_emit_in_flight_message(IFMSG_002_MISSION_RESUMED, g_sim.player);
	g_action_key = 0;
	g_flight_display_rebuild_pending = 0;
	flight_reset_unused_resume_slots();
	g_sim.paused = 0;
	return 1;
}

/* Runs one player's update for this tick, in order:
 * 1. unless the update is resuming after a pause: return at once if the mission is ending; repair a
 *    camera focus whose object is gone; read the input; for the local player, act on a function key
 *    (Alt-P pauses, leaves the update pending and returns 0);
 * 2. stop when the dormant-region flag is set; for a player awaiting a new craft, rebind or retire a
 *    destroyed craft, then stop;
 * 3. outside hyperspace, fire while the fire modifier is held, and pick a target when the target
 *    modifier is tapped;
 * 4. process the player's actions or chat input, update flight controls and camera, and apply a
 *    recorded throttle unless the player changed craft during the tick.
 * It stays one function: each part is short and runs in this order on the same player record. */
int xvt_flight_sim_update_player_step(int player_idx)
{
	enum {
		PALETTED_BYTES_PER_PIXEL = 1,
		BRIGHTNESS_STEP_Q8 = 0x40,
		BRIGHTNESS_MIN_Q8 = 0x100,
		BRIGHTNESS_LIMIT_Q8 = 0x300,
		GRAPHICS_DETAIL_PRESET_COUNT = 4,
		GRAPHICS_DETAIL_MESSAGE_BASE = 102,
		FIRE_MODIFIER_MASK = 0xD,
		FIRE_MODIFIER = 1,
		TARGET_MODIFIER_MASK = 0xE,
		TARGET_MODIFIER = 2,
		TARGET_TAP_MAX_TICKS = 59,
		FLIGHT_INPUT_WAIT_FOR_ANY_KEY = -2,
	};

	int object_index;
	uint16_t saved_key_mods;
	uint16_t *key_mods_hold_timer;
	int16_t new_target_object_index;

	if (!g_sim.player_step_pending) {
		if (g_flight_mission_state.mission_end_pending == 1) {
			return 1;
		}

		if (g_flight_sim_side_effects_suppressed == 0) {
			if (g_players[player_idx]
					    .view_state.camera_focus_obj_idx !=
				    UINT16_MAX &&
			    g_object_table[g_players[player_idx]
						   .view_state
						   .camera_focus_obj_idx]
					    .object_type == 0) {
				if (g_players[player_idx].map_camera_state !=
				    0) {
					g_players[player_idx]
						.view_state
						.camera_focus_obj_idx =
						UINT16_MAX;
				} else {
					g_players[player_idx]
						.view_state
						.target_camera_active = 0;
					g_players[player_idx]
						.view_state
						.external_camera_active = 0;
					g_players[player_idx]
						.view_state
						.player_input_blocked = 0;
					g_players[player_idx]
						.view_state
						.camera_focus_obj_idx =
						(uint16_t)g_players[player_idx]
							.object_index;
					hud_set_hud_view_state(HUD_VIEW_FORWARD,
							       player_idx);
					g_players[player_idx]
						.view_state.hud_aim_x = 0;
					g_players[player_idx]
						.view_state.hud_aim_y = 0;
				}
			}
			if (g_players[player_idx].map_camera_state != 0 &&
			    g_players[player_idx].view_state.aim_target_idx !=
				    UINT16_MAX &&
			    g_object_table[g_players[player_idx]
						   .view_state.aim_target_idx]
					    .object_type == 0) {
				g_players[player_idx]
					.view_state.aim_target_idx = UINT16_MAX;
			}
		}

		flight_input_read(player_idx);
		flight_input_latch_flight_controls();
		if (player_idx == g_local_player) {
			if (g_flight_sim_side_effects_suppressed == 0) {
				switch (g_current_action_key) {
				case FLIGHT_KEY_SHIFT_L:
					if (g_radio_message_backup_enabled !=
					    0) {
						g_radio_message_backup_enabled =
							0;
						msg_write_message_log_file();
						msg_emit_in_flight_message(
							IFMSG_402_RADIO_MESSAGE_BACKUP_TURNED_OFF,
							player_idx);
					} else {
						g_radio_message_backup_enabled =
							1;
						msg_emit_in_flight_message(
							IFMSG_403_RADIO_MESSAGE_BACKUP_TURNED_ON,
							player_idx);
					}
					break;
				case FLIGHT_KEY_ALT_B:
					if (g_flight_bytes_per_pixel ==
					    PALETTED_BYTES_PER_PIXEL) {
						g_flight_brightness_scale_q8 +=
							BRIGHTNESS_STEP_Q8;
						if (g_flight_brightness_scale_q8 ==
						    BRIGHTNESS_LIMIT_Q8) {
							g_flight_brightness_scale_q8 =
								BRIGHTNESS_MIN_Q8;
						}
						g_flight_reset_palette_fn();
						g_msg_arg_table[0] =
							(uint16_t)(((unsigned int)(g_flight_brightness_scale_q8 -
										   BRIGHTNESS_MIN_Q8) >>
								    6) +
								   1);
						msg_emit_in_flight_message(
							IFMSG_287_BRIGHTNESS_SET_TO_LEVEL_ARG,
							player_idx);
						fsfx_play_sound(
							FLIGHT_SOUND_SMALL_CLICK,
							-1, player_idx);
					}
					break;
				case FLIGHT_KEY_ALT_D:
					++g_flight_graphics_detail_preset;
					if (g_flight_graphics_detail_preset >=
					    GRAPHICS_DETAIL_PRESET_COUNT) {
						g_flight_graphics_detail_preset =
							0;
					}
					flight_apply_graphics_detail_preset(
						g_flight_graphics_detail_preset);
					msg_emit_in_flight_message(
						(in_flight_message_id)(g_flight_graphics_detail_preset +
								       GRAPHICS_DETAIL_MESSAGE_BASE),
						player_idx);
					fsfx_play_sound(
						FLIGHT_SOUND_SMALL_CLICK, -1,
						player_idx);
					break;
				case FLIGHT_KEY_ALT_I:
					g_sw3d_skip_odd_scanlines =
						g_sw3d_skip_odd_scanlines == 0;
					break;
				case FLIGHT_KEY_ALT_M:
					if (g_flight_alt_m_toggle != 0) {
						g_flight_alt_m_toggle = 0;
					} else {
						g_flight_alt_m_toggle = 1;
					}
					break;
				case FLIGHT_KEY_ALT_P:
					if (g_flight_player_count == 1 &&
					    !xvt_port_network_requires_progress()) {
						xvt_flight_controls_recover();
						g_input_timestamp +=
							time_consume_elapsed_ticks();
						fsfx_play_sound(
							FLIGHT_SOUND_SMALL_CLICK,
							-1, player_idx);
						/* Publish the pause text over the retained HD flight view. */
						xvt_render_capture_begin_overlay();
						msg_emit_in_flight_message(
							IFMSG_001_MISSION_PAUSED_PRESS_ANY_KEY_TO_CONTINUE,
							player_idx);
						g_flight_draw_to_hud_layer = 0;
						flight_surface_lock();
						hud_blit_software_hud_text_panes();
						flight_surface_unlock();
						flight_display_flip();
						xvt_render_capture_end_overlay();
						g_flight_draw_to_hud_layer = 1;
						sound_stop_all_instances();
						g_replay_inputs[player_idx]
							.flags = 0;
						g_replay_inputs[player_idx]
							.throttle = 0;
						g_sim.paused = 1;
						g_sim.player_step_pending = 1;
						g_sim.player = player_idx;
						return 0;
					}
					break;
				case FLIGHT_KEY_ALT_S:
					if (g_system_message_display_enabled !=
					    0) {
						g_system_message_display_enabled =
							0;
						msg_emit_in_flight_message(
							IFMSG_400_SYSTEM_MESSAGE_DISPLAYING_TURNED_OFF,
							player_idx);
					} else {
						g_system_message_display_enabled =
							1;
						msg_emit_in_flight_message(
							IFMSG_401_SYSTEM_MESSAGE_DISPLAYING_TURNED_ON,
							player_idx);
					}
					break;
				case FLIGHT_KEY_ALT_V:
					fsfx_play_sound(
						FLIGHT_SOUND_SMALL_CLICK, -1,
						player_idx);
					msg_emit_in_flight_message(
						IFMSG_000_X_WING_VS_TIE_FIGHTER_VER_1_10_05_11_97,
						player_idx);
					break;
				case FLIGHT_KEY_SCREENSHOT:
					fsfx_play_sound(
						FLIGHT_SOUND_SMALL_CLICK, -1,
						player_idx);
					flight_screenshot_capture();
					break;
				default:
					break;
				}
			}
		}
	}
	g_sim.player_step_pending = 0;
	if (g_flight_runtime_state_initialized > 1 &&
	    g_dormant_flight_region_session_early_return_flag != 0) {
		return 1;
	}

	if (g_players[player_idx].awaiting_new_craft != 0) {
		if (g_flight_sim_side_effects_suppressed == 0) {
			object_index = g_players[player_idx].object_index;
			if (object_index != -1 &&
			    g_object_table[object_index].object_type == 0) {
				mission_process_flight_group_wave_completion(
					g_players[player_idx]
						.bound_flight_group_idx);
				if (player_bind_to_available_craft(player_idx,
								   UINT32_MAX,
								   0, 0) != 0) {
					player_end_flight_participation(
						player_idx);
					player_emit_remote_player_departed_messages(
						player_idx);
				} else if (player_idx == g_local_player) {
					msg_emit_local_player_craft_message(
						IFMSG_290_PREVIOUS_CRAFT_DESTROYED_NOW_PILOTING_ARG_ARG_ARG);
				}
			}
		}
		return 1;
	}

	if (g_players[player_idx].hyperspace_phase == 0) {
		if ((g_flight_key_mods & FIRE_MODIFIER_MASK) == FIRE_MODIFIER &&
		    g_players[player_idx].view_state.player_input_blocked ==
			    0 &&
		    g_players[player_idx].map_camera_state == 0) {
			laser_fireplayerweapon(player_idx);
		}

		saved_key_mods = g_players[player_idx].saved_key_mods &
				 TARGET_MODIFIER_MASK;
		if ((g_flight_key_mods & TARGET_MODIFIER_MASK) ==
		    TARGET_MODIFIER) {
			key_mods_hold_timer =
				&g_players[player_idx].key_mods_hold_timer;
			if (saved_key_mods == TARGET_MODIFIER) {
				*key_mods_hold_timer += g_elapsed_ticks;
			} else {
				*key_mods_hold_timer = g_elapsed_ticks;
			}
			g_players[player_idx].saved_key_mods =
				g_flight_key_mods;
			if (*key_mods_hold_timer < TARGET_TAP_MAX_TICKS) {
				g_flight_key_mods &= (uint16_t)~TARGET_MODIFIER;
			}
		} else {
			if (saved_key_mods == TARGET_MODIFIER &&
			    g_players[player_idx].key_mods_hold_timer <
				    TARGET_TAP_MAX_TICKS) {
				if (g_players[player_idx].map_camera_state !=
				    0) {
					new_target_object_index =
						flight_map_pick_object_nearest_screen_center(
							player_idx);
					if (new_target_object_index != -1) {
						player_set_target(
							new_target_object_index,
							player_idx);
					}
				} else if (
					g_flight_mission_state
							.proving_grounds_mode_active ==
						0 &&
					g_players[player_idx]
							.view_state
							.player_input_blocked ==
						0) {
					new_target_object_index =
						player_pick_target_in_sight(
							player_idx);
					if (new_target_object_index != -1) {
						player_set_target(
							new_target_object_index,
							player_idx);
					}
				}
			}
			g_players[player_idx].saved_key_mods =
				g_flight_key_mods;
			g_players[player_idx].key_mods_hold_timer = 0;
		}
	}

	bool throttle_eligible =
		xvt_flight_controls_throttle_eligible((unsigned)player_idx);
	int throttle_object = g_players[player_idx].object_index;
	unsigned throttle_signature =
		g_players[player_idx].bound_object_signature;
	if (g_players[player_idx].chat_recipient_mode ==
	    FLIGHT_CHAT_RECIPIENT_INACTIVE) {
		flight_process_player_actions(player_idx);
	} else {
		flight_chat_handle_input(player_idx);
	}
	if (g_players[player_idx].participation_state != 0) {
		player_update_flight_controls_and_camera(player_idx);
	}
	/* Recorded throttle wins over same-tick key/modifier adjustments, never across a craft transition. */
	if (throttle_eligible &&
	    throttle_object == g_players[player_idx].object_index &&
	    throttle_signature ==
		    g_players[player_idx].bound_object_signature) {
		xvt_flight_controls_apply_throttle(
			(unsigned)player_idx, &g_replay_inputs[player_idx]);
	}
	return 1;
}

/* Replays each connected player's input frames up to target_game_time, or resumes a replay that a
 * pause interrupted. For each frame that is due:
 * 1. if the game clock has reached the frame, put the player's craft back to its last lockstep pose
 *    (or, when the player changed craft, reschedule the frame);
 * 2. step the player's craft alone to the frame's time, saving its pose and checkpoint;
 * 3. set the tick length for the frame, load its input into the player's replay slot, and run the
 *    player's update;
 * 4. on a confirmed network frame, confirm the prediction; restore the saved clock and tick length.
 * A pause inside the update saves the replay position and returns 0; otherwise returns 1. It stays
 * one function: the steps share the frame, the saved clock values and the replay position. */
int xvt_flight_sim_advance(int target_game_time)
{
	enum {
		PLAYER_COUNT = sizeof(g_input_frame_count) /
			       sizeof(g_input_frame_count[0]),
		MINIMUM_REPLAY_TICKS = 4,
	};

	int suppress_side_effects;
	int player_idx;
	int saved_elapsed_ticks;
	int saved_sim_steps_per_second;

	if (g_sim.replay_pending) {
		target_game_time = g_sim.advance_target;
	} else {
		g_sim.advance_target = target_game_time;
	}
	suppress_side_effects = !xvt_flight_timing_is_network125()
					? 1
					: g_flight_sim_side_effects_suppressed;
	for (player_idx = g_sim.replay_pending ? g_sim.player : 0;
	     player_idx < PLAYER_COUNT; ++player_idx) {
		struct input_frame *frame;
		int frame_iteration;
		int frame_count;

		if (g_players[player_idx].participation_state == 0) {
			continue;
		}

		frame_count = g_sim.replay_pending
				      ? g_sim.frame_count
				      : g_input_frame_count[player_idx];
		frame = &g_input_history[player_idx][g_sim.replay_pending
							     ? g_sim.frame_index
							     : 0];
		for (frame_iteration =
			     g_sim.replay_pending ? g_sim.frame_iteration : 0;
		     frame_iteration < frame_count;
		     ++frame_iteration, ++frame) {
			int saved_game_time;
			uint8_t participation_state;

			if (!g_sim.replay_pending) {
				if (!((suppress_side_effects != 0 &&
				       xvt_flight_timing_is_network125()) ||
				      frame->timestamp >
					      g_players[player_idx]
						      .lockstep_timestamp ||
				      frame->awaiting_relay != 0)) {
					flight_sync_remove_input_history_frame(
						player_idx, frame);
					--frame;
					continue;
				}
				if (frame->timestamp > target_game_time ||
				    (suppress_side_effects == 0 &&
				     frame->input_source !=
					     XVT_INPUT_AUTHORITATIVE)) {
					continue;
				}
				if (g_players[player_idx].lockstep_timestamp >=
				    frame->timestamp) {
					continue;
				}

				saved_game_time = g_game_time;
				if (g_game_time >= frame->timestamp &&
				    g_players[player_idx].object_index != -1) {
					struct object_record *object;
					struct mobile_object *mobile_object;

					object =
						&g_object_table
							[g_players[player_idx]
								 .object_index];
					mobile_object = object->mobj;
					if (mobile_object == NULL ||
					    mobile_object->p_craft == NULL) {
						continue;
					}
					if (g_players[player_idx]
							    .saved_object_signature ==
						    object->object_signature &&
					    g_players[player_idx]
							    .saved_awaiting_new_craft ==
						    g_players[player_idx]
							    .awaiting_new_craft) {
						if (mobile_object
							    ->sim_state_timestamp >
						    g_players[player_idx]
							    .lockstep_timestamp) {
							xvt_flight_checkpoint_restore_player(
								player_idx);
							mobile_object
								->sim_state_timestamp =
								g_players[player_idx]
									.lockstep_timestamp;
							g_object_table
								[g_players[player_idx]
									 .object_index]
									.world_x =
								g_players[player_idx]
									.saved_x;
							g_object_table
								[g_players[player_idx]
									 .object_index]
									.world_y =
								g_players[player_idx]
									.saved_y;
							g_object_table
								[g_players[player_idx]
									 .object_index]
									.world_z =
								g_players[player_idx]
									.saved_z;
							g_object_table
								[g_players[player_idx]
									 .object_index]
									.roll =
								g_players[player_idx]
									.saved_roll;
							g_object_table
								[g_players[player_idx]
									 .object_index]
									.pitch =
								g_players[player_idx]
									.saved_pitch;
							g_object_table
								[g_players[player_idx]
									 .object_index]
									.yaw =
								g_players[player_idx]
									.saved_yaw;
							g_object_table
								[g_players[player_idx]
									 .object_index]
									.mobj
									->lifetime_timer =
								g_players[player_idx]
									.saved_lifetime_timer;
							g_object_table
								[g_players[player_idx]
									 .object_index]
									.mobj
									->roll_impulse_rate =
								g_players[player_idx]
									.saved_roll_impulse_rate;
							g_object_table
								[g_players[player_idx]
									 .object_index]
									.mobj
									->speed =
								g_players[player_idx]
									.saved_speed;
							g_object_table
								[g_players[player_idx]
									 .object_index]
									.mobj
									->speed_remainder =
								g_players[player_idx]
									.saved_speed_remainder;
							g_object_table
								[g_players[player_idx]
									 .object_index]
									.mobj
									->p_craft
									->pitch =
								g_players[player_idx]
									.saved_pitch;
							g_object_table
								[g_players[player_idx]
									 .object_index]
									.mobj
									->orient_matrix_dirty =
								1;
							g_object_table
								[g_players[player_idx]
									 .object_index]
									.mobj
									->move_vector_dirty =
								1;
						}
						if (g_game_time >
						    g_object_table[g_players[player_idx]
									   .object_index]
							    .mobj
							    ->sim_state_timestamp) {
							g_game_time =
								g_object_table
									[g_players[player_idx]
										 .object_index]
										.mobj
										->sim_state_timestamp;
						}
					} else {
						if (suppress_side_effects !=
						    0) {
							continue;
						}
						xvt_flight_checkpoint_invalidate_player(
							player_idx);
						frame->timestamp =
							xvt_flight_timing_is_network125()
								? (g_game_time &
								   ~1) + 2
								: g_game_time +
									  MINIMUM_REPLAY_TICKS;
						g_players[player_idx]
							.lockstep_timestamp =
							g_game_time;
					}
				}

				saved_elapsed_ticks = g_elapsed_ticks;
				saved_sim_steps_per_second =
					g_sim_steps_per_second;
				if (g_players[player_idx].object_index != -1) {
					g_single_object_update_override_idx =
						g_players[player_idx]
							.object_index;
					if (g_object_table
						    [g_single_object_update_override_idx]
							    .mobj != NULL) {
						struct object_record *object;

						g_elapsed_ticks =
							(uint16_t)(frame->timestamp -
								   g_game_time);
						if (g_elapsed_ticks == 0) {
							g_sim_steps_per_second =
								SIMULATION_TICKS_PER_SECOND;
						} else {
							g_sim_steps_per_second =
								(uint16_t)(SIMULATION_TICKS_PER_SECOND /
									   g_elapsed_ticks);
						}
						if (g_sim_steps_per_second ==
						    0) {
							g_sim_steps_per_second =
								1;
						}
						flight_update_craft_steering_and_speed();
						object_update_lifetime_and_movement();
						g_object_table[g_single_object_update_override_idx]
							.mobj
							->sim_state_timestamp =
							frame->timestamp;
						object =
							&g_object_table
								[g_single_object_update_override_idx];
						g_players[player_idx].saved_x =
							object->world_x;
						g_players[player_idx].saved_y =
							object->world_y;
						g_players[player_idx].saved_z =
							object->world_z;
						g_players[player_idx]
							.saved_roll =
							object->roll;
						g_players[player_idx]
							.saved_pitch =
							object->pitch;
						g_players[player_idx]
							.saved_yaw =
							object->yaw;
						g_players[player_idx]
							.saved_lifetime_timer =
							object->mobj
								->lifetime_timer;
						g_players[player_idx]
							.saved_roll_impulse_rate =
							object->mobj
								->roll_impulse_rate;
						g_players[player_idx]
							.saved_speed =
							object->mobj->speed;
						g_players[player_idx]
							.saved_speed_remainder =
							object->mobj
								->speed_remainder;
						g_players[player_idx]
							.saved_object_signature =
							object->object_signature;
						g_players[player_idx]
							.saved_awaiting_new_craft =
							g_players[player_idx]
								.awaiting_new_craft;
						xvt_flight_checkpoint_save_player(
							player_idx,
							frame->timestamp);
					}
					g_single_object_update_override_idx =
						-1;
				}

				g_elapsed_ticks =
					(uint16_t)(frame->timestamp -
						   g_players[player_idx]
							   .lockstep_timestamp);
				if (!xvt_flight_timing_is_unlocked() &&
				    g_elapsed_ticks < MINIMUM_REPLAY_TICKS) {
					g_elapsed_ticks = MINIMUM_REPLAY_TICKS;
				}
				if (g_elapsed_ticks == 0) {
					g_sim_steps_per_second =
						SIMULATION_TICKS_PER_SECOND;
				} else {
					g_sim_steps_per_second =
						(uint16_t)(SIMULATION_TICKS_PER_SECOND /
							   g_elapsed_ticks);
				}
				if (g_sim_steps_per_second == 0) {
					g_sim_steps_per_second = 1;
				}

				g_players[player_idx].lockstep_timestamp =
					frame->timestamp;
				g_replay_inputs[player_idx] = frame->input;
				if (suppress_side_effects != 0 &&
				    frame->timestamp <= saved_game_time) {
					g_replay_inputs[player_idx].key = 0;
					g_replay_inputs[player_idx].key_mods =
						0;
					g_replay_inputs[player_idx].flags = 0;
					g_replay_inputs[player_idx].throttle =
						0;
				}
				if (xvt_flight_timing_is_network125() &&
				    g_local_player == player_idx) {
					g_flight_sfx_side_effect_gate = 2;
					if (frame->timestamp >
					    g_last_local_replay_input_timestamp) {
						g_flight_sfx_side_effect_gate =
							1;
						g_last_local_replay_input_timestamp =
							frame->timestamp;
					}
				}
				g_sim.saved_elapsed = saved_elapsed_ticks;
				g_sim.saved_sim_steps_per_second =
					saved_sim_steps_per_second;
				g_sim.saved_game_time = saved_game_time;
			}
			if (!xvt_flight_sim_update_player_step(player_idx)) {
				g_sim.replay_pending = 1;
				g_sim.player = player_idx;
				g_sim.frame_index =
					(int)(frame -
					      g_input_history[player_idx]);
				g_sim.frame_iteration = frame_iteration;
				g_sim.frame_count = frame_count;
				return 0;
			}
			g_sim.replay_pending = 0;
			if (xvt_flight_timing_is_network125() &&
			    !suppress_side_effects &&
			    frame->input_source == XVT_INPUT_AUTHORITATIVE) {
				xvt_flight_prediction_confirm(player_idx,
							      frame->timestamp,
							      &frame->input);
			}
			saved_elapsed_ticks = g_sim.saved_elapsed;
			saved_sim_steps_per_second =
				g_sim.saved_sim_steps_per_second;
			saved_game_time = g_sim.saved_game_time;
			g_elapsed_ticks = (uint16_t)saved_elapsed_ticks;
			g_sim_steps_per_second =
				(uint16_t)saved_sim_steps_per_second;
			participation_state =
				g_players[player_idx].participation_state;
			g_game_time = saved_game_time;
			g_flight_sfx_side_effect_gate = 0;
			if (participation_state == 0) {
				break;
			}
		}
	}
	return 1;
}

static xvt_flight_step_result xvt_flight_sim_finished_advance(void)
{
	return !g_flight_sim_side_effects_suppressed &&
			       g_flight_mission_state.mission_end_pending
		       ? XVT_STEP_TERMINAL
		       : XVT_STEP_COMPLETE;
}

/* Runs whole simulation steps up to the target time. Each step calls the
 * recovered game's per-tick stages in their original order, with a stop
 * after each stage that can end the mission, so the order reads in one
 * place. */
xvt_flight_step_result xvt_flight_sim_step_to_time(int target_game_time)
{
	enum { MINIMUM_SIM_STEP_TICKS = 1, MINIMUM_SIM_STEPS_PER_SECOND = 1 };

	int game_time;

	if (!g_sim.step_pending) {
		g_sim.target = target_game_time;
		g_sim.step_game_time = g_game_time;
		g_gunner_collision_probe_count = 0;
	}
	target_game_time = g_sim.target;
	game_time = g_sim.step_game_time;
	if (g_sim.zero_step) {
		if (!xvt_flight_sim_advance(game_time +
					    (uint16_t)g_elapsed_ticks)) {
			return XVT_STEP_PENDING;
		}
		g_sim.zero_step = g_sim.step_pending = 0;
		return xvt_flight_sim_finished_advance();
	}
	for (;;) {
		if (!g_sim.step_pending) {
			g_elapsed_ticks =
				(uint16_t)(target_game_time - game_time);
			if (g_elapsed_ticks < MINIMUM_SIM_STEP_TICKS) {
				break;
			}
			if ((int)(uint16_t)g_elapsed_ticks >
			    xvt_flight_timing_maximum_step_ticks()) {
				g_elapsed_ticks = (uint16_t)
					xvt_flight_timing_maximum_step_ticks();
			}
			g_sim_steps_per_second =
				(uint16_t)(SIMULATION_TICKS_PER_SECOND /
					   (int)(uint16_t)g_elapsed_ticks);
			if (g_sim_steps_per_second == 0) {
				g_sim_steps_per_second =
					MINIMUM_SIM_STEPS_PER_SECOND;
			}
			g_game_time = game_time;
			g_sim.step_game_time = game_time;
			xvt_flight_timing_begin_advance(g_elapsed_ticks);
		}
		if (!xvt_flight_sim_advance(game_time +
					    (uint16_t)g_elapsed_ticks)) {
			g_sim.step_pending = 1;
			return XVT_STEP_PENDING;
		}
		g_sim.step_pending = 0;
		if (g_flight_sim_side_effects_suppressed == 0 &&
		    g_flight_mission_state.mission_end_pending == 1) {
			xvt_flight_timing_end_advance();
			return XVT_STEP_TERMINAL;
		}

		if (g_flight_mission_state.proving_grounds_mode_active == 0 &&
		    xvt_flight_timing_reference_due()) {
			struct xvt_flight_clock clock =
				xvt_flight_timing_enter_reference();
			mission_update_flight_group_arrivals();
			pai_update_all_craft_ai();
			xvt_flight_timing_restore_clock(clock);
		}
		flight_update_timers();
		laser_weaponsfire();
		xvt_reference_motion_commit_boundary();
		flight_update_craft_steering_and_speed();
		if (g_debris_enabled != 0 &&
		    g_flight_mission_state.proving_grounds_mode_active == 0 &&
		    xvt_flight_timing_reference_due()) {
			flight_object_recycle_local_debris_near_player();
		}
		collide_collisions();
		if (g_flight_sim_side_effects_suppressed == 0 &&
		    g_flight_mission_state.mission_end_pending == 1) {
			xvt_flight_timing_end_advance();
			return XVT_STEP_TERMINAL;
		}

		object_update_lifetime_and_movement();
		flight_object_update_special_behavior();
		if (g_flight_sim_side_effects_suppressed == 0 &&
		    g_flight_mission_state.mission_end_pending == 1) {
			xvt_flight_timing_end_advance();
			return XVT_STEP_TERMINAL;
		}

		player_validate_all_current_targets();
		player_update_participation_state();
		if (g_flight_sim_side_effects_suppressed == 0 &&
		    g_flight_mission_state.mission_end_pending == 1) {
			xvt_flight_timing_end_advance();
			return XVT_STEP_TERMINAL;
		}

		if (xvt_flight_timing_reference_due() ||
		    g_flight_mission_state.mission_end_pending) {
			mission_update_logic();
		}
		hud_update_flight_message_panes();
		flight_update_dynamic_music_state();
		if (g_fsfx_loaded != 0) {
			fsfx_update_voice_queue();
			fsfx_update_flight_sfx();
		}
		game_time = g_game_time;
		game_time += (uint16_t)g_elapsed_ticks;
		g_game_time = game_time;
		xvt_flight_timing_end_advance();
		if (game_time >= target_game_time) {
			return xvt_flight_sim_finished_advance();
		}
	}

	g_game_time = game_time;
	xvt_flight_timing_end_advance();
	if (!xvt_flight_sim_advance(game_time + (uint16_t)g_elapsed_ticks)) {
		g_sim.step_game_time = game_time;
		g_sim.zero_step = g_sim.step_pending = 1;
		return XVT_STEP_PENDING;
	}
	return xvt_flight_sim_finished_advance();
}

void xvt_flight_history_restore_checkpoint(void)
{
	for (unsigned player = 0; player < XVT_FLIGHT_PLAYERS; ++player) {
		int retained = 0;
		for (int i = 0; i < g_input_frame_count[player]; ++i) {
			const struct input_frame *frame =
				&g_input_history[player][i];
			if (!g_players[player].participation_state ||
			    frame->input_source == XVT_INPUT_PREDICTED ||
			    frame->timestamp <=
				    g_players[player].lockstep_timestamp) {
				continue;
			}
			g_input_history[player][retained++] = *frame;
		}
		g_input_frame_count[player] = retained;
	}
}

xvt_input_insert_status
xvt_flight_history_insert(unsigned player, int tick,
			  const struct flight_input_frame_record *input,
			  struct input_frame **out)
{
	*out = NULL;
	if (player >= XVT_FLIGHT_PLAYERS || tick <= 0 || !input ||
	    (input->flags & ~XVT_INPUT_THROTTLE_PRESENT) ||
	    (!(input->flags & XVT_INPUT_THROTTLE_PRESENT) && input->throttle)) {
		return XVT_INPUT_INVALID;
	}
	int count = g_input_frame_count[player], index = 0;
	if (count < 0 || count > XVT_INPUT_HISTORY_CAPACITY) {
		return XVT_INPUT_INVALID;
	}
	struct input_frame *frames = g_input_history[player];
	while (index < count && frames[index].timestamp < tick) {
		++index;
	}
	if (index < count && frames[index].timestamp == tick) {
		if (frames[index].input_source == XVT_INPUT_AUTHORITATIVE ||
		    frames[index].awaiting_relay == 1) {
			return XVT_INPUT_DUPLICATE;
		}
	} else {
		if (count == XVT_INPUT_HISTORY_CAPACITY) {
			return XVT_INPUT_FULL;
		}
		memmove(frames + index + 1, frames + index,
			(count - index) * sizeof *frames);
		++g_input_frame_count[player];
	}
	struct input_frame *frame = frames + index;
	frame->timestamp = tick;
	frame->input_source = XVT_INPUT_REAL;
	frame->awaiting_relay = 0;
	frame->input = *input;
	*out = frame;
	return XVT_INPUT_INSERTED;
}

xvt_input_insert_status
xvt_flight_history_insert_real(unsigned player, int tick,
			       const struct flight_input_frame_record *input,
			       int authoritative)
{
	struct input_frame *frame;
	xvt_input_insert_status status =
		xvt_flight_history_insert(player, tick, input, &frame);
	if (status == XVT_INPUT_FULL) {
		flight_sync_discard_predicted_input_frames(player);
		status = xvt_flight_history_insert(player, tick, input, &frame);
	}
	if (status == XVT_INPUT_FULL || status == XVT_INPUT_INVALID) {
		return status;
	}
	if (frame) {
		frame->input_source = authoritative ? XVT_INPUT_AUTHORITATIVE
						    : XVT_INPUT_REAL;
		frame->awaiting_relay =
			!authoritative && net_session_is_local_host();
	} else if (authoritative) {
		for (int i = 0; i < g_input_frame_count[player]; ++i) {
			struct input_frame *old = &g_input_history[player][i];
			if (old->timestamp != tick) {
				continue;
			}
			if (old->input.key != input->key ||
			    old->input.key_mods != input->key_mods ||
			    old->input.axis_x != input->axis_x ||
			    old->input.axis_y != input->axis_y ||
			    old->input.axis_r != input->axis_r ||
			    old->input.flags != input->flags ||
			    old->input.throttle != input->throttle) {
				XVT_LOG_ERROR(
					"network.input_conflict player=%u tick=%d",
					player, tick);
				return XVT_INPUT_CONFLICT;
			}
			old->input_source = old->awaiting_relay = 0;
		}
	}
	return status;
}

void xvt_flight_history_recover(void)
{
	xvt_flight_prediction_reset();
	for (unsigned player = 0; player < XVT_FLIGHT_PLAYERS; ++player) {
		int retained = 0;
		for (int i = 0; i < g_input_frame_count[player]; ++i) {
			const struct input_frame *frame =
				&g_input_history[player][i];
			/* Host records will reconstruct peer input; preserve only future local samples. */
			if (player != (unsigned)g_local_player ||
			    !g_players[player].participation_state ||
			    frame->timestamp <= g_game_time ||
			    frame->input_source == XVT_INPUT_PREDICTED) {
				continue;
			}
			g_input_history[player][retained++] = *frame;
		}
		g_input_frame_count[player] = retained;
	}
}
