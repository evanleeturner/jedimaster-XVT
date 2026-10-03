#ifndef XVT_ASSETS_OPT_MODEL_H
#define XVT_ASSETS_OPT_MODEL_H

#include "xvt/assets/file.h"
#include "xvt/xvt_typedefs.h"
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef XVT_MODERN
#include <strings.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

struct ModelWeaponHardpoint {
	/* Side offset from the model's origin, the OPT hardpoint's x; readers
	 * pass x, z and y to pai_calcrotatedpoint as side, up and forward. */
	int16_t x;
	int16_t z; /* Up offset, the OPT hardpoint's z. */
	/* Forward offset: the OPT hardpoint's y negated, as
	 * ModelMesh_GetHardpoint gives it. */
	int16_t y;
	/* For a laser slot on a turret mesh, the index in that mesh of its next
	 * hardpoint of the same type; 0xFF when there is none.
	 * laser_fireturretslot fires from it instead while
	 * g_missionElapsedClock.subsecondTicks is odd, except on
	 * SUPER_STAR_DESTROYER_OBJECT_TYPE. Launcher slots keep the table's
	 * value. */
	uint8_t alternateMeshHardpointIdx;
	/* Mesh that carries the point; readers skip the slot while the craft's
	 * componentHp for that mesh is 0. */
	uint8_t meshIdx;
};

struct ModelLocalPoint {
	int side;    /* Side offset, the OPT hardpoint's x. */
	int up;	     /* Up offset, the OPT hardpoint's z. */
	int forward; /* Forward offset, the OPT hardpoint's y negated. */
};

struct ModelHangarPoints {
	struct ModelLocalPoint inside;	/* From the hardpoint of type 25. */
	struct ModelLocalPoint outside; /* From the hardpoint of type 26. */
};

struct ModelDef {
	/* Short name such as "X-W"; the HUD, radio messages and goal text show
	 * it. */
	char name[10];
	/* Full name such as "X-wing", shown by messages and goal text; NULL in
	 * the table until StringTable_LoadGameStrings sets it. */
	char *nameLong;
	/* Points the craft is worth; the scoring code multiplies it, by 40 in
	 * Mission_ComputeCraftPointValue. */
	uint8_t craftPointValue;
	/* Weight in rating awards: Mission_CreditDestructionDamageContributors
	 * multiplies a kill's rating points by the victim's weight and divides
	 * by the attacker's craft's; a victim with 0 gets the minimum award
	 * instead. */
	uint8_t ratingWeight;
	/* Copied into a player's boundCraftEngineGlowCount when the player's
	 * craft spawns; the game code never reads that copy, only the modern
	 * build's state records. */
	uint8_t engineGlowCount;
	/* 0 for a model without a hyperdrive: spawning then flips
	 * CRAFT_SUBSYSTEM_FLAG_HYPERDRIVE in the craft's systemFlags unless a
	 * spawn status is 9 or 16. The AI's orders check it before a hyperspace
	 * exit. */
	uint8_t hasHyperdrive;
	/* 0 for a model without shields: spawning then flips
	 * CRAFT_SUBSYSTEM_FLAG_SHIELDS and empties the shields unless a spawn
	 * status is 8 or 16, and BuildCraftTechStats rates the shields 0. */
	uint8_t hasShields;
	/* Half the most each of the craft's two shield energies holds:
	 * Craft_GetObjectMaxShield returns 2 times it. BuildCraftTechStats
	 * rates it divided by 50. */
	int shieldStrength;
	/* Hits paiman_attackmaneuver lets the craft take before it breaks off;
	 * halved with the front shield under an eighth, 1 at
	 * systemDamageHullThreshold, both only for a craft with shields. */
	uint8_t reactionThreshold;
	/* Nothing reads or writes it by name. */
	uint8_t modelClassFlag; ///< Static 0/1 model-class discriminator; exact runtime behavior is not yet
	///< identified.
	/* Hull strength: copied into the craft's hullMax at spawn;
	 * BuildCraftTechStats rates it divided by 105. */
	int hullStrength;
	/* Hull damage from which hits reach the craft's systems; copied into
	 * the craft's systemDamageHullThreshold at spawn. */
	int systemDamageHullThreshold;
	/* Ion damage the craft's systems stand: collide_damagecraft compares
	 * the craft's subsystemDamage with it, and the HUD shows what is
	 * left. */
	uint16_t systemStrength;
	/* Top speed, in an object's speed units: the base of the AI's speeds
	 * (aiFlight.maxSpeedCache) and of the spawn speed; BuildCraftTechStats
	 * rates it. */
	uint16_t maxSpeed;
	/* Speed gained per simulated second: Flight_SlewObjectSpeedTowardTarget
	 * accelerates by a quarter of it, at least 1, plus the rest times the
	 * throttle fraction over 65,536, tripled unless engineOverdriveOff is
	 * set. BuildCraftTechStats rates it. */
	uint16_t accelRate;
	/* Speed lost per simulated second: Flight_SlewObjectSpeedTowardTarget
	 * decelerates by a quarter of it, at least 1, plus the rest times
	 * 65,535 less the throttle fraction over 65,536. */
	uint16_t decelRate;
	/* Yaw rate in angle units per SIMULATION_TICKS_PER_SECOND ticks at a
	 * full step; copied into aiFlight.turnRate at spawn. */
	int16_t yawRate;
	/* Fraction of 65,536 of each AI yaw step that
	 * Flight_UpdateCraftSteeringAndSpeed also applies to roll while
	 * aiFlight.rollState is 0 or 4. */
	uint16_t autoBankFactor;
	/* Roll rate in angle units per SIMULATION_TICKS_PER_SECOND ticks at a
	 * full step; copied into aiFlight.rollRate at spawn.
	 * BuildCraftTechStats adds it to pitchRate for the maneuver rating. */
	int16_t rollRate;
	/* Pitch rate in angle units per SIMULATION_TICKS_PER_SECOND ticks at a
	 * full step; copied into aiFlight.pitchRate at spawn. */
	int16_t pitchRate;
	/* Limit on a destroyed craft's tumble: its random roll rate, 0x2000
	 * plus GameRand() & 0x3FFF, is halved while it is over this value,
	 * then becomes its rollImpulseRate. */
	uint16_t maxTumbleRate;
	/* Most a craft's pushAccumX moves it per SIMULATION_TICKS_PER_SECOND
	 * ticks (Object_UpdateLifetimeAndMovement), except in the board and
	 * dropoff maneuvers, which use BOARDING_PUSH_RATE and
	 * DROPOFF_PUSH_RATE. */
	uint16_t maxPushRate;
	/* Base name of the model's cockpit files, which
	 * Hud_LoadCockpitResources appends to the resolution's cockpit folder
	 * for the local player's craft. */
	char cockpitResourceName[9];
	/* Projectile type of each of the two laser groups, 0 for none.
	 * FeDiskIo_BuildModelDef fills an empty group with the type, minus 120
	 * as a byte, of a hardpoint whose g_optHardpointWeaponGroupKindByType
	 * entry is 1. Spawning copies it into laserState.projectileTypeId. */
	uint8_t laserGroupWeaponType[2];
	/* First weaponHardpoints slot of each laser group. */
	uint8_t laserGroupFirstSlot[2];
	/* Last weaponHardpoints slot of each laser group. */
	uint8_t laserGroupLastSlot[2];
	/* Slots in each laser group. */
	uint8_t laserGroupSlotCount[2];
	/* 2 for a gunner group, whose slots fire turret projectiles: the
	 * builder gives it to a group on a laser turret or gun mesh, or on a
	 * freighter, platform or starship. Otherwise FeDiskIo_BuildModelDef
	 * sets 1 for hardpoint types 5 and 16, else 0. */
	uint8_t laserGroupMountType[2];
	/* Projectile type of each of the two warhead launchers, 0 for none; the
	 * builder fills an empty one like a laser group. BuildCraftTechStats
	 * counts a launcher that has one; spawning loads the flight group's
	 * warhead instead. */
	uint8_t warheadLauncherType[2];
	/* First weaponHardpoints slot of each warhead launcher. */
	uint8_t warheadLauncherFirstSlot[2];
	/* Last weaponHardpoints slot of each warhead launcher. */
	uint8_t warheadLauncherLastSlot[2];
	/* Slots in each warhead launcher. */
	uint8_t warheadLauncherSlotCount[2];
	/* Full load per launcher slot: spawning scales it by the flight group's
	 * warhead fraction (g_warheadAmmoFractionQ16), at least 1, then doubles
	 * or halves it for spawn statuses 1 and 2. */
	uint8_t warheadLauncherCapacity[2];
	/* The weapon slots, the laser groups' first and then the launchers',
	 * which FeDiskIo_BuildModelDef fills from the model's hardpoints. */
	struct ModelWeaponHardpoint weaponHardpoints[16];
	/* Countermeasures a craft carries: its cmAmmoCount at spawn, 0xAAAC
	 * over 65,536 of it for flares, and again when paiman_boardmaneuver
	 * resupplies it. */
	uint8_t countermeasureCount;
	/* Forward offset of the model's hardpoint of type 31, HARDPOINT_COCKPIT
	 * among the importer's names; the player code turns it into the
	 * player's hardpointWorld position. */
	int16_t primaryHardpointY;
	/* Up offset of that type 31 hardpoint. */
	int16_t primaryHardpointZ;
	/* Read by paiman_boardmaneuver and Object_UpdateLifetimeAndMovement. */
	int16_t dockForward; ///< Shared local-forward coordinate from OPT docking hardpoints 27-30.
	/* FeDiskIo_BuildModelDef sets both from the model's largest z when the
	 * table's first is 0, before the hardpoints. */
	int16_t dockFromUp
		[2]; ///< Local-up coordinates from OPT DockFromSmall (index 0) and DockFromBig (index
	///< 1); defaults to the model maximum up bound.
	/* FeDiskIo_BuildModelDef sets both from the model's smallest z when the
	 * table's first is 0, before the hardpoints. */
	int16_t dockToUp
		[2]; ///< Local-up coordinates from OPT DockToSmall (index 0) and DockToBig (index 1);
	///< defaults to the model minimum up bound.
	/* Read by the hangar orders, Mission_SpawnFlightGroupWaveCraft and the
	 * collision code. */
	struct ModelHangarPoints
		hangarPoints; ///< Local-space hangar path points from OPT hardpoints 25 (inside) and 26 (outside).
	/* Times FeDiskIo_BuildModelDef halved the bound sizes to bring each to
	 * 0x280 or under; readers shift the sizes left by it. */
	uint16_t boundSizeShift;
	/* The model's size along x (ModelBounds_GetSizeX) shifted right by
	 * boundSizeShift; the targeting and HUD code use the three sizes'
	 * mean. */
	int16_t boundSizeX;
	int16_t boundSizeZ; /* Size along z, shifted the same way. */
	int16_t boundSizeY; /* Size along y, shifted the same way. */
};

extern struct ModelDef g_modelDefs[73];
extern const float g_sw3dUnitFloat;
extern const float g_sw3dTriangleCornerCount;
extern const float g_sw3dQuadCornerCount;
extern const float g_sw3dZeroFloat;
extern const float g_sw3dDistantDepth;
extern const float g_sw3dSpanLengthReciprocal[70];

struct OptVector {
	/* First component: x of a position or normal, red of a color. */
	float x;
	float y; /* Second component: y, or green. */
	float z; /* Third component: z, or blue. */
};

extern void *g_curMeshVertices;
extern void *g_curMeshTexCoords;
extern void *g_modelNodeWalkUnusedScratch2;
extern void *g_curMeshMaterials;
extern int g_curVertexCount;
extern struct OptVector *g_curVertNormals;

typedef enum OptNodeType {
	OPT_GROUP = 0x0,
	OPT_FACEDATA = 0x1,
	OPT_TRANSFORM = 0x2,
	OPT_MESHVERTS = 0x3,
	OPT_TRANSLATION = 0x4,
	OPT_ROTATION = 0x5,
	OPT_SCALE = 0x6,
	OPT_NODEREF = 0x7,
	OPT_DEF = 0x8,
	OPT_MATERIAL = 0x9,
	OPT_MATERIAL_BINDING = 0xA,
	OPT_VERTNORMALS = 0xB,
	OPT_NORMAL_BINDING = 0xC,
	OPT_TEXCOORDS = 0xD,
	OPT_TEXCOORD_BINDING = 0xE,
	OPT_FACEDATA_QUAD_MESH = 0xF,
	OPT_FACEDATA_FACE_SET = 0x10,
	OPT_FACEDATA_TRIANGLE_STRIP_SET = 0x11,
	OPT_INVENTOR_GROUP = 0x12,
	OPT_BASE_COLOR = 0x13,
	OPT_TEXTURE = 0x14,
	OPT_FACEGROUP = 0x15,
	OPT_HARDPOINT = 0x16,
	OPT_ROTSCALE = 0x17,
	OPT_NODESWITCH = 0x18,
	OPT_MESHDESC = 0x19,
} OptNodeType;

#ifdef XVT_MODERN
typedef intptr_t XvtOptValue;
#else
typedef int XvtOptValue;
#endif

struct OptNode {
	/* The node's name, or NULL; OPT_NODEREF links find nodes by it. The
	 * original build's walkers reuse an OPT_NODEREF node's pName to keep
	 * its target. */
	char *pName;
	OptNodeType nodeType; /* What the node is; sets its payload's layout. */
	int childCount;	      /* Entries in pChildren. */
	/* The children, NULL when none; a slot may be NULL. */
	struct OptNode **pChildren;
	/* Items in the payload for a list node; faces for a face node; the
	 * binding for a binding node; records for an imported Inventor node. */
	XvtOptValue
		payloadCount; ///< Node-type-dependent scalar/count; OPT_NODEREF may store a relocated OptNode
	///< pointer value.
	/* For an OPT_NODEREF, the referenced name; for an imported Inventor
	 * node, its InventorFieldRecord array. */
	void *payload; ///< Relocated pointer to node-type-dependent payload data.
};

struct OptimizedPolyObject {
	/* The block's address when its pointers were last fixed; when it is not
	 * where the block is, OptModel_AdjustOptimizedPolyObjectPointers moves
	 * them. */
	void *selfMarker;
	/* The modern decoder sets it from the 2 bytes at its place in the
	 * file's body. */
	uint16_t
		reserved; ///< Import packing stores the temporary packed-block handle here; the final returned
	///< block preserves the now-stale value. No runtime consumer is identified.
	int rootNodeCount;   /* Entries in rootNodes. */
	struct OptNode **rootNodes; /* The top-level nodes. */
};

extern uint16_t g_loadedModels[201];
extern int g_cacheResolvedOptNodeRefs;
extern int g_optSourceIsVersion0;
#ifndef XVT_MODERN
extern int g_optModelInvertFaceNormals;
extern int g_generatedVertexNormalCount;
extern const char g_extRgb[4];
extern const char g_extTex[4];
#endif

struct FaceRecord {
	/* Corner vertex indices; the fourth is -1 for a triangle. The
	 * renderer's name for the layout of OptPackedFaceRecord. */
	int vertexIdx[4];
	/* Edge number of each side; -1 for a triangle's fourth. */
	int edgeIdx[4];
	int uvIdx[4];	  /* Texture coordinate index of each corner. */
	int normalIdx[4]; /* Vertex normal index of each corner. */
};

struct OptTexCoord {
	float u; /* Horizontal texture coordinate. */
	float v; /* Vertical texture coordinate. */
};

struct OptTextureData {
	/* The palette block, 4096 one-byte entries then 4096 RGB565 colors as
	 * 16 sub-palettes of 256. With inlinePaletteCount 0 it points at the
	 * texture's own block after its texels or at another texture's. With an
	 * inline palette the importer and the default texture store 256 here
	 * until a runtime copy points it at the copy. In a runtime model on a
	 * 16-bit display it points 4096 bytes before the converted colors. */
	uint16_t *palette;
	/* Nonzero when the palette is stored inline after the texels: its count
	 * of 256-color sub-palettes, 16 from the importer, 768 bytes each in a
	 * packed model. A runtime copy sets it to 0. */
	int inlinePaletteCount;
	/* Compared with width times height: when equal, the texels take
	 * dataSize bytes, else width times height. A runtime copy sets it to
	 * width times height. */
	int textureSize;
	/* Texel bytes, mip levels included, when textureSize equals width times
	 * height. */
	int dataSize;
	int width;  /* Width of the top level in texels. */
	int height; /* Height of the top level in texels. */
};

struct OptPackedFaceRecord {
	/* Corner vertex indices; the fourth is -1 for a triangle. */
	int vertexIndices[4];
	/* Edge number of the side from each corner to the next, the last back
	 * to corner 0; the fourth is -1 for a triangle. */
	int edgeIndices[4];
	int texCoordIndices[4]; /* Texture coordinate index of each corner. */
	int normalIndices[4];	/* Vertex normal index of each corner. */
};

struct OptLegacyFaceRecordV0 {
	/* Corner vertex indices. No field of this record is read or written by
	 * name; the converter reads version 0 records as plain ints. */
	int vertexIndices[4];
	int edgeIndices[4];	/* Edge number of each side. */
	int texCoordIndices[4]; /* Texture coordinate index of each corner. */
};

struct OptLegacyFaceDataV0 {
	/* Nothing uses this structure. */
	int edgeCount;
	struct OptLegacyFaceRecordV0
		records[1]; /* Nothing uses this structure. */
};

struct OptLegacyFaceStorageV0 {
	/* Not read or written by name: only the size of this structure is used,
	 * to find where a version 0 face node's data ends. The data holds all
	 * the records first, then all the normals, then all the gradients. */
	struct OptLegacyFaceRecordV0 faceRecord;
	struct OptVector faceNormal;	      /* Not read or written by name. */
	struct OptVector textureGradients[2]; /* Not read or written by name. */
};

struct OptLegacyFaceStorage {
	/* Not read or written by name: only the size of this structure is used,
	 * to find where a version 1 face node's data ends. The data holds all
	 * the records first, then all the normals, then all the gradients. */
	struct OptPackedFaceRecord faceRecord;
	struct OptVector faceNormal;	      /* Not read or written by name. */
	struct OptVector textureGradients[2]; /* Not read or written by name. */
};

struct OptLegacyFacePayloadV0 {
	/* The face node's edge count; the converter reads it as the payload's
	 * first int, never by this name. */
	int edgeCount;
	/* Only &storage[payloadCount] is used: the end of the face data, where
	 * the vertex normals of a mesh without a normal node follow. */
	struct OptLegacyFaceStorageV0 storage[1];
};

struct OptLegacyFacePayload {
	/* The face node's edge count; the converter reads it as the payload's
	 * first int, never by this name. */
	int edgeCount;
	/* Only &storage[payloadCount] is used: the end of the face data, where
	 * the vertex normals of a mesh without a normal node follow. */
	struct OptLegacyFaceStorage storage[1];
};

#ifndef XVT_MODERN
struct OptPackedFaceNode {
	char *name;		     /* OptNode's pName. */
	int nodeType;		     /* OptNode's nodeType. */
	int childCount;		     /* OptNode's childCount. */
	struct OptNode **children;   /* OptNode's pChildren. */
	int faceCount;		     /* OptNode's payloadCount: the faces. */
	struct OptPackedFaceData
		*faceData; /* OptNode's payload: the face data. */
};
#endif

struct OptPackedFaceData {
	/* Edges the faces number; the renderer's edge flag table holds at least
	 * this many (g_sceneEdgeFlagsCapacity). */
	int edgeCount;
	/* The face records, then one normal and two texture gradient vectors
	 * per face, then, when the mesh has no normal node, one normal per
	 * vertex. */
	struct OptPackedFaceRecord records[1];
};

struct OptHardpoint {
	/* Type 0 to 31, named in g_hardpointTypeNames: weapons, then hangar,
	 * dock and cockpit points. */
	int hardpointType;
	struct OptVector position; /* Position in the model's coordinates. */
};

uint16_t OptModel_LoadHandle(const char *modelFilename);
#ifndef XVT_MODERN
uint16_t OptModel_LoadInventorBinaryToHandle(XvtFile *stream);
uint16_t OptModel_LoadInventorAsciiToHandle(XvtFile *stream);
int OptModel_ParseInventorAsciiNode(XvtFile *stream, char *nodeStorage,
				    struct OptNode **outNode);
#endif
void OptModel_TranslateNodeVerticesRecursive(struct OptNode *node,
					     struct OptimizedPolyObject *model,
					     const float *translation);
void OptModel_TranslateVertices(struct OptimizedPolyObject *model,
				const float *translation);
#ifndef XVT_MODERN
void OptModel_RelocateLoadedPointers(struct OptimizedPolyObject *model);
void OptModel_RelocateNodePointersRecursive(struct OptNode *node,
					    XvtOptValue relocationDelta);
#endif
void OptModel_AdjustOptimizedPolyObjectPointers(
	struct OptimizedPolyObject *model);
void OptModel_AdjustOptimizedNodePointers(struct OptNode *node,
					  XvtOptValue relocationDelta);
uint16_t OptModel_LoadFileToHandle(char *filename);
unsigned int OptModel_ConvertLegacyModelToOptimized(unsigned int sourceSize);
void *OptModel_FindSharedTextureDataInNodeBeforeTarget(
	const void *textureData, struct OptNode *node,
	const struct OptNode *stopNode);
void *OptModel_FindEarlierSharedTextureData(const void *textureData,
					    struct OptimizedPolyObject *model,
					    const struct OptNode *stopNode);
unsigned int
OptModel_ConvertLegacyNodeToOptimized(uint8_t *dst, struct OptNode *srcNode,
				      struct OptimizedPolyObject *srcModel,
				      struct OptimizedPolyObject *dstModel,
				      struct SceneMesh *meshState);
void OptModel_CollectUniqueVertices(struct OptNode *dstVertexNode,
				    struct OptNode *srcNode,
				    struct OptimizedPolyObject *srcModel,
				    struct SceneMesh *meshState);
void OptModel_CollectUniqueTexCoords(struct OptNode *dstTexCoordNode,
				     struct OptNode *srcNode,
				     struct OptimizedPolyObject *srcModel,
				     struct SceneMesh *meshState);
void OptModel_CollectUniqueVertexNormals(struct OptNode *dstNormalNode,
					 struct OptNode *srcNode,
					 struct OptimizedPolyObject *srcModel,
					 struct SceneMesh *meshState);
int OptModel_RemapVectorIndex(const struct OptNode *uniqueVectorNode,
			      const struct OptVector *sourceVectors,
			      int sourceIndex);
int OptModel_RemapTexCoordIndex(const struct OptNode *uniqueTexCoordNode,
				const struct OptTexCoord *sourceTexCoords,
				int sourceIndex);
void OptModel_AppendConvertedFacesForNode(struct OptNode *dstFaceNode,
					  struct OptNode *targetFaceNode,
					  struct OptNode *node,
					  struct OptimizedPolyObject *srcModel,
					  struct SceneMesh *meshState);
void OptModel_AppendConvertedFacesForCurrentMesh(
	struct OptNode *dstFaceNode, struct OptNode *targetFaceNode,
	struct OptimizedPolyObject *srcModel, struct SceneMesh *meshState);
uint16_t OptModel_CreateRuntimeHandle(unsigned int sourceHandle);
void OptModel_FixupRuntimeTexturePointers(struct OptNode *node,
					  struct OptimizedPolyObject *dstModel,
					  struct OptimizedPolyObject *srcModel);
struct OptNode *
OptModel_FindCorrespondingTextureNode(struct OptNode *srcNode,
				      struct OptNode *dstNode,
				      const uint16_t *sourcePalette);
struct OptNode *OptModel_FindCorrespondingTextureNodeInModel(
	struct OptimizedPolyObject *dstModel,
	struct OptimizedPolyObject *srcModel, const uint16_t *sourcePalette);
#ifndef XVT_MODERN
void OptModel_SaveHandleToFile(const char *filename, uint16_t handle);
#endif
unsigned int
OptModel_MeasureNodeAndRaiseCapacities(struct OptNode *node,
				       struct SceneMesh *parentState);
void OptModel_PrepareTexturePalette(uint16_t *palette, int entryCount);
unsigned int OptModel_BuildRuntimeNode(const struct OptNode *srcNode,
				       struct SceneMesh *meshState,
				       uint8_t *dst);
#ifndef XVT_MODERN
uint16_t OptModel_ConvertImportedHandleToPacked(uint16_t sourceHandle);
size_t OptModel_ConvertImportedNodeToPackedRecursive(
	const struct OptimizedPolyObject *sourceModel,
	const struct OptNode *sourceNode, void *conversionState,
	uint8_t *destBuffer);
size_t OptModel_CalculatePackedNodeSizeRecursive(
	const struct OptimizedPolyObject *sourceModel,
	const struct OptNode *sourceNode, void *conversionState);
int OptModel_FindUniqueEdgeIndex(const struct OptPackedFaceNode *faceNode,
				 int vertexIndexA, int vertexIndexB);
float *OptModel_AppendPackedFaceDerivedData(struct OptPackedFaceNode *faceNode,
					    uint8_t *dest,
					    void *conversionState);
void OptModel_BuildFaceNormalTangentData(
	float *dest, const struct OptPackedFaceData *faceData, int faceCount,
	const void *conversionState);
void OptModel_BuildVertexNormalsFromFaces(
	float *dest, const struct OptPackedFaceData *faceData, int faceCount);
#endif
struct OptNode *
OptModel_ResolveNodeRef(const struct OptimizedPolyObject *object,
			const char *name);
struct OptNode *OptModel_FindNodeByName(struct OptNode *node, const char *name);
#ifndef XVT_MODERN
int OptModel_GetExternalTextureSerializedSize(const char *sourceFileName);
#endif

#ifdef __cplusplus
}
#endif

#endif
