#ifndef XVT_RENDER_FLIGHT_LIGHT_H
#define XVT_RENDER_FLIGHT_LIGHT_H

#include <stdint.h>

#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

struct object_point_light {
	int x; /* Offset along the lit object's side axis, Q15 dot product. */
	int y; /* Offset along its forward axis, negated. */
	int z; /* Offset along its up axis. */
	int intensity; /* Brightness, the explosion's value times 8. */
};

extern int g_object_point_light_count;
extern struct object_point_light g_object_point_lights[10];

void flight_light_reset_software_face_sample_cache(void);
float flight_light_compute_software_face_sample_intensity(
	struct scene_face *face, int screen_x, int screen_y,
	float reciprocal_depth);
void flight_light_setup_object_lighting(struct object_record *object);
void flight_light_setup_object_lighting_by_index(unsigned int object_index);

#ifdef __cplusplus
}
#endif

#endif
