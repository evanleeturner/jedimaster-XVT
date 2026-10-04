#include "xvt/assets/inventor_ascii.h"

#include "xvt/assets/file.h"

#ifndef XVT_MODERN
/* The fscanf format " %c": skip blanks, then read one character. Every function
 * in this file reads with it. */
// GLOBAL: XVT 0x51BE2C
static const char g_inventor_ascii_char_scan_format[] = " %c";

/* Consumes the stream up to and including the next '{', or to the end of the
 * file. Only the original build calls this. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x4138A0
void inventor_ascii_skip_past_open_brace(xvt_file *stream)
{
	char character;

	while (FILE_SCANF(stream, g_inventor_ascii_char_scan_format,
			  &character) == 1 &&
	       character != '{') {
	}
}

/* Consumes the stream up to and including the next '[', or to the end of the
 * file. Only the original build calls this. */
// FUNCTION: XVT 0x4138E0
void inventor_ascii_skip_past_open_bracket(xvt_file *stream)
{
	char character;

	while (FILE_SCANF(stream, g_inventor_ascii_char_scan_format,
			  &character) == 1 &&
	       character != '[') {
	}
}

/* Unless the next non-blank character is ']', consumes the stream up to and
 * including the next ',', or to the end of the file, so items with no comma
 * between them are consumed too. Only the original build calls this. */
// FUNCTION: XVT 0x413920
void inventor_ascii_skip_list_separator(xvt_file *stream)
{
	char character;

	if (inventor_ascii_peek_next_is_close_bracket(stream) == 0) {
		while (FILE_SCANF(stream, g_inventor_ascii_char_scan_format,
				  &character) == 1 &&
		       character != ',') {
		}
	}
}

/* Consumes the stream up to and including the next '"', or to the end of the
 * file. Only the original build calls this. */
// FUNCTION: XVT 0x413970
void inventor_ascii_skip_past_quote(xvt_file *stream)
{
	char character;

	while (FILE_SCANF(stream, g_inventor_ascii_char_scan_format,
			  &character) == 1 &&
	       character != '"') {
	}
}

/* Consumes the stream up to and including the next '}', or to the end of the
 * file. Only the original build calls this. */
// FUNCTION: XVT 0x4139B0
void inventor_ascii_skip_past_close_brace(xvt_file *stream)
{
	char character;

	while (FILE_SCANF(stream, g_inventor_ascii_char_scan_format,
			  &character) == 1 &&
	       character != '}') {
	}
}

/* Consumes the stream up to and including the next ']', or to the end of the
 * file. Only the original build calls this. */
// FUNCTION: XVT 0x4139F0
void inventor_ascii_skip_past_close_bracket(xvt_file *stream)
{
	char character;

	while (FILE_SCANF(stream, g_inventor_ascii_char_scan_format,
			  &character) == 1 &&
	       character != ']') {
	}
}

/* Returns 1 when the next non-blank character is '"', else 0, also at the end
 * of the file; seeks back, so the stream does not move. Only the original build
 * calls this. */
// FUNCTION: XVT 0x413A30
int inventor_ascii_peek_next_is_quote(xvt_file *stream)
{
	long position;
	char character;

	position = FILE_RAW_TELL(stream);
	if (FILE_SCANF(stream, g_inventor_ascii_char_scan_format, &character) !=
	    1) {
		FILE_RAW_SEEK(stream, position, SEEK_SET);
		return 0;
	}

	if (character == '"') {
		FILE_RAW_SEEK(stream, position, SEEK_SET);
		return 1;
	}

	FILE_RAW_SEEK(stream, position, SEEK_SET);
	return 0;
}

/* Returns 1 when the next non-blank character is '}', else 0, also at the end
 * of the file; seeks back, so the stream does not move. Only the original build
 * calls this. */
// FUNCTION: XVT 0x413AA0
int inventor_ascii_peek_next_is_close_brace(xvt_file *stream)
{
	long position;
	char character;

	position = FILE_RAW_TELL(stream);
	if (FILE_SCANF(stream, g_inventor_ascii_char_scan_format, &character) !=
	    1) {
		FILE_RAW_SEEK(stream, position, SEEK_SET);
		return 0;
	}

	if (character == '}') {
		FILE_RAW_SEEK(stream, position, SEEK_SET);
		return 1;
	}

	FILE_RAW_SEEK(stream, position, SEEK_SET);
	return 0;
}

/* Returns 1 when the next non-blank character is '{', else 0, also at the end
 * of the file; seeks back, so the stream does not move. Only the original build
 * calls this. */
// FUNCTION: XVT 0x413B10
int inventor_ascii_peek_next_is_open_brace(xvt_file *stream)
{
	long position;
	char character;

	position = FILE_RAW_TELL(stream);
	if (FILE_SCANF(stream, g_inventor_ascii_char_scan_format, &character) !=
	    1) {
		FILE_RAW_SEEK(stream, position, SEEK_SET);
		return 0;
	}

	if (character == '{') {
		FILE_RAW_SEEK(stream, position, SEEK_SET);
		return 1;
	}

	FILE_RAW_SEEK(stream, position, SEEK_SET);
	return 0;
}

/* Returns 1 when the next non-blank character is ']', else 0, also at the end
 * of the file; seeks back, so the stream does not move. Only the original build
 * calls this. */
// FUNCTION: XVT 0x413B80
int inventor_ascii_peek_next_is_close_bracket(xvt_file *stream)
{
	long position;
	char character;

	position = FILE_RAW_TELL(stream);
	if (FILE_SCANF(stream, g_inventor_ascii_char_scan_format, &character) !=
	    1) {
		FILE_RAW_SEEK(stream, position, SEEK_SET);
		return 0;
	}

	if (character == ']') {
		FILE_RAW_SEEK(stream, position, SEEK_SET);
		return 1;
	}

	FILE_RAW_SEEK(stream, position, SEEK_SET);
	return 0;
}

/* Returns 1 when the next non-blank character is '[', else 0, also at the end
 * of the file; seeks back, so the stream does not move. Only the original build
 * calls this. */
// FUNCTION: XVT 0x413BF0
int inventor_ascii_peek_next_is_open_bracket(xvt_file *stream)
{
	long position;
	char character;

	position = FILE_RAW_TELL(stream);
	if (FILE_SCANF(stream, g_inventor_ascii_char_scan_format, &character) !=
	    1) {
		FILE_RAW_SEEK(stream, position, SEEK_SET);
		return 0;
	}

	if (character == '[') {
		FILE_RAW_SEEK(stream, position, SEEK_SET);
		return 1;
	}

	FILE_RAW_SEEK(stream, position, SEEK_SET);
	return 0;
}

/* Reads characters up to and including the next '\n'. Never stops at the end of
 * the file: EOF cast to a byte is 0xFF, not '\n', so it loops forever there.
 * Only the original build calls this, for a '#' comment line. */
// FUNCTION: XVT 0x413C60
void inventor_ascii_skip_to_end_of_line(xvt_file *stream)
{

	while ((uint8_t)FILE_GETC(stream) != '\n') {
	}
}
#endif
