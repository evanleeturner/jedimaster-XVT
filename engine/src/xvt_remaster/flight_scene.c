#include <stdlib.h>
#include <string.h>

#include "aeron/asset/opt_model.h"
#include "xvt_remaster/component_animation.h"
#include "xvt_remaster/config.h"
#include "xvt_remaster/effects.h"
#include "xvt_remaster/engine_glows.h"
#include "xvt_remaster/flight.h"
#include "xvt_remaster/flight_map.h"
#include "xvt_remaster/flight_pipeline.h"
#include "xvt_remaster/hud_renderer.h"
#include "xvt_remaster/lighting.h"
#include "xvt_remaster/ship.h"
#include "xvt_remaster/sky.h"

static AeronScene3D *g_scene;
static AeronSceneMeshTable *g_tables;
static int g_width;
static int g_height;
static int g_samples;
static int g_output_valid;

static int ensure(int width, int height)
{
	int samples = xvt_remaster_config_effective()->msaa_samples;
	if (!g_tables) {
		g_tables = calloc(XVT_SNAP_OBJECTS * 3, sizeof *g_tables);
	}
	if (!g_tables) {
		return 0;
	}
	if (g_scene && g_width == width && g_height == height &&
	    samples == g_samples) {
		return 1;
	}
	AeronScene3D *scene = AeronScene_Create(&(AeronScene3DDesc){
		.rt_width = width,
		.rt_height = height,
		.color_format = AERON_TEXTURE_FORMAT_RGBA16_FLOAT,
		.with_normal_rt = 1,
		.sample_count = (AeronSampleCount)samples,
		.view_space_to_meters = AERON_OPT_METERS_PER_UNIT});
	if (!scene) {
		return 0;
	}
	AeronScene_SetClearColor(scene, (const float[4]){0, 0, 0, 1});
	xvt_flight_pipeline_forget_sources();
	AeronScene_Destroy(g_scene);
	g_scene = scene;
	g_width = width;
	g_height = height;
	g_samples = samples;
	g_output_valid = 0;
	return 1;
}

static int eligible(const struct xvt_render_snapshot *s,
		    const struct xvt_snap_object *o)
{
	if (o->slot_class == XVT_SLOT_STATIC &&
	    (o->genus < CRAFT_GENUS_MINE ||
	     o->genus > CRAFT_GENUS_SMALL_DEBRIS)) {
		return 0;
	}
	if (o->slot_class != XVT_SLOT_STATIC &&
	    o->genus > CRAFT_GENUS_OTHER_PROJECTILE &&
	    o->genus != CRAFT_GENUS_SMALL_DEBRIS &&
	    o->genus != CRAFT_GENUS_EXPLOSION &&
	    o->genus != CRAFT_GENUS_OBSTACLE) {
		return 0;
	}
	if (o->slot_class == XVT_SLOT_LOCAL_TRANSIENT &&
	    (!s->sky.debris_enabled || s->sky.proving_grounds ||
	     s->camera.hyperspace_phase == XVT_SNAP_HYPERSPACE_TRANSITION)) {
		return 0;
	}
	return 1;
}

int xvt_remaster_flight_prepare_resources(int width, int height)
{
	AeronScene3D *previous = g_scene;
	return ensure(width, height) &&
	       (previous == g_scene ||
		xvt_flight_pipeline_prepare_scene_resources(g_scene, 1));
}

static void draw_hud_after_upscale(AeronCommandBuffer *cmd,
				   AeronRenderPass *pass, int width, int height,
				   void *user)
{
	(void)width;
	(void)height;
	xvt_hud_renderer_draw(cmd, pass,
			      AeronScene_SceneRt((AeronScene3D *)user));
}

/* Records the flight view for one frame (or the map, in map mode): sky and
 * lighting, then for each eligible object its mesh, engine glows, previous pose
 * for motion blur and its hull or shadow caster, then effects and the HUD pass.
 * It stays one function because each object's steps read and change the same
 * instance, pose and frame flags. */
int xvt_remaster_flight_render(AeronCommandBuffer *cmd,
			       const struct xvt_render_snapshot *s,
			       const struct xvt_render_snapshot *p)
{
	const struct xvt_prepared_flight *frame = xvt_remaster_flight_current();
	if (!frame || !s->flight_valid) {
		g_output_valid = 0;
		return 1;
	}
	if (s->camera.map_mode) {
		if (!frame->render_needed && g_output_valid) {
			return 1;
		}
		g_output_valid = xvt_flight_map_render(cmd, s, &frame->view);
		return g_output_valid;
	}
	int recreate =
		!g_scene || g_width != frame->view.camera.viewport.width ||
		g_height != frame->view.camera.viewport.height ||
		g_samples != xvt_remaster_config_effective()->msaa_samples;
	if (!frame->render_needed && !recreate && g_output_valid) {
		return 1;
	}
	if (!ensure(frame->view.camera.viewport.width,
		    frame->view.camera.viewport.height) ||
	    !xvt_flight_pipeline_begin(g_scene, s, frame,
				       frame->reset_history || recreate)) {
		return 0;
	}
	if (!xvt_sky_prepare(cmd, g_scene, s, &frame->view)) {
		return 0;
	}
	if (!xvt_lighting_begin(g_scene, s)) {
		return 0;
	}
	xvt_remaster_ship_set_environment(g_scene, &s->lighting, NULL,
					  frame->view.camera.pos);
	int streaks =
		s->hyperspace.phase == XVT_SNAP_HYPERSPACE_TRANSITION &&
		s->hyperspace.elapsed_ticks < XVT_SNAP_HYPERSPACE_STREAK_END;
	const struct xvt_render_settings *settings =
		xvt_remaster_config_effective();
	int camera_motion = settings->motion_blur.camera_blur ||
			    settings->temporal_mode != AERON_TEMPORAL_OFF;
	for (unsigned i = 0; !streaks && i < s->object_count; ++i) {
		const struct xvt_snap_object *object = &s->objects[i];
		if (!eligible(s, object)) {
			continue;
		}
		struct xvt_ship_selection selection;
		if (!xvt_remaster_ship_select(s, object, &selection)) {
			continue;
		}
		const struct xvt_mesh_asset *asset =
			xvt_remaster_ship_mesh(s, selection.asset_id);
		if (!asset) {
			continue;
		}
		const struct xvt_prepared_object *pose = &frame->objects[i];
		int hidden_owner = object->id.slot == s->camera.focus.slot &&
				   !s->camera.external &&
				   !s->camera.replay_view;
		AeronSceneMeshTable *table = &g_tables[i * 3];
		AeronSceneMeshTable *previous_table = &g_tables[i * 3 + 1];
		float visual[XVT_SNAP_COMPONENTS];
		xvt_remaster_ship_build_mesh_table(
			asset, object, selection.component,
			xvt_component_animation_angles(s, object, asset,
						       visual),
			table);
		AeronSceneMeshInstance instance = {
			.mesh = asset->mesh,
			.variant = object->node_switch,
			.mesh_table = table,
			.prev_mesh_table = table,
			.zero_velocity = pose->zero_velocity || recreate ||
					 !frame->regenerate_motion,
			.cull_mode = AERON_CULL_BACK};
		memcpy(instance.transform, pose->transform,
		       sizeof instance.transform);
		memcpy(instance.prev_transform, pose->previous_transform,
		       sizeof instance.prev_transform);
		if (!xvt_engine_glows_submit(cmd, g_scene, s, object, asset,
					     table, instance.transform,
					     !hidden_owner)) {
			return 0;
		}
		if (hidden_owner) {
			continue;
		}
		if (frame->regenerate_motion && !instance.zero_velocity && p &&
		    pose->previous_index >= 0 &&
		    (unsigned)pose->previous_index < p->object_count) {
			const struct xvt_snap_object *old =
				&p->objects[pose->previous_index];
			struct xvt_ship_selection old_selection;
			if (xvt_remaster_ship_select(p, old, &old_selection) &&
			    old_selection.asset_id == selection.asset_id) {
				if (s->flight_unlocked ||
				    old_selection.component !=
					    selection.component ||
				    memcmp(old->mesh_rotation,
					   object->mesh_rotation,
					   sizeof object->mesh_rotation) ||
				    memcmp(old->component_state,
					   object->component_state,
					   sizeof object->component_state)) {
					float prior_visual[XVT_SNAP_COMPONENTS];
					xvt_remaster_ship_build_mesh_table(
						asset, old,
						old_selection.component,
						xvt_component_animation_angles(
							p, old, asset,
							prior_visual),
						previous_table);
					instance.prev_mesh_table =
						previous_table;
				}
			} else {
				instance.zero_velocity = 1;
			}
		}
		if (object->genus == CRAFT_GENUS_PLAYER_PROJECTILE ||
		    object->genus == CRAFT_GENUS_OTHER_PROJECTILE) {
			xvt_effects_projectile_matrix(
				object, s->camera.world_pos,
				s->camera.world_pos, instance.transform);
			memcpy(instance.prev_transform, instance.transform,
			       sizeof instance.transform);
			if (!instance.zero_velocity && p &&
			    pose->previous_index >= 0) {
				xvt_effects_projectile_matrix(
					&p->objects[pose->previous_index],
					camera_motion ? p->camera.world_pos
						      : s->camera.world_pos,
					s->camera.world_pos,
					instance.prev_transform);
			}
			instance.base_color_emissive_strength =
				xvt_remaster_config_effective()
					->models
					.opt_projectile_emissive_strength;
			instance.no_local_lights = 1;
			instance.shadow_flags =
				AERON_SCENE_INSTANCE_NO_CAST_SHADOW |
				AERON_SCENE_INSTANCE_NO_RECEIVE_SHADOW;
			instance.cull_mode = AERON_CULL_NONE;
		}
		int full_model =
			object->genus != CRAFT_GENUS_OBSTACLE ||
			object->id.slot == s->sky.checkpoint_slot ||
			object->id.slot == (unsigned)s->sky.checkpoint_slot + 1;
		if (full_model) {
			float radius = xvt_remaster_ship_radius(
				asset, table, instance.transform);
			if (radius > 0) {
				if (xvt_remaster_ship_visible(
					    &frame->view, instance.transform,
					    radius)) {
					AeronScene_AddMeshInstance(g_scene,
								   &instance);
				} else {
					AeronScene_AddShadowCaster(g_scene,
								   &instance);
				}
			}
		}
		if (object->genus == CRAFT_GENUS_OBSTACLE &&
		    asset->component_count) {
			unsigned hull = asset->component_count - 1;
			for (unsigned m = 0; m < asset->component_count &&
					     m < AERON_MAX_MESH_SLOTS;
			     ++m) {
				if (asset->mesh->mesh_rot[m].mesh_type ==
				    XVT_SNAP_MESH_HULL) {
					hull = m;
					break;
				}
			}
			AeronSceneMeshTable *hull_table = &g_tables[i * 3 + 2];
			xvt_remaster_ship_build_mesh_table(
				asset, object, (uint16_t)hull,
				xvt_component_animation_angles(s, object, asset,
							       visual),
				hull_table);
			AeronSceneMeshInstance selected = instance;
			selected.mesh_table = hull_table;
			selected.prev_mesh_table = hull_table;
			selected.variant = 1;
			float bound = xvt_remaster_ship_radius(
				asset, hull_table, selected.transform);
			if (xvt_remaster_ship_visible(
				    &frame->view, selected.transform, bound)) {
				AeronScene_AddMeshInstance(g_scene, &selected);
			} else {
				AeronScene_AddShadowCaster(g_scene, &selected);
			}
		}
	}
	if (!streaks) {
		xvt_effects_submit(
			g_scene, s, p, &s->camera,
			p ? (camera_motion ? &p->camera : &s->camera) : NULL,
			frame->regenerate_motion && !frame->reset_history,
			NULL);
	}
	AeronScene_SetPassHook(g_scene, AERON_SCENE_HOOK_AFTER_UPSCALE,
			       draw_hud_after_upscale, g_scene);
	if (!xvt_flight_pipeline_finish(cmd, g_scene)) {
		return 0;
	}
	g_output_valid = 1;
	return 1;
}

AeronTexture *xvt_remaster_flight_output(void)
{
	return g_output_valid ? xvt_flight_pipeline_output() : NULL;
}

void xvt_remaster_flight_shutdown(void)
{
	xvt_flight_map_shutdown();
	xvt_sky_shutdown();
	xvt_engine_glows_shutdown();
	xvt_flight_pipeline_shutdown();
	AeronScene_Destroy(g_scene);
	g_scene = NULL;
	free(g_tables);
	g_tables = NULL;
	g_output_valid = 0;
	xvt_remaster_flight_invalidate();
}
