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

#ifdef __cplusplus
}
#endif

#endif
