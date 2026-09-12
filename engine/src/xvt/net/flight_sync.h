#ifndef XVT_NET_FLIGHT_SYNC_H
#define XVT_NET_FLIGHT_SYNC_H

#include "xvt/flight/flight_input.h"
#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern int g_inputFrameCount[8];
extern InputFrame g_inputHistory[8][450];
extern int g_flightNetDirtyAllObjectTransformsAfterRestore;
extern int g_remotePlayerRenderSmoothingEnabled;

void FlightSync_QueuePredictedRemoteInputFrames(int predictedFrameDelta);
void FlightSync_DiscardAllPredictedInputFrames(void);
void FlightSync_DiscardPredictedInputFrames(int playerIdx);
void FlightSync_RemoveInputHistoryFrame(int playerIdx, InputFrame* frame);
InputFrame* FlightSync_InsertInputFrame(int playerIdx, int timestamp, const FlightInputFrameRecord* input);
InputFrame* FlightSync_FindLastNonzeroInputFrame(int playerIdx);
void FlightSync_ResetRemotePlayerRenderSmoothing(void);
void FlightSync_CaptureRemotePlayerRenderSamples(void);
void FlightSync_ApplyRemotePlayerRenderSmoothing(void);
#ifndef XVT_MODERN
void FlightSync_ApplyWorldMessagePacket(uint8_t* packet);
#endif
void FlightSync_HandleWorldChecksumPacket(int senderDpid, const int* packet);
void FlightSync_HandleServerChecksumPacket(uint8_t* packet);
void FlightSync_CopyWorldStateResyncChunk(const void* src, int offset, unsigned int size);
void FlightSync_ReplayResyncMessages(unsigned int worldStateBytes, int serverTickTime);
void FlightSync_SnapshotWorldStateForReplay(void);
#ifndef XVT_MODERN
void FlightSync_BufferWorldMessagePacket(uint8_t* packet);
#endif
void FlightSync_ResetWorldMessageBufferCursor(void);
#ifndef XVT_MODERN
void FlightSync_ReplayBufferedWorldMessages(void);
#endif
int FlightSync_UnusedFourArgForwarder(int arg1, int arg2, int arg3, int arg4);

#ifdef __cplusplus
}
#endif

#endif
