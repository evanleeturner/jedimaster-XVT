#define _POSIX_C_SOURCE 200809L
/* Checks the modern file listing behind FrontendFileList_BuildSorted
 * (xvt_runtime/compat/frontend_file_list_port.h) against the promises in its header. Each case starts
 * from a fresh temporary folder holding empty asset and user folders, which an Aeron VFS bound to storage
 * uses as the ASSET and USER roots; the test writes every file the listing sees. The list is released
 * with FrontendFileList_Free from the recovered frontend code, as the header says. */
#include "test_assert.h"
#include "test_temp_folder.h"
#include "xvt/frontend/frontend_file_list.h"
#include "xvt_runtime/compat/frontend_file_list_port.h"
#include "xvt_runtime/storage/storage.h"

#include <string.h>

static char g_folder[XVT_TEST_PATH_CAPACITY];
static char g_asset[XVT_TEST_PATH_CAPACITY];
static char g_user[XVT_TEST_PATH_CAPACITY];
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

/* Starts a case: empty ASSET and USER roots in a fresh folder, bound to storage. */
static void FreshRoots(void)
{
	EndRoots();
	XvtTest_MakeFolder(g_folder);
	XvtTest_MakeSubfolder(g_folder, "asset");
	XvtTest_MakeSubfolder(g_folder, "user");
	XvtTest_Join(g_asset, g_folder, "asset");
	XvtTest_Join(g_user, g_folder, "user");
	AeronVfsConfig config = {0};
	config.asset_root = g_asset;
	config.resource_root = g_asset;
	config.user_root = g_user;
	config.temp_root = g_user;
	g_vfs = AeronVfs_Create(&config);
	XVT_ASSERT_TRUE(g_vfs != NULL);
	XvtStorage_Bind(g_vfs);
}

/* Checks that list holds exactly the count paths given, in that order, and that count matches. */
static void ExpectPaths(const FrontendFileList *list, const char *const *paths,
			int count)
{
	XVT_ASSERT_TRUE(list != NULL);
	XVT_ASSERT_INT_EQ(list->count, count);
	const FrontendFileListNode *node = list->head;
	for (int i = 0; i < count; ++i) {
		XVT_ASSERT_TRUE(node != NULL);
		XVT_ASSERT_INT_EQ(strcmp(node->path, paths[i]), 0);
		node = node->next;
	}
	XVT_ASSERT_TRUE(node == NULL);
}

static void CheckSortedFiles(void)
{
	FreshRoots();
	XvtTest_MakeSubfolder(g_user, "pilots");
	XvtTest_MakeSubfolder(g_user, "pilots/dir.plt");
	XvtTest_WriteText(g_user, "pilots/dave.plt", "x");
	XvtTest_WriteText(g_user, "pilots/bob.plt", "x");
	XvtTest_WriteText(g_user, "pilots/Carl.plt", "x");
	XvtTest_WriteText(g_user, "pilots/Ace.PLT", "x");
	XvtTest_WriteText(g_user, "pilots/notes.txt", "x");
	XvtTest_MakeSubfolder(g_asset, "pilots");
	XvtTest_WriteText(g_asset, "pilots/asset.plt", "x");

	/* Files in USER only, never the folder, matched in any letter case, each path the wildcard's folder
	 * part plus the name, in strcmp order: capitals sort before small letters. */
	static const char *const expected[] = {
		"pilots/Ace.PLT", "pilots/Carl.plt", "pilots/bob.plt",
		"pilots/dave.plt"};
	FrontendFileList *list =
		FrontendFileList_BuildSortedModern("pilots/*.plt");
	ExpectPaths(list, expected, 4);
	FrontendFileList_Free(list);
	EndRoots();
}

static void CheckBareNames(void)
{
	FreshRoots();
	/* With no folder part in the wildcard, each path is the bare file name. */
	XvtTest_WriteText(g_user, "b.pl2", "x");
	XvtTest_WriteText(g_user, "a.pl2", "x");
	static const char *const expected[] = {"a.pl2", "b.pl2"};
	FrontendFileList *list = FrontendFileList_BuildSortedModern("*.pl2");
	ExpectPaths(list, expected, 2);
	FrontendFileList_Free(list);
	EndRoots();
}

static void CheckEmptyAndRefused(void)
{
	FreshRoots();
	XvtTest_WriteText(g_user, "a.pl2", "x");

	/* Nothing matched: an empty list, not NULL. */
	FrontendFileList *list = FrontendFileList_BuildSortedModern("*.zzz");
	ExpectPaths(list, NULL, 0);
	FrontendFileList_Free(list);

	/* A rejected wildcard: NULL. */
	XVT_ASSERT_TRUE(FrontendFileList_BuildSortedModern("../*.pl2") == NULL);
	XVT_ASSERT_TRUE(FrontendFileList_BuildSortedModern("/tmp/*.pl2") ==
			NULL);
	XVT_ASSERT_TRUE(FrontendFileList_BuildSortedModern("C:*.pl2") == NULL);
	EndRoots();
}

int main(void)
{
	CheckSortedFiles();
	CheckBareNames();
	CheckEmptyAndRefused();
	return 0;
}
