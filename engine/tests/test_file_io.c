#define _POSIX_C_SOURCE 200809L
/* Checks the stand-ins for the C library's FILE calls (xvt_runtime/storage/file_io.h) against the promises
 * in their header. Each case writes the bytes it reads to a file in a fresh temporary folder and opens it
 * through an Aeron VFS whose user root is that folder; no game data is read.
 *
 * Not run here: the calls that end the program through xvt_storage_fatal (a Read or Write size that
 * overflows, a scan conversion other than %s, %d and %u, a scan width over seven digits), because that
 * path shows a message box. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_assert.h"
#include "test_temp_folder.h"
#include "xvt_runtime/storage/file_io.h"

static char g_folder[XVT_TEST_PATH_CAPACITY];
static AeronVfs *g_vfs;

/* Replaces the file "case.bin" with size bytes of data and opens it in mode. */
static AeronFile *open_with(const void *data, size_t size,
			    AeronVfsOpenMode mode)
{
	xvt_test_write_file(g_folder, "case.bin", data, size);
	AeronFile *file = NULL;
	XVT_ASSERT_INT_EQ(AeronVfs_Open(g_vfs, AERON_VFS_ROOT_USER, "case.bin",
					mode, &file),
			  1);
	return file;
}

/* Replaces the file "case.bin" with text and opens it for reading only. */
static AeronFile *open_text(const char *text)
{
	return open_with(text, strlen(text), AERON_VFS_READ);
}

/* Opens an empty "case.bin" for writing only. */
static AeronFile *open_empty_for_writing(void)
{
	return open_with("", 0, AERON_VFS_WRITE);
}

static void check_read(void)
{
	char buffer[16];
	AeronFile *file = open_text("0123456789");
	XVT_ASSERT_INT_EQ(xvt_file_read(buffer, 0, 3, file), 0);
	XVT_ASSERT_INT_EQ(xvt_file_read(buffer, 4, 0, file), 0);

	/* Ten bytes hold two whole 4-byte elements and part of a third, which is consumed but not counted. */
	XVT_ASSERT_INT_EQ(xvt_file_read(buffer, 4, 3, file), 2);
	XVT_ASSERT_INT_EQ(memcmp(buffer, "0123456789", 10), 0);
	XVT_ASSERT_INT_EQ(xvt_file_getc(file), EOF);
	XVT_ASSERT_INT_EQ(xvt_file_close(file), 0);

	/* A handle open only for writing reads nothing. */
	file = open_empty_for_writing();
	XVT_ASSERT_INT_EQ(xvt_file_read(buffer, 1, 4, file), 0);
	xvt_file_close(file);
}

static void check_write(void)
{
	AeronFile *file = open_empty_for_writing();
	XVT_ASSERT_INT_EQ(xvt_file_write("abcdefghijkl", 4, 3, file), 3);
	XVT_ASSERT_INT_EQ(xvt_file_write("x", 0, 1, file), 0);
	XVT_ASSERT_INT_EQ(xvt_file_write("x", 1, 0, file), 0);
	XVT_ASSERT_INT_EQ(xvt_file_close(file), 0);
	XVT_ASSERT_TRUE(xvt_test_file_is(g_folder, "case.bin", "abcdefghijkl"));

	/* A handle open only for reading writes nothing. */
	file = open_text("abc");
	XVT_ASSERT_INT_EQ(xvt_file_write("xyz", 1, 3, file), 0);
	xvt_file_close(file);
	XVT_ASSERT_TRUE(xvt_test_file_is(g_folder, "case.bin", "abc"));
}

static void check_getc_putc(void)
{
	/* A 0xFF byte reads as 255, not as EOF. */
	const unsigned char bytes[] = {0x00, 0xFF, 0x41};
	AeronFile *file = open_with(bytes, sizeof bytes, AERON_VFS_READ);
	XVT_ASSERT_INT_EQ(xvt_file_getc(file), 0x00);
	XVT_ASSERT_INT_EQ(xvt_file_getc(file), 0xFF);
	XVT_ASSERT_INT_EQ(xvt_file_getc(file), 0x41);
	XVT_ASSERT_INT_EQ(xvt_file_getc(file), EOF);
	XVT_ASSERT_INT_EQ(xvt_file_close(file), 0);

	/* Putc writes the low byte and returns it as 0..255, so -1 writes 0xFF and returns 255. */
	file = open_empty_for_writing();
	XVT_ASSERT_INT_EQ(xvt_file_putc(0x1FF, file), 0xFF);
	XVT_ASSERT_INT_EQ(xvt_file_putc(-1, file), 0xFF);
	XVT_ASSERT_INT_EQ(xvt_file_putc('A', file), 'A');
	XVT_ASSERT_INT_EQ(xvt_file_close(file), 0);
	size_t size;
	char *written = xvt_test_read_file(g_folder, "case.bin", &size);
	XVT_ASSERT_INT_EQ(size, 3);
	XVT_ASSERT_INT_EQ((unsigned char)written[0], 0xFF);
	XVT_ASSERT_INT_EQ((unsigned char)written[1], 0xFF);
	XVT_ASSERT_INT_EQ(written[2], 'A');
	free(written);

	/* A handle open only for writing gives EOF to Getc; one open only for reading gives EOF to Putc. */
	file = open_empty_for_writing();
	XVT_ASSERT_INT_EQ(xvt_file_getc(file), EOF);
	xvt_file_close(file);
	file = open_text("q");
	XVT_ASSERT_INT_EQ(xvt_file_putc('A', file), EOF);
	xvt_file_close(file);
}

static void check_gets(void)
{
	char line[16];
	AeronFile *file = open_text("ab\r\ncd\nef");
	XVT_ASSERT_TRUE(xvt_file_gets(line, sizeof line, file) == line);
	XVT_ASSERT_INT_EQ(strcmp(line, "ab\n"), 0);
	XVT_ASSERT_TRUE(xvt_file_gets(line, sizeof line, file) == line);
	XVT_ASSERT_INT_EQ(strcmp(line, "cd\n"), 0);
	XVT_ASSERT_TRUE(xvt_file_gets(line, sizeof line, file) == line);
	XVT_ASSERT_INT_EQ(strcmp(line, "ef"), 0);
	XVT_ASSERT_TRUE(xvt_file_gets(line, sizeof line, file) == NULL);
	XVT_ASSERT_INT_EQ(xvt_file_close(file), 0);

	/* At most capacity - 1 bytes per call. A CR and its LF become one newline only when one call reads
	 * both. */
	file = open_text("abc\r\n\r\nz");
	XVT_ASSERT_TRUE(xvt_file_gets(line, 3, file) == line);
	XVT_ASSERT_INT_EQ(strcmp(line, "ab"), 0);
	XVT_ASSERT_TRUE(xvt_file_gets(line, 3, file) == line);
	XVT_ASSERT_INT_EQ(strcmp(line, "c\r"), 0);
	XVT_ASSERT_TRUE(xvt_file_gets(line, 3, file) == line);
	XVT_ASSERT_INT_EQ(strcmp(line, "\n"), 0);
	XVT_ASSERT_TRUE(xvt_file_gets(line, 3, file) == line);
	XVT_ASSERT_INT_EQ(strcmp(line, "\n"), 0);
	XVT_ASSERT_TRUE(xvt_file_gets(line, 3, file) == line);
	XVT_ASSERT_INT_EQ(strcmp(line, "z"), 0);
	XVT_ASSERT_TRUE(xvt_file_gets(line, 3, file) == NULL);
	XVT_ASSERT_INT_EQ(xvt_file_close(file), 0);

	/* A capacity that is not positive, or that leaves room for no byte, reads nothing. */
	file = open_text("xyz\n");
	XVT_ASSERT_TRUE(xvt_file_gets(line, 0, file) == NULL);
	XVT_ASSERT_TRUE(xvt_file_gets(line, -1, file) == NULL);
	XVT_ASSERT_TRUE(xvt_file_gets(line, 1, file) == NULL);
	XVT_ASSERT_INT_EQ(xvt_file_getc(file), 'x');
	XVT_ASSERT_INT_EQ(xvt_file_close(file), 0);
}

static void check_printf(void)
{
	/* Longer than any fixed buffer a formatter is likely to start with. */
	char text[2001];
	memset(text, 'x', 2000);
	text[2000] = 0;

	AeronFile *file = open_empty_for_writing();
	XVT_ASSERT_INT_EQ(xvt_file_printf(file, "%d-%s", 42, "ok"), 5);
	XVT_ASSERT_INT_EQ(xvt_file_printf(file, "[%s]", text), 2002);
	XVT_ASSERT_INT_EQ(xvt_file_close(file), 0);

	size_t size;
	char *written = xvt_test_read_file(g_folder, "case.bin", &size);
	XVT_ASSERT_INT_EQ(size, 5 + 2002);
	XVT_ASSERT_INT_EQ(memcmp(written, "42-ok[", 6), 0);
	XVT_ASSERT_INT_EQ(memcmp(written + 6, text, 2000), 0);
	XVT_ASSERT_INT_EQ(written[2006], ']');
	free(written);

	/* A handle open only for reading takes a short write: -1. */
	file = open_text("abc");
	XVT_ASSERT_INT_EQ(xvt_file_printf(file, "%s", "zz"), -1);
	xvt_file_close(file);
}

static void check_scanf_conversions(void)
{
	char word[32];
	int number = 0;
	unsigned value = 0;

	AeronFile *file = open_text("  alpha -42\n 7 ");
	XVT_ASSERT_INT_EQ(
		xvt_file_scanf(file, "%s %d %u", word, &number, &value), 3);
	XVT_ASSERT_INT_EQ(strcmp(word, "alpha"), 0);
	XVT_ASSERT_INT_EQ(number, -42);
	XVT_ASSERT_INT_EQ(value, 7);
	xvt_file_close(file);

	/* A number ends where its digits do; the rest of the token stays unread for the next conversion. */
	file = open_text("12abc");
	XVT_ASSERT_INT_EQ(xvt_file_scanf(file, "%d%s", &number, word), 2);
	XVT_ASSERT_INT_EQ(number, 12);
	XVT_ASSERT_INT_EQ(strcmp(word, "abc"), 0);
	xvt_file_close(file);

	/* A width bounds each conversion. */
	file = open_text("12345 abcdef");
	char rest[32];
	int second = 0;
	XVT_ASSERT_INT_EQ(xvt_file_scanf(file, "%2d%d %3s%s", &number, &second,
					 word, rest),
			  4);
	XVT_ASSERT_INT_EQ(number, 12);
	XVT_ASSERT_INT_EQ(second, 345);
	XVT_ASSERT_INT_EQ(strcmp(word, "abc"), 0);
	XVT_ASSERT_INT_EQ(strcmp(rest, "def"), 0);
	xvt_file_close(file);

	/* A number token is at most 511 bytes: 511 zeros and a 7 read as two numbers. */
	char digits[513];
	memset(digits, '0', 511);
	digits[511] = '7';
	digits[512] = 0;
	file = open_text(digits);
	number = -1;
	second = -1;
	XVT_ASSERT_INT_EQ(xvt_file_scanf(file, "%d%d", &number, &second), 2);
	XVT_ASSERT_INT_EQ(number, 0);
	XVT_ASSERT_INT_EQ(second, 7);
	xvt_file_close(file);

	char long_text[2001];
	memset(long_text, 'w', 2000);
	long_text[2000] = 0;
	file = open_text(long_text);
	/* %s with no width has no bound. */
	char long_word[2001];
	XVT_ASSERT_INT_EQ(xvt_file_scanf(file, "%s", long_word), 1);
	XVT_ASSERT_INT_EQ(strcmp(long_word, long_text), 0);
	xvt_file_close(file);
}

static void check_scanf_literals_and_ends(void)
{
	int number = 0;
	int second = 0;

	/* A literal must match the next byte; whitespace in the format skips any whitespace first. */
	AeronFile *file = open_text("1 ,2");
	XVT_ASSERT_INT_EQ(xvt_file_scanf(file, "%d,%d", &number, &second), 1);
	xvt_file_close(file);
	file = open_text("1 ,2");
	XVT_ASSERT_INT_EQ(xvt_file_scanf(file, "%d ,%d", &number, &second), 2);
	XVT_ASSERT_INT_EQ(second, 2);
	xvt_file_close(file);
	file = open_text("key=5");
	XVT_ASSERT_INT_EQ(xvt_file_scanf(file, "key=%d", &number), 1);
	XVT_ASSERT_INT_EQ(number, 5);
	xvt_file_close(file);
	file = open_text("kez=5");
	XVT_ASSERT_INT_EQ(xvt_file_scanf(file, "key=%d", &number), 0);
	xvt_file_close(file);

	/* A token that is not a number is left unread and ends the scan. */
	file = open_text("xyz 5");
	XVT_ASSERT_INT_EQ(xvt_file_scanf(file, "%d %d", &number, &second), 0);
	XVT_ASSERT_INT_EQ(xvt_file_getc(file), 'x');
	xvt_file_close(file);

	/* EOF only when the input ended before the first conversion. */
	file = open_text("");
	XVT_ASSERT_INT_EQ(xvt_file_scanf(file, "%d", &number), EOF);
	xvt_file_close(file);
	file = open_text(" \n\t ");
	char word[32];
	XVT_ASSERT_INT_EQ(xvt_file_scanf(file, "%s", word), EOF);
	xvt_file_close(file);
	file = open_text("");
	XVT_ASSERT_INT_EQ(xvt_file_scanf(file, "key=%d", &number), EOF);
	xvt_file_close(file);
	file = open_text("5");
	XVT_ASSERT_INT_EQ(xvt_file_scanf(file, "%d %d", &number, &second), 1);
	xvt_file_close(file);
}

static void check_seek_tell(void)
{
	AeronFile *file = open_text("abcdef");
	XVT_ASSERT_INT_EQ(xvt_file_tell(file), 0);
	XVT_ASSERT_INT_EQ(xvt_file_seek(file, 2, SEEK_SET), 0);
	XVT_ASSERT_INT_EQ(xvt_file_getc(file), 'c');
	XVT_ASSERT_INT_EQ(xvt_file_tell(file), 3);
	XVT_ASSERT_INT_EQ(xvt_file_seek(file, 1, SEEK_CUR), 0);
	XVT_ASSERT_INT_EQ(xvt_file_getc(file), 'e');
	XVT_ASSERT_INT_EQ(xvt_file_seek(file, -1, SEEK_END), 0);
	XVT_ASSERT_INT_EQ(xvt_file_getc(file), 'f');

	/* Reading past the end sets the handle's end-of-file flag; a successful seek clears it. */
	XVT_ASSERT_INT_EQ(xvt_file_getc(file), EOF);
	XVT_ASSERT_TRUE(AeronVfs_Eof(file));
	XVT_ASSERT_INT_EQ(xvt_file_seek(file, 0, SEEK_SET), 0);
	XVT_ASSERT_TRUE(!AeronVfs_Eof(file));
	XVT_ASSERT_INT_EQ(xvt_file_getc(file), 'a');

	/* An offset before the start of the file fails. */
	XVT_ASSERT_INT_EQ(xvt_file_seek(file, -5, SEEK_SET), -1);
	XVT_ASSERT_INT_EQ(xvt_file_close(file), 0);
}

static void check_close_flush(void)
{
	XVT_ASSERT_INT_EQ(xvt_file_close(NULL), 0);
	AeronFile *file = open_empty_for_writing();
	XVT_ASSERT_INT_EQ(xvt_file_putc('z', file), 'z');
	XVT_ASSERT_INT_EQ(xvt_file_flush(file), 0);
	XVT_ASSERT_INT_EQ(xvt_file_close(file), 0);
	XVT_ASSERT_TRUE(xvt_test_file_is(g_folder, "case.bin", "z"));
}

int main(void)
{
	xvt_test_make_folder(g_folder);
	AeronVfsConfig config = {0};
	config.asset_root = g_folder;
	config.resource_root = g_folder;
	config.user_root = g_folder;
	config.temp_root = g_folder;
	g_vfs = AeronVfs_Create(&config);
	XVT_ASSERT_TRUE(g_vfs != NULL);

	check_read();
	check_write();
	check_getc_putc();
	check_gets();
	check_printf();
	check_scanf_conversions();
	check_scanf_literals_and_ends();
	check_seek_tell();
	check_close_flush();

	AeronVfs_Destroy(g_vfs);
	xvt_test_remove_tree(g_folder);
	return 0;
}
