#ifndef XVT_ASSETS_MODEL_BOUNDS_H
#define XVT_ASSETS_MODEL_BOUNDS_H

#include "xvt/assets/opt_model.h"
#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern OptVector g_modelBoundsMin[201];
extern int g_modelBoundsCached[202];
extern OptVector g_modelBoundsMax[201];

void ModelBounds_EnsureCached(int modelType);
int ModelBounds_GetMaxExtent(int modelType);
int ModelBounds_GetMinY(int modelType);
int ModelBounds_GetMinZ(int modelType);
int ModelBounds_GetMaxY(int modelType);
int ModelBounds_GetMaxZ(int modelType);
int ModelBounds_GetSizeX(int modelType);
int ModelBounds_GetSizeY(int modelType);
int ModelBounds_GetSizeZ(int modelType);

#ifdef __cplusplus
}
#endif

#endif
