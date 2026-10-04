#include "xvt/audio/fsfx.h"
#ifdef XVT_MODERN
#include "xvt_runtime/timing/flight_timing.h"
#endif
#include <stdio.h>
#include <string.h>

#include "xvt/assets/file.h"
#include "xvt/audio/sound.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/fediskio.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/flight_loading.h"
#include "xvt/flight/hud/msg.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/collide.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/flight/transfm2.h"
#include "xvt/frontend/config.h"
#include "xvt/math/math.h"
#include "xvt/math/math2.h"
#include "xvt/math/trig2.h"
#include "xvt/util/game_rand.h"

/* Falloff distance, in world units, of flight sound ids 0 to 95, one entry per
 * id; fsfx_compute_source_volume reads entries 0 to 94 (ids from 95 use 8192).
 * Closer than it, the volume rises from base >> 2 toward the base; from it to
 * twice it the sound plays at base >> 2, from twice to four times it at
 * base >> 3, and from four times it not at all. Ids 0 to 3 hold 0. */
// GLOBAL: XVT 0x520F18
uint16_t g_fsfx_falloff_distance_by_sfx_slot[96] = {
	0,     0,     0,     0,	    8192,  8192,  8192,	 10240, 10240, 10240,
	10240, 10240, 12288, 12288, 12288, 8192,  10240, 10240, 8192,  8192,
	8192,  49152, 24576, 24576, 24576, 24576, 24576, 24576, 8192,  8192,
	8192,  8192,  8192,  8192,  8192,  8192,  8192,	 8192,	8192,  8192,
	8192,  8192,  8192,  8192,  8192,  8192,  8192,	 8192,	8192,  8192,
	8192,  8192,  8192,  8192,  8192,  8192,  8192,	 8192,	8192,  8192,
	8192,  8192,  8192,  8192,  8192,  8192,  8192,	 8192,	8192,  8192,
	8192,  8192,  8192,  8192,  8192,  8192,  8192,	 8192,	8192,  8192,
	8192,  8192,  8192,  8192,  8192,  8192,  8192,	 8192,	8192,  8192,
	8192,  8192,  8192,  8192,  8192,  8192,
};
/* Base volume, 0 to 127, of flight sound ids 0 to 95, one entry per id.
 * fsfx_compute_source_volume scales it by distance for ids under 95 (ids from 95
 * use 112) and by the interior volume setting for an interior sound of any
 * id. */
// GLOBAL: XVT 0x520FD8
uint8_t g_fsfx_base_volume_by_sfx_slot[96] = {
	0,   0,	  0,   0,   72,	 72,  96,  112, 80,  80,  80,  80,  96,	 96,
	96,  72,  80,  80,  111, 111, 127, 127, 127, 127, 127, 127, 127, 127,
	88,  127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 80,
	127, 95,  56,  56,  56,	 56,  56,  127, 127, 127, 72,  56,  88,	 72,
	56,  72,  56,  56,  56,	 56,  112, 112, 112, 112, 112, 112, 112, 112,
	112, 112, 112, 112, 112, 112, 112, 112, 127, 127, 112, 112, 112, 112,
	112, 112, 96,  96,  112, 112, 127, 127, 64,  127, 127, 112,
};
/* First line of each wingman voice category, 0 to 23, as an offset within a
 * pilot's 97-line voice list. */
// GLOBAL: XVT 0x521038
static const uint8_t g_fsfx_voice_category_base_offset[24] = {
	0x00, 0x06, 0x0E, 0x14, 0x18, 0x19, 0x1B, 0x1D, 0x1F, 0x20, 0x28, 0x2D,
	0x2E, 0x38, 0x39, 0x3A, 0x3C, 0x40, 0x46, 0x47, 0x4C, 0x52, 0x5A, 0x5C};
/* Number of lines in each wingman voice category, 0 to 23. */
// GLOBAL: XVT 0x521050
static const uint8_t g_fsfx_voice_category_variant_count[24] = {
	0x06, 0x08, 0x06, 0x04, 0x01, 0x02, 0x02, 0x01, 0x01, 0x08, 0x05, 0x01,
	0x05, 0x01, 0x01, 0x02, 0x04, 0x06, 0x01, 0x05, 0x06, 0x08, 0x02, 0x05};
/* Plays of one line, per wingman voice category, after which
 * fsfx_select_available_voice_variant passes it over; 0 never passes a line
 * over. */
// GLOBAL: XVT 0x521068
static const uint8_t g_fsfx_voice_category_repeat_threshold[24] = {
	0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x02, 0x02, 0x01,
	0x01, 0x02, 0x01, 0x01, 0x02, 0x00, 0x01, 0x01, 0x00, 0x02, 0x01, 0x01};
/* Tactical officer line, as an offset from voice slot 696, that names a flight
 * group, indexed by the group's designation code; 0xFF for codes with no
 * line. */
// GLOBAL: XVT 0x521080
static const uint8_t g_fsfx_designation_to_tactical_message_id[24] = {
	0xFF, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0D,
	0x0E, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00};
/* First line of each commander voice category, 0 to 9, as an offset from voice
 * slot 804. */
// GLOBAL: XVT 0x521098
static const uint8_t g_commander_voice_sfx_offset_by_category[10] = {
	0x00, 0x02, 0x0A, 0x0C, 0x0D, 0x0E, 0x12, 0x14, 0x1A, 0x1E};
/* Number of lines in each commander voice category, 0 to 9; a 0 count plays the
 * category's first line. */
// GLOBAL: XVT 0x5210A8
static const uint8_t g_commander_voice_variant_count_by_category[10] = {
	0x02, 0x08, 0x02, 0x00, 0x00, 0x04, 0x02, 0x06, 0x04, 0x04};
/* 1 once fsfx_load_sfx_list has passed a list line to sound_load_effect; only that
 * function writes it, and nothing sets it back to 0. The voice queue runs only
 * while it is set. */
// GLOBAL: XVT 0x9ECC52
uint8_t g_fsfx_loaded = 0;
/* Entries in the voice queue, the five g_fsfxVoiceQueue arrays, 0 to 128. Four
 * functions write it: fsfx_queue_voice_sfx adds one, fsfx_update_voice_queue takes
 * one off, fsfx_remove_voice_queue_entry_chain takes off a chain and
 * fsfx_reset_flight_sfx_state sets 0. */
// GLOBAL: XVT 0x9A20A4
static uint8_t g_fsfx_voice_queue_count = 0;
/* Speaker type of the line fsfx_update_voice_queue last took from the queue; only
 * it writes it, and nothing reads it. */
// GLOBAL: XVT 0xA0A870
static uint8_t g_fsfx_current_voice_speaker_type = 0;
/* Chain flag of each queued voice line: 0 starts a message, nonzero continues
 * the one before it, so the lines are pruned together. */
// GLOBAL: XVT 0xA0A880
static uint8_t g_fsfx_voice_queue_chain_flag[128] = {0};
/* Voice category of the line fsfx_update_voice_queue last took from the queue;
 * only it writes it, and nothing reads it. */
// GLOBAL: XVT 0xA0A900
static uint8_t g_fsfx_current_voice_category = 0;
/* Object signature each queued voice line is about, 0xFFFF for none;
 * fsfx_prune_stale_voice_queue_entries drops a tactical status message whose object
 * is gone. */
// GLOBAL: XVT 0xA0A910
static uint16_t g_fsfx_voice_queue_object_signature[128] = {0};
/* Mission time, in seconds, at which the tactical officer last reported each
 * craft slot, 0 for never; fsfx_speak_tactical_officer_event holds a report back
 * within 10 seconds of the last. fsfx_reset_flight_sfx_state clears the slots
 * below g_active_region_craft_object_slot_end. */
// GLOBAL: XVT 0xA0A7F0
static int g_fsfx_tac_officer_last_speak_seconds_by_obj[136] = {0};
/* Path fsfx_load_sfx_list builds for each effect it loads: the list line prefixed
 * with the wave folder. */
// GLOBAL: XVT 0xA0AB20
static char g_fsfx_sfx_load_path[720] = {0};
/* Path of the mission file being flown: "DEMO.TIE" until flight_main (original
 * build) or xvt_flight_entry_create_devices (modern) copies in the mission path
 * from the launch arguments. fe_disk_io_init_resources, flight_main_loop and
 * xvt_flight_loading_palette change its last three letters while they open the
 * mission's other files, then put them back. */
// GLOBAL: XVT 0x523448
char g_current_mission_file[128] = "DEMO.TIE";
/* Speaker type of each queued voice line: 0 special, 1 wingman pilot, 2
 * tactical officer, 3 commander. */
// GLOBAL: XVT 0xA0AA20
static uint8_t g_fsfx_voice_queue_speaker_type[128] = {0};
/* Voice category of each queued voice line. */
// GLOBAL: XVT 0xA0AAA0
static uint8_t g_fsfx_voice_queue_category[128] = {0};
/* Flight sound id of the voice line fsfx_update_voice_queue last started, 0 when
 * none; it starts no other line while this one plays. fsfx_reset_flight_sfx_state
 * sets 0. */
// GLOBAL: XVT 0xA0AA10
static int g_fsfx_current_voice_sfx_slot = 0;
/* Counted plays of each wingman voice line, in six lists of 97 entries indexed
 * by craft ordinal: fsfx_speak_wingman_event adds to it as it queues most lines,
 * and fsfx_select_available_voice_variant compares it with the repeat thresholds.
 * fsfx_reset_flight_sfx_state clears it. */
// GLOBAL: XVT 0xA0ABA0
static uint8_t g_fsfx_voice_line_play_counts[6 * 97] = {0};
/* Effect name of each flight sound id, the list line it was loaded from and the
 * name the Sound_ functions find the effect by; empty for an id never loaded.
 * fsfx_load_sfx_list fills it, fsfx_clear_sfx_name_table empties it. */
// GLOBAL: XVT 0xA0ADF0
char g_fsfx_sfx_name_table[838][24] = {{0}};
/* What sound_load_effect returned for each flight sound id: 1 when the effect
 * loaded, 0 when not. fsfx_load_sfx_list writes it and fsfx_reset_flight_sfx_state
 * clears it; fsfx_play_sound and the voice queue play nothing from an id holding
 * 0. */
// GLOBAL: XVT 0xA0FE80
static uint16_t g_fsfx_loaded_by_slot[838] = {0};
/* Flight sound id of each queued voice line. */
// GLOBAL: XVT 0xA0FC80
static int g_fsfx_voice_queue_sfx_slot[128] = {0};
/* Object signature of the line fsfx_update_voice_queue last took from the queue;
 * only it writes it, and nothing reads it. */
// GLOBAL: XVT 0xA1050C
static uint16_t g_fsfx_current_voice_object_signature = 0;
/* Chain flag of the line fsfx_update_voice_queue last took from the queue; only
 * it writes it, and nothing reads it. */
// GLOBAL: XVT 0xA1050E
static uint8_t g_fsfx_current_voice_chain_flag = 0;
/* Object type of the local player's craft when fsfx_update_player_engine_loop last
 * found one with an engine sound; it uses it to stop that sound once the craft
 * is gone. Only that function writes it, and nothing resets it. */
// GLOBAL: XVT 0x556350
uint8_t g_player_engine_loop_object_type = 0;

/* Empties every name in g_fsfx_sfx_name_table. Returns 0. flight_main_loop calls it
 * in the original build, xvt_flight_loading_globals in the modern one. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x42DE40
int fsfx_clear_sfx_name_table(void)
{
	memset(g_fsfx_sfx_name_table, 0, sizeof(g_fsfx_sfx_name_table));
	return 0;
}

/* Calls sound_unload_all_effects. fe_disk_io_free_flight_resources is its only
 * caller. */
// FUNCTION: XVT 0x42DE70
void fsfx_unload_all_effects_thunk(void) { sound_unload_all_effects(); }

/* Clears the flight sound state for a new flight: g_fsfx_loaded_by_slot,
 * g_fsfx_voice_line_play_counts, the entries of
 * g_fsfx_tac_officer_last_speak_seconds_by_obj below g_active_region_craft_object_slot_end,
 * g_fsfx_voice_queue_count and g_fsfx_current_voice_sfx_slot. Leaves g_fsfx_loaded.
 * fe_disk_io_init_global_buffers is its only caller. */
// FUNCTION: XVT 0x42DE80
void fsfx_reset_flight_sfx_state(void)
{
	memset(g_fsfx_loaded_by_slot, 0, sizeof(g_fsfx_loaded_by_slot));
	for (unsigned int voice_offset = 0;
	     voice_offset < sizeof(g_fsfx_voice_line_play_counts);
	     voice_offset += 97) {
		memset(&g_fsfx_voice_line_play_counts[voice_offset], 0, 97);
	}
	for (unsigned int object_index = 0;
	     object_index < (unsigned int)g_active_region_craft_object_slot_end;
	     ++object_index) {
		g_fsfx_tac_officer_last_speak_seconds_by_obj[object_index] = 0;
	}
	g_fsfx_voice_queue_count = 0;
	g_fsfx_current_voice_sfx_slot = 0;
}

/* Loads the effects a list file names, one per line, into consecutive flight
 * sound ids from first_sound_id. Each line that is not empty, cut at its first CR
 * or LF, becomes the id's name in g_fsfx_sfx_name_table, and the file it names in
 * the wave folder, its path built in g_fsfx_sfx_load_path, loads with
 * sound_load_effect, whose result goes in g_fsfx_loaded_by_slot; after each load it
 * calls flight_loading_pulse_and_draw_progress_screen and sets g_fsfx_loaded to 1.
 * With g_flight_conf_voice_enabled 0, ids over 0x5F are skipped without loading.
 * Returns the number of lines it passed to sound_load_effect, or 0 when the list
 * does not open. Does not check that the ids stay under 838 or that a line fits
 * the 24-byte name. */
// FUNCTION: XVT 0x42DED0
int fsfx_load_sfx_list(const char *file_name_buffer, uint16_t first_sound_id)
{
	if (fe_disk_io_open_global_stream(file_name_buffer, "rb", 0, 0) == 0) {
		return 0;
	}
	xvt_file *stream = (xvt_file *)g_stream;
	char buffer[256];
	uint16_t loaded_count = 0;
	while (FILE_GETS(buffer, sizeof(buffer), stream) != NULL) {
		char *line_end = buffer;
		while (*line_end != '\0' && *line_end != '\r' &&
		       *line_end != '\n') {
			++line_end;
		}
		*line_end = '\0';
		if (buffer[0] == '\0') {
			continue;
		}
		if (g_flight_conf_voice_enabled == 0 &&
		    first_sound_id > 0x5fu) {
			++first_sound_id;
			continue;
		}

		strcpy(g_fsfx_sfx_load_path, "wave\\");
		strcat(g_fsfx_sfx_load_path, buffer);
		strcpy(g_fsfx_sfx_name_table[first_sound_id], buffer);
		g_fsfx_loaded_by_slot[first_sound_id] =
			(uint16_t)sound_load_effect(
				g_fsfx_sfx_load_path,
				g_fsfx_sfx_name_table[first_sound_id]);
		++first_sound_id;
		flight_loading_pulse_and_draw_progress_screen();
		g_fsfx_loaded = 1;
		++loaded_count;
	}
	fe_disk_io_close_global_stream(0);
	return loaded_count;
}

/* Loads the mission's voice lists from the wave folder with fsfx_load_sfx_list;
 * does nothing when g_flight_conf_voice_enabled or the voice volume is 0. With the
 * tactical officer on it loads RTO1.LST or RTO2.LST for a player on IFF 0, else
 * ITO1.LST or ITO2.LST, chosen by game_rand2() & 1, at id 0x2B8 (696). With the
 * commander on it loads RCMD.LST for IFF 0, ICMD.LST for IFF 1, else PCMD.LST,
 * at 0x324 (804). With wingman voices on, when the player's flight group (the
 * first whose player_owner_idx is g_local_player) has more than one craft or more
 * than one group has a player_number, it loads one pilot list per craft of that
 * group, RSP1.LST to RSP6.LST (ISP for IFF 1) starting at a random one and
 * cycling, at ids 114, 211 and on, 97 apart, with no limit on the count. With
 * special voices on it loads the mission file's name, less anything up to and
 * including its first backslash, with its last three letters changed to "lst",
 * at 0x5F. Does not check that the mission name fits its 64-byte buffers; the
 * modern build changes the letters only for a name of 3 or more characters. */
// FUNCTION: XVT 0x42E070
void fsfx_load_mission_voice_sfx(void)
{
	char list_name[64];
	char mission_file_name[64];
	char path[64];
	int number_of_craft;
	unsigned int player_group;
	int player_flown_group_count;
	uint16_t variant;
	int first_sound_id;
	int mission_file_name_length;
	int mission_file_name_start;
	int path_length;

	if (g_flight_conf_voice_enabled == 0 ||
	    g_game_config.voice_volume == 0) {
		return;
	}
	if (g_game_config.voice_tactical_officer_level != 0) {
		strcpy(path, "wave\\");
		if (g_players[g_local_player].iff == 0) {
			if ((game_rand2() & 1) != 0) {
				strcat(path, "RTO1.LST");
			} else {
				strcat(path, "RTO2.LST");
			}
		} else {
			if ((game_rand2() & 1) != 0) {
				strcat(path, "ITO1.LST");
			} else {
				strcat(path, "ITO2.LST");
			}
		}
		fsfx_load_sfx_list(path, 0x2b8u);
	}
	if (g_game_config.voice_commander_enabled != 0) {
		strcpy(path, "wave\\");
		if (g_players[g_local_player].iff == 0) {
			strcat(path, "RCMD.LST");
		} else if (g_players[g_local_player].iff == 1) {
			strcat(path, "ICMD.LST");
		} else {
			strcat(path, "PCMD.LST");
		}
		fsfx_load_sfx_list(path, 0x324u);
	}
	if (g_game_config.voice_pilot_level != 0) {
		player_flown_group_count = 0;
		for (player_group = 0;
		     (int)player_group < g_mission_header.num_flight_groups;
		     ++player_group) {
			if (g_mission_flight_groups[player_group]
				    .fg.player_number != 0) {
				++player_flown_group_count;
			}
		}
		for (player_group = 0;
		     (int)player_group < g_mission_header.num_flight_groups;
		     ++player_group) {
			if (g_mission_flight_groups[player_group]
				    .player_owner_idx == g_local_player) {
				break;
			}
		}
		if ((int)player_group < g_mission_header.num_flight_groups &&
		    (g_mission_flight_groups[player_group].fg.number_of_craft >
			     1 ||
		     player_flown_group_count > 1)) {
			number_of_craft = g_mission_flight_groups[player_group]
						  .fg.number_of_craft;
			variant = fsfx_random_index(6);
			if (number_of_craft-- != 0) {
				first_sound_id = 114;
				do {
					strcpy(path, "wave\\");
					if (g_players[g_local_player].iff ==
					    1) {
						strcpy(list_name, "ISP1.LST");
					} else {
						strcpy(list_name, "RSP1.LST");
					}
					list_name[3] = (char)('1' + variant);
					strcat(path, list_name);
					fsfx_load_sfx_list(
						path, (uint16_t)first_sound_id);
					++variant;
					first_sound_id += 97;
					if (variant >= 6) {
						variant = 0;
					}
				} while (number_of_craft-- != 0);
			}
		}
	}
	if (g_game_config.voice_special_enabled != 0) {
		strcpy(path, "wave\\");
		strcpy(mission_file_name, g_current_mission_file);
		mission_file_name_length = strlen(mission_file_name);
#ifdef XVT_MODERN
		if (mission_file_name_length >= 3) {
#endif
			mission_file_name[mission_file_name_length - 3] = 'l';
			mission_file_name[mission_file_name_length - 2] = 's';
			mission_file_name[mission_file_name_length - 1] = 't';
#ifdef XVT_MODERN
		}
#endif
		mission_file_name_start = 0;
		while (mission_file_name[mission_file_name_start] != '\\' &&
		       mission_file_name_start < mission_file_name_length) {
			++mission_file_name_start;
		}
		if (mission_file_name_start < mission_file_name_length) {
			++mission_file_name_start;
		} else {
			mission_file_name_start = 0;
		}
		path_length = strlen(path);
		if (mission_file_name_start < mission_file_name_length) {
			mission_file_name_length -= mission_file_name_start;
			memcpy(&path[path_length],
			       &mission_file_name[mission_file_name_start],
			       mission_file_name_length);
			path_length += mission_file_name_length;
		}
		path[path_length] = '\0';
		fsfx_load_sfx_list(path, 0x5fu);
	}
}

/* Stops the hyperspace exit sounds, ids 81 and 84, when they play. Does nothing
 * when g_flight_sim_side_effects_suppressed is set, player_idx is not g_local_player,
 * g_flight_conf_sfx_enabled is 0, or interior sounds are off or at volume 0. */
// FUNCTION: XVT 0x42E460
void fsfx_stop_hyperspace_exit_sounds(int player_idx)
{
	if (g_flight_sim_side_effects_suppressed != 0) {
		return;
	}
	if (g_local_player != player_idx) {
		return;
	}
	if (g_flight_conf_sfx_enabled == 0) {
		return;
	}
	if (g_game_config.sfx_interior_enabled == 0) {
		return;
	}
	if (g_game_config.sfx_interior_volume == 0) {
		return;
	}
	if (sound_get_param(FLIGHT_SOUND_HYPERSPACE_EXIT_IMPERIAL, 256) != 0) {
		sound_stop_oldest_instance_by_id(
			FLIGHT_SOUND_HYPERSPACE_EXIT_IMPERIAL);
	}
	if (sound_get_param(FLIGHT_SOUND_HYPERSPACE_EXIT_NON_IMPERIAL, 256) !=
	    0) {
		sound_stop_oldest_instance_by_id(
			FLIGHT_SOUND_HYPERSPACE_EXIT_NON_IMPERIAL);
	}
}

/* Queues a flight sound for the local player with sound_queue_effect, restart
 * allowed, played once. Returns 0, queuing nothing, when the side-effect gate
 * shuts it out (g_flight_sfx_side_effect_gate 0 needs
 * g_flight_sim_side_effects_suppressed 0, 1 needs it 1, 2 never plays), player_idx
 * is not g_local_player, g_flight_conf_sfx_enabled is 0, the id holds 0 in
 * g_fsfx_loaded_by_slot, interior sounds (emitter_obj_idx -1) or exterior ones (any
 * other) are off or at volume 0, an R2 sound (ids 86 to 89) comes while the
 * player has no craft or one of an object type other than 1 or 2, or, with a
 * nonzero volume, a flyby or engine-wash sound (ids 72 to 79) already plays. A
 * hyperspace exit sound (81, 84) first stops the matching entry sound (80, 83).
 * The volume comes from fsfx_compute_source_volume and, when it is not 0, the pan
 * from fsfx_compute_source_pan, which may lower the volume. The priority is 127
 * for the danger warning (37) and 125 for another sound the local player makes
 * (no emitter, an emitter the player owns, or one whose mobile object's source
 * the player owns); for any other, the volume when under 125, else 124. Returns
 * 1 otherwise, also when the volume was 0 and nothing was queued. Does not
 * check sound_id against 838. */
// FUNCTION: XVT 0x42E4D0
int fsfx_play_sound(unsigned int sound_id, int emitter_obj_idx, int player_idx)
{
	if (g_flight_sfx_side_effect_gate != 0) {
		if (g_flight_sim_side_effects_suppressed == 0) {
			return 0;
		}
		if (g_flight_sfx_side_effect_gate == 2) {
			return 0;
		}
	} else if (g_flight_sim_side_effects_suppressed != 0) {
		return 0;
	}
	if (player_idx != g_local_player) {
		return 0;
	}
	if (g_flight_conf_sfx_enabled == 0) {
		return 0;
	}
	if (g_fsfx_loaded_by_slot[sound_id] == 0) {
		return 0;
	}

	if (emitter_obj_idx == -1) {
		if (g_game_config.sfx_interior_enabled == 0) {
			return 0;
		}
		if (g_game_config.sfx_interior_volume == 0) {
			return 0;
		}
	} else {
		if (g_game_config.sfx_exterior_enabled == 0) {
			return 0;
		}
		if (g_game_config.sfx_exterior_volume == 0) {
			return 0;
		}
	}

	if (sound_id >= FLIGHT_SOUND_R2_HAPPY &&
	    sound_id <= FLIGHT_SOUND_R2_HIT) {
		int object_index = g_players[player_idx].object_index;
		if (object_index == -1) {
			return 0;
		}
		int object_type = g_object_table[object_index].object_type;
		if (object_type != 1 && object_type != 2) {
			return 0;
		}
	}
	if (sound_id == FLIGHT_SOUND_HYPERSPACE_EXIT_IMPERIAL) {
		sound_stop_oldest_instance_by_id(
			FLIGHT_SOUND_HYPERSPACE_ENTER_IMPERIAL);
	}
	if (sound_id == FLIGHT_SOUND_HYPERSPACE_EXIT_NON_IMPERIAL) {
		sound_stop_oldest_instance_by_id(
			FLIGHT_SOUND_HYPERSPACE_ENTER_NON_IMPERIAL);
	}

	int volume = fsfx_compute_source_volume(emitter_obj_idx, sound_id);
	if (volume != 0) {
		int pan = fsfx_compute_source_pan(emitter_obj_idx, &volume);
		int priority = 124;
		if ((unsigned int)volume < 125) {
			priority = volume;
		}

		if (emitter_obj_idx == -1 ||
		    g_object_table[emitter_obj_idx].player_owner_idx ==
			    g_local_player) {
			priority = sound_id == FLIGHT_SOUND_DANGER_WARNING
					   ? 127
					   : 125;
		} else {
			struct object_record *source_object =
				&g_object_table[emitter_obj_idx];
			struct mobile_object *source_mobile_object =
				source_object->mobj;
			if (source_mobile_object != NULL &&
			    g_object_table[source_mobile_object->source_obj_idx]
					    .player_owner_idx ==
				    g_local_player) {
				priority = 125;
			}
		}

		if (sound_get_param(sound_id, 256) != 0 &&
		    sound_id >= FLIGHT_SOUND_TIE_FLYBY &&
		    sound_id <= FLIGHT_SOUND_ENGINE_WASH_OTHER) {
			return 0;
		}
		sound_queue_effect(g_fsfx_sfx_name_table[sound_id], 1, 0,
				   priority, volume, pan);
	}

	return 1;
}

/* Plays the firing sound of a projectile through fsfx_play_sound, with the
 * projectile as the emitter: object types 0x89 to 0x93 play id type - 133 (4 to
 * 14), 0x94 and 0x95 play type - 138 (10, 11), 0x96 and 0x97 play type - 135
 * (15, 16), and 0x98 to 0x9B play FLIGHT_SOUND_MAGNETIC_PULSE (17), returning
 * fsfx_play_sound's result. Returns 0 when player_idx is not g_local_player,
 * g_flight_conf_sfx_enabled is 0 or exterior sounds are off or at volume 0. */
// FUNCTION: XVT 0x42E720
int fsfx_triggerweaponsfx(unsigned int projectile_object_index, int player_idx)
{
	if (player_idx != g_local_player) {
		return 0;
	}
	if (g_flight_conf_sfx_enabled == 0) {
		return 0;
	}
	if (g_game_config.sfx_exterior_enabled == 0) {
		return 0;
	}
	if (g_game_config.sfx_exterior_volume == 0) {
		return 0;
	}

	/* result first holds the projectile's object type, from which the sound ids below are computed;
	 * a played sound replaces it with fsfx_play_sound's return, and an unlisted type returns the type. */
	int result = g_object_table[projectile_object_index].object_type;
	switch (g_object_table[projectile_object_index].object_type) {
	case 0x89:
	case 0x8a:
	case 0x8b:
	case 0x8c:
	case 0x8d:
	case 0x8e:
	case 0x8f:
	case 0x90:
	case 0x91:
	case 0x92:
	case 0x93:
		result = fsfx_play_sound(result - 133, projectile_object_index,
					 player_idx);
		break;
	case 0x94:
	case 0x95:
		result = fsfx_play_sound(result - 138, projectile_object_index,
					 player_idx);
		break;
	case 0x96:
	case 0x97:
		result = fsfx_play_sound(result - 135, projectile_object_index,
					 player_idx);
		break;
	case 0x98:
	case 0x99:
	case 0x9a:
	case 0x9b:
		result = fsfx_play_sound(FLIGHT_SOUND_MAGNETIC_PULSE,
					 projectile_object_index, player_idx);
		break;
	}
	return result;
}

/* Volume, 0 to 127, of a sound as the local player hears it. An interior sound
 * (emitter_obj_idx -1) gets s * g_fsfx_base_volume_by_sfx_slot[sound_id] / 127, s being
 * the interior volume setting times 13, or 127 for a setting of 10 or more;
 * that path does not check sound_id against the table's 96 entries. For any
 * other, f and b are the falloff distance and base volume from the two tables
 * (8192 and 112 for ids from 95), and d is the rough distance
 * (collide_roughdistance3d) from the local player's camera to the emitter, at
 * its previous-step position when it has a mobile object, else where
 * mission_resolve_object_or_mission_point_world_loc puts it. It returns 0 when d >> 2
 * is at least f, b >> 3 when d >> 1 is, and b >> 2 when d is. Otherwise it
 * returns v = (b >> 2) + (f - d) * (b - (b >> 2)) / (f - (f >> 5)), times
 * e / 10 when the exterior volume setting e is not 10, capped at 127; e does
 * not scale the three far cases. */
// FUNCTION: XVT 0x42E810
unsigned int fsfx_compute_source_volume(int emitter_obj_idx,
					unsigned int sound_id)
{
	if (emitter_obj_idx == -1) {
		unsigned int volume_scale = g_game_config.sfx_interior_volume;
		if (volume_scale >= 10) {
			volume_scale = 127;
		} else {
			volume_scale *= 13;
		}
		return volume_scale * g_fsfx_base_volume_by_sfx_slot[sound_id] /
		       127;
	}

	unsigned int falloff_distance;
	unsigned int base_volume;
	if (sound_id >= 95) {
		falloff_distance = 8192;
		base_volume = 112;
	} else {
		falloff_distance =
			g_fsfx_falloff_distance_by_sfx_slot[sound_id];
		base_volume = g_fsfx_base_volume_by_sfx_slot[sound_id];
	}
	struct player_data *listener;
	int delta_x;
	int delta_y;
	int world_z;
	if (g_object_table[emitter_obj_idx].mobj != NULL) {
		listener = &g_players[g_local_player];
		delta_x = g_object_table[emitter_obj_idx].mobj->prev_world_x -
			  listener->view_state.camera_world_x;
		delta_y = g_object_table[emitter_obj_idx].mobj->prev_world_y -
			  listener->view_state.camera_world_y;
		world_z = g_object_table[emitter_obj_idx].mobj->prev_world_z;
	} else {
		mission_resolve_object_or_mission_point_world_loc(
			emitter_obj_idx, 0);
		listener = &g_players[g_local_player];
		delta_x = g_world_loc_x - listener->view_state.camera_world_x;
		delta_y = g_world_loc_y - listener->view_state.camera_world_y;
		world_z = g_world_loc_z;
	}
	unsigned int distance = collide_roughdistance3d(
		delta_x, delta_y,
		world_z - listener->view_state.camera_world_z);
	unsigned int scaled_distance = distance >> 2;
	if (scaled_distance >= falloff_distance) {
		return 0;
	}
	scaled_distance = distance >> 1;
	if (scaled_distance >= falloff_distance) {
		return base_volume >> 3;
	}
	if (distance >= falloff_distance) {
		return base_volume >> 2;
	}

	unsigned int quarter_volume = base_volume >> 2;
	unsigned int distance_span = falloff_distance - distance;
	unsigned int volume_range = base_volume - quarter_volume;
	unsigned int volume =
		quarter_volume +
		distance_span * volume_range /
			(falloff_distance - (falloff_distance >> 5));
	if (g_game_config.sfx_exterior_volume != 10) {
		volume = volume * g_game_config.sfx_exterior_volume / 10;
	}
	if (volume > 127) {
		volume = 127;
	}
	return volume;
}

/* Pan, 0 to 127 with 64 centered, of a sound from an object as the local
 * player hears it; 64 for emitter_obj_idx -1. It takes the emitter's offset
 * from the local player's camera (placed as in fsfx_compute_source_volume),
 * each axis cut to 16 bits, projects it with math_dot3q15_wrapped on the
 * camera matrix's side row (g_camMatR0) and forward row (g_camMatR2), and
 * takes the angle trig2_arctan(side, forward). For an angle of 0x4000 or
 * more either way, a source behind, it mirrors the angle front to back,
 * keeping its side, and lowers *volume by (int16_t)(*volume * r) / 128,
 * where r = ((0x4000 - v) >> 8) * ((0x4000 - a) >> 8) / 64, a being the size
 * of the mirrored angle and v the size of 0x8000 minus the angle of the
 * offset along the camera's up row (g_camMatR1) against the forward one: up
 * to half the volume for a source straight behind, none for one level at the
 * side. Returns the angle >> 7, clamped to -64 to 63, plus 64. */
// FUNCTION: XVT 0x42E9A0
int fsfx_compute_source_pan(int emitter_obj_idx, int *volume)
{
	if (emitter_obj_idx == -1) {
		return 64;
	}

	struct mobile_object *source_mobile_object =
		g_object_table[emitter_obj_idx].mobj;
	int dx;
	int dy;
	int dz;
	if (source_mobile_object != NULL) {
		dx = source_mobile_object->prev_world_x -
		     g_players[g_local_player].view_state.camera_world_x;
		dy = source_mobile_object->prev_world_y -
		     g_players[g_local_player].view_state.camera_world_y;
		dz = source_mobile_object->prev_world_z -
		     g_players[g_local_player].view_state.camera_world_z;
	} else {
		mission_resolve_object_or_mission_point_world_loc(
			emitter_obj_idx, 0);
		dz = g_world_loc_z -
		     g_players[g_local_player].view_state.camera_world_z;
		dx = g_world_loc_x -
		     g_players[g_local_player].view_state.camera_world_x;
		dy = g_world_loc_y -
		     g_players[g_local_player].view_state.camera_world_y;
	}

	int16_t side_offset = (int16_t)math_dot3q15_wrapped(
		(int16_t)dx, (int16_t)dy, (int16_t)dz, g_cam_mat_r0_x,
		g_cam_mat_r0_y, g_cam_mat_r0_z);
	int16_t forward_offset = (int16_t)math_dot3q15_wrapped(
		(int16_t)dx, (int16_t)dy, (int16_t)dz, g_cam_mat_r2_x,
		g_cam_mat_r2_y, g_cam_mat_r2_z);
	int16_t pan_angle = trig2_arctan(side_offset, forward_offset);

	if (pan_angle >= 0x4000 || pan_angle <= -0x4000) {
		/* From here side_offset holds the offset along the camera's up axis, for the vertical angle. */
		side_offset = (int16_t)math_dot3q15_wrapped(
			(int16_t)dx, (int16_t)dy, (int16_t)dz, g_cam_mat_r1_x,
			g_cam_mat_r1_y, g_cam_mat_r1_z);
		int16_t vertical_angle =
			(int16_t)(0x8000 -
				  trig2_arctan(side_offset, forward_offset));
		int16_t rear_angle = (int16_t)(0x8000 - pan_angle);
		pan_angle = (int16_t)(0x8000 - pan_angle);
		if (vertical_angle < 0) {
			vertical_angle = (int16_t)-vertical_angle;
		}
		if (pan_angle < 0) {
			rear_angle = (int16_t)-pan_angle;
		}

		int16_t rear_scale = (int16_t)(0x4000 - rear_angle);
		int16_t reduction = (int16_t)(0x4000 - vertical_angle);
		rear_scale >>= 8;
		reduction >>= 8;
		reduction = (int16_t)(reduction * rear_scale);
		reduction = (int16_t)(reduction / 64);
		reduction = (int16_t)(*volume * reduction);
		*volume -= reduction / 128;
	}

	pan_angle >>= 7;
	if (pan_angle < -64) {
		pan_angle = -64;
	}
	if (pan_angle > 63) {
		pan_angle = 63;
	}
	return (int16_t)(pan_angle + 64);
}

/* Plays or stops the targeting tone loops, ids 50 and 51. State 0 or 1 stops 51
 * when it plays and returns, else stops 50 when it plays. State 3 stops 50 and
 * queues 51 unless it plays; any other state stops 51 and queues 50 unless it
 * plays. Loops are queued looping, centered, at priority 125 and the interior
 * volume setting times 13, 127 from 10 up. Returns 1, or 0 when
 * g_flight_conf_sfx_enabled is 0 or interior sounds are off or at volume 0. */
// FUNCTION: XVT 0x42EC80
int fsfx_update_targeting_tone(unsigned int tone_state)
{
	if (g_flight_conf_sfx_enabled == 0) {
		return 0;
	}
	if (g_game_config.sfx_interior_enabled == 0) {
		return 0;
	}
	if (g_game_config.sfx_interior_volume == 0) {
		return 0;
	}

	int interior_volume = g_game_config.sfx_interior_volume;
	int volume = 127;
	if (interior_volume < 10) {
		volume = 13 * interior_volume;
	}

	if (tone_state == 0 || tone_state == 1) {
		if (sound_get_param(51, 256) != 0) {
			sound_stop_oldest_instance_by_id(51);
			return 1;
		}
		if (sound_get_param(50, 256) != 0) {
			sound_stop_oldest_instance_by_id(50);
		}
	} else if (tone_state == 3) {
		if (sound_get_param(50, 256) != 0) {
			sound_stop_oldest_instance_by_id(50);
		}
		if (sound_get_param(51, 256) == 0) {
			sound_queue_effect(g_fsfx_sfx_name_table[51], 1, 1, 125,
					   volume, 64);
			return 1;
		}
	} else {
		if (sound_get_param(51, 256) != 0) {
			sound_stop_oldest_instance_by_id(51);
		}
		if (sound_get_param(50, 256) == 0) {
			sound_queue_effect(g_fsfx_sfx_name_table[50], 1, 1, 125,
					   volume, 64);
			return 1;
		}
	}
	return 1;
}

/* Keeps the beam weapon's loop sounds in step with the local player's beam.
 * With active set and the beam subsystem working: a tractor beam (while its
 * fire sound, 52, is not playing) or a jamming beam (while 55 is not) plays 53
 * or 56 while g_local_beam_target_obj_idx is 0xFFFF and the next id, 54 or 57, once
 * the beam holds a target, stopping the other of the pair; a decoy beam queues
 * 59 unless 58 or 59 plays; any other beam type queues 61 unless 60 or 61
 * plays. Otherwise it stops every playing id from 52 to 61. Loops are queued
 * looping, centered, at priority 125 with fsfx_compute_source_volume(-1, id).
 * Does nothing when g_flight_sim_side_effects_suppressed is set, player_idx is not
 * g_local_player, g_flight_conf_sfx_enabled is 0 or interior sounds are off or at
 * volume 0. Does not check that the local player has a craft. */
// FUNCTION: XVT 0x42EDC0
void fsfx_update_beam_system_loop(int active, int player_idx)
{
	if (g_flight_sim_side_effects_suppressed != 0) {
		return;
	}
	if (g_local_player != player_idx) {
		return;
	}
	if (g_flight_conf_sfx_enabled == 0) {
		return;
	}
	if (g_game_config.sfx_interior_enabled == 0) {
		return;
	}
	if (g_game_config.sfx_interior_volume == 0) {
		return;
	}

	struct craft_data *craft =
		g_object_table[g_players[g_local_player].object_index]
			.mobj->p_craft;
	if (active != 0 && (craft->working_subsystems &
			    CRAFT_SUBSYSTEM_FLAG_BEAM_SYSTEM) != 0) {
		beam_type beam_type = craft->beam_type_id;
		int sound_id;
		int volume;
		if (beam_type == BEAM_TYPE_TRACTOR) {
			if (sound_get_param(52, 256) != 0) {
				return;
			}
			sound_id = 53;
		} else if (beam_type == BEAM_TYPE_JAMMING) {
			if (sound_get_param(55, 256) != 0) {
				return;
			}
			sound_id = 56;
		} else if (beam_type == BEAM_TYPE_DECOY) {
			if (sound_get_param(58, 256) == 0 &&
			    sound_get_param(59, 256) == 0) {
				volume = fsfx_compute_source_volume(-1, 59);
				sound_queue_effect(g_fsfx_sfx_name_table[59], 1,
						   1, 125, volume, 64);
			}
			return;
		} else {
			if (sound_get_param(60, 256) == 0 &&
			    sound_get_param(61, 256) == 0) {
				volume = fsfx_compute_source_volume(-1, 61);
				sound_queue_effect(g_fsfx_sfx_name_table[61], 1,
						   1, 125, volume, 64);
			}
			return;
		}

		int paired_sound_id;
		if (g_local_beam_target_obj_idx == UINT16_MAX) {
			paired_sound_id = sound_id + 1;
			if (sound_get_param(paired_sound_id, 256) != 0) {
				sound_stop_oldest_instance_by_id(
					paired_sound_id);
			}
			if (sound_get_param(sound_id, 256) == 0) {
				volume = fsfx_compute_source_volume(-1,
								    sound_id);
				sound_queue_effect(
					g_fsfx_sfx_name_table[sound_id], 1, 1,
					125, volume, 64);
			}
		} else {
			if (sound_get_param(sound_id, 256) != 0) {
				sound_stop_oldest_instance_by_id(sound_id);
			}
			paired_sound_id = sound_id + 1;
			if (sound_get_param(paired_sound_id, 256) == 0) {
				volume = fsfx_compute_source_volume(
					-1, paired_sound_id);
				sound_queue_effect(
					g_fsfx_sfx_name_table[sound_id + 1], 1,
					1, 125, volume, 64);
			}
		}
		return;
	}

	for (int stop_sound_id = 52; stop_sound_id <= 61; ++stop_sound_id) {
		if (sound_get_param(stop_sound_id, 256) != 0) {
			sound_stop_oldest_instance_by_id(stop_sound_id);
		}
	}
}

/* State 0 stops the incoming-missile warning loops, ids 39 and 40. State 1
 * queues 40 at the interior volume / 3, any other state 39 at the interior
 * volume / 2, looping, centered, at priority 125, unless that id plays; it does
 * not stop the other loop. The interior volume is the setting times 13, 127
 * from 10 up. Does nothing when g_flight_conf_sfx_enabled is 0 or interior sounds
 * are off or at volume 0. */
// FUNCTION: XVT 0x42F030
void fsfx_update_incoming_missile_warning(int warning_state)
{
	if (g_flight_conf_sfx_enabled &&
	    g_game_config.sfx_interior_enabled != 0 &&
	    g_game_config.sfx_interior_volume != 0) {
		if (warning_state == 0) {
			if (sound_get_param(39, 256) != 0) {
				sound_stop_oldest_instance_by_id(39);
			}
			if (sound_get_param(40, 256) != 0) {
				sound_stop_oldest_instance_by_id(40);
			}
			return;
		}

		int interior_volume = g_game_config.sfx_interior_volume;
		int volume = 127;
		if (interior_volume < 10) {
			volume = 13 * interior_volume;
		}
		int sound_id;
		if (warning_state == 1) {
			sound_id = 40;
			volume /= 3;
		} else {
			sound_id = 39;
			volume /= 2;
		}
		if (sound_get_param(sound_id, 256) == 0) {
			sound_queue_effect(g_fsfx_sfx_name_table[sound_id], 1,
					   1, 125, volume, 64);
		}
	}
}

/* Keeps the chaff loop, id 19, playing while the local player's chaff runs.
 * Stops 19 when the player has no craft or awaiting_new_craft is 1; does nothing
 * more for a craft whose countermeasure is not chaff. Otherwise, while the
 * craft's chaff_active_seconds is nonzero, queues 19 looping, centered, at
 * priority 125 and a quarter of the interior volume (the setting times 13, 127
 * from 10 up) unless it plays, and stops it once that count is 0. Does nothing
 * when g_flight_sim_side_effects_suppressed is set, g_flight_conf_sfx_enabled is 0 or
 * interior sounds are off or at volume 0. */
// FUNCTION: XVT 0x42F110
void fsfx_update_chaff_loop(void)
{
	if (g_flight_sim_side_effects_suppressed != 0) {
		return;
	}
	if (g_flight_conf_sfx_enabled == 0) {
		return;
	}
	if (g_game_config.sfx_interior_enabled == 0) {
		return;
	}
	if (g_game_config.sfx_interior_volume == 0) {
		return;
	}

	int player_object_index = g_players[g_local_player].object_index;
	if (player_object_index == -1) {
		if (sound_get_param(19, 256) != 0) {
			sound_stop_oldest_instance_by_id(19);
		}
		return;
	}

	struct object_record *player_object =
		&g_object_table[player_object_index];
	struct craft_data *craft = player_object->mobj->p_craft;
	if (craft->cm_type_id != COUNTERMEASURE_TYPE_CHAFF) {
		return;
	}

	if (g_players[g_local_player].awaiting_new_craft == 1) {
		if (sound_get_param(19, 256) != 0) {
			sound_stop_oldest_instance_by_id(19);
		}
		return;
	}

	int interior_volume = g_game_config.sfx_interior_volume;
	int volume;
	if (interior_volume >= 10) {
		volume = 127;
	} else {
		volume = 13 * interior_volume;
	}
	volume /= 4;

	if (craft->cm_type_id == COUNTERMEASURE_TYPE_CHAFF &&
	    craft->chaff_active_seconds != 0) {
		if (sound_get_param(19, 256) == 0) {
			sound_queue_effect(g_fsfx_sfx_name_table[19], 1, 1, 125,
					   volume, 64);
		}
	} else if (sound_get_param(19, 256) != 0) {
		sound_stop_oldest_instance_by_id(19);
	}
}

/* Keeps the local player's engine loop in step with the throttle. The craft's
 * object type picks the sound and base frequency in hertz: types 1, 4, 14 and
 * 15 give 67 at 11000, 2 gives 68, 3 and 13 give 69, 5 to 9 give 70, each at
 * 5500, and 12 and 16 give 71 at 11000. For such a craft it stores the type in
 * g_player_engine_loop_object_type; when the player is not awaiting a new craft and
 * the engines work, it sets the frequency of the effect's newest instance
 * (sound_set_param code 0x777) to 55 * (math2_ratio_q16(throttle_speed,
 * 0xFFFF) / 655) + base, at most base + 5500, and then, when the sound is not
 * playing, queues it looping, centered, at priority 125 and volume
 * math2_fraction(e, throttle_speed) >> 1, e being the engine volume setting
 * times 13, 127 from 10 up. Otherwise it stops the engine sound and the
 * engine-wash sounds 78 and 79. With no craft or one of another type, and
 * map_camera_state nonzero, it stops the sound for g_player_engine_loop_object_type's
 * type and 78 and 79. Does nothing when g_flight_sim_side_effects_suppressed is
 * set, g_flight_conf_sfx_enabled is 0 or engine sounds are off or at volume 0. */
// FUNCTION: XVT 0x42F270
void fsfx_update_player_engine_loop(void)
{
	if (g_flight_sim_side_effects_suppressed != 0 ||
	    g_flight_conf_sfx_enabled == 0 ||
	    g_game_config.sfx_engine_enabled == 0 ||
	    g_game_config.sfx_engine_volume == 0) {
		return;
	}

	int engine_sound_id = -1;
	int object_index = g_players[g_local_player].object_index;
	int base_frequency;
	uint8_t object_type;
	if (object_index != -1) {
		object_type = g_object_table[object_index].object_type;
		switch (object_type) {
		case 1:
		case 4:
		case 14:
		case 15:
			engine_sound_id = 67;
			base_frequency = 11000;
			break;
		case 2:
			engine_sound_id = 68;
			base_frequency = 5500;
			break;
		case 3:
		case 13:
			engine_sound_id = 69;
			base_frequency = 5500;
			break;
		case 5:
		case 6:
		case 7:
		case 8:
		case 9:
			engine_sound_id = 70;
			base_frequency = 5500;
			break;
		case 12:
		case 16:
			engine_sound_id = 71;
			base_frequency = 11000;
			break;
		}
	}

	if (engine_sound_id != -1) {
		g_player_engine_loop_object_type = object_type;
		if (g_players[g_local_player].awaiting_new_craft != 1) {
			struct craft_data *craft =
				g_object_table[object_index].mobj->p_craft;
			if ((craft->working_subsystems &
			     CRAFT_SUBSYSTEM_FLAG_ENGINES) != 0) {
				uint16_t config_volume =
					g_game_config.sfx_engine_volume;
				if (config_volume >= 10) {
					config_volume = 127;
				} else {
					config_volume *= 13;
				}
				int frequency =
					55 * (math2_ratio_q16(
						      craft->throttle_speed,
						      0xffff) /
					      655) +
					base_frequency;
				config_volume = (uint16_t)math2_fraction(
					config_volume, craft->throttle_speed);
				int volume = config_volume >> 1;
				if (sound_get_param(engine_sound_id, 256) ==
				    0) {
					sound_set_param(engine_sound_id, 1911,
							frequency);
					sound_queue_effect(
						g_fsfx_sfx_name_table
							[engine_sound_id],
						1, 1, 125, volume, 64);
					return;
				}
				sound_set_param(engine_sound_id, 1911,
						frequency);
				return;
			}
		}
		if (sound_get_param(engine_sound_id, 256) != 0) {
			sound_stop_oldest_instance_by_id(engine_sound_id);
		}
		if (sound_get_param(78, 256) != 0) {
			sound_stop_oldest_instance_by_id(78);
		}
		if (sound_get_param(79, 256) != 0) {
			sound_stop_oldest_instance_by_id(79);
		}
		return;
	} else {
		if (g_players[g_local_player].map_camera_state == 0) {
			return;
		}
		switch (g_player_engine_loop_object_type) {
		case 1:
		case 4:
		case 14:
		case 15:
			engine_sound_id = 67;
			break;
		case 2:
			engine_sound_id = 68;
			break;
		case 3:
		case 13:
			engine_sound_id = 69;
			break;
		case 5:
		case 6:
		case 7:
		case 8:
		case 9:
			engine_sound_id = 70;
			break;
		case 12:
		case 16:
			engine_sound_id = 71;
			break;
		}
		if (sound_get_param(engine_sound_id, 256) != 0) {
			sound_stop_oldest_instance_by_id(engine_sound_id);
		}
		if (sound_get_param(78, 256) != 0) {
			sound_stop_oldest_instance_by_id(78);
		}
		if (sound_get_param(79, 256) != 0) {
			sound_stop_oldest_instance_by_id(79);
		}
	}
}

/* Keeps the loops for the beam effects on the local player's craft in step.
 * While beam_effect_accum[1] is nonzero it queues 63 unless it plays and,
 * when neither 64 nor 65 plays and game_rand2() < 0x1000 (in the modern
 * build only while xvt_flight_timing_reference_due is true), queues id
 * (game_rand2() & 1) + 63, so 63 or 64; once it is 0 it stops 63, 64 and 65.
 * While beam_effect_accum[2] is nonzero it queues 66 at half volume unless it
 * plays, and stops it once that is 0. With no craft it stops 63 to 66.
 * Loops are queued looping, centered, at priority 125 and the interior
 * volume setting times 13, 127 from 10 up. Does nothing when
 * g_flight_sim_side_effects_suppressed is set, g_flight_conf_sfx_enabled is 0 or
 * interior sounds are off or at volume 0. */
// FUNCTION: XVT 0x42F5D0
void fsfx_update_beam_effect_loops(void)
{
	if (g_flight_sim_side_effects_suppressed != 0) {
		return;
	}
	if (g_flight_conf_sfx_enabled == 0) {
		return;
	}
	if (g_game_config.sfx_interior_enabled == 0) {
		return;
	}
	if (g_game_config.sfx_interior_volume == 0) {
		return;
	}

	int interior_volume = g_game_config.sfx_interior_volume;
	int volume = 127;
	if (interior_volume < 10) {
		volume = 13 * interior_volume;
	}

	int object_index = g_players[g_local_player].object_index;
	if (object_index == -1) {
		if (sound_get_param(63, 256) != 0) {
			sound_stop_oldest_instance_by_id(63);
		}
		if (sound_get_param(64, 256) != 0) {
			sound_stop_oldest_instance_by_id(64);
		}
		if (sound_get_param(65, 256) != 0) {
			sound_stop_oldest_instance_by_id(65);
		}
		if (sound_get_param(66, 256) != 0) {
			sound_stop_oldest_instance_by_id(66);
		}
		return;
	}

	struct craft_data *craft = g_object_table[object_index].mobj->p_craft;
	if (craft->beam_effect_accum[1] != 0) {
		if (sound_get_param(63, 256) == 0) {
			sound_queue_effect(g_fsfx_sfx_name_table[63], 1, 1, 125,
					   volume, 64);
		}
		if (sound_get_param(64, 256) == 0 &&
		    sound_get_param(65, 256) == 0
#ifdef XVT_MODERN
		    && xvt_flight_timing_reference_due()
#endif
		    && game_rand2() < 0x1000) {
			sound_queue_effect(
				g_fsfx_sfx_name_table[(game_rand2() & 1) + 63],
				1, 1, 125, volume, 64);
		}
	} else {
		if (sound_get_param(63, 256) != 0) {
			sound_stop_oldest_instance_by_id(63);
		}
		if (sound_get_param(64, 256) != 0) {
			sound_stop_oldest_instance_by_id(64);
		}
		if (sound_get_param(65, 256) != 0) {
			sound_stop_oldest_instance_by_id(65);
		}
	}

	if (craft->beam_effect_accum[2] != 0) {
		if (sound_get_param(66, 256) == 0) {
			sound_queue_effect(g_fsfx_sfx_name_table[66], 1, 1, 125,
					   volume / 2, 64);
		}
	} else if (sound_get_param(66, 256) != 0) {
		sound_stop_oldest_instance_by_id(66);
	}
}

/* Runs the per-step flight sounds of the local player; returns at once when
 * the player has no craft. Calls fsfx_update_chaff_loop,
 * fsfx_update_player_engine_loop and fsfx_update_beam_effect_loops, then stops
 * there when g_flight_sim_side_effects_suppressed is set,
 * g_flight_conf_sfx_enabled is 0 or engine sounds are off or at volume 0.
 * Engine wash: while engine_wash_source_obj_idx is not -1 it picks id 78 for a
 * source of object type 51 to 54, else 79, and a volume of 4 * the exterior
 * volume setting * engine_wash_strength / 10, capped at 127. When that id is
 * not playing it emits in-flight message IFMSG_221 and queues the id
 * looping, centered, with that volume passed as the priority and 65 as the
 * volume; while it plays it sets its volume to that value (sound_set_param
 * code 0x600). With no wash source it stops 78 and 79. Flybys: for every
 * other craft slot below g_active_region_craft_object_slot_end holding an active
 * craft with working subsystems and nonzero speed whose object type has a
 * flyby sound (ids 72 to 77), it plays that sound through fsfx_play_sound
 * when the craft's rough distance to the player is under its type's
 * max_bounds_extent + 1024 and its distance at the previous step was not. */
// FUNCTION: XVT 0x42F810
void fsfx_update_flight_sfx(void)
{
	int player_object_index = g_players[g_local_player].object_index;
	if (player_object_index == -1) {
		return;
	}

	fsfx_update_chaff_loop();
	fsfx_update_player_engine_loop();
	fsfx_update_beam_effect_loops();
	if (g_flight_sim_side_effects_suppressed != 0 ||
	    g_flight_conf_sfx_enabled == 0 ||
	    g_game_config.sfx_engine_enabled == 0 ||
	    g_game_config.sfx_engine_volume == 0) {
		return;
	}

	uint8_t object_type;
	if (g_players[g_local_player].engine_wash_source_obj_idx != -1) {
		object_type =
			g_object_table[(uint16_t)g_players[g_local_player]
					       .engine_wash_source_obj_idx]
				.object_type;
		int wash_sound_id;
		if (object_type == 51 || object_type == 52 ||
		    object_type == 53) {
			wash_sound_id = 78;
		} else {
			wash_sound_id = 79;
			if (object_type == 54) {
				wash_sound_id = 78;
			}
		}
		int wash_volume =
			4 * g_game_config.sfx_exterior_volume *
			g_players[g_local_player].engine_wash_strength / 10;
		if (wash_volume > 127) {
			wash_volume = 127;
		}
		if (sound_get_param(wash_sound_id, 256) == 0) {
			msg_emit_in_flight_message(
				IFMSG_221_YOU_RE_TAKING_DAMAGE_FROM_ENGINE_WASH,
				g_local_player);
			sound_queue_effect(g_fsfx_sfx_name_table[wash_sound_id],
					   1, 1, wash_volume, 65, 64);
		} else {
			sound_set_param(wash_sound_id, 1536, wash_volume);
		}
	} else {
		if (sound_get_param(78, 256) != 0) {
			sound_stop_oldest_instance_by_id(78);
		}
		if (sound_get_param(79, 256) != 0) {
			sound_stop_oldest_instance_by_id(79);
		}
	}

	int object_index = 0;
	struct object_record *object;
	struct mobile_object *mobile_object;
	struct craft_data *craft;
	uint16_t flyby_sound_id;
	if (g_active_region_craft_object_slot_end > 0) {
		do {
			if (object_index != player_object_index) {
				object = &g_object_table[object_index];
				if (g_object_table[object_index].object_type !=
				    0) {
					mobile_object = object->mobj;
					craft = mobile_object->p_craft;
					if (craft->object_kind ==
						    CRAFT_OBJECT_KIND_ACTIVE &&
					    craft->working_subsystems != 0 &&
					    mobile_object->speed != 0) {
						flyby_sound_id = UINT16_MAX;
						object_type =
							g_object_table[object_index]
								.object_type;
						switch (object_type) {
						case 1:
						case 4:
						case 14:
						case 15:
							flyby_sound_id =
								FLIGHT_SOUND_X_WING_FLYBY;
							break;
						case 2:
							flyby_sound_id =
								FLIGHT_SOUND_Y_WING_FLYBY;
							break;
						case 3:
						case 13:
							flyby_sound_id =
								FLIGHT_SOUND_A_WING_FLYBY;
							break;
						case 5:
						case 6:
						case 7:
						case 8:
						case 9:
							flyby_sound_id =
								FLIGHT_SOUND_TIE_FLYBY;
							break;
						case 12:
						case 16:
							flyby_sound_id =
								FLIGHT_SOUND_SHUTTLE_FLYBY;
							break;
						case 17:
						case 18:
						case 19:
						case 20:
						case 21:
						case 22:
						case 23:
						case 24:
						case 25:
						case 30:
							flyby_sound_id =
								FLIGHT_SOUND_SHUTTLE_FLYBY;
							break;
						case 38:
						case 39:
							flyby_sound_id =
								FLIGHT_SOUND_MILLENNIUM_FALCON_FLYBY;
							break;
						}
						if (flyby_sound_id !=
						    UINT16_MAX) {
							int current_distance = collide_roughdistance3d(
								object->world_x -
									g_object_table[player_object_index]
										.world_x,
								object->world_y -
									g_object_table[player_object_index]
										.world_y,
								object->world_z -
									g_object_table[player_object_index]
										.world_z);
							int previous_distance = collide_roughdistance3d(
								g_object_table[object_index]
										.mobj
										->prev_world_x -
									g_object_table[player_object_index]
										.mobj
										->prev_world_x,
								g_object_table[object_index]
										.mobj
										->prev_world_y -
									g_object_table[player_object_index]
										.mobj
										->prev_world_y,
								g_object_table[object_index]
										.mobj
										->prev_world_z -
									g_object_table[player_object_index]
										.mobj
										->prev_world_z);
							int flyby_distance =
								g_object_type_table
									[object_type]
										.max_bounds_extent +
								1024;
							if (flyby_distance >
							    current_distance) {
								if (previous_distance >=
								    flyby_distance) {
									fsfx_play_sound(
										flyby_sound_id,
										object_index,
										g_local_player);
								}
							}
						}
					}
				}
			}
			++object_index;
		} while (g_active_region_craft_object_slot_end > object_index);
	}
}

/* Queues a wingman's radio line, speaker type 1, about target_obj_idx. Returns 0
 * when wingman voices are off, player_idx is -1 or not g_local_player, or the
 * player has no craft; unless probability is 0xFFFF it is halved at wingman
 * voice level 1 and the call returns 0 unless game_rand2() is under it. With
 * speaker_obj_idx -1 it picks the speaker with fsfx_random_index among the craft
 * slots from g_active_region_object_slot_start below
 * g_active_region_craft_object_slot_end that hold a craft of mobile family 0 in the
 * player's flight group not owned by player_idx, kept in a 6-entry array it does
 * not bound; with none, category 12 queues voice slot 37 twice, and it returns
 * 0. When the pick is the target it returns 0 if it is the only one, else keeps
 * it as the speaker: the next candidate it computes is not used. A given
 * speaker returns 0 unless its slot is below g_active_region_craft_object_slot_end
 * and, for a category other than 1, it flies in the player's group. The line
 * comes from the speaker's voice list at slot 97 * craft_ordinal + 114;
 * craft_index_in_group over 6 counts as 0. With response_index -1 the category
 * picks the line: 3 and 5, which return 0 unless the voice queue is empty, take
 * fsfx_select_available_voice_variant(category, the target's craft_ordinal) and,
 * when craft_index_in_group is nonzero, count the play and queue the call sign
 * line (category 2) first, then the line chained; 6, which also needs the queue
 * empty, and 9, 10 and 21 take and count a variant; 12 takes
 * fsfx_random_index(4); 16 takes and counts a variant, then chains category 17's
 * line for the target's craft_ordinal; a missing variant returns 0, and other
 * categories queue nothing. With a response_index, category 1 queues the call
 * sign (category 0) when craft_index_in_group is nonzero, category 1's first line
 * when game_rand2() < 0x5555, then the response; 23 queues a random line of
 * category 21 (among the first 5) when game_rand2() < 0x8000, then the response,
 * counted under the speaker's craft_ordinal; other categories queue the
 * response. A response not under its category's variant count returns 0, after
 * the lines queued before it. Counts go to g_fsfx_voice_line_play_counts, under the
 * target's craft_ordinal (0 without a target) except for category 23. Returns 1
 * otherwise. */
// FUNCTION: XVT 0x42FB70
int fsfx_speak_wingman_event(int player_idx, int speaker_obj_idx,
			     int voice_category, int response_index,
			     int target_obj_idx, uint16_t probability)
{
	if (g_game_config.voice_pilot_level == 0) {
		return 0;
	}
	if (player_idx == -1) {
		return 0;
	}
	if (g_local_player != player_idx) {
		return 0;
	}
	int player_obj_idx = g_players[player_idx].object_index;
	if (player_obj_idx == -1) {
		return 0;
	}
	if (probability != UINT16_MAX) {
		if (g_game_config.voice_pilot_level == 1) {
			probability >>= 1;
		}
		if (game_rand2() >= probability) {
			return 0;
		}
	}

	if (speaker_obj_idx == -1) {
		int candidate_count = 0;
		unsigned int candidate_index =
			g_active_region_object_slot_start;
		int candidates[6];
		if ((unsigned int)g_active_region_craft_object_slot_end >
		    candidate_index) {
			do {
				if (g_object_table[candidate_index]
						    .object_type != 0 &&
				    g_object_table[candidate_index]
						    .mobj->family == 0 &&
				    g_object_table[candidate_index]
						    .player_owner_idx !=
					    player_idx &&
				    g_object_table[player_obj_idx]
						    .flight_group_idx ==
					    g_object_table[candidate_index]
						    .flight_group_idx) {
					candidates[candidate_count++] =
						candidate_index;
				}
				candidate_index++;
			} while ((unsigned int)
					 g_active_region_craft_object_slot_end >
				 candidate_index);
		}
		if (candidate_count == 0) {
			if (voice_category == 12) {
				fsfx_queue_voice_sfx(37, 0, 0, 0, UINT16_MAX);
				fsfx_queue_voice_sfx(37, 0, 0, 0, UINT16_MAX);
			}
			return 0;
		}
		candidate_index = fsfx_random_index(candidate_count);
		speaker_obj_idx = candidates[candidate_index];
		if (target_obj_idx == speaker_obj_idx) {
			unsigned int alternate_index = candidate_index + 1;
			if (alternate_index >= (unsigned int)candidate_count) {
				alternate_index = 0;
			}
			if (alternate_index == candidate_index) {
				return 0;
			}
		}
	} else {
		if (g_active_region_craft_object_slot_end <= speaker_obj_idx) {
			return 0;
		}
		if (voice_category != 1 &&
		    g_object_table[speaker_obj_idx].flight_group_idx !=
			    g_object_table[player_obj_idx].flight_group_idx) {
			return 0;
		}
	}

	struct craft_data *craft =
		g_object_table[speaker_obj_idx].mobj->p_craft;
	int craft_ordinal = craft->craft_ordinal;
	unsigned int craft_index_in_group = craft->craft_index_in_group;
	if (craft_index_in_group > 6) {
		craft_index_in_group = 0;
	}
	int speaker_voice_list_slot = 97 * craft_ordinal + 114;
	int target_craft_ordinal = 0;
	uint16_t object_signature;
	if (target_obj_idx == -1 ||
	    g_active_region_craft_object_slot_end <= target_obj_idx) {
		object_signature = UINT16_MAX;
	} else {
		object_signature =
			g_object_table[target_obj_idx].object_signature;
		target_craft_ordinal = g_object_table[target_obj_idx]
					       .mobj->p_craft->craft_ordinal;
	}
	int base_offset = g_fsfx_voice_category_base_offset[voice_category];

	int selected_response = response_index;
	if (selected_response == -1) {
		switch (voice_category) {
		case 3:
		case 5:
			if (fsfx_is_voice_queue_empty()) {
				selected_response =
					fsfx_select_available_voice_variant(
						voice_category,
						target_craft_ordinal);
				if (selected_response == -1) {
					return 0;
				}
				if (craft_index_in_group != 0) {
					g_fsfx_voice_line_play_counts
						[selected_response +
						 97 * target_craft_ordinal +
						 base_offset]++;
					fsfx_queue_voice_sfx(
						g_fsfx_voice_category_base_offset
								[2] +
							craft_index_in_group +
							speaker_voice_list_slot -
							1,
						1, 2, 0, object_signature);
				}
				fsfx_queue_voice_sfx(
					base_offset + speaker_voice_list_slot +
						selected_response,
					1, voice_category, 1, object_signature);
				break;
			}
			return 0;
		case 6:
			if (fsfx_is_voice_queue_empty()) {
				selected_response =
					fsfx_select_available_voice_variant(
						voice_category,
						target_craft_ordinal);
				if (selected_response == -1) {
					return 0;
				}
				g_fsfx_voice_line_play_counts
					[selected_response +
					 97 * target_craft_ordinal +
					 base_offset]++;
				fsfx_queue_voice_sfx(
					base_offset + speaker_voice_list_slot +
						selected_response,
					1, voice_category, 0, object_signature);
				break;
			}
			return 0;
		case 9:
		case 10:
		case 21:
			selected_response = fsfx_select_available_voice_variant(
				voice_category, target_craft_ordinal);
			if (selected_response == -1) {
				return 0;
			}
			g_fsfx_voice_line_play_counts
				[selected_response + 97 * target_craft_ordinal +
				 base_offset]++;
			fsfx_queue_voice_sfx(
				base_offset + speaker_voice_list_slot +
					selected_response,
				1, voice_category, 0, object_signature);
			break;
		case 12:
			selected_response = fsfx_random_index(4);
			fsfx_queue_voice_sfx(
				base_offset + speaker_voice_list_slot +
					selected_response,
				1, voice_category, 0, object_signature);
			break;
		case 16:
			selected_response = fsfx_select_available_voice_variant(
				voice_category, target_craft_ordinal);
			if (selected_response == -1) {
				return 0;
			}
			g_fsfx_voice_line_play_counts
				[selected_response + 97 * target_craft_ordinal +
				 base_offset]++;
			fsfx_queue_voice_sfx(
				base_offset + speaker_voice_list_slot +
					selected_response,
				1, voice_category, 0, object_signature);
			fsfx_queue_voice_sfx(
				g_fsfx_voice_category_base_offset[17] +
					target_craft_ordinal +
					speaker_voice_list_slot,
				1, 17, 1, object_signature);
			break;
		default:
			break;
		}
	} else {
		switch (voice_category) {
		case 1:
			if (craft_index_in_group != 0) {
				fsfx_queue_voice_sfx(
					g_fsfx_voice_category_base_offset[0] +
						craft_index_in_group +
						speaker_voice_list_slot - 1,
					1, 0, 0, object_signature);
			}
			if (game_rand2() < 0x5555) {
				fsfx_queue_voice_sfx(
					g_fsfx_voice_category_base_offset[1] +
						speaker_voice_list_slot,
					1, 1, 1, object_signature);
			}
			if (g_fsfx_voice_category_variant_count
				    [voice_category] > selected_response) {
				fsfx_queue_voice_sfx(
					base_offset + speaker_voice_list_slot +
						selected_response,
					1, voice_category, 2, object_signature);
				break;
			}
			return 0;
		case 23:
			if (game_rand2() < 0x8000) {
				int voice_variant = fsfx_random_index(
					g_fsfx_voice_category_variant_count
						[voice_category]);
				fsfx_queue_voice_sfx(
					g_fsfx_voice_category_base_offset[21] +
						speaker_voice_list_slot +
						voice_variant,
					1, 21, 0, object_signature);
			}
			if (g_fsfx_voice_category_variant_count
				    [voice_category] > selected_response) {
				fsfx_queue_voice_sfx(
					speaker_voice_list_slot +
						selected_response + base_offset,
					1, voice_category, 1, object_signature);
				g_fsfx_voice_line_play_counts
					[97 * craft_ordinal +
					 selected_response + base_offset]++;
				break;
			}
			return 0;
		default:
			if (g_fsfx_voice_category_variant_count
				    [voice_category] > selected_response) {
				fsfx_queue_voice_sfx(
					base_offset + speaker_voice_list_slot +
						selected_response,
					1, voice_category, 0, object_signature);
				break;
			}
			return 0;
		}
	}
	return 1;
}

/* Queues a tactical officer line, speaker type 2, at voice slot 696 plus the
 * id. Returns 0 when the tactical officer is off, the local player has no
 * craft, or probability is not 0xFFFF and game_rand2() is not under it. Category
 * 1 (status) also returns 0 when obj_idx is -1 or not below
 * g_active_region_craft_object_slot_end, or when
 * g_fsfx_designation_to_tactical_message_id gives 0xFF for the object's flight
 * group's designation code on the player's team; except for messages 27 and 28
 * (destroyed, disabled) it returns 0 when the officer spoke of that object at
 * most 10 mission seconds ago, and otherwise stores the time in
 * g_fsfx_tac_officer_last_speak_seconds_by_obj. Designation lines 4 and 5 become 10
 * and 11 for a group of another team. It queues the designation line, then
 * messageId's line chained, both with the object's signature. Categories 2 to 6
 * queue messageId's line alone with signature 0xFFFF; any other category queues
 * nothing. Returns 1 in those cases. */
// FUNCTION: XVT 0x430200
int fsfx_speak_tactical_officer_event(int voice_category, int message_id,
				      int obj_idx, uint16_t probability)
{
	if (g_game_config.voice_tactical_officer_level == 0) {
		return 0;
	}
	if (g_players[g_local_player].object_index == -1) {
		return 0;
	}
	if (probability != UINT16_MAX && game_rand2() >= probability) {
		return 0;
	}

	int16_t object_signature = -1;
	int designation_message_id;
	if (voice_category == TACTICAL_VOICE_STATUS) {
		if (obj_idx == -1 ||
		    obj_idx >= g_active_region_craft_object_slot_end) {
			return 0;
		}
		designation_message_id =
			g_fsfx_designation_to_tactical_message_id
				[g_flight_mission_state.runtime
					 .team_fg_designation_code
						 [(uint16_t)g_players
							  [g_local_player]
								  .team]
						 [g_object_table[obj_idx]
							  .flight_group_idx]];
		if (designation_message_id == 0xFF) {
			return 0;
		}
		object_signature = g_object_table[obj_idx].object_signature;
		int elapsed_seconds = mission_clock_to_seconds(
			g_mission_elapsed_clock.hours,
			g_mission_elapsed_clock.minutes,
			g_mission_elapsed_clock.seconds);
		if (message_id != TACTICAL_MSG_DESTROYED &&
		    message_id != TACTICAL_MSG_DISABLED) {
			int last_speak_seconds =
				g_fsfx_tac_officer_last_speak_seconds_by_obj
					[obj_idx];
			if (last_speak_seconds != 0 &&
			    (unsigned int)(elapsed_seconds -
					   last_speak_seconds) <= 10) {
				return 0;
			}
			g_fsfx_tac_officer_last_speak_seconds_by_obj[obj_idx] =
				elapsed_seconds;
		}
		if ((designation_message_id == 4 ||
		     designation_message_id == 5) &&
		    g_mission_flight_groups[g_object_table[obj_idx]
						    .flight_group_idx]
				    .fg.team !=
			    (uint16_t)g_players[g_local_player].team) {
			if (designation_message_id == 4) {
				designation_message_id = 10;
			} else {
				designation_message_id = 11;
			}
		}
	}

	switch (voice_category) {
	case TACTICAL_VOICE_STATUS:
		fsfx_queue_voice_sfx(designation_message_id + 696,
				     FLIGHT_VOICE_SPEAKER_TACTICAL,
				     voice_category, 0, object_signature);
		fsfx_queue_voice_sfx(message_id + 696,
				     FLIGHT_VOICE_SPEAKER_TACTICAL,
				     voice_category, 1, object_signature);
		break;
	case TACTICAL_VOICE_ORDER:
	case 3:
	case 4:
	case 5:
	case 6:
		fsfx_queue_voice_sfx(message_id + 696,
				     FLIGHT_VOICE_SPEAKER_TACTICAL,
				     voice_category, 0, object_signature);
		break;
	}
	return 1;
}

/* Queues a random line of commander voice category voice_category, speaker type
 * 3, signature 0xFFFF: voice slot 804 plus the category's offset plus an index
 * below its count (0 when the count is 0). Returns 1, or 0 when the commander
 * voice is off. A category over 9 queues nothing and still returns 1, after
 * reading the offset table past its 10 entries. object_signature is ignored. */
// FUNCTION: XVT 0x430420
int fsfx_queue_commander_voice_category(int voice_category,
					int object_signature)
{
	(void)object_signature;
	if (g_game_config.voice_commander_enabled == 0) {
		return 0;
	}
	int base_offset =
		g_commander_voice_sfx_offset_by_category[voice_category];
	uint8_t variant_count;
	uint16_t variant_index;
	switch (voice_category) {
	case 0:
		variant_count = g_commander_voice_variant_count_by_category
			[voice_category];
		if (variant_count != 0) {
			variant_index = fsfx_random_index(variant_count);
		} else {
			variant_index = 0;
		}
		fsfx_queue_voice_sfx(base_offset + variant_index + 804, 3,
				     voice_category, 0, -1);
		return 1;
	case 1:
		variant_count = g_commander_voice_variant_count_by_category
			[voice_category];
		if (variant_count != 0) {
			variant_index = fsfx_random_index(variant_count);
		} else {
			variant_index = 0;
		}
		fsfx_queue_voice_sfx(base_offset + variant_index + 804, 3,
				     voice_category, 0, -1);
		return 1;
	case 2:
	case 3:
	case 4:
	case 5:
	case 6:
	case 7:
	case 8:
	case 9:
		variant_count = g_commander_voice_variant_count_by_category
			[voice_category];
		if (variant_count != 0) {
			variant_index = fsfx_random_index(variant_count);
		} else {
			variant_index = 0;
		}
		fsfx_queue_voice_sfx(base_offset + variant_index + 804, 3,
				     voice_category, 0, -1);
		break;
	default:
		break;
	}
	return 1;
}

/* Picks a line of a wingman voice category for a craft ordinal: a random index
 * below the category's variant count from fsfx_random_index. With a repeat
 * threshold of 0 it returns that index. Otherwise, when the line's count in
 * g_fsfx_voice_line_play_counts (list 97 * craft_ordinal) has reached the threshold,
 * it steps on through the indexes, wrapping, and returns the first under it, or
 * -1 when every line has reached it. Writes nothing. */
// FUNCTION: XVT 0x430540
int fsfx_select_available_voice_variant(int voice_category, int craft_ordinal)
{
	int variant_count = g_fsfx_voice_category_variant_count[voice_category];
	int base_offset = g_fsfx_voice_category_base_offset[voice_category];
	int variant_index = fsfx_random_index(variant_count);
	uint8_t repeat_threshold =
		g_fsfx_voice_category_repeat_threshold[voice_category];
	if (repeat_threshold != 0) {
		int craft_list_offset = craft_ordinal * 97;
		if (g_fsfx_voice_line_play_counts[craft_list_offset +
						  variant_index +
						  base_offset] >=
		    repeat_threshold) {
			int remaining_variants = variant_count;
			while (remaining_variants-- != 0) {
				++variant_index;
				if (variant_index >= variant_count) {
					variant_index = 0;
				}
				if (g_fsfx_voice_line_play_counts
					    [craft_list_offset + variant_index +
					     base_offset] < repeat_threshold) {
					return variant_index;
				}
			}
			return -1;
		}
	}

	return variant_index;
}

/* Returns an index below count: r % q, where r is a game_rand2() value modulo
 * count and q that value divided by count, so r itself whenever q exceeds r; 0
 * when q is 0. Divides by count without checking it for 0. */
// FUNCTION: XVT 0x4305D0
uint16_t fsfx_random_index(uint16_t count)
{
	uint16_t random_value = game_rand2();
	uint16_t quotient = random_value / count;
	if (quotient == 0) {
		return 0;
	}

	return random_value % quotient;
}

/* Returns 1 when g_fsfx_voice_queue_count is 0, else 0. */
// FUNCTION: XVT 0x430600
int fsfx_is_voice_queue_empty(void) { return g_fsfx_voice_queue_count == 0; }

/* Appends a voice line to the voice queue, the five g_fsfxVoiceQueue arrays,
 * and returns 1. Returns 0, queuing nothing, when
 * g_flight_sim_side_effects_suppressed is set, g_flight_conf_voice_enabled or the
 * voice volume is 0, the slot holds 0 in g_fsfx_loaded_by_slot, the queue holds
 * 128, or the line starts a tactical status message (speaker 2, category 1,
 * chain_flag 0) about the same object signature as the last queued line, itself
 * a tactical status line. Does not check sfx_slot against 838. */
// FUNCTION: XVT 0x430610
int fsfx_queue_voice_sfx(int sfx_slot, char speaker_type, char voice_category,
			 char chain_flag, uint16_t object_signature)
{
	if (g_flight_sim_side_effects_suppressed != 0) {
		return 0;
	}
	if (g_flight_conf_voice_enabled == 0) {
		return 0;
	}
	if (g_fsfx_loaded_by_slot[sfx_slot] == 0) {
		return 0;
	}
	if (g_game_config.voice_volume == 0) {
		return 0;
	}
	uint8_t queue_count = g_fsfx_voice_queue_count;
	if (queue_count == 128) {
		return 0;
	}
	if (queue_count != 0 && speaker_type == FLIGHT_VOICE_SPEAKER_TACTICAL &&
	    voice_category == TACTICAL_VOICE_STATUS && chain_flag == 0 &&
	    g_fsfx_voice_queue_speaker_type[queue_count - 1] ==
		    FLIGHT_VOICE_SPEAKER_TACTICAL &&
	    g_fsfx_voice_queue_category[queue_count - 1] ==
		    TACTICAL_VOICE_STATUS &&
	    g_fsfx_voice_queue_object_signature[queue_count - 1] ==
		    object_signature) {
		return 0;
	}

	int queue_index = g_fsfx_voice_queue_count;
	g_fsfx_voice_queue_object_signature[queue_index] = object_signature;
	g_fsfx_voice_queue_sfx_slot[queue_index] = sfx_slot;
	g_fsfx_voice_queue_speaker_type[queue_index] = speaker_type;
	g_fsfx_voice_queue_category[queue_index] = voice_category;
	g_fsfx_voice_queue_chain_flag[queue_index] = chain_flag;
	g_fsfx_voice_queue_count = queue_count + 1;
	return 1;
}

/* Starts the next voice line once the last one ends. Does nothing when
 * g_fsfx_loaded is 0. Prunes the queue with fsfx_prune_stale_voice_queue_entries,
 * then returns while the line in g_fsfx_current_voice_sfx_slot still plays.
 * Otherwise it sets g_fsfx_current_voice_sfx_slot to 0 and, with a line queued,
 * takes the first, copying its speaker type, category, chain flag and signature
 * into the g_fsfxCurrentVoice globals, moves the rest up one and lowers
 * g_fsfx_voice_queue_count; when that line's sound loaded and the voice volume is
 * not 0, it plays it at once with sound_play_effect_now (restart allowed, once,
 * priority 126, centered) at the voice volume setting times 13, 127 from 10 up,
 * and stores its id in g_fsfx_current_voice_sfx_slot. */
// FUNCTION: XVT 0x430700
void fsfx_update_voice_queue(void)
{
	if (g_fsfx_loaded == 0) {
		return;
	}
	fsfx_prune_stale_voice_queue_entries();
	if (g_fsfx_current_voice_sfx_slot != 0 &&
	    sound_get_param(g_fsfx_current_voice_sfx_slot, 256) != 0) {
		return;
	}

	unsigned int queue_index = 0;
	uint8_t queue_count = g_fsfx_voice_queue_count;
	g_fsfx_current_voice_sfx_slot = 0;
	if (queue_count == 0) {
		return;
	}

	int sfx_slot = g_fsfx_voice_queue_sfx_slot[0];
	g_fsfx_current_voice_speaker_type = g_fsfx_voice_queue_speaker_type[0];
	--queue_count;
	g_fsfx_current_voice_category = g_fsfx_voice_queue_category[0];
	g_fsfx_voice_queue_count = queue_count;
	g_fsfx_current_voice_chain_flag = g_fsfx_voice_queue_chain_flag[0];
	g_fsfx_current_voice_object_signature =
		g_fsfx_voice_queue_object_signature[0];

	while (queue_index < queue_count) {
		g_fsfx_voice_queue_sfx_slot[queue_index] =
			g_fsfx_voice_queue_sfx_slot[queue_index + 1];
		g_fsfx_voice_queue_speaker_type[queue_index] =
			g_fsfx_voice_queue_speaker_type[queue_index + 1];
		g_fsfx_voice_queue_category[queue_index] =
			g_fsfx_voice_queue_category[queue_index + 1];
		g_fsfx_voice_queue_chain_flag[queue_index] =
			g_fsfx_voice_queue_chain_flag[queue_index + 1];
		g_fsfx_voice_queue_object_signature[queue_index] =
			g_fsfx_voice_queue_object_signature[queue_index + 1];
		++queue_index;
	}

	if (g_fsfx_loaded_by_slot[sfx_slot] == 0 ||
	    g_game_config.voice_volume == 0) {
		return;
	}
	int volume = 127;
	if (g_game_config.voice_volume < 10) {
		volume = 13 * g_game_config.voice_volume;
	}
	sound_play_effect_now(g_fsfx_sfx_name_table[sfx_slot], 1, 0, 126,
			      volume, 64);
	g_fsfx_current_voice_sfx_slot = sfx_slot;
}

/* Drops queued tactical status messages about objects that are gone. Each
 * queued line that starts one (chain_flag 0, speaker 2, category 1) and whose
 * next line's id is not 723 or 732 is kept only while a craft slot from
 * g_active_region_object_slot_start below g_active_region_craft_object_slot_end holds an
 * object with its signature that has a mobile object and craft and is not
 * breaking up or exploding; otherwise fsfx_remove_voice_queue_entry_chain removes
 * it with its chained lines and the same index is looked at again. When the
 * 128th queued line starts such a message it reads one entry past the end of
 * g_fsfx_voice_queue_sfx_slot. */
// FUNCTION: XVT 0x430830
void fsfx_prune_stale_voice_queue_entries(void)
{
	unsigned int queue_index = 0;
	if (g_fsfx_voice_queue_count == 0) {
		return;
	}
	while (g_fsfx_voice_queue_count > queue_index) {
		if (g_fsfx_voice_queue_chain_flag[queue_index] != 0 ||
		    g_fsfx_voice_queue_speaker_type[queue_index] !=
			    FLIGHT_VOICE_SPEAKER_TACTICAL ||
		    g_fsfx_voice_queue_category[queue_index] !=
			    TACTICAL_VOICE_STATUS ||
		    g_fsfx_voice_queue_sfx_slot[queue_index + 1] == 723 ||
		    g_fsfx_voice_queue_sfx_slot[queue_index + 1] == 732) {
			++queue_index;
			continue;
		}

		unsigned int object_index = g_active_region_object_slot_start;
		unsigned int object_slot_end =
			g_active_region_craft_object_slot_end;
		int referenced_object_found = 0;
		if (object_index < object_slot_end) {
			do {
				if (g_object_table[object_index]
						    .object_signature ==
					    (uint16_t)
						    g_fsfx_voice_queue_object_signature
							    [queue_index] &&
				    g_object_table[object_index].object_type !=
					    0 &&
				    g_object_table[object_index].mobj != NULL &&
				    g_object_table[object_index]
						    .mobj->p_craft != NULL &&
				    g_object_table[object_index]
						    .mobj->p_craft
						    ->object_kind !=
					    CRAFT_OBJECT_KIND_BREAKING_UP &&
				    g_object_table[object_index]
						    .mobj->p_craft
						    ->object_kind !=
					    CRAFT_OBJECT_KIND_EXPLODING) {
					referenced_object_found = 1;
					break;
				}
				++object_index;
			} while (object_index < object_slot_end);
		}

		if (referenced_object_found != 0) {
			++queue_index;
		} else {
			fsfx_remove_voice_queue_entry_chain(queue_index);
		}
	}
}

/* Removes queued voice line queueIndex and the lines chained after it (each
 * following line with a nonzero chain flag, up to the first with 0), moves the
 * later lines up and lowers g_fsfx_voice_queue_count by the number removed. Does
 * not check that queueIndex is queued. */
// FUNCTION: XVT 0x430930
void fsfx_remove_voice_queue_entry_chain(unsigned int queue_index)
{
	int removed_count = 1;
	unsigned int destination_index = queue_index;
	uint8_t chain_flag = g_fsfx_voice_queue_chain_flag[queue_index + 1];
	unsigned int scan_index = queue_index + 1;
	if (chain_flag != 0) {
		while (scan_index < g_fsfx_voice_queue_count) {
			chain_flag =
				g_fsfx_voice_queue_chain_flag[scan_index + 1];
			++removed_count;
			++scan_index;
			if (chain_flag == 0) {
				break;
			}
		}
	}

	/* From here chain_flag holds the shrunken queue count, which also ends the copy loop below. */
	chain_flag = g_fsfx_voice_queue_count;
	chain_flag -= (uint8_t)removed_count;
	g_fsfx_voice_queue_count = chain_flag;
	if (queue_index >= chain_flag) {
		return;
	}

	unsigned int source_index = queue_index + removed_count;
	uint16_t *destination_signature =
		&g_fsfx_voice_queue_object_signature[queue_index];
	uint16_t *source_signature =
		&g_fsfx_voice_queue_object_signature[source_index];
	int *source_sfx_slot = &g_fsfx_voice_queue_sfx_slot[source_index];
	int *destination_sfx_slot = &g_fsfx_voice_queue_sfx_slot[queue_index];
	while (1) {
		*destination_sfx_slot++ = *source_sfx_slot++;
		g_fsfx_voice_queue_speaker_type[destination_index] =
			g_fsfx_voice_queue_speaker_type[destination_index +
							removed_count];
		g_fsfx_voice_queue_category[destination_index] =
			g_fsfx_voice_queue_category[destination_index +
						    removed_count];
		g_fsfx_voice_queue_chain_flag[destination_index] =
			g_fsfx_voice_queue_chain_flag[destination_index +
						      removed_count];
		*destination_signature++ = *source_signature++;
		++destination_index;
		if (destination_index >= chain_flag) {
			break;
		}
	}
}
