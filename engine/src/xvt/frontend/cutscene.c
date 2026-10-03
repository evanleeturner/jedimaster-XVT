#include "xvt/frontend/cutscene.h"
#ifdef XVT_MODERN
#include "xvt_runtime/runtime/cutscene_task.h"
#endif

#include "xvt/assets/file.h"
#include "xvt/audio/cd_audio.h"
#include "xvt/frontend/frontend.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/frontend_draw.h"
#include "xvt/frontend/frontend_mission_list.h"
#include "xvt/frontend/movie.h"
#include "xvt/frontend/pilot_record.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Entries loaded in g_cutsceneTable. Cutscene_LoadTable sets it once it has
 * a table; GameMain in the original build and XvtFrontendTask_Shutdown in the
 * modern one set 0 when they free the table. */
// GLOBAL: XVT 0xB69E34
int g_cutsceneCount = 0;
/* Heap table of the campaign cutscenes, read from movies\cutscene.lst by
 * Cutscene_LoadTable when Frontend_LoadResources runs; NULL before that and
 * after a failed load. */
// GLOBAL: XVT 0xB6A248
CutsceneEntry *g_cutsceneTable = NULL;

/* Reads a cutscene list into g_cutsceneTable, freeing the old table first.
 * The first line gives the entry count, for which it allocates a zeroed
 * table. Each entry is four lines, skipping lines that start with "//": the
 * movie name, three numbers (campaignId, playAfterDebriefing,
 * campaignMissionId), the thumbnail image name and the description. Returns
 * 0, with g_cutsceneCount unchanged, when the file does not open, is empty or
 * the table cannot be allocated; that last case leaves the file open.
 * Otherwise returns 1 with g_cutsceneCount at the entries read: it stops
 * early at the end of the file, and at a numbers line without three numbers,
 * which it clears. The names are copied as fixed 128-, 32- and 128-byte
 * blocks, so a longer line leaves one without its terminator. */
// FUNCTION: XVT 0x4DF250
int Cutscene_LoadTable(char *fileName)
{
	XvtFile *stream;
	unsigned int declaredCount;
	char *line;
	size_t allocationSize;

	if (g_cutsceneTable != NULL) {
		free(g_cutsceneTable);
		g_cutsceneTable = NULL;
	}

	stream = File_Open(fileName, "r");
	if (stream == NULL) {
		return 0;
	}
	if (File_Gets(g_frontendScratchBuffer, 255, stream) == NULL) {
		File_Close(stream);
		return 0;
	}

	declaredCount = (unsigned int)atoi(g_frontendScratchBuffer);
	allocationSize = sizeof(CutsceneEntry) * declaredCount;
	g_cutsceneTable = (CutsceneEntry *)malloc(allocationSize);
	if (g_cutsceneTable == NULL) {
		return 0;
	}
	memset(g_cutsceneTable, 0, allocationSize);
	g_cutsceneCount = 0;

	while ((unsigned int)g_cutsceneCount < declaredCount) {
		do {
			line = File_Gets(g_frontendScratchBuffer, 255, stream);
			if (line == NULL) {
				File_Close(stream);
				return 1;
			}
		} while (line[0] == '/' && line[1] == '/');

		if (g_frontendScratchBuffer[strlen(g_frontendScratchBuffer) -
					    1] == '\n') {
			g_frontendScratchBuffer
				[strlen(g_frontendScratchBuffer) - 1] = '\0';
		}
		memcpy(g_cutsceneTable[g_cutsceneCount].movieName,
		       g_frontendScratchBuffer,
		       sizeof(g_cutsceneTable[g_cutsceneCount].movieName));

		do {
			line = File_Gets(g_frontendScratchBuffer, 255, stream);
			if (line == NULL) {
				g_cutsceneTable[g_cutsceneCount].movieName[0] =
					'\0';
				File_Close(stream);
				return 1;
			}
		} while (line[0] == '/' && line[1] == '/');

		if (sscanf(g_frontendScratchBuffer, "%d %d %d",
			   &g_cutsceneTable[g_cutsceneCount].campaignId,
			   &g_cutsceneTable[g_cutsceneCount]
				    .playAfterDebriefing,
			   &g_cutsceneTable[g_cutsceneCount]
				    .campaignMissionId) != 3) {
			memset(&g_cutsceneTable[g_cutsceneCount], 0,
			       sizeof(CutsceneEntry));
			File_Close(stream);
			return 1;
		}

		do {
			line = File_Gets(g_frontendScratchBuffer, 255, stream);
			if (line == NULL) {
				File_Close(stream);
				return 1;
			}
		} while (line[0] == '/' && line[1] == '/');

		if (g_frontendScratchBuffer[strlen(g_frontendScratchBuffer) -
					    1] == '\n') {
			g_frontendScratchBuffer
				[strlen(g_frontendScratchBuffer) - 1] = '\0';
		}
		memcpy(g_cutsceneTable[g_cutsceneCount].thumbnailSprite,
		       g_frontendScratchBuffer,
		       sizeof(g_cutsceneTable[g_cutsceneCount]
				      .thumbnailSprite));

		do {
			line = File_Gets(g_frontendScratchBuffer, 255, stream);
			if (line == NULL) {
				File_Close(stream);
				return 1;
			}
		} while (line[0] == '/' && line[1] == '/');

		if (g_frontendScratchBuffer[strlen(g_frontendScratchBuffer) -
					    1] == '\n') {
			g_frontendScratchBuffer
				[strlen(g_frontendScratchBuffer) - 1] = '\0';
		}
		memcpy(g_cutsceneTable[g_cutsceneCount].description,
		       g_frontendScratchBuffer,
		       sizeof(g_cutsceneTable[g_cutsceneCount].description));
		++g_cutsceneCount;
	}

	File_Close(stream);
	return 1;
}

/* Plays the cutscenes for the current point of a campaign: the argument is 0
 * before a mission, from the mission setup screen, and 1 after its debriefing.
 * Acts only while the pilot is in the training-exercises directory with a
 * mission sequence active and a table is loaded; plays every entry whose
 * campaignId is the pilot's mission id in the campaigns directory, whose
 * campaignMissionId is the one in the training-exercises directory and whose
 * playAfterDebriefing equals the argument. Around each movie it suspends the CD
 * music, turns off the offscreen refill, clears and presents the screen, and
 * afterwards clears again, clears the offscreen surface, locks the back buffer
 * into g_drawSurfacePtr, turns the refill on and asks the CD music to resume.
 * Movies play with multiplayer sync. Returns 0 when it does not act or a movie
 * returns nonzero, which stops the rest, else 1. The modern build hands the
 * call to XvtCutsceneTask_Play. */
// FUNCTION: XVT 0x4DF5F0
int Cutscene_PlayForCurrentMissionPhase(int phase)
{
#ifdef XVT_MODERN
	return XvtCutsceneTask_Play(phase);
#else
	enum {
		MISSION_SEQUENCE_ACTIVE = 1,
		SYNCHRONIZE_MULTIPLAYER = 1,
	};

	unsigned int entryIndex;
	int playResult;

	if (g_pilotData.missionDirectoryId !=
		    MISSION_DIRECTORY_TRAINING_EXERCISES ||
	    g_pilotData.missionSequenceActive != MISSION_SEQUENCE_ACTIVE ||
	    g_pilotData.missionDirectoryId == MISSION_DIRECTORY_CAMPAIGNS) {
		return 0;
	}
	if (g_cutsceneTable == NULL) {
		return 0;
	}

	for (entryIndex = 0; entryIndex < (unsigned int)g_cutsceneCount;
	     ++entryIndex) {
		if (g_cutsceneTable[entryIndex].campaignId ==
			    g_pilotData.missionDescriptionIds
				    [MISSION_DIRECTORY_CAMPAIGNS] &&
		    g_cutsceneTable[entryIndex].playAfterDebriefing == phase &&
		    g_cutsceneTable[entryIndex].campaignMissionId ==
			    g_pilotData.missionDescriptionIds
				    [MISSION_DIRECTORY_TRAINING_EXERCISES]) {
			CDAudio_SuspendPlayback();
			FrontendDisplay_DisableOffscreenRestore();
			FrontendDisplay_UnlockBackBuffer();
			FrontendDisplay_ClearBackBuffer();
			FrontendDisplay_PresentFrame();
			FrontendDisplay_ClearBackBuffer();
			playResult = Movie_Play(
				g_cutsceneTable[entryIndex].movieName,
				SYNCHRONIZE_MULTIPLAYER);
			FrontendDisplay_ClearBackBuffer();
			FrontendDisplay_PresentFrame();
			FrontendDisplay_ClearBackBuffer();
			FrontendDisplay_ClearOffscreenSurface();
			g_drawSurfacePtr = FrontendDisplay_LockBackBuffer();
			FrontendDisplay_EnableOffscreenRestore();
			CDAudio_RequestResumePlayback();
			if (playResult != 0) {
				return 0;
			}
		}
	}
	return 1;
#endif
}
