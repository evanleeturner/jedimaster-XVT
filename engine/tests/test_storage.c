#define _POSIX_C_SOURCE 200809L
/* Checks where the game's files are placed and found (xvt_runtime/storage/storage.h) against the promises
 * in its header. Each case starts from a fresh temporary folder holding three empty folders, asset, user
 * and temp, which an Aeron VFS bound to storage uses as the ASSET, USER and TEMP roots. The test writes
 * every file it reads there, and inspects the folders directly to see where storage put a file.
 *
 * Not run here: xvt_storage_fatal, which shows a message box before it exits. */
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "test_assert.h"
#include "test_temp_folder.h"
#include "xvt_runtime/storage/storage.h"

static char g_folder[XVT_TEST_PATH_CAPACITY];
static char g_asset[XVT_TEST_PATH_CAPACITY];
static char g_user[XVT_TEST_PATH_CAPACITY];
static char g_temp[XVT_TEST_PATH_CAPACITY];
static AeronVfs *g_vfs;

/* Unbinds and destroys the case's VFS and removes its folder. */
static void end_roots(void)
{
	if (!g_vfs) {
		return;
	}
	xvt_storage_bind(NULL);
	AeronVfs_Destroy(g_vfs);
	g_vfs = NULL;
	xvt_test_remove_tree(g_folder);
}

/* Starts a case: empty ASSET, USER and TEMP roots in a fresh folder, bound to storage. */
static void fresh_roots(void)
{
	end_roots();
	xvt_test_make_folder(g_folder);
	xvt_test_make_subfolder(g_folder, "asset");
	xvt_test_make_subfolder(g_folder, "user");
	xvt_test_make_subfolder(g_folder, "temp");
	xvt_test_join(g_asset, g_folder, "asset");
	xvt_test_join(g_user, g_folder, "user");
	xvt_test_join(g_temp, g_folder, "temp");
	AeronVfsConfig config = {0};
	config.asset_root = g_asset;
	config.resource_root = g_asset;
	config.user_root = g_user;
	config.temp_root = g_temp;
	g_vfs = AeronVfs_Create(&config);
	XVT_ASSERT_TRUE(g_vfs != NULL);
	xvt_storage_bind(g_vfs);
}

/* Returns 1 when the file that Open gives path in mode holds exactly text. */
static int opens_text(const char *path, const char *mode, const char *text)
{
	AeronFile *file = xvt_storage_open(path, mode);
	if (!file) {
		return 0;
	}
	char buffer[64];
	size_t got = 0;
	AeronVfs_Read(file, buffer, sizeof buffer, &got);
	AeronVfs_Close(file);
	return got == strlen(text) && memcmp(buffer, text, got) == 0;
}

/* Opens path with Open in mode, which must succeed, and closes it. */
static void open_and_close(const char *path, const char *mode)
{
	AeronFile *file = xvt_storage_open(path, mode);
	XVT_ASSERT_TRUE(file != NULL);
	XVT_ASSERT_TRUE(AeronVfs_Close(file));
}

static void check_bind(void)
{
	fresh_roots();
	XVT_ASSERT_TRUE(xvt_storage_vfs() == g_vfs);
	xvt_test_write_text(g_asset, "a.dat", "a");
	XVT_ASSERT_TRUE(opens_text("a.dat", "rb", "a"));
	XVT_ASSERT_TRUE(xvt_storage_last_path()[0] != 0);

	/* Binding clears the last path. */
	xvt_storage_bind(g_vfs);
	XVT_ASSERT_INT_EQ(strcmp(xvt_storage_last_path(), ""), 0);

	/* With no VFS bound, Probe and the Open calls fail. */
	xvt_storage_bind(NULL);
	XVT_ASSERT_TRUE(xvt_storage_vfs() == NULL);
	XVT_ASSERT_INT_EQ(xvt_storage_probe(AERON_VFS_ROOT_ASSET, "a.dat"), -1);
	XVT_ASSERT_TRUE(xvt_storage_open("a.dat", "rb") == NULL);
	XVT_ASSERT_TRUE(xvt_storage_open_root(AERON_VFS_ROOT_ASSET, "a.dat",
					      "rb") == NULL);
	xvt_storage_bind(g_vfs);
	end_roots();
}

static void check_normalize(void)
{
	char out[64];
	XVT_ASSERT_INT_EQ(
		xvt_storage_normalize("a\\b/./c//d/", out, sizeof out), 1);
	XVT_ASSERT_INT_EQ(strcmp(out, "a/b/c/d"), 0);
	XVT_ASSERT_INT_EQ(xvt_storage_normalize("./x", out, sizeof out), 1);
	XVT_ASSERT_INT_EQ(strcmp(out, "x"), 0);
	/* A part that only contains two dots is refused; a longer name that contains them is not. */
	XVT_ASSERT_INT_EQ(xvt_storage_normalize("a..b/..c", out, sizeof out),
			  1);
	XVT_ASSERT_INT_EQ(strcmp(out, "a..b/..c"), 0);

	XVT_ASSERT_INT_EQ(xvt_storage_normalize(NULL, out, sizeof out), 0);
	XVT_ASSERT_INT_EQ(xvt_storage_normalize("/a", out, sizeof out), 0);
	XVT_ASSERT_INT_EQ(xvt_storage_normalize("\\a", out, sizeof out), 0);
	XVT_ASSERT_INT_EQ(xvt_storage_normalize("c:a", out, sizeof out), 0);
	XVT_ASSERT_INT_EQ(xvt_storage_normalize("a/b:c", out, sizeof out), 0);
	XVT_ASSERT_INT_EQ(xvt_storage_normalize("a/../b", out, sizeof out), 0);
	XVT_ASSERT_INT_EQ(xvt_storage_normalize("a\\..", out, sizeof out), 0);
	XVT_ASSERT_INT_EQ(xvt_storage_normalize("", out, sizeof out), 0);
	XVT_ASSERT_INT_EQ(xvt_storage_normalize("././/", out, sizeof out), 0);

	/* "abc/de" is six characters: it fits in seven bytes with its terminator, not in six. */
	XVT_ASSERT_INT_EQ(xvt_storage_normalize("abc\\de", out, 7), 1);
	XVT_ASSERT_INT_EQ(strcmp(out, "abc/de"), 0);
	XVT_ASSERT_INT_EQ(xvt_storage_normalize("abc\\de", out, 6), 0);
}

static void check_probe(void)
{
	fresh_roots();
	xvt_test_make_subfolder(g_user, "pilots");
	xvt_test_make_subfolder(g_user, "pilots/sub");
	xvt_test_write_text(g_user, "pilots/Ace.PLT", "p");
	xvt_test_write_text(g_user, "flat.txt", "f");

	XVT_ASSERT_INT_EQ(
		xvt_storage_probe(AERON_VFS_ROOT_USER, "pilots/Ace.PLT"), 1);
	XVT_ASSERT_INT_EQ(
		xvt_storage_probe(AERON_VFS_ROOT_USER, "pilots\\Ace.PLT"), 1);

	/* Proven absent: the parent lists no such name, or an ancestor is proven missing. */
	XVT_ASSERT_INT_EQ(
		xvt_storage_probe(AERON_VFS_ROOT_USER, "pilots/none.plt"), 0);
	XVT_ASSERT_INT_EQ(xvt_storage_probe(AERON_VFS_ROOT_USER, "top.plt"), 0);
	XVT_ASSERT_INT_EQ(
		xvt_storage_probe(AERON_VFS_ROOT_USER, "missing/deeper/x.plt"),
		0);
	XVT_ASSERT_INT_EQ(
		xvt_storage_probe(AERON_VFS_ROOT_ASSET, "pilots/Ace.PLT"), 0);

	/* Not proven either way: a folder, a rejected path, a parent that is a file. */
	XVT_ASSERT_INT_EQ(xvt_storage_probe(AERON_VFS_ROOT_USER, "pilots/sub"),
			  -1);
	XVT_ASSERT_INT_EQ(xvt_storage_probe(AERON_VFS_ROOT_USER, "../x.plt"),
			  -1);
	XVT_ASSERT_INT_EQ(xvt_storage_probe(AERON_VFS_ROOT_USER, "flat.txt/x"),
			  -1);

	/* A name the parent lists in other letter case: where the file system tells case apart, the exact
	 * lookup misses it and the answer is -1; where it does not, the lookup finds the file. */
	char other[XVT_TEST_PATH_CAPACITY];
	xvt_test_join(other, g_user, "pilots/ace.plt");
	struct stat info;
	int case_blind = stat(other, &info) == 0;
	XVT_ASSERT_INT_EQ(
		xvt_storage_probe(AERON_VFS_ROOT_USER, "pilots/ace.plt"),
		case_blind ? 1 : -1);
	end_roots();
}

static void check_resolve_asset(void)
{
	fresh_roots();
	xvt_test_make_subfolder(g_asset, "BalanceOfPower");
	xvt_test_write_text(g_asset, "BalanceOfPower/both.dat", "bop");
	xvt_test_write_text(g_asset, "both.dat", "plain");
	xvt_test_write_text(g_asset, "plain.dat", "plain");
	xvt_test_write_text(g_user, "user.dat", "user");

	char resolved[XVT_PATH_CAPACITY];
	XVT_ASSERT_INT_EQ(xvt_storage_resolve_asset("both.dat", resolved,
						    sizeof resolved),
			  1);
	XVT_ASSERT_INT_EQ(strcmp(resolved, "BalanceOfPower/both.dat"), 0);
	XVT_ASSERT_INT_EQ(xvt_storage_resolve_asset("plain.dat", resolved,
						    sizeof resolved),
			  1);
	XVT_ASSERT_INT_EQ(strcmp(resolved, "plain.dat"), 0);

	XVT_ASSERT_INT_EQ(xvt_storage_resolve_asset("none.dat", resolved,
						    sizeof resolved),
			  0);
	XVT_ASSERT_INT_EQ(xvt_storage_resolve_asset("user.dat", resolved,
						    sizeof resolved),
			  0);
	XVT_ASSERT_INT_EQ(xvt_storage_resolve_asset("../none.dat", resolved,
						    sizeof resolved),
			  -1);
	XVT_ASSERT_INT_EQ(xvt_storage_resolve_asset("plain.dat/inner", resolved,
						    sizeof resolved),
			  -1);
	end_roots();
}

static void check_open_temp(void)
{
	fresh_roots();
	/* .tmp and .tmt, in any letter case, go to TEMP for writes and reads alike. */
	open_and_close("work.tmp", "wb");
	XVT_ASSERT_INT_EQ(xvt_test_kind(g_temp, "work.tmp"), 1);
	XVT_ASSERT_INT_EQ(xvt_test_kind(g_user, "work.tmp"), 0);
	open_and_close("WORK.TMT", "wb");
	XVT_ASSERT_INT_EQ(xvt_test_kind(g_temp, "WORK.TMT"), 1);

	xvt_test_write_text(g_temp, "read.tmp", "temp");
	xvt_test_write_text(g_asset, "read.tmp", "asset");
	XVT_ASSERT_TRUE(opens_text("read.tmp", "rb", "temp"));
	end_roots();
}

static void check_open_cache(void)
{
	fresh_roots();
	/* Every cache extension, in any letter case, writes to USER under cache/, which is created. */
	static const char *const names[] = {"a.pal", "b.act", "c.inv",
					    "d.bin", "e.plo", "F.PAL"};
	for (size_t i = 0; i < sizeof names / sizeof names[0]; ++i) {
		char placed[64];
		snprintf(placed, sizeof placed, "cache/%s", names[i]);
		open_and_close(names[i], "wb");
		XVT_ASSERT_INT_EQ(xvt_test_kind(g_user, placed), 1);
		XVT_ASSERT_INT_EQ(xvt_test_kind(g_user, names[i]), 0);
	}

	/* A read finds the cache copy first, and falls back to ASSET when there is none. */
	xvt_test_write_text(g_user, "cache/both.inv", "cache");
	xvt_test_write_text(g_asset, "both.inv", "asset");
	XVT_ASSERT_TRUE(opens_text("both.inv", "rb", "cache"));
	xvt_test_make_subfolder(g_asset, "BalanceOfPower");
	xvt_test_write_text(g_asset, "BalanceOfPower/only.act", "asset");
	XVT_ASSERT_TRUE(opens_text("only.act", "rb", "asset"));
	end_roots();
}

static void check_open_user(void)
{
	fresh_roots();
	/* .plt and .pl2 reads come from USER, never ASSET. */
	xvt_test_write_text(g_user, "p.plt", "user");
	xvt_test_write_text(g_asset, "p.plt", "asset");
	XVT_ASSERT_TRUE(opens_text("p.plt", "rb", "user"));
	xvt_test_write_text(g_asset, "q.pl2", "asset");
	XVT_ASSERT_TRUE(xvt_storage_open("q.pl2", "rb") == NULL);

	/* Any write goes to USER, creating missing folders; "r+" counts as a write. */
	open_and_close("save\\game.sav", "wb");
	XVT_ASSERT_INT_EQ(xvt_test_kind(g_user, "save/game.sav"), 1);
	XVT_ASSERT_INT_EQ(xvt_test_kind(g_asset, "save"), 0);
	xvt_test_write_text(g_user, "r.dat", "user");
	xvt_test_write_text(g_asset, "r.dat", "asset");
	XVT_ASSERT_TRUE(opens_text("r.dat", "r+b", "user"));
	end_roots();
}

static void check_open_asset(void)
{
	fresh_roots();
	/* Any other read: ASSET's BalanceOfPower/<path> first, then <path>; USER is not read. */
	xvt_test_make_subfolder(g_asset, "BalanceOfPower");
	xvt_test_write_text(g_asset, "BalanceOfPower/o.dat", "bop");
	xvt_test_write_text(g_asset, "o.dat", "plain");
	xvt_test_write_text(g_asset, "p.dat", "plain");
	xvt_test_write_text(g_user, "u.dat", "user");
	XVT_ASSERT_TRUE(opens_text("o.dat", "rb", "bop"));
	XVT_ASSERT_TRUE(opens_text("p.dat", "r", "plain"));
	XVT_ASSERT_TRUE(xvt_storage_open("u.dat", "rb") == NULL);

	/* Refusals: no mode, a mode that does not start with r, w or a, a rejected path, a missing file. */
	XVT_ASSERT_TRUE(xvt_storage_open("p.dat", NULL) == NULL);
	XVT_ASSERT_TRUE(xvt_storage_open("p.dat", "x") == NULL);
	XVT_ASSERT_TRUE(xvt_storage_open("p.dat", "") == NULL);
	XVT_ASSERT_TRUE(xvt_storage_open("../p.dat", "rb") == NULL);
	XVT_ASSERT_TRUE(xvt_storage_open("/p.dat", "rb") == NULL);
	XVT_ASSERT_TRUE(xvt_storage_open("missing.dat", "rb") == NULL);
	end_roots();
}

static void check_open_root(void)
{
	fresh_roots();
	/* A mode that writes is refused outside USER and TEMP, and creates nothing. */
	XVT_ASSERT_TRUE(xvt_storage_open_root(AERON_VFS_ROOT_ASSET, "w.dat",
					      "wb") == NULL);
	XVT_ASSERT_TRUE(xvt_storage_open_root(AERON_VFS_ROOT_ASSET, "w.dat",
					      "ab") == NULL);
	XVT_ASSERT_INT_EQ(xvt_test_kind(g_asset, "w.dat"), 0);
	xvt_test_write_text(g_asset, "w.dat", "asset");
	XVT_ASSERT_TRUE(xvt_storage_open_root(AERON_VFS_ROOT_ASSET, "w.dat",
					      "r+b") == NULL);

	/* A write creates missing parent folders. */
	AeronFile *file =
		xvt_storage_open_root(AERON_VFS_ROOT_TEMP, "a/b/c.dat", "wb");
	XVT_ASSERT_TRUE(file != NULL);
	XVT_ASSERT_TRUE(AeronVfs_Close(file));
	XVT_ASSERT_INT_EQ(xvt_test_kind(g_temp, "a/b/c.dat"), 1);

	/* A read refuses a folder. */
	XVT_ASSERT_TRUE(xvt_storage_open_root(AERON_VFS_ROOT_TEMP, "a/b",
					      "rb") == NULL);

	/* "a+" opens for appending only: what it writes goes to the end, and it reads nothing. */
	xvt_test_write_text(g_user, "log.txt", "abc");
	file = xvt_storage_open_root(AERON_VFS_ROOT_USER, "log.txt", "a+");
	XVT_ASSERT_TRUE(file != NULL);
	XVT_ASSERT_TRUE(AeronVfs_Write(file, "def", 3, NULL));
	XVT_ASSERT_TRUE(AeronVfs_Seek(file, 0, 0));
	char buffer[8];
	size_t got = 99;
	AeronVfs_Read(file, buffer, 3, &got);
	XVT_ASSERT_INT_EQ(got, 0);
	AeronVfs_Close(file);
	XVT_ASSERT_TRUE(xvt_test_file_is(g_user, "log.txt", "abcdef"));

	/* Refusals: a bad mode, a rejected path. */
	XVT_ASSERT_TRUE(xvt_storage_open_root(AERON_VFS_ROOT_USER, "log.txt",
					      "z") == NULL);
	XVT_ASSERT_TRUE(xvt_storage_open_root(AERON_VFS_ROOT_USER, "log.txt",
					      NULL) == NULL);
	XVT_ASSERT_TRUE(xvt_storage_open_root(AERON_VFS_ROOT_USER, "../log.txt",
					      "rb") == NULL);
	end_roots();
}

static void check_write_atomic(void)
{
	fresh_roots();
	XVT_ASSERT_INT_EQ(xvt_storage_write_atomic("state.dat", "first", 5), 1);
	XVT_ASSERT_TRUE(xvt_test_file_is(g_user, "state.dat", "first"));
	XVT_ASSERT_INT_EQ(xvt_storage_write_atomic("state.dat", "2nd", 3), 1);
	XVT_ASSERT_TRUE(xvt_test_file_is(g_user, "state.dat", "2nd"));
	XVT_ASSERT_INT_EQ(xvt_test_kind(g_user, "state.dat.tmp"), 0);

	/* Placed as Open places a write: a cache name in USER's cache/, a .tmp name in TEMP. */
	xvt_test_make_subfolder(g_user, "cache");
	XVT_ASSERT_INT_EQ(xvt_storage_write_atomic("look.pal", "p", 1), 1);
	XVT_ASSERT_TRUE(xvt_test_file_is(g_user, "cache/look.pal", "p"));
	XVT_ASSERT_INT_EQ(xvt_storage_write_atomic("scratch.tmp", "t", 1), 1);
	XVT_ASSERT_TRUE(xvt_test_file_is(g_temp, "scratch.tmp", "t"));

	XVT_ASSERT_INT_EQ(xvt_storage_write_atomic("../escape.dat", "x", 1), 0);
	XVT_ASSERT_INT_EQ(xvt_test_kind(g_folder, "escape.dat"), 0);
	end_roots();
}

static void check_remove(void)
{
	fresh_roots();
	xvt_test_write_text(g_user, "old.sav", "x");
	XVT_ASSERT_INT_EQ(xvt_storage_remove("old.sav"), 0);
	XVT_ASSERT_INT_EQ(xvt_test_kind(g_user, "old.sav"), 0);

	/* A cache file goes from USER's cache/, never from ASSET. */
	xvt_test_make_subfolder(g_user, "cache");
	xvt_test_write_text(g_user, "cache/c.bin", "x");
	xvt_test_write_text(g_asset, "c.bin", "x");
	XVT_ASSERT_INT_EQ(xvt_storage_remove("c.bin"), 0);
	XVT_ASSERT_INT_EQ(xvt_test_kind(g_user, "cache/c.bin"), 0);
	XVT_ASSERT_INT_EQ(xvt_test_kind(g_asset, "c.bin"), 1);

	/* A .tmp file goes from TEMP. */
	xvt_test_write_text(g_temp, "t.tmp", "x");
	XVT_ASSERT_INT_EQ(xvt_storage_remove("t.tmp"), 0);
	XVT_ASSERT_INT_EQ(xvt_test_kind(g_temp, "t.tmp"), 0);

	/* An empty folder goes; one that holds a file stays. */
	xvt_test_make_subfolder(g_user, "empty");
	XVT_ASSERT_INT_EQ(xvt_storage_remove("empty"), 0);
	XVT_ASSERT_INT_EQ(xvt_test_kind(g_user, "empty"), 0);
	xvt_test_make_subfolder(g_user, "full");
	xvt_test_write_text(g_user, "full/f.txt", "x");
	XVT_ASSERT_INT_EQ(xvt_storage_remove("full"), -1);
	XVT_ASSERT_INT_EQ(xvt_test_kind(g_user, "full/f.txt"), 1);

	XVT_ASSERT_INT_EQ(xvt_storage_remove("../user/full/f.txt"), -1);
	XVT_ASSERT_INT_EQ(xvt_test_kind(g_user, "full/f.txt"), 1);
	end_roots();
}

static void check_rename(void)
{
	fresh_roots();
	xvt_test_write_text(g_user, "a.sav", "a");
	XVT_ASSERT_INT_EQ(xvt_storage_rename("a.sav", "b.sav"), 0);
	XVT_ASSERT_INT_EQ(xvt_test_kind(g_user, "a.sav"), 0);
	XVT_ASSERT_TRUE(xvt_test_file_is(g_user, "b.sav", "a"));

	/* USER only, without cache/: a cache name is renamed where it is given. */
	xvt_test_write_text(g_user, "x.pal", "p");
	XVT_ASSERT_INT_EQ(xvt_storage_rename("x.pal", "y.pal"), 0);
	XVT_ASSERT_INT_EQ(xvt_test_kind(g_user, "y.pal"), 1);
	XVT_ASSERT_INT_EQ(xvt_test_kind(g_user, "cache"), 0);

	/* A .tmp file is looked for in USER, so one that is only in TEMP is not renamed. */
	xvt_test_write_text(g_temp, "w.tmp", "t");
	XVT_ASSERT_INT_EQ(xvt_storage_rename("w.tmp", "v.tmp"), -1);
	XVT_ASSERT_INT_EQ(xvt_test_kind(g_temp, "w.tmp"), 1);

	XVT_ASSERT_INT_EQ(xvt_storage_rename("../user/b.sav", "c.sav"), -1);
	XVT_ASSERT_INT_EQ(xvt_storage_rename("b.sav", "../c.sav"), -1);
	XVT_ASSERT_INT_EQ(xvt_test_kind(g_user, "b.sav"), 1);
	end_roots();
}

struct glob_seen {
	int count;
	int stop_after;
	char names[8][32];
};

static int collect(void *context, const AeronVfsEntry *entry)
{
	struct glob_seen *seen = context;
	if (seen->count < 8) {
		snprintf(seen->names[seen->count], sizeof seen->names[0], "%s",
			 entry->name);
	}
	++seen->count;
	return seen->stop_after == 0 || seen->count < seen->stop_after;
}

static int saw(const struct glob_seen *seen, const char *name)
{
	for (int i = 0; i < seen->count && i < 8; ++i) {
		if (!strcmp(seen->names[i], name)) {
			return 1;
		}
	}
	return 0;
}

static void check_glob(void)
{
	fresh_roots();
	xvt_test_make_subfolder(g_user, "pilots");
	xvt_test_make_subfolder(g_user, "pilots/dir.plt");
	xvt_test_write_text(g_user, "pilots/Ace.PLT", "x");
	xvt_test_write_text(g_user, "pilots/bob.plt", "x");
	xvt_test_write_text(g_user, "pilots/notes.txt", "x");
	xvt_test_write_text(g_user, "top.plt", "x");
	xvt_test_make_subfolder(g_asset, "pilots");
	xvt_test_write_text(g_asset, "pilots/carl.plt", "x");

	/* Files only, in the named folder of the named root, matched in any letter case. */
	struct glob_seen seen = {0};
	XVT_ASSERT_TRUE(xvt_storage_glob(AERON_VFS_ROOT_USER, "pilots/*.plt",
					 collect, &seen) != 0);
	XVT_ASSERT_INT_EQ(seen.count, 2);
	XVT_ASSERT_TRUE(saw(&seen, "Ace.PLT"));
	XVT_ASSERT_TRUE(saw(&seen, "bob.plt"));

	/* A wildcard with no folder part lists the root itself. */
	memset(&seen, 0, sizeof seen);
	XVT_ASSERT_TRUE(xvt_storage_glob(AERON_VFS_ROOT_USER, "*.PLT", collect,
					 &seen) != 0);
	XVT_ASSERT_INT_EQ(seen.count, 1);
	XVT_ASSERT_TRUE(saw(&seen, "top.plt"));

	/* A callback that returns 0 stops the listing, and Glob returns 0. */
	memset(&seen, 0, sizeof seen);
	seen.stop_after = 1;
	XVT_ASSERT_INT_EQ(xvt_storage_glob(AERON_VFS_ROOT_USER, "pilots/*.plt",
					   collect, &seen),
			  0);
	XVT_ASSERT_INT_EQ(seen.count, 1);

	/* A rejected wildcard lists nothing. */
	memset(&seen, 0, sizeof seen);
	XVT_ASSERT_INT_EQ(xvt_storage_glob(AERON_VFS_ROOT_USER, "../user/*.plt",
					   collect, &seen),
			  0);
	XVT_ASSERT_INT_EQ(seen.count, 0);
	end_roots();
}

/* Opens path with Open in mode, remembers it as the global stream, and returns it. */
static AeronFile *open_global(const char *path, const char *mode)
{
	AeronFile *file = xvt_storage_open(path, mode);
	XVT_ASSERT_TRUE(file != NULL);
	xvt_storage_capture_global_stream();
	return file;
}

/* Sets the stream's error flag by reading from a stream open only for writing. */
static void fail_read(AeronFile *file)
{
	char byte;
	AeronVfs_Read(file, &byte, 1, NULL);
	XVT_ASSERT_TRUE(AeronVfs_HasError(file));
}

static void check_global_stream(void)
{
	fresh_roots();
	XVT_ASSERT_INT_EQ(xvt_storage_close_global_stream(NULL, 1), 0);

	/* A clean stream closes with 0 and keeps its file. */
	AeronFile *file = open_global("clean.sav", "wb");
	XVT_ASSERT_TRUE(AeronVfs_Write(file, "ok", 2, NULL));
	XVT_ASSERT_INT_EQ(xvt_storage_close_global_stream(file, 1), 0);
	XVT_ASSERT_TRUE(xvt_test_file_is(g_user, "clean.sav", "ok"));

	/* A stream with its error flag set returns 1; with remove_on_error its USER or TEMP file goes. */
	file = open_global("bad.sav", "wb");
	fail_read(file);
	XVT_ASSERT_INT_EQ(xvt_storage_close_global_stream(file, 1), 1);
	XVT_ASSERT_INT_EQ(xvt_test_kind(g_user, "bad.sav"), 0);
	file = open_global("bad.tmp", "wb");
	fail_read(file);
	XVT_ASSERT_INT_EQ(xvt_storage_close_global_stream(file, 1), 1);
	XVT_ASSERT_INT_EQ(xvt_test_kind(g_temp, "bad.tmp"), 0);

	/* Without remove_on_error the file stays. */
	file = open_global("kept.sav", "wb");
	fail_read(file);
	XVT_ASSERT_INT_EQ(xvt_storage_close_global_stream(file, 0), 1);
	XVT_ASSERT_INT_EQ(xvt_test_kind(g_user, "kept.sav"), 1);

	/* A file in ASSET is never removed. */
	xvt_test_write_text(g_asset, "asset.dat", "a");
	file = open_global("asset.dat", "rb");
	XVT_ASSERT_TRUE(!AeronVfs_Write(file, "x", 1, NULL));
	XVT_ASSERT_TRUE(AeronVfs_HasError(file));
	XVT_ASSERT_INT_EQ(xvt_storage_close_global_stream(file, 1), 1);
	XVT_ASSERT_INT_EQ(xvt_test_kind(g_asset, "asset.dat"), 1);
	end_roots();
}

static void check_last_path(void)
{
	fresh_roots();
	XVT_ASSERT_INT_EQ(strcmp(xvt_storage_last_path(), ""), 0);

	/* A read that finds nothing tries ASSET's <path> last. */
	XVT_ASSERT_TRUE(xvt_storage_open("dir\\Gone.dat", "rb") == NULL);
	XVT_ASSERT_INT_EQ(strcmp(xvt_storage_last_path(), "dir/Gone.dat"), 0);
	XVT_ASSERT_INT_EQ(xvt_storage_last_root(), AERON_VFS_ROOT_ASSET);

	/* A write: the place it opened. */
	open_and_close("save/one.sav", "wb");
	XVT_ASSERT_INT_EQ(strcmp(xvt_storage_last_path(), "save/one.sav"), 0);
	XVT_ASSERT_INT_EQ(xvt_storage_last_root(), AERON_VFS_ROOT_USER);
	open_and_close("c.pal", "wb");
	XVT_ASSERT_INT_EQ(strcmp(xvt_storage_last_path(), "cache/c.pal"), 0);
	XVT_ASSERT_INT_EQ(xvt_storage_last_root(), AERON_VFS_ROOT_USER);
	open_and_close("t.tmp", "wb");
	XVT_ASSERT_INT_EQ(strcmp(xvt_storage_last_path(), "t.tmp"), 0);
	XVT_ASSERT_INT_EQ(xvt_storage_last_root(), AERON_VFS_ROOT_TEMP);

	/* A path Open rejects stays as given. */
	XVT_ASSERT_TRUE(xvt_storage_open("../up.dat", "rb") == NULL);
	XVT_ASSERT_INT_EQ(strcmp(xvt_storage_last_path(), "../up.dat"), 0);

	/* OpenRoot and ResolveAsset: the last place they try. */
	AeronFile *file =
		xvt_storage_open_root(AERON_VFS_ROOT_TEMP, "x\\y.dat", "wb");
	XVT_ASSERT_TRUE(file != NULL);
	AeronVfs_Close(file);
	XVT_ASSERT_INT_EQ(strcmp(xvt_storage_last_path(), "x/y.dat"), 0);
	XVT_ASSERT_INT_EQ(xvt_storage_last_root(), AERON_VFS_ROOT_TEMP);
	xvt_test_make_subfolder(g_asset, "BalanceOfPower");
	xvt_test_write_text(g_asset, "BalanceOfPower/r.dat", "r");
	char resolved[XVT_PATH_CAPACITY];
	XVT_ASSERT_INT_EQ(
		xvt_storage_resolve_asset("r.dat", resolved, sizeof resolved),
		1);
	XVT_ASSERT_INT_EQ(
		strcmp(xvt_storage_last_path(), "BalanceOfPower/r.dat"), 0);
	XVT_ASSERT_INT_EQ(xvt_storage_last_root(), AERON_VFS_ROOT_ASSET);
	end_roots();
}

int main(void)
{
	check_bind();
	check_normalize();
	check_probe();
	check_resolve_asset();
	check_open_temp();
	check_open_cache();
	check_open_user();
	check_open_asset();
	check_open_root();
	check_write_atomic();
	check_remove();
	check_rename();
	check_glob();
	check_global_stream();
	check_last_path();
	return 0;
}
