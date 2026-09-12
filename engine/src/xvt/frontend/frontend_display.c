#include "xvt/frontend/frontend_display.h"

#ifdef XVT_MODERN
#include "xvt_runtime/runtime/presentation.h"
#include "xvt_runtime/snapshot/render_frontend.h"
#endif
#ifdef XVT_MODERN
#include "xvt_runtime/runtime/frontend_task.h"
#endif
#include "xvt/assets/file.h"
#include "xvt/audio/cd_audio.h"
#include "xvt/audio/frontend_sound.h"
#include "xvt/flight/flight.h"
#include "xvt/frontend/concourse.h"
#include "xvt/frontend/cutscene.h"
#include "xvt/frontend/front_image.h"
#include "xvt/frontend/frontend.h"
#include "xvt/frontend/frontend_bootstrap.h"
#include "xvt/frontend/frontend_cursor.h"
#include "xvt/frontend/frontend_draw.h"
#include "xvt/frontend/frontend_joystick.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt/frontend/frontend_string.h"
#include "xvt/frontend/frontend_text.h"
#include "xvt/frontend/movie.h"
#include "xvt/frontend/pilot.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt/net/frontend_net.h"
#include "xvt/net/net.h"
#include "xvt_runtime/compat/win_message_port.h"

#ifdef XVT_MODERN
#include "aeron/aeron.h"
#include "aeron/compat/host.h"
#include "aeron/dialog.h"
#include "xvt/util/time.h"
#else
typedef struct WNDCLASSA {
	unsigned int style;
	int32_t(AERON_DXAPI* lpfnWndProc)(void* hWnd, unsigned int Msg, uint32_t wParam, int32_t lParam);
	int cbClsExtra;
	int cbWndExtra;
	void* hInstance;
	void* hIcon;
	void* hCursor;
	void* hbrBackground;
	const char* lpszMenuName;
	const char* lpszClassName;
} WNDCLASSA;

struct FrontendDisplayWin32Message {
	void* window;
	uint32_t message;
	uint32_t wParam;
	int32_t lParam;
	uint32_t time;
	int32_t pointX;
	int32_t pointY;
};

uint32_t GetTickCount(void);
__declspec(dllimport) int __stdcall TranslateMessage(const struct FrontendDisplayWin32Message* message);
__declspec(dllimport) int __stdcall GetMessageA(struct FrontendDisplayWin32Message* message, void* hWnd,
												unsigned int filterMin, unsigned int filterMax);
__declspec(dllimport) int32_t __stdcall DispatchMessageA(const struct FrontendDisplayWin32Message* message);
__declspec(dllimport) int __stdcall PeekMessageA(struct FrontendDisplayWin32Message* message, void* hWnd,
												 unsigned int filterMin, unsigned int filterMax,
												 unsigned int removeMessage);
__declspec(dllimport) void* __stdcall LoadIconA(void* hInstance, uintptr_t iconName);
__declspec(dllimport) void* __stdcall LoadCursorA(void* hInstance, uintptr_t cursorName);
__declspec(dllimport) uint16_t __stdcall RegisterClassA(const WNDCLASSA* windowClass);
__declspec(dllimport) void* __stdcall CreateWindowExA(uint32_t exStyle, const char* className,
													  const char* windowName, uint32_t style, int x, int y,
													  int width, int height, void* parent, void* menu,
													  void* instance, void* param);
__declspec(dllimport) int __stdcall UpdateWindow(void* hWnd);
__declspec(dllimport) void* __stdcall SetFocus(void* hWnd);
__declspec(dllimport) int __stdcall GetKeyboardState(uint8_t* keyState);
__declspec(dllimport) void* __stdcall FindWindowA(const char* className, const char* windowName);
__declspec(dllimport) int __stdcall MessageBoxA(void* hWnd, const char* text, const char* caption,
												unsigned int type);
__declspec(dllimport) int __stdcall ShowWindowAsync(void* hWnd, int nCmdShow);
__declspec(dllimport) void* __stdcall CreateDCA(const char* driver, const char* device, const char* port,
												void* deviceMode);
__declspec(dllimport) void* __stdcall GetStockObject(int objectIndex);
__declspec(dllimport) void* __stdcall SelectObject(void* dc, void* object);
__declspec(dllimport) int __stdcall GetSystemMetrics(int index);
__declspec(dllimport) int __stdcall Rectangle(void* dc, int left, int top, int right, int bottom);
__declspec(dllimport) int __stdcall DeleteDC(void* dc);
__declspec(dllimport) void* __stdcall CreateFontA(int height, int width, int escapement, int orientation,
												  int weight, unsigned int italic, unsigned int underline,
												  unsigned int strikeOut, unsigned int charSet,
												  unsigned int outPrecision, unsigned int clipPrecision,
												  unsigned int quality, unsigned int pitchAndFamily,
												  const char* faceName);
__declspec(dllimport) int __stdcall SetMapMode(void* dc, int mode);
__declspec(dllimport) int __stdcall SetTextCharacterExtra(void* dc, int extra);
__declspec(dllimport) uint32_t __stdcall SetTextColor(void* dc, uint32_t color);
__declspec(dllimport) uint32_t __stdcall SetBkColor(void* dc, uint32_t color);
__declspec(dllimport) int __stdcall SetBkMode(void* dc, int mode);
__declspec(dllimport) int __stdcall DrawTextA(void* dc, const char* text, int length, RECT* rect,
											  unsigned int format);
__declspec(dllimport) int __stdcall DeleteObject(void* object);
__declspec(dllimport) uint32_t __stdcall GetPixel(void* dc, int x, int y);
__declspec(dllimport) uint32_t __stdcall SetPixel(void* dc, int x, int y, uint32_t color);
__declspec(dllimport) int __stdcall DestroyWindow(void* hWnd);
__declspec(dllimport) int __stdcall ShowCursor(int show);
__declspec(dllimport) int __stdcall SetCursorPos(int x, int y);
__declspec(dllimport) void* __stdcall SetCapture(void* hWnd);
__declspec(dllimport) int __stdcall ReleaseCapture(void);
__declspec(dllimport) void* __stdcall SetCursor(void* cursor);
__declspec(dllimport) int __stdcall PostMessageA(void* hWnd, unsigned int message, uint32_t wParam,
												 int32_t lParam);
__declspec(dllimport) void __stdcall PostQuitMessage(int exitCode);
__declspec(dllimport) int32_t __stdcall DefWindowProcA(void* hWnd, unsigned int message, uint32_t wParam,
													   int32_t lParam);
__declspec(dllimport) void* __stdcall FindResourceA(void* module, const char* name, uintptr_t type);
__declspec(dllimport) void* __stdcall LoadResource(void* module, void* resourceInfo);
__declspec(dllimport) void* __stdcall LockResource(void* resourceData);
__declspec(dllimport) int __stdcall _lopen(const char* path, int mode);
__declspec(dllimport) unsigned int __stdcall _lread(int file, void* buffer, unsigned int size);
__declspec(dllimport) int __stdcall _lclose(int file);
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef HRESULT(AERON_DXAPI* FrontendDisplaySurfaceGetDcFunc)(IDirectDrawSurface* surface, void** dc);
typedef HRESULT(AERON_DXAPI* FrontendDisplaySurfaceReleaseDcFunc)(IDirectDrawSurface* surface, void* dc);

#pragma pack(push, 1)

typedef struct FrontendDisplayBmpFileHeader {
	uint16_t signature;
	uint32_t fileSize;
	uint16_t reserved0;
	uint16_t reserved1;
	uint32_t pixelOffset;
} FrontendDisplayBmpFileHeader;

typedef struct FrontendDisplayBmpInfoHeader {
	uint32_t headerSize;
	int32_t width;
	int32_t height;
	uint16_t planes;
	uint16_t bitsPerPixel;
	uint32_t compression;
	uint32_t imageSize;
	int32_t pixelsPerMeterX;
	int32_t pixelsPerMeterY;
	uint32_t colorsUsed;
	uint32_t colorsImportant;
} FrontendDisplayBmpInfoHeader;

#pragma pack(pop)
typedef char xvt_size_FrontendDisplayBmpFileHeader[(sizeof(FrontendDisplayBmpFileHeader) == 14) ? 1 : -1];
typedef char xvt_size_FrontendDisplayBmpInfoHeader[(sizeof(FrontendDisplayBmpInfoHeader) == 40) ? 1 : -1];

// GLOBAL: XVT 0x52BA8C
int g_shutdownComplete = 0;
// GLOBAL: XVT 0x52BA90
static char g_windowName[] = "X-Wing vs. TIE Fighter";
// GLOBAL: XVT 0x52BB40
const unsigned int g_colorDistLUT[256] = {
	0,     1,     4,     9,     16,    25,    36,    49,    64,    81,    100,   121,   144,   169,   196,
	225,   256,   289,   324,   361,   400,   441,   484,   529,   576,   625,   676,   729,   784,   841,
	900,   961,   1024,  1089,  1156,  1225,  1296,  1369,  1444,  1521,  1600,  1681,  1764,  1849,  1936,
	2025,  2116,  2209,  2304,  2401,  2500,  2601,  2704,  2809,  2916,  3025,  3136,  3249,  3364,  3481,
	3600,  3721,  3844,  3969,  4096,  4225,  4356,  4489,  4624,  4761,  4900,  5041,  5184,  5329,  5476,
	5625,  5776,  5929,  6084,  6241,  6400,  6561,  6724,  6889,  7056,  7225,  7396,  7569,  7744,  7921,
	8100,  8281,  8464,  8649,  8836,  9025,  9216,  9409,  9604,  9801,  10000, 10201, 10404, 10609, 10816,
	11025, 11236, 11449, 11664, 11881, 12100, 12321, 12544, 12769, 12996, 13225, 13456, 13689, 13924, 14161,
	14400, 14641, 14884, 15129, 15376, 15625, 15876, 16129, 16384, 16641, 16900, 17161, 17424, 17689, 17956,
	18225, 18496, 18769, 19044, 19321, 19600, 19881, 20164, 20449, 20736, 21025, 21316, 21609, 21904, 22201,
	22500, 22801, 23104, 23409, 23716, 24025, 24336, 24649, 24964, 25281, 25600, 25921, 26244, 26569, 26896,
	27225, 27556, 27889, 28224, 28561, 28900, 29241, 29584, 29929, 30276, 30625, 30976, 31329, 31684, 32041,
	32400, 32761, 33124, 33489, 33856, 34225, 34596, 34969, 35344, 35721, 36100, 36481, 36864, 37249, 37636,
	38025, 38416, 38809, 39204, 39601, 40000, 40401, 40804, 41209, 41616, 42025, 42436, 42849, 43264, 43681,
	44100, 44521, 44944, 45369, 45796, 46225, 46656, 47089, 47524, 47961, 48400, 48841, 49284, 49729, 50176,
	50625, 51076, 51529, 51984, 52441, 52900, 53361, 53824, 54289, 54756, 55225, 55696, 56169, 56644, 57121,
	57600, 58081, 58564, 59049, 59536, 60025, 60516, 61009, 61504, 62001, 62500, 63001, 63504, 64009, 64516,
	65025,
};
// GLOBAL: XVT 0x665420
static DxGuid g_configuredDirectDrawDriverGuid = { 0 };

// GLOBAL: XVT 0x527EB8
int g_pixelFormatCode = 8;
// GLOBAL: XVT 0x527EA0
int g_flightRenderToFrontend = 1;
// GLOBAL: XVT 0xB69CB0
int g_optNoFullscreen = 0;
// GLOBAL: XVT 0xB69CBC
int g_noPageFlip = 0;
// GLOBAL: XVT 0xB69CAC
char* g_cmdLine;
// GLOBAL: XVT 0xB69CB8
int g_optSkipIntro;
// GLOBAL: XVT 0xB69CA8
int g_optIsHost;
// GLOBAL: XVT 0xB69CB4
int g_optIsClient;
// GLOBAL: XVT 0xB6A2AC
void* g_cursorBitmap;
// GLOBAL: XVT 0x52BA5C
int g_gameMainSkipIntroRelaunchGate;

// FUNCTION: XVT 0x4D3600
int GameMain(void* hInstance, void* hPrevInstance, char* lpCmdLine, int nShowCmd) {
	FrontendScreenUpdateFn updateFunction;
	FrontendScreenExitFn exitFunction;
	int (*initFunction)(void);

	g_cmdLine = lpCmdLine;
	if (strstr(lpCmdLine, "nofrontflip") != NULL)
		g_noPageFlip = 1;
	else
		g_noPageFlip = 0;
	if (strstr(lpCmdLine, "nopageflip") != NULL || strstr(lpCmdLine, "nofullscreen") != NULL)
		g_optNoFullscreen = 1;
	else
		g_optNoFullscreen = 0;
	if (strstr(lpCmdLine, "skipintro") != NULL)
		g_optSkipIntro = 1;
	else
		g_optSkipIntro = 0;
	if (strstr(lpCmdLine, "ishost") != NULL)
		g_optIsHost = 1;
	else
		g_optIsHost = 0;
	if (strstr(lpCmdLine, "isclient") != NULL)
		g_optIsClient = 1;
	else
		g_optIsClient = 0;
	if (Win32_CheckSingleInstance() != 0)
		exit(0);
	if (g_optSkipIntro != 0 || g_optIsHost != 0 || g_optIsClient != 0) {
		updateFunction = Concourse_Update;
		exitFunction = Concourse_Exit;
		initFunction = Frontend_LoadResources;
	} else {
		updateFunction = FrontendBootstrap_PlayOpeningAndEnterCredits;
		exitFunction = FrontendBootstrap_ExitIntroAndLoadCredits;
		initFunction = FrontendBootstrap_InitMode;
	}
	FrontendDisplay_Init(hInstance, hPrevInstance, lpCmdLine, nShowCmd, updateFunction, exitFunction,
						 initFunction, 24, 16);
	free(g_cursorBitmap);
	g_cursorBitmap = NULL;
	free(g_frontendChatLogBuffer);
	g_frontendChatLogBuffer = NULL;
	if (g_cutsceneTable != NULL) {
		free(g_cutsceneTable);
		g_cutsceneTable = NULL;
		g_cutsceneCount = 0;
	}
	free(g_campaignAwardSprites);
	g_campaignAwardSprites = NULL;
	g_campaignAwardSpriteCount = 0;
	Pilot_Save(0);
	return g_gameMainSkipIntroRelaunchGate == 0;
}

// FUNCTION: XVT 0x4D37E0
HRESULT FrontendDisplay_RestoreLostSurfaces(void) {
	HRESULT result;

	result = g_frontState.primarySurface->lpVtbl->Restore(g_frontState.primarySurface);
	if (result == 0) {
		if (g_optNoFullscreen != 0 || g_noPageFlip != 0)
			return g_frontState.backBufferSurface->lpVtbl->Restore(g_frontState.backBufferSurface);
		return g_frontState.offscreenSurface->lpVtbl->Restore(g_frontState.offscreenSurface);
	}
	return result;
}

// FUNCTION: XVT 0x4D3820
void FrontendDisplay_Shutdown(int bDestroyWindow) {
	int screenIndex;

	if (g_shutdownComplete != 0) {
		return;
	}
	g_shutdownComplete = 1;
	FrontendSound_ShutdownDirectSound();
	FrontendText_FreeAllFonts();
	for (screenIndex = 0; screenIndex < g_frontState.screenStackTop; screenIndex++) {
		if (g_frontState.screenStates[screenIndex].savedImage.pixels != NULL) {
			free(g_frontState.screenStates[screenIndex].savedImage.pixels);
			g_frontState.screenStates[screenIndex].savedImage.pixels = NULL;
			g_frontState.screenStates[screenIndex].savedImage.pixelCount = 0;
		}
	}
	FrontImage_FreeAllResources();
	if (g_frontState.frontendSoundBuffers != NULL) {
		free(g_frontState.frontendSoundBuffers);
		g_frontState.frontendSoundBuffers = NULL;
	}
	if (g_frontState.frontendSoundVoices != NULL) {
		free(g_frontState.frontendSoundVoices);
		g_frontState.frontendSoundVoices = NULL;
	}
	if (g_frontState.resourceTable != NULL) {
		free(g_frontState.resourceTable);
		g_frontState.resourceTable = NULL;
	}
	if (g_frontState.offscreenBackupBuffer != NULL) {
		free(g_frontState.offscreenBackupBuffer);
		g_frontState.offscreenBackupBuffer = NULL;
	}
	FrontendString_UnloadTable();
	if (g_frontState.directDraw != NULL) {
		g_frontState.directDraw->lpVtbl->FlipToGDISurface(g_frontState.directDraw);
		g_frontState.directDraw->lpVtbl->RestoreDisplayMode(g_frontState.directDraw);
		if (g_frontState.primarySurface != NULL) {
			g_frontState.primarySurface->lpVtbl->Release(g_frontState.primarySurface);
			g_frontState.primarySurface = NULL;
		}
		if (g_frontState.ddPalette != NULL) {
			g_frontState.ddPalette->lpVtbl->Release(g_frontState.ddPalette);
			g_frontState.ddPalette = NULL;
		}
		if (g_frontState.offscreenSurface != NULL) {
			g_frontState.offscreenSurface->lpVtbl->Release(g_frontState.offscreenSurface);
			g_frontState.offscreenSurface = NULL;
		}
		if ((g_optNoFullscreen != 0 || g_noPageFlip != 0) && g_frontState.backBufferSurface != NULL) {
			g_frontState.backBufferSurface->lpVtbl->Release(g_frontState.backBufferSurface);
			g_frontState.backBufferSurface = NULL;
		}
		g_frontState.directDraw->lpVtbl->Release(g_frontState.directDraw);
		g_frontState.directDraw = NULL;
		if (g_frontState.hWnd != NULL && bDestroyWindow != 0) {
#ifdef XVT_MODERN
			g_frontState.hWnd = NULL;
#else
			DestroyWindow(g_frontState.hWnd);
			g_frontState.hWnd = NULL;
#endif
		}
	}
#ifdef XVT_MODERN
	Aeron_SetHostCursorVisible(0);
#else
	while (ShowCursor(1) < 0) {
	}
#endif
}

// FUNCTION: XVT 0x4D3A00
int32_t AERON_DXAPI FrontendDisplay_MainWndProc(void* hWnd, unsigned int Msg, uint32_t wParam,
												int32_t lParam) {
	int cursorClamped;

	switch (Msg) {
		case 0x02:
			FrontendDisplay_Shutdown(1);
#ifdef XVT_MODERN
			Aeron_RequestQuit();
#else
			PostQuitMessage(0);
#endif
			return 0;

		case 0x1C:
			g_frontState.appActive = (int)wParam;
			if (wParam != 0) {
				CDAudio_RequestResumePlayback();
#ifndef XVT_MODERN
				if (g_frontState.hWnd != NULL) {
					SetCapture(g_frontState.hWnd);
				}
#endif
				g_frontState.restoreOffscreenOverlayAfterActivate = 1;
				FrontendCursor_HideOsCursor();
			} else {
				CDAudio_SuspendPlayback();
#ifndef XVT_MODERN
				ReleaseCapture();
#endif
			}
			break;

		case 0x20:
#ifdef XVT_MODERN
			Aeron_SetHostCursorVisible(0);
#else
			SetCursor(NULL);
#endif
			return 1;

		case 0x100:
			if (wParam == 27 && g_frontState.escapeCloseEnabled != 0) {
#ifdef XVT_MODERN
				Aeron_RequestQuit();
#else
				PostMessageA(hWnd, 0x10, 0, 0);
#endif
			}
			break;

		case 0x101:
			memset(g_frontState.keyDownState, 0, sizeof(g_frontState.keyDownState));
			break;

		case 0x102:
			if ((g_frontState.charReadIdx - g_frontState.charWriteIdx == 1 ||
				 (g_frontState.charWriteIdx == 1023 && g_frontState.charReadIdx == 0)) &&
				++g_frontState.charReadIdx == 1024) {
				g_frontState.charReadIdx = 0;
			}
			g_frontState.charRingBuffer[g_frontState.charWriteIdx] = (char)wParam;
			if (g_frontState.charWriteIdx == 1023) {
				g_frontState.charWriteIdx = 0;
			} else {
				++g_frontState.charWriteIdx;
			}
			break;

		case 0x105:
			if (wParam == 79) {
				FrontendDisplay_CaptureScreenshot();
				return 0;
			}
			if (wParam == 115) {
				return 0;
			}
			/* fall through */
		case 0x104:
			if (wParam == 115) {
				return 0;
			}
			break;

		case 0x200:
			g_frontState.mouseX = (uint16_t)lParam;
			g_frontState.mouseY = (uint16_t)((uint32_t)lParam >> 16);
			cursorClamped = 0;
			if (g_frontState.mouseX > 640) {
				cursorClamped = 1;
				g_frontState.mouseX = 640;
			}
			if (g_frontState.mouseY > 480) {
				cursorClamped = 1;
				g_frontState.mouseY = 480;
			}
			if (cursorClamped != 0) {
#ifdef XVT_MODERN
				XvtPresentation_WarpClassic(g_frontState.mouseX, g_frontState.mouseY);
#else
				SetCursorPos(g_frontState.mouseX, g_frontState.mouseY);
#endif
			}
			break;

		case 0x201:
			g_frontState.mouseLeftDown = 1;
			if ((wParam & 2) != 0) {
				g_frontState.mouseRightDown = 1;
			}
			break;

		case 0x202:
			g_frontState.mouseLeftDown = 0;
			g_frontState.mouseClickLatch = 1;
			if ((wParam & 2) != 0) {
				g_frontState.mouseRightDown = 0;
				g_frontState.mouseRightClickLatch = 1;
			}
			break;

		case 0x204:
			g_frontState.mouseRightDown = 1;
			if ((wParam & 1) != 0) {
				g_frontState.mouseLeftDown = 1;
			}
			break;

		case 0x205:
			g_frontState.mouseRightDown = 0;
			g_frontState.mouseRightClickLatch = 1;
			if ((wParam & 1) != 0) {
				g_frontState.mouseLeftDown = 0;
				g_frontState.mouseClickLatch = 1;
			}
			break;
	}

#ifdef XVT_MODERN
	(void)hWnd;
	(void)lParam;
	return 0;
#else
	return DefWindowProcA(hWnd, Msg, wParam, lParam);
#endif
}

// FUNCTION: XVT 0x4D3D20
int32_t AERON_DXAPI FrontendDisplay_WndProc(void* hWnd, unsigned int Msg, uint32_t wParam, int32_t lParam) {
	switch (FrontendDisplay_GetWndProcMode()) {
		case 0:
			return FrontendDisplay_MainWndProc(hWnd, Msg, wParam, lParam);
		case 1:
			return StubWndProc(hWnd, Msg, wParam, lParam);
		case 2:
			return Movie_WindowProc(hWnd, Msg, XvtPort_WinMessageParamAsPointer(wParam),
									XvtPort_WinMessageParamAsPointer((uint32_t)lParam));
		default:
#ifdef XVT_MODERN
			return 0;
#else
			return DefWindowProcA(hWnd, Msg, wParam, lParam);
#endif
	}
}

// FUNCTION: XVT 0x4D3DA0
int FrontendDisplay_ReportDirectDrawInitFailure(void* hWnd, int stage) {
	char message[256];
#ifdef XVT_MODERN
	AeronMessageBoxButton button = { 1, "OK", 1, 1 };
	AeronMessageBoxOptions options;
	(void)hWnd;
#endif

	sprintf(message, "DirectDraw Init FAILED at %d", stage);
#ifdef XVT_MODERN
	options.kind = AERON_MESSAGE_BOX_ERROR;
	options.title = g_windowName;
	options.message = message;
	options.buttons = &button;
	options.button_count = 1;
	Aeron_ShowMessageBox(&options, NULL);
#else
	MessageBoxA(hWnd, message, g_windowName, 0);
#endif
	FrontendDisplay_Shutdown(1);
	return 0;
}

// FUNCTION: XVT 0x4D3DF0
int FrontendDisplay_ShowGameMessageBox(const char* text) {
	int wasBackBufferLocked;
#ifdef XVT_MODERN
	AeronMessageBoxButton button = { 1, "OK", 1, 1 };
	AeronMessageBoxOptions options = { AERON_MESSAGE_BOX_WARNING, g_windowName, text, &button, 1 };
#endif

	wasBackBufferLocked = g_frontState.backBufferLocked;
	FrontendDisplay_UnlockBackBuffer();
	if (g_frontState.directDraw != NULL)
		g_frontState.directDraw->lpVtbl->FlipToGDISurface(g_frontState.directDraw);

#ifdef XVT_MODERN
	Aeron_ShowMessageBox(&options, NULL);
#else
	MessageBoxA(g_frontState.hWnd, text, g_windowName, 0x30);
#endif

	if (wasBackBufferLocked != 0)
		g_drawSurfacePtr = FrontendDisplay_LockBackBuffer();
	return 1;
}

// FUNCTION: XVT 0x4D3E40
uint32_t FrontendDisplay_RunMainLoop(void* hInstance, void* hPrevInstance, char* lpCmdLine, int nShowCmd) {
#ifdef XVT_MODERN
	(void)hInstance;
	(void)hPrevInstance;
	(void)lpCmdLine;
	(void)nShowCmd;
	return 0;
#else
	enum {
		SCREEN_CONTINUE = 0,
		SCREEN_FINISHED = 1,
		JOYSTICK_UPDATE_INTERVAL_MS = 100,
		WINDOW_CLOSE_MESSAGE = 0x10,
	};
	struct FrontendDisplayWin32Message message;
	uint32_t frameStart;
	uint32_t joystickUpdate;
	int updateResult;
	int frameReady;

	(void)hPrevInstance;
	(void)lpCmdLine;
	updateResult = SCREEN_CONTINUE;
	frameReady = 0;
	if (FrontendDisplay_InitMainWindow(hInstance, nShowCmd) == 0)
		return 0;
	if (g_frontState.modeInitFn != NULL && g_frontState.modeInitFn() != 0) {
		FrontendDisplay_Shutdown(1);
		return 0;
	}

	frameStart = GetTickCount();
	joystickUpdate = frameStart;
	g_frontState.textColorCodes[0] = 0xFFFF;
	g_frontState.textColorCodes[1] = FrontendDisplay_PackRGB(0x40, 0xC4, 0x40);
	g_frontState.textColorCodes[2] = FrontendDisplay_PackRGB(0xFF, 0, 0);
	g_frontState.textColorCodes[3] = FrontendDisplay_PackRGB(0xFF, 0xFF, 0);
	g_frontState.textColorCodes[4] = FrontendDisplay_PackRGB(0, 0, 0xFF);
	g_frontState.textColorCodes[5] = FrontendDisplay_PackRGB(0x80, 0x80, 0xFF);

	for (;;) {
		if (g_frontState.appActive != 0) {
			if (frameReady != 0 && updateResult == SCREEN_CONTINUE) {
				FrontendScreenExitFn exitFn;

				frameReady = 0;
				g_frontState.netReadyPlayerLeftThisFrame = 0;
				if (g_frontState.glyphScratchTtl != 0)
					memset(&g_frontState.glyphScratchBuffer, 0, sizeof(g_frontState.glyphScratchBuffer));
				Net_PumpIncomingPackets();
				if (g_frontState.screenStates[g_frontState.screenStackTop].updateFn != NULL) {
					GetKeyboardState(g_frontState.keyState);
					g_drawSurfacePtr = FrontendDisplay_LockBackBuffer();
					exitFn = g_frontState.screenStates[g_frontState.screenStackTop].exitFn;
					updateResult = g_frontState.screenStates[g_frontState.screenStackTop].updateFn(
						g_frontState.frameCounter);
					if (g_frontState.screenCallbacksDirty == 1 || updateResult == SCREEN_FINISHED) {
						g_frontState.screenCallbacksDirty = 0;
						if (exitFn != NULL)
							exitFn(g_frontState.frameCounter);
					}
					FrontendDisplay_UnlockBackBuffer();
					if (g_frontState.pendingScreenUpdateFn != NULL) {
						FrontendScreen_PushState(g_frontState.pendingScreenUpdateFn,
												 &g_frontState.pendingScreenRect);
						g_frontState.pendingScreenUpdateFn = NULL;
					}
					if (g_frontState.cursorVisible == 1)
						FrontendCursor_Draw();
					memset(g_frontState.joystickButtonReleased[0], 0,
						   sizeof(g_frontState.joystickButtonReleased[0]));
					memset(g_frontState.joystickButtonReleased[1], 0,
						   sizeof(g_frontState.joystickButtonReleased[1]));
					++g_frontState.frameCounter;
					if (updateResult == SCREEN_FINISHED) {
						if (g_frontState.cdAudioMciDeviceId != 0 && g_frontState.cdAudioCurrentTrack != 0 &&
							g_frontState.cdAudioPlaybackComplete == 0) {
							CDAudio_FadeAuxVolume(g_frontState.cdAudioTrackCache.currentAuxVolume, 0x200,
												  2000);
						}
						CDAudio_CloseDevice();
						PostMessageA(g_frontState.hWnd, WINDOW_CLOSE_MESSAGE, 0, 0);
					}
					if (g_frontState.glyphScratchTtl != 0)
						--g_frontState.glyphScratchTtl;
					g_frontState.mouseClickLatch = 0;
					g_frontState.mouseRightClickLatch = 0;
					if (g_frontState.cdAudioSuspendState == CDAudio_ResumePending &&
						GetTickCount() > g_frontState.cdAudioResumeDueTick) {
						CDAudio_ResumeSuspendedPlayback();
					}
					if (g_frontState.cdAudioCurrentTrack != 0 &&
						GetTickCount() > g_frontState.cdAudioTrackEndTick) {
						if (g_frontState.cdAudioLoopCurrentTrack != 0)
							CDAudio_PlayTrackFromTime(g_frontState.cdAudioCurrentTrack, 0, 0);
						else
							g_frontState.cdAudioPlaybackComplete = 1;
					}
				}
			}

			if (PeekMessageA(&message, NULL, 0, 0, 0) != 0) {
				if (GetMessageA(&message, NULL, 0, 0) == 0)
					return message.wParam;
				TranslateMessage(&message);
				DispatchMessageA(&message);
				continue;
			}
			if (updateResult == SCREEN_CONTINUE) {
				uint32_t now;

				now = GetTickCount();
				if ((int32_t)(now - frameStart) >= g_frontState.frameIntervalMs) {
					frameReady = 1;
					frameStart = now;
					FrontendDisplay_PresentFrame();
				}
				if ((int32_t)(now - joystickUpdate) >= JOYSTICK_UPDATE_INTERVAL_MS) {
					Joystick_UpdateState(0);
					Joystick_UpdateState(1);
					joystickUpdate = now;
				}
			}
		}

		else if (PeekMessageA(&message, NULL, 0, 0, 0) != 0) {
			if (GetMessageA(&message, NULL, 0, 0) == 0)
				return message.wParam;
			TranslateMessage(&message);
			DispatchMessageA(&message);
		}
	}
#endif
}

// FUNCTION: XVT 0x4D41E0
int FrontendDisplay_InitMainWindow(void* hInstance, int nShowCmd) {
#ifndef XVT_MODERN
	WNDCLASSA windowClass;
#endif
	DDSURFACEDESC surfaceDesc;
	DDSCAPS attachedSurfaceCaps;
	const DxGuid* driverGuid;
	void* windowHandle;
	HRESULT result;
	(void)nShowCmd;

#ifdef XVT_MODERN
	/* The host shell owns the window, so the port keeps the handle it published. */
	(void)hInstance;
	windowHandle = g_frontState.hWnd;
#else
	windowClass.style = 8; /* CS_DBLCLKS */
	windowClass.lpfnWndProc = FrontendDisplay_WndProc;
	windowClass.cbClsExtra = 0;
	windowClass.cbWndExtra = 0;
	windowClass.hInstance = hInstance;
	windowClass.hIcon = LoadIconA(hInstance, 101);
	windowClass.hCursor = LoadCursorA(NULL, 0x7F00); /* IDC_ARROW */
	windowClass.hbrBackground = GetStockObject(4);   /* BLACK_BRUSH */
	windowClass.lpszMenuName = NULL;
	windowClass.lpszClassName = g_windowName;
	RegisterClassA(&windowClass);
	windowHandle = CreateWindowExA(0, g_windowName, g_windowName, 0x90000000, 0, 0, GetSystemMetrics(0),
								   GetSystemMetrics(1), NULL, NULL, hInstance, NULL);
	if (windowHandle == NULL)
		return 0;
	g_frontState.hWnd = windowHandle;
	UpdateWindow(windowHandle);
	SetFocus(windowHandle);
#endif

	driverGuid = FrontendDisplay_LoadDriverGuid();
#ifdef XVT_MODERN
	if (DirectDrawCreate_Compat(driverGuid, &g_frontState.directDraw, NULL) != 0) {
		if (DirectDrawCreate_Compat(NULL, &g_frontState.directDraw, NULL) != 0)
#else
	if (DirectDrawCreate(driverGuid, &g_frontState.directDraw, NULL) != 0) {
		if (DirectDrawCreate(NULL, &g_frontState.directDraw, NULL) != 0)
#endif
			return FrontendDisplay_ReportDirectDrawInitFailure(windowHandle, 0);
		g_frontState.secondaryDirectDrawActive = 0;
	} else {
		g_frontState.secondaryDirectDrawActive = 0;
		if (driverGuid != NULL)
			g_frontState.secondaryDirectDrawActive = 1;
	}

	result = g_frontState.directDraw->lpVtbl->SetCooperativeLevel(
		g_frontState.directDraw, windowHandle, DDSCL_FULLSCREEN | DDSCL_EXCLUSIVE | DDSCL_ALLOWMODEX);
	if (result != 0)
		return FrontendDisplay_ReportDirectDrawInitFailure(windowHandle, 1);
	result = g_frontState.directDraw->lpVtbl->SetDisplayMode(g_frontState.directDraw, 640, 480,
															 g_frontState.displayBpp);
	if (result != 0)
		return FrontendDisplay_ReportDirectDrawInitFailure(windowHandle, 2);

	if (g_optNoFullscreen == 0 && g_noPageFlip == 0) {
		memset(&surfaceDesc, 0, sizeof(surfaceDesc));
		surfaceDesc.dwSize = sizeof(surfaceDesc);
		surfaceDesc.dwFlags = DDSD_CAPS | DDSD_BACKBUFFERCOUNT;
		surfaceDesc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE | DDSCAPS_FLIP | DDSCAPS_COMPLEX;
		surfaceDesc.dwBackBufferCount = 1;
	} else {
		memset(&surfaceDesc, 0, sizeof(surfaceDesc));
		surfaceDesc.dwSize = sizeof(surfaceDesc);
		surfaceDesc.dwFlags = DDSD_CAPS;
		surfaceDesc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
	}
	result = g_frontState.directDraw->lpVtbl->CreateSurface(g_frontState.directDraw, &surfaceDesc,
															&g_frontState.primarySurface, NULL);
	if (result != 0)
		return FrontendDisplay_ReportDirectDrawInitFailure(windowHandle, 3);
	g_frontState.primarySurface->lpVtbl->GetSurfaceDesc(g_frontState.primarySurface, &surfaceDesc);
	g_frontState.pixelFormat555 = (surfaceDesc.ddpfPixelFormat.dwGBitMask & 0x400) == 0;

	if (g_optNoFullscreen == 0 && g_noPageFlip == 0) {
		attachedSurfaceCaps.dwCaps = DDSCAPS_BACKBUFFER;
		result = g_frontState.primarySurface->lpVtbl->GetAttachedSurface(
			g_frontState.primarySurface, &attachedSurfaceCaps, &g_frontState.backBufferSurface);
	} else {
		memset(&surfaceDesc, 0, sizeof(surfaceDesc));
		surfaceDesc.dwSize = sizeof(surfaceDesc);
		surfaceDesc.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH;
		surfaceDesc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
		surfaceDesc.dwWidth = 640;
		surfaceDesc.dwHeight = 480;
		result = g_frontState.directDraw->lpVtbl->CreateSurface(g_frontState.directDraw, &surfaceDesc,
																&g_frontState.backBufferSurface, NULL);
	}
	if (result != 0)
		return FrontendDisplay_ReportDirectDrawInitFailure(windowHandle, 4);
	g_frontState.backBufferSurface->lpVtbl->GetSurfaceDesc(g_frontState.backBufferSurface, &surfaceDesc);
	g_frontState.backBufferPitch = surfaceDesc.lPitch;

	memset(&surfaceDesc, 0, sizeof(surfaceDesc));
	surfaceDesc.dwSize = sizeof(surfaceDesc);
	surfaceDesc.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH;
	if (g_optNoFullscreen == 0 && g_noPageFlip == 0)
		surfaceDesc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN;
	else
		surfaceDesc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
	surfaceDesc.dwWidth = 640;
	surfaceDesc.dwHeight = 480;
	result = g_frontState.directDraw->lpVtbl->CreateSurface(g_frontState.directDraw, &surfaceDesc,
															&g_frontState.offscreenSurface, NULL);
	if (result != 0)
		return FrontendDisplay_ReportDirectDrawInitFailure(windowHandle, 5);
	g_frontState.offscreenSurface->lpVtbl->GetSurfaceDesc(g_frontState.offscreenSurface, &surfaceDesc);
	g_frontState.offscreenSurfacePitch = surfaceDesc.lPitch;

	g_frontState.ddPalette = FrontendDisplay_LoadPalette(g_frontState.directDraw, NULL);
	if (g_optNoFullscreen == 0 && g_noPageFlip == 0) {
		if (g_frontState.ddPalette != NULL && g_frontState.displayBpp == 8)
			g_frontState.primarySurface->lpVtbl->SetPalette(g_frontState.primarySurface,
															g_frontState.ddPalette);
	} else {
		if (g_frontState.ddPalette != NULL && g_frontState.displayBpp == 8) {
			g_frontState.primarySurface->lpVtbl->SetPalette(g_frontState.primarySurface,
															g_frontState.ddPalette);
			FrontendDisplay_SetPalette();
		}
		if (g_optNoFullscreen != 0) {
			result = g_frontState.directDraw->lpVtbl->SetCooperativeLevel(g_frontState.directDraw,
																		  windowHandle, DDSCL_NORMAL);
			if (result != 0)
				return FrontendDisplay_ReportDirectDrawInitFailure(windowHandle, 1);
		}
	}

	g_frontState.textColorCodes[0] = 0xFFFF;
	g_frontState.textColorCodes[1] = FrontendDisplay_PackRGB(0, 0xFF, 0);
	g_frontState.textColorCodes[2] = FrontendDisplay_PackRGB(0xFF, 0, 0);
	g_frontState.textColorCodes[3] = FrontendDisplay_PackRGB(0xFF, 0xFF, 0);
	g_frontState.textColorCodes[4] = FrontendDisplay_PackRGB(0x32, 0x32, 0xFF);
	g_frontState.textColorCodes[5] = FrontendDisplay_PackRGB(0x80, 0x80, 0xFF);

#ifdef XVT_MODERN
	XvtRenderFrontend_Reset();
#endif
	FrontendDisplay_ClearBackBuffer();
	FrontendDisplay_ClearOffscreenSurface();
	FrontendDisplay_PresentFrame();
	if (FrontendText_LoadFont(20) != 1)
		return FrontendDisplay_ReportDirectDrawInitFailure(windowHandle, 6);

	Joystick_InitDevices();
	FrontendCursor_Init();
#ifdef XVT_MODERN
	XvtPresentation_WarpClassic(0, 0);
#else
	SetCursorPos(0, 0);
#endif
	if (FrontendSound_InitDirectSound(g_frontState.hWnd) == 0)
		FrontendDisplay_ShowGameMessageBox("Sound not available.");
#ifdef XVT_MODERN
	Aeron_SetHostCursorVisible(0);
#else
	while (ShowCursor(0) >= 0) {
	}
#endif
	g_frontState.offscreenBackupBuffer = malloc(480 * g_frontState.offscreenSurfacePitch);
	if (g_frontState.offscreenBackupBuffer != NULL)
		memset(g_frontState.offscreenBackupBuffer, 0, 480 * g_frontState.offscreenSurfacePitch);
	return 1;
}

// FUNCTION: XVT 0x4D4770
uint32_t FrontendDisplay_Init(void* hInstance, void* hPrevInstance, char* lpCmdLine, int nShowCmd,
							  FrontendScreenUpdateFn screenUpdateFn, FrontendScreenExitFn screenExitFn,
							  int (*modeInitFn)(void), int fps, int bpp) {
	int frameRate;
	int zeroValue;

	memset(&g_frontState, 0, sizeof(g_frontState));
	g_frontState.frontendDisplayWndProcMode = 0;
	g_shutdownComplete = 0;
	srand(GetTickCount());
	File_DetectGameAndCdPaths("\\wave\\PBC\\Pb1los07.wav");
	g_frontState.frontendSoundBuffers = malloc(0xB1BC);
	if (g_frontState.frontendSoundBuffers == NULL)
		return 0;
	g_frontState.frontendSoundVoices = malloc(0x90);
	if (g_frontState.frontendSoundVoices == NULL) {
		free(g_frontState.frontendSoundBuffers);
		return 0;
	}
	g_frontState.resourceTable = malloc(0x8800);
	if (g_frontState.resourceTable == NULL) {
		free(g_frontState.frontendSoundBuffers);
		free(g_frontState.frontendSoundVoices);
		return 0;
	}
	if (bpp != 8 && bpp != 16)
		return 0;

	g_frontState.clipMaxX = 639;
	g_frontState.clipMaxY = 479;
	g_frontState.presentFrameReady = 1;
	g_frontState.displayBpp = bpp;
	zeroValue = 0;
	g_frontState.pixelFormat555 = zeroValue;
	g_frontState.clipMinX = zeroValue;
	g_frontState.clipMinY = zeroValue;
	g_frontState.charWriteIdx = zeroValue;
	g_frontState.charReadIdx = zeroValue;
	g_frontState.resourceCount = zeroValue;
	g_frontState.cdAudioSavedAuxVolume = -1;
	frameRate = fps;
	if (frameRate <= zeroValue)
		frameRate = 1;
	g_frontState.frameIntervalMs = 1000 / frameRate;
	g_frontState.screenStates[0].updateFn = screenUpdateFn;
	g_frontState.screenStates[0].exitFn = screenExitFn;
	g_frontState.modeInitFn = modeInitFn;
	g_frontState.escapeCloseEnabled = 1;
	return FrontendDisplay_RunMainLoop(hInstance, hPrevInstance, lpCmdLine, nShowCmd);
}

// FUNCTION: XVT 0x4D48E0
uint8_t* FrontendDisplay_LockBackBuffer(void) {
	enum {
		FRONTEND_DDERR_SURFACEBUSY = -2005532242,
		FRONTEND_DDERR_SURFACEISOBSCURED = -2005532232,
	};

	HRESULT lockResult;

	if (g_frontState.directDraw == NULL)
		return NULL;
	if (g_frontState.backBufferSurface == NULL)
		return NULL;

#ifdef XVT_MODERN
	XvtRenderFrontend_Select(XVT_TARGET_FRONT_BACK);
#endif
	g_frontState.drawSurfacePitch = g_frontState.backBufferPitch;
	if (g_frontState.backBufferLocked != 0)
		return (uint8_t*)g_frontState.backBufferDesc.lpSurface;

	g_frontState.backBufferDesc.dwSize = sizeof(g_frontState.backBufferDesc);
	do {
		do {
			lockResult = g_frontState.backBufferSurface->lpVtbl->Lock(g_frontState.backBufferSurface, NULL,
																	  &g_frontState.backBufferDesc, 0, NULL);
			if (lockResult != DX_DDERR_SURFACELOST)
				break;
			g_frontState.backBufferSurface->lpVtbl->Restore(g_frontState.backBufferSurface);
		} while (1);
	} while (lockResult == DX_DDERR_WASSTILLDRAWING || lockResult == FRONTEND_DDERR_SURFACEBUSY ||
			 lockResult == FRONTEND_DDERR_SURFACEISOBSCURED);

	g_frontState.backBufferLocked = 1;
	return (uint8_t*)g_frontState.backBufferDesc.lpSurface;
}

// FUNCTION: XVT 0x4D4970
void FrontendDisplay_UnlockBackBuffer(void) {
	if (g_frontState.directDraw != NULL && g_frontState.backBufferSurface != NULL) {
		g_frontState.backBufferSurface->lpVtbl->Unlock(g_frontState.backBufferSurface, NULL);
		g_frontState.backBufferLocked = 0;
	}
}

// FUNCTION: XVT 0x4D49A0
void FrontendDisplay_PresentFrame(void) {
	int verticalBlankStatus;
	RECT sourceRect;
	DDSURFACEDESC surfaceDesc;
	HRESULT result;

	if (g_frontState.directDraw == NULL)
		return;

	if (g_frontState.backBufferLocked != 0)
		FrontendDisplay_UnlockBackBuffer();

	sourceRect.right = 640;
	sourceRect.bottom = 480;
	sourceRect.left = 0;
	sourceRect.top = 0;

	if (g_optNoFullscreen == 0 && g_noPageFlip == 0) {
		do {
			result = g_frontState.directDraw->lpVtbl->GetVerticalBlankStatus(g_frontState.directDraw,
																			 &verticalBlankStatus);
		} while (result == DX_DD_OK && verticalBlankStatus == 0);

		if (g_frontState.paletteNeedsSet == 1 && g_frontState.displayBpp == 8) {
			FrontendDisplay_SetPalette();
			g_frontState.paletteNeedsSet = 0;
		}

		for (;;) {
			result = g_frontState.primarySurface->lpVtbl->Flip(g_frontState.primarySurface,
															   g_frontState.backBufferSurface, 1);
			if (result == DX_DD_OK)
				break;
			if (result == DX_DDERR_SURFACELOST) {
				if (FrontendDisplay_RestoreLostSurfaces() == DX_DD_OK)
					continue;
			} else if (result == DX_DDERR_WASSTILLDRAWING) {
				continue;
			}
			break;
		}
	} else {
		for (;;) {
			result = g_frontState.primarySurface->lpVtbl->BltFast(
				g_frontState.primarySurface, 0, 0, g_frontState.backBufferSurface, &sourceRect, 0);
			if (result == DX_DD_OK)
				break;
			if (result == DX_DDERR_SURFACELOST) {
				if (FrontendDisplay_RestoreLostSurfaces() != DX_DD_OK)
					return;
			} else if (result != DX_DDERR_WASSTILLDRAWING) {
				return;
			}
		}
	}

#ifdef XVT_MODERN
	if (result == DX_DD_OK)
		XvtRenderFrontend_Present();
#endif

	if (g_frontState.offscreenRestoreEnabled != 0) {
		if (g_frontState.restoreOffscreenOverlayAfterActivate != 0) {
			g_frontState.restoreOffscreenOverlayAfterActivate = 0;
			if (g_frontState.offscreenBackupBuffer != NULL && g_frontState.offscreenSurface != NULL) {
				memset(&surfaceDesc, 0, sizeof(surfaceDesc));
				surfaceDesc.dwSize = sizeof(surfaceDesc);
				for (;;) {
					result = g_frontState.offscreenSurface->lpVtbl->Lock(g_frontState.offscreenSurface, NULL,
																		 &surfaceDesc, 0, NULL);
					if (result == DX_DD_OK)
						break;
					if (result == DX_DDERR_SURFACELOST) {
						g_frontState.offscreenSurface->lpVtbl->Restore(g_frontState.offscreenSurface);
					} else if (result != DX_DDERR_WASSTILLDRAWING) {
						break;
					}
				}

				if (surfaceDesc.lpSurface != NULL) {

#ifdef XVT_MODERN
					XvtRenderFrontend_Copy(XVT_TARGET_FRONT_BACKUP, XVT_TARGET_FRONT_OFFSCREEN);
#endif
					memcpy(surfaceDesc.lpSurface, g_frontState.offscreenBackupBuffer,
						   (size_t)(480 * g_frontState.offscreenSurfacePitch));
					g_frontState.offscreenSurface->lpVtbl->Unlock(g_frontState.offscreenSurface, NULL);
				}
			}
		}

		if (g_optNoFullscreen == 0 && g_noPageFlip == 0) {
			for (;;) {
				result = g_frontState.backBufferSurface->lpVtbl->BltFast(
					g_frontState.backBufferSurface, 0, 0, g_frontState.offscreenSurface, &sourceRect, 0);
				if (result == DX_DD_OK)
					break;
				if (result == DX_DDERR_SURFACELOST) {
					if (FrontendDisplay_RestoreLostSurfaces() != DX_DD_OK)
						return;
				} else if (result != DX_DDERR_WASSTILLDRAWING) {
					return;
				}
			}
#ifdef XVT_MODERN
			XvtRenderFrontend_Copy(XVT_TARGET_FRONT_OFFSCREEN, XVT_TARGET_FRONT_BACK);
#endif

		} else {
			FrontendDisplay_RestoreBackBuffer();
		}
	}

	if (g_frontState.presentFrameReady != 0)
		FrontendDisplay_ClearBackBuffer();
}

// FUNCTION: XVT 0x4D4BF0
void FrontendDisplay_ClearPresentFrameReady(void) { g_frontState.presentFrameReady = 0; }

// FUNCTION: XVT 0x4D4C00
void FrontendDisplay_SetSurfaceClearColor(uint32_t color) { g_frontState.surfaceClearColor = color; }

// FUNCTION: XVT 0x4D4C10
void FrontendDisplay_ClearBackBuffer(void) {
	RECT rect;
	DDBLTFX effects;
	int wasLocked;
	HRESULT result;

	if (g_frontState.directDraw == NULL || g_frontState.backBufferSurface == NULL)
		return;

	wasLocked = g_frontState.backBufferLocked;
	FrontendDisplay_UnlockBackBuffer();
	FrontendDraw_RectAssign(&rect, 0, 0, 640, 480);
	memset(&effects, 0, sizeof(effects));
	effects.dwSize = sizeof(effects);
	effects.dwFillColor = g_frontState.surfaceClearColor;
	for (;;) {
		result = g_frontState.backBufferSurface->lpVtbl->Blt(g_frontState.backBufferSurface, &rect, NULL,
															 NULL, DDBLT_COLORFILL, &effects);
		if (result == DX_DD_OK)
			break;
		if (result == DX_DDERR_SURFACELOST) {
			if (FrontendDisplay_RestoreLostSurfaces() != DX_DD_OK) {
				if (wasLocked != 0)
					g_drawSurfacePtr = FrontendDisplay_LockBackBuffer();
				return;
			}
		} else if (result != DX_DDERR_WASSTILLDRAWING) {
			if (wasLocked != 0)
				g_drawSurfacePtr = FrontendDisplay_LockBackBuffer();
			return;
		}
	}

#ifdef XVT_MODERN
	XvtRenderFrontend_Clear(XVT_TARGET_FRONT_BACK, g_frontState.surfaceClearColor);
#endif

	if (wasLocked != 0)
		g_drawSurfacePtr = FrontendDisplay_LockBackBuffer();
}

// FUNCTION: XVT 0x4D4CF0
void FrontendDisplay_GetScreenClipRect(RECT* outRect) {
	FrontendDraw_RectAssign(outRect, g_frontState.clipMinX, g_frontState.clipMinY, g_frontState.clipMaxX,
							g_frontState.clipMaxY);
}

// FUNCTION: XVT 0x4D4D20
void FrontendDisplay_SetScreenClipRect640x480(const RECT* src) {
	RECT clippedRect;

	FrontendDraw_RectCopy(&clippedRect, src);
	if (clippedRect.left < 0)
		clippedRect.left = 0;
	if (clippedRect.top < 0)
		clippedRect.top = 0;
	if (clippedRect.right >= 640)
		clippedRect.right = 639;
	if (clippedRect.bottom >= 480)
		clippedRect.bottom = 479;

	if (clippedRect.right >= clippedRect.left) {
		if (clippedRect.top <= clippedRect.bottom) {
			g_frontState.clipMinX = clippedRect.left;
			g_frontState.clipMinY = clippedRect.top;
			g_frontState.clipMaxX = clippedRect.right;
			g_frontState.clipMaxY = clippedRect.bottom;
		}
	}
}

// FUNCTION: XVT 0x4D4DD0
void FrontendDisplay_DisableEscapeClose(void) { g_frontState.escapeCloseEnabled = 0; }

// FUNCTION: XVT 0x4D4DE0
int FrontendDisplay_GetFrameCounter(void) { return g_frontState.frameCounter; }

// FUNCTION: XVT 0x4D4E00
int FrontendDisplay_SetFrameRate(int fps) { return g_frontState.frameIntervalMs = 1000 / fps; }

// FUNCTION: XVT 0x4D4E20
int FrontendDisplay_LockOffscreenSurface(void) {
	DDSURFACEDESC surfaceDesc;
	HRESULT result;

	if (g_frontState.directDraw == NULL)
		return 0;
	if (g_frontState.offscreenSurface == NULL)
		return 0;

	memset(&surfaceDesc, 0, sizeof(surfaceDesc));
	surfaceDesc.dwSize = sizeof(surfaceDesc);
	for (;;) {
		result = g_frontState.offscreenSurface->lpVtbl->Lock(g_frontState.offscreenSurface, NULL,
															 &surfaceDesc, 0, NULL);
		if (result == DX_DD_OK)
			break;
		if (result == DX_DDERR_SURFACELOST) {
			g_frontState.offscreenSurface->lpVtbl->Restore(g_frontState.offscreenSurface);
		} else if (result != DX_DDERR_WASSTILLDRAWING) {
			return 0;
		}
	}

	g_frontState.drawSurfacePitch = g_frontState.offscreenSurfacePitch;
	g_drawSurfacePtr = (uint8_t*)surfaceDesc.lpSurface;

#ifdef XVT_MODERN
	XvtRenderFrontend_Select(XVT_TARGET_FRONT_OFFSCREEN);
#endif
	return 1;
}

// FUNCTION: XVT 0x4D4EC0
int FrontendDisplay_UnlockOffscreenSurface(int saveToBackup) {
	if (g_frontState.directDraw == NULL)
		return 0;
	if (g_frontState.offscreenSurface == NULL)
		return 0;

	if (g_drawSurfacePtr != NULL && g_frontState.offscreenBackupBuffer != NULL && saveToBackup != 0) {

#ifdef XVT_MODERN
		XvtRenderFrontend_Copy(XVT_TARGET_FRONT_OFFSCREEN, XVT_TARGET_FRONT_BACKUP);
#endif
		memcpy(g_frontState.offscreenBackupBuffer, g_drawSurfacePtr,
			   (size_t)(480 * g_frontState.offscreenSurfacePitch));
	}

	g_frontState.offscreenSurface->lpVtbl->Unlock(g_frontState.offscreenSurface, NULL);
	g_frontState.drawSurfacePitch = g_frontState.backBufferPitch;
	if (g_frontState.backBufferLocked != 0)
		g_drawSurfacePtr = (uint8_t*)g_frontState.backBufferDesc.lpSurface;
	else
		g_drawSurfacePtr = FrontendDisplay_LockBackBuffer();

#ifdef XVT_MODERN
	XvtRenderFrontend_Select(XVT_TARGET_FRONT_BACK);
#endif
	return 1;
}

// FUNCTION: XVT 0x4D4F60
int FrontendDisplay_EnableOffscreenRestore(void) {
	g_frontState.offscreenRestoreEnabled = 1;
	return 1;
}

// FUNCTION: XVT 0x4D4F70
int FrontendDisplay_DisableOffscreenRestore(void) {
	g_frontState.offscreenRestoreEnabled = 0;
	return 0;
}

// FUNCTION: XVT 0x4D4F80
void FrontendDisplay_ClearOffscreenSurface(void) {
	RECT rect;
	DDBLTFX effects;
	int wasLocked;
	HRESULT result;

	if (g_frontState.directDraw == NULL || g_frontState.offscreenSurface == NULL)
		return;

	wasLocked = g_frontState.backBufferLocked;
	FrontendDisplay_UnlockBackBuffer();
	FrontendDraw_RectAssign(&rect, 0, 0, 640, 480);
	memset(&effects, 0, sizeof(effects));
	effects.dwSize = sizeof(effects);
	effects.dwFillColor = g_frontState.surfaceClearColor;
	for (;;) {
		result = g_frontState.offscreenSurface->lpVtbl->Blt(g_frontState.offscreenSurface, &rect, NULL, NULL,
															DDBLT_COLORFILL, &effects);
		if (result == DX_DD_OK)
			break;
		if (result == DX_DDERR_SURFACELOST) {
			if (FrontendDisplay_RestoreLostSurfaces() != DX_DD_OK) {
				if (wasLocked != 0)
					g_drawSurfacePtr = FrontendDisplay_LockBackBuffer();
				return;
			}
		} else if (result != DX_DDERR_WASSTILLDRAWING) {
			if (wasLocked != 0)
				g_drawSurfacePtr = FrontendDisplay_LockBackBuffer();
			return;
		}
	}

#ifdef XVT_MODERN
	XvtRenderFrontend_Clear(XVT_TARGET_FRONT_OFFSCREEN, g_frontState.surfaceClearColor);
#endif

	if (wasLocked != 0)
		g_drawSurfacePtr = FrontendDisplay_LockBackBuffer();
}

// FUNCTION: XVT 0x4D5060
int FrontendDisplay_GetPixelFormat555(void) { return g_frontState.pixelFormat555; }

// FUNCTION: XVT 0x4D5070
int FrontendDisplay_GetFrontendOrFlightDrawPitch(void) { return g_frontState.drawSurfacePitch; }

// FUNCTION: XVT 0x4D5080
int FrontendDisplay_GetBytesPerPixel(void) { return g_frontState.displayBpp / 8; }

// FUNCTION: XVT 0x4D5090
uint32_t FrontendDisplay_InitPreservingNetworkSession(void* hInstance, void* hPrevInstance, char* lpCmdLine,
													  int nShowCmd, FrontendScreenUpdateFn screenUpdateFn,
													  FrontendScreenExitFn screenExitFn,
													  int (*modeInitFn)(void), int fps, int bpp) {
	int frameRate;
	int zeroValue;

	(void)hPrevInstance;
	(void)lpCmdLine;
	(void)nShowCmd;

	FrontendDisplay_ResetGlobalStatePreservingNetworkSession();
	srand(GetTickCount());
	File_DetectGameAndCdPaths("\\wave\\PBC\\Pb1los07.wav");
	g_frontState.frontendSoundBuffers = malloc(0xB1BC);
	if (g_frontState.frontendSoundBuffers == NULL)
		return 0;
	g_frontState.frontendSoundVoices = malloc(0x90);
	if (g_frontState.frontendSoundVoices == NULL) {
		free(g_frontState.frontendSoundBuffers);
		return 0;
	}
	g_frontState.resourceTable = malloc(0x8800);
	if (g_frontState.resourceTable == NULL) {
		free(g_frontState.frontendSoundBuffers);
		free(g_frontState.frontendSoundVoices);
		return 0;
	}
	if (bpp != 8 && bpp != 16)
		return 0;

	g_frontState.clipMaxX = 639;
	g_frontState.clipMaxY = 479;
	g_frontState.presentFrameReady = 1;
	g_frontState.displayBpp = bpp;
	zeroValue = 0;
	g_frontState.pixelFormat555 = zeroValue;
	g_frontState.clipMinX = zeroValue;
	g_frontState.clipMinY = zeroValue;
	g_frontState.charWriteIdx = zeroValue;
	g_frontState.charReadIdx = zeroValue;
	g_frontState.resourceCount = zeroValue;
	frameRate = fps;
	if (frameRate <= g_frontState.resourceCount)
		frameRate = 1;
	g_frontState.frameIntervalMs = 1000 / frameRate;
	g_frontState.screenStates[0].updateFn = screenUpdateFn;
	g_frontState.screenStates[0].exitFn = screenExitFn;
	g_frontState.modeInitFn = modeInitFn;
	g_frontState.escapeCloseEnabled = 1;
	return FrontendDisplay_RunMainLoop(hInstance, hPrevInstance, lpCmdLine, nShowCmd);
}

// FUNCTION: XVT 0x4D51E0
void FrontendDisplay_ResetGlobalStatePreservingNetworkSession(void) {
	IDirectPlay2A* netDirectPlay;
	GUID netAppGuid;
	GUID netJoinedSessionGuid;
	DPID netHostPlayerId;
	DPID netGroupDplayId;
	int netIsHost;
	char netSessionName[32];
	NetPlayerInfo netRuntimeLocalPlayer;
	int playerIndex;
	DPID candidateHostPlayerId;

	netDirectPlay = g_frontState.netDirectPlay;
	netAppGuid = g_frontState.netAppGuid;
	netJoinedSessionGuid = g_frontState.netJoinedSessionGuid;
	netHostPlayerId = g_frontState.netHostPlayerId;
	netGroupDplayId = g_frontState.netGroupDplayId;
	netIsHost = g_frontState.netIsHost;
	memcpy(netSessionName, g_frontState.netSessionName, sizeof(netSessionName));
	netRuntimeLocalPlayer = g_frontState.netRuntimeLocalPlayer;

	memset(&g_frontState, 0, sizeof(g_frontState));

	g_frontState.netDirectPlay = netDirectPlay;
	g_frontState.netAppGuid = netAppGuid;
	g_frontState.netJoinedSessionGuid = netJoinedSessionGuid;
	g_frontState.netHostPlayerId = netHostPlayerId;
	g_frontState.netGroupDplayId = netGroupDplayId;
	g_frontState.netIsHost = netIsHost;
	memcpy(g_frontState.netSessionName, netSessionName, sizeof(g_frontState.netSessionName));
	g_frontState.netRuntimeLocalPlayer = netRuntimeLocalPlayer;
	g_frontState.frontendPostResetMarker = 1;
	g_frontState.netPlayerCount = 1;
	g_frontState.netPlayers[0] = netRuntimeLocalPlayer;

	if (g_frontState.netDirectPlay != NULL) {
		Net_RefreshPlayerRoster();
		for (playerIndex = 0; playerIndex < 32; ++playerIndex) {
			if (g_frontState.netPlayers[playerIndex].playerId == g_frontState.netHostPlayerId)
				break;
		}
		if (playerIndex == 32) {
			candidateHostPlayerId = g_frontState.netPlayers[playerIndex].playerId;
			for (playerIndex = 0; playerIndex < 32; ++playerIndex) {
				if (g_frontState.netPlayers[playerIndex].playerId != 0 &&
					g_frontState.netPlayers[playerIndex].readyFlag != 0 &&
					candidateHostPlayerId > g_frontState.netPlayers[playerIndex].playerId) {
					candidateHostPlayerId = g_frontState.netPlayers[playerIndex].playerId;
				}
			}
			g_frontState.netHostPlayerId = candidateHostPlayerId;
		}
	}
}

// FUNCTION: XVT 0x4D5380
int FrontendDisplay_CaptureScreenshot(void) {
	char fileName[64];
	int sequence;
	XvtFile* stream;
	int result;

	sequence = 0;
	for (;;) {
		sprintf(fileName, "frontscreen%d.bmp", sequence);
#ifdef XVT_MODERN
		stream = XvtStorage_OpenRoot(AERON_VFS_ROOT_USER, fileName, g_fileModeReadBinary);
#else
		stream = File_RawOpen(fileName, g_fileModeReadBinary);
#endif
		if (stream == NULL) {
			break;
		}
#ifdef XVT_MODERN
		File_Close(stream);
#else
		File_RawClose(stream);
#endif
		++sequence;
	}

	g_drawSurfacePtr = FrontendDisplay_LockBackBuffer();
	result = FrontImage_SaveBmpFile(fileName, g_drawSurfacePtr, 640, 480, g_frontState.backBufferPitch,
									g_frontState.displayBpp, g_frontState.pixelFormat555,
									g_frontState.displayPalette);
	FrontendDisplay_UnlockBackBuffer();
	return result;
}

// FUNCTION: XVT 0x4D5410
uint8_t* FrontendDisplay_LockSurfaceForFlight(void) { return g_drawSurfacePtr; }

// FUNCTION: XVT 0x4D5420
int FrontendDisplay_RunFrame(void) {
#ifdef XVT_MODERN
	return XvtFrontendTask_RunFrame();
#else
	enum {
		FRAME_CONTINUE = 0,
		FRAME_FINISHED = 1,
		FRAME_QUIT = 2,
		JOYSTICK_UPDATE_INTERVAL_MS = 100,
	};
	struct FrontendDisplayWin32Message message;
	uint32_t frameStart;
	uint32_t joystickUpdate;
	int frameReady;

	frameStart = GetTickCount();
	joystickUpdate = frameStart;
	frameReady = 0;
	for (;;) {
		if (g_frontState.appActive != 0) {
			uint32_t now;

			if (frameReady != 0)
				break;
			if (PeekMessageA(&message, NULL, 0, 0, 0) != 0) {
				if (GetMessageA(&message, NULL, 0, 0) == 0)
					return FRAME_QUIT;
				TranslateMessage(&message);
				DispatchMessageA(&message);
				continue;
			}
			now = GetTickCount();
			if ((int32_t)(now - frameStart) >= g_frontState.frameIntervalMs) {
				frameReady = 1;
				frameStart = now;
				FrontendDisplay_PresentFrame();
			}
			if ((int32_t)(now - joystickUpdate) >= JOYSTICK_UPDATE_INTERVAL_MS) {
				Joystick_UpdateState(0);
				Joystick_UpdateState(1);
				joystickUpdate = now;
			}
		} else {
			if (PeekMessageA(&message, NULL, 0, 0, 0) != 0) {
				if (GetMessageA(&message, NULL, 0, 0) == 0)
					return FRAME_QUIT;
				TranslateMessage(&message);
				DispatchMessageA(&message);
			}
		}
	}
	if (g_frontState.glyphScratchTtl != 0)
		memset(&g_frontState.glyphScratchBuffer, 0, sizeof(g_frontState.glyphScratchBuffer));
	Net_PumpIncomingPackets();
	if (g_frontState.screenStates[g_frontState.screenStackTop].updateFn != NULL) {
		FrontendScreenExitFn exitFn;
		int updateResult;
		GetKeyboardState(g_frontState.keyState);
		g_drawSurfacePtr = FrontendDisplay_LockBackBuffer();
		exitFn = g_frontState.screenStates[g_frontState.screenStackTop].exitFn;
		updateResult =
			g_frontState.screenStates[g_frontState.screenStackTop].updateFn(g_frontState.frameCounter);
		if (g_frontState.screenCallbacksDirty == 1 || updateResult == FRAME_FINISHED) {
			g_frontState.screenCallbacksDirty = 0;
			if (exitFn != NULL)
				exitFn(g_frontState.frameCounter);
		}
		FrontendDisplay_UnlockBackBuffer();
		if (g_frontState.cursorVisible == 1)
			FrontendCursor_Draw();
		memset(g_frontState.joystickButtonReleased[0], 0, sizeof(g_frontState.joystickButtonReleased[0]));
		memset(g_frontState.joystickButtonReleased[1], 0, sizeof(g_frontState.joystickButtonReleased[1]));
		++g_frontState.frameCounter;
		if (updateResult == FRAME_FINISHED)
			return FRAME_FINISHED;
		if (g_frontState.glyphScratchTtl != 0)
			--g_frontState.glyphScratchTtl;
		g_frontState.mouseClickLatch = 0;
		g_frontState.mouseRightClickLatch = 0;
		if (g_frontState.cdAudioCurrentTrack != 0 && GetTickCount() > g_frontState.cdAudioTrackEndTick) {
			if (g_frontState.cdAudioLoopCurrentTrack != 0) {
				CDAudio_PlayTrackFromTime(g_frontState.cdAudioCurrentTrack, 0, 0);
				return FRAME_CONTINUE;
			}
			g_frontState.cdAudioPlaybackComplete = 1;
		}
	}
	return FRAME_CONTINUE;
#endif
}

// FUNCTION: XVT 0x4D5690
void* FrontendDisplay_GetMainWindowHandle(void) { return g_frontState.hWnd; }

// FUNCTION: XVT 0x4D56A0
IDirectDraw* FrontendDisplay_GetDirectDraw(void) { return g_frontState.directDraw; }

// FUNCTION: XVT 0x4D56B0
int FrontendDisplay_ReleaseSurfacesForFlight(void) {
	FrontendDisplay_UnlockBackBuffer();
	FrontendSound_UnloadAllBuffers();
	FrontendSound_ShutdownDirectSound();
	CDAudio_CloseDevice();
	if (g_frontState.primarySurface != NULL) {
		g_frontState.primarySurface->lpVtbl->Release(g_frontState.primarySurface);
		g_frontState.primarySurface = NULL;
	}
	if (g_frontState.ddPalette != NULL) {
		g_frontState.ddPalette->lpVtbl->Release(g_frontState.ddPalette);
		g_frontState.ddPalette = NULL;
	}
	if (g_frontState.offscreenSurface != NULL) {
		g_frontState.offscreenSurface->lpVtbl->Release(g_frontState.offscreenSurface);
		g_frontState.offscreenSurface = NULL;
	}
	if ((g_optNoFullscreen != 0 || g_noPageFlip != 0) && g_frontState.backBufferSurface != NULL) {
		g_frontState.backBufferSurface->lpVtbl->Release(g_frontState.backBufferSurface);
		g_frontState.backBufferSurface = NULL;
	}
	return 1;
}

// FUNCTION: XVT 0x4D5760
int FrontendDisplay_ReinitSurfaces(void) {
	DDSURFACEDESC surfaceDesc;
	DDSCAPS attachedSurfaceCaps;
	void* windowHandle;
	HRESULT result;

	windowHandle = g_frontState.hWnd;
	if (g_optNoFullscreen != 0) {
		result = g_frontState.directDraw->lpVtbl->SetCooperativeLevel(g_frontState.directDraw, windowHandle,
																	  DDSCL_NORMAL);
		if (result != 0)
			return FrontendDisplay_ReportDirectDrawInitFailure(windowHandle, 1);
	} else {
		result = g_frontState.directDraw->lpVtbl->SetCooperativeLevel(
			g_frontState.directDraw, windowHandle, DDSCL_FULLSCREEN | DDSCL_EXCLUSIVE | DDSCL_ALLOWMODEX);
		if (result != 0)
			return FrontendDisplay_ReportDirectDrawInitFailure(windowHandle, 1);
		result = g_frontState.directDraw->lpVtbl->SetDisplayMode(g_frontState.directDraw, 640, 480,
																 g_frontState.displayBpp);
		if (result != 0)
			return FrontendDisplay_ReportDirectDrawInitFailure(windowHandle, 2);
	}

	if (g_optNoFullscreen == 0 && g_noPageFlip == 0) {
		memset(&surfaceDesc, 0, sizeof(surfaceDesc));
		surfaceDesc.dwSize = sizeof(surfaceDesc);
		surfaceDesc.dwFlags = DDSD_CAPS | DDSD_BACKBUFFERCOUNT;
		surfaceDesc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE | DDSCAPS_FLIP | DDSCAPS_COMPLEX;
		surfaceDesc.dwBackBufferCount = 1;
	} else {
		memset(&surfaceDesc, 0, sizeof(surfaceDesc));
		surfaceDesc.dwSize = sizeof(surfaceDesc);
		surfaceDesc.dwFlags = DDSD_CAPS;
		surfaceDesc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
	}
	result = g_frontState.directDraw->lpVtbl->CreateSurface(g_frontState.directDraw, &surfaceDesc,
															&g_frontState.primarySurface, NULL);
	if (result != 0)
		return FrontendDisplay_ReportDirectDrawInitFailure(windowHandle, 3);
	g_frontState.primarySurface->lpVtbl->GetSurfaceDesc(g_frontState.primarySurface, &surfaceDesc);
	g_frontState.pixelFormat555 = (surfaceDesc.ddpfPixelFormat.dwGBitMask & 0x400) == 0;

	if (g_optNoFullscreen != 0 || g_noPageFlip != 0) {
		memset(&surfaceDesc, 0, sizeof(surfaceDesc));
		surfaceDesc.dwSize = sizeof(surfaceDesc);
		surfaceDesc.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH;
		surfaceDesc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
		surfaceDesc.dwWidth = 640;
		surfaceDesc.dwHeight = 480;
		result = g_frontState.directDraw->lpVtbl->CreateSurface(g_frontState.directDraw, &surfaceDesc,
																&g_frontState.backBufferSurface, NULL);
	} else {
		attachedSurfaceCaps.dwCaps = DDSCAPS_BACKBUFFER;
		result = g_frontState.primarySurface->lpVtbl->GetAttachedSurface(
			g_frontState.primarySurface, &attachedSurfaceCaps, &g_frontState.backBufferSurface);
	}
	if (result != 0)
		return FrontendDisplay_ReportDirectDrawInitFailure(windowHandle, 4);
	g_frontState.backBufferSurface->lpVtbl->GetSurfaceDesc(g_frontState.backBufferSurface, &surfaceDesc);
	g_frontState.backBufferPitch = surfaceDesc.lPitch;

	memset(&surfaceDesc, 0, sizeof(surfaceDesc));
	surfaceDesc.dwSize = sizeof(surfaceDesc);
	surfaceDesc.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH;
	if (g_optNoFullscreen != 0 || g_noPageFlip != 0)
		surfaceDesc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
	else
		surfaceDesc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN;
	surfaceDesc.dwWidth = 640;
	surfaceDesc.dwHeight = 480;
	result = g_frontState.directDraw->lpVtbl->CreateSurface(g_frontState.directDraw, &surfaceDesc,
															&g_frontState.offscreenSurface, NULL);
	if (result != 0)
		return FrontendDisplay_ReportDirectDrawInitFailure(windowHandle, 5);
	g_frontState.offscreenSurface->lpVtbl->GetSurfaceDesc(g_frontState.offscreenSurface, &surfaceDesc);
	g_frontState.offscreenSurfacePitch = surfaceDesc.lPitch;

	g_frontState.ddPalette = FrontendDisplay_LoadPalette(g_frontState.directDraw, NULL);
	if (g_optNoFullscreen == 0 && g_noPageFlip == 0) {
		if (g_frontState.ddPalette != NULL && g_frontState.displayBpp == 8)
			g_frontState.primarySurface->lpVtbl->SetPalette(g_frontState.primarySurface,
															g_frontState.ddPalette);
	} else if (g_frontState.ddPalette != NULL && g_frontState.displayBpp == 8) {
		g_frontState.primarySurface->lpVtbl->SetPalette(g_frontState.primarySurface, g_frontState.ddPalette);
		FrontendDisplay_SetPalette();
	}

	g_frontState.textColorCodes[0] = 0xFFFF;
	g_frontState.textColorCodes[1] = FrontendDisplay_PackRGB(0, 0xFF, 0);
	g_frontState.textColorCodes[2] = FrontendDisplay_PackRGB(0xFF, 0, 0);
	g_frontState.textColorCodes[3] = FrontendDisplay_PackRGB(0xFF, 0xFF, 0);
	g_frontState.textColorCodes[4] = FrontendDisplay_PackRGB(0x32, 0x32, 0xFF);
	g_frontState.textColorCodes[5] = FrontendDisplay_PackRGB(0x80, 0x80, 0xFF);
#ifdef XVT_MODERN
	XvtPresentation_WarpClassic(0, 0);
#else
	SetCursorPos(0, 0);
#endif

#ifdef XVT_MODERN
	XvtRenderFrontend_Reset();
#endif
	FrontendDisplay_ClearOffscreenSurface();
	FrontendDisplay_ClearBackBuffer();
	FrontendDisplay_PresentFrame();
	if (FrontendSound_InitDirectSound(g_frontState.hWnd) == 0)
		FrontendDisplay_ShowGameMessageBox("Sound not available.");
	g_drawSurfacePtr = FrontendDisplay_LockBackBuffer();
	if (g_frontState.offscreenBackupBuffer == NULL) {
		g_frontState.offscreenBackupBuffer = malloc(480 * g_frontState.offscreenSurfacePitch);
		if (g_frontState.offscreenBackupBuffer != NULL)
			memset(g_frontState.offscreenBackupBuffer, 0, 480 * g_frontState.offscreenSurfacePitch);
	}
	return 1;
}

// FUNCTION: XVT 0x4D5B70
void FrontendDisplay_SetWndProcMode(uint8_t mode) { g_frontState.frontendDisplayWndProcMode = mode; }

// FUNCTION: XVT 0x4D5B80
int FrontendDisplay_GetWndProcMode(void) { return g_frontState.frontendDisplayWndProcMode; }

// FUNCTION: XVT 0x4D5B90
int Win32_CheckSingleInstance(void) {
#ifdef XVT_MODERN
	return 0;
#else
	void* window = FindWindowA(g_windowName, g_windowName);
	if (window != NULL) {
		ShowWindowAsync(window, 9);
		return 1;
	}
	return 0;
#endif
}

// FUNCTION: XVT 0x4D5BC0
void FrontendDisplay_FlipDirectDrawToGDISurface(void) {
	if (g_frontState.directDraw != NULL) {
		g_frontState.directDraw->lpVtbl->FlipToGDISurface(g_frontState.directDraw);
	}
}

// FUNCTION: XVT 0x4D5C20
const DxGuid* FrontendDisplay_LoadDriverGuid(void) {
	XvtFile* stream;
	int readSucceeded;

	stream = File_Open("video.cfg", "rb");
	if (stream == NULL) {
		return NULL;
	}
	readSucceeded =
		File_ReadCount(stream, &g_configuredDirectDrawDriverGuid, sizeof(g_configuredDirectDrawDriverGuid));
	File_Close(stream);
	return readSucceeded != 0 ? &g_configuredDirectDrawDriverGuid : NULL;
}

// FUNCTION: XVT 0x4D5C70
int FrontendDisplay_DrawGdiTextOnSecondaryDisplay(const RECT* unused, const char* text,
												  const char* overlayText) {
#ifdef XVT_MODERN
	(void)unused;
	(void)text;
	(void)overlayText;
	return 0;
#else
	void* dc;
	void* font;
	void* previousObject;
	RECT rect;

	(void)unused;
	if (g_frontState.secondaryDirectDrawActive == 0) {
		return 0;
	}
	dc = CreateDCA("DISPLAY", NULL, NULL, NULL);
	if (dc == NULL) {
		return 0;
	}
	FrontendDraw_RectAssign(&rect, 0, 0, GetSystemMetrics(0), GetSystemMetrics(1));
	font = CreateFontA(-12, 0, 0, 0, 400, 0, 0, 0, 0, 4, 0, 3, 0x12, "times new roman");
	if (font == NULL) {
		DeleteDC(dc);
		return 0;
	}
	previousObject = SelectObject(dc, font);
	SetMapMode(dc, 1);
	SetTextCharacterExtra(dc, 0);
	SetTextColor(dc, 0xFFFFFF);
	SetBkColor(dc, 0);
	SetBkMode(dc, 2);
	DrawTextA(dc, text, strlen(text), &rect, 0x25);
	if (overlayText != NULL) {
		SetTextColor(dc, 0xFF);
		DrawTextA(dc, overlayText, -1, &rect, 0x21);
		DrawTextA(dc, overlayText, -1, &rect, 0x29);
	}
	SelectObject(dc, previousObject);
	DeleteObject(font);
	DeleteDC(dc);
	return 1;
#endif
}

// FUNCTION: XVT 0x4D5DC0
int FrontendDisplay_ClearSecondaryDisplayGdi(const RECT* unused) {
#ifdef XVT_MODERN
	(void)unused;
	return 0;
#else
	void* dc;
	RECT rect;

	(void)unused;

	if (g_frontState.secondaryDirectDrawActive == 0)
		return 0;

	dc = CreateDCA("DISPLAY", NULL, NULL, NULL);
	if (dc == NULL)
		return 0;

	SelectObject(dc, GetStockObject(4));
	FrontendDraw_RectAssign(&rect, 0, 0, GetSystemMetrics(0), GetSystemMetrics(1));
	Rectangle(dc, rect.left, rect.top, rect.right, rect.bottom);
	DeleteDC(dc);
	return 1;
#endif
}

// FUNCTION: XVT 0x4D5E60
int FrontendDisplay_IsSecondaryDirectDrawActive(void) { return g_frontState.secondaryDirectDrawActive; }

// FUNCTION: XVT 0x4D6CA0
void FrontendDisplay_SetPalette(void) {
	if (g_optNoFullscreen != 0) {
		return;
	}
	g_frontState.ddPalette->lpVtbl->SetEntries(g_frontState.ddPalette, 0, 0, 256,
											   g_frontState.displayPalette);
}

// FUNCTION: XVT 0x4D6E30
int FrontendDisplay_PackRGB(uint8_t r, uint8_t g, uint8_t b) {
	int index;
	unsigned int bestDistance;
	int bestIndex;
	FrontendPaletteEntry* entry;

	switch (g_frontState.displayBpp) {
		case 8:
			bestDistance = 0x7FFFFFFFu;
			bestIndex = 1;
			for (index = 1; index < 256; ++index) {
				int redDelta;
				int greenDelta;
				int blueDelta;
				unsigned int distance;

				entry = &g_frontState.displayPalette[index];
				redDelta = (int)entry->red - r;
				if (redDelta < 0) {
					redDelta = -redDelta;
				}
				greenDelta = (int)entry->green - g;
				if (greenDelta < 0) {
					greenDelta = -greenDelta;
				}
				blueDelta = (int)entry->blue - b;
				if (blueDelta < 0) {
					blueDelta = -blueDelta;
				}
				distance = g_colorDistLUT[blueDelta];
				distance += g_colorDistLUT[redDelta];
				distance += g_colorDistLUT[greenDelta];
				if (distance == 0) {
					return index;
				}
				if (distance < bestDistance) {
					bestDistance = distance;
					bestIndex = index;
				}
			}
			return bestIndex;
		case 16: {
			uint8_t redComponent;
			uint8_t greenComponent;
			uint8_t blueComponent;

			if (g_frontState.pixelFormat555 != 0) {
				redComponent = r >> 3;
				greenComponent = g >> 3;
				blueComponent = b >> 3;
				return 32 * (greenComponent + 32 * redComponent) + blueComponent;
			}
			redComponent = r >> 3;
			greenComponent = g >> 2;
			blueComponent = b >> 3;
			return 32 * (greenComponent + (redComponent << 6)) + blueComponent;
		}
		default:
			return g_frontState.displayBpp;
	}
}

// FUNCTION: XVT 0x4DC9B0
int FrontendDisplay_SaveBackBuffer(void) {
	int wasBackBufferLocked;
	uint8_t* source;
	uint8_t* destination;
	int row;

#ifdef XVT_MODERN
	XvtRenderFrontend_Suppress(1);
#endif
	wasBackBufferLocked = g_frontState.backBufferLocked;
	source = FrontendDisplay_LockBackBuffer();
	FrontendDisplay_LockOffscreenSurface();
	destination = g_drawSurfacePtr;
	for (row = 480; row != 0; --row) {
		memcpy(destination, source, (size_t)(80 * (g_frontState.displayBpp & 0xFFFFFFF8)));
		destination += g_frontState.offscreenSurfacePitch;
		source += g_frontState.backBufferPitch;
	}
	FrontendDisplay_UnlockOffscreenSurface(0);
	if (wasBackBufferLocked == 0)
		FrontendDisplay_UnlockBackBuffer();

#ifdef XVT_MODERN
	XvtRenderFrontend_Suppress(0);
	XvtRenderFrontend_Copy(XVT_TARGET_FRONT_BACK, XVT_TARGET_FRONT_OFFSCREEN);
	XvtRenderFrontend_Select(XVT_TARGET_FRONT_BACK);
#endif
	return 1;
}

// FUNCTION: XVT 0x4DCA20
int FrontendDisplay_RestoreBackBuffer(void) {
	int wasBackBufferLocked;
	uint8_t* destination;
	uint8_t* source;
	int row;

#ifdef XVT_MODERN
	XvtRenderFrontend_Suppress(1);
#endif
	wasBackBufferLocked = g_frontState.backBufferLocked;
	destination = FrontendDisplay_LockBackBuffer();
	FrontendDisplay_LockOffscreenSurface();
	source = g_drawSurfacePtr;
	for (row = 480; row != 0; --row) {
		memcpy(destination, source, (size_t)(80 * (g_frontState.displayBpp & 0xFFFFFFF8)));
		source += g_frontState.offscreenSurfacePitch;
		destination += g_frontState.backBufferPitch;
	}
	FrontendDisplay_UnlockOffscreenSurface(0);
	if (wasBackBufferLocked == 0)
		FrontendDisplay_UnlockBackBuffer();

#ifdef XVT_MODERN
	XvtRenderFrontend_Suppress(0);
	XvtRenderFrontend_Copy(XVT_TARGET_FRONT_OFFSCREEN, XVT_TARGET_FRONT_BACK);
	XvtRenderFrontend_Select(XVT_TARGET_FRONT_BACK);
#endif
	return 1;
}

// FUNCTION: XVT 0x4F0E30
IDirectDrawPalette* FrontendDisplay_LoadPalette(IDirectDraw* pDD, const char* lpName) {
	IDirectDrawPalette* palette;
	FrontendDisplayBmpFileHeader fileHeader;
	FrontendDisplayBmpInfoHeader infoHeader;
	FrontendPaletteEntry entries[256];
	FrontendPaletteEntry* entry;
	int index;
	int colorCount;

	entry = entries;
	index = 0;
	do {
		entry->red = (uint8_t)(255 * ((index & 0xE0) >> 5) / 7);
		entry->green = (uint8_t)(255 * ((index & 0x1C) >> 2) / 7);
		entry->blue = (uint8_t)(255 * (index & 3) / 3);
		entry->flags = 0;
		++entry;
		++index;
	} while (entry < entries + 256);

	if (lpName != NULL) {
#ifndef XVT_MODERN
		void* resourceInfo;

		resourceInfo = FindResourceA(NULL, lpName, 2);
		if (resourceInfo != NULL) {
			uint8_t* resourceData;
			uint8_t* sourceEntry;
			uint8_t color;
			uint16_t bitsPerPixel;

			resourceData = LockResource(LoadResource(NULL, resourceInfo));
			sourceEntry = resourceData + *(uint32_t*)resourceData;
			if (resourceData != NULL && *(uint32_t*)resourceData >= sizeof(FrontendDisplayBmpInfoHeader) &&
				(bitsPerPixel = *(uint16_t*)(resourceData + 14)) <= 8) {
				colorCount = *(uint32_t*)(resourceData + 32);
				if (colorCount == 0)
					colorCount = 1 << bitsPerPixel;
			} else {
				colorCount = 0;
			}
			if (colorCount > 0) {
				entry = entries;
				do {
					color = sourceEntry[2];
					entry->red = color;
					color = sourceEntry[1];
					entry->green = color;
					color = sourceEntry[0];
					entry->blue = color;
					entry->flags = 0;
					++entry;
					sourceEntry += 4;
					--colorCount;
				} while (colorCount != 0);
			}
		} else {
			int file;

			file = _lopen(lpName, 0);
			if (file != -1) {
				_lread(file, &fileHeader, sizeof(fileHeader));
				_lread(file, &infoHeader, sizeof(infoHeader));
				_lread(file, entries, sizeof(entries));
				_lclose(file);
				if (infoHeader.headerSize == sizeof(infoHeader)) {
					if (infoHeader.bitsPerPixel <= 8) {
						colorCount = infoHeader.colorsUsed;
						if (colorCount == 0)
							colorCount = 1 << infoHeader.bitsPerPixel;
					} else {
						colorCount = 0;
					}
				} else {
					colorCount = 0;
				}
				for (index = 0; index < colorCount; ++index) {
					uint8_t red;

					red = entries[index].red;
					entries[index].red = entries[index].blue;
					entries[index].blue = red;
				}
			}
		}
#else
		XvtFile* stream;

		stream = File_Open(lpName, "rb");
		if (stream != NULL) {
			File_ReadCount(stream, &fileHeader, sizeof(fileHeader));
			File_ReadCount(stream, &infoHeader, sizeof(infoHeader));
			File_ReadCount(stream, entries, sizeof(entries));
			File_Close(stream);
			if (infoHeader.headerSize == sizeof(infoHeader)) {
				if (infoHeader.bitsPerPixel <= 8) {
					colorCount = infoHeader.colorsUsed;
					if (colorCount == 0)
						colorCount = 1 << infoHeader.bitsPerPixel;
				} else {
					colorCount = 0;
				}
			} else {
				colorCount = 0;
			}
			for (index = 0; index < colorCount; ++index) {
				uint8_t red;

				red = entries[index].red;
				entries[index].red = entries[index].blue;
				entries[index].blue = red;
			}
		}
#endif
	}

#ifdef XVT_MODERN
	palette = NULL;
#endif
	pDD->lpVtbl->CreatePalette(pDD, DDPCAPS_8BIT | DDPCAPS_ALLOW256, entries, &palette, NULL);
	if (palette != NULL) {
		memcpy(g_frontState.displayPalette, entries, sizeof(g_frontState.displayPalette));
		g_frontState.displayPalette[0].red = 0;
		g_frontState.displayPalette[0].green = 0;
		g_frontState.displayPalette[0].blue = 0;
		g_frontState.displayPalette[255].red = 255;
		g_frontState.displayPalette[255].green = 255;
		g_frontState.displayPalette[255].blue = 255;
	}
	return palette;
}

// FUNCTION: XVT 0x4F1070
uint32_t FrontendDisplay_ConvertColorRefToSurfacePixel(IDirectDrawSurface* surface, uint32_t color) {
#ifdef XVT_MODERN
	uint32_t surfacePixel;
	uint32_t red;
	uint32_t green;
	uint32_t blue;
	uint32_t redMask;
	uint32_t greenMask;
	uint32_t blueMask;
	uint32_t redShift;
	uint32_t greenShift;
	uint32_t blueShift;
	uint32_t redMax;
	uint32_t greenMax;
	uint32_t blueMax;
	HRESULT lockResult;
	DDSURFACEDESC surfaceDesc;

	surfacePixel = UINT32_MAX;
	surfaceDesc.dwSize = sizeof(surfaceDesc);
	do {
		lockResult = surface->lpVtbl->Lock(surface, NULL, &surfaceDesc, 0, NULL);
	} while (lockResult == DX_DDERR_WASSTILLDRAWING);
	if (lockResult != 0) {
		return surfacePixel;
	}
	if (color == UINT32_MAX) {
		surfacePixel = *(uint32_t*)surfaceDesc.lpSurface;
	} else {
		red = color & 0xFF;
		green = (color >> 8) & 0xFF;
		blue = (color >> 16) & 0xFF;
		redMask = surfaceDesc.ddpfPixelFormat.dwRBitMask;
		greenMask = surfaceDesc.ddpfPixelFormat.dwGBitMask;
		blueMask = surfaceDesc.ddpfPixelFormat.dwBBitMask;
		redShift = 0;
		greenShift = 0;
		blueShift = 0;
		while (redMask != 0 && ((redMask >> redShift) & 1) == 0) {
			++redShift;
		}
		while (greenMask != 0 && ((greenMask >> greenShift) & 1) == 0) {
			++greenShift;
		}
		while (blueMask != 0 && ((blueMask >> blueShift) & 1) == 0) {
			++blueShift;
		}
		redMax = redMask >> redShift;
		greenMax = greenMask >> greenShift;
		blueMax = blueMask >> blueShift;
		surfacePixel = (((red * redMax / 255) << redShift) & redMask) |
					   (((green * greenMax / 255) << greenShift) & greenMask) |
					   (((blue * blueMax / 255) << blueShift) & blueMask);
	}
	if (surfaceDesc.ddpfPixelFormat.dwRGBBitCount < 32) {
		surfacePixel &= (1u << surfaceDesc.ddpfPixelFormat.dwRGBBitCount) - 1;
	}
	surface->lpVtbl->Unlock(surface, NULL);
	return surfacePixel;
#else
	uint32_t surfacePixel;
	uint32_t originalColor;
	HRESULT lockResult;
	void* dc;
	DDSURFACEDESC surfaceDesc;

	surfacePixel = UINT32_MAX;
	if (color != UINT32_MAX && ((FrontendDisplaySurfaceGetDcFunc)surface->lpVtbl->GetDC)(surface, &dc) == 0) {
		originalColor = GetPixel(dc, 0, 0);
		SetPixel(dc, 0, 0, color);
		((FrontendDisplaySurfaceReleaseDcFunc)surface->lpVtbl->ReleaseDC)(surface, dc);
	} else {
		originalColor = surfaceDesc.dwSize;
	}
	surfaceDesc.dwSize = sizeof(surfaceDesc);
	do {
		lockResult = surface->lpVtbl->Lock(surface, NULL, &surfaceDesc, 0, NULL);
	} while (lockResult == DX_DDERR_WASSTILLDRAWING);
	if (lockResult == 0) {
		surfacePixel = *(uint32_t*)surfaceDesc.lpSurface &
					   ((1u << (int8_t)surfaceDesc.ddpfPixelFormat.dwRGBBitCount) - 1);
		surface->lpVtbl->Unlock(surface, NULL);
	}
	if (color != UINT32_MAX && ((FrontendDisplaySurfaceGetDcFunc)surface->lpVtbl->GetDC)(surface, &dc) == 0) {
		SetPixel(dc, 0, 0, originalColor);
		((FrontendDisplaySurfaceReleaseDcFunc)surface->lpVtbl->ReleaseDC)(surface, dc);
	}
	return surfacePixel;
#endif
}

// FUNCTION: XVT 0x4F1160
HRESULT FrontendDisplay_SetSurfaceColorKey(IDirectDrawSurface* surface, uint32_t color) {
	DDCOLORKEY colorKey;

	colorKey.dwColorSpaceLowValue = FrontendDisplay_ConvertColorRefToSurfacePixel(surface, color);
	colorKey.dwColorSpaceHighValue = colorKey.dwColorSpaceLowValue;
	return surface->lpVtbl->SetColorKey(surface, DDCKEY_SRCBLT, &colorKey);
}
