/* What the asset dump tools share: the game's install bound as the asset
 * folder, the first lines of every sheet, quoted text, and the front end's
 * tables set up as the front end's start sets them. */
#ifndef XVT_TOOLS_ASSET_DUMP_H
#define XVT_TOOLS_ASSET_DUMP_H

#include <stddef.h>

/* Binds storage to root, with fresh user and temp folders under /tmp, and
 * looks names up in root in any letter case, as the game's setup does: a game
 * name resolves under root/BalanceOfPower first, then under root. program
 * starts every message the tool writes on stderr. Returns 0, printing why,
 * when a folder or the binding cannot be made. */
int asset_dump_bind_root(const char *program, const char *root);

/* Writes into relative, size bytes, the host path game_name resolves to,
 * relative to the root, the name's separators cleaned as the engine's file
 * opening cleans them. Returns 0, writing nothing, when the name does not
 * resolve. */
int asset_dump_resolve(const char *game_name, char *relative, size_t size);

/* Prints the sheet's first lines: the kind, the game name and the host path it
 * resolves to, relative to the root, the name's separators cleaned as the
 * engine's file opening cleans them. Returns 0, printing why on stderr, when
 * the name does not resolve. */
int asset_dump_print_header(const char *kind, const char *game_name);

/* Prints value quoted, at most size bytes, stopping at a NUL; a quote or
 * backslash is escaped and any byte outside printable ASCII prints as an
 * escape. */
void asset_dump_quote(const char *value, size_t size);

/* Sets up the front end's tables and display depth the way the front end's
 * start does (frontend_task.c): room for 128 sounds, 12 voices and 512
 * images, 16 bits per pixel in the 565 layout. Returns 0 when a table cannot
 * be allocated. */
int asset_dump_open_front_state(void);

#endif
