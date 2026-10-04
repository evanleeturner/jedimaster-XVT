#ifndef XVT_FLIGHT_OBJECT_STATIC_H
#define XVT_FLIGHT_OBJECT_STATIC_H

#include <stdint.h>

#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

uint16_t static_test_swept_static_collision(uint16_t source_obj_idx,
					    uint16_t static_obj_idx);
void static_apply_static_hit(uint16_t source_obj_idx, int victim_obj_idx);

#ifdef __cplusplus
}
#endif

#endif
