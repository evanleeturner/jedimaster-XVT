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
	MODEL_MESH_FIX_MIN_CLAMP =
		-MODEL_MESH_FIX_LIMIT + (MODEL_MESH_Q15_SCALE << 1)
};

struct model_mesh_scale_operation {
	/* The number to scale; laser_createcountermeasureprojectile puts a move
	 * vector component here. */
	int value;
	/* The other factor: model_mesh_scale_q15, which nothing calls, returns
	 * value times scale shifted right 15 bits.
	 * laser_createcountermeasureprojectile puts a distance here and passes
	 * both to math_mul_q15. */
	int scale;
};

struct model_mesh_rotation_operation {
	/* Read only by model_mesh_compute_rotation_coefficient and
	 * model_mesh_compute_negative_cosine_rotation_coefficient, which nothing
	 * calls. */
	int value;
	int other_axis;	  /* Read only by those two functions. */
	int cosine_scale; /* Read only by those two functions. */
	int sine_term;	  /* Read only by those two functions. */
};

struct model_mesh_axis {
	int x; /* Read only by model_mesh_rotate_axis_q15, which nothing calls. */
	int y; /* Read only by model_mesh_rotate_axis_q15. */
	int z; /* Read only by model_mesh_rotate_axis_q15. */
};

static __inline int
model_mesh_scale_q15(struct model_mesh_scale_operation operation)
{
	operation.value = (int)(((int64_t)operation.value * operation.scale) >>
				MODEL_MESH_Q15_SHIFT);
	return operation.value;
}

static __inline int model_mesh_compute_rotation_coefficient(
	struct model_mesh_rotation_operation operation)
{
	operation.sine_term = (int32_t)((uint32_t)operation.sine_term
					<< MODEL_MESH_Q15_SHIFT);
	operation.value = (int32_t)((uint32_t)operation.value *
				    (uint32_t)operation.other_axis);
	operation.value >>= MODEL_MESH_Q15_SHIFT;
	operation.value *= operation.cosine_scale;
	operation.value += operation.sine_term;
	if (operation.value >= MODEL_MESH_FIX_LIMIT) {
		operation.value = MODEL_MESH_FIX_LIMIT - 1;
	}
	if (operation.value <= -MODEL_MESH_FIX_LIMIT) {
		operation.value = MODEL_MESH_FIX_MIN_CLAMP;
	}
	return operation.value >> MODEL_MESH_Q15_SHIFT;
}

static __inline int model_mesh_compute_negative_cosine_rotation_coefficient(
	struct model_mesh_rotation_operation operation)
{
	int product;

	operation.sine_term = (int32_t)((uint32_t)operation.sine_term
					<< MODEL_MESH_Q15_SHIFT);
	product = (int32_t)((uint32_t)operation.value *
			    (uint32_t)operation.other_axis);
	operation.value = product >> MODEL_MESH_Q15_SHIFT;
	operation.value *= operation.cosine_scale;
	operation.value += product;
	operation.value += operation.sine_term;
	if (operation.value >= MODEL_MESH_FIX_LIMIT) {
		operation.value = MODEL_MESH_FIX_LIMIT - 1;
	}
	if (operation.value <= -MODEL_MESH_FIX_LIMIT) {
		operation.value = MODEL_MESH_FIX_MIN_CLAMP;
	}
	return operation.value >> MODEL_MESH_Q15_SHIFT;
}

static __inline int model_mesh_rotate_axis_q15(struct model_mesh_axis axis,
					       struct model_mesh_axis row)
{
	axis.x = (int32_t)((uint32_t)axis.x * (uint32_t)row.x +
			   (uint32_t)axis.y * (uint32_t)row.y +
			   (uint32_t)axis.z * (uint32_t)row.z);
	row.x = axis.x;
	if (row.x >= MODEL_MESH_FIX_LIMIT) {
		row.x = MODEL_MESH_FIX_LIMIT - 1;
	}
	if (row.x <= -MODEL_MESH_FIX_LIMIT) {
		row.x = MODEL_MESH_FIX_MIN_CLAMP;
	}
	return row.x >> MODEL_MESH_Q15_SHIFT;
}

#ifdef __cplusplus
}
#endif

#endif
