#ifndef XVT_FLIGHT_OBJECT_STATIC_H
#define XVT_FLIGHT_OBJECT_STATIC_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint16_t static_laserstaticcollide(uint16_t sourceObjIdx, uint16_t staticObjIdx);
void static_laserhitstatic(uint16_t sourceObjIdx, int victimObjIdx);

#ifdef __cplusplus
}
#endif

#endif
