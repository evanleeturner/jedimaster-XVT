#ifndef XVT_FRONTEND_FRONTEND_STATE_H
#define XVT_FRONTEND_FRONTEND_STATE_H

#include "aeron/compat/mmsystem.h"
#include "xvt/audio/cd_audio.h"
#include "xvt/audio/frontend_sound.h"
#include "xvt/frontend/front_image.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/frontend_screen.h"
#include "xvt/frontend/frontend_text.h"
#include "xvt/net/net.h"
#include "xvt/net/net_reliable.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FrontendGlobalState FrontendGlobalState;

struct FrontendGlobalState {
	FrontImageRleRowBuffer rleRowBuffer;
	FrontImageResourceRecord* resourceTable;
	int resourceCount;
	int mouseX;
	int mouseY;
	int cursorPrevDrawX;
	int cursorPrevDrawY;
	uint8_t cursorVisible;
	uint8_t cursorDefaultMask[100];
	uint8_t cursorDefaultSaveBuf[200];
	uint8_t* cursorMaskPixels;
	uint8_t* cursorSaveBuf;
	int cursorWidth;
	int cursorHeight;
	int cursorPrevDrawWidth;
	int cursorPrevDrawHeight;
	char cursorSpriteName[64];
	uint8_t mouseLeftDown;
	uint8_t mouseRightDown;
	uint8_t mouseClickLatch;
	uint8_t mouseRightClickLatch;
	int mouseInputGate;
	unsigned int joyDeviceIds[2];
	uint8_t joystickInitFlags[2];
	uint8_t joystickPresent[2];
	uint8_t joystickHasPov[2];
	uint8_t joystickButtonCount[2];
	uint8_t joystickButtonHeld[2][32];
	uint8_t joystickButtonReleased[2][32];
	uint8_t joystickPovDirection[2];
	int joystickAxisX[2];
	int joystickAxisY[2];
	uint32_t joystickXMin[2];
	uint32_t joystickXMax[2];
	uint32_t joystickYMin[2];
	uint32_t joystickYMax[2];
	int joystickXCenter[2];
	int joystickYCenter[2];
	int joystickXNegativeScale[2];
	int joystickXPositiveScale[2];
	int joystickYNegativeScale[2];
	int joystickYPositiveScale[2];
	uint8_t keyState[256];
	uint8_t keyDownState[256];
	char charRingBuffer[1024];
	int charWriteIdx;
	int charReadIdx;
	uint8_t escapeCloseEnabled;
	DDSURFACEDESC backBufferDesc;
	uint8_t backBufferLocked;
	uint8_t presentFrameReady;
	uint32_t surfaceClearColor;
	int drawSurfacePitch;
	int backBufferPitch;
	int offscreenSurfacePitch;
	int displayBpp;
	uint8_t pixelFormat555;
	uint8_t offscreenRestoreEnabled;
	uint8_t secondaryDirectDrawActive;
	void* hWnd;
	int frontendDisplayWndProcMode;
	IDirectDraw* directDraw;
	IDirectDrawSurface* offscreenSurface;
	IDirectDrawSurface* primarySurface;
	IDirectDrawSurface* backBufferSurface;
	int appActive;
	uint8_t unknownDisplayState_E42[0x40];
	int32_t clipMinX;
	int32_t clipMaxX;
	int32_t clipMinY;
	int32_t clipMaxY;
	void* offscreenBackupBuffer;
	int restoreOffscreenOverlayAfterActivate;
	IDirectDrawPalette* ddPalette;
	FrontendPaletteEntry displayPalette[256];
	uint8_t paletteNeedsSet;
	int textColorCodes[6];
	IDirectSound* frontendDirectSound;
	IDirectSoundBuffer* frontendPrimarySoundBuffer;
	FrontendSoundBufferRecord* frontendSoundBuffers;
	FrontendSoundVoice* frontendSoundVoices;
	int frontendSoundBufferCount;
	int frontendActiveVoiceCount;
	int frontendSoundPlaySerial;
	MCIDEVICEID cdAudioMciDeviceId;
	int cdAudioCurrentTrack;
	int cdAudioTrackCount;
	int cdAudioPlaybackComplete;
	uint32_t cdAudioTrackEndTick;
	int cdAudioLoopCurrentTrack;
	int cdAudioSuspendRemainingMs;
	int cdAudioSuspendElapsedMs;
	uint32_t cdAudioResumeDueTick;
	CDAudioSuspendState cdAudioSuspendState;
	int cdAudioSavedAuxVolume;
	CDAudioTrackCache cdAudioTrackCache;
	BitmapFont fontSlots[10];
	BitmapFont* fontBySize[256];
	int glyphScratchTtl;
	int glyphScratchReload;
	GlyphScratchBuffer glyphScratchBuffer;
	int (*modeInitFn)(void);
	FrontendScreenUpdateFn pendingScreenUpdateFn;
	RECT pendingScreenRect;
	int screenStackTop;
	int screenCallbacksDirty;
	FrontendScreenState screenStates[10];
	int frameIntervalMs;
	int frameCounter;
	IDirectPlay* netTempDirectPlay;
	IDirectPlay2A* netDirectPlay;
	IDirectPlayLobbyA* netDirectPlayLobby;
	uint8_t unknownNetState_27ECD[4];
	GUID netAppGuid;
	GUID netJoinedSessionGuid;
	DPID netHostPlayerId;
	DPID netGroupDplayId;
	int netIsHost;
	int netPlayerCount;
	int netReadyPlayerLeftThisFrame;
	char netSessionName[32];
	NetPlayerInfo netPlayers[32];
	NetPlayerInfo netRuntimeLocalPlayer;
	int netRuntimeBroadcastSeqCounter;
	NetPendingPayload netRuntimeBroadcastPendingPayload;
	int netRuntimeGroupSeqCounter;
	NetPendingPayload netRuntimeGroupPendingPayload;
	uint8_t unknownNetState_28865[4];
	int frontendPostResetMarker;
	int netReliableRetryLongTimeoutMode;
	NetQueuedPacket netRuntimeRecvQueue[1024];
	int netRuntimeRecvQueueWriteIndex;
	int netRuntimeRecvQueueReadIndex;
	int netRuntimeRecvQueueCount;
	NetQueuedPacket netRuntimeRecvHistory[128];
	int netRuntimeRecvHistoryCount;
	NetQueuedPacket netRuntimeRecvScratchPacket;
	NetReliablePeerSlot netRuntimeReliablePeerSlots[40];
	uint8_t unknownNetState_C2B51[0x238];
	uint32_t netSequenceCount;
	NetQueuedPacket* netExportRecvQueuePtr;
	int netExportRecvQueueHighWater;
	unsigned int* uiStringOffsets;
	char* uiStringData;
	unsigned int uiStringCount;
	unsigned int uiStringCapacity;
	char installDriveLetter;
	char cdDriveLetter;
	char installPath[256];
	char baseGameInstallPath[256];
};

typedef char
	xvt_size_FrontendGlobalState[(sizeof(void*) != 4 || sizeof(FrontendGlobalState) == 0xC2FA7) ? 1 : -1];

extern FrontendGlobalState g_frontState;

#ifdef __cplusplus
}
#endif

#endif
