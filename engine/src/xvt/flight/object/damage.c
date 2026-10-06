#include "xvt/flight/object/damage.h"

#include "xvt/assets/model_mesh.h"
#include "xvt/assets/object_type.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/flight_input.h"
#include "xvt/flight/hud/flight_text.h"
#include "xvt/flight/hud/hud.h"
#include "xvt/flight/hud/mfd.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/flight/transfm2.h"
#include "xvt/math/trig2.h"
#include "xvt/render/flight_palette.h"
#include "xvt/render/flight_sw.h"
#include "xvt/render/renderer.h"
#include "xvt/render/scene_billboard.h"
#include "xvt_runtime/log/log.h"
#include "xvt_runtime/snapshot/cockpit_pages.h"

/* Names of the craft systems by damage_system_id, for the damage page;
 * string_table_load_game_strings points each entry at a line it read from
 * strings.txt, and they are NULL until then. Entry 10 (Damage Assessment)
 * is the page's title. */
// GLOBAL: XVT 0x99F900
const char *g_str_damage_system_names[DAMAGE_SYSTEM_ID_COUNT] = {0};
/* Damaged systems the damage page last drew rows for; when the count
 * changes, damage_display_mfd_page clears the rows and resets the selection.
 * Only that function writes it. */
// GLOBAL: XVT 0x559790
int16_t g_damage_mfd_damaged_system_count_cached = 0;
/* 1 while damage_display_mfd_page draws its rows, which makes every row
 * redraw; it sets 1 before the rows and 0 after, so every pass draws all
 * of them. Only that function uses it. */
// GLOBAL: XVT 0x559794
int16_t g_damage_mfd_redraw_all_rows = 0;
/* System selected on the damage page's previous pass, whose row is drawn
 * again; only damage_display_mfd_page uses it. */
// GLOBAL: XVT 0x559798
int16_t g_damage_mfd_last_selected_system_id = 0;
/* Set to 1 by damage_display_mfd_page when the page opens or the selection
 * moves, and to 0 once the rows are drawn; nothing reads it. */
// GLOBAL: XVT 0x55979C
static int16_t g_damage_mfd_selection_changed = 0;
/* System selected on the damage page, a damage_system_id; -1 makes
 * damage_display_mfd_page select the first damaged system it lists. Set to 0
 * as a mission loads: by flight_main_loop in the original build and
 * xvt_flight_loading_mission_setup in the modern one. flight_process_player_actions
 * also reads it. */
// GLOBAL: XVT 0xA004D4
int16_t g_damage_mfd_current_system_id = 0;
/* Mesh count of the object type damage_queue_craft_billboards_for_object_type is
 * working through; only that function uses it. */
// GLOBAL: XVT 0x9EC60A
static uint16_t g_damage_billboard_mesh_count = 0;

/* Calls damage_queue_craft_billboards_for_object_type for object_index with the
 * object's own type. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x41FC20
void damage_queue_craft_billboards(uint16_t object_index)
{
	uint16_t object_type = g_object_table[object_index].object_type;
	damage_queue_craft_billboards_for_object_type(object_index,
						      object_type);
}

/* Walks the meshes of objectType for the object being drawn and, for an
 * object in a craft slot, queues a textured billboard over each intact
 * fuselage mesh (component_state 0) when the craft's damage frame calls for
 * one. The frame comes from g_fuselage_damage_texture_frame_sequence at the
 * craft's component_state entry just past its last mesh; frames 0x8000 to
 * 0xFEFF are billboards, drawn at size 256 at the projected point and
 * turned to the object's roll on screen. Along the way it sets
 * g_billboard_model_node_switch_index to each mesh, and
 * g_billboard_target_selection_state to 1 for the whole walk when the object is
 * the local beam target, else to 2 on the mesh matching
 * g_render_target_component_idx and 0 on the others. g_render_object_ref points
 * at the object while it is the beam target and is put back on return.
 * Returns that saved g_render_object_ref. Also writes
 * g_billboard_object_or_type_index and g_damage_billboard_mesh_count. In the
 * original build's text have_billboard_angle is read before anything sets it;
 * the modern build sets it to 0 first. */
// FUNCTION: XVT 0x41FC50
uint16_t
damage_queue_craft_billboards_for_object_type(unsigned int object_index,
					      int object_type)
{
	uint16_t have_billboard_angle;

	have_billboard_angle = 0;
	g_billboard_object_or_type_index = (uint16_t)object_index;
	uint16_t saved_render_object_ref = g_render_object_ref;
	g_billboard_target_selection_state = 0;
	if ((uint16_t)object_index == g_local_beam_target_obj_idx) {
		g_billboard_target_selection_state = 1;
		g_render_object_ref = (uint16_t)object_index;
	}

	if (object_type < 73) {
		g_damage_billboard_mesh_count =
			(uint16_t)g_object_type_mesh_cache[object_type]
				.mesh_count;
	} else {
		g_damage_billboard_mesh_count =
			(uint16_t)model_mesh_get_object_type_mesh_count(
				object_type);
	}

	uint16_t mesh_index = 0;
	if (g_damage_billboard_mesh_count > 0) {
		uint16_t billboard_angle = have_billboard_angle;
		do {
			g_billboard_model_node_switch_index = mesh_index;
			int mesh_type_index = mesh_index;
			int mesh_type;
			if (object_type < 73) {
				if (mesh_type_index < 0) {
					mesh_type = MESH_COMPONENT_00_DEFAULT;
				} else {
					if (mesh_type_index >=
					    g_object_type_mesh_cache
						    [object_type]
							    .mesh_count) {
						mesh_type_index =
							g_object_type_mesh_cache
								[object_type]
									.mesh_count -
							1;
					}
					mesh_type =
						g_object_type_mesh_cache[object_type]
							.mesh_types
								[mesh_type_index];
				}
			} else {
				mesh_type =
					model_mesh_get_object_type_mesh_type(
						object_type, mesh_type_index);
			}

			if (g_billboard_target_selection_state == 2) {
				g_billboard_target_selection_state = 0;
			}
			if (mesh_index == g_render_target_component_idx &&
			    g_billboard_target_selection_state == 0) {
				g_billboard_target_selection_state = 2;
			}

			if (object_index <
			    (unsigned int)
				    g_active_region_craft_object_slot_end) {
				int16_t component_state = 0;
				if (g_object_table[object_index].mobj != 0 &&
				    g_object_table[object_index]
						    .mobj->p_craft != 0) {
					component_state =
						g_object_table[object_index]
							.mobj->p_craft
							->component_state
								[mesh_index];
				}
				if (component_state != 0) {
					continue;
				}
			}

			if ((int16_t)mesh_type == MESH_COMPONENT_03_FUSELAGE &&
			    object_index <
				    (unsigned int)
					    g_active_region_craft_object_slot_end) {
				uint16_t frame_index = 0;
				if (g_object_table[object_index].mobj != 0 &&
				    g_object_table[object_index]
						    .mobj->p_craft != 0) {
					frame_index =
						g_object_table[object_index]
							.mobj->p_craft
							->component_state
								[g_damage_billboard_mesh_count];
				}
				int16_t frame =
					g_fuselage_damage_texture_frame_sequence
						[frame_index];
				if ((uint16_t)frame >= 0x8000 &&
				    (uint16_t)frame < 0xFF00) {
					if (have_billboard_angle == 0) {
						int abs_r0z =
							g_obj_view_mat_r0_z;
						int abs_r1z =
							g_obj_view_mat_r1_z;
						if (abs_r0z < 0) {
							abs_r0z =
								(int)(0u -
								      (uint32_t)
									      abs_r0z);
						}
						if (abs_r1z < 0) {
							abs_r1z =
								(int)(0u -
								      (uint32_t)
									      abs_r1z);
						}
						int matrix_x;
						int matrix_y;
						if (abs_r1z > abs_r0z) {
							matrix_x =
								g_obj_view_mat_r0_x;
							matrix_y =
								g_obj_view_mat_r0_y;
						} else {
							matrix_x =
								g_obj_view_mat_r1_x;
							matrix_y =
								g_obj_view_mat_r1_y;
						}

						if (matrix_x < 0) {
							billboard_angle = (uint16_t)trig2_arctan(
								matrix_y,
								(int)(0u -
								      (uint32_t)
									      matrix_x));
						} else {
							billboard_angle =
								(uint16_t)-trig2_arctan(
									matrix_y,
									matrix_x);
						}
						have_billboard_angle = 1;
					}

					billboard_angle =
						(uint16_t)(billboard_angle +
							   g_object_table
								   [object_index]
									   .roll);
					int screen_x =
						transfm2_project_screen_x(
							g_view_space_x,
							g_view_space_depth);
					if ((screen_x & (int)0xFFFF0000) <= 0 &&
					    (screen_x & (int)0xFFFF0000) >=
						    -65536) {
						int screen_y =
							transfm2_project_screen_y(
								g_view_space_y,
								g_view_space_depth);
						if ((screen_y &
						     (int)0xFFFF0000) <= 0 &&
						    (screen_y &
						     (int)0xFFFF0000) >=
							    -65536) {
							uint16_t viewport_half_height =
								g_flight_vp_height >>
								1;
							screen_y -=
								viewport_half_height;
							screen_y =
								viewport_half_height -
								screen_y;
							scene_billboard_queue_projected_textured(
								g_billboard_object_or_type_index,
								(uint16_t)frame,
								0x100,
								(int16_t)
									screen_x,
								(int16_t)
									screen_y,
								g_view_space_depth,
								billboard_angle);
						}
					}
				}
			}

		} while (g_damage_billboard_mesh_count > ++mesh_index);
	}

	g_render_object_ref = saved_render_object_ref;
	return saved_render_object_ref;
}

/* Draws the damage page of the multi-function display for the local
 * player's craft into g_flight_offscreen_buffer: under the Damage Assessment
 * title, one row per fitted system whose health is 0, in the craft's
 * display order, with its repair time. It marks the page closed when
 * nothing is damaged. While it is the active page in a one-player game,
 * action key 0x0D moves the selected system to the top of the display
 * order, and 0xA6 and 0xA7 step the selection back and forward through the
 * damaged systems. Returns 0 when the local player has no craft (clearing
 * the page area if its state changed) or no craft record, or when the
 * page's state changed while it is closing or nothing is damaged; otherwise
 * 1 after drawing the rows. Writes g_mfd_page_states[MFD_PAGE_DAMAGE],
 * g_mfd_active_page, g_mfd_secondary_page, g_hud_element_state_cache, the craft's
 * system_display_slot_by_system and the g_damageMfd* globals; the modern build
 * also records the page through XvtCockpitPages_*. */
// FUNCTION: XVT 0x46B650
int16_t damage_display_mfd_page(void)
{
	const int default_width = 320;
	const int default_height = 200;
	const int hud_state_index = HUD_MFD_DAMAGE_ELEMENT;
	int object_index = g_players[g_local_player].object_index;
	int16_t source_left;
	int16_t source_top;
	int16_t source_right;
	int16_t source_bottom;
	int pitch_bytes;
	if (object_index == -1) {
		if (g_hud_element_state_cache[g_hud_instrument_set_base_index +
					      hud_state_index] !=
		    g_mfd_page_states[MFD_PAGE_DAMAGE]) {
			pitch_bytes = g_flight_bytes_per_pixel;
			pitch_bytes *= g_screen_width;
			flight_sw_set_render_target(
				g_flight_offscreen_buffer, g_screen_width,
				g_screen_height, pitch_bytes);
			flight_text_set_font_tier(0);
			source_left = g_mfd_damage_blit_source_x + 2;
			source_top = g_mfd_damage_blit_source_y + 2;
			source_right = g_mfd_damage_blit_source_x +
				       g_mfd_damage_blit_width - 2;
			source_bottom = g_mfd_damage_blit_source_y +
					g_mfd_damage_blit_height - 2;
			flight_text_set_background_color(
				g_flight_transparent_color_index);
			flight_text_set_clip_rect(
				source_left - 2, source_top - 2,
				source_right + 2, source_bottom + 2);
			g_flight_fill_clip_rect_fn();
			if (g_mfd_page_states[MFD_PAGE_DAMAGE] ==
				    MFD_PAGE_STATE_CLOSING &&
			    g_mfd_active_page == MFD_PAGE_DAMAGE) {
				g_mfd_active_page = g_mfd_secondary_page;
				g_mfd_secondary_page =
					mfd_find_secondary_open_page();
			}
			flight_sw_set_render_target(NULL, default_width,
						    default_height, 0);
		}
		return 0;
	}
	struct craft_data *craft = g_object_table[object_index].mobj->p_craft;
	if (craft == NULL) {
		return 0;
	}

	uint16_t system_ids[CRAFT_SUBSYSTEM_COUNT];
	/* display_slot has two jobs. In the loops that read
	 * system_display_slot_by_system it counts systems, which fills
	 * system_ids with the system shown in each display slot; in the loops
	 * that read system_ids it counts display slots. */
	int16_t display_slot;
	for (display_slot = 0; display_slot < CRAFT_SUBSYSTEM_COUNT;
	     ++display_slot) {
		system_ids[craft->system_display_slot_by_system[display_slot]] =
			(int16_t)display_slot;
	}
	display_slot = 0;
	int16_t all_systems_ok = 1;
	int16_t damaged_system_count = 0;
	for (; display_slot < CRAFT_SUBSYSTEM_COUNT; ++display_slot) {
		uint16_t system_id = system_ids[display_slot];
		if (craft->system_health[system_id] == 0 &&
		    (g_subsystem_id_to_flag[system_id] & craft->system_flags) !=
			    0) {
			all_systems_ok = 0;
			++damaged_system_count;
		}
	}
	if (all_systems_ok != 0) {
		g_mfd_page_states[MFD_PAGE_DAMAGE] = MFD_PAGE_STATE_CLOSED;
	}

	pitch_bytes = g_flight_bytes_per_pixel;
	pitch_bytes *= g_screen_width;
	flight_sw_set_render_target(g_flight_offscreen_buffer, g_screen_width,
				    g_screen_height, pitch_bytes);
	flight_text_set_font_tier(0);
	source_left = g_mfd_damage_blit_source_x + 2;
	source_top = g_mfd_damage_blit_source_y + 2;
	source_right = g_mfd_damage_blit_source_x + g_mfd_damage_blit_width - 2;
	source_bottom =
		g_mfd_damage_blit_source_y + g_mfd_damage_blit_height - 2;
	xvt_cockpit_pages_set_origin(MFD_PAGE_DAMAGE,
				     g_mfd_damage_blit_source_x,
				     g_mfd_damage_blit_source_y);
	uint16_t row_top = source_top;
	uint16_t line_height = g_flight_font_line_height + 1;
	if (g_hud_element_state_cache[g_hud_instrument_set_base_index +
				      hud_state_index] !=
	    g_mfd_page_states[MFD_PAGE_DAMAGE]) {
		xvt_cockpit_pages_clear(MFD_PAGE_DAMAGE);
		flight_text_set_background_color(
			g_flight_transparent_color_index);
		flight_text_set_clip_rect(source_left - 2, source_top - 2,
					  source_right + 2, source_bottom + 2);
		g_flight_fill_clip_rect_fn();
		if (g_mfd_page_states[MFD_PAGE_DAMAGE] !=
			    MFD_PAGE_STATE_CLOSING &&
		    all_systems_ok == 0) {
			flight_text_set_background_color(
				g_flight_transparent_color_index);
			flight_text_set_clear_line_background(1);
			flight_text_set_color(0x46);
			g_flight_fill_clip_rect_fn();
			flight_text_set_cursor(source_left, source_top);
			xvt_cockpit_pages_record_background(MFD_PAGE_DAMAGE);
			xvt_cockpit_pages_begin_section(
				MFD_PAGE_DAMAGE, XVT_COCKPIT_PAGE_HEADER);
			flight_text_draw_string_centered(
				g_str_damage_system_names
					[DAMAGE_SYSTEM_10_DAMAGE_ASSESSMENT]);
			xvt_cockpit_pages_end_section();
			g_damage_mfd_damaged_system_count_cached =
				(int16_t)damaged_system_count;
		} else {
			if (g_mfd_active_page == MFD_PAGE_DAMAGE) {
				g_mfd_active_page = g_mfd_secondary_page;
				g_mfd_secondary_page =
					mfd_find_secondary_open_page();
			}
			if (all_systems_ok != 0) {
				g_hud_element_state_cache
					[g_hud_instrument_set_base_index +
					 hud_state_index] =
						MFD_PAGE_STATE_CLOSING;
			}
			flight_sw_set_render_target(NULL, default_width,
						    default_height, 0);
			return 0;
		}
	} else {
		flight_text_set_background_color(
			g_flight_transparent_color_index);
		flight_text_set_clear_line_background(1);
	}
	{
		row_top += line_height;
		int16_t row_bottom =
			row_top + line_height * damaged_system_count + 2;
		if (damaged_system_count !=
		    g_damage_mfd_damaged_system_count_cached) {
			flight_text_set_clip_rect(
				source_left - 2, row_top, source_right + 2,
				row_top +
					line_height *
						(g_damage_mfd_damaged_system_count_cached +
						 1));
			g_flight_fill_clip_rect_fn();
			g_damage_mfd_current_system_id = -1;
			g_damage_mfd_damaged_system_count_cached =
				(int16_t)damaged_system_count;
		}
		if (g_players[g_local_player].map_camera_state == 0 &&
		    g_mfd_active_page == MFD_PAGE_NONE) {
			g_mfd_active_page = MFD_PAGE_DAMAGE;
		}
		if (g_mfd_active_page == MFD_PAGE_DAMAGE) {
			flight_text_set_background_color(0x46);
			flight_text_set_clip_rect(
				source_left - 2, source_top - 2,
				source_right + 2, row_bottom + 2);
			xvt_cockpit_pages_record_border(MFD_PAGE_DAMAGE);
			g_flight_fill_rect_clipped_fn(
				source_left - 2, source_top - 2,
				source_right + 2, row_bottom + 2, 1);
		} else if (g_mfd_secondary_page == MFD_PAGE_DAMAGE) {
			flight_text_set_background_color(
				g_flight_transparent_color_index);
			flight_text_set_clip_rect(
				source_left - 2, source_top - 2,
				source_right + 2, row_bottom + 2);
			xvt_cockpit_pages_record_border(MFD_PAGE_DAMAGE);
			g_flight_fill_rect_clipped_fn(
				source_left - 2, source_top - 2,
				source_right + 2, row_bottom + 2, 1);
		}
		flight_text_set_clip_rect(source_left, row_top, source_right,
					  row_bottom);
		flight_text_set_background_color(
			g_flight_transparent_color_index);
		flight_text_set_color(0x43);
		g_damage_mfd_redraw_all_rows = 1;
		if (g_hud_element_state_cache[g_hud_instrument_set_base_index +
					      hud_state_index] !=
		    g_mfd_page_states[MFD_PAGE_DAMAGE]) {
			g_damage_mfd_current_system_id = -1;
			g_damage_mfd_selection_changed = 1;
		} else if (g_mfd_active_page == MFD_PAGE_DAMAGE &&
			   g_flight_player_count == 1) {
			switch (g_current_action_key) {
			case 0x0D: {
				uint8_t selected_slot =
					craft->system_display_slot_by_system
						[g_damage_mfd_current_system_id];
				for (display_slot = 0;
				     display_slot < CRAFT_SUBSYSTEM_COUNT;
				     ++display_slot) {
					uint8_t slot =
						craft->system_display_slot_by_system
							[display_slot];
					if (slot < selected_slot) {
						craft->system_display_slot_by_system
							[display_slot] =
							slot + 1;
					}
				}
				craft->system_display_slot_by_system
					[g_damage_mfd_current_system_id] = 0;
				if (selected_slot != 0) {
					XVT_LOG_DEBUG(
						"combat.repair_first slot=%d system=%d from=%d",
						g_local_player,
						(int)g_damage_mfd_current_system_id,
						(int)selected_slot);
				}
				g_damage_mfd_selection_changed = 1;
				break;
			}
			case 0xA6: {
				int16_t adjacent =
					g_damage_mfd_current_system_id;
				do {
					adjacent =
						damage_find_adjacent_damaged_system(
							adjacent,
							(uint16_t)0xFFFF);
					if ((g_subsystem_id_to_flag[adjacent] &
					     craft->system_flags) != 0 &&
					    craft->system_health[adjacent] ==
						    0) {
						g_damage_mfd_current_system_id =
							adjacent;
					}
				} while ((g_subsystem_id_to_flag[adjacent] &
					  craft->system_flags) == 0);
				g_damage_mfd_selection_changed = 1;
				break;
			}
			case 0xA7: {
				int16_t adjacent =
					g_damage_mfd_current_system_id;
				do {
					adjacent =
						damage_find_adjacent_damaged_system(
							adjacent, 1);
					if ((g_subsystem_id_to_flag[adjacent] &
					     craft->system_flags) != 0 &&
					    craft->system_health[adjacent] ==
						    0) {
						g_damage_mfd_current_system_id =
							adjacent;
					}
				} while (
					(g_subsystem_id_to_flag
						 [g_damage_mfd_current_system_id] &
					 craft->system_flags) == 0);
				g_damage_mfd_selection_changed = 1;
				break;
			}
			default:
				break;
			}
			if (g_current_action_key == 0xA6 ||
			    g_current_action_key == 0xA7) {
				XVT_LOG_DEBUG(
					"combat.repair_selected slot=%d direction=%d system=%d",
					g_local_player,
					g_current_action_key == 0xA6 ? -1 : 1,
					(int)g_damage_mfd_current_system_id);
			}
		}
		for (display_slot = 0; display_slot < CRAFT_SUBSYSTEM_COUNT;
		     ++display_slot) {
			system_ids[craft->system_display_slot_by_system
					   [display_slot]] =
				(int16_t)display_slot;
		}
		xvt_cockpit_pages_begin_section(MFD_PAGE_DAMAGE,
						XVT_COCKPIT_PAGE_BODY);
		for (display_slot = 0; display_slot < CRAFT_SUBSYSTEM_COUNT;
		     ++display_slot) {
			uint16_t system_id = system_ids[display_slot];
			if (craft->system_health[system_id] == 0 &&
			    (g_subsystem_id_to_flag[system_id] &
			     craft->system_flags) != 0 &&
			    row_top + line_height < row_bottom) {
				flight_text_set_clip_rect(
					source_left, row_top, source_right,
					row_top + line_height);
				flight_text_set_cursor(source_left, row_top);
				if (g_damage_mfd_current_system_id == -1) {
					g_damage_mfd_current_system_id =
						(int16_t)system_id;
					g_damage_mfd_last_selected_system_id =
						(int16_t)system_id;
				}
				if (g_damage_mfd_current_system_id ==
				    system_id) {
					flight_text_set_background_color(0x33);
				} else {
					flight_text_set_background_color(
						g_flight_transparent_color_index);
				}
				xvt_cockpit_pages_record_row(
					system_id,
					g_damage_mfd_current_system_id ==
						system_id);
				if (g_damage_mfd_redraw_all_rows != 0 ||
				    g_damage_mfd_current_system_id ==
					    system_id ||
				    g_damage_mfd_last_selected_system_id ==
					    system_id) {
					damage_draw_mfd_system_status_row(
						(damage_system_id)system_id,
						(int16_t)row_top,
						(int16_t)source_left);
				}
				row_top += line_height;
			}
		}
		xvt_cockpit_pages_end_section();
		xvt_cockpit_pages_record_scroll(MFD_PAGE_DAMAGE, 0,
						damaged_system_count,
						g_damage_mfd_current_system_id);
		g_damage_mfd_selection_changed = 0;
		g_damage_mfd_redraw_all_rows = 0;
		g_damage_mfd_last_selected_system_id =
			g_damage_mfd_current_system_id;
		flight_sw_set_render_target(NULL, default_width, default_height,
					    0);
		return 1;
	}
}

/* Finds the system with health 0 before or after current_system_idx in the
 * local player's craft's display order: back when direction_step is -1,
 * forward otherwise. Returns 0 when current_system_idx itself does not have
 * health 0. Despite the name, at either end it can return an undamaged
 * system: back from the first damaged system it returns the last undamaged
 * one in display order (the last system when all are damaged), and forward
 * from the last it returns the first undamaged one, else the first damaged
 * one. Looks at health only, not at whether the craft is fitted with the
 * system; does not check that the local player has a craft. */
// FUNCTION: XVT 0x46BE10
int16_t damage_find_adjacent_damaged_system(int16_t current_system_idx,
					    int16_t direction_step)
{
	uint8_t system_by_display_slot[CRAFT_SUBSYSTEM_COUNT];

	struct craft_data *craft =
		g_object_table[g_players[g_local_player].object_index]
			.mobj->p_craft;
	int system_idx = 0;
	do {
		system_by_display_slot
			[craft->system_display_slot_by_system[system_idx]] =
				system_idx;
		++system_idx;
	} while (system_idx < CRAFT_SUBSYSTEM_COUNT);

	int16_t previous_damaged_system = -1;
	int display_slot = 0;
	int16_t result = current_system_idx;
	for (;;) {
		uint16_t display_system = system_by_display_slot[display_slot];
		system_idx = display_system;
		if (craft->system_health[system_idx] == 0) {
			unsigned int promoted_current_system = (uint16_t)result;
			if (promoted_current_system ==
			    (unsigned int)system_idx) {
				break;
			}
			previous_damaged_system = display_system;
		}
		++display_slot;
		if (display_slot >= CRAFT_SUBSYSTEM_COUNT) {
			return 0;
		}
	}

	if (direction_step == -1) {
		if (previous_damaged_system == -1) {
			display_slot = CRAFT_SUBSYSTEM_COUNT - 1;
			while (craft->system_health
				       [system_by_display_slot[display_slot]] ==
			       0) {
				--display_slot;
				if (display_slot < 0) {
					return system_by_display_slot
						[CRAFT_SUBSYSTEM_COUNT - 1];
				}
			}
			return system_by_display_slot[display_slot];
		} else {
			return previous_damaged_system;
		}
	}

	while (++display_slot < CRAFT_SUBSYSTEM_COUNT) {
		system_idx = system_by_display_slot[display_slot];
		if (craft->system_health[system_idx] == 0) {
			result = system_by_display_slot[display_slot];
			return result;
		}
	}
	for (display_slot = 0; display_slot < CRAFT_SUBSYSTEM_COUNT;
	     ++display_slot) {
		system_idx = system_by_display_slot[display_slot];
		if (craft->system_health[system_idx] != 0) {
			return system_by_display_slot[display_slot];
		}
	}
	display_slot = 0;
	while (display_slot < CRAFT_SUBSYSTEM_COUNT) {
		system_idx = system_by_display_slot[display_slot];
		if (craft->system_health[system_idx] == 0) {
			return system_by_display_slot[display_slot];
		}
		++display_slot;
	}
	return result;
}

/* Draws one damage-page row for systemId at the text cursor: clears the
 * clip rectangle, draws the system's name, then sets the cursor to (value_x,
 * y) and draws the status with flight_text_draw_string_right_aligned: "N/A"
 * when the local player's craft is not fitted with the system, the repair
 * time as MM:SS when its health is 0, "100%", or else two digits and "%",
 * each in its own color. Does not check that the local player has a craft,
 * nor that health is at most 100. */
// FUNCTION: XVT 0x46BF90
void damage_draw_mfd_system_status_row(damage_system_id system_id, int16_t y,
				       int16_t value_x)
{
	struct mobile_object *mobile_object =
		g_object_table[g_players[g_local_player].object_index].mobj;
	uint16_t subsystem_flag = g_subsystem_id_to_flag[(uint16_t)system_id];
	struct craft_data *craft = mobile_object->p_craft;
	char status_text[8];
	if ((craft->system_flags & subsystem_flag) == 0) {
		flight_text_set_color(0x41);
		status_text[0] = 'N';
		status_text[1] = '/';
		status_text[2] = 'A';
		status_text[3] = '\0';
	} else {
		uint16_t health = craft->system_health[(uint16_t)system_id];
		if (health == 0) {
			flight_text_set_color(0x4A);
			uint16_t repair_seconds = craft->system_repair_seconds[(
				uint16_t)system_id];
			uint8_t repair_minutes = repair_seconds / 60;
			uint8_t remaining_seconds =
				repair_seconds - 60 * repair_minutes;
			uint8_t minute_tens = repair_minutes / 10;
			status_text[0] = minute_tens + '0';
			status_text[2] = ':';
			status_text[1] =
				repair_minutes - 10 * minute_tens + '0';
			uint8_t second_tens = remaining_seconds / 10;
			status_text[3] = second_tens + '0';
			status_text[4] =
				remaining_seconds - 10 * second_tens + '0';
			status_text[5] = '\0';
		} else if (health == 100) {
			flight_text_set_color(0x52);
			status_text[0] = '1';
			status_text[1] = '0';
			status_text[2] = '0';
			status_text[3] = '%';
			status_text[4] = '\0';
		} else {
			flight_text_set_color(0x4E);
			int16_t health_tens =
				craft->system_health[(uint16_t)system_id] / 10;
			status_text[0] = health_tens + '0';
			uint16_t health_value =
				craft->system_health[(uint16_t)system_id];
			status_text[2] = '%';
			status_text[1] = health_value - 10 * health_tens + '0';
			status_text[3] = '\0';
		}
	}

	g_flight_fill_clip_rect_fn();
	flight_text_draw_string(g_str_damage_system_names[(uint16_t)system_id]);
	g_flight_draw_char_fn(10);
	flight_text_set_cursor((uint16_t)value_x, (uint16_t)y);
	flight_text_draw_string_right_aligned(status_text);
}
