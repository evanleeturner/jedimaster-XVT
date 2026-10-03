#include "xvt/frontend/frontend_bootstrap.h"
#ifdef XVT_MODERN
#include "xvt_runtime/runtime/movie_task.h"
#endif
#include "xvt/assets/file.h"
#include "xvt/audio/cd_audio.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/credits.h"
#include "xvt/frontend/front_image.h"
#include "xvt/frontend/frontend.h"
#include "xvt/frontend/frontend_cursor.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/frontend_draw.h"
#include "xvt/frontend/frontend_screen.h"
#include "xvt/frontend/frontend_text.h"
#include "xvt/frontend/movie.h"

#include <stdio.h>

/* Exit function of the first screen when the intro plays: runs once
 * frontend_bootstrap_play_opening_and_enter_credits has switched to the credits,
 * and loads the credits screen through credits_load_screen_resources. Returns
 * 0. */
// FUNCTION: XVT 0x4F0860
int frontend_bootstrap_exit_intro_and_load_credits(int frame_counter)
{
	(void)frame_counter;
	credits_load_screen_resources();
	return 0;
}

/* Update function of the first screen when the intro plays. Unlocks the back
 * buffer, plays the "Opening" movie, clears the back buffer, makes
 * credits_update_screen and frontend_bootstrap_exit_credits_and_load_frontend the
 * screen's callbacks and locks the back buffer again into g_draw_surface_ptr.
 * The original build plays the whole movie inside movie_play. In the modern
 * build movie_play starts it and answers XVT_MOVIE_PENDING, and this returns
 * at once with the back buffer unlocked; no frontend frame runs while the
 * movie plays, and the next call takes its result and goes on. Returns 0 on
 * every path and ignores the movie's result. */
// FUNCTION: XVT 0x4F0870
int frontend_bootstrap_play_opening_and_enter_credits(int frame_counter)
{
	(void)frame_counter;
	frontend_display_unlock_back_buffer();
#ifdef XVT_MODERN
	if (movie_play("Opening", 0) == XVT_MOVIE_PENDING) {
		return 0;
	}
#else
	movie_play("Opening", 0);
#endif
	frontend_display_clear_back_buffer();
	frontend_screen_set_callbacks(
		credits_update_screen,
		(frontend_screen_exit_fn)
			frontend_bootstrap_exit_credits_and_load_frontend);
	g_draw_surface_ptr = frontend_display_lock_back_buffer();
	return 0;
}

/* Start function of the frontend when the intro plays, run once before the
 * first frame: by the main loop through mode_init_fn in the original build, by
 * xvt_frontend_task_update in the modern one. Turns off quitting on Esc, sets
 * the surface clear color to 0, hides the cursor, turns off clearing the back
 * buffer after each present and refilling it from the offscreen surface, and
 * loads font size 12. Closes g_movie_subtitle_file and sets it NULL when it is
 * open. Returns 0, which in the original build lets the main loop start. */
// FUNCTION: XVT 0x4F08B0
int frontend_bootstrap_init_mode(void)
{
	frontend_display_disable_escape_close();
	frontend_display_set_surface_clear_color(0);
	frontend_cursor_hide();
	frontend_display_disable_clear_after_present();
	frontend_display_disable_offscreen_restore();
	frontend_text_load_font(12);
	if (g_movie_subtitle_file != NULL) {
		FILE_RAW_CLOSE(g_movie_subtitle_file);
		g_movie_subtitle_file = NULL;
	}
	return 0;
}

/* Exit function of the credits screen, run once the credits have switched to
 * the concourse. Closes g_frontend_credits_file and sets it NULL when it is
 * open, frees the seven credits images, stops the text fade, loads the
 * frontend through frontend_load_resources and turns on looping of the
 * current CD track. Returns 0 and ignores what frontend_load_resources
 * returns. */
// FUNCTION: XVT 0x4FB5E0
int frontend_bootstrap_exit_credits_and_load_frontend(int frame_counter)
{
	(void)frame_counter;

	if (g_frontend_credits_file != NULL) {
		file_close(g_frontend_credits_file);
		g_frontend_credits_file = NULL;
	}
	front_image_free_resource_by_name("background");
	front_image_free_resource_by_name("leclogo");
	front_image_free_resource_by_name("totallylogo");
	front_image_free_resource_by_name("comp01");
	front_image_free_resource_by_name("testers");
	front_image_free_resource_by_name("lakota");
	front_image_free_resource_by_name("artists");
	frontend_text_stop_text_fade();
	frontend_load_resources();
	cd_audio_enable_loop_current_track();
	return 0;
}
