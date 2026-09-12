#ifndef XVT_RUNTIME_RESYNC_TASK_H
#define XVT_RUNTIME_RESYNC_TASK_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void XvtResync_RequestState(void);
int XvtResync_HasStateRequest(void);
int XvtResync_ReceiveFloor(void);
int XvtResync_ReceivePacket(int sender, const uint8_t* bytes, unsigned size);
int XvtResync_BeginSend(int player, uint8_t* world, int size);
void XvtResync_BeginApply(int player, int size);
int XvtResync_WaitAcks(int player, int count);
int XvtResync_IsActive(void);
int XvtResync_HoldsInput(void);
uint64_t XvtResync_NextWakeDelayUs(void);
void XvtResync_Tick(void);
void XvtResync_Reset(void);
void XvtResync_DeferChecksum(int sender, const int* packet);
/* Called after the received world and buffered messages have been applied. */
void XvtResync_WorldApplied(void);

#ifdef __cplusplus
}
#endif
#endif
