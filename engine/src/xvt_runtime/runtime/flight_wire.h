#ifndef XVT_RUNTIME_FLIGHT_WIRE_H
#define XVT_RUNTIME_FLIGHT_WIRE_H
#include "xvt_runtime/runtime/flight_protocol.h"
#ifdef __cplusplus
extern "C" {
#endif
int XvtFlightWire_ValidTick(uint32_t tick);
uint16_t XvtWire_Get16(const XvtWireU16 value);
uint32_t XvtWire_Get32(const XvtWireU32 value);
uint64_t XvtWire_Get64(const XvtWireU64 value);
void XvtWire_Set16(XvtWireU16 bytes, uint16_t value);
void XvtWire_Set32(XvtWireU32 bytes, uint32_t value);
void XvtWire_Set64(XvtWireU64 bytes, uint64_t value);
int XvtWire_IsZero(const void* bytes, size_t size);
uint32_t XvtFlightWire_Crc32c(const uint8_t* bytes, size_t size);
#ifdef __cplusplus
}
#endif
#endif
