#include "xvt_runtime/config/settings.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "xvt_runtime/config/controller_config.h"
#include "xvt_runtime/config/keyboard_config.h"

typedef enum xvt_setting_type {
	XVT_SETTING_BOOL,
	XVT_SETTING_INT,
	XVT_SETTING_FLOAT,
	XVT_SETTING_STRING
} xvt_setting_type;

struct xvt_setting_field {
	const char *path;
	size_t offset;
	xvt_setting_type type;
	double minimum;
	double maximum;
};

#define SETTING_BOOL(path, member)                                             \
	{path, offsetof(struct xvt_settings, member), XVT_SETTING_BOOL, 0, 1}
#define SETTING_INT(path, member, low, high)                                   \
	{path, offsetof(struct xvt_settings, member), XVT_SETTING_INT, low,    \
	 high}
#define SETTING_FLOAT(path, member, low, high)                                 \
	{path, offsetof(struct xvt_settings, member), XVT_SETTING_FLOAT, low,  \
	 high}
#define SETTING_STRING(path, member)                                           \
	{path, offsetof(struct xvt_settings, member), XVT_SETTING_STRING, 0,   \
	 sizeof(((struct xvt_settings *)0)->member)}

static const struct xvt_setting_field g_fields[] = {
	SETTING_BOOL("startup.skip_intro", skip_intro),
	SETTING_STRING("paths.game_data", game_data),
	SETTING_STRING("ui.font", ui_font),
	SETTING_STRING("network.lobby_url", lobby_url),
	SETTING_INT("presentation.vsync_divisor",
		    render.presentation.vsync_divisor, 1, 2),
	SETTING_BOOL("presentation.hdr_output", render.presentation.hdr_output),
	SETTING_INT("render.msaa_samples", render.msaa_samples, 1, 8),
	SETTING_BOOL("render.cockpit_undither", render.cockpit_undither),
	SETTING_FLOAT("render.temporal_upscaling.sharpness",
		      render.temporal_sharpness, 0, 1),
	SETTING_BOOL("render.motion_blur.camera_blur",
		     render.motion_blur.camera_blur),
	SETTING_BOOL("render.motion_blur.pause_keep_blur",
		     render.motion_blur.pause_keep_blur),
	SETTING_BOOL("render.motion_blur.velocity_viz",
		     render.motion_blur.velocity_viz),
	SETTING_BOOL("render.motion_blur.fsr_direct_motion",
		     render.motion_blur.fsr_direct_motion),
	SETTING_INT("render.motion_blur.quality", render.motion_blur.quality, 0,
		    2),
	SETTING_FLOAT("render.motion_blur.shutter", render.motion_blur.shutter,
		      0, 1),
	SETTING_BOOL("texture_filtering.anisotropic", render.anisotropic),
	SETTING_FLOAT("texture_filtering.max_anisotropy", render.max_anisotropy,
		      1, 16),
	SETTING_FLOAT("models.smooth_angle_degrees",
		      render.models.smooth_angle_degrees, 0, 180),
	SETTING_FLOAT("models.opt_emissive_strength",
		      render.models.opt_emissive_strength, 0, FLT_MAX),
	SETTING_FLOAT("models.opt_projectile_emissive_strength",
		      render.models.opt_projectile_emissive_strength, 0,
		      FLT_MAX),
	SETTING_FLOAT("models.engine_emissive_strength",
		      render.models.engine_emissive_strength, 0, FLT_MAX),
	SETTING_BOOL("point_lights.enabled", render.point_lights.enabled),
	SETTING_BOOL("point_lights.clustered", render.point_lights.clustered),
	SETTING_BOOL("point_lights.cluster_debug",
		     render.point_lights.cluster_debug),
	SETTING_INT("point_lights.cluster_depth_slices",
		    render.point_lights.cluster_depth_slices, 4, 64),
	SETTING_FLOAT("point_lights.scale", render.point_lights.scale, 0,
		      FLT_MAX),
	SETTING_FLOAT("point_lights.range_scale",
		      render.point_lights.range_scale, FLT_MIN, FLT_MAX),
	SETTING_FLOAT("point_lights.min_distance",
		      render.point_lights.min_distance, FLT_MIN, FLT_MAX),
	SETTING_FLOAT("point_lights.spec_weight",
		      render.point_lights.spec_weight, 0, FLT_MAX),
	SETTING_FLOAT("point_lights.diffuse_wrap",
		      render.point_lights.diffuse_wrap, 0, 1),
	SETTING_FLOAT("point_lights.contrib_cap",
		      render.point_lights.contrib_cap, 0, FLT_MAX),
	SETTING_FLOAT("lighting.intensity", render.lighting.intensity, 0,
		      FLT_MAX),
	SETTING_FLOAT("lighting.spec_mul", render.lighting.spec_mul, 0,
		      FLT_MAX),
	SETTING_FLOAT("lighting.wrap", render.lighting.wrap, 0, 1),
	SETTING_BOOL("lighting.spec_geom_adapt",
		     render.lighting.spec_geom_adapt),
	SETTING_FLOAT("lighting.ambient_r", render.lighting.ambient[0], 0,
		      FLT_MAX),
	SETTING_FLOAT("lighting.ambient_g", render.lighting.ambient[1], 0,
		      FLT_MAX),
	SETTING_FLOAT("lighting.ambient_b", render.lighting.ambient[2], 0,
		      FLT_MAX),
	SETTING_BOOL("skybox.enabled", render.sky.enabled),
	SETTING_STRING("skybox.path", render.sky.path),
	SETTING_FLOAT("skybox.exposure", render.sky.exposure, 0, FLT_MAX),
	SETTING_FLOAT("skybox.star_brightness", render.sky.star_brightness, 0,
		      FLT_MAX),
	SETTING_FLOAT("hyperspace_tunnel.travel_speed",
		      render.hyperspace.travel_speed, FLT_MIN, 64),
	SETTING_FLOAT("hyperspace_tunnel.rotation_speed",
		      render.hyperspace.rotation_speed, -8, 8),
	SETTING_FLOAT("hyperspace_tunnel.noise_scale",
		      render.hyperspace.noise_scale, 0.125, 8),
	SETTING_FLOAT("hyperspace_tunnel.brightness",
		      render.hyperspace.brightness, 0, 16),
	SETTING_FLOAT("hyperspace_tunnel.highlight_strength",
		      render.hyperspace.highlight_strength, 0, 16),
	SETTING_FLOAT("hyperspace_tunnel.focal_length",
		      render.hyperspace.focal_length, 0.25, 4),
	SETTING_FLOAT("hyperspace_tunnel.twist", render.hyperspace.twist, -2,
		      2),
	SETTING_FLOAT("hyperspace_tunnel.cap_radius",
		      render.hyperspace.cap_radius, 0.001, 1),
	SETTING_FLOAT("hyperspace_tunnel.cap_falloff",
		      render.hyperspace.cap_falloff, 0.1, 64),
	SETTING_FLOAT("hyperspace_tunnel.mesh_ambient_strength",
		      render.hyperspace.mesh_ambient_strength, 0, 4),
	SETTING_FLOAT("hyperspace_tunnel.mesh_environment_roughness",
		      render.hyperspace.mesh_environment_roughness, 0, 1),
	SETTING_FLOAT("hyperspace_tunnel.mesh_key_strength",
		      render.hyperspace.mesh_key_strength, 0, 4),
	SETTING_FLOAT("hyperspace_tunnel.dark_color_r",
		      render.hyperspace.dark_color[0], 0, 16),
	SETTING_FLOAT("hyperspace_tunnel.dark_color_g",
		      render.hyperspace.dark_color[1], 0, 16),
	SETTING_FLOAT("hyperspace_tunnel.dark_color_b",
		      render.hyperspace.dark_color[2], 0, 16),
	SETTING_FLOAT("hyperspace_tunnel.body_color_r",
		      render.hyperspace.body_color[0], 0, 16),
	SETTING_FLOAT("hyperspace_tunnel.body_color_g",
		      render.hyperspace.body_color[1], 0, 16),
	SETTING_FLOAT("hyperspace_tunnel.body_color_b",
		      render.hyperspace.body_color[2], 0, 16),
	SETTING_FLOAT("hyperspace_tunnel.highlight_color_r",
		      render.hyperspace.highlight_color[0], 0, 16),
	SETTING_FLOAT("hyperspace_tunnel.highlight_color_g",
		      render.hyperspace.highlight_color[1], 0, 16),
	SETTING_FLOAT("hyperspace_tunnel.highlight_color_b",
		      render.hyperspace.highlight_color[2], 0, 16),
	SETTING_FLOAT("hyperspace_tunnel.cap_color_r",
		      render.hyperspace.cap_color[0], 0, 16),
	SETTING_FLOAT("hyperspace_tunnel.cap_color_g",
		      render.hyperspace.cap_color[1], 0, 16),
	SETTING_FLOAT("hyperspace_tunnel.cap_color_b",
		      render.hyperspace.cap_color[2], 0, 16),
	SETTING_FLOAT("bloom.intensity", render.bloom_intensity, 0, FLT_MAX),
	SETTING_FLOAT("effects.explosion_genus_emissive_strength",
		      render.explosion_emissive_strength, 0, FLT_MAX),
};

#undef SETTING_BOOL
#undef SETTING_INT
#undef SETTING_FLOAT
#undef SETTING_STRING

static const char *xvt_settings_root_name(AeronVfsRoot root)
{
	switch (root) {
	case AERON_VFS_ROOT_RESOURCE:
		return "RESOURCE";
	case AERON_VFS_ROOT_USER:
		return "USER";
	case AERON_VFS_ROOT_TEMP:
		return "TEMP";
	default:
		return "ASSET";
	}
}

int xvt_settings_file_error(const AeronConfigError *detail, char *error,
			    size_t capacity)
{
	snprintf(error, capacity, "%s/%s:%d:%d: %s",
		 xvt_settings_root_name(detail->root), detail->path,
		 detail->line, detail->column, detail->message);
	return 0;
}

int xvt_settings_node_error(const AeronConfigFile *document, const char *path,
			    const char *message, char *error, size_t capacity)
{
	const AeronConfigNode *node = AeronConfigFile_GetNode(document, path);
	const char *source;
	if (!node) {
		node = AeronConfigFile_Root(document);
	}
	source = AeronConfigNode_SourcePath(node);
	snprintf(error, capacity, "%s/%s:%d:%d: %s: %s",
		 xvt_settings_root_name(AeronConfigNode_SourceRoot(node)),
		 source ? source : "config.yaml", AeronConfigNode_Line(node),
		 AeronConfigNode_Column(node), path, message);
	return 0;
}

static int xvt_settings_read_field(const AeronConfigFile *document,
				   const struct xvt_setting_field *field,
				   struct xvt_settings *settings, char *error,
				   size_t capacity)
{
	const AeronConfigNode *node =
		AeronConfigFile_GetNode(document, field->path);
	AeronConfigNodeType type = AeronConfigNode_Type(node);
	unsigned char *destination = (unsigned char *)settings + field->offset;
	const char *problem =
		"missing required setting or incorrect value type";
	if (field->type == XVT_SETTING_STRING && type == AERON_CONFIG_STRING) {
		const char *value = AeronConfigNode_String(node, "");
		if (strlen(value) < (size_t)field->maximum) {
			memcpy(destination, value, strlen(value) + 1);
			return 1;
		}
		problem = "text exceeds the supported length";
	} else if (field->type == XVT_SETTING_BOOL &&
		   type == AERON_CONFIG_BOOL) {
		int value = AeronConfigNode_Bool(node, 0);
		memcpy(destination, &value, sizeof(value));
		return 1;
	} else if (field->type == XVT_SETTING_INT && type == AERON_CONFIG_INT) {
		int64_t value = AeronConfigNode_Int(node, 0);
		if ((double)value >= field->minimum &&
		    (double)value <= field->maximum) {
			int integer = (int)value;
			memcpy(destination, &integer, sizeof(integer));
			return 1;
		}
		problem = "integer outside the supported range";
	} else if (field->type == XVT_SETTING_FLOAT &&
		   (type == AERON_CONFIG_FLOAT || type == AERON_CONFIG_INT)) {
		double value = AeronConfigNode_Float(node, 0);
		if (isfinite(value) && value >= field->minimum &&
		    value <= field->maximum) {
			float number = (float)value;
			memcpy(destination, &number, sizeof(number));
			return 1;
		}
		problem =
			"number must be finite and within the supported range";
	}
	return xvt_settings_node_error(document, field->path, problem, error,
				       capacity);
}

static int xvt_settings_read_choice(const AeronConfigFile *document,
				    const char *path,
				    const char *const *choices, size_t count,
				    int *out, char *error, size_t capacity)
{
	const AeronConfigNode *node = AeronConfigFile_GetNode(document, path);
	const char *value = AeronConfigNode_String(node, NULL);
	if (value) {
		for (size_t i = 0; i < count; ++i) {
			if (!strcmp(value, choices[i])) {
				*out = (int)i;
				return 1;
			}
		}
	}
	return xvt_settings_node_error(document, path,
				       "missing or unsupported option", error,
				       capacity);
}

static int xvt_settings_read_display(const AeronConfigFile *document,
				     struct xvt_presentation_settings *out,
				     char *error, size_t capacity)
{
	const char *gamma_path = "presentation.sdr_gamma";
	const char *white_path = "presentation.paper_white_nits";
	const AeronConfigNode *gamma =
		AeronConfigFile_GetNode(document, gamma_path);
	const AeronConfigNode *white =
		AeronConfigFile_GetNode(document, white_path);
	const char *name = AeronConfigNode_String(gamma, "");
	double value = AeronConfigNode_Float(gamma, -1);
	if (!strcmp(name, "auto")) {
		out->sdr_gamma = -1.0f;
	} else if (!strcmp(name, "srgb")) {
		out->sdr_gamma = 0.0f;
	} else if (value == 2.2 || !strcmp(name, "2.2")) {
		out->sdr_gamma = 2.2f;
	} else if (value == 2.4 || !strcmp(name, "2.4")) {
		out->sdr_gamma = 2.4f;
	} else {
		return xvt_settings_node_error(
			document, gamma_path, "expected auto, srgb, 2.2 or 2.4",
			error, capacity);
	}
	name = AeronConfigNode_String(white, "");
	value = AeronConfigNode_Float(white, -1);
	if (!strcmp(name, "auto")) {
		out->paper_white_nits = 0.0f;
	} else if (isfinite(value) && value > 0 && value <= FLT_MAX) {
		out->paper_white_nits = (float)value;
	} else {
		return xvt_settings_node_error(
			document, white_path,
			"expected auto or a positive finite luminance", error,
			capacity);
	}
	return 1;
}

int xvt_settings_parse(const AeronConfigFile *document,
		       const struct xvt_scene_settings *scene_defaults,
		       struct xvt_settings *out, char *error, size_t capacity)
{
	struct xvt_settings candidate = {0};
	AeronConfigError detail;
	static const char *const flight_rates[] = {"native", "unlocked"};
	static const char *const window_modes[] = {"windowed", "fullscreen"};
	static const char *const temporal_modes[] = {
		"off", "native_aa", "quality", "balanced", "performance"};
	static const char *const sky_modes[] = {"stars", "cube", "procedural"};
	int temporal_mode;
	if (!document || !scene_defaults || !out) {
		return xvt_settings_node_error(
			document, "",
			"configuration document and output are required", error,
			capacity);
	}
	for (size_t i = 0; i < sizeof(g_fields) / sizeof(g_fields[0]); ++i) {
		if (!xvt_settings_read_field(document, &g_fields[i], &candidate,
					     error, capacity)) {
			return 0;
		}
	}
	if (!xvt_settings_read_choice(
		    document, "flight.update_rate", flight_rates, 2,
		    &candidate.flight_unlocked, error, capacity) ||
	    !xvt_settings_read_choice(document, "video.window_mode",
				      window_modes, 2, &candidate.fullscreen,
				      error, capacity) ||
	    !xvt_settings_read_choice(
		    document, "render.temporal_upscaling.mode", temporal_modes,
		    5, &temporal_mode, error, capacity) ||
	    !xvt_settings_read_choice(document, "skybox.mode", sky_modes, 3,
				      &candidate.render.sky.mode, error,
				      capacity) ||
	    !xvt_settings_read_display(document, &candidate.render.presentation,
				       error, capacity)) {
		return 0;
	}
	candidate.render.temporal_mode = (AeronTemporalMode)temporal_mode;
	if (candidate.render.sky.mode == 2) {
		candidate.render.sky.mode = XVT_SKY_STARS;
	}
	if (candidate.render.msaa_samples != 1 &&
	    candidate.render.msaa_samples != 2 &&
	    candidate.render.msaa_samples != 4 &&
	    candidate.render.msaa_samples != 8) {
		return xvt_settings_node_error(document, "render.msaa_samples",
					       "expected 1, 2, 4 or 8", error,
					       capacity);
	}
	if (candidate.render.sky.mode == XVT_SKY_CUBE &&
	    !candidate.render.sky.path[0]) {
		return xvt_settings_node_error(document, "skybox.path",
					       "cube mode requires a path",
					       error, capacity);
	}
	candidate.render.scene = *scene_defaults;
	if (!AeronSceneSettings_Overlay(
		    AeronConfigFile_GetNode(document, "render"),
		    &candidate.render.scene.ssao,
		    &candidate.render.scene.shadows,
		    &candidate.render.scene.tonemap, &detail)) {
		return xvt_settings_file_error(&detail, error, capacity);
	}
	if (!xvt_keyboard_config_read(document, &candidate.keyboard, error,
				      capacity) ||
	    !xvt_controller_config_parse(document, &candidate.controller, error,
					 capacity) ||
	    !xvt_controller_config_read_profile(
		    document, "input.gamepad_defaults",
		    AERON_CONTROLLER_KIND_GAMEPAD, &candidate.gamepad_defaults,
		    error, capacity)) {
		return 0;
	}
	if (!xvt_mouse_config_parse(document, &candidate.mouse, error,
				    capacity)) {
		return 0;
	}
	*out = candidate;
	return 1;
}
