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

// GLOBAL: XVT 0xB69E34
int g_cutsceneCount = 0;
// GLOBAL: XVT 0xB6A248
CutsceneEntry* g_cutsceneTable = NULL;

// FUNCTION: XVT 0x4DF250
int Cutscene_LoadTable(char* fileName) {
	XvtFile* stream;
	unsigned int declaredCount;
	char* line;
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
	g_cutsceneTable = (CutsceneEntry*)malloc(allocationSize);
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

		if (g_frontendScratchBuffer[strlen(g_frontendScratchBuffer) - 1] == '\n') {
			g_frontendScratchBuffer[strlen(g_frontendScratchBuffer) - 1] = '\0';
		}
		memcpy(g_cutsceneTable[g_cutsceneCount].movieName, g_frontendScratchBuffer,
			   sizeof(g_cutsceneTable[g_cutsceneCount].movieName));

		do {
			line = File_Gets(g_frontendScratchBuffer, 255, stream);
			if (line == NULL) {
				g_cutsceneTable[g_cutsceneCount].movieName[0] = '\0';
				File_Close(stream);
				return 1;
			}
		} while (line[0] == '/' && line[1] == '/');

		if (sscanf(g_frontendScratchBuffer, "%d %d %d", &g_cutsceneTable[g_cutsceneCount].missionIdx,
				   &g_cutsceneTable[g_cutsceneCount].playAfterDebriefing,
				   &g_cutsceneTable[g_cutsceneCount].missionDescriptionId) != 3) {
			memset(&g_cutsceneTable[g_cutsceneCount], 0, sizeof(CutsceneEntry));
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

		if (g_frontendScratchBuffer[strlen(g_frontendScratchBuffer) - 1] == '\n') {
			g_frontendScratchBuffer[strlen(g_frontendScratchBuffer) - 1] = '\0';
		}
		memcpy(g_cutsceneTable[g_cutsceneCount].thumbnailSprite, g_frontendScratchBuffer,
			   sizeof(g_cutsceneTable[g_cutsceneCount].thumbnailSprite));

		do {
			line = File_Gets(g_frontendScratchBuffer, 255, stream);
			if (line == NULL) {
				File_Close(stream);
				return 1;
			}
		} while (line[0] == '/' && line[1] == '/');

		if (g_frontendScratchBuffer[strlen(g_frontendScratchBuffer) - 1] == '\n') {
			g_frontendScratchBuffer[strlen(g_frontendScratchBuffer) - 1] = '\0';
		}
		memcpy(g_cutsceneTable[g_cutsceneCount].description, g_frontendScratchBuffer,
			   sizeof(g_cutsceneTable[g_cutsceneCount].description));
		++g_cutsceneCount;
	}

	File_Close(stream);
	return 1;
}

// FUNCTION: XVT 0x4DF5F0
int Cutscene_PlayForCurrentMissionPhase(int phase) {
#ifdef XVT_MODERN
	return XvtCutsceneTask_Play(phase);
#else
	enum {
		MISSION_INDEX_SLOT = 5,
		MISSION_DESCRIPTION_SLOT = 0,
		MISSION_SEQUENCE_ACTIVE = 1,
		SYNCHRONIZE_MULTIPLAYER = 1,
	};

	unsigned int entryIndex;
	int playResult;

	if (g_pilotData.missionDirectoryId != MISSION_DIRECTORY_TRAINING_EXERCISES ||
		g_pilotData.missionSequenceActive != MISSION_SEQUENCE_ACTIVE ||
		g_pilotData.missionDirectoryId == MISSION_DIRECTORY_CAMPAIGNS)
		return 0;
	if (g_cutsceneTable == NULL)
		return 0;

	for (entryIndex = 0; entryIndex < (unsigned int)g_cutsceneCount; ++entryIndex) {
		if (g_cutsceneTable[entryIndex].missionIdx == g_pilotData.missionDescriptionIds[MISSION_INDEX_SLOT] &&
			g_cutsceneTable[entryIndex].playAfterDebriefing == phase &&
			g_cutsceneTable[entryIndex].missionDescriptionId ==
				g_pilotData.missionDescriptionIds[MISSION_DESCRIPTION_SLOT]) {
			CDAudio_SuspendPlayback();
			FrontendDisplay_DisableOffscreenRestore();
			FrontendDisplay_UnlockBackBuffer();
			FrontendDisplay_ClearBackBuffer();
			FrontendDisplay_PresentFrame();
			FrontendDisplay_ClearBackBuffer();
			playResult = Movie_Play(g_cutsceneTable[entryIndex].movieName, SYNCHRONIZE_MULTIPLAYER);
			FrontendDisplay_ClearBackBuffer();
			FrontendDisplay_PresentFrame();
			FrontendDisplay_ClearBackBuffer();
			FrontendDisplay_ClearOffscreenSurface();
			g_drawSurfacePtr = FrontendDisplay_LockBackBuffer();
			FrontendDisplay_EnableOffscreenRestore();
			CDAudio_RequestResumePlayback();
			if (playResult != 0)
				return 0;
		}
	}
	return 1;
#endif
}
