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

/* The face data block of the built-in streak model, laid out as the renderer
 * reads an OPT_FACEDATA payload: the edge count, then per face its record, its
 * normal and its texture axes. */
typedef struct HyperspaceFacePayload {
	int edgeCount; /* Edge count the renderer takes for the mesh: 4. */
	OptPackedFaceRecord face; /* The one face: its index lists. */
	OptVector faceNormal;	  /* The face's normal, (0, 0, 1). */
	/* Texture axes, (1, 0, 0) and (0, 1, 0). */
	OptVector textureGradients[2];
} HyperspaceFacePayload;

/* The built-in model a hyperspace streak is drawn with: one textured quad as a
 * group of four OPT nodes, all in one block. */
typedef struct HyperspaceStreakEmbeddedModelData {
	/* Vertex node: the 4 corners in g_hyperspaceStreakQuadVertices. */
	OptNode verticesNode;
	OptTexCoord texCoords[4]; /* The corners' texture coordinates. */
	/* Texture coordinate node: the 4 in texCoords. */
	OptNode texCoordsNode;
	OptVector normal;      /* The one vertex normal, (0, 0, 1). */
	int normalNodePadding; /* 0; not used by name. */
	OptNode normalsNode;   /* Vertex normal node: the 1 in normal. */
	/* The face data the face node points at. */
	HyperspaceFacePayload facePayload;
	OptNode faceNode;	/* Face data node: 1 face, facePayload. */
	OptNode *childNodes[4]; /* The four nodes above, in order. */
	OptNode rootNode;	/* Group node holding childNodes. */
	OptNode *rootNodes[1];	/* Points at rootNode: the model's root list. */
	int trailingPadding;	/* 0; not used by name. */
} HyperspaceStreakEmbeddedModelData;

/* The streak quad's four corners: x is plus or minus the streak's half width, y
 * runs from 0 to the stretched length. Starts with x at 64 or -64 and y at 0 or
 * -256; FlightHyperspace_RenderTransitionEffect, its only writer, sets the x of
 * all four and the y of corners 1 and 2 before each streak is drawn. */
// GLOBAL: XVT 0x51C340
OptVector g_hyperspaceStreakQuadVertices[4] = {
	{64.0f, 0.0f, 0.0f},
	{64.0f, -256.0f, 0.0f},
	{-64.0f, -256.0f, 0.0f},
	{-64.0f, 0.0f, 0.0f},
};
/* The built-in streak model: one quad over g_hyperspaceStreakQuadVertices, set
 * up in its initializer; nothing writes it by name. */
// GLOBAL: XVT 0x51C370
HyperspaceStreakEmbeddedModelData g_hyperspaceStreakEmbeddedModelData = {
	{NULL, OPT_MESHVERTS, 0, NULL, 4, g_hyperspaceStreakQuadVertices},
	{{1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}, {0.0f, 0.0f}},
	{NULL, OPT_TEXCOORDS, 0, NULL, 4,
	 g_hyperspaceStreakEmbeddedModelData.texCoords},
	{0.0f, 0.0f, 1.0f},
	0,
	{NULL, OPT_VERTNORMALS, 0, NULL, 1,
	 &g_hyperspaceStreakEmbeddedModelData.normal},
	{
		4,
		{{0, 1, 2, 3}, {0, 1, 2, 3}, {0, 0, 0, 0}, {0, 1, 2, 3}},
		{0.0f, 0.0f, 1.0f},
		{{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
	},
	{NULL, OPT_FACEDATA, 0, NULL, 1,
	 &g_hyperspaceStreakEmbeddedModelData.facePayload},
	{
		&g_hyperspaceStreakEmbeddedModelData.verticesNode,
		&g_hyperspaceStreakEmbeddedModelData.texCoordsNode,
		&g_hyperspaceStreakEmbeddedModelData.normalsNode,
		&g_hyperspaceStreakEmbeddedModelData.faceNode,
	},
	{NULL, OPT_GROUP, 4, g_hyperspaceStreakEmbeddedModelData.childNodes, 4,
	 g_hyperspaceStreakEmbeddedModelData.childNodes},
	{&g_hyperspaceStreakEmbeddedModelData.rootNode},
	0,
};
/* Model header that FlightHyperspace_DrawTransitionEffectObject lays over the
 * loaded model of object type 137 while it draws a streak, so the renderer
 * draws the built-in streak model (one root,
 * g_hyperspaceStreakEmbeddedModelData.rootNodes); that function, its only
 * writer, sets selfMarker to the model it covers. */
// GLOBAL: XVT 0x51C498
OptimizedPolyObject g_hyperspaceModelHeaderPatch = {
	&g_hyperspaceModelHeaderPatch, 0, 1,
	g_hyperspaceStreakEmbeddedModelData.rootNodes};

/* 1 when FlightHyperspace_RenderTransitionEffect is to place new streaks and
 * play the entry sound. Starts at 1;
 * FlightHyperspace_RequestTransitionEffectInitialization sets it and
 * FlightHyperspace_RenderTransitionEffect clears it. */
// GLOBAL: XVT 0x51C4A8
int g_hyperspaceTransitionEffectInitPending = 1;
/* 1 while the exit sound is still to play:
 * FlightHyperspace_RenderTransitionEffect, its only writer, sets it while the
 * transition is under 472 ticks (HYPERSPACE_STRETCH_PHASE_TICKS) and clears it
 * when it plays the exit sound after that. Starts at 1. */
// GLOBAL: XVT 0x51C4AC
int g_hyperspaceTransitionEffectSoundPending = 1;

/* Per streak, its offset from the camera in world Y: always 0x4000
 * (HYPERSPACE_FORWARD_OFFSET). Written only by
 * FlightHyperspace_RenderTransitionEffect, when it places the streaks. */
// GLOBAL: XVT 0x550C80
int g_hyperspaceStreakOffsetY[1024] = {0};
/* Per streak, its offset from the camera in world Z: random, up to 10,239
 * either way. Written only by FlightHyperspace_RenderTransitionEffect, when it
 * places the streaks. */
// GLOBAL: XVT 0x551C80
int g_hyperspaceStreakOffsetZ[1024] = {0};
/* Per streak, half its width: 1 plus the average of its X and Z offset sizes
 * shifted right 7. Written only by FlightHyperspace_RenderTransitionEffect,
 * when it places the streaks. */
// GLOBAL: XVT 0x552C80
int g_hyperspaceStreakHalfWidth[1024] = {0};
/* Per streak, its offset from the camera in world X: random, up to 10,239
 * either way. Written only by FlightHyperspace_RenderTransitionEffect, when it
 * places the streaks. */
// GLOBAL: XVT 0x553C80
int g_hyperspaceStreakOffsetX[1024] = {0};
/* Per streak, its roll: the angle of its (X, Z) offset (trig2_arctan) plus a
 * quarter turn, 0x4000 (a full circle is 65,536). Written only by
 * FlightHyperspace_RenderTransitionEffect, when it places the streaks. */
// GLOBAL: XVT 0x554C80
int g_hyperspaceStreakRollAngle[1024] = {0};

/* Draws object 0 (g_objectTable[0]) as one hyperspace streak: lays
 * g_hyperspaceModelHeaderPatch over the header of the loaded model of object
 * type 137, so the built-in streak model is drawn, sets
 * g_billboardObjectOrTypeIndex to 0, marks the object's orientation for
 * recomputing, sets its transform (FVIEW_SetObjectTransform), draws it
 * (RenderScene_DrawObjectModel) and puts the model's header back. Does not
 * check that the model's handle locked. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x424410
void FlightHyperspace_DrawTransitionEffectObject(void)
{
	OptimizedPolyObject *model;
	OptimizedPolyObject savedHeader;
	ObjectRecord *object;

	model = (OptimizedPolyObject *)Memory_LockHandle(
		g_loadedModels[HYPERSPACE_TRANSITION_OBJECT_TYPE]);
	memcpy(&savedHeader, model, sizeof(savedHeader));
	g_hyperspaceModelHeaderPatch.selfMarker = model;
	*model = g_hyperspaceModelHeaderPatch;
	g_billboardObjectOrTypeIndex = 0;
	object = g_objectTable;
	object->mobj->orientMatrixDirty = 1;
	FVIEW_SetObjectTransform(object->roll, object->pitch, object->yaw, 0,
				 object);
	RenderScene_DrawObjectModel(object);
	memcpy(model, &savedHeader, sizeof(savedHeader));
}

/* Sets g_hyperspaceTransitionEffectInitPending to 1; FlightView_Render calls it
 * while the local player's hyperspacePhase is 1. */
// FUNCTION: XVT 0x4244D0
void FlightHyperspace_RequestTransitionEffectInitialization(void)
{
	g_hyperspaceTransitionEffectInitPending = 1;
}

/* Draws the hyperspace streaks around the local player's camera: 1,024 with
 * hardware 3D, 512 in software. When g_hyperspaceTransitionEffectInitPending is
 * set it first plays the entry sound for the player's side (the alliance sound
 * for IFF 1, else the empire one) and places all 1,024 streaks at random (the
 * g_hyperspaceStreak arrays). Each streak is drawn as object 0, type 137, at
 * the camera plus its offset, pitched a quarter turn and rolled by its angle
 * (FlightHyperspace_DrawTransitionEffectObject). Until the local player's
 * hyperspaceRuntime.phaseElapsedTicks reach 472, a streak's length is the
 * square of a quarter of them and its world Y is lowered by 16 per tick; after
 * that its length is 16,000, its Y is lowered by 7,552 plus the square of twice
 * the ticks less 944, and the exit sound plays once. Object 0, its mobj and
 * g_bilinearEnabled, cleared while drawing, are put back after. The modern
 * build also hands the streaks to XvtRenderCapture_Hyperspace. */
// FUNCTION: XVT 0x4244E0
void FlightHyperspace_RenderTransitionEffect(void)
{
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
			fsfx_PlaySound(FLIGHT_SOUND_HYPERSPACE_ENTER_ALLIANCE,
				       -1, g_localPlayer);
		} else {
			fsfx_PlaySound(FLIGHT_SOUND_HYPERSPACE_ENTER_EMPIRE, -1,
				       g_localPlayer);
		}
		for (streakIndex = 0; streakIndex < HYPERSPACE_STREAK_COUNT;
		     ++streakIndex) {
			int randomX;
			int randomZ;
			int randomScale;
			int offsetX;
			int offsetZ;
			int averageRadius;

			do {
				randomX = rand() & HYPERSPACE_RANDOM_COORD_MASK;
				randomZ = rand() & HYPERSPACE_RANDOM_COORD_MASK;
				randomScale = (rand() &
					       HYPERSPACE_RANDOM_SCALE_MASK) +
					      HYPERSPACE_RANDOM_SCALE_BASE;
				offsetX = (randomX * randomScale) >>
					  HYPERSPACE_RANDOM_COORD_SHIFT;
				offsetZ = (randomZ * randomScale) >>
					  HYPERSPACE_RANDOM_COORD_SHIFT;
				averageRadius = (offsetX + offsetZ) >> 1;
			} while (averageRadius < HYPERSPACE_MINIMUM_RADIUS);

			g_hyperspaceStreakHalfWidth[streakIndex] =
				(averageRadius >>
				 (HYPERSPACE_LENGTH_SHIFT - 1)) +
				1;
			if ((rand() & HYPERSPACE_RANDOM_SIGN_MASK) != 0) {
				g_hyperspaceStreakOffsetX[streakIndex] =
					offsetX;
			} else {
				g_hyperspaceStreakOffsetX[streakIndex] =
					-offsetX;
			}
			if ((rand() & HYPERSPACE_RANDOM_SIGN_MASK) != 0) {
				g_hyperspaceStreakOffsetZ[streakIndex] =
					offsetZ;
			} else {
				g_hyperspaceStreakOffsetZ[streakIndex] =
					-offsetZ;
			}
			g_hyperspaceStreakOffsetY[streakIndex] =
				HYPERSPACE_FORWARD_OFFSET;
			g_hyperspaceStreakRollAngle[streakIndex] =
				(uint16_t)trig2_arctan(
					g_hyperspaceStreakOffsetZ[streakIndex],
					g_hyperspaceStreakOffsetX
						[streakIndex]) +
				HYPERSPACE_ROLL_OFFSET;
		}
		g_hyperspaceTransitionEffectInitPending = 0;
	}

#ifdef XVT_MODERN
	XvtRenderCapture_Hyperspace(
		(unsigned)streakCount, g_hyperspaceStreakOffsetX,
		g_hyperspaceStreakOffsetY, g_hyperspaceStreakOffsetZ,
		g_hyperspaceStreakHalfWidth, g_hyperspaceStreakRollAngle);
#endif
	for (streakIndex = 0; streakIndex < streakCount; ++streakIndex) {
		unsigned int phaseElapsedTicks;
		int streakLength;

		g_objectTable->world_x =
			g_players[g_localPlayer].viewState.cameraWorldX +
			g_hyperspaceStreakOffsetX[streakIndex];
		g_objectTable->world_y =
			g_players[g_localPlayer].viewState.cameraWorldY +
			g_hyperspaceStreakOffsetY[streakIndex];
		g_objectTable->world_z =
			g_players[g_localPlayer].viewState.cameraWorldZ +
			g_hyperspaceStreakOffsetZ[streakIndex];
		g_objectTable->objectType = HYPERSPACE_TRANSITION_OBJECT_TYPE;
		g_objectTable->genusId = CRAFT_GENUS_OTHER_PROJECTILE;
		g_objectTable->roll =
			(int16_t)g_hyperspaceStreakRollAngle[streakIndex];
		g_objectTable->yaw = 0;
		/* HYPERSPACE_FORWARD_OFFSET is reused here as an angle: a quarter turn of pitch. */
		g_objectTable->pitch = HYPERSPACE_FORWARD_OFFSET;

		streakLength = g_hyperspaceStreakHalfWidth[streakIndex];
		g_hyperspaceStreakQuadVertices[0].x = (float)streakLength;
		g_hyperspaceStreakQuadVertices[1].x =
			g_hyperspaceStreakQuadVertices[0].x;
		g_hyperspaceStreakQuadVertices[2].x = (float)-streakLength;
		g_hyperspaceStreakQuadVertices[3].x =
			g_hyperspaceStreakQuadVertices[2].x;

		phaseElapsedTicks =
			g_players[g_localPlayer]
				.hyperspaceRuntime.phaseElapsedTicks;
		if (phaseElapsedTicks < HYPERSPACE_STRETCH_PHASE_TICKS) {
			double stretchedLength;

			stretchedLength =
				(double)(int64_t)(uint32_t)(phaseElapsedTicks >>
							    2);
			stretchedLength *= stretchedLength;
			g_hyperspaceStreakQuadVertices[1].y =
				(float)stretchedLength;
			g_hyperspaceStreakQuadVertices[2].y =
				g_hyperspaceStreakQuadVertices[1].y;
			g_objectTable->world_y -= (int)(phaseElapsedTicks << 4);
			g_hyperspaceTransitionEffectSoundPending = 1;
		} else {
			int64_t stretchedPhaseTicks;
			double stretchOffset;

			if (g_hyperspaceTransitionEffectSoundPending != 0) {
				if (g_players[g_localPlayer].iff ==
				    HYPERSPACE_ALLIANCE_IFF) {
					fsfx_PlaySound(
						FLIGHT_SOUND_HYPERSPACE_EXIT_ALLIANCE,
						-1, g_localPlayer);
				} else {
					fsfx_PlaySound(
						FLIGHT_SOUND_HYPERSPACE_EXIT_EMPIRE,
						-1, g_localPlayer);
				}
				g_hyperspaceTransitionEffectSoundPending = 0;
			}
			g_hyperspaceStreakQuadVertices[1].y =
				fullyStretchedLength;
			g_hyperspaceStreakQuadVertices[2].y =
				fullyStretchedLength;
			g_objectTable->world_y -=
				HYPERSPACE_STRETCH_WORLD_OFFSET;
			stretchedPhaseTicks =
				(int64_t)(uint32_t)(g_players[g_localPlayer]
								    .hyperspaceRuntime
								    .phaseElapsedTicks *
							    2 -
						    HYPERSPACE_STRETCH_TIME_OFFSET);
			stretchOffset = (double)stretchedPhaseTicks *
					(double)stretchedPhaseTicks;
			g_objectTable->world_y -= (int)stretchOffset;
		}
		FlightHyperspace_DrawTransitionEffectObject();
	}

	*g_objectTable = savedObject;
	*g_objectTable->mobj = savedMobileObject;
	g_bilinearEnabled = savedBilinearEnabled;
}
