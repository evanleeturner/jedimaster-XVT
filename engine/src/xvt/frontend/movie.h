#ifndef XVT_FRONTEND_MOVIE_H
#define XVT_FRONTEND_MOVIE_H

#include <stdint.h>
#include <stdio.h>

#include "aeron/compat/ddraw.h"
#include "aeron/compat/win_types.h"
#include "xvt/assets/file.h"
#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*movie_input_callback)(void *hWnd, unsigned int message,
				    void *wParam, void *lParam, void *context,
				    int *handled_result);

typedef int (*movie_progress_callback)(unsigned int current_frame,
				       unsigned int final_frame, void *context);

/* What the original build's movie_play passes to movie_run_smacker_playback. */
struct movie_playback_params {
	/* Movie name, without folder or extension. */
	const char *movie_name;
	/* Display width the movie is centered in; movie_play passes 640. */
	int display_width;
	/* Display height the movie is centered in; movie_play passes 480. */
	int display_height;
	/* The display's primary surface, flipped in page-flip full screen. */
	IDirectDrawSurface *primary_surface;
	/* Surface frames are decoded into and copied from. */
	IDirectDrawSurface *decode_surface;
	/* Display palette the movie's colors are written to. */
	IDirectDrawPalette *palette;
	/* DirectSound object handed to Smacker for the sound. */
	IDirectSound *direct_sound;
	/* Never read or written by name. */
	/* Unused playback-parameter slot; the sole caller leaves it
	 * uninitialized and all movie consumers skip offset 0x1C. */
	void *unused1c;
	/* The game window: blits land in its client area, and decoding waits
	 * while another window has focus. */
	void *window;
	/* Gets every window message first; a 0 from it skips the default
	 * handling. */
	movie_input_callback input_callback;
	/* Passed to input_callback; movie_play leaves it unset and both
	 * callbacks ignore it. */
	void *input_callback_context;
	/* Called with the current and last frame numbers; 1 ends playback.
	 * NULL ends playback at the last frame. */
	movie_progress_callback progress_callback;
	/* Passed to progress_callback; movie_play leaves it unset and the
	 * callback ignores it. */
	void *progress_callback_context;
};

/* A changed area of a movie frame, in decode surface pixels. */
struct movie_dirty_rect {
	int x; /* Left edge. */
	int y; /* Top edge. */
	/* Width; movie_merge_dirty_rect_lists negates it to mark a rectangle
	 * used. */
	int width;
	int height; /* Height. */
};

/* One player of a network game's movie. */
struct movie_multiplayer_sync_player {
	int player_id; /* DirectPlay id; 0 for an empty entry. */
	/* 1 once the player has finished or stopped the movie and waits for
	 * the others, 0 while watching. */
	int is_waiting;
};

extern struct movie_multiplayer_sync_player g_movie_multiplayer_sync_players[8];
extern xvt_file *g_movie_subtitle_file;

int movie_play(const char *name, int synchronize_multiplayer);
extern int g_movie_skip_requested;
extern int g_movie_playback_completion_state;
extern unsigned int g_movie_multiplayer_sync_deadline_ms;
extern int g_movie_previous_wnd_proc_mode;
int movie_multiplayer_input_callback(int window, unsigned int event_code,
				     int key_code, int lParam,
				     int callback_context,
				     uint32_t *playback_flag);
unsigned int movie_read_subtitle_cue(char *line1, char *line2, char *line3);

#ifdef __cplusplus
}
#endif

#endif
