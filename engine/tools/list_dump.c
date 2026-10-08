/* Prints what the engine's own readers keep from one of the game's list files,
 * so that another reader of the same files can be checked against the engine.
 * Build with -DXVT_BUILD_TOOLS=ON; it links the engine library.
 *
 * Usage: list_dump ROOT KIND [ARGUMENTS]
 *
 * ROOT is bound as the asset folder, as the game's install is: a game name
 * resolves under ROOT/BalanceOfPower first, then under ROOT, in any letter
 * case. Every sheet starts with the kind, the game name and the host path it
 * resolved to, relative to ROOT, so the caller can tell which file was read.
 * The kinds, each calling the engine's reader for that list:
 *
 *   menu DIRECTORY VIEW      mission_setup_load_mission_list. DIRECTORY is 0
 *                            to 5 (train, melee, tourn, combat, battle,
 *                            campaign). VIEW network reads mission.lst as a
 *                            network host does; rebel and imperial read the
 *                            list a solo pilot of that faction gets. Prints
 *                            each entry kept. The pilot record is zeroed, so
 *                            every entry marked '*' prints as unavailable.
 *   sequence DIRECTORY VIEW INDEX
 *                            the sequence file named by entry INDEX of that
 *                            menu: the description through
 *                            mission_setup_load_mission_desc_text, then the
 *                            count line and the mission lines, read here with
 *                            the engine's line reader the way
 *                            mission_setup_select_first_sequence_mission
 *                            names a mission (line ordinal + 1 after the count
 *                            line), each line as that reader returns it.
 *   images LIST              front_image_load_resource_list: each image
 *                            registered, in the engine's table order (by
 *                            name), with the bitmap's size.
 *   sounds LIST              frontend_sound_load_list, after starting SDL's
 *                            audio and opening the front end's sound device
 *                            (run with SDL_AUDIODRIVER set to dummy where
 *                            there is no sound card): each sound loaded, in
 *                            table order (by name).
 *   ships                    ship_list_load, which reads frontres\frntspec.lst:
 *                            each ship kept and the type-to-ship table.
 *   cutscenes LIST           cutscene_load_table: each cutscene kept.
 *   awards LIST              pilot_record_load_campaign_award_sprite_table:
 *                            each campaign's medal sprites.
 *
 * The engine's log runs at DEBUG and goes to stderr, as SDL writes it: its
 * lines name values the tables drop, such as each image's file and compress
 * flag, so a caller keeps stderr beside the sheet. Text prints quoted with C
 * escapes, so a carriage return or a stray byte the engine kept shows. A menu
 * list that does not exist is refused before the engine reads it, since the
 * engine's reader ends the program then. Exit status: 0 when the sheet
 * printed; 1 when the game name does not resolve, the sound device does not
 * open, or a write fails; 2 for bad arguments. The reader's own return value
 * is printed, not judged. */
#define _XOPEN_SOURCE 700

#include <SDL3/SDL_init.h>
#include <SDL3/SDL_log.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "aeron/vfs.h"
#include "xvt/assets/file.h"
#include "xvt/audio/frontend_sound.h"
#include "xvt/frontend/cutscene.h"
#include "xvt/frontend/front_image.h"
#include "xvt/frontend/frontend_mission.h"
#include "xvt/frontend/frontend_mission_list.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt/frontend/mission_debrief.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt_runtime/log/log.h"
#include "xvt_runtime/storage/storage.h"

enum {
	PATH_CAPACITY = 1024,
	IMAGE_TABLE_CAPACITY = 512,
	SOUND_TABLE_CAPACITY = 128,
	VOICE_TABLE_CAPACITY = 12,
	DISPLAY_BITS_PER_PIXEL = 16,
	SEQUENCE_LINE_CAPACITY = 256,
	DESCRIPTION_CAPACITY = 4096,
};

static char g_root[PATH_CAPACITY];

/* Prints value quoted, at most size bytes, stopping at a NUL; a quote or
 * backslash is escaped and any byte outside printable ASCII prints as an
 * escape. */
static void quote(const char *value, size_t size)
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

/* Prints the sheet's first lines: the kind, the game name and the host path it
 * resolves to, relative to the root, the name's separators cleaned as the
 * engine's file opening cleans them. Returns 0, printing why on stderr, when
 * the name does not resolve. */
static int print_header(const char *kind, const char *game_name)
{
	char normalized[PATH_CAPACITY];
	char resolved[PATH_CAPACITY];
	if (!xvt_storage_normalize(game_name, normalized, sizeof(normalized)) ||
	    xvt_storage_resolve_asset(normalized, resolved, sizeof(resolved)) !=
		    1) {
		fprintf(stderr, "list_dump: %s does not resolve under %s\n",
			game_name, g_root);
		return 0;
	}
	size_t root_length = strlen(g_root);
	const char *relative = resolved;
	if (strncmp(resolved, g_root, root_length) == 0) {
		relative = resolved + root_length;
		while (*relative == '/') {
			++relative;
		}
	}
	printf("kind %s\nname ", kind);
	quote(game_name, strlen(game_name));
	printf("\nfile ");
	quote(relative, strlen(relative));
	putchar('\n');
	return 1;
}

/* Binds storage to the root, with fresh user and temp folders under /tmp, and
 * looks names up in the root in any letter case, as the game's setup does.
 * Returns 0 when a folder or the binding cannot be made. */
static int bind_root(const char *root)
{
	static char user[] = "/tmp/list_dump_user_XXXXXX";
	static char temp[] = "/tmp/list_dump_temp_XXXXXX";
	if (realpath(root, g_root) == NULL) {
		fprintf(stderr, "list_dump: %s: no such folder\n", root);
		return 0;
	}
	if (mkdtemp(user) == NULL || mkdtemp(temp) == NULL) {
		fprintf(stderr, "list_dump: cannot make a folder in /tmp\n");
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
		fprintf(stderr, "list_dump: cannot bind %s\n", g_root);
		return 0;
	}
	xvt_storage_bind(vfs);
	return 1;
}

/* Reads a directory number 0 to 5 and a view; returns 0 for anything else.
 * Sets the session mode and faction the engine's menu reader decides by, and
 * writes the game name that reader opens for them into game_name. */
static int set_menu_view(const char *directory_text, const char *view,
			 int *directory, char *game_name, size_t capacity)
{
	char *end;
	long value = strtol(directory_text, &end, 10);
	if (*end != '\0' || value < 0 || value > 5) {
		return 0;
	}
	*directory = (int)value;
	memset(&g_pilot_data, 0, sizeof(g_pilot_data));
	const char *file = "mission.lst";
	if (strcmp(view, "network") == 0) {
		g_frontend_mission_session_mode =
			FRONTEND_MISSION_SESSION_NET_HOST;
	} else if (strcmp(view, "rebel") == 0 ||
		   strcmp(view, "imperial") == 0) {
		g_frontend_mission_session_mode =
			FRONTEND_MISSION_SESSION_SINGLEPLAYER;
		g_pilot_data.current_faction_id = strcmp(view, "imperial") == 0;
		if (value == MISSION_DIRECTORY_TRAINING_EXERCISES ||
		    value == MISSION_DIRECTORY_COMBAT_ENGAGEMENTS ||
		    value == MISSION_DIRECTORY_CAMPAIGNS) {
			file = strcmp(view, "imperial") == 0 ? "imperial.lst"
							     : "rebel.lst";
		}
	} else {
		return 0;
	}
	snprintf(game_name, capacity, "%s\\%s",
		 g_mission_directory_names[value], file);
	return 1;
}

/* Prints each entry the engine's menu reader kept. */
static void print_menu_entries(void)
{
	printf("count %u\n", g_mission_count);
	for (unsigned int i = 0; g_mission_list != NULL && i < g_mission_count;
	     ++i) {
		const struct mission_list_entry *entry = &g_mission_list[i];
		printf("entry %u id=%d unavailable=%d section=", i,
		       entry->mission_idx, entry->is_unavailable);
		quote(entry->section_name, sizeof(entry->section_name));
		printf(" file=");
		quote(entry->file_name, sizeof(entry->file_name));
		printf(" description=");
		quote(entry->description, sizeof(entry->description));
		putchar('\n');
	}
}

static int dump_menu(int argc, char **argv)
{
	int directory;
	char game_name[PATH_CAPACITY];
	if (argc != 2 || !set_menu_view(argv[0], argv[1], &directory, game_name,
					sizeof(game_name))) {
		return 2;
	}
	if (!print_header("menu", game_name)) {
		return 1;
	}
	mission_setup_load_mission_list(directory);
	print_menu_entries();
	return 0;
}

/* Prints the sequence file's count line and the mission lines it names, read
 * with the engine's line reader as the sequence screens read them. */
static void print_sequence_lines(const char *game_name)
{
	char line[SEQUENCE_LINE_CAPACITY];
	xvt_file *stream = file_open(game_name, "r");
	if (stream == NULL) {
		printf("lines unreadable\n");
		return;
	}
	if (FILE_GETS(line, (int)sizeof(line), stream) == NULL) {
		printf("lines empty\n");
		file_close(stream);
		return;
	}
	int count = atoi(line);
	printf("count_line ");
	quote(line, sizeof(line));
	printf(" count=%d\n", count);
	for (int ordinal = 0; ordinal < count; ++ordinal) {
		if (FILE_GETS(line, (int)sizeof(line), stream) == NULL) {
			printf("mission %d missing\n", ordinal);
			break;
		}
		printf("mission %d line=", ordinal);
		quote(line, sizeof(line));
		putchar('\n');
	}
	file_close(stream);
}

static int dump_sequence(int argc, char **argv)
{
	int directory;
	char menu_name[PATH_CAPACITY];
	if (argc != 3 || !set_menu_view(argv[0], argv[1], &directory, menu_name,
					sizeof(menu_name))) {
		return 2;
	}
	char *end;
	long index = strtol(argv[2], &end, 10);
	if (*end != '\0' || index < 0) {
		return 2;
	}
	mission_setup_load_mission_list(directory);
	if (g_mission_list == NULL || (unsigned long)index >= g_mission_count) {
		fprintf(stderr, "list_dump: %s has no entry %ld\n", menu_name,
			index);
		return 1;
	}
	const struct mission_list_entry *entry = &g_mission_list[index];
	char game_name[PATH_CAPACITY];
	snprintf(game_name, sizeof(game_name), "%s\\%s",
		 g_mission_directory_names[directory], entry->file_name);
	if (!print_header("sequence", game_name)) {
		return 1;
	}
	printf("menu ");
	quote(menu_name, strlen(menu_name));
	printf(" entry=%ld id=%d\n", index, entry->mission_idx);

	static char description[DESCRIPTION_CAPACITY];
	g_pilot_data.mission_directory_id = directory;
	g_pilot_data.mission_description_ids[directory] = entry->mission_idx;
	mission_setup_load_mission_desc_text(description);
	printf("description ");
	quote(description, sizeof(description));
	putchar('\n');
	print_sequence_lines(game_name);
	return 0;
}

/* Sets up the front end's tables and display depth the way the front end's
 * start does (frontend_task.c): room for 128 sounds, 12 voices and 512
 * images, 16 bits per pixel. Returns 0 when a table cannot be allocated. */
static int open_front_state(void)
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
	return g_front_state.frontend_sound_buffers != NULL &&
	       g_front_state.frontend_sound_voices != NULL &&
	       g_front_state.resource_table != NULL;
}

static int dump_images(int argc, char **argv)
{
	if (argc != 1 || !print_header("images", argv[0])) {
		return argc != 1 ? 2 : 1;
	}
	if (!open_front_state()) {
		return 1;
	}
	int result = front_image_load_resource_list(argv[0]);
	printf("result %d\ncount %d\n", result, g_front_state.resource_count);
	for (int i = 0; i < g_front_state.resource_count; ++i) {
		const struct front_image_resource_record *record =
			&g_front_state.resource_table[i];
		printf("image %d name=", i);
		quote(record->name, sizeof(record->name));
		printf(" width=%d height=%d compressed=%d\n",
		       record->image->width, record->image->height,
		       record->image->is_compressed);
	}
	return 0;
}

static int dump_sounds(int argc, char **argv)
{
	if (argc != 1 || !print_header("sounds", argv[0])) {
		return argc != 1 ? 2 : 1;
	}
	if (!open_front_state() || !SDL_InitSubSystem(SDL_INIT_AUDIO) ||
	    !frontend_sound_init_direct_sound(NULL)) {
		fprintf(stderr, "list_dump: the sound device did not open\n");
		return 1;
	}
	int result = frontend_sound_load_list(argv[0]);
	printf("result %d\ncount %d\n", result,
	       g_front_state.frontend_sound_buffer_count);
	for (int i = 0; i < g_front_state.frontend_sound_buffer_count; ++i) {
		const struct frontend_sound_buffer_record *record =
			&g_front_state.frontend_sound_buffers[i];
		printf("sound %d name=", i);
		quote(record->name, sizeof(record->name));
		printf(" file=");
		quote(record->file_name, sizeof(record->file_name));
		putchar('\n');
	}
	return 0;
}

static int dump_ships(int argc, char **argv)
{
	(void)argv;
	if (argc != 0) {
		return 2;
	}
	if (!print_header("ships", "frontres\\frntspec.lst")) {
		return 1;
	}
	ship_list_load();
	printf("count %d\n", g_ship_count);
	for (int i = 0; g_ship_list != NULL && i < g_ship_count; ++i) {
		printf("ship %d model=", i);
		quote(g_ship_list[i].model_file_name,
		      sizeof(g_ship_list[i].model_file_name));
		printf(" type=%d\n", g_ship_list[i].craft_type);
	}
	int types = (int)(sizeof(g_ship_type_to_ship_list_index) /
			  sizeof(*g_ship_type_to_ship_list_index));
	printf("type_to_ship");
	for (int type = 0; type < types; ++type) {
		printf(" %d", g_ship_type_to_ship_list_index[type]);
	}
	putchar('\n');
	return 0;
}

static int dump_cutscenes(int argc, char **argv)
{
	if (argc != 1 || !print_header("cutscenes", argv[0])) {
		return argc != 1 ? 2 : 1;
	}
	int result = cutscene_load_table(argv[0]);
	printf("result %d\ncount %d\n", result, g_cutscene_count);
	for (int i = 0; g_cutscene_table != NULL && i < g_cutscene_count; ++i) {
		const struct cutscene_entry *entry = &g_cutscene_table[i];
		printf("cutscene %d movie=", i);
		quote(entry->movie_name, sizeof(entry->movie_name));
		printf(" campaign=%d after_debriefing=%d mission=%d thumbnail=",
		       entry->campaign_id, entry->play_after_debriefing,
		       entry->campaign_mission_id);
		quote(entry->thumbnail_sprite, sizeof(entry->thumbnail_sprite));
		printf(" description=");
		quote(entry->description, sizeof(entry->description));
		putchar('\n');
	}
	return 0;
}

/* Prints one award line: the record, the kind and the 16 sprite names. */
static void print_award_names(unsigned int record, const char *kind,
			      char names[16][32])
{
	printf("award %u %s", record, kind);
	for (int slot = 0; slot < 16; ++slot) {
		putchar(' ');
		quote(names[slot], sizeof(names[slot]));
	}
	putchar('\n');
}

static int dump_awards(int argc, char **argv)
{
	if (argc != 1 || !print_header("awards", argv[0])) {
		return argc != 1 ? 2 : 1;
	}
	int result = pilot_record_load_campaign_award_sprite_table(argv[0]);
	printf("result %d\ncount %u\n", result, g_campaign_award_sprite_count);
	for (unsigned int i = 0; g_campaign_award_sprites != NULL &&
				 i < g_campaign_award_sprite_count;
	     ++i) {
		struct campaign_award_sprite_entry *entry =
			&g_campaign_award_sprites[i];
		printf("award %u campaign=%d main=", i, entry->campaign_id);
		quote(entry->main_award_sprite_name,
		      sizeof(entry->main_award_sprite_name));
		putchar('\n');
		print_award_names(
			i, "multiplayer",
			entry->multiplayer_mission_award_sprite_names);
		print_award_names(
			i, "singleplayer",
			entry->singleplayer_mission_award_sprite_names);
	}
	return 0;
}

int main(int argc, char **argv)
{
	static const struct {
		const char *kind;
		int (*dump)(int argc, char **argv);
	} kinds[] = {
		{"menu", dump_menu},	 {"sequence", dump_sequence},
		{"images", dump_images}, {"sounds", dump_sounds},
		{"ships", dump_ships},	 {"cutscenes", dump_cutscenes},
		{"awards", dump_awards},
	};
	if (argc < 3) {
		fprintf(stderr, "Usage: %s ROOT KIND [ARGUMENTS]\n", argv[0]);
		return 2;
	}
	int status = 2;
	for (size_t i = 0; i < sizeof(kinds) / sizeof(*kinds); ++i) {
		if (strcmp(argv[2], kinds[i].kind) != 0) {
			continue;
		}
		xvt_log_set_level(AERON_LOG_DEBUG);
		SDL_SetLogPriorities(SDL_LOG_PRIORITY_VERBOSE);
		if (!bind_root(argv[1])) {
			return 1;
		}
		status = kinds[i].dump(argc - 3, argv + 3);
		break;
	}
	if (status == 2) {
		fprintf(stderr, "Usage: %s ROOT KIND [ARGUMENTS]\n", argv[0]);
	}
	if (fflush(stdout) != 0) {
		return 1;
	}
	return status;
}
