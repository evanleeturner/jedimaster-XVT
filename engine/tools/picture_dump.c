/* Prints what the engine decodes from .bmp files, so that another reader of
 * the same files can be checked against the engine without printing every
 * pixel. Build with -DXVT_BUILD_TOOLS=ON; it links the engine library.
 *
 * Usage: picture_dump ROOT < NAMES
 *
 * ROOT is bound as the asset folder, as the game's install is: a game name
 * resolves under ROOT/BalanceOfPower first, then under ROOT, in any letter
 * case. NAMES holds one game name per line (frontres\stars.bmp); empty lines
 * are skipped. Each name is loaded with front_image_register_resource, neither
 * remapped nor compressed, at 16 bits per pixel in the 565 layout, as the
 * front end's start sets the display, then freed again. One line per name:
 *
 *   picture "NAME" file="PATH" width=W height=H pixels=P colors=C
 *   picture "NAME" missing        (the name does not resolve)
 *   picture "NAME" not_loaded     (it resolves but the engine refuses it)
 *
 * PATH is the file the name resolved to, relative to ROOT. P is the 64-bit
 * FNV-1a hash (offset basis 0xcbf29ce484222325, prime 0x100000001b3) of the
 * decoded palette indices, width times height bytes, rows top to bottom; C is
 * the same hash of the image's 256 display colors (its color_lut), each as two
 * bytes, low byte first. Both print as 16 hex digits.
 *
 * The engine's log runs at DEBUG and goes to stderr, as SDL writes it. Exit
 * status: 0 when every line printed; 1 when the front end's tables cannot be
 * allocated or a write fails; 2 for bad arguments. A missing or refused name
 * is printed, not judged. */
#include <SDL3/SDL_log.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "asset_dump.h"
#include "xvt/frontend/front_image.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt_runtime/log/log.h"

enum {
	NAME_CAPACITY = 1024,
	COLOR_COUNT = 256,
};

static const uint64_t FNV_OFFSET_BASIS = 0xcbf29ce484222325u;
static const uint64_t FNV_PRIME = 0x100000001b3u;

static uint64_t fnv1a(uint64_t hash, const uint8_t *bytes, size_t count)
{
	for (size_t i = 0; i < count; ++i) {
		hash ^= bytes[i];
		hash *= FNV_PRIME;
	}
	return hash;
}

/* Prints the line for one image the engine loaded. */
static void print_picture(const struct image_resource *image)
{
	uint64_t pixels = fnv1a(FNV_OFFSET_BASIS, image->pixels,
				(size_t)image->width * (size_t)image->height);
	uint64_t colors = FNV_OFFSET_BASIS;
	for (int i = 0; i < COLOR_COUNT; ++i) {
		uint8_t pair[2] = {
			(uint8_t)(image->color_lut[i] & 0xFF),
			(uint8_t)((image->color_lut[i] >> 8) & 0xFF)};
		colors = fnv1a(colors, pair, sizeof(pair));
	}
	printf(" width=%d height=%d pixels=%016llx colors=%016llx\n",
	       image->width, image->height, (unsigned long long)pixels,
	       (unsigned long long)colors);
}

/* Loads, prints and frees one name. */
static void dump_name(const char *name)
{
	char relative[NAME_CAPACITY];
	printf("picture ");
	asset_dump_quote(name, strlen(name));
	if (!asset_dump_resolve(name, relative, sizeof(relative))) {
		printf(" missing\n");
		return;
	}
	if (!front_image_register_resource(name, name, 0, 0)) {
		printf(" not_loaded\n");
		return;
	}
	int index = front_image_find_resource_by_name(name);
	printf(" file=");
	asset_dump_quote(relative, strlen(relative));
	print_picture(g_front_state.resource_table[index].image);
	front_image_free_resource_by_name(name);
}

int main(int argc, char **argv)
{
	if (argc != 2) {
		fprintf(stderr, "Usage: %s ROOT < NAMES\n", argv[0]);
		return 2;
	}
	xvt_log_set_level(AERON_LOG_DEBUG);
	SDL_SetLogPriorities(SDL_LOG_PRIORITY_VERBOSE);
	if (!asset_dump_bind_root("picture_dump", argv[1]) ||
	    !asset_dump_open_front_state()) {
		return 1;
	}
	char line[NAME_CAPACITY];
	while (fgets(line, sizeof(line), stdin) != NULL) {
		line[strcspn(line, "\r\n")] = '\0';
		if (line[0] != '\0') {
			dump_name(line);
		}
	}
	return fflush(stdout) != 0;
}
