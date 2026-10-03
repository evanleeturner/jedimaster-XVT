#define _POSIX_C_SOURCE 200809L
/* Checks the modern file listing behind frontend_file_list_build_sorted
 * (xvt_runtime/compat/frontend_file_list_port.h) against the promises in its header. Each case starts
 * from a fresh temporary folder holding empty asset and user folders, which an Aeron VFS bound to storage
 * uses as the ASSET and USER roots; the test writes every file the listing sees. The list is released
 * with frontend_file_list_free from the recovered frontend code, as the header says. */
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

/* Starts a case: empty ASSET and USER roots in a fresh folder, bound to storage. */
static void fresh_roots(void)
{
	end_roots();
	xvt_test_make_folder(g_folder);
	xvt_test_make_subfolder(g_folder, "asset");
	xvt_test_make_subfolder(g_folder, "user");
	xvt_test_join(g_asset, g_folder, "asset");
	xvt_test_join(g_user, g_folder, "user");
	AeronVfsConfig config = {0};
	config.asset_root = g_asset;
	config.resource_root = g_asset;
	config.user_root = g_user;
	config.temp_root = g_user;
	g_vfs = AeronVfs_Create(&config);
	XVT_ASSERT_TRUE(g_vfs != NULL);
	xvt_storage_bind(g_vfs);
}

/* Checks that list holds exactly the count paths given, in that order, and that count matches. */
static void expect_paths(const struct frontend_file_list *list,
			 const char *const *paths, int count)
{
	XVT_ASSERT_TRUE(list != NULL);
	XVT_ASSERT_INT_EQ(list->count, count);
	const struct frontend_file_list_node *node = list->head;
	for (int i = 0; i < count; ++i) {
		XVT_ASSERT_TRUE(node != NULL);
		XVT_ASSERT_INT_EQ(strcmp(node->path, paths[i]), 0);
		node = node->next;
	}
	XVT_ASSERT_TRUE(node == NULL);
}

static void check_sorted_files(void)
{
	fresh_roots();
	xvt_test_make_subfolder(g_user, "pilots");
	xvt_test_make_subfolder(g_user, "pilots/dir.plt");
	xvt_test_write_text(g_user, "pilots/dave.plt", "x");
	xvt_test_write_text(g_user, "pilots/bob.plt", "x");
	xvt_test_write_text(g_user, "pilots/Carl.plt", "x");
	xvt_test_write_text(g_user, "pilots/Ace.PLT", "x");
	xvt_test_write_text(g_user, "pilots/notes.txt", "x");
	xvt_test_make_subfolder(g_asset, "pilots");
	xvt_test_write_text(g_asset, "pilots/asset.plt", "x");

	/* Files in USER only, never the folder, matched in any letter case, each path the wildcard's folder
	 * part plus the name, in strcmp order: capitals sort before small letters. */
	static const char *const expected[] = {
		"pilots/Ace.PLT", "pilots/Carl.plt", "pilots/bob.plt",
		"pilots/dave.plt"};
	struct frontend_file_list *list =
		frontend_file_list_build_sorted_modern("pilots/*.plt");
	expect_paths(list, expected, 4);
	frontend_file_list_free(list);
	end_roots();
}

static void check_bare_names(void)
{
	fresh_roots();
	/* With no folder part in the wildcard, each path is the bare file name. */
	xvt_test_write_text(g_user, "b.pl2", "x");
	xvt_test_write_text(g_user, "a.pl2", "x");
	static const char *const expected[] = {"a.pl2", "b.pl2"};
	struct frontend_file_list *list =
		frontend_file_list_build_sorted_modern("*.pl2");
	expect_paths(list, expected, 2);
	frontend_file_list_free(list);
	end_roots();
}

static void check_empty_and_refused(void)
{
	fresh_roots();
	xvt_test_write_text(g_user, "a.pl2", "x");

	/* Nothing matched: an empty list, not NULL. */
	struct frontend_file_list *list =
		frontend_file_list_build_sorted_modern("*.zzz");
	expect_paths(list, NULL, 0);
	frontend_file_list_free(list);

	/* A rejected wildcard: NULL. */
	XVT_ASSERT_TRUE(frontend_file_list_build_sorted_modern("../*.pl2") ==
			NULL);
	XVT_ASSERT_TRUE(frontend_file_list_build_sorted_modern("/tmp/*.pl2") ==
			NULL);
	XVT_ASSERT_TRUE(frontend_file_list_build_sorted_modern("C:*.pl2") ==
			NULL);
	end_roots();
}

int main(void)
{
	check_sorted_files();
	check_bare_names();
	check_empty_and_refused();
	return 0;
}
