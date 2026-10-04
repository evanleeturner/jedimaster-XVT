#include "xvt_remaster/preview.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#include "aeron/asset/opt_model.h"
#include "aeron/scene/present.h"
#include "xvt_remaster/component_animation.h"
#include "xvt_remaster/config.h"
#include "xvt_remaster/effects.h"
#include "xvt_remaster/flight_pipeline.h"

/* Frontend previews share a fixed scene; the independent CRT follows its displayed size. */
enum {
	PREVIEW_SLOTS = XVT_SNAP_PREVIEWS + 1,
	PREVIEW_WIDTH = 2048,
	PREVIEW_HEIGHT = 1536
};

static AeronScene3D *g_scene;
static AeronScene3D *g_crt_scene;

static struct {
	AeronScene3D *scene;
	int width;
	int height;
	int samples;
} g_crt_views[3];

static AeronRenderTarget *g_targets[PREVIEW_SLOTS];
static struct xvt_preview_output g_outputs[PREVIEW_SLOTS];
static AeronScenePresentChain *g_chain;
static AeronSampler *g_sampler;
static int g_scene_width;
static int g_scene_height;
static int g_samples;
static AeronSceneMeshTable g_table;

struct crt_dependencies {
	uint64_t world;
	uint64_t mission;
	uint64_t config;
	uint64_t models;
	uint64_t textures;
	uint64_t component_pose;
	struct xvt_snap_preview preview;
	struct xvt_snap_lighting effect_lighting;
	struct xvt_snap_type types[XVT_SNAP_TYPES];
	int16_t fuselage[25];
	uint16_t craft_slot_end;
	uint16_t checkpoint;
	uint8_t debris;
	uint8_t proving_grounds;
	int width;
	int height;
	unsigned object_count;
	struct xvt_snap_object objects[XVT_SNAP_OBJECTS];
};

static struct crt_dependencies g_crt_dependencies;
static struct crt_dependencies g_crt_candidate;
static int g_crt_valid;

static int ensure_scene(AeronScene3D **scene, int *old_width, int *old_height,
			int *old_samples, int width, int height)
{
	int samples = xvt_remaster_config_effective()->msaa_samples;
	if (*scene && width == *old_width && height == *old_height &&
	    samples == *old_samples) {
		return 1;
	}
	AeronScene3D *next = AeronScene_Create(&(AeronScene3DDesc){
		.rt_width = width,
		.rt_height = height,
		.color_format = AERON_TEXTURE_FORMAT_RGBA16_FLOAT,
		.with_normal_rt = 1,
		.sample_count = (AeronSampleCount)samples,
		.view_space_to_meters = AERON_OPT_METERS_PER_UNIT});
	if (!next) {
		return 0;
	}
	AeronScene_SetClearColor(next, (const float[4]){0, 0, 0, 0});
	AeronScene_Destroy(*scene);
	*scene = next;
	*old_width = width;
	*old_height = height;
	*old_samples = samples;
	return 1;
}

static int ensure(unsigned slot, int width, int height)
{
	if (slot == XVT_SNAP_PREVIEWS) {
		for (unsigned index = 0; index < 3; ++index) {
			if (g_crt_views[index].scene &&
			    g_crt_views[index].width == width &&
			    g_crt_views[index].height == height) {
				g_crt_scene = g_crt_views[index].scene;
				return 1;
			}
		}
		return 0;
	}
	if (!ensure_scene(&g_scene, &g_scene_width, &g_scene_height, &g_samples,
			  width, height)) {
		return 0;
	}
	if (!g_chain) {
		g_chain = AeronScenePresentChain_Create(
			AERON_TEXTURE_FORMAT_RGBA16_FLOAT);
	}
	if (!g_sampler) {
		g_sampler = Aeron_CreateSampler(&(AeronSamplerDesc){
			.min_filter = AERON_FILTER_LINEAR,
			.mag_filter = AERON_FILTER_LINEAR,
			.address_u = AERON_ADDRESS_CLAMP_TO_EDGE,
			.address_v = AERON_ADDRESS_CLAMP_TO_EDGE});
	}
	if (!g_targets[slot] || width != g_outputs[slot].width ||
	    height != g_outputs[slot].height) {
		AeronRenderTarget *target =
			Aeron_CreateRenderTarget(&(AeronRenderTargetDesc){
				.width = width,
				.height = height,
				.format = AERON_TEXTURE_FORMAT_RGBA16_FLOAT,
				.debug_name = "xvt.preview.present"});
		if (!target) {
			return 0;
		}
		Aeron_DestroyRenderTarget(g_targets[slot]);
		g_targets[slot] = target;
		g_outputs[slot].width = width;
		g_outputs[slot].height = height;
	}
	return g_chain && g_sampler;
}

static const struct xvt_snap_object *
preview_target(const struct xvt_render_snapshot *s,
	       struct xvt_snap_object_id id)
{
	for (unsigned i = 0; i < s->object_count; ++i) {
		if (s->objects[i].id.slot == id.slot &&
		    s->objects[i].id.signature == id.signature) {
			return &s->objects[i];
		}
	}
	return NULL;
}

static int includes_crt_projectile(const struct xvt_snap_object *object,
				   const struct xvt_snap_preview *preview)
{
	if (object->genus != CRAFT_GENUS_PLAYER_PROJECTILE &&
	    object->genus != CRAFT_GENUS_OTHER_PROJECTILE) {
		return 0;
	}
	if (object->id.slot == preview->camera.focus.slot &&
	    !preview->camera.external && !preview->camera.replay_view) {
		return 0;
	}
	return object->source_slot == preview->object.slot ||
	       (!preview->camera.map_mode &&
		object->source_slot == preview->camera.player.slot);
}

void xvt_remaster_preview_invalidate_crt(void)
{
	g_crt_valid = 0;
	g_outputs[XVT_SNAP_PREVIEWS].texture = NULL;
}

int xvt_remaster_preview_prepare_crt_resources(
	const struct xvt_cockpit_resources *resources, int width, int height)
{
	if (!resources->view.screen_width || !resources->view.screen_height) {
		return 0;
	}
	float scale = fminf((float)width / resources->view.screen_width,
			    (float)height / resources->view.screen_height);
	unsigned count = 0;
	for (unsigned index = 0; index < 3; ++index) {
		const struct xvt_snap_hud_element *element =
			&resources->definition.layout
				 .elements[index * HUD_INSTRUMENTS_PER_SET + 2];
		if (!element->selector || !element->color_index) {
			continue;
		}
		int w = (int)ceilf(element->selector * scale);
		int h = (int)ceilf(element->color_index * scale);
		unsigned existing = 0;
		for (; existing < count; ++existing) {
			if (g_crt_views[existing].width == w &&
			    g_crt_views[existing].height == h) {
				break;
			}
		}
		if (existing < count) {
			continue;
		}
		if (!g_crt_views[count].scene ||
		    g_crt_views[count].width != w ||
		    g_crt_views[count].height != h ||
		    g_crt_views[count].samples !=
			    xvt_remaster_config_effective()->msaa_samples) {
			xvt_remaster_preview_invalidate_crt();
		}
		AeronScene3D *previous = g_crt_views[count].scene;
		if (!ensure_scene(&g_crt_views[count].scene,
				  &g_crt_views[count].width,
				  &g_crt_views[count].height,
				  &g_crt_views[count].samples, w, h)) {
			return 0;
		}
		if (previous != g_crt_views[count].scene &&
		    !xvt_flight_pipeline_prepare_scene_resources(
			    g_crt_views[count].scene, 0)) {
			return 0;
		}
		++count;
	}
	for (unsigned index = count; index < 3; ++index) {
		if (g_crt_views[index].scene) {
			xvt_remaster_preview_invalidate_crt();
		}
		AeronScene_Destroy(g_crt_views[index].scene);
		memset(&g_crt_views[index], 0, sizeof g_crt_views[index]);
	}
	return 1;
}

int xvt_remaster_preview_crt_needs_render(const struct xvt_render_snapshot *s,
					  int width, int height)
{
	const struct xvt_snap_preview *preview = &s->cockpit.crt;
	if (!preview->valid || !preview_target(s, preview->object)) {
		xvt_remaster_preview_invalidate_crt();
		return 0;
	}
	struct crt_dependencies *key = &g_crt_candidate;
	memset(key, 0, offsetof(struct crt_dependencies, objects));
	key->component_pose = s->flight_unlocked
				      ? xvt_component_animation_object_revision(
						preview->object.slot)
				      : 0;
	key->world = s->world_generation;
	key->mission = s->mission_generation;
	key->config = xvt_remaster_config_generation();
	key->models = s->opt_asset_generation;
	key->textures = s->texture_asset_generation;
	key->preview = *preview;
	memset(&key->preview.draw, 0, sizeof key->preview.draw);
	key->preview.component_marker_valid = 0;
	memset(key->preview.component_marker_world, 0,
	       sizeof key->preview.component_marker_world);
	key->preview.component = 0;
	key->preview.destination.x = 0;
	key->preview.destination.y = 0;
	key->effect_lighting = s->lighting;
	memcpy(key->types, s->types, sizeof key->types);
	memcpy(key->fuselage, s->fuselage_sequence, sizeof key->fuselage);
	key->craft_slot_end = s->sky.craft_slot_end;
	key->checkpoint = s->sky.checkpoint_slot;
	key->debris = s->sky.debris_enabled;
	key->proving_grounds = s->sky.proving_grounds;
	key->width = width;
	key->height = height;
	/* Animation is already resolved into type/component frame state. No host tick
	 * or wall clock participates in these transparent CRT draws. */
	for (unsigned i = 0; i < s->object_count; ++i) {
		const struct xvt_snap_object *object = &s->objects[i];
		if ((object->id.slot == preview->object.slot &&
		     object->id.signature == preview->object.signature) ||
		    includes_crt_projectile(object, preview) ||
		    object->genus == CRAFT_GENUS_EXPLOSION) {
			key->objects[key->object_count++] = *object;
		}
	}
	size_t bytes = offsetof(struct crt_dependencies, objects) +
		       key->object_count * sizeof *key->objects;
	return !g_crt_valid || memcmp(&g_crt_dependencies, key, bytes) != 0;
}

static void crt_projectiles(AeronScene3D *scene,
			    const struct xvt_render_snapshot *s,
			    const struct xvt_snap_preview *p)
{
	for (unsigned i = 0; i < s->object_count; ++i) {
		const struct xvt_snap_object *o = &s->objects[i];
		if (!includes_crt_projectile(o, p)) {
			continue;
		}
		struct xvt_ship_selection selection;
		if (!xvt_remaster_ship_select(s, o, &selection)) {
			continue;
		}
		const struct xvt_mesh_asset *mesh =
			xvt_remaster_ship_mesh(s, selection.asset_id);
		if (!mesh) {
			continue;
		}
		AeronSceneMeshInstance instance = {
			.mesh = mesh->mesh,
			.variant = o->node_switch,
			.zero_velocity = 1,
			.no_local_lights = 1,
			.base_color_emissive_strength =
				xvt_remaster_config_effective()
					->models
					.opt_projectile_emissive_strength,
			.cull_mode = AERON_CULL_NONE};
		xvt_effects_projectile_matrix(o, p->camera.world_pos,
					      p->camera.world_pos,
					      instance.transform);
		memcpy(instance.prev_transform, instance.transform,
		       sizeof instance.transform);
		AeronScene_AddMeshInstance(scene, &instance);
	}
}

/* Renders one model preview into its scene: a frontend preview, tone-mapped
 * into its slot's target, or the cockpit CRT's target with its projectiles and
 * effects. Both kinds run the same steps on one size, scene, camera and mesh
 * instance, with the kind deciding a small part of most steps, so keeping them
 * in one function keeps each step's two cases side by side. */
static int render_one(AeronCommandBuffer *cmd,
		      const struct xvt_render_snapshot *s,
		      const struct xvt_snap_preview *p, unsigned slot, int tw,
		      int th, int crt)
{
	if (!p->valid || p->destination.width <= 0 ||
	    p->destination.height <= 0) {
		return 1;
	}
	const struct xvt_mesh_asset *mesh =
		xvt_remaster_ship_mesh(s, p->opt_asset_id);
	if (!mesh && !crt) {
		return 1;
	}
	struct xvt_layout_transform layout;
	if (!xvt_render_math_layout(p->camera.screen_width,
				    p->camera.screen_height, (float)tw,
				    (float)th, &layout)) {
		return 0;
	}
	float scale = fminf((float)tw / p->camera.screen_width,
			    (float)th / p->camera.screen_height);
	int width =
		crt ? (int)ceilf(p->destination.width * scale) : PREVIEW_WIDTH;
	int height = crt ? (int)ceilf(p->destination.height * scale)
			 : PREVIEW_HEIGHT;
	if (!ensure(slot, width, height)) {
		return 0;
	}
	AeronScene3D *scene = crt ? g_crt_scene : g_scene;
	AeronSceneCamera camera = {0};
	AeronSceneMeshInstance instance = {.mesh = mesh ? mesh->mesh : NULL,
					   .variant = p->node_switch,
					   .zero_velocity = 1,
					   .no_local_lights = 1,
					   .cull_mode = AERON_CULL_BACK,
					   .mesh_table = &g_table};
	const struct xvt_snap_object *object =
		crt ? preview_target(s, p->object) : NULL;
	if (crt) {
		if (!object) {
			return 1;
		}
		struct xvt_render_view view;
		if (!xvt_render_math_build_view(&p->camera, p->camera.world_pos,
						p->destination.width,
						p->destination.height, &view)) {
			return 0;
		}
		camera = view.camera;
		camera.viewport = (AeronRectI){0, 0, width, height};
		xvt_render_math_object_matrix(object, p->camera.world_pos,
					      instance.transform);
		if (object->genus == CRAFT_GENUS_PLAYER_PROJECTILE ||
		    object->genus == CRAFT_GENUS_OTHER_PROJECTILE) {
			xvt_effects_projectile_matrix(
				object, p->camera.world_pos,
				p->camera.world_pos, instance.transform);
		}
	} else {
		camera.ori[0] = 1;
		float focal = ldexpf(1.0f, p->camera.perspective_shift & 31);
		float aspect =
			!p->camera.aspect_y_q16 ||
					p->camera.aspect_y_q16 == UINT16_MAX
				? 1
				: (float)p->camera.aspect_y_q16 / 65536.0f;
		camera.h_half_rad =
			atanf((float)p->destination.width / (2 * focal));
		camera.v_half_rad = atanf((float)p->destination.height /
					  (2 * focal * aspect));
		camera.near_z = 1;
		camera.proj_x_offset = ((float)p->camera.center_x -
					p->destination.width * 0.5f) /
				       (p->destination.width * 0.5f);
		camera.proj_y_offset =
			(p->destination.height * 0.5f - p->camera.center_y -
			 p->camera.projection_offset_y) /
			(p->destination.height * 0.5f);
		camera.viewport = (AeronRectI){0, 0, width, height};
		float model_scale = p->model_scale * AERON_OPT_UNITS_PER_METER;
		for (int row = 0; row < 3; ++row) {
			for (int col = 0; col < 3; ++col) {
				instance.transform[row * 4 + col] =
					model_scale *
					p->view_orient[col * 3 + row];
			}
			instance.transform[row * 4 + 3] = p->view_pos[row];
		}
		instance.transform[15] = 1;
	}
	if (mesh) {
		float visual[XVT_SNAP_COMPONENTS];
		xvt_remaster_ship_build_mesh_table(
			mesh, object, UINT16_MAX,
			crt ? xvt_component_animation_angles(s, object, mesh,
							     visual)
			    : NULL,
			&g_table);
	}
	memcpy(instance.prev_transform, instance.transform,
	       sizeof instance.transform);
	if (!AeronScene_Begin(scene, &camera)) {
		return 0;
	}
	if (!AeronScene_SetMeshSampler(scene,
				       xvt_remaster_config_mesh_sampler())) {
		return 0;
	}
	xvt_flight_pipeline_post(scene, 0, 0);
	xvt_remaster_ship_set_environment(scene, &p->lighting,
					  crt ? NULL : &p->camera, camera.pos);
	if (mesh) {
		AeronScene_AddMeshInstance(scene, &instance);
	}
	if (crt) {
		crt_projectiles(scene, s, p);
		xvt_effects_submit(scene, s, NULL, &p->camera, NULL, 0, p);
	}
	if (!AeronScene_Render(scene, cmd)) {
		return 0;
	}
	if (!crt) {
		AeronRenderPass *pass =
			Aeron_BeginRenderPass(&(AeronRenderPassDesc){
				.color_target = g_targets[slot],
				.clear_color = 1,
				.clear_color_rgba = {0, 0, 0, 0},
				.command_buffer = cmd,
				.debug_label = "XvT model preview tonemap"});
		if (!pass) {
			return 0;
		}
		AeronScenePresentChain_Draw(
			g_chain, pass,
			Aeron_RenderTargetGetTexture(AeronScene_SceneRt(scene)),
			g_sampler, NULL, 0, width, height, 1,
			(const float[4]){1, 1, 1, 1}, 1);
		Aeron_EndRenderPass(pass);
	}
	struct xvt_preview_output *out = &g_outputs[slot];
	out->snapshot_serial = s->snapshot_serial;
	out->texture = crt ? AeronScene_ColorTexture(scene)
			   : Aeron_RenderTargetGetTexture(g_targets[slot]);
	out->width = width;
	out->height = height;
	out->draw = p->draw;
	out->mask_index = p->mask_index;
	out->destination = p->destination;
	return 1;
}

int xvt_remaster_preview_render(AeronCommandBuffer *cmd,
				const struct xvt_render_snapshot *s, int width,
				int height)
{
	xvt_remaster_preview_begin_frame();
	for (unsigned i = 0; i < s->preview_count; ++i) {
		if (!render_one(cmd, s, &s->previews[i], i, width, height, 0)) {
			return 0;
		}
	}
	return 1;
}

int xvt_remaster_preview_render_crt(AeronCommandBuffer *cmd,
				    const struct xvt_render_snapshot *s,
				    int width, int height)
{
	if (!xvt_remaster_preview_crt_needs_render(s, width, height)) {
		return 1;
	}
	if (!render_one(cmd, s, &s->cockpit.crt, XVT_SNAP_PREVIEWS, width,
			height, 1)) {
		xvt_remaster_preview_invalidate_crt();
		return 0;
	}
	size_t bytes =
		offsetof(struct crt_dependencies, objects) +
		g_crt_candidate.object_count * sizeof *g_crt_candidate.objects;
	memcpy(&g_crt_dependencies, &g_crt_candidate, bytes);
	g_crt_valid = 1;
	return 1;
}

void xvt_remaster_preview_begin_frame(void)
{
	for (unsigned i = 0; i < XVT_SNAP_PREVIEWS; ++i) {
		g_outputs[i].texture = NULL;
	}
}

AeronTexture *xvt_remaster_preview_crt_linear(void)
{
	return g_outputs[XVT_SNAP_PREVIEWS].texture;
}

const struct xvt_preview_output *xvt_remaster_preview_output(unsigned slot)
{
	return slot < PREVIEW_SLOTS && g_outputs[slot].texture
		       ? &g_outputs[slot]
		       : NULL;
}

void xvt_remaster_preview_release_frontend(void)
{
	AeronScene_Destroy(g_scene);
	g_scene = NULL;
	AeronScenePresentChain_Destroy(g_chain);
	g_chain = NULL;
	Aeron_DestroySampler(g_sampler);
	g_sampler = NULL;
	for (unsigned i = 0; i < XVT_SNAP_PREVIEWS; ++i) {
		Aeron_DestroyRenderTarget(g_targets[i]);
		g_targets[i] = NULL;
		memset(&g_outputs[i], 0, sizeof g_outputs[i]);
	}
	g_scene_width = 0;
	g_scene_height = 0;
	g_samples = 0;
}

void xvt_remaster_preview_shutdown(void)
{
	xvt_remaster_preview_release_frontend();
	xvt_remaster_preview_invalidate_crt();
	for (unsigned index = 0; index < 3; ++index) {
		AeronScene_Destroy(g_crt_views[index].scene);
	}
	memset(g_crt_views, 0, sizeof g_crt_views);
	g_crt_scene = NULL;
	memset(&g_outputs[XVT_SNAP_PREVIEWS], 0,
	       sizeof g_outputs[XVT_SNAP_PREVIEWS]);
}
