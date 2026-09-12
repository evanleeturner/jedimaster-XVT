#include "xvt_runtime/runtime/cutscene_task.h"

#include "xvt/audio/cd_audio.h"
#include "xvt/frontend/cutscene.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/frontend_draw.h"
#include "xvt/frontend/frontend_mission_list.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt_runtime/runtime/movie_task.h"

#include <string.h>

static struct {
	unsigned int entry;
	int phase;
	int active;
	int waiting;
} g_cutscene;

static void XvtCutsceneTask_Restore(void) {
	FrontendDisplay_ClearBackBuffer();
	FrontendDisplay_PresentFrame();
	FrontendDisplay_ClearBackBuffer();
	FrontendDisplay_ClearOffscreenSurface();
	g_drawSurfacePtr = FrontendDisplay_LockBackBuffer();
	FrontendDisplay_EnableOffscreenRestore();
	CDAudio_RequestResumePlayback();
}

int XvtCutsceneTask_Play(int phase) {
	int result;
	if (!g_cutscene.active) {
		if (g_pilotData.missionDirectoryId != MISSION_DIRECTORY_TRAINING_EXERCISES ||
			g_pilotData.missionSequenceActive != 1 || !g_cutsceneTable)
			return 0;
		g_cutscene.phase = phase;
		g_cutscene.entry = 0;
		g_cutscene.active = 1;
	}
	if (g_cutscene.waiting) {
		if (!XvtMovieTask_TakeResult(&result))
			return XVT_MOVIE_PENDING;
		g_cutscene.waiting = 0;
		XvtCutsceneTask_Restore();
		if (result != 0) {
			XvtCutsceneTask_Reset();
			return 0;
		}
		++g_cutscene.entry;
	}
	for (; g_cutscene.entry < (unsigned int)g_cutsceneCount; ++g_cutscene.entry) {
		const CutsceneEntry* entry = &g_cutsceneTable[g_cutscene.entry];
		if (entry->missionIdx != g_pilotData.missionDescriptionIds[5] ||
			entry->playAfterDebriefing != g_cutscene.phase ||
			entry->missionDescriptionId != g_pilotData.missionDescriptionIds[0])
			continue;
		CDAudio_SuspendPlayback();
		FrontendDisplay_DisableOffscreenRestore();
		FrontendDisplay_UnlockBackBuffer();
		FrontendDisplay_ClearBackBuffer();
		FrontendDisplay_PresentFrame();
		FrontendDisplay_ClearBackBuffer();
		result = XvtMovieTask_Begin(entry->movieName, 1);
		if (result == XVT_MOVIE_PENDING) {
			g_cutscene.waiting = 1;
			return XVT_MOVIE_PENDING;
		}
		XvtCutsceneTask_Restore();
		if (result != 0) {
			XvtCutsceneTask_Reset();
			return 0;
		}
	}
	XvtCutsceneTask_Reset();
	return 1;
}

void XvtCutsceneTask_Reset(void) { memset(&g_cutscene, 0, sizeof(g_cutscene)); }
