/* Prints what the engine keeps when it loads a pilot, so that another reader
 * of the same files can be checked against the engine. Build with
 * -DXVT_BUILD_TOOLS=ON; it links the engine library.
 *
 * Usage: pilot_dump ROOT layout
 *        pilot_dump ROOT load DIR STEM
 *        pilot_dump ROOT raw FILE
 *        pilot_dump ROOT pattern DIR STEM SEED
 *
 * ROOT is bound as the asset folder, as the game's install is: a game name
 * resolves under ROOT/BalanceOfPower first, then under ROOT, in any letter
 * case. Pilot files live in the user folder, a fresh one under /tmp. The
 * kinds:
 *
 *   layout   every struct a pilot record is made of, its size, and each
 *            member's offset, type, dimensions and size.
 *   load     copies DIR/STEM.pl2 and DIR/STEM.plt, whichever exist, into the
 *            user folder, loads the menus' string table as the front end's
 *            start does, then pilot_load_from_path on STEM.plt, and prints
 *            its result and the pilot record it filled.
 *   raw      prints FILE as the base game's pilot record, read whole, with
 *            no conversion.
 *   pattern  fills the pilot record with bytes from a 32-bit xorshift
 *            generator started at SEED (the low byte of each step), names it
 *            STEM, and writes DIR/STEM.pl2 as the engine saves it and
 *            DIR/STEM.plt with pilot_write_xvt_record.
 *
 * A record prints one line per member that holds values, "PATH = VALUES":
 * PATH joins struct members with "." and indexes arrays of structs with
 * "[i]"; a 32-bit member prints its values in decimal (signed for int, not
 * for unsigned), all elements of a many-dimensional array in a row, last
 * index fastest; a byte array prints as hex; a char array prints its text
 * quoted up to its first byte 0 and then "hex=" and all its bytes. An
 * element of an array of structs whose bytes are all 0 is left out.
 *
 * The engine's log runs at DEBUG and goes to stderr, as SDL writes it. Exit
 * status: 0 when the sheet printed; 1 when a file cannot be read or written
 * or the engine's load fails; 2 for bad arguments. */
#include <SDL3/SDL_log.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "asset_dump.h"
#include "xvt/frontend/frontend_string.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/frontend/pilot.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt_runtime/log/log.h"

enum {
	PATH_TEXT = 256,
	FILE_PATH = 1024,
};

#define COUNT_OF(array) (sizeof(array) / sizeof((array)[0]))

/* One member of a pilot struct: its name, place, size in bytes (the whole
 * array), type ("i32", "u32", "u8", "char" or a struct's name), dimensions
 * ("-" or "[a][b]") and, for a struct type, that struct's row in g_layout. */
struct layout_member {
	const char *name;
	size_t offset;
	size_t size;
	const char *type;
	const char *dims;
	int sub;
};

struct layout_struct {
	const char *name;
	size_t size;
	const struct layout_member *members;
	size_t member_count;
};

#include "pilot_layout.inc"

/* Writes format's text into out, size bytes; ends the tool with status 1,
 * saying so, when it does not fit. */
static void format_path(char *out, size_t size, const char *format, ...)
	__attribute__((format(printf, 3, 4)));

static void format_path(char *out, size_t size, const char *format, ...)
{
	va_list args;
	va_start(args, format);
	int written = vsnprintf(out, size, format, args);
	va_end(args);
	if (written < 0 || (size_t)written >= size) {
		fprintf(stderr, "pilot_dump: a path is too long: %s\n", out);
		exit(1);
	}
}

/* Prints one member's values. */
static void print_leaf(const char *prefix, const char *field, const char *kind,
		       const void *values, int count)
{
	printf("%s%s =", prefix, field);
	const uint8_t *bytes = values;
	if (strcmp(kind, "char") == 0) {
		putchar(' ');
		asset_dump_quote((const char *)bytes, (size_t)count);
		printf(" hex=");
		for (int i = 0; i < count; ++i) {
			printf("%02x", bytes[i]);
		}
	} else if (strcmp(kind, "u8") == 0) {
		putchar(' ');
		for (int i = 0; i < count; ++i) {
			printf("%02x", bytes[i]);
		}
	} else {
		for (int i = 0; i < count; ++i) {
			uint32_t value;
			memcpy(&value, bytes + 4 * (size_t)i, sizeof(value));
			if (strcmp(kind, "u32") == 0) {
				printf(" %u", value);
			} else {
				printf(" %d", (int32_t)value);
			}
		}
	}
	putchar('\n');
}

static void print_record(const char *prefix, int layout, const uint8_t *base);

/* Prints one element of an array of structs, as PATH.member lines, unless all
 * its bytes are 0. */
static void print_record_element(const char *path, int layout,
				 const uint8_t *element)
{
	size_t size = g_layout[layout].size;
	size_t i = 0;
	while (i < size && element[i] == 0) {
		++i;
	}
	if (i == size) {
		return;
	}
	char prefix[PATH_TEXT];
	format_path(prefix, sizeof(prefix), "%s.", path);
	print_record(prefix, layout, element);
}

/* Prints a struct member: in place for one struct, element by element ("[i]",
 * counting through every dimension) for an array of structs. */
static void print_struct_member(const char *prefix,
				const struct layout_member *member,
				const uint8_t *at)
{
	char path[PATH_TEXT];
	if (strcmp(member->dims, "-") == 0) {
		format_path(path, sizeof(path), "%s%s.", prefix, member->name);
		print_record(path, member->sub, at);
		return;
	}
	size_t element_size = g_layout[member->sub].size;
	int count = (int)(member->size / element_size);
	for (int i = 0; i < count; ++i) {
		format_path(path, sizeof(path), "%s%s[%d]", prefix,
			    member->name, i);
		print_record_element(path, member->sub,
				     at + (size_t)i * element_size);
	}
}

/* Prints the struct of row layout held at base, one line per member. */
static void print_record(const char *prefix, int layout, const uint8_t *base)
{
	const struct layout_struct *record = &g_layout[layout];
	for (size_t m = 0; m < record->member_count; ++m) {
		const struct layout_member *member = &record->members[m];
		const uint8_t *at = base + member->offset;
		if (member->sub >= 0) {
			print_struct_member(prefix, member, at);
			continue;
		}
		int wide = strcmp(member->type, "i32") == 0 ||
			   strcmp(member->type, "u32") == 0;
		print_leaf(prefix, member->name, member->type, at,
			   (int)(wide ? member->size / 4 : member->size));
	}
}

/* Prints every struct's size and each member's offset, type, dimensions and
 * size. */
static void print_layouts(void)
{
	for (size_t s = 0; s < COUNT_OF(g_layout); ++s) {
		printf("struct %s size=%zu\n", g_layout[s].name,
		       g_layout[s].size);
		for (size_t m = 0; m < g_layout[s].member_count; ++m) {
			const struct layout_member *member =
				&g_layout[s].members[m];
			printf("member %s offset=%zu type=%s dims=%s size=%zu\n",
			       member->name, member->offset, member->type,
			       member->dims, member->size);
		}
	}
}

/* Copies the file at from to the path to, whole. Returns 0 when either fails. */
static int copy_file(const char *from, const char *to)
{
	FILE *in = fopen(from, "rb");
	if (in == NULL) {
		return 0;
	}
	FILE *out = fopen(to, "wb");
	if (out == NULL) {
		fclose(in);
		return 0;
	}
	char buffer[65536];
	size_t count;
	int ok = 1;
	while ((count = fread(buffer, 1, sizeof(buffer), in)) > 0) {
		if (fwrite(buffer, 1, count, out) != count) {
			ok = 0;
			break;
		}
	}
	ok = ok && !ferror(in);
	fclose(in);
	return fclose(out) == 0 && ok;
}

/* Copies dir/stem.extension into the user folder when it exists; returns 1
 * when it was there and copied, 0 when it is not there, -1 when the copy
 * fails. */
static int bring_in(const char *dir, const char *stem, const char *extension)
{
	char from[FILE_PATH];
	char to[FILE_PATH];
	format_path(from, sizeof(from), "%s/%s%s", dir, stem, extension);
	format_path(to, sizeof(to), "%s/%s%s", asset_dump_user_root(), stem,
		    extension);
	FILE *probe = fopen(from, "rb");
	if (probe == NULL) {
		return 0;
	}
	fclose(probe);
	return copy_file(from, to) ? 1 : -1;
}

static int dump_layout(void)
{
	printf("kind layout\n");
	print_layouts();
	return 0;
}

static int dump_load(const char *dir, const char *stem)
{
	int expansion = bring_in(dir, stem, ".pl2");
	int base = bring_in(dir, stem, ".plt");
	if (expansion < 0 || base < 0) {
		fprintf(stderr, "pilot_dump: cannot copy %s/%s\n", dir, stem);
		return 1;
	}
	frontend_string_load_table("fronttxt.txt");
	char path[FILE_PATH];
	format_path(path, sizeof(path), "%s.plt", stem);
	printf("kind load\nfiles pl2=%d plt=%d\n", expansion, base);
	int result = pilot_load_from_path(path);
	printf("result %d\n", result);
	print_record("", LAYOUT_PILOT_DATA, (const uint8_t *)&g_pilot_data);
	return result == 1 ? 0 : 1;
}

static int dump_raw(const char *file)
{
	static struct pilot_xvt_record record;
	FILE *in = fopen(file, "rb");
	if (in == NULL) {
		fprintf(stderr, "pilot_dump: cannot open %s\n", file);
		return 1;
	}
	size_t count = fread(&record, 1, sizeof(record), in);
	fclose(in);
	printf("kind raw\nbytes %zu of %zu\n", count, sizeof(record));
	if (count != sizeof(record)) {
		return 1;
	}
	print_record("", LAYOUT_PILOT_XVT_RECORD, (const uint8_t *)&record);
	return 0;
}

static int dump_pattern(const char *dir, const char *stem, uint32_t seed)
{
	uint8_t *bytes = (uint8_t *)&g_pilot_data;
	uint32_t state = seed != 0 ? seed : 1;
	for (size_t i = 0; i < sizeof(g_pilot_data); ++i) {
		state ^= state << 13;
		state ^= state >> 17;
		state ^= state << 5;
		bytes[i] = (uint8_t)state;
	}
	memset(g_pilot_data.name, 0, sizeof(g_pilot_data.name));
	snprintf(g_pilot_data.name, sizeof(g_pilot_data.name), "%s", stem);
	char to[FILE_PATH];
	format_path(to, sizeof(to), "%s/%s.pl2", dir, stem);
	FILE *out = fopen(to, "wb");
	if (out == NULL ||
	    fwrite(&g_pilot_data, 1, sizeof(g_pilot_data), out) !=
		    sizeof(g_pilot_data) ||
	    fclose(out) != 0) {
		fprintf(stderr, "pilot_dump: cannot write %s\n", to);
		return 1;
	}
	char name[FILE_PATH];
	format_path(name, sizeof(name), "%s.plt", stem);
	int result = pilot_write_xvt_record(name, NULL);
	char from[FILE_PATH];
	format_path(from, sizeof(from), "%s/%s", asset_dump_user_root(), name);
	format_path(to, sizeof(to), "%s/%s", dir, name);
	printf("kind pattern\nseed %u\nresult %d\n", seed, result);
	return result == 1 && copy_file(from, to) ? 0 : 1;
}

int main(int argc, char **argv)
{
	if (argc < 3) {
		fprintf(stderr, "Usage: %s ROOT KIND [ARGS]\n", argv[0]);
		return 2;
	}
	const char *kind = argv[2];
	int known = (strcmp(kind, "layout") == 0 && argc == 3) ||
		    (strcmp(kind, "load") == 0 && argc == 5) ||
		    (strcmp(kind, "raw") == 0 && argc == 4) ||
		    (strcmp(kind, "pattern") == 0 && argc == 6);
	if (!known) {
		fprintf(stderr, "Usage: %s ROOT KIND [ARGS]\n", argv[0]);
		return 2;
	}
	xvt_log_set_level(AERON_LOG_DEBUG);
	SDL_SetLogPriorities(SDL_LOG_PRIORITY_VERBOSE);
	if (!asset_dump_bind_root("pilot_dump", argv[1]) ||
	    !asset_dump_open_front_state()) {
		return 1;
	}
	int status;
	if (strcmp(kind, "layout") == 0) {
		status = dump_layout();
	} else if (strcmp(kind, "load") == 0) {
		status = dump_load(argv[3], argv[4]);
	} else if (strcmp(kind, "raw") == 0) {
		status = dump_raw(argv[3]);
	} else {
		status = dump_pattern(argv[3], argv[4],
				      (uint32_t)strtoul(argv[5], NULL, 10));
	}
	if (fflush(stdout) != 0) {
		return 1;
	}
	return status;
}
