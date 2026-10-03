#ifndef XVT_ASSETS_MODEL_BOUNDS_H
#define XVT_ASSETS_MODEL_BOUNDS_H

#include "xvt/assets/opt_model.h"
#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern struct OptVector g_modelBoundsMin[201];
extern int g_modelBoundsCached[202];
extern struct OptVector g_modelBoundsMax[201];

void ModelBounds_EnsureCached(int objectType);
int ModelBounds_GetMaxExtent(int objectType);
int ModelBounds_GetMinY(int objectType);
int ModelBounds_GetMinZ(int objectType);
int ModelBounds_GetMaxY(int objectType);
int ModelBounds_GetMaxZ(int objectType);
int ModelBounds_GetSizeX(int objectType);
int ModelBounds_GetSizeY(int objectType);
int ModelBounds_GetSizeZ(int objectType);

#ifdef __cplusplus
}
#endif

#endif
