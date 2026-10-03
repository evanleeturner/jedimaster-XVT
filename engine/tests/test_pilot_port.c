#define _POSIX_C_SOURCE 200809L
/* Checks the modern pilot-file delete (xvt_runtime/compat/pilot_port.h) against the promises in its
 * header, for .plt and .pl2 names, the two extensions the header says Probe and Remove place alike. Each
 * case starts from a fresh temporary folder whose user folder an Aeron VFS bound to storage uses as the
 * USER root; the test writes every file it deletes.
 *
 * Not run here: a removal that fails on a file that is there. On a POSIX system that takes a folder the
 * test cannot write to, and the test may run as a user whom permissions do not stop. */
#include "test_assert.h"
#include "test_temp_folder.h"
#include "xvt_runtime/compat/pilot_port.h"
#include "xvt_runtime/storage/storage.h"

#include <sys/stat.h>

static char g_folder[XVT_TEST_PATH_CAPACITY];
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

/* Starts a case: an empty USER root in a fresh folder, bound to storage. */
static void FreshRoots(void)
{
	EndRoots();
	XvtTest_MakeFolder(g_folder);
	XvtTest_MakeSubfolder(g_folder, "user");
	XvtTest_Join(g_user, g_folder, "user");
	AeronVfsConfig config = {0};
	config.asset_root = g_user;
	config.resource_root = g_user;
	config.user_root = g_user;
	config.temp_root = g_user;
	g_vfs = AeronVfs_Create(&config);
	XVT_ASSERT_TRUE(g_vfs != NULL);
	XvtStorage_Bind(g_vfs);
}

static void CheckRemovesFile(void)
{
	FreshRoots();
	XvtTest_WriteText(g_user, "ace.plt", "x");
	XVT_ASSERT_INT_EQ(Pilot_RemoveFileModern("ace.plt"), 1);
	XVT_ASSERT_INT_EQ(XvtTest_Kind(g_user, "ace.plt"), 0);

	XvtTest_MakeSubfolder(g_user, "pilots");
	XvtTest_WriteText(g_user, "pilots/bob.pl2", "x");
	XVT_ASSERT_INT_EQ(Pilot_RemoveFileModern("pilots\\bob.pl2"), 1);
	XVT_ASSERT_INT_EQ(XvtTest_Kind(g_user, "pilots/bob.pl2"), 0);
	EndRoots();
}

static void CheckProvenAbsent(void)
{
	FreshRoots();
	XVT_ASSERT_INT_EQ(Pilot_RemoveFileModern("none.plt"), 1);
	XVT_ASSERT_INT_EQ(Pilot_RemoveFileModern("nofolder/none.pl2"), 1);
	EndRoots();
}

static void CheckPresenceUnknown(void)
{
	FreshRoots();
	/* A rejected name, and a name that is a folder, are not known to be gone: 0, and the folder stays. */
	XVT_ASSERT_INT_EQ(Pilot_RemoveFileModern("../ace.plt"), 0);
	XvtTest_MakeSubfolder(g_user, "dir.plt");
	XVT_ASSERT_INT_EQ(Pilot_RemoveFileModern("dir.plt"), 0);
	XVT_ASSERT_INT_EQ(XvtTest_Kind(g_user, "dir.plt"), 2);

	/* With no VFS bound nothing is known: 0, and the file stays. */
	XvtTest_WriteText(g_user, "kept.plt", "x");
	XvtStorage_Bind(NULL);
	XVT_ASSERT_INT_EQ(Pilot_RemoveFileModern("kept.plt"), 0);
	XvtStorage_Bind(g_vfs);
	XVT_ASSERT_INT_EQ(XvtTest_Kind(g_user, "kept.plt"), 1);

	/* A name listed only in other letter case, where the file system tells case apart: Probe cannot
	 * prove the file absent, so 0, and the file stays. */
	XvtTest_WriteText(g_user, "Case.PLT", "x");
	char other[XVT_TEST_PATH_CAPACITY];
	struct stat info;
	XvtTest_Join(other, g_user, "case.plt");
	if (stat(other, &info) != 0) {
		XVT_ASSERT_INT_EQ(Pilot_RemoveFileModern("case.plt"), 0);
		XVT_ASSERT_INT_EQ(XvtTest_Kind(g_user, "Case.PLT"), 1);
	}
	EndRoots();
}

int main(void)
{
	CheckRemovesFile();
	CheckProvenAbsent();
	CheckPresenceUnknown();
	return 0;
}
