/* Shared setup for the tests of src/xvt_runtime/config/. Each check works in a fresh temporary folder that
 * holds the four VFS roots: resource/ with a copy of the shipped defaults (resources/config.yaml and
 * aeron/config/scene3d_defaults.yaml from the source tree), and empty user/, asset/ and temp/ folders. The
 * storage module is bound to the same VFS, since the settings code probes and opens files through it.
 * Nothing outside the folder is written, and the folder is removed when the check ends or the program
 * exits. The folder calls are POSIX: a test that includes this header defines _XOPEN_SOURCE 700 before
 * its first include, and CMake builds it only where MSVC is not the compiler. */
#ifndef XVT_TESTS_CONFIG_FIXTURE_H
#define XVT_TESTS_CONFIG_FIXTURE_H

#include "aeron/config_file.h"
#include "aeron/vfs.h"
#include "test_assert.h"
#include "xvt_runtime/config/config.h"
#include "xvt_runtime/config/settings.h"
#include "xvt_runtime/storage/storage.h"

#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifndef XVT_TEST_SOURCE_DIR
#error "XVT_TEST_SOURCE_DIR must name the source tree that holds the shipped defaults"
#endif

static char g_fixtureFolder[512];
static AeronVfs* g_fixtureVfs;
static int g_fixtureDocuments;
static const char* g_fixtureCase;

/* Names the input a check is working on, so that a failed check prints it; NULL once that input passed. */
static inline void Fixture_Case(const char* text) { g_fixtureCase = text; }

/* Writes the host path of relative, a path inside the fixture folder, into path. */
static inline void Fixture_Path(char* path, size_t capacity, const char* relative) {
	int length = snprintf(path, capacity, "%s/%s", g_fixtureFolder, relative);
	XVT_ASSERT_TRUE(length > 0 && (size_t)length < capacity);
}

/* The whole host file as a NUL-terminated string to free(), or NULL when it cannot be read. */
static inline char* Fixture_ReadHostFile(const char* path) {
	FILE* file = fopen(path, "rb");
	if (!file)
		return NULL;
	char* text = NULL;
	size_t size = 0;
	char chunk[4096];
	size_t got;
	while ((got = fread(chunk, 1, sizeof chunk, file)) > 0) {
		char* grown = realloc(text, size + got + 1);
		XVT_ASSERT_TRUE(grown != NULL);
		text = grown;
		memcpy(text + size, chunk, got);
		size += got;
	}
	fclose(file);
	if (!text)
		text = calloc(1, 1);
	XVT_ASSERT_TRUE(text != NULL);
	text[size] = 0;
	return text;
}

/* The file at relative inside the fixture folder as a string to free(), or NULL when there is none. */
static inline char* Fixture_ReadText(const char* relative) {
	char path[1024];
	Fixture_Path(path, sizeof path, relative);
	return Fixture_ReadHostFile(path);
}

static inline void Fixture_WriteText(const char* relative, const char* text) {
	char path[1024];
	Fixture_Path(path, sizeof path, relative);
	FILE* file = fopen(path, "wb");
	XVT_ASSERT_TRUE(file != NULL);
	size_t size = strlen(text);
	XVT_ASSERT_TRUE(fwrite(text, 1, size, file) == size);
	XVT_ASSERT_TRUE(fclose(file) == 0);
}

/* 1 when something (file or folder) exists at relative inside the fixture folder. */
static inline int Fixture_Exists(const char* relative) {
	char path[1024];
	struct stat info;
	Fixture_Path(path, sizeof path, relative);
	return stat(path, &info) == 0;
}

static inline void Fixture_MakeFolder(const char* relative) {
	char path[1024];
	Fixture_Path(path, sizeof path, relative);
	XVT_ASSERT_INT_EQ(mkdir(path, 0700), 0);
}

static inline void Fixture_Remove(const char* relative) {
	char path[1024];
	Fixture_Path(path, sizeof path, relative);
	XVT_ASSERT_INT_EQ(remove(path), 0);
}

static inline int Fixture_RemoveEntry(const char* path, const struct stat* info, int type, struct FTW* walk) {
	(void)info;
	(void)type;
	(void)walk;
	return remove(path);
}

/* Ends a check: drops the loaded settings, unbinds and frees the VFS, and removes the folder. */
static inline void Fixture_End(void) {
	XvtConfig_Shutdown();
	XvtStorage_Bind(NULL);
	if (g_fixtureVfs) {
		AeronVfs_Destroy(g_fixtureVfs);
		g_fixtureVfs = NULL;
	}
	if (g_fixtureFolder[0]) {
		nftw(g_fixtureFolder, Fixture_RemoveEntry, 16, FTW_DEPTH | FTW_PHYS);
		g_fixtureFolder[0] = 0;
	}
}

static inline void Fixture_CopyShipped(const char* source, const char* relative) {
	char path[1024];
	int length = snprintf(path, sizeof path, "%s/%s", XVT_TEST_SOURCE_DIR, source);
	XVT_ASSERT_TRUE(length > 0 && (size_t)length < sizeof path);
	char* text = Fixture_ReadHostFile(path);
	XVT_ASSERT_TRUE(text != NULL);
	Fixture_WriteText(relative, text);
	free(text);
}

/* Runs at exit: after a failed check, prints the input it was working on, then cleans up. */
static inline void Fixture_AtExit(void) {
	if (g_fixtureCase)
		fprintf(stderr, "while checking this input:\n%s\n", g_fixtureCase);
	Fixture_End();
}

/* Starts a check from nothing: ends any earlier one, makes a fresh folder with the shipped defaults in
 * resource/, and binds a VFS over it. No settings are loaded. */
static inline void Fixture_Begin(void) {
	static int registered;
	Fixture_End();
	if (!registered) {
		XVT_ASSERT_INT_EQ(atexit(Fixture_AtExit), 0);
		registered = 1;
	}
	const char* base = getenv("TMPDIR");
	int length = snprintf(g_fixtureFolder, sizeof g_fixtureFolder, "%s/openxvt-test-XXXXXX",
						  base && base[0] ? base : "/tmp");
	XVT_ASSERT_TRUE(length > 0 && (size_t)length < sizeof g_fixtureFolder);
	char* made = mkdtemp(g_fixtureFolder);
	if (!made)
		g_fixtureFolder[0] = 0;
	XVT_ASSERT_TRUE(made != NULL);
	g_fixtureDocuments = 0;
	g_fixtureCase = NULL;

	Fixture_MakeFolder("resource");
	Fixture_MakeFolder("resource/aeron");
	Fixture_MakeFolder("user");
	Fixture_MakeFolder("asset");
	Fixture_MakeFolder("temp");
	Fixture_CopyShipped("resources/config.yaml", "resource/config.yaml");
	Fixture_CopyShipped("aeron/config/scene3d_defaults.yaml", "resource/aeron/scene3d_defaults.yaml");

	char asset[1024], resource[1024], user[1024], temp[1024];
	Fixture_Path(asset, sizeof asset, "asset");
	Fixture_Path(resource, sizeof resource, "resource");
	Fixture_Path(user, sizeof user, "user");
	Fixture_Path(temp, sizeof temp, "temp");
	AeronVfsConfig config = { .org_name = "OpenXvT",
							  .app_name = "tests",
							  .asset_root = asset,
							  .resource_root = resource,
							  .user_root = user,
							  .temp_root = temp };
	g_fixtureVfs = AeronVfs_Create(&config);
	XVT_ASSERT_TRUE(g_fixtureVfs != NULL);
	XvtStorage_Bind(g_fixtureVfs);
}

/* Loads the settings from the fixture's VFS; the check stops when the load fails. */
static inline void Fixture_Load(void) {
	char error[1024] = "";
	int loaded = XvtConfig_Load(g_fixtureVfs, error, sizeof error);
	if (!loaded)
		fprintf(stderr, "settings did not load: %s\n", error);
	XVT_ASSERT_INT_EQ(loaded, 1);
}

/* Parses text as a YAML document, through a new file temp/documentN.yaml in the TEMP root, N counting from 1
 * in each fresh folder; the check stops when it does not parse. */
static inline AeronConfigFile* Fixture_Yaml(const char* text) {
	char name[64], relative[80];
	AeronConfigFile* document = NULL;
	AeronConfigError detail;
	snprintf(name, sizeof name, "document%d.yaml", ++g_fixtureDocuments);
	snprintf(relative, sizeof relative, "temp/%s", name);
	Fixture_WriteText(relative, text);
	int loaded = AeronConfigFile_LoadYamlEx(g_fixtureVfs, AERON_VFS_ROOT_TEMP, name, &document, &detail);
	if (!loaded)
		fprintf(stderr, "%s:%d:%d: %s\n", detail.path, detail.line, detail.column, detail.message);
	XVT_ASSERT_TRUE(loaded);
	return document;
}

/* The fixture's copy of the shipped defaults, as a document of its own. */
static inline AeronConfigFile* Fixture_ShippedDocument(void) {
	AeronConfigFile* document = NULL;
	AeronConfigError detail;
	XVT_ASSERT_TRUE(
		AeronConfigFile_LoadYamlEx(g_fixtureVfs, AERON_VFS_ROOT_RESOURCE, "config.yaml", &document, &detail));
	return document;
}

/* The shipped scene defaults, read from the fixture's copy. */
static inline void Fixture_ShippedScene(XvtSceneSettings* scene) {
	AeronConfigFile* document = NULL;
	AeronConfigError detail;
	XVT_ASSERT_TRUE(AeronConfigFile_LoadYamlEx(g_fixtureVfs, AERON_VFS_ROOT_RESOURCE,
											   "aeron/scene3d_defaults.yaml", &document, &detail));
	XVT_ASSERT_TRUE(AeronSceneSettings_Load(AeronConfigFile_Root(document), &scene->ssao, &scene->shadows,
											&scene->tonemap, &detail));
	AeronConfigFile_Destroy(document);
}

/* 1 when both documents serialize to the same YAML text, or both are NULL. */
static inline int Fixture_SameDocument(const AeronConfigFile* left, const AeronConfigFile* right) {
	if (!left || !right)
		return left == right;
	char *a = NULL, *b = NULL;
	size_t a_size = 0, b_size = 0;
	AeronConfigError detail;
	XVT_ASSERT_TRUE(AeronConfigFile_SerializeYaml(left, &a, &a_size, &detail));
	XVT_ASSERT_TRUE(AeronConfigFile_SerializeYaml(right, &b, &b_size, &detail));
	int same = a_size == b_size && memcmp(a, b, a_size) == 0;
	AeronConfigFile_FreeSerialized(a);
	AeronConfigFile_FreeSerialized(b);
	return same;
}

/* A copy of the user overrides as they are now, to compare with later; destroy it when done. */
static inline AeronConfigFile* Fixture_UserCopy(void) {
	AeronConfigFile* copy = NULL;
	AeronConfigError detail;
	if (!XvtConfig_UserDocument())
		return NULL;
	XVT_ASSERT_TRUE(AeronConfigFile_Clone(XvtConfig_UserDocument(), &copy, &detail));
	return copy;
}

#endif
