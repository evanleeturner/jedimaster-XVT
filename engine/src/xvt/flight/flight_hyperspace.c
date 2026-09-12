#include "xvt/flight/flight_hyperspace.h"
#ifdef XVT_MODERN
#include "xvt_runtime/snapshot/render_capture.h"
#endif

#include "xvt/assets/opt_model.h"
#include "xvt/audio/fsfx.h"
#include "xvt/flight/fview.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/math/trig2.h"
#include "xvt/render/render_scene.h"
#include "xvt/render/renderer.h"
#include "xvt/render/scene_billboard.h"
#include "xvt/util/memory.h"
#include <stdlib.h>

enum {
	HYPERSPACE_TRANSITION_OBJECT_TYPE = 137,
};

typedef struct HyperspaceFacePayload {
	int edgeCount;
	OptPackedFaceRecord face;
	OptVector faceNormal;
	OptVector textureGradients[2];
} HyperspaceFacePayload;

typedef struct HyperspaceStreakEmbeddedModelData {
	OptNode verticesNode;
	OptTexCoord texCoords[4];
	OptNode texCoordsNode;
	OptVector normal;
	int normalNodePadding;
	OptNode normalsNode;
	HyperspaceFacePayload facePayload;
	OptNode faceNode;
	OptNode* childNodes[4];
	OptNode rootNode;
	OptNode* rootNodes[1];
	int trailingPadding;
} HyperspaceStreakEmbeddedModelData;

// GLOBAL: XVT 0x51C340
OptVector g_hyperspaceStreakQuadVertices[4] = {
	{ 64.0f, 0.0f, 0.0f },
	{ 64.0f, -256.0f, 0.0f },
	{ -64.0f, -256.0f, 0.0f },
	{ -64.0f, 0.0f, 0.0f },
};
// GLOBAL: XVT 0x51C370
HyperspaceStreakEmbeddedModelData g_hyperspaceStreakEmbeddedModelData = {
	{ NULL, OPT_MESHVERTS, 0, NULL, 4, g_hyperspaceStreakQuadVertices },
	{ { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f }, { 0.0f, 0.0f } },
	{ NULL, OPT_TEXCOORDS, 0, NULL, 4, g_hyperspaceStreakEmbeddedModelData.texCoords },
	{ 0.0f, 0.0f, 1.0f },
	0,
	{ NULL, OPT_VERTNORMALS, 0, NULL, 1, &g_hyperspaceStreakEmbeddedModelData.normal },
	{
		4,
		{ { 0, 1, 2, 3 }, { 0, 1, 2, 3 }, { 0, 0, 0, 0 }, { 0, 1, 2, 3 } },
		{ 0.0f, 0.0f, 1.0f },
		{ { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
	},
	{ NULL, OPT_FACEDATA, 0, NULL, 1, &g_hyperspaceStreakEmbeddedModelData.facePayload },
	{
		&g_hyperspaceStreakEmbeddedModelData.verticesNode,
		&g_hyperspaceStreakEmbeddedModelData.texCoordsNode,
		&g_hyperspaceStreakEmbeddedModelData.normalsNode,
		&g_hyperspaceStreakEmbeddedModelData.faceNode,
	},
	{ NULL, OPT_GROUP, 4, g_hyperspaceStreakEmbeddedModelData.childNodes, 4,
	  g_hyperspaceStreakEmbeddedModelData.childNodes },
	{ &g_hyperspaceStreakEmbeddedModelData.rootNode },
	0,
};
// GLOBAL: XVT 0x51C498
OptimizedPolyObject g_hyperspaceModelHeaderPatch = { &g_hyperspaceModelHeaderPatch, 0, 1,
													 g_hyperspaceStreakEmbeddedModelData.rootNodes };

// GLOBAL: XVT 0x51C4A8
int g_hyperspaceTransitionEffectInitPending = 1;
// GLOBAL: XVT 0x51C4AC
int g_hyperspaceTransitionEffectSoundPending = 1;

// GLOBAL: XVT 0x550C80
int g_hyperspaceStreakOffsetY[1024] = { 0 };
// GLOBAL: XVT 0x551C80
int g_hyperspaceStreakOffsetZ[1024] = { 0 };
// GLOBAL: XVT 0x552C80
int g_hyperspaceStreakLength[1024] = { 0 };
// GLOBAL: XVT 0x553C80
int g_hyperspaceStreakOffsetX[1024] = { 0 };
// GLOBAL: XVT 0x554C80
int g_hyperspaceStreakRollAngle[1024] = { 0 };

// FLAGS: /O2 /G5
// FUNCTION: XVT 0x424410
void FlightHyperspace_DrawTransitionEffectObject(void) {
	OptimizedPolyObject* model;
	OptimizedPolyObject savedHeader;
	ObjectRecord* object;

	model = (OptimizedPolyObject*)Memory_LockHandle(g_loadedModels[HYPERSPACE_TRANSITION_OBJECT_TYPE]);
	memcpy(&savedHeader, model, sizeof(savedHeader));
	g_hyperspaceModelHeaderPatch.selfMarker = model;
	*model = g_hyperspaceModelHeaderPatch;
	g_billboardObjectOrTypeIndex = 0;
	object = g_objectTable;
	object->mobj->orientMatrixDirty = 1;
	FVIEW_SetObjectTransform(object->roll, object->pitch, object->yaw, 0, object);
	RenderScene_DrawObjectModel(object);
	memcpy(model, &savedHeader, sizeof(savedHeader));
}

// FUNCTION: XVT 0x4244D0
void FlightHyperspace_RequestTransitionEffectInitialization(void) {
	g_hyperspaceTransitionEffectInitPending = 1;
}

// FUNCTION: XVT 0x4244E0
void FlightHyperspace_RenderTransitionEffect(void) {
	enum {
		HYPERSPACE_STREAK_COUNT = 1024,
		HYPERSPACE_SOFTWARE_STREAK_COUNT = 512,
		HYPERSPACE_RANDOM_COORD_MASK = 0x3FFF,
		HYPERSPACE_RANDOM_SCALE_MASK = 3,
		HYPERSPACE_RANDOM_SCALE_BASE = 2,
		HYPERSPACE_RANDOM_COORD_SHIFT = 3,
		HYPERSPACE_RANDOM_SIGN_MASK = 0x1000,
		HYPERSPACE_MINIMUM_RADIUS = 8,
		HYPERSPACE_LENGTH_SHIFT = 8,
		HYPERSPACE_FORWARD_OFFSET = 0x4000,
		HYPERSPACE_ROLL_OFFSET = 0x4000,
		HYPERSPACE_STRETCH_PHASE_TICKS = 0x1D8,
		HYPERSPACE_STRETCH_WORLD_OFFSET = 0x1D80,
		HYPERSPACE_STRETCH_TIME_OFFSET = 944,
		HYPERSPACE_ALLIANCE_IFF = 1,
	};

	const float fullyStretchedLength = 16000.0f;
	ObjectRecord savedObject;
	MobileObject savedMobileObject;
	int savedBilinearEnabled;
	int streakCount;
	int streakIndex;

	streakCount = HYPERSPACE_STREAK_COUNT;
	if (g_useHardware3D == 0) {
		streakCount = HYPERSPACE_SOFTWARE_STREAK_COUNT;
	}
	savedBilinearEnabled = g_bilinearEnabled;
	g_bilinearEnabled = 0;
	savedObject = *g_objectTable;
	savedMobileObject = *g_objectTable->mobj;

	if (g_hyperspaceTransitionEffectInitPending != 0) {
		if (g_players[g_localPlayer].iff == HYPERSPACE_ALLIANCE_IFF) {
			fsfx_PlaySound(FLIGHT_SOUND_HYPERSPACE_ENTER_ALLIANCE, -1, g_localPlayer);
		} else {
			fsfx_PlaySound(FLIGHT_SOUND_HYPERSPACE_ENTER_EMPIRE, -1, g_localPlayer);
		}
		for (streakIndex = 0; streakIndex < HYPERSPACE_STREAK_COUNT; ++streakIndex) {
			int randomX;
			int randomZ;
			int randomScale;
			int offsetX;
			int offsetZ;
			int averageRadius;

			do {
				randomX = rand() & HYPERSPACE_RANDOM_COORD_MASK;
				randomZ = rand() & HYPERSPACE_RANDOM_COORD_MASK;
				randomScale = (rand() & HYPERSPACE_RANDOM_SCALE_MASK) + HYPERSPACE_RANDOM_SCALE_BASE;
				offsetX = (randomX * randomScale) >> HYPERSPACE_RANDOM_COORD_SHIFT;
				offsetZ = (randomZ * randomScale) >> HYPERSPACE_RANDOM_COORD_SHIFT;
				averageRadius = (offsetX + offsetZ) >> 1;
			} while (averageRadius < HYPERSPACE_MINIMUM_RADIUS);

			g_hyperspaceStreakLength[streakIndex] = (averageRadius >> (HYPERSPACE_LENGTH_SHIFT - 1)) + 1;
			if ((rand() & HYPERSPACE_RANDOM_SIGN_MASK) != 0) {
				g_hyperspaceStreakOffsetX[streakIndex] = offsetX;
			} else {
				g_hyperspaceStreakOffsetX[streakIndex] = -offsetX;
			}
			if ((rand() & HYPERSPACE_RANDOM_SIGN_MASK) != 0) {
				g_hyperspaceStreakOffsetZ[streakIndex] = offsetZ;
			} else {
				g_hyperspaceStreakOffsetZ[streakIndex] = -offsetZ;
			}
			g_hyperspaceStreakOffsetY[streakIndex] = HYPERSPACE_FORWARD_OFFSET;
			g_hyperspaceStreakRollAngle[streakIndex] =
				(uint16_t)trig2_arctan(g_hyperspaceStreakOffsetZ[streakIndex],
									   g_hyperspaceStreakOffsetX[streakIndex]) +
				HYPERSPACE_ROLL_OFFSET;
		}
		g_hyperspaceTransitionEffectInitPending = 0;
	}

#ifdef XVT_MODERN
	XvtRenderCapture_Hyperspace((unsigned)streakCount, g_hyperspaceStreakOffsetX, g_hyperspaceStreakOffsetY,
								g_hyperspaceStreakOffsetZ, g_hyperspaceStreakLength,
								g_hyperspaceStreakRollAngle);
#endif
	for (streakIndex = 0; streakIndex < streakCount; ++streakIndex) {
		unsigned int phaseElapsedTicks;
		int streakLength;

		g_objectTable->world_x =
			g_players[g_localPlayer].viewState.savedTargetX + g_hyperspaceStreakOffsetX[streakIndex];
		g_objectTable->world_y =
			g_players[g_localPlayer].viewState.savedTargetY + g_hyperspaceStreakOffsetY[streakIndex];
		g_objectTable->world_z =
			g_players[g_localPlayer].viewState.savedTargetZ + g_hyperspaceStreakOffsetZ[streakIndex];
		g_objectTable->objectType = HYPERSPACE_TRANSITION_OBJECT_TYPE;
		g_objectTable->genusId = CRAFT_GENUS_OTHER_PROJECTILE;
		g_objectTable->roll = (int16_t)g_hyperspaceStreakRollAngle[streakIndex];
		g_objectTable->yaw = 0;
		g_objectTable->pitch = HYPERSPACE_FORWARD_OFFSET;

		streakLength = g_hyperspaceStreakLength[streakIndex];
		g_hyperspaceStreakQuadVertices[0].x = (float)streakLength;
		g_hyperspaceStreakQuadVertices[1].x = g_hyperspaceStreakQuadVertices[0].x;
		g_hyperspaceStreakQuadVertices[2].x = (float)-streakLength;
		g_hyperspaceStreakQuadVertices[3].x = g_hyperspaceStreakQuadVertices[2].x;

		phaseElapsedTicks = g_players[g_localPlayer].hyperspaceRuntime.phaseElapsedTicks;
		if (phaseElapsedTicks < HYPERSPACE_STRETCH_PHASE_TICKS) {
			double stretchedLength;

			stretchedLength = (double)(int64_t)(uint32_t)(phaseElapsedTicks >> 2);
			stretchedLength *= stretchedLength;
			g_hyperspaceStreakQuadVertices[1].y = (float)stretchedLength;
			g_hyperspaceStreakQuadVertices[2].y = g_hyperspaceStreakQuadVertices[1].y;
			g_objectTable->world_y -= (int)(phaseElapsedTicks << 4);
			g_hyperspaceTransitionEffectSoundPending = 1;
		} else {
			int64_t stretchedPhaseTicks;
			double stretchOffset;

			if (g_hyperspaceTransitionEffectSoundPending != 0) {
				if (g_players[g_localPlayer].iff == HYPERSPACE_ALLIANCE_IFF) {
					fsfx_PlaySound(FLIGHT_SOUND_HYPERSPACE_EXIT_ALLIANCE, -1, g_localPlayer);
				} else {
					fsfx_PlaySound(FLIGHT_SOUND_HYPERSPACE_EXIT_EMPIRE, -1, g_localPlayer);
				}
				g_hyperspaceTransitionEffectSoundPending = 0;
			}
			g_hyperspaceStreakQuadVertices[1].y = fullyStretchedLength;
			g_hyperspaceStreakQuadVertices[2].y = fullyStretchedLength;
			g_objectTable->world_y -= HYPERSPACE_STRETCH_WORLD_OFFSET;
			stretchedPhaseTicks =
				(int64_t)(uint32_t)(g_players[g_localPlayer].hyperspaceRuntime.phaseElapsedTicks * 2 -
									HYPERSPACE_STRETCH_TIME_OFFSET);
			stretchOffset = (double)stretchedPhaseTicks * (double)stretchedPhaseTicks;
			g_objectTable->world_y -= (int)stretchOffset;
		}
		FlightHyperspace_DrawTransitionEffectObject();
	}

	*g_objectTable = savedObject;
	*g_objectTable->mobj = savedMobileObject;
	g_bilinearEnabled = savedBilinearEnabled;
}
