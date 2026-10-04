#ifndef XVT_ASSETS_INVENTOR_ASCII_H
#define XVT_ASSETS_INVENTOR_ASCII_H

#include <stdint.h>
#include <stdio.h>

#include "xvt/assets/file.h"
#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef XVT_MODERN
struct inventor_node_def {
	/* Name as written in an Inventor file. A word read from the file
	 * matches when, ignoring case, it is this name or its start. */
	const char *node_name;
	/* Entries in field_defs; also the parsed node's record count. */
	int32_t field_count;
	/* The node's fields, in record order; NULL when there are none. */
	const struct inventor_field_def *const *field_defs;
};

void inventor_ascii_skip_past_open_brace(xvt_file *stream);
void inventor_ascii_skip_past_open_bracket(xvt_file *stream);
void inventor_ascii_skip_list_separator(xvt_file *stream);
void inventor_ascii_skip_past_quote(xvt_file *stream);
void inventor_ascii_skip_past_close_brace(xvt_file *stream);
void inventor_ascii_skip_past_close_bracket(xvt_file *stream);
int inventor_ascii_peek_next_is_quote(xvt_file *stream);
int inventor_ascii_peek_next_is_close_brace(xvt_file *stream);
int inventor_ascii_peek_next_is_open_brace(xvt_file *stream);
int inventor_ascii_peek_next_is_close_bracket(xvt_file *stream);
int inventor_ascii_peek_next_is_open_bracket(xvt_file *stream);
void inventor_ascii_skip_to_end_of_line(xvt_file *stream);
#endif

#ifdef __cplusplus
}
#endif

#endif
