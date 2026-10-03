#include "xvt/assets/model_mesh.h"
#ifdef XVT_MODERN
#include "xvt_runtime/assets/opt_native.h"
#endif
#include "xvt/assets/model_mesh_internal.h"

#include "xvt/assets/object_type.h"
#include "xvt/math/math.h"
#include "xvt/math/trig2.h"
#include "xvt/util/memory.h"

/* X of the vector the last point rotation produced. Many functions write it,
 * chiefly pai_calcrotatedpoint, which turns a local vector into world axes
 * here, and ModelMesh_ApplyAnimatedMeshRotationToPoint; callers read it right
 * after the call and often scale or offset it in place. */
// GLOBAL: XVT 0x9D8A58
int g_rotatedX = 0;
/* Y of the vector the last point rotation produced; written and read like
 * g_rotatedX. */
// GLOBAL: XVT 0x9D8A54
int g_rotatedY = 0;
/* Z of the vector the last point rotation produced; written and read like
 * g_rotatedX. */
// GLOBAL: XVT 0x9D8B60
int g_rotatedZ = 0;

/* Per object type 0 to 72, its mesh count and, for up to 50 meshes, each mesh's
 * type and descriptor. Only ModelMesh_BuildObjectTypeMeshCache fills it, after
 * the flight resources load. */
// GLOBAL: XVT 0xA00870
struct ModelMeshObjectTypeCache g_objectTypeMeshCache[73] = {0};
/* Hardpoints passed so far in the current ModelMesh_FindNthHardpointNode
 * search: that function sets it to 0 and
 * ModelMesh_FindNthHardpointNodeRecursive raises it. */
// GLOBAL: XVT 0x528128
int g_optHardpointSearchIndex = 0;

/* Sets g_rotatedX, g_rotatedY and g_rotatedZ to the local point, then, when
 * mesh meshIndex has an OPT_ROTSCALE node (ModelMesh_GetRotScaleData), turns
 * that point by angleQ16 (65,536 a full circle) about the node's axis: its
 * floats 3 to 5 cast to int and taken as 1.15 fixed point, through the point of
 * its floats 0 to 2 with the y negated. Leaves the result in the same three
 * globals. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x4285A0
void ModelMesh_ApplyAnimatedMeshRotationToPoint(int16_t angleQ16,
						int objectType, int meshIndex,
						int localX, int localY,
						int localZ)
{
	float *rotScaleData;
	int axisX;
	int axisY;
	int axisZ;
	int cosine;
	int sine;
	int coefficient00;
	int coefficient01;
	int coefficient02;
	int coefficient10;
	int coefficient11;
	int coefficient12;
	int coefficient20;
	int coefficient21;
	int coefficient22;
	int transformedX;
	int transformedY;
	int transformedZ;

	g_rotatedX = localX;
	g_rotatedY = localY;
	g_rotatedZ = localZ;
	rotScaleData = ModelMesh_GetRotScaleData(objectType, meshIndex);
	if (rotScaleData == NULL) {
		return;
	}

	axisX = (int)rotScaleData[3];
	axisY = (int)rotScaleData[4];
	axisZ = (int)rotScaleData[5];
	cosine = trig2_getsignedcos(angleQ16);
	sine = trig2_getsignedsin(angleQ16);
	if (cosine >= 0) {
		const int oneMinusCosine = MODEL_MESH_Q15_ONE - cosine;

		coefficient00 = Math_RodriguesTermNonnegativeCos(
			axisX, axisX, oneMinusCosine, cosine);
		coefficient01 = Math_RodriguesTermNonnegativeCos(
			axisX, axisY, oneMinusCosine, Math_MulQ15(sine, axisZ));
		coefficient02 = Math_RodriguesTermNonnegativeCos(
			axisX, axisZ, oneMinusCosine,
			-Math_MulQ15(sine, axisY));
		coefficient10 = Math_RodriguesTermNonnegativeCos(
			axisX, axisY, oneMinusCosine,
			-Math_MulQ15(sine, axisZ));
		coefficient11 = Math_RodriguesTermNonnegativeCos(
			axisY, axisY, oneMinusCosine, cosine);
		coefficient12 = Math_RodriguesTermNonnegativeCos(
			axisY, axisZ, oneMinusCosine, Math_MulQ15(sine, axisX));
		coefficient20 = Math_RodriguesTermNonnegativeCos(
			axisX, axisZ, oneMinusCosine, Math_MulQ15(sine, axisY));
		coefficient21 = Math_RodriguesTermNonnegativeCos(
			axisY, axisZ, oneMinusCosine,
			-Math_MulQ15(sine, axisX));
		coefficient22 = Math_RodriguesTermNonnegativeCos(
			axisZ, axisZ, oneMinusCosine, cosine);
	} else {
		coefficient00 = Math_RodriguesTermNegativeCos(axisX, axisX,
							      -cosine, cosine);
		coefficient01 = Math_RodriguesTermNegativeCos(
			axisX, axisY, -cosine, Math_MulQ15(sine, axisZ));
		coefficient02 = Math_RodriguesTermNegativeCos(
			axisX, axisZ, -cosine, -Math_MulQ15(sine, axisY));
		coefficient10 = Math_RodriguesTermNegativeCos(
			axisX, axisY, -cosine, -Math_MulQ15(sine, axisZ));
		coefficient11 = Math_RodriguesTermNegativeCos(axisY, axisY,
							      -cosine, cosine);
		coefficient12 = Math_RodriguesTermNegativeCos(
			axisY, axisZ, -cosine, Math_MulQ15(sine, axisX));
		coefficient20 = Math_RodriguesTermNegativeCos(
			axisX, axisZ, -cosine, Math_MulQ15(sine, axisY));
		coefficient21 = Math_RodriguesTermNegativeCos(
			axisY, axisZ, -cosine, -Math_MulQ15(sine, axisX));
		coefficient22 = Math_RodriguesTermNegativeCos(axisZ, axisZ,
							      -cosine, cosine);
	}

	localX -= (int)rotScaleData[0];
	localY += (int)rotScaleData[1];
	localZ -= (int)rotScaleData[2];
	transformedX =
		Math_Dot3Q15Wrapped(localX, localY, localZ, coefficient00,
				    coefficient10, coefficient20);
	transformedY =
		Math_Dot3Q15Wrapped(localX, localY, localZ, coefficient01,
				    coefficient11, coefficient21);
	transformedZ =
		Math_Dot3Q15Wrapped(localX, localY, localZ, coefficient02,
				    coefficient12, coefficient22);
	transformedY -= (int)rotScaleData[1];
	transformedZ += (int)rotScaleData[2];
	transformedX += (int)rotScaleData[0];
	g_rotatedX = transformedX;
	g_rotatedY = transformedY;
	g_rotatedZ = transformedZ;
}

/* Returns the object type's mesh count: its model's root count, 1 less when the
 * first root is an OPT_TEXTURE, capped at 50. Returns 0 when g_loadedModels
 * holds no handle for it or its assetFlags lacks the 0x1 bit. */
// FUNCTION: XVT 0x4ADC40
int ModelMesh_GetObjectTypeMeshCount(int objectType)
{
	uint16_t modelHandle;
	struct OptimizedPolyObject *model;
	int meshCount;

	modelHandle = g_loadedModels[objectType];
	if (modelHandle == 0) {
		return 0;
	}
	if ((g_objectTypeTable[objectType].assetFlags & 1) == 0) {
		return 0;
	}

	model = (struct OptimizedPolyObject *)Memory_GetHandleBlock(
		modelHandle);
	if (model->selfMarker != model) {
		OptModel_AdjustOptimizedPolyObjectPointers(model);
	}
	meshCount = model->rootNodeCount;
	if (model->rootNodes[0]->nodeType == OPT_TEXTURE) {
		--meshCount;
	}
	Memory_HandleBlockDoneStub(g_loadedModels[objectType]);

	if (meshCount > 50) {
		meshCount = 50;
	}

	return meshCount;
}

/* Returns the first OPT_MESHVERTS node at or below node, depth first, or NULL.
 * Skips NULL child slots and does not follow OPT_NODEREF links. */
// FUNCTION: XVT 0x4ADCC0
struct OptNode *ModelMesh_FindFirstMeshVertsNode(struct OptNode *node)
{
	struct OptNode *result;
	int childIndex;

	if (node == 0) {
		return 0;
	}

	if (node->nodeType == OPT_MESHVERTS) {
		return node;
	}

	for (childIndex = 0; childIndex < node->childCount; childIndex++) {
		if (node->pChildren[childIndex] != 0) {
			result = ModelMesh_FindFirstMeshVertsNode(
				node->pChildren[childIndex]);
			if (result != 0) {
				return result;
			}
		}
	}

	return 0;
}

/* Returns the first OPT_ROTSCALE node at or below node, depth first, or NULL.
 * Skips NULL child slots and does not follow OPT_NODEREF links. */
// FUNCTION: XVT 0x4ADD10
struct OptNode *ModelMesh_FindFirstRotScaleNode(struct OptNode *node)
{
	struct OptNode *result;
	int childIndex;

	if (node == 0) {
		return 0;
	}

	if (node->nodeType == OPT_ROTSCALE) {
		return node;
	}

	for (childIndex = 0; childIndex < node->childCount; childIndex++) {
		if (node->pChildren[childIndex] != 0) {
			result = ModelMesh_FindFirstRotScaleNode(
				node->pChildren[childIndex]);
			if (result != 0) {
				return result;
			}
		}
	}

	return 0;
}

/* Returns, as a MeshDescriptor, the payload of the first OPT_MESHDESC node at
 * or below node, depth first, or NULL. Skips NULL child slots, does not follow
 * OPT_NODEREF links and ignores model. */
// FUNCTION: XVT 0x4AE1A0
struct MeshDescriptor *
ModelMesh_FindDescriptorNodeRecursive(struct OptNode *node,
				      struct OptimizedPolyObject *model)
{
	struct MeshDescriptor *descriptor;
	int childIndex;

	if (node == NULL) {
		return NULL;
	}
	if (node->nodeType == OPT_MESHDESC) {
		return (struct MeshDescriptor *)node->payload;
	}

	for (childIndex = 0; childIndex < node->childCount; ++childIndex) {
		if (node->pChildren[childIndex] != NULL) {
			descriptor = ModelMesh_FindDescriptorNodeRecursive(
				node->pChildren[childIndex], model);
			if (descriptor != NULL) {
				return descriptor;
			}
		}
	}

	return NULL;
}

/* Returns the MeshDescriptor of mesh meshIndex of the object type's model, or
 * NULL; NULL also when g_loadedModels holds no handle for it, meshIndex is
 * negative or its assetFlags lacks the 0x1 bit. Mesh n is root node n, or n + 1
 * when the first root is an OPT_TEXTURE, cut to the last root; the other
 * ModelMesh getters find a mesh the same way. */
// FUNCTION: XVT 0x4AE200
struct MeshDescriptor *ModelMesh_GetDescriptor(int objectType, int meshIndex)
{
	uint16_t modelHandle;
	struct OptimizedPolyObject *model;
	struct OptNode **rootNodes;

	modelHandle = g_loadedModels[objectType];
	if (modelHandle == 0) {
		return NULL;
	}
	if (meshIndex < 0) {
		return NULL;
	}
	if ((g_objectTypeTable[objectType].assetFlags & 1) == 0) {
		return NULL;
	}

	model = (struct OptimizedPolyObject *)Memory_GetHandleBlock(
		modelHandle);
	Memory_HandleBlockDoneStub(g_loadedModels[objectType]);
	if (model->selfMarker != model) {
		OptModel_AdjustOptimizedPolyObjectPointers(model);
	}

	rootNodes = model->rootNodes;
	if (rootNodes[0]->nodeType == OPT_TEXTURE) {
		++meshIndex;
	}
	if (meshIndex >= model->rootNodeCount) {
		meshIndex = model->rootNodeCount - 1;
	}
	return ModelMesh_FindDescriptorNodeRecursive(rootNodes[meshIndex],
						     model);
}

/* Returns the meshType of mesh meshIndex's descriptor;
 * MESH_COMPONENT_00_DEFAULT when it has none, when meshIndex is negative, when
 * g_loadedModels holds no handle or when assetFlags lacks the 0x1 bit. */
// FUNCTION: XVT 0x4AE2A0
MeshComponentType ModelMesh_GetObjectTypeMeshType(int objectType, int meshIndex)
{
	uint16_t modelHandle;
	struct OptimizedPolyObject *model;
	struct OptNode **rootNodes;
	struct MeshDescriptor *descriptor;

	modelHandle = g_loadedModels[objectType];
	if (modelHandle == 0) {
		return MESH_COMPONENT_00_DEFAULT;
	}
	if (meshIndex < 0) {
		return MESH_COMPONENT_00_DEFAULT;
	}
	if ((g_objectTypeTable[objectType].assetFlags & 1) == 0) {
		return MESH_COMPONENT_00_DEFAULT;
	}

	model = (struct OptimizedPolyObject *)Memory_GetHandleBlock(
		modelHandle);
	if (model->selfMarker != model) {
		OptModel_AdjustOptimizedPolyObjectPointers(model);
	}

	rootNodes = model->rootNodes;
	if (rootNodes[0]->nodeType == OPT_TEXTURE) {
		++meshIndex;
	}
	if (meshIndex >= model->rootNodeCount) {
		meshIndex = model->rootNodeCount - 1;
	}

	descriptor = ModelMesh_FindDescriptorNodeRecursive(rootNodes[meshIndex],
							   model);
	/* From here meshIndex holds the result, no longer a root node index: the descriptor's mesh type,
	 * or MESH_COMPONENT_00_DEFAULT without a descriptor. */
	if (descriptor != NULL) {
		meshIndex = descriptor->meshType;
	} else {
		meshIndex = MESH_COMPONENT_00_DEFAULT;
	}

	Memory_HandleBlockDoneStub(g_loadedModels[objectType]);
	return meshIndex;
}

/* Returns the vertex count of mesh meshIndex's first OPT_MESHVERTS node; 0 when
 * assetFlags lacks the 0x1 bit. Does not check for a missing model, a negative
 * meshIndex or a mesh without a vertex node. */
// FUNCTION: XVT 0x4AE340
int ModelMesh_GetVertexCount(int objectType, int meshIndex)
{
	struct OptimizedPolyObject *model;
	struct OptNode **rootNodes;
	int nodeType;
	struct OptNode *vertexNode;
	int vertexCount;

	if ((g_objectTypeTable[objectType].assetFlags & 1) == 0) {
		return 0;
	}

	model = (struct OptimizedPolyObject *)Memory_GetHandleBlock(
		g_loadedModels[objectType]);
	if (model->selfMarker != model) {
		OptModel_AdjustOptimizedPolyObjectPointers(model);
	}

	rootNodes = model->rootNodes;
	nodeType = rootNodes[0]->nodeType;
	if (nodeType == OPT_TEXTURE) {
		++meshIndex;
	}
	if (meshIndex >= model->rootNodeCount) {
		meshIndex = model->rootNodeCount - 1;
	}

	vertexNode = ModelMesh_FindFirstMeshVertsNode(rootNodes[meshIndex]);
	vertexCount = vertexNode->payloadCount;

	Memory_HandleBlockDoneStub(g_loadedModels[objectType]);
	return vertexCount;
}

/* Returns, cut to an int, the x of vertex vertexIndex, lowered to the last one,
 * of mesh meshIndex's first OPT_MESHVERTS node; 0 when assetFlags lacks the 0x1
 * bit. Does not check for a missing model, a negative index or a mesh without a
 * vertex node. */
// FUNCTION: XVT 0x4AE3C0
int ModelMesh_GetVertexX(int objectType, int meshIndex, int vertexIndex)
{
	struct OptimizedPolyObject *model;
	struct OptNode **rootNodes;
	struct OptNode *vertexNode;
	struct OptVector *vertices;
	int clampedVertexIndex;
	int result;

	if ((g_objectTypeTable[objectType].assetFlags & 1) == 0) {
		return 0;
	}

	model = (struct OptimizedPolyObject *)Memory_GetHandleBlock(
		g_loadedModels[objectType]);
	if (model->selfMarker != model) {
		OptModel_AdjustOptimizedPolyObjectPointers(model);
	}

	rootNodes = model->rootNodes;
	if (rootNodes[0]->nodeType == OPT_TEXTURE) {
		++meshIndex;
	}
	if (meshIndex >= model->rootNodeCount) {
		meshIndex = model->rootNodeCount - 1;
	}

	vertexNode = ModelMesh_FindFirstMeshVertsNode(rootNodes[meshIndex]);
	vertices = (struct OptVector *)vertexNode->payload;
	clampedVertexIndex = vertexIndex;
	if (clampedVertexIndex >= vertexNode->payloadCount) {
		clampedVertexIndex = vertexNode->payloadCount - 1;
	}
	result = (int)vertices[clampedVertexIndex].x;

	Memory_HandleBlockDoneStub(g_loadedModels[objectType]);
	return result;
}

/* Returns, cut to an int, the y of vertex vertexIndex, lowered to the last one,
 * of mesh meshIndex's first OPT_MESHVERTS node; 0 when assetFlags lacks the 0x1
 * bit. Does not check for a missing model, a negative index or a mesh without a
 * vertex node. */
// FUNCTION: XVT 0x4AE460
int ModelMesh_GetVertexY(int objectType, int meshIndex, int vertexIndex)
{
	struct OptimizedPolyObject *model;
	struct OptNode **rootNodes;
	struct OptNode *vertexNode;
	struct OptVector *vertices;
	int clampedVertexIndex;
	int result;

	if ((g_objectTypeTable[objectType].assetFlags & 1) == 0) {
		return 0;
	}

	model = (struct OptimizedPolyObject *)Memory_GetHandleBlock(
		g_loadedModels[objectType]);
	if (model->selfMarker != model) {
		OptModel_AdjustOptimizedPolyObjectPointers(model);
	}

	rootNodes = model->rootNodes;
	if (rootNodes[0]->nodeType == OPT_TEXTURE) {
		++meshIndex;
	}
	if (meshIndex >= model->rootNodeCount) {
		meshIndex = model->rootNodeCount - 1;
	}

	vertexNode = ModelMesh_FindFirstMeshVertsNode(rootNodes[meshIndex]);
	vertices = (struct OptVector *)vertexNode->payload;
	clampedVertexIndex = vertexIndex;
	if (clampedVertexIndex >= vertexNode->payloadCount) {
		clampedVertexIndex = vertexNode->payloadCount - 1;
	}
	result = (int)vertices[clampedVertexIndex].y;

	Memory_HandleBlockDoneStub(g_loadedModels[objectType]);
	return result;
}

/* Returns, cut to an int, the z of vertex vertexIndex, lowered to the last one,
 * of mesh meshIndex's first OPT_MESHVERTS node; 0 when assetFlags lacks the 0x1
 * bit. Does not check for a missing model, a negative index or a mesh without a
 * vertex node. */
// FUNCTION: XVT 0x4AE500
int ModelMesh_GetVertexZ(int objectType, int meshIndex, int vertexIndex)
{
	struct OptimizedPolyObject *model;
	struct OptNode **rootNodes;
	struct OptNode *vertexNode;
	struct OptVector *vertices;
	int clampedVertexIndex;
	int result;

	if ((g_objectTypeTable[objectType].assetFlags & 1) == 0) {
		return 0;
	}

	model = (struct OptimizedPolyObject *)Memory_GetHandleBlock(
		g_loadedModels[objectType]);
	if (model->selfMarker != model) {
		OptModel_AdjustOptimizedPolyObjectPointers(model);
	}

	rootNodes = model->rootNodes;
	if (rootNodes[0]->nodeType == OPT_TEXTURE) {
		++meshIndex;
	}
	if (meshIndex >= model->rootNodeCount) {
		meshIndex = model->rootNodeCount - 1;
	}

	vertexNode = ModelMesh_FindFirstMeshVertsNode(rootNodes[meshIndex]);
	vertices = (struct OptVector *)vertexNode->payload;
	clampedVertexIndex = vertexIndex;
	if (clampedVertexIndex >= vertexNode->payloadCount) {
		clampedVertexIndex = vertexNode->payloadCount - 1;
	}
	result = (int)vertices[clampedVertexIndex].z;

	Memory_HandleBlockDoneStub(g_loadedModels[objectType]);
	return result;
}

/* Returns, cut to an int, center.x of mesh meshIndex's descriptor; 0 without
 * one, for a negative meshIndex, or when assetFlags lacks the 0x1 bit. Does not
 * check for a missing model. */
// FUNCTION: XVT 0x4AE5A0
int ModelMesh_GetCenterX(int objectType, int meshIndex)
{
	struct OptimizedPolyObject *model;
	struct OptNode **rootNodes;
	struct MeshDescriptor *descriptor;
	int nodeIndex;
	int result;

	if (meshIndex < 0) {
		return 0;
	}
	if ((g_objectTypeTable[objectType].assetFlags & 1) == 0) {
		return 0;
	}

	model = (struct OptimizedPolyObject *)Memory_GetHandleBlock(
		g_loadedModels[objectType]);
	if (model->selfMarker != model) {
		OptModel_AdjustOptimizedPolyObjectPointers(model);
	}

	rootNodes = model->rootNodes;
	nodeIndex = meshIndex;
	if (rootNodes[0]->nodeType == OPT_TEXTURE) {
		++nodeIndex;
	}
	if (nodeIndex >= model->rootNodeCount) {
		nodeIndex = model->rootNodeCount - 1;
	}

	descriptor = ModelMesh_FindDescriptorNodeRecursive(rootNodes[nodeIndex],
							   model);
	if (descriptor != NULL) {
		result = (int)descriptor->center.x;
	} else {
		result = 0;
	}

	Memory_HandleBlockDoneStub(g_loadedModels[objectType]);
	return result;
}

/* Returns, cut to an int, center.y of mesh meshIndex's descriptor; 0 without
 * one, for a negative meshIndex, or when assetFlags lacks the 0x1 bit. Does not
 * check for a missing model. */
// FUNCTION: XVT 0x4AE640
int ModelMesh_GetCenterY(int objectType, int meshIndex)
{
	struct OptimizedPolyObject *model;
	struct OptNode **rootNodes;
	struct MeshDescriptor *descriptor;
	int rootNodeCount;
	int result;

	if (meshIndex < 0) {
		return 0;
	}
	if ((g_objectTypeTable[objectType].assetFlags & 1) == 0) {
		return 0;
	}

	model = (struct OptimizedPolyObject *)Memory_GetHandleBlock(
		g_loadedModels[objectType]);
	if (model->selfMarker != model) {
		OptModel_AdjustOptimizedPolyObjectPointers(model);
	}

	rootNodes = model->rootNodes;
	if (rootNodes[0]->nodeType == OPT_TEXTURE) {
		++meshIndex;
	}
	rootNodeCount = model->rootNodeCount;
	if (rootNodeCount <= meshIndex) {
		meshIndex = rootNodeCount - 1;
	}

	descriptor = ModelMesh_FindDescriptorNodeRecursive(rootNodes[meshIndex],
							   model);
	if (descriptor != NULL) {
		result = (int)descriptor->center.y;
	} else {
		result = 0;
	}

	Memory_HandleBlockDoneStub(g_loadedModels[objectType]);
	return result;
}

/* Returns, cut to an int, center.z of mesh meshIndex's descriptor; 0 without
 * one, for a negative meshIndex, or when assetFlags lacks the 0x1 bit. Does not
 * check for a missing model. */
// FUNCTION: XVT 0x4AE6E0
int ModelMesh_GetCenterZ(int objectType, int meshIndex)
{
	struct OptimizedPolyObject *model;
	struct OptNode **rootNodes;
	struct MeshDescriptor *descriptor;
	int result;

	if (meshIndex < 0) {
		return 0;
	}
	if ((g_objectTypeTable[objectType].assetFlags & 1) == 0) {
		return 0;
	}

	model = (struct OptimizedPolyObject *)Memory_GetHandleBlock(
		g_loadedModels[objectType]);
	if (model->selfMarker != model) {
		OptModel_AdjustOptimizedPolyObjectPointers(model);
	}

	rootNodes = model->rootNodes;
	if (rootNodes[0]->nodeType == OPT_TEXTURE) {
		++meshIndex;
	}
	meshIndex = meshIndex < model->rootNodeCount ? meshIndex
						     : model->rootNodeCount - 1;

	descriptor = ModelMesh_FindDescriptorNodeRecursive(rootNodes[meshIndex],
							   model);
	if (descriptor != NULL) {
		result = (int)descriptor->center.z;
	} else {
		result = 0;
	}

	Memory_HandleBlockDoneStub(g_loadedModels[objectType]);
	return result;
}

/* Returns, cut to an int, boxMin.x of mesh meshIndex's descriptor; 0 without
 * one, for a negative meshIndex, or when assetFlags lacks the 0x1 bit. Nothing
 * calls this. */
// FUNCTION: XVT 0x4AE780
int ModelMesh_GetBoundsMinX(int objectType, int meshIndex)
{
	struct OptimizedPolyObject *model;
	struct OptNode **rootNodes;
	struct MeshDescriptor *descriptor;
	int nodeIndex;
	int result;

	if (meshIndex < 0) {
		return 0;
	}
	if ((g_objectTypeTable[objectType].assetFlags & 1) == 0) {
		return 0;
	}

	model = (struct OptimizedPolyObject *)Memory_GetHandleBlock(
		g_loadedModels[objectType]);
	if (model->selfMarker != model) {
		OptModel_AdjustOptimizedPolyObjectPointers(model);
	}

	rootNodes = model->rootNodes;
	nodeIndex = meshIndex;
	if (rootNodes[0]->nodeType == OPT_TEXTURE) {
		++nodeIndex;
	}
	if (nodeIndex >= model->rootNodeCount) {
		nodeIndex = model->rootNodeCount - 1;
	}

	descriptor = ModelMesh_FindDescriptorNodeRecursive(rootNodes[nodeIndex],
							   model);
	if (descriptor != NULL) {
		result = (int)descriptor->boxMin.x;
	} else {
		result = 0;
	}

	Memory_HandleBlockDoneStub(g_loadedModels[objectType]);
	return result;
}

/* Returns, cut to an int, boxMin.y of mesh meshIndex's descriptor; 0 without
 * one, for a negative meshIndex, or when assetFlags lacks the 0x1 bit. Nothing
 * calls this. */
// FUNCTION: XVT 0x4AE820
int ModelMesh_GetBoundsMinY(int objectType, int meshIndex)
{
	struct OptimizedPolyObject *model;
	struct OptNode **rootNodes;
	struct MeshDescriptor *descriptor;
	int nodeIndex;
	int result;

	if (meshIndex < 0) {
		return 0;
	}
	if ((g_objectTypeTable[objectType].assetFlags & 1) == 0) {
		return 0;
	}

	model = (struct OptimizedPolyObject *)Memory_GetHandleBlock(
		g_loadedModels[objectType]);
	if (model->selfMarker != model) {
		OptModel_AdjustOptimizedPolyObjectPointers(model);
	}

	rootNodes = model->rootNodes;
	nodeIndex = meshIndex;
	if (rootNodes[0]->nodeType == OPT_TEXTURE) {
		++nodeIndex;
	}
	if (nodeIndex >= model->rootNodeCount) {
		nodeIndex = model->rootNodeCount - 1;
	}
	descriptor = ModelMesh_FindDescriptorNodeRecursive(rootNodes[nodeIndex],
							   model);
	if (descriptor != NULL) {
		result = (int)descriptor->boxMin.y;
	} else {
		result = 0;
	}

	Memory_HandleBlockDoneStub(g_loadedModels[objectType]);
	return result;
}

/* Returns, cut to an int, boxMin.z of mesh meshIndex's descriptor; 0 without
 * one, for a negative meshIndex, or when assetFlags lacks the 0x1 bit. Nothing
 * calls this. */
// FUNCTION: XVT 0x4AE8C0
int ModelMesh_GetBoundsMinZ(int objectType, int meshIndex)
{
	struct OptimizedPolyObject *model;
	struct OptNode **rootNodes;
	struct MeshDescriptor *descriptor;
	int result;

	if (meshIndex < 0) {
		return 0;
	}
	if ((g_objectTypeTable[objectType].assetFlags & 1) == 0) {
		return 0;
	}

	model = (struct OptimizedPolyObject *)Memory_GetHandleBlock(
		g_loadedModels[objectType]);
	if (model->selfMarker != model) {
		OptModel_AdjustOptimizedPolyObjectPointers(model);
	}

	rootNodes = model->rootNodes;
	if (rootNodes[0]->nodeType == OPT_TEXTURE) {
		++meshIndex;
	}
	meshIndex = meshIndex < model->rootNodeCount ? meshIndex
						     : model->rootNodeCount - 1;

	descriptor = ModelMesh_FindDescriptorNodeRecursive(rootNodes[meshIndex],
							   model);
	if (descriptor != NULL) {
		result = (int)descriptor->boxMin.z;
	} else {
		result = 0;
	}

	Memory_HandleBlockDoneStub(g_loadedModels[objectType]);
	return result;
}

/* Returns, cut to an int, boxMax.x of mesh meshIndex's descriptor; 0 without
 * one, for a negative meshIndex, or when assetFlags lacks the 0x1 bit. Nothing
 * calls this. */
// FUNCTION: XVT 0x4AE960
int ModelMesh_GetBoundsMaxX(int objectType, int meshIndex)
{
	struct OptimizedPolyObject *model;
	struct OptNode **rootNodes;
	struct MeshDescriptor *descriptor;
	int nodeIndex;
	int result;

	if (meshIndex < 0) {
		return 0;
	}
	if ((g_objectTypeTable[objectType].assetFlags & 1) == 0) {
		return 0;
	}

	model = (struct OptimizedPolyObject *)Memory_GetHandleBlock(
		g_loadedModels[objectType]);
	if (model->selfMarker != model) {
		OptModel_AdjustOptimizedPolyObjectPointers(model);
	}

	rootNodes = model->rootNodes;
	nodeIndex = meshIndex;
	if (rootNodes[0]->nodeType == OPT_TEXTURE) {
		++nodeIndex;
	}
	if (nodeIndex >= model->rootNodeCount) {
		nodeIndex = model->rootNodeCount - 1;
	}

	descriptor = ModelMesh_FindDescriptorNodeRecursive(rootNodes[nodeIndex],
							   model);
	if (descriptor != NULL) {
		result = (int)descriptor->boxMax.x;
	} else {
		result = 0;
	}

	Memory_HandleBlockDoneStub(g_loadedModels[objectType]);
	return result;
}

/* Returns, cut to an int, boxMax.y of mesh meshIndex's descriptor; 0 without
 * one, for a negative meshIndex, or when assetFlags lacks the 0x1 bit. Nothing
 * calls this. */
// FUNCTION: XVT 0x4AEA00
int ModelMesh_GetBoundsMaxY(int objectType, int meshIndex)
{
	struct OptimizedPolyObject *model;
	struct OptNode **rootNodes;
	struct MeshDescriptor *descriptor;
	int result;

	if (meshIndex < 0) {
		return 0;
	}
	if ((g_objectTypeTable[objectType].assetFlags & 1) == 0) {
		return 0;
	}

	model = (struct OptimizedPolyObject *)Memory_GetHandleBlock(
		g_loadedModels[objectType]);
	if (model->selfMarker != model) {
		OptModel_AdjustOptimizedPolyObjectPointers(model);
	}

	rootNodes = model->rootNodes;
	if (rootNodes[0]->nodeType == OPT_TEXTURE) {
		++meshIndex;
	}
	meshIndex = meshIndex < model->rootNodeCount ? meshIndex
						     : model->rootNodeCount - 1;

	descriptor = ModelMesh_FindDescriptorNodeRecursive(rootNodes[meshIndex],
							   model);
	if (descriptor != NULL) {
		result = (int)descriptor->boxMax.y;
	} else {
		result = 0;
	}

	Memory_HandleBlockDoneStub(g_loadedModels[objectType]);
	return result;
}

/* Returns, cut to an int, boxMax.z of mesh meshIndex's descriptor; 0 without
 * one, for a negative meshIndex, or when assetFlags lacks the 0x1 bit. Nothing
 * calls this. */
// FUNCTION: XVT 0x4AEAA0
int ModelMesh_GetBoundsMaxZ(int objectType, int meshIndex)
{
	struct OptimizedPolyObject *model;
	struct OptNode **rootNodes;
	struct MeshDescriptor *descriptor;
	int result;

	if (meshIndex < 0) {
		return 0;
	}
	if ((g_objectTypeTable[objectType].assetFlags & 1) == 0) {
		return 0;
	}

	model = (struct OptimizedPolyObject *)Memory_GetHandleBlock(
		g_loadedModels[objectType]);
	if (model->selfMarker != model) {
		OptModel_AdjustOptimizedPolyObjectPointers(model);
	}

	rootNodes = model->rootNodes;
	if (rootNodes[0]->nodeType == OPT_TEXTURE) {
		++meshIndex;
	}
	meshIndex = meshIndex < model->rootNodeCount ? meshIndex
						     : model->rootNodeCount - 1;

	descriptor = ModelMesh_FindDescriptorNodeRecursive(rootNodes[meshIndex],
							   model);
	if (descriptor != NULL) {
		result = (int)descriptor->boxMax.z;
	} else {
		result = 0;
	}

	Memory_HandleBlockDoneStub(g_loadedModels[objectType]);
	return result;
}

/* Returns the targetId of mesh meshIndex's descriptor; 0 without one, for a
 * negative meshIndex, or when assetFlags lacks the 0x1 bit. Does not check for
 * a missing model. */
// FUNCTION: XVT 0x4AEB40
int ModelMesh_GetTargetId(int objectType, int meshIndex)
{
	struct OptimizedPolyObject *model;
	struct OptNode **rootNodes;
	struct MeshDescriptor *descriptor;

	if (meshIndex < 0) {
		return 0;
	}
	if ((g_objectTypeTable[objectType].assetFlags & 1) == 0) {
		return 0;
	}

	model = (struct OptimizedPolyObject *)Memory_GetHandleBlock(
		g_loadedModels[objectType]);
	if (model->selfMarker != model) {
		OptModel_AdjustOptimizedPolyObjectPointers(model);
	}

	rootNodes = model->rootNodes;
	if (rootNodes[0]->nodeType == OPT_TEXTURE) {
		++meshIndex;
	}
	if (model->rootNodeCount <= meshIndex) {
		meshIndex = model->rootNodeCount - 1;
	}

	descriptor = ModelMesh_FindDescriptorNodeRecursive(rootNodes[meshIndex],
							   model);
	/* From here meshIndex holds the result, no longer a root node index: the descriptor's target id,
	 * or 0 without a descriptor. */
	if (descriptor != NULL) {
		meshIndex = descriptor->targetId;
	} else {
		meshIndex = 0;
	}

	Memory_HandleBlockDoneStub(g_loadedModels[objectType]);
	return meshIndex;
}

/* Returns, cut to an int, targetPoint.x of mesh meshIndex's descriptor when its
 * targetId is nonzero, else center.x; 0 without a descriptor, for a negative
 * meshIndex, or when assetFlags lacks the 0x1 bit. */
// FUNCTION: XVT 0x4AEBE0
int ModelMesh_GetComponentFocusX(int objectType, int meshIndex)
{
	struct OptimizedPolyObject *model;
	struct OptNode **rootNodes;
	struct MeshDescriptor *descriptor;
	int value;

	if (meshIndex < 0) {
		return 0;
	}
	if ((g_objectTypeTable[objectType].assetFlags & 1) == 0) {
		return 0;
	}

	model = (struct OptimizedPolyObject *)Memory_GetHandleBlock(
		g_loadedModels[objectType]);
	if (model->selfMarker != model) {
		OptModel_AdjustOptimizedPolyObjectPointers(model);
	}

	rootNodes = model->rootNodes;
	if (rootNodes[0]->nodeType == OPT_TEXTURE) {
		++meshIndex;
	}
	if (meshIndex >= model->rootNodeCount) {
		meshIndex = model->rootNodeCount - 1;
	}

	descriptor = ModelMesh_FindDescriptorNodeRecursive(rootNodes[meshIndex],
							   model);
	if (descriptor != NULL) {
		if (descriptor->targetId != 0) {
			value = (int)descriptor->targetPoint.x;
		} else {
			value = (int)descriptor->center.x;
		}
	} else {
		value = 0;
	}

	Memory_HandleBlockDoneStub(g_loadedModels[objectType]);
	return value;
}

/* Returns, cut to an int, targetPoint.y of mesh meshIndex's descriptor when its
 * targetId is nonzero, else center.y; 0 without a descriptor, for a negative
 * meshIndex, or when assetFlags lacks the 0x1 bit. */
// FUNCTION: XVT 0x4AEC90
int ModelMesh_GetComponentFocusY(int objectType, int meshIndex)
{
	struct OptimizedPolyObject *model;
	struct OptNode **rootNodes;
	struct MeshDescriptor *descriptor;
	int value;

	if (meshIndex < 0) {
		return 0;
	}
	if ((g_objectTypeTable[objectType].assetFlags & 1) == 0) {
		return 0;
	}

	model = (struct OptimizedPolyObject *)Memory_GetHandleBlock(
		g_loadedModels[objectType]);
	if (model->selfMarker != model) {
		OptModel_AdjustOptimizedPolyObjectPointers(model);
	}

	rootNodes = model->rootNodes;
	if (rootNodes[0]->nodeType == OPT_TEXTURE) {
		++meshIndex;
	}
	if (meshIndex >= model->rootNodeCount) {
		meshIndex = model->rootNodeCount - 1;
	}

	descriptor = ModelMesh_FindDescriptorNodeRecursive(rootNodes[meshIndex],
							   model);
	if (descriptor != NULL) {
		if (descriptor->targetId != 0) {
			value = (int)descriptor->targetPoint.y;
		} else {
			value = (int)descriptor->center.y;
		}
	} else {
		value = 0;
	}

	Memory_HandleBlockDoneStub(g_loadedModels[objectType]);
	return value;
}

/* Returns, cut to an int, targetPoint.z of mesh meshIndex's descriptor when its
 * targetId is nonzero, else center.z; 0 without a descriptor, for a negative
 * meshIndex, or when assetFlags lacks the 0x1 bit. */
// FUNCTION: XVT 0x4AED40
int ModelMesh_GetComponentFocusZ(int objectType, int meshIndex)
{
	struct OptimizedPolyObject *model;
	struct OptNode **rootNodes;
	struct MeshDescriptor *descriptor;
	int value;

	if (meshIndex < 0) {
		return 0;
	}
	if ((g_objectTypeTable[objectType].assetFlags & 1) == 0) {
		return 0;
	}

	model = (struct OptimizedPolyObject *)Memory_GetHandleBlock(
		g_loadedModels[objectType]);
	if (model->selfMarker != model) {
		OptModel_AdjustOptimizedPolyObjectPointers(model);
	}

	rootNodes = model->rootNodes;
	if (rootNodes[0]->nodeType == OPT_TEXTURE) {
		++meshIndex;
	}
	if (meshIndex >= model->rootNodeCount) {
		meshIndex = model->rootNodeCount - 1;
	}

	descriptor = ModelMesh_FindDescriptorNodeRecursive(rootNodes[meshIndex],
							   model);
	if (descriptor != NULL) {
		if (descriptor->targetId != 0) {
			value = (int)descriptor->targetPoint.z;
		} else {
			value = (int)descriptor->center.z;
		}
	} else {
		value = 0;
	}

	Memory_HandleBlockDoneStub(g_loadedModels[objectType]);
	return value;
}

/* Returns the largest of the three span components of mesh meshIndex's
 * descriptor, each cut to an int; 0 without a descriptor, for a negative
 * meshIndex, or when assetFlags lacks the 0x1 bit. */
// FUNCTION: XVT 0x4AEDF0
int ModelMesh_GetComponentMaxExtent(int objectType, int meshIndex)
{
	struct OptimizedPolyObject *model;
	struct OptNode **rootNodes;
	struct MeshDescriptor *descriptor;
	int extentX;
	int extentY;
	int extentZ;

	if (meshIndex < 0) {
		return 0;
	}
	if ((g_objectTypeTable[objectType].assetFlags & 1) == 0) {
		return 0;
	}
	model = (struct OptimizedPolyObject *)Memory_GetHandleBlock(
		g_loadedModels[objectType]);
	if (model->selfMarker != model) {
		OptModel_AdjustOptimizedPolyObjectPointers(model);
	}
	rootNodes = model->rootNodes;
	if (rootNodes[0]->nodeType == OPT_TEXTURE) {
		++meshIndex;
	}
	if (meshIndex >= model->rootNodeCount) {
		meshIndex = model->rootNodeCount - 1;
	}
	descriptor = ModelMesh_FindDescriptorNodeRecursive(rootNodes[meshIndex],
							   model);
	if (descriptor != NULL) {
		extentX = (int)descriptor->span.x;
		extentY = (int)descriptor->span.y;
		extentZ = (int)descriptor->span.z;
		if (extentY >= extentX && extentZ <= extentY) {
			extentX = extentY;
		} else if (extentZ >= extentX && extentZ >= extentY) {
			extentX = extentZ;
		}
	} else {
		extentX = 0;
	}
	Memory_HandleBlockDoneStub(g_loadedModels[objectType]);
	return extentX;
}

/* Returns the 0x2 bit of mesh meshIndex's componentFlags, so 2 or 0; 0 without
 * a descriptor, for a negative meshIndex, or when assetFlags lacks the 0x1 bit.
 * Spawning gives a mesh with the bit set its component hit points, and the
 * damage code damages the component of a hit mesh with it. */
// FUNCTION: XVT 0x4AEEC0
int ModelMesh_IsObjectTypeMeshDamageable(int objectType, int meshIndex)
{
	struct OptimizedPolyObject *model;
	struct OptNode **rootNodes;
	struct MeshDescriptor *descriptor;

	if (meshIndex < 0) {
		return 0;
	}
	if ((g_objectTypeTable[objectType].assetFlags & 1) == 0) {
		return 0;
	}

	model = (struct OptimizedPolyObject *)Memory_GetHandleBlock(
		g_loadedModels[objectType]);
	if (model->selfMarker != model) {
		OptModel_AdjustOptimizedPolyObjectPointers(model);
	}

	rootNodes = model->rootNodes;
	if (rootNodes[0]->nodeType == OPT_TEXTURE) {
		++meshIndex;
	}
	if (model->rootNodeCount <= meshIndex) {
		meshIndex = model->rootNodeCount - 1;
	}

	descriptor = ModelMesh_FindDescriptorNodeRecursive(rootNodes[meshIndex],
							   model);
	/* From here meshIndex holds the result, no longer a root node index: bit 1 (value 2) of the
	 * descriptor's component flags, or 0 without a descriptor. */
	if (descriptor != NULL) {
		meshIndex = descriptor->componentFlags & 2;
	} else {
		meshIndex = 0;
	}

	Memory_HandleBlockDoneStub(g_loadedModels[objectType]);
	return meshIndex;
}

/* Returns the 0x1 bit of mesh meshIndex's componentFlags; 0 without a
 * descriptor, for a negative meshIndex, or when assetFlags lacks the 0x1 bit.
 * Its one caller, collide_damagecraft, damages the hit mesh's component with
 * Craft_DamageComponent when the bit is set and either the difficulty is 0 or
 * both shield energies are 0. */
// FUNCTION: XVT 0x4AEF60
int ModelMesh_HasExplosionTypeBit0(int objectType, int meshIndex)
{
	struct OptimizedPolyObject *model;
	struct OptNode **rootNodes;
	struct MeshDescriptor *descriptor;

	if (meshIndex < 0) {
		return 0;
	}
	if ((g_objectTypeTable[objectType].assetFlags & 1) == 0) {
		return 0;
	}

	model = (struct OptimizedPolyObject *)Memory_GetHandleBlock(
		g_loadedModels[objectType]);
	if (model->selfMarker != model) {
		OptModel_AdjustOptimizedPolyObjectPointers(model);
	}

	rootNodes = model->rootNodes;
	if (rootNodes[0]->nodeType == OPT_TEXTURE) {
		++meshIndex;
	}
	if (model->rootNodeCount <= meshIndex) {
		meshIndex = model->rootNodeCount - 1;
	}

	descriptor = ModelMesh_FindDescriptorNodeRecursive(rootNodes[meshIndex],
							   model);
	/* From here meshIndex holds the result, no longer a root node index: bit 0 of the descriptor's
	 * component flags, or 0 without a descriptor. */
	if (descriptor != NULL) {
		meshIndex = descriptor->componentFlags & 1;
	} else {
		meshIndex = 0;
	}

	Memory_HandleBlockDoneStub(g_loadedModels[objectType]);
	return meshIndex;
}

/* Returns the payload of mesh meshIndex's first OPT_ROTSCALE node, 12 floats:
 * the pivot point, then three axes; NULL without one or when assetFlags lacks
 * the 0x1 bit. Does not check for a missing model or a negative meshIndex. */
// FUNCTION: XVT 0x4AF000
float *ModelMesh_GetRotScaleData(int objectType, int meshIndex)
{
	struct OptimizedPolyObject *model;
	struct OptNode **rootNodes;
	struct OptNode *rotScaleNode;
	float *result;

	if ((g_objectTypeTable[objectType].assetFlags & 1) == 0) {
		return 0;
	}

	model = (struct OptimizedPolyObject *)Memory_GetHandleBlock(
		g_loadedModels[objectType]);
	if (model->selfMarker != model) {
		OptModel_AdjustOptimizedPolyObjectPointers(model);
	}

	rootNodes = model->rootNodes;
	if (rootNodes[0]->nodeType == OPT_TEXTURE) {
		++meshIndex;
	}
	if (meshIndex >= model->rootNodeCount) {
		meshIndex = model->rootNodeCount - 1;
	}

	rotScaleNode = rootNodes[meshIndex];
	rotScaleNode = ModelMesh_FindFirstRotScaleNode(rotScaleNode);
	if (rotScaleNode != 0) {
		result = (float *)rotScaleNode->payload;
	} else {
		result = 0;
	}
	Memory_HandleBlockDoneStub(g_loadedModels[objectType]);
	return result;
}

/* Walks node and the nodes below it depth first, counting OPT_HARDPOINT nodes
 * in g_optHardpointSearchIndex, and returns the hardpoint met while the count
 * equals hardpointIndex, or NULL. Follows OPT_NODEREF links; while
 * g_cacheResolvedOptNodeRefs is set it keeps each target in the link node,
 * through XvtOpt_ResolveCached in the modern build, and in the original build
 * in its pName, blanking the first character of its name. A link that does not
 * resolve ends that branch. */
// FUNCTION: XVT 0x4AF090
struct OptNode *
ModelMesh_FindNthHardpointNodeRecursive(struct OptNode *node,
					struct OptimizedPolyObject *model,
					int hardpointIndex)
{
	struct OptNode *resolvedNode;
	struct OptNode *result;
	int childIndex;
	int visitedChildCount;
#ifndef XVT_MODERN
	char **referenceName;
#endif

	resolvedNode = node;
	if (resolvedNode == NULL) {
		return NULL;
	}
	while (resolvedNode->nodeType == OPT_NODEREF) {
		if (g_cacheResolvedOptNodeRefs != 0) {
#ifdef XVT_MODERN
			resolvedNode =
				XvtOpt_ResolveCached(model, resolvedNode);
#else

			referenceName = (char **)&resolvedNode->payload;
			if (**referenceName == '\0') {
				resolvedNode =
					(struct OptNode *)resolvedNode->pName;
			} else {
				resolvedNode->pName =
					(char *)OptModel_ResolveNodeRef(
						model, *referenceName);
				**referenceName = '\0';
				resolvedNode =
					(struct OptNode *)resolvedNode->pName;
			}
#endif
		} else {
			resolvedNode = OptModel_ResolveNodeRef(
				model, (const char *)resolvedNode->payload);
		}
		if (resolvedNode == NULL) {
			return NULL;
		}
	}
	if (resolvedNode->nodeType == OPT_HARDPOINT) {
		if (hardpointIndex == g_optHardpointSearchIndex) {
			return resolvedNode;
		}
		++g_optHardpointSearchIndex;
	}
	childIndex = 0;
	visitedChildCount = 0;
	while (resolvedNode->childCount > visitedChildCount) {
		result = ModelMesh_FindNthHardpointNodeRecursive(
			resolvedNode->pChildren[childIndex], model,
			hardpointIndex);
		if (result != NULL) {
			return result;
		}
		++childIndex;
		++visitedChildCount;
	}
	return NULL;
}

/* Sets g_optHardpointSearchIndex to 0 and returns hardpoint number
 * hardpointIndex, from 0, at or below node
 * (ModelMesh_FindNthHardpointNodeRecursive), or NULL. */
// FUNCTION: XVT 0x4AF150
struct OptNode *
ModelMesh_FindNthHardpointNode(struct OptNode *node,
			       struct OptimizedPolyObject *model,
			       int hardpointIndex)
{
	g_optHardpointSearchIndex = 0;
	return ModelMesh_FindNthHardpointNodeRecursive(node, model,
						       hardpointIndex);
}

/* Returns the number of OPT_HARDPOINT nodes at or below node, following
 * OPT_NODEREF links as ModelMesh_FindNthHardpointNodeRecursive does; a node
 * reached through two links counts twice. */
// FUNCTION: XVT 0x4AF180
int ModelMesh_CountHardpointNodesRecursive(struct OptNode *node,
					   struct OptimizedPolyObject *model)
{
	struct OptNode *resolvedNode;
	int count;
	int childIndex;
	int visitedChildCount;
#ifndef XVT_MODERN
	char **referenceName;
#endif

	count = 0;
	resolvedNode = node;
	if (resolvedNode == NULL) {
		return 0;
	}
	while (resolvedNode->nodeType == OPT_NODEREF) {
		if (g_cacheResolvedOptNodeRefs != 0) {
#ifdef XVT_MODERN
			resolvedNode =
				XvtOpt_ResolveCached(model, resolvedNode);
#else

			referenceName = (char **)&resolvedNode->payload;
			if (**referenceName == '\0') {
				resolvedNode =
					(struct OptNode *)resolvedNode->pName;
			} else {
				resolvedNode->pName =
					(char *)OptModel_ResolveNodeRef(
						model, *referenceName);
				**referenceName = '\0';
				resolvedNode =
					(struct OptNode *)resolvedNode->pName;
			}
#endif
		} else {
			resolvedNode = OptModel_ResolveNodeRef(
				model, (const char *)resolvedNode->payload);
		}
		if (resolvedNode == NULL) {
			return 0;
		}
	}
	if (resolvedNode->nodeType == OPT_HARDPOINT) {
		count = 1;
	}
	visitedChildCount = 0;
	if (resolvedNode->childCount > 0) {
		childIndex = 0;
		do {
			count += ModelMesh_CountHardpointNodesRecursive(
				resolvedNode->pChildren[childIndex], model);
			++childIndex;
			++visitedChildCount;
		} while (resolvedNode->childCount > visitedChildCount);
	}
	return count;
}

/* Returns the number of hardpoints in mesh meshIndex; 0 when assetFlags lacks
 * the 0x1 bit. Does not check for a missing model or a negative meshIndex. */
// FUNCTION: XVT 0x4AF250
int ModelMesh_CountHardpoints(int objectType, int meshIndex)
{
	struct OptimizedPolyObject *model;
	struct OptNode **rootNodes;
	int hardpointCount;

	if ((g_objectTypeTable[objectType].assetFlags & 1) == 0) {
		return 0;
	}
	model = (struct OptimizedPolyObject *)Memory_GetHandleBlock(
		g_loadedModels[objectType]);
	if (model->selfMarker != model) {
		OptModel_AdjustOptimizedPolyObjectPointers(model);
	}
	rootNodes = model->rootNodes;
	if (rootNodes[0]->nodeType == OPT_TEXTURE) {
		++meshIndex;
	}
	if (meshIndex >= model->rootNodeCount) {
		meshIndex = model->rootNodeCount - 1;
	}
	hardpointCount = ModelMesh_CountHardpointNodesRecursive(
		rootNodes[meshIndex], model);
	Memory_HandleBlockDoneStub(g_loadedModels[objectType]);
	return hardpointCount;
}

/* Returns hardpointIndex as given and ignores objectType and meshIndex.
 * FeDiskIo_BuildModelDef stores the result as a weapon slot's
 * alternateMeshHardpointIdx. */
// FUNCTION: XVT 0x4AF2D0
int ModelMesh_GetHardpointIndex(int objectType, int meshIndex,
				int hardpointIndex)
{
	(void)objectType;
	(void)meshIndex;

	return hardpointIndex;
}

/* Returns, cut to an int, the x of hardpoint hardpointIndex of mesh meshIndex
 * (ModelMesh_FindNthHardpointNode); 0 without that hardpoint or when assetFlags
 * lacks the 0x1 bit. */
// FUNCTION: XVT 0x4AF2E0
int ModelMesh_GetHardpointX(int objectType, int meshIndex, int hardpointIndex)
{
	struct OptimizedPolyObject *model;
	struct OptNode **rootNodes;
	struct OptNode *rootNode;
	struct OptNode *hardpointNode;
	int result;

	if ((g_objectTypeTable[objectType].assetFlags & 1) == 0) {
		return 0;
	}
	model = (struct OptimizedPolyObject *)Memory_GetHandleBlock(
		g_loadedModels[objectType]);
	if (model->selfMarker != model) {
		OptModel_AdjustOptimizedPolyObjectPointers(model);
	}
	rootNodes = model->rootNodes;
	if (rootNodes[0]->nodeType == OPT_TEXTURE) {
		++meshIndex;
	}
	if (meshIndex >= model->rootNodeCount) {
		meshIndex = model->rootNodeCount - 1;
	}
	rootNode = rootNodes[meshIndex];
	hardpointNode =
		ModelMesh_FindNthHardpointNode(rootNode, model, hardpointIndex);
	if (hardpointNode != NULL) {
		result = (int)((struct OptHardpoint *)hardpointNode->payload)
				 ->position.x;
	} else {
		result = 0;
	}
	Memory_HandleBlockDoneStub(g_loadedModels[objectType]);
	return result;
}

/* Returns, cut to an int and negated, the y of hardpoint hardpointIndex of mesh
 * meshIndex (ModelMesh_FindNthHardpointNode); 0 without that hardpoint or when
 * assetFlags lacks the 0x1 bit. */
// FUNCTION: XVT 0x4AF380
int ModelMesh_GetHardpointY(int objectType, int meshIndex, int hardpointIndex)
{
	struct OptimizedPolyObject *model;
	struct OptNode **rootNodes;
	struct OptNode *rootNode;
	struct OptNode *hardpointNode;
	int result;

	if ((g_objectTypeTable[objectType].assetFlags & 1) == 0) {
		return 0;
	}
	model = (struct OptimizedPolyObject *)Memory_GetHandleBlock(
		g_loadedModels[objectType]);
	if (model->selfMarker != model) {
		OptModel_AdjustOptimizedPolyObjectPointers(model);
	}
	rootNodes = model->rootNodes;
	if (rootNodes[0]->nodeType == OPT_TEXTURE) {
		++meshIndex;
	}
	if (meshIndex >= model->rootNodeCount) {
		meshIndex = model->rootNodeCount - 1;
	}
	rootNode = rootNodes[meshIndex];
	hardpointNode =
		ModelMesh_FindNthHardpointNode(rootNode, model, hardpointIndex);
	if (hardpointNode != NULL) {
		result = (int)((struct OptHardpoint *)hardpointNode->payload)
				 ->position.y;
	} else {
		result = 0;
	}
	Memory_HandleBlockDoneStub(g_loadedModels[objectType]);
	return -result;
}

/* Returns, cut to an int, the z of hardpoint hardpointIndex of mesh meshIndex
 * (ModelMesh_FindNthHardpointNode); 0 without that hardpoint or when assetFlags
 * lacks the 0x1 bit. */
// FUNCTION: XVT 0x4AF420
int ModelMesh_GetHardpointZ(int objectType, int meshIndex, int hardpointIndex)
{
	struct OptimizedPolyObject *model;
	struct OptNode **rootNodes;
	struct OptNode *rootNode;
	struct OptNode *hardpointNode;
	int rootNodeCount;
	int result;

	if ((g_objectTypeTable[objectType].assetFlags & 1) == 0) {
		return 0;
	}
	model = (struct OptimizedPolyObject *)Memory_GetHandleBlock(
		g_loadedModels[objectType]);
	if (model->selfMarker != model) {
		OptModel_AdjustOptimizedPolyObjectPointers(model);
	}
	rootNodes = model->rootNodes;
	if (rootNodes[0]->nodeType == OPT_TEXTURE) {
		++meshIndex;
	}
	rootNodeCount = model->rootNodeCount;
	if (rootNodeCount <= meshIndex) {
		meshIndex = rootNodeCount - 1;
	}
	rootNode = rootNodes[meshIndex];
	hardpointNode =
		ModelMesh_FindNthHardpointNode(rootNode, model, hardpointIndex);
	if (hardpointNode != NULL) {
		result = (int)((struct OptHardpoint *)hardpointNode->payload)
				 ->position.z;
	} else {
		result = 0;
	}
	Memory_HandleBlockDoneStub(g_loadedModels[objectType]);
	return result;
}

/* Stores the type of hardpoint hardpointIndex of mesh meshIndex in *outType and
 * its position, cut to ints, in *outX, *outY and *outZ, with the y negated; all
 * four 0 when there is no such hardpoint. Writes nothing when assetFlags lacks
 * the 0x1 bit. */
// FUNCTION: XVT 0x4AF4C0
void ModelMesh_GetHardpoint(int objectType, int meshIndex, int hardpointIndex,
			    int *outType, int *outX, int *outY, int *outZ)
{
	struct OptimizedPolyObject *model;
	struct OptNode **rootNodes;
	struct OptNode *hardpointNode;
	const struct OptHardpoint *hardpoint;
	const struct OptVector *position;

	if ((g_objectTypeTable[objectType].assetFlags & 1) == 0) {
		return;
	}

	model = (struct OptimizedPolyObject *)Memory_GetHandleBlock(
		g_loadedModels[objectType]);
	if (model->selfMarker != model) {
		OptModel_AdjustOptimizedPolyObjectPointers(model);
	}

	rootNodes = model->rootNodes;
	if (rootNodes[0]->nodeType == OPT_TEXTURE) {
		++meshIndex;
	}
	meshIndex = meshIndex < model->rootNodeCount ? meshIndex
						     : model->rootNodeCount - 1;

	hardpointNode = ModelMesh_FindNthHardpointNode(rootNodes[meshIndex],
						       model, hardpointIndex);
	if (hardpointNode == NULL) {
		*outType = 0;
		*outX = 0;
		*outY = 0;
		*outZ = 0;
	} else {
		hardpoint = (const struct OptHardpoint *)hardpointNode->payload;
		position = &hardpoint->position;
		*outType = hardpoint->hardpointType;
		*outX = (int)position->x;
		*outY = -(int)position->y;
		*outZ = (int)position->z;
	}

	Memory_HandleBlockDoneStub(g_loadedModels[objectType]);
}

/* Returns 1 when a root of the object type's model, other than an OPT_TEXTURE,
 * has a descriptor of type MESH_COMPONENT_03_FUSELAGE; else 0, also when
 * assetFlags lacks the 0x1 bit. */
// FUNCTION: XVT 0x4AF5B0
int ModelMesh_HasFuselage(int objectType)
{
	struct OptimizedPolyObject *model;
	int rootIndex;

	if ((g_objectTypeTable[objectType].assetFlags & 1) == 0) {
		return 0;
	}

	model = (struct OptimizedPolyObject *)Memory_GetHandleBlock(
		g_loadedModels[objectType]);
	if (model->selfMarker != model) {
		OptModel_AdjustOptimizedPolyObjectPointers(model);
	}

	for (rootIndex = 0; rootIndex < model->rootNodeCount; ++rootIndex) {
		struct OptNode *rootNode;
		struct MeshDescriptor *descriptor;

		rootNode = model->rootNodes[rootIndex];
		if (rootNode != NULL && rootNode->nodeType != OPT_TEXTURE) {
			descriptor = ModelMesh_FindDescriptorNodeRecursive(
				rootNode, model);
			if (descriptor != NULL &&
			    descriptor->meshType ==
				    MESH_COMPONENT_03_FUSELAGE) {
				Memory_HandleBlockDoneStub(
					g_loadedModels[objectType]);
				return 1;
			}
		}
	}

	Memory_HandleBlockDoneStub(g_loadedModels[objectType]);
	return 0;
}

/* Returns the mesh index of the MESH_COMPONENT_01_MAIN_HULL mesh whose
 * descriptor box lies nearest the point, the distance being the largest of the
 * three per-axis gaps; stops at the first box that holds the point. Returns 0
 * when assetFlags lacks the 0x1 bit, and an uninitialized value when the model
 * has no main hull mesh. */
// FUNCTION: XVT 0x4AF660
int ModelMesh_FindNearestMainHullByBounds(int objectType, int localX,
					  int localY, int localZ)
{
	float pointX;
	float pointY;
	float pointZ;
	float nearestDistance;
	float boundsDistance;
	float axisDistance;
	int nearestMeshIndex;
	int rootNodeIndex;
	struct OptimizedPolyObject *model;
	struct OptNode *rootNode;
	struct MeshDescriptor *descriptor;

	pointX = (float)localX;
	pointY = (float)localY;
	nearestDistance = 2147483648.0f;
	pointZ = (float)localZ;
	if ((g_objectTypeTable[objectType].assetFlags & 1) == 0) {
		return 0;
	}

	model = (struct OptimizedPolyObject *)Memory_GetHandleBlock(
		g_loadedModels[objectType]);
	if (model->selfMarker != model) {
		OptModel_AdjustOptimizedPolyObjectPointers(model);
	}

	for (rootNodeIndex = 0; rootNodeIndex < model->rootNodeCount;
	     ++rootNodeIndex) {
		rootNode = model->rootNodes[rootNodeIndex];
		if (rootNode->nodeType == OPT_TEXTURE) {
			continue;
		}

		descriptor =
			ModelMesh_FindDescriptorNodeRecursive(rootNode, model);
		if (descriptor == NULL ||
		    descriptor->meshType != MESH_COMPONENT_01_MAIN_HULL) {
			continue;
		}

		if (pointX > descriptor->boxMax.x) {
			axisDistance = pointX - descriptor->boxMax.x;
		} else if (pointX < descriptor->boxMin.x) {
			axisDistance = descriptor->boxMin.x - pointX;
		} else {
			axisDistance = 0.0f;
		}
		boundsDistance = axisDistance;

		if (pointY > descriptor->boxMax.y) {
			axisDistance = pointY - descriptor->boxMax.y;
		} else if (pointY < descriptor->boxMin.y) {
			axisDistance = descriptor->boxMin.y - pointY;
		} else {
			axisDistance = 0.0f;
		}
		if (boundsDistance < axisDistance) {
			boundsDistance = axisDistance;
		}

		if (pointZ > descriptor->boxMax.z) {
			axisDistance = pointZ - descriptor->boxMax.z;
		} else if (pointZ < descriptor->boxMin.z) {
			axisDistance = descriptor->boxMin.z - pointZ;
		} else {
			axisDistance = 0.0f;
		}
		if (boundsDistance < axisDistance) {
			boundsDistance = axisDistance;
		}

		if (boundsDistance < nearestDistance) {
			nearestMeshIndex = rootNodeIndex;
			nearestDistance = boundsDistance;
			if (boundsDistance == 0.0f) {
				break;
			}
		}
	}

	if (model->rootNodes[0]->nodeType == OPT_TEXTURE) {
		--nearestMeshIndex;
	}
	Memory_HandleBlockDoneStub(g_loadedModels[objectType]);
	return nearestMeshIndex;
}

/* Returns the index of a vertex of mesh meshIndex's first OPT_MESHVERTS node,
 * for nearestRank 0 the one nearest the point. It leaves out the last two
 * vertices when there are more than two, and looks at the first 256 at most.
 * For a nearestRank above 0 each pass keeps the best distance found by the
 * passes before, so it finds no new vertex and the swaps move other entries:
 * rank 1 always gives vertex 0. Returns 0 when assetFlags lacks the 0x1 bit. */
// FUNCTION: XVT 0x4AF850
int ModelMesh_FindNearestVertexForPoint(int objectType, int localX, int localY,
					int localZ, int meshIndex,
					int nearestRank)
{
	float nearestDistanceSq;
	float pointX;
	float pointY;
	float pointZ;
	float vertexDistanceSq[256];
	int vertexIndices[256];
	struct OptimizedPolyObject *model;
	struct OptNode **rootNodes;
	struct OptNode *verticesNode;
	struct OptVector *vertices;
	int rootNodeIndex;
	int vertexCount;
	int vertexIndex;
	int selectedCount;
	int candidateIndex;
	int nearestIndex;
	float deltaX;
	float deltaY;
	float deltaZ;
	float swapDistance;
	int swapIndex;

	pointX = (float)localX;
	pointY = (float)localY;
	pointZ = (float)localZ;
	if ((g_objectTypeTable[objectType].assetFlags & 1) == 0) {
		return 0;
	}

	model = (struct OptimizedPolyObject *)Memory_GetHandleBlock(
		g_loadedModels[objectType]);
	if (model->selfMarker != model) {
		OptModel_AdjustOptimizedPolyObjectPointers(model);
	}
	rootNodes = model->rootNodes;
	rootNodeIndex = meshIndex;
	if (rootNodes[0]->nodeType == OPT_TEXTURE) {
		rootNodeIndex++;
	}
	if (rootNodeIndex >= model->rootNodeCount) {
		rootNodeIndex = model->rootNodeCount - 1;
	}
	verticesNode =
		ModelMesh_FindFirstMeshVertsNode(rootNodes[rootNodeIndex]);
	vertices = (struct OptVector *)verticesNode->payload;
	vertexCount = verticesNode->payloadCount;
	if (vertexCount > 2) {
		vertexCount -= 2;
	}
	if (vertexCount > 256) {
		vertexCount = 256;
	}
	if (nearestRank >= vertexCount) {
		nearestRank = vertexCount - 1;
	}

	for (vertexIndex = 0; vertexIndex < vertexCount; vertexIndex++) {
		deltaX = vertices[vertexIndex].x - pointX;
		deltaY = vertices[vertexIndex].y - pointY;
		deltaZ = vertices[vertexIndex].z - pointZ;
		vertexDistanceSq[vertexIndex] =
			deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ;
		vertexIndices[vertexIndex] = vertexIndex;
	}

	selectedCount = 0;
	nearestDistanceSq = 4611686018427387904.0f;
	if (nearestRank + 1 > 0) {
		nearestIndex = vertexIndices[0];
		do {
			for (candidateIndex = selectedCount;
			     candidateIndex < vertexCount; candidateIndex++) {
				if (vertexDistanceSq[candidateIndex] <
				    nearestDistanceSq) {
					nearestIndex = candidateIndex;
					nearestDistanceSq = vertexDistanceSq
						[candidateIndex];
				}
			}
			swapDistance = vertexDistanceSq[selectedCount];
			vertexDistanceSq[nearestIndex] = swapDistance;
			swapIndex = vertexIndices[nearestIndex];
			vertexDistanceSq[selectedCount] = nearestDistanceSq;
			vertexIndices[nearestIndex] =
				vertexIndices[selectedCount];
			vertexIndices[selectedCount] = swapIndex;
			selectedCount++;
		} while (nearestRank + 1 > selectedCount);
	}

	Memory_HandleBlockDoneStub(g_loadedModels[objectType]);
	return vertexIndices[nearestRank];
}

/* Returns the mesh index, counting the roots that are not an OPT_TEXTURE, of
 * the first mesh whose descriptor type is MESH_COMPONENT_07_BRIDGE, or -1 when
 * there is none. */
// FUNCTION: XVT 0x4AFA30
int ModelMesh_FindBridgeIndex(struct OptimizedPolyObject *model)
{
	int rootIndex;
	int meshIndex;

	meshIndex = 0;
	for (rootIndex = 0; rootIndex < model->rootNodeCount; ++rootIndex) {
		struct OptNode *rootNode;
		struct MeshDescriptor *descriptor;

		rootNode = model->rootNodes[rootIndex];
		if (rootNode->nodeType == OPT_TEXTURE) {
			continue;
		}
		descriptor =
			ModelMesh_FindDescriptorNodeRecursive(rootNode, model);
		if (descriptor != NULL &&
		    descriptor->meshType == MESH_COMPONENT_07_BRIDGE) {
			break;
		}
		++meshIndex;
	}

	if (rootIndex < model->rootNodeCount) {
		return meshIndex;
	}
	return -1;
}

/* Fills g_objectTypeMeshCache for all 73 object types. The pointer returned is one past the end of
 * the array, not a cache entry; it must not be dereferenced. */
/* Each entry gets ModelMesh_GetObjectTypeMeshCount and, per mesh, its type and
 * descriptor. FeDiskIo_InitResources calls this after
 * FeDiskIo_LoadResources. */
// FUNCTION: XVT 0x4AFA90
struct ModelMeshObjectTypeCache *ModelMesh_BuildObjectTypeMeshCache(void)
{
	struct ModelMeshObjectTypeCache *cache;
	int objectType;

	objectType = 0;
	do {
		int meshIndex;
		int meshCount;

		cache = &g_objectTypeMeshCache[objectType];
		meshIndex = 0;
		meshCount = ModelMesh_GetObjectTypeMeshCount(objectType);
		cache->meshCount = meshCount;
		while (meshIndex < meshCount) {
			cache->meshTypes[meshIndex] =
				ModelMesh_GetObjectTypeMeshType(objectType,
								meshIndex);
			cache->meshDescriptors[meshIndex] =
				ModelMesh_GetDescriptor(objectType, meshIndex);
			++meshIndex;
		}

		++objectType;
	} while (objectType < 73);

	return &g_objectTypeMeshCache[73];
}
