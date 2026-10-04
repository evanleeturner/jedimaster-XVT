#include "xvt/frontend/frontend_string.h"

#include <stdlib.h>
#include <string.h>

#include "xvt/assets/file.h"
#include "xvt/frontend/frontend_state.h"

/* Loads the frontend string table from the text file fileName, replacing the
 * one loaded before: each line that does not start with //, read up to 1,023
 * characters at a time with its line feed dropped, becomes the next entry.
 * Writes g_front_state.ui_string_data, one heap block holding the strings end to
 * end, shrunk to fit at the end; ui_string_offsets, each entry's byte offset in
 * it, grown 64 entries at a time; ui_string_count and ui_string_capacity. Keeps the
 * old table when the file does not open. Leaves an empty table when the first
 * allocations fail or the final shrink returns NULL; a failed growth of the
 * offsets stops the reading with the table full. */
// FUNCTION: XVT 0x4DD9D0
void frontend_string_load_table(const char *file_name)
{
	unsigned int total_data_size = 0;
	xvt_file *stream = file_open(file_name, "r");
	if (stream == NULL) {
		return;
	}

	frontend_string_unload_table();
	g_front_state.ui_string_offsets =
		malloc(64 * sizeof(*g_front_state.ui_string_offsets));
	if (g_front_state.ui_string_offsets == NULL) {
		file_close(stream);
		return;
	}

	g_front_state.ui_string_capacity = 64;
	g_front_state.ui_string_data = malloc(file_get_size(stream));
	if (g_front_state.ui_string_data == NULL) {
		frontend_string_unload_table();
		file_close(stream);
		return;
	}

	char *write_position = g_front_state.ui_string_data;
	char line[1024];
	for (;;) {
		if (FILE_GETS(line, sizeof(line), stream) == NULL) {
			break;
		}
		line[sizeof(line) - 1] = '\0';
		if (line[0] != '/' || line[1] != '/') {
			unsigned int line_length = strlen(line) + 1;
			unsigned int copy_length = line_length - 1;
			if (line[line_length - 2] == '\n') {
				--copy_length;
				line[line_length - 2] = '\0';
			}

			unsigned int string_offset =
				write_position - g_front_state.ui_string_data;
			char *copy_destination = write_position;
			total_data_size += copy_length + 1;
			write_position += copy_length + 1;
			g_front_state.ui_string_offsets
				[g_front_state.ui_string_count] = string_offset;
			memcpy(copy_destination, line, copy_length + 1);
			++g_front_state.ui_string_count;
			if (g_front_state.ui_string_capacity ==
			    g_front_state.ui_string_count) {
				unsigned int *resized_offsets = realloc(
					g_front_state.ui_string_offsets,
					(g_front_state.ui_string_capacity +
					 64) * sizeof(*g_front_state
							       .ui_string_offsets));
				if (resized_offsets == NULL) {
					break;
				}
				g_front_state.ui_string_offsets =
					resized_offsets;
				g_front_state.ui_string_capacity += 64;
			}
		}
	}

	char *resized_data =
		realloc(g_front_state.ui_string_data, total_data_size);
	if (resized_data != NULL) {
		g_front_state.ui_string_data = resized_data;
	} else {
		free(g_front_state.ui_string_data);
		free(g_front_state.ui_string_offsets);
		g_front_state.ui_string_count = 0;
		g_front_state.ui_string_data = NULL;
		g_front_state.ui_string_offsets = NULL;
	}

	file_close(stream);
}

/* Frees the frontend string table and sets g_front_state.ui_string_offsets and
 * ui_string_data to NULL and ui_string_count and ui_string_capacity to 0. */
// FUNCTION: XVT 0x4DDBC0
void frontend_string_unload_table(void)
{
	unsigned int **offsets = &g_front_state.ui_string_offsets;
	char **data = &g_front_state.ui_string_data;
	if (*offsets) {
		free(*offsets);
		*offsets = 0;
	}

	if (*data) {
		free(*data);
		*data = 0;
	}

	g_front_state.ui_string_count = 0;
	g_front_state.ui_string_capacity = 0;
}

/* Returns entry index of the frontend string table, or the text "No text." when
 * index, read as unsigned, is not under g_front_state.ui_string_count. */
// FUNCTION: XVT 0x4DDC10
const char *frontend_string_get(frontend_string_id index)
{
	if ((unsigned int)index >= g_front_state.ui_string_count) {
		return "No text.";
	}
	return &g_front_state
			.ui_string_data[g_front_state.ui_string_offsets[index]];
}
