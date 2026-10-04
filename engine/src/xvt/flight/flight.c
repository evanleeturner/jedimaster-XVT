#include "xvt/flight/flight.h"
#ifdef XVT_MODERN
#include "xvt_runtime/timing/flight_integration.h"
#include "xvt_runtime/timing/flight_timing.h"
#endif
#ifdef XVT_MODERN
#include "xvt_runtime/runtime/flight_sim.h"
#include "xvt_runtime/runtime/port.h"
#include "xvt_runtime/snapshot/world_state.h"
#endif

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

#ifndef XVT_MODERN
/* The Windows message record PeekMessageA fills in flight_pump_window_messages,
 * in the original build. */
/* drift-ok: camelcase -- wParam, lParam: Windows' MSG */
struct flight_win32_message {
	void *window; /* Window the message is for; not read by name. */
	/* Message number; flight_pump_window_messages tests it. */
	uint32_t message;
	uint32_t wParam; /* First message argument; not read by name. */
	int32_t lParam;	 /* Second message argument; not read by name. */
	uint32_t time;	 /* Time the message was posted; not read by name. */
	int32_t point_x; /* Cursor X when posted; not read by name. */
	int32_t point_y; /* Cursor Y when posted; not read by name. */
};

__declspec(dllimport) int __stdcall UpdateWindow(void *hWnd);
__declspec(dllimport) void *__stdcall SetFocus(void *hWnd);
__declspec(dllimport) int32_t __stdcall
DefWindowProcA(void *hWnd, unsigned int Msg, uint32_t wParam, int32_t lParam);
__declspec(dllimport) void *__stdcall GetForegroundWindow(void);
__declspec(dllimport) int __stdcall SetForegroundWindow(void *hWnd);
__declspec(dllimport) int __stdcall ShowCursor(int show);
__declspec(dllimport) int __stdcall
PeekMessageA(struct flight_win32_message *message, void *hWnd,
	     unsigned int filter_min, unsigned int filter_max,
	     unsigned int remove_message);
__declspec(dllimport) int __stdcall
TranslateMessage(const struct flight_win32_message *message);
__declspec(dllimport) int32_t __stdcall
DispatchMessageA(const struct flight_win32_message *message);
#endif

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
	int player_index;
	int object_index;
	uint16_t *timer;

	timer = (uint16_t *)&g_flight_global_countdown_timers;
	for (index = 0; index < (int)(sizeof(g_flight_global_countdown_timers) /
				      sizeof(*timer));
	     ++index) {
		if (timer[index] != 0) {

#ifdef XVT_MODERN
			timer[index] -=
				xvt_flight_timing_is_unlocked() &&
						((index >= 1 && index <= 5) ||
						 index == 10)
					? xvt_flight_timing_reference_elapsed()
					: g_elapsed_ticks;
#else
			timer[index] -= g_elapsed_ticks;
#endif

			if ((int16_t)timer[index] < 0) {
				timer[index] = 0;
			}
		}
	}

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

	for (object_index = g_active_region_object_slot_start;
	     object_index < g_active_region_craft_object_slot_end;
	     ++object_index) {
		if (g_object_table[object_index].object_type != 0) {
			struct ai_controller *controller;

			g_cur_craft =
				g_object_table[object_index].mobj->p_craft;
			controller = &g_cur_craft->ai_controller;
			if (controller->think_timer != 0) {
#ifdef XVT_MODERN
				controller->think_timer -=
					xvt_flight_timing_reference_elapsed();
#else
				controller->think_timer -= g_elapsed_ticks;
#endif
			}

			if (controller->maneuver_timer != 0) {

#ifdef XVT_MODERN
				controller->maneuver_timer -=
					xvt_flight_timing_reference_elapsed();
#else
				controller->maneuver_timer -= g_elapsed_ticks;
#endif

				if (controller->maneuver_timer < 0) {
					controller->maneuver_timer = 0;
				}
			}
			if (controller->secondary_maneuver_timer != 0) {

#ifdef XVT_MODERN
				controller->secondary_maneuver_timer -=
					xvt_flight_timing_reference_elapsed();
#else
				controller->secondary_maneuver_timer -=
					g_elapsed_ticks;
#endif

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
#ifdef XVT_MODERN
						controller->think_timer -=
							xvt_flight_timing_reference_elapsed();
#else
						controller->think_timer -=
							g_elapsed_ticks;
#endif
					}

					if (controller->maneuver_timer != 0) {

#ifdef XVT_MODERN
						controller->maneuver_timer -=
							xvt_flight_timing_reference_elapsed();
#else
						controller->maneuver_timer -=
							g_elapsed_ticks;
#endif

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

#ifdef XVT_MODERN
						controller
							->secondary_maneuver_timer -=
							xvt_flight_timing_reference_elapsed();
#else
						controller
							->secondary_maneuver_timer -=
							g_elapsed_ticks;
#endif

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

#ifdef XVT_MODERN
		max_bounds_extent = 0;
#endif
		g_target_proximity_blink_bit ^= 0x0400;
		if ((uint16_t)g_players[g_local_player]
				    .current_target_object_idx != UINT16_MAX &&
		    g_players[g_local_player].object_index != -1) {
			int object_type;

			pai_object_ref_direction_to_object_ref(
				(uint16_t)g_players[g_local_player]
					.current_target_object_idx,
				g_players[g_local_player].object_index);
			object_type =
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
		}
	}

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
			int active_flag = 1;
			int active_team_count;
			int active_team;

			memset(team_active, 0, sizeof(team_active));

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
			active_team_count = 0;
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
		}
	}

	for (player_index = 0; player_index < 8; ++player_index) {
		struct player_data *player = &g_players[player_index];

		if (player->participation_state != 0 &&
		    player->object_index != -1) {
			uint16_t repair_display_slot = UINT16_MAX;
			uint16_t repair_system = UINT16_MAX;
			struct craft_data *craft =
				g_object_table[player->object_index]
					.mobj->p_craft;

			if (craft->working_subsystems != 0) {
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
						} else {
							--craft->system_repair_seconds
								  [index];
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
	uint8_t track_number;
	uint16_t player_team;
	uint8_t primary_goal_status;
	uint32_t current_time_ms;
	uint32_t elapsed_ms;

	if (g_game_config.music_enabled == 0 ||
	    g_game_config.music_volume == 0 ||
	    g_pilot_data.num_human_players_last_mission != 1) {
		return;
	}

	track_number = 0;
	if (g_dynamic_music_state == 2 &&
	    g_dynamic_music_outcome_latched == 0) {
		player_team = g_players[g_local_player].team;
		primary_goal_status = g_flight_mission_state.runtime
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
		}
	}

	if (track_number != 0) {
		return;
	}

	current_time_ms = timeGetTime();
	elapsed_ms = current_time_ms - g_dynamic_music_last_update_ms;
	g_dynamic_music_last_update_ms = current_time_ms;
	g_dynamic_music_track_remaining_ms -= elapsed_ms;
	if (g_dynamic_music_track_remaining_ms <= 0) {
		music_cd_play_track_from_time(2, 0, 0);
		g_dynamic_music_track_remaining_ms =
			music_cd_get_track_length_ms(2);
		g_dynamic_music_state = 2;
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
	size_t buffer_size;

	buffer_size = flight_calculate_world_state_buffer_size();
	g_world_state_handle = memory_alloc_handle(buffer_size, 0);
	if (g_world_state_handle == 0) {
		fe_disk_io_fatal_error(FILE_ERROR_STR_NOT_ENOUGH_MEMORY);
#ifdef XVT_MODERN
		return;
#endif
	}
	g_world_state_buffer = memory_get_handle_block(g_world_state_handle);

	g_world_state_dup_handle = memory_alloc_handle(buffer_size, 0);
	if (g_world_state_dup_handle == 0) {
		fe_disk_io_fatal_error(FILE_ERROR_STR_NOT_ENOUGH_MEMORY);
#ifdef XVT_MODERN
		return;
#endif
	}
	g_world_state_dup_buffer =
		memory_get_handle_block(g_world_state_dup_handle);
}

/* Frees both world state buffers (the modern build skips a handle of 0) and
 * sets g_world_state_handle, g_world_state_buffer, g_world_state_dup_handle and
 * g_world_state_dup_buffer to 0 or NULL. */
// FUNCTION: XVT 0x4167A0
void flight_free_world_state_buffers(void)
{
	unsigned int handle;

	handle = g_world_state_handle;
#ifdef XVT_MODERN
	if (handle) {
		memory_free_handle((uint16_t)handle);
	}
#else
	memory_free_handle((uint16_t)handle);
#endif
	g_world_state_handle = 0;
	g_world_state_buffer = NULL;
	handle = g_world_state_dup_handle;
#ifdef XVT_MODERN
	if (handle) {
		memory_free_handle((uint16_t)handle);
	}
#else
	memory_free_handle((uint16_t)handle);
#endif
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
#ifdef XVT_MODERN
	xvt_snapshot_save();
#else
	uint8_t *cursor;
	int object_index;

	cursor = g_world_state_buffer;
	for (object_index = 0;
	     object_index <
	     g_region_main_object_slot_end + g_region_static_object_slot_count;
	     ++object_index) {
		if (object_index >= g_local_transient_slot_start &&
		    object_index < g_local_debris_slot_end) {
			continue;
		}
		*cursor++ = g_object_table[object_index].object_type;
		if (g_object_table[object_index].object_type == 0) {
			continue;
		}

		if (g_object_table[object_index].mobj != NULL) {
			g_object_table[object_index].mobj =
				(struct mobile_object
					 *)((uint8_t *)
						    g_object_table[object_index]
							    .mobj -
					    ((uint8_t *)
						     g_mobile_object_pool_base -
					     (uint8_t *)NULL));
			g_object_table[object_index].mobj =
				(struct mobile_object
					 *)((uint8_t *)
						    g_object_table[object_index]
							    .mobj +
					    1);
		}
		memcpy(cursor, &g_object_table[object_index],
		       sizeof(struct object_record));
		cursor += sizeof(struct object_record);
		if (g_object_table[object_index].mobj != NULL) {
			g_object_table[object_index].mobj =
				(struct mobile_object
					 *)((uint8_t *)
						    g_object_table[object_index]
							    .mobj -
					    1);
			g_object_table[object_index].mobj =
				(struct mobile_object
					 *)((uint8_t *)
						    g_object_table[object_index]
							    .mobj +
					    ((uint8_t *)
						     g_mobile_object_pool_base -
					     (uint8_t *)NULL));
		}
		if (g_object_table[object_index].mobj == NULL) {
			continue;
		}

		if (g_object_table[object_index].mobj->p_craft != NULL) {
			g_object_table[object_index].mobj->p_craft =
				(struct craft_data
					 *)((uint8_t *)
						    g_object_table[object_index]
							    .mobj->p_craft -
					    ((uint8_t *)g_craft_data_pool_base -
					     (uint8_t *)NULL));
			g_object_table[object_index].mobj->p_craft =
				(struct craft_data
					 *)((uint8_t *)
						    g_object_table[object_index]
							    .mobj->p_craft +
					    1);
		}
		if (g_object_table[object_index].mobj->p_warhead_guidance !=
		    NULL) {
			g_object_table[object_index].mobj->p_warhead_guidance =
				(struct warhead_guidance_state
					 *)((uint8_t *)
						    g_object_table[object_index]
							    .mobj
							    ->p_warhead_guidance -
					    ((uint8_t *)
						     g_projectile_guidance_states -
					     (uint8_t *)NULL));
			g_object_table[object_index].mobj->p_warhead_guidance =
				(struct warhead_guidance_state
					 *)((uint8_t *)
						    g_object_table[object_index]
							    .mobj
							    ->p_warhead_guidance +
					    1);
		}
		if (g_object_table[object_index].mobj->p_char_data != NULL) {
			g_object_table[object_index].mobj->p_char_data =
				(struct mobile_object_char_data
					 *)((uint8_t *)
						    g_object_table[object_index]
							    .mobj->p_char_data -
					    ((uint8_t *)
						     g_mobile_object_char_data_pool -
					     (uint8_t *)NULL));
			g_object_table[object_index].mobj->p_char_data =
				(struct mobile_object_char_data
					 *)((uint8_t *)
						    g_object_table[object_index]
							    .mobj->p_char_data +
					    1);
		}
		memcpy(cursor, g_object_table[object_index].mobj,
		       sizeof(struct mobile_object));
		cursor += sizeof(struct mobile_object);
		if (g_object_table[object_index].mobj->p_craft != NULL) {
			g_object_table[object_index].mobj->p_craft =
				(struct craft_data
					 *)((uint8_t *)
						    g_object_table[object_index]
							    .mobj->p_craft -
					    1);
			g_object_table[object_index].mobj->p_craft =
				(struct craft_data
					 *)((uint8_t *)
						    g_object_table[object_index]
							    .mobj->p_craft +
					    ((uint8_t *)g_craft_data_pool_base -
					     (uint8_t *)NULL));
		}
		if (g_object_table[object_index].mobj->p_warhead_guidance !=
		    NULL) {
			g_object_table[object_index].mobj->p_warhead_guidance =
				(struct warhead_guidance_state
					 *)((uint8_t *)
						    g_object_table[object_index]
							    .mobj
							    ->p_warhead_guidance -
					    1);
			g_object_table[object_index].mobj->p_warhead_guidance =
				(struct warhead_guidance_state
					 *)((uint8_t *)
						    g_object_table[object_index]
							    .mobj
							    ->p_warhead_guidance +
					    ((uint8_t *)
						     g_projectile_guidance_states -
					     (uint8_t *)NULL));
		}
		if (g_object_table[object_index].mobj->p_char_data != NULL) {
			g_object_table[object_index].mobj->p_char_data =
				(struct mobile_object_char_data
					 *)((uint8_t *)
						    g_object_table[object_index]
							    .mobj->p_char_data -
					    1);
			g_object_table[object_index].mobj->p_char_data =
				(struct mobile_object_char_data
					 *)((uint8_t *)
						    g_object_table[object_index]
							    .mobj->p_char_data +
					    ((uint8_t *)
						     g_mobile_object_char_data_pool -
					     (uint8_t *)NULL));
		}

		if (g_object_table[object_index].mobj->p_craft != NULL) {
			int link_index;

			for (link_index = 0; link_index < 16; ++link_index) {
				if (g_object_table[object_index]
					    .mobj->p_craft
					    ->turret_object_links[link_index] !=
				    NULL) {
					g_object_table[object_index]
						.mobj->p_craft
						->turret_object_links
							[link_index] =
						(struct object_record
							 *)((uint8_t *)g_object_table[object_index]
								    .mobj
								    ->p_craft
								    ->turret_object_links
									    [link_index] -
							    ((uint8_t *)
								     g_object_table -
							     (uint8_t *)NULL));
					g_object_table[object_index]
						.mobj->p_craft
						->turret_object_links
							[link_index] =
						(struct object_record
							 *)((uint8_t *)g_object_table[object_index]
								    .mobj
								    ->p_craft
								    ->turret_object_links
									    [link_index] +
							    1);
				}
			}
			if (g_object_table[object_index]
				    .mobj->p_craft->effective_ai_object_link !=
			    NULL) {
				g_object_table[object_index]
					.mobj->p_craft
					->effective_ai_object_link =
					(struct object_record
						 *)((uint8_t *)g_object_table
							    [object_index]
								    .mobj
								    ->p_craft
								    ->effective_ai_object_link -
						    ((uint8_t *)g_object_table -
						     (uint8_t *)NULL));
				g_object_table[object_index]
					.mobj->p_craft
					->effective_ai_object_link =
					(struct object_record
						 *)((uint8_t *)g_object_table
							    [object_index]
								    .mobj
								    ->p_craft
								    ->effective_ai_object_link +
						    1);
			}
			memcpy(cursor,
			       g_object_table[object_index].mobj->p_craft,
			       sizeof(struct craft_data));
			cursor += sizeof(struct craft_data);
			for (link_index = 0; link_index < 16; ++link_index) {
				if (g_object_table[object_index]
					    .mobj->p_craft
					    ->turret_object_links[link_index] !=
				    NULL) {
					g_object_table[object_index]
						.mobj->p_craft
						->turret_object_links
							[link_index] =
						(struct object_record
							 *)((uint8_t *)g_object_table[object_index]
								    .mobj
								    ->p_craft
								    ->turret_object_links
									    [link_index] -
							    1);
					g_object_table[object_index]
						.mobj->p_craft
						->turret_object_links
							[link_index] =
						(struct object_record
							 *)((uint8_t *)g_object_table[object_index]
								    .mobj
								    ->p_craft
								    ->turret_object_links
									    [link_index] +
							    ((uint8_t *)
								     g_object_table -
							     (uint8_t *)NULL));
				}
			}
			if (g_object_table[object_index]
				    .mobj->p_craft->effective_ai_object_link !=
			    NULL) {
				g_object_table[object_index]
					.mobj->p_craft
					->effective_ai_object_link =
					(struct object_record
						 *)((uint8_t *)g_object_table
							    [object_index]
								    .mobj
								    ->p_craft
								    ->effective_ai_object_link -
						    1);
				g_object_table[object_index]
					.mobj->p_craft
					->effective_ai_object_link =
					(struct object_record
						 *)((uint8_t *)g_object_table
							    [object_index]
								    .mobj
								    ->p_craft
								    ->effective_ai_object_link +
						    ((uint8_t *)g_object_table -
						     (uint8_t *)NULL));
			}
		}
		if (g_object_table[object_index].mobj->p_warhead_guidance !=
		    NULL) {
			memcpy(cursor,
			       g_object_table[object_index]
				       .mobj->p_warhead_guidance,
			       sizeof(struct warhead_guidance_state));
			cursor += sizeof(struct warhead_guidance_state);
		}
		if (g_object_table[object_index].mobj->p_char_data != NULL) {
			memcpy(cursor,
			       g_object_table[object_index].mobj->p_char_data,
			       sizeof(struct mobile_object_char_data));
			cursor += sizeof(struct mobile_object_char_data);
		}
	}

	memcpy(cursor, &g_mission_elapsed_clock,
	       sizeof(g_mission_elapsed_clock));
	cursor += sizeof(g_mission_elapsed_clock);
	memcpy(cursor, &g_mission_countdown_clock,
	       sizeof(g_mission_countdown_clock));
	cursor += sizeof(g_mission_countdown_clock);
	memcpy(cursor, &g_mission_header, 162);
	cursor += 162;
	memcpy(cursor, g_mission_fg_stats,
	       294 * (int16_t)g_mission_header.num_flight_groups);
	cursor += 294 * (int16_t)g_mission_header.num_flight_groups;
	memcpy(cursor, g_mission_flight_groups,
	       1382 * (int16_t)g_mission_header.num_flight_groups);
	cursor += 1382 * (int16_t)g_mission_header.num_flight_groups;
	memcpy(cursor, &g_flight_mission_state, 3376);
	cursor += 3376;
	memcpy(cursor, &g_flight_global_countdown_timers, 22);
	cursor += 22;
	memcpy(cursor, &g_mission_file_version, sizeof(g_mission_file_version));
	cursor += sizeof(g_mission_file_version);
	memcpy(cursor, &g_flight_player_count, sizeof(g_flight_player_count));
	cursor += sizeof(g_flight_player_count);
	*cursor++ = g_world_state_reserved_byte;

	memcpy(cursor, &g_craft_data_pool_capacity,
	       sizeof(g_craft_data_pool_capacity));
	cursor += sizeof(g_craft_data_pool_capacity);
	memcpy(cursor, &g_mobile_object_char_data_count,
	       sizeof(g_mobile_object_char_data_count));
	cursor += sizeof(g_mobile_object_char_data_count);
	memcpy(cursor, &g_projectile_object_slots_total,
	       sizeof(g_projectile_object_slots_total));
	cursor += sizeof(g_projectile_object_slots_total);
	memcpy(cursor, &g_debris_object_slots_total,
	       sizeof(g_debris_object_slots_total));
	cursor += sizeof(g_debris_object_slots_total);
	memcpy(cursor, &g_world_state_debris_slot_count,
	       sizeof(g_world_state_debris_slot_count));
	cursor += sizeof(g_world_state_debris_slot_count);
	memcpy(cursor, &g_local_debris_slot_count,
	       sizeof(g_local_debris_slot_count));
	cursor += sizeof(g_local_debris_slot_count);
	memcpy(cursor, &g_active_region_object_slot_start,
	       sizeof(g_active_region_object_slot_start));
	cursor += sizeof(g_active_region_object_slot_start);
	memcpy(cursor, &g_active_region_craft_object_slot_end,
	       sizeof(g_active_region_craft_object_slot_end));
	cursor += sizeof(g_active_region_craft_object_slot_end);
	memcpy(cursor, &g_mobile_object_char_data_slot_start,
	       sizeof(g_mobile_object_char_data_slot_start));
	cursor += sizeof(g_mobile_object_char_data_slot_start);
	memcpy(cursor, &g_mobile_object_char_data_slot_end,
	       sizeof(g_mobile_object_char_data_slot_end));
	cursor += sizeof(g_mobile_object_char_data_slot_end);
	memcpy(cursor, &g_projectile_object_slot_start,
	       sizeof(g_projectile_object_slot_start));
	cursor += sizeof(g_projectile_object_slot_start);
	memcpy(cursor, &g_projectile_object_slot_end,
	       sizeof(g_projectile_object_slot_end));
	cursor += sizeof(g_projectile_object_slot_end);
	memcpy(cursor, &g_debris_object_slot_start,
	       sizeof(g_debris_object_slot_start));
	cursor += sizeof(g_debris_object_slot_start);
	memcpy(cursor, &g_debris_object_slot_end,
	       sizeof(g_debris_object_slot_end));
	cursor += sizeof(g_debris_object_slot_end);
	memcpy(cursor, &g_explosion_object_slot_start,
	       sizeof(g_explosion_object_slot_start));
	cursor += sizeof(g_explosion_object_slot_start);
	memcpy(cursor, &g_explosion_object_slot_end,
	       sizeof(g_explosion_object_slot_end));
	cursor += sizeof(g_explosion_object_slot_end);
	memcpy(cursor, &g_local_transient_slot_start,
	       sizeof(g_local_transient_slot_start));
	cursor += sizeof(g_local_transient_slot_start);
	memcpy(cursor, &g_local_debris_slot_end,
	       sizeof(g_local_debris_slot_end));
	cursor += sizeof(g_local_debris_slot_end);
	memcpy(cursor, &g_region_main_object_slot_end,
	       sizeof(g_region_main_object_slot_end));
	cursor += sizeof(g_region_main_object_slot_end);
	memcpy(cursor, &g_region_static_object_slot_count,
	       sizeof(g_region_static_object_slot_count));
	cursor += sizeof(g_region_static_object_slot_count);
	memcpy(cursor, g_plan_table, 21760);
	cursor += 21760;
	memcpy(cursor, &g_plan_count, sizeof(g_plan_count));
	cursor += sizeof(g_plan_count);
	memcpy(cursor, &g_unused_world_state_serialized_dword,
	       sizeof(g_unused_world_state_serialized_dword));
	cursor += sizeof(g_unused_world_state_serialized_dword);
	memcpy(cursor, g_builtin_plan_id_by_name_index, 256);
	cursor += 256;
	memcpy(cursor, &g_game_rand_feedback_state,
	       sizeof(g_game_rand_feedback_state));
	cursor += sizeof(g_game_rand_feedback_state);
	memcpy(cursor, &g_next_object_signature,
	       sizeof(g_next_object_signature));
	cursor += sizeof(g_next_object_signature);
	memcpy(cursor, &g_laser_fire_timestamp_tracking_enabled,
	       sizeof(g_laser_fire_timestamp_tracking_enabled));
	cursor += sizeof(g_laser_fire_timestamp_tracking_enabled);
	memcpy(cursor, g_players, 11752);
	cursor += 11752;
	g_world_state_size = (unsigned int)(cursor - g_world_state_buffer);
#endif
}

/* Reads the world state back from g_world_state_buffer in the order
 * flight_save_world_state writes it, turning stored offsets back into pointers; a
 * slot saved empty has its record, mobile object, craft data, guidance and
 * character data cleared, with player_owner_idx -1 and the mobile object's IFF
 * 0xFF. The modern build calls xvt_snapshot_restore instead. */
// FUNCTION: XVT 0x416E50
void flight_restore_world_state(void)
{
#ifdef XVT_MODERN
	xvt_snapshot_restore();
#else
	uint8_t *cursor;
	int object_index;

	cursor = g_world_state_buffer;
	for (object_index = 0;
	     object_index <
	     g_region_main_object_slot_end + g_region_static_object_slot_count;
	     ++object_index) {
		if (object_index >= g_local_transient_slot_start &&
		    object_index < g_local_debris_slot_end) {
			continue;
		}
		g_object_table[object_index].object_type = *cursor++;
		if (g_object_table[object_index].object_type != 0) {
			memcpy(&g_object_table[object_index], cursor,
			       sizeof(struct object_record));
			cursor += sizeof(struct object_record);
			if (g_object_table[object_index].mobj != NULL) {
				g_object_table[object_index].mobj =
					(struct mobile_object
						 *)((uint8_t *)g_object_table
							    [object_index]
								    .mobj -
						    1);
				g_object_table[object_index].mobj =
					(struct mobile_object
						 *)((uint8_t *)g_object_table
							    [object_index]
								    .mobj +
						    ((uint8_t *)
							     g_mobile_object_pool_base -
						     (uint8_t *)NULL));
			}
			if (g_object_table[object_index].mobj == NULL) {
				continue;
			}

			memcpy(g_object_table[object_index].mobj, cursor,
			       sizeof(struct mobile_object));
			cursor += sizeof(struct mobile_object);
			if (g_object_table[object_index].mobj->p_craft !=
			    NULL) {
				g_object_table[object_index].mobj->p_craft =
					(struct craft_data
						 *)((uint8_t *)g_object_table
							    [object_index]
								    .mobj
								    ->p_craft -
						    1);
				g_object_table[object_index].mobj->p_craft =
					(struct craft_data
						 *)((uint8_t *)g_object_table
							    [object_index]
								    .mobj
								    ->p_craft +
						    ((uint8_t *)
							     g_craft_data_pool_base -
						     (uint8_t *)NULL));
			}
			if (g_object_table[object_index]
				    .mobj->p_warhead_guidance != NULL) {
				g_object_table[object_index]
					.mobj->p_warhead_guidance =
					(struct warhead_guidance_state
						 *)((uint8_t *)g_object_table
							    [object_index]
								    .mobj
								    ->p_warhead_guidance -
						    1);
				g_object_table[object_index]
					.mobj->p_warhead_guidance =
					(struct warhead_guidance_state
						 *)((uint8_t *)g_object_table
							    [object_index]
								    .mobj
								    ->p_warhead_guidance +
						    ((uint8_t *)
							     g_projectile_guidance_states -
						     (uint8_t *)NULL));
			}
			if (g_object_table[object_index].mobj->p_char_data !=
			    NULL) {
				g_object_table[object_index].mobj->p_char_data =
					(struct mobile_object_char_data
						 *)((uint8_t *)g_object_table
							    [object_index]
								    .mobj
								    ->p_char_data -
						    1);
				g_object_table[object_index].mobj->p_char_data =
					(struct mobile_object_char_data
						 *)((uint8_t *)g_object_table
							    [object_index]
								    .mobj
								    ->p_char_data +
						    ((uint8_t *)
							     g_mobile_object_char_data_pool -
						     (uint8_t *)NULL));
			}

			if (g_object_table[object_index].mobj->p_craft !=
			    NULL) {
				int link_index;

				memcpy(g_object_table[object_index]
					       .mobj->p_craft,
				       cursor, sizeof(struct craft_data));
				cursor += sizeof(struct craft_data);
				for (link_index = 0; link_index < 16;
				     ++link_index) {
					if (g_object_table[object_index]
						    .mobj->p_craft
						    ->turret_object_links
							    [link_index] !=
					    NULL) {
						g_object_table[object_index]
							.mobj->p_craft
							->turret_object_links
								[link_index] =
							(struct object_record
								 *)((uint8_t *)g_object_table[object_index]
									    .mobj
									    ->p_craft
									    ->turret_object_links
										    [link_index] -
								    1);
						g_object_table[object_index]
							.mobj->p_craft
							->turret_object_links
								[link_index] =
							(struct object_record
								 *)((uint8_t *)g_object_table[object_index]
									    .mobj
									    ->p_craft
									    ->turret_object_links
										    [link_index] +
								    ((uint8_t *)
									     g_object_table -
								     (uint8_t *)
									     NULL));
					}
				}
				if (g_object_table[object_index]
					    .mobj->p_craft
					    ->effective_ai_object_link !=
				    NULL) {
					g_object_table[object_index]
						.mobj->p_craft
						->effective_ai_object_link =
						(struct object_record
							 *)((uint8_t *)g_object_table
								    [object_index]
									    .mobj
									    ->p_craft
									    ->effective_ai_object_link -
							    1);
					g_object_table[object_index]
						.mobj->p_craft
						->effective_ai_object_link =
						(struct object_record
							 *)((uint8_t *)g_object_table
								    [object_index]
									    .mobj
									    ->p_craft
									    ->effective_ai_object_link +
							    ((uint8_t *)
								     g_object_table -
							     (uint8_t *)NULL));
				}
			}
			if (g_object_table[object_index]
				    .mobj->p_warhead_guidance != NULL) {
				memcpy(g_object_table[object_index]
					       .mobj->p_warhead_guidance,
				       cursor,
				       sizeof(struct warhead_guidance_state));
				cursor += sizeof(struct warhead_guidance_state);
			}
			if (g_object_table[object_index].mobj->p_char_data !=
			    NULL) {
				memcpy(g_object_table[object_index]
					       .mobj->p_char_data,
				       cursor,
				       sizeof(struct mobile_object_char_data));
				cursor +=
					sizeof(struct mobile_object_char_data);
			}
		} else {
			memset(&g_object_table[object_index], 0, 0x1f);
			g_object_table[object_index].player_owner_idx = -1;
			if (g_object_table[object_index].mobj != NULL) {
				memset(g_object_table[object_index].mobj, 0,
				       0x8b);
				g_object_table[object_index].mobj->iff = 0xff;
				if (g_object_table[object_index]
					    .mobj->p_craft != NULL) {
					memset(g_object_table[object_index]
						       .mobj->p_craft,
					       0, 0x412);
				}
				if (g_object_table[object_index]
					    .mobj->p_warhead_guidance != NULL) {
					memset(g_object_table[object_index]
						       .mobj
						       ->p_warhead_guidance,
					       0,
					       sizeof(struct
						      warhead_guidance_state));
				}
				if (g_object_table[object_index]
					    .mobj->p_char_data != NULL) {
					memset(g_object_table[object_index]
						       .mobj->p_char_data,
					       0,
					       sizeof(struct
						      mobile_object_char_data));
				}
			}
		}
	}

	memcpy(&g_mission_elapsed_clock, cursor,
	       sizeof(g_mission_elapsed_clock));
	cursor += sizeof(g_mission_elapsed_clock);
	memcpy(&g_mission_countdown_clock, cursor,
	       sizeof(g_mission_countdown_clock));
	cursor += sizeof(g_mission_countdown_clock);
	memcpy(&g_mission_header, cursor, sizeof(g_mission_header));
	cursor += sizeof(g_mission_header);
	memcpy(g_mission_fg_stats, cursor,
	       sizeof(*g_mission_fg_stats) *
		       g_mission_header.num_flight_groups);
	cursor += sizeof(*g_mission_fg_stats) *
		  g_mission_header.num_flight_groups;
	memcpy(g_mission_flight_groups, cursor,
	       sizeof(*g_mission_flight_groups) *
		       g_mission_header.num_flight_groups);
	cursor += sizeof(*g_mission_flight_groups) *
		  g_mission_header.num_flight_groups;
	memcpy(&g_flight_mission_state, cursor, sizeof(g_flight_mission_state));
	cursor += sizeof(g_flight_mission_state);
	memcpy(&g_flight_global_countdown_timers, cursor,
	       sizeof(g_flight_global_countdown_timers));
	cursor += sizeof(g_flight_global_countdown_timers);
	g_mission_file_version = *(uint16_t *)cursor;
	cursor += sizeof(g_mission_file_version);
	g_flight_player_count = *(int32_t *)cursor;
	cursor += sizeof(g_flight_player_count);
	g_world_state_reserved_byte = *cursor++;
	g_craft_data_pool_capacity = *(int *)cursor;
	cursor += sizeof(g_craft_data_pool_capacity);
	g_mobile_object_char_data_count = *(int *)cursor;
	cursor += sizeof(g_mobile_object_char_data_count);
	g_projectile_object_slots_total = *(unsigned int *)cursor;
	cursor += sizeof(g_projectile_object_slots_total);
	g_debris_object_slots_total = *(unsigned int *)cursor;
	cursor += sizeof(g_debris_object_slots_total);
	g_world_state_debris_slot_count = *(int *)cursor;
	cursor += sizeof(g_world_state_debris_slot_count);
	g_local_debris_slot_count = *(int *)cursor;
	cursor += sizeof(g_local_debris_slot_count);
	g_active_region_object_slot_start = *(int *)cursor;
	cursor += sizeof(g_active_region_object_slot_start);
	g_active_region_craft_object_slot_end = *(int *)cursor;
	cursor += sizeof(g_active_region_craft_object_slot_end);
	g_mobile_object_char_data_slot_start = *(int *)cursor;
	cursor += sizeof(g_mobile_object_char_data_slot_start);
	g_mobile_object_char_data_slot_end = *(int *)cursor;
	cursor += sizeof(g_mobile_object_char_data_slot_end);
	g_projectile_object_slot_start = *(int *)cursor;
	cursor += sizeof(g_projectile_object_slot_start);
	g_projectile_object_slot_end = *(int *)cursor;
	cursor += sizeof(g_projectile_object_slot_end);
	g_debris_object_slot_start = *(int *)cursor;
	cursor += sizeof(g_debris_object_slot_start);
	g_debris_object_slot_end = *(int *)cursor;
	cursor += sizeof(g_debris_object_slot_end);
	g_explosion_object_slot_start = *(int *)cursor;
	cursor += sizeof(g_explosion_object_slot_start);
	g_explosion_object_slot_end = *(unsigned int *)cursor;
	cursor += sizeof(g_explosion_object_slot_end);
	g_local_transient_slot_start = *(int *)cursor;
	cursor += sizeof(g_local_transient_slot_start);
	g_local_debris_slot_end = *(int *)cursor;
	cursor += sizeof(g_local_debris_slot_end);
	g_region_main_object_slot_end = *(int *)cursor;
	cursor += sizeof(g_region_main_object_slot_end);
	g_region_static_object_slot_count = *(int *)cursor;
	cursor += sizeof(g_region_static_object_slot_count);
	memcpy(g_plan_table, cursor, sizeof(g_plan_table));
	cursor += sizeof(g_plan_table);
	g_plan_count = *(int *)cursor;
	cursor += sizeof(g_plan_count);
	g_unused_world_state_serialized_dword = *(int *)cursor;
	cursor += sizeof(g_unused_world_state_serialized_dword);
	memcpy(g_builtin_plan_id_by_name_index, cursor,
	       sizeof(g_builtin_plan_id_by_name_index));
	cursor += sizeof(g_builtin_plan_id_by_name_index);
	g_game_rand_feedback_state = *(int16_t *)cursor;
	cursor += sizeof(g_game_rand_feedback_state);
	g_next_object_signature = *(uint16_t *)cursor;
	cursor += sizeof(g_next_object_signature);
	g_laser_fire_timestamp_tracking_enabled = *(int *)cursor;
	cursor += sizeof(g_laser_fire_timestamp_tracking_enabled);
	memcpy(g_players, cursor, sizeof(g_players));
#endif
}

/* Returns the bytes flight_save_world_state may write: the fixed part plus the
 * size of each flight group's stats and record, each static slot, each main
 * slot with its mobile object, each character data entry, each projectile
 * guidance and each craft data entry. The modern build returns
 * xvt_snapshot_calculate_size. */
// FUNCTION: XVT 0x4173D0
size_t flight_calculate_world_state_buffer_size(void)
{
#ifdef XVT_MODERN
	return xvt_snapshot_calculate_size();
#else
	int size;

	/* The fixed trailer contains 24 dwords, three words, and one byte around the fixed arrays. */
	size = (int)(2 * sizeof(struct mission_clock) +
		     sizeof(struct mission_header) +
		     sizeof(struct flight_mission_state) +
		     sizeof(struct flight_global_countdown_timers) +
		     sizeof(g_plan_table) +
		     sizeof(g_builtin_plan_id_by_name_index) +
		     sizeof(g_players) + 24 * sizeof(uint32_t) +
		     3 * sizeof(uint16_t) + sizeof(uint8_t) +
		     sizeof(struct warhead_guidance_state));
	size += (int)(sizeof(struct mission_fg_runtime_stats) +
		      sizeof(struct mission_flight_group)) *
		g_mission_header.num_flight_groups;
	size += (int)(sizeof(uint8_t) + sizeof(struct object_record)) *
		g_region_static_object_slot_count;
	size += (int)(sizeof(uint8_t) + sizeof(struct object_record) +
		      sizeof(struct mobile_object)) *
		g_region_main_object_slot_end;
	size += (int)sizeof(struct mobile_object_char_data) *
		(int)g_mobile_object_char_data_count;
	size += (int)sizeof(struct warhead_guidance_state) *
		(int)g_projectile_object_slots_total;
	size += (int)sizeof(struct craft_data) * g_craft_data_pool_capacity;
	return (size_t)size;
#endif
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
#ifdef XVT_MODERN
	xvt_snapshot_checksum(unused_arg0, unused_arg1);
#else
	uint8_t *cursor;
	uint8_t *region_start;
	unsigned int checksum;
	int region_target_size;
	int checksum_region_index;
	int object_index;
	int object_count;
	int bytes_remaining;
	int flight_group_count;

	(void)unused_arg0;
	(void)unused_arg1;

	region_target_size = (int)g_world_state_size >> 4;
	cursor = g_world_state_buffer;
	region_start = g_world_state_buffer;
	checksum = 0;
	checksum_region_index = 0;
	object_index = 0;
	object_count = g_region_main_object_slot_end +
		       g_region_static_object_slot_count;
	if (object_count > 0) {
		do {
			if (g_local_transient_slot_start > object_index ||
			    g_local_debris_slot_end <= object_index) {
				uint8_t object_present;

				object_present = *cursor++;
				if (object_present != 0) {
					struct object_record *object_state;
					int object_data_bytes;

					object_state =
						(struct object_record *)cursor;
					object_data_bytes =
						sizeof(*object_state) -
						sizeof(object_state->mobj);
					do {
						checksum += *cursor++;
					} while (--object_data_bytes != 0);
					cursor = (uint8_t *)(object_state + 1);
					if (object_state->mobj != NULL) {
						struct mobile_object
							*mobile_state;
						int mobile_data_bytes;

						mobile_state =
							(struct mobile_object *)
								cursor;
						mobile_data_bytes =
							sizeof(*mobile_state) -
							sizeof(mobile_state
								       ->move_vector_dirty) -
							sizeof(mobile_state
								       ->move_x) -
							sizeof(mobile_state
								       ->move_y) -
							sizeof(mobile_state
								       ->move_z) -
							sizeof(mobile_state
								       ->orient_matrix_dirty) -
							sizeof(mobile_state
								       ->cached_fwd_x) -
							sizeof(mobile_state
								       ->cached_fwd_y) -
							sizeof(mobile_state
								       ->cached_fwd_z) -
							sizeof(mobile_state
								       ->cached_side_x) -
							sizeof(mobile_state
								       ->cached_side_y) -
							sizeof(mobile_state
								       ->cached_side_z) -
							sizeof(mobile_state
								       ->cached_up_x) -
							sizeof(mobile_state
								       ->cached_up_y) -
							sizeof(mobile_state
								       ->cached_up_z) -
							sizeof(mobile_state
								       ->p_warhead_guidance) -
							sizeof(mobile_state
								       ->p_craft) -
							sizeof(mobile_state
								       ->p_char_data);
						do {
							checksum += *cursor++;
						} while (--mobile_data_bytes !=
							 0);
						cursor =
							(uint8_t *)(mobile_state +
								    1);
						if (mobile_state->p_craft !=
						    NULL) {
							struct craft_data
								*craft_state;
							int craft_data_bytes;

							craft_state =
								(struct
								 craft_data *)
									cursor;
							craft_data_bytes =
								sizeof(*craft_state) -
								sizeof(craft_state
									       ->unused3f2) -
								sizeof(craft_state
									       ->turret_object_links) -
								sizeof(craft_state
									       ->effective_ai_object_link) +
								32;
							do {
								checksum +=
									*cursor++;
							} while (
								--craft_data_bytes !=
								0);
							cursor =
								(uint8_t *)(craft_state +
									    1);
						}
						if (mobile_state
							    ->p_warhead_guidance !=
						    NULL) {
							bytes_remaining = sizeof(
								struct
								warhead_guidance_state);
							do {
								checksum +=
									*cursor++;
							} while (
								--bytes_remaining !=
								0);
						}
						if (mobile_state->p_char_data !=
						    NULL) {
							bytes_remaining = sizeof(
								struct
								mobile_object_char_data);
							do {
								checksum +=
									*cursor++;
							} while (
								--bytes_remaining !=
								0);
						}
					}
				}
				if (cursor - region_start >
				    region_target_size) {
					g_world_checksum_region_lengths
						[checksum_region_index] =
							(unsigned int)(cursor -
								       region_start);
					g_world_checksum
						[checksum_region_index++] =
							checksum;
					checksum = 0;
					region_start = cursor;
				}
			}
			++object_index;
		} while (object_count > object_index);
	}

	bytes_remaining = 8;
	do {
		checksum += *cursor++;
	} while (--bytes_remaining != 0);
	bytes_remaining = 8;
	do {
		checksum += *cursor++;
	} while (--bytes_remaining != 0);
	bytes_remaining = sizeof(struct mission_header);
	do {
		checksum += *cursor++;
	} while (--bytes_remaining != 0);
	flight_group_count = (int16_t)g_mission_header.num_flight_groups;
	bytes_remaining = 294 * flight_group_count;
	if (bytes_remaining > 0) {
		do {
			checksum += *cursor++;
		} while (--bytes_remaining != 0);
	}
	if (cursor - region_start > region_target_size) {
		g_world_checksum_region_lengths[checksum_region_index] =
			(unsigned int)(cursor - region_start);
		g_world_checksum[checksum_region_index++] = checksum;
		checksum = 0;
		region_start = cursor;
	}

	bytes_remaining = 1382 * flight_group_count;
	if (bytes_remaining > 0) {
		do {
			checksum += *cursor++;
		} while (--bytes_remaining != 0);
	}
	if (cursor - region_start > region_target_size) {
		g_world_checksum_region_lengths[checksum_region_index] =
			(unsigned int)(cursor - region_start);
		g_world_checksum[checksum_region_index++] = checksum;
		checksum = 0;
		region_start = cursor;
	}

	bytes_remaining = 3376;
	do {
		checksum += *cursor++;
	} while (--bytes_remaining != 0);
	if (cursor - region_start > region_target_size) {
		g_world_checksum_region_lengths[checksum_region_index] =
			(unsigned int)(cursor - region_start);
		g_world_checksum[checksum_region_index++] = checksum;
		checksum = 0;
		region_start = cursor;
	}

	bytes_remaining = 22;
	do {
		checksum += *cursor++;
	} while (--bytes_remaining != 0);
	bytes_remaining = 2;
	do {
		checksum += *cursor++;
	} while (--bytes_remaining != 0);
	if (cursor - region_start > region_target_size) {
		g_world_checksum_region_lengths[checksum_region_index] =
			(unsigned int)(cursor - region_start);
		g_world_checksum[checksum_region_index++] = checksum;
		checksum = 0;
		region_start = cursor;
	}

	bytes_remaining = 4;
	do {
		checksum += *cursor++;
	} while (--bytes_remaining != 0);
	checksum += *cursor++;
	bytes_remaining = 4;
	do {
		checksum += *cursor++;
	} while (--bytes_remaining != 0);
	bytes_remaining = 4;
	do {
		checksum += *cursor++;
	} while (--bytes_remaining != 0);
	bytes_remaining = 4;
	do {
		checksum += *cursor++;
	} while (--bytes_remaining != 0);
	bytes_remaining = 4;
	do {
		checksum += *cursor++;
	} while (--bytes_remaining != 0);
	bytes_remaining = 4;
	do {
		checksum += *cursor++;
	} while (--bytes_remaining != 0);
	bytes_remaining = 4;
	do {
		checksum += *cursor++;
	} while (--bytes_remaining != 0);
	bytes_remaining = 4;
	do {
		checksum += *cursor++;
	} while (--bytes_remaining != 0);
	bytes_remaining = 4;
	do {
		checksum += *cursor++;
	} while (--bytes_remaining != 0);
	bytes_remaining = 4;
	do {
		checksum += *cursor++;
	} while (--bytes_remaining != 0);
	bytes_remaining = 4;
	do {
		checksum += *cursor++;
	} while (--bytes_remaining != 0);
	bytes_remaining = 4;
	do {
		checksum += *cursor++;
	} while (--bytes_remaining != 0);
	bytes_remaining = 4;
	do {
		checksum += *cursor++;
	} while (--bytes_remaining != 0);
	bytes_remaining = 4;
	do {
		checksum += *cursor++;
	} while (--bytes_remaining != 0);
	bytes_remaining = 4;
	do {
		checksum += *cursor++;
	} while (--bytes_remaining != 0);
	bytes_remaining = 4;
	do {
		checksum += *cursor++;
	} while (--bytes_remaining != 0);
	bytes_remaining = 4;
	do {
		checksum += *cursor++;
	} while (--bytes_remaining != 0);
	bytes_remaining = 4;
	do {
		checksum += *cursor++;
	} while (--bytes_remaining != 0);
	bytes_remaining = 4;
	do {
		checksum += *cursor++;
	} while (--bytes_remaining != 0);
	bytes_remaining = 4;
	do {
		checksum += *cursor++;
	} while (--bytes_remaining != 0);
	bytes_remaining = 4;
	do {
		checksum += *cursor++;
	} while (--bytes_remaining != 0);
	bytes_remaining = 21760;
	do {
		checksum += *cursor++;
	} while (--bytes_remaining != 0);
	bytes_remaining = 4;
	do {
		checksum += *cursor++;
	} while (--bytes_remaining != 0);
	bytes_remaining = 4;
	do {
		checksum += *cursor++;
	} while (--bytes_remaining != 0);
	bytes_remaining = 256;
	do {
		checksum += *cursor++;
	} while (--bytes_remaining != 0);
	if (cursor - region_start > region_target_size) {
		g_world_checksum_region_lengths[checksum_region_index] =
			(unsigned int)(cursor - region_start);
		g_world_checksum[checksum_region_index++] = checksum;
		checksum = 0;
		region_start = cursor;
	}

	bytes_remaining = 2;
	do {
		checksum += *cursor++;
	} while (--bytes_remaining != 0);
	bytes_remaining = 2;
	do {
		checksum += *cursor++;
	} while (--bytes_remaining != 0);
	bytes_remaining = 4;
	do {
		checksum += *cursor++;
	} while (--bytes_remaining != 0);
	bytes_remaining = 11752;
	do {
		checksum += *cursor++;
	} while (--bytes_remaining != 0);
	if (cursor - region_start > region_target_size) {
		g_world_checksum_region_lengths[checksum_region_index] =
			(unsigned int)(cursor - region_start);
		g_world_checksum[checksum_region_index++] = checksum;
	}
	if (checksum_region_index < 16) {
		memset(&g_world_checksum[checksum_region_index], 0,
		       sizeof(g_world_checksum[0]) *
			       (16 - checksum_region_index));
		memset(&g_world_checksum_region_lengths[checksum_region_index],
		       0,
		       sizeof(g_world_checksum_region_lengths[0]) *
			       (16 - checksum_region_index));
	}
#endif
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
	int remaining_size;
	int segment_size;
	int segment_count;

	remaining_size = world_state_size;
	segment_size = world_state_size / 124;
	if (segment_size == 0) {
		segment_size = world_state_size;
	}
	segment_count = 125;
	do {
		uint32_t checksum = 0;
		int bytes_in_segment;

		if (segment_size > 0) {
			bytes_in_segment = segment_size;
			do {
				if (remaining_size != 0) {
					checksum += *world_state++;
					--remaining_size;
#ifdef XVT_MODERN
					checksum = (checksum << 1) |
						   (checksum >> 31);
#else
					checksum = _rotl(checksum, 1);
#endif
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
#ifdef XVT_MODERN
	return xvt_snapshot_build_presence_map(out_map, world_state);
#else
	int empty_run_length;
	uint8_t *map_start;
	int object_index;

	map_start = out_map;
	*(int *)out_map = g_region_static_object_slot_count +
			  g_region_main_object_slot_end;
	out_map += sizeof(int);
	empty_run_length = 0;
	object_index = 0;
	while (object_index < g_region_static_object_slot_count +
				      g_region_main_object_slot_end) {
		if (object_index < g_local_transient_slot_start ||
		    object_index >= g_local_debris_slot_end) {
			uint8_t component_flags;

			component_flags = 0;
			if (*world_state++ != 0) {
				const struct object_record *object_state;

				component_flags = FLIGHT_WORLDSTATE_HAS_OBJECT;
				object_state = (const struct object_record *)
					world_state;
				world_state += sizeof(*object_state);
				if (object_state->mobj != NULL) {
					const struct mobile_object
						*mobile_object_state;

					component_flags |=
						FLIGHT_WORLDSTATE_HAS_MOBILE;
					mobile_object_state =
						(const struct mobile_object *)
							world_state;
					world_state +=
						sizeof(*mobile_object_state);
					if (mobile_object_state->p_craft !=
					    NULL) {
						component_flags |=
							FLIGHT_WORLDSTATE_HAS_CRAFT;
						world_state += sizeof(
							struct craft_data);
					}
					if (mobile_object_state
						    ->p_warhead_guidance !=
					    NULL) {
						component_flags |=
							FLIGHT_WORLDSTATE_HAS_WARHEAD_GUIDANCE;
						world_state += sizeof(
							struct
							warhead_guidance_state);
					}
					if (mobile_object_state->p_char_data !=
					    NULL) {
						component_flags |=
							FLIGHT_WORLDSTATE_HAS_CHAR_DATA;
						world_state += sizeof(
							struct
							mobile_object_char_data);
					}
				}
			}

			if (component_flags == 0) {
				++empty_run_length;
				if (empty_run_length >=
				    FLIGHT_WORLDSTATE_MAX_EMPTY_RUN) {
					*out_map++ =
						(uint8_t)(empty_run_length |
							  FLIGHT_WORLDSTATE_EMPTY_RUN_FLAG);
					empty_run_length = 0;
				}
			} else {
				if (empty_run_length != 0) {
					*out_map++ =
						(uint8_t)(empty_run_length |
							  FLIGHT_WORLDSTATE_EMPTY_RUN_FLAG);
					empty_run_length = 0;
				}
				*out_map++ = component_flags;
			}
		}
		++object_index;
	}

	if (empty_run_length != 0) {
		*out_map++ = (uint8_t)(empty_run_length |
				       FLIGHT_WORLDSTATE_EMPTY_RUN_FLAG);
	}
	return (int)(out_map - map_start);
#endif
}

#ifndef XVT_MODERN
#pragma function(memcpy)
#endif
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
#ifdef XVT_MODERN
	xvt_snapshot_apply_presence_map(presence_map);
#else
	uint8_t *cursor;
	uint8_t *end;
	int map_slot_limit;
	int empty_run_remaining;
	int object_index;

	cursor = g_world_state_dup_buffer;
	end = &g_world_state_dup_buffer[g_world_state_dup_size];
	map_slot_limit = *(const int *)presence_map;
	presence_map += sizeof(map_slot_limit);
	empty_run_remaining = 0;
	object_index = 0;
	while (object_index < g_region_static_object_slot_count +
				      g_region_main_object_slot_end) {
		if (g_local_transient_slot_start > object_index ||
		    g_local_debris_slot_end <= object_index) {
			int8_t presence;
			int8_t object_type;

			if (empty_run_remaining != 0) {
				presence = 0;
				--empty_run_remaining;
			} else {
				uint16_t empty_run_length;

				if (map_slot_limit > object_index) {
					presence = (int8_t)*presence_map++;
				} else {
					break;
				}
				if (presence < 0) {
					empty_run_length =
						presence &
						FLIGHT_WORLDSTATE_EMPTY_RUN_LENGTH_MASK;
					presence = 0;
					empty_run_remaining =
						empty_run_length - 1;
				}
			}

			object_type = *cursor++;
			if (object_type != 0) {
				if ((presence & FLIGHT_WORLDSTATE_HAS_OBJECT) !=
				    0) {
					const struct object_record
						*object_state;

					object_state =
						(const struct object_record *)
							cursor;
					cursor += sizeof(*object_state);
					if (object_state->mobj != NULL) {
						if ((presence &
						     FLIGHT_WORLDSTATE_HAS_MOBILE) !=
						    0) {
							const struct mobile_object
								*mobile_state;

							mobile_state =
								(const struct
								 mobile_object
									 *)
									cursor;
							cursor += sizeof(
								*mobile_state);
							if (mobile_state
								    ->p_craft !=
							    NULL) {
								if ((presence &
								     FLIGHT_WORLDSTATE_HAS_CRAFT) !=
								    0) {
									cursor += sizeof(
										struct
										craft_data);
								} else {
									uint8_t *
										block_start;

									block_start =
										cursor;
									cursor += sizeof(
										struct
										craft_data);
									memcpy(block_start,
									       cursor,
									       (size_t)(end -
											cursor));
									cursor =
										block_start;
									end -= sizeof(
										struct
										craft_data);
								}
							} else if (
								(presence &
								 FLIGHT_WORLDSTATE_HAS_CRAFT) !=
								0) {
								uint8_t *
									block_start;

								block_start =
									cursor;
								cursor += sizeof(
									struct
									craft_data);
								memcpy(cursor,
								       block_start,
								       (size_t)(end -
										block_start));
								memset(block_start,
								       0,
								       (size_t)(cursor -
										block_start));
								end += sizeof(
									struct
									craft_data);
							}

							if (mobile_state
								    ->p_warhead_guidance !=
							    NULL) {
								if ((presence &
								     FLIGHT_WORLDSTATE_HAS_WARHEAD_GUIDANCE) !=
								    0) {
									cursor += sizeof(
										struct
										warhead_guidance_state);
								} else {
									uint8_t *
										block_start;

									block_start =
										cursor;
									cursor += sizeof(
										struct
										warhead_guidance_state);
									memcpy(block_start,
									       cursor,
									       (size_t)(end -
											cursor));
									cursor =
										block_start;
									end -= sizeof(
										struct
										warhead_guidance_state);
								}
							} else if (
								(presence &
								 FLIGHT_WORLDSTATE_HAS_WARHEAD_GUIDANCE) !=
								0) {
								uint8_t *
									block_start;

								block_start =
									cursor;
								cursor += sizeof(
									struct
									warhead_guidance_state);
								memcpy(cursor,
								       block_start,
								       (size_t)(end -
										block_start));
								memset(block_start,
								       0,
								       (size_t)(cursor -
										block_start));
								end += sizeof(
									struct
									warhead_guidance_state);
							}

							if (mobile_state
								    ->p_char_data !=
							    NULL) {
								if ((presence &
								     FLIGHT_WORLDSTATE_HAS_CHAR_DATA) !=
								    0) {
									cursor += sizeof(
										struct
										mobile_object_char_data);
								} else {
									uint8_t *
										block_start;

									block_start =
										cursor;
									cursor += sizeof(
										struct
										mobile_object_char_data);
									memcpy(block_start,
									       cursor,
									       (size_t)(end -
											cursor));
									cursor =
										block_start;
									end -= sizeof(
										struct
										mobile_object_char_data);
								}
							} else if (
								(presence &
								 FLIGHT_WORLDSTATE_HAS_CHAR_DATA) !=
								0) {
								uint8_t *
									block_start;

								block_start =
									cursor;
								cursor += sizeof(
									struct
									mobile_object_char_data);
								memcpy(cursor,
								       block_start,
								       (size_t)(end -
										block_start));
								memset(block_start,
								       0,
								       (size_t)(cursor -
										block_start));
								end += sizeof(
									struct
									mobile_object_char_data);
							}
						} else {
							uint8_t *block_start;

							block_start = cursor;
							cursor += sizeof(
								struct
								mobile_object);
							memcpy(block_start,
							       cursor,
							       (size_t)(end -
									cursor));
							end -= sizeof(
								struct
								mobile_object);
							cursor = block_start;
						}
					} else if (
						(presence &
						 FLIGHT_WORLDSTATE_HAS_MOBILE) !=
						0) {
						uint8_t *block_start;

						block_start = cursor;
						cursor += sizeof(
							struct mobile_object);
						memcpy(cursor, block_start,
						       (size_t)(end -
								block_start));
						memset(block_start, 0,
						       (size_t)(cursor -
								block_start));
						end += sizeof(
							struct mobile_object);
					}
				} else {
					uint8_t *block_start;

					block_start = cursor;
					cursor += sizeof(struct object_record);
					memcpy(block_start, cursor,
					       (size_t)(end - cursor));
					end -= sizeof(struct object_record);
					cursor = block_start;
				}
			} else if ((presence & FLIGHT_WORLDSTATE_HAS_OBJECT) !=
				   0) {
				uint8_t *block_start;

				block_start = cursor;
				cursor += sizeof(struct object_record);
				memcpy(cursor, block_start,
				       (size_t)(end - block_start));
				memset(block_start, 0,
				       (size_t)(cursor - block_start));
				end += sizeof(struct object_record);
			}
		}
		++object_index;
	}

	g_world_state_dup_size = (int)(end - g_world_state_dup_buffer);
#endif
}
#ifndef XVT_MODERN
#pragma intrinsic(memcpy)
#endif

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
#ifdef XVT_MODERN
	xvt_flight_sim_step_to_time(target_game_time);
#else
	enum { MINIMUM_SIM_STEP_TICKS = 1 };

	int game_time;

	game_time = g_game_time;
	g_gunner_collision_probe_count = 0;
	for (;;) {
		g_elapsed_ticks = (uint16_t)(target_game_time - game_time);
		if (g_elapsed_ticks < MINIMUM_SIM_STEP_TICKS) {
			break;
		}
		if ((int)(uint16_t)g_elapsed_ticks >
		    g_net_update_interval_ticks) {
			g_elapsed_ticks = (uint16_t)g_net_update_interval_ticks;
		}
		g_sim_steps_per_second =
			(uint16_t)(SIMULATION_TICKS_PER_SECOND /
				   (int)(uint16_t)g_elapsed_ticks);
		/* MINIMUM_SIM_STEP_TICKS is reused below as a rate: at least one step per second. */
		if (g_sim_steps_per_second == 0) {
			g_sim_steps_per_second = MINIMUM_SIM_STEP_TICKS;
		}
		g_game_time = game_time;
		flight_advance_one_step(game_time + (uint16_t)g_elapsed_ticks);
		if (g_flight_sim_side_effects_suppressed == 0 &&
		    g_flight_mission_state.mission_end_pending == 1) {
			return;
		}

		if (g_flight_mission_state.proving_grounds_mode_active == 0) {
			mission_update_flight_group_arrivals();
			pai_update_all_craft_ai();
		}
		flight_update_timers();
		laser_weaponsfire();
		flight_update_craft_steering_and_speed();
		if (g_debris_enabled != 0 &&
		    g_flight_mission_state.proving_grounds_mode_active == 0) {
			flight_object_recycle_local_debris_near_player();
		}
		collide_collisions();
		if (g_flight_sim_side_effects_suppressed == 0 &&
		    g_flight_mission_state.mission_end_pending == 1) {
			return;
		}

		object_update_lifetime_and_movement();
		flight_object_update_special_behavior();
		if (g_flight_sim_side_effects_suppressed == 0 &&
		    g_flight_mission_state.mission_end_pending == 1) {
			return;
		}

		player_validate_all_current_targets();
		player_update_participation_state();
		if (g_flight_sim_side_effects_suppressed == 0 &&
		    g_flight_mission_state.mission_end_pending == 1) {
			return;
		}

		mission_update_logic();
		hud_update_flight_message_panes();
		flight_update_dynamic_music_state();
		if (g_fsfx_loaded != 0) {
			fsfx_update_voice_queue();
			fsfx_update_flight_sfx();
		}
		game_time = g_game_time;
		game_time += (uint16_t)g_elapsed_ticks;
		g_game_time = game_time;
		if (game_time >= target_game_time) {
			return;
		}
	}

	g_game_time = game_time;
	flight_advance_one_step(game_time + (uint16_t)g_elapsed_ticks);
#endif
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
#ifdef XVT_MODERN
	xvt_flight_sim_advance(target_game_time);
#else
	enum {
		PLAYER_COUNT = sizeof(g_input_frame_count) /
			       sizeof(g_input_frame_count[0]),
		MINIMUM_REPLAY_TICKS = 4,
	};

	int suppress_side_effects;
	int player_idx;
	int saved_elapsed_ticks;
	int saved_sim_steps_per_second;

	suppress_side_effects = g_flight_player_count == 1
					? 1
					: g_flight_sim_side_effects_suppressed;
	for (player_idx = 0; player_idx < PLAYER_COUNT; ++player_idx) {
		struct input_frame *frame;
		int frame_iteration;
		int frame_count;

		if (g_players[player_idx].participation_state == 0) {
			continue;
		}

		frame_count = g_input_frame_count[player_idx];
		frame = g_input_history[player_idx];
		for (frame_iteration = 0; frame_iteration < frame_count;
		     ++frame_iteration, ++frame) {
			int saved_game_time;
			uint8_t participation_state;

			if (!((suppress_side_effects != 0 &&
			       g_flight_player_count != 1) ||
			      frame->timestamp > g_players[player_idx]
							 .lockstep_timestamp ||
			      frame->awaiting_relay != 0)) {
				flight_sync_remove_input_history_frame(
					player_idx, frame);
				--frame;
				continue;
			}
			if (frame->timestamp > target_game_time ||
			    (suppress_side_effects == 0 &&
			     frame->input_source != 0)) {
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

				object = &g_object_table[g_players[player_idx]
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
					if (mobile_object->sim_state_timestamp >
					    g_players[player_idx]
						    .lockstep_timestamp) {
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
								.mobj->speed =
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
								.mobj->p_craft
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
					if (suppress_side_effects != 0) {
						continue;
					}
					frame->timestamp = g_game_time +
							   MINIMUM_REPLAY_TICKS;
					g_players[player_idx]
						.lockstep_timestamp =
						g_game_time;
				}
			}

			saved_elapsed_ticks = g_elapsed_ticks;
			saved_sim_steps_per_second = g_sim_steps_per_second;
			if (g_players[player_idx].object_index != -1) {
				g_single_object_update_override_idx =
					g_players[player_idx].object_index;
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
					if (g_sim_steps_per_second == 0) {
						g_sim_steps_per_second = 1;
					}
					flight_update_craft_steering_and_speed();
					object_update_lifetime_and_movement();
					g_object_table
						[g_single_object_update_override_idx]
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
					g_players[player_idx].saved_roll =
						object->roll;
					g_players[player_idx].saved_pitch =
						object->pitch;
					g_players[player_idx].saved_yaw =
						object->yaw;
					g_players[player_idx]
						.saved_lifetime_timer =
						object->mobj->lifetime_timer;
					g_players[player_idx]
						.saved_roll_impulse_rate =
						object->mobj->roll_impulse_rate;
					g_players[player_idx].saved_speed =
						object->mobj->speed;
					g_players[player_idx]
						.saved_speed_remainder =
						object->mobj->speed_remainder;
					g_players[player_idx]
						.saved_object_signature =
						object->object_signature;
					g_players[player_idx]
						.saved_awaiting_new_craft =
						g_players[player_idx]
							.awaiting_new_craft;
				}
				g_single_object_update_override_idx = -1;
			}

			g_elapsed_ticks =
				(uint16_t)(frame->timestamp -
					   g_players[player_idx]
						   .lockstep_timestamp);
			if (g_elapsed_ticks < MINIMUM_REPLAY_TICKS) {
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
				g_replay_inputs[player_idx].key_mods = 0;
			}
			if (g_flight_player_count > 1 &&
			    g_local_player == player_idx) {
				g_flight_sfx_side_effect_gate = 2;
				if (frame->timestamp >
				    g_last_local_replay_input_timestamp) {
					g_flight_sfx_side_effect_gate = 1;
					g_last_local_replay_input_timestamp =
						frame->timestamp;
				}
			}
			flight_update_player_step(player_idx);
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
#endif
}

#ifndef XVT_MODERN
/* Runs one flight from setup to end; the argument is ignored. It resets the
 * network indicators, abort flags, sound names, remote smoothing and loading
 * progress; sets the resolution mode from the surface width; finds the local
 * player and the player counts; clears the replay inputs and the unused blocks;
 * takes the flight options from g_game_config into g_flight_mission_state; seeds
 * the game random generators (from the configured seed in multiplayer, else
 * from the clock); clears g_players and marks the session's players as taking
 * part; loads the AI plans; sets up the display and palette, at 1 byte per
 * pixel from the mission's .pal file for entries 64 to 255, or
 * g_generate_mission_palette 1 when there is none; sets the proving grounds craft
 * and level for traincourse; resets input and fills g_flight_noise_table; resets
 * the message log and MFD pages; loads the mission, its voices and resources;
 * fills the loading bar; sets up the flight runtime state; starts the flight
 * music at a random point; and starts the proving grounds course. When the
 * options sync with the other players fails it frees the flight resources,
 * keeps a copy of the local player, fades the music and returns. Otherwise it
 * saves the world state, draws the first frame, waits for the mission start and
 * runs flight_run_mission_loop, then frees the world state buffers, commits the
 * results, frees the resources, saves the player's record (pilot_save) and
 * fades the music. Only the original build has this function. */
// FUNCTION: XVT 0x4473C0
void flight_main_loop(int unused)
{
	enum {
		PLAYER_COUNT = sizeof(g_players) / sizeof(g_players[0]),
		PALETTE_COLOR_COUNT = 256,
		PALETTE_BYTES =
			PALETTE_COLOR_COUNT * sizeof(struct rgb_triplet),
		PALETTE_HALF_BYTES = PALETTE_BYTES / 2,
		PALETTE_LAST_COLOR_OFFSET =
			PALETTE_BYTES - sizeof(struct rgb_triplet),
		PALETTE_CHANNEL_MAX = 63,
		FLIGHT_RESOURCE_SCRATCH_BYTES = 1024,
		MISSION_EXTENSION_LENGTH = 3,
		MISSION_EXTENSION_FIRST = 0,
		MISSION_EXTENSION_SECOND = 1,
		MISSION_EXTENSION_THIRD = 2,
		NOISE_TABLE_VALUE_LIMIT = 124,
		NO_VIEWPORT_INSET = 0,
		MUSIC_TRACK_FLIGHT = 2,
		MUSIC_START_CHOICE_COUNT = 4,
		MUSIC_VOLUME_MAX_LEVEL = 9,
		MUSIC_FADE_DIVISOR = 8,
		MUSIC_FADE_DURATION_MS = 1000,
		MILLISECONDS_PER_SECOND = 1000,
		MILLISECONDS_PER_MINUTE = 60000,
		PROVING_GROUNDS_DEFAULT_CRAFT = 2,
		PROVING_GROUNDS_DEFAULT_LEVEL = 4,
		PROVING_GROUNDS_SCORE_STEP_POINTS = 2000,
		PROVING_GROUNDS_SCORE_STEPS_PER_LEVEL = 5,
		RANDOM_SEED_XOR = 0xBEEF,
		ASTEROID_FIELD_RANDOM_SEED = -21267,
		DEFAULT_MODEL_LIGHT_DIRECTION = 18900,
	};

	uint8_t resource_scratch[FLIGHT_RESOURCE_SCRATCH_BYTES];
	int16_t abort_player_index;
	int16_t disconnect_player_index;
	int16_t connect_player_index;
	int16_t palette_byte_offset;
	int16_t mfd_index;
	int16_t mission_extension_offset;
	char saved_mission_extension_prefix[2];
	char saved_mission_extension_third;
	int mission_sync_succeeded;
	(void)unused;

	flight_pump_window_messages();
	g_packet_drop_indicator = 0;
	g_lag_indicator = 0;
	g_sw3d_skip_odd_scanlines = 0;
	g_flight_net_host_abort_received = 0;
	for (abort_player_index = 0; abort_player_index < PLAYER_COUNT;
	     ++abort_player_index) {
		g_player_abort_flags[abort_player_index] = 0;
	}

	fsfx_clear_sfx_name_table();
	flight_sync_reset_remote_player_render_smoothing();
	flight_loading_reset_progress_state();
	time_reset_elapsed_ticks();
	g_flight_display_surfaces_active = 1;
	g_flight_draw_to_hud_layer = 1;
	if (g_flight_viewport_inset_x == NO_VIEWPORT_INSET &&
	    g_surface_width == 320) {
		g_flight_resolution_mode = FLIGHT_RESOLUTION_320X240;
	} else if (g_flight_viewport_inset_x == NO_VIEWPORT_INSET &&
		   g_surface_width == 480) {
		g_flight_resolution_mode = FLIGHT_RESOLUTION_480X360;
	} else {
		g_flight_resolution_mode = FLIGHT_RESOLUTION_640X480;
	}

	g_flight_sim_side_effects_suppressed = 0;
	g_unused_flight_session_reset_state = 0;
	g_unused_flight_transient_reset_state = 0;
	g_local_player = net_session_find_player_slot_by_dpid(
		net_session_get_local_dplay_id());
	g_active_flight_player_count = net_session_get_player_count();
	g_flight_player_count = g_active_flight_player_count;
	memset(g_replay_inputs, 0, sizeof(g_replay_inputs));
	memset(g_unused_flight_network_block, 0,
	       sizeof(g_unused_flight_network_block));
	memset(g_unused_flight_runtime_block, 0,
	       sizeof(g_unused_flight_runtime_block));
	memset(&g_current_input_frame, 0, sizeof(g_current_input_frame));
	g_remote_player_render_smoothing_enabled = g_internet_play_enabled;
	g_flight_mission_state.connected_player_count =
		g_active_flight_player_count;
	g_flight_mission_state.max_connected_player_count_this_mission =
		g_active_flight_player_count;

	if ((unsigned int)g_pilot_data.num_human_players_last_mission > 1 &&
	    (unsigned int)g_pilot_data.mission_directory_id >=
		    MISSION_DIRECTORY_COMBAT_ENGAGEMENTS) {
		g_flight_mission_state.difficulty = GAME_DIFFICULTY_MEDIUM;
	} else {
		g_flight_mission_state.difficulty = g_game_config.difficulty;
		if (g_flight_mission_state.difficulty > GAME_DIFFICULTY_HARD) {
			g_flight_mission_state.difficulty =
				GAME_DIFFICULTY_EASY;
		}
	}
	g_flight_mission_state.collisions_enabled = g_game_config.collisions;
	g_flight_mission_state.craft_jumping_enabled =
		g_game_config.craft_jumping;
	g_flight_mission_state.random_variation_enabled =
		g_game_config.random_setup;
	g_flight_mission_state.battle_length_index =
		g_game_config.battle_length_index;
	g_flight_mission_state.locate_players_enabled =
		g_game_config.locate_players;
	g_flight_mission_state.player_flight_group_wave_mode =
		g_game_config.craft_waves;
	if (g_pilot_data.num_human_players_last_mission > 1) {
		g_flight_mission_state.mission_time_limit_minutes =
			g_game_config.mission_time_limit;
		g_flight_mission_state.team_victory_time_limit_minutes =
			g_game_config.last_team_time_limit_minutes;
		g_flight_mission_state.ai_opponents_enabled =
			g_game_config.ai_opponents;
	} else {
		g_flight_mission_state.mission_time_limit_minutes = UINT8_MAX;
		g_flight_mission_state.team_victory_time_limit_minutes = 0;
		g_flight_mission_state.ai_opponents_enabled = 1;
	}
	g_flight_mission_state.craft_impact_bounce_enabled = 1;
	if ((unsigned int)g_pilot_data.mission_directory_id >=
		    MISSION_DIRECTORY_COMBAT_ENGAGEMENTS &&
	    g_pilot_data.mission_sequence_active == 1) {
		g_flight_mission_state.random_variation_enabled = 0;
	}

	if (g_active_flight_player_count != 1) {
		g_game_rand_feedback_state = (int16_t)g_game_config.random_seed;
	} else {
		uint16_t random_seed;

		random_seed = (uint16_t)timeGetTime();
		random_seed ^= RANDOM_SEED_XOR;
		g_game_rand_feedback_state = (int16_t)random_seed;
	}
	{
		uint32_t random_time;

		random_time = timeGetTime();
		g_asteroid_field_rand_seed =
			(uint16_t)ASTEROID_FIELD_RANDOM_SEED;
		g_game_rand2_feedback_state =
			(uint16_t)(random_time + g_game_rand_feedback_state);
	}

	memset(g_players, 0, sizeof(g_players));
	{
		int16_t reset_player_index;

		for (reset_player_index = 0; reset_player_index < PLAYER_COUNT;
		     ++reset_player_index) {
			g_input_frame_count[reset_player_index] = 0;
			g_player_connected[reset_player_index] = 1;
			g_flight_net_world_checksum_peer_status
				[reset_player_index] = 0;
			g_players[reset_player_index].lockstep_timestamp = 0;
			g_players[reset_player_index]
				.next_engine_wash_check_time = 0;
			g_players[reset_player_index].field_5b5 = 0;
		}
	}
	for (disconnect_player_index = 0;
	     disconnect_player_index < PLAYER_COUNT;
	     ++disconnect_player_index) {
		g_players[disconnect_player_index].participation_state = 0;
	}
	for (connect_player_index = 0;
	     connect_player_index < g_active_flight_player_count;
	     ++connect_player_index) {
		g_players[connect_player_index].participation_state = 1;
	}

	g_flight_net_buffer_world_messages_until_checksum = 0;
	g_flight_net_world_checksum_epoch = 0;
	g_single_object_update_override_idx = -1;
	if (g_flight_conf_no_pilot == 0) {
		mission_sync_pilot_network_players_to_session_slots();
	}
	pai_loadplans((char *)g_pai_plan_resource_base_name);
	pai_cache_builtin_plan_ids();
	g_hud_cockpit_resources_loaded = 0;
	g_flight_sw_rot_sprite_coeff_cache_valid = 0;
	g_unused_flight_startup_object_pass_state = 0;
	g_unused_flight_debug_log_file = NULL;
	g_flight_sw_rot_sprite_span_runs_enabled = 1;

	flight_surface_lock();
	flight_display_configure_resolution_state();
	flight_surface_unlock();
	flight_render_transition_hook_stub();
	flight_surface_lock();
	flight_sw_init_framebuffer();
	flight_surface_unlock();
	nullsub_11();
	flight_display_flip();
	flight_render_configure_callbacks_for_resolution(3);
	fe_disk_io_read_all_bytes_or_fatal(g_flight_palette_resource_file_name,
					   resource_scratch);
	/* Inside this loop MISSION_EXTENSION_FIRST, _SECOND and _THIRD are reused as the red, green and blue
	 * offsets in a palette triplet. */
	for (palette_byte_offset = 0; palette_byte_offset < PALETTE_HALF_BYTES;
	     palette_byte_offset += sizeof(struct rgb_triplet)) {
		uint8_t channel;

		channel = resource_scratch[palette_byte_offset +
					   MISSION_EXTENSION_FIRST] >>
			  2;
		resource_scratch[palette_byte_offset +
				 MISSION_EXTENSION_FIRST] =
			resource_scratch[PALETTE_LAST_COLOR_OFFSET -
					 palette_byte_offset +
					 MISSION_EXTENSION_FIRST] >>
			2;
		resource_scratch[PALETTE_LAST_COLOR_OFFSET -
				 palette_byte_offset +
				 MISSION_EXTENSION_FIRST] = channel;
		channel = resource_scratch[palette_byte_offset +
					   MISSION_EXTENSION_SECOND] >>
			  2;
		resource_scratch[palette_byte_offset +
				 MISSION_EXTENSION_SECOND] =
			resource_scratch[PALETTE_LAST_COLOR_OFFSET -
					 palette_byte_offset +
					 MISSION_EXTENSION_SECOND] >>
			2;
		resource_scratch[PALETTE_LAST_COLOR_OFFSET -
				 palette_byte_offset +
				 MISSION_EXTENSION_SECOND] = channel;
		channel = resource_scratch[palette_byte_offset +
					   MISSION_EXTENSION_THIRD] >>
			  2;
		resource_scratch[palette_byte_offset +
				 MISSION_EXTENSION_THIRD] =
			resource_scratch[PALETTE_LAST_COLOR_OFFSET -
					 palette_byte_offset +
					 MISSION_EXTENSION_THIRD] >>
			2;
		resource_scratch[PALETTE_LAST_COLOR_OFFSET -
				 palette_byte_offset +
				 MISSION_EXTENSION_THIRD] = channel;
	}
	g_flight_set_palette_range_fn((struct rgb_triplet *)resource_scratch, 0,
				      PALETTE_COLOR_COUNT);
	flight_palette_apply_to_display();
	flight_surface_lock();
	fe_disk_io_init_global_buffers();
	flight_surface_unlock();
	flight_surface_clear_to_black();

	if (g_flight_bytes_per_pixel == 1) {
		mission_extension_offset = (int)strlen(g_current_mission_file) -
					   MISSION_EXTENSION_LENGTH;
		saved_mission_extension_prefix[MISSION_EXTENSION_FIRST] =
			g_current_mission_file[mission_extension_offset +
					       MISSION_EXTENSION_FIRST];
		saved_mission_extension_prefix[MISSION_EXTENSION_SECOND] =
			g_current_mission_file[mission_extension_offset +
					       MISSION_EXTENSION_SECOND];
		saved_mission_extension_third =
			g_current_mission_file[mission_extension_offset +
					       MISSION_EXTENSION_THIRD];
		g_current_mission_file[mission_extension_offset +
				       MISSION_EXTENSION_FIRST] = 'p';
		g_current_mission_file[mission_extension_offset +
				       MISSION_EXTENSION_SECOND] = 'a';
		g_current_mission_file[mission_extension_offset +
				       MISSION_EXTENSION_THIRD] = 'l';
		if (fe_disk_io_open_global_stream(g_current_mission_file, "rb",
						  0, 0) == 0) {
			g_generate_mission_palette = 1;
		} else {
			g_generate_mission_palette = 0;
			fe_disk_io_close_global_stream(0);
			fe_disk_io_read_all_bytes_or_fatal(
				g_current_mission_file, g_flight_aux_buffer);
			g_flight_set_palette_range_fn(
				(struct rgb_triplet *)g_flight_aux_buffer,
				PALETTE_CHANNEL_MAX + 1,
				PALETTE_COLOR_COUNT -
					(PALETTE_CHANNEL_MAX + 1));
		}
		g_current_mission_file[mission_extension_offset +
				       MISSION_EXTENSION_FIRST] =
			saved_mission_extension_prefix[MISSION_EXTENSION_FIRST];
		g_current_mission_file[mission_extension_offset +
				       MISSION_EXTENSION_SECOND] =
			saved_mission_extension_prefix
				[MISSION_EXTENSION_SECOND];
		g_current_mission_file[mission_extension_offset +
				       MISSION_EXTENSION_THIRD] =
			saved_mission_extension_third;
	}

	if (g_flight_conf_train_course != 0) {
		g_flight_mission_state.proving_grounds_craft_type =
			PROVING_GROUNDS_DEFAULT_CRAFT;
		g_flight_mission_state.proving_grounds_level =
			PROVING_GROUNDS_DEFAULT_LEVEL;
	} else {
		g_flight_mission_state.proving_grounds_craft_type = 0;
		g_flight_mission_state.proving_grounds_level = 0;
	}
	flight_surface_lock();
	flight_input_reset_runtime_state();
	flight_surface_unlock();
	{
		int16_t noise_index;

		for (noise_index = 0;
		     noise_index < (int)sizeof(g_flight_noise_table) - 1;
		     noise_index += 2) {
			do {
				g_flight_noise_table[noise_index] =
					(uint8_t)(rand() & 0x7F);
			} while (g_flight_noise_table[noise_index] >
				 NOISE_TABLE_VALUE_LIMIT);
			g_flight_noise_table[noise_index + 1] =
				(uint8_t)(rand() & 3);
		}
	}

	g_message_log_total_count = 0;
	g_unused_flight_message_runtime_state = 0;
	g_world_light_direction_x = DEFAULT_MODEL_LIGHT_DIRECTION;
	g_world_light_direction_y = DEFAULT_MODEL_LIGHT_DIRECTION;
	g_world_light_direction_z = DEFAULT_MODEL_LIGHT_DIRECTION;
	g_system_message_display_enabled = 1;
	g_ready_message_pane_left = -1;
	g_message_log_write_index = UINT16_MAX;
	g_mfd_active_page = MFD_PAGE_NONE;
	g_mfd_secondary_page = MFD_PAGE_NONE;
	g_mfd_saved_active_page = MFD_PAGE_NONE;
	g_mfd_saved_secondary_page = MFD_PAGE_NONE;
	for (mfd_index = 0; mfd_index < (int)(sizeof(g_mfd_page_states) /
					      sizeof(g_mfd_page_states[0]));
	     ++mfd_index) {
		g_saved_mfd_page_states[mfd_index] = MFD_PAGE_STATE_CLOSED;
		g_mfd_page_states[mfd_index] = MFD_PAGE_STATE_CLOSED;
	}
	g_damage_mfd_current_system_id = 0;

	flight_surface_lock();
	mission_init(g_current_mission_file);
	flight_surface_unlock();
	fsfx_load_mission_voice_sfx();
	fe_disk_io_init_resources();
	if (g_generate_mission_palette != 0) {
		mission_extension_offset = (int)strlen(g_current_mission_file) -
					   MISSION_EXTENSION_LENGTH;
		saved_mission_extension_prefix[MISSION_EXTENSION_FIRST] =
			g_current_mission_file[mission_extension_offset +
					       MISSION_EXTENSION_FIRST];
		saved_mission_extension_prefix[MISSION_EXTENSION_SECOND] =
			g_current_mission_file[mission_extension_offset +
					       MISSION_EXTENSION_SECOND];
		saved_mission_extension_third =
			g_current_mission_file[mission_extension_offset +
					       MISSION_EXTENSION_THIRD];
		g_current_mission_file[mission_extension_offset +
				       MISSION_EXTENSION_FIRST] = 'p';
		g_current_mission_file[mission_extension_offset +
				       MISSION_EXTENSION_SECOND] = 'a';
		g_current_mission_file[mission_extension_offset +
				       MISSION_EXTENSION_THIRD] = 'l';
		g_current_mission_file[mission_extension_offset +
				       MISSION_EXTENSION_FIRST] =
			saved_mission_extension_prefix[MISSION_EXTENSION_FIRST];
		g_current_mission_file[mission_extension_offset +
				       MISSION_EXTENSION_SECOND] =
			saved_mission_extension_prefix
				[MISSION_EXTENSION_SECOND];
		g_current_mission_file[mission_extension_offset +
				       MISSION_EXTENSION_THIRD] =
			saved_mission_extension_third;
	}

	flight_loading_draw_progress_to_completion();
	flight_surface_lock();
	mission_init_flight_runtime_state();
	flight_surface_unlock();
	g_dynamic_music_outcome_latched = 0;
	if (g_game_config.music_enabled != 0 &&
	    g_game_config.music_volume != 0 && music_cd_initialize() != 0) {
		int music_choice;
		uint16_t music_volume;
		uint32_t music_update_ms;

		music_volume = UINT16_MAX * g_game_config.music_volume /
			       MUSIC_VOLUME_MAX_LEVEL;
		music_cd_set_aux_volume(music_volume);
		music_choice = game_rand2() & (MUSIC_START_CHOICE_COUNT - 1);
		music_cd_play_track_from_time(
			MUSIC_TRACK_FLIGHT,
			g_dynamic_music_initial_start_minute_choices
				[music_choice],
			g_dynamic_music_initial_start_second_choices
				[music_choice]);
		g_dynamic_music_track_remaining_ms =
			music_cd_get_track_length_ms(MUSIC_TRACK_FLIGHT);
		g_dynamic_music_track_remaining_ms -=
			MILLISECONDS_PER_MINUTE *
			g_dynamic_music_initial_start_minute_choices
				[music_choice];
		g_dynamic_music_track_remaining_ms -=
			MILLISECONDS_PER_SECOND *
			g_dynamic_music_initial_start_second_choices
				[music_choice];
		music_update_ms = timeGetTime();
		g_dynamic_music_state = MUSIC_TRACK_FLIGHT;
		g_dynamic_music_last_update_ms = music_update_ms;
	} else {
		g_dynamic_music_track_remaining_ms = INT32_MAX;
		g_dynamic_music_state = 0;
	}

	if (g_flight_mission_state.proving_grounds_mode_active != 0) {
		proving_grounds_init_course_objects();
		proving_grounds_start_level(
			g_flight_mission_state.proving_grounds_level);
		if (g_flight_mission_state.proving_grounds_mode_active != 0 &&
		    g_flight_mission_state.proving_grounds_level > 1) {
			g_msg_arg_table[0] =
				g_flight_mission_state.proving_grounds_level -
				1;
			msg_emit_in_flight_message(
				IFMSG_197_ARG_10000_POINTS_AWARDED_FOR_PREVIOUS_LEVELS,
				g_local_player);
			g_flight_mission_state.proving_grounds_score =
				PROVING_GROUNDS_SCORE_STEP_POINTS *
				(PROVING_GROUNDS_SCORE_STEPS_PER_LEVEL *
					 g_flight_mission_state
						 .proving_grounds_level -
				 PROVING_GROUNDS_SCORE_STEPS_PER_LEVEL);
		}
	}

	do {
		g_input_timestamp += time_consume_elapsed_ticks();
	} while (g_input_timestamp == 0);
	object_relink_mobile_object_pointers();
	mission_sync_succeeded = flight_net_sync_player_options_and_taunts();
	if (mission_sync_succeeded == 0) {
		g_flight_display_surfaces_active = 0;
		sound_stop_all_instances();
		sound_empty_stub();
		fe_disk_io_free_flight_resources();
		if (g_pre_flight_resolution_mode != g_flight_resolution_mode) {
			flight_display_apply_resolution_mode_stub(
				g_pre_flight_resolution_mode);
		}
		memcpy(&g_local_player_snapshot_on_options_sync_failure,
		       &g_players[g_local_player],
		       sizeof(g_local_player_snapshot_on_options_sync_failure));
		if (g_game_config.music_enabled != 0 &&
		    g_game_config.music_volume != 0) {
			uint16_t music_volume;

			music_volume = (uint16_t)(UINT16_MAX *
						  g_game_config.music_volume /
						  MUSIC_VOLUME_MAX_LEVEL);
			music_cd_fade_aux_volume(
				music_volume, music_volume / MUSIC_FADE_DIVISOR,
				MUSIC_FADE_DURATION_MS);
		}
		music_cd_close_device();
		return;
	}

	{
		int16_t object_index;

		g_unused_flight_startup_object_pass_state = 0;
		for (object_index = 0;
		     object_index < g_region_main_object_slot_end;
		     ++object_index) {
			if (g_object_table[object_index].mobj != NULL) {
				g_object_table[object_index]
					.mobj->sim_state_timestamp = 0;
			}
		}
	}
	flight_alloc_world_state_buffers();
	flight_sync_clear_buffered_world_messages();
	flight_save_world_state();
	flight_view_render_startup_frame();
	net_session_stub_return_true();
	if (flight_net_wait_for_mission_start() != 0) {
		flight_run_mission_loop();
	}
	flight_free_world_state_buffers();
	if (g_unused_flight_debug_log_file != NULL) {
		FILE_RAW_CLOSE(g_unused_flight_debug_log_file);
	}
	g_flight_render_transition_hook();
	fe_disk_io_commit_flight_results(0, 0);
	g_flight_display_surfaces_active = 0;
	sound_stop_all_instances();
	fe_disk_io_free_flight_resources();
	if (g_pre_flight_resolution_mode != g_flight_resolution_mode) {
		flight_display_apply_resolution_mode_stub(
			g_pre_flight_resolution_mode);
	}
	pilot_save(0);
	if (g_game_config.music_enabled != 0 &&
	    g_game_config.music_volume != 0) {
		uint16_t music_volume;

		music_volume =
			(uint16_t)(UINT16_MAX * g_game_config.music_volume /
				   MUSIC_VOLUME_MAX_LEVEL);
		music_cd_fade_aux_volume(music_volume,
					 music_volume / MUSIC_FADE_DIVISOR,
					 MUSIC_FADE_DURATION_MS);
	}
	music_cd_close_device();
}
#endif

#ifndef XVT_MODERN
/* The original build's frame loop for one mission; it returns when the mission
 * is over. Each pass waits until the input clock (g_input_timestamp) is at least
 * 8 ticks past g_game_time and reads the network packets. A client leaves when
 * its host has been silent over 7,080 ticks. Once the mission has ended
 * (flight_recount_players_and_check_mission_end) it returns when the simulation has
 * caught up with the server, writing the message log first when
 * g_radio_message_backup_enabled, or once the clock is 1,180 ticks past
 * g_game_time. Solo, it places a step target from the last target plus
 * g_predicted_frame_delta, moved toward the clock, at least 4 ticks past
 * g_game_time; samples the local input there; steps the simulation to it with
 * side effects on; and moves the server time with it. In multiplayer it first
 * steers the clock toward g_server_tick_time plus g_flight_net_clock_lead_ticks by a
 * sixteenth of the gap, at most an eighth of g_predicted_frame_delta (kept in
 * g_flight_net_clock_adjust_accum_ticks), jumps forward when behind the server, and
 * on a client more than 1,652 ticks further ahead than
 * g_flight_net_clock_lead_ticks shows the communication failure alert and waits for
 * the server, where ESC or a 7,080-tick silence makes the player leave and
 * returns. It then places the step target the same way, samples the local
 * input, queues predicted remote input and steps the simulation with side
 * effects suppressed. Each pass then sets g_lag_indicator, 0 to 3 as the clock
 * runs 472, 944 and 1,416 ticks beyond g_server_tick_time plus
 * g_flight_net_clock_lead_ticks, and g_packet_drop_indicator, smooths the remote
 * craft, draws the frame (flight_view_render_frame) and plays the queued sounds.
 * With the tickcounter option it also fills g_flight_update_duration_histogram and
 * formats timing lines into buffers nothing shows. Only the original build has
 * this function. */
// FUNCTION: XVT 0x447F50
void flight_run_mission_loop(void)
{
	enum {
		MINIMUM_FRAME_ADVANCE_TICKS = 4,
		MINIMUM_FRAME_WAIT_TICKS = 8,
		FRAME_ADJUST_DIVISOR_SHIFT = 3,
		CLOCK_ADJUST_DIVISOR_SHIFT = 4,
		MAX_FINE_FRAME_ADJUSTMENT = 4,
		MISSION_END_WAIT_LIMIT_TICKS = 1180,
		LAG_LEVEL_1_TICKS = 472,
		LAG_LEVEL_2_TICKS = 944,
		LAG_LEVEL_3_TICKS = 1416,
		EXCESSIVE_CLOCK_LEAD_TICKS = 1652,
		HOST_TIMEOUT_TICKS = 7080,
		COUNTDOWN_INTERVAL_TICKS = 118,
		COUNTDOWN_HALF_SECONDS_THRESHOLD = 50,
		UPDATE_HISTOGRAM_BUCKETS = 20,
		LONG_UPDATE_TICKS = 8,
		PACKET_DROP_SCORE_STEP = 10,
		PACKET_DROP_LEVEL_2_SCORE = 10,
		PACKET_DROP_LEVEL_3_SCORE = 20,
		ALERT_BACKGROUND_COLOR = 0x34
	};

	int previous_remaining_half_seconds;
	int frame_start_timestamp;
	int loop_start_timestamp;
	int simulation_warp_ticks;
	char countdown_text[80];
	char status_line[256];
	char overlay_line[180];
	char too_far_ahead_log_line[180];
	char fell_behind_log_line[180];

	g_flight_last_step_target_timestamp = 0;
	g_last_local_replay_input_timestamp = 0;
	g_flight_sfx_side_effect_gate = 0;
	g_flight_prev_host_packet_drop_count = 0;
	g_flight_packet_drop_score = 0;
	memset(g_flight_update_duration_histogram, 0,
	       sizeof(g_flight_update_duration_histogram));

	for (;;) {
		int network_update_ticks;
		int render_ticks;
		int update_ticks;
		int loop_ticks;
		int frame_target_timestamp;
		int saved_input_timestamp;
		int packet_start_timestamp;
		int mission_ended;

		simulation_warp_ticks = 0;
		g_input_timestamp += time_consume_elapsed_ticks();
		loop_start_timestamp = g_input_timestamp;
		for (g_input_timestamp += time_consume_elapsed_ticks();
		     g_input_timestamp - g_game_time < MINIMUM_FRAME_WAIT_TICKS;
		     g_input_timestamp += time_consume_elapsed_ticks()) {
		}

		packet_start_timestamp = g_input_timestamp;
		flight_net_process_incoming_packets();
		if (net_session_is_local_host() == 0 &&
		    g_flight_net_host_timeout_elapsed_ticks >
			    HOST_TIMEOUT_TICKS) {
			g_players[g_local_player].participation_state = 0;
			flight_net_mark_pilot_network_player_left(
				g_local_player);
			return;
		}
		g_input_timestamp += time_consume_elapsed_ticks();
		network_update_ticks = g_input_timestamp;
		mission_ended = flight_recount_players_and_check_mission_end();
		network_update_ticks -= packet_start_timestamp;

		if (mission_ended != 0) {
			if (g_game_time == g_server_tick_time) {
				if (g_radio_message_backup_enabled != 0) {
					msg_write_message_log_file();
					g_radio_message_backup_enabled = 0;
				}
				return;
			}
			if (g_input_timestamp - g_game_time >
			    MISSION_END_WAIT_LIMIT_TICKS) {
				return;
			}
			for (g_input_timestamp += time_consume_elapsed_ticks();
			     g_input_timestamp - g_game_time <
			     MINIMUM_FRAME_WAIT_TICKS;
			     g_input_timestamp +=
			     time_consume_elapsed_ticks()) {
			}
			continue;
		}

		frame_start_timestamp = g_input_timestamp;
		if (g_flight_player_count == 1) {
			int frame_adjustment;

			g_input_timestamp += time_consume_elapsed_ticks();
			if (g_flight_last_step_target_timestamp == 0) {
				frame_target_timestamp = g_input_timestamp;
			} else {
				frame_target_timestamp =
					g_flight_last_step_target_timestamp +
					g_predicted_frame_delta;
				if ((unsigned int)frame_target_timestamp >=
				    (unsigned int)g_input_timestamp) {
					if ((unsigned int)
						    frame_target_timestamp >
					    (unsigned int)g_input_timestamp) {
						frame_adjustment =
							frame_target_timestamp -
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
						frame_target_timestamp -=
							frame_adjustment;
					}
				} else {
					frame_adjustment =
						g_input_timestamp -
						frame_target_timestamp;
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
							g_input_timestamp -
							frame_target_timestamp;
					}
					frame_target_timestamp +=
						frame_adjustment;
				}
			}
			if (frame_target_timestamp - g_game_time <
			    MINIMUM_FRAME_ADVANCE_TICKS) {
				frame_target_timestamp =
					g_game_time +
					MINIMUM_FRAME_ADVANCE_TICKS;
			}
			g_predicted_frame_delta =
				frame_target_timestamp -
				g_flight_last_step_target_timestamp;
			saved_input_timestamp = g_input_timestamp;
			g_input_timestamp = frame_target_timestamp;
			flight_net_sample_local_input();
			g_flight_sim_side_effects_suppressed = 0;
			g_net_update_interval_ticks =
				g_input_timestamp - g_game_time;
			flight_step_sim_to_time(g_input_timestamp);
			g_game_time = g_input_timestamp;
			g_server_tick_time = g_input_timestamp;
			g_flight_last_step_target_timestamp = g_input_timestamp;
			g_input_timestamp +=
				saved_input_timestamp - frame_target_timestamp;
			sound_flush_queued_effects();
		} else {
			int clock_adjustment;

			g_input_timestamp += time_consume_elapsed_ticks();
			if (g_server_tick_time +
				    g_flight_net_clock_lead_ticks >=
			    (unsigned int)g_input_timestamp) {
				if (g_server_tick_time +
					    g_flight_net_clock_lead_ticks >
				    (unsigned int)g_input_timestamp) {
					clock_adjustment =
						(g_server_tick_time +
						 g_flight_net_clock_lead_ticks -
						 g_input_timestamp) >>
						CLOCK_ADJUST_DIVISOR_SHIFT;
					if (clock_adjustment == 0) {
						clock_adjustment = 1;
					}
					if (clock_adjustment >
					    g_predicted_frame_delta >>
					    FRAME_ADJUST_DIVISOR_SHIFT) {
						clock_adjustment =
							g_predicted_frame_delta >>
							FRAME_ADJUST_DIVISOR_SHIFT;
					}
					g_flight_net_clock_adjust_accum_ticks -=
						clock_adjustment;
					g_input_timestamp += clock_adjustment;
				}
			} else {
				clock_adjustment =
					(g_input_timestamp -
					 g_flight_net_clock_lead_ticks -
					 g_server_tick_time) >>
					CLOCK_ADJUST_DIVISOR_SHIFT;
				if (clock_adjustment == 0) {
					clock_adjustment = 1;
				}
				if (clock_adjustment >
				    g_predicted_frame_delta >>
				    FRAME_ADJUST_DIVISOR_SHIFT) {
					clock_adjustment =
						g_predicted_frame_delta >>
						FRAME_ADJUST_DIVISOR_SHIFT;
				}
				g_flight_net_clock_adjust_accum_ticks +=
					clock_adjustment;
				g_input_timestamp -= clock_adjustment;
			}

			if (g_server_tick_time > g_input_timestamp) {
				sprintf(fell_behind_log_line,
					"Fell Behind! tickcounter:%-7d serverticks:%-7d adjustment:%-4d\n",
					g_input_timestamp, g_server_tick_time,
					g_server_tick_time +
						g_flight_net_clock_lead_ticks -
						g_input_timestamp);
				clock_adjustment =
					g_server_tick_time +
					g_flight_net_clock_lead_ticks -
					g_input_timestamp;
				g_input_timestamp += clock_adjustment;
				g_flight_net_clock_adjust_accum_ticks -=
					clock_adjustment;
			}

			if (g_input_timestamp - g_server_tick_time >
				    g_flight_net_clock_lead_ticks +
					    EXCESSIVE_CLOCK_LEAD_TICKS &&
			    net_session_is_local_host() == 0) {
				int status_pulse_ticks;
				unsigned int frame_delta;

				flight_net_broadcast_player_disconnected(
					g_local_player);
				sprintf(too_far_ahead_log_line,
					"Too far Ahead! tickcounter:%-7d serverticks:%-7d warp:%-4d\n",
					g_input_timestamp, g_server_tick_time,
					g_input_timestamp - g_server_tick_time);
				status_pulse_ticks = 0;
				frame_delta = time_consume_elapsed_ticks();
				g_flight_net_host_timeout_elapsed_ticks = 0;
				g_input_timestamp += frame_delta;
				flight_alert_save_box_background();
				strcpy(status_line,
				       g_str_disk_io_messages
					       [DISK_IO_STR_COM_FAILURE_WAITING]);
				{
					char *player_name;

					player_name =
						flight_net_resolve_resync_player_name();
					if (player_name != NULL) {
						strcat(status_line,
						       player_name);
					}
				}
				flight_alert_draw_box(1, status_line,
						      ALERT_BACKGROUND_COLOR);

				do {
					int packet_start_timestamp;
					int remaining_half_seconds;

					if (g_input_timestamp -
						    g_server_tick_time <=
					    g_flight_net_clock_lead_ticks) {
						break;
					}
					if (flight_input_has_key_ready() != 0 &&
					    flight_input_get_next_key() ==
						    FLIGHT_KEY_ESCAPE) {
						g_flight_mission_state
							.mission_end_pending =
							1;
						flight_net_broadcast_player_abort(
							g_local_player);
						g_player_abort_flags
							[g_local_player] = 1;
						g_players[g_local_player]
							.participation_state =
							0;
						flight_net_mark_pilot_network_player_left(
							g_local_player);
						return;
					}

					packet_start_timestamp =
						g_input_timestamp;
					flight_net_process_incoming_packets();
					status_pulse_ticks -=
						packet_start_timestamp;
					g_input_timestamp +=
						time_consume_elapsed_ticks();
					status_pulse_ticks += g_input_timestamp;
					g_flight_net_host_timeout_elapsed_ticks +=
						g_input_timestamp -
						packet_start_timestamp;
					g_input_timestamp =
						packet_start_timestamp;
					if (g_flight_net_host_timeout_elapsed_ticks >
					    HOST_TIMEOUT_TICKS) {
						g_flight_mission_state
							.mission_end_pending =
							1;
						flight_net_broadcast_player_abort(
							g_local_player);
						g_player_abort_flags
							[g_local_player] = 1;
						g_players[g_local_player]
							.participation_state =
							0;
						flight_net_mark_pilot_network_player_left(
							g_local_player);
						return;
					}
					if (status_pulse_ticks >
					    SIMULATION_TICKS_PER_SECOND) {
						status_pulse_ticks = 0;
						flight_net_send_still_loading_pulse();
					}

					remaining_half_seconds =
						(HOST_TIMEOUT_TICKS -
						 g_flight_net_host_timeout_elapsed_ticks) /
						COUNTDOWN_INTERVAL_TICKS;
					if (previous_remaining_half_seconds !=
					    remaining_half_seconds) {
						previous_remaining_half_seconds =
							remaining_half_seconds;
						if (remaining_half_seconds >=
						    COUNTDOWN_HALF_SECONDS_THRESHOLD) {
							if ((remaining_half_seconds &
							     1) != 0) {
								flight_alert_draw_box(
									3,
									g_str_disk_io_messages
										[DISK_IO_STR_ESC_DISCONNECT],
									ALERT_BACKGROUND_COLOR);
							} else {
								flight_alert_draw_box(
									3,
									g_str_disk_io_messages
										[DISK_IO_STR_RECOVERING_WAIT],
									ALERT_BACKGROUND_COLOR);
							}
						} else {
							sprintf(countdown_text,
								g_str_disk_io_messages
									[DISK_IO_STR_DISCONNECT_COUNTDOWN],
								remaining_half_seconds /
									2,
								5 * (remaining_half_seconds &
								     1));
							flight_alert_draw_box(
								3,
								countdown_text,
								ALERT_BACKGROUND_COLOR);
						}
					}
				} while (
					flight_recount_players_and_check_mission_end() ==
						0 ||
					g_game_time != g_server_tick_time);

				flight_alert_restore_box_background();
				time_consume_elapsed_ticks();
				if (flight_recount_players_and_check_mission_end() !=
					    0 &&
				    g_game_time == g_server_tick_time) {
					return;
				}
				g_input_timestamp =
					g_server_tick_time +
					g_flight_net_clock_lead_ticks;
			}

			if ((unsigned int)g_input_timestamp >
			    (unsigned int)g_game_time) {
				int frame_adjustment;

				if (g_flight_last_step_target_timestamp == 0) {
					frame_target_timestamp =
						g_input_timestamp;
				} else {
					frame_target_timestamp =
						g_flight_last_step_target_timestamp +
						g_predicted_frame_delta;
					if ((unsigned int)
						    frame_target_timestamp >=
					    (unsigned int)g_input_timestamp) {
						if ((unsigned int)
							    frame_target_timestamp >
						    (unsigned int)
							    g_input_timestamp) {
							frame_adjustment =
								frame_target_timestamp -
								g_input_timestamp;
							if (frame_adjustment >
							    g_predicted_frame_delta >>
							    FRAME_ADJUST_DIVISOR_SHIFT) {
								frame_adjustment =
									g_predicted_frame_delta >>
									FRAME_ADJUST_DIVISOR_SHIFT;
							}
							if (frame_adjustment ==
							    0) {
								frame_adjustment =
									1;
							}
							if (frame_adjustment >
							    MAX_FINE_FRAME_ADJUSTMENT) {
								frame_adjustment =
									frame_target_timestamp -
									g_input_timestamp;
							}
							frame_target_timestamp -=
								frame_adjustment;
						}
					} else {
						frame_adjustment =
							g_input_timestamp -
							frame_target_timestamp;
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
								g_input_timestamp -
								frame_target_timestamp;
						}
						frame_target_timestamp +=
							frame_adjustment;
					}
				}
				if (frame_target_timestamp - g_game_time <
				    MINIMUM_FRAME_ADVANCE_TICKS) {
					frame_target_timestamp =
						g_game_time +
						MINIMUM_FRAME_ADVANCE_TICKS;
				}
				g_predicted_frame_delta =
					frame_target_timestamp -
					g_flight_last_step_target_timestamp;
				saved_input_timestamp = g_input_timestamp;
				g_input_timestamp = frame_target_timestamp;
				flight_net_sample_local_input();
				flight_sync_queue_predicted_remote_input_frames(
					g_predicted_frame_delta);
				g_flight_sim_side_effects_suppressed = 1;
				simulation_warp_ticks =
					g_input_timestamp - g_game_time;
				flight_step_sim_to_time(g_input_timestamp);
				g_game_time = g_input_timestamp;
				g_flight_last_step_target_timestamp =
					g_input_timestamp;
				g_input_timestamp += saved_input_timestamp -
						     frame_target_timestamp;
			}
		}

		g_input_timestamp += time_consume_elapsed_ticks();
		update_ticks = g_input_timestamp;
		update_ticks -= frame_start_timestamp;
		g_input_timestamp += time_consume_elapsed_ticks();
		{
			int lag_ticks;

			saved_input_timestamp = g_input_timestamp;
			lag_ticks = g_input_timestamp -
				    g_flight_net_clock_lead_ticks -
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

		flight_sync_apply_remote_player_render_smoothing();
		if (g_flight_player_count > 1) {
			g_flight_sfx_side_effect_gate = 1;
		}
		flight_view_render_frame();
		g_flight_sfx_side_effect_gate = 0;
		sound_flush_queued_effects();
		flight_sync_capture_samples_and_restore_poses();
		g_input_timestamp += time_consume_elapsed_ticks();
		render_ticks = g_input_timestamp;
		render_ticks -= saved_input_timestamp;
		loop_ticks = g_input_timestamp;
		loop_ticks -= loop_start_timestamp;
		if (g_input_timestamp == loop_start_timestamp) {
			loop_ticks = 1;
		}

		if (g_flight_conf_tick_counter_enabled == 0) {
			g_flight_tick_overlay_sample_count = 0;
			g_flight_tick_overlay_window_ticks = 0;
		} else {
			unsigned int histogram_total;
			int histogram_index;
			int ai_projectile_count;
			int player_projectile_count;
			int object_index;

			if (g_flight_tick_overlay_window_ticks >
			    LAG_LEVEL_2_TICKS) {
				g_flight_tick_overlay_sample_count = 0;
				g_flight_tick_overlay_window_ticks = 0;
			}
			g_flight_tick_overlay_last_loop_ticks = loop_ticks;
			g_flight_tick_overlay_window_ticks += loop_ticks;
			++g_flight_tick_overlay_sample_count;
			sprintf(overlay_line,
				"R:%-2d U:%-2d N:%-2d O:%-2d T:%-2d FR:%-2d NOW:%-7dL:%-7dS:%-7dW:%-3dD:%-3dA%d\n",
				render_ticks, update_ticks,
				network_update_ticks,
				loop_ticks - network_update_ticks -
					update_ticks - render_ticks,
				loop_ticks,
				SIMULATION_TICKS_PER_SECOND / loop_ticks,
				g_input_timestamp, g_game_time,
				g_server_tick_time,
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
				++g_flight_update_duration_histogram
					[update_ticks];
			}
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
			for (histogram_index = 0;
			     histogram_index < UPDATE_HISTOGRAM_BUCKETS;
			     ++histogram_index) {
				histogram_total +=
					g_flight_update_duration_histogram
						[histogram_index];
			}
			if (histogram_total != 0) {
				sprintf(g_mission_debug_buffer,
					"Pct  0:%2d  1:%2d  2:%2d  3:%2d  4:%2d  5:%2d  6:%2d  7:%2d  8:%2d   9:%2d\n",
					100 *
						g_flight_update_duration_histogram
							[0] /
						histogram_total,
					100 *
						g_flight_update_duration_histogram
							[1] /
						histogram_total,
					100 *
						g_flight_update_duration_histogram
							[2] /
						histogram_total,
					100 *
						g_flight_update_duration_histogram
							[3] /
						histogram_total,
					100 *
						g_flight_update_duration_histogram
							[4] /
						histogram_total,
					100 *
						g_flight_update_duration_histogram
							[5] /
						histogram_total,
					100 *
						g_flight_update_duration_histogram
							[6] /
						histogram_total,
					100 *
						g_flight_update_duration_histogram
							[7] /
						histogram_total,
					100 *
						g_flight_update_duration_histogram
							[8] /
						histogram_total,
					100 *
						g_flight_update_duration_histogram
							[9] /
						histogram_total);
				sprintf(g_mission_debug_buffer,
					"Pct 10:%2d 11:%2d 12:%2d 13:%2d 14:%2d 15:%2d 16:%2d 17:%2d 18:%2d >18:%2d\n",
					100 *
						g_flight_update_duration_histogram
							[10] /
						histogram_total,
					100 *
						g_flight_update_duration_histogram
							[11] /
						histogram_total,
					100 *
						g_flight_update_duration_histogram
							[12] /
						histogram_total,
					100 *
						g_flight_update_duration_histogram
							[13] /
						histogram_total,
					100 *
						g_flight_update_duration_histogram
							[14] /
						histogram_total,
					100 *
						g_flight_update_duration_histogram
							[15] /
						histogram_total,
					100 *
						g_flight_update_duration_histogram
							[16] /
						histogram_total,
					100 *
						g_flight_update_duration_histogram
							[17] /
						histogram_total,
					100 *
						g_flight_update_duration_histogram
							[18] /
						histogram_total,
					100 *
						g_flight_update_duration_histogram
							[19] /
						histogram_total);
			}

			ai_projectile_count = 0;
			player_projectile_count = 0;
			for (object_index = g_projectile_object_slot_start;
			     object_index < g_projectile_object_slot_end;
			     ++object_index) {
				if (g_object_table[object_index].object_type !=
				    0) {
					if (g_object_table[object_index]
						    .genus_id == 6) {
						++player_projectile_count;
					} else {
						++ai_projectile_count;
					}
				}
			}
			if (update_ticks >= LONG_UPDATE_TICKS) {
				sprintf(g_mission_debug_buffer,
					"****** Long Update: %d ***** Warp: %d  *****  Player:  %d *****  AI:  %d\n",
					update_ticks, simulation_warp_ticks,
					player_projectile_count,
					ai_projectile_count);
			}
		}
	}
}
#endif

/* Counts the players taking part (participation_state 1 or 2), stores the count
 * in g_active_flight_player_count and returns it. */
// FUNCTION: XVT 0x448CD0
int flight_update_active_player_count(void)
{
	int active_player_count;
	int player_index;

	active_player_count = 0;
	for (player_index = 0; player_index < 8; player_index++) {
		if (g_players[player_index].participation_state == 1 ||
		    g_players[player_index].participation_state == 2) {
			active_player_count++;
		}
		g_active_flight_player_count = active_player_count;
	}

	return active_player_count;
}

/* Recounts g_active_flight_player_count as flight_update_active_player_count does,
 * sets g_flight_mission_state.mission_end_pending to 1 when no player is connected
 * (participation_state 1) or the local player no longer takes part, and returns
 * mission_end_pending. */
// FUNCTION: XVT 0x448D00
int flight_recount_players_and_check_mission_end(void)
{
	int player_index;
	int connected_count;
	int connected_or_pending_count;
	uint8_t connected_state;

	connected_count = 0;
	connected_or_pending_count = 0;
	for (player_index = 0; player_index < 8; player_index++) {
		connected_state = g_players[player_index].participation_state;
		if (connected_state == 1 || connected_state == 2) {
			connected_or_pending_count++;
		}
		if (connected_state == 1) {
			connected_count++;
		}
		g_active_flight_player_count = connected_or_pending_count;
	}

	if (connected_count == 0) {
		g_flight_mission_state.mission_end_pending = 1;
	}
	if (g_players[g_local_player].participation_state == 0) {
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
#ifdef XVT_MODERN
	return xvt_snapshot_live_checksum();
#else
	uint32_t checksum;
	uint32_t *checksum_ptr;
	int first_slot;
	int char_data_index;
	int mobile_object_index;
	int object_index;
	int static_object_index;
	int craft_index;
	int projectile_index;
	int flight_group_index;
	int goal_index;
	int player_index;

	checksum = 0;
	checksum_ptr = &checksum;
	first_slot = g_object_slot_range_by_genus[16].start;
	for (char_data_index = 0;
	     char_data_index < (int)g_mobile_object_char_data_count;
	     char_data_index++) {
		if (g_object_table[first_slot + char_data_index].object_type !=
		    0) {
			*checksum_ptr ^= flight_checksum_buffer_rotate_xor(
				&g_mobile_object_char_data_pool
					[char_data_index],
				0x4C);
			checksum = flight_rotate_checksum_left(*checksum_ptr);
		}
	}

	for (mobile_object_index = 0;
	     mobile_object_index <
	     g_region_main_object_slot_end - g_local_debris_slot_count;
	     mobile_object_index++) {
		if (g_object_table[mobile_object_index].object_type != 0) {
			*checksum_ptr ^= flight_checksum_buffer_rotate_xor(
				&g_mobile_object_pool_base[mobile_object_index],
				0x8B);
			checksum = flight_rotate_checksum_left(*checksum_ptr);
		}
	}
	for (object_index = 0; object_index < g_region_main_object_slot_end -
						      g_local_debris_slot_count;
	     object_index++) {
		if (g_object_table[object_index].object_type != 0) {
			*checksum_ptr ^= flight_checksum_buffer_rotate_xor(
				&g_object_table[object_index], 0x1F);
			checksum = flight_rotate_checksum_left(*checksum_ptr);
		}
	}
	for (static_object_index = g_region_main_object_slot_end;
	     static_object_index <
	     g_region_main_object_slot_end + g_region_static_object_slot_count;
	     static_object_index++) {
		if (g_object_table[static_object_index].object_type != 0) {
			*checksum_ptr ^= flight_checksum_buffer_rotate_xor(
				&g_object_table[static_object_index], 0x1F);
			checksum = flight_rotate_checksum_left(*checksum_ptr);
		}
	}

	first_slot = g_object_slot_range_by_genus[0].start;
	for (craft_index = 0; craft_index < g_craft_data_pool_capacity;
	     craft_index++) {
		if (g_object_table[first_slot + craft_index].object_type != 0) {
			*checksum_ptr ^= flight_checksum_buffer_rotate_xor(
				&g_craft_data_pool_base[craft_index], 0x412);
			checksum = flight_rotate_checksum_left(*checksum_ptr);
		}
	}
	first_slot = g_object_slot_range_by_genus[6].start;
	for (projectile_index = 0;
	     projectile_index < (int)g_projectile_object_slots_total;
	     projectile_index++) {
		if (g_object_table[first_slot + projectile_index].object_type !=
		    0) {
			*checksum_ptr ^= flight_checksum_buffer_rotate_xor(
				&g_projectile_guidance_states[projectile_index],
				0xA);
			checksum = flight_rotate_checksum_left(*checksum_ptr);
		}
	}

	*checksum_ptr ^= flight_checksum_buffer_rotate_xor(
		&g_mission_elapsed_clock, sizeof(g_mission_elapsed_clock));
	checksum = flight_rotate_checksum_left(*checksum_ptr);
	*checksum_ptr ^= flight_checksum_buffer_rotate_xor(
		&g_mission_countdown_clock, sizeof(g_mission_countdown_clock));
	checksum = flight_rotate_checksum_left(*checksum_ptr);
	for (flight_group_index = 0;
	     flight_group_index < (int16_t)g_mission_header.num_flight_groups;
	     flight_group_index++) {
		*checksum_ptr ^= flight_checksum_buffer_rotate_xor(
			&g_mission_fg_stats[flight_group_index], 0x126);
		checksum = flight_rotate_checksum_left(*checksum_ptr);
	}

	*checksum_ptr ^= flight_checksum_buffer_rotate_xor(
		&g_flight_mission_state, 0xD30);
	checksum = flight_rotate_checksum_left(*checksum_ptr);
	*checksum_ptr ^= g_next_object_signature;
	checksum = flight_rotate_checksum_left(*checksum_ptr);
	*checksum_ptr ^= flight_checksum_buffer_rotate_xor(
		&g_flight_global_countdown_timers, 0x16);
	checksum = flight_rotate_checksum_left(*checksum_ptr);
	*checksum_ptr ^= (uint32_t)(int16_t)g_mission_file_version;
	checksum = flight_rotate_checksum_left(*checksum_ptr);
	*checksum_ptr ^=
		flight_checksum_buffer_rotate_xor(&g_mission_header, 0xA2);
	checksum = flight_rotate_checksum_left(*checksum_ptr);
	for (flight_group_index = 0;
	     flight_group_index < (int16_t)g_mission_header.num_flight_groups;
	     flight_group_index++) {
		*checksum_ptr ^= flight_checksum_buffer_rotate_xor(
			&g_mission_flight_groups[flight_group_index], 0x562);
		checksum = flight_rotate_checksum_left(*checksum_ptr);
	}
	*checksum_ptr ^= flight_checksum_buffer_rotate_xor(
		g_mission_messages, sizeof(g_mission_messages));
	checksum = flight_rotate_checksum_left(*checksum_ptr);
	for (goal_index = 0; goal_index < 10; goal_index++) {
		*checksum_ptr ^= flight_checksum_buffer_rotate_xor(
			g_mission_global_goals[goal_index],
			sizeof(g_mission_global_goals[0]));
		checksum = flight_rotate_checksum_left(*checksum_ptr);
	}

	*checksum_ptr ^= g_active_flight_player_count;
	checksum = flight_rotate_checksum_left(*checksum_ptr);
	*checksum_ptr ^= g_world_state_reserved_byte;
	checksum = flight_rotate_checksum_left(*checksum_ptr);
	*checksum_ptr ^= g_craft_data_pool_capacity;
	checksum = flight_rotate_checksum_left(*checksum_ptr);
	*checksum_ptr ^= g_mobile_object_char_data_count;
	checksum = flight_rotate_checksum_left(*checksum_ptr);
	*checksum_ptr ^= g_projectile_object_slots_total;
	checksum = flight_rotate_checksum_left(*checksum_ptr);
	*checksum_ptr ^= g_debris_object_slots_total;
	checksum = flight_rotate_checksum_left(*checksum_ptr);
	*checksum_ptr ^= g_world_state_debris_slot_count;
	checksum = flight_rotate_checksum_left(*checksum_ptr);
	*checksum_ptr ^= g_local_debris_slot_count;
	checksum = flight_rotate_checksum_left(*checksum_ptr);
	*checksum_ptr ^= g_active_region_object_slot_start;
	checksum = flight_rotate_checksum_left(*checksum_ptr);
	*checksum_ptr ^= g_active_region_craft_object_slot_end;
	checksum = flight_rotate_checksum_left(*checksum_ptr);
	*checksum_ptr ^= g_mobile_object_char_data_slot_start;
	checksum = flight_rotate_checksum_left(*checksum_ptr);
	*checksum_ptr ^= g_mobile_object_char_data_slot_end;
	checksum = flight_rotate_checksum_left(*checksum_ptr);
	*checksum_ptr ^= g_projectile_object_slot_start;
	checksum = flight_rotate_checksum_left(*checksum_ptr);
	*checksum_ptr ^= g_projectile_object_slot_end;
	checksum = flight_rotate_checksum_left(*checksum_ptr);
	*checksum_ptr ^= g_debris_object_slot_start;
	checksum = flight_rotate_checksum_left(*checksum_ptr);
	*checksum_ptr ^= g_debris_object_slot_end;
	checksum = flight_rotate_checksum_left(*checksum_ptr);
	*checksum_ptr ^= g_explosion_object_slot_start;
	checksum = flight_rotate_checksum_left(*checksum_ptr);
	*checksum_ptr ^= g_explosion_object_slot_end;
	checksum = flight_rotate_checksum_left(*checksum_ptr);
	*checksum_ptr ^= g_local_transient_slot_start;
	checksum = flight_rotate_checksum_left(*checksum_ptr);
	*checksum_ptr ^= g_local_debris_slot_end;
	checksum = flight_rotate_checksum_left(*checksum_ptr);
	*checksum_ptr ^= g_region_main_object_slot_end;
	checksum = flight_rotate_checksum_left(*checksum_ptr);
	*checksum_ptr ^= g_region_static_object_slot_count;
	checksum = flight_rotate_checksum_left(*checksum_ptr);
	*checksum_ptr ^=
		flight_checksum_buffer_rotate_xor(g_plan_table, 0x5500);
	checksum = flight_rotate_checksum_left(*checksum_ptr);
	*checksum_ptr ^= g_plan_count;
	checksum = flight_rotate_checksum_left(*checksum_ptr);
	*checksum_ptr ^=
		flight_checksum_buffer_rotate_xor(g_plan_order_data, 0x1FFFF);
	checksum = flight_rotate_checksum_left(*checksum_ptr);
	*checksum_ptr ^= g_unused_world_state_serialized_dword;
	checksum = flight_rotate_checksum_left(*checksum_ptr);
	*checksum_ptr ^= (uint16_t)g_game_rand_feedback_state;
	checksum = flight_rotate_checksum_left(*checksum_ptr);
	*checksum_ptr ^= g_flight_conf_new_net;
	checksum = flight_rotate_checksum_left(*checksum_ptr);

	for (player_index = 0; player_index < 8; player_index++) {
		if (g_players[player_index].participation_state != 0) {
			*checksum_ptr ^= flight_checksum_buffer_rotate_xor(
				&g_players[player_index], 0x5BD);
			checksum = flight_rotate_checksum_left(*checksum_ptr);
		}
	}
	return (int)checksum;
#endif
}

/* Returns a checksum of size bytes at data: each 4-byte word, then a 2-byte and
 * a 1-byte tail, XORed into the sum, which is rotated left 1 bit after each.
 * Returns 0 for size 0. */
// FUNCTION: XVT 0x4630C0
unsigned int flight_checksum_buffer_rotate_xor(const void *data,
					       unsigned int size)
{
	const uint8_t *cursor;
	uint32_t checksum;
	uint32_t *checksum_ptr;
#ifdef XVT_MODERN
	uint32_t word;
	uint32_t tail_value;
#endif
	unsigned int word_count;

	cursor = (const uint8_t *)data;
	checksum = 0;
	checksum_ptr = &checksum;
	if (size >= sizeof(uint32_t)) {
		word_count = size / sizeof(uint32_t);
		size -= word_count * sizeof(uint32_t);
		do {
#ifdef XVT_MODERN
			memcpy(&word, cursor, sizeof(word));
			*checksum_ptr ^= word;
#else
			*checksum_ptr ^= *(const uint32_t *)cursor;
#endif
			*checksum_ptr =
				(*checksum_ptr << 1) | (*checksum_ptr >> 31);
			cursor += sizeof(uint32_t);
		} while (--word_count != 0);
	}

	if (size >= sizeof(uint16_t)) {
#ifdef XVT_MODERN
		tail_value = 0;
		memcpy(&tail_value, cursor, sizeof(uint16_t));
		*checksum_ptr ^= tail_value;
#else
		*checksum_ptr ^= *(const uint16_t *)cursor;
#endif
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
#ifdef XVT_MODERN
	xvt_flight_sim_update_player_step(player_idx);
#else
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

	if (g_flight_mission_state.mission_end_pending == 1) {
		return;
	}

	if (g_flight_sim_side_effects_suppressed == 0) {
		if (g_players[player_idx].view_state.camera_focus_obj_idx !=
			    UINT16_MAX &&
		    g_object_table[g_players[player_idx]
					   .view_state.camera_focus_obj_idx]
				    .object_type == 0) {
			if (g_players[player_idx].map_camera_state != 0) {
				g_players[player_idx]
					.view_state.camera_focus_obj_idx =
					UINT16_MAX;
			} else {
				g_players[player_idx]
					.view_state.target_camera_active = 0;
				g_players[player_idx]
					.view_state.external_camera_active = 0;
				g_players[player_idx]
					.view_state.player_input_blocked = 0;
				g_players[player_idx]
					.view_state.camera_focus_obj_idx =
					(uint16_t)g_players[player_idx]
						.object_index;
				hud_set_hud_view_state(HUD_VIEW_FORWARD,
						       player_idx);
				g_players[player_idx].view_state.hud_aim_x = 0;
				g_players[player_idx].view_state.hud_aim_y = 0;
			}
		}
		if (g_players[player_idx].map_camera_state != 0 &&
		    g_players[player_idx].view_state.aim_target_idx !=
			    UINT16_MAX &&
		    g_object_table[g_players[player_idx]
					   .view_state.aim_target_idx]
				    .object_type == 0) {
			g_players[player_idx].view_state.aim_target_idx =
				UINT16_MAX;
		}
	}

	flight_input_read(player_idx);
	flight_input_latch_flight_controls();
	if (player_idx == g_local_player) {
		if (g_flight_sim_side_effects_suppressed == 0) {
			switch (g_current_action_key) {
			case FLIGHT_KEY_SHIFT_L:
				if (g_radio_message_backup_enabled != 0) {
					g_radio_message_backup_enabled = 0;
					msg_write_message_log_file();
					msg_emit_in_flight_message(
						IFMSG_402_RADIO_MESSAGE_BACKUP_TURNED_OFF,
						player_idx);
				} else {
					g_radio_message_backup_enabled = 1;
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
						FLIGHT_SOUND_SMALL_CLICK, -1,
						player_idx);
				}
				break;
			case FLIGHT_KEY_ALT_D:
				++g_flight_graphics_detail_preset;
				if (g_flight_graphics_detail_preset >=
				    GRAPHICS_DETAIL_PRESET_COUNT) {
					g_flight_graphics_detail_preset = 0;
				}
				flight_apply_graphics_detail_preset(
					g_flight_graphics_detail_preset);
				msg_emit_in_flight_message(
					(in_flight_message_id)(g_flight_graphics_detail_preset +
							       GRAPHICS_DETAIL_MESSAGE_BASE),
					player_idx);
				fsfx_play_sound(FLIGHT_SOUND_SMALL_CLICK, -1,
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
				if (g_flight_player_count == 1) {
					g_input_timestamp +=
						time_consume_elapsed_ticks();
					fsfx_play_sound(
						FLIGHT_SOUND_SMALL_CLICK, -1,
						player_idx);
					msg_emit_in_flight_message(
						IFMSG_001_MISSION_PAUSED_PRESS_ANY_KEY_TO_CONTINUE,
						player_idx);
					g_flight_draw_to_hud_layer = 0;
					flight_surface_lock();
					hud_blit_software_hud_text_panes();
					flight_surface_unlock();
					flight_display_flip();
					g_flight_draw_to_hud_layer = 1;
					sound_stop_all_instances();
					while (flight_input_read(
						       FLIGHT_INPUT_WAIT_FOR_ANY_KEY) ==
					       0) {
					}
					time_consume_elapsed_ticks();
					msg_emit_in_flight_message(
						IFMSG_002_MISSION_RESUMED,
						player_idx);
					g_action_key = 0;
					g_flight_display_rebuild_pending = 0;
					flight_reset_unused_resume_slots();
				}
				break;
			case FLIGHT_KEY_ALT_S:
				if (g_system_message_display_enabled != 0) {
					g_system_message_display_enabled = 0;
					msg_emit_in_flight_message(
						IFMSG_400_SYSTEM_MESSAGE_DISPLAYING_TURNED_OFF,
						player_idx);
				} else {
					g_system_message_display_enabled = 1;
					msg_emit_in_flight_message(
						IFMSG_401_SYSTEM_MESSAGE_DISPLAYING_TURNED_ON,
						player_idx);
				}
				break;
			case FLIGHT_KEY_ALT_V:
				fsfx_play_sound(FLIGHT_SOUND_SMALL_CLICK, -1,
						player_idx);
				msg_emit_in_flight_message(
					IFMSG_000_X_WING_VS_TIE_FIGHTER_VER_1_10_05_11_97,
					player_idx);
				break;
			case FLIGHT_KEY_SCREENSHOT:
				fsfx_play_sound(FLIGHT_SOUND_SMALL_CLICK, -1,
						player_idx);
				flight_screenshot_capture();
				break;
			default:
				break;
			}
		}
	}

	if (g_flight_runtime_state_initialized > 1 &&
	    g_dormant_flight_region_session_early_return_flag != 0) {
		return;
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
		return;
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

	if (g_players[player_idx].chat_recipient_mode ==
	    FLIGHT_CHAT_RECIPIENT_INACTIVE) {
		flight_process_player_actions(player_idx);
	} else {
		flight_chat_handle_input(player_idx);
	}
	if (g_players[player_idx].participation_state != 0) {
		player_update_flight_controls_and_camera(player_idx);
	}
#endif
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
	const int standard_energy_transfer = 4;
	const int special_energy_transfer = 32;
	const int max_laser_charge = 127;
	const int max_transfer_iterations = 100;
	const int map_default_distance = 512;
	const int map_overview_distance = 0x40000;
	const object_type_id special_slam_craft_type = 12;
	int transfer_charge_units;
	unsigned int nearest_object_distance;
	int energy_transfer_step;
	int16_t departure_object_index;
	int object_index;
	struct craft_data *craft;
	object_index = g_players[player_idx].object_index;
	if (object_index != -1) {
		craft = g_object_table[object_index].mobj->p_craft;
	} else {
		craft = NULL;
	}

	if (g_players[player_idx].hyperspace_phase != 0) {
		if (g_players[player_idx].hyperspace_phase == 1 &&
		    g_current_action_key == FLIGHT_KEY_H) {
			g_players[player_idx].hyperspace_phase = 0;
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
			g_current_action_key = FLIGHT_KEY_NONE;
			break;
		default:
			break;
		}
	}

	if (g_players[player_idx].map_camera_state == 0) {
		switch (g_current_action_key) {
		case FLIGHT_KEY_BACKSPACE:
			craft->throttle_speed = UINT16_MAX;
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
					} else {
						craft->throttle_speed =
							UINT16_MAX;
					}
				}
				fsfx_play_sound(FLIGHT_SOUND_SMALL_CLICK, -1,
						player_idx);
			}
			break;
		case FLIGHT_KEY_QUOTES: {
			int max_shield;
			int shield_deficit;
			int16_t remaining_charge = 0;
			int slot_index;

			if ((craft->system_flags &
			     CRAFT_SUBSYSTEM_FLAG_SHIELDS) != 0) {
				if ((craft->working_subsystems &
				     CRAFT_SUBSYSTEM_FLAG_SHIELDS) != 0) {
					max_shield =
						craft_get_object_max_shield(
							(uint16_t)object_index);
					shield_deficit =
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
			int max_shield;
			int missing_shield_energy;
			int slot_index = 0;
			int16_t iteration = 0;

			if ((craft->system_flags &
			     CRAFT_SUBSYSTEM_FLAG_SHIELDS) != 0) {
				if ((craft->working_subsystems &
				     CRAFT_SUBSYSTEM_FLAG_SHIELDS) != 0) {
					max_shield =
						2 *
						g_model_defs
							[get_model_index_from_type(
								 g_object_table
									 [g_players[player_idx]
										  .object_index]
										 .object_type)]
								.shield_strength;
					missing_shield_energy =
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
			fsfx_play_sound(FLIGHT_SOUND_SMALL_CLICK, -1,
					player_idx);
			break;
		}
		case FLIGHT_KEY_SEMICOLON:
		case FLIGHT_KEY_SHIFT_F9: {
			int16_t empty_charge_units = 0;
			int16_t shield_energy_to_remove;
			int removed_shield_energy;
			int slot_index;
			int16_t iteration;

			if ((craft->system_flags &
			     CRAFT_SUBSYSTEM_FLAG_SHIELDS) != 0) {
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
				shield_energy_to_remove = energy_transfer_step *
							  empty_charge_units;

				removed_shield_energy = shield_energy_to_remove;
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
				if (transfer_charge_units != 0) {
					slot_index = 0;
					for (iteration = 0;
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
					struct ai_controller *controller;
					g_cur_craft =
						g_object_table[target_index]
							.mobj->p_craft;
					controller =
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
				int16_t other_player_idx;
				int attacker_index =
					player_find_attacker_of_target(
						(uint16_t)object_index,
						(int16_t)object_index);
				if (attacker_index != -1) {
					player_issue_ai_wingman_target_order(
						(uint16_t)attacker_index, 0x96,
						6, player_idx);
				}
				for (other_player_idx = 0; other_player_idx < 8;
				     ++other_player_idx) {
					int other_object_index;
					int hostile;
					struct craft_data *other_craft;
					if (other_player_idx == player_idx ||
					    g_players[other_player_idx]
							    .participation_state !=
						    1) {
						continue;
					}
					other_object_index =
						g_players[other_player_idx]
							.object_index;
					if (other_object_index ==
						    attacker_index ||
					    other_object_index == -1) {
						continue;
					}
					hostile =
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
					other_craft =
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
			fsfx_play_sound(FLIGHT_SOUND_SMALL_CLICK, -1,
					player_idx);
			msg_emit_in_flight_message(
				IFMSG_120_THROTTLE_SET_TO_1_3_POWER,
				player_idx);
			break;
		case FLIGHT_KEY_BACKSLASH:
			craft->throttle_speed = 0;
			fsfx_play_sound(FLIGHT_SOUND_SMALL_CLICK, -1,
					player_idx);
			msg_emit_in_flight_message(
				IFMSG_119_THROTTLE_SET_TO_NO_POWER, player_idx);
			break;
		case FLIGHT_KEY_RIGHT_BRACKET:
			craft->throttle_speed = (uint16_t)-21846;
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
								fsfx_play_sound(
									FLIGHT_SOUND_CHAFF_TRIGGER,
									-1,
									player_idx);
							} else if (
								laser_createcountermeasureprojectile(
									object_index,
									COUNTERMEASURE_PROJECTILE_OBJECT_TYPE) !=
								-1) {
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
			int16_t have_laser_energy = 0;
			unsigned int slot_index;
			if (get_model_index_from_type(
				    g_object_table[object_index].object_type) ==
			    get_model_index_from_type(
				    special_slam_craft_type)) {
				craft->engine_overdrive_off =
					(uint16_t)~craft->engine_overdrive_off;
				if (craft->engine_overdrive_off == 0) {
					for (slot_index = 0;
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
					if (player_idx == g_local_player) {
						msg_emit_in_flight_message(
							(in_flight_message_id)(link_mode +
									       4),
							player_idx);
					}
				}
			} else {
				uint16_t warhead_kind;
				craft->warhead_launcher_flags
					[g_players[player_idx]
						 .selected_weapon_bank] ^= 2;
				warhead_kind = object_type_get_warhead_kind_index(
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
			break;
		case FLIGHT_KEY_THROTTLE_6:
		case FLIGHT_KEY_THROTTLE_7:
		case FLIGHT_KEY_THROTTLE_8:
		case FLIGHT_KEY_THROTTLE_9:
		case FLIGHT_KEY_THROTTLE_10:
			craft->throttle_speed =
				(uint16_t)((g_current_action_key + 7) << 12);
			break;
		case FLIGHT_KEY_THROTTLE_11:
		case FLIGHT_KEY_THROTTLE_12:
		case FLIGHT_KEY_THROTTLE_13:
		case FLIGHT_KEY_THROTTLE_14:
			craft->throttle_speed =
				(uint16_t)((g_current_action_key + 8) << 12);
			break;
		default:
			break;
		}
	} else {
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
			break;
		case FLIGHT_KEY_PAD_MINUS:
			g_players[player_idx].view_state.aim_target_idx =
				UINT16_MAX;
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
			}
			break;
		default:
			break;
		}
	}

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
				departure_object_index =
					g_players[player_idx]
						.pending_action_param;
				if (departure_object_index == -1) {
					if (g_players[player_idx]
						    .participation_state == 1) {
						int connected_count = 0;
						int16_t other_player_idx;
						for (other_player_idx = 0;
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
						}
					} else {
						if (player_idx ==
						    g_local_player) {
							g_flight_mission_state
								.mission_end_pending =
								1;
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
			}
			break;
		case 7:
			if (g_players[player_idx].map_camera_state == 0) {
				craft->throttle_speed = 0;
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
			} else if (g_players[player_idx]
					   .view_state.external_camera_active !=
				   0) {
				g_players[player_idx]
					.view_state.player_input_blocked =
					g_players[player_idx]
						.view_state
						.player_input_blocked == 0;
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
			int16_t attempt;
			for (attempt = 0; attempt < mesh_count; ++attempt) {
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
			int16_t attempt;
			for (attempt = 0; attempt < mesh_count; ++attempt) {
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
			}
		}
		return;
	case FLIGHT_KEY_1:
	case FLIGHT_KEY_2:
	case FLIGHT_KEY_3:
	case FLIGHT_KEY_4: {
		int16_t dst_player_idx;
		int taunt_index = g_current_action_key - FLIGHT_KEY_1;
		msg_add_message_ptr(0, net_session_get_player_name(player_idx));
		msg_add_message_ptr(
			1, g_player_taunt_text[player_idx][taunt_index]);
		for (dst_player_idx = 0; dst_player_idx < 8; ++dst_player_idx) {
			if (g_players[dst_player_idx].participation_state !=
			    0) {
				g_msg_sender_iff = 3;
				msg_emit_in_flight_message(
					IFMSG_374_FROM_ARG_ARG, dst_player_idx);
			}
		}
		msg_emit_in_flight_message(IFMSG_378_MESSAGE_SENT, player_idx);
		return;
	}
	case FLIGHT_KEY_SHIFT_A: {
		int target_index =
			g_players[player_idx].current_target_object_idx;
		int16_t other_player_idx;
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
		player_issue_ai_wingman_target_order((uint16_t)target_index,
						     0x9A, 4, player_idx);
		for (other_player_idx = 0; other_player_idx < 8;
		     ++other_player_idx) {
			struct craft_data *other_craft;
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
			other_craft = g_object_table[g_players[other_player_idx]
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
		int16_t other_player_idx;
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
			struct ai_controller *controller;
			g_cur_craft =
				g_object_table[target_index].mobj->p_craft;
			controller = &g_cur_craft->ai_controller;
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
			msg_radio_message((uint16_t)target_index,
					  (uint8_t *)g_cur_craft, 0x97, 7, 0);
			return;
		}
		for (other_player_idx = 0; other_player_idx < 8;
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
		int16_t other_player_idx;
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
				msg_radio_message((uint16_t)target_index,
						  (uint8_t *)g_cur_craft, 0x99,
						  3, 0);
			}
			return;
		}
		for (other_player_idx = 0; other_player_idx < 8;
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
		int16_t other_player_idx;
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
			struct ai_controller *controller;
			g_cur_craft = target->mobj->p_craft;
			controller = &g_cur_craft->ai_controller;
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
			}
			msg_radio_message((uint16_t)target_index,
					  (uint8_t *)g_cur_craft, 0x95, 1, 0);
			return;
		}
		for (other_player_idx = 0; other_player_idx < 8;
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
		int16_t other_player_idx;
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
		player_issue_ai_wingman_target_order((uint16_t)target_index,
						     0x9B, 5, player_idx);
		for (other_player_idx = 0; other_player_idx < 8;
		     ++other_player_idx) {
			struct craft_data *other_craft;
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
			other_craft = g_object_table[g_players[other_player_idx]
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
			struct object_record *object;
			int owner;
			int team;
			int hostile;
			if (++candidate >=
			    g_active_region_craft_object_slot_end) {
				candidate = g_active_region_object_slot_start;
			}
			object = &g_object_table[candidate];
			if (object->object_type == 0 ||
			    object->genus_id == CRAFT_GENUS_EXPLOSION ||
			    object_has_active_decoy_beam((uint16_t)candidate) !=
				    0) {
				continue;
			}
			owner = object->player_owner_idx;
			if (owner == -1 || owner == player_idx) {
				continue;
			}
			team = g_mission_flight_groups[object->flight_group_idx]
				       .fg.team;
			hostile =
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
		int target_index;
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
		target_index = g_players[player_idx].current_target_object_idx;
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
					int16_t other_player_idx;
					for (other_player_idx = 0;
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
		uint16_t flight_group_idx;
		uint8_t reinforcement_available = 0;
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
		for (flight_group_idx = 0;
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
		int16_t other_player_idx;
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
			struct ai_controller *controller;
			g_cur_craft =
				g_object_table[target_index].mobj->p_craft;
			controller = &g_cur_craft->ai_controller;
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
				msg_radio_message((uint16_t)target_index,
						  (uint8_t *)g_cur_craft, 0x98,
						  2, 0);
			}
			return;
		}
		for (other_player_idx = 0; other_player_idx < 8;
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
		int16_t attacker = -1;
		if (g_players[player_idx].object_index == -1) {
			return;
		}
		while (remaining-- != 0) {
			struct object_record *object;
			struct craft_data *candidate_craft;
			if (++candidate >=
			    g_active_region_craft_object_slot_end) {
				candidate = g_active_region_object_slot_start;
			}
			object = &g_object_table[candidate];
			if (object->object_type == 0 ||
			    candidate == g_players[player_idx].object_index ||
			    object->genus_id == CRAFT_GENUS_EXPLOSION) {
				continue;
			}
			candidate_craft = object->mobj->p_craft;
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
		int projectile_index;
		int nearest_projectile = -1;
		nearest_object_distance = UINT32_MAX;
		for (projectile_index = g_projectile_object_slot_start;
		     projectile_index < g_projectile_object_slot_end;
		     ++projectile_index) {
			struct object_record *projectile =
				&g_object_table[projectile_index];
			struct warhead_guidance_state *guidance;
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
			guidance = projectile->mobj->p_warhead_guidance;
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
				struct warhead_guidance_state *guidance;
				struct mobile_object *target_mobile;
				int target_team;
				int player_team;
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
				guidance = projectile->mobj->p_warhead_guidance;
				if (guidance == NULL ||
				    guidance->target_obj_idx == UINT16_MAX) {
					continue;
				}
				target_mobile =
					g_object_table[guidance->target_obj_idx]
						.mobj;
				if (target_mobile == NULL ||
				    target_mobile->family != 0) {
					continue;
				}
				target_team = target_mobile->team;
				player_team =
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
					int16_t saved_map_camera_state;
					mission_process_flight_group_wave_completion(
						g_players[player_idx]
							.bound_flight_group_idx);
					saved_map_camera_state =
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
		int candidate;
		int remaining;
		int nearest_target = -1;
		nearest_object_distance = UINT32_MAX;
		if (g_flight_mission_state.locate_players_enabled == 0) {
			return;
		}
		candidate = g_players[player_idx].current_target_object_idx;
		remaining = g_active_region_craft_object_slot_end -
			    g_active_region_object_slot_start - 1;
		while (remaining-- >= 0) {
			struct object_record *object;
			int team;
			int hostile;
			if (++candidate >=
			    g_active_region_craft_object_slot_end) {
				candidate = g_active_region_object_slot_start;
			}
			object = &g_object_table[candidate];
			if (object->object_type == 0 ||
			    object->player_owner_idx == -1 ||
			    object->player_owner_idx == player_idx ||
			    object->genus_id == CRAFT_GENUS_EXPLOSION) {
				continue;
			}
			team = g_mission_flight_groups[object->flight_group_idx]
				       .fg.team;
			hostile =
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
		int candidate;
		int newest_target = -1;
		uint16_t newest_age = UINT16_MAX;
		for (candidate = g_active_region_object_slot_start;
		     candidate < g_active_region_craft_object_slot_end;
		     ++candidate) {
			struct object_record *object =
				&g_object_table[candidate];
			struct craft_data *candidate_craft;
			if (object->object_type == 0 ||
			    candidate == object_index ||
			    object->genus_id == CRAFT_GENUS_EXPLOSION) {
				continue;
			}
			candidate_craft = object->mobj->p_craft;
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
			fsfx_play_sound(FLIGHT_SOUND_TARGET_SELECTED, -1,
					player_idx);
		}
		return;
	case FLIGHT_KEY_LEFT:
	case FLIGHT_KEY_RIGHT: {
		int16_t page;
		int16_t page_found = 0;
		if (player_idx != g_local_player ||
		    g_flight_sim_side_effects_suppressed != 0) {
			return;
		}
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
	return (char)g_debris_enabled;
}

#ifndef XVT_MODERN
/* A placeholder: the original WinMain is not rebuilt, and this returns 0. Only
 * the original build has it, and no engine code calls it. */
// FUNCTION: XVT 0x4A9B80
int WinMain(void *hInstance, void *hPrevInstance, char *lpCmdLine, int nShowCmd)
{
	/* Original WinMain; the modern port never calls it (host shell owns the loop). */
	(void)hInstance;
	(void)hPrevInstance;
	(void)lpCmdLine;
	(void)nShowCmd;

	/* TODO: Reimplement WinMain @ 0x4A9B80. */
	return 0;
}
#endif

#ifndef XVT_MODERN
/* Runs a flight from the launch command line; returns 1 after it ends, 0 when
 * it cannot start (no command line, too few arguments, no network session, no
 * display or no sound). It loads the configuration; sets g_flight_conf_flicker by
 * whether flicker.txt exists, g_laser_fire_timestamp_tracking_enabled and
 * g_internet_play_enabled; reads the launch options found anywhere in the line
 * ("traincourse", "nopilot", "dinput", "sfx", "music", "voice", "tickcounter"
 * and "mipmaps", each with a "no" form, "inprogress", "newnet", "nolauncher",
 * "fullscreen" and "pageflip" with their "no" forms, '-' or "/+" at the start);
 * takes the main window; splits the line at spaces, '~' quoting text with
 * spaces, into the 7 g_flight_launch_args arguments; starts the network session;
 * takes the display, detail and lighting options for solo or multiplayer from
 * g_game_config; sets up the display (flight_display_init) and writes the mode it
 * got back into the configuration; starts DirectInput and sound (error 13 when
 * sound fails); then runs the mission named in the first argument
 * (flight_main_loop), shuts sound, input, network and 3D down, clears and
 * releases the surfaces and palette, and hands drawing back to the frontend
 * (g_flight_render_to_frontend 1). Only the original build has this function. */
// FUNCTION: XVT 0x4A9C00
int flight_main(char *mission_cmd_line)
{
	enum {
		BUILTIN_ARGUMENT_COUNT = 2,
		PARSED_ARGUMENT_COUNT = 7,
		REQUIRED_ARGUMENT_COUNT =
			BUILTIN_ARGUMENT_COUNT + PARSED_ARGUMENT_COUNT,
		BRIGHTNESS_CONFIG_OFFSET = 4,
		BRIGHTNESS_CONFIG_SHIFT = 6,
		BRIGHTNESS_SCALE_MIN = 256,
		BRIGHTNESS_SCALE_MAX = 704,
		STAR_GRID_DIVISOR_LOW_DENSITY = 4,
		STAR_GRID_DIVISOR_MEDIUM_DENSITY = 2,
		STAR_GRID_DIVISOR_HIGH_DENSITY = 1,
		LOD_CONFIG_OFFSET = 5,
		LOD_CONFIG_MAX_VALUE = 20,
		LOD_SCALE_INVERSION_NUMERATOR = 1,
		MIPMAPPING_DISABLED_VALUE = 19,
		DISPLAY_WIDTH_LOW = 320,
		DISPLAY_HEIGHT_LOW = 240,
		DISPLAY_WIDTH_MEDIUM = 512,
		DISPLAY_HEIGHT_MEDIUM = 384,
		DISPLAY_WIDTH_HIGH = 640,
		DISPLAY_HEIGHT_HIGH = 480,
		WINDOW_WIDTH_MEDIUM = 480,
		WINDOW_HEIGHT_MEDIUM = 360,
		DISPLAY_CONFIG_LOW = 0,
		DISPLAY_CONFIG_MEDIUM = 1,
		DISPLAY_CONFIG_HIGH = 2,
		PALETTED_BYTES_PER_PIXEL = 1,
		HIGH_COLOR_BYTES_PER_PIXEL = 2,
		DISPLAY_INIT_SOUND_ERROR = 13,
	};

	xvt_file *flicker_file;
	network_transport_type network_type;
	const char *connection_address;
	int argument_count;
	int argument_index;
	int command_line_offset;
	int quoted_argument;
	int brightness_limit;
	char *option_match;

	model_preview_free_resources();
	g_flight_render_to_frontend = 0;
	if (mission_cmd_line == NULL) {
		return 0;
	}

	config_load();
	flicker_file = FILE_RAW_OPEN("flicker.txt", "r");
	if (flicker_file != NULL) {
		FILE_RAW_CLOSE(flicker_file);
		g_flight_conf_flicker = 0;
	} else {
		g_flight_conf_flicker = 1;
	}

	g_laser_fire_timestamp_tracking_enabled = 1;
	g_internet_play_enabled = g_game_config.internet_play;
	option_match = strstr(mission_cmd_line, "traincourse");
	g_flight_conf_train_course = 1;
	if (option_match == NULL) {
		g_flight_conf_train_course = 0;
	}
	option_match = strstr(mission_cmd_line, "nopilot");
	g_flight_conf_no_pilot = 1;
	if (option_match == NULL) {
		g_flight_conf_no_pilot = 0;
	}
	if (strstr(mission_cmd_line, "nodinput") != NULL) {
		g_flight_conf_direct_input = 0;
	} else if (strstr(mission_cmd_line, "dinput") != NULL) {
		g_flight_conf_direct_input = 1;
	} else {
		g_flight_conf_direct_input = 1;
	}
	if (strstr(mission_cmd_line, "nosfx") != NULL) {
		g_flight_conf_sfx_enabled = 0;
	} else if (strstr(mission_cmd_line, "sfx") != NULL) {
		g_flight_conf_sfx_enabled = 1;
	} else {
		g_flight_conf_sfx_enabled = 1;
	}
	if (strstr(mission_cmd_line, "nomusic") != NULL) {
		g_flight_conf_music_enabled = 0;
	} else if (strstr(mission_cmd_line, "music") != NULL) {
		g_flight_conf_music_enabled = 1;
	} else {
		g_flight_conf_music_enabled = 1;
	}
	if (strstr(mission_cmd_line, "novoice") != NULL) {
		g_flight_conf_voice_enabled = 0;
	} else if (strstr(mission_cmd_line, "voice") != NULL) {
		g_flight_conf_voice_enabled = 1;
	} else {
		g_flight_conf_voice_enabled = 1;
	}
	if (strstr(mission_cmd_line, "notickcounter") != NULL) {
		g_flight_conf_tick_counter_enabled = 0;
	} else if (strstr(mission_cmd_line, "tickcounter") != NULL) {
		g_flight_conf_tick_counter_enabled = 1;
	} else {
		g_flight_conf_tick_counter_enabled = 0;
	}
	if (strstr(mission_cmd_line, "nomipmaps") != NULL) {
		g_mipmapping_enabled = 0;
	} else if (strstr(mission_cmd_line, "mipmaps") != NULL) {
		g_mipmapping_enabled = 1;
	} else {
		g_mipmapping_enabled = 1;
	}
	option_match = strstr(mission_cmd_line, "inprogress");
	g_flight_in_progress_launch = 1;
	if (option_match == NULL) {
		g_flight_in_progress_launch = 0;
	}
	option_match = strstr(mission_cmd_line, "newnet");
	g_flight_conf_new_net = 1;
	if (option_match == NULL) {
		g_flight_conf_new_net = 0;
	}
	option_match = strstr(mission_cmd_line, "nolauncher");
	g_flight_conf_no_launcher = 1;
	if (option_match == NULL) {
		g_flight_conf_no_launcher = 0;
	}
	if (strstr(mission_cmd_line, "nofullscreen") != NULL) {
		g_flight_fullscreen = 0;
	} else if (strstr(mission_cmd_line, "fullscreen") != NULL) {
		g_flight_fullscreen = 1;
	}
	if (strstr(mission_cmd_line, "nopageflip") != NULL) {
		g_flight_page_flip = 0;
	} else if (strstr(mission_cmd_line, "pageflip") != NULL) {
		g_flight_page_flip = 1;
	}
	if (mission_cmd_line[0] == '-') {
		g_flight_started_with_dash_arg = 1;
	} else if (mission_cmd_line[0] == '/' && mission_cmd_line[1] == '+') {
		g_unused_flight_cmd_line_plus_switch_flag = 1;
	}

	if (flight_update_and_focus_main_window() == 0) {
		return 0;
	}

	command_line_offset = 0;
	quoted_argument = 0;
	argument_count = BUILTIN_ARGUMENT_COUNT;
	g_flight_launch_args.program_name = "xtie";
	g_flight_launch_args.sentinel = "/trebla";
	if (mission_cmd_line[0] != '\0') {
		for (argument_index = 0; argument_index < PARSED_ARGUMENT_COUNT;
		     ++argument_index) {
			g_flight_launch_args.arguments[argument_index] =
				&mission_cmd_line[command_line_offset];
			while (1) {
				char character;

				character =
					mission_cmd_line[command_line_offset];
				if (character == ' ') {
					if (quoted_argument != 1) {
						break;
					}
				} else if (character == '\0') {
					break;
				}
				if (character == '~') {
					if (quoted_argument != 0) {
						quoted_argument = 0;
						mission_cmd_line
							[command_line_offset] =
								'\0';
						++command_line_offset;
					} else {
						quoted_argument = 1;
						++command_line_offset;
						g_flight_launch_args.arguments
							[argument_index] =
							&mission_cmd_line
								[command_line_offset];
					}
				} else {
					++command_line_offset;
				}
			}
			++argument_count;
			if (mission_cmd_line[command_line_offset] == '\0') {
				break;
			}
			mission_cmd_line[command_line_offset] = '\0';
			++command_line_offset;
			if (mission_cmd_line[command_line_offset] == '\0') {
				break;
			}
		}
	}
	if (argument_count < REQUIRED_ARGUMENT_COUNT) {
		return 0;
	}

	network_type = (network_transport_type)g_game_config.network_type;
	switch (network_type) {
	case NET_TRANSPORT_TCPIP:
		connection_address = g_game_config.ip_address;
		break;
	case NET_TRANSPORT_MODEM:
		connection_address = g_game_config.phone_number;
		break;
	default:
	case NET_TRANSPORT_IPX:
		connection_address = NULL;
		break;
	}
	if (net_session_init_game_session(
		    g_flight_launch_args
			    .arguments[FLIGHT_LAUNCH_ARG_FORMAL_NAME],
		    g_flight_launch_args
			    .arguments[FLIGHT_LAUNCH_ARG_PILOT_NAME],
		    atoi(g_flight_launch_args
				 .arguments[FLIGHT_LAUNCH_ARG_IS_HOST]),
		    g_flight_launch_args
			    .arguments[FLIGHT_LAUNCH_ARG_MP_GAME_NAME],
		    network_type,
		    atoi(g_flight_launch_args
				 .arguments[FLIGHT_LAUNCH_ARG_NUM_PLAYERS]),
		    g_flight_in_progress_launch, connection_address) == 0) {
		net_session_shutdown();
		return 0;
	}

	g_flight_brightness_scale_q8 =
		(g_game_config.brightness[net_session_get_player_count() > 1] +
		 BRIGHTNESS_CONFIG_OFFSET)
		<< BRIGHTNESS_CONFIG_SHIFT;
	brightness_limit = BRIGHTNESS_SCALE_MIN;
	if ((unsigned int)g_flight_brightness_scale_q8 < BRIGHTNESS_SCALE_MIN) {
		g_flight_brightness_scale_q8 = brightness_limit;
	} else {
		brightness_limit = BRIGHTNESS_SCALE_MAX;
		if ((unsigned int)g_flight_brightness_scale_q8 >
		    BRIGHTNESS_SCALE_MAX) {
			g_flight_brightness_scale_q8 = brightness_limit;
		}
	}
	g_backdrops_enabled =
		g_game_config.backdrop[net_session_get_player_count() > 1];
	g_debris_enabled =
		g_game_config.debris[net_session_get_player_count() > 1];
	switch (g_game_config
			.star_density[net_session_get_player_count() > 1]) {
	case 0:
		g_star_grid_divisor = STAR_GRID_DIVISOR_LOW_DENSITY;
		break;
	case 1:
		g_star_grid_divisor = STAR_GRID_DIVISOR_MEDIUM_DENSITY;
		break;
	case 2:
		g_star_grid_divisor = STAR_GRID_DIVISOR_HIGH_DENSITY;
		break;
	default:
		break;
	}
	g_use_hardware3d =
		g_game_config
			.use3d_hardware[net_session_get_player_count() > 1];
	g_bilinear_enabled =
		g_game_config.bilinear[net_session_get_player_count() > 1];
	{
		int bpp_config_value;

		bpp_config_value = g_game_config.color_depth_choice
					   [net_session_get_player_count() > 1];
		switch (bpp_config_value) {
		case DISPLAY_CONFIG_LOW:
			g_flight_bytes_per_pixel = PALETTED_BYTES_PER_PIXEL;
			break;
		case DISPLAY_CONFIG_MEDIUM:
			g_flight_bytes_per_pixel = HIGH_COLOR_BYTES_PER_PIXEL;
			break;
		default:
			g_flight_bytes_per_pixel = PALETTED_BYTES_PER_PIXEL;
			break;
		}
	}
	net_session_get_player_count();
	{
		int lod_config_value;

		lod_config_value =
			g_game_config.lod[net_session_get_player_count() > 1] +
			LOD_CONFIG_OFFSET;
		g_lod_distance_scale = (float)lod_config_value;
		if (g_lod_distance_scale > g_lod_config_max_value) {
			g_lod_distance_scale = (float)LOD_CONFIG_MAX_VALUE;
		}
		g_lod_distance_scale =
			g_lod_distance_scale * g_lod_config_scale_factor;
		g_lod_distance_scale =
			g_lod_distance_scale * g_lod_config_curve_double;
		if (g_lod_distance_scale > g_lod_config_curve_threshold) {
			g_lod_distance_scale = g_lod_config_curve_threshold /
					       (g_lod_config_curve_double -
						g_lod_distance_scale);
		}
		g_forced_lod_level = 0;
		g_lod_distance_scale = (float)LOD_SCALE_INVERSION_NUMERATOR /
				       g_lod_distance_scale;
	}
	{
		int mipmap_config_option;

		mipmap_config_option =
			g_game_config
				.mipmap[net_session_get_player_count() > 1];
		if (mipmap_config_option != MIPMAPPING_DISABLED_VALUE) {
			int64_t mipmap_config_value;

			mipmap_config_value =
				g_game_config
					.mipmap[net_session_get_player_count() >
						1];
			g_mip_lod_scale = (float)mipmap_config_value;
			g_mip_lod_scale =
				g_mip_lod_scale * g_mipmap_config_scale_factor;
			g_mip_lod_scale =
				g_mip_lod_scale * g_lod_config_curve_double;
			if (g_mip_lod_scale > g_lod_config_curve_threshold) {
				g_mip_lod_scale = g_lod_config_curve_threshold /
						  (g_lod_config_curve_double -
						   g_mip_lod_scale);
			}
			g_mipmapping_enabled = 1;
			g_mip_lod_scale = (float)LOD_SCALE_INVERSION_NUMERATOR /
					  g_mip_lod_scale;
		} else {
			g_mipmapping_enabled = 0;
		}
	}
	switch (g_game_config.texture_res[net_session_get_player_count() > 1]) {
	case 0:
		g_texture_resolution_level = 0;
		break;
	case 1:
		g_texture_resolution_level = 1;
		break;
	default:
		g_texture_resolution_level = 2;
		break;
	}
	{
		int local_lights_enabled;

		local_lights_enabled =
			g_game_config
				.local_lights[net_session_get_player_count() >
					      1];
		g_local_lights_enabled = 1;
		if (local_lights_enabled == 0) {
			g_local_lights_enabled = 0;
		}
	}
	{
		int specular_enabled;

		specular_enabled =
			g_game_config
				.specular[net_session_get_player_count() > 1];
		g_specular_enabled = 1;
		if (specular_enabled == 0) {
			g_specular_enabled = 0;
		}
	}
	{
		int diffuse_lighting_enabled;

		diffuse_lighting_enabled =
			g_game_config
				.diffuse[net_session_get_player_count() > 1];
		g_dir_lighting_enabled = 1;
		if (diffuse_lighting_enabled == 0) {
			g_dir_lighting_enabled = 0;
		}
	}
	{
		int dithering_enabled;

		dithering_enabled =
			g_game_config
				.dither[net_session_get_player_count() > 1];
		g_dithering_enabled = 1;
		if (dithering_enabled == 0) {
			g_dithering_enabled = 0;
		}
	}
	switch (g_game_config.screen_res[net_session_get_player_count() > 1]) {
	case DISPLAY_CONFIG_LOW:
		g_display_mode_width = DISPLAY_WIDTH_LOW;
		g_display_mode_height = DISPLAY_HEIGHT_LOW;
		break;
	case DISPLAY_CONFIG_MEDIUM:
		g_display_mode_width = DISPLAY_WIDTH_MEDIUM;
		g_display_mode_height = DISPLAY_HEIGHT_MEDIUM;
		break;
	default:
		g_display_mode_width = DISPLAY_WIDTH_HIGH;
		g_display_mode_height = DISPLAY_HEIGHT_HIGH;
		break;
	}
	switch (g_game_config.window_size[net_session_get_player_count() > 1]) {
	case DISPLAY_CONFIG_LOW:
		g_surface_width = DISPLAY_WIDTH_LOW;
		g_surface_height = DISPLAY_HEIGHT_LOW;
		break;
	case DISPLAY_CONFIG_MEDIUM:
		g_surface_width = WINDOW_WIDTH_MEDIUM;
		g_surface_height = WINDOW_HEIGHT_MEDIUM;
		break;
	default:
		g_surface_width = DISPLAY_WIDTH_HIGH;
		g_surface_height = DISPLAY_HEIGHT_HIGH;
		break;
	}
	g_render_target_width = g_display_mode_width;
	g_requested_flight_bytes_per_pixel = g_flight_bytes_per_pixel;
	g_requested_flight_hardware3d = g_use_hardware3d;
	if (flight_display_init() == 0) {
		return 0;
	}

	switch (g_display_mode_width) {
	case DISPLAY_WIDTH_LOW:
		g_game_config.screen_res[net_session_get_player_count() > 1] =
			DISPLAY_CONFIG_LOW;
		break;
	case DISPLAY_WIDTH_MEDIUM:
		g_game_config.screen_res[net_session_get_player_count() > 1] =
			DISPLAY_CONFIG_MEDIUM;
		break;
	case DISPLAY_WIDTH_HIGH:
		g_game_config.screen_res[net_session_get_player_count() > 1] =
			DISPLAY_CONFIG_HIGH;
		break;
	default:
		break;
	}
	switch (g_flight_bytes_per_pixel) {
	case PALETTED_BYTES_PER_PIXEL:
		g_game_config
			.color_depth_choice[net_session_get_player_count() >
					    1] = DISPLAY_CONFIG_LOW;
		break;
	case HIGH_COLOR_BYTES_PER_PIXEL:
		g_game_config
			.color_depth_choice[net_session_get_player_count() >
					    1] = DISPLAY_CONFIG_MEDIUM;
		break;
	default:
		break;
	}
	g_game_config.use3d_hardware[net_session_get_player_count() > 1] =
		(uint8_t)g_use_hardware3d;
	debug_printf("Init Dinput\n");
	if (g_flight_conf_direct_input != 0 && dinput_init() == 0) {
		g_flight_conf_direct_input = 0;
	}
	debug_printf("Init Dsound\n");
	g_flight_sound_init_start_time_ms = timeGetTime();
	if (sound_init_sound_engine(g_flight_main_window_handle) == 0) {
		flight_display_cleanup_and_report_error(
			DISPLAY_INIT_SOUND_ERROR);
		return 0;
	}

	strcpy(g_current_mission_file,
	       g_flight_launch_args.arguments[FLIGHT_LAUNCH_ARG_MISSION_PATH]);
	flight_main_loop(0);
	g_sw3d_skip_odd_scanlines = 0;
	sound_shutdown_sound_engine();
	if (g_flight_conf_direct_input != 0) {
		dinput_shutdown();
	}
	net_session_shutdown();
	if (g_use_hardware3d != 0) {
		std3d_detach_and_release_z_buffer_surface();
		std3d_close();
		std3d_shutdown();
	}
	if (g_flight_fullscreen != 0) {
		flight_display_clear_surface(g_flight_primary_surface);
		if (g_flight_page_flip != 0) {
			flight_display_clear_surface(g_flight_back_buffer);
			flight_display_clear_surface(
				g_flight_offscreen_surface);
		}
	}
	if (g_flight_primary_surface != NULL) {
		g_flight_primary_surface->lpVtbl->Release(
			g_flight_primary_surface);
		g_flight_primary_surface = NULL;
	}
	if (g_flight_palette != NULL) {
		g_flight_palette->lpVtbl->Release(g_flight_palette);
		g_flight_palette = NULL;
	}
	if (g_flight_page_flip != 0 && g_flight_offscreen_surface != NULL) {
		g_flight_offscreen_surface->lpVtbl->Release(
			g_flight_offscreen_surface);
		g_flight_offscreen_surface = NULL;
	}
	g_flight_render_to_frontend = 1;
	g_use_hardware3d = 0;
	return 1;
}
#endif

/* Sets g_flight_main_window_handle to the frontend's main window and returns 1;
 * the original build also updates the window and gives it the focus. */
// FUNCTION: XVT 0x4AA6F0
int flight_update_and_focus_main_window(void)
{
#ifdef XVT_MODERN
	g_flight_main_window_handle = frontend_display_get_main_window_handle();
#else
	UpdateWindow(g_flight_main_window_handle =
			     frontend_display_get_main_window_handle());
	SetFocus(g_flight_main_window_handle);
#endif
	return 1;
}

/* In the original build, brings the main window to the front when it is not
 * there (resetting an 8-bit palette, flight_palette_reset_if8_bit, and hiding the
 * cursor), then takes one waiting window message and dispatches it unless it is
 * 0x06, 0x08, 0x1C, 0x1F or 0x86. Returns the dispatch's result, the message
 * number when not dispatched, or 0 with no message. The modern build returns
 * 0. */
// FUNCTION: XVT 0x4AA720
int32_t flight_pump_window_messages(void)
{
#ifdef XVT_MODERN
	return 0;
#else
	struct flight_win32_message message;
	int32_t result;

	if (GetForegroundWindow() != g_flight_main_window_handle) {
		SetForegroundWindow(g_flight_main_window_handle);
		flight_palette_reset_if8_bit();
		while (ShowCursor(0) >= 0) {
		}
	}

	result = PeekMessageA(&message, NULL, 0, 0, 1);
	if (result != 0) {
		result = (int32_t)message.message;
		if (message.message != 0x1C && message.message != 0x08 &&
		    message.message != 0x06 && message.message != 0x1F &&
		    message.message != 0x86) {
			TranslateMessage(&message);
			result = DispatchMessageA(&message);
		}
	}
	return result;
#endif
}

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
#ifndef XVT_MODERN
	if (Msg == 0x0f) {
		return DefWindowProcA(hWnd, Msg, wParam, lParam);
	}
#else
	(void)hWnd;
	(void)wParam;
	(void)lParam;
#endif
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
#ifdef XVT_MODERN
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
#endif
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
#ifdef XVT_MODERN
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
#endif
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
#ifdef XVT_MODERN
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
#endif
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
						}
					} else {
						g_cur_craft->pitch =
							controller
								->target_z_angle;
						g_cur_craft->ai_flight
							.pitch_state = 3;
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
						}
					} else {
						g_cur_craft->pitch =
							controller
								->target_z_angle;
						g_cur_craft->ai_flight
							.pitch_state = 3;
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
#ifdef XVT_MODERN
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
#endif
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
#ifdef XVT_MODERN
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
#endif
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

#ifdef XVT_MODERN
		if (xvt_flight_timing_reference_due()) {
			struct xvt_flight_clock decision_clock =
				xvt_flight_timing_enter_reference();
#endif
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

#ifdef XVT_MODERN
			xvt_flight_timing_restore_clock(decision_clock);
		}
#endif

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
	uint32_t speed_delta;

	speed_delta = target_speed;
	speed_delta -= g_object_table[object_idx].mobj->speed;
	if (speed_delta == 0) {
		return;
	}

	if (speed_delta < 0x8000u) {
		uint32_t step;

		step = (uint16_t)math2_fraction(
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
		uint32_t step;

		step = (uint16_t)math2_fraction(
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
	uint32_t product;
	uint32_t whole_quotient;
	uint16_t whole_delta;
	uint16_t frac_delta;
	uint16_t *speed_remainder;
	uint16_t old_remainder;

	product = (uint32_t)(uint16_t)g_elapsed_ticks *
		  (uint32_t)acceleration_per_second;
	whole_quotient = product / 236u;
	whole_delta = (uint16_t)whole_quotient;
	frac_delta =
		(uint16_t)(((product - whole_quotient * 236u) << 16) / 236u);

	speed_remainder = &g_object_table[object_idx].mobj->speed_remainder;
	old_remainder = *speed_remainder;
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
	uint32_t product;
	uint32_t whole_quotient;
	uint16_t whole_delta;
	uint16_t frac_delta;
	uint16_t *speed_remainder;
	uint16_t old_remainder;

	product = (uint32_t)(uint16_t)g_elapsed_ticks *
		  (uint32_t)deceleration_per_second;
	whole_quotient = product / 236u;
	whole_delta = (uint16_t)whole_quotient;
	frac_delta =
		(uint16_t)(((product - whole_quotient * 236u) << 16) / 236u);

	speed_remainder = &g_object_table[object_idx].mobj->speed_remainder;
	old_remainder = *speed_remainder;
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
	struct ai_controller *controller;
	int altitude_delta;
	struct object_record *object;
	int move_z;
	int projected_movement;

	controller = &g_cur_craft->ai_controller;
	object = &g_object_table[object_idx];
	altitude_delta = object->world_z - controller->aim_point_z;
	if (altitude_delta < 0 || altitude_delta <= 0x100) {
		g_cur_craft->pitch = 0x4000;
		g_cur_craft->ai_flight.pitch_state = 0;
		g_cur_craft->ai_flight.dive_state = 2;
	} else {
		if (object->mobj->move_vector_dirty != 0) {
			fview_calcrotatemove(object->pitch, object->yaw,
					     object);
		}
		move_z = object->mobj->move_z;
		projected_movement = move_z;
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
