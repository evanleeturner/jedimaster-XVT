/* What opt_dump.c and opt_draw.c share: the draw kind, kept in a file of its
 * own. */
#ifndef XVT_TOOLS_OPT_DUMP_H
#define XVT_TOOLS_OPT_DUMP_H

/* Routes SDL's log lines through the draw kind's reader of the drawing code's
 * render.mesh_dropped lines; every other line goes on to SDL's own output. */
void opt_draw_capture_log(void);

/* Prints the draw sheet's part for one model, game name name resolved to
 * relative; returns 0 when a pass's walk differs from the engine's, else 1
 * (a model the reader refuses prints not_loaded and returns 1). */
int opt_draw_dump(const char *name, const char *relative);

#endif
