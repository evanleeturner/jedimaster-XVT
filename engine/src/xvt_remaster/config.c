#include "xvt_remaster/config.h"

#include <stdio.h>
#include <string.h>

#include "aeron/aeron.h"
#include "xvt_runtime/config/config.h"
#include "xvt_runtime/log/log.h"
static struct xvt_render_settings g_effective;
static struct xvt_render_settings g_requested;
static struct xvt_video_settings g_video;
static uint64_t g_generation;
static uint64_t g_document_generation;
static int g_initialized;
static int g_video_override;
static AeronSampler *g_mesh_sampler;

static void
xvt_remaster_config_read_output(struct xvt_render_settings *settings)
{
	settings->presentation.hdr_output = Aeron_OutputHdrEnabled();
	settings->presentation.sdr_gamma = Aeron_OutputSdrContentGamma();
	settings->presentation.paper_white_nits = Aeron_OutputPaperWhiteNits();
}

static int
xvt_remaster_config_apply(const struct xvt_render_settings *requested,
			  char *error, size_t capacity)
{
	struct xvt_render_settings next = *requested;
	if (next.temporal_mode != AERON_TEMPORAL_OFF) {
		next.msaa_samples = 1;
	}
	while (next.msaa_samples > 1 &&
	       (!Aeron_TextureFormatSupportsSampleCount(
			AERON_TEXTURE_FORMAT_RGBA16_FLOAT,
			(AeronSampleCount)next.msaa_samples) ||
		!Aeron_TextureFormatSupportsSampleCount(
			AERON_TEXTURE_FORMAT_D32_FLOAT,
			(AeronSampleCount)next.msaa_samples))) {
		next.msaa_samples /= 2;
	}
	bool sampler_changed =
		!g_initialized || next.anisotropic != g_effective.anisotropic ||
		next.max_anisotropy != g_effective.max_anisotropy;
	AeronSampler *sampler = g_mesh_sampler;
	if (sampler_changed) {
		sampler = next.anisotropic
				  ? Aeron_CreateSampler(&(AeronSamplerDesc){
					    .min_filter = AERON_FILTER_LINEAR,
					    .mag_filter = AERON_FILTER_LINEAR,
					    .mip_filter = AERON_FILTER_LINEAR,
					    .address_u =
						    AERON_ADDRESS_CLAMP_TO_EDGE,
					    .address_v =
						    AERON_ADDRESS_CLAMP_TO_EDGE,
					    .address_w =
						    AERON_ADDRESS_CLAMP_TO_EDGE,
					    .max_lod = 1000,
					    .enable_anisotropy = 1,
					    .max_anisotropy =
						    next.max_anisotropy})
				  : NULL;
		if (next.anisotropic && !sampler) {
			snprintf(error, capacity,
				 "Cannot create texture sampler");
			return 0;
		}
	}
	if (!Aeron_SetPresentationVsyncDivisor(
		    next.presentation.vsync_divisor) ||
	    ((!g_initialized || requested->presentation.hdr_output !=
					g_requested.presentation.hdr_output) &&
	     !Aeron_SetOutputHdr(next.presentation.hdr_output))) {
		if (g_initialized) {
			Aeron_SetPresentationVsyncDivisor(
				g_requested.presentation.vsync_divisor);
			if (!Aeron_SetOutputHdr(
				    g_requested.presentation.hdr_output)) {
				Aeron_RequestFatalRendererError(
					"restoring HDR output");
			}
		}
		if (sampler_changed) {
			Aeron_DestroySampler(sampler);
		}
		snprintf(error, capacity, "Cannot apply display settings");
		return 0;
	}
#if !defined(__APPLE__)
	Aeron_SetOutputSdrContentGamma(next.presentation.sdr_gamma < 0
					       ? 2.2f
					       : next.presentation.sdr_gamma);
	Aeron_SetOutputPaperWhiteNits(next.presentation.paper_white_nits);
#endif
	AeronScenePresent_ApplySettings(&next.scene.tonemap);
	if (sampler_changed) {
		Aeron_DestroySampler(g_mesh_sampler);
		g_mesh_sampler = sampler;
	}
	xvt_remaster_config_read_output(&next);
	if (!g_initialized || memcmp(&next, &g_effective, sizeof next)) {
		++g_generation;
	}
	g_effective = next;
	g_requested = *requested;
	g_initialized = 1;
	return 1;
}

int xvt_remaster_config_sync(void)
{
	const struct xvt_settings *settings = xvt_config_settings();
	if (!settings) {
		return 0;
	}
	if (!g_initialized ||
	    g_document_generation != xvt_config_generation()) {
		struct xvt_render_settings next = settings->render;
		if (g_video_override) {
			xvt_video_settings_apply_to(&g_video, &next);
		}
		char error[512];
		if (!xvt_remaster_config_apply(&next, error, sizeof error)) {
			XVT_LOG_ERROR("remaster.config_failed error=\"%s\"",
				      error);
			return 0;
		}
		g_document_generation = xvt_config_generation();
	} else {
		struct xvt_render_settings next = g_effective;
		xvt_remaster_config_read_output(&next);
		if (memcmp(&next.presentation, &g_effective.presentation,
			   sizeof next.presentation)) {
			g_effective = next;
			++g_generation;
		}
	}
	return 1;
}

bool xvt_remaster_config_apply_video(const struct xvt_video_settings *previous,
				     const struct xvt_video_settings *requested,
				     char *error, size_t capacity)
{
	if (!xvt_video_settings_validate(requested, error, capacity)) {
		return false;
	}
	int fullscreen = Aeron_Fullscreen();
	if (fullscreen != requested->fullscreen &&
	    !Aeron_SetFullscreen(requested->fullscreen)) {
		snprintf(error, capacity, "Cannot change fullscreen mode");
		return false;
	}
	struct xvt_render_settings next = xvt_config_settings()->render;
	xvt_video_settings_apply_to(requested, &next);
	if (!xvt_remaster_config_apply(&next, error, capacity)) {
		if (fullscreen != requested->fullscreen &&
		    !Aeron_SetFullscreen(fullscreen)) {
			Aeron_RequestFatalRendererError(
				"restoring fullscreen mode");
		}
		return false;
	}
	(void)previous;
	g_video = *requested;
	g_video_override = 1;
	return true;
}

const struct xvt_render_settings *xvt_remaster_config_effective(void)
{
	return g_initialized ? &g_effective : NULL;
}

uint64_t xvt_remaster_config_generation(void) { return g_generation; }

AeronSampler *xvt_remaster_config_mesh_sampler(void) { return g_mesh_sampler; }

void xvt_remaster_config_shutdown(void)
{
	Aeron_DestroySampler(g_mesh_sampler);
	g_mesh_sampler = NULL;
	g_initialized = 0;
	g_video_override = 0;
	g_document_generation = 0;
	g_generation = 0;
}
