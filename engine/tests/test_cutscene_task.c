#define _POSIX_C_SOURCE 200809L
/* Checks the cutscene task (xvt_runtime/runtime/cutscene_task.h) against the
 * promises in its header: when a run starts, which table entries match, a
 * pending movie returning -1, a movie's nonzero result ending the run and
 * skipping the rest, CD audio suspended for a movie and asked to resume after
 * it, no run left after a final return, and Reset. The test builds its own
 * cutscene table and pilot state; the movies are empty files in a temporary
 * asset folder, which Aeron's decoder reports unreadable from its own thread
 * (result 2), or are missing (result 2 at once). Every case starts from Reset,
 * no movie, a cleared frontend with a CD track playing, and a training mission
 * sequence whose mission index is 11 and whose description id is 22.
 *
 * Not checked here: moving on to the next match after a movie that played to
 * its end, which needs a Smacker movie from the game, and the display restore,
 * which needs a window. */
#include <string.h>
#include <time.h>

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

enum { MISSION_INDEX = 11, DESCRIPTION_ID = 22 };

static struct xvt_test_assets g_assets;
static struct cutscene_entry g_table[5];

static void entry(int index, const char *movie, int mission, int phase,
		  int description)
{
	memset(&g_table[index], 0, sizeof g_table[index]);
	strcpy(g_table[index].movie_name, movie);
	g_table[index].campaign_id = mission;
	g_table[index].play_after_debriefing = phase;
	g_table[index].campaign_mission_id = description;
}

/* The table: "first" and "fourth" match phase 0, "second" matches phase 1, the
 * others never match the pilot's mission. Placeholders exist for the movies
 * named in present. */
static void fresh(const char *present_a, const char *present_b)
{
	xvt_cutscene_task_reset();
	xvt_movie_task_shutdown();
	xvt_test_close_assets(&g_assets);
	memset(&g_front_state, 0, sizeof g_front_state);
	g_front_state.cd_audio_mci_device_id = 1;
	g_front_state.cd_audio_current_track = 1;
	g_front_state.cd_audio_suspend_state = CD_AUDIO_NOT_SUSPENDED;
	g_frontend_mission_session_mode = FRONTEND_MISSION_SESSION_SINGLEPLAYER;
	memset(&g_pilot_data, 0, sizeof g_pilot_data);
	g_pilot_data.mission_directory_id =
		MISSION_DIRECTORY_TRAINING_EXERCISES;
	g_pilot_data.mission_sequence_active = 1;
	g_pilot_data.mission_description_ids[5] = MISSION_INDEX;
	g_pilot_data.mission_description_ids[0] = DESCRIPTION_ID;
	entry(0, "first", MISSION_INDEX, 0, DESCRIPTION_ID);
	entry(1, "other", MISSION_INDEX + 1, 0, DESCRIPTION_ID);
	entry(2, "second", MISSION_INDEX, 1, DESCRIPTION_ID);
	entry(3, "third", MISSION_INDEX, 0, DESCRIPTION_ID + 1);
	entry(4, "fourth", MISSION_INDEX, 0, DESCRIPTION_ID);
	g_cutscene_table = g_table;
	g_cutscene_count = 5;
	xvt_test_open_assets(&g_assets);
	char path[XVT_TEST_PATH_CAPACITY];
	if (present_a) {
		snprintf(path, sizeof path, "movies/%s.smk", present_a);
		xvt_test_add_asset(&g_assets, path);
	}
	if (present_b) {
		snprintf(path, sizeof path, "movies/%s.smk", present_b);
		xvt_test_add_asset(&g_assets, path);
	}
}

/* Ticks the movie task until the movie completes and reaps it, as the port does. */
static void finish_movie(void)
{
	struct timespec pause = {0, 1000000};
	for (int i = 0; i < 10000 && xvt_movie_task_is_active(); ++i) {
		xvt_movie_task_update();
		nanosleep(&pause, NULL);
	}
	XVT_ASSERT_INT_EQ(xvt_movie_task_is_active(), 0);
	xvt_movie_task_reap_finished();
}

static void check_refusals(void)
{
	fresh(NULL, NULL);
	g_pilot_data.mission_directory_id = MISSION_DIRECTORY_MELEES;
	XVT_ASSERT_INT_EQ(xvt_cutscene_task_play(0), 0);
	fresh(NULL, NULL);
	g_pilot_data.mission_sequence_active = 0;
	XVT_ASSERT_INT_EQ(xvt_cutscene_task_play(0), 0);
	fresh(NULL, NULL);
	g_cutscene_table = NULL;
	XVT_ASSERT_INT_EQ(xvt_cutscene_task_play(0), 0);

	/* A refusal starts no run: with the table back, the next call starts one. */
	g_cutscene_table = g_table;
	XVT_ASSERT_INT_EQ(xvt_cutscene_task_play(5), 1);
	XVT_ASSERT_INT_EQ(g_front_state.cd_audio_suspend_state,
			  CD_AUDIO_NOT_SUSPENDED);
}

static void check_no_match(void)
{
	fresh("first", "fourth");
	XVT_ASSERT_INT_EQ(xvt_cutscene_task_play(5), 1);
	XVT_ASSERT_INT_EQ(xvt_movie_task_is_active(), 0);
	XVT_ASSERT_INT_EQ(g_front_state.cd_audio_suspend_state,
			  CD_AUDIO_NOT_SUSPENDED);

	/* That return left no run: phase 0 now starts a new one, whose first match plays. */
	XVT_ASSERT_INT_EQ(xvt_cutscene_task_play(0), XVT_MOVIE_PENDING);
	XVT_ASSERT_INT_EQ(xvt_movie_task_is_active(), 1);
}

static void check_missing_movie_ends_run(void)
{
	/* "first" is missing, "fourth" is there: the failure of the first ends
	 * the run before the other. */
	fresh("fourth", NULL);
	XVT_ASSERT_INT_EQ(xvt_cutscene_task_play(0), 0);
	XVT_ASSERT_INT_EQ(xvt_movie_task_is_active(), 0);
	/* CD audio was suspended for the movie and asked to resume after it. */
	XVT_ASSERT_INT_EQ(g_front_state.cd_audio_suspend_state,
			  CD_AUDIO_RESUME_PENDING);
}

static void check_pending_movie(void)
{
	fresh("first", "fourth");
	XVT_ASSERT_INT_EQ(xvt_cutscene_task_play(0), XVT_MOVIE_PENDING);
	XVT_ASSERT_INT_EQ(xvt_movie_task_is_active(), 1);
	XVT_ASSERT_INT_EQ(g_front_state.cd_audio_suspend_state,
			  CD_AUDIO_SUSPENDED);
	/* Later calls continue the run whatever phase they pass. */
	XVT_ASSERT_INT_EQ(xvt_cutscene_task_play(1), XVT_MOVIE_PENDING);

	/* The empty file fails (result 2): the run ends with 0 and "fourth" is skipped. */
	finish_movie();
	XVT_ASSERT_INT_EQ(xvt_cutscene_task_play(1), 0);
	XVT_ASSERT_INT_EQ(xvt_movie_task_is_active(), 0);
	XVT_ASSERT_INT_EQ(g_front_state.cd_audio_suspend_state,
			  CD_AUDIO_RESUME_PENDING);
	int result = 77;
	XVT_ASSERT_INT_EQ(xvt_movie_task_take_result(&result), 0);

	/* No run is left: phase 1 starts a new one, matching "second" only, which is missing. */
	XVT_ASSERT_INT_EQ(xvt_cutscene_task_play(1), 0);
	XVT_ASSERT_INT_EQ(xvt_movie_task_is_active(), 0);
}

static void check_reset(void)
{
	fresh("first", NULL);
	XVT_ASSERT_INT_EQ(xvt_cutscene_task_play(0), XVT_MOVIE_PENDING);
	xvt_cutscene_task_reset();
	/* The movie plays on, and CD audio stays suspended. */
	XVT_ASSERT_INT_EQ(xvt_movie_task_is_active(), 1);
	XVT_ASSERT_INT_EQ(g_front_state.cd_audio_suspend_state,
			  CD_AUDIO_SUSPENDED);

	/* The next Play starts over: it does not wait for that movie, but tries
	 * "first" again, which the movie task refuses while a movie is
	 * active. */
	XVT_ASSERT_INT_EQ(xvt_cutscene_task_play(0), 0);
	XVT_ASSERT_INT_EQ(xvt_movie_task_is_active(), 1);
	finish_movie();
	int result = 77;
	XVT_ASSERT_INT_EQ(xvt_movie_task_take_result(&result), 1);
}

int main(void)
{
	check_refusals();
	check_no_match();
	check_missing_movie_ends_run();
	check_pending_movie();
	check_reset();
	xvt_cutscene_task_reset();
	xvt_movie_task_shutdown();
	xvt_test_close_assets(&g_assets);
	g_cutscene_table = NULL;
	g_cutscene_count = 0;
	return 0;
}
