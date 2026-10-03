#include "xvt/render/std3d.h"
#include "xvt/render/renderer.h"
#include "xvt/util/debug_console.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct Std3DUnknown Std3DUnknown;

typedef struct Std3DUnknownVtbl {
	void *queryInterface;
	void *addRef;
	uint32_t(AERON_DXAPI *release)(Std3DUnknown *self);
} Std3DUnknownVtbl;

struct Std3DUnknown {
	const Std3DUnknownVtbl *lpVtbl;
};

/* Render options in the bits of Std3DRenderStateFlags, plus 0x1
 * perspective-correct texturing, 0x2 dither, 0x4 specular, 0x8 antialias, and
 * 0x10 and 0x20 subpixel, as std3D_SetInitialRenderState reads them.
 * std3D_Startup sets 0x19B3: perspective, dither, both subpixel bits, linear
 * filtering both ways, z compare and z write. The only other writer,
 * std3D_SetCapFlags, has no caller. */
// GLOBAL: XVT 0x528C50
unsigned int g_std3DRenderOptionFlags;
/* Render-state bits last written into the execute buffer; std3D_SetRenderState
 * writes only the states whose bits differ from them, then stores the new bits.
 * std3D_SetInitialRenderState sets it to g_std3DRenderOptionFlags. */
// GLOBAL: XVT 0x528C54
Std3DRenderStateFlags g_d3dStateFlags = 0;
/* Devices in g_std3DDevices, 0 to 4: std3D_Startup sets 0 and
 * std3D_EnumDevicesCallback adds each. */
// GLOBAL: XVT 0x528C58
unsigned int g_std3DNumDevices = 0;
/* Index in g_std3DDevices of the device std3D_CreateDevice opened. */
// GLOBAL: XVT 0xA90A68
unsigned int g_std3DCurDeviceIdx = 0;
/* The device std3D_CreateDevice opened, in g_std3DDevices; NULL before. */
// GLOBAL: XVT 0x528C5C
Std3DDevice *g_pStd3DCurDevice = 0;
/* Formats in g_std3DTextureFormats, 0 to 8: std3D_CreateDevice sets 0 and
 * std3D_EnumTextureFormats adds each it keeps. */
// GLOBAL: XVT 0x528C60
int g_std3DNumTextureFormats = 0;
/* The texture format closest to the render target's 16-bit RGB, picked by
 * std3D_CreateDevice. */
// GLOBAL: XVT 0x528C64
Std3DTexFmt *g_pFmtOpaqueTexture;
/* Index of g_pFmtOpaqueTexture in g_std3DTextureFormats; only debug prints read
 * it. */
// GLOBAL: XVT 0xA90B58
int g_fmtIdxOpaqueTexture = 0;
/* The texture format closest to RGBA with 5, 5, 5 and 1 bits, picked by
 * std3D_CreateDevice when the device has alpha textures; NULL otherwise. */
// GLOBAL: XVT 0x528C68
Std3DTexFmt *g_pFmtRGBA1555 = 0;
/* Index of g_pFmtRGBA1555 in g_std3DTextureFormats; only debug prints read
 * it. */
// GLOBAL: XVT 0xA91B88
int g_fmtIdxRGBA1555 = 0;
/* The texture format closest to RGBA with 4 bits each, picked by
 * std3D_CreateDevice when the device has alpha textures but no alpha blending;
 * NULL otherwise. */
// GLOBAL: XVT 0x528C6C
Std3DTexFmt *g_pFmtRGBA4444 = 0;
/* Index of g_pFmtRGBA4444 in g_std3DTextureFormats; only debug prints read
 * it. */
// GLOBAL: XVT 0xA90A64
int g_fmtIdxRGBA4444 = 0;
/* 1 after std3D_Startup succeeds, 0 after std3D_Shutdown; nothing reads it. */
// GLOBAL: XVT 0x528CB8
static int g_std3DStartupDone = 0;
/* 1 when the open device draws with a z-buffer: std3D_CreateDevice sets it when
 * asked to, when the device has one, and when g_std3DRenderOptionFlags has a
 * 0x1800 bit. Starts at 1, and std3D_Startup sets 1. */
// GLOBAL: XVT 0x528CB0
int g_std3DZBufferEnabled = 1;
/* The DirectDraw object, which std3D_Startup takes from
 * Renderer_GetDirectDraw. */
// GLOBAL: XVT 0x528CB4
IDirectDraw *g_std3DDirectDraw = 0;
/* The texture formats the open device offers, filled by
 * std3D_EnumTextureFormats, which skips 4-bit palette formats. */
// GLOBAL: XVT 0xA90C00
Std3DTexFmt g_std3DTextureFormats[8] = {0};
/* Message std3D_Shutdown passes to DebugPrintf, whose calls print nothing. */
// GLOBAL: XVT 0x529168
static const char g_std3DShutdownSucceededMessage[] = "Shutdown Succeeded.\n";
/* What std3D_LookupErrorString returns for a code the table lacks. */
// GLOBAL: XVT 0x5293B0
static const char g_std3DUnknownErrorMessage[] = "Unknown Error";
/* Format passed to DebugPrintf, whose calls print nothing, when a scene does
 * not begin. */
// GLOBAL: XVT 0x529470
static const char g_std3DBeginSceneErrorFormat[] =
	"Error %s beginning scene.\n";
/* Format passed to DebugPrintf, whose calls print nothing, when a scene does
 * not end. */
// GLOBAL: XVT 0x52948C
static const char g_std3DEndSceneErrorFormat[] = "Error %s ending scene.\n";
/* Format passed to DebugPrintf, whose calls print nothing, when the execute
 * buffer does not lock. */
// GLOBAL: XVT 0x5294A4
static const char g_std3DLockExecuteBufferErrorFormat[] =
	"Error %s locking D3D Execute buffer.\n";
/* Rows of a result code and its name, for the Direct3D and then the DirectDraw
 * codes, that std3D_LookupErrorString searches. */
// GLOBAL: XVT 0x528CC0
const Std3DErrorStringEntry g_std3DErrorStringTable[121] = {
	{0, "D3D_OK"},
	{-2005531972, "D3DERR_BADMAJORVERSION"},
	{-2005531971, "D3DERR_BADMINORVERSION"},
	{-2005531961, "D3DERR_EXECUTE_DESTROY_FAILED"},
	{-2005531960, "D3DERR_EXECUTE_LOCK_FAILED"},
	{-2005531959, "D3DERR_EXECUTE_UNLOCK_FAILED"},
	{-2005531958, "D3DERR_EXECUTE_LOCKED"},
	{-2005531957, "D3DERR_EXECUTE_NOT_LOCKED"},
	{-2005531955, "D3DERR_EXECUTE_CLIPPED_FAILED"},
	{-2005531951, "D3DERR_TEXTURE_CREATE_FAILED"},
	{-2005531950, "D3DERR_TEXTURE_DESTROY_FAILED"},
	{-2005531949, "D3DERR_TEXTURE_LOCK_FAILED"},
	{-2005531948, "D3DERR_TEXTURE_UNLOCK_FAILED"},
	{-2005531947, "D3DERR_TEXTURE_LOAD_FAILED"},
	{-2005531946, "D3DERR_TEXTURE_SWAP_FAILED"},
	{-2005531945, "D3DERR_TEXTURE_LOCKED"},
	{-2005531944, "D3DERR_TEXTURE_NOT_LOCKED"},
	{-2005531943, "D3DERR_TEXTURE_GETSURF_FAILED"},
	{-2005531941, "D3DERR_MATRIX_DESTROY_FAILED"},
	{-2005531940, "D3DERR_MATRIX_SETDATA_FAILED"},
	{-2005531939, "D3DERR_MATRIX_GETDATA_FAILED"},
	{-2005531938, "D3DERR_SETVIEWPORTDATA_FAILED"},
	{-2005531931, "D3DERR_MATERIAL_DESTROY_FAILED"},
	{-2005531930, "D3DERR_MATERIAL_SETDATA_FAILED"},
	{-2005531929, "D3DERR_MATERIAL_GETDATA_FAILED"},
	{-2005531911, "D3DERR_SCENE_NOT_IN_SCENE"},
	{-2005531910, "D3DERR_SCENE_BEGIN_FAILED"},
	{-2005531909, "D3DERR_SCENE_END_FAILED"},
	{0, "DD_OK"},
	{-2005532667, "DDERR_ALREADYINITIALIZED"},
	{-2005532662, "DDERR_CANNOTATTACHSURFACE"},
	{-2005532652, "DDERR_CANNOTDETACHSURFACE"},
	{-2005532632, "DDERR_CURRENTLYNOTAVAIL"},
	{-2005532617, "DDERR_EXCEPTION"},
	{-2147467259, "DDERR_GENERIC"},
	{-2005532582, "DDERR_HEIGHTALIGN"},
	{-2005532577, "DDERR_INCOMPATIBLEPRIMARY"},
	{-2005532572, "DDERR_INVALIDCAPS"},
	{-2005532562, "DDERR_INVALIDCLIPLIST"},
	{-2005532552, "DDERR_INVALIDMODE"},
	{-2005532542, "DDERR_INVALIDOBJECT"},
	{-2147024809, "DDERR_INVALIDPARAMS"},
	{-2005532527, "DDERR_INVALIDPIXELFORMAT"},
	{-2005532522, "DDERR_INVALIDRECT"},
	{-2005532512, "DDERR_LOCKEDSURFACES"},
	{-2005532502, "DDERR_NO3D"},
	{-2005532492, "DDERR_NOALPHAHW"},
	{-2005532467, "DDERR_NOCLIPLIST"},
	{-2005532462, "DDERR_NOCOLORCONVHW"},
	{-2005532460, "DDERR_NOCOOPERATIVELEVELSET"},
	{-2005532457, "DDERR_NOCOLORKEY"},
	{-2005532452, "DDERR_NOCOLORKEYHW"},
	{-2005532450, "DDERR_NODIRECTDRAWSUPPORT"},
	{-2005532447, "DDERR_NOEXCLUSIVEMODE"},
	{-2005532442, "DDERR_NOFLIPHW"},
	{-2005532432, "DDERR_NOGDI"},
	{-2005532422, "DDERR_NOMIRRORHW"},
	{-2005532417, "DDERR_NOTFOUND"},
	{-2005532412, "DDERR_NOOVERLAYHW"},
	{-2005532392, "DDERR_NORASTEROPHW"},
	{-2005532382, "DDERR_NOROTATIONHW"},
	{-2005532362, "DDERR_NOSTRETCHHW"},
	{-2005532356, "DDERR_NOT4BITCOLOR"},
	{-2005532355, "DDERR_NOT4BITCOLORINDEX"},
	{-2005532352, "DDERR_NOT8BITCOLOR"},
	{-2005532342, "DDERR_NOTEXTUREHW"},
	{-2005532337, "DDERR_NOVSYNCHW"},
	{-2005532332, "DDERR_NOZBUFFERHW"},
	{-2005532322, "DDERR_NOZOVERLAYHW"},
	{-2005532312, "DDERR_OUTOFCAPS"},
	{-2147024882, "DDERR_OUTOFMEMORY"},
	{-2005532292, "DDERR_OUTOFVIDEOMEMORY"},
	{-2005532290, "DDERR_OVERLAYCANTCLIP"},
	{-2005532288, "DDERR_OVERLAYCOLORKEYONLYONEACTIVE"},
	{-2005532285, "DDERR_PALETTEBUSY"},
	{-2005532272, "DDERR_COLORKEYNOTSET"},
	{-2005532262, "DDERR_SURFACEALREADYATTACHED"},
	{-2005532252, "DDERR_SURFACEALREADYDEPENDENT"},
	{-2005532242, "DDERR_SURFACEBUSY"},
	{-2005532232, "DDERR_SURFACEISOBSCURED"},
	{-2005532222, "DDERR_SURFACELOST"},
	{-2005532212, "DDERR_SURFACENOTATTACHED"},
	{-2005532202, "DDERR_TOOBIGHEIGHT"},
	{-2005532192, "DDERR_TOOBIGSIZE"},
	{-2005532182, "DDERR_TOOBIGWIDTH"},
	{-2147467263, "DDERR_UNSUPPORTED"},
	{-2005532162, "DDERR_UNSUPPORTEDFORMAT"},
	{-2005532152, "DDERR_UNSUPPORTEDMASK"},
	{-2005532135, "DDERR_VERTICALBLANKINPROGRESS"},
	{-2005532132, "DDERR_WASSTILLDRAWING"},
	{-2005532112, "DDERR_XALIGN"},
	{-2005532111, "DDERR_INVALIDDIRECTDRAWGUID"},
	{-2005532110, "DDERR_DIRECTDRAWALREADYCREATED"},
	{-2005532109, "DDERR_NODIRECTDRAWHW"},
	{-2005532108, "DDERR_PRIMARYSURFACEALREADYEXISTS"},
	{-2005532107, "DDERR_NOEMULATION"},
	{-2005532106, "DDERR_REGIONTOOSMALL"},
	{-2005532105, "DDERR_CLIPPERISUSINGHWND"},
	{-2005532104, "DDERR_NOCLIPPERATTACHED"},
	{-2005532103, "DDERR_NOHWND"},
	{-2005532102, "DDERR_HWNDSUBCLASSED"},
	{-2005532101, "DDERR_HWNDALREADYSET"},
	{-2005532100, "DDERR_NOPALETTEATTACHED"},
	{-2005532099, "DDERR_NOPALETTEHW"},
	{-2005532098, "DDERR_BLTFASTCANTCLIP"},
	{-2005532097, "DDERR_NOBLTHW"},
	{-2005532096, "DDERR_NODDROPSHW"},
	{-2005532095, "DDERR_OVERLAYNOTVISIBLE"},
	{-2005532094, "DDERR_NOOVERLAYDEST"},
	{-2005532093, "DDERR_INVALIDPOSITION"},
	{-2005532092, "DDERR_NOTAOVERLAYSURFACE"},
	{-2005532091, "DDERR_EXCLUSIVEMODEALREADYSET"},
	{-2005532090, "DDERR_NOTFLIPPABLE"},
	{-2005532089, "DDERR_CANTDUPLICATE"},
	{-2005532088, "DDERR_NOTLOCKED"},
	{-2005532087, "DDERR_CANTCREATEDC"},
	{-2005532086, "DDERR_NODC"},
	{-2005532085, "DDERR_WRONGMODE"},
	{-2005532084, "DDERR_IMPLICITLYCREATED"},
	{-2005532083, "DDERR_NOTPALETTIZED"},
	{-2005532082, "DDERR_UNSUPPORTEDMODE"},
};
/* Fog table end that std3D_SetRenderState writes when fog turns on. Only
 * std3D_SetFogTableRangeBits writes it, and nothing calls that, so it stays
 * 0. */
// GLOBAL: XVT 0x6644F0
static unsigned int g_std3DFogTableEndBits;
/* Blue of the fog color, 0 to 255, that std3D_SetRenderState writes when fog
 * turns on. Only std3D_SetFogColor8 writes it, and nothing calls that, so it
 * stays 0. */
// GLOBAL: XVT 0x6644F4
static unsigned int g_std3DFogColorBlue8;
/* Fog table start, written and read as g_std3DFogTableEndBits is; it stays
 * 0. */
// GLOBAL: XVT 0x664544
static unsigned int g_std3DFogTableStartBits;
/* Red of the fog color, written and read as g_std3DFogColorBlue8 is; it stays
 * 0. */
// GLOBAL: XVT 0x664750
static unsigned int g_std3DFogColorRed8;
/* 16-bit colors of the palette being uploaded in the opaque format:
 * std3D_CopyPaletteToScratch16 copies them in. std3D_AddToTextureCache looks up
 * 8-bit texels through it, and takes entry 0 as the color key when the device
 * has no alpha textures. */
// GLOBAL: XVT 0x664770
uint16_t g_std3DPaletteScratch16[256];
/* The palette in the RGBA 4-bit format, for translucent textures. Only
 * std3D_SetPaletteConversionSource fills it, and nothing calls that, so it
 * stays 0. */
// GLOBAL: XVT 0x6642F0
uint16_t g_texConvBuf4444[256] = {0};
/* The palette in the RGBA 5, 5, 5 and 1 bit format, for color-keyed textures on
 * a device with alpha textures; std3D_ConvertPaletteTo1555 fills it. */
// GLOBAL: XVT 0x664550
uint16_t g_texConvBuf1555[256] = {0};
/* A copy of the last palette given to std3D_SetPaletteConversionSource, which
 * nothing calls; nothing reads it. */
// GLOBAL: XVT 0x664980
static uint8_t g_std3DPaletteConversionSourceRgb[768] = {0};
/* Green of the fog color, written and read as g_std3DFogColorBlue8 is; it stays
 * 0. */
// GLOBAL: XVT 0x664978
static unsigned int g_std3DFogColorGreen8;
/* The Direct3D interface std3D_Startup gets from the DirectDraw object;
 * std3D_Shutdown releases it, and the modern build then sets it to NULL. */
// GLOBAL: XVT 0x664970
static IDirect3D *g_lpD3D = 0;
/* The Direct3D viewport std3D_CreateViewport makes; std3D_Close releases it and
 * sets it to NULL. */
// GLOBAL: XVT 0x664974
static IDirect3DViewport *g_d3dViewport = 0;
/* Released by std3D_Close when set, but nothing sets it, so it stays NULL. */
// GLOBAL: XVT 0x66497C
static Std3DUnknown *g_d3dViewportMaterial = 0;
/* The Direct3D device std3D_CreateDevice gets from the render surface;
 * std3D_Close releases it and sets it to NULL. */
// GLOBAL: XVT 0x664548
IDirect3DDevice *g_d3dDevice;
/* The surface Direct3D draws into, set by std3D_SetRenderSurface;
 * Renderer_InitD3DDevice passes g_flightBackBuffer. */
// GLOBAL: XVT 0xA91B14
struct IDirectDrawSurface *g_std3DRenderSurface;
/* The Direct3D devices std3D_EnumDevicesCallback found, up to 4. */
// GLOBAL: XVT 0xA91120
Std3DDevice g_std3DDevices[4] = {{0}};
/* Red of the full-viewport color overlay. Set by std3D_SetColorOverlayParams,
 * which Renderer_InitD3DDevice calls with 0 and off; read by
 * std3D_DrawColorOverlay, which nothing calls. */
// GLOBAL: XVT 0xA90B10
float g_std3DColorOverlayRed;
/* Green of the color overlay, set and read as g_std3DColorOverlayRed is. */
// GLOBAL: XVT 0xA90B14
float g_std3DColorOverlayGreen;
/* Blue of the color overlay, set and read as g_std3DColorOverlayRed is. */
// GLOBAL: XVT 0xA90B18
float g_std3DColorOverlayBlue;
/* Nonzero when the color overlay is on; set and read as g_std3DColorOverlayRed
 * is. */
// GLOBAL: XVT 0xA90B1C
int g_std3DColorOverlayEnabled;
/* The z test of the open device: 16 (greater) when its compare caps have the
 * 0x10 bit, else 2 (less); std3D_CreateDevice sets it when the z-buffer is
 * enabled, else it stays 0. With 2, RenderScene_EmitFlightVertex writes 1 minus
 * its z value, so that a nearer point has the smaller value. */
// GLOBAL: XVT 0xA90B20
int g_std3DZCompareCap = 0;
/* Two triangles over the viewport quad in g_std3DQuadVerts, alpha-blended with
 * mono off; std3D_BuildViewportQuad sets them and std3D_DrawColorOverlay draws
 * them. */
// GLOBAL: XVT 0xA90B30
Std3DRenderTri g_std3DViewportQuadTriangles[2] = {{0}};
/* The viewport's corners, clockwise from the top left, set by
 * std3D_BuildViewportQuad with every other field 0. */
// GLOBAL: XVT 0xA90B60
D3DTLVERTEX g_std3DQuadVerts[4] = {{0}};
/* The viewport rectangle std3D_BuildViewportQuad was given: 0, 0 and the render
 * target's size. std3D_ClearZBuffer clears this area. */
// GLOBAL: XVT 0xA90BF0
static Std3DViewportRect g_std3DQuadRect = {0};
/* The texture std3D_DrawColorOverlay uploads and removes again each time it
 * draws without alpha blending. */
// GLOBAL: XVT 0xA90A70
Std3DTexCacheNode g_std3DColorOverlayTexNode = {0};
/* The z-buffer surface and its description, made by std3D_CreateZBuffer. */
// GLOBAL: XVT 0xA91A34
Std3DZBufferSurfaceBlock g_std3DZBufferSurfaceBlock = {0};
/* A record of the z-buffer that std3D_CreateZBuffer fills; only its debug print
 * reads it. */
// GLOBAL: XVT 0xA919D0
Std3DZBufferTarget g_std3DZBufferTarget = {0};
/* Only std3D_SetTextureSizeCaps writes it, and nothing calls that, so it stays
 * 0. */
// GLOBAL: XVT 0x528C70
int g_std3DMinTextureWidth;
/* Only std3D_SetTextureSizeCaps writes it, and nothing calls that, so it stays
 * 0. */
// GLOBAL: XVT 0x528C74
int g_std3DMinTextureHeight;
/* Only std3D_SetTextureSizeCaps writes it, and nothing calls that, so it stays
 * 0. */
// GLOBAL: XVT 0x528C78
int g_std3DMaxTextureWidth;
/* Only std3D_SetTextureSizeCaps writes it, and nothing calls that; nothing
 * reads it. */
// GLOBAL: XVT 0x528C7C
int g_std3DMaxTextureHeight;
/* Most vertices one execute buffer takes: the device's maxVertexCount, at most
 * 512, or 512 when it states none; set by std3D_CreateDevice. std3D_AddVertices
 * refuses more. */
// GLOBAL: XVT 0x528C80
unsigned int g_std3DExecBufMaxVerts = 0;
/* Counter each std3D_LockExecuteBuffer raises; a texture used in the current
 * execute buffer carries it in cacheBatchTag. std3D_CreateDevice and
 * std3D_FlushTextureCache set it to 1. */
// GLOBAL: XVT 0x528C84
unsigned int g_std3DTextureBatchTag = 1;
/* Textures on the cache list. */
// GLOBAL: XVT 0x528C88
int g_texCacheCount = 0;
/* Least recently used texture on the cache list; NULL when it is empty. */
// GLOBAL: XVT 0x528C8C
Std3DTexCacheNode *g_pTexCacheHead = 0;
/* Most recently used texture on the cache list; NULL when it is empty. */
// GLOBAL: XVT 0x528C90
Std3DTexCacheNode *g_pTexCacheTail = 0;
/* A 32 by 32 buffer in the RGBA 4-bit format, made by std3D_CreateDevice when
 * the device has alpha textures but no alpha blending; std3D_DrawColorOverlay
 * fills it and std3D_Close frees it. */
// GLOBAL: XVT 0x528C94
Std3DVBuffer *g_pStd3DVBuffer = 0;
/* The execute buffer every batch is written into; std3D_CreateDevice makes it
 * and std3D_Close releases it. */
// GLOBAL: XVT 0x528C98
IDirect3DExecuteBuffer *g_d3dExecuteBuffer = 0;
/* Size of the execute buffer in bytes: the device's maxBufferSize, or 0x10000
 * when it states none. */
// GLOBAL: XVT 0x528C9C
unsigned int g_std3DExecBufSize = 0;
/* Vertices in the execute buffer since std3D_LockExecuteBuffer set it to 0. */
// GLOBAL: XVT 0x528CA0
int g_d3dBufVertCount = 0;
/* Triangles added since the last lock; nothing reads it. */
// GLOBAL: XVT 0x528CA4
static unsigned int g_std3DExecBufTriCount = 0;
/* The texture the execute buffer last selected. std3D_LockExecuteBuffer sets it
 * to 1, which no texture is, so the first triangle group always writes a
 * texture state. */
// GLOBAL: XVT 0x528CA8
static Std3DTexCacheNode *g_d3dCurTexture = (Std3DTexCacheNode *)1;
/* Points at g_std3DZBufferSurfaceBlock once std3D_CreateZBuffer runs; NULL
 * before. */
// GLOBAL: XVT 0x528CAC
Std3DZBufferSurfaceBlock *g_pStd3DZBufferState = NULL;
/* 1 while a device is open: std3D_CreateDevice sets it on success and
 * std3D_Close clears it. */
// GLOBAL: XVT 0x528CBC
int g_std3DDeviceOpen = 0;
/* Start of the locked execute buffer. */
// GLOBAL: XVT 0x664754
static uint8_t *g_d3dExecBufBase = 0;
/* Description of the execute buffer: std3D_CreateDevice sets its size, and each
 * lock fills in where it lies. */
// GLOBAL: XVT 0x664758
D3DEXECUTEBUFFERDESC g_d3dExecBufDesc = {0};
/* Where the next vertex or instruction goes in the locked execute buffer; all
 * bits set until the first lock. */
// GLOBAL: XVT 0x664C80
uint8_t *g_d3dWritePtr = (uint8_t *)(uintptr_t)-1;
/* Where the instructions start, after the vertices; set by
 * std3D_BeginInstructions. */
// GLOBAL: XVT 0x664C84
static uint8_t *g_d3dInstrStart = 0;
/* The render target's description, filled by std3D_InitRenderTargetDesc. */
// GLOBAL: XVT 0x6644F8
Std3DRenderTargetDesc g_std3DRenderTargetDesc = {0};
/* Points at g_std3DRenderTargetDesc once std3D_InitRenderTargetDesc runs; NULL
 * before. */
// GLOBAL: XVT 0xA90BE0
Std3DRenderTargetDesc *g_pStd3DRenderTarget = 0;

/* Copies colorCount 16-bit colors into g_std3DPaletteScratch16. Does not check
 * colorCount against its 256 entries. */
// FUNCTION: XVT 0x4B0B60
void std3D_CopyPaletteToScratch16(const uint16_t *palette, int colorCount)
{
	memcpy(g_std3DPaletteScratch16, palette,
	       (size_t)colorCount * sizeof(*palette));
}

/* Fills g_texConvBuf1555 from colorCount colors in the opaque texture format: a
 * plain copy when that format is the 1555 one; otherwise each channel is taken
 * up to 8 bits and down into the 1555 format, and every entry but 0 gets full
 * alpha, so entry 0 is transparent. Does not check colorCount against 256. */
// FUNCTION: XVT 0x4B0B90
void std3D_ConvertPaletteTo1555(const uint16_t *palette, int colorCount)
{
	int colorIndex;
	ColorInfo *sourceFormat;
	ColorInfo *targetFormat;

	if (g_pFmtOpaqueTexture == g_pFmtRGBA1555) {
		memcpy(g_texConvBuf1555, palette,
		       (size_t)colorCount * sizeof(*palette));
	} else {
		sourceFormat = &g_pFmtOpaqueTexture->colorInfo;
		targetFormat = &g_pFmtRGBA1555->colorInfo;
		for (colorIndex = 0; colorIndex < colorCount; ++colorIndex) {
			uint8_t channel;

			channel = (uint8_t)((palette[colorIndex] >>
					     sourceFormat->redPosShift)
					    << sourceFormat->redPosShiftRight);
			g_texConvBuf1555[colorIndex] =
				(uint16_t)((channel >>
					    targetFormat->redPosShiftRight)
					   << targetFormat->redPosShift);
			channel =
				(uint8_t)((palette[colorIndex] >>
					   sourceFormat->greenPosShift)
					  << sourceFormat->greenPosShiftRight);
			g_texConvBuf1555[colorIndex] |=
				(uint16_t)((channel >>
					    targetFormat->greenPosShiftRight)
					   << targetFormat->greenPosShift);
			channel = (uint8_t)((palette[colorIndex] >>
					     sourceFormat->bluePosShift)
					    << sourceFormat->bluePosShiftRight);
			g_texConvBuf1555[colorIndex] |=
				(uint16_t)((channel >>
					    targetFormat->bluePosShiftRight)
					   << targetFormat->bluePosShift);
			if (colorIndex != 0) {
				channel = 0xff;
				g_texConvBuf1555[colorIndex] |=
					(uint16_t)((channel >>
						    targetFormat
							    ->alphaPosShiftRight)
						   << targetFormat
							      ->alphaPosShift);
			}
		}
	}
}

/* Takes the DirectDraw object, sets g_std3DRenderOptionFlags to 0x19B3 and
 * g_std3DZBufferEnabled to 1, gets the Direct3D interface into g_lpD3D and
 * enumerates the devices into g_std3DDevices. Returns 1, setting
 * g_std3DStartupDone, when at least one device was found; 0 when there is no
 * DirectDraw object, getting the interface or the enumeration fails, or no
 * device is found. */
// FUNCTION: XVT 0x4B0DB0
int std3D_Startup(void)
{
	int result;

	g_std3DDirectDraw = Renderer_GetDirectDraw();
	if (g_std3DDirectDraw == NULL) {
		DebugPrintf("DDraw device not created yet!\n", 0, 0, 0, 0);
		return 0;
	}

	g_std3DRenderOptionFlags = 0x19b3;
	g_std3DZBufferEnabled = 1;
	DebugPrintf("Creating D3D interface object.\n", 0, 0, 0, 0);
	result = g_std3DDirectDraw->lpVtbl->QueryInterface(
		g_std3DDirectDraw, &CLSID_IDirect3D, (void **)&g_lpD3D);
	if (result != 0) {
		DebugPrintf("Error %s creating Direct3D interface object.\n",
			    std3D_LookupErrorString(
				    result, g_std3DErrorStringTable, 121),
			    0, 0, 0);
		return 0;
	}

	DebugPrintf("Enumerating D3D devices.\n", 0, 0, 0, 0);
	g_std3DNumDevices = 0;
	result = g_lpD3D->lpVtbl->EnumDevices(g_lpD3D,
					      std3D_EnumDevicesCallback, NULL);
	if (result != 0) {
		DebugPrintf("Error %s when enumerating D3D devices.\n",
			    std3D_LookupErrorString(
				    result, g_std3DErrorStringTable, 121),
			    0, 0, 0);
		return 0;
	}
	if (g_std3DNumDevices == 0) {
		return 0;
	}

	DebugPrintf(
		"%d D3D devices found.\n", g_std3DNumDevices,
		std3D_LookupErrorString(result, g_std3DErrorStringTable, 121),
		0, 0);
	g_std3DStartupDone = 1;
	DebugPrintf("Startup Succeeded.\n", 0, 0, 0, 0);
	return 1;
}

/* Fills g_std3DRenderTargetDesc for a 16-bit RGB target (5, 6 and 5 bits) of
 * width by height pixels and pitchBytes per row, with pitchPixels of pitchBytes
 * / 2 and sizeBytes of pitch times height; points g_pStd3DRenderTarget at it
 * and returns it. */
// FUNCTION: XVT 0x4B0EF0
Std3DRenderTargetDesc *std3D_InitRenderTargetDesc(unsigned int width,
						  unsigned int height,
						  int pitchBytes)
{
	Std3DRenderTargetDesc *result;
	Std3DRenderTargetDesc **renderTarget;

	memset(&g_std3DRenderTargetDesc, 0, sizeof(g_std3DRenderTargetDesc));
	renderTarget = &g_pStd3DRenderTarget;
	*renderTarget = &g_std3DRenderTargetDesc;
	g_std3DRenderTargetDesc.width = width;
	(*renderTarget)->height = height;
	g_pStd3DRenderTarget->pitch = pitchBytes;
	g_pStd3DRenderTarget->pitchPixels = pitchBytes / 2;
	g_pStd3DRenderTarget->sizeBytes =
		g_pStd3DRenderTarget->pitch * g_pStd3DRenderTarget->height;
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

/* Releases g_lpD3D when it is set (the modern build then sets it to NULL) and
 * sets g_std3DStartupDone to 0. */
// FUNCTION: XVT 0x4B0FF0
void std3D_Shutdown(void)
{
	if (g_lpD3D != 0) {
		g_lpD3D->lpVtbl->Release(g_lpD3D);
#ifdef XVT_MODERN
		g_lpD3D = NULL;
#endif
	}
	DebugPrintf(g_std3DShutdownSucceededMessage, 0, 0, 0, 0);
	g_std3DStartupDone = 0;
}

/* Opens device deviceIdx of g_std3DDevices on the render surface. Returns 0
 * when a device is already open, the index is out of range, or a step fails:
 * the z-buffer, made when bUseZBuffer is set, the device has one and
 * g_std3DRenderOptionFlags has a 0x1800 bit (it also sets g_std3DZCompareCap);
 * the device; the texture formats, none found counting as a failure; the
 * viewport; or the initial render state. Then makes the execute buffer (a
 * failure there is only printed), empties the texture cache, picks the opaque
 * texture format and, with alpha textures, the 1555 format and, without alpha
 * blending, the 4444 format with a 32 by 32 buffer in it, reads the texture
 * memory, sets g_std3DDeviceOpen and returns 1. */
// FUNCTION: XVT 0x4B1030
int std3D_CreateDevice(unsigned int deviceIdx, int bUseZBuffer)
{
	Std3DRasterInfo raster;
	ColorInfo alphaFormat;
	unsigned int maxBufferSize;
	unsigned int maxVertexCount;
	HRESULT result;
	const char *errorString;

	if (g_std3DDeviceOpen != 0) {
		DebugPrintf("Error: Multiple Opens Attempted.\n", 0, 0, 0, 0);
		return 0;
	}
	if (g_std3DNumDevices <= deviceIdx) {
		return 0;
	}

	g_std3DCurDeviceIdx = deviceIdx;
	g_pStd3DCurDevice = &g_std3DDevices[deviceIdx];
	g_std3DZBufferEnabled = bUseZBuffer != 0 &&
				g_pStd3DCurDevice->caps.bHasZBuffer != 0 &&
				(g_std3DRenderOptionFlags & 0x1800) != 0;
	if (g_std3DZBufferEnabled != 0) {
		if (std3D_CreateZBuffer(g_pStd3DRenderTarget->width,
					g_pStd3DRenderTarget->height) == 0) {
			DebugPrintf("Error creating Z buffer.\n", 0, 0, 0, 0);
			return 0;
		}
		g_std3DZCompareCap = 16;
		if ((g_pStd3DCurDevice->caps.zCmpCapsMask & 0x10) == 0) {
			g_std3DZCompareCap = 2;
		}
		DebugPrintf("Z compare: %s\n",
			    g_std3DZCompareCap == 16 ? "Greater" : "Less", 0, 0,
			    0);
	}

	DebugPrintf("Creating D3D device #%d.\n", g_std3DCurDeviceIdx, 0, 0, 0);
	result = g_std3DRenderSurface->lpVtbl->QueryInterface(
		g_std3DRenderSurface, &g_pStd3DCurDevice->guid,
		(void **)&g_d3dDevice);
	if (result != 0) {
		errorString = std3D_LookupErrorString(
			result, g_std3DErrorStringTable, 121);
		DebugPrintf("Error %s creating Direct3D device.\n", errorString,
			    0, 0, 0);
		return 0;
	}

	g_std3DNumTextureFormats = 0;
	result = g_d3dDevice->lpVtbl->EnumTextureFormats(
		g_d3dDevice, (void *)std3D_EnumTextureFormats, NULL);
	if (result != 0) {
		errorString = std3D_LookupErrorString(
			result, g_std3DErrorStringTable, 121);
		DebugPrintf(
			"Error %s when enumerating D3D device texture formats.\n",
			errorString, 0, 0, 0);
		return 0;
	}
	if (g_std3DNumTextureFormats == 0) {
		DebugPrintf("Error: no texture formats found.\n", 0, 0, 0, 0);
		return 0;
	}

	errorString = std3D_LookupErrorString(0, g_std3DErrorStringTable, 121);
	DebugPrintf("%d texture formats found.\n", g_std3DNumTextureFormats,
		    errorString, 0);
	if (std3D_CreateViewport(g_pStd3DRenderTarget->width,
				 g_pStd3DRenderTarget->height) == 0) {
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
	if (maxBufferSize != 0) {
		g_std3DExecBufSize = maxBufferSize;
	}
	g_d3dExecBufDesc.dwFlags = 0;
	g_d3dExecBufDesc.dwCaps = 0;
	g_d3dExecBufDesc.dwBufferSize = 0;
	g_d3dExecBufDesc.lpData = NULL;
	g_d3dExecBufDesc.dwSize = 20;
	g_d3dExecBufDesc.dwFlags = 1;
	g_d3dExecBufDesc.dwBufferSize = g_std3DExecBufSize;
	maxVertexCount = g_pStd3DCurDevice->caps.maxVertexCount;
	g_std3DExecBufMaxVerts =
		maxVertexCount == 0
			? 512
			: (maxVertexCount < 512 ? maxVertexCount : 512);
	DebugPrintf("Execute buffer size: %d.\n", g_std3DExecBufSize, 0, 0, 0);
	DebugPrintf("Max vertices: %d.\n", g_std3DExecBufMaxVerts, 0, 0, 0);
	result = g_d3dDevice->lpVtbl->CreateExecuteBuffer(
		g_d3dDevice, &g_d3dExecBufDesc, &g_d3dExecuteBuffer, NULL);
	if (result != 0) {
		errorString = std3D_LookupErrorString(
			result, g_std3DErrorStringTable, 121);
		DebugPrintf("Error %s creating D3D Execute buffer.\n",
			    errorString, 0, 0, 0);
	}

	g_texCacheCount = 0;
	g_pTexCacheHead = NULL;
	g_pTexCacheTail = NULL;
	g_std3DTextureBatchTag = 1;
	g_fmtIdxOpaqueTexture = std3D_FindClosestFormat(
		&g_pStd3DRenderTarget->colorInfo, g_std3DTextureFormats,
		g_std3DNumTextureFormats);
	g_pFmtOpaqueTexture = &g_std3DTextureFormats[g_fmtIdxOpaqueTexture];
	if (g_pStd3DCurDevice->caps.bAlphaTexture != 0) {
		alphaFormat.colorMode = STDCOLOR_RGBA;
		alphaFormat.bpp = 16;
		alphaFormat.redBPP = 5;
		alphaFormat.greenBPP = 5;
		alphaFormat.blueBPP = 5;
		alphaFormat.alphaBPP = 1;
		g_fmtIdxRGBA1555 = std3D_FindClosestFormat(
			&alphaFormat, g_std3DTextureFormats,
			g_std3DNumTextureFormats);
		g_pFmtRGBA1555 = &g_std3DTextureFormats[g_fmtIdxRGBA1555];
		if (g_pStd3DCurDevice->caps.bAlphaBlend == 0) {
			alphaFormat.redBPP = 4;
			alphaFormat.greenBPP = 4;
			alphaFormat.blueBPP = 4;
			alphaFormat.alphaBPP = 4;
			g_fmtIdxRGBA4444 = std3D_FindClosestFormat(
				&alphaFormat, g_std3DTextureFormats,
				g_std3DNumTextureFormats);
			g_pFmtRGBA4444 =
				&g_std3DTextureFormats[g_fmtIdxRGBA4444];
			raster.width = 32;
			raster.height = 32;
			memcpy(&raster.colorMode, &g_pFmtRGBA4444->colorInfo,
			       sizeof(g_pFmtRGBA4444->colorInfo));
			g_pStd3DVBuffer = std3D_AllocVBuffer(&raster, 0, 0, 0);
		}
	}

	std3D_QueryTextureVidMem(&g_pStd3DCurDevice->totalMemory,
				 &g_pStd3DCurDevice->availableMemory);
	DebugPrintf("Texture Ram  Total: %d bytes  Free: %d bytes.\n",
		    g_pStd3DCurDevice->totalMemory,
		    g_pStd3DCurDevice->availableMemory, 0, 0);
	DebugPrintf("Device #%d opened successfully.\n", g_std3DCurDeviceIdx, 0,
		    0, 0);
	DebugPrintf("std3D opened.\n", 0, 0, 0, 0);
	g_std3DDeviceOpen = 1;
	return 1;
}

/* Sets g_std3DRenderSurface to surface and returns it. */
// FUNCTION: XVT 0x4B15A0
struct IDirectDrawSurface *
std3D_SetRenderSurface(struct IDirectDrawSurface *surface)
{
	return g_std3DRenderSurface = surface;
}

/* Sets the color overlay's red, green, blue and on flag; returns enabled. */
// FUNCTION: XVT 0x4B15B0
int std3D_SetColorOverlayParams(float red, float green, float blue, int enabled)
{
	g_std3DColorOverlayRed = red;
	g_std3DColorOverlayGreen = green;
	g_std3DColorOverlayBlue = blue;
	return g_std3DColorOverlayEnabled = enabled;
}

/* Returns how many times n halves before it is 1 or less: log2 of n rounded
 * down for n of 1 or more, 0 for n under 2. */
// FUNCTION: XVT 0x4B15E0
int std3D_Log2Floor(int n)
{
	int result = 0;

	while (n > 1) {
		n >>= 1;
		++result;
	}
	return result;
}

/* Returns the message of the first of entryCount entries whose code is
 * errorCode, or g_std3DUnknownErrorMessage when none is. */
// FUNCTION: XVT 0x4B1600
const char *std3D_LookupErrorString(int errorCode,
				    const Std3DErrorStringEntry *entries,
				    int entryCount)
{
	const char *result;
	int entryIndex;
	const Std3DErrorStringEntry *entry;

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

/* Fills 256 16-bit colors in pFmt's format from 256 RGB triples, each channel
 * shifted down from 8 bits and into place. For a format with alpha bits the
 * alpha is defaultAlpha, or with colorKey 0 for a color that comes out 0 and
 * 0xFF for any other; but a 1-bit format first replaces the value with 0xFF
 * minus it, and without colorKey that change carries into the next entry, so
 * the alpha alternates. With colorKey and 1-bit alpha, a color that comes out 0
 * is therefore opaque and every other transparent. */
// FUNCTION: XVT 0x4B1640
void std3D_BuildColormap16(uint8_t *pRGB888, uint16_t *pOut, ColorInfo *pFmt,
			   uint8_t defaultAlpha, int colorKey)
{
	uint16_t *output = pOut;
	ColorInfo *format = pFmt;
	uint8_t *rgb888 = pRGB888;
	int alphaBPP;
	int entriesRemaining;

	entriesRemaining = 256;
	do {
		*output = (uint16_t)((uint8_t)(rgb888[0] >>
					       format->redPosShiftRight)
				     << format->redPosShift);
		*output |= (uint16_t)((uint8_t)(rgb888[1] >>
						format->greenPosShiftRight)
				      << format->greenPosShift);
		*output |= (uint16_t)((uint8_t)(rgb888[2] >>
						format->bluePosShiftRight)
				      << format->bluePosShift);
		if ((uint8_t)colorKey != 0) {
			defaultAlpha = (uint8_t)((*output == 0) - 1);
		}
		alphaBPP = format->alphaBPP;
		if (alphaBPP == 1) {
			defaultAlpha = (uint8_t)(0xFF - defaultAlpha);
		}
		if (alphaBPP != 0) {
			*output |=
				(uint16_t)((uint8_t)(defaultAlpha >>
						     format->alphaPosShiftRight)
					   << format->alphaPosShift);
		}
		rgb888 += 3;
		++output;
		--entriesRemaining;
	} while (entriesRemaining != 0);
}

/* std3D_BuildColormap16 with alpha 0xFF and no color key. */
// FUNCTION: XVT 0x4B1710
void std3D_BuildColormapOpaque(uint8_t *pRGB888, uint16_t *pOut,
			       ColorInfo *pFmt)
{
	std3D_BuildColormap16(pRGB888, pOut, pFmt, 0xFF, 0);
}

/* std3D_BuildColormap16 with alpha 0xFF and the color key. */
// FUNCTION: XVT 0x4B1730
void std3D_BuildColormapColorKey(uint8_t *pRGB888, uint16_t *pOut,
				 ColorInfo *pFmt)
{
	std3D_BuildColormap16(pRGB888, pOut, pFmt, 0xFF, 1);
}

/* std3D_BuildColormap16 with alpha and no color key. */
// FUNCTION: XVT 0x4B1750
void std3D_BuildColormapAlpha(uint8_t *pRGB888, uint16_t *pOut, ColorInfo *pFmt,
			      uint8_t alpha)
{
	std3D_BuildColormap16(pRGB888, pOut, pFmt, alpha, 0);
}

/* Allocates a buffer in plain memory with a copy of raster and width * height *
 * (bpp >> 3) bytes of pixels, sets its rowPitch to width * (bpp >> 3) and
 * returns it. Does not check either allocation; ignores any further
 * arguments. */
// FUNCTION: XVT 0x4B1770
Std3DVBuffer *std3D_AllocVBuffer(const Std3DRasterInfo *raster, ...)
{
	Std3DVBuffer *vbuffer;

	vbuffer = (Std3DVBuffer *)malloc(sizeof(*vbuffer));
	memset(vbuffer, 0, sizeof(*vbuffer));
	vbuffer->storageType = 0;
	memcpy(&vbuffer->raster, raster, sizeof(vbuffer->raster));
	vbuffer->pixels =
		malloc(raster->width * raster->height * (raster->bpp >> 3));
	vbuffer->raster.rowPitch = raster->width * (raster->bpp >> 3);
	return vbuffer;
}

/* Releases a surface buffer's surface (storageType 1) or frees a plain buffer's
 * pixels, then zeroes and frees the buffer. */
// FUNCTION: XVT 0x4B17D0
void std3D_FreeVBuffer(Std3DVBuffer *vbuffer)
{
	if (vbuffer->storageType == 1) {
		if (vbuffer->ddSurface != NULL) {
			vbuffer->ddSurface->lpVtbl->Release(vbuffer->ddSurface);
		}
	} else {
		free(vbuffer->pixels);
	}
	memset(vbuffer, 0, sizeof(*vbuffer));
	free(vbuffer);
}

/* Adds a lock to the buffer. On the first lock of a surface buffer it locks the
 * surface, waiting, and takes its pixels and pitch; when that fails it returns
 * without counting the lock. */
// FUNCTION: XVT 0x4B1810
void std3D_LockVBuffer(Std3DVBuffer *vbuffer)
{
	DDSURFACEDESC surfaceDesc;
	HRESULT result;

	if (vbuffer->storageType == 1 && vbuffer->lockCount == 0) {
		memset(&surfaceDesc, 0, sizeof(surfaceDesc));
		surfaceDesc.dwSize = sizeof(surfaceDesc);
		result = vbuffer->ddSurface->lpVtbl->Lock(vbuffer->ddSurface,
							  NULL, &surfaceDesc,
							  DDLOCK_WAIT, NULL);
		if (result != 0) {
			DebugPrintf("Error %x locking buffer %x, surface %x\n",
				    result, vbuffer, vbuffer->ddSurface);
			return;
		}
		vbuffer->pixels = surfaceDesc.lpSurface;
		vbuffer->raster.rowPitch = surfaceDesc.lPitch;
	}

	++vbuffer->lockCount;
}

/* Undoes a lock; a buffer with none is left alone. On the last lock of a
 * surface buffer it unlocks the surface; when that fails it returns without
 * undoing the count. */
// FUNCTION: XVT 0x4B1890
void std3D_UnlockVBuffer(Std3DVBuffer *vbuffer)
{
	if ((unsigned int)vbuffer->lockCount < 1) {
		DebugPrintf("Unlock Warning: buffer %x, not locked\n", vbuffer);
		return;
	}

	if (vbuffer->lockCount == 1 && vbuffer->storageType == 1) {
		HRESULT result;

		result = vbuffer->ddSurface->lpVtbl->Unlock(vbuffer->ddSurface,
							    vbuffer->pixels);
		if (result != 0) {
			DebugPrintf(
				"Error %x unlocking buffer %x, surface %x\n",
				result, vbuffer, vbuffer->ddSurface);
			return;
		}
	}

	--vbuffer->lockCount;
}

/* Closes the open device: frees g_pStd3DVBuffer, releases the execute buffer,
 * empties the texture cache, and releases the viewport, the viewport material,
 * the z-buffer surface and the device, setting each to NULL; then clears
 * g_std3DDeviceOpen. With no device open it only makes a debug print. */
// FUNCTION: XVT 0x4B18F0
void std3D_Close(void)
{
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
		g_std3DZBufferSurfaceBlock.surface->lpVtbl->Release(
			g_std3DZBufferSurfaceBlock.surface);
		g_std3DZBufferSurfaceBlock.surface = NULL;
	}

	if (g_d3dDevice != NULL) {
		g_d3dDevice->lpVtbl->Release(g_d3dDevice);
		g_d3dDevice = NULL;
	}

	DebugPrintf("std3D closed.\n", 0, 0, 0, 0);
	g_std3DDeviceOpen = 0;
}

/* Copies all of source into destination with its top left at destinationX,
 * destinationY, row by row, locking both. Ignores sourceX and sourceY and does
 * not clip. */
// FUNCTION: XVT 0x4B19E0
void std3D_BlitVBuffer(Std3DVBuffer *destination, Std3DVBuffer *source,
		       int destinationX, int destinationY, int sourceX,
		       int sourceY)
{
	uint8_t *sourcePixels;
	uint8_t *destinationPixels;
	unsigned int rowBytes;
	unsigned int rowIndex;

	(void)sourceX;
	(void)sourceY;

	std3D_LockVBuffer(destination);
	std3D_LockVBuffer(source);

	sourcePixels = (uint8_t *)source->pixels;
	destinationPixels = (uint8_t *)destination->pixels +
			    destinationX * (destination->raster.bpp >> 3) +
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

/* Returns g_std3DRenderOptionFlags. Nothing calls this. */
// FUNCTION: XVT 0x4B1A80
unsigned int std3D_GetCapFlags(void) { return g_std3DRenderOptionFlags; }

/* Fills the whole buffer with packedColor at 8, 16 or 32 bits per pixel,
 * locking it; leaves a buffer of any other depth alone. Ignores fillMode. */
// FUNCTION: XVT 0x4B1A90
void std3D_FillVBuffer(Std3DVBuffer *vbuffer, unsigned int packedColor,
		       int fillMode)
{
	uint8_t *rowPixels;
	unsigned int rowIndex;
	unsigned int columnIndex;
	uint16_t *destination16;

	(void)fillMode;
	std3D_LockVBuffer(vbuffer);
	rowPixels = vbuffer->pixels;
	switch (vbuffer->raster.bpp) {
	case 8:
		rowIndex = 0;
		if (vbuffer->raster.height > rowIndex) {
			do {
				memset(rowPixels, (uint8_t)packedColor,
				       vbuffer->raster.width);
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
					destination16 = (uint16_t *)rowPixels;
					do {
						*destination16 =
							(uint16_t)packedColor;
						++destination16;
						++columnIndex;
					} while (vbuffer->raster.width >
						 columnIndex);
				}
				rowPixels += vbuffer->raster.rowPitch;
				++rowIndex;
			} while (vbuffer->raster.height > rowIndex);
		}
		break;
	case 32: {
		unsigned int row32;
		unsigned int column32;
		uint32_t *destination32;

		row32 = 0;
		if (vbuffer->raster.height > row32) {
			do {
				column32 = 0;
				if (vbuffer->raster.width > column32) {
					destination32 = (uint32_t *)rowPixels;
					do {
						*destination32 = packedColor;
						++destination32;
						++column32;
					} while (vbuffer->raster.width >
						 column32);
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

/* Sets g_std3DRenderOptionFlags and writes the initial render state again.
 * Nothing calls this. */
// FUNCTION: XVT 0x4B1B70
void std3D_SetCapFlags(unsigned int capFlags)
{
	int result;

	g_std3DRenderOptionFlags = capFlags;
	result = std3D_SetInitialRenderState();
	if (result == 0) {
		DebugPrintf("Error initializing render state.\n", 0, 0, 0, 0);
	}
}

/* Sets the fog color, 0 to 255 for each channel, and returns red8. Nothing
 * calls this. */
// FUNCTION: XVT 0x4B1BA0
int std3D_SetFogColor8(unsigned int red8, unsigned int green8,
		       unsigned int blue8)
{
	g_std3DFogColorRed8 = red8;
	g_std3DFogColorGreen8 = green8;
	g_std3DFogColorBlue8 = blue8;
	return red8;
}

/* Sets the fog table start and end and returns startBits. Nothing calls
 * this. */
// FUNCTION: XVT 0x4B1BC0
int std3D_SetFogTableRangeBits(unsigned int startBits, unsigned int endBits)
{
	g_std3DFogTableStartBits = startBits;
	g_std3DFogTableEndBits = endBits;
	return startBits;
}

/* Sets the four texture size limits and returns maxHeight. Nothing calls
 * this. */
// FUNCTION: XVT 0x4B1BE0
int std3D_SetTextureSizeCaps(int minWidth, int minHeight, int maxWidth,
			     int maxHeight)
{
	g_std3DMinTextureWidth = minWidth;
	g_std3DMinTextureHeight = minHeight;
	g_std3DMaxTextureWidth = maxWidth;
	return g_std3DMaxTextureHeight = maxHeight;
}

/* Begins a Direct3D scene on g_d3dDevice; a failure is only printed. */
// FUNCTION: XVT 0x4B1C10
void std3D_StartScene(void)
{
	int result;

	result = g_d3dDevice->lpVtbl->BeginScene(g_d3dDevice);
	if (result != 0) {
		DebugPrintf(g_std3DBeginSceneErrorFormat,
			    std3D_LookupErrorString(
				    result, g_std3DErrorStringTable, 121),
			    0, 0, 0);
	}
}

/* Ends the Direct3D scene; a failure is only printed. */
// FUNCTION: XVT 0x4B1C50
void std3D_EndScene(void)
{
	int result;

	result = g_d3dDevice->lpVtbl->EndScene(g_d3dDevice);
	if (result != 0) {
		DebugPrintf(g_std3DEndSceneErrorFormat,
			    std3D_LookupErrorString(
				    result, g_std3DErrorStringTable, 121),
			    0, 0, 0);
	}
}

/* Starts a batch: sets the vertex and triangle counts to 0, raises
 * g_std3DTextureBatchTag, marks no texture as selected, and locks the execute
 * buffer, pointing g_d3dExecBufBase and g_d3dWritePtr at its start. Returns 1,
 * or 0 when the lock fails. */
// FUNCTION: XVT 0x4B1C90
int std3D_LockExecuteBuffer(void)
{
	int result;

	g_d3dBufVertCount = 0;
	g_std3DExecBufTriCount = 0;
	++g_std3DTextureBatchTag;
	g_d3dCurTexture = (Std3DTexCacheNode *)1;
	result = g_d3dExecuteBuffer->lpVtbl->Lock(g_d3dExecuteBuffer,
						  &g_d3dExecBufDesc);
	if (result != 0) {
		DebugPrintf(g_std3DLockExecuteBufferErrorFormat,
			    std3D_LookupErrorString(
				    result, g_std3DErrorStringTable, 121),
			    0, 0, 0);
		return 0;
	}
	g_d3dExecBufBase = (uint8_t *)g_d3dExecBufDesc.lpData;
	g_d3dWritePtr = g_d3dExecBufBase;
	return 1;
}

/* Copies count vertices to g_d3dWritePtr, unless they are already there, and
 * advances it; returns 1, or 0 without copying when g_d3dBufVertCount would
 * pass g_std3DExecBufMaxVerts. Does not check the buffer's size. */
// FUNCTION: XVT 0x4B1D10
int std3D_AddVertices(const D3DTLVERTEX *vertices, int count)
{
	int previousVertexCount;
	uint8_t *writePtr;

	previousVertexCount = g_d3dBufVertCount;
	if ((unsigned int)(count + g_d3dBufVertCount) >
	    g_std3DExecBufMaxVerts) {
		return 0;
	}
	writePtr = g_d3dWritePtr;
	if ((const void *)writePtr != (const void *)vertices) {
		memcpy(writePtr, vertices, (size_t)count * sizeof(*vertices));
	}
	g_d3dBufVertCount = count + previousVertexCount;
	g_d3dWritePtr = writePtr + (size_t)count * sizeof(*vertices);
	return 1;
}

/* Writes, after the vertices, the instruction to copy all g_d3dBufVertCount of
 * them, records it as the start of the instructions, and returns 1. */
// FUNCTION: XVT 0x4B1D70
int std3D_BeginInstructions(void)
{
	g_d3dInstrStart = g_d3dWritePtr;
	((D3DINSTRUCTION *)g_d3dWritePtr)->bOpcode = D3DOP_PROCESSVERTICES;
	((D3DINSTRUCTION *)g_d3dWritePtr)->bSize = sizeof(D3DPROCESSVERTICES);
	((D3DINSTRUCTION *)g_d3dWritePtr)->wCount = 1;
	g_d3dWritePtr += sizeof(D3DINSTRUCTION);

	((D3DPROCESSVERTICES *)g_d3dWritePtr)->dwFlags =
		D3DPROCESSVERTICES_COPY;
	((D3DPROCESSVERTICES *)g_d3dWritePtr)->wStart = 0;
	((D3DPROCESSVERTICES *)g_d3dWritePtr)->wDest = 0;
	((D3DPROCESSVERTICES *)g_d3dWritePtr)->dwCount =
		(uint32_t)g_d3dBufVertCount;
	((D3DPROCESSVERTICES *)g_d3dWritePtr)->dwReserved = 0;
	g_d3dWritePtr += sizeof(D3DPROCESSVERTICES);
	return 1;
}

/* Writes count triangles as instructions, grouping neighbors with the same
 * texture and flags: for each group the changed render states
 * (std3D_SetRenderState), a texture handle state when the texture differs from
 * the one last selected (handle 0 for none), then one triangle instruction with
 * every edge enabled. Returns 1. Does not check the buffer's size. */
// FUNCTION: XVT 0x4B1DE0
int std3D_AddTriangles(const Std3DRenderTri *triangles, unsigned int count)
{
	Std3DTexCacheNode *texture;
	unsigned int groupStart;
	int groupCount;
	unsigned int triangleIndex;

	for (groupStart = 0; groupStart < count; groupStart += groupCount) {
		texture = triangles[groupStart].texture;
		groupCount = 0;
		if (texture == NULL) {
			Std3DRenderStateFlags flags =
				triangles[groupStart].flags;

			for (triangleIndex = groupStart; triangleIndex < count;
			     ++triangleIndex) {
				if (triangles[triangleIndex].texture != NULL ||
				    triangles[triangleIndex].flags != flags) {
					break;
				}
				++groupCount;
			}

			std3D_SetRenderState(flags);
			if (g_d3dCurTexture != NULL) {
				((D3DINSTRUCTION *)g_d3dWritePtr)->bOpcode =
					D3DOP_STATERENDER;
				((D3DINSTRUCTION *)g_d3dWritePtr)->bSize =
					sizeof(D3DSTATE);
				((D3DINSTRUCTION *)g_d3dWritePtr)->wCount = 1;
				g_d3dWritePtr += sizeof(D3DINSTRUCTION);
				((D3DSTATE *)g_d3dWritePtr)->dwState =
					D3DRENDERSTATE_TEXTUREHANDLE;
				((D3DSTATE *)g_d3dWritePtr)->dwArg = 0;
				g_d3dCurTexture = NULL;
				g_d3dWritePtr += sizeof(D3DSTATE);
			}

			((D3DINSTRUCTION *)g_d3dWritePtr)->bOpcode =
				D3DOP_TRIANGLE;
			((D3DINSTRUCTION *)g_d3dWritePtr)->bSize =
				sizeof(D3DTRIANGLE);
			((D3DINSTRUCTION *)g_d3dWritePtr)->wCount =
				(uint16_t)groupCount;
			g_d3dWritePtr += sizeof(D3DINSTRUCTION);
			for (triangleIndex = 0;
			     triangleIndex < (unsigned int)groupCount;
			     ++triangleIndex) {
				((D3DTRIANGLE *)g_d3dWritePtr)->v1 =
					(uint16_t)triangles[groupStart +
							    triangleIndex]
						.vertexIndex0;
				((D3DTRIANGLE *)g_d3dWritePtr)->v2 =
					(uint16_t)triangles[groupStart +
							    triangleIndex]
						.vertexIndex1;
				((D3DTRIANGLE *)g_d3dWritePtr)->v3 =
					(uint16_t)triangles[groupStart +
							    triangleIndex]
						.vertexIndex2;
				((D3DTRIANGLE *)g_d3dWritePtr)->wFlags =
					D3DTRIFLAG_EDGEENABLE1 |
					D3DTRIFLAG_EDGEENABLE2 |
					D3DTRIFLAG_EDGEENABLE3;
				g_d3dWritePtr += sizeof(D3DTRIANGLE);
			}
		} else {
			Std3DRenderStateFlags flags =
				triangles[groupStart].flags;

			for (triangleIndex = groupStart; triangleIndex < count;
			     ++triangleIndex) {
				if (triangles[triangleIndex].texture !=
					    texture ||
				    triangles[triangleIndex].flags != flags) {
					break;
				}
				++groupCount;
			}

			std3D_SetRenderState(flags);
			if (g_d3dCurTexture != texture) {
				((D3DINSTRUCTION *)g_d3dWritePtr)->bOpcode =
					D3DOP_STATERENDER;
				((D3DINSTRUCTION *)g_d3dWritePtr)->bSize =
					sizeof(D3DSTATE);
				((D3DINSTRUCTION *)g_d3dWritePtr)->wCount = 1;
				g_d3dWritePtr += sizeof(D3DINSTRUCTION);
				((D3DSTATE *)g_d3dWritePtr)->dwState =
					D3DRENDERSTATE_TEXTUREHANDLE;
				((D3DSTATE *)g_d3dWritePtr)->dwArg =
					texture->texHandle;
				g_d3dWritePtr += sizeof(D3DSTATE);
				g_d3dCurTexture = texture;
			}

			((D3DINSTRUCTION *)g_d3dWritePtr)->bOpcode =
				D3DOP_TRIANGLE;
			((D3DINSTRUCTION *)g_d3dWritePtr)->bSize =
				sizeof(D3DTRIANGLE);
			((D3DINSTRUCTION *)g_d3dWritePtr)->wCount =
				(uint16_t)groupCount;
			g_d3dWritePtr += sizeof(D3DINSTRUCTION);
			for (triangleIndex = 0;
			     triangleIndex < (unsigned int)groupCount;
			     ++triangleIndex) {
				((D3DTRIANGLE *)g_d3dWritePtr)->v1 =
					(uint16_t)triangles[groupStart +
							    triangleIndex]
						.vertexIndex0;
				((D3DTRIANGLE *)g_d3dWritePtr)->v2 =
					(uint16_t)triangles[groupStart +
							    triangleIndex]
						.vertexIndex1;
				((D3DTRIANGLE *)g_d3dWritePtr)->v3 =
					(uint16_t)triangles[groupStart +
							    triangleIndex]
						.vertexIndex2;
				((D3DTRIANGLE *)g_d3dWritePtr)->wFlags =
					D3DTRIFLAG_EDGEENABLE1 |
					D3DTRIFLAG_EDGEENABLE2 |
					D3DTRIFLAG_EDGEENABLE3;
				g_d3dWritePtr += sizeof(D3DTRIANGLE);
			}
		}
	}

	g_std3DExecBufTriCount += count;
	return 1;
}

/* Ends the instructions, unlocks the execute buffer and executes it, unclipped,
 * on the viewport. Returns 1, or 0 when the unlock or the execute fails. */
// FUNCTION: XVT 0x4B2020
int std3D_ExecuteBuffer(void)
{
	D3DEXECUTEDATA executeData;
	int result;

	((D3DINSTRUCTION *)g_d3dWritePtr)->bOpcode = D3DOP_EXIT;
	((D3DINSTRUCTION *)g_d3dWritePtr)->bSize = 0;
	((D3DINSTRUCTION *)g_d3dWritePtr)->wCount = 0;
	g_d3dWritePtr += sizeof(D3DINSTRUCTION);

	result = g_d3dExecuteBuffer->lpVtbl->Unlock(g_d3dExecuteBuffer);
	if (result != 0) {
		DebugPrintf("Error %s unlocking D3D Execute buffer.\n",
			    std3D_LookupErrorString(
				    result, g_std3DErrorStringTable, 121),
			    0, 0, 0);
		return 0;
	}

	memset(&executeData, 0, sizeof(executeData));
	executeData.dwSize = sizeof(executeData);
	executeData.dwVertexCount = g_d3dBufVertCount;
	executeData.dwInstructionOffset =
		(uint32_t)(g_d3dInstrStart - g_d3dExecBufBase);
	executeData.dwInstructionLength =
		(uint32_t)(g_d3dWritePtr - g_d3dInstrStart);
	g_d3dExecuteBuffer->lpVtbl->SetExecuteData(g_d3dExecuteBuffer,
						   &executeData);
	result = g_d3dDevice->lpVtbl->Execute(g_d3dDevice, g_d3dExecuteBuffer,
					      g_d3dViewport,
					      D3DEXECUTE_UNCLIPPED);
	if (result != 0) {
		DebugPrintf("Error %s executing buffer.\n",
			    std3D_LookupErrorString(
				    result, g_std3DErrorStringTable, 121),
			    0, 0, 0);
		return 0;
	}
	return 1;
}

/* Writes render-state instructions for the groups of bits in flags that differ
 * from g_d3dStateFlags, then stores flags there; does nothing when they are
 * equal. Mono is off with STD3D_RS_MONO_DISABLE. With alpha blend or
 * modulate-alpha: source blend 5, destination blend 6, texture blend 4 with
 * modulate-alpha else 2, blending on; with neither: source blend 2, destination
 * blend 1, the same texture blend, blending off. The z compare is
 * std3D_MapZCmpFunc(g_std3DZCompareCap) with STD3D_RS_Z_COMPARE_ENABLE, else 8;
 * z writes follow STD3D_RS_Z_WRITE_ENABLE. Each filter is 2 (linear) with its
 * bit, else 1. Fog is on with the fog color, table mode 3 and the table start
 * and end, or off. STD3D_RS_TEXTURE_ADDRESS_CLAMP is not written. */
// FUNCTION: XVT 0x4B2130
void std3D_SetRenderState(Std3DRenderStateFlags flags)
{
	int textureFilter;

	if (g_d3dStateFlags == flags) {
		return;
	}

	if (((unsigned int)(flags ^ g_d3dStateFlags) & STD3D_RS_MONO_DISABLE) !=
	    0) {
		((D3DINSTRUCTION *)g_d3dWritePtr)->bOpcode = D3DOP_STATERENDER;
		((D3DINSTRUCTION *)g_d3dWritePtr)->bSize = sizeof(D3DSTATE);
		((D3DINSTRUCTION *)g_d3dWritePtr)->wCount = 1;
		g_d3dWritePtr += sizeof(D3DINSTRUCTION);
		((D3DSTATE *)g_d3dWritePtr)->dwState =
			D3DRENDERSTATE_MONOENABLE;
		if ((flags & STD3D_RS_MONO_DISABLE) != 0) {
			((D3DSTATE *)g_d3dWritePtr)->dwArg = 0;
		} else {
			((D3DSTATE *)g_d3dWritePtr)->dwArg = 1;
		}
		g_d3dWritePtr += sizeof(D3DSTATE);
	}

	if (((unsigned int)(flags ^ g_d3dStateFlags) &
	     (STD3D_RS_ALPHA_BLEND | STD3D_RS_TEXTURE_MODULATE_ALPHA)) != 0) {
		((D3DINSTRUCTION *)g_d3dWritePtr)->bOpcode = D3DOP_STATERENDER;
		((D3DINSTRUCTION *)g_d3dWritePtr)->bSize = sizeof(D3DSTATE);
		((D3DINSTRUCTION *)g_d3dWritePtr)->wCount = 4;
		if ((flags & (STD3D_RS_ALPHA_BLEND |
			      STD3D_RS_TEXTURE_MODULATE_ALPHA)) != 0) {
			g_d3dWritePtr += sizeof(D3DINSTRUCTION);
			((D3DSTATE *)g_d3dWritePtr)->dwState =
				D3DRENDERSTATE_SRCBLEND;
			((D3DSTATE *)g_d3dWritePtr)->dwArg = 5;
			g_d3dWritePtr += sizeof(D3DSTATE);
			((D3DSTATE *)g_d3dWritePtr)->dwState =
				D3DRENDERSTATE_DESTBLEND;
			((D3DSTATE *)g_d3dWritePtr)->dwArg = 6;
			g_d3dWritePtr += sizeof(D3DSTATE);
			((D3DSTATE *)g_d3dWritePtr)->dwState =
				D3DRENDERSTATE_TEXTUREMAPBLEND;
			if ((flags & STD3D_RS_TEXTURE_MODULATE_ALPHA) != 0) {
				((D3DSTATE *)g_d3dWritePtr)->dwArg = 4;
			} else {
				((D3DSTATE *)g_d3dWritePtr)->dwArg = 2;
			}
			g_d3dWritePtr += sizeof(D3DSTATE);
			((D3DSTATE *)g_d3dWritePtr)->dwState =
				D3DRENDERSTATE_BLENDENABLE;
			((D3DSTATE *)g_d3dWritePtr)->dwArg = 1;
		} else {
			g_d3dWritePtr += sizeof(D3DINSTRUCTION);
			((D3DSTATE *)g_d3dWritePtr)->dwState =
				D3DRENDERSTATE_SRCBLEND;
			((D3DSTATE *)g_d3dWritePtr)->dwArg = 2;
			g_d3dWritePtr += sizeof(D3DSTATE);
			((D3DSTATE *)g_d3dWritePtr)->dwState =
				D3DRENDERSTATE_DESTBLEND;
			((D3DSTATE *)g_d3dWritePtr)->dwArg = 1;
			g_d3dWritePtr += sizeof(D3DSTATE);
			((D3DSTATE *)g_d3dWritePtr)->dwState =
				D3DRENDERSTATE_TEXTUREMAPBLEND;
			if ((flags & STD3D_RS_TEXTURE_MODULATE_ALPHA) != 0) {
				((D3DSTATE *)g_d3dWritePtr)->dwArg = 4;
			} else {
				((D3DSTATE *)g_d3dWritePtr)->dwArg = 2;
			}
			g_d3dWritePtr += sizeof(D3DSTATE);
			((D3DSTATE *)g_d3dWritePtr)->dwState =
				D3DRENDERSTATE_BLENDENABLE;
			((D3DSTATE *)g_d3dWritePtr)->dwArg = 0;
		}
		g_d3dWritePtr += sizeof(D3DSTATE);
	}

	if (((unsigned int)(flags ^ g_d3dStateFlags) &
	     (STD3D_RS_Z_COMPARE_ENABLE | STD3D_RS_Z_WRITE_ENABLE)) != 0) {
		((D3DINSTRUCTION *)g_d3dWritePtr)->bOpcode = D3DOP_STATERENDER;
		((D3DINSTRUCTION *)g_d3dWritePtr)->bSize = sizeof(D3DSTATE);
		((D3DINSTRUCTION *)g_d3dWritePtr)->wCount = 2;
		g_d3dWritePtr += sizeof(D3DINSTRUCTION);
		((D3DSTATE *)g_d3dWritePtr)->dwState = D3DRENDERSTATE_ZFUNC;
		if ((flags & STD3D_RS_Z_COMPARE_ENABLE) != 0) {
			((D3DSTATE *)g_d3dWritePtr)->dwArg =
				std3D_MapZCmpFunc(g_std3DZCompareCap);
		} else {
			((D3DSTATE *)g_d3dWritePtr)->dwArg = 8;
		}
		g_d3dWritePtr += sizeof(D3DSTATE);
		((D3DSTATE *)g_d3dWritePtr)->dwState =
			D3DRENDERSTATE_ZWRITEENABLE;
		if ((flags & STD3D_RS_Z_WRITE_ENABLE) != 0) {
			((D3DSTATE *)g_d3dWritePtr)->dwArg = 1;
		} else {
			((D3DSTATE *)g_d3dWritePtr)->dwArg = 0;
		}
		g_d3dWritePtr += sizeof(D3DSTATE);
	}

	if (((unsigned int)(flags ^ g_d3dStateFlags) &
	     (STD3D_RS_TEXTURE_MAG_LINEAR | STD3D_RS_TEXTURE_MIN_LINEAR)) !=
	    0) {
		((D3DINSTRUCTION *)g_d3dWritePtr)->bOpcode = D3DOP_STATERENDER;
		((D3DINSTRUCTION *)g_d3dWritePtr)->bSize = sizeof(D3DSTATE);
		((D3DINSTRUCTION *)g_d3dWritePtr)->wCount = 2;
		g_d3dWritePtr += sizeof(D3DINSTRUCTION);
		((D3DSTATE *)g_d3dWritePtr)->dwState =
			D3DRENDERSTATE_TEXTUREMAG;
		textureFilter =
			(flags & STD3D_RS_TEXTURE_MAG_LINEAR) != 0 ? 2 : 1;
		((D3DSTATE *)g_d3dWritePtr)->dwArg = textureFilter;
		g_d3dWritePtr += sizeof(D3DSTATE);
		((D3DSTATE *)g_d3dWritePtr)->dwState =
			D3DRENDERSTATE_TEXTUREMIN;
		textureFilter =
			(flags & STD3D_RS_TEXTURE_MIN_LINEAR) != 0 ? 2 : 1;
		((D3DSTATE *)g_d3dWritePtr)->dwArg = textureFilter;
		g_d3dWritePtr += sizeof(D3DSTATE);
	}

	if (((unsigned int)(flags ^ g_d3dStateFlags) & STD3D_RS_FOG_ENABLE) !=
	    0) {
		((D3DINSTRUCTION *)g_d3dWritePtr)->bOpcode = D3DOP_STATERENDER;
		((D3DINSTRUCTION *)g_d3dWritePtr)->bSize = sizeof(D3DSTATE);
		if ((flags & STD3D_RS_FOG_ENABLE) != 0) {
			((D3DINSTRUCTION *)g_d3dWritePtr)->wCount = 5;
			g_d3dWritePtr += sizeof(D3DINSTRUCTION);
			((D3DSTATE *)g_d3dWritePtr)->dwState =
				D3DRENDERSTATE_FOGENABLE;
			((D3DSTATE *)g_d3dWritePtr)->dwArg = 1;
			g_d3dWritePtr += sizeof(D3DSTATE);
			((D3DSTATE *)g_d3dWritePtr)->dwState =
				D3DRENDERSTATE_FOGCOLOR;
			((D3DSTATE *)g_d3dWritePtr)->dwArg =
				(g_std3DFogColorGreen8 << 8) |
				(g_std3DFogColorRed8 << 16) |
				g_std3DFogColorBlue8;
			g_d3dWritePtr += sizeof(D3DSTATE);
			((D3DSTATE *)g_d3dWritePtr)->dwState =
				D3DRENDERSTATE_FOGTABLEMODE;
			((D3DSTATE *)g_d3dWritePtr)->dwArg = 3;
			g_d3dWritePtr += sizeof(D3DSTATE);
			((D3DSTATE *)g_d3dWritePtr)->dwState =
				D3DRENDERSTATE_FOGTABLESTART;
			((D3DSTATE *)g_d3dWritePtr)->dwArg =
				g_std3DFogTableStartBits;
			g_d3dWritePtr += sizeof(D3DSTATE);
			((D3DSTATE *)g_d3dWritePtr)->dwState =
				D3DRENDERSTATE_FOGTABLEEND;
			((D3DSTATE *)g_d3dWritePtr)->dwArg =
				g_std3DFogTableEndBits;
			g_d3dWritePtr += sizeof(D3DSTATE);
		} else {
			((D3DINSTRUCTION *)g_d3dWritePtr)->wCount = 1;
			g_d3dWritePtr += sizeof(D3DINSTRUCTION);
			((D3DSTATE *)g_d3dWritePtr)->dwState =
				D3DRENDERSTATE_FOGENABLE;
			((D3DSTATE *)g_d3dWritePtr)->dwArg = 0;
			g_d3dWritePtr += sizeof(D3DSTATE);
		}
	}

	g_d3dStateFlags = flags;
}

/* Copies 768 bytes of RGB palette and builds from it the opaque palette when
 * that format is 16-bit RGB, and, with alpha textures, the color-keyed 1555
 * palette and, without alpha blending, the 4444 palette with alpha. Returns 1.
 * Nothing calls this. */
// FUNCTION: XVT 0x4B2540
int std3D_SetPaletteConversionSource(const void *paletteRgb888, uint8_t alpha)
{
	memcpy(g_std3DPaletteConversionSourceRgb, paletteRgb888,
	       sizeof(g_std3DPaletteConversionSourceRgb));
	if (g_pFmtOpaqueTexture->colorInfo.colorMode == STDCOLOR_RGB &&
	    g_pFmtOpaqueTexture->colorInfo.bpp == 16) {
		std3D_BuildColormapOpaque((uint8_t *)paletteRgb888,
					  g_std3DPaletteScratch16,
					  &g_pFmtOpaqueTexture->colorInfo);
	}
	if (g_pStd3DCurDevice->caps.bAlphaTexture != 0) {
		if (g_pFmtRGBA1555->colorInfo.colorMode == STDCOLOR_RGBA &&
		    g_pFmtRGBA1555->colorInfo.bpp == 16) {
			std3D_BuildColormapColorKey((uint8_t *)paletteRgb888,
						    g_texConvBuf1555,
						    &g_pFmtRGBA1555->colorInfo);
		}
		if (g_pStd3DCurDevice->caps.bAlphaBlend == 0 &&
		    g_pFmtRGBA4444->colorInfo.colorMode == STDCOLOR_RGBA &&
		    g_pFmtRGBA4444->colorInfo.bpp == 16) {
			std3D_BuildColormapAlpha(
				(uint8_t *)paletteRgb888, g_texConvBuf4444,
				&g_pFmtRGBA4444->colorInfo, alpha);
		}
	}
	return 1;
}

/* Writes the size a texture of srcWidth by srcHeight would get: each side at
 * least 1 and at most g_std3DMaxTextureWidth; then, when under the minimums or
 * not square on a square-only device, both sides made the larger, or each
 * raised to its minimum. With the limits at 0, as they stay, it writes 0 by 0.
 * Nothing calls this. */
// FUNCTION: XVT 0x4B25E0
void std3D_ClampTextureDimensions(int srcWidth, int srcHeight, int *outWidth,
				  int *outHeight)
{
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
		/* The height is clamped by the width limit, as in the original; g_std3DMaxTextureHeight is never
		 * read. */
		height = (uint32_t)g_std3DMaxTextureWidth;
		if (height >= (uint32_t)srcHeight) {
			height = (uint32_t)srcHeight;
		}
	} else {
		height = 1;
	}

	if (width < (unsigned int)g_std3DMinTextureWidth ||
	    height < (unsigned int)g_std3DMinTextureHeight ||
	    (g_pStd3DCurDevice->caps.bSquareOnlyTexture && width != height)) {
		if (g_pStd3DCurDevice->caps.bSquareOnlyTexture &&
		    width != height) {
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

/* Uploads source as a texture for node and puts the node at the most recently
 * used end of the cache list. Returns 1, or 0 with node cleared after any
 * failure. The size is the source's, cut to 256 on each side; a texture under
 * the minimum size, or not square on a square-only device, is first tiled into
 * a temporary buffer by whole copies, their number rounded to nearest. The
 * format is the 1555 one for a color-keyed texture on a device with alpha
 * textures, the 4444 one for a translucent texture, else the opaque one. It
 * fills a system-memory surface (8-bit texels through g_texConvBuf4444,
 * g_texConvBuf1555 or g_std3DPaletteScratch16; 16-bit rows copied as they are),
 * sets a color key for a color-keyed texture on a device without alpha textures
 * (palette entry 0, or the buffer's transparentColor), then creates the texture
 * in video memory and loads it. When video memory runs out it releases cached
 * textures from the least recently used end, stopping at the first used in the
 * current batch, until as many texels are freed, and tries again; when that
 * frees too few it fails. Sets the node's handle, size, texel count and batch
 * tag. */
// FUNCTION: XVT 0x4B2680
int std3D_AddToTextureCache(Std3DVBuffer *source, Std3DTexCacheNode *node,
			    int colorKeyed, int translucent)
{
	IDirectDrawSurface *sourceSurface;
	unsigned int width;
	unsigned int height;
	unsigned int texelCount;
	IDirect3DTexture *sourceTexture;
	IDirect3DTexture *destinationTexture;
	IDirectDrawSurface *destinationSurface;
	Std3DVBuffer *temporaryBuffer;
	Std3DVBuffer *uploadBuffer;
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
		if (width >= 256) {
			width = 256;
		}
	} else {
		width = 1;
	}
	height = source->raster.height;
	if (height >= 1) {
		if (height >= 256) {
			height = 256;
		}
	} else {
		height = 1;
	}

	if (width < (unsigned int)g_std3DMinTextureWidth ||
	    height < (unsigned int)g_std3DMinTextureHeight ||
	    (g_pStd3DCurDevice->caps.bSquareOnlyTexture && width != height)) {
		int targetWidth;
		int targetHeight;
		unsigned int horizontalCopies;
		unsigned int verticalCopies;
		unsigned int destinationX;
		unsigned int destinationY;

		resizedRaster = source->raster;
		if (g_pStd3DCurDevice->caps.bSquareOnlyTexture &&
		    width != height) {
			targetWidth = (int)width;
			if ((unsigned int)targetWidth <= height) {
				targetWidth = (int)height;
			}
			targetHeight = targetWidth;
		} else {
			targetWidth = (int)width;
			if ((unsigned int)targetWidth <=
			    (unsigned int)g_std3DMinTextureWidth) {
				targetWidth = g_std3DMinTextureWidth;
			}
			targetHeight = g_std3DMinTextureHeight;
			if ((unsigned int)targetHeight <= height) {
				targetHeight = (int)height;
			}
		}
		horizontalCopies =
			(unsigned int)((double)(unsigned int)targetWidth /
					       (double)width +
				       0.5);
		verticalCopies =
			(unsigned int)((double)(unsigned int)targetHeight /
					       (double)height +
				       0.5);
		resizedRaster.width = horizontalCopies * resizedRaster.width;
		resizedRaster.height = verticalCopies * resizedRaster.height;
		temporaryBuffer = std3D_AllocVBuffer(&resizedRaster, 0, 0, 0);
		destinationY = 0;
		while (verticalCopies != 0) {
			destinationX = 0;
			/* From here targetWidth counts down the copies left in this row, not a width. */
			for (targetWidth = horizontalCopies; targetWidth != 0;
			     --targetWidth) {
				std3D_BlitVBuffer(temporaryBuffer, source,
						  (int)destinationX,
						  (int)destinationY, 0, 1);
				destinationX += width;
			}
			destinationY += height;
			--verticalCopies;
		}
		uploadBuffer = temporaryBuffer;
		width = resizedRaster.width;
		height = resizedRaster.height;
	}

	texelCount = width * height;
	if (colorKeyed && g_pStd3DCurDevice->caps.bAlphaTexture) {
		node->usesAlphaFormat = 1;
		if (translucent) {
			surfaceDesc = g_pFmtRGBA4444->ddsd;
			DebugPrintf("Using D3D texture format #%d.\n",
				    g_fmtIdxRGBA4444, 0, 0, 0);
		} else {
			surfaceDesc = g_pFmtRGBA1555->ddsd;
			DebugPrintf("Using D3D texture format #%d.\n",
				    g_fmtIdxRGBA1555, 0, 0, 0);
		}
	} else if (translucent) {
		node->usesAlphaFormat = 1;
		surfaceDesc = g_pFmtRGBA4444->ddsd;
		DebugPrintf("Using D3D texture format #%d.\n", g_fmtIdxRGBA4444,
			    0, 0, 0);
	} else {
		node->usesAlphaFormat = 0;
		surfaceDesc = g_pFmtOpaqueTexture->ddsd;
		DebugPrintf("Using D3D texture format #%d.\n",
			    g_fmtIdxOpaqueTexture, 0, 0, 0);
	}
	surfaceDesc.dwWidth = width;
	surfaceDesc.dwHeight = height;
	surfaceDesc.dwSize = sizeof(surfaceDesc);
	surfaceDesc.dwFlags =
		DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT;
	surfaceDesc.ddsCaps.dwCaps = DDSCAPS_TEXTURE | DDSCAPS_SYSTEMMEMORY;
	do {
		result = g_std3DDirectDraw->lpVtbl->CreateSurface(
			g_std3DDirectDraw, &surfaceDesc, &sourceSurface, NULL);
		if (result) {
			DebugPrintf(
				"Error %s when creating the DirectDraw source surface.\n",
				std3D_LookupErrorString(
					result, g_std3DErrorStringTable, 121),
				0, 0, 0);
			sourceSurface = NULL;
			break;
		}

		memset(&lockedDesc, 0, sizeof(lockedDesc));
		lockedDesc.dwSize = sizeof(lockedDesc);
		result = sourceSurface->lpVtbl->Lock(
			sourceSurface, NULL, &lockedDesc, DDLOCK_WAIT, NULL);
		if (result) {
			DebugPrintf(
				"Error %s when locking the DDSurface source buffer.\n",
				std3D_LookupErrorString(
					result, g_std3DErrorStringTable, 121),
				0, 0, 0);
			break;
		}

		switch (uploadBuffer->raster.colorMode) {
		case STDCOLOR_PAL: {
			unsigned int row;

			std3D_LockVBuffer(uploadBuffer);
			if (colorKeyed &&
			    g_pStd3DCurDevice->caps.bAlphaTexture) {
				if (translucent) {
					for (row = 0; row < height; ++row) {
						uint16_t *destinationPixels;
						uint8_t *sourcePixels;
						unsigned int remaining;

						sourcePixels =
							(uint8_t *)uploadBuffer
								->pixels +
							row * uploadBuffer
									->raster
									.rowPitch;
						destinationPixels =
							(uint16_t
								 *)((uint8_t *)lockedDesc
									    .lpSurface +
								    row * lockedDesc.lPitch);
						if (width != 0) {
							remaining = width;
							do {
								*destinationPixels++ = g_texConvBuf4444
									[*sourcePixels++];
								--remaining;
							} while (remaining !=
								 0);
						}
					}
				} else {
					for (row = 0; row < height; ++row) {
						unsigned int remaining;
						uint16_t *destinationPixels;
						uint8_t *sourcePixels;

						sourcePixels =
							(uint8_t *)uploadBuffer
								->pixels +
							row * uploadBuffer
									->raster
									.rowPitch;
						destinationPixels =
							(uint16_t
								 *)((uint8_t *)lockedDesc
									    .lpSurface +
								    row * lockedDesc.lPitch);
						if (width != 0) {
							remaining = width;
							do {
								*destinationPixels++ = g_texConvBuf1555
									[*sourcePixels++];
								--remaining;
							} while (remaining !=
								 0);
						}
					}
				}
			} else if (translucent) {
				for (row = 0; row < height; ++row) {
					unsigned int remaining;
					uint8_t *sourcePixels =
						(uint8_t *)
							uploadBuffer->pixels +
						row * uploadBuffer->raster
								.rowPitch;
					uint16_t *destinationPixels =
						(uint16_t
							 *)((uint8_t *)lockedDesc
								    .lpSurface +
							    row * lockedDesc.lPitch);
					if (width != 0) {
						remaining = width;
						do {
							*destinationPixels++ = g_texConvBuf4444
								[*sourcePixels++];
							--remaining;
						} while (remaining != 0);
					}
				}
			} else {
				for (row = 0; row < height; ++row) {
					unsigned int remaining;
					uint8_t *sourcePixels =
						(uint8_t *)
							uploadBuffer->pixels +
						row * uploadBuffer->raster
								.rowPitch;
					uint16_t *destinationPixels =
						(uint16_t
							 *)((uint8_t *)lockedDesc
								    .lpSurface +
							    row * lockedDesc.lPitch);
					if (width != 0) {
						remaining = width;
						do {
							*destinationPixels++ = g_std3DPaletteScratch16
								[*sourcePixels++];
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
				const uint8_t *sourcePixels =
					(const uint8_t *)uploadBuffer->pixels +
					row * uploadBuffer->raster.rowPitch;
				uint8_t *destinationPixels =
					(uint8_t *)lockedDesc.lpSurface +
					row * lockedDesc.lPitch;
				memcpy(destinationPixels, sourcePixels,
				       width * 2);
			}
			std3D_UnlockVBuffer(uploadBuffer);
			break;
		}
		default:
			break;
		}

		result = sourceSurface->lpVtbl->Unlock(sourceSurface, NULL);
		if (result) {
			DebugPrintf(
				"Error %s when unlocking the DDSurface source buffer.\n",
				std3D_LookupErrorString(
					result, g_std3DErrorStringTable, 121),
				0, 0, 0);
			break;
		}

		if (colorKeyed && !g_pStd3DCurDevice->caps.bAlphaTexture) {
			switch (uploadBuffer->raster.colorMode) {
			case STDCOLOR_PAL:
				colorKey.dwColorSpaceLowValue =
					g_std3DPaletteScratch16[0];
				colorKey.dwColorSpaceHighValue =
					g_std3DPaletteScratch16[0];
				break;
			case STDCOLOR_RGB:
				colorKey.dwColorSpaceLowValue =
					uploadBuffer->transparentColor;
				colorKey.dwColorSpaceHighValue =
					uploadBuffer->transparentColor;
				break;
#ifdef XVT_MODERN
			default:
				colorKey.dwColorSpaceLowValue = 0;
				colorKey.dwColorSpaceHighValue = 0;
				break;
#endif
			}
			(void)sourceSurface->lpVtbl->SetColorKey(
				sourceSurface, DDCKEY_SRCBLT, &colorKey);
		}

		result = sourceSurface->lpVtbl->QueryInterface(
			sourceSurface, &CLSID_IDirect3DTexture,
			(void **)&sourceTexture);
		if (result) {
			DebugPrintf(
				"Error %s creating Direct3D source texture.\n",
				std3D_LookupErrorString(
					result, g_std3DErrorStringTable, 121),
				0, 0, 0);
			sourceTexture = NULL;
			break;
		}
		result = sourceSurface->lpVtbl->GetSurfaceDesc(sourceSurface,
							       &surfaceDesc);
		if (result) {
			DebugPrintf(
				"Error %s get surface description.\n",
				std3D_LookupErrorString(
					result, g_std3DErrorStringTable, 121),
				0, 0, 0);
#ifndef XVT_MODERN
			sourceTexture = NULL;
#endif
			break;
		}

		node->ddsd = surfaceDesc;
		surfaceDesc.dwFlags =
			DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT;
		surfaceDesc.ddsCaps.dwCaps = DDSCAPS_TEXTURE |
					     DDSCAPS_VIDEOMEMORY |
					     DDSCAPS_ALLOCONLOAD;
		result = g_std3DDirectDraw->lpVtbl->CreateSurface(
			g_std3DDirectDraw, &surfaceDesc, &destinationSurface,
			NULL);
		if (result) {
			if (result != -2005532292) {
				DebugPrintf(
					"Error %s creating texture surface.\n",
					std3D_LookupErrorString(
						result, g_std3DErrorStringTable,
						121),
					0, 0, 0);
				break;
			}
			DebugPrintf(
				"Error %s Creating surface.\n",
				std3D_LookupErrorString(
					result, g_std3DErrorStringTable, 121),
				0, 0, 0);
			DebugPrintf(
				"Assuming texture ram overflow - PURGING.\n", 0,
				0, 0, 0);
			{
				int created = 0;
				Std3DTexCacheNode *candidate = g_pTexCacheHead;

				while (!created) {
					unsigned int freed = 0;

					while (freed < texelCount &&
					       candidate != NULL &&
					       candidate->cacheBatchTag !=
						       g_std3DTextureBatchTag) {
						candidate->pCachedSurface
							->lpVtbl->Release(
								candidate
									->pCachedSurface);
						candidate->pCachedTexture
							->lpVtbl->Release(
								candidate
									->pCachedTexture);
						candidate->bCached = 0;
						freed += candidate->texelCount;
						std3D_CacheListRemove(
							candidate);
						candidate = candidate->pNext;
					}
					if (freed < texelCount) {
						DebugPrintf(
							"WARNING: Scene texture overflow occurred!!!.\n",
							0, 0, 0, 0);
						destinationSurface = NULL;
						break;
					}
					result =
						g_std3DDirectDraw->lpVtbl
							->CreateSurface(
								g_std3DDirectDraw,
								&surfaceDesc,
								&destinationSurface,
								NULL);
					if (!result) {
						created = 1;
						DebugPrintf(
							"Success adding new texture after purge.\n",
							0, 0, 0, 0);
					} else if (result != -2005532292) {
						DebugPrintf(
							"Error %s creating texture surface.\n",
							std3D_LookupErrorString(
								result,
								g_std3DErrorStringTable,
								121),
							0, 0, 0);
						destinationSurface = NULL;
						break;
					}
				}
			}
			if (destinationSurface == NULL) {
				break;
			}
		}

		result = destinationSurface->lpVtbl->QueryInterface(
			destinationSurface, &CLSID_IDirect3DTexture,
			(void **)&destinationTexture);
		if (result) {
			DebugPrintf(
				"Error %s creating Direct3D dest texture.\n",
				std3D_LookupErrorString(
					result, g_std3DErrorStringTable, 121),
				0, 0, 0);
			destinationTexture = NULL;
			break;
		}
		result = destinationTexture->lpVtbl->Load(destinationTexture,
							  sourceTexture);
		if (result) {
			DebugPrintf(
				"Error %s loading Direct3D dest texture from source.\n",
				std3D_LookupErrorString(
					result, g_std3DErrorStringTable, 121),
				0, 0, 0);
			break;
		}
		result = destinationTexture->lpVtbl->GetHandle(
			destinationTexture, g_d3dDevice, &textureHandle);
		if (result) {
			DebugPrintf(
				"Error %s when getting texture handle.\n",
				std3D_LookupErrorString(
					result, g_std3DErrorStringTable, 121),
				0, 0, 0);
			textureHandle = 0;
		}

		sourceTexture->lpVtbl->Release(sourceTexture);
		sourceTexture = NULL;
		sourceSurface->lpVtbl->Release(sourceSurface);
		sourceSurface = NULL;
		if (temporaryBuffer != NULL) {
			std3D_FreeVBuffer(temporaryBuffer);
		}
		node->pCachedTexture = destinationTexture;
		node->pCachedSurface = destinationSurface;
		node->texHandle = textureHandle;
		node->width = width;
		node->height = height;
		node->bCached = 1;
		node->texelCount = texelCount;
		node->cacheBatchTag = g_std3DTextureBatchTag;
		std3D_CacheListAppend(node);
		return 1;
	} while (0);

	if (sourceSurface != NULL) {
		sourceSurface->lpVtbl->Release(sourceSurface);
	}
	if (sourceTexture != NULL) {
		sourceTexture->lpVtbl->Release(sourceTexture);
	}
	if (temporaryBuffer != NULL) {
		std3D_FreeVBuffer(temporaryBuffer);
	}
	if (destinationSurface != NULL) {
		destinationSurface->lpVtbl->Release(destinationSurface);
	}
	if (destinationTexture != NULL) {
		destinationTexture->lpVtbl->Release(destinationTexture);
	}
	node->pCachedTexture = NULL;
	node->pCachedSurface = NULL;
	node->texHandle = 0;
	node->bCached = 0;
	node->cacheBatchTag = 0;
	DebugPrintf("Done error exit from std3D_AddToTextureCache.\n", 0, 0, 0,
		    0);
	return 0;
}

/* Releases every cached texture and unlinks the list; sets g_texCacheCount to 0
 * and g_std3DTextureBatchTag to 1, and sets the device's availableMemory back
 * to its totalMemory. */
// FUNCTION: XVT 0x4B3070
void std3D_FlushTextureCache(void)
{
	Std3DTexCacheNode *node;

	node = g_pTexCacheHead;
	while (node != NULL) {
		Std3DTexCacheNode **nextLink;
		Std3DTexCacheNode *current;

		if (node->pCachedSurface != NULL) {
			node->pCachedSurface->lpVtbl->Release(
				node->pCachedSurface);
			node->pCachedSurface = NULL;
		}
		if (node->pCachedTexture != NULL) {
			node->pCachedTexture->lpVtbl->Release(
				node->pCachedTexture);
			node->pCachedTexture = NULL;
		}
		current = node;
		nextLink = &node->pNext;
		node->bCached = 0;
		node->cacheBatchTag = 0;
		node = *nextLink;
		*nextLink = NULL;
		current->pPrev = NULL;
	}

	g_pTexCacheHead = NULL;
	g_pTexCacheTail = NULL;
	g_texCacheCount = 0;
	g_pStd3DCurDevice->availableMemory = g_pStd3DCurDevice->totalMemory;
	g_std3DTextureBatchTag = 1;
}

/* Puts node at the most recently used end of the cache list, raises
 * g_texCacheCount, and takes the node's texelCount from availableMemory. */
// FUNCTION: XVT 0x4B30F0
void std3D_CacheListAppend(Std3DTexCacheNode *node)
{
	Std3DTexCacheNode *previousTail;

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
	g_pStd3DCurDevice->availableMemory -= node->texelCount;
}

/* Takes node off the cache list, lowers g_texCacheCount, and gives its
 * texelCount back to availableMemory. Does not check that node is on the
 * list. */
// FUNCTION: XVT 0x4B3160
void std3D_CacheListRemove(Std3DTexCacheNode *node)
{
	if (node == g_pTexCacheHead) {
		g_pTexCacheHead = node->pNext;
		if (g_pTexCacheHead != NULL) {
			g_pTexCacheHead->pPrev = NULL;
			if (g_pTexCacheHead->pNext == NULL) {
				g_pTexCacheTail = g_pTexCacheHead;
			}
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
	g_pStd3DCurDevice->availableMemory += node->texelCount;
}

/* Asks DirectDraw for the total and free texture video memory in bytes. Returns
 * 1, or 0 when it cannot; when only the memory query fails, the IDirectDraw2
 * interface it got is not released. */
// FUNCTION: XVT 0x4B3210
int std3D_QueryTextureVidMem(unsigned int *totalBytes, unsigned int *freeBytes)
{
	IDirectDraw *directDraw2 = 0;
	DDSCAPS caps;

	if (g_std3DDirectDraw->lpVtbl->QueryInterface(
		    g_std3DDirectDraw, &CLSID_IDirectDraw2,
		    (void **)&directDraw2) != DX_DD_OK) {
		return 0;
	}

	caps.dwCaps = DDSCAPS_TEXTURE;
	if (directDraw2->lpVtbl->GetAvailableVidMem(
		    directDraw2, &caps, totalBytes, freeBytes) != DX_DD_OK) {
		return 0;
	}

	directDraw2->lpVtbl->Release(directDraw2);
	return 1;
}

/* Marks node as used in the current batch and moves it to the most recently
 * used end of the cache list. */
// FUNCTION: XVT 0x4B3280
void std3D_CacheTextureSurface(Std3DTexCacheNode *node)
{
	node->cacheBatchTag = g_std3DTextureBatchTag;
	std3D_CacheListRemove(node);
	std3D_CacheListAppend(node);
}

/* Fills g_std3DQuadRect of the z-buffer with 0 when g_std3DZCompareCap is 16,
 * else 0xFFFF, restoring a lost surface and trying again. Returns 1, or 0 when
 * the fill or the restore fails. */
// FUNCTION: XVT 0x4B32B0
int std3D_ClearZBuffer(void)
{
	int rect[4];
	DDBLTFX effects;
	int result;

	memset(&effects, 0, sizeof(effects));
	effects.dwSize = sizeof(effects);
	effects.dwFillDepth = 0;
	if (g_std3DZCompareCap != 16) {
		effects.dwFillDepth = 0xFFFF;
	}
	rect[0] = g_std3DQuadRect.x;
	rect[1] = g_std3DQuadRect.y;
	rect[2] = g_std3DQuadRect.x + g_std3DQuadRect.width;
	rect[3] = g_std3DQuadRect.y + g_std3DQuadRect.height;

	for (;;) {
		result = g_std3DZBufferSurfaceBlock.surface->lpVtbl->Blt(
			g_std3DZBufferSurfaceBlock.surface, rect, NULL, NULL,
			DDBLT_WAIT | DDBLT_DEPTHFILL, &effects);
		if (result == DX_DD_OK) {
			return 1;
		}
		if (result == DX_DDERR_SURFACELOST) {
			result = g_std3DZBufferSurfaceBlock.surface->lpVtbl
					 ->Restore(g_std3DZBufferSurfaceBlock
							   .surface);
		}
		if (result != DX_DD_OK) {
			DebugPrintf(
				"Error %s clearing zbuffer.\n",
				std3D_LookupErrorString(
					result, g_std3DErrorStringTable, 121),
				0, 0, 0);
			return 0;
		}
	}
}

/* Returns the index of the first device that matches requiredCaps on
 * perspective texturing and z-buffer (each only when required), shares a color
 * model with it and has the same bHardware. Without one, returns the first
 * device that matches the most of those, in that order. Returns 0 when there is
 * no device. */
// FUNCTION: XVT 0x4B3380
int std3D_SelectBestDevice(Std3DDeviceCaps *requiredCaps)
{
	int bestMatchQuality;
	Std3DDevice *device;
	int deviceIndex;
	int requiredPerspective;
	int matchQuality;
	int requiredZBuffer;
	int bestDeviceIndex;

	if (g_std3DNumDevices == 0) {
		return 0;
	}
	bestMatchQuality = 0;
	device = g_std3DDevices;
	deviceIndex = 0;
	bestDeviceIndex = 0;
	if (g_std3DNumDevices > (unsigned int)deviceIndex) {
		requiredPerspective = requiredCaps->bTexturePerspective;
		do {
			matchQuality = 0;
			if (requiredPerspective == 0 ||
			    device->caps.bTexturePerspective ==
				    requiredPerspective) {
				matchQuality = 1;
				requiredZBuffer = requiredCaps->bHasZBuffer;
				if (requiredZBuffer == 0 ||
				    device->caps.bHasZBuffer ==
					    requiredZBuffer) {
					matchQuality = 2;
					if ((requiredCaps->colorModelFlags &
					     device->caps.colorModelFlags) !=
					    0) {
						matchQuality = 3;
						if (device->caps.bHardware ==
						    requiredCaps->bHardware) {
							DebugPrintf(
								"Found a perfect device match #%d!\n",
								deviceIndex, 0,
								0, 0);
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
	DebugPrintf("Settling for a closest match #%d..\n", bestDeviceIndex, 0,
		    0, 0);
	return bestDeviceIndex;
}

/* Returns the index of the first format that matches match exactly: the same
 * mode and bpp and, for RGB, the same red, green and blue bits, for RGBA the
 * alpha bits too, and for a palette mode nothing more. Without one, the first
 * with the best score: 1 for the mode, 2 for the mode and bpp, 3 for an RGBA
 * mode and bpp. Returns 0 when count is 0. */
// FUNCTION: XVT 0x4B3450
int std3D_FindClosestFormat(const ColorInfo *match, Std3DTexFmt *formats,
			    unsigned int count)
{
	int bestFormatIndex;
	Std3DTexFmt *format;
	int formatIndex;
	unsigned int matchScore;
	int bestMatchScore;

	if (count == 0) {
		return 0;
	}
	bestMatchScore = 0;
	bestFormatIndex = 0;
	format = formats;
	for (formatIndex = 0; (unsigned int)formatIndex < count;
	     ++formatIndex) {
		matchScore = 0;
		if (format->colorInfo.colorMode == match->colorMode) {
			++matchScore;
			if (format->colorInfo.bpp == match->bpp) {
				++matchScore;
				switch (match->colorMode) {
				case STDCOLOR_RGB:
					if (format->colorInfo.redBPP ==
						    match->redBPP &&
					    format->colorInfo.greenBPP ==
						    match->greenBPP &&
					    format->colorInfo.blueBPP ==
						    match->blueBPP) {
						DebugPrintf(
							"Found a perfect mode match #%d!\n",
							formatIndex, 0, 0, 0);
						return formatIndex;
					}
					break;
				case STDCOLOR_RGBA:
					if (format->colorInfo.colorMode ==
					    STDCOLOR_RGBA) {
						++matchScore;
					}
					if (format->colorInfo.redBPP ==
						    match->redBPP &&
					    format->colorInfo.greenBPP ==
						    match->greenBPP &&
					    format->colorInfo.blueBPP ==
						    match->blueBPP &&
					    format->colorInfo.alphaBPP ==
						    match->alphaBPP) {
						DebugPrintf(
							"Found a perfect mode match #%d!\n",
							formatIndex, 0, 0, 0);
						return formatIndex;
					}
					break;
				default:
					DebugPrintf(
						"Found a perfect mode match #%d!\n",
						formatIndex, 0, 0, 0);
					return formatIndex;
				}
			}
		}
		if ((int)matchScore > bestMatchScore) {
			bestFormatIndex = formatIndex;
			bestMatchScore = matchScore;
		}
		++format;
	}
	DebugPrintf("Settling for a closest match #%d..\n", bestFormatIndex, 0,
		    0, 0);
	return bestFormatIndex;
}

/* Draws the full-viewport color overlay when it is on and not all 0. Each
 * channel is its share of the largest, times 255; alpha is the largest times
 * 0.736 with stippled alpha, else times 0.9, held to 0 to 255, and an alpha of
 * 0 draws nothing. With alpha blending it draws the quad in that color;
 * without, it fills g_pStd3DVBuffer in the 4444 format, uploads it as a
 * translucent texture, draws the quad with it in white, and releases it.
 * Nothing calls this. */
// FUNCTION: XVT 0x4B3590
void std3D_DrawColorOverlay(void)
{
	float maximum;
	uint8_t blue;
	uint8_t green;
	uint8_t red;
	uint8_t alpha;
	uint32_t packedColor;

	if (g_std3DColorOverlayEnabled == 0 ||
	    (g_std3DColorOverlayRed == 0.0f &&
	     g_std3DColorOverlayGreen == 0.0f &&
	     g_std3DColorOverlayBlue == 0.0f)) {
		return;
	}
	maximum = g_std3DColorOverlayRed >= g_std3DColorOverlayGreen
			  ? g_std3DColorOverlayRed
			  : g_std3DColorOverlayGreen;
	maximum = g_std3DColorOverlayBlue >= maximum ? g_std3DColorOverlayBlue
						     : maximum;
	red = (uint8_t)(g_std3DColorOverlayRed / maximum * 255.0f);
	green = (uint8_t)(g_std3DColorOverlayGreen / maximum * 255.0f);
	blue = (uint8_t)(g_std3DColorOverlayBlue / maximum * 255.0f);
	if (g_pStd3DCurDevice->caps.bStippledShade != 0) {
		float alphaScale;
		float upperClampedAlpha;

		alphaScale = maximum * 0.736f;
		if (alphaScale >= 0.0f) {
			if (alphaScale > 255.0f) {
				upperClampedAlpha = 255.0f;
			} else {
				upperClampedAlpha = alphaScale;
			}
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
			if (alphaScale > 255.0f) {
				upperClampedAlpha = 255.0f;
			} else {
				upperClampedAlpha = alphaScale;
			}
			alphaScale = upperClampedAlpha;
		} else {
			alphaScale = 0.0f;
		}
		alpha = (uint8_t)alphaScale;
	}
	if (alpha == 0) {
		return;
	}
	if (g_pStd3DCurDevice->caps.bAlphaBlend != 0) {
		packedColor =
			(uint32_t)blue | ((uint32_t)red << 16) |
			(((uint32_t)green | ((uint32_t)alpha << 16)) << 8);
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

		color = (uint16_t)(green >>
				   g_pFmtRGBA4444->colorInfo.greenPosShiftRight)
			<< g_pFmtRGBA4444->colorInfo.greenPosShift;
		color |= (uint16_t)(red >>
				    g_pFmtRGBA4444->colorInfo.redPosShiftRight)
			 << g_pFmtRGBA4444->colorInfo.redPosShift;
		color |=
			(uint16_t)(alpha >>
				   g_pFmtRGBA4444->colorInfo.alphaPosShiftRight)
			<< g_pFmtRGBA4444->colorInfo.alphaPosShift;
		blueColor =
			(uint16_t)(blue >>
				   g_pFmtRGBA4444->colorInfo.bluePosShiftRight)
			<< g_pFmtRGBA4444->colorInfo.bluePosShift;
		std3D_FillVBuffer(g_pStd3DVBuffer,
				  (uint16_t)(color | blueColor), 0);
		std3D_StartScene();
		std3D_LockExecuteBuffer();
		std3D_AddToTextureCache(g_pStd3DVBuffer,
					&g_std3DColorOverlayTexNode, 0, 1);
		g_std3DQuadVerts[0].color = UINT32_MAX;
		g_std3DQuadVerts[1].color = UINT32_MAX;
		g_std3DQuadVerts[2].color = UINT32_MAX;
		g_std3DQuadVerts[3].color = UINT32_MAX;
		g_std3DViewportQuadTriangles[0].texture =
			&g_std3DColorOverlayTexNode;
		g_std3DViewportQuadTriangles[1].texture =
			&g_std3DColorOverlayTexNode;
		std3D_AddVertices(g_std3DQuadVerts, 4);
		std3D_BeginInstructions();
		std3D_AddTriangles(g_std3DViewportQuadTriangles, 2);
		std3D_ExecuteBuffer();
		std3D_EndScene();
		g_std3DColorOverlayTexNode.pCachedSurface->lpVtbl->Release(
			g_std3DColorOverlayTexNode.pCachedSurface);
		g_std3DColorOverlayTexNode.pCachedTexture->lpVtbl->Release(
			g_std3DColorOverlayTexNode.pCachedTexture);
		g_std3DColorOverlayTexNode.bCached = 0;
		std3D_CacheListRemove(&g_std3DColorOverlayTexNode);
	}
}

/* Stores rect in g_std3DQuadRect, sets g_std3DQuadVerts to its corners and
 * g_std3DViewportQuadTriangles to two untextured alpha-blended triangles over
 * it. Returns 0. */
// FUNCTION: XVT 0x4B3890
int std3D_BuildViewportQuad(const Std3DViewportRect *rect)
{
	g_std3DQuadRect = *rect;
	memset(g_std3DQuadVerts, 0, sizeof(g_std3DQuadVerts));

	g_std3DQuadVerts[0].sx = (float)g_std3DQuadRect.x;
	g_std3DQuadVerts[0].sy = (float)g_std3DQuadRect.y;
	g_std3DQuadVerts[1].sx =
		(float)(g_std3DQuadRect.x + g_std3DQuadRect.width);
	g_std3DQuadVerts[1].sy = (float)g_std3DQuadRect.y;
	g_std3DQuadVerts[2].sx =
		(float)(g_std3DQuadRect.x + g_std3DQuadRect.width);
	g_std3DQuadVerts[2].sy =
		(float)(g_std3DQuadRect.y + g_std3DQuadRect.height);
	g_std3DQuadVerts[3].sx = (float)g_std3DQuadRect.x;
	g_std3DQuadVerts[3].sy =
		(float)(g_std3DQuadRect.y + g_std3DQuadRect.height);

	g_std3DViewportQuadTriangles[0].vertexIndex1 = 1;
	g_std3DViewportQuadTriangles[0].vertexIndex0 = 0;
	g_std3DViewportQuadTriangles[0].vertexIndex2 = 2;
	g_std3DViewportQuadTriangles[0].texture = NULL;
	g_std3DViewportQuadTriangles[0].flags =
		STD3D_RS_ALPHA_BLEND | STD3D_RS_MONO_DISABLE;
	g_std3DViewportQuadTriangles[1].vertexIndex0 = 0;
	g_std3DViewportQuadTriangles[1].vertexIndex1 = 2;
	g_std3DViewportQuadTriangles[1].texture = NULL;
	g_std3DViewportQuadTriangles[1].vertexIndex2 = 3;
	g_std3DViewportQuadTriangles[1].flags =
		STD3D_RS_ALPHA_BLEND | STD3D_RS_MONO_DISABLE;

	return 0;
}

/* Writes the starting render states from g_std3DRenderOptionFlags into an
 * execute buffer of its own, 4096 bytes, executes it inside a scene, releases
 * it, and sets g_d3dStateFlags to the flags. The states: perspective, the
 * filters, subpixel, no wrapping, blending (as std3D_SetRenderState sets it for
 * the 0x600 bits), alpha test with compare 6, stippled alpha when the device
 * stipples, shade mode 2, mono, specular, fog, fill mode 3, dither, antialias,
 * z test and z writes when g_std3DZBufferEnabled is set, the z compare from
 * g_std3DZCompareCap, and cull mode 1. Returns 1; failures are only printed,
 * and it goes on when making or locking the buffer fails. */
// FUNCTION: XVT 0x4B3980
int std3D_SetInitialRenderState(void)
{
	IDirect3DExecuteBuffer *executeBuffer;
	D3DEXECUTEBUFFERDESC descriptor;
	D3DEXECUTEDATA executeData;
	D3DINSTRUCTION *instruction;
	D3DSTATE *state;
	uint8_t *base;
	uint8_t *cursor;
	int result;
	int zEnabled;

	executeBuffer = NULL;
	memset(&descriptor, 0, sizeof(descriptor));
	descriptor.dwSize = 20;
	descriptor.dwFlags = 1;
	descriptor.dwBufferSize = 4096;
	result = g_d3dDevice->lpVtbl->CreateExecuteBuffer(
		g_d3dDevice, &descriptor, &executeBuffer, NULL);
	if (result != 0) {
		DebugPrintf("Error %s creating D3D Execute buffer.\n",
			    std3D_LookupErrorString(
				    result, g_std3DErrorStringTable, 121),
			    0, 0, 0);
	}
	result = executeBuffer->lpVtbl->Lock(executeBuffer, &descriptor);
	if (result != 0) {
		DebugPrintf(g_std3DLockExecuteBufferErrorFormat,
			    std3D_LookupErrorString(
				    result, g_std3DErrorStringTable, 121),
			    0, 0, 0);
	}

	memset(descriptor.lpData, 0, 4096);
	base = (uint8_t *)descriptor.lpData;
	instruction = (D3DINSTRUCTION *)base;
	instruction->bOpcode = D3DOP_STATERENDER;
	instruction->bSize = sizeof(D3DSTATE);
	instruction->wCount = 25;
	state = (D3DSTATE *)(instruction + 1);

	state->dwState = D3DRENDERSTATE_TEXTUREPERSPECTIVE;
	state->dwArg = g_std3DRenderOptionFlags & 1;
	++state;
	state->dwState = D3DRENDERSTATE_TEXTUREMAG;
	state->dwArg = (g_std3DRenderOptionFlags & 0x80) != 0 ? 2 : 1;
	++state;
	state->dwState = D3DRENDERSTATE_TEXTUREMIN;
	state->dwArg = (g_std3DRenderOptionFlags & 0x100) != 0 ? 2 : 1;
	++state;
	state->dwState = D3DRENDERSTATE_SUBPIXEL;
	state->dwArg = (g_std3DRenderOptionFlags & 0x10) != 0;
	++state;
	state->dwState = D3DRENDERSTATE_SUBPIXELX;
	state->dwArg = (g_std3DRenderOptionFlags & 0x20) != 0;
	++state;
	state->dwState = D3DRENDERSTATE_WRAPU;
	state->dwArg = 0;
	++state;
	state->dwState = D3DRENDERSTATE_WRAPV;
	state->dwArg = 0;
	++state;
	state->dwState = D3DRENDERSTATE_BLENDENABLE;
	state->dwArg = (g_std3DRenderOptionFlags & 0x600) != 0;
	++state;
	if ((g_std3DRenderOptionFlags & 0x600) != 0) {
		if ((g_std3DRenderOptionFlags & 0x400) != 0) {
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
	state->dwArg = (g_std3DRenderOptionFlags & 0x8000) == 0;
	++state;
	state->dwState = D3DRENDERSTATE_SPECULARENABLE;
	state->dwArg = (g_std3DRenderOptionFlags & 4) != 0;
	++state;
	state->dwState = D3DRENDERSTATE_FOGENABLE;
	state->dwArg = (g_std3DRenderOptionFlags & 0x40) != 0;
	++state;
	state->dwState = D3DRENDERSTATE_FILLMODE;
	state->dwArg = 3;
	++state;
	state->dwState = D3DRENDERSTATE_DITHERENABLE;
	state->dwArg = (g_std3DRenderOptionFlags & 2) != 0;
	++state;
	state->dwState = D3DRENDERSTATE_ANTIALIAS;
	state->dwArg = (g_std3DRenderOptionFlags & 8) != 0;
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
	state->dwArg = std3D_MapZCmpFunc(g_std3DZCompareCap);
	++state;
	state->dwState = D3DRENDERSTATE_CULLMODE;
	state->dwArg = 1;
	++state;

	instruction = (D3DINSTRUCTION *)state;
	instruction->bOpcode = D3DOP_EXIT;
	instruction->bSize = 0;
	instruction->wCount = 0;
	cursor = (uint8_t *)(instruction + 1);
	result = executeBuffer->lpVtbl->Unlock(executeBuffer);
	if (result != 0) {
		DebugPrintf("Error %s unlocking D3D Execute buffer.\n",
			    std3D_LookupErrorString(
				    result, g_std3DErrorStringTable, 121),
			    0, 0, 0);
	}

	memset(&executeData, 0, sizeof(executeData));
	executeData.dwSize = sizeof(executeData);
	executeData.dwInstructionOffset = 0;
	executeData.dwInstructionLength = (uint32_t)(cursor - base);
	executeBuffer->lpVtbl->SetExecuteData(executeBuffer, &executeData);
	result = g_d3dDevice->lpVtbl->BeginScene(g_d3dDevice);
	if (result != 0) {
		DebugPrintf(g_std3DBeginSceneErrorFormat,
			    std3D_LookupErrorString(
				    result, g_std3DErrorStringTable, 121),
			    0, 0, 0);
	}
	result = g_d3dDevice->lpVtbl->Execute(g_d3dDevice, executeBuffer,
					      g_d3dViewport,
					      D3DEXECUTE_UNCLIPPED);
	if (result != 0) {
		DebugPrintf("Error %s executing buffer.\n",
			    std3D_LookupErrorString(
				    result, g_std3DErrorStringTable, 121),
			    0, 0, 0);
	}
	result = g_d3dDevice->lpVtbl->EndScene(g_d3dDevice);
	if (result != 0) {
		DebugPrintf(g_std3DEndSceneErrorFormat,
			    std3D_LookupErrorString(
				    result, g_std3DErrorStringTable, 121),
			    0, 0, 0);
	}
	executeBuffer->lpVtbl->Release(executeBuffer);
	g_d3dStateFlags = (Std3DRenderStateFlags)g_std3DRenderOptionFlags;
	DebugPrintf("Initial render state set.\n", 0, 0, 0, 0);
	return 1;
}

/* Makes the Direct3D viewport, adds it to the device and sets it to width by
 * height at 0, 0 with scales of half the width and height, then builds the
 * viewport quad. Returns 1, or 0 when a step fails. */
// FUNCTION: XVT 0x4B3E30
int std3D_CreateViewport(int width, int height)
{
	int result;
	Std3DViewportRect rect;
	D3DVIEWPORT viewport;

	result = g_lpD3D->lpVtbl->CreateViewport(g_lpD3D, &g_d3dViewport, NULL);
	if (result != 0) {
		DebugPrintf("Error %s when creating D3D viewport.\n",
			    std3D_LookupErrorString(
				    result, g_std3DErrorStringTable, 121),
			    0, 0, 0);
		return 0;
	}
	result = g_d3dDevice->lpVtbl->AddViewport(g_d3dDevice, g_d3dViewport);
	if (result != 0) {
		DebugPrintf(
			"Error %s when adding the D3D viewport to the device.\n",
			std3D_LookupErrorString(result, g_std3DErrorStringTable,
						121),
			0, 0, 0);
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
	viewport.dvMaxX =
		(float)(unsigned int)width / (viewport.dvScaleX * 2.0f);
	viewport.dvMaxY =
		(float)(unsigned int)height / (viewport.dvScaleY * 2.0f);
	result = g_d3dViewport->lpVtbl->SetViewport(g_d3dViewport, &viewport);
	if (result != 0) {
		DebugPrintf("Error %s when creating D3D viewport.\n",
			    std3D_LookupErrorString(
				    result, g_std3DErrorStringTable, 121),
			    0, 0, 0);
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

/* Makes a z-buffer surface of width by height, in video memory on a hardware
 * device and system memory otherwise, at 32, 16 or 8 bits, the first the
 * device's depth flags allow (0x100, 0x400, 0x800); attaches it to
 * g_std3DRenderSurface and reads back its description. Also fills
 * g_std3DZBufferTarget and points g_pStd3DZBufferState at
 * g_std3DZBufferSurfaceBlock. Returns 1, or 0 when no depth fits or a step
 * fails. */
// FUNCTION: XVT 0x4B3FC0
int std3D_CreateZBuffer(int width, int height)
{
	unsigned int zBufferBitDepth;
	HRESULT result;

	g_std3DZBufferTarget.storageType = 1;
	g_std3DZBufferTarget.bVideoMemory = 0;
	memcpy(&g_std3DZBufferTarget.raster, g_pStd3DRenderTarget,
	       sizeof(g_std3DZBufferTarget.raster));
	g_std3DZBufferTarget.unk58 = 0;
	g_pStd3DZBufferState = &g_std3DZBufferSurfaceBlock;
	g_std3DZBufferTarget.pixels = NULL;

	memset(&g_pStd3DZBufferState->desc, 0,
	       sizeof(g_pStd3DZBufferState->desc));
	g_pStd3DZBufferState->desc.dwSize = sizeof(g_pStd3DZBufferState->desc);
	g_pStd3DZBufferState->desc.dwFlags =
		DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | 0x40;
	g_pStd3DZBufferState->desc.ddsCaps.dwCaps = DDSCAPS_ZBUFFER;
	g_pStd3DZBufferState->desc.dwWidth = width;
	g_pStd3DZBufferState->desc.dwHeight = height;
	if (g_pStd3DCurDevice->caps.bHardware != 0) {
		g_pStd3DZBufferState->desc.ddsCaps.dwCaps |=
			DDSCAPS_VIDEOMEMORY;
	} else {
		g_pStd3DZBufferState->desc.ddsCaps.dwCaps |=
			DDSCAPS_SYSTEMMEMORY;
	}

	zBufferBitDepth = g_pStd3DCurDevice->d3dDesc.dwDeviceZBufferBitDepth;
	if ((zBufferBitDepth & 0x100) != 0) {
		g_pStd3DZBufferState->desc.dwZBufferBitDepth = 32;
	} else if ((zBufferBitDepth & 0x400) != 0) {
		g_pStd3DZBufferState->desc.dwZBufferBitDepth = 16;
	} else if ((zBufferBitDepth & 0x800) != 0) {
		g_pStd3DZBufferState->desc.dwZBufferBitDepth = 8;
	} else {
		DebugPrintf("Error: unsupported zbuffer bit depth!\n", 0, 0, 0,
			    0);
		return 0;
	}
	DebugPrintf("ZBuffer depth: %d.\n",
		    g_pStd3DZBufferState->desc.dwZBufferBitDepth, 0, 0, 0);

	result = g_std3DDirectDraw->lpVtbl->CreateSurface(
		g_std3DDirectDraw, &g_pStd3DZBufferState->desc,
		&g_pStd3DZBufferState->surface, NULL);
	if (result != 0) {
		DebugPrintf("Error %s when creating zBuffer DDraw surface.\n",
			    std3D_LookupErrorString(
				    result, g_std3DErrorStringTable, 121),
			    0, 0, 0);
		return 0;
	}

	result = g_std3DRenderSurface->lpVtbl->AddAttachedSurface(
		g_std3DRenderSurface, g_pStd3DZBufferState->surface);
	if (result != 0) {
		DebugPrintf("Error %s when attaching zbuffer to backbuffer.\n",
			    std3D_LookupErrorString(
				    result, g_std3DErrorStringTable, 121),
			    0, 0, 0);
		return 0;
	}

	result = g_pStd3DZBufferState->surface->lpVtbl->GetSurfaceDesc(
		g_pStd3DZBufferState->surface, &g_pStd3DZBufferState->desc);
	if (result != 0) {
		DebugPrintf(
			"Error %s when getting zbuffer surface description.\n",
			std3D_LookupErrorString(result, g_std3DErrorStringTable,
						121),
			0, 0, 0);
		return 0;
	}

	if ((g_pStd3DZBufferState->desc.ddsCaps.dwCaps & DDSCAPS_VIDEOMEMORY) !=
	    0) {
		g_std3DZBufferTarget.bVideoMemory = 1;
	}
	DebugPrintf("ZBuffer in %s memory.\n",
		    g_std3DZBufferTarget.bVideoMemory != 0 ? "VIDEO" : "SYSTEM",
		    0, 0, 0);
	DebugPrintf("ZBuffer created successfully.\n", 0, 0, 0, 0);
	return 1;
}

/* Records one enumerated device in g_std3DDevices while fewer than 4 are
 * recorded: its GUID, name and description, its hardware description when that
 * has a color model, else the software one, and the caps read from it (texture
 * sizes 1 to 256). Returns 1 to go on, or 0 once 4 are recorded. */
// FUNCTION: XVT 0x4B4210
HRESULT AERON_DXAPI std3D_EnumDevicesCallback(
	DxGuid *guid, char *deviceDescription, char *deviceName,
	D3DDEVICEDESC *hardwareDesc, D3DDEVICEDESC *softwareDesc, void *context)
{
	Std3DDevice *device;
	unsigned int shadeCaps;
	(void)context;

	if (g_std3DNumDevices < 4) {

		device = &g_std3DDevices[g_std3DNumDevices];
		memcpy(&device->guid, guid, sizeof(device->guid));
		strncpy(device->deviceDescription, deviceDescription,
			sizeof(device->deviceDescription));
		strncpy(device->deviceName, deviceName,
			sizeof(device->deviceName));
		if (hardwareDesc->dcmColorModel != 0) {
			device->caps.bHardware = 1;
			memcpy(&device->d3dDesc, hardwareDesc,
			       sizeof(device->d3dDesc));
		} else {
			device->caps.bHardware = 0;
			memcpy(&device->d3dDesc, softwareDesc,
			       sizeof(device->d3dDesc));
		}

		device->caps.colorModelFlags = 0;
		if ((device->d3dDesc.dcmColorModel & 2) != 0) {
			device->caps.colorModelFlags = 2;
		}
		if ((device->d3dDesc.dcmColorModel & 1) != 0) {
			device->caps.colorModelFlags |= 1;
		}
		device->caps.bTexturePerspective =
			device->d3dDesc.dpcTriCaps.dwTextureCaps & 1;
		device->caps.bHasZBuffer =
			device->d3dDesc.dwDeviceZBufferBitDepth != 0;
		device->caps.bSquareOnlyTexture =
			(device->d3dDesc.dpcTriCaps.dwTextureCaps & 0x20) != 0;
		device->caps.bAlphaTexture =
			(device->d3dDesc.dpcTriCaps.dwTextureCaps & 4) != 0;
		shadeCaps = device->d3dDesc.dpcTriCaps.dwShadeCaps;
		device->caps.bStippledShade =
			(shadeCaps & 0x1000) == 0 && (shadeCaps & 0x2000) != 0;
		device->caps.bAlphaBlend =
			((device->d3dDesc.dpcTriCaps.dwTextureBlendCaps & 8) !=
				 0 &&
			 (device->d3dDesc.dpcTriCaps.dwShadeCaps & 0x4000) !=
				 0) ||
			device->caps.bStippledShade != 0;
		device->caps.bColorKeyTexture =
			(device->d3dDesc.dpcTriCaps.dwTextureCaps & 8) != 0;
		device->caps.renderBitDepthMask = std3D_PackRenderBitDepths(
			device->d3dDesc.dwDeviceRenderBitDepth);
		device->caps.zCmpCapsMask = std3D_MaskZCmpCaps(
			device->d3dDesc.dpcTriCaps.dwZCmpCaps);
		device->caps.minTextureWidth = 1;
		device->caps.minTextureHeight = 1;
		device->caps.maxTextureWidth = 256;
		device->caps.maxTextureHeight = 256;
		device->caps.maxBufferSize = device->d3dDesc.dwMaxBufferSize;
		device->caps.maxVertexCount = device->d3dDesc.dwMaxVertexCount;

		DebugPrintf("Found |%s|%s|%s|%s| D3D Device\n",
			    device->caps.bHardware != 0 ? "HW" : "SW",
			    device->caps.colorModelFlags & 1 ? "MONO" : "",
			    device->caps.colorModelFlags & 2 ? "RGB" : "",
			    device->caps.bHasZBuffer != 0 ? "Z" : "Non-Z");
		DebugPrintf(
			"      |%s|%s|%s| %dbpp\n",
			device->caps.bAlphaTexture != 0 ? "Alpha" : "No Alpha",
			device->caps.bStippledShade != 0 ? "Stippled" : "Blend",
			device->caps.bColorKeyTexture != 0 ? "Colorkey"
							   : "No Colorkey",
			device->caps.renderBitDepthMask);
		DebugPrintf("Description: %s [%s]\n", device->deviceName,
			    device->deviceDescription, 0, 0);
		++g_std3DNumDevices;
		return 1;
	}
	return 0;
}

/* Records one enumerated texture format in g_std3DTextureFormats while fewer
 * than 8 are recorded: an 8-bit palette format, or RGB or RGBA with each
 * channel's position, bits and shift from 8 bits read from its mask. Skips a
 * 4-bit palette format (flag 0x8). Returns 1 to go on, or 0 once 8 are
 * recorded. Does not check for an empty mask, on which its bit search never
 * ends. */
// FUNCTION: XVT 0x4B4490
int AERON_DXAPI std3D_EnumTextureFormats(DDSURFACEDESC *surfaceDesc,
					 void *context)
{
	Std3DTexFmt *format;
	/* Each of these four first counts its mask's trailing zeros (the channel's position), then is reset
	 * to count the mask's set bits (the channel's width, stored as its BPP). */
	int redShift;
	int greenShift;
	int blueShift;
	int alphaShift;
	unsigned int mask;

	(void)context;
	if ((unsigned int)g_std3DNumTextureFormats < 8) {
		format = &g_std3DTextureFormats[g_std3DNumTextureFormats];
		memcpy(&format->ddsd, surfaceDesc, sizeof(format->ddsd));
		if ((surfaceDesc->ddpfPixelFormat.dwFlags &
		     DDPF_PALETTEINDEXED8) != 0) {
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
			DebugPrintf("Found %dbpp palettized tex format.\n",
				    format->colorInfo.bpp, 0, 0, 0);
		} else if ((surfaceDesc->ddpfPixelFormat.dwFlags & 8) != 0) {
			return 1;
		} else if ((surfaceDesc->ddpfPixelFormat.dwFlags &
			    DDPF_ALPHAPIXELS) != 0) {
			format->colorInfo.colorMode = STDCOLOR_RGBA;
			format->colorInfo.bpp =
				surfaceDesc->ddpfPixelFormat.dwRGBBitCount;

			redShift = 0;
			mask = surfaceDesc->ddpfPixelFormat.dwRBitMask;
			while ((mask & 1) == 0) {
				++redShift;
				mask >>= 1;
			}
			format->colorInfo.redPosShift = redShift;
			format->colorInfo.redPosShiftRight = std3D_Log2Floor(
				0xFFu /
				(surfaceDesc->ddpfPixelFormat.dwRBitMask >>
				 redShift));
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
			format->colorInfo.greenPosShiftRight = std3D_Log2Floor(
				0xFFu /
				(surfaceDesc->ddpfPixelFormat.dwGBitMask >>
				 greenShift));
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
			format->colorInfo.bluePosShiftRight = std3D_Log2Floor(
				0xFFu /
				(surfaceDesc->ddpfPixelFormat.dwBBitMask >>
				 blueShift));
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
			format->colorInfo.alphaPosShiftRight = std3D_Log2Floor(
				0xFFu / (surfaceDesc->ddpfPixelFormat
						 .dwRGBAlphaBitMask >>
					 alphaShift));
			alphaShift = 0;
			while ((mask & 1) != 0) {
				++alphaShift;
				mask >>= 1;
			}
			format->colorInfo.alphaBPP = alphaShift;
			DebugPrintf("Found RGBA tex format (%d:%d:%d:%d).\n",
				    format->colorInfo.redBPP,
				    format->colorInfo.greenBPP,
				    format->colorInfo.blueBPP,
				    format->colorInfo.alphaBPP);
			++g_std3DNumTextureFormats;
			return 1;
		} else {
			format->colorInfo.colorMode = STDCOLOR_RGB;
			format->colorInfo.bpp =
				surfaceDesc->ddpfPixelFormat.dwRGBBitCount;

			redShift = 0;
			mask = surfaceDesc->ddpfPixelFormat.dwRBitMask;
			while ((mask & 1) == 0) {
				++redShift;
				mask >>= 1;
			}
			format->colorInfo.redPosShift = redShift;
			format->colorInfo.redPosShiftRight = std3D_Log2Floor(
				0xFFu /
				(surfaceDesc->ddpfPixelFormat.dwRBitMask >>
				 redShift));
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
			format->colorInfo.greenPosShiftRight = std3D_Log2Floor(
				0xFFu /
				(surfaceDesc->ddpfPixelFormat.dwGBitMask >>
				 greenShift));
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
			format->colorInfo.bluePosShiftRight = std3D_Log2Floor(
				0xFFu /
				(surfaceDesc->ddpfPixelFormat.dwBBitMask >>
				 blueShift));
			blueShift = 0;
			while ((mask & 1) != 0) {
				++blueShift;
				mask >>= 1;
			}
			format->colorInfo.blueBPP = blueShift;
			format->colorInfo.alphaPosShift = 0;
			format->colorInfo.alphaPosShiftRight = 0;
			format->colorInfo.alphaBPP = 0;
			DebugPrintf("Found RGB tex format (%d:%d:%d).\n",
				    format->colorInfo.redBPP,
				    format->colorInfo.greenBPP,
				    format->colorInfo.blueBPP, 0);
		}

		++g_std3DNumTextureFormats;
		return 1;
	}
	return 0;
}

/* Maps DirectDraw bit-depth flags to bits: 0x4000 (1 bit) to 0x01, 0x2000 to
 * 0x02, 0x1000 to 0x04, 0x800 to 0x08, 0x400 to 0x10, 0x200 to 0x20 and 0x100
 * (32 bits) to 0x40. */
// FUNCTION: XVT 0x4B47E0
int std3D_PackRenderBitDepths(int ddbdFlags)
{
	int result;

	result = 0;
	if ((ddbdFlags & 0x4000) != 0) {
		result |= 0x01;
	}
	if ((ddbdFlags & 0x2000) != 0) {
		result |= 0x02;
	}
	if ((ddbdFlags & 0x1000) != 0) {
		result |= 0x04;
	}
	if ((ddbdFlags & 0x0800) != 0) {
		result |= 0x08;
	}
	if ((ddbdFlags & 0x0400) != 0) {
		result |= 0x10;
	}
	if ((ddbdFlags & 0x0200) != 0) {
		result |= 0x20;
	}
	if ((ddbdFlags & 0x0100) != 0) {
		result |= 0x40;
	}
	return result;
}

/* Returns d3dpcmpcaps & 0xFF: each of the eight compare caps bits is copied
 * to the same place. */
// FUNCTION: XVT 0x4B4880
int std3D_MaskZCmpCaps(unsigned int d3dpcmpcaps)
{
	int result = 0;

	if (d3dpcmpcaps & 1) {
		result = 1;
	}
	if (d3dpcmpcaps & 4) {
		result |= 4;
	}
	if (d3dpcmpcaps & 2) {
		result |= 2;
	}
	if (d3dpcmpcaps & 8) {
		result |= 8;
	}
	if (d3dpcmpcaps & 0x10) {
		result |= 0x10;
	}
	if (d3dpcmpcaps & 0x20) {
		result |= 0x20;
	}
	if (d3dpcmpcaps & 0x40) {
		result |= 0x40;
	}
	if (d3dpcmpcaps & 0x80) {
		result |= 0x80;
	}
	return result;
}

/* Returns the compare function for one compare caps bit: 0x1 gives 1, 0x2 gives
 * 2, 0x4 gives 3, 0x8 gives 4, 0x10 gives 5, 0x20 gives 6, 0x40 gives 7 and
 * 0x80 gives 8. With several bits set it ORs their values together; with none
 * it returns 0. */
// FUNCTION: XVT 0x4B48D0
unsigned int std3D_MapZCmpFunc(unsigned int capsMask)
{
	unsigned int result = 0;

	if (capsMask & 1) {
		result = 1;
	}
	if (capsMask & 4) {
		result |= 3;
	}
	if (capsMask & 2) {
		result |= 2;
	}
	if (capsMask & 8) {
		result |= 4;
	}
	if (capsMask & 0x10) {
		result |= 5;
	}
	if (capsMask & 0x20) {
		result |= 6;
	}
	if (capsMask & 0x40) {
		result |= 7;
	}
	if (capsMask & 0x80) {
		result |= 8;
	}
	return result;
}
