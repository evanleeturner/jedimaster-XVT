#include "xvt_app/settings/video_options.h"
#include "aeron/aeron.h"
#include "xvt_runtime/config/config.h"

static struct {
	XvtVideoSettings defaults, requested, accepted, persisted;
	XvtVideoApplyFn apply;
	bool pending, restore;
	int observed_fullscreen;
} g_video;

void XvtVideoOptions_Configure(XvtVideoApplyFn apply) {
	XvtVideoSettings_Read(XvtConfig_DefaultSettings(), &g_video.defaults);
	XvtVideoSettings_Read(XvtConfig_Settings(), &g_video.requested);
	if (g_video.requested.fsr_mode != AERON_TEMPORAL_OFF)
		g_video.requested.msaa_samples = 1;
	if (g_video.defaults.fsr_mode != AERON_TEMPORAL_OFF)
		g_video.defaults.msaa_samples = 1;
	g_video.accepted = g_video.persisted = g_video.requested;
	g_video.apply = apply;
	g_video.pending = g_video.restore = false;
	g_video.observed_fullscreen = Aeron_Fullscreen();
}

void XvtVideoOptions_Get(XvtVideoSettings* out) { *out = g_video.requested; }

bool XvtVideoOptions_Request(const XvtVideoSettings* options, char* error, size_t capacity) {
	if (!XvtVideoSettings_Validate(options, error, capacity))
		return false;
	g_video.requested = *options;
	g_video.pending = !XvtVideoSettings_Equals(options, &g_video.accepted);
	g_video.restore = false;
	return true;
}

void XvtVideoOptions_RestoreDefaults(void) {
	g_video.requested = g_video.defaults;
	g_video.pending = !XvtVideoSettings_Equals(&g_video.defaults, &g_video.accepted);
	g_video.restore = true;
}

bool XvtVideoOptions_ApplyPending(char* error, size_t capacity) {
	int fullscreen = Aeron_Fullscreen();
	if (fullscreen != g_video.observed_fullscreen && !g_video.pending) {
		g_video.accepted.fullscreen = g_video.requested.fullscreen = fullscreen;
	}
	g_video.observed_fullscreen = fullscreen;
	if (!g_video.pending)
		return true;
	g_video.pending = false;
	if (!g_video.apply || !g_video.apply(&g_video.accepted, &g_video.requested, error, capacity)) {
		g_video.requested = g_video.accepted;
		g_video.restore = false;
		return false;
	}
	g_video.accepted = g_video.requested;
	g_video.observed_fullscreen = Aeron_Fullscreen();
	return true;
}

bool XvtVideoOptions_Flush(bool exiting, char* error, size_t capacity) {
	const XvtVideoSettings* options = exiting ? &g_video.requested : &g_video.accepted;
	if (!g_video.restore && XvtVideoSettings_Equals(options, &g_video.persisted))
		return true;
	bool success = XvtVideoSettings_Equals(options, &g_video.defaults)
					   ? XvtConfig_RestoreVideo(error, capacity)
					   : XvtConfig_SetVideo(options, error, capacity);
	if (success) {
		g_video.persisted = *options;
		g_video.restore = false;
	}
	return success;
}
