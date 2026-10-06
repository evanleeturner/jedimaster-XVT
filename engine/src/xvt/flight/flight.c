#include "xvt/flight/flight.h"

#include "xvt_runtime/timing/flight_integration.h"
#include "xvt_runtime/timing/flight_timing.h"
#include "xvt_runtime/runtime/flight_sim.h"

#include "xvt_runtime/runtime/port.h"
#include "xvt_runtime/snapshot/world_state.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xvt/assets/file.h"
#include "xvt/assets/model_mesh.h"
#include "xvt/assets/model_preview.h"
#include "xvt/assets/object_type.h"
#include "xvt/assets/opt_model.h"
#include "xvt/audio/fsfx.h"
#include "xvt/audio/music_cd.h"
#include "xvt/audio/sound.h"
#include "xvt/flight/ai/pai.h"
#include "xvt/flight/ai/paifight.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/fediskio.h"
#include "xvt/flight/flight_display.h"
#include "xvt/flight/flight_input.h"
#include "xvt/flight/flight_loading.h"
#include "xvt/flight/flight_object.h"
#include "xvt/flight/flight_render.h"
#include "xvt/flight/flight_surface.h"
#include "xvt/flight/flight_view.h"
#include "xvt/flight/fview.h"
#include "xvt/flight/hud/flight_alert.h"
#include "xvt/flight/hud/flight_map.h"
#include "xvt/flight/hud/hud.h"
#include "xvt/flight/hud/mfd.h"
#include "xvt/flight/hud/msg.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/collide.h"
#include "xvt/flight/object/damage.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/flight_player.h"
#include "xvt/flight/player/player.h"
#include "xvt/flight/proving_grounds.h"
#include "xvt/flight/transfm2.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/pilot.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt/input/dinput.h"
#include "xvt/math/math.h"
#include "xvt/math/math2.h"
#include "xvt/math/trig2.h"
#include "xvt/net/flight_net.h"
#include "xvt/net/flight_sync.h"
#include "xvt/net/net_reliable.h"
#include "xvt/net/net_session.h"
#include "xvt/render/color.h"
#include "xvt/render/flight_palette.h"
#include "xvt/render/flight_sw.h"
#include "xvt/render/render_scene.h"
#include "xvt/render/renderer.h"
#include "xvt/render/std3d.h"
#include "xvt/render/sw3d.h"
#include "xvt/util/debug_console.h"
#include "xvt/util/game_rand.h"
#include "xvt/util/memory.h"
#include "xvt/util/time.h"
#include "xvt_runtime/log/log_both_builds.h"

/* Per graphics detail preset 0 to 3, the value flight_apply_graphics_detail_preset
 * gives g_graphics_detail_distance_threshold: 0x1000, 0x2000, 0x4000, 0x7FFF. */
// GLOBAL: XVT 0x527348
const uint16_t g_graphics_detail_distance_threshold_by_preset[4] = {
	0x1000, 0x2000, 0x4000, 0x7FFF};
/* Per graphics detail preset, the value flight_apply_graphics_detail_preset gives
 * g_star_grid_divisor: 2 at preset 0, else 1. */
// GLOBAL: XVT 0x527358
const uint16_t g_star_grid_divisor_by_graphics_detail_preset[4] = {2, 1, 1, 1};
/* Per graphics detail preset, the value flight_apply_graphics_detail_preset gives
 * g_backdrops_enabled: 1 only at preset 3. */
// GLOBAL: XVT 0x527360
const uint16_t g_backdrops_enabled_by_graphics_detail_preset[4] = {0, 0, 0, 1};
/* Per graphics detail preset, the value flight_apply_graphics_detail_preset gives
 * g_debris_enabled: 1 at presets 2 and 3. */
// GLOBAL: XVT 0x527368
const uint16_t g_debris_enabled_by_graphics_detail_preset[4] = {0, 0, 1, 1};
/* Resolution mode to go back to when the flight ends. Nothing writes it, so it
 * stays FLIGHT_RESOLUTION_320X240; flight_main_loop and
 * xvt_flight_task_release_mission hand it to
 * flight_display_apply_resolution_mode_stub, which does nothing, when it differs
 * from g_flight_resolution_mode. */
// GLOBAL: XVT 0x5233F0
int g_pre_flight_resolution_mode = FLIGHT_RESOLUTION_320X240;
/* 20.0: the cap on the level-of-detail option plus 5 that g_lod_distance_scale is
 * worked out from (flight_main, xvt_flight_entry_configure_lod_distance). Never
 * written. */
// GLOBAL: XVT 0x518218
const float g_lod_config_max_value = 20.0f;
/* 0.04: multiplies the level-of-detail option plus 5 on the way to
 * g_lod_distance_scale. Never written. */
// GLOBAL: XVT 0x51821C
const float g_lod_config_scale_factor = 0.04f;
/* 2.0: the level-of-detail and mipmap values are doubled, and above
 * g_lod_config_curve_threshold become 1 over 2 less the value, before
 * g_lod_distance_scale and g_mip_lod_scale take 1 over the result. Never
 * written. */
// GLOBAL: XVT 0x518220
const float g_lod_config_curve_double = 2.0f;
/* 1.0: the value above which the level-of-detail and mipmap curves bend (see
 * g_lod_config_curve_double), and the numerator of the bend. Never written. */
// GLOBAL: XVT 0x518224
const float g_lod_config_curve_threshold = 1.0f;
/* 1/19: multiplies the mipmap option on the way to g_mip_lod_scale; an option of
 * 19 turns mipmapping off instead (flight_main,
 * xvt_flight_entry_configure_mipmaps). Never written. */
// GLOBAL: XVT 0x518228
const float g_mipmap_config_scale_factor = 0.052631579f;
/* Minutes into the flight music track (track 2) at which a flight's music may
 * start; flight start picks one of the four at random (game_rand2), with the
 * second from g_dynamic_music_initial_start_second_choices (flight_main_loop,
 * xvt_flight_loading_runtime). Never written. */
// GLOBAL: XVT 0x523644
uint8_t g_dynamic_music_initial_start_minute_choices[4] = {0, 4, 8, 12};
/* Seconds added to the start minute picked from
 * g_dynamic_music_initial_start_minute_choices, same index. Never written. */
// GLOBAL: XVT 0x523648
uint8_t g_dynamic_music_initial_start_second_choices[4] = {0, 1, 40, 52};
/* "paiplan": the name flight start passes to pai_loadplans to load the AI plans
 * (flight_main_loop, xvt_flight_loading_globals). */
// GLOBAL: XVT 0x5236BC
const char g_pai_plan_resource_base_name[8] = "paiplan";
/* 1 when the launch command line holds "traincourse": flight start then sets
 * the proving grounds craft type to 2 and level to 4 before the mission loads
 * (flight_main_loop, xvt_flight_loading_mission_setup). Set by flight_main in the
 * original build and xvt_flight_entry_read_launch_switches in the modern one. */
// GLOBAL: XVT 0x527E98
int g_flight_conf_train_course = 0;
/* Set to 1 when the launch command line starts with "/+" (flight_main,
 * xvt_flight_entry_read_launch_switches); nothing reads it. */
// GLOBAL: XVT 0x527E9C
int g_unused_flight_cmd_line_plus_switch_flag = 0;
/* 1 when the launch command line holds "nolauncher" (flight_main,
 * xvt_flight_entry_read_launch_switches); nothing reads it. */
// GLOBAL: XVT 0x527EB0
int g_flight_conf_no_launcher = 0;
/* Per keypad look key 1 to 9 (indexed by the key less keypad 1), the HUD view
 * state flight_process_player_actions shows, added to the view's
 * hud_aim_x_snap_state; a sum of 0 means the player's saved view. Entry 4, keypad
 * 5, is not used: that key has its own case. */
// GLOBAL: XVT 0x5272D1
static const uint8_t g_hud_view_state_offset_by_look_action[9] = {
	3, 4, 5, 2, 16, 6, 1, 0, 7};
/* Per keypad look key 1 to 9, the hud_aim_y flight_process_player_actions gives the
 * view: the look direction's turn (a full circle is 65,536). Entry 4 is not
 * used. */
// GLOBAL: XVT 0x5272E2
static const int16_t g_hud_aim_y_by_look_action[9] = {
	(int16_t)0xA000, (int16_t)0x8000, 0x6000, (int16_t)0xC000, 0,
	0x4000,		 (int16_t)0xE000, 0,	  0x2000,
};
/* Divisor of the starfield grid (flight_starfield_render): 4, 2 or 1 from the
 * star density option at launch (flight_main, xvt_flight_entry_configure), then
 * from the graphics detail preset (flight_apply_graphics_detail_preset). Starts at
 * 1. */
// GLOBAL: XVT 0x5233FC
uint16_t g_star_grid_divisor = 1;
/* 1 while the simulation runs ahead on predicted input in multiplayer, 0 while
 * it runs on confirmed input (a world message from the server, or solo play);
 * many functions read it to hold back effects that must happen once, such as
 * messages, sounds and the end of the mission. Seven functions write it:
 * flight_main_loop, flight_run_mission_loop and flight_sync_apply_world_message_packet
 * in the original build; xvt_flight_loading_globals, xvt_flight_frame_start_advance,
 * xvt_flight_frame_confirm and xvt_flight_frame_network_update in the modern one. */
// GLOBAL: XVT 0x523410
int g_flight_sim_side_effects_suppressed = 0;
/* Which simulation pass fsfx_play_sound lets play: 0, only a pass with side
 * effects on; 1, only a pass with them suppressed; 2, none. In multiplayer,
 * flight_advance_one_step sets 1 while it applies a local input frame for the
 * first time and 2 when it applies one again, and flight_run_mission_loop sets 1
 * while it draws the frame; both set 0 after. Five functions write it: those
 * two, and xvt_flight_sim_advance, xvt_flight_frame_begin and xvt_flight_frame_render
 * in the modern build. */
// GLOBAL: XVT 0x523414
int g_flight_sfx_side_effect_gate = 0;
/* 1 from a world checksum, taken when a world message asks for one, until every
 * player's checksum has matched or a resync replays the world messages; while
 * it is 1 a client keeps the server's world messages for that replay. Many
 * functions write it, chiefly flight_sync_apply_world_message_packet (1), the
 * checksum and resync handlers (0) and the modern build's frame and resync
 * code; flight start sets 0. */
// GLOBAL: XVT 0x52341C
int g_flight_net_buffer_world_messages_until_checksum = 0;
/* g_server_tick_time at the last world checksum; resync packets from another
 * epoch are dropped. Six functions write it: flight_main_loop (0 at flight
 * start) and flight_sync_apply_world_message_packet in the original build;
 * xvt_flight_loading_globals, xvt_flight_task_start_world, xvt_flight_frame_checksum
 * and xvt_resync_full_apply in the modern one. */
// GLOBAL: XVT 0x523420
unsigned int g_flight_net_world_checksum_epoch = 0;
/* 1 for internet play: the input clock runs 130 ticks ahead of the server
 * instead of 30, remote craft are drawn smoothed, and no predicted remote input
 * is queued. Copied from g_game_config.internet_play at launch (flight_main,
 * xvt_flight_entry_read_launch_switches), its only writers. */
// GLOBAL: XVT 0x523428
int g_internet_play_enabled = 0;
/* Set to -1 by flight_reset_unused_resume_slots, its only writer; nothing reads
 * it. */
// GLOBAL: XVT 0x51A84C
static int g_unused_flight_resume_reset_slot0;
/* Set to -1 by flight_reset_unused_resume_slots, its only writer; nothing reads
 * it. */
// GLOBAL: XVT 0x51A850
static int g_unused_flight_resume_reset_slot1;
/* Per subsystem id, its CRAFT_SUBSYSTEM_FLAG_ bit in a craft's
 * working_subsystems; the last two ids have none. flight_update_timers sets the
 * bit back when a repair ends. */
// GLOBAL: XVT 0x51BF60
const uint16_t g_subsystem_id_to_flag[12] = {
	CRAFT_SUBSYSTEM_FLAG_ENGINES,
	CRAFT_SUBSYSTEM_FLAG_FLIGHT_CONTROLS,
	CRAFT_SUBSYSTEM_FLAG_SHIELDS,
	CRAFT_SUBSYSTEM_FLAG_CANNONS,
	CRAFT_SUBSYSTEM_FLAG_TARGETING_COMPUTER,
	CRAFT_SUBSYSTEM_FLAG_WARHEAD_LAUNCHER,
	CRAFT_SUBSYSTEM_FLAG_BEAM_SYSTEM,
	CRAFT_SUBSYSTEM_FLAG_COMMUNICATIONS,
	CRAFT_SUBSYSTEM_FLAG_COUNTERMEASURES,
	CRAFT_SUBSYSTEM_FLAG_HYPERDRIVE,
	0,
	0,
};
/* Per subsystem id, the message argument that names it in IFMSG_086 ("ARG
 * system is ARG"), used by flight_update_timers and collide_damagecraft. */
// GLOBAL: XVT 0x51BF78
const uint8_t g_subsystem_message_arg_by_id[CRAFT_SUBSYSTEM_COUNT] = {
	90, 91, 98, 92, 96, 100, 95, 101, 97, 99};
/* Per subsystem id, the simulated seconds a failed subsystem takes to repair:
 * collide_damagecraft and collide_laserhitcraft put it in the craft's
 * system_repair_seconds, and flight_update_timers counts that down once per
 * second. The last two entries are 0. */
// GLOBAL: XVT 0x51BF88
const uint16_t g_subsystem_repair_duration[12] = {15, 20, 30, 25,  35, 20,
						  40, 20, 20, 100, 0,  0};
/* HUD features a damaging hit can knock out, one picked at random (game_rand() &
 * 15) by collide_damagecraft; each value is a bit of the craft's
 * installed_hud_feature_mask. */
// GLOBAL: XVT 0x51BFA0
const uint16_t g_subsystem_failure_hud_mask_by_random_slot[16] = {
	0x0200, 0x0010, 0x0020, 0x0002, 0x0400, 0x0180, 0x0010, 0x0008,
	0x0800, 0x0180, 0x0020, 0x0002, 0x1000, 0x0001, 0x0020, 0x0008,
};
/* Copy of the saved world state, as large as g_world_state_buffer:
 * flight_sync_snapshot_world_state_for_replay copies the saved state into it at each
 * world checksum, the copy a resync sends to a player whose checksum differs,
 * and a resync gathers a received state in it. Only
 * flight_alloc_world_state_buffers and flight_free_world_state_buffers set the
 * pointer. */
// GLOBAL: XVT 0x550880
uint8_t *g_world_state_dup_buffer;
/* Byte length of each region g_world_checksum covers, 0 past the last. Written
 * by flight_checksum_world_state in the original build and by the modern build's
 * snapshot and resync code. */
// GLOBAL: XVT 0x550840
unsigned int g_world_checksum_region_lengths[16] = {0};
/* The saved world state, laid out as flight_save_world_state writes it: saved at
 * flight start and after each world message is applied, and read back by
 * flight_restore_world_state. Only flight_alloc_world_state_buffers
 * (flight_calculate_world_state_buffer_size bytes) and flight_free_world_state_buffers
 * set the pointer. */
// GLOBAL: XVT 0x550B88
uint8_t *g_world_state_buffer;
/* Bytes in use in g_world_state_dup_buffer. Five functions write it:
 * flight_sync_snapshot_world_state_for_replay, and in the original build
 * flight_sync_apply_resync_and_replay_world_messages and
 * flight_apply_world_state_object_presence_map; in the modern one
 * xvt_resync_full_apply and xvt_snapshot_apply_presence_map. */
// GLOBAL: XVT 0x550B8C
int g_world_state_dup_size;
/* Memory handle of g_world_state_dup_buffer, 0 when none; only
 * flight_alloc_world_state_buffers and flight_free_world_state_buffers write it. */
// GLOBAL: XVT 0x550B94
uint16_t g_world_state_dup_handle = 0;
/* The world checksum: per region of the saved world state, up to 16, the sum of
 * its bytes, and 0 past the last region. Computed by flight_checksum_world_state
 * (xvt_snapshot_checksum in the modern build), sent between the players and
 * compared. */
// GLOBAL: XVT 0x550B98
unsigned int g_world_checksum[16] = {0};
/* Bytes in use in g_world_state_buffer. Four functions write it:
 * flight_save_world_state and flight_sync_apply_resync_and_replay_world_messages in the
 * original build, xvt_snapshot_save and xvt_resync_full_apply in the modern
 * one. */
// GLOBAL: XVT 0x550BD8
unsigned int g_world_state_size;
/* Memory handle of g_world_state_buffer, 0 when none; only
 * flight_alloc_world_state_buffers and flight_free_world_state_buffers write it. */
// GLOBAL: XVT 0x550BDC
uint16_t g_world_state_handle = 0;
/* The frontend's main window, which flight draws in and takes messages for;
 * flight_update_and_focus_main_window, its only writer, sets it. */
// GLOBAL: XVT 0x66DDD0
void *g_flight_main_window_handle = NULL;
/* Model index of the craft flight_update_craft_steering_and_speed is working on,
 * its only writer; flight_slew_object_speed_toward_target reads the model's
 * acceleration and deceleration rates through it. */
// GLOBAL: XVT 0x9A7394
uint16_t g_cur_craft_model_index = 0;
/* 1 when debris is drawn and the local debris slots are used. Set from the
 * debris option at launch (flight_main, xvt_flight_entry_configure), then from
 * the graphics detail preset (flight_apply_graphics_detail_preset). */
// GLOBAL: XVT 0x9A73F4
uint8_t g_debris_enabled = 0;
/* timeGetTime, in ms, of the last music update. Written by
 * flight_update_dynamic_music_state and at flight start (flight_main_loop,
 * xvt_flight_loading_runtime). */
// GLOBAL: XVT 0x9A7EC4
uint32_t g_dynamic_music_last_update_ms = 0;
/* 0 when the launch command line holds "novoice", else 1 (flight_main,
 * xvt_flight_entry_read_launch_switches); the voice loading and queueing code reads
 * it. */
// GLOBAL: XVT 0x9A8C10
uint8_t g_flight_conf_voice_enabled = 0;
/* 0 when the launch command line holds "nomusic", else 1 (flight_main,
 * xvt_flight_entry_read_launch_switches); nothing reads it. */
// GLOBAL: XVT 0x9D8C28
uint8_t g_flight_conf_music_enabled = 0;
/* 1 when the launch command line holds "nopilot" (flight_main,
 * xvt_flight_entry_read_launch_switches): flight start then leaves the network
 * players of g_pilot_data unmatched to the session's slots
 * (mission_sync_pilot_network_players_to_session_slots), and mission_init reads it
 * too. */
// GLOBAL: XVT 0x9C8E40
int g_flight_conf_no_pilot = 0;
/* Cleared at flight start (flight_main_loop, xvt_flight_loading_globals); nothing
 * else uses it. */
// GLOBAL: XVT 0x9C8E50
uint8_t g_unused_flight_runtime_block[768] = {0};
/* Random bytes filled at mission setup (flight_main_loop,
 * xvt_flight_loading_mission_setup): even entries 0 to 124, odd entries 0 to 3.
 * Nothing else reads it. */
// GLOBAL: XVT 0x9CD060
uint8_t g_flight_noise_table[512] = {0};
/* First object slot of the range each player fills locally, up to
 * g_local_debris_slot_end, which the saved world state, the checksums and the
 * presence maps leave out. Set by mission_init and put back with the world
 * state (flight_restore_world_state, xvt_snapshot_decode_prefix). */
// GLOBAL: XVT 0x9A8E24
int g_local_transient_slot_start = 0;
/* 0 when the launch command line holds "nosfx", else 1 (flight_main,
 * xvt_flight_entry_read_launch_switches); fsfx_play_sound and the sound loops check
 * it. */
// GLOBAL: XVT 0x9D80C9
uint8_t g_flight_conf_sfx_enabled = 0;
/* Object the local player's beam is on, 0xFFFF while the beam is off; two
 * functions write it: laser_weaponsfire, and proving_grounds_draw_course_object
 * for a course object it draws. damage_queue_craft_billboards_for_object_type marks
 * that object's billboards as selected, and fsfx_update_beam_system_loop reads
 * it. */
// GLOBAL: XVT 0x9D8110
uint16_t g_local_beam_target_obj_idx = 0;
/* Ticks until g_target_proximity_blink_bit flips again: flight_update_timers, its
 * only writer, counts it down and restarts it at 118 or 14 by the bit's new
 * value and by whether the local player's target is nearer than 32 times its
 * type's bounds extent (the distance is shifted down 5 bits before the
 * test). */
// GLOBAL: XVT 0x9A8064
static int16_t g_target_proximity_blink_timer = 0;
/* Steps per simulated second at the current step's length: 236
 * (SIMULATION_TICKS_PER_SECOND) over g_elapsed_ticks, at least 1. Many functions
 * write it, chiefly flight_step_sim_to_time and flight_advance_one_step (the
 * XvtFlightSim_ functions in the modern build) and the per-object updates,
 * which change it for each object and put it back. */
// GLOBAL: XVT 0x9D8112
uint16_t g_sim_steps_per_second = 0;
/* End, one past, of the local slot range that starts at
 * g_local_transient_slot_start; written the same way. */
// GLOBAL: XVT 0x9CD274
int g_local_debris_slot_end = 0;
/* Detail distance from the graphics detail preset
 * (g_graphics_detail_distance_threshold_by_preset); only
 * flight_apply_graphics_detail_preset writes it, and nothing reads it. */
// GLOBAL: XVT 0x9E8F50
uint16_t g_graphics_detail_distance_threshold = 0;
/* 1 when the mission has no .pal file of its own (checked at flight start at 1
 * byte per pixel), so the palette is built from the colors of the loaded
 * models' textures. Three functions write it: flight_main_loop,
 * xvt_flight_loading_palette and fe_disk_io_init_resources, which clears it unless
 * g_palette_generation_enabled. */
// GLOBAL: XVT 0x9E8F54
int g_generate_mission_palette = 0;
/* Set to 0 at flight start (flight_main_loop, xvt_flight_loading_globals,
 * xvt_flight_task_start_world); nothing reads it. */
// GLOBAL: XVT 0x9E964C
int g_unused_flight_startup_object_pass_state = 0;
/* 1 when fview_compute_object_view_matrix turns the world light direction into
 * each object's space, 0 when it copies it unchanged. Five functions write it:
 * flight_apply_graphics_detail_preset (1), flight_view_render, which clears it for
 * course obstacles, and the flight map, HUD 3D display and model preview
 * code. */
// GLOBAL: XVT 0x9EC476
uint8_t g_transform_light_direction_to_object_space = 0;
/* Ms left in the music track playing; when flight_update_dynamic_music_state
 * counts it to 0 or below it starts the flight track (track 2) again. INT32_MAX
 * with no music. Written by flight_update_dynamic_music_state and at flight start
 * (flight_main_loop, xvt_flight_loading_runtime). */
// GLOBAL: XVT 0x9EC468
int g_dynamic_music_track_remaining_ms = 0;
/* Length of the current simulation step, in ticks. Many functions write it,
 * chiefly flight_step_sim_to_time and flight_advance_one_step (the XvtFlightSim_
 * functions in the modern build) and the per-object updates, which change it
 * for each object and put it back. */
// GLOBAL: XVT 0x9EC5FE
uint16_t g_elapsed_ticks = 0;
/* Tick the simulation has reached. Many functions write it, chiefly
 * flight_step_sim_to_time, flight_advance_one_step and flight_run_mission_loop in the
 * original build and the XvtFlightSim_ and XvtFlightFrame_ functions in the
 * modern one; the wait for the mission start sets 0. */
// GLOBAL: XVT 0x9E9658
int g_game_time = 0;
/* -1, or the one object flight_update_craft_steering_and_speed and
 * object_update_lifetime_and_movement move alone while flight_advance_one_step
 * (xvt_flight_sim_advance) brings a player's craft up to an input frame's time.
 * Flight start sets -1. */
// GLOBAL: XVT 0x9EC5D0
int g_single_object_update_override_idx = -1;
/* The flight's shared countdown timers (see flight_global_countdown_timers),
 * counted down by flight_update_timers and cleared by
 * mission_init_flight_runtime_state. */
// GLOBAL: XVT 0x9EC5E0
struct flight_global_countdown_timers g_flight_global_countdown_timers = {0};
/* Players taking part (participation_state 1 or 2): the session's player count
 * at flight start, then recounted by flight_update_active_player_count and
 * flight_recount_players_and_check_mission_end. Four functions write it: those two,
 * flight_main_loop and xvt_flight_loading_globals. */
// GLOBAL: XVT 0x9FD394
int g_active_flight_player_count;
/* Set to 0 at mission setup (flight_main_loop, xvt_flight_loading_mission_setup);
 * nothing reads it. */
// GLOBAL: XVT 0x9FD390
uint8_t g_unused_flight_message_runtime_state = 0;
/* Players in the session at flight start (flight_main_loop,
 * xvt_flight_loading_globals), put back with the world state; 1 means solo
 * play. */
// GLOBAL: XVT 0xA080F8
int g_flight_player_count = 0;
/* Time stamp of the newest local input frame applied in multiplayer, so one
 * applied again is known (g_flight_sfx_side_effect_gate 2). Four functions write
 * it: flight_advance_one_step and flight_run_mission_loop (0 at its start) in the
 * original build, xvt_flight_sim_advance and xvt_flight_frame_begin in the modern
 * one. */
// GLOBAL: XVT 0xA0085C
int g_last_local_replay_input_timestamp = 0;
/* Per simulation update length in ticks, the frames that took it (the last
 * bucket counts 19 or more), counted only with the tickcounter launch option.
 * Written by flight_run_mission_loop, which clears it at its start, in the
 * original build and by xvt_flight_frame_begin and xvt_flight_frame_render in the
 * modern one. */
// GLOBAL: XVT 0x523650
unsigned int g_flight_update_duration_histogram[20] = {0};
/* Packet loss score behind g_packet_drop_indicator: up 10 for each new drop on
 * the host link, down 1 per frame. It changes only while
 * g_flight_prev_host_packet_drop_count is nonzero, which never happens, so it stays
 * 0. Written by flight_run_mission_loop in the original build and
 * xvt_flight_frame_begin and xvt_flight_frame_update_packet_drop_indicator in the
 * modern one. */
// GLOBAL: XVT 0x556974
int g_flight_packet_drop_score = 0;
/* Ticks between the last two step targets, from which the next is placed.
 * Written by flight_run_mission_loop in the original build and
 * xvt_flight_frame_begin, xvt_flight_frame_start_advance and
 * xvt_flight_frame_network_update in the modern one. */
// GLOBAL: XVT 0x556978
int g_predicted_frame_delta = 0;
/* Tick the simulation was last stepped to, 0 at the mission loop's start.
 * Written by flight_run_mission_loop in the original build and
 * xvt_flight_frame_begin, xvt_flight_frame_advance and xvt_flight_frame_network_update
 * in the modern one. */
// GLOBAL: XVT 0x55697C
int g_flight_last_step_target_timestamp = 0;
/* The host link's drop count (net_reliable_get_peer_packet_drop_count_by_dpid) at the
 * last frame, 0 at the mission loop's start. Its only update sits in the branch
 * taken when it is nonzero, so it stays 0 and g_packet_drop_indicator with it.
 * Written by flight_run_mission_loop in the original build and
 * xvt_flight_frame_begin and xvt_flight_frame_update_packet_drop_indicator in the
 * modern one. */
// GLOBAL: XVT 0x556980
int g_flight_prev_host_packet_drop_count = 0;
/* Music track playing: 0 none, 2 the flight track, 3 to 7 an outcome track
 * flight_update_dynamic_music_state picks by the local player's team goal status.
 * Written by flight_update_dynamic_music_state and at flight start
 * (flight_main_loop, xvt_flight_loading_runtime). */
// GLOBAL: XVT 0x9FD434
uint8_t g_dynamic_music_state = 0;
/* The flight's mission state (see flight_mission_state); saved and restored with
 * the world state. Many functions write it, chiefly the mission code, flight
 * start and flight_update_timers. */
// GLOBAL: XVT 0x9D6940
struct flight_mission_state g_flight_mission_state = {0};
/* 1 once an outcome track has started (flight_update_dynamic_music_state); flight
 * start clears it (flight_main_loop, xvt_flight_loading_runtime). */
// GLOBAL: XVT 0xA004C8
uint8_t g_dynamic_music_outcome_latched = 0;
/* 1 when backdrops are drawn. Set from the backdrop option at launch
 * (flight_main, xvt_flight_entry_configure), then from the graphics detail preset
 * (flight_apply_graphics_detail_preset). */
// GLOBAL: XVT 0xA080FC
uint8_t g_backdrops_enabled = 0;
/* Set to 0 at flight start (flight_main_loop, xvt_flight_loading_globals); nothing
 * reads it. */
// GLOBAL: XVT 0xA081F0
int g_unused_flight_session_reset_state = 0;
/* A debug log file nothing opens: flight start sets NULL, and the flight's end
 * closes it when set (flight_main_loop, xvt_flight_task_release_mission). */
// GLOBAL: XVT 0xA08210
xvt_file *g_unused_flight_debug_log_file = NULL;
/* A byte saved and restored with the world state and folded into the live
 * checksum. Only the restores write it (flight_restore_world_state,
 * xvt_snapshot_decode_prefix), so it stays 0. */
// GLOBAL: XVT 0x9D8C20
uint8_t g_world_state_reserved_byte = 0;
/* Debris slot count saved and restored with the world state and folded into the
 * checksums; mission_init sets it to 16. */
// GLOBAL: XVT 0x9A7B50
int g_world_state_debris_slot_count = 0;
/* A word saved and restored with the world state and folded into the live
 * checksum. Only the restores write it (flight_restore_world_state,
 * xvt_snapshot_decode_prefix), so it stays 0. */
// GLOBAL: XVT 0x9A73A4
int g_unused_world_state_serialized_dword = 0;
/* 1 when the launch command line holds "newnet" (flight_main,
 * xvt_flight_entry_read_launch_switches); the options sync with the other players
 * (flight_net_sync_player_options_and_taunts, xvt_flight_network_accept_roster) also
 * writes it, and the live checksum includes it. */
// GLOBAL: XVT 0xA08138
int g_flight_conf_new_net = 0;
/* Set to 1 at launch (flight_main, xvt_flight_entry_read_launch_switches) and put
 * back with the world state; laser_fireplayerweapon reads it. */
// GLOBAL: XVT 0x523424
int g_laser_fire_timestamp_tracking_enabled = 0;
/* Never written, so it stays 0 and the early returns that test it
 * (flight_update_player_step, xvt_flight_sim_update_player_step,
 * xvt_flight_controls_throttle_eligible) never run. */
// GLOBAL: XVT 0xA07C70
uint8_t g_dormant_flight_region_session_early_return_flag = 0;
/* Flipped by the Alt+M key in flight_update_player_step
 * (xvt_flight_sim_update_player_step in the modern build), its only writers;
 * nothing else reads it. */
// GLOBAL: XVT 0xA07CCE
uint8_t g_flight_alt_m_toggle = 0;
/* Set to 0 at flight start (flight_main_loop, xvt_flight_loading_globals); nothing
 * reads it. */
// GLOBAL: XVT 0x9FE734
int g_unused_flight_transient_reset_state = 0;
/* Cleared at flight start (flight_main_loop, xvt_flight_loading_globals); nothing
 * else uses it. */
// GLOBAL: XVT 0x9FE7A0
uint8_t g_unused_flight_network_block[48] = {0};
/* Copy of the local player's record taken when the options sync with the other
 * players fails at flight start (flight_main_loop,
 * xvt_flight_task_release_mission); nothing reads it. */
// GLOBAL: XVT 0x9ECC60
struct player_data g_local_player_snapshot_on_options_sync_failure = {0};
/* 1 when the launch command line holds "inprogress": a client joining a flight
 * already under way (flight_main, xvt_flight_entry_read_launch_switches). Handed to
 * net_session_init_game_session. */
// GLOBAL: XVT 0x523640
int g_flight_in_progress_launch = 0;
/* The launch command line split into arguments (see flight_launch_args), by
 * flight_main in the original build and xvt_flight_entry_prepare in the modern
 * one. */
// GLOBAL: XVT 0x622CC0
struct flight_launch_args g_flight_launch_args = {0};
/* 1 when the launch command line starts with '-' (flight_main,
 * xvt_flight_entry_read_launch_switches); nothing reads it. */
// GLOBAL: XVT 0x66DDE8
int g_flight_started_with_dash_arg = 0;
/* timeGetTime, in ms, when sound setup began (flight_main,
 * xvt_flight_entry_create_devices); nothing reads it. */
// GLOBAL: XVT 0x66E1FC
uint32_t g_flight_sound_init_start_time_ms = 0;

/* Sets g_unused_flight_resume_reset_slot0 and g_unused_flight_resume_reset_slot1, which
 * nothing reads, to -1. Called when a pause ends: by flight_update_player_step in
 * the original build, xvt_flight_sim_resume in the modern one. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x410FE0
void flight_reset_unused_resume_slots(void)
{
	g_unused_flight_resume_reset_slot0 = -1;
	g_unused_flight_resume_reset_slot1 = -1;
}

/* Counts down the flight's timers once per simulation step, by g_elapsed_ticks:
 * every g_flight_global_countdown_timers entry (the modern build takes the
 * reference clock's elapsed ticks for entries 1 to 5 and 10 when the timing is
 * unlocked); with side effects on, each active player's
 * g_player_flight_transient_timers; each player's pending_action_timer and
 * beam_fire_cooldown_timer; and for each craft in the active region and each
 * object with character data, the AI think, maneuver and secondary maneuver
 * timers (the reference clock's ticks in the modern build), plus each craft's
 * weapon fire inhibit and countermeasure cooldown timers and its turrets'
 * retarget cooldowns. Each stops at 0 except the think timers and turret
 * cooldowns. When g_target_proximity_blink_timer runs out it flips
 * g_target_proximity_blink_bit and restarts the timer; it then sets
 * g_render_object_ref and g_render_target_component_idx from the local player's
 * target. The rest runs once per 236 ticks, a simulated second: it advances
 * g_mission_elapsed_clock and counts g_mission_countdown_clock down, and when a
 * mission time limit runs out with side effects on, sets every player's
 * participation_state to 2 and mission_end_pending; it gives the time warnings at
 * 2 minutes, 1 minute, 15 and 2 seconds; in melee and combat missions with more
 * than one player it starts the team victory time limit when one team is left
 * or a team's goal status allows it; for each player's craft with any working
 * subsystem, it counts down the system_repair_seconds of the failed subsystem
 * with the lowest display slot and repairs it when they run out; it adds 1 to
 * every live object's seconds_alive; and it advances the HUD message panes. In
 * the original build, with no target, the blink test reads max_bounds_extent
 * before any value is set. */
// FUNCTION: XVT 0x415C90
void flight_update_timers(void)
{
	int index;

	uint16_t *timer = (uint16_t *)&g_flight_global_countdown_timers;
	for (index = 0; index < (int)(sizeof(g_flight_global_countdown_timers) /
				      sizeof(*timer));
	     ++index) {
		if (timer[index] != 0) {

			timer[index] -=
				xvt_flight_timing_is_unlocked() &&
						((index >= 1 && index <= 5) ||
						 index == 10)
					? xvt_flight_timing_reference_elapsed()
					: g_elapsed_ticks;

			if ((int16_t)timer[index] < 0) {
				timer[index] = 0;
			}
		}
	}

	int player_index;
	if (g_flight_sim_side_effects_suppressed == 0) {
		for (index = 0;
		     index <
		     (int)(sizeof(struct player_flight_transient_timers) /
			   sizeof(uint16_t));
		     ++index) {
			for (player_index = 0; player_index < 8;
			     ++player_index) {
				uint16_t *player_timer =
					(uint16_t
						 *)&g_player_flight_transient_timers
						[player_index] +
					index;

				if (g_players[player_index]
						    .participation_state != 0 &&
				    *player_timer != 0) {
					*player_timer -= g_elapsed_ticks;
					if ((int16_t)*player_timer < 0) {
						*player_timer = 0;
					}
				}
			}
		}
	}

	for (player_index = 0; player_index < 8; ++player_index) {
		if (g_players[player_index].pending_action_timer != 0) {
			g_players[player_index].pending_action_timer -=
				g_elapsed_ticks;
			if (g_players[player_index].pending_action_timer < 0) {
				g_players[player_index].pending_action_timer =
					0;
			}
		}
		if (g_players[player_index].beam_fire_cooldown_timer != 0) {
			g_players[player_index].beam_fire_cooldown_timer -=
				g_elapsed_ticks;
			if (g_players[player_index].beam_fire_cooldown_timer <
			    0) {
				g_players[player_index]
					.beam_fire_cooldown_timer = 0;
			}
		}
	}

	int object_index;
	for (object_index = g_active_region_object_slot_start;
	     object_index < g_active_region_craft_object_slot_end;
	     ++object_index) {
		if (g_object_table[object_index].object_type != 0) {
			g_cur_craft =
				g_object_table[object_index].mobj->p_craft;
			struct ai_controller *controller =
				&g_cur_craft->ai_controller;
			if (controller->think_timer != 0) {
				controller->think_timer -=
					xvt_flight_timing_reference_elapsed();
			}

			if (controller->maneuver_timer != 0) {

				controller->maneuver_timer -=
					xvt_flight_timing_reference_elapsed();

				if (controller->maneuver_timer < 0) {
					controller->maneuver_timer = 0;
				}
			}
			if (controller->secondary_maneuver_timer != 0) {

				controller->secondary_maneuver_timer -=
					xvt_flight_timing_reference_elapsed();

				if (controller->secondary_maneuver_timer < 0) {
					controller->secondary_maneuver_timer =
						0;
				}
			}

			if (g_cur_craft->weapon_fire_inhibit_timer != 0) {
				uint16_t previous_timer =
					(uint16_t)g_cur_craft
						->weapon_fire_inhibit_timer;

				g_cur_craft->weapon_fire_inhibit_timer -=
					g_elapsed_ticks;
				if ((uint16_t)g_cur_craft
					    ->weapon_fire_inhibit_timer >
				    previous_timer) {
					g_cur_craft->weapon_fire_inhibit_timer =
						0;
				}
			}
			if (g_cur_craft->cm_fire_cooldown_timer != 0) {
				g_cur_craft->cm_fire_cooldown_timer -=
					g_elapsed_ticks;
				if ((int16_t)g_cur_craft
					    ->cm_fire_cooldown_timer < 0) {
					g_cur_craft->cm_fire_cooldown_timer = 0;
				}
			}

			for (index = 0; index < g_cur_craft->laser_slot_count;
			     ++index) {
				struct turret_target_state *target_state =
					&g_cur_craft
						 ->turret_target_states[index];

				if (target_state->retarget_cooldown_timer > 0) {
					target_state->retarget_cooldown_timer -=
						g_elapsed_ticks;
				}
			}
		}
	}

	{
		int char_data_slot = g_mobile_object_char_data_slot_start;

		if (g_mobile_object_char_data_slot_end > char_data_slot) {
			object_index = g_mobile_object_char_data_slot_start;
			do {
				if (g_object_table[object_index].object_type !=
				    0) {
					struct ai_controller *controller =
						&g_object_table[object_index]
							 .mobj->p_char_data
							 ->ai_controller;

					if (controller->think_timer != 0) {
						controller->think_timer -=
							xvt_flight_timing_reference_elapsed();
					}

					if (controller->maneuver_timer != 0) {

						controller->maneuver_timer -=
							xvt_flight_timing_reference_elapsed();

						if (controller->maneuver_timer <
						    0) {
							controller
								->maneuver_timer =
								0;
						}
					}
					if (controller
						    ->secondary_maneuver_timer !=
					    0) {

						controller
							->secondary_maneuver_timer -=
							xvt_flight_timing_reference_elapsed();

						if (controller
							    ->secondary_maneuver_timer <
						    0) {
							controller
								->secondary_maneuver_timer =
								0;
						}
					}
				}
				++object_index;
				++char_data_slot;
			} while (g_mobile_object_char_data_slot_end >
				 char_data_slot);
		}
	}

	g_target_proximity_blink_timer -= g_elapsed_ticks;
	if (g_target_proximity_blink_timer < 0) {
		int max_bounds_extent;

		max_bounds_extent = 0;
		g_target_proximity_blink_bit ^= 0x0400;
		if ((uint16_t)g_players[g_local_player]
				    .current_target_object_idx != UINT16_MAX &&
		    g_players[g_local_player].object_index != -1) {
			pai_object_ref_direction_to_object_ref(
				(uint16_t)g_players[g_local_player]
					.current_target_object_idx,
				g_players[g_local_player].object_index);
			int object_type =
				g_object_table
					[(uint16_t)g_players[g_local_player]
						 .current_target_object_idx]
						.object_type;
			max_bounds_extent = g_object_type_table[object_type]
						    .max_bounds_extent;
			trig2_polardistance >>= 5;
		}
		{
			int target_distance = trig2_polardistance;

			if ((g_target_proximity_blink_bit & 0x0400) != 0) {
				if (target_distance < max_bounds_extent) {
					g_target_proximity_blink_timer = 118;
				} else {
					g_target_proximity_blink_timer = 14;
				}
			} else {
				g_target_proximity_blink_timer = 14;
				if (target_distance >= max_bounds_extent) {
					g_target_proximity_blink_timer = 118;
				}
			}
		}
	}

	g_render_object_ref =
		(uint16_t)g_players[g_local_player].current_target_object_idx |
		g_render_object_ref_flags | g_target_proximity_blink_bit;
	g_render_target_component_idx =
		(uint16_t)g_players[g_local_player].selected_target_component;

	g_mission_elapsed_clock.subsecond_ticks -= g_elapsed_ticks;
	if (g_mission_elapsed_clock.subsecond_ticks > 0) {
		return;
	}

	g_mission_elapsed_clock.subsecond_ticks += SIMULATION_TICKS_PER_SECOND;
	if (++g_mission_elapsed_clock.seconds >= 60) {
		g_mission_elapsed_clock.seconds = 0;
		if (++g_mission_elapsed_clock.minutes >= 60) {
			g_mission_elapsed_clock.minutes = 0;
			if (++g_mission_elapsed_clock.hours >= 24) {
				g_mission_elapsed_clock.hours = 0;
			}
		}
	}

	if (g_mission_countdown_clock.minutes != 0 ||
	    g_mission_countdown_clock.seconds != 0) {
		if (--g_mission_countdown_clock.seconds == UINT8_MAX) {
			g_mission_countdown_clock.seconds = 59;
			if (--g_mission_countdown_clock.minutes == UINT8_MAX) {
				g_mission_countdown_clock.seconds = 0;
				g_mission_countdown_clock.minutes = 0;
			}
		}
		if (g_flight_mission_state.mission_time_limit_minutes != 0 &&
		    g_mission_countdown_clock.minutes == 0 &&
		    g_mission_countdown_clock.seconds == 0 &&
		    g_flight_sim_side_effects_suppressed == 0) {
			for (player_index = 0; player_index < 8;
			     ++player_index) {
				g_players[player_index].participation_state = 2;
			}
			g_flight_mission_state.mission_end_pending = 1;
			XVT_LOG_INFO(
				"flight.mission_ending reason=\"%s\" slot=%d tick=%d",
				g_flight_mission_state
						.team_victory_time_limit_started
					? "victory_countdown"
					: "time_limit",
				-1, g_game_time);
			XVT_LOG_DEBUG(
				"flight.time_limit_reached minutes=%d victory=%d elapsed=%d tick=%d",
				(int)g_flight_mission_state
					.mission_time_limit_minutes,
				(int)g_flight_mission_state
					.team_victory_time_limit_started,
				g_mission_elapsed_clock.hours * 3600 +
					g_mission_elapsed_clock.minutes * 60 +
					g_mission_elapsed_clock.seconds,
				g_game_time);
		}
	}
	XVT_LOG_DEBUG(
		"flight.clock_second elapsed=%d left=%d limit=%d tick=%d predicted=%d",
		g_mission_elapsed_clock.hours * 3600 +
			g_mission_elapsed_clock.minutes * 60 +
			g_mission_elapsed_clock.seconds,
		g_mission_countdown_clock.minutes * 60 +
			g_mission_countdown_clock.seconds,
		(int)g_flight_mission_state.mission_time_limit_minutes,
		g_game_time, g_flight_sim_side_effects_suppressed);

	if (g_flight_mission_state.mission_time_limit_minutes != 0) {
		if (g_mission_countdown_clock.minutes == 2 &&
		    g_mission_countdown_clock.seconds == 0) {
			fsfx_play_sound(FLIGHT_SOUND_MISSION_TIMER_WARNING, -1,
					g_local_player);
			msg_emit_in_flight_message(
				IFMSG_201_MISSION_ENDS_IN_2_MINUTES,
				g_local_player);
		}
		if (g_mission_countdown_clock.minutes == 1 &&
		    g_mission_countdown_clock.seconds == 0) {
			fsfx_play_sound(FLIGHT_SOUND_WARNING_BEEP, -1,
					g_local_player);
			msg_emit_in_flight_message(
				IFMSG_202_MISSION_ENDS_IN_1_MINUTE,
				g_local_player);
		}
		if (g_mission_countdown_clock.minutes == 0 &&
		    g_mission_countdown_clock.seconds == 15) {
			int player_object_index =
				g_players[g_local_player].object_index;
			int use_rebel_craft_sound =
				player_object_index != -1 &&
				(g_object_table[player_object_index]
						 .object_type == 1 ||
				 g_object_table[player_object_index]
						 .object_type == 2 ||
				 g_object_table[player_object_index]
						 .object_type == 3 ||
				 g_object_table[player_object_index]
						 .object_type == 14 ||
				 g_object_table[player_object_index]
						 .object_type == 4);
			if (use_rebel_craft_sound != 0) {
				fsfx_play_sound(FLIGHT_SOUND_R2_WARNING, -1,
						g_local_player);
			} else {
				fsfx_play_sound(FLIGHT_SOUND_WARNING_BEEP, -1,
						g_local_player);
				fsfx_play_sound(FLIGHT_SOUND_WARNING_BEEP, -1,
						g_local_player);
			}
		}
		if (g_mission_countdown_clock.minutes == 0 &&
		    g_mission_countdown_clock.seconds == 2) {
			msg_emit_in_flight_message(
				IFMSG_203_MISSION_TIME_EXPIRING,
				g_local_player);
		}
	}

	if ((unsigned int)g_flight_mission_state
			    .max_connected_player_count_this_mission > 1 &&
	    g_flight_mission_state.team_victory_time_limit_minutes != 0 &&
	    g_flight_mission_state.team_victory_time_limit_started == 0) {
		if ((g_mission_header.mission_type == MISSION_TYPE_MELEE ||
		     g_mission_header.mission_type == MISSION_TYPE_COMBAT) &&
		    (g_mission_countdown_clock.minutes >
			     g_flight_mission_state
				     .team_victory_time_limit_minutes ||
		     (g_mission_countdown_clock.minutes == 0 &&
		      g_mission_countdown_clock.seconds == 0))) {
			uint8_t team_active[10];

			memset(team_active, 0, sizeof(team_active));

			int active_flag = 1;
			for (player_index = 0; player_index < 8;
			     ++player_index) {
				if (g_players[player_index]
					    .participation_state == 1) {
					team_active[(uint16_t)g_players
							    [player_index]
								    .team] =
						active_flag;
				}
			}
			if (g_mission_header.mission_type ==
			    MISSION_TYPE_MELEE) {
				for (object_index =
					     g_active_region_object_slot_start;
				     object_index <
				     g_active_region_craft_object_slot_end;
				     ++object_index) {
					if (g_object_table[object_index]
							    .object_type != 0 &&
					    g_mission_flight_groups
							    [g_object_table[object_index]
								     .flight_group_idx]
								    .fg
								    .player_number !=
						    0) {
						team_active
							[g_mission_flight_groups
								 [g_object_table[object_index]
									  .flight_group_idx]
									 .fg
									 .team] =
								active_flag;
					}
				}
			}
			int active_team_count = 0;
			int active_team;
			for (index = 0; index < 10; ++index) {
				if (team_active[index] != 0) {
					++active_team_count;
					active_team = index;
				}
			}
			if (active_team_count == 1 &&
			    (g_mission_header.mission_type ==
				     MISSION_TYPE_MELEE ||
			     g_flight_mission_state.runtime
					     .team_goal_status[active_team]
							      [0] == 1 ||
			     g_flight_mission_state.runtime
					     .team_goal_status[active_team]
							      [0] == 2 ||
			     g_flight_mission_state.runtime
					     .team_goal_status[active_team]
							      [1] == 1)) {
				g_mission_countdown_clock.seconds = 0;
				g_flight_mission_state
					.team_victory_time_limit_started = 1;
				g_mission_countdown_clock.minutes =
					g_flight_mission_state
						.team_victory_time_limit_minutes;
				g_flight_mission_state
					.mission_time_limit_minutes =
					g_flight_mission_state
						.team_victory_time_limit_minutes;
				if (g_flight_sim_side_effects_suppressed == 0) {
					XVT_LOG_INFO(
						"flight.victory_countdown team=%d minutes=%d reason=\"one_team\" tick=%d",
						active_team,
						(int)g_flight_mission_state
							.team_victory_time_limit_minutes,
						g_game_time);
				}
			}
		}
		if (g_flight_mission_state.team_victory_time_limit_started ==
			    0 &&
		    g_mission_header.mission_type == MISSION_TYPE_COMBAT &&
		    (g_flight_mission_state.runtime.team_goal_status[0][0] ==
			     1 ||
		     g_flight_mission_state.runtime.team_goal_status[1][0] ==
			     1)) {
			g_mission_countdown_clock.seconds = 0;
			g_flight_mission_state.team_victory_time_limit_started =
				1;
			g_mission_countdown_clock.minutes =
				g_flight_mission_state
					.team_victory_time_limit_minutes;
			g_flight_mission_state.mission_time_limit_minutes =
				g_flight_mission_state
					.team_victory_time_limit_minutes;
			if (g_flight_sim_side_effects_suppressed == 0) {
				XVT_LOG_INFO(
					"flight.victory_countdown team=%d minutes=%d reason=\"goal\" tick=%d",
					g_flight_mission_state.runtime
								.team_goal_status
									[0]
									[0] == 1
						? 0
						: 1,
					(int)g_flight_mission_state
						.team_victory_time_limit_minutes,
					g_game_time);
			}
		}
	}

	for (player_index = 0; player_index < 8; ++player_index) {
		struct player_data *player = &g_players[player_index];

		if (player->participation_state != 0 &&
		    player->object_index != -1) {
			uint16_t repair_display_slot = UINT16_MAX;
			struct craft_data *craft =
				g_object_table[player->object_index]
					.mobj->p_craft;

			if (craft->working_subsystems != 0) {
				uint16_t repair_system = UINT16_MAX;
				for (index = 0; index < CRAFT_SUBSYSTEM_COUNT;
				     ++index) {
					if (craft->system_health[index] == 0 &&
					    repair_display_slot >
						    craft->system_display_slot_by_system
							    [index]) {
						repair_display_slot =
							craft->system_display_slot_by_system
								[index];
						repair_system = (uint16_t)index;
					}
				}
				for (index = 0; index < CRAFT_SUBSYSTEM_COUNT;
				     ++index) {
					if (craft->system_health[index] == 0 &&
					    repair_system == index) {
						if (craft->system_repair_seconds
							    [index] == 0) {
							craft->system_health
								[index] = 100;
							craft->working_subsystems |=
								g_subsystem_id_to_flag
									[index];
							g_msg_arg_table[1] = 88;
							g_msg_arg_table[0] =
								g_subsystem_message_arg_by_id
									[index];
							msg_emit_in_flight_message(
								IFMSG_086_ARG_SYSTEM_IS_ARG,
								player_index);
							XVT_LOG_DEBUG(
								"flight.system_repaired slot=%d object=%d system=%d working=%04x predicted=%d",
								player_index,
								player->object_index,
								index,
								(unsigned)craft
									->working_subsystems,
								g_flight_sim_side_effects_suppressed);
						} else {
							--craft->system_repair_seconds
								  [index];
							XVT_LOG_DEBUG(
								"flight.system_repairing slot=%d object=%d system=%d left=%d predicted=%d",
								player_index,
								player->object_index,
								index,
								(int)craft->system_repair_seconds
									[index],
								g_flight_sim_side_effects_suppressed);
						}
					}
				}
			}
		}
	}

	{
		int slots_walked = 0;

		if (g_region_main_object_slot_end > slots_walked) {
			object_index = 0;
			do {
				if (g_object_table[object_index].object_type !=
				    0) {
					++g_object_table[object_index]
						  .mobj->seconds_alive;
				}
				++object_index;
				++slots_walked;
			} while (g_region_main_object_slot_end > slots_walked);
		}
	}
	hud_advance_flight_message_pane_timers();
}

/* Keeps the flight music going when music is on and the last mission had one
 * human player. While the flight track (2) plays and no outcome track has
 * started, it starts one when the local player's team goal status calls for it:
 * track 7, or 3 to 6 by mission type and the player's IFF, setting
 * g_dynamic_music_state and g_dynamic_music_outcome_latched. Otherwise it lowers
 * g_dynamic_music_track_remaining_ms by the ms since the last call
 * (g_dynamic_music_last_update_ms) and starts the flight track again when it runs
 * out. */
// FUNCTION: XVT 0x4165B0
void flight_update_dynamic_music_state(void)
{
	if (g_game_config.music_enabled == 0 ||
	    g_game_config.music_volume == 0 ||
	    g_pilot_data.num_human_players_last_mission != 1) {
		return;
	}

	uint8_t track_number = 0;
	if (g_dynamic_music_state == 2 &&
	    g_dynamic_music_outcome_latched == 0) {
		uint16_t player_team = g_players[g_local_player].team;
		uint8_t primary_goal_status =
			g_flight_mission_state.runtime
				.team_goal_status[player_team][0];
		if (primary_goal_status == 2 ||
		    g_flight_mission_state.runtime
				    .team_goal_status[player_team][1] == 1) {
			track_number = 7;
		} else if (primary_goal_status == 1 &&
			   g_mission_header.mission_type !=
				   MISSION_TYPE_MELEE) {
			if (g_mission_header.mission_type ==
			    MISSION_TYPE_COMBAT) {
				track_number =
					g_players[g_local_player].iff == 1 ? 6
									   : 4;
			} else {
				track_number =
					g_players[g_local_player].iff == 1 ? 5
									   : 3;
			}
		}

		if (track_number != 0) {
			music_cd_play_track_from_time(track_number, 0, 0);
			g_dynamic_music_track_remaining_ms =
				music_cd_get_track_length_ms(track_number);
			g_dynamic_music_outcome_latched = 1;
			g_dynamic_music_state = track_number;
			XVT_LOG_DEBUG(
				"flight.music_track track=%d reason=\"outcome\" ms=%d predicted=%d",
				(int)g_dynamic_music_state,
				g_dynamic_music_track_remaining_ms,
				g_flight_sim_side_effects_suppressed);
		}
	}

	if (track_number != 0) {
		return;
	}

	uint32_t current_time_ms = timeGetTime();
	uint32_t elapsed_ms = current_time_ms - g_dynamic_music_last_update_ms;
	g_dynamic_music_last_update_ms = current_time_ms;
	g_dynamic_music_track_remaining_ms -= elapsed_ms;
	if (g_dynamic_music_track_remaining_ms <= 0) {
		music_cd_play_track_from_time(2, 0, 0);
		g_dynamic_music_track_remaining_ms =
			music_cd_get_track_length_ms(2);
		g_dynamic_music_state = 2;
		XVT_LOG_DEBUG(
			"flight.music_track track=%d reason=\"repeat\" ms=%d predicted=%d",
			(int)g_dynamic_music_state,
			g_dynamic_music_track_remaining_ms,
			g_flight_sim_side_effects_suppressed);
	}
}

/* Returns g_world_state_dup_buffer. Only the original build calls this. */
// FUNCTION: XVT 0x416700
uint8_t *flight_get_duplicate_world_state_buffer(void)
{
	return g_world_state_dup_buffer;
}

/* Returns g_world_state_dup_size. Only the original build calls this. */
// FUNCTION: XVT 0x416710
int flight_get_duplicate_world_state_size(void)
{
	return g_world_state_dup_size;
}

/* Allocates the two world state buffers, flight_calculate_world_state_buffer_size
 * bytes each, and locks them: g_world_state_handle and g_world_state_buffer, then
 * g_world_state_dup_handle and g_world_state_dup_buffer. A failed allocation calls
 * fe_disk_io_fatal_error with the not-enough-memory message; the modern build
 * returns after it. */
// FUNCTION: XVT 0x416720
void flight_alloc_world_state_buffers(void)
{
	size_t buffer_size = flight_calculate_world_state_buffer_size();
	g_world_state_handle = memory_alloc_handle(buffer_size, 0);
	if (g_world_state_handle == 0) {
		XVT_LOG_ERROR(
			"flight.world_alloc_failed buffer=\"main\" bytes=%u",
			(unsigned)buffer_size);
		fe_disk_io_fatal_error(FILE_ERROR_STR_NOT_ENOUGH_MEMORY);
		return;
	}
	g_world_state_buffer = memory_get_handle_block(g_world_state_handle);

	g_world_state_dup_handle = memory_alloc_handle(buffer_size, 0);
	if (g_world_state_dup_handle == 0) {
		XVT_LOG_ERROR(
			"flight.world_alloc_failed buffer=\"copy\" bytes=%u",
			(unsigned)buffer_size);
		fe_disk_io_fatal_error(FILE_ERROR_STR_NOT_ENOUGH_MEMORY);
		return;
	}
	g_world_state_dup_buffer =
		memory_get_handle_block(g_world_state_dup_handle);
	XVT_LOG_DEBUG("flight.world_buffers bytes=%u main=%u copy=%u",
		      (unsigned)buffer_size, (unsigned)g_world_state_handle,
		      (unsigned)g_world_state_dup_handle);
}

/* Frees both world state buffers (the modern build skips a handle of 0) and
 * sets g_world_state_handle, g_world_state_buffer, g_world_state_dup_handle and
 * g_world_state_dup_buffer to 0 or NULL. */
// FUNCTION: XVT 0x4167A0
void flight_free_world_state_buffers(void)
{
	XVT_LOG_INFO("flight.world_freed held=%d bytes=%u",
		     (g_world_state_handle != 0) +
			     (g_world_state_dup_handle != 0),
		     g_world_state_size);
	unsigned int handle = g_world_state_handle;
	if (handle) {
		memory_free_handle((uint16_t)handle);
	}
	g_world_state_handle = 0;
	g_world_state_buffer = NULL;
	handle = g_world_state_dup_handle;
	if (handle) {
		memory_free_handle((uint16_t)handle);
	}
	g_world_state_dup_handle = 0;
	g_world_state_dup_buffer = NULL;
}

/* Writes the world state into g_world_state_buffer and sets g_world_state_size to
 * its length. Per object slot, main and static, except the local slots from
 * g_local_transient_slot_start to g_local_debris_slot_end: the type byte, and for a
 * live object its record, mobile object, craft data, warhead guidance and
 * character data, each pointer written as its offset into its pool plus 1, so
 * NULL stays 0 (the live pointers are put back after). Then the mission clocks,
 * header, flight group stats and flight groups, g_flight_mission_state,
 * g_flight_global_countdown_timers, the mission file version, g_flight_player_count,
 * g_world_state_reserved_byte, the pool sizes and slot ranges, the AI plan table
 * and count, g_unused_world_state_serialized_dword, the built-in plan ids, the game
 * random state, the next object signature, g_laser_fire_timestamp_tracking_enabled
 * and g_players. Does not check the buffer's size. The modern build calls
 * xvt_snapshot_save instead. */
// FUNCTION: XVT 0x4167F0
void flight_save_world_state(void)
{
	xvt_snapshot_save();
	if (g_world_state_size == 0) {
		XVT_LOG_ERROR("flight.world_save_failed held=%d tick=%d",
			      g_world_state_buffer != NULL, g_game_time);
	}
}

/* Reads the world state back from g_world_state_buffer in the order
 * flight_save_world_state writes it, turning stored offsets back into pointers; a
 * slot saved empty has its record, mobile object, craft data, guidance and
 * character data cleared, with player_owner_idx -1 and the mobile object's IFF
 * 0xFF. The modern build calls xvt_snapshot_restore instead. */
// FUNCTION: XVT 0x416E50
void flight_restore_world_state(void)
{
	xvt_snapshot_restore();
	if (g_flight_mission_state.mission_end_pending != 0) {
		XVT_LOG_ERROR(
			"flight.world_restore_failed bytes=%u confirmed=%d tick=%d",
			g_world_state_size, g_server_tick_time, g_game_time);
	}
}

/* Returns the bytes flight_save_world_state may write: the fixed part plus the
 * size of each flight group's stats and record, each static slot, each main
 * slot with its mobile object, each character data entry, each projectile
 * guidance and each craft data entry. The modern build returns
 * xvt_snapshot_calculate_size. */
// FUNCTION: XVT 0x4173D0
size_t flight_calculate_world_state_buffer_size(void)
{
	return xvt_snapshot_calculate_size();
}

/* Computes the world checksum of the saved state in g_world_state_buffer into
 * g_world_checksum and g_world_checksum_region_lengths: the byte sum of each
 * region, a region closing once it passes a sixteenth of g_world_state_size,
 * unused entries set to 0; a last region no longer than that is left out. Of
 * each object's record, mobile object and craft data only the first bytes are
 * summed: the struct's size less the sizes of its pointer fields and its cached
 * move and orientation fields (for craft data, less its field_3F2 too, plus
 * 32). The arguments are ignored; the modern build passes them to
 * xvt_snapshot_checksum instead. */
// FUNCTION: XVT 0x417450
void flight_checksum_world_state(int unused_arg0, int unused_arg1)
{
	xvt_snapshot_checksum(unused_arg0, unused_arg1);
	if (g_flight_mission_state.mission_end_pending != 0) {
		XVT_LOG_ERROR("flight.world_checksum_failed bytes=%u tick=%d",
			      g_world_state_size, g_game_time);
	}
}

/* Returns world_state_size divided by 124: the length of each resync segment.
 * Only the original build calls this. */
// FUNCTION: XVT 0x4178F0
int flight_compute_world_state_resync_segment_size(int world_state_size)
{
	return world_state_size / 124;
}

/* Writes 125 segment checksums of a world state to out_checksums and returns
 * 125. Each segment is world_state_size divided by 124 bytes (the whole state
 * when that is 0), each byte added and the sum rotated left 1 bit; a segment
 * past the end gets 0. Does not check the room at out_checksums. Only the
 * original build calls this. */
// FUNCTION: XVT 0x417900
int flight_build_world_state_resync_segment_checksums(
	int *out_checksums, const uint8_t *world_state, int world_state_size)
{
	int remaining_size = world_state_size;
	int segment_size = world_state_size / 124;
	if (segment_size == 0) {
		segment_size = world_state_size;
	}
	int segment_count = 125;
	do {
		uint32_t checksum = 0;

		if (segment_size > 0) {
			int bytes_in_segment = segment_size;
			do {
				if (remaining_size != 0) {
					checksum += *world_state++;
					--remaining_size;
					checksum = (checksum << 1) |
						   (checksum >> 31);
				}
			} while (--bytes_in_segment != 0);
		}
		*out_checksums++ = (int)checksum;
	} while (--segment_count != 0);

	return 125;
}

enum flight_world_state_presence_flags {
	FLIGHT_WORLDSTATE_HAS_OBJECT = 0x01,
	FLIGHT_WORLDSTATE_HAS_MOBILE = 0x02,
	FLIGHT_WORLDSTATE_HAS_CRAFT = 0x04,
	FLIGHT_WORLDSTATE_HAS_WARHEAD_GUIDANCE = 0x08,
	FLIGHT_WORLDSTATE_HAS_CHAR_DATA = 0x10,
	FLIGHT_WORLDSTATE_EMPTY_RUN_FLAG = 0x80,
	FLIGHT_WORLDSTATE_EMPTY_RUN_LENGTH_MASK = 0x7F,
	FLIGHT_WORLDSTATE_MAX_EMPTY_RUN = 0x7E
};

/* Writes the presence map of a saved world state (laid out as
 * flight_save_world_state writes it) to out_map and returns its length in bytes:
 * an int holding the object slot count, then for each saved slot a byte of
 * FLIGHT_WORLDSTATE_HAS_ flags for the blocks it holds, with a run of empty
 * slots, up to 126, written as one byte of 0x80 plus the run length. The modern
 * build returns xvt_snapshot_build_presence_map. Only the original build calls
 * this. */
// FUNCTION: XVT 0x417960
int flight_build_world_state_object_presence_map(uint8_t *out_map,
						 const uint8_t *world_state)
{
	return xvt_snapshot_build_presence_map(out_map, world_state);
}

/* Reshapes the state gathered in g_world_state_dup_buffer to match another
 * player's presence map: for each saved slot, up to the map's slot count, it
 * takes out the blocks (record, mobile object, craft data, guidance, character
 * data) the map says are absent and puts in zeroed ones it says are present,
 * moving the rest of the buffer, then sets g_world_state_dup_size. Does not check
 * the buffer has room for what it puts in; moving the rest up uses memcpy on
 * overlapping bytes. The modern build calls xvt_snapshot_apply_presence_map. Only
 * the original build calls this. */
// FUNCTION: XVT 0x417A60
void flight_apply_world_state_object_presence_map(const uint8_t *presence_map)
{
	xvt_snapshot_apply_presence_map(presence_map);
}

/* Runs the simulation from g_game_time to target_game_time in steps of at most
 * g_net_update_interval_ticks ticks, setting g_elapsed_ticks and
 * g_sim_steps_per_second for each. A step applies the players' input frames that
 * are due (flight_advance_one_step), then, outside the proving grounds, flight
 * group arrivals and the AI; then the timers, weapons, steering and speed,
 * debris recycling near the player (with debris on, outside the proving
 * grounds), collisions, object movement and special behavior, target checks,
 * participation, the mission logic, the message panes, the music and the
 * sounds, and adds the step to g_game_time. It returns early when a mission end
 * is pending with side effects on. When the target is the current time it only
 * applies the due input frames. Only the original build calls this; its modern
 * arm hands the work to xvt_flight_sim_step_to_time. */
// FUNCTION: XVT 0x417D70
void flight_step_sim_to_time(int target_game_time)
{
	xvt_flight_sim_step_to_time(target_game_time);
}

/* Applies each active player's due input frames from g_input_history, up to
 * target_game_time, through flight_update_player_step. A frame already applied and
 * not awaiting relay is removed, except in multiplayer with side effects
 * suppressed; a frame past the target is left, and so, with side effects on, is
 * one not from the server (input_source other than 0). For a frame at or before
 * g_game_time it first puts the player's craft back to the state saved after its
 * last applied frame, and sets g_game_time back to that state's time; when the
 * craft has changed it skips the frame with side effects suppressed, else moves
 * the frame to 4 ticks past now. It then moves the craft alone
 * (g_single_object_update_override_idx) up to the frame's time and saves its state
 * in the player record, sets g_replay_inputs for the player to the frame's input
 * (key and buttons cleared when it replays a past tick with side effects
 * suppressed) and g_elapsed_ticks to the ticks since the player's last frame, at
 * least 4. g_elapsed_ticks, g_sim_steps_per_second and g_game_time are put back
 * after each frame; a player who stops taking part gets no more frames. Sets
 * g_flight_sfx_side_effect_gate for the local player in multiplayer. Only the
 * original build calls this; its modern arm hands the work to
 * xvt_flight_sim_advance. */
// FUNCTION: XVT 0x417F10
void flight_advance_one_step(int target_game_time)
{
	xvt_flight_sim_advance(target_game_time);
}

/* Counts the players taking part (participation_state 1 or 2), stores the count
 * in g_active_flight_player_count and returns it. */
// FUNCTION: XVT 0x448CD0
int flight_update_active_player_count(void)
{
	int active_player_count = 0;
	for (int player_index = 0; player_index < 8; player_index++) {
		if (g_players[player_index].participation_state == 1 ||
		    g_players[player_index].participation_state == 2) {
			active_player_count++;
		}
		g_active_flight_player_count = active_player_count;
	}
	XVT_LOG_DEBUG(
		"flight.players_counted active=%d states=\"%d%d%d%d%d%d%d%d\" predicted=%d",
		active_player_count, (int)g_players[0].participation_state,
		(int)g_players[1].participation_state,
		(int)g_players[2].participation_state,
		(int)g_players[3].participation_state,
		(int)g_players[4].participation_state,
		(int)g_players[5].participation_state,
		(int)g_players[6].participation_state,
		(int)g_players[7].participation_state,
		g_flight_sim_side_effects_suppressed);

	return active_player_count;
}

/* Recounts g_active_flight_player_count as flight_update_active_player_count does,
 * sets g_flight_mission_state.mission_end_pending to 1 when no player is connected
 * (participation_state 1) or the local player no longer takes part, and returns
 * mission_end_pending. */
// FUNCTION: XVT 0x448D00
int flight_recount_players_and_check_mission_end(void)
{
	int connected_count = 0;
	int connected_or_pending_count = 0;
	for (int player_index = 0; player_index < 8; player_index++) {
		uint8_t connected_state =
			g_players[player_index].participation_state;
		if (connected_state == 1 || connected_state == 2) {
			connected_or_pending_count++;
		}
		if (connected_state == 1) {
			connected_count++;
		}
		g_active_flight_player_count = connected_or_pending_count;
	}

	if (connected_count == 0) {
		if (g_flight_mission_state.mission_end_pending == 0 &&
		    g_game_time == g_server_tick_time) {
			XVT_LOG_INFO(
				"flight.mission_ending reason=\"nobody_flying\" slot=%d tick=%d",
				-1, g_game_time);
		}
		g_flight_mission_state.mission_end_pending = 1;
	}
	if (g_players[g_local_player].participation_state == 0) {
		if (g_flight_mission_state.mission_end_pending == 0 &&
		    g_game_time == g_server_tick_time) {
			XVT_LOG_INFO(
				"flight.mission_ending reason=\"local_gone\" slot=%d tick=%d",
				g_local_player, g_game_time);
		}
		g_flight_mission_state.mission_end_pending = 1;
	}
	return g_flight_mission_state.mission_end_pending;
}

/* Returns a checksum of the live world, each part folded in by XOR and a left
 * rotation of 1 bit: the flight_checksum_buffer_rotate_xor checksum of every live
 * object's character data, mobile object, record and craft data, every
 * projectile's guidance, the mission clocks, flight group stats and records,
 * g_flight_mission_state, the timers, the mission header, messages and global
 * goals, the AI plan table and order data, and each active player's record,
 * along with the slot ranges, pool sizes and other counters. The modern build
 * returns xvt_snapshot_live_checksum. */
// FUNCTION: XVT 0x462A90
int flight_compute_live_world_state_checksum(void)
{
	return xvt_snapshot_live_checksum();
}

/* Returns a checksum of size bytes at data: each 4-byte word, then a 2-byte and
 * a 1-byte tail, XORed into the sum, which is rotated left 1 bit after each.
 * Returns 0 for size 0. */
// FUNCTION: XVT 0x4630C0
unsigned int flight_checksum_buffer_rotate_xor(const void *data,
					       unsigned int size)
{
	uint32_t word;
	uint32_t tail_value;
	const uint8_t *cursor = (const uint8_t *)data;
	uint32_t checksum = 0;
	uint32_t *checksum_ptr = &checksum;
	if (size >= sizeof(uint32_t)) {
		unsigned int word_count = size / sizeof(uint32_t);
		size -= word_count * sizeof(uint32_t);
		do {
			memcpy(&word, cursor, sizeof(word));
			*checksum_ptr ^= word;
			*checksum_ptr =
				(*checksum_ptr << 1) | (*checksum_ptr >> 31);
			cursor += sizeof(uint32_t);
		} while (--word_count != 0);
	}

	if (size >= sizeof(uint16_t)) {
		tail_value = 0;
		memcpy(&tail_value, cursor, sizeof(uint16_t));
		*checksum_ptr ^= tail_value;
		*checksum_ptr = (*checksum_ptr << 1) | (*checksum_ptr >> 31);
		cursor += sizeof(uint16_t);
		size -= sizeof(uint16_t);
		if (size == 1) {
			*checksum_ptr ^= *cursor;
			*checksum_ptr =
				(*checksum_ptr << 1) | (*checksum_ptr >> 31);
		}
	} else if (size == 1) {
		*checksum_ptr ^= *cursor;
		*checksum_ptr = (*checksum_ptr << 1) | (*checksum_ptr >> 31);
	}

	return checksum;
}

/* One player's part of a simulation step; does nothing once a mission end is
 * pending. With side effects on, a camera whose focus object is gone goes back
 * to the player's craft in the forward view (or loses its focus in the map
 * view), and a map aim target that is gone is cleared. It applies the player's
 * input record (flight_input_read with the player's index) and latches it. For
 * the local player with side effects on it handles Shift+L (radio message
 * backup), Alt+B (brightness, at 1 byte per pixel), Alt+D (graphics detail
 * preset), Alt+I (skipping odd scan lines), Alt+M (g_flight_alt_m_toggle), Alt+P
 * (pause, solo only, waiting for a key), Alt+S (system messages), Alt+V
 * (version message) and the screenshot key. A player awaiting a new craft whose
 * craft is gone gets the flight group's next craft, or stops taking part, and
 * the step ends there; outside hyperspace, button bit 0 fires and bit 1 picks a
 * target on a tap. Then the action keys (flight_process_player_actions), or the
 * chat input while chatting, and the flight controls while the player takes
 * part. Only the original build calls this; its modern arm hands the work to
 * xvt_flight_sim_update_player_step. */
// FUNCTION: XVT 0x47A5E0
void flight_update_player_step(int player_idx)
{
	xvt_flight_sim_update_player_step(player_idx);
}

/* Acts on the action key a player pressed this step (g_current_action_key). In
 * hyperspace it lets H abort the jump while hyperspace_phase is 1, else advances
 * the jump (flight_object_update_player_hyperspace_transition), and returns. In the
 * proving grounds it drops A, E, R, T, U, Y and F5 to F7. Outside the map view
 * it handles the cockpit keys: throttle (Backspace, the brackets and backslash,
 * plus and minus, the throttle keys, Enter to match the target's speed), the
 * energy and shield transfers and presets, views and the keypad look keys, the
 * beam, countermeasures, hyperspace, craft jumping, overdrive, S-foils, warhead
 * selection, the external camera, and Alt+E, which records the craft as lost
 * and sets it breaking up; in the map view its own keys move the map camera.
 * Then, in both, Space answers the pending prompt (pending_action_id), and it
 * handles the messages, the radio orders to wingmen, the targeting keys, the
 * MFD pages, the map, quitting and the arrow keys. Does not check that the
 * player has a craft before keys such as Backspace use it. */
// FUNCTION: XVT 0x47AC10
void flight_process_player_actions(int player_idx)
{
	int object_index = g_players[player_idx].object_index;
	struct craft_data *craft;
	if (object_index != -1) {
		craft = g_object_table[object_index].mobj->p_craft;
	} else {
		craft = NULL;
	}
	if (g_current_action_key != FLIGHT_KEY_NONE) {
		XVT_LOG_DEBUG(
			"flight.action_key slot=%d key=%u object=%d map=%d hyper=%d request=%d predicted=%d",
			player_idx, (unsigned)g_current_action_key,
			object_index,
			(int)g_players[player_idx].map_camera_state,
			(int)g_players[player_idx].hyperspace_phase,
			(int)g_players[player_idx].pending_action_id,
			g_flight_sim_side_effects_suppressed);
	}

	if (g_players[player_idx].hyperspace_phase != 0) {
		if (g_players[player_idx].hyperspace_phase == 1 &&
		    g_current_action_key == FLIGHT_KEY_H) {
			g_players[player_idx].hyperspace_phase = 0;
			XVT_LOG_DEBUG(
				"flight.hyperspace_aborted slot=%d object=%d predicted=%d",
				player_idx, object_index,
				g_flight_sim_side_effects_suppressed);
			msg_emit_in_flight_message(
				IFMSG_107_HYPERSPACE_JUMP_ABORTED, player_idx);
		} else {
			flight_object_update_player_hyperspace_transition(
				player_idx);
		}
		return;
	}

	if (g_flight_mission_state.proving_grounds_mode_active != 0) {
		switch (g_current_action_key) {
		case FLIGHT_KEY_A:
		case FLIGHT_KEY_E:
		case FLIGHT_KEY_R:
		case FLIGHT_KEY_T:
		case FLIGHT_KEY_U:
		case FLIGHT_KEY_Y:
		case FLIGHT_KEY_F5:
		case FLIGHT_KEY_F6:
		case FLIGHT_KEY_F7:
			XVT_LOG_DEBUG(
				"flight.proving_grounds_key_dropped slot=%d key=%u predicted=%d",
				player_idx, (unsigned)g_current_action_key,
				g_flight_sim_side_effects_suppressed);
			g_current_action_key = FLIGHT_KEY_NONE;
			break;
		default:
			break;
		}
	}

	if (g_players[player_idx].map_camera_state == 0) {
		const int standard_energy_transfer = 4;
		const int special_energy_transfer = 32;
		const int max_transfer_iterations = 100;
		const object_type_id special_slam_craft_type = 12;
		int transfer_charge_units;
		int energy_transfer_step;
		switch (g_current_action_key) {
		case FLIGHT_KEY_BACKSPACE:
			craft->throttle_speed = UINT16_MAX;
			XVT_LOG_DEBUG(
				"flight.throttle_set slot=%d key=%u throttle=%u predicted=%d",
				player_idx, (unsigned)g_current_action_key,
				(unsigned)craft->throttle_speed,
				g_flight_sim_side_effects_suppressed);
			fsfx_play_sound(FLIGHT_SOUND_SMALL_CLICK, -1,
					player_idx);
			msg_emit_in_flight_message(
				IFMSG_122_THROTTLE_SET_TO_FULL_POWER,
				player_idx);
			break;
		case FLIGHT_KEY_ENTER:
		case FLIGHT_KEY_PAD_ENTER:
			if ((g_mfd_active_page != MFD_PAGE_DAMAGE ||
			     player_idx != g_local_player ||
			     g_flight_player_count != 1 ||
			     g_object_table[object_index]
					     .mobj->p_craft
					     ->system_display_slot_by_system
						     [g_damage_mfd_current_system_id] ==
				     0) &&
			    g_players[player_idx].current_target_object_idx !=
				    -1) {
				struct mobile_object *target_mobile =
					g_object_table
						[(uint16_t)g_players[player_idx]
							 .current_target_object_idx]
							.mobj;
				if (target_mobile == NULL) {
					craft->throttle_speed = 0;
					XVT_LOG_DEBUG(
						"flight.throttle_set slot=%d key=%u throttle=%u predicted=%d",
						player_idx,
						(unsigned)g_current_action_key,
						(unsigned)craft->throttle_speed,
						g_flight_sim_side_effects_suppressed);
				} else {
					if (target_mobile->p_craft != NULL) {
						uint16_t target_speed =
							target_mobile->speed;
						uint16_t power_margin =
							6 -
							craft->shield_recharge_level -
							craft->beam_recharge_level -
							craft->laser_recharge_level;
						uint16_t max_speed =
							craft->ai_flight
								.max_speed_cache;
						if (power_margin >= 0x8000u) {
							max_speed -= math2_fraction(
								-power_margin
									<< 13,
								max_speed);
						} else {
							max_speed += math2_fraction(
								power_margin
									<< 13,
								max_speed);
						}
						if (max_speed <= target_speed) {
							craft->throttle_speed =
								UINT16_MAX;
							msg_emit_in_flight_message(
								IFMSG_280_TRYING_TO_MATCH_SPEED_WITH_TARGET_THROTTLE_SET_TO_FULL,
								player_idx);
						} else {
							craft->throttle_speed =
								math2_ratio_q16(
									target_speed,
									max_speed);
							msg_emit_in_flight_message(
								IFMSG_279_MATCHING_SPEED_WITH_TARGET,
								player_idx);
						}
						XVT_LOG_DEBUG(
							"flight.speed_matched slot=%d target=%d speed=%u max_speed=%u margin=%d throttle=%u predicted=%d",
							player_idx,
							(int)g_players[player_idx]
								.current_target_object_idx,
							(unsigned)target_speed,
							(unsigned)max_speed,
							(int)(int16_t)
								power_margin,
							(unsigned)craft
								->throttle_speed,
							g_flight_sim_side_effects_suppressed);
					} else {
						craft->throttle_speed =
							UINT16_MAX;
						XVT_LOG_DEBUG(
							"flight.throttle_set slot=%d key=%u throttle=%u predicted=%d",
							player_idx,
							(unsigned)
								g_current_action_key,
							(unsigned)craft
								->throttle_speed,
							g_flight_sim_side_effects_suppressed);
					}
				}
				fsfx_play_sound(FLIGHT_SOUND_SMALL_CLICK, -1,
						player_idx);
			}
			break;
		case FLIGHT_KEY_QUOTES: {
			if ((craft->system_flags &
			     CRAFT_SUBSYSTEM_FLAG_SHIELDS) != 0) {
				if ((craft->working_subsystems &
				     CRAFT_SUBSYSTEM_FLAG_SHIELDS) != 0) {
					int max_shield =
						craft_get_object_max_shield(
							(uint16_t)object_index);
					int shield_deficit =
						2 * max_shield -
						craft->shield_energy[1] -
						craft->shield_energy[0];
					if (shield_deficit < 0) {
						shield_deficit = 0;
					}
					if (shield_deficit != 0) {
						energy_transfer_step =
							get_model_index_from_type(
								g_object_table
									[g_players[player_idx]
										 .object_index]
										.object_type) ==
									get_model_index_from_type(
										special_slam_craft_type)
								? special_energy_transfer
								: standard_energy_transfer;
						int16_t remaining_charge = 0;
						int slot_index;
						for (slot_index = 0;
						     slot_index <
						     craft->laser_slot_count;
						     ++slot_index) {
							if (craft->weapon_slots
								    [slot_index]
									    .laser_charge >
							    0) {
								remaining_charge +=
									craft->weapon_slots
										[slot_index]
											.laser_charge;
							}
						}
						if (remaining_charge != 0) {
							slot_index = 0;
							do {
								int charge =
									craft->weapon_slots
										[slot_index]
											.laser_charge;
								if (charge >
								    0) {
									--remaining_charge;
									craft->weapon_slots
										[slot_index]
											.laser_charge =
										(int8_t)(charge -
											 1);
									if (craft->shield_energy
										    [0] <
									    max_shield) {
										int amount =
											craft->shield_energy[1] <
													max_shield
												? energy_transfer_step /
													  2
												: energy_transfer_step;
										craft->shield_energy
											[0] +=
											amount;
									}
									if (craft->shield_energy
										    [1] <
									    max_shield) {
										int amount =
											craft->shield_energy[0] <
													max_shield
												? energy_transfer_step /
													  2
												: energy_transfer_step;
										craft->shield_energy
											[1] +=
											amount;
									}
									if (craft->shield_energy
											    [0] >=
										    max_shield &&
									    craft->shield_energy
											    [1] >=
										    max_shield) {
										break;
									}
								}
								++slot_index;
								if (slot_index >=
								    craft->laser_slot_count) {
									slot_index =
										0;
								}
							} while (
								remaining_charge !=
								0);
							fsfx_play_sound(
								FLIGHT_SOUND_CONFIRM_BEEP,
								-1, player_idx);
							msg_emit_in_flight_message(
								IFMSG_360_TRANSFERRING_ALL_LASER_ENERGY_TO_SHIELDS,
								player_idx);
						} else {
							fsfx_play_sound(
								FLIGHT_SOUND_SMALL_CLICK,
								-1, player_idx);
						}

						switch (craft->shield_distrib_mode) {
						case SHIELD_DISTRIBUTION_FULLY_FORWARD:
							craft->shield_energy
								[0] +=
								energy_transfer_step;
							break;
						case SHIELD_DISTRIBUTION_FULLY_AFT:
							craft->shield_energy
								[1] +=
								energy_transfer_step;
							break;
						default:
							craft->shield_energy
								[0] +=
								energy_transfer_step /
								2;
							craft->shield_energy
								[1] +=
								energy_transfer_step /
								2;
							break;
						}
						XVT_LOG_DEBUG(
							"flight.lasers_to_shields slot=%d kind=\"all\" step=%d left=%d front=%d rear=%d limit=%d mode=%d predicted=%d",
							player_idx,
							energy_transfer_step,
							(int)remaining_charge,
							craft->shield_energy[0],
							craft->shield_energy[1],
							max_shield,
							(int)craft
								->shield_distrib_mode,
							g_flight_sim_side_effects_suppressed);
					} else {
						fsfx_play_sound(
							FLIGHT_SOUND_SMALL_CLICK,
							-1, player_idx);
					}
				} else {
					g_msg_arg_table[0] = 98;
					g_msg_arg_table[1] = 87;
					msg_emit_in_flight_message(
						IFMSG_086_ARG_SYSTEM_IS_ARG,
						player_idx);
				}
			} else {
				msg_emit_in_flight_message(
					IFMSG_226_YOUR_CRAFT_DOES_NOT_HAVE_A_SHIELD_SYSTEM,
					player_idx);
			}
			break;
		}
		case FLIGHT_KEY_APOSTROPHE:
		case FLIGHT_KEY_SHIFT_F10: {
			int slot_index = 0;

			if ((craft->system_flags &
			     CRAFT_SUBSYSTEM_FLAG_SHIELDS) != 0) {
				if ((craft->working_subsystems &
				     CRAFT_SUBSYSTEM_FLAG_SHIELDS) != 0) {
					int max_shield =
						2 *
						g_model_defs
							[get_model_index_from_type(
								 g_object_table
									 [g_players[player_idx]
										  .object_index]
										 .object_type)]
								.shield_strength;
					int missing_shield_energy =
						2 * max_shield -
						craft->shield_energy[1] -
						craft->shield_energy[0];
					if (missing_shield_energy < 0) {
						missing_shield_energy = 0;
					}
					if (missing_shield_energy > 800) {
						missing_shield_energy = 800;
					}
					if (get_model_index_from_type(
						    g_object_table
							    [g_players[player_idx]
								     .object_index]
								    .object_type) ==
					    get_model_index_from_type(
						    special_slam_craft_type)) {
						energy_transfer_step =
							special_energy_transfer;
						transfer_charge_units =
							missing_shield_energy /
							energy_transfer_step;
					} else {
						energy_transfer_step =
							standard_energy_transfer;
						transfer_charge_units =
							missing_shield_energy /
							energy_transfer_step;
					}
					if (transfer_charge_units != 0) {

						int16_t iteration = 0;
						for (iteration = 0;
						     transfer_charge_units > 0;
						     ++iteration) {
							if (iteration >=
							    max_transfer_iterations) {
								break;
							}
							if (craft->weapon_slots
								    [slot_index]
									    .laser_charge >
							    0) {
								--craft->weapon_slots
									  [slot_index]
										  .laser_charge;
								--transfer_charge_units;
								if (craft->shield_distrib_mode ==
								    SHIELD_DISTRIBUTION_FULLY_FORWARD) {
									if (craft->shield_energy
										    [0] <
									    max_shield) {
										craft->shield_energy
											[0] +=
											energy_transfer_step;
									} else if (
										craft->shield_energy
											[1] <
										max_shield) {
										craft->shield_energy
											[1] +=
											energy_transfer_step;
									}
								} else if (
									craft->shield_distrib_mode ==
									SHIELD_DISTRIBUTION_FULLY_AFT) {
									if (craft->shield_energy
										    [1] <
									    max_shield) {
										craft->shield_energy
											[1] +=
											energy_transfer_step;
									} else if (
										craft->shield_energy
											[0] <
										max_shield) {
										craft->shield_energy
											[0] +=
											energy_transfer_step;
									}
								} else {
									craft->shield_energy
										[0] +=
										energy_transfer_step /
										2;
									craft->shield_energy
										[1] +=
										energy_transfer_step /
										2;
								}
							}
							++slot_index;
							if (slot_index >=
							    craft->laser_slot_count) {
								slot_index = 0;
							}
						}

						if (craft->shield_energy[0] >
						    max_shield) {
							craft->shield_energy
								[0] =
								max_shield;
						}
						if (craft->shield_energy[1] >
						    max_shield) {
							craft->shield_energy
								[1] =
								max_shield;
						}
						XVT_LOG_DEBUG(
							"flight.lasers_to_shields slot=%d kind=\"part\" step=%d left=%d front=%d rear=%d limit=%d mode=%d predicted=%d",
							player_idx,
							energy_transfer_step,
							transfer_charge_units,
							craft->shield_energy[0],
							craft->shield_energy[1],
							max_shield,
							(int)craft
								->shield_distrib_mode,
							g_flight_sim_side_effects_suppressed);
						fsfx_play_sound(
							FLIGHT_SOUND_CONFIRM_BEEP,
							-1, player_idx);
						msg_emit_in_flight_message(
							IFMSG_132_TRANSFERRING_PARTIAL_POWER_FROM_CANNONS_TO_SHIELDS,
							player_idx);
					} else {
						fsfx_play_sound(
							FLIGHT_SOUND_SMALL_CLICK,
							-1, player_idx);
					}
				} else {
					g_msg_arg_table[0] = 98;
					g_msg_arg_table[1] = 87;
					msg_emit_in_flight_message(
						IFMSG_086_ARG_SYSTEM_IS_ARG,
						player_idx);
				}
			} else {
				msg_emit_in_flight_message(
					IFMSG_226_YOUR_CRAFT_DOES_NOT_HAVE_A_SHIELD_SYSTEM,
					player_idx);
			}
			break;
		}
		case FLIGHT_KEY_SHIFT_9:
		case FLIGHT_KEY_SHIFT_0: {
			int preset_index =
				g_current_action_key != FLIGHT_KEY_SHIFT_9;
			g_players[player_idx].throttle_preset[preset_index] =
				(int16_t)craft->throttle_speed;
			g_players[player_idx].laser_preset[preset_index] =
				craft->laser_recharge_level;
			g_players[player_idx].shield_preset[preset_index] =
				craft->shield_recharge_level;
			g_players[player_idx].beam_preset[preset_index] =
				craft->beam_recharge_level;
			XVT_LOG_DEBUG(
				"flight.preset_saved slot=%d preset=%d throttle=%u laser_level=%d shield_level=%d beam_level=%d predicted=%d",
				player_idx, preset_index,
				(unsigned)craft->throttle_speed,
				(int)craft->laser_recharge_level,
				(int)craft->shield_recharge_level,
				(int)craft->beam_recharge_level,
				g_flight_sim_side_effects_suppressed);
			msg_emit_in_flight_message(
				IFMSG_123_CONFIGURATION_SAVED_TO_PRESET,
				player_idx);
			fsfx_play_sound(FLIGHT_SOUND_SMALL_CLICK, -1,
					player_idx);
			break;
		}
		case FLIGHT_KEY_PLUS:
		case FLIGHT_KEY_EQUAL:
		case FLIGHT_KEY_PAD_PLUS:
			flight_player_increase_throttle_speed(2048, player_idx);
			fsfx_play_sound(FLIGHT_SOUND_SMALL_CLICK, -1,
					player_idx);
			break;
		case FLIGHT_KEY_MINUS:
		case FLIGHT_KEY_PAD_MINUS:
			flight_player_decrease_throttle_speed(2048, player_idx);
			fsfx_play_sound(FLIGHT_SOUND_SMALL_CLICK, -1,
					player_idx);
			break;
		case FLIGHT_KEY_PERIOD:
		case FLIGHT_KEY_PAD_DOT:
			if (g_flight_sim_side_effects_suppressed == 0) {
				if (g_players[player_idx]
						    .view_state
						    .external_camera_active ==
					    0 &&
				    g_players[player_idx]
						    .view_state
						    .camera_focus_obj_idx ==
					    g_players[player_idx]
						    .object_index) {
					if (g_players[player_idx]
						    .view_state
						    .hud_state_live !=
					    HUD_VIEW_HUD_ONLY) {
						g_players[player_idx]
							.saved_hud_view_state =
							HUD_VIEW_HUD_ONLY;
						hud_set_hud_view_state(
							HUD_VIEW_HUD_ONLY,
							player_idx);
					} else {
						g_players[player_idx]
							.saved_hud_view_state =
							HUD_VIEW_FORWARD;
						hud_set_hud_view_state(
							HUD_VIEW_FORWARD,
							player_idx);
					}
				}
				g_players[player_idx].view_state.hud_aim_x = 0;
				g_players[player_idx].view_state.hud_aim_y = 0;
				XVT_LOG_DEBUG(
					"flight.view_set slot=%d key=%u hud=%d focus=%d external=%d camera=%d blocked=%d aim_x=%d aim_y=%d predicted=%d",
					player_idx,
					(unsigned)g_current_action_key,
					(int)g_players[player_idx]
						.view_state.hud_state_live,
					(int)g_players[player_idx]
						.view_state
						.camera_focus_obj_idx,
					(int)g_players[player_idx]
						.view_state
						.external_camera_active,
					(int)g_players[player_idx]
						.view_state
						.target_camera_active,
					(int)g_players[player_idx]
						.view_state
						.player_input_blocked,
					(int)g_players[player_idx]
						.view_state.hud_aim_x,
					(int)g_players[player_idx]
						.view_state.hud_aim_y,
					g_flight_sim_side_effects_suppressed);
			}
			break;
		case FLIGHT_KEY_0:
		case FLIGHT_KEY_9: {
			int preset_index = g_current_action_key != FLIGHT_KEY_9;
			craft->throttle_speed =
				(uint16_t)g_players[player_idx]
					.throttle_preset[preset_index];
			craft->laser_recharge_level =
				g_players[player_idx]
					.laser_preset[preset_index];
			craft->shield_recharge_level =
				g_players[player_idx]
					.shield_preset[preset_index];
			craft->beam_recharge_level =
				g_players[player_idx].beam_preset[preset_index];
			XVT_LOG_DEBUG(
				"flight.preset_recalled slot=%d preset=%d throttle=%u laser_level=%d shield_level=%d beam_level=%d predicted=%d",
				player_idx, preset_index,
				(unsigned)craft->throttle_speed,
				(int)craft->laser_recharge_level,
				(int)craft->shield_recharge_level,
				(int)craft->beam_recharge_level,
				g_flight_sim_side_effects_suppressed);
			fsfx_play_sound(FLIGHT_SOUND_SMALL_CLICK, -1,
					player_idx);
			break;
		}
		case FLIGHT_KEY_SEMICOLON:
		case FLIGHT_KEY_SHIFT_F9: {
			if ((craft->system_flags &
			     CRAFT_SUBSYSTEM_FLAG_SHIELDS) != 0) {
				const int max_laser_charge = 127;
				int16_t empty_charge_units = 0;
				int slot_index;
				for (slot_index = 0;
				     slot_index < craft->laser_slot_count;
				     ++slot_index) {
					empty_charge_units +=
						max_laser_charge -
						craft->weapon_slots[slot_index]
							.laser_charge;
				}
				energy_transfer_step =
					get_model_index_from_type(
						g_object_table[object_index]
							.object_type) ==
							get_model_index_from_type(
								special_slam_craft_type)
						? special_energy_transfer
						: standard_energy_transfer;
				if (empty_charge_units >
				    max_transfer_iterations) {
					empty_charge_units =
						max_transfer_iterations;
				}
				int16_t shield_energy_to_remove =
					energy_transfer_step *
					empty_charge_units;

				int removed_shield_energy =
					shield_energy_to_remove;
				if (craft->shield_distrib_mode ==
				    SHIELD_DISTRIBUTION_FULLY_FORWARD) {
					if (removed_shield_energy >
					    craft->shield_energy[0]) {
						removed_shield_energy =
							craft->shield_energy[0];
					}
					craft->shield_energy[0] -=
						removed_shield_energy;
				} else if (craft->shield_distrib_mode ==
					   SHIELD_DISTRIBUTION_FULLY_AFT) {
					if (removed_shield_energy >
					    craft->shield_energy[1]) {
						removed_shield_energy =
							craft->shield_energy[1];
					}
					craft->shield_energy[1] -=
						removed_shield_energy;
				} else {
					int front_removed =
						shield_energy_to_remove >> 1;
					int rear_removed =
						shield_energy_to_remove >> 1;
					if (front_removed >
					    craft->shield_energy[0]) {
						front_removed =
							craft->shield_energy[0];
					}
					craft->shield_energy[0] -=
						front_removed;
					if (shield_energy_to_remove >
					    craft->shield_energy[1]) {
						rear_removed =
							craft->shield_energy[1];
					}
					craft->shield_energy[1] -= rear_removed;
					removed_shield_energy =
						front_removed + rear_removed;
				}

				transfer_charge_units = removed_shield_energy /
							energy_transfer_step;
				XVT_LOG_DEBUG(
					"flight.shields_to_lasers slot=%d step=%d room=%d removed=%d units=%d front=%d rear=%d mode=%d predicted=%d",
					player_idx, energy_transfer_step,
					(int)empty_charge_units,
					removed_shield_energy,
					transfer_charge_units,
					craft->shield_energy[0],
					craft->shield_energy[1],
					(int)craft->shield_distrib_mode,
					g_flight_sim_side_effects_suppressed);
				if (transfer_charge_units != 0) {
					slot_index = 0;
					for (int16_t iteration = 0;
					     transfer_charge_units > 0;
					     ++iteration) {
						if (iteration >=
						    max_transfer_iterations) {
							break;
						}
						if (craft
							    ->weapon_slots
								    [slot_index]
							    .laser_charge <
						    max_laser_charge) {
							++craft
								  ->weapon_slots
									  [slot_index]
								  .laser_charge;
						}
						--transfer_charge_units;
						++slot_index;
						if (slot_index >=
						    craft->laser_slot_count) {
							slot_index = 0;
						}
					}
					fsfx_play_sound(
						FLIGHT_SOUND_CONFIRM_BEEP, -1,
						player_idx);
					msg_emit_in_flight_message(
						IFMSG_131_TRANSFERRING_PARTIAL_POWER_FROM_SHIELDS_TO_CANNON_SYSTEM,
						player_idx);
				} else {
					fsfx_play_sound(
						FLIGHT_SOUND_SMALL_CLICK, -1,
						player_idx);
				}
			} else {
				fsfx_play_sound(FLIGHT_SOUND_SMALL_CLICK, -1,
						player_idx);
				msg_emit_in_flight_message(
					IFMSG_226_YOUR_CRAFT_DOES_NOT_HAVE_A_SHIELD_SYSTEM,
					player_idx);
			}
			break;
		}
		case FLIGHT_KEY_SHIFT_B:
			if ((craft->working_subsystems &
			     CRAFT_SUBSYSTEM_FLAG_COMMUNICATIONS) != 0) {
				if (g_players[player_idx]
						    .current_target_object_idx !=
					    -1 &&
				    g_players[player_idx]
						    .current_target_object_idx <
					    g_active_region_craft_object_slot_end) {
					int target_index =
						(uint16_t)g_players[player_idx]
							.current_target_object_idx;
					g_cur_craft =
						g_object_table[target_index]
							.mobj->p_craft;
					struct ai_controller *controller =
						&g_cur_craft->ai_controller;
					pai_setupcraftcontext(
						(uint16_t)target_index);
					if ((strcmp(g_plan_table
							    [controller
								     ->running_plan_id]
								    .name,
						    "boardtogivepln") == 0 ||
					     strcmp(g_plan_table
							    [controller
								     ->running_plan_id]
								    .name,
						    "board3pln") == 0) &&
					    pai_current_order_targets_match_object(
						    (uint16_t)g_players[player_idx]
							    .object_index) !=
						    0) {
						controller
							->candidate_target_idx =
							(uint16_t)g_players
								[player_idx]
									.object_index;
						XVT_LOG_DEBUG(
							"flight.resupply_called slot=%d target=%d plan=%d predicted=%d",
							player_idx,
							target_index,
							(int)controller
								->running_plan_id,
							g_flight_sim_side_effects_suppressed);
						msg_radio_message(
							(uint16_t)target_index,
							(uint8_t *)g_cur_craft,
							0x102, 0, 0);
						fsfx_speak_tactical_officer_event(
							TACTICAL_VOICE_STATUS,
							TACTICAL_MSG_RESUPPLIES_ON_THE_WAY,
							target_index,
							UINT16_MAX);
					} else {
						g_msg_sender_iff =
							g_players[player_idx]
								.iff;
						msg_emit_in_flight_message(
							IFMSG_146_CRAFT_NOT_RESPONDING_TO_ORDER,
							player_idx);
					}
				}
			} else {
				g_msg_arg_table[0] = 101;
				g_msg_arg_table[1] = 87;
				msg_emit_in_flight_message(
					IFMSG_086_ARG_SYSTEM_IS_ARG,
					player_idx);
			}
			break;
		case FLIGHT_KEY_SHIFT_C:
			if ((craft->working_subsystems &
			     CRAFT_SUBSYSTEM_FLAG_COMMUNICATIONS) != 0) {
				int attacker_index =
					player_find_attacker_of_target(
						(uint16_t)object_index,
						(int16_t)object_index);
				XVT_LOG_DEBUG(
					"flight.order_given slot=%d command=\"cover\" target=%d predicted=%d",
					player_idx, attacker_index,
					g_flight_sim_side_effects_suppressed);
				if (attacker_index != -1) {
					player_issue_ai_wingman_target_order(
						(uint16_t)attacker_index, 0x96,
						6, player_idx);
				}
				for (int16_t other_player_idx = 0;
				     other_player_idx < 8; ++other_player_idx) {
					if (other_player_idx == player_idx ||
					    g_players[other_player_idx]
							    .participation_state !=
						    1) {
						continue;
					}
					int other_object_index =
						g_players[other_player_idx]
							.object_index;
					if (other_object_index ==
						    attacker_index ||
					    other_object_index == -1) {
						continue;
					}
					int hostile =
						g_players[other_player_idx]
								.team !=
							g_players[player_idx]
								.team &&
						g_mission_teams[(uint16_t)g_players
									[other_player_idx]
										.team]
								.allies[(uint16_t)g_players
										[player_idx]
											.team] ==
							0;
					if (hostile ||
					    g_players[other_player_idx]
							    .pending_action_id !=
						    0) {
						continue;
					}
					struct craft_data *other_craft =
						g_object_table
							[other_object_index]
								.mobj->p_craft;
					if (other_craft == NULL ||
					    (other_craft->working_subsystems &
					     CRAFT_SUBSYSTEM_FLAG_COMMUNICATIONS) ==
						    0) {
						continue;
					}
					g_players[other_player_idx]
						.pending_action_id = 5;
					g_players[other_player_idx]
						.pending_action_issuer_player_idx =
						(uint16_t)player_idx;
					g_players[other_player_idx]
						.pending_action_param =
						(int16_t)(attacker_index != -1
								  ? attacker_index
								  : g_players[player_idx]
									    .object_index);
					g_players[other_player_idx]
						.pending_action_timer = 1416;
					XVT_LOG_DEBUG(
						"flight.request_posted slot=%d request=%d by=%d param=%d timer=%d predicted=%d",
						(int)other_player_idx,
						(int)g_players[other_player_idx]
							.pending_action_id,
						(int)g_players[other_player_idx]
							.pending_action_issuer_player_idx,
						(int)g_players[other_player_idx]
							.pending_action_param,
						g_players[other_player_idx]
							.pending_action_timer,
						g_flight_sim_side_effects_suppressed);
					if (other_player_idx ==
					    g_local_player) {
						fsfx_play_sound(
							FLIGHT_SOUND_INCOMING_ORDER,
							-1, g_local_player);
						msg_add_message_ptr(
							0,
							net_session_get_player_name(
								player_idx));
						if (attacker_index != -1) {
							msg_emit_in_flight_message(
								IFMSG_277_FROM_ARG_COVER_ME_HIT_SPACE_TO_TARGET_ATTACKER,
								g_local_player);
						} else {
							msg_emit_in_flight_message(
								IFMSG_278_FROM_ARG_COVER_ME_HIT_SPACE_TO_TARGET_ME,
								g_local_player);
						}
					}
				}
			} else {
				g_msg_arg_table[0] = 101;
				g_msg_arg_table[1] = 87;
				msg_emit_in_flight_message(
					IFMSG_086_ARG_SYSTEM_IS_ARG,
					player_idx);
			}
			break;
		case FLIGHT_KEY_LEFT_BRACKET:
			craft->throttle_speed = 21845;
			XVT_LOG_DEBUG(
				"flight.throttle_set slot=%d key=%u throttle=%u predicted=%d",
				player_idx, (unsigned)g_current_action_key,
				(unsigned)craft->throttle_speed,
				g_flight_sim_side_effects_suppressed);
			fsfx_play_sound(FLIGHT_SOUND_SMALL_CLICK, -1,
					player_idx);
			msg_emit_in_flight_message(
				IFMSG_120_THROTTLE_SET_TO_1_3_POWER,
				player_idx);
			break;
		case FLIGHT_KEY_BACKSLASH:
			craft->throttle_speed = 0;
			XVT_LOG_DEBUG(
				"flight.throttle_set slot=%d key=%u throttle=%u predicted=%d",
				player_idx, (unsigned)g_current_action_key,
				(unsigned)craft->throttle_speed,
				g_flight_sim_side_effects_suppressed);
			fsfx_play_sound(FLIGHT_SOUND_SMALL_CLICK, -1,
					player_idx);
			msg_emit_in_flight_message(
				IFMSG_119_THROTTLE_SET_TO_NO_POWER, player_idx);
			break;
		case FLIGHT_KEY_RIGHT_BRACKET:
			craft->throttle_speed = (uint16_t)-21846;
			XVT_LOG_DEBUG(
				"flight.throttle_set slot=%d key=%u throttle=%u predicted=%d",
				player_idx, (unsigned)g_current_action_key,
				(unsigned)craft->throttle_speed,
				g_flight_sim_side_effects_suppressed);
			fsfx_play_sound(FLIGHT_SOUND_SMALL_CLICK, -1,
					player_idx);
			msg_emit_in_flight_message(
				IFMSG_121_THROTTLE_SET_TO_2_3_POWER,
				player_idx);
			break;
		case FLIGHT_KEY_B:
			if ((craft->system_flags &
			     CRAFT_SUBSYSTEM_FLAG_BEAM_SYSTEM) != 0) {
				if ((craft->working_subsystems &
				     CRAFT_SUBSYSTEM_FLAG_BEAM_SYSTEM) != 0) {
					if (craft->beam_active != 0) {
						craft->beam_active = 0;
						craft->beam_output = 0;
						XVT_LOG_DEBUG(
							"flight.beam_switched slot=%d on=%d beam=%d charge=%u predicted=%d",
							player_idx,
							(int)craft->beam_active,
							(int)craft
								->beam_type_id,
							(unsigned)craft
								->beam_charge,
							g_flight_sim_side_effects_suppressed);
						fsfx_play_sound(
							FLIGHT_SOUND_SMALL_CLICK,
							-1, player_idx);
						msg_emit_in_flight_message(
							(in_flight_message_id)(craft->beam_type_id +
									       242),
							player_idx);
					} else if (craft->beam_charge != 0) {
						craft->beam_active = 1;
						craft->beam_output = -1;
						XVT_LOG_DEBUG(
							"flight.beam_switched slot=%d on=%d beam=%d charge=%u predicted=%d",
							player_idx,
							(int)craft->beam_active,
							(int)craft
								->beam_type_id,
							(unsigned)craft
								->beam_charge,
							g_flight_sim_side_effects_suppressed);
						if (g_players[player_idx]
								    .current_target_object_idx !=
							    -1 ||
						    craft->beam_type_id ==
							    BEAM_TYPE_DECOY) {
							msg_emit_in_flight_message(
								(in_flight_message_id)(craft->beam_type_id +
										       236),
								player_idx);
						} else {
							msg_emit_in_flight_message(
								(in_flight_message_id)(craft->beam_type_id +
										       248),
								player_idx);
						}

						switch (craft->beam_type_id) {
						case BEAM_TYPE_TRACTOR:
							fsfx_play_sound(
								FLIGHT_SOUND_TRACTOR_FIRE,
								-1, player_idx);
							break;
						case BEAM_TYPE_JAMMING:
							fsfx_play_sound(
								FLIGHT_SOUND_JAMMING_FIRE,
								-1, player_idx);
							break;
						case BEAM_TYPE_DECOY:
							fsfx_play_sound(
								FLIGHT_SOUND_DECOY_FIRE,
								-1, player_idx);
							break;
						case BEAM_TYPE_ENERGY_TRANSFER:
							fsfx_play_sound(
								FLIGHT_SOUND_ENERGY_TRANSFER_FIRE,
								-1, player_idx);
							break;
						default:
							break;
						}
					} else {
						fsfx_play_sound(
							FLIGHT_SOUND_SMALL_CLICK,
							-1, player_idx);
						msg_emit_in_flight_message(
							IFMSG_255_NO_ENERGY_FOR_BEAM_TO_ACTIVATE,
							player_idx);
					}
				} else {
					fsfx_play_sound(
						FLIGHT_SOUND_SMALL_CLICK, -1,
						player_idx);
					g_msg_arg_table[0] = 95;
					g_msg_arg_table[1] = 87;
					msg_emit_in_flight_message(
						IFMSG_086_ARG_SYSTEM_IS_ARG,
						player_idx);
				}
			} else {
				msg_emit_in_flight_message(
					IFMSG_227_YOUR_CRAFT_DOES_NOT_HAVE_A_BEAM_SYSTEM,
					player_idx);
			}
			break;
		case FLIGHT_KEY_C:
			if ((craft->working_subsystems &
			     CRAFT_SUBSYSTEM_FLAG_COUNTERMEASURES) != 0) {
				if (craft->cm_fire_cooldown_timer == 0) {
					if (craft->cm_type_id !=
					    COUNTERMEASURE_TYPE_NONE) {
						if (craft->cm_ammo_count != 0) {
							if (craft->cm_type_id ==
							    COUNTERMEASURE_TYPE_CHAFF) {
								craft->chaff_active_seconds +=
									10;
								msg_emit_in_flight_message(
									IFMSG_367_CHAFF_BURST_TRIGGERED,
									player_idx);
								if (g_mission_flight_groups
										    [g_object_table
											     [g_players[player_idx]
												      .object_index]
												     .flight_group_idx]
											    .fg
											    .status1 !=
									    21 &&
								    g_mission_flight_groups
										    [g_object_table
											     [g_players[player_idx]
												      .object_index]
												     .flight_group_idx]
											    .fg
											    .status2 !=
									    21) {
									--craft->cm_ammo_count;
								}
								XVT_LOG_DEBUG(
									"flight.countermeasure_used slot=%d cm=%d ammo=%d seconds=%u predicted=%d",
									player_idx,
									(int)craft
										->cm_type_id,
									(int)craft
										->cm_ammo_count,
									(unsigned)craft
										->chaff_active_seconds,
									g_flight_sim_side_effects_suppressed);
								fsfx_play_sound(
									FLIGHT_SOUND_CHAFF_TRIGGER,
									-1,
									player_idx);
							} else if (
								laser_createcountermeasureprojectile(
									object_index,
									COUNTERMEASURE_PROJECTILE_OBJECT_TYPE) !=
								-1) {
								XVT_LOG_DEBUG(
									"flight.countermeasure_used slot=%d cm=%d ammo=%d seconds=%u predicted=%d",
									player_idx,
									(int)craft
										->cm_type_id,
									(int)craft
										->cm_ammo_count,
									(unsigned)craft
										->chaff_active_seconds,
									g_flight_sim_side_effects_suppressed);
								msg_emit_in_flight_message(
									IFMSG_370_FLARE_FIRED,
									player_idx);
								fsfx_play_sound(
									FLIGHT_SOUND_COUNTERMEASURE_FLARE,
									-1,
									player_idx);
							}
						} else {
							msg_emit_in_flight_message(
								(in_flight_message_id)(craft->cm_type_id +
										       362),
								player_idx);
							fsfx_play_sound(
								FLIGHT_SOUND_SMALL_CLICK,
								-1, player_idx);
						}
					} else {
						msg_emit_in_flight_message(
							IFMSG_362_NO_COUNTERMEASURES_LOADED,
							player_idx);
						fsfx_play_sound(
							FLIGHT_SOUND_SMALL_CLICK,
							-1, player_idx);
					}
				}
			} else {
				fsfx_play_sound(FLIGHT_SOUND_SMALL_CLICK, -1,
						player_idx);
				g_msg_arg_table[0] = 97;
				g_msg_arg_table[1] = 89;
				msg_emit_in_flight_message(
					IFMSG_086_ARG_SYSTEM_IS_ARG,
					player_idx);
			}
			break;
		case FLIGHT_KEY_H:
			player_handle_hyperspace_command(craft, player_idx);
			break;
		case FLIGHT_KEY_J:
			if (g_flight_sim_side_effects_suppressed == 0) {
				if (g_flight_mission_state
					    .craft_jumping_enabled != 0) {
					if (player_unbind_from_current_craft(
						    player_idx, 1, 1) != 0) {
						player_bind_to_available_craft(
							player_idx,
							(uint16_t)object_index,
							0, 0);
						XVT_LOG_DEBUG(
							"flight.craft_jumped slot=%d previous=%d object=%d predicted=%d",
							player_idx,
							object_index,
							g_players[player_idx]
								.object_index,
							g_flight_sim_side_effects_suppressed);
						if (g_players[player_idx]
							    .object_index ==
						    -1) {
							XVT_LOG_WARN(
								"flight.craft_jump_unbound slot=%d previous=%d",
								player_idx,
								object_index);
						}
						if (player_idx ==
						    g_local_player) {
							msg_emit_local_player_craft_message(
								IFMSG_289_YOU_ARE_NOW_PILOTING_ARG_ARG_ARG);
						}
					} else {
						msg_emit_in_flight_message(
							IFMSG_293_NO_OTHER_CRAFT_TO_PILOT,
							player_idx);
					}
				} else {
					msg_emit_in_flight_message(
						IFMSG_294_CRAFT_JUMPING_NOT_ENABLED_FOR_THIS_MISSION,
						player_idx);
				}
			}
			break;
		case FLIGHT_KEY_N: {
			if (get_model_index_from_type(
				    g_object_table[object_index].object_type) ==
			    get_model_index_from_type(
				    special_slam_craft_type)) {
				craft->engine_overdrive_off =
					(uint16_t)~craft->engine_overdrive_off;
				if (craft->engine_overdrive_off == 0) {
					int16_t have_laser_energy = 0;
					for (unsigned int slot_index = 0;
					     slot_index <
					     craft->laser_slot_count;
					     ++slot_index) {
						if (craft
							    ->weapon_slots
								    [slot_index]
							    .laser_charge > 0) {
							have_laser_energy = 1;
						}
					}
					if (have_laser_energy != 0) {
						msg_emit_in_flight_message(
							IFMSG_284_ENGINE_OVERDRIVE_BOOSTERS_ENGAGED,
							player_idx);
						fsfx_play_sound(
							FLIGHT_SOUND_POWER_UP,
							-1, player_idx);
					} else {
						craft->engine_overdrive_off =
							UINT16_MAX;
						msg_emit_in_flight_message(
							IFMSG_286_ENGINE_OVERDRIVE_BOOSTERS_CANNOT_BE_ENGAGED,
							player_idx);
						fsfx_play_sound(
							FLIGHT_SOUND_SMALL_CLICK,
							-1, player_idx);
					}
				} else {
					msg_emit_in_flight_message(
						IFMSG_285_ENGINE_OVERDRIVE_BOOSTERS_DISENGAGED,
						player_idx);
					fsfx_play_sound(FLIGHT_SOUND_POWER_DOWN,
							-1, player_idx);
				}
				XVT_LOG_DEBUG(
					"flight.overdrive_switched slot=%d on=%d predicted=%d",
					player_idx,
					craft->engine_overdrive_off == 0,
					g_flight_sim_side_effects_suppressed);
			} else {
				msg_emit_in_flight_message(
					IFMSG_229_YOUR_CRAFT_DOES_NOT_HAVE_A_SLAM_SYSTEM,
					player_idx);
			}
			break;
		}
		case FLIGHT_KEY_S:
			if ((craft->system_flags &
			     CRAFT_SUBSYSTEM_FLAG_SHIELDS) != 0) {
				if ((craft->working_subsystems &
				     CRAFT_SUBSYSTEM_FLAG_SHIELDS) != 0) {
					++craft->shield_distrib_mode;
					if (craft->shield_distrib_mode >
					    SHIELD_DISTRIBUTION_FULLY_AFT) {
						craft->shield_distrib_mode =
							SHIELD_DISTRIBUTION_FULLY_FORWARD;
						player_transfer_shield_bank_energy(
							0, 1, player_idx);
					} else if (
						craft->shield_distrib_mode ==
						SHIELD_DISTRIBUTION_FULLY_AFT) {
						player_transfer_shield_bank_energy(
							1, 0, player_idx);
					} else {
						uint16_t front_percent = math2_longratio_q16(
							g_model_defs
								[get_model_index_from_type(
									 g_object_table
										 [g_players[player_idx]
											  .object_index]
											 .object_type)]
									.shield_strength,
							craft_get_object_max_shield(
								(uint16_t)g_players[player_idx]
									.object_index));
						int16_t total_shield =
							craft->shield_energy
								[1] +
							craft->shield_energy[0];
						if (total_shield > 0) {
							craft->shield_energy
								[0] = math2_fraction(
								total_shield,
								front_percent);
							craft->shield_energy
								[1] =
								total_shield -
								craft->shield_energy
									[0];
						}
					}
					XVT_LOG_DEBUG(
						"flight.shields_distributed slot=%d mode=%d front=%d rear=%d predicted=%d",
						player_idx,
						(int)craft->shield_distrib_mode,
						craft->shield_energy[0],
						craft->shield_energy[1],
						g_flight_sim_side_effects_suppressed);
					msg_emit_in_flight_message(
						(in_flight_message_id)(craft->shield_distrib_mode +
								       68),
						player_idx);
					fsfx_play_sound(
						FLIGHT_SOUND_CONFIRM_BEEP, -1,
						player_idx);
				} else {
					fsfx_play_sound(
						FLIGHT_SOUND_SMALL_CLICK, -1,
						player_idx);
					g_msg_arg_table[0] = 98;
					g_msg_arg_table[1] = 87;
					msg_emit_in_flight_message(
						IFMSG_086_ARG_SYSTEM_IS_ARG,
						player_idx);
				}
			} else {
				fsfx_play_sound(FLIGHT_SOUND_SMALL_CLICK, -1,
						player_idx);
				msg_emit_in_flight_message(
					IFMSG_226_YOUR_CRAFT_DOES_NOT_HAVE_A_SHIELD_SYSTEM,
					player_idx);
			}
			break;
		case FLIGHT_KEY_V:
			if (g_object_table[g_players[player_idx].object_index]
					    .object_type ==
				    CRAFT_SPECIES_X_WING ||
			    g_object_table[g_players[player_idx].object_index]
					    .object_type ==
				    CRAFT_SPECIES_B_WING) {
				g_object_table[g_players[player_idx]
						       .object_index]
					.mobj->p_craft->s_foil_state ^= 2;
				g_object_table[g_players[player_idx]
						       .object_index]
					.mobj->p_craft->s_foil_state |= 1;
				XVT_LOG_DEBUG(
					"flight.sfoils_switched slot=%d foils=%d predicted=%d",
					player_idx,
					(int)g_object_table
						[g_players[player_idx]
							 .object_index]
							.mobj->p_craft
							->s_foil_state,
					g_flight_sim_side_effects_suppressed);
				fsfx_play_sound(FLIGHT_SOUND_S_FOIL, -1,
						player_idx);
				if ((g_object_table[g_players[player_idx]
							    .object_index]
					     .mobj->p_craft->s_foil_state &
				     2) != 0) {
					msg_emit_in_flight_message(
						IFMSG_127_S_FOILS_CLOSING,
						player_idx);
				} else {
					msg_emit_in_flight_message(
						IFMSG_126_S_FOILS_OPENING,
						player_idx);
				}
			} else {
				msg_emit_in_flight_message(
					IFMSG_228_YOUR_CRAFT_DOES_NOT_HAVE_S_FOILS,
					player_idx);
			}
			break;
		case FLIGHT_KEY_W: {
			uint8_t selection =
				g_players[player_idx].selected_weapon_bank + 1;
			g_players[player_idx].selected_weapon_bank = selection;
			if (g_players[player_idx].selected_weapon_mode == 0) {
				if (craft->cannon_group_count <= selection) {
					if (craft->warhead_launcher_count !=
					    0) {
						int first_slot =
							g_model_defs[craft->model_index]
								.warhead_launcher_first_slot
									[0];
						if (craft->weapon_slots[first_slot]
								    .ammo_count +
							    craft->weapon_slots
								    [first_slot +
								     1]
									    .ammo_count !=
						    0) {
							int8_t flags =
								craft->warhead_launcher_flags
									[0];
							g_players[player_idx]
								.selected_weapon_mode =
								1;
							g_players[player_idx]
								.missile_lock_state =
								0;
							craft->warhead_lock_ticks =
								0;
							if ((flags & 0x7F) !=
							    3) {
								if (flags < 0) {
									if (craft->weapon_slots
										    [first_slot +
										     1]
											    .ammo_count <
									    craft
										    ->weapon_slots
											    [first_slot]
										    .ammo_count) {
										craft->warhead_launcher_flags
											[0] =
											(int8_t)(flags &
												 0x7F);
									}
								} else {
									if (craft->weapon_slots
										    [first_slot +
										     1]
											    .ammo_count >
									    craft
										    ->weapon_slots
											    [first_slot]
										    .ammo_count) {
										craft->warhead_launcher_flags
											[0] =
											(int8_t)(flags |
												 0x80);
									}
								}
							}
						}
					}
					g_players[player_idx]
						.selected_weapon_bank = 0;
				}
			} else if (craft->warhead_launcher_count <= selection) {
				if (craft->cannon_group_count != 0) {
					g_players[player_idx]
						.selected_weapon_mode = 0;
				}
				g_players[player_idx].selected_weapon_bank = 0;
			}
			XVT_LOG_DEBUG(
				"flight.weapon_selected slot=%d mode=%d bank=%d flags=%d predicted=%d",
				player_idx,
				(int)g_players[player_idx].selected_weapon_mode,
				(int)g_players[player_idx].selected_weapon_bank,
				(int)craft->warhead_launcher_flags[0],
				g_flight_sim_side_effects_suppressed);

			g_msg_arg_table[1] = 87;
			if (g_players[player_idx].selected_weapon_mode != 0) {
				if ((craft->working_subsystems &
				     CRAFT_SUBSYSTEM_FLAG_WARHEAD_LAUNCHER) !=
				    0) {
					int warhead_kind = object_type_get_warhead_kind_index(
						craft->warhead_slot_type_ids
							[g_players[player_idx]
								 .selected_weapon_bank]);
					msg_emit_in_flight_message(
						(in_flight_message_id)(warhead_kind +
								       8),
						player_idx);
					fsfx_play_sound(
						FLIGHT_SOUND_SETTING_MEDIUM, -1,
						player_idx);
				} else {
					g_msg_arg_table[0] = 94;
					msg_emit_in_flight_message(
						IFMSG_086_ARG_SYSTEM_IS_ARG,
						player_idx);
					fsfx_play_sound(
						FLIGHT_SOUND_SETTING_OFF, -1,
						player_idx);
				}
			} else if ((craft->working_subsystems &
				    CRAFT_SUBSYSTEM_FLAG_CANNONS) != 0) {
				msg_emit_in_flight_message(
					(in_flight_message_id)(g_players[player_idx]
								       .selected_weapon_bank +
							       3),
					player_idx);
				fsfx_play_sound(
					g_players[player_idx].selected_weapon_bank !=
							0
						? FLIGHT_SOUND_SETTING_LOW
						: FLIGHT_SOUND_SETTING_VERY_LOW,
					-1, player_idx);
			} else {
				g_msg_arg_table[0] =
					g_players[player_idx]
						.selected_weapon_bank +
					92;
				msg_emit_in_flight_message(
					IFMSG_086_ARG_SYSTEM_IS_ARG,
					player_idx);
				fsfx_play_sound(FLIGHT_SOUND_SETTING_OFF, -1,
						player_idx);
			}
			break;
		}
		case FLIGHT_KEY_X:
			if (g_players[player_idx].selected_weapon_mode == 0) {
				if (g_model_defs[get_model_index_from_type(
							 g_object_table[object_index]
								 .object_type)]
					    .laser_group_slot_count
						    [g_players[player_idx]
							     .selected_weapon_bank] !=
				    1) {
					uint16_t link_mode =
						craft->laser_state.link_mode
							[g_players[player_idx]
								 .selected_weapon_bank] +
						1;
					if (link_mode > 3) {
						link_mode = 1;
					}
					if (g_model_defs[get_model_index_from_type(
								 g_object_table
									 [g_players[player_idx]
										  .object_index]
										 .object_type)]
							    .laser_group_slot_count
								    [g_players[player_idx]
									     .selected_weapon_bank] !=
						    4 &&
					    link_mode == 2) {
						link_mode = 3;
					}
					craft->laser_state.link_mode
						[g_players[player_idx]
							 .selected_weapon_bank] =
						(uint8_t)link_mode;
					craft->laser_state.next_slot
						[g_players[player_idx]
							 .selected_weapon_bank] =
						g_model_defs
							[get_model_index_from_type(
								 g_object_table
									 [g_players[player_idx]
										  .object_index]
										 .object_type)]
								.laser_group_first_slot
									[g_players[player_idx]
										 .selected_weapon_bank];
					XVT_LOG_DEBUG(
						"flight.laser_link_set slot=%d bank=%d link=%d predicted=%d",
						player_idx,
						(int)g_players[player_idx]
							.selected_weapon_bank,
						(int)craft->laser_state.link_mode
							[g_players[player_idx]
								 .selected_weapon_bank],
						g_flight_sim_side_effects_suppressed);
					if (player_idx == g_local_player) {
						msg_emit_in_flight_message(
							(in_flight_message_id)(link_mode +
									       4),
							player_idx);
					}
				}
			} else {
				craft->warhead_launcher_flags
					[g_players[player_idx]
						 .selected_weapon_bank] ^= 2;
				XVT_LOG_DEBUG(
					"flight.warhead_link_set slot=%d bank=%d flags=%d predicted=%d",
					player_idx,
					(int)g_players[player_idx]
						.selected_weapon_bank,
					(int)craft->warhead_launcher_flags
						[g_players[player_idx]
							 .selected_weapon_bank],
					g_flight_sim_side_effects_suppressed);
				uint16_t warhead_kind = object_type_get_warhead_kind_index(
					craft->warhead_slot_type_ids
						[g_players[player_idx]
							 .selected_weapon_bank]);
				if ((craft->warhead_launcher_flags
					     [g_players[player_idx]
						      .selected_weapon_bank] &
				     2) != 0) {
					msg_emit_in_flight_message(
						(in_flight_message_id)(warhead_kind +
								       28),
						player_idx);
				} else {
					msg_emit_in_flight_message(
						(in_flight_message_id)(warhead_kind +
								       18),
						player_idx);
				}
			}
			fsfx_play_sound(FLIGHT_SOUND_CONFIRM_BEEP, -1,
					player_idx);
			break;
		case FLIGHT_KEY_Z:
			if (g_flight_sim_side_effects_suppressed == 0) {
				struct player_view_state *view =
					&g_players[player_idx].view_state;
				if (view->target_camera_active != 0) {
					view->target_camera_active = 0;
					view->camera_focus_obj_idx =
						(uint16_t)g_players[player_idx]
							.object_index;
					view->external_camera_active = 0;
					view->player_input_blocked = 0;
					if (player_idx == g_local_player) {
						g_hud_cached_target_object_idx =
							-2;
						g_render_object_ref_flags = 0;
					}
					hud_set_hud_view_state(
						g_players[player_idx].saved_hud_view_state ==
								HUD_VIEW_HUD_ONLY
							? HUD_VIEW_HUD_ONLY
							: HUD_VIEW_FORWARD,
						player_idx);
					view->hud_aim_x = 0;
					view->hud_aim_y = 0;
					XVT_LOG_DEBUG(
						"flight.view_set slot=%d key=%u hud=%d focus=%d external=%d camera=%d blocked=%d aim_x=%d aim_y=%d predicted=%d",
						player_idx,
						(unsigned)g_current_action_key,
						(int)g_players[player_idx]
							.view_state
							.hud_state_live,
						(int)g_players[player_idx]
							.view_state
							.camera_focus_obj_idx,
						(int)g_players[player_idx]
							.view_state
							.external_camera_active,
						(int)g_players[player_idx]
							.view_state
							.target_camera_active,
						(int)g_players[player_idx]
							.view_state
							.player_input_blocked,
						(int)g_players[player_idx]
							.view_state.hud_aim_x,
						(int)g_players[player_idx]
							.view_state.hud_aim_y,
						g_flight_sim_side_effects_suppressed);
				} else if (g_players[player_idx]
						   .current_target_object_idx ==
					   -1) {
					msg_emit_in_flight_message(
						IFMSG_223_NO_CRAFT_TARGETED,
						player_idx);
				} else {
					if (view->external_camera_active == 0) {
						view->saved_hud_state_byte =
							view->hud_state_live;
						view->saved_hud_aim_x =
							view->hud_aim_x;
						view->saved_hud_aim_y =
							view->hud_aim_y;
					}
					view->external_camera_active = 1;
					view->target_camera_active = 1;
					view->camera_focus_obj_idx =
						(uint16_t)g_players[player_idx]
							.current_target_object_idx;
					if (player_idx == g_local_player) {
						g_render_object_ref_flags =
							1024;
					}
					player_update_hud_view_for_camera_focus(
						player_idx);
					XVT_LOG_DEBUG(
						"flight.view_set slot=%d key=%u hud=%d focus=%d external=%d camera=%d blocked=%d aim_x=%d aim_y=%d predicted=%d",
						player_idx,
						(unsigned)g_current_action_key,
						(int)g_players[player_idx]
							.view_state
							.hud_state_live,
						(int)g_players[player_idx]
							.view_state
							.camera_focus_obj_idx,
						(int)g_players[player_idx]
							.view_state
							.external_camera_active,
						(int)g_players[player_idx]
							.view_state
							.target_camera_active,
						(int)g_players[player_idx]
							.view_state
							.player_input_blocked,
						(int)g_players[player_idx]
							.view_state.hud_aim_x,
						(int)g_players[player_idx]
							.view_state.hud_aim_y,
						g_flight_sim_side_effects_suppressed);
				}
			}
			break;
		case FLIGHT_KEY_ALT_E:
			if (g_flight_sim_side_effects_suppressed == 0) {
				if (g_flight_mission_state
					    .proving_grounds_mode_active != 0) {
					g_flight_mission_state
						.mission_end_pending = 1;
					g_players[player_idx]
						.participation_state = 2;
					XVT_LOG_INFO(
						"flight.mission_ending reason=\"proving_grounds_left\" slot=%d tick=%d",
						player_idx, g_game_time);
				} else {
					player_save_craft_settings(player_idx);
					if (g_players[player_idx]
							    .hyperspace_phase !=
						    0 ||
					    g_replay_view_mode != 0 ||
					    g_players[player_idx]
							    .map_camera_state !=
						    0) {
						fsfx_play_sound(
							FLIGHT_SOUND_SMALL_CLICK,
							-1, player_idx);
					} else {
						int source_player_idx =
							mission_record_player_craft_loss(
								object_index,
								1);
						int tumble_rate =
							(game_rand() & 0x3FFF) +
							0x2000;
						player_start_post_destruction_state(
							player_idx, UINT32_MAX,
							source_player_idx);
						while (tumble_rate >
						       g_model_defs[craft->model_index]
							       .max_tumble_rate) {
							tumble_rate >>= 1;
						}
						g_object_table
							[g_players[player_idx]
								 .object_index]
								.mobj
								->roll_impulse_rate =
							(int16_t)tumble_rate;
						craft->object_kind =
							CRAFT_OBJECT_KIND_BREAKING_UP;
						g_object_table
							[g_players[player_idx]
								 .object_index]
								.mobj
								->lifetime_timer =
							472;
						XVT_LOG_INFO(
							"flight.player_ejected slot=%d object=%d by=%d tick=%d",
							player_idx,
							object_index,
							source_player_idx,
							g_game_time);
						XVT_LOG_DEBUG(
							"flight.ejection_tumble slot=%d object=%d tumble=%d predicted=%d",
							player_idx,
							object_index,
							tumble_rate,
							g_flight_sim_side_effects_suppressed);
					}
				}
			}
			break;
		case FLIGHT_KEY_ALT_1:
			if (g_players[player_idx].map_camera_state != 0) {
				player_set_target(
					flight_map_pick_object_nearest_screen_center(
						player_idx),
					player_idx);
			} else if (
				g_flight_mission_state
						.proving_grounds_mode_active ==
					0 &&
				g_players[player_idx]
						.view_state
						.player_input_blocked == 0) {
				player_set_target(
					player_pick_target_in_sight(player_idx),
					player_idx);
			}
			break;
		case FLIGHT_KEY_ALT_2:
			if (g_players[player_idx]
				    .view_state.player_input_blocked == 0) {
				laser_fireplayerweapon(player_idx);
			}
			break;
		case FLIGHT_KEY_PAD_0:
			if (g_flight_sim_side_effects_suppressed == 0 &&
			    (g_players[player_idx].view_state.hud_state_live <
				     16 ||
			     g_players[player_idx]
					     .view_state
					     .external_camera_active != 0)) {
				struct player_view_state *view =
					&g_players[player_idx].view_state;
				view->hud_aim_x_snap_state ^= 8;
				view->hud_aim_x = view->hud_aim_x_snap_state
						  << 10;
				if (view->external_camera_active == 0 &&
				    view->camera_focus_obj_idx ==
					    g_players[player_idx]
						    .object_index) {
					hud_set_hud_view_state(
						view->hud_state_live ^ 8,
						player_idx);
				}
				XVT_LOG_DEBUG(
					"flight.view_set slot=%d key=%u hud=%d focus=%d external=%d camera=%d blocked=%d aim_x=%d aim_y=%d predicted=%d",
					player_idx,
					(unsigned)g_current_action_key,
					(int)g_players[player_idx]
						.view_state.hud_state_live,
					(int)g_players[player_idx]
						.view_state
						.camera_focus_obj_idx,
					(int)g_players[player_idx]
						.view_state
						.external_camera_active,
					(int)g_players[player_idx]
						.view_state
						.target_camera_active,
					(int)g_players[player_idx]
						.view_state
						.player_input_blocked,
					(int)g_players[player_idx]
						.view_state.hud_aim_x,
					(int)g_players[player_idx]
						.view_state.hud_aim_y,
					g_flight_sim_side_effects_suppressed);
			}
			break;
		case FLIGHT_KEY_PAD_1:
		case FLIGHT_KEY_PAD_2:
		case FLIGHT_KEY_PAD_3:
		case FLIGHT_KEY_PAD_4:
		case FLIGHT_KEY_PAD_6:
		case FLIGHT_KEY_PAD_7:
		case FLIGHT_KEY_PAD_8:
		case FLIGHT_KEY_PAD_9:
			if (g_flight_sim_side_effects_suppressed == 0) {
				int look_index =
					g_current_action_key - FLIGHT_KEY_PAD_1;
				struct player_view_state *view =
					&g_players[player_idx].view_state;
				if (view->external_camera_active == 0 &&
				    view->camera_focus_obj_idx ==
					    g_players[player_idx]
						    .object_index) {
					int hud_state =
						view->hud_aim_x_snap_state +
						g_hud_view_state_offset_by_look_action
							[look_index];
					if (hud_state == 0) {
						hud_state =
							g_players[player_idx]
								.saved_hud_view_state;
					}
					hud_set_hud_view_state(
						(uint8_t)hud_state, player_idx);
				}
				view->hud_aim_x = view->hud_aim_x_snap_state
						  << 10;
				view->hud_aim_y =
					g_hud_aim_y_by_look_action[look_index];
				XVT_LOG_DEBUG(
					"flight.view_set slot=%d key=%u hud=%d focus=%d external=%d camera=%d blocked=%d aim_x=%d aim_y=%d predicted=%d",
					player_idx,
					(unsigned)g_current_action_key,
					(int)g_players[player_idx]
						.view_state.hud_state_live,
					(int)g_players[player_idx]
						.view_state
						.camera_focus_obj_idx,
					(int)g_players[player_idx]
						.view_state
						.external_camera_active,
					(int)g_players[player_idx]
						.view_state
						.target_camera_active,
					(int)g_players[player_idx]
						.view_state
						.player_input_blocked,
					(int)g_players[player_idx]
						.view_state.hud_aim_x,
					(int)g_players[player_idx]
						.view_state.hud_aim_y,
					g_flight_sim_side_effects_suppressed);
			}
			break;
		case FLIGHT_KEY_PAD_5:
			if (g_flight_sim_side_effects_suppressed == 0) {
				g_players[player_idx].view_state.hud_aim_x =
					0x4000;
				g_players[player_idx].view_state.hud_aim_y = 0;
				if (g_players[player_idx]
						    .view_state
						    .external_camera_active ==
					    0 &&
				    g_players[player_idx]
						    .view_state
						    .camera_focus_obj_idx ==
					    g_players[player_idx]
						    .object_index) {
					hud_set_hud_view_state(16, player_idx);
				}
				XVT_LOG_DEBUG(
					"flight.view_set slot=%d key=%u hud=%d focus=%d external=%d camera=%d blocked=%d aim_x=%d aim_y=%d predicted=%d",
					player_idx,
					(unsigned)g_current_action_key,
					(int)g_players[player_idx]
						.view_state.hud_state_live,
					(int)g_players[player_idx]
						.view_state
						.camera_focus_obj_idx,
					(int)g_players[player_idx]
						.view_state
						.external_camera_active,
					(int)g_players[player_idx]
						.view_state
						.target_camera_active,
					(int)g_players[player_idx]
						.view_state
						.player_input_blocked,
					(int)g_players[player_idx]
						.view_state.hud_aim_x,
					(int)g_players[player_idx]
						.view_state.hud_aim_y,
					g_flight_sim_side_effects_suppressed);
			}
			break;
		case FLIGHT_KEY_F8:
			if ((craft->system_flags &
			     CRAFT_SUBSYSTEM_FLAG_BEAM_SYSTEM) != 0) {
				if ((craft->working_subsystems &
				     CRAFT_SUBSYSTEM_FLAG_BEAM_SYSTEM) != 0) {
					if (++craft->beam_recharge_level >=
					    POWER_RECHARGE_LEVEL_COUNT) {
						craft->beam_recharge_level =
							POWER_RECHARGE_FULLY_REDIRECTED_TO_ENGINES;
					}
					XVT_LOG_DEBUG(
						"flight.recharge_set slot=%d system=\"beam\" level=%d predicted=%d",
						player_idx,
						(int)craft->beam_recharge_level,
						g_flight_sim_side_effects_suppressed);
					msg_emit_in_flight_message(
						(in_flight_message_id)(craft->beam_recharge_level +
								       81),
						player_idx);
					fsfx_play_sound(
						(unsigned int)(craft->beam_recharge_level +
							       FLIGHT_SOUND_SETTING_OFF),
						-1, player_idx);
				} else {
					g_msg_arg_table[0] = 95;
					g_msg_arg_table[1] = 87;
					msg_emit_in_flight_message(
						IFMSG_086_ARG_SYSTEM_IS_ARG,
						player_idx);
					fsfx_play_sound(
						FLIGHT_SOUND_SMALL_CLICK, -1,
						player_idx);
				}
			} else {
				msg_emit_in_flight_message(
					IFMSG_227_YOUR_CRAFT_DOES_NOT_HAVE_A_BEAM_SYSTEM,
					player_idx);
				fsfx_play_sound(FLIGHT_SOUND_SMALL_CLICK, -1,
						player_idx);
			}
			break;
		case FLIGHT_KEY_F9:
			if (++craft->laser_recharge_level >=
			    POWER_RECHARGE_LEVEL_COUNT) {
				craft->laser_recharge_level =
					POWER_RECHARGE_FULLY_REDIRECTED_TO_ENGINES;
			}
			XVT_LOG_DEBUG(
				"flight.recharge_set slot=%d system=\"lasers\" level=%d predicted=%d",
				player_idx, (int)craft->laser_recharge_level,
				g_flight_sim_side_effects_suppressed);
			if (player_idx == g_local_player) {
				if ((craft->working_subsystems &
				     CRAFT_SUBSYSTEM_FLAG_CANNONS) != 0) {
					msg_emit_in_flight_message(
						(in_flight_message_id)(craft->laser_recharge_level +
								       71),
						player_idx);
				} else {
					g_msg_arg_table[0] = 92;
					g_msg_arg_table[1] = 87;
					msg_emit_in_flight_message(
						IFMSG_086_ARG_SYSTEM_IS_ARG,
						player_idx);
				}
				fsfx_play_sound(
					(unsigned int)(craft->laser_recharge_level +
						       FLIGHT_SOUND_SETTING_OFF),
					-1, player_idx);
			}
			break;
		case FLIGHT_KEY_F10:
			if ((craft->system_flags &
			     CRAFT_SUBSYSTEM_FLAG_SHIELDS) != 0) {
				if ((craft->working_subsystems &
				     CRAFT_SUBSYSTEM_FLAG_SHIELDS) != 0) {
					if (++craft->shield_recharge_level >=
					    POWER_RECHARGE_LEVEL_COUNT) {
						craft->shield_recharge_level =
							POWER_RECHARGE_FULLY_REDIRECTED_TO_ENGINES;
					}
					XVT_LOG_DEBUG(
						"flight.recharge_set slot=%d system=\"shields\" level=%d predicted=%d",
						player_idx,
						(int)craft
							->shield_recharge_level,
						g_flight_sim_side_effects_suppressed);
					msg_emit_in_flight_message(
						(in_flight_message_id)(craft->shield_recharge_level +
								       76),
						player_idx);
					fsfx_play_sound(
						(unsigned int)(craft->shield_recharge_level +
							       FLIGHT_SOUND_SETTING_OFF),
						-1, player_idx);
				} else {
					g_msg_arg_table[0] = 98;
					g_msg_arg_table[1] = 87;
					msg_emit_in_flight_message(
						IFMSG_086_ARG_SYSTEM_IS_ARG,
						player_idx);
					fsfx_play_sound(
						FLIGHT_SOUND_SMALL_CLICK, -1,
						player_idx);
				}
			} else {
				msg_emit_in_flight_message(
					IFMSG_226_YOUR_CRAFT_DOES_NOT_HAVE_A_SHIELD_SYSTEM,
					player_idx);
				fsfx_play_sound(FLIGHT_SOUND_SMALL_CLICK, -1,
						player_idx);
			}
			break;
		case FLIGHT_KEY_THROTTLE_1:
		case FLIGHT_KEY_THROTTLE_2:
		case FLIGHT_KEY_THROTTLE_3:
		case FLIGHT_KEY_THROTTLE_4:
			craft->throttle_speed =
				(uint16_t)((g_current_action_key + 6) << 12);
			XVT_LOG_DEBUG(
				"flight.throttle_set slot=%d key=%u throttle=%u predicted=%d",
				player_idx, (unsigned)g_current_action_key,
				(unsigned)craft->throttle_speed,
				g_flight_sim_side_effects_suppressed);
			break;
		case FLIGHT_KEY_THROTTLE_6:
		case FLIGHT_KEY_THROTTLE_7:
		case FLIGHT_KEY_THROTTLE_8:
		case FLIGHT_KEY_THROTTLE_9:
		case FLIGHT_KEY_THROTTLE_10:
			craft->throttle_speed =
				(uint16_t)((g_current_action_key + 7) << 12);
			XVT_LOG_DEBUG(
				"flight.throttle_set slot=%d key=%u throttle=%u predicted=%d",
				player_idx, (unsigned)g_current_action_key,
				(unsigned)craft->throttle_speed,
				g_flight_sim_side_effects_suppressed);
			break;
		case FLIGHT_KEY_THROTTLE_11:
		case FLIGHT_KEY_THROTTLE_12:
		case FLIGHT_KEY_THROTTLE_13:
		case FLIGHT_KEY_THROTTLE_14:
			craft->throttle_speed =
				(uint16_t)((g_current_action_key + 8) << 12);
			XVT_LOG_DEBUG(
				"flight.throttle_set slot=%d key=%u throttle=%u predicted=%d",
				player_idx, (unsigned)g_current_action_key,
				(unsigned)craft->throttle_speed,
				g_flight_sim_side_effects_suppressed);
			break;
		default:
			break;
		}
	} else {
		const int map_default_distance = 512;
		switch (g_current_action_key) {
		case FLIGHT_KEY_C:
			if (g_players[player_idx].map_camera_state > 1 &&
			    g_players[player_idx].current_target_object_idx !=
				    -1) {
				g_players[player_idx]
					.view_state.camera_world_x =
					g_object_table
						[(uint16_t)g_players[player_idx]
							 .current_target_object_idx]
							.world_x;
				g_players[player_idx]
					.view_state.camera_world_y =
					g_object_table
						[(uint16_t)g_players[player_idx]
							 .current_target_object_idx]
							.world_y;
				XVT_LOG_DEBUG(
					"flight.map_view_set slot=%d key=%u map=%d focus=%d aim=%d distance=%d x=%d y=%d z=%d predicted=%d",
					player_idx,
					(unsigned)g_current_action_key,
					(int)g_players[player_idx]
						.map_camera_state,
					(int)g_players[player_idx]
						.view_state
						.camera_focus_obj_idx,
					(int)g_players[player_idx]
						.view_state.aim_target_idx,
					g_players[player_idx]
						.view_state.camera_distance,
					g_players[player_idx]
						.view_state.camera_world_x,
					g_players[player_idx]
						.view_state.camera_world_y,
					g_players[player_idx]
						.view_state.camera_world_z,
					g_flight_sim_side_effects_suppressed);
			}
			break;
		case FLIGHT_KEY_Z:
			if (g_players[player_idx].map_camera_state > 1) {
				if (g_players[player_idx]
					    .current_target_object_idx != -1) {
					g_players[player_idx]
						.view_state.camera_world_x =
						g_object_table
							[(uint16_t)g_players[player_idx]
								 .current_target_object_idx]
								.world_x;
					g_players[player_idx]
						.view_state.camera_world_y =
						g_object_table
							[(uint16_t)g_players[player_idx]
								 .current_target_object_idx]
								.world_y;
					g_players[player_idx]
						.view_state.camera_world_z =
						g_object_table
							[(uint16_t)g_players[player_idx]
								 .current_target_object_idx]
								.world_z;
					g_players[player_idx]
						.view_state.camera_world_z +=
						16 *
						g_object_type_table
							[g_object_table
								 [(uint16_t)g_players
									  [player_idx]
										  .current_target_object_idx]
									 .object_type]
								.max_bounds_extent;
				}
			} else if (g_players[player_idx]
					   .view_state.camera_focus_obj_idx !=
				   UINT16_MAX) {
				g_players[player_idx]
					.view_state.camera_distance =
					g_object_type_table
						[g_object_table
							 [g_players[player_idx]
								  .view_state
								  .camera_focus_obj_idx]
								 .object_type]
							.max_bounds_extent +
					map_default_distance;
			} else if (g_players[player_idx]
					   .current_target_object_idx != -1) {
				g_players[player_idx]
					.view_state.camera_world_x =
					g_object_table
						[(uint16_t)g_players[player_idx]
							 .current_target_object_idx]
							.world_x;
				g_players[player_idx]
					.view_state.camera_world_y =
					g_object_table
						[(uint16_t)g_players[player_idx]
							 .current_target_object_idx]
							.world_y;
				g_players[player_idx]
					.view_state.camera_world_z =
					g_object_table
						[(uint16_t)g_players[player_idx]
							 .current_target_object_idx]
							.world_z;
				g_players[player_idx]
					.view_state.camera_distance =
					g_object_type_table
						[g_object_table
							 [(uint16_t)g_players
								  [player_idx]
									  .current_target_object_idx]
								 .object_type]
							.max_bounds_extent +
					map_default_distance;
				fview_build_camera_orient(
					0,
					g_players[player_idx]
						.view_state.view_pitch,
					g_players[player_idx]
						.view_state.view_yaw,
					0, 0, 0, NULL);
				g_players[player_idx]
					.view_state.camera_world_x -=
					math_mul_q15(g_players[player_idx]
							     .view_state
							     .camera_distance,
						     g_cam_mat_r2_x);
				g_players[player_idx]
					.view_state.camera_world_y -=
					math_mul_q15(g_players[player_idx]
							     .view_state
							     .camera_distance,
						     g_cam_mat_r2_y);
				g_players[player_idx]
					.view_state.camera_world_z -=
					math_mul_q15(g_players[player_idx]
							     .view_state
							     .camera_distance,
						     g_cam_mat_r2_z);
			}
			XVT_LOG_DEBUG(
				"flight.map_view_set slot=%d key=%u map=%d focus=%d aim=%d distance=%d x=%d y=%d z=%d predicted=%d",
				player_idx, (unsigned)g_current_action_key,
				(int)g_players[player_idx].map_camera_state,
				(int)g_players[player_idx]
					.view_state.camera_focus_obj_idx,
				(int)g_players[player_idx]
					.view_state.aim_target_idx,
				g_players[player_idx]
					.view_state.camera_distance,
				g_players[player_idx].view_state.camera_world_x,
				g_players[player_idx].view_state.camera_world_y,
				g_players[player_idx].view_state.camera_world_z,
				g_flight_sim_side_effects_suppressed);
			break;
		case FLIGHT_KEY_PAD_MINUS:
			g_players[player_idx].view_state.aim_target_idx =
				UINT16_MAX;
			XVT_LOG_DEBUG(
				"flight.map_view_set slot=%d key=%u map=%d focus=%d aim=%d distance=%d x=%d y=%d z=%d predicted=%d",
				player_idx, (unsigned)g_current_action_key,
				(int)g_players[player_idx].map_camera_state,
				(int)g_players[player_idx]
					.view_state.camera_focus_obj_idx,
				(int)g_players[player_idx]
					.view_state.aim_target_idx,
				g_players[player_idx]
					.view_state.camera_distance,
				g_players[player_idx].view_state.camera_world_x,
				g_players[player_idx].view_state.camera_world_y,
				g_players[player_idx].view_state.camera_world_z,
				g_flight_sim_side_effects_suppressed);
			break;
		case FLIGHT_KEY_PAD_PLUS:
			if (g_players[player_idx].current_target_object_idx !=
			    -1) {
				g_players[player_idx]
					.view_state.aim_target_idx =
					(uint16_t)g_players[player_idx]
						.current_target_object_idx;
				if (g_players[player_idx].map_camera_state >
				    1) {
					g_players[player_idx].map_camera_state =
						1;
					g_players[player_idx]
						.view_state.hud_aim_x = 0;
				}
				XVT_LOG_DEBUG(
					"flight.map_view_set slot=%d key=%u map=%d focus=%d aim=%d distance=%d x=%d y=%d z=%d predicted=%d",
					player_idx,
					(unsigned)g_current_action_key,
					(int)g_players[player_idx]
						.map_camera_state,
					(int)g_players[player_idx]
						.view_state
						.camera_focus_obj_idx,
					(int)g_players[player_idx]
						.view_state.aim_target_idx,
					g_players[player_idx]
						.view_state.camera_distance,
					g_players[player_idx]
						.view_state.camera_world_x,
					g_players[player_idx]
						.view_state.camera_world_y,
					g_players[player_idx]
						.view_state.camera_world_z,
					g_flight_sim_side_effects_suppressed);
			}
			break;
		default:
			break;
		}
	}

	const int map_overview_distance = 0x40000;
	unsigned int nearest_object_distance;
	switch (g_current_action_key) {
	case FLIGHT_KEY_TAB:
		if (g_players[player_idx].map_camera_state == 0 &&
		    (craft == NULL ||
		     (craft->working_subsystems &
		      CRAFT_SUBSYSTEM_FLAG_COMMUNICATIONS) == 0)) {
			g_msg_arg_table[0] = 101;
			g_msg_arg_table[1] = 87;
			msg_emit_in_flight_message(IFMSG_086_ARG_SYSTEM_IS_ARG,
						   player_idx);
		} else if (g_active_flight_player_count != 1 &&
			   g_players[player_idx].chat_recipient_mode ==
				   FLIGHT_CHAT_RECIPIENT_INACTIVE) {
			g_players[player_idx].chat_recipient_mode =
				FLIGHT_CHAT_RECIPIENT_TEAM;
			g_players[player_idx].msg_length = 0;
			g_players[player_idx].msg_text[0] = '_';
			g_players[player_idx].msg_text[1] = '\0';
			XVT_LOG_DEBUG("flight.chat_opened slot=%d predicted=%d",
				      player_idx,
				      g_flight_sim_side_effects_suppressed);
			msg_add_message_ptr(0, g_players[player_idx].msg_text);
			msg_emit_in_flight_message(IFMSG_375_TEAM_MESSAGE_ARG,
						   player_idx);
		}
		return;
	case FLIGHT_KEY_SPACE:
		switch (g_players[player_idx].pending_action_id) {
		case 0:
			if (g_players[player_idx].map_camera_state != 0) {
				if (g_players[player_idx].map_camera_state ==
				    1) {
					if (g_players[player_idx]
						    .view_state
						    .camera_focus_obj_idx !=
					    UINT16_MAX) {
						g_players[player_idx]
							.view_state
							.camera_distance = collide_roughdistance3d(
							g_players[player_idx]
									.view_state
									.camera_world_x -
								g_object_table
									[g_players[player_idx]
										 .view_state
										 .camera_focus_obj_idx]
										.world_x,
							g_players[player_idx]
									.view_state
									.camera_world_y -
								g_object_table
									[g_players[player_idx]
										 .view_state
										 .camera_focus_obj_idx]
										.world_y,
							g_players[player_idx]
									.view_state
									.camera_world_z -
								g_object_table
									[g_players[player_idx]
										 .view_state
										 .camera_focus_obj_idx]
										.world_z);
						g_players[player_idx]
							.view_state
							.camera_world_x =
							g_object_table
								[g_players[player_idx]
									 .view_state
									 .camera_focus_obj_idx]
									.world_x;
						g_players[player_idx]
							.view_state
							.camera_world_y =
							g_object_table
								[g_players[player_idx]
									 .view_state
									 .camera_focus_obj_idx]
									.world_y;
					} else if (g_players[player_idx]
							   .view_state
							   .aim_target_idx !=
						   UINT16_MAX) {
						g_players[player_idx]
							.view_state
							.camera_distance = collide_roughdistance3d(
							g_players[player_idx]
									.view_state
									.camera_world_x -
								g_object_table
									[g_players[player_idx]
										 .view_state
										 .aim_target_idx]
										.world_x,
							g_players[player_idx]
									.view_state
									.camera_world_y -
								g_object_table
									[g_players[player_idx]
										 .view_state
										 .aim_target_idx]
										.world_y,
							g_players[player_idx]
									.view_state
									.camera_world_z -
								g_object_table
									[g_players[player_idx]
										 .view_state
										 .aim_target_idx]
										.world_z);
						g_players[player_idx]
							.view_state
							.camera_world_x =
							g_object_table
								[g_players[player_idx]
									 .view_state
									 .aim_target_idx]
									.world_x;
						g_players[player_idx]
							.view_state
							.camera_world_y =
							g_object_table
								[g_players[player_idx]
									 .view_state
									 .aim_target_idx]
									.world_y;
						g_players[player_idx]
							.view_state
							.camera_focus_obj_idx =
							g_players[player_idx]
								.view_state
								.aim_target_idx;
					} else if (
						g_players[player_idx]
							.current_target_object_idx !=
						-1) {
						g_players[player_idx]
							.view_state
							.camera_distance = collide_roughdistance3d(
							g_players[player_idx]
									.view_state
									.camera_world_x -
								g_object_table
									[(uint16_t)g_players
										 [player_idx]
											 .current_target_object_idx]
										.world_x,
							g_players[player_idx]
									.view_state
									.camera_world_y -
								g_object_table
									[(uint16_t)g_players
										 [player_idx]
											 .current_target_object_idx]
										.world_y,
							g_players[player_idx]
									.view_state
									.camera_world_z -
								g_object_table
									[(uint16_t)g_players
										 [player_idx]
											 .current_target_object_idx]
										.world_z);
						g_players[player_idx]
							.view_state
							.camera_world_x =
							g_object_table
								[(uint16_t)g_players
									 [player_idx]
										 .current_target_object_idx]
									.world_x;
						g_players[player_idx]
							.view_state
							.camera_world_y =
							g_object_table
								[(uint16_t)g_players
									 [player_idx]
										 .current_target_object_idx]
									.world_y;
						g_players[player_idx]
							.view_state
							.camera_focus_obj_idx =
							(uint16_t)g_players[player_idx]
								.current_target_object_idx;
					} else {
						g_players[player_idx]
							.view_state
							.camera_distance =
							map_overview_distance;
						g_players[player_idx]
							.view_state
							.camera_world_z =
							map_overview_distance;
					}
					g_players[player_idx]
						.view_state.aim_target_idx =
						UINT16_MAX;
				} else if (g_players[player_idx]
						   .current_target_object_idx !=
					   -1) {
					g_players[player_idx]
						.view_state
						.camera_focus_obj_idx =
						(uint16_t)g_players[player_idx]
							.current_target_object_idx;
					g_players[player_idx]
						.view_state
						.camera_distance = collide_roughdistance3d(
						g_players[player_idx]
								.view_state
								.camera_world_x -
							g_object_table
								[(uint16_t)g_players
									 [player_idx]
										 .current_target_object_idx]
									.world_x,
						g_players[player_idx]
								.view_state
								.camera_world_y -
							g_object_table
								[(uint16_t)g_players
									 [player_idx]
										 .current_target_object_idx]
									.world_y,
						g_players[player_idx]
								.view_state
								.camera_world_z -
							g_object_table
								[(uint16_t)g_players
									 [player_idx]
										 .current_target_object_idx]
									.world_z);
				}
				g_players[player_idx].map_camera_state ^= 0x80;
				XVT_LOG_DEBUG(
					"flight.map_view_set slot=%d key=%u map=%d focus=%d aim=%d distance=%d x=%d y=%d z=%d predicted=%d",
					player_idx,
					(unsigned)g_current_action_key,
					(int)g_players[player_idx]
						.map_camera_state,
					(int)g_players[player_idx]
						.view_state
						.camera_focus_obj_idx,
					(int)g_players[player_idx]
						.view_state.aim_target_idx,
					g_players[player_idx]
						.view_state.camera_distance,
					g_players[player_idx]
						.view_state.camera_world_x,
					g_players[player_idx]
						.view_state.camera_world_y,
					g_players[player_idx]
						.view_state.camera_world_z,
					g_flight_sim_side_effects_suppressed);
			}
			break;
		case 1:
		case 5:
			if (g_players[player_idx].map_camera_state == 0) {
				if ((craft->working_subsystems &
				     CRAFT_SUBSYSTEM_FLAG_TARGETING_COMPUTER) !=
				    0) {
					g_players[player_idx]
						.current_target_object_idx =
						g_players[player_idx]
							.pending_action_param;
					if (g_players[player_idx]
						    .view_state
						    .target_camera_active !=
					    0) {
						g_players[player_idx]
							.view_state
							.camera_focus_obj_idx =
							(uint16_t)g_players[player_idx]
								.pending_action_param;
					}
					g_players[player_idx]
						.missile_lock_state = 0;
					craft->warhead_lock_ticks = 0;
					XVT_LOG_DEBUG(
						"flight.request_accepted slot=%d request=%d by=%d param=%d target=%d throttle=%d predicted=%d",
						player_idx,
						(int)g_players[player_idx]
							.pending_action_id,
						(int)g_players[player_idx]
							.pending_action_issuer_player_idx,
						(int)g_players[player_idx]
							.pending_action_param,
						(int)g_players[player_idx]
							.current_target_object_idx,
						craft != NULL
							? (int)craft
								  ->throttle_speed
							: -1,
						g_flight_sim_side_effects_suppressed);
					if (g_players[player_idx]
							    .current_target_object_idx ==
						    -1 ||
					    g_object_table[(uint16_t)g_players
								   [player_idx]
									   .current_target_object_idx]
							    .object_type == 0) {
						msg_emit_in_flight_message(
							IFMSG_265_OBJECT_DESTROYED,
							player_idx);
					} else {
						msg_emit_in_flight_message(
							IFMSG_264_OBJECT_TARGETED,
							player_idx);
					}
					if (g_players[player_idx]
						    .pending_action_issuer_player_idx ==
					    g_local_player) {
						if (g_players[player_idx]
							    .pending_action_id ==
						    1) {
							msg_radio_message(
								(uint16_t)g_players
									[player_idx]
										.object_index,
								(uint8_t *)g_object_table
									[g_players[player_idx]
										 .object_index]
										.mobj
										->p_craft,
								154, 4, 0);
						} else {
							msg_radio_message(
								(uint16_t)g_players
									[player_idx]
										.object_index,
								(uint8_t *)g_object_table
									[g_players[player_idx]
										 .object_index]
										.mobj
										->p_craft,
								150, 6, 0);
						}
					}
				} else {
					g_msg_arg_table[0] = 96;
					g_msg_arg_table[1] = 87;
					msg_emit_in_flight_message(
						IFMSG_086_ARG_SYSTEM_IS_ARG,
						player_idx);
				}
			}
			break;
		case 2:
			if (g_flight_sim_side_effects_suppressed == 0) {
				int16_t departure_object_index =
					g_players[player_idx]
						.pending_action_param;
				if (departure_object_index == -1) {
					if (g_players[player_idx]
						    .participation_state == 1) {
						int connected_count = 0;
						for (int16_t other_player_idx =
							     0;
						     other_player_idx < 8;
						     ++other_player_idx) {
							if (g_players[other_player_idx]
								    .participation_state ==
							    1) {
								++connected_count;
							}
						}
						if (connected_count > 1) {
							if (g_players[player_idx]
								    .object_index !=
							    -1) {
								fsfx_update_beam_system_loop(
									0,
									player_idx);
								fsfx_update_incoming_missile_warning(
									0);
							}
							player_unbind_from_current_craft(
								player_idx, 0,
								1);
						}
						player_end_flight_participation(
							player_idx);
						if (player_idx !=
						    g_local_player) {
							msg_add_message_ptr(
								0,
								net_session_get_player_name(
									player_idx));
							msg_emit_in_flight_message(
								IFMSG_380_ARG_HAS_QUIT_THE_MISSION,
								g_local_player);
						}
						if (g_mission_header.mission_type ==
							    MISSION_TYPE_MELEE &&
						    g_pilot_data.num_human_players_last_mission ==
							    1 &&
						    g_flight_mission_state
								    .runtime
								    .team_goal_status
									    [(uint16_t)g_players
										     [player_idx]
											     .team]
									    [0] !=
							    1) {
							g_players[player_idx]
								.mission_stats
								.mission_score -=
								2000;
							g_flight_mission_state
								.runtime
								.team_scores
									[TEAM_SCORE_MISSION]
									[(uint16_t)g_players
										 [player_idx]
											 .team] -=
								2000;
							XVT_LOG_DEBUG(
								"flight.quit_penalty slot=%d team=%d score=%d team_score=%d predicted=%d",
								player_idx,
								(int)g_players[player_idx]
									.team,
								g_players[player_idx]
									.mission_stats
									.mission_score,
								g_flight_mission_state
									.runtime
									.team_scores
										[TEAM_SCORE_MISSION]
										[(uint16_t)g_players
											 [player_idx]
												 .team],
								g_flight_sim_side_effects_suppressed);
						}
						XVT_LOG_INFO(
							"flight.player_quit slot=%d player=%u players=%d penalty=%d tick=%d",
							player_idx,
							(unsigned)g_players[player_idx]
								.network
								.direct_play_id,
							connected_count,
							g_mission_header.mission_type ==
										MISSION_TYPE_MELEE &&
									g_pilot_data.num_human_players_last_mission ==
										1 &&
									g_flight_mission_state
											.runtime
											.team_goal_status
												[(uint16_t)g_players
													 [player_idx]
														 .team]
												[0] !=
										1
								? 2000
								: 0,
							g_game_time);
						XVT_LOG_INFO(
							"battle.player_quit who=%d penalty=%d tick=%d",
							player_idx,
							g_mission_header.mission_type ==
										MISSION_TYPE_MELEE &&
									g_pilot_data.num_human_players_last_mission ==
										1 &&
									g_flight_mission_state
											.runtime
											.team_goal_status
												[(uint16_t)g_players
													 [player_idx]
														 .team]
												[0] !=
										1
								? 2000
								: 0,
							g_game_time);
					} else {
						if (player_idx ==
						    g_local_player) {
							g_flight_mission_state
								.mission_end_pending =
								1;
							XVT_LOG_INFO(
								"flight.mission_ending reason=\"local_left\" slot=%d tick=%d",
								player_idx,
								g_game_time);
						}
						g_players[player_idx]
							.participation_state =
							0;
						flight_update_active_player_count();
						if (net_session_get_host_dplay_id() !=
						    g_players[player_idx]
							    .network
							    .direct_play_id) {
							flight_net_mark_pilot_network_player_left(
								player_idx);
						}
						if (player_idx ==
							    g_local_player &&
						    net_session_is_local_host() !=
							    0) {
							flight_net_broadcast_host_session_abort();
						}
						XVT_LOG_INFO(
							"flight.player_departed slot=%d player=%u local=%d host=%d players=%d tick=%d",
							player_idx,
							(unsigned)g_players[player_idx]
								.network
								.direct_play_id,
							player_idx ==
								g_local_player,
							g_players[player_idx]
									.network
									.direct_play_id ==
								g_net_session
									.host_dplay_id,
							g_active_flight_player_count,
							g_game_time);
					}
				} else {
					uint16_t flight_group_idx =
						g_players[player_idx]
							.bound_flight_group_idx;
					uint16_t mothership = pai_find_mothership_object(
						g_mission_flight_groups[flight_group_idx]
							.fg
							.departure_mothership);
					mission_record_craft_outcome(
						(uint16_t)object_index,
						flight_group_idx,
						(uint16_t)(mothership == (uint16_t)departure_object_index
								   ? FLIGHT_GROUP_OUTCOME_ALTERNATE_MOTHERSHIP_DEPENDENT
								   : FLIGHT_GROUP_OUTCOME_PRIMARY_MOTHERSHIP_DEPENDENT));
					if (g_mission_header.mission_type !=
						    MISSION_TYPE_MELEE &&
					    g_flight_mission_state
							    .player_flight_group_wave_mode ==
						    1 &&
					    g_mission_flight_groups[flight_group_idx]
							    .fg
							    .number_of_waves !=
						    99) {
						g_players[player_idx]
							.mission_stats
							.mission_score +=
							40 *
							g_model_defs
								[get_model_index_from_type(
									 g_object_table[object_index]
										 .object_type)]
									.craft_point_value;
						XVT_LOG_DEBUG(
							"flight.dock_bonus slot=%d object=%d score=%d predicted=%d",
							player_idx,
							object_index,
							g_players[player_idx]
								.mission_stats
								.mission_score,
							g_flight_sim_side_effects_suppressed);
					}
					if (g_players[player_idx]
						    .object_index != -1) {
						fsfx_update_beam_system_loop(
							0, player_idx);
						fsfx_update_incoming_missile_warning(
							0);
					}
					g_object_table[object_index]
						.object_type = 0;
					player_save_craft_settings(player_idx);
					craft_free_linked_objects(craft);
					mission_process_flight_group_wave_completion(
						flight_group_idx);
					if (player_bind_to_available_craft(
						    player_idx, UINT32_MAX, 0,
						    0) != 0) {
						player_end_flight_participation(
							player_idx);
						player_emit_remote_player_departed_messages(
							player_idx);
					} else if (player_idx ==
						   g_local_player) {
						msg_emit_local_player_craft_message(
							IFMSG_292_PREVIOUS_CRAFT_ENTERED_HANGAR_NOW_PILOTING_ARG_ARG_ARG);
					}
					XVT_LOG_INFO(
						"flight.player_docked slot=%d object=%d fg=%d hangar=%d next=%d state=%d tick=%d",
						player_idx, object_index,
						(int)flight_group_idx,
						(int)departure_object_index,
						g_players[player_idx]
							.object_index,
						(int)g_players[player_idx]
							.participation_state,
						g_game_time);
				}
			}
			break;
		case 3:
			g_flight_mission_state.runtime
				.team_reinforcements_called
					[(uint16_t)g_players[player_idx].team] =
				1;
			g_flight_mission_state.runtime.team_scores
				[TEAM_SCORE_BONUS]
				[(uint16_t)g_players[player_idx].team] -= 5000;
			if (g_flight_sim_side_effects_suppressed == 0) {
				XVT_LOG_INFO(
					"flight.reinforcements_called slot=%d team=%d score=%d tick=%d",
					player_idx,
					(int)g_players[player_idx].team,
					g_flight_mission_state.runtime
						.team_scores
							[TEAM_SCORE_BONUS]
							[(uint16_t)g_players
								 [player_idx]
									 .team],
					g_game_time);
			}
			if (g_players[g_local_player].iff ==
			    g_players[player_idx].iff) {
				msg_emit_in_flight_message(
					IFMSG_231_REQUEST_FOR_REINFORCEMENTS_ACKNOWLEDGED,
					player_idx);
				fsfx_speak_tactical_officer_event(
					TACTICAL_VOICE_ORDER,
					TACTICAL_MSG_REINFORCEMENTS_ACKNOWLEDGED,
					-1, UINT16_MAX);
			}
			break;
		case 4:
			if (g_players[player_idx].map_camera_state == 0) {
				if ((craft->working_subsystems &
				     CRAFT_SUBSYSTEM_FLAG_TARGETING_COMPUTER) !=
				    0) {
					if (g_players[player_idx]
						    .current_target_object_idx ==
					    g_players[player_idx]
						    .pending_action_param) {
						int target_index = player_find_nearest_enemy_fighter(
							player_idx,
							(uint16_t)g_players[player_idx]
								.pending_action_param);
						if (target_index == -1) {
							g_players[player_idx]
								.current_target_object_idx =
								-1;
						} else {
							player_set_target(
								target_index,
								player_idx);
						}
						XVT_LOG_DEBUG(
							"flight.request_accepted slot=%d request=%d by=%d param=%d target=%d throttle=%d predicted=%d",
							player_idx,
							(int)g_players[player_idx]
								.pending_action_id,
							(int)g_players[player_idx]
								.pending_action_issuer_player_idx,
							(int)g_players[player_idx]
								.pending_action_param,
							(int)g_players[player_idx]
								.current_target_object_idx,
							craft != NULL
								? (int)craft
									  ->throttle_speed
								: -1,
							g_flight_sim_side_effects_suppressed);
						if (g_players[player_idx]
							    .pending_action_param !=
						    -1) {
							msg_emit_in_flight_message(
								g_object_table[(uint16_t)g_players
										       [player_idx]
											       .pending_action_param]
											.object_type !=
										0
									? IFMSG_266_OBJECT_IGNORED
									: IFMSG_265_OBJECT_DESTROYED,
								player_idx);
							if (g_players[player_idx]
								    .pending_action_issuer_player_idx ==
							    g_local_player) {
								msg_radio_message(
									(uint16_t)g_players
										[player_idx]
											.object_index,
									(uint8_t *)g_object_table
										[g_players[player_idx]
											 .object_index]
											.mobj
											->p_craft,
									0x9B, 5,
									0);
							}
						}
					}
				} else {
					g_msg_arg_table[0] = 96;
					g_msg_arg_table[1] = 87;
					msg_emit_in_flight_message(
						IFMSG_086_ARG_SYSTEM_IS_ARG,
						player_idx);
				}
			}
			break;
		case 6:
			if (g_players[player_idx].map_camera_state == 0) {
				if (g_players[player_idx]
					    .pending_action_issuer_player_idx ==
				    g_local_player) {
					msg_radio_message(
						(uint16_t)g_players[player_idx]
							.object_index,
						(uint8_t *)g_object_table
							[g_players[player_idx]
								 .object_index]
								.mobj->p_craft,
						0x95, 1, 0);
				}
				player_handle_hyperspace_command(craft,
								 player_idx);
				XVT_LOG_DEBUG(
					"flight.request_accepted slot=%d request=%d by=%d param=%d target=%d throttle=%d predicted=%d",
					player_idx,
					(int)g_players[player_idx]
						.pending_action_id,
					(int)g_players[player_idx]
						.pending_action_issuer_player_idx,
					(int)g_players[player_idx]
						.pending_action_param,
					(int)g_players[player_idx]
						.current_target_object_idx,
					craft != NULL
						? (int)craft->throttle_speed
						: -1,
					g_flight_sim_side_effects_suppressed);
			}
			break;
		case 7:
			if (g_players[player_idx].map_camera_state == 0) {
				craft->throttle_speed = 0;
				XVT_LOG_DEBUG(
					"flight.request_accepted slot=%d request=%d by=%d param=%d target=%d throttle=%d predicted=%d",
					player_idx,
					(int)g_players[player_idx]
						.pending_action_id,
					(int)g_players[player_idx]
						.pending_action_issuer_player_idx,
					(int)g_players[player_idx]
						.pending_action_param,
					(int)g_players[player_idx]
						.current_target_object_idx,
					craft != NULL
						? (int)craft->throttle_speed
						: -1,
					g_flight_sim_side_effects_suppressed);
				fsfx_play_sound(FLIGHT_SOUND_SMALL_CLICK, -1,
						player_idx);
				msg_emit_in_flight_message(
					IFMSG_267_WAITING_THROTTLE_SET_TO_NO_POWER,
					player_idx);
				if (g_players[player_idx]
					    .pending_action_issuer_player_idx ==
				    g_local_player) {
					msg_radio_message(
						(uint16_t)g_players[player_idx]
							.object_index,
						(uint8_t *)g_object_table
							[g_players[player_idx]
								 .object_index]
								.mobj->p_craft,
						0x98, 2, 0);
				}
			}
			break;
		case 8:
			if (g_players[player_idx].map_camera_state == 0) {
				craft->throttle_speed = UINT16_MAX;
				XVT_LOG_DEBUG(
					"flight.request_accepted slot=%d request=%d by=%d param=%d target=%d throttle=%d predicted=%d",
					player_idx,
					(int)g_players[player_idx]
						.pending_action_id,
					(int)g_players[player_idx]
						.pending_action_issuer_player_idx,
					(int)g_players[player_idx]
						.pending_action_param,
					(int)g_players[player_idx]
						.current_target_object_idx,
					craft != NULL
						? (int)craft->throttle_speed
						: -1,
					g_flight_sim_side_effects_suppressed);
				fsfx_play_sound(FLIGHT_SOUND_SMALL_CLICK, -1,
						player_idx);
				msg_emit_in_flight_message(
					IFMSG_268_RESUMING_THROTTLE_SET_TO_FULL_POWER,
					player_idx);
				if (g_players[player_idx]
					    .pending_action_issuer_player_idx ==
				    g_local_player) {
					msg_radio_message(
						(uint16_t)g_players[player_idx]
							.object_index,
						(uint8_t *)g_object_table
							[g_players[player_idx]
								 .object_index]
								.mobj->p_craft,
						0x99, 3, 0);
				}
			}
			break;
		case 9:
			if (g_players[player_idx].map_camera_state == 0) {
				int target_index =
					player_find_nearest_enemy_fighter(
						player_idx,
						(uint16_t)g_players[player_idx]
							.pending_action_param);
				if (target_index == -1) {
					g_players[player_idx]
						.current_target_object_idx = -1;
				} else {
					player_set_target(target_index,
							  player_idx);
				}
				XVT_LOG_DEBUG(
					"flight.request_accepted slot=%d request=%d by=%d param=%d target=%d throttle=%d predicted=%d",
					player_idx,
					(int)g_players[player_idx]
						.pending_action_id,
					(int)g_players[player_idx]
						.pending_action_issuer_player_idx,
					(int)g_players[player_idx]
						.pending_action_param,
					(int)g_players[player_idx]
						.current_target_object_idx,
					craft != NULL
						? (int)craft->throttle_speed
						: -1,
					g_flight_sim_side_effects_suppressed);
				msg_emit_in_flight_message(
					IFMSG_264_OBJECT_TARGETED, player_idx);
				if (g_players[player_idx]
					    .pending_action_issuer_player_idx ==
				    g_local_player) {
					msg_radio_message(
						(uint16_t)g_players[player_idx]
							.object_index,
						(uint8_t *)g_object_table
							[g_players[player_idx]
								 .object_index]
								.mobj->p_craft,
						0x97, 7, 0);
				}
			}
			break;
		default:
			break;
		}
		g_players[player_idx].pending_action_id = 0;
		return;

	case FLIGHT_KEY_STAR:
	case FLIGHT_KEY_PAD_STAR:
		if (g_flight_sim_side_effects_suppressed == 0 &&
		    g_replay_view_mode == 0) {
			if (g_players[player_idx].map_camera_state != 0) {
				g_players[player_idx]
					.view_state.camera_focus_obj_idx =
					UINT16_MAX;
				XVT_LOG_DEBUG(
					"flight.map_view_set slot=%d key=%u map=%d focus=%d aim=%d distance=%d x=%d y=%d z=%d predicted=%d",
					player_idx,
					(unsigned)g_current_action_key,
					(int)g_players[player_idx]
						.map_camera_state,
					(int)g_players[player_idx]
						.view_state
						.camera_focus_obj_idx,
					(int)g_players[player_idx]
						.view_state.aim_target_idx,
					g_players[player_idx]
						.view_state.camera_distance,
					g_players[player_idx]
						.view_state.camera_world_x,
					g_players[player_idx]
						.view_state.camera_world_y,
					g_players[player_idx]
						.view_state.camera_world_z,
					g_flight_sim_side_effects_suppressed);
			} else if (g_players[player_idx]
					   .view_state.external_camera_active !=
				   0) {
				g_players[player_idx]
					.view_state.player_input_blocked =
					g_players[player_idx]
						.view_state
						.player_input_blocked == 0;
				XVT_LOG_DEBUG(
					"flight.view_set slot=%d key=%u hud=%d focus=%d external=%d camera=%d blocked=%d aim_x=%d aim_y=%d predicted=%d",
					player_idx,
					(unsigned)g_current_action_key,
					(int)g_players[player_idx]
						.view_state.hud_state_live,
					(int)g_players[player_idx]
						.view_state
						.camera_focus_obj_idx,
					(int)g_players[player_idx]
						.view_state
						.external_camera_active,
					(int)g_players[player_idx]
						.view_state
						.target_camera_active,
					(int)g_players[player_idx]
						.view_state
						.player_input_blocked,
					(int)g_players[player_idx]
						.view_state.hud_aim_x,
					(int)g_players[player_idx]
						.view_state.hud_aim_y,
					g_flight_sim_side_effects_suppressed);
				fsfx_play_sound(FLIGHT_SOUND_CONFIRM_BEEP, -1,
						player_idx);
			} else {
				fsfx_play_sound(FLIGHT_SOUND_SMALL_CLICK, -1,
						player_idx);
			}
		}
		return;
	case FLIGHT_KEY_COMMA: {
		int target_index =
			g_players[player_idx].current_target_object_idx;
		if (target_index != -1 &&
		    target_index < g_active_region_craft_object_slot_end) {
			struct craft_data *target_craft =
				g_object_table[(uint16_t)target_index]
					.mobj->p_craft;
			uint16_t mesh_count =
				g_object_table[(uint16_t)target_index]
							.object_type >= 73
					? model_mesh_get_object_type_mesh_count(
						  g_object_table
							  [(uint16_t)
								   target_index]
								  .object_type)
					: g_object_type_mesh_cache
						  [g_object_table
							   [(uint16_t)
								    target_index]
								   .object_type]
							  .mesh_count;
			for (int16_t attempt = 0; attempt < mesh_count;
			     ++attempt) {
				++g_players[player_idx]
					  .selected_target_component;
				if ((uint16_t)g_players[player_idx]
					    .selected_target_component >=
				    mesh_count) {
					g_players[player_idx]
						.selected_target_component = 0;
				}
				if (target_craft->component_state
						    [(uint16_t)g_players[player_idx]
							     .selected_target_component] ==
					    0 &&
				    craft_is_selectable_damage_component_mesh(
					    g_object_table
						    [(uint16_t)g_players[player_idx]
							     .current_target_object_idx]
							    .object_type,
					    (uint16_t)g_players[player_idx]
						    .selected_target_component) &&
				    target_craft->component_hp
						    [(uint16_t)g_players[player_idx]
							     .selected_target_component] !=
					    0) {
					break;
				}
			}
			XVT_LOG_DEBUG(
				"flight.component_selected slot=%d target=%d component=%d predicted=%d",
				player_idx, target_index,
				(int)g_players[player_idx]
					.selected_target_component,
				g_flight_sim_side_effects_suppressed);
			fsfx_play_sound(FLIGHT_SOUND_CONFIRM_BEEP, -1,
					player_idx);
		}
		return;
	}
	case FLIGHT_KEY_LESS_THAN: {
		int target_index =
			g_players[player_idx].current_target_object_idx;
		if (target_index != -1 &&
		    target_index < g_active_region_craft_object_slot_end) {
			struct craft_data *target_craft =
				g_object_table[(uint16_t)target_index]
					.mobj->p_craft;
			uint16_t mesh_count =
				g_object_table[(uint16_t)target_index]
							.object_type >= 73
					? model_mesh_get_object_type_mesh_count(
						  g_object_table
							  [(uint16_t)
								   target_index]
								  .object_type)
					: g_object_type_mesh_cache
						  [g_object_table
							   [(uint16_t)
								    target_index]
								   .object_type]
							  .mesh_count;
			for (int16_t attempt = 0; attempt < mesh_count;
			     ++attempt) {
				--g_players[player_idx]
					  .selected_target_component;
				if ((uint16_t)g_players[player_idx]
					    .selected_target_component ==
				    UINT16_MAX) {
					g_players[player_idx]
						.selected_target_component =
						(int16_t)(mesh_count - 1);
				}
				if (target_craft->component_state
						    [(uint16_t)g_players[player_idx]
							     .selected_target_component] ==
					    0 &&
				    craft_is_selectable_damage_component_mesh(
					    g_object_table
						    [(uint16_t)g_players[player_idx]
							     .current_target_object_idx]
							    .object_type,
					    (uint16_t)g_players[player_idx]
						    .selected_target_component) &&
				    target_craft->component_hp
						    [(uint16_t)g_players[player_idx]
							     .selected_target_component] !=
					    0) {
					break;
				}
			}
			XVT_LOG_DEBUG(
				"flight.component_selected slot=%d target=%d component=%d predicted=%d",
				player_idx, target_index,
				(int)g_players[player_idx]
					.selected_target_component,
				g_flight_sim_side_effects_suppressed);
			fsfx_play_sound(FLIGHT_SOUND_CONFIRM_BEEP, -1,
					player_idx);
		}
		return;
	}
	case FLIGHT_KEY_SLASH:
	case FLIGHT_KEY_PAD_SLASH:
		if (g_flight_sim_side_effects_suppressed == 0) {
			struct player_view_state *view =
				&g_players[player_idx].view_state;
			if (g_players[player_idx].map_camera_state != 0) {
				int target_index =
					g_players[player_idx]
						.current_target_object_idx;
				if (target_index != -1) {
					view->camera_focus_obj_idx =
						(uint16_t)target_index;
					view->camera_distance =
						4 *
						g_object_type_table
							[g_object_table[target_index]
								 .object_type]
								.max_bounds_extent;
					if (g_players[player_idx]
						    .map_camera_state > 1) {
						g_players[player_idx]
							.map_camera_state = 1;
						view->hud_aim_x = 0;
					}
					XVT_LOG_DEBUG(
						"flight.map_view_set slot=%d key=%u map=%d focus=%d aim=%d distance=%d x=%d y=%d z=%d predicted=%d",
						player_idx,
						(unsigned)g_current_action_key,
						(int)g_players[player_idx]
							.map_camera_state,
						(int)g_players[player_idx]
							.view_state
							.camera_focus_obj_idx,
						(int)g_players[player_idx]
							.view_state
							.aim_target_idx,
						g_players[player_idx]
							.view_state
							.camera_distance,
						g_players[player_idx]
							.view_state
							.camera_world_x,
						g_players[player_idx]
							.view_state
							.camera_world_y,
						g_players[player_idx]
							.view_state
							.camera_world_z,
						g_flight_sim_side_effects_suppressed);
				}
			} else if (view->target_camera_active == 0) {
				view->external_camera_active =
					view->external_camera_active == 0;
				if (view->external_camera_active != 0 &&
				    view->camera_focus_obj_idx ==
					    g_players[player_idx]
						    .object_index) {
					view->saved_hud_state_byte =
						view->hud_state_live;
					view->saved_hud_aim_x = view->hud_aim_x;
					view->saved_hud_aim_y = view->hud_aim_y;
				}
				player_update_hud_view_for_camera_focus(
					player_idx);
				XVT_LOG_DEBUG(
					"flight.view_set slot=%d key=%u hud=%d focus=%d external=%d camera=%d blocked=%d aim_x=%d aim_y=%d predicted=%d",
					player_idx,
					(unsigned)g_current_action_key,
					(int)g_players[player_idx]
						.view_state.hud_state_live,
					(int)g_players[player_idx]
						.view_state
						.camera_focus_obj_idx,
					(int)g_players[player_idx]
						.view_state
						.external_camera_active,
					(int)g_players[player_idx]
						.view_state
						.target_camera_active,
					(int)g_players[player_idx]
						.view_state
						.player_input_blocked,
					(int)g_players[player_idx]
						.view_state.hud_aim_x,
					(int)g_players[player_idx]
						.view_state.hud_aim_y,
					g_flight_sim_side_effects_suppressed);
			}
		}
		return;
	case FLIGHT_KEY_1:
	case FLIGHT_KEY_2:
	case FLIGHT_KEY_3:
	case FLIGHT_KEY_4: {
		int taunt_index = g_current_action_key - FLIGHT_KEY_1;
		msg_add_message_ptr(0, net_session_get_player_name(player_idx));
		msg_add_message_ptr(
			1, g_player_taunt_text[player_idx][taunt_index]);
		for (int16_t dst_player_idx = 0; dst_player_idx < 8;
		     ++dst_player_idx) {
			if (g_players[dst_player_idx].participation_state !=
			    0) {
				g_msg_sender_iff = 3;
				msg_emit_in_flight_message(
					IFMSG_374_FROM_ARG_ARG, dst_player_idx);
			}
		}
		XVT_LOG_DEBUG("flight.taunt_sent slot=%d taunt=%d predicted=%d",
			      player_idx, taunt_index,
			      g_flight_sim_side_effects_suppressed);
		msg_emit_in_flight_message(IFMSG_378_MESSAGE_SENT, player_idx);
		return;
	}
	case FLIGHT_KEY_SHIFT_A: {
		int target_index =
			g_players[player_idx].current_target_object_idx;
		if (g_players[player_idx].map_camera_state == 0 &&
		    (craft == NULL ||
		     (craft->working_subsystems &
		      CRAFT_SUBSYSTEM_FLAG_COMMUNICATIONS) == 0)) {
			g_msg_arg_table[0] = 101;
			g_msg_arg_table[1] = 87;
			msg_emit_in_flight_message(IFMSG_086_ARG_SYSTEM_IS_ARG,
						   player_idx);
			return;
		}
		if (target_index == -1) {
			return;
		}
		if (craft != NULL &&
		    craft->player_command_avoid_target_obj_idx ==
			    target_index) {
			craft->player_command_avoid_target_obj_idx = UINT16_MAX;
		}
		XVT_LOG_DEBUG(
			"flight.order_given slot=%d command=\"attack\" target=%d predicted=%d",
			player_idx, target_index,
			g_flight_sim_side_effects_suppressed);
		player_issue_ai_wingman_target_order((uint16_t)target_index,
						     0x9A, 4, player_idx);
		for (int16_t other_player_idx = 0; other_player_idx < 8;
		     ++other_player_idx) {
			if (other_player_idx == player_idx ||
			    g_players[other_player_idx].participation_state !=
				    1 ||
			    g_players[other_player_idx].team !=
				    g_players[player_idx].team ||
			    g_players[other_player_idx].object_index == -1 ||
			    g_players[other_player_idx].object_index ==
				    (uint16_t)target_index ||
			    g_players[other_player_idx].pending_action_id !=
				    0) {
				continue;
			}
			struct craft_data *other_craft =
				g_object_table[g_players[other_player_idx]
						       .object_index]
					.mobj->p_craft;
			if (other_craft == NULL ||
			    (other_craft->working_subsystems &
			     CRAFT_SUBSYSTEM_FLAG_COMMUNICATIONS) == 0) {
				continue;
			}
			g_players[other_player_idx].pending_action_id = 1;
			g_players[other_player_idx].pending_action_param =
				(int16_t)target_index;
			g_players[other_player_idx]
				.pending_action_issuer_player_idx =
				(uint16_t)player_idx;
			g_players[other_player_idx].pending_action_timer = 1416;
			XVT_LOG_DEBUG(
				"flight.request_posted slot=%d request=%d by=%d param=%d timer=%d predicted=%d",
				(int)other_player_idx,
				(int)g_players[other_player_idx]
					.pending_action_id,
				(int)g_players[other_player_idx]
					.pending_action_issuer_player_idx,
				(int)g_players[other_player_idx]
					.pending_action_param,
				g_players[other_player_idx]
					.pending_action_timer,
				g_flight_sim_side_effects_suppressed);
			if (other_player_idx == g_local_player) {
				fsfx_play_sound(FLIGHT_SOUND_INCOMING_ORDER, -1,
						g_local_player);
				msg_add_message_ptr(0,
						    net_session_get_player_name(
							    player_idx));
				msg_emit_in_flight_message(
					IFMSG_270_FROM_ARG_ATTACK_MY_TARGET_HIT_SPACE_TO_TARGET,
					g_local_player);
			}
		}
		return;
	}
	case FLIGHT_KEY_SHIFT_E: {
		if (g_players[player_idx].map_camera_state == 0 &&
		    (craft == NULL ||
		     (craft->working_subsystems &
		      CRAFT_SUBSYSTEM_FLAG_COMMUNICATIONS) == 0)) {
			g_msg_arg_table[0] = 101;
			g_msg_arg_table[1] = 87;
			msg_emit_in_flight_message(IFMSG_086_ARG_SYSTEM_IS_ARG,
						   player_idx);
			return;
		}
		if (player_can_radio_command_craft(player_idx) != 0) {
			int target_index = (uint16_t)g_players[player_idx]
						   .current_target_object_idx;
			g_cur_craft =
				g_object_table[target_index].mobj->p_craft;
			struct ai_controller *controller =
				&g_cur_craft->ai_controller;
			if (strcmp(g_plan_table[controller->running_plan_id]
					   .name,
				   "craftwaitforgopln") == 0) {
				controller->running_plan_id =
					controller->saved_plan_id;
				pai_setupcraftcontext((uint16_t)target_index);
				pai_apply_running_plan_target_and_maneuver(
					(uint16_t)target_index);
			}
			controller->candidate_target_idx = AI_TARGET_ABORT;
			XVT_LOG_DEBUG(
				"flight.craft_ordered slot=%d command=\"evade\" target=%d plan=%d saved=%d predicted=%d",
				player_idx, target_index,
				(int)controller->running_plan_id,
				(int)controller->saved_plan_id,
				g_flight_sim_side_effects_suppressed);
			msg_radio_message((uint16_t)target_index,
					  (uint8_t *)g_cur_craft, 0x97, 7, 0);
			return;
		}
		for (int16_t other_player_idx = 0; other_player_idx < 8;
		     ++other_player_idx) {
			if (g_players[other_player_idx].object_index !=
			    (uint16_t)g_players[player_idx]
				    .current_target_object_idx) {
				continue;
			}
			if (g_players[other_player_idx].team ==
			    g_players[player_idx].team) {
				g_players[other_player_idx].pending_action_id =
					9;
				g_players[other_player_idx]
					.pending_action_issuer_player_idx =
					(uint16_t)player_idx;
				g_players[other_player_idx]
					.pending_action_timer = 1416;
				XVT_LOG_DEBUG(
					"flight.request_posted slot=%d request=%d by=%d param=%d timer=%d predicted=%d",
					(int)other_player_idx,
					(int)g_players[other_player_idx]
						.pending_action_id,
					(int)g_players[other_player_idx]
						.pending_action_issuer_player_idx,
					(int)g_players[other_player_idx]
						.pending_action_param,
					g_players[other_player_idx]
						.pending_action_timer,
					g_flight_sim_side_effects_suppressed);
				if (other_player_idx == g_local_player) {
					fsfx_play_sound(
						FLIGHT_SOUND_INCOMING_ORDER, -1,
						g_local_player);
					msg_add_message_ptr(
						0, net_session_get_player_name(
							   player_idx));
					msg_emit_in_flight_message(
						IFMSG_275_FROM_ARG_EVADE_HIT_SPACE_TO_TARGET_ATTACKER,
						g_local_player);
				}
			} else {
				g_msg_sender_iff = g_players[player_idx].iff;
				msg_emit_in_flight_message(
					IFMSG_146_CRAFT_NOT_RESPONDING_TO_ORDER,
					player_idx);
			}
		}
		return;
	}
	case FLIGHT_KEY_SHIFT_G: {
		if (g_players[player_idx].map_camera_state == 0 &&
		    (craft == NULL ||
		     (craft->working_subsystems &
		      CRAFT_SUBSYSTEM_FLAG_COMMUNICATIONS) == 0)) {
			g_msg_arg_table[0] = 101;
			g_msg_arg_table[1] = 87;
			msg_emit_in_flight_message(IFMSG_086_ARG_SYSTEM_IS_ARG,
						   player_idx);
			return;
		}
		if (player_can_radio_command_craft(player_idx) != 0) {
			int target_index = (uint16_t)g_players[player_idx]
						   .current_target_object_idx;
			g_cur_craft =
				g_object_table[target_index].mobj->p_craft;
			if (strcmp(g_plan_table[g_cur_craft->ai_controller
							.running_plan_id]
					   .name,
				   "craftwaitforgopln") == 0) {
				g_cur_craft->ai_controller.running_plan_id =
					g_cur_craft->ai_controller
						.saved_plan_id;
				pai_setupcraftcontext((uint16_t)target_index);
				pai_apply_running_plan_target_and_maneuver(
					(uint16_t)target_index);
				XVT_LOG_DEBUG(
					"flight.craft_ordered slot=%d command=\"go\" target=%d plan=%d saved=%d predicted=%d",
					player_idx, target_index,
					(int)g_object_table[target_index]
						.mobj->p_craft->ai_controller
						.running_plan_id,
					(int)g_object_table[target_index]
						.mobj->p_craft->ai_controller
						.saved_plan_id,
					g_flight_sim_side_effects_suppressed);
				msg_radio_message((uint16_t)target_index,
						  (uint8_t *)g_cur_craft, 0x99,
						  3, 0);
			}
			return;
		}
		for (int16_t other_player_idx = 0; other_player_idx < 8;
		     ++other_player_idx) {
			if (g_players[other_player_idx].object_index !=
			    (uint16_t)g_players[player_idx]
				    .current_target_object_idx) {
				continue;
			}
			if (g_players[other_player_idx].team ==
			    g_players[player_idx].team) {
				g_players[other_player_idx].pending_action_id =
					8;
				g_players[other_player_idx]
					.pending_action_issuer_player_idx =
					(uint16_t)player_idx;
				g_players[other_player_idx]
					.pending_action_timer = 1416;
				XVT_LOG_DEBUG(
					"flight.request_posted slot=%d request=%d by=%d param=%d timer=%d predicted=%d",
					(int)other_player_idx,
					(int)g_players[other_player_idx]
						.pending_action_id,
					(int)g_players[other_player_idx]
						.pending_action_issuer_player_idx,
					(int)g_players[other_player_idx]
						.pending_action_param,
					g_players[other_player_idx]
						.pending_action_timer,
					g_flight_sim_side_effects_suppressed);
				if (other_player_idx == g_local_player) {
					fsfx_play_sound(
						FLIGHT_SOUND_INCOMING_ORDER, -1,
						g_local_player);
					msg_add_message_ptr(
						0, net_session_get_player_name(
							   player_idx));
					msg_emit_in_flight_message(
						IFMSG_274_FROM_ARG_GO_AHEAD_HIT_SPACE_TO_PROCEED_WITH_MISSION,
						g_local_player);
				}
			} else {
				g_msg_sender_iff = g_players[player_idx].iff;
				msg_emit_in_flight_message(
					IFMSG_146_CRAFT_NOT_RESPONDING_TO_ORDER,
					player_idx);
			}
		}
		return;
	}
	case FLIGHT_KEY_SHIFT_H: {
		if (g_players[player_idx].map_camera_state == 0 &&
		    (craft == NULL ||
		     (craft->working_subsystems &
		      CRAFT_SUBSYSTEM_FLAG_COMMUNICATIONS) == 0)) {
			g_msg_arg_table[0] = 101;
			g_msg_arg_table[1] = 87;
			msg_emit_in_flight_message(IFMSG_086_ARG_SYSTEM_IS_ARG,
						   player_idx);
			return;
		}
		if (player_can_radio_command_craft(player_idx) != 0) {
			int target_index = (uint16_t)g_players[player_idx]
						   .current_target_object_idx;
			struct object_record *target =
				&g_object_table[target_index];
			g_cur_craft = target->mobj->p_craft;
			struct ai_controller *controller =
				&g_cur_craft->ai_controller;
			if (strcmp(g_plan_table[controller->running_plan_id]
					   .name,
				   "flyhomeevadepln") != 0 &&
			    strcmp(g_plan_table[controller->running_plan_id]
					   .name,
				   "starshipintohyperpln") != 0) {
				if (g_cur_craft->ai_flight
					    .mission_aborted_flag == 0) {
					++g_mission_fg_stats[target->flight_group_idx]
						  .outcome_count
							  [FLIGHT_GROUP_OUTCOME_ABORTED];
					if (g_mission_flight_groups
						    [target->flight_group_idx]
							    .fg
							    .special_cargo_craft ==
					    g_cur_craft->craft_ordinal) {
						g_mission_fg_stats[target->flight_group_idx]
							.special_cargo_outcome
								[FLIGHT_GROUP_OUTCOME_ABORTED] =
							1;
					}
					XVT_LOG_DEBUG(
						"flight.craft_aborted slot=%d target=%d fg=%d aborted=%u special=%d predicted=%d",
						player_idx, target_index,
						(int)target->flight_group_idx,
						(unsigned)g_mission_fg_stats
							[target->flight_group_idx]
								.outcome_count
									[FLIGHT_GROUP_OUTCOME_ABORTED],
						(int)g_mission_fg_stats[target->flight_group_idx]
							.special_cargo_outcome
								[FLIGHT_GROUP_OUTCOME_ABORTED],
						g_flight_sim_side_effects_suppressed);
				}
				g_cur_craft->ai_flight.mission_aborted_flag = 1;
				controller->running_plan_id =
					pai_find_plan_id_by_name_or_zero(
						target->genus_id ==
								CRAFT_GENUS_STARSHIP
							? "starshipintohyperpln"
							: "flyhomeevadepln");
				pai_setupcraftcontext((uint16_t)target_index);
				pai_apply_running_plan_target_and_maneuver(
					(uint16_t)target_index);
				XVT_LOG_DEBUG(
					"flight.craft_ordered slot=%d command=\"home\" target=%d plan=%d saved=%d predicted=%d",
					player_idx, target_index,
					(int)controller->running_plan_id,
					(int)controller->saved_plan_id,
					g_flight_sim_side_effects_suppressed);
			}
			msg_radio_message((uint16_t)target_index,
					  (uint8_t *)g_cur_craft, 0x95, 1, 0);
			return;
		}
		for (int16_t other_player_idx = 0; other_player_idx < 8;
		     ++other_player_idx) {
			if (g_players[other_player_idx].object_index !=
			    (uint16_t)g_players[player_idx]
				    .current_target_object_idx) {
				continue;
			}
			if (g_players[other_player_idx].team ==
			    g_players[player_idx].team) {
				g_players[other_player_idx].pending_action_id =
					6;
				g_players[other_player_idx]
					.pending_action_issuer_player_idx =
					(uint16_t)player_idx;
				g_players[other_player_idx]
					.pending_action_timer = 1416;
				XVT_LOG_DEBUG(
					"flight.request_posted slot=%d request=%d by=%d param=%d timer=%d predicted=%d",
					(int)other_player_idx,
					(int)g_players[other_player_idx]
						.pending_action_id,
					(int)g_players[other_player_idx]
						.pending_action_issuer_player_idx,
					(int)g_players[other_player_idx]
						.pending_action_param,
					g_players[other_player_idx]
						.pending_action_timer,
					g_flight_sim_side_effects_suppressed);
				if (other_player_idx == g_local_player) {
					fsfx_play_sound(
						FLIGHT_SOUND_INCOMING_ORDER, -1,
						g_local_player);
					msg_add_message_ptr(
						0, net_session_get_player_name(
							   player_idx));
					msg_emit_in_flight_message(
						IFMSG_272_FROM_ARG_HEAD_HOME_HIT_SPACE_TO_COMPLY,
						g_local_player);
				}
			} else {
				g_msg_sender_iff = g_players[player_idx].iff;
				msg_emit_in_flight_message(
					IFMSG_146_CRAFT_NOT_RESPONDING_TO_ORDER,
					player_idx);
			}
		}
		return;
	}
	case FLIGHT_KEY_SHIFT_I: {
		int target_index =
			g_players[player_idx].current_target_object_idx;
		if (g_players[player_idx].map_camera_state == 0 &&
		    (craft == NULL ||
		     (craft->working_subsystems &
		      CRAFT_SUBSYSTEM_FLAG_COMMUNICATIONS) == 0)) {
			g_msg_arg_table[0] = 101;
			g_msg_arg_table[1] = 87;
			msg_emit_in_flight_message(IFMSG_086_ARG_SYSTEM_IS_ARG,
						   player_idx);
			return;
		}
		if (target_index == -1) {
			return;
		}
		if (craft != NULL) {
			craft->player_command_avoid_target_obj_idx =
				(uint16_t)target_index;
		}
		XVT_LOG_DEBUG(
			"flight.order_given slot=%d command=\"ignore\" target=%d predicted=%d",
			player_idx, target_index,
			g_flight_sim_side_effects_suppressed);
		player_issue_ai_wingman_target_order((uint16_t)target_index,
						     0x9B, 5, player_idx);
		for (int16_t other_player_idx = 0; other_player_idx < 8;
		     ++other_player_idx) {
			if (other_player_idx == player_idx ||
			    g_players[other_player_idx].participation_state !=
				    1 ||
			    g_players[other_player_idx].team !=
				    g_players[player_idx].team ||
			    g_players[other_player_idx].object_index == -1 ||
			    g_players[other_player_idx]
					    .current_target_object_idx !=
				    target_index ||
			    g_players[other_player_idx]
					    .current_target_object_idx ==
				    g_players[other_player_idx].object_index ||
			    g_players[other_player_idx].pending_action_id !=
				    0) {
				continue;
			}
			struct craft_data *other_craft =
				g_object_table[g_players[other_player_idx]
						       .object_index]
					.mobj->p_craft;
			if (other_craft == NULL ||
			    (other_craft->working_subsystems &
			     CRAFT_SUBSYSTEM_FLAG_COMMUNICATIONS) == 0) {
				continue;
			}
			g_players[other_player_idx].pending_action_id = 4;
			g_players[other_player_idx].pending_action_param =
				(int16_t)target_index;
			g_players[other_player_idx]
				.pending_action_issuer_player_idx =
				(uint16_t)player_idx;
			g_players[other_player_idx].pending_action_timer = 1416;
			XVT_LOG_DEBUG(
				"flight.request_posted slot=%d request=%d by=%d param=%d timer=%d predicted=%d",
				(int)other_player_idx,
				(int)g_players[other_player_idx]
					.pending_action_id,
				(int)g_players[other_player_idx]
					.pending_action_issuer_player_idx,
				(int)g_players[other_player_idx]
					.pending_action_param,
				g_players[other_player_idx]
					.pending_action_timer,
				g_flight_sim_side_effects_suppressed);
			if (other_player_idx == g_local_player) {
				fsfx_play_sound(FLIGHT_SOUND_INCOMING_ORDER, -1,
						g_local_player);
				msg_add_message_ptr(0,
						    net_session_get_player_name(
							    player_idx));
				msg_emit_in_flight_message(
					IFMSG_271_FROM_ARG_IGNORE_MY_TARGET_HIT_SPACE_TO_IGNORE,
					g_local_player);
			}
		}
		return;
	}
	case FLIGHT_KEY_SHIFT_P: {
		uint16_t candidate =
			g_players[player_idx].current_target_object_idx;
		int16_t remaining = g_active_region_craft_object_slot_end -
				    g_active_region_object_slot_start;
		int16_t new_target = -1;
		while (remaining-- != 0) {
			if (++candidate >=
			    g_active_region_craft_object_slot_end) {
				candidate = g_active_region_object_slot_start;
			}
			struct object_record *object =
				&g_object_table[candidate];
			if (object->object_type == 0 ||
			    object->genus_id == CRAFT_GENUS_EXPLOSION ||
			    object_has_active_decoy_beam((uint16_t)candidate) !=
				    0) {
				continue;
			}
			int owner = object->player_owner_idx;
			if (owner == -1 || owner == player_idx) {
				continue;
			}
			int team = g_mission_flight_groups
					   [object->flight_group_idx]
						   .fg.team;
			int hostile =
				team != (uint16_t)g_players[player_idx].team &&
				g_mission_teams[(uint16_t)g_players[player_idx]
							.team]
						.allies[team] == 0;
			if (g_flight_mission_state.locate_players_enabled ==
				    0 &&
			    hostile) {
				continue;
			}
			if (object->mobj->family != 0 ||
			    (object->mobj->p_craft->object_kind !=
				     CRAFT_OBJECT_KIND_BREAKING_UP &&
			     object->mobj->p_craft->object_kind !=
				     CRAFT_OBJECT_KIND_EXPLODING)) {
				new_target = candidate;
				break;
			}
		}
		if (new_target != -1) {
			player_set_target(new_target, player_idx);
		} else if (g_flight_mission_state.locate_players_enabled == 0) {
			msg_emit_in_flight_message(
				IFMSG_225_AUTO_LOCATING_OF_PLAYERS_NOT_ENABLED_FOR_THIS_MISSION,
				player_idx);
		}
		return;
	}
	case FLIGHT_KEY_SHIFT_R: {
		if (g_players[player_idx].map_camera_state == 0 &&
		    (craft == NULL ||
		     (craft->working_subsystems &
		      CRAFT_SUBSYSTEM_FLAG_COMMUNICATIONS) == 0)) {
			g_msg_arg_table[0] = 101;
			g_msg_arg_table[1] = 87;
			msg_emit_in_flight_message(IFMSG_086_ARG_SYSTEM_IS_ARG,
						   player_idx);
			return;
		}
		int target_index =
			g_players[player_idx].current_target_object_idx;
		if (target_index != -1 &&
		    target_index < g_active_region_craft_object_slot_end) {
			struct object_record *target =
				&g_object_table[(uint16_t)target_index];
			int player_team = (uint16_t)g_players[player_idx].team;
			int team = g_mission_flight_groups
					   [target->flight_group_idx]
						   .fg.team;
			if (player_team == team ||
			    g_mission_teams[player_team].allies[team] != 0) {
				if (target->player_owner_idx == -1) {
					g_cur_craft = target->mobj->p_craft;
					msg_reportmessage(
						(uint16_t)target_index,
						g_cur_craft,
						g_plan_report_message_id_by_plan_id
							[g_cur_craft
								 ->ai_controller
								 .running_plan_id]);
				} else {
					for (int16_t other_player_idx = 0;
					     other_player_idx < 8;
					     ++other_player_idx) {
						if (other_player_idx !=
							    player_idx &&
						    g_players[other_player_idx]
								    .participation_state ==
							    1 &&
						    g_players[other_player_idx]
								    .team ==
							    g_players[player_idx]
								    .team &&
						    g_players[other_player_idx]
								    .object_index ==
							    (uint16_t)
								    target_index &&
						    other_player_idx ==
							    g_local_player) {
							fsfx_play_sound(
								FLIGHT_SOUND_INCOMING_ORDER,
								-1,
								g_local_player);
							msg_add_message_ptr(
								0,
								net_session_get_player_name(
									player_idx));
							msg_emit_in_flight_message(
								IFMSG_276_FROM_ARG_REPORT_IN,
								g_local_player);
						}
					}
				}
			}
		}
		return;
	}
	case FLIGHT_KEY_SHIFT_S: {
		if (g_players[player_idx].map_camera_state == 0 &&
		    (craft == NULL ||
		     (craft->working_subsystems &
		      CRAFT_SUBSYSTEM_FLAG_COMMUNICATIONS) == 0)) {
			g_msg_arg_table[0] = 101;
			g_msg_arg_table[1] = 87;
			msg_emit_in_flight_message(IFMSG_086_ARG_SYSTEM_IS_ARG,
						   player_idx);
			return;
		}
		if (g_players[player_idx].pending_action_id != 0) {
			return;
		}
		uint8_t reinforcement_available = 0;
		for (uint16_t flight_group_idx = 0;
		     flight_group_idx < g_mission_header.num_flight_groups;
		     ++flight_group_idx) {
			if ((g_mission_flight_groups[flight_group_idx]
					     .fg.arrival_triggers[0]
					     .triggers[0]
					     .condition == 20 &&
			     g_mission_flight_groups[flight_group_idx]
					     .fg.arrival_triggers[0]
					     .triggers[0]
					     .variable ==
				     (uint16_t)g_players[player_idx].team) ||
			    (g_mission_flight_groups[flight_group_idx]
					     .fg.arrival_triggers[0]
					     .triggers[1]
					     .condition == 20 &&
			     g_mission_flight_groups[flight_group_idx]
					     .fg.arrival_triggers[0]
					     .triggers[1]
					     .variable ==
				     (uint16_t)g_players[player_idx].team)) {
				reinforcement_available = 1;
			}
			if ((g_mission_flight_groups[flight_group_idx]
					     .fg.arrival_triggers[1]
					     .triggers[0]
					     .condition == 20 &&
			     g_mission_flight_groups[flight_group_idx]
					     .fg.arrival_triggers[1]
					     .triggers[0]
					     .variable ==
				     (uint16_t)g_players[player_idx].team) ||
			    (g_mission_flight_groups[flight_group_idx]
					     .fg.arrival_triggers[1]
					     .triggers[1]
					     .condition == 20 &&
			     g_mission_flight_groups[flight_group_idx]
					     .fg.arrival_triggers[1]
					     .triggers[1]
					     .variable ==
				     (uint16_t)g_players[player_idx].team)) {
				reinforcement_available = 1;
			}
		}
		g_msg_sender_iff = g_players[player_idx].iff;
		if (reinforcement_available == 0) {
			if (g_players[g_local_player].iff ==
			    g_players[player_idx].iff) {
				msg_emit_in_flight_message(
					IFMSG_230_NO_REINFORCEMENTS_AVAILABLE,
					player_idx);
				fsfx_speak_tactical_officer_event(
					TACTICAL_VOICE_ORDER,
					TACTICAL_MSG_NO_REINFORCEMENTS_AVAILABLE,
					-1, UINT16_MAX);
			}
		} else if (g_flight_mission_state.runtime
				   .team_reinforcements_called
					   [(uint16_t)g_players[player_idx]
						    .team] == 0) {
			if (g_players[g_local_player].iff ==
			    g_players[player_idx].iff) {
				msg_emit_in_flight_message(
					IFMSG_233_HIT_SPACE_TO_CONFIRM_REINFORCEMENT_REQUEST,
					player_idx);
			}
			g_players[player_idx].pending_action_id = 3;
			g_players[player_idx].pending_action_timer = 1888;
			XVT_LOG_DEBUG(
				"flight.confirm_asked slot=%d request=%d param=%d timer=%d predicted=%d",
				player_idx,
				(int)g_players[player_idx].pending_action_id,
				(int)g_players[player_idx].pending_action_param,
				g_players[player_idx].pending_action_timer,
				g_flight_sim_side_effects_suppressed);
		} else {
			if (g_players[g_local_player].iff ==
			    g_players[player_idx].iff) {
				msg_emit_in_flight_message(
					IFMSG_232_REINFORCEMENTS_ALREADY_SENT_NO_MORE_AVAILABLE,
					player_idx);
				fsfx_speak_tactical_officer_event(
					TACTICAL_VOICE_ORDER,
					TACTICAL_MSG_REINFORCEMENTS_ALREADY_SENT,
					-1, UINT16_MAX);
			}
		}
		return;
	}
	case FLIGHT_KEY_SHIFT_W: {
		if (g_players[player_idx].map_camera_state == 0 &&
		    (craft == NULL ||
		     (craft->working_subsystems &
		      CRAFT_SUBSYSTEM_FLAG_COMMUNICATIONS) == 0)) {
			g_msg_arg_table[0] = 101;
			g_msg_arg_table[1] = 87;
			msg_emit_in_flight_message(IFMSG_086_ARG_SYSTEM_IS_ARG,
						   player_idx);
			return;
		}
		if (player_can_radio_command_craft(player_idx) != 0) {
			int target_index = (uint16_t)g_players[player_idx]
						   .current_target_object_idx;
			g_cur_craft =
				g_object_table[target_index].mobj->p_craft;
			struct ai_controller *controller =
				&g_cur_craft->ai_controller;
			if (strcmp(g_plan_table[controller->running_plan_id]
					   .name,
				   "craftwaitforgopln") != 0 &&
			    strcmp(g_plan_table[controller->running_plan_id]
					   .name,
				   "intohyperspacepln") != 0 &&
			    strcmp(g_plan_table[controller->running_plan_id]
					   .name,
				   "outofhyperspacepln") != 0) {
				controller->saved_plan_id =
					controller->running_plan_id;
				controller->running_plan_id =
					pai_find_plan_id_by_name_or_zero(
						g_object_table[target_index]
									.genus_id ==
								CRAFT_GENUS_STARSHIP
							? "starshipwaitforgopln"
							: "craftwaitforgopln");
				pai_setupcraftcontext((uint16_t)target_index);
				pai_apply_running_plan_target_and_maneuver(
					(uint16_t)target_index);
				XVT_LOG_DEBUG(
					"flight.craft_ordered slot=%d command=\"wait\" target=%d plan=%d saved=%d predicted=%d",
					player_idx, target_index,
					(int)controller->running_plan_id,
					(int)controller->saved_plan_id,
					g_flight_sim_side_effects_suppressed);
				msg_radio_message((uint16_t)target_index,
						  (uint8_t *)g_cur_craft, 0x98,
						  2, 0);
			}
			return;
		}
		for (int16_t other_player_idx = 0; other_player_idx < 8;
		     ++other_player_idx) {
			if (g_players[other_player_idx].object_index !=
			    (uint16_t)g_players[player_idx]
				    .current_target_object_idx) {
				continue;
			}
			if (g_players[other_player_idx].team ==
			    g_players[player_idx].team) {
				g_players[other_player_idx].pending_action_id =
					7;
				g_players[other_player_idx]
					.pending_action_issuer_player_idx =
					(uint16_t)player_idx;
				g_players[other_player_idx]
					.pending_action_timer = 1416;
				XVT_LOG_DEBUG(
					"flight.request_posted slot=%d request=%d by=%d param=%d timer=%d predicted=%d",
					(int)other_player_idx,
					(int)g_players[other_player_idx]
						.pending_action_id,
					(int)g_players[other_player_idx]
						.pending_action_issuer_player_idx,
					(int)g_players[other_player_idx]
						.pending_action_param,
					g_players[other_player_idx]
						.pending_action_timer,
					g_flight_sim_side_effects_suppressed);
				if (other_player_idx == g_local_player) {
					fsfx_play_sound(
						FLIGHT_SOUND_INCOMING_ORDER, -1,
						g_local_player);
					msg_add_message_ptr(
						0, net_session_get_player_name(
							   player_idx));
					msg_emit_in_flight_message(
						IFMSG_273_FROM_ARG_WAIT_FOR_ORDERS_HIT_SPACE_TO_WAIT_FOR_ORDERS,
						g_local_player);
				}
			} else {
				g_msg_sender_iff = g_players[player_idx].iff;
				msg_emit_in_flight_message(
					IFMSG_146_CRAFT_NOT_RESPONDING_TO_ORDER,
					player_idx);
			}
		}
		return;
	}
	case FLIGHT_KEY_A:
		player_set_target(
			player_find_attacker_of_target(
				g_players[player_idx].current_target_object_idx,
				g_players[player_idx].object_index),
			player_idx);
		return;
	case FLIGHT_KEY_E: {
		uint16_t candidate =
			g_players[player_idx].current_target_object_idx;
		int16_t remaining = g_active_region_craft_object_slot_end -
				    g_active_region_object_slot_start;
		if (g_players[player_idx].object_index == -1) {
			return;
		}
		int16_t attacker = -1;
		while (remaining-- != 0) {
			if (++candidate >=
			    g_active_region_craft_object_slot_end) {
				candidate = g_active_region_object_slot_start;
			}
			struct object_record *object =
				&g_object_table[candidate];
			if (object->object_type == 0 ||
			    candidate == g_players[player_idx].object_index ||
			    object->genus_id == CRAFT_GENUS_EXPLOSION) {
				continue;
			}
			struct craft_data *candidate_craft =
				object->mobj->p_craft;
			if (candidate_craft->working_subsystems == 0 ||
			    candidate_craft->object_kind !=
				    CRAFT_OBJECT_KIND_ACTIVE ||
			    object_has_active_decoy_beam((uint16_t)candidate) !=
				    0) {
				continue;
			}
			if (object->player_owner_idx == -1) {
				int maneuver = candidate_craft->ai_controller
						       .maneuver_mode;
				if (candidate_craft->ai_controller
						    .target_obj_idx ==
					    object_index &&
				    (maneuver == 12 || maneuver == 23)) {
					attacker = candidate;
					break;
				}
			} else {
				int recent_attacker =
					(uint16_t)craft->last_attacker_obj_idx ==
						candidate &&
					(uint16_t)mission_clock_to_seconds(
						g_mission_elapsed_clock.hours,
						g_mission_elapsed_clock.minutes,
						g_mission_elapsed_clock
							.seconds) -
							craft->last_hit_mission_second <
						5;
				int owner = object->player_owner_idx;
				int team = g_mission_flight_groups
						   [object->flight_group_idx]
							   .fg.team;
				int hostile =
					team != (uint16_t)g_players[player_idx]
							.team &&
					g_mission_teams[(uint16_t)g_players
								[player_idx]
									.team]
							.allies[team] == 0;
				if (recent_attacker ||
				    ((uint16_t)g_players[owner]
						     .current_target_object_idx ==
					     object_index &&
				     hostile)) {
					attacker = candidate;
					break;
				}
			}
		}
		player_set_target(attacker, player_idx);
		return;
	}
	case FLIGHT_KEY_D:
		if (player_idx == g_local_player &&
		    g_flight_sim_side_effects_suppressed == 0 &&
		    g_players[player_idx].map_camera_state == 0) {
			if (flight_player_has_disabled_subsystem() != 0) {
				mfd_toggle_page(MFD_PAGE_DAMAGE);
			} else {
				msg_emit_in_flight_message(
					IFMSG_397_ALL_SYSTEMS_OPERATIONAL,
					g_local_player);
			}
		}
		return;
	case FLIGHT_KEY_SHIFT_F:
		if (player_idx == g_local_player &&
		    g_flight_sim_side_effects_suppressed == 0) {
			int player_object_index =
				g_players[player_idx].object_index;
			if (player_object_index != -1 &&
			    (g_object_table[player_object_index]
				     .mobj->p_craft->working_subsystems &
			     CRAFT_SUBSYSTEM_FLAG_TARGETING_COMPUTER) == 0) {
				g_msg_arg_table[0] = 96;
				g_msg_arg_table[1] = 87;
				msg_emit_in_flight_message(
					IFMSG_086_ARG_SYSTEM_IS_ARG,
					player_idx);
			} else {
				mfd_toggle_page(MFD_PAGE_HOSTILE_CRAFT);
			}
		}
		return;
	case FLIGHT_KEY_F:
		if (player_idx == g_local_player &&
		    g_flight_sim_side_effects_suppressed == 0) {
			if (g_players[player_idx].object_index == -1 ||
			    (g_object_table[g_players[player_idx].object_index]
				     .mobj->p_craft->working_subsystems &
			     CRAFT_SUBSYSTEM_FLAG_TARGETING_COMPUTER) != 0) {
				if (g_players[player_idx].map_camera_state ==
				    0) {
					mfd_toggle_page(
						MFD_PAGE_FRIENDLY_CRAFT);
				}
			} else {
				g_msg_arg_table[0] = 96;
				g_msg_arg_table[1] = 87;
				msg_emit_in_flight_message(
					IFMSG_086_ARG_SYSTEM_IS_ARG,
					player_idx);
			}
		}
		return;
	case FLIGHT_KEY_G:
		if (player_idx == g_local_player &&
		    g_flight_sim_side_effects_suppressed == 0) {
			mfd_toggle_page(MFD_PAGE_GOALS);
		}
		return;
	case FLIGHT_KEY_H:
		if (g_players[player_idx].map_camera_state != 0 &&
		    g_flight_sim_side_effects_suppressed == 0 &&
		    player_idx == g_local_player) {
			mfd_toggle_page(MFD_PAGE_MAP_HELP);
		}
		return;
	case FLIGHT_KEY_I: {
		nearest_object_distance = UINT32_MAX;
		int projectile_index;
		int nearest_projectile = -1;
		for (projectile_index = g_projectile_object_slot_start;
		     projectile_index < g_projectile_object_slot_end;
		     ++projectile_index) {
			struct object_record *projectile =
				&g_object_table[projectile_index];
			if (projectile->object_type == 0 ||
			    (projectile->genus_id !=
				     CRAFT_GENUS_PLAYER_PROJECTILE &&
			     projectile->genus_id !=
				     CRAFT_GENUS_OTHER_PROJECTILE) ||
			    g_projectile_type_data.warhead_class
					    [projectile->object_type -
					     PROJECTILE_OBJECT_TYPE_FIRST] ==
				    0) {
				continue;
			}
			struct warhead_guidance_state *guidance =
				projectile->mobj->p_warhead_guidance;
			if (guidance == NULL ||
			    guidance->target_obj_idx !=
				    g_players[player_idx].object_index) {
				continue;
			}
			pai_object_ref_direction_to_object_ref(
				g_players[player_idx].object_index,
				projectile_index);
			if ((unsigned int)trig2_polardistance <
			    nearest_object_distance) {
				nearest_projectile = projectile_index;
				nearest_object_distance = trig2_polardistance;
			}
		}
		if (nearest_projectile == -1) {
			for (projectile_index = g_projectile_object_slot_start;
			     projectile_index < g_projectile_object_slot_end;
			     ++projectile_index) {
				struct object_record *projectile =
					&g_object_table[projectile_index];
				if (projectile->object_type == 0 ||
				    (projectile->genus_id !=
					     CRAFT_GENUS_PLAYER_PROJECTILE &&
				     projectile->genus_id !=
					     CRAFT_GENUS_OTHER_PROJECTILE) ||
				    g_projectile_type_data.warhead_class
						    [projectile->object_type -
						     PROJECTILE_OBJECT_TYPE_FIRST] ==
					    0) {
					continue;
				}
				struct warhead_guidance_state *guidance =
					projectile->mobj->p_warhead_guidance;
				if (guidance == NULL ||
				    guidance->target_obj_idx == UINT16_MAX) {
					continue;
				}
				struct mobile_object *target_mobile =
					g_object_table[guidance->target_obj_idx]
						.mobj;
				if (target_mobile == NULL ||
				    target_mobile->family != 0) {
					continue;
				}
				int target_team = target_mobile->team;
				int player_team =
					(uint16_t)g_players[player_idx].team;
				if (target_team != player_team &&
				    g_mission_teams[target_team]
						    .allies[player_team] == 0) {
					continue;
				}
				pai_object_ref_direction_to_object_ref(
					g_players[player_idx].object_index,
					projectile_index);
				if ((unsigned int)trig2_polardistance <
				    nearest_object_distance) {
					nearest_projectile = projectile_index;
					nearest_object_distance =
						trig2_polardistance;
				}
			}
		}
		player_set_target(nearest_projectile, player_idx);
		return;
	}
	case FLIGHT_KEY_K:
		if (player_idx == g_local_player &&
		    g_flight_sim_side_effects_suppressed == 0) {
			mfd_toggle_page(MFD_PAGE_SCOREBOARD);
		}
		return;
	case FLIGHT_KEY_L:
		if (player_idx == g_local_player &&
		    g_flight_sim_side_effects_suppressed == 0) {
			mfd_toggle_page(MFD_PAGE_MESSAGE_LOG);
		}
		return;
	case FLIGHT_KEY_SHIFT_M:
	case FLIGHT_KEY_M:
		if (g_flight_sim_side_effects_suppressed == 0 &&
		    g_players[player_idx].hyperspace_phase == 0) {
			if (g_players[player_idx].map_camera_state != 0) {
				if (g_players[player_idx].participation_state !=
				    2) {
					mission_process_flight_group_wave_completion(
						g_players[player_idx]
							.bound_flight_group_idx);
					int16_t saved_map_camera_state =
						g_players[player_idx]
							.map_camera_state;
					g_players[player_idx].map_camera_state =
						0;
					if (player_bind_to_available_craft(
						    player_idx, UINT32_MAX,
						    g_players[player_idx]
							    .bound_object_signature,
						    0) == 0) {
						g_players[player_idx]
							.map_camera_state = 0;
					} else {
						g_players[player_idx]
							.map_camera_state = (uint8_t)
							saved_map_camera_state;
					}
					XVT_LOG_DEBUG(
						"flight.map_left slot=%d object=%d map=%d predicted=%d",
						player_idx,
						g_players[player_idx]
							.object_index,
						(int)g_players[player_idx]
							.map_camera_state,
						g_flight_sim_side_effects_suppressed);
				}
			} else {
				int16_t target_index =
					g_players[player_idx]
						.current_target_object_idx;
				fsfx_update_beam_system_loop(0, player_idx);
				fsfx_update_incoming_missile_warning(0);
				if (target_index == -1) {
					target_index =
						(int16_t)g_players[player_idx]
							.object_index;
				}
				player_unbind_from_current_craft(
					player_idx, 0,
					g_current_action_key != FLIGHT_KEY_M &&
						g_mission_header.mission_type !=
							MISSION_TYPE_MELEE);
				g_players[player_idx].map_camera_state =
					UINT8_MAX;
				hud_set_hud_view_state(HUD_VIEW_CRAFT_LIST,
						       player_idx);
				g_players[player_idx]
					.view_state.player_input_blocked = 1;
				g_players[player_idx]
					.view_state.external_camera_active = 1;
				g_players[player_idx]
					.view_state.camera_distance =
					map_overview_distance;
				if (target_index != -1) {
					player_set_target(target_index,
							  player_idx);
				}
				g_players[player_idx]
					.view_state.camera_focus_obj_idx =
					UINT16_MAX;
				g_players[player_idx]
					.view_state.aim_target_idx = UINT16_MAX;
				g_players[player_idx]
					.view_state.camera_world_z =
					map_overview_distance;
				if (g_players[player_idx]
					    .current_target_object_idx != -1) {
					g_players[player_idx]
						.view_state.camera_world_x =
						g_object_table
							[(uint16_t)g_players[player_idx]
								 .current_target_object_idx]
								.world_x;
					g_players[player_idx]
						.view_state.camera_world_y =
						g_object_table
							[(uint16_t)g_players[player_idx]
								 .current_target_object_idx]
								.world_y;
					g_players[player_idx]
						.view_state.camera_distance =
						16 *
						g_object_type_table
							[g_object_table
								 [(uint16_t)g_players
									  [player_idx]
										  .current_target_object_idx]
									 .object_type]
								.max_bounds_extent;
				}
				g_players[player_idx].pending_action_timer = 0;
				g_players[player_idx].pending_action_id = 0;
				XVT_LOG_DEBUG(
					"flight.map_entered slot=%d target=%d ai=%d predicted=%d",
					player_idx,
					(int)g_players[player_idx]
						.current_target_object_idx,
					g_current_action_key != FLIGHT_KEY_M &&
						g_mission_header.mission_type !=
							MISSION_TYPE_MELEE,
					g_flight_sim_side_effects_suppressed);
				fsfx_update_player_engine_loop();
				fsfx_update_chaff_loop();
				fsfx_update_beam_effect_loops();
			}
		}
		return;
	case FLIGHT_KEY_O: {
		int16_t target_index =
			player_find_nearest_objective(0, player_idx);
		if (target_index != -1) {
			player_set_target(target_index, player_idx);
		} else {
			target_index =
				player_find_nearest_objective(2, player_idx);
			if (target_index != -1) {
				player_set_target(target_index, player_idx);
			}
		}
		return;
	}
	case FLIGHT_KEY_P: {
		nearest_object_distance = UINT32_MAX;
		if (g_flight_mission_state.locate_players_enabled == 0) {
			return;
		}
		int candidate = g_players[player_idx].current_target_object_idx;
		int remaining = g_active_region_craft_object_slot_end -
				g_active_region_object_slot_start - 1;
		int nearest_target = -1;
		while (remaining-- >= 0) {
			if (++candidate >=
			    g_active_region_craft_object_slot_end) {
				candidate = g_active_region_object_slot_start;
			}
			struct object_record *object =
				&g_object_table[candidate];
			if (object->object_type == 0 ||
			    object->player_owner_idx == -1 ||
			    object->player_owner_idx == player_idx ||
			    object->genus_id == CRAFT_GENUS_EXPLOSION) {
				continue;
			}
			int team = g_mission_flight_groups
					   [object->flight_group_idx]
						   .fg.team;
			int hostile =
				team != (uint16_t)g_players[player_idx].team &&
				g_mission_teams[(uint16_t)g_players[player_idx]
							.team]
						.allies[team] == 0;
			if (!hostile || object_has_active_decoy_beam(
						(uint16_t)candidate) != 0) {
				continue;
			}
			if (object->mobj->family == 0 &&
			    (object->mobj->p_craft->object_kind ==
				     CRAFT_OBJECT_KIND_BREAKING_UP ||
			     object->mobj->p_craft->object_kind ==
				     CRAFT_OBJECT_KIND_EXPLODING)) {
				continue;
			}
			player_compute_polar_to_object_ref(player_idx,
							   candidate);
			if ((unsigned int)trig2_polardistance <
			    nearest_object_distance) {
				nearest_target = candidate;
				nearest_object_distance = trig2_polardistance;
			}
		}
		player_set_target(nearest_target, player_idx);
		return;
	}
	case FLIGHT_KEY_Q:
		if (g_players[player_idx].participation_state == 1) {
			fsfx_play_sound(FLIGHT_SOUND_CONFIRM_BEEP, -1,
					player_idx);
			fsfx_play_sound(FLIGHT_SOUND_CONFIRM_BEEP, -1,
					player_idx);
			if (g_mission_header.mission_type ==
				    MISSION_TYPE_MELEE &&
			    g_pilot_data.num_human_players_last_mission == 1 &&
			    g_flight_mission_state.runtime.team_goal_status
					    [(uint16_t)g_players[player_idx]
						     .team][0] != 1) {
				fsfx_play_sound(FLIGHT_SOUND_DANGER_WARNING, -1,
						player_idx);
				msg_emit_in_flight_message(
					IFMSG_216_LEAVING_NOW_IS_2000_POINT_PENALTY_PRESS_SPACE_TO_QUIT_ANYWAY,
					player_idx);
			} else {
				msg_emit_in_flight_message(
					IFMSG_215_PRESS_SPACE_TO_END_MISSION,
					player_idx);
			}
			g_players[player_idx].pending_action_id = 2;
			g_players[player_idx].pending_action_param = -1;
			g_players[player_idx].pending_action_timer = 1888;
			XVT_LOG_DEBUG(
				"flight.confirm_asked slot=%d request=%d param=%d timer=%d predicted=%d",
				player_idx,
				(int)g_players[player_idx].pending_action_id,
				(int)g_players[player_idx].pending_action_param,
				g_players[player_idx].pending_action_timer,
				g_flight_sim_side_effects_suppressed);
		} else {
			msg_emit_in_flight_message(
				IFMSG_384_YOU_MUST_WAIT_UNTIL_THE_OTHER_PLAYERS_ARE_FINISHED,
				player_idx);
		}
		return;
	case FLIGHT_KEY_R:
		player_set_target(player_find_nearest_enemy_fighter(player_idx,
								    UINT16_MAX),
				  player_idx);
		return;
	case FLIGHT_KEY_T:
		if (g_players[player_idx].current_target_object_idx != -1) {
			player_set_target(
				player_cycle_target_any_iff(
					g_players[player_idx]
						.current_target_object_idx,
					1, player_idx),
				player_idx);
		} else {
			player_set_target(player_cycle_target_any_iff(
						  g_players[player_idx]
							  .target_cycle_start,
						  1, player_idx),
					  player_idx);
		}
		return;
	case FLIGHT_KEY_Y:
		if (g_players[player_idx].current_target_object_idx != -1) {
			player_set_target(
				player_cycle_target_any_iff(
					g_players[player_idx]
						.current_target_object_idx,
					-1, player_idx),
				player_idx);
		} else {
			player_set_target(player_cycle_target_any_iff(
						  g_players[player_idx]
							  .target_cycle_start,
						  -1, player_idx),
					  player_idx);
		}
		return;
	case FLIGHT_KEY_U: {
		int newest_target = -1;
		uint16_t newest_age = UINT16_MAX;
		for (int candidate = g_active_region_object_slot_start;
		     candidate < g_active_region_craft_object_slot_end;
		     ++candidate) {
			struct object_record *object =
				&g_object_table[candidate];
			if (object->object_type == 0 ||
			    candidate == object_index ||
			    object->genus_id == CRAFT_GENUS_EXPLOSION) {
				continue;
			}
			struct craft_data *candidate_craft =
				object->mobj->p_craft;
			if (candidate_craft->leader_obj_idx != UINT8_MAX ||
			    (candidate_craft->object_kind !=
				     CRAFT_OBJECT_KIND_ACTIVE &&
			     candidate_craft->object_kind !=
				     CRAFT_OBJECT_KIND_DISABLED &&
			     candidate_craft->object_kind !=
				     CRAFT_OBJECT_KIND_ARRIVING_FROM_HYPERSPACE) ||
			    object_has_active_decoy_beam((uint16_t)candidate) !=
				    0) {
				continue;
			}
			if (newest_age > object->mobj->seconds_alive) {
				newest_target = candidate;
				newest_age = object->mobj->seconds_alive;
			}
		}
		player_set_target(newest_target, player_idx);
		return;
	}
	case FLIGHT_KEY_ALT_C:
		g_players[player_idx].current_target_object_idx = -1;
		XVT_LOG_DEBUG("flight.target_cleared slot=%d predicted=%d",
			      player_idx, g_flight_sim_side_effects_suppressed);
		if (g_players[player_idx].view_state.target_camera_active !=
		    0) {
			struct player_view_state *view =
				&g_players[player_idx].view_state;
			view->target_camera_active = 0;
			view->external_camera_active = 0;
			view->player_input_blocked = 0;
			view->camera_focus_obj_idx =
				(uint16_t)g_players[player_idx].object_index;
			if (player_idx == g_local_player) {
				g_hud_cached_target_object_idx = -2;
				g_render_object_ref_flags = 0;
			}
			hud_set_hud_view_state(HUD_VIEW_FORWARD, player_idx);
			view->hud_aim_x = 0;
			view->hud_aim_y = 0;
			XVT_LOG_DEBUG(
				"flight.view_set slot=%d key=%u hud=%d focus=%d external=%d camera=%d blocked=%d aim_x=%d aim_y=%d predicted=%d",
				player_idx, (unsigned)g_current_action_key,
				(int)g_players[player_idx]
					.view_state.hud_state_live,
				(int)g_players[player_idx]
					.view_state.camera_focus_obj_idx,
				(int)g_players[player_idx]
					.view_state.external_camera_active,
				(int)g_players[player_idx]
					.view_state.target_camera_active,
				(int)g_players[player_idx]
					.view_state.player_input_blocked,
				(int)g_players[player_idx].view_state.hud_aim_x,
				(int)g_players[player_idx].view_state.hud_aim_y,
				g_flight_sim_side_effects_suppressed);
		}
		return;
	case FLIGHT_KEY_ALT_Q:
		if (g_players[player_idx].participation_state != 1) {
			fsfx_play_sound(FLIGHT_SOUND_CONFIRM_BEEP, -1,
					player_idx);
			fsfx_play_sound(FLIGHT_SOUND_CONFIRM_BEEP, -1,
					player_idx);
			fsfx_play_sound(FLIGHT_SOUND_DANGER_WARNING, -1,
					player_idx);
			if (net_session_get_host_dplay_id() ==
			    g_players[player_idx].network.direct_play_id) {
				msg_emit_in_flight_message(
					IFMSG_219_WARNING_YOU_ARE_THE_HOST_PRESSING_SPACE_WILL_ABORT_THIS_GAME,
					player_idx);
			} else {
				msg_emit_in_flight_message(
					IFMSG_218_WARNING_PRESSING_SPACE_WILL_DISCONNECT_FROM_THE_HOST,
					player_idx);
			}
			g_players[player_idx].pending_action_id = 2;
			g_players[player_idx].pending_action_param = -1;
			g_players[player_idx].pending_action_timer = 1888;
			XVT_LOG_DEBUG(
				"flight.confirm_asked slot=%d request=%d param=%d timer=%d predicted=%d",
				player_idx,
				(int)g_players[player_idx].pending_action_id,
				(int)g_players[player_idx].pending_action_param,
				g_players[player_idx].pending_action_timer,
				g_flight_sim_side_effects_suppressed);
		}
		return;
	case FLIGHT_KEY_F1:
		if (g_players[player_idx].current_target_object_idx != -1) {
			player_set_target(
				player_cycle_target(
					g_players[player_idx]
						.current_target_object_idx,
					1, player_idx, 2, 5),
				player_idx);
		} else {
			player_set_target(
				player_cycle_target(g_players[player_idx]
							    .target_cycle_start,
						    1, player_idx, 2, 5),
				player_idx);
		}
		return;
	case FLIGHT_KEY_F2:
		if (g_players[player_idx].current_target_object_idx != -1) {
			player_set_target(
				player_cycle_target(
					g_players[player_idx]
						.current_target_object_idx,
					-1, player_idx, 2, 5),
				player_idx);
		} else {
			player_set_target(
				player_cycle_target(g_players[player_idx]
							    .target_cycle_start,
						    -1, player_idx, 2, 5),
				player_idx);
		}
		return;
	case FLIGHT_KEY_F3:
		if (g_players[player_idx].current_target_object_idx != -1) {
			player_set_target(
				player_cycle_target(
					g_players[player_idx]
						.current_target_object_idx,
					1, player_idx, 3, 5),
				player_idx);
		} else {
			player_set_target(
				player_cycle_target(g_players[player_idx]
							    .target_cycle_start,
						    1, player_idx, 3, 5),
				player_idx);
		}
		return;
	case FLIGHT_KEY_F4:
		if (g_players[player_idx].current_target_object_idx != -1) {
			player_set_target(
				player_cycle_target(
					g_players[player_idx]
						.current_target_object_idx,
					-1, player_idx, 3, 5),
				player_idx);
		} else {
			player_set_target(
				player_cycle_target(g_players[player_idx]
							    .target_cycle_start,
						    -1, player_idx, 3, 5),
				player_idx);
		}
		return;
	case FLIGHT_KEY_F5:
	case FLIGHT_KEY_F6:
	case FLIGHT_KEY_F7: {
		int target_index =
			g_players[player_idx]
				.target_preset_slot[g_current_action_key -
						    FLIGHT_KEY_F5];
		if (target_index != -1 &&
		    g_object_table[target_index].object_type != 0 &&
		    object_has_active_decoy_beam((uint16_t)target_index) == 0) {
			player_set_target(target_index, player_idx);
		}
		return;
	}
	case FLIGHT_KEY_SHIFT_F5:
	case FLIGHT_KEY_SHIFT_F6:
	case FLIGHT_KEY_SHIFT_F7:
		if (g_players[player_idx].current_target_object_idx == -1) {
			fsfx_play_sound(FLIGHT_SOUND_SMALL_CLICK, -1,
					player_idx);
		} else {
			g_players[player_idx]
				.target_preset_slot[g_current_action_key -
						    FLIGHT_KEY_SHIFT_F5] =
				g_players[player_idx].current_target_object_idx;
			XVT_LOG_DEBUG(
				"flight.target_preset_saved slot=%d preset=%d target=%d predicted=%d",
				player_idx,
				(int)(g_current_action_key -
				      FLIGHT_KEY_SHIFT_F5),
				(int)g_players[player_idx]
					.current_target_object_idx,
				g_flight_sim_side_effects_suppressed);
			fsfx_play_sound(FLIGHT_SOUND_TARGET_SELECTED, -1,
					player_idx);
		}
		return;
	case FLIGHT_KEY_LEFT:
	case FLIGHT_KEY_RIGHT: {
		if (player_idx != g_local_player ||
		    g_flight_sim_side_effects_suppressed != 0) {
			return;
		}
		int16_t page;
		int16_t page_found = 0;
		if (g_mfd_active_page == MFD_PAGE_NONE) {
			for (page = MFD_PAGE_SCOREBOARD; page < MFD_PAGE_COUNT;
			     ++page) {
				if (g_mfd_page_states[page] ==
				    MFD_PAGE_STATE_OPEN) {
					page_found = 1;
					break;
				}
			}
			if (page_found != 0) {
				g_mfd_active_page = page;
			}
			return;
		}
		for (page = g_mfd_active_page + 1; page <= MFD_PAGE_COUNT;
		     ++page) {
			if (page == MFD_PAGE_COUNT) {
				page = MFD_PAGE_SCOREBOARD;
			}
			if (g_mfd_page_states[page] == MFD_PAGE_STATE_OPEN) {
				page_found = 1;
				break;
			}
		}
		if (page_found != 0 && page != g_mfd_active_page) {
			g_mfd_secondary_page = g_mfd_active_page;
			g_mfd_active_page = page;
		}
		return;
	}
	default:
		return;
	}
}

/* Sets g_graphics_detail_distance_threshold, g_star_grid_divisor, g_backdrops_enabled
 * and g_debris_enabled from the preset's entry in the ByPreset tables, and
 * g_transform_light_direction_to_object_space to 1; returns g_debris_enabled. Does
 * not check that the preset is 0 to 3. */
// FUNCTION: XVT 0x484160
char flight_apply_graphics_detail_preset(uint16_t preset)
{
	g_graphics_detail_distance_threshold =
		g_graphics_detail_distance_threshold_by_preset[preset];
	g_star_grid_divisor =
		g_star_grid_divisor_by_graphics_detail_preset[preset];
	g_backdrops_enabled =
		(uint8_t)g_backdrops_enabled_by_graphics_detail_preset[preset];
	g_debris_enabled =
		(uint8_t)g_debris_enabled_by_graphics_detail_preset[preset];
	g_transform_light_direction_to_object_space = 1;
	XVT_LOG_DEBUG(
		"flight.detail_preset preset=%u distance=%u stars=%u backdrops=%u debris=%u predicted=%d",
		(unsigned)preset,
		(unsigned)g_graphics_detail_distance_threshold,
		(unsigned)g_star_grid_divisor, (unsigned)g_backdrops_enabled,
		(unsigned)g_debris_enabled,
		g_flight_sim_side_effects_suppressed);
	return (char)g_debris_enabled;
}

/* Sets g_flight_main_window_handle to the frontend's main window and returns 1;
 * the original build also updates the window and gives it the focus. */
// FUNCTION: XVT 0x4AA6F0
int flight_update_and_focus_main_window(void)
{
	g_flight_main_window_handle = frontend_display_get_main_window_handle();
	return 1;
}

/* In the original build, brings the main window to the front when it is not
 * there (resetting an 8-bit palette, flight_palette_reset_if8_bit, and hiding the
 * cursor), then takes one waiting window message and dispatches it unless it is
 * 0x06, 0x08, 0x1C, 0x1F or 0x86. Returns the dispatch's result, the message
 * number when not dispatched, or 0 with no message. The modern build returns
 * 0. */
// FUNCTION: XVT 0x4AA720
int32_t flight_pump_window_messages(void) { return 0; }

/* Flight's part of the window procedure, called by frontend_display_wnd_proc:
 * resets an 8-bit palette on message 0x311 (flight_palette_reset_if8_bit), and in
 * the original build passes message 0x0F to DefWindowProcA and returns its
 * result. Returns 0 otherwise. */
// FUNCTION: XVT 0x4AA7B0
int32_t flight_wnd_proc(void *hWnd, unsigned int Msg, uint32_t wParam,
			int32_t lParam)
{
	if (Msg == 0x311) {
		flight_palette_reset_if8_bit();
	}
	(void)hWnd;
	(void)wParam;
	(void)lParam;
	return 0;
}

/* Steers each craft in the active region's craft slots, or only
 * g_single_object_update_override_idx when that is set, and sets its speed, over
 * g_elapsed_ticks plus the ticks its own state lags g_game_time
 * (sim_state_timestamp) when that is set, else over g_elapsed_ticks. An AI craft
 * (no player owner) with working systems and not held by a beam rolls, pitches
 * and turns toward its AI controller's targets at its flight rates, banking as
 * it turns; pitching through a loop turns its yaw and roll half a circle.
 * Climbs end and dives pull out (flight_update_dive_pullout_pitch_target). An
 * active craft's speed moves toward its commanded speed or its throttle's share
 * of a top speed that rises or falls with the energy settings and doubles
 * unless engine_overdrive_off is set; a player's throttle counts only with
 * working engines. Breaking up and exploding craft slow to their top speed,
 * disabled craft slow by 20 per simulated second, and craft entering hyperspace
 * speed up by 50, 200 or 500 per simulated second. A changed orientation is
 * marked for recomputing, and a carried object takes the craft's orientation.
 * Sets g_cur_craft and g_cur_craft_model_index, and puts g_elapsed_ticks and
 * g_sim_steps_per_second back at the end. The modern build steers with its own
 * integration when the timing is unlocked. */
// FUNCTION: XVT 0x4ACE80
void flight_update_craft_steering_and_speed(void)
{
	struct ai_controller *controller;
	int player_owner;
	int simulation_rate;
	int saved_sim_steps_per_second;
	uint16_t object_index;
	int override_processed;
	int saved_elapsed_ticks;
	uint16_t throttle_fraction;
	uint16_t old_pitch;
	uint16_t old_yaw;
	uint16_t old_roll;
	int object_idx;

	simulation_rate = SIMULATION_TICKS_PER_SECOND;
	saved_sim_steps_per_second = g_sim_steps_per_second;
	object_index = (uint16_t)g_active_region_object_slot_start;
	override_processed = 0;
	saved_elapsed_ticks = g_elapsed_ticks;
	for (; object_index < g_active_region_craft_object_slot_end;
	     ++object_index) {
		if (g_single_object_update_override_idx != -1) {
			if (override_processed != 0) {
				break;
			}
			override_processed = 1;
			object_index =
				(uint16_t)g_single_object_update_override_idx;
		}
		g_sim_steps_per_second = (uint16_t)saved_sim_steps_per_second;
		g_elapsed_ticks = (uint16_t)saved_elapsed_ticks;
		object_idx = object_index;
		if (g_object_table[object_idx].mobj != NULL) {
			if (g_object_table[object_idx]
				    .mobj->sim_state_timestamp != 0) {
				g_elapsed_ticks =
					(uint16_t)(g_elapsed_ticks +
						   g_game_time -
						   g_object_table[object_idx]
							   .mobj
							   ->sim_state_timestamp);
				if (g_elapsed_ticks == 0) {
					continue;
				}
				g_sim_steps_per_second =
					(uint16_t)(SIMULATION_TICKS_PER_SECOND /
						   g_elapsed_ticks);
				if (g_sim_steps_per_second == 0) {
					g_sim_steps_per_second = 1;
				}
			}
		}
		if (g_object_table[object_idx].object_type == 0 ||
		    g_object_table[object_idx].mobj->family != 0) {
			continue;
		}

		throttle_fraction = 0;
		g_cur_craft = g_object_table[object_idx].mobj->p_craft;
		controller = &g_cur_craft->ai_controller;
		old_pitch = g_object_table[object_idx].pitch;
		old_yaw = g_object_table[object_idx].yaw;
		old_roll = g_object_table[object_idx].roll;
		g_cur_craft_model_index = g_cur_craft->model_index;
		player_owner = g_object_table[object_idx].player_owner_idx;
		if (xvt_flight_timing_is_unlocked()) {
			if (player_owner != -1 ||
			    !g_cur_craft->working_subsystems ||
			    g_cur_craft->beam_effect_accum[1]) {
				xvt_flight_integration_clear(
					object_idx, XVT_INTEGRATE_ROLL);
				xvt_flight_integration_clear(
					object_idx, XVT_INTEGRATE_PITCH);
				xvt_flight_integration_clear(
					object_idx, XVT_INTEGRATE_TURN);
				xvt_flight_integration_clear(
					object_idx, XVT_INTEGRATE_BANK);
			}

			if (g_cur_craft->ai_flight.roll_state < 1 ||
			    g_cur_craft->ai_flight.roll_state > 3) {
				xvt_flight_integration_clear(
					object_idx, XVT_INTEGRATE_ROLL);
			}
			if (g_cur_craft->ai_flight.pitch_state != 1 &&
			    g_cur_craft->ai_flight.pitch_state != 2) {
				xvt_flight_integration_clear(
					object_idx, XVT_INTEGRATE_PITCH);
			}
			if (!g_cur_craft->ai_flight.turn_state ||
			    controller->target_xy_angle ==
				    g_object_table[object_idx].yaw) {
				xvt_flight_integration_clear(
					object_idx, XVT_INTEGRATE_TURN);
			}
		}
		if (player_owner != -1) {
			if ((g_cur_craft->working_subsystems &
			     CRAFT_SUBSYSTEM_FLAG_ENGINES) != 0) {
				throttle_fraction = g_cur_craft->throttle_speed;
			}
		} else if (g_cur_craft->working_subsystems != 0) {
			throttle_fraction = g_cur_craft->throttle_speed;
		}

		if (player_owner == -1 &&
		    g_cur_craft->working_subsystems != 0 &&
		    g_cur_craft->beam_effect_accum[1] == 0) {
			uint8_t roll_state;

			roll_state = g_cur_craft->ai_flight.roll_state;
			if (roll_state >= 1 && roll_state <= 3) {
				uint16_t roll_delta;
				uint16_t raw_roll_step;
				uint16_t roll_step;

				roll_delta =
					(uint16_t)(controller->target_roll -
						   g_object_table[object_idx]
							   .roll);
				raw_roll_step =
					(uint16_t)((uint16_t)g_elapsed_ticks *
						   (uint16_t)g_cur_craft
							   ->ai_flight
							   .roll_rate /
						   simulation_rate);
				roll_step = (uint16_t)math2_fraction(
					raw_roll_step,
					(uint16_t)g_cur_craft->ai_flight
						.roll_accel);
				roll_step =
					(uint16_t)(2 *
						   math2_fraction(
							   roll_step,
							   g_cur_craft
								   ->ai_flight
								   .roll_step));
				if (xvt_flight_timing_is_unlocked()) {
					roll_step =
						(uint16_t)(2 *
							   xvt_flight_integration_steer(
								   object_idx,
								   XVT_INTEGRATE_ROLL,
								   g_cur_craft
									   ->ai_flight
									   .roll_rate,
								   g_cur_craft
									   ->ai_flight
									   .roll_accel,
								   g_cur_craft
									   ->ai_flight
									   .roll_step,
								   (int16_t)(controller
										     ->target_roll -
									     g_object_table[object_idx]
										     .roll) <
										   0
									   ? -1
									   : 1));
				}
				if (g_cur_craft->ai_flight.roll_state != 3) {
					if (roll_delta < 0x8000u) {
						if (roll_delta <= roll_step) {
							g_object_table
								[object_idx]
									.roll =
								controller
									->target_roll;
							g_cur_craft->ai_flight
								.roll_state = 4;
							XVT_LOG_DEBUG(
								"flight.maneuver object=%d stage=\"roll_reached\" angle=%u predicted=%d",
								object_idx,
								(unsigned)g_object_table
									[object_idx]
										.roll,
								g_flight_sim_side_effects_suppressed);
						} else {
							g_object_table
								[object_idx]
									.roll +=
								roll_step;
						}
					} else if ((uint16_t)-roll_delta >
						   roll_step) {
						g_object_table[object_idx]
							.roll -= roll_step;
					} else {
						g_object_table[object_idx]
							.roll =
							controller->target_roll;
						g_cur_craft->ai_flight
							.roll_state = 4;
						XVT_LOG_DEBUG(
							"flight.maneuver object=%d stage=\"roll_reached\" angle=%u predicted=%d",
							object_idx,
							(unsigned)g_object_table
								[object_idx]
									.roll,
							g_flight_sim_side_effects_suppressed);
					}
				} else if (controller->target_roll < 0x8000u) {
					g_object_table[object_idx].roll +=
						roll_step;
				} else {
					g_object_table[object_idx].roll -=
						roll_step;
				}
			}

			if (g_cur_craft->ai_flight.pitch_state != 0) {
				uint16_t pitch_delta;
				uint16_t raw_pitch_step;
				uint16_t pitch_step;

				pitch_delta =
					(uint16_t)(controller->target_z_angle -
						   g_cur_craft->pitch);
				if (pitch_delta >= 0x8000u) {
					pitch_delta = (uint16_t)-pitch_delta;
				}
				raw_pitch_step =
					(uint16_t)((uint16_t)g_elapsed_ticks *
						   (uint16_t)g_cur_craft
							   ->ai_flight
							   .pitch_rate /
						   simulation_rate);
				pitch_step = (uint16_t)math2_fraction(
					raw_pitch_step,
					(uint16_t)g_cur_craft->ai_flight
						.pitch_accel);
				pitch_step = (uint16_t)math2_fraction(
					pitch_step, g_cur_craft->ai_flight
							    .pitch_step_scale);
				if (xvt_flight_timing_is_unlocked() &&
				    g_cur_craft->ai_flight.pitch_state <= 2) {
					pitch_step = (uint16_t)(xvt_flight_integration_steer(
						object_idx, XVT_INTEGRATE_PITCH,
						g_cur_craft->ai_flight
							.pitch_rate,
						g_cur_craft->ai_flight
							.pitch_accel,
						g_cur_craft->ai_flight
							.pitch_step_scale,
						g_cur_craft->ai_flight.pitch_state ==
								1
							? -1
							: 1));
				}
				(void)math2_fraction(pitch_step, 0x8000u);
				if (g_cur_craft->ai_flight.pitch_state == 1) {
					if (pitch_step < pitch_delta ||
					    g_cur_craft->ai_flight
							    .pitch_through_loop !=
						    0) {
						g_cur_craft->pitch -=
							pitch_step;
						if (g_cur_craft->pitch >=
						    0xE000u) {
							g_cur_craft->pitch =
								(uint16_t)-g_cur_craft
									->pitch;
							g_object_table
								[object_idx]
									.yaw -=
								0x8000u;
							g_object_table
								[object_idx]
									.roll -=
								0x8000u;
							g_cur_craft->ai_flight
								.pitch_through_loop =
								0;
							g_cur_craft->ai_flight
								.pitch_state =
								2;
							XVT_LOG_DEBUG(
								"flight.maneuver object=%d stage=\"looped\" angle=%u predicted=%d",
								object_idx,
								(unsigned)g_cur_craft
									->pitch,
								g_flight_sim_side_effects_suppressed);
						}
					} else {
						g_cur_craft->pitch =
							controller
								->target_z_angle;
						g_cur_craft->ai_flight
							.pitch_state = 3;
						XVT_LOG_DEBUG(
							"flight.maneuver object=%d stage=\"pitch_reached\" angle=%u predicted=%d",
							object_idx,
							(unsigned)g_cur_craft
								->pitch,
							g_flight_sim_side_effects_suppressed);
					}
				} else if (g_cur_craft->ai_flight.pitch_state ==
					   2) {
					if (pitch_step < pitch_delta ||
					    g_cur_craft->ai_flight
							    .pitch_through_loop !=
						    0) {
						g_cur_craft->pitch +=
							pitch_step;
						if (g_cur_craft->pitch >=
						    0x8000u) {
							g_cur_craft->pitch =
								(uint16_t)-g_cur_craft
									->pitch;
							g_object_table
								[object_idx]
									.yaw -=
								0x8000u;
							g_object_table
								[object_idx]
									.roll -=
								0x8000u;
							g_cur_craft->ai_flight
								.pitch_through_loop =
								0;
							g_cur_craft->ai_flight
								.pitch_state =
								1;
							XVT_LOG_DEBUG(
								"flight.maneuver object=%d stage=\"looped\" angle=%u predicted=%d",
								object_idx,
								(unsigned)g_cur_craft
									->pitch,
								g_flight_sim_side_effects_suppressed);
						}
					} else {
						g_cur_craft->pitch =
							controller
								->target_z_angle;
						g_cur_craft->ai_flight
							.pitch_state = 3;
						XVT_LOG_DEBUG(
							"flight.maneuver object=%d stage=\"pitch_reached\" angle=%u predicted=%d",
							object_idx,
							(unsigned)g_cur_craft
								->pitch,
							g_flight_sim_side_effects_suppressed);
					}
				}
			}

			if (g_cur_craft->object_kind !=
				    CRAFT_OBJECT_KIND_DISABLED &&
			    g_cur_craft->ai_flight.turn_state >= 1) {
				uint16_t turn_delta;

				turn_delta =
					(uint16_t)(controller->target_xy_angle -
						   g_object_table[object_idx]
							   .yaw);
				if (turn_delta != 0) {
					uint16_t raw_turn_step;
					uint16_t turn_step;

					raw_turn_step =
						(uint16_t)((uint16_t)
								   g_elapsed_ticks *
							   (uint16_t)g_cur_craft
								   ->ai_flight
								   .turn_rate /
							   simulation_rate);
					turn_step = (uint16_t)math2_fraction(
						raw_turn_step,
						(uint16_t)g_cur_craft->ai_flight
							.turn_accel);
					turn_step = (uint16_t)math2_fraction(
						turn_step,
						g_cur_craft->ai_flight
							.turn_step);
					if (xvt_flight_timing_is_unlocked()) {
						turn_step = (uint16_t)(xvt_flight_integration_steer(
							object_idx,
							XVT_INTEGRATE_TURN,
							g_cur_craft->ai_flight
								.turn_rate,
							g_cur_craft->ai_flight
								.turn_accel,
							g_cur_craft->ai_flight
								.turn_step,
							(int16_t)turn_delta < 0
								? -1
								: 1));
					}
					if (turn_delta < 0x8000u &&
					    turn_step < turn_delta) {
						g_object_table[object_idx]
							.yaw += turn_step;
					} else if (
						turn_delta >= 0x8000u &&
						turn_step <
							(uint16_t)-turn_delta) {
						g_object_table[object_idx]
							.yaw -= turn_step;
					} else {
						g_object_table[object_idx].yaw =
							controller
								->target_xy_angle;
						turn_step = 0;
						g_cur_craft->ai_flight
							.turn_state = 3;
						XVT_LOG_DEBUG(
							"flight.maneuver object=%d stage=\"turn_reached\" angle=%u predicted=%d",
							object_idx,
							(unsigned)g_object_table
								[object_idx]
									.yaw,
							g_flight_sim_side_effects_suppressed);
					}
					if (g_cur_craft->ai_flight.roll_state ==
						    0 ||
					    g_cur_craft->ai_flight.roll_state ==
						    4) {
						int16_t bank;

						bank = (int16_t)math2_fraction(
							turn_step,
							g_model_defs[g_cur_craft_model_index]
								.auto_bank_factor);
						if (xvt_flight_timing_is_unlocked()) {
							unsigned fraction =
								g_model_defs[g_cur_craft_model_index]
									.auto_bank_factor;
							int signed_bank = xvt_flight_integration_rate(
								object_idx,
								XVT_INTEGRATE_BANK,
								(int16_t)turn_delta <
										0
									? -(int)turn_step
									: turn_step,
								fraction == UINT16_MAX
									? 65536u
									: fraction,
								65536);
							bank = (int16_t)(signed_bank < 0
										 ? -signed_bank
										 : signed_bank);
						}
						if (turn_delta < 0x8000u) {
							g_object_table
								[object_idx]
									.roll -=
								bank;
						} else {
							g_object_table
								[object_idx]
									.roll +=
								bank;
						}
					}
				}
			}
		}

		if (xvt_flight_timing_reference_due()) {
			struct xvt_flight_clock decision_clock =
				xvt_flight_timing_enter_reference();
			if (g_object_table[object_idx].player_owner_idx == -1 &&
			    g_cur_craft->ai_flight.climb_state == 1 &&
			    g_cur_craft->working_subsystems != 0 &&
			    g_cur_craft->beam_effect_accum[1] == 0 &&
			    controller->aim_point_z <=
				    g_object_table[object_idx].world_z) {
				g_cur_craft->ai_flight.climb_state = 0;
				g_cur_craft->pitch = 0x4000;
			}
			if (g_object_table[object_idx].player_owner_idx == -1 &&
			    g_cur_craft->ai_flight.dive_state == 1 &&
			    g_cur_craft->working_subsystems != 0 &&
			    g_cur_craft->beam_effect_accum[1] == 0) {
				flight_update_dive_pullout_pitch_target(
					object_idx);
			}

			xvt_flight_timing_restore_clock(decision_clock);
		}

		switch (g_cur_craft->object_kind) {
		case CRAFT_OBJECT_KIND_ACTIVE: {
			uint16_t commanded_speed;
			uint16_t max_speed;
			int16_t speed_bias;

			g_object_table[object_idx].pitch = g_cur_craft->pitch;
			commanded_speed = g_cur_craft->commanded_speed;
			if (commanded_speed == 0 ||
			    throttle_fraction != UINT16_MAX) {
				max_speed =
					g_cur_craft->ai_flight.max_speed_cache;
				speed_bias = 6 -
					     (uint8_t)g_cur_craft
						     ->shield_recharge_level -
					     (uint8_t)g_cur_craft
						     ->beam_recharge_level -
					     (uint8_t)g_cur_craft
						     ->laser_recharge_level;
				if (g_object_table[object_idx].object_type ==
				    7) {
					max_speed += speed_bias *
						     math2_fraction(max_speed,
								    0x1000u);
				} else if (g_object_table[object_idx]
							   .object_type == 5 &&
					   speed_bias > 0) {
					max_speed += speed_bias *
						     math2_fraction(max_speed,
								    0x3000u);
				} else {
					max_speed += speed_bias *
						     math2_fraction(max_speed,
								    0x2000u);
				}
				commanded_speed = (uint16_t)math2_fraction(
					max_speed, throttle_fraction);
				if (g_cur_craft->engine_overdrive_off == 0) {
					commanded_speed += commanded_speed;
				}
			}
			if (commanded_speed <
			    g_object_table[object_idx].mobj->speed) {
				if ((unsigned int)(g_object_table[object_idx]
							   .mobj->speed -
						   commanded_speed) < 200) {
					flight_slew_object_speed_toward_target(
						object_idx, commanded_speed, 1,
						throttle_fraction);
				} else {
					flight_decelerate_object_speed(
						object_idx,
						(unsigned int)(g_object_table[object_idx]
								       .mobj
								       ->speed -
							       commanded_speed) /
							3u);
				}
			} else {
				flight_slew_object_speed_toward_target(
					object_idx, commanded_speed, 1,
					throttle_fraction);
			}
			break;
		}
		case CRAFT_OBJECT_KIND_UNKNOWN_1:
		case CRAFT_OBJECT_KIND_BREAKING_UP:
		case CRAFT_OBJECT_KIND_EXPLODING:
			if (g_object_table[object_idx].mobj->speed >
			    (unsigned int)(uint16_t)
				    g_cur_craft->ai_flight.max_speed_cache) {
				flight_decelerate_object_speed(
					object_idx,
					(g_object_table[object_idx]
						 .mobj->speed -
					 (uint16_t)g_cur_craft->ai_flight
						 .max_speed_cache) /
						3);
			}
			/* fall through */
		case CRAFT_OBJECT_KIND_ARRIVING_FROM_HYPERSPACE:
			g_cur_craft->ai_flight.climb_state = 0;
			g_cur_craft->ai_flight.dive_state = 0;
			g_cur_craft->ai_flight.roll_state = 0;
			g_cur_craft->ai_flight.pitch_state = 0;
			break;
		case CRAFT_OBJECT_KIND_DISABLED:
			if (g_object_table[object_idx].mobj->speed > 0) {
				flight_decelerate_object_speed(object_idx, 20);
			}
			break;
		case CRAFT_OBJECT_KIND_ENTERING_HYPERSPACE:
			if (g_cur_craft->working_subsystems != 0) {
				if (controller->secondary_maneuver_timer != 0) {
					flight_accelerate_object_speed(
						object_idx, 50);
				} else if (controller->maneuver_timer != 0) {
					flight_accelerate_object_speed(
						object_idx, 200);
				} else {
					flight_accelerate_object_speed(
						object_idx, 500);
				}
			} else {
				if (g_object_table[object_idx].mobj->speed >
				    0) {
					flight_decelerate_object_speed(
						object_idx, 20);
				}
				if (g_object_table[object_idx].mobj->speed ==
				    0) {
					g_cur_craft->object_kind =
						CRAFT_OBJECT_KIND_ACTIVE;
					XVT_LOG_DEBUG(
						"flight.hyperspace_stalled object=%d fg=%d predicted=%d",
						object_idx,
						(int)g_object_table[object_idx]
							.flight_group_idx,
						g_flight_sim_side_effects_suppressed);
				}
			}
			break;
		default:
			break;
		}

		g_cur_craft->yaw = g_object_table[object_idx].yaw;
		if (g_object_table[object_idx].pitch != old_pitch ||
		    g_object_table[object_idx].yaw != old_yaw ||
		    g_object_table[object_idx].roll != old_roll) {
			g_object_table[object_idx].mobj->move_vector_dirty = 1;
			g_object_table[object_idx].mobj->orient_matrix_dirty =
				1;
		}
		if (g_cur_craft->carried_object_index != UINT16_MAX) {
			uint16_t carried_object_index;

			carried_object_index =
				g_cur_craft->carried_object_index;
			if (g_object_table[carried_object_index].mobj != NULL) {
				g_object_table[carried_object_index].pitch =
					g_object_table[object_idx].pitch;
				g_object_table[carried_object_index].yaw =
					g_object_table[object_idx].yaw;
				g_object_table[carried_object_index].roll =
					g_object_table[object_idx].roll;
				g_object_table[carried_object_index]
					.mobj->p_craft->pitch =
					g_cur_craft->pitch;
				g_object_table[carried_object_index]
					.mobj->move_vector_dirty = 1;
				g_object_table[carried_object_index]
					.mobj->orient_matrix_dirty = 1;
			}
		}
	}
	g_sim_steps_per_second = (uint16_t)saved_sim_steps_per_second;
	g_elapsed_ticks = (uint16_t)saved_elapsed_ticks;
}

/* Moves an object's speed toward target_speed by one step's worth: when below,
 * it accelerates by at most a quarter of the craft model's accel_rate (at least
 * 1) plus the rest of it times throttle_fraction over 65,536, tripled unless
 * engine_overdrive_off is set; when above and allow_decel is 1, it decelerates by
 * at most a quarter of decel_rate plus the rest times 65,535 less
 * throttle_fraction over 65,536. The rates are per simulated second
 * (flight_accelerate_object_speed, flight_decelerate_object_speed). The model is
 * g_cur_craft_model_index's. */
// FUNCTION: XVT 0x4AD880
void flight_slew_object_speed_toward_target(unsigned int object_idx,
					    int target_speed, int allow_decel,
					    int throttle_fraction)
{
	uint32_t speed_delta = target_speed;
	speed_delta -= g_object_table[object_idx].mobj->speed;
	if (speed_delta == 0) {
		return;
	}

	if (speed_delta < 0x8000u) {
		uint32_t step = (uint16_t)math2_fraction(
			g_model_defs[g_cur_craft_model_index].accel_rate,
			0x4000u);
		if (step == 0) {
			step = 1;
		}
		step += (uint16_t)math2_fraction(
			(uint16_t)(g_model_defs[g_cur_craft_model_index]
					   .accel_rate -
				   step),
			throttle_fraction);
		if (g_object_table[object_idx]
			    .mobj->p_craft->engine_overdrive_off == 0) {
			step *= 3;
		}
		if (speed_delta >= step) {
			speed_delta = step;
		}
		flight_accelerate_object_speed(object_idx, speed_delta);
	} else if (allow_decel == 1) {
		uint32_t step = (uint16_t)math2_fraction(
			g_model_defs[g_cur_craft_model_index].decel_rate,
			0x4000u);
		if (step == 0) {
			step = 1;
		}
		step += (uint16_t)math2_fraction(
			(uint16_t)(g_model_defs[g_cur_craft_model_index]
					   .decel_rate -
				   step),
			(uint16_t)(0xFFFFu - throttle_fraction));
		speed_delta = -speed_delta;
		if (speed_delta >= step) {
			speed_delta = step;
		}
		flight_decelerate_object_speed(object_idx, speed_delta);
	}
}

/* Adds acceleration_per_second times g_elapsed_ticks over 236 to an object's mobj
 * speed, carrying the fraction in speed_remainder (units of 1/65,536), and caps
 * the speed at 3,600. Does not check that the object has a mobile object. */
// FUNCTION: XVT 0x4AD9E0
void flight_accelerate_object_speed(int object_idx, int acceleration_per_second)
{
	uint32_t product = (uint32_t)(uint16_t)g_elapsed_ticks *
			   (uint32_t)acceleration_per_second;
	uint32_t whole_quotient = product / 236u;
	uint16_t whole_delta = (uint16_t)whole_quotient;
	uint16_t frac_delta =
		(uint16_t)(((product - whole_quotient * 236u) << 16) / 236u);

	uint16_t *speed_remainder =
		&g_object_table[object_idx].mobj->speed_remainder;
	uint16_t old_remainder = *speed_remainder;
	*speed_remainder = (uint16_t)(old_remainder + frac_delta);
	if (g_object_table[object_idx].mobj->speed_remainder < old_remainder) {
		++g_object_table[object_idx].mobj->speed;
	}
	g_object_table[object_idx].mobj->speed += whole_delta;
	if (g_object_table[object_idx].mobj->speed > 3600u) {
		g_object_table[object_idx].mobj->speed = 3600;
	}
}

/* Takes deceleration_per_second times g_elapsed_ticks over 236 from an object's
 * mobj speed, carrying the fraction in speed_remainder, and sets 0 when the
 * speed would go below 0. Does not check that the object has a mobile
 * object. */
// FUNCTION: XVT 0x4ADA90
void flight_decelerate_object_speed(int object_idx, int deceleration_per_second)
{
	uint32_t product = (uint32_t)(uint16_t)g_elapsed_ticks *
			   (uint32_t)deceleration_per_second;
	uint32_t whole_quotient = product / 236u;
	uint16_t whole_delta = (uint16_t)whole_quotient;
	uint16_t frac_delta =
		(uint16_t)(((product - whole_quotient * 236u) << 16) / 236u);

	uint16_t *speed_remainder =
		&g_object_table[object_idx].mobj->speed_remainder;
	uint16_t old_remainder = *speed_remainder;
	*speed_remainder = old_remainder;
	*speed_remainder -= frac_delta;
	if (g_object_table[object_idx].mobj->speed_remainder > old_remainder) {
		--g_object_table[object_idx].mobj->speed;
	}
	g_object_table[object_idx].mobj->speed -= whole_delta;
	if (g_object_table[object_idx].mobj->speed > 0x8000u) {
		g_object_table[object_idx].mobj->speed = 0;
	}
}

/* For an AI craft in a dive: at 256 or less above its AI controller's aim_point_z
 * it levels out (craft pitch 0x4000, pitch_state 0, dive_state 2). Otherwise,
 * once its height above that point is no more than minus its vertical move
 * component (move_z) times g_sim_steps_per_second, times 3 for genus 0 and 2 for
 * others, and its pitch is above 0x4000, it aims the pitch halfway back to
 * level (target_z_angle, pitch_state 1). Recomputes the move vector when it is out
 * of date. Works on g_cur_craft, which must be the object's craft. */
// FUNCTION: XVT 0x4ADB50
void flight_update_dive_pullout_pitch_target(int object_idx)
{
	struct ai_controller *controller = &g_cur_craft->ai_controller;
	struct object_record *object = &g_object_table[object_idx];
	int altitude_delta = object->world_z - controller->aim_point_z;
	if (altitude_delta < 0 || altitude_delta <= 0x100) {
		g_cur_craft->pitch = 0x4000;
		g_cur_craft->ai_flight.pitch_state = 0;
		g_cur_craft->ai_flight.dive_state = 2;
	} else {
		if (object->mobj->move_vector_dirty != 0) {
			fview_calcrotatemove(object->pitch, object->yaw,
					     object);
		}
		int move_z = object->mobj->move_z;
		int projected_movement = move_z;
		projected_movement *= g_sim_steps_per_second;
		if (object->genus_id == 0) {
			projected_movement *= 3;
		} else {
			projected_movement *= 2;
		}
		if (altitude_delta <= -projected_movement &&
		    g_cur_craft->pitch > 0x4000u) {
			controller->target_z_angle =
				(uint16_t)(((g_cur_craft->pitch - 0x4000) >>
					    1) +
					   0x4000);
			g_cur_craft->ai_flight.pitch_state = 1;
		}
	}
}
