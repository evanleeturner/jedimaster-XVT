#ifndef XVT_RUNTIME_SNAPSHOT_RENDER_ASSETS_H
#define XVT_RUNTIME_SNAPSHOT_RENDER_ASSETS_H

#include "xvt_runtime/snapshot/render_snapshot.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void XvtRenderAssets_Init(void);
void XvtRenderAssets_Shutdown(void);
void XvtRenderAssets_BeginTick(void);
void XvtRenderAssets_Export(XvtRenderSnapshot* snapshot);
void XvtRenderAssets_Consumed(uint64_t tick);
void XvtRenderAssets_RegisterOpt(uint16_t handle, const char* path);
void XvtRenderAssets_RegisterTexture(uint16_t handle, const char* path);
void XvtRenderAssets_BindType(uint16_t type, uint16_t handle);
void XvtRenderAssets_FreeHandle(unsigned int handle);
void XvtRenderAssets_ClearMission(void);
uint64_t XvtRenderAssets_HandleId(uint16_t handle);
uint64_t XvtRenderAssets_ImageId(const void* owner);
void XvtRenderAssets_FreeImage(const void* owner);
struct ImageResource;

typedef struct XvtFrontendImageColors {
	uint16_t color_lut[256];
	uint8_t pixel_format_555;
} XvtFrontendImageColors;

void XvtRenderAssets_RegisterFrontendImage(const struct ImageResource* image, const char* path,
										   int make_palette, int pixel_format_555);
/* Host-thread asset preparation copies registry-owned data before marking the frame consumed. */
int XvtRenderAssets_CopyFrontendColors(uint64_t id, XvtFrontendImageColors* colors);
uint64_t XvtRenderAssets_RegisterImage(const void* owner, uint16_t handle, const char* path,
									   XvtSnapImageKind kind, uint32_t first, uint32_t count,
									   uint16_t point_size, uint8_t row_bytes, int make_palette);
/* Original pointer keys are used only during capture, never by the remaster. */
void XvtRenderAssets_CaptureCockpit(int auxiliary);
void XvtRenderAssets_RegisterLfd(const char* path, uint8_t** entries);
void XvtRenderAssets_RegisterPanel(const char* path, uint16_t first_sprite, uint16_t count, uint16_t skip);
void XvtRenderAssets_RegisterIcons(const char* path, uint8_t** frames, uint16_t count);
void XvtRenderAssets_RegisterFlightFonts(void);
uint64_t XvtRenderAssets_MapIconFrame(unsigned index, uint32_t* frame);
const uint8_t* XvtRenderAssets_DefaultCursor(void);

#ifdef __cplusplus
}
#endif
#endif
