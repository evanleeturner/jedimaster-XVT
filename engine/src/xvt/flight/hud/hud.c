#include "xvt/flight/hud/hud.h"
#ifdef XVT_MODERN
#include "xvt_runtime/snapshot/cockpit_capture.h"
#include "xvt_runtime/snapshot/cockpit_instruments.h"
#include "xvt_runtime/snapshot/cockpit_messages.h"
#include "xvt_runtime/snapshot/cockpit_pages.h"
#include "xvt_runtime/snapshot/cockpit_readouts.h"
#include "xvt_runtime/snapshot/cockpit_text.h"
#include "xvt_runtime/snapshot/render_hud.h"
#endif

#ifdef XVT_MODERN
#include "xvt_runtime/snapshot/render_assets.h"
#include "xvt_runtime/snapshot/render_capture.h"
#endif
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "xvt/assets/file.h"
#include "xvt/assets/model_mesh.h"
#include "xvt/assets/opt_model.h"
#include "xvt/audio/fsfx.h"
#include "xvt/flight/ai/pai.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/fediskio.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/flight_display.h"
#include "xvt/flight/flight_render.h"
#include "xvt/flight/flight_surface.h"
#include "xvt/flight/flight_view.h"
#include "xvt/flight/fview.h"
#include "xvt/flight/hud/flight_map.h"
#include "xvt/flight/hud/flight_text.h"
#include "xvt/flight/hud/mfd.h"
#include "xvt/flight/hud/msg.h"
#include "xvt/flight/mission/goals.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/collide.h"
#include "xvt/flight/object/damage.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/flight/proving_grounds.h"
#include "xvt/flight/targeting.h"
#include "xvt/flight/transfm2.h"
#include "xvt/frontend/config.h"
#include "xvt/math/math.h"
#include "xvt/math/math2.h"
#include "xvt/math/trig2.h"
#include "xvt/net/net_session.h"
#include "xvt/render/flight_light.h"
#include "xvt/render/flight_palette.h"
#include "xvt/render/flight_sw.h"
#include "xvt/render/render_scene.h"
#include "xvt/render/renderer.h"
#include "xvt/render/scene_billboard.h"
#include "xvt/render/std3d.h"
#include "xvt/render/sw3d.h"
#include "xvt/util/memory.h"
#include "xvt_runtime/log/log_both_builds.h"
#ifndef XVT_MODERN
int(_fileno)(xvt_file *stream);
long _filelength(int file_descriptor);
#endif

#ifndef XVT_MODERN
struct msvc42_crt_file_prefix {
	/* Never read or written; puts flags at byte 12. */
	uint8_t reserved[12];
	/* Stream flags; the original build's loaders read bit 0x10 as end of
	 * file. */
	int flags;
};
#endif

struct lfd_entry_header {
	uint8_t resource_type[4]; /* Type tag; PLTT marks a palette. */
	char resource_name[8];	  /* Read from the file; nothing uses it. */
	uint32_t data_size;	  /* Bytes of data after the header. */
};

enum { PANEL_BOX_SPAN_SCRATCH_SIZE = 2048 };

/* Pixels of one corner stroke in the box color, which
 * hud_draw_depth_tested_box_corners, its only user, fills from the box's left x
 * when that is positive (twice that many bytes in at 16 bits) and draws
 * from. */
// GLOBAL: XVT 0x6122D8
static uint8_t g_panel_box_span_scratch[PANEL_BOX_SPAN_SCRATCH_SIZE] = {0};
/* 1 after the targeting computer sees a new target, telling the next
 * hud_update3d_crt of the target inset to copy the inset's span mask again.
 * hud_update_targeting_computer_display sets it to 0 each time it draws and to 1
 * on a target change; hud_draw_hud_target_inset_if_enabled passes it on. */
// GLOBAL: XVT 0x521550
static uint16_t g_hud_target_inset_mask_refresh_pending = 0;

/* Memory handle of the panel sprite data, HUD_PANEL_SPRITE_BUFFER_BYTES
 * (120,000) long, allocated by fe_disk_io_init_global_buffers and freed and set to
 * 0 by fe_disk_io_free_flight_resources; the modern build's xvt_flight_loading_reset
 * also sets it to 0. hud_rebuild_display_for_view_state starts
 * g_hud_panel_sprite_data_write_cursor at its memory. */
// GLOBAL: XVT 0x9D8A50
uint16_t g_hud_panel_sprite_data_handle = 0;
/* Memory handle of the message log, MESSAGE_LOG_BUFFER_BYTES (32,000) long,
 * allocated by fe_disk_io_init_global_buffers and freed and set to 0 by
 * fe_disk_io_free_flight_resources; the modern build's xvt_flight_loading_reset also
 * sets it to 0. msg_emit_in_flight_message and mfd_draw_message_log_page lock it into
 * g_message_log_records. */
// GLOBAL: XVT 0x9EC5FC
uint16_t g_message_log_handle = 0;
/* Memory handle of the flight icon frames and their pointers, allocated by
 * fe_disk_io_init_global_buffers, which also locks it and loads the frames, and
 * freed and set to 0 by fe_disk_io_free_flight_resources; the modern build's
 * xvt_flight_loading_reset also sets it to 0. */
// GLOBAL: XVT 0xA07CCC
uint16_t g_flight_icon_frames_handle = 0;
/* Per view, the memory handle and the three LFD entries (cockpit image,
 * viewport span mask, palette) of its cockpit image.
 * hud_load_cockpit_sprite_resources fills the views whose descriptor has
 * resource_ref 1 and sets the others' handle to 0;
 * hud_rebuild_display_for_view_state loads a view's entries into
 * g_flight_scratch_screen_buffer when they are not loaded.
 * fe_disk_io_free_flight_resources frees the handles. */
// GLOBAL: XVT 0xA08A10
struct hud_cockpit_resource g_hud_cockpit_resources[28] = {{0}};
/* Per view (hud_view_state), the cockpit image, viewport and name read by
 * hud_load_cockpit_interface_file from the cockpit's .INT file, its only
 * writer. */
// GLOBAL: XVT 0xA08610
struct hud_cockpit_resource_descriptor g_hud_cockpit_resource_descriptors[28] =
	{{0}};
/* Path of the last cockpit file opened: the .INT files and the .LFD files.
 * Written by hud_load_cockpit_interface_file,
 * hud_load_auxiliary_cockpit_interface_file, hud_load_cockpit_lfd_entries and
 * hud_load_cockpit_sprite_resources with strcpy and strcat, which do not check its
 * 32 bytes. */
// GLOBAL: XVT 0xA08BD0
char g_hud_cockpit_resource_path[32] = {0};
/* Base path of the cockpit or panel file being loaded: the resolution's cockpit
 * folder and a name, with ".PNL" when hud_rebuild_display_for_view_state reads
 * panel sprites. Written by hud_load_cockpit_resources and
 * hud_rebuild_display_for_view_state with strcpy and strcat, which do not check its
 * 32 bytes. */
// GLOBAL: XVT 0xA08310
char g_hud_cockpit_base_path[32] = {0};
/* Panel sprite file name and sprite counts read from the cockpit's .INT file by
 * hud_load_cockpit_interface_file, its only writer. */
// GLOBAL: XVT 0xA08BC0
struct hud_panel_sprite_file_info g_hud_panel_sprite_file_info = {{0}, 0, 0};
/* Target the targeting computer last drew, an object index, or -1 for none.
 * hud_update_targeting_computer_display and hud_draw_cmd_target_details record the
 * target; hud_init_hud and hud_update_craft_system_status_indicators set -1;
 * flight_process_player_actions and player_validate_current_targets set -2, and
 * collide_collisions and paiman_boardmaneuver -3, to force a redraw. After any
 * of -1 to -3 the targeting computer also redraws its labels. */
// GLOBAL: XVT 0xA08C74
int16_t g_hud_cached_target_object_idx = 0;
/* Panel sprite set the cockpit wants; set to 0 by hud_load_cockpit_resources and
 * hud_reload_cockpit_interface_file, and nothing sets another value.
 * hud_rebuild_display_for_view_state reloads the panel sprites while it differs
 * from g_hud_loaded_panel_set_id. */
// GLOBAL: XVT 0xA0813E
uint8_t g_hud_panel_set_id = 0;
/* Where hud_load_cockpit_lfd_entries writes the next LFD entry; it advances past
 * each one. Set to a view's memory by hud_load_cockpit_sprite_resources and to
 * g_flight_scratch_screen_buffer by hud_rebuild_display_for_view_state. */
// GLOBAL: XVT 0xA08370
uint8_t *g_hud_cockpit_resource_write_cursor = NULL;
/* 1 once hud_load_cockpit_sprite_resources has loaded the cockpit images. Set to 0
 * by fe_disk_io_init_global_buffers and at flight start (flight_main_loop in the
 * original build, xvt_flight_loading_globals in the modern one);
 * mission_init_flight_runtime_state loads the cockpit while it is 0. */
// GLOBAL: XVT 0x9D8C10
int g_hud_cockpit_resources_loaded = 0;
/* Panel sprite set last loaded by hud_rebuild_display_for_view_state, which sets it
 * to g_hud_panel_set_id; mission_init_flight_runtime_state sets 0xFF so the next
 * rebuild reloads. */
// GLOBAL: XVT 0x9D113E
uint8_t g_hud_loaded_panel_set_id = 0;
/* Set to 1 by mission_init_flight_runtime_state and to 0 by
 * hud_rebuild_display_for_view_state and by flight_update_player_step in the original
 * build or xvt_flight_sim_resume in the modern one. Nothing reads it. */
// GLOBAL: XVT 0xA00730
uint8_t g_flight_display_rebuild_pending = 0;
/* Names of the waypoints, by waypoint index, filled by
 * string_table_load_game_strings; hud_format_object_display_name reads them for
 * references of 0x8000 and up. */
// GLOBAL: XVT 0xA08380
const char *g_str_waypoint_names[14] = {0};
/* Names of target components by mesh type, filled by
 * string_table_load_game_strings; entry 32 (MESH_COMPONENT_32_DASHES) is the
 * dashes shown for no object. */
// GLOBAL: XVT 0xA08BF0
const char *g_str_mesh_component_names[33] = {0};
/* Start of each panel sprite in the panel sprite data, by sprite number;
 * hud_load_panel_sprite_records, its only writer, fills it. A layout's selector
 * picks the first sprite of a widget here. */
// GLOBAL: XVT 0x9ED240
uint8_t *g_hud_panel_sprite_data_by_index[265] = {0};
/* Where hud_load_panel_sprite_records writes the next sprite byte; it advances
 * past each sprite. hud_rebuild_display_for_view_state starts it at the memory of
 * g_hud_panel_sprite_data_handle before reloading. */
// GLOBAL: XVT 0xA082A0
uint8_t *g_hud_panel_sprite_data_write_cursor = NULL;
/* The HUD instrument layouts: three sets of 144 (cockpit, HUD-only, craft
 * list), each element's position and widget values.
 * hud_load_cockpit_interface_file reads the first two sets and
 * hud_load_auxiliary_cockpit_interface_file the third. At run time hud_init_hud and
 * hud_update_critical_hull_shield_warning use layout 127's
 * clip_height_or_foreground_color as a counter, hud_draw_cmd_target_details stores
 * label widths in the clip_width of layouts 104 to 107, and
 * hud_rebuild_display_for_view_state sets layout 396's selector. */
// GLOBAL: XVT 0xA08CA0
struct hud_element_layout g_hud_element_layouts[HUD_INSTRUMENT_COUNT] = {{0}};
/* Per HUD element, the value or state it was last drawn with, so it is redrawn
 * only on a change; writers set -1 or -2 to force a redraw. Many functions
 * write it, chiefly the Hud_DrawCached... functions and hud_init_hud, which sets
 * -2 everywhere and -3 for the MFD page elements; the modern build's
 * xvt_cockpit_readouts_begin_target sets entries 102 and 103 to -2. */
// GLOBAL: XVT 0xA0A1E0
int16_t g_hud_element_state_cache[HUD_INSTRUMENT_COUNT] = {0};
/* Index in g_hud_element_layouts of the current view's set: 0 for the cockpit
 * set, 144 for the HUD-only set, 288 for the craft list set. Only
 * hud_rebuild_display_for_view_state writes it. */
// GLOBAL: XVT 0xA08374
uint16_t g_hud_instrument_set_base_index = HUD_COCKPIT_INSTRUMENT_BASE_INDEX;
/* Palette index of the radar blip being added; only hud_add_blip_to_radar writes
 * it, and an IFF it does not list keeps the last value. */
// GLOBAL: XVT 0xA08368
uint16_t g_radar_blip_color = 0;
/* Blip list the current radar frame fills for the fore radar: one of
 * g_radar_fore_blip_buffer_a and B, chosen by hud_draw_radar_blips each frame. */
// GLOBAL: XVT 0xA08A04
struct hud_radar_blip_point *g_radar_fore_draw_blips = NULL;
/* Blips in g_radar_fore_draw_blips, 0 to 47. Set to 0 by hud_init_hud and at the
 * start of each hud_draw_radar_blips; hud_add_blip_to_radar raises it. */
// GLOBAL: XVT 0xA08A02
uint16_t g_radar_fore_blip_count = 0;
/* Blip list the current radar frame fills for the aft radar: one of
 * g_radar_aft_blip_buffer_a and B, chosen by hud_draw_radar_blips each frame. */
// GLOBAL: XVT 0xA08340
struct hud_radar_blip_point *g_radar_aft_draw_blips = NULL;
/* Blips in g_radar_aft_draw_blips, 0 to 47. Set to 0 by hud_init_hud and at the
 * start of each hud_draw_radar_blips; hud_add_blip_to_radar raises it. */
// GLOBAL: XVT 0xA08C7E
uint16_t g_radar_aft_blip_count = 0;
/* Fore radar blips of the frame before, which hud_draw_radar_blips erases: the
 * buffer not in g_radar_fore_draw_blips. */
// GLOBAL: XVT 0xA08360
struct hud_radar_blip_point *g_radar_fore_erase_blips = NULL;
/* Aft radar blips of the frame before, which hud_draw_radar_blips erases: the
 * buffer not in g_radar_aft_draw_blips. */
// GLOBAL: XVT 0xA08BB0
struct hud_radar_blip_point *g_radar_aft_erase_blips = NULL;
/* Fore blips drawn the frame before, the count hud_draw_radar_blips erases; only
 * that function writes it. */
// GLOBAL: XVT 0xA083BA
uint16_t g_radar_fore_prev_blip_count = 0;
/* Aft blips drawn the frame before, the count hud_draw_radar_blips erases; only
 * that function writes it. */
// GLOBAL: XVT 0xA08344
uint16_t g_radar_aft_prev_blip_count = 0;
/* Which radar buffers draw this frame: 1 draws into the A buffers and erases
 * the B ones, 0 the reverse. hud_draw_radar_blips flips it each frame;
 * hud_init_hud sets 0. */
// GLOBAL: XVT 0xA08364
uint8_t g_radar_blip_buffer_parity = 0;
/* 1 while the radar target marker is drawn and its background saved, so the
 * next hud_draw_radar_blips restores it first. Set by hud_draw_radar_blips; set to
 * 0 by it with no target and by hud_init_hud. */
// GLOBAL: XVT 0xA0837A
uint8_t g_radar_target_marker_background_saved = 0;
/* First of the two fore radar blip lists, 48 entries. */
// GLOBAL: XVT 0xA083C0
static struct hud_radar_blip_point g_radar_fore_blip_buffer_a[48] = {{0}};
/* Second of the two fore radar blip lists, 48 entries. */
// GLOBAL: XVT 0xA084E0
static struct hud_radar_blip_point g_radar_fore_blip_buffer_b[48] = {{0}};
/* First of the two aft radar blip lists, 48 entries. */
// GLOBAL: XVT 0xA0A540
static struct hud_radar_blip_point g_radar_aft_blip_buffer_a[48] = {{0}};
/* Second of the two aft radar blip lists, 48 entries. */
// GLOBAL: XVT 0xA0A660
static struct hud_radar_blip_point g_radar_aft_blip_buffer_b[48] = {{0}};
/* Width, in pixels, of the scoreboard page area that hud_blit_software_mfd_pages
 * copies from g_flight_offscreen_buffer: 112 at 320x240, 224 at 640x480, 168 at
 * 480x360. Only hud_init_hud writes it, for the local player's resolution. */
// GLOBAL: XVT 0xA08366
uint16_t g_mfd_mission_scoreboard_blit_width = 0;
/* Source top edge, in pixels, of the scoreboard page area that
 * hud_blit_software_mfd_pages copies from g_flight_offscreen_buffer: 109 at 320x240,
 * 219 at 640x480, 164 at 480x360. Only hud_init_hud writes it, for the local
 * player's resolution. */
// GLOBAL: XVT 0xA0836A
uint16_t g_mfd_mission_scoreboard_blit_source_y = 0;
/* Height, in pixels, of the scoreboard page area that hud_blit_software_mfd_pages
 * copies from g_flight_offscreen_buffer: 70 at 320x240, 140 at 640x480, 105 at
 * 480x360. Only hud_init_hud writes it, for the local player's resolution. */
// GLOBAL: XVT 0xA0836C
uint16_t g_mfd_mission_scoreboard_blit_height = 0;
/* Source left edge, in pixels, of the scoreboard page area that
 * hud_blit_software_mfd_pages copies from g_flight_offscreen_buffer: 2 at 320x240, 4
 * at 640x480, 3 at 480x360. Only hud_init_hud writes it, for the local player's
 * resolution. */
// GLOBAL: XVT 0xA0836E
uint16_t g_mfd_mission_scoreboard_blit_source_x = 0;
/* Source top edge, in pixels, of the map view page area that
 * hud_blit_software_mfd_pages copies from g_flight_offscreen_buffer: 186 at 320x240,
 * 373 at 640x480, 279 at 480x360. Only hud_init_hud writes it, for the local
 * player's resolution. */
// GLOBAL: XVT 0xA08376
uint16_t g_mfd_map_blit_source_y = 0;
/* Width, in pixels, of the map view page area that hud_blit_software_mfd_pages
 * copies from g_flight_offscreen_buffer: 111 at 320x240, 223 at 640x480, 167 at
 * 480x360. Only hud_init_hud writes it, for the local player's resolution. */
// GLOBAL: XVT 0xA08378
uint16_t g_mfd_map_blit_width = 0;
/* Height, in pixels, of the map view page area that hud_blit_software_mfd_pages
 * copies from g_flight_offscreen_buffer: 52 at 320x240, 104 at 640x480, 78 at
 * 480x360. Only hud_init_hud writes it, for the local player's resolution. */
// GLOBAL: XVT 0xA083B8
uint16_t g_mfd_map_blit_height = 0;
/* Source left edge, in pixels, of the map view page area that
 * hud_blit_software_mfd_pages copies from g_flight_offscreen_buffer: 121 at 320x240,
 * 242 at 640x480, 181 at 480x360. Only hud_init_hud writes it, for the local
 * player's resolution. */
// GLOBAL: XVT 0xA083BC
uint16_t g_mfd_map_blit_source_x = 0;
/* Height, in pixels, of the craft list page area that hud_blit_software_mfd_pages
 * copies from g_flight_offscreen_buffer: 47 at 320x240, 94 at 640x480, 70 at
 * 480x360. hud_init_hud writes it for the local player's resolution, or from
 * layout 130 of the current set while the map camera is on;
 * hud_rebuild_display_for_view_state sets the resolution's value again when leaving
 * the craft list view. */
// GLOBAL: XVT 0xA08604
uint16_t g_mfd_craft_list_blit_height = 0;
/* Source left edge, in pixels, of the craft list page area that
 * hud_blit_software_mfd_pages copies from g_flight_offscreen_buffer: 153 at 320x240,
 * 306 at 640x480, 229 at 480x360. Only hud_init_hud writes it, for the local
 * player's resolution. */
// GLOBAL: XVT 0xA08606
uint16_t g_mfd_craft_list_blit_source_x = 0;
/* Source top edge, in pixels, of the craft list page area that
 * hud_blit_software_mfd_pages copies from g_flight_offscreen_buffer: 53 at 320x240,
 * 107 at 640x480, 80 at 480x360. Only hud_init_hud writes it, for the local
 * player's resolution. */
// GLOBAL: XVT 0xA08608
uint16_t g_mfd_craft_list_blit_source_y = 0;
/* Width, in pixels, of the craft list page area that hud_blit_software_mfd_pages
 * copies from g_flight_offscreen_buffer: 112 at 320x240, 225 at 640x480, 168 at
 * 480x360. hud_init_hud writes it for the local player's resolution, or from
 * layout 130 of the current set while the map camera is on;
 * hud_rebuild_display_for_view_state sets the resolution's value again when leaving
 * the craft list view. */
// GLOBAL: XVT 0xA08A00
uint16_t g_mfd_craft_list_blit_width = 0;
/* Height, in pixels, of the goals page area that hud_blit_software_mfd_pages
 * copies from g_flight_offscreen_buffer: 53 at 320x240, 107 at 640x480, 80 at
 * 480x360. Only hud_init_hud writes it, for the local player's resolution. */
// GLOBAL: XVT 0xA08C80
uint16_t g_mfd_goals_blit_height = 0;
/* Source left edge, in pixels, of the damage page area that
 * hud_blit_software_mfd_pages copies from g_flight_offscreen_buffer: 153 at 320x240,
 * 306 at 640x480, 229 at 480x360. Only hud_init_hud writes it, for the local
 * player's resolution. */
// GLOBAL: XVT 0xA08C82
uint16_t g_mfd_damage_blit_source_x = 0;
/* Source left edge, in pixels, of the goals page area that
 * hud_blit_software_mfd_pages copies from g_flight_offscreen_buffer: 6 at 320x240,
 * 12 at 640x480, 9 at 480x360. Only hud_init_hud writes it, for the local
 * player's resolution. */
// GLOBAL: XVT 0xA08C84
uint16_t g_mfd_goals_blit_source_x = 0;
/* Height, in pixels, of the damage page area that hud_blit_software_mfd_pages
 * copies from g_flight_offscreen_buffer: 80 at 320x240, 160 at 640x480, 120 at
 * 480x360. Only hud_init_hud writes it, for the local player's resolution. */
// GLOBAL: XVT 0xA08C8A
uint16_t g_mfd_damage_blit_height = 0;
/* Width, in pixels, of the goals page area that hud_blit_software_mfd_pages copies
 * from g_flight_offscreen_buffer: 141 at 320x240, 282 at 640x480, 211 at 480x360.
 * Only hud_init_hud writes it, for the local player's resolution. */
// GLOBAL: XVT 0xA08C8C
uint16_t g_mfd_goals_blit_width = 0;
/* Width, in pixels, of the damage page area that hud_blit_software_mfd_pages
 * copies from g_flight_offscreen_buffer: 100 at 320x240, 200 at 640x480, 150 at
 * 480x360. Only hud_init_hud writes it, for the local player's resolution. */
// GLOBAL: XVT 0xA08C8E
uint16_t g_mfd_damage_blit_width = 0;
/* Source top edge, in pixels, of the damage page area that
 * hud_blit_software_mfd_pages copies from g_flight_offscreen_buffer: 103 at 320x240,
 * 206 at 640x480, 154 at 480x360. Only hud_init_hud writes it, for the local
 * player's resolution. */
// GLOBAL: XVT 0xA08C90
uint16_t g_mfd_damage_blit_source_y = 0;
/* Source top edge, in pixels, of the goals page area that
 * hud_blit_software_mfd_pages copies from g_flight_offscreen_buffer: 53 at 320x240,
 * 107 at 640x480, 80 at 480x360. Only hud_init_hud writes it, for the local
 * player's resolution. */
// GLOBAL: XVT 0xA08C92
uint16_t g_mfd_goals_blit_source_y = 0;
/* Palette indices of a beam segment by the charge it holds, in thirds of 1,000
 * from empty (entry 0) to full (entry 3). Nothing writes it;
 * hud_draw_beam_strength2d reads it. */
// GLOBAL: XVT 0x521588
uint8_t g_hud_beam_segment_color_by_charge_step[4] = {0x30, 0x2D, 0x31, 0x32};
/* Offset in pixels of each of the nine beam segments from layout 51 at
 * 480x360. */
// GLOBAL: XVT 0x521590
const struct hud_beam_segment_offset g_hud_beam_segment_offsets480x360[9] = {
	{14, 14}, {12, 12}, {10, 10}, {9, 9}, {7, 7},
	{5, 5},	  {4, 4},   {2, 2},   {0, 0},
};
/* Sprite levels 0..10 include hit flash; text uses offset 10 plus levels 0..9. */
/* Palette shifts of the shield sprites by level 0 to 10 (10 is the hit flash),
 * then text colors by level 0 to 9 from HUD_SHIELD_TEXT_COLOR_OFFSET on, for
 * hud_draw_shield_strength2d. */
// GLOBAL: XVT 0x521570
const uint8_t g_hud_shield_colors[22] = {
	0x2c, 0x34, 0x35, 0x36, 0x38, 0x39, 0x3a, 0x3c, 0x3d, 0x3e, 0x2e,
	0x2e, 0x37, 0x37, 0x37, 0x3b, 0x3b, 0x3b, 0x3f, 0x3f, 0x3f, 0x2e,
};
/* Shield side, 0 front or 1 rear, that took the last hit; collide_damagecraft,
 * its only writer, sets it, and hud_draw_shield_strength2d flashes that side
 * while shield_hit_flash_timer runs. */
// GLOBAL: XVT 0x9FE7D0
uint8_t g_last_shield_damage_side = 0;
/* Offset in pixels of each of the nine beam segments from layout 51 at
 * 320x240. */
// GLOBAL: XVT 0x5215B8
const struct hud_beam_segment_offset g_hud_beam_segment_offsets320x240[9] = {
	{11, 11}, {10, 10}, {8, 8}, {7, 7}, {6, 6},
	{4, 4},	  {3, 3},   {2, 2}, {0, 0},
};
/* 1 while a laser cannon due to fire would hit the target, or with warheads
 * selected while missile_lock_state is 2. hud_draw_laser_cannon_indicators sets it
 * to 0 and then to 1 on a hit; hud_update_targeting_lock_indicator sets it for
 * warheads and reads it for lasers. */
// GLOBAL: XVT 0xA0A1D0
uint8_t g_target_lock_active = 0;
/* 1 while hud_init_hud, its only writer, redraws the HUD;
 * proving_grounds_draw_status_panel reads it. */
// GLOBAL: XVT 0x9D8B68
uint8_t g_hud_full_redraw_in_progress = 0;
/* Span mask of the craft list view's target inset, 480 bytes at 640x480 and
 * 480x360 and 200 at 320x240, read from the craft list's .INT file by
 * hud_load_auxiliary_cockpit_interface_file; hud_update3d_crt copies it into the
 * viewport's mask. */
// GLOBAL: XVT 0x556720
uint8_t g_hud_craft_list_inset_span_mask[480] = {0};
/* Span mask of the cockpit view's target inset, 480 bytes at 640x480 and
 * 480x360 and 200 at 320x240, read by hud_load_cockpit_interface_file;
 * hud_update3d_crt copies it into the viewport's mask. */
// GLOBAL: XVT 0x556540
uint8_t g_hud_cockpit_inset_span_mask[480] = {0};
/* Span mask of the HUD-only view's target inset, 480 bytes at 640x480 and
 * 480x360 and 200 at 320x240, read by hud_load_cockpit_interface_file;
 * hud_update3d_crt copies it into the viewport's mask. */
// GLOBAL: XVT 0x556360
uint8_t g_hud_only_view_inset_span_mask[480] = {0};
/* Cockpit overlay strings by cockpit_overlay_string_id (labels, scoreboard
 * headings, goal words, EJECT and the strings after it), filled by
 * string_table_load_game_strings. */
// GLOBAL: XVT 0xA0A130
const char *g_str_cockpit_overlay_text[40] = {0};
/* Target camera and targeting computer strings by cmd_threat_string_id, filled by
 * string_table_load_game_strings. */
// GLOBAL: XVT 0xA0A0E0
const char *g_str_cmd_threat_display_text[18] = {0};
/* Armament labels by threat_display_string_id (laser, ion, warhead, beam), filled
 * by string_table_load_game_strings; hud_draw_cmd_target_status_indicators draws
 * them. */
// GLOBAL: XVT 0xA08350
const char *g_str_threat_display_text[4] = {0};
/* Color codes, for flight_text_set_color, of a message by its pane type byte
 * (entries 0 to 8), and of a type 1 message by its digit 0 to 3 (entries 8 to
 * 11); read by hud_show_flight_message_pane and mfd_draw_message_log_page. */
// GLOBAL: XVT 0x5240A0
const uint8_t g_message_text_prefix_color_codes[16] = {
	0x42, 0x4A, 0x46, 0x4E, 0x52, 0x45, 0x42, 0x52,
	0x4A, 0x52, 0x46, 0x4E, 0x4A, 0x4E, 0,	  0,
};
/* Color codes, for flight_text_set_color, of a type 2 message by its sender's
 * IFF; read by hud_show_flight_message_pane and mfd_draw_message_log_page. */
// GLOBAL: XVT 0x5240B0
const uint8_t g_message_sender_iff_color_codes[8] = {0x52, 0x4A, 0x46, 0x4E,
						     0x4A, 0x4E, 0,    0};
/* 1 when the mission command line asks for the frame rate overlay with
 * "tickcounter"; set from it by flight_main in the original build and
 * xvt_flight_entry_read_launch_switches in the modern one. */
// GLOBAL: XVT 0x5235E0
uint8_t g_flight_conf_tick_counter_enabled = 0;
/* Ticks the last flight loop took, for the frame rate overlay; written by
 * flight_run_mission_loop in the original build and xvt_flight_frame_render in the
 * modern one, only while the overlay is on. */
// GLOBAL: XVT 0x9A8D90
int g_flight_tick_overlay_last_loop_ticks = 0;
/* Ticks summed over the overlay's sampling window; reset to 0 while the overlay
 * is off or once the sum passes LAG_LEVEL_2_TICKS (944). Written by
 * flight_run_mission_loop in the original build and xvt_flight_frame_render in the
 * modern one. */
// GLOBAL: XVT 0x9A7BA0
int g_flight_tick_overlay_window_ticks = 0;
/* Loops counted in g_flight_tick_overlay_window_ticks; reset with it. Written by
 * flight_run_mission_loop in the original build and xvt_flight_frame_render in the
 * modern one. */
// GLOBAL: XVT 0xA0829C
int g_flight_tick_overlay_sample_count = 0;
/* Packet drop level, 0 to 3, shown by the status line's packet drop mark; 0
 * hides it. Set by flight_run_mission_loop in the original build and
 * xvt_flight_frame_update_packet_drop_indicator in the modern one; flight start sets
 * 0. */
// GLOBAL: XVT 0x9A8BFC
int g_packet_drop_indicator = 0;
/* Lag level, 0 to 3, shown by the status line's lag mark; 0 hides it. Set by
 * flight_run_mission_loop in the original build and
 * xvt_flight_frame_update_lag_indicator in the modern one; flight start sets 0. */
// GLOBAL: XVT 0x9EC45C
int g_lag_indicator = 0;

/* The system message pane's message (pane types 3, 4 and 7), its
 * state_or_message_id 0xFFFF while empty. msg_emit_in_flight_message fills it;
 * hud_show_flight_message_pane marks it shown, hud_update_flight_message_panes and
 * hud_reset_flight_message_panes empty it, hud_advance_flight_message_pane_timers ages
 * it. */
// GLOBAL: XVT 0x5569F8
struct hud_in_flight_message_record g_system_message_pane;
/* The flight group message pane's message (pane type 8), its state_or_message_id
 * 0xFFFF while empty. msg_emit_in_flight_message fills it;
 * hud_show_flight_message_pane marks it shown, hud_update_flight_message_panes and
 * hud_reset_flight_message_panes empty it, hud_advance_flight_message_pane_timers ages
 * it. */
// GLOBAL: XVT 0x556A50
struct hud_in_flight_message_record g_flight_group_message_pane;
/* The ready message pane: slot 0 is the message shown, its state_or_message_id
 * 0xFFFF while empty; slots 1 to g_ready_message_queue_count wait in order, and
 * slot 10 can take a message that is never shown. Filled by
 * msg_emit_in_flight_message and moved by hud_shift_ready_message_queue_for_replacement
 * and hud_advance_ready_message_queue. */
// GLOBAL: XVT 0x9A6FF0
struct hud_in_flight_message_record g_ready_message_pane_queue[11];
/* Messages waiting behind slot 0 of g_ready_message_pane_queue, 0 to 9. Raised by
 * msg_emit_in_flight_message and hud_shift_ready_message_queue_for_replacement, lowered
 * by hud_advance_ready_message_queue, set to 0 by hud_reset_flight_message_panes,
 * hud_clear_ready_message_queue and mission_init_flight_runtime_state. */
// GLOBAL: XVT 0x9D77F8
uint8_t g_ready_message_queue_count;
/* Nonzero when the next hud_update_flight_message_panes must expire all three
 * panes; hud_reset_flight_message_panes raises it, and
 * hud_update_flight_message_panes sets it back to 0. */
// GLOBAL: XVT 0x556AA4
static int g_flight_message_panes_force_expire = 0;
/* Slot 0's state_or_message_id as hud_reset_flight_message_panes, its only writer,
 * last found it before emptying the pane. Nothing reads it. */
// GLOBAL: XVT 0x9D7686
static uint16_t g_unused_ready_message_pane_initial_state = 0;
/* Goal phrase (a message id) of the last target description shown, which
 * hud_update_flight_message_panes compares to decide whether to show it again.
 * Written by msg_build_target_description when it emits, by
 * hud_update_flight_message_panes, and by hud_reset_flight_message_panes, which sets
 * 331, the blank message. */
// GLOBAL: XVT 0x9D7694
int g_target_description_message_id = 0;
/* 1 while radio messages are backed up: msg_write_message_log_file then runs when
 * the message log wraps, at mission end, and when the player turns the backup
 * off. The player's Shift+L turns it on and off (flight_update_player_step in the
 * original build, xvt_flight_sim_update_player_step in the modern one); mission end
 * and hud_reset_flight_message_panes set 0. */
// GLOBAL: XVT 0x9ECC3C
int g_radio_message_backup_enabled = 0;
/* Read as a replay view switch by msg_emit_in_flight_message, hud_update3d_crt and
 * several other functions. Nothing writes it, so it stays 0. */
// GLOBAL: XVT 0xA00860
uint16_t g_replay_view_mode = 0;
/* 1 while system messages (pane types 3, 4 and 7) are shown. Flight start sets
 * 1 (flight_main_loop in the original build, xvt_flight_loading_mission_setup in
 * the modern one); the player step toggles it (flight_update_player_step in the
 * original build, xvt_flight_sim_update_player_step in the modern one). */
// GLOBAL: XVT 0x9A7B54
int g_system_message_display_enabled = 0;
/* The ready pane's left edge on g_flight_offscreen_buffer: 42 at 320x240, 85 at
 * 640x480, 63 at 480x360, set by hud_reset_flight_message_panes when it finds -1.
 * Flight start sets -1 (flight_main_loop in the original build,
 * xvt_flight_loading_mission_setup in the modern one), so the pane rectangles are
 * set once per flight. */
// GLOBAL: XVT 0x5235DC
int g_ready_message_pane_left = -1;
/* The ready pane's top edge on g_flight_offscreen_buffer: 3 at 320x240, 6 at
 * 640x480, 4 at 480x360, set the first time hud_reset_flight_message_panes runs in
 * a flight, its only writer. */
// GLOBAL: XVT 0x9D1148
int g_ready_message_pane_top = 0;
/* The ready pane's right edge, exclusive, on g_flight_offscreen_buffer: 277 at
 * 320x240, 505 at 640x480, 378 at 480x360, set the first time
 * hud_reset_flight_message_panes runs in a flight, its only writer. */
// GLOBAL: XVT 0x9A7EC0
int g_ready_message_pane_right = 0;
/* The ready pane's bottom edge, exclusive, on g_flight_offscreen_buffer: 26 at
 * 320x240, 53 at 640x480, 39 at 480x360, set the first time
 * hud_reset_flight_message_panes runs in a flight, its only writer. */
// GLOBAL: XVT 0x9A7B44
int g_ready_message_pane_bottom = 0;
/* The system message pane's left edge on g_flight_offscreen_buffer: 42 at
 * 320x240, 85 at 640x480, 63 at 480x360, set the first time
 * hud_reset_flight_message_panes runs in a flight, its only writer. */
// GLOBAL: XVT 0x9A7390
static int g_system_message_pane_left = 0;
/* The system message pane's top edge on g_flight_offscreen_buffer: 32 at 320x240,
 * 64 at 640x480, 48 at 480x360, set the first time hud_reset_flight_message_panes
 * runs in a flight, its only writer. */
// GLOBAL: XVT 0x9D12F8
static int g_system_message_pane_top = 0;
/* The system message pane's right edge, exclusive, on g_flight_offscreen_buffer:
 * 277 at 320x240, 555 at 640x480, 415 at 480x360, set the first time
 * hud_reset_flight_message_panes runs in a flight, its only writer. */
// GLOBAL: XVT 0x9A8D98
static int g_system_message_pane_right = 0;
/* The system message pane's bottom edge, exclusive, on g_flight_offscreen_buffer:
 * 37 at 320x240, 75 at 640x480, 56 at 480x360, set the first time
 * hud_reset_flight_message_panes runs in a flight, its only writer. */
// GLOBAL: XVT 0x9A8D94
static int g_system_message_pane_bottom = 0;
/* The flight group message pane's left edge on g_flight_offscreen_buffer: 75 at
 * 320x240, 150 at 640x480, 112 at 480x360, set the first time
 * hud_reset_flight_message_panes runs in a flight, its only writer. */
// GLOBAL: XVT 0x9EC470
static int g_flight_group_message_pane_left = 0;
/* The flight group message pane's top edge on g_flight_offscreen_buffer: 44 at
 * 320x240, 89 at 640x480, 66 at 480x360, set the first time
 * hud_reset_flight_message_panes runs in a flight, its only writer. */
// GLOBAL: XVT 0x9ECC38
static int g_flight_group_message_pane_top = 0;
/* The flight group message pane's right edge, exclusive, on
 * g_flight_offscreen_buffer: 265 at 320x240, 530 at 640x480, 397 at 480x360, set
 * the first time hud_reset_flight_message_panes runs in a flight, its only
 * writer. */
// GLOBAL: XVT 0x9D12F4
static int g_flight_group_message_pane_right = 0;
/* The flight group message pane's bottom edge, exclusive, on
 * g_flight_offscreen_buffer: 49 at 320x240, 100 at 640x480, 74 at 480x360, set
 * the first time hud_reset_flight_message_panes runs in a flight, its only
 * writer. */
// GLOBAL: XVT 0x9D6934
static int g_flight_group_message_pane_bottom = 0;
/* Text measured for the width of a three-digit field. */
// GLOBAL: XVT 0x5215E0
const char g_three_digit_width_text[4] = "000";
/* Text measured for the width of the clock's minutes and colon. */
// GLOBAL: XVT 0x52168C
const char g_mission_clock_minutes_width_text[4] = "00:";
/* Type tag of an LFD palette entry, which hud_load_cockpit_lfd_entries
 * converts. */
// GLOBAL: XVT 0x521564
const uint8_t g_lfd_palette_resource_type_tag[4] = {'P', 'L', 'T', 'T'};

/* Draws a box marker in the flight view for the hardware renderer: eight
 * one-pixel strokes along the corners of the box at x, y (relative to the
 * viewport), each an eighth of the box's width or height, at least 3 and at
 * most the box's size, clipped to the viewport. The strokes go into
 * g_flight_vertex_buffer and g_tri_buffer as opaque quads in palette color
 * color_idx (g_sw_palette times 4), depth-tested and written at depth, at least
 * 1, through g_inv_depth_proj_scale; the batch is flushed first when 32 vertices
 * or 16 triangles would not fit. A 4 by 4 box at depth 1 is instead filled in
 * software with color_idx through g_flight_fill_rect_clipped_fn, leaving the text
 * clip on the viewport. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x40C430
void hud_draw_box_overlay_hw(int x, int y, int width, int height, int color_idx,
			     int depth)
{

	enum {
		MARKER_SIZE = 4,
		MIN_CORNER_LENGTH = 3,
		QUAD_VERTEX_COUNT = 4,
		QUAD_TRIANGLE_COUNT = 2,
		MAX_BOX_VERTEX_COUNT = 32,
		MAX_BOX_TRIANGLE_COUNT = 16,
	};

	int box_width = width;
	int adjusted_depth = depth;
	if (adjusted_depth == 1 && box_width == MARKER_SIZE &&
	    height == MARKER_SIZE) {
		flight_surface_lock();
		uint8_t saved_text_background_color = g_flight_text_bg_color;
		g_flight_text_bg_color = (uint8_t)color_idx;
		uint16_t marker_x = (uint16_t)(g_flight_vp_x + x);
		uint16_t marker_y = (uint16_t)(g_flight_vp_y + y);
		flight_text_set_clip_rect(g_flight_vp_x, g_flight_vp_y,
					  g_flight_vp_x + g_flight_vp_width,
					  g_flight_vp_y + g_flight_vp_height);
		g_flight_fill_rect_clipped_fn(marker_x, marker_y,
					      marker_x + MARKER_SIZE,
					      marker_y + MARKER_SIZE, 1);
		g_flight_text_bg_color = saved_text_background_color;
		flight_surface_unlock();
		return;
	}
	int box_height = height;

	if (g_d3d_vertex_count + MAX_BOX_VERTEX_COUNT > g_max_batch_verts ||
	    g_d3d_triangle_count + MAX_BOX_TRIANGLE_COUNT > g_max_batch_tris) {
		math_set_fpu_extended_precision_mode();
		std3d_start_scene();
		std3d_lock_execute_buffer();
		std3d_add_vertices(g_flight_vertex_buffer, g_d3d_vertex_count);
		std3d_begin_instructions();
		std3d_add_triangles(g_tri_buffer,
				    (unsigned int)g_d3d_triangle_count);
		std3d_execute_buffer();
		std3d_end_scene();
		math_set_fpu_single_precision_mode();
		g_d3d_triangle_count = 0;
		g_d3d_vertex_count = 0;
	}

	uint32_t color = 4 * (g_sw_palette[color_idx].b +
			      ((g_sw_palette[color_idx].g +
				((g_sw_palette[color_idx].r - 64) << 8))
			       << 8));
	int box_top = y;
	int right = x + box_width;
	int bottom = box_top + box_height;
	int corner_width = box_width >> 3;
	int corner_height = box_height >> 3;
	if (corner_width < MIN_CORNER_LENGTH) {
		corner_width = MIN_CORNER_LENGTH;
	}
	if (corner_height < MIN_CORNER_LENGTH) {
		corner_height = MIN_CORNER_LENGTH;
	}
	if (corner_width > box_width) {
		corner_width = box_width;
	}
	if (corner_height > box_height) {
		corner_height = box_height;
	}
	if (adjusted_depth < 1) {
		adjusted_depth = 1;
	}
	float depth_value = g_render_unit_float /
			    ((float)adjusted_depth * g_inv_depth_proj_scale +
			     g_render_unit_float);
	if (g_std3dz_compare_cap == 2) {
		depth_value = g_render_unit_float - depth_value;
	}

	const std3d_render_state_flags render_flags =
		STD3D_RS_Z_COMPARE_ENABLE | STD3D_RS_Z_WRITE_ENABLE |
		STD3D_RS_MONO_DISABLE;
	int start;
	int end;
	int vertex_index;
	if (box_top >= 0 && box_top < g_flight_vp_height) {
		start = x;
		end = x + corner_width;
		if (start < 0) {
			start = 0;
		}
		if (end >= g_flight_vp_width) {
			end = g_flight_vp_width - 1;
		}
		if (end > start) {
			g_flight_vertex_buffer[g_d3d_vertex_count].sx =
				g_flight_vp_origin_x + (float)(start);
			g_flight_vertex_buffer[g_d3d_vertex_count].sy =
				g_flight_vp_origin_y + (float)box_top;
			g_flight_vertex_buffer[g_d3d_vertex_count + 1].sx =
				g_flight_vp_origin_x + (float)(end);
			g_flight_vertex_buffer[g_d3d_vertex_count + 1].sy =
				g_flight_vp_origin_y + (float)box_top;
			g_flight_vertex_buffer[g_d3d_vertex_count + 2].sx =
				g_flight_vp_origin_x + (float)(end);
			g_flight_vertex_buffer[g_d3d_vertex_count + 2].sy =
				g_flight_vp_origin_y + (float)(box_top + 1);
			g_flight_vertex_buffer[g_d3d_vertex_count + 3].sx =
				g_flight_vp_origin_x + (float)(start);
			g_flight_vertex_buffer[g_d3d_vertex_count + 3].sy =
				g_flight_vp_origin_y + (float)(box_top + 1);
			for (vertex_index = 0; vertex_index < QUAD_VERTEX_COUNT;
			     ++vertex_index) {
				g_flight_vertex_buffer[g_d3d_vertex_count +
						       vertex_index]
					.sz = depth_value;
				g_flight_vertex_buffer[g_d3d_vertex_count +
						       vertex_index]
					.rhw = depth_value;
				g_flight_vertex_buffer[g_d3d_vertex_count +
						       vertex_index]
					.tu = 0.0f;
				g_flight_vertex_buffer[g_d3d_vertex_count +
						       vertex_index]
					.tv = 0.0f;
				g_flight_vertex_buffer[g_d3d_vertex_count +
						       vertex_index]
					.color = color;
				g_flight_vertex_buffer[g_d3d_vertex_count +
						       vertex_index]
					.specular = 0;
			}
			g_tri_buffer[g_d3d_triangle_count].vertex_index0 =
				g_d3d_vertex_count;
			g_tri_buffer[g_d3d_triangle_count].vertex_index1 =
				g_d3d_vertex_count + 1;
			g_tri_buffer[g_d3d_triangle_count].vertex_index2 =
				g_d3d_vertex_count + 2;
			g_tri_buffer[g_d3d_triangle_count].texture = NULL;
			g_tri_buffer[g_d3d_triangle_count++].flags =
				render_flags;
			g_tri_buffer[g_d3d_triangle_count].vertex_index0 =
				g_d3d_vertex_count;
			g_tri_buffer[g_d3d_triangle_count].vertex_index1 =
				g_d3d_vertex_count + 2;
			g_tri_buffer[g_d3d_triangle_count].vertex_index2 =
				g_d3d_vertex_count + 3;
			g_tri_buffer[g_d3d_triangle_count].texture = NULL;
			g_tri_buffer[g_d3d_triangle_count++].flags =
				render_flags;
			g_d3d_vertex_count += QUAD_VERTEX_COUNT;
		}
		start = right - corner_width;
		end = right;
		if (start < 0) {
			start = 0;
		}
		if (end >= g_flight_vp_width) {
			end = g_flight_vp_width - 1;
		}
		if (end > start) {
			g_flight_vertex_buffer[g_d3d_vertex_count].sx =
				g_flight_vp_origin_x + (float)(start);
			g_flight_vertex_buffer[g_d3d_vertex_count].sy =
				g_flight_vp_origin_y + (float)box_top;
			g_flight_vertex_buffer[g_d3d_vertex_count + 1].sx =
				g_flight_vp_origin_x + (float)(end);
			g_flight_vertex_buffer[g_d3d_vertex_count + 1].sy =
				g_flight_vp_origin_y + (float)box_top;
			g_flight_vertex_buffer[g_d3d_vertex_count + 2].sx =
				g_flight_vp_origin_x + (float)(end);
			g_flight_vertex_buffer[g_d3d_vertex_count + 2].sy =
				g_flight_vp_origin_y + (float)(box_top + 1);
			g_flight_vertex_buffer[g_d3d_vertex_count + 3].sx =
				g_flight_vp_origin_x + (float)(start);
			g_flight_vertex_buffer[g_d3d_vertex_count + 3].sy =
				g_flight_vp_origin_y + (float)(box_top + 1);
			for (vertex_index = 0; vertex_index < QUAD_VERTEX_COUNT;
			     ++vertex_index) {
				g_flight_vertex_buffer[g_d3d_vertex_count +
						       vertex_index]
					.sz = depth_value;
				g_flight_vertex_buffer[g_d3d_vertex_count +
						       vertex_index]
					.rhw = depth_value;
				g_flight_vertex_buffer[g_d3d_vertex_count +
						       vertex_index]
					.tu = 0.0f;
				g_flight_vertex_buffer[g_d3d_vertex_count +
						       vertex_index]
					.tv = 0.0f;
				g_flight_vertex_buffer[g_d3d_vertex_count +
						       vertex_index]
					.color = color;
				g_flight_vertex_buffer[g_d3d_vertex_count +
						       vertex_index]
					.specular = 0;
			}
			g_tri_buffer[g_d3d_triangle_count].vertex_index0 =
				g_d3d_vertex_count;
			g_tri_buffer[g_d3d_triangle_count].vertex_index1 =
				g_d3d_vertex_count + 1;
			g_tri_buffer[g_d3d_triangle_count].vertex_index2 =
				g_d3d_vertex_count + 2;
			g_tri_buffer[g_d3d_triangle_count].texture = NULL;
			g_tri_buffer[g_d3d_triangle_count++].flags =
				render_flags;
			g_tri_buffer[g_d3d_triangle_count].vertex_index0 =
				g_d3d_vertex_count;
			g_tri_buffer[g_d3d_triangle_count].vertex_index1 =
				g_d3d_vertex_count + 2;
			g_tri_buffer[g_d3d_triangle_count].vertex_index2 =
				g_d3d_vertex_count + 3;
			g_tri_buffer[g_d3d_triangle_count].texture = NULL;
			g_tri_buffer[g_d3d_triangle_count++].flags =
				render_flags;
			g_d3d_vertex_count += QUAD_VERTEX_COUNT;
		}
	}

	if (bottom >= 0 && bottom < g_flight_vp_height) {
		start = x;
		end = x + corner_width;
		if (start < 0) {
			start = 0;
		}
		if (end >= g_flight_vp_width) {
			end = g_flight_vp_width - 1;
		}
		if (end > start) {
			g_flight_vertex_buffer[g_d3d_vertex_count].sx =
				g_flight_vp_origin_x + (float)(start);
			g_flight_vertex_buffer[g_d3d_vertex_count].sy =
				g_flight_vp_origin_y + (float)(bottom);
			g_flight_vertex_buffer[g_d3d_vertex_count + 1].sx =
				g_flight_vp_origin_x + (float)(end);
			g_flight_vertex_buffer[g_d3d_vertex_count + 1].sy =
				g_flight_vp_origin_y + (float)(bottom);
			g_flight_vertex_buffer[g_d3d_vertex_count + 2].sx =
				g_flight_vp_origin_x + (float)(end);
			g_flight_vertex_buffer[g_d3d_vertex_count + 2].sy =
				g_flight_vp_origin_y + (float)(bottom + 1);
			g_flight_vertex_buffer[g_d3d_vertex_count + 3].sx =
				g_flight_vp_origin_x + (float)(start);
			g_flight_vertex_buffer[g_d3d_vertex_count + 3].sy =
				g_flight_vp_origin_y + (float)(bottom + 1);
			for (vertex_index = 0; vertex_index < QUAD_VERTEX_COUNT;
			     ++vertex_index) {
				g_flight_vertex_buffer[g_d3d_vertex_count +
						       vertex_index]
					.sz = depth_value;
				g_flight_vertex_buffer[g_d3d_vertex_count +
						       vertex_index]
					.rhw = depth_value;
				g_flight_vertex_buffer[g_d3d_vertex_count +
						       vertex_index]
					.tu = 0.0f;
				g_flight_vertex_buffer[g_d3d_vertex_count +
						       vertex_index]
					.tv = 0.0f;
				g_flight_vertex_buffer[g_d3d_vertex_count +
						       vertex_index]
					.color = color;
				g_flight_vertex_buffer[g_d3d_vertex_count +
						       vertex_index]
					.specular = 0;
			}
			g_tri_buffer[g_d3d_triangle_count].vertex_index0 =
				g_d3d_vertex_count;
			g_tri_buffer[g_d3d_triangle_count].vertex_index1 =
				g_d3d_vertex_count + 1;
			g_tri_buffer[g_d3d_triangle_count].vertex_index2 =
				g_d3d_vertex_count + 2;
			g_tri_buffer[g_d3d_triangle_count].texture = NULL;
			g_tri_buffer[g_d3d_triangle_count++].flags =
				render_flags;
			g_tri_buffer[g_d3d_triangle_count].vertex_index0 =
				g_d3d_vertex_count;
			g_tri_buffer[g_d3d_triangle_count].vertex_index1 =
				g_d3d_vertex_count + 2;
			g_tri_buffer[g_d3d_triangle_count].vertex_index2 =
				g_d3d_vertex_count + 3;
			g_tri_buffer[g_d3d_triangle_count].texture = NULL;
			g_tri_buffer[g_d3d_triangle_count++].flags =
				render_flags;
			g_d3d_vertex_count += QUAD_VERTEX_COUNT;
		}
		start = right - corner_width;
		end = right + 1;
		if (start < 0) {
			start = 0;
		}
		if (end >= g_flight_vp_width) {
			end = g_flight_vp_width - 1;
		}
		if (end > start) {
			g_flight_vertex_buffer[g_d3d_vertex_count].sx =
				g_flight_vp_origin_x + (float)(start);
			g_flight_vertex_buffer[g_d3d_vertex_count].sy =
				g_flight_vp_origin_y + (float)(bottom);
			g_flight_vertex_buffer[g_d3d_vertex_count + 1].sx =
				g_flight_vp_origin_x + (float)(end);
			g_flight_vertex_buffer[g_d3d_vertex_count + 1].sy =
				g_flight_vp_origin_y + (float)(bottom);
			g_flight_vertex_buffer[g_d3d_vertex_count + 2].sx =
				g_flight_vp_origin_x + (float)(end);
			g_flight_vertex_buffer[g_d3d_vertex_count + 2].sy =
				g_flight_vp_origin_y + (float)(bottom + 1);
			g_flight_vertex_buffer[g_d3d_vertex_count + 3].sx =
				g_flight_vp_origin_x + (float)(start);
			g_flight_vertex_buffer[g_d3d_vertex_count + 3].sy =
				g_flight_vp_origin_y + (float)(bottom + 1);
			for (vertex_index = 0; vertex_index < QUAD_VERTEX_COUNT;
			     ++vertex_index) {
				g_flight_vertex_buffer[g_d3d_vertex_count +
						       vertex_index]
					.sz = depth_value;
				g_flight_vertex_buffer[g_d3d_vertex_count +
						       vertex_index]
					.rhw = depth_value;
				g_flight_vertex_buffer[g_d3d_vertex_count +
						       vertex_index]
					.tu = 0.0f;
				g_flight_vertex_buffer[g_d3d_vertex_count +
						       vertex_index]
					.tv = 0.0f;
				g_flight_vertex_buffer[g_d3d_vertex_count +
						       vertex_index]
					.color = color;
				g_flight_vertex_buffer[g_d3d_vertex_count +
						       vertex_index]
					.specular = 0;
			}
			g_tri_buffer[g_d3d_triangle_count].vertex_index0 =
				g_d3d_vertex_count;
			g_tri_buffer[g_d3d_triangle_count].vertex_index1 =
				g_d3d_vertex_count + 1;
			g_tri_buffer[g_d3d_triangle_count].vertex_index2 =
				g_d3d_vertex_count + 2;
			g_tri_buffer[g_d3d_triangle_count].texture = NULL;
			g_tri_buffer[g_d3d_triangle_count++].flags =
				render_flags;
			g_tri_buffer[g_d3d_triangle_count].vertex_index0 =
				g_d3d_vertex_count;
			g_tri_buffer[g_d3d_triangle_count].vertex_index1 =
				g_d3d_vertex_count + 2;
			g_tri_buffer[g_d3d_triangle_count].vertex_index2 =
				g_d3d_vertex_count + 3;
			g_tri_buffer[g_d3d_triangle_count].texture = NULL;
			g_tri_buffer[g_d3d_triangle_count++].flags =
				render_flags;
			g_d3d_vertex_count += QUAD_VERTEX_COUNT;
		}
	}

	if (x >= 0 && x < g_flight_vp_width) {
		start = box_top;
		end = box_top + corner_height;
		if (start < 0) {
			start = 0;
		}
		if (end >= g_flight_vp_height) {
			end = g_flight_vp_height - 1;
		}
		if (end > start) {
			g_flight_vertex_buffer[g_d3d_vertex_count].sx =
				g_flight_vp_origin_x + (float)(x);
			g_flight_vertex_buffer[g_d3d_vertex_count].sy =
				g_flight_vp_origin_y + (float)(start);
			g_flight_vertex_buffer[g_d3d_vertex_count + 1].sx =
				g_flight_vp_origin_x + (float)(x);
			g_flight_vertex_buffer[g_d3d_vertex_count + 1].sy =
				g_flight_vp_origin_y + (float)(end);
			g_flight_vertex_buffer[g_d3d_vertex_count + 2].sx =
				g_flight_vp_origin_x + (float)(x + 1);
			g_flight_vertex_buffer[g_d3d_vertex_count + 2].sy =
				g_flight_vp_origin_y + (float)(end);
			g_flight_vertex_buffer[g_d3d_vertex_count + 3].sx =
				g_flight_vp_origin_x + (float)(x + 1);
			g_flight_vertex_buffer[g_d3d_vertex_count + 3].sy =
				g_flight_vp_origin_y + (float)(start);
			for (vertex_index = 0; vertex_index < QUAD_VERTEX_COUNT;
			     ++vertex_index) {
				g_flight_vertex_buffer[g_d3d_vertex_count +
						       vertex_index]
					.sz = depth_value;
				g_flight_vertex_buffer[g_d3d_vertex_count +
						       vertex_index]
					.rhw = depth_value;
				g_flight_vertex_buffer[g_d3d_vertex_count +
						       vertex_index]
					.tu = 0.0f;
				g_flight_vertex_buffer[g_d3d_vertex_count +
						       vertex_index]
					.tv = 0.0f;
				g_flight_vertex_buffer[g_d3d_vertex_count +
						       vertex_index]
					.color = color;
				g_flight_vertex_buffer[g_d3d_vertex_count +
						       vertex_index]
					.specular = 0;
			}
			g_tri_buffer[g_d3d_triangle_count].vertex_index0 =
				g_d3d_vertex_count;
			g_tri_buffer[g_d3d_triangle_count].vertex_index1 =
				g_d3d_vertex_count + 1;
			g_tri_buffer[g_d3d_triangle_count].vertex_index2 =
				g_d3d_vertex_count + 2;
			g_tri_buffer[g_d3d_triangle_count].texture = NULL;
			g_tri_buffer[g_d3d_triangle_count++].flags =
				render_flags;
			g_tri_buffer[g_d3d_triangle_count].vertex_index0 =
				g_d3d_vertex_count;
			g_tri_buffer[g_d3d_triangle_count].vertex_index1 =
				g_d3d_vertex_count + 2;
			g_tri_buffer[g_d3d_triangle_count].vertex_index2 =
				g_d3d_vertex_count + 3;
			g_tri_buffer[g_d3d_triangle_count].texture = NULL;
			g_tri_buffer[g_d3d_triangle_count++].flags =
				render_flags;
			g_d3d_vertex_count += QUAD_VERTEX_COUNT;
		}
		start = bottom - corner_height;
		end = bottom;
		if (start < 0) {
			start = 0;
		}
		if (end >= g_flight_vp_height) {
			end = g_flight_vp_height - 1;
		}
		if (end > start) {
			g_flight_vertex_buffer[g_d3d_vertex_count].sx =
				g_flight_vp_origin_x + (float)(x);
			g_flight_vertex_buffer[g_d3d_vertex_count].sy =
				g_flight_vp_origin_y + (float)(start);
			g_flight_vertex_buffer[g_d3d_vertex_count + 1].sx =
				g_flight_vp_origin_x + (float)(x);
			g_flight_vertex_buffer[g_d3d_vertex_count + 1].sy =
				g_flight_vp_origin_y + (float)(end);
			g_flight_vertex_buffer[g_d3d_vertex_count + 2].sx =
				g_flight_vp_origin_x + (float)(x + 1);
			g_flight_vertex_buffer[g_d3d_vertex_count + 2].sy =
				g_flight_vp_origin_y + (float)(end);
			g_flight_vertex_buffer[g_d3d_vertex_count + 3].sx =
				g_flight_vp_origin_x + (float)(x + 1);
			g_flight_vertex_buffer[g_d3d_vertex_count + 3].sy =
				g_flight_vp_origin_y + (float)(start);
			for (vertex_index = 0; vertex_index < QUAD_VERTEX_COUNT;
			     ++vertex_index) {
				g_flight_vertex_buffer[g_d3d_vertex_count +
						       vertex_index]
					.sz = depth_value;
				g_flight_vertex_buffer[g_d3d_vertex_count +
						       vertex_index]
					.rhw = depth_value;
				g_flight_vertex_buffer[g_d3d_vertex_count +
						       vertex_index]
					.tu = 0.0f;
				g_flight_vertex_buffer[g_d3d_vertex_count +
						       vertex_index]
					.tv = 0.0f;
				g_flight_vertex_buffer[g_d3d_vertex_count +
						       vertex_index]
					.color = color;
				g_flight_vertex_buffer[g_d3d_vertex_count +
						       vertex_index]
					.specular = 0;
			}
			g_tri_buffer[g_d3d_triangle_count].vertex_index0 =
				g_d3d_vertex_count;
			g_tri_buffer[g_d3d_triangle_count].vertex_index1 =
				g_d3d_vertex_count + 1;
			g_tri_buffer[g_d3d_triangle_count].vertex_index2 =
				g_d3d_vertex_count + 2;
			g_tri_buffer[g_d3d_triangle_count].texture = NULL;
			g_tri_buffer[g_d3d_triangle_count++].flags =
				render_flags;
			g_tri_buffer[g_d3d_triangle_count].vertex_index0 =
				g_d3d_vertex_count;
			g_tri_buffer[g_d3d_triangle_count].vertex_index1 =
				g_d3d_vertex_count + 2;
			g_tri_buffer[g_d3d_triangle_count].vertex_index2 =
				g_d3d_vertex_count + 3;
			g_tri_buffer[g_d3d_triangle_count].texture = NULL;
			g_tri_buffer[g_d3d_triangle_count++].flags =
				render_flags;
			g_d3d_vertex_count += QUAD_VERTEX_COUNT;
		}
	}

	if (right >= 0 && right < g_flight_vp_width) {
		start = box_top;
		end = box_top + corner_height;
		if (start < 0) {
			start = 0;
		}
		if (end >= g_flight_vp_height) {
			end = g_flight_vp_height - 1;
		}
		if (end > start) {
			g_flight_vertex_buffer[g_d3d_vertex_count].sx =
				g_flight_vp_origin_x + (float)(right);
			g_flight_vertex_buffer[g_d3d_vertex_count].sy =
				g_flight_vp_origin_y + (float)(start);
			g_flight_vertex_buffer[g_d3d_vertex_count + 1].sx =
				g_flight_vp_origin_x + (float)(right);
			g_flight_vertex_buffer[g_d3d_vertex_count + 1].sy =
				g_flight_vp_origin_y + (float)(end);
			g_flight_vertex_buffer[g_d3d_vertex_count + 2].sx =
				g_flight_vp_origin_x + (float)(right + 1);
			g_flight_vertex_buffer[g_d3d_vertex_count + 2].sy =
				g_flight_vp_origin_y + (float)(end);
			g_flight_vertex_buffer[g_d3d_vertex_count + 3].sx =
				g_flight_vp_origin_x + (float)(right + 1);
			g_flight_vertex_buffer[g_d3d_vertex_count + 3].sy =
				g_flight_vp_origin_y + (float)(start);
			for (vertex_index = 0; vertex_index < QUAD_VERTEX_COUNT;
			     ++vertex_index) {
				g_flight_vertex_buffer[g_d3d_vertex_count +
						       vertex_index]
					.sz = depth_value;
				g_flight_vertex_buffer[g_d3d_vertex_count +
						       vertex_index]
					.rhw = depth_value;
				g_flight_vertex_buffer[g_d3d_vertex_count +
						       vertex_index]
					.tu = 0.0f;
				g_flight_vertex_buffer[g_d3d_vertex_count +
						       vertex_index]
					.tv = 0.0f;
				g_flight_vertex_buffer[g_d3d_vertex_count +
						       vertex_index]
					.color = color;
				g_flight_vertex_buffer[g_d3d_vertex_count +
						       vertex_index]
					.specular = 0;
			}
			g_tri_buffer[g_d3d_triangle_count].vertex_index0 =
				g_d3d_vertex_count;
			g_tri_buffer[g_d3d_triangle_count].vertex_index1 =
				g_d3d_vertex_count + 1;
			g_tri_buffer[g_d3d_triangle_count].vertex_index2 =
				g_d3d_vertex_count + 2;
			g_tri_buffer[g_d3d_triangle_count].texture = NULL;
			g_tri_buffer[g_d3d_triangle_count++].flags =
				render_flags;
			g_tri_buffer[g_d3d_triangle_count].vertex_index0 =
				g_d3d_vertex_count;
			g_tri_buffer[g_d3d_triangle_count].vertex_index1 =
				g_d3d_vertex_count + 2;
			g_tri_buffer[g_d3d_triangle_count].vertex_index2 =
				g_d3d_vertex_count + 3;
			g_tri_buffer[g_d3d_triangle_count].texture = NULL;
			g_tri_buffer[g_d3d_triangle_count++].flags =
				render_flags;
			g_d3d_vertex_count += QUAD_VERTEX_COUNT;
		}
		start = bottom - corner_height;
		end = bottom;
		if (start < 0) {
			start = 0;
		}
		if (end >= g_flight_vp_height) {
			end = g_flight_vp_height - 1;
		}
		if (end > start) {
			g_flight_vertex_buffer[g_d3d_vertex_count].sx =
				g_flight_vp_origin_x + (float)(right);
			g_flight_vertex_buffer[g_d3d_vertex_count].sy =
				g_flight_vp_origin_y + (float)(start);
			g_flight_vertex_buffer[g_d3d_vertex_count + 1].sx =
				g_flight_vp_origin_x + (float)(right);
			g_flight_vertex_buffer[g_d3d_vertex_count + 1].sy =
				g_flight_vp_origin_y + (float)(end);
			g_flight_vertex_buffer[g_d3d_vertex_count + 2].sx =
				g_flight_vp_origin_x + (float)(right + 1);
			g_flight_vertex_buffer[g_d3d_vertex_count + 2].sy =
				g_flight_vp_origin_y + (float)(end);
			g_flight_vertex_buffer[g_d3d_vertex_count + 3].sx =
				g_flight_vp_origin_x + (float)(right + 1);
			g_flight_vertex_buffer[g_d3d_vertex_count + 3].sy =
				g_flight_vp_origin_y + (float)(start);
			for (vertex_index = 0; vertex_index < QUAD_VERTEX_COUNT;
			     ++vertex_index) {
				g_flight_vertex_buffer[g_d3d_vertex_count +
						       vertex_index]
					.sz = depth_value;
				g_flight_vertex_buffer[g_d3d_vertex_count +
						       vertex_index]
					.rhw = depth_value;
				g_flight_vertex_buffer[g_d3d_vertex_count +
						       vertex_index]
					.tu = 0.0f;
				g_flight_vertex_buffer[g_d3d_vertex_count +
						       vertex_index]
					.tv = 0.0f;
				g_flight_vertex_buffer[g_d3d_vertex_count +
						       vertex_index]
					.color = color;
				g_flight_vertex_buffer[g_d3d_vertex_count +
						       vertex_index]
					.specular = 0;
			}
			g_tri_buffer[g_d3d_triangle_count].vertex_index0 =
				g_d3d_vertex_count;
			g_tri_buffer[g_d3d_triangle_count].vertex_index1 =
				g_d3d_vertex_count + 1;
			g_tri_buffer[g_d3d_triangle_count].vertex_index2 =
				g_d3d_vertex_count + 2;
			g_tri_buffer[g_d3d_triangle_count].texture = NULL;
			g_tri_buffer[g_d3d_triangle_count++].flags =
				render_flags;
			g_tri_buffer[g_d3d_triangle_count].vertex_index0 =
				g_d3d_vertex_count;
			g_tri_buffer[g_d3d_triangle_count].vertex_index1 =
				g_d3d_vertex_count + 2;
			g_tri_buffer[g_d3d_triangle_count].vertex_index2 =
				g_d3d_vertex_count + 3;
			g_tri_buffer[g_d3d_triangle_count].texture = NULL;
			g_tri_buffer[g_d3d_triangle_count++].flags =
				render_flags;
			g_d3d_vertex_count += QUAD_VERTEX_COUNT;
		}
	}
}

/* Switches player_idx to HUD view hud_view_state and returns 1, or returns 0 for
 * the local player when g_hud_cockpit_resource_descriptors has no resource for
 * that view. Returns 1 at once when it is already the live view. Otherwise sets
 * viewState.hud_state_live and hud_state_mirror; for the local player it also
 * rebuilds the display with hud_rebuild_display_for_view_state, shows it with every
 * surface lock released, and resets the palette. Does not check hud_view_state
 * against the 28 descriptors. */
// FUNCTION: XVT 0x427720
int hud_set_hud_view_state(int hud_view_state, int player_idx)
{
	int local_player = g_local_player;
	if (player_idx == local_player &&
	    g_hud_cockpit_resource_descriptors[hud_view_state].resource_ref ==
		    0) {
		XVT_LOG_DEBUG(
			"hud.view_refused slot=%d view=%d current=%d predicted=%d",
			player_idx, hud_view_state,
			(int)g_players[player_idx].view_state.hud_state_live,
			g_flight_sim_side_effects_suppressed);
		return 0;
	}
	if (g_players[player_idx].view_state.hud_state_live == hud_view_state) {
		return 1;
	}
	XVT_LOG_DEBUG(
		"hud.view_state_set slot=%d view=%d from=%d last=%d local=%d predicted=%d",
		player_idx, hud_view_state,
		(int)g_players[player_idx].view_state.hud_state_live,
		(int)g_players[player_idx].view_state.hud_state_mirror,
		player_idx == local_player,
		g_flight_sim_side_effects_suppressed);

	g_players[player_idx].view_state.hud_state_live =
		(uint8_t)hud_view_state;
	if (player_idx == local_player) {
		flight_render_invoke_transition_hook(1);
		flight_surface_lock();
		hud_rebuild_display_for_view_state(hud_view_state, player_idx);
		flight_surface_unlock();

		int saved_lock_count = flight_surface_get_lock_count();
		if (saved_lock_count > 0) {
			for (int remaining_locks = saved_lock_count;
			     remaining_locks != 0; --remaining_locks) {
				flight_surface_unlock();
			}
		}
		flight_display_blit_render_surface();
		flight_display_flip();
		flight_display_blit_render_surface();
		if (saved_lock_count > 0) {
			do {
				flight_surface_lock();
				--saved_lock_count;
			} while (saved_lock_count != 0);
		}
		flight_render_reset_palette(1);
	}
	g_players[player_idx].view_state.hud_state_mirror =
		(uint8_t)hud_view_state;
	return 1;
}

/* Prepares the HUD of the local player for a full redraw; does nothing for
 * another player. Sets the MFD page blit rectangles (g_mfdGoalsBlit...,
 * g_mfdDamageBlit..., g_mfdCraftListBlit..., g_mfdMissionScoreboardBlit...,
 * g_mfdMapBlit...) for g_flight_resolution_mode, the craft list one from its
 * layout while map_camera_state is not 0. Sets every entry of
 * g_hud_element_state_cache to -2 (invalid, so it redraws), the MFD page elements
 * 117 and 130 to 134 of each set to -3 (inactive); sets layout 127's
 * clip_height_or_foreground_color to 1, g_hud_cached_target_object_idx to -1 and the
 * radar counts, parity and marker flag to 0. Then refreshes the craft system
 * indicators, draws the HUD (hud_render_hud) and its static text.
 * g_hud_full_redraw_in_progress is 1 while it runs. */
// FUNCTION: XVT 0x438A30
void hud_init_hud(int player_idx)
{
	enum {
		CRITICAL_WARNING_LAYOUT_INDEX = 127,
		HUD_ELEMENT_STATE_INVALID = -2,
		HUD_ELEMENT_STATE_INACTIVE = -3,
	};

	g_hud_full_redraw_in_progress = 1;
	if (g_local_player == player_idx) {
		switch (g_flight_resolution_mode) {
		case FLIGHT_RESOLUTION_320X240:
			g_mfd_goals_blit_source_x = 6;
			g_mfd_goals_blit_width = 141;
			g_mfd_damage_blit_source_y = 103;
			g_mfd_damage_blit_width = 100;
			g_mfd_damage_blit_height = 80;
			g_mfd_goals_blit_source_y = 53;
			g_mfd_craft_list_blit_height = 47;
			g_mfd_mission_scoreboard_blit_source_x = 2;
			g_mfd_mission_scoreboard_blit_source_y = 109;
			g_mfd_goals_blit_height = 53;
			g_mfd_mission_scoreboard_blit_height = 70;
			g_mfd_map_blit_source_x = 121;
			g_mfd_map_blit_source_y = 186;
			g_mfd_map_blit_width = 111;
			g_mfd_map_blit_height = 52;
			g_mfd_damage_blit_source_x = 153;
			g_mfd_craft_list_blit_source_x = 153;
			g_mfd_craft_list_blit_source_y = 53;
			g_mfd_craft_list_blit_width = 112;
			g_mfd_mission_scoreboard_blit_width = 112;
			break;
		case FLIGHT_RESOLUTION_640X480:
			g_mfd_goals_blit_source_x = 12;
			g_mfd_goals_blit_width = 282;
			g_mfd_damage_blit_source_y = 206;
			g_mfd_damage_blit_width = 200;
			g_mfd_damage_blit_height = 160;
			g_mfd_goals_blit_source_y = 107;
			g_mfd_craft_list_blit_width = 225;
			g_mfd_craft_list_blit_height = 94;
			g_mfd_mission_scoreboard_blit_source_x = 4;
			g_mfd_mission_scoreboard_blit_source_y = 219;
			g_mfd_mission_scoreboard_blit_width = 224;
			g_mfd_mission_scoreboard_blit_height = 140;
			g_mfd_map_blit_source_x = 242;
			g_mfd_map_blit_source_y = 373;
			g_mfd_map_blit_width = 223;
			g_mfd_map_blit_height = 104;
			g_mfd_goals_blit_height = 107;
			g_mfd_damage_blit_source_x = 306;
			g_mfd_craft_list_blit_source_x = 306;
			g_mfd_craft_list_blit_source_y = 107;
			break;
		case FLIGHT_RESOLUTION_480X360:
			g_mfd_goals_blit_source_x = 9;
			g_mfd_goals_blit_width = 211;
			g_mfd_damage_blit_source_y = 154;
			g_mfd_damage_blit_width = 150;
			g_mfd_damage_blit_height = 120;
			g_mfd_goals_blit_source_y = 80;
			g_mfd_craft_list_blit_height = 70;
			g_mfd_mission_scoreboard_blit_source_x = 3;
			g_mfd_mission_scoreboard_blit_source_y = 164;
			g_mfd_goals_blit_height = 80;
			g_mfd_mission_scoreboard_blit_height = 105;
			g_mfd_map_blit_source_x = 181;
			g_mfd_map_blit_source_y = 279;
			g_mfd_map_blit_width = 167;
			g_mfd_map_blit_height = 78;
			g_mfd_damage_blit_source_x = 229;
			g_mfd_craft_list_blit_source_x = 229;
			g_mfd_craft_list_blit_source_y = 80;
			g_mfd_craft_list_blit_width = 168;
			g_mfd_mission_scoreboard_blit_width = 168;
			break;
		default:
			break;
		}

		if (g_players[g_local_player].map_camera_state != 0) {
			g_mfd_craft_list_blit_width =
				g_hud_element_layouts
					[g_hud_instrument_set_base_index +
					 HUD_MFD_CRAFT_LIST_ELEMENT]
						.clip_width;
			g_mfd_craft_list_blit_height =
				g_hud_element_layouts
					[g_hud_instrument_set_base_index +
					 HUD_MFD_CRAFT_LIST_ELEMENT]
						.clip_height_or_foreground_color;
		}

		for (uint16_t instrument_set = 0;
		     instrument_set < HUD_INSTRUMENT_SET_COUNT;
		     ++instrument_set) {
			for (uint16_t instrument_index = 0;
			     instrument_index < HUD_INSTRUMENTS_PER_SET;
			     ++instrument_index) {
				if (instrument_index >=
					    HUD_MFD_CRAFT_LIST_ELEMENT &&
				    instrument_index <=
					    HUD_MFD_SCOREBOARD_ELEMENT) {
					g_hud_element_state_cache
						[HUD_INSTRUMENTS_PER_SET *
							 instrument_set +
						 instrument_index] =
							HUD_ELEMENT_STATE_INACTIVE;
				} else {
					g_hud_element_state_cache
						[HUD_INSTRUMENTS_PER_SET *
							 instrument_set +
						 instrument_index] =
							HUD_ELEMENT_STATE_INVALID;
				}
				if (instrument_index ==
				    HUD_MFD_MESSAGE_LOG_ELEMENT) {
					g_hud_element_state_cache
						[HUD_INSTRUMENTS_PER_SET *
							 instrument_set +
						 instrument_index] =
							HUD_ELEMENT_STATE_INACTIVE;
				}
			}
		}

		g_hud_element_layouts[CRITICAL_WARNING_LAYOUT_INDEX]
			.clip_height_or_foreground_color = 1;
		g_hud_cached_target_object_idx = -1;
		g_radar_fore_blip_count = 0;
		g_radar_target_marker_background_saved = 0;
		g_radar_aft_blip_count = 0;
		g_radar_blip_buffer_parity = 0;
		hud_clear_unavailable_craft_system_indicators();
		if (g_flight_sim_side_effects_suppressed == 0) {
			hud_update_craft_system_status_indicators();
		}
		hud_render_hud(g_local_player);
		hud_draw_static_cockpit_text(g_local_player);
		hud_init_hud_end_stub(g_local_player);
		XVT_LOG_DEBUG(
			"hud.reset slot=%d view=%d set=%u map=%d list_width=%u list_height=%u lights=%d",
			g_local_player,
			(int)g_players[g_local_player]
				.view_state.hud_state_live,
			(unsigned)g_hud_instrument_set_base_index,
			(int)g_players[g_local_player].map_camera_state,
			(unsigned)g_mfd_craft_list_blit_width,
			(unsigned)g_mfd_craft_list_blit_height,
			g_flight_sim_side_effects_suppressed == 0);
	}
	g_hud_full_redraw_in_progress = 0;
}

/* Draws the local player's HUD for the current view; another player gets
 * nothing drawn. While awaiting a new craft or with the mission ending, it only
 * stops the incoming missile warning. In the map view it draws the map overlay;
 * in the forward cockpit hud_update_hud, in the HUD-only view
 * hud_update_hud_only_view, in the target camera hud_update_cmd_text, in any other
 * view the MFD pages; views other than the forward and HUD-only ones also call
 * fsfx_update_targeting_tone(0) to stop the targeting tone. Every view but the
 * first case then updates the critical hull and shield warning. The modern
 * build records the instruments for its renderer around it. */
// FUNCTION: XVT 0x438DF0
void hud_render_hud(int player_idx)
{
#ifdef XVT_MODERN
	xvt_cockpit_instruments_begin_update(player_idx);
#endif

	if (player_idx == g_local_player) {
		if (g_players[g_local_player].awaiting_new_craft != 0 ||
		    g_flight_mission_state.mission_end_pending != 0) {
			flight_surface_unlock();
			fsfx_update_incoming_missile_warning(0);
			flight_surface_lock();
		} else if (g_players[player_idx].map_camera_state != 0) {
			hud_draw_map_view_overlay();
			flight_surface_unlock();
			fsfx_update_targeting_tone(0);
			flight_surface_lock();
			hud_update_critical_hull_shield_warning();
		} else {
			uint8_t hud_state = g_players[g_local_player]
						    .view_state.hud_state_live;
			if (hud_state == HUD_VIEW_FORWARD) {
				hud_update_hud();
				hud_update_critical_hull_shield_warning();
			} else if (hud_state == HUD_VIEW_HUD_ONLY) {
				hud_update_hud_only_view();
				hud_update_critical_hull_shield_warning();
			} else if (hud_state == HUD_VIEW_TARGET_CAMERA) {
				hud_update_cmd_text();
				flight_surface_unlock();
				fsfx_update_targeting_tone(0);
				flight_surface_lock();
				hud_update_critical_hull_shield_warning();
			} else {
				flight_surface_unlock();
				fsfx_update_targeting_tone(0);
				hud_update_mfd_pages();
				flight_surface_lock();
				hud_update_critical_hull_shield_warning();
			}
		}
	}
#ifdef XVT_MODERN
	xvt_cockpit_refresh_instruments(player_idx);
#endif
}

/* Draws the 3D image of the local player's target into the target inset (layout
 * 2 of the current set) through hud_update3d_crt, passing
 * g_hud_target_inset_mask_refresh_pending. Draws nothing while awaiting a new craft
 * or with the mission ending, or without a target. In the map view that is all
 * it needs; otherwise only the forward and HUD-only views draw it, and only
 * while bit 0 of the craft's active_hud_feature_mask, the targeting computer, is
 * set. */
// FUNCTION: XVT 0x438EF0
void hud_draw_hud_target_inset_if_enabled(int player_index)
{
	enum {
		TARGET_INSET_LAYOUT_INDEX = 2,
		INVALID_TARGET_OBJECT_INDEX = -1,
		TARGET_INSET_FEATURE_MASK = 1,
	};

#ifdef XVT_MODERN
	xvt_render_draw_scope(XVT_SCOPE_COCKPIT);
#endif

	if (player_index == g_local_player &&
	    g_players[g_local_player].awaiting_new_craft == 0 &&
	    g_flight_mission_state.mission_end_pending == 0) {
		if (g_players[player_index].map_camera_state != 0) {
			if (g_players[player_index].current_target_object_idx ==
			    INVALID_TARGET_OBJECT_INDEX) {
				return;
			}
			hud_update3d_crt(
				g_hud_element_layouts
					[g_hud_instrument_set_base_index +
					 TARGET_INSET_LAYOUT_INDEX]
						.x,
				g_hud_element_layouts
					[g_hud_instrument_set_base_index +
					 TARGET_INSET_LAYOUT_INDEX]
						.y,
				g_hud_element_layouts
					[g_hud_instrument_set_base_index +
					 TARGET_INSET_LAYOUT_INDEX]
						.selector,
				g_hud_element_layouts
					[g_hud_instrument_set_base_index +
					 TARGET_INSET_LAYOUT_INDEX]
						.color_index_or_widget_param,
				g_hud_target_inset_mask_refresh_pending);
			return;
		} else {
			uint8_t hud_state = g_players[player_index]
						    .view_state.hud_state_live;
			if (hud_state != HUD_VIEW_FORWARD &&
			    hud_state != HUD_VIEW_HUD_ONLY) {
				return;
			}
			if (g_players[player_index].current_target_object_idx ==
			    INVALID_TARGET_OBJECT_INDEX) {
				return;
			}
			if ((g_object_table[g_players[player_index]
						    .object_index]
				     .mobj->p_craft->damage_stats
				     .active_hud_feature_mask &
			     TARGET_INSET_FEATURE_MASK) == 0) {
				return;
			}
		}

		hud_update3d_crt(
			g_hud_element_layouts[g_hud_instrument_set_base_index +
					      TARGET_INSET_LAYOUT_INDEX]
				.x,
			g_hud_element_layouts[g_hud_instrument_set_base_index +
					      TARGET_INSET_LAYOUT_INDEX]
				.y,
			g_hud_element_layouts[g_hud_instrument_set_base_index +
					      TARGET_INSET_LAYOUT_INDEX]
				.selector,
			g_hud_element_layouts[g_hud_instrument_set_base_index +
					      TARGET_INSET_LAYOUT_INDEX]
				.color_index_or_widget_param,
			g_hud_target_inset_mask_refresh_pending);
	}
}

/* Draws the labels that stay put on the local player's cockpit or HUD-only
 * view; nothing in other views or for another player. The power labels (L, S,
 * E, B) at layouts 122 to 125 show when their layout is set and the craft has
 * the matching HUD feature and, for S and B, shields or a beam system. With HUD
 * feature 0x40 it draws the speed and throttle labels (the short forms when the
 * layout's selector is 4 or less) and the throttle's "%"; with 0x20 the F and R
 * shield labels. Then the craft's name and status, centered in layout 126, and
 * in the forward view the mission clock's ":" at layout 46. Leaves
 * g_flight_text_shadow_enabled at 0 in the forward view and font tier 0 or 2. */
// FUNCTION: XVT 0x439030
void hud_draw_static_cockpit_text(uint16_t player_idx)
{
	if (g_local_player != player_idx) {
		return;
	}
	if (g_players[player_idx].view_state.hud_state_live !=
		    HUD_VIEW_FORWARD &&
	    g_players[player_idx].view_state.hud_state_live !=
		    HUD_VIEW_HUD_ONLY) {
		return;
	}
	struct craft_data *craft =
		g_object_table[g_players[g_local_player].object_index]
			.mobj->p_craft;
	uint16_t system_flags = craft->system_flags;
	uint16_t feature_mask = craft->damage_stats.active_hud_feature_mask;

	flight_text_set_font_tier(0);
	flight_text_set_color(0x2F);
	uint16_t layout_index;
	for (layout_index = 122; layout_index < 126; ++layout_index) {
		if (g_hud_element_layouts[g_hud_instrument_set_base_index +
					  layout_index]
			    .selector == 0) {
			continue;
		}
		switch (layout_index) {
		case 122:
			if ((feature_mask & 0x0200) == 0) {
				continue;
			}
			break;
		case 123:
			if ((system_flags & CRAFT_SUBSYSTEM_FLAG_SHIELDS) ==
				    0 ||
			    (feature_mask & 0x0800) == 0) {
				continue;
			}
			break;
		case 124:
			if ((feature_mask & 0x0400) == 0) {
				continue;
			}
			break;
		case 125:
			if ((system_flags & CRAFT_SUBSYSTEM_FLAG_BEAM_SYSTEM) ==
				    0 ||
			    (feature_mask & 0x1000) == 0) {
				continue;
			}
			break;
		}
		flight_text_set_background_color(
			g_hud_element_layouts[g_hud_instrument_set_base_index +
					      layout_index]
				.color_index_or_widget_param);
		flight_text_set_cursor(
			g_hud_element_layouts[g_hud_instrument_set_base_index +
					      layout_index]
				.x,
			g_hud_element_layouts[g_hud_instrument_set_base_index +
					      layout_index]
				.y);
		flight_text_set_clip_rect(
			g_hud_element_layouts[g_hud_instrument_set_base_index +
					      layout_index]
				.x,
			g_hud_element_layouts[g_hud_instrument_set_base_index +
					      layout_index]
				.y,
			g_hud_element_layouts[g_hud_instrument_set_base_index +
					      layout_index]
					.x +
				g_hud_element_layouts
					[g_hud_instrument_set_base_index +
					 layout_index]
						.clip_width,
			g_hud_element_layouts[g_hud_instrument_set_base_index +
					      layout_index]
					.y +
				g_flight_font_line_height);
#ifdef XVT_MODERN
		xvt_cockpit_text_record_field(
			(xvt_cockpit_text_field_id)(XVT_COCKPIT_TEXT_POWER_LABEL_FIRST +
						    layout_index - 122),
			g_str_cockpit_overlay_text[layout_index - 122 +
						   COCKPIT_OVERLAY_STR_L],
			XVT_COCKPIT_ALIGN_LEFT);
#endif
		flight_text_draw_string(
			g_str_cockpit_overlay_text[layout_index - 122 +
						   COCKPIT_OVERLAY_STR_L]);
	}

	if ((feature_mask & 0x40) != 0) {
		if (g_hud_element_layouts[g_hud_instrument_set_base_index + 121]
			    .selector != 0) {
			if (g_hud_instrument_set_base_index !=
			    HUD_COCKPIT_INSTRUMENT_BASE_INDEX) {
				flight_text_set_color(0x4A);
			}
			flight_text_set_background_color(
				g_hud_element_layouts
					[g_hud_instrument_set_base_index + 121]
						.color_index_or_widget_param);
			flight_text_set_cursor(
				g_hud_element_layouts
					[g_hud_instrument_set_base_index + 121]
						.x,
				g_hud_element_layouts
					[g_hud_instrument_set_base_index + 121]
						.y);
			flight_text_set_clip_rect(
				g_hud_element_layouts
					[g_hud_instrument_set_base_index + 121]
						.x,
				g_hud_element_layouts
					[g_hud_instrument_set_base_index + 121]
						.y,
				g_hud_element_layouts
						[g_hud_instrument_set_base_index +
						 121]
							.x +
					g_hud_element_layouts
						[g_hud_instrument_set_base_index +
						 121]
							.clip_width,
				g_hud_element_layouts
						[g_hud_instrument_set_base_index +
						 121]
							.y +
					g_flight_font_line_height);
			if (g_hud_element_layouts
				    [g_hud_instrument_set_base_index + 121]
					    .selector <= 4) {
#ifdef XVT_MODERN
				xvt_cockpit_text_record_field(
					XVT_COCKPIT_TEXT_SPEED_LABEL,
					g_str_cockpit_overlay_text
						[COCKPIT_OVERLAY_STR_SPD],
					XVT_COCKPIT_ALIGN_LEFT);
#endif
				flight_text_draw_string(
					g_str_cockpit_overlay_text
						[COCKPIT_OVERLAY_STR_SPD]);
			} else {
#ifdef XVT_MODERN
				xvt_cockpit_text_record_field(
					XVT_COCKPIT_TEXT_SPEED_LABEL,
					g_str_cockpit_overlay_text
						[COCKPIT_OVERLAY_STR_SPEED],
					XVT_COCKPIT_ALIGN_LEFT);
#endif
				flight_text_draw_string(
					g_str_cockpit_overlay_text
						[COCKPIT_OVERLAY_STR_SPEED]);
			}
		}
		if (g_hud_element_layouts[g_hud_instrument_set_base_index + 120]
			    .selector != 0) {
			if (g_hud_instrument_set_base_index !=
			    HUD_COCKPIT_INSTRUMENT_BASE_INDEX) {
				flight_text_set_color(0x4A);
			}
			flight_text_set_background_color(
				g_hud_element_layouts
					[g_hud_instrument_set_base_index + 120]
						.color_index_or_widget_param);
			flight_text_set_cursor(
				g_hud_element_layouts
					[g_hud_instrument_set_base_index + 120]
						.x,
				g_hud_element_layouts
					[g_hud_instrument_set_base_index + 120]
						.y);
			flight_text_set_clip_rect(
				g_hud_element_layouts
					[g_hud_instrument_set_base_index + 120]
						.x,
				g_hud_element_layouts
					[g_hud_instrument_set_base_index + 120]
						.y,
				g_hud_element_layouts
						[g_hud_instrument_set_base_index +
						 120]
							.x +
					g_hud_element_layouts
						[g_hud_instrument_set_base_index +
						 120]
							.clip_width,
				g_hud_element_layouts
						[g_hud_instrument_set_base_index +
						 120]
							.y +
					g_flight_font_line_height);
			if (g_hud_element_layouts
				    [g_hud_instrument_set_base_index + 120]
					    .selector <= 4) {
#ifdef XVT_MODERN
				xvt_cockpit_text_record_field(
					XVT_COCKPIT_TEXT_THROTTLE_LABEL,
					g_str_cockpit_overlay_text
						[COCKPIT_OVERLAY_STR_THTL],
					XVT_COCKPIT_ALIGN_LEFT);
#endif
				flight_text_draw_string(
					g_str_cockpit_overlay_text
						[COCKPIT_OVERLAY_STR_THTL]);
			} else {
#ifdef XVT_MODERN
				xvt_cockpit_text_record_field(
					XVT_COCKPIT_TEXT_THROTTLE_LABEL,
					g_str_cockpit_overlay_text
						[COCKPIT_OVERLAY_STR_THROTTLE],
					XVT_COCKPIT_ALIGN_LEFT);
#endif
				flight_text_draw_string(
					g_str_cockpit_overlay_text
						[COCKPIT_OVERLAY_STR_THROTTLE]);
			}
		}
		if (g_hud_instrument_set_base_index !=
		    HUD_COCKPIT_INSTRUMENT_BASE_INDEX) {
			flight_text_set_font_tier(0);
		} else {
			flight_text_set_font_tier(2);
		}
		uint16_t text_width = flight_text_measure_string_width(
			g_three_digit_width_text);
		flight_text_set_color(0x4A);
		flight_text_set_background_color(0x2C);
		flight_text_set_clip_rect(
			g_hud_element_layouts[g_hud_instrument_set_base_index +
					      41]
					.x +
				text_width,
			g_hud_element_layouts[g_hud_instrument_set_base_index +
					      41]
				.y,
			g_hud_element_layouts[g_hud_instrument_set_base_index +
					      41]
					.x +
				text_width +
				flight_text_measure_string_width("%"),
			g_hud_element_layouts[g_hud_instrument_set_base_index +
					      41]
					.y +
				g_flight_font_line_height);
		flight_text_set_cursor(
			g_hud_element_layouts[g_hud_instrument_set_base_index +
					      41]
					.x +
				text_width,
			g_hud_element_layouts[g_hud_instrument_set_base_index +
					      41]
				.y);
#ifdef XVT_MODERN
		xvt_cockpit_text_record_field(XVT_COCKPIT_TEXT_THROTTLE_PERCENT,
					      "%", XVT_COCKPIT_ALIGN_LEFT);
#endif
		g_flight_draw_char_fn('%');
	}

	if ((feature_mask & 0x20) != 0) {
		for (layout_index = 0; layout_index <= 1; ++layout_index) {
			if (g_hud_element_layouts
				    [g_hud_instrument_set_base_index + 128 +
				     layout_index]
					    .selector == 0) {
				continue;
			}
			flight_text_set_clip_rect(
				g_hud_element_layouts
					[g_hud_instrument_set_base_index + 128 +
					 layout_index]
						.x,
				g_hud_element_layouts
					[g_hud_instrument_set_base_index + 128 +
					 layout_index]
						.y,
				g_hud_element_layouts
						[g_hud_instrument_set_base_index +
						 128 + layout_index]
							.x +
					g_hud_element_layouts
						[g_hud_instrument_set_base_index +
						 128 + layout_index]
							.clip_width,
				g_hud_element_layouts
						[g_hud_instrument_set_base_index +
						 128 + layout_index]
							.y +
					g_flight_font_line_height);
			flight_text_set_cursor(
				g_hud_element_layouts
					[g_hud_instrument_set_base_index + 128 +
					 layout_index]
						.x,
				g_hud_element_layouts
					[g_hud_instrument_set_base_index + 128 +
					 layout_index]
						.y);
			flight_text_set_color(
				g_hud_element_layouts
					[g_hud_instrument_set_base_index + 128 +
					 layout_index]
						.clip_height_or_foreground_color);
			flight_text_set_background_color(
				g_hud_element_layouts
					[g_hud_instrument_set_base_index + 128 +
					 layout_index]
						.color_index_or_widget_param);
#ifdef XVT_MODERN
			xvt_cockpit_text_record_field(
				(xvt_cockpit_text_field_id)(XVT_COCKPIT_TEXT_SHIELD_LABEL_FIRST +
							    layout_index),
				g_str_cockpit_overlay_text
					[layout_index + COCKPIT_OVERLAY_STR_F],
				XVT_COCKPIT_ALIGN_LEFT);
#endif
			flight_text_draw_string(
				g_str_cockpit_overlay_text
					[layout_index + COCKPIT_OVERLAY_STR_F]);
		}
	}

	flight_text_set_background_color(0x2C);
	flight_text_set_font_tier(0);
	if (g_flight_resolution_mode == FLIGHT_RESOLUTION_320X240) {
		hud_format_object_display_name(
			(uint16_t)g_players[g_local_player].object_index, 7);
	} else {
		hud_format_object_display_name(
			(uint16_t)g_players[g_local_player].object_index, 3);
	}
	flight_text_set_cursor(
		g_hud_element_layouts[g_hud_instrument_set_base_index + 126].x,
		g_hud_element_layouts[g_hud_instrument_set_base_index + 126].y);
	flight_text_set_clip_rect(
		g_hud_element_layouts[g_hud_instrument_set_base_index + 126].x,
		g_hud_element_layouts[g_hud_instrument_set_base_index + 126].y,
		g_hud_element_layouts[g_hud_instrument_set_base_index + 126].x +
			g_hud_element_layouts[g_hud_instrument_set_base_index +
					      126]
				.clip_width,
		g_hud_element_layouts[g_hud_instrument_set_base_index + 126].y +
			g_hud_element_layouts[g_hud_instrument_set_base_index +
					      126]
				.clip_height_or_foreground_color);
	g_flight_fill_clip_rect_fn();
#ifdef XVT_MODERN
	xvt_cockpit_text_record_field(XVT_COCKPIT_TEXT_CRAFT_STATUS,
				      g_flight_text_scratch_buffer,
				      XVT_COCKPIT_ALIGN_CENTER);
#endif
	flight_text_draw_string_centered(g_flight_text_scratch_buffer);
	if (g_players[player_idx].view_state.hud_state_live ==
	    HUD_VIEW_FORWARD) {
		flight_text_set_font_tier(2);
		flight_text_set_clip_rect(0, 0, g_screen_width,
					  g_screen_height);
		flight_text_set_background_color(0x40);
		if (g_flight_resolution_mode == FLIGHT_RESOLUTION_320X240) {
			flight_text_set_color(0x4D);
		} else {
			flight_text_set_color(0x4E);
		}
		g_flight_text_shadow_enabled = 0;
		flight_text_set_cursor(
			g_hud_element_layouts[46].x +
				flight_text_measure_string_width("00"),
			g_hud_element_layouts[46].y);
#ifdef XVT_MODERN
		xvt_cockpit_text_record_field(XVT_COCKPIT_TEXT_CLOCK_SEPARATOR,
					      ":", XVT_COCKPIT_ALIGN_LEFT);
#endif
		g_flight_draw_char_fn(':');
	}
}

/* Does nothing; hud_init_hud calls it last. */
// FUNCTION: XVT 0x439700
void hud_init_hud_end_stub(int player_idx) { (void)player_idx; }

/* Updates the forward cockpit view's instruments for one frame: radar, reticle,
 * lock indicator, targeting computer, warheads, shields, beam, mission clock,
 * speed, throttle, power settings, threats, countermeasures, MFD pages and the
 * craft name and status line. In an X-wing, Y-wing, A-wing, Z-95 or B-wing it
 * also draws the shield distribution sprite (layout 51) with HUD feature 0x20
 * and the S-foil sprite (layout 45), state 1 while the 0x2 bit of s_foil_state
 * is clear, each when its layout is placed. */
// FUNCTION: XVT 0x439710
void hud_update_hud(void)
{
	enum {
		SHIELD_DISTRIBUTION_ELEMENT = 51,
		S_FOIL_STATE_ELEMENT = 45,
		SHIELD_DISPLAY_FEATURE_MASK = 0x20,
		S_FOIL_CLOSED_MASK = 2,
	};

	flight_text_set_font_tier(2);
	hud_draw_radar_blips();
	hud_draw_laser_cannon_indicators();
	hud_update_targeting_lock_indicator();
	hud_update_targeting_computer_display();
	hud_update_warhead_cnt();
	hud_draw_shield_strength2d();
	hud_draw_beam_strength2d();
	hud_update_mission_clock_display();

	int object_index = g_players[g_local_player].object_index;
	int is_rebel_fighter = object_index != -1 &&
			       (g_object_table[object_index].object_type ==
					CRAFT_SPECIES_X_WING ||
				g_object_table[object_index].object_type ==
					CRAFT_SPECIES_Y_WING ||
				g_object_table[object_index].object_type ==
					CRAFT_SPECIES_A_WING ||
				g_object_table[object_index].object_type ==
					CRAFT_SPECIES_Z_95_HEADHUNTER ||
				g_object_table[object_index].object_type ==
					CRAFT_SPECIES_B_WING);
	if (is_rebel_fighter) {
		struct craft_data *craft =
			g_object_table[object_index].mobj->p_craft;
		if ((craft->damage_stats.active_hud_feature_mask &
		     SHIELD_DISPLAY_FEATURE_MASK) != 0 &&
		    (uint16_t)g_hud_element_layouts[SHIELD_DISTRIBUTION_ELEMENT]
					    .x +
				    (uint16_t)g_hud_element_layouts
					    [SHIELD_DISTRIBUTION_ELEMENT]
						    .y !=
			    0) {
			hud_draw_cached_sprite_element(
				SHIELD_DISTRIBUTION_ELEMENT,
				(uint8_t)craft->shield_distrib_mode);
		}
	}

	hud_update_speed_percent();
	hud_update_throttle_percent();
	hud_draw_power_settings2d();
	hud_update_threat_indicators(0);
	hud_update_countermeasure_status();

	object_index = g_players[g_local_player].object_index;
	is_rebel_fighter = object_index != -1 &&
			   (g_object_table[object_index].object_type ==
				    CRAFT_SPECIES_X_WING ||
			    g_object_table[object_index].object_type ==
				    CRAFT_SPECIES_Y_WING ||
			    g_object_table[object_index].object_type ==
				    CRAFT_SPECIES_A_WING ||
			    g_object_table[object_index].object_type ==
				    CRAFT_SPECIES_Z_95_HEADHUNTER ||
			    g_object_table[object_index].object_type ==
				    CRAFT_SPECIES_B_WING);
	if (is_rebel_fighter &&
	    g_hud_element_layouts[S_FOIL_STATE_ELEMENT].x != 0) {
		uint16_t s_foil_indicator_state =
			(g_object_table[object_index]
				 .mobj->p_craft->s_foil_state &
			 S_FOIL_CLOSED_MASK) == 0;
		hud_draw_cached_sprite_element(S_FOIL_STATE_ELEMENT,
					       s_foil_indicator_state);
	}

	hud_update_mfd_pages();
	hud_draw_craft_name_fps_and_network_status();
}

/* Updates the HUD-only view's instruments for one frame: as hud_update_hud
 * without the mission clock, countermeasures, shield distribution and S-foils,
 * and with the threat indicators in mode 1. */
// FUNCTION: XVT 0x4398B0
void hud_update_hud_only_view(void)
{
	hud_draw_radar_blips();
	hud_draw_laser_cannon_indicators();
	hud_update_targeting_lock_indicator();
	hud_update_warhead_cnt();
	hud_update_threat_indicators(1);
	flight_text_set_font_tier(2);
	hud_update_targeting_computer_display();
	hud_draw_shield_strength2d();
	hud_draw_beam_strength2d();
	hud_update_speed_percent();
	hud_update_throttle_percent();
	hud_draw_power_settings2d();
	hud_update_mfd_pages();
	hud_draw_craft_name_fps_and_network_status();
}

/* Updates the map view's text for the local player: the targeting computer and
 * MFD pages every call, and the "following" and "tracking" lines (layouts 120
 * and 121 of the current set) when the camera focus object or aim target
 * changed since g_hud_element_state_cache last recorded it. Each line is redrawn
 * centered on a cleared field with the object's name, or dashes for none, and
 * the new index is recorded. */
// FUNCTION: XVT 0x439900
void hud_draw_map_view_overlay(void)
{
	enum {
		CAMERA_FOCUS_LAYOUT = 120,
		AIM_TARGET_LAYOUT = 121,
		DEFAULT_SCREEN_WIDTH = 320,
		DEFAULT_SCREEN_HEIGHT = 200,
		OBJECT_DISPLAY_FLAGS = 3,
	};

	hud_update_targeting_computer_display();
	hud_update_mfd_pages();
	uint16_t object_idx;
	const char *object_name;
	char text[80];
	if ((uint16_t)
		    g_hud_element_state_cache[g_hud_instrument_set_base_index +
					      CAMERA_FOCUS_LAYOUT] !=
	    g_players[g_local_player].view_state.camera_focus_obj_idx) {
		flight_surface_lock();
		flight_sw_set_render_target(NULL, DEFAULT_SCREEN_WIDTH,
					    DEFAULT_SCREEN_HEIGHT, 0);
		flight_text_set_font_tier(2);
		flight_text_set_background_color(
			g_flight_transparent_color_index);
		flight_text_set_color('N');
		flight_text_set_clip_rect(
			g_hud_element_layouts[g_hud_instrument_set_base_index +
					      CAMERA_FOCUS_LAYOUT]
				.x,
			g_hud_element_layouts[g_hud_instrument_set_base_index +
					      CAMERA_FOCUS_LAYOUT]
				.y,
			g_hud_element_layouts[g_hud_instrument_set_base_index +
					      CAMERA_FOCUS_LAYOUT]
					.x +
				g_hud_element_layouts
					[g_hud_instrument_set_base_index +
					 CAMERA_FOCUS_LAYOUT]
						.clip_width,
			g_hud_element_layouts[g_hud_instrument_set_base_index +
					      CAMERA_FOCUS_LAYOUT]
					.y +
				g_hud_element_layouts
					[g_hud_instrument_set_base_index +
					 CAMERA_FOCUS_LAYOUT]
						.clip_height_or_foreground_color);
		flight_text_set_cursor(
			g_hud_element_layouts[g_hud_instrument_set_base_index +
					      CAMERA_FOCUS_LAYOUT]
				.x,
			g_hud_element_layouts[g_hud_instrument_set_base_index +
					      CAMERA_FOCUS_LAYOUT]
				.y);
		g_flight_fill_clip_rect_fn();

		object_idx = g_players[g_local_player]
				     .view_state.camera_focus_obj_idx;
		if (object_idx != UINT16_MAX) {
			hud_format_object_display_name(object_idx,
						       OBJECT_DISPLAY_FLAGS);
			object_name = g_flight_text_scratch_buffer;
		} else {
			object_name = g_str_mesh_component_names
				[MESH_COMPONENT_32_DASHES];
		}
		strcpy(text, object_name);
		flight_text_set_scratch(
			g_str_map_room_text[MAP_ROOM_STR_FOLLOWING]);
		flight_text_append_scratch_char(' ');
		flight_text_append_scratch_string(text);
#ifdef XVT_MODERN
		xvt_cockpit_text_record_field(XVT_COCKPIT_TEXT_MAP_FOLLOWING,
					      g_flight_text_scratch_buffer,
					      XVT_COCKPIT_ALIGN_CENTER);
#endif
		flight_text_draw_string_centered(g_flight_text_scratch_buffer);
		flight_surface_unlock();
		g_hud_element_state_cache[g_hud_instrument_set_base_index +
					  CAMERA_FOCUS_LAYOUT] =
			g_players[g_local_player]
				.view_state.camera_focus_obj_idx;
	}

	if ((uint16_t)
		    g_hud_element_state_cache[g_hud_instrument_set_base_index +
					      AIM_TARGET_LAYOUT] !=
	    g_players[g_local_player].view_state.aim_target_idx) {
		flight_surface_lock();
		flight_sw_set_render_target(NULL, DEFAULT_SCREEN_WIDTH,
					    DEFAULT_SCREEN_HEIGHT, 0);
		flight_text_set_font_tier(2);
		flight_text_set_background_color(
			g_flight_transparent_color_index);
		flight_text_set_color('R');
		flight_text_set_clip_rect(
			g_hud_element_layouts[g_hud_instrument_set_base_index +
					      AIM_TARGET_LAYOUT]
				.x,
			g_hud_element_layouts[g_hud_instrument_set_base_index +
					      AIM_TARGET_LAYOUT]
				.y,
			g_hud_element_layouts[g_hud_instrument_set_base_index +
					      AIM_TARGET_LAYOUT]
					.x +
				g_hud_element_layouts
					[g_hud_instrument_set_base_index +
					 AIM_TARGET_LAYOUT]
						.clip_width,
			g_hud_element_layouts[g_hud_instrument_set_base_index +
					      AIM_TARGET_LAYOUT]
					.y +
				g_hud_element_layouts
					[g_hud_instrument_set_base_index +
					 AIM_TARGET_LAYOUT]
						.clip_height_or_foreground_color);
		flight_text_set_cursor(
			g_hud_element_layouts[g_hud_instrument_set_base_index +
					      AIM_TARGET_LAYOUT]
				.x,
			g_hud_element_layouts[g_hud_instrument_set_base_index +
					      AIM_TARGET_LAYOUT]
				.y);
		g_flight_fill_clip_rect_fn();

		object_idx =
			g_players[g_local_player].view_state.aim_target_idx;
		if (object_idx != UINT16_MAX) {
			hud_format_object_display_name(object_idx,
						       OBJECT_DISPLAY_FLAGS);
			object_name = g_flight_text_scratch_buffer;
		} else {
			object_name = g_str_mesh_component_names
				[MESH_COMPONENT_32_DASHES];
		}
		strcpy(text, object_name);
		flight_text_set_scratch(
			g_str_map_room_text[MAP_ROOM_STR_TRACKING]);
		flight_text_append_scratch_char(' ');
		flight_text_append_scratch_string(text);
#ifdef XVT_MODERN
		xvt_cockpit_text_record_field(XVT_COCKPIT_TEXT_MAP_TRACKING,
					      g_flight_text_scratch_buffer,
					      XVT_COCKPIT_ALIGN_CENTER);
#endif
		flight_text_draw_string_centered(g_flight_text_scratch_buffer);
		flight_surface_unlock();
		g_hud_element_state_cache[g_hud_instrument_set_base_index +
					  AIM_TARGET_LAYOUT] =
			g_players[g_local_player].view_state.aim_target_idx;
	}
}

/* Updates the target camera (CMD) view: when g_hud_element_state_cache entry 143
 * reads 0xFFFE, it updates the MFD pages, draws the four headers at layouts 139
 * to 142 (shield, hull, distance and a component name, each followed by ":") in
 * each layout's color, and sets that entry to 0. 0xFFFE is the -2 hud_init_hud
 * writes. Then it draws the target's details and status indicators every
 * call. */
// FUNCTION: XVT 0x439C60
void hud_update_cmd_text(void)
{
	enum {
		CMD_TEXT_LAYOUT_FIRST = 139,
		CMD_TEXT_LAYOUT_END = 143,
		CMD_TEXT_CACHE_INDEX = 143,
		MESH_COMPONENT_NAME = 17,
		COLOR_MFD_BACKGROUND = 0x2C,
		HUD_ELEMENT_STATE_INVALID = UINT16_MAX - 1,
	};

	if (((const uint16_t *)
		     g_hud_element_state_cache)[CMD_TEXT_CACHE_INDEX] ==
	    HUD_ELEMENT_STATE_INVALID) {
		hud_update_mfd_pages();
		flight_text_set_background_color(COLOR_MFD_BACKGROUND);
		flight_text_set_font_tier(2);
		for (uint16_t layout_index = CMD_TEXT_LAYOUT_FIRST;
		     layout_index < CMD_TEXT_LAYOUT_END; ++layout_index) {
			flight_text_set_cursor(
				g_hud_element_layouts[layout_index].x,
				g_hud_element_layouts[layout_index].y);
			flight_text_set_color(
				g_hud_element_layouts[layout_index]
					.color_index_or_widget_param);
			switch (layout_index - CMD_TEXT_LAYOUT_FIRST) {
			case 0:
				flight_text_set_scratch(
					g_str_cmd_threat_display_text[1]);
				break;
			case 1:
				flight_text_set_scratch(
					g_str_cmd_threat_display_text[2]);
				break;
			case 2:
				flight_text_set_scratch(
					g_str_cmd_threat_display_text[0]);
				break;
			case 3:
				flight_text_set_scratch(
					g_str_mesh_component_names
						[MESH_COMPONENT_NAME]);
				break;
			default:
				break;
			}
			flight_text_append_scratch_char(':');
			flight_text_set_clip_rect(
				g_hud_element_layouts[layout_index].x,
				g_hud_element_layouts[layout_index].y,
				g_hud_element_layouts[layout_index].x +
					flight_text_measure_string_width(
						g_flight_text_scratch_buffer),
				g_hud_element_layouts[layout_index].y +
					g_flight_font_line_height);
#ifdef XVT_MODERN
			xvt_cockpit_text_record_field(
				(xvt_cockpit_text_field_id)(XVT_COCKPIT_TEXT_CMD_HEADER_FIRST +
							    layout_index -
							    CMD_TEXT_LAYOUT_FIRST),
				g_flight_text_scratch_buffer,
				XVT_COCKPIT_ALIGN_LEFT);
#endif
			flight_text_draw_string(g_flight_text_scratch_buffer);
		}
		g_hud_element_state_cache[CMD_TEXT_CACHE_INDEX] = 0;
	}
	hud_draw_cmd_target_details();
	hud_draw_cmd_target_status_indicators();
}

/* Redraws the radar for the local player's craft when it has both radar HUD
 * features (0x80 and 0x100). Moves the blip counts to the previous counts,
 * swaps the draw and erase buffers by g_radar_blip_buffer_parity, and adds a blip
 * through hud_add_blip_to_radar for each radar-visible object: craft other than
 * the player's that are the target or are neither breaking up, exploding nor
 * hidden by a decoy beam; projectiles; and static region objects. Then it
 * restores the target marker's background, erases the previous blips, draws the
 * new ones, draws the target marker when there is a target (setting
 * g_radar_target_marker_background_saved), and flips the parity. */
// FUNCTION: XVT 0x439D90
void hud_draw_radar_blips(void)
{
	uint16_t player_object_idx = g_players[g_local_player].object_index;
	struct craft_data *player_craft =
		g_object_table[player_object_idx].mobj->p_craft;

	if ((player_craft->damage_stats.active_hud_feature_mask & 0x80) == 0 ||
	    (player_craft->damage_stats.active_hud_feature_mask & 0x100) == 0) {
		return;
	}

	g_radar_fore_prev_blip_count = g_radar_fore_blip_count;
	g_radar_aft_prev_blip_count = g_radar_aft_blip_count;
	g_radar_fore_blip_count = 0;
	g_radar_aft_blip_count = 0;
	g_radar_target_marker_restore_x = g_radar_target_marker_draw_x;
	g_radar_target_marker_restore_y = g_radar_target_marker_draw_y;
	if (g_radar_blip_buffer_parity != 0) {
		g_radar_fore_erase_blips = g_radar_fore_blip_buffer_b;
		g_radar_fore_draw_blips = g_radar_fore_blip_buffer_a;
		g_radar_aft_erase_blips = g_radar_aft_blip_buffer_b;
		g_radar_aft_draw_blips = g_radar_aft_blip_buffer_a;
	} else {
		g_radar_fore_erase_blips = g_radar_fore_blip_buffer_a;
		g_radar_fore_draw_blips = g_radar_fore_blip_buffer_b;
		g_radar_aft_erase_blips = g_radar_aft_blip_buffer_a;
		g_radar_aft_draw_blips = g_radar_aft_blip_buffer_b;
	}

	int object_idx;
	for (object_idx = g_active_region_object_slot_start;
	     object_idx < g_active_region_craft_object_slot_end; ++object_idx) {
		struct object_record *object = &g_object_table[object_idx];
		if (object_idx != player_object_idx &&
		    (g_object_type_table[object->object_type].behavior_flags &
		     1) != 0) {
			struct craft_data *craft = object->mobj->p_craft;
			if (g_players[g_local_player]
					    .current_target_object_idx ==
				    object_idx ||
			    (!object_has_active_decoy_beam(
				     (uint16_t)object_idx) &&
			     craft->object_kind !=
				     CRAFT_OBJECT_KIND_BREAKING_UP &&
			     craft->object_kind !=
				     CRAFT_OBJECT_KIND_EXPLODING)) {
				hud_add_blip_to_radar((int16_t)object_idx);
			}
		}
	}
	for (object_idx = g_projectile_object_slot_start;
	     object_idx < g_projectile_object_slot_end; ++object_idx) {
		if ((g_object_type_table[g_object_table[object_idx].object_type]
			     .behavior_flags &
		     1) != 0) {
			hud_add_blip_to_radar((int16_t)object_idx);
		}
	}
	for (object_idx = g_region_main_object_slot_end;
	     object_idx <
	     g_region_main_object_slot_end + g_region_static_object_slot_count;
	     ++object_idx) {
		if ((g_object_type_table[g_object_table[object_idx].object_type]
			     .behavior_flags &
		     1) != 0) {
			hud_add_blip_to_radar((int16_t)object_idx);
		}
	}
	if ((g_radar_fore_blip_count == 47 &&
	     g_radar_fore_prev_blip_count != 47) ||
	    (g_radar_aft_blip_count == 47 &&
	     g_radar_aft_prev_blip_count != 47)) {
		XVT_LOG_DEBUG("hud.radar_full slot=%d fore=%u aft=%u",
			      g_local_player, (unsigned)g_radar_fore_blip_count,
			      (unsigned)g_radar_aft_blip_count);
	}

	if (g_radar_target_marker_background_saved != 0) {
		g_flight_restore_radar_target_marker_fn();
	}
	if (g_radar_fore_prev_blip_count != 0) {
		g_flight_draw_point_array_masked_fn(
			(uint16_t *)&g_radar_fore_erase_blips->x,
			g_radar_fore_prev_blip_count);
	}
	if (g_radar_fore_blip_count != 0) {
		g_flight_draw_point_array_fn(
			(uint16_t *)&g_radar_fore_draw_blips->x,
			g_radar_fore_blip_count);
	}
	if (g_radar_aft_prev_blip_count != 0) {
		g_flight_draw_point_array_masked_fn(
			(uint16_t *)&g_radar_aft_erase_blips->x,
			g_radar_aft_prev_blip_count);
	}
	if (g_radar_aft_blip_count != 0) {
		g_flight_draw_point_array_fn(
			(uint16_t *)&g_radar_aft_draw_blips->x,
			g_radar_aft_blip_count);
	}
#ifdef XVT_MODERN
	xvt_cockpit_instruments_complete_radar();
#endif

	if (g_players[g_local_player].current_target_object_idx == -1) {
		g_radar_target_marker_background_saved = 0;
	} else {
		g_flight_draw_radar_target_marker_fn();
		g_radar_target_marker_background_saved = 1;
	}
	g_radar_blip_buffer_parity ^= 1;
}

/* Adds one radar blip for obj_idx. Takes its offset from the local player's
 * craft along the craft's forward, side and up vectors, recomputing them first
 * when marked dirty; ahead of the craft it goes to the fore radar (layout 0 of
 * the current set), behind to the aft one (layout 1), at the point
 * math2_getradarcoord gives in radarx and radary, with y kept at 0 or more.
 * g_radar_blip_color is 47 with no mobile object or for a satellite, blinks
 * between 59 and 55 for a projectile, else follows the IFF: 63, 55, 51, 59, 55,
 * 211 for 0 to 5, and keeps the last blip's color for any other. Past 61,083
 * and 122,166 in rough distance it steps the color down by 1 or 2 (up for 211;
 * 46 and 45 for 47). Each list holds 48; past that the last entry is
 * overwritten. Sets the target marker position when obj_idx is the target. */
// FUNCTION: XVT 0x43A0C0
void hud_add_blip_to_radar(int16_t obj_idx)
{
	int player_object_idx = g_players[g_local_player].object_index;
	int object_idx = (uint16_t)obj_idx;
	int delta_x = g_object_table[object_idx].world_x -
		      g_object_table[player_object_idx].world_x;
	int delta_y = g_object_table[object_idx].world_y -
		      g_object_table[player_object_idx].world_y;
	int delta_z = g_object_table[object_idx].world_z -
		      g_object_table[player_object_idx].world_z;

	if (g_object_table[player_object_idx].mobj->orient_matrix_dirty != 0) {
		fview_calcrotatemove(g_object_table[player_object_idx].pitch,
				     g_object_table[player_object_idx].yaw,
				     &g_object_table[player_object_idx]);
		fview_calcrotateorient(g_object_table[player_object_idx].roll,
				       0, &g_object_table[player_object_idx]);
	}
	int fwd_x_product = math_mul_q15(
		delta_x, g_object_table[player_object_idx].mobj->cached_fwd_x);
	int fwd_y_product = math_mul_q15(
		delta_y, g_object_table[player_object_idx].mobj->cached_fwd_y);
	int forward = fwd_x_product + fwd_y_product;
	int fwd_z_product = math_mul_q15(
		delta_z, g_object_table[player_object_idx].mobj->cached_fwd_z);
	forward += fwd_z_product;
	int side_x_product = math_mul_q15(
		delta_x, g_object_table[player_object_idx].mobj->cached_side_x);
	int side_y_product = math_mul_q15(
		delta_y, g_object_table[player_object_idx].mobj->cached_side_y);
	int side = side_x_product + side_y_product;
	int side_z = g_object_table[player_object_idx].mobj->cached_side_z;
	int side_z_product = math_mul_q15(delta_z, side_z);
	side += side_z_product;
	int up_x_product = math_mul_q15(
		delta_x, g_object_table[player_object_idx].mobj->cached_up_x);
	int up_y_product = math_mul_q15(
		delta_y, g_object_table[player_object_idx].mobj->cached_up_y);
	int up = up_x_product + up_y_product;
	int up_z_product = math_mul_q15(
		delta_z, g_object_table[player_object_idx].mobj->cached_up_z);
	up += up_z_product;
	up = -up;
	int16_t front_blip = 1;
	if (forward < 0) {
		forward = -forward;
		front_blip = 0;
	}

	struct mobile_object *mobile_object = g_object_table[object_idx].mobj;
	if (mobile_object == NULL) {
		g_radar_blip_color = 47;
	} else if (g_object_table[object_idx].genus_id ==
		   CRAFT_GENUS_SATELLITE) {
		g_radar_blip_color = 47;
	} else if (mobile_object->family == 1) {
		if (((g_mission_elapsed_clock.subsecond_ticks / 4) & 1) != 0) {
			g_radar_blip_color = 59;
		} else {
			g_radar_blip_color = 55;
		}
	} else {
		switch (mobile_object->iff) {
		case 0:
			g_radar_blip_color = 63;
			break;
		case 1:
		case 4:
			g_radar_blip_color = 55;
			break;
		case 2:
			g_radar_blip_color = 51;
			break;
		case 3:
			g_radar_blip_color = 59;
			break;
		case 5:
			g_radar_blip_color = 211;
			break;
		default:
			break;
		}
	}

	pai_object_ref_update_rough_distance(
		g_players[g_local_player].object_index, object_idx);
	if (g_last_rough_distance > 122166) {
		if (g_radar_blip_color == 47) {
			g_radar_blip_color = 45;
		} else if (g_radar_blip_color == 211) {
			g_radar_blip_color += 2;
		} else {
			g_radar_blip_color -= 2;
		}
	} else if (g_last_rough_distance > 61083) {
		if (g_radar_blip_color == 47) {
			g_radar_blip_color = 46;
		} else if (g_radar_blip_color == 211) {
			++g_radar_blip_color;
		} else {
			--g_radar_blip_color;
		}
	}

	math2_getradarcoord(side, up, forward);
	if (front_blip != 0) {
		radarx += g_hud_element_layouts[g_hud_instrument_set_base_index]
				  .x;
		radary += g_hud_element_layouts[g_hud_instrument_set_base_index]
				  .y;
		if (radary < 0) {
			radary = 0;
		}
		g_radar_fore_draw_blips[g_radar_fore_blip_count].x =
			(uint16_t)radarx;
		g_radar_fore_draw_blips[g_radar_fore_blip_count].y =
			(uint16_t)radary;
		g_radar_fore_draw_blips[g_radar_fore_blip_count].color =
			g_radar_blip_color;
#ifdef XVT_MODERN
		xvt_cockpit_instruments_record_radar(
			object_idx, 1, g_radar_fore_blip_count, radarx, radary,
			g_radar_blip_color);
#endif
		++g_radar_fore_blip_count;
		if (g_radar_fore_blip_count == 48) {
			--g_radar_fore_blip_count;
		}
	} else {
		radarx +=
			g_hud_element_layouts[g_hud_instrument_set_base_index +
					      1]
				.x;
		radary +=
			g_hud_element_layouts[g_hud_instrument_set_base_index +
					      1]
				.y;
		if (radary < 0) {
			radary = 0;
		}
		g_radar_aft_draw_blips[g_radar_aft_blip_count].x =
			(uint16_t)radarx;
		g_radar_aft_draw_blips[g_radar_aft_blip_count].y =
			(uint16_t)radary;
		g_radar_aft_draw_blips[g_radar_aft_blip_count].color =
			g_radar_blip_color;
#ifdef XVT_MODERN
		xvt_cockpit_instruments_record_radar(
			object_idx, 0, g_radar_aft_blip_count, radarx, radary,
			g_radar_blip_color);
#endif
		++g_radar_aft_blip_count;
		if (g_radar_aft_blip_count == 48) {
			--g_radar_aft_blip_count;
		}
	}

	if (g_players[g_local_player].current_target_object_idx == obj_idx) {
		g_radar_target_marker_draw_x = (uint16_t)radarx;
		g_radar_target_marker_draw_y = (uint16_t)radary;
	}
}

/* Draws the local player's targeting computer for one frame. In the proving
 * grounds it draws proving_grounds_draw_status_panel at the target inset's corner
 * instead. Outside the map view it draws nothing without HUD feature bit 0, and
 * with the targeting computer subsystem out it draws the cover panel sprite and
 * stops. When the target differs from g_hud_cached_target_object_idx it records it,
 * sets g_hud_target_inset_mask_refresh_pending, marks the target elements of
 * g_hud_element_state_cache for redraw, draws the labels when there was no target
 * before, and draws the new name, or the cover panel when there is no target
 * now.
 *
 * With a target it then draws every frame: the name, with the owning player's
 * name added when it may be shown; shields, the mean of the two shield values
 * against the model's strength; hull, what is left of hull_max; systems, what is
 * left of the model's system strength, at most 25 while weapon fire is
 * inhibited; and the distance, to the selected component for a starship or
 * platform. For a target that is not a fighter it shows the cargo (unknown
 * until identified) and the selected component, and stops. For a fighter flown
 * by the AI it shows the first word of its plan's report and its target; for
 * one flown by a player in a combat mission, hyperspace, attack or patrol
 * status and the target or first waypoint. Leaves g_flight_text_shadow_enabled at
 * 0. A status message with no space in its first 40 characters draws whatever
 * the word buffer held. */
// FUNCTION: XVT 0x43A5E0
void hud_update_targeting_computer_display(void)
{
	enum {
		TARGET_INSET_ELEMENT = 2,
		TARGET_PANEL_ELEMENT = 69,
		TARGET_SYSTEM_VALUE_ELEMENT = 82,
		TARGET_DISTANCE_VALUE_ELEMENT = 83,
		TARGET_DISTANCE_FRACTION_ELEMENT = 84,
		TARGET_SHIELD_VALUE_ELEMENT = 85,
		TARGET_HULL_VALUE_ELEMENT = 86,
		TARGET_CARGO_ELEMENT = 87,
		TARGET_UNUSED_ELEMENT = 88,
		TARGET_DETAIL_ELEMENT = 89,
		TARGET_ALT_PANEL_ELEMENT = 108,
		TARGET_SYSTEM_LABEL_ELEMENT = 111,
		TARGET_DISTANCE_LABEL_ELEMENT = 112,
		TARGET_SHIELD_LABEL_ELEMENT = 113,
		TARGET_HULL_LABEL_ELEMENT = 114,
		TARGET_NAME_ELEMENT = 115,
		TARGETING_COMPUTER_FEATURE_MASK = 1,
		HIDDEN_TARGET_DISPLAY_FLAGS = 1,
		NORMAL_TARGET_DISPLAY_FLAGS = 3,
		SHORT_TARGET_DISPLAY_FLAGS = 2,
		PERCENTAGE_SCALE = 0x28F,
		MAX_STATUS_FIRST_WORD_LENGTH = 40,
		NO_SELECTED_COMPONENT = 50,
		WAYPOINT_ZERO_OBJECT_REF = 0x8000,
	};

	bool draw_target_display;
	bool use_left_aligned_details;

#ifdef XVT_MODERN
	xvt_cockpit_readouts_begin_target(0);
#endif

	g_flight_text_shadow_enabled = 0;
	uint16_t left = g_hud_element_layouts[g_hud_instrument_set_base_index +
					      TARGET_INSET_ELEMENT]
				.x;
	uint16_t top = g_hud_element_layouts[g_hud_instrument_set_base_index +
					     TARGET_INSET_ELEMENT]
			       .y;
	if (g_flight_mission_state.proving_grounds_mode_active != 0) {
#ifdef XVT_MODERN
		xvt_cockpit_readouts_hide_target();
#endif
		proving_grounds_draw_status_panel(left, top);
		return;
	}

	draw_target_display = true;
	uint8_t map_camera_state = g_players[g_local_player].map_camera_state;
	if (map_camera_state == 0) {
		if ((g_object_table[g_players[g_local_player].object_index]
			     .mobj->p_craft->damage_stats
			     .active_hud_feature_mask &
		     TARGETING_COMPUTER_FEATURE_MASK) == 0) {
			draw_target_display = false;
		}
	}

	int local_object_index = g_players[g_local_player].object_index;
	use_left_aligned_details =
		local_object_index != -1 &&
		(g_object_table[local_object_index].object_type == 1 ||
		 g_object_table[local_object_index].object_type == 2 ||
		 g_object_table[local_object_index].object_type == 3 ||
		 g_object_table[local_object_index].object_type == 14 ||
		 g_object_table[local_object_index].object_type == 4);

	uint16_t dirty_state = UINT16_MAX;
	if (map_camera_state == 0) {
		if (!draw_target_display) {
#ifdef XVT_MODERN
			xvt_cockpit_readouts_hide_target();
			xvt_cockpit_text_clear_target_fields();
#endif
			return;
		}
		if ((g_object_table[local_object_index]
			     .mobj->p_craft->working_subsystems &
		     CRAFT_SUBSYSTEM_FLAG_TARGETING_COMPUTER) == 0) {
			if (g_hud_instrument_set_base_index ==
			    HUD_COCKPIT_INSTRUMENT_BASE_INDEX) {
				if (use_left_aligned_details) {
#ifdef XVT_MODERN
					xvt_cockpit_readouts_record_target_cover(
						TARGET_ALT_PANEL_ELEMENT);
#endif
					hud_draw_cached_sprite_element(
						TARGET_ALT_PANEL_ELEMENT, 0);
				} else {
#ifdef XVT_MODERN
					xvt_cockpit_readouts_record_target_cover(
						TARGET_PANEL_ELEMENT);
#endif
					hud_draw_cached_sprite_element(
						TARGET_PANEL_ELEMENT, 0);
				}
			} else {
#ifdef XVT_MODERN
				xvt_cockpit_readouts_record_target_cover(
					g_hud_instrument_set_base_index +
					TARGET_PANEL_ELEMENT);
#endif
				hud_draw_cached_sprite_element(
					g_hud_instrument_set_base_index +
						TARGET_PANEL_ELEMENT,
					0);
				g_hud_element_state_cache
					[g_hud_instrument_set_base_index +
					 TARGET_SYSTEM_LABEL_ELEMENT] =
						dirty_state;
				g_hud_element_state_cache
					[g_hud_instrument_set_base_index +
					 TARGET_HULL_LABEL_ELEMENT] =
						dirty_state;
				g_hud_element_state_cache
					[g_hud_instrument_set_base_index +
					 TARGET_SHIELD_LABEL_ELEMENT] =
						dirty_state;
				g_hud_element_state_cache
					[g_hud_instrument_set_base_index +
					 TARGET_SYSTEM_VALUE_ELEMENT] =
						dirty_state;
				g_hud_element_state_cache
					[g_hud_instrument_set_base_index +
					 TARGET_SHIELD_VALUE_ELEMENT] =
						dirty_state;
				g_hud_element_state_cache
					[g_hud_instrument_set_base_index +
					 TARGET_HULL_VALUE_ELEMENT] =
						dirty_state;
				g_hud_element_state_cache
					[g_hud_instrument_set_base_index +
					 TARGET_ALT_PANEL_ELEMENT] =
						dirty_state;
			}
			draw_target_display = false;
		}
	}
	if (!draw_target_display) {
#ifdef XVT_MODERN
		xvt_cockpit_readouts_hide_target();
		xvt_cockpit_text_clear_target_fields();
#endif
		return;
	}

	if (g_hud_instrument_set_base_index !=
	    HUD_COCKPIT_INSTRUMENT_BASE_INDEX) {
		flight_text_set_font_tier(0);
	} else {
		flight_text_set_font_tier(2);
	}
	g_hud_target_inset_mask_refresh_pending = 0;
	uint16_t current_target_object_index =
		(uint16_t)g_players[g_local_player].current_target_object_idx;
	struct object_record *target_object;
	struct mobile_object *target_mobile_object;
	struct craft_data *target_craft;
	if (current_target_object_index !=
	    (uint16_t)g_hud_cached_target_object_idx) {
#ifdef XVT_MODERN
		xvt_cockpit_text_clear_target_fields();
#endif
		int16_t previous_target_object_index =
			g_hud_cached_target_object_idx;
		g_hud_target_inset_mask_refresh_pending = 1;
		g_hud_cached_target_object_idx =
			(int16_t)current_target_object_index;
		XVT_LOG_DEBUG(
			"hud.target_shown slot=%d target=%d previous=%d map=%d",
			g_local_player,
			(int)g_players[g_local_player]
				.current_target_object_idx,
			(int)previous_target_object_index,
			(int)map_camera_state);
		g_hud_element_state_cache[g_hud_instrument_set_base_index +
					  TARGET_PANEL_ELEMENT] = dirty_state;
		g_hud_element_state_cache[TARGET_PANEL_ELEMENT] = dirty_state;
		g_hud_element_state_cache[g_hud_instrument_set_base_index +
					  TARGET_SYSTEM_VALUE_ELEMENT] =
			dirty_state;
		g_hud_element_state_cache[TARGET_SYSTEM_VALUE_ELEMENT] =
			dirty_state;
		g_hud_element_state_cache[g_hud_instrument_set_base_index +
					  TARGET_DISTANCE_VALUE_ELEMENT] =
			dirty_state;
		g_hud_element_state_cache[TARGET_DISTANCE_VALUE_ELEMENT] =
			dirty_state;
		g_hud_element_state_cache[g_hud_instrument_set_base_index +
					  TARGET_DISTANCE_FRACTION_ELEMENT] =
			dirty_state;
		g_hud_element_state_cache[TARGET_DISTANCE_FRACTION_ELEMENT] =
			dirty_state;
		g_hud_element_state_cache[g_hud_instrument_set_base_index +
					  TARGET_SHIELD_VALUE_ELEMENT] =
			dirty_state;
		g_hud_element_state_cache[TARGET_SHIELD_VALUE_ELEMENT] =
			dirty_state;
		g_hud_element_state_cache[g_hud_instrument_set_base_index +
					  TARGET_HULL_VALUE_ELEMENT] =
			dirty_state;
		g_hud_element_state_cache[TARGET_HULL_VALUE_ELEMENT] =
			dirty_state;
		g_hud_element_state_cache[g_hud_instrument_set_base_index +
					  TARGET_CARGO_ELEMENT] = dirty_state;
		g_hud_element_state_cache[TARGET_CARGO_ELEMENT] = dirty_state;
#ifdef XVT_MODERN
		xvt_cockpit_text_clear_field(XVT_COCKPIT_TEXT_TARGET_CARGO);
#endif
		g_hud_element_state_cache[g_hud_instrument_set_base_index +
					  TARGET_UNUSED_ELEMENT] = dirty_state;
		g_hud_element_state_cache[TARGET_UNUSED_ELEMENT] = dirty_state;
		g_hud_element_state_cache[g_hud_instrument_set_base_index +
					  TARGET_DETAIL_ELEMENT] = dirty_state;
		g_hud_element_state_cache[TARGET_DETAIL_ELEMENT] = dirty_state;
#ifdef XVT_MODERN
		xvt_cockpit_text_clear_field(XVT_COCKPIT_TEXT_TARGET_DETAIL);
#endif
		if (use_left_aligned_details ||
		    g_hud_instrument_set_base_index !=
			    HUD_COCKPIT_INSTRUMENT_BASE_INDEX) {
			g_hud_element_state_cache
				[g_hud_instrument_set_base_index +
				 TARGET_ALT_PANEL_ELEMENT] = dirty_state;
			g_hud_element_state_cache[TARGET_ALT_PANEL_ELEMENT] =
				dirty_state;
			if (g_hud_instrument_set_base_index !=
			    HUD_COCKPIT_INSTRUMENT_BASE_INDEX) {
				flight_text_set_font_tier(0);
			} else {
				flight_text_set_font_tier(2);
			}
		} else {
			flight_text_set_font_tier(2);
		}
		flight_text_set_background_color(0x30);
		flight_text_set_clear_line_background(1);

		if (previous_target_object_index == -1 ||
		    previous_target_object_index == -2 ||
		    previous_target_object_index == -3) {
			if (use_left_aligned_details ||
			    g_flight_resolution_mode !=
				    FLIGHT_RESOLUTION_320X240) {
				flight_text_set_color(0x46);
			} else {
				flight_text_set_color(0x45);
			}
			flight_text_set_clip_rect(0, 0, g_screen_width,
						  g_screen_height);
			flight_text_set_cursor(
				g_hud_element_layouts
					[g_hud_instrument_set_base_index +
					 TARGET_DISTANCE_LABEL_ELEMENT]
						.x,
				g_hud_element_layouts
					[g_hud_instrument_set_base_index +
					 TARGET_DISTANCE_LABEL_ELEMENT]
						.y);
#ifdef XVT_MODERN
			xvt_cockpit_text_record_field(
				XVT_COCKPIT_TEXT_TARGET_RANGE_LABEL,
				g_str_cmd_threat_display_text
					[CMD_THREAT_STR_DIST],
				XVT_COCKPIT_ALIGN_LEFT);
#endif
			flight_text_draw_string(g_str_cmd_threat_display_text
							[CMD_THREAT_STR_DIST]);
			flight_text_set_cursor(
				g_hud_element_layouts
						[g_hud_instrument_set_base_index +
						 TARGET_DISTANCE_VALUE_ELEMENT]
							.x +
					flight_text_measure_string_width("00"),
				g_hud_element_layouts
					[g_hud_instrument_set_base_index +
					 TARGET_DISTANCE_VALUE_ELEMENT]
						.y);
#ifdef XVT_MODERN
			xvt_cockpit_text_record_field(
				XVT_COCKPIT_TEXT_TARGET_RANGE_SEPARATOR, ".",
				XVT_COCKPIT_ALIGN_LEFT);
#endif
			g_flight_draw_char_fn('.');
			if (use_left_aligned_details) {
				flight_text_set_font_tier(0);
			}
			uint16_t percent_width =
				flight_text_measure_string_width(
					g_three_digit_width_text);
			flight_text_set_cursor(
				g_hud_element_layouts
					[g_hud_instrument_set_base_index +
					 TARGET_SHIELD_LABEL_ELEMENT]
						.x,
				g_hud_element_layouts
					[g_hud_instrument_set_base_index +
					 TARGET_SHIELD_LABEL_ELEMENT]
						.y);
			if (use_left_aligned_details ||
			    g_flight_resolution_mode !=
				    FLIGHT_RESOLUTION_320X240) {
				flight_text_set_color(0x46);
			} else {
				flight_text_set_color(0x45);
			}
#ifdef XVT_MODERN
			xvt_cockpit_text_record_field(
				XVT_COCKPIT_TEXT_TARGET_SHIELD_LABEL,
				g_str_cmd_threat_display_text
					[CMD_THREAT_STR_SHD],
				XVT_COCKPIT_ALIGN_LEFT);
#endif
			flight_text_draw_string(g_str_cmd_threat_display_text
							[CMD_THREAT_STR_SHD]);
			flight_text_set_cursor(
				g_hud_element_layouts
						[g_hud_instrument_set_base_index +
						 TARGET_SHIELD_VALUE_ELEMENT]
							.x +
					percent_width,
				g_hud_element_layouts
					[g_hud_instrument_set_base_index +
					 TARGET_SHIELD_VALUE_ELEMENT]
						.y);
#ifdef XVT_MODERN
			xvt_cockpit_text_record_field(
				XVT_COCKPIT_TEXT_TARGET_SHIELD_PERCENT, "%",
				XVT_COCKPIT_ALIGN_LEFT);
#endif
			g_flight_draw_char_fn('%');
			flight_text_set_cursor(
				g_hud_element_layouts
					[g_hud_instrument_set_base_index +
					 TARGET_HULL_LABEL_ELEMENT]
						.x,
				g_hud_element_layouts
					[g_hud_instrument_set_base_index +
					 TARGET_HULL_LABEL_ELEMENT]
						.y);
#ifdef XVT_MODERN
			xvt_cockpit_text_record_field(
				XVT_COCKPIT_TEXT_TARGET_HULL_LABEL,
				g_str_cmd_threat_display_text
					[CMD_THREAT_STR_HULL],
				XVT_COCKPIT_ALIGN_LEFT);
#endif
			flight_text_draw_string(g_str_cmd_threat_display_text
							[CMD_THREAT_STR_HULL]);
			flight_text_set_cursor(
				g_hud_element_layouts
						[g_hud_instrument_set_base_index +
						 TARGET_HULL_VALUE_ELEMENT]
							.x +
					percent_width,
				g_hud_element_layouts
					[g_hud_instrument_set_base_index +
					 TARGET_HULL_VALUE_ELEMENT]
						.y);
#ifdef XVT_MODERN
			xvt_cockpit_text_record_field(
				XVT_COCKPIT_TEXT_TARGET_HULL_PERCENT, "%",
				XVT_COCKPIT_ALIGN_LEFT);
#endif
			g_flight_draw_char_fn('%');
			flight_text_set_cursor(
				g_hud_element_layouts
					[g_hud_instrument_set_base_index +
					 TARGET_SYSTEM_LABEL_ELEMENT]
						.x,
				g_hud_element_layouts
					[g_hud_instrument_set_base_index +
					 TARGET_SYSTEM_LABEL_ELEMENT]
						.y);
#ifdef XVT_MODERN
			xvt_cockpit_text_record_field(
				XVT_COCKPIT_TEXT_TARGET_SYSTEM_LABEL,
				g_str_cmd_threat_display_text
					[CMD_THREAT_STR_SYS],
				XVT_COCKPIT_ALIGN_LEFT);
#endif
			flight_text_draw_string(g_str_cmd_threat_display_text
							[CMD_THREAT_STR_SYS]);
			flight_text_set_cursor(
				g_hud_element_layouts
						[g_hud_instrument_set_base_index +
						 TARGET_SYSTEM_VALUE_ELEMENT]
							.x +
					percent_width,
				g_hud_element_layouts
					[g_hud_instrument_set_base_index +
					 TARGET_SYSTEM_VALUE_ELEMENT]
						.y);
#ifdef XVT_MODERN
			xvt_cockpit_text_record_field(
				XVT_COCKPIT_TEXT_TARGET_SYSTEM_PERCENT, "%",
				XVT_COCKPIT_ALIGN_LEFT);
#endif
			g_flight_draw_char_fn('%');
			if (use_left_aligned_details &&
			    g_hud_instrument_set_base_index ==
				    HUD_COCKPIT_INSTRUMENT_BASE_INDEX) {
				flight_text_set_font_tier(2);
			}
		}

		if (current_target_object_index != UINT16_MAX) {
			target_object =
				&g_object_table[current_target_object_index];
			uint16_t target_object_type =
				target_object->object_type;
			target_mobile_object = target_object->mobj;
			target_craft = target_mobile_object != NULL
					       ? target_mobile_object->p_craft
					       : NULL;
			left = g_hud_element_layouts
				       [g_hud_instrument_set_base_index +
					TARGET_NAME_ELEMENT]
					       .x;
			top = g_hud_element_layouts
				      [g_hud_instrument_set_base_index +
				       TARGET_NAME_ELEMENT]
					      .y;
			flight_text_set_clip_rect(
				left, top,
				left + g_hud_element_layouts
						[g_hud_instrument_set_base_index +
						 TARGET_NAME_ELEMENT]
							.clip_width,
				top + g_flight_font_line_height + 1);
			g_flight_fill_clip_rect_fn();
			if (g_mission_header.mission_type ==
				    MISSION_TYPE_MELEE &&
			    g_flight_player_count > 1) {
				int16_t display_flags =
					NORMAL_TARGET_DISPLAY_FLAGS;
				if (g_object_table[current_target_object_index]
						    .mobj != NULL &&
				    target_craft != NULL &&
				    g_flight_mission_state
						    .locate_players_enabled ==
					    0) {
					int player_team =
						(uint16_t)g_players
							[g_local_player]
								.team;
					if (target_craft
						    ->identified_order_by_team
							    [player_team] ==
					    0) {
						int flight_group_index =
							g_object_table[current_target_object_index]
								.flight_group_idx;
						int team =
							g_mission_flight_groups
								[flight_group_index]
									.fg
									.team;
						int hostile = 0;
						if (team != player_team) {
							hostile =
								g_mission_teams[player_team]
									.allies[team] ==
								0;
						}
						if (hostile == 1 &&
						    g_mission_flight_groups[flight_group_index]
								    .fg
								    .player_number !=
							    0) {
							display_flags =
								HIDDEN_TARGET_DISPLAY_FLAGS;
						}
					}
				}
				hud_format_object_display_name(
					current_target_object_index,
					display_flags);
			} else {
				hud_format_object_display_name(
					current_target_object_index,
					NORMAL_TARGET_DISPLAY_FLAGS);
			}
			target_object =
				&g_object_table[current_target_object_index];
			if (target_object->player_owner_idx != -1) {
				if (g_flight_mission_state
						    .locate_players_enabled !=
					    0 ||
				    target_craft->identified_order_by_team
						    [(uint16_t)g_players
							     [g_local_player]
								     .team] !=
					    0 ||
				    !(g_mission_flight_groups[target_object
								      ->flight_group_idx]
							      .fg.team ==
						      (uint16_t)g_players
							      [g_local_player]
								      .team
					      ? 0
					      : g_mission_teams[(uint16_t)g_players
									[g_local_player]
										.team]
								.allies[g_mission_flight_groups
										[target_object
											 ->flight_group_idx]
											.fg
											.team] ==
							0)) {
					flight_text_append_scratch_char('-');
					flight_text_append_scratch_string(
						net_session_get_player_name(
							g_object_table[current_target_object_index]
								.player_owner_idx));
				}
			}
			flight_text_set_cursor(left, top);
#ifdef XVT_MODERN
			xvt_cockpit_text_record_field(
				XVT_COCKPIT_TEXT_TARGET_NAME,
				g_flight_text_scratch_buffer,
				XVT_COCKPIT_ALIGN_CENTER);
#endif
			flight_text_draw_string_centered(
				g_flight_text_scratch_buffer);

			if (g_projectile_object_slot_start <=
				    current_target_object_index &&
			    g_projectile_object_slot_end >
				    current_target_object_index &&
			    g_projectile_type_data.warhead_class
					    [target_object_type -
					     PROJECTILE_OBJECT_TYPE_FIRST] !=
				    0) {
				struct warhead_guidance_state *guidance =
					g_object_table
						[(uint16_t)g_players[g_local_player]
							 .current_target_object_idx]
							.mobj
							->p_warhead_guidance;
				if (guidance->homing_tier == 0 ||
				    guidance->target_obj_idx == UINT16_MAX) {
					flight_text_set_scratch(
						g_str_mesh_component_names
							[MESH_COMPONENT_32_DASHES]);
				} else if (g_object_table
						   [guidance->target_obj_idx]
							   .player_owner_idx ==
					   g_local_player) {
					flight_text_set_scratch(
						g_str_cmd_threat_display_text
							[CMD_THREAT_STR_THIS_CRAFT]);
				} else {
					hud_format_object_display_name(
						guidance->target_obj_idx,
						SHORT_TARGET_DISPLAY_FLAGS);
				}
				left = g_hud_element_layouts
					       [g_hud_instrument_set_base_index +
						TARGET_DETAIL_ELEMENT]
						       .x;
				top = g_hud_element_layouts
					      [g_hud_instrument_set_base_index +
					       TARGET_DETAIL_ELEMENT]
						      .y;
				flight_text_set_clip_rect(
					left, top,
					left + g_hud_element_layouts
							[g_hud_instrument_set_base_index +
							 TARGET_DETAIL_ELEMENT]
								.clip_width,
					top + g_flight_font_line_height + 1);
				g_flight_fill_clip_rect_fn();
				flight_text_set_cursor(left, top);
				flight_text_set_color(0x4E);
#ifdef XVT_MODERN
				xvt_cockpit_text_record_field(
					XVT_COCKPIT_TEXT_TARGET_DETAIL,
					g_flight_text_scratch_buffer,
					XVT_COCKPIT_ALIGN_RIGHT);
#endif
				flight_text_draw_string_right_aligned(
					g_flight_text_scratch_buffer);
			}
		} else {
			if (g_hud_instrument_set_base_index !=
			    HUD_ONLY_VIEW_INSTRUMENT_BASE_INDEX) {
				if (use_left_aligned_details ||
				    g_players[g_local_player]
						    .map_camera_state != 0) {
#ifdef XVT_MODERN
					xvt_cockpit_readouts_record_target_cover(
						g_hud_instrument_set_base_index +
						TARGET_ALT_PANEL_ELEMENT);
#endif
					hud_draw_cached_sprite_element(
						g_hud_instrument_set_base_index +
							TARGET_ALT_PANEL_ELEMENT,
						0);
				} else {
#ifdef XVT_MODERN
					xvt_cockpit_readouts_record_target_cover(
						g_hud_instrument_set_base_index +
						TARGET_PANEL_ELEMENT);
#endif
					hud_draw_cached_sprite_element(
						g_hud_instrument_set_base_index +
							TARGET_PANEL_ELEMENT,
						0);
				}
			} else {
#ifdef XVT_MODERN
				xvt_cockpit_readouts_record_target_cover(
					g_hud_instrument_set_base_index +
					TARGET_PANEL_ELEMENT);
#endif
				hud_draw_cached_sprite_element(
					g_hud_instrument_set_base_index +
						TARGET_PANEL_ELEMENT,
					0);
				g_hud_element_state_cache
					[g_hud_instrument_set_base_index +
					 TARGET_SYSTEM_LABEL_ELEMENT] =
						dirty_state;
				g_hud_element_state_cache
					[g_hud_instrument_set_base_index +
					 TARGET_HULL_LABEL_ELEMENT] =
						dirty_state;
				g_hud_element_state_cache
					[g_hud_instrument_set_base_index +
					 TARGET_SHIELD_LABEL_ELEMENT] =
						dirty_state;
				g_hud_element_state_cache
					[g_hud_instrument_set_base_index +
					 TARGET_SYSTEM_VALUE_ELEMENT] =
						dirty_state;
				g_hud_element_state_cache
					[g_hud_instrument_set_base_index +
					 TARGET_SHIELD_VALUE_ELEMENT] =
						dirty_state;
				g_hud_element_state_cache
					[g_hud_instrument_set_base_index +
					 TARGET_HULL_VALUE_ELEMENT] =
						dirty_state;
				g_hud_element_state_cache
					[g_hud_instrument_set_base_index +
					 TARGET_ALT_PANEL_ELEMENT] =
						dirty_state;
			}
		}
	}

	if (g_players[g_local_player].current_target_object_idx == -1) {
		return;
	}

	flight_text_set_background_color(0x30);
	left = g_hud_element_layouts[g_hud_instrument_set_base_index +
				     TARGET_NAME_ELEMENT]
		       .x;
	top = g_hud_element_layouts[g_hud_instrument_set_base_index +
				    TARGET_NAME_ELEMENT]
		      .y;
	flight_text_set_clip_rect(
		left, top,
		left + g_hud_element_layouts[g_hud_instrument_set_base_index +
					     TARGET_NAME_ELEMENT]
				.clip_width,
		top + g_flight_font_line_height + 1);
	g_flight_fill_clip_rect_fn();
	if (g_mission_header.mission_type == MISSION_TYPE_MELEE &&
	    g_flight_player_count > 1) {
		int16_t display_flags = NORMAL_TARGET_DISPLAY_FLAGS;
		if (g_object_table[(uint16_t)g_players[g_local_player]
					   .current_target_object_idx]
				    .mobj != NULL &&
		    g_object_table[(uint16_t)g_players[g_local_player]
					   .current_target_object_idx]
				    .mobj->p_craft != NULL &&
		    g_flight_mission_state.locate_players_enabled == 0) {
			struct craft_data *display_craft =
				g_object_table
					[(uint16_t)g_players[g_local_player]
						 .current_target_object_idx]
						.mobj->p_craft;

			int player_team =
				(uint16_t)g_players[g_local_player].team;
			if (display_craft
				    ->identified_order_by_team[player_team] ==
			    0) {
				int flight_group_index =
					g_object_table
						[(uint16_t)g_players[g_local_player]
							 .current_target_object_idx]
							.flight_group_idx;

				int team = g_mission_flight_groups
						   [flight_group_index]
							   .fg.team;
				int hostile = 0;
				if (team != player_team) {
					hostile = g_mission_teams[player_team]
							  .allies[team] == 0;
				}
				if (hostile == 1 &&
				    g_mission_flight_groups[flight_group_index]
						    .fg.player_number != 0) {
					display_flags =
						HIDDEN_TARGET_DISPLAY_FLAGS;
				}
			}
		}
		hud_format_object_display_name(
			(uint16_t)g_players[g_local_player]
				.current_target_object_idx,
			display_flags);
	} else {
		hud_format_object_display_name(
			(uint16_t)g_players[g_local_player]
				.current_target_object_idx,
			NORMAL_TARGET_DISPLAY_FLAGS);
	}
	target_object = &g_object_table[(uint16_t)g_players[g_local_player]
						.current_target_object_idx];
	if (target_object->player_owner_idx != -1) {
		if (g_flight_mission_state.locate_players_enabled != 0 ||
		    target_object->mobj->p_craft->identified_order_by_team
				    [(uint16_t)g_players[g_local_player]
					     .team] != 0 ||
		    !(g_mission_flight_groups[target_object->flight_group_idx]
					      .fg.team ==
				      (uint16_t)g_players[g_local_player].team
			      ? 0
			      : g_mission_teams[(uint16_t)g_players
							[g_local_player]
								.team]
						.allies[g_mission_flight_groups
								[target_object
									 ->flight_group_idx]
									.fg
									.team] ==
					0)) {
			flight_text_append_scratch_string(" (");
			flight_text_append_scratch_string(net_session_get_player_name(
				g_object_table
					[(uint16_t)g_players[g_local_player]
						 .current_target_object_idx]
						.player_owner_idx));
			flight_text_append_scratch_char(')');
		}
	}
	flight_text_set_cursor(left, top);
#ifdef XVT_MODERN
	xvt_cockpit_text_record_field(XVT_COCKPIT_TEXT_TARGET_NAME,
				      g_flight_text_scratch_buffer,
				      XVT_COCKPIT_ALIGN_CENTER);
#endif
	flight_text_draw_string_centered(g_flight_text_scratch_buffer);

	target_craft = NULL;
	target_mobile_object =
		g_object_table[(uint16_t)g_players[g_local_player]
				       .current_target_object_idx]
			.mobj;
	if (target_mobile_object != NULL) {
		target_craft = target_mobile_object->p_craft;
	}
	unsigned int shield_percentage;
	if (target_mobile_object == NULL) {
		shield_percentage = 0;
	} else {
		unsigned int shield_total;
		unsigned int shield_average;
		unsigned int max_shield;

		if (target_craft == NULL ||
		    target_craft->object_kind ==
			    CRAFT_OBJECT_KIND_BREAKING_UP ||
		    target_craft->object_kind == CRAFT_OBJECT_KIND_EXPLODING) {
			max_shield = 0;
		} else {
			shield_total = target_craft->shield_energy[0] +
				       target_craft->shield_energy[1];
			shield_average = shield_total >> 1;
			max_shield = 2 * g_model_defs[target_craft->model_index]
						 .shield_strength;
		}
		if (max_shield != 0) {
			shield_percentage = (uint16_t)math2_longratio_q16(
				shield_average, max_shield);
			shield_percentage =
				2 * (shield_percentage / PERCENTAGE_SCALE);
			if (shield_total != 0 && shield_percentage == 0) {
				shield_percentage = 1;
			}
		} else {
			shield_percentage = 0;
		}
	}
	if (use_left_aligned_details) {
		flight_text_set_font_tier(0);
	}
	hud_draw_cached_numeric_element(g_hud_instrument_set_base_index +
						TARGET_SHIELD_VALUE_ELEMENT,
					(int16_t)shield_percentage, 1);
	if (use_left_aligned_details &&
	    g_hud_instrument_set_base_index ==
		    HUD_COCKPIT_INSTRUMENT_BASE_INDEX) {
		flight_text_set_font_tier(2);
	}

	unsigned int hull_percentage;
	if ((uint16_t)g_players[g_local_player].current_target_object_idx <
		    g_active_region_craft_object_slot_end &&
	    target_craft != NULL) {
		if (target_craft->object_kind ==
			    CRAFT_OBJECT_KIND_BREAKING_UP ||
		    target_craft->object_kind == CRAFT_OBJECT_KIND_EXPLODING) {
			hull_percentage = 0;
		} else if (target_craft->hull_damage > target_craft->hull_max) {
			hull_percentage = 1;
		} else {
			hull_percentage = (uint16_t)math2_longratio_q16(
				target_craft->hull_max -
					target_craft->hull_damage,
				target_craft->hull_max);
			hull_percentage = hull_percentage / PERCENTAGE_SCALE;
			if (hull_percentage == 0) {
				hull_percentage = 1;
			}
		}
	} else {
		hull_percentage = 100;
	}
	if (use_left_aligned_details) {
		flight_text_set_font_tier(0);
	}
	hud_draw_cached_numeric_element(g_hud_instrument_set_base_index +
						TARGET_HULL_VALUE_ELEMENT,
					hull_percentage, 1);
	if (use_left_aligned_details &&
	    g_hud_instrument_set_base_index ==
		    HUD_COCKPIT_INSTRUMENT_BASE_INDEX) {
		flight_text_set_font_tier(2);
	}

	target_object = &g_object_table[(uint16_t)g_players[g_local_player]
						.current_target_object_idx];
	unsigned int system_percentage;
	if (target_object->mobj != NULL) {
		if (target_craft == NULL ||
		    target_craft->object_kind ==
			    CRAFT_OBJECT_KIND_BREAKING_UP ||
		    target_craft->object_kind == CRAFT_OBJECT_KIND_EXPLODING) {
			system_percentage = 0;
		} else if (target_craft->object_kind ==
				   CRAFT_OBJECT_KIND_BREAKING_UP ||
			   target_craft->object_kind ==
				   CRAFT_OBJECT_KIND_EXPLODING ||
			   target_craft->working_subsystems == 0) {
			system_percentage = 0;
		} else {
			uint16_t system_strength =
				g_model_defs[target_craft->model_index]
					.system_strength;
			uint16_t subsystem_damage =
				(uint16_t)target_craft->subsystem_damage;
			if (system_strength <= subsystem_damage) {
				system_percentage = 0;
			} else {
				system_percentage = math2_ratio_q16(
					system_strength - subsystem_damage,
					system_strength);
				system_percentage /= PERCENTAGE_SCALE;
			}
			if (system_percentage > 25 &&
			    target_craft->weapon_fire_inhibit_timer != 0) {
				system_percentage = 25;
			}
		}
	} else {
		system_percentage =
			target_object->type_specific_word == 0 ? 0 : 100;
	}
	if (use_left_aligned_details) {
		flight_text_set_font_tier(0);
	}
	hud_draw_cached_numeric_element(g_hud_instrument_set_base_index +
						TARGET_SYSTEM_VALUE_ELEMENT,
					(int16_t)system_percentage, 1);
	if (use_left_aligned_details &&
	    g_hud_instrument_set_base_index ==
		    HUD_COCKPIT_INSTRUMENT_BASE_INDEX) {
		flight_text_set_font_tier(2);
	}

	if (g_players[g_local_player].map_camera_state == 0 &&
	    (g_object_table[(uint16_t)g_players[g_local_player]
				    .current_target_object_idx]
			     .genus_id == CRAFT_GENUS_STARSHIP ||
	     g_object_table[(uint16_t)g_players[g_local_player]
				    .current_target_object_idx]
			     .genus_id == CRAFT_GENUS_PLATFORM)) {
		object_direction_and_distance_to_mesh_center(
			g_players[g_local_player].object_index,
			(uint16_t)g_players[g_local_player]
				.current_target_object_idx,
			(uint16_t)g_players[g_local_player]
				.selected_target_component);
	} else {
		player_compute_polar_to_object_ref(
			g_local_player, (uint16_t)g_players[g_local_player]
						.current_target_object_idx);
	}
	hud_draw_target_distance(trig2_polardistance);

	target_object = &g_object_table[(uint16_t)g_players[g_local_player]
						.current_target_object_idx];
	if (target_object->genus_id != CRAFT_GENUS_STARFIGHTER) {
		uint16_t cargo_state = 2;
		const char *cargo_text =
			g_str_mesh_component_names[MESH_COMPONENT_32_DASHES];

		if ((uint16_t)g_players[g_local_player]
				    .current_target_object_idx <
			    g_active_region_craft_object_slot_end &&
		    target_craft != NULL &&
		    g_object_table[(uint16_t)g_players[g_local_player]
					   .current_target_object_idx]
				    .mobj->family == 0) {
			if (target_craft->identified_order_by_team
					    [(uint16_t)g_players[g_local_player]
						     .team] == 0 ||
			    target_craft->object_kind ==
				    CRAFT_OBJECT_KIND_BREAKING_UP ||
			    target_craft->object_kind ==
				    CRAFT_OBJECT_KIND_EXPLODING) {
				cargo_state = 0;
				cargo_text = g_str_unknown;
			} else {
				cargo_state = 1;
				cargo_text = target_craft->special_cargo_name;
				if (target_craft->special_cargo_name[0] ==
				    '\0') {
					cargo_state = 2;
					cargo_text = g_str_cmd_threat_display_text
						[CMD_THREAT_STR_NO_CARGO];
				}
			}
		}
		if (cargo_state !=
		    g_hud_element_state_cache[TARGET_CARGO_ELEMENT]) {
			g_hud_element_state_cache[TARGET_CARGO_ELEMENT] =
				cargo_state;
			left = g_hud_element_layouts
				       [g_hud_instrument_set_base_index +
					TARGET_CARGO_ELEMENT]
					       .x;
			top = g_hud_element_layouts
				      [g_hud_instrument_set_base_index +
				       TARGET_CARGO_ELEMENT]
					      .y;
			flight_text_set_clip_rect(
				left, top,
				left + g_hud_element_layouts
						[g_hud_instrument_set_base_index +
						 TARGET_CARGO_ELEMENT]
							.clip_width,
				top + g_flight_font_line_height + 1);
			g_flight_fill_clip_rect_fn();
			flight_text_set_cursor(left, top);
			flight_text_set_color(0x46);
			if (!use_left_aligned_details) {
#ifdef XVT_MODERN
				xvt_cockpit_text_record_field(
					XVT_COCKPIT_TEXT_TARGET_CARGO,
					cargo_text, XVT_COCKPIT_ALIGN_RIGHT);
#endif
				flight_text_draw_string_right_aligned(
					cargo_text);
			} else {
#ifdef XVT_MODERN
				xvt_cockpit_text_record_field(
					XVT_COCKPIT_TEXT_TARGET_CARGO,
					cargo_text, XVT_COCKPIT_ALIGN_LEFT);
#endif
				flight_text_draw_string(cargo_text);
			}
		}

		int8_t component_display_state = 0;
		if (target_mobile_object == NULL) {
			component_display_state = 1;
		} else if (target_craft != NULL) {
			component_display_state = 2;
		}
		if (component_display_state > 0) {
			uint16_t selected_component = NO_SELECTED_COMPONENT;
			if (component_display_state == 2) {
				selected_component =
					g_players[g_local_player]
						.selected_target_component;
			}
			flight_text_set_color(0x4E);
			if (selected_component !=
			    g_hud_element_state_cache[TARGET_DETAIL_ELEMENT]) {
				g_hud_element_state_cache
					[TARGET_DETAIL_ELEMENT] =
						selected_component;
				left = g_hud_element_layouts
					       [g_hud_instrument_set_base_index +
						TARGET_DETAIL_ELEMENT]
						       .x;
				top = g_hud_element_layouts
					      [g_hud_instrument_set_base_index +
					       TARGET_DETAIL_ELEMENT]
						      .y;
				flight_text_set_clip_rect(
					left, top,
					left + g_hud_element_layouts
							[g_hud_instrument_set_base_index +
							 TARGET_DETAIL_ELEMENT]
								.clip_width,
					top + g_flight_font_line_height + 1);
				g_flight_fill_clip_rect_fn();
				flight_text_set_cursor(left, top);
				if (selected_component ==
				    NO_SELECTED_COMPONENT) {
#ifdef XVT_MODERN
					xvt_cockpit_text_record_field(
						XVT_COCKPIT_TEXT_TARGET_DETAIL,
						g_str_mesh_component_names
							[MESH_COMPONENT_32_DASHES],
						XVT_COCKPIT_ALIGN_RIGHT);
#endif
					flight_text_draw_string_right_aligned(
						g_str_mesh_component_names
							[MESH_COMPONENT_32_DASHES]);
				} else {
					int mesh_index =
						(uint16_t)g_players[g_local_player]
							.selected_target_component;
					int object_type =
						g_object_table
							[(uint16_t)g_players
								 [g_local_player]
									 .current_target_object_idx]
								.object_type;
					uint16_t mesh_type;

					if (object_type < 73) {
						if (mesh_index < 0) {
							mesh_type = 0;
						} else {
							int mesh_count =
								g_object_type_mesh_cache
									[object_type]
										.mesh_count;
							if (mesh_index >=
							    mesh_count) {
								mesh_index =
									mesh_count -
									1;
							}
							mesh_type =
								g_object_type_mesh_cache[object_type]
									.mesh_types
										[mesh_index];
						}
					} else {
						mesh_type =
							model_mesh_get_object_type_mesh_type(
								object_type,
								mesh_index);
					}
					if (target_object->genus_id ==
						    CRAFT_GENUS_STARFIGHTER &&
					    mesh_type ==
						    MESH_COMPONENT_07_BRIDGE) {
						mesh_type =
							MESH_COMPONENT_26_COCKPIT;
					}
					if ((target_object->object_type == 38 ||
					     target_object->object_type ==
						     39) &&
					    mesh_type ==
						    MESH_COMPONENT_07_BRIDGE) {
						mesh_type =
							MESH_COMPONENT_26_COCKPIT;
					}
#ifdef XVT_MODERN
					xvt_cockpit_text_record_field(
						XVT_COCKPIT_TEXT_TARGET_DETAIL,
						g_str_mesh_component_names
							[mesh_type],
						XVT_COCKPIT_ALIGN_RIGHT);
#endif
					flight_text_draw_string_right_aligned(
						g_str_mesh_component_names
							[mesh_type]);
				}
			}
		}
		return;
	}

	{
		int16_t ownership_display_mode;

		if (g_active_region_craft_object_slot_end <=
			    (uint16_t)g_players[g_local_player]
				    .current_target_object_idx ||
		    target_object->mobj == NULL) {
			ownership_display_mode = 0;
		} else if (target_object->player_owner_idx == -1) {
			ownership_display_mode = 1;
			if (g_mission_header.mission_type ==
				    MISSION_TYPE_MELEE &&
			    g_flight_player_count > 1 && target_craft != NULL &&
			    g_flight_mission_state.locate_players_enabled ==
				    0) {
				int player_team =
					(uint16_t)g_players[g_local_player]
						.team;
				if (target_craft->identified_order_by_team
					    [player_team] == 0) {
					int team =
						g_mission_flight_groups
							[target_object
								 ->flight_group_idx]
								.fg.team;
					int hostile =
						team == player_team
							? 0
							: g_mission_teams[player_team]
									  .allies[team] ==
								  0;
					if (hostile == 1 &&
					    g_mission_flight_groups
							    [target_object
								     ->flight_group_idx]
								    .fg
								    .player_number !=
						    0) {
						ownership_display_mode = 0;
					}
				}
			}
		} else {
			ownership_display_mode =
				g_mission_header.mission_type ==
						MISSION_TYPE_COMBAT
					? -1
					: 0;
		}

		uint16_t invalid_state = UINT16_MAX - 1;
		char status_first_word[64];
		if (ownership_display_mode == 1) {
			struct mobile_object *order_mobile_object =
				target_object->mobj;
			struct craft_data *order_craft =
				order_mobile_object->p_craft;

			if (order_craft == NULL) {
				return;
			}
			struct ai_controller *ai_controller =
				&order_craft->ai_controller;
			uint16_t display_plan_id =
				ai_controller->running_plan_id;
			if (order_craft->working_subsystems == 0) {
				display_plan_id = (uint16_t)
					pai_find_plan_id_by_name_or_zero(
						"disabledpln");
			} else if (order_mobile_object->speed == 0) {
				const char *plan_name =
					g_plan_table[ai_controller
							     ->running_plan_id]
						.name;
				if (strcmp(plan_name, "flyhomepln") == 0 ||
				    strcmp(plan_name, "followhomepln") == 0 ||
				    strcmp(plan_name, "flyhomeevadepln") == 0 ||
				    strcmp(plan_name, "followhomeevadepln") ==
					    0 ||
				    strcmp(plan_name, "enterhangarpln") == 0 ||
				    strcmp(plan_name, "exithangarpln") == 0 ||
				    strcmp(plan_name, "intohyperspacepln") ==
					    0 ||
				    strcmp(plan_name, "outofhyperspacepln") ==
					    0 ||
				    strcmp(plan_name, "starshipintohyperpln") ==
					    0 ||
				    strcmp(plan_name,
					   "starshipfollowhomepln") == 0) {
					display_plan_id = (uint16_t)
						pai_find_plan_id_by_name_or_zero(
							"waitpln");
				}
			}
			if (display_plan_id !=
			    g_hud_element_state_cache[TARGET_CARGO_ELEMENT]) {
				g_hud_element_state_cache
					[TARGET_CARGO_ELEMENT] =
						display_plan_id;
				left = g_hud_element_layouts
					       [g_hud_instrument_set_base_index +
						TARGET_CARGO_ELEMENT]
						       .x;
				top = g_hud_element_layouts
					      [g_hud_instrument_set_base_index +
					       TARGET_CARGO_ELEMENT]
						      .y;
				flight_text_set_clip_rect(
					left, top,
					left + g_hud_element_layouts
							[g_hud_instrument_set_base_index +
							 TARGET_CARGO_ELEMENT]
								.clip_width,
					top + g_flight_font_line_height + 1);
				g_flight_fill_clip_rect_fn();
				flight_text_set_cursor(left, top);
				flight_text_set_color(0x46);
				const char *status_text = g_str_in_flight_messages
					[g_plan_report_message_id_by_plan_id
						 [display_plan_id]];
				const char *status_char = status_text;
				for (uint16_t word_length = 0;
				     word_length < MAX_STATUS_FIRST_WORD_LENGTH;
				     ++word_length, ++status_char) {
					if (*status_char == ' ') {
						strncpy(status_first_word,
							status_text,
							word_length);
						status_first_word[word_length] =
							'\0';
						break;
					}
				}
#ifdef XVT_MODERN
				xvt_cockpit_text_record_field(
					XVT_COCKPIT_TEXT_TARGET_CARGO,
					status_first_word,
					XVT_COCKPIT_ALIGN_LEFT);
#endif
				flight_text_draw_string(status_first_word);
			}

			uint16_t ai_target_object_index =
				ai_controller->target_obj_idx;
			if (ai_target_object_index == 255 ||
			    ai_target_object_index == UINT16_MAX) {
				if (g_hud_element_state_cache
					    [TARGET_DETAIL_ELEMENT] !=
				    invalid_state) {
					g_hud_element_state_cache
						[TARGET_DETAIL_ELEMENT] =
							invalid_state;
#ifdef XVT_MODERN
					xvt_cockpit_text_clear_field(
						XVT_COCKPIT_TEXT_TARGET_DETAIL);
#endif
					flight_text_set_clip_rect(
						g_hud_element_layouts
							[g_hud_instrument_set_base_index +
							 TARGET_DETAIL_ELEMENT]
								.x,
						g_hud_element_layouts
							[g_hud_instrument_set_base_index +
							 TARGET_DETAIL_ELEMENT]
								.y,
						g_hud_element_layouts
								[g_hud_instrument_set_base_index +
								 TARGET_DETAIL_ELEMENT]
									.x +
							g_hud_element_layouts
								[g_hud_instrument_set_base_index +
								 TARGET_DETAIL_ELEMENT]
									.clip_width,
						g_hud_element_layouts
								[g_hud_instrument_set_base_index +
								 TARGET_DETAIL_ELEMENT]
									.y +
							g_flight_font_line_height +
							1);
					g_flight_fill_clip_rect_fn();
				}
			} else if (ai_target_object_index !=
				   g_hud_element_state_cache
					   [TARGET_DETAIL_ELEMENT]) {
				g_hud_element_state_cache
					[TARGET_DETAIL_ELEMENT] =
						ai_target_object_index;
				left = g_hud_element_layouts
					       [g_hud_instrument_set_base_index +
						TARGET_DETAIL_ELEMENT]
						       .x;
				top = g_hud_element_layouts
					      [g_hud_instrument_set_base_index +
					       TARGET_DETAIL_ELEMENT]
						      .y;
				flight_text_set_clip_rect(
					left, top,
					left + g_hud_element_layouts
							[g_hud_instrument_set_base_index +
							 TARGET_DETAIL_ELEMENT]
								.clip_width,
					top + g_flight_font_line_height + 1);
				g_flight_fill_clip_rect_fn();
				flight_text_set_cursor(left, top);
				hud_format_object_display_name(
					ai_target_object_index,
					NORMAL_TARGET_DISPLAY_FLAGS);
#ifdef XVT_MODERN
				xvt_cockpit_text_record_field(
					XVT_COCKPIT_TEXT_TARGET_DETAIL,
					g_flight_text_scratch_buffer,
					XVT_COCKPIT_ALIGN_RIGHT);
#endif
				flight_text_draw_string_right_aligned(
					g_flight_text_scratch_buffer);
			}
			return;
		}

		if (ownership_display_mode == 0) {
			flight_text_set_background_color(0x30);
			if (g_hud_element_state_cache[TARGET_CARGO_ELEMENT] !=
			    invalid_state) {
				g_hud_element_state_cache
					[TARGET_CARGO_ELEMENT] = invalid_state;
#ifdef XVT_MODERN
				xvt_cockpit_text_clear_field(
					XVT_COCKPIT_TEXT_TARGET_CARGO);
#endif
				flight_text_set_clip_rect(
					g_hud_element_layouts
						[g_hud_instrument_set_base_index +
						 TARGET_CARGO_ELEMENT]
							.x,
					g_hud_element_layouts
						[g_hud_instrument_set_base_index +
						 TARGET_CARGO_ELEMENT]
							.y,
					g_hud_element_layouts
							[g_hud_instrument_set_base_index +
							 TARGET_CARGO_ELEMENT]
								.x +
						g_hud_element_layouts
							[g_hud_instrument_set_base_index +
							 TARGET_CARGO_ELEMENT]
								.clip_width,
					g_hud_element_layouts
							[g_hud_instrument_set_base_index +
							 TARGET_CARGO_ELEMENT]
								.y +
						g_flight_font_line_height + 1);
				g_flight_fill_clip_rect_fn();
			}
			if (g_hud_element_state_cache[TARGET_DETAIL_ELEMENT] !=
			    invalid_state) {
				g_hud_element_state_cache
					[TARGET_DETAIL_ELEMENT] = invalid_state;
#ifdef XVT_MODERN
				xvt_cockpit_text_clear_field(
					XVT_COCKPIT_TEXT_TARGET_DETAIL);
#endif
				flight_text_set_clip_rect(
					g_hud_element_layouts
						[g_hud_instrument_set_base_index +
						 TARGET_DETAIL_ELEMENT]
							.x,
					g_hud_element_layouts
						[g_hud_instrument_set_base_index +
						 TARGET_DETAIL_ELEMENT]
							.y,
					g_hud_element_layouts
							[g_hud_instrument_set_base_index +
							 TARGET_DETAIL_ELEMENT]
								.x +
						g_hud_element_layouts
							[g_hud_instrument_set_base_index +
							 TARGET_DETAIL_ELEMENT]
								.clip_width,
					g_hud_element_layouts
							[g_hud_instrument_set_base_index +
							 TARGET_DETAIL_ELEMENT]
								.y +
						g_flight_font_line_height + 1);
				g_flight_fill_clip_rect_fn();
			}
		} else {
			int player_owner_index =
				target_object->player_owner_idx;
			if (g_players[player_owner_index].hyperspace_phase !=
			    0) {
				if (g_hud_element_state_cache
					    [TARGET_CARGO_ELEMENT] !=
				    IFMSG_181_HYPERING_OUT_OF_COMBAT_AREA) {
					g_hud_element_state_cache
						[TARGET_CARGO_ELEMENT] =
							IFMSG_181_HYPERING_OUT_OF_COMBAT_AREA;
					left = g_hud_element_layouts
						       [g_hud_instrument_set_base_index +
							TARGET_CARGO_ELEMENT]
							       .x;
					top = g_hud_element_layouts
						      [g_hud_instrument_set_base_index +
						       TARGET_CARGO_ELEMENT]
							      .y;
					flight_text_set_clip_rect(
						left, top,
						left + g_hud_element_layouts
								[g_hud_instrument_set_base_index +
								 TARGET_CARGO_ELEMENT]
									.clip_width,
						top + g_flight_font_line_height +
							1);
					g_flight_fill_clip_rect_fn();
					flight_text_set_cursor(left, top);
					flight_text_set_color(0x46);
					const char *status_char = g_str_in_flight_messages
						[IFMSG_181_HYPERING_OUT_OF_COMBAT_AREA];
					for (uint16_t word_length = 0;
					     word_length <
					     MAX_STATUS_FIRST_WORD_LENGTH;
					     ++word_length, ++status_char) {
						if (*status_char == ' ') {
							strncpy(status_first_word,
								g_str_in_flight_messages
									[IFMSG_181_HYPERING_OUT_OF_COMBAT_AREA],
								word_length);
							status_first_word
								[word_length] =
									'\0';
							break;
						}
					}
#ifdef XVT_MODERN
					xvt_cockpit_text_record_field(
						XVT_COCKPIT_TEXT_TARGET_CARGO,
						status_first_word,
						XVT_COCKPIT_ALIGN_LEFT);
#endif
					flight_text_draw_string(
						status_first_word);
				}
				if (g_hud_element_state_cache
					    [TARGET_DETAIL_ELEMENT] !=
				    dirty_state) {
					g_hud_element_state_cache
						[TARGET_DETAIL_ELEMENT] =
							dirty_state;
#ifdef XVT_MODERN
					xvt_cockpit_text_clear_field(
						XVT_COCKPIT_TEXT_TARGET_DETAIL);
#endif
					flight_text_set_clip_rect(
						g_hud_element_layouts
							[g_hud_instrument_set_base_index +
							 TARGET_DETAIL_ELEMENT]
								.x,
						g_hud_element_layouts
							[g_hud_instrument_set_base_index +
							 TARGET_DETAIL_ELEMENT]
								.y,
						g_hud_element_layouts
								[g_hud_instrument_set_base_index +
								 TARGET_DETAIL_ELEMENT]
									.x +
							g_hud_element_layouts
								[g_hud_instrument_set_base_index +
								 TARGET_DETAIL_ELEMENT]
									.clip_width,
						g_hud_element_layouts
								[g_hud_instrument_set_base_index +
								 TARGET_DETAIL_ELEMENT]
									.y +
							g_flight_font_line_height +
							1);
					g_flight_fill_clip_rect_fn();
				}
			} else {
				uint16_t owner_target_object_index =
					(uint16_t)g_players[player_owner_index]
						.current_target_object_idx;
				if (owner_target_object_index != UINT16_MAX) {
					if (owner_target_object_index !=
					    g_hud_element_state_cache
						    [TARGET_CARGO_ELEMENT]) {
						g_hud_element_state_cache
							[TARGET_CARGO_ELEMENT] =
								owner_target_object_index;
						left = g_hud_element_layouts
							       [g_hud_instrument_set_base_index +
								TARGET_CARGO_ELEMENT]
								       .x;
						top = g_hud_element_layouts
							      [g_hud_instrument_set_base_index +
							       TARGET_CARGO_ELEMENT]
								      .y;
						flight_text_set_clip_rect(
							left, top,
							left + g_hud_element_layouts
									[g_hud_instrument_set_base_index +
									 TARGET_CARGO_ELEMENT]
										.clip_width,
							top + g_flight_font_line_height +
								1);
						g_flight_fill_clip_rect_fn();
						flight_text_set_cursor(left,
								       top);
						flight_text_set_color(0x46);
						const char *status_char = g_str_in_flight_messages
							[IFMSG_165_ATTACKING_TARGET];
						for (uint16_t word_length = 0;
						     word_length <
						     MAX_STATUS_FIRST_WORD_LENGTH;
						     ++word_length,
							      ++status_char) {
							if (*status_char ==
							    ' ') {
								strncpy(status_first_word,
									g_str_in_flight_messages
										[IFMSG_165_ATTACKING_TARGET],
									word_length);
								status_first_word
									[word_length] =
										'\0';
								break;
							}
						}
#ifdef XVT_MODERN
						xvt_cockpit_text_record_field(
							XVT_COCKPIT_TEXT_TARGET_CARGO,
							status_first_word,
							XVT_COCKPIT_ALIGN_LEFT);
#endif
						flight_text_draw_string(
							status_first_word);
					}
					if (owner_target_object_index !=
					    g_hud_element_state_cache
						    [TARGET_DETAIL_ELEMENT]) {
						g_hud_element_state_cache
							[TARGET_DETAIL_ELEMENT] =
								owner_target_object_index;
						left = g_hud_element_layouts
							       [g_hud_instrument_set_base_index +
								TARGET_DETAIL_ELEMENT]
								       .x;
						top = g_hud_element_layouts
							      [g_hud_instrument_set_base_index +
							       TARGET_DETAIL_ELEMENT]
								      .y;
						flight_text_set_clip_rect(
							left, top,
							left + g_hud_element_layouts
									[g_hud_instrument_set_base_index +
									 TARGET_DETAIL_ELEMENT]
										.clip_width,
							top + g_flight_font_line_height +
								1);
						g_flight_fill_clip_rect_fn();
						flight_text_set_cursor(left,
								       top);
						hud_format_object_display_name(
							(uint16_t)g_players
								[g_object_table
									 [(uint16_t)g_players
										  [g_local_player]
											  .current_target_object_idx]
										 .player_owner_idx]
									.current_target_object_idx,
							NORMAL_TARGET_DISPLAY_FLAGS);
#ifdef XVT_MODERN
						xvt_cockpit_text_record_field(
							XVT_COCKPIT_TEXT_TARGET_DETAIL,
							g_flight_text_scratch_buffer,
							XVT_COCKPIT_ALIGN_RIGHT);
#endif
						flight_text_draw_string_right_aligned(
							g_flight_text_scratch_buffer);
					}
				} else {
					if (g_hud_element_state_cache
						    [TARGET_CARGO_ELEMENT] !=
					    IFMSG_160_PATROLLING) {
						g_hud_element_state_cache
							[TARGET_CARGO_ELEMENT] =
								IFMSG_160_PATROLLING;
						left = g_hud_element_layouts
							       [g_hud_instrument_set_base_index +
								TARGET_CARGO_ELEMENT]
								       .x;
						top = g_hud_element_layouts
							      [g_hud_instrument_set_base_index +
							       TARGET_CARGO_ELEMENT]
								      .y;
						flight_text_set_clip_rect(
							left, top,
							left + g_hud_element_layouts
									[g_hud_instrument_set_base_index +
									 TARGET_CARGO_ELEMENT]
										.clip_width,
							top + g_flight_font_line_height +
								1);
						g_flight_fill_clip_rect_fn();
						flight_text_set_cursor(left,
								       top);
						flight_text_set_color(0x46);
						const char *status_char = g_str_in_flight_messages
							[IFMSG_160_PATROLLING];
						for (uint16_t word_length = 0;
						     word_length <
						     MAX_STATUS_FIRST_WORD_LENGTH;
						     ++word_length,
							      ++status_char) {
							if (*status_char ==
							    ' ') {
								strncpy(status_first_word,
									g_str_in_flight_messages
										[IFMSG_160_PATROLLING],
									word_length);
								status_first_word
									[word_length] =
										'\0';
								break;
							}
						}
#ifdef XVT_MODERN
						xvt_cockpit_text_record_field(
							XVT_COCKPIT_TEXT_TARGET_CARGO,
							status_first_word,
							XVT_COCKPIT_ALIGN_LEFT);
#endif
						flight_text_draw_string(
							status_first_word);
					}
					if ((uint16_t)g_hud_element_state_cache
						    [TARGET_DETAIL_ELEMENT] !=
					    WAYPOINT_ZERO_OBJECT_REF) {
						g_hud_element_state_cache
							[TARGET_DETAIL_ELEMENT] =
								(int16_t)
									WAYPOINT_ZERO_OBJECT_REF;
						left = g_hud_element_layouts
							       [g_hud_instrument_set_base_index +
								TARGET_DETAIL_ELEMENT]
								       .x;
						top = g_hud_element_layouts
							      [g_hud_instrument_set_base_index +
							       TARGET_DETAIL_ELEMENT]
								      .y;
						flight_text_set_clip_rect(
							left, top,
							left + g_hud_element_layouts
									[g_hud_instrument_set_base_index +
									 TARGET_DETAIL_ELEMENT]
										.clip_width,
							top + g_flight_font_line_height +
								1);
						g_flight_fill_clip_rect_fn();
						flight_text_set_cursor(left,
								       top);
						hud_format_object_display_name(
							WAYPOINT_ZERO_OBJECT_REF,
							NORMAL_TARGET_DISPLAY_FLAGS);
#ifdef XVT_MODERN
						xvt_cockpit_text_record_field(
							XVT_COCKPIT_TEXT_TARGET_DETAIL,
							g_flight_text_scratch_buffer,
							XVT_COCKPIT_ALIGN_RIGHT);
#endif
						flight_text_draw_string_right_aligned(
							g_flight_text_scratch_buffer);
					}
				}
			}
		}
	}
}

/* Empties g_flight_text_scratch_buffer and writes object_ref's display name there.
 * For a craft, display_flags bit 0 adds the model's short name, bit 1 the flight
 * group's name and the craft number when hud_mission_fg_get_craft_number_if_shown
 * gives one (at most 999), and bit 2 a ":" after the first part; bits 0 and 1
 * together put ": " there. Another mobile object gets only its warhead,
 * satellite, mine, probe or buoy name, under bit 0. Each part starts with a
 * 0xFE color escape chosen by IFF: Q, I, E, U or M before the first part and R,
 * J, F, V or N before the group, for IFF 0, 1 or 4, 2, 5 and any other; an
 * object with no mobile object takes its group's IFF and shows IFF 5 with V. A
 * reference of 0x8000 or more is waypoint object_ref - 0x8000, written after a C
 * escape when bit 0 is set, else left empty. Object type 0 gives the dashes of
 * g_str_mesh_component_names[32]. */
// FUNCTION: XVT 0x43C290
void hud_format_object_display_name(uint16_t object_ref, int16_t display_flags)
{
	g_flight_text_scratch_buffer[0] = 0;
	if (object_ref >= 0x8000) {
		if ((display_flags & 1) != 0) {
			flight_text_append_scratch_char(254);
			flight_text_append_scratch_char(67);
			flight_text_append_scratch_string(g_str_waypoint_names[(
				uint16_t)(object_ref + 0x8000)]);
		}
		return;
	}

	int object_index = object_ref;
	struct object_record *object = &g_object_table[object_index];
	uint16_t object_type;
	if (g_object_table[object_index].mobj != NULL) {
		object_type = object->object_type;
		if (object_type != 0) {
			flight_text_append_scratch_char(254);
			int8_t iff = g_object_table[object_index].mobj->iff;
			if (iff == 0) {
				flight_text_append_scratch_char(81);
			} else if (iff == 1 || iff == 4) {
				flight_text_append_scratch_char(73);
			} else if (iff == 2) {
				flight_text_append_scratch_char(69);
			} else if (iff == 5) {
				flight_text_append_scratch_char(85);
			} else {
				flight_text_append_scratch_char(77);
			}

			struct mobile_object *mobile_object =
				g_object_table[object_index].mobj;
			if (mobile_object->family == 0) {
				struct craft_data *craft =
					mobile_object->p_craft;
				if ((display_flags & 1) != 0) {
					flight_text_append_scratch_string(
						g_model_defs[craft->model_index]
							.name);
				}
				if ((display_flags & 4) != 0) {
					flight_text_append_scratch_char(58);
				} else if ((display_flags & 3) == 3) {
					flight_text_append_scratch_char(58);
					flight_text_append_scratch_char(32);
				}
				if ((display_flags & 2) != 0) {
					flight_text_append_scratch_char(254);
					iff = g_object_table[object_index]
						      .mobj->iff;
					if (iff == 0) {
						flight_text_append_scratch_char(
							82);
					} else if (iff == 1 || iff == 4) {
						flight_text_append_scratch_char(
							74);
					} else if (iff == 2) {
						flight_text_append_scratch_char(
							70);
					} else if (iff == 5) {
						flight_text_append_scratch_char(
							86);
					} else {
						flight_text_append_scratch_char(
							78);
					}
					int flight_group_idx =
						g_object_table[object_index]
							.flight_group_idx;
					flight_text_append_scratch_string(
						g_mission_flight_groups
							[flight_group_idx]
								.fg.name);
					int16_t craft_number =
						hud_mission_fg_get_craft_number_if_shown(
							flight_group_idx,
							craft);
					if (craft_number != 0) {
						flight_text_append_scratch_char(
							32);
						if ((uint16_t)craft_number >=
						    1000) {
							craft_number = 999;
						}
						uint16_t tens_digit;
						uint16_t ones_digit;
						if ((uint16_t)craft_number >=
						    100) {
							uint16_t hundreds_digit =
								(uint16_t)
									craft_number /
								100;
							tens_digit =
								(uint16_t)
									craft_number %
								100 / 10;
							ones_digit =
								(uint16_t)
									craft_number %
								100 % 10;
							flight_text_append_scratch_char(
								hundreds_digit +
								48);
							flight_text_append_scratch_char(
								tens_digit +
								48);
							flight_text_append_scratch_char(
								ones_digit +
								48);
						} else if (
							(uint16_t)
								craft_number >=
							10) {
							tens_digit =
								(uint16_t)
									craft_number /
								10;
							ones_digit =
								(uint16_t)
									craft_number %
								10;
							flight_text_append_scratch_char(
								tens_digit +
								48);
							flight_text_append_scratch_char(
								ones_digit +
								48);
						} else {
							flight_text_append_scratch_char(
								craft_number +
								48);
						}
					}
				}
			} else if ((display_flags & 1) != 0) {
				if (object_type >= 0x8f &&
				    object_type <= 0x9b) {
					flight_text_append_scratch_string(
						g_str_warhead_names
							[object_type - 0x8f]);
				} else if (object_type >= 0x46 &&
					   object_type <= 0x54) {
					flight_text_append_scratch_string(
						g_str_sat_mine_probe_buoy_pilot_names
							[object_type - 0x46]);
				}
			}
			return;
		}
	} else {
		object_type = object->object_type;
		if (object_type != 0) {
			flight_text_append_scratch_char(254);
			uint8_t static_iff =
				g_mission_flight_groups
					[g_object_table[object_index]
						 .flight_group_idx]
						.fg.iff;
			if (static_iff == 0) {
				flight_text_append_scratch_char(81);
			} else if (static_iff == 1 || static_iff == 4) {
				flight_text_append_scratch_char(73);
			} else if (static_iff == 2) {
				flight_text_append_scratch_char(69);
			} else if (static_iff == 5) {
				flight_text_append_scratch_char(86);
			} else {
				flight_text_append_scratch_char(77);
			}
			if ((display_flags & 1) != 0 && object_type >= 0x46 &&
			    object_type <= 0x55) {
				flight_text_append_scratch_string(
					g_str_sat_mine_probe_buoy_pilot_names
						[object_type - 0x46]);
			}
			if ((display_flags & 4) != 0) {
				flight_text_append_scratch_char(58);
			} else if ((display_flags & 3) == 3) {
				flight_text_append_scratch_char(58);
				flight_text_append_scratch_char(32);
			}
			if ((display_flags & 2) != 0) {
				flight_text_append_scratch_char(254);
				if (static_iff == 0) {
					flight_text_append_scratch_char(82);
				} else if (static_iff == 1 || static_iff == 4) {
					flight_text_append_scratch_char(74);
				} else if (static_iff == 2) {
					flight_text_append_scratch_char(70);
				} else if (static_iff == 5) {
					flight_text_append_scratch_char(86);
				} else {
					flight_text_append_scratch_char(78);
				}
				flight_text_append_scratch_string(
					g_mission_flight_groups
						[g_object_table[object_index]
							 .flight_group_idx]
							.fg.name);
			}
			return;
		}
	}
	flight_text_set_scratch(g_str_mesh_component_names[32]);
}

/* Returns craft's craft_index_in_group, or 0 when flight group flight_group_idx
 * turns its craft numbering off, or has one craft, no further arrivals and no
 * global unit. */
// FUNCTION: XVT 0x43C740
int hud_mission_fg_get_craft_number_if_shown(int flight_group_idx,
					     const struct craft_data *craft)
{
	if (g_mission_flight_groups[flight_group_idx]
			    .fg.disable_wave_numbering == 1 ||
	    (g_mission_flight_groups[flight_group_idx].fg.global_unit == 0 &&
	     g_mission_flight_groups[flight_group_idx].fg.number_of_craft ==
		     1 &&
	     g_mission_flight_groups[flight_group_idx].fg.number_of_waves ==
		     0)) {
		return 0;
	}

	return craft->craft_index_in_group;
}

/* Draws a distance on the targeting computer with two decimals: polar_distance *
 * 161 / 65,536 in hundredths, at most 99.99; the whole part in element 83 and
 * the hundredths, two digits, in element 84 of the current set. Does not check
 * polar_distance * 161 for overflow. */
// FUNCTION: XVT 0x43C890
void hud_draw_target_distance(int polar_distance)
{
	uint16_t distance_hundredths = (uint32_t)(polar_distance * 161) >> 16;
	if (distance_hundredths >= 10000) {
		distance_hundredths = 9999;
	}

	uint16_t whole_distance = distance_hundredths / 100;
	hud_draw_cached_numeric_element(g_hud_instrument_set_base_index + 83,
					whole_distance, 1);
	hud_draw_cached_numeric_element(
		g_hud_instrument_set_base_index + 84,
		distance_hundredths - whole_distance * 100, 2);
}

/* Draws the lock indicator (element 52 of the current set) and sets the
 * targeting tone to the same state. With selected_weapon_mode 0 the state is 4
 * while g_target_lock_active is set, else 0; with warheads it is missile_lock_state
 * plus 1 with a target, else 1, and g_target_lock_active becomes 1 only while
 * missile_lock_state is 2. */
// FUNCTION: XVT 0x43C900
void hud_update_targeting_lock_indicator(void)
{
	uint16_t indicator_state;

	if (g_players[g_local_player].selected_weapon_mode == 0) {
		indicator_state = g_target_lock_active == 0 ? 0 : 4;
	} else {
		if (g_players[g_local_player].current_target_object_idx != -1) {
			indicator_state =
				g_players[g_local_player].missile_lock_state +
				1;
		} else {
			indicator_state = 1;
		}
		g_target_lock_active = 1;
		if (g_players[g_local_player].missile_lock_state != 2) {
			g_target_lock_active = 0;
		}
	}
	if (indicator_state !=
	    (uint16_t)
		    g_hud_element_state_cache[g_hud_instrument_set_base_index +
					      52]) {
		XVT_LOG_DEBUG(
			"hud.lock_indicator slot=%d state=%u from=%d mode=%d target=%d lock=%d",
			g_local_player, (unsigned)indicator_state,
			(int)g_hud_element_state_cache
				[g_hud_instrument_set_base_index + 52],
			(int)g_players[g_local_player].selected_weapon_mode,
			(int)g_players[g_local_player]
				.current_target_object_idx,
			(int)g_players[g_local_player].missile_lock_state);
	}

	hud_draw_cached_sprite_element(g_hud_instrument_set_base_index + 52,
				       indicator_state);
	flight_surface_unlock();
	fsfx_update_targeting_tone(indicator_state);
	flight_surface_lock();
}

/* Draws each laser cannon's charge, fire and lock sprites for the local
 * player's craft. In the forward view with HUD features 2 and 4 it draws the
 * charge bar of each cannon placed in the current set (element 3 plus the
 * cannon): ten sprites 3, 4 or 6 pixels apart by resolution, right to left when
 * the layout's color_index_or_widget_param is set; up to half charge the charged
 * ones are in state 1 over 0, above it in state 2 over 1. Outside the cockpit
 * set it draws a number from the charge instead. From the bank's link mode and
 * next cannon it works out whether the cannon fires next and draws that at
 * element 53 plus the cannon; for an X-wing, Y-wing, A-wing, Z-95 or B-wing
 * also at element 11 plus the cannon, and the lock state at element 61 plus the
 * cannon. The lock state is 2 when the targeting computer works and
 * collide_would_shot_hit_target says a cannon due to fire would hit the target.
 * Sets g_target_lock_active to 0 first and to 1 on any such hit; returns at once
 * for a craft with no cannons. */
// FUNCTION: XVT 0x43C9A0
void hud_draw_laser_cannon_indicators(void)
{
	enum {
		LASER_CHARGE_HALF = 64,
		LASER_CHARGE_SCALE = 6,
		LASER_CHARGE_SEGMENT_COUNT = 10,
		LASER_CHARGE_DENOMINATOR = 127,
		LASER_PERCENT_SCALE = 655,
		LASER_CHARGE_ELEMENT_BASE = 3,
		LASER_SELECTION_ELEMENT_BASE = 11,
		LASER_READY_ELEMENT_BASE = 53,
		LASER_LOCK_ELEMENT_BASE = 61,
		DEFAULT_HUD_WIDTH = 320,
		DEFAULT_HUD_HEIGHT = 200,
	};

	g_target_lock_active = 0;
	struct craft_data *craft =
		g_object_table[g_players[g_local_player].object_index]
			.mobj->p_craft;
	uint16_t laser_slot = 0;
	uint16_t laser_slot_count = craft->laser_slot_count;
	if (laser_slot_count == 0) {
		return;
	}

	uint8_t *laser_group_last_slot =
		g_model_defs[craft->model_index].laser_group_last_slot;
	for (; laser_slot_count > laser_slot; ++laser_slot) {
		uint16_t laser_bank = laser_slot > *laser_group_last_slot;
		int layout_index = g_hud_instrument_set_base_index + laser_slot;
		int x = (int16_t)
				g_hud_element_layouts[layout_index +
						      LASER_CHARGE_ELEMENT_BASE]
					.x;
		int y = g_hud_element_layouts[layout_index +
					      LASER_CHARGE_ELEMENT_BASE]
				.y;
		if (x + y == 0 && g_hud_instrument_set_base_index ==
					  HUD_COCKPIT_INSTRUMENT_BASE_INDEX) {
			continue;
		}

		uint16_t selector =
			g_hud_element_layouts[layout_index +
					      LASER_CHARGE_ELEMENT_BASE]
				.selector;
		int16_t charge = craft->weapon_slots[laser_slot].laser_charge;
		int16_t charged_segment_state = 0;
		if (g_players[g_local_player].view_state.hud_state_live ==
			    HUD_VIEW_FORWARD &&
		    (craft->damage_stats.active_hud_feature_mask & 2) != 0 &&
		    (craft->damage_stats.active_hud_feature_mask & 4) != 0) {
			uint16_t charged_segment_count;
			int16_t empty_segment_state;

			if (charge > 0 && (craft->working_subsystems &
					   CRAFT_SUBSYSTEM_FLAG_CANNONS) != 0) {
				++charge;
				if (charge <= LASER_CHARGE_HALF) {
					empty_segment_state = 0;
					charged_segment_state = 1;
				} else {
					empty_segment_state = 1;
					charged_segment_state = 2;
					charge -= LASER_CHARGE_HALF;
				}
				charged_segment_count =
					(uint16_t)charge / LASER_CHARGE_SCALE;
				if (charged_segment_count >=
				    LASER_CHARGE_SEGMENT_COUNT + 1) {
					charged_segment_count =
						LASER_CHARGE_SEGMENT_COUNT;
				}
			} else {
				charged_segment_count = 0;
				empty_segment_state = 0;
				charged_segment_state = 0;
			}

			if ((uint16_t)g_hud_element_state_cache
				    [layout_index +
				     LASER_CHARGE_ELEMENT_BASE] !=
			    charged_segment_count) {
				g_hud_element_state_cache
					[layout_index +
					 LASER_CHARGE_ELEMENT_BASE] =
						charged_segment_count;
				int16_t x_step = 3;
				if (g_flight_resolution_mode !=
				    FLIGHT_RESOLUTION_320X240) {
					if (g_flight_resolution_mode ==
					    FLIGHT_RESOLUTION_480X360) {
						x_step = 4;
					} else {
						x_step = 6;
					}
				}
				uint16_t reverse_direction = 0;
				if (g_hud_element_layouts
					    [layout_index +
					     LASER_CHARGE_ELEMENT_BASE]
						    .color_index_or_widget_param !=
				    0) {
					x_step = -x_step;
					reverse_direction = 1;
				}

				if (g_hud_instrument_set_base_index !=
				    HUD_COCKPIT_INSTRUMENT_BASE_INDEX) {
					if (selector != 0) {
						uint16_t charge_percent =
							math2_ratio_q16(
								(uint16_t)
									charge,
								LASER_CHARGE_DENOMINATOR) /
							LASER_PERCENT_SCALE;

						flight_sw_set_render_target(
							g_flight_offscreen_buffer,
							g_screen_width,
							g_screen_height,
							g_screen_width *
								g_flight_bytes_per_pixel);
						flight_text_set_font_tier(2);
						flight_text_set_clip_rect(
							0, 0, g_screen_width,
							g_screen_height);
						flight_text_set_cursor(x, y);
						flight_text_set_background_color(
							0);
						flight_text_set_color(0x4A);
#ifdef XVT_MODERN
						xvt_cockpit_readouts_record_number(
							(xvt_cockpit_number_id)(XVT_COCKPIT_NUMBER_LASER_FIRST +
										laser_slot),
							charge_percent,
							selector, 1);
#endif
						flight_text_draw_decimal_number(
							charge_percent,
							selector, 1);
						flight_sw_set_render_target(
							NULL, DEFAULT_HUD_WIDTH,
							DEFAULT_HUD_HEIGHT, 0);
					}
				} else {
					for (uint16_t segment = 0;
					     segment <
					     LASER_CHARGE_SEGMENT_COUNT;
					     ++segment) {
						uint16_t sprite_state =
							segment < charged_segment_count
								? charged_segment_state
								: empty_segment_state;
						if (g_hud_panel_sprite_data_by_index
							    [selector +
							     sprite_state] ==
						    NULL) {
							XVT_LOG_ERROR(
								"hud.sprite_missing element=%d sprite=%u",
								(int)(layout_index +
								      LASER_CHARGE_ELEMENT_BASE),
								(unsigned)(selector +
									   sprite_state));
						}

						g_flight_blit_sprite_fn(
							g_hud_panel_sprite_data_by_index
								[selector +
								 sprite_state],
							x + x_step * segment, y,
							253, reverse_direction);
					}
				}
			}
		}

		/* lock_state first holds this cannon's fire-ready state, drawn
		 * on the ready indicator; after that draw it becomes the target
		 * lock state. */
		uint16_t lock_state = 0;
		uint16_t ready_state;
		if (charge > 0 && (craft->working_subsystems &
				   CRAFT_SUBSYSTEM_FLAG_CANNONS) != 0) {
			if (g_players[g_local_player].selected_weapon_mode ==
				    0 &&
			    g_players[g_local_player].selected_weapon_bank ==
				    laser_bank) {
				switch (craft->laser_state
						.link_mode[laser_bank]) {
				case 0:
				case 4:
					ready_state = 0;
					break;
				case 1:
					ready_state =
						craft->laser_state.next_slot
									[laser_bank] ==
								laser_slot
							? 3
							: 1;
					break;
				case 2:
					if (craft->laser_state.next_slot
							    [laser_bank] ==
						    laser_slot ||
					    (laser_slot_count >= 4 &&
					     (int)craft->laser_state.next_slot
								     [laser_bank] -
							     laser_slot ==
						     -2)) {
						ready_state = 3;
					} else {
						ready_state = 1;
					}
					break;
				case 3:
					ready_state = 3;
					break;
				default:
					ready_state = 0;
					break;
				}
				lock_state = ready_state;
				if (ready_state == 3 &&
				    craft->laser_state.fire_cooldown_ticks
						    [laser_bank] != 0) {
					ready_state = 5;
					lock_state = 2;
				}
			} else {
				ready_state = 1;
			}
		} else {
			ready_state = 0;
		}

		if (charged_segment_state == 2) {
			++ready_state;
		}

		int object_index = g_players[g_local_player].object_index;
		int is_rebel_fighter =
			object_index != -1 &&
			(g_object_table[object_index].object_type == 1 ||
			 g_object_table[object_index].object_type == 2 ||
			 g_object_table[object_index].object_type == 3 ||
			 g_object_table[object_index].object_type == 14 ||
			 g_object_table[object_index].object_type == 4);
		if (is_rebel_fighter &&
		    g_players[g_local_player].view_state.hud_state_live ==
			    HUD_VIEW_FORWARD &&
		    (craft->damage_stats.active_hud_feature_mask & 2) != 0 &&
		    (craft->damage_stats.active_hud_feature_mask & 4) != 0 &&
		    (uint16_t)g_hud_element_layouts
					    [g_hud_instrument_set_base_index +
					     laser_slot +
					     LASER_SELECTION_ELEMENT_BASE]
						    .x +
				    (uint16_t)g_hud_element_layouts
					    [g_hud_instrument_set_base_index +
					     laser_slot +
					     LASER_SELECTION_ELEMENT_BASE]
						    .y !=
			    0) {
			hud_draw_cached_sprite_element(
				g_hud_instrument_set_base_index + laser_slot +
					LASER_SELECTION_ELEMENT_BASE,
				ready_state);
		}

		hud_draw_cached_sprite_element(g_hud_instrument_set_base_index +
						       laser_slot +
						       LASER_READY_ELEMENT_BASE,
					       lock_state);
		if ((craft->working_subsystems &
		     CRAFT_SUBSYSTEM_FLAG_TARGETING_COMPUTER) != 0 &&
		    lock_state == 3) {
			uint16_t target_object_index =
				g_players[g_local_player]
					.current_target_object_idx;

			if (target_object_index != UINT16_MAX &&
			    (uint16_t)collide_would_shot_hit_target(
				    g_players[g_local_player].object_index,
				    target_object_index, laser_slot) != 0) {
				g_target_lock_active = 1;
				lock_state = 2;
			} else {
				lock_state = 1;
			}
		} else if ((craft->working_subsystems &
			    CRAFT_SUBSYSTEM_FLAG_TARGETING_COMPUTER) == 0) {
			lock_state = 0;
		} else if (lock_state == 2) {
			lock_state = 1;
		}

		if (lock_state == 3) {
			lock_state = 0;
		}
#ifdef XVT_MODERN
		xvt_cockpit_instruments_record_laser_lock(laser_slot,
							  lock_state);
#endif

		object_index = g_players[g_local_player].object_index;
		is_rebel_fighter =
			object_index != -1 &&
			(g_object_table[object_index].object_type == 1 ||
			 g_object_table[object_index].object_type == 2 ||
			 g_object_table[object_index].object_type == 3 ||
			 g_object_table[object_index].object_type == 14 ||
			 g_object_table[object_index].object_type == 4);
		if (is_rebel_fighter) {
			hud_draw_cached_sprite_element(
				g_hud_instrument_set_base_index + laser_slot +
					LASER_LOCK_ELEMENT_BASE,
				lock_state);
		}
	}
}

/* Draws the local player's warhead counts when HUD feature 8 is on and the
 * model has warhead launchers: the first and last slots of launcher group 0 in
 * display slots 0 and 1, and for the missile boat those of group 1 in slots 2
 * and 3. */
// FUNCTION: XVT 0x43D010
void hud_update_warhead_cnt(void)
{
	struct object_record *object =
		&g_object_table[g_players[g_local_player].object_index];
	if ((object->mobj->p_craft->damage_stats.active_hud_feature_mask & 8) !=
	    0) {
		int16_t first_launcher_slot_count =
			g_model_defs[get_model_index_from_type(
					     object->object_type)]
				.warhead_launcher_slot_count[0];
		if ((uint16_t)(first_launcher_slot_count +
			       g_model_defs
				       [get_model_index_from_type(
						g_object_table
							[g_players[g_local_player]
								 .object_index]
								.object_type)]
					       .warhead_launcher_slot_count
						       [1]) != 0) {
			hud_output_warhead_count(
				g_model_defs
					[get_model_index_from_type(
						 g_object_table
							 [g_players[g_local_player]
								  .object_index]
								 .object_type)]
						.warhead_launcher_first_slot[0],
				0, 0);
			hud_output_warhead_count(
				g_model_defs
					[get_model_index_from_type(
						 g_object_table
							 [g_players[g_local_player]
								  .object_index]
								 .object_type)]
						.warhead_launcher_last_slot[0],
				1, 0);

			if (get_model_index_from_type(
				    CRAFT_SPECIES_MISSILE_BOAT) ==
			    get_model_index_from_type(
				    g_object_table[g_players[g_local_player]
							   .object_index]
					    .object_type)) {
				hud_output_warhead_count(
					g_model_defs
						[get_model_index_from_type(
							 g_object_table
								 [g_players[g_local_player]
									  .object_index]
									 .object_type)]
							.warhead_launcher_first_slot
								[1],
					2, 1);
				hud_output_warhead_count(
					g_model_defs
						[get_model_index_from_type(
							 g_object_table
								 [g_players[g_local_player]
									  .object_index]
									 .object_type)]
							.warhead_launcher_last_slot
								[1],
					3, 1);
			}
		}
	}
}

/* Draws the warhead count of weapon slot warhead_slot_idx (0 with no launchers)
 * for display slot display_slot, and its launcher's selection state: 0 with no
 * warheads or the launcher out, 1 with lasers selected or another bank; with
 * bank warhead_bank selected, 2 for both slots when the low bits of its
 * warhead_launcher_flags are 3, else 2 for the slot whose side matches bit 7 and
 * 1 for the other. In the cockpit set the count is drawn at layout 27 plus the
 * slot when it changed, two digits for the missile boat else one, and the state
 * as a sprite at element 19 plus the slot (an X-wing, Y-wing, A-wing, Z-95 or
 * B-wing shows state 2 as 4). In other sets it draws the count every call while
 * layout 27 plus the slot has a selector, in color code 0x52 for state 2 else
 * 0x4A, at (slot * (g_flight_font_digit_width + 1) + 2, 2) on
 * g_flight_offscreen_buffer, from where hud_blit_software_mfd_pages copies it, or
 * clears that cell for 0. Records the count in g_hud_element_state_cache and
 * leaves g_flight_text_shadow_enabled at 0 when it draws. */
// FUNCTION: XVT 0x43D2B0
void hud_output_warhead_count(uint16_t warhead_slot_idx, uint16_t display_slot,
			      uint16_t warhead_bank)
{
	struct craft_data *craft;
	uint16_t warhead_count;
	uint16_t selection_state;
	uint8_t launcher_flags;
	model_index craft_model_index;

	if (g_hud_instrument_set_base_index !=
	    HUD_COCKPIT_INSTRUMENT_BASE_INDEX) {
		flight_sw_set_render_target(g_flight_offscreen_buffer,
					    g_screen_width, g_screen_height,
					    g_screen_width *
						    g_flight_bytes_per_pixel);
		int local_player = g_local_player;
		struct mobile_object **player_mobile_object =
			&g_object_table[g_players[local_player].object_index]
				 .mobj;
		craft = (*player_mobile_object)->p_craft;
		if (craft->warhead_launcher_count == 0) {
			warhead_count = 0;
		} else {
			warhead_count = craft->weapon_slots[warhead_slot_idx]
						.ammo_count;
		}
		g_hud_element_state_cache[display_slot + 27 +
					  g_hud_instrument_set_base_index] =
			(int16_t)warhead_count;

		if (warhead_count != 0) {
			craft = (*player_mobile_object)->p_craft;
			if ((craft->working_subsystems &
			     CRAFT_SUBSYSTEM_FLAG_WARHEAD_LAUNCHER) == 0) {
				selection_state = 0;
			} else if (g_players[local_player]
					   .selected_weapon_mode == 0) {
				selection_state = 1;
			} else {
				if (warhead_bank !=
				    g_players[local_player]
					    .selected_weapon_bank) {
					selection_state = 1;
				} else {
					launcher_flags =
						(uint8_t)craft->warhead_launcher_flags
							[g_players[local_player]
								 .selected_weapon_bank];
					if ((launcher_flags & 0x7F) == 3) {
						selection_state = 2;
					} else {
						selection_state =
							((display_slot & 1) ==
							 (launcher_flags >>
							  7)) +
							1;
					}
				}
			}
		} else {
			selection_state = 0;
		}

		if (warhead_count != 0) {
			flight_text_set_font_tier(2);
			flight_text_set_clip_rect(0, 0, g_screen_width,
						  g_screen_height);
			flight_text_set_cursor(
				display_slot * (g_flight_font_digit_width + 1) +
					2,
				2);
			flight_text_set_background_color(
				g_flight_transparent_color_index);
			if (selection_state == 2) {
				flight_text_set_color(0x52);
			} else {
				flight_text_set_color(0x4A);
			}
			g_flight_text_shadow_enabled = 0;
			if (g_hud_element_layouts
				    [display_slot + 27 +
				     g_hud_instrument_set_base_index]
					    .selector != 0) {
				flight_text_set_font_tier(0);
				craft_model_index = get_model_index_from_type(
					g_object_table[g_players[g_local_player]
							       .object_index]
						.object_type);
				if (get_model_index_from_type(
					    CRAFT_SPECIES_MISSILE_BOAT) ==
				    craft_model_index) {
#ifdef XVT_MODERN
					xvt_cockpit_readouts_record_number(
						(xvt_cockpit_number_id)(XVT_COCKPIT_NUMBER_LAUNCHER_FIRST +
									display_slot),
						warhead_count, 2, 1);
#endif
					flight_text_draw_decimal_number(
						warhead_count, 2, 1);
				} else {
#ifdef XVT_MODERN
					xvt_cockpit_readouts_record_number(
						(xvt_cockpit_number_id)(XVT_COCKPIT_NUMBER_LAUNCHER_FIRST +
									display_slot),
						warhead_count, 1, 1);
#endif
					flight_text_draw_decimal_number(
						warhead_count, 1, 1);
				}
			}
		} else {
			flight_text_set_font_tier(2);
			flight_text_set_clip_rect(
				display_slot * (g_flight_font_digit_width + 1) +
					2,
				2,
				g_flight_font_digit_width +
					display_slot *
						(g_flight_font_digit_width +
						 1) +
					3,
				g_flight_font_line_height + 3);
#ifdef XVT_MODERN
			xvt_cockpit_readouts_clear_launcher(display_slot);
#endif
			flight_text_set_background_color(
				g_flight_transparent_color_index);
			g_flight_fill_clip_rect_fn();
		}

		flight_sw_set_render_target(NULL, 320, 200, 0);
		return;
	}

	craft = g_object_table[g_players[g_local_player].object_index]
			.mobj->p_craft;
	if (craft->warhead_launcher_count == 0) {
		warhead_count = 0;
	} else {
		warhead_count =
			craft->weapon_slots[warhead_slot_idx].ammo_count;
	}
	if (g_hud_element_state_cache[display_slot + 27 +
				      g_hud_instrument_set_base_index] !=
	    (int16_t)warhead_count) {
		g_hud_element_state_cache[display_slot + 27 +
					  g_hud_instrument_set_base_index] =
			(int16_t)warhead_count;
		flight_text_set_font_tier(2);
		flight_text_set_clip_rect(0, 0, g_screen_width,
					  g_screen_height);
		flight_text_set_cursor(
			g_hud_element_layouts[display_slot + 27 +
					      g_hud_instrument_set_base_index]
				.x,
			g_hud_element_layouts[display_slot + 27 +
					      g_hud_instrument_set_base_index]
				.y);
		flight_text_set_background_color(
			g_flight_transparent_color_index);
		flight_text_set_color(0x4A);
		g_flight_text_shadow_enabled = 0;
		craft_model_index = get_model_index_from_type(
			g_object_table[g_players[g_local_player].object_index]
				.object_type);
		if (get_model_index_from_type(CRAFT_SPECIES_MISSILE_BOAT) ==
		    craft_model_index) {
#ifdef XVT_MODERN
			xvt_cockpit_readouts_record_number(
				(xvt_cockpit_number_id)(XVT_COCKPIT_NUMBER_LAUNCHER_FIRST +
							display_slot),
				warhead_count, 2, 1);
#endif
			flight_text_draw_decimal_number(warhead_count, 2, 1);
		} else {
#ifdef XVT_MODERN
			xvt_cockpit_readouts_record_number(
				(xvt_cockpit_number_id)(XVT_COCKPIT_NUMBER_LAUNCHER_FIRST +
							display_slot),
				warhead_count, 1, 1);
#endif
			flight_text_draw_decimal_number(warhead_count, 1, 1);
		}
	}

	if (warhead_count != 0) {
		craft = g_object_table[g_players[g_local_player].object_index]
				.mobj->p_craft;
		if ((craft->working_subsystems &
		     CRAFT_SUBSYSTEM_FLAG_WARHEAD_LAUNCHER) == 0) {
			selection_state = 0;
		} else if (g_players[g_local_player].selected_weapon_mode ==
			   0) {
			selection_state = 1;
		} else {
			if (warhead_bank !=
			    g_players[g_local_player].selected_weapon_bank) {
				selection_state = 1;
			} else {
				launcher_flags =
					(uint8_t)craft->warhead_launcher_flags
						[g_players[g_local_player]
							 .selected_weapon_bank];
				if ((launcher_flags & 0x7F) == 3) {
					selection_state = 2;
				} else {
					selection_state =
						((display_slot & 1) ==
						 (launcher_flags >> 7)) +
						1;
				}
			}
		}
	} else {
		selection_state = 0;
	}

	int object_index = g_players[g_local_player].object_index;
	int is_rebel_fighter =
		object_index != -1 &&
		(g_object_table[object_index].object_type == 1 ||
		 g_object_table[object_index].object_type == 2 ||
		 g_object_table[object_index].object_type == 3 ||
		 g_object_table[object_index].object_type == 14 ||
		 g_object_table[object_index].object_type == 4);
	if (is_rebel_fighter && selection_state == 2) {
		selection_state = 4;
	}
	hud_draw_cached_sprite_element(display_slot + 19, selection_state);
}

/* Draws the local player's front and rear shields (shield_energy 0 and 1, 0
 * while the shield system is out) against half of craft_get_object_max_shield, and
 * the hull, when HUD feature 0x20 is on. Where the layout's
 * color_index_or_widget_param is not 0xFFFF, each side is two faded sprites
 * (elements 35 and 36 front, 37 and 38 rear) colored from g_hud_shield_colors by
 * a level 0 to 9 of the charge up to full and of the charge above it; while
 * shield_hit_flash_timer runs, the side in g_last_shield_damage_side shows level 10
 * in place of its overcharge level, or of its main level when it has none.
 * Otherwise each side is a percent, above 100 when overcharged, drawn when it
 * changes in the matching text color. The hull sprite, element 39, is 3 while
 * hull_hit_flash_timer runs, else 2, 1 or 0 as hull_damage passes each third of
 * hull_max. */
// FUNCTION: XVT 0x43D800
void hud_draw_shield_strength2d(void)
{
	struct craft_data *craft =
		g_object_table[g_players[g_local_player].object_index]
			.mobj->p_craft;

	if ((craft->damage_stats.active_hud_feature_mask & 0x20u) == 0) {
		return;
	}

	int shield = craft->shield_energy[0];
	if (shield < 0) {
		shield = 0;
	}
	if (!(craft->working_subsystems & CRAFT_SUBSYSTEM_FLAG_SHIELDS)) {
		shield = 0;
	}
	int max_shield = craft_get_object_max_shield(
				 g_players[g_local_player].object_index) /
			 2;
	if (g_hud_element_layouts[g_hud_instrument_set_base_index + 35]
		    .color_index_or_widget_param != 0xFFFFu) {
		uint16_t shield_ratio_q16;
		uint16_t strength_level;
		uint16_t shield_percent;
		if (max_shield <= shield) {
			strength_level = 9;
			shield_ratio_q16 = math2_longratio_q16(
				(unsigned int)(shield - max_shield),
				(unsigned int)max_shield);
			shield_percent = math2_longfraction(
				9, (uint16_t)shield_ratio_q16);
		} else {
			shield_ratio_q16 = math2_longratio_q16(
				(unsigned int)shield, (unsigned int)max_shield);
			strength_level = math2_longfraction(
				9, (uint16_t)shield_ratio_q16);
			shield_percent = 0;
		}
		if (g_player_flight_transient_timers[g_local_player]
				    .shield_hit_flash_timer != 0 &&
		    g_last_shield_damage_side == 0) {
			if (shield_percent == 0) {
				strength_level = 10;
			} else {
				shield_percent = 10;
			}
		}
		int16_t primary_fade =
			strength_level != 0
				? g_hud_element_layouts
					  [g_hud_instrument_set_base_index + 35]
						  .clip_width
				: -1;
		int16_t secondary_fade =
			shield_percent != 0
				? g_hud_element_layouts
					  [g_hud_instrument_set_base_index + 36]
						  .clip_width
				: -1;
		hud_draw_cached_faded_sprite_element(
			(uint16_t)(g_hud_instrument_set_base_index + 35),
			(int16_t)g_hud_shield_colors[strength_level],
			primary_fade);
		hud_draw_cached_faded_sprite_element(
			(uint16_t)(g_hud_instrument_set_base_index + 36),
			(int16_t)g_hud_shield_colors[shield_percent],
			secondary_fade);
	} else {
		uint16_t strength_level;
		uint16_t shield_ratio_q16;
		uint16_t shield_percent;
		if (max_shield <= shield) {
			shield_ratio_q16 = math2_longratio_q16(
				(unsigned int)(shield - max_shield),
				(unsigned int)max_shield);
			strength_level = 9;
			shield_percent =
				(uint16_t)(shield_ratio_q16 / 0x28Fu + 100);
		} else {
			shield_ratio_q16 = math2_longratio_q16(
				(unsigned int)shield, (unsigned int)max_shield);
			strength_level = math2_longfraction(
				9, (uint16_t)shield_ratio_q16);
			shield_percent = (uint16_t)(shield_ratio_q16 / 0x28Fu);
		}
		if ((uint16_t)g_hud_element_state_cache
			    [g_hud_instrument_set_base_index + 35] !=
		    shield_percent) {
			flight_text_set_font_tier(2);
			flight_text_set_clip_rect(
				g_hud_element_layouts
					[g_hud_instrument_set_base_index + 35]
						.x,
				g_hud_element_layouts
					[g_hud_instrument_set_base_index + 35]
						.y,
				g_hud_element_layouts
						[g_hud_instrument_set_base_index +
						 35]
							.x +
					flight_text_measure_string_width(
						"123%"),
				g_hud_element_layouts
						[g_hud_instrument_set_base_index +
						 35]
							.y +
					g_flight_font_line_height);
			flight_text_set_background_color(0x40);
			g_flight_fill_clip_rect_fn();
			flight_text_set_color(
				g_hud_shield_colors
					[HUD_SHIELD_TEXT_COLOR_OFFSET +
					 strength_level]);
			flight_text_set_cursor(
				g_hud_element_layouts
					[g_hud_instrument_set_base_index + 35]
						.x,
				g_hud_element_layouts
					[g_hud_instrument_set_base_index + 35]
						.y);
			flight_text_format_scratch_int(shield_percent);
			flight_text_append_scratch_char('%');
#ifdef XVT_MODERN
			xvt_cockpit_text_record_field(
				XVT_COCKPIT_TEXT_SHIELD_FORE,
				g_flight_text_scratch_buffer,
				XVT_COCKPIT_ALIGN_RIGHT);
#endif
			flight_text_draw_string_right_aligned(
				g_flight_text_scratch_buffer);
			g_hud_element_state_cache
				[g_hud_instrument_set_base_index + 35] =
					(int16_t)shield_percent;
		}
	}
	craft = g_object_table[g_players[g_local_player].object_index]
			.mobj->p_craft;
	shield = craft->shield_energy[1];
	if (shield < 0) {
		shield = 0;
	}
	if (!(craft->working_subsystems & CRAFT_SUBSYSTEM_FLAG_SHIELDS)) {
		shield = 0;
	}
	max_shield = craft_get_object_max_shield(
			     g_players[g_local_player].object_index) /
		     2;
	if (g_hud_element_layouts[g_hud_instrument_set_base_index + 37]
		    .color_index_or_widget_param != 0xFFFFu) {
		uint16_t shield_ratio_q16;
		uint16_t strength_level;
		uint16_t secondary_level;
		if (max_shield <= shield) {
			strength_level = 9;
			shield_ratio_q16 = math2_longratio_q16(
				(unsigned int)(shield - max_shield),
				(unsigned int)max_shield);
			secondary_level = math2_longfraction(
				9, (uint16_t)shield_ratio_q16);
		} else {
			shield_ratio_q16 = math2_longratio_q16(
				(unsigned int)shield, (unsigned int)max_shield);
			strength_level = math2_longfraction(
				9, (uint16_t)shield_ratio_q16);
			secondary_level = 0;
		}
		if (g_player_flight_transient_timers[g_local_player]
				    .shield_hit_flash_timer != 0 &&
		    g_last_shield_damage_side == 1) {
			if (secondary_level == 0) {
				strength_level = 10;
			} else {
				secondary_level = 10;
			}
		}
		int16_t primary_fade =
			strength_level != 0
				? g_hud_element_layouts
					  [g_hud_instrument_set_base_index + 37]
						  .clip_width
				: -1;
		int16_t secondary_fade =
			secondary_level != 0
				? g_hud_element_layouts
					  [g_hud_instrument_set_base_index + 38]
						  .clip_width
				: -1;
		hud_draw_cached_faded_sprite_element(
			(uint16_t)(g_hud_instrument_set_base_index + 37),
			(int16_t)g_hud_shield_colors[strength_level],
			primary_fade);
		hud_draw_cached_faded_sprite_element(
			(uint16_t)(g_hud_instrument_set_base_index + 38),
			(int16_t)g_hud_shield_colors[secondary_level],
			secondary_fade);
	} else {
		uint16_t secondary_level;
		uint16_t strength_level;
		uint16_t shield_ratio_q16;
		if (max_shield <= shield) {
			shield_ratio_q16 = math2_longratio_q16(
				(unsigned int)(shield - max_shield),
				(unsigned int)max_shield);
			strength_level = 9;
			secondary_level =
				(uint16_t)(shield_ratio_q16 / 0x28Fu + 100);
		} else {
			shield_ratio_q16 = math2_longratio_q16(
				(unsigned int)shield, (unsigned int)max_shield);
			strength_level = math2_longfraction(
				9, (uint16_t)shield_ratio_q16);
			secondary_level = (uint16_t)(shield_ratio_q16 / 0x28Fu);
		}
		if ((uint16_t)g_hud_element_state_cache
			    [g_hud_instrument_set_base_index + 37] !=
		    secondary_level) {
			flight_text_set_font_tier(2);
			flight_text_set_clip_rect(
				g_hud_element_layouts
					[g_hud_instrument_set_base_index + 37]
						.x,
				g_hud_element_layouts
					[g_hud_instrument_set_base_index + 37]
						.y,
				g_hud_element_layouts
						[g_hud_instrument_set_base_index +
						 37]
							.x +
					flight_text_measure_string_width(
						"123%"),
				g_hud_element_layouts
						[g_hud_instrument_set_base_index +
						 37]
							.y +
					g_flight_font_line_height);
			flight_text_set_background_color(0x40);
			g_flight_fill_clip_rect_fn();
			flight_text_set_color(
				g_hud_shield_colors
					[HUD_SHIELD_TEXT_COLOR_OFFSET +
					 strength_level]);
			flight_text_set_cursor(
				g_hud_element_layouts
					[g_hud_instrument_set_base_index + 37]
						.x,
				g_hud_element_layouts
					[g_hud_instrument_set_base_index + 37]
						.y);
			flight_text_format_scratch_int(secondary_level);
			flight_text_append_scratch_char('%');
#ifdef XVT_MODERN
			xvt_cockpit_text_record_field(
				XVT_COCKPIT_TEXT_SHIELD_AFT,
				g_flight_text_scratch_buffer,
				XVT_COCKPIT_ALIGN_RIGHT);
#endif
			flight_text_draw_string_right_aligned(
				g_flight_text_scratch_buffer);
			g_hud_element_state_cache
				[g_hud_instrument_set_base_index + 37] =
					(int16_t)secondary_level;
		}
	}

	{
		uint16_t hull_state;
		if (g_player_flight_transient_timers[g_local_player]
			    .hull_hit_flash_timer != 0) {
			hull_state = 3;
		} else {
			craft = g_object_table[g_players[g_local_player]
						       .object_index]
					.mobj->p_craft;
			unsigned int hull_third = craft->hull_max / 3;
			if (!hull_third) {
				hull_state = 2;
			} else {
				hull_state = craft->hull_damage / hull_third;
				if ((uint16_t)hull_state > 2) {
					hull_state = 2;
				}
				hull_state = 2 - hull_state;
			}
		}
		hud_draw_cached_sprite_element(
			g_hud_instrument_set_base_index + 39, hull_state);
	}
}

/* Draws the local player's beam when HUD feature 0x10 is on: the beam-active
 * sprite (element 116 of the current set), then, when beam_charge (0 while the
 * beam system is out) differs from g_hud_element_state_cache[51], the nine
 * segments of layout 51 of the current set, placed by resolution. Each segment
 * holds 1,000 of charge: full ones in the fourth color of
 * g_hud_beam_segment_color_by_charge_step, the partly charged one by thirds of 1,000,
 * empty ones in the first color; a segment in the first color is drawn without
 * fading. */
// FUNCTION: XVT 0x43DE10
void hud_draw_beam_strength2d(void)
{
	uint16_t layout_index = g_hud_instrument_set_base_index + 51;
	struct craft_data *craft =
		g_object_table[g_players[g_local_player].object_index]
			.mobj->p_craft;
	if ((craft->damage_stats.active_hud_feature_mask & 0x10) == 0) {
		return;
	}

	int16_t beam_strength = craft->beam_charge;
	if (beam_strength < 0) {
		beam_strength = 0;
	}
	int16_t beam_system_available =
		craft->working_subsystems & CRAFT_SUBSYSTEM_FLAG_BEAM_SYSTEM;
	if (beam_system_available == 0) {
		beam_strength = 0;
	}
	uint16_t beam_active_state = craft->beam_active != 0;
	if (beam_system_available == 0) {
		beam_active_state = 0;
	}
	hud_draw_cached_sprite_element(g_hud_instrument_set_base_index + 116,
				       beam_active_state);

	int original_beam_strength = beam_strength;
	if ((uint16_t)g_hud_element_state_cache[51] == beam_strength) {
		return;
	}
	g_hud_element_state_cache[51] = beam_strength;

	int16_t segment_index = 0;
	uint16_t segment_color;
	do {
		if (200 * (5 * segment_index + 5) < original_beam_strength) {
			segment_color =
				g_hud_beam_segment_color_by_charge_step[3];
		} else {
			int16_t clamped_strength = beam_strength;
			if (beam_strength < 0) {
				segment_color =
					g_hud_beam_segment_color_by_charge_step
						[0];
			} else {
				if (beam_strength > 1000) {
					clamped_strength = 1000;
				}
				segment_color =
					g_hud_beam_segment_color_by_charge_step
						[clamped_strength / 333];
			}
		}

		uint16_t x = g_hud_element_layouts[layout_index].x;
		uint16_t y = g_hud_element_layouts[layout_index].y;
		if (g_flight_resolution_mode == FLIGHT_RESOLUTION_640X480) {
			x += 3 * (8 - segment_index);
			y += 3 * (8 - segment_index);
		} else if (g_flight_resolution_mode ==
			   FLIGHT_RESOLUTION_480X360) {
			x += g_hud_beam_segment_offsets480x360[segment_index].x;
			y += g_hud_beam_segment_offsets480x360[segment_index].y;
		} else {
			x += g_hud_beam_segment_offsets320x240[segment_index].x;
			y += g_hud_beam_segment_offsets320x240[segment_index].y;
		}

		beam_strength -= 1000;
		if (g_hud_panel_sprite_data_by_index
			    [g_hud_element_layouts
				     [g_hud_instrument_set_base_index + 51]
					     .selector +
			     segment_index] == NULL) {
			XVT_LOG_ERROR(
				"hud.sprite_missing element=%d sprite=%u",
				(int)(g_hud_instrument_set_base_index + 51),
				(unsigned)(g_hud_element_layouts
						   [g_hud_instrument_set_base_index +
						    51]
							   .selector +
					   segment_index));
		}
		g_flight_blit_sprite_faded_fn(
			g_hud_panel_sprite_data_by_index
				[g_hud_element_layouts
					 [g_hud_instrument_set_base_index + 51]
						 .selector +
				 segment_index],
			x, y,
			g_hud_element_layouts[g_hud_instrument_set_base_index +
					      51]
				.color_index_or_widget_param,
			(int8_t)segment_color,
			g_hud_beam_segment_color_by_charge_step[0] ==
					segment_color
				? 0
				: g_hud_element_layouts
					  [g_hud_instrument_set_base_index + 51]
						  .clip_width);
		++segment_index;
	} while (segment_index < 9);
}

/* Draws the local player's speed in element 40 of the current set when HUD
 * feature 0x40 is on: speed * 0x71C7 / 65,536, rounded. */
// FUNCTION: XVT 0x43E050
void hud_update_speed_percent(void)
{
	if ((g_object_table[g_players[g_local_player].object_index]
		     .mobj->p_craft->damage_stats.active_hud_feature_mask &
	     0x40) != 0) {
		flight_text_set_background_color(0x40);
		int16_t speed_percent = math2_fraction(
			g_object_table[g_players[g_local_player].object_index]
				.mobj->speed,
			0x71C7);
		if (g_hud_instrument_set_base_index !=
		    HUD_COCKPIT_INSTRUMENT_BASE_INDEX) {
			flight_text_set_font_tier(0);
		} else {
			flight_text_set_font_tier(2);
		}
		hud_draw_cached_numeric_element(
			g_hud_instrument_set_base_index + 40, speed_percent, 1);
	}
}

/* Draws the local player's throttle in element 41 of the current set when HUD
 * feature 0x40 is on: throttle_speed / 655, doubled while engine_overdrive_off is
 * 0. */
// FUNCTION: XVT 0x43E110
void hud_update_throttle_percent(void)
{
	if ((g_object_table[g_players[g_local_player].object_index]
		     .mobj->p_craft->damage_stats.active_hud_feature_mask &
	     0x40) != 0) {
		flight_text_set_background_color(0x40);
		struct mobile_object *mobile_object =
			g_object_table[g_players[g_local_player].object_index]
				.mobj;
		struct craft_data *craft = mobile_object->p_craft;
		int16_t throttle_percent = craft->throttle_speed / 0x28F;
		if (craft->engine_overdrive_off == 0) {
			throttle_percent *= 2;
		}
		if (g_hud_instrument_set_base_index !=
		    HUD_COCKPIT_INSTRUMENT_BASE_INDEX) {
			flight_text_set_font_tier(0);
		} else {
			flight_text_set_font_tier(2);
		}
		hud_draw_cached_numeric_element(
			g_hud_instrument_set_base_index + 41, throttle_percent,
			1);
	}
}

/* Draws the mission clock at layout 46 when its whole seconds differ from
 * g_hud_element_state_cache[46]: the countdown clock in the proving grounds or
 * with a time limit, minutes zero-padded, else the elapsed clock, minutes
 * space-padded; seconds always two digits. Leaves g_flight_text_shadow_enabled at
 * 0 when it draws. */
// FUNCTION: XVT 0x43E1F0
void hud_update_mission_clock_display(void)
{
	uint8_t seconds;
	int16_t minute_seconds;

	if (g_flight_mission_state.proving_grounds_mode_active != 0 ||
	    g_flight_mission_state.mission_time_limit_minutes != 0) {
		minute_seconds = 60 * g_mission_countdown_clock.minutes;
		seconds = g_mission_countdown_clock.seconds;
	} else {
		minute_seconds = 60 * g_mission_elapsed_clock.minutes;
		seconds = g_mission_elapsed_clock.seconds;
	}
	int16_t total_seconds = minute_seconds + seconds;
	if (g_hud_element_state_cache[46] != total_seconds) {
		g_hud_element_state_cache[46] = total_seconds;
		flight_text_set_font_tier(2);
		flight_text_set_clip_rect(0, 0, g_screen_width,
					  g_screen_height);
		flight_text_set_background_color(0x40);
		if (g_flight_resolution_mode == FLIGHT_RESOLUTION_320X240) {
			flight_text_set_color(0x4D);
		} else {
			flight_text_set_color(0x4E);
		}
		g_flight_text_shadow_enabled = 0;
		flight_text_set_cursor(g_hud_element_layouts[46].x,
				       g_hud_element_layouts[46].y);

		if (g_flight_mission_state.proving_grounds_mode_active != 0 ||
		    g_flight_mission_state.mission_time_limit_minutes != 0) {
#ifdef XVT_MODERN
			xvt_cockpit_readouts_record_number(
				XVT_COCKPIT_NUMBER_CLOCK_MINUTES,
				g_mission_countdown_clock.minutes, 2, 2);
#endif
			flight_text_draw_decimal_number(
				g_mission_countdown_clock.minutes, 2, 2);
		} else {
#ifdef XVT_MODERN
			xvt_cockpit_readouts_record_number(
				XVT_COCKPIT_NUMBER_CLOCK_MINUTES,
				g_mission_elapsed_clock.minutes, 2, 1);
#endif
			flight_text_draw_decimal_number(
				g_mission_elapsed_clock.minutes, 2, 1);
		}

		if (g_flight_resolution_mode == FLIGHT_RESOLUTION_320X240) {
			flight_text_set_cursor(
				g_hud_element_layouts[46].x +
					flight_text_measure_string_width(
						g_mission_clock_minutes_width_text),
				g_hud_element_layouts[46].y);
		} else {
			flight_text_set_cursor(
				g_hud_element_layouts[46].x +
					flight_text_measure_string_width(
						g_mission_clock_minutes_width_text) +
					1,
				g_hud_element_layouts[46].y);
		}

		if (g_flight_mission_state.proving_grounds_mode_active != 0 ||
		    g_flight_mission_state.mission_time_limit_minutes != 0) {
#ifdef XVT_MODERN
			xvt_cockpit_readouts_record_number(
				XVT_COCKPIT_NUMBER_CLOCK_SECONDS,
				g_mission_countdown_clock.seconds, 2, 2);
#endif
			flight_text_draw_decimal_number(
				g_mission_countdown_clock.seconds, 2, 2);
		} else {
#ifdef XVT_MODERN
			xvt_cockpit_readouts_record_number(
				XVT_COCKPIT_NUMBER_CLOCK_SECONDS,
				g_mission_elapsed_clock.seconds, 2, 2);
#endif
			flight_text_draw_decimal_number(
				g_mission_elapsed_clock.seconds, 2, 2);
		}
	}
}

/* Draws the local player's power bars. An X-wing, Y-wing, A-wing, Z-95 or
 * B-wing shows engine, shield and laser levels (engine 8 minus the other two)
 * as 4-segment bars, doubled outside the cockpit set, the engine bar with twice
 * as many segments; other craft show laser, shield and beam levels times 3 and
 * the engine level (8 minus the laser level, minus each of the shield and beam
 * levels less 2 when fitted) as 12-segment bars. Each bar needs its HUD
 * feature: laser 0x200, engine 0x400, shields 0x800, beam 0x1000. */
// FUNCTION: XVT 0x43E390
void hud_draw_power_settings2d(void)
{
	int object_index = g_players[g_local_player].object_index;
	struct object_record *object = &g_object_table[object_index];
	struct craft_data *craft = object->mobj->p_craft;
	int uses_compact_power_display = 0;
	if (object_index != -1) {
		if (object->object_type == 1 || object->object_type == 2 ||
		    object->object_type == 3 || object->object_type == 14 ||
		    object->object_type == 4) {
			uses_compact_power_display = 1;
		}
	}

	int16_t y_step;
	uint16_t engine_power;
	if (!uses_compact_power_display) {
		y_step = 2;
		if (g_flight_resolution_mode != FLIGHT_RESOLUTION_320X240) {
			y_step = g_flight_resolution_mode ==
						 FLIGHT_RESOLUTION_480X360
					 ? 4
					 : 6;
		}
		if ((craft->damage_stats.active_hud_feature_mask & 0x200) !=
		    0) {
			hud_draw_cached_segmented_bar(
				3 * (uint8_t)craft->laser_recharge_level,
				g_hud_instrument_set_base_index + 43, 12,
				y_step);
		}

		craft = g_object_table[g_players[g_local_player].object_index]
				.mobj->p_craft;
		if ((craft->damage_stats.active_hud_feature_mask & 0x800) !=
			    0 &&
		    (craft->system_flags & CRAFT_SUBSYSTEM_FLAG_SHIELDS) != 0) {
			hud_draw_cached_segmented_bar(
				3 * (uint8_t)craft->shield_recharge_level,
				g_hud_instrument_set_base_index + 44, 12,
				y_step);
		}

		craft = g_object_table[g_players[g_local_player].object_index]
				.mobj->p_craft;
		if ((craft->damage_stats.active_hud_feature_mask & 0x1000) !=
			    0 &&
		    (craft->system_flags & CRAFT_SUBSYSTEM_FLAG_BEAM_SYSTEM) !=
			    0) {
			hud_draw_cached_segmented_bar(
				3 * (uint8_t)craft->beam_recharge_level,
				g_hud_instrument_set_base_index + 45, 12,
				y_step);
		}

		craft = g_object_table[g_players[g_local_player].object_index]
				.mobj->p_craft;
		if ((craft->damage_stats.active_hud_feature_mask & 0x400) !=
		    0) {
			engine_power =
				(uint16_t)(8 - (uint8_t)craft
						       ->laser_recharge_level);
			uint16_t system_flags = craft->system_flags;
			if ((system_flags & CRAFT_SUBSYSTEM_FLAG_SHIELDS) !=
			    0) {
				engine_power =
					(uint16_t)(engine_power -
						   (uint8_t)craft
							   ->shield_recharge_level +
						   2);
			}
			if ((system_flags & CRAFT_SUBSYSTEM_FLAG_BEAM_SYSTEM) !=
			    0) {
				engine_power =
					(uint16_t)(engine_power -
						   (uint8_t)craft
							   ->beam_recharge_level +
						   2);
			}
			hud_draw_cached_segmented_bar(
				engine_power,
				g_hud_instrument_set_base_index + 42, 12,
				y_step);
		}
	} else {
		uint16_t active_hud_feature_mask =
			craft->damage_stats.active_hud_feature_mask;
		if ((active_hud_feature_mask & 0xE00) == 0) {
			return;
		}

		uint16_t segment_count = 0;
		y_step = 0;
		switch (g_flight_resolution_mode) {
		case FLIGHT_RESOLUTION_320X240:
			y_step = 2;
			segment_count = 4;
			break;
		case FLIGHT_RESOLUTION_640X480:
			y_step = 5;
			segment_count = 4;
			break;
		case FLIGHT_RESOLUTION_480X360:
			y_step = 3;
			segment_count = 4;
			break;
		default:
			break;
		}

		uint16_t laser_power = (uint8_t)craft->laser_recharge_level;
		uint16_t shield_power = (uint8_t)craft->shield_recharge_level;
		engine_power = (uint16_t)(8 - shield_power - laser_power);
		if (g_hud_instrument_set_base_index !=
		    HUD_COCKPIT_INSTRUMENT_BASE_INDEX) {
			segment_count *= 2;
			laser_power *= 2;
			shield_power *= 2;
			engine_power *= 2;
		}
		if ((active_hud_feature_mask & 0x400) != 0) {
			hud_draw_cached_segmented_bar(
				engine_power,
				g_hud_instrument_set_base_index + 42,
				2 * segment_count, y_step);
		}
		if ((active_hud_feature_mask & 0x800) != 0) {
			hud_draw_cached_segmented_bar(
				shield_power,
				g_hud_instrument_set_base_index + 44,
				segment_count, 2 * y_step);
		}
		if ((active_hud_feature_mask & 0x200) != 0) {
			hud_draw_cached_segmented_bar(
				laser_power,
				g_hud_instrument_set_base_index + 43,
				segment_count, 2 * y_step);
		}
	}
}

/* Draws a bar of segmentCount sprites from element_idx's layout upward, y_step
 * pixels apart, the first filled_count from sprite selector + 1 and the rest
 * from sprite selector, when filled_count differs from the element's entry in
 * g_hud_element_state_cache, which it then records. */
// FUNCTION: XVT 0x43E6F0
void hud_draw_cached_segmented_bar(uint16_t filled_count, uint16_t element_idx,
				   uint16_t segment_count, int16_t y_step)
{
	if ((uint16_t)g_hud_element_state_cache[element_idx] == filled_count) {
		return;
	}

	uint16_t segment_index = 0;
	g_hud_element_state_cache[element_idx] = (int16_t)filled_count;
	uint16_t x = g_hud_element_layouts[element_idx].x;
	uint16_t y = g_hud_element_layouts[element_idx].y;
	uint16_t selector = g_hud_element_layouts[element_idx].selector;
	if (segment_count == segment_index) {
		return;
	}

	do {
		uint16_t sprite_offset = segment_index < filled_count;
		if (g_hud_panel_sprite_data_by_index[selector +
						     sprite_offset] == NULL) {
			XVT_LOG_ERROR("hud.sprite_missing element=%d sprite=%u",
				      (int)element_idx,
				      (unsigned)(selector + sprite_offset));
		}
		g_flight_blit_sprite_fn(
			g_hud_panel_sprite_data_by_index[selector +
							 sprite_offset],
			x, y, 253, 0);
		y = (uint16_t)(y - y_step);
		++segment_index;
	} while (segment_index < segment_count);
}

/* Draws the threat indicators of the current set from the active craft around
 * the local player: attack (element 90) when an AI craft attacks it with linked
 * laser cannons (projectile types 0x89 or 0x8B), or a player's craft hit it
 * within the last 5 mission seconds or aims lasers at it from rough distance
 * under 0x10000; turret lasers (91) when a craft's working turret targets it;
 * beam (92), the beam type of a charged beam on it. Then the incoming warhead
 * warning (93): 2 when any lock on it exceeds 944 warhead_lock_ticks, blinking
 * while any lock is building, else 0, and the missile warning sound to match.
 * hud_mode is ignored. */
// FUNCTION: XVT 0x43E790
void hud_update_threat_indicators(int hud_mode)
{
	(void)hud_mode;

	uint16_t attack_threat = 0;
	uint16_t laser_threat = 0;
	uint16_t beam_threat = 0;
	int player_object_idx = g_players[g_local_player].object_index;
	int object_idx;
	for (object_idx = g_active_region_object_slot_start;
	     object_idx < g_active_region_craft_object_slot_end; ++object_idx) {
		if (g_object_table[object_idx].object_type == 0 ||
		    g_object_table[object_idx].mobj->family != 0) {
			continue;
		}
		struct craft_data *craft =
			g_object_table[object_idx].mobj->p_craft;
		if (craft->working_subsystems == 0 ||
		    craft->object_kind != CRAFT_OBJECT_KIND_ACTIVE) {
			continue;
		}
		if (g_object_table[object_idx].player_owner_idx == -1) {
			struct ai_controller *ai = &craft->ai_controller;

			if (ai->target_obj_idx == player_object_idx &&
			    (ai->maneuver_mode == AI_MANEUVER_MODE_ATTACK ||
			     ai->maneuver_mode ==
				     AI_MANEUVER_MODE_ROCKET_ATTACK)) {
				for (int cannon = 0;
				     cannon < craft->cannon_group_count;
				     ++cannon) {
					if ((craft->laser_state
							     .projectile_type_id
								     [cannon] ==
						     0x8B ||
					     craft->laser_state
							     .projectile_type_id
								     [cannon] ==
						     0x89) &&
					    craft->laser_state.link_mode
							    [cannon] != 0) {
						attack_threat = 1;
					}
				}
				if (craft->beam_active != 0 &&
				    craft->beam_charge != 0 &&
				    craft->beam_type_id != BEAM_TYPE_NONE &&
				    (uint8_t)craft->beam_type_id <
					    BEAM_TYPE_DECOY) {
					beam_threat =
						(uint8_t)craft->beam_type_id;
				}
			}
		} else {
			struct craft_data *player_craft =
				g_object_table[player_object_idx].mobj->p_craft;

			if (player_craft->last_attacker_obj_idx == object_idx &&
			    (uint16_t)mission_clock_to_seconds(
				    g_mission_elapsed_clock.hours,
				    g_mission_elapsed_clock.minutes,
				    g_mission_elapsed_clock.seconds) -
					    player_craft
						    ->last_hit_mission_second <
				    5) {
				attack_threat = 1;
			}
			int player_owner_idx =
				g_object_table[object_idx].player_owner_idx;
			if ((uint16_t)g_players[player_owner_idx]
					    .current_target_object_idx ==
				    player_object_idx &&
			    g_players[player_owner_idx].selected_weapon_mode ==
				    0) {
				pai_object_ref_update_rough_distance(
					object_idx, player_object_idx);
				if (g_last_rough_distance < 0x10000 &&
				    targeting_test_aim_cone(player_object_idx,
							    0,
							    player_owner_idx)) {
					attack_threat = 1;
				}
			}
			if ((uint16_t)g_players[player_owner_idx]
					    .current_target_object_idx ==
				    player_object_idx &&
			    craft->beam_active != 0 &&
			    craft->beam_charge != 0 &&
			    craft->beam_type_id != BEAM_TYPE_NONE &&
			    (uint8_t)craft->beam_type_id < BEAM_TYPE_DECOY) {
				beam_threat = (uint8_t)craft->beam_type_id;
			}
		}
		if (g_mission_flight_groups[g_object_table[object_idx]
						    .flight_group_idx]
			    .fg.status1 != 5) {
			for (int slot = 0; slot < craft->laser_slot_count;
			     ++slot) {
				if (craft->weapon_slots[slot]
						    .projectile_type_id == 2 &&
				    craft->turret_target_states[slot]
						    .target_obj_idx ==
					    player_object_idx &&
				    craft->component_hp
						    [g_model_defs[craft->model_index]
							     .weapon_hardpoints
								     [slot]
							     .mesh_idx] != 0) {
					laser_threat = 1;
				}
			}
			if (craft->beam_active != 0 &&
			    craft->beam_charge != 0 &&
			    (uint16_t)craft->beam_target_obj_idx ==
				    player_object_idx &&
			    craft->beam_type_id != BEAM_TYPE_NONE &&
			    (uint8_t)craft->beam_type_id < BEAM_TYPE_DECOY) {
				beam_threat = (uint8_t)craft->beam_type_id;
			}
		}
	}
	if ((uint16_t)g_hud_element_state_cache
			    [g_hud_instrument_set_base_index + 90] !=
		    attack_threat ||
	    (uint16_t)g_hud_element_state_cache
			    [g_hud_instrument_set_base_index + 91] !=
		    laser_threat ||
	    (uint16_t)g_hud_element_state_cache
			    [g_hud_instrument_set_base_index + 92] !=
		    beam_threat) {
		XVT_LOG_DEBUG(
			"hud.threat_changed slot=%d attack=%u laser=%u beam=%u",
			g_local_player, (unsigned)attack_threat,
			(unsigned)laser_threat, (unsigned)beam_threat);
	}
	hud_draw_cached_sprite_element(g_hud_instrument_set_base_index + 90,
				       attack_threat);
	hud_draw_cached_sprite_element(g_hud_instrument_set_base_index + 91,
				       laser_threat);
	hud_draw_cached_sprite_element(g_hud_instrument_set_base_index + 92,
				       beam_threat);
	int16_t max_warhead_lock = 0;
	for (object_idx = g_active_region_object_slot_start;
	     object_idx < g_active_region_craft_object_slot_end; ++object_idx) {
		if (g_object_table[object_idx].object_type == 0 ||
		    g_object_table[object_idx].mobj->family != 0) {
			continue;
		}
		struct craft_data *craft =
			g_object_table[object_idx].mobj->p_craft;
		if (craft->working_subsystems == 0 ||
		    craft->object_kind != CRAFT_OBJECT_KIND_ACTIVE) {
			continue;
		}
		int player_owner_idx =
			g_object_table[object_idx].player_owner_idx;
		if (player_owner_idx == -1) {
			struct ai_controller *ai = &craft->ai_controller;

			if (ai->target_obj_idx == player_object_idx &&
			    ai->maneuver_mode ==
				    AI_MANEUVER_MODE_ROCKET_ATTACK) {
				int16_t warhead_lock_ticks =
					craft->warhead_lock_ticks;

				if (max_warhead_lock < warhead_lock_ticks) {
					max_warhead_lock = warhead_lock_ticks;
				}
			}
		} else if ((uint16_t)g_players[player_owner_idx]
					   .current_target_object_idx ==
				   player_object_idx &&
			   g_players[player_owner_idx].selected_weapon_mode !=
				   0) {
			int16_t warhead_lock_ticks = craft->warhead_lock_ticks;

			if (max_warhead_lock < warhead_lock_ticks) {
				max_warhead_lock = warhead_lock_ticks;
			}
		}
	}
	uint16_t warning_state;
	if (max_warhead_lock > 944) {
		warning_state = 2;
	} else if (max_warhead_lock > 0) {
		warning_state =
			(uint16_t)((g_mission_elapsed_clock.subsecond_ticks /
				    59) &
				   1);
	} else {
		warning_state = 0;
	}
	if ((max_warhead_lock > 944) !=
	    ((uint16_t)
		     g_hud_element_state_cache[g_hud_instrument_set_base_index +
					       93] == 2)) {
		XVT_LOG_DEBUG("hud.warhead_warning slot=%d warning=%u lock=%d",
			      g_local_player, (unsigned)warning_state,
			      (int)max_warhead_lock);
	}
	hud_draw_cached_sprite_element(g_hud_instrument_set_base_index + 93,
				       warning_state);
#ifdef XVT_MODERN
	xvt_cockpit_instruments_record_threats(attack_threat, laser_threat,
					       beam_threat, warning_state);
#endif
	flight_surface_unlock();
	fsfx_update_incoming_missile_warning(warning_state);
	flight_surface_lock();
}

/* Runs the critical warning of an X-wing, Y-wing, A-wing, Z-95 or B-wing whose
 * layout 50 is placed. With shields under 100 in total and hull_damage in the
 * last third of hull_max, the warning blinks; on each lit call it plays
 * FLIGHT_SOUND_CRITICAL_WARNING and steps layout 127's
 * clip_height_or_foreground_color, used here as a counter that wraps back to 1 at
 * 0x2F0. In the forward view it draws sprite 50 and, unless the counter is 0,
 * EJECT centered in layout 127 in the layout's colors; once the counter passes
 * 0x200 the box is cleared first and the text moves on, every 16 steps, through
 * the 14 strings that follow EJECT in g_str_cockpit_overlay_text. */
// FUNCTION: XVT 0x43EC40
void hud_update_critical_hull_shield_warning(void)
{
	int object_idx = g_players[g_local_player].object_index;
	int supported_craft = object_idx != -1 &&
			      (g_object_table[object_idx].object_type == 1 ||
			       g_object_table[object_idx].object_type == 2 ||
			       g_object_table[object_idx].object_type == 3 ||
			       g_object_table[object_idx].object_type == 14 ||
			       g_object_table[object_idx].object_type == 4);
	if (supported_craft == 0 ||
	    (uint16_t)g_hud_element_layouts[50].y +
			    (uint16_t)g_hud_element_layouts[50].x ==
		    0) {
		return;
	}

	struct craft_data *craft = g_object_table[object_idx].mobj->p_craft;
	unsigned int shield_energy =
		craft->shield_energy[0] + craft->shield_energy[1];
	unsigned int hull_third = craft->hull_max / 3;
	unsigned int hull_damage_level = 2;
	if (hull_third != 0) {
		hull_damage_level = craft->hull_damage / hull_third;
	}
	uint16_t warning_state;
	if (shield_energy < 100 && hull_damage_level == 2) {
		warning_state =
			(g_mission_elapsed_clock.subsecond_ticks / 59) & 1;
		if (warning_state != 0) {
			flight_surface_unlock();
			fsfx_play_sound(FLIGHT_SOUND_CRITICAL_WARNING, -1,
					g_local_player);
			flight_surface_lock();
			++g_hud_element_layouts[127]
				  .clip_height_or_foreground_color;
			if ((uint16_t)g_hud_element_layouts[127]
				    .clip_height_or_foreground_color >= 0x2F0) {
				g_hud_element_layouts[127]
					.clip_height_or_foreground_color = 1;
			}
			if (g_hud_element_layouts[127]
				    .clip_height_or_foreground_color == 2) {
				XVT_LOG_DEBUG(
					"hud.critical_warning slot=%d shields=%u hull=%u max=%u",
					g_local_player, shield_energy,
					craft->hull_damage, craft->hull_max);
			}
		}
	} else {
		warning_state = 0;
	}

	if (g_players[g_local_player].view_state.hud_state_live !=
	    HUD_VIEW_FORWARD) {
		return;
	}
	hud_draw_cached_sprite_element(50, warning_state);
	if (g_hud_element_layouts[127].clip_height_or_foreground_color == 0) {
		return;
	}

	flight_text_set_font_tier(0);
	flight_text_set_clip_rect(
		g_hud_element_layouts[127].x, g_hud_element_layouts[127].y,
		g_hud_element_layouts[127].x +
			g_hud_element_layouts[127].clip_width,
		g_hud_element_layouts[127].y + g_flight_font_line_height);
	flight_text_set_cursor(g_hud_element_layouts[127].x,
			       g_hud_element_layouts[127].y);
	flight_text_set_color(
		warning_state +
		g_hud_element_layouts[127].color_index_or_widget_param);
	flight_text_set_background_color(
		(uint16_t)g_hud_element_layouts[127].selector +
		(warning_state == 0 ? 0 : 2));
	unsigned int warning_text_idx =
		(uint16_t)g_hud_element_layouts[127]
			.clip_height_or_foreground_color;
	if (warning_text_idx > 0x200) {
		warning_text_idx -= 0x200;
		g_flight_fill_clip_rect_fn();
		warning_text_idx >>= 4;
	} else {
		warning_text_idx = 0;
	}
#ifdef XVT_MODERN
	xvt_cockpit_text_record_field(
		XVT_COCKPIT_TEXT_CRITICAL_WARNING,
		g_str_cockpit_overlay_text[COCKPIT_OVERLAY_STR_EJECT +
					   warning_text_idx],
		XVT_COCKPIT_ALIGN_CENTER);
#endif
	flight_text_draw_string_centered(
		g_str_cockpit_overlay_text[COCKPIT_OVERLAY_STR_EJECT +
					   warning_text_idx]);
}

/* In the cockpit set, draws the local player's countermeasure count, three
 * digits at layout 48 when it changed, and the chaff sprite (element 47), 1
 * while chaff_active_seconds is not 0. */
// FUNCTION: XVT 0x43EE80
void hud_update_countermeasure_status(void)
{
	uint16_t countermeasure_count =
		g_object_table[g_players[g_local_player].object_index]
			.mobj->p_craft->cm_ammo_count;
	if (g_hud_instrument_set_base_index ==
	    HUD_COCKPIT_INSTRUMENT_BASE_INDEX) {
		if ((uint16_t)g_hud_element_state_cache
			    [g_hud_instrument_set_base_index + 48] !=
		    countermeasure_count) {
			uint16_t left =
				g_hud_element_layouts
					[g_hud_instrument_set_base_index + 48]
						.x;
			uint16_t y =
				g_hud_element_layouts
					[g_hud_instrument_set_base_index + 48]
						.y;
			if (left + y == 0) {
				return;
			}

			flight_text_set_font_tier(2);
			flight_text_set_clip_rect(
				left, y,
				left + flight_text_measure_string_width(
					       g_three_digit_width_text),
				y + g_flight_font_line_height);
			flight_text_set_cursor(left, y);
			flight_text_set_background_color(0x2C);
			g_flight_fill_clip_rect_fn();
			flight_text_set_color(0x4E);
#ifdef XVT_MODERN
			xvt_cockpit_readouts_record_number(
				XVT_COCKPIT_NUMBER_COUNTERMEASURES,
				countermeasure_count, 3, 1);
#endif
			flight_text_draw_decimal_number(countermeasure_count, 3,
							1);
			g_hud_element_state_cache
				[g_hud_instrument_set_base_index + 48] =
					countermeasure_count;
		}

		if ((uint8_t)g_object_table[g_players[g_local_player]
						    .object_index]
			    .mobj->p_craft->chaff_active_seconds != 0) {
			hud_draw_cached_sprite_element(47, 1);
		} else {
			hud_draw_cached_sprite_element(47, 0);
		}
	}
}

/* Outside the map view, in the forward and HUD-only views of a craft other than
 * an X-wing, Y-wing, A-wing, Z-95, B-wing or TIE fighter, draws elements 109
 * and 110 of the current set in state 0 when the craft has no beam system and
 * element 108 when it has no shields. */
// FUNCTION: XVT 0x43F010
void hud_clear_unavailable_craft_system_indicators(void)
{
	if (g_players[g_local_player].map_camera_state == 0) {
		int object_index = g_players[g_local_player].object_index;
		uint8_t object_type;
		int excluded_craft =
			object_index != -1 &&
			((object_type =
				  g_object_table[object_index].object_type) ==
				 CRAFT_SPECIES_X_WING ||
			 object_type == CRAFT_SPECIES_Y_WING ||
			 object_type == CRAFT_SPECIES_A_WING ||
			 object_type == CRAFT_SPECIES_Z_95_HEADHUNTER ||
			 object_type == CRAFT_SPECIES_B_WING);

		if (!excluded_craft) {
			struct object_record *object =
				&g_object_table[object_index];
			if (object->object_type != CRAFT_SPECIES_TIE_FIGHTER) {
				uint8_t hud_state =
					g_players[g_local_player]
						.view_state.hud_state_live;
				if (hud_state == 0 || hud_state == 19) {
					unsigned int instrument_base_index =
						g_hud_instrument_set_base_index;
					if ((object->mobj->p_craft
						     ->system_flags &
					     CRAFT_SUBSYSTEM_FLAG_BEAM_SYSTEM) ==
					    0) {
						hud_draw_cached_sprite_element(
							instrument_base_index +
								109,
							0);
						hud_draw_cached_sprite_element(
							instrument_base_index +
								110,
							0);
					}
					if ((g_object_table
						     [g_players[g_local_player]
							      .object_index]
							     .mobj->p_craft
							     ->system_flags &
					     CRAFT_SUBSYSTEM_FLAG_SHIELDS) ==
					    0) {
						hud_draw_cached_sprite_element(
							instrument_base_index +
								108,
							0);
					}
				}
			}
		}
	}
}

/* Draws the HUD feature status sprites for the 13 features the local player's
 * craft has installed, at element 69 plus the feature's bit: state 13 for a
 * feature that is out, else 0. In the forward view the elements are those of
 * the cockpit set, and an X-wing, Y-wing, A-wing, Z-95 or B-wing draws only the
 * features that are out, in state 0. In the HUD-only view features 1 to 3 are
 * skipped, and features 4 and 12 for those craft. Sets
 * g_hud_cached_target_object_idx to -1 in both views, so the targeting computer
 * redraws; other views draw nothing. */
// FUNCTION: XVT 0x43F140
void hud_update_craft_system_status_indicators(void)
{
	uint8_t hud_state = g_players[g_local_player].view_state.hud_state_live;
	uint16_t feature_index;
	uint16_t feature_mask;
	uint16_t indicator_state;
	int object_index;
	uint8_t object_type;
	int excluded_craft;
	if (hud_state == 0) {
		feature_mask = 1;
		for (feature_index = 0; feature_index < 13;
		     feature_mask *= 2, ++feature_index) {
			object_index = g_players[g_local_player].object_index;
			excluded_craft =
				object_index != -1 &&
				((object_type = g_object_table[object_index]
							.object_type) ==
					 CRAFT_SPECIES_X_WING ||
				 object_type == CRAFT_SPECIES_Y_WING ||
				 object_type == CRAFT_SPECIES_A_WING ||
				 object_type == CRAFT_SPECIES_Z_95_HEADHUNTER ||
				 object_type == CRAFT_SPECIES_B_WING);

			if (!excluded_craft) {
				indicator_state =
					(feature_mask &
					 g_object_table[object_index]
						 .mobj->p_craft->damage_stats
						 .active_hud_feature_mask) == 0
						? 13
						: 0;
			} else if ((feature_mask &
				    g_object_table[object_index]
					    .mobj->p_craft->damage_stats
					    .active_hud_feature_mask) != 0) {
				continue;
			} else {
				indicator_state = 0;
			}

			if ((feature_mask &
			     g_object_table[object_index]
				     .mobj->p_craft->damage_stats
				     .installed_hud_feature_mask) != 0) {
				hud_draw_cached_sprite_element(
					feature_index + 69, indicator_state);
			}
		}
		g_hud_cached_target_object_idx = -1;
		return;
	}

	if (hud_state != 19) {
		return;
	}

	feature_mask = 1;
	struct craft_data *craft;
	for (feature_index = 0; feature_index < 13;
	     feature_mask *= 2, ++feature_index) {
		switch (feature_mask) {
		case 2:
		case 4:
		case 8:
			continue;

		case 16:
		case 4096:
			object_index = g_players[g_local_player].object_index;
			excluded_craft =
				object_index != -1 &&
				((object_type = g_object_table[object_index]
							.object_type) ==
					 CRAFT_SPECIES_X_WING ||
				 object_type == CRAFT_SPECIES_Y_WING ||
				 object_type == CRAFT_SPECIES_A_WING ||
				 object_type == CRAFT_SPECIES_Z_95_HEADHUNTER ||
				 object_type == CRAFT_SPECIES_B_WING);
			if (excluded_craft) {
				continue;
			}
			break;

		default:
			break;
		}

		craft = g_object_table[g_players[g_local_player].object_index]
				.mobj->p_craft;
		indicator_state =
			(feature_mask &
			 craft->damage_stats.active_hud_feature_mask) == 0
				? 13
				: 0;
		if ((feature_mask &
		     craft->damage_stats.installed_hud_feature_mask) != 0) {
			uint16_t base_index = g_hud_instrument_set_base_index;
			hud_draw_cached_sprite_element(
				base_index + feature_index + 69,
				indicator_state);
		}
	}
	g_hud_cached_target_object_idx = -1;
}

/* Draws the target camera (CMD) view's text about the local player's target:
 * name, range, cargo, and for a craft its orders, the order's target or
 * destination, the range to it and the time to reach it. On a target change it
 * records the target in g_hud_cached_target_object_idx, marks the CMD entries of
 * g_hud_element_state_cache for redraw, draws the name, and clears the panel
 * (layout 143) for a target outside the craft slots. With a target it draws the
 * range label when there was none before, the range as whole.hundredths (polar
 * distance * 161 / 65,536, at most 99.99), and the cargo, unknown until the
 * team identifies the craft.
 *
 * For a craft target that is not an unidentified hostile in a melee with
 * several players: its plan's report as its orders (with disabled and stopped
 * craft shown as for the targeting computer), the order target (a player's own
 * target for a player's craft, none while disabled or waiting), the range to
 * it, and the time: the order range over 18 times the speed, or for a stopped
 * craft its maneuver_timer in simulated seconds under board2pln and waitpln,
 * else 00:00 at zero range or unknown. Each part is redrawn when it changes;
 * the order range only when its hundredths change. Stores label widths in the
 * clip_width of layouts 104 to 107, leaves trig2_polardistance multiplied by
 * 161, and leaves g_flight_text_shadow_enabled at 0. */
// FUNCTION: XVT 0x43F390
void hud_draw_cmd_target_details(void)
{
	enum {
		CMD_TARGET_NAME_ELEMENT = 94,
		CMD_CARGO_ELEMENT = 95,
		CMD_RANGE_ELEMENT = 96,
		CMD_RANGE_FRACTION_CACHE = 97,
		CMD_ORDERS_ELEMENT = 104,
		CMD_ORDER_TARGET_ELEMENT = 105,
		CMD_ORDER_RANGE_ELEMENT = 106,
		CMD_ORDER_TIME_ELEMENT = 107,
		CMD_RANGE_LABEL_ELEMENT = 141,
		CMD_PANEL_BOUNDS_ELEMENT = 143,
		NORMAL_TARGET_DISPLAY_FLAGS = 3,
		HIDDEN_TARGET_DISPLAY_FLAGS = 1,
		HUD_CACHE_DIRTY = -1,
		HUD_CACHE_INVALID = -2,
		DISTANCE_FIXED_SCALE = 161,
		DISTANCE_DECIMAL_SCALE = 100,
		MAX_DISPLAY_DISTANCE = 9999,
		DISTANCE_PER_SPEED_SECOND = 18,
		SECONDS_PER_MINUTE = 60,
		NO_AI_TARGET = 255,
	};

#ifdef XVT_MODERN
	xvt_cockpit_readouts_begin_target(1);
#endif

	g_flight_text_shadow_enabled = 0;
	flight_text_set_background_color(0x2C);
	flight_text_set_font_tier(2);
	uint16_t panel_left = g_hud_element_layouts[CMD_PANEL_BOUNDS_ELEMENT].x;
	uint16_t panel_right =
		panel_left +
		g_hud_element_layouts[CMD_PANEL_BOUNDS_ELEMENT].clip_width;
	int16_t previous_target_object_idx = g_hud_cached_target_object_idx;

	uint16_t current_target_object_idx;
	if (g_players[g_local_player].current_target_object_idx !=
	    g_hud_cached_target_object_idx) {
#ifdef XVT_MODERN
		xvt_cockpit_text_clear_target_fields();
#endif

		previous_target_object_idx = g_hud_cached_target_object_idx;
		g_hud_cached_target_object_idx =
			g_players[g_local_player].current_target_object_idx;
		uint16_t dirty_state = UINT16_MAX;
		uint16_t invalid_state = UINT16_MAX - 1;
		g_hud_element_state_cache[CMD_CARGO_ELEMENT] = dirty_state;
		g_hud_element_state_cache[CMD_RANGE_ELEMENT] = dirty_state;
		g_hud_element_state_cache[CMD_RANGE_FRACTION_CACHE] =
			dirty_state;
		g_hud_element_state_cache[CMD_ORDERS_ELEMENT] = dirty_state;
		g_hud_element_state_cache[CMD_ORDER_TARGET_ELEMENT] =
			invalid_state;
		g_hud_element_state_cache[CMD_ORDER_RANGE_ELEMENT] =
			invalid_state;
		g_hud_element_state_cache[CMD_ORDER_TIME_ELEMENT] = dirty_state;

		uint16_t left =
			g_hud_element_layouts[CMD_TARGET_NAME_ELEMENT].x;
		uint16_t top = g_hud_element_layouts[CMD_TARGET_NAME_ELEMENT].y;
		flight_text_set_clip_rect(
			left, top,
			left + g_hud_element_layouts[CMD_TARGET_NAME_ELEMENT]
					.clip_width,
			top + g_flight_font_line_height + 1);
		g_flight_fill_clip_rect_fn();
		flight_text_set_cursor(left, top);
		flight_text_set_clear_line_background(1);

		current_target_object_idx = (uint16_t)g_players[g_local_player]
						    .current_target_object_idx;
		if (current_target_object_idx != dirty_state) {
			struct object_record *target_object =
				&g_object_table[current_target_object_idx];
			struct mobile_object *target_mobile_object =
				target_object->mobj;
			struct craft_data *target_craft =
				target_mobile_object != NULL
					? target_mobile_object->p_craft
					: NULL;
			if (g_mission_header.mission_type ==
				    MISSION_TYPE_MELEE &&
			    g_flight_player_count > 1) {
				int16_t display_flags =
					NORMAL_TARGET_DISPLAY_FLAGS;
				if (target_mobile_object != NULL &&
				    target_craft != NULL &&
				    g_flight_mission_state
						    .locate_players_enabled ==
					    0) {
					int player_team =
						(uint16_t)g_players
							[g_local_player]
								.team;
					if (target_craft
						    ->identified_order_by_team
							    [player_team] ==
					    0) {
						int flight_group_idx =
							target_object
								->flight_group_idx;
						int team =
							g_mission_flight_groups
								[flight_group_idx]
									.fg
									.team;
						int hostile =
							team == player_team
								? 0
								: g_mission_teams[player_team]
										  .allies[team] ==
									  0;
						if (hostile == 1 &&
						    g_mission_flight_groups[flight_group_idx]
								    .fg
								    .player_number !=
							    0) {
							display_flags =
								HIDDEN_TARGET_DISPLAY_FLAGS;
						}
					}
				}
				hud_format_object_display_name(
					current_target_object_idx,
					display_flags);
			} else {
				hud_format_object_display_name(
					current_target_object_idx,
					NORMAL_TARGET_DISPLAY_FLAGS);
			}
#ifdef XVT_MODERN
			xvt_cockpit_text_record_field(
				XVT_COCKPIT_TEXT_TARGET_NAME,
				g_flight_text_scratch_buffer,
				XVT_COCKPIT_ALIGN_CENTER);
#endif
			flight_text_draw_string_centered(
				g_flight_text_scratch_buffer);
		}

		if ((uint16_t)g_hud_cached_target_object_idx >=
		    g_active_region_craft_object_slot_end) {
			flight_text_set_clip_rect(
				panel_left,
				g_hud_element_layouts[CMD_PANEL_BOUNDS_ELEMENT]
					.y,
				panel_right,
				g_hud_element_layouts[CMD_PANEL_BOUNDS_ELEMENT]
						.y +
					g_hud_element_layouts[CMD_PANEL_BOUNDS_ELEMENT]
						.clip_height_or_foreground_color);
			g_flight_fill_clip_rect_fn();
		}
	}

	if (g_players[g_local_player].current_target_object_idx != -1) {
		uint16_t left = g_hud_element_layouts[CMD_RANGE_ELEMENT].x;
		uint16_t top = g_hud_element_layouts[CMD_RANGE_ELEMENT].y;
		if (previous_target_object_idx == -1) {
			flight_text_set_cursor((uint16_t)g_hud_element_layouts
						       [CMD_RANGE_LABEL_ELEMENT]
							       .x,
					       (uint16_t)g_hud_element_layouts
						       [CMD_RANGE_LABEL_ELEMENT]
							       .y);
			flight_text_set_color(0x49);
			flight_text_draw_string(g_str_cmd_threat_display_text
							[CMD_THREAT_STR_DIST]);
		}
		player_compute_polar_to_object_ref(
			g_local_player,
			(uint16_t)g_hud_cached_target_object_idx);
		flight_text_set_clip_rect(
			left, top,
			left + flight_text_measure_string_width("00.00"),
			top + g_flight_font_line_height + 1);
		flight_text_set_color(0x4A);
		trig2_polardistance *= DISTANCE_FIXED_SCALE;
		uint16_t distance = (uint16_t)(trig2_polardistance >> 16);
		if (distance >= MAX_DISPLAY_DISTANCE + 1) {
			distance = MAX_DISPLAY_DISTANCE;
		}
		uint16_t whole_distance = distance / DISTANCE_DECIMAL_SCALE;
		uint16_t fractional_distance =
			distance - whole_distance * DISTANCE_DECIMAL_SCALE;
		if (whole_distance != (uint16_t)g_hud_element_state_cache
					      [CMD_RANGE_ELEMENT] ||
		    fractional_distance != (uint16_t)g_hud_element_state_cache
						   [CMD_RANGE_FRACTION_CACHE]) {
			g_hud_element_state_cache[CMD_RANGE_ELEMENT] =
				(int16_t)whole_distance;
			g_hud_element_state_cache[CMD_RANGE_FRACTION_CACHE] =
				(int16_t)fractional_distance;
			flight_text_set_cursor(left, top);
			g_flight_fill_clip_rect_fn();
			if (fractional_distance < 10) {
				sprintf(g_flight_text_scratch_buffer,
					"%ld.0%ld", (long)whole_distance,
					(long)fractional_distance);
			} else {
				sprintf(g_flight_text_scratch_buffer, "%ld.%ld",
					(long)whole_distance,
					(long)fractional_distance);
			}
#ifdef XVT_MODERN
			xvt_cockpit_text_record_field(
				XVT_COCKPIT_TEXT_CMD_RANGE,
				g_flight_text_scratch_buffer,
				XVT_COCKPIT_ALIGN_LEFT);
#endif
			flight_text_draw_string(g_flight_text_scratch_buffer);
		}

		int16_t cargo_state = 2;
		const char *cargo_text =
			g_str_cmd_threat_display_text[CMD_THREAT_STR_NO_CARGO];
		current_target_object_idx = (uint16_t)g_players[g_local_player]
						    .current_target_object_idx;
		if (current_target_object_idx <
		    g_active_region_craft_object_slot_end) {
			struct mobile_object *target_mobile_object =
				g_object_table[current_target_object_idx].mobj;
			if (target_mobile_object->family == 0) {
				struct craft_data *target_craft =
					target_mobile_object->p_craft;
				if (target_craft->identified_order_by_team
					    [(uint16_t)g_players[g_local_player]
						     .team] != 0) {
					cargo_state = 1;
					cargo_text =
						target_craft
							->special_cargo_name;
					if (cargo_text[0] == '\0') {
						cargo_state = 2;
						cargo_text = g_str_cmd_threat_display_text
							[CMD_THREAT_STR_NO_CARGO];
					}
				} else {
					cargo_state = 0;
					cargo_text = g_str_unknown;
				}
			}
		}
		if (cargo_state !=
		    g_hud_element_state_cache[CMD_CARGO_ELEMENT]) {
			g_hud_element_state_cache[CMD_CARGO_ELEMENT] =
				cargo_state;
			left = g_hud_element_layouts[CMD_CARGO_ELEMENT].x;
			top = g_hud_element_layouts[CMD_CARGO_ELEMENT].y;
			flight_text_set_clip_rect(
				left, top,
				left + g_hud_element_layouts[CMD_CARGO_ELEMENT]
						.clip_width,
				top + g_flight_font_line_height + 1);
			g_flight_fill_clip_rect_fn();
			flight_text_set_cursor(left, top);
			flight_text_set_color(0x46);
#ifdef XVT_MODERN
			xvt_cockpit_text_record_field(
				XVT_COCKPIT_TEXT_TARGET_CARGO, cargo_text,
				XVT_COCKPIT_ALIGN_LEFT);
#endif
			flight_text_draw_string(cargo_text);
		}
	}

	current_target_object_idx =
		(uint16_t)g_players[g_local_player].current_target_object_idx;
	if (current_target_object_idx < g_active_region_craft_object_slot_end &&
	    g_players[g_local_player].current_target_object_idx != -1) {
		struct mobile_object *target_mobile_object =
			g_object_table[current_target_object_idx].mobj;
		struct craft_data *target_craft = target_mobile_object->p_craft;
		struct ai_controller *controller = &target_craft->ai_controller;
		if (g_mission_header.mission_type == MISSION_TYPE_MELEE &&
		    g_flight_player_count > 1 && target_mobile_object != NULL &&
		    target_craft != NULL &&
		    g_flight_mission_state.locate_players_enabled == 0) {
			int player_team =
				(uint16_t)g_players[g_local_player].team;
			if (target_craft
				    ->identified_order_by_team[player_team] ==
			    0) {
				int team =
					g_mission_flight_groups
						[g_object_table
							 [current_target_object_idx]
								 .flight_group_idx]
							.fg.team;
				int hostile =
					team == player_team
						? 0
						: g_mission_teams[player_team]
								  .allies[team] ==
							  0;
				if (hostile == 1) {
					return;
				}
			}
		}

		int display_plan_id = controller->running_plan_id;
		if (target_craft->working_subsystems == 0) {
			display_plan_id =
				pai_find_plan_id_by_name_or_zero("disabledpln");
		} else if (target_mobile_object->speed == 0) {
			const char *plan_name =
				g_plan_table[display_plan_id].name;
			if (strcmp(plan_name, "flyhomepln") == 0 ||
			    strcmp(plan_name, "followhomepln") == 0 ||
			    strcmp(plan_name, "flyhomeevadepln") == 0 ||
			    strcmp(plan_name, "followhomeevadepln") == 0 ||
			    strcmp(plan_name, "enterhangarpln") == 0 ||
			    strcmp(plan_name, "exithangarpln") == 0 ||
			    strcmp(plan_name, "intohyperspacepln") == 0 ||
			    strcmp(plan_name, "outofhyperspacepln") == 0 ||
			    strcmp(plan_name, "starshipintohyperpln") == 0 ||
			    strcmp(plan_name, "starshipfollowhomepln") == 0) {
				display_plan_id =
					pai_find_plan_id_by_name_or_zero(
						"waitpln");
			}
		}

		if ((uint16_t)g_hud_element_state_cache[CMD_ORDERS_ELEMENT] !=
		    display_plan_id) {
			g_hud_element_state_cache[CMD_ORDERS_ELEMENT] =
				(int16_t)display_plan_id;
			uint16_t left =
				g_hud_element_layouts[CMD_ORDERS_ELEMENT].x;
			uint16_t top =
				g_hud_element_layouts[CMD_ORDERS_ELEMENT].y;
			flight_text_set_clip_rect(
				left, top, panel_right,
				top + g_flight_font_line_height + 1);
			g_flight_fill_clip_rect_fn();
			flight_text_set_cursor(left, top);
			if (g_flight_resolution_mode ==
			    FLIGHT_RESOLUTION_320X240) {
				flight_text_set_color(0x45);
			} else {
				flight_text_set_color(0x46);
			}
#ifdef XVT_MODERN
			xvt_cockpit_text_record_field(
				XVT_COCKPIT_TEXT_CMD_ORDERS_LABEL,
				g_str_cmd_threat_display_text
					[CMD_THREAT_STR_CURRENT_ORDERS],
				XVT_COCKPIT_ALIGN_LEFT);
#endif
			flight_text_draw_string(
				g_str_cmd_threat_display_text
					[CMD_THREAT_STR_CURRENT_ORDERS]);
			if (g_hud_element_layouts[CMD_ORDERS_ELEMENT]
				    .clip_width == 0) {
				g_hud_element_layouts[CMD_ORDERS_ELEMENT]
					.clip_width = flight_text_measure_string_width(
					g_str_cmd_threat_display_text
						[CMD_THREAT_STR_CURRENT_ORDERS]);
			}
			flight_text_set_cursor(
				(uint16_t)left + (uint16_t)g_hud_element_layouts
							 [CMD_ORDERS_ELEMENT]
								 .clip_width,
				g_flight_cursor_y);
			flight_text_set_color(0x4E);
#ifdef XVT_MODERN
			xvt_cockpit_text_record_field(
				XVT_COCKPIT_TEXT_CMD_ORDERS,
				g_str_in_flight_messages
					[g_plan_report_message_id_by_plan_id
						 [display_plan_id]],
				XVT_COCKPIT_ALIGN_LEFT);
#endif
			flight_text_draw_string(
				g_str_in_flight_messages
					[g_plan_report_message_id_by_plan_id
						 [display_plan_id]]);

			left = g_hud_element_layouts[CMD_ORDER_TIME_ELEMENT].x;
			top = g_hud_element_layouts[CMD_ORDER_TIME_ELEMENT].y;
			flight_text_set_clip_rect(
				left, top, panel_right,
				top + g_flight_font_line_height + 1);
			g_flight_fill_clip_rect_fn();
			flight_text_set_cursor(left, top);
			if (g_flight_resolution_mode ==
			    FLIGHT_RESOLUTION_320X240) {
				flight_text_set_color(0x45);
			} else {
				flight_text_set_color(0x46);
			}
			const char *time_label;
			if (g_object_table[(uint16_t)g_players[g_local_player]
						   .current_target_object_idx]
				    .mobj->speed == 0) {
				time_label = g_str_cmd_threat_display_text
					[CMD_THREAT_STR_TIME_REMAINING];
			} else if (controller->target_obj_idx < 0x8000) {
				time_label = g_str_cmd_threat_display_text
					[CMD_THREAT_STR_TIME_TO_TARGET];
			} else {
				time_label = g_str_cmd_threat_display_text
					[CMD_THREAT_STR_TIME_TO_DESTINATION];
			}
			flight_text_set_scratch(time_label);
			g_hud_element_layouts[CMD_ORDER_TIME_ELEMENT]
				.clip_width = flight_text_measure_string_width(
				g_flight_text_scratch_buffer);
#ifdef XVT_MODERN
			xvt_cockpit_readouts_clear_order_time();
			xvt_cockpit_text_clear_field(
				XVT_COCKPIT_TEXT_CMD_TIME_UNKNOWN);
			xvt_cockpit_text_record_field(
				XVT_COCKPIT_TEXT_CMD_TIME_LABEL,
				g_flight_text_scratch_buffer,
				XVT_COCKPIT_ALIGN_LEFT);
#endif
			flight_text_draw_string(g_flight_text_scratch_buffer);
		}

		{
			uint16_t order_target_object_idx;

			if (g_object_table[current_target_object_idx]
				    .player_owner_idx != -1) {
				order_target_object_idx =
					(uint16_t)g_players
						[g_object_table
							 [current_target_object_idx]
								 .player_owner_idx]
							.current_target_object_idx;
			} else {
				order_target_object_idx =
					controller->target_obj_idx;
			}
			if (target_craft->working_subsystems == 0) {
				order_target_object_idx = UINT16_MAX;
			}
			if (strcmp(g_plan_table[display_plan_id].name,
				   "waitpln") == 0) {
				order_target_object_idx = UINT16_MAX;
			}

			uint16_t value_left;
			uint16_t value_top;
			if (order_target_object_idx !=
			    (uint16_t)g_hud_element_state_cache
				    [CMD_ORDER_TARGET_ELEMENT]) {
				g_hud_element_state_cache
					[CMD_ORDER_TARGET_ELEMENT] = (int16_t)
						order_target_object_idx;
				if (g_flight_resolution_mode ==
				    FLIGHT_RESOLUTION_320X240) {
					flight_text_set_color(0x45);
				} else {
					flight_text_set_color(0x46);
				}
				uint16_t left =
					g_hud_element_layouts
						[CMD_ORDER_TARGET_ELEMENT]
							.x;
				uint16_t top =
					g_hud_element_layouts
						[CMD_ORDER_TARGET_ELEMENT]
							.y;
				flight_text_set_clip_rect(
					left, top, panel_right,
					top + g_flight_font_line_height);
				g_flight_fill_clip_rect_fn();
				flight_text_set_cursor(left, top);
				const char *target_label;
				if (order_target_object_idx < 0x8000) {
					target_label = g_str_cmd_threat_display_text
						[CMD_THREAT_STR_CURRENT_TARGET];
				} else {
					target_label = g_str_cmd_threat_display_text
						[CMD_THREAT_STR_CURRENT_DESTINATION];
				}
				flight_text_set_scratch(target_label);
#ifdef XVT_MODERN
				xvt_cockpit_text_record_field(
					XVT_COCKPIT_TEXT_CMD_TARGET_LABEL,
					g_flight_text_scratch_buffer,
					XVT_COCKPIT_ALIGN_LEFT);
#endif
				flight_text_draw_string(
					g_flight_text_scratch_buffer);
				g_hud_element_layouts[CMD_ORDER_TARGET_ELEMENT]
					.clip_width =
					flight_text_measure_string_width(
						g_flight_text_scratch_buffer);

				left = g_hud_element_layouts
					       [CMD_ORDER_RANGE_ELEMENT]
						       .x;
				top = g_hud_element_layouts
					      [CMD_ORDER_RANGE_ELEMENT]
						      .y;
				flight_text_set_clip_rect(
					left, top, panel_right,
					top + g_flight_font_line_height);
				g_flight_fill_clip_rect_fn();
				flight_text_set_cursor(left, top);
				const char *distance_label;
				if (order_target_object_idx < 0x8000) {
					distance_label = g_str_cmd_threat_display_text
						[CMD_THREAT_STR_DISTANCE_FROM_TARGET];
				} else {
					distance_label = g_str_cmd_threat_display_text
						[CMD_THREAT_STR_DISTANCE_TO_DESTINATION];
				}
				flight_text_set_scratch(distance_label);
#ifdef XVT_MODERN
				xvt_cockpit_readouts_clear_order_range();
				xvt_cockpit_text_record_field(
					XVT_COCKPIT_TEXT_CMD_RANGE_LABEL,
					g_flight_text_scratch_buffer,
					XVT_COCKPIT_ALIGN_LEFT);
#endif
				flight_text_draw_string(
					g_flight_text_scratch_buffer);
				g_hud_element_layouts[CMD_ORDER_RANGE_ELEMENT]
					.clip_width =
					flight_text_measure_string_width(
						g_flight_text_scratch_buffer);

				value_left = g_hud_element_layouts
						     [CMD_ORDER_TARGET_ELEMENT]
							     .x +
					     g_hud_element_layouts
						     [CMD_ORDER_TARGET_ELEMENT]
							     .clip_width;
				value_top = g_hud_element_layouts
						    [CMD_ORDER_TARGET_ELEMENT]
							    .y;
				flight_text_set_clip_rect(
					value_left, value_top, panel_right,
					value_top + g_flight_font_line_height +
						1);
				g_flight_fill_clip_rect_fn();
				flight_text_set_cursor(value_left, value_top);
				if (order_target_object_idx == NO_AI_TARGET ||
				    order_target_object_idx == UINT16_MAX) {
#ifdef XVT_MODERN
					xvt_cockpit_text_record_field(
						XVT_COCKPIT_TEXT_CMD_TARGET,
						g_str_cmd_threat_display_text
							[CMD_THREAT_STR_NONE],
						XVT_COCKPIT_ALIGN_LEFT);
#endif
					flight_text_draw_string(
						g_str_cmd_threat_display_text
							[CMD_THREAT_STR_NONE]);
				} else {
					hud_format_object_display_name(
						order_target_object_idx,
						NORMAL_TARGET_DISPLAY_FLAGS);
#ifdef XVT_MODERN
					xvt_cockpit_text_record_field(
						XVT_COCKPIT_TEXT_CMD_TARGET,
						g_flight_text_scratch_buffer,
						XVT_COCKPIT_ALIGN_LEFT);
#endif
					flight_text_draw_string(
						g_flight_text_scratch_buffer);
				}
			}

			unsigned int target_distance = 0;
			if (order_target_object_idx != UINT16_MAX) {
				if (g_object_table[current_target_object_idx]
					    .player_owner_idx != -1) {
					pai_object_ref_direction_to_object_ref(
						current_target_object_idx,
						order_target_object_idx);
				} else {
					trig2_ctop(
						controller->aim_point_x -
							g_object_table
								[current_target_object_idx]
									.world_x,
						controller->aim_point_y -
							g_object_table
								[current_target_object_idx]
									.world_y,
						controller->aim_point_z -
							g_object_table
								[current_target_object_idx]
									.world_z);
				}
				value_left = g_hud_element_layouts
						     [CMD_ORDER_RANGE_ELEMENT]
							     .x +
					     g_hud_element_layouts
						     [CMD_ORDER_RANGE_ELEMENT]
							     .clip_width;
				value_top = g_hud_element_layouts
						    [CMD_ORDER_RANGE_ELEMENT]
							    .y;
				target_distance =
					(unsigned int)trig2_polardistance;
				flight_text_set_clip_rect(
					value_left, value_top, panel_right,
					value_top + g_flight_font_line_height +
						1);
				flight_text_set_color(0x4A);
				trig2_polardistance *= DISTANCE_FIXED_SCALE;
				uint16_t distance =
					(uint16_t)(trig2_polardistance >> 16);
				if (distance >= MAX_DISPLAY_DISTANCE + 1) {
					distance = MAX_DISPLAY_DISTANCE;
				}
				uint16_t distance_whole =
					distance / DISTANCE_DECIMAL_SCALE;
				uint16_t distance_fraction =
					distance -
					distance_whole * DISTANCE_DECIMAL_SCALE;
				if (distance_fraction !=
				    (uint16_t)g_hud_element_state_cache
					    [CMD_ORDER_RANGE_ELEMENT]) {
					g_hud_element_state_cache
						[CMD_ORDER_RANGE_ELEMENT] =
							(int16_t)
								distance_fraction;
					flight_text_set_cursor(value_left,
							       value_top);
#ifdef XVT_MODERN
					xvt_cockpit_readouts_record_number(
						XVT_COCKPIT_NUMBER_ORDER_RANGE,
						distance_whole, 2, 1);
#endif
					flight_text_draw_decimal_number(
						distance_whole, 2, 1);
#ifdef XVT_MODERN
					xvt_cockpit_text_record_field(
						XVT_COCKPIT_TEXT_ORDER_RANGE_SEPARATOR,
						".", XVT_COCKPIT_ALIGN_LEFT);
#endif
					g_flight_draw_char_fn('.');
#ifdef XVT_MODERN
					xvt_cockpit_readouts_record_number(
						XVT_COCKPIT_NUMBER_ORDER_RANGE_FRACTION,
						distance_fraction, 2, 2);
#endif
					flight_text_draw_decimal_number(
						distance_fraction, 2, 2);
				}
			}

			value_left =
				g_hud_element_layouts[CMD_ORDER_TIME_ELEMENT]
					.x +
				g_hud_element_layouts[CMD_ORDER_TIME_ELEMENT]
					.clip_width;
			value_top =
				g_hud_element_layouts[CMD_ORDER_TIME_ELEMENT].y;
			flight_text_set_clip_rect(
				value_left, value_top, panel_right,
				value_top + g_flight_font_line_height + 1);
			flight_text_set_color(0x52);
			flight_text_set_cursor(value_left, value_top);
			if (g_object_table[current_target_object_idx]
				    .mobj->speed == 0) {
				if (strcmp(g_plan_table
						   [controller->running_plan_id]
							   .name,
					   "board2pln") != 0 &&
				    strcmp(g_plan_table
						   [controller->running_plan_id]
							   .name,
					   "waitpln") != 0) {
					if (target_distance == 0) {
						g_hud_element_state_cache
							[CMD_ORDER_TIME_ELEMENT] =
								0;
						g_flight_fill_clip_rect_fn();
#ifdef XVT_MODERN
						xvt_cockpit_text_clear_field(
							XVT_COCKPIT_TEXT_CMD_TIME_UNKNOWN);
#endif
#ifdef XVT_MODERN
						xvt_cockpit_readouts_record_number(
							XVT_COCKPIT_NUMBER_ORDER_MINUTES,
							0, 2, 1);
#endif
						flight_text_draw_decimal_number(
							0, 2, 1);
#ifdef XVT_MODERN
						xvt_cockpit_text_record_field(
							XVT_COCKPIT_TEXT_ORDER_TIME_SEPARATOR,
							":",
							XVT_COCKPIT_ALIGN_LEFT);
#endif
						g_flight_draw_char_fn(':');
#ifdef XVT_MODERN
						xvt_cockpit_readouts_record_number(
							XVT_COCKPIT_NUMBER_ORDER_SECONDS,
							0, 2, 2);
#endif
						flight_text_draw_decimal_number(
							0, 2, 2);
					} else {
#ifdef XVT_MODERN
						xvt_cockpit_readouts_clear_order_time();
						xvt_cockpit_text_record_field(
							XVT_COCKPIT_TEXT_CMD_TIME_UNKNOWN,
							g_str_unknown,
							XVT_COCKPIT_ALIGN_LEFT);
#endif
						flight_text_draw_string(
							g_str_unknown);
					}
					return;
				}
				uint16_t total_seconds =
					(uint16_t)(controller->maneuver_timer /
						   SIMULATION_TICKS_PER_SECOND);
				uint16_t minutes =
					total_seconds / SECONDS_PER_MINUTE;
				uint16_t seconds = total_seconds -
						   minutes * SECONDS_PER_MINUTE;
				if (seconds ==
				    (uint16_t)g_hud_element_state_cache
					    [CMD_ORDER_TIME_ELEMENT]) {
					return;
				}
				g_hud_element_state_cache
					[CMD_ORDER_TIME_ELEMENT] =
						(int16_t)seconds;
				g_flight_fill_clip_rect_fn();
#ifdef XVT_MODERN
				xvt_cockpit_text_clear_field(
					XVT_COCKPIT_TEXT_CMD_TIME_UNKNOWN);
#endif
#ifdef XVT_MODERN
				xvt_cockpit_readouts_record_number(
					XVT_COCKPIT_NUMBER_ORDER_MINUTES,
					minutes, 2, 1);
#endif
				flight_text_draw_decimal_number(minutes, 2, 1);
#ifdef XVT_MODERN
				xvt_cockpit_text_record_field(
					XVT_COCKPIT_TEXT_ORDER_TIME_SEPARATOR,
					":", XVT_COCKPIT_ALIGN_LEFT);
#endif
				g_flight_draw_char_fn(':');
#ifdef XVT_MODERN
				xvt_cockpit_readouts_record_number(
					XVT_COCKPIT_NUMBER_ORDER_SECONDS,
					seconds, 2, 2);
#endif
				flight_text_draw_decimal_number(seconds, 2, 2);
			} else {
				uint16_t distance_per_second =
					(uint16_t)(DISTANCE_PER_SPEED_SECOND *
						   g_object_table
							   [current_target_object_idx]
								   .mobj
								   ->speed);
				uint16_t total_seconds =
					(uint16_t)(target_distance /
						   distance_per_second);
				uint16_t minutes =
					total_seconds / SECONDS_PER_MINUTE;
				uint16_t seconds = total_seconds -
						   minutes * SECONDS_PER_MINUTE;
				if (seconds ==
				    (uint16_t)g_hud_element_state_cache
					    [CMD_ORDER_TIME_ELEMENT]) {
					return;
				}
				g_hud_element_state_cache
					[CMD_ORDER_TIME_ELEMENT] =
						(int16_t)seconds;
				g_flight_fill_clip_rect_fn();
#ifdef XVT_MODERN
				xvt_cockpit_text_clear_field(
					XVT_COCKPIT_TEXT_CMD_TIME_UNKNOWN);
#endif
#ifdef XVT_MODERN
				xvt_cockpit_readouts_record_number(
					XVT_COCKPIT_NUMBER_ORDER_MINUTES,
					minutes, 2, 1);
#endif
				flight_text_draw_decimal_number(minutes, 2, 1);
#ifdef XVT_MODERN
				xvt_cockpit_text_record_field(
					XVT_COCKPIT_TEXT_ORDER_TIME_SEPARATOR,
					":", XVT_COCKPIT_ALIGN_LEFT);
#endif
				g_flight_draw_char_fn(':');
#ifdef XVT_MODERN
				xvt_cockpit_readouts_record_number(
					XVT_COCKPIT_NUMBER_ORDER_SECONDS,
					seconds, 2, 2);
#endif
				flight_text_draw_decimal_number(seconds, 2, 2);
			}
		}
	}
}

/* Draws the target camera view's status of the local player's target: shield
 * and hull percents (elements 102 and 103) and the armament sprites 98 to 101
 * for lasers, ion cannons, warheads and beam. Each shows 1 when the target
 * carries the weapon: lasers and ion cannons blink between 1 and 2 while
 * linked, warheads while a lock builds (an AI craft's count only during a
 * rocket attack), and the beam shows 2 while active and charged. While a state
 * is not 0 its label from g_str_threat_display_text is drawn at layouts 135 to
 * 138, plain for 1 and inverted for 2. A target outside the craft slots shows
 * zeros. The hull shows 100 when under 1 percent is left, and the label check
 * reads g_hud_element_state_cache 135 to 138, which nothing here writes, so the
 * labels redraw on every call. */
// FUNCTION: XVT 0x440140
void hud_draw_cmd_target_status_indicators(void)
{
	int current_target_object_idx =
		(uint16_t)g_players[g_local_player].current_target_object_idx;

	struct craft_data *craft;
	{
		unsigned int shield_percent;
		if (g_active_region_craft_object_slot_end >
		    current_target_object_idx) {
			craft = g_object_table[current_target_object_idx]
					.mobj->p_craft;
			unsigned int shield =
				(unsigned int)(craft->shield_energy[0] +
					       craft->shield_energy[1]);
			unsigned int max_shield = craft_get_object_max_shield(
				g_players[g_local_player]
					.current_target_object_idx);
			shield >>= 1;
			if (max_shield != 0) {
				unsigned int shield_ratio_q16 =
					math2_longratio_q16(shield, max_shield);
				shield_ratio_q16 &= 0xFFFFu;
				shield_percent = 2 * (shield_ratio_q16 / 0x28F);
			} else {
				shield_percent = 0;
			}
		} else {
			shield_percent = 0;
		}
		hud_draw_cached_numeric_element(0x66, shield_percent, 1);
	}

	{
		unsigned int hull_percent;
		if (g_active_region_craft_object_slot_end >
		    current_target_object_idx) {
			craft = g_object_table[current_target_object_idx]
					.mobj->p_craft;
			if (craft->object_kind !=
				    CRAFT_OBJECT_KIND_BREAKING_UP &&
			    craft->object_kind != CRAFT_OBJECT_KIND_EXPLODING) {
				if (craft->hull_max < craft->hull_damage) {
					hull_percent = 1;
				} else {
					unsigned int hull_ratio_q16 =
						(uint16_t)math2_longratio_q16(
							craft->hull_max -
								craft->hull_damage,
							craft->hull_max);
					hull_ratio_q16 &= 0xFFFFu;
					hull_percent = hull_ratio_q16 / 0x28F;
					if (hull_percent == 0) {
						hull_percent = 100;
					}
				}
			} else {
				hull_percent = 0;
			}
		} else {
			hull_percent = 0;
		}
		hud_draw_cached_numeric_element(0x67, hull_percent, 1);
	}

	int16_t y;
	uint16_t width;
	{
		uint16_t laser_state = 0;
		if (g_active_region_craft_object_slot_end >
		    current_target_object_idx) {
			uint16_t i = 0;
			uint8_t cannon_class_count = craft->cannon_group_count;
			if (cannon_class_count != 0) {
				do {
					uint8_t projectile_type =
						craft->laser_state
							.projectile_type_id[i];
					if (projectile_type ==
						    PROJECTILE_OBJECT_TYPE_IMPERIAL_LASER ||
					    projectile_type ==
						    PROJECTILE_OBJECT_TYPE_REBEL_LASER) {
						laser_state = 1;
						if (craft->laser_state
							    .link_mode[i] !=
						    0) {
							laser_state =
								(uint16_t)(((g_mission_elapsed_clock
										     .subsecond_ticks /
									     59) &
									    1) +
									   1);
						}
					}
					++i;
				} while (i < cannon_class_count);
			}
		}
#ifdef XVT_MODERN
		xvt_cockpit_readouts_record_armament(0, laser_state);
#endif
		hud_draw_cached_sprite_element(0x62, laser_state);
		if (laser_state != 0 &&
		    (uint16_t)g_hud_element_state_cache[135] != laser_state) {
			if (laser_state == 1) {
				flight_text_set_color(
					g_hud_element_layouts[135]
						.color_index_or_widget_param);
				flight_text_set_background_color(0x2C);
			} else {
				flight_text_set_color(0x2C);
				flight_text_set_background_color(
					g_hud_element_layouts[135]
						.color_index_or_widget_param +
					1);
			}
			y = g_hud_element_layouts[135].y;
			int16_t bottom = g_hud_element_layouts[135].y +
					 g_flight_font_line_height;
			width = flight_text_measure_string_width(
				g_str_threat_display_text[0]);
			width += g_hud_element_layouts[135].x;
			flight_text_set_clip_rect(g_hud_element_layouts[135].x,
						  y, width, bottom);
			flight_text_set_cursor(g_hud_element_layouts[135].x,
					       g_hud_element_layouts[135].y);
#ifdef XVT_MODERN
			xvt_cockpit_text_record_field(
				(xvt_cockpit_text_field_id)(XVT_COCKPIT_TEXT_ARMAMENT_LABEL_FIRST +
							    0),
				g_str_threat_display_text[0],
				XVT_COCKPIT_ALIGN_LEFT);
#endif
			flight_text_draw_string(g_str_threat_display_text[0]);
		}
	}

	{
		uint16_t ion_state = 0;
		if (g_active_region_craft_object_slot_end >
		    current_target_object_idx) {
			uint16_t i = 0;
			uint8_t cannon_class_count = craft->cannon_group_count;
			while (i < cannon_class_count) {
				if (craft->laser_state.projectile_type_id[i] ==
				    PROJECTILE_OBJECT_TYPE_ION_LASER) {
					ion_state = 1;
					if (craft->laser_state.link_mode[i] !=
					    0) {
						ion_state =
							(uint16_t)(((g_mission_elapsed_clock
									     .subsecond_ticks /
								     59) &
								    1) +
								   1);
					}
				}
				++i;
			}
		}
#ifdef XVT_MODERN
		xvt_cockpit_readouts_record_armament(1, ion_state);
#endif
		hud_draw_cached_sprite_element(0x63, ion_state);
		if (ion_state != 0 &&
		    (uint16_t)g_hud_element_state_cache[136] != ion_state) {
			if (ion_state == 1) {
				flight_text_set_color(
					g_hud_element_layouts[136]
						.color_index_or_widget_param);
				flight_text_set_background_color(0x2C);
			} else {
				flight_text_set_color(0x2C);
				flight_text_set_background_color(
					g_hud_element_layouts[136]
						.color_index_or_widget_param +
					1);
			}
			y = g_hud_element_layouts[136].y;
			int16_t bottom = g_hud_element_layouts[136].y +
					 g_flight_font_line_height;
			width = flight_text_measure_string_width(
				g_str_threat_display_text[1]);
			width += g_hud_element_layouts[136].x;
			flight_text_set_clip_rect(g_hud_element_layouts[136].x,
						  y, width, bottom);
			flight_text_set_cursor(g_hud_element_layouts[136].x,
					       g_hud_element_layouts[136].y);
#ifdef XVT_MODERN
			xvt_cockpit_text_record_field(
				(xvt_cockpit_text_field_id)(XVT_COCKPIT_TEXT_ARMAMENT_LABEL_FIRST +
							    1),
				g_str_threat_display_text[1],
				XVT_COCKPIT_ALIGN_LEFT);
#endif
			flight_text_draw_string(g_str_threat_display_text[1]);
		}
	}

	{
		uint16_t warhead_state = 0;
		if (g_active_region_craft_object_slot_end >
		    current_target_object_idx) {
			uint16_t i;
			if (g_object_table[current_target_object_idx]
				    .player_owner_idx == -1) {
				if (craft->ai_controller.maneuver_mode ==
				    AI_MANEUVER_MODE_ROCKET_ATTACK) {
					i = 0;
					uint8_t warhead_launcher_count =
						craft->warhead_launcher_count;
					if (warhead_launcher_count != 0) {
						do {
							if (craft->warhead_slot_type_ids
								    [i] != 0) {
								warhead_state =
									1;
								if (craft->warhead_lock_ticks >
								    0) {
									warhead_state =
										(uint16_t)(((g_mission_elapsed_clock
												     .subsecond_ticks /
											     59) &
											    1) +
											   1);
								}
							}
							++i;
						} while (
							i <
							warhead_launcher_count);
					}
				}
			} else {
				i = 0;
				uint8_t warhead_launcher_count =
					craft->warhead_launcher_count;
				if (warhead_launcher_count != 0) {
					do {
						if (craft->warhead_slot_type_ids
							    [i] != 0) {
							warhead_state = 1;
							if (craft->warhead_lock_ticks >
							    0) {
								warhead_state =
									(uint16_t)(((g_mission_elapsed_clock
											     .subsecond_ticks /
										     59) &
										    1) +
										   1);
							}
						}
						++i;
					} while (i < warhead_launcher_count);
				}
			}
		}
#ifdef XVT_MODERN
		xvt_cockpit_readouts_record_armament(2, warhead_state);
#endif
		hud_draw_cached_sprite_element(0x64, warhead_state);
		if (warhead_state != 0 &&
		    (uint16_t)g_hud_element_state_cache[137] != warhead_state) {
			if (warhead_state == 1) {
				flight_text_set_color(
					g_hud_element_layouts[137]
						.color_index_or_widget_param);
				flight_text_set_background_color(0x2C);
			} else {
				flight_text_set_color(0x2C);
				flight_text_set_background_color(
					g_hud_element_layouts[137]
						.color_index_or_widget_param +
					1);
			}
			y = g_hud_element_layouts[137].y;
			int16_t bottom = g_hud_element_layouts[137].y +
					 g_flight_font_line_height;
			width = flight_text_measure_string_width(
				g_str_threat_display_text[2]);
			width += g_hud_element_layouts[137].x;
			flight_text_set_clip_rect(g_hud_element_layouts[137].x,
						  y, width, bottom);
			flight_text_set_cursor(g_hud_element_layouts[137].x,
					       g_hud_element_layouts[137].y);
#ifdef XVT_MODERN
			xvt_cockpit_text_record_field(
				(xvt_cockpit_text_field_id)(XVT_COCKPIT_TEXT_ARMAMENT_LABEL_FIRST +
							    2),
				g_str_threat_display_text[2],
				XVT_COCKPIT_ALIGN_LEFT);
#endif
			flight_text_draw_string(g_str_threat_display_text[2]);
		}
	}

	{
		uint16_t beam_state = 0;
		if (g_active_region_craft_object_slot_end >
			    current_target_object_idx &&
		    craft->beam_type_id != BEAM_TYPE_NONE) {
			beam_state = 1;
			if (craft->beam_active != 0 &&
			    craft->beam_charge != 0) {
				beam_state = 2;
			}
		}
#ifdef XVT_MODERN
		xvt_cockpit_readouts_record_armament(3, beam_state);
#endif
		hud_draw_cached_sprite_element(0x65, beam_state);
		if (beam_state != 0 &&
		    (uint16_t)g_hud_element_state_cache[138] != beam_state) {
			if (beam_state == 1) {
				flight_text_set_color(
					g_hud_element_layouts[138]
						.color_index_or_widget_param);
				flight_text_set_background_color(0x2C);
			} else {
				flight_text_set_color(0x2C);
				flight_text_set_background_color(
					g_hud_element_layouts[138]
						.color_index_or_widget_param +
					1);
			}
			y = g_hud_element_layouts[138].y;
			int16_t bottom = g_hud_element_layouts[138].y +
					 g_flight_font_line_height;
			width = flight_text_measure_string_width(
				g_str_threat_display_text[3]);
			width += g_hud_element_layouts[138].x;
			flight_text_set_clip_rect(g_hud_element_layouts[138].x,
						  y, width, bottom);
			flight_text_set_cursor(g_hud_element_layouts[138].x,
					       g_hud_element_layouts[138].y);
#ifdef XVT_MODERN
			xvt_cockpit_text_record_field(
				(xvt_cockpit_text_field_id)(XVT_COCKPIT_TEXT_ARMAMENT_LABEL_FIRST +
							    3),
				g_str_threat_display_text[3],
				XVT_COCKPIT_ALIGN_LEFT);
#endif
			flight_text_draw_string(g_str_threat_display_text[3]);
		}
	}
}

/* Blits sprite selector + state of element_idx's layout at its position, with
 * its color_index_or_widget_param as the transparent color, when state differs from
 * the element's entry in g_hud_element_state_cache, which it then records. */
// FUNCTION: XVT 0x440760
void hud_draw_cached_sprite_element(unsigned int element_idx,
				    unsigned int state)
{
	if ((uint16_t)g_hud_element_state_cache[element_idx] != state) {
		g_hud_element_state_cache[element_idx] = (int16_t)state;
		if (g_hud_panel_sprite_data_by_index
			    [(uint16_t)g_hud_element_layouts[element_idx]
				     .selector +
			     state] == NULL) {
			XVT_LOG_ERROR("hud.sprite_missing element=%d sprite=%u",
				      (int)element_idx,
				      (unsigned)((uint16_t)g_hud_element_layouts
							 [element_idx]
								 .selector +
						 state));
		}
		g_flight_blit_sprite_fn(
			g_hud_panel_sprite_data_by_index
				[(uint16_t)g_hud_element_layouts[element_idx]
					 .selector +
				 state],
			g_hud_element_layouts[element_idx].x,
			g_hud_element_layouts[element_idx].y,
			g_hud_element_layouts[element_idx]
				.color_index_or_widget_param,
			0);
	}
}

/* Blits the first sprite of element_idx's layout with palette shift state and
 * fade amount fade, when state differs from the element's entry in
 * g_hud_element_state_cache, which it then records; a change of fade alone draws
 * nothing. */
// FUNCTION: XVT 0x4407D0
void hud_draw_cached_faded_sprite_element(uint16_t element_idx, int16_t state,
					  int16_t fade)
{
	if (g_hud_element_state_cache[element_idx] != state) {
		g_hud_element_state_cache[element_idx] = state;
		if (g_hud_panel_sprite_data_by_index
			    [g_hud_element_layouts[element_idx].selector] ==
		    NULL) {
			XVT_LOG_ERROR(
				"hud.sprite_missing element=%d sprite=%u",
				(int)element_idx,
				(unsigned)g_hud_element_layouts[element_idx]
					.selector);
		}
		g_flight_blit_sprite_faded_fn(
			g_hud_panel_sprite_data_by_index
				[g_hud_element_layouts[element_idx].selector],
			g_hud_element_layouts[element_idx].x,
			g_hud_element_layouts[element_idx].y,
			g_hud_element_layouts[element_idx]
				.color_index_or_widget_param,
			(int8_t)state, fade);
	}
}

/* Draws value in element_idx's layout, in as many digits as its selector and at
 * least minDigits, over a cleared field, when value differs from the element's
 * entry in g_hud_element_state_cache, which it then records. The target's system,
 * shield and hull values (elements 82, 85, 86, 102 and 103 of any set) are
 * drawn in color code 74 at 20 or less and 78 at 50 or less; the speed and
 * throttle (40 and 41) in 82 while engine_overdrive_off is 0; anything else in
 * the layout's color_index_or_widget_param. */
// FUNCTION: XVT 0x440840
void hud_draw_cached_numeric_element(uint16_t element_idx, int16_t value,
				     uint16_t min_digits)
{
	uint16_t normalized_element_idx = element_idx;
	if (g_hud_element_state_cache[element_idx] == value) {
		return;
	}
	g_hud_element_state_cache[element_idx] = value;
	uint16_t selector = g_hud_element_layouts[element_idx].selector;
	flight_text_set_clip_rect(g_hud_element_layouts[element_idx].x,
				  g_hud_element_layouts[element_idx].y,
				  g_hud_element_layouts[element_idx].x +
					  selector * g_flight_font_digit_width +
					  2,
				  g_hud_element_layouts[element_idx].y +
					  g_flight_font_line_height);
	g_flight_fill_clip_rect_fn();

	if (element_idx > HUD_INSTRUMENTS_PER_SET) {
		normalized_element_idx =
			element_idx -
			HUD_INSTRUMENTS_PER_SET * ((uint16_t)(element_idx - 1) /
						   HUD_INSTRUMENTS_PER_SET);
	}
	if ((uint16_t)value <= 20 &&
	    (normalized_element_idx == 85 || normalized_element_idx == 82 ||
	     normalized_element_idx == 102 || normalized_element_idx == 86 ||
	     normalized_element_idx == 103)) {
		flight_text_set_color(74);
	} else if ((uint16_t)value <= 50 && (normalized_element_idx == 85 ||
					     normalized_element_idx == 82 ||
					     normalized_element_idx == 86 ||
					     normalized_element_idx == 102 ||
					     normalized_element_idx == 103)) {
		flight_text_set_color(78);
	} else if ((normalized_element_idx == 41 ||
		    normalized_element_idx == 40) &&
		   g_object_table[g_players[g_local_player].object_index]
				   .mobj->p_craft->engine_overdrive_off == 0) {
		flight_text_set_color(82);
	} else {
		flight_text_set_color(g_hud_element_layouts[element_idx]
					      .color_index_or_widget_param);
	}
	flight_text_set_cursor(g_hud_element_layouts[element_idx].x,
			       g_hud_element_layouts[element_idx].y);
#ifdef XVT_MODERN
	xvt_cockpit_readouts_record_cached_number(element_idx, value,
						  min_digits);
#endif
	flight_text_draw_decimal_number(value, selector, min_digits);
}

/* Loads the local player's cockpit: sets g_hud_cockpit_base_path to the
 * resolution's cockpit folder and the model's cockpit_resource_name, sets
 * g_hud_panel_set_id to 0, reads its .INT file and the craft list's, and loads the
 * cockpit image resources. Does not check the name's length. */
// FUNCTION: XVT 0x4409D0
void hud_load_cockpit_resources(void)
{
	strcpy(g_hud_cockpit_base_path, g_hud_cockpit_resolution_directory);
	model_index model_index = get_model_index_from_type(
		g_object_table[g_players[g_local_player].object_index]
			.object_type);
	const char *source_name =
		g_model_defs[model_index].cockpit_resource_name;
	char cockpit_resource_name[16];
	unsigned int name_index;
	for (name_index = 0; source_name[name_index] != '\0'; ++name_index) {
		cockpit_resource_name[name_index] = source_name[name_index];
	}
	cockpit_resource_name[name_index] = source_name[name_index];
	strcat(g_hud_cockpit_base_path, cockpit_resource_name);
	g_hud_panel_set_id = 0;
	hud_load_cockpit_interface_file(g_hud_cockpit_base_path);
	hud_load_auxiliary_cockpit_interface_file();
	model_index = get_model_index_from_type(
		g_object_table[g_players[g_local_player].object_index]
			.object_type);
	hud_load_cockpit_sprite_resources(model_index);
	XVT_LOG_INFO(
		"hud.cockpit_loaded slot=%d craft=%d model=%u cockpit=\"%s\"",
		g_local_player,
		(int)g_object_table[g_players[g_local_player].object_index]
			.object_type,
		(unsigned)model_index, cockpit_resource_name);
}

/* Reads base_path plus ".INT", named in g_hud_cockpit_resource_path, into the HUD
 * tables: the 28 g_hud_cockpit_resource_descriptors, the first 288
 * g_hud_element_layouts (the cockpit and HUD-only sets),
 * g_hud_panel_sprite_file_info, and the cockpit and HUD-only inset span masks, 480
 * bytes each at 640x480 and 480x360 and 200 at 320x240. Reads go through
 * fe_disk_io_read_with_retry_prompt. The modern build records the cockpit for its
 * renderer. Does not check the path's length. */
// FUNCTION: XVT 0x440B10
void hud_load_cockpit_interface_file(const char *base_path)
{
	strcpy(g_hud_cockpit_resource_path, base_path);
	strcat(g_hud_cockpit_resource_path, ".INT");
	fe_disk_io_open_global_stream(g_hud_cockpit_resource_path, "rb", 1, 0);
	xvt_file *stream = g_stream;
	fe_disk_io_read_with_retry_prompt(
		g_hud_cockpit_resource_descriptors,
		sizeof(struct hud_cockpit_resource_descriptor), 28, stream);
	fe_disk_io_read_with_retry_prompt(
		g_hud_element_layouts, sizeof(struct hud_element_layout),
		HUD_CRAFT_LIST_INSTRUMENT_BASE_INDEX, stream);
	fe_disk_io_read_with_retry_prompt(
		&g_hud_panel_sprite_file_info,
		sizeof(struct hud_panel_sprite_file_info), 1, stream);
	if (g_flight_resolution_mode == FLIGHT_RESOLUTION_640X480) {
		fe_disk_io_read_with_retry_prompt(g_hud_cockpit_inset_span_mask,
						  480, 1, stream);
		fe_disk_io_read_with_retry_prompt(
			g_hud_only_view_inset_span_mask, 480, 1, stream);
	} else if (g_flight_resolution_mode == FLIGHT_RESOLUTION_480X360) {
		fe_disk_io_read_with_retry_prompt(g_hud_cockpit_inset_span_mask,
						  480, 1, stream);
		fe_disk_io_read_with_retry_prompt(
			g_hud_only_view_inset_span_mask, 480, 1, stream);
	} else {
		fe_disk_io_read_with_retry_prompt(g_hud_cockpit_inset_span_mask,
						  200, 1, stream);
		fe_disk_io_read_with_retry_prompt(
			g_hud_only_view_inset_span_mask, 200, 1, stream);
	}
	fe_disk_io_close_global_stream(0);
	XVT_LOG_DEBUG(
		"hud.cockpit_layout_read panel=\"%s\" sprites=%d mask_bytes=%d",
		g_hud_panel_sprite_file_info.base_name,
		g_hud_panel_sprite_file_info.sprite_count +
			g_hud_panel_sprite_file_info.sprite_count_addend,
		g_flight_resolution_mode == FLIGHT_RESOLUTION_640X480 ||
				g_flight_resolution_mode ==
					FLIGHT_RESOLUTION_480X360
			? 480
			: 200);
#ifdef XVT_MODERN
	xvt_render_assets_capture_cockpit(0);
#endif
}

/* Reads the craft list view's .INT file, the one named by
 * g_hud_cockpit_resource_descriptors[HUD_VIEW_CRAFT_LIST] in the resolution's
 * cockpit folder: its 144 layouts into the third set of g_hud_element_layouts and
 * g_hud_craft_list_inset_span_mask, 480 or 200 bytes by resolution. Returns what
 * fe_disk_io_close_global_stream returns. */
// FUNCTION: XVT 0x440C50
int16_t hud_load_auxiliary_cockpit_interface_file(void)
{
	strcpy(g_hud_cockpit_resource_path, g_hud_cockpit_resolution_directory);
	strcat(g_hud_cockpit_resource_path,
	       g_hud_cockpit_resource_descriptors[HUD_VIEW_CRAFT_LIST]
		       .lfd_name);
	strcat(g_hud_cockpit_resource_path, ".INT");
	fe_disk_io_open_global_stream(g_hud_cockpit_resource_path, "rb", 1, 0);
	xvt_file *stream = g_stream;
	fe_disk_io_read_with_retry_prompt(
		&g_hud_element_layouts[HUD_CRAFT_LIST_INSTRUMENT_BASE_INDEX],
		sizeof(struct hud_element_layout), HUD_INSTRUMENTS_PER_SET,
		stream);
	if (g_flight_resolution_mode == FLIGHT_RESOLUTION_640X480) {
		fe_disk_io_read_with_retry_prompt(
			g_hud_craft_list_inset_span_mask, 480, 1, stream);
	} else if (g_flight_resolution_mode == FLIGHT_RESOLUTION_480X360) {
		fe_disk_io_read_with_retry_prompt(
			g_hud_craft_list_inset_span_mask, 480, 1, stream);
	} else {
		fe_disk_io_read_with_retry_prompt(
			g_hud_craft_list_inset_span_mask, 200, 1, stream);
	}
#ifdef XVT_MODERN
	xvt_render_assets_capture_cockpit(1);
#endif
	return fe_disk_io_close_global_stream(0);
}

/* Switches player_idx to hud_view_state even when it is already showing, by
 * setting hud_state_live to 0xFF first, then for the local player resets the
 * message panes without expiring them. */
// FUNCTION: XVT 0x440D60
void hud_force_player_view_state(int hud_view_state, int player_idx)
{
	g_players[player_idx].view_state.hud_state_live = UINT8_MAX;
	hud_set_hud_view_state(hud_view_state, player_idx);
	if (player_idx == g_local_player) {
		hud_reset_flight_message_panes(0);
	}
}

/* Redraws the local player's cockpit for view hud_view_state; does nothing for
 * another player. The view's resource_ref picks the cockpit image: below 0x80
 * its own, from 0x80 another, from 0xC0 another drawn mirrored; the full-screen
 * view always takes its own. Reloads the panel sprites (.PNL) when
 * g_hud_panel_set_id differs from g_hud_loaded_panel_set_id, adding the craft list's
 * first sprite to layout 396 once. For a view with a cockpit it loads the
 * image's LFD entries into g_flight_scratch_screen_buffer if they are not loaded,
 * sets palette entries 0 to 63 from it, clears the surface, blits the image,
 * and sets the flight viewport, span mask and g_proj_offset_y from the view's
 * descriptor; the full-screen view gets the whole surface and offset 0.
 *
 * Then it saves the MFD pages when leaving the forward or HUD-only view, closes
 * them when leaving the craft list, and sets g_hud_instrument_set_base_index and
 * the pages for the new view: the HUD-only set, with the saved pages unless
 * coming from the forward view; the craft list set, with the map help and
 * friendly craft pages; the cockpit set for every other view. The full-screen
 * view closes the pages while the external camera is on, the target camera view
 * always; the remaining cockpit views, unless coming from the HUD-only view,
 * get the saved pages back. The craft list, the target camera and those cockpit
 * views also reset the message panes with expiry. Then hud_init_hud, a palette
 * reset, and for resource 17 the view's displayName at layout 49. Sets
 * g_flight_initial_texture_cache_flush_pending to 1 and
 * g_flight_display_rebuild_pending to 0. The modern build resets and refreshes its
 * captured cockpit. */
// FUNCTION: XVT 0x440DA0
void hud_rebuild_display_for_view_state(int hud_view_state, int player_idx)
{
	enum {
		HUD_VIEW_RESOURCE_NAME = 17,
		HUD_RESOURCE_REFERENCE = 0x80,
		HUD_RESOURCE_MIRRORED_REFERENCE = 0xC0,
		HUD_AUXILIARY_PANEL_LAYOUT = 396,
		HUD_RESOURCE_NAME_LAYOUT = 49,
		PALETTE_COCKPIT_COLOR_COUNT = 0x40,
	};

	if (g_local_player != player_idx) {
		return;
	}

#ifdef XVT_MODERN
	xvt_cockpit_text_reset_fields();
	xvt_cockpit_readouts_reset();
	xvt_cockpit_pages_reset_working();
	xvt_cockpit_messages_reset_working();
#endif
	uint8_t mirror_horizontal = 0;
	int cockpit_x = 0;
	unsigned int resource_index =
		g_hud_cockpit_resource_descriptors[hud_view_state].resource_ref;
	if (hud_view_state != HUD_VIEW_FULL_SCREEN) {
		if (resource_index >= HUD_RESOURCE_MIRRORED_REFERENCE) {
			resource_index -= HUD_RESOURCE_MIRRORED_REFERENCE;
			mirror_horizontal = 1;
			cockpit_x = (int)(g_screen_width - 1);
		} else if (resource_index < HUD_RESOURCE_REFERENCE) {
			resource_index = (unsigned int)hud_view_state;
		} else {
			resource_index -= HUD_RESOURCE_REFERENCE;
		}
	} else {
		resource_index = (unsigned int)hud_view_state;
	}

	if (g_hud_loaded_panel_set_id != g_hud_panel_set_id) {
		g_hud_panel_sprite_data_write_cursor =
			(uint8_t *)memory_get_handle_block(
				g_hud_panel_sprite_data_handle);
		memory_handle_block_done_stub(g_hud_panel_sprite_data_handle);
		strcpy(g_hud_cockpit_base_path,
		       g_hud_cockpit_resolution_directory);
		strcat(g_hud_cockpit_base_path,
		       g_hud_panel_sprite_file_info.base_name);
		strcat(g_hud_cockpit_base_path, ".PNL");
		hud_load_panel_sprite_records(
			g_hud_cockpit_base_path, 0,
			g_hud_panel_sprite_file_info.sprite_count_addend +
				g_hud_panel_sprite_file_info.sprite_count,
			0);
		g_hud_loaded_panel_set_id = g_hud_panel_set_id;

		if (g_hud_element_layouts[HUD_AUXILIARY_PANEL_LAYOUT]
			    .selector == 0) {
			strcpy(g_hud_cockpit_base_path,
			       g_hud_cockpit_resolution_directory);
			strcat(g_hud_cockpit_base_path,
			       g_hud_cockpit_resource_descriptors
				       [HUD_VIEW_CRAFT_LIST]
					       .lfd_name);
			strcat(g_hud_cockpit_base_path, ".PNL");
			hud_load_panel_sprite_records(
				g_hud_cockpit_base_path,
				g_hud_panel_sprite_file_info
						.sprite_count_addend +
					g_hud_panel_sprite_file_info
						.sprite_count +
					1,
				1, 0);
			g_hud_element_layouts[HUD_AUXILIARY_PANEL_LAYOUT]
				.selector =
				g_hud_panel_sprite_file_info
					.sprite_count_addend +
				g_hud_panel_sprite_file_info.sprite_count + 1;
		}
	}

	flight_render_invoke_transition_hook(1);
	if (g_players[g_local_player].view_state.hud_state_live !=
	    HUD_VIEW_FULL_SCREEN) {
		int resource_loaded =
			g_hud_cockpit_resources[resource_index].memory_handle !=
				0 &&
			g_hud_cockpit_resources_loaded != 0;
		if (resource_loaded == 0) {
			g_hud_cockpit_resource_write_cursor =
				g_flight_scratch_screen_buffer;
			hud_load_cockpit_lfd_entries(
				g_hud_cockpit_resource_descriptors
					[resource_index]
						.lfd_name,
				g_hud_cockpit_resources[resource_index].entries,
				sizeof(g_hud_cockpit_resources[resource_index]
					       .entries) /
					sizeof(g_hud_cockpit_resources
						       [resource_index]
							       .entries[0]));
		}
		g_flight_set_palette_range_fn(
			(struct rgb_triplet *)
				g_hud_cockpit_resources[resource_index]
					.entries[2],
			0, PALETTE_COCKPIT_COLOR_COUNT);
		flight_text_set_clip_rect(0, 0, g_surface_width,
					  g_surface_height);
		g_flight_text_bg_color = g_flight_background_color_index;
		g_flight_fill_clip_rect_fn();
		g_flight_blit_sprite_fn(
			g_hud_cockpit_resources[resource_index].entries[0],
			cockpit_x, 0, 0, mirror_horizontal);

		unsigned int viewport_descriptor_index =
			mirror_horizontal == 1 ? (unsigned int)hud_view_state
					       : resource_index;
		unsigned int base_offset = g_flight_compute_pixel_offset_fn(
			g_hud_cockpit_resource_descriptors
				[viewport_descriptor_index]
					.viewport_origin_x,
			g_hud_cockpit_resource_descriptors
				[viewport_descriptor_index]
					.viewport_origin_y);
		set_flight_viewport(g_hud_cockpit_resource_descriptors
					    [viewport_descriptor_index]
						    .viewport_width,
				    g_hud_cockpit_resource_descriptors
					    [viewport_descriptor_index]
						    .viewport_height,
				    g_flight_viewport_mode, base_offset);
		flight_sw_copy_viewport_span_mask_rle(
			g_hud_cockpit_resources[resource_index].entries[1],
			g_flight_vp_width, g_flight_vp_height,
			mirror_horizontal);
		g_proj_offset_y = g_hud_cockpit_resource_descriptors
					  [viewport_descriptor_index]
						  .projection_offset_y;
	} else {
		flight_text_set_clip_rect(0, 0, g_surface_width,
					  g_surface_height);
		g_flight_text_bg_color = g_flight_background_color_index;
		g_flight_fill_clip_rect_fn();
		set_flight_viewport(g_surface_width, g_surface_height,
				    g_flight_viewport_mode, 0);
		flight_sw_build_full_viewport_span_mask_rle(
			(uint16_t)g_surface_width,
			(unsigned int)g_surface_height);
		g_proj_offset_y = 0;
	}

	unsigned int page_index;
	if (g_players[player_idx].view_state.hud_state_mirror ==
		    HUD_VIEW_HUD_ONLY ||
	    g_players[player_idx].view_state.hud_state_mirror ==
		    HUD_VIEW_FORWARD) {
		g_mfd_saved_secondary_page = g_mfd_secondary_page;
		g_mfd_saved_active_page = g_mfd_active_page;
		for (page_index = MFD_PAGE_SCOREBOARD;
		     page_index < MFD_PAGE_COUNT; ++page_index) {
			g_saved_mfd_page_states[page_index] =
				g_mfd_page_states[page_index];
		}
	}

	if (g_players[player_idx].view_state.hud_state_mirror ==
	    HUD_VIEW_CRAFT_LIST) {
		switch (g_flight_resolution_mode) {
		case FLIGHT_RESOLUTION_320X240:
			g_mfd_craft_list_blit_width = 112;
			g_mfd_craft_list_blit_height = 47;
			break;
		case FLIGHT_RESOLUTION_640X480:
			g_mfd_craft_list_blit_width = 225;
			g_mfd_craft_list_blit_height = 94;
			break;
		case FLIGHT_RESOLUTION_480X360:
			g_mfd_craft_list_blit_width = 168;
			g_mfd_craft_list_blit_height = 70;
			break;
		}
		for (page_index = MFD_PAGE_SCOREBOARD;
		     page_index < MFD_PAGE_COUNT; ++page_index) {
			if (g_mfd_page_states[page_index] !=
			    MFD_PAGE_STATE_CLOSED) {
				g_mfd_page_states[page_index] =
					MFD_PAGE_STATE_CLOSING;
			}
		}
		g_mfd_active_page = MFD_PAGE_NONE;
		g_mfd_secondary_page = MFD_PAGE_NONE;
		hud_update_mfd_pages();
	}

	if (hud_view_state == HUD_VIEW_FULL_SCREEN) {
		g_hud_instrument_set_base_index =
			HUD_COCKPIT_INSTRUMENT_BASE_INDEX;
		if (g_players[player_idx].view_state.external_camera_active !=
		    0) {
			for (page_index = MFD_PAGE_SCOREBOARD;
			     page_index < MFD_PAGE_COUNT; ++page_index) {
				if (g_mfd_page_states[page_index] !=
				    MFD_PAGE_STATE_CLOSED) {
					g_mfd_page_states[page_index] =
						MFD_PAGE_STATE_CLOSING;
				}
			}
			g_mfd_active_page = MFD_PAGE_NONE;
			g_mfd_secondary_page = MFD_PAGE_NONE;
		}
	} else if (hud_view_state == HUD_VIEW_HUD_ONLY) {
		if (g_players[player_idx].view_state.hud_state_mirror !=
		    HUD_VIEW_FORWARD) {
			g_mfd_secondary_page = g_mfd_saved_secondary_page;
			g_mfd_active_page = g_mfd_saved_active_page;
			for (page_index = MFD_PAGE_SCOREBOARD;
			     page_index < MFD_PAGE_COUNT; ++page_index) {
				g_mfd_page_states[page_index] =
					g_saved_mfd_page_states[page_index];
			}
		}
		g_hud_instrument_set_base_index =
			HUD_ONLY_VIEW_INSTRUMENT_BASE_INDEX;
	} else if (hud_view_state == HUD_VIEW_CRAFT_LIST) {
		for (page_index = MFD_PAGE_SCOREBOARD;
		     page_index < MFD_PAGE_COUNT; ++page_index) {
			if (g_mfd_page_states[page_index] !=
			    MFD_PAGE_STATE_CLOSED) {
				g_mfd_page_states[page_index] =
					MFD_PAGE_STATE_CLOSING;
			}
		}
		g_hud_instrument_set_base_index =
			HUD_CRAFT_LIST_INSTRUMENT_BASE_INDEX;
		++g_mfd_page_states[MFD_PAGE_MAP_HELP];
		++g_mfd_page_states[MFD_PAGE_FRIENDLY_CRAFT];
		g_mfd_active_page = MFD_PAGE_FRIENDLY_CRAFT;
		g_mfd_secondary_page = MFD_PAGE_FRIENDLY_CRAFT;
		hud_reset_flight_message_panes(1);
	} else if (hud_view_state == HUD_VIEW_TARGET_CAMERA) {
		for (page_index = MFD_PAGE_SCOREBOARD;
		     page_index < MFD_PAGE_COUNT; ++page_index) {
			if (g_mfd_page_states[page_index] !=
			    MFD_PAGE_STATE_CLOSED) {
				g_mfd_page_states[page_index] =
					MFD_PAGE_STATE_CLOSING;
			}
		}
		g_hud_instrument_set_base_index =
			HUD_COCKPIT_INSTRUMENT_BASE_INDEX;
		g_mfd_active_page = MFD_PAGE_NONE;
		g_mfd_secondary_page = MFD_PAGE_NONE;
		hud_reset_flight_message_panes(1);
	} else {
		g_hud_instrument_set_base_index =
			HUD_COCKPIT_INSTRUMENT_BASE_INDEX;
		if (g_players[player_idx].view_state.hud_state_mirror !=
		    HUD_VIEW_HUD_ONLY) {
			hud_reset_flight_message_panes(1);
			g_mfd_secondary_page = g_mfd_saved_secondary_page;
			g_mfd_active_page = g_mfd_saved_active_page;
			for (page_index = MFD_PAGE_SCOREBOARD;
			     page_index < MFD_PAGE_COUNT; ++page_index) {
				g_mfd_page_states[page_index] =
					g_saved_mfd_page_states[page_index];
			}
		}
	}

	hud_init_hud(player_idx);
	g_flight_initial_texture_cache_flush_pending = 1;
	flight_render_reset_palette(1);
	g_flight_display_rebuild_pending = 0;
	XVT_LOG_DEBUG(
		"hud.view_rebuilt slot=%d view=%d from=%d image=%u mirrored=%d set=%u width=%u height=%u offset=%d active=%u secondary=%u saved_active=%u saved_secondary=%u states=\"%d,%d,%d,%d,%d,%d,%d,%d\" predicted=%d",
		player_idx, hud_view_state,
		(int)g_players[player_idx].view_state.hud_state_mirror,
		resource_index, (int)mirror_horizontal,
		(unsigned)g_hud_instrument_set_base_index,
		(unsigned)g_flight_vp_width, (unsigned)g_flight_vp_height,
		g_proj_offset_y, (unsigned)g_mfd_active_page,
		(unsigned)g_mfd_secondary_page,
		(unsigned)g_mfd_saved_active_page,
		(unsigned)g_mfd_saved_secondary_page, (int)g_mfd_page_states[0],
		(int)g_mfd_page_states[1], (int)g_mfd_page_states[2],
		(int)g_mfd_page_states[3], (int)g_mfd_page_states[4],
		(int)g_mfd_page_states[5], (int)g_mfd_page_states[6],
		(int)g_mfd_page_states[7],
		g_flight_sim_side_effects_suppressed);
	if (resource_index == HUD_VIEW_RESOURCE_NAME) {
		flight_text_set_font_tier(2);
		unsigned int text_y = 0;
		text_y = (uint16_t)
				 g_hud_element_layouts[HUD_RESOURCE_NAME_LAYOUT]
					 .y;
		flight_text_set_clip_rect(
			g_hud_element_layouts[HUD_RESOURCE_NAME_LAYOUT].x,
			(int)text_y,
			g_hud_element_layouts[HUD_RESOURCE_NAME_LAYOUT].x +
				g_hud_element_layouts[HUD_RESOURCE_NAME_LAYOUT]
					.clip_width,
			(int)(text_y + g_flight_font_line_height + 1));
		flight_text_set_background_color(PALETTE_COCKPIT_COLOR_COUNT);
		g_flight_fill_clip_rect_fn();
		flight_text_set_color(PALETTE_COCKPIT_COLOR_COUNT + 3);
		flight_text_set_cursor(0, text_y);
#ifdef XVT_MODERN
		xvt_cockpit_text_record_field(
			XVT_COCKPIT_TEXT_RESOURCE_NAME,
			g_hud_cockpit_resource_descriptors
				[g_players[g_local_player]
					 .view_state.hud_state_live]
					.display_name,
			XVT_COCKPIT_ALIGN_CENTER);
#endif
		flight_text_draw_string_centered(
			g_hud_cockpit_resource_descriptors
				[g_players[g_local_player]
					 .view_state.hud_state_live]
					.display_name);
	}
#ifdef XVT_MODERN
	xvt_cockpit_refresh_instruments(player_idx);
#endif
}

/* Reads entry_count entries of lfd_name's .LFD file, in the resolution's cockpit
 * folder, to g_hud_cockpit_resource_write_cursor and advances it, storing where
 * each entry's data starts in out_entries. Each entry is a 16-byte header (type
 * tag, name, data size) and its data; a PLTT (palette) entry has every byte
 * divided by 4 and its pointer moved 2 bytes in. Does not check the space at
 * the cursor or the sizes in the file. The modern build registers the entries
 * for its renderer. */
// FUNCTION: XVT 0x441550
void hud_load_cockpit_lfd_entries(const char *lfd_name, uint8_t **out_entries,
				  unsigned int entry_count)
{
	strcpy(g_hud_cockpit_resource_path, g_hud_cockpit_resolution_directory);
	strcat(g_hud_cockpit_resource_path, lfd_name);
	strcat(g_hud_cockpit_resource_path, ".LFD");
	fe_disk_io_open_global_stream(g_hud_cockpit_resource_path, "rb", 1, 0);
	xvt_file *stream = g_stream;
	uint16_t entry_index = 0;
	struct lfd_entry_header header;
	while (entry_index < entry_count) {
		uint8_t **output_entry = &out_entries[entry_index];
		*output_entry = g_hud_cockpit_resource_write_cursor;
		int16_t is_palette = 1;
		fe_disk_io_read_with_retry_prompt(&header, sizeof(header), 1,
						  stream);
		uint16_t palette_tag_index = 0;
		while (palette_tag_index < 4) {
			if (g_lfd_palette_resource_type_tag
				    [palette_tag_index] !=
			    header.resource_type[palette_tag_index]) {
				is_palette = 0;
			}
			++palette_tag_index;
		}
		size_t data_size = header.data_size;
		fe_disk_io_read_with_retry_prompt(
			g_hud_cockpit_resource_write_cursor, data_size, 1,
			stream);
		if (is_palette == 0) {
			g_hud_cockpit_resource_write_cursor += data_size;
		} else {
			while (data_size-- != 0) {
				*g_hud_cockpit_resource_write_cursor >>= 2;
				++g_hud_cockpit_resource_write_cursor;
			}
		}
		if (is_palette != 0) {
			*output_entry += 2;
		}
		++entry_index;
	}
	fe_disk_io_close_global_stream(0);
	XVT_LOG_DEBUG(
		"hud.cockpit_lfd_read file=\"%s\" entries=%u bytes=%ld predicted=%d",
		lfd_name, entry_count,
		(long)(g_hud_cockpit_resource_write_cursor - out_entries[0]),
		g_flight_sim_side_effects_suppressed);
#ifdef XVT_MODERN
	xvt_render_assets_register_lfd(g_hud_cockpit_resource_path,
				       out_entries);
#endif
}

/* Loads the cockpit images: for each of the 28 views whose descriptor has
 * resource_ref 1 it allocates a memory handle the size of the view's .LFD file
 * (a failure is fatal) and reads the file's three entries into it; a second
 * pass reads every one again after all are allocated. Other views get handle 0.
 * Sets g_hud_cockpit_resources_loaded to 1. model_index is ignored. */
// FUNCTION: XVT 0x4416F0
void hud_load_cockpit_sprite_resources(unsigned int model_index)
{
	(void)model_index;

	uint16_t resource_index;
	for (resource_index = 0;
	     resource_index < (uint16_t)(sizeof(g_hud_cockpit_resources) /
					 sizeof(g_hud_cockpit_resources[0]));
	     ++resource_index) {
		g_hud_cockpit_resources[resource_index].memory_handle = 0;
		size_t file_size;
		if (g_hud_cockpit_resource_descriptors[resource_index]
			    .resource_ref == 1) {
			strcpy(g_hud_cockpit_resource_path,
			       g_hud_cockpit_resolution_directory);
			strcat(g_hud_cockpit_resource_path,
			       g_hud_cockpit_resource_descriptors
				       [resource_index]
					       .lfd_name);
			strcat(g_hud_cockpit_resource_path, ".LFD");
			fe_disk_io_open_global_stream(
				g_hud_cockpit_resource_path, "rb", 1, 0);
			if (g_stream != NULL) {
#ifdef XVT_MODERN
				FILE_RAW_SEEK((xvt_file *)g_stream, 0,
					      SEEK_END);
				file_size = (size_t)FILE_RAW_TELL(
					(xvt_file *)g_stream);
#else
				file_size = (size_t)_filelength(
					(_fileno)((xvt_file *)g_stream));
#endif
				fe_disk_io_close_global_stream(0);
				fe_disk_io_unlock_global_buffers();
				uint16_t memory_handle =
					memory_alloc_handle(file_size, 0);
				if (memory_handle == 0) {
					XVT_LOG_ERROR(
						"hud.cockpit_image_alloc_failed view=%u file=\"%s\" bytes=%u",
						(unsigned)resource_index,
						g_hud_cockpit_resource_descriptors
							[resource_index]
								.lfd_name,
						(unsigned)file_size);
					fe_disk_io_fatal_error(
						FILE_ERROR_STR_NOT_ENOUGH_MEMORY);
				}
				fe_disk_io_lock_global_buffers();
				if (memory_handle != 0) {
					g_hud_cockpit_resources[resource_index]
						.memory_handle =
						(int16_t)memory_handle;
					g_hud_cockpit_resource_write_cursor =
						(uint8_t *)
							memory_get_handle_block(
								memory_handle);
					memory_handle_block_done_stub(
						memory_handle);
					hud_load_cockpit_lfd_entries(
						g_hud_cockpit_resource_descriptors
							[resource_index]
								.lfd_name,
						g_hud_cockpit_resources
							[resource_index]
								.entries,
						sizeof(g_hud_cockpit_resources
							       [resource_index]
								       .entries) /
							sizeof(g_hud_cockpit_resources[resource_index]
								       .entries[0]));
					XVT_LOG_DEBUG(
						"hud.cockpit_image_loaded view=%u file=\"%s\" bytes=%u handle=%u",
						(unsigned)resource_index,
						g_hud_cockpit_resource_descriptors
							[resource_index]
								.lfd_name,
						(unsigned)file_size,
						(unsigned)memory_handle);
				}
			}
		}
	}

	for (resource_index = 0;
	     resource_index < (uint16_t)(sizeof(g_hud_cockpit_resources) /
					 sizeof(g_hud_cockpit_resources[0]));
	     ++resource_index) {
		if (g_hud_cockpit_resource_descriptors[resource_index]
			    .resource_ref == 1) {
			strcpy(g_hud_cockpit_resource_path,
			       g_hud_cockpit_resolution_directory);
			strcat(g_hud_cockpit_resource_path,
			       g_hud_cockpit_resource_descriptors
				       [resource_index]
					       .lfd_name);
			strcat(g_hud_cockpit_resource_path, ".LFD");
			uint16_t memory_handle =
				(uint16_t)
					g_hud_cockpit_resources[resource_index]
						.memory_handle;
			if (memory_handle != 0) {
				g_hud_cockpit_resource_write_cursor =
					(uint8_t *)memory_get_handle_block(
						memory_handle);
				memory_handle_block_done_stub(memory_handle);
				hud_load_cockpit_lfd_entries(
					g_hud_cockpit_resource_descriptors
						[resource_index]
							.lfd_name,
					g_hud_cockpit_resources[resource_index]
						.entries,
					sizeof(g_hud_cockpit_resources
						       [resource_index]
							       .entries) /
						sizeof(g_hud_cockpit_resources
							       [resource_index]
								       .entries[0]));
			}
		}
	}
	g_hud_cockpit_resources_loaded = 1;
}

/* Calls g_flight_render_transition_hook, sets g_hud_panel_set_id to 0 and reads the
 * .INT file again through g_hud_cockpit_resource_path, passing that path as the
 * base name: hud_load_cockpit_interface_file copies it onto itself and adds
 * ".INT" after the extension of the last file opened, so the name gets a
 * second extension. Nothing calls this. */
// FUNCTION: XVT 0x4419B0
void hud_reload_cockpit_interface_file(void)
{
	g_flight_render_transition_hook();
	g_hud_panel_set_id = 0;
	hud_load_cockpit_interface_file(g_hud_cockpit_resource_path);
}

/* Draws the MFD pages that need it: first each page closing
 * (MFD_PAGE_STATE_CLOSING) once more, which then becomes closed; then every
 * page whose state is not closed. Each draw records the page's state in its
 * element of g_hud_element_state_cache (scoreboard 134, goals 132, message log
 * 117, damage 133 when damage_display_mfd_page returns nonzero, friendly craft
 * 130, hostile craft and map help 131). */
// FUNCTION: XVT 0x4419D0
void hud_update_mfd_pages(void)
{
	uint16_t page;
	int16_t page_state;
	unsigned int page_index;

	for (page = MFD_PAGE_SCOREBOARD; page < MFD_PAGE_COUNT; ++page) {
		page_index = page;
		page_state = g_mfd_page_states[page_index];
		switch (page_index) {
		case MFD_PAGE_SCOREBOARD:
			if (page_state == MFD_PAGE_STATE_CLOSING) {
				mfd_draw_mission_scoreboard_page();
				g_hud_element_state_cache
					[g_hud_instrument_set_base_index +
					 HUD_MFD_SCOREBOARD_ELEMENT] =
						page_state;
			}
			break;
		case MFD_PAGE_GOALS:
			if (page_state == MFD_PAGE_STATE_CLOSING) {
				mfd_draw_mission_goals_page();
				g_hud_element_state_cache
					[g_hud_instrument_set_base_index +
					 HUD_MFD_GOALS_ELEMENT] = page_state;
			}
			break;
		case MFD_PAGE_MESSAGE_LOG:
			if (page_state == MFD_PAGE_STATE_CLOSING) {
				mfd_draw_message_log_page();
				g_hud_element_state_cache
					[g_hud_instrument_set_base_index +
					 HUD_MFD_MESSAGE_LOG_ELEMENT] =
						page_state;
			}
			break;
		case MFD_PAGE_DAMAGE:
			if (page_state == MFD_PAGE_STATE_CLOSING) {
				damage_display_mfd_page();
				g_hud_element_state_cache
					[g_hud_instrument_set_base_index +
					 HUD_MFD_DAMAGE_ELEMENT] = page_state;
			}
			break;
		case MFD_PAGE_HOSTILE_CRAFT:
			if (page_state == MFD_PAGE_STATE_CLOSING) {
				mfd_draw_craft_list_page(1);
				g_hud_element_state_cache
					[g_hud_instrument_set_base_index +
					 HUD_MFD_MAP_OR_COMMAND_ELEMENT] =
						page_state;
			}
			break;
		case MFD_PAGE_FRIENDLY_CRAFT:
			if (page_state == MFD_PAGE_STATE_CLOSING) {
				mfd_draw_craft_list_page(0);
				g_hud_element_state_cache
					[g_hud_instrument_set_base_index +
					 HUD_MFD_CRAFT_LIST_ELEMENT] =
						page_state;
			}
			break;
		case MFD_PAGE_MAP_HELP:
			if (page_state == MFD_PAGE_STATE_CLOSING) {
				mfd_draw_map_help_page();
				g_hud_element_state_cache
					[g_hud_instrument_set_base_index +
					 HUD_MFD_MAP_OR_COMMAND_ELEMENT] =
						page_state;
			}
			break;
		default:
			break;
		}
		if (page_state == MFD_PAGE_STATE_CLOSING) {
			g_mfd_page_states[page_index] = MFD_PAGE_STATE_CLOSED;
		}
	}

	for (page = MFD_PAGE_SCOREBOARD; page < MFD_PAGE_COUNT; ++page) {
		page_index = page;
		page_state = g_mfd_page_states[page_index];
		switch (page_index) {
		case MFD_PAGE_SCOREBOARD:
			if (page_state) {
				mfd_draw_mission_scoreboard_page();
				g_hud_element_state_cache
					[g_hud_instrument_set_base_index +
					 HUD_MFD_SCOREBOARD_ELEMENT] =
						page_state;
			}
			break;
		case MFD_PAGE_GOALS:
			if (page_state) {
				mfd_draw_mission_goals_page();
				g_hud_element_state_cache
					[g_hud_instrument_set_base_index +
					 HUD_MFD_GOALS_ELEMENT] = page_state;
			}
			break;
		case MFD_PAGE_MESSAGE_LOG:
			if (page_state) {
				mfd_draw_message_log_page();
				g_hud_element_state_cache
					[g_hud_instrument_set_base_index +
					 HUD_MFD_MESSAGE_LOG_ELEMENT] =
						page_state;
			}
			break;
		case MFD_PAGE_DAMAGE:
			if (page_state) {
				if (damage_display_mfd_page() != 0) {
					g_hud_element_state_cache
						[g_hud_instrument_set_base_index +
						 HUD_MFD_DAMAGE_ELEMENT] =
							page_state;
				}
			}
			break;
		case MFD_PAGE_HOSTILE_CRAFT:
			if (page_state) {
				mfd_draw_craft_list_page(1);
				g_hud_element_state_cache
					[g_hud_instrument_set_base_index +
					 HUD_MFD_MAP_OR_COMMAND_ELEMENT] =
						page_state;
			}
			break;
		case MFD_PAGE_FRIENDLY_CRAFT:
			if (page_state) {
				mfd_draw_craft_list_page(0);
				g_hud_element_state_cache
					[g_hud_instrument_set_base_index +
					 HUD_MFD_CRAFT_LIST_ELEMENT] =
						page_state;
			}
			break;
		case MFD_PAGE_MAP_HELP:
			if (page_state) {
				mfd_draw_map_help_page();
				g_hud_element_state_cache
					[g_hud_instrument_set_base_index +
					 HUD_MFD_MAP_OR_COMMAND_ELEMENT] =
						page_state;
			}
			break;
		default:
			break;
		}
	}
}

/* Copies each open MFD page from g_flight_offscreen_buffer onto the flight
 * surface, skipping pixels of g_flight_transparent_color_index, with the
 * g_mfd...Blit rectangles: the scoreboard, goals and hostile craft pages to the
 * map element while the map camera is on, else to their own; damage only
 * outside the map, map help only in it, friendly craft always. In the HUD-only
 * set with HUD feature 8 it also copies each warhead count drawn by
 * hud_output_warhead_count to layout 27 plus the launcher. The modern build
 * latches the pages for its renderer. */
// FUNCTION: XVT 0x441C30
void hud_blit_software_mfd_pages(void)
{
	int16_t page_state;

	for (uint16_t page = MFD_PAGE_SCOREBOARD; page < MFD_PAGE_COUNT;
	     ++page) {
		page_state = g_mfd_page_states[page];
		switch (page) {
		case MFD_PAGE_SCOREBOARD:
			if (page_state != MFD_PAGE_STATE_CLOSED) {
				if (g_players[g_local_player]
					    .map_camera_state != 0) {
					flight_sw_blit_rect_to_flight_surface(
						g_flight_offscreen_buffer,
						g_flight_transparent_color_index,
						g_mfd_map_blit_source_x,
						g_mfd_map_blit_source_y,
						g_hud_element_layouts
							[g_hud_instrument_set_base_index +
							 HUD_MFD_MAP_OR_COMMAND_ELEMENT]
								.x,
						g_hud_element_layouts
							[g_hud_instrument_set_base_index +
							 HUD_MFD_MAP_OR_COMMAND_ELEMENT]
								.y,
						g_mfd_map_blit_width,
						g_mfd_map_blit_height,
						g_flight_bytes_per_pixel *
							g_screen_width);
				} else {
					flight_sw_blit_rect_to_flight_surface(
						g_flight_offscreen_buffer,
						g_flight_transparent_color_index,
						g_mfd_mission_scoreboard_blit_source_x,
						g_mfd_mission_scoreboard_blit_source_y,
						g_hud_element_layouts
							[g_hud_instrument_set_base_index +
							 HUD_MFD_SCOREBOARD_ELEMENT]
								.x,
						g_hud_element_layouts
							[g_hud_instrument_set_base_index +
							 HUD_MFD_SCOREBOARD_ELEMENT]
								.y,
						g_mfd_mission_scoreboard_blit_width,
						g_mfd_mission_scoreboard_blit_height,
						g_flight_bytes_per_pixel *
							g_screen_width);
				}
			}
			break;
		case MFD_PAGE_GOALS:
			if (page_state != MFD_PAGE_STATE_CLOSED) {
				if (g_players[g_local_player]
					    .map_camera_state != 0) {
					flight_sw_blit_rect_to_flight_surface(
						g_flight_offscreen_buffer,
						g_flight_transparent_color_index,
						g_mfd_map_blit_source_x,
						g_mfd_map_blit_source_y,
						g_hud_element_layouts
							[g_hud_instrument_set_base_index +
							 HUD_MFD_MAP_OR_COMMAND_ELEMENT]
								.x,
						g_hud_element_layouts
							[g_hud_instrument_set_base_index +
							 HUD_MFD_MAP_OR_COMMAND_ELEMENT]
								.y,
						g_mfd_map_blit_width,
						g_mfd_map_blit_height,
						g_flight_bytes_per_pixel *
							g_screen_width);
				} else {
					flight_sw_blit_rect_to_flight_surface(
						g_flight_offscreen_buffer,
						g_flight_transparent_color_index,
						g_mfd_goals_blit_source_x,
						g_mfd_goals_blit_source_y,
						g_hud_element_layouts
							[g_hud_instrument_set_base_index +
							 HUD_MFD_GOALS_ELEMENT]
								.x,
						g_hud_element_layouts
							[g_hud_instrument_set_base_index +
							 HUD_MFD_GOALS_ELEMENT]
								.y,
						g_mfd_goals_blit_width,
						g_mfd_goals_blit_height,
						g_flight_bytes_per_pixel *
							g_screen_width);
				}
			}
			break;
		case MFD_PAGE_DAMAGE:
			if (page_state != MFD_PAGE_STATE_CLOSED &&
			    g_players[g_local_player].map_camera_state == 0) {
				flight_sw_blit_rect_to_flight_surface(
					g_flight_offscreen_buffer,
					g_flight_transparent_color_index,
					g_mfd_damage_blit_source_x,
					g_mfd_damage_blit_source_y,
					g_hud_element_layouts
						[g_hud_instrument_set_base_index +
						 HUD_MFD_DAMAGE_ELEMENT]
							.x,
					g_hud_element_layouts
						[g_hud_instrument_set_base_index +
						 HUD_MFD_DAMAGE_ELEMENT]
							.y,
					g_mfd_damage_blit_width,
					g_mfd_damage_blit_height,
					g_flight_bytes_per_pixel *
						g_screen_width);
			}
			break;
		case MFD_PAGE_HOSTILE_CRAFT:
			if (page_state != MFD_PAGE_STATE_CLOSED) {
				if (g_players[g_local_player]
					    .map_camera_state != 0) {
					flight_sw_blit_rect_to_flight_surface(
						g_flight_offscreen_buffer,
						g_flight_transparent_color_index,
						g_mfd_map_blit_source_x,
						g_mfd_map_blit_source_y,
						g_hud_element_layouts
							[g_hud_instrument_set_base_index +
							 HUD_MFD_MAP_OR_COMMAND_ELEMENT]
								.x,
						g_hud_element_layouts
							[g_hud_instrument_set_base_index +
							 HUD_MFD_MAP_OR_COMMAND_ELEMENT]
								.y,
						g_mfd_map_blit_width,
						g_mfd_map_blit_height,
						g_flight_bytes_per_pixel *
							g_screen_width);
				} else {
					flight_sw_blit_rect_to_flight_surface(
						g_flight_offscreen_buffer,
						g_flight_transparent_color_index,
						g_mfd_craft_list_blit_source_x,
						g_mfd_craft_list_blit_source_y,
						g_hud_element_layouts
							[g_hud_instrument_set_base_index +
							 HUD_MFD_CRAFT_LIST_ELEMENT]
								.x,
						g_hud_element_layouts
							[g_hud_instrument_set_base_index +
							 HUD_MFD_CRAFT_LIST_ELEMENT]
								.y,
						g_mfd_craft_list_blit_width,
						g_mfd_craft_list_blit_height,
						g_flight_bytes_per_pixel *
							g_screen_width);
				}
			}
			break;
		case MFD_PAGE_FRIENDLY_CRAFT:
			if (page_state != MFD_PAGE_STATE_CLOSED) {
				flight_sw_blit_rect_to_flight_surface(
					g_flight_offscreen_buffer,
					g_flight_transparent_color_index,
					g_mfd_craft_list_blit_source_x,
					g_mfd_craft_list_blit_source_y,
					g_hud_element_layouts
						[g_hud_instrument_set_base_index +
						 HUD_MFD_CRAFT_LIST_ELEMENT]
							.x,
					g_hud_element_layouts
						[g_hud_instrument_set_base_index +
						 HUD_MFD_CRAFT_LIST_ELEMENT]
							.y,
					g_mfd_craft_list_blit_width,
					g_mfd_craft_list_blit_height,
					g_flight_bytes_per_pixel *
						g_screen_width);
			}
			break;
		case MFD_PAGE_MAP_HELP:
			if (page_state != MFD_PAGE_STATE_CLOSED &&
			    g_players[g_local_player].map_camera_state != 0) {
				flight_sw_blit_rect_to_flight_surface(
					g_flight_offscreen_buffer,
					g_flight_transparent_color_index,
					g_mfd_map_blit_source_x,
					g_mfd_map_blit_source_y,
					g_hud_element_layouts
						[g_hud_instrument_set_base_index +
						 HUD_MFD_MAP_OR_COMMAND_ELEMENT]
							.x,
					g_hud_element_layouts
						[g_hud_instrument_set_base_index +
						 HUD_MFD_MAP_OR_COMMAND_ELEMENT]
							.y,
					g_mfd_map_blit_width,
					g_mfd_map_blit_height,
					g_flight_bytes_per_pixel *
						g_screen_width);
			}
			break;
		default:
			break;
		}
	}

	if (g_hud_instrument_set_base_index ==
	    HUD_ONLY_VIEW_INSTRUMENT_BASE_INDEX) {
		struct craft_data *craft =
			g_object_table[g_players[g_local_player].object_index]
				.mobj->p_craft;
		if ((craft->damage_stats.active_hud_feature_mask & 8) != 0) {
			int model_index = craft->model_index;
			flight_text_set_font_tier(0);
			uint16_t launcher_index = 0;
			uint16_t launcher_count =
				g_model_defs[model_index]
					.warhead_launcher_slot_count[1] +
				g_model_defs[model_index]
					.warhead_launcher_slot_count[0];
			while (launcher_index < launcher_count) {
				int layout_index =
					g_hud_instrument_set_base_index +
					launcher_index;
				int16_t selector =
					g_hud_element_layouts[layout_index + 27]
						.selector;
				if (selector != 0) {
#ifdef XVT_MODERN
					xvt_cockpit_latch_launcher(
						launcher_index,
						g_hud_element_layouts
							[layout_index + 27]
								.x,
						g_hud_element_layouts
							[layout_index + 27]
								.y,
						(g_flight_font_digit_width +
						 1) * selector,
						g_flight_font_line_height);
#endif
					flight_sw_blit_rect_to_flight_surface(
						g_flight_offscreen_buffer,
						g_flight_transparent_color_index,
						(g_flight_font_digit_width +
						 1) * launcher_index +
							2,
						2,
						g_hud_element_layouts
							[layout_index + 27]
								.x,
						g_hud_element_layouts
							[layout_index + 27]
								.y,
						(g_flight_font_digit_width +
						 1) * selector,
						g_flight_font_line_height,
						g_flight_bytes_per_pixel *
							g_screen_width);
				}
				++launcher_index;
			}
		}
	}
#ifdef XVT_MODERN
	xvt_cockpit_latch_pages();
#endif
}

/* Renders the local player's target into a small viewport at screen_x, screen_y:
 * copies the current set's inset span mask into the viewport when
 * refresh_span_mask is set, points the camera at the target (hud_point_camera),
 * draws the target model (craft lit and with damage billboards, projectiles, or
 * mines to debris), the visible projectiles fired by the target, and outside
 * the map those fired by the player, and visible explosions, on background
 * color 48. With target_box_enabled, a 4-pixel box marks the selected component
 * of a craft target that is not a fighter. Restores the camera position,
 * g_proj_offset_y, g_render_object_ref and the viewport, and sets
 * g_flight_background_color_index to the transparent color. Writes the scene
 * globals it uses, among them g_cam_rel_world_x, g_view_space_x, g_rotated_x and
 * g_cur_craft. */
// FUNCTION: XVT 0x4422C0
void hud_update3d_crt(uint16_t screen_x, uint16_t screen_y, uint16_t width,
		      uint16_t height, int16_t refresh_span_mask)
{
	enum {
		LOW_RESOLUTION_MASK_SIZE = 200,
		HIGH_RESOLUTION_MASK_SIZE = 480,
		TARGET_BOX_RENDER_FLAG = 0x0200,
		TARGET_CROSS_RENDER_FLAG = 0x0400,
		CRT_RENDER_COLOR = 48,
		COMPONENT_MARKER_SIZE = 4,
		COMPONENT_MARKER_HALF_SIZE = COMPONENT_MARKER_SIZE / 2,
		COMPONENT_MARKER_COLOR = 0xCE,
	};

#ifdef XVT_MODERN
	xvt_render_draw_scope(XVT_SCOPE_CRT);
#endif
	int saved_proj_offset_y = g_proj_offset_y;
	int saved_camera_world_x =
		g_players[g_local_player].view_state.camera_world_x;
	uint16_t saved_render_object_ref = g_render_object_ref;
	int saved_camera_world_y =
		g_players[g_local_player].view_state.camera_world_y;
	int saved_camera_world_z =
		g_players[g_local_player].view_state.camera_world_z;
	g_proj_offset_y = 0;
	unsigned int base_offset =
		(unsigned int)g_flight_compute_pixel_offset_fn(screen_x,
							       screen_y);
	push_flight_viewport(width, height, refresh_span_mask, base_offset);

	if (refresh_span_mask != 0) {
		uint8_t *destination_mask =
			&g_flight_aux_buffer[g_viewport_span_mask_offset];
		unsigned int hud_instrument_base_index =
			g_hud_instrument_set_base_index;
		const uint8_t *source_mask;
		if (hud_instrument_base_index !=
		    HUD_COCKPIT_INSTRUMENT_BASE_INDEX) {
			if (hud_instrument_base_index ==
			    HUD_ONLY_VIEW_INSTRUMENT_BASE_INDEX) {
				source_mask = g_hud_only_view_inset_span_mask;
			} else if (hud_instrument_base_index ==
				   HUD_CRAFT_LIST_INSTRUMENT_BASE_INDEX) {
				source_mask = g_hud_craft_list_inset_span_mask;
			} else {
				source_mask = g_hud_cockpit_inset_span_mask;
			}
		} else {
			source_mask = g_hud_cockpit_inset_span_mask;
		}
		uint16_t mask_index;
		if (g_flight_resolution_mode == FLIGHT_RESOLUTION_320X240) {
			for (mask_index = LOW_RESOLUTION_MASK_SIZE;
			     mask_index != 0; --mask_index) {
				destination_mask[mask_index - 1] =
					source_mask[mask_index - 1];
			}
		} else {
			for (mask_index = HIGH_RESOLUTION_MASK_SIZE;
			     mask_index != 0; --mask_index) {
				destination_mask[mask_index - 1] =
					source_mask[mask_index - 1];
			}
		}
	}

	if (g_use_hardware3d != 0) {
		std3d_fill_z_buffer_from_viewport_mask();
	}
	hud_point_camera(g_players[g_local_player].current_target_object_idx, 1,
			 g_local_player);
#ifdef XVT_MODERN
	xvt_render_capture_crt(screen_x, screen_y, width, height,
			       refresh_span_mask);
#endif
	if (g_players[g_local_player].target_box_enabled != 0) {
		g_render_object_ref |= TARGET_BOX_RENDER_FLAG;
	} else {
		g_render_object_ref |= TARGET_CROSS_RENDER_FLAG;
	}
	g_flight_background_color_index = CRT_RENDER_COLOR;
	render_scene_initialize(1);
	g_scene_billboard_queue_count = 0;
	g_cam_rel_world_x = g_world_loc_x -
			    g_players[g_local_player].view_state.camera_world_x;
	g_cam_rel_world_y = g_world_loc_y -
			    g_players[g_local_player].view_state.camera_world_y;
	g_cam_rel_world_z = g_world_loc_z -
			    g_players[g_local_player].view_state.camera_world_z;

	int component_rel_x;
	int component_rel_y;
	int component_rel_z;
	uint16_t target_object_idx;
	if (g_players[g_local_player].target_box_enabled != 0) {
		target_object_idx = (uint16_t)g_players[g_local_player]
					    .current_target_object_idx;
		if (target_object_idx < g_active_region_craft_object_slot_end) {
			g_rotated_x = model_mesh_get_component_focus_x(
				g_object_table
					[(uint16_t)g_players[g_local_player]
						 .current_target_object_idx]
						.object_type,
				(uint16_t)g_players[g_local_player]
					.selected_target_component);
			g_rotated_y = model_mesh_get_component_focus_z(
				g_object_table
					[(uint16_t)g_players[g_local_player]
						 .current_target_object_idx]
						.object_type,
				(uint16_t)g_players[g_local_player]
					.selected_target_component);
			g_rotated_z = -model_mesh_get_component_focus_y(
				g_object_table
					[(uint16_t)g_players[g_local_player]
						 .current_target_object_idx]
						.object_type,
				(uint16_t)g_players[g_local_player]
					.selected_target_component);
			pai_rotate_local_vector_to_world_scratch(
				&g_object_table
					[(uint16_t)g_players[g_local_player]
						 .current_target_object_idx],
				g_rotated_x, g_rotated_y, g_rotated_z);
			component_rel_x = g_rotated_x + g_world_loc_x -
					  g_players[g_local_player]
						  .view_state.camera_world_x;
			component_rel_y = g_rotated_y + g_world_loc_y -
					  g_players[g_local_player]
						  .view_state.camera_world_y;
			component_rel_z = g_rotated_z + g_world_loc_z -
					  g_players[g_local_player]
						  .view_state.camera_world_z;
		}
	}

	g_view_space_x = transfm2_cam_mat_dot_row0(
		g_cam_rel_world_x, g_cam_rel_world_y, g_cam_rel_world_z);
	g_view_space_y = transfm2_cam_mat_dot_row1(
		g_cam_rel_world_x, g_cam_rel_world_y, g_cam_rel_world_z);
	g_view_space_depth = transfm2_cam_mat_dot_row2(
		g_cam_rel_world_x, g_cam_rel_world_y, g_cam_rel_world_z);
	target_object_idx =
		(uint16_t)g_players[g_local_player].current_target_object_idx;
	struct object_record *target_object =
		&g_object_table[target_object_idx];
	struct mobile_object *target_mobile_object = target_object->mobj;
	if (target_mobile_object != NULL) {
		switch (target_object->genus_id) {
		case CRAFT_GENUS_STARFIGHTER:
		case CRAFT_GENUS_TRANSPORT:
		case CRAFT_GENUS_UTILITY_VEHICLE:
		case CRAFT_GENUS_FREIGHTER:
		case CRAFT_GENUS_STARSHIP:
		case CRAFT_GENUS_PLATFORM:
			g_cur_craft = target_mobile_object->p_craft;
			fview_set_object_transform(
				target_object->roll, target_object->pitch,
				target_object->yaw, 0,
				&g_object_table
					[(uint16_t)g_players[g_local_player]
						 .current_target_object_idx]);
			flight_light_setup_object_lighting_by_index(
				(uint16_t)g_players[g_local_player]
					.current_target_object_idx);
			damage_queue_craft_billboards(
				g_players[g_local_player]
					.current_target_object_idx);
			render_scene_draw_object_model(
				&g_object_table
					[(uint16_t)g_players[g_local_player]
						 .current_target_object_idx]);
			break;
		case CRAFT_GENUS_PLAYER_PROJECTILE:
		case CRAFT_GENUS_OTHER_PROJECTILE:
			fview_set_object_transform(
				target_object->roll, target_object->pitch,
				target_object->yaw, 0,
				&g_object_table
					[(uint16_t)g_players[g_local_player]
						 .current_target_object_idx]);
			scene_billboard_draw_roll_aligned_object_model(
				g_players[g_local_player]
					.current_target_object_idx);
			break;
		default:
			break;
		}
	} else if (target_object->genus_id >= CRAFT_GENUS_MINE &&
		   target_object->genus_id <= CRAFT_GENUS_SMALL_DEBRIS) {
		fview_set_object_transform(target_object->roll,
					   target_object->pitch,
					   target_object->yaw, 0, NULL);
		g_transform_light_direction_to_object_space = 1;
		render_non_craft_scene_object(target_object_idx);
	}

	for (uint16_t object_index = 0;
	     object_index < g_region_main_object_slot_end; ++object_index) {
		if (object_index == g_local_transient_slot_start &&
		    (g_debris_enabled == 0 ||
		     g_flight_mission_state.proving_grounds_mode_active != 0)) {
			object_index = (uint16_t)g_local_debris_slot_end;
			if (object_index == g_region_main_object_slot_end) {
				break;
			}
		}
		if (g_players[g_local_player].view_state.camera_focus_obj_idx !=
			    object_index ||
		    g_players[g_local_player]
				    .view_state.external_camera_active != 0 ||
		    g_replay_view_mode != 0) {
			int object_table_index = object_index;
			struct object_record *object =
				&g_object_table[object_table_index];
			uint16_t object_type =
				g_object_table[object_table_index].object_type;
			if (object_type != 0) {
				g_current_object_bounds_extent =
					g_object_type_table[object_type]
						.max_bounds_extent;
				int genus_id = object->genus_id;
				if (genus_id >= CRAFT_GENUS_PLAYER_PROJECTILE) {
					if (object->genus_id <=
					    CRAFT_GENUS_OTHER_PROJECTILE) {
						if (g_players[g_local_player]
							    .map_camera_state !=
						    0) {
							if (object->mobj->source_obj_idx !=
								    g_players[g_local_player]
									    .current_target_object_idx ||
							    flight_view_project_and_test_sphere_visible(
								    object_index,
								    g_current_object_bounds_extent) ==
								    0) {
								continue;
							}
						} else if (
							(g_players[g_local_player]
									 .current_target_object_idx !=
								 object->mobj
									 ->source_obj_idx &&
							 g_players[g_local_player]
									 .object_index !=
								 object->mobj
									 ->source_obj_idx) ||
							flight_view_project_and_test_sphere_visible(
								object_index,
								g_current_object_bounds_extent) ==
								0) {
							continue;
						}
						fview_set_object_transform(
							g_object_table
								[object_table_index]
									.roll,
							g_object_table
								[object_table_index]
									.pitch,
							g_object_table
								[object_table_index]
									.yaw,
							0,
							&g_object_table
								[object_table_index]);
						scene_billboard_draw_roll_aligned_object_model(
							object_index);
					} else if (
						genus_id ==
							CRAFT_GENUS_EXPLOSION &&
						flight_view_project_and_test_sphere_visible(
							object_index,
							g_current_object_bounds_extent) !=
							0) {
						fview_set_object_transform(
							g_object_table
								[object_table_index]
									.roll,
							g_object_table
								[object_table_index]
									.pitch,
							g_object_table
								[object_table_index]
									.yaw,
							0,
							&g_object_table
								[object_table_index]);
						scene_billboard_draw_or_queue_object(
							object_index);
					}
				}
			}
		}
	}

	sw3d_draw_visible_faces_to_surface();
	if (g_scene_billboard_queue_count != 0) {
		g_flight_sw_rot_sprite_coeff_cache_valid = 0;
		scene_billboard_render_queued_textured(0);
		g_flight_sw_rot_sprite_coeff_cache_valid = 0;
	}
	if (g_players[g_local_player].target_box_enabled != 0) {
		target_object_idx = (uint16_t)g_players[g_local_player]
					    .current_target_object_idx;
		if (target_object_idx < g_active_region_craft_object_slot_end &&
		    g_object_table[target_object_idx].genus_id !=
			    CRAFT_GENUS_STARFIGHTER) {
			g_view_space_x = transfm2_cam_mat_dot_row0(
				component_rel_x, component_rel_y,
				component_rel_z);
#ifdef XVT_MODERN
			xvt_render_capture_crt_marker(component_rel_x,
						      component_rel_y,
						      component_rel_z);
#endif
			g_view_space_y = transfm2_cam_mat_dot_row1(
				component_rel_x, component_rel_y,
				component_rel_z);
			g_view_space_depth = transfm2_cam_mat_dot_row2(
				component_rel_x, component_rel_y,
				component_rel_z);
			int marker_x = transfm2_project_screen_x(
				g_view_space_x, g_view_space_depth);
			int marker_y = transfm2_project_screen_y(
				g_view_space_y, g_view_space_depth);
			hud_draw_component_marker_box(
				marker_x - COMPONENT_MARKER_HALF_SIZE,
				marker_y - COMPONENT_MARKER_HALF_SIZE,
				COMPONENT_MARKER_SIZE, COMPONENT_MARKER_SIZE,
				COMPONENT_MARKER_COLOR);
		}
	}

	render_scene_unlock_buffers();
	g_flight_background_color_index = g_flight_transparent_color_index;
	pop_flight_viewport();
	g_render_object_ref = saved_render_object_ref;
	g_players[g_local_player].view_state.camera_world_x =
		saved_camera_world_x;
	g_players[g_local_player].view_state.camera_world_y =
		saved_camera_world_y;
	g_players[g_local_player].view_state.camera_world_z =
		saved_camera_world_z;
	g_proj_offset_y = saved_proj_offset_y;
#ifdef XVT_MODERN
	xvt_render_draw_scope(XVT_SCOPE_COCKPIT);
#endif
}

/* Draws box corners at depth 1 through hud_draw_depth_tested_box_corners. */
// FUNCTION: XVT 0x442BC0
void hud_draw_component_marker_box(int x, int y, int width, int height,
				   uint8_t color_idx)
{
	enum { COMPONENT_MARKER_DEPTH = 1 };

	hud_draw_depth_tested_box_corners(x, y, width, height, color_idx,
					  COMPONENT_MARKER_DEPTH);
}

/* Places player_idx's camera to look at target_idx, an object or mission point:
 * turns it toward the target from the map camera or the player's craft
 * (fview_build_camera_orient), then sets viewState.camera_world_x, Y and Z back
 * from the target along the view by a distance in proportion to its size, the
 * mean of its model's two largest bounds or its type's max_bounds_extent, divided
 * by the inset layout's color_index_or_widget_param when use_hud_layout_scale is set,
 * else by 60, 100 or 144 for the player's resolution, and a quarter more at
 * 640x480. Writes g_world_loc_x, Y and Z, the camera matrix and the trig2_ctop
 * results. */
// FUNCTION: XVT 0x442BF0
void hud_point_camera(uint16_t target_idx, int16_t use_hud_layout_scale,
		      int player_idx)
{
	enum {
		CAMERA_SCALE_LIMIT = 0x3FFF,
		CAMERA_AIM_CENTER_Q16 = 0x4000,
		CAMERA_DIVISOR_LOW = 60,
		CAMERA_DIVISOR_HIGH = 100,
		CAMERA_DIVISOR_DEFAULT = 144,
		CAMERA_OFFSET_COUNT = 3,
	};

	mission_resolve_object_or_mission_point_world_loc(target_idx, 0);
	int delta_x;
	int delta_y;
	int delta_z;
	if (g_players[player_idx].map_camera_state != 0) {
		delta_x = (int32_t)((uint32_t)g_world_loc_x -
				    (uint32_t)g_players[player_idx]
					    .view_state.camera_world_x);
		delta_y = (int32_t)((uint32_t)g_world_loc_y -
				    (uint32_t)g_players[player_idx]
					    .view_state.camera_world_y);
		delta_z = (int32_t)((uint32_t)g_world_loc_z -
				    (uint32_t)g_players[player_idx]
					    .view_state.camera_world_z);
	} else {
		struct object_record *player_object =
			&g_object_table[g_players[player_idx].object_index];
		delta_x = (int32_t)((uint32_t)g_world_loc_x -
				    (uint32_t)player_object->world_x);
		delta_y = (int32_t)((uint32_t)g_world_loc_y -
				    (uint32_t)player_object->world_y);
		delta_z = (int32_t)((uint32_t)g_world_loc_z -
				    (uint32_t)player_object->world_z);
	}
	int scaled_x = (int32_t)((uint32_t)delta_x * 2u);
	int scaled_y = (int32_t)((uint32_t)delta_y * 2u);
	int scaled_z;
	{
		int16_t high_y = (int16_t)(scaled_y >> 16);
		scaled_z = (int32_t)((uint32_t)delta_z * 2u);
		int16_t high_x = (int16_t)(scaled_x >> 16);
		int16_t high_z = (int16_t)(scaled_z >> 16);
		if ((high_x & 0x8000) != 0) {
			high_x = (int16_t)-high_x;
		}
		if ((high_y & 0x8000) != 0) {
			high_y = (int16_t)-high_y;
		}
		if ((high_z & 0x8000) != 0) {
			high_z = (int16_t)-high_z;
		}
		do {
			high_x = (int16_t)((uint16_t)high_x >> 1);
			scaled_x >>= 1;
			scaled_y >>= 1;
			scaled_z >>= 1;
			high_y = (int16_t)((uint16_t)high_y >> 1);
			high_z = (int16_t)((uint16_t)high_z >> 1);
		} while (high_x != 0 || high_y != 0 || high_z != 0);
	}
	scaled_x >>= 1;
	scaled_y >>= 1;
	scaled_z >>= 1;

	int camera_x;
	int camera_y;
	int camera_z;
	if (g_players[player_idx].map_camera_state != 0) {
		fview_build_camera_orient(
			g_players[player_idx].view_state.view_roll,
			g_players[player_idx].view_state.view_pitch,
			g_players[player_idx].view_state.view_yaw, 0, 0, 0,
			NULL);
		camera_x = math_dot3q15_wrapped(
			(int16_t)scaled_x, (int16_t)scaled_y, (int16_t)scaled_z,
			g_cam_mat_r0_x, g_cam_mat_r0_y, g_cam_mat_r0_z);
		camera_y = math_dot3q15_wrapped(
			(int16_t)scaled_x, (int16_t)scaled_y, (int16_t)scaled_z,
			g_cam_mat_r2_x, g_cam_mat_r2_y, g_cam_mat_r2_z);
		camera_z = math_dot3q15_wrapped(
			(int16_t)scaled_x, (int16_t)scaled_y, (int16_t)scaled_z,
			g_cam_mat_r1_x, g_cam_mat_r1_y, g_cam_mat_r1_z);
		trig2_ctop(camera_x, camera_y, camera_z);
		fview_build_camera_orient(
			g_players[player_idx].view_state.view_roll,
			g_players[player_idx].view_state.view_pitch,
			g_players[player_idx].view_state.view_yaw, 0,
			(int16_t)(CAMERA_AIM_CENTER_Q16 - trig2_pitch),
			trig2_xyangle, NULL);
	} else {
		struct object_record *player_object =
			&g_object_table[g_players[player_idx].object_index];
		if (player_object->mobj->orient_matrix_dirty != 0) {
			fview_calcrotatemove(
				player_object->pitch, player_object->yaw,
				&g_object_table[g_players[player_idx]
							.object_index]);
			fview_calcrotateorient(
				g_object_table[g_players[player_idx]
						       .object_index]
					.roll,
				0,
				&g_object_table[g_players[player_idx]
							.object_index]);
		}
		camera_x = math_dot3q15_wrapped(
			(int16_t)scaled_x, (int16_t)scaled_y, (int16_t)scaled_z,
			g_object_table[g_players[player_idx].object_index]
				.mobj->cached_side_x,
			g_object_table[g_players[player_idx].object_index]
				.mobj->cached_side_y,
			g_object_table[g_players[player_idx].object_index]
				.mobj->cached_side_z);
		camera_y = math_dot3q15_wrapped(
			(int16_t)scaled_x, (int16_t)scaled_y, (int16_t)scaled_z,
			g_object_table[g_players[player_idx].object_index]
				.mobj->cached_fwd_x,
			g_object_table[g_players[player_idx].object_index]
				.mobj->cached_fwd_y,
			g_object_table[g_players[player_idx].object_index]
				.mobj->cached_fwd_z);
		camera_z = math_dot3q15_wrapped(
			(int16_t)scaled_x, (int16_t)scaled_y, (int16_t)scaled_z,
			g_object_table[g_players[player_idx].object_index]
				.mobj->cached_up_x,
			g_object_table[g_players[player_idx].object_index]
				.mobj->cached_up_y,
			g_object_table[g_players[player_idx].object_index]
				.mobj->cached_up_z);
		trig2_ctop(camera_x, camera_y, camera_z);
		fview_build_camera_orient(
			g_object_table[g_players[player_idx].object_index].roll,
			g_object_table[g_players[player_idx].object_index]
				.pitch,
			g_object_table[g_players[player_idx].object_index].yaw,
			0, (int16_t)(CAMERA_AIM_CENTER_Q16 - trig2_pitch),
			trig2_xyangle, NULL);
	}

	int max_extent;
	{
		struct object_record *target = &g_object_table[target_idx];
		if (target->mobj != NULL && target->mobj->p_craft != NULL) {
			model_index model_index =
				target->mobj->p_craft->model_index;
			int16_t bound_y =
				g_model_defs[model_index].bound_size_y;
			int16_t bound_x =
				g_model_defs[model_index].bound_size_x;
			int largest_a;
			int largest_b;
			if (bound_x <= bound_y &&
			    bound_x <= g_model_defs[model_index].bound_size_z) {
				largest_a = bound_y;
				largest_b =
					g_model_defs[model_index].bound_size_z;
			} else if (bound_x >= bound_y &&
				   g_model_defs[model_index].bound_size_z >=
					   bound_y) {
				largest_a = bound_x;
				largest_b =
					g_model_defs[model_index].bound_size_z;
			} else {
				largest_a = bound_x;
				largest_b = bound_y;
			}
			max_extent =
				(int)((unsigned int)(largest_a + largest_b) >>
				      1)
				<< g_model_defs[model_index].bound_size_shift;
		} else {
			max_extent = g_object_type_table[target->object_type]
					     .max_bounds_extent;
		}
	}
	uint16_t camera_divisor;
	uint16_t resolution_mode;
	if (use_hud_layout_scale != 0) {
		camera_divisor =
			g_hud_element_layouts[g_hud_instrument_set_base_index +
					      2]
				.color_index_or_widget_param;
	} else {
		resolution_mode =
			g_players[player_idx].network.flight_resolution_mode;
		camera_divisor =
			resolution_mode == FLIGHT_RESOLUTION_320X240
				? CAMERA_DIVISOR_LOW
				: (resolution_mode == FLIGHT_RESOLUTION_480X360
					   ? CAMERA_DIVISOR_HIGH
					   : CAMERA_DIVISOR_DEFAULT);
	}
	resolution_mode = g_players[player_idx].network.flight_resolution_mode;
	uint16_t scale_shift = (resolution_mode == FLIGHT_RESOLUTION_640X480 ||
				resolution_mode == FLIGHT_RESOLUTION_480X360)
				       ? 9
				       : 8;
	unsigned int scale = ((unsigned int)max_extent << scale_shift) /
			     (unsigned int)camera_divisor;
	scale_shift = 0;
	while (scale > CAMERA_SCALE_LIMIT) {
		scale >>= 1;
		++scale_shift;
	}
	if (resolution_mode == FLIGHT_RESOLUTION_640X480) {
		scale = ((uint16_t)scale >> 2) + (uint16_t)scale;
	}
	scale = (uint16_t)scale;
	g_players[player_idx].view_state.camera_world_x =
		math_mul_q15((int)scale, g_cam_mat_r2_x);
	g_players[player_idx].view_state.camera_world_y =
		math_mul_q15((int)scale, g_cam_mat_r2_y);
	g_players[player_idx].view_state.camera_world_z =
		math_mul_q15((int)scale, g_cam_mat_r2_z);
	if (scale_shift != 0) {
		g_players[player_idx].view_state.camera_world_x =
			(int32_t)((uint32_t)g_players[player_idx]
					  .view_state.camera_world_x
				  << scale_shift);
		g_players[player_idx].view_state.camera_world_y =
			(int32_t)((uint32_t)g_players[player_idx]
					  .view_state.camera_world_y
				  << scale_shift);
		g_players[player_idx].view_state.camera_world_z =
			(int32_t)((uint32_t)g_players[player_idx]
					  .view_state.camera_world_z
				  << scale_shift);
	}
	g_players[player_idx].view_state.camera_world_x =
		(int32_t)((uint32_t)g_world_loc_x -
			  (uint32_t)g_players[player_idx]
				  .view_state.camera_world_x);
	g_players[player_idx].view_state.camera_world_y =
		(int32_t)((uint32_t)g_world_loc_y -
			  (uint32_t)g_players[player_idx]
				  .view_state.camera_world_y);
	g_players[player_idx].view_state.camera_world_z =
		(int32_t)((uint32_t)g_world_loc_z -
			  (uint32_t)g_players[player_idx]
				  .view_state.camera_world_z);
}

/* Resets the three message panes. The first time in a flight (while
 * g_ready_message_pane_left is -1) it sets the ready, system and flight group pane
 * rectangles for the resolution and clears them on g_flight_offscreen_buffer;
 * later calls with force_expire_active_messages set raise
 * g_flight_message_panes_force_expire instead. Then it redraws the craft name and
 * status line, empties the three panes and the ready queue, sets
 * g_radio_message_backup_enabled to 0, g_target_description_message_id to 331 (the
 * blank message) and g_unused_ready_message_pane_initial_state to slot 0's id. The
 * modern build resets its message capture. */
// FUNCTION: XVT 0x450260
void hud_reset_flight_message_panes(int force_expire_active_messages)
{

	XVT_LOG_DEBUG(
		"hud.panes_reset setup=%d expire=%d waiting=%u ready_message=%u system_message=%u group_message=%u predicted=%d",
		g_ready_message_pane_left == -1, force_expire_active_messages,
		(unsigned)g_ready_message_queue_count,
		(unsigned)g_ready_message_pane_queue[0].state_or_message_id,
		(unsigned)g_system_message_pane.state_or_message_id,
		(unsigned)g_flight_group_message_pane.state_or_message_id,
		g_flight_sim_side_effects_suppressed);
	if (g_ready_message_pane_left == -1) {
		switch (g_flight_resolution_mode) {
		case FLIGHT_RESOLUTION_320X240:
			g_ready_message_pane_left = 42;
			g_ready_message_pane_right = 277;
			g_system_message_pane_left = 42;
			g_system_message_pane_right = 277;
			g_ready_message_pane_top = 3;
			g_ready_message_pane_bottom = 26;
			g_system_message_pane_top = 32;
			g_system_message_pane_bottom = 37;
			g_flight_group_message_pane_left = 75;
			g_flight_group_message_pane_top = 44;
			g_flight_group_message_pane_right = 265;
			g_flight_group_message_pane_bottom = 49;
			break;
		case FLIGHT_RESOLUTION_640X480:
			g_ready_message_pane_top = 6;
			g_ready_message_pane_right = 505;
			g_ready_message_pane_bottom = 53;
			g_system_message_pane_top = 64;
			g_system_message_pane_right = 555;
			g_system_message_pane_bottom = 75;
			g_flight_group_message_pane_left = 150;
			g_flight_group_message_pane_top = 89;
			g_flight_group_message_pane_right = 530;
			g_flight_group_message_pane_bottom = 100;
			g_ready_message_pane_left = 85;
			g_system_message_pane_left = 85;
			break;
		case FLIGHT_RESOLUTION_480X360:
			g_ready_message_pane_top = 4;
			g_ready_message_pane_right = 378;
			g_ready_message_pane_bottom = 39;
			g_system_message_pane_top = 48;
			g_system_message_pane_right = 415;
			g_system_message_pane_bottom = 56;
			g_flight_group_message_pane_left = 112;
			g_flight_group_message_pane_top = 66;
			g_flight_group_message_pane_right = 397;
			g_flight_group_message_pane_bottom = 74;
			g_ready_message_pane_left = 63;
			g_system_message_pane_left = 63;
			break;
		}

		flight_sw_set_render_target(g_flight_offscreen_buffer,
					    g_screen_width, g_screen_height,
					    g_screen_width *
						    g_flight_bytes_per_pixel);
		flight_text_set_background_color(
			g_flight_transparent_color_index);
		flight_text_set_clip_rect(g_ready_message_pane_left,
					  g_ready_message_pane_top,
					  g_ready_message_pane_right,
					  g_ready_message_pane_bottom);
		g_flight_fill_clip_rect_fn();
		flight_text_set_clip_rect(g_system_message_pane_left,
					  g_system_message_pane_top,
					  g_system_message_pane_right,
					  g_system_message_pane_bottom);
		g_flight_fill_clip_rect_fn();
		flight_text_set_clip_rect(g_flight_group_message_pane_left,
					  g_flight_group_message_pane_top,
					  g_flight_group_message_pane_right,
					  g_flight_group_message_pane_bottom);
		g_flight_fill_clip_rect_fn();
	} else if (force_expire_active_messages != 0) {
		++g_flight_message_panes_force_expire;
	}

	flight_surface_lock();
	flight_sw_set_render_target(NULL, 320, 200, 0);
	flight_text_set_word_wrap(0);
	flight_text_set_clear_line_background(0);
	flight_text_set_font_tier(1);
	hud_draw_craft_name_fps_and_network_status();
	flight_text_set_color(0x43);
	g_unused_ready_message_pane_initial_state =
		g_ready_message_pane_queue[0].state_or_message_id;
	g_radio_message_backup_enabled = 0;
	g_ready_message_queue_count = 0;
	g_target_description_message_id = 331;
	g_ready_message_pane_queue[0].state_or_message_id = UINT16_MAX;
	g_system_message_pane.state_or_message_id = UINT16_MAX;
	g_flight_group_message_pane.state_or_message_id = UINT16_MAX;
	flight_surface_unlock();
#ifdef XVT_MODERN
	xvt_cockpit_messages_reset_working();
#endif
}

/* Makes room in slot 0 of g_ready_message_pane_queue for a new message: when the
 * message there has been shown fewer than 2 times and is under a simulated
 * second old, moves every entry up one, so it waits behind the new one, and
 * raises g_ready_message_queue_count, at most 9. Otherwise does nothing and the
 * message in slot 0 is overwritten. */
// FUNCTION: XVT 0x450BC0
void hud_shift_ready_message_queue_for_replacement(void)
{
	if (g_ready_message_pane_queue[0].show_count < 2 &&
	    g_ready_message_pane_queue[0].age_seconds == 0) {
		XVT_LOG_DEBUG(
			"hud.ready_message_deferred message=%u shown=%u waiting=%u predicted=%d",
			(unsigned)g_ready_message_pane_queue[0]
				.state_or_message_id,
			(unsigned)g_ready_message_pane_queue[0].show_count,
			(unsigned)g_ready_message_queue_count,
			g_flight_sim_side_effects_suppressed);
		uint8_t old_pending_count = g_ready_message_queue_count;
		uint16_t destination_index = old_pending_count + 1;
		if (destination_index != 0) {
			do {
				g_ready_message_pane_queue[destination_index] =
					g_ready_message_pane_queue
						[destination_index - 1];
				destination_index--;
			} while (destination_index != 0);
		}

		uint8_t new_pending_count = old_pending_count + 1;
		g_ready_message_queue_count = new_pending_count;
		if (new_pending_count >= 10) {
			g_ready_message_queue_count = new_pending_count - 1;
			XVT_LOG_WARN(
				"hud.ready_queue_full message=%u waiting=%u",
				(unsigned)g_ready_message_pane_queue[10]
					.state_or_message_id,
				(unsigned)g_ready_message_queue_count);
		}
	} else {
		XVT_LOG_DEBUG(
			"hud.ready_message_replaced message=%u shown=%u age=%u predicted=%d",
			(unsigned)g_ready_message_pane_queue[0]
				.state_or_message_id,
			(unsigned)g_ready_message_pane_queue[0].show_count,
			(unsigned)g_ready_message_pane_queue[0].age_seconds,
			g_flight_sim_side_effects_suppressed);
	}
}

/* Moves the waiting messages of g_ready_message_pane_queue down one, so the next
 * one is in slot 0, and lowers g_ready_message_queue_count. Does not check for an
 * empty queue, where the count wraps to 255. */
// FUNCTION: XVT 0x450C30
void hud_advance_ready_message_queue(void)
{
	uint8_t old_pending_count = g_ready_message_queue_count;

	if (old_pending_count != 0) {
		uint16_t destination_index = 0;
		do {
			g_ready_message_pane_queue[destination_index] =
				g_ready_message_pane_queue[destination_index +
							   1];
			++destination_index;
		} while (destination_index < old_pending_count);
	}

	g_ready_message_queue_count = old_pending_count - 1;
}

/* Draws the message of pane pane_type onto g_flight_offscreen_buffer: types 3, 4
 * and 7 in the system pane, 8 in the flight group pane, others in the ready
 * pane from slot 0. Returns at once when slot 0 is empty and the type is not a
 * system or flight group one. Plays slot 0's voice when it has one, has not
 * been shown and the voice option is on, whichever pane is drawn. The system
 * and flight group panes are cleared, their text centered in font tier 1 and
 * their state set to 1. For the ready pane, message 374 plays
 * FLIGHT_SOUND_MESSAGE_READY, and while the message log page is open the
 * message is not drawn, only finished and counted as shown; else
 * hud_setup_ready_message_pane_text.
 *
 * A first byte below 9 picks the color from g_message_text_prefix_color_codes; type
 * 1 followed by a digit 0 to 3 takes entries 8 to 11 instead, and type 2 the
 * color of the sender's IFF; any other start is color 0x42. Up to 70 characters
 * are drawn; "[" and "]" step g_flight_text_color_index to highlight and back, and
 * a 0xFE escape and its byte are skipped. Then hud_finish_flight_message_pane and
 * the pane's show_count rises. */
// FUNCTION: XVT 0x450C90
void hud_show_flight_message_pane(int16_t pane_type)
{

	char last_char;

#ifdef XVT_MODERN
	last_char = 0;
#endif

	if (g_ready_message_pane_queue[0].state_or_message_id == UINT16_MAX &&
	    pane_type != 3 && pane_type != 8 && pane_type != 4 &&
	    pane_type != 7) {
		return;
	}

	flight_sw_set_render_target(g_flight_offscreen_buffer, g_screen_width,
				    g_screen_height,
				    g_flight_bytes_per_pixel * g_screen_width);
	if (g_ready_message_pane_queue[0].voice_sfx_id != 0 &&
	    g_ready_message_pane_queue[0].show_count == 0 &&
	    g_game_config.voice_special_enabled != 0) {
		fsfx_queue_voice_sfx(g_ready_message_pane_queue[0].voice_sfx_id,
				     0, 0, 0, 0xFFFFu);
	}

	char *text;
	uint16_t pane_width;
	uint16_t text_width;
	if (pane_type == 3 || pane_type == 4 || pane_type == 7) {
		flight_text_set_font_tier(1);
		text = g_system_message_pane.text;
		g_system_message_pane.state_or_message_id = 1;
		flight_text_set_clip_rect(g_system_message_pane_left,
					  g_system_message_pane_top,
					  g_system_message_pane_right,
					  g_system_message_pane_bottom);
		pane_width = g_system_message_pane_right -
			     g_system_message_pane_left;
		flight_text_set_background_color(
			g_flight_transparent_color_index);
		g_flight_fill_clip_rect_fn();
		pane_width >>= 1;
		text_width = hud_measure_flight_message_pane_text(pane_type);
		text_width >>= 1;
		pane_width -= text_width;
		flight_text_set_cursor(g_system_message_pane_left + pane_width,
				       g_system_message_pane_top);
	} else if (pane_type == 8) {
		flight_text_set_font_tier(1);
		text = g_flight_group_message_pane.text;
		g_flight_group_message_pane.state_or_message_id = 1;
		flight_text_set_clip_rect(g_flight_group_message_pane_left,
					  g_flight_group_message_pane_top,
					  g_flight_group_message_pane_right,
					  g_flight_group_message_pane_bottom);
		pane_width = g_flight_group_message_pane_right -
			     g_flight_group_message_pane_left;
		flight_text_set_background_color(
			g_flight_transparent_color_index);
		g_flight_fill_clip_rect_fn();
		pane_width >>= 1;
		text_width = hud_measure_flight_message_pane_text(pane_type);
		text_width >>= 1;
		pane_width -= text_width;
		flight_text_set_cursor(g_flight_group_message_pane_left +
					       pane_width,
				       g_flight_group_message_pane_top);
	} else {
		if (g_ready_message_pane_queue[0].state_or_message_id == 374) {
			fsfx_play_sound(FLIGHT_SOUND_MESSAGE_READY, -1,
					g_local_player);
		}
		if (g_mfd_page_states[MFD_PAGE_MESSAGE_LOG] !=
		    MFD_PAGE_STATE_CLOSED) {
			hud_finish_flight_message_pane(
				g_ready_message_pane_queue[0].pane_type,
				last_char);
			++g_ready_message_pane_queue[0].show_count;
			flight_sw_set_render_target(NULL, 320, 200, 0);
			return;
		}
		text = g_ready_message_pane_queue[0].text;
		hud_setup_ready_message_pane_text();
	}

#ifdef XVT_MODERN
	xvt_cockpit_messages_begin_message(pane_type);
#endif
	uint8_t prefix = (uint8_t)*text;
	uint8_t current_char;
	if (prefix < 9) {
		++text;
		flight_text_set_color(
			g_message_text_prefix_color_codes[prefix]);
		if (prefix == 1) {
			current_char = (uint8_t)*text;
			if (current_char >= '0' && current_char <= '3') {
				++text;
				flight_text_set_color(
					g_message_text_prefix_color_codes
						[current_char - '(']);
			}
		} else if (prefix == 2) {
			flight_text_set_color(
				g_message_sender_iff_color_codes
					[g_ready_message_pane_queue[0]
						 .sender_iff]);
		}
	} else {
		flight_text_set_color(0x42);
	}

	uint16_t visible_chars = 0;
	while (*text != '\0' && visible_chars < 70) {
		current_char = (uint8_t)*text;
		if (current_char == '[') {
			if (g_flight_text_color_index == 0xD4) {
				--g_flight_text_color_index;
			} else {
				++g_flight_text_color_index;
			}
			++text;
		} else if (current_char == ']') {
			if (g_flight_text_color_index == 0xD3) {
				++g_flight_text_color_index;
			} else {
				--g_flight_text_color_index;
			}
			++text;
		} else if (current_char == 0xFE) {
			text += 2;
		} else {
			++visible_chars;
			g_flight_draw_char_fn(current_char);
			last_char = *text;
			++text;
		}
	}

#ifdef XVT_MODERN
	xvt_cockpit_messages_record_reveal(visible_chars);
#endif
	if (prefix == 3 || prefix == 4 || prefix == 7) {
		hud_finish_flight_message_pane(g_system_message_pane.pane_type,
					       last_char);
		++g_system_message_pane.show_count;
	} else if (prefix == 8) {
		hud_finish_flight_message_pane(
			g_flight_group_message_pane.pane_type, last_char);
		++g_flight_group_message_pane.show_count;
	} else {
		hud_finish_flight_message_pane(
			g_ready_message_pane_queue[0].pane_type, last_char);
		++g_ready_message_pane_queue[0].show_count;
	}
	flight_sw_set_render_target(NULL, 320, 200, 0);
}

/* Prepares to draw the ready pane: font tier 1, a cleared pane with the
 * transparent background, shadow on in color 0x40, color 0x43, and the cursor
 * where slot 0's text is centered. Sets g_flight_text_shadow_enabled to 1. */
// FUNCTION: XVT 0x451060
void hud_setup_ready_message_pane_text(void)
{
	flight_text_set_font_tier(1);
	flight_text_set_background_color(g_flight_transparent_color_index);
	g_flight_text_shadow_enabled = 1;
	flight_text_set_shadow_color(0x40);
	flight_text_set_clip_rect(
		g_ready_message_pane_left, g_ready_message_pane_top,
		g_ready_message_pane_right, g_ready_message_pane_bottom);
	g_flight_fill_clip_rect_fn();
	uint16_t pane_width =
		g_ready_message_pane_right - g_ready_message_pane_left;
	uint16_t text_width = hud_measure_flight_message_pane_text(0);
	pane_width >>= 1;
	text_width >>= 1;
	pane_width -= text_width;
	flight_text_set_cursor(g_ready_message_pane_left + pane_width,
			       g_ready_message_pane_top);
	flight_text_set_color(0x43);
}

/* Ends a drawn message: while the message log page is closed it adds "." unless
 * the text ended in "?", "!", ":", " " or ".", and clears the rest of the line.
 * Then sets how long the pane stays, in ticks of g_player_flight_transient_timers:
 * the system pane 472 for types 3 and 7 and 1,888 for type 4; the flight group
 * pane 1,888; the ready pane 354 while messages wait, else 1,416 for types 1
 * and 2 and 1,652 for others. Redraws the craft name and status line and leaves
 * font tier 2. */
// FUNCTION: XVT 0x451100
void hud_finish_flight_message_pane(int16_t pane_type, char last_char)
{
	if (g_mfd_page_states[MFD_PAGE_MESSAGE_LOG] == MFD_PAGE_STATE_CLOSED) {
		if (last_char != '?' && last_char != '!' && last_char != ':' &&
		    last_char != ' ' && last_char != '.') {
			g_flight_draw_char_fn('.');
		}
		flight_text_set_clear_line_background(1);
		g_flight_draw_char_fn('\n');
		flight_text_set_clear_line_background(0);
	}
	if (pane_type == 3 || pane_type == 7) {
		g_player_flight_transient_timers[g_local_player]
			.system_message_pane_timer = 472;
	} else if (pane_type == 8) {
		g_player_flight_transient_timers[g_local_player]
			.flight_group_message_pane_timer = 1888;
	} else if (pane_type == 4) {
		g_player_flight_transient_timers[g_local_player]
			.system_message_pane_timer = 1888;
	} else if (g_ready_message_queue_count != 0) {
		g_player_flight_transient_timers[g_local_player]
			.ready_message_pane_timer = 354;
	} else if (pane_type == 2 || pane_type == 1) {
		g_player_flight_transient_timers[g_local_player]
			.ready_message_pane_timer = 1416;
	} else {
		g_player_flight_transient_timers[g_local_player]
			.ready_message_pane_timer = 1652;
	}
#ifdef XVT_MODERN
	xvt_cockpit_messages_end_message();
#endif
	hud_draw_craft_name_fps_and_network_status();
	flight_text_set_font_tier(2);
}

/* Expires the message panes. While the message log page is closed,
 * a ready message whose timer ran out (or all, with
 * g_flight_message_panes_force_expire) gives way to the next waiting one or the
 * pane is cleared and slot 0 emptied; the system and flight group panes are
 * cleared and emptied when their timers run out or on a forced expiry. When the
 * target description timer is 0 and the player has a target whose description
 * changed and is actionable, it emits the description in the forward, HUD-only
 * and target camera views and restarts the timer at 1,180 ticks. Sets each
 * active player's pending_action_id to 0 once its pending_action_timer is 0, and
 * clears g_flight_message_panes_force_expire. */
// FUNCTION: XVT 0x451210
void hud_update_flight_message_panes(void)
{

	int local_player = g_local_player;
	const int message_surface_width = 320;
	const int message_surface_height = 200;

	if (((g_player_flight_transient_timers[local_player]
			      .ready_message_pane_timer == 0 &&
	      g_ready_message_pane_queue[0].state_or_message_id !=
		      UINT16_MAX) ||
	     g_flight_message_panes_force_expire != 0) &&
	    g_mfd_page_states[MFD_PAGE_MESSAGE_LOG] == MFD_PAGE_STATE_CLOSED) {
		XVT_LOG_DEBUG(
			"hud.pane_expired pane=\"ready\" message=%u shown=%u age=%u forced=%d predicted=%d",
			(unsigned)g_ready_message_pane_queue[0]
				.state_or_message_id,
			(unsigned)g_ready_message_pane_queue[0].show_count,
			(unsigned)g_ready_message_pane_queue[0].age_seconds,
			g_flight_message_panes_force_expire,
			g_flight_sim_side_effects_suppressed);
		if (g_ready_message_queue_count != 0) {
			hud_advance_ready_message_queue();
			hud_show_flight_message_pane(
				g_ready_message_pane_queue[0].pane_type);
			XVT_LOG_DEBUG(
				"hud.message_advanced message=%u type=%d shown=%u waiting=%u ticks=%u predicted=%d",
				(unsigned)g_ready_message_pane_queue[0]
					.state_or_message_id,
				(int)g_ready_message_pane_queue[0].pane_type,
				(unsigned)g_ready_message_pane_queue[0]
					.show_count,
				(unsigned)g_ready_message_queue_count,
				(unsigned)g_player_flight_transient_timers
					[g_local_player]
						.ready_message_pane_timer,
				g_flight_sim_side_effects_suppressed);
		} else {
			flight_text_set_background_color(
				g_flight_transparent_color_index);
			flight_sw_set_render_target(
				g_flight_offscreen_buffer, g_screen_width,
				g_screen_height,
				g_screen_width * g_flight_bytes_per_pixel);
			flight_text_set_clip_rect(g_ready_message_pane_left,
						  g_ready_message_pane_top,
						  g_ready_message_pane_right,
						  g_ready_message_pane_bottom);
			g_flight_fill_clip_rect_fn();
			flight_sw_set_render_target(NULL, message_surface_width,
						    message_surface_height, 0);
			g_ready_message_pane_queue[0].state_or_message_id =
				UINT16_MAX;
#ifdef XVT_MODERN
			xvt_cockpit_messages_clear(XVT_COCKPIT_MESSAGE_READY);
#endif
		}
		local_player = g_local_player;
	}
	if ((g_player_flight_transient_timers[local_player]
			     .system_message_pane_timer == 0 &&
	     g_system_message_pane.state_or_message_id != UINT16_MAX) ||
	    g_flight_message_panes_force_expire != 0) {
		XVT_LOG_DEBUG(
			"hud.pane_expired pane=\"system\" message=%u shown=%u age=%u forced=%d predicted=%d",
			(unsigned)g_system_message_pane.state_or_message_id,
			(unsigned)g_system_message_pane.show_count,
			(unsigned)g_system_message_pane.age_seconds,
			g_flight_message_panes_force_expire,
			g_flight_sim_side_effects_suppressed);
		flight_text_set_background_color(
			g_flight_transparent_color_index);
		flight_sw_set_render_target(g_flight_offscreen_buffer,
					    g_screen_width, g_screen_height,
					    g_screen_width *
						    g_flight_bytes_per_pixel);
		flight_text_set_clip_rect(g_system_message_pane_left,
					  g_system_message_pane_top,
					  g_system_message_pane_right,
					  g_system_message_pane_bottom);
		g_flight_fill_clip_rect_fn();
		flight_sw_set_render_target(NULL, message_surface_width,
					    message_surface_height, 0);
		g_system_message_pane.state_or_message_id = UINT16_MAX;
#ifdef XVT_MODERN
		xvt_cockpit_messages_clear(XVT_COCKPIT_MESSAGE_SYSTEM);
#endif
		local_player = g_local_player;
	}
	if ((g_player_flight_transient_timers[local_player]
			     .flight_group_message_pane_timer == 0 &&
	     g_flight_group_message_pane.state_or_message_id != UINT16_MAX) ||
	    g_flight_message_panes_force_expire != 0) {
		XVT_LOG_DEBUG(
			"hud.pane_expired pane=\"flight_group\" message=%u shown=%u age=%u forced=%d predicted=%d",
			(unsigned)
				g_flight_group_message_pane.state_or_message_id,
			(unsigned)g_flight_group_message_pane.show_count,
			(unsigned)g_flight_group_message_pane.age_seconds,
			g_flight_message_panes_force_expire,
			g_flight_sim_side_effects_suppressed);
		flight_text_set_background_color(
			g_flight_transparent_color_index);
		flight_sw_set_render_target(g_flight_offscreen_buffer,
					    g_screen_width, g_screen_height,
					    g_screen_width *
						    g_flight_bytes_per_pixel);
		flight_text_set_clip_rect(g_flight_group_message_pane_left,
					  g_flight_group_message_pane_top,
					  g_flight_group_message_pane_right,
					  g_flight_group_message_pane_bottom);
		g_flight_fill_clip_rect_fn();
		flight_sw_set_render_target(NULL, message_surface_width,
					    message_surface_height, 0);
		g_flight_group_message_pane.state_or_message_id = UINT16_MAX;
#ifdef XVT_MODERN
		xvt_cockpit_messages_clear(XVT_COCKPIT_MESSAGE_FLIGHT_GROUP);
#endif
		local_player = g_local_player;
	}
	if (g_player_flight_transient_timers[local_player]
			    .target_description_refresh_timer == 0 &&
	    g_players[local_player].current_target_object_idx != -1) {
		if (g_target_description_message_id !=
		    msg_build_target_description(
			    g_players[local_player].current_target_object_idx,
			    local_player, 0, 0)) {
			if (msg_build_target_description(
				    g_players[g_local_player]
					    .current_target_object_idx,
				    g_local_player, 0, 1) != 0) {
				uint8_t hud_state =
					g_players[g_local_player]
						.view_state.hud_state_live;
				if (hud_state == 19 || hud_state == 0 ||
				    hud_state == 20) {
					g_target_description_message_id =
						msg_build_target_description(
							g_players[g_local_player]
								.current_target_object_idx,
							g_local_player, 1, 0);
					g_player_flight_transient_timers[g_local_player]
						.target_description_refresh_timer =
						1180;
				}
			}
		}
	}
	{
		for (int player_index = 0; player_index < 8; ++player_index) {
			if (g_players[player_index].participation_state != 0 &&
			    g_players[player_index].pending_action_timer == 0) {
				if (g_players[player_index].pending_action_id !=
				    0) {
					XVT_LOG_DEBUG(
						"flight.request_expired slot=%d request=%d param=%d predicted=%d",
						player_index,
						(int)g_players[player_index]
							.pending_action_id,
						(int)g_players[player_index]
							.pending_action_param,
						g_flight_sim_side_effects_suppressed);
				}
				g_players[player_index].pending_action_id = 0;
			}
		}
	}
	if (g_flight_message_panes_force_expire != 0) {
		g_flight_message_panes_force_expire = 0;
	}
}

/* Also zeroes the system pane's timer, so the next
 * hud_update_flight_message_panes clears any system message. */
/* Empties g_ready_message_pane_queue and its count without clearing the drawn
 * pane. */
// FUNCTION: XVT 0x451560
void hud_clear_ready_message_queue(void)
{
	XVT_LOG_DEBUG(
		"hud.ready_queue_cleared slot=%d waiting=%u message=%u predicted=%d",
		g_local_player, (unsigned)g_ready_message_queue_count,
		(unsigned)g_ready_message_pane_queue[0].state_or_message_id,
		g_flight_sim_side_effects_suppressed);
	g_ready_message_queue_count = 0;
	g_ready_message_pane_queue[0].state_or_message_id = UINT16_MAX;
	g_player_flight_transient_timers[g_local_player]
		.system_message_pane_timer = 0;
}

/* Raises the age_seconds of each pane that holds a message. flight_update_timers
 * calls it in the part that runs once per simulated second. */
// FUNCTION: XVT 0x451590
void hud_advance_flight_message_pane_timers(void)
{
	if (g_ready_message_pane_queue[0].state_or_message_id != UINT16_MAX) {
		++g_ready_message_pane_queue[0].age_seconds;
	}
	if (g_system_message_pane.state_or_message_id != UINT16_MAX) {
		++g_system_message_pane.age_seconds;
	}
	if (g_flight_group_message_pane.state_or_message_id != UINT16_MAX) {
		++g_flight_group_message_pane.age_seconds;
	}
}

/* In the forward and HUD-only views, redraws the status line in layout 126 of
 * the current set: the local player's craft name, or with
 * g_flight_conf_tick_counter_enabled and all three samples nonzero, two numbers:
 * SIMULATION_TICKS_PER_SECOND divided by g_flight_tick_overlay_last_loop_ticks and
 * by the mean ticks per sample of the window. With more than one player it also
 * draws the packet drop and lag marks (g_str_cmd_threat_display_text 16 and 17)
 * beside the ready pane on g_flight_offscreen_buffer, colored by
 * g_packet_drop_indicator and g_lag_indicator (0 draws them in the transparent
 * color). Leaves g_flight_text_shadow_enabled at 0 and the shadow color 64 in that
 * case. */
// FUNCTION: XVT 0x4515D0
void hud_draw_craft_name_fps_and_network_status(void)
{

	uint8_t hud_state_live =
		g_players[g_local_player].view_state.hud_state_live;
	if (hud_state_live != HUD_VIEW_FORWARD &&
	    hud_state_live != HUD_VIEW_HUD_ONLY) {
		return;
	}

	flight_surface_lock();
	flight_sw_set_render_target(NULL, 320, 200, 0);
	flight_text_set_background_color(44);
	flight_text_set_font_tier(0);
	if (g_flight_resolution_mode == FLIGHT_RESOLUTION_320X240) {
		hud_format_object_display_name(
			g_players[g_local_player].object_index, 7);
	} else {
		hud_format_object_display_name(
			g_players[g_local_player].object_index, 3);
	}
	flight_text_set_cursor(
		g_hud_element_layouts[g_hud_instrument_set_base_index + 126].x,
		g_hud_element_layouts[g_hud_instrument_set_base_index + 126].y);
	int element_index = g_hud_instrument_set_base_index;
	flight_text_set_clip_rect(
		g_hud_element_layouts[element_index + 126].x,
		g_hud_element_layouts[element_index + 126].y,
		g_hud_element_layouts[element_index + 126].x +
			g_hud_element_layouts[element_index + 126].clip_width,
		g_hud_element_layouts[element_index + 126].y +
			g_hud_element_layouts[element_index + 126]
				.clip_height_or_foreground_color);
	g_flight_fill_clip_rect_fn();
	if (g_flight_conf_tick_counter_enabled != 0 &&
	    g_flight_tick_overlay_last_loop_ticks != 0 &&
	    g_flight_tick_overlay_window_ticks != 0 &&
	    g_flight_tick_overlay_sample_count != 0) {
		flight_text_set_color(78);
		sprintf(g_flight_text_scratch_buffer, "%d %d",
			SIMULATION_TICKS_PER_SECOND /
				g_flight_tick_overlay_last_loop_ticks,
			SIMULATION_TICKS_PER_SECOND /
				(g_flight_tick_overlay_window_ticks /
				 g_flight_tick_overlay_sample_count));
	}
#ifdef XVT_MODERN
	xvt_cockpit_text_record_field(XVT_COCKPIT_TEXT_CRAFT_STATUS,
				      g_flight_text_scratch_buffer,
				      XVT_COCKPIT_ALIGN_CENTER);
#endif
	flight_text_draw_string_centered(g_flight_text_scratch_buffer);
	flight_surface_unlock();

	if (g_flight_player_count > 1) {
		g_flight_text_shadow_enabled = 0;
		flight_text_set_font_tier(0);
		int offscreen_pitch_bytes =
			g_flight_bytes_per_pixel * g_screen_width;
		flight_sw_set_render_target(g_flight_offscreen_buffer,
					    g_screen_width, g_screen_height,
					    offscreen_pitch_bytes);
		flight_text_set_clip_rect(g_ready_message_pane_left -
						  2 * g_flight_font_digit_width,
					  g_ready_message_pane_top,
					  2 * g_flight_font_digit_width +
						  g_ready_message_pane_right +
						  1,
					  g_ready_message_pane_bottom);
		flight_text_set_cursor(g_ready_message_pane_left -
					       2 * g_flight_font_digit_width,
				       g_ready_message_pane_top);
		switch (g_packet_drop_indicator) {
		case 0:
			flight_text_set_background_color(
				g_flight_transparent_color_index);
			flight_text_set_color(g_flight_transparent_color_index);
			flight_text_set_shadow_color(
				g_flight_transparent_color_index);
			break;
		case 1:
			flight_text_set_background_color(65);
			flight_text_set_color(78);
			break;
		case 2:
			flight_text_set_background_color(65);
			flight_text_set_color(75);
			break;
		case 3:
			flight_text_set_background_color(65);
			flight_text_set_color(74);
			break;
		}
#ifdef XVT_MODERN
		xvt_cockpit_text_record_field(XVT_COCKPIT_TEXT_NETWORK_PING,
					      g_str_cmd_threat_display_text[16],
					      XVT_COCKPIT_ALIGN_LEFT);
#endif
		flight_text_draw_string(g_str_cmd_threat_display_text[16]);
		switch (g_lag_indicator) {
		case 0:
			flight_text_set_background_color(
				g_flight_transparent_color_index);
			flight_text_set_color(g_flight_transparent_color_index);
			flight_text_set_shadow_color(
				g_flight_transparent_color_index);
			break;
		case 1:
			flight_text_set_background_color(65);
			flight_text_set_color(78);
			break;
		case 2:
			flight_text_set_background_color(65);
			flight_text_set_color(74);
			break;
		case 3:
			flight_text_set_background_color(65);
			flight_text_set_color(75);
			break;
		}
#ifdef XVT_MODERN
		xvt_cockpit_text_record_field(XVT_COCKPIT_TEXT_NETWORK_LAG,
					      g_str_cmd_threat_display_text[17],
					      XVT_COCKPIT_ALIGN_RIGHT);
#endif
		flight_text_draw_string_right_aligned(
			g_str_cmd_threat_display_text[17]);
		flight_text_set_shadow_color(64);
		flight_sw_set_render_target(NULL, 320, 200, 0);
	}
}

/* Returns g_system_message_pane.state_or_message_id: 0xFFFF while the pane is empty,
 * 1 once a message is shown in it. */
// FUNCTION: XVT 0x451930
uint16_t hud_get_system_message_pane_state(void)
{
	return g_system_message_pane.state_or_message_id;
}

/* Copies the message panes from g_flight_offscreen_buffer onto the flight
 * surface, skipping the transparent color: the ready pane, widened by two digit
 * widths on each side while g_flight_player_count is 1 or more, to layout 117 of
 * the current set; the system and flight group panes, while they hold a
 * message, to layouts 118 and 119, or in the full-screen view to those layouts'
 * x at 11 and 22 rows above the viewport's bottom. The modern build latches the
 * panes for its renderer. */
// FUNCTION: XVT 0x452960
void hud_blit_software_hud_text_panes(void)
{
#ifdef XVT_MODERN
	xvt_cockpit_begin_message_placement();
#endif
	uint16_t transparent_color = g_flight_transparent_color_index;
	if (g_flight_player_count >= 1) {
		flight_text_set_font_tier(0);
#ifdef XVT_MODERN
		xvt_cockpit_latch_message(
			XVT_COCKPIT_MESSAGE_READY,
			g_ready_message_pane_left -
				2 * g_flight_font_digit_width,
			g_ready_message_pane_top,
			g_hud_element_layouts[g_hud_instrument_set_base_index +
					      HUD_MFD_MESSAGE_LOG_ELEMENT]
				.x,
			g_hud_element_layouts[g_hud_instrument_set_base_index +
					      HUD_MFD_MESSAGE_LOG_ELEMENT]
				.y,
			g_ready_message_pane_right +
				4 * g_flight_font_digit_width -
				g_ready_message_pane_left + 1,
			g_ready_message_pane_bottom - g_ready_message_pane_top);
#endif
		flight_sw_blit_rect_to_flight_surface(
			g_flight_offscreen_buffer, transparent_color,
			g_ready_message_pane_left -
				2 * g_flight_font_digit_width,
			g_ready_message_pane_top,
			g_hud_element_layouts[g_hud_instrument_set_base_index +
					      HUD_MFD_MESSAGE_LOG_ELEMENT]
				.x,
			g_hud_element_layouts[g_hud_instrument_set_base_index +
					      HUD_MFD_MESSAGE_LOG_ELEMENT]
				.y,
			g_ready_message_pane_right +
				4 * g_flight_font_digit_width -
				g_ready_message_pane_left + 1,
			g_ready_message_pane_bottom - g_ready_message_pane_top,
			g_screen_width * g_flight_bytes_per_pixel);
	} else {
#ifdef XVT_MODERN
		xvt_cockpit_latch_message(
			XVT_COCKPIT_MESSAGE_READY, g_ready_message_pane_left,
			g_ready_message_pane_top,
			g_hud_element_layouts[g_hud_instrument_set_base_index +
					      HUD_MFD_MESSAGE_LOG_ELEMENT]
				.x,
			g_hud_element_layouts[g_hud_instrument_set_base_index +
					      HUD_MFD_MESSAGE_LOG_ELEMENT]
				.y,
			g_ready_message_pane_right - g_ready_message_pane_left,
			g_ready_message_pane_bottom - g_ready_message_pane_top);
#endif
		flight_sw_blit_rect_to_flight_surface(
			g_flight_offscreen_buffer,
			g_flight_transparent_color_index,
			g_ready_message_pane_left, g_ready_message_pane_top,
			g_hud_element_layouts[g_hud_instrument_set_base_index +
					      HUD_MFD_MESSAGE_LOG_ELEMENT]
				.x,
			g_hud_element_layouts[g_hud_instrument_set_base_index +
					      HUD_MFD_MESSAGE_LOG_ELEMENT]
				.y,
			g_ready_message_pane_right - g_ready_message_pane_left,
			g_ready_message_pane_bottom - g_ready_message_pane_top,
			g_screen_width * g_flight_bytes_per_pixel);
	}

	if (g_players[g_local_player].view_state.hud_state_live !=
	    HUD_VIEW_FULL_SCREEN) {
		if (g_system_message_pane.state_or_message_id != UINT16_MAX) {
#ifdef XVT_MODERN
			xvt_cockpit_latch_message(
				XVT_COCKPIT_MESSAGE_SYSTEM,
				g_system_message_pane_left,
				g_system_message_pane_top,
				g_hud_element_layouts
					[g_hud_instrument_set_base_index + 118]
						.x,
				g_hud_element_layouts
					[g_hud_instrument_set_base_index + 118]
						.y,
				g_system_message_pane_right -
					g_system_message_pane_left,
				g_system_message_pane_bottom -
					g_system_message_pane_top);
#endif
			flight_sw_blit_rect_to_flight_surface(
				g_flight_offscreen_buffer, transparent_color,
				g_system_message_pane_left,
				g_system_message_pane_top,
				g_hud_element_layouts
					[g_hud_instrument_set_base_index + 118]
						.x,
				g_hud_element_layouts
					[g_hud_instrument_set_base_index + 118]
						.y,
				g_system_message_pane_right -
					g_system_message_pane_left,
				g_system_message_pane_bottom -
					g_system_message_pane_top,
				g_screen_width * g_flight_bytes_per_pixel);
		}
		if (g_flight_group_message_pane.state_or_message_id !=
		    UINT16_MAX) {
#ifdef XVT_MODERN
			xvt_cockpit_latch_message(
				XVT_COCKPIT_MESSAGE_FLIGHT_GROUP,
				g_flight_group_message_pane_left,
				g_flight_group_message_pane_top,
				g_hud_element_layouts
					[g_hud_instrument_set_base_index + 119]
						.x,
				g_hud_element_layouts
					[g_hud_instrument_set_base_index + 119]
						.y,
				g_flight_group_message_pane_right -
					g_flight_group_message_pane_left,
				g_flight_group_message_pane_bottom -
					g_flight_group_message_pane_top);
#endif
			flight_sw_blit_rect_to_flight_surface(
				g_flight_offscreen_buffer, transparent_color,
				g_flight_group_message_pane_left,
				g_flight_group_message_pane_top,
				g_hud_element_layouts
					[g_hud_instrument_set_base_index + 119]
						.x,
				g_hud_element_layouts
					[g_hud_instrument_set_base_index + 119]
						.y,
				g_flight_group_message_pane_right -
					g_flight_group_message_pane_left,
				g_flight_group_message_pane_bottom -
					g_flight_group_message_pane_top,
				g_screen_width * g_flight_bytes_per_pixel);
		}
	} else {
		if (g_system_message_pane.state_or_message_id != UINT16_MAX) {
#ifdef XVT_MODERN
			xvt_cockpit_latch_message(
				XVT_COCKPIT_MESSAGE_SYSTEM,
				g_system_message_pane_left,
				g_system_message_pane_top,
				g_hud_element_layouts
					[g_hud_instrument_set_base_index + 118]
						.x,
				g_flight_vp_height - 11,
				g_system_message_pane_right -
					g_system_message_pane_left,
				g_system_message_pane_bottom -
					g_system_message_pane_top);
#endif
			flight_sw_blit_rect_to_flight_surface(
				g_flight_offscreen_buffer, transparent_color,
				g_system_message_pane_left,
				g_system_message_pane_top,
				g_hud_element_layouts
					[g_hud_instrument_set_base_index + 118]
						.x,
				g_flight_vp_height - 11,
				g_system_message_pane_right -
					g_system_message_pane_left,
				g_system_message_pane_bottom -
					g_system_message_pane_top,
				g_screen_width * g_flight_bytes_per_pixel);
		}
		if (g_flight_group_message_pane.state_or_message_id !=
		    UINT16_MAX) {
#ifdef XVT_MODERN
			xvt_cockpit_latch_message(
				XVT_COCKPIT_MESSAGE_FLIGHT_GROUP,
				g_flight_group_message_pane_left,
				g_flight_group_message_pane_top,
				g_hud_element_layouts
					[g_hud_instrument_set_base_index + 119]
						.x,
				g_flight_vp_height - 22,
				g_flight_group_message_pane_right -
					g_flight_group_message_pane_left,
				g_flight_group_message_pane_bottom -
					g_flight_group_message_pane_top);
#endif
			flight_sw_blit_rect_to_flight_surface(
				g_flight_offscreen_buffer, transparent_color,
				g_flight_group_message_pane_left,
				g_flight_group_message_pane_top,
				g_hud_element_layouts
					[g_hud_instrument_set_base_index + 119]
						.x,
				g_flight_vp_height - 22,
				g_flight_group_message_pane_right -
					g_flight_group_message_pane_left,
				g_flight_group_message_pane_bottom -
					g_flight_group_message_pane_top,
				g_screen_width * g_flight_bytes_per_pixel);
		}
	}
#ifdef XVT_MODERN
	xvt_cockpit_latch_messages();
#endif
}

/* Returns the width in pixels of pane pane_type's text without its type byte and
 * without "[" and "]", up to 70 characters. The test for a 0xFE escape inside
 * the bracket check never holds; flight_text_measure_string_width skips the escape
 * instead. A type 1 message's color digit is measured too. */
// FUNCTION: XVT 0x452C40
uint16_t hud_measure_flight_message_pane_text(int16_t pane_type)
{
	const char *text;

	if (pane_type == 3 || pane_type == 4 || pane_type == 7) {
		text = g_system_message_pane.text;
	} else if (pane_type == 8) {
		text = g_flight_group_message_pane.text;
	} else {
		text = g_ready_message_pane_queue[0].text;
	}

	if ((uint8_t)text[0] < 9) {
		++text;
	}

	unsigned int processed_count = 0;
	uint16_t output_length = 0;
	char measured_text[80];
	if (text[processed_count] != '\0') {
		do {
			if ((uint16_t)processed_count >= 70) {
				break;
			}
			char current_char = text[processed_count];
			if (current_char == '[' || current_char == ']') {
				if (current_char == (char)0xFE) {
					text += 2;
				}
			} else {
				measured_text[output_length++] = current_char;
			}
			++processed_count;
		} while (text[processed_count] != '\0');
	}
	measured_text[output_length] = '\0';

	return flight_text_measure_string_width(measured_text);
}

/* Draws the corners of a box at x, y in the flight viewport, each stroke an
 * eighth of the box's size, at least 3 and at most the size, tested against the
 * scene's depth at depth (at least 1). Returns at once when the box is empty or
 * lies outside the viewport. With hardware 3D it hands the box to
 * hud_draw_box_overlay_hw; else it draws the strokes as spans through
 * sw3d_blit_occluded_span at depth g_proj_scale_int / depth, from a run of color_idx
 * in g_panel_box_span_scratch, locking the surface unless
 * g_flight_surface_already_locked is set. */
// FUNCTION: XVT 0x497E00
void hud_draw_depth_tested_box_corners(int x, int y, int width, int height,
				       int color_idx, int depth)
{

	int left = x;
	int bottom = y + height;
	if (bottom <= 0) {
		return;
	}
	if (left + width <= 0 || left >= g_flight_vp_width ||
	    y >= g_flight_vp_height || height <= 0 || width <= 0) {
		return;
	}
	if (g_use_hardware3d != 0) {
		hud_draw_box_overlay_hw(left, y, width, height, color_idx,
					depth);
		return;
	}

	int corner_width = width >> 3;
	int corner_height = height >> 3;
	if (corner_width < 3) {
		corner_width = 3;
	}
	if (corner_height < 3) {
		corner_height = 3;
	}
	if (corner_width > width) {
		corner_width = width;
	}
	if (height < corner_height) {
		corner_height = height;
	}

	uint8_t *span = g_panel_box_span_scratch;
	if (g_flight_bytes_per_pixel == 2) {
		if (left > 0) {
			span = &g_panel_box_span_scratch[2 * left];
		}
		uint16_t *span16 = (uint16_t *)span;
		uint16_t color = g_flight_palette16_bpp[color_idx];
		for (int i = 0; i < corner_width; ++i) {
			span16[i] = color;
		}
	} else {
		if (left > 0) {
			span = &g_panel_box_span_scratch[left];
		}
		for (int i = 0; i < corner_width; ++i) {
			span[i] = (uint8_t)color_idx;
		}
	}

	if (depth < 1) {
		depth = 1;
	}
	float span_depth = (float)(uint32_t)g_proj_scale_int / (float)depth;
	if (g_flight_surface_already_locked == 0) {
		flight_surface_lock();
	}

	if (y >= 0) {
		int span_start = left;
		int span_end = left + corner_width;
		if (span_end > 0 && g_flight_vp_width > left) {
			if (span_start < 0) {
				span_start = 0;
			}
			if (span_end > g_flight_vp_width) {
				span_end = g_flight_vp_width;
			}
			sw3d_blit_occluded_span(span, span_start, span_end, y,
						span_depth);
		}

		span_start = width - corner_width + left;
		span_end = left + width;
		if (span_end > 0 && span_start < g_flight_vp_width) {
			if (span_start < 0) {
				span_start = 0;
			}
			if (span_end > g_flight_vp_width) {
				span_end = g_flight_vp_width;
			}
			sw3d_blit_occluded_span(span, span_start, span_end, y,
						span_depth);
		}
	}

	if (g_flight_vp_height >= bottom) {
		int span_start = left;
		int span_end = left + corner_width;
		if (span_end > 0 && g_flight_vp_width > left) {
			if (span_start < 0) {
				span_start = 0;
			}
			if (span_end > g_flight_vp_width) {
				span_end = g_flight_vp_width;
			}
			sw3d_blit_occluded_span(span, span_start, span_end,
						bottom - 1, span_depth);
		}

		span_start = width - corner_width + left;
		span_end = left + width;
		if (span_end > 0 && span_start < g_flight_vp_width) {
			if (span_start < 0) {
				span_start = 0;
			}
			if (span_end > g_flight_vp_width) {
				span_end = g_flight_vp_width;
			}
			sw3d_blit_occluded_span(span, span_start, span_end,
						bottom - 1, span_depth);
		}
	}

	{
		for (int row_offset = 1; row_offset < corner_height;
		     ++row_offset) {
			int scan_y = y + row_offset;
			if (scan_y >= 0 && g_flight_vp_height > scan_y) {
				if (left >= 0) {
					sw3d_blit_occluded_span(
						span, left, left + 1, scan_y,
						span_depth);
				}
				if (left + width <= g_flight_vp_width) {
					sw3d_blit_occluded_span(
						span, left + width - 1,
						left + width, scan_y,
						span_depth);
				}
			}
		}
	}

	{
		int last_row = height - 1;
		for (int row_offset = height - corner_height;
		     row_offset < last_row; ++row_offset) {
			if (row_offset >= corner_height) {
				int scan_y = y + row_offset;
				if (scan_y >= 0 &&
				    g_flight_vp_height > scan_y) {
					if (left >= 0) {
						sw3d_blit_occluded_span(
							span, left, left + 1,
							scan_y, span_depth);
					}
					if (left + width <= g_flight_vp_width) {
						sw3d_blit_occluded_span(
							span, left + width - 1,
							left + width, scan_y,
							span_depth);
					}
				}
			}
		}
	}

	if (g_flight_surface_already_locked == 0) {
		flight_surface_unlock();
	}
}

/* Reads panel sprites from fileName: records end at a 0xFF byte; the first
 * records_to_skip are dropped and the next sprite_count are copied, each ended
 * with 0xFF, to g_hud_panel_sprite_data_write_cursor, which advances, with
 * g_hud_panel_sprite_data_by_index from first_sprite_index on pointing at each. A file
 * that ends early gives empty sprites. Returns what fe_disk_io_close_global_stream
 * returns; in the modern build 1 when the file does not open, and a read error
 * is fatal. Does not check the 265 entries or the space at the cursor. */
// FUNCTION: XVT 0x49C250
int16_t hud_load_panel_sprite_records(const char *file_name,
				      uint16_t first_sprite_index,
				      int16_t sprite_count,
				      uint16_t records_to_skip)
{
	fe_disk_io_open_global_stream(file_name, "rb", 1, 0);
	int16_t remaining_sprites = sprite_count;
	xvt_file *stream = g_stream;
#ifdef XVT_MODERN
	if (!stream) {
		return 1;
	}
#endif
	int16_t record_index = 0;
	int16_t byte_value;
	while (remaining_sprites != 0) {
		g_hud_panel_sprite_data_by_index[first_sprite_index] =
			g_hud_panel_sprite_data_write_cursor;
		if (record_index >= (int)records_to_skip) {
			++first_sprite_index;
		}
#ifdef XVT_MODERN
		for (byte_value = (int16_t)FILE_GETC(stream);
		     !FILE_EOF(stream) && !FILE_HAS_ERROR(stream);
		     byte_value = (int16_t)FILE_GETC(stream)) {
#else
		for (byte_value = (int16_t)FILE_GETC(stream);
		     (((struct msvc42_crt_file_prefix *)stream)->flags &
		      0x10) == 0;
		     byte_value = (int16_t)FILE_GETC(stream)) {
#endif
			if (byte_value == 0xff) {
				break;
			}
			if (record_index >= (int)records_to_skip) {
				*g_hud_panel_sprite_data_write_cursor =
					(uint8_t)byte_value;
				++g_hud_panel_sprite_data_write_cursor;
			}
		}
#ifdef XVT_MODERN
		if (FILE_HAS_ERROR(stream)) {
			fe_disk_io_close_global_stream(0);
			xvt_storage_fatal("Cannot read panel sprites", 1);
			return 1;
		}
#endif
		if (record_index >= (int)records_to_skip) {
			--remaining_sprites;
			*g_hud_panel_sprite_data_write_cursor = 0xff;
			++g_hud_panel_sprite_data_write_cursor;
			if (remaining_sprites == 0 && byte_value != 0xff) {
				XVT_LOG_WARN(
					"hud.panel_sprites_short sprites=%d start=%u",
					(int)sprite_count,
					(unsigned)records_to_skip);
			}
		}
		++record_index;
	}
	XVT_LOG_DEBUG(
		"hud.panel_sprites_read first=%u sprites=%d start=%u records=%d",
		(unsigned)(uint16_t)(first_sprite_index - sprite_count),
		(int)sprite_count, (unsigned)records_to_skip,
		(int)record_index);
#ifdef XVT_MODERN
	xvt_render_assets_register_panel(
		file_name, (uint16_t)(first_sprite_index - sprite_count),
		(uint16_t)sprite_count, records_to_skip);
#endif
	return fe_disk_io_close_global_stream(0);
}

/* Reads flight icon frames from fileName into dataBuffer, frames ending at a
 * 0xFF byte, each copied with its 0xFF and pointed at by frame_pointers in
 * order. Returns the frame count, or 0 when the file does not open; in the
 * modern build a read error is fatal. A file ending in 0xFF yields an empty
 * last frame. Does not check the buffer's size or the pointer count. */
// FUNCTION: XVT 0x49C330
int flight_icon_load_frames(const char *file_name, uint8_t *data_buffer,
			    uint8_t **frame_pointers)
{
#ifndef XVT_MODERN
	int *stream_flags;
#endif

	if (fe_disk_io_open_global_stream(file_name, "rb", 1, 0) == 0) {
		return 0;
	}
	int16_t frame_count = 0;
	xvt_file *stream = g_stream;
	int16_t value;
#ifdef XVT_MODERN
	while (1) {
		if (FILE_HAS_ERROR(stream)) {
			fe_disk_io_close_global_stream(0);
			xvt_storage_fatal("Cannot read icon frames", 1);
			return 0;
		}
		if (FILE_EOF(stream)) {
			break;
		}
		frame_pointers[frame_count] = data_buffer;
		while (1) {
			value = (int16_t)FILE_GETC(stream);
			if (FILE_HAS_ERROR(stream)) {
				fe_disk_io_close_global_stream(0);
				xvt_storage_fatal("Cannot read icon frames", 1);
				return 0;
			}
			if (FILE_EOF(stream)) {
				break;
			}
			if (value == 0xff) {
				break;
			}
			*data_buffer = (uint8_t)value;
			++data_buffer;
		}
		++frame_count;
		*data_buffer = 0xff;
		++data_buffer;
	}
#else
	stream_flags = &((struct msvc42_crt_file_prefix *)stream)->flags;
	for (; (*stream_flags & 0x10) == 0; ++data_buffer) {
		frame_pointers[frame_count] = data_buffer;
		for (value = (int16_t)FILE_GETC(stream);
		     (*stream_flags & 0x10) == 0;
		     value = (int16_t)FILE_GETC(stream)) {
			if (value == 0xff) {
				break;
			}
			*data_buffer = (uint8_t)value;
			++data_buffer;
		}
		++frame_count;
		*data_buffer = 0xff;
	}
#endif
	fe_disk_io_close_global_stream(0);
#ifdef XVT_MODERN
	xvt_render_assets_register_icons(file_name, frame_pointers,
					 (uint16_t)frame_count);
#endif
	return frame_count;
}
