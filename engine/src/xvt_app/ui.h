#ifndef XVT_APP_UI_H
#define XVT_APP_UI_H
#include "aeron/aeron.h"
#include "aeron/scene/font_atlas.h"
#include "aeron/scene/ui.h"
#include <stdbool.h>

/* The application-owned UI: one Aeron UI context with one font atlas serving as both the regular and
 * the title font, in a dark theme with a red accent. */
struct XvtAppUi {
	AeronUiContext *context;
	AeronFontAtlas font;
};

/* Zeroes ui, loads RESOURCE/<font>.fnt and .png as the atlas through a command buffer (each file at
 * most 64 MiB), creates the context, applies the theme and sets the fonts. Returns false, writing error
 * when it has capacity, for a NULL ui or a NULL or empty font, a command buffer that cannot be
 * acquired, an atlas that fails to load (the buffer is cancelled), a submission that fails, or a
 * context that cannot be created (the atlas is released). */
bool XvtAppUi_Init(struct XvtAppUi *ui, const char *font, char *error,
		   size_t capacity);
/* Destroys the context, releases the atlas and zeroes ui; nothing for NULL. */
void XvtAppUi_Shutdown(struct XvtAppUi *ui);
/* The context; NULL for a NULL or zeroed ui. */
AeronUiContext *XvtAppUi_Context(struct XvtAppUi *ui);
#endif
