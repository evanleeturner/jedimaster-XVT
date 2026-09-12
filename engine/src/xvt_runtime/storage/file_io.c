#include "xvt_runtime/storage/file_io.h"
#include "xvt_runtime/storage/storage.h"
#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

size_t XvtFile_Read(void* data, size_t size, size_t count, AeronFile* file) {
	size_t bytes = 0;
	if (!size || !count)
		return 0;
	if (count > SIZE_MAX / size) {
		XvtStorage_Fatal("Read size overflow", 1);
		return 0;
	}
	AeronVfs_Read(file, data, size * count, &bytes);
	return bytes / size;
}

size_t XvtFile_Write(const void* data, size_t size, size_t count, AeronFile* file) {
	size_t bytes = 0;
	if (!size || !count)
		return 0;
	if (count > SIZE_MAX / size) {
		XvtStorage_Fatal("Write size overflow", 1);
		return 0;
	}
	AeronVfs_Write(file, data, size * count, &bytes);
	return bytes / size;
}

int XvtFile_Getc(AeronFile* file) {
	unsigned char value;
	return XvtFile_Read(&value, 1, 1, file) == 1 ? value : EOF;
}

int XvtFile_Putc(int value, AeronFile* file) {
	unsigned char byte = (unsigned char)value;
	return XvtFile_Write(&byte, 1, 1, file) == 1 ? byte : EOF;
}

char* XvtFile_Gets(char* buffer, int capacity, AeronFile* file) {
	return capacity > 0 && AeronVfs_ReadLine(file, buffer, (size_t)capacity) ? buffer : NULL;
}

int XvtFile_Seek(AeronFile* file, long offset, int origin) {
	return AeronVfs_Seek(file, offset, origin) ? 0 : -1;
}

long XvtFile_Tell(AeronFile* file) {
	int64_t value = AeronVfs_Tell(file);
	return value < 0 || value > LONG_MAX ? -1 : (long)value;
}

int XvtFile_Close(AeronFile* file) { return file ? (AeronVfs_Close(file) ? 0 : EOF) : 0; }

int XvtFile_Flush(AeronFile* file) { return AeronVfs_Flush(file) ? 0 : EOF; }

int XvtFile_Printf(AeronFile* file, const char* format, ...) {
	va_list args, copy;
	char local[512];
	char* text = local;
	int length;
	va_start(args, format);
	va_copy(copy, args);
	length = vsnprintf(local, sizeof(local), format, args);
	va_end(args);
	if (length >= (int)sizeof(local)) {
		text = malloc((size_t)length + 1);
		if (text)
			vsnprintf(text, (size_t)length + 1, format, copy);
	}
	va_end(copy);
	if (!text || length < 0)
		return -1;
	if (XvtFile_Write(text, 1, (size_t)length, file) != (size_t)length)
		length = -1;
	if (text != local)
		free(text);
	return length;
}

static int XvtFile_Peek(AeronFile* file) {
	int ch = XvtFile_Getc(file);
	if (ch != EOF && !AeronVfs_Seek(file, -1, SEEK_CUR))
		return EOF;
	return ch;
}

static void XvtFile_SkipSpace(AeronFile* file) {
	int ch;
	while ((ch = XvtFile_Peek(file)) != EOF && isspace((unsigned char)ch))
		XvtFile_Getc(file);
}

/* The recovered resource lists use strings and decimal integers. Numeric
 * conversion uses libc; unread delimiters stay in the VFS stream. */
int XvtFile_Scanf(AeronFile* file, const char* format, ...) {
	va_list args;
	int assigned = 0;
	int input_failure = 0;
	va_start(args, format);
	while (*format) {
		int width = 0;
		int ch;
		char conversion;
		if (isspace((unsigned char)*format)) {
			XvtFile_SkipSpace(file);
			++format;
			continue;
		}
		if (*format++ != '%') {
			ch = XvtFile_Peek(file);
			if (ch != (unsigned char)format[-1]) {
				input_failure = ch == EOF;
				break;
			}
			XvtFile_Getc(file);
			continue;
		}
		while (isdigit((unsigned char)*format)) {
			if (width > 1000000) {
				XvtStorage_Fatal("Invalid scan width", 1);
				goto done;
			}
			width = width * 10 + *format++ - '0';
		}
		conversion = *format++;
		XvtFile_SkipSpace(file);
		if (XvtFile_Peek(file) == EOF) {
			input_failure = 1;
			break;
		}
		if (conversion == 's') {
			char* output = va_arg(args, char*);
			int count = 0;
			if (!width)
				width = INT_MAX;
			while (count < width && (ch = XvtFile_Peek(file)) != EOF && !isspace((unsigned char)ch))
				output[count++] = (char)XvtFile_Getc(file);
			output[count] = 0;
			if (!count) {
				input_failure = 1;
				break;
			}
		} else if (conversion == 'd' || conversion == 'u') {
			char token[512], spec[16];
			int count = 0, consumed = 0, result;
			int64_t start = AeronVfs_Tell(file);
			void* output = va_arg(args, void*);
			if (!width || width >= (int)sizeof(token))
				width = (int)sizeof(token) - 1;
			while (count < width && (ch = XvtFile_Peek(file)) != EOF && !isspace((unsigned char)ch))
				token[count++] = (char)XvtFile_Getc(file);
			token[count] = 0;
			snprintf(spec, sizeof(spec), "%%%c%%n", conversion);
			result = sscanf(token, spec, output, &consumed);
			if ((consumed != count && !AeronVfs_Seek(file, start + consumed, SEEK_SET)) || result != 1)
				break;
		} else {
			XvtStorage_Fatal("Unsupported file scan format", 1);
			break;
		}
		++assigned;
	}
done:
	va_end(args);
	return !assigned && input_failure ? EOF : assigned;
}
