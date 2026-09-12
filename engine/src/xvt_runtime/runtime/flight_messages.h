#ifndef XVT_RUNTIME_FLIGHT_MESSAGES_H
#define XVT_RUNTIME_FLIGHT_MESSAGES_H
#include "xvt/flight/flight_input.h"
#include "xvt_runtime/runtime/flight_wire.h"
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef enum XvtFlightQueue { XVT_QUEUE_PENDING, XVT_QUEUE_REPLAY, XVT_QUEUE_COUNT } XvtFlightQueue;

typedef struct XvtFlightMessage {
	uint32_t target_flags;
	uint16_t count;
	uint8_t mask;
	XvtFlightWorldInputWire records[XVT_WORLD_RECORDS];
} XvtFlightMessage;

int XvtFlightMessages_Enqueue(const XvtFlightMessage* message, XvtFlightQueue queue);

int XvtFlightMessages_PrepareRecovery(unsigned tick);

void XvtFlightMessages_DiscardAssemblyThrough(unsigned tick);

unsigned XvtFlightMessages_PartCount(unsigned count);
unsigned XvtFlightMessages_Count(XvtFlightQueue queue);
int XvtFlightMessages_HasRoom(XvtFlightQueue queue, size_t size);
void XvtFlightWire_EncodeInput(XvtFlightInputWire* record, int tick, const FlightInputFrameRecord* input);
int XvtFlightWire_DecodeInput(const XvtFlightInputWire* record, int* tick, FlightInputFrameRecord* input);
size_t XvtFlightMessages_EncodeBatch(uint8_t* out, uint32_t cookie, const XvtFlightInputWire* records,
									 unsigned count);
int XvtFlightMessages_DecodeBatch(const uint8_t* bytes, size_t size, uint32_t cookie);
size_t XvtFlightMessages_EncodePart(uint8_t* out, const XvtFlightMessage* message, uint32_t cookie,
									unsigned part);
/* Returns 1 for a complete message, 0 for pending/old, -1 for invalid or a gap. */
int XvtFlightMessages_ReceivePart(const uint8_t* bytes, size_t size, uint32_t cookie, int confirmed,
								  XvtFlightMessage* out);

int XvtFlightMessages_Push(XvtFlightQueue queue, const void* bytes, size_t size);
size_t XvtFlightMessages_Peek(XvtFlightQueue queue, void* bytes, size_t capacity);
void XvtFlightMessages_Pop(XvtFlightQueue queue);
void XvtFlightMessages_Clear(XvtFlightQueue queue);
void XvtFlightMessages_Reset(void);
#ifdef __cplusplus
}
#endif
#endif
