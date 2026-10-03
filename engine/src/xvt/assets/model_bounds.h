#ifndef XVT_ASSETS_MODEL_BOUNDS_H
#define XVT_ASSETS_MODEL_BOUNDS_H

#include "xvt/assets/opt_model.h"
#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern struct opt_vector g_model_bounds_min[201];
extern int g_model_bounds_cached[202];
extern struct opt_vector g_model_bounds_max[201];

void model_bounds_ensure_cached(int object_type);
int model_bounds_get_max_extent(int object_type);
int model_bounds_get_min_y(int object_type);
int model_bounds_get_min_z(int object_type);
int model_bounds_get_max_y(int object_type);
int model_bounds_get_max_z(int object_type);
int model_bounds_get_size_x(int object_type);
int model_bounds_get_size_y(int object_type);
int model_bounds_get_size_z(int object_type);

#ifdef __cplusplus
}
#endif

#endif
