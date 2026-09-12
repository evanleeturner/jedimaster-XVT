#ifndef XVT_ASSETS_MODEL_MESH_H
#define XVT_ASSETS_MODEL_MESH_H

#include "xvt/assets/opt_model.h"
#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Stored as int32_t in the binary (IDB enum MeshComponentType). */
typedef int32_t MeshComponentType;

enum {
	MESH_COMPONENT_00_HULL = 0x0,      ///< strings.txt line 1734: Hull
	MESH_COMPONENT_01_HULL = 0x1,      ///< strings.txt line 1735: Hull
	MESH_COMPONENT_02_WING = 0x2,      ///< strings.txt line 1736: Wing
	MESH_COMPONENT_03_FUSELAGE = 0x3,  ///< strings.txt line 1737: Fuselage
	MESH_COMPONENT_04_LASR_TUR = 0x4,  ///< strings.txt line 1738: Lasr Tur
	MESH_COMPONENT_05_LASR_GUN = 0x5,  ///< strings.txt line 1739: Lasr Gun
	MESH_COMPONENT_06_ENGINE = 0x6,    ///< strings.txt line 1740: Engine
	MESH_COMPONENT_07_BRIDGE = 0x7,    ///< strings.txt line 1741: Bridge
	MESH_COMPONENT_08_SHLD_GEN = 0x8,  ///< strings.txt line 1742: Shld Gen
	MESH_COMPONENT_09_ENRG_GEN = 0x9,  ///< strings.txt line 1743: Enrg Gen
	MESH_COMPONENT_10_WHEAD_LN = 0xA,  ///< strings.txt line 1744: Whead Ln
	MESH_COMPONENT_11_COMM_SYS = 0xB,  ///< strings.txt line 1745: Comm Sys
	MESH_COMPONENT_12_BEAM_SYS = 0xC,  ///< strings.txt line 1746: Beam Sys
	MESH_COMPONENT_13_COMM_SYS = 0xD,  ///< strings.txt line 1747: Comm Sys
	MESH_COMPONENT_14_DOCK_PLT = 0xE,  ///< strings.txt line 1748: Dock Plt
	MESH_COMPONENT_15_LAND_PLT = 0xF,  ///< strings.txt line 1749: Land Plt
	MESH_COMPONENT_16_HANGAR = 0x10,   ///< strings.txt line 1750: Hangar
	MESH_COMPONENT_17_CARGO = 0x11,    ///< strings.txt line 1751: Cargo
	MESH_COMPONENT_18_HULL = 0x12,     ///< strings.txt line 1752: Hull
	MESH_COMPONENT_19_ANTENNA = 0x13,  ///< strings.txt line 1753: Antenna
	MESH_COMPONENT_20_WING = 0x14,     ///< strings.txt line 1754: Wing
	MESH_COMPONENT_21_LASR_TUR = 0x15, ///< strings.txt line 1755: Lasr Tur
	MESH_COMPONENT_22_WHEAD_LN = 0x16, ///< strings.txt line 1756: Whead Ln
	MESH_COMPONENT_23_COMM_SYS = 0x17, ///< strings.txt line 1757: Comm Sys
	MESH_COMPONENT_24_BEAM_SYS = 0x18, ///< strings.txt line 1758: Beam Sys
	MESH_COMPONENT_25_COMM_SYS = 0x19, ///< strings.txt line 1759: Comm Sys
	MESH_COMPONENT_26_COCKPIT = 0x1A,  ///< strings.txt line 1760: Cockpit
	MESH_COMPONENT_27_HULL = 0x1B,     ///< strings.txt line 1761: Hull
	MESH_COMPONENT_28_HULL = 0x1C,     ///< strings.txt line 1762: Hull
	MESH_COMPONENT_29_HULL = 0x1D,     ///< strings.txt line 1763: Hull
	MESH_COMPONENT_30_HULL = 0x1E,     ///< strings.txt line 1764: Hull
	MESH_COMPONENT_31_HULL = 0x1F,     ///< strings.txt line 1765: Hull
	MESH_COMPONENT_32_DASHES = 0x20,   ///< strings.txt line 1766: --------
};

struct MeshDescriptor {
	MeshComponentType meshType;
	int explosionType;
	OptVector span;
	OptVector center;
	OptVector boxMin;
	OptVector boxMax;
	int targetId;
	OptVector target;
};

struct ModelMeshObjectTypeCache {
	int meshCount;
	int meshTypes[50];
	MeshDescriptor* meshDescriptors[50];
};

extern ModelMeshObjectTypeCache g_objectTypeMeshCache[73];
extern int g_optHardpointSearchIndex;
extern uint8_t g_optHardpointWeaponGroupKindByType[32];

static __inline MeshComponentType ModelMesh_GetCachedObjectTypeMeshType(int objectType, int meshIndex) {
	if (meshIndex < 0)
		return MESH_COMPONENT_00_HULL;
	if (g_objectTypeMeshCache[objectType].meshCount <= meshIndex)
		meshIndex = g_objectTypeMeshCache[objectType].meshCount - 1;
	return g_objectTypeMeshCache[objectType].meshTypes[meshIndex];
}

void ModelMesh_ApplyAnimatedMeshRotationToPoint(int16_t angleQ16, int modelType, int meshIndex, int localX,
												int localY, int localZ);
int ModelMesh_GetObjectTypeMeshCount(int objectType);
OptNode* ModelMesh_FindFirstMeshVertsNode(OptNode* node);
OptNode* ModelMesh_FindFirstRotScaleNode(OptNode* node);
MeshDescriptor* ModelMesh_FindDescriptorNodeRecursive(OptNode* node, OptimizedPolyObject* model);
MeshDescriptor* ModelMesh_GetDescriptor(int modelType, int meshIndex);
MeshComponentType ModelMesh_GetObjectTypeMeshType(int objectType, int meshIndex);
int ModelMesh_GetVertexCount(int modelType, int meshIndex);
int ModelMesh_GetVertexX(int modelType, int meshIndex, int vertexIndex);
int ModelMesh_GetVertexY(int modelType, int meshIndex, int vertexIndex);
int ModelMesh_GetVertexZ(int modelType, int meshIndex, int vertexIndex);
int ModelMesh_GetCenterX(int modelType, int meshIndex);
int ModelMesh_GetCenterY(int modelType, int meshIndex);
int ModelMesh_GetCenterZ(int modelType, int meshIndex);
int ModelMesh_GetBoundsMinX(int modelType, int meshIndex);
int ModelMesh_GetBoundsMinY(int modelType, int meshIndex);
int ModelMesh_GetBoundsMinZ(int modelType, int meshIndex);
int ModelMesh_GetBoundsMaxX(int modelType, int meshIndex);
int ModelMesh_GetBoundsMaxY(int modelType, int meshIndex);
int ModelMesh_GetBoundsMaxZ(int modelType, int meshIndex);
int ModelMesh_GetTargetId(int modelType, int meshIndex);
int ModelMesh_GetComponentFocusX(int modelType, int meshIndex);
int ModelMesh_GetComponentFocusY(int modelType, int meshIndex);
int ModelMesh_GetComponentFocusZ(int modelType, int meshIndex);
int ModelMesh_GetComponentMaxExtent(int modelType, int meshIndex);
int ModelMesh_IsObjectTypeMeshDamageable(int objectType, int meshIndex);
int ModelMesh_HasExplosionType1(int modelType, int meshIndex);
float* ModelMesh_GetRotScaleData(int modelType, int meshIndex);
OptNode* ModelMesh_FindNthHardpointNodeRecursive(OptNode* node, OptimizedPolyObject* model,
												 int hardpointIndex);
OptNode* ModelMesh_FindNthHardpointNode(OptNode* node, OptimizedPolyObject* model, int hardpointIndex);
int ModelMesh_CountHardpointNodesRecursive(OptNode* node, OptimizedPolyObject* object);
int ModelMesh_CountHardpoints(int modelType, int meshIndex);
int ModelMesh_GetAlternateHardpointIndex(int modelType, int meshIndex, int hardpointIndex);
int ModelMesh_GetHardpointX(int modelIndex, int meshIndex, int hardpointIndex);
int ModelMesh_GetHardpointY(int modelIndex, int meshIndex, int hardpointIndex);
int ModelMesh_GetHardpointZ(int modelIndex, int meshIndex, int hardpointIndex);
void ModelMesh_GetHardpoint(int modelType, int meshIndex, int hardpointIndex, int* outType, int* outX,
							int* outY, int* outZ);
int ModelMesh_HasFuselage(int modelType);
int ModelMesh_FindNearestLiveMainHullByBounds(int modelType, int localX, int localY, int localZ);
int ModelMesh_FindNearestVertexForPoint(int modelType, int localX, int localY, int localZ, int meshIndex,
										int nearestRank);
int ModelMesh_FindBridgeIndex(OptimizedPolyObject* model);
ModelMeshObjectTypeCache* ModelMesh_BuildObjectTypeMeshCache(void);

#ifdef __cplusplus
}
#endif

#endif
