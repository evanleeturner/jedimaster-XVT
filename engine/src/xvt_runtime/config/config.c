#include "xvt_runtime/config/config.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aeron/aeron.h"
#include "aeron/compat/dplay_directory.h"
#include "xvt/net/net.h"
#include "xvt_runtime/config/controller_config.h"
#include "xvt_runtime/config/keyboard_config.h"
#include "xvt_runtime/log/log.h"
#include "xvt_runtime/storage/file_io.h"
#include "xvt_runtime/storage/storage.h"

struct xvt_config_field {
	const char *legacy;
	const char *path;
	size_t offset;
	size_t size;
	int string;
	int64_t maximum;
};

static const struct xvt_config_field g_fields[] = {
	{"lastpilot", "game.lastpilot",
	 offsetof(struct game_config, last_pilot_name),
	 sizeof(((struct game_config *)0)->last_pilot_name), 1, 1},
	{"backdrop1", "game.single_player.backdrop",
	 offsetof(struct game_config, backdrop[0]),
	 sizeof(((struct game_config *)0)->backdrop[0]), 0, 1},
	{"backdrop2", "game.multiplayer.backdrop",
	 offsetof(struct game_config, backdrop[1]),
	 sizeof(((struct game_config *)0)->backdrop[1]), 0, 1},
	{"stardensity1", "game.single_player.stardensity",
	 offsetof(struct game_config, star_density[0]),
	 sizeof(((struct game_config *)0)->star_density[0]), 0, 2},
	{"stardensity2", "game.multiplayer.stardensity",
	 offsetof(struct game_config, star_density[1]),
	 sizeof(((struct game_config *)0)->star_density[1]), 0, 2},
	{"debris1", "game.single_player.debris",
	 offsetof(struct game_config, debris[0]),
	 sizeof(((struct game_config *)0)->debris[0]), 0, 1},
	{"debris2", "game.multiplayer.debris",
	 offsetof(struct game_config, debris[1]),
	 sizeof(((struct game_config *)0)->debris[1]), 0, 1},
	{"locallights1", "game.single_player.locallights",
	 offsetof(struct game_config, local_lights[0]),
	 sizeof(((struct game_config *)0)->local_lights[0]), 0, 1},
	{"locallights2", "game.multiplayer.locallights",
	 offsetof(struct game_config, local_lights[1]),
	 sizeof(((struct game_config *)0)->local_lights[1]), 0, 1},
	{"specular1", "game.single_player.specular",
	 offsetof(struct game_config, specular[0]),
	 sizeof(((struct game_config *)0)->specular[0]), 0, 1},
	{"specular2", "game.multiplayer.specular",
	 offsetof(struct game_config, specular[1]),
	 sizeof(((struct game_config *)0)->specular[1]), 0, 1},
	{"diffuse1", "game.single_player.diffuse",
	 offsetof(struct game_config, diffuse[0]),
	 sizeof(((struct game_config *)0)->diffuse[0]), 0, 1},
	{"diffuse2", "game.multiplayer.diffuse",
	 offsetof(struct game_config, diffuse[1]),
	 sizeof(((struct game_config *)0)->diffuse[1]), 0, 1},
	{"dither1", "game.single_player.dither",
	 offsetof(struct game_config, dither[0]),
	 sizeof(((struct game_config *)0)->dither[0]), 0, 1},
	{"dither2", "game.multiplayer.dither",
	 offsetof(struct game_config, dither[1]),
	 sizeof(((struct game_config *)0)->dither[1]), 0, 1},
	{"textureres1", "game.single_player.textureres",
	 offsetof(struct game_config, texture_res[0]),
	 sizeof(((struct game_config *)0)->texture_res[0]), 0, 2},
	{"textureres2", "game.multiplayer.textureres",
	 offsetof(struct game_config, texture_res[1]),
	 sizeof(((struct game_config *)0)->texture_res[1]), 0, 2},
	{"mipmap1", "game.single_player.mipmap",
	 offsetof(struct game_config, mipmap[0]),
	 sizeof(((struct game_config *)0)->mipmap[0]), 0, 19},
	{"mipmap2", "game.multiplayer.mipmap",
	 offsetof(struct game_config, mipmap[1]),
	 sizeof(((struct game_config *)0)->mipmap[1]), 0, 19},
	{"lod1", "game.single_player.lod", offsetof(struct game_config, lod[0]),
	 sizeof(((struct game_config *)0)->lod[0]), 0, 19},
	{"lod2", "game.multiplayer.lod", offsetof(struct game_config, lod[1]),
	 sizeof(((struct game_config *)0)->lod[1]), 0, 19},
	{"screenres1", "game.single_player.screenres",
	 offsetof(struct game_config, screen_res[0]),
	 sizeof(((struct game_config *)0)->screen_res[0]), 0, 2},
	{"screenres2", "game.multiplayer.screenres",
	 offsetof(struct game_config, screen_res[1]),
	 sizeof(((struct game_config *)0)->screen_res[1]), 0, 2},
	{"windowsize1", "game.single_player.windowsize",
	 offsetof(struct game_config, window_size[0]),
	 sizeof(((struct game_config *)0)->window_size[0]), 0, 2},
	{"windowsize2", "game.multiplayer.windowsize",
	 offsetof(struct game_config, window_size[1]),
	 sizeof(((struct game_config *)0)->window_size[1]), 0, 2},
	{"bpp1", "game.single_player.bpp",
	 offsetof(struct game_config, color_depth_choice[0]),
	 sizeof(((struct game_config *)0)->color_depth_choice[0]), 0, 1},
	{"bpp2", "game.multiplayer.bpp",
	 offsetof(struct game_config, color_depth_choice[1]),
	 sizeof(((struct game_config *)0)->color_depth_choice[1]), 0, 1},
	{"brightness1", "game.single_player.brightness",
	 offsetof(struct game_config, brightness[0]),
	 sizeof(((struct game_config *)0)->brightness[0]), 0, 7},
	{"brightness2", "game.multiplayer.brightness",
	 offsetof(struct game_config, brightness[1]),
	 sizeof(((struct game_config *)0)->brightness[1]), 0, 7},
	{"use_3d_hardware1", "game.single_player.use_3d_hardware",
	 offsetof(struct game_config, use3d_hardware[0]),
	 sizeof(((struct game_config *)0)->use3d_hardware[0]), 0, 1},
	{"use_3d_hardware2", "game.multiplayer.use_3d_hardware",
	 offsetof(struct game_config, use3d_hardware[1]),
	 sizeof(((struct game_config *)0)->use3d_hardware[1]), 0, 1},
	{"bilinear1", "game.single_player.bilinear",
	 offsetof(struct game_config, bilinear[0]),
	 sizeof(((struct game_config *)0)->bilinear[0]), 0, 1},
	{"bilinear2", "game.multiplayer.bilinear",
	 offsetof(struct game_config, bilinear[1]),
	 sizeof(((struct game_config *)0)->bilinear[1]), 0, 1},
	{"server_update_rate", "game.server_update_rate",
	 offsetof(struct game_config, server_update_rate),
	 sizeof(((struct game_config *)0)->server_update_rate), 0, 255},
	{"sfx_exterior", "game.sfx_exterior",
	 offsetof(struct game_config, sfx_exterior_enabled),
	 sizeof(((struct game_config *)0)->sfx_exterior_enabled), 0, 1},
	{"sfx_interior", "game.sfx_interior",
	 offsetof(struct game_config, sfx_interior_enabled),
	 sizeof(((struct game_config *)0)->sfx_interior_enabled), 0, 1},
	{"sfx_engine", "game.sfx_engine",
	 offsetof(struct game_config, sfx_engine_enabled),
	 sizeof(((struct game_config *)0)->sfx_engine_enabled), 0, 1},
	{"sfx_datapad", "game.sfx_datapad",
	 offsetof(struct game_config, sfx_datapad_enabled),
	 sizeof(((struct game_config *)0)->sfx_datapad_enabled), 0, 1},
	{"voice_pilot", "game.voice_pilot",
	 offsetof(struct game_config, voice_pilot_level),
	 sizeof(((struct game_config *)0)->voice_pilot_level), 0, 2},
	{"voice_tactical_officer", "game.voice_tactical_officer",
	 offsetof(struct game_config, voice_tactical_officer_level),
	 sizeof(((struct game_config *)0)->voice_tactical_officer_level), 0, 2},
	{"voice_commander", "game.voice_commander",
	 offsetof(struct game_config, voice_commander_enabled),
	 sizeof(((struct game_config *)0)->voice_commander_enabled), 0, 1},
	{"voice_special", "game.voice_special",
	 offsetof(struct game_config, voice_special_enabled),
	 sizeof(((struct game_config *)0)->voice_special_enabled), 0, 1},
	{"music", "game.music", offsetof(struct game_config, music_enabled),
	 sizeof(((struct game_config *)0)->music_enabled), 0, 1},
	{"sfx_datapad_volume", "game.sfx_datapad_volume",
	 offsetof(struct game_config, sfx_datapad_volume),
	 sizeof(((struct game_config *)0)->sfx_datapad_volume), 0, 9},
	{"sfx_exterior_volume", "game.sfx_exterior_volume",
	 offsetof(struct game_config, sfx_exterior_volume),
	 sizeof(((struct game_config *)0)->sfx_exterior_volume), 0, 9},
	{"sfx_interior_volume", "game.sfx_interior_volume",
	 offsetof(struct game_config, sfx_interior_volume),
	 sizeof(((struct game_config *)0)->sfx_interior_volume), 0, 9},
	{"sfx_engine_volume", "game.sfx_engine_volume",
	 offsetof(struct game_config, sfx_engine_volume),
	 sizeof(((struct game_config *)0)->sfx_engine_volume), 0, 9},
	{"voice_volume", "game.voice_volume",
	 offsetof(struct game_config, voice_volume),
	 sizeof(((struct game_config *)0)->voice_volume), 0, 9},
	{"music_volume", "game.music_volume",
	 offsetof(struct game_config, music_volume),
	 sizeof(((struct game_config *)0)->music_volume), 0, 9},
	{"datapad_music", "game.datapad_music",
	 offsetof(struct game_config, datapad_music_enabled),
	 sizeof(((struct game_config *)0)->datapad_music_enabled), 0, 1},
	{"joybutton1", "game.joybutton1",
	 offsetof(struct game_config, joy_buttons[0]),
	 sizeof(((struct game_config *)0)->joy_buttons[0]), 0, 255},
	{"joybutton2", "game.joybutton2",
	 offsetof(struct game_config, joy_buttons[1]),
	 sizeof(((struct game_config *)0)->joy_buttons[1]), 0, 255},
	{"joybutton3", "game.joybutton3",
	 offsetof(struct game_config, joy_buttons[2]),
	 sizeof(((struct game_config *)0)->joy_buttons[2]), 0, 255},
	{"joybutton4", "game.joybutton4",
	 offsetof(struct game_config, joy_buttons[3]),
	 sizeof(((struct game_config *)0)->joy_buttons[3]), 0, 255},
	{"joybutton5", "game.joybutton5",
	 offsetof(struct game_config, joy_buttons[4]),
	 sizeof(((struct game_config *)0)->joy_buttons[4]), 0, 255},
	{"joybutton6", "game.joybutton6",
	 offsetof(struct game_config, joy_buttons[5]),
	 sizeof(((struct game_config *)0)->joy_buttons[5]), 0, 255},
	{"joybutton7", "game.joybutton7",
	 offsetof(struct game_config, joy_buttons[6]),
	 sizeof(((struct game_config *)0)->joy_buttons[6]), 0, 255},
	{"joybutton8", "game.joybutton8",
	 offsetof(struct game_config, joy_buttons[7]),
	 sizeof(((struct game_config *)0)->joy_buttons[7]), 0, 255},
	{"joybutton9", "game.joybutton9",
	 offsetof(struct game_config, joy_buttons[8]),
	 sizeof(((struct game_config *)0)->joy_buttons[8]), 0, 255},
	{"joybutton10", "game.joybutton10",
	 offsetof(struct game_config, joy_buttons[9]),
	 sizeof(((struct game_config *)0)->joy_buttons[9]), 0, 255},
	{"joybutton11", "game.joybutton11",
	 offsetof(struct game_config, joy_buttons[10]),
	 sizeof(((struct game_config *)0)->joy_buttons[10]), 0, 255},
	{"joybutton12", "game.joybutton12",
	 offsetof(struct game_config, joy_buttons[11]),
	 sizeof(((struct game_config *)0)->joy_buttons[11]), 0, 255},
	{"joybutton13", "game.joybutton13",
	 offsetof(struct game_config, joy_buttons[12]),
	 sizeof(((struct game_config *)0)->joy_buttons[12]), 0, 255},
	{"joybutton14", "game.joybutton14",
	 offsetof(struct game_config, joy_buttons[13]),
	 sizeof(((struct game_config *)0)->joy_buttons[13]), 0, 255},
	{"joybutton15", "game.joybutton15",
	 offsetof(struct game_config, joy_buttons[14]),
	 sizeof(((struct game_config *)0)->joy_buttons[14]), 0, 255},
	{"joybutton16", "game.joybutton16",
	 offsetof(struct game_config, joy_buttons[15]),
	 sizeof(((struct game_config *)0)->joy_buttons[15]), 0, 255},
	{"joybutton17", "game.joybutton17",
	 offsetof(struct game_config, joy_buttons[16]),
	 sizeof(((struct game_config *)0)->joy_buttons[16]), 0, 255},
	{"joybutton18", "game.joybutton18",
	 offsetof(struct game_config, joy_buttons[17]),
	 sizeof(((struct game_config *)0)->joy_buttons[17]), 0, 255},
	{"joybutton19", "game.joybutton19",
	 offsetof(struct game_config, joy_buttons[18]),
	 sizeof(((struct game_config *)0)->joy_buttons[18]), 0, 255},
	{"joybutton20", "game.joybutton20",
	 offsetof(struct game_config, joy_buttons[19]),
	 sizeof(((struct game_config *)0)->joy_buttons[19]), 0, 255},
	{"difficulty", "game.difficulty",
	 offsetof(struct game_config, difficulty),
	 sizeof(((struct game_config *)0)->difficulty), 0, 3},
	{"collisions", "game.collisions",
	 offsetof(struct game_config, collisions),
	 sizeof(((struct game_config *)0)->collisions), 0, 1},
	{"craft_jumping", "game.craft_jumping",
	 offsetof(struct game_config, craft_jumping),
	 sizeof(((struct game_config *)0)->craft_jumping), 0, 1},
	{"random_setup", "game.random_setup",
	 offsetof(struct game_config, random_setup),
	 sizeof(((struct game_config *)0)->random_setup), 0, 1},
	/* Battle length, 0 to 2 for two to four wins; the YAML path still calls it handicapping. */
	{"handicapping", "game.handicapping",
	 offsetof(struct game_config, battle_length_index),
	 sizeof(((struct game_config *)0)->battle_length_index), 0, 2},
	{"require_password", "game.require_password",
	 offsetof(struct game_config, require_password),
	 sizeof(((struct game_config *)0)->require_password), 0, 1},
	{"in_progress_join", "game.in_progress_join",
	 offsetof(struct game_config, in_progress_join),
	 sizeof(((struct game_config *)0)->in_progress_join), 0, 1},
	{"craft_selection", "game.craft_selection",
	 offsetof(struct game_config, craft_selection),
	 sizeof(((struct game_config *)0)->craft_selection), 0, 2},
	{"locate_players", "game.locate_players",
	 offsetof(struct game_config, locate_players),
	 sizeof(((struct game_config *)0)->locate_players), 0, 1},
	{"craft_waves", "game.craft_waves",
	 offsetof(struct game_config, craft_waves),
	 sizeof(((struct game_config *)0)->craft_waves), 0, 2},
	{"mission_time_limit", "game.mission_time_limit",
	 offsetof(struct game_config, mission_time_limit),
	 sizeof(((struct game_config *)0)->mission_time_limit), 0, 255},
	{"last_time_limit", "game.last_time_limit",
	 offsetof(struct game_config, last_team_time_limit_minutes),
	 sizeof(((struct game_config *)0)->last_team_time_limit_minutes), 0,
	 255},
	{"random_seed", "game.random_seed",
	 offsetof(struct game_config, random_seed),
	 sizeof(((struct game_config *)0)->random_seed), 0, 4294967295},
	{"password", "game.password", offsetof(struct game_config, password),
	 sizeof(((struct game_config *)0)->password), 1, 1},
	{"async_flag", "game.async_flag",
	 offsetof(struct game_config, internet_play),
	 sizeof(((struct game_config *)0)->internet_play), 0, 1},
	{"ai_opponents", "game.ai_opponents",
	 offsetof(struct game_config, ai_opponents),
	 sizeof(((struct game_config *)0)->ai_opponents), 0, 1},
	{"help_on", "game.help_on", offsetof(struct game_config, help_on),
	 sizeof(((struct game_config *)0)->help_on), 0, 1},
	{"combat_balance", "game.combat_balance",
	 offsetof(struct game_config, combat_balance),
	 sizeof(((struct game_config *)0)->combat_balance), 0, 3},
	{"taunt1", "game.taunt1", offsetof(struct game_config, taunts[0]),
	 sizeof(((struct game_config *)0)->taunts[0]), 1, 1},
	{"taunt2", "game.taunt2", offsetof(struct game_config, taunts[1]),
	 sizeof(((struct game_config *)0)->taunts[1]), 1, 1},
	{"taunt3", "game.taunt3", offsetof(struct game_config, taunts[2]),
	 sizeof(((struct game_config *)0)->taunts[2]), 1, 1},
	{"taunt4", "game.taunt4", offsetof(struct game_config, taunts[3]),
	 sizeof(((struct game_config *)0)->taunts[3]), 1, 1},
	{"continue_sequence", "game.continue_sequence",
	 offsetof(struct game_config, continue_battle_or_campaign),
	 sizeof(((struct game_config *)0)->continue_battle_or_campaign), 0, 1},
};
static AeronConfigFile *g_defaults;
static AeronConfigFile *g_user;
static AeronConfigFile *g_resolved;
static AeronVfs *g_config_vfs;
static struct xvt_settings g_settings;
static struct xvt_settings g_default_settings;
static struct xvt_scene_settings g_scene_defaults;
static uint64_t g_generation;
static int g_writable;

const AeronConfigFile *xvt_config_user_document(void) { return g_user; }

const AeronConfigFile *xvt_config_resolved_document(void) { return g_resolved; }

const struct xvt_settings *xvt_config_settings(void)
{
	return g_resolved ? &g_settings : NULL;
}

const struct xvt_settings *xvt_config_default_settings(void)
{
	return g_defaults ? &g_default_settings : NULL;
}

uint64_t xvt_config_generation(void) { return g_generation; }

int xvt_config_can_reset_to_defaults(void) { return g_defaults != NULL; }

void xvt_config_shutdown(void)
{
	AeronConfigFile_Destroy(g_resolved);
	AeronConfigFile_Destroy(g_user);
	AeronConfigFile_Destroy(g_defaults);
	g_defaults = NULL;
	g_user = NULL;
	g_resolved = NULL;
	g_config_vfs = NULL;
	g_writable = 0;
}

static int xvt_config_error(char *error, size_t capacity, const char *message,
			    const char *path)
{
	snprintf(error, capacity, "%s: %s", path ? path : "USER/config.yaml",
		 message);
	return 0;
}

enum { XVT_CONFIG_VERSION = 3 };

static int xvt_config_check_version(const AeronConfigFile *document,
				    int minimum, char *error, size_t capacity)
{
	const AeronConfigNode *node =
		AeronConfigFile_GetNode(document, "version");
	if (AeronConfigNode_Type(AeronConfigFile_Root(document)) !=
	    AERON_CONFIG_MAP) {
		return xvt_settings_node_error(
			document, "",
			"The settings file has an invalid format.", error,
			capacity);
	}
	if (AeronConfigNode_Type(node) != AERON_CONFIG_INT ||
	    (AeronConfigNode_Int(node, -1) < minimum ||
	     AeronConfigNode_Int(node, -1) > XVT_CONFIG_VERSION)) {
		return xvt_settings_node_error(
			document, "version",
			"This settings file uses an unsupported format.", error,
			capacity);
	}
	return 1;
}

static int xvt_config_apply_document(const AeronConfigFile *document,
				     struct game_config *game, char *error,
				     size_t capacity)
{
	struct game_config candidate = *game;
	for (size_t i = 0; i < sizeof(g_fields) / sizeof(g_fields[0]); ++i) {
		const struct xvt_config_field *field = &g_fields[i];
		const AeronConfigNode *node =
			AeronConfigFile_GetNode(document, field->path);
		uint8_t *destination = (uint8_t *)&candidate + field->offset;
		if (field->string) {
			const char *value = AeronConfigNode_String(node, NULL);
			if (!value || strlen(value) >= field->size) {
				return xvt_settings_node_error(
					document, field->path,
					"required text is missing, invalid or too long",
					error, capacity);
			}
			memset(destination, 0, field->size);
			memcpy(destination, value, strlen(value));
		} else {
			int64_t value = AeronConfigNode_Int(node, -1);
			if (AeronConfigNode_Type(node) != AERON_CONFIG_INT ||
			    value < 0 || value > field->maximum) {
				return xvt_settings_node_error(
					document, field->path,
					"expected an integer in the supported range",
					error, capacity);
			}
			if (field->size == 1) {
				*destination = (uint8_t)value;
			} else {
				uint32_t narrowed = (uint32_t)value;
				memcpy(destination, &narrowed,
				       sizeof(narrowed));
			}
		}
	}
	for (int i = 0; i < 2; ++i) {
		if (candidate.window_size[i] > candidate.screen_res[i]) {
			candidate.window_size[i] = candidate.screen_res[i];
		}
	}
	candidate.network_type = NET_TRANSPORT_TCPIP;
	memset(candidate.ip_address, 0, sizeof(candidate.ip_address));
	*game = candidate;
	return 1;
}

static int xvt_config_validate_and_parse(const AeronConfigFile *document,
					 struct xvt_settings *settings,
					 char *error, size_t capacity)
{
	struct game_config game = {0};
	return xvt_config_check_version(document, XVT_CONFIG_VERSION, error,
					capacity) &&
	       xvt_config_apply_document(document, &game, error, capacity) &&
	       xvt_settings_parse(document, &g_scene_defaults, settings, error,
				  capacity);
}

int xvt_config_apply(struct game_config *game, char *error, size_t capacity)
{
	if (!g_resolved || !game) {
		return xvt_config_error(error, capacity,
					"configuration is not loaded", NULL);
	}
	return xvt_config_apply_document(g_resolved, game, error, capacity);
}

static int xvt_config_remove_obsolete_keys(AeronConfigFile *document,
					   AeronConfigError *detail)
{
	const char *obsolete[] = {"network.port",     "network.join_address",
				  "game.ipaddress",   "game.networktype",
				  "game.phonenumber", "input.mouse_mode"};
	for (size_t i = 0; i < sizeof(obsolete) / sizeof(obsolete[0]); ++i) {
		if (AeronConfigFile_Has(document, obsolete[i]) &&
		    !AeronConfigFile_Remove(document, obsolete[i], detail)) {
			return 0;
		}
	}
	return 1;
}

static int xvt_config_upgrade_bindings(AeronConfigFile *document,
				       AeronConfigError *detail)
{
	int version = (int)AeronConfigFile_GetInt(document, "version", 0);
	if (version == 1 && AeronConfigFile_Has(document, "input.keyboard") &&
	    !AeronConfigFile_Remove(document, "input.keyboard", detail)) {
		return 0;
	}
	if (version < 3) {
		const char *paths[] = {"input.device", "input.gamepad",
				       "input.joystick", "input.controller"};
		for (size_t i = 0; i < sizeof paths / sizeof paths[0]; ++i) {
			if (AeronConfigFile_Has(document, paths[i]) &&
			    !AeronConfigFile_Remove(document, paths[i],
						    detail)) {
				return 0;
			}
		}
		for (int button = 1; button <= 20; ++button) {
			char path[32];
			snprintf(path, sizeof path, "game.joybutton%d", button);
			if (AeronConfigFile_Has(document, path) &&
			    !AeronConfigFile_Remove(document, path, detail)) {
				return 0;
			}
		}
		AeronConfigValue empty = {.type = AERON_CONFIG_SEQUENCE};
		if (!AeronConfigFile_SetValue(document, "input.controllers",
					      &empty, detail)) {
			return 0;
		}
	}
	if (AeronConfigFile_Has(document, "input.gamepad_defaults")) {
		XVT_LOG_WARN(
			"config.override_ignored key=input.gamepad_defaults");
		if (!AeronConfigFile_Remove(document, "input.gamepad_defaults",
					    detail)) {
			return 0;
		}
	}
	return AeronConfigFile_SetInt(document, "version", XVT_CONFIG_VERSION,
				      detail);
}

int xvt_config_update_user(const AeronConfigFile *candidate, int save,
			   char *error, size_t capacity)
{
	AeronConfigFile *user_root = NULL;
	AeronConfigFile *updated = NULL;
	AeronConfigFile *resolved = NULL;
	int success = 0;
	if (!g_defaults || !g_config_vfs) {
		return xvt_config_error(error, capacity,
					"valid shipped defaults are required",
					"RESOURCE/config.yaml");
	}
	if (!xvt_config_check_version(candidate, 1, error, capacity)) {
		return 0;
	}
	AeronConfigError detail;
	/* The destination always belongs to USER, independently of node provenance. */
	if (!AeronConfigFile_CreateMap(AERON_VFS_ROOT_USER, "config.yaml",
				       &user_root, &detail) ||
	    !AeronConfigFile_Overlay(user_root, candidate, &updated, &detail) ||
	    !xvt_config_remove_obsolete_keys(updated, &detail) ||
	    !xvt_config_upgrade_bindings(updated, &detail) ||
	    !AeronConfigFile_Overlay(g_defaults, updated, &resolved, &detail)) {
		xvt_settings_file_error(&detail, error, capacity);
		goto done;
	}
	struct xvt_settings settings;
	if (!xvt_keyboard_config_resolve(&g_default_settings.keyboard, updated,
					 resolved, error, capacity) ||
	    !xvt_config_validate_and_parse(resolved, &settings, error,
					   capacity)) {
		goto done;
	}
	if (save && !AeronConfigFile_SaveYaml(g_config_vfs, updated, &detail)) {
		xvt_settings_file_error(&detail, error, capacity);
		goto done;
	}
	AeronConfigFile_Destroy(g_user);
	AeronConfigFile_Destroy(g_resolved);
	g_user = updated;
	g_resolved = resolved;
	updated = NULL;
	resolved = NULL;
	g_settings = settings;
	g_writable = 1;
	++g_generation;
	success = 1;
done:
	AeronConfigFile_Destroy(user_root);
	AeronConfigFile_Destroy(updated);
	AeronConfigFile_Destroy(resolved);
	return success;
}

int xvt_config_reset_to_defaults(char *error, size_t capacity)
{
	if (!g_defaults) {
		return xvt_config_error(
			error, capacity,
			"cannot reset without valid shipped defaults",
			"RESOURCE/config.yaml");
	}
	AeronConfigFile *replacement = NULL;
	AeronConfigError detail;
	if (!AeronConfigFile_CreateMap(AERON_VFS_ROOT_USER, "config.yaml",
				       &replacement, &detail) ||
	    !AeronConfigFile_SetInt(replacement, "version", XVT_CONFIG_VERSION,
				    &detail)) {
		AeronConfigFile_Destroy(replacement);
		return xvt_settings_file_error(&detail, error, capacity);
	}
	int success = xvt_config_update_user(replacement, 0, error, capacity);
	AeronConfigFile_Destroy(replacement);
	return success;
}

int xvt_config_load(AeronVfs *vfs, char *error, size_t capacity)
{
	xvt_config_shutdown();
	xvt_keyboard_mapping_set_policy(Aeron_DebugUiAvailable() != 0);
	g_config_vfs = vfs;
	AeronConfigError detail;
	AeronConfigFile *scene_defaults = NULL;
	if (!AeronConfigFile_LoadYamlEx(vfs, AERON_VFS_ROOT_RESOURCE,
					"aeron/scene3d_defaults.yaml",
					&scene_defaults, &detail)) {
		return xvt_settings_file_error(&detail, error, capacity);
	}
	int success = AeronSceneSettings_Load(
		AeronConfigFile_Root(scene_defaults), &g_scene_defaults.ssao,
		&g_scene_defaults.shadows, &g_scene_defaults.tonemap, &detail);
	AeronConfigFile_Destroy(scene_defaults);
	if (!success) {
		return xvt_settings_file_error(&detail, error, capacity);
	}
	if (!AeronConfigFile_LoadYamlEx(vfs, AERON_VFS_ROOT_RESOURCE,
					"config.yaml", &g_defaults, &detail)) {
		return xvt_settings_file_error(&detail, error, capacity);
	}
	if (!xvt_config_validate_and_parse(g_defaults, &g_default_settings,
					   error, capacity)) {
		AeronConfigFile_Destroy(g_defaults);
		g_defaults = NULL;
		return 0;
	}
	if (xvt_storage_probe(AERON_VFS_ROOT_USER, "config.yaml") < 0) {
		return xvt_config_error(
			error, capacity,
			"cannot inspect configuration; file preserved",
			"USER/config.yaml");
	}
	AeronConfigFile *user = NULL;
	if (!AeronConfigFile_LoadYamlEx(vfs, AERON_VFS_ROOT_USER, "config.yaml",
					&user, &detail)) {
		if (detail.code == AERON_CONFIG_ERROR_NOT_FOUND) {
			return xvt_config_reset_to_defaults(error, capacity);
		}
		return xvt_settings_file_error(&detail, error, capacity);
	}
	success = xvt_config_update_user(user, 0, error, capacity);
	AeronConfigFile_Destroy(user);
	return success;
}

int xvt_config_save(char *error, size_t capacity)
{
	if (!g_writable) {
		return xvt_config_error(error, capacity,
					"configuration saving is disabled",
					NULL);
	}
	return xvt_config_update_user(g_user, 1, error, capacity);
}

int xvt_config_set_game_data(const char *path, int save, char *error,
			     size_t capacity)
{
	if (!g_writable) {
		return xvt_config_error(error, capacity,
					"configuration is not loaded", NULL);
	}
	AeronConfigFile *updated = NULL;
	AeronConfigError detail;
	if (!AeronConfigFile_Clone(g_user, &updated, &detail) ||
	    !AeronConfigFile_SetString(updated, "paths.game_data", path,
				       &detail)) {
		AeronConfigFile_Destroy(updated);
		return xvt_settings_file_error(&detail, error, capacity);
	}
	int success = xvt_config_update_user(updated, save, error, capacity);
	AeronConfigFile_Destroy(updated);
	return success;
}

bool xvt_config_set_flight_rate(bool unlocked, char *error, size_t capacity)
{
	if (!g_writable) {
		return xvt_config_error(error, capacity,
					"configuration saving is disabled",
					NULL);
	}
	AeronConfigFile *updated = NULL;
	AeronConfigError detail;
	if (!AeronConfigFile_Clone(g_user, &updated, &detail)) {
		return xvt_settings_file_error(&detail, error, capacity);
	}
	bool success =
		unlocked == (xvt_config_default_settings()->flight_unlocked !=
			     0)
			? AeronConfigFile_Remove(updated, "flight.update_rate",
						 &detail)
			: AeronConfigFile_SetString(
				  updated, "flight.update_rate",
				  unlocked ? "unlocked" : "native", &detail);
	if (success) {
		success = xvt_config_update_user(updated, 0, error, capacity);
	} else {
		xvt_settings_file_error(&detail, error, capacity);
	}
	AeronConfigFile_Destroy(updated);
	return success;
}

bool xvt_config_set_skip_intro(bool enabled, char *error, size_t capacity)
{
	if (!g_writable) {
		return xvt_config_error(error, capacity,
					"configuration saving is disabled",
					NULL);
	}
	AeronConfigFile *updated = NULL;
	AeronConfigError detail;
	if (!AeronConfigFile_Clone(g_user, &updated, &detail)) {
		return xvt_settings_file_error(&detail, error, capacity);
	}
	bool success =
		enabled == (xvt_config_default_settings()->skip_intro != 0)
			? AeronConfigFile_Remove(updated, "startup.skip_intro",
						 &detail)
			: AeronConfigFile_SetBool(updated, "startup.skip_intro",
						  enabled, &detail);
	if (success) {
		success = xvt_config_update_user(updated, 0, error, capacity);
	} else {
		xvt_settings_file_error(&detail, error, capacity);
	}
	AeronConfigFile_Destroy(updated);
	return success;
}

int xvt_config_write(const struct game_config *game, char *error,
		     size_t capacity)
{
	if (!g_writable || !game) {
		return xvt_config_error(error, capacity,
					"configuration saving is disabled",
					NULL);
	}
	AeronConfigFile *updated = NULL;
	AeronConfigError detail;
	if (!AeronConfigFile_Clone(g_user, &updated, &detail)) {
		return xvt_settings_file_error(&detail, error, capacity);
	}
	for (size_t i = 0; i < sizeof(g_fields) / sizeof(g_fields[0]); ++i) {
		const struct xvt_config_field *field = &g_fields[i];
		const uint8_t *source = (const uint8_t *)game + field->offset;
		int same;
		int success;
		if (field->string) {
			if (!memchr(source, 0, field->size)) {
				AeronConfigFile_Destroy(updated);
				return xvt_config_error(
					error, capacity,
					"unterminated game option",
					field->path);
			}
			same = !strcmp((const char *)source,
				       AeronConfigFile_GetString(
					       g_defaults, field->path, ""));
			success =
				same || AeronConfigFile_SetString(
						updated, field->path,
						(const char *)source, &detail);
		} else {
			uint32_t value = *source;
			if (field->size == 4) {
				memcpy(&value, source, sizeof(value));
			}
			if (value > field->maximum) {
				AeronConfigFile_Destroy(updated);
				return xvt_config_error(
					error, capacity,
					"game option outside supported range",
					field->path);
			}
			same = value == AeronConfigFile_GetInt(g_defaults,
							       field->path, -1);
			success = same ||
				  AeronConfigFile_SetInt(updated, field->path,
							 value, &detail);
		}
		if (success && same &&
		    AeronConfigFile_Has(updated, field->path)) {
			success = AeronConfigFile_Remove(updated, field->path,
							 &detail);
		}
		if (!success) {
			AeronConfigFile_Destroy(updated);
			return xvt_settings_file_error(&detail, error,
						       capacity);
		}
	}
	int committed = xvt_config_update_user(updated, 1, error, capacity);
	AeronConfigFile_Destroy(updated);
	return committed;
}

int xvt_config_import(const char *path, char *error, size_t capacity)
{
	const char *base = strrchr(path, '/');
	int is_config_cfg = strcmp(base ? base + 1 : path, "config.cfg") == 0;
	if (!g_writable) {
		return xvt_config_error(error, capacity,
					"Configuration saving is disabled",
					"config.yaml");
	}
	AeronFile *file =
		xvt_storage_open_root(AERON_VFS_ROOT_ASSET, path, "r");
	if (!file) {
		return xvt_config_error(error, capacity,
					"Cannot open legacy configuration",
					path);
	}
	AeronConfigFile *imported = NULL;
	AeronConfigError detail;
	if (!AeronConfigFile_Clone(g_user, &imported, &detail)) {
		AeronVfs_Close(file);
		return xvt_config_error(error, capacity, detail.message, path);
	}
	char line[512];
	int success = 1;
	int recognized = 0;
	while (success && xvt_file_gets(line, sizeof(line), file)) {
		if (!strchr(line, '\n') && strlen(line) == sizeof(line) - 1) {
			success = 0;
			break;
		}
		char *value = line;
		while (*value && !isspace((unsigned char)*value)) {
			++value;
		}
		if (!*value) {
			continue;
		}
		*value++ = 0;
		while (*value == ' ' || *value == '\t') {
			++value;
		}
		value[strcspn(value, "\r\n")] = 0;
		for (size_t i = 0; i < sizeof(g_fields) / sizeof(g_fields[0]);
		     ++i) {
			const struct xvt_config_field *field = &g_fields[i];
			if (strcmp(line, field->legacy)) {
				continue;
			}
			++recognized;
			if (field->string) {
				success = strlen(value) < field->size &&
					  AeronConfigFile_SetString(
						  imported, field->path, value,
						  &detail);
			} else {
				errno = 0;
				char *end;
				int64_t number = strtoll(value, &end, 10);
				while (isspace((unsigned char)*end)) {
					++end;
				}
				success = !errno && end != value && !*end;
				if (is_config_cfg &&
				    strncmp(line, "joybutton", 9) == 0 &&
				    number >= 124 && number <= 229) {
					number += 4;
				}
				success = success && number >= 0 &&
					  number <= field->maximum &&
					  AeronConfigFile_SetInt(
						  imported, field->path, number,
						  &detail);
			}
			break;
		}
	}
	if (!recognized || AeronVfs_HasError(file)) {
		success = 0;
	}
	if (!AeronVfs_Close(file)) {
		success = 0;
	}
	if (!success) {
		AeronConfigFile_Destroy(imported);
		return xvt_config_error(
			error, capacity,
			"Invalid legacy option or import I/O failure; existing YAML preserved",
			path);
	}
	int committed = xvt_config_update_user(imported, 1, error, capacity);
	AeronConfigFile_Destroy(imported);
	return committed;
}

bool xvt_config_set_keyboard(const struct xvt_keyboard_bindings *bindings,
			     char *error, size_t capacity)
{
	if (!g_writable) {
		return xvt_config_error(error, capacity,
					"configuration is not loaded", NULL);
	}
	AeronConfigFile *candidate = NULL;
	AeronConfigError detail = {0};
	if (!AeronConfigFile_Clone(g_user, &candidate, &detail) ||
	    !xvt_keyboard_config_write(candidate, bindings, &detail)) {
		AeronConfigFile_Destroy(candidate);
		return xvt_settings_file_error(&detail, error, capacity);
	}
	bool ok = xvt_config_update_user(candidate, 0, error, capacity) != 0;
	AeronConfigFile_Destroy(candidate);
	return ok;
}

bool xvt_config_restore_keyboard(char *error, size_t capacity)
{
	if (!g_writable) {
		return xvt_config_error(error, capacity,
					"configuration is not loaded", NULL);
	}
	AeronConfigFile *candidate = NULL;
	AeronConfigError detail = {0};
	if (!AeronConfigFile_Clone(g_user, &candidate, &detail) ||
	    !AeronConfigFile_Remove(candidate, "input.keyboard", &detail)) {
		AeronConfigFile_Destroy(candidate);
		return xvt_settings_file_error(&detail, error, capacity);
	}
	bool ok = xvt_config_update_user(candidate, 0, error, capacity) != 0;
	AeronConfigFile_Destroy(candidate);
	return ok;
}
