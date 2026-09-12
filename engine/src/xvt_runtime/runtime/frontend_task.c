#include "xvt_runtime/runtime/frontend_task.h"

#include "xvt_runtime/runtime/presentation.h"
#include "xvt_runtime/snapshot/render_frontend.h"

#include "aeron/aeron.h"
#include "aeron/compat/dsound.h"
#include "xvt/assets/model_preview.h"
#include "xvt/assets/opt_model.h"
#include "xvt/audio/cd_audio.h"
#include "xvt/audio/direct_sound.h"
#include "xvt/audio/sound.h"
#include "xvt/flight/flight.h"
#include "xvt/frontend/concourse.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/credits.h"
#include "xvt/frontend/cutscene.h"
#include "xvt/frontend/frontend.h"
#include "xvt/frontend/frontend_bootstrap.h"
#include "xvt/frontend/frontend_cursor.h"
#include "xvt/frontend/frontend_draw.h"
#include "xvt/frontend/frontend_joystick.h"
#include "xvt/frontend/frontend_mission_list.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/frontend/pilot.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt/frontend/tech_library.h"
#include "xvt/net/frontend_net.h"
#include "xvt/util/memory.h"
#include "xvt_runtime/runtime/campaign_task.h"
#include "xvt_runtime/runtime/cd_task.h"
#include "xvt_runtime/runtime/dialog_task.h"
#include "xvt_runtime/runtime/frontend_actions.h"
#include "xvt_runtime/runtime/frontend_movies.h"
#include "xvt_runtime/runtime/launch_task.h"
#include "xvt_runtime/runtime/movie_task.h"
#include "xvt_runtime/runtime/network_task.h"
#include "xvt_runtime/storage/storage.h"
#include "xvt_runtime/timing/host_clock.h"

#include <stdlib.h>
#include <string.h>

static uint64_t g_nextFrame;
static uint64_t g_nextJoystick;
static int g_quit;
static int g_initialized;
static int g_startupMode;
static int g_continuationFrame;

int XvtFrontendTask_Init(int skip_intro) {
	static char commandLine[] = "";
	memset(&g_frontState, 0, sizeof(g_frontState));
	g_shutdownComplete = 0;
	g_quit = 0;
	XvtFrontendAction_Reset();
	XvtFrontendMovies_Reset();
	XvtCampaignTask_Reset();
	g_initialized = 1;
	g_cmdLine = commandLine;
	g_optSkipIntro = skip_intro;
	g_noPageFlip = 1;
	g_optNoFullscreen = 0;
	g_frontState.frontendSoundBuffers = calloc(128, sizeof(FrontendSoundBufferRecord));
	g_frontState.frontendSoundVoices = calloc(12, sizeof(FrontendSoundVoice));
	g_frontState.resourceTable = calloc(512, sizeof(FrontImageResourceRecord));
	if (!g_frontState.frontendSoundBuffers || !g_frontState.frontendSoundVoices ||
		!g_frontState.resourceTable)
		return 0;
	g_frontState.clipMaxX = 639;
	g_frontState.clipMaxY = 479;
	g_frontState.displayBpp = 16;
	g_frontState.presentFrameReady = 1;
	g_frontState.cdAudioSavedAuxVolume = -1;
	g_frontState.appActive = 1;
	FrontendDisplay_SetFrameRate(24);
	File_DetectGameAndCdPaths(NULL);
	Config_Load();
	if (!FrontendDisplay_InitMainWindow(NULL, 0))
		return 0;
	g_frontState.screenStates[0].updateFn =
		skip_intro ? Concourse_Update : FrontendBootstrap_PlayOpeningAndEnterCredits;
	g_frontState.screenStates[0].exitFn =
		skip_intro ? Concourse_Exit : FrontendBootstrap_ExitIntroAndLoadCredits;
	g_startupMode = skip_intro ? 2 : 1;
	g_nextFrame = XvtTime_GetElapsedUs();
	g_nextJoystick = g_nextFrame;
	Aeron_LogInfo("xvt.frontend", "Frontend initialized");
	return 1;
}

void XvtFrontendTask_ServiceFrameSystems(void) {
	uint64_t now = XvtTime_GetElapsedUs();
	if (now >= g_nextJoystick) {
		Joystick_UpdateState(0);
		Joystick_UpdateState(1);
		g_nextJoystick = now + 100000;
	}
	if (!XvtNetworkTask_IsActive())
		Net_PumpIncomingPackets();
	XvtCdTask_Tick();
}

int XvtFrontendTask_RunFrame(void) {
	FrontendScreenExitFn exitFn;
	FrontendScreenUpdateFn updateFn;
	int result;
	int stack = g_frontState.screenStackTop;
	int modal = XvtDialog_IsActive();
	g_continuationFrame = 0;
	g_frontState.netReadyPlayerLeftThisFrame = 0;
	if (g_frontState.glyphScratchTtl)
		memset(&g_frontState.glyphScratchBuffer, 0, sizeof(g_frontState.glyphScratchBuffer));
	updateFn = g_frontState.screenStates[stack].updateFn;
	if (!updateFn)
		return 0;
	XvtPresentation_RequireClassic();
	XvtRenderFrontend_BeginDraw();
	g_drawSurfacePtr = FrontendDisplay_LockBackBuffer();
	if (!g_drawSurfacePtr) {
		XvtStorage_Fatal("Cannot lock frontend display", 1);
		return 2;
	}
	exitFn = g_frontState.screenStates[stack].exitFn;
	g_continuationFrame = XvtNetworkTask_Resume(&result);
	if (!g_continuationFrame)
		g_continuationFrame = XvtDialog_ResumeContinuation(&result);
	if (!g_continuationFrame)
		result = updateFn(g_frontState.frameCounter);
	if (XvtCampaignTask_IsPending() || XvtNetworkTask_IsActive()) {
		/* The entry prefix is suspended before the screen has completed frame zero. */
		FrontendDisplay_UnlockBackBuffer();
		if (g_continuationFrame && XvtNetworkTask_IsActive()) {
			FrontendCursor_Draw();
			FrontendDisplay_PresentFrame();
			g_frontState.mouseClickLatch = 0;
			g_frontState.mouseRightClickLatch = 0;
			memset(g_frontState.joystickButtonReleased, 0, sizeof(g_frontState.joystickButtonReleased));
		}
		return 0;
	}
	if (!modal && XvtDialog_IsActive()) {
		FrontendDisplay_UnlockBackBuffer();
		return 0;
	}
	if (g_frontState.screenCallbacksDirty || result == 1) {
		g_frontState.screenCallbacksDirty = 0;
		if (exitFn)
			exitFn(g_frontState.frameCounter);
	}
	FrontendDisplay_UnlockBackBuffer();
	if (g_frontState.pendingScreenUpdateFn) {
		FrontendScreen_PushState(g_frontState.pendingScreenUpdateFn, &g_frontState.pendingScreenRect);
		g_frontState.pendingScreenUpdateFn = NULL;
	}
	if (!XvtMovieTask_IsActive() && g_frontState.cursorVisible)
		FrontendCursor_Draw();
	memset(g_frontState.joystickButtonReleased, 0, sizeof(g_frontState.joystickButtonReleased));
	++g_frontState.frameCounter;
	if (g_frontState.glyphScratchTtl)
		--g_frontState.glyphScratchTtl;
	g_frontState.mouseClickLatch = 0;
	g_frontState.mouseRightClickLatch = 0;
	return result;
}

void XvtFrontendTask_Tick(void) {
	uint64_t now = XvtTime_GetElapsedUs();
	int result;
	if (g_quit || now < g_nextFrame)
		return;
	g_nextFrame = now + (uint64_t)g_frontState.frameIntervalMs * 1000;
	if (g_startupMode) {
		int mode = g_startupMode;
		g_startupMode = 0;
		if (mode == 2) {
			if (Frontend_LoadResources())
				return;
		} else {
			FrontendBootstrap_InitMode();
		}
	}
	if (XvtLaunchTask_IsActive()) {
		XvtLaunchTask_Tick();
		return;
	}
	int credits_frame =
		!XvtDialog_IsActive() &&
		g_frontState.screenStates[g_frontState.screenStackTop].updateFn == Config_CreditsScreen;
	if (XvtDialog_IsActive()) {
		XvtDialog_Tick();
		result = 0;
	} else {
		result = XvtFrontendTask_RunFrame();
	}
	if (result == 1 || result == 2)
		g_quit = 1;
	/* The original credits callback blocks in the music fade before presenting.
	 * Retain its last presentation through the fade and concourse transition. */
	if (credits_frame && g_creditsExitPending)
		return;
	/* Keep the last presented dialog until the parent has drawn its next frame. */
	if (!XvtMovieTask_IsActive() && !XvtDialog_HasResult() && !g_continuationFrame &&
		!XvtCampaignTask_IsPending() && !XvtNetworkTask_IsActive())
		FrontendDisplay_PresentFrame();
}

int XvtFrontendTask_ShouldQuit(void) { return g_quit; }

uint64_t XvtFrontendTask_NextWakeDelayUs(void) {
	uint64_t now = XvtTime_GetElapsedUs();
	uint64_t delay = g_nextFrame > now ? g_nextFrame - now : 0;
	uint64_t cd = XvtCdTask_NextWakeDelayUs();
	return cd < delay ? cd : delay;
}

void XvtFrontendTask_Shutdown(void) {
	int index;
	if (!g_initialized)
		return;
	XvtCampaignTask_Reset();
	XvtNetworkTask_Shutdown();
	Net_ShutdownDirectPlaySessionForQuit();
	XvtFrontendMovies_Reset();
	XvtCdTask_Cancel();
	XvtLaunchTask_Shutdown();
	XvtDialog_Shutdown();
	CDAudio_CloseDevice();
	if (g_frontendCreditsFile)
		File_Close(g_frontendCreditsFile);
	g_frontendCreditsFile = NULL;
	FrontendDisplay_UnlockBackBuffer();
	if (g_frontState.uiStringCount) {
		if (g_pilotData.name[0] && !Pilot_Save(0))
			XvtStorage_Fatal("Cannot save the selected pilot", 1);
		Config_Write();
	}
	Concourse_Exit(0);
	free(g_shipList);
	g_shipList = NULL;
	g_shipCount = 0;
	free(g_missionList);
	g_missionList = NULL;
	free(g_techLibrarySpecTextTable);
	g_techLibrarySpecTextTable = NULL;
	ModelPreview_FreeResources();
	for (index = 0; index < 32768; ++index)
		if (g_handleTables.ptrTable[index])
			Memory_FreeHandle((unsigned int)index + 1);
	memset(g_loadedModels, 0, sizeof(g_loadedModels));
	g_modelPreviewModelData = NULL;
	g_modelPreviewAuxBufferHandle = 0;
	g_modelPreviewAuxBufferCapacityBytes = 0;
	if (g_frontState.frontendSoundVoices) {
		for (index = 0; index < 12; ++index) {
			IDirectSoundBuffer* buffer = g_frontState.frontendSoundVoices[index].buffer;
			if (buffer) {
				buffer->lpVtbl->Stop(buffer);
				buffer->lpVtbl->Release(buffer);
				g_frontState.frontendSoundVoices[index].buffer = NULL;
			}
		}
	}
	if (g_frontState.frontendSoundBuffers) {
		for (index = 0; index < 128; ++index) {
			IDirectSoundBuffer* buffer = g_frontState.frontendSoundBuffers[index].buffer;
			if (buffer) {
				buffer->lpVtbl->Release(buffer);
				g_frontState.frontendSoundBuffers[index].buffer = NULL;
			}
		}
	}
	if (g_frontState.frontendPrimarySoundBuffer) {
		g_frontState.frontendPrimarySoundBuffer->lpVtbl->Release(g_frontState.frontendPrimarySoundBuffer);
		g_frontState.frontendPrimarySoundBuffer = NULL;
	}
	FrontendDisplay_Shutdown(0);
	free(g_cursorBitmap);
	g_cursorBitmap = NULL;
	free(g_frontendChatLogBuffer);
	g_frontendChatLogBuffer = NULL;
	free(g_cutsceneTable);
	g_cutsceneTable = NULL;
	g_cutsceneCount = 0;
	free(g_campaignAwardSprites);
	g_campaignAwardSprites = NULL;
	g_campaignAwardSpriteCount = 0;
	g_initialized = 0;
}
