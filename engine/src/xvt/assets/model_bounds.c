#include "xvt/assets/model_bounds.h"
#include "xvt/assets/model_mesh.h"
#include "xvt/assets/object_type.h"
#include "xvt/util/memory.h"

/* Per object type, the smallest corner of its model's box, which
 * ModelBounds_EnsureCached fills; only that function writes it. */
// GLOBAL: XVT 0x662CE8
struct OptVector g_modelBoundsMin[201] = {{0}};
/* Per object type, 1 once ModelBounds_EnsureCached has filled its bounds, so
 * the getters stop calling it. Only that function writes it and nothing sets it
 * back to 0, so a model loaded later for the same type keeps the first bounds.
 * The table has one entry more than there are types. */
// GLOBAL: XVT 0x663658
int g_modelBoundsCached[202] = {0};
/* Per object type, the largest corner of its model's box, which
 * ModelBounds_EnsureCached fills; only that function writes it. */
// GLOBAL: XVT 0x663980
struct OptVector g_modelBoundsMax[201] = {{0}};

/* Fills g_modelBoundsMin and g_modelBoundsMax for the object type and sets its
 * g_modelBoundsCached entry to 1. From the first OPT_MESHVERTS node of each
 * root other than an OPT_TEXTURE, when it has at least two vertices, it takes
 * the last two, which the converter makes the box's smallest and largest
 * corners: the smallest components of the second-to-last vertices and the
 * largest of the last ones are kept. A model with none gives 1073741800.0 for
 * every smallest and -1073741800.0 for every largest component. Does nothing
 * when the type's assetFlags lacks the 0x1 bit, and does not check that
 * g_loadedModels holds a handle. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x4ADD60
void ModelBounds_EnsureCached(int objectType)
{
	struct OptimizedPolyObject *model;
	int rootNodeIndex;
	struct OptNode *rootNode;
	struct OptNode *vertexNode;
	float *vertexData;
	float *bounds;
	int vertexCount;
	struct OptVector minBounds;
	struct OptVector maxBounds;

	minBounds.x = minBounds.y = minBounds.z = 1073741800.0f;
	maxBounds.x = maxBounds.y = maxBounds.z = -1073741800.0f;
	if ((g_objectTypeTable[objectType].assetFlags & 1) != 0) {
		model = (struct OptimizedPolyObject *)Memory_GetHandleBlock(
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
		Memory_HandleBlockDoneStub(g_loadedModels[objectType]);
	}
}

/* Returns the largest of the object type's three box sizes, cut to an int,
 * filling the cache first when needed (ModelBounds_EnsureCached). */
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

/* Returns the y of the object type's smallest box corner, cut to an int,
 * filling the cache first when needed. */
// FUNCTION: XVT 0x4ADFF0
int ModelBounds_GetMinY(int objectType)
{
	if (g_modelBoundsCached[objectType] == 0) {
		ModelBounds_EnsureCached(objectType);
	}
	return (int)g_modelBoundsMin[objectType].y;
}

/* Returns the z of the object type's smallest box corner, cut to an int,
 * filling the cache first when needed. */
// FUNCTION: XVT 0x4AE020
int ModelBounds_GetMinZ(int objectType)
{
	if (g_modelBoundsCached[objectType] == 0) {
		ModelBounds_EnsureCached(objectType);
	}
	return (int)g_modelBoundsMin[objectType].z;
}

/* Returns the y of the object type's largest box corner, cut to an int, filling
 * the cache first when needed. */
// FUNCTION: XVT 0x4AE080
int ModelBounds_GetMaxY(int objectType)
{
	if (g_modelBoundsCached[objectType] == 0) {
		ModelBounds_EnsureCached(objectType);
	}
	return (int)g_modelBoundsMax[objectType].y;
}

/* Returns the z of the object type's largest box corner, cut to an int, filling
 * the cache first when needed. */
// FUNCTION: XVT 0x4AE0B0
int ModelBounds_GetMaxZ(int objectType)
{
	if (g_modelBoundsCached[objectType] == 0) {
		ModelBounds_EnsureCached(objectType);
	}
	return (int)g_modelBoundsMax[objectType].z;
}

/* Returns the object type's box size along x, largest minus smallest corner,
 * cut to an int, filling the cache first when needed. */
// FUNCTION: XVT 0x4AE0E0
int ModelBounds_GetSizeX(int objectType)
{
	if (g_modelBoundsCached[objectType] == 0) {
		ModelBounds_EnsureCached(objectType);
	}
	return (int)(g_modelBoundsMax[objectType].x -
		     g_modelBoundsMin[objectType].x);
}

/* Returns the object type's box size along y, largest minus smallest corner,
 * cut to an int, filling the cache first when needed. */
// FUNCTION: XVT 0x4AE120
int ModelBounds_GetSizeY(int objectType)
{
	if (g_modelBoundsCached[objectType] == 0) {
		ModelBounds_EnsureCached(objectType);
	}

	return (int)(g_modelBoundsMax[objectType].y -
		     g_modelBoundsMin[objectType].y);
}

/* Returns the object type's box size along z, largest minus smallest corner,
 * cut to an int, filling the cache first when needed. */
// FUNCTION: XVT 0x4AE160
int ModelBounds_GetSizeZ(int objectType)
{
	if (g_modelBoundsCached[objectType] == 0) {
		ModelBounds_EnsureCached(objectType);
	}

	return (int)(g_modelBoundsMax[objectType].z -
		     g_modelBoundsMin[objectType].z);
}
