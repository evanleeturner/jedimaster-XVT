#ifndef XVT_RENDER_SCENE_BILLBOARD_H
#define XVT_RENDER_SCENE_BILLBOARD_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct SceneBillboardQueueEntry {
	/* Object or type index RenderQuad_DrawModelTexture draws for. */
	uint16_t objectOrTypeIndex;
	/* Texture frame, 0x8000 to 0xFEFF from
	 * SceneBillboard_DrawOrQueueObject. */
	int16_t frame;
	/* Size, before RenderQuad_DrawModelTexture scales it by depth. */
	int16_t screenSize;
	int16_t screenX; /* Projected X on the viewport. */
	/* Projected Y, from SceneBillboard_DrawOrQueueObject counted up from
	 * the viewport's bottom. */
	int16_t screenY;
	int depthZ; /* View depth; the queue is drawn largest first. */
	int16_t rotationAngle; /* Roll on screen, in angle units. */
};

extern uint16_t g_billboardModelNodeSwitchIndex;
extern uint16_t g_billboardTargetSelectionState;
extern uint16_t g_billboardObjectOrTypeIndex;
extern int16_t g_sceneBillboardQueueCount;

void SceneBillboard_DrawOrQueueObject(int objectIndex);
void SceneBillboard_QueueProjectedTextured(int objectOrTypeIndex, int frame,
					   int screenSize, int screenX,
					   int screenY, int depthZ,
					   int rotationAngle);
void SceneBillboard_RenderQueuedTextured(int16_t drawTargetMarkers);
void SceneBillboard_DrawRollAlignedObjectModel(uint16_t objectIndex);
int SceneBillboard_ComputeProjectedSize(int depthZ, uint16_t modelMaxExtent,
					uint16_t baseScreenSize);

#ifdef __cplusplus
}
#endif

#endif
