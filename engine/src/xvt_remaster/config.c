#include "xvt_remaster/config.h"
#include "aeron/aeron.h"
#include "xvt_runtime/config/config.h"
#include <stdio.h>
#include <string.h>
static XvtRenderSettings g_effective, g_requested;
static XvtVideoSettings g_video;
static uint64_t g_generation, g_documentGeneration;
static int g_initialized, g_videoOverride;
static AeronSampler* g_meshSampler;

static void XvtRemasterConfig_ReadOutput(XvtRenderSettings* settings) {
	settings->presentation.hdr_output = Aeron_OutputHdrEnabled();
	settings->presentation.sdr_gamma = Aeron_OutputSdrContentGamma();
	settings->presentation.paper_white_nits = Aeron_OutputPaperWhiteNits();
}

static int XvtRemasterConfig_Apply(const XvtRenderSettings* requested, char* error, size_t capacity) {
	XvtRenderSettings next = *requested;
	if (next.temporal_mode != AERON_TEMPORAL_OFF)
		next.msaa_samples = 1;
	while (next.msaa_samples > 1 &&
		   (!Aeron_TextureFormatSupportsSampleCount(AERON_TEXTURE_FORMAT_RGBA16_FLOAT,
													(AeronSampleCount)next.msaa_samples) ||
			!Aeron_TextureFormatSupportsSampleCount(AERON_TEXTURE_FORMAT_D32_FLOAT,
													(AeronSampleCount)next.msaa_samples)))
		next.msaa_samples /= 2;
	bool sampler_changed = !g_initialized || next.anisotropic != g_effective.anisotropic ||
						   next.max_anisotropy != g_effective.max_anisotropy;
	AeronSampler* sampler = g_meshSampler;
	if (sampler_changed) {
		sampler = next.anisotropic
					  ? Aeron_CreateSampler(&(AeronSamplerDesc) { .min_filter = AERON_FILTER_LINEAR,
																  .mag_filter = AERON_FILTER_LINEAR,
																  .mip_filter = AERON_FILTER_LINEAR,
																  .address_u = AERON_ADDRESS_CLAMP_TO_EDGE,
																  .address_v = AERON_ADDRESS_CLAMP_TO_EDGE,
																  .address_w = AERON_ADDRESS_CLAMP_TO_EDGE,
																  .max_lod = 1000,
																  .enable_anisotropy = 1,
																  .max_anisotropy = next.max_anisotropy })
					  : NULL;
		if (next.anisotropic && !sampler) {
			snprintf(error, capacity, "Cannot create texture sampler");
			return 0;
		}
	}
	if (!Aeron_SetPresentationVsyncDivisor(next.presentation.vsync_divisor) ||
		((!g_initialized || requested->presentation.hdr_output != g_requested.presentation.hdr_output) &&
		 !Aeron_SetOutputHdr(next.presentation.hdr_output))) {
		if (g_initialized) {
			Aeron_SetPresentationVsyncDivisor(g_requested.presentation.vsync_divisor);
			if (!Aeron_SetOutputHdr(g_requested.presentation.hdr_output))
				Aeron_RequestFatalRendererError("restoring HDR output");
		}
		if (sampler_changed)
			Aeron_DestroySampler(sampler);
		snprintf(error, capacity, "Cannot apply display settings");
		return 0;
	}
#if !defined(__APPLE__)
	Aeron_SetOutputSdrContentGamma(next.presentation.sdr_gamma < 0 ? 2.2f : next.presentation.sdr_gamma);
	Aeron_SetOutputPaperWhiteNits(next.presentation.paper_white_nits);
#endif
	AeronScenePresent_ApplySettings(&next.scene.tonemap);
	if (sampler_changed) {
		Aeron_DestroySampler(g_meshSampler);
		g_meshSampler = sampler;
	}
	XvtRemasterConfig_ReadOutput(&next);
	if (!g_initialized || memcmp(&next, &g_effective, sizeof next))
		++g_generation;
	g_effective = next;
	g_requested = *requested;
	g_initialized = 1;
	return 1;
}

int XvtRemasterConfig_Sync(void) {
	const XvtSettings* settings = XvtConfig_Settings();
	if (!settings)
		return 0;
	if (!g_initialized || g_documentGeneration != XvtConfig_Generation()) {
		XvtRenderSettings next = settings->render;
		if (g_videoOverride)
			XvtVideoSettings_ApplyTo(&g_video, &next);
		char error[512];
		if (!XvtRemasterConfig_Apply(&next, error, sizeof error)) {
			Aeron_LogError("xvt.remaster", "%s", error);
			return 0;
		}
		g_documentGeneration = XvtConfig_Generation();
	} else {
		XvtRenderSettings next = g_effective;
		XvtRemasterConfig_ReadOutput(&next);
		if (memcmp(&next.presentation, &g_effective.presentation, sizeof next.presentation)) {
			g_effective = next;
			++g_generation;
		}
	}
	return 1;
}

bool XvtRemasterConfig_ApplyVideo(const XvtVideoSettings* previous, const XvtVideoSettings* requested,
								  char* error, size_t capacity) {
	if (!XvtVideoSettings_Validate(requested, error, capacity))
		return false;
	int fullscreen = Aeron_Fullscreen();
	if (fullscreen != requested->fullscreen && !Aeron_SetFullscreen(requested->fullscreen)) {
		snprintf(error, capacity, "Cannot change fullscreen mode");
		return false;
	}
	XvtRenderSettings next = XvtConfig_Settings()->render;
	XvtVideoSettings_ApplyTo(requested, &next);
	if (!XvtRemasterConfig_Apply(&next, error, capacity)) {
		if (fullscreen != requested->fullscreen && !Aeron_SetFullscreen(fullscreen))
			Aeron_RequestFatalRendererError("restoring fullscreen mode");
		return false;
	}
	(void)previous;
	g_video = *requested;
	g_videoOverride = 1;
	return true;
}

const XvtRenderSettings* XvtRemasterConfig_Effective(void) { return g_initialized ? &g_effective : NULL; }

uint64_t XvtRemasterConfig_Generation(void) { return g_generation; }

AeronSampler* XvtRemasterConfig_MeshSampler(void) { return g_meshSampler; }

void XvtRemasterConfig_Shutdown(void) {
	Aeron_DestroySampler(g_meshSampler);
	g_meshSampler = NULL;
	g_initialized = g_videoOverride = 0;
	g_documentGeneration = g_generation = 0;
}
