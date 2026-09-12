#ifndef XVT_REMASTER_UI_DRAW_H
#define XVT_REMASTER_UI_DRAW_H
#include "aeron/scene/draw_list2d.h"
#include "xvt_remaster/assets.h"
#ifdef __cplusplus
extern "C" {
#endif
void XvtUi_Color(uint32_t argb, float out[4]);
void XvtUi_Glyph(AeronDrawList2D* list, const XvtSnapGlyph* glyph, float scale, float ox, float oy);
void XvtUi_Text(AeronDrawList2D* list, uint64_t font, const char* text, float x, float y, float scale,
				uint32_t color, int centered);
int XvtUi_MapIcon(AeronDrawList2D* list, AeronCommandBuffer* cmd, uint64_t asset_id, unsigned frame,
				  int remap, const uint32_t palette[256], float x, float y, float scale, int width,
				  int height);
void XvtUi_Paint(AeronDrawList2D* list, const XvtSnapPaint* paint, float scale);
int XvtUi_CopyFrontend(AeronCommandBuffer* cmd, AeronRenderTarget* dst, AeronTexture* src,
					   const XvtSnapRect* from, const XvtSnapRect* to, int source_width, int source_height);
void XvtUi_Shutdown(void);
#ifdef __cplusplus
}
#endif
#endif
