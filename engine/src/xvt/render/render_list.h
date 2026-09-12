#ifndef XVT_RENDER_RENDER_LIST_H
#define XVT_RENDER_RENDER_LIST_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct RenderObjectListEntry {
	int sortDepth;
	int objectIdx;
	struct RenderObjectListEntry* next;
};

extern RenderObjectListEntry* g_renderListHead;
extern RenderObjectListEntry* g_renderObjectListEntries;

void RenderList_QueueObject(int objectIdx, int sortDepth);
void RenderList_Reset(void);
int RenderList_ProjectObjectBoundsForCulling(int objectIdx, unsigned int boundsRadius, int playerIdx);
void RenderList_SortDepthDescending(void);
void RenderList_SortDepthAscending(void);

#ifdef __cplusplus
}
#endif

#endif
