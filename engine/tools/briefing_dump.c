/* Plays a mission's briefing in the engine's own briefing code, frame by
 * frame as the mission setup screen plays it, and prints what the briefing
 * holds and what the map panel draws, so that another player of the same
 * briefings can be checked against the engine. Build with
 * -DXVT_BUILD_TOOLS=ON on Linux; it links the engine library and takes the
 * engine's drawing and sound calls with the GNU linker's --wrap, so nothing
 * is drawn or heard.
 *
 * Usage: briefing_dump ROOT FILE teams
 *        briefing_dump ROOT FILE TEAM STEP...
 *
 * ROOT is the game's install. The tool binds as the asset folder a folder of
 * links it makes under /tmp: every entry of ROOT but a top-level "train"
 * folder, and train/zzbriefing.tie linked to FILE, any mission file. The
 * engine then opens the mission as "train\zzbriefing.tie", from a one-entry
 * list the tool hands it in place of the training list. Font 10
 * (times10.abp) and the menu strings (fronttxt.txt) load from ROOT.
 *
 * teams loads FILE once for each team 0 to 9 and prints which of the eight
 * briefings the engine keeps for it, the last one flagged for the team:
 *
 *   team T briefing=I frames=D        (or: team T none)
 *
 * Otherwise the tool loads FILE for team TEAM as the setup screen does
 * (frontend_mission_load_for_briefing), prints the sheet's first lines and
 * "state 0", then runs the steps in order:
 *
 *   run N     N frames of the setup screen, each the briefing's update
 *             (briefing_script_advance_or_reset_at_end) and then the map
 *             panel drawn (mission_briefing_draw_map_viewport)
 *   rewind, stop, play, forward
 *             the setup screen's button of that name, pressed during the
 *             last frame run, after its drawing, with the setup screen's own
 *             handling; a button the screen does not offer then (stop or
 *             forward while stopped, play while playing) is ignored
 *   listall   print every later frame's records, not only listed frames'
 *
 * Every frame run prints its sounds, its state and its drawing:
 *
 *   sound F NAME
 *   state F play=P t=T c=X,Y tc=X,Y s=X,Y ts=X,Y page=N last=B
 *           slots=A0:B0,A1:B1 markers=M labels=L        (one line)
 *   draw F crc=C records=N
 *     RECORD                           (listed frames only, two spaces in)
 *
 * and every press "press F NAME" (or "press F NAME ignored") and the state
 * after it. F counts frames run, from 1. play is 1 while the briefing plays;
 * t the next script frame; c, tc, s and ts the map center, its target, the
 * zoom and its target; page the page number drawn and last the text block it
 * last counted; slots each text slot's on flag and block. M lists the
 * markers on, "SLOT:GROUP:AGE" joined by ';', and L the labels on,
 * "SLOT:STRING:X:Y:ROW:AGE"; each prints "-" when none is on.
 *
 * A frame is listed when it is the first, every 24th, the first after a
 * press, or when its state differs from the frame before in anything but
 * the center, the zoom and the ages. Records, in the order drawn, panel
 * coordinates (the setup screen draws the panel at 84, 96):
 *
 *   clip L T R B                       the screen clip set
 *   vline X T B COLOR, hline Y L R COLOR
 *   icon NAME I X Y                    icon I of g_map_icon_rects from NAME
 *   tint NAME I X Y COLOR              the same, tinted
 *   fill L T R B COLOR, edge L T R B COLOR, outline L T R B COLOR
 *   text X Y COLOR "TEXT"              glyphs of font 10
 *
 * A text record is a run of glyphs drawn one after another: the next glyph
 * joins the run when it is drawn at the run's y, in its color, at the x where
 * the last glyph ended (its x plus its width plus the font's spacing). TEXT
 * escapes '"' and '\' with a backslash and any byte outside ' ' to '~' as
 * \xHH. COLOR is "shade N" for entry N of the 40 text shades (rows 0 to 4 of
 * g_text_shade_ramps read as one strip), "code1" to "code5" for the text
 * color codes, "yellow" for g_color_yellow, "white" for 0xFFFF, "major" and
 * "minor" for the grid's two reds, else "xHEX". C is the CRC-32 (as zlib's
 * crc32) of the frame's records, each followed by a newline, as listed.
 *
 * The engine's log runs at DEBUG and goes to stderr, as SDL writes it. Exit
 * status: 0 when the sheet printed in full; 1 when the folder of links, the
 * font or a write fails; 2 for bad arguments. */
#define _DEFAULT_SOURCE

#include <SDL3/SDL_log.h>
#include <dirent.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "asset_dump.h"
#include "xvt/audio/frontend_sound.h"
#include "xvt/frontend/briefing_map.h"
#include "xvt/frontend/briefing_script.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/front_image.h"
#include "xvt/frontend/frontend.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/frontend_draw.h"
#include "xvt/frontend/frontend_mission.h"
#include "xvt/frontend/frontend_mission_list.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt/frontend/frontend_string.h"
#include "xvt/frontend/frontend_text.h"
#include "xvt/frontend/mission_briefing.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt_runtime/log/log.h"

enum {
	FONT_SIZE = 10,
	SHADE_COUNT = 40,
	LIST_EVERY = 24,
	SHADE_SENTINEL = 0x10000,
	CODE_SENTINEL = 0x20000,
	YELLOW_SENTINEL = 0x30000,
	RUN_CAPACITY = 640,
};

static const char LINK_NAME[] = "zzbriefing.tie";

/* The setup screen's map rectangle, (84, 96) to (443, 335), moved to the
 * panel's own top left. */
static const struct RECT PANEL_RECT = {0, 0, 359, 239};

/* The frame being run, for the sound lines. */
static int g_frame;

/* The records of the frame being drawn, each ending in a newline. */
static char *g_records;
static size_t g_records_length;
static size_t g_records_capacity;
static int g_record_count;
static int g_out_of_memory;

/* The glyph run not yet written as a record. */
static struct {
	int open;
	int x;
	int y;
	unsigned int color;
	int end_x;
	size_t length;
	unsigned char text[RUN_CAPACITY];
} g_run;

static int g_grid_major;
static int g_grid_minor;
static int g_unknown_glyphs;

static struct mission_list_entry g_list_entry;

static uint32_t crc32_of(const char *bytes, size_t length)
{
	uint32_t crc = 0xFFFFFFFFu;
	for (size_t i = 0; i < length; ++i) {
		crc ^= (uint8_t)bytes[i];
		for (int bit = 0; bit < 8; ++bit) {
			crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
		}
	}
	return crc ^ 0xFFFFFFFFu;
}

static void append_record(const char *format, ...)
{
	char line[2 * RUN_CAPACITY + 64];
	va_list arguments;
	va_start(arguments, format);
	int length = vsnprintf(line, sizeof(line) - 1, format, arguments);
	va_end(arguments);
	if (length < 0 || (size_t)length >= sizeof(line) - 1) {
		g_out_of_memory = 1;
		return;
	}
	line[length++] = '\n';
	if (g_records_length + (size_t)length > g_records_capacity) {
		size_t capacity =
			g_records_capacity * 2 + (size_t)length + 4096;
		char *grown = realloc(g_records, capacity);
		if (grown == NULL) {
			g_out_of_memory = 1;
			return;
		}
		g_records = grown;
		g_records_capacity = capacity;
	}
	memcpy(g_records + g_records_length, line, (size_t)length);
	g_records_length += (size_t)length;
	++g_record_count;
}

/* Writes the color's name into name: see the header comment. */
static void name_color(char *name, size_t size, unsigned int color)
{
	if (color >= SHADE_SENTINEL && color < SHADE_SENTINEL + SHADE_COUNT) {
		snprintf(name, size, "shade %u", color - SHADE_SENTINEL);
	} else if (color > CODE_SENTINEL && color <= CODE_SENTINEL + 5) {
		snprintf(name, size, "code%u", color - CODE_SENTINEL);
	} else if (color == YELLOW_SENTINEL) {
		snprintf(name, size, "yellow");
	} else if (color == 0xFFFF) {
		snprintf(name, size, "white");
	} else if ((int)color == g_grid_major) {
		snprintf(name, size, "major");
	} else if ((int)color == g_grid_minor) {
		snprintf(name, size, "minor");
	} else {
		snprintf(name, size, "x%x", color);
	}
}

static void close_run(void)
{
	if (!g_run.open) {
		return;
	}
	char color[32];
	char text[4 * RUN_CAPACITY + 1];
	size_t out = 0;
	for (size_t i = 0; i < g_run.length; ++i) {
		unsigned char byte = g_run.text[i];
		if (byte == '"' || byte == '\\') {
			text[out++] = '\\';
			text[out++] = (char)byte;
		} else if (byte >= ' ' && byte <= '~') {
			text[out++] = (char)byte;
		} else {
			out += (size_t)sprintf(&text[out], "\\x%02x", byte);
		}
	}
	text[out] = '\0';
	name_color(color, sizeof(color), g_run.color);
	append_record("text %d %d %s \"%s\"", g_run.x, g_run.y, color, text);
	g_run.open = 0;
}

/* The character of font 10 whose glyph bits start at pixels with the given
 * width and height, the lowest when several share them; -1 when none. */
static int glyph_character(const struct image_resource *glyph)
{
	const struct bitmap_font *font = g_front_state.font_by_size[FONT_SIZE];
	for (int character = 0; character < 256; ++character) {
		if (glyph->pixels ==
			    &font->p_glyph_bits
				     [font->glyph_bit_offset[character]] &&
		    glyph->width == font->glyph_width[character] &&
		    glyph->height == font->glyph_height[character]) {
			return character;
		}
	}
	return -1;
}

/* How many characters 1 to 255 draw the same glyph bits as a lower one. */
static int count_shared_glyphs(void)
{
	const struct bitmap_font *font = g_front_state.font_by_size[FONT_SIZE];
	int shared = 0;
	for (int character = 1; character < 256; ++character) {
		for (int lower = 0; lower < character; ++lower) {
			if (font->glyph_bit_offset[lower] ==
				    font->glyph_bit_offset[character] &&
			    font->glyph_width[lower] ==
				    font->glyph_width[character] &&
			    font->glyph_height[lower] ==
				    font->glyph_height[character]) {
				++shared;
				break;
			}
		}
	}
	return shared;
}

int __wrap_front_image_draw_glyph(const struct image_resource *glyph, int x,
				  int y, unsigned int color,
				  int apply_text_fade);
int __wrap_front_image_draw_glyph(const struct image_resource *glyph, int x,
				  int y, unsigned int color,
				  int apply_text_fade)
{
	(void)apply_text_fade;
	int character = glyph_character(glyph);
	if (character < 0) {
		++g_unknown_glyphs;
		character = '?';
	}
	if (!g_run.open || g_run.y != y || g_run.color != color ||
	    g_run.end_x != x || g_run.length >= RUN_CAPACITY) {
		close_run();
		g_run.open = 1;
		g_run.x = x;
		g_run.y = y;
		g_run.color = color;
		g_run.length = 0;
	}
	g_run.text[g_run.length++] = (unsigned char)character;
	g_run.end_x = x + glyph->width +
		      g_front_state.font_by_size[FONT_SIZE]->char_spacing;
	return 0;
}

/* The index in g_map_icon_rects of rect, or -1. */
static int icon_index(const struct RECT *rect)
{
	for (int index = 0; index < 70; ++index) {
		if (rect == &g_map_icon_rects[index]) {
			return index;
		}
	}
	return -1;
}

int __wrap_front_image_draw_sprite_rect_transparent(const char *name,
						    const struct RECT *src_rect,
						    int dst_x, int dst_y);
int __wrap_front_image_draw_sprite_rect_transparent(const char *name,
						    const struct RECT *src_rect,
						    int dst_x, int dst_y)
{
	close_run();
	append_record("icon %s %d %d %d", name, icon_index(src_rect), dst_x,
		      dst_y);
	return 1;
}

int __wrap_front_image_draw_sprite_rect_tinted(const char *name,
					       const struct RECT *src_rect,
					       int dst_x, int dst_y,
					       unsigned int tint_color);
int __wrap_front_image_draw_sprite_rect_tinted(const char *name,
					       const struct RECT *src_rect,
					       int dst_x, int dst_y,
					       unsigned int tint_color)
{
	char color[32];
	close_run();
	name_color(color, sizeof(color), tint_color);
	append_record("tint %s %d %d %d %s", name, icon_index(src_rect), dst_x,
		      dst_y, color);
	return 1;
}

void __wrap_frontend_draw_rect(const struct RECT *rect, int dx, int dy,
			       int color, int filled);
void __wrap_frontend_draw_rect(const struct RECT *rect, int dx, int dy,
			       int color, int filled)
{
	char name[32];
	close_run();
	name_color(name, sizeof(name), (unsigned int)color);
	append_record("%s %d %d %d %d %s", filled ? "fill" : "edge",
		      rect->left + dx, rect->top + dy, rect->right + dx,
		      rect->bottom + dy, name);
}

void __wrap_frontend_draw_rect_outline(const struct RECT *rect, int dx, int dy,
				       int color);
void __wrap_frontend_draw_rect_outline(const struct RECT *rect, int dx, int dy,
				       int color)
{
	char name[32];
	close_run();
	name_color(name, sizeof(name), (unsigned int)color);
	append_record("outline %d %d %d %d %s", rect->left + dx, rect->top + dy,
		      rect->right + dx, rect->bottom + dy, name);
}

void __wrap_frontend_draw_vertical_line_clipped(int y0, int y1, int x,
						int color);
void __wrap_frontend_draw_vertical_line_clipped(int y0, int y1, int x,
						int color)
{
	char name[32];
	close_run();
	name_color(name, sizeof(name), (unsigned int)color);
	append_record("vline %d %d %d %s", x, y0, y1, name);
}

void __wrap_frontend_draw_horizontal_line_clipped(int x0, int x1, int y,
						  int color);
void __wrap_frontend_draw_horizontal_line_clipped(int x0, int x1, int y,
						  int color)
{
	char name[32];
	close_run();
	name_color(name, sizeof(name), (unsigned int)color);
	append_record("hline %d %d %d %s", y, x0, x1, name);
}

void __real_frontend_display_set_screen_clip_rect640x480(
	const struct RECT *src);
void __wrap_frontend_display_set_screen_clip_rect640x480(
	const struct RECT *src);
void __wrap_frontend_display_set_screen_clip_rect640x480(const struct RECT *src)
{
	close_run();
	append_record("clip %d %d %d %d", src->left, src->top, src->right,
		      src->bottom);
	__real_frontend_display_set_screen_clip_rect640x480(src);
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
	(void)allow_restart_existing;
	(void)loop;
	(void)priority;
	(void)volume0_to127;
	(void)pan0_to127;
	printf("sound %d %s\n", g_frame, sound_name);
	return 1;
}

/* In place of the training list: one entry, the linked mission. */
void __wrap_mission_setup_load_mission_list(int mission_directory_id);
void __wrap_mission_setup_load_mission_list(int mission_directory_id)
{
	(void)mission_directory_id;
	memset(&g_list_entry, 0, sizeof(g_list_entry));
	snprintf(g_list_entry.file_name, sizeof(g_list_entry.file_name), "%s",
		 LINK_NAME);
	g_list_entry.mission_idx = 0;
	g_mission_list = &g_list_entry;
	g_mission_count = 1;
}

/* Makes the folder of links the header comment describes and writes its
 * path into folder. Returns 0, printing why, when it cannot. */
static int make_link_folder(const char *root, const char *file, char *folder,
			    size_t size)
{
	char real_root[PATH_MAX];
	char real_file[PATH_MAX];
	char from[PATH_MAX * 2];
	char to[PATH_MAX * 2];
	if (realpath(root, real_root) == NULL ||
	    realpath(file, real_file) == NULL) {
		fprintf(stderr, "briefing_dump: %s or %s: not found\n", root,
			file);
		return 0;
	}
	snprintf(folder, size, "/tmp/briefing_dump_root_XXXXXX");
	if (mkdtemp(folder) == NULL) {
		fprintf(stderr,
			"briefing_dump: cannot make a folder in /tmp\n");
		return 0;
	}
	DIR *listing = opendir(real_root);
	if (listing == NULL) {
		fprintf(stderr, "briefing_dump: %s: cannot list\n", real_root);
		return 0;
	}
	const struct dirent *entry;
	while ((entry = readdir(listing)) != NULL) {
		if (strcmp(entry->d_name, ".") == 0 ||
		    strcmp(entry->d_name, "..") == 0 ||
		    strcasecmp(entry->d_name, "train") == 0) {
			continue;
		}
		snprintf(from, sizeof(from), "%s/%s", real_root, entry->d_name);
		snprintf(to, sizeof(to), "%s/%s", folder, entry->d_name);
		if (symlink(from, to) != 0) {
			closedir(listing);
			fprintf(stderr, "briefing_dump: cannot link %s\n", to);
			return 0;
		}
	}
	closedir(listing);
	snprintf(to, sizeof(to), "%s/train", folder);
	if (mkdir(to, 0700) != 0) {
		fprintf(stderr, "briefing_dump: cannot make %s\n", to);
		return 0;
	}
	snprintf(to, sizeof(to), "%s/train/%s", folder, LINK_NAME);
	if (symlink(real_file, to) != 0) {
		fprintf(stderr, "briefing_dump: cannot link %s\n", to);
		return 0;
	}
	return 1;
}

/* Points the pilot at the linked mission for team; datapad sounds on. */
static void set_pilot(int team)
{
	g_pilot_data.team = team;
	g_pilot_data.mission_directory_id = (mission_directory_id)0;
	g_pilot_data.mission_description_ids[0] = 0;
	g_game_config.sfx_datapad_enabled = 1;
	g_game_config.sfx_datapad_volume = 8;
}

static int dump_teams(void)
{
	for (int team = 0; team < 10; ++team) {
		set_pilot(team);
		frontend_mission_init_for_briefing();
		g_briefing_script.duration_frames = INT16_MIN;
		frontend_mission_load_current_with_briefing();
		if (g_briefing_script.duration_frames == INT16_MIN) {
			printf("team %d none\n", team);
		} else {
			printf("team %d briefing=%d frames=%d\n", team,
			       g_active_briefing_index,
			       (int)g_briefing_script.duration_frames);
		}
	}
	return 1;
}

static void print_state(int frame)
{
	printf("state %d play=%d t=%d c=%d,%d tc=%d,%d s=%d,%d ts=%d,%d page=%d last=%d slots=%d:%d,%d:%d markers=",
	       frame, (int)g_briefing_playback_active,
	       (int)g_briefing_script.current_frame,
	       (int)g_briefing_map_center.x, (int)g_briefing_map_center.y,
	       (int)g_briefing_map_target_center.x,
	       (int)g_briefing_map_target_center.y, (int)g_briefing_map_scale.x,
	       (int)g_briefing_map_scale.y, (int)g_briefing_map_target_scale.x,
	       (int)g_briefing_map_target_scale.y, g_briefing_text_page_number,
	       g_briefing_last_narrated_text_block_idx,
	       (int)g_briefing_text_slot_active[0],
	       (int)g_briefing_text_slot_block_idx[0],
	       (int)g_briefing_text_slot_active[1],
	       (int)g_briefing_text_slot_block_idx[1]);
	const char *separator = "";
	for (int slot = 0; slot < 8; ++slot) {
		if (g_briefing_map_fg_marker_active[slot] != 0) {
			printf("%s%d:%d:%d", separator, slot,
			       (int)g_briefing_map_fg_marker_flight_group_idx
				       [slot],
			       (int)g_briefing_map_fg_marker_age[slot]);
			separator = ";";
		}
	}
	printf("%s labels=", *separator == '\0' ? "-" : "");
	separator = "";
	for (int slot = 0; slot < 8; ++slot) {
		if (g_briefing_map_label_active[slot] != 0) {
			printf("%s%d:%d:%d:%d:%d:%d", separator, slot,
			       (int)g_briefing_map_label_text_idx[slot],
			       (int)g_briefing_map_label_x[slot],
			       (int)g_briefing_map_label_y[slot],
			       (int)g_briefing_map_label_style[slot],
			       (int)g_briefing_map_label_age[slot]);
			separator = ";";
		}
	}
	printf("%s\n", *separator == '\0' ? "-" : "");
}

/* The state less the center, the zoom and the ages, written into shape. */
static void shape_of_state(char *shape, size_t size)
{
	int length = snprintf(shape, size, "%d %d,%d %d,%d %d %d %d:%d,%d:%d",
			      (int)g_briefing_playback_active,
			      (int)g_briefing_map_target_center.x,
			      (int)g_briefing_map_target_center.y,
			      (int)g_briefing_map_target_scale.x,
			      (int)g_briefing_map_target_scale.y,
			      g_briefing_text_page_number,
			      g_briefing_last_narrated_text_block_idx,
			      (int)g_briefing_text_slot_active[0],
			      (int)g_briefing_text_slot_block_idx[0],
			      (int)g_briefing_text_slot_active[1],
			      (int)g_briefing_text_slot_block_idx[1]);
	for (int slot = 0; slot < 8 && length > 0 && (size_t)length < size;
	     ++slot) {
		if (g_briefing_map_fg_marker_active[slot] != 0) {
			length += snprintf(
				shape + length, size - (size_t)length,
				" m%d:%d", slot,
				(int)g_briefing_map_fg_marker_flight_group_idx
					[slot]);
		}
	}
	for (int slot = 0; slot < 8 && length > 0 && (size_t)length < size;
	     ++slot) {
		if (g_briefing_map_label_active[slot] != 0) {
			length += snprintf(
				shape + length, size - (size_t)length,
				" l%d:%d:%d:%d:%d", slot,
				(int)g_briefing_map_label_text_idx[slot],
				(int)g_briefing_map_label_x[slot],
				(int)g_briefing_map_label_y[slot],
				(int)g_briefing_map_label_style[slot]);
		}
	}
}

/* Runs one frame of the setup screen and prints it; list forces its
 * records out. */
static void run_frame(int list)
{
	static char previous_shape[512];
	char shape[512];
	struct RECT viewport = PANEL_RECT;
	struct RECT clip = PANEL_RECT;

	++g_frame;
	briefing_script_advance_or_reset_at_end(g_frame);
	g_records_length = 0;
	g_record_count = 0;
	g_run.open = 0;
	mission_briefing_draw_map_viewport(&viewport, &clip, 1);
	close_run();
	print_state(g_frame);
	printf("draw %d crc=%08x records=%d\n", g_frame,
	       (unsigned)crc32_of(g_records, g_records_length), g_record_count);
	shape_of_state(shape, sizeof(shape));
	if (list || g_frame == 1 || g_frame % LIST_EVERY == 0 ||
	    strcmp(shape, previous_shape) != 0) {
		const char *line = g_records;
		const char *end = g_records + g_records_length;
		while (line < end) {
			const char *newline =
				memchr(line, '\n', (size_t)(end - line));
			printf("  %.*s\n", (int)(newline - line), line);
			line = newline + 1;
		}
	}
	snprintf(previous_shape, sizeof(previous_shape), "%s", shape);
}

/* The setup screen's handling of a press of button, from its button code.
 * Returns 0 when the screen does not offer the button now. */
static int press(const char *button)
{
	if (strcmp(button, "rewind") == 0) {
		g_briefing_last_narrated_text_block_idx = 0;
		g_briefing_text_page_number = 0;
		briefing_script_reset_state();
		return 1;
	}
	if (strcmp(button, "stop") == 0) {
		if (g_briefing_playback_active == 0) {
			return 0;
		}
		g_briefing_playback_active = 0;
		return 1;
	}
	if (strcmp(button, "play") == 0) {
		if (g_briefing_playback_active != 0) {
			return 0;
		}
		g_briefing_playback_active = 1;
		return 1;
	}
	if (g_briefing_playback_active == 0) {
		return 0;
	}
	briefing_script_advance_to_next_visible_line();
	if (g_briefing_text_slot_block_idx[1] ==
	    g_briefing_last_narrated_text_block_idx) {
		g_briefing_last_narrated_text_block_idx = 0;
		g_briefing_text_page_number = 0;
		briefing_script_reset_state();
	}
	return 1;
}

static int is_button(const char *step)
{
	return strcmp(step, "rewind") == 0 || strcmp(step, "stop") == 0 ||
	       strcmp(step, "play") == 0 || strcmp(step, "forward") == 0;
}

/* Checks the steps before anything runs. Returns 0, printing why, for a step
 * the header comment does not name. */
static int check_steps(int count, char **steps)
{
	for (int i = 0; i < count; ++i) {
		if (strcmp(steps[i], "run") == 0) {
			char *end = NULL;
			long frames = i + 1 < count
					      ? strtol(steps[i + 1], &end, 10)
					      : -1;
			if (end == NULL || *end != '\0' || frames < 0 ||
			    frames > 1000000) {
				fprintf(stderr,
					"briefing_dump: run needs a count\n");
				return 0;
			}
			++i;
		} else if (!is_button(steps[i]) &&
			   strcmp(steps[i], "listall") != 0) {
			fprintf(stderr, "briefing_dump: unknown step %s\n",
				steps[i]);
			return 0;
		}
	}
	return 1;
}

static int dump_steps(const char *file, int team, int count, char **steps)
{
	int list_all = 0;
	int list_next = 0;

	set_pilot(team);
	frontend_mission_load_for_briefing();
	printf("kind briefing\nfile ");
	asset_dump_quote(file, strlen(file));
	printf("\nteam %d\nbriefing %d frames %d\n", team,
	       g_active_briefing_index, (int)g_briefing_script.duration_frames);
	printf("font %d spacing %d height %d shared %d\n", FONT_SIZE,
	       g_front_state.font_by_size[FONT_SIZE]->char_spacing,
	       frontend_text_get_font_height(FONT_SIZE), count_shared_glyphs());
	printf("panel %d %d %d %d\n", PANEL_RECT.left, PANEL_RECT.top,
	       PANEL_RECT.right, PANEL_RECT.bottom);
	print_state(0);
	for (int i = 0; i < count; ++i) {
		if (strcmp(steps[i], "run") == 0) {
			long frames = strtol(steps[++i], NULL, 10);
			for (long k = 0; k < frames; ++k) {
				run_frame(list_all || list_next);
				list_next = 0;
			}
		} else if (strcmp(steps[i], "listall") == 0) {
			list_all = 1;
		} else {
			int offered = press(steps[i]);
			printf("press %d %s%s\n", g_frame, steps[i],
			       offered ? "" : " ignored");
			print_state(g_frame);
			list_next = 1;
		}
	}
	printf("unknown_glyphs %d\n", g_unknown_glyphs);
	return !g_out_of_memory;
}

int main(int argc, char **argv)
{
	char folder[PATH_MAX];
	int teams = argc == 4 && strcmp(argv[3], "teams") == 0;
	char *end = NULL;
	long team = argc >= 4 ? strtol(argv[3], &end, 10) : -1;
	if (!teams && (argc < 5 || end == NULL || *end != '\0' || team < 0 ||
		       team > 9 || !check_steps(argc - 4, &argv[4]))) {
		fprintf(stderr,
			"Usage: %s ROOT FILE teams\n       %s ROOT FILE TEAM STEP...\n",
			argv[0], argv[0]);
		return 2;
	}
	xvt_log_set_level(AERON_LOG_DEBUG);
	SDL_SetLogPriorities(SDL_LOG_PRIORITY_VERBOSE);
	if (!make_link_folder(argv[1], argv[2], folder, sizeof(folder)) ||
	    !asset_dump_bind_root("briefing_dump", folder) ||
	    !asset_dump_open_front_state()) {
		return 1;
	}
	if (frontend_text_load_font(FONT_SIZE) != 1 ||
	    g_front_state.font_by_size[FONT_SIZE] == NULL) {
		fprintf(stderr, "briefing_dump: font %d does not load\n",
			FONT_SIZE);
		return 1;
	}
	frontend_string_load_table("fronttxt.txt");
	g_grid_major = (int16_t)frontend_display_pack_rgb(0x96, 0, 0);
	g_grid_minor = (int16_t)frontend_display_pack_rgb(0x50, 0, 0);
	printf("yellow %d\n", g_color_yellow);
	for (int index = 0; index < SHADE_COUNT; ++index) {
		g_text_shade_ramps[index / 8][index % 8] =
			SHADE_SENTINEL + index;
	}
	g_front_state.text_color_codes[0] = 0xFFFF;
	for (int code = 1; code <= 5; ++code) {
		g_front_state.text_color_codes[code] = CODE_SENTINEL + code;
	}
	g_color_yellow = YELLOW_SENTINEL;
	int printed =
		teams ? dump_teams()
		      : dump_steps(argv[2], (int)team, argc - 4, &argv[4]);
	return !printed || fflush(stdout) != 0;
}
