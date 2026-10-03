#include "xvt/render/renderer.h"
#include "xvt/flight/flight_display.h"
#include "xvt/math/math.h"
#include "xvt/render/render_scene.h"
#include "xvt/render/render_texture.h"
#include "xvt/render/std3d.h"
#include "xvt/util/debug_console.h"
#include <string.h>

/* Level-of-detail child RenderScene_DrawModelNode takes in every face group, 0
 * to choose by distance (child 1 at a view depth of 0 or less). Only
 * Flight_Main (original build) and XvtFlightEntry_ConfigureLodDistance (modern)
 * write it, setting 0, so no level is ever forced. */
// GLOBAL: XVT 0x5233A4
int g_forcedLodLevel = 0;
/* Factor on view depth when RenderScene_DrawModelNode picks a level of detail,
 * from the level-of-detail option (Flight_Main in the original build,
 * XvtFlightEntry_ConfigureLodDistance in the modern one); 1.0 at start and in
 * the model preview (ModelPreview_ResetViewAndRenderState). */
// GLOBAL: XVT 0x5233A8
float g_lodDistanceScale = 1.0f;
/* 1 when model textures get mipmap levels, built by OptModel_BuildRuntimeNode
 * and chosen by the drawing code, 0 not. Set from the "nomipmaps" and "mipmaps"
 * launch options and the mipmap option (Flight_Main in the original build,
 * XvtFlightEntry_ReadLaunchSwitches and XvtFlightEntry_ConfigureMipmaps in the
 * modern one); ModelPreview_LoadModel sets 1. */
// GLOBAL: XVT 0x5233AC
int g_mipmappingEnabled = 1;
/* Factor on a face's texels per pixel when the mipmap level is chosen, from the
 * mipmap option; set with g_mipmappingEnabled, 1.0 at start and in the model
 * preview. */
// GLOBAL: XVT 0x5233B0
float g_mipLodScale = 1.0f;
/* The dithering option, 1 on, 0 off, set at flight start (Flight_Main,
 * XvtFlightEntry_Configure) and 1 in the model preview; nothing reads it. */
// GLOBAL: XVT 0x5233B4
int g_ditheringEnabled = 1;
/* The local lights option, 1 on, 0 off, set at flight start (Flight_Main,
 * XvtFlightEntry_Configure); the object lighting setup lights from nearby
 * sources only while it is set. ModelPreview_RenderViewport sets 0 while it
 * draws and puts it back. */
// GLOBAL: XVT 0x5233B8
int g_localLightsEnabled = 1;
/* The specular lighting option, 1 on, 0 off, set at flight start (Flight_Main,
 * XvtFlightEntry_Configure) and 1 in the model preview; read by the face and
 * vertex lighting. */
// GLOBAL: XVT 0x5233BC
int g_specularEnabled = 1;
/* The diffuse (directional) lighting option, 1 on, 0 off, set at flight start
 * (Flight_Main, XvtFlightEntry_Configure) and 1 in the model preview; read by
 * RenderScene_ComputeVertexLighting. */
// GLOBAL: XVT 0x5233C0
int g_dirLightingEnabled = 1;
/* The texture resolution option, 0 to 2, set at flight start (Flight_Main,
 * XvtFlightEntry_Configure) and 1 in the model preview; read when model
 * textures are built. */
// GLOBAL: XVT 0x5233C4
int g_textureResolutionLevel = 1;
/* The DirectDraw object flight draws with, which FlightDisplay_Init takes from
 * FrontendDisplay_GetDirectDraw; Renderer_GetDirectDraw returns it. */
// GLOBAL: XVT 0x66DDD4
IDirectDraw *g_flightDirectDraw;
/* Palette index used as the transparent color: blits skip it, hardware textures
 * put its pixel in their transparent entry, and FlightSurface_ClearToBlack
 * fills with it. 0xFB at start; FeDiskIo_InitResources sets it, with
 * g_flightBackgroundColorIndex, to the palette color nearest (0, 0, 2) at
 * flight start. */
// GLOBAL: XVT 0x5233C8
uint8_t g_flightTransparentColorIndex = 0xfb;
/* Software drawing mode: 0 at 320x240 or an unknown resolution, 1 at 640x480
 * and 480x360, 2 in 16-bit color; only
 * FlightRender_ConfigureCallbacksForResolution writes it.
 * FlightPalette_SetRange keeps g_flightPalette16Bpp up to date in mode 2. */
// GLOBAL: XVT 0x9A7A30
uint8_t g_flightPixelMode = 0;
/* Graphics detail preset, 3 at start:
 * FlightRender_ConfigureCallbacksForResolution sets the starting one, and the
 * Alt+D key steps it, wrapping to 0 at GRAPHICS_DETAIL_PRESET_COUNT, and
 * applies it (Flight_UpdatePlayerStep in the original build,
 * XvtFlightSim_UpdatePlayerStep in the modern one). */
// GLOBAL: XVT 0x5233F8
uint8_t g_flightGraphicsDetailPreset = 3;
/* 0 at start; only FlightRender_ConfigureCallbacksForResolution writes it,
 * setting 1. Hud_RebuildDisplayForViewState passes it as the third argument of
 * SetFlightViewport, which ignores it. */
// GLOBAL: XVT 0xA08290
uint16_t g_flightViewportMode = 0;
/* 1 while flight draws through Direct3D hardware, 0 for the software renderer.
 * Set at flight start from the 3D hardware option (Flight_Main,
 * XvtFlightEntry_Configure); Renderer_InitD3DDevice sets 0 when the device
 * cannot be used, FlightDisplay_Init when the color depth is not 16-bit, and
 * Flight_Main (original build) or XvtFlightEntry_Cleanup (modern) when flight
 * ends. */
// GLOBAL: XVT 0x527E90
int g_useHardware3D;
/* The bilinear filtering option for hardware drawing, 1 on, 0 off, set at
 * flight start (Flight_Main, XvtFlightEntry_Configure). FlightView_Render and
 * FlightHyperspace_RenderTransitionEffect turn it off for part of a frame and
 * put it back. */
// GLOBAL: XVT 0x527E94
int g_bilinearEnabled = 1;
/* 1 while FeDiskIo_InitResources loads the flight's resources, else 0; only it
 * writes it. Display_IsPixelFormat555 reads it. */
// GLOBAL: XVT 0x527EC8
int g_loadingModel;
/* Width in pixels of the flight's drawing surface: 640 at start; at flight
 * start 320, 480 or 640 from the window size option (Flight_Main,
 * XvtFlightEntry_ConfigureDisplaySize). */
// GLOBAL: XVT 0x5233E0
int g_surfaceWidth = 640;
/* Height in pixels of the flight's drawing surface: 480 at start; at flight
 * start 240, 360 or 480 with g_surfaceWidth. */
// GLOBAL: XVT 0x5233E8
int g_surfaceHeight = 480;
/* Address of the first pixel of the surface flight draws into, set by
 * FlightSurface_Lock each time it locks one; 0xA0000 at start. */
// GLOBAL: XVT 0x527ECC
void *g_surfacePixels = (void *)0xA0000;
/* Width in pixels of the display mode: 320, 512 or 640 from the screen
 * resolution option at flight start (Flight_Main,
 * XvtFlightEntry_ConfigureDisplaySize); FlightDisplay_Init changes it when it
 * has to fall back to another mode. */
// GLOBAL: XVT 0x66DDC4
int g_displayModeWidth;
/* Height in pixels of the display mode: 240, 384 or 480, written with
 * g_displayModeWidth. */
// GLOBAL: XVT 0x66DDE0
int g_displayModeHeight;
/* Width in pixels flight's drawing code lays the screen out for: 320, 640 or
 * 480 from g_flightResolutionMode, 320 for any other mode; only
 * FlightDisplay_ConfigureResolutionState writes it. 0 at start. */
// GLOBAL: XVT 0x9A7BAC
unsigned int g_screenWidth = 0;
/* A screen-sized work buffer, locked from its memory handle by
 * FeDiskIo_InitGlobalBuffers and FeDiskIo_LockGlobalBuffers; the HUD and damage
 * displays draw into it and copy from it. */
// GLOBAL: XVT 0x9A806C
void *g_flightOffscreenBuffer = 0;
/* Top row of the flight viewport, in pixels: the viewport's byte offset divided
 * by g_surfacePitch. Written by SetFlightViewport, PushFlightViewport,
 * PopFlightViewport and ModelPreview_RenderViewport. */
// GLOBAL: XVT 0x9A7B60
int g_flightVpY = 0;
/* Height of the flight viewport in pixels, written with g_flightVpY. */
// GLOBAL: XVT 0x9A1FE4
uint16_t g_flightVpHeight = 0;
/* Half of g_flightVpHeight, rounded down, written with g_flightVpY; the
 * projection centers the view on it. */
// GLOBAL: XVT 0x9A20A2
uint16_t g_flightVpCenterY = 0;
/* Height in pixels flight's drawing code lays the screen out for: 240, 480 or
 * 360, written with g_screenWidth. */
// GLOBAL: XVT 0x9A8C14
unsigned int g_screenHeight = 0;
/* Left column of the flight viewport, in pixels: the remainder of its byte
 * offset by g_surfacePitch, divided by g_flightBytesPerPixel. Written with
 * g_flightVpY. */
// GLOBAL: XVT 0x9A8E3C
int g_flightVpX = 0;
/* Rows added to the projected screen Y, so the view's center can sit off the
 * viewport's middle: the cockpit's projection offset in
 * Hud_RebuildDisplayForViewState, else 0; Hud_Update3DCrt sets 0 while it draws
 * and puts it back, and the model preview sets 0. */
// GLOBAL: XVT 0x9D7680
int g_projOffsetY;
/* Projection scale: the drawing code turns a view offset into a screen offset
 * as offset * g_projScaleInt / depth. 256 at 320x240, 512 at 640x480 and
 * 480x360 (FlightDisplay_ConfigureResolutionState), 512 in the model
 * preview. */
// GLOBAL: XVT 0xA081EC
uint32_t g_projScaleInt = 0;
/* Width of the flight viewport in pixels, written with g_flightVpY. */
// GLOBAL: XVT 0x9D682C
uint16_t g_flightVpWidth = 0;
/* Component of the local player's target that is selected, set by
 * Flight_UpdateTimers from selectedTargetComponent; read by
 * Damage_QueueCraftBillboardsForObjectType. */
// GLOBAL: XVT 0x9D682E
uint16_t g_renderTargetComponentIdx = 0;
/* g_flightVpWidth - 1, written with g_flightVpY. */
// GLOBAL: XVT 0x9CD272
uint16_t g_flightVpMaxX = 0;
/* Object the targeting display draws for, with flags in its high bits:
 * Flight_UpdateTimers sets it to the local player's target object index OR
 * g_renderObjectRefFlags OR g_targetProximityBlinkBit. Hud_Update3DCrt adds its
 * box and cross flags while it draws, and
 * Damage_QueueCraftBillboardsForObjectType and ProvingGrounds_DrawCourseObject
 * set it for one draw; each puts it back. Mission_InitFlightRuntimeState sets
 * 0xFFFF. */
// GLOBAL: XVT 0x9CC450
uint16_t g_renderObjectRef = 0;
/* Flag bits Flight_UpdateTimers adds to g_renderObjectRef: 1024 or 0, set by
 * Flight_ProcessPlayerActions, and 0 from Mission_InitFlightRuntimeState and
 * Player_ValidateCurrentTargets. */
// GLOBAL: XVT 0xA0810A
uint16_t g_renderObjectRefFlags = 0;
/* g_flightVpHeight - 1, written with g_flightVpY. */
// GLOBAL: XVT 0x9D1260
uint16_t g_flightVpMaxY = 0;
/* Fills the clip rectangle with the text background color:
 * FlightRender_InstallCallbacks sets it to FlightSw_FillClipRect8bpp, or
 * FlightSw_FillClipRect16bpp in pixel mode 2. */
// GLOBAL: XVT 0x9D1300
void (*g_flightFillClipRectFn)(void) = 0;
/* Bytes from one row of the flight drawing surface to the next.
 * FlightDisplay_ConfigureResolutionState sets it from the primary surface,
 * FlightDisplay_Init from its surface description, and FlightSurface_Lock from
 * the surface it locks. */
// GLOBAL: XVT 0x9D8C08
int g_surfacePitch = 0;
/* Byte offset of the flight viewport's first pixel in the drawing surface,
 * written with g_flightVpY. */
// GLOBAL: XVT 0x9ED234
unsigned int g_flightVpBaseOffset = 0;
/* Draws a run-length sprite faded: FlightRender_InstallCallbacks sets it to
 * FlightSw_BlitSpriteRleFaded8bpp, or FlightSw_BlitSpriteRleFaded16bpp in pixel
 * mode 2. */
// GLOBAL: XVT 0x9D130C
FlightBlitSpriteFadedFn g_flightBlitSpriteFadedFn = 0;
/* Draws a run-length sprite: FlightRender_InstallCallbacks sets it to
 * FlightSw_BlitSpriteRle8bpp, or FlightSw_BlitSpriteRle16bpp in pixel mode
 * 2. */
// GLOBAL: XVT 0x9D80C4
FlightBlitSpriteFn g_flightBlitSpriteFn = 0;
/* Draws one character of HUD text: FlightRender_InstallCallbacks sets it to
 * FlightText_DrawWideGlyph8bpp, or FlightText_DrawWideGlyph in pixel mode 2. */
// GLOBAL: XVT 0x9A6FE4
FlightDrawCharFn g_flightDrawCharFn = 0;
/* FlightRender_InstallCallbacks sets it to FlightSw_InitFramebuffer in every
 * pixel mode; nothing calls through it. */
// GLOBAL: XVT 0x9D77C0
void (*g_flightInitLineBufferFn)(void) = 0;
/* FlightRender_InstallCallbacks sets it to FlightRender_TransitionHookStub in
 * every pixel mode. */
// GLOBAL: XVT 0x9A8D44
void (*g_flightRenderTransitionHook)(void) = 0;
/* Sends the palette to the display again: FlightRender_InstallCallbacks sets it
 * to FlightPalette_ApplyToDisplay in every pixel mode. */
// GLOBAL: XVT 0x9A8C20
void (*g_flightResetPaletteFn)(void) = 0;
/* Sets a range of palette colors: FlightRender_InstallCallbacks sets it to
 * FlightPalette_SetRange in every pixel mode. */
// GLOBAL: XVT 0x9D77F4
FlightSetPaletteRangeFn g_flightSetPaletteRangeFn = 0;
/* FlightRender_InstallCallbacks sets it to FlightPalette_GetFull in every pixel
 * mode; nothing calls through it. */
// GLOBAL: XVT 0xA0810C
FlightPaletteFn g_flightGetPaletteFn = 0;
/* FlightRender_InstallCallbacks sets it to FlightPalette_SetFull in every pixel
 * mode; nothing calls through it. */
// GLOBAL: XVT 0x9FE7E0
FlightPaletteFn g_flightSetPaletteFn = 0;
/* Byte offset of a pixel in the drawing surface: FlightRender_InstallCallbacks
 * sets it to FlightSw_ComputePixelOffset8bpp, or FlightSw_ComputePixelOffset in
 * pixel mode 2. */
// GLOBAL: XVT 0x9A8C30
FlightComputePixelOffsetFn g_flightComputePixelOffsetFn = 0;
/* Fills a clipped rectangle or its border: FlightRender_InstallCallbacks sets
 * it to FlightSw_FillRectClipped8bpp, or FlightSw_FillRectClipped16bpp in pixel
 * mode 2. */
// GLOBAL: XVT 0x9ECA20
FlightFillRectClippedFn g_flightFillRectClippedFn = 0;
/* Copies a screen rectangle out to a buffer: FlightRender_InstallCallbacks sets
 * it to FlightSw_SaveScreenRect8bpp, or FlightSw_SaveScreenRect16bpp in pixel
 * mode 2. */
// GLOBAL: XVT 0x9D8B6C
FlightScreenRectFn g_flightSaveScreenRectFn = 0;
/* Copies a saved rectangle back to the screen: FlightRender_InstallCallbacks
 * sets it to FlightSw_RestoreScreenRect8bpp, or FlightSw_RestoreScreenRect16bpp
 * in pixel mode 2. */
// GLOBAL: XVT 0xA07CE0
FlightScreenRectFn g_flightRestoreScreenRectFn = 0;
/* Draws a list of points, the radar blips: FlightRender_InstallCallbacks sets
 * it to FlightSw_DrawPointArray8bpp, or FlightSw_DrawPointArray16bpp in pixel
 * mode 2. */
// GLOBAL: XVT 0x9A20A8
FlightDrawPointArrayFn g_flightDrawPointArrayFn = 0;
/* Erases a list of points: FlightRender_InstallCallbacks sets it to
 * FlightSw_ErasePointArray8bpp, or FlightSw_ErasePointArray16bpp in pixel mode
 * 2. */
// GLOBAL: XVT 0x9D12E4
FlightDrawPointArrayFn g_flightDrawPointArrayMaskedFn = 0;
/* FlightRender_InstallCallbacks sets it to FlightSw_DrawPixel8bpp, or
 * FlightSw_DrawPixel16bpp in pixel mode 2; nothing calls through it. */
// GLOBAL: XVT 0x9E95F4
FlightDrawPixelFn g_flightDrawPixelFn = 0;
/* Draws the radar's target marker: FlightRender_InstallCallbacks sets it to
 * FlightSw_DrawRadarTargetMarker8bpp, or FlightSw_DrawRadarTargetMarker16bpp in
 * pixel mode 2. */
// GLOBAL: XVT 0x9EC46C
void (*g_flightDrawRadarTargetMarkerFn)(void) = 0;
/* Restores the screen under the radar's target marker:
 * FlightRender_InstallCallbacks sets it to
 * FlightSw_RestoreRadarTargetMarker8bpp, or
 * FlightSw_RestoreRadarTargetMarker16bpp in pixel mode 2. */
// GLOBAL: XVT 0x9A1FEC
void (*g_flightRestoreRadarTargetMarkerFn)(void) = 0;
/* Draws a line: FlightRender_InstallCallbacks sets it to FlightSw_DrawLine8bpp,
 * or FlightSw_DrawLine16bpp in pixel mode 2. */
// GLOBAL: XVT 0x9CC454
FlightDrawLineFn g_flightDrawLineFn = 0;
/* Row 0 (the side axis), Y term, of the current object matrix, Q15; see
 * g_curMatR0_X. */
// GLOBAL: XVT 0xA0048C
int g_curMatR0_Y = 0;
/* Object-to-view rotation, Q15: the current object matrix's row 0 dotted with
 * camera row 0. Only FVIEW_ComputeObjectViewMatrix writes the nine g_objViewMat
 * entries; the model drawing reads them. */
// GLOBAL: XVT 0x9D77AC
int g_objViewMat_R0_X = 0;
/* Object-to-view rotation: object row 0 dotted with camera row 1. */
// GLOBAL: XVT 0x9D77B8
int g_objViewMat_R0_Y = 0;
/* Object-to-view rotation: object row 0 dotted with camera row 2. */
// GLOBAL: XVT 0x9D77B4
int g_objViewMat_R0_Z = 0;
/* Object-to-view rotation: object row 2 dotted with camera row 0. */
// GLOBAL: XVT 0x9D77B0
int g_objViewMat_R1_X = 0;
/* Object-to-view rotation: object row 2 dotted with camera row 1. */
// GLOBAL: XVT 0x9D7690
int g_objViewMat_R1_Y = 0;
/* Object-to-view rotation: object row 2 dotted with camera row 2. */
// GLOBAL: XVT 0x9D768C
int g_objViewMat_R1_Z = 0;
/* Object-to-view rotation: object row 1 dotted with camera row 0. */
// GLOBAL: XVT 0x9D7688
int g_objViewMat_R2_X = 0;
/* Object-to-view rotation: object row 1 dotted with camera row 1. */
// GLOBAL: XVT 0x9D77A4
int g_objViewMat_R2_Y = 0;
/* Object-to-view rotation: object row 1 dotted with camera row 2. */
// GLOBAL: XVT 0x9D77A0
int g_objViewMat_R2_Z = 0;
/* Light direction for the object being drawn, X term, Q15: the world light
 * direction dotted with object row 0 while
 * g_transformLightDirectionToObjectSpace is set, else g_worldLightDirectionX.
 * Only FVIEW_ComputeObjectViewMatrix writes the three terms. */
// GLOBAL: XVT 0xA081E0
int g_objectLightDirectionX = 0;
/* Light direction, Y term: dotted with object row 2, else
 * g_worldLightDirectionY. */
// GLOBAL: XVT 0xA081E4
int g_objectLightDirectionY = 0;
/* Light direction, Z term: dotted with object row 1, else
 * g_worldLightDirectionZ. */
// GLOBAL: XVT 0xA08150
int g_objectLightDirectionZ = 0;
/* Row 0, Z term, of the current object matrix, Q15; see g_curMatR0_X. */
// GLOBAL: XVT 0xA00494
int g_curMatR0_Z = 0;
/* Row 0 (the side axis), X term, of the current object matrix, Q15. The
 * matrix's nine terms are written by FVIEW_SetObjectTransform,
 * FVIEW_calcrotatemove, FVIEW_transformaxes and FlightView_RotateViewByInput,
 * and in the original build Player_ApplyPitchYawSteps; FVIEW_BuildCameraOrient
 * also negates rows 1 and 2. */
// GLOBAL: XVT 0xA0049C
int g_curMatR0_X = 0;
/* Row 1 (the up axis), Y term, of the current object matrix, Q15; see
 * g_curMatR0_X. */
// GLOBAL: XVT 0xA004AC
int g_curMatR1_Y = 0;
/* Row 1, Z term, of the current object matrix, Q15; see g_curMatR0_X. */
// GLOBAL: XVT 0xA004B4
int g_curMatR1_Z = 0;
/* Row 1 (the up axis), X term, of the current object matrix, Q15; see
 * g_curMatR0_X. */
// GLOBAL: XVT 0xA004D0
int g_curMatR1_X = 0;
/* Row 2, X term, of the current object matrix, Q15: the negated forward axis
 * after FVIEW_calcrotatemove; see g_curMatR0_X. */
// GLOBAL: XVT 0xA07C68
int g_curMatR2_X = 0;
/* Row 2, Y term, of the current object matrix, Q15; see g_curMatR0_X. */
// GLOBAL: XVT 0xA07C6C
int g_curMatR2_Y = 0;
/* Row 2, Z term, of the current object matrix, Q15; see g_curMatR0_X. */
// GLOBAL: XVT 0xA07CC4
int g_curMatR2_Z = 0;
/* Forward direction, Y term, Q15: -g_curMatR2_Y as FVIEW_calcrotatemove, its
 * only writer, leaves it. */
// GLOBAL: XVT 0x9A8D84
int g_fviewMoveY_Q15 = 0;
/* Forward direction, Z term, Q15: -g_curMatR2_Z as FVIEW_calcrotatemove, its
 * only writer, leaves it. */
// GLOBAL: XVT 0x9A8D88
int g_fviewMoveZ_Q15 = 0;
/* Forward direction, X term, Q15: -g_curMatR2_X as FVIEW_calcrotatemove, its
 * only writer, leaves it. */
// GLOBAL: XVT 0x9A8DA4
int g_fviewMoveX_Q15 = 0;

/* Returns g_flightDirectDraw. std3D_Startup is its only caller. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x4079D0
IDirectDraw *Renderer_GetDirectDraw(void) { return g_flightDirectDraw; }

/* Sets up Direct3D for hardware drawing. Sets g_renderTextureCacheCursor to -1,
 * gives std3D the render target (g_displayModeWidth by g_displayModeHeight,
 * pitch g_surfacePitch, surface g_flightBackBuffer), clears the color overlay
 * and starts std3D, then picks the best device asking for hardware, perspective
 * texturing, a z-buffer and color model 2. When that device has all three it
 * creates it and gets the back buffer's attached z-buffer into
 * g_std3DZBufferSurface; when that fails, or when the device lacks one of the
 * three, it shuts std3D down (closing it first on the z-buffer failure) and
 * sets g_useHardware3D to 0. Every path ends with
 * Math_SetFpuSinglePrecisionMode. Its error lines go to DebugPrintf, which
 * prints nothing. FlightDisplay_Init calls it while g_useHardware3D is set. */
// FUNCTION: XVT 0x408170
void Renderer_InitD3DDevice(void)
{
	struct Std3DDeviceCaps deviceCaps;
	DDSURFACEDESC zBufferDesc;
	HRESULT result;
	unsigned int deviceIndex;

	g_renderTextureCacheCursor = -1;
	std3D_InitRenderTargetDesc((unsigned int)g_displayModeWidth,
				   (unsigned int)g_displayModeHeight,
				   g_surfacePitch);
	std3D_SetRenderSurface(g_flightBackBuffer);
	std3D_SetColorOverlayParams(0.0f, 0.0f, 0.0f, 0);
	std3D_Startup();

	memset(&deviceCaps, 0, sizeof(deviceCaps));
	deviceCaps.bHardware = 1;
	deviceCaps.bTexturePerspective = 1;
	deviceCaps.bHasZBuffer = 1;
	deviceCaps.colorModelFlags = 2;
	deviceIndex = std3D_SelectBestDevice(&deviceCaps);
	memcpy(&deviceCaps, &g_std3DDevices[deviceIndex].caps,
	       sizeof(deviceCaps));

	if (deviceCaps.bHasZBuffer != 0 &&
	    deviceCaps.bTexturePerspective != 0 && deviceCaps.bHardware != 0) {
		std3D_CreateDevice(deviceIndex, 1);
		memset(&zBufferDesc, 0, sizeof(zBufferDesc));
		zBufferDesc.dwSize = sizeof(zBufferDesc);
		zBufferDesc.dwFlags = DDSD_CAPS;
		zBufferDesc.ddsCaps.dwCaps = DDSCAPS_ZBUFFER;
		result = g_flightBackBuffer->lpVtbl->GetAttachedSurface(
			g_flightBackBuffer, &zBufferDesc.ddsCaps,
			&g_std3DZBufferSurface);
		if (result != 0) {
			DebugPrintf("ERROR(%x)! Failed to get HW Zbuffer\n",
				    result);
			std3D_Close();
			std3D_Shutdown();
			g_useHardware3D = 0;
			Math_SetFpuSinglePrecisionMode();
			return;
		}
		Math_SetFpuSinglePrecisionMode();
	} else {
		DebugPrintf(
			"Essential Hardware Feature NOT Supported: Z:%d Tex:%d HW:%d\n",
			deviceCaps.bHasZBuffer, deviceCaps.bTexturePerspective,
			deviceCaps.bHardware);
		std3D_Shutdown();
		g_useHardware3D = 0;
		Math_SetFpuSinglePrecisionMode();
	}
}
