/* Shared setup for the tests of src/xvt_runtime/config/. Each check works in a fresh temporary folder that
 * holds the four VFS roots: resource/ with a copy of the shipped defaults (resources/config.yaml and
 * aeron/config/scene3d_defaults.yaml from the source tree), and empty user/, asset/ and temp/ folders. The
 * storage module is bound to the same VFS, since the settings code probes and opens files through it.
 * Nothing outside the folder is written, and the folder is removed when the check ends or the program
 * exits. The folder calls are POSIX: a test that includes this header defines _XOPEN_SOURCE 700 before
 * its first include, and CMake builds it only where MSVC is not the compiler. */
#ifndef XVT_TESTS_CONFIG_FIXTURE_H
#define XVT_TESTS_CONFIG_FIXTURE_H

#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "aeron/config_file.h"
#include "aeron/vfs.h"
#include "test_assert.h"
#include "xvt_runtime/config/config.h"
#include "xvt_runtime/config/settings.h"
#include "xvt_runtime/storage/storage.h"

#ifndef XVT_TEST_SOURCE_DIR
#error "XVT_TEST_SOURCE_DIR must name the source tree that holds the shipped defaults"
#endif

static char g_fixture_folder[512];
static AeronVfs *g_fixture_vfs;
static int g_fixture_documents;
static const char *g_fixture_case;

/* Names the input a check is working on, so that a failed check prints it; NULL once that input passed. */
static inline void fixture_case(const char *text) { g_fixture_case = text; }

/* Writes the host path of relative, a path inside the fixture folder, into path. */
static inline void fixture_path(char *path, size_t capacity,
				const char *relative)
{
	int length =
		snprintf(path, capacity, "%s/%s", g_fixture_folder, relative);
	XVT_ASSERT_TRUE(length > 0 && (size_t)length < capacity);
}

/* The whole host file as a NUL-terminated string to free(), or NULL when it cannot be read. */
static inline char *fixture_read_host_file(const char *path)
{
	FILE *file = fopen(path, "rb");
	if (!file) {
		return NULL;
	}
	char *text = NULL;
	size_t size = 0;
	char chunk[4096];
	size_t got;
	while ((got = fread(chunk, 1, sizeof chunk, file)) > 0) {
		char *grown = realloc(text, size + got + 1);
		XVT_ASSERT_TRUE(grown != NULL);
		text = grown;
		memcpy(text + size, chunk, got);
		size += got;
	}
	fclose(file);
	if (!text) {
		text = calloc(1, 1);
	}
	XVT_ASSERT_TRUE(text != NULL);
	text[size] = 0;
	return text;
}

/* The file at relative inside the fixture folder as a string to free(), or NULL when there is none. */
static inline char *fixture_read_text(const char *relative)
{
	char path[1024];
	fixture_path(path, sizeof path, relative);
	return fixture_read_host_file(path);
}

static inline void fixture_write_text(const char *relative, const char *text)
{
	char path[1024];
	fixture_path(path, sizeof path, relative);
	FILE *file = fopen(path, "wb");
	XVT_ASSERT_TRUE(file != NULL);
	size_t size = strlen(text);
	XVT_ASSERT_TRUE(fwrite(text, 1, size, file) == size);
	XVT_ASSERT_TRUE(fclose(file) == 0);
}

/* 1 when something (file or folder) exists at relative inside the fixture folder. */
static inline int fixture_exists(const char *relative)
{
	char path[1024];
	struct stat info;
	fixture_path(path, sizeof path, relative);
	return stat(path, &info) == 0;
}

static inline void fixture_make_folder(const char *relative)
{
	char path[1024];
	fixture_path(path, sizeof path, relative);
	XVT_ASSERT_INT_EQ(mkdir(path, 0700), 0);
}

static inline void fixture_remove(const char *relative)
{
	char path[1024];
	fixture_path(path, sizeof path, relative);
	XVT_ASSERT_INT_EQ(remove(path), 0);
}

static inline int fixture_remove_entry(const char *path,
				       const struct stat *info, int type,
				       struct FTW *walk)
{
	(void)info;
	(void)type;
	(void)walk;
	return remove(path);
}

/* Ends a check: drops the loaded settings, unbinds and frees the VFS, and removes the folder. */
static inline void fixture_end(void)
{
	xvt_config_shutdown();
	xvt_storage_bind(NULL);
	if (g_fixture_vfs) {
		AeronVfs_Destroy(g_fixture_vfs);
		g_fixture_vfs = NULL;
	}
	if (g_fixture_folder[0]) {
		nftw(g_fixture_folder, fixture_remove_entry, 16,
		     FTW_DEPTH | FTW_PHYS);
		g_fixture_folder[0] = 0;
	}
}

static inline void fixture_copy_shipped(const char *source,
					const char *relative)
{
	char path[1024];
	int length = snprintf(path, sizeof path, "%s/%s", XVT_TEST_SOURCE_DIR,
			      source);
	XVT_ASSERT_TRUE(length > 0 && (size_t)length < sizeof path);
	char *text = fixture_read_host_file(path);
	XVT_ASSERT_TRUE(text != NULL);
	fixture_write_text(relative, text);
	free(text);
}

/* Runs at exit: after a failed check, prints the input it was working on, then cleans up. */
static inline void fixture_at_exit(void)
{
	if (g_fixture_case) {
		fprintf(stderr, "while checking this input:\n%s\n",
			g_fixture_case);
	}
	fixture_end();
}

/* Starts a check from nothing: ends any earlier one, makes a fresh folder with the shipped defaults in
 * resource/, and binds a VFS over it. No settings are loaded. */
static inline void fixture_begin(void)
{
	static int registered;
	fixture_end();
	if (!registered) {
		XVT_ASSERT_INT_EQ(atexit(fixture_at_exit), 0);
		registered = 1;
	}
	const char *base = getenv("TMPDIR");
	int length = snprintf(g_fixture_folder, sizeof g_fixture_folder,
			      "%s/openxvt-test-XXXXXX",
			      base && base[0] ? base : "/tmp");
	XVT_ASSERT_TRUE(length > 0 && (size_t)length < sizeof g_fixture_folder);
	char *made = mkdtemp(g_fixture_folder);
	if (!made) {
		g_fixture_folder[0] = 0;
	}
	XVT_ASSERT_TRUE(made != NULL);
	g_fixture_documents = 0;
	g_fixture_case = NULL;

	fixture_make_folder("resource");
	fixture_make_folder("resource/aeron");
	fixture_make_folder("user");
	fixture_make_folder("asset");
	fixture_make_folder("temp");
	fixture_copy_shipped("resources/config.yaml", "resource/config.yaml");
	fixture_copy_shipped("aeron/config/scene3d_defaults.yaml",
			     "resource/aeron/scene3d_defaults.yaml");

	char asset[1024], resource[1024], user[1024], temp[1024];
	fixture_path(asset, sizeof asset, "asset");
	fixture_path(resource, sizeof resource, "resource");
	fixture_path(user, sizeof user, "user");
	fixture_path(temp, sizeof temp, "temp");
	AeronVfsConfig config = {.org_name = "OpenXvT",
				 .app_name = "tests",
				 .asset_root = asset,
				 .resource_root = resource,
				 .user_root = user,
				 .temp_root = temp};
	g_fixture_vfs = AeronVfs_Create(&config);
	XVT_ASSERT_TRUE(g_fixture_vfs != NULL);
	xvt_storage_bind(g_fixture_vfs);
}

/* Loads the settings from the fixture's VFS; the check stops when the load fails. */
static inline void fixture_load(void)
{
	char error[1024] = "";
	int loaded = xvt_config_load(g_fixture_vfs, error, sizeof error);
	if (!loaded) {
		fprintf(stderr, "settings did not load: %s\n", error);
	}
	XVT_ASSERT_INT_EQ(loaded, 1);
}

/* Parses text as a YAML document, through a new file temp/documentN.yaml in the TEMP root, N counting from 1
 * in each fresh folder; the check stops when it does not parse. */
static inline AeronConfigFile *fixture_yaml(const char *text)
{
	char name[64], relative[80];
	AeronConfigFile *document = NULL;
	AeronConfigError detail;
	snprintf(name, sizeof name, "document%d.yaml", ++g_fixture_documents);
	snprintf(relative, sizeof relative, "temp/%s", name);
	fixture_write_text(relative, text);
	int loaded = AeronConfigFile_LoadYamlEx(
		g_fixture_vfs, AERON_VFS_ROOT_TEMP, name, &document, &detail);
	if (!loaded) {
		fprintf(stderr, "%s:%d:%d: %s\n", detail.path, detail.line,
			detail.column, detail.message);
	}
	XVT_ASSERT_TRUE(loaded);
	return document;
}

/* The fixture's copy of the shipped defaults, as a document of its own. */
static inline AeronConfigFile *fixture_shipped_document(void)
{
	AeronConfigFile *document = NULL;
	AeronConfigError detail;
	XVT_ASSERT_TRUE(AeronConfigFile_LoadYamlEx(
		g_fixture_vfs, AERON_VFS_ROOT_RESOURCE, "config.yaml",
		&document, &detail));
	return document;
}

/* The shipped scene defaults, read from the fixture's copy. */
static inline void fixture_shipped_scene(struct xvt_scene_settings *scene)
{
	AeronConfigFile *document = NULL;
	AeronConfigError detail;
	XVT_ASSERT_TRUE(AeronConfigFile_LoadYamlEx(
		g_fixture_vfs, AERON_VFS_ROOT_RESOURCE,
		"aeron/scene3d_defaults.yaml", &document, &detail));
	XVT_ASSERT_TRUE(AeronSceneSettings_Load(AeronConfigFile_Root(document),
						&scene->ssao, &scene->shadows,
						&scene->tonemap, &detail));
	AeronConfigFile_Destroy(document);
}

/* 1 when both documents serialize to the same YAML text, or both are NULL. */
static inline int fixture_same_document(const AeronConfigFile *left,
					const AeronConfigFile *right)
{
	if (!left || !right) {
		return left == right;
	}
	char *a = NULL, *b = NULL;
	size_t a_size = 0, b_size = 0;
	AeronConfigError detail;
	XVT_ASSERT_TRUE(
		AeronConfigFile_SerializeYaml(left, &a, &a_size, &detail));
	XVT_ASSERT_TRUE(
		AeronConfigFile_SerializeYaml(right, &b, &b_size, &detail));
	int same = a_size == b_size && memcmp(a, b, a_size) == 0;
	AeronConfigFile_FreeSerialized(a);
	AeronConfigFile_FreeSerialized(b);
	return same;
}

/* A copy of the user overrides as they are now, to compare with later; destroy it when done. */
static inline AeronConfigFile *fixture_user_copy(void)
{
	AeronConfigFile *copy = NULL;
	AeronConfigError detail;
	if (!xvt_config_user_document()) {
		return NULL;
	}
	XVT_ASSERT_TRUE(AeronConfigFile_Clone(xvt_config_user_document(), &copy,
					      &detail));
	return copy;
}

#endif
