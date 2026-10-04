#include "xvt_runtime/storage/file_io.h"

#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xvt_runtime/storage/storage.h"

size_t xvt_file_read(void *data, size_t size, size_t count, AeronFile *file)
{
	size_t bytes = 0;
	if (!size || !count) {
		return 0;
	}
	if (count > SIZE_MAX / size) {
		xvt_storage_fatal("Read size overflow", 1);
		return 0;
	}
	AeronVfs_Read(file, data, size * count, &bytes);
	return bytes / size;
}

size_t xvt_file_write(const void *data, size_t size, size_t count,
		      AeronFile *file)
{
	size_t bytes = 0;
	if (!size || !count) {
		return 0;
	}
	if (count > SIZE_MAX / size) {
		xvt_storage_fatal("Write size overflow", 1);
		return 0;
	}
	AeronVfs_Write(file, data, size * count, &bytes);
	return bytes / size;
}

int xvt_file_getc(AeronFile *file)
{
	unsigned char value;
	return xvt_file_read(&value, 1, 1, file) == 1 ? value : EOF;
}

int xvt_file_putc(int value, AeronFile *file)
{
	unsigned char byte = (unsigned char)value;
	return xvt_file_write(&byte, 1, 1, file) == 1 ? byte : EOF;
}

char *xvt_file_gets(char *buffer, int capacity, AeronFile *file)
{
	return capacity > 0 && AeronVfs_ReadLine(file, buffer, (size_t)capacity)
		       ? buffer
		       : NULL;
}

int xvt_file_seek(AeronFile *file, long offset, int origin)
{
	return AeronVfs_Seek(file, offset, origin) ? 0 : -1;
}

long xvt_file_tell(AeronFile *file)
{
	int64_t value = AeronVfs_Tell(file);
	return value < 0 || value > LONG_MAX ? -1 : (long)value;
}

int xvt_file_close(AeronFile *file)
{
	return file ? (AeronVfs_Close(file) ? 0 : EOF) : 0;
}

int xvt_file_flush(AeronFile *file) { return AeronVfs_Flush(file) ? 0 : EOF; }

int xvt_file_printf(AeronFile *file, const char *format, ...)
{
	va_list args;
	va_list copy;
	char local[512];
	char *text = local;
	int length;
	va_start(args, format);
	va_copy(copy, args);
	length = vsnprintf(local, sizeof(local), format, args);
	va_end(args);
	if (length >= (int)sizeof(local)) {
		text = malloc((size_t)length + 1);
		if (text) {
			vsnprintf(text, (size_t)length + 1, format, copy);
		}
	}
	va_end(copy);
	if (!text || length < 0) {
		return -1;
	}
	if (xvt_file_write(text, 1, (size_t)length, file) != (size_t)length) {
		length = -1;
	}
	if (text != local) {
		free(text);
	}
	return length;
}

static int xvt_file_peek(AeronFile *file)
{
	int ch = xvt_file_getc(file);
	if (ch != EOF && !AeronVfs_Seek(file, -1, SEEK_CUR)) {
		return EOF;
	}
	return ch;
}

static void xvt_file_skip_space(AeronFile *file)
{
	int ch;
	while ((ch = xvt_file_peek(file)) != EOF &&
	       isspace((unsigned char)ch)) {
		xvt_file_getc(file);
	}
}

/* The recovered resource lists use strings and decimal integers. Numeric
 * conversion uses libc; unread delimiters stay in the VFS stream. */
int xvt_file_scanf(AeronFile *file, const char *format, ...)
{
	va_list args;
	int assigned = 0;
	int input_failure = 0;
	va_start(args, format);
	while (*format) {
		int width = 0;
		int ch;
		char conversion;
		if (isspace((unsigned char)*format)) {
			xvt_file_skip_space(file);
			++format;
			continue;
		}
		if (*format++ != '%') {
			ch = xvt_file_peek(file);
			if (ch != (unsigned char)format[-1]) {
				input_failure = ch == EOF;
				break;
			}
			xvt_file_getc(file);
			continue;
		}
		while (isdigit((unsigned char)*format)) {
			if (width > 1000000) {
				xvt_storage_fatal("Invalid scan width", 1);
				goto done;
			}
			width = width * 10 + *format++ - '0';
		}
		conversion = *format++;
		xvt_file_skip_space(file);
		if (xvt_file_peek(file) == EOF) {
			input_failure = 1;
			break;
		}
		if (conversion == 's') {
			char *output = va_arg(args, char *);
			int count = 0;
			if (!width) {
				width = INT_MAX;
			}
			while (count < width &&
			       (ch = xvt_file_peek(file)) != EOF &&
			       !isspace((unsigned char)ch)) {
				output[count++] = (char)xvt_file_getc(file);
			}
			output[count] = 0;
			if (!count) {
				input_failure = 1;
				break;
			}
		} else if (conversion == 'd' || conversion == 'u') {
			char token[512];
			char spec[16];
			int count = 0;
			int consumed = 0;
			int result;
			int64_t start = AeronVfs_Tell(file);
			void *output = va_arg(args, void *);
			if (!width || width >= (int)sizeof(token)) {
				width = (int)sizeof(token) - 1;
			}
			while (count < width &&
			       (ch = xvt_file_peek(file)) != EOF &&
			       !isspace((unsigned char)ch)) {
				token[count++] = (char)xvt_file_getc(file);
			}
			token[count] = 0;
			snprintf(spec, sizeof(spec), "%%%c%%n", conversion);
			result = sscanf(token, spec, output, &consumed);
			if ((consumed != count &&
			     !AeronVfs_Seek(file, start + consumed,
					    SEEK_SET)) ||
			    result != 1) {
				break;
			}
		} else {
			xvt_storage_fatal("Unsupported file scan format", 1);
			break;
		}
		++assigned;
	}
done:
	va_end(args);
	return !assigned && input_failure ? EOF : assigned;
}
