#ifndef XVT_FLIGHT_FLIGHT_GEOMETRY_H
#define XVT_FLIGHT_FLIGHT_GEOMETRY_H

#include <stdint.h>

#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

extern int g_last_rough_distance;
extern int g_rotated_x;
extern int g_rotated_y;
extern int g_rotated_z;

void flight_geometry_direction_between_refs(unsigned int from_ref,
					    unsigned int to_ref);
void flight_geometry_rough_distance_between_refs(unsigned int from_ref,
						 unsigned int to_ref);
void flight_geometry_local_to_world_i16(const struct object_record *obj,
					int16_t side_arg, int16_t up_arg,
					int16_t fwd_arg);
void flight_geometry_local_to_world(const struct object_record *obj_record,
				    int local_side, int local_up,
				    int local_fwd);

#ifdef __cplusplus
}
#endif

#endif
