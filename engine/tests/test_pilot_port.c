#define _POSIX_C_SOURCE 200809L
/* Checks the modern pilot-file delete (xvt_runtime/compat/pilot_port.h) against the promises in its
 * header, for .plt and .pl2 names, the two extensions the header says Probe and Remove place alike. Each
 * case starts from a fresh temporary folder whose user folder an Aeron VFS bound to storage uses as the
 * USER root; the test writes every file it deletes.
 *
 * Not run here: a removal that fails on a file that is there. On a POSIX system that takes a folder the
 * test cannot write to, and the test may run as a user whom permissions do not stop. */
#include <sys/stat.h>

#include "test_assert.h"
#include "test_temp_folder.h"
#include "xvt_runtime/compat/pilot_port.h"
#include "xvt_runtime/storage/storage.h"

static char g_folder[XVT_TEST_PATH_CAPACITY];
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

/* Starts a case: an empty USER root in a fresh folder, bound to storage. */
static void fresh_roots(void)
{
	end_roots();
	xvt_test_make_folder(g_folder);
	xvt_test_make_subfolder(g_folder, "user");
	xvt_test_join(g_user, g_folder, "user");
	AeronVfsConfig config = {0};
	config.asset_root = g_user;
	config.resource_root = g_user;
	config.user_root = g_user;
	config.temp_root = g_user;
	g_vfs = AeronVfs_Create(&config);
	XVT_ASSERT_TRUE(g_vfs != NULL);
	xvt_storage_bind(g_vfs);
}

static void check_removes_file(void)
{
	fresh_roots();
	xvt_test_write_text(g_user, "ace.plt", "x");
	XVT_ASSERT_INT_EQ(pilot_remove_file_modern("ace.plt"), 1);
	XVT_ASSERT_INT_EQ(xvt_test_kind(g_user, "ace.plt"), 0);

	xvt_test_make_subfolder(g_user, "pilots");
	xvt_test_write_text(g_user, "pilots/bob.pl2", "x");
	XVT_ASSERT_INT_EQ(pilot_remove_file_modern("pilots\\bob.pl2"), 1);
	XVT_ASSERT_INT_EQ(xvt_test_kind(g_user, "pilots/bob.pl2"), 0);
	end_roots();
}

static void check_proven_absent(void)
{
	fresh_roots();
	XVT_ASSERT_INT_EQ(pilot_remove_file_modern("none.plt"), 1);
	XVT_ASSERT_INT_EQ(pilot_remove_file_modern("nofolder/none.pl2"), 1);
	end_roots();
}

static void check_presence_unknown(void)
{
	fresh_roots();
	/* A rejected name, and a name that is a folder, are not known to be gone: 0, and the folder stays. */
	XVT_ASSERT_INT_EQ(pilot_remove_file_modern("../ace.plt"), 0);
	xvt_test_make_subfolder(g_user, "dir.plt");
	XVT_ASSERT_INT_EQ(pilot_remove_file_modern("dir.plt"), 0);
	XVT_ASSERT_INT_EQ(xvt_test_kind(g_user, "dir.plt"), 2);

	/* With no VFS bound nothing is known: 0, and the file stays. */
	xvt_test_write_text(g_user, "kept.plt", "x");
	xvt_storage_bind(NULL);
	XVT_ASSERT_INT_EQ(pilot_remove_file_modern("kept.plt"), 0);
	xvt_storage_bind(g_vfs);
	XVT_ASSERT_INT_EQ(xvt_test_kind(g_user, "kept.plt"), 1);

	/* A name listed only in other letter case, where the file system tells case apart: Probe cannot
	 * prove the file absent, so 0, and the file stays. */
	xvt_test_write_text(g_user, "Case.PLT", "x");
	char other[XVT_TEST_PATH_CAPACITY];
	struct stat info;
	xvt_test_join(other, g_user, "case.plt");
	if (stat(other, &info) != 0) {
		XVT_ASSERT_INT_EQ(pilot_remove_file_modern("case.plt"), 0);
		XVT_ASSERT_INT_EQ(xvt_test_kind(g_user, "Case.PLT"), 1);
	}
	end_roots();
}

int main(void)
{
	check_removes_file();
	check_proven_absent();
	check_presence_unknown();
	return 0;
}
