#include "xvt/assets/model_bounds.h"
#include "xvt/assets/model_mesh.h"
#include "xvt/assets/object_type.h"
#include "xvt/util/memory.h"

// GLOBAL: XVT 0x662CE8
OptVector g_modelBoundsMin[201] = {{0}};
// GLOBAL: XVT 0x663658
int g_modelBoundsCached[202] = {0};
// GLOBAL: XVT 0x663980
OptVector g_modelBoundsMax[201] = {{0}};

// FLAGS: /O2 /G5
// FUNCTION: XVT 0x4ADD60
void ModelBounds_EnsureCached(int objectType)
{
	OptimizedPolyObject *model;
	int rootNodeIndex;
	OptNode *rootNode;
	OptNode *vertexNode;
	float *vertexData;
	float *bounds;
	int vertexCount;
	OptVector minBounds;
	OptVector maxBounds;

	minBounds.x = minBounds.y = minBounds.z = 1073741800.0f;
	maxBounds.x = maxBounds.y = maxBounds.z = -1073741800.0f;
	if ((g_objectTypeTable[objectType].assetFlags & 1) != 0) {
		model = (OptimizedPolyObject *)Memory_LockHandle(
			g_loadedModels[objectType]);
		if (model->selfMarker != model) {
			OptModel_AdjustOptimizedPolyObjectPointers(model);
		}

		for (rootNodeIndex = 0; rootNodeIndex < model->rootNodeCount;
		     ++rootNodeIndex) {
			rootNode = model->rootNodes[rootNodeIndex];
			if (rootNode != 0 &&
			    rootNode->nodeType != OPT_TEXTURE) {
				vertexNode = ModelMesh_FindFirstMeshVertsNode(
					rootNode);
				if (vertexNode != 0) {
					vertexData =
						(float *)vertexNode->payload;
					vertexCount = vertexNode->payloadCount;
					if (vertexCount >= 2) {
						bounds = vertexData +
							 3 * vertexCount - 6;
						if (bounds[0] < minBounds.x) {
							minBounds.x = bounds[0];
						}
						if (bounds[1] < minBounds.y) {
							minBounds.y = bounds[1];
						}
						if (bounds[2] < minBounds.z) {
							minBounds.z = bounds[2];
						}
						bounds += 3;
						if (bounds[0] > maxBounds.x) {
							maxBounds.x = bounds[0];
						}
						if (bounds[1] > maxBounds.y) {
							maxBounds.y = bounds[1];
						}
						if (bounds[2] > maxBounds.z) {
							maxBounds.z = bounds[2];
						}
					}
				}
			}
		}

		g_modelBoundsMin[objectType].x = minBounds.x;
		g_modelBoundsMin[objectType].y = minBounds.y;
		g_modelBoundsMin[objectType].z = minBounds.z;
		g_modelBoundsMax[objectType].x = maxBounds.x;
		g_modelBoundsMax[objectType].y = maxBounds.y;
		g_modelBoundsCached[objectType] = 1;
		g_modelBoundsMax[objectType].z = maxBounds.z;
		Memory_UnlockHandle(g_loadedModels[objectType]);
	}
}

// FUNCTION: XVT 0x4ADF10
int ModelBounds_GetMaxExtent(int objectType)
{
	float size[3];

	if (g_modelBoundsCached[objectType] == 0) {
		ModelBounds_EnsureCached(objectType);
	}

	size[0] =
		g_modelBoundsMax[objectType].x - g_modelBoundsMin[objectType].x;
	size[1] =
		g_modelBoundsMax[objectType].y - g_modelBoundsMin[objectType].y;
	size[2] =
		g_modelBoundsMax[objectType].z - g_modelBoundsMin[objectType].z;

	if (size[1] >= size[0] && size[1] >= size[2]) {
		size[0] = size[1];
	} else if (size[2] >= size[0] && size[1] <= size[2]) {
		size[0] = size[2];
	}
	return (int)size[0];
}

// FUNCTION: XVT 0x4ADFF0
int ModelBounds_GetMinY(int objectType)
{
	if (g_modelBoundsCached[objectType] == 0) {
		ModelBounds_EnsureCached(objectType);
	}
	return (int)g_modelBoundsMin[objectType].y;
}

// FUNCTION: XVT 0x4AE020
int ModelBounds_GetMinZ(int objectType)
{
	if (g_modelBoundsCached[objectType] == 0) {
		ModelBounds_EnsureCached(objectType);
	}
	return (int)g_modelBoundsMin[objectType].z;
}

// FUNCTION: XVT 0x4AE080
int ModelBounds_GetMaxY(int objectType)
{
	if (g_modelBoundsCached[objectType] == 0) {
		ModelBounds_EnsureCached(objectType);
	}
	return (int)g_modelBoundsMax[objectType].y;
}

// FUNCTION: XVT 0x4AE0B0
int ModelBounds_GetMaxZ(int objectType)
{
	if (g_modelBoundsCached[objectType] == 0) {
		ModelBounds_EnsureCached(objectType);
	}
	return (int)g_modelBoundsMax[objectType].z;
}

// FUNCTION: XVT 0x4AE0E0
int ModelBounds_GetSizeX(int objectType)
{
	if (g_modelBoundsCached[objectType] == 0) {
		ModelBounds_EnsureCached(objectType);
	}
	return (int)(g_modelBoundsMax[objectType].x -
		     g_modelBoundsMin[objectType].x);
}

// FUNCTION: XVT 0x4AE120
int ModelBounds_GetSizeY(int objectType)
{
	if (g_modelBoundsCached[objectType] == 0) {
		ModelBounds_EnsureCached(objectType);
	}

	return (int)(g_modelBoundsMax[objectType].y -
		     g_modelBoundsMin[objectType].y);
}

// FUNCTION: XVT 0x4AE160
int ModelBounds_GetSizeZ(int objectType)
{
	if (g_modelBoundsCached[objectType] == 0) {
		ModelBounds_EnsureCached(objectType);
	}

	return (int)(g_modelBoundsMax[objectType].z -
		     g_modelBoundsMin[objectType].z);
}
