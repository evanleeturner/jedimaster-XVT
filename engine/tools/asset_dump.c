#define _XOPEN_SOURCE 700

#include "asset_dump.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "aeron/vfs.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt_runtime/storage/storage.h"

enum {
	PATH_CAPACITY = 1024,
	IMAGE_TABLE_CAPACITY = 512,
	SOUND_TABLE_CAPACITY = 128,
	VOICE_TABLE_CAPACITY = 12,
	DISPLAY_BITS_PER_PIXEL = 16,
};

static const char *g_program = "asset_dump";
static char g_root[PATH_CAPACITY];

void asset_dump_quote(const char *value, size_t size)
{
	putchar('"');
	for (size_t i = 0; i < size && value[i] != '\0'; ++i) {
		unsigned char c = (unsigned char)value[i];
		if (c == '"' || c == '\\') {
			printf("\\%c", c);
		} else if (c == '\n') {
			printf("\\n");
		} else if (c == '\r') {
			printf("\\r");
		} else if (c == '\t') {
			printf("\\t");
		} else if (c < 32 || c >= 127) {
			printf("\\x%02x", c);
		} else {
			putchar(c);
		}
	}
	putchar('"');
}

int asset_dump_resolve(const char *game_name, char *relative, size_t size)
{
	char normalized[PATH_CAPACITY];
	char resolved[PATH_CAPACITY];
	if (!xvt_storage_normalize(game_name, normalized, sizeof(normalized)) ||
	    xvt_storage_resolve_asset(normalized, resolved, sizeof(resolved)) !=
		    1) {
		return 0;
	}
	size_t root_length = strlen(g_root);
	const char *path = resolved;
	if (strncmp(resolved, g_root, root_length) == 0) {
		path = resolved + root_length;
		while (*path == '/') {
			++path;
		}
	}
	snprintf(relative, size, "%s", path);
	return 1;
}

int asset_dump_print_header(const char *kind, const char *game_name)
{
	char relative[PATH_CAPACITY];
	if (!asset_dump_resolve(game_name, relative, sizeof(relative))) {
		fprintf(stderr, "%s: %s does not resolve under %s\n", g_program,
			game_name, g_root);
		return 0;
	}
	printf("kind %s\nname ", kind);
	asset_dump_quote(game_name, strlen(game_name));
	printf("\nfile ");
	asset_dump_quote(relative, strlen(relative));
	putchar('\n');
	return 1;
}

/* The user folder asset_dump_bind_root made; empty before it. */
static char g_user_root[] = "/tmp/asset_dump_user_XXXXXX";

const char *asset_dump_user_root(void) { return g_user_root; }

int asset_dump_bind_root(const char *program, const char *root)
{
	char *user = g_user_root;
	static char temp[] = "/tmp/asset_dump_temp_XXXXXX";
	g_program = program;
	if (realpath(root, g_root) == NULL) {
		fprintf(stderr, "%s: %s: no such folder\n", g_program, root);
		return 0;
	}
	if (mkdtemp(user) == NULL || mkdtemp(temp) == NULL) {
		fprintf(stderr, "%s: cannot make a folder in /tmp\n",
			g_program);
		return 0;
	}
	AeronVfsConfig config = {0};
	config.asset_root = g_root;
	config.resource_root = g_root;
	config.user_root = user;
	config.temp_root = temp;
	AeronVfs *vfs = AeronVfs_Create(&config);
	if (vfs == NULL ||
	    !AeronVfs_SetRootOptions(
		    vfs, AERON_VFS_ROOT_ASSET,
		    AERON_VFS_ROOT_OPTION_CASE_INSENSITIVE_LOOKUP)) {
		fprintf(stderr, "%s: cannot bind %s\n", g_program, g_root);
		return 0;
	}
	xvt_storage_bind(vfs);
	return 1;
}

int asset_dump_open_front_state(void)
{
	g_front_state.frontend_sound_buffers =
		calloc(SOUND_TABLE_CAPACITY,
		       sizeof(*g_front_state.frontend_sound_buffers));
	g_front_state.frontend_sound_voices =
		calloc(VOICE_TABLE_CAPACITY,
		       sizeof(*g_front_state.frontend_sound_voices));
	g_front_state.resource_table = calloc(
		IMAGE_TABLE_CAPACITY, sizeof(*g_front_state.resource_table));
	g_front_state.display_bpp = DISPLAY_BITS_PER_PIXEL;
	g_front_state.pixel_format555 = 0;
	return g_front_state.frontend_sound_buffers != NULL &&
	       g_front_state.frontend_sound_voices != NULL &&
	       g_front_state.resource_table != NULL;
}
