#include "xvt/frontend/pilot.h"

#include "xvt/assets/file.h"
#include "xvt/frontend/concourse.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/frontend.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/frontend_file_list.h"
#include "xvt/frontend/frontend_mission.h"
#include "xvt/frontend/frontend_string.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt/net/net.h"
#include "xvt/util/time.h"
#include "xvt_runtime/log/log_both_builds.h"

#ifdef XVT_MODERN
#include <strings.h>

#include "xvt_runtime/compat/pilot_port.h"
#endif

#include <stdlib.h>
#include <string.h>

/* Forty pilot names; pilot_parse_command_line picks one at random for a pilot
 * named "joiner" or "host" on the command line. */
// GLOBAL: XVT 0x52AF78
static const char *const g_random_pilot_names[40] = {
	"Luke",	       "Han Solo",    "Darth Vader", "Leia",	     "Lando",
	"Boba Fett",   "Chewbacca",   "R2-D2",	     "C-3PO",	     "Jabba",
	"Greedo",      "Thrawn",      "Ackbar",	     "Wedge",	     "Bollux",
	"Blue Max",    "Bossk",	      "C'Baoth",     "Palpatine",    "Biggs",
	"Dodonna",     "Bib Fortuna", "Mara Jade",   "Talon Karrde", "Obi-Wan",
	"Lobot",       "Crix Madine", "Mon Mothma",  "Nien Nunb",    "Pellaeon",
	"Anakin Solo", "Jacen Solo",  "Jaina Solo",  "Tarkin",	     "Yoda",
	"Zaarin",      "Harkov",      "Tarrak",	     "Xizor",	     "Guri",
};

/* Deletes the selected pilot's files and clears g_pilot_data. Finds the pilot's
 * name in g_pilot_list_display_names, ignoring case, and deletes that entry's .plt
 * file in the base game folder and the file of the same name ending in '2'
 * (.pl2) in the install folder. Then clears g_pilot_data and rebuilds the pilot
 * list. Returns 1; the modern build returns 0, leaving g_pilot_data as it was,
 * when a file cannot be removed. Also zeroes a 253,754-byte local record that
 * it never uses. */
// FUNCTION: XVT 0x4BF260
int pilot_delete_current(void)
{
	int selected_index;

	if (g_pilot_list_display_names != NULL &&
	    g_pilot_data.name[0] != '\0' && g_pilot_file_list != NULL) {
		struct frontend_file_list_node *node = g_pilot_file_list->head;
		selected_index = 0;
		if (g_pilot_list_display_names != NULL) {
			while (g_pilot_file_list->count > selected_index) {
#ifdef XVT_MODERN
				if (strcasecmp(g_pilot_list_display_names
						       [selected_index],
					       g_pilot_data.name) == 0) {
#else
				if (_strcmpi(g_pilot_list_display_names
						     [selected_index],
					     g_pilot_data.name) == 0) {
#endif
					file_change_to_base_game_install_path();
					XVT_LOG_DEBUG(
						"pilot.delete_match index=%d path=\"%s\"",
						selected_index, node->path);
#ifdef XVT_MODERN
					if (!pilot_remove_file_modern(
						    node->path)) {
						XVT_LOG_ERROR(
							"pilot.delete_failed kind=\"record\" index=%d",
							selected_index);
						return 0;
					}
#else
					FILE_REMOVE(node->path);
#endif
					file_change_to_install_path();
					strcpy(g_frontend_scratch_buffer,
					       node->path);
					g_frontend_scratch_buffer
						[strlen(g_frontend_scratch_buffer) -
						 1] = '2';
#ifdef XVT_MODERN
					if (!pilot_remove_file_modern(
						    g_frontend_scratch_buffer)) {
						XVT_LOG_ERROR(
							"pilot.delete_failed kind=\"expansion\" index=%d",
							selected_index);
						return 0;
					}
#else
					FILE_REMOVE(g_frontend_scratch_buffer);
#endif
					XVT_LOG_INFO("pilot.deleted index=%d",
						     selected_index);
					break;
				}
				node = node->next;
				++selected_index;
				if (g_pilot_list_display_names
					    [selected_index] == NULL) {
					break;
				}
			}
			if (selected_index >= g_pilot_file_list->count) {
				XVT_LOG_WARN("pilot.delete_unlisted count=%d",
					     g_pilot_file_list->count);
			}
		}
	}

	memset(&g_pilot_data, 0, sizeof(g_pilot_data));
	uint8_t xvt_pilot_record[0x3DF3A];
	memset(xvt_pilot_record, 0, sizeof(xvt_pilot_record));
	selected_index = 0;
	pilot_record_rebuild_pilot_list(&selected_index);
	return 1;
}

/* Creates a pilot named pilot_name and makes it the current one. Picks the first
 * NAMEn.pl2 that does not exist, counting n from 0; the original build opens it
 * for writing and returns 0 when it cannot. Clears g_pilot_data and sets the
 * name, the Trainee rating, and the game and host names (the name with
 * FRONTSTR_470_S_GAME after it). The first mission of the training exercises
 * list becomes the choice of the pilot and of faction record 0, that of the
 * list loaded for faction 1 record 1's, and that of the multiplayer list record
 * 2's. Writes g_pilot_data to the file, then sets combat engagement choices (the
 * list's third mission for the pilot, the first for factions 0 and 1) and saves
 * again with pilot_save(0). Returns 1 in the original build, or pilot_save's
 * result in the modern one, which also returns 0 when the first write fails.
 * Leaves g_frontend_mission_session_mode at none. Checks neither the name's length
 * nor that the combat list holds three missions. */
// FUNCTION: XVT 0x4BF3A0
int pilot_create_new(const char *pilot_name)
{
#ifndef XVT_MODERN
	xvt_file *stream;
#endif
	int file_index = 0;
	char pilot_path[32];
	xvt_file *probe_stream;
	for (;;) {
		sprintf(pilot_path, "%s%d.pl2", pilot_name, file_index);
#ifdef XVT_MODERN
		probe_stream = file_open(pilot_path, g_file_mode_read_binary);
#else
		probe_stream =
			FILE_RAW_OPEN(pilot_path, g_file_mode_read_binary);
#endif
		if (probe_stream == NULL) {
			break;
		}
#ifdef XVT_MODERN
		file_close(probe_stream);
#else
		FILE_RAW_CLOSE(probe_stream);
#endif
		++file_index;
	}
	XVT_LOG_DEBUG("pilot.create_file number=%d path=\"%s\"", file_index,
		      pilot_path);

#ifndef XVT_MODERN
	stream = file_open(pilot_path, "wb");
	if (stream == NULL) {
		return 0;
	}
#endif

	memset(&g_pilot_data, 0, sizeof(g_pilot_data));
	strcpy(g_pilot_data.name, pilot_name);
	g_pilot_data.rating = PILOT_RATING_TRAINEE;
	strcpy(g_pilot_data.rating_name,
	       frontend_string_get(FRONTSTR_124_TRAINEE));
	sprintf(g_pilot_data.multiplayer_game_name, "%s%s", pilot_name,
		frontend_string_get(FRONTSTR_470_S_GAME));
	strcpy(g_pilot_data.multiplayer_host_name,
	       g_pilot_data.multiplayer_game_name);

	mission_setup_load_mission_list(MISSION_DIRECTORY_TRAINING_EXERCISES);
	if (g_mission_list != NULL) {
		g_pilot_data.mission_description_ids
			[MISSION_DIRECTORY_TRAINING_EXERCISES] =
			g_mission_list[0].mission_idx;
		g_pilot_data.faction_statistics[0].mission_description_ids
			[MISSION_DIRECTORY_TRAINING_EXERCISES] =
			g_mission_list[0].mission_idx;
		free(g_mission_list);
		g_mission_list = NULL;
	}

	g_pilot_data.current_faction_id = 1;
	mission_setup_load_mission_list(MISSION_DIRECTORY_TRAINING_EXERCISES);
	g_pilot_data.current_faction_id = 0;
	if (g_mission_list != NULL) {
		g_pilot_data.faction_statistics[1].mission_description_ids
			[MISSION_DIRECTORY_TRAINING_EXERCISES] =
			g_mission_list[0].mission_idx;
		free(g_mission_list);
		g_mission_list = NULL;
	}

	g_frontend_mission_session_mode = FRONTEND_MISSION_SESSION_NET_HOST;
	mission_setup_load_mission_list(MISSION_DIRECTORY_TRAINING_EXERCISES);
	if (g_mission_list != NULL) {
		g_pilot_data.faction_statistics[2].mission_description_ids
			[MISSION_DIRECTORY_TRAINING_EXERCISES] =
			g_mission_list[0].mission_idx;
		free(g_mission_list);
		g_mission_list = NULL;
	}
	g_frontend_mission_session_mode = FRONTEND_MISSION_SESSION_NONE;
#ifdef XVT_MODERN
	if (!xvt_storage_write_atomic(pilot_path, &g_pilot_data,
				      sizeof(g_pilot_data))) {
		return 0;
	}
#else
	file_write_bytes(stream, &g_pilot_data, sizeof(g_pilot_data));
#endif

	mission_setup_load_mission_list(MISSION_DIRECTORY_COMBAT_ENGAGEMENTS);
	if (g_mission_list != NULL && g_mission_count < 3) {
		XVT_LOG_WARN("pilot.create_combat_short count=%u",
			     g_mission_count);
	}
	if (g_mission_list != NULL) {
		g_pilot_data.mission_description_ids
			[MISSION_DIRECTORY_COMBAT_ENGAGEMENTS] =
			g_mission_list[2].mission_idx;
		g_pilot_data.faction_statistics[0].mission_description_ids
			[MISSION_DIRECTORY_COMBAT_ENGAGEMENTS] =
			g_mission_list[0].mission_idx;
		free(g_mission_list);
		g_mission_list = NULL;
	}

	g_pilot_data.current_faction_id = 1;
	mission_setup_load_mission_list(MISSION_DIRECTORY_COMBAT_ENGAGEMENTS);
	g_pilot_data.current_faction_id = 0;
	if (g_mission_list != NULL) {
		g_pilot_data.faction_statistics[1].mission_description_ids
			[MISSION_DIRECTORY_COMBAT_ENGAGEMENTS] =
			g_mission_list[0].mission_idx;
		free(g_mission_list);
		g_mission_list = NULL;
	}
	XVT_LOG_DEBUG(
		"pilot.create_choices training=\"%d,%d,%d,%d\" combat=\"%d,%d,%d\"",
		(int)g_pilot_data.mission_description_ids
			[MISSION_DIRECTORY_TRAINING_EXERCISES],
		g_pilot_data.faction_statistics[0].mission_description_ids
			[MISSION_DIRECTORY_TRAINING_EXERCISES],
		g_pilot_data.faction_statistics[1].mission_description_ids
			[MISSION_DIRECTORY_TRAINING_EXERCISES],
		g_pilot_data.faction_statistics[2].mission_description_ids
			[MISSION_DIRECTORY_TRAINING_EXERCISES],
		(int)g_pilot_data.mission_description_ids
			[MISSION_DIRECTORY_COMBAT_ENGAGEMENTS],
		g_pilot_data.faction_statistics[0].mission_description_ids
			[MISSION_DIRECTORY_COMBAT_ENGAGEMENTS],
		g_pilot_data.faction_statistics[1].mission_description_ids
			[MISSION_DIRECTORY_COMBAT_ENGAGEMENTS]);

#ifndef XVT_MODERN
	file_close(stream);
#endif
	XVT_LOG_INFO("pilot.created number=%d", file_index);
#ifdef XVT_MODERN
	return pilot_save(0);
#else
	pilot_save(0);
	return 1;
#endif
}

/* Saves g_pilot_data to the pilot's .pl2 file and the base game's record to its
 * .plt file. Returns 0 without a pilot name. With use_temporary_file 0, the .pl2
 * is the *.pl2 file in the current folder whose first 14 bytes hold the pilot's
 * name, ignoring case; otherwise "__temp__.tmp". With none found, it is the
 * name of the matching *.plt in the base game folder with its last letter made
 * '2', or else NAME0.pl2. The original build returns 0 when the file does not
 * open; the modern build writes it whole or returns 0. Then writes the record
 * with pilot_write_xvt_record to the same name with its last letter made 't'.
 * Returns 1 in the original build, ignoring that write, or
 * pilot_write_xvt_record's result in the modern one. Every caller passes 0. */
// FUNCTION: XVT 0x4BF650
int pilot_save(int use_temporary_file)
{
	if (g_pilot_data.name[0] == '\0') {
		XVT_LOG_WARN("pilot.save_no_pilot");
		return 0;
	}

	char pilot_path[30];
	memset(pilot_path, 0, sizeof(pilot_path));
	struct frontend_file_list *file_list;
	struct frontend_file_list_node *node;
	xvt_file *stream;
	int file_index;
	if (use_temporary_file == 0) {
		file_list = frontend_file_list_build_sorted("*.pl2");
		if (file_list != NULL) {
			node = file_list->head;
			file_index = 0;
			while (file_list->count > file_index) {
				stream = file_open(node->path,
						   g_file_mode_read_binary);
				if (stream != NULL) {
					file_read_bytes(
						stream,
						g_frontend_scratch_buffer,
						sizeof(g_pilot_data.name));
					file_close(stream);
#ifdef XVT_MODERN
					if (strcasecmp(
						    g_frontend_scratch_buffer,
						    g_pilot_data.name) == 0) {
						snprintf(pilot_path,
							 sizeof(pilot_path),
							 "%s", node->path);
#else
					if (_strcmpi(g_frontend_scratch_buffer,
						     g_pilot_data.name) == 0) {
						strcpy(pilot_path, node->path);
#endif
						break;
					}
				}
				node = node->next;
				++file_index;
			}
			frontend_file_list_free(file_list);
		} else {
			XVT_LOG_WARN(
				"pilot.list_failed by=\"save\" kind=\"expansion\"");
		}
	} else {
		strcpy(pilot_path, "__temp__.tmp");
	}

	if (pilot_path[0] == '\0') {
		file_change_to_base_game_install_path();
		file_list = frontend_file_list_build_sorted("*.plt");
		file_change_to_install_path();
		if (file_list != NULL) {
			node = file_list->head;
			file_index = 0;
			while (file_list->count > file_index) {
				stream = file_open(node->path,
						   g_file_mode_read_binary);
				if (stream != NULL) {
					file_read_bytes(
						stream,
						g_frontend_scratch_buffer,
						sizeof(g_pilot_data.name));
					file_close(stream);
#ifdef XVT_MODERN
					if (strcasecmp(
						    g_frontend_scratch_buffer,
						    g_pilot_data.name) == 0) {
						snprintf(pilot_path,
							 sizeof(pilot_path),
							 "%s", node->path);
#else
					if (_strcmpi(g_frontend_scratch_buffer,
						     g_pilot_data.name) == 0) {
						strcpy(pilot_path, node->path);
#endif
						break;
					}
				}
				node = node->next;
				++file_index;
			}
			frontend_file_list_free(file_list);
		} else {
			XVT_LOG_WARN(
				"pilot.list_failed by=\"save\" kind=\"record\"");
		}

		if (pilot_path[0] == '\0') {
#ifdef XVT_MODERN
			snprintf(pilot_path, sizeof(pilot_path), "%s0.pl2",
				 g_pilot_data.name);
#else
			sprintf(pilot_path, "%s0.pl2", g_pilot_data.name);
#endif
		} else {
			pilot_path[strlen(pilot_path) - 1] = '2';
		}
	}
	XVT_LOG_DEBUG("pilot.save_path path=\"%s\" temporary=%d", pilot_path,
		      use_temporary_file);

#ifndef XVT_MODERN
	stream = file_open(pilot_path, "wb");
	if (stream == NULL) {
		return 0;
	}
#endif
#ifdef XVT_MODERN
	if (!xvt_storage_write_atomic(pilot_path, &g_pilot_data,
				      sizeof(g_pilot_data))) {
		return 0;
	}
#else
	file_write_bytes(stream, &g_pilot_data, sizeof(g_pilot_data));
#endif
#ifndef XVT_MODERN
	file_close(stream);
#endif
	XVT_LOG_INFO("pilot.saved rating=%d missions=%d score=%d faction=%d",
		     (int)g_pilot_data.rating,
		     g_pilot_data.total_missions_played_count,
		     g_pilot_data.total_score, g_pilot_data.current_faction_id);
	if (pilot_path[0] == '\0') {
#ifdef XVT_MODERN
		snprintf(pilot_path, sizeof(pilot_path), "%s0.plt",
			 g_pilot_data.name);
#else
		sprintf(pilot_path, "%s0.plt", g_pilot_data.name);
#endif
	} else {
		pilot_path[strlen(pilot_path) - 1] = 't';
	}
#ifdef XVT_MODERN
	return pilot_write_xvt_record(pilot_path, NULL);
#else
	pilot_write_xvt_record(pilot_path, stream);
#endif
	return 1;
}

/* Loads the pilot whose .plt file in the base game folder holds pilot_name in
 * its first 14 bytes, ignoring case, through pilot_load_from_path; the first
 * match decides. Returns 1 when it loaded, 0 when the file list cannot be
 * built, pilot_name is NULL or empty, nothing matches, or the load fails. */
// FUNCTION: XVT 0x4BF8C0
int pilot_find_and_load_by_name(const char *pilot_name)
{
	file_change_to_base_game_install_path();
	struct frontend_file_list *file_list =
		frontend_file_list_build_sorted("*.plt");
	file_change_to_install_path();
	if (file_list == NULL) {
		XVT_LOG_WARN(
			"pilot.list_failed by=\"startup\" kind=\"record\"");
		return 0;
	}
	if (pilot_name == NULL) {
		frontend_file_list_free(file_list);
		return 0;
	}
	if (pilot_name[0] == '\0') {
		XVT_LOG_DEBUG("pilot.restore_none");
		frontend_file_list_free(file_list);
		return 0;
	}

	struct frontend_file_list_node *node = file_list->head;
	int file_index = 0;
	int was_loaded = 0;
	while (file_index < file_list->count) {
		xvt_file *stream =
			file_open(node->path, g_file_mode_read_binary);
		if (stream != NULL) {
			file_read_bytes(stream, g_frontend_scratch_buffer,
					sizeof(g_pilot_data.name));
			file_close(stream);
#ifdef XVT_MODERN
			if (strcasecmp(g_frontend_scratch_buffer, pilot_name) ==
			    0) {
#else
			if (_strcmpi(g_frontend_scratch_buffer, pilot_name) ==
			    0) {
#endif
				if (pilot_load_from_path(node->path) != 0) {
					was_loaded = 1;
					XVT_LOG_INFO(
						"pilot.loaded by=\"startup\" index=%d rating=%d faction=%d missions=%d score=%d",
						file_index,
						(int)g_pilot_data.rating,
						g_pilot_data.current_faction_id,
						g_pilot_data
							.total_missions_played_count,
						g_pilot_data.total_score);
				} else {
					XVT_LOG_WARN(
						"pilot.select_failed by=\"startup\" index=%d partial=%d",
						file_index,
						g_pilot_data.name[0] != '\0');
				}
				break;
			}
		}
		node = node->next;
		++file_index;
	}
	if (file_index >= file_list->count) {
		XVT_LOG_WARN("pilot.restore_unlisted count=%d",
			     file_list->count);
	}

	frontend_file_list_free(file_list);
	return was_loaded;
}

/* Reads the quoted options of the command line. "a=ADDRESS" copies the address
 * into g_game_config.ip_address; any other quoted option whose second character
 * is '=' gives a pilot name. With a pilot name, when no last pilot is set or it
 * cannot be loaded, it makes the given name, or a random one of
 * g_random_pilot_names for "joiner" or "host", the last pilot and creates it;
 * when the last pilot loads, the given name is ignored. With both a name and an
 * address it sets TCP/IP and internet play; otherwise it clears g_opt_is_host and
 * g_opt_is_client. Returns 1 on every path. */
// FUNCTION: XVT 0x4C9C50
int pilot_parse_command_line(const char *cmd_line)
{
	struct parsed_command_line {
		int has_pilot_name; /* 1 once an option gave a pilot name. */
		/* Name from the last name option, at most 12 characters. */
		char pilot_name[16];
		/* The quoted option being read, at most 255 characters. */
		char parameter[256];
	} parsed;

	enum {
		RANDOM_PILOT_NAME_COUNT = 40,
	};

	int command_index = 0;
	int command_length = strlen(cmd_line);
	int has_network_address = 0;
	parsed.has_pilot_name = 0;
	if (command_length > 0) {
		do {
			if (cmd_line[command_index] == '"') {
				++command_index;
				int parameter_index = 0;
				while (cmd_line[command_index] != '"') {
					if (command_index >= command_length) {
						break;
					}
					if (parameter_index >=
					    (int)sizeof(parsed.parameter) - 1) {
						break;
					}
					parsed.parameter[parameter_index] =
						cmd_line[command_index];
					++parameter_index;
					++command_index;
				}

				parsed.parameter[parameter_index] = '\0';
				if (parsed.parameter[0] == 'a' &&
				    parsed.parameter[1] == '=') {
					strncpy(g_game_config.ip_address,
						&parsed.parameter[2],
						sizeof(g_game_config
							       .ip_address) -
							1);
					g_game_config.ip_address
						[sizeof(g_game_config
								.ip_address) -
						 1] = '\0';
					has_network_address = 1;
				} else {
					/* The original parser accepts every
					 * non-address option using the x=
					 * form. */
					parsed.parameter[0] =
						parsed.parameter[1] == '=';
					if (parsed.parameter[0] != 0) {
						strncpy(parsed.pilot_name,
							&parsed.parameter[2],
							sizeof(g_game_config
								       .last_pilot_name) -
								1);
						parsed.pilot_name
							[sizeof(g_game_config
									.last_pilot_name) -
							 1] = '\0';
						parsed.has_pilot_name = 1;
					}
				}
			}

			++command_index;
		} while (command_index < command_length);
	}
	XVT_LOG_DEBUG("pilot.command_line length=%d named=%d address=%d",
		      command_length, parsed.has_pilot_name,
		      has_network_address);

	if (parsed.has_pilot_name != 0) {
		if (g_game_config.last_pilot_name[0] == '\0') {
			if (strcmp(parsed.pilot_name, "joiner") == 0 ||
			    strcmp(parsed.pilot_name, "host") == 0) {
				srand(GetTickCount());
				strcpy(g_game_config.last_pilot_name,
				       g_random_pilot_names
					       [rand() %
						RANDOM_PILOT_NAME_COUNT]);
			} else {
				strcpy(g_game_config.last_pilot_name,
				       parsed.pilot_name);
			}
			pilot_create_new(g_game_config.last_pilot_name);
		} else if (pilot_find_and_load_by_name(
				   g_game_config.last_pilot_name) == 0) {
			if (strcmp(parsed.pilot_name, "joiner") == 0 ||
			    strcmp(parsed.pilot_name, "host") == 0) {
				srand(GetTickCount());
				strcpy(g_game_config.last_pilot_name,
				       g_random_pilot_names
					       [rand() %
						RANDOM_PILOT_NAME_COUNT]);
			} else {
				strcpy(g_game_config.last_pilot_name,
				       parsed.pilot_name);
			}
			pilot_create_new(g_game_config.last_pilot_name);
		}
	}

	if (parsed.has_pilot_name != 0 && has_network_address != 0) {
		g_game_config.network_type = NET_TRANSPORT_TCPIP;
		g_game_config.internet_play = 1;
		return 1;
	}

	g_opt_is_host = 0;
	g_opt_is_client = 0;
	return 1;
}

/* Loads the pilot whose .plt file in the base game folder is base_pilot_path.
 * Clears g_pilot_data and reads the .pl2 file of the same name ending in '2', in
 * the install folder, into it when that opens. When the .plt opens, a pilot
 * without a .pl2 first gets a new pilot's rating and training mission choices,
 * then pilot_load_xvt_record copies the record over g_pilot_data, and the pilot
 * without a .pl2 gets its game and host names. Returns 0 when neither file
 * opens, and in the modern build when a read fails; else 1. */
// FUNCTION: XVT 0x4CB0C0
int pilot_load_from_path(const char *base_pilot_path)
{
	int has_expansion_record = 0;
	memset(&g_pilot_data, 0, sizeof(g_pilot_data));
	char expansion_pilot_path[128];
	strcpy(expansion_pilot_path, base_pilot_path);
	expansion_pilot_path[strlen(expansion_pilot_path) - 1] = '2';
	xvt_file *expansion_stream =
		file_open(expansion_pilot_path, g_file_mode_read_binary);
	if (expansion_stream != NULL) {
		has_expansion_record = 1;
#ifdef XVT_MODERN
		if (!file_read_bytes(expansion_stream, &g_pilot_data,
				     sizeof(g_pilot_data))) {
			file_close(expansion_stream);
			memset(&g_pilot_data, 0, sizeof(g_pilot_data));
			XVT_LOG_ERROR("pilot.expansion_unreadable bytes=%u",
				      (unsigned)sizeof(g_pilot_data));
			return 0;
		}
#else
		file_read_bytes(expansion_stream, &g_pilot_data,
				sizeof(g_pilot_data));
#endif
		file_close(expansion_stream);
	}

	file_change_to_base_game_install_path();
	xvt_file *xvt_stream =
		file_open(base_pilot_path, g_file_mode_read_binary);
	file_change_to_install_path();
	XVT_LOG_DEBUG("pilot.load_files path=\"%s\" expansion=%d base=%d",
		      base_pilot_path, has_expansion_record,
		      xvt_stream != NULL);
	if (xvt_stream != NULL) {
		if (has_expansion_record == 0) {
			g_pilot_data.rating = PILOT_RATING_TRAINEE;
			strcpy(g_pilot_data.rating_name,
			       frontend_string_get(FRONTSTR_124_TRAINEE));
			mission_setup_load_mission_list(
				MISSION_DIRECTORY_TRAINING_EXERCISES);
			if (g_mission_list != NULL) {
				g_pilot_data.mission_description_ids
					[MISSION_DIRECTORY_TRAINING_EXERCISES] =
					g_mission_list->mission_idx;
				g_pilot_data.faction_statistics[0].mission_description_ids
					[MISSION_DIRECTORY_TRAINING_EXERCISES] =
					g_mission_list->mission_idx;
				free(g_mission_list);
				g_mission_list = NULL;
			}

			g_pilot_data.current_faction_id = 1;
			mission_setup_load_mission_list(
				MISSION_DIRECTORY_TRAINING_EXERCISES);
			g_pilot_data.current_faction_id = 0;
			if (g_mission_list != NULL) {
				g_pilot_data.faction_statistics[1].mission_description_ids
					[MISSION_DIRECTORY_TRAINING_EXERCISES] =
					g_mission_list->mission_idx;
				free(g_mission_list);
				g_mission_list = NULL;
			}

			g_frontend_mission_session_mode =
				FRONTEND_MISSION_SESSION_NET_HOST;
			mission_setup_load_mission_list(
				MISSION_DIRECTORY_TRAINING_EXERCISES);
			if (g_mission_list != NULL) {
				g_pilot_data.faction_statistics[2].mission_description_ids
					[MISSION_DIRECTORY_TRAINING_EXERCISES] =
					g_mission_list->mission_idx;
				free(g_mission_list);
				g_mission_list = NULL;
			}
			g_frontend_mission_session_mode =
				FRONTEND_MISSION_SESSION_NONE;
			XVT_LOG_DEBUG(
				"pilot.defaults_set training=\"%d,%d,%d,%d\"",
				(int)g_pilot_data.mission_description_ids
					[MISSION_DIRECTORY_TRAINING_EXERCISES],
				g_pilot_data.faction_statistics[0].mission_description_ids
					[MISSION_DIRECTORY_TRAINING_EXERCISES],
				g_pilot_data.faction_statistics[1].mission_description_ids
					[MISSION_DIRECTORY_TRAINING_EXERCISES],
				g_pilot_data.faction_statistics[2].mission_description_ids
					[MISSION_DIRECTORY_TRAINING_EXERCISES]);
		}

#ifdef XVT_MODERN
		if (!pilot_load_xvt_record(xvt_stream)) {
			file_close(xvt_stream);
			return 0;
		}
#else
		pilot_load_xvt_record(xvt_stream);
#endif
		file_close(xvt_stream);
		if (has_expansion_record == 0) {
			sprintf(g_pilot_data.multiplayer_game_name, "%s%s",
				g_pilot_data.name,
				frontend_string_get(FRONTSTR_470_S_GAME));
			strcpy(g_pilot_data.multiplayer_host_name,
			       g_pilot_data.multiplayer_game_name);
		}
	} else if (has_expansion_record == 0) {
		XVT_LOG_ERROR("pilot.files_missing");
		return 0;
	} else {
		XVT_LOG_WARN("pilot.record_missing");
	}

	return 1;
}

#pragma pack(push, 1)

/* pilot_stats as the base game's pilot file stores it: the same tables in the
 * same order, each by the three mission types, but 88 craft types in each
 * per-craft table where pilot_stats has 100. pilot_load_xvt_record and
 * pilot_write_xvt_record copy each field to and from the pilot_stats field of the
 * same name, with the exceptions their comments give. */
struct pilot_xvt_stats {
	int total_score_per_mt[3]; /* Score. */
	/* Missions flown on their own. */
	int standalone_missions_played_per_mt[3];
	int sequence_missions_played_per_mt
		[3];		   /* Missions flown in a sequence. */
	int total_kills_per_mt[3]; /* Full kills. */
	int total_friendlies_killed_per_mt[3]; /* Friendly craft killed. */
	int kills_per_craft_per_mt[3][88];     /* Full kills by craft type. */
	int kills_shared_per_craft_per_mt[3]
					 [88]; /* Shared kills by craft type. */
	int kills_assists_per_craft_per_mt[3][88]; /* Assists by craft type. */
	/* Full kills of players, by the victim's rating. */
	int kills_full_on_player_rating_per_mt[3][25];
	/* Shared kills of players, by the victim's rating. */
	int kills_shared_on_player_rating_per_mt[3][25];
	/* Assists on players, by the victim's rating. */
	int kills_assist_on_player_rating_per_mt[3][25];
	/* Full kills of AI craft, by the victim's AI rating. */
	int kills_full_on_ai_rating_per_mt[3][6];
	/* Shared kills of AI craft, by the victim's AI rating. */
	int kills_shared_on_ai_rating_per_mt[3][6];
	/* Assists on AI craft, by the victim's AI rating. */
	int kills_assist_on_ai_rating_per_mt[3][6];
	int num_special_inspected_per_mt[3]; /* Special craft inspected. */
	int energy_hits_per_mt[3];	     /* Laser and ion hits. */
	int energy_fired_per_mt[3];	     /* Laser and ion shots fired. */
	int warheads_hits_per_mt[3];	     /* Warhead hits. */
	int warheads_fired_per_mt[3];	     /* Warheads fired. */
	int total_craft_losses_per_mt[3];    /* Craft lost. */
	int losses_by_collisions_per_mt[3];  /* Craft lost to collisions. */
	int losses_by_starships_per_mt[3];   /* Craft lost to starships. */
	int losses_by_mines_per_mt[3];	     /* Craft lost to mines. */
	/* Times killed by players, by the killer's rating. */
	int killed_by_player_rating_per_mt[3][25];
	/* Times killed by AI craft, by the killer's AI rating. */
	int killed_by_ai_rating_per_mt[3][6];
};

/* One faction record of the base game's pilot file. The history blocks at the
 * end are copied to and from g_pilot_data's pilot_faction starting at the place
 * each comment names: 4 bytes before the array of the same history there. */
struct pilot_xvt_faction {
	/* Missions flown; copied with pilot_faction's. */
	int total_missions_played_count;
	/* Never read or written by name; a save keeps the file's bytes. */
	uint8_t selection_state[68];
	/* Plaque counts; copied, with the three tables after it, as one
	 * 96-byte block to and from pilot_faction.melee_plaques. */
	int melee_plaques[6];
	/* Never read or written by name; copied in melee_plaques' block. */
	int tournament_trophies[6];
	/* Never read or written by name; copied in melee_plaques' block. */
	int mission_evaluations[6];
	/* Never read or written by name; copied in melee_plaques' block. */
	int battle_medallions[6];
	int mission_awards[4]; /* Copied with pilot_faction's. */
	uint8_t field_bc[16];  /* Copied with pilot_faction's. */
	int total_score;       /* Faction score; copied with pilot_faction's. */
	struct pilot_xvt_stats stats; /* Copied with pilot_faction.stats. */
	/* Single-player training history, at field1558. */
	uint8_t sp_training_data[3600];
	/* Single-player melee history, at sp_training_missions[99].field20. */
	uint8_t sp_melee_data[9000];
	/* Single-player combat history, at sp_melee_missions[249].field20. */
	uint8_t sp_combat_data[9000];
	/* Multiplayer training history, at sp_combat_missions[249].field20. */
	uint8_t mp_training_data[4800];
	/* Multiplayer melee history, at mp_training_missions[99].field2c. */
	uint8_t mp_melee_data[12000];
	/* Multiplayer combat history, at mp_melee_missions[249].field2c. */
	uint8_t mp_combat_data[12000];
	/* Single-player tournament history, at
	 * mp_combat_missions[249].field2c. */
	uint8_t sp_tournament_data[1000];
	/* Multiplayer tournament history, at sp_tournaments[24].field24. */
	uint8_t mp_tournament_data[1100];
	/* Single-player battle history, at mp_tournaments[24].field28. */
	uint8_t sp_battle_data[900];
	/* Multiplayer battle history, at sp_battles[24].field20. */
	uint8_t mp_battle_data[1000];
};

/* The base game's pilot file (.plt), 253,754 bytes. pilot_load_xvt_record copies
 * it into g_pilot_data and pilot_write_xvt_record back out; a field is copied to
 * and from pilot_data's field of the same name unless its comment says
 * otherwise. */
struct pilot_xvt_record {
	char name[14];	 /* Pilot name. */
	int total_score; /* Total score. */
	/* Local DirectPlay player id, stored when a mission launches. */
	int local_player_id;
	/* Set to 1 when a mission launches; only the file copies read it. */
	int launch_session_marker;
	/* net_is_host at the last launch from the debriefing, 1 in single
	 * player. */
	int is_host;
	/* Human players in the last mission launched. */
	unsigned int num_human_players_last_mission;
	/* g_frontend_mission_session_mode at the last launch. */
	int session_mode;
	/* Bytes 0 to 319 of pilot_data.xvt_record_payload. */
	uint8_t xvt_record_combat_payload[320];
	/* Bytes 320 to 351 of pilot_data.xvt_record_payload. */
	uint8_t xvt_record_identity_payload[32];
	/* Bytes 352 to 671 of pilot_data.xvt_record_payload. */
	uint8_t xvt_record_object_payload[320];
	/* Never read or written by name; a save keeps the file's bytes. */
	uint8_t legacy_rating_state[100];
	int current_rating_promo_points; /* Points toward the next rank. */
	/* Points from worse_rating_promo_points toward the next rank. */
	int current_rating_worse_promo_points;
	pilot_promotion_delta
		promotion_delta;    /* Last rank change: -1, 0 or 1. */
	int next_promotion_percent; /* Percent of the way to the next rank. */
	/* Lifetime statistics. A save leaves the first five tables as the
	 * file had them (see pilot_write_xvt_record). */
	struct pilot_xvt_stats main_stats;
	/* Never read or written by name; a save keeps the file's bytes. */
	uint8_t mission_sequence_state[3348];
	pilot_rating rating;		 /* Rank. */
	int total_missions_played_count; /* Missions flown. */
	/* Mission count at which each rank was reached. */
	int rating_achieved_on_mission[25];
	char rating_name[32]; /* Rank name. */
	int mission_score;    /* Score of the last mission. */
	/* The last mission's kill tables, by player or flight group; zeroed
	 * before each mission by frontend_mission_init_player_state. */
	int kills_full_on_player[8];
	int kills_shared_on_player[8];		/* As kills_full_on_player. */
	int kills_full_on_flight_group[48];	/* As kills_full_on_player. */
	int kills_shared_on_flight_group[48];	/* As kills_full_on_player. */
	int kills_full_from_player[8];		/* As kills_full_on_player. */
	int kills_shared_from_player[8];	/* As kills_full_on_player. */
	int kills_full_from_flight_group[48];	/* As kills_full_on_player. */
	int kills_shared_from_flight_group[48]; /* As kills_full_on_player. */
	/* AI rating of each flight group, shown in the debriefing. */
	int flight_group_rating[48];
	struct pilot_xvt_stats
		last_mission_stats; /* Last mission's statistics. */
	struct pilot_network_player
		network_players[8];  /* Last mission's players. */
	struct pilot_team teams[10]; /* Last mission's teams. */
	int current_faction_id;	     /* Faction record in use. */
	/* The four faction records, copied field by field with
	 * pilot_data.faction_statistics. */
	struct pilot_xvt_faction faction_statistics[4];
};

#pragma pack(pop)

typedef char xvt_size_pilot_xvt_stats[(sizeof(struct pilot_xvt_stats) == 4824)
					      ? 1
					      : -1];
typedef char xvt_size_pilot_xvt_faction
	[(sizeof(struct pilot_xvt_faction) == 59428) ? 1 : -1];
typedef char xvt_size_pilot_xvt_record
	[(sizeof(struct pilot_xvt_record) == 0x3DF3A) ? 1 : -1];

/* Reads a whole base game pilot record from stream and copies it into
 * g_pilot_data: identity, payloads, promotion state, both stats blocks, rating,
 * the kill tables, network players, teams, current faction and the four faction
 * records. In each per-craft table the record's 88 entries replace
 * g_pilot_data's first 88, except entries 4, 36, 41, 43, 45, 54 and 78, which
 * keep g_pilot_data's values. Each faction's history blocks go in starting 4
 * bytes before the matching arrays of its pilot_faction. legacy_rating_state,
 * mission_sequence_state and selection_state are not copied. Returns 1; the modern
 * build returns 0 when the read fails. Does not check the record's size or
 * contents. */
// FUNCTION: XVT 0x4C9F80
int pilot_load_xvt_record(xvt_file *stream)
{
	/* g_pilot_data's entries for the seven craft the record must not
	 * overwrite, saved before each per-craft table is copied and put
	 * back after. */
	struct preserved_craft_stats {
		int b_wing;		  /* Entry 4. */
		int super_star_destroyer; /* Entry 54. */
		int modified_frigate;	  /* Entry 43. */
		int carrack_cruiser;	  /* Entry 45. */
		int modified_corvette;	  /* Entry 41. */
		int dreadnaught;	  /* Entry 36. */
		int gun_emplacement;	  /* Entry 78. */
	} preserved_craft_stats;

	enum {
		MISSION_TYPE_COUNT = 3,
		FACTION_COUNT = 4,
		B_WING_CRAFT_INDEX = 4,
		DREADNAUGHT_CRAFT_INDEX = 36,
		MODIFIED_CORVETTE_CRAFT_INDEX = 41,
		MODIFIED_FRIGATE_CRAFT_INDEX = 43,
		CARRACK_CRUISER_CRAFT_INDEX = 45,
		SUPER_STAR_DESTROYER_CRAFT_INDEX = 54,
		GUN_EMPLACEMENT_CRAFT_INDEX = 78
	};

	struct pilot_xvt_record record;
#ifdef XVT_MODERN
	if (!file_read_bytes(stream, &record, sizeof(record))) {
		XVT_LOG_ERROR("pilot.record_unreadable stage=\"load\" bytes=%u",
			      (unsigned)sizeof(record));
		return 0;
	}
#else
	file_read_bytes(stream, &record, sizeof(record));
#endif
	memcpy(g_pilot_data.name, record.name, sizeof(record.name));
	g_pilot_data.total_score = record.total_score;
	g_pilot_data.local_player_id = record.local_player_id;
	g_pilot_data.launch_session_marker = record.launch_session_marker;
	g_pilot_data.is_host = record.is_host;
	g_pilot_data.num_human_players_last_mission =
		record.num_human_players_last_mission;
	g_pilot_data.session_mode = record.session_mode;
	memcpy(g_pilot_data.xvt_record_payload,
	       record.xvt_record_combat_payload,
	       sizeof(record.xvt_record_combat_payload));
	memcpy(&g_pilot_data.xvt_record_payload[sizeof(
		       record.xvt_record_combat_payload)],
	       record.xvt_record_identity_payload,
	       sizeof(record.xvt_record_identity_payload));
	memcpy(&g_pilot_data.xvt_record_payload
			[sizeof(record.xvt_record_combat_payload) +
			 sizeof(record.xvt_record_identity_payload)],
	       record.xvt_record_object_payload,
	       sizeof(record.xvt_record_object_payload));
	g_pilot_data.current_rating_promo_points =
		record.current_rating_promo_points;
	g_pilot_data.current_rating_worse_promo_points =
		record.current_rating_worse_promo_points;
	g_pilot_data.promotion_delta = record.promotion_delta;
	g_pilot_data.next_promotion_percent = record.next_promotion_percent;

	memcpy(g_pilot_data.main_stats.total_score_per_mt,
	       record.main_stats.total_score_per_mt,
	       sizeof(record.main_stats.total_score_per_mt));
	memcpy(g_pilot_data.main_stats.standalone_missions_played_per_mt,
	       record.main_stats.standalone_missions_played_per_mt,
	       sizeof(record.main_stats.standalone_missions_played_per_mt));
	memcpy(g_pilot_data.main_stats.sequence_missions_played_per_mt,
	       record.main_stats.sequence_missions_played_per_mt,
	       sizeof(record.main_stats.sequence_missions_played_per_mt));
	memcpy(g_pilot_data.main_stats.total_kills_per_mt,
	       record.main_stats.total_kills_per_mt,
	       sizeof(record.main_stats.total_kills_per_mt));
	memcpy(g_pilot_data.main_stats.total_friendlies_killed_per_mt,
	       record.main_stats.total_friendlies_killed_per_mt,
	       sizeof(record.main_stats.total_friendlies_killed_per_mt));
	for (int main_mission_type = 0; main_mission_type < MISSION_TYPE_COUNT;
	     ++main_mission_type) {
		preserved_craft_stats.b_wing =
			g_pilot_data.main_stats
				.kills_per_craft_per_mt[main_mission_type]
						       [B_WING_CRAFT_INDEX];
		preserved_craft_stats.dreadnaught =
			g_pilot_data.main_stats.kills_per_craft_per_mt
				[main_mission_type][DREADNAUGHT_CRAFT_INDEX];
		preserved_craft_stats.modified_corvette =
			g_pilot_data.main_stats.kills_per_craft_per_mt
				[main_mission_type]
				[MODIFIED_CORVETTE_CRAFT_INDEX];
		preserved_craft_stats.modified_frigate =
			g_pilot_data.main_stats.kills_per_craft_per_mt
				[main_mission_type]
				[MODIFIED_FRIGATE_CRAFT_INDEX];
		preserved_craft_stats.carrack_cruiser =
			g_pilot_data.main_stats.kills_per_craft_per_mt
				[main_mission_type]
				[CARRACK_CRUISER_CRAFT_INDEX];
		preserved_craft_stats.super_star_destroyer =
			g_pilot_data.main_stats.kills_per_craft_per_mt
				[main_mission_type]
				[SUPER_STAR_DESTROYER_CRAFT_INDEX];
		preserved_craft_stats.gun_emplacement =
			g_pilot_data.main_stats.kills_per_craft_per_mt
				[main_mission_type]
				[GUN_EMPLACEMENT_CRAFT_INDEX];
		memcpy(g_pilot_data.main_stats
			       .kills_per_craft_per_mt[main_mission_type],
		       record.main_stats
			       .kills_per_craft_per_mt[main_mission_type],
		       sizeof(record.main_stats.kills_per_craft_per_mt
				      [main_mission_type]));
		g_pilot_data.main_stats
			.kills_per_craft_per_mt[main_mission_type]
					       [B_WING_CRAFT_INDEX] =
			preserved_craft_stats.b_wing;
		g_pilot_data.main_stats
			.kills_per_craft_per_mt[main_mission_type]
					       [DREADNAUGHT_CRAFT_INDEX] =
			preserved_craft_stats.dreadnaught;
		g_pilot_data.main_stats
			.kills_per_craft_per_mt[main_mission_type]
					       [MODIFIED_CORVETTE_CRAFT_INDEX] =
			preserved_craft_stats.modified_corvette;
		g_pilot_data.main_stats
			.kills_per_craft_per_mt[main_mission_type]
					       [MODIFIED_FRIGATE_CRAFT_INDEX] =
			preserved_craft_stats.modified_frigate;
		g_pilot_data.main_stats
			.kills_per_craft_per_mt[main_mission_type]
					       [CARRACK_CRUISER_CRAFT_INDEX] =
			preserved_craft_stats.carrack_cruiser;
		g_pilot_data.main_stats.kills_per_craft_per_mt
			[main_mission_type][SUPER_STAR_DESTROYER_CRAFT_INDEX] =
			preserved_craft_stats.super_star_destroyer;
		g_pilot_data.main_stats
			.kills_per_craft_per_mt[main_mission_type]
					       [GUN_EMPLACEMENT_CRAFT_INDEX] =
			preserved_craft_stats.gun_emplacement;

		preserved_craft_stats.b_wing =
			g_pilot_data.main_stats.kills_shared_per_craft_per_mt
				[main_mission_type][B_WING_CRAFT_INDEX];
		preserved_craft_stats.dreadnaught =
			g_pilot_data.main_stats.kills_shared_per_craft_per_mt
				[main_mission_type][DREADNAUGHT_CRAFT_INDEX];
		preserved_craft_stats.modified_corvette =
			g_pilot_data.main_stats.kills_shared_per_craft_per_mt
				[main_mission_type]
				[MODIFIED_CORVETTE_CRAFT_INDEX];
		preserved_craft_stats.modified_frigate =
			g_pilot_data.main_stats.kills_shared_per_craft_per_mt
				[main_mission_type]
				[MODIFIED_FRIGATE_CRAFT_INDEX];
		preserved_craft_stats.carrack_cruiser =
			g_pilot_data.main_stats.kills_shared_per_craft_per_mt
				[main_mission_type]
				[CARRACK_CRUISER_CRAFT_INDEX];
		preserved_craft_stats.super_star_destroyer =
			g_pilot_data.main_stats.kills_shared_per_craft_per_mt
				[main_mission_type]
				[SUPER_STAR_DESTROYER_CRAFT_INDEX];
		preserved_craft_stats.gun_emplacement =
			g_pilot_data.main_stats.kills_shared_per_craft_per_mt
				[main_mission_type]
				[GUN_EMPLACEMENT_CRAFT_INDEX];
		memcpy(g_pilot_data.main_stats.kills_shared_per_craft_per_mt
			       [main_mission_type],
		       record.main_stats.kills_shared_per_craft_per_mt
			       [main_mission_type],
		       sizeof(record.main_stats.kills_shared_per_craft_per_mt
				      [main_mission_type]));
		g_pilot_data.main_stats
			.kills_shared_per_craft_per_mt[main_mission_type]
						      [B_WING_CRAFT_INDEX] =
			preserved_craft_stats.b_wing;
		g_pilot_data.main_stats.kills_shared_per_craft_per_mt
			[main_mission_type][DREADNAUGHT_CRAFT_INDEX] =
			preserved_craft_stats.dreadnaught;
		g_pilot_data.main_stats.kills_shared_per_craft_per_mt
			[main_mission_type][MODIFIED_CORVETTE_CRAFT_INDEX] =
			preserved_craft_stats.modified_corvette;
		g_pilot_data.main_stats.kills_shared_per_craft_per_mt
			[main_mission_type][MODIFIED_FRIGATE_CRAFT_INDEX] =
			preserved_craft_stats.modified_frigate;
		g_pilot_data.main_stats.kills_shared_per_craft_per_mt
			[main_mission_type][CARRACK_CRUISER_CRAFT_INDEX] =
			preserved_craft_stats.carrack_cruiser;
		g_pilot_data.main_stats.kills_shared_per_craft_per_mt
			[main_mission_type][SUPER_STAR_DESTROYER_CRAFT_INDEX] =
			preserved_craft_stats.super_star_destroyer;
		g_pilot_data.main_stats.kills_shared_per_craft_per_mt
			[main_mission_type][GUN_EMPLACEMENT_CRAFT_INDEX] =
			preserved_craft_stats.gun_emplacement;

		preserved_craft_stats.b_wing =
			g_pilot_data.main_stats.kills_assists_per_craft_per_mt
				[main_mission_type][B_WING_CRAFT_INDEX];
		preserved_craft_stats.dreadnaught =
			g_pilot_data.main_stats.kills_assists_per_craft_per_mt
				[main_mission_type][DREADNAUGHT_CRAFT_INDEX];
		preserved_craft_stats.modified_corvette =
			g_pilot_data.main_stats.kills_assists_per_craft_per_mt
				[main_mission_type]
				[MODIFIED_CORVETTE_CRAFT_INDEX];
		preserved_craft_stats.modified_frigate =
			g_pilot_data.main_stats.kills_assists_per_craft_per_mt
				[main_mission_type]
				[MODIFIED_FRIGATE_CRAFT_INDEX];
		preserved_craft_stats.carrack_cruiser =
			g_pilot_data.main_stats.kills_assists_per_craft_per_mt
				[main_mission_type]
				[CARRACK_CRUISER_CRAFT_INDEX];
		preserved_craft_stats.super_star_destroyer =
			g_pilot_data.main_stats.kills_assists_per_craft_per_mt
				[main_mission_type]
				[SUPER_STAR_DESTROYER_CRAFT_INDEX];
		preserved_craft_stats.gun_emplacement =
			g_pilot_data.main_stats.kills_assists_per_craft_per_mt
				[main_mission_type]
				[GUN_EMPLACEMENT_CRAFT_INDEX];
		memcpy(g_pilot_data.main_stats.kills_assists_per_craft_per_mt
			       [main_mission_type],
		       record.main_stats.kills_assists_per_craft_per_mt
			       [main_mission_type],
		       sizeof(record.main_stats.kills_assists_per_craft_per_mt
				      [main_mission_type]));
		g_pilot_data.main_stats
			.kills_assists_per_craft_per_mt[main_mission_type]
						       [B_WING_CRAFT_INDEX] =
			preserved_craft_stats.b_wing;
		g_pilot_data.main_stats.kills_assists_per_craft_per_mt
			[main_mission_type][DREADNAUGHT_CRAFT_INDEX] =
			preserved_craft_stats.dreadnaught;
		g_pilot_data.main_stats.kills_assists_per_craft_per_mt
			[main_mission_type][MODIFIED_CORVETTE_CRAFT_INDEX] =
			preserved_craft_stats.modified_corvette;
		g_pilot_data.main_stats.kills_assists_per_craft_per_mt
			[main_mission_type][MODIFIED_FRIGATE_CRAFT_INDEX] =
			preserved_craft_stats.modified_frigate;
		g_pilot_data.main_stats.kills_assists_per_craft_per_mt
			[main_mission_type][CARRACK_CRUISER_CRAFT_INDEX] =
			preserved_craft_stats.carrack_cruiser;
		g_pilot_data.main_stats.kills_assists_per_craft_per_mt
			[main_mission_type][SUPER_STAR_DESTROYER_CRAFT_INDEX] =
			preserved_craft_stats.super_star_destroyer;
		g_pilot_data.main_stats.kills_assists_per_craft_per_mt
			[main_mission_type][GUN_EMPLACEMENT_CRAFT_INDEX] =
			preserved_craft_stats.gun_emplacement;
	}

	memcpy(g_pilot_data.main_stats.kills_full_on_player_rating_per_mt,
	       record.main_stats.kills_full_on_player_rating_per_mt,
	       sizeof(record.main_stats.kills_full_on_player_rating_per_mt));
	memcpy(g_pilot_data.main_stats.kills_shared_on_player_rating_per_mt,
	       record.main_stats.kills_shared_on_player_rating_per_mt,
	       sizeof(record.main_stats.kills_shared_on_player_rating_per_mt));
	memcpy(g_pilot_data.main_stats.kills_assist_on_player_rating_per_mt,
	       record.main_stats.kills_assist_on_player_rating_per_mt,
	       sizeof(record.main_stats.kills_assist_on_player_rating_per_mt));
	memcpy(g_pilot_data.main_stats.kills_full_on_ai_rating_per_mt,
	       record.main_stats.kills_full_on_ai_rating_per_mt,
	       sizeof(record.main_stats.kills_full_on_ai_rating_per_mt));
	memcpy(g_pilot_data.main_stats.kills_shared_on_ai_rating_per_mt,
	       record.main_stats.kills_shared_on_ai_rating_per_mt,
	       sizeof(record.main_stats.kills_shared_on_ai_rating_per_mt));
	memcpy(g_pilot_data.main_stats.kills_assist_on_ai_rating_per_mt,
	       record.main_stats.kills_assist_on_ai_rating_per_mt,
	       sizeof(record.main_stats.kills_assist_on_ai_rating_per_mt));
	memcpy(g_pilot_data.main_stats.num_special_inspected_per_mt,
	       record.main_stats.num_special_inspected_per_mt,
	       sizeof(record.main_stats.num_special_inspected_per_mt));
	memcpy(g_pilot_data.main_stats.energy_hits_per_mt,
	       record.main_stats.energy_hits_per_mt,
	       sizeof(record.main_stats.energy_hits_per_mt));
	memcpy(g_pilot_data.main_stats.energy_fired_per_mt,
	       record.main_stats.energy_fired_per_mt,
	       sizeof(record.main_stats.energy_fired_per_mt));
	memcpy(g_pilot_data.main_stats.warheads_hits_per_mt,
	       record.main_stats.warheads_hits_per_mt,
	       sizeof(record.main_stats.warheads_hits_per_mt));
	memcpy(g_pilot_data.main_stats.warheads_fired_per_mt,
	       record.main_stats.warheads_fired_per_mt,
	       sizeof(record.main_stats.warheads_fired_per_mt));
	memcpy(g_pilot_data.main_stats.total_craft_losses_per_mt,
	       record.main_stats.total_craft_losses_per_mt,
	       sizeof(record.main_stats.total_craft_losses_per_mt));
	memcpy(g_pilot_data.main_stats.losses_by_collisions_per_mt,
	       record.main_stats.losses_by_collisions_per_mt,
	       sizeof(record.main_stats.losses_by_collisions_per_mt));
	memcpy(g_pilot_data.main_stats.losses_by_starships_per_mt,
	       record.main_stats.losses_by_starships_per_mt,
	       sizeof(record.main_stats.losses_by_starships_per_mt));
	memcpy(g_pilot_data.main_stats.losses_by_mines_per_mt,
	       record.main_stats.losses_by_mines_per_mt,
	       sizeof(record.main_stats.losses_by_mines_per_mt));
	memcpy(g_pilot_data.main_stats.killed_by_player_rating_per_mt,
	       record.main_stats.killed_by_player_rating_per_mt,
	       sizeof(record.main_stats.killed_by_player_rating_per_mt));
	memcpy(g_pilot_data.main_stats.killed_by_ai_rating_per_mt,
	       record.main_stats.killed_by_ai_rating_per_mt,
	       sizeof(record.main_stats.killed_by_ai_rating_per_mt));

	g_pilot_data.rating = record.rating;
	g_pilot_data.total_missions_played_count =
		record.total_missions_played_count;
	memcpy(g_pilot_data.rating_achieved_on_mission,
	       record.rating_achieved_on_mission,
	       sizeof(record.rating_achieved_on_mission));
	memcpy(g_pilot_data.rating_name, record.rating_name,
	       sizeof(record.rating_name));
	g_pilot_data.mission_score = record.mission_score;
	memcpy(g_pilot_data.kills_full_on_player, record.kills_full_on_player,
	       sizeof(record.kills_full_on_player));
	memcpy(g_pilot_data.kills_shared_on_player,
	       record.kills_shared_on_player,
	       sizeof(record.kills_shared_on_player));
	memcpy(g_pilot_data.kills_full_on_flight_group,
	       record.kills_full_on_flight_group,
	       sizeof(record.kills_full_on_flight_group));
	memcpy(g_pilot_data.kills_shared_on_flight_group,
	       record.kills_shared_on_flight_group,
	       sizeof(record.kills_shared_on_flight_group));
	memcpy(g_pilot_data.kills_full_from_player,
	       record.kills_full_from_player,
	       sizeof(record.kills_full_from_player));
	memcpy(g_pilot_data.kills_shared_from_player,
	       record.kills_shared_from_player,
	       sizeof(record.kills_shared_from_player));
	memcpy(g_pilot_data.kills_full_from_flight_group,
	       record.kills_full_from_flight_group,
	       sizeof(record.kills_full_from_flight_group));
	memcpy(g_pilot_data.kills_shared_from_flight_group,
	       record.kills_shared_from_flight_group,
	       sizeof(record.kills_shared_from_flight_group));
	memcpy(g_pilot_data.flight_group_rating, record.flight_group_rating,
	       sizeof(record.flight_group_rating));

	memcpy(g_pilot_data.last_mission_stats.total_score_per_mt,
	       record.last_mission_stats.total_score_per_mt,
	       sizeof(record.last_mission_stats.total_score_per_mt));
	memcpy(g_pilot_data.last_mission_stats
		       .standalone_missions_played_per_mt,
	       record.last_mission_stats.standalone_missions_played_per_mt,
	       sizeof(record.last_mission_stats
			      .standalone_missions_played_per_mt));
	memcpy(g_pilot_data.last_mission_stats.sequence_missions_played_per_mt,
	       record.last_mission_stats.sequence_missions_played_per_mt,
	       sizeof(record.last_mission_stats
			      .sequence_missions_played_per_mt));
	memcpy(g_pilot_data.last_mission_stats.total_kills_per_mt,
	       record.last_mission_stats.total_kills_per_mt,
	       sizeof(record.last_mission_stats.total_kills_per_mt));
	memcpy(g_pilot_data.last_mission_stats.total_friendlies_killed_per_mt,
	       record.last_mission_stats.total_friendlies_killed_per_mt,
	       sizeof(record.last_mission_stats
			      .total_friendlies_killed_per_mt));
	int mission_type;
	for (mission_type = 0; mission_type < MISSION_TYPE_COUNT;
	     ++mission_type) {
		preserved_craft_stats.b_wing =
			g_pilot_data.last_mission_stats
				.kills_per_craft_per_mt[mission_type]
						       [B_WING_CRAFT_INDEX];
		preserved_craft_stats.dreadnaught =
			g_pilot_data.last_mission_stats.kills_per_craft_per_mt
				[mission_type][DREADNAUGHT_CRAFT_INDEX];
		preserved_craft_stats.modified_corvette =
			g_pilot_data.last_mission_stats.kills_per_craft_per_mt
				[mission_type][MODIFIED_CORVETTE_CRAFT_INDEX];
		preserved_craft_stats.modified_frigate =
			g_pilot_data.last_mission_stats.kills_per_craft_per_mt
				[mission_type][MODIFIED_FRIGATE_CRAFT_INDEX];
		preserved_craft_stats.carrack_cruiser =
			g_pilot_data.last_mission_stats.kills_per_craft_per_mt
				[mission_type][CARRACK_CRUISER_CRAFT_INDEX];
		preserved_craft_stats.super_star_destroyer =
			g_pilot_data.last_mission_stats.kills_per_craft_per_mt
				[mission_type]
				[SUPER_STAR_DESTROYER_CRAFT_INDEX];
		preserved_craft_stats.gun_emplacement =
			g_pilot_data.last_mission_stats.kills_per_craft_per_mt
				[mission_type][GUN_EMPLACEMENT_CRAFT_INDEX];
		memcpy(g_pilot_data.last_mission_stats
			       .kills_per_craft_per_mt[mission_type],
		       record.last_mission_stats
			       .kills_per_craft_per_mt[mission_type],
		       sizeof(record.last_mission_stats
				      .kills_per_craft_per_mt[mission_type]));
		g_pilot_data.last_mission_stats
			.kills_per_craft_per_mt[mission_type]
					       [B_WING_CRAFT_INDEX] =
			preserved_craft_stats.b_wing;
		g_pilot_data.last_mission_stats
			.kills_per_craft_per_mt[mission_type]
					       [DREADNAUGHT_CRAFT_INDEX] =
			preserved_craft_stats.dreadnaught;
		g_pilot_data.last_mission_stats
			.kills_per_craft_per_mt[mission_type]
					       [MODIFIED_CORVETTE_CRAFT_INDEX] =
			preserved_craft_stats.modified_corvette;
		g_pilot_data.last_mission_stats
			.kills_per_craft_per_mt[mission_type]
					       [MODIFIED_FRIGATE_CRAFT_INDEX] =
			preserved_craft_stats.modified_frigate;
		g_pilot_data.last_mission_stats
			.kills_per_craft_per_mt[mission_type]
					       [CARRACK_CRUISER_CRAFT_INDEX] =
			preserved_craft_stats.carrack_cruiser;
		g_pilot_data.last_mission_stats.kills_per_craft_per_mt
			[mission_type][SUPER_STAR_DESTROYER_CRAFT_INDEX] =
			preserved_craft_stats.super_star_destroyer;
		g_pilot_data.last_mission_stats
			.kills_per_craft_per_mt[mission_type]
					       [GUN_EMPLACEMENT_CRAFT_INDEX] =
			preserved_craft_stats.gun_emplacement;

		preserved_craft_stats.b_wing =
			g_pilot_data.last_mission_stats
				.kills_shared_per_craft_per_mt
					[mission_type][B_WING_CRAFT_INDEX];
		preserved_craft_stats.dreadnaught =
			g_pilot_data.last_mission_stats
				.kills_shared_per_craft_per_mt
					[mission_type][DREADNAUGHT_CRAFT_INDEX];
		preserved_craft_stats.modified_corvette =
			g_pilot_data.last_mission_stats
				.kills_shared_per_craft_per_mt
					[mission_type]
					[MODIFIED_CORVETTE_CRAFT_INDEX];
		preserved_craft_stats.modified_frigate =
			g_pilot_data.last_mission_stats
				.kills_shared_per_craft_per_mt
					[mission_type]
					[MODIFIED_FRIGATE_CRAFT_INDEX];
		preserved_craft_stats.carrack_cruiser =
			g_pilot_data.last_mission_stats
				.kills_shared_per_craft_per_mt
					[mission_type]
					[CARRACK_CRUISER_CRAFT_INDEX];
		preserved_craft_stats.super_star_destroyer =
			g_pilot_data.last_mission_stats
				.kills_shared_per_craft_per_mt
					[mission_type]
					[SUPER_STAR_DESTROYER_CRAFT_INDEX];
		preserved_craft_stats.gun_emplacement =
			g_pilot_data.last_mission_stats
				.kills_shared_per_craft_per_mt
					[mission_type]
					[GUN_EMPLACEMENT_CRAFT_INDEX];
		memcpy(g_pilot_data.last_mission_stats
			       .kills_shared_per_craft_per_mt[mission_type],
		       record.last_mission_stats
			       .kills_shared_per_craft_per_mt[mission_type],
		       sizeof(record.last_mission_stats
				      .kills_shared_per_craft_per_mt
					      [mission_type]));
		g_pilot_data.last_mission_stats
			.kills_shared_per_craft_per_mt[mission_type]
						      [B_WING_CRAFT_INDEX] =
			preserved_craft_stats.b_wing;
		g_pilot_data.last_mission_stats.kills_shared_per_craft_per_mt
			[mission_type][DREADNAUGHT_CRAFT_INDEX] =
			preserved_craft_stats.dreadnaught;
		g_pilot_data.last_mission_stats.kills_shared_per_craft_per_mt
			[mission_type][MODIFIED_CORVETTE_CRAFT_INDEX] =
			preserved_craft_stats.modified_corvette;
		g_pilot_data.last_mission_stats.kills_shared_per_craft_per_mt
			[mission_type][MODIFIED_FRIGATE_CRAFT_INDEX] =
			preserved_craft_stats.modified_frigate;
		g_pilot_data.last_mission_stats.kills_shared_per_craft_per_mt
			[mission_type][CARRACK_CRUISER_CRAFT_INDEX] =
			preserved_craft_stats.carrack_cruiser;
		g_pilot_data.last_mission_stats.kills_shared_per_craft_per_mt
			[mission_type][SUPER_STAR_DESTROYER_CRAFT_INDEX] =
			preserved_craft_stats.super_star_destroyer;
		g_pilot_data.last_mission_stats.kills_shared_per_craft_per_mt
			[mission_type][GUN_EMPLACEMENT_CRAFT_INDEX] =
			preserved_craft_stats.gun_emplacement;

		preserved_craft_stats.b_wing =
			g_pilot_data.last_mission_stats
				.kills_assists_per_craft_per_mt
					[mission_type][B_WING_CRAFT_INDEX];
		preserved_craft_stats.dreadnaught =
			g_pilot_data.last_mission_stats
				.kills_assists_per_craft_per_mt
					[mission_type][DREADNAUGHT_CRAFT_INDEX];
		preserved_craft_stats.modified_corvette =
			g_pilot_data.last_mission_stats
				.kills_assists_per_craft_per_mt
					[mission_type]
					[MODIFIED_CORVETTE_CRAFT_INDEX];
		preserved_craft_stats.modified_frigate =
			g_pilot_data.last_mission_stats
				.kills_assists_per_craft_per_mt
					[mission_type]
					[MODIFIED_FRIGATE_CRAFT_INDEX];
		preserved_craft_stats.carrack_cruiser =
			g_pilot_data.last_mission_stats
				.kills_assists_per_craft_per_mt
					[mission_type]
					[CARRACK_CRUISER_CRAFT_INDEX];
		preserved_craft_stats.super_star_destroyer =
			g_pilot_data.last_mission_stats
				.kills_assists_per_craft_per_mt
					[mission_type]
					[SUPER_STAR_DESTROYER_CRAFT_INDEX];
		preserved_craft_stats.gun_emplacement =
			g_pilot_data.last_mission_stats
				.kills_assists_per_craft_per_mt
					[mission_type]
					[GUN_EMPLACEMENT_CRAFT_INDEX];
		memcpy(g_pilot_data.last_mission_stats
			       .kills_assists_per_craft_per_mt[mission_type],
		       record.last_mission_stats
			       .kills_assists_per_craft_per_mt[mission_type],
		       sizeof(record.last_mission_stats
				      .kills_assists_per_craft_per_mt
					      [mission_type]));
		g_pilot_data.last_mission_stats
			.kills_assists_per_craft_per_mt[mission_type]
						       [B_WING_CRAFT_INDEX] =
			preserved_craft_stats.b_wing;
		g_pilot_data.last_mission_stats.kills_assists_per_craft_per_mt
			[mission_type][DREADNAUGHT_CRAFT_INDEX] =
			preserved_craft_stats.dreadnaught;
		g_pilot_data.last_mission_stats.kills_assists_per_craft_per_mt
			[mission_type][MODIFIED_CORVETTE_CRAFT_INDEX] =
			preserved_craft_stats.modified_corvette;
		g_pilot_data.last_mission_stats.kills_assists_per_craft_per_mt
			[mission_type][MODIFIED_FRIGATE_CRAFT_INDEX] =
			preserved_craft_stats.modified_frigate;
		g_pilot_data.last_mission_stats.kills_assists_per_craft_per_mt
			[mission_type][CARRACK_CRUISER_CRAFT_INDEX] =
			preserved_craft_stats.carrack_cruiser;
		g_pilot_data.last_mission_stats.kills_assists_per_craft_per_mt
			[mission_type][SUPER_STAR_DESTROYER_CRAFT_INDEX] =
			preserved_craft_stats.super_star_destroyer;
		g_pilot_data.last_mission_stats.kills_assists_per_craft_per_mt
			[mission_type][GUN_EMPLACEMENT_CRAFT_INDEX] =
			preserved_craft_stats.gun_emplacement;
	}

	memcpy(g_pilot_data.last_mission_stats
		       .kills_full_on_player_rating_per_mt,
	       record.last_mission_stats.kills_full_on_player_rating_per_mt,
	       sizeof(record.last_mission_stats
			      .kills_full_on_player_rating_per_mt));
	memcpy(g_pilot_data.last_mission_stats
		       .kills_shared_on_player_rating_per_mt,
	       record.last_mission_stats.kills_shared_on_player_rating_per_mt,
	       sizeof(record.last_mission_stats
			      .kills_shared_on_player_rating_per_mt));
	memcpy(g_pilot_data.last_mission_stats
		       .kills_assist_on_player_rating_per_mt,
	       record.last_mission_stats.kills_assist_on_player_rating_per_mt,
	       sizeof(record.last_mission_stats
			      .kills_assist_on_player_rating_per_mt));
	memcpy(g_pilot_data.last_mission_stats.kills_full_on_ai_rating_per_mt,
	       record.last_mission_stats.kills_full_on_ai_rating_per_mt,
	       sizeof(record.last_mission_stats
			      .kills_full_on_ai_rating_per_mt));
	memcpy(g_pilot_data.last_mission_stats.kills_shared_on_ai_rating_per_mt,
	       record.last_mission_stats.kills_shared_on_ai_rating_per_mt,
	       sizeof(record.last_mission_stats
			      .kills_shared_on_ai_rating_per_mt));
	memcpy(g_pilot_data.last_mission_stats.kills_assist_on_ai_rating_per_mt,
	       record.last_mission_stats.kills_assist_on_ai_rating_per_mt,
	       sizeof(record.last_mission_stats
			      .kills_assist_on_ai_rating_per_mt));
	memcpy(g_pilot_data.last_mission_stats.num_special_inspected_per_mt,
	       record.last_mission_stats.num_special_inspected_per_mt,
	       sizeof(record.last_mission_stats.num_special_inspected_per_mt));
	memcpy(g_pilot_data.last_mission_stats.energy_hits_per_mt,
	       record.last_mission_stats.energy_hits_per_mt,
	       sizeof(record.last_mission_stats.energy_hits_per_mt));
	memcpy(g_pilot_data.last_mission_stats.energy_fired_per_mt,
	       record.last_mission_stats.energy_fired_per_mt,
	       sizeof(record.last_mission_stats.energy_fired_per_mt));
	memcpy(g_pilot_data.last_mission_stats.warheads_hits_per_mt,
	       record.last_mission_stats.warheads_hits_per_mt,
	       sizeof(record.last_mission_stats.warheads_hits_per_mt));
	memcpy(g_pilot_data.last_mission_stats.warheads_fired_per_mt,
	       record.last_mission_stats.warheads_fired_per_mt,
	       sizeof(record.last_mission_stats.warheads_fired_per_mt));
	memcpy(g_pilot_data.last_mission_stats.total_craft_losses_per_mt,
	       record.last_mission_stats.total_craft_losses_per_mt,
	       sizeof(record.last_mission_stats.total_craft_losses_per_mt));
	memcpy(g_pilot_data.last_mission_stats.losses_by_collisions_per_mt,
	       record.last_mission_stats.losses_by_collisions_per_mt,
	       sizeof(record.last_mission_stats.losses_by_collisions_per_mt));
	memcpy(g_pilot_data.last_mission_stats.losses_by_starships_per_mt,
	       record.last_mission_stats.losses_by_starships_per_mt,
	       sizeof(record.last_mission_stats.losses_by_starships_per_mt));
	memcpy(g_pilot_data.last_mission_stats.losses_by_mines_per_mt,
	       record.last_mission_stats.losses_by_mines_per_mt,
	       sizeof(record.last_mission_stats.losses_by_mines_per_mt));
	memcpy(g_pilot_data.last_mission_stats.killed_by_player_rating_per_mt,
	       record.last_mission_stats.killed_by_player_rating_per_mt,
	       sizeof(record.last_mission_stats
			      .killed_by_player_rating_per_mt));
	memcpy(g_pilot_data.last_mission_stats.killed_by_ai_rating_per_mt,
	       record.last_mission_stats.killed_by_ai_rating_per_mt,
	       sizeof(record.last_mission_stats.killed_by_ai_rating_per_mt));
	memcpy(g_pilot_data.network_players, record.network_players,
	       sizeof(record.network_players));
	memcpy(g_pilot_data.teams, record.teams, sizeof(record.teams));
	g_pilot_data.current_faction_id = record.current_faction_id;

	for (int faction_id = 0; faction_id < FACTION_COUNT; ++faction_id) {
		struct pilot_faction *destination_faction =
			&g_pilot_data.faction_statistics[faction_id];
		struct pilot_xvt_faction *source_faction =
			&record.faction_statistics[faction_id];
		destination_faction->total_missions_played_count =
			source_faction->total_missions_played_count;
		memcpy(destination_faction->melee_plaques,
		       source_faction->melee_plaques,
		       sizeof(source_faction->melee_plaques) +
			       sizeof(source_faction->tournament_trophies) +
			       sizeof(source_faction->mission_evaluations) +
			       sizeof(source_faction->battle_medallions));
		memcpy(destination_faction->mission_awards,
		       source_faction->mission_awards,
		       sizeof(source_faction->mission_awards));
		memcpy(destination_faction->field_bc, source_faction->field_bc,
		       sizeof(source_faction->field_bc));
		destination_faction->total_score = source_faction->total_score;
		memcpy(destination_faction->stats.total_score_per_mt,
		       source_faction->stats.total_score_per_mt,
		       sizeof(source_faction->stats.total_score_per_mt));
		memcpy(destination_faction->stats
			       .standalone_missions_played_per_mt,
		       source_faction->stats.standalone_missions_played_per_mt,
		       sizeof(source_faction->stats
				      .standalone_missions_played_per_mt));
		memcpy(destination_faction->stats
			       .sequence_missions_played_per_mt,
		       source_faction->stats.sequence_missions_played_per_mt,
		       sizeof(source_faction->stats
				      .sequence_missions_played_per_mt));
		memcpy(destination_faction->stats.total_kills_per_mt,
		       source_faction->stats.total_kills_per_mt,
		       sizeof(source_faction->stats.total_kills_per_mt));
		memcpy(destination_faction->stats
			       .total_friendlies_killed_per_mt,
		       source_faction->stats.total_friendlies_killed_per_mt,
		       sizeof(source_faction->stats
				      .total_friendlies_killed_per_mt));

		for (mission_type = 0; mission_type < MISSION_TYPE_COUNT;
		     ++mission_type) {
			preserved_craft_stats.b_wing =
				destination_faction->stats
					.kills_per_craft_per_mt
						[mission_type]
						[B_WING_CRAFT_INDEX];
			preserved_craft_stats.dreadnaught =
				destination_faction->stats
					.kills_per_craft_per_mt
						[mission_type]
						[DREADNAUGHT_CRAFT_INDEX];
			preserved_craft_stats.modified_corvette =
				destination_faction->stats
					.kills_per_craft_per_mt
						[mission_type]
						[MODIFIED_CORVETTE_CRAFT_INDEX];
			preserved_craft_stats.modified_frigate =
				destination_faction->stats
					.kills_per_craft_per_mt
						[mission_type]
						[MODIFIED_FRIGATE_CRAFT_INDEX];
			preserved_craft_stats.carrack_cruiser =
				destination_faction->stats
					.kills_per_craft_per_mt
						[mission_type]
						[CARRACK_CRUISER_CRAFT_INDEX];
			preserved_craft_stats.super_star_destroyer =
				destination_faction->stats.kills_per_craft_per_mt
					[mission_type]
					[SUPER_STAR_DESTROYER_CRAFT_INDEX];
			preserved_craft_stats.gun_emplacement =
				destination_faction->stats
					.kills_per_craft_per_mt
						[mission_type]
						[GUN_EMPLACEMENT_CRAFT_INDEX];
			memcpy(destination_faction->stats
				       .kills_per_craft_per_mt[mission_type],
			       source_faction->stats
				       .kills_per_craft_per_mt[mission_type],
			       sizeof(source_faction->stats
					      .kills_per_craft_per_mt
						      [mission_type]));
			destination_faction->stats
				.kills_per_craft_per_mt[mission_type]
						       [B_WING_CRAFT_INDEX] =
				preserved_craft_stats.b_wing;
			destination_faction->stats.kills_per_craft_per_mt
				[mission_type][DREADNAUGHT_CRAFT_INDEX] =
				preserved_craft_stats.dreadnaught;
			destination_faction->stats.kills_per_craft_per_mt
				[mission_type][MODIFIED_CORVETTE_CRAFT_INDEX] =
				preserved_craft_stats.modified_corvette;
			destination_faction->stats.kills_per_craft_per_mt
				[mission_type][MODIFIED_FRIGATE_CRAFT_INDEX] =
				preserved_craft_stats.modified_frigate;
			destination_faction->stats.kills_per_craft_per_mt
				[mission_type][CARRACK_CRUISER_CRAFT_INDEX] =
				preserved_craft_stats.carrack_cruiser;
			destination_faction->stats.kills_per_craft_per_mt
				[mission_type]
				[SUPER_STAR_DESTROYER_CRAFT_INDEX] =
				preserved_craft_stats.super_star_destroyer;
			destination_faction->stats.kills_per_craft_per_mt
				[mission_type][GUN_EMPLACEMENT_CRAFT_INDEX] =
				preserved_craft_stats.gun_emplacement;

			preserved_craft_stats.b_wing =
				destination_faction->stats
					.kills_shared_per_craft_per_mt
						[mission_type]
						[B_WING_CRAFT_INDEX];
			preserved_craft_stats.dreadnaught =
				destination_faction->stats
					.kills_shared_per_craft_per_mt
						[mission_type]
						[DREADNAUGHT_CRAFT_INDEX];
			preserved_craft_stats.modified_corvette =
				destination_faction->stats
					.kills_shared_per_craft_per_mt
						[mission_type]
						[MODIFIED_CORVETTE_CRAFT_INDEX];
			preserved_craft_stats.modified_frigate =
				destination_faction->stats
					.kills_shared_per_craft_per_mt
						[mission_type]
						[MODIFIED_FRIGATE_CRAFT_INDEX];
			preserved_craft_stats.carrack_cruiser =
				destination_faction->stats
					.kills_shared_per_craft_per_mt
						[mission_type]
						[CARRACK_CRUISER_CRAFT_INDEX];
			preserved_craft_stats.super_star_destroyer =
				destination_faction->stats
					.kills_shared_per_craft_per_mt
						[mission_type]
						[SUPER_STAR_DESTROYER_CRAFT_INDEX];
			preserved_craft_stats.gun_emplacement =
				destination_faction->stats
					.kills_shared_per_craft_per_mt
						[mission_type]
						[GUN_EMPLACEMENT_CRAFT_INDEX];
			memcpy(destination_faction->stats
				       .kills_shared_per_craft_per_mt
					       [mission_type],
			       source_faction->stats
				       .kills_shared_per_craft_per_mt
					       [mission_type],
			       sizeof(source_faction->stats
					      .kills_shared_per_craft_per_mt
						      [mission_type]));
			destination_faction->stats.kills_shared_per_craft_per_mt
				[mission_type][B_WING_CRAFT_INDEX] =
				preserved_craft_stats.b_wing;
			destination_faction->stats.kills_shared_per_craft_per_mt
				[mission_type][DREADNAUGHT_CRAFT_INDEX] =
				preserved_craft_stats.dreadnaught;
			destination_faction->stats.kills_shared_per_craft_per_mt
				[mission_type][MODIFIED_CORVETTE_CRAFT_INDEX] =
				preserved_craft_stats.modified_corvette;
			destination_faction->stats.kills_shared_per_craft_per_mt
				[mission_type][MODIFIED_FRIGATE_CRAFT_INDEX] =
				preserved_craft_stats.modified_frigate;
			destination_faction->stats.kills_shared_per_craft_per_mt
				[mission_type][CARRACK_CRUISER_CRAFT_INDEX] =
				preserved_craft_stats.carrack_cruiser;
			destination_faction->stats.kills_shared_per_craft_per_mt
				[mission_type]
				[SUPER_STAR_DESTROYER_CRAFT_INDEX] =
				preserved_craft_stats.super_star_destroyer;
			destination_faction->stats.kills_shared_per_craft_per_mt
				[mission_type][GUN_EMPLACEMENT_CRAFT_INDEX] =
				preserved_craft_stats.gun_emplacement;

			preserved_craft_stats.b_wing =
				destination_faction->stats
					.kills_assists_per_craft_per_mt
						[mission_type]
						[B_WING_CRAFT_INDEX];
			preserved_craft_stats.dreadnaught =
				destination_faction->stats
					.kills_assists_per_craft_per_mt
						[mission_type]
						[DREADNAUGHT_CRAFT_INDEX];
			preserved_craft_stats.modified_corvette =
				destination_faction->stats
					.kills_assists_per_craft_per_mt
						[mission_type]
						[MODIFIED_CORVETTE_CRAFT_INDEX];
			preserved_craft_stats.modified_frigate =
				destination_faction->stats
					.kills_assists_per_craft_per_mt
						[mission_type]
						[MODIFIED_FRIGATE_CRAFT_INDEX];
			preserved_craft_stats.carrack_cruiser =
				destination_faction->stats
					.kills_assists_per_craft_per_mt
						[mission_type]
						[CARRACK_CRUISER_CRAFT_INDEX];
			preserved_craft_stats.super_star_destroyer =
				destination_faction->stats
					.kills_assists_per_craft_per_mt
						[mission_type]
						[SUPER_STAR_DESTROYER_CRAFT_INDEX];
			preserved_craft_stats.gun_emplacement =
				destination_faction->stats
					.kills_assists_per_craft_per_mt
						[mission_type]
						[GUN_EMPLACEMENT_CRAFT_INDEX];
			memcpy(destination_faction->stats
				       .kills_assists_per_craft_per_mt
					       [mission_type],
			       source_faction->stats
				       .kills_assists_per_craft_per_mt
					       [mission_type],
			       sizeof(source_faction->stats
					      .kills_assists_per_craft_per_mt
						      [mission_type]));
			destination_faction->stats
				.kills_assists_per_craft_per_mt
					[mission_type][B_WING_CRAFT_INDEX] =
				preserved_craft_stats.b_wing;
			destination_faction->stats
				.kills_assists_per_craft_per_mt
					[mission_type]
					[DREADNAUGHT_CRAFT_INDEX] =
				preserved_craft_stats.dreadnaught;
			destination_faction->stats
				.kills_assists_per_craft_per_mt
					[mission_type]
					[MODIFIED_CORVETTE_CRAFT_INDEX] =
				preserved_craft_stats.modified_corvette;
			destination_faction->stats
				.kills_assists_per_craft_per_mt
					[mission_type]
					[MODIFIED_FRIGATE_CRAFT_INDEX] =
				preserved_craft_stats.modified_frigate;
			destination_faction->stats
				.kills_assists_per_craft_per_mt
					[mission_type]
					[CARRACK_CRUISER_CRAFT_INDEX] =
				preserved_craft_stats.carrack_cruiser;
			destination_faction->stats
				.kills_assists_per_craft_per_mt
					[mission_type]
					[SUPER_STAR_DESTROYER_CRAFT_INDEX] =
				preserved_craft_stats.super_star_destroyer;
			destination_faction->stats
				.kills_assists_per_craft_per_mt
					[mission_type]
					[GUN_EMPLACEMENT_CRAFT_INDEX] =
				preserved_craft_stats.gun_emplacement;
		}

		memcpy(destination_faction->stats
			       .kills_full_on_player_rating_per_mt,
		       source_faction->stats.kills_full_on_player_rating_per_mt,
		       sizeof(source_faction->stats
				      .kills_full_on_player_rating_per_mt));
		memcpy(destination_faction->stats
			       .kills_shared_on_player_rating_per_mt,
		       source_faction->stats
			       .kills_shared_on_player_rating_per_mt,
		       sizeof(source_faction->stats
				      .kills_shared_on_player_rating_per_mt));
		memcpy(destination_faction->stats
			       .kills_assist_on_player_rating_per_mt,
		       source_faction->stats
			       .kills_assist_on_player_rating_per_mt,
		       sizeof(source_faction->stats
				      .kills_assist_on_player_rating_per_mt));
		memcpy(destination_faction->stats
			       .kills_full_on_ai_rating_per_mt,
		       source_faction->stats.kills_full_on_ai_rating_per_mt,
		       sizeof(source_faction->stats
				      .kills_full_on_ai_rating_per_mt));
		memcpy(destination_faction->stats
			       .kills_shared_on_ai_rating_per_mt,
		       source_faction->stats.kills_shared_on_ai_rating_per_mt,
		       sizeof(source_faction->stats
				      .kills_shared_on_ai_rating_per_mt));
		memcpy(destination_faction->stats
			       .kills_assist_on_ai_rating_per_mt,
		       source_faction->stats.kills_assist_on_ai_rating_per_mt,
		       sizeof(source_faction->stats
				      .kills_assist_on_ai_rating_per_mt));
		memcpy(destination_faction->stats.num_special_inspected_per_mt,
		       source_faction->stats.num_special_inspected_per_mt,
		       sizeof(source_faction->stats
				      .num_special_inspected_per_mt));
		memcpy(destination_faction->stats.energy_hits_per_mt,
		       source_faction->stats.energy_hits_per_mt,
		       sizeof(source_faction->stats.energy_hits_per_mt));
		memcpy(destination_faction->stats.energy_fired_per_mt,
		       source_faction->stats.energy_fired_per_mt,
		       sizeof(source_faction->stats.energy_fired_per_mt));
		memcpy(destination_faction->stats.warheads_hits_per_mt,
		       source_faction->stats.warheads_hits_per_mt,
		       sizeof(source_faction->stats.warheads_hits_per_mt));
		memcpy(destination_faction->stats.warheads_fired_per_mt,
		       source_faction->stats.warheads_fired_per_mt,
		       sizeof(source_faction->stats.warheads_fired_per_mt));
		memcpy(destination_faction->stats.total_craft_losses_per_mt,
		       source_faction->stats.total_craft_losses_per_mt,
		       sizeof(source_faction->stats.total_craft_losses_per_mt));
		memcpy(destination_faction->stats.losses_by_collisions_per_mt,
		       source_faction->stats.losses_by_collisions_per_mt,
		       sizeof(source_faction->stats
				      .losses_by_collisions_per_mt));
		memcpy(destination_faction->stats.losses_by_starships_per_mt,
		       source_faction->stats.losses_by_starships_per_mt,
		       sizeof(source_faction->stats
				      .losses_by_starships_per_mt));
		memcpy(destination_faction->stats.losses_by_mines_per_mt,
		       source_faction->stats.losses_by_mines_per_mt,
		       sizeof(source_faction->stats.losses_by_mines_per_mt));
		memcpy(destination_faction->stats
			       .killed_by_player_rating_per_mt,
		       source_faction->stats.killed_by_player_rating_per_mt,
		       sizeof(source_faction->stats
				      .killed_by_player_rating_per_mt));
		memcpy(destination_faction->stats.killed_by_ai_rating_per_mt,
		       source_faction->stats.killed_by_ai_rating_per_mt,
		       sizeof(source_faction->stats
				      .killed_by_ai_rating_per_mt));
		memcpy(destination_faction->field1558,
		       source_faction->sp_training_data,
		       sizeof(source_faction->sp_training_data));
		memcpy(&destination_faction->sp_training_missions[99].field20,
		       source_faction->sp_melee_data,
		       sizeof(source_faction->sp_melee_data));
		memcpy(&destination_faction->sp_melee_missions[249].field20,
		       source_faction->sp_combat_data,
		       sizeof(source_faction->sp_combat_data));
		memcpy(&destination_faction->sp_combat_missions[249].field20,
		       source_faction->mp_training_data,
		       sizeof(source_faction->mp_training_data));
		memcpy(&destination_faction->mp_training_missions[99].field2c,
		       source_faction->mp_melee_data,
		       sizeof(source_faction->mp_melee_data));
		memcpy(&destination_faction->mp_melee_missions[249].field2c,
		       source_faction->mp_combat_data,
		       sizeof(source_faction->mp_combat_data));
		memcpy(&destination_faction->mp_combat_missions[249].field2c,
		       source_faction->sp_tournament_data,
		       sizeof(source_faction->sp_tournament_data));
		memcpy(&destination_faction->sp_tournaments[24].field24,
		       source_faction->mp_tournament_data,
		       sizeof(source_faction->mp_tournament_data));
		memcpy(&destination_faction->mp_tournaments[24].field28,
		       source_faction->sp_battle_data,
		       sizeof(source_faction->sp_battle_data));
		memcpy(&destination_faction->sp_battles[24].field20,
		       source_faction->mp_battle_data,
		       sizeof(source_faction->mp_battle_data));
	}
	XVT_LOG_DEBUG(
		"pilot.record_loaded rating=%d missions=%d score=%d faction=%d promo=%d percent=%d rank_change=%d",
		(int)g_pilot_data.rating,
		g_pilot_data.total_missions_played_count,
		g_pilot_data.total_score, g_pilot_data.current_faction_id,
		g_pilot_data.current_rating_promo_points,
		g_pilot_data.next_promotion_percent,
		(int)g_pilot_data.promotion_delta);
	if ((unsigned)g_pilot_data.rating >
		    (unsigned)PILOT_RATING_JEDI_MASTER ||
	    (unsigned)g_pilot_data.current_faction_id >=
		    (unsigned)FACTION_COUNT) {
		XVT_LOG_WARN("pilot.record_out_of_range rating=%d faction=%d",
			     (int)g_pilot_data.rating,
			     g_pilot_data.current_faction_id);
	}

	return 1;
}

/* Updates the base game pilot record fileName, in the base game folder, from
 * g_pilot_data: reads the existing record when it opens, else starts from zeros,
 * copies in the same fields pilot_load_xvt_record copies out, and writes it back.
 * Per-craft entries 4, 36, 41, 43, 45, 54 and 78 are written as 0. The five
 * per-mission-type totals at the start of main_stats are copied from the record
 * onto themselves, so they keep the old file's values. The original build opens
 * the file itself, ignoring stream, writes without checking the open, and
 * returns 1; the modern build ignores stream and returns the result of writing
 * the file whole. */
// FUNCTION: XVT 0x4CB310
int pilot_write_xvt_record(const char *file_name, xvt_file *stream)
{
	struct pilot_xvt_record record;

	memset(&record, 0, sizeof(record));
	file_change_to_base_game_install_path();
	xvt_file *input_stream = file_open(file_name, g_file_mode_read_binary);
	XVT_LOG_DEBUG("pilot.record_base found=%d", input_stream != NULL);
	if (input_stream != NULL) {
#ifdef XVT_MODERN
		if (!file_read_bytes(input_stream, &record, sizeof(record))) {
			file_close(input_stream);
			XVT_LOG_ERROR(
				"pilot.record_unreadable stage=\"save\" bytes=%u",
				(unsigned)sizeof(record));
			return 0;
		}
#else
		file_read_bytes(input_stream, &record, sizeof(record));
#endif
		file_close(input_stream);
	}
	file_change_to_install_path();
	file_change_to_base_game_install_path();
#ifndef XVT_MODERN
	stream = file_open(file_name, "wb");
#endif

	memcpy(record.name, g_pilot_data.name, sizeof(record.name));
	record.total_score = g_pilot_data.total_score;
	record.local_player_id = g_pilot_data.local_player_id;
	record.launch_session_marker = g_pilot_data.launch_session_marker;
	record.is_host = g_pilot_data.is_host;
	record.num_human_players_last_mission =
		g_pilot_data.num_human_players_last_mission;
	record.session_mode = g_pilot_data.session_mode;
	memcpy(record.xvt_record_combat_payload,
	       g_pilot_data.xvt_record_payload,
	       sizeof(record.xvt_record_combat_payload));
	memcpy(record.xvt_record_identity_payload,
	       &g_pilot_data.xvt_record_payload[320],
	       sizeof(record.xvt_record_identity_payload));
	memcpy(record.xvt_record_object_payload,
	       &g_pilot_data.xvt_record_payload[352],
	       sizeof(record.xvt_record_object_payload));
	record.current_rating_promo_points =
		g_pilot_data.current_rating_promo_points;
	record.current_rating_worse_promo_points =
		g_pilot_data.current_rating_worse_promo_points;
	record.promotion_delta = g_pilot_data.promotion_delta;
	record.next_promotion_percent = g_pilot_data.next_promotion_percent;

	memcpy(record.main_stats.total_score_per_mt,
	       record.main_stats.total_score_per_mt,
	       sizeof(record.main_stats.total_score_per_mt));
	memcpy(record.main_stats.standalone_missions_played_per_mt,
	       record.main_stats.standalone_missions_played_per_mt,
	       sizeof(record.main_stats.standalone_missions_played_per_mt));
	memcpy(record.main_stats.sequence_missions_played_per_mt,
	       record.main_stats.sequence_missions_played_per_mt,
	       sizeof(record.main_stats.sequence_missions_played_per_mt));
	memcpy(record.main_stats.total_kills_per_mt,
	       record.main_stats.total_kills_per_mt,
	       sizeof(record.main_stats.total_kills_per_mt));
	memcpy(record.main_stats.total_friendlies_killed_per_mt,
	       record.main_stats.total_friendlies_killed_per_mt,
	       sizeof(record.main_stats.total_friendlies_killed_per_mt));

	int mission_type;
	for (mission_type = 0; mission_type < 3; ++mission_type) {
		memcpy(record.main_stats.kills_per_craft_per_mt[mission_type],
		       g_pilot_data.main_stats
			       .kills_per_craft_per_mt[mission_type],
		       sizeof(record.main_stats
				      .kills_per_craft_per_mt[mission_type]));
		memcpy(record.main_stats
			       .kills_shared_per_craft_per_mt[mission_type],
		       g_pilot_data.main_stats
			       .kills_shared_per_craft_per_mt[mission_type],
		       sizeof(record.main_stats.kills_shared_per_craft_per_mt
				      [mission_type]));
		memcpy(record.main_stats
			       .kills_assists_per_craft_per_mt[mission_type],
		       g_pilot_data.main_stats
			       .kills_assists_per_craft_per_mt[mission_type],
		       sizeof(record.main_stats.kills_assists_per_craft_per_mt
				      [mission_type]));
		record.main_stats.kills_per_craft_per_mt[mission_type][78] = 0;
		record.main_stats.kills_per_craft_per_mt[mission_type][54] = 0;
		record.main_stats.kills_per_craft_per_mt[mission_type][45] = 0;
		record.main_stats.kills_per_craft_per_mt[mission_type][43] = 0;
		record.main_stats.kills_per_craft_per_mt[mission_type][41] = 0;
		record.main_stats.kills_per_craft_per_mt[mission_type][36] = 0;
		record.main_stats.kills_per_craft_per_mt[mission_type][4] = 0;
		record.main_stats
			.kills_shared_per_craft_per_mt[mission_type][78] = 0;
		record.main_stats
			.kills_shared_per_craft_per_mt[mission_type][54] = 0;
		record.main_stats
			.kills_shared_per_craft_per_mt[mission_type][45] = 0;
		record.main_stats
			.kills_shared_per_craft_per_mt[mission_type][43] = 0;
		record.main_stats
			.kills_shared_per_craft_per_mt[mission_type][41] = 0;
		record.main_stats
			.kills_shared_per_craft_per_mt[mission_type][36] = 0;
		record.main_stats
			.kills_shared_per_craft_per_mt[mission_type][4] = 0;
		record.main_stats
			.kills_assists_per_craft_per_mt[mission_type][78] = 0;
		record.main_stats
			.kills_assists_per_craft_per_mt[mission_type][54] = 0;
		record.main_stats
			.kills_assists_per_craft_per_mt[mission_type][45] = 0;
		record.main_stats
			.kills_assists_per_craft_per_mt[mission_type][43] = 0;
		record.main_stats
			.kills_assists_per_craft_per_mt[mission_type][41] = 0;
		record.main_stats
			.kills_assists_per_craft_per_mt[mission_type][36] = 0;
		record.main_stats
			.kills_assists_per_craft_per_mt[mission_type][4] = 0;
	}
	memcpy(record.main_stats.kills_full_on_player_rating_per_mt,
	       g_pilot_data.main_stats.kills_full_on_player_rating_per_mt,
	       sizeof(record.main_stats.kills_full_on_player_rating_per_mt));
	memcpy(record.main_stats.kills_shared_on_player_rating_per_mt,
	       g_pilot_data.main_stats.kills_shared_on_player_rating_per_mt,
	       sizeof(record.main_stats.kills_shared_on_player_rating_per_mt));
	memcpy(record.main_stats.kills_assist_on_player_rating_per_mt,
	       g_pilot_data.main_stats.kills_assist_on_player_rating_per_mt,
	       sizeof(record.main_stats.kills_assist_on_player_rating_per_mt));
	memcpy(record.main_stats.kills_full_on_ai_rating_per_mt,
	       g_pilot_data.main_stats.kills_full_on_ai_rating_per_mt,
	       sizeof(record.main_stats.kills_full_on_ai_rating_per_mt));
	memcpy(record.main_stats.kills_shared_on_ai_rating_per_mt,
	       g_pilot_data.main_stats.kills_shared_on_ai_rating_per_mt,
	       sizeof(record.main_stats.kills_shared_on_ai_rating_per_mt));
	memcpy(record.main_stats.kills_assist_on_ai_rating_per_mt,
	       g_pilot_data.main_stats.kills_assist_on_ai_rating_per_mt,
	       sizeof(record.main_stats.kills_assist_on_ai_rating_per_mt));
	memcpy(record.main_stats.num_special_inspected_per_mt,
	       g_pilot_data.main_stats.num_special_inspected_per_mt,
	       sizeof(record.main_stats.num_special_inspected_per_mt));
	memcpy(record.main_stats.energy_hits_per_mt,
	       g_pilot_data.main_stats.energy_hits_per_mt,
	       sizeof(record.main_stats.energy_hits_per_mt));
	memcpy(record.main_stats.energy_fired_per_mt,
	       g_pilot_data.main_stats.energy_fired_per_mt,
	       sizeof(record.main_stats.energy_fired_per_mt));
	memcpy(record.main_stats.warheads_hits_per_mt,
	       g_pilot_data.main_stats.warheads_hits_per_mt,
	       sizeof(record.main_stats.warheads_hits_per_mt));
	memcpy(record.main_stats.warheads_fired_per_mt,
	       g_pilot_data.main_stats.warheads_fired_per_mt,
	       sizeof(record.main_stats.warheads_fired_per_mt));
	memcpy(record.main_stats.total_craft_losses_per_mt,
	       g_pilot_data.main_stats.total_craft_losses_per_mt,
	       sizeof(record.main_stats.total_craft_losses_per_mt));
	memcpy(record.main_stats.losses_by_collisions_per_mt,
	       g_pilot_data.main_stats.losses_by_collisions_per_mt,
	       sizeof(record.main_stats.losses_by_collisions_per_mt));
	memcpy(record.main_stats.losses_by_starships_per_mt,
	       g_pilot_data.main_stats.losses_by_starships_per_mt,
	       sizeof(record.main_stats.losses_by_starships_per_mt));
	memcpy(record.main_stats.losses_by_mines_per_mt,
	       g_pilot_data.main_stats.losses_by_mines_per_mt,
	       sizeof(record.main_stats.losses_by_mines_per_mt));
	memcpy(record.main_stats.killed_by_player_rating_per_mt,
	       g_pilot_data.main_stats.killed_by_player_rating_per_mt,
	       sizeof(record.main_stats.killed_by_player_rating_per_mt));
	memcpy(record.main_stats.killed_by_ai_rating_per_mt,
	       g_pilot_data.main_stats.killed_by_ai_rating_per_mt,
	       sizeof(record.main_stats.killed_by_ai_rating_per_mt));

	record.rating = g_pilot_data.rating;
	record.total_missions_played_count =
		g_pilot_data.total_missions_played_count;
	memcpy(record.rating_achieved_on_mission,
	       g_pilot_data.rating_achieved_on_mission,
	       sizeof(record.rating_achieved_on_mission));
	memcpy(record.rating_name, g_pilot_data.rating_name,
	       sizeof(record.rating_name));
	record.mission_score = g_pilot_data.mission_score;
	memcpy(record.kills_full_on_player, g_pilot_data.kills_full_on_player,
	       sizeof(record.kills_full_on_player));
	memcpy(record.kills_shared_on_player,
	       g_pilot_data.kills_shared_on_player,
	       sizeof(record.kills_shared_on_player));
	memcpy(record.kills_full_on_flight_group,
	       g_pilot_data.kills_full_on_flight_group,
	       sizeof(record.kills_full_on_flight_group));
	memcpy(record.kills_shared_on_flight_group,
	       g_pilot_data.kills_shared_on_flight_group,
	       sizeof(record.kills_shared_on_flight_group));
	memcpy(record.kills_full_from_player,
	       g_pilot_data.kills_full_from_player,
	       sizeof(record.kills_full_from_player));
	memcpy(record.kills_shared_from_player,
	       g_pilot_data.kills_shared_from_player,
	       sizeof(record.kills_shared_from_player));
	memcpy(record.kills_full_from_flight_group,
	       g_pilot_data.kills_full_from_flight_group,
	       sizeof(record.kills_full_from_flight_group));
	memcpy(record.kills_shared_from_flight_group,
	       g_pilot_data.kills_shared_from_flight_group,
	       sizeof(record.kills_shared_from_flight_group));
	memcpy(record.flight_group_rating, g_pilot_data.flight_group_rating,
	       sizeof(record.flight_group_rating));

	memcpy(record.last_mission_stats.total_score_per_mt,
	       g_pilot_data.last_mission_stats.total_score_per_mt,
	       sizeof(record.last_mission_stats.total_score_per_mt));
	memcpy(record.last_mission_stats.standalone_missions_played_per_mt,
	       g_pilot_data.last_mission_stats
		       .standalone_missions_played_per_mt,
	       sizeof(record.last_mission_stats
			      .standalone_missions_played_per_mt));
	memcpy(record.last_mission_stats.sequence_missions_played_per_mt,
	       g_pilot_data.last_mission_stats.sequence_missions_played_per_mt,
	       sizeof(record.last_mission_stats
			      .sequence_missions_played_per_mt));
	memcpy(record.last_mission_stats.total_kills_per_mt,
	       g_pilot_data.last_mission_stats.total_kills_per_mt,
	       sizeof(record.last_mission_stats.total_kills_per_mt));
	memcpy(record.last_mission_stats.total_friendlies_killed_per_mt,
	       g_pilot_data.last_mission_stats.total_friendlies_killed_per_mt,
	       sizeof(record.last_mission_stats
			      .total_friendlies_killed_per_mt));
	for (mission_type = 0; mission_type < 3; ++mission_type) {
		memcpy(record.last_mission_stats
			       .kills_per_craft_per_mt[mission_type],
		       g_pilot_data.last_mission_stats
			       .kills_per_craft_per_mt[mission_type],
		       sizeof(record.last_mission_stats
				      .kills_per_craft_per_mt[mission_type]));
		memcpy(record.last_mission_stats
			       .kills_shared_per_craft_per_mt[mission_type],
		       g_pilot_data.last_mission_stats
			       .kills_shared_per_craft_per_mt[mission_type],
		       sizeof(record.last_mission_stats
				      .kills_shared_per_craft_per_mt
					      [mission_type]));
		memcpy(record.last_mission_stats
			       .kills_assists_per_craft_per_mt[mission_type],
		       g_pilot_data.last_mission_stats
			       .kills_assists_per_craft_per_mt[mission_type],
		       sizeof(record.last_mission_stats
				      .kills_assists_per_craft_per_mt
					      [mission_type]));
		record.last_mission_stats
			.kills_per_craft_per_mt[mission_type][78] = 0;
		record.last_mission_stats
			.kills_per_craft_per_mt[mission_type][54] = 0;
		record.last_mission_stats
			.kills_per_craft_per_mt[mission_type][45] = 0;
		record.last_mission_stats
			.kills_per_craft_per_mt[mission_type][43] = 0;
		record.last_mission_stats
			.kills_per_craft_per_mt[mission_type][41] = 0;
		record.last_mission_stats
			.kills_per_craft_per_mt[mission_type][36] = 0;
		record.last_mission_stats
			.kills_per_craft_per_mt[mission_type][4] = 0;
		record.last_mission_stats
			.kills_shared_per_craft_per_mt[mission_type][78] = 0;
		record.last_mission_stats
			.kills_shared_per_craft_per_mt[mission_type][54] = 0;
		record.last_mission_stats
			.kills_shared_per_craft_per_mt[mission_type][45] = 0;
		record.last_mission_stats
			.kills_shared_per_craft_per_mt[mission_type][43] = 0;
		record.last_mission_stats
			.kills_shared_per_craft_per_mt[mission_type][41] = 0;
		record.last_mission_stats
			.kills_shared_per_craft_per_mt[mission_type][36] = 0;
		record.last_mission_stats
			.kills_shared_per_craft_per_mt[mission_type][4] = 0;
		record.last_mission_stats
			.kills_assists_per_craft_per_mt[mission_type][78] = 0;
		record.last_mission_stats
			.kills_assists_per_craft_per_mt[mission_type][54] = 0;
		record.last_mission_stats
			.kills_assists_per_craft_per_mt[mission_type][45] = 0;
		record.last_mission_stats
			.kills_assists_per_craft_per_mt[mission_type][43] = 0;
		record.last_mission_stats
			.kills_assists_per_craft_per_mt[mission_type][41] = 0;
		record.last_mission_stats
			.kills_assists_per_craft_per_mt[mission_type][36] = 0;
		record.last_mission_stats
			.kills_assists_per_craft_per_mt[mission_type][4] = 0;
	}
	memcpy(record.last_mission_stats.kills_full_on_player_rating_per_mt,
	       g_pilot_data.last_mission_stats
		       .kills_full_on_player_rating_per_mt,
	       sizeof(record.last_mission_stats
			      .kills_full_on_player_rating_per_mt));
	memcpy(record.last_mission_stats.kills_shared_on_player_rating_per_mt,
	       g_pilot_data.last_mission_stats
		       .kills_shared_on_player_rating_per_mt,
	       sizeof(record.last_mission_stats
			      .kills_shared_on_player_rating_per_mt));
	memcpy(record.last_mission_stats.kills_assist_on_player_rating_per_mt,
	       g_pilot_data.last_mission_stats
		       .kills_assist_on_player_rating_per_mt,
	       sizeof(record.last_mission_stats
			      .kills_assist_on_player_rating_per_mt));
	memcpy(record.last_mission_stats.kills_full_on_ai_rating_per_mt,
	       g_pilot_data.last_mission_stats.kills_full_on_ai_rating_per_mt,
	       sizeof(record.last_mission_stats
			      .kills_full_on_ai_rating_per_mt));
	memcpy(record.last_mission_stats.kills_shared_on_ai_rating_per_mt,
	       g_pilot_data.last_mission_stats.kills_shared_on_ai_rating_per_mt,
	       sizeof(record.last_mission_stats
			      .kills_shared_on_ai_rating_per_mt));
	memcpy(record.last_mission_stats.kills_assist_on_ai_rating_per_mt,
	       g_pilot_data.last_mission_stats.kills_assist_on_ai_rating_per_mt,
	       sizeof(record.last_mission_stats
			      .kills_assist_on_ai_rating_per_mt));
	memcpy(record.last_mission_stats.num_special_inspected_per_mt,
	       g_pilot_data.last_mission_stats.num_special_inspected_per_mt,
	       sizeof(record.last_mission_stats.num_special_inspected_per_mt));
	memcpy(record.last_mission_stats.energy_hits_per_mt,
	       g_pilot_data.last_mission_stats.energy_hits_per_mt,
	       sizeof(record.last_mission_stats.energy_hits_per_mt));
	memcpy(record.last_mission_stats.energy_fired_per_mt,
	       g_pilot_data.last_mission_stats.energy_fired_per_mt,
	       sizeof(record.last_mission_stats.energy_fired_per_mt));
	memcpy(record.last_mission_stats.warheads_hits_per_mt,
	       g_pilot_data.last_mission_stats.warheads_hits_per_mt,
	       sizeof(record.last_mission_stats.warheads_hits_per_mt));
	memcpy(record.last_mission_stats.warheads_fired_per_mt,
	       g_pilot_data.last_mission_stats.warheads_fired_per_mt,
	       sizeof(record.last_mission_stats.warheads_fired_per_mt));
	memcpy(record.last_mission_stats.total_craft_losses_per_mt,
	       g_pilot_data.last_mission_stats.total_craft_losses_per_mt,
	       sizeof(record.last_mission_stats.total_craft_losses_per_mt));
	memcpy(record.last_mission_stats.losses_by_collisions_per_mt,
	       g_pilot_data.last_mission_stats.losses_by_collisions_per_mt,
	       sizeof(record.last_mission_stats.losses_by_collisions_per_mt));
	memcpy(record.last_mission_stats.losses_by_starships_per_mt,
	       g_pilot_data.last_mission_stats.losses_by_starships_per_mt,
	       sizeof(record.last_mission_stats.losses_by_starships_per_mt));
	memcpy(record.last_mission_stats.losses_by_mines_per_mt,
	       g_pilot_data.last_mission_stats.losses_by_mines_per_mt,
	       sizeof(record.last_mission_stats.losses_by_mines_per_mt));
	memcpy(record.last_mission_stats.killed_by_player_rating_per_mt,
	       g_pilot_data.last_mission_stats.killed_by_player_rating_per_mt,
	       sizeof(record.last_mission_stats
			      .killed_by_player_rating_per_mt));
	memcpy(record.last_mission_stats.killed_by_ai_rating_per_mt,
	       g_pilot_data.last_mission_stats.killed_by_ai_rating_per_mt,
	       sizeof(record.last_mission_stats.killed_by_ai_rating_per_mt));
	memcpy(record.network_players, g_pilot_data.network_players,
	       sizeof(record.network_players));
	memcpy(record.teams, g_pilot_data.teams, sizeof(record.teams));
	record.current_faction_id = g_pilot_data.current_faction_id;

	for (int faction_id = 0; faction_id < 4; ++faction_id) {
		struct pilot_xvt_faction *destination =
			&record.faction_statistics[faction_id];
		struct pilot_faction *source =
			&g_pilot_data.faction_statistics[faction_id];

		destination->total_missions_played_count =
			source->total_missions_played_count;
		memcpy(destination->melee_plaques, source->melee_plaques,
		       sizeof(destination->melee_plaques) +
			       sizeof(destination->tournament_trophies) +
			       sizeof(destination->mission_evaluations) +
			       sizeof(destination->battle_medallions));
		memcpy(destination->mission_awards, source->mission_awards,
		       sizeof(destination->mission_awards));
		memcpy(destination->field_bc, source->field_bc,
		       sizeof(destination->field_bc));
		destination->total_score = source->total_score;
		memcpy(destination->stats.total_score_per_mt,
		       source->stats.total_score_per_mt,
		       sizeof(destination->stats.total_score_per_mt));
		memcpy(destination->stats.standalone_missions_played_per_mt,
		       source->stats.standalone_missions_played_per_mt,
		       sizeof(destination->stats
				      .standalone_missions_played_per_mt));
		memcpy(destination->stats.sequence_missions_played_per_mt,
		       source->stats.sequence_missions_played_per_mt,
		       sizeof(destination->stats
				      .sequence_missions_played_per_mt));
		memcpy(destination->stats.total_kills_per_mt,
		       source->stats.total_kills_per_mt,
		       sizeof(destination->stats.total_kills_per_mt));
		memcpy(destination->stats.total_friendlies_killed_per_mt,
		       source->stats.total_friendlies_killed_per_mt,
		       sizeof(destination->stats
				      .total_friendlies_killed_per_mt));
		for (mission_type = 0; mission_type < 3; ++mission_type) {
			memcpy(destination->stats
				       .kills_per_craft_per_mt[mission_type],
			       source->stats
				       .kills_per_craft_per_mt[mission_type],
			       sizeof(destination->stats.kills_per_craft_per_mt
					      [mission_type]));
			memcpy(destination->stats.kills_shared_per_craft_per_mt
				       [mission_type],
			       source->stats.kills_shared_per_craft_per_mt
				       [mission_type],
			       sizeof(destination->stats
					      .kills_shared_per_craft_per_mt
						      [mission_type]));
			memcpy(destination->stats.kills_assists_per_craft_per_mt
				       [mission_type],
			       source->stats.kills_assists_per_craft_per_mt
				       [mission_type],
			       sizeof(destination->stats
					      .kills_assists_per_craft_per_mt
						      [mission_type]));
			destination->stats
				.kills_per_craft_per_mt[mission_type][78] = 0;
			destination->stats
				.kills_per_craft_per_mt[mission_type][54] = 0;
			destination->stats
				.kills_per_craft_per_mt[mission_type][45] = 0;
			destination->stats
				.kills_per_craft_per_mt[mission_type][43] = 0;
			destination->stats
				.kills_per_craft_per_mt[mission_type][41] = 0;
			destination->stats
				.kills_per_craft_per_mt[mission_type][36] = 0;
			destination->stats
				.kills_per_craft_per_mt[mission_type][4] = 0;
			destination->stats
				.kills_shared_per_craft_per_mt[mission_type]
							      [78] = 0;
			destination->stats
				.kills_shared_per_craft_per_mt[mission_type]
							      [54] = 0;
			destination->stats
				.kills_shared_per_craft_per_mt[mission_type]
							      [45] = 0;
			destination->stats
				.kills_shared_per_craft_per_mt[mission_type]
							      [43] = 0;
			destination->stats
				.kills_shared_per_craft_per_mt[mission_type]
							      [41] = 0;
			destination->stats
				.kills_shared_per_craft_per_mt[mission_type]
							      [36] = 0;
			destination->stats
				.kills_shared_per_craft_per_mt[mission_type]
							      [4] = 0;
			destination->stats
				.kills_assists_per_craft_per_mt[mission_type]
							       [78] = 0;
			destination->stats
				.kills_assists_per_craft_per_mt[mission_type]
							       [54] = 0;
			destination->stats
				.kills_assists_per_craft_per_mt[mission_type]
							       [45] = 0;
			destination->stats
				.kills_assists_per_craft_per_mt[mission_type]
							       [43] = 0;
			destination->stats
				.kills_assists_per_craft_per_mt[mission_type]
							       [41] = 0;
			destination->stats
				.kills_assists_per_craft_per_mt[mission_type]
							       [36] = 0;
			destination->stats
				.kills_assists_per_craft_per_mt[mission_type]
							       [4] = 0;
		}
		memcpy(destination->stats.kills_full_on_player_rating_per_mt,
		       source->stats.kills_full_on_player_rating_per_mt,
		       sizeof(destination->stats
				      .kills_full_on_player_rating_per_mt));
		memcpy(destination->stats.kills_shared_on_player_rating_per_mt,
		       source->stats.kills_shared_on_player_rating_per_mt,
		       sizeof(destination->stats
				      .kills_shared_on_player_rating_per_mt));
		memcpy(destination->stats.kills_assist_on_player_rating_per_mt,
		       source->stats.kills_assist_on_player_rating_per_mt,
		       sizeof(destination->stats
				      .kills_assist_on_player_rating_per_mt));
		memcpy(destination->stats.kills_full_on_ai_rating_per_mt,
		       source->stats.kills_full_on_ai_rating_per_mt,
		       sizeof(destination->stats
				      .kills_full_on_ai_rating_per_mt));
		memcpy(destination->stats.kills_shared_on_ai_rating_per_mt,
		       source->stats.kills_shared_on_ai_rating_per_mt,
		       sizeof(destination->stats
				      .kills_shared_on_ai_rating_per_mt));
		memcpy(destination->stats.kills_assist_on_ai_rating_per_mt,
		       source->stats.kills_assist_on_ai_rating_per_mt,
		       sizeof(destination->stats
				      .kills_assist_on_ai_rating_per_mt));
		memcpy(destination->stats.num_special_inspected_per_mt,
		       source->stats.num_special_inspected_per_mt,
		       sizeof(destination->stats.num_special_inspected_per_mt));
		memcpy(destination->stats.energy_hits_per_mt,
		       source->stats.energy_hits_per_mt,
		       sizeof(destination->stats.energy_hits_per_mt));
		memcpy(destination->stats.energy_fired_per_mt,
		       source->stats.energy_fired_per_mt,
		       sizeof(destination->stats.energy_fired_per_mt));
		memcpy(destination->stats.warheads_hits_per_mt,
		       source->stats.warheads_hits_per_mt,
		       sizeof(destination->stats.warheads_hits_per_mt));
		memcpy(destination->stats.warheads_fired_per_mt,
		       source->stats.warheads_fired_per_mt,
		       sizeof(destination->stats.warheads_fired_per_mt));
		memcpy(destination->stats.total_craft_losses_per_mt,
		       source->stats.total_craft_losses_per_mt,
		       sizeof(destination->stats.total_craft_losses_per_mt));
		memcpy(destination->stats.losses_by_collisions_per_mt,
		       source->stats.losses_by_collisions_per_mt,
		       sizeof(destination->stats.losses_by_collisions_per_mt));
		memcpy(destination->stats.losses_by_starships_per_mt,
		       source->stats.losses_by_starships_per_mt,
		       sizeof(destination->stats.losses_by_starships_per_mt));
		memcpy(destination->stats.losses_by_mines_per_mt,
		       source->stats.losses_by_mines_per_mt,
		       sizeof(destination->stats.losses_by_mines_per_mt));
		memcpy(destination->stats.killed_by_player_rating_per_mt,
		       source->stats.killed_by_player_rating_per_mt,
		       sizeof(destination->stats
				      .killed_by_player_rating_per_mt));
		memcpy(destination->stats.killed_by_ai_rating_per_mt,
		       source->stats.killed_by_ai_rating_per_mt,
		       sizeof(destination->stats.killed_by_ai_rating_per_mt));
		memcpy(destination->sp_training_data, source->field1558,
		       sizeof(destination->sp_training_data));
		memcpy(destination->sp_melee_data,
		       &source->sp_training_missions[99].field20,
		       sizeof(destination->sp_melee_data));
		memcpy(destination->sp_combat_data,
		       &source->sp_melee_missions[249].field20,
		       sizeof(destination->sp_combat_data));
		memcpy(destination->mp_training_data,
		       &source->sp_combat_missions[249].field20,
		       sizeof(destination->mp_training_data));
		memcpy(destination->mp_melee_data,
		       &source->mp_training_missions[99].field2c,
		       sizeof(destination->mp_melee_data));
		memcpy(destination->mp_combat_data,
		       &source->mp_melee_missions[249].field2c,
		       sizeof(destination->mp_combat_data));
		memcpy(destination->sp_tournament_data,
		       &source->mp_combat_missions[249].field2c,
		       sizeof(destination->sp_tournament_data));
		memcpy(destination->mp_tournament_data,
		       &source->sp_tournaments[24].field24,
		       sizeof(destination->mp_tournament_data));
		memcpy(destination->sp_battle_data,
		       &source->mp_tournaments[24].field28,
		       sizeof(destination->sp_battle_data));
		memcpy(destination->mp_battle_data,
		       &source->sp_battles[24].field20,
		       sizeof(destination->mp_battle_data));
	}
	XVT_LOG_DEBUG(
		"pilot.record_prepared rating=%d missions=%d score=%d faction=%d promo=%d percent=%d rank_change=%d",
		(int)record.rating, record.total_missions_played_count,
		record.total_score, record.current_faction_id,
		record.current_rating_promo_points,
		record.next_promotion_percent, (int)record.promotion_delta);

#ifdef XVT_MODERN
	(void)stream;
	return xvt_storage_write_atomic(file_name, &record, sizeof(record));
#else
	file_write_bytes(stream, &record, sizeof(record));
	file_change_to_install_path();
	file_close(stream);
	return 1;
#endif
}
