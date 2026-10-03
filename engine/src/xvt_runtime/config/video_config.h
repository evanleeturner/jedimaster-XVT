#ifndef XVT_RUNTIME_CONFIG_VIDEO_CONFIG_H
#define XVT_RUNTIME_CONFIG_VIDEO_CONFIG_H
#include "xvt_runtime/config/settings.h"

/* The settings the video page edits, as one flat record: read from and applied to the typed settings,
 * checked, and stored as user overrides. fsr_mode and fsr_sharpness are the temporal upscaling mode and
 * sharpness. */
struct XvtVideoSettings {
	int cockpit_undither;
	int fullscreen;
	int hdr;
	float sdr_gamma;
	float paper_white_nits;
	int ssao_quality;
	int shadows_enabled;
	int shadow_atlas_size;
	AeronTemporalMode fsr_mode;
	float fsr_sharpness;
	int msaa_samples;
	int motion_blur_quality;
	float motion_blur_shutter;
};

/* Copies the video fields out of settings. */
void XvtVideoSettings_Read(const struct XvtSettings *settings,
			   struct XvtVideoSettings *out);
/* true when every field is equal. */
bool XvtVideoSettings_Equals(const struct XvtVideoSettings *left,
			     const struct XvtVideoSettings *right);
/* true when every field is in its allowed range or set; temporal upscaling other than off also needs
 * msaa_samples 1. On failure error says only "Invalid video settings". */
bool XvtVideoSettings_Validate(const struct XvtVideoSettings *options,
			       char *error, size_t capacity);
/* Copies the fields into render, all but fullscreen, which is not a render setting. */
void XvtVideoSettings_ApplyTo(const struct XvtVideoSettings *options,
			      struct XvtRenderSettings *render);
/* Validates options, then writes every video field into the user overrides, even one that equals the
 * default, in memory only. */
bool XvtConfig_SetVideo(const struct XvtVideoSettings *options, char *error,
			size_t capacity);
/* Removes the video fields from the user overrides, so the shipped values apply, in memory only. */
bool XvtConfig_RestoreVideo(char *error, size_t capacity);
#endif
