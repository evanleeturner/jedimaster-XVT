#define _POSIX_C_SOURCE 200809L
/* Checks the cutscene task (xvt_runtime/runtime/cutscene_task.h) against the promises in its header: when
 * a run starts, which table entries match, a pending movie returning -1, a movie's nonzero result ending
 * the run and skipping the rest, CD audio suspended for a movie and asked to resume after it, no run left
 * after a final return, and Reset. The test builds its own cutscene table and pilot state; the movies are
 * empty files in a temporary asset folder, which Aeron's decoder reports unreadable from its own thread
 * (result 2), or are missing (result 2 at once). Every case starts from Reset, no movie, a cleared
 * frontend with a CD track playing, and a training mission sequence whose mission index is 11 and whose
 * description id is 22.
 *
 * Not checked here: moving on to the next match after a movie that played to its end, which needs a
 * Smacker movie from the game, and the display restore, which needs a window. */
#include "test_assert.h"
#include "test_asset_folder.h"
#include "xvt/audio/cd_audio.h"
#include "xvt/frontend/cutscene.h"
#include "xvt/frontend/frontend_mission.h"
#include "xvt/frontend/frontend_mission_list.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt_runtime/runtime/cutscene_task.h"
#include "xvt_runtime/runtime/movie_task.h"

#include <string.h>
#include <time.h>

enum { MISSION_INDEX = 11, DESCRIPTION_ID = 22 };

static XvtTestAssets g_assets;
static CutsceneEntry g_table[5];

static void Entry(int index, const char* movie, int mission, int phase, int description) {
	memset(&g_table[index], 0, sizeof g_table[index]);
	strcpy(g_table[index].movieName, movie);
	g_table[index].campaignId = mission;
	g_table[index].playAfterDebriefing = phase;
	g_table[index].campaignMissionId = description;
}

/* The table: "first" and "fourth" match phase 0, "second" matches phase 1, the others never match the
 * pilot's mission. Placeholders exist for the movies named in present. */
static void Fresh(const char* present_a, const char* present_b) {
	XvtCutsceneTask_Reset();
	XvtMovieTask_Shutdown();
	XvtTest_CloseAssets(&g_assets);
	memset(&g_frontState, 0, sizeof g_frontState);
	g_frontState.cdAudioMciDeviceId = 1;
	g_frontState.cdAudioCurrentTrack = 1;
	g_frontState.cdAudioSuspendState = CDAudio_NotSuspended;
	g_frontendMissionSessionMode = FRONTEND_MISSION_SESSION_SINGLEPLAYER;
	memset(&g_pilotData, 0, sizeof g_pilotData);
	g_pilotData.missionDirectoryId = MISSION_DIRECTORY_TRAINING_EXERCISES;
	g_pilotData.missionSequenceActive = 1;
	g_pilotData.missionDescriptionIds[5] = MISSION_INDEX;
	g_pilotData.missionDescriptionIds[0] = DESCRIPTION_ID;
	Entry(0, "first", MISSION_INDEX, 0, DESCRIPTION_ID);
	Entry(1, "other", MISSION_INDEX + 1, 0, DESCRIPTION_ID);
	Entry(2, "second", MISSION_INDEX, 1, DESCRIPTION_ID);
	Entry(3, "third", MISSION_INDEX, 0, DESCRIPTION_ID + 1);
	Entry(4, "fourth", MISSION_INDEX, 0, DESCRIPTION_ID);
	g_cutsceneTable = g_table;
	g_cutsceneCount = 5;
	XvtTest_OpenAssets(&g_assets);
	char path[XVT_TEST_PATH_CAPACITY];
	if (present_a) {
		snprintf(path, sizeof path, "movies/%s.smk", present_a);
		XvtTest_AddAsset(&g_assets, path);
	}
	if (present_b) {
		snprintf(path, sizeof path, "movies/%s.smk", present_b);
		XvtTest_AddAsset(&g_assets, path);
	}
}

/* Ticks the movie task until the movie completes and reaps it, as the port does. */
static void FinishMovie(void) {
	struct timespec pause = { 0, 1000000 };
	for (int i = 0; i < 10000 && XvtMovieTask_IsActive(); ++i) {
		XvtMovieTask_Update();
		nanosleep(&pause, NULL);
	}
	XVT_ASSERT_INT_EQ(XvtMovieTask_IsActive(), 0);
	XvtMovieTask_ReapFinished();
}

static void CheckRefusals(void) {
	Fresh(NULL, NULL);
	g_pilotData.missionDirectoryId = MISSION_DIRECTORY_MELEES;
	XVT_ASSERT_INT_EQ(XvtCutsceneTask_Play(0), 0);
	Fresh(NULL, NULL);
	g_pilotData.missionSequenceActive = 0;
	XVT_ASSERT_INT_EQ(XvtCutsceneTask_Play(0), 0);
	Fresh(NULL, NULL);
	g_cutsceneTable = NULL;
	XVT_ASSERT_INT_EQ(XvtCutsceneTask_Play(0), 0);

	/* A refusal starts no run: with the table back, the next call starts one. */
	g_cutsceneTable = g_table;
	XVT_ASSERT_INT_EQ(XvtCutsceneTask_Play(5), 1);
	XVT_ASSERT_INT_EQ(g_frontState.cdAudioSuspendState, CDAudio_NotSuspended);
}

static void CheckNoMatch(void) {
	Fresh("first", "fourth");
	XVT_ASSERT_INT_EQ(XvtCutsceneTask_Play(5), 1);
	XVT_ASSERT_INT_EQ(XvtMovieTask_IsActive(), 0);
	XVT_ASSERT_INT_EQ(g_frontState.cdAudioSuspendState, CDAudio_NotSuspended);

	/* That return left no run: phase 0 now starts a new one, whose first match plays. */
	XVT_ASSERT_INT_EQ(XvtCutsceneTask_Play(0), XVT_MOVIE_PENDING);
	XVT_ASSERT_INT_EQ(XvtMovieTask_IsActive(), 1);
}

static void CheckMissingMovieEndsRun(void) {
	/* "first" is missing, "fourth" is there: the failure of the first ends the run before the other. */
	Fresh("fourth", NULL);
	XVT_ASSERT_INT_EQ(XvtCutsceneTask_Play(0), 0);
	XVT_ASSERT_INT_EQ(XvtMovieTask_IsActive(), 0);
	/* CD audio was suspended for the movie and asked to resume after it. */
	XVT_ASSERT_INT_EQ(g_frontState.cdAudioSuspendState, CDAudio_ResumePending);
}

static void CheckPendingMovie(void) {
	Fresh("first", "fourth");
	XVT_ASSERT_INT_EQ(XvtCutsceneTask_Play(0), XVT_MOVIE_PENDING);
	XVT_ASSERT_INT_EQ(XvtMovieTask_IsActive(), 1);
	XVT_ASSERT_INT_EQ(g_frontState.cdAudioSuspendState, CDAudio_Suspended);
	/* Later calls continue the run whatever phase they pass. */
	XVT_ASSERT_INT_EQ(XvtCutsceneTask_Play(1), XVT_MOVIE_PENDING);

	/* The empty file fails (result 2): the run ends with 0 and "fourth" is skipped. */
	FinishMovie();
	XVT_ASSERT_INT_EQ(XvtCutsceneTask_Play(1), 0);
	XVT_ASSERT_INT_EQ(XvtMovieTask_IsActive(), 0);
	XVT_ASSERT_INT_EQ(g_frontState.cdAudioSuspendState, CDAudio_ResumePending);
	int result = 77;
	XVT_ASSERT_INT_EQ(XvtMovieTask_TakeResult(&result), 0);

	/* No run is left: phase 1 starts a new one, matching "second" only, which is missing. */
	XVT_ASSERT_INT_EQ(XvtCutsceneTask_Play(1), 0);
	XVT_ASSERT_INT_EQ(XvtMovieTask_IsActive(), 0);
}

static void CheckReset(void) {
	Fresh("first", NULL);
	XVT_ASSERT_INT_EQ(XvtCutsceneTask_Play(0), XVT_MOVIE_PENDING);
	XvtCutsceneTask_Reset();
	/* The movie plays on, and CD audio stays suspended. */
	XVT_ASSERT_INT_EQ(XvtMovieTask_IsActive(), 1);
	XVT_ASSERT_INT_EQ(g_frontState.cdAudioSuspendState, CDAudio_Suspended);

	/* The next Play starts over: it does not wait for that movie, but tries "first" again, which the
	 * movie task refuses while a movie is active. */
	XVT_ASSERT_INT_EQ(XvtCutsceneTask_Play(0), 0);
	XVT_ASSERT_INT_EQ(XvtMovieTask_IsActive(), 1);
	FinishMovie();
	int result = 77;
	XVT_ASSERT_INT_EQ(XvtMovieTask_TakeResult(&result), 1);
}

int main(void) {
	CheckRefusals();
	CheckNoMatch();
	CheckMissingMovieEndsRun();
	CheckPendingMovie();
	CheckReset();
	XvtCutsceneTask_Reset();
	XvtMovieTask_Shutdown();
	XvtTest_CloseAssets(&g_assets);
	g_cutsceneTable = NULL;
	g_cutsceneCount = 0;
	return 0;
}
