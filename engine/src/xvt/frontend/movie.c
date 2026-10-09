#include "xvt/frontend/movie.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aeron/aeron.h"
#include "xvt/assets/file.h"
#include "xvt/frontend/frontend.h"
#include "xvt/frontend/frontend_dialog.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/frontend_draw.h"
#include "xvt/frontend/frontend_mission.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt/frontend/frontend_string.h"
#include "xvt/frontend/frontend_text.h"
#include "xvt/frontend/mission_briefing.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/net/frontend_net.h"
#include "xvt/net/net.h"
#include "xvt/net/net_send.h"
#include "xvt/util/time.h"
#include "xvt/util/win32.h"
#include "xvt_runtime/log/log.h"
#include "xvt_runtime/runtime/movie_task.h"

/* One palette color in the layout of a Windows palette entry. */
struct movie_palette_entry {
	uint8_t red;   /* Red, 0 to 255. */
	uint8_t green; /* Green, 0 to 255. */
	uint8_t blue;  /* Blue, 0 to 255. */
	/* 0 for the system's colors, 0x4 for the movie's (entries 10 to
	 * 245). */
	uint8_t flags;
};

/* DirectDraw's pixel format record; only the movie_get_pixel_format_fn typedef
 * names it. */
struct movie_pixel_format {
	uint32_t size;		/* Size of the record, set before the call. */
	uint32_t flags;		/* Set to 0x40 before the call. */
	uint32_t four_cc;	/* Never read or written by name. */
	uint32_t rgb_bit_count; /* Bits per pixel; 8 is a palette display. */
	uint32_t red_mask;	/* Red bits; compared with 0xF800 and 0x7C00. */
	uint32_t green_mask; /* Green bits; compared with 0x07E0 and 0x03E0. */
	uint32_t blue_mask;  /* Blue bits; compared with 0x001F. */
	uint32_t alpha_mask; /* Never read or written by name. */
};

/* Windows' window position record, which message 0x46 points to. */
struct movie_window_pos {
	void *window;	    /* Never read or written by name. */
	void *insert_after; /* Never read or written by name. */
	int x;	    /* New left edge. */
	int y;	    /* New top edge. */
	int width;  /* Never read or written by name. */
	int height; /* Never read or written by name. */
	unsigned int flags; /* Its 0x2 bit means the window does not move. */
};

/* Windows' message record, which the 1997 playback loop filled and passed
 * on. */
/* drift-ok: camelcase -- wParam, lParam: Windows' MSG */
struct movie_win32_message {
	void *window;	  /* Never read or written by name. */
	uint32_t message; /* Never read or written by name. */
	uint32_t wParam;  /* Never read or written by name. */
	int32_t lParam;	  /* Never read or written by name. */
	uint32_t time;	  /* Never read or written by name. */
	int32_t point_x;  /* Never read or written by name. */
	int32_t point_y;  /* Never read or written by name. */
};

/* The start of the Smacker library's movie handle, as far as this code reads
 * it; the library writes it. */
struct movie_smack_handle {
	uint32_t field00;     /* Never read or written by name. */
	uint32_t width;	      /* Frame width in pixels. */
	uint32_t height;      /* Frame height in pixels. */
	uint32_t frame_count; /* Frames in the movie. */
	uint8_t gap10[0x58];  /* Never read or written by name. */
	/* Nonzero when the frame to decode brings a new palette. */
	uint32_t palette_changed;
	/* Holds the movie's palette, 3 bytes a color, color 10 at offset 0x8A
	 * of the handle (the 1997 playback read colors 10 to 245); nothing else
	 * in it is read. */
	uint8_t gap6c[0x308];
	uint32_t current_frame; /* Index of the frame being shown. */
	uint8_t gap378[8];	/* Never read or written by name. */
	/* Left edge of the rectangle SmackToBufferRect last reported
	 * changed. */
	uint32_t dirty_x;
	uint32_t dirty_y;      /* Its top edge. */
	uint32_t dirty_width;  /* Its width; 0 when nothing changed there. */
	uint32_t dirty_height; /* Its height. */
};

typedef char xvt_size_movie_smack_handle
	[(sizeof(struct movie_smack_handle) == 0x390) ? 1 : -1];

typedef HRESULT(AERON_DXAPI *movie_get_pixel_format_fn)(
	IDirectDrawSurface *surface, struct movie_pixel_format *pixel_format);

/* Subtitle file of the movie playing, the movie's path with the extension txt;
 * NULL when none is open. xvt_movie_task_begin opens it and the movie task
 * closes it, and frontend_bootstrap_init_mode closes one still open. */
// GLOBAL: XVT 0xAA6078
xvt_file *g_movie_subtitle_file = NULL;
/* Window procedure mode to go back to when the movie ends. Saved by
 * xvt_movie_task_begin. */
// GLOBAL: XVT 0xAA6080
int g_movie_previous_wnd_proc_mode = 0;
/* 0 while the movie plays, 1 once its last frame is shown, which in network
 * play means waiting for the others, and 2 once that wait has passed its
 * deadline and the continue or exit prompt shows. The movie sync functions set
 * it. */
// GLOBAL: XVT 0xAA6084
int g_movie_playback_completion_state = 0;
/* GetTickCount time, in milliseconds, after which a network movie wait shows
 * its prompt: 5000 ms after the local movie ended for the host, 20000 for a
 * client; 0 until the wait starts. */
// GLOBAL: XVT 0xAA607C
unsigned int g_movie_multiplayer_sync_deadline_ms = 0;
/* Set to -1 when a client presses E at the timeout prompt to leave the game,
 * which ends playback with result 5; set to 0 when playback starts. In single
 * player a skip leaves it at 0. */
// GLOBAL: XVT 0x665D98
int g_movie_skip_requested = 0;
/* The players of a network game's movie: the first net_count_ready_players
 * entries of g_mp_roster at frame 0, and whether each still watches. Network
 * packets mark players waiting or remove them. */
// GLOBAL: XVT 0xAA6090
struct movie_multiplayer_sync_player g_movie_multiplayer_sync_players[8] = {
	{0}};

/* Plays a movie by name. It returns the result of a movie that has finished,
 * when one waits, and otherwise starts the movie through xvt_movie_task_begin,
 * which returns XVT_MOVIE_PENDING (-1), or 2 when it cannot start. */
// FUNCTION: XVT 0x4EFCE0
int movie_play(const char *name, int synchronize_multiplayer)
{
	int result;
	if (xvt_movie_task_take_result(&result)) {
		return result;
	}
	XVT_LOG_DEBUG("movie.requested name=\"%.127s\" sync=%d", name,
		      synchronize_multiplayer);
	return xvt_movie_task_begin(name, synchronize_multiplayer);
}

/* Input callback for network movies. Paint: clears and presents the screen.
 * Backspace, Enter, Esc, Space or a button release sends a movie sync packet of
 * 0 (this player waits), writes 0 to *playback_flag and returns 0; the movie
 * task stops its movie when it sees the 0. At the timeout prompt
 * (g_movie_playback_completion_state 2), C on the host sends a packet of 1,
 * which marks every player waiting, and E on a client sets
 * g_movie_skip_requested to -1 and restores the previous mode. Returns 1 when
 * it did not stop playback. It is called with characters and clicks. */
// FUNCTION: XVT 0x4F0140
int movie_multiplayer_input_callback(int window, unsigned int event_code,
				     int key_code, int lParam,
				     int callback_context,
				     uint32_t *playback_flag)
{
	enum {
		MOVIE_PAINT_EVENT = 15,
		MOVIE_EVENT_CHAR = 0x102,
		MOVIE_EVENT_LEFT_BUTTON_UP = 0x202,
		MOVIE_EVENT_RIGHT_BUTTON_UP = 0x205,
		MOVIE_EVENT_MIDDLE_BUTTON_UP = 0x208,
	};

	(void)window;
	(void)lParam;
	(void)callback_context;
	int stop_playback = 0;
	int packet[2];
	switch (event_code) {
	case MOVIE_PAINT_EVENT:
		frontend_display_clear_back_buffer();
		frontend_display_clear_offscreen_surface();
		frontend_display_present_frame();
		frontend_display_clear_back_buffer();
		break;
	case MOVIE_EVENT_CHAR:
		switch (key_code) {
		case 8:
		case 13:
		case 27:
		case 32:
			stop_playback = 1;
			break;
		case 'C':
		case 'c':
			if (g_movie_playback_completion_state == 2 &&
			    net_is_host() != 0) {
				packet[0] = NET_PACKET_MOVIE_SYNC;
				packet[1] = 1;
				net_send_packet_and_flush(0, packet,
							  sizeof(packet));
				XVT_LOG_INFO("movie.sync_continued");
			}
			break;
		case 'E':
		case 'e':
			if (g_movie_playback_completion_state == 2 &&
			    net_is_host() == 0) {
				g_movie_skip_requested = -1;
				frontend_display_set_wnd_proc_mode(
					g_movie_previous_wnd_proc_mode);
				XVT_LOG_INFO("movie.sync_left");
			}
			break;
		default:
			break;
		}
		break;
	case MOVIE_EVENT_LEFT_BUTTON_UP:
	case MOVIE_EVENT_RIGHT_BUTTON_UP:
	case MOVIE_EVENT_MIDDLE_BUTTON_UP:
		stop_playback = 1;
		break;
	default:
		break;
	}
	XVT_LOG_DEBUG("movie.sync_input input_code=%u key=%d stop=%d state=%d",
		      event_code, key_code, stop_playback,
		      g_movie_playback_completion_state);
	if (stop_playback == 1) {
		packet[0] = NET_PACKET_MOVIE_SYNC;
		packet[1] = 0;
		net_send_packet_and_flush(0, packet, sizeof(packet));
		*playback_flag = 0;
		return 0;
	}
	return 1;
}

/* Reads the next subtitle record from g_movie_subtitle_file: a frame number
 * line, then three text lines, each without its newline; a line starting with
 * '.' or missing at the end of the file reads as empty. Returns the frame
 * number; 0, reading nothing, when no file is open; 0xFFFF, with line1 emptied,
 * when no number can be read. Reads each line with a 256-byte limit. The movie
 * task calls it. */
// FUNCTION: XVT 0x4F0900
unsigned int movie_read_subtitle_cue(char *line1, char *line2, char *line3)
{
	if (g_movie_subtitle_file == NULL) {
		return 0;
	}
	unsigned int frame_number;
	int scan_result =
		FILE_SCANF(g_movie_subtitle_file, "%u\n", &frame_number);
	if (scan_result == 0 || scan_result == EOF) {
		XVT_LOG_DEBUG("movie.subtitles_ended scanned=%d", scan_result);
		if (scan_result == 0) {
			XVT_LOG_WARN("movie.subtitle_cue_invalid");
		}
		line1[0] = '\0';
		return UINT16_MAX;
	}

	if (FILE_GETS(line1, 256, g_movie_subtitle_file) == NULL) {
		line1[0] = '\0';
	} else {
		if (line1[strlen(line1) - 1] == '\n') {
			line1[strlen(line1) - 1] = '\0';
		}
		if (line1[0] == '.') {
			line1[0] = '\0';
		}
	}

	if (FILE_GETS(line2, 256, g_movie_subtitle_file) == NULL) {
		line2[0] = '\0';
	} else {
		if (line2[strlen(line2) - 1] == '\n') {
			line2[strlen(line2) - 1] = '\0';
		}
		if (line2[0] == '.') {
			line2[0] = '\0';
		}
	}

	if (FILE_GETS(line3, 256, g_movie_subtitle_file) == NULL) {
		line3[0] = '\0';
	} else {
		if (line3[strlen(line3) - 1] == '\n') {
			line3[strlen(line3) - 1] = '\0';
		}
		if (line3[0] == '.') {
			line3[0] = '\0';
		}
	}
	XVT_LOG_DEBUG("movie.subtitle_cue frame=%u lines=%d", frame_number,
		      (line1[0] != '\0') + (line2[0] != '\0') +
			      (line3[0] != '\0'));

	return frame_number;
}
