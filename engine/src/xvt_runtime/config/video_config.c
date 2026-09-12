#include "xvt_runtime/config/video_config.h"
#include "xvt_runtime/config/config.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

void XvtVideoSettings_Read(const XvtSettings* settings, XvtVideoSettings* out) {
	memset(out, 0, sizeof *out);
	out->cockpit_undither = settings->render.cockpit_undither;
	out->fullscreen = settings->fullscreen;
	out->hdr = settings->render.presentation.hdr_output;
	out->sdr_gamma = settings->render.presentation.sdr_gamma;
	out->paper_white_nits = settings->render.presentation.paper_white_nits;
	out->ssao_quality = settings->render.scene.ssao.ssao_quality;
	out->shadows_enabled = settings->render.scene.shadows.enabled;
	out->shadow_atlas_size = settings->render.scene.shadows.atlas_size;
	out->fsr_mode = settings->render.temporal_mode;
	out->fsr_sharpness = settings->render.temporal_sharpness;
	out->msaa_samples = settings->render.msaa_samples;
	out->motion_blur_quality = settings->render.motion_blur.quality;
	out->motion_blur_shutter = settings->render.motion_blur.shutter;
}

bool XvtVideoSettings_Equals(const XvtVideoSettings* left, const XvtVideoSettings* right) {
	return left->cockpit_undither == right->cockpit_undither && left->fullscreen == right->fullscreen &&
		   left->hdr == right->hdr && left->sdr_gamma == right->sdr_gamma &&
		   left->paper_white_nits == right->paper_white_nits && left->ssao_quality == right->ssao_quality &&
		   left->shadows_enabled == right->shadows_enabled &&
		   left->shadow_atlas_size == right->shadow_atlas_size && left->fsr_mode == right->fsr_mode &&
		   left->fsr_sharpness == right->fsr_sharpness && left->msaa_samples == right->msaa_samples &&
		   left->motion_blur_quality == right->motion_blur_quality &&
		   left->motion_blur_shutter == right->motion_blur_shutter;
}

bool XvtVideoSettings_Validate(const XvtVideoSettings* o, char* error, size_t capacity) {
	bool valid =
		o && (o->cockpit_undither == 0 || o->cockpit_undither == 1) &&
		(o->fullscreen == 0 || o->fullscreen == 1) && (o->hdr == 0 || o->hdr == 1) &&
		(o->sdr_gamma == -1 || o->sdr_gamma == 0 || o->sdr_gamma == 2.2f || o->sdr_gamma == 2.4f) &&
		isfinite(o->paper_white_nits) && o->paper_white_nits >= 0 && o->ssao_quality >= 0 &&
		o->ssao_quality <= 2 && (o->shadows_enabled == 0 || o->shadows_enabled == 1) &&
		(o->shadow_atlas_size == 1024 || o->shadow_atlas_size == 2048 || o->shadow_atlas_size == 4096 ||
		 o->shadow_atlas_size == 8192) &&
		o->fsr_mode >= AERON_TEMPORAL_OFF && o->fsr_mode <= AERON_TEMPORAL_PERFORMANCE &&
		isfinite(o->fsr_sharpness) && o->fsr_sharpness >= 0 && o->fsr_sharpness <= 1 &&
		(o->msaa_samples == 1 || o->msaa_samples == 2 || o->msaa_samples == 4 || o->msaa_samples == 8) &&
		(o->fsr_mode == AERON_TEMPORAL_OFF || o->msaa_samples == 1) && o->motion_blur_quality >= 0 &&
		o->motion_blur_quality <= 2 && isfinite(o->motion_blur_shutter) && o->motion_blur_shutter >= 0 &&
		o->motion_blur_shutter <= 1;
	if (!valid && error && capacity)
		snprintf(error, capacity, "Invalid video settings");
	return valid;
}

void XvtVideoSettings_ApplyTo(const XvtVideoSettings* options, XvtRenderSettings* render) {
	render->cockpit_undither = options->cockpit_undither;
	render->presentation.hdr_output = options->hdr;
	render->presentation.sdr_gamma = options->sdr_gamma;
	render->presentation.paper_white_nits = options->paper_white_nits;
	render->scene.ssao.ssao_quality = options->ssao_quality;
	render->scene.shadows.enabled = options->shadows_enabled;
	render->scene.shadows.atlas_size = options->shadow_atlas_size;
	render->temporal_mode = options->fsr_mode;
	render->temporal_sharpness = options->fsr_sharpness;
	render->msaa_samples = options->msaa_samples;
	render->motion_blur.quality = options->motion_blur_quality;
	render->motion_blur.shutter = options->motion_blur_shutter;
}

static const char* const g_videoPaths[] = {
	"render.cockpit_undither",
	"video.window_mode",
	"presentation.hdr_output",
	"presentation.sdr_gamma",
	"presentation.paper_white_nits",
	"render.ssao.quality",
	"render.shadows.mode",
	"render.shadows.atlas_size",
	"render.temporal_upscaling.mode",
	"render.temporal_upscaling.sharpness",
	"render.msaa_samples",
	"render.motion_blur.quality",
	"render.motion_blur.shutter",
};

static bool XvtConfig_WriteVideo(AeronConfigFile* document, const XvtVideoSettings* options,
								 AeronConfigError* detail) {
	static const char* modes[] = { "off", "native_aa", "quality", "balanced", "performance" };
	return AeronConfigFile_SetBool(document, "render.cockpit_undither", options->cockpit_undither, detail) &&
		   AeronConfigFile_SetString(document, "video.window_mode",
									 options->fullscreen ? "fullscreen" : "windowed", detail) &&
		   AeronConfigFile_SetBool(document, "presentation.hdr_output", options->hdr, detail) &&
		   AeronConfigFile_SetString(document, "presentation.sdr_gamma",
									 options->sdr_gamma < 0       ? "auto"
									 : options->sdr_gamma == 0    ? "srgb"
									 : options->sdr_gamma == 2.2f ? "2.2"
																  : "2.4",
									 detail) &&
		   (options->paper_white_nits == 0
				? AeronConfigFile_SetString(document, "presentation.paper_white_nits", "auto", detail)
				: AeronConfigFile_SetFloat(document, "presentation.paper_white_nits",
										   options->paper_white_nits, detail)) &&
		   AeronConfigFile_SetInt(document, "render.ssao.quality", options->ssao_quality, detail) &&
		   AeronConfigFile_SetString(document, "render.shadows.mode",
									 options->shadows_enabled ? "pcf" : "off", detail) &&
		   AeronConfigFile_SetInt(document, "render.shadows.atlas_size", options->shadow_atlas_size,
								  detail) &&
		   AeronConfigFile_SetString(document, "render.temporal_upscaling.mode", modes[options->fsr_mode],
									 detail) &&
		   AeronConfigFile_SetFloat(document, "render.temporal_upscaling.sharpness", options->fsr_sharpness,
									detail) &&
		   AeronConfigFile_SetInt(document, "render.msaa_samples", options->msaa_samples, detail) &&
		   AeronConfigFile_SetInt(document, "render.motion_blur.quality", options->motion_blur_quality,
								  detail) &&
		   AeronConfigFile_SetFloat(document, "render.motion_blur.shutter", options->motion_blur_shutter,
									detail);
}

bool XvtConfig_SetVideo(const XvtVideoSettings* options, char* error, size_t capacity) {
	AeronConfigFile* candidate = NULL;
	AeronConfigError detail;
	if (!XvtVideoSettings_Validate(options, error, capacity))
		return false;
	if (!AeronConfigFile_Clone(XvtConfig_UserDocument(), &candidate, &detail))
		return XvtSettings_FileError(&detail, error, capacity);
	bool success = XvtConfig_WriteVideo(candidate, options, &detail);
	if (success)
		success = XvtConfig_UpdateUser(candidate, 0, error, capacity);
	else
		XvtSettings_FileError(&detail, error, capacity);
	AeronConfigFile_Destroy(candidate);
	return success;
}

bool XvtConfig_RestoreVideo(char* error, size_t capacity) {
	AeronConfigFile* candidate = NULL;
	AeronConfigError detail;
	if (!AeronConfigFile_Clone(XvtConfig_UserDocument(), &candidate, &detail))
		return XvtSettings_FileError(&detail, error, capacity);
	bool success = true;
	for (size_t i = 0; i < sizeof g_videoPaths / sizeof g_videoPaths[0] && success; ++i)
		if (AeronConfigFile_Has(candidate, g_videoPaths[i]))
			success = AeronConfigFile_Remove(candidate, g_videoPaths[i], &detail);
	if (success)
		success = XvtConfig_UpdateUser(candidate, 0, error, capacity);
	else
		XvtSettings_FileError(&detail, error, capacity);
	AeronConfigFile_Destroy(candidate);
	return success;
}
