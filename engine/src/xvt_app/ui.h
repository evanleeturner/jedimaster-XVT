#ifndef XVT_APP_UI_H
#define XVT_APP_UI_H
#include "aeron/aeron.h"
#include "aeron/scene/font_atlas.h"
#include "aeron/scene/ui.h"
#include <stdbool.h>

typedef struct XvtAppUi {
	AeronUiContext* context;
	AeronFontAtlas font;
} XvtAppUi;

bool XvtAppUi_Init(XvtAppUi* ui, const char* font, char* error, size_t capacity);
void XvtAppUi_Shutdown(XvtAppUi* ui);
AeronUiContext* XvtAppUi_Context(XvtAppUi* ui);
#endif
