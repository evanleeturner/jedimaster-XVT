#ifndef XVT_REMASTER_COCKPIT_LOADING_H
#define XVT_REMASTER_COCKPIT_LOADING_H
#include "xvt_runtime/snapshot/render_snapshot.h"
int XvtCockpitLoading_Prepare(const XvtRenderSnapshot* snapshot, int width, int height);
void XvtCockpitLoading_Reset(void);
#endif
