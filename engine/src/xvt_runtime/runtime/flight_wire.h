#ifndef XVT_RUNTIME_FLIGHT_WIRE_H
#define XVT_RUNTIME_FLIGHT_WIRE_H
#include "xvt_runtime/runtime/flight_protocol.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Byte-level helpers for the network flight protocol, whose integers are little-endian uint8_t
 * arrays. */

/* 1 when tick is a usable network tick: nonzero, at most INT32_MAX - 1, and a multiple of
 * XVT_NETWORK_STEP_TICKS. */
int xvt_flight_wire_valid_tick(uint32_t tick);
/* Get reads and Set writes a little-endian integer of the named width. */
uint16_t xvt_wire_get16(const xvt_wire_u16 value);
uint32_t xvt_wire_get32(const xvt_wire_u32 value);
uint64_t xvt_wire_get64(const xvt_wire_u64 value);
void xvt_wire_set16(xvt_wire_u16 bytes, uint16_t value);
void xvt_wire_set32(xvt_wire_u32 bytes, uint32_t value);
void xvt_wire_set64(xvt_wire_u64 bytes, uint64_t value);
/* 1 when every one of size bytes is zero, including when size is 0. */
int xvt_wire_is_zero(const void *bytes, size_t size);
/* CRC-32C (Castagnoli, reflected, initial and final XOR 0xFFFFFFFF) of size bytes; "123456789"
 * gives 0xE3069283. */
uint32_t xvt_flight_wire_crc32c(const uint8_t *bytes, size_t size);
#ifdef __cplusplus
}
#endif
#endif
