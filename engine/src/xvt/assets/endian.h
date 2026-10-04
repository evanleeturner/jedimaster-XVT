#ifndef XVT_ASSETS_ENDIAN_H
#define XVT_ASSETS_ENDIAN_H

#include <stdint.h>

#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

uint16_t endian_swap16(uint16_t value);
unsigned int endian_swap32(unsigned int value);

#ifdef __cplusplus
}
#endif

#endif
