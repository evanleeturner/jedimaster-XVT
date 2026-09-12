#include "xvt/render/std3d.h"
#include "xvt/render/renderer.h"
#include "xvt/util/debug_console.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct Std3DUnknown Std3DUnknown;

typedef struct Std3DUnknownVtbl {
	void* queryInterface;
	void* addRef;
	uint32_t(AERON_DXAPI* release)(Std3DUnknown* self);
} Std3DUnknownVtbl;

struct Std3DUnknown {
	const Std3DUnknownVtbl* lpVtbl;
};

// GLOBAL: XVT 0x528C50
unsigned int g_std3DCapFlags;
// GLOBAL: XVT 0x528C54
Std3DRenderStateFlags g_d3dStateFlags = 0;
// GLOBAL: XVT 0x528C58
unsigned int g_std3DNumDevices = 0;
// GLOBAL: XVT 0xA90A68
unsigned int g_std3DCurDeviceIdx = 0;
// GLOBAL: XVT 0x528C5C
Std3DDevice* g_pStd3DCurDevice = 0;
// GLOBAL: XVT 0x528C60
int g_std3DNumTextureFormats = 0;
// GLOBAL: XVT 0x528C64
Std3DTexFmt* g_pFmtRGB565;
// GLOBAL: XVT 0xA90B58
int g_fmtIdxRGB565 = 0;
// GLOBAL: XVT 0x528C68
Std3DTexFmt* g_pFmtRGBA1555 = 0;
// GLOBAL: XVT 0xA91B88
int g_fmtIdxRGBA1555 = 0;
// GLOBAL: XVT 0x528C6C
Std3DTexFmt* g_pFmtRGBA4444 = 0;
// GLOBAL: XVT 0xA90A64
int g_fmtIdxRGBA4444 = 0;
// GLOBAL: XVT 0x528CB8
static int g_std3DStartupDone = 0;
// GLOBAL: XVT 0x528CB0
int g_std3DZBufferEnabled = 1;
// GLOBAL: XVT 0x528CB4
IDirectDraw* g_std3DDirectDraw = 0;
// GLOBAL: XVT 0xA90C00
Std3DTexFmt g_std3DTextureFormats[8] = { 0 };
// GLOBAL: XVT 0x529168
static const char g_std3DShutdownSucceededMessage[] = "Shutdown Succeeded.\n";
// GLOBAL: XVT 0x5293B0
static const char g_std3DUnknownErrorMessage[] = "Unknown Error";
// GLOBAL: XVT 0x529470
static const char g_std3DBeginSceneErrorFormat[] = "Error %s beginning scene.\n";
// GLOBAL: XVT 0x52948C
static const char g_std3DEndSceneErrorFormat[] = "Error %s ending scene.\n";
// GLOBAL: XVT 0x5294A4
static const char g_std3DLockExecuteBufferErrorFormat[] = "Error %s locking D3D Execute buffer.\n";
// GLOBAL: XVT 0x528CC0
const Std3DErrorStringEntry g_std3DErrorStringTable[121] = {
	{ 0, "D3D_OK" },
	{ -2005531972, "D3DERR_BADMAJORVERSION" },
	{ -2005531971, "D3DERR_BADMINORVERSION" },
	{ -2005531961, "D3DERR_EXECUTE_DESTROY_FAILED" },
	{ -2005531960, "D3DERR_EXECUTE_LOCK_FAILED" },
	{ -2005531959, "D3DERR_EXECUTE_UNLOCK_FAILED" },
	{ -2005531958, "D3DERR_EXECUTE_LOCKED" },
	{ -2005531957, "D3DERR_EXECUTE_NOT_LOCKED" },
	{ -2005531955, "D3DERR_EXECUTE_CLIPPED_FAILED" },
	{ -2005531951, "D3DERR_TEXTURE_CREATE_FAILED" },
	{ -2005531950, "D3DERR_TEXTURE_DESTROY_FAILED" },
	{ -2005531949, "D3DERR_TEXTURE_LOCK_FAILED" },
	{ -2005531948, "D3DERR_TEXTURE_UNLOCK_FAILED" },
	{ -2005531947, "D3DERR_TEXTURE_LOAD_FAILED" },
	{ -2005531946, "D3DERR_TEXTURE_SWAP_FAILED" },
	{ -2005531945, "D3DERR_TEXTURE_LOCKED" },
	{ -2005531944, "D3DERR_TEXTURE_NOT_LOCKED" },
	{ -2005531943, "D3DERR_TEXTURE_GETSURF_FAILED" },
	{ -2005531941, "D3DERR_MATRIX_DESTROY_FAILED" },
	{ -2005531940, "D3DERR_MATRIX_SETDATA_FAILED" },
	{ -2005531939, "D3DERR_MATRIX_GETDATA_FAILED" },
	{ -2005531938, "D3DERR_SETVIEWPORTDATA_FAILED" },
	{ -2005531931, "D3DERR_MATERIAL_DESTROY_FAILED" },
	{ -2005531930, "D3DERR_MATERIAL_SETDATA_FAILED" },
	{ -2005531929, "D3DERR_MATERIAL_GETDATA_FAILED" },
	{ -2005531911, "D3DERR_SCENE_NOT_IN_SCENE" },
	{ -2005531910, "D3DERR_SCENE_BEGIN_FAILED" },
	{ -2005531909, "D3DERR_SCENE_END_FAILED" },
	{ 0, "DD_OK" },
	{ -2005532667, "DDERR_ALREADYINITIALIZED" },
	{ -2005532662, "DDERR_CANNOTATTACHSURFACE" },
	{ -2005532652, "DDERR_CANNOTDETACHSURFACE" },
	{ -2005532632, "DDERR_CURRENTLYNOTAVAIL" },
	{ -2005532617, "DDERR_EXCEPTION" },
	{ -2147467259, "DDERR_GENERIC" },
	{ -2005532582, "DDERR_HEIGHTALIGN" },
	{ -2005532577, "DDERR_INCOMPATIBLEPRIMARY" },
	{ -2005532572, "DDERR_INVALIDCAPS" },
	{ -2005532562, "DDERR_INVALIDCLIPLIST" },
	{ -2005532552, "DDERR_INVALIDMODE" },
	{ -2005532542, "DDERR_INVALIDOBJECT" },
	{ -2147024809, "DDERR_INVALIDPARAMS" },
	{ -2005532527, "DDERR_INVALIDPIXELFORMAT" },
	{ -2005532522, "DDERR_INVALIDRECT" },
	{ -2005532512, "DDERR_LOCKEDSURFACES" },
	{ -2005532502, "DDERR_NO3D" },
	{ -2005532492, "DDERR_NOALPHAHW" },
	{ -2005532467, "DDERR_NOCLIPLIST" },
	{ -2005532462, "DDERR_NOCOLORCONVHW" },
	{ -2005532460, "DDERR_NOCOOPERATIVELEVELSET" },
	{ -2005532457, "DDERR_NOCOLORKEY" },
	{ -2005532452, "DDERR_NOCOLORKEYHW" },
	{ -2005532450, "DDERR_NODIRECTDRAWSUPPORT" },
	{ -2005532447, "DDERR_NOEXCLUSIVEMODE" },
	{ -2005532442, "DDERR_NOFLIPHW" },
	{ -2005532432, "DDERR_NOGDI" },
	{ -2005532422, "DDERR_NOMIRRORHW" },
	{ -2005532417, "DDERR_NOTFOUND" },
	{ -2005532412, "DDERR_NOOVERLAYHW" },
	{ -2005532392, "DDERR_NORASTEROPHW" },
	{ -2005532382, "DDERR_NOROTATIONHW" },
	{ -2005532362, "DDERR_NOSTRETCHHW" },
	{ -2005532356, "DDERR_NOT4BITCOLOR" },
	{ -2005532355, "DDERR_NOT4BITCOLORINDEX" },
	{ -2005532352, "DDERR_NOT8BITCOLOR" },
	{ -2005532342, "DDERR_NOTEXTUREHW" },
	{ -2005532337, "DDERR_NOVSYNCHW" },
	{ -2005532332, "DDERR_NOZBUFFERHW" },
	{ -2005532322, "DDERR_NOZOVERLAYHW" },
	{ -2005532312, "DDERR_OUTOFCAPS" },
	{ -2147024882, "DDERR_OUTOFMEMORY" },
	{ -2005532292, "DDERR_OUTOFVIDEOMEMORY" },
	{ -2005532290, "DDERR_OVERLAYCANTCLIP" },
	{ -2005532288, "DDERR_OVERLAYCOLORKEYONLYONEACTIVE" },
	{ -2005532285, "DDERR_PALETTEBUSY" },
	{ -2005532272, "DDERR_COLORKEYNOTSET" },
	{ -2005532262, "DDERR_SURFACEALREADYATTACHED" },
	{ -2005532252, "DDERR_SURFACEALREADYDEPENDENT" },
	{ -2005532242, "DDERR_SURFACEBUSY" },
	{ -2005532232, "DDERR_SURFACEISOBSCURED" },
	{ -2005532222, "DDERR_SURFACELOST" },
	{ -2005532212, "DDERR_SURFACENOTATTACHED" },
	{ -2005532202, "DDERR_TOOBIGHEIGHT" },
	{ -2005532192, "DDERR_TOOBIGSIZE" },
	{ -2005532182, "DDERR_TOOBIGWIDTH" },
	{ -2147467263, "DDERR_UNSUPPORTED" },
	{ -2005532162, "DDERR_UNSUPPORTEDFORMAT" },
	{ -2005532152, "DDERR_UNSUPPORTEDMASK" },
	{ -2005532135, "DDERR_VERTICALBLANKINPROGRESS" },
	{ -2005532132, "DDERR_WASSTILLDRAWING" },
	{ -2005532112, "DDERR_XALIGN" },
	{ -2005532111, "DDERR_INVALIDDIRECTDRAWGUID" },
	{ -2005532110, "DDERR_DIRECTDRAWALREADYCREATED" },
	{ -2005532109, "DDERR_NODIRECTDRAWHW" },
	{ -2005532108, "DDERR_PRIMARYSURFACEALREADYEXISTS" },
	{ -2005532107, "DDERR_NOEMULATION" },
	{ -2005532106, "DDERR_REGIONTOOSMALL" },
	{ -2005532105, "DDERR_CLIPPERISUSINGHWND" },
	{ -2005532104, "DDERR_NOCLIPPERATTACHED" },
	{ -2005532103, "DDERR_NOHWND" },
	{ -2005532102, "DDERR_HWNDSUBCLASSED" },
	{ -2005532101, "DDERR_HWNDALREADYSET" },
	{ -2005532100, "DDERR_NOPALETTEATTACHED" },
	{ -2005532099, "DDERR_NOPALETTEHW" },
	{ -2005532098, "DDERR_BLTFASTCANTCLIP" },
	{ -2005532097, "DDERR_NOBLTHW" },
	{ -2005532096, "DDERR_NODDROPSHW" },
	{ -2005532095, "DDERR_OVERLAYNOTVISIBLE" },
	{ -2005532094, "DDERR_NOOVERLAYDEST" },
	{ -2005532093, "DDERR_INVALIDPOSITION" },
	{ -2005532092, "DDERR_NOTAOVERLAYSURFACE" },
	{ -2005532091, "DDERR_EXCLUSIVEMODEALREADYSET" },
	{ -2005532090, "DDERR_NOTFLIPPABLE" },
	{ -2005532089, "DDERR_CANTDUPLICATE" },
	{ -2005532088, "DDERR_NOTLOCKED" },
	{ -2005532087, "DDERR_CANTCREATEDC" },
	{ -2005532086, "DDERR_NODC" },
	{ -2005532085, "DDERR_WRONGMODE" },
	{ -2005532084, "DDERR_IMPLICITLYCREATED" },
	{ -2005532083, "DDERR_NOTPALETTIZED" },
	{ -2005532082, "DDERR_UNSUPPORTEDMODE" },
};
// GLOBAL: XVT 0x6644F0
static unsigned int g_std3DFogTableEndBits;
// GLOBAL: XVT 0x6644F4
static unsigned int g_std3DFogColorBlue8;
// GLOBAL: XVT 0x664544
static unsigned int g_std3DFogTableStartBits;
// GLOBAL: XVT 0x664750
static unsigned int g_std3DFogColorRed8;
// GLOBAL: XVT 0x664770
uint16_t g_std3DPaletteScratch16[256];
// GLOBAL: XVT 0x6642F0
uint16_t g_texConvBuf4444[256] = { 0 };
// GLOBAL: XVT 0x664550
uint16_t g_texConvBuf1555[256] = { 0 };
// GLOBAL: XVT 0x664980
static uint8_t g_std3DPaletteConversionSourceRgb[768] = { 0 };
// GLOBAL: XVT 0x664978
static unsigned int g_std3DFogColorGreen8;
// GLOBAL: XVT 0x664970
static IDirect3D* g_lpD3D = 0;
// GLOBAL: XVT 0x664974
static IDirect3DViewport* g_d3dViewport = 0;
// GLOBAL: XVT 0x66497C
static Std3DUnknown* g_d3dViewportMaterial = 0;
// GLOBAL: XVT 0x664548
IDirect3DDevice* g_d3dDevice;
// GLOBAL: XVT 0xA91B14
struct IDirectDrawSurface* g_std3DRenderSurface;
// GLOBAL: XVT 0xA91120
Std3DDevice g_std3DDevices[4] = { { 0 } };
// GLOBAL: XVT 0xA90B10
float g_std3DColorOverlayRed;
// GLOBAL: XVT 0xA90B14
float g_std3DColorOverlayGreen;
// GLOBAL: XVT 0xA90B18
float g_std3DColorOverlayBlue;
// GLOBAL: XVT 0xA90B1C
int g_std3DColorOverlayEnabled;
// GLOBAL: XVT 0xA90B20
int g_std3DZBufferBitDepth = 0;
// GLOBAL: XVT 0xA90B30
Std3DRenderTri g_std3DViewportQuadTriangles[2] = { { 0 } };
// GLOBAL: XVT 0xA90B60
D3DTLVERTEX g_std3DQuadVerts[4] = { { 0 } };
// GLOBAL: XVT 0xA90BF0
static Std3DViewportRect g_std3DQuadRect = { 0 };
// GLOBAL: XVT 0xA90A70
Std3DTexCacheNode g_std3DColorOverlayTexNode = { 0 };
// GLOBAL: XVT 0xA91A34
Std3DZBufferSurfaceBlock g_std3DZBufferSurfaceBlock = { 0 };
// GLOBAL: XVT 0xA919D0
Std3DZBufferTargetScratch g_std3DZBufferTargetScratch = { 0 };
// GLOBAL: XVT 0x528C70
int g_std3DMinTextureWidth;
// GLOBAL: XVT 0x528C74
int g_std3DMinTextureHeight;
// GLOBAL: XVT 0x528C78
int g_std3DMaxTextureWidth;
// GLOBAL: XVT 0x528C7C
int g_std3DMaxTextureHeight;
// GLOBAL: XVT 0x528C80
unsigned int g_std3DExecBufMaxVerts = 0;
// GLOBAL: XVT 0x528C84
unsigned int g_std3DTextureFrameTag = 1;
// GLOBAL: XVT 0x528C88
int g_texCacheCount = 0;
// GLOBAL: XVT 0x528C8C
Std3DTexCacheNode* g_pTexCacheHead = 0;
// GLOBAL: XVT 0x528C90
Std3DTexCacheNode* g_pTexCacheTail = 0;
// GLOBAL: XVT 0x528C94
Std3DVBuffer* g_pStd3DVBuffer = 0;
// GLOBAL: XVT 0x528C98
IDirect3DExecuteBuffer* g_d3dExecuteBuffer = 0;
// GLOBAL: XVT 0x528C9C
unsigned int g_std3DExecBufSize = 0;
// GLOBAL: XVT 0x528CA0
int g_d3dBufVertCount = 0;
// GLOBAL: XVT 0x528CA4
static unsigned int g_std3DExecBufTriCount = 0;
// GLOBAL: XVT 0x528CA8
static Std3DTexCacheNode* g_d3dCurTexture = (Std3DTexCacheNode*)1;
// GLOBAL: XVT 0x528CAC
Std3DZBufferSurfaceBlock* g_pStd3DZBufferState = NULL;
// GLOBAL: XVT 0x528CBC
int g_std3DDeviceOpen = 0;
// GLOBAL: XVT 0x664754
static uint8_t* g_d3dExecBufBase = 0;
// GLOBAL: XVT 0x664758
D3DEXECUTEBUFFERDESC g_d3dExecBufDesc = { 0 };
// GLOBAL: XVT 0x664C80
uint8_t* g_d3dWritePtr = (uint8_t*)(uintptr_t)-1;
// GLOBAL: XVT 0x664C84
static uint8_t* g_d3dInstrStart = 0;
// GLOBAL: XVT 0x6644F8
Std3DRenderTargetDesc g_std3DRenderTargetDesc = { 0 };
// GLOBAL: XVT 0xA90BE0
Std3DRenderTargetDesc* g_pStd3DRenderTarget = 0;

// FUNCTION: XVT 0x4B0B60
void std3D_CopyPaletteToScratch16(const uint16_t* palette, int colorCount) {
	memcpy(g_std3DPaletteScratch16, palette, (size_t)colorCount * sizeof(*palette));
}

// FUNCTION: XVT 0x4B0B90
void std3D_ConvertTexTo1555(const uint16_t* srcPixels, int pixelCount) {
	int pixelIndex;
	ColorInfo* sourceFormat;
	ColorInfo* targetFormat;

	if (g_pFmtRGB565 == g_pFmtRGBA1555) {
		memcpy(g_texConvBuf1555, srcPixels, (size_t)pixelCount * sizeof(*srcPixels));
	} else {
		sourceFormat = &g_pFmtRGB565->colorInfo;
		targetFormat = &g_pFmtRGBA1555->colorInfo;
		for (pixelIndex = 0; pixelIndex < pixelCount; ++pixelIndex) {
			uint8_t channel;

			channel = (uint8_t)((srcPixels[pixelIndex] >> sourceFormat->redPosShift)
								<< sourceFormat->redPosShiftRight);
			g_texConvBuf1555[pixelIndex] =
				(uint16_t)((channel >> targetFormat->redPosShiftRight) << targetFormat->redPosShift);
			channel = (uint8_t)((srcPixels[pixelIndex] >> sourceFormat->greenPosShift)
								<< sourceFormat->greenPosShiftRight);
			g_texConvBuf1555[pixelIndex] |=
				(uint16_t)((channel >> targetFormat->greenPosShiftRight) << targetFormat->greenPosShift);
			channel = (uint8_t)((srcPixels[pixelIndex] >> sourceFormat->bluePosShift)
								<< sourceFormat->bluePosShiftRight);
			g_texConvBuf1555[pixelIndex] |=
				(uint16_t)((channel >> targetFormat->bluePosShiftRight) << targetFormat->bluePosShift);
			if (pixelIndex != 0) {
				channel = 0xff;
				g_texConvBuf1555[pixelIndex] |=
					(uint16_t)((channel >> targetFormat->alphaPosShiftRight) << targetFormat->alphaPosShift);
			}
		}
	}
}

// FUNCTION: XVT 0x4B0DB0
int std3D_Startup(void) {
	int result;

	g_std3DDirectDraw = Renderer_GetDirectDraw();
	if (g_std3DDirectDraw == NULL) {
		DebugPrintf("DDraw device not created yet!\n", 0, 0, 0, 0);
		return 0;
	}

	g_std3DCapFlags = 0x19b3;
	g_std3DZBufferEnabled = 1;
	DebugPrintf("Creating D3D interface object.\n", 0, 0, 0, 0);
	result = g_std3DDirectDraw->lpVtbl->QueryInterface(g_std3DDirectDraw, &CLSID_IDirect3D, (void**)&g_lpD3D);
	if (result != 0) {
		DebugPrintf("Error %s creating Direct3D interface object.\n",
					std3D_LookupErrorString(result, g_std3DErrorStringTable, 121), 0, 0, 0);
		return 0;
	}

	DebugPrintf("Enumerating D3D devices.\n", 0, 0, 0, 0);
	g_std3DNumDevices = 0;
	result = g_lpD3D->lpVtbl->EnumDevices(g_lpD3D, std3D_EnumDevicesCallback, NULL);
	if (result != 0) {
		DebugPrintf("Error %s when enumerating D3D devices.\n",
					std3D_LookupErrorString(result, g_std3DErrorStringTable, 121), 0, 0, 0);
		return 0;
	}
	if (g_std3DNumDevices == 0) {
		return 0;
	}

	DebugPrintf("%d D3D devices found.\n", g_std3DNumDevices,
				std3D_LookupErrorString(result, g_std3DErrorStringTable, 121), 0, 0);
	g_std3DStartupDone = 1;
	DebugPrintf("Startup Succeeded.\n", 0, 0, 0, 0);
	return 1;
}

// FUNCTION: XVT 0x4B0EF0
Std3DRenderTargetDesc* std3D_InitRenderTargetDesc(unsigned int arg1, unsigned int arg2, int arg3) {
	Std3DRenderTargetDesc* result;
	Std3DRenderTargetDesc** renderTarget;

	memset(&g_std3DRenderTargetDesc, 0, sizeof(g_std3DRenderTargetDesc));
	renderTarget = &g_pStd3DRenderTarget;
	*renderTarget = &g_std3DRenderTargetDesc;
	g_std3DRenderTargetDesc.width = arg1;
	(*renderTarget)->height = arg2;
	g_pStd3DRenderTarget->pitch = arg3;
	g_pStd3DRenderTarget->widthPixels = arg3 / 2;
	g_pStd3DRenderTarget->sizeBytes = g_pStd3DRenderTarget->pitch * g_pStd3DRenderTarget->height;
	g_pStd3DRenderTarget->colorInfo.colorMode = STDCOLOR_RGB;
	g_pStd3DRenderTarget->colorInfo.bpp = 16;
	g_pStd3DRenderTarget->colorInfo.redBPP = 5;
	g_pStd3DRenderTarget->colorInfo.greenBPP = 6;
	g_pStd3DRenderTarget->colorInfo.blueBPP = 5;
	g_pStd3DRenderTarget->colorInfo.redPosShift = 11;
	g_pStd3DRenderTarget->colorInfo.greenPosShift = 5;
	g_pStd3DRenderTarget->colorInfo.bluePosShift = 0;
	g_pStd3DRenderTarget->colorInfo.redPosShiftRight = 3;
	g_pStd3DRenderTarget->colorInfo.greenPosShiftRight = 2;
	g_pStd3DRenderTarget->colorInfo.bluePosShiftRight = 3;
	g_pStd3DRenderTarget->colorInfo.alphaBPP = 0;
	g_pStd3DRenderTarget->colorInfo.alphaPosShift = 0;
	result = g_pStd3DRenderTarget;
	result->colorInfo.alphaPosShiftRight = 0;
	return result;
}

// FUNCTION: XVT 0x4B0FF0
void std3D_Shutdown(void) {
	if (g_lpD3D != 0) {
		g_lpD3D->lpVtbl->Release(g_lpD3D);
#ifdef XVT_MODERN
		g_lpD3D = NULL;
#endif
	}
	DebugPrintf(g_std3DShutdownSucceededMessage, 0, 0, 0, 0);
	g_std3DStartupDone = 0;
}

// FUNCTION: XVT 0x4B1030
int std3D_CreateDevice(unsigned int deviceIdx, int bUseZBuffer) {
	Std3DRasterInfo raster;
	ColorInfo alphaFormat;
	unsigned int maxBufferSize;
	unsigned int maxVertexCount;
	HRESULT result;
	const char* errorString;

	if (g_std3DDeviceOpen != 0) {
		DebugPrintf("Error: Multiple Opens Attempted.\n", 0, 0, 0, 0);
		return 0;
	}
	if (g_std3DNumDevices <= deviceIdx)
		return 0;

	g_std3DCurDeviceIdx = deviceIdx;
	g_pStd3DCurDevice = &g_std3DDevices[deviceIdx];
	g_std3DZBufferEnabled =
		bUseZBuffer != 0 && g_pStd3DCurDevice->caps.bHasZBuffer != 0 && (g_std3DCapFlags & 0x1800) != 0;
	if (g_std3DZBufferEnabled != 0) {
		if (std3D_CreateZBuffer(g_pStd3DRenderTarget->width, g_pStd3DRenderTarget->height) == 0) {
			DebugPrintf("Error creating Z buffer.\n", 0, 0, 0, 0);
			return 0;
		}
		g_std3DZBufferBitDepth = 16;
		if ((g_pStd3DCurDevice->caps.zCmpCapsMask & 0x10) == 0)
			g_std3DZBufferBitDepth = 2;
		DebugPrintf("Z compare: %s\n", g_std3DZBufferBitDepth == 16 ? "Greater" : "Less", 0, 0, 0);
	}

	DebugPrintf("Creating D3D device #%d.\n", g_std3DCurDeviceIdx, 0, 0, 0);
	result = g_std3DRenderSurface->lpVtbl->QueryInterface(g_std3DRenderSurface, &g_pStd3DCurDevice->guid,
														  (void**)&g_d3dDevice);
	if (result != 0) {
		errorString = std3D_LookupErrorString(result, g_std3DErrorStringTable, 121);
		DebugPrintf("Error %s creating Direct3D device.\n", errorString, 0, 0, 0);
		return 0;
	}

	g_std3DNumTextureFormats = 0;
	result = g_d3dDevice->lpVtbl->EnumTextureFormats(g_d3dDevice, (void*)std3D_EnumTextureFormats, NULL);
	if (result != 0) {
		errorString = std3D_LookupErrorString(result, g_std3DErrorStringTable, 121);
		DebugPrintf("Error %s when enumerating D3D device texture formats.\n", errorString, 0, 0, 0);
		return 0;
	}
	if (g_std3DNumTextureFormats == 0) {
		DebugPrintf("Error: no texture formats found.\n", 0, 0, 0, 0);
		return 0;
	}

	errorString = std3D_LookupErrorString(0, g_std3DErrorStringTable, 121);
	DebugPrintf("%d texture formats found.\n", g_std3DNumTextureFormats, errorString, 0);
	if (std3D_CreateViewport(g_pStd3DRenderTarget->width, g_pStd3DRenderTarget->height) == 0) {
		DebugPrintf("Error creating viewport.\n", 0, 0, 0, 0);
		return 0;
	}
	if (std3D_SetInitialRenderState() == 0) {
		DebugPrintf("Error initializing render state.\n", 0, 0, 0, 0);
		return 0;
	}

	DebugPrintf("Creating Execute buffer.\n", 0, 0, 0, 0);
	maxBufferSize = g_pStd3DCurDevice->caps.maxBufferSize;
	g_std3DExecBufSize = 0x10000;
	if (maxBufferSize != 0)
		g_std3DExecBufSize = maxBufferSize;
	g_d3dExecBufDesc.dwFlags = 0;
	g_d3dExecBufDesc.dwCaps = 0;
	g_d3dExecBufDesc.dwBufferSize = 0;
	g_d3dExecBufDesc.lpData = NULL;
	g_d3dExecBufDesc.dwSize = 20;
	g_d3dExecBufDesc.dwFlags = 1;
	g_d3dExecBufDesc.dwBufferSize = g_std3DExecBufSize;
	maxVertexCount = g_pStd3DCurDevice->caps.maxVertexCount;
	g_std3DExecBufMaxVerts = maxVertexCount == 0 ? 512 : (maxVertexCount < 512 ? maxVertexCount : 512);
	DebugPrintf("Execute buffer size: %d.\n", g_std3DExecBufSize, 0, 0, 0);
	DebugPrintf("Max vertices: %d.\n", g_std3DExecBufMaxVerts, 0, 0, 0);
	result =
		g_d3dDevice->lpVtbl->CreateExecuteBuffer(g_d3dDevice, &g_d3dExecBufDesc, &g_d3dExecuteBuffer, NULL);
	if (result != 0) {
		errorString = std3D_LookupErrorString(result, g_std3DErrorStringTable, 121);
		DebugPrintf("Error %s creating D3D Execute buffer.\n", errorString, 0, 0, 0);
	}

	g_texCacheCount = 0;
	g_pTexCacheHead = NULL;
	g_pTexCacheTail = NULL;
	g_std3DTextureFrameTag = 1;
	g_fmtIdxRGB565 = std3D_FindClosestFormat(&g_pStd3DRenderTarget->colorInfo, g_std3DTextureFormats,
											 g_std3DNumTextureFormats);
	g_pFmtRGB565 = &g_std3DTextureFormats[g_fmtIdxRGB565];
	if (g_pStd3DCurDevice->caps.bAlphaTexture != 0) {
		alphaFormat.colorMode = STDCOLOR_RGBA;
		alphaFormat.bpp = 16;
		alphaFormat.redBPP = 5;
		alphaFormat.greenBPP = 5;
		alphaFormat.blueBPP = 5;
		alphaFormat.alphaBPP = 1;
		g_fmtIdxRGBA1555 =
			std3D_FindClosestFormat(&alphaFormat, g_std3DTextureFormats, g_std3DNumTextureFormats);
		g_pFmtRGBA1555 = &g_std3DTextureFormats[g_fmtIdxRGBA1555];
		if (g_pStd3DCurDevice->caps.bAlphaBlend == 0) {
			alphaFormat.redBPP = 4;
			alphaFormat.greenBPP = 4;
			alphaFormat.blueBPP = 4;
			alphaFormat.alphaBPP = 4;
			g_fmtIdxRGBA4444 =
				std3D_FindClosestFormat(&alphaFormat, g_std3DTextureFormats, g_std3DNumTextureFormats);
			g_pFmtRGBA4444 = &g_std3DTextureFormats[g_fmtIdxRGBA4444];
			raster.width = 32;
			raster.height = 32;
			memcpy(&raster.colorMode, &g_pFmtRGBA4444->colorInfo, sizeof(g_pFmtRGBA4444->colorInfo));
			g_pStd3DVBuffer = std3D_AllocVBuffer(&raster, 0, 0, 0);
		}
	}

	std3D_QueryTextureVidMem(&g_pStd3DCurDevice->totalMemory, &g_pStd3DCurDevice->availableMemory);
	DebugPrintf("Texture Ram  Total: %d bytes  Free: %d bytes.\n", g_pStd3DCurDevice->totalMemory,
				g_pStd3DCurDevice->availableMemory, 0, 0);
	DebugPrintf("Device #%d opened successfully.\n", g_std3DCurDeviceIdx, 0, 0, 0);
	DebugPrintf("std3D opened.\n", 0, 0, 0, 0);
	g_std3DDeviceOpen = 1;
	return 1;
}

// FUNCTION: XVT 0x4B15A0
struct IDirectDrawSurface* std3D_SetRenderSurface(struct IDirectDrawSurface* arg1) {
	return g_std3DRenderSurface = arg1;
}

// FUNCTION: XVT 0x4B15B0
int std3D_SetColorOverlayParams(float red, float green, float blue, int enabled) {
	g_std3DColorOverlayRed = red;
	g_std3DColorOverlayGreen = green;
	g_std3DColorOverlayBlue = blue;
	return g_std3DColorOverlayEnabled = enabled;
}

// FUNCTION: XVT 0x4B15E0
int std3D_Log2Floor(int n) {
	int result = 0;

	while (n > 1) {
		n >>= 1;
		++result;
	}
	return result;
}

// FUNCTION: XVT 0x4B1600
const char* std3D_LookupErrorString(int errorCode, const Std3DErrorStringEntry* entries, int entryCount) {
	const char* result;
	int entryIndex;
	const Std3DErrorStringEntry* entry;

	result = g_std3DUnknownErrorMessage;
	entryIndex = 0;
	if (entryCount > 0) {
		entry = entries;
		do {
			if (entry->code == errorCode) {
				result = entries[entryIndex].message;
				break;
			}
			++entry;
			++entryIndex;
		} while (entryIndex < entryCount);
	}
	return result;
}

// FUNCTION: XVT 0x4B1640
void std3D_BuildColormap16(uint8_t* pRGB888, uint16_t* pOut, ColorInfo* pFmt, uint8_t defaultAlpha,
						   int colorKey) {
	uint16_t* output = pOut;
	ColorInfo* format = pFmt;
	uint8_t* rgb888 = pRGB888;
	int alphaBPP;
	int entriesRemaining;

	entriesRemaining = 256;
	do {
		*output = (uint16_t)((uint8_t)(rgb888[0] >> format->redPosShiftRight) << format->redPosShift);
		*output |= (uint16_t)((uint8_t)(rgb888[1] >> format->greenPosShiftRight) << format->greenPosShift);
		*output |= (uint16_t)((uint8_t)(rgb888[2] >> format->bluePosShiftRight) << format->bluePosShift);
		if ((uint8_t)colorKey != 0)
			defaultAlpha = (uint8_t)((*output == 0) - 1);
		alphaBPP = format->alphaBPP;
		if (alphaBPP == 1)
			defaultAlpha = (uint8_t)(0xFF - defaultAlpha);
		if (alphaBPP != 0) {
			*output |=
				(uint16_t)((uint8_t)(defaultAlpha >> format->alphaPosShiftRight) << format->alphaPosShift);
		}
		rgb888 += 3;
		++output;
		--entriesRemaining;
	} while (entriesRemaining != 0);
}

// FUNCTION: XVT 0x4B1710
void std3D_BuildColormapOpaque(uint8_t* pRGB888, uint16_t* pOut, ColorInfo* pFmt) {
	std3D_BuildColormap16(pRGB888, pOut, pFmt, 0xFF, 0);
}

// FUNCTION: XVT 0x4B1730
void std3D_BuildColormapColorKey(uint8_t* pRGB888, uint16_t* pOut, ColorInfo* pFmt) {
	std3D_BuildColormap16(pRGB888, pOut, pFmt, 0xFF, 1);
}

// FUNCTION: XVT 0x4B1750
void std3D_BuildColormapAlpha(uint8_t* pRGB888, uint16_t* pOut, ColorInfo* pFmt, uint8_t alpha) {
	std3D_BuildColormap16(pRGB888, pOut, pFmt, alpha, 0);
}

// FUNCTION: XVT 0x4B1770
Std3DVBuffer* std3D_AllocVBuffer(const Std3DRasterInfo* raster, ...) {
	Std3DVBuffer* vbuffer;

	vbuffer = (Std3DVBuffer*)malloc(sizeof(*vbuffer));
	memset(vbuffer, 0, sizeof(*vbuffer));
	vbuffer->storageType = 0;
	memcpy(&vbuffer->raster, raster, sizeof(vbuffer->raster));
	vbuffer->pixels = malloc(raster->width * raster->height * (raster->bpp >> 3));
	vbuffer->raster.rowPitch = raster->width * (raster->bpp >> 3);
	return vbuffer;
}

// FUNCTION: XVT 0x4B17D0
void std3D_FreeVBuffer(Std3DVBuffer* vbuffer) {
	if (vbuffer->storageType == 1) {
		if (vbuffer->ddSurface != NULL)
			vbuffer->ddSurface->lpVtbl->Release(vbuffer->ddSurface);
	} else {
		free(vbuffer->pixels);
	}
	memset(vbuffer, 0, sizeof(*vbuffer));
	free(vbuffer);
}

// FUNCTION: XVT 0x4B1810
void std3D_LockVBuffer(Std3DVBuffer* vbuffer) {
	DDSURFACEDESC surfaceDesc;
	HRESULT result;

	if (vbuffer->storageType == 1 && vbuffer->lockCount == 0) {
		memset(&surfaceDesc, 0, sizeof(surfaceDesc));
		surfaceDesc.dwSize = sizeof(surfaceDesc);
		result = vbuffer->ddSurface->lpVtbl->Lock(vbuffer->ddSurface, NULL, &surfaceDesc, DDLOCK_WAIT, NULL);
		if (result != 0) {
			DebugPrintf("Error %x locking buffer %x, surface %x\n", result, vbuffer, vbuffer->ddSurface);
			return;
		}
		vbuffer->pixels = surfaceDesc.lpSurface;
		vbuffer->raster.rowPitch = surfaceDesc.lPitch;
	}

	++vbuffer->lockCount;
}

// FUNCTION: XVT 0x4B1890
void std3D_UnlockVBuffer(Std3DVBuffer* vbuffer) {
	if ((unsigned int)vbuffer->lockCount < 1) {
		DebugPrintf("Unlock Warning: buffer %x, not locked\n", vbuffer);
		return;
	}

	if (vbuffer->lockCount == 1 && vbuffer->storageType == 1) {
		HRESULT result;

		result = vbuffer->ddSurface->lpVtbl->Unlock(vbuffer->ddSurface, vbuffer->pixels);
		if (result != 0) {
			DebugPrintf("Error %x unlocking buffer %x, surface %x\n", result, vbuffer, vbuffer->ddSurface);
			return;
		}
	}

	--vbuffer->lockCount;
}

// FUNCTION: XVT 0x4B18F0
void std3D_Close(void) {
	if (!g_std3DDeviceOpen) {
		DebugPrintf("Error: Multiple Closes Attempted.\n", 0, 0, 0, 0);
		return;
	}

	if (g_pStd3DVBuffer != NULL) {
		std3D_FreeVBuffer(g_pStd3DVBuffer);
		g_pStd3DVBuffer = NULL;
	}

	g_d3dExecuteBuffer->lpVtbl->Release(g_d3dExecuteBuffer);
	std3D_FlushTextureCache();

	if (g_d3dViewport != NULL) {
		g_d3dViewport->lpVtbl->Release(g_d3dViewport);
		g_d3dViewport = NULL;
	}

	if (g_d3dViewportMaterial != NULL) {
		g_d3dViewportMaterial->lpVtbl->release(g_d3dViewportMaterial);
		g_d3dViewportMaterial = NULL;
	}

	if (g_std3DZBufferSurfaceBlock.surface != NULL) {
		g_std3DZBufferSurfaceBlock.surface->lpVtbl->Release(g_std3DZBufferSurfaceBlock.surface);
		g_std3DZBufferSurfaceBlock.surface = NULL;
	}

	if (g_d3dDevice != NULL) {
		g_d3dDevice->lpVtbl->Release(g_d3dDevice);
		g_d3dDevice = NULL;
	}

	DebugPrintf("std3D closed.\n", 0, 0, 0, 0);
	g_std3DDeviceOpen = 0;
}

// FUNCTION: XVT 0x4B19E0
void std3D_BlitVBuffer(Std3DVBuffer* destination, Std3DVBuffer* source, int destinationX, int destinationY,
					   int sourceX, int sourceY) {
	uint8_t* sourcePixels;
	uint8_t* destinationPixels;
	unsigned int rowBytes;
	unsigned int rowIndex;

	(void)sourceX;
	(void)sourceY;

	std3D_LockVBuffer(destination);
	std3D_LockVBuffer(source);

	sourcePixels = (uint8_t*)source->pixels;
	destinationPixels = (uint8_t*)destination->pixels + destinationX * (destination->raster.bpp >> 3) +
						destinationY * destination->raster.rowPitch;
	rowBytes = source->raster.width * (source->raster.bpp >> 3);
	for (rowIndex = 0; rowIndex < source->raster.height; ++rowIndex) {
		memcpy(destinationPixels, sourcePixels, rowBytes);
		destinationPixels += destination->raster.rowPitch;
		sourcePixels += source->raster.rowPitch;
	}

	std3D_UnlockVBuffer(destination);
	std3D_UnlockVBuffer(source);
}

// FUNCTION: XVT 0x4B1A80
unsigned int std3D_GetCapFlags(void) { return g_std3DCapFlags; }

// FUNCTION: XVT 0x4B1A90
void std3D_FillVBuffer(Std3DVBuffer* vbuffer, unsigned int packedColor, int fillMode) {
	uint8_t* rowPixels;
	unsigned int rowIndex;
	unsigned int columnIndex;
	uint16_t* destination16;

	(void)fillMode;
	std3D_LockVBuffer(vbuffer);
	rowPixels = vbuffer->pixels;
	switch (vbuffer->raster.bpp) {
		case 8:
			rowIndex = 0;
			if (vbuffer->raster.height > rowIndex) {
				do {
					memset(rowPixels, (uint8_t)packedColor, vbuffer->raster.width);
					rowPixels += vbuffer->raster.rowPitch;
					++rowIndex;
				} while (vbuffer->raster.height > rowIndex);
			}
			break;
		case 16:
			rowIndex = 0;
			if (vbuffer->raster.height > rowIndex) {
				do {
					columnIndex = 0;
					if (vbuffer->raster.width > columnIndex) {
						destination16 = (uint16_t*)rowPixels;
						do {
							*destination16 = (uint16_t)packedColor;
							++destination16;
							++columnIndex;
						} while (vbuffer->raster.width > columnIndex);
					}
					rowPixels += vbuffer->raster.rowPitch;
					++rowIndex;
				} while (vbuffer->raster.height > rowIndex);
			}
			break;
		case 32: {
			unsigned int row32;
			unsigned int column32;
			uint32_t* destination32;

			row32 = 0;
			if (vbuffer->raster.height > row32) {
				do {
					column32 = 0;
					if (vbuffer->raster.width > column32) {
						destination32 = (uint32_t*)rowPixels;
						do {
							*destination32 = packedColor;
							++destination32;
							++column32;
						} while (vbuffer->raster.width > column32);
					}
					rowPixels += vbuffer->raster.rowPitch;
					++row32;
				} while (vbuffer->raster.height > row32);
			}
			break;
		}
		default:
			break;
	}

	std3D_UnlockVBuffer(vbuffer);
}

// FUNCTION: XVT 0x4B1B70
void std3D_SetCapFlags(unsigned int capFlags) {
	int result;

	g_std3DCapFlags = capFlags;
	result = std3D_SetInitialRenderState();
	if (result == 0)
		DebugPrintf("Error initializing render state.\n", 0, 0, 0, 0);
}

// FUNCTION: XVT 0x4B1BA0
int std3D_SetFogColor8(unsigned int red8, unsigned int green8, unsigned int blue8) {
	g_std3DFogColorRed8 = red8;
	g_std3DFogColorGreen8 = green8;
	g_std3DFogColorBlue8 = blue8;
	return red8;
}

// FUNCTION: XVT 0x4B1BC0
int std3D_SetFogTableRangeBits(unsigned int startBits, unsigned int endBits) {
	g_std3DFogTableStartBits = startBits;
	g_std3DFogTableEndBits = endBits;
	return startBits;
}

// FUNCTION: XVT 0x4B1BE0
int std3D_SetTextureSizeCaps(int minWidth, int minHeight, int maxWidth, int maxHeight) {
	g_std3DMinTextureWidth = minWidth;
	g_std3DMinTextureHeight = minHeight;
	g_std3DMaxTextureWidth = maxWidth;
	return g_std3DMaxTextureHeight = maxHeight;
}

// FUNCTION: XVT 0x4B1C10
void std3D_StartScene(void) {
	int result;

	result = g_d3dDevice->lpVtbl->BeginScene(g_d3dDevice);
	if (result != 0) {
		DebugPrintf(g_std3DBeginSceneErrorFormat,
					std3D_LookupErrorString(result, g_std3DErrorStringTable, 121), 0, 0, 0);
	}
}

// FUNCTION: XVT 0x4B1C50
void std3D_EndScene(void) {
	int result;

	result = g_d3dDevice->lpVtbl->EndScene(g_d3dDevice);
	if (result != 0) {
		DebugPrintf(g_std3DEndSceneErrorFormat, std3D_LookupErrorString(result, g_std3DErrorStringTable, 121),
					0, 0, 0);
	}
}

// FUNCTION: XVT 0x4B1C90
int std3D_LockExecuteBuffer(void) {
	int result;

	g_d3dBufVertCount = 0;
	g_std3DExecBufTriCount = 0;
	++g_std3DTextureFrameTag;
	g_d3dCurTexture = (Std3DTexCacheNode*)1;
	result = g_d3dExecuteBuffer->lpVtbl->Lock(g_d3dExecuteBuffer, &g_d3dExecBufDesc);
	if (result != 0) {
		DebugPrintf(g_std3DLockExecuteBufferErrorFormat,
					std3D_LookupErrorString(result, g_std3DErrorStringTable, 121), 0, 0, 0);
		return 0;
	}
	g_d3dExecBufBase = (uint8_t*)g_d3dExecBufDesc.lpData;
	g_d3dWritePtr = g_d3dExecBufBase;
	return 1;
}

// FUNCTION: XVT 0x4B1D10
int std3D_AddVertices(const D3DTLVERTEX* vertices, int count) {
	int previousVertexCount;
	uint8_t* writePtr;

	previousVertexCount = g_d3dBufVertCount;
	if ((unsigned int)(count + g_d3dBufVertCount) > g_std3DExecBufMaxVerts) {
		return 0;
	}
	writePtr = g_d3dWritePtr;
	if ((const void*)writePtr != (const void*)vertices) {
		memcpy(writePtr, vertices, (size_t)count * sizeof(*vertices));
	}
	g_d3dBufVertCount = count + previousVertexCount;
	g_d3dWritePtr = writePtr + (size_t)count * sizeof(*vertices);
	return 1;
}

// FUNCTION: XVT 0x4B1D70
int std3D_BeginInstructions(void) {
	g_d3dInstrStart = g_d3dWritePtr;
	((D3DINSTRUCTION*)g_d3dWritePtr)->bOpcode = D3DOP_PROCESSVERTICES;
	((D3DINSTRUCTION*)g_d3dWritePtr)->bSize = sizeof(D3DPROCESSVERTICES);
	((D3DINSTRUCTION*)g_d3dWritePtr)->wCount = 1;
	g_d3dWritePtr += sizeof(D3DINSTRUCTION);

	((D3DPROCESSVERTICES*)g_d3dWritePtr)->dwFlags = D3DPROCESSVERTICES_COPY;
	((D3DPROCESSVERTICES*)g_d3dWritePtr)->wStart = 0;
	((D3DPROCESSVERTICES*)g_d3dWritePtr)->wDest = 0;
	((D3DPROCESSVERTICES*)g_d3dWritePtr)->dwCount = (uint32_t)g_d3dBufVertCount;
	((D3DPROCESSVERTICES*)g_d3dWritePtr)->dwReserved = 0;
	g_d3dWritePtr += sizeof(D3DPROCESSVERTICES);
	return 1;
}

// FUNCTION: XVT 0x4B1DE0
int std3D_AddTriangles(const Std3DRenderTri* triangles, unsigned int count) {
	Std3DTexCacheNode* texture;
	unsigned int groupStart;
	int groupCount;
	unsigned int triangleIndex;

	for (groupStart = 0; groupStart < count; groupStart += groupCount) {
		texture = triangles[groupStart].texture;
		groupCount = 0;
		if (texture == NULL) {
			Std3DRenderStateFlags flags = triangles[groupStart].flags;

			for (triangleIndex = groupStart; triangleIndex < count; ++triangleIndex) {
				if (triangles[triangleIndex].texture != NULL || triangles[triangleIndex].flags != flags)
					break;
				++groupCount;
			}

			std3D_SetRenderState(flags);
			if (g_d3dCurTexture != NULL) {
				((D3DINSTRUCTION*)g_d3dWritePtr)->bOpcode = D3DOP_STATERENDER;
				((D3DINSTRUCTION*)g_d3dWritePtr)->bSize = sizeof(D3DSTATE);
				((D3DINSTRUCTION*)g_d3dWritePtr)->wCount = 1;
				g_d3dWritePtr += sizeof(D3DINSTRUCTION);
				((D3DSTATE*)g_d3dWritePtr)->dwState = D3DRENDERSTATE_TEXTUREHANDLE;
				((D3DSTATE*)g_d3dWritePtr)->dwArg = 0;
				g_d3dCurTexture = NULL;
				g_d3dWritePtr += sizeof(D3DSTATE);
			}

			((D3DINSTRUCTION*)g_d3dWritePtr)->bOpcode = D3DOP_TRIANGLE;
			((D3DINSTRUCTION*)g_d3dWritePtr)->bSize = sizeof(D3DTRIANGLE);
			((D3DINSTRUCTION*)g_d3dWritePtr)->wCount = (uint16_t)groupCount;
			g_d3dWritePtr += sizeof(D3DINSTRUCTION);
			for (triangleIndex = 0; triangleIndex < (unsigned int)groupCount; ++triangleIndex) {
				((D3DTRIANGLE*)g_d3dWritePtr)->v1 = (uint16_t)triangles[groupStart + triangleIndex].v0;
				((D3DTRIANGLE*)g_d3dWritePtr)->v2 = (uint16_t)triangles[groupStart + triangleIndex].v1;
				((D3DTRIANGLE*)g_d3dWritePtr)->v3 = (uint16_t)triangles[groupStart + triangleIndex].v2;
				((D3DTRIANGLE*)g_d3dWritePtr)->wFlags =
					D3DTRIFLAG_EDGEENABLE1 | D3DTRIFLAG_EDGEENABLE2 | D3DTRIFLAG_EDGEENABLE3;
				g_d3dWritePtr += sizeof(D3DTRIANGLE);
			}
		} else {
			Std3DRenderStateFlags flags = triangles[groupStart].flags;

			for (triangleIndex = groupStart; triangleIndex < count; ++triangleIndex) {
				if (triangles[triangleIndex].texture != texture || triangles[triangleIndex].flags != flags)
					break;
				++groupCount;
			}

			std3D_SetRenderState(flags);
			if (g_d3dCurTexture != texture) {
				((D3DINSTRUCTION*)g_d3dWritePtr)->bOpcode = D3DOP_STATERENDER;
				((D3DINSTRUCTION*)g_d3dWritePtr)->bSize = sizeof(D3DSTATE);
				((D3DINSTRUCTION*)g_d3dWritePtr)->wCount = 1;
				g_d3dWritePtr += sizeof(D3DINSTRUCTION);
				((D3DSTATE*)g_d3dWritePtr)->dwState = D3DRENDERSTATE_TEXTUREHANDLE;
				((D3DSTATE*)g_d3dWritePtr)->dwArg = texture->texHandle;
				g_d3dWritePtr += sizeof(D3DSTATE);
				g_d3dCurTexture = texture;
			}

			((D3DINSTRUCTION*)g_d3dWritePtr)->bOpcode = D3DOP_TRIANGLE;
			((D3DINSTRUCTION*)g_d3dWritePtr)->bSize = sizeof(D3DTRIANGLE);
			((D3DINSTRUCTION*)g_d3dWritePtr)->wCount = (uint16_t)groupCount;
			g_d3dWritePtr += sizeof(D3DINSTRUCTION);
			for (triangleIndex = 0; triangleIndex < (unsigned int)groupCount; ++triangleIndex) {
				((D3DTRIANGLE*)g_d3dWritePtr)->v1 = (uint16_t)triangles[groupStart + triangleIndex].v0;
				((D3DTRIANGLE*)g_d3dWritePtr)->v2 = (uint16_t)triangles[groupStart + triangleIndex].v1;
				((D3DTRIANGLE*)g_d3dWritePtr)->v3 = (uint16_t)triangles[groupStart + triangleIndex].v2;
				((D3DTRIANGLE*)g_d3dWritePtr)->wFlags =
					D3DTRIFLAG_EDGEENABLE1 | D3DTRIFLAG_EDGEENABLE2 | D3DTRIFLAG_EDGEENABLE3;
				g_d3dWritePtr += sizeof(D3DTRIANGLE);
			}
		}
	}

	g_std3DExecBufTriCount += count;
	return 1;
}

// FUNCTION: XVT 0x4B2020
int std3D_ExecuteBuffer(void) {
	D3DEXECUTEDATA executeData;
	int result;

	((D3DINSTRUCTION*)g_d3dWritePtr)->bOpcode = D3DOP_EXIT;
	((D3DINSTRUCTION*)g_d3dWritePtr)->bSize = 0;
	((D3DINSTRUCTION*)g_d3dWritePtr)->wCount = 0;
	g_d3dWritePtr += sizeof(D3DINSTRUCTION);

	result = g_d3dExecuteBuffer->lpVtbl->Unlock(g_d3dExecuteBuffer);
	if (result != 0) {
		DebugPrintf("Error %s unlocking D3D Execute buffer.\n",
					std3D_LookupErrorString(result, g_std3DErrorStringTable, 121), 0, 0, 0);
		return 0;
	}

	memset(&executeData, 0, sizeof(executeData));
	executeData.dwSize = sizeof(executeData);
	executeData.dwVertexCount = g_d3dBufVertCount;
	executeData.dwInstructionOffset = (uint32_t)(g_d3dInstrStart - g_d3dExecBufBase);
	executeData.dwInstructionLength = (uint32_t)(g_d3dWritePtr - g_d3dInstrStart);
	g_d3dExecuteBuffer->lpVtbl->SetExecuteData(g_d3dExecuteBuffer, &executeData);
	result =
		g_d3dDevice->lpVtbl->Execute(g_d3dDevice, g_d3dExecuteBuffer, g_d3dViewport, D3DEXECUTE_UNCLIPPED);
	if (result != 0) {
		DebugPrintf("Error %s executing buffer.\n",
					std3D_LookupErrorString(result, g_std3DErrorStringTable, 121), 0, 0, 0);
		return 0;
	}
	return 1;
}

// FUNCTION: XVT 0x4B2130
void std3D_SetRenderState(Std3DRenderStateFlags flags) {
	int textureFilter;

	if (g_d3dStateFlags == flags)
		return;

	if (((unsigned int)(flags ^ g_d3dStateFlags) & STD3D_RS_MONO_DISABLE) != 0) {
		((D3DINSTRUCTION*)g_d3dWritePtr)->bOpcode = D3DOP_STATERENDER;
		((D3DINSTRUCTION*)g_d3dWritePtr)->bSize = sizeof(D3DSTATE);
		((D3DINSTRUCTION*)g_d3dWritePtr)->wCount = 1;
		g_d3dWritePtr += sizeof(D3DINSTRUCTION);
		((D3DSTATE*)g_d3dWritePtr)->dwState = D3DRENDERSTATE_MONOENABLE;
		if ((flags & STD3D_RS_MONO_DISABLE) != 0)
			((D3DSTATE*)g_d3dWritePtr)->dwArg = 0;
		else
			((D3DSTATE*)g_d3dWritePtr)->dwArg = 1;
		g_d3dWritePtr += sizeof(D3DSTATE);
	}

	if (((unsigned int)(flags ^ g_d3dStateFlags) &
		 (STD3D_RS_ALPHA_BLEND | STD3D_RS_TEXTURE_MODULATE_ALPHA)) != 0) {
		((D3DINSTRUCTION*)g_d3dWritePtr)->bOpcode = D3DOP_STATERENDER;
		((D3DINSTRUCTION*)g_d3dWritePtr)->bSize = sizeof(D3DSTATE);
		((D3DINSTRUCTION*)g_d3dWritePtr)->wCount = 4;
		if ((flags & (STD3D_RS_ALPHA_BLEND | STD3D_RS_TEXTURE_MODULATE_ALPHA)) != 0) {
			g_d3dWritePtr += sizeof(D3DINSTRUCTION);
			((D3DSTATE*)g_d3dWritePtr)->dwState = D3DRENDERSTATE_SRCBLEND;
			((D3DSTATE*)g_d3dWritePtr)->dwArg = 5;
			g_d3dWritePtr += sizeof(D3DSTATE);
			((D3DSTATE*)g_d3dWritePtr)->dwState = D3DRENDERSTATE_DESTBLEND;
			((D3DSTATE*)g_d3dWritePtr)->dwArg = 6;
			g_d3dWritePtr += sizeof(D3DSTATE);
			((D3DSTATE*)g_d3dWritePtr)->dwState = D3DRENDERSTATE_TEXTUREMAPBLEND;
			if ((flags & STD3D_RS_TEXTURE_MODULATE_ALPHA) != 0)
				((D3DSTATE*)g_d3dWritePtr)->dwArg = 4;
			else
				((D3DSTATE*)g_d3dWritePtr)->dwArg = 2;
			g_d3dWritePtr += sizeof(D3DSTATE);
			((D3DSTATE*)g_d3dWritePtr)->dwState = D3DRENDERSTATE_BLENDENABLE;
			((D3DSTATE*)g_d3dWritePtr)->dwArg = 1;
		} else {
			g_d3dWritePtr += sizeof(D3DINSTRUCTION);
			((D3DSTATE*)g_d3dWritePtr)->dwState = D3DRENDERSTATE_SRCBLEND;
			((D3DSTATE*)g_d3dWritePtr)->dwArg = 2;
			g_d3dWritePtr += sizeof(D3DSTATE);
			((D3DSTATE*)g_d3dWritePtr)->dwState = D3DRENDERSTATE_DESTBLEND;
			((D3DSTATE*)g_d3dWritePtr)->dwArg = 1;
			g_d3dWritePtr += sizeof(D3DSTATE);
			((D3DSTATE*)g_d3dWritePtr)->dwState = D3DRENDERSTATE_TEXTUREMAPBLEND;
			if ((flags & STD3D_RS_TEXTURE_MODULATE_ALPHA) != 0)
				((D3DSTATE*)g_d3dWritePtr)->dwArg = 4;
			else
				((D3DSTATE*)g_d3dWritePtr)->dwArg = 2;
			g_d3dWritePtr += sizeof(D3DSTATE);
			((D3DSTATE*)g_d3dWritePtr)->dwState = D3DRENDERSTATE_BLENDENABLE;
			((D3DSTATE*)g_d3dWritePtr)->dwArg = 0;
		}
		g_d3dWritePtr += sizeof(D3DSTATE);
	}

	if (((unsigned int)(flags ^ g_d3dStateFlags) & (STD3D_RS_Z_COMPARE_ENABLE | STD3D_RS_Z_WRITE_ENABLE)) !=
		0) {
		((D3DINSTRUCTION*)g_d3dWritePtr)->bOpcode = D3DOP_STATERENDER;
		((D3DINSTRUCTION*)g_d3dWritePtr)->bSize = sizeof(D3DSTATE);
		((D3DINSTRUCTION*)g_d3dWritePtr)->wCount = 2;
		g_d3dWritePtr += sizeof(D3DINSTRUCTION);
		((D3DSTATE*)g_d3dWritePtr)->dwState = D3DRENDERSTATE_ZFUNC;
		if ((flags & STD3D_RS_Z_COMPARE_ENABLE) != 0)
			((D3DSTATE*)g_d3dWritePtr)->dwArg = std3D_MapZCmpFunc(g_std3DZBufferBitDepth);
		else
			((D3DSTATE*)g_d3dWritePtr)->dwArg = 8;
		g_d3dWritePtr += sizeof(D3DSTATE);
		((D3DSTATE*)g_d3dWritePtr)->dwState = D3DRENDERSTATE_ZWRITEENABLE;
		if ((flags & STD3D_RS_Z_WRITE_ENABLE) != 0)
			((D3DSTATE*)g_d3dWritePtr)->dwArg = 1;
		else
			((D3DSTATE*)g_d3dWritePtr)->dwArg = 0;
		g_d3dWritePtr += sizeof(D3DSTATE);
	}

	if (((unsigned int)(flags ^ g_d3dStateFlags) &
		 (STD3D_RS_TEXTURE_MAG_LINEAR | STD3D_RS_TEXTURE_MIN_LINEAR)) != 0) {
		((D3DINSTRUCTION*)g_d3dWritePtr)->bOpcode = D3DOP_STATERENDER;
		((D3DINSTRUCTION*)g_d3dWritePtr)->bSize = sizeof(D3DSTATE);
		((D3DINSTRUCTION*)g_d3dWritePtr)->wCount = 2;
		g_d3dWritePtr += sizeof(D3DINSTRUCTION);
		((D3DSTATE*)g_d3dWritePtr)->dwState = D3DRENDERSTATE_TEXTUREMAG;
		textureFilter = (flags & STD3D_RS_TEXTURE_MAG_LINEAR) != 0 ? 2 : 1;
		((D3DSTATE*)g_d3dWritePtr)->dwArg = textureFilter;
		g_d3dWritePtr += sizeof(D3DSTATE);
		((D3DSTATE*)g_d3dWritePtr)->dwState = D3DRENDERSTATE_TEXTUREMIN;
		textureFilter = (flags & STD3D_RS_TEXTURE_MIN_LINEAR) != 0 ? 2 : 1;
		((D3DSTATE*)g_d3dWritePtr)->dwArg = textureFilter;
		g_d3dWritePtr += sizeof(D3DSTATE);
	}

	if (((unsigned int)(flags ^ g_d3dStateFlags) & STD3D_RS_FOG_ENABLE) != 0) {
		((D3DINSTRUCTION*)g_d3dWritePtr)->bOpcode = D3DOP_STATERENDER;
		((D3DINSTRUCTION*)g_d3dWritePtr)->bSize = sizeof(D3DSTATE);
		if ((flags & STD3D_RS_FOG_ENABLE) != 0) {
			((D3DINSTRUCTION*)g_d3dWritePtr)->wCount = 5;
			g_d3dWritePtr += sizeof(D3DINSTRUCTION);
			((D3DSTATE*)g_d3dWritePtr)->dwState = D3DRENDERSTATE_FOGENABLE;
			((D3DSTATE*)g_d3dWritePtr)->dwArg = 1;
			g_d3dWritePtr += sizeof(D3DSTATE);
			((D3DSTATE*)g_d3dWritePtr)->dwState = D3DRENDERSTATE_FOGCOLOR;
			((D3DSTATE*)g_d3dWritePtr)->dwArg =
				(g_std3DFogColorGreen8 << 8) | (g_std3DFogColorRed8 << 16) | g_std3DFogColorBlue8;
			g_d3dWritePtr += sizeof(D3DSTATE);
			((D3DSTATE*)g_d3dWritePtr)->dwState = D3DRENDERSTATE_FOGTABLEMODE;
			((D3DSTATE*)g_d3dWritePtr)->dwArg = 3;
			g_d3dWritePtr += sizeof(D3DSTATE);
			((D3DSTATE*)g_d3dWritePtr)->dwState = D3DRENDERSTATE_FOGTABLESTART;
			((D3DSTATE*)g_d3dWritePtr)->dwArg = g_std3DFogTableStartBits;
			g_d3dWritePtr += sizeof(D3DSTATE);
			((D3DSTATE*)g_d3dWritePtr)->dwState = D3DRENDERSTATE_FOGTABLEEND;
			((D3DSTATE*)g_d3dWritePtr)->dwArg = g_std3DFogTableEndBits;
			g_d3dWritePtr += sizeof(D3DSTATE);
		} else {
			((D3DINSTRUCTION*)g_d3dWritePtr)->wCount = 1;
			g_d3dWritePtr += sizeof(D3DINSTRUCTION);
			((D3DSTATE*)g_d3dWritePtr)->dwState = D3DRENDERSTATE_FOGENABLE;
			((D3DSTATE*)g_d3dWritePtr)->dwArg = 0;
			g_d3dWritePtr += sizeof(D3DSTATE);
		}
	}

	g_d3dStateFlags = flags;
}

// FUNCTION: XVT 0x4B2540
int std3D_SetPaletteConversionSource(const void* paletteRgb888, uint8_t alpha) {
	memcpy(g_std3DPaletteConversionSourceRgb, paletteRgb888, sizeof(g_std3DPaletteConversionSourceRgb));
	if (g_pFmtRGB565->colorInfo.colorMode == STDCOLOR_RGB && g_pFmtRGB565->colorInfo.bpp == 16) {
		std3D_BuildColormapOpaque((uint8_t*)paletteRgb888, g_std3DPaletteScratch16, &g_pFmtRGB565->colorInfo);
	}
	if (g_pStd3DCurDevice->caps.bAlphaTexture != 0) {
		if (g_pFmtRGBA1555->colorInfo.colorMode == STDCOLOR_RGBA && g_pFmtRGBA1555->colorInfo.bpp == 16) {
			std3D_BuildColormapColorKey((uint8_t*)paletteRgb888, g_texConvBuf1555,
										&g_pFmtRGBA1555->colorInfo);
		}
		if (g_pStd3DCurDevice->caps.bAlphaBlend == 0 &&
			g_pFmtRGBA4444->colorInfo.colorMode == STDCOLOR_RGBA && g_pFmtRGBA4444->colorInfo.bpp == 16) {
			std3D_BuildColormapAlpha((uint8_t*)paletteRgb888, g_texConvBuf4444, &g_pFmtRGBA4444->colorInfo,
									 alpha);
		}
	}
	return 1;
}

// FUNCTION: XVT 0x4B25E0
void std3D_ClampTextureDimensions(int srcWidth, int srcHeight, int* outWidth, int* outHeight) {
	unsigned int width;
	unsigned int height;

	if ((uint32_t)srcWidth >= 1) {
		width = (uint32_t)g_std3DMaxTextureWidth;
		if (width >= (uint32_t)srcWidth) {
			width = (uint32_t)srcWidth;
		}
	} else {
		width = 1;
	}

	if ((uint32_t)srcHeight >= 1) {
		height = (uint32_t)g_std3DMaxTextureWidth;
		if (height >= (uint32_t)srcHeight) {
			height = (uint32_t)srcHeight;
		}
	} else {
		height = 1;
	}

	if (width < (unsigned int)g_std3DMinTextureWidth || height < (unsigned int)g_std3DMinTextureHeight ||
		(g_pStd3DCurDevice->caps.bSquareOnlyTexture && width != height)) {
		if (g_pStd3DCurDevice->caps.bSquareOnlyTexture && width != height) {
			if (height <= width) {
				height = width;
			}
			width = height;
		} else {
			unsigned int minWidth;

			minWidth = (unsigned int)g_std3DMinTextureWidth;
			if (width <= minWidth) {
				width = minWidth;
			}
			if (height <= (uint32_t)g_std3DMinTextureHeight) {
				height = (uint32_t)g_std3DMinTextureHeight;
			}
		}
		*outWidth = (int)width;
		*outHeight = (int)height;
		return;
	}

	*outWidth = (int)width;
	*outHeight = (int)height;
}

// FUNCTION: XVT 0x4B2680
int std3D_CreateMipSurface(Std3DVBuffer* source, Std3DTexCacheNode* node, int textureFormatMode,
						   int alphaMask) {
	IDirectDrawSurface* sourceSurface;
	unsigned int width;
	unsigned int height;
	unsigned int textureSize;
	IDirect3DTexture* sourceTexture;
	IDirect3DTexture* destinationTexture;
	IDirectDrawSurface* destinationSurface;
	Std3DVBuffer* temporaryBuffer;
	Std3DVBuffer* uploadBuffer;
	unsigned int textureHandle;
	DDCOLORKEY colorKey;
	Std3DRasterInfo resizedRaster;
	DDSURFACEDESC lockedDesc;
	DDSURFACEDESC surfaceDesc;
	int result;

	sourceSurface = NULL;
	destinationSurface = NULL;
	sourceTexture = NULL;
	destinationTexture = NULL;
	uploadBuffer = source;
	temporaryBuffer = NULL;
	width = source->raster.width;
	if (width >= 1) {
		if (width >= 256)
			width = 256;
	} else {
		width = 1;
	}
	height = source->raster.height;
	if (height >= 1) {
		if (height >= 256)
			height = 256;
	} else {
		height = 1;
	}

	if (width < (unsigned int)g_std3DMinTextureWidth || height < (unsigned int)g_std3DMinTextureHeight ||
		(g_pStd3DCurDevice->caps.bSquareOnlyTexture && width != height)) {
		int targetWidth;
		int targetHeight;
		unsigned int horizontalCopies;
		unsigned int verticalCopies;
		unsigned int destinationX;
		unsigned int destinationY;

		resizedRaster = source->raster;
		if (g_pStd3DCurDevice->caps.bSquareOnlyTexture && width != height) {
			targetWidth = (int)width;
			if ((unsigned int)targetWidth <= height)
				targetWidth = (int)height;
			targetHeight = targetWidth;
		} else {
			targetWidth = (int)width;
			if ((unsigned int)targetWidth <= (unsigned int)g_std3DMinTextureWidth)
				targetWidth = g_std3DMinTextureWidth;
			targetHeight = g_std3DMinTextureHeight;
			if ((unsigned int)targetHeight <= height)
				targetHeight = (int)height;
		}
		horizontalCopies = (unsigned int)((double)(unsigned int)targetWidth / (double)width + 0.5);
		verticalCopies = (unsigned int)((double)(unsigned int)targetHeight / (double)height + 0.5);
		resizedRaster.width = horizontalCopies * resizedRaster.width;
		resizedRaster.height = verticalCopies * resizedRaster.height;
		temporaryBuffer = std3D_AllocVBuffer(&resizedRaster, 0, 0, 0);
		destinationY = 0;
		while (verticalCopies != 0) {
			destinationX = 0;
			for (targetWidth = horizontalCopies; targetWidth != 0; --targetWidth) {
				std3D_BlitVBuffer(temporaryBuffer, source, (int)destinationX, (int)destinationY, 0, 1);
				destinationX += width;
			}
			destinationY += height;
			--verticalCopies;
		}
		uploadBuffer = temporaryBuffer;
		width = resizedRaster.width;
		height = resizedRaster.height;
	}

	textureSize = width * height;
	if (textureFormatMode && g_pStd3DCurDevice->caps.bAlphaTexture) {
		node->usesAlphaFormat = 1;
		if (alphaMask) {
			surfaceDesc = g_pFmtRGBA4444->ddsd;
			DebugPrintf("Using D3D texture format #%d.\n", g_fmtIdxRGBA4444, 0, 0, 0);
		} else {
			surfaceDesc = g_pFmtRGBA1555->ddsd;
			DebugPrintf("Using D3D texture format #%d.\n", g_fmtIdxRGBA1555, 0, 0, 0);
		}
	} else if (alphaMask) {
		node->usesAlphaFormat = 1;
		surfaceDesc = g_pFmtRGBA4444->ddsd;
		DebugPrintf("Using D3D texture format #%d.\n", g_fmtIdxRGBA4444, 0, 0, 0);
	} else {
		node->usesAlphaFormat = 0;
		surfaceDesc = g_pFmtRGB565->ddsd;
		DebugPrintf("Using D3D texture format #%d.\n", g_fmtIdxRGB565, 0, 0, 0);
	}
	surfaceDesc.dwWidth = width;
	surfaceDesc.dwHeight = height;
	surfaceDesc.dwSize = sizeof(surfaceDesc);
	surfaceDesc.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT;
	surfaceDesc.ddsCaps.dwCaps = DDSCAPS_TEXTURE | DDSCAPS_SYSTEMMEMORY;
	do {
		result =
			g_std3DDirectDraw->lpVtbl->CreateSurface(g_std3DDirectDraw, &surfaceDesc, &sourceSurface, NULL);
		if (result) {
			DebugPrintf("Error %s when creating the DirectDraw source surface.\n",
						std3D_LookupErrorString(result, g_std3DErrorStringTable, 121), 0, 0, 0);
			sourceSurface = NULL;
			break;
		}

		memset(&lockedDesc, 0, sizeof(lockedDesc));
		lockedDesc.dwSize = sizeof(lockedDesc);
		result = sourceSurface->lpVtbl->Lock(sourceSurface, NULL, &lockedDesc, DDLOCK_WAIT, NULL);
		if (result) {
			DebugPrintf("Error %s when locking the DDSurface source buffer.\n",
						std3D_LookupErrorString(result, g_std3DErrorStringTable, 121), 0, 0, 0);
			break;
		}

		switch (uploadBuffer->raster.colorMode) {
			case STDCOLOR_PAL: {
				unsigned int row;

				std3D_LockVBuffer(uploadBuffer);
				if (textureFormatMode && g_pStd3DCurDevice->caps.bAlphaTexture) {
					if (alphaMask) {
						for (row = 0; row < height; ++row) {
							uint16_t* destinationPixels;
							uint8_t* sourcePixels;
							unsigned int remaining;

							sourcePixels =
								(uint8_t*)uploadBuffer->pixels + row * uploadBuffer->raster.rowPitch;
							destinationPixels =
								(uint16_t*)((uint8_t*)lockedDesc.lpSurface + row * lockedDesc.lPitch);
							if (width != 0) {
								remaining = width;
								do {
									*destinationPixels++ = g_texConvBuf4444[*sourcePixels++];
									--remaining;
								} while (remaining != 0);
							}
						}
					} else {
						for (row = 0; row < height; ++row) {
							unsigned int remaining;
							uint16_t* destinationPixels;
							uint8_t* sourcePixels;

							sourcePixels =
								(uint8_t*)uploadBuffer->pixels + row * uploadBuffer->raster.rowPitch;
							destinationPixels =
								(uint16_t*)((uint8_t*)lockedDesc.lpSurface + row * lockedDesc.lPitch);
							if (width != 0) {
								remaining = width;
								do {
									*destinationPixels++ = g_texConvBuf1555[*sourcePixels++];
									--remaining;
								} while (remaining != 0);
							}
						}
					}
				} else if (alphaMask) {
					for (row = 0; row < height; ++row) {
						unsigned int remaining;
						uint8_t* sourcePixels =
							(uint8_t*)uploadBuffer->pixels + row * uploadBuffer->raster.rowPitch;
						uint16_t* destinationPixels =
							(uint16_t*)((uint8_t*)lockedDesc.lpSurface + row * lockedDesc.lPitch);
						if (width != 0) {
							remaining = width;
							do {
								*destinationPixels++ = g_texConvBuf4444[*sourcePixels++];
								--remaining;
							} while (remaining != 0);
						}
					}
				} else {
					for (row = 0; row < height; ++row) {
						unsigned int remaining;
						uint8_t* sourcePixels =
							(uint8_t*)uploadBuffer->pixels + row * uploadBuffer->raster.rowPitch;
						uint16_t* destinationPixels =
							(uint16_t*)((uint8_t*)lockedDesc.lpSurface + row * lockedDesc.lPitch);
						if (width != 0) {
							remaining = width;
							do {
								*destinationPixels++ = g_std3DPaletteScratch16[*sourcePixels++];
								--remaining;
							} while (remaining != 0);
						}
					}
				}
				std3D_UnlockVBuffer(uploadBuffer);
				break;
			}
			case STDCOLOR_RGB:
			case STDCOLOR_RGBA: {
				unsigned int row;

				std3D_LockVBuffer(uploadBuffer);
				for (row = 0; row < height; ++row) {
					const uint8_t* sourcePixels =
						(const uint8_t*)uploadBuffer->pixels + row * uploadBuffer->raster.rowPitch;
					uint8_t* destinationPixels = (uint8_t*)lockedDesc.lpSurface + row * lockedDesc.lPitch;
					memcpy(destinationPixels, sourcePixels, width * 2);
				}
				std3D_UnlockVBuffer(uploadBuffer);
				break;
			}
			default:
				break;
		}

		result = sourceSurface->lpVtbl->Unlock(sourceSurface, NULL);
		if (result) {
			DebugPrintf("Error %s when unlocking the DDSurface source buffer.\n",
						std3D_LookupErrorString(result, g_std3DErrorStringTable, 121), 0, 0, 0);
			break;
		}

		if (textureFormatMode && !g_pStd3DCurDevice->caps.bAlphaTexture) {
			switch (uploadBuffer->raster.colorMode) {
				case STDCOLOR_PAL:
					colorKey.dwColorSpaceLowValue = g_std3DPaletteScratch16[0];
					colorKey.dwColorSpaceHighValue = g_std3DPaletteScratch16[0];
					break;
				case STDCOLOR_RGB:
					colorKey.dwColorSpaceLowValue = uploadBuffer->transparentColor;
					colorKey.dwColorSpaceHighValue = uploadBuffer->transparentColor;
					break;
#ifdef XVT_MODERN
				default:
					colorKey.dwColorSpaceLowValue = 0;
					colorKey.dwColorSpaceHighValue = 0;
					break;
#endif
			}
			(void)sourceSurface->lpVtbl->SetColorKey(sourceSurface, DDCKEY_SRCBLT, &colorKey);
		}

		result = sourceSurface->lpVtbl->QueryInterface(sourceSurface, &CLSID_IDirect3DTexture,
													   (void**)&sourceTexture);
		if (result) {
			DebugPrintf("Error %s creating Direct3D source texture.\n",
						std3D_LookupErrorString(result, g_std3DErrorStringTable, 121), 0, 0, 0);
			sourceTexture = NULL;
			break;
		}
		result = sourceSurface->lpVtbl->GetSurfaceDesc(sourceSurface, &surfaceDesc);
		if (result) {
			DebugPrintf("Error %s get surface description.\n",
						std3D_LookupErrorString(result, g_std3DErrorStringTable, 121), 0, 0, 0);
#ifndef XVT_MODERN
			sourceTexture = NULL;
#endif
			break;
		}

		node->ddsd = surfaceDesc;
		surfaceDesc.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT;
		surfaceDesc.ddsCaps.dwCaps = DDSCAPS_TEXTURE | DDSCAPS_VIDEOMEMORY | DDSCAPS_ALLOCONLOAD;
		result = g_std3DDirectDraw->lpVtbl->CreateSurface(g_std3DDirectDraw, &surfaceDesc,
														  &destinationSurface, NULL);
		if (result) {
			if (result != -2005532292) {
				DebugPrintf("Error %s creating texture surface.\n",
							std3D_LookupErrorString(result, g_std3DErrorStringTable, 121), 0, 0, 0);
				break;
			}
			DebugPrintf("Error %s Creating surface.\n",
						std3D_LookupErrorString(result, g_std3DErrorStringTable, 121), 0, 0, 0);
			DebugPrintf("Assuming texture ram overflow - PURGING.\n", 0, 0, 0, 0);
			{
				int created = 0;
				Std3DTexCacheNode* candidate = g_pTexCacheHead;

				while (!created) {
					unsigned int freed = 0;

					while (freed < textureSize && candidate != NULL &&
						   candidate->cacheFrameTag != g_std3DTextureFrameTag) {
						candidate->pCachedSurface->lpVtbl->Release(candidate->pCachedSurface);
						candidate->pCachedTexture->lpVtbl->Release(candidate->pCachedTexture);
						candidate->bCached = 0;
						freed += candidate->byteSize;
						std3D_CacheListRemove(candidate);
						candidate = candidate->pNext;
					}
					if (freed < textureSize) {
						DebugPrintf("WARNING: Scene texture overflow occurred!!!.\n", 0, 0, 0, 0);
						destinationSurface = NULL;
						break;
					}
					result = g_std3DDirectDraw->lpVtbl->CreateSurface(g_std3DDirectDraw, &surfaceDesc,
																	  &destinationSurface, NULL);
					if (!result) {
						created = 1;
						DebugPrintf("Success adding new texture after purge.\n", 0, 0, 0, 0);
					} else if (result != -2005532292) {
						DebugPrintf("Error %s creating texture surface.\n",
									std3D_LookupErrorString(result, g_std3DErrorStringTable, 121), 0, 0, 0);
						destinationSurface = NULL;
						break;
					}
				}
			}
			if (destinationSurface == NULL)
				break;
		}

		result = destinationSurface->lpVtbl->QueryInterface(destinationSurface, &CLSID_IDirect3DTexture,
															(void**)&destinationTexture);
		if (result) {
			DebugPrintf("Error %s creating Direct3D dest texture.\n",
						std3D_LookupErrorString(result, g_std3DErrorStringTable, 121), 0, 0, 0);
			destinationTexture = NULL;
			break;
		}
		result = destinationTexture->lpVtbl->Load(destinationTexture, sourceTexture);
		if (result) {
			DebugPrintf("Error %s loading Direct3D dest texture from source.\n",
						std3D_LookupErrorString(result, g_std3DErrorStringTable, 121), 0, 0, 0);
			break;
		}
		result = destinationTexture->lpVtbl->GetHandle(destinationTexture, g_d3dDevice, &textureHandle);
		if (result) {
			DebugPrintf("Error %s when getting texture handle.\n",
						std3D_LookupErrorString(result, g_std3DErrorStringTable, 121), 0, 0, 0);
			textureHandle = 0;
		}

		sourceTexture->lpVtbl->Release(sourceTexture);
		sourceTexture = NULL;
		sourceSurface->lpVtbl->Release(sourceSurface);
		sourceSurface = NULL;
		if (temporaryBuffer != NULL)
			std3D_FreeVBuffer(temporaryBuffer);
		node->pCachedTexture = destinationTexture;
		node->pCachedSurface = destinationSurface;
		node->texHandle = textureHandle;
		node->width = width;
		node->height = height;
		node->bCached = 1;
		node->byteSize = textureSize;
		node->cacheFrameTag = g_std3DTextureFrameTag;
		std3D_CacheListAppend(node);
		return 1;
	} while (0);

	if (sourceSurface != NULL)
		sourceSurface->lpVtbl->Release(sourceSurface);
	if (sourceTexture != NULL)
		sourceTexture->lpVtbl->Release(sourceTexture);
	if (temporaryBuffer != NULL)
		std3D_FreeVBuffer(temporaryBuffer);
	if (destinationSurface != NULL)
		destinationSurface->lpVtbl->Release(destinationSurface);
	if (destinationTexture != NULL)
		destinationTexture->lpVtbl->Release(destinationTexture);
	node->pCachedTexture = NULL;
	node->pCachedSurface = NULL;
	node->texHandle = 0;
	node->bCached = 0;
	node->cacheFrameTag = 0;
	DebugPrintf("Done error exit from std3D_AddToTextureCache.\n", 0, 0, 0, 0);
	return 0;
}

// FUNCTION: XVT 0x4B3070
void std3D_FlushTextureCache(void) {
	Std3DTexCacheNode* node;

	node = g_pTexCacheHead;
	while (node != NULL) {
		Std3DTexCacheNode** nextLink;
		Std3DTexCacheNode* current;

		if (node->pCachedSurface != NULL) {
			node->pCachedSurface->lpVtbl->Release(node->pCachedSurface);
			node->pCachedSurface = NULL;
		}
		if (node->pCachedTexture != NULL) {
			node->pCachedTexture->lpVtbl->Release(node->pCachedTexture);
			node->pCachedTexture = NULL;
		}
		current = node;
		nextLink = &node->pNext;
		node->bCached = 0;
		node->cacheFrameTag = 0;
		node = *nextLink;
		*nextLink = NULL;
		current->pPrev = NULL;
	}

	g_pTexCacheHead = NULL;
	g_pTexCacheTail = NULL;
	g_texCacheCount = 0;
	g_pStd3DCurDevice->availableMemory = g_pStd3DCurDevice->totalMemory;
	g_std3DTextureFrameTag = 1;
}

// FUNCTION: XVT 0x4B30F0
void std3D_CacheListAppend(Std3DTexCacheNode* node) {
	Std3DTexCacheNode* previousTail;

	if (g_pTexCacheHead == 0) {
		g_pTexCacheTail = node;
		g_pTexCacheHead = node;
		node->pPrev = 0;
		node->pNext = 0;
	} else {
		g_pTexCacheTail->pNext = node;
		previousTail = g_pTexCacheTail;
		node->pNext = 0;
		node->pPrev = previousTail;
		g_pTexCacheTail = node;
	}

	++g_texCacheCount;
	g_pStd3DCurDevice->availableMemory -= node->byteSize;
}

// FUNCTION: XVT 0x4B3160
void std3D_CacheListRemove(Std3DTexCacheNode* node) {
	if (node == g_pTexCacheHead) {
		g_pTexCacheHead = node->pNext;
		if (g_pTexCacheHead != NULL) {
			g_pTexCacheHead->pPrev = NULL;
			if (g_pTexCacheHead->pNext == NULL)
				g_pTexCacheTail = g_pTexCacheHead;
		} else {
			g_pTexCacheTail = NULL;
		}
	} else if (node == g_pTexCacheTail) {
		g_pTexCacheTail = node->pPrev;
		g_pTexCacheTail->pNext = NULL;
	} else {
		node->pPrev->pNext = node->pNext;
		node->pNext->pPrev = node->pPrev;
	}

	--g_texCacheCount;
	g_pStd3DCurDevice->availableMemory += node->byteSize;
}

// FUNCTION: XVT 0x4B3210
int std3D_QueryTextureVidMem(unsigned int* totalBytes, unsigned int* freeBytes) {
	IDirectDraw* directDraw2 = 0;
	DDSCAPS caps;

	if (g_std3DDirectDraw->lpVtbl->QueryInterface(g_std3DDirectDraw, &CLSID_IDirectDraw2,
												  (void**)&directDraw2) != DX_DD_OK) {
		return 0;
	}

	caps.dwCaps = DDSCAPS_TEXTURE;
	if (directDraw2->lpVtbl->GetAvailableVidMem(directDraw2, &caps, totalBytes, freeBytes) != DX_DD_OK) {
		return 0;
	}

	directDraw2->lpVtbl->Release(directDraw2);
	return 1;
}

// FUNCTION: XVT 0x4B3280
void std3D_CacheTextureSurface(Std3DTexCacheNode* node) {
	node->cacheFrameTag = g_std3DTextureFrameTag;
	std3D_CacheListRemove(node);
	std3D_CacheListAppend(node);
}

// FUNCTION: XVT 0x4B32B0
int std3D_ClearZBuffer(void) {
	int rect[4];
	DDBLTFX effects;
	int result;

	memset(&effects, 0, sizeof(effects));
	effects.dwSize = sizeof(effects);
	effects.dwFillDepth = 0;
	if (g_std3DZBufferBitDepth != 16)
		effects.dwFillDepth = 0xFFFF;
	rect[0] = g_std3DQuadRect.x;
	rect[1] = g_std3DQuadRect.y;
	rect[2] = g_std3DQuadRect.x + g_std3DQuadRect.width;
	rect[3] = g_std3DQuadRect.y + g_std3DQuadRect.height;

	for (;;) {
		result = g_std3DZBufferSurfaceBlock.surface->lpVtbl->Blt(
			g_std3DZBufferSurfaceBlock.surface, rect, NULL, NULL, DDBLT_WAIT | DDBLT_DEPTHFILL, &effects);
		if (result == DX_DD_OK)
			return 1;
		if (result == DX_DDERR_SURFACELOST) {
			result = g_std3DZBufferSurfaceBlock.surface->lpVtbl->Restore(g_std3DZBufferSurfaceBlock.surface);
		}
		if (result != DX_DD_OK) {
			DebugPrintf("Error %s clearing zbuffer.\n",
						std3D_LookupErrorString(result, g_std3DErrorStringTable, 121), 0, 0, 0);
			return 0;
		}
	}
}

// FUNCTION: XVT 0x4B3380
int std3D_SelectBestDevice(Std3DDeviceCaps* arg1) {
	int bestMatchQuality;
	Std3DDevice* device;
	int deviceIndex;
	int requiredPerspective;
	int matchQuality;
	int requiredZBuffer;
	int bestDeviceIndex;

	if (g_std3DNumDevices == 0)
		return 0;
	bestMatchQuality = 0;
	device = g_std3DDevices;
	deviceIndex = 0;
	bestDeviceIndex = 0;
	if (g_std3DNumDevices > (unsigned int)deviceIndex) {
		requiredPerspective = arg1->bTexturePerspective;
		do {
			matchQuality = 0;
			if (requiredPerspective == 0 || device->caps.bTexturePerspective == requiredPerspective) {
				matchQuality = 1;
				requiredZBuffer = arg1->bHasZBuffer;
				if (requiredZBuffer == 0 || device->caps.bHasZBuffer == requiredZBuffer) {
					matchQuality = 2;
					if ((arg1->colorModelFlags & device->caps.colorModelFlags) != 0) {
						matchQuality = 3;
						if (device->caps.bHardware == arg1->bHardware) {
							DebugPrintf("Found a perfect device match #%d!\n", deviceIndex, 0, 0, 0);
							return deviceIndex;
						}
					}
				}
			}
			if (bestMatchQuality < matchQuality) {
				bestMatchQuality = matchQuality;
				bestDeviceIndex = deviceIndex;
			}
			++device;
			++deviceIndex;
		} while (g_std3DNumDevices > (unsigned int)deviceIndex);
	}
	DebugPrintf("Settling for a closest match #%d..\n", bestDeviceIndex, 0, 0, 0);
	return bestDeviceIndex;
}

// FUNCTION: XVT 0x4B3450
int std3D_FindClosestFormat(const ColorInfo* match, Std3DTexFmt* formats, unsigned int count) {
	int bestScore;
	Std3DTexFmt* format;
	int bestIndex;
	unsigned int matchScore;
	int score;

	if (count == 0) {
		return 0;
	}
	score = 0;
	bestScore = 0;
	format = formats;
	for (bestIndex = 0; (unsigned int)bestIndex < count; ++bestIndex) {
		matchScore = 0;
		if (format->colorInfo.colorMode == match->colorMode) {
			++matchScore;
			if (format->colorInfo.bpp == match->bpp) {
				++matchScore;
				switch (match->colorMode) {
					case STDCOLOR_RGB:
						if (format->colorInfo.redBPP == match->redBPP &&
							format->colorInfo.greenBPP == match->greenBPP &&
							format->colorInfo.blueBPP == match->blueBPP) {
							DebugPrintf("Found a perfect mode match #%d!\n", bestIndex, 0, 0, 0);
							return bestIndex;
						}
						break;
					case STDCOLOR_RGBA:
						if (format->colorInfo.colorMode == STDCOLOR_RGBA) {
							++matchScore;
						}
						if (format->colorInfo.redBPP == match->redBPP &&
							format->colorInfo.greenBPP == match->greenBPP &&
							format->colorInfo.blueBPP == match->blueBPP &&
							format->colorInfo.alphaBPP == match->alphaBPP) {
							DebugPrintf("Found a perfect mode match #%d!\n", bestIndex, 0, 0, 0);
							return bestIndex;
						}
						break;
					default:
						DebugPrintf("Found a perfect mode match #%d!\n", bestIndex, 0, 0, 0);
						return bestIndex;
				}
			}
		}
		if ((int)matchScore > score) {
			bestScore = bestIndex;
			score = matchScore;
		}
		++format;
	}
	DebugPrintf("Settling for a closest match #%d..\n", bestScore, 0, 0, 0);
	return bestScore;
}

// FUNCTION: XVT 0x4B3590
void std3D_DrawColorOverlay(void) {
	float maximum;
	uint8_t blue;
	uint8_t green;
	uint8_t red;
	uint8_t alpha;
	uint32_t packedColor;

	if (g_std3DColorOverlayEnabled == 0 ||
		(g_std3DColorOverlayRed == 0.0f && g_std3DColorOverlayGreen == 0.0f &&
		 g_std3DColorOverlayBlue == 0.0f))
		return;
	maximum = g_std3DColorOverlayRed >= g_std3DColorOverlayGreen ? g_std3DColorOverlayRed
																 : g_std3DColorOverlayGreen;
	maximum = g_std3DColorOverlayBlue >= maximum ? g_std3DColorOverlayBlue : maximum;
	red = (uint8_t)(g_std3DColorOverlayRed / maximum * 255.0f);
	green = (uint8_t)(g_std3DColorOverlayGreen / maximum * 255.0f);
	blue = (uint8_t)(g_std3DColorOverlayBlue / maximum * 255.0f);
	if (g_pStd3DCurDevice->caps.bStippledShade != 0) {
		float alphaScale;
		float upperClampedAlpha;

		alphaScale = maximum * 0.736f;
		if (alphaScale >= 0.0f) {
			if (alphaScale > 255.0f)
				upperClampedAlpha = 255.0f;
			else
				upperClampedAlpha = alphaScale;
			alphaScale = upperClampedAlpha;
		} else {
			alphaScale = 0.0f;
		}
		alpha = (uint8_t)alphaScale;
	} else {
		float alphaScale;
		float upperClampedAlpha;

		alphaScale = maximum * 0.9f;
		if (alphaScale >= 0.0f) {
			if (alphaScale > 255.0f)
				upperClampedAlpha = 255.0f;
			else
				upperClampedAlpha = alphaScale;
			alphaScale = upperClampedAlpha;
		} else {
			alphaScale = 0.0f;
		}
		alpha = (uint8_t)alphaScale;
	}
	if (alpha == 0)
		return;
	if (g_pStd3DCurDevice->caps.bAlphaBlend != 0) {
		packedColor =
			(uint32_t)blue | ((uint32_t)red << 16) | (((uint32_t)green | ((uint32_t)alpha << 16)) << 8);
		g_std3DQuadVerts[0].color = packedColor;
		g_std3DQuadVerts[1].color = packedColor;
		g_std3DQuadVerts[2].color = packedColor;
		g_std3DQuadVerts[3].color = packedColor;
		std3D_StartScene();
		std3D_LockExecuteBuffer();
		std3D_AddVertices(g_std3DQuadVerts, 4);
		std3D_BeginInstructions();
		std3D_AddTriangles(g_std3DViewportQuadTriangles, 2);
		std3D_ExecuteBuffer();
		std3D_EndScene();
	} else {
		uint16_t blueColor;
		uint16_t color;

		color = (uint16_t)(green >> g_pFmtRGBA4444->colorInfo.greenPosShiftRight)
				<< g_pFmtRGBA4444->colorInfo.greenPosShift;
		color |= (uint16_t)(red >> g_pFmtRGBA4444->colorInfo.redPosShiftRight)
				 << g_pFmtRGBA4444->colorInfo.redPosShift;
		color |= (uint16_t)(alpha >> g_pFmtRGBA4444->colorInfo.alphaPosShiftRight)
				 << g_pFmtRGBA4444->colorInfo.alphaPosShift;
		blueColor = (uint16_t)(blue >> g_pFmtRGBA4444->colorInfo.bluePosShiftRight)
					<< g_pFmtRGBA4444->colorInfo.bluePosShift;
		std3D_FillVBuffer(g_pStd3DVBuffer, (uint16_t)(color | blueColor), 0);
		std3D_StartScene();
		std3D_LockExecuteBuffer();
		std3D_CreateMipSurface(g_pStd3DVBuffer, &g_std3DColorOverlayTexNode, 0, 1);
		g_std3DQuadVerts[0].color = UINT32_MAX;
		g_std3DQuadVerts[1].color = UINT32_MAX;
		g_std3DQuadVerts[2].color = UINT32_MAX;
		g_std3DQuadVerts[3].color = UINT32_MAX;
		g_std3DViewportQuadTriangles[0].texture = &g_std3DColorOverlayTexNode;
		g_std3DViewportQuadTriangles[1].texture = &g_std3DColorOverlayTexNode;
		std3D_AddVertices(g_std3DQuadVerts, 4);
		std3D_BeginInstructions();
		std3D_AddTriangles(g_std3DViewportQuadTriangles, 2);
		std3D_ExecuteBuffer();
		std3D_EndScene();
		g_std3DColorOverlayTexNode.pCachedSurface->lpVtbl->Release(g_std3DColorOverlayTexNode.pCachedSurface);
		g_std3DColorOverlayTexNode.pCachedTexture->lpVtbl->Release(g_std3DColorOverlayTexNode.pCachedTexture);
		g_std3DColorOverlayTexNode.bCached = 0;
		std3D_CacheListRemove(&g_std3DColorOverlayTexNode);
	}
}

// FUNCTION: XVT 0x4B3890
int std3D_BuildViewportQuad(const Std3DViewportRect* rect) {
	g_std3DQuadRect = *rect;
	memset(g_std3DQuadVerts, 0, sizeof(g_std3DQuadVerts));

	g_std3DQuadVerts[0].sx = (float)g_std3DQuadRect.x;
	g_std3DQuadVerts[0].sy = (float)g_std3DQuadRect.y;
	g_std3DQuadVerts[1].sx = (float)(g_std3DQuadRect.x + g_std3DQuadRect.width);
	g_std3DQuadVerts[1].sy = (float)g_std3DQuadRect.y;
	g_std3DQuadVerts[2].sx = (float)(g_std3DQuadRect.x + g_std3DQuadRect.width);
	g_std3DQuadVerts[2].sy = (float)(g_std3DQuadRect.y + g_std3DQuadRect.height);
	g_std3DQuadVerts[3].sx = (float)g_std3DQuadRect.x;
	g_std3DQuadVerts[3].sy = (float)(g_std3DQuadRect.y + g_std3DQuadRect.height);

	g_std3DViewportQuadTriangles[0].v1 = 1;
	g_std3DViewportQuadTriangles[0].v0 = 0;
	g_std3DViewportQuadTriangles[0].v2 = 2;
	g_std3DViewportQuadTriangles[0].texture = NULL;
	g_std3DViewportQuadTriangles[0].flags = STD3D_RS_ALPHA_BLEND | STD3D_RS_MONO_DISABLE;
	g_std3DViewportQuadTriangles[1].v0 = 0;
	g_std3DViewportQuadTriangles[1].v1 = 2;
	g_std3DViewportQuadTriangles[1].texture = NULL;
	g_std3DViewportQuadTriangles[1].v2 = 3;
	g_std3DViewportQuadTriangles[1].flags = STD3D_RS_ALPHA_BLEND | STD3D_RS_MONO_DISABLE;

	return 0;
}

// FUNCTION: XVT 0x4B3980
int std3D_SetInitialRenderState(void) {
	IDirect3DExecuteBuffer* executeBuffer;
	D3DEXECUTEBUFFERDESC descriptor;
	D3DEXECUTEDATA executeData;
	D3DINSTRUCTION* instruction;
	D3DSTATE* state;
	uint8_t* base;
	uint8_t* cursor;
	int result;
	int zEnabled;

	executeBuffer = NULL;
	memset(&descriptor, 0, sizeof(descriptor));
	descriptor.dwSize = 20;
	descriptor.dwFlags = 1;
	descriptor.dwBufferSize = 4096;
	result = g_d3dDevice->lpVtbl->CreateExecuteBuffer(g_d3dDevice, &descriptor, &executeBuffer, NULL);
	if (result != 0) {
		DebugPrintf("Error %s creating D3D Execute buffer.\n",
					std3D_LookupErrorString(result, g_std3DErrorStringTable, 121), 0, 0, 0);
	}
	result = executeBuffer->lpVtbl->Lock(executeBuffer, &descriptor);
	if (result != 0) {
		DebugPrintf(g_std3DLockExecuteBufferErrorFormat,
					std3D_LookupErrorString(result, g_std3DErrorStringTable, 121), 0, 0, 0);
	}

	memset(descriptor.lpData, 0, 4096);
	base = (uint8_t*)descriptor.lpData;
	instruction = (D3DINSTRUCTION*)base;
	instruction->bOpcode = D3DOP_STATERENDER;
	instruction->bSize = sizeof(D3DSTATE);
	instruction->wCount = 25;
	state = (D3DSTATE*)(instruction + 1);

	state->dwState = D3DRENDERSTATE_TEXTUREPERSPECTIVE;
	state->dwArg = g_std3DCapFlags & 1;
	++state;
	state->dwState = D3DRENDERSTATE_TEXTUREMAG;
	state->dwArg = (g_std3DCapFlags & 0x80) != 0 ? 2 : 1;
	++state;
	state->dwState = D3DRENDERSTATE_TEXTUREMIN;
	state->dwArg = (g_std3DCapFlags & 0x100) != 0 ? 2 : 1;
	++state;
	state->dwState = D3DRENDERSTATE_SUBPIXEL;
	state->dwArg = (g_std3DCapFlags & 0x10) != 0;
	++state;
	state->dwState = D3DRENDERSTATE_SUBPIXELX;
	state->dwArg = (g_std3DCapFlags & 0x20) != 0;
	++state;
	state->dwState = D3DRENDERSTATE_WRAPU;
	state->dwArg = 0;
	++state;
	state->dwState = D3DRENDERSTATE_WRAPV;
	state->dwArg = 0;
	++state;
	state->dwState = D3DRENDERSTATE_BLENDENABLE;
	state->dwArg = (g_std3DCapFlags & 0x600) != 0;
	++state;
	if ((g_std3DCapFlags & 0x600) != 0) {
		if ((g_std3DCapFlags & 0x400) != 0) {
			state->dwState = D3DRENDERSTATE_TEXTUREMAPBLEND;
			state->dwArg = 4;
		} else {
			state->dwState = D3DRENDERSTATE_TEXTUREMAPBLEND;
			state->dwArg = 2;
		}
		++state;
		state->dwState = D3DRENDERSTATE_SRCBLEND;
		state->dwArg = 5;
		++state;
		state->dwState = D3DRENDERSTATE_DESTBLEND;
		state->dwArg = 6;
		++state;
	} else {
		state->dwState = D3DRENDERSTATE_TEXTUREMAPBLEND;
		state->dwArg = 2;
		++state;
		state->dwState = D3DRENDERSTATE_SRCBLEND;
		state->dwArg = 2;
		++state;
		state->dwState = D3DRENDERSTATE_DESTBLEND;
		state->dwArg = 1;
		++state;
	}
	state->dwState = D3DRENDERSTATE_ALPHATESTENABLE;
	state->dwArg = 1;
	++state;
	state->dwState = D3DRENDERSTATE_ALPHAFUNC;
	state->dwArg = 6;
	++state;
	if (g_pStd3DCurDevice->caps.bStippledShade != 0) {
		state->dwState = D3DRENDERSTATE_STIPPLEDALPHA;
		state->dwArg = 1;
	} else {
		state->dwState = D3DRENDERSTATE_STIPPLEDALPHA;
		state->dwArg = 0;
	}
	++state;
	state->dwState = D3DRENDERSTATE_SHADEMODE;
	state->dwArg = 2;
	++state;
	zEnabled = 1;
	state->dwState = D3DRENDERSTATE_MONOENABLE;
	state->dwArg = (g_std3DCapFlags & 0x8000) == 0;
	++state;
	state->dwState = D3DRENDERSTATE_SPECULARENABLE;
	state->dwArg = (g_std3DCapFlags & 4) != 0;
	++state;
	state->dwState = D3DRENDERSTATE_FOGENABLE;
	state->dwArg = (g_std3DCapFlags & 0x40) != 0;
	++state;
	state->dwState = D3DRENDERSTATE_FILLMODE;
	state->dwArg = 3;
	++state;
	state->dwState = D3DRENDERSTATE_DITHERENABLE;
	state->dwArg = (g_std3DCapFlags & 2) != 0;
	++state;
	state->dwState = D3DRENDERSTATE_ANTIALIAS;
	state->dwArg = (g_std3DCapFlags & 8) != 0;
	++state;

	if (g_std3DZBufferEnabled != 0) {
		DebugPrintf("Enabling Z buffer render state.\n", 0, 0, 0, 0);
	} else {
		zEnabled = 0;
		DebugPrintf("Disabling Z buffer render state.\n", 0, 0, 0, 0);
	}
	state->dwState = D3DRENDERSTATE_ZENABLE;
	state->dwArg = zEnabled;
	++state;
	state->dwState = D3DRENDERSTATE_ZWRITEENABLE;
	state->dwArg = zEnabled;
	++state;
	state->dwState = D3DRENDERSTATE_ZFUNC;
	state->dwArg = std3D_MapZCmpFunc(g_std3DZBufferBitDepth);
	++state;
	state->dwState = D3DRENDERSTATE_CULLMODE;
	state->dwArg = 1;
	++state;

	instruction = (D3DINSTRUCTION*)state;
	instruction->bOpcode = D3DOP_EXIT;
	instruction->bSize = 0;
	instruction->wCount = 0;
	cursor = (uint8_t*)(instruction + 1);
	result = executeBuffer->lpVtbl->Unlock(executeBuffer);
	if (result != 0) {
		DebugPrintf("Error %s unlocking D3D Execute buffer.\n",
					std3D_LookupErrorString(result, g_std3DErrorStringTable, 121), 0, 0, 0);
	}

	memset(&executeData, 0, sizeof(executeData));
	executeData.dwSize = sizeof(executeData);
	executeData.dwInstructionOffset = 0;
	executeData.dwInstructionLength = (uint32_t)(cursor - base);
	executeBuffer->lpVtbl->SetExecuteData(executeBuffer, &executeData);
	result = g_d3dDevice->lpVtbl->BeginScene(g_d3dDevice);
	if (result != 0) {
		DebugPrintf(g_std3DBeginSceneErrorFormat,
					std3D_LookupErrorString(result, g_std3DErrorStringTable, 121), 0, 0, 0);
	}
	result = g_d3dDevice->lpVtbl->Execute(g_d3dDevice, executeBuffer, g_d3dViewport, D3DEXECUTE_UNCLIPPED);
	if (result != 0) {
		DebugPrintf("Error %s executing buffer.\n",
					std3D_LookupErrorString(result, g_std3DErrorStringTable, 121), 0, 0, 0);
	}
	result = g_d3dDevice->lpVtbl->EndScene(g_d3dDevice);
	if (result != 0) {
		DebugPrintf(g_std3DEndSceneErrorFormat, std3D_LookupErrorString(result, g_std3DErrorStringTable, 121),
					0, 0, 0);
	}
	executeBuffer->lpVtbl->Release(executeBuffer);
	g_d3dStateFlags = (Std3DRenderStateFlags)g_std3DCapFlags;
	DebugPrintf("Initial render state set.\n", 0, 0, 0, 0);
	return 1;
}

// FUNCTION: XVT 0x4B3E30
int std3D_CreateViewport(int width, int height) {
	int result;
	Std3DViewportRect rect;
	D3DVIEWPORT viewport;

	result = g_lpD3D->lpVtbl->CreateViewport(g_lpD3D, &g_d3dViewport, NULL);
	if (result != 0) {
		DebugPrintf("Error %s when creating D3D viewport.\n",
					std3D_LookupErrorString(result, g_std3DErrorStringTable, 121), 0, 0, 0);
		return 0;
	}
	result = g_d3dDevice->lpVtbl->AddViewport(g_d3dDevice, g_d3dViewport);
	if (result != 0) {
		DebugPrintf("Error %s when adding the D3D viewport to the device.\n",
					std3D_LookupErrorString(result, g_std3DErrorStringTable, 121), 0, 0, 0);
		return 0;
	}

	memset(&viewport, 0, sizeof(viewport));
	viewport.dwY = 0;
	viewport.dwX = 0;
	viewport.dwWidth = width;
	viewport.dwHeight = height;
	viewport.dwSize = sizeof(viewport);
	viewport.dvScaleX = (float)(unsigned int)width * 0.5f;
	viewport.dvScaleY = (float)(unsigned int)height * 0.5f;
	viewport.dvMaxX = (float)(unsigned int)width / (viewport.dvScaleX * 2.0f);
	viewport.dvMaxY = (float)(unsigned int)height / (viewport.dvScaleY * 2.0f);
	result = g_d3dViewport->lpVtbl->SetViewport(g_d3dViewport, &viewport);
	if (result != 0) {
		DebugPrintf("Error %s when creating D3D viewport.\n",
					std3D_LookupErrorString(result, g_std3DErrorStringTable, 121), 0, 0, 0);
		return 0;
	}

	rect.x = 0;
	rect.y = 0;
	rect.width = width;
	rect.height = height;
	std3D_BuildViewportQuad(&rect);
	DebugPrintf("Viewport created successfully.\n", 0, 0, 0, 0);
	return 1;
}

// FUNCTION: XVT 0x4B3FC0
int std3D_CreateZBuffer(int width, int height) {
	unsigned int zBufferBitDepth;
	HRESULT result;

	g_std3DZBufferTargetScratch.storageType = 1;
	g_std3DZBufferTargetScratch.bVideoMemory = 0;
	memcpy(&g_std3DZBufferTargetScratch.raster, g_pStd3DRenderTarget,
		   sizeof(g_std3DZBufferTargetScratch.raster));
	g_std3DZBufferTargetScratch.unk58 = 0;
	g_pStd3DZBufferState = &g_std3DZBufferSurfaceBlock;
	g_std3DZBufferTargetScratch.pixels = NULL;

	memset(&g_pStd3DZBufferState->desc, 0, sizeof(g_pStd3DZBufferState->desc));
	g_pStd3DZBufferState->desc.dwSize = sizeof(g_pStd3DZBufferState->desc);
	g_pStd3DZBufferState->desc.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | 0x40;
	g_pStd3DZBufferState->desc.ddsCaps.dwCaps = DDSCAPS_ZBUFFER;
	g_pStd3DZBufferState->desc.dwWidth = width;
	g_pStd3DZBufferState->desc.dwHeight = height;
	if (g_pStd3DCurDevice->caps.bHardware != 0) {
		g_pStd3DZBufferState->desc.ddsCaps.dwCaps |= DDSCAPS_VIDEOMEMORY;
	} else {
		g_pStd3DZBufferState->desc.ddsCaps.dwCaps |= DDSCAPS_SYSTEMMEMORY;
	}

	zBufferBitDepth = g_pStd3DCurDevice->d3dDesc.dwDeviceZBufferBitDepth;
	if ((zBufferBitDepth & 0x100) != 0) {
		g_pStd3DZBufferState->desc.dwZBufferBitDepth = 32;
	} else if ((zBufferBitDepth & 0x400) != 0) {
		g_pStd3DZBufferState->desc.dwZBufferBitDepth = 16;
	} else if ((zBufferBitDepth & 0x800) != 0) {
		g_pStd3DZBufferState->desc.dwZBufferBitDepth = 8;
	} else {
		DebugPrintf("Error: unsupported zbuffer bit depth!\n", 0, 0, 0, 0);
		return 0;
	}
	DebugPrintf("ZBuffer depth: %d.\n", g_pStd3DZBufferState->desc.dwZBufferBitDepth, 0, 0, 0);

	result = g_std3DDirectDraw->lpVtbl->CreateSurface(g_std3DDirectDraw, &g_pStd3DZBufferState->desc,
													  &g_pStd3DZBufferState->surface, NULL);
	if (result != 0) {
		DebugPrintf("Error %s when creating zBuffer DDraw surface.\n",
					std3D_LookupErrorString(result, g_std3DErrorStringTable, 121), 0, 0, 0);
		return 0;
	}

	result =
		g_std3DRenderSurface->lpVtbl->AddAttachedSurface(g_std3DRenderSurface, g_pStd3DZBufferState->surface);
	if (result != 0) {
		DebugPrintf("Error %s when attaching zbuffer to backbuffer.\n",
					std3D_LookupErrorString(result, g_std3DErrorStringTable, 121), 0, 0, 0);
		return 0;
	}

	result = g_pStd3DZBufferState->surface->lpVtbl->GetSurfaceDesc(g_pStd3DZBufferState->surface,
																   &g_pStd3DZBufferState->desc);
	if (result != 0) {
		DebugPrintf("Error %s when getting zbuffer surface description.\n",
					std3D_LookupErrorString(result, g_std3DErrorStringTable, 121), 0, 0, 0);
		return 0;
	}

	if ((g_pStd3DZBufferState->desc.ddsCaps.dwCaps & DDSCAPS_VIDEOMEMORY) != 0) {
		g_std3DZBufferTargetScratch.bVideoMemory = 1;
	}
	DebugPrintf("ZBuffer in %s memory.\n", g_std3DZBufferTargetScratch.bVideoMemory != 0 ? "VIDEO" : "SYSTEM",
				0, 0, 0);
	DebugPrintf("ZBuffer created successfully.\n", 0, 0, 0, 0);
	return 1;
}

// FUNCTION: XVT 0x4B4210
HRESULT AERON_DXAPI std3D_EnumDevicesCallback(DxGuid* arg1, char* Source, char* arg3, D3DDEVICEDESC* arg4,
											  D3DDEVICEDESC* arg5, void* arg6) {
	Std3DDevice* device;
	unsigned int shadeCaps;
	(void)arg6;

	if (g_std3DNumDevices < 4) {

		device = &g_std3DDevices[g_std3DNumDevices];
		memcpy(&device->guid, arg1, sizeof(device->guid));
		strncpy(device->deviceDescription, Source, sizeof(device->deviceDescription));
		strncpy(device->deviceName, arg3, sizeof(device->deviceName));
		if (arg4->dcmColorModel != 0) {
			device->caps.bHardware = 1;
			memcpy(&device->d3dDesc, arg4, sizeof(device->d3dDesc));
		} else {
			device->caps.bHardware = 0;
			memcpy(&device->d3dDesc, arg5, sizeof(device->d3dDesc));
		}

		device->caps.colorModelFlags = 0;
		if ((device->d3dDesc.dcmColorModel & 2) != 0)
			device->caps.colorModelFlags = 2;
		if ((device->d3dDesc.dcmColorModel & 1) != 0)
			device->caps.colorModelFlags |= 1;
		device->caps.bTexturePerspective = device->d3dDesc.dpcTriCaps.dwTextureCaps & 1;
		device->caps.bHasZBuffer = device->d3dDesc.dwDeviceZBufferBitDepth != 0;
		device->caps.bSquareOnlyTexture = (device->d3dDesc.dpcTriCaps.dwTextureCaps & 0x20) != 0;
		device->caps.bAlphaTexture = (device->d3dDesc.dpcTriCaps.dwTextureCaps & 4) != 0;
		shadeCaps = device->d3dDesc.dpcTriCaps.dwShadeCaps;
		device->caps.bStippledShade = (shadeCaps & 0x1000) == 0 && (shadeCaps & 0x2000) != 0;
		device->caps.bAlphaBlend = ((device->d3dDesc.dpcTriCaps.dwTextureBlendCaps & 8) != 0 &&
									(device->d3dDesc.dpcTriCaps.dwShadeCaps & 0x4000) != 0) ||
								   device->caps.bStippledShade != 0;
		device->caps.bColorKeyTexture = (device->d3dDesc.dpcTriCaps.dwTextureCaps & 8) != 0;
		device->caps.renderBitDepthMask = std3D_PackRenderBitDepths(device->d3dDesc.dwDeviceRenderBitDepth);
		device->caps.zCmpCapsMask = std3D_PackZCmpCaps(device->d3dDesc.dpcTriCaps.dwZCmpCaps);
		device->caps.minTextureWidth = 1;
		device->caps.minTextureHeight = 1;
		device->caps.maxTextureWidth = 256;
		device->caps.maxTextureHeight = 256;
		device->caps.maxBufferSize = device->d3dDesc.dwMaxBufferSize;
		device->caps.maxVertexCount = device->d3dDesc.dwMaxVertexCount;

		DebugPrintf("Found |%s|%s|%s|%s| D3D Device\n", device->caps.bHardware != 0 ? "HW" : "SW",
					device->caps.colorModelFlags & 1 ? "MONO" : "",
					device->caps.colorModelFlags & 2 ? "RGB" : "",
					device->caps.bHasZBuffer != 0 ? "Z" : "Non-Z");
		DebugPrintf("      |%s|%s|%s| %dbpp\n", device->caps.bAlphaTexture != 0 ? "Alpha" : "No Alpha",
					device->caps.bStippledShade != 0 ? "Stippled" : "Blend",
					device->caps.bColorKeyTexture != 0 ? "Colorkey" : "No Colorkey",
					device->caps.renderBitDepthMask);
		DebugPrintf("Description: %s [%s]\n", device->deviceName, device->deviceDescription, 0, 0);
		++g_std3DNumDevices;
		return 1;
	}
	return 0;
}

// FUNCTION: XVT 0x4B4490
int AERON_DXAPI std3D_EnumTextureFormats(DDSURFACEDESC* surfaceDesc, void* context) {
	Std3DTexFmt* format;
	int redShift;
	int greenShift;
	int blueShift;
	int alphaShift;
	unsigned int mask;

	(void)context;
	if ((unsigned int)g_std3DNumTextureFormats < 8) {
		format = &g_std3DTextureFormats[g_std3DNumTextureFormats];
		memcpy(&format->ddsd, surfaceDesc, sizeof(format->ddsd));
		if ((surfaceDesc->ddpfPixelFormat.dwFlags & DDPF_PALETTEINDEXED8) != 0) {
			format->colorInfo.colorMode = STDCOLOR_PAL;
			format->colorInfo.bpp = 8;
			format->colorInfo.redPosShift = 0;
			format->colorInfo.redPosShiftRight = 0;
			format->colorInfo.redBPP = 0;
			format->colorInfo.greenPosShift = 0;
			format->colorInfo.greenPosShiftRight = 0;
			format->colorInfo.greenBPP = 0;
			format->colorInfo.bluePosShift = 0;
			format->colorInfo.bluePosShiftRight = 0;
			format->colorInfo.blueBPP = 0;
			format->colorInfo.alphaPosShift = 0;
			format->colorInfo.alphaPosShiftRight = 0;
			format->colorInfo.alphaBPP = 0;
			DebugPrintf("Found %dbpp palettized tex format.\n", format->colorInfo.bpp, 0, 0, 0);
		} else if ((surfaceDesc->ddpfPixelFormat.dwFlags & 8) != 0) {
			return 1;
		} else if ((surfaceDesc->ddpfPixelFormat.dwFlags & DDPF_ALPHAPIXELS) != 0) {
			format->colorInfo.colorMode = STDCOLOR_RGBA;
			format->colorInfo.bpp = surfaceDesc->ddpfPixelFormat.dwRGBBitCount;

			redShift = 0;
			mask = surfaceDesc->ddpfPixelFormat.dwRBitMask;
			while ((mask & 1) == 0) {
				++redShift;
				mask >>= 1;
			}
			format->colorInfo.redPosShift = redShift;
			format->colorInfo.redPosShiftRight =
				std3D_Log2Floor(0xFFu / (surfaceDesc->ddpfPixelFormat.dwRBitMask >> redShift));
			redShift = 0;
			while ((mask & 1) != 0) {
				++redShift;
				mask >>= 1;
			}
			format->colorInfo.redBPP = redShift;

			greenShift = 0;
			mask = surfaceDesc->ddpfPixelFormat.dwGBitMask;
			while ((mask & 1) == 0) {
				++greenShift;
				mask >>= 1;
			}
			format->colorInfo.greenPosShift = greenShift;
			format->colorInfo.greenPosShiftRight =
				std3D_Log2Floor(0xFFu / (surfaceDesc->ddpfPixelFormat.dwGBitMask >> greenShift));
			greenShift = 0;
			while ((mask & 1) != 0) {
				++greenShift;
				mask >>= 1;
			}
			format->colorInfo.greenBPP = greenShift;

			blueShift = 0;
			mask = surfaceDesc->ddpfPixelFormat.dwBBitMask;
			while ((mask & 1) == 0) {
				++blueShift;
				mask >>= 1;
			}
			format->colorInfo.bluePosShift = blueShift;
			format->colorInfo.bluePosShiftRight =
				std3D_Log2Floor(0xFFu / (surfaceDesc->ddpfPixelFormat.dwBBitMask >> blueShift));
			blueShift = 0;
			while ((mask & 1) != 0) {
				++blueShift;
				mask >>= 1;
			}
			format->colorInfo.blueBPP = blueShift;

			alphaShift = 0;
			mask = surfaceDesc->ddpfPixelFormat.dwRGBAlphaBitMask;
			while ((mask & 1) == 0) {
				++alphaShift;
				mask >>= 1;
			}
			format->colorInfo.alphaPosShift = alphaShift;
			format->colorInfo.alphaPosShiftRight =
				std3D_Log2Floor(0xFFu / (surfaceDesc->ddpfPixelFormat.dwRGBAlphaBitMask >> alphaShift));
			alphaShift = 0;
			while ((mask & 1) != 0) {
				++alphaShift;
				mask >>= 1;
			}
			format->colorInfo.alphaBPP = alphaShift;
			DebugPrintf("Found RGBA tex format (%d:%d:%d:%d).\n", format->colorInfo.redBPP,
						format->colorInfo.greenBPP, format->colorInfo.blueBPP, format->colorInfo.alphaBPP);
			++g_std3DNumTextureFormats;
			return 1;
		} else {
			format->colorInfo.colorMode = STDCOLOR_RGB;
			format->colorInfo.bpp = surfaceDesc->ddpfPixelFormat.dwRGBBitCount;

			redShift = 0;
			mask = surfaceDesc->ddpfPixelFormat.dwRBitMask;
			while ((mask & 1) == 0) {
				++redShift;
				mask >>= 1;
			}
			format->colorInfo.redPosShift = redShift;
			format->colorInfo.redPosShiftRight =
				std3D_Log2Floor(0xFFu / (surfaceDesc->ddpfPixelFormat.dwRBitMask >> redShift));
			redShift = 0;
			while ((mask & 1) != 0) {
				++redShift;
				mask >>= 1;
			}
			format->colorInfo.redBPP = redShift;

			greenShift = 0;
			mask = surfaceDesc->ddpfPixelFormat.dwGBitMask;
			while ((mask & 1) == 0) {
				++greenShift;
				mask >>= 1;
			}
			format->colorInfo.greenPosShift = greenShift;
			format->colorInfo.greenPosShiftRight =
				std3D_Log2Floor(0xFFu / (surfaceDesc->ddpfPixelFormat.dwGBitMask >> greenShift));
			greenShift = 0;
			while ((mask & 1) != 0) {
				++greenShift;
				mask >>= 1;
			}
			format->colorInfo.greenBPP = greenShift;

			blueShift = 0;
			mask = surfaceDesc->ddpfPixelFormat.dwBBitMask;
			while ((mask & 1) == 0) {
				++blueShift;
				mask >>= 1;
			}
			format->colorInfo.bluePosShift = blueShift;
			format->colorInfo.bluePosShiftRight =
				std3D_Log2Floor(0xFFu / (surfaceDesc->ddpfPixelFormat.dwBBitMask >> blueShift));
			blueShift = 0;
			while ((mask & 1) != 0) {
				++blueShift;
				mask >>= 1;
			}
			format->colorInfo.blueBPP = blueShift;
			format->colorInfo.alphaPosShift = 0;
			format->colorInfo.alphaPosShiftRight = 0;
			format->colorInfo.alphaBPP = 0;
			DebugPrintf("Found RGB tex format (%d:%d:%d).\n", format->colorInfo.redBPP,
						format->colorInfo.greenBPP, format->colorInfo.blueBPP, 0);
		}

		++g_std3DNumTextureFormats;
		return 1;
	}
	return 0;
}

// FUNCTION: XVT 0x4B47E0
int std3D_PackRenderBitDepths(int ddbdFlags) {
	int result;

	result = 0;
	if ((ddbdFlags & 0x4000) != 0)
		result |= 0x01;
	if ((ddbdFlags & 0x2000) != 0)
		result |= 0x02;
	if ((ddbdFlags & 0x1000) != 0)
		result |= 0x04;
	if ((ddbdFlags & 0x0800) != 0)
		result |= 0x08;
	if ((ddbdFlags & 0x0400) != 0)
		result |= 0x10;
	if ((ddbdFlags & 0x0200) != 0)
		result |= 0x20;
	if ((ddbdFlags & 0x0100) != 0)
		result |= 0x40;
	return result;
}

// FUNCTION: XVT 0x4B4880
int std3D_PackZCmpCaps(unsigned int d3dpcmpcaps) {
	int result = 0;

	if (d3dpcmpcaps & 1)
		result = 1;
	if (d3dpcmpcaps & 4)
		result |= 4;
	if (d3dpcmpcaps & 2)
		result |= 2;
	if (d3dpcmpcaps & 8)
		result |= 8;
	if (d3dpcmpcaps & 0x10)
		result |= 0x10;
	if (d3dpcmpcaps & 0x20)
		result |= 0x20;
	if (d3dpcmpcaps & 0x40)
		result |= 0x40;
	if (d3dpcmpcaps & 0x80)
		result |= 0x80;
	return result;
}

// FUNCTION: XVT 0x4B48D0
unsigned int std3D_MapZCmpFunc(unsigned int capsMask) {
	unsigned int result = 0;

	if (capsMask & 1)
		result = 1;
	if (capsMask & 4)
		result |= 3;
	if (capsMask & 2)
		result |= 2;
	if (capsMask & 8)
		result |= 4;
	if (capsMask & 0x10)
		result |= 5;
	if (capsMask & 0x20)
		result |= 6;
	if (capsMask & 0x40)
		result |= 7;
	if (capsMask & 0x80)
		result |= 8;
	return result;
}
