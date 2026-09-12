#ifndef XVT_RENDER_SCENE_BILLBOARD_H
#define XVT_RENDER_SCENE_BILLBOARD_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct SceneBillboardQueueEntry {
	uint16_t objectOrTypeIndex;
	int16_t frame;
	int16_t screenSize;
	int16_t screenX;
	int16_t screenY;
	int depthZ;
	int16_t rotationAngle;
};

extern uint16_t g_billboardModelNodeSwitchIndex;
extern uint16_t g_billboardTargetSelectionState;
extern uint16_t g_billboardObjectOrTypeIndex;
extern int16_t g_sceneBillboardQueueCount;

void SceneBillboard_QueueObjectTextured(int objectIndex);
void SceneBillboard_QueueProjectedTextured(int objectOrTypeIndex, int frame, int screenSize, int screenX,
										   int screenY, int depthZ, int rotationAngle);
void SceneBillboard_RenderQueuedTextured(int16_t drawTargetMarkers);
void RenderBillboard_DrawRollAlignedObjectModel(uint16_t objectIndex);
int SceneBillboard_ComputeProjectedSize(int depthZ, uint16_t modelMaxExtent, uint16_t baseScreenSize);

#ifdef __cplusplus
}
#endif

#endif
