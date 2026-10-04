#include "xvt_runtime/snapshot/render_capture.h"

#include <string.h>

#include "aeron/aeron.h"
#include "xvt/assets/model_preview.h"
#include "xvt/assets/object_type.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/fediskio.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/hud/hud.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/flight/proving_grounds.h"
#include "xvt/flight/transfm2.h"
#include "xvt/render/backdrop.h"
#include "xvt/render/flight_palette.h"
#include "xvt/render/renderer.h"
#include "xvt/util/memory.h"
#include "xvt_runtime/log/log.h"
#include "xvt_runtime/runtime/flight_task.h"
#include "xvt_runtime/runtime/presentation.h"
#include "xvt_runtime/snapshot/cockpit_capture.h"
#include "xvt_runtime/snapshot/cockpit_messages.h"
#include "xvt_runtime/snapshot/render_assets.h"
#include "xvt_runtime/snapshot/render_camera.h"
#include "xvt_runtime/snapshot/render_frontend.h"
#include "xvt_runtime/snapshot/render_hud.h"
#include "xvt_runtime/snapshot/render_map.h"
#include "xvt_runtime/timing/flight_timing.h"

/* One in-progress view; completed views live in the root snapshot slots.
 * Failed later flips cannot overwrite an earlier successful view in the tick. */
static struct {
	struct xvt_snap_camera camera;
	struct xvt_snap_lighting lighting;
	struct xvt_snap_map map;
	struct xvt_snap_preview crt;
	struct xvt_snap_sky sky;
	struct xvt_snap_hyperspace hyperspace;
	struct xvt_snap_object objects[XVT_SNAP_OBJECTS];
	struct xvt_snap_type types[XVT_SNAP_TYPES];
	int16_t fuselage[25];
	uint32_t object_count;
	uint32_t dropped;
	int32_t time;
	uint64_t component_event_serial;
	int32_t component_event_time;
	uint8_t flight_unlocked;
	int valid;
	int sealed;
} g_pending;

struct xvt_authoritative_pose {
	int32_t position[3];
	uint16_t signature;
	uint16_t yaw;
	uint16_t pitch;
	uint16_t roll;
	uint8_t type;
	uint8_t mesh_rotation[50];
};

static struct xvt_authoritative_pose g_authoritative_poses[XVT_SNAP_OBJECTS];
static struct xvt_authoritative_pose g_candidate_poses[XVT_SNAP_OBJECTS];
static int g_candidate_tick = -1;
static int g_authoritative_tick = -1;
static int g_network_correction;

static uint64_t g_mission;
static uint64_t g_world;
static uint64_t g_serial;
static int g_active;
static int g_published;
static int32_t g_last_view_time;
static int g_has_view_time;
static uint32_t g_last_dropped;
static unsigned g_overlay_depth;

void xvt_render_capture_begin_overlay(void)
{
	xvt_presentation_require_classic();
	if (!g_overlay_depth++) {
		xvt_cockpit_retain_presented_frame();
	}
}

void xvt_render_capture_end_overlay(void)
{
	if (g_overlay_depth) {
		--g_overlay_depth;
	}
}

void xvt_render_capture_reset(void)
{
	xvt_cockpit_reset();
	xvt_cockpit_messages_clear_progress();
	memset(&g_pending, 0, sizeof g_pending);
	g_mission = 0;
	g_world = 0;
	g_serial = 0;
	g_active = 0;
	g_published = 0;
	g_has_view_time = 0;
	g_last_dropped = 0;
	g_overlay_depth = 0;
}

void xvt_render_capture_begin_frame(void)
{
	g_pending.valid = 0;
	g_pending.sealed = 0;
	g_published = 0;
}

static void invalidate_world_history(void)
{
	g_authoritative_tick = -1;
	g_candidate_tick = -1;
	g_network_correction = 0;
	++g_world;
	g_pending.valid = 0;
	g_pending.sealed = 0;
	g_published = 0;
	g_has_view_time = 0;
	struct xvt_render_snapshot *writer = xvt_render_snapshot_writer();
	if (writer) {
		writer->flight_valid = 0;
		writer->camera.valid = 0;
	}
}

void xvt_render_capture_world_changed(void)
{
	xvt_cockpit_reset();
	invalidate_world_history();
}

void xvt_render_capture_begin_mission(void)
{
	xvt_presentation_require_classic();
	xvt_render_hud_reset();
	++g_mission;
	g_active = 1;
	xvt_render_capture_world_changed();
}

void xvt_render_capture_end_mission(void)
{
	xvt_presentation_require_classic();
	g_active = 0;
	xvt_render_capture_world_changed();
}

static struct xvt_snap_object_id object_id(unsigned slot, unsigned capacity)
{
	struct xvt_snap_object_id id = {UINT16_MAX, 0};
	if (slot < capacity && g_object_table[slot].object_type) {
		id.slot = (uint16_t)slot;
		id.signature = g_object_table[slot].object_signature;
	}
	return id;
}

static void capture_camera(struct xvt_snap_camera *out,
			   struct xvt_snap_lighting *lighting,
			   unsigned capacity)
{
	const struct player_data *p = &g_players[g_local_player];
	const struct player_view_state *v = &p->view_state;
	memset(out, 0, sizeof *out);
	out->world_pos[0] = v->camera_world_x;
	out->world_pos[1] = v->camera_world_y;
	out->world_pos[2] = v->camera_world_z;
	xvt_render_camera_copy_rows(out->rows);
	out->viewport =
		(struct xvt_snap_rect){g_flight_vp_x, g_flight_vp_y,
				       g_flight_vp_width, g_flight_vp_height};
	out->center_x = g_flight_vp_center_x;
	out->center_y = g_flight_vp_center_y;
	out->projection_offset_y = g_proj_offset_y;
	out->screen_width = (uint16_t)g_screen_width;
	out->screen_height = (uint16_t)g_screen_height;
	out->aspect_y_q16 = g_proj_aspect_y;
	out->perspective_shift = g_perspective_shift;
	out->player = object_id(p->object_index, capacity);
	out->focus = object_id(v->camera_focus_obj_idx, capacity);
	out->view_pitch = (uint16_t)v->view_pitch;
	out->view_yaw = (uint16_t)v->view_yaw;
	out->view_roll = (uint16_t)v->view_roll;
	out->view_up_axis_angle = (uint16_t)v->view_angle_d;
	out->hud_aim_x = v->hud_aim_x;
	out->hud_aim_y = v->hud_aim_y;
	out->external = v->external_camera_active;
	out->replay_view = g_replay_view_mode;
	out->map_mode = p->map_camera_state;
	out->hyperspace_phase = p->hyperspace_phase;
	out->hud_state = v->hud_state_live;
	out->valid = g_flight_vp_width && g_flight_vp_height &&
		     g_screen_width && g_screen_height;
	*lighting = (struct xvt_snap_lighting){{g_world_light_direction_x,
						g_world_light_direction_y,
						g_world_light_direction_z},
					       g_local_lights_enabled,
					       g_dir_lighting_enabled};
}

static void capture_object(unsigned slot)
{
	const struct object_record *o = &g_object_table[slot];
	if (!o->object_type) {
		return;
	}
	if (g_pending.object_count == XVT_SNAP_OBJECTS) {
		++g_pending.dropped;
		return;
	}
	struct xvt_snap_object *out =
		&g_pending.objects[g_pending.object_count++];
	memset(out, 0, sizeof *out);
	out->id = (struct xvt_snap_object_id){(uint16_t)slot,
					      o->object_signature};
	out->object_type = o->object_type;
	out->genus = o->genus_id;
	out->flight_group = o->flight_group_idx;
	out->slot_class =
		slot >= (unsigned)g_region_main_object_slot_end
			? XVT_SLOT_STATIC
			: (slot >= (unsigned)g_local_transient_slot_start &&
					   slot < (unsigned)
							   g_local_debris_slot_end
				   ? XVT_SLOT_LOCAL_TRANSIENT
				   : XVT_SLOT_MAIN);
	out->world_pos[0] = o->world_x;
	out->world_pos[1] = o->world_y;
	out->world_pos[2] = o->world_z;
	memcpy(out->prev_world_pos, out->world_pos, sizeof out->world_pos);
	out->player_owner = o->player_owner_idx;
	out->yaw = o->yaw;
	out->pitch = o->pitch;
	out->roll = o->roll;
	out->type_specific_word = o->type_specific_word;
	memcpy(out->type_specific, o->type_specific_byte,
	       sizeof out->type_specific);
	const struct mobile_object *m = o->mobj;
	if (!m) {
		return;
	}
	out->has_mobile = 1;
	out->family = m->family;
	out->light_scale = m->effect_size;
	out->prev_world_pos[0] = m->prev_world_x;
	out->prev_world_pos[1] = m->prev_world_y;
	out->prev_world_pos[2] = m->prev_world_z;
	out->source_slot = m->source_obj_idx;
	out->source_type = m->source_object_type;
	out->iff = m->iff;
	out->team = m->team;
	out->node_switch = m->node_switch_index;
	out->speed = m->speed;
	out->move_dirty = m->move_vector_dirty;
	out->orient_dirty = m->orient_matrix_dirty;
	out->move_q15[0] = m->move_x;
	out->move_q15[1] = m->move_y;
	out->move_q15[2] = m->move_z;
	const int16_t rows[9] = {
		m->cached_side_x, m->cached_side_y, m->cached_side_z,
		m->cached_fwd_x,  m->cached_fwd_y,  m->cached_fwd_z,
		m->cached_up_x,	  m->cached_up_y,   m->cached_up_z};
	memcpy(out->cached_rows_q15, rows, sizeof rows);
	const struct craft_data *c = m->p_craft;
	if (!c) {
		return;
	}
	out->has_craft = 1;
	out->sfoil_state = c->s_foil_state;
	out->object_kind = c->object_kind;
	out->working_subsystems = c->working_subsystems;
	out->installed_subsystems = c->system_flags;
	out->throttle = c->throttle_speed;
	out->overdrive_off = c->engine_overdrive_off;
	out->max_speed = c->ai_flight.max_speed_cache;
	out->laser_recharge_level = c->laser_recharge_level;
	out->shield_recharge_level = c->shield_recharge_level;
	out->beam_recharge_level = c->beam_recharge_level;
	memcpy(out->component_state, c->component_state,
	       sizeof out->component_state);
	memcpy(out->component_hp, c->component_hp, sizeof out->component_hp);
	memcpy(out->mesh_rotation, c->mesh_rotation, sizeof out->mesh_rotation);
}

struct sequence {
	const int16_t *data;
	size_t count;
};

static const struct sequence g_sequences[] = {
	{g_object_type127_texture_frame_sequence,
	 sizeof g_object_type127_texture_frame_sequence / sizeof(int16_t)},
	{g_object_type131_texture_frame_sequence,
	 sizeof g_object_type131_texture_frame_sequence / sizeof(int16_t)},
	{g_object_type132_texture_frame_sequence,
	 sizeof g_object_type132_texture_frame_sequence / sizeof(int16_t)},
	{g_object_type133_texture_frame_sequence,
	 sizeof g_object_type133_texture_frame_sequence / sizeof(int16_t)},
	{g_object_type134_texture_frame_sequence,
	 sizeof g_object_type134_texture_frame_sequence / sizeof(int16_t)},
	{g_object_type157_texture_frame_sequence,
	 sizeof g_object_type157_texture_frame_sequence / sizeof(int16_t)},
	{g_fuselage_damage_texture_frame_sequence,
	 sizeof g_fuselage_damage_texture_frame_sequence / sizeof(int16_t)},
	{g_object_type110_texture_frame_sequence,
	 sizeof g_object_type110_texture_frame_sequence / sizeof(int16_t)},
	{g_object_type111_texture_frame_sequence,
	 sizeof g_object_type111_texture_frame_sequence / sizeof(int16_t)},
	{g_object_type112_texture_frame_sequence,
	 sizeof g_object_type112_texture_frame_sequence / sizeof(int16_t)},
	{g_object_type113_texture_frame_sequence,
	 sizeof g_object_type113_texture_frame_sequence / sizeof(int16_t)},
	{g_object_type128_texture_frame_sequence,
	 sizeof g_object_type128_texture_frame_sequence / sizeof(int16_t)},
	{g_object_type129_texture_frame_sequence,
	 sizeof g_object_type129_texture_frame_sequence / sizeof(int16_t)},
	{g_object_type130_texture_frame_sequence,
	 sizeof g_object_type130_texture_frame_sequence / sizeof(int16_t)}};

static void capture_types(void)
{
	for (unsigned i = 0; i < XVT_SNAP_TYPES; ++i) {
		const struct object_type_info *t = &g_object_type_table[i];
		struct xvt_snap_type *out = &g_pending.types[i];
		memset(out, 0, sizeof *out);
		uint64_t id = xvt_render_assets_handle_id(t->resource_handle);
		if (t->asset_flags & 1) {
			out->model_asset_id = id;
		} else if (t->asset_flags & 2) {
			out->texture_asset_id = id;
		}
		out->max_extent = t->max_bounds_extent;
		out->half_extent = t->half_bounds_extent;
		out->record_flags = t->record_flags;
		out->asset_flags = t->asset_flags;
		out->behavior_flags = t->behavior_flags;
		out->model_index = t->model_index;
		out->family = t->family_id;
		out->genus = t->genus_id;
		out->texture_group = t->texture_group;
		out->resource_entry = t->resource_index;
		for (unsigned j = 0;
		     j < sizeof g_sequences / sizeof g_sequences[0]; ++j) {
			if (t->texture_frame_sequence != g_sequences[j].data) {
				continue;
			}
			out->sequence_count = (uint8_t)g_sequences[j].count;
			memcpy(out->sequence, g_sequences[j].data,
			       g_sequences[j].count * sizeof(int16_t));
			break;
		}
		for (unsigned j = 0; j < 17; ++j) {
			if (t->palette == g_object_type_palette_remaps[j]) {
				out->remap_count = 16;
			}
		}
		if (t->palette == g_object_type110_palette ||
		    t->palette == g_object_type111_palette ||
		    t->palette == g_object_type112_palette ||
		    t->palette == g_object_type113_palette) {
			out->remap_count = 16;
		}
		if (out->remap_count) {
			memcpy(out->remap, t->palette, out->remap_count);
		}
		if ((t->texture_frame_sequence && !out->sequence_count) ||
		    (t->palette && !out->remap_count)) {
			++g_pending.dropped;
		}
	}
	memcpy(g_pending.fuselage, g_fuselage_damage_texture_frame_sequence,
	       sizeof g_pending.fuselage);
}

void xvt_render_capture_capture_view(void)
{
	xvt_render_draw_scope(XVT_SCOPE_WORLD);
	g_pending.valid = 0;
	g_pending.sealed = 0;
	g_pending.object_count = 0;
	g_pending.dropped = 0;
	if (!g_active || !xvt_render_snapshot_writer() || !g_object_table ||
	    !g_object_table_handle || (unsigned)g_local_player >= 8) {
		return;
	}
	size_t capacity =
		g_handle_tables.size_table[g_object_table_handle - 1] /
		sizeof(struct object_record);
	if (g_handle_tables.ptr_table[g_object_table_handle - 1] !=
		    g_object_table ||
	    g_region_main_object_slot_end < 0 ||
	    g_region_static_object_slot_count < 0) {
		return;
	}
	size_t end = (size_t)g_region_main_object_slot_end +
		     (size_t)g_region_static_object_slot_count;
	if (end > capacity) {
		end = capacity;
		++g_pending.dropped;
	}
	if (end > UINT16_MAX) {
		end = UINT16_MAX;
	}
	capture_camera(&g_pending.camera, &g_pending.lighting, (unsigned)end);
	g_pending.component_event_serial = xvt_flight_timing_animation_serial();
	g_pending.component_event_time = xvt_flight_timing_animation_time();
	g_pending.flight_unlocked = xvt_flight_timing_is_unlocked();
	g_pending.sky.debris_enabled = g_debris_enabled;
	g_pending.sky.proving_grounds =
		g_flight_mission_state.proving_grounds_mode_active;
	g_pending.sky.star_grid_divisor = g_star_grid_divisor;
	g_pending.sky.backdrop_enabled = g_backdrops_enabled;
	g_pending.sky.checkpoint_slot =
		g_proving_grounds_current_checkpoint_obj_idx;
	g_pending.sky.craft_slot_end =
		(uint16_t)g_active_region_craft_object_slot_end;
	memcpy(g_pending.sky.backdrop_types, g_backdrop_model_types,
	       sizeof g_pending.sky.backdrop_types);
	memcpy(g_pending.sky.backdrop_directions, g_backdrop_packed_directions,
	       sizeof g_pending.sky.backdrop_directions);
	/* Counts and records share the recovered Y, X, Z face order. */
	const uint16_t counts[6] = {
		g_backdrop_positive_y_count, g_backdrop_negative_y_count,
		g_backdrop_positive_x_count, g_backdrop_negative_x_count,
		g_backdrop_positive_z_count, g_backdrop_negative_z_count};
	memcpy(g_pending.sky.direction_counts, counts, sizeof counts);
	g_pending.hyperspace.phase = g_players[g_local_player].hyperspace_phase;
	g_pending.hyperspace.elapsed_ticks =
		g_players[g_local_player]
			.hyperspace_runtime.phase_elapsed_ticks;
	g_pending.hyperspace.iff = g_players[g_local_player].iff;
	g_pending.hyperspace.valid =
		g_pending.hyperspace.phase == XVT_SNAP_HYPERSPACE_TRANSITION;
	g_pending.hyperspace.count = 0;
	for (unsigned i = 0; i < end; ++i) {
		capture_object(i);
	}
	xvt_render_map_capture(&g_pending.map, g_pending.objects,
			       g_pending.object_count);
	if (g_pending.map.active) {
		xvt_render_draw_scope(XVT_SCOPE_MAP);
	}
	capture_types();
	g_pending.time = g_game_time;
	g_pending.valid = g_pending.camera.valid;
}

void xvt_render_capture_seal_view(void)
{
	xvt_cockpit_seal(&g_pending.crt);
	g_pending.sealed = g_pending.valid;
}

void xvt_render_capture_end_presentation(void)
{
	g_pending.valid = 0;
	g_pending.sealed = 0;
}

void xvt_render_capture_presented(int succeeded)
{
	struct xvt_render_snapshot *out = xvt_render_snapshot_writer();
	if (!succeeded || !out) {
		return;
	}
	if (!g_pending.sealed) {
		if (g_overlay_depth) {
			xvt_cockpit_presented(1);

			xvt_render_frontend_flight_ui_scene(
				xvt_flight_task_is_loading()
					? XVT_SCENE_LOADING
					: XVT_SCENE_FLIGHT);
		}
		return;
	}
	xvt_cockpit_presented(0);
	if (g_has_view_time && g_pending.time < g_last_view_time) {
		++g_world;
	}
	if (xvt_flight_timing_is_network125() &&
	    g_candidate_tick == g_pending.time) {
		memcpy(g_authoritative_poses, g_candidate_poses,
		       sizeof g_authoritative_poses);
		g_authoritative_tick = g_candidate_tick;
	}
	g_last_view_time = g_pending.time;
	g_has_view_time = 1;
	out->camera = g_pending.camera;
	out->lighting = g_pending.lighting;
	out->map = g_pending.map;
	xvt_render_hud_publish(out);

	xvt_render_frontend_presented_scene(XVT_SCENE_FLIGHT);
	out->sky = g_pending.sky;
	out->hyperspace = g_pending.hyperspace;
	out->object_count = g_pending.object_count;
	memcpy(out->objects, g_pending.objects,
	       out->object_count * sizeof out->objects[0]);
	memcpy(out->types, g_pending.types, sizeof out->types);
	memcpy(out->fuselage_sequence, g_pending.fuselage,
	       sizeof out->fuselage_sequence);
	out->view_time_ticks = g_pending.time;
	out->component_event_serial = g_pending.component_event_serial;
	out->component_event_time = g_pending.component_event_time;
	out->flight_unlocked = g_pending.flight_unlocked;
	out->flight_frame_serial = ++g_serial;
	out->flight_valid = 1;
	out->dropped_records += g_pending.dropped;
	if (g_pending.dropped && g_pending.dropped != g_last_dropped) {
		XVT_LOG_WARN("snapshot.records_dropped count=%u",
			     g_pending.dropped);
	}
	g_last_dropped = g_pending.dropped;
	g_published = 1;
	g_pending.sealed = 0;
}

void xvt_render_capture_commit(struct xvt_render_snapshot *out,
			       const struct xvt_render_snapshot *previous)
{
	xvt_cockpit_export(&out->cockpit);
	xvt_cockpit_export_resources(&out->cockpit_resources);
	out->mission_generation = g_mission;
	out->world_generation = g_world;
	out->flight_frame_serial = g_serial;

	if (!g_active) {
		out->flight_valid = 0;
		out->camera.valid = 0;
		out->object_count = 0;
		return;
	}
	if (g_published) {
		return;
	}
	if (!previous || !previous->flight_valid ||
	    previous->mission_generation != g_mission ||
	    previous->world_generation != g_world) {
		return;
	}
	out->camera = previous->camera;
	out->lighting = previous->lighting;
	out->map = previous->map;
	out->target_box_count = previous->target_box_count;
	memcpy(out->target_boxes, previous->target_boxes,
	       previous->target_box_count * sizeof out->target_boxes[0]);
	out->sky = previous->sky;
	out->hyperspace = previous->hyperspace;
	out->object_count = previous->object_count;
	memcpy(out->objects, previous->objects,
	       out->object_count * sizeof out->objects[0]);
	memcpy(out->types, previous->types, sizeof out->types);
	memcpy(out->fuselage_sequence, previous->fuselage_sequence,
	       sizeof out->fuselage_sequence);
	out->view_time_ticks = previous->view_time_ticks;
	out->component_event_serial = previous->component_event_serial;
	out->component_event_time = previous->component_event_time;
	out->flight_unlocked = previous->flight_unlocked;
	out->flight_frame_serial = previous->flight_frame_serial;
	out->flight_valid = 1;
}

void xvt_render_capture_begin_classic_frame(void)
{
	xvt_cockpit_begin_frame();
	g_pending.crt.valid = 0;
	xvt_render_hud_begin_frame();
}

void xvt_render_capture_hyperspace(unsigned count, const int *x, const int *y,
				   const int *z, const int *half_width,
				   const int *roll)
{
	if (!g_pending.valid || count > XVT_SNAP_STREAKS) {
		return;
	}
	g_pending.hyperspace.count = count;
	for (unsigned i = 0; i < count; ++i) {
		g_pending.hyperspace.streaks[i] = (struct xvt_snap_streak){
			{x[i], y[i], z[i]}, half_width[i], (uint16_t)roll[i]};
	}
}

void xvt_render_capture_frontend_preview(uint16_t handle,
					 const float position[3],
					 const float orientation[9],
					 float scale, uint16_t node_switch,
					 int x, int y, int width, int height)
{
	struct xvt_render_snapshot *s = xvt_render_snapshot_writer();
	if (!s || width <= 0 || height <= 0 || (unsigned)g_local_player >= 8) {
		return;
	}
	if (s->preview_count == XVT_SNAP_PREVIEWS) {
		++s->dropped_records;
		return;
	}
	struct xvt_snap_preview *out = &s->previews[s->preview_count++];
	memset(out, 0, sizeof *out);
	out->opt_asset_id = xvt_render_assets_handle_id(handle);
	capture_camera(&out->camera, &out->lighting, 0);
	out->camera.screen_width = 640;
	out->camera.screen_height = 480;
	out->camera.valid = 1;
	out->camera.projection_offset_y = g_proj_offset_y;
	memcpy(out->view_pos, position, sizeof out->view_pos);
	memcpy(out->view_orient, orientation, sizeof out->view_orient);
	out->model_scale = scale;
	out->node_switch = node_switch;
	out->component = UINT16_MAX;
	out->object.slot = UINT16_MAX;
	out->mask_index = UINT8_MAX;
	out->destination = (struct xvt_snap_rect){x, y, width, height};

	out->valid = out->opt_asset_id != 0;
}

void xvt_render_capture_crt(int x, int y, int width, int height, int masked)
{
	if (!g_active || !g_object_table_handle ||
	    (unsigned)g_local_player >= 8 || width <= 0 || height <= 0) {
		return;
	}
	unsigned capacity =
		(unsigned)(g_handle_tables
				   .size_table[g_object_table_handle - 1] /
			   sizeof(struct object_record));
	unsigned target = g_players[g_local_player].current_target_object_idx;
	struct xvt_snap_preview *out = &g_pending.crt;
	memset(out, 0, sizeof *out);
	out->object = object_id(target, capacity);
	if (out->object.slot == UINT16_MAX) {
		return;
	}
	const struct object_record *object = &g_object_table[target];
	if (object->object_type >= XVT_SNAP_TYPES) {
		return;
	}
	out->opt_asset_id = xvt_render_assets_handle_id(
		g_object_type_table[object->object_type].resource_handle);
	capture_camera(&out->camera, &out->lighting, capacity);
	out->node_switch = object->mobj ? object->mobj->node_switch_index : 0;
	out->model_scale = 1;
	out->component =
		(uint16_t)g_players[g_local_player].selected_target_component;
	/* The argument requests refreshing the mask, not disabling an existing mask. */
	(void)masked;
	out->mask_index = (uint8_t)(g_hud_instrument_set_base_index / 144);
	out->destination = (struct xvt_snap_rect){x, y, width, height};

	out->valid = out->camera.valid;
}

void xvt_render_capture_crt_marker(int x, int y, int z)
{
	g_pending.crt.component_marker_valid = 1;
	const int v[3] = {x, y, z};
	for (int i = 0; i < 3; ++i) {
		g_pending.crt.component_marker_world[i] =
			(int32_t)((uint32_t)v[i] +
				  (uint32_t)g_pending.crt.camera.world_pos[i]);
	}
}

static struct xvt_authoritative_pose
xvt_render_capture_capture_live_pose(unsigned slot)
{
	struct xvt_authoritative_pose pose = {0};
	const struct object_record *o = &g_object_table[slot];
	if (!o->object_type) {
		return pose;
	}
	pose.type = o->object_type;
	pose.signature = o->object_signature;
	pose.position[0] = o->world_x;
	pose.position[1] = o->world_y;
	pose.position[2] = o->world_z;
	pose.yaw = o->yaw;
	pose.pitch = o->pitch;
	pose.roll = o->roll;
	if (o->mobj && o->mobj->p_craft) {
		memcpy(pose.mesh_rotation, o->mobj->p_craft->mesh_rotation,
		       sizeof pose.mesh_rotation);
	}
	return pose;
}

void xvt_render_capture_check_network_correction(void)
{
	if (!xvt_flight_timing_is_network125() ||
	    g_game_time != g_authoritative_tick) {
		return;
	}
	unsigned end = (unsigned)(g_region_main_object_slot_end +
				  g_region_static_object_slot_count);
	if (end > XVT_SNAP_OBJECTS) {
		end = XVT_SNAP_OBJECTS;
	}
	for (unsigned slot = 0; slot < end; ++slot) {
		if (slot >= (unsigned)g_local_transient_slot_start &&
		    slot < (unsigned)g_local_debris_slot_end) {
			continue;
		}
		struct xvt_authoritative_pose pose =
			xvt_render_capture_capture_live_pose(slot);
		const struct xvt_authoritative_pose *previous =
			&g_authoritative_poses[slot];
		if (pose.type != previous->type ||
		    pose.signature != previous->signature ||
		    pose.yaw != previous->yaw ||
		    pose.pitch != previous->pitch ||
		    pose.roll != previous->roll ||
		    memcmp(pose.position, previous->position,
			   sizeof pose.position) ||
		    memcmp(pose.mesh_rotation, previous->mesh_rotation,
			   sizeof pose.mesh_rotation)) {
			g_network_correction = 1;
			return;
		}
	}
}

void xvt_render_capture_complete_network_world(void)
{
	if (!xvt_flight_timing_is_network125()) {
		return;
	}
	/* Prediction corrections invalidate world interpolation, but the retained
	 * cockpit still represents the classic HUD surface composed by the next frame. */
	if (g_network_correction) {
		invalidate_world_history();
	}
	unsigned end = (unsigned)(g_region_main_object_slot_end +
				  g_region_static_object_slot_count);
	if (end > XVT_SNAP_OBJECTS) {
		end = XVT_SNAP_OBJECTS;
	}
	for (unsigned slot = 0; slot < end; ++slot) {
		g_candidate_poses[slot] =
			xvt_render_capture_capture_live_pose(slot);
	}
	g_candidate_tick = g_game_time;
}

int xvt_render_capture_last_view_tick(void)
{
	return g_has_view_time ? g_last_view_time : -1;
}
