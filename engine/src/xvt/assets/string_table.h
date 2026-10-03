#ifndef XVT_ASSETS_STRING_TABLE_H
#define XVT_ASSETS_STRING_TABLE_H

#include "xvt/assets/file.h"
#include "xvt/xvt_typedefs.h"
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

extern char *g_str_file_error_messages[4];

void string_table_load_game_strings(int load_from_disk);
int string_table_read_non_comment_line(xvt_file *stream, char *buffer);

#ifdef __cplusplus
}
#endif

#endif
