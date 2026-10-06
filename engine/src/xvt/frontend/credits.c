#include "xvt/frontend/credits.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xvt/assets/file.h"
#include "xvt/audio/cd_audio.h"
#include "xvt/audio/frontend_sound.h"
#include "xvt/frontend/front_image.h"
#include "xvt/frontend/frontend_cursor.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/frontend_text.h"

/* Per text buffer (0 and 1), the X position of the logo drawn with that
 * buffer's page. credits_parse_next_page sets it from the page header's seventh
 * number; credits_update_screen sets both to 0 on its first frame. */
// GLOBAL: XVT 0x6697A8
int g_credits_logo_x[2] = {0};
/* Per text buffer, the logo X last drawn into the offscreen background. Only
 * credits_update_screen writes it: it sets both to 0 on its first frame and
 * copies g_credits_logo_x in whenever one of the six logo values changed, which
 * redraws the background. */
// GLOBAL: XVT 0x6697A0
int g_credits_prev_logo_x[2] = {0};
/* Per text buffer, the Y position of the logo drawn with that buffer's page;
 * the page header's eighth number. Written like g_credits_logo_x. */
// GLOBAL: XVT 0x6697B0
int g_credits_logo_y[2] = {0};
/* 1 when the page last read from credits.txt ended at a line starting with
 * '*', so another page follows; 0 when the file ended. Written by
 * credits_parse_next_page through the pointer credits_update_screen passes, and
 * set to 0 by credits_update_screen on its first frame. */
// GLOBAL: XVT 0x6697B8
int g_credits_has_more_pages = 0;
/* Pages shown before the current one: 0 on the first page. Only
 * credits_update_screen writes it: 0 on its first frame, raised by one at each
 * new page. It picks the hidden photo that Shift, Alt and F12 held together
 * draw. */
// GLOBAL: XVT 0x6697BC
int g_credits_page_index = 0;
/* Per text buffer, the logo Y last drawn into the offscreen background.
 * Written like g_credits_prev_logo_x. */
// GLOBAL: XVT 0x6697C0
int g_credits_prev_logo_y[2] = {0};
/* Color given to each credits line as it is read: the low 16 bits of
 * frontend_display_pack_rgb for the last line starting with '~'. Set by
 * credits_parse_text_line; credits_update_screen sets 0xFFFF on its first
 * frame. */
// GLOBAL: XVT 0x6697C8
uint16_t g_credits_current_text_color = 0;
/* credits.txt, open while the credits screen runs. credits_update_screen opens
 * it on its first frame, closing any copy still open; it is NULL when that open
 * fails. frontend_bootstrap_exit_credits_and_load_frontend closes it and sets
 * NULL, and so does xvt_frontend_task_shutdown. */
// GLOBAL: XVT 0x6697CC
xvt_file *g_frontend_credits_file = NULL;
/* Per text buffer, the logo drawn with that buffer's page: 1 the "totallylogo"
 * image, 2 the "leclogo" image, any other value none. The page header's sixth
 * number. Written like g_credits_logo_x. */
// GLOBAL: XVT 0x6697D0
int g_credits_logo_id[2] = {0};
/* Per text buffer, the logo id last drawn into the offscreen background.
 * Written like g_credits_prev_logo_x. */
// GLOBAL: XVT 0x6697D8
int g_credits_prev_logo_id[2] = {0};
/* Frontend frame count at which the current page's time is up. The first page
 * stores its duration here directly, since the screen's frames count from 0;
 * credits_update_screen adds each later page's duration, the page header's fifth
 * number, when it reads that page. */
// GLOBAL: XVT 0x6697E0
int g_credits_page_end_frame = 0;
/* Frames the newest page's text takes to fade in: the page header's fourth
 * number, passed to frontend_text_start_text_fade_in. The first page ignores it
 * and fades over 200 frames. Written by credits_parse_next_page through the
 * pointer credits_update_screen passes. */
// GLOBAL: XVT 0x6697E4
int g_credits_text_fade_frames = 0;
/* Per text buffer, the Y position of the page's first text line; later lines
 * sit 19 pixels apart. The page header's second number, set by
 * credits_parse_next_page. credits_update_screen sets only entry 1 to 0 on its
 * first frame. */
// GLOBAL: XVT 0x6697E8
int g_credits_text_y[2] = {0};
/* Per text buffer, the X position of the page's text lines; the page header's
 * first number, set by credits_parse_next_page. credits_update_screen sets only
 * entry 0 to 0 on its first frame. */
// GLOBAL: XVT 0x6697F0
int g_credits_text_x[2] = {0};
/* Text buffer, 0 or 1, the newest page was read into: the page header's third
 * number minus 1, with any result above 1 made 1. Written by
 * credits_parse_next_page through the pointer credits_update_screen passes. That
 * buffer's text fades in; the other buffer's is drawn without the fade. */
// GLOBAL: XVT 0x6697F8
unsigned int g_credits_buffer_idx = 0;
/* Two pages of credits text, up to 32 lines of 255 characters each, one page
 * per buffer. credits_parse_next_page empties a buffer before it fills it, and
 * credits_parse_text_line copies each line in without the color prefix. */
// GLOBAL: XVT 0x669800
char g_credits_text_lines[2][32][256] = {0};
/* Color of each line in g_credits_text_lines, set by credits_parse_text_line from
 * g_credits_current_text_color. */
// GLOBAL: XVT 0x66D800
uint16_t g_credits_text_colors[2][32] = {0};
/* 1 once the credits are ending: after the last page's time is up, or on a
 * mouse click, Esc, Enter or Space. Only credits_update_screen writes it,
 * setting 0 on its first frame; its next frame after the 1 switches to the
 * concourse once the CD fade is over. xvt_frontend_task_update also reads it,
 * to keep the last frame shown. */
// GLOBAL: XVT 0x52D0C8
int g_credits_exit_pending = 0;

/* Besides loading the credits images, font and sound list, this sets the
 * display options for the screen, hides the cursor and starts CD track 4
 * playing on a loop. */
/* Also loads font size 15, turns on refilling the back buffer from the
 * offscreen surface after each present, sets the CD aux volume to 0x8000 and
 * plays track 4 from its start. Returns 0 on every path and checks no load or
 * play result. Its one caller is frontend_bootstrap_exit_intro_and_load_credits. */
// FUNCTION: XVT 0x4FB4F0
int credits_load_screen_resources(void)
{
	frontend_display_disable_escape_close();
	frontend_display_set_surface_clear_color(0);
	frontend_cursor_hide();
	frontend_display_disable_clear_after_present();
	frontend_display_enable_offscreen_restore();
	frontend_text_load_font(15);
	frontend_sound_load_list("sfx\\sfx.lst");
	front_image_register_resource_default("frontres\\credits.bmp",
					      "background");
	front_image_register_resource("frontres\\leclogo.bmp", "leclogo", 0, 0);
	front_image_register_resource("frontres\\totallyg.bmp", "totallylogo",
				      0, 0);
	front_image_register_resource_default("frontres\\comp01.bmp", "comp01");
	front_image_register_resource_default("frontres\\test.bmp", "testers");
	front_image_register_resource_default("frontres\\jbrs.bmp", "lakota");
	front_image_register_resource_default("frontres\\arts.bmp", "artists");
	cd_audio_enable_loop_current_track();
	cd_audio_initialize();
	cd_audio_set_aux_volume(0x8000u);
	cd_audio_play_track_from_time(4, 0, 0);
	return 0;
}

/* Reads the next page of credits.txt into one of the two text buffers. A page
 * is a header line of ten unsigned numbers (text X, text Y, buffer 1 or 2 with
 * any other number counting as 2, fade frames, duration frames, logo id, logo
 * X, logo Y and two that are read and ignored), then up to 32 text lines; lines
 * starting with "//" are skipped and do not count. The header sets that
 * buffer's g_credits_text_x, g_credits_text_y, g_credits_logo_id, g_credits_logo_x and
 * g_credits_logo_y, and the buffer's 32 lines are emptied first. Each text line
 * goes through credits_parse_text_line, without its newline. A line starting with
 * '*' ends the page and sets *out_has_more_pages to 1; after 32 lines it skips
 * ahead to that line or the end of the file. Returns 0 with *out_has_more_pages at
 * 0 when g_frontend_credits_file is NULL, else 1, also at the end of the file,
 * where *out_has_more_pages stays 0. Does not check how many header numbers were
 * read, and lowers *out_buffer_idx by one even when none were. */
// FUNCTION: XVT 0x4FBAF0
int credits_parse_next_page(unsigned int *out_buffer_idx,
			    int *out_has_more_pages,
			    int *out_page_duration_frames,
			    int *out_text_fade_frames)
{
	*out_has_more_pages = 0;
	if (g_frontend_credits_file == NULL) {
		return 0;
	}
	unsigned int logo_id;
	unsigned int logo_x;
	unsigned int logo_y;
	unsigned int text_x;
	unsigned int text_y;
	unsigned int unused_a;
	unsigned int unused_b;
	FILE_SCANF(g_frontend_credits_file, "%u %u %u %u %u %u %u %u %u %u\n",
		   &text_x, &text_y, out_buffer_idx, out_text_fade_frames,
		   out_page_duration_frames, &logo_id, &logo_x, &logo_y,
		   &unused_a, &unused_b);
	(*out_buffer_idx)--;
	if (*out_buffer_idx > 1) {
		*out_buffer_idx = 1;
	}
	g_credits_logo_id[*out_buffer_idx] = logo_id;
	g_credits_logo_x[*out_buffer_idx] = logo_x;
	g_credits_logo_y[*out_buffer_idx] = logo_y;
	g_credits_text_x[*out_buffer_idx] = text_x;
	g_credits_text_y[*out_buffer_idx] = text_y;
	for (int clear_index = 0; clear_index < 32; clear_index++) {
		g_credits_text_lines[*out_buffer_idx][clear_index][0] = '\0';
	}

	int line_index = 0;
	char line[256];
	do {
		if (FILE_GETS(line, sizeof(line), g_frontend_credits_file) ==
		    NULL) {
			return 1;
		}
		if (line[0] != '/' || line[1] != '/') {
			size_t line_length = strlen(line);
			if (line[line_length - 1] == '\n') {
				line[line_length - 1] = '\0';
			}
			if (line[0] == '*') {
				*out_has_more_pages = 1;
				return 1;
			}
			credits_parse_text_line(line, *out_buffer_idx,
						line_index);
			line_index++;
		}
	} while (line_index < 32);

	while (FILE_GETS(line, sizeof(line), g_frontend_credits_file) != NULL) {
		if (line[0] == '*') {
			*out_has_more_pages = 1;
			return 1;
		}
	}
	return 1;
}

/* Stores one credits line in g_credits_text_lines[buffer_idx][line_idx] and its
 * color in g_credits_text_colors. A line starting with '~' first sets
 * g_credits_current_text_color from three decimal numbers after it, red, green
 * and blue, each ended by a space, through frontend_display_pack_rgb; the text
 * after them is stored. Other lines are stored whole, in the color still in
 * g_credits_current_text_color. Returns 1. Checks neither index nor the length
 * of the text it copies. */
// FUNCTION: XVT 0x4FBCD0
int credits_parse_text_line(const char *line, unsigned int buffer_idx,
			    unsigned int line_idx)
{
	const char *text = line;
	int remaining = strlen(line);
	if (*text == '~') {
		++text;
		int token_length = 0;
		char token[256];
		int char_index;
		if (remaining > 0) {
			for (char_index = 0; remaining > char_index;
			     ++char_index) {
				char c = *text;
				if (c == ' ' || c == '\0') {
					++text;
					--remaining;
					token[token_length] = '\0';
					break;
				}
				token[token_length++] = c;
				++text;
				--remaining;
			}
		}
		int red = atoi(token);

		token_length = 0;
		if (remaining > 0) {
			for (char_index = 0; remaining > char_index;
			     ++char_index) {
				char c = *text;
				if (c == ' ' || c == '\0') {
					++text;
					--remaining;
					token[token_length] = '\0';
					break;
				}
				token[token_length++] = c;
				++text;
				--remaining;
			}
		}
		int green = atoi(token);

		token_length = 0;
		if (remaining > 0) {
			for (char_index = 0; remaining > char_index;
			     ++char_index) {
				char c = *text;
				if (c == ' ' || c == '\0') {
					token[token_length] = '\0';
					++text;
					break;
				}
				token[token_length++] = c;
				++text;
				--remaining;
			}
		}
		g_credits_current_text_color =
			frontend_display_pack_rgb(red, green, atoi(token));
	}
	g_credits_text_colors[buffer_idx][line_idx] =
		g_credits_current_text_color;
	strcpy(g_credits_text_lines[buffer_idx][line_idx], text);
	return 1;
}
