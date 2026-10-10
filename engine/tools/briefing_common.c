/* What the briefing tools share: see briefing_common.h. */
#define _DEFAULT_SOURCE

#include "briefing_common.h"

#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "xvt/frontend/briefing_map.h"
#include "xvt/frontend/briefing_script.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/frontend_mission_list.h"
#include "xvt/frontend/mission_briefing.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/frontend/pilot_record.h"

const char BRIEFING_LINK_NAME[] = "zzbriefing.tie";

static struct mission_list_entry g_list_entry;

void __wrap_mission_setup_load_mission_list(int mission_directory_id);
void __wrap_mission_setup_load_mission_list(int mission_directory_id)
{
	(void)mission_directory_id;
	memset(&g_list_entry, 0, sizeof(g_list_entry));
	snprintf(g_list_entry.file_name, sizeof(g_list_entry.file_name), "%s",
		 BRIEFING_LINK_NAME);
	g_list_entry.mission_idx = 0;
	g_mission_list = &g_list_entry;
	g_mission_count = 1;
}

int briefing_make_link_folder(const char *program, const char *root,
			      const char *file, int directory, char *folder,
			      size_t size)
{
	const char *directory_name = g_mission_directory_names[directory];
	char real_root[PATH_MAX];
	char real_file[PATH_MAX];
	char from[PATH_MAX * 2];
	char to[PATH_MAX * 2];
	if (realpath(root, real_root) == NULL ||
	    realpath(file, real_file) == NULL) {
		fprintf(stderr, "%s: %s or %s: not found\n", program, root,
			file);
		return 0;
	}
	snprintf(folder, size, "/tmp/%s_root_XXXXXX", program);
	if (mkdtemp(folder) == NULL) {
		fprintf(stderr, "%s: cannot make a folder in /tmp\n", program);
		return 0;
	}
	DIR *listing = opendir(real_root);
	if (listing == NULL) {
		fprintf(stderr, "%s: %s: cannot list\n", program, real_root);
		return 0;
	}
	const struct dirent *entry;
	while ((entry = readdir(listing)) != NULL) {
		if (strcmp(entry->d_name, ".") == 0 ||
		    strcmp(entry->d_name, "..") == 0 ||
		    strcasecmp(entry->d_name, directory_name) == 0) {
			continue;
		}
		snprintf(from, sizeof(from), "%s/%s", real_root, entry->d_name);
		snprintf(to, sizeof(to), "%s/%s", folder, entry->d_name);
		if (symlink(from, to) != 0) {
			closedir(listing);
			fprintf(stderr, "%s: cannot link %s\n", program, to);
			return 0;
		}
	}
	closedir(listing);
	snprintf(to, sizeof(to), "%s/%s", folder, directory_name);
	if (mkdir(to, 0700) != 0) {
		fprintf(stderr, "%s: cannot make %s\n", program, to);
		return 0;
	}
	snprintf(to, sizeof(to), "%s/%s/%s", folder, directory_name,
		 BRIEFING_LINK_NAME);
	if (symlink(real_file, to) != 0) {
		fprintf(stderr, "%s: cannot link %s\n", program, to);
		return 0;
	}
	return 1;
}

void briefing_set_pilot(int team, int directory)
{
	g_pilot_data.team = team;
	g_pilot_data.mission_directory_id = (mission_directory_id)directory;
	g_pilot_data.mission_description_ids[0] = 0;
	g_game_config.sfx_datapad_enabled = 1;
	g_game_config.sfx_datapad_volume = 8;
}

void briefing_print_state(int frame)
{
	printf("state %d play=%d t=%d c=%d,%d tc=%d,%d s=%d,%d ts=%d,%d page=%d last=%d slots=%d:%d,%d:%d markers=",
	       frame, (int)g_briefing_playback_active,
	       (int)g_briefing_script.current_frame,
	       (int)g_briefing_map_center.x, (int)g_briefing_map_center.y,
	       (int)g_briefing_map_target_center.x,
	       (int)g_briefing_map_target_center.y, (int)g_briefing_map_scale.x,
	       (int)g_briefing_map_scale.y, (int)g_briefing_map_target_scale.x,
	       (int)g_briefing_map_target_scale.y, g_briefing_text_page_number,
	       g_briefing_last_narrated_text_block_idx,
	       (int)g_briefing_text_slot_active[0],
	       (int)g_briefing_text_slot_block_idx[0],
	       (int)g_briefing_text_slot_active[1],
	       (int)g_briefing_text_slot_block_idx[1]);
	const char *separator = "";
	for (int slot = 0; slot < 8; ++slot) {
		if (g_briefing_map_fg_marker_active[slot] != 0) {
			printf("%s%d:%d:%d", separator, slot,
			       (int)g_briefing_map_fg_marker_flight_group_idx
				       [slot],
			       (int)g_briefing_map_fg_marker_age[slot]);
			separator = ";";
		}
	}
	printf("%s labels=", *separator == '\0' ? "-" : "");
	separator = "";
	for (int slot = 0; slot < 8; ++slot) {
		if (g_briefing_map_label_active[slot] != 0) {
			printf("%s%d:%d:%d:%d:%d:%d", separator, slot,
			       (int)g_briefing_map_label_text_idx[slot],
			       (int)g_briefing_map_label_x[slot],
			       (int)g_briefing_map_label_y[slot],
			       (int)g_briefing_map_label_style[slot],
			       (int)g_briefing_map_label_age[slot]);
			separator = ";";
		}
	}
	printf("%s\n", *separator == '\0' ? "-" : "");
}
