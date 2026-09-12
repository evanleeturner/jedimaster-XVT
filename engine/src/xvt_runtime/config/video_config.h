#ifndef XVT_RUNTIME_CONFIG_VIDEO_CONFIG_H
#define XVT_RUNTIME_CONFIG_VIDEO_CONFIG_H
#include "xvt_runtime/config/settings.h"

typedef struct XvtVideoSettings {
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
} XvtVideoSettings;

void XvtVideoSettings_Read(const XvtSettings* settings, XvtVideoSettings* out);
bool XvtVideoSettings_Equals(const XvtVideoSettings* left, const XvtVideoSettings* right);
bool XvtVideoSettings_Validate(const XvtVideoSettings* options, char* error, size_t capacity);
void XvtVideoSettings_ApplyTo(const XvtVideoSettings* options, XvtRenderSettings* render);
bool XvtConfig_SetVideo(const XvtVideoSettings* options, char* error, size_t capacity);
bool XvtConfig_RestoreVideo(char* error, size_t capacity);
#endif
