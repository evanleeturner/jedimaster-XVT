#ifndef XVT_ASSETS_ENDIAN_H
#define XVT_ASSETS_ENDIAN_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint16_t Endian_Swap16(uint16_t value);
unsigned int Endian_Swap32(unsigned int value);

#ifdef __cplusplus
}
#endif

#endif
