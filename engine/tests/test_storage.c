#define _POSIX_C_SOURCE 200809L
/* Checks where the game's files are placed and found (xvt_runtime/storage/storage.h) against the promises
 * in its header. Each case starts from a fresh temporary folder holding three empty folders, asset, user
 * and temp, which an Aeron VFS bound to storage uses as the ASSET, USER and TEMP roots. The test writes
 * every file it reads there, and inspects the folders directly to see where storage put a file.
 *
 * Not run here: XvtStorage_Fatal, which shows a message box before it exits. */
#include "test_assert.h"
#include "test_temp_folder.h"
#include "xvt_runtime/storage/storage.h"

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static char g_folder[XVT_TEST_PATH_CAPACITY];
static char g_asset[XVT_TEST_PATH_CAPACITY];
static char g_user[XVT_TEST_PATH_CAPACITY];
static char g_temp[XVT_TEST_PATH_CAPACITY];
static AeronVfs *g_vfs;

/* Unbinds and destroys the case's VFS and removes its folder. */
static void EndRoots(void)
{
	if (!g_vfs) {
		return;
	}
	XvtStorage_Bind(NULL);
	AeronVfs_Destroy(g_vfs);
	g_vfs = NULL;
	XvtTest_RemoveTree(g_folder);
}

/* Starts a case: empty ASSET, USER and TEMP roots in a fresh folder, bound to storage. */
static void FreshRoots(void)
{
	EndRoots();
	XvtTest_MakeFolder(g_folder);
	XvtTest_MakeSubfolder(g_folder, "asset");
	XvtTest_MakeSubfolder(g_folder, "user");
	XvtTest_MakeSubfolder(g_folder, "temp");
	XvtTest_Join(g_asset, g_folder, "asset");
	XvtTest_Join(g_user, g_folder, "user");
	XvtTest_Join(g_temp, g_folder, "temp");
	AeronVfsConfig config = {0};
	config.asset_root = g_asset;
	config.resource_root = g_asset;
	config.user_root = g_user;
	config.temp_root = g_temp;
	g_vfs = AeronVfs_Create(&config);
	XVT_ASSERT_TRUE(g_vfs != NULL);
	XvtStorage_Bind(g_vfs);
}

/* Returns 1 when the file that Open gives path in mode holds exactly text. */
static int OpensText(const char *path, const char *mode, const char *text)
{
	char buffer[64];
	size_t got = 0;
	AeronFile *file = XvtStorage_Open(path, mode);
	if (!file) {
		return 0;
	}
	AeronVfs_Read(file, buffer, sizeof buffer, &got);
	AeronVfs_Close(file);
	return got == strlen(text) && memcmp(buffer, text, got) == 0;
}

/* Opens path with Open in mode, which must succeed, and closes it. */
static void OpenAndClose(const char *path, const char *mode)
{
	AeronFile *file = XvtStorage_Open(path, mode);
	XVT_ASSERT_TRUE(file != NULL);
	XVT_ASSERT_TRUE(AeronVfs_Close(file));
}

static void CheckBind(void)
{
	FreshRoots();
	XVT_ASSERT_TRUE(XvtStorage_Vfs() == g_vfs);
	XvtTest_WriteText(g_asset, "a.dat", "a");
	XVT_ASSERT_TRUE(OpensText("a.dat", "rb", "a"));
	XVT_ASSERT_TRUE(XvtStorage_LastPath()[0] != 0);

	/* Binding clears the last path. */
	XvtStorage_Bind(g_vfs);
	XVT_ASSERT_INT_EQ(strcmp(XvtStorage_LastPath(), ""), 0);

	/* With no VFS bound, Probe and the Open calls fail. */
	XvtStorage_Bind(NULL);
	XVT_ASSERT_TRUE(XvtStorage_Vfs() == NULL);
	XVT_ASSERT_INT_EQ(XvtStorage_Probe(AERON_VFS_ROOT_ASSET, "a.dat"), -1);
	XVT_ASSERT_TRUE(XvtStorage_Open("a.dat", "rb") == NULL);
	XVT_ASSERT_TRUE(XvtStorage_OpenRoot(AERON_VFS_ROOT_ASSET, "a.dat",
					    "rb") == NULL);
	XvtStorage_Bind(g_vfs);
	EndRoots();
}

static void CheckNormalize(void)
{
	char out[64];
	XVT_ASSERT_INT_EQ(XvtStorage_Normalize("a\\b/./c//d/", out, sizeof out),
			  1);
	XVT_ASSERT_INT_EQ(strcmp(out, "a/b/c/d"), 0);
	XVT_ASSERT_INT_EQ(XvtStorage_Normalize("./x", out, sizeof out), 1);
	XVT_ASSERT_INT_EQ(strcmp(out, "x"), 0);
	/* A part that only contains two dots is refused; a longer name that contains them is not. */
	XVT_ASSERT_INT_EQ(XvtStorage_Normalize("a..b/..c", out, sizeof out), 1);
	XVT_ASSERT_INT_EQ(strcmp(out, "a..b/..c"), 0);

	XVT_ASSERT_INT_EQ(XvtStorage_Normalize(NULL, out, sizeof out), 0);
	XVT_ASSERT_INT_EQ(XvtStorage_Normalize("/a", out, sizeof out), 0);
	XVT_ASSERT_INT_EQ(XvtStorage_Normalize("\\a", out, sizeof out), 0);
	XVT_ASSERT_INT_EQ(XvtStorage_Normalize("c:a", out, sizeof out), 0);
	XVT_ASSERT_INT_EQ(XvtStorage_Normalize("a/b:c", out, sizeof out), 0);
	XVT_ASSERT_INT_EQ(XvtStorage_Normalize("a/../b", out, sizeof out), 0);
	XVT_ASSERT_INT_EQ(XvtStorage_Normalize("a\\..", out, sizeof out), 0);
	XVT_ASSERT_INT_EQ(XvtStorage_Normalize("", out, sizeof out), 0);
	XVT_ASSERT_INT_EQ(XvtStorage_Normalize("././/", out, sizeof out), 0);

	/* "abc/de" is six characters: it fits in seven bytes with its terminator, not in six. */
	XVT_ASSERT_INT_EQ(XvtStorage_Normalize("abc\\de", out, 7), 1);
	XVT_ASSERT_INT_EQ(strcmp(out, "abc/de"), 0);
	XVT_ASSERT_INT_EQ(XvtStorage_Normalize("abc\\de", out, 6), 0);
}

static void CheckProbe(void)
{
	FreshRoots();
	XvtTest_MakeSubfolder(g_user, "pilots");
	XvtTest_MakeSubfolder(g_user, "pilots/sub");
	XvtTest_WriteText(g_user, "pilots/Ace.PLT", "p");
	XvtTest_WriteText(g_user, "flat.txt", "f");

	XVT_ASSERT_INT_EQ(
		XvtStorage_Probe(AERON_VFS_ROOT_USER, "pilots/Ace.PLT"), 1);
	XVT_ASSERT_INT_EQ(
		XvtStorage_Probe(AERON_VFS_ROOT_USER, "pilots\\Ace.PLT"), 1);

	/* Proven absent: the parent lists no such name, or an ancestor is proven missing. */
	XVT_ASSERT_INT_EQ(
		XvtStorage_Probe(AERON_VFS_ROOT_USER, "pilots/none.plt"), 0);
	XVT_ASSERT_INT_EQ(XvtStorage_Probe(AERON_VFS_ROOT_USER, "top.plt"), 0);
	XVT_ASSERT_INT_EQ(
		XvtStorage_Probe(AERON_VFS_ROOT_USER, "missing/deeper/x.plt"),
		0);
	XVT_ASSERT_INT_EQ(
		XvtStorage_Probe(AERON_VFS_ROOT_ASSET, "pilots/Ace.PLT"), 0);

	/* Not proven either way: a folder, a rejected path, a parent that is a file. */
	XVT_ASSERT_INT_EQ(XvtStorage_Probe(AERON_VFS_ROOT_USER, "pilots/sub"),
			  -1);
	XVT_ASSERT_INT_EQ(XvtStorage_Probe(AERON_VFS_ROOT_USER, "../x.plt"),
			  -1);
	XVT_ASSERT_INT_EQ(XvtStorage_Probe(AERON_VFS_ROOT_USER, "flat.txt/x"),
			  -1);

	/* A name the parent lists in other letter case: where the file system tells case apart, the exact
	 * lookup misses it and the answer is -1; where it does not, the lookup finds the file. */
	char other[XVT_TEST_PATH_CAPACITY];
	struct stat info;
	XvtTest_Join(other, g_user, "pilots/ace.plt");
	int caseBlind = stat(other, &info) == 0;
	XVT_ASSERT_INT_EQ(
		XvtStorage_Probe(AERON_VFS_ROOT_USER, "pilots/ace.plt"),
		caseBlind ? 1 : -1);
	EndRoots();
}

static void CheckResolveAsset(void)
{
	FreshRoots();
	char resolved[XVT_PATH_CAPACITY];
	XvtTest_MakeSubfolder(g_asset, "BalanceOfPower");
	XvtTest_WriteText(g_asset, "BalanceOfPower/both.dat", "bop");
	XvtTest_WriteText(g_asset, "both.dat", "plain");
	XvtTest_WriteText(g_asset, "plain.dat", "plain");
	XvtTest_WriteText(g_user, "user.dat", "user");

	XVT_ASSERT_INT_EQ(
		XvtStorage_ResolveAsset("both.dat", resolved, sizeof resolved),
		1);
	XVT_ASSERT_INT_EQ(strcmp(resolved, "BalanceOfPower/both.dat"), 0);
	XVT_ASSERT_INT_EQ(
		XvtStorage_ResolveAsset("plain.dat", resolved, sizeof resolved),
		1);
	XVT_ASSERT_INT_EQ(strcmp(resolved, "plain.dat"), 0);

	XVT_ASSERT_INT_EQ(
		XvtStorage_ResolveAsset("none.dat", resolved, sizeof resolved),
		0);
	XVT_ASSERT_INT_EQ(
		XvtStorage_ResolveAsset("user.dat", resolved, sizeof resolved),
		0);
	XVT_ASSERT_INT_EQ(XvtStorage_ResolveAsset("../none.dat", resolved,
						  sizeof resolved),
			  -1);
	XVT_ASSERT_INT_EQ(XvtStorage_ResolveAsset("plain.dat/inner", resolved,
						  sizeof resolved),
			  -1);
	EndRoots();
}

static void CheckOpenTemp(void)
{
	FreshRoots();
	/* .tmp and .tmt, in any letter case, go to TEMP for writes and reads alike. */
	OpenAndClose("work.tmp", "wb");
	XVT_ASSERT_INT_EQ(XvtTest_Kind(g_temp, "work.tmp"), 1);
	XVT_ASSERT_INT_EQ(XvtTest_Kind(g_user, "work.tmp"), 0);
	OpenAndClose("WORK.TMT", "wb");
	XVT_ASSERT_INT_EQ(XvtTest_Kind(g_temp, "WORK.TMT"), 1);

	XvtTest_WriteText(g_temp, "read.tmp", "temp");
	XvtTest_WriteText(g_asset, "read.tmp", "asset");
	XVT_ASSERT_TRUE(OpensText("read.tmp", "rb", "temp"));
	EndRoots();
}

static void CheckOpenCache(void)
{
	FreshRoots();
	/* Every cache extension, in any letter case, writes to USER under cache/, which is created. */
	static const char *const names[] = {"a.pal", "b.act", "c.inv",
					    "d.bin", "e.plo", "F.PAL"};
	for (size_t i = 0; i < sizeof names / sizeof names[0]; ++i) {
		char placed[64];
		snprintf(placed, sizeof placed, "cache/%s", names[i]);
		OpenAndClose(names[i], "wb");
		XVT_ASSERT_INT_EQ(XvtTest_Kind(g_user, placed), 1);
		XVT_ASSERT_INT_EQ(XvtTest_Kind(g_user, names[i]), 0);
	}

	/* A read finds the cache copy first, and falls back to ASSET when there is none. */
	XvtTest_WriteText(g_user, "cache/both.inv", "cache");
	XvtTest_WriteText(g_asset, "both.inv", "asset");
	XVT_ASSERT_TRUE(OpensText("both.inv", "rb", "cache"));
	XvtTest_MakeSubfolder(g_asset, "BalanceOfPower");
	XvtTest_WriteText(g_asset, "BalanceOfPower/only.act", "asset");
	XVT_ASSERT_TRUE(OpensText("only.act", "rb", "asset"));
	EndRoots();
}

static void CheckOpenUser(void)
{
	FreshRoots();
	/* .plt and .pl2 reads come from USER, never ASSET. */
	XvtTest_WriteText(g_user, "p.plt", "user");
	XvtTest_WriteText(g_asset, "p.plt", "asset");
	XVT_ASSERT_TRUE(OpensText("p.plt", "rb", "user"));
	XvtTest_WriteText(g_asset, "q.pl2", "asset");
	XVT_ASSERT_TRUE(XvtStorage_Open("q.pl2", "rb") == NULL);

	/* Any write goes to USER, creating missing folders; "r+" counts as a write. */
	OpenAndClose("save\\game.sav", "wb");
	XVT_ASSERT_INT_EQ(XvtTest_Kind(g_user, "save/game.sav"), 1);
	XVT_ASSERT_INT_EQ(XvtTest_Kind(g_asset, "save"), 0);
	XvtTest_WriteText(g_user, "r.dat", "user");
	XvtTest_WriteText(g_asset, "r.dat", "asset");
	XVT_ASSERT_TRUE(OpensText("r.dat", "r+b", "user"));
	EndRoots();
}

static void CheckOpenAsset(void)
{
	FreshRoots();
	/* Any other read: ASSET's BalanceOfPower/<path> first, then <path>; USER is not read. */
	XvtTest_MakeSubfolder(g_asset, "BalanceOfPower");
	XvtTest_WriteText(g_asset, "BalanceOfPower/o.dat", "bop");
	XvtTest_WriteText(g_asset, "o.dat", "plain");
	XvtTest_WriteText(g_asset, "p.dat", "plain");
	XvtTest_WriteText(g_user, "u.dat", "user");
	XVT_ASSERT_TRUE(OpensText("o.dat", "rb", "bop"));
	XVT_ASSERT_TRUE(OpensText("p.dat", "r", "plain"));
	XVT_ASSERT_TRUE(XvtStorage_Open("u.dat", "rb") == NULL);

	/* Refusals: no mode, a mode that does not start with r, w or a, a rejected path, a missing file. */
	XVT_ASSERT_TRUE(XvtStorage_Open("p.dat", NULL) == NULL);
	XVT_ASSERT_TRUE(XvtStorage_Open("p.dat", "x") == NULL);
	XVT_ASSERT_TRUE(XvtStorage_Open("p.dat", "") == NULL);
	XVT_ASSERT_TRUE(XvtStorage_Open("../p.dat", "rb") == NULL);
	XVT_ASSERT_TRUE(XvtStorage_Open("/p.dat", "rb") == NULL);
	XVT_ASSERT_TRUE(XvtStorage_Open("missing.dat", "rb") == NULL);
	EndRoots();
}

static void CheckOpenRoot(void)
{
	FreshRoots();
	/* A mode that writes is refused outside USER and TEMP, and creates nothing. */
	XVT_ASSERT_TRUE(XvtStorage_OpenRoot(AERON_VFS_ROOT_ASSET, "w.dat",
					    "wb") == NULL);
	XVT_ASSERT_TRUE(XvtStorage_OpenRoot(AERON_VFS_ROOT_ASSET, "w.dat",
					    "ab") == NULL);
	XVT_ASSERT_INT_EQ(XvtTest_Kind(g_asset, "w.dat"), 0);
	XvtTest_WriteText(g_asset, "w.dat", "asset");
	XVT_ASSERT_TRUE(XvtStorage_OpenRoot(AERON_VFS_ROOT_ASSET, "w.dat",
					    "r+b") == NULL);

	/* A write creates missing parent folders. */
	AeronFile *file =
		XvtStorage_OpenRoot(AERON_VFS_ROOT_TEMP, "a/b/c.dat", "wb");
	XVT_ASSERT_TRUE(file != NULL);
	XVT_ASSERT_TRUE(AeronVfs_Close(file));
	XVT_ASSERT_INT_EQ(XvtTest_Kind(g_temp, "a/b/c.dat"), 1);

	/* A read refuses a folder. */
	XVT_ASSERT_TRUE(XvtStorage_OpenRoot(AERON_VFS_ROOT_TEMP, "a/b", "rb") ==
			NULL);

	/* "a+" opens for appending only: what it writes goes to the end, and it reads nothing. */
	XvtTest_WriteText(g_user, "log.txt", "abc");
	file = XvtStorage_OpenRoot(AERON_VFS_ROOT_USER, "log.txt", "a+");
	XVT_ASSERT_TRUE(file != NULL);
	XVT_ASSERT_TRUE(AeronVfs_Write(file, "def", 3, NULL));
	XVT_ASSERT_TRUE(AeronVfs_Seek(file, 0, 0));
	char buffer[8];
	size_t got = 99;
	AeronVfs_Read(file, buffer, 3, &got);
	XVT_ASSERT_INT_EQ(got, 0);
	AeronVfs_Close(file);
	XVT_ASSERT_TRUE(XvtTest_FileIs(g_user, "log.txt", "abcdef"));

	/* Refusals: a bad mode, a rejected path. */
	XVT_ASSERT_TRUE(XvtStorage_OpenRoot(AERON_VFS_ROOT_USER, "log.txt",
					    "z") == NULL);
	XVT_ASSERT_TRUE(XvtStorage_OpenRoot(AERON_VFS_ROOT_USER, "log.txt",
					    NULL) == NULL);
	XVT_ASSERT_TRUE(XvtStorage_OpenRoot(AERON_VFS_ROOT_USER, "../log.txt",
					    "rb") == NULL);
	EndRoots();
}

static void CheckWriteAtomic(void)
{
	FreshRoots();
	XVT_ASSERT_INT_EQ(XvtStorage_WriteAtomic("state.dat", "first", 5), 1);
	XVT_ASSERT_TRUE(XvtTest_FileIs(g_user, "state.dat", "first"));
	XVT_ASSERT_INT_EQ(XvtStorage_WriteAtomic("state.dat", "2nd", 3), 1);
	XVT_ASSERT_TRUE(XvtTest_FileIs(g_user, "state.dat", "2nd"));
	XVT_ASSERT_INT_EQ(XvtTest_Kind(g_user, "state.dat.tmp"), 0);

	/* Placed as Open places a write: a cache name in USER's cache/, a .tmp name in TEMP. */
	XvtTest_MakeSubfolder(g_user, "cache");
	XVT_ASSERT_INT_EQ(XvtStorage_WriteAtomic("look.pal", "p", 1), 1);
	XVT_ASSERT_TRUE(XvtTest_FileIs(g_user, "cache/look.pal", "p"));
	XVT_ASSERT_INT_EQ(XvtStorage_WriteAtomic("scratch.tmp", "t", 1), 1);
	XVT_ASSERT_TRUE(XvtTest_FileIs(g_temp, "scratch.tmp", "t"));

	XVT_ASSERT_INT_EQ(XvtStorage_WriteAtomic("../escape.dat", "x", 1), 0);
	XVT_ASSERT_INT_EQ(XvtTest_Kind(g_folder, "escape.dat"), 0);
	EndRoots();
}

static void CheckRemove(void)
{
	FreshRoots();
	XvtTest_WriteText(g_user, "old.sav", "x");
	XVT_ASSERT_INT_EQ(XvtStorage_Remove("old.sav"), 0);
	XVT_ASSERT_INT_EQ(XvtTest_Kind(g_user, "old.sav"), 0);

	/* A cache file goes from USER's cache/, never from ASSET. */
	XvtTest_MakeSubfolder(g_user, "cache");
	XvtTest_WriteText(g_user, "cache/c.bin", "x");
	XvtTest_WriteText(g_asset, "c.bin", "x");
	XVT_ASSERT_INT_EQ(XvtStorage_Remove("c.bin"), 0);
	XVT_ASSERT_INT_EQ(XvtTest_Kind(g_user, "cache/c.bin"), 0);
	XVT_ASSERT_INT_EQ(XvtTest_Kind(g_asset, "c.bin"), 1);

	/* A .tmp file goes from TEMP. */
	XvtTest_WriteText(g_temp, "t.tmp", "x");
	XVT_ASSERT_INT_EQ(XvtStorage_Remove("t.tmp"), 0);
	XVT_ASSERT_INT_EQ(XvtTest_Kind(g_temp, "t.tmp"), 0);

	/* An empty folder goes; one that holds a file stays. */
	XvtTest_MakeSubfolder(g_user, "empty");
	XVT_ASSERT_INT_EQ(XvtStorage_Remove("empty"), 0);
	XVT_ASSERT_INT_EQ(XvtTest_Kind(g_user, "empty"), 0);
	XvtTest_MakeSubfolder(g_user, "full");
	XvtTest_WriteText(g_user, "full/f.txt", "x");
	XVT_ASSERT_INT_EQ(XvtStorage_Remove("full"), -1);
	XVT_ASSERT_INT_EQ(XvtTest_Kind(g_user, "full/f.txt"), 1);

	XVT_ASSERT_INT_EQ(XvtStorage_Remove("../user/full/f.txt"), -1);
	XVT_ASSERT_INT_EQ(XvtTest_Kind(g_user, "full/f.txt"), 1);
	EndRoots();
}

static void CheckRename(void)
{
	FreshRoots();
	XvtTest_WriteText(g_user, "a.sav", "a");
	XVT_ASSERT_INT_EQ(XvtStorage_Rename("a.sav", "b.sav"), 0);
	XVT_ASSERT_INT_EQ(XvtTest_Kind(g_user, "a.sav"), 0);
	XVT_ASSERT_TRUE(XvtTest_FileIs(g_user, "b.sav", "a"));

	/* USER only, without cache/: a cache name is renamed where it is given. */
	XvtTest_WriteText(g_user, "x.pal", "p");
	XVT_ASSERT_INT_EQ(XvtStorage_Rename("x.pal", "y.pal"), 0);
	XVT_ASSERT_INT_EQ(XvtTest_Kind(g_user, "y.pal"), 1);
	XVT_ASSERT_INT_EQ(XvtTest_Kind(g_user, "cache"), 0);

	/* A .tmp file is looked for in USER, so one that is only in TEMP is not renamed. */
	XvtTest_WriteText(g_temp, "w.tmp", "t");
	XVT_ASSERT_INT_EQ(XvtStorage_Rename("w.tmp", "v.tmp"), -1);
	XVT_ASSERT_INT_EQ(XvtTest_Kind(g_temp, "w.tmp"), 1);

	XVT_ASSERT_INT_EQ(XvtStorage_Rename("../user/b.sav", "c.sav"), -1);
	XVT_ASSERT_INT_EQ(XvtStorage_Rename("b.sav", "../c.sav"), -1);
	XVT_ASSERT_INT_EQ(XvtTest_Kind(g_user, "b.sav"), 1);
	EndRoots();
}

struct GlobSeen {
	int count;
	int stopAfter;
	char names[8][32];
};

static int Collect(void *context, const AeronVfsEntry *entry)
{
	struct GlobSeen *seen = context;
	if (seen->count < 8) {
		snprintf(seen->names[seen->count], sizeof seen->names[0], "%s",
			 entry->name);
	}
	++seen->count;
	return seen->stopAfter == 0 || seen->count < seen->stopAfter;
}

static int Saw(const struct GlobSeen *seen, const char *name)
{
	for (int i = 0; i < seen->count && i < 8; ++i) {
		if (!strcmp(seen->names[i], name)) {
			return 1;
		}
	}
	return 0;
}

static void CheckGlob(void)
{
	FreshRoots();
	XvtTest_MakeSubfolder(g_user, "pilots");
	XvtTest_MakeSubfolder(g_user, "pilots/dir.plt");
	XvtTest_WriteText(g_user, "pilots/Ace.PLT", "x");
	XvtTest_WriteText(g_user, "pilots/bob.plt", "x");
	XvtTest_WriteText(g_user, "pilots/notes.txt", "x");
	XvtTest_WriteText(g_user, "top.plt", "x");
	XvtTest_MakeSubfolder(g_asset, "pilots");
	XvtTest_WriteText(g_asset, "pilots/carl.plt", "x");

	/* Files only, in the named folder of the named root, matched in any letter case. */
	struct GlobSeen seen = {0};
	XVT_ASSERT_TRUE(XvtStorage_Glob(AERON_VFS_ROOT_USER, "pilots/*.plt",
					Collect, &seen) != 0);
	XVT_ASSERT_INT_EQ(seen.count, 2);
	XVT_ASSERT_TRUE(Saw(&seen, "Ace.PLT"));
	XVT_ASSERT_TRUE(Saw(&seen, "bob.plt"));

	/* A wildcard with no folder part lists the root itself. */
	memset(&seen, 0, sizeof seen);
	XVT_ASSERT_TRUE(XvtStorage_Glob(AERON_VFS_ROOT_USER, "*.PLT", Collect,
					&seen) != 0);
	XVT_ASSERT_INT_EQ(seen.count, 1);
	XVT_ASSERT_TRUE(Saw(&seen, "top.plt"));

	/* A callback that returns 0 stops the listing, and Glob returns 0. */
	memset(&seen, 0, sizeof seen);
	seen.stopAfter = 1;
	XVT_ASSERT_INT_EQ(XvtStorage_Glob(AERON_VFS_ROOT_USER, "pilots/*.plt",
					  Collect, &seen),
			  0);
	XVT_ASSERT_INT_EQ(seen.count, 1);

	/* A rejected wildcard lists nothing. */
	memset(&seen, 0, sizeof seen);
	XVT_ASSERT_INT_EQ(XvtStorage_Glob(AERON_VFS_ROOT_USER, "../user/*.plt",
					  Collect, &seen),
			  0);
	XVT_ASSERT_INT_EQ(seen.count, 0);
	EndRoots();
}

/* Opens path with Open in mode, remembers it as the global stream, and returns it. */
static AeronFile *OpenGlobal(const char *path, const char *mode)
{
	AeronFile *file = XvtStorage_Open(path, mode);
	XVT_ASSERT_TRUE(file != NULL);
	XvtStorage_CaptureGlobalStream();
	return file;
}

/* Sets the stream's error flag by reading from a stream open only for writing. */
static void FailRead(AeronFile *file)
{
	char byte;
	AeronVfs_Read(file, &byte, 1, NULL);
	XVT_ASSERT_TRUE(AeronVfs_HasError(file));
}

static void CheckGlobalStream(void)
{
	FreshRoots();
	XVT_ASSERT_INT_EQ(XvtStorage_CloseGlobalStream(NULL, 1), 0);

	/* A clean stream closes with 0 and keeps its file. */
	AeronFile *file = OpenGlobal("clean.sav", "wb");
	XVT_ASSERT_TRUE(AeronVfs_Write(file, "ok", 2, NULL));
	XVT_ASSERT_INT_EQ(XvtStorage_CloseGlobalStream(file, 1), 0);
	XVT_ASSERT_TRUE(XvtTest_FileIs(g_user, "clean.sav", "ok"));

	/* A stream with its error flag set returns 1; with remove_on_error its USER or TEMP file goes. */
	file = OpenGlobal("bad.sav", "wb");
	FailRead(file);
	XVT_ASSERT_INT_EQ(XvtStorage_CloseGlobalStream(file, 1), 1);
	XVT_ASSERT_INT_EQ(XvtTest_Kind(g_user, "bad.sav"), 0);
	file = OpenGlobal("bad.tmp", "wb");
	FailRead(file);
	XVT_ASSERT_INT_EQ(XvtStorage_CloseGlobalStream(file, 1), 1);
	XVT_ASSERT_INT_EQ(XvtTest_Kind(g_temp, "bad.tmp"), 0);

	/* Without remove_on_error the file stays. */
	file = OpenGlobal("kept.sav", "wb");
	FailRead(file);
	XVT_ASSERT_INT_EQ(XvtStorage_CloseGlobalStream(file, 0), 1);
	XVT_ASSERT_INT_EQ(XvtTest_Kind(g_user, "kept.sav"), 1);

	/* A file in ASSET is never removed. */
	XvtTest_WriteText(g_asset, "asset.dat", "a");
	file = OpenGlobal("asset.dat", "rb");
	XVT_ASSERT_TRUE(!AeronVfs_Write(file, "x", 1, NULL));
	XVT_ASSERT_TRUE(AeronVfs_HasError(file));
	XVT_ASSERT_INT_EQ(XvtStorage_CloseGlobalStream(file, 1), 1);
	XVT_ASSERT_INT_EQ(XvtTest_Kind(g_asset, "asset.dat"), 1);
	EndRoots();
}

static void CheckLastPath(void)
{
	FreshRoots();
	XVT_ASSERT_INT_EQ(strcmp(XvtStorage_LastPath(), ""), 0);

	/* A read that finds nothing tries ASSET's <path> last. */
	XVT_ASSERT_TRUE(XvtStorage_Open("dir\\Gone.dat", "rb") == NULL);
	XVT_ASSERT_INT_EQ(strcmp(XvtStorage_LastPath(), "dir/Gone.dat"), 0);
	XVT_ASSERT_INT_EQ(XvtStorage_LastRoot(), AERON_VFS_ROOT_ASSET);

	/* A write: the place it opened. */
	OpenAndClose("save/one.sav", "wb");
	XVT_ASSERT_INT_EQ(strcmp(XvtStorage_LastPath(), "save/one.sav"), 0);
	XVT_ASSERT_INT_EQ(XvtStorage_LastRoot(), AERON_VFS_ROOT_USER);
	OpenAndClose("c.pal", "wb");
	XVT_ASSERT_INT_EQ(strcmp(XvtStorage_LastPath(), "cache/c.pal"), 0);
	XVT_ASSERT_INT_EQ(XvtStorage_LastRoot(), AERON_VFS_ROOT_USER);
	OpenAndClose("t.tmp", "wb");
	XVT_ASSERT_INT_EQ(strcmp(XvtStorage_LastPath(), "t.tmp"), 0);
	XVT_ASSERT_INT_EQ(XvtStorage_LastRoot(), AERON_VFS_ROOT_TEMP);

	/* A path Open rejects stays as given. */
	XVT_ASSERT_TRUE(XvtStorage_Open("../up.dat", "rb") == NULL);
	XVT_ASSERT_INT_EQ(strcmp(XvtStorage_LastPath(), "../up.dat"), 0);

	/* OpenRoot and ResolveAsset: the last place they try. */
	AeronFile *file =
		XvtStorage_OpenRoot(AERON_VFS_ROOT_TEMP, "x\\y.dat", "wb");
	XVT_ASSERT_TRUE(file != NULL);
	AeronVfs_Close(file);
	XVT_ASSERT_INT_EQ(strcmp(XvtStorage_LastPath(), "x/y.dat"), 0);
	XVT_ASSERT_INT_EQ(XvtStorage_LastRoot(), AERON_VFS_ROOT_TEMP);
	char resolved[XVT_PATH_CAPACITY];
	XvtTest_MakeSubfolder(g_asset, "BalanceOfPower");
	XvtTest_WriteText(g_asset, "BalanceOfPower/r.dat", "r");
	XVT_ASSERT_INT_EQ(
		XvtStorage_ResolveAsset("r.dat", resolved, sizeof resolved), 1);
	XVT_ASSERT_INT_EQ(strcmp(XvtStorage_LastPath(), "BalanceOfPower/r.dat"),
			  0);
	XVT_ASSERT_INT_EQ(XvtStorage_LastRoot(), AERON_VFS_ROOT_ASSET);
	EndRoots();
}

int main(void)
{
	CheckBind();
	CheckNormalize();
	CheckProbe();
	CheckResolveAsset();
	CheckOpenTemp();
	CheckOpenCache();
	CheckOpenUser();
	CheckOpenAsset();
	CheckOpenRoot();
	CheckWriteAtomic();
	CheckRemove();
	CheckRename();
	CheckGlob();
	CheckGlobalStream();
	CheckLastPath();
	return 0;
}
