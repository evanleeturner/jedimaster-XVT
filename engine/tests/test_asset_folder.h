/* A temporary asset folder bound to storage, for tests of code that looks game files up by name. The test
 * places empty files at the names it wants found; they stand in for the game's files only as names, never
 * as contents. The folder is made with test_temp_folder.h and removed with everything in it on Close.
 *
 * POSIX only, as test_temp_folder.h: define _POSIX_C_SOURCE as 200809L before the first include and
 * register the test under if(NOT MSVC). */
#ifndef XVT_TESTS_TEST_ASSET_FOLDER_H
#define XVT_TESTS_TEST_ASSET_FOLDER_H

#include "aeron/vfs.h"
#include "test_assert.h"
#include "test_temp_folder.h"
#include "xvt_runtime/storage/storage.h"

#include <stdio.h>
#include <string.h>

typedef struct XvtTestAssets {
	char folder[XVT_TEST_PATH_CAPACITY];
	char asset[XVT_TEST_PATH_CAPACITY];
	AeronVfs *vfs;
} XvtTestAssets;

/* Makes a fresh folder with empty asset, user and temp folders, and binds storage to them. */
static inline void XvtTest_OpenAssets(XvtTestAssets *assets)
{
	char user[XVT_TEST_PATH_CAPACITY];
	char temp[XVT_TEST_PATH_CAPACITY];
	XvtTest_MakeFolder(assets->folder);
	XvtTest_MakeSubfolder(assets->folder, "asset");
	XvtTest_MakeSubfolder(assets->folder, "user");
	XvtTest_MakeSubfolder(assets->folder, "temp");
	XvtTest_Join(assets->asset, assets->folder, "asset");
	XvtTest_Join(user, assets->folder, "user");
	XvtTest_Join(temp, assets->folder, "temp");
	AeronVfsConfig config = {0};
	config.asset_root = assets->asset;
	config.resource_root = assets->asset;
	config.user_root = user;
	config.temp_root = temp;
	assets->vfs = AeronVfs_Create(&config);
	XVT_ASSERT_TRUE(assets->vfs != NULL);
	XvtStorage_Bind(assets->vfs);
}

/* Places an empty file at path, relative to the asset folder and written with '/', making the folders on
 * the way that are missing. */
static inline void XvtTest_AddAsset(XvtTestAssets *assets, const char *path)
{
	char prefix[XVT_TEST_PATH_CAPACITY];
	size_t length = strlen(path);
	XVT_ASSERT_TRUE(length > 0 && length < sizeof prefix);
	memcpy(prefix, path, length + 1);
	for (size_t i = 0; i < length; ++i) {
		if (prefix[i] != '/') {
			continue;
		}
		prefix[i] = 0;
		if (XvtTest_Kind(assets->asset, prefix) == 0) {
			XvtTest_MakeSubfolder(assets->asset, prefix);
		}
		prefix[i] = '/';
	}
	XvtTest_WriteFile(assets->asset, path, "", 0);
}

/* Unbinds storage and removes the folder. */
static inline void XvtTest_CloseAssets(XvtTestAssets *assets)
{
	if (!assets->vfs) {
		return;
	}
	XvtStorage_Bind(NULL);
	AeronVfs_Destroy(assets->vfs);
	assets->vfs = NULL;
	XvtTest_RemoveTree(assets->folder);
}

#endif
