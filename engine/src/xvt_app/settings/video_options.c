#include "xvt_app/settings/video_options.h"

#include "aeron/aeron.h"
#include "xvt_runtime/config/config.h"

static struct {
	struct xvt_video_settings defaults, requested, accepted, persisted;
	xvt_video_apply_fn apply;
	bool pending, restore;
	int observed_fullscreen;
} g_video;

void xvt_video_options_configure(xvt_video_apply_fn apply)
{
	xvt_video_settings_read(xvt_config_default_settings(),
				&g_video.defaults);
	xvt_video_settings_read(xvt_config_settings(), &g_video.requested);
	if (g_video.requested.fsr_mode != AERON_TEMPORAL_OFF) {
		g_video.requested.msaa_samples = 1;
	}
	if (g_video.defaults.fsr_mode != AERON_TEMPORAL_OFF) {
		g_video.defaults.msaa_samples = 1;
	}
	g_video.accepted = g_video.persisted = g_video.requested;
	g_video.apply = apply;
	g_video.pending = g_video.restore = false;
	g_video.observed_fullscreen = Aeron_Fullscreen();
}

void xvt_video_options_get(struct xvt_video_settings *out)
{
	*out = g_video.requested;
}

bool xvt_video_options_request(const struct xvt_video_settings *options,
			       char *error, size_t capacity)
{
	if (!xvt_video_settings_validate(options, error, capacity)) {
		return false;
	}
	g_video.requested = *options;
	g_video.pending =
		!xvt_video_settings_equals(options, &g_video.accepted);
	g_video.restore = false;
	return true;
}

void xvt_video_options_restore_defaults(void)
{
	g_video.requested = g_video.defaults;
	g_video.pending = !xvt_video_settings_equals(&g_video.defaults,
						     &g_video.accepted);
	g_video.restore = true;
}

bool xvt_video_options_apply_pending(char *error, size_t capacity)
{
	int fullscreen = Aeron_Fullscreen();
	if (fullscreen != g_video.observed_fullscreen && !g_video.pending) {
		g_video.accepted.fullscreen = g_video.requested.fullscreen =
			fullscreen;
	}
	g_video.observed_fullscreen = fullscreen;
	if (!g_video.pending) {
		return true;
	}
	g_video.pending = false;
	if (!g_video.apply ||
	    !g_video.apply(&g_video.accepted, &g_video.requested, error,
			   capacity)) {
		g_video.requested = g_video.accepted;
		g_video.restore = false;
		return false;
	}
	g_video.accepted = g_video.requested;
	g_video.observed_fullscreen = Aeron_Fullscreen();
	return true;
}

bool xvt_video_options_flush(bool exiting, char *error, size_t capacity)
{
	const struct xvt_video_settings *options =
		exiting ? &g_video.requested : &g_video.accepted;
	if (!g_video.restore &&
	    xvt_video_settings_equals(options, &g_video.persisted)) {
		return true;
	}
	bool success = xvt_video_settings_equals(options, &g_video.defaults)
			       ? xvt_config_restore_video(error, capacity)
			       : xvt_config_set_video(options, error, capacity);
	if (success) {
		g_video.persisted = *options;
		g_video.restore = false;
	}
	return success;
}
