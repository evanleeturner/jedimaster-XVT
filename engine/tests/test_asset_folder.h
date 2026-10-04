/* A temporary asset folder bound to storage, for tests of code that looks game
 * files up by name. The test places empty files at the names it wants found;
 * they stand in for the game's files only as names, never as contents. The
 * folder is made with test_temp_folder.h and removed with everything in it on
 * Close.
 *
 * POSIX only, as test_temp_folder.h: define _POSIX_C_SOURCE as 200809L before
 * the first include and register the test under if(NOT MSVC). */
#ifndef XVT_TESTS_TEST_ASSET_FOLDER_H
#define XVT_TESTS_TEST_ASSET_FOLDER_H

#include <stdio.h>
#include <string.h>

#include "aeron/vfs.h"
#include "test_assert.h"
#include "test_temp_folder.h"
#include "xvt_runtime/storage/storage.h"

struct xvt_test_assets {
	char folder[XVT_TEST_PATH_CAPACITY];
	char asset[XVT_TEST_PATH_CAPACITY];
	AeronVfs *vfs;
};

/* Makes a fresh folder with empty asset, user and temp folders, and binds storage to them. */
static inline void xvt_test_open_assets(struct xvt_test_assets *assets)
{
	char user[XVT_TEST_PATH_CAPACITY];
	char temp[XVT_TEST_PATH_CAPACITY];
	xvt_test_make_folder(assets->folder);
	xvt_test_make_subfolder(assets->folder, "asset");
	xvt_test_make_subfolder(assets->folder, "user");
	xvt_test_make_subfolder(assets->folder, "temp");
	xvt_test_join(assets->asset, assets->folder, "asset");
	xvt_test_join(user, assets->folder, "user");
	xvt_test_join(temp, assets->folder, "temp");
	AeronVfsConfig config = {0};
	config.asset_root = assets->asset;
	config.resource_root = assets->asset;
	config.user_root = user;
	config.temp_root = temp;
	assets->vfs = AeronVfs_Create(&config);
	XVT_ASSERT_TRUE(assets->vfs != NULL);
	xvt_storage_bind(assets->vfs);
}

/* Places an empty file at path, relative to the asset folder and written with
 * '/', making the folders on the way that are missing. */
static inline void xvt_test_add_asset(struct xvt_test_assets *assets,
				      const char *path)
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
		if (xvt_test_kind(assets->asset, prefix) == 0) {
			xvt_test_make_subfolder(assets->asset, prefix);
		}
		prefix[i] = '/';
	}
	xvt_test_write_file(assets->asset, path, "", 0);
}

/* Unbinds storage and removes the folder. */
static inline void xvt_test_close_assets(struct xvt_test_assets *assets)
{
	if (!assets->vfs) {
		return;
	}
	xvt_storage_bind(NULL);
	AeronVfs_Destroy(assets->vfs);
	assets->vfs = NULL;
	xvt_test_remove_tree(assets->folder);
}

#endif
