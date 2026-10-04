#include "xvt_remaster/flight_pipeline.h"

#include "aeron/aeron.h"
#include "aeron/scene/bloom.h"
#include "aeron/scene/draw_list2d.h"
#include "aeron/scene/present.h"
#include "xvt_remaster/config.h"
#include "xvt_remaster/hud_renderer.h"

static AeronSceneBloom *g_bloom;
static AeronScenePresentChain *g_present;
static AeronSampler *g_sampler;
static AeronRenderTarget *g_target;
static int g_width;
static int g_height;
static uint64_t g_last_host_us;
static AeronScenePresentChain *g_direct;
static AeronTextureFormat g_direct_format;
static AeronTexture *g_color;
static AeronTexture *g_bloom_texture;
static float g_intensity;
static int g_direct_wanted;
static int g_retained;
static AeronDrawList2D *g_bars;
static int g_bars_visible;

void xvt_flight_pipeline_forget_sources(void)
{
	g_color = g_bloom_texture = NULL;
}

int xvt_flight_pipeline_set_direct(int enabled, int width, int height)
{
	g_direct_wanted = 0;
	if (!enabled || !Aeron_CanRenderDirectToSwapchain(width, height)) {
		return 0;
	}
	AeronTextureFormat format = Aeron_SwapchainFormat();
	if (!g_direct || format != g_direct_format) {
		AeronScenePresentChain_Destroy(g_direct);
		g_direct = AeronScenePresentChain_Create(format);
		g_direct_format = format;
		if (!g_direct) {
			Aeron_RequestFatalRendererError(
				"direct flight tonemap creation");
			return 0;
		}
	}
	g_direct_wanted = 1;
	return 1;
}

static int ensure(int width, int height)
{
	if (!g_present) {
		g_present = AeronScenePresentChain_Create(
			AERON_TEXTURE_FORMAT_RGBA16_FLOAT);
	}
	if (!g_sampler) {
		g_sampler = Aeron_CreateSampler(&(AeronSamplerDesc){
			.min_filter = AERON_FILTER_LINEAR,
			.mag_filter = AERON_FILTER_LINEAR,
			.address_u = AERON_ADDRESS_CLAMP_TO_EDGE,
			.address_v = AERON_ADDRESS_CLAMP_TO_EDGE});
	}
	if (!g_present || !g_sampler) {
		return 0;
	}
	if (g_target && g_width == width && g_height == height) {
		return 1;
	}
	AeronSceneBloom *bloom = AeronSceneBloom_Create(width, height);
	AeronRenderTarget *target =
		Aeron_CreateRenderTarget(&(AeronRenderTargetDesc){
			.width = width,
			.height = height,
			.format = AERON_TEXTURE_FORMAT_RGBA16_FLOAT,
			.debug_name = "xvt.flight.present"});
	if (!bloom || !target) {
		AeronSceneBloom_Destroy(bloom);
		Aeron_DestroyRenderTarget(target);
		return 0;
	}
	AeronSceneBloom_Destroy(g_bloom);
	Aeron_DestroyRenderTarget(g_target);
	g_color = g_bloom_texture = NULL;
	g_retained = 0;
	g_bloom = bloom;
	g_target = target;
	g_width = width;
	g_height = height;
	return 1;
}

void xvt_flight_pipeline_post(AeronScene3D *scene, float shutter, int motion)
{
	const struct xvt_render_settings *settings =
		xvt_remaster_config_effective();
	const AeronSceneSsaoSettings *a = &settings->scene.ssao;
	const struct xvt_motion_blur_settings *m = &settings->motion_blur;
	AeronScene_SetPost(
		scene, &(AeronScenePostDesc){
			       .ssao_quality = a->ssao_quality,
			       .ssao_intensity = a->ssao_intensity,
			       .ssao_power = a->ssao_power,
			       .ssao_radius_view = a->ssao_radius_view,
			       .ssao_bias_view = a->ssao_bias_view,
			       .ssao_direct = a->ssao_direct,
			       .ssao_debug_viz = a->ssao_debug_viz,
			       .ssao_min_screen_frac = a->ssao_min_screen_frac,
			       .ssao_max_screen_frac = a->ssao_max_screen_frac,
			       .ssao_sample_jitter = a->ssao_sample_jitter,
			       .mb_quality = motion ? m->quality : 0,
			       .mb_shutter = shutter,
			       .mb_camera_blur = m->camera_blur,
			       .mb_velocity_viz = motion && m->velocity_viz,
			       .mb_fsr_direct_motion = m->fsr_direct_motion});
}

int xvt_flight_pipeline_begin(AeronScene3D *scene,
			      const struct xvt_render_snapshot *s,
			      const struct xvt_prepared_flight *frame,
			      int reset)
{
	const struct xvt_render_settings *settings =
		xvt_remaster_config_effective();
	uint64_t now = Aeron_NowUs();
	float delta_ms = g_last_host_us && now > g_last_host_us
				 ? (float)(now - g_last_host_us) / 1000
				 : 16.6667f;
	g_last_host_us = now;
	AeronScene_SetTemporal(
		scene, &(AeronSceneTemporalDesc){
			       .mode = settings->temporal_mode,
			       .frame_time_delta_ms = delta_ms,
			       .sharpness = settings->temporal_sharpness,
			       .reset_history = reset});
	if (!AeronScene_Begin(scene, &frame->view.camera) ||
	    !AeronScene_SetMeshSampler(scene,
				       xvt_remaster_config_mesh_sampler())) {
		return 0;
	}
	/* XWA's 32 ms reference exposure: retained motion spans whole captured poses. */
	float shutter = !reset && frame->velocity_span_us
				? settings->motion_blur.shutter * 32000.0f /
					  (float)frame->velocity_span_us
				: 0;
	if ((s->paused || !frame->regenerate_motion) &&
	    !settings->motion_blur.pause_keep_blur) {
		shutter = 0;
	}
	xvt_flight_pipeline_post(scene, shutter, 1);
	/* FSR sees a stationary current frame while Aeron retains the last blur vectors. */
	AeronScene_SetMotionContext(
		scene,
		reset ? NULL
		      : (frame->regenerate_motion
				 ? frame->previous_view.view_proj
				 : frame->view.view_proj),
		reset || frame->regenerate_motion);
	return 1;
}

int xvt_flight_pipeline_prepare_scene_resources(AeronScene3D *scene, int flight)
{
	int width;
	int height;
	AeronScene_RtDims(scene, &width, &height);
	const struct xvt_render_settings *settings =
		xvt_remaster_config_effective();
	AeronScene_SetTemporal(
		scene, &(AeronSceneTemporalDesc){
			       .mode = flight ? settings->temporal_mode
					      : AERON_TEMPORAL_OFF,
			       .frame_time_delta_ms = 16.6667f,
			       .sharpness = settings->temporal_sharpness,
			       .reset_history = 1});
	xvt_flight_pipeline_post(scene, 0, flight);
	AeronSceneCamera camera = {.ori = {1, 0, 0, 0},
				   .h_half_rad = .785398163f,
				   .v_half_rad = .785398163f,
				   .near_z = 1,
				   .viewport = {0, 0, width, height}};
	return AeronScene_Begin(scene, &camera) &&
	       AeronScene_PrepareResources(scene, AERON_CULL_BACK);
}

int xvt_flight_pipeline_finish(AeronCommandBuffer *cmd, AeronScene3D *scene)
{
	int width;
	int height;
	AeronScene_RtDims(scene, &width, &height);
	if (!ensure(width, height) || !AeronScene_Render(scene, cmd)) {
		return 0;
	}
	AeronTexture *color =
		Aeron_RenderTargetGetTexture(AeronScene_SceneRt(scene));
	return xvt_flight_pipeline_resolve(cmd, color, width, height, 1);
}

static int prepare_bars(AeronCommandBuffer *cmd, int width, int height)
{
	const struct xvt_prepared_flight *frame = xvt_remaster_flight_current();
	g_bars_visible = frame && (frame->content_rect.width != width ||
				   frame->content_rect.height != height);
	if (!g_bars_visible) {
		return 1;
	}
	if (!g_bars) {
		g_bars = AeronDrawList_Create(4);
	}
	if (!g_bars) {
		return 0;
	}
	/* Retain this mask with the resolved sources, including across idle frames. */
	AeronRectI r = frame->content_rect;
	const float black[4] = {0, 0, 0, 1};
	AeronDrawList_Begin(g_bars, NULL, width, height, AERON_DRAWLIST2D_LOAD,
			    NULL);
	AeronDrawList_AddFill(g_bars, 0, 0, r.x, height, black,
			      AERON_BLIT2D_BLEND_NONE, NULL);
	AeronDrawList_AddFill(g_bars, r.x + r.width, 0, width - r.x - r.width,
			      height, black, AERON_BLIT2D_BLEND_NONE, NULL);
	AeronDrawList_AddFill(g_bars, r.x, 0, r.width, r.y, black,
			      AERON_BLIT2D_BLEND_NONE, NULL);
	AeronDrawList_AddFill(g_bars, r.x, r.y + r.height, r.width,
			      height - r.y - r.height, black,
			      AERON_BLIT2D_BLEND_NONE, NULL);
	return AeronDrawList_Prepare(g_bars, cmd);
}

int xvt_flight_pipeline_resolve(AeronCommandBuffer *cmd, AeronTexture *color,
				int width, int height, int enable_bloom)
{
	if (!ensure(width, height) || !prepare_bars(cmd, width, height)) {
		return 0;
	}
	float intensity =
		enable_bloom ? xvt_remaster_config_effective()->bloom_intensity
			     : 0;
	AeronTexture *bloom = NULL;
	if (intensity > 0) {
		if (!AeronSceneBloom_Apply(g_bloom, cmd, color, width, height,
					   height)) {
			return 0;
		}
		bloom = Aeron_RenderTargetGetTexture(
			AeronSceneBloom_ColorRt(g_bloom));
		if (!bloom) {
			return 0;
		}
	}
	g_color = color;
	g_bloom_texture = bloom;
	g_intensity = intensity;
	g_retained = 0;
	return g_direct_wanted || xvt_flight_pipeline_retain(cmd);
}

int xvt_flight_pipeline_retain(AeronCommandBuffer *cmd)
{
	if (!g_color || g_retained) {
		return 1;
	}
	AeronRenderPass *pass = Aeron_BeginRenderPass(
		&(AeronRenderPassDesc){.color_target = g_target,
				       .clear_color = 1,
				       .clear_color_rgba = {0, 0, 0, 1},
				       .command_buffer = cmd,
				       .debug_label = "XvT flight tonemap"});
	if (!pass) {
		return 0;
	}
	AeronScenePresentChain_Draw(g_present, pass, g_color, g_sampler,
				    g_bloom_texture, g_intensity, g_width,
				    g_height, 1, (const float[4]){1, 1, 1, 1},
				    0);
	/* Mask after tonemapping so bloom cannot brighten the bars. */
	if (g_bars_visible) {
		AeronDrawList_RenderIntoPass(g_bars, cmd, pass, g_target);
	}
	Aeron_EndRenderPass(pass);
	g_retained = 1;
	return 1;
}

int xvt_flight_pipeline_needs_retain(void)
{
	return g_color && !g_retained && !g_direct_wanted;
}

int xvt_flight_pipeline_draw_standalone_hud(AeronCommandBuffer *cmd, int width,
					    int height)
{
	if (!ensure(width, height)) {
		return 0;
	}
	AeronRenderPass *pass = Aeron_BeginRenderPass(&(AeronRenderPassDesc){
		.command_buffer = cmd,
		.color_target = g_target,
		.clear_color = 1,
		.clear_color_rgba = {0, 0, 0, 1},
		.debug_label = "XvT standalone flight UI"});
	if (!pass) {
		return 0;
	}
	xvt_hud_renderer_draw(cmd, pass, g_target);
	Aeron_EndRenderPass(pass);
	g_color = Aeron_RenderTargetGetTexture(g_target);
	g_bloom_texture = NULL;
	g_direct_wanted = 0;
	g_retained = 1;
	g_bars_visible = 0;
	return 1;
}

static void draw_direct(AeronCommandBuffer *cmd, AeronRenderPass *pass,
			AeronRenderTarget *target, int width, int height,
			void *user)
{
	(void)user;
	if (!g_direct_wanted || !g_color || width != g_width ||
	    height != g_height) {
		return;
	}
	AeronRectI full = {0, 0, width, height};
	Aeron_SetViewport(pass, &full);
	Aeron_SetScissor(pass, &full);
	AeronScenePresentChain_Draw(g_direct, pass, g_color, g_sampler,
				    g_bloom_texture, g_intensity, width, height,
				    1, (const float[4]){1, 1, 1, 1}, 0);
	if (g_bars_visible) {
		AeronDrawList_RenderIntoPass(g_bars, cmd, pass, target);
	}
}

int xvt_flight_pipeline_submit_direct(void)
{
	if (!g_direct_wanted || !g_color) {
		return 0;
	}
	return Aeron_SubmitSwapchainRenderLayer(
		&(AeronSwapchainRenderLayerDesc){
			.callback = draw_direct,
			.required_width = g_width,
			.required_height = g_height,
			.debug_label = "XvT flight direct presentation"});
}

AeronTexture *xvt_flight_pipeline_output(void)
{
	return g_color && g_target ? Aeron_RenderTargetGetTexture(g_target)
				   : NULL;
}

void xvt_flight_pipeline_shutdown(void)
{
	AeronDrawList_Destroy(g_bars);
	g_bars = NULL;
	g_bars_visible = 0;
	g_direct_wanted = g_retained = 0;
	g_color = g_bloom_texture = NULL;
	AeronScenePresentChain_Destroy(g_direct);
	g_direct = NULL;
	AeronSceneBloom_Destroy(g_bloom);
	AeronScenePresentChain_Destroy(g_present);
	Aeron_DestroySampler(g_sampler);
	Aeron_DestroyRenderTarget(g_target);
	g_bloom = NULL;
	g_present = NULL;
	g_sampler = NULL;
	g_target = NULL;
	g_last_host_us = 0;
	g_width = g_height = 0;
}
