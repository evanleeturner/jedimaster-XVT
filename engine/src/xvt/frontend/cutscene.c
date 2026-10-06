#include "xvt/frontend/cutscene.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xvt/assets/file.h"
#include "xvt/audio/cd_audio.h"
#include "xvt/frontend/frontend.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/frontend_draw.h"
#include "xvt/frontend/frontend_mission_list.h"
#include "xvt/frontend/movie.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt_runtime/log/log.h"
#include "xvt_runtime/runtime/cutscene_task.h"

/* Entries loaded in g_cutscene_table. cutscene_load_table sets it once it has
 * a table; game_main in the original build and xvt_frontend_task_shutdown in the
 * modern one set 0 when they free the table. */
// GLOBAL: XVT 0xB69E34
int g_cutscene_count = 0;
/* Heap table of the campaign cutscenes, read from movies\cutscene.lst by
 * cutscene_load_table when frontend_load_resources runs; NULL before that and
 * after a failed load. */
// GLOBAL: XVT 0xB6A248
struct cutscene_entry *g_cutscene_table = NULL;

/* Reads a cutscene list into g_cutscene_table, freeing the old table first.
 * The first line gives the entry count, for which it allocates a zeroed
 * table. Each entry is four lines, skipping lines that start with "//": the
 * movie name, three numbers (campaign_id, play_after_debriefing,
 * campaign_mission_id), the thumbnail image name and the description. Returns
 * 0, with g_cutscene_count unchanged, when the file does not open, is empty or
 * the table cannot be allocated; that last case leaves the file open.
 * Otherwise returns 1 with g_cutscene_count at the entries read: it stops
 * early at the end of the file, and at a numbers line without three numbers,
 * which it clears. The names are copied as fixed 128-, 32- and 128-byte
 * blocks, so a longer line leaves one without its terminator. */
// FUNCTION: XVT 0x4DF250
int cutscene_load_table(const char *file_name)
{
	if (g_cutscene_table != NULL) {
		free(g_cutscene_table);
		g_cutscene_table = NULL;
	}

	xvt_file *stream = file_open(file_name, "r");
	if (stream == NULL) {
		XVT_LOG_WARN("cutscene.table_open_failed file=\"%s\"",
			     file_name);
		return 0;
	}
	if (FILE_GETS(g_frontend_scratch_buffer, 255, stream) == NULL) {
		XVT_LOG_WARN("cutscene.table_empty file=\"%s\"", file_name);
		file_close(stream);
		return 0;
	}

	unsigned int declared_count =
		(unsigned int)atoi(g_frontend_scratch_buffer);
	size_t allocation_size = sizeof(struct cutscene_entry) * declared_count;
	g_cutscene_table = (struct cutscene_entry *)malloc(allocation_size);
	if (g_cutscene_table == NULL) {
		XVT_LOG_ERROR(
			"cutscene.table_alloc_failed file=\"%s\" count=%u bytes=%u",
			file_name, declared_count, (unsigned)allocation_size);
		return 0;
	}
	memset(g_cutscene_table, 0, allocation_size);
	g_cutscene_count = 0;

	char *line;
	while ((unsigned int)g_cutscene_count < declared_count) {
		do {
			line = FILE_GETS(g_frontend_scratch_buffer, 255,
					 stream);
			if (line == NULL) {
				XVT_LOG_WARN(
					"cutscene.table_short file=\"%s\" part=\"name\" read=%d count=%u",
					file_name, g_cutscene_count,
					declared_count);
				file_close(stream);
				return 1;
			}
		} while (line[0] == '/' && line[1] == '/');

		if (g_frontend_scratch_buffer
			    [strlen(g_frontend_scratch_buffer) - 1] == '\n') {
			g_frontend_scratch_buffer
				[strlen(g_frontend_scratch_buffer) - 1] = '\0';
		}
		memcpy(g_cutscene_table[g_cutscene_count].movie_name,
		       g_frontend_scratch_buffer,
		       sizeof(g_cutscene_table[g_cutscene_count].movie_name));

		do {
			line = FILE_GETS(g_frontend_scratch_buffer, 255,
					 stream);
			if (line == NULL) {
				XVT_LOG_WARN(
					"cutscene.table_short file=\"%s\" part=\"numbers\" read=%d count=%u",
					file_name, g_cutscene_count,
					declared_count);
				g_cutscene_table[g_cutscene_count]
					.movie_name[0] = '\0';
				file_close(stream);
				return 1;
			}
		} while (line[0] == '/' && line[1] == '/');

		if (sscanf(g_frontend_scratch_buffer, "%d %d %d",
			   &g_cutscene_table[g_cutscene_count].campaign_id,
			   &g_cutscene_table[g_cutscene_count]
				    .play_after_debriefing,
			   &g_cutscene_table[g_cutscene_count]
				    .campaign_mission_id) != 3) {
			XVT_LOG_WARN(
				"cutscene.table_entry_invalid file=\"%s\" entry=%d",
				file_name, g_cutscene_count);
			memset(&g_cutscene_table[g_cutscene_count], 0,
			       sizeof(struct cutscene_entry));
			file_close(stream);
			return 1;
		}

		do {
			line = FILE_GETS(g_frontend_scratch_buffer, 255,
					 stream);
			if (line == NULL) {
				XVT_LOG_WARN(
					"cutscene.table_short file=\"%s\" part=\"thumbnail\" read=%d count=%u",
					file_name, g_cutscene_count,
					declared_count);
				file_close(stream);
				return 1;
			}
		} while (line[0] == '/' && line[1] == '/');

		if (g_frontend_scratch_buffer
			    [strlen(g_frontend_scratch_buffer) - 1] == '\n') {
			g_frontend_scratch_buffer
				[strlen(g_frontend_scratch_buffer) - 1] = '\0';
		}
		memcpy(g_cutscene_table[g_cutscene_count].thumbnail_sprite,
		       g_frontend_scratch_buffer,
		       sizeof(g_cutscene_table[g_cutscene_count]
				      .thumbnail_sprite));

		do {
			line = FILE_GETS(g_frontend_scratch_buffer, 255,
					 stream);
			if (line == NULL) {
				XVT_LOG_WARN(
					"cutscene.table_short file=\"%s\" part=\"description\" read=%d count=%u",
					file_name, g_cutscene_count,
					declared_count);
				file_close(stream);
				return 1;
			}
		} while (line[0] == '/' && line[1] == '/');

		if (g_frontend_scratch_buffer
			    [strlen(g_frontend_scratch_buffer) - 1] == '\n') {
			g_frontend_scratch_buffer
				[strlen(g_frontend_scratch_buffer) - 1] = '\0';
		}
		memcpy(g_cutscene_table[g_cutscene_count].description,
		       g_frontend_scratch_buffer,
		       sizeof(g_cutscene_table[g_cutscene_count].description));
		++g_cutscene_count;
		XVT_LOG_DEBUG(
			"cutscene.table_entry index=%d movie=\"%.127s\" campaign=%d phase=%d mission=%d",
			g_cutscene_count - 1,
			g_cutscene_table[g_cutscene_count - 1].movie_name,
			g_cutscene_table[g_cutscene_count - 1].campaign_id,
			g_cutscene_table[g_cutscene_count - 1]
				.play_after_debriefing,
			g_cutscene_table[g_cutscene_count - 1]
				.campaign_mission_id);
	}
	XVT_LOG_DEBUG("cutscene.table_loaded file=\"%s\" read=%d count=%u",
		      file_name, g_cutscene_count, declared_count);

	file_close(stream);
	return 1;
}

/* Plays the cutscenes for the current point of a campaign: the argument is 0
 * before a mission, from the mission setup screen, and 1 after its debriefing.
 * Acts only while the pilot is in the training-exercises directory with a
 * mission sequence active and a table is loaded; plays every entry whose
 * campaign_id is the pilot's mission id in the campaigns directory, whose
 * campaign_mission_id is the one in the training-exercises directory and whose
 * play_after_debriefing equals the argument. Around each movie it suspends the CD
 * music, turns off the offscreen refill, clears and presents the screen, and
 * afterwards clears again, clears the offscreen surface, locks the back buffer
 * into g_draw_surface_ptr, turns the refill on and asks the CD music to resume.
 * Movies play with multiplayer sync. Returns 0 when it does not act or a movie
 * returns nonzero, which stops the rest, else 1. The modern build hands the
 * call to xvt_cutscene_task_play. */
// FUNCTION: XVT 0x4DF5F0
int cutscene_play_for_current_mission_phase(int phase)
{
	return xvt_cutscene_task_play(phase);
}
