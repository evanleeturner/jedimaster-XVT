#include "xvt/render/renderer.h"
#include "xvt/flight/flight_display.h"
#include "xvt/math/math.h"
#include "xvt/render/render_scene.h"
#include "xvt/render/render_texture.h"
#include "xvt/render/std3d.h"
#include "xvt/util/debug_console.h"
#include <string.h>

// GLOBAL: XVT 0x5233A4
int g_forcedLodLevel = 0;
// GLOBAL: XVT 0x5233A8
float g_lodDistanceScale = 1.0f;
// GLOBAL: XVT 0x5233AC
int g_mipmappingEnabled = 1;
// GLOBAL: XVT 0x5233B0
float g_mipLodScale = 1.0f;
// GLOBAL: XVT 0x5233B4
int g_ditheringEnabled = 1;
// GLOBAL: XVT 0x5233B8
int g_localLightsLevel = 1;
// GLOBAL: XVT 0x5233BC
int g_specularEnabled = 1;
// GLOBAL: XVT 0x5233C0
int g_dirLightingEnabled = 1;
// GLOBAL: XVT 0x5233C4
int g_keepFullResTextures = 1;
// GLOBAL: XVT 0x66DDD4
IDirectDraw* g_flightDirectDraw;
// GLOBAL: XVT 0x5233C8
uint8_t g_flightColorEscapeBypassChar = 0xfb;
// GLOBAL: XVT 0x9A7A30
uint8_t g_palettePackedMode = 0;
// GLOBAL: XVT 0x5233F8
uint8_t g_flightGraphicsDetailPreset = 3;
// GLOBAL: XVT 0xA08290
uint16_t g_flightViewportMode = 0;
// GLOBAL: XVT 0x527E90
int g_useHardware3D;
// GLOBAL: XVT 0x527E94
int g_bilinearEnabled = 1;
// GLOBAL: XVT 0x527EC8
int g_loadingModel;
// GLOBAL: XVT 0x5233E0
int g_surfaceWidth = 640;
// GLOBAL: XVT 0x5233E8
int g_surfaceHeight = 480;
// GLOBAL: XVT 0x527ECC
void* g_surfacePixels = (void*)0xA0000;
// GLOBAL: XVT 0x66DDC4
int width;
// GLOBAL: XVT 0x66DDE0
int height;
// GLOBAL: XVT 0x9A7BAC
unsigned int g_screenWidth = 0;
// GLOBAL: XVT 0x9A806C
void* g_flightOffscreenBuffer = 0;
// GLOBAL: XVT 0x9A7B60
int g_flightVpY = 0;
// GLOBAL: XVT 0x9A1FE4
uint16_t g_flightVpHeight = 0;
// GLOBAL: XVT 0x9A20A2
uint16_t g_flightVpCenterY = 0;
// GLOBAL: XVT 0x9A8C14
unsigned int g_screenHeight = 0;
// GLOBAL: XVT 0x9A8E3C
int g_flightVpX = 0;
// GLOBAL: XVT 0x9D7680
int g_projOffsetY;
// GLOBAL: XVT 0xA081EC
uint32_t g_projScaleInt = 0;
// GLOBAL: XVT 0x9D682C
uint16_t g_flightVpWidth = 0;
// GLOBAL: XVT 0x9D682E
uint16_t g_renderTargetComponentIdx = 0;
// GLOBAL: XVT 0x9CD272
uint16_t g_flightVpMaxX = 0;
// GLOBAL: XVT 0x9CC450
uint16_t g_renderObjectRef = 0;
// GLOBAL: XVT 0xA0810A
uint16_t g_renderObjectRefFlags = 0;
// GLOBAL: XVT 0x9D1260
uint16_t g_flightVpMaxY = 0;
// GLOBAL: XVT 0x9D1300
void (*g_flightFillClipRectFn)(void) = 0;
// GLOBAL: XVT 0x9D8C08
int g_surfacePitch = 0;
// GLOBAL: XVT 0x9ED234
unsigned int g_flightVpBaseOffset = 0;
// GLOBAL: XVT 0x9D130C
FlightBlitSpriteFadedFn g_flightBlitSpriteFadedFn = 0;
// GLOBAL: XVT 0x9D80C4
FlightBlitSpriteFn g_flightBlitSpriteFn = 0;
// GLOBAL: XVT 0x9A6FE4
FlightDrawCharFn g_flightDrawCharFn = 0;
// GLOBAL: XVT 0x9D77C0
void (*g_flightInitLineBufferFn)(void) = 0;
// GLOBAL: XVT 0x9A8D44
void (*g_flightRenderTransitionHook)(void) = 0;
// GLOBAL: XVT 0x9A8C20
void (*g_flightResetPaletteFn)(void) = 0;
// GLOBAL: XVT 0x9D77F4
FlightSetPaletteRangeFn g_flightSetPaletteRangeFn = 0;
// GLOBAL: XVT 0xA0810C
FlightPaletteFn g_flightGetPaletteFn = 0;
// GLOBAL: XVT 0x9FE7E0
FlightPaletteFn g_flightSetPaletteFn = 0;
// GLOBAL: XVT 0x9A8C30
FlightComputePixelOffsetFn g_flightComputePixelOffsetFn = 0;
// GLOBAL: XVT 0x9ECA20
FlightFillRectClippedFn g_flightFillRectClippedFn = 0;
// GLOBAL: XVT 0x9D8B6C
FlightScreenRectFn g_flightSaveScreenRectFn = 0;
// GLOBAL: XVT 0xA07CE0
FlightScreenRectFn g_flightRestoreScreenRectFn = 0;
// GLOBAL: XVT 0x9A20A8
FlightDrawPointArrayFn g_flightDrawPointArrayFn = 0;
// GLOBAL: XVT 0x9D12E4
FlightDrawPointArrayFn g_flightDrawPointArrayMaskedFn = 0;
// GLOBAL: XVT 0x9E95F4
FlightDrawPixelFn g_flightDrawPixelFn = 0;
// GLOBAL: XVT 0x9EC46C
void (*g_flightSaveDrawCursorFn)(void) = 0;
// GLOBAL: XVT 0x9A1FEC
void (*g_flightRestoreCursorFn)(void) = 0;
// GLOBAL: XVT 0x9CC454
FlightDrawLineFn g_flightDrawLineFn = 0;
// GLOBAL: XVT 0xA0048C
int g_curMatR0_Y = 0;
// GLOBAL: XVT 0x9D77AC
int g_objViewMat_R0_X = 0;
// GLOBAL: XVT 0x9D77B8
int g_objViewMat_R0_Y = 0;
// GLOBAL: XVT 0x9D77B4
int g_objViewMat_R0_Z = 0;
// GLOBAL: XVT 0x9D77B0
int g_objViewMat_R1_X = 0;
// GLOBAL: XVT 0x9D7690
int g_objViewMat_R1_Y = 0;
// GLOBAL: XVT 0x9D768C
int g_objViewMat_R1_Z = 0;
// GLOBAL: XVT 0x9D7688
int g_objViewMat_R2_X = 0;
// GLOBAL: XVT 0x9D77A4
int g_objViewMat_R2_Y = 0;
// GLOBAL: XVT 0x9D77A0
int g_objViewMat_R2_Z = 0;
// GLOBAL: XVT 0xA081E0
int g_objectLightDirectionX = 0;
// GLOBAL: XVT 0xA081E4
int g_objectLightDirectionY = 0;
// GLOBAL: XVT 0xA08150
int g_objectLightDirectionZ = 0;
// GLOBAL: XVT 0xA00494
int g_curMatR0_Z = 0;
// GLOBAL: XVT 0xA0049C
int g_curMatR0_X = 0;
// GLOBAL: XVT 0xA004AC
int g_curMatR1_Y = 0;
// GLOBAL: XVT 0xA004B4
int g_curMatR1_Z = 0;
// GLOBAL: XVT 0xA004D0
int g_curMatR1_X = 0;
// GLOBAL: XVT 0xA07C68
int g_curMatR2_X = 0;
// GLOBAL: XVT 0xA07C6C
int g_curMatR2_Y = 0;
// GLOBAL: XVT 0xA07CC4
int g_curMatR2_Z = 0;
// GLOBAL: XVT 0x9A8D84
int g_fviewMoveY_Q15 = 0;
// GLOBAL: XVT 0x9A8D88
int g_fviewMoveZ_Q15 = 0;
// GLOBAL: XVT 0x9A8DA4
int g_fviewMoveX_Q15 = 0;

// FLAGS: /O2 /G5
// FUNCTION: XVT 0x4079D0
IDirectDraw* Renderer_GetDirectDraw(void) { return g_flightDirectDraw; }

// FUNCTION: XVT 0x408170
void Renderer_InitD3DDevice(void) {
	Std3DDeviceCaps deviceCaps;
	DDSURFACEDESC zBufferDesc;
	HRESULT result;
	unsigned int deviceIndex;

	g_renderTextureCacheCursor = -1;
	std3D_InitRenderTargetDesc((unsigned int)width, (unsigned int)height, g_surfacePitch);
	std3D_SetRenderSurface(g_flightBackBuffer);
	std3D_SetColorOverlayParams(0.0f, 0.0f, 0.0f, 0);
	std3D_Startup();

	memset(&deviceCaps, 0, sizeof(deviceCaps));
	deviceCaps.bHardware = 1;
	deviceCaps.bTexturePerspective = 1;
	deviceCaps.bHasZBuffer = 1;
	deviceCaps.colorModelFlags = 2;
	deviceIndex = std3D_SelectBestDevice(&deviceCaps);
	memcpy(&deviceCaps, &g_std3DDevices[deviceIndex].caps, sizeof(deviceCaps));

	if (deviceCaps.bHasZBuffer != 0 && deviceCaps.bTexturePerspective != 0 && deviceCaps.bHardware != 0) {
		std3D_CreateDevice(deviceIndex, 1);
		memset(&zBufferDesc, 0, sizeof(zBufferDesc));
		zBufferDesc.dwSize = sizeof(zBufferDesc);
		zBufferDesc.dwFlags = DDSD_CAPS;
		zBufferDesc.ddsCaps.dwCaps = DDSCAPS_ZBUFFER;
		result = g_flightBackBuffer->lpVtbl->GetAttachedSurface(g_flightBackBuffer, &zBufferDesc.ddsCaps,
																&g_std3DZBufferSurface);
		if (result != 0) {
			DebugPrintf("ERROR(%x)! Failed to get HW Zbuffer\n", result);
			std3D_Close();
			std3D_Shutdown();
			g_useHardware3D = 0;
			Math_SetFpuSinglePrecisionMode();
			return;
		}
		Math_SetFpuSinglePrecisionMode();
	} else {
		DebugPrintf("Essential Hardware Feature NOT Supported: Z:%d Tex:%d HW:%d\n", deviceCaps.bHasZBuffer,
					deviceCaps.bTexturePerspective, deviceCaps.bHardware);
		std3D_Shutdown();
		g_useHardware3D = 0;
		Math_SetFpuSinglePrecisionMode();
	}
}
