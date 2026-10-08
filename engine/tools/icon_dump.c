/* Prints what the engine keeps and draws for the briefing map's craft icons,
 * so that another reader of the same files can be checked against the engine.
 * Build with -DXVT_BUILD_TOOLS=ON; it links the engine library.
 *
 * Usage: icon_dump ROOT KIND
 *
 * ROOT is bound as the asset folder, as the game's install is: a game name
 * resolves under ROOT/BalanceOfPower first, then under ROOT, in any letter
 * case. The kinds:
 *
 *   sheets   front_image_load_resource_list on frontres\mapicons.lst at 16
 *            bits per pixel in the 565 layout, as the front end loads it: each
 *            image registered, in the engine's table order (by name), with its
 *            size; for each uncompressed one its 256 display colors (the
 *            image's color_lut) and its pixels as the engine decoded them, one
 *            row per line, top row first, two hex digits a palette index.
 *   tables   the briefing map's icon rectangles (g_map_icon_rects) and the
 *            icon of each craft type (g_map_icon_by_craft_type).
 *   draws    loads the sheets as above, then for each IFF 0 to 7 and 255 and
 *            each craft type 0 to 105 draws one flight group with
 *            briefing_map_draw_overlays on a blank 16-bit surface, its map
 *            point at the surface's middle, and prints the pixels the draw
 *            wrote: their box relative to that middle point, and each row of
 *            the box with a written pixel's 16-bit value or "----". Each
 *            draw runs twice, on two backgrounds, so a written pixel that
 *            matches one background still shows.
 *
 * The engine's log runs at DEBUG and goes to stderr, as SDL writes it: its
 * lines name each image's file and compress flag, so a caller keeps stderr
 * beside the sheet. Exit status: 0 when the sheet printed; 1 when the list
 * does not resolve, a table cannot be allocated, or a write fails; 2 for bad
 * arguments. The list reader's own return value is printed, not judged. */
#include <SDL3/SDL_log.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "asset_dump.h"
#include "xvt/frontend/briefing_map.h"
#include "xvt/frontend/front_image.h"
#include "xvt/frontend/frontend_draw.h"
#include "xvt/frontend/frontend_mission.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt_runtime/log/log.h"

enum {
	SURFACE_SIZE = 64,
	SURFACE_MIDDLE = SURFACE_SIZE / 2,
	CRAFT_TYPE_COUNT = 106,
	BRIEFING_ICON_POINT = 14,
	MAP_SCALE = 32,
	FIRST_BACKGROUND = 0x0000,
	SECOND_BACKGROUND = 0xFFFF,
};

static const char g_icon_list[] = "frontres\\mapicons.lst";
static const int g_drawn_iffs[] = {0, 1, 2, 3, 4, 5, 6, 7, 255};

static uint16_t g_first_surface[SURFACE_SIZE * SURFACE_SIZE];
static uint16_t g_second_surface[SURFACE_SIZE * SURFACE_SIZE];

/* Prints the list header and loads the icon sheets into the front end's image
 * table, printing the reader's result and the count. Returns 0 when the list
 * does not resolve or a table cannot be allocated. */
static int load_sheets(const char *kind)
{
	if (!asset_dump_print_header(kind, g_icon_list) ||
	    !asset_dump_open_front_state()) {
		return 0;
	}
	int result = front_image_load_resource_list(g_icon_list);
	printf("result %d\ncount %d\n", result, g_front_state.resource_count);
	return 1;
}

/* Prints an uncompressed image's 256 display colors and its pixel rows. */
static void print_image_pixels(const struct image_resource *image)
{
	printf("colors");
	for (int i = 0; i < 256; ++i) {
		printf(" %04x", (unsigned)image->color_lut[i] & 0xFFFF);
	}
	putchar('\n');
	for (int y = 0; y < image->height; ++y) {
		printf("row %d ", y);
		const uint8_t *row = &image->pixels[y * image->width];
		for (int x = 0; x < image->width; ++x) {
			printf("%02x", row[x]);
		}
		putchar('\n');
	}
}

static int dump_sheets(void)
{
	if (!load_sheets("sheets")) {
		return 1;
	}
	for (int i = 0; i < g_front_state.resource_count; ++i) {
		const struct front_image_resource_record *record =
			&g_front_state.resource_table[i];
		const struct image_resource *image = record->image;
		printf("image %d name=", i);
		asset_dump_quote(record->name, sizeof(record->name));
		printf(" width=%d height=%d compressed=%d\n", image->width,
		       image->height, image->is_compressed);
		if (image->is_compressed == 0) {
			print_image_pixels(image);
		}
	}
	return 0;
}

static int dump_tables(void)
{
	int box_count =
		(int)(sizeof(g_map_icon_rects) / sizeof(*g_map_icon_rects));
	printf("kind tables\nboxes %d\n", box_count);
	for (int i = 0; i < box_count; ++i) {
		const struct RECT *box = &g_map_icon_rects[i];
		printf("box %d left=%d top=%d right=%d bottom=%d\n", i,
		       box->left, box->top, box->right, box->bottom);
	}
	int craft_count = (int)(sizeof(g_map_icon_by_craft_type) /
				sizeof(*g_map_icon_by_craft_type));
	printf("crafts %d\n", craft_count);
	for (int type = 0; type < craft_count; ++type) {
		printf("craft %d icon=%d\n", type,
		       g_map_icon_by_craft_type[type]);
	}
	return 0;
}

/* Fills surface with background and draws the one flight group's overlays on
 * it, the surface's middle being the group's map point. */
static void draw_on(uint16_t *surface, uint16_t background)
{
	for (int i = 0; i < SURFACE_SIZE * SURFACE_SIZE; ++i) {
		surface[i] = background;
	}
	g_draw_surface_ptr = (uint8_t *)surface;
	struct RECT viewport = {0, 0, SURFACE_SIZE, SURFACE_SIZE};
	briefing_map_draw_overlays(&viewport, &viewport);
}

/* Whether the draws wrote the pixel at index i, and if so its value. */
static int written_at(int i, uint16_t *value)
{
	if (g_first_surface[i] != FIRST_BACKGROUND) {
		*value = g_first_surface[i];
		return 1;
	}
	if (g_second_surface[i] != SECOND_BACKGROUND) {
		*value = g_second_surface[i];
		return 1;
	}
	return 0;
}

/* Prints one draw's line and, when it wrote pixels, their box relative to the
 * surface's middle and each of its rows. */
static void print_draw(int iff, int craft_type)
{
	int count = 0;
	int left = SURFACE_SIZE;
	int top = SURFACE_SIZE;
	int right = -1;
	int bottom = -1;
	uint16_t value;
	for (int y = 0; y < SURFACE_SIZE; ++y) {
		for (int x = 0; x < SURFACE_SIZE; ++x) {
			if (!written_at(y * SURFACE_SIZE + x, &value)) {
				continue;
			}
			++count;
			left = x < left ? x : left;
			right = x > right ? x : right;
			top = y < top ? y : top;
			bottom = y > bottom ? y : bottom;
		}
	}
	printf("draw iff=%d craft=%d written=%d\n", iff, craft_type, count);
	if (count == 0) {
		return;
	}
	printf("at %d %d size %d %d\n", left - SURFACE_MIDDLE,
	       top - SURFACE_MIDDLE, right - left + 1, bottom - top + 1);
	for (int y = top; y <= bottom; ++y) {
		printf("row %d", y - SURFACE_MIDDLE);
		for (int x = left; x <= right; ++x) {
			if (written_at(y * SURFACE_SIZE + x, &value)) {
				printf(" %04x", value);
			} else {
				printf(" ----");
			}
		}
		putchar('\n');
	}
}

/* Sets up the one flight group the draws show and the map view: the group at
 * map point (0, 0) on briefing point 14, centre (0, 0), scale 32, no markers
 * or labels, the surface clipped to its edges. */
static void open_map(void)
{
	memset(&g_frontend_mission, 0, sizeof(g_frontend_mission));
	g_frontend_mission.flight_group_count = 1;
	g_frontend_mission.flight_groups[0]
		.mission_point_enabled[BRIEFING_ICON_POINT] = 1;
	g_active_briefing_index = 0;
	g_briefing_map_center.x = 0;
	g_briefing_map_center.y = 0;
	g_briefing_map_scale.x = MAP_SCALE;
	g_briefing_map_scale.y = MAP_SCALE;
	g_front_state.draw_surface_pitch = SURFACE_SIZE * 2;
	g_front_state.clip_min_x = 0;
	g_front_state.clip_max_x = SURFACE_SIZE - 1;
	g_front_state.clip_min_y = 0;
	g_front_state.clip_max_y = SURFACE_SIZE - 1;
}

static int dump_draws(void)
{
	if (!load_sheets("draws")) {
		return 1;
	}
	open_map();
	struct xvt_flight_group *group = &g_frontend_mission.flight_groups[0];
	for (size_t i = 0; i < sizeof(g_drawn_iffs) / sizeof(*g_drawn_iffs);
	     ++i) {
		for (int type = 0; type < CRAFT_TYPE_COUNT; ++type) {
			group->iff = (uint8_t)g_drawn_iffs[i];
			group->craft_type = (craft_species)type;
			draw_on(g_first_surface, FIRST_BACKGROUND);
			draw_on(g_second_surface, SECOND_BACKGROUND);
			print_draw(g_drawn_iffs[i], type);
		}
	}
	return 0;
}

int main(int argc, char **argv)
{
	static const struct {
		const char *kind;
		int (*dump)(void);
	} kinds[] = {
		{"sheets", dump_sheets},
		{"tables", dump_tables},
		{"draws", dump_draws},
	};
	if (argc != 3) {
		fprintf(stderr, "Usage: %s ROOT KIND\n", argv[0]);
		return 2;
	}
	int status = 2;
	for (size_t i = 0; i < sizeof(kinds) / sizeof(*kinds); ++i) {
		if (strcmp(argv[2], kinds[i].kind) != 0) {
			continue;
		}
		xvt_log_set_level(AERON_LOG_DEBUG);
		SDL_SetLogPriorities(SDL_LOG_PRIORITY_VERBOSE);
		if (!asset_dump_bind_root("icon_dump", argv[1])) {
			return 1;
		}
		status = kinds[i].dump();
		break;
	}
	if (status == 2) {
		fprintf(stderr, "Usage: %s ROOT KIND\n", argv[0]);
	}
	if (fflush(stdout) != 0) {
		return 1;
	}
	return status;
}
