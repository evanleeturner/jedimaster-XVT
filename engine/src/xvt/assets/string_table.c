#include "xvt/assets/string_table.h"
#include "xvt/assets/file.h"

#include "xvt/assets/opt_model.h"
#include "xvt/flight/fediskio.h"
#include "xvt/flight/hud/hud.h"
#include "xvt/flight/hud/mfd.h"
#include "xvt/flight/hud/msg.h"
#include "xvt/flight/mission/goals.h"
#include "xvt/flight/object/damage.h"
#include "xvt/flight/proving_grounds.h"
#include "xvt/util/memory.h"
#include <string.h>

/* The four file error messages, indexed by file_error_string_id: the first four
 * lines of the block string_table_load_game_strings reads after the damage system
 * names, before g_str_disk_io_messages. The original build's fe_disk_io_fatal_error
 * and fe_disk_io_show_fatal_error_message_and_wait_key show them. */
// GLOBAL: XVT 0xA60A40
char *g_str_file_error_messages[4] = {0};

/* Reads strings.txt into the g_string_data_handle block and points the string
 * tables at its lines; does nothing when load_from_disk is 0. A file over 0x7D00
 * bytes gets a new block of its size in place of the old one. The tables come
 * in the file's order: g_str_damage_system_names; g_str_file_error_messages and
 * g_str_disk_io_messages, 36 lines; g_proving_grounds_status_labels;
 * g_str_goal_escape[0]; g_str_goal_cond_masculine, 188 rows of
 * g_goal_condition_text_variant_count[row % 47] lines; g_str_goal_percentages,
 * g_str_goal_operators, g_str_goal_titles, g_str_goal_conjunctions, g_str_goal_sides,
 * g_str_goal_family_names and g_str_goal_genus_names; g_str_map_room_text;
 * g_str_in_flight_messages; g_str_cmd_threat_display_text, g_str_waypoint_names,
 * g_str_mesh_component_names, g_str_cockpit_overlay_text, g_str_threat_display_text and
 * g_str_status_strings; g_str_warhead_names, then g_str_unknown;
 * g_str_sat_mine_probe_buoy_pilot_names; the 73 g_model_defs name_long entries;
 * g_str_species_names_plural; g_str_wingman_commands; then g_str_goal_cond_feminine and
 * g_str_goal_cond_neutered, laid out like the masculine rows, each read only when
 * some model has that gender. Lines starting with "//" are skipped, a final
 * newline is dropped, and a line of 1023 or more characters is read in pieces.
 * A model line starts with m, f or n, which sets g_craft_gender for it, and one
 * character more, which is dropped; any other first character ends the program
 * with FILE_ERROR_STR_PRESS_KEY_TO_EXIT. In an in-flight message a backslash
 * and the two characters after it become one byte: the last minus '0' when the
 * middle one is '0', else the last minus '('. When the file does not open or
 * ends early it calls FILE_RAW_CLOSE on the stream, even a NULL one, and ends
 * the program with FILE_ERROR_STR_STRINGS_OUT_OF_SYNC. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x4248B0
void string_table_load_game_strings(int load_from_disk)
{
	enum {
		STRING_LINE_CAPACITY = 1024,
		DEFAULT_STRING_DATA_CAPACITY = 0x7D00,
		FILE_AND_DISK_IO_STRING_COUNT = 36,
		FILE_ERROR_MESSAGE_COUNT = 4,
		MODEL_STRING_COUNT = 73,
		GOAL_CONDITION_TEXT_ROW_COUNT = 188,
		GOAL_CONDITIONS_PER_ROW_BLOCK = 47,
	};

	xvt_file *stream;
	int gender_used_by_any_craft;
	int entry_index;
	char *write_ptr;
	char **model_name;
	char line[STRING_LINE_CAPACITY];
	size_t file_size;
	int line_length;
	int condition_index;
	int variant_index;

	if (load_from_disk == 0) {
		return;
	}

	fe_disk_io_open_global_stream("strings.txt", "r", 1, 0);
	stream = (xvt_file *)g_stream;
	if (stream != NULL) {
		FILE_RAW_SEEK(stream, 0, SEEK_END);
		file_size = (size_t)FILE_RAW_TELL(stream);
		if (file_size > DEFAULT_STRING_DATA_CAPACITY) {
			memory_free_handle(g_string_data_handle);
			g_string_data_handle =
				memory_alloc_handle(file_size, 0);
			if (g_string_data_handle == 0) {
				fe_disk_io_fatal_error(
					FILE_ERROR_STR_NOT_ENOUGH_MEMORY);
			}
		}
		write_ptr =
			(char *)memory_get_handle_block(g_string_data_handle);
		FILE_RAW_SEEK(stream, 0, SEEK_SET);
		if (FILE_RAW_TELL(stream) != 0) {
			FILE_RAW_CLOSE(stream);
			fe_disk_io_open_global_stream("strings.txt", "r", 1, 0);
			stream = (xvt_file *)g_stream;
		}
		if (stream != NULL) {
			if (write_ptr != NULL) {
				for (entry_index = 0;
				     entry_index <
				     (int)(sizeof(g_str_damage_system_names) /
					   sizeof(g_str_damage_system_names
							  [0]));
				     ++entry_index) {
					int length;

					length =
						string_table_read_non_comment_line(
							stream, line);
					if (length == -1) {
						write_ptr = NULL;
						break;
					}
					memcpy(write_ptr, line,
					       (size_t)length + 1);
					g_str_damage_system_names[entry_index] =
						write_ptr;
					write_ptr += length + 1;
				}
			}

			if (write_ptr != NULL) {
				entry_index = 0;
				while (entry_index <
				       FILE_AND_DISK_IO_STRING_COUNT) {
					if (FILE_GETS(line, sizeof(line),
						      stream) == NULL) {
						write_ptr = NULL;
						break;
					}
					line[sizeof(line) - 1] = '\0';
					if (line[0] == '/' && line[1] == '/') {
						continue;
					}
					line_length = (int)strlen(line);
					if (line[line_length - 1] == '\n') {
						line[--line_length] = '\0';
					}
					if (entry_index <
					    FILE_ERROR_MESSAGE_COUNT) {
						memcpy(write_ptr, line,
						       line_length + 1);
						g_str_file_error_messages
							[entry_index] =
								write_ptr;
					} else {
						memcpy(write_ptr, line,
						       line_length + 1);
						g_str_disk_io_messages
							[entry_index -
							 FILE_ERROR_MESSAGE_COUNT] =
								write_ptr;
					}
					write_ptr += line_length + 1;
					++entry_index;
				}
			}

			if (write_ptr != NULL) {
				entry_index = 0;
				while (entry_index <
				       (int)(sizeof(g_proving_grounds_status_labels) /
					     sizeof(g_proving_grounds_status_labels
							    [0]))) {
					if (FILE_GETS(line, sizeof(line),
						      stream) == NULL) {
						write_ptr = NULL;
						break;
					}
					line[sizeof(line) - 1] = '\0';
					if (line[0] == '/' && line[1] == '/') {
						continue;
					}
					line_length = (int)strlen(line);
					if (line[line_length - 1] == '\n') {
						line[--line_length] = '\0';
					}
					memcpy(write_ptr, line,
					       line_length + 1);
					g_proving_grounds_status_labels
						[entry_index] = write_ptr;
					write_ptr += line_length + 1;
					++entry_index;
				}
			}

			if (write_ptr != NULL) {
				entry_index = 0;
				while (entry_index < 1) {
					if (FILE_GETS(line, sizeof(line),
						      stream) == NULL) {
						write_ptr = NULL;
						break;
					}
					line[sizeof(line) - 1] = '\0';
					if (line[0] == '/' && line[1] == '/') {
						continue;
					}
					line_length = (int)strlen(line);
					if (line[line_length - 1] == '\n') {
						line[--line_length] = '\0';
					}
					memcpy(write_ptr, line,
					       line_length + 1);
					g_str_goal_escape[0] = write_ptr;
					write_ptr += line_length + 1;
					++entry_index;
				}
			}

			if (write_ptr != NULL) {
				for (entry_index = 0;
				     entry_index <
				     GOAL_CONDITION_TEXT_ROW_COUNT;
				     ++entry_index) {
					condition_index =
						entry_index %
						GOAL_CONDITIONS_PER_ROW_BLOCK;
					variant_index = 0;
					while (variant_index <
					       g_goal_condition_text_variant_count
						       [condition_index]) {
						if (FILE_GETS(line,
							      sizeof(line),
							      stream) == NULL) {
							write_ptr = NULL;
							break;
						}
						line[sizeof(line) - 1] = '\0';
						if (line[0] == '/' &&
						    line[1] == '/') {
							continue;
						}
						line_length = (int)strlen(line);
						if (line[line_length - 1] ==
						    '\n') {
							line[--line_length] =
								'\0';
						}
						memcpy(write_ptr, line,
						       line_length + 1);
						g_str_goal_cond_masculine
							[entry_index]
							[variant_index] =
								write_ptr;
						write_ptr += line_length + 1;
						++variant_index;
					}
					if (write_ptr == NULL) {
						break;
					}
				}
			}

			if (write_ptr != NULL) {
				entry_index = 0;
				while (entry_index <
				       (int)(sizeof(g_str_goal_percentages) /
					     sizeof(g_str_goal_percentages
							    [0]))) {
					if (FILE_GETS(line, sizeof(line),
						      stream) == NULL) {
						write_ptr = NULL;
						break;
					}
					line[sizeof(line) - 1] = '\0';
					if (line[0] == '/' && line[1] == '/') {
						continue;
					}
					line_length = (int)strlen(line);
					if (line[line_length - 1] == '\n') {
						line[--line_length] = '\0';
					}
					memcpy(write_ptr, line,
					       line_length + 1);
					g_str_goal_percentages[entry_index] =
						write_ptr;
					write_ptr += line_length + 1;
					++entry_index;
				}
			}

			if (write_ptr != NULL) {
				entry_index = 0;
				while (entry_index <
				       (int)(sizeof(g_str_goal_operators) /
					     sizeof(g_str_goal_operators[0]))) {
					if (FILE_GETS(line, sizeof(line),
						      stream) == NULL) {
						write_ptr = NULL;
						break;
					}
					line[sizeof(line) - 1] = '\0';
					if (line[0] == '/' && line[1] == '/') {
						continue;
					}
					line_length = (int)strlen(line);
					if (line[line_length - 1] == '\n') {
						line[--line_length] = '\0';
					}
					memcpy(write_ptr, line,
					       line_length + 1);
					g_str_goal_operators[entry_index] =
						write_ptr;
					write_ptr += line_length + 1;
					++entry_index;
				}
			}

			if (write_ptr != NULL) {
				entry_index = 0;
				while (entry_index <
				       (int)(sizeof(g_str_goal_titles) /
					     sizeof(g_str_goal_titles[0]))) {
					if (FILE_GETS(line, sizeof(line),
						      stream) == NULL) {
						write_ptr = NULL;
						break;
					}
					line[sizeof(line) - 1] = '\0';
					if (line[0] == '/' && line[1] == '/') {
						continue;
					}
					line_length = (int)strlen(line);
					if (line[line_length - 1] == '\n') {
						line[--line_length] = '\0';
					}
					memcpy(write_ptr, line,
					       line_length + 1);
					g_str_goal_titles[entry_index] =
						write_ptr;
					write_ptr += line_length + 1;
					++entry_index;
				}
			}

			if (write_ptr != NULL) {
				entry_index = 0;
				while (entry_index <
				       (int)(sizeof(g_str_goal_conjunctions) /
					     sizeof(g_str_goal_conjunctions
							    [0]))) {
					if (FILE_GETS(line, sizeof(line),
						      stream) == NULL) {
						write_ptr = NULL;
						break;
					}
					line[sizeof(line) - 1] = '\0';
					if (line[0] == '/' && line[1] == '/') {
						continue;
					}
					line_length = (int)strlen(line);
					if (line[line_length - 1] == '\n') {
						line[--line_length] = '\0';
					}
					memcpy(write_ptr, line,
					       line_length + 1);
					g_str_goal_conjunctions[entry_index] =
						write_ptr;
					write_ptr += line_length + 1;
					++entry_index;
				}
			}

			if (write_ptr != NULL) {
				entry_index = 0;
				while (entry_index <
				       (int)(sizeof(g_str_goal_sides) /
					     sizeof(g_str_goal_sides[0]))) {
					if (FILE_GETS(line, sizeof(line),
						      stream) == NULL) {
						write_ptr = NULL;
						break;
					}
					line[sizeof(line) - 1] = '\0';
					if (line[0] == '/' && line[1] == '/') {
						continue;
					}
					line_length = (int)strlen(line);
					if (line[line_length - 1] == '\n') {
						line[--line_length] = '\0';
					}
					memcpy(write_ptr, line,
					       line_length + 1);
					g_str_goal_sides[entry_index] =
						write_ptr;
					write_ptr += line_length + 1;
					++entry_index;
				}
			}

			if (write_ptr != NULL) {
				entry_index = 0;
				while (entry_index <
				       (int)(sizeof(g_str_goal_family_names) /
					     sizeof(g_str_goal_family_names
							    [0]))) {
					if (FILE_GETS(line, sizeof(line),
						      stream) == NULL) {
						write_ptr = NULL;
						break;
					}
					line[sizeof(line) - 1] = '\0';
					if (line[0] == '/' && line[1] == '/') {
						continue;
					}
					line_length = (int)strlen(line);
					if (line[line_length - 1] == '\n') {
						line[--line_length] = '\0';
					}
					memcpy(write_ptr, line,
					       line_length + 1);
					g_str_goal_family_names[entry_index] =
						write_ptr;
					write_ptr += line_length + 1;
					++entry_index;
				}
			}

			if (write_ptr != NULL) {
				entry_index = 0;
				while (entry_index <
				       (int)(sizeof(g_str_goal_genus_names) /
					     sizeof(g_str_goal_genus_names
							    [0]))) {
					if (FILE_GETS(line, sizeof(line),
						      stream) == NULL) {
						write_ptr = NULL;
						break;
					}
					line[sizeof(line) - 1] = '\0';
					if (line[0] == '/' && line[1] == '/') {
						continue;
					}
					line_length = (int)strlen(line);
					if (line[line_length - 1] == '\n') {
						line[--line_length] = '\0';
					}
					memcpy(write_ptr, line,
					       line_length + 1);
					g_str_goal_genus_names[entry_index] =
						write_ptr;
					write_ptr += line_length + 1;
					++entry_index;
				}
			}

			if (write_ptr != NULL) {
				entry_index = 0;
				while (entry_index <
				       (int)(sizeof(g_str_map_room_text) /
					     sizeof(g_str_map_room_text[0]))) {
					if (FILE_GETS(line, sizeof(line),
						      stream) == NULL) {
						write_ptr = NULL;
						break;
					}
					line[sizeof(line) - 1] = '\0';
					if (line[0] == '/' && line[1] == '/') {
						continue;
					}
					line_length = (int)strlen(line);
					if (line[line_length - 1] == '\n') {
						line[--line_length] = '\0';
					}
					memcpy(write_ptr, line,
					       line_length + 1);
					g_str_map_room_text[entry_index] =
						write_ptr;
					write_ptr += line_length + 1;
					++entry_index;
				}
			}

			if (write_ptr != NULL) {
				entry_index = 0;
				while (entry_index <
				       (int)(sizeof(g_str_in_flight_messages) /
					     sizeof(g_str_in_flight_messages
							    [0]))) {
					int source_index;
					char *read_ptr;

					if (FILE_GETS(line, sizeof(line),
						      stream) == NULL) {
						write_ptr = NULL;
						break;
					}
					line[sizeof(line) - 1] = '\0';
					if (line[0] == '/' && line[1] == '/') {
						continue;
					}
					line_length = (int)strlen(line);
					if (line[line_length - 1] == '\n') {
						line[--line_length] = '\0';
					}
					g_str_in_flight_messages[entry_index] =
						write_ptr;
					read_ptr = line;
					source_index = 0;
					while (line_length > source_index) {
						if (*read_ptr == '\\') {
							if (read_ptr[1] ==
							    '0') {
								*write_ptr++ =
									(char)(read_ptr[2] -
									       '0');
							} else {
								*write_ptr++ =
									(char)(read_ptr[2] -
									       '(');
							}
							read_ptr += 3;
							source_index += 3;
						} else {
							*write_ptr++ =
								*read_ptr++;
							++source_index;
						}
					}
					*write_ptr++ = '\0';
					++entry_index;
				}
			}

			if (write_ptr != NULL) {
				entry_index = 0;
				while (entry_index <
				       (int)(sizeof(g_str_cmd_threat_display_text) /
					     sizeof(g_str_cmd_threat_display_text
							    [0]))) {
					if (FILE_GETS(line, sizeof(line),
						      stream) == NULL) {
						write_ptr = NULL;
						break;
					}
					line[sizeof(line) - 1] = '\0';
					if (line[0] == '/' && line[1] == '/') {
						continue;
					}
					line_length = (int)strlen(line);
					if (line[line_length - 1] == '\n') {
						line[--line_length] = '\0';
					}
					memcpy(write_ptr, line,
					       line_length + 1);
					g_str_cmd_threat_display_text
						[entry_index] = write_ptr;
					write_ptr += line_length + 1;
					++entry_index;
				}
			}

			if (write_ptr != NULL) {
				entry_index = 0;
				while (entry_index <
				       (int)(sizeof(g_str_waypoint_names) /
					     sizeof(g_str_waypoint_names[0]))) {
					if (FILE_GETS(line, sizeof(line),
						      stream) == NULL) {
						write_ptr = NULL;
						break;
					}
					line[sizeof(line) - 1] = '\0';
					if (line[0] == '/' && line[1] == '/') {
						continue;
					}
					line_length = (int)strlen(line);
					if (line[line_length - 1] == '\n') {
						line[--line_length] = '\0';
					}
					memcpy(write_ptr, line,
					       line_length + 1);
					g_str_waypoint_names[entry_index] =
						write_ptr;
					write_ptr += line_length + 1;
					++entry_index;
				}
			}

			if (write_ptr != NULL) {
				entry_index = 0;
				while (entry_index <
				       (int)(sizeof(g_str_mesh_component_names) /
					     sizeof(g_str_mesh_component_names
							    [0]))) {
					if (FILE_GETS(line, sizeof(line),
						      stream) == NULL) {
						write_ptr = NULL;
						break;
					}
					line[sizeof(line) - 1] = '\0';
					if (line[0] == '/' && line[1] == '/') {
						continue;
					}
					line_length = (int)strlen(line);
					if (line[line_length - 1] == '\n') {
						line[--line_length] = '\0';
					}
					memcpy(write_ptr, line,
					       line_length + 1);
					g_str_mesh_component_names
						[entry_index] = write_ptr;
					write_ptr += line_length + 1;
					++entry_index;
				}
			}

			if (write_ptr != NULL) {
				entry_index = 0;
				while (entry_index <
				       (int)(sizeof(g_str_cockpit_overlay_text) /
					     sizeof(g_str_cockpit_overlay_text
							    [0]))) {
					if (FILE_GETS(line, sizeof(line),
						      stream) == NULL) {
						write_ptr = NULL;
						break;
					}
					line[sizeof(line) - 1] = '\0';
					if (line[0] == '/' && line[1] == '/') {
						continue;
					}
					line_length = (int)strlen(line);
					if (line[line_length - 1] == '\n') {
						line[--line_length] = '\0';
					}
					memcpy(write_ptr, line,
					       line_length + 1);
					g_str_cockpit_overlay_text
						[entry_index] = write_ptr;
					write_ptr += line_length + 1;
					++entry_index;
				}
			}

			if (write_ptr != NULL) {
				entry_index = 0;
				while (entry_index <
				       (int)(sizeof(g_str_threat_display_text) /
					     sizeof(g_str_threat_display_text
							    [0]))) {
					if (FILE_GETS(line, sizeof(line),
						      stream) == NULL) {
						write_ptr = NULL;
						break;
					}
					line[sizeof(line) - 1] = '\0';
					if (line[0] == '/' && line[1] == '/') {
						continue;
					}
					line_length = (int)strlen(line);
					if (line[line_length - 1] == '\n') {
						line[--line_length] = '\0';
					}
					memcpy(write_ptr, line,
					       line_length + 1);
					g_str_threat_display_text[entry_index] =
						write_ptr;
					write_ptr += line_length + 1;
					++entry_index;
				}
			}

			if (write_ptr != NULL) {
				entry_index = 0;
				while (entry_index <
				       (int)(sizeof(g_str_status_strings) /
					     sizeof(g_str_status_strings[0]))) {
					if (FILE_GETS(line, sizeof(line),
						      stream) == NULL) {
						write_ptr = NULL;
						break;
					}
					line[sizeof(line) - 1] = '\0';
					if (line[0] == '/' && line[1] == '/') {
						continue;
					}
					line_length = (int)strlen(line);
					if (line[line_length - 1] == '\n') {
						line[--line_length] = '\0';
					}
					memcpy(write_ptr, line,
					       line_length + 1);
					g_str_status_strings[entry_index] =
						write_ptr;
					write_ptr += line_length + 1;
					++entry_index;
				}
			}

			if (write_ptr != NULL) {
				entry_index = 0;
				while (entry_index <
				       (int)(sizeof(g_str_warhead_names) /
					     sizeof(g_str_warhead_names[0])) +
					       1) {
					if (FILE_GETS(line, sizeof(line),
						      stream) == NULL) {
						write_ptr = NULL;
						break;
					}
					line[sizeof(line) - 1] = '\0';
					if (line[0] == '/' && line[1] == '/') {
						continue;
					}
					line_length = (int)strlen(line);
					if (line[line_length - 1] == '\n') {
						line[--line_length] = '\0';
					}
					if (entry_index ==
					    (int)(sizeof(g_str_warhead_names) /
						  sizeof(g_str_warhead_names
								 [0]))) {
						memcpy(write_ptr, line,
						       line_length + 1);
						g_str_unknown = write_ptr;
					} else {
						memcpy(write_ptr, line,
						       line_length + 1);
						g_str_warhead_names
							[entry_index] =
								write_ptr;
					}
					write_ptr += line_length + 1;
					++entry_index;
				}
			}

			if (write_ptr != NULL) {
				entry_index = 0;
				while (entry_index <
				       (int)(sizeof(g_str_sat_mine_probe_buoy_pilot_names) /
					     sizeof(g_str_sat_mine_probe_buoy_pilot_names
							    [0]))) {
					if (FILE_GETS(line, sizeof(line),
						      stream) == NULL) {
						write_ptr = NULL;
						break;
					}
					line[sizeof(line) - 1] = '\0';
					if (line[0] == '/' && line[1] == '/') {
						continue;
					}
					line_length = (int)strlen(line);
					if (line[line_length - 1] == '\n') {
						line[--line_length] = '\0';
					}
					memcpy(write_ptr, line,
					       line_length + 1);
					g_str_sat_mine_probe_buoy_pilot_names
						[entry_index] = write_ptr;
					write_ptr += line_length + 1;
					++entry_index;
				}
			}

			if (write_ptr != NULL) {
				for (entry_index = 0;
				     entry_index < MODEL_STRING_COUNT;) {
					model_name = &g_model_defs[entry_index]
							      .name_long;
					if (FILE_GETS(line, sizeof(line),
						      stream) == NULL) {
						write_ptr = NULL;
						break;
					}
					line[sizeof(line) - 1] = '\0';
					if (line[0] == '/' && line[1] == '/') {
						continue;
					}
					line_length = (int)strlen(line);
					if (line[line_length - 1] == '\n') {
						line[--line_length] = '\0';
					}
					*model_name = write_ptr;
					if (line[0] == 'm') {
						g_craft_gender[entry_index] =
							CRAFT_GENDER_MASCULINE;
					} else if (line[0] == 'f') {
						g_craft_gender[entry_index] =
							CRAFT_GENDER_FEMININE;
					} else if (line[0] == 'n') {
						g_craft_gender[entry_index] =
							CRAFT_GENDER_NEUTERED;
					} else {
						fe_disk_io_fatal_error(
							FILE_ERROR_STR_PRESS_KEY_TO_EXIT);
					}
					memcpy(write_ptr, &line[2],
					       line_length - 1);
					write_ptr += line_length - 1;
					++entry_index;
				}
			}

			if (write_ptr != NULL) {
				entry_index = 0;
				while (entry_index <
				       (int)(sizeof(g_str_species_names_plural) /
					     sizeof(g_str_species_names_plural
							    [0]))) {
					if (FILE_GETS(line, sizeof(line),
						      stream) == NULL) {
						write_ptr = NULL;
						break;
					}
					line[sizeof(line) - 1] = '\0';
					if (line[0] == '/' && line[1] == '/') {
						continue;
					}
					line_length = (int)strlen(line);
					if (line[line_length - 1] == '\n') {
						line[--line_length] = '\0';
					}
					memcpy(write_ptr, line,
					       line_length + 1);
					g_str_species_names_plural
						[entry_index] = write_ptr;
					write_ptr += line_length + 1;
					++entry_index;
				}
			}

			if (write_ptr != NULL) {
				entry_index = 0;
				while (entry_index <
				       (int)(sizeof(g_str_wingman_commands) /
					     sizeof(g_str_wingman_commands
							    [0]))) {
					if (FILE_GETS(line, sizeof(line),
						      stream) == NULL) {
						write_ptr = NULL;
						break;
					}
					line[sizeof(line) - 1] = '\0';
					if (line[0] == '/' && line[1] == '/') {
						continue;
					}
					line_length = (int)strlen(line);
					if (line[line_length - 1] == '\n') {
						line[--line_length] = '\0';
					}
					memcpy(write_ptr, line,
					       line_length + 1);
					g_str_wingman_commands[entry_index] =
						write_ptr;
					write_ptr += line_length + 1;
					++entry_index;
				}
			}

			if (write_ptr != NULL) {
				entry_index = 0;
				gender_used_by_any_craft = 0;
				while (entry_index < MODEL_STRING_COUNT) {
					if (g_craft_gender[entry_index] ==
					    CRAFT_GENDER_FEMININE) {
						gender_used_by_any_craft = 1;
						break;
					}
					++entry_index;
				}
				if (gender_used_by_any_craft != 0) {
					for (entry_index = 0;
					     entry_index <
					     GOAL_CONDITION_TEXT_ROW_COUNT;
					     ++entry_index) {
						condition_index =
							entry_index %
							GOAL_CONDITIONS_PER_ROW_BLOCK;
						variant_index = 0;
						while (variant_index <
						       g_goal_condition_text_variant_count
							       [condition_index]) {
							if (FILE_GETS(
								    line,
								    sizeof(line),
								    stream) ==
							    NULL) {
								write_ptr =
									NULL;
								break;
							}
							line[sizeof(line) - 1] =
								'\0';
							if (line[0] == '/' &&
							    line[1] == '/') {
								continue;
							}
							line_length =
								(int)strlen(
									line);
							if (line[line_length -
								 1] == '\n') {
								line[--line_length] =
									'\0';
							}
							memcpy(write_ptr, line,
							       line_length + 1);
							g_str_goal_cond_feminine
								[entry_index]
								[variant_index] =
									write_ptr;
							write_ptr +=
								line_length + 1;
							++variant_index;
						}
						if (write_ptr == NULL) {
							break;
						}
					}
				}
			}

			if (write_ptr != NULL) {
				entry_index = 0;
				gender_used_by_any_craft = 0;
				while (entry_index < MODEL_STRING_COUNT) {
					if (g_craft_gender[entry_index] ==
					    CRAFT_GENDER_NEUTERED) {
						gender_used_by_any_craft = 1;
						break;
					}
					++entry_index;
				}
				if (gender_used_by_any_craft != 0) {
					for (entry_index = 0;
					     entry_index <
					     GOAL_CONDITION_TEXT_ROW_COUNT;
					     ++entry_index) {
						condition_index =
							entry_index %
							GOAL_CONDITIONS_PER_ROW_BLOCK;
						variant_index = 0;
						while (variant_index <
						       g_goal_condition_text_variant_count
							       [condition_index]) {
							if (FILE_GETS(
								    line,
								    sizeof(line),
								    stream) ==
							    NULL) {
								write_ptr =
									NULL;
								break;
							}
							line[sizeof(line) - 1] =
								'\0';
							if (line[0] == '/' &&
							    line[1] == '/') {
								continue;
							}
							line_length =
								(int)strlen(
									line);
							if (line[line_length -
								 1] == '\n') {
								line[--line_length] =
									'\0';
							}
							memcpy(write_ptr, line,
							       line_length + 1);
							g_str_goal_cond_neutered
								[entry_index]
								[variant_index] =
									write_ptr;
							write_ptr +=
								line_length + 1;
							++variant_index;
						}
						if (write_ptr == NULL) {
							break;
						}
					}
				}
			}

			if (write_ptr != NULL) {
				FILE_RAW_CLOSE(stream);
				return;
			}
		}
	}
	FILE_RAW_CLOSE(stream);
	fe_disk_io_fatal_error(FILE_ERROR_STR_STRINGS_OUT_OF_SYNC);
}

/* Reads into buffer, 1024 bytes, the next line that does not start with "//",
 * drops its final newline and returns its length; -1 at the end of the file. A
 * line of 1023 or more characters comes back in pieces. Only
 * string_table_load_game_strings calls this, for g_str_damage_system_names. */
// FUNCTION: XVT 0x425A70
int string_table_read_non_comment_line(xvt_file *stream, char *buffer)
{
	int line_length;

	do {
		if (FILE_GETS(buffer, 1024, stream) == 0) {
			return -1;
		}
		buffer[1023] = 0;
	} while (buffer[0] == '/' && buffer[1] == '/');

	line_length = strlen(buffer);
	if (buffer[line_length - 1] == '\n') {
		buffer[line_length - 1] = 0;
		--line_length;
	}
	return line_length;
}
