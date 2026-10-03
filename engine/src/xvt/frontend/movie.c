#include "xvt/frontend/movie.h"
#ifdef XVT_MODERN
#include "xvt_runtime/runtime/movie_task.h"
#endif

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
#include "xvt/util/time.h"
#include "xvt/util/win32.h"

#ifdef XVT_MODERN
#include "aeron/aeron.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* One palette color in the layout of a Windows palette entry. */
struct movie_palette_entry {
	uint8_t red;   /* Red, 0 to 255. */
	uint8_t green; /* Green, 0 to 255. */
	uint8_t blue;  /* Blue, 0 to 255. */
	/* 0 for the system's colors, 0x4 for the movie's (entries 10 to
	 * 245). */
	uint8_t flags;
};

/* DirectDraw's pixel format record, which movie_get_smack_buffer_format has the
 * primary surface fill. */
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
	int x;	    /* New left edge; movie_window_proc may change it. */
	int y;	    /* New top edge; kept as g_movie_client_offset_y. */
	int width;  /* Never read or written by name. */
	int height; /* Never read or written by name. */
	unsigned int flags; /* Its 0x2 bit means the window does not move. */
};

/* Windows' message record, filled and passed on by the original build's
 * playback loop. */
struct movie_win32_message {
	void *window;	  /* Never read or written by name. */
	uint32_t message; /* Never read or written by name. */
	uint32_t w_param; /* Never read or written by name. */
	int32_t l_param;  /* Never read or written by name. */
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
	 * of the handle, where movie_update_direct_draw_palette reads colors 10 to
	 * 245; nothing else in it is read. */
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

/* Top edge of the movie in the display, in pixels: (display_height - movie
 * height) / 2, unsigned, set by movie_run_smacker_playback when a movie starts.
 * The band above it holds the network sync status. Only the original build sets
 * or reads it. */
// GLOBAL: XVT 0x52C80C
int g_movie_y = 0;
/* Pixels left of the display's right edge beside the movie: display_width -
 * movie width - g_movie_x, set with g_movie_x. Only the original build sets or
 * reads it. */
// GLOBAL: XVT 0x52C810
int g_movie_right_margin = 0;

typedef HRESULT(AERON_DXAPI *movie_get_pixel_format_fn)(
	IDirectDrawSurface *surface, struct movie_pixel_format *pixel_format);

#ifndef XVT_MODERN
__declspec(dllimport) void *__stdcall GetFocus(void);
__declspec(dllimport) int __stdcall
SmackToBuffer(struct movie_smack_handle *handle, int x, int y, int pitch,
	      int height, void *pixels, int format);
__declspec(dllimport) int __stdcall
SmackDoFrame(struct movie_smack_handle *handle);
__declspec(dllimport) int __stdcall
SmackToBufferRect(struct movie_smack_handle *handle, int rect_index);
__declspec(dllimport) int __stdcall
SmackNextFrame(struct movie_smack_handle *handle);
__declspec(dllimport) void *__stdcall GetDC(void *h_wnd);
__declspec(dllimport) unsigned int __stdcall
GetSystemPaletteEntries(void *hdc, unsigned int start_index,
			unsigned int entry_count,
			struct movie_palette_entry *entries);
__declspec(dllimport) int __stdcall ReleaseDC(void *h_wnd, void *hdc);
__declspec(dllimport) void *__stdcall BeginPaint(void *h_wnd, void *paint);
__declspec(dllimport) int __stdcall EndPaint(void *h_wnd, const void *paint);
__declspec(dllimport) int __stdcall GetUpdateRect(void *h_wnd,
						  struct RECT *rect, int erase);
__declspec(dllimport) void __stdcall PostQuitMessage(int exit_code);
__declspec(dllimport) int32_t __stdcall
DefWindowProcA(void *h_wnd, unsigned int message, void *w_param, void *l_param);
__declspec(dllimport) int __stdcall ClientToScreen(void *h_wnd,
						   struct POINT *point);
__declspec(dllimport) int __stdcall
PeekMessageA(struct movie_win32_message *message, void *h_wnd,
	     unsigned int filter_min, unsigned int filter_max,
	     unsigned int remove_message);
__declspec(dllimport) int __stdcall
TranslateMessage(const struct movie_win32_message *message);
__declspec(dllimport) int32_t __stdcall
DispatchMessageA(const struct movie_win32_message *message);
__declspec(dllimport) void __stdcall
SmackSoundUseDirectSound(IDirectSound *direct_sound);
__declspec(dllimport) struct movie_smack_handle *__stdcall
SmackOpen(const char *file_name, unsigned int flags, int extra_buffer);
__declspec(dllimport) int __stdcall
SmackWait(struct movie_smack_handle *handle);
__declspec(dllimport) void __stdcall
SmackClose(struct movie_smack_handle *handle);
#else
int SmackToBuffer(struct movie_smack_handle *handle, int x, int y, int pitch,
		  int height, void *pixels, int format);
int SmackDoFrame(struct movie_smack_handle *handle);
int SmackToBufferRect(struct movie_smack_handle *handle, int rect_index);
int SmackNextFrame(struct movie_smack_handle *handle);
void SmackSoundUseDirectSound(IDirectSound *direct_sound);
struct movie_smack_handle *SmackOpen(const char *file_name, unsigned int flags,
				     int extra_buffer);
int SmackWait(struct movie_smack_handle *handle);
void SmackClose(struct movie_smack_handle *handle);
#endif

/* Left edge of the movie in the display, in pixels: (display_width - movie
 * width) / 2, unsigned, set by movie_run_smacker_playback when a movie starts.
 * Only the original build sets or reads it. */
// GLOBAL: XVT 0x52C808
int g_movie_x = 0;
/* Pixels below the movie: display_height - movie height - g_movie_y, set with
 * g_movie_y. Subtitles and the network timeout prompt are drawn there. Only the
 * original build sets or reads it. */
// GLOBAL: XVT 0x52C814
unsigned int g_movie_bottom_margin = 0;
/* Parameters of the movie playing: movie_run_smacker_playback sets it when it
 * starts and NULL when it ends or fails. Only the original build sets it. */
// GLOBAL: XVT 0x52C818
const struct movie_playback_params *g_movie_playback_params = 0;
/* Smacker handle of the movie playing. movie_run_smacker_playback opens it and
 * closes it at the end, leaving the pointer as it was. Only the original build
 * opens one. */
// GLOBAL: XVT 0x52C81C
struct movie_smack_handle *g_movie_smack_handle = 0;
/* Screen position of the window's client area, added to every blit's
 * destination. movie_run_smacker_playback passes it to ClientToScreen at each
 * start; nothing resets it, and ClientToScreen adds the origin to the point it
 * already holds. Only the original build sets it. */
// GLOBAL: XVT 0x52C820
struct POINT g_movie_client_screen_origin = {0, 0};
/* Horizontal window offset added to every blit's destination: the x of the last
 * window move movie_window_proc saw during playback, rounded as that function
 * says. Never reset. */
// GLOBAL: XVT 0x6661A0
int g_movie_client_offset_x = 0;
/* Vertical window offset added to every blit's destination: the y of the last
 * window move movie_window_proc saw during playback. Never reset. */
// GLOBAL: XVT 0x6661A4
int g_movie_client_offset_y = 0;
/* Set to 1 by movie_decode_and_present_frame once a frame has been decoded, and
 * never cleared; movie_handle_paint repaints only while it is set. */
// GLOBAL: XVT 0x52C828
int g_movie_frame_available = 0;
/* Smacker buffer format for SmackToBuffer, from movie_get_smack_buffer_format when
 * a movie starts. */
// GLOBAL: XVT 0x52C82C
int g_movie_smack_buffer_format = 0;
/* Rectangles in g_movie_previous_dirty_rects; set by movie_decode_and_present_frame
 * when it swaps the lists. */
// GLOBAL: XVT 0x52C830
unsigned int g_movie_previous_dirty_rect_count = 0;
/* One of the two 256-entry lists that g_movie_previous_dirty_rects and
 * g_movie_current_dirty_rects swap between. */
// GLOBAL: XVT 0x6661A8
static struct movie_dirty_rect g_movie_dirty_rects_a[256] = {{0}};
/* The other of the two 256-entry lists that g_movie_previous_dirty_rects and
 * g_movie_current_dirty_rects swap between. */
// GLOBAL: XVT 0x6671A8
static struct movie_dirty_rect g_movie_dirty_rects_b[256] = {{0}};
/* The changed rectangles of the frame before, kept in page-flip playback;
 * swapped with g_movie_current_dirty_rects after each frame. */
// GLOBAL: XVT 0x52C834
struct movie_dirty_rect *g_movie_previous_dirty_rects = g_movie_dirty_rects_a;
/* The list the frame being decoded fills with its changed rectangles in
 * page-flip playback. */
// GLOBAL: XVT 0x52C838
struct movie_dirty_rect *g_movie_current_dirty_rects = g_movie_dirty_rects_b;
/* The palette written to the playback palette: the system's colors kept at
 * entries 0 to 9 and 246 to 255, the movie's colors at 10 to 245.
 * movie_initialize_system_palette fills it and movie_update_direct_draw_palette
 * copies the movie's colors in. */
// GLOBAL: XVT 0x665DA0
struct movie_palette_entry g_movie_palette_entries[256] = {{0}};
/* Subtitle file of the movie playing, the movie's path with the extension txt;
 * NULL when none is open. The original build's movie_run_smacker_playback opens
 * it and closes it at the end, and frontend_bootstrap_init_mode closes one still
 * open; in the modern build xvt_movie_task_begin opens it and the movie task
 * closes it. */
// GLOBAL: XVT 0xAA6078
xvt_file *g_movie_subtitle_file = NULL;
/* Second line of the subtitle being shown; movie_draw_subtitles reads the first
 * into g_frontend_scratch_buffer. */
// GLOBAL: XVT 0xAA5E70
static char g_movie_subtitle_line2[256] = {0};
/* Third line of the subtitle being shown. */
// GLOBAL: XVT 0xAA5F70
static char g_movie_subtitle_line3[256] = {0};
/* Frame at which the subtitle being shown began; movie_draw_subtitles draws it
 * on that frame and the next. Set to 0 at frame 0. */
// GLOBAL: XVT 0xAA6070
static unsigned int g_movie_active_subtitle_frame = 0;
/* Frame at which movie_draw_subtitles reads the next cue: the number of the last
 * cue read, 0xFFFF after the last one. Set to 0 at frame 0. */
// GLOBAL: XVT 0xAA6074
static unsigned int g_movie_next_subtitle_frame = 0;
/* Window procedure mode to go back to when the movie ends; setting the mode
 * back ends the original build's playback loop. Saved by
 * movie_run_smacker_playback, and by xvt_movie_task_begin in the modern build. */
// GLOBAL: XVT 0xAA6080
int g_movie_previous_wnd_proc_mode = 0;
/* 0 while the movie plays, 1 once its last frame is shown, which in network
 * play means waiting for the others, and 2 once that wait has passed its
 * deadline and the continue or exit prompt shows. The original build's
 * movie_run_smacker_playback, movie_decode_and_present_frame and
 * movie_update_multiplayer_sync_timeout set 0, 1 and 2; the modern build's movie
 * sync functions set them. */
// GLOBAL: XVT 0xAA6084
int g_movie_playback_completion_state = 0;
/* GetTickCount time, in milliseconds, after which a network movie wait shows
 * its prompt: 5000 ms after the local movie ended for the host, 20000 for a
 * client; 0 until the wait starts. The original build makes a sum of 0 into
 * 1. */
// GLOBAL: XVT 0xAA607C
unsigned int g_movie_multiplayer_sync_deadline_ms = 0;
/* Set to -1 when a client presses E at the timeout prompt to leave the game,
 * which ends playback with result 5; set to 0 when playback starts. In single
 * player a skip leaves it at 0. */
// GLOBAL: XVT 0x665D98
int g_movie_skip_requested = 0;
/* Output of movie_merge_dirty_rect_lists when it merges two lists. */
// GLOBAL: XVT 0x6681A8
struct movie_dirty_rect g_movie_merged_dirty_rects[256] = {{0}};
/* The players of a network game's movie: the first net_count_ready_players
 * entries of g_mp_roster at frame 0, and whether each still watches. Network
 * packets mark players waiting or remove them. */
// GLOBAL: XVT 0xAA6090
struct movie_multiplayer_sync_player g_movie_multiplayer_sync_players[8] = {
	{0}};

/* Copies the rectangle at (x, y), width by height, of the decode surface to the
 * same place on the display, moved by g_movie_client_screen_origin and
 * g_movie_client_offset_x and g_movie_client_offset_y. In page-flip full screen it
 * copies to the primary surface's back buffer, first restoring a lost primary
 * and setting its palette again; otherwise straight to the primary surface. It
 * retries while BltFast reports a lost surface, restoring the surfaces it
 * checks, and stops at a failed restore. Returns the last DirectDraw result.
 * Only the original build reaches it. */
// FUNCTION: XVT 0x4EECB0
HRESULT movie_blit_rect_to_display(int x, int y, int width, int height)
{
	IDirectDrawSurface *back_buffer;
	DDSCAPS caps;
	struct RECT source_rect;
	uint32_t destination_x;
	uint32_t destination_y;
	HRESULT result;

	source_rect.left = x;
	destination_x =
		g_movie_client_offset_x + g_movie_client_screen_origin.x + x;
	source_rect.top = y;
	destination_y =
		g_movie_client_screen_origin.y + g_movie_client_offset_y + y;
	source_rect.right = x + width;
	source_rect.bottom = y + height;
	if (g_opt_no_fullscreen == 0 && g_no_page_flip == 0) {
		caps.dwCaps = DDSCAPS_BACKBUFFER;
		result = g_movie_playback_params->primary_surface->lpVtbl
				 ->GetAttachedSurface(g_movie_playback_params
							      ->primary_surface,
						      &caps, &back_buffer);
		while (result == DX_DDERR_SURFACELOST) {
			result =
				g_movie_playback_params->primary_surface->lpVtbl
					->Restore(g_movie_playback_params
							  ->primary_surface);
			if (result != 0) {
				return result;
			}
			g_movie_playback_params->primary_surface->lpVtbl
				->SetPalette(g_movie_playback_params
						     ->primary_surface,
					     g_movie_playback_params->palette);
			movie_update_direct_draw_palette();
			result = g_movie_playback_params->primary_surface
					 ->lpVtbl->GetAttachedSurface(
						 g_movie_playback_params
							 ->primary_surface,
						 &caps, &back_buffer);
		}
		do {
			result = back_buffer->lpVtbl->BltFast(
				back_buffer, destination_x, destination_y,
				g_movie_playback_params->decode_surface,
				&source_rect, DDBLTFAST_WAIT);
			if (result != DX_DDERR_SURFACELOST) {
				break;
			}
			if (g_movie_playback_params->decode_surface->lpVtbl
				    ->IsLost(g_movie_playback_params
						     ->decode_surface) ==
			    DX_DDERR_SURFACELOST) {
				result =
					g_movie_playback_params->decode_surface
						->lpVtbl->Restore(
							g_movie_playback_params
								->decode_surface);
				if (result != 0) {
					break;
				}
			}
		} while (1);
	} else {
		do {
			result =
				g_movie_playback_params->primary_surface->lpVtbl
					->BltFast(g_movie_playback_params
							  ->primary_surface,
						  destination_x, destination_y,
						  g_movie_playback_params
							  ->decode_surface,
						  &source_rect, DDBLTFAST_WAIT);
			if (result != DX_DDERR_SURFACELOST) {
				break;
			}
			if (g_movie_playback_params->primary_surface->lpVtbl
				    ->IsLost(g_movie_playback_params
						     ->primary_surface) ==
			    DX_DDERR_SURFACELOST) {
				result =
					g_movie_playback_params->primary_surface
						->lpVtbl->Restore(
							g_movie_playback_params
								->primary_surface);
				if (result != 0) {
					break;
				}
				g_movie_playback_params->primary_surface->lpVtbl
					->SetPalette(g_movie_playback_params
							     ->primary_surface,
						     g_movie_playback_params
							     ->palette);
				movie_update_direct_draw_palette();
			}
			if (g_movie_playback_params->decode_surface->lpVtbl
				    ->IsLost(g_movie_playback_params
						     ->decode_surface) ==
			    DX_DDERR_SURFACELOST) {
				result =
					g_movie_playback_params->decode_surface
						->lpVtbl->Restore(
							g_movie_playback_params
								->decode_surface);
				if (result != 0) {
					break;
				}
			}
		} while (1);
	}
	return result;
}

/* Copies the movie's colors 10 to 245 from the Smacker handle's palette, 3
 * bytes each, into g_movie_palette_entries and writes all 256 entries to the
 * playback palette. Returns that call's result. Only the original build reaches
 * it. */
// FUNCTION: XVT 0x4EEE70
HRESULT movie_update_direct_draw_palette(void)
{
	struct movie_palette_entry *entry;
	uint8_t *smack_color;
	unsigned int index;

	entry = &g_movie_palette_entries[10];
	smack_color = (uint8_t *)g_movie_smack_handle + 0x8A;
	for (index = 10; index < 246; ++index) {
		entry->red = *smack_color++;
		entry->green = *smack_color++;
		entry->blue = *smack_color++;
		++entry;
	}
	return g_movie_playback_params->palette->lpVtbl->SetEntries(
		g_movie_playback_params->palette, 0, 0, 256,
		g_movie_palette_entries);
}

/* Window procedure while a movie plays. Each message first goes to the
 * playback's input callback, when one is set. Messages 0x02, 0x46 and 0x30F are
 * then handled whatever it answered. 0x02 (destroy) restores the previous mode,
 * shuts the display down and quits, returning 0. 0x46 (window moving), without
 * its 0x2 flag, sets the new x to ((origin x + x + 1) & 0xFFFC) - origin x, cut
 * to 16 bits, with origin x from g_movie_client_screen_origin, and stores that x
 * and the new y as the client offsets. 0x30F sets the playback palette on the
 * primary surface and returns 0. When the callback answered 0, the result it
 * wrote is returned. Otherwise, default handling: 0x0F (paint) runs
 * movie_handle_paint and returns 0, 0x14 returns 1, 0x311 from this window
 * returns 0, and anything else goes to DefWindowProcA, or returns 0 in the
 * modern build. Only the original build reaches it: its caller,
 * frontend_display_wnd_proc, is the window procedure only that build
 * registers. */
// FUNCTION: XVT 0x4EEEC0
int32_t AERON_DXAPI movie_window_proc(void *h_wnd, unsigned int message,
				      void *w_param, void *l_param)
{
	int use_default;
	int handled_result;
	struct movie_window_pos *window_pos;
	uint16_t origin_x;
	uint16_t aligned_x;

	use_default = 1;
	handled_result = 0;
	if (g_movie_playback_params != NULL &&
	    g_movie_playback_params->input_callback != NULL) {
		use_default = g_movie_playback_params->input_callback(
			h_wnd, message, w_param, l_param,
			g_movie_playback_params->input_callback_context,
			&handled_result);
	}

	switch (message) {
	case 0x02:
		frontend_display_set_wnd_proc_mode(
			g_movie_previous_wnd_proc_mode);
		frontend_display_shutdown(1);
#ifndef XVT_MODERN
		PostQuitMessage(0);
#else
		Aeron_RequestQuit();
#endif
		return 0;
	case 0x46:
		window_pos = l_param;
		if ((window_pos->flags & 2) == 0) {
			origin_x = (uint16_t)g_movie_client_screen_origin.x;
			aligned_x = (uint16_t)((origin_x + window_pos->x + 1) &
					       0xFFFC);
			aligned_x = (uint16_t)(aligned_x - origin_x);
			window_pos->x = aligned_x;
			g_movie_client_offset_x = aligned_x;
			g_movie_client_offset_y = window_pos->y;
		}
		break;
	case 0x30F:
		if (g_movie_playback_params != NULL &&
		    g_movie_playback_params->primary_surface != NULL &&
		    g_movie_playback_params->palette != NULL) {
			g_movie_playback_params->primary_surface->lpVtbl
				->SetPalette(g_movie_playback_params
						     ->primary_surface,
					     g_movie_playback_params->palette);
		}
		return 0;
	default:
		break;
	}

	if (use_default == 0) {
		return handled_result;
	}
	switch (message) {
	case 0x0F:
		movie_handle_paint(h_wnd);
		return 0;
	case 0x14:
		return 1;
	case 0x311:
		if (w_param == h_wnd) {
			return 0;
		}
		break;
	default:
		break;
	}
#ifndef XVT_MODERN
	return DefWindowProcA(h_wnd, (uint16_t)message, w_param, l_param);
#else
	return 0;
#endif
}

/* Repaints the movie after a paint message. Once a frame has been decoded and a
 * movie is playing: in page-flip full screen it copies the rectangle from (0,
 * 0) the size of the movie and flips, returning Flip's result; otherwise it
 * copies the window's update rectangle (the movie's size in the modern build)
 * and returns the copy's result. Else returns EndPaint's result, or 0 in the
 * modern build. Only the original build reaches it, through
 * movie_window_proc. */
// FUNCTION: XVT 0x4EF040
int movie_handle_paint(void *h_wnd)
{
	struct RECT update_rect;
#ifndef XVT_MODERN
	uint8_t paint[64];
#endif
	int result;

#ifndef XVT_MODERN
	BeginPaint(h_wnd, paint);
	result = EndPaint(h_wnd, paint);
#else
	(void)h_wnd;
	result = 0;
#endif
	if (g_movie_frame_available != 0 && g_movie_smack_handle != NULL &&
	    g_movie_playback_params != NULL) {
		if (g_opt_no_fullscreen == 0 && g_no_page_flip == 0) {
			movie_blit_rect_to_display(
				0, 0, ((int *)g_movie_smack_handle)[1],
				((int *)g_movie_smack_handle)[2]);
			return g_movie_playback_params->primary_surface->lpVtbl
				->Flip(g_movie_playback_params->primary_surface,
				       NULL, 1);
		}
#ifndef XVT_MODERN
		GetUpdateRect(h_wnd, &update_rect, 0);
#else
		update_rect.left = 0;
		update_rect.top = 0;
		update_rect.right = ((int *)g_movie_smack_handle)[1];
		update_rect.bottom = ((int *)g_movie_smack_handle)[2];
#endif
		return movie_blit_rect_to_display(
			update_rect.left, update_rect.top,
			update_rect.right - update_rect.left,
			update_rect.bottom - update_rect.top);
	}
	return result;
}

/* Plays a Smacker movie to its end. The modern build hands it to
 * xvt_movie_task_begin instead; only the original build calls it, from
 * movie_play. Sets g_movie_playback_params, the palette, the buffer format and
 * the client origin, clears and presents the screen, and opens movies\NAME.smk,
 * or the same on the CD drive; returns 2 when neither opens. Opens the subtitle
 * file and returns 3, leaving that file open, when the movie is both wider and
 * taller than the display. Centers the movie (g_movie_x, g_movie_y and the
 * margins), switches the window procedure to the movie's (mode 2), and loops
 * while it stays there: pumping network packets and window messages, decoding
 * each frame when Smacker says it is due, and after the last frame calling the
 * progress callback until it returns 1, or ending at once without one. Closes
 * the files and returns 0, or 5 when g_movie_skip_requested is set; a skip in
 * single player returns 0. */
// FUNCTION: XVT 0x4EF100
int movie_run_smacker_playback(const struct movie_playback_params *params)
{
#ifdef XVT_MODERN
	return xvt_movie_task_begin(params->movie_name,
				    params->progress_callback != NULL);
#else
	enum {
		MOVIE_STATUS_OK = 0,
		MOVIE_STATUS_NOT_FOUND = 2,
		MOVIE_STATUS_TOO_LARGE = 3,
		MOVIE_STATUS_SKIPPED = 5,
		MOVIE_WINDOW_MODE = 2,
		SMACK_OPEN_FLAGS = 0xFE000,
		SMACK_DEFAULT_EXTRA_BUFFER = -1,
		REMOVE_MESSAGE = 1,
		FLIP_WAIT = 1,
		PROGRESS_FINISHED = 1,
		MOVIE_PATH_CAPACITY = 256,
		MOVIE_EXTENSION_LENGTH = 3,
		MOVIE_EXTENSION_LAST_INDEX = 2,
	};
	struct movie_win32_message message;
	char file_name[MOVIE_PATH_CAPACITY];
	char *subtitle_extension;
	movie_progress_callback progress_callback;

	g_movie_playback_params = params;
	movie_initialize_system_palette(params->window);
	g_movie_smack_buffer_format = movie_get_smack_buffer_format();
	ClientToScreen(g_movie_playback_params->window,
		       &g_movie_client_screen_origin);
	frontend_display_clear_back_buffer();
	frontend_display_clear_offscreen_surface();
	frontend_display_present_frame();
	frontend_display_clear_back_buffer();
	SmackSoundUseDirectSound(g_movie_playback_params->direct_sound);

	strcpy(file_name, "movies\\");
	strcat(file_name, g_movie_playback_params->movie_name);
	strcat(file_name, ".smk");
	g_movie_smack_handle = SmackOpen(file_name, SMACK_OPEN_FLAGS,
					 SMACK_DEFAULT_EXTRA_BUFFER);
	if (g_movie_smack_handle == NULL) {
		strcpy(file_name, "d:\\movies\\");
		file_name[0] = file_get_cd_drive_letter();
		strcat(file_name, g_movie_playback_params->movie_name);
		strcat(file_name, ".smk");
		g_movie_smack_handle = SmackOpen(file_name, SMACK_OPEN_FLAGS,
						 SMACK_DEFAULT_EXTRA_BUFFER);
		if (g_movie_smack_handle == NULL) {
			g_movie_playback_params = NULL;
			return MOVIE_STATUS_NOT_FOUND;
		}
	}

	subtitle_extension =
		file_name + strlen(file_name) - MOVIE_EXTENSION_LENGTH;
	subtitle_extension[MOVIE_EXTENSION_LAST_INDEX] = 't';
	subtitle_extension[0] = 't';
	subtitle_extension[1] = 'x';
	g_movie_subtitle_file = FILE_RAW_OPEN(file_name, "r");
	if (g_movie_smack_handle->width >
		    (unsigned int)g_movie_playback_params->display_width &&
	    g_movie_smack_handle->height >
		    (unsigned int)g_movie_playback_params->display_height) {
		g_movie_playback_params = NULL;
		SmackClose(g_movie_smack_handle);
		return MOVIE_STATUS_TOO_LARGE;
	}

	g_movie_x = ((unsigned int)g_movie_playback_params->display_width -
		     g_movie_smack_handle->width) /
		    2;
	g_movie_y = ((unsigned int)g_movie_playback_params->display_height -
		     g_movie_smack_handle->height) /
		    2;
	g_movie_right_margin = g_movie_playback_params->display_width -
			       g_movie_smack_handle->width - g_movie_x;
	g_movie_bottom_margin = g_movie_playback_params->display_height -
				g_movie_smack_handle->height - g_movie_y;
	g_movie_previous_wnd_proc_mode = frontend_display_get_wnd_proc_mode();
	frontend_display_set_wnd_proc_mode(MOVIE_WINDOW_MODE);
	g_movie_playback_completion_state = 0;
	g_movie_skip_requested = 0;
	while (frontend_display_get_wnd_proc_mode() == MOVIE_WINDOW_MODE) {
		net_pump_incoming_packets();
		if (PeekMessageA(&message, NULL, 0, 0, REMOVE_MESSAGE) != 0) {
			TranslateMessage(&message);
			DispatchMessageA(&message);
		} else if (g_movie_playback_completion_state == 0) {
			if (SmackWait(g_movie_smack_handle) == 0) {
				movie_decode_and_present_frame();
			}
		} else {
			progress_callback =
				g_movie_playback_params->progress_callback;
			if (progress_callback != NULL) {
				if (progress_callback(
					    g_movie_smack_handle->current_frame,
					    g_movie_smack_handle->frame_count -
						    1,
					    g_movie_playback_params
						    ->progress_callback_context) ==
				    PROGRESS_FINISHED) {
					frontend_display_set_wnd_proc_mode(
						g_movie_previous_wnd_proc_mode);
				}
				if (g_opt_no_fullscreen == 0 &&
				    g_no_page_flip == 0) {
					g_movie_playback_params->primary_surface
						->lpVtbl
						->Flip(g_movie_playback_params
							       ->primary_surface,
						       NULL, FLIP_WAIT);
				}
			} else {
				frontend_display_set_wnd_proc_mode(
					g_movie_previous_wnd_proc_mode);
			}
		}
	}

	if (g_movie_subtitle_file != NULL) {
		FILE_RAW_CLOSE(g_movie_subtitle_file);
		g_movie_subtitle_file = NULL;
	}
	SmackClose(g_movie_smack_handle);
	g_movie_playback_params = NULL;
	return g_movie_skip_requested == 0 ? MOVIE_STATUS_OK
					   : MOVIE_STATUS_SKIPPED;
#endif
}

/* Picks the Smacker buffer format for the primary surface's pixel format:
 * 0xC0000000 for 16 bits with 5-6-5 masks, 0x80000000 for 5-5-5 masks, else 0,
 * as for 8 bits. Only the original build reaches it. */
// FUNCTION: XVT 0x4EF500
int movie_get_smack_buffer_format(void)
{
	struct movie_pixel_format pixel_format;
	IDirectDrawSurface *surface;

	pixel_format.size = sizeof(pixel_format);
	pixel_format.flags = 0x40;
	surface = g_movie_playback_params->primary_surface;
	((movie_get_pixel_format_fn)surface->lpVtbl->GetPixelFormat)(
		surface, &pixel_format);

	if (pixel_format.rgb_bit_count == 8) {
		return 0;
	}
	if (pixel_format.red_mask == 0xF800 &&
	    pixel_format.green_mask == 0x07E0 &&
	    pixel_format.blue_mask == 0x001F) {
		return (int)0xC0000000u;
	}
	if (pixel_format.red_mask == 0x7C00 &&
	    pixel_format.green_mask == 0x03E0 &&
	    pixel_format.blue_mask == 0x001F) {
		return (int)0x80000000u;
	}
	return 0;
}

/* Prepares g_movie_palette_entries: the original build first reads the system
 * palette into it. Entries 0 to 9 and 246 to 255 get flags 0 and entries 10 to
 * 245 flags 0x4. Returns ReleaseDC's result, or 1 in the modern build. Only the
 * original build reaches it. */
// FUNCTION: XVT 0x4EF590
int movie_initialize_system_palette(void *h_wnd)
{
	int index;
#ifndef XVT_MODERN
	void *dc;

	dc = GetDC(h_wnd);
	GetSystemPaletteEntries(dc, 0, 256, g_movie_palette_entries);
#else
	(void)h_wnd;
#endif

	for (index = 0; index < 10; ++index) {
		g_movie_palette_entries[index].flags = 0;
	}
	for (index = 10; index < 246; ++index) {
		g_movie_palette_entries[index].flags = 4;
	}
	for (index = 246; index < 256; ++index) {
		g_movie_palette_entries[index].flags = 0;
	}

#ifdef XVT_MODERN
	return 1;
#else
	return ReleaseDC(h_wnd, dc);
#endif
}

/* Decodes and shows one movie frame; the modern build's body is empty, and only
 * the original build calls it. Does nothing while the playback window lacks
 * focus. Updates the palette when the frame changes it, decodes into the decode
 * surface at (g_movie_x, g_movie_y), sets g_movie_frame_available, draws the
 * subtitles and calls the progress callback, restoring the previous mode when
 * it returns 1. In page-flip full screen it copies the merged changed
 * rectangles of this frame and the frame before (movie_merge_dirty_rect_lists),
 * flips, and swaps the two lists; otherwise it copies each changed rectangle.
 * At the last frame it sets g_movie_playback_completion_state to 1, else it steps
 * Smacker to the next frame. Does not check the changed rectangles against the
 * lists' 256 entries. */
// FUNCTION: XVT 0x4EF600
void movie_decode_and_present_frame(void)
{
#ifndef XVT_MODERN
	DDSURFACEDESC surface_desc;
	struct movie_dirty_rect *merged_rects;
	unsigned int merged_count;
	unsigned int current_count;
	unsigned int dirty_index;
	void *focus_window;
	int result;

	focus_window = GetFocus();
	if (g_movie_playback_params->window != focus_window) {
		return;
	}
	if (g_movie_smack_handle->palette_changed != 0) {
		movie_update_direct_draw_palette();
	}
	surface_desc.dwSize = sizeof(surface_desc);
	while (g_movie_playback_params->decode_surface->lpVtbl->Lock(
		       g_movie_playback_params->decode_surface, NULL,
		       &surface_desc, 1, NULL) == DX_DDERR_SURFACELOST) {
		result = g_movie_playback_params->decode_surface->lpVtbl
				 ->Restore(g_movie_playback_params
						   ->decode_surface);
		if (result != 0) {
			return;
		}
	}
	SmackToBuffer(g_movie_smack_handle, g_movie_x, g_movie_y,
		      surface_desc.lPitch, g_movie_smack_handle->height,
		      surface_desc.lpSurface, g_movie_smack_buffer_format);
	SmackDoFrame(g_movie_smack_handle);
	g_movie_frame_available = 1;
	g_movie_playback_params->decode_surface->lpVtbl->Unlock(
		g_movie_playback_params->decode_surface,
		surface_desc.lpSurface);
	movie_draw_subtitles(g_movie_smack_handle->current_frame);
	if (g_movie_playback_params->progress_callback != NULL &&
	    g_movie_playback_params->progress_callback(
		    g_movie_smack_handle->current_frame,
		    g_movie_smack_handle->frame_count - 1,
		    g_movie_playback_params->progress_callback_context) == 1) {
		frontend_display_set_wnd_proc_mode(
			g_movie_previous_wnd_proc_mode);
	}
	if (g_opt_no_fullscreen == 0 && g_no_page_flip == 0) {
		current_count = 0;
		if (SmackToBufferRect(g_movie_smack_handle, 0) != 0) {
			dirty_index = 0;
			do {
				if (g_movie_smack_handle->dirty_width != 0) {
					g_movie_current_dirty_rects[dirty_index]
						.x =
						g_movie_smack_handle->dirty_x;
					++dirty_index;
					++current_count;
					g_movie_current_dirty_rects
						[dirty_index - 1]
							.y =
						g_movie_smack_handle->dirty_y;
					g_movie_current_dirty_rects
						[dirty_index - 1]
							.width =
						g_movie_smack_handle
							->dirty_width;
					g_movie_current_dirty_rects
						[dirty_index - 1]
							.height =
						g_movie_smack_handle
							->dirty_height;
				}
			} while (SmackToBufferRect(g_movie_smack_handle, 0) !=
				 0);
		}
		movie_merge_dirty_rect_lists(g_movie_current_dirty_rects,
					     current_count,
					     g_movie_previous_dirty_rects,
					     g_movie_previous_dirty_rect_count,
					     &merged_rects, &merged_count);
		if (merged_count-- != 0) {
			do {
				movie_blit_rect_to_display(
					merged_rects[merged_count].x,
					merged_rects[merged_count].y,
					merged_rects[merged_count].width,
					merged_rects[merged_count].height);
			} while (merged_count-- != 0);
		}
		g_movie_playback_params->primary_surface->lpVtbl->Flip(
			g_movie_playback_params->primary_surface, NULL, 1);
		/* merged_rects is reused as the temporary for swapping the previous and current dirty lists. */
		merged_rects = g_movie_previous_dirty_rects;
		g_movie_previous_dirty_rects = g_movie_current_dirty_rects;
		g_movie_current_dirty_rects = merged_rects;
		g_movie_previous_dirty_rect_count = current_count;
	} else {
		while (SmackToBufferRect(g_movie_smack_handle, 0) != 0) {
			movie_blit_rect_to_display(
				g_movie_smack_handle->dirty_x,
				g_movie_smack_handle->dirty_y,
				g_movie_smack_handle->dirty_width,
				g_movie_smack_handle->dirty_height);
		}
	}
	if (g_movie_smack_handle->frame_count -
		    g_movie_smack_handle->current_frame ==
	    1) {
		g_movie_playback_completion_state = 1;
		return;
	}
	SmackNextFrame(g_movie_smack_handle);
#endif
}

/* Merges two lists of changed rectangles into fewer, larger ones. When either
 * list is empty it returns the other as it is; otherwise it writes
 * g_movie_merged_dirty_rects. Starting from the first current rectangle, it
 * repeatedly finds the unused rectangle whose bounding box with the one being
 * built adds the least area beyond both (stopping early at 0), marking used
 * rectangles by negating their width. When that added area is 0, or the box's
 * area divided by it is 20 or more, the box replaces the one being built;
 * otherwise the one being built is written out and the found rectangle starts
 * the next. Restores every width to its absolute value at the end. Does not
 * check the output against 256 entries. Only the original build reaches it. */
// FUNCTION: XVT 0x4EF8E0
void movie_merge_dirty_rect_lists(struct movie_dirty_rect *current_rects,
				  unsigned int current_count,
				  struct movie_dirty_rect *previous_rects,
				  unsigned int previous_count,
				  struct movie_dirty_rect **merged_rects,
				  unsigned int *merged_count)
{
	struct movie_dirty_rect candidate;
	struct movie_dirty_rect best_union;
	struct movie_dirty_rect rect_union;
	struct movie_dirty_rect intersection;
	struct movie_dirty_rect *best_rect;
	struct movie_dirty_rect *output;
	unsigned int remaining;
	unsigned int output_count;
	unsigned int index;
	unsigned int restore_index;

	if (current_count == 0) {
		*merged_rects = previous_rects;
		*merged_count = previous_count;
		return;
	}
	if (previous_count == 0) {
		*merged_rects = current_rects;
		*merged_count = current_count;
		return;
	}

	*merged_rects = g_movie_merged_dirty_rects;
	output_count = 0;
	candidate = current_rects[0];
	remaining = current_count + previous_count - 1;
	current_rects[0].width = -current_rects[0].width;
	if (remaining != 0) {
		output = g_movie_merged_dirty_rects;
		do {
			unsigned int best_cost;
			unsigned int best_union_area;
			unsigned int index;
			unsigned int previous_index;
			int candidate_area;

			best_cost = 0x1000000;
			candidate_area = candidate.width * candidate.height;
			for (index = 0; index < current_count; ++index) {
				unsigned int cost;
				int union_area;

				if (current_rects[index].width <= 0) {
					continue;
				}
				movie_compute_rect_union_and_intersection(
					&current_rects[index], &candidate,
					&rect_union, &intersection);
				union_area =
					rect_union.width * rect_union.height;
				cost = intersection.width *
					       intersection.height -
				       current_rects[index].height *
					       current_rects[index].width -
				       candidate_area + union_area;
				if (cost < best_cost) {
					best_cost = cost;
					best_union_area = union_area;
					best_union = rect_union;
					best_rect = &current_rects[index];
					if (cost == 0) {
						break;
					}
				}
			}

			for (previous_index = 0;
			     previous_index < previous_count;
			     ++previous_index) {
				unsigned int cost;
				int union_area;

				if (previous_rects[previous_index].width <= 0) {
					continue;
				}
				movie_compute_rect_union_and_intersection(
					&previous_rects[previous_index],
					&candidate, &rect_union, &intersection);
				union_area =
					rect_union.width * rect_union.height;
				cost = intersection.width *
					       intersection.height -
				       previous_rects[previous_index].height *
					       previous_rects[previous_index]
						       .width -
				       candidate_area + union_area;
				if (cost < best_cost) {
					best_cost = cost;
					best_union_area = union_area;
					best_union = rect_union;
					best_rect =
						&previous_rects[previous_index];
					if (cost == 0) {
						break;
					}
				}
			}

			if (best_cost != 0 &&
			    best_union_area / best_cost < 20) {
				*output++ = candidate;
				++output_count;
				if (best_cost != 0x1000000) {
					candidate = *best_rect;
					best_rect->width = -best_rect->width;
				}
			} else {
				candidate = best_union;
				best_rect->width = -best_rect->width;
			}
			--remaining;
		} while (remaining != 0);
	}

	g_movie_merged_dirty_rects[output_count] = candidate;
	*merged_count = output_count + 1;
	for (index = 0; index < current_count; ++index) {
		current_rects[index].width = abs(current_rects[index].width);
	}
	for (restore_index = 0; restore_index < previous_count;
	     ++restore_index) {
		previous_rects[restore_index].width =
			abs(previous_rects[restore_index].width);
	}
}

/* Computes the bounding union and overlap intersection of two movie
 * rectangles; clears the intersection when they do not overlap. The return
 * value is incidental (last computed bottom edge). */
/* Rectangles are x, y, width and height. Only movie_merge_dirty_rect_lists calls
 * it, in the original build. */
// FUNCTION: XVT 0x4EFBD0
int movie_compute_rect_union_and_intersection(
	const struct movie_dirty_rect *a, const struct movie_dirty_rect *b,
	struct movie_dirty_rect *union_rect,
	struct movie_dirty_rect *intersection_rect)
{
	int x;
	int width;
	int y;
	int height;

	if (b->x < a->x) {
		union_rect->width = a->width - b->x + a->x;
		union_rect->x = b->x;
		intersection_rect->x = a->x;
		intersection_rect->width = a->width;
	} else {
		intersection_rect->width = a->width - b->x + a->x;
		intersection_rect->x = b->x;
		union_rect->x = a->x;
		union_rect->width = a->width;
	}
	if (b->y < a->y) {
		union_rect->height = a->height - b->y + a->y;
		union_rect->y = b->y;
		intersection_rect->y = a->y;
		intersection_rect->height = a->height;
	} else {
		intersection_rect->height = a->height - b->y + a->y;
		intersection_rect->y = b->y;
		union_rect->y = a->y;
		union_rect->height = a->height;
	}
	width = b->width;
	x = b->x;
	if (a->x + a->width < x + width) {
		union_rect->width = width - union_rect->x + x;
	} else {
		intersection_rect->width = width - intersection_rect->x + x;
	}
	y = b->y;
	height = b->height;
	if (a->y + a->height < y + height) {
		union_rect->height = y - union_rect->y + height;
	} else {
		intersection_rect->height = y - intersection_rect->y + height;
	}
	if (intersection_rect->width <= 0 || intersection_rect->height <= 0) {
		intersection_rect->x = 0;
		intersection_rect->y = 0;
		intersection_rect->width = 0;
		intersection_rect->height = 0;
		return 0;
	}
	return y + height;
}

/* Plays a movie by name. The modern build returns the result of a movie that
 * has finished, when one waits, and otherwise starts the movie through
 * xvt_movie_task_begin, which returns XVT_MOVIE_PENDING (-1), or 2 when it cannot
 * start. The original build looks for movies\NAME.smk, then on the CD drive.
 * With synchronize_multiplayer set outside single player it plays "Flyby1a"
 * instead when neither is there; otherwise it asks for the CD until the file is
 * found and returns 2 on Cancel. Then it plays the movie 640 by 480 through
 * movie_run_smacker_playback, decoding into the offscreen surface in page-flip
 * full screen and the back buffer otherwise, with the network input and sync
 * callbacks in that synchronized case and the single-player input callback
 * otherwise, and returns its result: 0 played, 2 not found, 3 too large, 5 left
 * the game. */
// FUNCTION: XVT 0x4EFCE0
int movie_play(const char *name, int synchronize_multiplayer)
{
#ifdef XVT_MODERN
	int result;
	if (xvt_movie_task_take_result(&result)) {
		return result;
	}
	return xvt_movie_task_begin(name, synchronize_multiplayer);
#else
	enum {
		MOVIE_PATH_CAPACITY = 128,
		MOVIE_NAME_CAPACITY = 256,
		MOVIE_DISPLAY_WIDTH = 640,
		MOVIE_DISPLAY_HEIGHT = 480,
		MOVIE_STATUS_NOT_FOUND = 2,
	};

	struct movie_playback_params playback_params;
	char movie_path[MOVIE_PATH_CAPACITY];
	char movie_name[MOVIE_NAME_CAPACITY];
	xvt_file *probe_stream;

	strcpy(movie_name, name);
	strcpy(movie_path, "movies\\");
	strcat(movie_path, movie_name);
	strcat(movie_path, ".smk");
	probe_stream = FILE_RAW_OPEN(movie_path, g_file_mode_read_binary);
	if (synchronize_multiplayer != 0 &&
	    g_frontend_mission_session_mode !=
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		if (probe_stream == NULL) {
			strcpy(movie_path, "d:\\movies\\");
			movie_path[0] = file_get_cd_drive_letter();
			strcat(movie_path, movie_name);
			strcat(movie_path, ".smk");
			probe_stream = FILE_RAW_OPEN(movie_path,
						     g_file_mode_read_binary);
			if (probe_stream == NULL) {
				strcpy(movie_name, "Flyby1a");
			} else {
				FILE_RAW_CLOSE(probe_stream);
			}
		} else {
			FILE_RAW_CLOSE(probe_stream);
		}
	} else {
		if (probe_stream == NULL) {
			strcpy(movie_path, "d:\\movies\\");
			movie_path[0] = file_get_cd_drive_letter();
			strcat(movie_path, movie_name);
			strcat(movie_path, ".smk");
			for (;;) {
				probe_stream = FILE_RAW_OPEN(
					movie_path, g_file_mode_read_binary);
				if (probe_stream != NULL) {
					break;
				}
				frontend_display_enable_offscreen_restore();
				if (frontend_dialog_show_confirm_dialog(
					    frontend_string_get(
						    FRONTSTR_827_PLEASE_INSERT_BALANCE_OF_POWER_CD),
					    frontend_string_get(
						    FRONTSTR_828_INTO_YOUR_CD_ROM_DRIVE),
					    frontend_string_get(
						    FRONTSTR_829_EMPTY_TRANSLATION_PLACEHOLDER),
					    frontend_string_get(
						    FRONTSTR_523_OKAY),
					    frontend_string_get(
						    FRONTSTR_019_CANCEL)) ==
				    0) {
					frontend_display_disable_offscreen_restore();
					frontend_display_clear_back_buffer();
					return MOVIE_STATUS_NOT_FOUND;
				}
				frontend_display_disable_offscreen_restore();
				frontend_display_clear_back_buffer();
			}
		}
		FILE_RAW_CLOSE(probe_stream);
	}

	playback_params.movie_name = movie_name;
	playback_params.primary_surface = g_front_state.primary_surface;
	playback_params.display_width = MOVIE_DISPLAY_WIDTH;
	playback_params.display_height = MOVIE_DISPLAY_HEIGHT;
	if (g_opt_no_fullscreen == 0 && g_no_page_flip == 0) {
		playback_params.decode_surface =
			g_front_state.offscreen_surface;
	} else {
		playback_params.decode_surface =
			g_front_state.back_buffer_surface;
	}
	playback_params.palette = g_front_state.dd_palette;
	playback_params.direct_sound = g_front_state.frontend_direct_sound;
	playback_params.window = g_front_state.h_wnd;
	if (synchronize_multiplayer != 0 &&
	    g_frontend_mission_session_mode !=
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		playback_params.input_callback =
			(movie_input_callback)movie_multiplayer_input_callback;
		playback_params.progress_callback = (movie_progress_callback)
			movie_multiplayer_sync_callback;
	} else {
		playback_params.input_callback =
			(movie_input_callback)movie_singleplayer_input_callback;
		playback_params.progress_callback = NULL;
	}
	return movie_run_smacker_playback(&playback_params);
#endif
}

/* Input callback for single-player movies. Paint: clears and presents the
 * screen and returns 1, so default painting follows. A character: Backspace,
 * Enter, Esc and Space restore the previous mode, which ends playback; every
 * character writes 0 to *handled_result, which the window procedure returns, and
 * returns 0. A left, right or middle button release ends playback the same way.
 * Other messages return 1. Only the original build uses it. */
// FUNCTION: XVT 0x4F0070
int movie_singleplayer_input_callback(int window, unsigned int event_code,
				      int key_code, int l_param,
				      int callback_context,
				      uint32_t *handled_result)
{
	(void)window;
	(void)l_param;
	(void)callback_context;

	switch (event_code) {
	case 0x0F:
		frontend_display_clear_back_buffer();
		frontend_display_clear_offscreen_surface();
		frontend_display_present_frame();
		frontend_display_clear_back_buffer();
		return 1;
	case 0x102:
		switch (key_code) {
		case 8:
		case 13:
		case 27:
		case 32:
			frontend_display_set_wnd_proc_mode(
				g_movie_previous_wnd_proc_mode);
			break;
		default:
			break;
		}
		*handled_result = 0;
		return 0;
	case 0x202:
	case 0x205:
	case 0x208:
		frontend_display_set_wnd_proc_mode(
			g_movie_previous_wnd_proc_mode);
		*handled_result = 0;
		return 0;
	default:
		return 1;
	}
}

/* Input callback for network movies. Paint: clears and presents the screen.
 * Backspace, Enter, Esc, Space or a button release sends a movie sync packet of
 * 0 (this player waits), writes 0 to *playback_flag and returns 0; this does not
 * end the original build's playback, while the modern build stops its movie
 * when it sees the 0. At the timeout prompt (g_movie_playback_completion_state 2),
 * C on the host sends a packet of 1, which marks every player waiting, and E on
 * a client sets g_movie_skip_requested to -1 and restores the previous mode.
 * Returns 1 when it did not stop playback. The modern build calls it with
 * characters and clicks. */
// FUNCTION: XVT 0x4F0140
int movie_multiplayer_input_callback(int window, unsigned int event_code,
				     int key_code, int l_param,
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

	int stop_playback = 0;
	int packet[2];

	(void)window;
	(void)l_param;
	(void)callback_context;
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
			}
			break;
		case 'E':
		case 'e':
			if (g_movie_playback_completion_state == 2 &&
			    net_is_host() == 0) {
				g_movie_skip_requested = -1;
				frontend_display_set_wnd_proc_mode(
					g_movie_previous_wnd_proc_mode);
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
	if (stop_playback == 1) {
		packet[0] = NET_PACKET_MOVIE_SYNC;
		packet[1] = 0;
		net_send_packet_and_flush(0, packet, sizeof(packet));
		*playback_flag = 0;
		return 0;
	}
	return 1;
}

/* Draws the network sync status in the band above the movie: each player in
 * g_movie_multiplayer_sync_players with FRONTSTR_804_WATCHING or
 * FRONTSTR_805_WAITING after the name, in four columns and two rows, white in
 * font 12, after clearing the band to black. Draws only when the local player's
 * entry is waiting; with no entry for the local player, only when
 * g_mission_briefing_craft_selection_active is 1. Copies the band to the display
 * outside page-flip full screen. A player not among the ready roster entries
 * gets the status appended to an unset name buffer. Only the original build
 * reaches it. */
// FUNCTION: XVT 0x4F02F0
void movie_draw_multiplayer_sync_status(void)
{
	struct RECT rect;
	const char *status_strings[2];
	char text[100];
	unsigned int player_index;
	unsigned int display_width;
	unsigned int horizontal_margin;
	unsigned int cell_width;
	unsigned int ready_player_count;
	unsigned int roster_index;
	int color;
	int local_player_waiting;

	player_index = 0;
	while (player_index < 8) {
		if (net_get_local_player_id() ==
		    g_movie_multiplayer_sync_players[player_index].player_id) {
			break;
		}
		++player_index;
	}
	if (player_index < 8) {
		local_player_waiting =
			g_movie_multiplayer_sync_players[player_index]
				.is_waiting;
	} else {
		local_player_waiting =
			g_mission_briefing_craft_selection_active;
	}
	if (local_player_waiting != 1) {
		return;
	}

	rect.left = 0;
	rect.top = 0;
	rect.right = g_movie_playback_params->display_width - 1;
	rect.bottom = g_movie_y - 1;
	status_strings[0] = frontend_string_get(FRONTSTR_804_WATCHING);
	status_strings[1] = frontend_string_get(FRONTSTR_805_WAITING);
	color = frontend_display_pack_rgb(0xFF, 0xFF, 0xFF);
	display_width = g_movie_playback_params->display_width;
	horizontal_margin = display_width / 20;
	cell_width = (display_width - 2 * horizontal_margin) >> 2;
	ready_player_count = net_count_ready_players();
	g_draw_surface_ptr = frontend_display_lock_back_buffer();
	frontend_draw_rect(&rect, 0, 0, frontend_display_pack_rgb(0, 0, 0), -1);
	for (player_index = 0; player_index < 8; ++player_index) {
		if (g_movie_multiplayer_sync_players[player_index].player_id !=
		    0) {
			rect.left = horizontal_margin +
				    cell_width * (player_index & 3);
			rect.top = ((unsigned int)g_movie_y >> 1) *
				   (player_index >> 2);
			rect.right = rect.left;
			rect.right += cell_width;
			rect.bottom = rect.top + ((unsigned int)g_movie_y >> 1);
			roster_index = 0;
			if (ready_player_count != 0) {
				for (;;) {
					if (g_mp_roster[roster_index]
						    .player_id ==
					    g_movie_multiplayer_sync_players
						    [player_index]
							    .player_id) {
						strcpy(text,
						       g_mp_roster[roster_index]
							       .name);
						break;
					}
					++roster_index;
					if (ready_player_count > roster_index) {
						continue;
					}
					break;
				}
			}
			strcat(text,
			       status_strings[g_movie_multiplayer_sync_players
						      [player_index]
							      .is_waiting]);
			frontend_text_draw_centered(12, text, &rect, color);
		}
	}
	frontend_display_unlock_back_buffer();
	if (g_opt_no_fullscreen != 0 || g_no_page_flip != 0) {
		movie_blit_rect_to_display(
			0, 0, g_movie_playback_params->display_width,
			g_movie_y);
	}
}

/* Runs the wait after the local movie ends in a network game. On its first call
 * (deadline 0) it sends a movie sync packet of 0, sets the deadline 5000 ms
 * ahead for the host or 20000 for a client, and clears the movie area to black
 * on both buffers in page-flip full screen, else on the display. Later calls
 * return until the deadline has passed (deadline - now, unsigned, above the
 * timeout); then it sets g_movie_playback_completion_state to 2 and draws,
 * centered in the bottom margin, the host's prompt to press C to continue or a
 * client's to press E to leave. Only the original build reaches it. */
// FUNCTION: XVT 0x4F0510
void movie_update_multiplayer_sync_timeout(void)
{
	enum {
		HOST_TIMEOUT_MS = 5000,
		CLIENT_TIMEOUT_MS = 20000,
		TIMEOUT_COMPLETION_STATE = 2,
		PROMPT_FONT_SIZE = 12,
		RGB_CHANNEL_MAX = 0xFF
	};

	unsigned int timeout_ms;
	struct RECT rect;
	int packet[2];

	timeout_ms = net_is_host() != 0 ? HOST_TIMEOUT_MS : CLIENT_TIMEOUT_MS;
	if (g_movie_multiplayer_sync_deadline_ms == 0) {
		packet[0] = NET_PACKET_MOVIE_SYNC;
		packet[1] = 0;
		net_send_packet_and_flush(0, packet, sizeof(packet));
		g_movie_multiplayer_sync_deadline_ms =
			timeout_ms + GetTickCount();
		if (g_movie_multiplayer_sync_deadline_ms == 0) {
			++g_movie_multiplayer_sync_deadline_ms;
		}
		rect.left = g_movie_x;
		rect.right = g_movie_playback_params->display_width -
			     (int)g_movie_right_margin - 1;
		rect.top = g_movie_y;
		rect.bottom = g_movie_playback_params->display_height -
			      (int)g_movie_bottom_margin - 1;
		g_draw_surface_ptr = frontend_display_lock_back_buffer();
		frontend_draw_rect(&rect, 0, 0,
				   frontend_display_pack_rgb(0, 0, 0), -1);
		frontend_display_unlock_back_buffer();
		if (g_opt_no_fullscreen == 0 && g_no_page_flip == 0) {
			frontend_display_present_frame();
			g_draw_surface_ptr =
				frontend_display_lock_back_buffer();
			frontend_draw_rect(&rect, 0, 0,
					   frontend_display_pack_rgb(0, 0, 0),
					   -1);
			frontend_display_unlock_back_buffer();
			return;
		}
		movie_blit_rect_to_display(
			g_movie_x, g_movie_y,
			g_movie_playback_params->display_width -
				(int)g_movie_right_margin,
			g_movie_playback_params->display_height -
				(int)g_movie_bottom_margin);
		return;
	}

	{
		unsigned int deadline_ms;
		const char *message;

		deadline_ms = g_movie_multiplayer_sync_deadline_ms;
		if (deadline_ms - GetTickCount() <= timeout_ms) {
			return;
		}
		g_movie_playback_completion_state = TIMEOUT_COMPLETION_STATE;
		g_draw_surface_ptr = frontend_display_lock_back_buffer();
		rect.left = 0;
		rect.top = g_movie_playback_params->display_height -
			   (int)g_movie_bottom_margin;
		rect.right = g_movie_playback_params->display_width - 1;
		rect.bottom = g_movie_playback_params->display_height - 1;
		frontend_draw_rect(&rect, 0, 0,
				   frontend_display_pack_rgb(0, 0, 0), -1);
		if (net_is_host() != 0) {
			message = frontend_string_get(
				FRONTSTR_807_STILL_WAITING_FOR_OTHERS_HIT_C_TO_CONTINUE_THE_GAME);
		} else {
			message = frontend_string_get(
				FRONTSTR_806_STILL_WAITING_FOR_OTHERS_HIT_E_TO_EXIT_THE_GAME);
		}
		frontend_text_draw_centered(
			PROMPT_FONT_SIZE, message, &rect,
			frontend_display_pack_rgb(RGB_CHANNEL_MAX,
						  RGB_CHANNEL_MAX,
						  RGB_CHANNEL_MAX));
		frontend_display_unlock_back_buffer();
		if (g_opt_no_fullscreen == 0 && g_no_page_flip == 0) {
			frontend_display_present_frame();
			g_draw_surface_ptr =
				frontend_display_lock_back_buffer();
			frontend_draw_rect(&rect, 0, 0,
					   frontend_display_pack_rgb(0, 0, 0),
					   -1);
			frontend_text_draw_centered(
				PROMPT_FONT_SIZE, message, &rect,
				frontend_display_pack_rgb(RGB_CHANNEL_MAX,
							  RGB_CHANNEL_MAX,
							  RGB_CHANNEL_MAX));
			frontend_display_unlock_back_buffer();
			frontend_display_present_frame();
			return;
		}
		movie_blit_rect_to_display(
			0,
			g_movie_playback_params->display_height -
				(int)g_movie_bottom_margin,
			g_movie_playback_params->display_width,
			(int)g_movie_bottom_margin);
	}
}

/* Progress callback for network movies; only current_frame is used. At frame 0
 * it fills g_movie_multiplayer_sync_players from the ready roster, all watching,
 * and sets the deadline to 0. Each call processes network packets and returns
 * 1, ending playback, once no player is watching; otherwise it draws the sync
 * status, runs the timeout while g_movie_playback_completion_state is 1, and
 * returns 0. Only the original build uses it. */
// FUNCTION: XVT 0x4F07D0
int movie_multiplayer_sync_callback(int current_frame)
{
	enum { MAX_MULTIPLAYER_PLAYERS = 8 };

	unsigned int player_index;
	unsigned int ready_player_count;
	int watching_player_count;

	if (current_frame == 0) {
		ready_player_count = net_count_ready_players();
		for (player_index = 0; player_index < MAX_MULTIPLAYER_PLAYERS;
		     ++player_index) {
			if (player_index < ready_player_count) {
				g_movie_multiplayer_sync_players[player_index]
					.player_id =
					g_mp_roster[player_index].player_id;
				g_movie_multiplayer_sync_players[player_index]
					.is_waiting = 0;
			} else {
				g_movie_multiplayer_sync_players[player_index]
					.player_id = 0;
			}
		}
		g_movie_multiplayer_sync_deadline_ms = 0;
	}

	frontend_net_process_network_packets();
	watching_player_count = 0;
	for (player_index = 0; player_index < MAX_MULTIPLAYER_PLAYERS;
	     ++player_index) {
		if (g_movie_multiplayer_sync_players[player_index].player_id !=
			    0 &&
		    g_movie_multiplayer_sync_players[player_index].is_waiting ==
			    0) {
			++watching_player_count;
		}
	}
	if (watching_player_count == 0) {
		return 1;
	}

	movie_draw_multiplayer_sync_status();
	if (g_movie_playback_completion_state == 1) {
		movie_update_multiplayer_sync_timeout();
	}
	return 0;
}

/* Reads the next subtitle record from g_movie_subtitle_file: a frame number line,
 * then three text lines, each without its newline; a line starting with '.' or
 * missing at the end of the file reads as empty. Returns the frame number; 0,
 * reading nothing, when no file is open; 0xFFFF, with line1 emptied, when no
 * number can be read. Reads each line with a 256-byte limit. Both builds call
 * it. */
// FUNCTION: XVT 0x4F0900
unsigned int movie_read_subtitle_cue(char *line1, char *line2, char *line3)
{
	unsigned int frame_number;
	int scan_result;

	if (g_movie_subtitle_file == NULL) {
		return 0;
	}
	scan_result = FILE_SCANF(g_movie_subtitle_file, "%u\n", &frame_number);
	if (scan_result == 0 || scan_result == EOF) {
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

	return frame_number;
}

/* Draws the subtitles for a movie frame, when a subtitle file is open. Frame 0
 * sets both cue frames to 0. When frame_number reaches g_movie_next_subtitle_frame,
 * that frame becomes the active one and the next record is read, so a record's
 * lines go with the frame the record before it named (frame 0 for the first).
 * On the active frame and the one after, it clears the bottom margin to black
 * and draws the three lines centered, white, in font 12, each a third of the
 * margin tall; in windowed play it also copies the margin to the display. Only
 * the original build reaches it. */
// FUNCTION: XVT 0x4F0A50
void movie_draw_subtitles(unsigned int frame_number)
{
	struct RECT rect;
	unsigned int line_height;
	int text_color;

	if (g_movie_subtitle_file != NULL) {
		if (frame_number == 0) {
			g_movie_next_subtitle_frame = 0;
			g_movie_active_subtitle_frame = 0;
		}
		if (g_movie_next_subtitle_frame == frame_number) {
			g_movie_active_subtitle_frame =
				g_movie_next_subtitle_frame;
			g_movie_next_subtitle_frame = movie_read_subtitle_cue(
				g_frontend_scratch_buffer,
				g_movie_subtitle_line2, g_movie_subtitle_line3);
		}
		if (g_movie_active_subtitle_frame == frame_number ||
		    g_movie_active_subtitle_frame - frame_number ==
			    (unsigned int)-1) {
			text_color =
				frontend_display_pack_rgb(0xFF, 0xFF, 0xFF);
			line_height = g_movie_bottom_margin / 3;
			g_draw_surface_ptr =
				frontend_display_lock_back_buffer();
			rect.left = 0;
			rect.top = g_movie_playback_params->display_height -
				   g_movie_bottom_margin;
			rect.right = g_movie_playback_params->display_width - 1;
			rect.bottom =
				g_movie_playback_params->display_height - 1;
			frontend_draw_rect(&rect, 0, 0,
					   frontend_display_pack_rgb(0, 0, 0),
					   -1);
			rect.bottom = rect.top + line_height;
			frontend_text_draw_centered(12,
						    g_frontend_scratch_buffer,
						    &rect, text_color);
			rect.top = rect.bottom;
			rect.bottom += line_height;
			frontend_text_draw_centered(12, g_movie_subtitle_line2,
						    &rect, text_color);
			rect.top = rect.bottom;
			rect.bottom += line_height;
			frontend_text_draw_centered(12, g_movie_subtitle_line3,
						    &rect, text_color);
			frontend_display_unlock_back_buffer();
			if (g_opt_no_fullscreen != 0) {
				movie_blit_rect_to_display(
					0,
					g_movie_playback_params
							->display_height -
						g_movie_bottom_margin,
					g_movie_playback_params->display_width,
					g_movie_bottom_margin);
			}
		}
	}
}
