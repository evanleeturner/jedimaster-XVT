/* Draws a mission's briefing with the engine's own mission setup screen, or
 * replays lists of the briefing map's draw records through the engine's own
 * drawing functions, into 16-bit screens held in memory, and writes their
 * pixels, so that another painter of the same briefings can be checked
 * against the engine pixel for pixel. Build with -DXVT_BUILD_TOOLS=ON on
 * Linux; it links the engine library and takes the sound, CD music, pilot
 * file and settings file calls with the GNU linker's --wrap, so nothing is
 * heard, no pilot is read, and the game's own defaults stand (config_load's,
 * with no config.yaml applied over them).
 *
 * Usage: briefing_pixels ROOT FILE DIRECTORY TEAM OUT frames F...
 *        briefing_pixels ROOT FILE DIRECTORY TEAM OUT pieces LIST...
 *
 * ROOT is the game's install and FILE any mission file. DIRECTORY is the
 * mission directory the screen takes FILE from: train, melee, tourn, combat
 * or battle; it picks the screen's background. TEAM, 0 to 9, is the team
 * whose briefing shows. OUT is the folder the pixels go to.
 *
 * The tool binds a folder of links as briefing_dump does, with FILE linked
 * into DIRECTORY's folder, and stands in for DirectDraw with two 640 by 480
 * screens in memory, the back buffer and the offscreen surface, at 16 bits
 * per pixel in the 565 layout, as the modern runtime sets the menus
 * (frontend_task.c). The stand-in surfaces answer Lock, Unlock, Restore,
 * GetSurfaceDesc and BltFast without a color key; any other surface call
 * prints its name and exits 1, and the stand-in DirectDraw object answers
 * nothing. The tool then loads the menus as the game does
 * (frontend_load_resources, with sound, CD music, the last pilot and
 * config.yaml left out), sets the six text color codes as the display's
 * start does (frontend_display_init_main_window), points the pilot at the
 * mission for TEAM in a solo game, and runs the flight assignment screen
 * (mission_setup_flight_assignment_update) through the frontend task's own
 * frame (xvt_frontend_task_run_frame), each frame followed by the refill of
 * the back buffer from the offscreen surface that the present makes
 * (frontend_display_restore_back_buffer). The cursor is hidden.
 *
 * frames runs the screen up to the largest F. After frame F, counted from 1
 * (the screen's frame_counter is F - 1), it writes OUT/frame-F.raw: the back
 * buffer as the present shows it, 480 rows top to bottom of 640 pixels, each
 * two bytes, low byte first; after frame 1 also OUT/offscreen.raw, the
 * offscreen surface the same way: what the screen laid under every frame
 * (its background and the art over it). Frame 1 draws over a back buffer
 * no screen filled yet; in the game it holds the screen before. When the
 * screen leaves or hands over to another, the tool stops there with exit
 * 1: a solo melee whose teams have one player flight group each never
 * shows it.
 *
 * pieces runs the screen one frame, so that it sets its colors, and stops
 * the text fade it starts. Then, for each LIST, it clears the back buffer to
 * 0, sets the clip to the panel and replays LIST's records, one a line in
 * the format briefing_dump prints them (blank lines skipped), moved from
 * panel coordinates to the setup screen's panel at (84, 96), each through
 * the function that drew it:
 *
 *   clip L T R B          frontend_display_set_screen_clip_rect640x480
 *   vline X T B COLOR     frontend_draw_vertical_line_clipped
 *   hline Y L R COLOR     frontend_draw_horizontal_line_clipped
 *   icon NAME I X Y       front_image_draw_sprite_rect_transparent, box I of
 *                         g_map_icon_rects
 *   tint NAME I X Y COLOR front_image_draw_sprite_rect_tinted, the same box
 *   fill, edge L T R B COLOR
 *                         frontend_draw_rect, filled and not
 *   outline L T R B COLOR frontend_draw_rect_outline
 *   text X Y COLOR "TEXT" frontend_text_draw in font 10
 *
 * COLOR is read back to the value the screen set: "shade N" entry N of the
 * 40 text shades (g_text_shade_ramps read as one strip), "code1" to "code5"
 * the text color codes, "yellow" g_color_yellow, "white" 0xFFFF, "major"
 * and "minor" the grid's two reds (frontend_display_pack_rgb of 0x96, 0, 0
 * and 0x50, 0, 0), "xHEX" the value itself. TEXT takes briefing_dump's
 * escapes back. It then writes OUT/NAME.raw as frames does, NAME being
 * LIST's file name less its last extension.
 *
 * Each file written prints "wrote PATH"; frames prints the state line
 * before it, as briefing_dump does. The first lines name the screen:
 * "directory NAME", "team T", "host_cd C" (C picks the screen's active
 * button art). The engine's log runs at DEBUG and goes to stderr, as SDL
 * writes it. Exit status: 0 when every file was written; 1 when the folder
 * of links, a record, a stand-in call or a write fails, or the screen
 * leaves; 2 for bad arguments. */
#define _DEFAULT_SOURCE

#include <SDL3/SDL_log.h>
#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "asset_dump.h"
#include "briefing_common.h"
#include "xvt/frontend/briefing_map.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/front_image.h"
#include "xvt/frontend/frontend.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/frontend_draw.h"
#include "xvt/frontend/frontend_mission.h"
#include "xvt/frontend/frontend_mission_list.h"
#include "xvt/frontend/frontend_screen.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt/frontend/frontend_text.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt_runtime/log/log.h"
#include "xvt_runtime/runtime/frontend_task.h"

enum {
	SCREEN_WIDTH = 640,
	SCREEN_HEIGHT = 480,
	PANEL_LEFT = 84,
	PANEL_TOP = 96,
	PANEL_RIGHT = 443,
	PANEL_BOTTOM = 335,
	FONT_SIZE = 10,
	SHADE_COUNT = 40,
	LINE_CAPACITY = 4096,
	MAX_FRAMES = 100000,
};

/* A stand-in DirectDraw surface: the interface first, so the engine's
 * pointer to it is a pointer to this. */
struct stand_in_surface {
	IDirectDrawSurface surface;
	const char *name;
	uint16_t *pixels;
};

static uint16_t g_back_pixels[SCREEN_HEIGHT * SCREEN_WIDTH];
static uint16_t g_offscreen_pixels[SCREEN_HEIGHT * SCREEN_WIDTH];

/* Fills desc as Lock and GetSurfaceDesc report a 640 by 480 surface. */
static void describe(const struct stand_in_surface *self, DDSURFACEDESC *desc)
{
	desc->dwWidth = SCREEN_WIDTH;
	desc->dwHeight = SCREEN_HEIGHT;
	desc->lPitch = SCREEN_WIDTH * 2;
	desc->lpSurface = self->pixels;
}

static HRESULT AERON_DXAPI lock(IDirectDrawSurface *surface, void *rect,
				DDSURFACEDESC *desc, uint32_t flags,
				void *event)
{
	(void)rect;
	(void)flags;
	(void)event;
	describe((const struct stand_in_surface *)surface, desc);
	return DX_DD_OK;
}

static HRESULT AERON_DXAPI unlock(IDirectDrawSurface *surface, void *rect)
{
	(void)surface;
	(void)rect;
	return DX_DD_OK;
}

static HRESULT AERON_DXAPI restore(IDirectDrawSurface *surface)
{
	(void)surface;
	return DX_DD_OK;
}

static HRESULT AERON_DXAPI get_surface_desc(IDirectDrawSurface *surface,
					    DDSURFACEDESC *desc)
{
	describe((const struct stand_in_surface *)surface, desc);
	return DX_DD_OK;
}

/* Copies source_rect (inclusive left and top, exclusive right and bottom,
 * as DirectDraw takes it; NULL for the whole surface) of source to (x, y). */
static HRESULT AERON_DXAPI blt_fast(IDirectDrawSurface *target, uint32_t x,
				    uint32_t y, IDirectDrawSurface *source,
				    void *source_rect, uint32_t flags)
{
	const struct stand_in_surface *to =
		(const struct stand_in_surface *)target;
	const struct stand_in_surface *from =
		(const struct stand_in_surface *)source;
	struct RECT whole = {0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};
	const struct RECT *rect =
		source_rect != NULL ? (const struct RECT *)source_rect : &whole;
	if (flags & DDBLTFAST_SRCCOLORKEY) {
		fprintf(stderr, "briefing_pixels: BltFast with a color key\n");
		exit(1);
	}
	for (int row = rect->top; row < rect->bottom; ++row) {
		int to_row = (int)y + row - rect->top;
		if (row < 0 || row >= SCREEN_HEIGHT || to_row < 0 ||
		    to_row >= SCREEN_HEIGHT || rect->left < 0 ||
		    rect->right > SCREEN_WIDTH ||
		    (int)x + rect->right - rect->left > SCREEN_WIDTH) {
			fprintf(stderr,
				"briefing_pixels: BltFast outside the screen\n");
			exit(1);
		}
		memcpy(&to->pixels[to_row * SCREEN_WIDTH + (int)x],
		       &from->pixels[row * SCREEN_WIDTH + rect->left],
		       (size_t)(rect->right - rect->left) * 2);
	}
	return DX_DD_OK;
}

static uint32_t AERON_DXAPI refuse_release(IDirectDrawSurface *surface)
{
	fprintf(stderr, "briefing_pixels: Release on %s\n",
		((const struct stand_in_surface *)surface)->name);
	exit(1);
}

static HRESULT AERON_DXAPI refuse_flip(IDirectDrawSurface *surface,
				       IDirectDrawSurface *other,
				       uint32_t flags)
{
	(void)other;
	(void)flags;
	fprintf(stderr, "briefing_pixels: Flip on %s\n",
		((const struct stand_in_surface *)surface)->name);
	exit(1);
}

static HRESULT AERON_DXAPI refuse_blt(IDirectDrawSurface *surface, void *rect,
				      IDirectDrawSurface *source,
				      void *source_rect, uint32_t flags,
				      DDBLTFX *effects)
{
	(void)rect;
	(void)source;
	(void)source_rect;
	(void)flags;
	(void)effects;
	fprintf(stderr, "briefing_pixels: Blt on %s\n",
		((const struct stand_in_surface *)surface)->name);
	exit(1);
}

static HRESULT AERON_DXAPI refuse_set_palette(IDirectDrawSurface *surface,
					      IDirectDrawPalette *palette)
{
	(void)palette;
	fprintf(stderr, "briefing_pixels: SetPalette on %s\n",
		((const struct stand_in_surface *)surface)->name);
	exit(1);
}

/* The other entries stay NULL: the frontend calls none of them. */
static const IDirectDrawSurfaceVtbl STAND_IN_SURFACE = {
	.Release = refuse_release,
	.Blt = refuse_blt,
	.BltFast = blt_fast,
	.Flip = refuse_flip,
	.GetSurfaceDesc = get_surface_desc,
	.Lock = lock,
	.Restore = restore,
	.SetPalette = refuse_set_palette,
	.Unlock = unlock,
};

static const IDirectDrawVtbl STAND_IN_DIRECT_DRAW = {0};
static IDirectDraw g_direct_draw = {&STAND_IN_DIRECT_DRAW};
static struct stand_in_surface g_back = {
	{&STAND_IN_SURFACE}, "the back buffer", g_back_pixels};
static struct stand_in_surface g_offscreen = {
	{&STAND_IN_SURFACE}, "the offscreen surface", g_offscreen_pixels};

/* Sound, CD music, the pilot files and config.yaml: the tool needs none
 * of them. */
int __wrap_frontend_sound_load_list(const char *file_name);
int __wrap_frontend_sound_load_list(const char *file_name)
{
	(void)file_name;
	return 1;
}

int __wrap_frontend_sound_play_ui_sound(const char *sound_name,
					int allow_restart_existing, int loop,
					int priority, int volume0_to127,
					int pan0_to127);
int __wrap_frontend_sound_play_ui_sound(const char *sound_name,
					int allow_restart_existing, int loop,
					int priority, int volume0_to127,
					int pan0_to127)
{
	(void)sound_name;
	(void)allow_restart_existing;
	(void)loop;
	(void)priority;
	(void)volume0_to127;
	(void)pan0_to127;
	return 0;
}

int __wrap_cd_audio_initialize(void);
int __wrap_cd_audio_initialize(void) { return 1; }
int __wrap_cd_audio_enable_loop_current_track(void);
int __wrap_cd_audio_enable_loop_current_track(void) { return 1; }
int __wrap_cd_audio_set_aux_volume(unsigned int volume0_to65535);
int __wrap_cd_audio_set_aux_volume(unsigned int volume0_to65535)
{
	(void)volume0_to65535;
	return 1;
}
int __wrap_cd_audio_play_track_from_time(int track_number,
					 uint16_t start_minute,
					 uint8_t start_second);
int __wrap_cd_audio_play_track_from_time(int track_number,
					 uint16_t start_minute,
					 uint8_t start_second)
{
	(void)track_number;
	(void)start_minute;
	(void)start_second;
	return 1;
}
int __wrap_cd_audio_stop_current_track(void);
int __wrap_cd_audio_stop_current_track(void) { return 1; }
int __wrap_pilot_find_and_load_by_name(const char *pilot_name);
int __wrap_pilot_find_and_load_by_name(const char *pilot_name)
{
	(void)pilot_name;
	return 0;
}
int __wrap_xvt_config_apply(struct game_config *game, char *error,
			    size_t capacity);
int __wrap_xvt_config_apply(struct game_config *game, char *error,
			    size_t capacity)
{
	(void)game;
	(void)error;
	(void)capacity;
	return 1;
}
int __wrap_pilot_parse_command_line(const char *cmd_line);
int __wrap_pilot_parse_command_line(const char *cmd_line)
{
	(void)cmd_line;
	return 0;
}

/* The index of name in g_mission_directory_names, below campaigns; -1 when
 * it is none of them. */
static int directory_of(const char *name)
{
	for (int index = MISSION_DIRECTORY_TRAINING_EXERCISES;
	     index < MISSION_DIRECTORY_CAMPAIGNS; ++index) {
		if (strcasecmp(name, g_mission_directory_names[index]) == 0) {
			return index;
		}
	}
	return -1;
}

/* Writes pixels, a 640 by 480 screen, to path, as the header comment says. */
static int write_screen(const char *path, const uint16_t *pixels)
{
	FILE *out = fopen(path, "wb");
	if (out == NULL) {
		fprintf(stderr, "briefing_pixels: cannot write %s\n", path);
		return 0;
	}
	unsigned char row[SCREEN_WIDTH * 2];
	int written = 1;
	for (int y = 0; y < SCREEN_HEIGHT && written; ++y) {
		for (int x = 0; x < SCREEN_WIDTH; ++x) {
			uint16_t pixel = pixels[y * SCREEN_WIDTH + x];
			row[2 * x] = (unsigned char)(pixel & 0xFF);
			row[2 * x + 1] = (unsigned char)(pixel >> 8);
		}
		written = fwrite(row, 1, sizeof(row), out) == sizeof(row);
	}
	if (fclose(out) != 0 || !written) {
		fprintf(stderr, "briefing_pixels: cannot write %s\n", path);
		return 0;
	}
	printf("wrote %s\n", path);
	return 1;
}

/* Points the frontend at the stand-ins and loads the menus, as the header
 * comment says. */
static void open_screens(void)
{
	g_front_state.direct_draw = &g_direct_draw;
	g_front_state.back_buffer_surface = &g_back.surface;
	g_front_state.offscreen_surface = &g_offscreen.surface;
	g_front_state.back_buffer_pitch = SCREEN_WIDTH * 2;
	g_front_state.offscreen_surface_pitch = SCREEN_WIDTH * 2;
	g_front_state.clip_min_x = 0;
	g_front_state.clip_min_y = 0;
	g_front_state.clip_max_x = SCREEN_WIDTH - 1;
	g_front_state.clip_max_y = SCREEN_HEIGHT - 1;
	frontend_load_resources();
	g_front_state.text_color_codes[0] = 0xFFFF;
	g_front_state.text_color_codes[1] =
		frontend_display_pack_rgb(0, 0xFF, 0);
	g_front_state.text_color_codes[2] =
		frontend_display_pack_rgb(0xFF, 0, 0);
	g_front_state.text_color_codes[3] =
		frontend_display_pack_rgb(0xFF, 0xFF, 0);
	g_front_state.text_color_codes[4] =
		frontend_display_pack_rgb(0x32, 0x32, 0xFF);
	g_front_state.text_color_codes[5] =
		frontend_display_pack_rgb(0x80, 0x80, 0xFF);
	g_front_state.cursor_visible = 0;
}

/* Puts the flight assignment screen on the stack for a solo game, at its
 * frame 0. */
static void enter_screen(int team, int directory)
{
	briefing_set_pilot(team, directory);
	g_frontend_mission_session_mode = FRONTEND_MISSION_SESSION_SINGLEPLAYER;
	g_front_state.screen_stack_top = 0;
	g_front_state.screen_states[0].update_fn =
		mission_setup_flight_assignment_update;
	g_front_state.screen_states[0].exit_fn = NULL;
	g_front_state.frame_counter = 0;
}

/* One frame as the game runs it: the screen's frame, then the refill the
 * present makes. Returns 0, printing why, when the screen asks to leave or
 * hands over to another screen (a solo melee whose teams have one player
 * flight group each goes straight to craft selection); between then does
 * not run. */
static int run_frame(long frame, void (*between)(void *), void *context)
{
	int result = xvt_frontend_task_run_frame();
	const struct frontend_screen_state *top =
		&g_front_state.screen_states[g_front_state.screen_stack_top];
	if (result != 0 ||
	    top->update_fn != mission_setup_flight_assignment_update) {
		fprintf(stderr,
			"briefing_pixels: the flight assignment screen left at frame %ld\n",
			frame);
		return 0;
	}
	if (between != NULL) {
		between(context);
	}
	frontend_display_restore_back_buffer();
	return 1;
}

struct frame_request {
	const char *out;
	long frame;
	int written;
};

static void write_frame(void *context)
{
	struct frame_request *request = context;
	char path[PATH_MAX];
	briefing_print_state((int)request->frame);
	snprintf(path, sizeof(path), "%s/frame-%ld.raw", request->out,
		 request->frame);
	request->written = write_screen(path, g_back_pixels);
}

static int is_listed(long frame, int count, char **frames)
{
	for (int i = 0; i < count; ++i) {
		if (strtol(frames[i], NULL, 10) == frame) {
			return 1;
		}
	}
	return 0;
}

static int run_frames(const char *out, int count, char **frames)
{
	long last = 0;
	for (int i = 0; i < count; ++i) {
		long frame = strtol(frames[i], NULL, 10);
		if (frame > last) {
			last = frame;
		}
	}
	for (long frame = 1; frame <= last; ++frame) {
		struct frame_request request = {out, frame, 1};
		int listed = is_listed(frame, count, frames);
		if (!run_frame(frame, listed ? write_frame : NULL, &request)) {
			return 0;
		}
		if (!request.written) {
			return 0;
		}
		if (frame == 1) {
			char path[PATH_MAX];
			snprintf(path, sizeof(path), "%s/offscreen.raw", out);
			if (!write_screen(path, g_offscreen_pixels)) {
				return 0;
			}
		}
	}
	return 1;
}

/* Reads a color name back to its value, as the header comment says. Returns
 * 0 when name is none of them. */
static int read_color(const char *name, int *color)
{
	unsigned int number;
	char tail;
	if (sscanf(name, "shade %u%c", &number, &tail) == 1 &&
	    number < SHADE_COUNT) {
		*color = g_text_shade_ramps[number / 8][number % 8];
	} else if (sscanf(name, "code%u%c", &number, &tail) == 1 &&
		   number >= 1 && number <= 5) {
		*color = (int)g_front_state.text_color_codes[number];
	} else if (strcmp(name, "yellow") == 0) {
		*color = g_color_yellow;
	} else if (strcmp(name, "white") == 0) {
		*color = 0xFFFF;
	} else if (strcmp(name, "major") == 0) {
		*color = frontend_display_pack_rgb(0x96, 0, 0);
	} else if (strcmp(name, "minor") == 0) {
		*color = frontend_display_pack_rgb(0x50, 0, 0);
	} else if (sscanf(name, "x%x%c", &number, &tail) == 1) {
		*color = (int)number;
	} else {
		return 0;
	}
	return 1;
}

/* Takes briefing_dump's escapes back from quoted, which starts after the
 * opening quote, into text. Returns 0 for a malformed TEXT, an escape the
 * dump never prints, or a color byte (1 to 6), which no glyph run holds. */
static int unquote(const char *quoted, char *text, size_t size)
{
	size_t length = 0;
	for (const char *at = quoted; *at != '"'; ++at) {
		unsigned int byte;
		if (*at == '\0' || length + 1 >= size) {
			return 0;
		}
		if (*at == '\\' && (at[1] == '"' || at[1] == '\\')) {
			byte = (unsigned char)*++at;
		} else if (*at == '\\' && at[1] == 'x' &&
			   isxdigit((unsigned char)at[2]) &&
			   isxdigit((unsigned char)at[3]) &&
			   sscanf(at + 2, "%2x", &byte) == 1) {
			at += 3;
		} else if (*at == '\\') {
			return 0;
		} else {
			byte = (unsigned char)*at;
		}
		if (byte >= 1 && byte <= 6) {
			return 0;
		}
		text[length++] = (char)byte;
	}
	text[length] = '\0';
	return 1;
}

/* Replays a text record: TEXT in font 10 at (X, Y) in COLOR. Returns 0
 * for a malformed record. */
static int replay_text(const char *line)
{
	char color_name[32];
	int a, b, color, used = 0;
	char text[LINE_CAPACITY];
	const char *quote = strchr(line, '"');
	if (quote == NULL ||
	    sscanf(line, "text %d %d %n", &a, &b, &used) != 2 ||
	    (size_t)(quote - line) <= (size_t)used + 1 ||
	    (size_t)(quote - line - used - 1) >= sizeof(color_name)) {
		return 0;
	}
	memcpy(color_name, line + used, (size_t)(quote - line - used - 1));
	color_name[quote - line - used - 1] = '\0';
	const char *end = quote + 1;
	while (*end != '\0' && *end != '"') {
		end += *end == '\\' && end[1] != '\0' ? 2 : 1;
	}
	if (!read_color(color_name, &color) ||
	    !unquote(quote + 1, text, sizeof(text)) || *end != '"' ||
	    end[1] != '\0') {
		return 0;
	}
	frontend_text_draw(FONT_SIZE, text, a + PANEL_LEFT, b + PANEL_TOP,
			   color);
	return 1;
}

/* Replays one record line. Returns 0 when it is none the header names. */
static int replay(const char *line)
{
	char kind[16];
	char name[32];
	char color_name[32];
	int a, b, c, d, color, used = 0;
	struct RECT rect;
	if (sscanf(line, "%15s", kind) != 1) {
		return 0;
	}
	if (strcmp(kind, "clip") == 0 &&
	    sscanf(line, "clip %d %d %d %d %n", &a, &b, &c, &d, &used) == 4 &&
	    line[used] == '\0') {
		rect = (struct RECT){a + PANEL_LEFT, b + PANEL_TOP,
				     c + PANEL_LEFT, d + PANEL_TOP};
		frontend_display_set_screen_clip_rect640x480(&rect);
	} else if ((strcmp(kind, "vline") == 0 || strcmp(kind, "hline") == 0) &&
		   sscanf(line, "%*s %d %d %d %31[^\n]", &a, &b, &c,
			  color_name) == 4 &&
		   read_color(color_name, &color)) {
		if (kind[0] == 'v') {
			frontend_draw_vertical_line_clipped(
				b + PANEL_TOP, c + PANEL_TOP, a + PANEL_LEFT,
				color);
		} else {
			frontend_draw_horizontal_line_clipped(
				b + PANEL_LEFT, c + PANEL_LEFT, a + PANEL_TOP,
				color);
		}
	} else if (strcmp(kind, "icon") == 0 &&
		   sscanf(line, "icon %31s %d %d %d %n", name, &a, &b, &c,
			  &used) == 4 &&
		   line[used] == '\0' && a >= 0 && a < 70) {
		front_image_draw_sprite_rect_transparent(
			name, &g_map_icon_rects[a], b + PANEL_LEFT,
			c + PANEL_TOP);
	} else if (strcmp(kind, "tint") == 0 &&
		   sscanf(line, "tint %31s %d %d %d %31[^\n]", name, &a, &b, &c,
			  color_name) == 5 &&
		   a >= 0 && a < 70 && read_color(color_name, &color)) {
		front_image_draw_sprite_rect_tinted(
			name, &g_map_icon_rects[a], b + PANEL_LEFT,
			c + PANEL_TOP, (unsigned int)color);
	} else if ((strcmp(kind, "fill") == 0 || strcmp(kind, "edge") == 0 ||
		    strcmp(kind, "outline") == 0) &&
		   sscanf(line, "%*s %d %d %d %d %31[^\n]", &a, &b, &c, &d,
			  color_name) == 5 &&
		   read_color(color_name, &color)) {
		rect = (struct RECT){a + PANEL_LEFT, b + PANEL_TOP,
				     c + PANEL_LEFT, d + PANEL_TOP};
		if (kind[0] == 'o') {
			frontend_draw_rect_outline(&rect, 0, 0, color);
		} else {
			frontend_draw_rect(&rect, 0, 0, color, kind[0] == 'f');
		}
	} else if (strcmp(kind, "text") == 0) {
		return replay_text(line);
	} else {
		return 0;
	}
	return 1;
}

/* Replays the list at path onto a black back buffer and writes it. */
static int replay_list(const char *out, const char *path)
{
	FILE *list = fopen(path, "r");
	if (list == NULL) {
		fprintf(stderr, "briefing_pixels: cannot read %s\n", path);
		return 0;
	}
	memset(g_back_pixels, 0, sizeof(g_back_pixels));
	g_draw_surface_ptr = frontend_display_lock_back_buffer();
	struct RECT panel = {PANEL_LEFT, PANEL_TOP, PANEL_RIGHT, PANEL_BOTTOM};
	frontend_display_set_screen_clip_rect640x480(&panel);
	char line[LINE_CAPACITY];
	int number = 0;
	int replayed = 1;
	while (replayed && fgets(line, sizeof(line), list) != NULL) {
		++number;
		line[strcspn(line, "\r\n")] = '\0';
		const char *record = line;
		while (*record == ' ') {
			++record;
		}
		if (*record != '\0' && !replay(record)) {
			fprintf(stderr, "briefing_pixels: %s line %d: %s\n",
				path, number, line);
			replayed = 0;
		}
	}
	fclose(list);
	frontend_display_unlock_back_buffer();
	if (!replayed) {
		return 0;
	}
	const char *base = strrchr(path, '/');
	base = base != NULL ? base + 1 : path;
	char name[PATH_MAX];
	snprintf(name, sizeof(name), "%s", base);
	char *dot = strrchr(name, '.');
	if (dot != NULL && dot != name) {
		*dot = '\0';
	}
	char target[PATH_MAX * 2];
	snprintf(target, sizeof(target), "%s/%s.raw", out, name);
	return write_screen(target, g_back_pixels);
}

static int replay_lists(const char *out, int count, char **lists)
{
	if (!run_frame(1, NULL, NULL)) {
		return 0;
	}
	g_front_state.text_fade_frames_left = 0;
	for (int i = 0; i < count; ++i) {
		if (!replay_list(out, lists[i])) {
			return 0;
		}
	}
	return 1;
}

/* Checks frames' numbers before anything runs. */
static int check_frames(int count, char **frames)
{
	for (int i = 0; i < count; ++i) {
		char *end = NULL;
		long frame = strtol(frames[i], &end, 10);
		if (end == NULL || *end != '\0' || frame < 1 ||
		    frame > MAX_FRAMES) {
			fprintf(stderr, "briefing_pixels: bad frame %s\n",
				frames[i]);
			return 0;
		}
	}
	return 1;
}

int main(int argc, char **argv)
{
	char folder[PATH_MAX];
	char *end = NULL;
	long team = argc >= 5 ? strtol(argv[4], &end, 10) : -1;
	int directory = argc >= 4 ? directory_of(argv[3]) : -1;
	int frames = argc >= 8 && strcmp(argv[6], "frames") == 0;
	int pieces = argc >= 8 && strcmp(argv[6], "pieces") == 0;
	if ((!frames && !pieces) || end == NULL || *end != '\0' || team < 0 ||
	    team > 9 || directory < 0 ||
	    (frames && !check_frames(argc - 7, &argv[7]))) {
		fprintf(stderr,
			"Usage: %s ROOT FILE DIRECTORY TEAM OUT frames F...\n"
			"       %s ROOT FILE DIRECTORY TEAM OUT pieces LIST...\n",
			argv[0], argv[0]);
		return 2;
	}
	xvt_log_set_level(AERON_LOG_DEBUG);
	SDL_SetLogPriorities(SDL_LOG_PRIORITY_VERBOSE);
	if (!briefing_make_link_folder("briefing_pixels", argv[1], argv[2],
				       directory, folder, sizeof(folder)) ||
	    !asset_dump_bind_root("briefing_pixels", folder) ||
	    !asset_dump_open_front_state()) {
		return 1;
	}
	open_screens();
	enter_screen((int)team, directory);
	printf("directory %s\nteam %ld\nhost_cd %d\n",
	       g_mission_directory_names[directory], team, g_host_cd_available);
	int written = frames ? run_frames(argv[5], argc - 7, &argv[7])
			     : replay_lists(argv[5], argc - 7, &argv[7]);
	return !written || fflush(stdout) != 0;
}
