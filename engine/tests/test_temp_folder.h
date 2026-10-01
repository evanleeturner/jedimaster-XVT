/* A fresh folder for a test that reads or writes files, and the few file operations such a test needs to
 * set up its files and inspect the result without going through the code under test.
 *
 * The folder is made under TMPDIR, or /tmp when that is unset, with a unique name, and removed with
 * everything in it when the test is done. Nothing is written anywhere else.
 *
 * POSIX only: a test that includes this header defines _POSIX_C_SOURCE as 200809L before its first
 * include, and is registered under if(NOT MSVC). */
#ifndef XVT_TESTS_TEST_TEMP_FOLDER_H
#define XVT_TESTS_TEST_TEMP_FOLDER_H

#include "test_assert.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define XVT_TEST_PATH_CAPACITY 4096

/* Writes folder/name into out; the check fails when it does not fit. */
static inline void XvtTest_Join(char* out, const char* folder, const char* name) {
	int length = snprintf(out, XVT_TEST_PATH_CAPACITY, "%s/%s", folder, name);
	XVT_ASSERT_TRUE(length > 0 && length < XVT_TEST_PATH_CAPACITY);
}

/* Creates a new, empty folder and writes its path into out, which holds XVT_TEST_PATH_CAPACITY bytes. */
static inline void XvtTest_MakeFolder(char* out) {
	const char* parent = getenv("TMPDIR");
	if (!parent || !parent[0])
		parent = "/tmp";
	XvtTest_Join(out, parent, "openxvt-test-XXXXXX");
	XVT_ASSERT_TRUE(mkdtemp(out) != NULL);
}

/* Removes path and, when it is a folder, everything in it. A missing path is not an error. */
static inline void XvtTest_RemoveTree(const char* path) {
	struct stat info;
	if (lstat(path, &info) != 0)
		return;
	if (S_ISDIR(info.st_mode)) {
		DIR* folder = opendir(path);
		XVT_ASSERT_TRUE(folder != NULL);
		struct dirent* entry;
		while ((entry = readdir(folder)) != NULL) {
			if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
				continue;
			char child[XVT_TEST_PATH_CAPACITY];
			XvtTest_Join(child, path, entry->d_name);
			XvtTest_RemoveTree(child);
		}
		closedir(folder);
		XVT_ASSERT_INT_EQ(rmdir(path), 0);
	} else {
		XVT_ASSERT_INT_EQ(unlink(path), 0);
	}
}

/* Creates the folder folder/name; its parent must exist. */
static inline void XvtTest_MakeSubfolder(const char* folder, const char* name) {
	char path[XVT_TEST_PATH_CAPACITY];
	XvtTest_Join(path, folder, name);
	XVT_ASSERT_INT_EQ(mkdir(path, 0700), 0);
}

/* Writes size bytes to folder/name, replacing any file there; its parent folder must exist. */
static inline void XvtTest_WriteFile(const char* folder, const char* name, const void* data, size_t size) {
	char path[XVT_TEST_PATH_CAPACITY];
	XvtTest_Join(path, folder, name);
	FILE* file = fopen(path, "wb");
	XVT_ASSERT_TRUE(file != NULL);
	XVT_ASSERT_INT_EQ(fwrite(data, 1, size, file), size);
	XVT_ASSERT_INT_EQ(fclose(file), 0);
}

/* Writes the text, without its terminator, to folder/name. */
static inline void XvtTest_WriteText(const char* folder, const char* name, const char* text) {
	XvtTest_WriteFile(folder, name, text, strlen(text));
}

/* Returns 1 when folder/name exists as a file, 2 as a folder, 0 when nothing is there. */
static inline int XvtTest_Kind(const char* folder, const char* name) {
	char path[XVT_TEST_PATH_CAPACITY];
	struct stat info;
	XvtTest_Join(path, folder, name);
	if (lstat(path, &info) != 0)
		return 0;
	return S_ISDIR(info.st_mode) ? 2 : 1;
}

/* Reads the whole of folder/name into a buffer the caller frees, with a terminator after the last byte
 * so a text file can be read as a string. Writes the byte count to size when size is not NULL. */
static inline char* XvtTest_ReadFile(const char* folder, const char* name, size_t* size) {
	char path[XVT_TEST_PATH_CAPACITY];
	XvtTest_Join(path, folder, name);
	FILE* file = fopen(path, "rb");
	XVT_ASSERT_TRUE(file != NULL);
	size_t used = 0, capacity = 256;
	char* data = malloc(capacity);
	XVT_ASSERT_TRUE(data != NULL);
	size_t got;
	while ((got = fread(data + used, 1, capacity - used - 1, file)) > 0) {
		used += got;
		if (capacity - used - 1 == 0) {
			capacity *= 2;
			data = realloc(data, capacity);
			XVT_ASSERT_TRUE(data != NULL);
		}
	}
	XVT_ASSERT_INT_EQ(ferror(file), 0);
	XVT_ASSERT_INT_EQ(fclose(file), 0);
	data[used] = 0;
	if (size)
		*size = used;
	return data;
}

/* Returns 1 when folder/name holds exactly the text, without a terminator. */
static inline int XvtTest_FileIs(const char* folder, const char* name, const char* text) {
	size_t size;
	char* data = XvtTest_ReadFile(folder, name, &size);
	int same = size == strlen(text) && memcmp(data, text, size) == 0;
	free(data);
	return same;
}

#endif
