#ifndef XVT_RUNTIME_CONFIG_SETTINGS_H
#define XVT_RUNTIME_CONFIG_SETTINGS_H

#include "aeron/compat/dplay_directory.h"
#include "aeron/config_file.h"
#include "aeron/input.h"
#include "aeron/scene/settings.h"
#include "aeron/temporal.h"
#include "xvt_runtime/config/mouse_config.h"
#include "xvt_runtime/input/controller_options.h"
#include "xvt_runtime/input/keyboard_mapping.h"
#include "xvt_runtime/storage/storage.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The typed settings the program reads, parsed from the resolved settings document (see config.h).
 * Every value is required; the shipped defaults supply any the player leaves out. */

struct XvtModelSettings {
	float smooth_angle_degrees;
	float opt_emissive_strength, opt_projectile_emissive_strength,
		engine_emissive_strength;
};

struct XvtPointLightSettings {
	int enabled, clustered, cluster_depth_slices, cluster_debug;
	float scale, range_scale, min_distance, spec_weight, diffuse_wrap,
		contrib_cap;
};

struct XvtLightingSettings {
	float intensity, spec_mul, wrap;
	int spec_geom_adapt;
	float ambient[3];
};

enum { XVT_SKY_STARS, XVT_SKY_CUBE };

struct XvtSkySettings {
	int enabled, mode;
	char path[XVT_PATH_CAPACITY];
	float exposure, star_brightness;
};

struct XvtHyperspaceSettings {
	float travel_speed, rotation_speed, noise_scale, brightness,
		highlight_strength;
	float focal_length, twist, cap_radius, cap_falloff;
	float mesh_ambient_strength, mesh_environment_roughness,
		mesh_key_strength;
	float dark_color[3], body_color[3], highlight_color[3], cap_color[3];
};

struct XvtMotionBlurSettings {
	int quality, camera_blur, pause_keep_blur, velocity_viz,
		fsr_direct_motion;
	float shutter;
};

struct XvtPresentationSettings {
	int vsync_divisor, hdr_output;
	/* Negative gamma follows the platform default; zero selects piecewise sRGB. */
	float sdr_gamma;
	/* Zero follows the OS reference white. */
	float paper_white_nits;
};

struct XvtSceneSettings {
	AeronSceneSsaoSettings ssao;
	AeronSceneShadowSettings shadows;
	AeronSceneTonemapSettings tonemap;
};

struct XvtRenderSettings {
	int cockpit_undither;
	struct XvtSceneSettings scene;
	int msaa_samples;
	AeronTemporalMode temporal_mode;
	float temporal_sharpness;
	struct XvtMotionBlurSettings motion_blur;
	struct XvtPresentationSettings presentation;
	int anisotropic;
	float max_anisotropy;
	struct XvtModelSettings models;
	struct XvtPointLightSettings point_lights;
	struct XvtLightingSettings lighting;
	struct XvtSkySettings sky;
	struct XvtHyperspaceSettings hyperspace;
	float bloom_intensity, explosion_emissive_strength;
};

struct XvtSettings {
	int skip_intro;
	int flight_unlocked;
	char game_data[XVT_PATH_CAPACITY];
	char ui_font[XVT_PATH_CAPACITY];
	char lobby_url[AERON_DPLAY_DIRECTORY_URL_CAPACITY];
	int fullscreen;
	struct XvtControllerOptions controller;
	struct XvtControllerProfile gamepad_defaults;
	struct XvtKeyboardBindings keyboard;
	struct XvtMouseOptions mouse;
	struct XvtRenderSettings render;
};

/* Layers game/user render overrides over the required Aeron scene defaults. */
/* Parses document into *out: every field in the settings table, each of its type and in range (a float
 * may be written as an integer); the choices flight.update_rate, video.window_mode,
 * render.temporal_upscaling.mode and skybox.mode; presentation.sdr_gamma (auto, srgb, 2.2 or 2.4) and
 * paper_white_nits (auto or a positive number); the render block laid over scene_defaults; then the
 * keyboard, controllers, gamepad defaults and mouse. msaa_samples must be 1, 2, 4 or 8, and the cube
 * sky mode needs a path. skybox.mode "procedural" is accepted and read as stars. Returns 1; or 0 with
 * *out unchanged and the first problem in error. */
int XvtSettings_Parse(const AeronConfigFile *document,
		      const struct XvtSceneSettings *scene_defaults,
		      struct XvtSettings *out, char *error, size_t capacity);
/* Writes "ROOT/file:line:column: path: message" for the node at path, or for the document root when
 * path is absent, into error. Returns 0. */
int XvtSettings_NodeError(const AeronConfigFile *document, const char *path,
			  const char *message, char *error, size_t capacity);
/* Writes "ROOT/file:line:column: message" from detail into error. Returns 0. */
int XvtSettings_FileError(const AeronConfigError *detail, char *error,
			  size_t capacity);

#ifdef __cplusplus
}
#endif

#endif
