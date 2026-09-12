#include "xvt_remaster/flight_map.h"
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
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static AeronScene3D* g_scene;
static AeronRenderTarget* g_composite;
static AeronDrawList2D* g_list;
static AeronSceneMeshTable* g_tables;
static int g_width, g_height, g_samples;

typedef struct MapOrder {
	unsigned index;
	float depth;
} MapOrder;

static MapOrder g_order[XVT_SNAP_OBJECTS];

static int Compare(const void* a, const void* b) {
	const MapOrder *x = a, *y = b;
	return x->depth < y->depth ? 1 : x->depth > y->depth ? -1 : x->index < y->index ? -1 : 1;
}

static int Ensure(int w, int h) {
	if (!g_list)
		g_list = AeronDrawList_Create(32768);
	if (!g_tables)
		g_tables = calloc(XVT_SNAP_OBJECTS, sizeof *g_tables);
	if (!g_list || !g_tables)
		return 0;
	int samples = XvtRemasterConfig_Effective()->msaa_samples;
	if (g_scene && w == g_width && h == g_height && samples == g_samples)
		return 1;
	AeronScene3D* scene =
		AeronScene_Create(&(AeronScene3DDesc) { .rt_width = w,
												.rt_height = h,
												.color_format = AERON_TEXTURE_FORMAT_RGBA16_FLOAT,
												.with_normal_rt = 1,
												.sample_count = (AeronSampleCount)samples,
												.view_space_to_meters = AERON_OPT_METERS_PER_UNIT });
	AeronRenderTarget* target =
		Aeron_CreateRenderTarget(&(AeronRenderTargetDesc) { .width = w,
															.height = h,
															.format = AERON_TEXTURE_FORMAT_RGBA16_FLOAT,
															.debug_name = "xvt.map.composite" });
	if (!scene || !target) {
		AeronScene_Destroy(scene);
		Aeron_DestroyRenderTarget(target);
		return 0;
	}
	XvtFlightPipeline_ForgetSources();
	AeronScene_Destroy(g_scene);
	Aeron_DestroyRenderTarget(g_composite);
	g_scene = scene;
	g_composite = target;
	g_width = w;
	g_height = h;
	g_samples = samples;
	AeronScene_SetClearColor(g_scene, (const float[4]) { 0, 0, 0, 0 });
	return 1;
}

int XvtFlightMap_PrepareResources(int width, int height) {
	AeronScene3D* previous = g_scene;
	return Ensure(width, height) &&
		   (previous == g_scene || XvtFlightPipeline_PrepareSceneResources(g_scene, 0));
}

static void Segment(const XvtRenderView* view, const int32_t a[3], const int32_t b[3], uint32_t color) {
	float p[3], q[3], clip[2][4];
	AeronWorld_LocalI32(view->origin_world, a, p);
	AeronWorld_LocalI32(view->origin_world, b, q);
	for (int r = 0; r < 4; ++r) {
		const float* m = view->view_proj + r * 4;
		clip[0][r] = m[0] * p[0] + m[1] * p[1] + m[2] * p[2] + m[3];
		clip[1][r] = m[0] * q[0] + m[1] * q[1] + m[2] * q[2] + m[3];
	}
	if (clip[0][3] < 1 && clip[1][3] < 1)
		return;
	for (int i = 0; i < 2; ++i)
		if (clip[i][3] < 1) {
			float t = (1 - clip[i][3]) / (clip[1 - i][3] - clip[i][3]);
			for (int r = 0; r < 4; ++r)
				clip[i][r] += (clip[1 - i][r] - clip[i][r]) * t;
		}
	float rgba[4];
	XvtUi_Color(color, rgba);
	AeronRectI scissor = { 0, 0, g_width, g_height };
	AeronDrawList_AddLine(
		g_list, (clip[0][0] / clip[0][3] + 1) * g_width * .5f, (1 - clip[0][1] / clip[0][3]) * g_height * .5f,
		(clip[1][0] / clip[1][3] + 1) * g_width * .5f, (1 - clip[1][1] / clip[1][3]) * g_height * .5f,
		view->classic_pixel_scale, rgba, AERON_BLIT2D_BLEND_PMA, &scissor);
}

static void Grid(const XvtRenderSnapshot* s, const XvtRenderView* view) {
	for (int i = -16; i <= 16; ++i) {
		int32_t a[3] = { -1048576, i * 65536, s->map.grid_z }, b[3] = { 1048576, i * 65536, s->map.grid_z };
		Segment(view, a, b, s->flight_palette_argb[49]);
		a[0] = b[0] = i * 65536;
		a[1] = -1048576;
		b[1] = 1048576;
		Segment(view, a, b, s->flight_palette_argb[49]);
	}
}

static void Overlay(const XvtRenderSnapshot* s, const XvtRenderView* view, const XvtSnapMapObject* m, float x,
					float y, float depth) {
	const XvtSnapObject* o = &s->objects[m->object_index];
	float scale = view->classic_pixel_scale;
	float extent = fmaxf(s->camera.screen_width / 80.0f,
						 fminf(s->camera.screen_width * .5f,
							   m->box_extent * ldexpf(1, s->camera.perspective_shift & 31) / depth));
	float size = (extent + 4) * scale;
	if (m->box_visible) {
		float rgba[4];
		XvtUi_Color(s->flight_palette_argb[m->box_color], rgba);
		float corner = fmaxf(3 * scale, size / 8);
		for (int i = 0; i < 2; ++i)
			for (int j = 0; j < 2; ++j) {
				float xx = x + (i ? size : -size) / 2, yy = y + (j ? size : -size) / 2;
				AeronDrawList_AddLine(g_list, xx, yy, xx + (i ? -corner : corner), yy, scale, rgba,
									  AERON_BLIT2D_BLEND_PMA, NULL);
				AeronDrawList_AddLine(g_list, xx, yy, xx, yy + (j ? -corner : corner), scale, rgba,
									  AERON_BLIT2D_BLEND_PMA, NULL);
			}
	}
	if (o->id.slot == s->map.target.slot && s->map.endpoint_valid)
		Segment(view, o->world_pos, s->map.order_endpoint, s->flight_palette_argb[54]);
	if (m->overlay_visible) {
		int32_t base[3] = { o->world_pos[0], o->world_pos[1], s->map.grid_z };
		Segment(view, o->world_pos, base, m->line_color_argb);
		if (m->movement_visible) {
			int32_t end[3];
			memcpy(end, base, sizeof end);
			int distance = 256 + (o->speed < 1024 ? 32 * o->speed : 32768);
			end[0] = (int32_t)((uint32_t)end[0] + (uint32_t)(((int64_t)m->move_x * 256) >> 15) +
							   (uint32_t)(((int64_t)m->move_x * (distance - 256)) >> 15));
			end[1] = (int32_t)((uint32_t)end[1] + (uint32_t)(((int64_t)m->move_y * 256) >> 15) +
							   (uint32_t)(((int64_t)m->move_y * (distance - 256)) >> 15));
			Segment(view, base, end, m->line_color_argb);
		}
	}
	const XvtFontAtlas* font = XvtRemasterAssets_Font(s->map.font_asset_id, 0);
	float text_height = (font ? font->cell_height : 5) * scale;
	if (m->label_visible && m->label_offset < s->map.label_bytes)
		XvtUi_Text(g_list, s->map.font_asset_id, s->map.labels + m->label_offset, x,
				   y - size / 2 - text_height - scale, scale, m->label_color_argb, 1);
	if (m->range_visible) {
		char text[16];
		snprintf(text, sizeof text, "%u.%02u", m->range_value / 100, m->range_value % 100);
		XvtUi_Text(g_list, s->map.font_asset_id, text, x, y + size / 2 + scale, scale, m->label_color_argb,
				   1);
	}
}

static int Objects(AeronCommandBuffer* cmd, const XvtRenderSnapshot* s, const XvtRenderView* view,
				   int above) {
	if (!AeronScene_Begin(g_scene, &view->camera) ||
		!AeronScene_SetMeshSampler(g_scene, XvtRemasterConfig_MeshSampler()))
		return 0;
	XvtFlightPipeline_Post(g_scene, 0, 0);
	if (!XvtLighting_Begin(g_scene, s))
		return 0;
	AeronScene_SetDirectionalShadow(g_scene, NULL);
	XvtRemasterShip_SetEnvironment(g_scene, &s->lighting, NULL, view->camera.pos);
	AeronDrawList_Begin(g_list, NULL, g_width, g_height, AERON_DRAWLIST2D_LOAD, NULL);
	for (unsigned k = 0; k < s->map.object_count; ++k) {
		const XvtSnapMapObject* m = &s->map.objects[g_order[k].index];
		if (m->object_index >= s->object_count)
			continue;
		const XvtSnapObject* o = &s->objects[m->object_index];
		if ((o->world_pos[2] >= s->map.grid_z) != above)
			continue;
		float x, y, z;
		int projected = XvtRenderMath_ProjectWorld(view, o->world_pos, &x, &y, &z);
		int icon = projected && m->render_kind == XVT_MAP_MODEL_OR_ICON &&
				   s->types[o->object_type].max_extent < z / 16;
		if (icon) {
			if (!XvtUi_MapIcon(
					g_list, cmd, s->map.icon_asset_id, m->icon_frame, m->effective_iff == 3,
					s->flight_palette_argb,
					(int)(x / view->classic_pixel_scale - m->icon_width / 2) * view->classic_pixel_scale,
					(int)(y / view->classic_pixel_scale - m->icon_height / 2) * view->classic_pixel_scale,
					view->classic_pixel_scale, g_width, g_height))
				return 0;
		} else {
			XvtShipSelection selection;
			if (XvtRemasterShip_Select(s, o, &selection)) {
				const XvtMeshAsset* asset = XvtRemasterShip_Mesh(s, selection.asset_id);
				if (asset) {
					AeronSceneMeshTable* table = &g_tables[m->object_index];
					float visual[XVT_SNAP_COMPONENTS];
					XvtRemasterShip_BuildMeshTable(asset, o, selection.component,
												   XvtComponentAnimation_Angles(s, o, asset, visual), table);
					AeronSceneMeshInstance instance = { .mesh = asset->mesh,
														.variant = o->node_switch,
														.mesh_table = table,
														.zero_velocity = 1,
														.cull_mode = AERON_CULL_BACK };
					XvtRenderMath_ObjectMatrix(o, s->camera.world_pos, instance.transform);
					if (o->genus == CRAFT_GENUS_PLAYER_PROJECTILE ||
						o->genus == CRAFT_GENUS_OTHER_PROJECTILE) {
						XvtEffects_ProjectileMatrix(o, s->camera.world_pos, s->camera.world_pos,
													instance.transform);
						instance.cull_mode = AERON_CULL_NONE;
						instance.base_color_emissive_strength =
							XvtRemasterConfig_Effective()->models.opt_projectile_emissive_strength;
					}
					AeronScene_AddMeshInstance(g_scene, &instance);
				}
			}
			XvtEffects_MapObject(g_scene, s, o);
		}
		if (projected)
			Overlay(s, view, m, x, y, z);
	}
	if (!AeronDrawList_Prepare(g_list, cmd) || !AeronScene_Render(g_scene, cmd))
		return 0;
	AeronRenderPass* pass = Aeron_BeginRenderPass(
		&(AeronRenderPassDesc) { .command_buffer = cmd, .color_target = AeronScene_SceneRt(g_scene) });
	if (!pass)
		return 0;
	AeronDrawList_RenderIntoPass(g_list, cmd, pass, AeronScene_SceneRt(g_scene));
	Aeron_EndRenderPass(pass);
	return 1;
}

static int Composite(AeronCommandBuffer* cmd, int clear) {
	AeronDrawList_Begin(g_list, g_composite, g_width, g_height,
						clear ? AERON_DRAWLIST2D_CLEAR : AERON_DRAWLIST2D_LOAD, NULL);
	AeronDrawList2DSprite d = { .texture = Aeron_RenderTargetGetTexture(AeronScene_SceneRt(g_scene)),
								.src_u1 = 1,
								.src_v1 = 1,
								.dst_w = (float)g_width,
								.dst_h = (float)g_height,
								.tint = { 1, 1, 1, 1 },
								.blend = AERON_BLIT2D_BLEND_PMA };
	AeronDrawList_AddSprite(g_list, &d);
	AeronDrawList_Render(g_list, cmd);
	return 1;
}

int XvtFlightMap_Render(AeronCommandBuffer* cmd, const XvtRenderSnapshot* s, const XvtRenderView* view) {
	if (!Ensure(view->camera.viewport.width, view->camera.viewport.height))
		return 0;
	for (unsigned i = 0; i < s->map.object_count; ++i) {
		float local[3];
		const XvtSnapObject* o = &s->objects[s->map.objects[i].object_index];
		AeronWorld_LocalI32(view->origin_world, o->world_pos, local);
		g_order[i] = (MapOrder) { i, view->view_proj[12] * local[0] + view->view_proj[13] * local[1] +
										 view->view_proj[14] * local[2] + view->view_proj[15] };
	}
	qsort(g_order, s->map.object_count, sizeof g_order[0], Compare);
	int first = s->camera.world_pos[2] < s->map.grid_z;
	if (!Objects(cmd, s, view, first) || !Composite(cmd, 1))
		return 0;
	AeronDrawList_Begin(g_list, g_composite, g_width, g_height, AERON_DRAWLIST2D_LOAD, NULL);
	Grid(s, view);
	AeronDrawList_Render(g_list, cmd);
	if (!Objects(cmd, s, view, !first) || !Composite(cmd, 0))
		return 0;
	AeronRenderPass* pass =
		Aeron_BeginRenderPass(&(AeronRenderPassDesc) { .command_buffer = cmd, .color_target = g_composite });
	if (!pass)
		return 0;
	XvtHudRenderer_Draw(cmd, pass, g_composite);
	Aeron_EndRenderPass(pass);
	return XvtFlightPipeline_Resolve(cmd, Aeron_RenderTargetGetTexture(g_composite), g_width, g_height, 0);
}

void XvtFlightMap_Shutdown(void) {
	XvtFlightPipeline_ForgetSources();
	AeronScene_Destroy(g_scene);
	Aeron_DestroyRenderTarget(g_composite);
	AeronDrawList_Destroy(g_list);
	free(g_tables);
	g_scene = NULL;
	g_composite = NULL;
	g_list = NULL;
	g_tables = NULL;
	g_width = g_height = g_samples = 0;
}
