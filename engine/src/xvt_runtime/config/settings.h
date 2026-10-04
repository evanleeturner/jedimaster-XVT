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

struct xvt_model_settings {
	float smooth_angle_degrees;
	float opt_emissive_strength;
	float opt_projectile_emissive_strength;
	float engine_emissive_strength;
};

struct xvt_point_light_settings {
	int enabled;
	int clustered;
	int cluster_depth_slices;
	int cluster_debug;
	float scale;
	float range_scale;
	float min_distance;
	float spec_weight;
	float diffuse_wrap;
	float contrib_cap;
};

struct xvt_lighting_settings {
	float intensity;
	float spec_mul;
	float wrap;
	int spec_geom_adapt;
	float ambient[3];
};

enum { XVT_SKY_STARS, XVT_SKY_CUBE };

struct xvt_sky_settings {
	int enabled;
	int mode;
	char path[XVT_PATH_CAPACITY];
	float exposure;
	float star_brightness;
};

struct xvt_hyperspace_settings {
	float travel_speed;
	float rotation_speed;
	float noise_scale;
	float brightness;
	float highlight_strength;
	float focal_length;
	float twist;
	float cap_radius;
	float cap_falloff;
	float mesh_ambient_strength;
	float mesh_environment_roughness;
	float mesh_key_strength;
	float dark_color[3];
	float body_color[3];
	float highlight_color[3];
	float cap_color[3];
};

struct xvt_motion_blur_settings {
	int quality;
	int camera_blur;
	int pause_keep_blur;
	int velocity_viz;
	int fsr_direct_motion;
	float shutter;
};

struct xvt_presentation_settings {
	int vsync_divisor;
	int hdr_output;
	/* Negative gamma follows the platform default; zero selects piecewise sRGB. */
	float sdr_gamma;
	/* Zero follows the OS reference white. */
	float paper_white_nits;
};

struct xvt_scene_settings {
	AeronSceneSsaoSettings ssao;
	AeronSceneShadowSettings shadows;
	AeronSceneTonemapSettings tonemap;
};

struct xvt_render_settings {
	int cockpit_undither;
	struct xvt_scene_settings scene;
	int msaa_samples;
	AeronTemporalMode temporal_mode;
	float temporal_sharpness;
	struct xvt_motion_blur_settings motion_blur;
	struct xvt_presentation_settings presentation;
	int anisotropic;
	float max_anisotropy;
	struct xvt_model_settings models;
	struct xvt_point_light_settings point_lights;
	struct xvt_lighting_settings lighting;
	struct xvt_sky_settings sky;
	struct xvt_hyperspace_settings hyperspace;
	float bloom_intensity;
	float explosion_emissive_strength;
};

struct xvt_settings {
	int skip_intro;
	int flight_unlocked;
	char game_data[XVT_PATH_CAPACITY];
	char ui_font[XVT_PATH_CAPACITY];
	char lobby_url[AERON_DPLAY_DIRECTORY_URL_CAPACITY];
	int fullscreen;
	struct xvt_controller_options controller;
	struct xvt_controller_profile gamepad_defaults;
	struct xvt_keyboard_bindings keyboard;
	struct xvt_mouse_options mouse;
	struct xvt_render_settings render;
};

/* Layers game/user render overrides over the required Aeron scene defaults. */
/* Parses document into *out: every field in the settings table, each of its
 * type and in range (a float may be written as an integer); the choices
 * flight.update_rate, video.window_mode, render.temporal_upscaling.mode and
 * skybox.mode; presentation.sdr_gamma (auto, srgb, 2.2 or 2.4) and
 * paper_white_nits (auto or a positive number); the render block laid over
 * scene_defaults; then the keyboard, controllers, gamepad defaults and mouse.
 * msaa_samples must be 1, 2, 4 or 8, and the cube sky mode needs a path.
 * skybox.mode "procedural" is accepted and read as stars. Returns 1; or 0 with
 * *out unchanged and the first problem in error. */
int xvt_settings_parse(const AeronConfigFile *document,
		       const struct xvt_scene_settings *scene_defaults,
		       struct xvt_settings *out, char *error, size_t capacity);
/* Writes "ROOT/file:line:column: path: message" for the node at path, or for the document root when
 * path is absent, into error. Returns 0. */
int xvt_settings_node_error(const AeronConfigFile *document, const char *path,
			    const char *message, char *error, size_t capacity);
/* Writes "ROOT/file:line:column: message" from detail into error. Returns 0. */
int xvt_settings_file_error(const AeronConfigError *detail, char *error,
			    size_t capacity);

#ifdef __cplusplus
}
#endif

#endif
