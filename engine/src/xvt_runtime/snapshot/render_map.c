#include "xvt_runtime/snapshot/render_map.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "xvt/assets/opt_model.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/hud/flight_map.h"
#include "xvt/flight/hud/flight_text.h"
#include "xvt/flight/hud/hud.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/player/player.h"
#include "xvt/math/trig2.h"
#include "xvt/render/flight_sw.h"
#include "xvt_runtime/snapshot/render_assets.h"
#include "xvt_runtime/snapshot/render_hud.h"

static uint32_t planar_distance(uint32_t a, uint32_t b)
{
	uint32_t max = a > b ? a : b, min = a > b ? b : a, index = 0;
	if (max == min) {
		index = 256;
	} else if (max) {
		uint32_t d = max, n = min;
		if (!(d & 0xff000000u)) {
			d <<= 8;
			n <<= 8;
			if (!(d & 0xff000000u)) {
				d <<= 8;
				n <<= 8;
			}
		}
		index = (uint16_t)(n / (d >> 16)) >> 8;
	}
	uint32_t scale = g_hypot_excess_q16_table[index];
	return max + (max >> 16) * scale +
	       (uint16_t)((((max & 65535) * scale) + 32768) >> 16);
}

static uint32_t magnitude(int32_t value)
{
	return value < 0 ? 0u - (uint32_t)value : (uint32_t)value;
}

static int other_player_box(const struct player_data *p,
			    const struct object_record *o, unsigned slot)
{
	const struct craft_data *c = o->mobj ? o->mobj->p_craft : NULL;
	if (!c || o->player_owner_idx == -1) {
		return 0;
	}
	unsigned player_team = (uint16_t)p->team, fg = o->flight_group_idx;
	if (!g_flight_mission_state.locate_players_enabled &&
	    player_team < 10 && !c->identified_order_by_team[player_team] &&
	    fg < 48) {
		unsigned team = g_mission_flight_groups[fg].fg.team;
		if (team < 10 && team != player_team &&
		    !g_mission_teams[player_team].allies[team]) {
			return 0;
		}
	}
	return !(slot < (unsigned)g_active_region_craft_object_slot_end &&
		 (c->working_subsystems & CRAFT_SUBSYSTEM_FLAG_BEAM_SYSTEM) &&
		 c->beam_active && c->beam_type_id == BEAM_TYPE_DECOY &&
		 c->beam_output);
}

static int extent(const struct object_record *o)
{
	const struct craft_data *c = o->mobj ? o->mobj->p_craft : NULL;
	if (!c) {
		return g_object_type_table[o->object_type].max_bounds_extent;
	}
	const struct model_def *d = &g_model_defs[c->model_index];
	return (int)((uint32_t)((d->bound_size_x + d->bound_size_y +
				 d->bound_size_z) /
				3)
		     << d->bound_size_shift);
}

static void endpoint(struct xvt_snap_map *map, unsigned slot)
{
	const struct object_record *o = &g_object_table[slot];
	if (!o->mobj || !o->mobj->p_craft) {
		return;
	}
	const struct craft_data *craft = o->mobj->p_craft;
	uint16_t ref;
	if (slot < (unsigned)g_craft_data_pool_capacity) {
		ref = craft->ai_controller.target_obj_idx;
	} else {
		memcpy(&ref, &craft->model_index, sizeof ref);
	}
	if (ref == UINT16_MAX) {
		return;
	}
	if (ref < 0x8000) {
		if (ref >= g_region_main_object_slot_end +
				   g_region_static_object_slot_count) {
			return;
		}
		map->order_endpoint[0] = g_object_table[ref].world_x;
		map->order_endpoint[1] = g_object_table[ref].world_y;
		map->order_endpoint[2] = g_object_table[ref].world_z;
	} else {
		unsigned fg = o->flight_group_idx;
		if (fg >= 48) {
			return;
		}
		if (ref == 0x8000) {
			ref = g_mission_fg_stats[fg].current_mission_point_ref;
		}
		unsigned i = (uint16_t)(ref - 0x8000);
		if (i >= sizeof g_mission_flight_groups[fg].fg.mission_point_x /
				 sizeof g_mission_flight_groups[fg]
					 .fg.mission_point_x[0]) {
			return;
		}
		map->order_endpoint[0] =
			g_mission_flight_groups[fg].fg.mission_point_x[i] * 256;
		map->order_endpoint[1] =
			-g_mission_flight_groups[fg].fg.mission_point_y[i] *
			256;
		map->order_endpoint[2] =
			g_mission_flight_groups[fg].fg.mission_point_z[i] * 256;
	}
	map->endpoint_valid = 1;
}

static void label(struct xvt_snap_map *map, struct xvt_snap_map_object *m,
		  const struct object_record *o)
{
	char text[96] = {0};
	unsigned fg = o->flight_group_idx;
	if (fg >= 48 || (!o->mobj && o->genus_id == CRAFT_GENUS_MINE)) {
		return;
	}
	const struct craft_data *craft = o->mobj ? o->mobj->p_craft : NULL;
	if (o->mobj && (o->mobj->family != 0 || !craft)) {
		return;
	}
	const struct xvt_flight_group *group = &g_mission_flight_groups[fg].fg;
	unsigned number = 0;
	if (craft && group->disable_wave_numbering != 1 &&
	    (group->global_unit || group->number_of_craft != 1 ||
	     group->number_of_waves)) {
		number = (uint16_t)craft->craft_index_in_group;
	}
	if (number > 999) {
		number = 999;
	}
	if (number) {
		snprintf(text, sizeof text, "%.*s %u", (int)sizeof group->name,
			 group->name, number);
	} else {
		snprintf(text, sizeof text, "%.*s", (int)sizeof group->name,
			 group->name);
	}
	size_t length = strlen(text) + 1;
	if (length > XVT_SNAP_LABEL_BYTES - map->label_bytes) {
		return;
	}
	m->label_offset = map->label_bytes;
	memcpy(map->labels + map->label_bytes, text, length);
	map->label_bytes += (uint32_t)length;
	m->label_visible = text[0] != 0;
	static const unsigned colors[6] = {62, 54, 50, 58, 54, 212};
	m->label_color_argb = xvt_render_draw_color(
		colors[m->effective_iff < 6 ? m->effective_iff : 3]);
}

/* Sets an object's map icon from its object type and IFF color group in the
 * current icon set: the icon's width and height and, once icons are loaded,
 * its frame and the map's icon asset id. */
static void icon(struct xvt_snap_map *map, struct xvt_snap_map_object *m,
		 const uint8_t *frames, unsigned object_type, unsigned group)
{
	unsigned base_frame = object_type < 106 ? frames[object_type] : 19;
	const uint8_t *widths = g_flight_icons640_width_by_frame,
		      *heights = g_flight_icons640_height_by_frame;
	if (frames == g_flight_map_icons320x240_frame_by_object_type) {
		widths = g_flight_map_icons320x240_width_by_frame;
		heights = g_flight_map_icons320x240_height_by_frame;
	} else if (frames == g_flight_map_icons480x360_frame_by_object_type) {
		widths = g_flight_map_icons480x360_width_by_frame;
		heights = g_flight_map_icons480x360_height_by_frame;
	}
	m->icon_width = widths[base_frame];
	m->icon_height = heights[base_frame];
	unsigned frame = base_frame + g_flight_icon_frame_count * group / 4;
	if (g_flight_icon_frames &&
	    frame < (unsigned)g_flight_icon_frame_count) {
		uint32_t actual;
		uint64_t id = xvt_render_assets_map_icon_frame(frame, &actual);
		map->icon_asset_id = id;
		m->icon_frame = (uint16_t)actual;
	}
}

/* Sets an object's range to the camera's focus object, capped at 9999, when
 * the focus is a valid slot; the range shows only with the object's overlay
 * and never for a projectile. */
static void range(struct xvt_snap_map_object *m, const struct player_data *p,
		  const struct object_record *o, unsigned genus)
{
	unsigned focus = p->view_state.camera_focus_obj_idx;
	if (focus < (unsigned)(g_region_main_object_slot_end +
			       g_region_static_object_slot_count)) {
		uint32_t dx = magnitude(
				 (int32_t)((uint32_t)o->world_x -
					   (uint32_t)g_object_table[focus]
						   .world_x)),
			 dy = magnitude(
				 (int32_t)((uint32_t)o->world_y -
					   (uint32_t)g_object_table[focus]
						   .world_y)),
			 dz = magnitude(
				 (int32_t)((uint32_t)o->world_z -
					   (uint32_t)g_object_table[focus]
						   .world_z));
		uint32_t range =
			(planar_distance(planar_distance(dx, dy), dz) * 161) >>
			16;
		m->range_value = (uint16_t)(range > 9999 ? 9999 : range);
		m->range_visible = m->overlay_visible &&
				   genus != CRAFT_GENUS_PLAYER_PROJECTILE &&
				   genus != CRAFT_GENUS_OTHER_PROJECTILE;
	}
}

void xvt_render_map_capture(struct xvt_snap_map *map,
			    const struct xvt_snap_object *objects,
			    unsigned count)
{
	memset(map, 0, sizeof *map);
	const struct player_data *p = &g_players[g_local_player];
	if (!p->map_camera_state) {
		return;
	}
	map->active = 1;
	map->grid_z = -65536;
	map->font_asset_id = xvt_render_assets_image_id(
		g_flight_resolution_mode == FLIGHT_RESOLUTION_640X480
			? g_flight_font_small_sw
			: g_flight_font_micro_sw);
	map->target = (struct xvt_snap_object_id){UINT16_MAX, 0};
	const uint8_t *frames = g_flight_icons640_frame_by_object_type;
	if (g_flight_icon_resource_path ==
	    g_flight_map_icons320x240_resource_path) {
		frames = g_flight_map_icons320x240_frame_by_object_type;
	} else if (g_flight_icon_resource_path ==
		   g_flight_map_icons480x360_resource_path) {
		frames = g_flight_map_icons480x360_frame_by_object_type;
	}
	for (unsigned i = 0; i < count; ++i) {
		const struct xvt_snap_object *snap = &objects[i];
		unsigned slot = snap->id.slot, genus = snap->genus;
		int box = genus <= CRAFT_GENUS_PLATFORM ||
			  (snap->slot_class == XVT_SLOT_STATIC &&
			   genus >= CRAFT_GENUS_MINE &&
			   genus <= CRAFT_GENUS_SATELLITE);
		int sphere = genus == CRAFT_GENUS_PLAYER_PROJECTILE ||
			     genus == CRAFT_GENUS_OTHER_PROJECTILE ||
			     genus == CRAFT_GENUS_SMALL_DEBRIS ||
			     genus == CRAFT_GENUS_EXPLOSION;
		if (snap->slot_class == XVT_SLOT_STATIC
			    ? !box
			    : (slot >= (unsigned)g_explosion_object_slot_end ||
			       (!box && !sphere))) {
			continue;
		}
		const struct object_record *o = &g_object_table[slot];
		struct xvt_snap_map_object *m =
			&map->objects[map->object_count++];
		m->object_index = (uint16_t)i;
		m->render_kind =
			(uint8_t)(box ? XVT_MAP_MODEL_OR_ICON : XVT_MAP_EFFECT);
		m->cull_kind = (uint8_t)box;
		m->box_extent = extent(o);
		m->effective_iff =
			o->mobj ? (uint8_t)o->mobj->iff
				: (o->flight_group_idx < 48
					   ? g_mission_flight_groups
						     [o->flight_group_idx]
							     .fg.iff
					   : 0);
		unsigned group =
			m->effective_iff == 0	? 0
			: m->effective_iff == 2 ? 3
			: (m->effective_iff == 1 || m->effective_iff == 3 ||
			   m->effective_iff == 4)
				? 2
				: 1;
		icon(map, m, frames, o->object_type, group);
		m->movement_visible = o->mobj && o->mobj->family == 0;
		m->move_x = snap->move_q15[0];
		m->move_y = snap->move_q15[1];
		if (snap->orient_dirty) {
			int16_t b = (int16_t)(0u - snap->yaw),
				a = (int16_t)(0xc000u - snap->pitch);
			int16_t sn = (int16_t)(sin(b * 0.000095873722f) *
					       32767),
				cs = (int16_t)(cos(b * 0.000095873722f) *
					       32767),
				cp = (int16_t)(cos(a * 0.000095873722f) *
					       32767);
			m->move_x = (int16_t)-((-sn * (int)cp) >> 15);
			m->move_y = (int16_t)-((cs * (int)cp) >> 15);
		}
		m->box_visible =
			p->target_box_enabled && !g_replay_view_mode &&
			(slot == p->view_state.camera_focus_obj_idx ||
			 slot == (unsigned)p->current_target_object_idx ||
			 (box && other_player_box(p, o, slot)));
		static const uint8_t line_colors[6] = {63, 55, 51, 59, 55, 59};
		m->box_color = slot == p->view_state.camera_focus_obj_idx ? 47
			       : slot == (unsigned)p->current_target_object_idx
				       ? 59
				       : line_colors[m->effective_iff < 6
							     ? m->effective_iff
							     : 3];
		m->line_color_argb = xvt_render_draw_color(
			line_colors[m->effective_iff < 6 ? m->effective_iff
							 : 3]);
		static const unsigned dark_colors[6] = {61, 53, 49,
							57, 53, 213};
		m->label_color_argb = xvt_render_draw_color(
			dark_colors[m->effective_iff < 6 ? m->effective_iff
							 : 3]);
		m->overlay_visible =
			genus != CRAFT_GENUS_SMALL_DEBRIS &&
			genus != CRAFT_GENUS_EXPLOSION &&
			(slot < (unsigned)g_craft_data_pool_capacity ||
			 !o->mobj || o->mobj->p_craft);
		if (m->overlay_visible) {
			label(map, m, o);
		}
		range(m, p, o, genus);
		if (slot == (unsigned)p->current_target_object_idx) {
			map->target = snap->id;
			endpoint(map, slot);
		}
	}
}
