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
	int16_t x;
	int16_t z;
	int16_t y;
	uint8_t alternateMeshHardpointIdx;
	uint8_t meshIdx;
};

struct ModelLocalPoint {
	int side;
	int up;
	int forward;
};

struct ModelHangarPoints {
	ModelLocalPoint inside;
	ModelLocalPoint outside;
};

struct ModelDef {
	char name[10];
	char* nameLong;
	uint8_t craftPointValue;
	uint8_t ratingWeight;
	uint8_t engineGlowCount;
	uint8_t hasHyperdrive;
	uint8_t hasShields;
	int shieldStrength;
	uint8_t reactionThreshold;
	uint8_t modelClassFlag; ///< Static 0/1 model-class discriminator; exact runtime behavior is not yet
							///< identified.
	int hullStrength;
	int systemDamageHullThreshold;
	uint16_t systemStrength;
	uint16_t maxSpeed;
	uint16_t accelRate;
	uint16_t decelRate;
	int16_t yawRate;
	uint16_t autoBankFactor;
	int16_t rollRate;
	int16_t pitchRate;
	uint16_t maxTumbleAngle;
	uint16_t maxPushRate;
	char cockpitResourceName[9];
	uint8_t laserGroupWeaponType[2];
	uint8_t laserGroupFirstSlot[2];
	uint8_t laserGroupLastSlot[2];
	uint8_t laserGroupSlotCount[2];
	uint8_t laserGroupMountType[2];
	uint8_t warheadLauncherType[2];
	uint8_t warheadLauncherFirstSlot[2];
	uint8_t warheadLauncherLastSlot[2];
	uint8_t warheadLauncherSlotCount[2];
	uint8_t warheadLauncherValue[2];
	ModelWeaponHardpoint weaponHardpoints[16];
	uint8_t countermeasureCount;
	int16_t primaryHardpointY;
	int16_t primaryHardpointZ;
	int16_t dockForward;   ///< Shared local-forward coordinate from OPT docking hardpoints 27-30.
	int16_t dockFromUp[2]; ///< Local-up coordinates from OPT DockFromSmall (index 0) and DockFromBig (index
						   ///< 1); defaults to the model maximum up bound.
	int16_t dockToUp[2];   ///< Local-up coordinates from OPT DockToSmall (index 0) and DockToBig (index 1);
						   ///< defaults to the model minimum up bound.
	ModelHangarPoints
		hangarPoints; ///< Local-space hangar path points from OPT hardpoints 25 (inside) and 26 (outside).
	uint16_t boundSizeShift;
	int16_t boundSizeX;
	int16_t boundSizeZ;
	int16_t boundSizeY;
};

extern ModelDef g_modelDefs[73];
extern const float g_sw3dUnitFloat;
extern const float g_sw3dTriangleCornerCount;
extern const float g_sw3dQuadCornerCount;
extern const float g_sw3dZeroFloat;
extern const float g_sw3dDistantDepth;
extern const float g_sw3dSpanLengthReciprocal[70];

struct OptVector {
	float x;
	float y;
	float z;
};

extern void* g_modelNodeWalkUnusedScratch0;
extern void* g_modelNodeWalkUnusedScratch1;
extern void* g_modelNodeWalkUnusedScratch2;
extern void* g_curMeshFlags;
extern int g_curVertexCount;
extern OptVector* g_curVertNormals;

typedef enum OptNodeType {
	OPT_GROUP = 0x0,
	OPT_FACEDATA = 0x1,
	OPT_TYPE_2 = 0x2,
	OPT_MESHVERTS = 0x3,
	OPT_TYPE_4 = 0x4,
	OPT_TYPE_5 = 0x5,
	OPT_TYPE_6 = 0x6,
	OPT_NODEREF = 0x7,
	OPT_TYPE_8 = 0x8,
	OPT_TYPE_9 = 0x9,
	OPT_TYPE_10 = 0xA,
	OPT_VERTNORMALS = 0xB,
	OPT_TYPE_12 = 0xC,
	OPT_TEXCOORDS = 0xD,
	OPT_TYPE_14 = 0xE,
	OPT_FACEDATA_15 = 0xF,
	OPT_FACEDATA_16 = 0x10,
	OPT_FACEDATA_17 = 0x11,
	OPT_TYPE_18 = 0x12,
	OPT_TYPE_19 = 0x13,
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
	char* pName;
	OptNodeType nodeType;
	int childCount;
	struct OptNode** pChildren;
	XvtOptValue param1; ///< Node-type-dependent scalar/count; OPT_NODEREF may store a relocated OptNode
						///< pointer value.
	void* param2;       ///< Relocated pointer to node-type-dependent payload data.
};

struct OptimizedPolyObject {
	void* selfMarker;
	uint16_t reserved; ///< Import packing stores the temporary packed-block handle here; the final returned
					   ///< block preserves the now-stale value. No runtime consumer is identified.
	int rootNodeCount;
	OptNode** rootNodes;
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
	int vertexIdx[4];
	int edgeIdx[4];
	int uvIdx[4];
	int normalIdx[4];
};

struct OptTexCoord {
	float u;
	float v;
};

struct OptTextureData {
	uint16_t* palette;
	int paletteType;
	int textureSize;
	int dataSize;
	int width;
	int height;
};

struct OptPackedFaceRecord {
	int vertexIndices[4];
	int edgeIndices[4];
	int texCoordIndices[4];
	int normalIndices[4];
};

struct OptLegacyFaceRecordV0 {
	int vertexIndices[4];
	int edgeIndices[4];
	int texCoordIndices[4];
};

struct OptLegacyFaceDataV0 {
	int edgeCount;
	OptLegacyFaceRecordV0 records[1];
};

struct OptLegacyFaceStorageV0 {
	OptLegacyFaceRecordV0 faceRecord;
	OptVector faceNormal;
	OptVector textureGradients[2];
};

struct OptLegacyFaceStorage {
	OptPackedFaceRecord faceRecord;
	OptVector faceNormal;
	OptVector textureGradients[2];
};

struct OptLegacyFacePayloadV0 {
	int edgeCount;
	OptLegacyFaceStorageV0 storage[1];
};

struct OptLegacyFacePayload {
	int edgeCount;
	OptLegacyFaceStorage storage[1];
};

#ifndef XVT_MODERN
struct OptPackedFaceNode {
	char* name;
	int nodeType;
	int childCount;
	OptNode** children;
	int faceCount;
	OptPackedFaceData* faceData;
};
#endif

struct OptPackedFaceData {
	int edgeCount;
	OptPackedFaceRecord records[1];
};

struct OptHardpoint {
	int hardpointType;
	OptVector position;
};

uint16_t OptModel_LoadHandle(const char* modelFilename);
#ifndef XVT_MODERN
uint16_t OptModel_LoadInventorBinaryToHandle(XvtFile* stream);
uint16_t OptModel_LoadInventorAsciiToHandle(XvtFile* stream);
int OptModel_ParseInventorAsciiNode(XvtFile* stream, char* nodeStorage, OptNode** outNode);
#endif
void OptModel_TranslateNodeVerticesRecursive(OptNode* node, OptimizedPolyObject* model,
											 const float* translation);
void OptModel_TranslateVertices(OptimizedPolyObject* model, const float* translation);
#ifndef XVT_MODERN
void OptModel_RelocateLoadedPointers(OptimizedPolyObject* model);
void OptModel_RelocateNodePointersRecursive(OptNode* node, XvtOptValue relocationDelta);
#endif
void OptModel_AdjustOptimizedPolyObjectPointers(OptimizedPolyObject* model);
void OptModel_AdjustOptimizedNodePointers(OptNode* node, XvtOptValue base);
uint16_t OptModel_LoadFileToHandle(char* filename);
unsigned int OptModel_ConvertLegacyModelToOptimized(unsigned int sourceSize);
void* OptModel_FindSharedTextureDataInNodeBeforeTarget(const void* textureData, OptNode* node,
													   const OptNode* stopNode);
void* OptModel_FindEarlierSharedTextureData(const void* textureData, OptimizedPolyObject* model,
											const OptNode* stopNode);
unsigned int OptModel_ConvertLegacyNodeToOptimized(uint8_t* dst, OptNode* srcNode,
												   OptimizedPolyObject* srcModel,
												   OptimizedPolyObject* dstModel, SceneMesh* meshState);
void OptModel_CollectUniqueVertices(OptNode* dstVertexNode, OptNode* srcNode, OptimizedPolyObject* srcModel,
									SceneMesh* meshState);
void OptModel_CollectUniqueTexCoords(OptNode* dstTexCoordNode, OptNode* srcNode,
									 OptimizedPolyObject* srcModel, SceneMesh* meshState);
void OptModel_CollectUniqueVertexNormals(OptNode* dstNormalNode, OptNode* srcNode,
										 OptimizedPolyObject* srcModel, SceneMesh* meshState);
int OptModel_RemapVectorIndex(const OptNode* uniqueVectorNode, const OptVector* sourceVectors,
							  int sourceIndex);
int OptModel_RemapTexCoordIndex(const OptNode* uniqueTexCoordNode, const OptTexCoord* sourceTexCoords,
								int sourceIndex);
void OptModel_AppendConvertedFacesForNode(OptNode* dstFaceNode, OptNode* targetFaceNode, OptNode* node,
										  OptimizedPolyObject* srcModel, SceneMesh* meshState);
void OptModel_AppendConvertedFacesForCurrentMesh(OptNode* dstFaceNode, OptNode* targetFaceNode,
												 OptimizedPolyObject* srcModel, SceneMesh* meshState);
uint16_t OptModel_CreateRuntimeHandle(unsigned int sourceHandle);
void OptModel_FixupRuntimeTexturePointers(OptNode* node, OptimizedPolyObject* dstModel,
										  OptimizedPolyObject* srcModel);
OptNode* OptModel_FindCorrespondingTextureNode(OptNode* srcNode, OptNode* dstNode,
											   const uint16_t* sourcePalette);
OptNode* OptModel_FindCorrespondingTextureNodeInModel(OptimizedPolyObject* dstModel,
													  OptimizedPolyObject* srcModel,
													  const uint16_t* sourcePalette);
#ifndef XVT_MODERN
void OptModel_SaveHandleToFile(const char* filename, uint16_t handle);
#endif
unsigned int OptModel_GetSerializedNodeSize(OptNode* node, SceneMesh* parentState);
void OptModel_PrepareTexturePalette(uint16_t* palette, int entryCount);
unsigned int OptModel_BuildRuntimeNode(const OptNode* srcNode, SceneMesh* meshState, uint8_t* dst);
#ifndef XVT_MODERN
uint16_t OptModel_ConvertImportedHandleToPacked(uint16_t sourceHandle);
size_t OptModel_ConvertImportedNodeToPackedRecursive(const OptimizedPolyObject* sourceModel,
													 const OptNode* sourceNode, void* conversionState,
													 uint8_t* destBuffer);
size_t OptModel_CalculatePackedNodeSizeRecursive(const OptimizedPolyObject* sourceModel,
												 const OptNode* sourceNode, void* conversionState);
int OptModel_FindUniqueEdgeIndex(const OptPackedFaceNode* faceNode, int vertexIndexA, int vertexIndexB);
float* OptModel_AppendPackedFaceDerivedData(OptPackedFaceNode* faceNode, uint8_t* dest,
											void* conversionState);
void OptModel_BuildFaceNormalTangentData(float* dest, const OptPackedFaceData* faceData, int faceCount,
										 const void* conversionState);
void OptModel_BuildVertexNormalsFromFaces(float* dest, const OptPackedFaceData* faceData, int faceCount);
#endif
OptNode* OptModel_ResolveNodeRef(const OptimizedPolyObject* object, const char* name);
OptNode* OptModel_FindNodeByName(OptNode* node, const char* name);
#ifndef XVT_MODERN
int OptModel_GetExternalTextureSerializedSize(const char* sourceFileName);
#endif

#ifdef __cplusplus
}
#endif

#endif
