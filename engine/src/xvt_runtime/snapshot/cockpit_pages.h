#ifndef XVT_RUNTIME_SNAPSHOT_COCKPIT_PAGES_H
#define XVT_RUNTIME_SNAPSHOT_COCKPIT_PAGES_H
#include "xvt_runtime/snapshot/cockpit_state.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum XvtCockpitPageSection {
	XVT_COCKPIT_PAGE_HEADER,
	XVT_COCKPIT_PAGE_BODY,
	XVT_COCKPIT_PAGE_SECTION_COUNT
} XvtCockpitPageSection;

void XvtCockpitPages_Reset(void);
void XvtCockpitPages_ResetWorking(void);
void XvtCockpitPages_BeginFrame(void);
void XvtCockpitPages_SetOrigin(unsigned page, int x, int y);
void XvtCockpitPages_Clear(unsigned page);
void XvtCockpitPages_BeginSection(unsigned page, XvtCockpitPageSection section);
void XvtCockpitPages_EndSection(void);
void XvtCockpitPages_RecordGlyph(unsigned character, unsigned advance, unsigned height, int narrow);
void XvtCockpitPages_RecordRow(uint32_t key, int selected);
void XvtCockpitPages_RecordBackground(unsigned page);
void XvtCockpitPages_RecordBorder(unsigned page);
void XvtCockpitPages_RecordScroll(unsigned page, int first_row, int total_rows, int selected_row);
void XvtCockpitPages_RecordMode(unsigned page, unsigned mode);
void XvtCockpitPages_Latch(unsigned page);
void XvtCockpitPages_Export(XvtCockpitState* state);
#ifdef __cplusplus
}
#endif
#endif
