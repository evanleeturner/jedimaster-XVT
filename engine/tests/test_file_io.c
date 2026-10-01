#define _POSIX_C_SOURCE 200809L
/* Checks the stand-ins for the C library's FILE calls (xvt_runtime/storage/file_io.h) against the promises
 * in their header. Each case writes the bytes it reads to a file in a fresh temporary folder and opens it
 * through an Aeron VFS whose user root is that folder; no game data is read.
 *
 * Not run here: the calls that end the program through XvtStorage_Fatal (a Read or Write size that
 * overflows, a scan conversion other than %s, %d and %u, a scan width over seven digits), because that
 * path shows a message box. */
#include "test_assert.h"
#include "test_temp_folder.h"
#include "xvt_runtime/storage/file_io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char g_folder[XVT_TEST_PATH_CAPACITY];
static AeronVfs* g_vfs;

/* Replaces the file "case.bin" with size bytes of data and opens it in mode. */
static AeronFile* OpenWith(const void* data, size_t size, AeronVfsOpenMode mode) {
	XvtTest_WriteFile(g_folder, "case.bin", data, size);
	AeronFile* file = NULL;
	XVT_ASSERT_INT_EQ(AeronVfs_Open(g_vfs, AERON_VFS_ROOT_USER, "case.bin", mode, &file), 1);
	return file;
}

/* Replaces the file "case.bin" with text and opens it for reading only. */
static AeronFile* OpenText(const char* text) { return OpenWith(text, strlen(text), AERON_VFS_READ); }

/* Opens an empty "case.bin" for writing only. */
static AeronFile* OpenEmptyForWriting(void) { return OpenWith("", 0, AERON_VFS_WRITE); }

static void CheckRead(void) {
	char buffer[16];
	AeronFile* file = OpenText("0123456789");
	XVT_ASSERT_INT_EQ(XvtFile_Read(buffer, 0, 3, file), 0);
	XVT_ASSERT_INT_EQ(XvtFile_Read(buffer, 4, 0, file), 0);

	/* Ten bytes hold two whole 4-byte elements and part of a third, which is consumed but not counted. */
	XVT_ASSERT_INT_EQ(XvtFile_Read(buffer, 4, 3, file), 2);
	XVT_ASSERT_INT_EQ(memcmp(buffer, "0123456789", 10), 0);
	XVT_ASSERT_INT_EQ(XvtFile_Getc(file), EOF);
	XVT_ASSERT_INT_EQ(XvtFile_Close(file), 0);

	/* A handle open only for writing reads nothing. */
	file = OpenEmptyForWriting();
	XVT_ASSERT_INT_EQ(XvtFile_Read(buffer, 1, 4, file), 0);
	XvtFile_Close(file);
}

static void CheckWrite(void) {
	AeronFile* file = OpenEmptyForWriting();
	XVT_ASSERT_INT_EQ(XvtFile_Write("abcdefghijkl", 4, 3, file), 3);
	XVT_ASSERT_INT_EQ(XvtFile_Write("x", 0, 1, file), 0);
	XVT_ASSERT_INT_EQ(XvtFile_Write("x", 1, 0, file), 0);
	XVT_ASSERT_INT_EQ(XvtFile_Close(file), 0);
	XVT_ASSERT_TRUE(XvtTest_FileIs(g_folder, "case.bin", "abcdefghijkl"));

	/* A handle open only for reading writes nothing. */
	file = OpenText("abc");
	XVT_ASSERT_INT_EQ(XvtFile_Write("xyz", 1, 3, file), 0);
	XvtFile_Close(file);
	XVT_ASSERT_TRUE(XvtTest_FileIs(g_folder, "case.bin", "abc"));
}

static void CheckGetcPutc(void) {
	/* A 0xFF byte reads as 255, not as EOF. */
	const unsigned char bytes[] = { 0x00, 0xFF, 0x41 };
	AeronFile* file = OpenWith(bytes, sizeof bytes, AERON_VFS_READ);
	XVT_ASSERT_INT_EQ(XvtFile_Getc(file), 0x00);
	XVT_ASSERT_INT_EQ(XvtFile_Getc(file), 0xFF);
	XVT_ASSERT_INT_EQ(XvtFile_Getc(file), 0x41);
	XVT_ASSERT_INT_EQ(XvtFile_Getc(file), EOF);
	XVT_ASSERT_INT_EQ(XvtFile_Close(file), 0);

	/* Putc writes the low byte and returns it as 0..255, so -1 writes 0xFF and returns 255. */
	file = OpenEmptyForWriting();
	XVT_ASSERT_INT_EQ(XvtFile_Putc(0x1FF, file), 0xFF);
	XVT_ASSERT_INT_EQ(XvtFile_Putc(-1, file), 0xFF);
	XVT_ASSERT_INT_EQ(XvtFile_Putc('A', file), 'A');
	XVT_ASSERT_INT_EQ(XvtFile_Close(file), 0);
	size_t size;
	char* written = XvtTest_ReadFile(g_folder, "case.bin", &size);
	XVT_ASSERT_INT_EQ(size, 3);
	XVT_ASSERT_INT_EQ((unsigned char)written[0], 0xFF);
	XVT_ASSERT_INT_EQ((unsigned char)written[1], 0xFF);
	XVT_ASSERT_INT_EQ(written[2], 'A');
	free(written);

	/* A handle open only for writing gives EOF to Getc; one open only for reading gives EOF to Putc. */
	file = OpenEmptyForWriting();
	XVT_ASSERT_INT_EQ(XvtFile_Getc(file), EOF);
	XvtFile_Close(file);
	file = OpenText("q");
	XVT_ASSERT_INT_EQ(XvtFile_Putc('A', file), EOF);
	XvtFile_Close(file);
}

static void CheckGets(void) {
	char line[16];
	AeronFile* file = OpenText("ab\r\ncd\nef");
	XVT_ASSERT_TRUE(XvtFile_Gets(line, sizeof line, file) == line);
	XVT_ASSERT_INT_EQ(strcmp(line, "ab\n"), 0);
	XVT_ASSERT_TRUE(XvtFile_Gets(line, sizeof line, file) == line);
	XVT_ASSERT_INT_EQ(strcmp(line, "cd\n"), 0);
	XVT_ASSERT_TRUE(XvtFile_Gets(line, sizeof line, file) == line);
	XVT_ASSERT_INT_EQ(strcmp(line, "ef"), 0);
	XVT_ASSERT_TRUE(XvtFile_Gets(line, sizeof line, file) == NULL);
	XVT_ASSERT_INT_EQ(XvtFile_Close(file), 0);

	/* At most capacity - 1 bytes per call. A CR and its LF become one newline only when one call reads
	 * both. */
	file = OpenText("abc\r\n\r\nz");
	XVT_ASSERT_TRUE(XvtFile_Gets(line, 3, file) == line);
	XVT_ASSERT_INT_EQ(strcmp(line, "ab"), 0);
	XVT_ASSERT_TRUE(XvtFile_Gets(line, 3, file) == line);
	XVT_ASSERT_INT_EQ(strcmp(line, "c\r"), 0);
	XVT_ASSERT_TRUE(XvtFile_Gets(line, 3, file) == line);
	XVT_ASSERT_INT_EQ(strcmp(line, "\n"), 0);
	XVT_ASSERT_TRUE(XvtFile_Gets(line, 3, file) == line);
	XVT_ASSERT_INT_EQ(strcmp(line, "\n"), 0);
	XVT_ASSERT_TRUE(XvtFile_Gets(line, 3, file) == line);
	XVT_ASSERT_INT_EQ(strcmp(line, "z"), 0);
	XVT_ASSERT_TRUE(XvtFile_Gets(line, 3, file) == NULL);
	XVT_ASSERT_INT_EQ(XvtFile_Close(file), 0);

	/* A capacity that is not positive, or that leaves room for no byte, reads nothing. */
	file = OpenText("xyz\n");
	XVT_ASSERT_TRUE(XvtFile_Gets(line, 0, file) == NULL);
	XVT_ASSERT_TRUE(XvtFile_Gets(line, -1, file) == NULL);
	XVT_ASSERT_TRUE(XvtFile_Gets(line, 1, file) == NULL);
	XVT_ASSERT_INT_EQ(XvtFile_Getc(file), 'x');
	XVT_ASSERT_INT_EQ(XvtFile_Close(file), 0);
}

static void CheckPrintf(void) {
	/* Longer than any fixed buffer a formatter is likely to start with. */
	char text[2001];
	memset(text, 'x', 2000);
	text[2000] = 0;

	AeronFile* file = OpenEmptyForWriting();
	XVT_ASSERT_INT_EQ(XvtFile_Printf(file, "%d-%s", 42, "ok"), 5);
	XVT_ASSERT_INT_EQ(XvtFile_Printf(file, "[%s]", text), 2002);
	XVT_ASSERT_INT_EQ(XvtFile_Close(file), 0);

	size_t size;
	char* written = XvtTest_ReadFile(g_folder, "case.bin", &size);
	XVT_ASSERT_INT_EQ(size, 5 + 2002);
	XVT_ASSERT_INT_EQ(memcmp(written, "42-ok[", 6), 0);
	XVT_ASSERT_INT_EQ(memcmp(written + 6, text, 2000), 0);
	XVT_ASSERT_INT_EQ(written[2006], ']');
	free(written);

	/* A handle open only for reading takes a short write: -1. */
	file = OpenText("abc");
	XVT_ASSERT_INT_EQ(XvtFile_Printf(file, "%s", "zz"), -1);
	XvtFile_Close(file);
}

static void CheckScanfConversions(void) {
	char word[32], rest[32];
	int number = 0, second = 0;
	unsigned value = 0;

	AeronFile* file = OpenText("  alpha -42\n 7 ");
	XVT_ASSERT_INT_EQ(XvtFile_Scanf(file, "%s %d %u", word, &number, &value), 3);
	XVT_ASSERT_INT_EQ(strcmp(word, "alpha"), 0);
	XVT_ASSERT_INT_EQ(number, -42);
	XVT_ASSERT_INT_EQ(value, 7);
	XvtFile_Close(file);

	/* A number ends where its digits do; the rest of the token stays unread for the next conversion. */
	file = OpenText("12abc");
	XVT_ASSERT_INT_EQ(XvtFile_Scanf(file, "%d%s", &number, word), 2);
	XVT_ASSERT_INT_EQ(number, 12);
	XVT_ASSERT_INT_EQ(strcmp(word, "abc"), 0);
	XvtFile_Close(file);

	/* A width bounds each conversion. */
	file = OpenText("12345 abcdef");
	XVT_ASSERT_INT_EQ(XvtFile_Scanf(file, "%2d%d %3s%s", &number, &second, word, rest), 4);
	XVT_ASSERT_INT_EQ(number, 12);
	XVT_ASSERT_INT_EQ(second, 345);
	XVT_ASSERT_INT_EQ(strcmp(word, "abc"), 0);
	XVT_ASSERT_INT_EQ(strcmp(rest, "def"), 0);
	XvtFile_Close(file);

	/* A number token is at most 511 bytes: 511 zeros and a 7 read as two numbers. */
	char digits[513];
	memset(digits, '0', 511);
	digits[511] = '7';
	digits[512] = 0;
	file = OpenText(digits);
	number = second = -1;
	XVT_ASSERT_INT_EQ(XvtFile_Scanf(file, "%d%d", &number, &second), 2);
	XVT_ASSERT_INT_EQ(number, 0);
	XVT_ASSERT_INT_EQ(second, 7);
	XvtFile_Close(file);

	/* %s with no width has no bound. */
	char longWord[2001];
	char longText[2001];
	memset(longText, 'w', 2000);
	longText[2000] = 0;
	file = OpenText(longText);
	XVT_ASSERT_INT_EQ(XvtFile_Scanf(file, "%s", longWord), 1);
	XVT_ASSERT_INT_EQ(strcmp(longWord, longText), 0);
	XvtFile_Close(file);
}

static void CheckScanfLiteralsAndEnds(void) {
	int number = 0, second = 0;
	char word[32];

	/* A literal must match the next byte; whitespace in the format skips any whitespace first. */
	AeronFile* file = OpenText("1 ,2");
	XVT_ASSERT_INT_EQ(XvtFile_Scanf(file, "%d,%d", &number, &second), 1);
	XvtFile_Close(file);
	file = OpenText("1 ,2");
	XVT_ASSERT_INT_EQ(XvtFile_Scanf(file, "%d ,%d", &number, &second), 2);
	XVT_ASSERT_INT_EQ(second, 2);
	XvtFile_Close(file);
	file = OpenText("key=5");
	XVT_ASSERT_INT_EQ(XvtFile_Scanf(file, "key=%d", &number), 1);
	XVT_ASSERT_INT_EQ(number, 5);
	XvtFile_Close(file);
	file = OpenText("kez=5");
	XVT_ASSERT_INT_EQ(XvtFile_Scanf(file, "key=%d", &number), 0);
	XvtFile_Close(file);

	/* A token that is not a number is left unread and ends the scan. */
	file = OpenText("xyz 5");
	XVT_ASSERT_INT_EQ(XvtFile_Scanf(file, "%d %d", &number, &second), 0);
	XVT_ASSERT_INT_EQ(XvtFile_Getc(file), 'x');
	XvtFile_Close(file);

	/* EOF only when the input ended before the first conversion. */
	file = OpenText("");
	XVT_ASSERT_INT_EQ(XvtFile_Scanf(file, "%d", &number), EOF);
	XvtFile_Close(file);
	file = OpenText(" \n\t ");
	XVT_ASSERT_INT_EQ(XvtFile_Scanf(file, "%s", word), EOF);
	XvtFile_Close(file);
	file = OpenText("");
	XVT_ASSERT_INT_EQ(XvtFile_Scanf(file, "key=%d", &number), EOF);
	XvtFile_Close(file);
	file = OpenText("5");
	XVT_ASSERT_INT_EQ(XvtFile_Scanf(file, "%d %d", &number, &second), 1);
	XvtFile_Close(file);
}

static void CheckSeekTell(void) {
	AeronFile* file = OpenText("abcdef");
	XVT_ASSERT_INT_EQ(XvtFile_Tell(file), 0);
	XVT_ASSERT_INT_EQ(XvtFile_Seek(file, 2, SEEK_SET), 0);
	XVT_ASSERT_INT_EQ(XvtFile_Getc(file), 'c');
	XVT_ASSERT_INT_EQ(XvtFile_Tell(file), 3);
	XVT_ASSERT_INT_EQ(XvtFile_Seek(file, 1, SEEK_CUR), 0);
	XVT_ASSERT_INT_EQ(XvtFile_Getc(file), 'e');
	XVT_ASSERT_INT_EQ(XvtFile_Seek(file, -1, SEEK_END), 0);
	XVT_ASSERT_INT_EQ(XvtFile_Getc(file), 'f');

	/* Reading past the end sets the handle's end-of-file flag; a successful seek clears it. */
	XVT_ASSERT_INT_EQ(XvtFile_Getc(file), EOF);
	XVT_ASSERT_TRUE(AeronVfs_Eof(file));
	XVT_ASSERT_INT_EQ(XvtFile_Seek(file, 0, SEEK_SET), 0);
	XVT_ASSERT_TRUE(!AeronVfs_Eof(file));
	XVT_ASSERT_INT_EQ(XvtFile_Getc(file), 'a');

	/* An offset before the start of the file fails. */
	XVT_ASSERT_INT_EQ(XvtFile_Seek(file, -5, SEEK_SET), -1);
	XVT_ASSERT_INT_EQ(XvtFile_Close(file), 0);
}

static void CheckCloseFlush(void) {
	XVT_ASSERT_INT_EQ(XvtFile_Close(NULL), 0);
	AeronFile* file = OpenEmptyForWriting();
	XVT_ASSERT_INT_EQ(XvtFile_Putc('z', file), 'z');
	XVT_ASSERT_INT_EQ(XvtFile_Flush(file), 0);
	XVT_ASSERT_INT_EQ(XvtFile_Close(file), 0);
	XVT_ASSERT_TRUE(XvtTest_FileIs(g_folder, "case.bin", "z"));
}

int main(void) {
	XvtTest_MakeFolder(g_folder);
	AeronVfsConfig config = { 0 };
	config.asset_root = g_folder;
	config.resource_root = g_folder;
	config.user_root = g_folder;
	config.temp_root = g_folder;
	g_vfs = AeronVfs_Create(&config);
	XVT_ASSERT_TRUE(g_vfs != NULL);

	CheckRead();
	CheckWrite();
	CheckGetcPutc();
	CheckGets();
	CheckPrintf();
	CheckScanfConversions();
	CheckScanfLiteralsAndEnds();
	CheckSeekTell();
	CheckCloseFlush();

	AeronVfs_Destroy(g_vfs);
	XvtTest_RemoveTree(g_folder);
	return 0;
}
