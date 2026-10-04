#include "xvt_remaster/flight_map.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aeron/aeron.h"
#include "aeron/asset/opt_model.h"
#include "aeron/scene/world.h"
#include "xvt_remaster/component_animation.h"
#include "xvt_remaster/config.h"
#include "xvt_remaster/effects.h"
#include "xvt_remaster/flight_pipeline.h"
#include "xvt_remaster/hud_renderer.h"
#include "xvt_remaster/lighting.h"
#include "xvt_remaster/ship.h"
#include "xvt_remaster/ui_draw.h"

static AeronScene3D *g_scene;
static AeronRenderTarget *g_composite;
static AeronDrawList2D *g_list;
static AeronSceneMeshTable *g_tables;
static int g_width;
static int g_height;
static int g_samples;

struct map_order {
	unsigned index;
	float depth;
};

static struct map_order g_order[XVT_SNAP_OBJECTS];

static int compare(const void *a, const void *b)
{
	const struct map_order *x = a;
	const struct map_order *y = b;
	return x->depth < y->depth   ? 1
	       : x->depth > y->depth ? -1
	       : x->index < y->index ? -1
				     : 1;
}

static int ensure(int w, int h)
{
	if (!g_list) {
		g_list = AeronDrawList_Create(32768);
	}
	if (!g_tables) {
		g_tables = calloc(XVT_SNAP_OBJECTS, sizeof *g_tables);
	}
	if (!g_list || !g_tables) {
		return 0;
	}
	int samples = xvt_remaster_config_effective()->msaa_samples;
	if (g_scene && w == g_width && h == g_height && samples == g_samples) {
		return 1;
	}
	AeronScene3D *scene = AeronScene_Create(&(AeronScene3DDesc){
		.rt_width = w,
		.rt_height = h,
		.color_format = AERON_TEXTURE_FORMAT_RGBA16_FLOAT,
		.with_normal_rt = 1,
		.sample_count = (AeronSampleCount)samples,
		.view_space_to_meters = AERON_OPT_METERS_PER_UNIT});
	AeronRenderTarget *target =
		Aeron_CreateRenderTarget(&(AeronRenderTargetDesc){
			.width = w,
			.height = h,
			.format = AERON_TEXTURE_FORMAT_RGBA16_FLOAT,
			.debug_name = "xvt.map.composite"});
	if (!scene || !target) {
		AeronScene_Destroy(scene);
		Aeron_DestroyRenderTarget(target);
		return 0;
	}
	xvt_flight_pipeline_forget_sources();
	AeronScene_Destroy(g_scene);
	Aeron_DestroyRenderTarget(g_composite);
	g_scene = scene;
	g_composite = target;
	g_width = w;
	g_height = h;
	g_samples = samples;
	AeronScene_SetClearColor(g_scene, (const float[4]){0, 0, 0, 0});
	return 1;
}

int xvt_flight_map_prepare_resources(int width, int height)
{
	AeronScene3D *previous = g_scene;
	return ensure(width, height) &&
	       (previous == g_scene ||
		xvt_flight_pipeline_prepare_scene_resources(g_scene, 0));
}

static void segment(const struct xvt_render_view *view, const int32_t a[3],
		    const int32_t b[3], uint32_t color)
{
	float p[3];
	float q[3];
	float clip[2][4];
	AeronWorld_LocalI32(view->origin_world, a, p);
	AeronWorld_LocalI32(view->origin_world, b, q);
	for (int r = 0; r < 4; ++r) {
		const float *m = view->view_proj + r * 4;
		clip[0][r] = m[0] * p[0] + m[1] * p[1] + m[2] * p[2] + m[3];
		clip[1][r] = m[0] * q[0] + m[1] * q[1] + m[2] * q[2] + m[3];
	}
	if (clip[0][3] < 1 && clip[1][3] < 1) {
		return;
	}
	for (int i = 0; i < 2; ++i) {
		if (clip[i][3] < 1) {
			float t = (1 - clip[i][3]) /
				  (clip[1 - i][3] - clip[i][3]);
			for (int r = 0; r < 4; ++r) {
				clip[i][r] += (clip[1 - i][r] - clip[i][r]) * t;
			}
		}
	}
	float rgba[4];
	xvt_ui_color(color, rgba);
	AeronRectI scissor = {0, 0, g_width, g_height};
	AeronDrawList_AddLine(g_list,
			      (clip[0][0] / clip[0][3] + 1) * g_width * .5f,
			      (1 - clip[0][1] / clip[0][3]) * g_height * .5f,
			      (clip[1][0] / clip[1][3] + 1) * g_width * .5f,
			      (1 - clip[1][1] / clip[1][3]) * g_height * .5f,
			      view->classic_pixel_scale, rgba,
			      AERON_BLIT2D_BLEND_PMA, &scissor);
}

static void grid(const struct xvt_render_snapshot *s,
		 const struct xvt_render_view *view)
{
	for (int i = -16; i <= 16; ++i) {
		int32_t a[3] = {-1048576, i * 65536, s->map.grid_z};
		int32_t b[3] = {1048576, i * 65536, s->map.grid_z};
		segment(view, a, b, s->flight_palette_argb[49]);
		b[0] = i * 65536;
		a[0] = b[0];
		a[1] = -1048576;
		b[1] = 1048576;
		segment(view, a, b, s->flight_palette_argb[49]);
	}
}

static void overlay(const struct xvt_render_snapshot *s,
		    const struct xvt_render_view *view,
		    const struct xvt_snap_map_object *m, float x, float y,
		    float depth)
{
	const struct xvt_snap_object *o = &s->objects[m->object_index];
	float scale = view->classic_pixel_scale;
	float extent = fmaxf(
		s->camera.screen_width / 80.0f,
		fminf(s->camera.screen_width * .5f,
		      m->box_extent *
			      ldexpf(1, s->camera.perspective_shift & 31) /
			      depth));
	float size = (extent + 4) * scale;
	if (m->box_visible) {
		float rgba[4];
		xvt_ui_color(s->flight_palette_argb[m->box_color], rgba);
		float corner = fmaxf(3 * scale, size / 8);
		for (int i = 0; i < 2; ++i) {
			for (int j = 0; j < 2; ++j) {
				float xx = x + (i ? size : -size) / 2;
				float yy = y + (j ? size : -size) / 2;
				AeronDrawList_AddLine(
					g_list, xx, yy,
					xx + (i ? -corner : corner), yy, scale,
					rgba, AERON_BLIT2D_BLEND_PMA, NULL);
				AeronDrawList_AddLine(
					g_list, xx, yy, xx,
					yy + (j ? -corner : corner), scale,
					rgba, AERON_BLIT2D_BLEND_PMA, NULL);
			}
		}
	}
	if (o->id.slot == s->map.target.slot && s->map.endpoint_valid) {
		segment(view, o->world_pos, s->map.order_endpoint,
			s->flight_palette_argb[54]);
	}
	if (m->overlay_visible) {
		int32_t base[3] = {o->world_pos[0], o->world_pos[1],
				   s->map.grid_z};
		segment(view, o->world_pos, base, m->line_color_argb);
		if (m->movement_visible) {
			int32_t end[3];
			memcpy(end, base, sizeof end);
			int distance =
				256 + (o->speed < 1024 ? 32 * o->speed : 32768);
			end[0] = (int32_t)((uint32_t)end[0] +
					   (uint32_t)(((int64_t)m->move_x *
						       256) >>
						      15) +
					   (uint32_t)(((int64_t)m->move_x *
						       (distance - 256)) >>
						      15));
			end[1] = (int32_t)((uint32_t)end[1] +
					   (uint32_t)(((int64_t)m->move_y *
						       256) >>
						      15) +
					   (uint32_t)(((int64_t)m->move_y *
						       (distance - 256)) >>
						      15));
			segment(view, base, end, m->line_color_argb);
		}
	}
	const struct xvt_font_atlas *font =
		xvt_remaster_assets_font(s->map.font_asset_id, 0);
	float text_height = (font ? font->cell_height : 5) * scale;
	if (m->label_visible && m->label_offset < s->map.label_bytes) {
		xvt_ui_text(g_list, s->map.font_asset_id,
			    s->map.labels + m->label_offset, x,
			    y - size / 2 - text_height - scale, scale,
			    m->label_color_argb, 1);
	}
	if (m->range_visible) {
		char text[16];
		snprintf(text, sizeof text, "%u.%02u", m->range_value / 100,
			 m->range_value % 100);
		xvt_ui_text(g_list, s->map.font_asset_id, text, x,
			    y + size / 2 + scale, scale, m->label_color_argb,
			    1);
	}
}

static int objects(AeronCommandBuffer *cmd, const struct xvt_render_snapshot *s,
		   const struct xvt_render_view *view, int above)
{
	if (!AeronScene_Begin(g_scene, &view->camera) ||
	    !AeronScene_SetMeshSampler(g_scene,
				       xvt_remaster_config_mesh_sampler())) {
		return 0;
	}
	xvt_flight_pipeline_post(g_scene, 0, 0);
	if (!xvt_lighting_begin(g_scene, s)) {
		return 0;
	}
	AeronScene_SetDirectionalShadow(g_scene, NULL);
	xvt_remaster_ship_set_environment(g_scene, &s->lighting, NULL,
					  view->camera.pos);
	AeronDrawList_Begin(g_list, NULL, g_width, g_height,
			    AERON_DRAWLIST2D_LOAD, NULL);
	for (unsigned k = 0; k < s->map.object_count; ++k) {
		const struct xvt_snap_map_object *m =
			&s->map.objects[g_order[k].index];
		if (m->object_index >= s->object_count) {
			continue;
		}
		const struct xvt_snap_object *o = &s->objects[m->object_index];
		if ((o->world_pos[2] >= s->map.grid_z) != above) {
			continue;
		}
		float x;
		float y;
		float z;
		int projected = xvt_render_math_project_world(
			view, o->world_pos, &x, &y, &z);
		int icon = projected &&
			   m->render_kind == XVT_MAP_MODEL_OR_ICON &&
			   s->types[o->object_type].max_extent < z / 16;
		if (icon) {
			if (!xvt_ui_map_icon(
				    g_list, cmd, s->map.icon_asset_id,
				    m->icon_frame, m->effective_iff == 3,
				    s->flight_palette_argb,
				    (int)(x / view->classic_pixel_scale -
					  m->icon_width / 2) *
					    view->classic_pixel_scale,
				    (int)(y / view->classic_pixel_scale -
					  m->icon_height / 2) *
					    view->classic_pixel_scale,
				    view->classic_pixel_scale, g_width,
				    g_height)) {
				return 0;
			}
		} else {
			struct xvt_ship_selection selection;
			if (xvt_remaster_ship_select(s, o, &selection)) {
				const struct xvt_mesh_asset *asset =
					xvt_remaster_ship_mesh(
						s, selection.asset_id);
				if (asset) {
					AeronSceneMeshTable *table =
						&g_tables[m->object_index];
					float visual[XVT_SNAP_COMPONENTS];
					xvt_remaster_ship_build_mesh_table(
						asset, o, selection.component,
						xvt_component_animation_angles(
							s, o, asset, visual),
						table);
					AeronSceneMeshInstance instance = {
						.mesh = asset->mesh,
						.variant = o->node_switch,
						.mesh_table = table,
						.zero_velocity = 1,
						.cull_mode = AERON_CULL_BACK};
					xvt_render_math_object_matrix(
						o, s->camera.world_pos,
						instance.transform);
					if (o->genus ==
						    CRAFT_GENUS_PLAYER_PROJECTILE ||
					    o->genus ==
						    CRAFT_GENUS_OTHER_PROJECTILE) {
						xvt_effects_projectile_matrix(
							o, s->camera.world_pos,
							s->camera.world_pos,
							instance.transform);
						instance.cull_mode =
							AERON_CULL_NONE;
						instance.base_color_emissive_strength =
							xvt_remaster_config_effective()
								->models
								.opt_projectile_emissive_strength;
					}
					AeronScene_AddMeshInstance(g_scene,
								   &instance);
				}
			}
			xvt_effects_map_object(g_scene, s, o);
		}
		if (projected) {
			overlay(s, view, m, x, y, z);
		}
	}
	if (!AeronDrawList_Prepare(g_list, cmd) ||
	    !AeronScene_Render(g_scene, cmd)) {
		return 0;
	}
	AeronRenderPass *pass = Aeron_BeginRenderPass(&(AeronRenderPassDesc){
		.command_buffer = cmd,
		.color_target = AeronScene_SceneRt(g_scene)});
	if (!pass) {
		return 0;
	}
	AeronDrawList_RenderIntoPass(g_list, cmd, pass,
				     AeronScene_SceneRt(g_scene));
	Aeron_EndRenderPass(pass);
	return 1;
}

static int composite(AeronCommandBuffer *cmd, int clear)
{
	AeronDrawList_Begin(
		g_list, g_composite, g_width, g_height,
		clear ? AERON_DRAWLIST2D_CLEAR : AERON_DRAWLIST2D_LOAD, NULL);
	AeronDrawList2DSprite d = {.texture = Aeron_RenderTargetGetTexture(
					   AeronScene_SceneRt(g_scene)),
				   .src_u1 = 1,
				   .src_v1 = 1,
				   .dst_w = (float)g_width,
				   .dst_h = (float)g_height,
				   .tint = {1, 1, 1, 1},
				   .blend = AERON_BLIT2D_BLEND_PMA};
	AeronDrawList_AddSprite(g_list, &d);
	AeronDrawList_Render(g_list, cmd);
	return 1;
}

int xvt_flight_map_render(AeronCommandBuffer *cmd,
			  const struct xvt_render_snapshot *s,
			  const struct xvt_render_view *view)
{
	if (!ensure(view->camera.viewport.width,
		    view->camera.viewport.height)) {
		return 0;
	}
	for (unsigned i = 0; i < s->map.object_count; ++i) {
		float local[3];
		const struct xvt_snap_object *o =
			&s->objects[s->map.objects[i].object_index];
		AeronWorld_LocalI32(view->origin_world, o->world_pos, local);
		g_order[i] = (struct map_order){
			i, view->view_proj[12] * local[0] +
				   view->view_proj[13] * local[1] +
				   view->view_proj[14] * local[2] +
				   view->view_proj[15]};
	}
	qsort(g_order, s->map.object_count, sizeof g_order[0], compare);
	int camera_below_grid = s->camera.world_pos[2] < s->map.grid_z;
	if (!objects(cmd, s, view, camera_below_grid) || !composite(cmd, 1)) {
		return 0;
	}
	AeronDrawList_Begin(g_list, g_composite, g_width, g_height,
			    AERON_DRAWLIST2D_LOAD, NULL);
	grid(s, view);
	AeronDrawList_Render(g_list, cmd);
	if (!objects(cmd, s, view, !camera_below_grid) || !composite(cmd, 0)) {
		return 0;
	}
	AeronRenderPass *pass = Aeron_BeginRenderPass(&(AeronRenderPassDesc){
		.command_buffer = cmd, .color_target = g_composite});
	if (!pass) {
		return 0;
	}
	xvt_hud_renderer_draw(cmd, pass, g_composite);
	Aeron_EndRenderPass(pass);
	return xvt_flight_pipeline_resolve(
		cmd, Aeron_RenderTargetGetTexture(g_composite), g_width,
		g_height, 0);
}

void xvt_flight_map_shutdown(void)
{
	xvt_flight_pipeline_forget_sources();
	AeronScene_Destroy(g_scene);
	Aeron_DestroyRenderTarget(g_composite);
	AeronDrawList_Destroy(g_list);
	free(g_tables);
	g_scene = NULL;
	g_composite = NULL;
	g_list = NULL;
	g_tables = NULL;
	g_width = 0;
	g_height = 0;
	g_samples = 0;
}
