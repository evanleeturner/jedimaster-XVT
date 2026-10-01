#ifndef XVT_MOUSE_PAGE_H
#define XVT_MOUSE_PAGE_H
#include "xvt_app/settings/settings.h"
/* The Mouse page: edits a copy of the current mouse options (mouse flight on or off, sensitivity 1 to
 * 9, invert Y) under a help text on the button roles and the Ctrl+Alt+M release chord; Restore Defaults
 * takes the shipped options. Any change is stored at once as the user's mouse options, in memory (a
 * rejection is reported to the menu). input is unused. */
void XvtMousePage_Draw(AeronUiContext* ui, const AeronInputSnapshot* input);
#endif
