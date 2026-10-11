#include "xvt_runtime/runtime/launch_record.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "xvt/flight/player/player.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/frontend_flight.h"
#include "xvt/frontend/frontend_mission.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt/net/frontend_net.h"
#include "xvt_runtime/log/log.h"

enum { RECORD_PATH_CAPACITY = 512, RECORD_FILE_SUFFIX_CAPACITY = 8 };

static char g_record_path[RECORD_PATH_CAPACITY];

void xvt_launch_record_set_path(const char *path)
{
	g_record_path[0] = '\0';
	if (path && strlen(path) < sizeof(g_record_path)) {
		strcpy(g_record_path, path);
	}
	XVT_LOG_DEBUG("launch.record_path path=\"%s\"", g_record_path);
}

int xvt_launch_record_is_enabled(void) { return g_record_path[0] != '\0'; }

/* Writes one line: the name, the byte count, and the bytes in hex. */
static void record_bytes(FILE *file, const char *name, const void *data,
			 size_t size)
{
	const unsigned char *bytes = data;
	fprintf(file, "%s %zu ", name, size);
	for (size_t index = 0; index < size; ++index) {
		fprintf(file, "%02x", bytes[index]);
	}
	fputc('\n', file);
}

/* Writes one line for a whole int value. */
static void record_int(FILE *file, const char *name, int value)
{
	record_bytes(file, name, &value, sizeof(value));
}

#define RECORD_MEMBER(file, prefix, object, member)                            \
	record_bytes(file, prefix "." #member, &(object).member,               \
		     sizeof((object).member))

/* Writes the members of the game settings that hold video, network and name
 * choices, one line each. */
static void record_video_and_network_settings(FILE *file)
{
	RECORD_MEMBER(file, "game_config", g_game_config, backdrop);
	RECORD_MEMBER(file, "game_config", g_game_config, star_density);
	RECORD_MEMBER(file, "game_config", g_game_config, debris);
	RECORD_MEMBER(file, "game_config", g_game_config, local_lights);
	RECORD_MEMBER(file, "game_config", g_game_config, specular);
	RECORD_MEMBER(file, "game_config", g_game_config, diffuse);
	RECORD_MEMBER(file, "game_config", g_game_config, dither);
	RECORD_MEMBER(file, "game_config", g_game_config, texture_res);
	RECORD_MEMBER(file, "game_config", g_game_config, mipmap);
	RECORD_MEMBER(file, "game_config", g_game_config, lod);
	RECORD_MEMBER(file, "game_config", g_game_config, screen_res);
	RECORD_MEMBER(file, "game_config", g_game_config, window_size);
	RECORD_MEMBER(file, "game_config", g_game_config, color_depth_choice);
	RECORD_MEMBER(file, "game_config", g_game_config, brightness);
	RECORD_MEMBER(file, "game_config", g_game_config, use3d_hardware);
	RECORD_MEMBER(file, "game_config", g_game_config, bilinear);
	RECORD_MEMBER(file, "game_config", g_game_config, network_type);
	RECORD_MEMBER(file, "game_config", g_game_config, phone_number);
	RECORD_MEMBER(file, "game_config", g_game_config, ip_address);
	RECORD_MEMBER(file, "game_config", g_game_config, last_pilot_name);
	RECORD_MEMBER(file, "game_config", g_game_config,
		      reserved_after_last_pilot_name);
}

/* Writes the members of the game settings that hold sound, game and taunt
 * choices, one line each. */
static void record_sound_and_game_settings(FILE *file)
{
	RECORD_MEMBER(file, "game_config", g_game_config, password);
	RECORD_MEMBER(file, "game_config", g_game_config, internet_play);
	RECORD_MEMBER(file, "game_config", g_game_config, server_update_rate);
	RECORD_MEMBER(file, "game_config", g_game_config, sfx_exterior_enabled);
	RECORD_MEMBER(file, "game_config", g_game_config, sfx_interior_enabled);
	RECORD_MEMBER(file, "game_config", g_game_config, sfx_engine_enabled);
	RECORD_MEMBER(file, "game_config", g_game_config, voice_pilot_level);
	RECORD_MEMBER(file, "game_config", g_game_config,
		      voice_tactical_officer_level);
	RECORD_MEMBER(file, "game_config", g_game_config,
		      voice_commander_enabled);
	RECORD_MEMBER(file, "game_config", g_game_config,
		      voice_special_enabled);
	RECORD_MEMBER(file, "game_config", g_game_config, music_enabled);
	RECORD_MEMBER(file, "game_config", g_game_config, sfx_datapad_volume);
	RECORD_MEMBER(file, "game_config", g_game_config, sfx_exterior_volume);
	RECORD_MEMBER(file, "game_config", g_game_config, sfx_interior_volume);
	RECORD_MEMBER(file, "game_config", g_game_config, sfx_engine_volume);
	RECORD_MEMBER(file, "game_config", g_game_config, voice_volume);
	RECORD_MEMBER(file, "game_config", g_game_config, music_volume);
	RECORD_MEMBER(file, "game_config", g_game_config,
		      datapad_music_enabled);
	RECORD_MEMBER(file, "game_config", g_game_config, joy_buttons);
	RECORD_MEMBER(file, "game_config", g_game_config, difficulty);
	RECORD_MEMBER(file, "game_config", g_game_config, collisions);
	RECORD_MEMBER(file, "game_config", g_game_config, craft_jumping);
	RECORD_MEMBER(file, "game_config", g_game_config, random_setup);
	RECORD_MEMBER(file, "game_config", g_game_config, battle_length_index);
	RECORD_MEMBER(file, "game_config", g_game_config, require_password);
	RECORD_MEMBER(file, "game_config", g_game_config, in_progress_join);
	RECORD_MEMBER(file, "game_config", g_game_config, craft_selection);
	RECORD_MEMBER(file, "game_config", g_game_config, locate_players);
	RECORD_MEMBER(file, "game_config", g_game_config, craft_waves);
	RECORD_MEMBER(file, "game_config", g_game_config, mission_time_limit);
	RECORD_MEMBER(file, "game_config", g_game_config,
		      last_team_time_limit_minutes);
	RECORD_MEMBER(file, "game_config", g_game_config, random_seed);
	RECORD_MEMBER(file, "game_config", g_game_config, ai_opponents);
	RECORD_MEMBER(file, "game_config", g_game_config, combat_balance);
	RECORD_MEMBER(file, "game_config", g_game_config,
		      continue_battle_or_campaign);
	RECORD_MEMBER(file, "game_config", g_game_config, sfx_datapad_enabled);
	RECORD_MEMBER(file, "game_config", g_game_config, help_on);
	RECORD_MEMBER(file, "game_config", g_game_config, taunts);
}

/* Writes every member of the game settings, one line each. */
static void record_game_config(FILE *file)
{
	record_video_and_network_settings(file);
	record_sound_and_game_settings(file);
}

/* Writes every member of each of the eight roster entries and of the
 * eight players' flight state. */
static void record_players(FILE *file)
{
	char name[64];
	for (int index = 0; index < 8; ++index) {
		snprintf(name, sizeof(name), "roster[%d].", index);
		size_t length = strlen(name);
		const struct mp_roster_entry *entry = &g_mp_roster[index];
		snprintf(name + length, sizeof(name) - length, "name");
		record_bytes(file, name, entry->name, sizeof(entry->name));
		snprintf(name + length, sizeof(name) - length, "player_id");
		record_int(file, name, entry->player_id);
		snprintf(name + length, sizeof(name) - length, "pilot_rating");
		record_int(file, name, (int)entry->pilot_rating);
		snprintf(name + length, sizeof(name) - length,
			 "craft_type_override");
		record_int(file, name, entry->craft_type_override);
		snprintf(name + length, sizeof(name) - length,
			 "craft_option_index");
		record_int(file, name, entry->craft_option_index);
		snprintf(name + length, sizeof(name) - length,
			 "warhead_option_index");
		record_int(file, name, entry->warhead_option_index);
		snprintf(name + length, sizeof(name) - length,
			 "beam_option_index");
		record_int(file, name, entry->beam_option_index);
		snprintf(name + length, sizeof(name) - length,
			 "countermeasure_option_index");
		record_int(file, name, entry->countermeasure_option_index);
		snprintf(name, sizeof(name), "roster_ready[%d]", index);
		record_int(file, name, g_mp_roster_ready_flags[index]);
		snprintf(name, sizeof(name), "players[%d].participation_state",
			 index);
		record_bytes(file, name, &g_players[index].participation_state,
			     sizeof(g_players[index].participation_state));
	}
}

/* Writes the team assignment tables and the scalar state the screens leave. */
static void record_scalars(FILE *file, const char *command)
{
	record_bytes(file, "launch_line", command, strlen(command));
	record_int(file, "session_mode", (int)g_frontend_mission_session_mode);
	record_int(file, "is_host", g_mission_setup_is_host);
	record_int(file, "human_player_count",
		   g_frontend_launch_human_player_count);
	record_int(file, "local_player_index",
		   g_local_pilot_network_player_index);
	record_int(file, "roster_authoritative",
		   g_mission_setup_roster_authoritative);
	record_int(file, "use_combat_sim_pilot_state",
		   g_mission_setup_use_combat_sim_pilot_state);
	record_int(file, "team_count", g_team_count);
	record_bytes(
		file, "team_player_ids",
		g_mission_setup_player_assignments.team_player_ids,
		sizeof(g_mission_setup_player_assignments.team_player_ids));
	record_bytes(
		file, "assigned_player_ids",
		g_mission_setup_player_assignments.assigned_player_ids,
		sizeof(g_mission_setup_player_assignments.assigned_player_ids));
}

int xvt_launch_record_write(const char *command)
{
	if (!xvt_launch_record_is_enabled() || !command) {
		return 0;
	}
	char pilot_path[RECORD_PATH_CAPACITY + RECORD_FILE_SUFFIX_CAPACITY];
	snprintf(pilot_path, sizeof(pilot_path), "%s.pilot", g_record_path);
	FILE *pilot = fopen(pilot_path, "wb");
	FILE *state = fopen(g_record_path, "wb");
	int written =
		pilot && state &&
		fwrite(&g_pilot_data, sizeof(g_pilot_data), 1, pilot) == 1;
	if (written) {
		record_scalars(state, command);
		record_game_config(state);
		record_players(state);
	}
	if (pilot && fclose(pilot) != 0) {
		written = 0;
	}
	if (state && fclose(state) != 0) {
		written = 0;
	}
	if (!written) {
		XVT_LOG_ERROR("launch.record_failed path=\"%s\"",
			      g_record_path);
		return 0;
	}
	XVT_LOG_INFO("launch.recorded path=\"%s\" pilot_bytes=%zu",
		     g_record_path, sizeof(g_pilot_data));
	return 1;
}
