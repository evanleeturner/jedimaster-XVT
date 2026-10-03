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

/* The frontend's whole state in one block: display, input, sound, CD music,
 * fonts, screens, the lobby's DirectPlay session, the string table and the
 * install paths; g_frontState is the one instance. */
struct FrontendGlobalState {
	/* Scratch row FrontImage_CompressRLE and the original build's glyph
	 * encoding fill with one encoded image row. */
	FrontImageRleRowBuffer rleRowBuffer;
	/* Heap table of the loaded images, room for 512, kept sorted by name
	 * for FrontImage_FindResourceByName. */
	FrontImageResourceRecord *resourceTable;
	int resourceCount; /* Images in resourceTable. */
	/* Cursor x in the 640 by 480 screen, from the window's mouse moves,
	 * FrontendCursor_SetPos or the modern input bridge. */
	int mouseX;
	int mouseY;	     /* Cursor y, kept like mouseX. */
	int cursorPrevDrawX; /* x where FrontendCursor_Draw last drew it. */
	int cursorPrevDrawY; /* y where FrontendCursor_Draw last drew it. */
	/* 1 while the frame loop draws the cursor after each frame. */
	uint8_t cursorVisible;
	/* The default 10 by 10 cursor, copied from g_defaultCursorBitmap by
	 * FrontendCursor_Init: 0 is clear; at 16 bits 1 draws color 31 and
	 * 0xFF white. */
	uint8_t cursorDefaultMask[100];
	/* Screen under the default cursor, 100 pixels of up to 2 bytes. */
	uint8_t cursorDefaultSaveBuf[200];
	/* Pixels of the cursor: cursorDefaultMask, or the image
	 * FrontendCursor_SetImageFromResourceName picks. */
	uint8_t *cursorMaskPixels;
	/* Where FrontendCursor_Draw keeps the screen under the cursor and
	 * FrontendCursor_Restore puts it back from. */
	uint8_t *cursorSaveBuf;
	int cursorWidth;	  /* Cursor width in pixels, 10 by default. */
	int cursorHeight;	  /* Cursor height in pixels, 10 by default. */
	int cursorPrevDrawWidth;  /* Part of the cursor last drawn, clipped. */
	int cursorPrevDrawHeight; /* Part of the cursor last drawn, clipped. */
	/* Image name the cursor is drawn from; empty for the default cursor,
	 * drawn from cursorMaskPixels. */
	char cursorSpriteName[64];
	uint8_t mouseLeftDown;	/* 1 while the left button is held. */
	uint8_t mouseRightDown; /* 1 while the right button is held. */
	/* 1 once the left button is released, until the end of the frame. */
	uint8_t mouseLeftClickLatch;
	/* 1 once the right button is released, until the end of the frame. */
	uint8_t mouseRightClickLatch;
	/* Id of the control that has the mouse, or 0; while it is set the
	 * plain FrontendMouse getters report no buttons. */
	int mouseInputGate;
	/* Windows joystick id of each of the two joystick slots. */
	unsigned int joyDeviceIds[2];
	/* Set to 1 by Joystick_InitDevices; nothing reads it. */
	uint8_t joystickInitFlags[2];
	/* 1 for a slot with a working joystick. */
	uint8_t joystickPresent[2];
	uint8_t joystickHasPov[2];	/* 1 when the joystick has a hat. */
	uint8_t joystickButtonCount[2]; /* Buttons the joystick reports. */
	/* Per slot and button, 1 while held at the last update. */
	uint8_t joystickButtonHeld[2][32];
	/* Per slot and button, 1 when released at the last update; cleared
	 * after every frame. */
	uint8_t joystickButtonReleased[2][32];
	/* Hat direction: 0 centered, else the hat's angle in hundredths of a
	 * degree divided by 0x2328, plus 1. */
	uint8_t joystickPovDirection[2];
	/* Stick x: 0 while no more than 1000 from center, else the distance
	 * from center divided by joystickXNegativeScale or
	 * joystickXPositiveScale. */
	int joystickAxisX[2];
	/* Stick y, worked out like joystickAxisX. */
	int joystickAxisY[2];
	uint32_t joystickXMin[2]; /* Lowest x the driver reports. */
	uint32_t joystickXMax[2]; /* Highest x the driver reports. */
	uint32_t joystickYMin[2]; /* Lowest y the driver reports. */
	uint32_t joystickYMax[2]; /* Highest y the driver reports. */
	/* x read when Joystick_InitDevices ran, taken as the center. */
	int joystickXCenter[2];
	/* y read when Joystick_InitDevices ran, taken as the center. */
	int joystickYCenter[2];
	/* (center x - lowest x) / 255; at least 1 in the modern build. */
	int joystickXNegativeScale[2];
	/* (highest x - center x) / 255; at least 1 in the modern build. */
	int joystickXPositiveScale[2];
	/* (center y - lowest y) / 255; at least 1 in the modern build. */
	int joystickYNegativeScale[2];
	/* (highest y - center y) / 255; at least 1 in the modern build. */
	int joystickYPositiveScale[2];
	/* Windows key state by virtual key, the 0x80 bit set while down;
	 * filled each frame by GetKeyboardState, or the modern input bridge. */
	uint8_t keyState[256];
	/* Cleared on every key release; nothing reads it. */
	uint8_t keyDownState[256];
	/* Ring of typed characters, written by the window procedure or the
	 * modern input bridge and read by Keyboard_DequeueChar. */
	char charRingBuffer[1024];
	int charWriteIdx; /* Next free entry of charRingBuffer. */
	/* Oldest unread entry; equal to charWriteIdx when empty. */
	int charReadIdx;
	/* 1 while Esc quits the game: set when the display starts, cleared by
	 * FrontendDisplay_DisableEscapeClose. */
	uint8_t escapeCloseEnabled;
	/* Surface record of the locked back buffer; its lpSurface is the
	 * drawing pointer. */
	DDSURFACEDESC backBufferDesc;
	/* 1 while the back buffer is locked; many callers save it and lock
	 * again after work that unlocks. */
	uint8_t backBufferLocked;
	/* 1 to clear the back buffer after each present. */
	uint8_t clearBackBufferAfterPresent;
	uint32_t surfaceClearColor; /* Fill color for clearing surfaces. */
	/* Bytes per row of the surface g_drawSurfacePtr points into. */
	int drawSurfacePitch;
	int backBufferPitch;	   /* Bytes per row of the back buffer. */
	int offscreenSurfacePitch; /* Bytes per row of the offscreen surface. */
	int displayBpp;		   /* Bits per pixel, 8 or 16. */
	/* 1 when the 16-bit display packs 5-5-5 (the green mask lacks the
	 * 0x400 bit), 0 for 5-6-5. */
	uint8_t pixelFormat555;
	/* 1 to refill the back buffer from the offscreen surface after each
	 * present; the offscreen surface holds the screen's fixed
	 * background. */
	uint8_t offscreenRestoreEnabled;
	/* 1 when DirectDraw runs on the driver FrontendDisplay_LoadDriverGuid
	 * named, 0 on the default one. */
	uint8_t secondaryDirectDrawActive;
	void *hWnd; /* The game window. */
	/* Which window procedure FrontendDisplay_WndProc forwards to: 0
	 * frontend, 1 flight, 2 movie. */
	int frontendDisplayWndProcMode;
	IDirectDraw *directDraw; /* The DirectDraw object. */
	/* Surface the fixed background is drawn into. */
	IDirectDrawSurface *offscreenSurface;
	IDirectDrawSurface *primarySurface; /* The visible surface. */
	/* Surface the frame is drawn into before it is shown. */
	IDirectDrawSurface *backBufferSurface;
	/* Nonzero while the game is the active application; the original
	 * frame loop runs frames only then. */
	int appActive;
	/* Never read or written by name. */
	uint8_t unknownDisplayState_E42[0x40];
	int32_t clipMinX; /* Left edge of the screen clip, inclusive. */
	int32_t clipMaxX; /* Right edge of the screen clip, inclusive. */
	int32_t clipMinY; /* Top edge of the screen clip, inclusive. */
	int32_t clipMaxY; /* Bottom edge of the screen clip, inclusive. */
	/* Copy of the offscreen surface, 480 rows, saved on request when it is
	 * unlocked and put back after the game is activated again. */
	void *offscreenBackupBuffer;
	/* 1 after the game is activated, until the next present puts
	 * offscreenBackupBuffer back. */
	int restoreOffscreenOverlayAfterActivate;
	IDirectDrawPalette *ddPalette; /* Palette of the 8-bit display. */
	FrontendPaletteEntry displayPalette[256]; /* The display's colors. */
	/* Checked and cleared by the present at 8 bits; nothing sets it to
	 * 1. */
	uint8_t paletteNeedsSet;
	/* Colors that text codes 2 to 6 switch to, entries 1 to 5; entry 0 is
	 * set to 0xFFFF. */
	int textColorCodes[6];
	IDirectSound *frontendDirectSound; /* The frontend's DirectSound. */
	/* DirectSound's primary buffer. */
	IDirectSoundBuffer *frontendPrimarySoundBuffer;
	/* Heap table of the loaded sounds, up to 128, kept sorted by name. */
	FrontendSoundBufferRecord *frontendSoundBuffers;
	/* Heap table of the 12 voices that play sounds. */
	FrontendSoundVoice *frontendSoundVoices;
	int frontendSoundBufferCount; /* Sounds in frontendSoundBuffers. */
	int frontendActiveVoiceCount; /* Voices playing, 0 to 12. */
	/* Count of sounds started; each voice keeps its start number, and
	 * the oldest gives way when all 12 are busy. */
	int frontendSoundPlaySerial;
	/* MCI id of the open CD audio device; 0 when none is open. */
	MCIDEVICEID cdAudioMciDeviceId;
	int cdAudioCurrentTrack; /* Track playing, 0 for none. */
	int cdAudioTrackCount;	 /* Tracks on the CD. */
	/* 1 once a track that does not loop has run past its end time. */
	int cdAudioPlaybackComplete;
	/* GetTickCount at which the track playing ends: its length plus 2000
	 * ms after it started. */
	uint32_t cdAudioTrackEndMs;
	/* 1 to start the track again when it ends. */
	int cdAudioLoopCurrentTrack;
	/* Milliseconds of the track left when it was suspended. */
	int cdAudioSuspendRemainingMs;
	/* Milliseconds of the track played when it was suspended; playback
	 * resumes from there. */
	int cdAudioSuspendElapsedMs;
	/* GetTickCount after which a pending resume plays again: 1000 ms
	 * after CDAudio_RequestResumePlayback. */
	uint32_t cdAudioResumeDueMs;
	/* Not suspended, suspended, or resume pending. */
	CDAudioSuspendState cdAudioSuspendState;
	/* CD aux volume that CDAudio_Initialize found, low 16 bits. */
	int cdAudioSavedAuxVolume;
	/* Volume set last and each track's length. */
	CDAudioTrackCache cdAudioTrackCache;
	BitmapFont fontSlots[10]; /* The loaded fonts. */
	/* Font of each point size, NULL when not loaded. */
	BitmapFont *fontBySize[256];
	/* Frames left in the text fade-in; text is drawn faded while it is
	 * nonzero. */
	int textFadeFramesLeft;
	int textFadeFrameCount; /* Length of the text fade-in, in frames. */
	/* Faded value of each 16-bit color for the current fade frame, 0 when
	 * not yet worked out; cleared every frame of a fade. */
	TextFadeColorCache textFadeColorCache;
	/* Function the original frame loop runs once before the first frame;
	 * a nonzero result shuts the display down. */
	int (*modeInitFn)(void);
	/* Screen FrontendScreen_QueuePush asked for, pushed after the frame;
	 * NULL when none waits. */
	FrontendScreenUpdateFn pendingScreenUpdateFn;
	RECT pendingScreenRect; /* Area of the screen waiting to be pushed. */
	int screenStackTop; /* Index in screenStates of the screen running. */
	/* 1 after FrontendScreen_SetCallbacks, until the frame loop has run
	 * the old screen's exit function. */
	int screenCallbacksDirty;
	/* The stack of screens: callbacks, and what a pushed screen saved of
	 * the one under it. */
	FrontendScreenState screenStates[10];
	/* Milliseconds per frontend frame: 1000 divided by the frame rate. */
	int frameIntervalMs;
	/* Frames the running screen has had, from 0; set to -1 when its
	 * callbacks change so the new screen starts at 0. */
	int frameCounter;
	/* DirectPlay object a session start creates and releases once it has
	 * the IDirectPlay2A interface. */
	IDirectPlay *netTempDirectPlay;
	/* The lobby session's IDirectPlay2A interface; NULL without one. */
	IDirectPlay2A *netDirectPlay;
	/* DirectPlay lobby object, held while a lobby connection opens. */
	IDirectPlayLobbyA *netDirectPlayLobby;
	uint8_t unknownNetState_27ECD[4]; /* Never read or written by name. */
	GUID netAppGuid;	   /* The game's DirectPlay application GUID. */
	GUID netJoinedSessionGuid; /* GUID of the session joined. */
	DPID netHostPlayerId; /* The host's DirectPlay id; 0 until known. */
	/* DirectPlay group of the session's players. */
	DPID netGroupDplayId;
	int netIsHost;	    /* Nonzero on the session's host. */
	int netPlayerCount; /* Entries in netPlayers. */
	/* Set when a ready player leaves (seen by the host); cleared every
	 * frontend frame. */
	int netReadyPlayerLeftThisFrame;
	char netSessionName[32]; /* Name of the session. */
	/* The lobby roster: entry 0 the local player, then the others. */
	NetPlayerInfo netPlayers[32];
	NetPlayerInfo netRuntimeLocalPlayer; /* The local player's entry. */
	/* Next sequence, 0 to 127, for packets to all players. */
	int netRuntimeBroadcastSeqCounter;
	/* Last packet to all players, sent again behind the next one. */
	NetPiggybackPayload netRuntimeBroadcastPendingPayload;
	/* Next sequence, 0 to 127, for the group channel. */
	int netRuntimeGroupSeqCounter;
	/* Last group-channel packet, sent again behind the next one. */
	NetPiggybackPayload netRuntimeGroupPendingPayload;
	uint8_t unknownNetState_28865[4]; /* Never read or written by name. */
	/* Set to 1 by FrontendDisplay_ResetGlobalStatePreservingNetworkSession,
	 * whose one caller nothing calls; nothing reads it. */
	int frontendPostResetMarker;
	/* Only ever set to 0. While nonzero, a gap in the lobby's packets
	 * would wait 20 seconds and ask no more. */
	int netReliableRetryLongTimeoutMode;
	/* The lobby's receive queue, a ring of 1024 packets. */
	NetQueuedPacket netRuntimeRecvQueue[1024];
	int netRuntimeRecvQueueWriteIndex; /* Next free entry of the queue. */
	int netRuntimeRecvQueueReadIndex;  /* Oldest entry of the queue. */
	int netRuntimeRecvQueueCount;	   /* Entries in the queue. */
	/* The last 128 packets sent, kept for resends. */
	NetQueuedPacket netRuntimeSentHistory[128];
	/* Entry netRuntimeSentHistory writes next. */
	int netRuntimeSentHistoryWriteIndex;
	/* Copy of the packet handed out last; callers get a pointer into
	 * it. */
	NetQueuedPacket netRuntimeRecvScratchPacket;
	/* Per-peer delivery state of the lobby session. */
	NetReliablePeerSlot netRuntimeReliablePeerSlots[40];
	/* Never read or written by name. */
	uint8_t unknownNetState_C2B51[0x238];
	/* netRuntimeReliablePeerSlots in use. */
	uint32_t netReliablePeerSlotCount;
	/* Despite the name, not a receive queue: after a flight,
	 * NetSession_ExportRuntimeState points it at the flight's 256-entry
	 * sent world-message history, which Net_PumpIncomingPackets resends
	 * from on a WORLD_NACK. */
	NetQueuedPacket *netExportRecvQueuePtr;
	/* Despite the name, the write index of that world-message history. */
	int netExportRecvQueueHighWater;
	/* Heap table of each string's offset in uiStringData. */
	unsigned int *uiStringOffsets;
	/* Heap block of the string table's text, from fronttxt.txt. */
	char *uiStringData;
	unsigned int uiStringCount;    /* Strings in the table. */
	unsigned int uiStringCapacity; /* Entries uiStringOffsets holds. */
	/* Lowercase drive letter of the install folder; 0 in the modern
	 * build. */
	char installDriveLetter;
	/* Drive letter of the game CD; 0 when none was found and in the
	 * modern build. */
	char cdDriveLetter;
	/* The install folder; "BalanceOfPower" in the modern build. */
	char installPath[256];
	/* The base game's install folder; empty in the modern build. */
	char baseGameInstallPath[256];
};

typedef char xvt_size_FrontendGlobalState
	[(sizeof(void *) != 4 || sizeof(FrontendGlobalState) == 0xC2FA7) ? 1
									 : -1];

extern FrontendGlobalState g_frontState;

#ifdef __cplusplus
}
#endif

#endif
