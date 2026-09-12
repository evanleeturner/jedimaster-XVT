#include "xvt/assets/model_mesh.h"
#ifdef XVT_MODERN
#include "xvt_runtime/assets/opt_native.h"
#endif
#include "xvt/assets/model_mesh_internal.h"

#include "xvt/assets/object_type.h"
#include "xvt/math/math.h"
#include "xvt/math/trig2.h"
#include "xvt/util/memory.h"

// GLOBAL: XVT 0x9D8A58
int g_rotatedX = 0;
// GLOBAL: XVT 0x9D8A54
int g_rotatedY = 0;
// GLOBAL: XVT 0x9D8B60
int g_rotatedZ = 0;

// GLOBAL: XVT 0xA00870
ModelMeshObjectTypeCache g_objectTypeMeshCache[73] = { 0 };
// GLOBAL: XVT 0x528128
int g_optHardpointSearchIndex = 0;

// FLAGS: /O2 /G5
// FUNCTION: XVT 0x4285A0
void ModelMesh_ApplyAnimatedMeshRotationToPoint(int16_t angleQ16, int modelType, int meshIndex, int localX,
												int localY, int localZ) {
	float* rotScaleData;
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
	rotScaleData = ModelMesh_GetRotScaleData(modelType, meshIndex);
	if (rotScaleData == NULL)
		return;

	axisX = (int)rotScaleData[3];
	axisY = (int)rotScaleData[4];
	axisZ = (int)rotScaleData[5];
	cosine = trig2_getsignedcos(angleQ16);
	sine = trig2_getsignedsin(angleQ16);
	if (cosine >= 0) {
		const int oneMinusCosine = MODEL_MESH_Q15_ONE - cosine;

		coefficient00 = Math_RodriguesTermNonnegativeCos(axisX, axisX, oneMinusCosine, cosine);
		coefficient01 =
			Math_RodriguesTermNonnegativeCos(axisX, axisY, oneMinusCosine, Math_MulQ15(sine, axisZ));
		coefficient02 =
			Math_RodriguesTermNonnegativeCos(axisX, axisZ, oneMinusCosine, -Math_MulQ15(sine, axisY));
		coefficient10 =
			Math_RodriguesTermNonnegativeCos(axisX, axisY, oneMinusCosine, -Math_MulQ15(sine, axisZ));
		coefficient11 = Math_RodriguesTermNonnegativeCos(axisY, axisY, oneMinusCosine, cosine);
		coefficient12 =
			Math_RodriguesTermNonnegativeCos(axisY, axisZ, oneMinusCosine, Math_MulQ15(sine, axisX));
		coefficient20 =
			Math_RodriguesTermNonnegativeCos(axisX, axisZ, oneMinusCosine, Math_MulQ15(sine, axisY));
		coefficient21 =
			Math_RodriguesTermNonnegativeCos(axisY, axisZ, oneMinusCosine, -Math_MulQ15(sine, axisX));
		coefficient22 = Math_RodriguesTermNonnegativeCos(axisZ, axisZ, oneMinusCosine, cosine);
	} else {
		coefficient00 = Math_RodriguesTermNegativeCos(axisX, axisX, -cosine, cosine);
		coefficient01 = Math_RodriguesTermNegativeCos(axisX, axisY, -cosine, Math_MulQ15(sine, axisZ));
		coefficient02 = Math_RodriguesTermNegativeCos(axisX, axisZ, -cosine, -Math_MulQ15(sine, axisY));
		coefficient10 = Math_RodriguesTermNegativeCos(axisX, axisY, -cosine, -Math_MulQ15(sine, axisZ));
		coefficient11 = Math_RodriguesTermNegativeCos(axisY, axisY, -cosine, cosine);
		coefficient12 = Math_RodriguesTermNegativeCos(axisY, axisZ, -cosine, Math_MulQ15(sine, axisX));
		coefficient20 = Math_RodriguesTermNegativeCos(axisX, axisZ, -cosine, Math_MulQ15(sine, axisY));
		coefficient21 = Math_RodriguesTermNegativeCos(axisY, axisZ, -cosine, -Math_MulQ15(sine, axisX));
		coefficient22 = Math_RodriguesTermNegativeCos(axisZ, axisZ, -cosine, cosine);
	}

	localX -= (int)rotScaleData[0];
	localY += (int)rotScaleData[1];
	localZ -= (int)rotScaleData[2];
	transformedX = Math_Dot3Q15Wrapped(localX, localY, localZ, coefficient00, coefficient10, coefficient20);
	transformedY = Math_Dot3Q15Wrapped(localX, localY, localZ, coefficient01, coefficient11, coefficient21);
	transformedZ = Math_Dot3Q15Wrapped(localX, localY, localZ, coefficient02, coefficient12, coefficient22);
	transformedY -= (int)rotScaleData[1];
	transformedZ += (int)rotScaleData[2];
	transformedX += (int)rotScaleData[0];
	g_rotatedX = transformedX;
	g_rotatedY = transformedY;
	g_rotatedZ = transformedZ;
}

// FUNCTION: XVT 0x4ADC40
int ModelMesh_GetObjectTypeMeshCount(int objectType) {
	uint16_t modelHandle;
	OptimizedPolyObject* model;
	int meshCount;

	modelHandle = g_loadedModels[objectType];
	if (modelHandle == 0)
		return 0;
	if ((g_modelTypeTable[objectType].assetFlags & 1) == 0)
		return 0;

	model = (OptimizedPolyObject*)Memory_LockHandle(modelHandle);
	if (model->selfMarker != model)
		OptModel_AdjustOptimizedPolyObjectPointers(model);
	meshCount = model->rootNodeCount;
	if (model->rootNodes[0]->nodeType == OPT_TEXTURE)
		--meshCount;
	Memory_UnlockHandle(g_loadedModels[objectType]);

	if (meshCount > 50)
		meshCount = 50;

	return meshCount;
}

// FUNCTION: XVT 0x4ADCC0
OptNode* ModelMesh_FindFirstMeshVertsNode(OptNode* node) {
	OptNode* result;
	int childIndex;

	if (node == 0) {
		return 0;
	}

	if (node->nodeType == OPT_MESHVERTS) {
		return node;
	}

	for (childIndex = 0; childIndex < node->childCount; childIndex++) {
		if (node->pChildren[childIndex] != 0) {
			result = ModelMesh_FindFirstMeshVertsNode(node->pChildren[childIndex]);
			if (result != 0) {
				return result;
			}
		}
	}

	return 0;
}

// FUNCTION: XVT 0x4ADD10
OptNode* ModelMesh_FindFirstRotScaleNode(OptNode* node) {
	OptNode* result;
	int childIndex;

	if (node == 0) {
		return 0;
	}

	if (node->nodeType == OPT_ROTSCALE) {
		return node;
	}

	for (childIndex = 0; childIndex < node->childCount; childIndex++) {
		if (node->pChildren[childIndex] != 0) {
			result = ModelMesh_FindFirstRotScaleNode(node->pChildren[childIndex]);
			if (result != 0) {
				return result;
			}
		}
	}

	return 0;
}

// FUNCTION: XVT 0x4AE1A0
MeshDescriptor* ModelMesh_FindDescriptorNodeRecursive(OptNode* node, OptimizedPolyObject* model) {
	MeshDescriptor* descriptor;
	int childIndex;

	if (node == NULL) {
		return NULL;
	}
	if (node->nodeType == OPT_MESHDESC) {
		return (MeshDescriptor*)node->param2;
	}

	for (childIndex = 0; childIndex < node->childCount; ++childIndex) {
		if (node->pChildren[childIndex] != NULL) {
			descriptor = ModelMesh_FindDescriptorNodeRecursive(node->pChildren[childIndex], model);
			if (descriptor != NULL) {
				return descriptor;
			}
		}
	}

	return NULL;
}

// FUNCTION: XVT 0x4AE200
MeshDescriptor* ModelMesh_GetDescriptor(int modelType, int meshIndex) {
	uint16_t modelHandle;
	OptimizedPolyObject* model;
	OptNode** rootNodes;

	modelHandle = g_loadedModels[modelType];
	if (modelHandle == 0)
		return NULL;
	if (meshIndex < 0)
		return NULL;
	if ((g_modelTypeTable[modelType].assetFlags & 1) == 0)
		return NULL;

	model = (OptimizedPolyObject*)Memory_LockHandle(modelHandle);
	Memory_UnlockHandle(g_loadedModels[modelType]);
	if (model->selfMarker != model)
		OptModel_AdjustOptimizedPolyObjectPointers(model);

	rootNodes = model->rootNodes;
	if (rootNodes[0]->nodeType == OPT_TEXTURE)
		++meshIndex;
	if (meshIndex >= model->rootNodeCount)
		meshIndex = model->rootNodeCount - 1;
	return ModelMesh_FindDescriptorNodeRecursive(rootNodes[meshIndex], model);
}

// FUNCTION: XVT 0x4AE2A0
MeshComponentType ModelMesh_GetObjectTypeMeshType(int objectType, int meshIndex) {
	uint16_t modelHandle;
	OptimizedPolyObject* model;
	OptNode** rootNodes;
	MeshDescriptor* descriptor;

	modelHandle = g_loadedModels[objectType];
	if (modelHandle == 0)
		return MESH_COMPONENT_00_HULL;
	if (meshIndex < 0)
		return MESH_COMPONENT_00_HULL;
	if ((g_modelTypeTable[objectType].assetFlags & 1) == 0)
		return MESH_COMPONENT_00_HULL;

	model = (OptimizedPolyObject*)Memory_LockHandle(modelHandle);
	if (model->selfMarker != model)
		OptModel_AdjustOptimizedPolyObjectPointers(model);

	rootNodes = model->rootNodes;
	if (rootNodes[0]->nodeType == OPT_TEXTURE)
		++meshIndex;
	if (meshIndex >= model->rootNodeCount)
		meshIndex = model->rootNodeCount - 1;

	descriptor = ModelMesh_FindDescriptorNodeRecursive(rootNodes[meshIndex], model);
	if (descriptor != NULL)
		meshIndex = descriptor->meshType;
	else
		meshIndex = MESH_COMPONENT_00_HULL;

	Memory_UnlockHandle(g_loadedModels[objectType]);
	return meshIndex;
}

// FUNCTION: XVT 0x4AE340
int ModelMesh_GetVertexCount(int modelType, int meshIndex) {
	OptimizedPolyObject* model;
	OptNode** rootNodes;
	int nodeType;
	OptNode* vertexNode;
	int vertexCount;

	if ((g_modelTypeTable[modelType].assetFlags & 1) == 0)
		return 0;

	model = (OptimizedPolyObject*)Memory_LockHandle(g_loadedModels[modelType]);
	if (model->selfMarker != model)
		OptModel_AdjustOptimizedPolyObjectPointers(model);

	rootNodes = model->rootNodes;
	nodeType = rootNodes[0]->nodeType;
	if (nodeType == OPT_TEXTURE)
		++meshIndex;
	if (meshIndex >= model->rootNodeCount)
		meshIndex = model->rootNodeCount - 1;

	vertexNode = ModelMesh_FindFirstMeshVertsNode(rootNodes[meshIndex]);
	vertexCount = vertexNode->param1;

	Memory_UnlockHandle(g_loadedModels[modelType]);
	return vertexCount;
}

// FUNCTION: XVT 0x4AE3C0
int ModelMesh_GetVertexX(int modelType, int meshIndex, int vertexIndex) {
	OptimizedPolyObject* model;
	OptNode** rootNodes;
	OptNode* vertexNode;
	OptVector* vertices;
	int clampedVertexIndex;
	int result;

	if ((g_modelTypeTable[modelType].assetFlags & 1) == 0)
		return 0;

	model = (OptimizedPolyObject*)Memory_LockHandle(g_loadedModels[modelType]);
	if (model->selfMarker != model)
		OptModel_AdjustOptimizedPolyObjectPointers(model);

	rootNodes = model->rootNodes;
	if (rootNodes[0]->nodeType == OPT_TEXTURE)
		++meshIndex;
	if (meshIndex >= model->rootNodeCount)
		meshIndex = model->rootNodeCount - 1;

	vertexNode = ModelMesh_FindFirstMeshVertsNode(rootNodes[meshIndex]);
	vertices = (OptVector*)vertexNode->param2;
	clampedVertexIndex = vertexIndex;
	if (clampedVertexIndex >= vertexNode->param1)
		clampedVertexIndex = vertexNode->param1 - 1;
	result = (int)vertices[clampedVertexIndex].x;

	Memory_UnlockHandle(g_loadedModels[modelType]);
	return result;
}

// FUNCTION: XVT 0x4AE460
int ModelMesh_GetVertexY(int modelType, int meshIndex, int vertexIndex) {
	OptimizedPolyObject* model;
	OptNode** rootNodes;
	OptNode* vertexNode;
	OptVector* vertices;
	int clampedVertexIndex;
	int result;

	if ((g_modelTypeTable[modelType].assetFlags & 1) == 0)
		return 0;

	model = (OptimizedPolyObject*)Memory_LockHandle(g_loadedModels[modelType]);
	if (model->selfMarker != model)
		OptModel_AdjustOptimizedPolyObjectPointers(model);

	rootNodes = model->rootNodes;
	if (rootNodes[0]->nodeType == OPT_TEXTURE)
		++meshIndex;
	if (meshIndex >= model->rootNodeCount)
		meshIndex = model->rootNodeCount - 1;

	vertexNode = ModelMesh_FindFirstMeshVertsNode(rootNodes[meshIndex]);
	vertices = (OptVector*)vertexNode->param2;
	clampedVertexIndex = vertexIndex;
	if (clampedVertexIndex >= vertexNode->param1)
		clampedVertexIndex = vertexNode->param1 - 1;
	result = (int)vertices[clampedVertexIndex].y;

	Memory_UnlockHandle(g_loadedModels[modelType]);
	return result;
}

// FUNCTION: XVT 0x4AE500
int ModelMesh_GetVertexZ(int modelType, int meshIndex, int vertexIndex) {
	OptimizedPolyObject* model;
	OptNode** rootNodes;
	OptNode* vertexNode;
	OptVector* vertices;
	int clampedVertexIndex;
	int result;

	if ((g_modelTypeTable[modelType].assetFlags & 1) == 0)
		return 0;

	model = (OptimizedPolyObject*)Memory_LockHandle(g_loadedModels[modelType]);
	if (model->selfMarker != model)
		OptModel_AdjustOptimizedPolyObjectPointers(model);

	rootNodes = model->rootNodes;
	if (rootNodes[0]->nodeType == OPT_TEXTURE)
		++meshIndex;
	if (meshIndex >= model->rootNodeCount)
		meshIndex = model->rootNodeCount - 1;

	vertexNode = ModelMesh_FindFirstMeshVertsNode(rootNodes[meshIndex]);
	vertices = (OptVector*)vertexNode->param2;
	clampedVertexIndex = vertexIndex;
	if (clampedVertexIndex >= vertexNode->param1)
		clampedVertexIndex = vertexNode->param1 - 1;
	result = (int)vertices[clampedVertexIndex].z;

	Memory_UnlockHandle(g_loadedModels[modelType]);
	return result;
}

// FUNCTION: XVT 0x4AE5A0
int ModelMesh_GetCenterX(int modelType, int meshIndex) {
	OptimizedPolyObject* model;
	OptNode** rootNodes;
	MeshDescriptor* descriptor;
	int nodeIndex;
	int result;

	if (meshIndex < 0)
		return 0;
	if ((g_modelTypeTable[modelType].assetFlags & 1) == 0)
		return 0;

	model = (OptimizedPolyObject*)Memory_LockHandle(g_loadedModels[modelType]);
	if (model->selfMarker != model)
		OptModel_AdjustOptimizedPolyObjectPointers(model);

	rootNodes = model->rootNodes;
	nodeIndex = meshIndex;
	if (rootNodes[0]->nodeType == OPT_TEXTURE)
		++nodeIndex;
	if (nodeIndex >= model->rootNodeCount)
		nodeIndex = model->rootNodeCount - 1;

	descriptor = ModelMesh_FindDescriptorNodeRecursive(rootNodes[nodeIndex], model);
	if (descriptor != NULL)
		result = (int)descriptor->center.x;
	else
		result = 0;

	Memory_UnlockHandle(g_loadedModels[modelType]);
	return result;
}

// FUNCTION: XVT 0x4AE640
int ModelMesh_GetCenterY(int modelType, int meshIndex) {
	OptimizedPolyObject* model;
	OptNode** rootNodes;
	MeshDescriptor* descriptor;
	int rootNodeCount;
	int result;

	if (meshIndex < 0)
		return 0;
	if ((g_modelTypeTable[modelType].assetFlags & 1) == 0)
		return 0;

	model = (OptimizedPolyObject*)Memory_LockHandle(g_loadedModels[modelType]);
	if (model->selfMarker != model)
		OptModel_AdjustOptimizedPolyObjectPointers(model);

	rootNodes = model->rootNodes;
	if (rootNodes[0]->nodeType == OPT_TEXTURE)
		++meshIndex;
	rootNodeCount = model->rootNodeCount;
	if (rootNodeCount <= meshIndex)
		meshIndex = rootNodeCount - 1;

	descriptor = ModelMesh_FindDescriptorNodeRecursive(rootNodes[meshIndex], model);
	if (descriptor != NULL)
		result = (int)descriptor->center.y;
	else
		result = 0;

	Memory_UnlockHandle(g_loadedModels[modelType]);
	return result;
}

// FUNCTION: XVT 0x4AE6E0
int ModelMesh_GetCenterZ(int modelType, int meshIndex) {
	OptimizedPolyObject* model;
	OptNode** rootNodes;
	MeshDescriptor* descriptor;
	int result;

	if (meshIndex < 0)
		return 0;
	if ((g_modelTypeTable[modelType].assetFlags & 1) == 0)
		return 0;

	model = (OptimizedPolyObject*)Memory_LockHandle(g_loadedModels[modelType]);
	if (model->selfMarker != model)
		OptModel_AdjustOptimizedPolyObjectPointers(model);

	rootNodes = model->rootNodes;
	if (rootNodes[0]->nodeType == OPT_TEXTURE)
		++meshIndex;
	meshIndex = meshIndex < model->rootNodeCount ? meshIndex : model->rootNodeCount - 1;

	descriptor = ModelMesh_FindDescriptorNodeRecursive(rootNodes[meshIndex], model);
	if (descriptor != NULL)
		result = (int)descriptor->center.z;
	else
		result = 0;

	Memory_UnlockHandle(g_loadedModels[modelType]);
	return result;
}

// FUNCTION: XVT 0x4AE780
int ModelMesh_GetBoundsMinX(int modelType, int meshIndex) {
	OptimizedPolyObject* model;
	OptNode** rootNodes;
	MeshDescriptor* descriptor;
	int nodeIndex;
	int result;

	if (meshIndex < 0)
		return 0;
	if ((g_modelTypeTable[modelType].assetFlags & 1) == 0)
		return 0;

	model = (OptimizedPolyObject*)Memory_LockHandle(g_loadedModels[modelType]);
	if (model->selfMarker != model)
		OptModel_AdjustOptimizedPolyObjectPointers(model);

	rootNodes = model->rootNodes;
	nodeIndex = meshIndex;
	if (rootNodes[0]->nodeType == OPT_TEXTURE)
		++nodeIndex;
	if (nodeIndex >= model->rootNodeCount)
		nodeIndex = model->rootNodeCount - 1;

	descriptor = ModelMesh_FindDescriptorNodeRecursive(rootNodes[nodeIndex], model);
	if (descriptor != NULL)
		result = (int)descriptor->boxMin.x;
	else
		result = 0;

	Memory_UnlockHandle(g_loadedModels[modelType]);
	return result;
}

// FUNCTION: XVT 0x4AE820
int ModelMesh_GetBoundsMinY(int modelType, int meshIndex) {
	OptimizedPolyObject* model;
	OptNode** rootNodes;
	MeshDescriptor* descriptor;
	int nodeIndex;
	int result;

	if (meshIndex < 0)
		return 0;
	if ((g_modelTypeTable[modelType].assetFlags & 1) == 0)
		return 0;

	model = (OptimizedPolyObject*)Memory_LockHandle(g_loadedModels[modelType]);
	if (model->selfMarker != model)
		OptModel_AdjustOptimizedPolyObjectPointers(model);

	rootNodes = model->rootNodes;
	nodeIndex = meshIndex;
	if (rootNodes[0]->nodeType == OPT_TEXTURE)
		++nodeIndex;
	if (nodeIndex >= model->rootNodeCount)
		nodeIndex = model->rootNodeCount - 1;
	descriptor = ModelMesh_FindDescriptorNodeRecursive(rootNodes[nodeIndex], model);
	if (descriptor != NULL)
		result = (int)descriptor->boxMin.y;
	else
		result = 0;

	Memory_UnlockHandle(g_loadedModels[modelType]);
	return result;
}

// FUNCTION: XVT 0x4AE8C0
int ModelMesh_GetBoundsMinZ(int modelType, int meshIndex) {
	OptimizedPolyObject* model;
	OptNode** rootNodes;
	MeshDescriptor* descriptor;
	int result;

	if (meshIndex < 0)
		return 0;
	if ((g_modelTypeTable[modelType].assetFlags & 1) == 0)
		return 0;

	model = (OptimizedPolyObject*)Memory_LockHandle(g_loadedModels[modelType]);
	if (model->selfMarker != model)
		OptModel_AdjustOptimizedPolyObjectPointers(model);

	rootNodes = model->rootNodes;
	if (rootNodes[0]->nodeType == OPT_TEXTURE)
		++meshIndex;
	meshIndex = meshIndex < model->rootNodeCount ? meshIndex : model->rootNodeCount - 1;

	descriptor = ModelMesh_FindDescriptorNodeRecursive(rootNodes[meshIndex], model);
	if (descriptor != NULL)
		result = (int)descriptor->boxMin.z;
	else
		result = 0;

	Memory_UnlockHandle(g_loadedModels[modelType]);
	return result;
}

// FUNCTION: XVT 0x4AE960
int ModelMesh_GetBoundsMaxX(int modelType, int meshIndex) {
	OptimizedPolyObject* model;
	OptNode** rootNodes;
	MeshDescriptor* descriptor;
	int nodeIndex;
	int result;

	if (meshIndex < 0)
		return 0;
	if ((g_modelTypeTable[modelType].assetFlags & 1) == 0)
		return 0;

	model = (OptimizedPolyObject*)Memory_LockHandle(g_loadedModels[modelType]);
	if (model->selfMarker != model)
		OptModel_AdjustOptimizedPolyObjectPointers(model);

	rootNodes = model->rootNodes;
	nodeIndex = meshIndex;
	if (rootNodes[0]->nodeType == OPT_TEXTURE)
		++nodeIndex;
	if (nodeIndex >= model->rootNodeCount)
		nodeIndex = model->rootNodeCount - 1;

	descriptor = ModelMesh_FindDescriptorNodeRecursive(rootNodes[nodeIndex], model);
	if (descriptor != NULL)
		result = (int)descriptor->boxMax.x;
	else
		result = 0;

	Memory_UnlockHandle(g_loadedModels[modelType]);
	return result;
}

// FUNCTION: XVT 0x4AEA00
int ModelMesh_GetBoundsMaxY(int modelType, int meshIndex) {
	OptimizedPolyObject* model;
	OptNode** rootNodes;
	MeshDescriptor* descriptor;
	int result;

	if (meshIndex < 0)
		return 0;
	if ((g_modelTypeTable[modelType].assetFlags & 1) == 0)
		return 0;

	model = (OptimizedPolyObject*)Memory_LockHandle(g_loadedModels[modelType]);
	if (model->selfMarker != model)
		OptModel_AdjustOptimizedPolyObjectPointers(model);

	rootNodes = model->rootNodes;
	if (rootNodes[0]->nodeType == OPT_TEXTURE)
		++meshIndex;
	meshIndex = meshIndex < model->rootNodeCount ? meshIndex : model->rootNodeCount - 1;

	descriptor = ModelMesh_FindDescriptorNodeRecursive(rootNodes[meshIndex], model);
	if (descriptor != NULL)
		result = (int)descriptor->boxMax.y;
	else
		result = 0;

	Memory_UnlockHandle(g_loadedModels[modelType]);
	return result;
}

// FUNCTION: XVT 0x4AEAA0
int ModelMesh_GetBoundsMaxZ(int modelType, int meshIndex) {
	OptimizedPolyObject* model;
	OptNode** rootNodes;
	MeshDescriptor* descriptor;
	int result;

	if (meshIndex < 0)
		return 0;
	if ((g_modelTypeTable[modelType].assetFlags & 1) == 0)
		return 0;

	model = (OptimizedPolyObject*)Memory_LockHandle(g_loadedModels[modelType]);
	if (model->selfMarker != model)
		OptModel_AdjustOptimizedPolyObjectPointers(model);

	rootNodes = model->rootNodes;
	if (rootNodes[0]->nodeType == OPT_TEXTURE)
		++meshIndex;
	meshIndex = meshIndex < model->rootNodeCount ? meshIndex : model->rootNodeCount - 1;

	descriptor = ModelMesh_FindDescriptorNodeRecursive(rootNodes[meshIndex], model);
	if (descriptor != NULL)
		result = (int)descriptor->boxMax.z;
	else
		result = 0;

	Memory_UnlockHandle(g_loadedModels[modelType]);
	return result;
}

// FUNCTION: XVT 0x4AEB40
int ModelMesh_GetTargetId(int modelType, int meshIndex) {
	OptimizedPolyObject* model;
	OptNode** rootNodes;
	MeshDescriptor* descriptor;

	if (meshIndex < 0)
		return 0;
	if ((g_modelTypeTable[modelType].assetFlags & 1) == 0)
		return 0;

	model = (OptimizedPolyObject*)Memory_LockHandle(g_loadedModels[modelType]);
	if (model->selfMarker != model)
		OptModel_AdjustOptimizedPolyObjectPointers(model);

	rootNodes = model->rootNodes;
	if (rootNodes[0]->nodeType == OPT_TEXTURE)
		++meshIndex;
	if (model->rootNodeCount <= meshIndex)
		meshIndex = model->rootNodeCount - 1;

	descriptor = ModelMesh_FindDescriptorNodeRecursive(rootNodes[meshIndex], model);
	if (descriptor != NULL)
		meshIndex = descriptor->targetId;
	else
		meshIndex = 0;

	Memory_UnlockHandle(g_loadedModels[modelType]);
	return meshIndex;
}

// FUNCTION: XVT 0x4AEBE0
int ModelMesh_GetComponentFocusX(int modelType, int meshIndex) {
	OptimizedPolyObject* model;
	OptNode** rootNodes;
	MeshDescriptor* descriptor;
	int value;

	if (meshIndex < 0)
		return 0;
	if ((g_modelTypeTable[modelType].assetFlags & 1) == 0)
		return 0;

	model = (OptimizedPolyObject*)Memory_LockHandle(g_loadedModels[modelType]);
	if (model->selfMarker != model)
		OptModel_AdjustOptimizedPolyObjectPointers(model);

	rootNodes = model->rootNodes;
	if (rootNodes[0]->nodeType == OPT_TEXTURE)
		++meshIndex;
	if (meshIndex >= model->rootNodeCount)
		meshIndex = model->rootNodeCount - 1;

	descriptor = ModelMesh_FindDescriptorNodeRecursive(rootNodes[meshIndex], model);
	if (descriptor != NULL) {
		if (descriptor->targetId != 0)
			value = (int)descriptor->target.x;
		else
			value = (int)descriptor->center.x;
	} else {
		value = 0;
	}

	Memory_UnlockHandle(g_loadedModels[modelType]);
	return value;
}

// FUNCTION: XVT 0x4AEC90
int ModelMesh_GetComponentFocusY(int modelType, int meshIndex) {
	OptimizedPolyObject* model;
	OptNode** rootNodes;
	MeshDescriptor* descriptor;
	int value;

	if (meshIndex < 0)
		return 0;
	if ((g_modelTypeTable[modelType].assetFlags & 1) == 0)
		return 0;

	model = (OptimizedPolyObject*)Memory_LockHandle(g_loadedModels[modelType]);
	if (model->selfMarker != model)
		OptModel_AdjustOptimizedPolyObjectPointers(model);

	rootNodes = model->rootNodes;
	if (rootNodes[0]->nodeType == OPT_TEXTURE)
		++meshIndex;
	if (meshIndex >= model->rootNodeCount)
		meshIndex = model->rootNodeCount - 1;

	descriptor = ModelMesh_FindDescriptorNodeRecursive(rootNodes[meshIndex], model);
	if (descriptor != NULL) {
		if (descriptor->targetId != 0)
			value = (int)descriptor->target.y;
		else
			value = (int)descriptor->center.y;
	} else {
		value = 0;
	}

	Memory_UnlockHandle(g_loadedModels[modelType]);
	return value;
}

// FUNCTION: XVT 0x4AED40
int ModelMesh_GetComponentFocusZ(int modelType, int meshIndex) {
	OptimizedPolyObject* model;
	OptNode** rootNodes;
	MeshDescriptor* descriptor;
	int value;

	if (meshIndex < 0)
		return 0;
	if ((g_modelTypeTable[modelType].assetFlags & 1) == 0)
		return 0;

	model = (OptimizedPolyObject*)Memory_LockHandle(g_loadedModels[modelType]);
	if (model->selfMarker != model)
		OptModel_AdjustOptimizedPolyObjectPointers(model);

	rootNodes = model->rootNodes;
	if (rootNodes[0]->nodeType == OPT_TEXTURE)
		++meshIndex;
	if (meshIndex >= model->rootNodeCount)
		meshIndex = model->rootNodeCount - 1;

	descriptor = ModelMesh_FindDescriptorNodeRecursive(rootNodes[meshIndex], model);
	if (descriptor != NULL) {
		if (descriptor->targetId != 0)
			value = (int)descriptor->target.z;
		else
			value = (int)descriptor->center.z;
	} else {
		value = 0;
	}

	Memory_UnlockHandle(g_loadedModels[modelType]);
	return value;
}

// FUNCTION: XVT 0x4AEDF0
int ModelMesh_GetComponentMaxExtent(int modelType, int meshIndex) {
	OptimizedPolyObject* model;
	OptNode** rootNodes;
	MeshDescriptor* descriptor;
	int extentX;
	int extentY;
	int extentZ;

	if (meshIndex < 0)
		return 0;
	if ((g_modelTypeTable[modelType].assetFlags & 1) == 0)
		return 0;
	model = (OptimizedPolyObject*)Memory_LockHandle(g_loadedModels[modelType]);
	if (model->selfMarker != model)
		OptModel_AdjustOptimizedPolyObjectPointers(model);
	rootNodes = model->rootNodes;
	if (rootNodes[0]->nodeType == OPT_TEXTURE)
		++meshIndex;
	if (meshIndex >= model->rootNodeCount)
		meshIndex = model->rootNodeCount - 1;
	descriptor = ModelMesh_FindDescriptorNodeRecursive(rootNodes[meshIndex], model);
	if (descriptor != NULL) {
		extentX = (int)descriptor->span.x;
		extentY = (int)descriptor->span.y;
		extentZ = (int)descriptor->span.z;
		if (extentY >= extentX && extentZ <= extentY)
			extentX = extentY;
		else if (extentZ >= extentX && extentZ >= extentY)
			extentX = extentZ;
	} else {
		extentX = 0;
	}
	Memory_UnlockHandle(g_loadedModels[modelType]);
	return extentX;
}

// FUNCTION: XVT 0x4AEEC0
int ModelMesh_IsObjectTypeMeshDamageable(int objectType, int meshIndex) {
	OptimizedPolyObject* model;
	OptNode** rootNodes;
	MeshDescriptor* descriptor;

	if (meshIndex < 0)
		return 0;
	if ((g_modelTypeTable[objectType].assetFlags & 1) == 0)
		return 0;

	model = (OptimizedPolyObject*)Memory_LockHandle(g_loadedModels[objectType]);
	if (model->selfMarker != model)
		OptModel_AdjustOptimizedPolyObjectPointers(model);

	rootNodes = model->rootNodes;
	if (rootNodes[0]->nodeType == OPT_TEXTURE)
		++meshIndex;
	if (model->rootNodeCount <= meshIndex)
		meshIndex = model->rootNodeCount - 1;

	descriptor = ModelMesh_FindDescriptorNodeRecursive(rootNodes[meshIndex], model);
	if (descriptor != NULL)
		meshIndex = descriptor->explosionType & 2;
	else
		meshIndex = 0;

	Memory_UnlockHandle(g_loadedModels[objectType]);
	return meshIndex;
}

// FUNCTION: XVT 0x4AEF60
int ModelMesh_HasExplosionType1(int modelType, int meshIndex) {
	OptimizedPolyObject* model;
	OptNode** rootNodes;
	MeshDescriptor* descriptor;

	if (meshIndex < 0)
		return 0;
	if ((g_modelTypeTable[modelType].assetFlags & 1) == 0)
		return 0;

	model = (OptimizedPolyObject*)Memory_LockHandle(g_loadedModels[modelType]);
	if (model->selfMarker != model)
		OptModel_AdjustOptimizedPolyObjectPointers(model);

	rootNodes = model->rootNodes;
	if (rootNodes[0]->nodeType == OPT_TEXTURE)
		++meshIndex;
	if (model->rootNodeCount <= meshIndex)
		meshIndex = model->rootNodeCount - 1;

	descriptor = ModelMesh_FindDescriptorNodeRecursive(rootNodes[meshIndex], model);
	if (descriptor != NULL)
		meshIndex = descriptor->explosionType & 1;
	else
		meshIndex = 0;

	Memory_UnlockHandle(g_loadedModels[modelType]);
	return meshIndex;
}

// FUNCTION: XVT 0x4AF000
float* ModelMesh_GetRotScaleData(int modelType, int meshIndex) {
	OptimizedPolyObject* model;
	OptNode** rootNodes;
	OptNode* rotScaleNode;
	float* result;

	if ((g_modelTypeTable[modelType].assetFlags & 1) == 0)
		return 0;

	model = (OptimizedPolyObject*)Memory_LockHandle(g_loadedModels[modelType]);
	if (model->selfMarker != model)
		OptModel_AdjustOptimizedPolyObjectPointers(model);

	rootNodes = model->rootNodes;
	if (rootNodes[0]->nodeType == OPT_TEXTURE)
		++meshIndex;
	if (meshIndex >= model->rootNodeCount)
		meshIndex = model->rootNodeCount - 1;

	rotScaleNode = rootNodes[meshIndex];
	rotScaleNode = ModelMesh_FindFirstRotScaleNode(rotScaleNode);
	if (rotScaleNode != 0)
		result = (float*)rotScaleNode->param2;
	else
		result = 0;
	Memory_UnlockHandle(g_loadedModels[modelType]);
	return result;
}

// FUNCTION: XVT 0x4AF090
OptNode* ModelMesh_FindNthHardpointNodeRecursive(OptNode* node, OptimizedPolyObject* model,
												 int hardpointIndex) {
	OptNode* resolvedNode;
	OptNode* result;
	int childIndex;
	int visitedChildCount;
#ifndef XVT_MODERN
	char** referenceName;
#endif

	resolvedNode = node;
	if (resolvedNode == NULL)
		return NULL;
	while (resolvedNode->nodeType == OPT_NODEREF) {
		if (g_cacheResolvedOptNodeRefs != 0) {
#ifdef XVT_MODERN
			resolvedNode = XvtOpt_ResolveCached(model, resolvedNode);
#else

			referenceName = (char**)&resolvedNode->param2;
			if (**referenceName == '\0') {
				resolvedNode = (OptNode*)resolvedNode->pName;
			} else {
				resolvedNode->pName = (char*)OptModel_ResolveNodeRef(model, *referenceName);
				**referenceName = '\0';
				resolvedNode = (OptNode*)resolvedNode->pName;
			}
#endif
		} else {
			resolvedNode = OptModel_ResolveNodeRef(model, (const char*)resolvedNode->param2);
		}
		if (resolvedNode == NULL)
			return NULL;
	}
	if (resolvedNode->nodeType == OPT_HARDPOINT) {
		if (hardpointIndex == g_optHardpointSearchIndex)
			return resolvedNode;
		++g_optHardpointSearchIndex;
	}
	childIndex = 0;
	visitedChildCount = 0;
	while (resolvedNode->childCount > visitedChildCount) {
		result = ModelMesh_FindNthHardpointNodeRecursive(resolvedNode->pChildren[childIndex], model,
														 hardpointIndex);
		if (result != NULL)
			return result;
		++childIndex;
		++visitedChildCount;
	}
	return NULL;
}

// FUNCTION: XVT 0x4AF150
OptNode* ModelMesh_FindNthHardpointNode(OptNode* node, OptimizedPolyObject* model, int hardpointIndex) {
	g_optHardpointSearchIndex = 0;
	return ModelMesh_FindNthHardpointNodeRecursive(node, model, hardpointIndex);
}

// FUNCTION: XVT 0x4AF180
int ModelMesh_CountHardpointNodesRecursive(OptNode* node, OptimizedPolyObject* object) {
	OptNode* resolvedNode;
	int count;
	int childIndex;
	int visitedChildCount;
#ifndef XVT_MODERN
	char** referenceName;
#endif

	count = 0;
	resolvedNode = node;
	if (resolvedNode == NULL)
		return 0;
	while (resolvedNode->nodeType == OPT_NODEREF) {
		if (g_cacheResolvedOptNodeRefs != 0) {
#ifdef XVT_MODERN
			resolvedNode = XvtOpt_ResolveCached(object, resolvedNode);
#else

			referenceName = (char**)&resolvedNode->param2;
			if (**referenceName == '\0') {
				resolvedNode = (OptNode*)resolvedNode->pName;
			} else {
				resolvedNode->pName = (char*)OptModel_ResolveNodeRef(object, *referenceName);
				**referenceName = '\0';
				resolvedNode = (OptNode*)resolvedNode->pName;
			}
#endif
		} else {
			resolvedNode = OptModel_ResolveNodeRef(object, (const char*)resolvedNode->param2);
		}
		if (resolvedNode == NULL)
			return 0;
	}
	if (resolvedNode->nodeType == OPT_HARDPOINT)
		count = 1;
	visitedChildCount = 0;
	if (resolvedNode->childCount > 0) {
		childIndex = 0;
		do {
			count += ModelMesh_CountHardpointNodesRecursive(resolvedNode->pChildren[childIndex], object);
			++childIndex;
			++visitedChildCount;
		} while (resolvedNode->childCount > visitedChildCount);
	}
	return count;
}

// FUNCTION: XVT 0x4AF250
int ModelMesh_CountHardpoints(int modelType, int meshIndex) {
	OptimizedPolyObject* model;
	OptNode** rootNodes;
	int hardpointCount;

	if ((g_modelTypeTable[modelType].assetFlags & 1) == 0)
		return 0;
	model = (OptimizedPolyObject*)Memory_LockHandle(g_loadedModels[modelType]);
	if (model->selfMarker != model)
		OptModel_AdjustOptimizedPolyObjectPointers(model);
	rootNodes = model->rootNodes;
	if (rootNodes[0]->nodeType == OPT_TEXTURE)
		++meshIndex;
	if (meshIndex >= model->rootNodeCount)
		meshIndex = model->rootNodeCount - 1;
	hardpointCount = ModelMesh_CountHardpointNodesRecursive(rootNodes[meshIndex], model);
	Memory_UnlockHandle(g_loadedModels[modelType]);
	return hardpointCount;
}

// FUNCTION: XVT 0x4AF2D0
int ModelMesh_GetAlternateHardpointIndex(int modelType, int meshIndex, int hardpointIndex) {
	(void)modelType;
	(void)meshIndex;

	return hardpointIndex;
}

// FUNCTION: XVT 0x4AF2E0
int ModelMesh_GetHardpointX(int modelIndex, int meshIndex, int hardpointIndex) {
	OptimizedPolyObject* model;
	OptNode** rootNodes;
	OptNode* rootNode;
	OptNode* hardpointNode;
	int result;

	if ((g_modelTypeTable[modelIndex].assetFlags & 1) == 0)
		return 0;
	model = (OptimizedPolyObject*)Memory_LockHandle(g_loadedModels[modelIndex]);
	if (model->selfMarker != model)
		OptModel_AdjustOptimizedPolyObjectPointers(model);
	rootNodes = model->rootNodes;
	if (rootNodes[0]->nodeType == OPT_TEXTURE)
		++meshIndex;
	if (meshIndex >= model->rootNodeCount)
		meshIndex = model->rootNodeCount - 1;
	rootNode = rootNodes[meshIndex];
	hardpointNode = ModelMesh_FindNthHardpointNode(rootNode, model, hardpointIndex);
	if (hardpointNode != NULL)
		result = (int)((OptHardpoint*)hardpointNode->param2)->position.x;
	else
		result = 0;
	Memory_UnlockHandle(g_loadedModels[modelIndex]);
	return result;
}

// FUNCTION: XVT 0x4AF380
int ModelMesh_GetHardpointY(int modelIndex, int meshIndex, int hardpointIndex) {
	OptimizedPolyObject* model;
	OptNode** rootNodes;
	OptNode* rootNode;
	OptNode* hardpointNode;
	int result;

	if ((g_modelTypeTable[modelIndex].assetFlags & 1) == 0)
		return 0;
	model = (OptimizedPolyObject*)Memory_LockHandle(g_loadedModels[modelIndex]);
	if (model->selfMarker != model)
		OptModel_AdjustOptimizedPolyObjectPointers(model);
	rootNodes = model->rootNodes;
	if (rootNodes[0]->nodeType == OPT_TEXTURE)
		++meshIndex;
	if (meshIndex >= model->rootNodeCount)
		meshIndex = model->rootNodeCount - 1;
	rootNode = rootNodes[meshIndex];
	hardpointNode = ModelMesh_FindNthHardpointNode(rootNode, model, hardpointIndex);
	if (hardpointNode != NULL)
		result = (int)((OptHardpoint*)hardpointNode->param2)->position.y;
	else
		result = 0;
	Memory_UnlockHandle(g_loadedModels[modelIndex]);
	return -result;
}

// FUNCTION: XVT 0x4AF420
int ModelMesh_GetHardpointZ(int modelIndex, int meshIndex, int hardpointIndex) {
	OptimizedPolyObject* model;
	OptNode** rootNodes;
	OptNode* rootNode;
	OptNode* hardpointNode;
	int rootNodeCount;
	int result;

	if ((g_modelTypeTable[modelIndex].assetFlags & 1) == 0)
		return 0;
	model = (OptimizedPolyObject*)Memory_LockHandle(g_loadedModels[modelIndex]);
	if (model->selfMarker != model)
		OptModel_AdjustOptimizedPolyObjectPointers(model);
	rootNodes = model->rootNodes;
	if (rootNodes[0]->nodeType == OPT_TEXTURE)
		++meshIndex;
	rootNodeCount = model->rootNodeCount;
	if (rootNodeCount <= meshIndex)
		meshIndex = rootNodeCount - 1;
	rootNode = rootNodes[meshIndex];
	hardpointNode = ModelMesh_FindNthHardpointNode(rootNode, model, hardpointIndex);
	if (hardpointNode != NULL)
		result = (int)((OptHardpoint*)hardpointNode->param2)->position.z;
	else
		result = 0;
	Memory_UnlockHandle(g_loadedModels[modelIndex]);
	return result;
}

// FUNCTION: XVT 0x4AF4C0
void ModelMesh_GetHardpoint(int modelType, int meshIndex, int hardpointIndex, int* outType, int* outX,
							int* outY, int* outZ) {
	OptimizedPolyObject* model;
	OptNode** rootNodes;
	OptNode* hardpointNode;
	const OptHardpoint* hardpoint;
	const OptVector* position;

	if ((g_modelTypeTable[modelType].assetFlags & 1) == 0) {
		return;
	}

	model = (OptimizedPolyObject*)Memory_LockHandle(g_loadedModels[modelType]);
	if (model->selfMarker != model) {
		OptModel_AdjustOptimizedPolyObjectPointers(model);
	}

	rootNodes = model->rootNodes;
	if (rootNodes[0]->nodeType == OPT_TEXTURE) {
		++meshIndex;
	}
	meshIndex = meshIndex < model->rootNodeCount ? meshIndex : model->rootNodeCount - 1;

	hardpointNode = ModelMesh_FindNthHardpointNode(rootNodes[meshIndex], model, hardpointIndex);
	if (hardpointNode == NULL) {
		*outType = 0;
		*outX = 0;
		*outY = 0;
		*outZ = 0;
	} else {
		hardpoint = (const OptHardpoint*)hardpointNode->param2;
		position = &hardpoint->position;
		*outType = hardpoint->hardpointType;
		*outX = (int)position->x;
		*outY = -(int)position->y;
		*outZ = (int)position->z;
	}

	Memory_UnlockHandle(g_loadedModels[modelType]);
}

// FUNCTION: XVT 0x4AF5B0
int ModelMesh_HasFuselage(int modelType) {
	OptimizedPolyObject* model;
	int rootIndex;

	if ((g_modelTypeTable[modelType].assetFlags & 1) == 0)
		return 0;

	model = (OptimizedPolyObject*)Memory_LockHandle(g_loadedModels[modelType]);
	if (model->selfMarker != model)
		OptModel_AdjustOptimizedPolyObjectPointers(model);

	for (rootIndex = 0; rootIndex < model->rootNodeCount; ++rootIndex) {
		OptNode* rootNode;
		MeshDescriptor* descriptor;

		rootNode = model->rootNodes[rootIndex];
		if (rootNode != NULL && rootNode->nodeType != OPT_TEXTURE) {
			descriptor = ModelMesh_FindDescriptorNodeRecursive(rootNode, model);
			if (descriptor != NULL && descriptor->meshType == MESH_COMPONENT_03_FUSELAGE) {
				Memory_UnlockHandle(g_loadedModels[modelType]);
				return 1;
			}
		}
	}

	Memory_UnlockHandle(g_loadedModels[modelType]);
	return 0;
}

// FUNCTION: XVT 0x4AF660
int ModelMesh_FindNearestLiveMainHullByBounds(int modelType, int localX, int localY, int localZ) {
	float pointX;
	float pointY;
	float pointZ;
	float nearestDistance;
	float boundsDistance;
	float axisDistance;
	int nearestMeshIndex;
	int rootNodeIndex;
	OptimizedPolyObject* model;
	OptNode* rootNode;
	MeshDescriptor* descriptor;

	pointX = (float)localX;
	pointY = (float)localY;
	nearestDistance = 2147483648.0f;
	pointZ = (float)localZ;
	if ((g_modelTypeTable[modelType].assetFlags & 1) == 0)
		return 0;

	model = (OptimizedPolyObject*)Memory_LockHandle(g_loadedModels[modelType]);
	if (model->selfMarker != model)
		OptModel_AdjustOptimizedPolyObjectPointers(model);

	for (rootNodeIndex = 0; rootNodeIndex < model->rootNodeCount; ++rootNodeIndex) {
		rootNode = model->rootNodes[rootNodeIndex];
		if (rootNode->nodeType == OPT_TEXTURE)
			continue;

		descriptor = ModelMesh_FindDescriptorNodeRecursive(rootNode, model);
		if (descriptor == NULL || descriptor->meshType != MESH_COMPONENT_01_HULL)
			continue;

		if (pointX > descriptor->boxMax.x)
			axisDistance = pointX - descriptor->boxMax.x;
		else if (pointX < descriptor->boxMin.x)
			axisDistance = descriptor->boxMin.x - pointX;
		else
			axisDistance = 0.0f;
		boundsDistance = axisDistance;

		if (pointY > descriptor->boxMax.y)
			axisDistance = pointY - descriptor->boxMax.y;
		else if (pointY < descriptor->boxMin.y)
			axisDistance = descriptor->boxMin.y - pointY;
		else
			axisDistance = 0.0f;
		if (boundsDistance < axisDistance)
			boundsDistance = axisDistance;

		if (pointZ > descriptor->boxMax.z)
			axisDistance = pointZ - descriptor->boxMax.z;
		else if (pointZ < descriptor->boxMin.z)
			axisDistance = descriptor->boxMin.z - pointZ;
		else
			axisDistance = 0.0f;
		if (boundsDistance < axisDistance)
			boundsDistance = axisDistance;

		if (boundsDistance < nearestDistance) {
			nearestMeshIndex = rootNodeIndex;
			nearestDistance = boundsDistance;
			if (boundsDistance == 0.0f)
				break;
		}
	}

	if (model->rootNodes[0]->nodeType == OPT_TEXTURE)
		--nearestMeshIndex;
	Memory_UnlockHandle(g_loadedModels[modelType]);
	return nearestMeshIndex;
}

// FUNCTION: XVT 0x4AF850
int ModelMesh_FindNearestVertexForPoint(int modelType, int localX, int localY, int localZ, int meshIndex,
										int nearestRank) {
	float nearestDistanceSq;
	float pointX;
	float pointY;
	float pointZ;
	float vertexDistanceSq[256];
	int vertexIndices[256];
	OptimizedPolyObject* model;
	OptNode** rootNodes;
	OptNode* verticesNode;
	OptVector* vertices;
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
	if ((g_modelTypeTable[modelType].assetFlags & 1) == 0) {
		return 0;
	}

	model = (OptimizedPolyObject*)Memory_LockHandle(g_loadedModels[modelType]);
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
	verticesNode = ModelMesh_FindFirstMeshVertsNode(rootNodes[rootNodeIndex]);
	vertices = (OptVector*)verticesNode->param2;
	vertexCount = verticesNode->param1;
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
		vertexDistanceSq[vertexIndex] = deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ;
		vertexIndices[vertexIndex] = vertexIndex;
	}

	selectedCount = 0;
	nearestDistanceSq = 4611686018427387904.0f;
	if (nearestRank + 1 > 0) {
		nearestIndex = vertexIndices[0];
		do {
			for (candidateIndex = selectedCount; candidateIndex < vertexCount; candidateIndex++) {
				if (vertexDistanceSq[candidateIndex] < nearestDistanceSq) {
					nearestIndex = candidateIndex;
					nearestDistanceSq = vertexDistanceSq[candidateIndex];
				}
			}
			swapDistance = vertexDistanceSq[selectedCount];
			vertexDistanceSq[nearestIndex] = swapDistance;
			swapIndex = vertexIndices[nearestIndex];
			vertexDistanceSq[selectedCount] = nearestDistanceSq;
			vertexIndices[nearestIndex] = vertexIndices[selectedCount];
			vertexIndices[selectedCount] = swapIndex;
			selectedCount++;
		} while (nearestRank + 1 > selectedCount);
	}

	Memory_UnlockHandle(g_loadedModels[modelType]);
	return vertexIndices[nearestRank];
}

// FUNCTION: XVT 0x4AFA30
int ModelMesh_FindBridgeIndex(OptimizedPolyObject* model) {
	int rootIndex;
	int meshIndex;

	meshIndex = 0;
	for (rootIndex = 0; rootIndex < model->rootNodeCount; ++rootIndex) {
		OptNode* rootNode;
		MeshDescriptor* descriptor;

		rootNode = model->rootNodes[rootIndex];
		if (rootNode->nodeType == OPT_TEXTURE) {
			continue;
		}
		descriptor = ModelMesh_FindDescriptorNodeRecursive(rootNode, model);
		if (descriptor != NULL && descriptor->meshType == MESH_COMPONENT_07_BRIDGE) {
			break;
		}
		++meshIndex;
	}

	if (rootIndex < model->rootNodeCount) {
		return meshIndex;
	}
	return -1;
}

// FUNCTION: XVT 0x4AFA90
ModelMeshObjectTypeCache* ModelMesh_BuildObjectTypeMeshCache(void) {
	ModelMeshObjectTypeCache* cache;
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
			cache->meshTypes[meshIndex] = ModelMesh_GetObjectTypeMeshType(objectType, meshIndex);
			cache->meshDescriptors[meshIndex] = ModelMesh_GetDescriptor(objectType, meshIndex);
			++meshIndex;
		}

		++objectType;
	} while (objectType < 73);

	return &g_objectTypeMeshCache[73];
}
