#ifndef XVT_RUNTIME_FILE_IO_H
#define XVT_RUNTIME_FILE_IO_H
#include <stddef.h>

#include "aeron/vfs.h"

#ifdef __cplusplus
extern "C" {
#endif
/* Stand-ins for the C library's FILE calls, over Aeron VFS handles.
 * src/xvt/assets/file.h maps the game's fread, fgets, fscanf and the rest onto
 * these. Each keeps the C library's return convention unless its note says
 * otherwise. The end-of-file and error flags are the handle's own, read with
 * AeronVfs_Eof and AeronVfs_HasError. */

/* Returns the number of whole elements read; a trailing partial element is
 * consumed but not counted. Returns 0 when size or count is 0. A size * count
 * that overflows ends the program through xvt_storage_fatal. */
size_t xvt_file_read(void *data, size_t size, size_t count, AeronFile *file);
/* Returns the number of whole elements written; otherwise as Read. */
size_t xvt_file_write(const void *data, size_t size, size_t count,
		      AeronFile *file);
/* The next byte as 0..255, or EOF at end of file or on error. */
int xvt_file_getc(AeronFile *file);
/* Writes the low byte of value; returns that byte as 0..255, or EOF. */
int xvt_file_putc(int value, AeronFile *file);
/* As fgets: reads at most capacity - 1 bytes, through the first newline, which
 * is kept; a CR LF pair read in the same call becomes one newline. Returns
 * buffer, or NULL when capacity is not positive or nothing was read. */
char *xvt_file_gets(char *buffer, int capacity, AeronFile *file);
/* Formats the whole text, of any length, then writes it. Returns the characters
 * written, or -1 on a format or allocation failure or a short write; a short
 * write may have written part of the text. */
int xvt_file_printf(AeronFile *file, const char *format, ...);
/* A subset of fscanf. Whitespace in the format skips any whitespace in the
 * input; any other literal character must match the next byte, or the scan
 * ends. The only conversions are %s, %d and %u, each with an optional width;
 * reaching any other, %% included, with input left ends the program through
 * xvt_storage_fatal, and a width longer than seven digits can too. %s with no
 * width has no bound. %d and %u read one whitespace-delimited token, no longer
 * than the width or 511 bytes, then seek back so the bytes the number did not
 * use stay unread; a token that is not a number is left unread and ends the
 * scan. Returns the number of conversions assigned, or EOF when the input ended
 * before the first one. */
int xvt_file_scanf(AeronFile *file, const char *format, ...);
/* origin is SEEK_SET, SEEK_CUR or SEEK_END. Returns 0, or -1 on failure. Success clears the
 * end-of-file flag. */
int xvt_file_seek(AeronFile *file, long offset, int origin);
/* The current offset, or -1 on failure or when the offset does not fit in a long. */
long xvt_file_tell(AeronFile *file);
/* Closes and frees the handle. Returns 0, also for NULL, or EOF when the close
 * failed; the handle is freed either way. */
int xvt_file_close(AeronFile *file);
/* Returns 0, or EOF when the flush failed. */
int xvt_file_flush(AeronFile *file);
#ifdef __cplusplus
}
#endif

#endif
