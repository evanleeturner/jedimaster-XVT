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

/* One triangle of a hardware batch, as std3D_AddTriangles writes it. */
struct Std3DRenderTri {
	int v0; /* First corner: index into the batch's vertices. */
	int v1; /* Second corner. */
	int v2; /* Third corner. */
	/* Render-state bits set before it; neighbors with the same bits and
	 * texture share one instruction. */
	Std3DRenderStateFlags flags;
	Std3DTexCacheNode *texture; /* Texture to draw with; NULL for none. */
};

/* One texture in video memory and its place on the cache list. */
struct Std3DTexCacheNode {
	/* The Direct3D texture while cached; NULL otherwise. */
	IDirect3DTexture *pCachedTexture;
	IDirectDrawSurface *pCachedSurface; /* Its DirectDraw surface. */
	/* Description of the system-memory surface it was loaded from. */
	DDSURFACEDESC ddsd;
	/* Direct3D handle std3D_AddTriangles sets as the texture render state;
	 * 0 when getting it failed. */
	unsigned int texHandle;
	/* 1 while the texture is in video memory and on the cache list. */
	int bCached;
	/* 1 when made in a format with alpha; nothing reads it. */
	int usesAlphaFormat;
	unsigned int width;  /* Width as made; nothing reads it. */
	unsigned int height; /* Height as made; nothing reads it. */
	/* width times height; taken from the device's availableMemory while
	 * cached. */
	unsigned int texelCount;
	/* g_std3DTextureBatchTag when last used; a purge spares the textures
	 * of the current batch. */
	unsigned int cacheBatchTag;
	/* Neighbor toward g_pTexCacheHead, the least recently used end. */
	struct Std3DTexCacheNode *pPrev;
	/* Neighbor toward g_pTexCacheTail, the most recently used end. */
	struct Std3DTexCacheNode *pNext;
};

/* What a Direct3D device can do, as std3D_EnumDevicesCallback reads it from
 * the device's description. */
struct Std3DDeviceCaps {
	/* 1 for a hardware device: its hardware description has a color
	 * model. */
	int bHardware;
	/* Perspective-correct texturing: the 0x1 bit of the triangle texture
	 * caps. */
	int bTexturePerspective;
	int bHasZBuffer; /* 1 when any z-buffer bit depth is reported. */
	/* Color-keyed textures: the 0x8 bit of the triangle texture caps. */
	int bColorKeyTexture;
	/* Alpha in textures: the 0x4 bit of the triangle texture caps. */
	int bAlphaTexture;
	/* 1 when the triangle shade caps have the 0x2000 bit and not the
	 * 0x1000 bit: stippled alpha in place of blended. */
	int bStippledShade;
	/* 1 when the texture blend caps have the 0x8 bit and the shade caps
	 * the 0x4000 bit, or bStippledShade is set. */
	int bAlphaBlend;
	/* Square textures only: the 0x20 bit of the triangle texture caps. */
	int bSquareOnlyTexture;
	int bClampSupported; /* Never read or written. */
	/* Color models: 0x1 for mono, 0x2 for RGB. */
	unsigned int colorModelFlags;
	/* Render bit depths from std3D_PackRenderBitDepths; only a debug print
	 * reads it. */
	unsigned int renderBitDepthMask;
	/* Z compare functions, as the D3D compare caps bits; with the 0x10
	 * bit (greater) the z-buffer uses the greater test. */
	unsigned int zCmpCapsMask;
	unsigned int minTextureWidth;  /* 1; nothing reads it. */
	unsigned int minTextureHeight; /* 1; nothing reads it. */
	unsigned int maxTextureWidth;  /* 256; nothing reads it. */
	unsigned int maxTextureHeight; /* 256; nothing reads it. */
	/* Largest execute buffer in bytes; 0 when the device states none. */
	unsigned int maxBufferSize;
	/* Most vertices in an execute buffer; 0 when the device states none. */
	unsigned int maxVertexCount;
};

/* A Direct3D device description, copied whole by std3D_EnumDevicesCallback:
 * the hardware one when it has a color model, else the software one. The
 * fields marked unread are only copied. */
struct Std3DDeviceDesc {
	unsigned int dwSize;	     /* Unread. */
	unsigned int dwFlags;	     /* Unread. */
	unsigned int dcmColorModel;  /* Color models: 0x1 mono, 0x2 RGB. */
	unsigned int dwDevCaps;	     /* Unread. */
	uint8_t dtcTransformCaps[8]; /* Unread. */
	int bClipping;		     /* Unread. */
	uint8_t dlcLightingCaps[16]; /* Unread. */
	D3DPRIMCAPS dpcLineCaps;     /* Unread. */
	/* Triangle caps: texture, shade, texture blend and z compare bits. */
	D3DPRIMCAPS dpcTriCaps;
	unsigned int dwDeviceRenderBitDepth; /* Render bit depth flags. */
	/* Z-buffer bit depth flags; std3D_CreateZBuffer takes 32, 16 or 8 bits
	 * from the 0x100, 0x400 and 0x800 bits, in that order. */
	unsigned int dwDeviceZBufferBitDepth;
	unsigned int dwMaxBufferSize;	 /* Largest execute buffer in bytes. */
	unsigned int dwMaxVertexCount;	 /* Most vertices per execute buffer. */
	unsigned int dwMinTextureWidth;	 /* Unread. */
	unsigned int dwMinTextureHeight; /* Unread. */
	unsigned int dwMaxTextureWidth;	 /* Unread. */
	unsigned int dwMaxTextureHeight; /* Unread. */
	unsigned int dwMinStippleWidth;	 /* Unread. */
	unsigned int dwMaxStippleWidth;	 /* Unread. */
	unsigned int dwMinStippleHeight; /* Unread. */
	unsigned int dwMaxStippleHeight; /* Unread. */
};

/* One Direct3D device found by std3D_Startup, in g_std3DDevices. */
struct Std3DDevice {
	Std3DDeviceCaps caps; /* What it can do, from d3dDesc. */
	/* Name from the enumeration, cut at 128 bytes without a terminator
	 * when it fills them. */
	char deviceName[128];
	/* Description from the enumeration, cut the same way. */
	char deviceDescription[128];
	/* Texture video memory in bytes, set by std3D_CreateDevice. */
	unsigned int totalMemory;
	/* Free texture memory: bytes when std3D_CreateDevice sets it, then
	 * lowered by each cached texture's texelCount and raised when one
	 * leaves; std3D_FlushTextureCache sets it back to totalMemory. Only a
	 * debug print reads it. */
	unsigned int availableMemory;
	Std3DDeviceDesc d3dDesc; /* The description it was found with. */
	/* Device GUID; std3D_CreateDevice asks the render surface for this
	 * interface to make the device. */
	DxGuid guid;
};

/* Size and pixel format of a Std3DVBuffer. From colorMode on it has
 * ColorInfo's layout and is filled by copying one in; the code reads only
 * colorMode and bpp of that part. */
struct Std3DRasterInfo {
	unsigned int width;  /* Pixels per row. */
	unsigned int height; /* Rows. */
	/* Nothing reads it; std3D_CreateZBuffer's copy of the render target
	 * puts its sizeBytes here. */
	unsigned int tileFactor;
	unsigned int rowPitch; /* Bytes from one row to the next. */
	/* Nothing reads it; that copy puts pitchPixels here. */
	unsigned int unk10;
	/* Palette, RGB or RGBA. */
	StdColorMode
		colorMode; ///< Start of the flattened ColorInfo copied verbatim from Std3DTexFmt.
	unsigned int bpp;  /* Bits per pixel. */
	int redBPP;	   /* Red bits; unread here. */
	int greenBPP;	   /* Green bits; unread here. */
	int blueBPP;	   /* Blue bits; unread here. */
	int redPosShift;   /* Red bit position; unread here. */
	int greenPosShift; /* Green bit position; unread here. */
	int bluePosShift;  /* Blue bit position; unread here. */
	int redPosShiftRight;	/* Shift from 8 bits to red's; unread here. */
	int greenPosShiftRight; /* Shift from 8 bits to green's; unread here. */
	int bluePosShiftRight;	/* Shift from 8 bits to blue's; unread here. */
	int alphaBPP;		/* Alpha bits; unread here. */
	int alphaPosShift;	/* Alpha bit position; unread here. */
	int alphaPosShiftRight; /* Shift from 8 bits to alpha's; unread here. */
};

/* A block of pixels the texture code copies from: memory of its own or a
 * DirectDraw surface. */
struct Std3DVBuffer {
	/* 1 for a DirectDraw surface in ddSurface; 0 for plain memory. */
	int storageType;
	/* std3D_LockVBuffer calls not yet undone; a surface is locked on the
	 * first and unlocked when the last is undone. */
	int lockCount;
	/* Never read or written by name. */
	int inVideoMemory; ///< Nonzero when the DirectDraw-backed record resides in video memory; zero for
	///< malloc-backed buffers.
	Std3DRasterInfo raster; /* Size, row pitch and pixel format. */
	int unk58;		/* Never read or written. */
	/* The pixels: memory from std3D_AllocVBuffer or the caller, or a
	 * surface's memory while it is locked. */
	void *pixels;
	/* Color key of an RGB buffer, used when the device has no alpha
	 * textures; nothing sets it but the callers' memset to 0. */
	unsigned int transparentColor;
	/* The surface when storageType is 1; std3D_FreeVBuffer releases it. */
	IDirectDrawSurface *ddSurface;
	/* Never read or written by name. */
	uint8_t reserved68
		[112]; ///< Reserved in ordinary software buffers. In the static z-buffer overlay, this
	///< tail is unused04 at +0x68 followed by DDSURFACEDESC at +0x6C.
};

/* One texture format the device offers, from std3D_EnumTextureFormats. */
struct Std3DTexFmt {
	/* Palette, RGB or RGBA, with each channel's bits and position read from
	 * the masks. */
	ColorInfo colorInfo;
	/* The surface description as enumerated; textures in this format are
	 * created from a copy. */
	DDSURFACEDESC ddsd;
};

/* The surface Direct3D draws into, as std3D_InitRenderTargetDesc fills
 * g_std3DRenderTargetDesc. */
struct Std3DRenderTargetDesc {
	unsigned int width;	  /* Pixels per row. */
	unsigned int height;	  /* Rows. */
	unsigned int sizeBytes;	  /* pitch times height; nothing reads it. */
	int pitch;		  /* Bytes from one row to the next. */
	unsigned int pitchPixels; /* pitch / 2; nothing reads it. */
	ColorInfo colorInfo;	  /* Always 16-bit RGB, 5, 6 and 5 bits. */
};

/* Nothing uses this structure. */
struct Std3DSurfaceState {
	IDirectDrawSurface *surface; /* Never read or written. */
	/* Never read or written. */
	int unused04; ///< Unused slot between the DirectDraw surface pointer and DDSURFACEDESC.
	DDSURFACEDESC ddsd; /* Never read or written. */
};

/* A rectangle on the render target, in pixels. */
struct Std3DViewportRect {
	int x;	    /* Left edge. */
	int y;	    /* Top edge. */
	int width;  /* Width. */
	int height; /* Height. */
};

/* One row of g_std3DErrorStringTable. */
struct Std3DErrorStringEntry {
	int code;	     /* The result code. */
	const char *message; /* Its name as text. */
};

/* A record of the z-buffer that std3D_CreateZBuffer fills and nothing else
 * reads. */
struct Std3DZBufferTarget {
	int storageType;	/* Set to 1. */
	int lockCount;		/* Never read or written. */
	int bVideoMemory;	/* 1 when the surface is in video memory. */
	Std3DRasterInfo raster; /* A copy of the render target's record. */
	int unk58;		/* Set to 0. */
	void *pixels;		/* Set to NULL. */
};

extern unsigned int g_std3DExecBufMaxVerts;
extern unsigned int g_std3DNumDevices;
extern unsigned int g_std3DCurDeviceIdx;
extern Std3DDevice g_std3DDevices[4];
extern Std3DDevice *g_pStd3DCurDevice;
extern unsigned int g_std3DTextureBatchTag;
extern int g_texCacheCount;
extern Std3DTexCacheNode *g_pTexCacheHead;
extern Std3DTexCacheNode *g_pTexCacheTail;
extern int g_d3dBufVertCount;
extern uint8_t *g_d3dWritePtr;

/* The z-buffer surface std3D_CreateZBuffer makes. */
struct Std3DZBufferSurfaceBlock {
	/* The surface, attached to g_std3DRenderSurface; std3D_ClearZBuffer
	 * fills it and std3D_Close releases it. */
	IDirectDrawSurface *surface;
	int unused04; /* Never read or written. */
	/* What std3D_CreateZBuffer asked for, then what the surface reports. */
	DDSURFACEDESC desc;
};

extern unsigned int g_std3DRenderOptionFlags;
extern Std3DTexFmt *g_pFmtOpaqueTexture;
extern float g_std3DColorOverlayRed;
extern float g_std3DColorOverlayGreen;
extern float g_std3DColorOverlayBlue;
extern int g_std3DColorOverlayEnabled;
extern int g_std3DMinTextureWidth;
extern int g_std3DZCompareCap;
extern int g_std3DMinTextureHeight;
extern int g_std3DMaxTextureWidth;
extern int g_std3DMaxTextureHeight;

void std3D_CopyPaletteToScratch16(const uint16_t *palette, int colorCount);
void std3D_ConvertPaletteTo1555(const uint16_t *palette, int colorCount);
int std3D_Startup(void);
Std3DRenderTargetDesc *std3D_InitRenderTargetDesc(unsigned int width,
						  unsigned int height,
						  int pitchBytes);
void std3D_Shutdown(void);
int std3D_CreateDevice(unsigned int deviceIdx, int bUseZBuffer);
struct IDirectDrawSurface *
std3D_SetRenderSurface(struct IDirectDrawSurface *surface);
int std3D_SetColorOverlayParams(float red, float green, float blue,
				int enabled);
int std3D_Log2Floor(int n);
const char *std3D_LookupErrorString(int errorCode,
				    const Std3DErrorStringEntry *entries,
				    int entryCount);
void std3D_BuildColormap16(uint8_t *pRGB888, uint16_t *pOut, ColorInfo *pFmt,
			   uint8_t defaultAlpha, int colorKey);
void std3D_BuildColormapOpaque(uint8_t *pRGB888, uint16_t *pOut,
			       ColorInfo *pFmt);
void std3D_BuildColormapColorKey(uint8_t *pRGB888, uint16_t *pOut,
				 ColorInfo *pFmt);
void std3D_BuildColormapAlpha(uint8_t *pRGB888, uint16_t *pOut, ColorInfo *pFmt,
			      uint8_t alpha);
Std3DVBuffer *std3D_AllocVBuffer(const Std3DRasterInfo *raster, ...);
void std3D_FreeVBuffer(Std3DVBuffer *vbuffer);
void std3D_LockVBuffer(Std3DVBuffer *vbuffer);
void std3D_UnlockVBuffer(Std3DVBuffer *vbuffer);
void std3D_Close(void);
void std3D_BlitVBuffer(Std3DVBuffer *destination, Std3DVBuffer *source,
		       int destinationX, int destinationY, int sourceX,
		       int sourceY);
unsigned int std3D_GetCapFlags(void);
void std3D_FillVBuffer(Std3DVBuffer *vbuffer, unsigned int packedColor,
		       int fillMode);
void std3D_SetCapFlags(unsigned int capFlags);
int std3D_SetFogColor8(unsigned int red8, unsigned int green8,
		       unsigned int blue8);
int std3D_SetFogTableRangeBits(unsigned int startBits, unsigned int endBits);
int std3D_SetTextureSizeCaps(int minWidth, int minHeight, int maxWidth,
			     int maxHeight);
void std3D_StartScene(void);
void std3D_EndScene(void);
int std3D_LockExecuteBuffer(void);
int std3D_AddVertices(const D3DTLVERTEX *vertices, int count);
int std3D_BeginInstructions(void);
int std3D_AddTriangles(const Std3DRenderTri *triangles, unsigned int count);
int std3D_ExecuteBuffer(void);
void std3D_SetRenderState(Std3DRenderStateFlags flags);
int std3D_SetPaletteConversionSource(const void *paletteRgb888, uint8_t alpha);
void std3D_ClampTextureDimensions(int srcWidth, int srcHeight, int *outWidth,
				  int *outHeight);
int std3D_AddToTextureCache(Std3DVBuffer *source, Std3DTexCacheNode *node,
			    int colorKeyed, int translucent);
void std3D_FlushTextureCache(void);
void std3D_CacheListAppend(Std3DTexCacheNode *node);
void std3D_CacheListRemove(Std3DTexCacheNode *node);
int std3D_QueryTextureVidMem(unsigned int *totalBytes, unsigned int *freeBytes);
void std3D_CacheTextureSurface(Std3DTexCacheNode *node);
int std3D_ClearZBuffer(void);
int std3D_SelectBestDevice(Std3DDeviceCaps *requiredCaps);
int std3D_FindClosestFormat(const ColorInfo *match, Std3DTexFmt *formats,
			    unsigned int count);
void std3D_DrawColorOverlay(void);
int std3D_BuildViewportQuad(const Std3DViewportRect *rect);
int std3D_SetInitialRenderState(void);
int std3D_CreateViewport(int width, int height);
int std3D_CreateZBuffer(int width, int height);
HRESULT AERON_DXAPI std3D_EnumDevicesCallback(DxGuid *guid,
					      char *deviceDescription,
					      char *deviceName,
					      D3DDEVICEDESC *hardwareDesc,
					      D3DDEVICEDESC *softwareDesc,
					      void *context);
int AERON_DXAPI std3D_EnumTextureFormats(DDSURFACEDESC *surfaceDesc,
					 void *context);
int std3D_PackRenderBitDepths(int ddbdFlags);
int std3D_PackZCmpCaps(unsigned int d3dpcmpcaps);
unsigned int std3D_MapZCmpFunc(unsigned int capsMask);

#ifdef __cplusplus
}
#endif

#endif
