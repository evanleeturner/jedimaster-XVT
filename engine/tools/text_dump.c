/* Prints what the engine keeps from the game's text files and draws from its
 * menu fonts, so that another reader of the same files can be checked against
 * the engine. Build with -DXVT_BUILD_TOOLS=ON; it links the engine library.
 *
 * Usage: text_dump ROOT KIND [POINTS]
 *
 * ROOT is bound as the asset folder, as the game's install is: a game name
 * resolves under ROOT/BalanceOfPower first, then under ROOT, in any letter
 * case. Every sheet starts with the kind, the file's game name and the file it
 * resolved to. The kinds:
 *
 *   strings   string_table_load_game_strings on strings.txt: each table the
 *             engine fills, in the file's order, "table NAME COUNT" and its
 *             entries; a table the engine skips prints "table NAME absent".
 *   front     frontend_string_load_table on fronttxt.txt: the count and each
 *             entry.
 *   specs     tech_library_load_spec_text_table on specdesc.txt: the five
 *             fields of each of the 93 entries, each up to its field's size.
 *   errors    error_text_load_line on xvterr.txt for line 0, 1, 2 and on
 *             until it returns 0: each line's text up to its first NUL.
 *   joystick  config_load_joystick_action_dictionary on joystick.txt: the
 *             count and each entry's code, name and description.
 *   credits   credits_parse_next_page on credits.txt, page after page until
 *             it reports no more: each page's header values and its 32 lines
 *             with their 16-bit colors. A page whose header numbers were not
 *             read (the page after the last one, at the end of the file)
 *             prints "header=unread" in place of values the engine never
 *             set.
 *   font      frontend_text_load_font(POINTS) on times<POINTS>.abp: the font's
 *             fields, each glyph drawn alone at the top-left of a blank 16-bit
 *             surface with front_image_draw_glyph, and a few strings drawn
 *             with frontend_text_draw and measured with
 *             frontend_text_measure_width. Drawn rows print "#" for a written
 *             pixel and "." for one left as it was, over the glyph's own box
 *             or as far right and down as any write reached, whichever is
 *             larger; a string's rows print each written pixel's 16-bit value
 *             or "----".
 *
 * Strings print quoted: a quote or backslash escaped, any byte outside
 * printable ASCII as an escape. The engine's log runs at DEBUG and goes to
 * stderr, as SDL writes it. Exit status: 0 when the sheet printed; 1 when the
 * file does not resolve, the engine does not load it, a table cannot be
 * allocated, or a write fails; 2 for bad arguments. */
#include <SDL3/SDL_log.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "asset_dump.h"
#include "xvt/assets/opt_model.h"
#include "xvt/assets/string_table.h"
#include "xvt/flight/fediskio.h"
#include "xvt/flight/hud/hud.h"
#include "xvt/flight/hud/mfd.h"
#include "xvt/flight/hud/msg.h"
#include "xvt/flight/mission/goals.h"
#include "xvt/flight/object/damage.h"
#include "xvt/flight/proving_grounds.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/credits.h"
#include "xvt/frontend/front_image.h"
#include "xvt/frontend/frontend.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/frontend_draw.h"
#include "xvt/frontend/frontend_joystick.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt/frontend/frontend_string.h"
#include "xvt/frontend/frontend_text.h"
#include "xvt/frontend/tech_library.h"
#include "xvt/util/memory.h"
#include "xvt_runtime/log/log.h"

#define COUNT_OF(array) (sizeof(array) / sizeof((array)[0]))

enum {
	STRING_DATA_CAPACITY = 0x7D00,
	GOAL_CONDITION_ROWS = 188,
	GOAL_CONDITIONS_PER_BLOCK = 47,
	MODEL_COUNT = 73,
	SPEC_ENTRY_COUNT = 93,
	ERROR_TEXT_CAPACITY = 256,
	ERROR_LINE_LIMIT = 1000,
	CREDITS_PAGE_LIMIT = 500,
	CREDITS_LINES = 32,
	UNREAD = -1,
	SURFACE_WIDTH = 640,
	SURFACE_HEIGHT = 64,
	WHITE = 0xFFFF,
};

static uint16_t g_surface[SURFACE_WIDTH * SURFACE_HEIGHT];

static void print_entry(int index, const char *text, size_t size)
{
	printf("entry %d ", index);
	if (text == NULL) {
		printf("null\n");
		return;
	}
	asset_dump_quote(text, size);
	putchar('\n');
}

static void print_table(const char *name, const char *const *entries, int count)
{
	printf("table %s %d\n", name, count);
	for (int i = 0; i < count; ++i) {
		print_entry(i, entries[i],
			    strlen(entries[i] ? entries[i] : ""));
	}
}

/* Prints a goal condition table, or "absent" when the engine left it unread. */
static void print_goal_conditions(const char *name,
				  const char *const (*rows)[14])
{
	if (rows[0][0] == NULL) {
		printf("table %s absent\n", name);
		return;
	}
	int count = 0;
	for (int row = 0; row < GOAL_CONDITION_ROWS; ++row) {
		count += g_goal_condition_text_variant_count
			[row % GOAL_CONDITIONS_PER_BLOCK];
	}
	printf("table %s %d\n", name, count);
	for (int row = 0; row < GOAL_CONDITION_ROWS; ++row) {
		int variants = g_goal_condition_text_variant_count
			[row % GOAL_CONDITIONS_PER_BLOCK];
		for (int variant = 0; variant < variants; ++variant) {
			printf("entry %d.%d ", row, variant);
			asset_dump_quote(rows[row][variant],
					 strlen(rows[row][variant]));
			putchar('\n');
		}
	}
}

#define PRINT_TABLE(name, array)                                               \
	print_table(name, (const char *const *)(array), (int)COUNT_OF(array))

static int dump_strings(void)
{
	if (!asset_dump_print_header("strings", "strings.txt")) {
		return 1;
	}
	g_string_data_handle = memory_alloc_handle(STRING_DATA_CAPACITY, 0);
	if (g_string_data_handle == 0) {
		return 1;
	}
	string_table_load_game_strings(1);
	PRINT_TABLE("damage_system_names", g_str_damage_system_names);
	PRINT_TABLE("file_error_messages", g_str_file_error_messages);
	PRINT_TABLE("disk_io_messages", g_str_disk_io_messages);
	PRINT_TABLE("proving_grounds_status_labels",
		    g_proving_grounds_status_labels);
	print_table("goal_escape", (const char *const *)g_str_goal_escape, 1);
	print_goal_conditions(
		"goal_conditions_masculine",
		(const char *const(*)[14])g_str_goal_cond_masculine);
	PRINT_TABLE("goal_percentages", g_str_goal_percentages);
	PRINT_TABLE("goal_operators", g_str_goal_operators);
	PRINT_TABLE("goal_titles", g_str_goal_titles);
	PRINT_TABLE("goal_conjunctions", g_str_goal_conjunctions);
	PRINT_TABLE("goal_sides", g_str_goal_sides);
	PRINT_TABLE("goal_family_names", g_str_goal_family_names);
	PRINT_TABLE("goal_genus_names", g_str_goal_genus_names);
	PRINT_TABLE("map_room_text", g_str_map_room_text);
	PRINT_TABLE("in_flight_messages", g_str_in_flight_messages);
	PRINT_TABLE("cmd_threat_display_text", g_str_cmd_threat_display_text);
	PRINT_TABLE("waypoint_names", g_str_waypoint_names);
	PRINT_TABLE("mesh_component_names", g_str_mesh_component_names);
	PRINT_TABLE("cockpit_overlay_text", g_str_cockpit_overlay_text);
	PRINT_TABLE("threat_display_text", g_str_threat_display_text);
	PRINT_TABLE("status_strings", g_str_status_strings);
	PRINT_TABLE("warhead_names", g_str_warhead_names);
	print_table("unknown", (const char *const *)&g_str_unknown, 1);
	PRINT_TABLE("sat_mine_probe_buoy_pilot_names",
		    g_str_sat_mine_probe_buoy_pilot_names);
	printf("table model_names %d\n", MODEL_COUNT);
	for (int i = 0; i < MODEL_COUNT; ++i) {
		printf("entry %d gender=%d ", i, g_craft_gender[i]);
		asset_dump_quote(g_model_defs[i].name_long,
				 strlen(g_model_defs[i].name_long));
		putchar('\n');
	}
	PRINT_TABLE("species_names_plural", g_str_species_names_plural);
	PRINT_TABLE("wingman_commands", g_str_wingman_commands);
	print_goal_conditions(
		"goal_conditions_feminine",
		(const char *const(*)[14])g_str_goal_cond_feminine);
	print_goal_conditions(
		"goal_conditions_neutered",
		(const char *const(*)[14])g_str_goal_cond_neutered);
	return 0;
}

static int dump_front(void)
{
	if (!asset_dump_print_header("front", "fronttxt.txt")) {
		return 1;
	}
	frontend_string_load_table("fronttxt.txt");
	printf("count %u\n", g_front_state.ui_string_count);
	for (unsigned int i = 0; i < g_front_state.ui_string_count; ++i) {
		const char *text = frontend_string_get((frontend_string_id)i);
		print_entry((int)i, text, strlen(text));
	}
	return 0;
}

static void print_field(const char *name, const char *value, size_t size)
{
	printf(" %s=", name);
	asset_dump_quote(value, size);
}

static int dump_specs(void)
{
	if (!asset_dump_print_header("specs", "specdesc.txt")) {
		return 1;
	}
	int result = tech_library_load_spec_text_table();
	printf("result %d\n", result);
	if (g_tech_library_spec_text_table == NULL) {
		return 1;
	}
	for (int i = 0; i < SPEC_ENTRY_COUNT; ++i) {
		const struct tech_library_spec_text *spec =
			&g_tech_library_spec_text_table[i];
		printf("spec %d", i);
		print_field("name", spec->craft_name, sizeof(spec->craft_name));
		print_field("manufacturer", spec->manufacturer,
			    sizeof(spec->manufacturer));
		print_field("users", spec->in_use_by, sizeof(spec->in_use_by));
		print_field("description", spec->description,
			    sizeof(spec->description));
		print_field("crew", spec->crew, sizeof(spec->crew));
		putchar('\n');
	}
	return 0;
}

static int dump_errors(void)
{
	if (!asset_dump_print_header("errors", "xvterr.txt")) {
		return 1;
	}
	int line = 0;
	for (; line < ERROR_LINE_LIMIT; ++line) {
		char text[ERROR_TEXT_CAPACITY];
		memset(text, 0, sizeof(text));
		if (error_text_load_line(line, text) == 0) {
			break;
		}
		print_entry(line, text, sizeof(text) - 1);
	}
	printf("count %d\n", line);
	return 0;
}

static int dump_joystick(void)
{
	if (!asset_dump_print_header("joystick", "joystick.txt")) {
		return 1;
	}
	int result = config_load_joystick_action_dictionary();
	printf("result %d\ncount %d\n", result, g_joystick_entry_count);
	for (int i = 0; i < g_joystick_entry_count && i < 128; ++i) {
		const struct joystick_entry *entry = &g_joystick_entries[i];
		printf("action %d code=%d", i, entry->action_code);
		print_field("name", entry->name, sizeof(entry->name));
		print_field("description", entry->description,
			    sizeof(entry->description));
		putchar('\n');
	}
	return 0;
}

static int dump_credits(void)
{
	if (!asset_dump_print_header("credits", "credits.txt")) {
		return 1;
	}
	if (!asset_dump_open_front_state()) {
		return 1;
	}
	g_credits_current_text_color = (uint16_t)-1;
	g_frontend_credits_file = file_open("credits.txt", "rt");
	if (g_frontend_credits_file == NULL) {
		return 1;
	}
	for (int page = 0; page < CREDITS_PAGE_LIMIT; ++page) {
		unsigned int buffer = 0;
		int more = 0;
		int duration = UNREAD;
		int fade = UNREAD;
		int result = credits_parse_next_page(&buffer, &more, &duration,
						     &fade);
		printf("page %d result=%d buffer=%u more=%d", page, result,
		       buffer, more);
		if (duration == UNREAD && fade == UNREAD) {
			/* The header's numbers were not read: the engine's
			 * own copies of them were never set. */
			printf(" header=unread\n");
		} else {
			printf(" duration=%d fade=%d", duration, fade);
			printf(" text_x=%d text_y=%d logo=%d logo_x=%d logo_y=%d\n",
			       g_credits_text_x[buffer],
			       g_credits_text_y[buffer],
			       g_credits_logo_id[buffer],
			       g_credits_logo_x[buffer],
			       g_credits_logo_y[buffer]);
		}
		for (int line = 0; line < CREDITS_LINES; ++line) {
			printf("line %d color=%04x ", line,
			       (unsigned)g_credits_text_colors[buffer][line]);
			asset_dump_quote(
				g_credits_text_lines[buffer][line],
				sizeof(g_credits_text_lines[buffer][line]));
			putchar('\n');
		}
		if (more == 0) {
			break;
		}
	}
	file_close(g_frontend_credits_file);
	return 0;
}

static void clear_surface(void)
{
	for (size_t i = 0; i < COUNT_OF(g_surface); ++i) {
		g_surface[i] = 0;
	}
}

/* The extent of written pixels: one past the rightmost and lowest written. */
static void written_extent(int *right, int *bottom, int *count)
{
	*right = 0;
	*bottom = 0;
	*count = 0;
	for (int y = 0; y < SURFACE_HEIGHT; ++y) {
		for (int x = 0; x < SURFACE_WIDTH; ++x) {
			if (g_surface[y * SURFACE_WIDTH + x] != 0) {
				++*count;
				*right = x + 1 > *right ? x + 1 : *right;
				*bottom = y + 1 > *bottom ? y + 1 : *bottom;
			}
		}
	}
}

static void print_glyph(const struct bitmap_font *font, int character)
{
	struct image_resource glyph;
	memset(&glyph, 0, sizeof(glyph));
	glyph.width = font->glyph_width[character];
	glyph.height = font->glyph_height[character];
	glyph.is_compressed = 1;
	glyph.pixels = &font->p_glyph_bits[font->glyph_bit_offset[character]];
	clear_surface();
	int clip = front_image_draw_glyph(&glyph, 0, 0, WHITE, 0);
	int right;
	int bottom;
	int count;
	written_extent(&right, &bottom, &count);
	printf("glyph %d width=%d height=%d offset=%u clip=%d written=%d\n",
	       character, glyph.width, glyph.height,
	       font->glyph_bit_offset[character], clip, count);
	int columns = right > glyph.width ? right : glyph.width;
	int rows = bottom > glyph.height ? bottom : glyph.height;
	for (int y = 0; y < rows; ++y) {
		printf("row %d ", y);
		for (int x = 0; x < columns; ++x) {
			putchar(g_surface[y * SURFACE_WIDTH + x] ? '#' : '.');
		}
		putchar('\n');
	}
}

static void print_text(int points, const char *text)
{
	clear_surface();
	int clip = frontend_text_draw(points, text, 0, 0, WHITE);
	int right;
	int bottom;
	int count;
	written_extent(&right, &bottom, &count);
	printf("text ");
	asset_dump_quote(text, strlen(text));
	printf(" measure=%d clip=%d written=%d right=%d bottom=%d\n",
	       frontend_text_measure_width(text, points), clip, count, right,
	       bottom);
	for (int y = 0; y < bottom; ++y) {
		printf("row %d", y);
		for (int x = 0; x < right; ++x) {
			uint16_t value = g_surface[y * SURFACE_WIDTH + x];
			if (value != 0) {
				printf(" %04x", value);
			} else {
				printf(" ----");
			}
		}
		putchar('\n');
	}
}

static int dump_font(int points)
{
	char name[32];
	snprintf(name, sizeof(name), "times%d.abp", points);
	if (!asset_dump_print_header("font", name) ||
	    !asset_dump_open_front_state()) {
		return 1;
	}
	g_front_state.text_color_codes[0] = WHITE;
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
	g_draw_surface_ptr = (uint8_t *)g_surface;
	g_front_state.draw_surface_pitch = SURFACE_WIDTH * 2;
	g_front_state.clip_min_x = 0;
	g_front_state.clip_max_x = SURFACE_WIDTH - 1;
	g_front_state.clip_min_y = 0;
	g_front_state.clip_max_y = SURFACE_HEIGHT - 1;
	int result = frontend_text_load_font(points);
	printf("result %d\n", result);
	const struct bitmap_font *font = g_front_state.font_by_size[points];
	if (result != 1 || font == NULL) {
		return 1;
	}
	printf("points %u in_use %d spacing %d field_60a %d height %d\n",
	       font->point_size, font->in_use, font->char_spacing,
	       font->field_60a, frontend_text_get_font_height(points));
	printf("color_codes");
	for (int i = 0; i < 6; ++i) {
		printf(" %04x", (unsigned)g_front_state.text_color_codes[i]);
	}
	putchar('\n');
	for (int character = 0; character < 256; ++character) {
		print_glyph(font, character);
	}
	static const char *const samples[] = {
		"Pilot Records",
		"AVAWAY 0123456789",
		"a\002b\003c\004d\005e\006f\001g",
		"",
		"\x80\x81\xe9\xff",
	};
	for (size_t i = 0; i < COUNT_OF(samples); ++i) {
		print_text(points, samples[i]);
	}
	return 0;
}

int main(int argc, char **argv)
{
	static const struct {
		const char *kind;
		int (*dump)(void);
	} kinds[] = {
		{"strings", dump_strings},   {"front", dump_front},
		{"specs", dump_specs},	     {"errors", dump_errors},
		{"joystick", dump_joystick}, {"credits", dump_credits},
	};
	if (argc < 3 || argc > 4) {
		fprintf(stderr, "Usage: %s ROOT KIND [POINTS]\n", argv[0]);
		return 2;
	}
	int points = 0;
	int (*dump)(void) = NULL;
	if (strcmp(argv[2], "font") == 0 && argc == 4) {
		points = atoi(argv[3]);
		if (points < 1 || points > 255) {
			fprintf(stderr,
				"Usage: %s ROOT font POINTS (1 to 255)\n",
				argv[0]);
			return 2;
		}
	} else if (argc == 3) {
		for (size_t i = 0; i < COUNT_OF(kinds); ++i) {
			if (strcmp(argv[2], kinds[i].kind) == 0) {
				dump = kinds[i].dump;
			}
		}
	}
	if (points == 0 && dump == NULL) {
		fprintf(stderr, "Usage: %s ROOT KIND [POINTS]\n", argv[0]);
		return 2;
	}
	xvt_log_set_level(AERON_LOG_DEBUG);
	SDL_SetLogPriorities(SDL_LOG_PRIORITY_VERBOSE);
	if (!asset_dump_bind_root("text_dump", argv[1])) {
		return 1;
	}
	int status = points != 0 ? dump_font(points) : dump();
	if (fflush(stdout) != 0) {
		return 1;
	}
	return status;
}
