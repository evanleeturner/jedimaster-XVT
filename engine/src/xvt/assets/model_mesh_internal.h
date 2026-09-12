#ifndef XVT_ASSETS_MODEL_MESH_INTERNAL_H
#define XVT_ASSETS_MODEL_MESH_INTERNAL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
	MODEL_MESH_Q15_SHIFT = 15,
	MODEL_MESH_Q15_ONE = 0x7FFF,
	MODEL_MESH_Q15_SCALE = 0x8000,
	MODEL_MESH_FIX_LIMIT = 0x40000000,
	MODEL_MESH_FIX_MIN_CLAMP = -MODEL_MESH_FIX_LIMIT + (MODEL_MESH_Q15_SCALE << 1)
};

typedef struct ModelMeshScaleOperation {
	int value;
	int scale;
} ModelMeshScaleOperation;

typedef struct ModelMeshRotationOperation {
	int value;
	int otherAxis;
	int cosineScale;
	int sineTerm;
} ModelMeshRotationOperation;

typedef struct ModelMeshAxis {
	int x;
	int y;
	int z;
} ModelMeshAxis;

static __inline int ModelMesh_ScaleQ15(ModelMeshScaleOperation operation) {
	operation.value = (int)(((int64_t)operation.value * operation.scale) >> MODEL_MESH_Q15_SHIFT);
	return operation.value;
}

static __inline int ModelMesh_ComputeRotationCoefficient(ModelMeshRotationOperation operation) {
	operation.sineTerm = (int32_t)((uint32_t)operation.sineTerm << MODEL_MESH_Q15_SHIFT);
	operation.value = (int32_t)((uint32_t)operation.value * (uint32_t)operation.otherAxis);
	operation.value >>= MODEL_MESH_Q15_SHIFT;
	operation.value *= operation.cosineScale;
	operation.value += operation.sineTerm;
	if (operation.value >= MODEL_MESH_FIX_LIMIT)
		operation.value = MODEL_MESH_FIX_LIMIT - 1;
	if (operation.value <= -MODEL_MESH_FIX_LIMIT)
		operation.value = MODEL_MESH_FIX_MIN_CLAMP;
	return operation.value >> MODEL_MESH_Q15_SHIFT;
}

static __inline int ModelMesh_ComputeNegativeCosineRotationCoefficient(ModelMeshRotationOperation operation) {
	int product;

	operation.sineTerm = (int32_t)((uint32_t)operation.sineTerm << MODEL_MESH_Q15_SHIFT);
	product = (int32_t)((uint32_t)operation.value * (uint32_t)operation.otherAxis);
	operation.value = product >> MODEL_MESH_Q15_SHIFT;
	operation.value *= operation.cosineScale;
	operation.value += product;
	operation.value += operation.sineTerm;
	if (operation.value >= MODEL_MESH_FIX_LIMIT)
		operation.value = MODEL_MESH_FIX_LIMIT - 1;
	if (operation.value <= -MODEL_MESH_FIX_LIMIT)
		operation.value = MODEL_MESH_FIX_MIN_CLAMP;
	return operation.value >> MODEL_MESH_Q15_SHIFT;
}

static __inline int ModelMesh_RotateAxisQ15(ModelMeshAxis axis, ModelMeshAxis row) {
	axis.x = (int32_t)((uint32_t)axis.x * (uint32_t)row.x + (uint32_t)axis.y * (uint32_t)row.y +
					   (uint32_t)axis.z * (uint32_t)row.z);
	row.x = axis.x;
	if (row.x >= MODEL_MESH_FIX_LIMIT)
		row.x = MODEL_MESH_FIX_LIMIT - 1;
	if (row.x <= -MODEL_MESH_FIX_LIMIT)
		row.x = MODEL_MESH_FIX_MIN_CLAMP;
	return row.x >> MODEL_MESH_Q15_SHIFT;
}

#ifdef __cplusplus
}
#endif

#endif
