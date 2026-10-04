#ifndef XVT_UTIL_DEBUG_CONSOLE_H
#define XVT_UTIL_DEBUG_CONSOLE_H

#include <stdint.h>

#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

extern int g_debug_console_initialized;
#ifndef XVT_MODERN
extern uint8_t g_debug_console_text_buffer[80 * 25 * 2];
#endif

void debug_printf(const char *format, ...);
void debug_console_set_initialized(int initialized);
void debug_console_set_cursor_position(int column, int row);
int debug_console_write_text(const char *text);
void debug_console_write_text_in_scroll_region(int top_row, int bottom_row,
					       const char *text);
void debug_console_toggle_file_dump(void);

#ifdef __cplusplus
}
#endif

#endif
