#ifndef XVT_RENDER_STD3D_H
#define XVT_RENDER_STD3D_H

#include "aeron/compat/d3d.h"
#include "aeron/compat/ddraw.h"
#include "xvt/render/color.h"
#include "xvt/util/win32.h"
#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum Std3DRenderStateFlags {
	STD3D_RS_FOG_ENABLE = 0x40,
	STD3D_RS_TEXTURE_MAG_LINEAR = 0x80,
	STD3D_RS_TEXTURE_MIN_LINEAR = 0x100,
	STD3D_RS_ALPHA_BLEND = 0x200,
	STD3D_RS_TEXTURE_MODULATE_ALPHA = 0x400,
	STD3D_RS_Z_COMPARE_ENABLE = 0x800,
	STD3D_RS_Z_WRITE_ENABLE = 0x1000,
	STD3D_RS_TEXTURE_ADDRESS_CLAMP = 0x2000,
	STD3D_RS_MONO_DISABLE = 0x8000,
} Std3DRenderStateFlags;

struct Std3DRenderTri {
	int v0;
	int v1;
	int v2;
	Std3DRenderStateFlags flags;
	Std3DTexCacheNode* texture;
};

struct Std3DTexCacheNode {
	IDirect3DTexture* pCachedTexture;
	IDirectDrawSurface* pCachedSurface;
	DDSURFACEDESC ddsd;
	unsigned int texHandle;
	int bCached;
	int usesAlphaFormat;
	unsigned int width;
	unsigned int height;
	unsigned int byteSize;
	unsigned int cacheFrameTag;
	struct Std3DTexCacheNode* pPrev;
	struct Std3DTexCacheNode* pNext;
};

struct Std3DDeviceCaps {
	int bHardware;
	int bTexturePerspective;
	int bHasZBuffer;
	int bColorKeyTexture;
	int bAlphaTexture;
	int bStippledShade;
	int bAlphaBlend;
	int bSquareOnlyTexture;
	int bClampSupported;
	unsigned int colorModelFlags;
	unsigned int renderBitDepthMask;
	unsigned int zCmpCapsMask;
	unsigned int minTextureWidth;
	unsigned int minTextureHeight;
	unsigned int maxTextureWidth;
	unsigned int maxTextureHeight;
	unsigned int maxBufferSize;
	unsigned int maxVertexCount;
};

struct Std3DDeviceDesc {
	unsigned int dwSize;
	unsigned int dwFlags;
	unsigned int dcmColorModel;
	unsigned int dwDevCaps;
	uint8_t dtcTransformCaps[8];
	int bClipping;
	uint8_t dlcLightingCaps[16];
	D3DPRIMCAPS dpcLineCaps;
	D3DPRIMCAPS dpcTriCaps;
	unsigned int dwDeviceRenderBitDepth;
	unsigned int dwDeviceZBufferBitDepth;
	unsigned int dwMaxBufferSize;
	unsigned int dwMaxVertexCount;
	unsigned int dwMinTextureWidth;
	unsigned int dwMinTextureHeight;
	unsigned int dwMaxTextureWidth;
	unsigned int dwMaxTextureHeight;
	unsigned int dwMinStippleWidth;
	unsigned int dwMaxStippleWidth;
	unsigned int dwMinStippleHeight;
	unsigned int dwMaxStippleHeight;
};

struct Std3DDevice {
	Std3DDeviceCaps caps;
	char deviceName[128];
	char deviceDescription[128];
	unsigned int totalMemory;
	unsigned int availableMemory;
	Std3DDeviceDesc d3dDesc;
	DxGuid guid;
};

struct Std3DRasterInfo {
	unsigned int width;
	unsigned int height;
	unsigned int tileFactor;
	unsigned int rowPitch;
	unsigned int unk10;
	StdColorMode colorMode; ///< Start of the flattened ColorInfo copied verbatim from Std3DTexFmt.
	unsigned int bpp;
	int redBPP;
	int greenBPP;
	int blueBPP;
	int redPosShift;
	int greenPosShift;
	int bluePosShift;
	int redPosShiftRight;
	int greenPosShiftRight;
	int bluePosShiftRight;
	int alphaBPP;
	int alphaPosShift;
	int alphaPosShiftRight;
};

struct Std3DVBuffer {
	int storageType;
	int lockCount;
	int inVideoMemory; ///< Nonzero when the DirectDraw-backed record resides in video memory; zero for
					   ///< malloc-backed buffers.
	Std3DRasterInfo raster;
	int unk58;
	void* pixels;
	unsigned int transparentColor;
	IDirectDrawSurface* ddSurface;
	uint8_t reserved68[112]; ///< Reserved in ordinary software buffers. In the static z-buffer overlay, this
							 ///< tail is unused04 at +0x68 followed by DDSURFACEDESC at +0x6C.
};

struct Std3DTexFmt {
	ColorInfo colorInfo;
	DDSURFACEDESC ddsd;
};

struct Std3DRenderTargetDesc {
	unsigned int width;
	unsigned int height;
	unsigned int sizeBytes;
	int pitch;
	unsigned int widthPixels;
	ColorInfo colorInfo;
};

struct Std3DSurfaceState {
	IDirectDrawSurface* surface;
	int unused04; ///< Unused slot between the DirectDraw surface pointer and DDSURFACEDESC.
	DDSURFACEDESC ddsd;
};

struct Std3DViewportRect {
	int x;
	int y;
	int width;
	int height;
};

struct Std3DErrorStringEntry {
	int code;
	const char* message;
};

struct Std3DZBufferTargetScratch {
	int storageType;
	int lockCount;
	int bVideoMemory;
	Std3DRasterInfo raster;
	int unk58;
	void* pixels;
};

extern unsigned int g_std3DExecBufMaxVerts;
extern unsigned int g_std3DNumDevices;
extern unsigned int g_std3DCurDeviceIdx;
extern Std3DDevice g_std3DDevices[4];
extern Std3DDevice* g_pStd3DCurDevice;
extern unsigned int g_std3DTextureFrameTag;
extern int g_texCacheCount;
extern Std3DTexCacheNode* g_pTexCacheHead;
extern Std3DTexCacheNode* g_pTexCacheTail;
extern int g_d3dBufVertCount;
extern uint8_t* g_d3dWritePtr;

struct Std3DZBufferSurfaceBlock {
	IDirectDrawSurface* surface;
	int unused04;
	DDSURFACEDESC desc;
};

extern unsigned int g_std3DCapFlags;
extern Std3DTexFmt* g_pFmtRGB565;
extern float g_std3DColorOverlayRed;
extern float g_std3DColorOverlayGreen;
extern float g_std3DColorOverlayBlue;
extern int g_std3DColorOverlayEnabled;
extern int g_std3DMinTextureWidth;
extern int g_std3DZBufferBitDepth;
extern int g_std3DMinTextureHeight;
extern int g_std3DMaxTextureWidth;
extern int g_std3DMaxTextureHeight;

void std3D_CopyPaletteToScratch16(const uint16_t* palette, int colorCount);
void std3D_ConvertTexTo1555(const uint16_t* srcPixels, int pixelCount);
int std3D_Startup(void);
Std3DRenderTargetDesc* std3D_InitRenderTargetDesc(unsigned int arg1, unsigned int arg2, int arg3);
void std3D_Shutdown(void);
int std3D_CreateDevice(unsigned int deviceIdx, int bUseZBuffer);
struct IDirectDrawSurface* std3D_SetRenderSurface(struct IDirectDrawSurface* arg1);
int std3D_SetColorOverlayParams(float red, float green, float blue, int enabled);
int std3D_Log2Floor(int n);
const char* std3D_LookupErrorString(int errorCode, const Std3DErrorStringEntry* entries, int entryCount);
void std3D_BuildColormap16(uint8_t* pRGB888, uint16_t* pOut, ColorInfo* pFmt, uint8_t defaultAlpha,
						   int colorKey);
void std3D_BuildColormapOpaque(uint8_t* pRGB888, uint16_t* pOut, ColorInfo* pFmt);
void std3D_BuildColormapColorKey(uint8_t* pRGB888, uint16_t* pOut, ColorInfo* pFmt);
void std3D_BuildColormapAlpha(uint8_t* pRGB888, uint16_t* pOut, ColorInfo* pFmt, uint8_t alpha);
Std3DVBuffer* std3D_AllocVBuffer(const Std3DRasterInfo* raster, ...);
void std3D_FreeVBuffer(Std3DVBuffer* vbuffer);
void std3D_LockVBuffer(Std3DVBuffer* vbuffer);
void std3D_UnlockVBuffer(Std3DVBuffer* vbuffer);
void std3D_Close(void);
void std3D_BlitVBuffer(Std3DVBuffer* destination, Std3DVBuffer* source, int destinationX, int destinationY,
					   int sourceX, int sourceY);
unsigned int std3D_GetCapFlags(void);
void std3D_FillVBuffer(Std3DVBuffer* vbuffer, unsigned int packedColor, int fillMode);
void std3D_SetCapFlags(unsigned int capFlags);
int std3D_SetFogColor8(unsigned int red8, unsigned int green8, unsigned int blue8);
int std3D_SetFogTableRangeBits(unsigned int startBits, unsigned int endBits);
int std3D_SetTextureSizeCaps(int minWidth, int minHeight, int maxWidth, int maxHeight);
void std3D_StartScene(void);
void std3D_EndScene(void);
int std3D_LockExecuteBuffer(void);
int std3D_AddVertices(const D3DTLVERTEX* vertices, int count);
int std3D_BeginInstructions(void);
int std3D_AddTriangles(const Std3DRenderTri* triangles, unsigned int count);
int std3D_ExecuteBuffer(void);
void std3D_SetRenderState(Std3DRenderStateFlags flags);
int std3D_SetPaletteConversionSource(const void* paletteRgb888, uint8_t alpha);
void std3D_ClampTextureDimensions(int srcWidth, int srcHeight, int* outWidth, int* outHeight);
int std3D_CreateMipSurface(Std3DVBuffer* source, Std3DTexCacheNode* node, int textureFormatMode,
						   int alphaMask);
void std3D_FlushTextureCache(void);
void std3D_CacheListAppend(Std3DTexCacheNode* node);
void std3D_CacheListRemove(Std3DTexCacheNode* node);
int std3D_QueryTextureVidMem(unsigned int* totalBytes, unsigned int* freeBytes);
void std3D_CacheTextureSurface(Std3DTexCacheNode* node);
int std3D_ClearZBuffer(void);
int std3D_SelectBestDevice(Std3DDeviceCaps* arg1);
int std3D_FindClosestFormat(const ColorInfo* match, Std3DTexFmt* formats, unsigned int count);
void std3D_DrawColorOverlay(void);
int std3D_BuildViewportQuad(const Std3DViewportRect* rect);
int std3D_SetInitialRenderState(void);
int std3D_CreateViewport(int arg1, int arg2);
int std3D_CreateZBuffer(int width, int height);
HRESULT AERON_DXAPI std3D_EnumDevicesCallback(DxGuid* arg1, char* Source, char* arg3, D3DDEVICEDESC* arg4,
											  D3DDEVICEDESC* arg5, void* arg6);
int AERON_DXAPI std3D_EnumTextureFormats(DDSURFACEDESC* surfaceDesc, void* context);
int std3D_PackRenderBitDepths(int ddbdFlags);
int std3D_PackZCmpCaps(unsigned int d3dpcmpcaps);
unsigned int std3D_MapZCmpFunc(unsigned int capsMask);

#ifdef __cplusplus
}
#endif

#endif
