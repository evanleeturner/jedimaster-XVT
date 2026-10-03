#include "xvt/util/debug_console.h"

#include "xvt/assets/file.h"

#include <stdio.h>
#include <string.h>

/* The first of three switches debug_console_toggle_file_dump checks: it turns the
 * file dump on only when one of them is nonzero. Starts at 0, and nothing
 * writes it. */
// GLOBAL: XVT 0x5235E4
static int g_debug_console_file_dump_gate0 = 0;
/* The second of those three switches. Starts at 0, and nothing writes it. */
// GLOBAL: XVT 0x5235E8
static int g_debug_console_file_dump_gate1 = 0;
/* The third of those three switches. Starts at 1 and nothing writes it, so
 * debug_console_toggle_file_dump may always turn the dump on. */
// GLOBAL: XVT 0x5235EC
static int g_debug_console_file_dump_gate2 = 1;
/* 1 once debug_console_write_text has filled its text buffer with blanks; while
 * it is 0, the next call does that first. debug_console_set_initialized, which
 * nothing calls, is its only other writer. */
// GLOBAL: XVT 0x528100
int g_debug_console_initialized;
/* Column, 0 to 80, where debug_console_write_text writes next. Written by
 * debug_console_write_text and by debug_console_set_cursor_position, which nothing
 * calls. */
// GLOBAL: XVT 0x528104
int g_debug_console_cursor_column;
/* Row where debug_console_write_text writes next. When it is past
 * g_debug_console_scroll_bottom_row, the next write scrolls the region up one row
 * and writes on its bottom row. Written by debug_console_write_text,
 * debug_console_write_text_in_scroll_region and debug_console_set_cursor_position; only
 * the first of these is ever called. */
// GLOBAL: XVT 0x528108
int g_debug_console_cursor_row;
/* Top row of the region debug_console_write_text scrolls: 0, except while
 * debug_console_write_text_in_scroll_region runs, which nothing calls. */
// GLOBAL: XVT 0x52810C
int g_debug_console_scroll_top_row = 0;
/* Bottom row of the region debug_console_write_text scrolls: 24, the last row,
 * except while debug_console_write_text_in_scroll_region runs, which nothing
 * calls. */
// GLOBAL: XVT 0x528110
int g_debug_console_scroll_bottom_row = 24;
/* Nonzero while debug_console_write_text also appends its text to mpDump.txt.
 * Only debug_console_toggle_file_dump and debug_console_set_initialized write it,
 * and nothing calls either, so it stays 0. */
// GLOBAL: XVT 0x528114
int g_debug_console_file_dump_enabled;

/* Does nothing in either build: its calls print nothing, and it reads none of
 * its arguments. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x4079E0
void debug_printf(const char *format, ...) { (void)format; }

/* Also turns off the mpDump.txt file dump. */
/* Sets g_debug_console_initialized; 0 makes the next debug_console_write_text blank
 * its buffer. Nothing calls this. */
// FUNCTION: XVT 0x4ACBF0
void debug_console_set_initialized(int initialized)
{
	g_debug_console_file_dump_enabled = 0;
	g_debug_console_initialized = initialized;
}

/* Sets g_debug_console_cursor_column and g_debug_console_cursor_row without checking
 * them against the 80 by 25 buffer. Nothing calls this. */
// FUNCTION: XVT 0x4ACC10
void debug_console_set_cursor_position(int column, int row)
{
	g_debug_console_cursor_column = column;
	g_debug_console_cursor_row = row;
}

#if defined(_MSC_VER) && _MSC_VER <= 1100
#pragma function(memcpy)
#endif
/* Writes text into an 80-column, 25-row buffer of character and color byte
 * pairs at g_debug_console_cursor_column and g_debug_console_cursor_row, writing the
 * character bytes only, and wraps at column 80. Before writing a row, when the
 * cursor row is past g_debug_console_scroll_bottom_row, it moves the rows from
 * g_debug_console_scroll_top_row + 1 to the bottom row up one, blanks the
 * characters of the bottom row and puts the cursor row there. A newline at the
 * start or end of text moves to column 0 of the next row; any other newline
 * moves down two rows. While g_debug_console_initialized is 0 it first fills the
 * buffer with blanks of color 7 and sets it to 1; while
 * g_debug_console_file_dump_enabled is set it first appends text to mpDump.txt.
 * Returns the cursor column after the text. The buffer is
 * g_debug_console_text_buffer in the original build and a static array in the
 * modern one; nothing in the engine reads either back. Its one caller,
 * render_texture_find_or_allocate_cache_entry, writes a warning when the texture
 * cache is full. */
// FUNCTION: XVT 0x4ACC30
int debug_console_write_text(const char *text)
{
	uint8_t *text_buffer;
	const char *text_cursor;
	int source_length;
	int remaining_length;
	int char_index;
	int zero;
	xvt_file *stream;
	uint8_t *text_cell;

#ifdef XVT_MODERN
	static uint8_t modern_text_buffer[80 * 25 * 2];
	text_buffer = modern_text_buffer;
#else
	text_buffer = g_debug_console_text_buffer;
#endif
	if (g_debug_console_initialized == 0) {
		int initialize_count;
		uint8_t *initialize_cell;

		initialize_cell = text_buffer;
		initialize_count = 2000;
		do {
			*initialize_cell++ = ' ';
			*initialize_cell++ = 7;
			--initialize_count;
		} while (initialize_count != 0);
		g_debug_console_initialized = 1;
	}

	if (g_debug_console_file_dump_enabled != 0) {
#ifdef XVT_MODERN
		stream = file_open("mpDump.txt", "a");
		text_cursor = text;
		if (stream != NULL) {
			file_write_bytes(stream, text, strlen(text));
			file_close(stream);
		}
#else
		stream = FILE_RAW_OPEN("mpDump.txt", "a");
		text_cursor = text;
		if (stream != NULL) {
			FILE_PRINTF(stream, "%s", text);
			FILE_RAW_CLOSE(stream);
		}
#endif
	} else {
		text_cursor = text;
	}

	if (*text_cursor == '\n') {
		++text_cursor;
		g_debug_console_cursor_column = 0;
		++g_debug_console_cursor_row;
	}

	zero = 0;
	for (;;) {
		remaining_length = strlen(text_cursor);
		source_length = remaining_length;
		if (g_debug_console_cursor_row >
		    g_debug_console_scroll_bottom_row) {
#ifdef XVT_MODERN
			memmove(&text_buffer[160 *
					     g_debug_console_scroll_top_row],
				&text_buffer
					[160 * g_debug_console_scroll_top_row +
					 160],
				(size_t)(160 *
					 (g_debug_console_scroll_bottom_row -
					  g_debug_console_scroll_top_row)));
#else
			memcpy(&text_buffer[160 *
					    g_debug_console_scroll_top_row],
			       &text_buffer
				       [160 * g_debug_console_scroll_top_row +
					160],
			       (size_t)(160 *
					(g_debug_console_scroll_bottom_row -
					 g_debug_console_scroll_top_row)));
#endif
			{
				int clear_count;
				uint8_t *clear_cell;

				clear_cell =
					&text_buffer
						[160 *
						 g_debug_console_scroll_bottom_row];
				clear_count = 80;
				do {
					*clear_cell = ' ';
					++clear_cell;
					++clear_cell;
					--clear_count;
				} while (clear_count != 0);
			}
			g_debug_console_cursor_row =
				g_debug_console_scroll_bottom_row;
		}

		if (remaining_length + g_debug_console_cursor_column > 80) {
			remaining_length = 80 - g_debug_console_cursor_column;
		}

		char_index = zero;
		text_cell = &text_buffer[2 * (g_debug_console_cursor_column +
					      80 * g_debug_console_cursor_row)];
		if (remaining_length > 0) {
			for (;;) {
				if (text_cursor[char_index] == '\n') {
					text_cursor += char_index + 1;
					source_length -= char_index + 1;
					remaining_length = zero;
					g_debug_console_cursor_column = zero;
					++g_debug_console_cursor_row;
					break;
				}
				text_cell[char_index * 2] =
					(uint8_t)text_cursor[char_index];
				++char_index;
				if (remaining_length > char_index) {
					continue;
				}
				break;
			}
		}

		g_debug_console_cursor_column += remaining_length;
		if (remaining_length == source_length) {
			return g_debug_console_cursor_column;
		}
		text_cursor += remaining_length;
		g_debug_console_cursor_column = zero;
		++g_debug_console_cursor_row;
	}
}
#if defined(_MSC_VER) && _MSC_VER <= 1100
#pragma intrinsic(memcpy)
#endif

/* Writes text with debug_console_write_text in a scroll region of rows top_row to
 * bottom_row, starting on bottom_row; when the cursor column is 0 it starts one
 * row below instead, so the write first scrolls the region up. Then sets the
 * region back to rows 0 to 24 and leaves the cursor where the write left it.
 * Nothing calls this. */
// FUNCTION: XVT 0x4ACDD0
void debug_console_write_text_in_scroll_region(int top_row, int bottom_row,
					       const char *text)
{
	g_debug_console_scroll_bottom_row = bottom_row;
	g_debug_console_scroll_top_row = top_row;
	g_debug_console_cursor_row = bottom_row;
	if (g_debug_console_cursor_column == 0) {
		g_debug_console_cursor_row = bottom_row + 1;
	}
	debug_console_write_text(text);
	g_debug_console_scroll_top_row = 0;
	g_debug_console_scroll_bottom_row = 24;
}

/* Sets g_debug_console_file_dump_enabled to 0 when it is nonzero. Otherwise adds 1
 * to it when one of the three gates is nonzero; the third starts at 1 and never
 * changes, so it always does. Nothing calls this. */
// FUNCTION: XVT 0x4ACE20
void debug_console_toggle_file_dump(void)
{
	if (g_debug_console_file_dump_enabled != 0) {
		g_debug_console_file_dump_enabled = 0;
		return;
	}

	if (g_debug_console_file_dump_gate0 != 0 ||
	    g_debug_console_file_dump_gate1 != 0 ||
	    g_debug_console_file_dump_gate2 != 0) {
		++g_debug_console_file_dump_enabled;
	}
}
