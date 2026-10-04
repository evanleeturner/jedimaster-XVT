#ifndef XVT_APP_INSTALLATION_PAGE_H
#define XVT_APP_INSTALLATION_PAGE_H
#include <stdbool.h>

#include "aeron/scene/ui.h"
/* The Original Installation section of the Game page: an editable path field
 * with Browse, a directory picker, and the accepted path. An edited path is
 * validated and stored as game_data when the menu closes; the running game
 * keeps the installation it mounted, so a change needs a restart. Static state:
 * the picker, the edited path and the accepted path. */
/* Creates the picker; false with "Cannot create installation picker" when it cannot be. */
bool xvt_installation_page_init(char *error, size_t capacity);
/* Destroys the picker. */
void xvt_installation_page_shutdown(void);
/* Resets the edited and accepted paths to the game_data setting, or to the
 * mounted installation when that is empty. */
void xvt_installation_page_open(void);
/* Cancels an open picker. */
void xvt_installation_page_cancel_picker(void);
/* Whether the picker is open. */
bool xvt_installation_page_picker_open(void);
/* Draws the header and the editable path field, whose Browse opens the picker
 * at the edited path (a failure is reported to the menu); when the edited path
 * differs from the mounted installation, also the current installation and
 * "Requires restart.". input is unused. */
void xvt_installation_page_draw(AeronUiContext *ui,
				const AeronInputSnapshot *input);
/* Draws the picker; a selection is resolved into the edited path (which changes
 * only on success), and a resolution or picker error is reported to the
 * menu. */
void xvt_installation_page_draw_picker(AeronUiContext *ui);
/* Nothing when the edited path equals the accepted one. Otherwise resolves it
 * in place and stores it as game_data in memory; on success it becomes the
 * accepted path. false with error on either failure. */
bool xvt_installation_page_flush(char *error, size_t capacity);
#endif
