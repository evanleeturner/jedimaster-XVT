#ifndef XVT_FRONTEND_FRONTEND_DISPLAY_H
#define XVT_FRONTEND_FRONTEND_DISPLAY_H

#include "aeron/compat/ddraw.h"
#include "aeron/compat/win_types.h"
#include "xvt/frontend/frontend_screen.h"
#include "xvt/util/win32.h"
#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FrontendPaletteEntry {
	uint8_t red;
	uint8_t green;
	uint8_t blue;
	uint8_t flags;
} FrontendPaletteEntry;

extern int g_pixelFormatCode;
extern int g_flightRenderToFrontend;
extern int g_optNoFullscreen;
extern int g_noPageFlip;
extern int g_optSkipIntro;
extern int g_optIsHost;
extern int g_optIsClient;
extern char* g_cmdLine;
extern int g_shutdownComplete;
extern void* g_cursorBitmap;
extern int g_gameMainSkipIntroRelaunchGate;
extern const unsigned int g_colorDistLUT[256];

int GameMain(void* hInstance, void* hPrevInstance, char* lpCmdLine, int nShowCmd);
HRESULT FrontendDisplay_RestoreLostSurfaces(void);
void FrontendDisplay_Shutdown(int bDestroyWindow);
int32_t AERON_DXAPI FrontendDisplay_MainWndProc(void* hWnd, unsigned int Msg, uint32_t wParam,
												int32_t lParam);
int32_t AERON_DXAPI FrontendDisplay_WndProc(void* hWnd, unsigned int Msg, uint32_t wParam, int32_t lParam);
int FrontendDisplay_ReportDirectDrawInitFailure(void* hWnd, int stage);
int FrontendDisplay_ShowGameMessageBox(const char* text);
uint32_t FrontendDisplay_RunMainLoop(void* hInstance, void* hPrevInstance, char* lpCmdLine, int nShowCmd);
int FrontendDisplay_InitMainWindow(void* hInstance, int nShowCmd);
uint32_t FrontendDisplay_Init(void* hInstance, void* hPrevInstance, char* lpCmdLine, int nShowCmd,
							  FrontendScreenUpdateFn screenUpdateFn, FrontendScreenExitFn screenExitFn,
							  int (*modeInitFn)(void), int fps, int bpp);
uint8_t* FrontendDisplay_LockBackBuffer(void);
void FrontendDisplay_UnlockBackBuffer(void);
void FrontendDisplay_PresentFrame(void);
void FrontendDisplay_ClearPresentFrameReady(void);
void FrontendDisplay_SetSurfaceClearColor(uint32_t color);
void FrontendDisplay_ClearBackBuffer(void);
void FrontendDisplay_GetScreenClipRect(RECT* outRect);
void FrontendDisplay_SetScreenClipRect640x480(const RECT* src);
void FrontendDisplay_DisableEscapeClose(void);
int FrontendDisplay_GetFrameCounter(void);
int FrontendDisplay_SetFrameRate(int fps);
int FrontendDisplay_LockOffscreenSurface(void);
int FrontendDisplay_UnlockOffscreenSurface(int saveToBackup);
int FrontendDisplay_EnableOffscreenRestore(void);
int FrontendDisplay_DisableOffscreenRestore(void);
void FrontendDisplay_ClearOffscreenSurface(void);
int FrontendDisplay_GetPixelFormat555(void);
int FrontendDisplay_GetFrontendOrFlightDrawPitch(void);
int FrontendDisplay_GetBytesPerPixel(void);
uint32_t FrontendDisplay_InitPreservingNetworkSession(void* hInstance, void* hPrevInstance, char* lpCmdLine,
													  int nShowCmd, FrontendScreenUpdateFn screenUpdateFn,
													  FrontendScreenExitFn screenExitFn,
													  int (*modeInitFn)(void), int fps, int bpp);
void FrontendDisplay_ResetGlobalStatePreservingNetworkSession(void);
int FrontendDisplay_CaptureScreenshot(void);
uint8_t* FrontendDisplay_LockSurfaceForFlight(void);
int FrontendDisplay_RunFrame(void);
void* FrontendDisplay_GetMainWindowHandle(void);
IDirectDraw* FrontendDisplay_GetDirectDraw(void);
int FrontendDisplay_ReleaseSurfacesForFlight(void);
int FrontendDisplay_ReinitSurfaces(void);
void FrontendDisplay_SetWndProcMode(uint8_t mode);
int FrontendDisplay_GetWndProcMode(void);
int Win32_CheckSingleInstance(void);
void FrontendDisplay_FlipDirectDrawToGDISurface(void);
const DxGuid* FrontendDisplay_LoadDriverGuid(void);
int FrontendDisplay_DrawGdiTextOnSecondaryDisplay(const RECT* unused, const char* text,
												  const char* overlayText);
int FrontendDisplay_ClearSecondaryDisplayGdi(const RECT* unused);
int FrontendDisplay_IsSecondaryDirectDrawActive(void);
void FrontendDisplay_SetPalette(void);
int FrontendDisplay_PackRGB(uint8_t r, uint8_t g, uint8_t b);
int FrontendDisplay_SaveBackBuffer(void);
int FrontendDisplay_RestoreBackBuffer(void);
IDirectDrawPalette* FrontendDisplay_LoadPalette(IDirectDraw* pDD, const char* lpName);
uint32_t FrontendDisplay_ConvertColorRefToSurfacePixel(IDirectDrawSurface* surface, uint32_t color);
HRESULT FrontendDisplay_SetSurfaceColorKey(IDirectDrawSurface* surface, uint32_t color);

#ifdef __cplusplus
}
#endif

#endif
