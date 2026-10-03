#include "xvt/render/flight_sw.h"
#ifdef XVT_MODERN
#include "xvt_runtime/snapshot/render_camera.h"
#endif

#include "xvt/assets/file.h"
#include "xvt/flight/fediskio.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/flight_display.h"
#include "xvt/flight/flight_surface.h"
#include "xvt/flight/hud/flight_text.h"
#include "xvt/flight/transfm2.h"
#include "xvt/frontend/front_image.h"
#include "xvt/math/math2.h"
#include "xvt/math/trig2.h"
#include "xvt/render/flight_palette.h"
#include "xvt/render/render_scene.h"
#include "xvt/render/renderer.h"
#include "xvt/render/sw3d.h"
#include "xvt/util/game_rand.h"
#include "xvt/util/memory.h"
#include "xvt_runtime/compat/framebuffer_address.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* Stars on each side of each of the starfield's three grids: 32 /
 * g_starGridDivisor, set by FlightStarfield_Render on each call. */
// GLOBAL: XVT 0x51A830
int g_starfieldGridDimension = 0;
/* 1 once FlightStarfield_Render has made the 8-bit star colors; nothing sets it
 * back to 0. */
// GLOBAL: XVT 0x51A834
int g_starfieldColors8Initialized = 0;
/* 1 once FlightStarfield_Render has made the 16-bit star colors; nothing sets
 * it back to 0. */
// GLOBAL: XVT 0x51A838
int g_starfieldColors16Initialized = 0;
/* 1 once FlightStarfield_Render has picked each star's jitter vector; nothing
 * sets it back to 0. */
// GLOBAL: XVT 0x51A83C
int g_starfieldRandomVectorIndicesInitialized = 0;
/* Memory handle of the 3072 star colors as palette indices, made by
 * FlightStarfield_Render. */
// GLOBAL: XVT 0x51A840
uint16_t g_starfieldColors8Handle = 0;
/* Memory handle of the 3072 star colors as 16-bit colors, made by
 * FlightStarfield_Render. */
// GLOBAL: XVT 0x51A844
uint16_t g_starfieldColors16Handle = 0;
/* Memory handle of 3 * 3072 bytes, of which FlightStarfield_Render fills the
 * first 3072 with each star's jitter vector, 0 to 124. */
// GLOBAL: XVT 0x51A848
uint16_t g_starfieldRandomVectorIndicesHandle = 0;
/* x of the 125 star jitter vectors: Backdrop_BuildStarOffsetsAndRender fills
 * it, and FlightStarfield_Render adds one vector to each star. */
// GLOBAL: XVT 0x9A7810
int32_t g_starfieldJitterX[125] = {0};
/* y of the 125 star jitter vectors, filled and read as g_starfieldJitterX
 * is. */
// GLOBAL: XVT 0x9A7400
int32_t g_starfieldJitterY[125] = {0};
/* z of the 125 star jitter vectors, filled and read as g_starfieldJitterX
 * is. */
// GLOBAL: XVT 0x9A7600
int32_t g_starfieldJitterZ[125] = {0};
/* Palette index of empty sky: FlightStarfield_Render draws a star only over a
 * pixel of this color (its 16-bit color at 16 bits). Starts at 0xFB; set by
 * FeDiskIo_InitResources, FlightView_Render and Hud_Update3DCrt. */
// GLOBAL: XVT 0x523408
uint8_t g_flightBackgroundColorIndex = 0xFB;

/* The viewport and camera matrix PushFlightViewport saves, which
 * PopFlightViewport puts back. */
struct FlightViewportSaveState {
	uint16_t viewportX;  /* g_flightVpX. */
	uint16_t pad02;	     /* Never read or written. */
	uint16_t viewportY;  /* g_flightVpY. */
	uint16_t pad06;	     /* Never read or written. */
	int camMatR0_X;	     /* g_camMatR0_X. */
	int camMatR1_X;	     /* g_camMatR1_X. */
	int camMatR0_Y;	     /* g_camMatR0_Y. */
	int camMatR1_Y;	     /* g_camMatR1_Y. */
	int camMatR2_X;	     /* g_camMatR2_X. */
	int camMatR0_Z;	     /* g_camMatR0_Z. */
	int camMatR1_Z;	     /* g_camMatR1_Z. */
	int camMatR2_Y;	     /* g_camMatR2_Y. */
	int camMatR2_Z;	     /* g_camMatR2_Z. */
	uint16_t baseOffset; /* g_flightVpBaseOffset, cut to 16 bits. */
	uint16_t pad2E;	     /* Never read or written. */
	uint16_t height;     /* g_flightVpHeight. */
	uint16_t pad32;	     /* Never read or written. */
	uint16_t width;	     /* g_flightVpWidth. */
	uint16_t pad36;	     /* Never read or written. */
};

typedef char xvt_size_FlightViewportSaveState
	[(sizeof(FlightViewportSaveState) == 56) ? 1 : -1];

/* The start of a rotated sprite's encoded image: where its corner lies
 * relative to its screen point, in texels. */
struct FlightSwRotSpriteDataHeader {
	/* x of the first corner, used while g_flightSwRotSpriteSpanRunsEnabled
	 * is 1, which it always is. */
	int32_t cornerX;
	int32_t cornerY; /* y of the first corner, used negated. */
	/* x used, negated, when that flag is not 1, which never happens. */
	int32_t alternateCornerX;
	int32_t unused0C; /* Never read or written. */
};

/* The viewport and camera matrix PushFlightViewport saves and PopFlightViewport
 * puts back. */
// GLOBAL: XVT 0x555C88
FlightViewportSaveState g_savedFlightViewport = {0};

/* Entry i is tan(i * pi / 512) * 65536 / 1.1, rounded, up to 65535 at entry
 * 136; the last three are 0. Read by FlightSw_LookupScaledTangent for 91
 * percent. */
// GLOBAL: XVT 0x51C018
static uint16_t g_flightSwTangent91Pct[140] = {
	0,     366,   731,   1097,  1463,  1828,  2194,	 2561,	2927,  3293,
	3660,  4027,  4395,  4762,  5131,  5499,  5868,	 6237,	6607,  6977,
	7348,  7720,  8092,  8464,  8838,  9212,  9586,	 9962,	10338, 10715,
	11093, 11471, 11851, 12231, 12613, 12995, 13379, 13763, 14149, 14536,
	14924, 15313, 15703, 16095, 16488, 16882, 17277, 17674, 18073, 18473,
	18874, 19277, 19682, 20088, 20496, 20906, 21317, 21731, 22146, 22563,
	22982, 23403, 23826, 24251, 24678, 25107, 25539, 25973, 26409, 26848,
	27289, 27732, 28178, 28627, 29078, 29532, 29989, 30449, 30911, 31377,
	31845, 32317, 32791, 33269, 33751, 34235, 34723, 35215, 35710, 36209,
	36711, 37217, 37727, 38242, 38760, 39282, 39809, 40340, 40875, 41415,
	41960, 42509, 43063, 43622, 44186, 44755, 45330, 45910, 46495, 47086,
	47683, 48286, 48895, 49509, 50131, 50758, 51392, 52033, 52681, 53336,
	53999, 54668, 55345, 56030, 56723, 57424, 58134, 58852, 59578, 60314,
	61059, 61813, 62577, 63351, 64135, 64929, 65535, 0,	0,     0,
};
/* Only the first 8 entries of g_flightSwTangent100Pct are 100% values; from entry 8 on it repeats
 * g_flightSwTangent91Pct, as the original data does. */
/* Entries 0 to 7 are tan(i * pi / 512) * 65536, rounded. Read by
 * FlightSw_LookupScaledTangent for any percentage but 91 and 110. */
// GLOBAL: XVT 0x51C130
static uint16_t g_flightSwTangent100Pct[140] = {
	0,     402,   804,   1206,  1608,  2011,  2414,	 2817,	2927,  3293,
	3660,  4027,  4395,  4762,  5131,  5499,  5868,	 6237,	6607,  6977,
	7348,  7720,  8092,  8464,  8838,  9212,  9586,	 9962,	10338, 10715,
	11093, 11471, 11851, 12231, 12613, 12995, 13379, 13763, 14149, 14536,
	14924, 15313, 15703, 16095, 16488, 16882, 17277, 17674, 18073, 18473,
	18874, 19277, 19682, 20088, 20496, 20906, 21317, 21731, 22146, 22563,
	22982, 23403, 23826, 24251, 24678, 25107, 25539, 25973, 26409, 26848,
	27289, 27732, 28178, 28627, 29078, 29532, 29989, 30449, 30911, 31377,
	31845, 32317, 32791, 33269, 33751, 34235, 34723, 35215, 35710, 36209,
	36711, 37217, 37727, 38242, 38760, 39282, 39809, 40340, 40875, 41415,
	41960, 42509, 43063, 43622, 44186, 44755, 45330, 45910, 46495, 47086,
	47683, 48286, 48895, 49509, 50131, 50758, 51392, 52033, 52681, 53336,
	53999, 54668, 55345, 56030, 56723, 57424, 58134, 58852, 59578, 60314,
	61059, 61813, 62577, 63351, 64135, 64929, 65535, 0,	0,     0,
};
/* Entry i is tan(i * pi / 512) * 65536 * 1.1, rounded, up to 65535 at entry
 * 121; the last two are 0. Read by FlightSw_LookupScaledTangent for 110
 * percent. */
// GLOBAL: XVT 0x51C248
static uint16_t g_flightSwTangent110Pct[124] = {
	0,     442,   885,   1327,  1770,  2212,  2655,	 3098,	3542,  3985,
	4429,  4873,  5318,  5763,  6208,  6654,  7100,	 7547,	7995,  8443,
	8891,  9341,  9791,  10242, 10693, 11146, 11599, 12054, 12509, 12965,
	13422, 13880, 14340, 14800, 15261, 15724, 16188, 16654, 17120, 17588,
	18058, 18528, 19001, 19474, 19950, 20427, 20906, 21386, 21868, 22352,
	22838, 23326, 23815, 24307, 24800, 25296, 25794, 26294, 26796, 27301,
	27808, 28317, 28829, 29344, 29860, 30380, 30902, 31427, 31955, 32486,
	33019, 33556, 34096, 34639, 35185, 35734, 36287, 36843, 37403, 37966,
	38533, 39103, 39678, 40256, 40838, 41425, 42015, 42610, 43209, 43812,
	44420, 45033, 45650, 46272, 46899, 47532, 48169, 48811, 49459, 50112,
	50771, 51436, 52106, 52783, 53465, 54154, 54849, 55551, 56259, 56974,
	57697, 58426, 59162, 59906, 60658, 61417, 62185, 62961, 63744, 64537,
	65338, 65535, 0,     0,
};

/* x and y of the 10 pixels of the radar target marker at 16 bits, the shape of
 * g_radarTargetMarkerShape10. */
// GLOBAL: XVT 0x523910
int8_t g_radarTargetMarkerShape16bpp[20] = {
	-1, 1, -2, 1, -2, 0, -2, -1, -1, -1, 1, -1, 2, -1, 2, 0, 2, 1, 1, 1,
};
/* The 10-pixel radar target marker, then two unused 0, 0 entries. */
// GLOBAL: XVT 0x51A7E8
static FlightRadarMarkerOffset g_radarTargetMarkerShape10[12] = {
	{-1, 1}, {-2, 1}, {-2, 0}, {-2, -1}, {-1, -1}, {1, -1},
	{2, -1}, {2, 0},  {2, 1},  {1, 1},   {0, 0},   {0, 0},
};
/* The 12-pixel radar target marker. */
// GLOBAL: XVT 0x51A800
static FlightRadarMarkerOffset g_radarTargetMarkerShape12[12] = {
	{-1, 2}, {-2, 2}, {-2, 1}, {-2, 0}, {-2, -1}, {-1, -1},
	{1, -1}, {2, -1}, {2, 0},  {2, 1},  {2, 2},   {1, 2},
};
/* The radar target marker shape at 8 bits, set by FlightSw_InitFramebuffer:
 * g_radarTargetMarkerShape10 at 320x240 or in an unknown mode,
 * g_radarTargetMarkerShape12 at 640x480 and 480x360. */
// GLOBAL: XVT 0x51A818
int8_t *g_radarTargetMarkerShape = (int8_t *)g_radarTargetMarkerShape10;
/* Pixels in g_radarTargetMarkerShape, 10 or 12, set with it. */
// GLOBAL: XVT 0x51A81C
int g_radarTargetMarkerPointCount = 10;
/* The 7 pixels of the cross marker: 5 across and 3 down through its center. */
// GLOBAL: XVT 0x51A820
FlightSwMarkerOffset g_flightSwCrossMarkerOffsets[7] = {
	{-2, 0}, {-1, 0}, {0, 0}, {1, 0}, {2, 0}, {0, 1}, {0, -1},
};
/* The same 7 pixels for 16-bit drawing. */
// GLOBAL: XVT 0x523928
static FlightSwMarkerOffset g_flightSwCrossMarkerOffsets16bpp[7] = {
	{-2, 0}, {-1, 0}, {0, 0}, {1, 0}, {2, 0}, {0, 1}, {0, -1},
};
/* (1 << mode) - 1 for packing modes 0 to 8: the run-length bits of a sprite run
 * byte. */
// GLOBAL: XVT 0x523600
uint8_t g_flightSwRleRunLengthMaskByPackingMode[9] = {0,  1,  3,   7,  15,
						      31, 63, 127, 255};
/* The shift for packing modes 0 to 8, equal to the mode, that brings a run
 * byte's color index down. */
// GLOBAL: XVT 0x523610
uint8_t g_flightSwRlePaletteShiftByPackingMode[9] = {0, 1, 2, 3, 4, 5, 6, 7, 8};
/* Starts at 1, and every writer sets 1:
 * FlightSw_RasterizePreparedRotatedSprite, RenderQuad_DrawModelTexture,
 * Backdrop_DrawModelTexQuadAtScreen, and at flight start Flight_MainLoop in the
 * original build and XvtFlightLoading_Globals in the modern one. So the arms of
 * FlightSw_DrawRotatedSpriteQuad and FlightSw_RasterizePreparedRotatedSprite
 * for other values never run. */
// GLOBAL: XVT 0x52361C
int g_flightSwRotSpriteSpanRunsEnabled = 1;
/* The pixels under the radar target marker at 8 bits, saved by
 * FlightSw_DrawRadarTargetMarker8bpp and put back by
 * FlightSw_RestoreRadarTargetMarker8bpp; up to 12 used. */
// GLOBAL: XVT 0x54F9C0
uint8_t g_radarTargetMarkerSavedPixels[16] = {0};
/* The pixels under the cross marker at 8 bits; the two functions that use it
 * have no caller. */
// GLOBAL: XVT 0x54F9D8
uint8_t g_flightSwCrossMarkerSavedPixels[7] = {0};
/* Row pitch in bytes of the target FlightSw_SetRenderTarget was last given with
 * a pitch; g_flightLinePitchPtr points here while that target is in use. */
// GLOBAL: XVT 0x54F9B0
int g_flightAltLinePitch = 0;
/* Byte offset of each row of that target: row times width times
 * g_flightBytesPerPixel, not times its pitch. */
// GLOBAL: XVT 0x54F9E0
int g_flightAltLineOffsetTable[768] = {0};
/* The pixels under the cross marker at 16 bits; the two functions that use it
 * have no caller. */
// GLOBAL: XVT 0x5569C0
static uint16_t g_flightSwCrossMarkerSavedPixels16bpp[7] = {0};
/* The 10 pixels under the radar target marker at 16 bits, saved by
 * FlightSw_DrawRadarTargetMarker16bpp and put back by
 * FlightSw_RestoreRadarTargetMarker16bpp. */
// GLOBAL: XVT 0x556998
uint16_t g_radarTargetMarkerSavedPixels16bpp[10];
/* Row after the last of the rectangle FlightSw_FillRectOrBorder16bpp fills. Set
 * by FlightSw_FillClipRect16bpp, FlightSw_FillRectClipped16bpp and
 * FlightText_ClearRemainingLineBackground. */
// GLOBAL: XVT 0x556988
uint16_t g_flightFillRectBottom16bpp = 0;
/* Column after the last of that rectangle, set as g_flightFillRectBottom16bpp
 * is. */
// GLOBAL: XVT 0x55698C
uint16_t g_flightFillRectRight16bpp = 0;
/* First column of that rectangle, set as g_flightFillRectBottom16bpp is. */
// GLOBAL: XVT 0x556990
uint16_t g_flightFillRectLeft16bpp = 0;
/* First row of that rectangle, set as g_flightFillRectBottom16bpp is. */
// GLOBAL: XVT 0x556994
uint16_t g_flightFillRectTop16bpp = 0;
/* Row FlightSw_FillRectOrBorder16bpp is filling; only it writes it. */
// GLOBAL: XVT 0x5569B8
int g_flightFillRectCurrentY16bpp = 0;
/* Rows FlightSw_FillRectOrBorder16bpp has left to fill; only it writes it. */
// GLOBAL: XVT 0x5569D0
int g_flightFillRectRemainingRows16bpp = 0;
/* Row FlightSw_FillRectOrBorder8bpp is filling; only it writes it. */
// GLOBAL: XVT 0x54F9D0
int32_t g_flightFillRectCurrentY8bpp = 0;
/* First row of the rectangle FlightSw_FillRectOrBorder8bpp fills. Set by
 * FlightSw_FillClipRect8bpp, FlightSw_FillRectClipped8bpp and
 * FlightText_ClearRemainingLineBackground8bpp. */
// GLOBAL: XVT 0x54F9B8
uint16_t g_flightFillRectTop8bpp = 0;
/* Row after the last of that rectangle, set as g_flightFillRectTop8bpp is. */
// GLOBAL: XVT 0x54F9A8
uint16_t g_flightFillRectBottom8bpp = 0;
/* Column after the last of that rectangle, set as g_flightFillRectTop8bpp
 * is. */
// GLOBAL: XVT 0x54F9AC
uint16_t g_flightFillRectRight8bpp = 0;
/* First column of that rectangle, set as g_flightFillRectTop8bpp is. */
// GLOBAL: XVT 0x54F9B4
uint16_t g_flightFillRectLeft8bpp = 0;
/* Rows FlightSw_FillRectOrBorder8bpp has left to fill; only it writes it. */
// GLOBAL: XVT 0x5505E8
unsigned int g_flightFillRectRemainingRows8bpp = 0;
/* y of the point FlightSw_RotateSpritePoint last turned and scaled, in pixels
 * from the sprite's screen point; only it writes it. */
// GLOBAL: XVT 0x9A7B40
int16_t g_flightSwRotSpriteOutputOffsetY = 0;
/* x of that point; only FlightSw_RotateSpritePoint writes it. */
// GLOBAL: XVT 0x9A7B48
int16_t g_flightSwRotSpriteOutputOffsetX = 0;
/* x of the sprite point FlightSw_RotateSpritePoint turns, in texels;
 * FlightSw_DrawRotatedSpriteQuad sets it for each corner, and
 * FlightSw_RotateSpritePoint leaves its magnitude there. */
// GLOBAL: XVT 0x9A8D4A
int16_t g_flightSwRotSpriteInputCornerX = 0;
/* y of that point, set and left as g_flightSwRotSpriteInputCornerX is. */
// GLOBAL: XVT 0x9A8D4E
int16_t g_flightSwRotSpriteInputCornerY = 0;
/* Screen x of the sprite's first corner, set by FlightSw_DrawRotatedSpriteQuad;
 * the octant set-up functions then move it along the edge. */
// GLOBAL: XVT 0x9A8D42
int16_t g_flightSwRotSpriteEdgeCursorX = 0;
/* Screen y of the sprite's first corner, set and moved as
 * g_flightSwRotSpriteEdgeCursorX is. */
// GLOBAL: XVT 0x9A8D48
int16_t g_flightSwRotSpriteEdgeCursorY = 0;
/* Row of the radar target marker to put back: Hud_DrawRadarBlips copies
 * g_radarTargetMarkerDrawY into it. */
// GLOBAL: XVT 0xA08C86
uint16_t g_radarTargetMarkerRestoreY;
/* Column of the radar target marker to put back: Hud_DrawRadarBlips copies
 * g_radarTargetMarkerDrawX into it. */
// GLOBAL: XVT 0xA08C88
uint16_t g_radarTargetMarkerRestoreX;
/* Column where the radar target marker is drawn, set by Hud_AddBlipToRadar for
 * the target's blip. */
// GLOBAL: XVT 0xA0A1D2
uint16_t g_radarTargetMarkerDrawX = 0;
/* Row where the radar target marker is drawn, set by Hud_AddBlipToRadar for the
 * target's blip. */
// GLOBAL: XVT 0xA0A1D4
uint16_t g_radarTargetMarkerDrawY = 0;
/* Column of the RLE sprite being drawn, set by the four RLE blit functions. */
// GLOBAL: XVT 0x9CC452
int16_t g_flightSwRleSpriteX = 0;
/* Row of the RLE sprite being drawn: the four RLE blit functions set it and
 * raise it by 1 per row. */
// GLOBAL: XVT 0x9CC458
int16_t g_flightSwRleSpriteY = 0;
/* Added to each run color of an RLE sprite: FlightSw_BlitSpriteRle8bpp and
 * FlightSw_BlitSpriteRle16bpp set it to 0, the faded draws to their palette
 * shift, and a 0xFB code in the data sets it from the next byte, except in a
 * faded draw. */
// GLOBAL: XVT 0x9ED21D
int8_t g_flightSwRlePaletteShift = 0;
/* The transparent index the RLE blit functions were last given; nothing reads
 * it. */
// GLOBAL: XVT 0x9ED23A
uint8_t g_flightSwRleTransparentColor = 0;

#ifndef XVT_MODERN
/* Window number passed to RtsVga2_SetCurrentPage, which ignores it; nothing
 * writes it, so it stays 0. */
// GLOBAL: XVT 0x5233D4
unsigned int g_vesaWindow = 0;
#endif
/* The flight display mode, a FlightResolutionMode. Flight_MainLoop in the
 * original build and XvtFlightLoading_Globals in the modern one set it from
 * g_surfaceWidth: 320x240 for 320, 480x360 for 480, else 640x480. */
// GLOBAL: XVT 0x5233EC
int g_flightResolutionMode = FLIGHT_RESOLUTION_640X480;
/* 1 when pixels are square (g_projAspectY of 0), set by
 * FlightSw_PrepareSpriteRotationTables: picks the 100 percent tangent table and
 * the 256 scales. */
// GLOBAL: XVT 0x5235F8
int g_flightSwRotSpriteSquarePixelMode = 0;
/* 1 once g_flightSwRotSpriteCoeffCache holds tables, set by
 * FlightSw_PrepareSpriteRotationTables. Set to 0, to have them built again, by
 * Hud_Update3DCrt and at flight start: by Flight_MainLoop in the original
 * build, XvtFlightLoading_Globals in the modern one. */
// GLOBAL: XVT 0x5235F4
int g_flightSwRotSpriteCoeffCacheValid = 0;
/* Nothing writes it, so it stays 0 and SetFlightViewport's inset of 160 never
 * applies. */
// GLOBAL: XVT 0x5233DC
int g_flightViewportInsetX = 0;
/* Where the software drawing functions write: the surface
 * FlightSurface_GetSoftwareFramebufferBase returns, or a target given to
 * FlightSw_SetRenderTarget. Starts at 0xA0000, the VGA window. Many functions
 * write it, chiefly FlightSurface_Lock and FlightSw_SetRenderTarget. */
// GLOBAL: XVT 0x5233F4
uint8_t *g_flightSwFramebufferBase = (uint8_t *)(uintptr_t)0xA0000;
/* Offset in g_flightAuxBuffer of the viewport span mask in use: 0xC000, or
 * 0xE000 from PushFlightViewport until PopFlightViewport, which alone write
 * it. */
// GLOBAL: XVT 0x52747C
uint16_t g_viewportSpanMaskOffset = 0xC000;
/* The row offset table FlightSw_GetLineOffset reads: g_flightLineOffsetTable,
 * or g_flightAltLineOffsetTable while FlightSw_SetRenderTarget has a target
 * with a pitch. */
// GLOBAL: XVT 0x5505E0
int *g_flightActiveLineOffsetTable;
/* Points at the row pitch FlightSw_GetLinePitch returns: g_surfacePitch, or
 * g_flightAltLinePitch while FlightSw_SetRenderTarget has a target with a
 * pitch. */
// GLOBAL: XVT 0x5505E4
int *g_flightLinePitchPtr;
/* A shared work buffer that holds, among other things, the viewport span masks
 * at g_viewportSpanMaskOffset. Many functions write it, chiefly
 * FeDiskIo_InitGlobalBuffers and FeDiskIo_LockGlobalBuffers. */
// GLOBAL: XVT 0x9A8074
uint8_t *g_flightAuxBuffer = 0;
/* Destination line the rotated-sprite walk is drawing:
 * FlightSw_PrepareSpriteRotationTables sets it to the buffer's start,
 * FlightSw_RasterizePreparedRotatedSprite to the first line, and each octant
 * step moves it. */
// GLOBAL: XVT 0x9A8C18
uint8_t *g_flightSwRotSpriteDestLinePtr = 0;
/* The sprite's 8-bit drawing palette from FlightSw_LoadSpritePaletteTables;
 * FlightSw_BlitPreparedRotatedSpriteSpans maps sprite colors through it. */
// GLOBAL: XVT 0x9A8C40
uint8_t g_flightSwRotSpritePalette8[256] = {0};
/* The runs of the sprite row being drawn, scaled, built by
 * FlightSw_RasterizePreparedRotatedSprite. Holds 512; nothing checks the count
 * against that. */
// GLOBAL: XVT 0x9A57E0
FlightSwRotSpriteSpanRun g_flightSwRotSpriteSpanRuns[512] = {{0}};
/* Index in runLengths by which the octant 0 to 3 functions step
 * g_flightSwRotSpriteClipMinX. */
// GLOBAL: XVT 0x9A73F6
static int16_t g_flightSwRotSpriteClipMinRunIdx03 = 0;
/* Row of the edge's far end, which the octant functions set and step. */
// GLOBAL: XVT 0x9A7BB0
static int16_t g_flightSwRotSpriteSecondaryEdgeY = 0;
/* Column of the edge's far end, which the octant functions set and step. */
// GLOBAL: XVT 0x9A7BB2
static int16_t g_flightSwRotSpriteSecondaryEdgeX = 0;
/* 1 makes the next FlightSw_AdvanceRotSpriteSecondaryScale only clear it: set
 * at a sprite's start, and by that function when the span base reaches a step
 * in the edge. */
// GLOBAL: XVT 0x9CD266
int16_t g_flightSwRotSpriteSkipSecondaryScaleStep = 0;
/* Byte offset of each row of the software surface, row times g_surfacePitch:
 * FlightSw_InitFramebuffer fills g_screenHeight rows, and
 * FlightSw_SetRenderTarget given a pitch of -1 fills the target's rows. */
// GLOBAL: XVT 0x9CC460
int g_flightLineOffsetTable[768] = {0};
/* High bytes of the sprite's 16-bit drawing colors, from
 * FlightSw_LoadSpritePaletteTables. */
// GLOBAL: XVT 0x9D1160
uint8_t g_flightSwRotSpritePalette16High[256] = {0};
/* Sum FlightSw_AdvanceRotSpriteSecondaryScale adds secondaryScaleLow to, whose
 * carries move the span base; set to 0 at a sprite's start. */
// GLOBAL: XVT 0x9D12E0
uint16_t g_flightSwRotSpriteSecondaryScaleAccum = 0;
/* Index in runLengths by which the octant 0 to 3 functions step
 * g_flightSwRotSpriteClipMaxX. */
// GLOBAL: XVT 0x9D1314
static int16_t g_flightSwRotSpriteClipMaxRunIdx03 = 0;
/* Index in runLengths by which the octant 4 to 7 functions step
 * g_flightSwRotSpriteClipMaxX. */
// GLOBAL: XVT 0x9D8C14
static int16_t g_flightSwRotSpriteClipMaxRunIdx47 = 0;
/* Index in runLengths by which the octant 4 to 7 functions step
 * g_flightSwRotSpriteClipMinX. */
// GLOBAL: XVT 0x9D8C2A
static int16_t g_flightSwRotSpriteClipMinRunIdx47 = 0;
/* g_flightVpWidth, copied by FlightSw_PrepareSpriteRotationTables. */
// GLOBAL: XVT 0x9D8C2C
int16_t g_flightSwRotSpriteViewportWidth = 0;
/* Low bytes of the sprite's 16-bit drawing colors, from
 * FlightSw_LoadSpritePaletteTables. */
// GLOBAL: XVT 0x9D6830
uint8_t g_flightSwRotSpritePalette16Low[256] = {0};
/* The rotation tables for the last angle, built by
 * FlightSw_BuildSpriteRotationCoeffs when the angle changes or
 * g_flightSwRotSpriteCoeffCacheValid is 0. */
// GLOBAL: XVT 0x9CD280
FlightSwRotSpriteCoeffState g_flightSwRotSpriteCoeffCache = {0};
/* The buffer rotated sprites are drawn into before
 * FlightSw_BlitPreparedRotatedSpriteSpans copies them out; set through
 * FlightSw_SetRotatedSpriteDestBuffer by FeDiskIo_InitGlobalBuffers and
 * FeDiskIo_LockGlobalBuffers. */
// GLOBAL: XVT 0x9D77C4
uint8_t *g_flightSwRotSpriteDestBuffer = NULL;
/* g_flightVpMaxX, copied by FlightSw_PrepareSpriteRotationTables. */
// GLOBAL: XVT 0x9D77F0
int16_t g_flightSwRotSpriteViewportMaxX = 0;
/* Edge position from which the clipped span functions draw on the current line;
 * the octant functions set and step it. */
// GLOBAL: XVT 0x9EC458
int16_t g_flightSwRotSpriteClipMinX = 0;
/* Copy of g_flightSwRotSpriteClipMinX taken by
 * FlightSw_RasterizePreparedRotatedSprite; nothing reads it. */
// GLOBAL: XVT 0x9E9646
uint16_t g_flightSwRotSpriteSavedClipMinX = 0;
/* Copy of g_flightSwRotSpriteClipMaxX taken by
 * FlightSw_RasterizePreparedRotatedSprite; nothing reads it. */
// GLOBAL: XVT 0x9E9650
uint16_t g_flightSwRotSpriteSavedClipMaxX = 0;
/* Edge position of the current line's start: run positions are added to it to
 * index spanOffsets. Set by the octant set-up functions and moved by
 * FlightSw_AdvanceRotSpriteSecondaryScale. */
// GLOBAL: XVT 0x9EC462
int16_t g_flightSwRotSpriteSpanBaseX = 0;
/* Column where the current line starts: fixed by the set-up for octants 0 to 3,
 * stepped by the octant 4 to 7 functions. */
// GLOBAL: XVT 0x9E9654
int16_t g_flightSwRotSpritePrimaryEdgeX = 0;
/* Row where the current line starts: stepped by the octant 0 to 3 functions,
 * fixed by the set-up for octants 4 to 7. */
// GLOBAL: XVT 0x9E9652
int16_t g_flightSwRotSpritePrimaryEdgeY = 0;
/* g_flightVpMaxY, copied by FlightSw_PrepareSpriteRotationTables. */
// GLOBAL: XVT 0x9ED230
int16_t g_flightSwRotSpriteViewportMaxY = 0;
/* Set to -1 by FlightSw_PrepareSpriteRotationTables, its only writer, so the
 * arms that test it for a positive value never run. */
// GLOBAL: XVT 0x9E95F2
int16_t g_flightSwRotSpriteDestYMode = 0;
/* Row pitch of g_flightSwRotSpriteDestBuffer: g_flightBytesPerPixel *
 * g_flightVpWidth, set by FlightSw_PrepareSpriteRotationTables. */
// GLOBAL: XVT 0x9A8D50
int g_flightSwRotSpriteDestPitchBytes = 0;
/* The scale of the sprite being drawn, from
 * FlightSw_PrepareRotatedSpriteScaleState. */
// GLOBAL: XVT 0x9EC610
FlightSwRotSpriteScaleState g_flightSwRotSpriteScaleState = {0};
/* Copy of g_flightSwRotSpritePrimaryEdgeY taken by
 * FlightSw_RasterizePreparedRotatedSprite; nothing reads it. */
// GLOBAL: XVT 0xA00850
uint16_t g_flightSwRotSpriteSavedPrimaryEdgeY = 0;
/* Copy of g_flightSwRotSpritePrimaryEdgeX taken by
 * FlightSw_RasterizePreparedRotatedSprite; nothing reads it. */
// GLOBAL: XVT 0xA00852
uint16_t g_flightSwRotSpriteSavedPrimaryEdgeX = 0;
/* Pixels left in the row the screen-rectangle save and restore functions are
 * copying. */
// GLOBAL: XVT 0x9ED238
uint16_t g_savedRowPixelsRemaining = 0;
/* Edge position at which the clipped span functions stop on the current line;
 * under 0, FlightSw_RasterizePreparedRotatedSprite draws nothing on it. The
 * octant functions set and step it. */
// GLOBAL: XVT 0x9D114C
int16_t g_flightSwRotSpriteClipMaxX = 0;
/* Points at g_flightSwRotSpriteCoeffCache once
 * FlightSw_PrepareSpriteRotationTables runs. */
// GLOBAL: XVT 0x9FE7D4
FlightSwRotSpriteCoeffState *g_flightSwRotSpriteCoeffs = 0;
/* Runs left for a span draw function: FlightSw_RasterizePreparedRotatedSprite
 * sets it to the row's run count, and the function counts it down. */
// GLOBAL: XVT 0xA60A50
int g_flightSwRotSpriteSpanRunCountdown = 0;
/* g_flightVpHeight, copied by FlightSw_PrepareSpriteRotationTables. */
// GLOBAL: XVT 0xA07CD0
int16_t g_flightSwRotSpriteViewportHeight = 0;
/* Folded angle from which a rotated sprite's edge steps along y: 0x2000 with
 * square pixels, else 0x2200. Set by FlightSw_PrepareSpriteRotationTables and
 * FlightSw_PrepareRotatedSpriteScaleState. */
// GLOBAL: XVT 0xA080F2
uint16_t g_flightSwRotSpriteAxisSwapThresholdAngle = 0;

/* Fills g_flightLineOffsetTable for g_screenHeight rows of g_surfacePitch, sets
 * g_flightSwFramebufferBase from FlightSurface_GetSoftwareFramebufferBase and
 * clears g_surfacePitch * g_screenHeight bytes there (the original build at
 * 640x480 a VESA page at a time), picks the radar target marker for
 * g_flightResolutionMode (12 pixels at 640x480 and 480x360, else 10), and
 * points g_flightLinePitchPtr and g_flightActiveLineOffsetTable at
 * g_surfacePitch and that table. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x40DF00
void FlightSw_InitFramebuffer(void)
{
	unsigned int line;
#ifndef XVT_MODERN
	unsigned int page;
#endif

	for (line = 0; line < (unsigned int)g_screenHeight; ++line) {
		g_flightLineOffsetTable[line] = line * g_surfacePitch;
	}

	g_flightSwFramebufferBase = FlightSurface_GetSoftwareFramebufferBase();
	switch (g_flightResolutionMode) {
	case FLIGHT_RESOLUTION_320X240:
		memset(g_flightSwFramebufferBase, 0,
		       g_surfacePitch * g_screenHeight);
		g_radarTargetMarkerShape = (int8_t *)g_radarTargetMarkerShape10;
		g_radarTargetMarkerPointCount = 10;
		break;

#ifndef XVT_MODERN
	case FLIGHT_RESOLUTION_640X480:
		for (page = 0;
		     page < (unsigned int)(g_surfacePitch * g_screenHeight) /
				    g_vesaPageSizeBytes;
		     ++page) {
			RtsVga2_SetCurrentPage((uint8_t)g_vesaWindow,
					       (uint16_t)page);
			memset(g_flightSwFramebufferBase, 0,
			       g_vesaPageSizeBytes);
		}
		if ((unsigned int)(g_surfacePitch * g_screenHeight) %
			    g_vesaPageSizeBytes !=
		    0) {
			RtsVga2_SetCurrentPage(
				(uint8_t)g_vesaWindow,
				(uint16_t)((unsigned int)(g_surfacePitch *
							  g_screenHeight) /
					   g_vesaPageSizeBytes));
			memset(g_flightSwFramebufferBase, 0,
			       (unsigned int)(g_surfacePitch * g_screenHeight) %
				       g_vesaPageSizeBytes);
		}
		g_radarTargetMarkerShape = (int8_t *)g_radarTargetMarkerShape12;
		g_radarTargetMarkerPointCount = 12;
		break;
#else
	case FLIGHT_RESOLUTION_640X480:
		memset(g_flightSwFramebufferBase, 0,
		       g_surfacePitch * g_screenHeight);
		g_radarTargetMarkerShape = (int8_t *)g_radarTargetMarkerShape12;
		g_radarTargetMarkerPointCount = 12;
		break;
#endif

	case FLIGHT_RESOLUTION_480X360:
		memset(g_flightSwFramebufferBase, 0,
		       g_surfacePitch * g_screenHeight);
		g_radarTargetMarkerShape = (int8_t *)g_radarTargetMarkerShape12;
		g_radarTargetMarkerPointCount = 12;
		break;

	default:
		memset(g_flightSwFramebufferBase, 0,
		       g_surfacePitch * g_screenHeight);
		g_radarTargetMarkerShape = (int8_t *)g_radarTargetMarkerShape10;
		g_radarTargetMarkerPointCount = 10;
		break;
	}

	g_flightLinePitchPtr = &g_surfacePitch;
	g_flightActiveLineOffsetTable = g_flightLineOffsetTable;
}

/* Points the software drawing functions at a target. With surface NULL: back at
 * the surface from FlightSurface_GetSoftwareFramebufferBase, with
 * g_surfacePitch and g_flightLineOffsetTable. With pitchBytes -1: at surface
 * with g_surfacePitch, refilling g_flightLineOffsetTable for height rows.
 * Otherwise at surface with pitchBytes in g_flightAltLinePitch, and
 * g_flightAltLineOffsetTable filled for height rows of width *
 * g_flightBytesPerPixel bytes, so row offsets follow that product, not
 * pitchBytes. Does not check height against the tables' 768 rows. */
// FUNCTION: XVT 0x40E0C0
void FlightSw_SetRenderTarget(void *surface, int width, unsigned int height,
			      int pitchBytes)
{
	int16_t line;
	int lineIndex;
	int bytesPerPixel;
	int lineOffset;

	if (surface == NULL) {
		g_flightSwFramebufferBase =
			FlightSurface_GetSoftwareFramebufferBase();
		g_flightLinePitchPtr = &g_surfacePitch;
		g_flightActiveLineOffsetTable = g_flightLineOffsetTable;
		return;
	}

	g_flightSwFramebufferBase = (uint8_t *)surface;
	if (pitchBytes == -1) {
		line = 0;
		if (height != 0) {
			do {
				lineIndex = line++;
				g_flightLineOffsetTable[lineIndex] =
					lineIndex * g_surfacePitch;
			} while ((unsigned int)(int)line < height);
		}
		g_flightLinePitchPtr = &g_surfacePitch;
		g_flightActiveLineOffsetTable = g_flightLineOffsetTable;
		return;
	}

	line = 0;
	if (height != 0) {
		bytesPerPixel = g_flightBytesPerPixel;
		do {
			lineIndex = line++;
			lineOffset = width;
			lineOffset *= bytesPerPixel;
			lineOffset *= lineIndex;
			g_flightAltLineOffsetTable[lineIndex] = lineOffset;
		} while ((unsigned int)(int)line < height);
	}
	g_flightAltLinePitch = pitchBytes;
	g_flightLinePitchPtr = &g_flightAltLinePitch;
	g_flightActiveLineOffsetTable = g_flightAltLineOffsetTable;
}

/* Returns the byte offset of row line from the active row table. Does not check
 * line. */
// FUNCTION: XVT 0x40E190
int FlightSw_GetLineOffset(int line)
{
	return g_flightActiveLineOffsetTable[line];
}

/* Returns the active row pitch in bytes, *g_flightLinePitchPtr. */
// FUNCTION: XVT 0x40E1A0
int FlightSw_GetLinePitch(void) { return *g_flightLinePitchPtr; }

/* Returns x plus y times the active row pitch. */
// FUNCTION: XVT 0x40EA50
int FlightSw_ComputePixelOffset8bpp(int x, int y)
{
	return x + y * FlightSw_GetLinePitch();
}

/* Draws an RLE sprite at 8 bits without fading: sets g_flightSwRlePaletteShift
 * to 0 and calls FlightSw_BlitSpriteRleImpl8bpp. */
// FUNCTION: XVT 0x40EA60
void FlightSw_BlitSpriteRle8bpp(uint8_t *rleData, int x, int y,
				int transparentColorIndex, int mirror)
{
	g_flightSwRlePaletteShift = 0;
	FlightSw_BlitSpriteRleImpl8bpp(rleData, x, y, transparentColorIndex,
				       mirror, 0, 0);
}

/* Draws an RLE sprite at 8 bits, faded: sets g_flightSwRlePaletteShift to
 * paletteShift and calls FlightSw_BlitSpriteRleImpl8bpp without mirroring. */
// FUNCTION: XVT 0x40EA90
void FlightSw_BlitSpriteRleFaded8bpp(uint8_t *rleData, int x, int y,
				     int transparentColorIndex,
				     int8_t paletteShift, int16_t fadeAmount)
{
	g_flightSwRlePaletteShift = paletteShift;
	FlightSw_BlitSpriteRleImpl8bpp(rleData, x, y, transparentColorIndex, 0,
				       1, fadeAmount);
}

/* Draws an RLE sprite on the 8-bit frame buffer from x, y, a row at a time,
 * rightward or, with mirror, leftward. A byte under 0xFB is a run of (byte & 3)
 * + 1 pixels of color byte >> 2; 0xFD is a run of the next byte + 1 pixels of
 * the color after it; 0xFC draws the byte after next + 1 pixels alternating
 * between the next byte's color and the color 1 above it; 0xFB sets
 * g_flightSwRlePaletteShift from the next byte, which a faded draw skips; 0xFE
 * ends a row and 0xFF the sprite. When unfaded, runs under 0xFB add
 * g_flightSwRlePaletteShift to their color. Any run but 0xFC whose color then
 * equals transparentColorIndex is skipped. Faded, each drawn color becomes
 * color - fadeAmount + g_flightSwRlePaletteShift, or g_flightSwRlePaletteShift
 * alone when fadeAmount is 0 or less. Does not clip. */
// FUNCTION: XVT 0x40EAC0
void FlightSw_BlitSpriteRleImpl8bpp(uint8_t *rleData, int x, int y,
				    int transparentColorIndex, int mirror,
				    char isFaded, int16_t fadeAmount)
{
	unsigned int pixelOffset;
	uint8_t *destination;
	uint8_t token;
	uint8_t color;
	int16_t alternatingPixelsRemaining;
	uint16_t runLength;
	uint8_t *source;
	int mirrorFlag;

	source = rleData;
	mirrorFlag = mirror;
	g_flightSwRleSpriteX = x;
	g_flightSwRleSpriteY = y;
	g_flightSwRleTransparentColor = (uint8_t)transparentColorIndex;

	for (;;) {
		pixelOffset =
			(uint16_t)g_flightSwRleSpriteX +
			FlightSw_GetLineOffset((uint16_t)g_flightSwRleSpriteY);
#ifndef XVT_MODERN
		if (g_flightResolutionMode != FLIGHT_RESOLUTION_320X240 &&
		    g_flightSwFramebufferBase == g_swFramebufferBase) {
			unsigned int page;

			page = pixelOffset / g_vesaPageSizeBytes;
			pixelOffset %= g_vesaPageSizeBytes;
			RtsVga2_SetCurrentPage((uint8_t)g_vesaWindow,
					       (uint16_t)page);
		}
#endif
		destination = g_flightSwFramebufferBase + pixelOffset;

		for (;;) {
			token = *source++;
			if (token < 0xFB) {
				runLength = token & 3;
				color = token >> 2;
				if (isFaded == 0) {
					color += g_flightSwRlePaletteShift;
				}
			} else {
				if (token > 0xFB) {
					if (token == 0xFC) {
						/* In this two-color dither run, runLength holds the second color, written to every
						 * other pixel. */
						color = source[0];
						if (isFaded != 0) {
							if (fadeAmount > 0) {
								color -= (uint8_t)
									fadeAmount;
								color += (uint8_t)
									g_flightSwRlePaletteShift;
								runLength =
									color;
								++runLength;
							} else {
								color = (uint8_t)
									g_flightSwRlePaletteShift;
								runLength =
									color;
							}
						} else {
							runLength = color;
							++runLength;
						}
						alternatingPixelsRemaining =
							(int16_t)source[1] + 1;
						source += 2;
						while (alternatingPixelsRemaining >
						       0) {
							*destination = color;
							if (mirrorFlag == 0) {
								++destination;
							} else {
								--destination;
							}
							--alternatingPixelsRemaining;
							if (alternatingPixelsRemaining >
							    0) {
								*destination = (uint8_t)
									runLength;
								if (mirrorFlag ==
								    0) {
									++destination;
								} else {
									--destination;
								}
								--alternatingPixelsRemaining;
							}
						}
						continue;
					}
					if (token == 0xFD) {
						runLength = source[0];
						color = source[1];
						source += 2;
					} else {
						break;
					}
				} else {
					if (isFaded == 0) {
						g_flightSwRlePaletteShift =
							(int8_t)*source;
					}
					++source;
					continue;
				}
			}

			++runLength;
			if (color == transparentColorIndex) {
				if (mirrorFlag == 0) {
					destination += runLength;
				} else {
					destination -= runLength;
				}
				continue;
			}
			if (isFaded != 0) {
				if (fadeAmount > 0) {
					color -= (uint8_t)fadeAmount;
					color += (uint8_t)
						g_flightSwRlePaletteShift;
				} else {
					color = (uint8_t)
						g_flightSwRlePaletteShift;
				}
			}
			if (mirrorFlag == 0) {
				memset(destination, color, runLength);
				destination += runLength;
			} else {
				memset(destination - (runLength - 1), color,
				       runLength);
				destination -= runLength;
			}
		}

		if (token == 0xFF) {
			return;
		}
		++g_flightSwRleSpriteY;
	}
}

/* Draws a map icon in the RLE form FlightSw_BlitSpriteRleImpl8bpp reads, at 8
 * bits, or through FlightSw_BlitMapIconRle16bpp at 16 bits, without fading.
 * Each drawn color is 4 above the run's color: runs under 0xFB add
 * g_flightSwRlePaletteShift first, 0xFD and 0xFC runs do not, 0xFB sets it, and
 * transparentIndex is tested before the 4 is added (not for 0xFC). */
// FUNCTION: XVT 0x40ECE0
void FlightSw_BlitMapIconRle(uint8_t *rleData, int x, int y,
			     int transparentIndex, int mirror)
{
	struct {
		uint8_t value;
		uint8_t padding[3];
	} color;

	uint8_t *destination;
	unsigned int pixelOffset;
	uint16_t runLength;
	uint16_t nextColor;
	uint16_t *next;
	uint16_t *count;
#ifndef XVT_MODERN
	uint8_t **source;
	void *colorRef;
	uint8_t **cursor;
	int *mirrorRef;
	int *transparentRef;
#endif

	if (g_flightBytesPerPixel == 2) {
		FlightSw_BlitMapIconRle16bpp(rleData, x, y, transparentIndex,
					     mirror);
		return;
	}

	next = &nextColor;
	count = &runLength;
#ifndef XVT_MODERN
	source = &rleData;
	colorRef = &color;
	cursor = &destination;
	mirrorRef = &mirror;
	transparentRef = &transparentIndex;
#endif

	g_flightSwRleSpriteX = (int16_t)x;
	g_flightSwRleSpriteY = (int16_t)y;
	g_flightSwRleTransparentColor = (uint8_t)transparentIndex;

	for (;;) {
		pixelOffset =
			(uint16_t)g_flightSwRleSpriteX +
			FlightSw_GetLineOffset((uint16_t)g_flightSwRleSpriteY);
#ifndef XVT_MODERN
		if (g_flightResolutionMode != FLIGHT_RESOLUTION_320X240 &&
		    XvtFramebufferAddress_IsLegacyBase(
			    g_flightSwFramebufferBase)) {
			unsigned int page;

			page = pixelOffset / g_vesaPageSizeBytes;
			pixelOffset %= g_vesaPageSizeBytes;
			RtsVga2_SetCurrentPage((uint8_t)g_vesaWindow,
					       (uint16_t)page);
		}
#endif
		destination = g_flightSwFramebufferBase + pixelOffset;

		for (;;) {
			color.value = *rleData++;
			if (color.value < 0xFB) {
				*count = color.value;
				color.value >>= 2;
				*count &= 3;
				color.value +=
					(uint8_t)g_flightSwRlePaletteShift;
			} else {
				if (color.value > 0xFB) {
					if (color.value == 0xFC) {
						color.value = *rleData;
						*next = (uint8_t)color.value +
							1;
						++rleData;
						*count = *rleData + 1;
						++rleData;
						if ((int16_t)*count > 0) {
							uint8_t *pixel;

							color.value += 4;
							do {
								pixel = destination;
								*pixel =
									color.value;
								if (mirror ==
								    0) {
									destination =
										pixel +
										1;
								} else {
									destination =
										pixel -
										1;
								}
								--*count;
								if ((int16_t)*count >
								    0) {
									pixel = destination;
									*pixel =
										(uint8_t)(*next +
											  4);
									if (mirror ==
									    0) {
										destination =
											pixel +
											1;
									} else {
										destination =
											pixel -
											1;
									}
								}
								--*count;
							} while (
								(int16_t)*count >
								0);
						}
						continue;
					}
					if (color.value == 0xFD) {
						*count = *rleData++;
						color.value = *rleData++;
					} else {
						break;
					}
				} else {
					g_flightSwRlePaletteShift =
						(int8_t)*rleData++;
					continue;
				}
			}

			++*count;
			if (color.value == transparentIndex) {
				if (mirror == 0) {
					destination += *count;
				} else {
					destination -= *count;
				}
				continue;
			}
			if (mirror == 0) {
				if (*count > 0) {
					memset(destination,
					       (int8_t)(color.value + 4),
					       *count);
					destination += *count;
				}
			} else {
				if (*count > 0) {
					uint8_t *pixel;

					while (*count > 0) {
						pixel = destination;
						*pixel = (int8_t)(color.value +
								  4);
						destination = pixel - 1;
						--*count;
					}
				}
			}
		}

		if (color.value == 0xFF) {
			return;
		}
		++g_flightSwRleSpriteY;
	}
}

/* Writes colorIndex at x, y on the 8-bit frame buffer through the active row
 * table. Does not clip. FlightRender_InstallCallbacks stores it in
 * g_flightDrawPixelFn, which nothing calls. */
// FUNCTION: XVT 0x40EFE0
void FlightSw_DrawPixel8bpp(uint16_t x, uint16_t y, int8_t colorIndex)
{
	unsigned int pixelOffset;
#ifndef XVT_MODERN
	unsigned int page;
#endif

	pixelOffset = x + FlightSw_GetLineOffset(y);
#ifndef XVT_MODERN
	if (g_flightResolutionMode != FLIGHT_RESOLUTION_320X240 &&
	    g_flightSwFramebufferBase == g_swFramebufferBase) {
		page = pixelOffset / g_vesaPageSizeBytes;
		pixelOffset %= g_vesaPageSizeBytes;
		RtsVga2_SetCurrentPage((uint8_t)g_vesaWindow, (uint16_t)page);
	}
#endif
	g_flightSwFramebufferBase[pixelOffset] = colorIndex;
}

/* Fills the g_flightClip rectangle with g_flightTextBgColor at 8 bits, through
 * FlightSw_FillRectOrBorder8bpp. */
// FUNCTION: XVT 0x40F9F0
void FlightSw_FillClipRect8bpp(void)
{
	g_flightFillRectBottom8bpp = (uint16_t)g_flightClipBottom;
	g_flightFillRectTop8bpp = (uint16_t)g_flightClipTop;
	g_flightFillRectLeft8bpp = (uint16_t)g_flightClipLeft;
	g_flightFillRectRight8bpp = (uint16_t)g_flightClipRight;
	FlightSw_FillRectOrBorder8bpp(0);
}

/* Fills the 8-bit g_flightFillRect rectangle with g_flightTextBgColor or, with
 * borderThickness nonzero, only a frame that many pixels thick; does nothing
 * when it has no width. Steps g_flightFillRectCurrentY8bpp and
 * g_flightFillRectRemainingRows8bpp down the rows. Drawing to the legacy
 * 0xA0000 base outside 320x240, it works out a VESA page per row for
 * RtsVga2_SetCurrentPage, which does nothing. Does not clip. */
// FUNCTION: XVT 0x40FA30
void FlightSw_FillRectOrBorder8bpp(uint16_t borderThickness)
{
	int directFramebuffer;
	unsigned int pixelOffset;
	unsigned int borderRow;
	unsigned int bottomRow;

	g_flightFillRectCurrentY8bpp = g_flightFillRectTop8bpp;
	g_flightFillRectRemainingRows8bpp =
		g_flightFillRectBottom8bpp - g_flightFillRectTop8bpp;
	directFramebuffer = 0;
	if ((int16_t)(g_flightFillRectRight8bpp - g_flightFillRectLeft8bpp) <=
	    0) {
		return;
	}

	pixelOffset = FlightSw_GetLineOffset(g_flightFillRectCurrentY8bpp);
	if (g_flightResolutionMode != FLIGHT_RESOLUTION_320X240 &&
	    XvtFramebufferAddress_IsLegacyBase(g_flightSwFramebufferBase)) {
		unsigned int page;

		page = pixelOffset / g_vesaPageSizeBytes;
		pixelOffset %= g_vesaPageSizeBytes;
#ifdef XVT_MODERN
		RtsVga2_SetCurrentPage((uint8_t)g_flightResolutionMode, page);
#else
		RtsVga2_SetCurrentPage((uint8_t)g_vesaWindow, page);
#endif
		if (pixelOffset +
			    g_flightFillRectRemainingRows8bpp * g_surfacePitch >
		    0xFFFF) {
			directFramebuffer = 1;
		}
	}
	if (!XvtFramebufferAddress_IsLegacyBase(g_flightSwFramebufferBase)) {
		directFramebuffer = 1;
	}

	if (!directFramebuffer) {
		uint8_t *destination;

		if (borderThickness != 0) {
			destination = &g_flightSwFramebufferBase[pixelOffset];
			borderRow = 0;
			while (borderRow < borderThickness) {
				uint8_t *rowStart;
				int16_t width;

				rowStart =
					&destination[g_flightFillRectLeft8bpp];
				width = (int16_t)(g_flightFillRectRight8bpp -
						  g_flightFillRectLeft8bpp);
				if (width <= 0) {
					return;
				}
				while (width-- != 0) {
					*rowStart++ = g_flightTextBgColor;
				}
				++borderRow;
				destination += FlightSw_GetLinePitch();
				--g_flightFillRectRemainingRows8bpp;
				++g_flightFillRectCurrentY8bpp;
			}
			while (g_flightFillRectRemainingRows8bpp >
			       borderThickness) {
				uint8_t *rowStart;
				unsigned int count;
				int16_t width;

				rowStart =
					&destination[g_flightFillRectLeft8bpp];
				width = (int16_t)(g_flightFillRectRight8bpp -
						  g_flightFillRectLeft8bpp);
				count = borderThickness;
				while (count-- != 0) {
					*rowStart++ = g_flightTextBgColor;
				}
				rowStart +=
					(int16_t)(width - 2 * borderThickness);
				count = borderThickness;
				while (count-- != 0) {
					*rowStart++ = g_flightTextBgColor;
				}
				destination += FlightSw_GetLinePitch();
				--g_flightFillRectRemainingRows8bpp;
				++g_flightFillRectCurrentY8bpp;
			}
			bottomRow = 0;
			while (bottomRow < borderThickness) {
				uint8_t *rowStart;
				int16_t width;

				rowStart =
					&destination[g_flightFillRectLeft8bpp];
				width = (int16_t)(g_flightFillRectRight8bpp -
						  g_flightFillRectLeft8bpp);
				if (width <= 0) {
					return;
				}
				while (width-- != 0) {
					*rowStart++ = g_flightTextBgColor;
				}
				++bottomRow;
				destination += FlightSw_GetLinePitch();
				--g_flightFillRectRemainingRows8bpp;
				++g_flightFillRectCurrentY8bpp;
			}
		} else {
			destination = &g_flightSwFramebufferBase[pixelOffset];
			while (g_flightFillRectRemainingRows8bpp != 0) {
				uint8_t *rowStart;
				int16_t width;

				rowStart =
					&destination[g_flightFillRectLeft8bpp];
				width = (int16_t)(g_flightFillRectRight8bpp -
						  g_flightFillRectLeft8bpp);
				if (width <= 0) {
					return;
				}
				while (width-- != 0) {
					*rowStart++ = g_flightTextBgColor;
				}
				destination += FlightSw_GetLinePitch();
				--g_flightFillRectRemainingRows8bpp;
				++g_flightFillRectCurrentY8bpp;
			}
		}
	} else {
		if (borderThickness != 0) {
			borderRow = 0;
			while (borderRow < borderThickness) {
				uint8_t *destination;
				int16_t width;

				pixelOffset =
					g_flightFillRectLeft8bpp +
					FlightSw_GetLineOffset(
						g_flightFillRectCurrentY8bpp);
				if (g_flightResolutionMode !=
					    FLIGHT_RESOLUTION_320X240 &&
				    XvtFramebufferAddress_IsLegacyBase(
					    g_flightSwFramebufferBase)) {
					unsigned int page;

					page = pixelOffset /
					       g_vesaPageSizeBytes;
					pixelOffset %= g_vesaPageSizeBytes;
#ifdef XVT_MODERN
					RtsVga2_SetCurrentPage(
						(uint8_t)g_flightResolutionMode,
						page);
#else
					RtsVga2_SetCurrentPage(
						(uint8_t)g_vesaWindow, page);
#endif
				}
				width = (int16_t)(g_flightFillRectRight8bpp -
						  g_flightFillRectLeft8bpp);
				destination =
					&g_flightSwFramebufferBase[pixelOffset];
				if (width <= 0) {
					return;
				}
				while (width-- != 0) {
					*destination++ = g_flightTextBgColor;
				}
				++borderRow;
				--g_flightFillRectRemainingRows8bpp;
				++g_flightFillRectCurrentY8bpp;
			}
			while (g_flightFillRectRemainingRows8bpp >
			       borderThickness) {
				uint8_t *destination;
				unsigned int count;
				int16_t width;

				pixelOffset =
					g_flightFillRectLeft8bpp +
					FlightSw_GetLineOffset(
						g_flightFillRectCurrentY8bpp);
				if (g_flightResolutionMode !=
					    FLIGHT_RESOLUTION_320X240 &&
				    XvtFramebufferAddress_IsLegacyBase(
					    g_flightSwFramebufferBase)) {
					unsigned int page;

					page = pixelOffset /
					       g_vesaPageSizeBytes;
					pixelOffset %= g_vesaPageSizeBytes;
#ifdef XVT_MODERN
					RtsVga2_SetCurrentPage(
						(uint8_t)g_flightResolutionMode,
						page);
#else
					RtsVga2_SetCurrentPage(
						(uint8_t)g_vesaWindow, page);
#endif
				}
				width = (int16_t)(g_flightFillRectRight8bpp -
						  g_flightFillRectLeft8bpp);
				destination =
					&g_flightSwFramebufferBase[pixelOffset];
				count = borderThickness;
				while (count-- != 0) {
					*destination++ = g_flightTextBgColor;
				}
				destination +=
					(int16_t)(width - 2 * borderThickness);
				count = borderThickness;
				while (count-- != 0) {
					*destination++ = g_flightTextBgColor;
				}
				--g_flightFillRectRemainingRows8bpp;
				++g_flightFillRectCurrentY8bpp;
			}
			bottomRow = 0;
			while (bottomRow < borderThickness) {
				uint8_t *destination;
				int16_t width;

				pixelOffset =
					g_flightFillRectLeft8bpp +
					FlightSw_GetLineOffset(
						g_flightFillRectCurrentY8bpp);
				if (g_flightResolutionMode !=
					    FLIGHT_RESOLUTION_320X240 &&
				    XvtFramebufferAddress_IsLegacyBase(
					    g_flightSwFramebufferBase)) {
					unsigned int page;

					page = pixelOffset /
					       g_vesaPageSizeBytes;
					pixelOffset %= g_vesaPageSizeBytes;
#ifdef XVT_MODERN
					RtsVga2_SetCurrentPage(
						(uint8_t)g_flightResolutionMode,
						page);
#else
					RtsVga2_SetCurrentPage(
						(uint8_t)g_vesaWindow, page);
#endif
				}
				width = (int16_t)(g_flightFillRectRight8bpp -
						  g_flightFillRectLeft8bpp);
				destination =
					&g_flightSwFramebufferBase[pixelOffset];
				if (width <= 0) {
					return;
				}
				while (width-- != 0) {
					*destination++ = g_flightTextBgColor;
				}
				++bottomRow;
				--g_flightFillRectRemainingRows8bpp;
				++g_flightFillRectCurrentY8bpp;
			}
		} else {
			while (g_flightFillRectRemainingRows8bpp != 0) {
				uint8_t *destination;
				int16_t width;

				pixelOffset =
					g_flightFillRectLeft8bpp +
					FlightSw_GetLineOffset(
						g_flightFillRectCurrentY8bpp);
				if (g_flightResolutionMode !=
					    FLIGHT_RESOLUTION_320X240 &&
				    XvtFramebufferAddress_IsLegacyBase(
					    g_flightSwFramebufferBase)) {
					unsigned int page;

					page = pixelOffset /
					       g_vesaPageSizeBytes;
					pixelOffset %= g_vesaPageSizeBytes;
#ifdef XVT_MODERN
					RtsVga2_SetCurrentPage(
						(uint8_t)g_flightResolutionMode,
						page);
#else
					RtsVga2_SetCurrentPage(
						(uint8_t)g_vesaWindow, page);
#endif
				}
				width = (int16_t)(g_flightFillRectRight8bpp -
						  g_flightFillRectLeft8bpp);
				destination =
					&g_flightSwFramebufferBase[pixelOffset];
				if (width <= 0) {
					return;
				}
				while (width-- != 0) {
					*destination++ = g_flightTextBgColor;
				}
				--g_flightFillRectRemainingRows8bpp;
				++g_flightFillRectCurrentY8bpp;
			}
		}
	}
}

/* Clips the rectangle from x1, y1 to x2, y2 (ends not included) to the
 * g_flightClip rectangle, stores it in the 8-bit g_flightFillRect globals, and
 * fills it or its frame with FlightSw_FillRectOrBorder8bpp when anything is
 * left. */
// FUNCTION: XVT 0x410040
void FlightSw_FillRectClipped8bpp(uint16_t x1, uint16_t y1, uint16_t x2,
				  uint16_t y2, uint16_t borderThickness)
{
	g_flightFillRectLeft8bpp = x1;
	g_flightFillRectRight8bpp = x2;
	g_flightFillRectTop8bpp = y1;
	g_flightFillRectBottom8bpp = y2;
	if (g_flightClipLeft > (int)x1) {
		g_flightFillRectLeft8bpp = (uint16_t)g_flightClipLeft;
	}
	if (g_flightClipRight < (int)x2) {
		g_flightFillRectRight8bpp = (uint16_t)g_flightClipRight;
	}
	if (g_flightClipTop > (int)y1) {
		g_flightFillRectTop8bpp = (uint16_t)g_flightClipTop;
	}
	if (g_flightClipBottom < (int)y2) {
		g_flightFillRectBottom8bpp = (uint16_t)g_flightClipBottom;
	}
	if (g_flightFillRectTop8bpp < g_flightFillRectBottom8bpp &&
	    g_flightFillRectRight8bpp > g_flightFillRectLeft8bpp) {
		FlightSw_FillRectOrBorder8bpp(borderThickness);
	}
}

/* Copies height rows of width pixels from x, y on the 8-bit frame buffer into
 * buffer, row after row. Does not clip. */
// FUNCTION: XVT 0x410230
void FlightSw_SaveScreenRect8bpp(uint8_t *buffer, int x, int y, int16_t width,
				 int height)
{
	unsigned int pixelOffset;
	uint8_t *source;
	int rowsRemaining;
	uint8_t pixel;
#ifndef XVT_MODERN
	unsigned int page;
#endif

	rowsRemaining = height;
	if (rowsRemaining == 0) {
		return;
	}
	do {
		pixelOffset = FlightSw_GetLineOffset(y) + x;
#ifndef XVT_MODERN
		if (g_flightResolutionMode != FLIGHT_RESOLUTION_320X240 &&
		    g_swFramebufferBase == g_flightSwFramebufferBase) {
			page = pixelOffset / g_vesaPageSizeBytes;
			pixelOffset %= g_vesaPageSizeBytes;
			RtsVga2_SetCurrentPage((uint8_t)g_vesaWindow,
					       (uint16_t)page);
			RtsVga2_SetCurrentPage(1, (uint16_t)page);
		}
#endif
		g_savedRowPixelsRemaining = width;
		source = g_flightSwFramebufferBase + pixelOffset;
		while (g_savedRowPixelsRemaining > 0) {
			pixel = *source++;
			*buffer++ = pixel;
			--g_savedRowPixelsRemaining;
		}
		--rowsRemaining;
		++y;
	} while (rowsRemaining != 0);
}

/* Writes height rows of width pixels from buffer back at x, y on the 8-bit
 * frame buffer. Does not clip. */
// FUNCTION: XVT 0x4102F0
void FlightSw_RestoreScreenRect8bpp(uint8_t *buffer, int x, int y,
				    int16_t width, int height)
{
	int rowsRemaining;
	uint8_t *destination;
	unsigned int pixelOffset;
#ifndef XVT_MODERN
	unsigned int page;
#endif

	rowsRemaining = height;
	for (; rowsRemaining != 0; ++y) {
		pixelOffset = FlightSw_GetLineOffset(y) + x;
#ifndef XVT_MODERN
		if (g_flightResolutionMode != FLIGHT_RESOLUTION_320X240 &&
		    g_swFramebufferBase == g_flightSwFramebufferBase) {
			page = pixelOffset / g_vesaPageSizeBytes;
			pixelOffset %= g_vesaPageSizeBytes;
			RtsVga2_SetCurrentPage((uint8_t)g_vesaWindow,
					       (uint16_t)page);
		}
#endif
		g_savedRowPixelsRemaining = width;
		destination = g_flightSwFramebufferBase;
		destination += pixelOffset;
		while (g_savedRowPixelsRemaining > 0) {
			*destination++ = *buffer++;
			--g_savedRowPixelsRemaining;
		}
		--rowsRemaining;
	}
}

/* Draws count points, each three 16-bit words: x, y, and a color in the low
 * byte of the third. A pixel is drawn only where the frame buffer holds palette
 * index 44. The third word then becomes a mask: 1 when the pixel was drawn,
 * else 0, and at 640x480 the pixel below gets the same test and adds the 0x2
 * bit. Does not clip. */
// FUNCTION: XVT 0x4103A0
void FlightSw_DrawPointArray8bpp(uint16_t *points, int16_t count)
{
	struct FlightSwPointRecord {
		uint16_t x;
		uint16_t y;
		uint8_t drawnMask;
		uint8_t payloadHigh;
	};

	unsigned int pixelOffset;
	uint8_t *destination;
	uint8_t color;
#ifndef XVT_MODERN
	unsigned int page;
#endif

	if (count == 0) {
		return;
	}
	do {
		pixelOffset = points[0];
		color = (uint8_t)points[2];
		pixelOffset += FlightSw_GetLineOffset(points[1]);
#ifndef XVT_MODERN
		if (g_flightResolutionMode != FLIGHT_RESOLUTION_320X240 &&
		    g_flightSwFramebufferBase == g_swFramebufferBase) {
			page = pixelOffset / g_vesaPageSizeBytes;
			pixelOffset %= g_vesaPageSizeBytes;
			RtsVga2_SetCurrentPage((uint8_t)g_vesaWindow,
					       (uint16_t)page);
			RtsVga2_SetCurrentPage(1, (uint16_t)page);
		}
#endif
		destination = g_flightSwFramebufferBase + pixelOffset;
		if (*destination != 44) {
			points[2] = 0;
		} else {
			*destination = color;
			points[2] = 1;
		}

		if (g_flightResolutionMode == FLIGHT_RESOLUTION_640X480) {
			pixelOffset = points[0];
			pixelOffset += FlightSw_GetLineOffset(
				(uint16_t)(points[1] + 1));
#ifndef XVT_MODERN
			if (g_flightResolutionMode !=
				    FLIGHT_RESOLUTION_320X240 &&
			    g_flightSwFramebufferBase == g_swFramebufferBase) {
				page = pixelOffset / g_vesaPageSizeBytes;
				pixelOffset %= g_vesaPageSizeBytes;
				RtsVga2_SetCurrentPage((uint8_t)g_vesaWindow,
						       (uint16_t)page);
				RtsVga2_SetCurrentPage(1, (uint16_t)page);
			}
#endif
			destination = g_flightSwFramebufferBase;
			destination += pixelOffset;
			if (*destination != 44) {
				points[2] &= 1;
			} else {
				*destination = color;
				((struct FlightSwPointRecord *)points)
					->drawnMask |= 2;
			}
		}
		--count;
		points += 3;
	} while (count != 0);
}

/* Undoes FlightSw_DrawPointArray8bpp: writes palette index 44 back at each
 * point whose mask has the 0x1 bit and, at 640x480, below it where the mask has
 * the 0x2 bit. */
// FUNCTION: XVT 0x4104D0
void FlightSw_ErasePointArray8bpp(uint16_t *points, int16_t count)
{
	uint16_t *current;
	int16_t remaining;
	unsigned int pixelOffset;
	uint8_t *destination;
	unsigned int x;
	unsigned int y;
	uint8_t mask;
#ifndef XVT_MODERN
	unsigned int page;
#endif

	remaining = count;
	if (remaining == 0) {
		return;
	}
	current = points;
	do {
		x = current[0];
		y = current[1];
		pixelOffset = FlightSw_GetLineOffset(y) + x;
#ifndef XVT_MODERN
		if (g_flightResolutionMode != FLIGHT_RESOLUTION_320X240 &&
		    g_flightSwFramebufferBase == g_swFramebufferBase) {
			page = pixelOffset / g_vesaPageSizeBytes;
			pixelOffset %= g_vesaPageSizeBytes;
			RtsVga2_SetCurrentPage((uint8_t)g_vesaWindow,
					       (uint16_t)page);
		}
#endif
		destination = g_flightSwFramebufferBase + pixelOffset;
		mask = (uint8_t)current[2];
		if ((mask & 1) != 0) {
			*destination = 44;
		}
		if (g_flightResolutionMode == FLIGHT_RESOLUTION_640X480) {
			x = current[0];
			y = (uint16_t)(current[1] + 1);
			pixelOffset = FlightSw_GetLineOffset(y) + x;
#ifndef XVT_MODERN
			if (g_flightResolutionMode !=
				    FLIGHT_RESOLUTION_320X240 &&
			    g_flightSwFramebufferBase == g_swFramebufferBase) {
				page = pixelOffset / g_vesaPageSizeBytes;
				pixelOffset %= g_vesaPageSizeBytes;
				RtsVga2_SetCurrentPage((uint8_t)g_vesaWindow,
						       (uint16_t)page);
			}
#endif
			destination = g_flightSwFramebufferBase + pixelOffset;
			mask = (uint8_t)current[2];
			if ((mask & 2) != 0) {
				*destination = 44;
			}
		}
		--remaining;
		current += 3;
	} while (remaining != 0);
}

/* Draws the radar target marker at g_radarTargetMarkerDrawX and
 * g_radarTargetMarkerDrawY in palette index 206: g_radarTargetMarkerPointCount
 * pixels of g_radarTargetMarkerShape, saving the pixels under them in
 * g_radarTargetMarkerSavedPixels. Does not clip. */
// FUNCTION: XVT 0x4105E0
void FlightSw_DrawRadarTargetMarker8bpp(void)
{
	unsigned int pixelOffset;
	uint16_t offsetIndex;
	uint16_t savedPixelIndex;
	int16_t remaining;
	int8_t *offset;
	uint8_t *pixel;
#ifndef XVT_MODERN
	unsigned int page;
#endif

	savedPixelIndex = 0;
	offsetIndex = 0;
	remaining = (int16_t)g_radarTargetMarkerPointCount;
	if (remaining == (int16_t)savedPixelIndex) {
		return;
	}
	do {
		offset = &g_radarTargetMarkerShape[offsetIndex];
		pixelOffset = FlightSw_GetLineOffset(g_radarTargetMarkerDrawY +
						     offset[1]) +
			      offset[0] + g_radarTargetMarkerDrawX;
#ifndef XVT_MODERN
		if (g_flightResolutionMode != FLIGHT_RESOLUTION_320X240 &&
		    g_flightSwFramebufferBase == g_swFramebufferBase) {
			page = pixelOffset / g_vesaPageSizeBytes;
			pixelOffset %= g_vesaPageSizeBytes;
			RtsVga2_SetCurrentPage((uint8_t)g_vesaWindow,
					       (uint16_t)page);
			RtsVga2_SetCurrentPage(1, (uint16_t)page);
		}
#endif
		offsetIndex += 2;
		pixel = g_flightSwFramebufferBase + pixelOffset;
		g_radarTargetMarkerSavedPixels[savedPixelIndex++] = *pixel;
		*pixel = 206;
		--remaining;
	} while (remaining != 0);
}

/* Puts back the pixels FlightSw_DrawRadarTargetMarker8bpp saved, at
 * g_radarTargetMarkerRestoreX and g_radarTargetMarkerRestoreY. */
// FUNCTION: XVT 0x4106C0
void FlightSw_RestoreRadarTargetMarker8bpp(void)
{
	unsigned int pixelOffset;
	uint16_t offsetIndex;
	uint16_t savedPixelIndex;
	int16_t remaining;
	int8_t *offset;
	uint8_t pixel;
	uint8_t *framebufferBase;
#ifndef XVT_MODERN
	unsigned int page;
#endif

	offsetIndex = 0;
	savedPixelIndex = 0;
	remaining = (int16_t)g_radarTargetMarkerPointCount;
	if (remaining == (int16_t)savedPixelIndex) {
		return;
	}
	do {
		offset = g_radarTargetMarkerShape + offsetIndex;
		pixelOffset = offset[0] +
			      FlightSw_GetLineOffset(
				      g_radarTargetMarkerRestoreY + offset[1]) +
			      g_radarTargetMarkerRestoreX;
#ifndef XVT_MODERN
		if (g_flightResolutionMode != FLIGHT_RESOLUTION_320X240 &&
		    g_swFramebufferBase == g_flightSwFramebufferBase) {
			page = pixelOffset / g_vesaPageSizeBytes;
			pixelOffset %= g_vesaPageSizeBytes;
			RtsVga2_SetCurrentPage((uint8_t)g_vesaWindow,
					       (uint16_t)page);
		}
#endif
		offsetIndex += 2;
		framebufferBase = g_flightSwFramebufferBase;
		pixel = g_radarTargetMarkerSavedPixels[savedPixelIndex++];
		framebufferBase[pixelOffset] = pixel;
		--remaining;
	} while (remaining != 0);
}

/* Draws the 7-pixel cross marker centered on x, y in color, saving the pixels
 * under it; returns color. Nothing calls this. */
// FUNCTION: XVT 0x410780
uint8_t FlightSw_DrawCrossMarker8bpp(uint16_t x, uint16_t y, uint8_t color)
{
	unsigned int pixelOffset;
	uint16_t offsetIndex;
	uint16_t savedPixelIndex;
	int16_t remaining;
	unsigned int coordinates[2];
	uint8_t *pixel;
#ifndef XVT_MODERN
	unsigned int page;
#endif

	remaining = 7;
	coordinates[0] = y;
	offsetIndex = 0;
	coordinates[1] = x;
	savedPixelIndex = 0;
	do {
		pixelOffset =
			FlightSw_GetLineOffset(
				coordinates[0] +
				((int8_t *)g_flightSwCrossMarkerOffsets)
					[offsetIndex + 1]) +
			((int8_t *)g_flightSwCrossMarkerOffsets)[offsetIndex] +
			coordinates[1];
#ifndef XVT_MODERN
		if (g_flightResolutionMode != FLIGHT_RESOLUTION_320X240 &&
		    g_flightSwFramebufferBase == g_swFramebufferBase) {
			page = pixelOffset / g_vesaPageSizeBytes;
			pixelOffset %= g_vesaPageSizeBytes;
			RtsVga2_SetCurrentPage((uint8_t)g_vesaWindow,
					       (uint16_t)page);
			RtsVga2_SetCurrentPage(1, (uint16_t)page);
		}
#endif
		offsetIndex += 2;
		pixel = g_flightSwFramebufferBase + pixelOffset;
		g_flightSwCrossMarkerSavedPixels[savedPixelIndex++] = *pixel;
		*pixel = color;
		--remaining;
	} while (remaining != 0);

	return color;
}

/* Puts back the 7 pixels saved around x, y and returns the last of them.
 * Nothing calls this. */
// FUNCTION: XVT 0x410860
uint8_t FlightSw_RestoreCrossMarker8bpp(uint16_t x, uint16_t y)
{
	int16_t remaining = 7;
	uint16_t savedPixelIndex = 0;
	uint16_t offsetIndex = 0;
	unsigned int pixelOffset;
	uint8_t pixel;
	uint8_t *framebufferBase;
#ifndef XVT_MODERN
	unsigned int page;
#endif

	do {
		pixelOffset =
			((int8_t *)g_flightSwCrossMarkerOffsets)[offsetIndex] +
			FlightSw_GetLineOffset(
				y + ((int8_t *)g_flightSwCrossMarkerOffsets)
					    [offsetIndex + 1]) +
			x;
#ifndef XVT_MODERN
		if (g_flightResolutionMode != FLIGHT_RESOLUTION_320X240 &&
		    g_flightSwFramebufferBase == g_swFramebufferBase) {
			page = pixelOffset / g_vesaPageSizeBytes;
			pixelOffset %= g_vesaPageSizeBytes;
			RtsVga2_SetCurrentPage((uint8_t)g_vesaWindow,
					       (uint16_t)page);
		}
#endif
		offsetIndex += 2;
		framebufferBase = g_flightSwFramebufferBase;
		pixel = g_flightSwCrossMarkerSavedPixels[savedPixelIndex++];
		framebufferBase[pixelOffset] = pixel;
		--remaining;
	} while (remaining != 0);
	return pixel;
}

/* Draws the background stars. Three grids of g_starfieldGridDimension by
 * g_starfieldGridDimension points lie on three faces of a cube around the eye,
 * turned by the camera matrix, each point moved by its star's jitter vector; a
 * point behind the eye is mirrored through it, so the opposite faces show too.
 * A point where |x| and |y| are under z that projects inside the viewport is
 * drawn in its star's color, but only over a pixel of the sky color
 * (g_flightBackgroundColorIndex, or its 16-bit color). On first use it makes
 * the star colors (grays: the palette entry from 0x40 on nearest gray 8 to 23,
 * or that level in each channel of a 16-bit color) and each star's random
 * jitter vector, 0 to 124. Calls FeDiskIo_FatalError(0) when an allocation
 * fails. */
// FUNCTION: XVT 0x410930
void FlightStarfield_Render(void)
{
	enum {
		STAR_COUNT = 3072,
		STAR_GRID_SPAN = 32,
		STAR_JITTER_COUNT = 125,
		STAR_PLANE_COUNT = 3,
	};

	int rowIndex;
	int planeIndex;
	uint8_t *colors8;
	uint16_t *colors16;
	uint8_t *randomVectorIndices;
	float stepViewZ[3];
	float stepViewY[3];
	float initialView[3];
	float stepViewX[3];
	int columnIndex;
	float baseView[3];
	int screenX;
	int screenY;
	int rowAxis;
	int columnAxis;
	int starIndex;
	uint8_t targetRgb[3];
	uint16_t backgroundColor16;

	g_starfieldGridDimension = STAR_GRID_SPAN / g_starGridDivisor;
	if (g_flightBytesPerPixel == 2) {
		backgroundColor16 =
			g_flightPalette16Bpp[g_flightBackgroundColorIndex];
		if (!g_starfieldColors16Initialized) {
			int colorIndex;
			g_starfieldColors16Handle = Memory_AllocHandle(
				STAR_COUNT * sizeof(uint16_t), 0);
			if (g_starfieldColors16Handle == 0) {
				FeDiskIo_FatalError(0);
			}
			colors16 = (uint16_t *)Memory_GetHandleBlock(
				g_starfieldColors16Handle);
			if (Display_IsPixelFormat555()) {
				for (colorIndex = 0; colorIndex < STAR_COUNT;
				     ++colorIndex) {
					int shade = (GameRand() & 0xF) + 8;
					colors16[colorIndex] =
						(uint16_t)(0x421 * shade);
				}
			} else {
				for (colorIndex = 0; colorIndex < STAR_COUNT;
				     ++colorIndex) {
					int shade = (GameRand() & 0xF) + 8;
					colors16[colorIndex] =
						(uint16_t)(0x841 * shade);
				}
			}
			Memory_HandleBlockDoneStub(g_starfieldColors16Handle);
			g_starfieldColors16Initialized = 1;
		}
		colors16 = (uint16_t *)Memory_GetHandleBlock(
			g_starfieldColors16Handle);
	} else {
		if (!g_starfieldColors8Initialized) {
			int colorIndex;
			g_starfieldColors8Handle =
				Memory_AllocHandle(STAR_COUNT, 0);
			if (g_starfieldColors8Handle == 0) {
				FeDiskIo_FatalError(0);
			}
			colors8 = (uint8_t *)Memory_GetHandleBlock(
				g_starfieldColors8Handle);
			for (colorIndex = 0; colorIndex < STAR_COUNT;
			     ++colorIndex) {
				int shade = (GameRand() & 0xF) + 8;
				targetRgb[0] = (uint8_t)shade;
				targetRgb[1] = (uint8_t)shade;
				targetRgb[2] = (uint8_t)shade;
				colors8[colorIndex] = (uint8_t)
					Color_FindNearestRgbTripletIndex(
						targetRgb,
						(const uint8_t *)g_swPalette,
						0x40, 0x100);
			}
			Memory_HandleBlockDoneStub(g_starfieldColors8Handle);
			g_starfieldColors8Initialized = 1;
		}
		colors8 = (uint8_t *)Memory_GetHandleBlock(
			g_starfieldColors8Handle);
	}
	if (!g_starfieldRandomVectorIndicesInitialized) {
		int vectorIndex;
		g_starfieldRandomVectorIndicesHandle =
			Memory_AllocHandle(STAR_COUNT * STAR_PLANE_COUNT, 0);
		if (g_starfieldRandomVectorIndicesHandle == 0) {
			FeDiskIo_FatalError(0);
		}
		randomVectorIndices = (uint8_t *)Memory_GetHandleBlock(
			g_starfieldRandomVectorIndicesHandle);
		for (vectorIndex = 0; vectorIndex < STAR_COUNT; ++vectorIndex) {
			int randomIndex;
			do {
				randomIndex = GameRand() & 0x7F;
			} while (randomIndex > STAR_JITTER_COUNT - 1);
			randomVectorIndices[vectorIndex] = (uint8_t)randomIndex;
		}
		Memory_HandleBlockDoneStub(
			g_starfieldRandomVectorIndicesHandle);
		g_starfieldRandomVectorIndicesInitialized = 1;
	}
	randomVectorIndices = (uint8_t *)Memory_GetHandleBlock(
		g_starfieldRandomVectorIndicesHandle);

	initialView[0] =
		(float)(-(g_camMatR0_X + g_camMatR0_Y + g_camMatR0_Z) >> 2);
	initialView[1] =
		(float)(-(g_camMatR1_X + g_camMatR1_Y + g_camMatR1_Z) >> 2);
	initialView[2] =
		(float)(-(g_camMatR2_X + g_camMatR2_Y + g_camMatR2_Z) >> 2);
	baseView[0] = initialView[0];
	baseView[1] = initialView[1];
	baseView[2] = initialView[2];
	stepViewX[0] = (float)(g_camMatR0_X >> 1) *
		       g_sw3dSpanLengthReciprocal[g_starfieldGridDimension];
	stepViewY[0] = (float)(g_camMatR1_X >> 1) *
		       g_sw3dSpanLengthReciprocal[g_starfieldGridDimension];
	stepViewZ[0] = (float)(g_camMatR2_X >> 1) *
		       g_sw3dSpanLengthReciprocal[g_starfieldGridDimension];
	stepViewX[1] = (float)(g_camMatR0_Y >> 1) *
		       g_sw3dSpanLengthReciprocal[g_starfieldGridDimension];
	stepViewY[1] = (float)(g_camMatR1_Y >> 1) *
		       g_sw3dSpanLengthReciprocal[g_starfieldGridDimension];
	stepViewZ[1] = (float)(g_camMatR2_Y >> 1) *
		       g_sw3dSpanLengthReciprocal[g_starfieldGridDimension];
	stepViewX[2] = (float)(g_camMatR0_Z >> 1) *
		       g_sw3dSpanLengthReciprocal[g_starfieldGridDimension];
	stepViewY[2] = (float)(g_camMatR1_Z >> 1) *
		       g_sw3dSpanLengthReciprocal[g_starfieldGridDimension];
	stepViewZ[2] = (float)(g_camMatR2_Z >> 1) *
		       g_sw3dSpanLengthReciprocal[g_starfieldGridDimension];
	starIndex = 0;
	columnAxis = 0;
	rowAxis = 1;
	for (planeIndex = 0; planeIndex < STAR_PLANE_COUNT; ++planeIndex) {
		for (rowIndex = 0; rowIndex < g_starfieldGridDimension;
		     ++rowIndex) {
			float currentViewZ = baseView[2];
			float currentViewY = baseView[1];
			float currentViewX = baseView[0];
			for (columnIndex = 0;
			     columnIndex < g_starfieldGridDimension;
			     ++columnIndex) {
				uint8_t jitterIndex =
					randomVectorIndices[starIndex];
				float viewX = currentViewX +
					      g_starfieldJitterX[jitterIndex];
				float viewY = currentViewY +
					      g_starfieldJitterY[jitterIndex];
				float viewZ = currentViewZ +
					      g_starfieldJitterZ[jitterIndex];
				float absView;
				if (viewZ < 0.0f) {
					viewX = -viewX;
					viewY = -viewY;
					viewZ = -viewZ;
				}
				if (viewX < 0.0f) {
					absView = -viewX;
				} else {
					absView = viewX;
				}
				if (absView < viewZ) {
					if (viewY < 0.0f) {
						absView = -viewY;
					} else {
						absView = viewY;
					}
					if (absView < viewZ) {
						float projectionScale =
							g_projScaleInt / viewZ;
						screenX =
							g_flightVpCenterX +
							(int)(viewX *
							      projectionScale);
						screenY =
							g_flightVpCenterY +
							(int)(viewY *
							      projectionScale) +
							g_projOffsetY;
						if (screenX >= 0 &&
						    screenX < g_flightVpWidth &&
						    screenY >= 0 &&
						    screenY <
							    g_flightVpHeight) {
							if (g_flightBytesPerPixel ==
							    2) {
								uint16_t *pixel =
									(uint16_t
										 *)(g_flightSwFramebufferBase +
										    g_surfacePitch *
											    (g_flightVpY +
											     screenY)) +
									(g_flightVpX +
									 screenX);
								if (*pixel ==
								    backgroundColor16) {
									*pixel = colors16
										[starIndex];
								}
							} else {
								uint8_t *pixel =
									g_flightSwFramebufferBase +
									g_surfacePitch *
										(g_flightVpY +
										 screenY) +
									g_flightVpX +
									screenX;
								if (*pixel ==
								    g_flightBackgroundColorIndex) {
									*pixel = colors8
										[starIndex];
								}
							}
						}
					}
				}
				currentViewZ += stepViewZ[columnAxis];
				currentViewY += stepViewY[columnAxis];
				currentViewX += stepViewX[columnAxis];
				++starIndex;
			}
			baseView[0] += stepViewX[rowAxis];
			baseView[1] += stepViewY[rowAxis];
			baseView[2] += stepViewZ[rowAxis];
		}
		baseView[0] = initialView[0];
		baseView[1] = initialView[1];
		baseView[2] = initialView[2];
		if (planeIndex == 0) {
			++rowAxis;
		}
		if (planeIndex == 1) {
			++columnAxis;
		}
	}
	Memory_HandleBlockDoneStub(g_starfieldRandomVectorIndicesHandle);
	if (g_flightBytesPerPixel == 2) {
		Memory_HandleBlockDoneStub(g_starfieldColors16Handle);
	} else {
		Memory_HandleBlockDoneStub(g_starfieldColors8Handle);
	}
}

/* Does nothing in either build. */
// FUNCTION: XVT 0x410FF0
void RtsVga2_SetCurrentPage(uint8_t window, uint16_t page)
{
	(void)window;
	(void)page;
}

/* Saves the screen as flightscreenN.bmp, N the first number with no such file
 * (in the modern build, in the user folder). Builds a BMP palette from
 * g_swPalette with each channel shifted up 2 bits, releases every lock on the
 * flight surface, flips, locks with g_flightDrawToHudLayer at 0, writes the
 * surface with FrontImage_SaveBmpFile at 8 or 16 bits, unlocks, puts
 * g_flightDrawToHudLayer back and takes the locks again. */
// FUNCTION: XVT 0x411010
void FlightScreenshot_Capture(void)
{
	char fileName[64];
	uint8_t palette[1024];
	int fileIndex;
	int lockCount;
	int remainingLocks;
	int savedLockBackBufferForHudDraw;
	int i;

	fileIndex = 0;
	for (;;) {
		sprintf(fileName, "flightscreen%d.bmp", fileIndex);
#ifdef XVT_MODERN
		g_stream = XvtStorage_OpenRoot(AERON_VFS_ROOT_USER, fileName,
					       g_fileModeReadBinary);
#else
		FeDiskIo_OpenGlobalStream(fileName, g_fileModeReadBinary, 0, 1);
#endif
		if (g_stream == NULL) {
			break;
		}
#ifdef XVT_MODERN
		File_Close(g_stream);
#else
		File_RawClose((XvtFile *)g_stream);
#endif
		++fileIndex;
	}

	for (i = 0; i < 256; ++i) {
		palette[4 * i + 0] = (uint8_t)(g_swPalette[i].b << 2);
		palette[4 * i + 1] = (uint8_t)(g_swPalette[i].g << 2);
		palette[4 * i + 2] = (uint8_t)(g_swPalette[i].r << 2);
		palette[4 * i + 3] = 0;
	}

	lockCount = FlightSurface_GetLockCount();
	if (lockCount > 0) {
		remainingLocks = lockCount;
		do {
			FlightSurface_Unlock();
			--remainingLocks;
		} while (remainingLocks != 0);
	}

	FlightDisplay_Flip();
	savedLockBackBufferForHudDraw = g_flightDrawToHudLayer;
	g_flightDrawToHudLayer = 0;
	FlightSurface_Lock();
	FrontImage_SaveBmpFile(fileName, g_surfacePixels, g_surfaceWidth,
			       g_surfaceHeight, g_surfacePitch,
			       8 * g_flightBytesPerPixel,
			       Display_IsPixelFormat555(), palette);
	FlightSurface_Unlock();
	g_flightDrawToHudLayer = savedLockBackBufferForHudDraw;

	if (lockCount > 0) {
		do {
			FlightSurface_Lock();
			--lockCount;
		} while (lockCount != 0);
	}
}

/* Draws a line from x1, y1 to x2, y2 in colorIdx on the 8-bit frame buffer,
 * clipped to the g_flightClip rectangle (right and bottom edges excluded),
 * stepping along the longer axis with a running error. Every line stops one
 * pixel short of its far end on the axis it steps along. Rows are
 * g_surfacePitch apart from g_flightSwFramebufferBase, not taken from the row
 * table. */
// FUNCTION: XVT 0x411120
void FlightSw_DrawLine8bpp(int x1, int y1, int x2, int y2, uint8_t colorIdx)
{
	int deltaX;
	int deltaY;
	int startY;
	int endX;
	int endY;
	uint8_t *pixel;

	endX = x2;
	deltaX = endX - x1;
	if (deltaX < 0) {
		int swapX;

		swapX = x1;
		startY = y2;
		endY = y1;
		deltaX = -deltaX;
		x1 = endX;
		endX = swapX;
	} else {
		if (deltaX == 0) {
			int count;

			if (x1 < g_flightClipLeft) {
				return;
			}
			if (x1 >= g_flightClipRight) {
				return;
			}

			if (y2 < y1) {
				int swapY;

				swapY = y1;
				y1 = y2;
				y2 = swapY;
			}
			if (y1 < g_flightClipTop) {
				y1 = g_flightClipTop;
			}
			if (y2 >= g_flightClipBottom) {
				y2 = g_flightClipBottom - 1;
			}

			count = y2 - y1;
			if (count > 0) {
				pixel = &g_flightSwFramebufferBase
						[y1 * g_surfacePitch + x1];
				while (count-- != 0) {
					*pixel = colorIdx;
					pixel += g_surfacePitch;
				}
			}
			return;
		}
		startY = y1;
		endY = y2;
	}

	if (x1 < g_flightClipRight) {
		if (endX >= g_flightClipLeft) {
			deltaY = endY - startY;
			if (deltaY < 0) {
				deltaY = -deltaY;
				if (startY < g_flightClipTop) {
					return;
				}
				if (endY >= g_flightClipBottom) {
					return;
				}

				if (startY >= g_flightClipBottom) {
					int advance;

					advance = MATH2_ABoverC32(
						startY - g_flightClipBottom + 1,
						deltaX, deltaY);
					x1 += advance;
					if (x1 >= g_flightClipRight) {
						return;
					}
					startY = g_flightClipBottom - 1;
				}
				if (x1 < g_flightClipLeft) {
					int advance;
					int clippedXDistance;

					clippedXDistance =
						g_flightClipLeft - x1;
					advance = MATH2_ABoverC32(
						clippedXDistance, deltaY,
						deltaX);
					startY -= advance;
					if (startY < g_flightClipTop) {
						return;
					}
					x1 = g_flightClipLeft;
				}
				if (endX >= g_flightClipRight) {
					endX = g_flightClipRight - 1;
				}
				if (endY < g_flightClipTop) {
					endY = g_flightClipTop;
				}

				pixel = &g_flightSwFramebufferBase
						[startY * g_surfacePitch + x1];
				if (deltaX >= deltaY) {
					int error;
					int ySteps;
					int xCount;

					error = deltaX >> 1;
					xCount = endX - x1;
					ySteps = startY - endY + 1;
					while (xCount-- != 0) {
						*pixel++ = colorIdx;
						error -= deltaY;
						if (error < 0) {
							error += deltaX;
							--ySteps;
							if (ySteps == 0) {
								return;
							}
							pixel -= g_surfacePitch;
						}
					}
				} else {
					int error;
					int xSteps;
					int yCount;

					error = deltaY >> 1;
					xSteps = endX - x1 + 1;
					yCount = startY - endY;
					while (yCount-- != 0) {
						*pixel = colorIdx;
						pixel -= g_surfacePitch;
						error -= deltaX;
						if (error < 0) {
							error += deltaY;
							--xSteps;
							if (xSteps == 0) {
								return;
							}
							++pixel;
						}
					}
				}
			} else if (deltaY > 0) {
				if (startY >= g_flightClipBottom) {
					return;
				}
				if (endY < g_flightClipTop) {
					return;
				}

				if (startY < g_flightClipTop) {
					int advance;
					int clippedYDistance;

					clippedYDistance =
						g_flightClipTop - startY;
					advance = MATH2_ABoverC32(
						clippedYDistance, deltaX,
						deltaY);
					x1 += advance;
					if (x1 >= g_flightClipRight) {
						return;
					}
					startY = g_flightClipTop;
				}
				if (x1 < g_flightClipLeft) {
					int advance;
					int clippedXDistance;

					clippedXDistance =
						g_flightClipLeft - x1;
					advance = MATH2_ABoverC32(
						clippedXDistance, deltaY,
						deltaX);
					startY += advance;
					if (startY >= g_flightClipBottom) {
						return;
					}
					x1 = g_flightClipLeft;
				}
				if (endX >= g_flightClipRight) {
					endX = g_flightClipRight - 1;
				}
				if (endY >= g_flightClipBottom) {
					endY = g_flightClipBottom - 1;
				}

				pixel = &g_flightSwFramebufferBase
						[startY * g_surfacePitch + x1];
				if (deltaX >= deltaY) {
					int error;
					int ySteps;
					int xCount;

					error = deltaX >> 1;
					xCount = endX - x1;
					ySteps = endY - startY + 1;
					while (xCount-- != 0) {
						*pixel++ = colorIdx;
						error -= deltaY;
						if (error < 0) {
							error += deltaX;
							--ySteps;
							if (ySteps == 0) {
								return;
							}
							pixel += g_surfacePitch;
						}
					}
				} else {
					int error;
					int xSteps;
					int yCount;

					error = deltaY >> 1;
					xSteps = endX - x1 + 1;
					yCount = endY - startY;
					while (yCount-- != 0) {
						*pixel = colorIdx;
						pixel += g_surfacePitch;
						error -= deltaX;
						if (error < 0) {
							error += deltaY;
							--xSteps;
							if (xSteps == 0) {
								return;
							}
							++pixel;
						}
					}
				}
			} else if (startY >= g_flightClipTop &&
				   startY < g_flightClipBottom) {
				int count;

				if (g_flightClipLeft > x1) {
					x1 = g_flightClipLeft;
				}
				if (endX >= g_flightClipRight) {
					endX = g_flightClipRight - 1;
				}

				count = endX - x1;
				if (count > 0) {
					pixel = g_flightSwFramebufferBase;
					pixel += startY * g_surfacePitch;
					pixel += x1;
					while (count-- != 0) {
						*pixel++ = colorIdx;
					}
				}
			}
		}
	}
}

/* Draws g_flightSwRotSpriteSpanRunCountdown runs of a rotated sprite's line at
 * 8 bits, counting it down to 0: pixel i of a run goes at destBase +
 * spanOffsets[g_flightSwRotSpriteSpanBaseX + startX + i] in the run's color
 * index. Does not clip; the countdown and each length must be at least 1. */
// FUNCTION: XVT 0x4213E0
void FlightSw_DrawRotSpriteSpanRuns8(const FlightSwRotSpriteSpanRun *runs,
				     uint8_t *destBase, const int *spanOffsets)
{
	int colorIndex;
	int destOffset;
	int spanX;
	int length;
	const int *runOffsets;

	for (;;) {
		colorIndex = runs->colorIndex;
		spanX = g_flightSwRotSpriteSpanBaseX;
		spanX += runs->startX;
		length = runs->length;
		runs++;
		runOffsets = &spanOffsets[spanX];
		do {
			destOffset = *runOffsets++;
			destBase += destOffset;
			*destBase = (uint8_t)colorIndex;
			destBase -= destOffset;
			length--;
		} while (length != 0);
		g_flightSwRotSpriteSpanRunCountdown--;
		if (g_flightSwRotSpriteSpanRunCountdown == 0) {
			break;
		}
	}
}

/* Draws the runs as FlightSw_DrawRotSpriteSpanRuns8 does, but only the part of
 * each from g_flightSwRotSpriteClipMinX up to, not including,
 * g_flightSwRotSpriteClipMaxX. */
// FUNCTION: XVT 0x421430
void FlightSw_DrawClippedRotSpriteSpanRuns8(
	const FlightSwRotSpriteSpanRun *runs, uint8_t *destBase,
	const int *spanOffsets)
{
	int length;
	int spanStart;
	int spanEnd;
	int colorIndex;
	const FlightSwRotSpriteSpanRun *drawRun;
	const int *runOffsets;
	int destOffset;

	do {
		drawRun = runs++;
		length = drawRun->length;
		spanStart = g_flightSwRotSpriteSpanBaseX + drawRun->startX;
		spanEnd = spanStart + length;
		if (spanStart < g_flightSwRotSpriteClipMinX) {
			spanStart = g_flightSwRotSpriteClipMinX;
		}
		if (spanStart <= g_flightSwRotSpriteClipMaxX &&
		    spanEnd >= g_flightSwRotSpriteClipMinX) {
			if (spanEnd > g_flightSwRotSpriteClipMaxX) {
				spanEnd = g_flightSwRotSpriteClipMaxX;
			}
			length = spanEnd - spanStart;
			if (length != 0) {
				colorIndex = drawRun->colorIndex;
				runOffsets = &spanOffsets[spanStart];
				do {
					destOffset = *runOffsets++;
					destBase[destOffset] =
						(uint8_t)colorIndex;
					length--;
				} while (length != 0);
			}
		}
		g_flightSwRotSpriteSpanRunCountdown--;
	} while (g_flightSwRotSpriteSpanRunCountdown != 0);
}

/* The 16-bit form of FlightSw_DrawRotSpriteSpanRuns8: writes the color index in
 * a pixel's low byte and 0x80 in its high byte, marking it for
 * FlightSw_BlitPreparedRotatedSpriteSpans. */
// FUNCTION: XVT 0x4214A0
void FlightSw_DrawRotSpriteSpanRuns16(const FlightSwRotSpriteSpanRun *runs,
				      uint8_t *destBase, const int *spanOffsets)
{
	int colorIndex;
	int length;
	int spanX;
	const int *runOffsets;
	int destOffset;

	do {
		spanX = g_flightSwRotSpriteSpanBaseX;
		colorIndex = runs->colorIndex;
		spanX += runs->startX;
		length = runs->length;
		runs++;
		runOffsets = &spanOffsets[spanX];
		do {
			destOffset = *runOffsets++;
			destBase[destOffset] = (uint8_t)colorIndex;
			destBase[destOffset + 1] = 0x80;
			length--;
		} while (length != 0);
		g_flightSwRotSpriteSpanRunCountdown--;
	} while (g_flightSwRotSpriteSpanRunCountdown != 0);
}

/* The 16-bit form of FlightSw_DrawClippedRotSpriteSpanRuns8, marking pixels as
 * FlightSw_DrawRotSpriteSpanRuns16 does. */
// FUNCTION: XVT 0x4214F0
void FlightSw_DrawClippedRotSpriteSpanRuns16(
	const FlightSwRotSpriteSpanRun *runs, uint8_t *destBase,
	const int *spanOffsets)
{
	const FlightSwRotSpriteSpanRun *drawRun;
	int clipMinX;
	int length;
	int colorIndex;
	int destOffset;
	int clipMaxX;
	int spanStart;
	int spanEnd;

	do {
		drawRun = runs++;
		length = drawRun->length;
		spanStart = g_flightSwRotSpriteSpanBaseX + drawRun->startX;
		spanEnd = spanStart + length;
		clipMinX = g_flightSwRotSpriteClipMinX;
		clipMaxX = g_flightSwRotSpriteClipMaxX;
		if (spanStart < clipMinX) {
			spanStart = clipMinX;
		}
		if (spanStart <= clipMaxX && spanEnd >= clipMinX) {
			if (spanEnd > clipMaxX) {
				spanEnd = clipMaxX;
			}
			length = spanEnd - spanStart;
			if (length != 0) {
				colorIndex = drawRun->colorIndex;
				do {
					destOffset = spanOffsets[spanStart];
					destBase[destOffset] =
						(uint8_t)colorIndex;
					destBase[destOffset + 1] = 0x80;
					spanStart++;
					length--;
				} while (length != 0);
			}
		}
		g_flightSwRotSpriteSpanRunCountdown--;
	} while (g_flightSwRotSpriteSpanRunCountdown != 0);
}

/* Draws a rotated, scaled sprite on the software surface. Takes its first
 * corner from the data header, sets the scale for screenSize with
 * FlightSw_PrepareRotatedSpriteScaleState, and turns that corner to find the
 * screen point, from screenX and screenY, where the walk starts;
 * FlightSw_RasterizePreparedRotatedSprite then draws the image into
 * g_flightSwRotSpriteDestBuffer. Then turns the other three corners (the first
 * plus the width, plus the width less the height, and less the height) and
 * passes the four to FlightSw_ClipAndBlitPreparedRotatedSprite, which copies
 * what was drawn to the screen. FlightSw_PrepareSpriteRotationTables must have
 * run for the angle. */
// FUNCTION: XVT 0x421560
void FlightSw_DrawRotatedSpriteQuad(int16_t screenX, int16_t screenY,
				    uint16_t screenSize, SpritePayload *sprite)
{
	struct FlightSwRotSpriteDataHeader *spriteData;
	int16_t cornerX;
	int16_t cornerY;
	int cornerCoords[8];

	spriteData =
		(struct FlightSwRotSpriteDataHeader *)((uint8_t *)sprite +
						       sprite->rowDataOffset);
	if (g_flightSwRotSpriteSpanRunsEnabled == 1) {
		g_flightSwRotSpriteInputCornerX = (int16_t)spriteData->cornerX;
	} else {
		g_flightSwRotSpriteInputCornerX =
			-(int16_t)spriteData->alternateCornerX;
	}
	cornerX = g_flightSwRotSpriteInputCornerX;
	g_flightSwRotSpriteInputCornerY = -(int16_t)spriteData->cornerY;
	cornerY = g_flightSwRotSpriteInputCornerY;
	FlightSw_PrepareRotatedSpriteScaleState(screenSize,
						g_flightSwRotSpriteCoeffs,
						&g_flightSwRotSpriteScaleState);
	FlightSw_RotateSpritePoint(&g_flightSwRotSpriteCoeffs->rotationAngle,
				   &g_flightSwRotSpriteScaleState);
	g_flightSwRotSpriteEdgeCursorX =
		screenX + g_flightSwRotSpriteOutputOffsetX;
	cornerCoords[0] = (int16_t)(screenX + g_flightSwRotSpriteOutputOffsetX);
	g_flightSwRotSpriteEdgeCursorY =
		screenY + g_flightSwRotSpriteOutputOffsetY;
	cornerCoords[1] = (int16_t)(screenY + g_flightSwRotSpriteOutputOffsetY);
	FlightSw_RasterizePreparedRotatedSprite((uint8_t *)(spriteData + 1),
						sprite->packingMode);

	g_flightSwRotSpriteInputCornerX = cornerX + (int16_t)sprite->width;
	g_flightSwRotSpriteInputCornerY = cornerY;
	FlightSw_RotateSpritePoint(&g_flightSwRotSpriteCoeffs->rotationAngle,
				   &g_flightSwRotSpriteScaleState);
	cornerCoords[2] = screenX + g_flightSwRotSpriteOutputOffsetX;
	cornerCoords[3] = screenY + g_flightSwRotSpriteOutputOffsetY;

	g_flightSwRotSpriteInputCornerX = cornerX + (int16_t)sprite->width;
	g_flightSwRotSpriteInputCornerY = cornerY - (int16_t)sprite->height;
	FlightSw_RotateSpritePoint(&g_flightSwRotSpriteCoeffs->rotationAngle,
				   &g_flightSwRotSpriteScaleState);
	cornerCoords[4] = screenX + g_flightSwRotSpriteOutputOffsetX;
	cornerCoords[5] = screenY + g_flightSwRotSpriteOutputOffsetY;

	g_flightSwRotSpriteInputCornerX = cornerX;
	g_flightSwRotSpriteInputCornerY = cornerY - (int16_t)sprite->height;
	FlightSw_RotateSpritePoint(&g_flightSwRotSpriteCoeffs->rotationAngle,
				   &g_flightSwRotSpriteScaleState);
	cornerCoords[6] = screenX + g_flightSwRotSpriteOutputOffsetX;
	cornerCoords[7] = screenY + g_flightSwRotSpriteOutputOffsetY;
	FlightSw_ClipAndBlitPreparedRotatedSprite(cornerCoords);
}

/* Turns the four corners' y into rows from the bottom (g_flightVpMaxY minus y,
 * written back into cornerCoords), takes their bounding box widened by 2 on
 * each side, clips it to the viewport, returning when it lies wholly outside,
 * and calls FlightSw_BlitPreparedRotatedSpriteSpans on that box of
 * g_flightSwRotSpriteDestBuffer. */
// FUNCTION: XVT 0x421700
void FlightSw_ClipAndBlitPreparedRotatedSprite(int *cornerCoords)
{
	int minX;
	int maxX;
	int minY;
	int maxY;
	int rawY2;
	int rawY3;
	int rawY4;
	int flippedY2;
	int flippedY3;
	int flippedY4;
	int startX;
	int startY;
	int endX;
	int endY;

	maxY = g_flightVpMaxY - cornerCoords[1];
	rawY2 = cornerCoords[3];
	rawY3 = cornerCoords[5];
	cornerCoords[1] = maxY;
	flippedY2 = g_flightVpMaxY - rawY2;
	rawY4 = cornerCoords[7];
	cornerCoords[3] = flippedY2;
	flippedY3 = g_flightVpMaxY - rawY3;
	cornerCoords[5] = flippedY3;
	minY = maxY;
	flippedY4 = g_flightVpMaxY - rawY4;
	cornerCoords[7] = flippedY4;

	minX = cornerCoords[0];
	maxX = cornerCoords[0];
	if (minX > cornerCoords[2]) {
		minX = cornerCoords[2];
	}
	if (minX > cornerCoords[4]) {
		minX = cornerCoords[4];
	}
	if (minX > cornerCoords[6]) {
		minX = cornerCoords[6];
	}
	if (maxX < cornerCoords[2]) {
		maxX = cornerCoords[2];
	}
	if (maxX < cornerCoords[4]) {
		maxX = cornerCoords[4];
	}
	if (maxX < cornerCoords[6]) {
		maxX = cornerCoords[6];
	}

	if (minY > flippedY2) {
		minY = flippedY2;
	}
	if (minY > flippedY3) {
		minY = flippedY3;
	}
	if (minY > flippedY4) {
		minY = flippedY4;
	}
	if (maxY < flippedY2) {
		maxY = flippedY2;
	}
	if (maxY < flippedY3) {
		maxY = flippedY3;
	}
	if (maxY < flippedY4) {
		maxY = flippedY4;
	}

	startX = minX - 2;
	startY = minY - 2;
	endX = maxX + 2;
	endY = maxY + 2;
	if (endY < 0 || startY >= g_flightSwRotSpriteViewportHeight) {
		return;
	}
	if (endY >= g_flightSwRotSpriteViewportHeight) {
		endY = g_flightSwRotSpriteViewportMaxY;
	}
	if (startY < 0) {
		startY = 0;
	}
	if (endX < 0 || startX >= g_flightSwRotSpriteViewportWidth) {
		return;
	}
	if (endX >= g_flightSwRotSpriteViewportWidth) {
		endX = g_flightSwRotSpriteViewportMaxX;
	}
	if (startX < 0) {
		startX = 0;
	}

	FlightSw_BlitPreparedRotatedSpriteSpans(
		g_flightSwRotSpriteDestBuffer + g_flightBytesPerPixel * startX +
			g_flightSwRotSpriteDestPitchBytes * startY,
		g_flightSwRotSpriteDestPitchBytes +
			g_flightBytesPerPixel * (startX - endX),
		startX, startY, endX, endY);
}

/* Readies the rotated-sprite state for the current viewport and an angle:
 * copies the viewport's size and limits, sets the destination pitch and line,
 * g_flightSwRotSpriteDestYMode to -1, square-pixel mode when g_projAspectY is 0
 * and the axis-swap angle (0x2000, else 0x2200), and points
 * g_flightSwRotSpriteCoeffs at the cache. Rebuilds the cache with
 * FlightSw_BuildSpriteRotationCoeffs when rotationAngle differs from the cached
 * angle, compared as signed 16-bit values, or
 * g_flightSwRotSpriteCoeffCacheValid is 0. Ignores bytesPerPixel. */
// FUNCTION: XVT 0x421850
void FlightSw_PrepareSpriteRotationTables(int16_t rotationAngle,
					  int bytesPerPixel)
{
	(void)bytesPerPixel;
	g_flightSwRotSpriteViewportWidth = (int16_t)g_flightVpWidth;
	g_flightSwRotSpriteDestPitchBytes =
		g_flightBytesPerPixel * g_flightVpWidth;
	g_flightSwRotSpriteDestLinePtr = g_flightSwRotSpriteDestBuffer;
	g_flightSwRotSpriteViewportMaxY = (int16_t)g_flightVpMaxY;
	g_flightSwRotSpriteViewportMaxX = (int16_t)g_flightVpMaxX;
	g_flightSwRotSpriteViewportHeight = (int16_t)g_flightVpHeight;
	g_flightSwRotSpriteSquarePixelMode = 0;
	g_flightSwRotSpriteDestYMode = -1;
	if (g_projAspectY == 0) {
		g_flightSwRotSpriteSquarePixelMode = 1;
	}
	g_flightSwRotSpriteCoeffs = &g_flightSwRotSpriteCoeffCache;
	g_flightSwRotSpriteAxisSwapThresholdAngle = 0x2000;
	if (g_flightSwRotSpriteSquarePixelMode != 1) {
		g_flightSwRotSpriteAxisSwapThresholdAngle = 0x2200;
	}

	if ((int16_t)g_flightSwRotSpriteCoeffCache.rotationAngle !=
		    rotationAngle ||
	    g_flightSwRotSpriteCoeffCacheValid == 0) {
		FlightSw_BuildSpriteRotationCoeffs(
			rotationAngle,
			&g_flightSwRotSpriteCoeffCache.rotationAngle);
		g_flightSwRotSpriteCoeffCacheValid = 1;
	}
}

/* Copies the sprite's colorCount drawing colors: at 16 bits each color's low
 * and high byte into g_flightSwRotSpritePalette16Low and
 * g_flightSwRotSpritePalette16High, else one byte each into
 * g_flightSwRotSpritePalette8. Returns colorCount, or 0 when it is negative.
 * Does not check colorCount against 256. */
// FUNCTION: XVT 0x421930
int FlightSw_LoadSpritePaletteTables(SpritePayload *sprite)
{
	uint8_t *palette;
	int colorCount;
	int colorIndex;

	colorCount = sprite->colorCount;
	palette = (uint8_t *)sprite + sprite->displayPaletteOffset;
	if (g_flightBytesPerPixel == 2) {
		for (colorIndex = 0; colorIndex < colorCount; colorIndex++) {
			g_flightSwRotSpritePalette16Low[colorIndex] =
				*palette++;
			g_flightSwRotSpritePalette16High[colorIndex] =
				*palette++;
		}
	} else {
		for (colorIndex = 0; colorIndex < colorCount; colorIndex++) {
			g_flightSwRotSpritePalette8[colorIndex] = *palette++;
		}
	}

	return colorIndex;
}

/* Returns entry angle >> 6 of g_flightSwTangent91Pct when scalePercent is 91,
 * of g_flightSwTangent110Pct when it is 110, else of g_flightSwTangent100Pct.
 * Does not check the entry against the table's size. */
// FUNCTION: XVT 0x421980
uint16_t FlightSw_LookupScaledTangent(uint16_t angle, int16_t scalePercent)
{
	angle >>= 6;
	if (scalePercent == 91) {
		return g_flightSwTangent91Pct[angle];
	}
	if (scalePercent == 110) {
		return g_flightSwTangent110Pct[angle];
	}
	return g_flightSwTangent100Pct[angle];
}

/* Sets scaleState for a sprite of screenSize: the screen scale; the aspect
 * scales, 256 and 256 with square pixels, else 233 and 282, setting
 * g_flightSwRotSpriteAxisSwapThresholdAngle to 0x2000 or 0x2200 as well; the
 * horizontal step, (screenSize * primaryCosMagnitudeQ16) >> 16, times
 * aspectScaleY >> 8 with primaryAxisSwap; and the vertical step, the base step
 * plus (base * secondaryStepByte) >> 8, times inverseAspectScaleY >> 8 without
 * secondaryAxisSwap. Rebuilds the run-width tables when the horizontal step
 * differs from the one they were built for. */
// FUNCTION: XVT 0x4219D0
void FlightSw_PrepareRotatedSpriteScaleState(
	uint16_t screenSize, FlightSwRotSpriteCoeffState *rotationCoeffs,
	FlightSwRotSpriteScaleState *scaleState)
{
	uint16_t *aspectScaleY;
	uint16_t *inverseAspectScaleY;
	unsigned int baseHorizontalStep;
	unsigned int verticalStep;
	int squarePixelMode;
	uint8_t horizontalStepLow;
	uint8_t cachedStepLow;

	scaleState->screenScale = screenSize;
	aspectScaleY = &scaleState->aspectScaleY;
	squarePixelMode = g_flightSwRotSpriteSquarePixelMode;
	if (squarePixelMode == 1) {
		*aspectScaleY = 256;
		inverseAspectScaleY = &scaleState->inverseAspectScaleY;
		*inverseAspectScaleY = 256;
		g_flightSwRotSpriteAxisSwapThresholdAngle = 0x2000;
	} else {
		*aspectScaleY = 233;
		inverseAspectScaleY = &scaleState->inverseAspectScaleY;
		*inverseAspectScaleY = 282;
		g_flightSwRotSpriteAxisSwapThresholdAngle = 0x2200;
	}

	baseHorizontalStep = ((unsigned int)screenSize *
			      rotationCoeffs->primaryCosMagnitudeQ16) >>
			     16;
	scaleState->horizontalStepLowByte = (uint8_t)baseHorizontalStep;
	scaleState->horizontalStepHighByte = (uint8_t)(baseHorizontalStep >> 8);
	if (rotationCoeffs->primaryAxisSwap != 0) {
		unsigned int scaledHorizontalStep;

		scaledHorizontalStep =
			(baseHorizontalStep * *aspectScaleY) >> 8;
		scaleState->horizontalStepLowByte =
			(uint8_t)scaledHorizontalStep;
		scaleState->horizontalStepHighByte =
			(uint8_t)(scaledHorizontalStep >> 8);
	}
	verticalStep =
		((baseHorizontalStep * rotationCoeffs->secondaryStepByte) >>
		 8) +
		baseHorizontalStep;
	if (rotationCoeffs->secondaryAxisSwap == 0) {
		verticalStep = (verticalStep * *inverseAspectScaleY) >> 8;
	}
	horizontalStepLow = scaleState->horizontalStepLowByte;
	cachedStepLow = scaleState->cachedStepLowByte;
	scaleState->verticalStepLowByte = (uint8_t)verticalStep;
	scaleState->verticalStepHighByte = (uint8_t)(verticalStep >> 8);
	if (cachedStepLow != horizontalStepLow ||
	    scaleState->cachedStepHighByte !=
		    scaleState->horizontalStepHighByte) {
		uint16_t tableIndex;
		unsigned int packedAccumulator;
		unsigned int packedStep;
		uint8_t horizontalStepHigh;

		horizontalStepHigh = scaleState->horizontalStepHighByte;
		scaleState->cachedStepLowByte = horizontalStepLow;
		scaleState->cachedStepHighByte = horizontalStepHigh;
		packedAccumulator = ((unsigned int)horizontalStepHigh << 16) |
				    ((unsigned int)horizontalStepLow << 8);
		packedStep = packedAccumulator;
		tableIndex = 0;
		do {
			scaleState->lowWordStepTable[tableIndex] =
				(uint16_t)packedAccumulator;
			scaleState->highWordStepTable[tableIndex] =
				(uint16_t)(packedAccumulator >> 16);
			packedAccumulator += packedStep;
			++tableIndex;
		} while (tableIndex < 256);
	}
}

/* Turns and scales the sprite point in g_flightSwRotSpriteInputCornerX and Y
 * (texels) into g_flightSwRotSpriteOutputOffsetX and Y (pixels from the
 * sprite's screen point). Scales the magnitudes by screenScale over 256 with
 * rounding, y first by inverseAspectScaleY over 256; then x' = (x * cos + y *
 * sin + 0x8000) >> 16 and y' = (x * sin - y * cos + 0x8000) >> 16, with signed
 * sine and cosine magnitudes from rotationCoeffs (65536 standing for 1); then
 * scales y' by aspectScaleY over 256 with rounding. Leaves the input's
 * magnitudes in the input globals. */
// FUNCTION: XVT 0x421AE0
void FlightSw_RotateSpritePoint(uint16_t *rotationCoeffs,
				FlightSwRotSpriteScaleState *scaleState)
{
	int16_t originalCornerX;
	int16_t originalCornerY;
	int scaledX;
	int rotatedXFromX;
	int scaledY;
	int rotatedXFromY;
	int rotatedYFromX;
	int rotatedYFromY;
	int rotatedY;
	int finalY;

	originalCornerX = g_flightSwRotSpriteInputCornerX;
	if (g_flightSwRotSpriteInputCornerX < 0) {
		g_flightSwRotSpriteInputCornerX =
			-g_flightSwRotSpriteInputCornerX;
	}
	scaledX = (g_flightSwRotSpriteInputCornerX * scaleState->screenScale +
		   128) >>
		  8;
	originalCornerY = g_flightSwRotSpriteInputCornerY;
	if (g_flightSwRotSpriteInputCornerY < 0) {
		g_flightSwRotSpriteInputCornerY =
			-g_flightSwRotSpriteInputCornerY;
	}
	scaledY = (g_flightSwRotSpriteInputCornerY *
			   scaleState->inverseAspectScaleY +
		   128) >>
		  8;
	scaledY = (scaledY * scaleState->screenScale + 128) >> 8;

	rotatedXFromX = (uint16_t)scaledX * rotationCoeffs[3];
	if (((originalCornerX ^ rotationCoeffs[4]) & 0x8000) != 0) {
		rotatedXFromX = -rotatedXFromX;
	}
	rotatedXFromY = (uint16_t)scaledY * rotationCoeffs[1];
	if (((originalCornerY ^ rotationCoeffs[2]) & 0x8000) != 0) {
		rotatedXFromY = -rotatedXFromY;
	}
	rotatedXFromX += rotatedXFromY + 0x8000;
	g_flightSwRotSpriteOutputOffsetX = rotatedXFromX >> 16;

	rotatedYFromX = (uint16_t)scaledX * rotationCoeffs[1];
	if (((originalCornerX ^ rotationCoeffs[2]) & 0x8000) != 0) {
		rotatedYFromX = -rotatedYFromX;
	}
	rotatedYFromY = (uint16_t)scaledY * rotationCoeffs[3];
	if (((originalCornerY ^ rotationCoeffs[4]) & 0x8000) == 0) {
		rotatedYFromY = -rotatedYFromY;
	}
	rotatedYFromX += rotatedYFromY + 0x8000;
	rotatedY = rotatedYFromX >> 16;
	g_flightSwRotSpriteOutputOffsetY = rotatedY;
	if ((int16_t)rotatedY < 0) {
		g_flightSwRotSpriteOutputOffsetY = -(int16_t)rotatedY;
	}
	finalY = (g_flightSwRotSpriteOutputOffsetY * scaleState->aspectScaleY +
		  128) >>
		 8;
	if ((int16_t)rotatedY < 0) {
		finalY = -finalY;
	}
	g_flightSwRotSpriteOutputOffsetY = finalY;
}

/* Fills the FlightSwRotSpriteCoeffState at outCoeffs for rotationAngle: the
 * sign bits and flips; the angle folded into a quarter turn and its sine and
 * cosine magnitudes; whether the edge steps along y (folded angle at or over
 * g_flightSwRotSpriteAxisSwapThresholdAngle, folding it once more); the edge's
 * scanCount points from (0, 0), one step along the main axis each, the cross
 * axis following a 16-bit fraction that starts at one half and grows by the
 * tangent (91, 100 or 110 percent by pixel shape); the runs of points that
 * share a cross coordinate; the secondary scale for the angle a quarter turn
 * on; the octant; the first and last points and the extents in entry 0; and
 * each point's destination byte offset with the flips applied. */
// FUNCTION: XVT 0x421C50
void FlightSw_BuildSpriteRotationCoeffs(uint16_t rotationAngle,
					uint16_t *outCoeffs)
{
	FlightSwRotSpriteCoeffState *coeffs;
	uint16_t primaryAngle;
	uint16_t primaryStep;
	uint16_t secondaryAngle;
	uint16_t primaryStepReciprocal;
	uint16_t flipY;
	uint16_t flipX;
	int16_t primaryAxisSwap;
	int16_t secondaryAxisSwap;
	uint16_t pointIndex;
	uint16_t runIndex;
	uint16_t remaining;
	uint16_t spanIndex;

	coeffs = (FlightSwRotSpriteCoeffState *)outCoeffs;
	coeffs->rotationAngle = rotationAngle;
	coeffs->sinSignMask = rotationAngle & 0x8000;
	flipY = 0;
	coeffs->cosSignMask = (rotationAngle + 0x4000) & 0x8000;
	flipX = 0;
	if (rotationAngle >= 0x8000) {
		rotationAngle &= 0x7FFF;
		flipY = 1;
		if (rotationAngle < 0x4000) {
			flipX = 2;
		}
	} else if (rotationAngle >= 0x4000) {
		flipX = 2;
	}
	coeffs->flipY = flipY;
	coeffs->flipX = flipX;

	if (rotationAngle >= 0x4000) {
		primaryAngle = 0x8000 - rotationAngle;
	} else {
		primaryAngle = rotationAngle;
	}
	coeffs->sinMagnitudeQ16 =
		FlightSw_LookupSpriteSineMagnitudeQ16(primaryAngle);
	coeffs->cosMagnitudeQ16 =
		FlightSw_LookupSpriteSineMagnitudeQ16(primaryAngle + 0x4000);

	if (primaryAngle < g_flightSwRotSpriteAxisSwapThresholdAngle) {
		primaryAxisSwap = 0;
		coeffs->primaryAxisSwap = primaryAxisSwap;
		if (g_flightSwRotSpriteSquarePixelMode == 1) {
			primaryStep =
				FlightSw_LookupScaledTangent(primaryAngle, 100);
		} else {
			primaryStep =
				FlightSw_LookupScaledTangent(primaryAngle, 91);
		}
	} else {
		primaryAxisSwap = 4;
		coeffs->primaryAxisSwap = primaryAxisSwap;
		primaryAngle = 0x4000 - primaryAngle;
		if (g_flightSwRotSpriteSquarePixelMode == 1) {
			primaryStep =
				FlightSw_LookupScaledTangent(primaryAngle, 100);
		} else {
			primaryStep =
				FlightSw_LookupScaledTangent(primaryAngle, 110);
		}
	}
	coeffs->primaryCosMagnitudeQ16 =
		FlightSw_LookupSpriteSineMagnitudeQ16(primaryAngle + 0x4000);
	primaryStepReciprocal =
		(uint16_t)(0x80000000u / coeffs->primaryCosMagnitudeQ16);
	if (primaryAxisSwap != 0 && g_flightSwRotSpriteSquarePixelMode == 0) {
		primaryStepReciprocal =
			(uint16_t)(((unsigned int)primaryStepReciprocal *
					    g_projAspectY +
				    0x8000) >>
				   16);
	}
	coeffs->primaryStepReciprocal = primaryStepReciprocal;

	coeffs->edgePointsWithPredecessor[1].x = 0;
	coeffs->edgePointsWithPredecessor[1].y = 0;
	if (coeffs->primaryAxisSwap == 0) {
		uint16_t accumulator;
		uint16_t remainingColumns;
		int16_t edgeX;
		int16_t edgeY;

		edgeX = 0;
		edgeY = 0;
		accumulator = 0x8000;
		pointIndex = 2;
		coeffs->scanCount = g_flightSwRotSpriteViewportWidth;
		remainingColumns = g_flightSwRotSpriteViewportWidth - 1;
		while (remainingColumns-- != 0) {
			uint16_t previousAccumulator;

			++edgeX;
			previousAccumulator = accumulator;
			accumulator += primaryStep;
			if (previousAccumulator > accumulator) {
				++edgeY;
			}
			coeffs->edgePointsWithPredecessor[pointIndex].x = edgeX;
			coeffs->edgePointsWithPredecessor[pointIndex].y = edgeY;
			++pointIndex;
		}
	} else {
		uint16_t accumulator;
		uint16_t remainingRows;
		int16_t edgeX;
		int16_t edgeY;

		accumulator = 0x8000;
		edgeX = 0;
		edgeY = 0;
		coeffs->scanCount = g_flightSwRotSpriteViewportHeight;
		pointIndex = 2;
		remainingRows = g_flightSwRotSpriteViewportHeight - 1;
		while (remainingRows-- != 0) {
			uint16_t previousAccumulator;

			++edgeY;
			previousAccumulator = accumulator;
			accumulator += primaryStep;
			if (previousAccumulator > accumulator) {
				++edgeX;
			}
			coeffs->edgePointsWithPredecessor[pointIndex].x = edgeX;
			coeffs->edgePointsWithPredecessor[pointIndex].y = edgeY;
			++pointIndex;
		}
	}

	pointIndex = 1;
	runIndex = 0;
	remaining = coeffs->scanCount;
	while (remaining != 0) {
		uint16_t runLength;
		int16_t coordinate;

		runLength = 1;
		if (coeffs->primaryAxisSwap != 0) {
			coordinate =
				coeffs->edgePointsWithPredecessor[pointIndex].x;
		} else {
			coordinate =
				coeffs->edgePointsWithPredecessor[pointIndex].y;
		}
		--remaining;
		while (remaining != 0) {
			int16_t nextCoordinate;

			if (coeffs->primaryAxisSwap != 0) {
				nextCoordinate =
					coeffs->edgePointsWithPredecessor
						[pointIndex + 1]
							.x;
			} else {
				nextCoordinate =
					coeffs->edgePointsWithPredecessor
						[pointIndex + 1]
							.y;
			}
			if (nextCoordinate != coordinate) {
				break;
			}
			++runLength;
			++pointIndex;
			--remaining;
		}
		++pointIndex;
		coeffs->runLengths[runIndex] = runLength;
		++runIndex;
	}
	coeffs->runLengthCount = runIndex;

	secondaryAngle = (rotationAngle + 0x4000) & 0x7FFF;
	if (secondaryAngle >= 0x4000) {
		secondaryAngle = 0x8000 - secondaryAngle;
	}
	if (secondaryAngle < g_flightSwRotSpriteAxisSwapThresholdAngle) {
		secondaryAxisSwap = 0;
		coeffs->secondaryAxisSwap = secondaryAxisSwap;
		if (g_flightSwRotSpriteSquarePixelMode == 1) {
			coeffs->secondaryScaleLow =
				FlightSw_LookupScaledTangent(secondaryAngle,
							     100);
		} else {
			coeffs->secondaryScaleLow =
				FlightSw_LookupScaledTangent(secondaryAngle,
							     91);
		}
	} else {
		secondaryAxisSwap = 4;
		coeffs->secondaryAxisSwap = secondaryAxisSwap;
		secondaryAngle = 0x4000 - secondaryAngle;
		if (g_flightSwRotSpriteSquarePixelMode == 1) {
			coeffs->secondaryScaleLow =
				FlightSw_LookupScaledTangent(secondaryAngle,
							     100);
		} else {
			coeffs->secondaryScaleLow =
				FlightSw_LookupScaledTangent(secondaryAngle,
							     110);
		}
	}
	coeffs->secondaryScaleHigh = 0;
	if (secondaryAxisSwap != primaryAxisSwap ||
	    g_flightSwRotSpriteSquarePixelMode != 0) {
		coeffs->secondaryStepByte =
			(uint16_t)((0x800000u +
				    (unsigned int)primaryStep *
					    coeffs->secondaryScaleLow) >>
				   24);
	} else if ((coeffs->rotationAngle >= 0xE000 ||
		    coeffs->rotationAngle < 0xA000) &&
		   (coeffs->rotationAngle >= 0x6000 ||
		    coeffs->rotationAngle < 0x2000)) {
		coeffs->secondaryScaleLow = 256;
		coeffs->secondaryScaleHigh = 256;
		coeffs->secondaryStepByte =
			(uint16_t)(((0xD800u * primaryStep) >> 16) >> 8);
	} else {
		unsigned int secondaryScale;

		secondaryScale =
			(unsigned int)(uint16_t)(0x1000000 /
						 coeffs->secondaryScaleLow)
			<< 8;
		coeffs->secondaryScaleLow = (uint16_t)secondaryScale;
		coeffs->secondaryScaleHigh = (uint16_t)(secondaryScale >> 16);
		coeffs->secondaryStepByte = primaryStep >> 8;
	}

	coeffs->octant =
		coeffs->primaryAxisSwap | coeffs->flipY | coeffs->flipX;
	coeffs->flipCount = (coeffs->flipX >> 1) + coeffs->flipY;
	coeffs->firstEdgeX = coeffs->edgePointsWithPredecessor[1].x;
	coeffs->firstEdgeY = coeffs->edgePointsWithPredecessor[1].y;
	coeffs->firstEdgeScreenY =
		g_flightSwRotSpriteViewportMaxY - coeffs->firstEdgeY;
	coeffs->lastEdgeX =
		coeffs->edgePointsWithPredecessor[coeffs->scanCount].x;
	coeffs->lastEdgeY =
		coeffs->edgePointsWithPredecessor[coeffs->scanCount].y;
	coeffs->lastEdgeScreenY =
		g_flightSwRotSpriteViewportMaxY - coeffs->lastEdgeY;
	coeffs->edgePointsWithPredecessor[0].x =
		coeffs->lastEdgeX - coeffs->firstEdgeX;
	if (coeffs->edgePointsWithPredecessor[0].x < 0) {
		coeffs->edgePointsWithPredecessor[0].x =
			-coeffs->edgePointsWithPredecessor[0].x;
	}
	coeffs->edgePointsWithPredecessor[0].y =
		coeffs->lastEdgeScreenY - coeffs->firstEdgeScreenY;
	if (coeffs->edgePointsWithPredecessor[0].y < 0) {
		coeffs->edgePointsWithPredecessor[0].y =
			-coeffs->edgePointsWithPredecessor[0].y;
	}

	if (g_flightBytesPerPixel == 2) {
		for (spanIndex = 0; spanIndex < coeffs->scanCount;
		     ++spanIndex) {
			int16_t x;
			int16_t y;

			x = coeffs->edgePointsWithPredecessor[spanIndex + 1].x;
			if (coeffs->flipX != 0) {
				x = -x;
			}
			y = coeffs->edgePointsWithPredecessor[spanIndex + 1].y;
			if (coeffs->flipY != 0) {
				y = -y;
			}
			if (g_flightSwRotSpriteDestYMode > 0) {
				coeffs->spanOffsets[spanIndex] =
					g_flightSwRotSpriteDestPitchBytes * y +
					2 * x;
			} else {
				coeffs->spanOffsets[spanIndex] =
					2 * x -
					g_flightSwRotSpriteDestPitchBytes * y;
			}
		}
	} else {
		for (spanIndex = 0; spanIndex < coeffs->scanCount;
		     ++spanIndex) {
			int16_t x;
			int16_t y;

			x = coeffs->edgePointsWithPredecessor[spanIndex + 1].x;
			if (coeffs->flipX != 0) {
				x = -x;
			}
			y = coeffs->edgePointsWithPredecessor[spanIndex + 1].y;
			if (coeffs->flipY != 0) {
				y = -y;
			}
			if (g_flightSwRotSpriteDestYMode > 0) {
				coeffs->spanOffsets[spanIndex] =
					g_flightSwRotSpriteDestPitchBytes * y +
					x;
			} else {
				coeffs->spanOffsets[spanIndex] =
					x -
					g_flightSwRotSpriteDestPitchBytes * y;
			}
		}
	}
}

/* Draws the encoded sprite rows into g_flightSwRotSpriteDestBuffer along the
 * rotated edge. Sets g_flightSwRotSpriteSpanRunsEnabled to 1, then returns at
 * once when FlightSw_InitRotSpriteForCurrentOctant finds nothing to draw; else
 * saves the start state in the four g_flightSwRotSpriteSaved globals and points
 * the walk at its first line. Each row, until a 0xFF, is decoded up to its
 * 0xFE: 0xFB sets a palette base from the next two bytes, low first; 0xFC skips
 * the next byte + 1 texels; 0xFD is a run of the next byte + 1 texels of the
 * color after it; any other byte is a run of (byte & mask) + 1 texels of color
 * palette base + (byte >> shift) for packingMode. Each run's width comes from
 * the step tables. The row is then drawn on each of the n lines the vertical
 * step gives it, stepping a line after each with the octant step function
 * (returning when that returns 0) and FlightSw_AdvanceRotSpriteSecondaryScale;
 * with n of 0 it is drawn once without stepping. A row with runs is drawn only
 * while g_flightSwRotSpriteClipMaxX is 0 or more: unclipped when its positions
 * run from at least 0 and g_flightSwRotSpriteClipMinX to under
 * g_flightSwRotSpriteClipMaxX, else clipped. */
// FUNCTION: XVT 0x422170
void FlightSw_RasterizePreparedRotatedSprite(uint8_t *spriteData,
					     int packingMode)
{
	uint8_t colorIndex;
	uint8_t scaleOverflow;
	uint8_t scaleLow;
	uint8_t scaleHigh;
	int16_t rowsRemaining;
	int16_t rowsToDraw;
	uint16_t previousFraction;
	int paletteBase;
	int previousScaledX;
	uint8_t nextScaleOverflow;
	uint8_t nextScaleHigh;
	uint8_t previousScaleHigh;
	unsigned int previousScalePosition;
	uint8_t token;
	uint8_t runLength;
	unsigned int runIndex;
	int spanRunCount;
	int scaledX;
	uint16_t scaledFraction;
	uint8_t *cursor;

	g_flightSwRotSpriteSpanRunsEnabled = 1;
	if (FlightSw_InitRotSpriteForCurrentOctant() == 0) {
		return;
	}

	g_flightSwRotSpriteSavedPrimaryEdgeX = g_flightSwRotSpritePrimaryEdgeX;
	g_flightSwRotSpriteSavedPrimaryEdgeY = g_flightSwRotSpritePrimaryEdgeY;
	g_flightSwRotSpriteSavedClipMinX = g_flightSwRotSpriteClipMinX;
	g_flightSwRotSpriteSavedClipMaxX = g_flightSwRotSpriteClipMaxX;
	if (g_flightSwRotSpriteDestYMode > 0) {
		g_flightSwRotSpriteCoeffs->destLinePtr =
			&g_flightSwRotSpriteDestLinePtr
				[g_flightBytesPerPixel *
					 g_flightSwRotSpritePrimaryEdgeX +
				 g_flightSwRotSpriteDestPitchBytes *
					 g_flightSwRotSpritePrimaryEdgeY];
		g_flightSwRotSpriteCoeffs->destPitchDelta =
			-g_flightSwRotSpriteDestPitchBytes;
	} else {
		g_flightSwRotSpriteCoeffs->destLinePtr =
			&g_flightSwRotSpriteDestLinePtr
				[g_flightSwRotSpriteDestPitchBytes *
					 (g_flightSwRotSpriteViewportMaxY -
					  g_flightSwRotSpritePrimaryEdgeY) +
				 g_flightBytesPerPixel *
					 g_flightSwRotSpritePrimaryEdgeX];
		g_flightSwRotSpriteCoeffs->destPitchDelta =
			g_flightSwRotSpriteDestPitchBytes;
	}

	scaleLow = 0;
	scaleHigh = 0;
	scaleOverflow = 0;
	cursor = spriteData;
	paletteBase = 0;
	g_flightSwRotSpriteSkipSecondaryScaleStep = 1;
	g_flightSwRotSpriteSecondaryScaleAccum = 0;
	g_flightSwRotSpriteDestLinePtr = g_flightSwRotSpriteCoeffs->destLinePtr;
	while (*cursor != 0xFF) {
		previousScalePosition =
			((unsigned int)scaleOverflow << 8) + scaleHigh;
		nextScaleOverflow = scaleOverflow;
		nextScaleHigh = scaleHigh;
		if ((uint8_t)(scaleLow + g_flightSwRotSpriteScaleState
						 .verticalStepLowByte) <
		    scaleLow) {
			++nextScaleHigh;
			if (scaleHigh == 0xFF) {
				++nextScaleOverflow;
			}
		}
		previousScaleHigh = nextScaleHigh;
		nextScaleHigh +=
			g_flightSwRotSpriteScaleState.verticalStepHighByte;
		if (nextScaleHigh < previousScaleHigh) {
			++nextScaleOverflow;
		}
		rowsRemaining =
			(int16_t)(((unsigned int)nextScaleOverflow << 8) +
				  nextScaleHigh - previousScalePosition);
		rowsToDraw = rowsRemaining;

		spanRunCount = 0;
		scaledX = 0;
		scaledFraction = 0;
		if (g_flightSwRotSpriteSpanRunsEnabled == 1) {
			while (*cursor != 0xFE) {
				token = *cursor;
				if (token == 0xFB) {
					paletteBase =
						cursor[1] + (cursor[2] << 8);
					cursor += 3;
				} else if (token == 0xFC) {
					previousFraction = scaledFraction;
					runIndex = cursor[1];
					cursor += 2;
					scaledX += g_flightSwRotSpriteScaleState
							   .highWordStepTable
								   [runIndex];
					scaledFraction +=
						g_flightSwRotSpriteScaleState
							.lowWordStepTable
								[runIndex];
					if (scaledFraction < previousFraction) {
						++scaledX;
					}
				} else {
					if (token == 0xFD) {
						runLength = cursor[1];
						colorIndex = cursor[2];
						cursor += 3;
					} else {
						++cursor;
						colorIndex =
							(uint8_t)(paletteBase +
								  (token >>
								   g_flightSwRlePaletteShiftByPackingMode
									   [packingMode]));
						runLength =
							token &
							g_flightSwRleRunLengthMaskByPackingMode
								[packingMode];
					}
					previousFraction = scaledFraction;
					previousScaledX = scaledX;
					runIndex = runLength;
					scaledX += g_flightSwRotSpriteScaleState
							   .highWordStepTable
								   [runIndex];
					scaledFraction +=
						g_flightSwRotSpriteScaleState
							.lowWordStepTable
								[runIndex];
					if (scaledFraction < previousFraction) {
						++scaledX;
					}
					g_flightSwRotSpriteSpanRuns
						[spanRunCount]
							.startX =
						previousScaledX;
					g_flightSwRotSpriteSpanRuns
						[spanRunCount]
							.colorIndex =
						colorIndex;
					g_flightSwRotSpriteSpanRuns
						[spanRunCount]
							.length =
						scaledX - previousScaledX + 1;
					++spanRunCount;
				}
			}
			++cursor;
		}

		do {
			if (spanRunCount != 0 &&
			    g_flightSwRotSpriteClipMaxX >= 0) {
				if ((int16_t)(scaledX +
					      g_flightSwRotSpriteSpanBaseX) >=
					    0 &&
				    g_flightSwRotSpriteClipMaxX >
					    (int16_t)(scaledX +
						      g_flightSwRotSpriteSpanBaseX) &&
				    g_flightSwRotSpriteSpanBaseX >= 0 &&
				    g_flightSwRotSpriteSpanBaseX >=
					    g_flightSwRotSpriteClipMinX) {
					g_flightSwRotSpriteSpanRunCountdown =
						spanRunCount;
					if (g_flightBytesPerPixel == 2) {
						FlightSw_DrawRotSpriteSpanRuns16(
							g_flightSwRotSpriteSpanRuns,
							g_flightSwRotSpriteDestLinePtr,
							g_flightSwRotSpriteCoeffs
								->spanOffsets);
					} else {
						FlightSw_DrawRotSpriteSpanRuns8(
							g_flightSwRotSpriteSpanRuns,
							g_flightSwRotSpriteDestLinePtr,
							g_flightSwRotSpriteCoeffs
								->spanOffsets);
					}
				} else {
					g_flightSwRotSpriteSpanRunCountdown =
						spanRunCount;
					if (g_flightBytesPerPixel == 2) {
						FlightSw_DrawClippedRotSpriteSpanRuns16(
							g_flightSwRotSpriteSpanRuns,
							g_flightSwRotSpriteDestLinePtr,
							g_flightSwRotSpriteCoeffs
								->spanOffsets);
					} else {
						FlightSw_DrawClippedRotSpriteSpanRuns8(
							g_flightSwRotSpriteSpanRuns,
							g_flightSwRotSpriteDestLinePtr,
							g_flightSwRotSpriteCoeffs
								->spanOffsets);
					}
				}
			}
			if (rowsToDraw != 0) {
				if (FlightSw_StepRotSpriteForCurrentOctant() ==
				    0) {
					return;
				}
				FlightSw_AdvanceRotSpriteSecondaryScale();
			}
			--rowsRemaining;
		} while (rowsRemaining != 0 && rowsToDraw != 0);

		{
			uint8_t previousScaleLow;

			previousScaleLow = scaleLow;
			scaleLow += g_flightSwRotSpriteScaleState
					    .verticalStepLowByte;
			if (scaleLow < previousScaleLow) {
				++scaleHigh;
			}
		}
		previousScaleHigh = scaleHigh;
		scaleHigh += g_flightSwRotSpriteScaleState.verticalStepHighByte;
		if (scaleHigh < previousScaleHigh) {
			++scaleOverflow;
		}
	}
}

/* Moves the span base along the edge as lines advance. When
 * g_flightSwRotSpriteSkipSecondaryScaleStep is set it clears it and returns.
 * Else adds secondaryScaleLow to g_flightSwRotSpriteSecondaryScaleAccum; with
 * secondaryScaleHigh 0 it goes on only on a carry, moving 1, otherwise it moves
 * 1, or 2 on a carry. It moves g_flightSwRotSpriteSpanBaseX up when flipCount
 * is 1 without primaryAxisSwap, or is not 1 with it, else down, and sets the
 * skip flag when the edge point beside the base's (the next when moving up, the
 * one before when moving down) has another cross coordinate. For that lookup
 * the base is reduced into 0 to scanCount - 1 as a 16-bit unsigned value, so a
 * negative base counts from 65536. */
// FUNCTION: XVT 0x422560
void FlightSw_AdvanceRotSpriteSecondaryScale(void)
{
	int16_t spanStep;
	uint16_t previousAccum;
	uint16_t pointIndex;
	unsigned int scanCount;
	FlightSwRotSpriteEdgePoint *currentPoint;
	int16_t currentCoordinate;

	spanStep = 1;
	if (g_flightSwRotSpriteSkipSecondaryScaleStep != 0) {
		g_flightSwRotSpriteSkipSecondaryScaleStep = 0;
		return;
	}

	previousAccum = g_flightSwRotSpriteSecondaryScaleAccum;
	g_flightSwRotSpriteSecondaryScaleAccum +=
		g_flightSwRotSpriteCoeffs->secondaryScaleLow;
	if (g_flightSwRotSpriteCoeffs->secondaryScaleHigh != 0) {
		if (previousAccum > g_flightSwRotSpriteSecondaryScaleAccum) {
			spanStep = 2;
		}
	} else if (previousAccum <= g_flightSwRotSpriteSecondaryScaleAccum) {
		return;
	}

	pointIndex = g_flightSwRotSpriteSpanBaseX;
	while (pointIndex < 0) {
		pointIndex += g_flightSwRotSpriteCoeffs->scanCount;
	}
	scanCount = g_flightSwRotSpriteCoeffs->scanCount;
	while ((int)pointIndex >= (int)scanCount) {
		pointIndex -= (int16_t)g_flightSwRotSpriteCoeffs->scanCount;
	}

	if (g_flightSwRotSpriteCoeffs->primaryAxisSwap == 0) {
		currentPoint =
			&g_flightSwRotSpriteCoeffs
				 ->edgePointsWithPredecessor[pointIndex + 1];
		currentCoordinate = currentPoint->y;
		if (g_flightSwRotSpriteCoeffs->flipCount == 1) {
			g_flightSwRotSpriteSpanBaseX += spanStep;
			if (currentPoint[1].y != currentCoordinate) {
				g_flightSwRotSpriteSkipSecondaryScaleStep = 1;
			}
		} else {
			g_flightSwRotSpriteSpanBaseX -= spanStep;
			if (g_flightSwRotSpriteCoeffs
				    ->edgePointsWithPredecessor[pointIndex]
				    .y != currentCoordinate) {
				g_flightSwRotSpriteSkipSecondaryScaleStep = 1;
			}
		}
	} else {
		currentPoint =
			&g_flightSwRotSpriteCoeffs
				 ->edgePointsWithPredecessor[pointIndex + 1];
		currentCoordinate = currentPoint->x;
		if (g_flightSwRotSpriteCoeffs->flipCount != 1) {
			g_flightSwRotSpriteSpanBaseX += spanStep;
			if (currentPoint[1].x != currentCoordinate) {
				g_flightSwRotSpriteSkipSecondaryScaleStep = 1;
			}
		} else {
			g_flightSwRotSpriteSpanBaseX -= spanStep;
			if (g_flightSwRotSpriteCoeffs
				    ->edgePointsWithPredecessor[pointIndex]
				    .x != currentCoordinate) {
				g_flightSwRotSpriteSkipSecondaryScaleStep = 1;
			}
		}
	}
}

/* Calls the set-up function for g_flightSwRotSpriteCoeffs->octant and returns
 * what it returns; for an octant over 7, returns the octant. */
// FUNCTION: XVT 0x4226A0
int FlightSw_InitRotSpriteForCurrentOctant(void)
{
	uint16_t octant;

	octant = g_flightSwRotSpriteCoeffs->octant;
	switch (octant) {
	case 0:
		return FlightSw_InitRotSpriteOctant0();
	case 1:
		return FlightSw_InitRotSpriteOctant1();
	case 2:
		return FlightSw_InitRotSpriteOctant2();
	case 3:
		return FlightSw_InitRotSpriteOctant3();
	case 4:
		return FlightSw_InitRotSpriteOctant4();
	case 5:
		return FlightSw_InitRotSpriteOctant5();
	case 6:
		return FlightSw_InitRotSpriteOctant6();
	case 7:
		return FlightSw_InitRotSpriteOctant7();
	default:
		return octant;
	}
}

/* Calls the line-step function for g_flightSwRotSpriteCoeffs->octant and
 * returns what it returns; for an octant over 7, returns the octant. */
// FUNCTION: XVT 0x422710
int FlightSw_StepRotSpriteForCurrentOctant(void)
{
	uint16_t octant;

	octant = g_flightSwRotSpriteCoeffs->octant;
	switch (octant) {
	case 0:
		return FlightSw_StepRotSpriteOctant0();
	case 1:
		return FlightSw_StepRotSpriteOctant1();
	case 2:
		return FlightSw_StepRotSpriteOctant2();
	case 3:
		return FlightSw_StepRotSpriteOctant3();
	case 4:
		return FlightSw_StepRotSpriteOctant4();
	case 5:
		return FlightSw_StepRotSpriteOctant5();
	case 6:
		return FlightSw_StepRotSpriteOctant6();
	case 7:
		return FlightSw_StepRotSpriteOctant7();
	default:
		return octant;
	}
}

/* Sets up the walk for octant 0: the edge steps along x, no flips. Moves the
 * edge cursor a whole edge (the extents in entry 0, plus 1) at a time until its
 * x is inside the viewport, moving the span base a viewport width each time;
 * puts the line start at the viewport's left side
 * (g_flightSwRotSpritePrimaryEdgeX 0) on the edge's row there and the far end's
 * row in g_flightSwRotSpriteSecondaryEdgeY; then sets the clip bounds from the
 * edge points where the edge crosses the viewport's rows, and
 * g_flightSwRotSpriteSpanBaseX. Returns 0 when the line start's row is at or
 * past g_flightSwRotSpriteViewportHeight, else 1. */
// FUNCTION: XVT 0x422780
int FlightSw_InitRotSpriteOctant0(void)
{
	int16_t spanBaseOffset;
	int16_t viewportWidth;
	int16_t viewportHeight;
	int16_t *negativeEdgeDeltaXPtr;
	int16_t *negativeEdgeDeltaYPtr;
	int16_t *positiveEdgeDeltaXPtr;
	int16_t *positiveEdgeDeltaYPtr;
	int16_t edgeCursorMinimum;
	uint16_t pointIndex;
	int16_t clipMinX;
	uint16_t maxPointIndex;

	spanBaseOffset = 0;
	if (g_flightSwRotSpriteEdgeCursorX < 0) {
		viewportWidth = g_flightSwRotSpriteViewportWidth;
		negativeEdgeDeltaXPtr =
			&g_flightSwRotSpriteCoeffs->edgePointsWithPredecessor[0]
				 .x;
		negativeEdgeDeltaYPtr =
			&g_flightSwRotSpriteCoeffs->edgePointsWithPredecessor[0]
				 .y;
		edgeCursorMinimum = 0;
		do {
			spanBaseOffset -= viewportWidth;
			g_flightSwRotSpriteEdgeCursorX +=
				*negativeEdgeDeltaXPtr + 1;
			g_flightSwRotSpriteEdgeCursorY +=
				*negativeEdgeDeltaYPtr + 1;
		} while (g_flightSwRotSpriteEdgeCursorX < edgeCursorMinimum);
	}
	if (g_flightSwRotSpriteEdgeCursorX >=
	    g_flightSwRotSpriteViewportWidth) {
		positiveEdgeDeltaXPtr =
			&g_flightSwRotSpriteCoeffs->edgePointsWithPredecessor[0]
				 .x;
		positiveEdgeDeltaYPtr =
			&g_flightSwRotSpriteCoeffs->edgePointsWithPredecessor[0]
				 .y;
		do {
			g_flightSwRotSpriteEdgeCursorX -=
				*positiveEdgeDeltaXPtr + 1;
			spanBaseOffset += g_flightSwRotSpriteViewportWidth;
			g_flightSwRotSpriteEdgeCursorY -=
				*positiveEdgeDeltaYPtr + 1;
		} while (g_flightSwRotSpriteEdgeCursorX >=
			 g_flightSwRotSpriteViewportWidth);
	}

	viewportHeight = g_flightSwRotSpriteViewportHeight;
	g_flightSwRotSpriteEdgeCursorY -=
		g_flightSwRotSpriteCoeffs
			->edgePointsWithPredecessor
				[g_flightSwRotSpriteEdgeCursorX + 1]
			.y;
	g_flightSwRotSpritePrimaryEdgeY = g_flightSwRotSpriteEdgeCursorY;
	if (g_flightSwRotSpriteEdgeCursorY >= viewportHeight) {
		g_flightSwRotSpriteSecondaryEdgeY =
			g_flightSwRotSpriteEdgeCursorY +
			g_flightSwRotSpriteCoeffs->edgePointsWithPredecessor[0]
				.y;
		return 0;
	}

	clipMinX = 0;
	g_flightSwRotSpritePrimaryEdgeX = 0;
	if (g_flightSwRotSpriteEdgeCursorY < 0) {
		clipMinX = -g_flightSwRotSpriteEdgeCursorY;
		g_flightSwRotSpriteEdgeCursorY = clipMinX;
		g_flightSwRotSpriteClipMinRunIdx03 = clipMinX;
		if (g_flightSwRotSpriteCoeffs->edgePointsWithPredecessor[0].y <
		    clipMinX) {
			clipMinX = -1;
		} else {
			pointIndex = 0;
			while (pointIndex <=
			       g_flightSwRotSpriteCoeffs
				       ->edgePointsWithPredecessor[0]
				       .x) {
				if ((uint16_t)g_flightSwRotSpriteCoeffs
					    ->edgePointsWithPredecessor
						    [pointIndex + 1]
					    .y ==
				    g_flightSwRotSpriteEdgeCursorY) {
					clipMinX =
						g_flightSwRotSpriteCoeffs
							->edgePointsWithPredecessor
								[pointIndex + 1]
							.x;
					break;
				}
				++pointIndex;
			}
		}
	}
	g_flightSwRotSpriteClipMinX = clipMinX;
	g_flightSwRotSpriteSecondaryEdgeX =
		g_flightSwRotSpriteCoeffs->edgePointsWithPredecessor[0].x;
	g_flightSwRotSpriteClipMaxRunIdx03 = viewportHeight;
	g_flightSwRotSpriteSecondaryEdgeY =
		g_flightSwRotSpritePrimaryEdgeY +
		g_flightSwRotSpriteCoeffs->edgePointsWithPredecessor[0].y;

	if (g_flightSwRotSpriteSecondaryEdgeY < 0) {
		g_flightSwRotSpriteClipMaxX = -1;
	} else if (g_flightSwRotSpriteSecondaryEdgeY >= 0 &&
		   g_flightSwRotSpriteSecondaryEdgeY < viewportHeight) {
		g_flightSwRotSpriteClipMaxX =
			g_flightSwRotSpriteCoeffs->scanCount - 1;
	} else {
		g_flightSwRotSpriteClipMaxRunIdx03 =
			viewportHeight - g_flightSwRotSpritePrimaryEdgeY;
		maxPointIndex = 0;
		g_flightSwRotSpriteClipMaxX = -1;
		while (maxPointIndex <=
		       g_flightSwRotSpriteCoeffs->edgePointsWithPredecessor[0]
			       .x) {
			if ((uint16_t)g_flightSwRotSpriteCoeffs
				    ->edgePointsWithPredecessor[maxPointIndex +
								1]
				    .y == g_flightSwRotSpriteClipMaxRunIdx03) {
				g_flightSwRotSpriteClipMaxX =
					g_flightSwRotSpriteCoeffs
						->edgePointsWithPredecessor
							[maxPointIndex + 1]
						.x -
					1;
				break;
			}
			++maxPointIndex;
		}
	}
	g_flightSwRotSpriteSpanBaseX =
		spanBaseOffset + g_flightSwRotSpriteEdgeCursorX;
	return 1;
}

/* Steps the octant 0 walk a line: moves g_flightSwRotSpriteDestLinePtr back by
 * destPitchDelta, raises g_flightSwRotSpritePrimaryEdgeY and
 * g_flightSwRotSpriteSecondaryEdgeY by 1, and moves the clip bounds by the run
 * lengths as either end enters or leaves the viewport's rows. Returns 0 once
 * the line start's row reaches the viewport height, else 1. */
// FUNCTION: XVT 0x4229F0
int FlightSw_StepRotSpriteOctant0(void)
{
	uint8_t *destLinePtr;
	int16_t viewportHeight;
	int16_t minRunIndex;

	destLinePtr = g_flightSwRotSpriteDestLinePtr;
	destLinePtr -= g_flightSwRotSpriteCoeffs->destPitchDelta;
	++g_flightSwRotSpritePrimaryEdgeY;
	g_flightSwRotSpriteDestLinePtr = destLinePtr;
	if (g_flightSwRotSpritePrimaryEdgeY == 0) {
		g_flightSwRotSpriteClipMinX = 0;
	} else {
		viewportHeight = g_flightSwRotSpriteViewportHeight;
		if (g_flightSwRotSpritePrimaryEdgeY >= viewportHeight) {
			return 0;
		}
		if (g_flightSwRotSpritePrimaryEdgeY < 0) {
			--g_flightSwRotSpriteClipMinRunIdx03;
			g_flightSwRotSpriteClipMinX -=
				g_flightSwRotSpriteCoeffs->runLengths
					[g_flightSwRotSpriteClipMinRunIdx03];
		}
	}

	++g_flightSwRotSpriteSecondaryEdgeY;
	if (g_flightSwRotSpriteSecondaryEdgeY == 0) {
		g_flightSwRotSpriteClipMaxX =
			g_flightSwRotSpriteCoeffs->scanCount - 1;
		minRunIndex = g_flightSwRotSpriteCoeffs->runLengthCount - 1;
		g_flightSwRotSpriteClipMinRunIdx03 = minRunIndex;
		g_flightSwRotSpriteClipMinX =
			g_flightSwRotSpriteClipMaxX -
			g_flightSwRotSpriteCoeffs->runLengths[minRunIndex] + 1;
		if (g_flightSwRotSpriteClipMinX < 0) {
			g_flightSwRotSpriteClipMinX = 0;
			return 1;
		}
	} else if (g_flightSwRotSpriteSecondaryEdgeY >=
		   g_flightSwRotSpriteViewportHeight) {
		if (g_flightSwRotSpriteSecondaryEdgeY ==
		    g_flightSwRotSpriteViewportHeight) {
			g_flightSwRotSpriteClipMaxRunIdx03 =
				g_flightSwRotSpriteCoeffs->runLengthCount;
		}
		--g_flightSwRotSpriteClipMaxRunIdx03;
		g_flightSwRotSpriteClipMaxX -=
			g_flightSwRotSpriteCoeffs->runLengths
				[g_flightSwRotSpriteClipMaxRunIdx03];
	}
	return 1;
}

/* Sets up the walk for octant 1 (flipY) the way FlightSw_InitRotSpriteOctant0
 * does for octant 0, the cursor's row moving the other way. Returns 0 when the
 * far end's row is at or past the viewport height, else 1. */
// FUNCTION: XVT 0x422B10
int FlightSw_InitRotSpriteOctant1(void)
{
	int16_t spanBaseOffset;
	int16_t clipMinValue;
	int16_t clipMaxValue;
	int16_t pointIndex;
	int16_t targetY;
	int16_t edgeDeltaX;
	int16_t viewportWidth;
	int16_t edgeCursorMinimum;
	int16_t *negativeEdgeDeltaXPtr;
	int16_t *negativeEdgeDeltaYPtr;
	int16_t *positiveEdgeDeltaXPtr;
	int16_t *positiveEdgeDeltaYPtr;
	int16_t *edgeDeltaXPtr;

	spanBaseOffset = 0;
	if (g_flightSwRotSpriteEdgeCursorX < 0) {
		viewportWidth = g_flightSwRotSpriteViewportWidth;
		negativeEdgeDeltaXPtr =
			&g_flightSwRotSpriteCoeffs->edgePointsWithPredecessor[0]
				 .x;
		negativeEdgeDeltaYPtr =
			&g_flightSwRotSpriteCoeffs->edgePointsWithPredecessor[0]
				 .y;
		edgeCursorMinimum = 0;
		do {
			spanBaseOffset -= viewportWidth;
			g_flightSwRotSpriteEdgeCursorX +=
				*negativeEdgeDeltaXPtr + 1;
			g_flightSwRotSpriteEdgeCursorY -=
				*negativeEdgeDeltaYPtr + 1;
		} while (g_flightSwRotSpriteEdgeCursorX < edgeCursorMinimum);
	}
	if (g_flightSwRotSpriteEdgeCursorX >=
	    g_flightSwRotSpriteViewportWidth) {
		viewportWidth = g_flightSwRotSpriteViewportWidth;
		positiveEdgeDeltaXPtr =
			&g_flightSwRotSpriteCoeffs->edgePointsWithPredecessor[0]
				 .x;
		positiveEdgeDeltaYPtr =
			&g_flightSwRotSpriteCoeffs->edgePointsWithPredecessor[0]
				 .y;
		do {
			spanBaseOffset += viewportWidth;
			g_flightSwRotSpriteEdgeCursorX -=
				*positiveEdgeDeltaXPtr + 1;
			g_flightSwRotSpriteEdgeCursorY +=
				*positiveEdgeDeltaYPtr + 1;
		} while (g_flightSwRotSpriteEdgeCursorX >= viewportWidth);
	}

	g_flightSwRotSpritePrimaryEdgeX = 0;
	g_flightSwRotSpriteEdgeCursorY +=
		g_flightSwRotSpriteCoeffs
			->edgePointsWithPredecessor
				[g_flightSwRotSpriteEdgeCursorX + 1]
			.y;
	g_flightSwRotSpritePrimaryEdgeY = g_flightSwRotSpriteEdgeCursorY;
	if (g_flightSwRotSpriteEdgeCursorY < 0) {
		clipMinValue = -1;
	} else if (g_flightSwRotSpriteEdgeCursorY <
		   g_flightSwRotSpriteViewportHeight) {
		clipMinValue = 0;
	} else {
		targetY = g_flightSwRotSpriteEdgeCursorY -
			  g_flightSwRotSpriteViewportHeight;
		g_flightSwRotSpriteClipMinRunIdx03 = targetY;
		clipMinValue = -1;
		if (g_flightSwRotSpriteCoeffs->edgePointsWithPredecessor[0].y >=
		    targetY + 1) {
			pointIndex = 0;
			edgeDeltaX = g_flightSwRotSpriteCoeffs
					     ->edgePointsWithPredecessor[0]
					     .x;
			while (pointIndex <= edgeDeltaX &&
			       (uint16_t)g_flightSwRotSpriteCoeffs
						       ->edgePointsWithPredecessor
							       [pointIndex + 1]
						       .y -
					       targetY !=
				       1) {
				++pointIndex;
			}
			if (pointIndex <= edgeDeltaX) {
				clipMinValue =
					g_flightSwRotSpriteCoeffs
						->edgePointsWithPredecessor
							[pointIndex + 1]
						.x;
			}
		}
	}
	g_flightSwRotSpriteClipMinX = clipMinValue;

	edgeDeltaXPtr =
		&g_flightSwRotSpriteCoeffs->edgePointsWithPredecessor[0].x;
	g_flightSwRotSpriteSecondaryEdgeX = *edgeDeltaXPtr;
	g_flightSwRotSpriteClipMaxRunIdx03 = g_flightSwRotSpriteViewportHeight;
	g_flightSwRotSpriteSecondaryEdgeY =
		g_flightSwRotSpriteEdgeCursorY -
		g_flightSwRotSpriteCoeffs->edgePointsWithPredecessor[0].y;
	if (g_flightSwRotSpriteSecondaryEdgeY >=
	    g_flightSwRotSpriteViewportHeight) {
		return 0;
	}

	if (g_flightSwRotSpriteSecondaryEdgeY >= 0) {
		clipMaxValue = g_flightSwRotSpriteCoeffs->scanCount - 1;
	} else if (g_flightSwRotSpriteEdgeCursorY >= 0) {
		g_flightSwRotSpriteClipMaxRunIdx03 =
			g_flightSwRotSpriteEdgeCursorY;
		pointIndex = 0;
		edgeDeltaX =
			g_flightSwRotSpriteCoeffs->edgePointsWithPredecessor[0]
				.x;
		targetY = g_flightSwRotSpriteCoeffs->firstEdgeY +
			  g_flightSwRotSpriteEdgeCursorY + 1;
		while (pointIndex <= edgeDeltaX &&
		       g_flightSwRotSpriteCoeffs
				       ->edgePointsWithPredecessor[pointIndex +
								   1]
				       .y != targetY) {
			++pointIndex;
		}
		clipMaxValue = -1;
		if (pointIndex <= edgeDeltaX) {
			clipMaxValue =
				g_flightSwRotSpriteCoeffs
					->edgePointsWithPredecessor[pointIndex +
								    1]
					.x -
				1;
		}
	} else {
		clipMaxValue = -1;
	}
	g_flightSwRotSpriteClipMaxX = clipMaxValue;
	g_flightSwRotSpriteSpanBaseX =
		spanBaseOffset + g_flightSwRotSpriteEdgeCursorX;
	return 1;
}

/* Steps the octant 1 walk a line as FlightSw_StepRotSpriteOctant0 does. Returns
 * 0 once the far end's row reaches the viewport height, else 1. */
// FUNCTION: XVT 0x422D90
int FlightSw_StepRotSpriteOctant1(void)
{
	uint16_t viewportHeight;

	g_flightSwRotSpriteDestLinePtr -=
		g_flightSwRotSpriteCoeffs->destPitchDelta;
	++g_flightSwRotSpritePrimaryEdgeY;
	if (g_flightSwRotSpritePrimaryEdgeY == 0) {
		g_flightSwRotSpriteClipMinX = 0;
		g_flightSwRotSpriteClipMaxX =
			g_flightSwRotSpriteCoeffs->runLengths[0];
		--g_flightSwRotSpriteClipMaxX;
		g_flightSwRotSpriteClipMaxRunIdx03 = 0;
	} else {
		viewportHeight = g_flightSwRotSpriteViewportHeight;
		if (g_flightSwRotSpritePrimaryEdgeY >=
		    (int16_t)viewportHeight) {
			if (g_flightSwRotSpritePrimaryEdgeY ==
			    (int16_t)viewportHeight) {
				g_flightSwRotSpriteClipMinRunIdx03 = -1;
			}
			++g_flightSwRotSpriteClipMinRunIdx03;
			g_flightSwRotSpriteClipMinX +=
				g_flightSwRotSpriteCoeffs->runLengths
					[g_flightSwRotSpriteClipMinRunIdx03];
		}
	}

	++g_flightSwRotSpriteSecondaryEdgeY;
	if (g_flightSwRotSpriteSecondaryEdgeY == 0) {
		g_flightSwRotSpriteClipMaxX =
			g_flightSwRotSpriteCoeffs->scanCount - 1;
		return 1;
	}
	if (g_flightSwRotSpriteSecondaryEdgeY >=
	    g_flightSwRotSpriteViewportHeight) {
		return 0;
	}
	if (g_flightSwRotSpriteSecondaryEdgeY < 0 &&
	    g_flightSwRotSpritePrimaryEdgeY > 0) {
		++g_flightSwRotSpriteClipMaxRunIdx03;
		g_flightSwRotSpriteClipMaxX +=
			g_flightSwRotSpriteCoeffs->runLengths
				[g_flightSwRotSpriteClipMaxRunIdx03];
	}
	return 1;
}

/* Sets up the walk for octant 2 (flipX) the way FlightSw_InitRotSpriteOctant0
 * does for octant 0, with the line start at the viewport's right side
 * (g_flightSwRotSpritePrimaryEdgeX of g_flightSwRotSpriteViewportMaxX). Returns
 * 0 when the far end's row is under 0, else 1. */
// FUNCTION: XVT 0x422E90
int FlightSw_InitRotSpriteOctant2(void)
{
	int16_t spanBaseOffset;
	int16_t viewportRightDelta;
	int16_t minPointIndex;
	int16_t maxPointIndex;

	spanBaseOffset = 0;
	while (g_flightSwRotSpriteEdgeCursorX < 0) {
		g_flightSwRotSpriteEdgeCursorX +=
			g_flightSwRotSpriteCoeffs->edgePointsWithPredecessor[0]
				.x +
			1;
		spanBaseOffset += g_flightSwRotSpriteViewportWidth;
		g_flightSwRotSpriteEdgeCursorY -=
			g_flightSwRotSpriteCoeffs->edgePointsWithPredecessor[0]
				.y +
			1;
	}
	while (g_flightSwRotSpriteEdgeCursorX >=
	       g_flightSwRotSpriteViewportWidth) {
		g_flightSwRotSpriteEdgeCursorX -=
			g_flightSwRotSpriteCoeffs->edgePointsWithPredecessor[0]
				.x +
			1;
		spanBaseOffset -= g_flightSwRotSpriteViewportWidth;
		g_flightSwRotSpriteEdgeCursorY +=
			g_flightSwRotSpriteCoeffs->edgePointsWithPredecessor[0]
				.y +
			1;
	}

	viewportRightDelta = g_flightSwRotSpriteViewportMaxX -
			     g_flightSwRotSpriteEdgeCursorX;
	g_flightSwRotSpriteEdgeCursorY -=
		g_flightSwRotSpriteCoeffs
			->edgePointsWithPredecessor[viewportRightDelta + 1]
			.y;
	g_flightSwRotSpritePrimaryEdgeY = g_flightSwRotSpriteEdgeCursorY;
	g_flightSwRotSpritePrimaryEdgeX = g_flightSwRotSpriteViewportMaxX;
	g_flightSwRotSpriteSecondaryEdgeX =
		g_flightSwRotSpriteViewportMaxX -
		g_flightSwRotSpriteCoeffs->edgePointsWithPredecessor[0].x;
	g_flightSwRotSpriteSecondaryEdgeY =
		g_flightSwRotSpriteEdgeCursorY +
		g_flightSwRotSpriteCoeffs->edgePointsWithPredecessor[0].y;
	g_flightSwRotSpriteClipMinX = -1;
	g_flightSwRotSpriteClipMaxX = -1;
	g_flightSwRotSpriteClipMinRunIdx03 = -1;
	g_flightSwRotSpriteClipMaxRunIdx03 = g_flightSwRotSpriteViewportHeight;

	if (g_flightSwRotSpriteEdgeCursorY >= 0) {
		if (g_flightSwRotSpriteEdgeCursorY >=
		    g_flightSwRotSpriteViewportHeight) {
			g_flightSwRotSpriteSpanBaseX =
				viewportRightDelta + spanBaseOffset;
			return 1;
		}
		minPointIndex = 0;
	} else {
		g_flightSwRotSpriteClipMinRunIdx03 =
			-g_flightSwRotSpriteEdgeCursorY - 1;
		minPointIndex = -1;
		if (g_flightSwRotSpriteCoeffs->edgePointsWithPredecessor[0].y >=
		    -g_flightSwRotSpriteEdgeCursorY) {
			minPointIndex = 0;
			if (g_flightSwRotSpriteCoeffs
				    ->edgePointsWithPredecessor[0]
				    .x >= 0) {
				while ((uint16_t)g_flightSwRotSpriteCoeffs
					       ->edgePointsWithPredecessor
						       [minPointIndex + 1]
					       .y !=
				       -g_flightSwRotSpriteEdgeCursorY) {
					++minPointIndex;
					if (minPointIndex >
					    g_flightSwRotSpriteCoeffs
						    ->edgePointsWithPredecessor
							    [0]
						    .x) {
						break;
					}
				}
			}
			if (minPointIndex <=
			    g_flightSwRotSpriteCoeffs
				    ->edgePointsWithPredecessor[0]
				    .x) {
				minPointIndex =
					g_flightSwRotSpriteCoeffs
						->edgePointsWithPredecessor
							[minPointIndex + 1]
						.x;
			} else {
				minPointIndex = -1;
			}
		}
	}
	/* minPointIndex is an edge point index only during the search above; here it holds the clip minimum
	 * x: 0, -1, or the found point's x. */
	g_flightSwRotSpriteClipMinX = minPointIndex;

	if (g_flightSwRotSpriteSecondaryEdgeY < 0) {
		return 0;
	}
	if (g_flightSwRotSpriteSecondaryEdgeY <
	    g_flightSwRotSpriteViewportHeight) {
		maxPointIndex = g_flightSwRotSpriteCoeffs->scanCount - 1;
	} else {
		g_flightSwRotSpriteClipMaxRunIdx03 =
			g_flightSwRotSpriteViewportHeight -
			g_flightSwRotSpriteEdgeCursorY - 1;
		maxPointIndex =
			g_flightSwRotSpriteCoeffs->edgePointsWithPredecessor[0]
				.x;
		if (maxPointIndex >= 0) {
			while ((uint16_t)g_flightSwRotSpriteCoeffs
				       ->edgePointsWithPredecessor
					       [maxPointIndex + 1]
				       .y !=
			       g_flightSwRotSpriteClipMaxRunIdx03) {
				--maxPointIndex;
				if (maxPointIndex < 0) {
					break;
				}
			}
		}
		if (maxPointIndex >= 0) {
			maxPointIndex = g_flightSwRotSpriteCoeffs
						->edgePointsWithPredecessor
							[maxPointIndex + 1]
						.x;
		} else {
			maxPointIndex = -1;
		}
	}
	/* maxPointIndex is an edge point index only during the search above; here it holds the clip maximum
	 * x: scanCount - 1, -1, or the found point's x. */
	g_flightSwRotSpriteClipMaxX = maxPointIndex;
	g_flightSwRotSpriteSpanBaseX = viewportRightDelta + spanBaseOffset;
	return 1;
}

/* Steps the octant 2 walk a line: moves g_flightSwRotSpriteDestLinePtr on by
 * destPitchDelta, lowers both edge rows by 1 and moves the clip bounds. Returns
 * 0 once the far end's row is under 0, else 1. */
// FUNCTION: XVT 0x4230F0
int FlightSw_StepRotSpriteOctant2(void)
{
	g_flightSwRotSpriteDestLinePtr +=
		g_flightSwRotSpriteCoeffs->destPitchDelta;
	--g_flightSwRotSpritePrimaryEdgeY;
	if (g_flightSwRotSpritePrimaryEdgeY ==
	    g_flightSwRotSpriteViewportMaxY) {
		g_flightSwRotSpriteClipMinX = 0;
		g_flightSwRotSpriteClipMaxX = -1;
		g_flightSwRotSpriteClipMaxRunIdx03 = -1;
	} else if (g_flightSwRotSpritePrimaryEdgeY < 0) {
		if (g_flightSwRotSpritePrimaryEdgeY == -1) {
			g_flightSwRotSpriteClipMinRunIdx03 = -1;
		}
		++g_flightSwRotSpriteClipMinRunIdx03;
		g_flightSwRotSpriteClipMinX +=
			g_flightSwRotSpriteCoeffs->runLengths
				[g_flightSwRotSpriteClipMinRunIdx03];
	}

	--g_flightSwRotSpriteSecondaryEdgeY;
	if (g_flightSwRotSpriteSecondaryEdgeY ==
	    g_flightSwRotSpriteViewportMaxY) {
		g_flightSwRotSpriteClipMaxX =
			g_flightSwRotSpriteCoeffs->scanCount - 1;
		return 1;
	}
	if (g_flightSwRotSpriteSecondaryEdgeY < 0) {
		return 0;
	}
	if (g_flightSwRotSpriteSecondaryEdgeY >=
		    g_flightSwRotSpriteViewportHeight &&
	    g_flightSwRotSpritePrimaryEdgeY <
		    g_flightSwRotSpriteViewportHeight) {
		++g_flightSwRotSpriteClipMaxRunIdx03;
		g_flightSwRotSpriteClipMaxX +=
			g_flightSwRotSpriteCoeffs->runLengths
				[g_flightSwRotSpriteClipMaxRunIdx03];
	}
	return 1;
}

/* Sets up the walk for octant 3 (flipX and flipY), with the line start at the
 * viewport's right side. Returns 0 when the line start's row is under 0, else
 * 1. */
// FUNCTION: XVT 0x423200
int FlightSw_InitRotSpriteOctant3(void)
{
	int16_t spanBaseOffset;
	int16_t *edgeDeltaXPtr;
	int16_t *edgeDeltaYPtr;
	int16_t clipValue;
	int16_t pointIndex;
	int16_t edgePointIndex;
	int16_t targetY;

	spanBaseOffset = 0;
	while (g_flightSwRotSpriteEdgeCursorX < 0) {
		g_flightSwRotSpriteEdgeCursorX +=
			g_flightSwRotSpriteCoeffs->edgePointsWithPredecessor[0]
				.x +
			1;
		spanBaseOffset += g_flightSwRotSpriteViewportWidth;
		g_flightSwRotSpriteEdgeCursorY +=
			g_flightSwRotSpriteCoeffs->edgePointsWithPredecessor[0]
				.y +
			1;
	}
	while (g_flightSwRotSpriteEdgeCursorX >=
	       g_flightSwRotSpriteViewportWidth) {
		spanBaseOffset -= g_flightSwRotSpriteViewportWidth;
		g_flightSwRotSpriteEdgeCursorX -=
			g_flightSwRotSpriteCoeffs->edgePointsWithPredecessor[0]
				.x +
			1;
		g_flightSwRotSpriteEdgeCursorY -=
			g_flightSwRotSpriteCoeffs->edgePointsWithPredecessor[0]
				.y +
			1;
	}

	edgePointIndex = g_flightSwRotSpriteViewportMaxX -
			 g_flightSwRotSpriteEdgeCursorX;
	g_flightSwRotSpriteEdgeCursorY +=
		g_flightSwRotSpriteCoeffs
			->edgePointsWithPredecessor[edgePointIndex + 1]
			.y;
	g_flightSwRotSpritePrimaryEdgeY = g_flightSwRotSpriteEdgeCursorY;
	edgeDeltaYPtr =
		&g_flightSwRotSpriteCoeffs->edgePointsWithPredecessor[0].y;
	g_flightSwRotSpritePrimaryEdgeX = g_flightSwRotSpriteViewportMaxX;
	edgeDeltaXPtr =
		&g_flightSwRotSpriteCoeffs->edgePointsWithPredecessor[0].x;
	g_flightSwRotSpriteSecondaryEdgeX =
		g_flightSwRotSpriteViewportMaxX - *edgeDeltaXPtr;
	g_flightSwRotSpriteSecondaryEdgeY =
		g_flightSwRotSpriteEdgeCursorY - *edgeDeltaYPtr;
	g_flightSwRotSpriteClipMinX = -1;
	g_flightSwRotSpriteClipMaxX = -1;
	g_flightSwRotSpriteClipMinRunIdx03 = -1;
	g_flightSwRotSpriteClipMaxRunIdx03 =
		g_flightSwRotSpriteCoeffs->runLengthCount;

	if (g_flightSwRotSpriteEdgeCursorY < 0) {
		return 0;
	}
	clipValue = 0;
	if (g_flightSwRotSpriteEdgeCursorY >=
	    g_flightSwRotSpriteViewportHeight) {
		targetY = g_flightSwRotSpriteEdgeCursorY -
			  g_flightSwRotSpriteViewportMaxY;
		g_flightSwRotSpriteClipMinRunIdx03 = targetY;
		clipValue = -1;
		if (*edgeDeltaYPtr >= targetY) {
			pointIndex = 0;
			while (pointIndex <= *edgeDeltaXPtr &&
			       (uint16_t)g_flightSwRotSpriteCoeffs
					       ->edgePointsWithPredecessor
						       [pointIndex + 1]
					       .y != targetY) {
				++pointIndex;
			}
			if (pointIndex <= *edgeDeltaXPtr) {
				clipValue = g_flightSwRotSpriteCoeffs
						    ->edgePointsWithPredecessor
							    [pointIndex + 1]
						    .x;
			}
		}
	}
	g_flightSwRotSpriteClipMinX = clipValue;

	if (g_flightSwRotSpriteSecondaryEdgeY >=
	    g_flightSwRotSpriteViewportHeight) {
		clipValue = -1;
	} else if (g_flightSwRotSpriteSecondaryEdgeY >= 0) {
		clipValue = g_flightSwRotSpriteCoeffs->scanCount - 1;
	} else {
		targetY = g_flightSwRotSpriteSecondaryEdgeY + *edgeDeltaYPtr;
		g_flightSwRotSpriteClipMaxRunIdx03 = targetY + 1;
		pointIndex = *edgeDeltaXPtr;
		while (pointIndex >= 0 &&
		       (uint16_t)g_flightSwRotSpriteCoeffs
				       ->edgePointsWithPredecessor[pointIndex +
								   1]
				       .y != targetY) {
			--pointIndex;
		}
		clipValue = -1;
		if (pointIndex >= 0) {
			clipValue =
				g_flightSwRotSpriteCoeffs
					->edgePointsWithPredecessor[pointIndex +
								    1]
					.x;
		}
	}
	g_flightSwRotSpriteClipMaxX = clipValue;
	g_flightSwRotSpriteSpanBaseX = g_flightSwRotSpriteViewportMaxX -
				       g_flightSwRotSpriteEdgeCursorX +
				       spanBaseOffset;
	return 1;
}

/* Steps the octant 3 walk a line as FlightSw_StepRotSpriteOctant2 does. Returns
 * 0 once the line start's row is under 0, else 1. */
// FUNCTION: XVT 0x423480
int FlightSw_StepRotSpriteOctant3(void)
{
	uint8_t *destLinePtr;
	int16_t viewportMaxY;
	uint16_t runLengthCount;
	int16_t minRunIndex;

	destLinePtr = &g_flightSwRotSpriteDestLinePtr[g_flightSwRotSpriteCoeffs
							      ->destPitchDelta];
	viewportMaxY = g_flightSwRotSpriteViewportMaxY;
	--g_flightSwRotSpritePrimaryEdgeY;
	g_flightSwRotSpriteDestLinePtr = destLinePtr;
	--g_flightSwRotSpriteSecondaryEdgeY;
	if (viewportMaxY == g_flightSwRotSpritePrimaryEdgeY) {
		g_flightSwRotSpriteClipMinX = 0;
		g_flightSwRotSpriteClipMinRunIdx03 = -1;
	} else {
		if (g_flightSwRotSpritePrimaryEdgeY < 0) {
			return 0;
		}
		if (g_flightSwRotSpritePrimaryEdgeY >=
			    g_flightSwRotSpriteViewportHeight &&
		    g_flightSwRotSpriteSecondaryEdgeY <
			    g_flightSwRotSpriteViewportHeight) {
			--g_flightSwRotSpriteClipMinRunIdx03;
			g_flightSwRotSpriteClipMinX -=
				g_flightSwRotSpriteCoeffs->runLengths
					[g_flightSwRotSpriteClipMinRunIdx03];
		}
	}

	if (g_flightSwRotSpriteViewportMaxY ==
	    g_flightSwRotSpriteSecondaryEdgeY) {
		g_flightSwRotSpriteClipMaxX =
			g_flightSwRotSpriteCoeffs->scanCount - 1;
		runLengthCount = g_flightSwRotSpriteCoeffs->runLengthCount;
		minRunIndex = runLengthCount;
		--minRunIndex;
		g_flightSwRotSpriteClipMaxRunIdx03 = runLengthCount;
		g_flightSwRotSpriteClipMinRunIdx03 = minRunIndex;
		g_flightSwRotSpriteClipMinX =
			g_flightSwRotSpriteClipMaxX -
			g_flightSwRotSpriteCoeffs->runLengths
				[g_flightSwRotSpriteClipMinRunIdx03];
		++g_flightSwRotSpriteClipMinX;
		if (g_flightSwRotSpriteClipMinX < 0) {
			g_flightSwRotSpriteClipMinX = 0;
			return 1;
		}
	} else if (g_flightSwRotSpriteSecondaryEdgeY < 0) {
		--g_flightSwRotSpriteClipMaxRunIdx03;
		g_flightSwRotSpriteClipMaxX -=
			g_flightSwRotSpriteCoeffs->runLengths
				[g_flightSwRotSpriteClipMaxRunIdx03];
	}
	return 1;
}

/* Sets up the walk for octant 4: the edge steps along y, no flips. Moves the
 * edge cursor a whole edge at a time until its y is inside the viewport, moving
 * the span base a viewport height each time; puts the line start at the
 * viewport's top (g_flightSwRotSpritePrimaryEdgeY 0) in the edge's column there
 * and the far end's column in g_flightSwRotSpriteSecondaryEdgeX; then sets the
 * clip bounds and g_flightSwRotSpriteSpanBaseX. Returns 0 when the far end's
 * column is under 0, and in the modern build when the clip search finds no edge
 * point; else 1. */
// FUNCTION: XVT 0x4235D0
int FlightSw_InitRotSpriteOctant4(void)
{
	int16_t spanBaseOffset;
	int16_t clipMinValue;
	int16_t clipMaxValue;
	int16_t pointIndex;
	int16_t targetX;
	int16_t maxPointIndex;
	int16_t edgeCursorMinimum;
	int16_t *edgeDeltaYPtr;
	int16_t *edgeDeltaXPtr;

	spanBaseOffset = 0;
	if (g_flightSwRotSpriteEdgeCursorY < 0) {
		edgeDeltaXPtr =
			&g_flightSwRotSpriteCoeffs->edgePointsWithPredecessor[0]
				 .x;
		edgeDeltaYPtr =
			&g_flightSwRotSpriteCoeffs->edgePointsWithPredecessor[0]
				 .y;
		edgeCursorMinimum = 0;
		do {
			g_flightSwRotSpriteEdgeCursorX += *edgeDeltaXPtr + 1;
			g_flightSwRotSpriteEdgeCursorY += *edgeDeltaYPtr + 1;
			spanBaseOffset -= g_flightSwRotSpriteViewportHeight;
		} while (g_flightSwRotSpriteEdgeCursorY < edgeCursorMinimum);
	}
	if (g_flightSwRotSpriteEdgeCursorY >=
	    g_flightSwRotSpriteViewportHeight) {
		edgeDeltaXPtr =
			&g_flightSwRotSpriteCoeffs->edgePointsWithPredecessor[0]
				 .x;
		edgeDeltaYPtr =
			&g_flightSwRotSpriteCoeffs->edgePointsWithPredecessor[0]
				 .y;
		do {
			g_flightSwRotSpriteEdgeCursorX -= *edgeDeltaXPtr + 1;
			g_flightSwRotSpriteEdgeCursorY -= *edgeDeltaYPtr + 1;
			spanBaseOffset += g_flightSwRotSpriteViewportHeight;
		} while (g_flightSwRotSpriteEdgeCursorY >=
			 g_flightSwRotSpriteViewportHeight);
	}

	g_flightSwRotSpriteEdgeCursorX -=
		g_flightSwRotSpriteCoeffs
			->edgePointsWithPredecessor
				[g_flightSwRotSpriteEdgeCursorY + 1]
			.x;
	g_flightSwRotSpritePrimaryEdgeY = 0;
	g_flightSwRotSpritePrimaryEdgeX = g_flightSwRotSpriteEdgeCursorX;
	g_flightSwRotSpriteClipMinX = 0;
	g_flightSwRotSpriteClipMinRunIdx47 = -1;
	g_flightSwRotSpriteClipMaxRunIdx47 = g_flightSwRotSpriteViewportWidth;
	if (g_flightSwRotSpriteEdgeCursorX < 0) {
		targetX = -g_flightSwRotSpriteEdgeCursorX;
		g_flightSwRotSpriteClipMinRunIdx47 = targetX - 1;
		if (g_flightSwRotSpriteCoeffs->edgePointsWithPredecessor[0].x <
		    targetX) {
			clipMinValue = -1;
		} else {
			pointIndex = 0;
			maxPointIndex = g_flightSwRotSpriteCoeffs
						->edgePointsWithPredecessor[0]
						.y;
			while (pointIndex <= maxPointIndex) {
				if ((uint16_t)g_flightSwRotSpriteCoeffs
					    ->edgePointsWithPredecessor
						    [pointIndex + 1]
					    .x == targetX) {
					clipMinValue =
						g_flightSwRotSpriteCoeffs
							->edgePointsWithPredecessor
								[pointIndex + 1]
							.y;
					break;
				}
				++pointIndex;
			}
#ifdef XVT_MODERN
			if (pointIndex > maxPointIndex) {
				return 0;
			}
#endif
		}
		g_flightSwRotSpriteClipMinX = clipMinValue;
	}

	g_flightSwRotSpriteSecondaryEdgeX =
		g_flightSwRotSpriteCoeffs->edgePointsWithPredecessor[0].x +
		g_flightSwRotSpriteEdgeCursorX;
	g_flightSwRotSpriteSecondaryEdgeY =
		g_flightSwRotSpriteCoeffs->edgePointsWithPredecessor[0].y;
	if (g_flightSwRotSpriteSecondaryEdgeX < 0) {
		return 0;
	}

	if (g_flightSwRotSpriteSecondaryEdgeX <
	    g_flightSwRotSpriteViewportWidth) {
		clipMaxValue = g_flightSwRotSpriteCoeffs->scanCount - 1;
	} else if (g_flightSwRotSpriteEdgeCursorX <
		   g_flightSwRotSpriteViewportWidth) {
		targetX = g_flightSwRotSpriteViewportWidth -
			  g_flightSwRotSpriteEdgeCursorX;
		g_flightSwRotSpriteClipMaxRunIdx47 = targetX - 1;
		pointIndex = 0;
		maxPointIndex =
			g_flightSwRotSpriteCoeffs->edgePointsWithPredecessor[0]
				.y;
		while (pointIndex <= maxPointIndex) {
			if ((uint16_t)g_flightSwRotSpriteCoeffs
				    ->edgePointsWithPredecessor[pointIndex + 1]
				    .x == targetX) {
				clipMaxValue =
					g_flightSwRotSpriteCoeffs
						->edgePointsWithPredecessor
							[pointIndex + 1]
						.y -
					1;
				break;
			}
			++pointIndex;
		}
#ifdef XVT_MODERN
		if (pointIndex > maxPointIndex) {
			return 0;
		}
#endif
	} else {
		clipMaxValue = -1;
	}
	g_flightSwRotSpriteClipMaxX = clipMaxValue;
	g_flightSwRotSpriteSpanBaseX =
		g_flightSwRotSpriteEdgeCursorY + spanBaseOffset;
	return 1;
}

/* Steps the octant 4 walk a line: moves g_flightSwRotSpriteDestLinePtr back one
 * pixel, lowers g_flightSwRotSpritePrimaryEdgeX and
 * g_flightSwRotSpriteSecondaryEdgeX by 1 and moves the clip bounds. Returns 0
 * once the far end's column is under 0, else 1. */
// FUNCTION: XVT 0x423810
int FlightSw_StepRotSpriteOctant4(void)
{
	--g_flightSwRotSpritePrimaryEdgeX;
	g_flightSwRotSpriteDestLinePtr -= g_flightBytesPerPixel;
	if (g_flightSwRotSpritePrimaryEdgeX ==
	    g_flightSwRotSpriteViewportMaxX) {
		g_flightSwRotSpriteClipMinX = 0;
		g_flightSwRotSpriteClipMaxX = -1;
		g_flightSwRotSpriteClipMaxRunIdx47 = -1;
	} else if (g_flightSwRotSpritePrimaryEdgeX < 0) {
		++g_flightSwRotSpriteClipMinRunIdx47;
		g_flightSwRotSpriteClipMinX +=
			g_flightSwRotSpriteCoeffs->runLengths
				[g_flightSwRotSpriteClipMinRunIdx47];
	}

	--g_flightSwRotSpriteSecondaryEdgeX;
	if (g_flightSwRotSpriteSecondaryEdgeX < 0) {
		return 0;
	}
	if (g_flightSwRotSpriteSecondaryEdgeX ==
	    g_flightSwRotSpriteViewportMaxX) {
		g_flightSwRotSpriteClipMaxX =
			g_flightSwRotSpriteCoeffs->scanCount - 1;
		return 1;
	}
	if (g_flightSwRotSpriteSecondaryEdgeX >=
		    g_flightSwRotSpriteViewportWidth &&
	    g_flightSwRotSpritePrimaryEdgeX <
		    g_flightSwRotSpriteViewportWidth) {
		++g_flightSwRotSpriteClipMaxRunIdx47;
		g_flightSwRotSpriteClipMaxX +=
			g_flightSwRotSpriteCoeffs->runLengths
				[g_flightSwRotSpriteClipMaxRunIdx47];
	}
	return 1;
}

/* Sets up the walk for octant 5 (edge along y, flipY) the way
 * FlightSw_InitRotSpriteOctant4 does for octant 4, with the line start at the
 * viewport's bottom (g_flightSwRotSpritePrimaryEdgeY of
 * g_flightSwRotSpriteViewportMaxY). Returns 0 when the line start's column is
 * at or past the viewport width, else 1. */
// FUNCTION: XVT 0x423900
int FlightSw_InitRotSpriteOctant5(void)
{
	int16_t spanBaseOffset;
	int16_t *edgeDeltaYPtr;
	int16_t *edgeDeltaXPtr;
	int16_t clipValue;
	int16_t targetX;
	int16_t pointIndex;

	spanBaseOffset = 0;
	while (g_flightSwRotSpriteEdgeCursorY < 0) {
		g_flightSwRotSpriteEdgeCursorX -=
			g_flightSwRotSpriteCoeffs->edgePointsWithPredecessor[0]
				.x +
			1;
		spanBaseOffset += g_flightSwRotSpriteViewportHeight;
		g_flightSwRotSpriteEdgeCursorY +=
			g_flightSwRotSpriteCoeffs->edgePointsWithPredecessor[0]
				.y +
			1;
	}
	while (g_flightSwRotSpriteEdgeCursorY >=
	       g_flightSwRotSpriteViewportHeight) {
		spanBaseOffset -= g_flightSwRotSpriteViewportHeight;
		g_flightSwRotSpriteEdgeCursorX +=
			g_flightSwRotSpriteCoeffs->edgePointsWithPredecessor[0]
				.x +
			1;
		g_flightSwRotSpriteEdgeCursorY -=
			g_flightSwRotSpriteCoeffs->edgePointsWithPredecessor[0]
				.y +
			1;
	}

	edgeDeltaYPtr =
		&g_flightSwRotSpriteCoeffs->edgePointsWithPredecessor[0].y;
	g_flightSwRotSpriteEdgeCursorX -=
		g_flightSwRotSpriteCoeffs
			->edgePointsWithPredecessor
				[(int16_t)(g_flightSwRotSpriteViewportMaxY -
					   g_flightSwRotSpriteEdgeCursorY) +
				 1]
			.x;
	g_flightSwRotSpritePrimaryEdgeY = g_flightSwRotSpriteViewportMaxY;
	g_flightSwRotSpritePrimaryEdgeX = g_flightSwRotSpriteEdgeCursorX;
	g_flightSwRotSpriteSecondaryEdgeY =
		g_flightSwRotSpriteViewportMaxY - *edgeDeltaYPtr;
	edgeDeltaXPtr =
		&g_flightSwRotSpriteCoeffs->edgePointsWithPredecessor[0].x;
	g_flightSwRotSpriteClipMinX = 0;
	g_flightSwRotSpriteClipMinRunIdx47 = -1;
	g_flightSwRotSpriteSecondaryEdgeX =
		g_flightSwRotSpriteEdgeCursorX + *edgeDeltaXPtr;
	g_flightSwRotSpriteClipMaxRunIdx47 = g_flightSwRotSpriteViewportWidth;
	if (g_flightSwRotSpriteEdgeCursorX >=
	    g_flightSwRotSpriteViewportWidth) {
		return 0;
	}

	if (g_flightSwRotSpriteEdgeCursorX < 0) {
		if (g_flightSwRotSpriteSecondaryEdgeX < 0) {
			g_flightSwRotSpriteSpanBaseX =
				g_flightSwRotSpriteViewportMaxY -
				g_flightSwRotSpriteEdgeCursorY + spanBaseOffset;
			g_flightSwRotSpriteClipMaxX = -1;
			return 1;
		}
		targetX = -g_flightSwRotSpriteEdgeCursorX;
		g_flightSwRotSpriteClipMinRunIdx47 = targetX;
		if (*edgeDeltaXPtr < targetX) {
			clipValue = -1;
		} else {
			pointIndex = 0;
			clipValue = targetX;
			if (*edgeDeltaYPtr >= 0) {
				do {
					if ((uint16_t)g_flightSwRotSpriteCoeffs
						    ->edgePointsWithPredecessor
							    [pointIndex + 1]
						    .x == targetX) {
						clipValue =
							g_flightSwRotSpriteCoeffs
								->edgePointsWithPredecessor
									[pointIndex +
									 1]
								.y;
						break;
					}
					++pointIndex;
				} while (pointIndex <= *edgeDeltaYPtr);
			}
		}
		g_flightSwRotSpriteClipMinX = clipValue;
	}

	if (g_flightSwRotSpriteSecondaryEdgeX >=
	    g_flightSwRotSpriteViewportWidth) {
		if (g_flightSwRotSpriteEdgeCursorX <
		    g_flightSwRotSpriteViewportWidth) {
			targetX = g_flightSwRotSpriteViewportWidth -
				  g_flightSwRotSpriteEdgeCursorX;
			g_flightSwRotSpriteClipMaxRunIdx47 = targetX;
			pointIndex = 0;
			clipValue = targetX;
			if (*edgeDeltaYPtr >= 0) {
				do {
					if ((uint16_t)g_flightSwRotSpriteCoeffs
						    ->edgePointsWithPredecessor
							    [pointIndex + 1]
						    .x == targetX) {
						clipValue =
							g_flightSwRotSpriteCoeffs
								->edgePointsWithPredecessor
									[pointIndex +
									 1]
								.y -
							1;
						break;
					}
					++pointIndex;
				} while (pointIndex <= *edgeDeltaYPtr);
			}
		} else {
			clipValue = -1;
		}
	} else if (g_flightSwRotSpriteSecondaryEdgeX < 0) {
		clipValue = -1;
	} else {
		clipValue = g_flightSwRotSpriteCoeffs->scanCount - 1;
	}
	g_flightSwRotSpriteClipMaxX = clipValue;
	g_flightSwRotSpriteSpanBaseX = g_flightSwRotSpriteViewportMaxY -
				       g_flightSwRotSpriteEdgeCursorY +
				       spanBaseOffset;
	return 1;
}

/* Steps the octant 5 walk a line: moves g_flightSwRotSpriteDestLinePtr on one
 * pixel, raises both edge columns by 1 and moves the clip bounds. Returns 0
 * once the line start's column reaches the viewport width, else 1. */
// FUNCTION: XVT 0x423B90
int FlightSw_StepRotSpriteOctant5(void)
{
	g_flightSwRotSpriteDestLinePtr += g_flightBytesPerPixel;
	++g_flightSwRotSpritePrimaryEdgeX;
	if (g_flightSwRotSpritePrimaryEdgeX == 0) {
		g_flightSwRotSpriteClipMinX = 0;
	} else if (g_flightSwRotSpritePrimaryEdgeX < 0) {
		--g_flightSwRotSpriteClipMinRunIdx47;
		g_flightSwRotSpriteClipMinX -=
			g_flightSwRotSpriteCoeffs->runLengths
				[g_flightSwRotSpriteClipMinRunIdx47];
		if (g_flightSwRotSpriteClipMinX < 0) {
			g_flightSwRotSpriteClipMinX = 0;
		}
	} else if (g_flightSwRotSpritePrimaryEdgeX >=
		   g_flightSwRotSpriteViewportWidth) {
		return 0;
	}

	++g_flightSwRotSpriteSecondaryEdgeX;
	if (g_flightSwRotSpriteSecondaryEdgeX == 0) {
		g_flightSwRotSpriteClipMaxX =
			g_flightSwRotSpriteCoeffs->scanCount - 1;
		g_flightSwRotSpriteClipMinRunIdx47 =
			g_flightSwRotSpriteCoeffs->runLengthCount;
		--g_flightSwRotSpriteClipMinRunIdx47;
		g_flightSwRotSpriteClipMinX =
			g_flightSwRotSpriteViewportHeight -
			g_flightSwRotSpriteCoeffs->runLengths
				[g_flightSwRotSpriteClipMinRunIdx47];
		if (g_flightSwRotSpriteClipMinX < 0) {
			g_flightSwRotSpriteClipMinX = 0;
			return 1;
		}
	} else if (g_flightSwRotSpriteSecondaryEdgeX >=
		   g_flightSwRotSpriteViewportWidth) {
		if (g_flightSwRotSpriteSecondaryEdgeX ==
		    g_flightSwRotSpriteViewportWidth) {
			g_flightSwRotSpriteClipMaxX =
				g_flightSwRotSpriteViewportMaxY;
			g_flightSwRotSpriteClipMaxRunIdx47 =
				g_flightSwRotSpriteCoeffs->runLengthCount;
		}
		--g_flightSwRotSpriteClipMaxRunIdx47;
		g_flightSwRotSpriteClipMaxX -=
			g_flightSwRotSpriteCoeffs->runLengths
				[g_flightSwRotSpriteClipMaxRunIdx47];
	}
	return 1;
}

/* Sets up the walk for octant 6 (edge along y, flipX), with the line start at
 * the viewport's top. Returns 0 when the line start's column is under 0, else
 * 1. */
// FUNCTION: XVT 0x423CD0
int FlightSw_InitRotSpriteOctant6(void)
{
	int16_t spanBaseOffset;
	int16_t clipValue;
	int16_t targetX;
	int16_t pointIndex;
	int16_t edgeDeltaY;
	int16_t *edgeDeltaXPtr;
	int16_t *edgeDeltaYPtr;

	spanBaseOffset = 0;
	while (g_flightSwRotSpriteEdgeCursorY < 0) {
		g_flightSwRotSpriteEdgeCursorX -=
			g_flightSwRotSpriteCoeffs->edgePointsWithPredecessor[0]
				.x +
			1;
		spanBaseOffset -= g_flightSwRotSpriteViewportHeight;
		g_flightSwRotSpriteEdgeCursorY +=
			g_flightSwRotSpriteCoeffs->edgePointsWithPredecessor[0]
				.y +
			1;
	}
	while (g_flightSwRotSpriteEdgeCursorY >=
	       g_flightSwRotSpriteViewportHeight) {
		spanBaseOffset += g_flightSwRotSpriteViewportHeight;
		g_flightSwRotSpriteEdgeCursorX +=
			g_flightSwRotSpriteCoeffs->edgePointsWithPredecessor[0]
				.x +
			1;
		g_flightSwRotSpriteEdgeCursorY -=
			g_flightSwRotSpriteCoeffs->edgePointsWithPredecessor[0]
				.y +
			1;
	}

	g_flightSwRotSpriteEdgeCursorX +=
		g_flightSwRotSpriteCoeffs
			->edgePointsWithPredecessor
				[g_flightSwRotSpriteEdgeCursorY + 1]
			.x;
	edgeDeltaXPtr =
		&g_flightSwRotSpriteCoeffs->edgePointsWithPredecessor[0].x;
	edgeDeltaYPtr =
		&g_flightSwRotSpriteCoeffs->edgePointsWithPredecessor[0].y;
	g_flightSwRotSpritePrimaryEdgeY = 0;
	g_flightSwRotSpritePrimaryEdgeX = g_flightSwRotSpriteEdgeCursorX;
	g_flightSwRotSpriteSecondaryEdgeX =
		g_flightSwRotSpriteEdgeCursorX - *edgeDeltaXPtr;
	g_flightSwRotSpriteSecondaryEdgeY = *edgeDeltaYPtr;
	g_flightSwRotSpriteClipMinX = -1;
	g_flightSwRotSpriteClipMinRunIdx47 = -1;
	g_flightSwRotSpriteClipMaxX = -1;
	g_flightSwRotSpriteClipMaxRunIdx47 =
		g_flightSwRotSpriteCoeffs->runLengthCount;
	if (g_flightSwRotSpriteEdgeCursorX < 0) {
		return 0;
	}

	clipValue = -1;
	if (g_flightSwRotSpriteEdgeCursorX < g_flightSwRotSpriteViewportWidth) {
		clipValue = 0;
	} else {
		targetX = g_flightSwRotSpriteEdgeCursorX -
			  g_flightSwRotSpriteViewportMaxX;
		g_flightSwRotSpriteClipMinRunIdx47 = targetX;
		if (*edgeDeltaXPtr >= targetX) {
			pointIndex = 0;
			edgeDeltaY = *edgeDeltaYPtr;
			while (pointIndex <= edgeDeltaY &&
			       (uint16_t)g_flightSwRotSpriteCoeffs
					       ->edgePointsWithPredecessor
						       [pointIndex + 1]
					       .x != targetX) {
				++pointIndex;
			}
			if (pointIndex <= edgeDeltaY) {
				clipValue = g_flightSwRotSpriteCoeffs
						    ->edgePointsWithPredecessor
							    [pointIndex + 1]
						    .y;
			}
		}
	}
	g_flightSwRotSpriteClipMinX = clipValue;

	if (g_flightSwRotSpriteViewportWidth <=
	    g_flightSwRotSpriteSecondaryEdgeX) {
		clipValue = -1;
	} else if (g_flightSwRotSpriteSecondaryEdgeX >= 0 &&
		   g_flightSwRotSpriteViewportWidth >
			   g_flightSwRotSpriteSecondaryEdgeX) {
		clipValue = g_flightSwRotSpriteCoeffs->scanCount - 1;
	} else {
		targetX = g_flightSwRotSpriteSecondaryEdgeX + *edgeDeltaXPtr;
		g_flightSwRotSpriteClipMaxRunIdx47 = targetX + 1;
		pointIndex = *edgeDeltaYPtr;
		while (pointIndex >= 0 &&
		       (uint16_t)g_flightSwRotSpriteCoeffs
				       ->edgePointsWithPredecessor[pointIndex +
								   1]
				       .x != targetX) {
			--pointIndex;
		}
		clipValue = -1;
		if (pointIndex >= 0) {
			clipValue =
				g_flightSwRotSpriteCoeffs
					->edgePointsWithPredecessor[pointIndex +
								    1]
					.y;
		}
	}

	g_flightSwRotSpriteClipMaxX = clipValue;
	g_flightSwRotSpriteSpanBaseX =
		spanBaseOffset + g_flightSwRotSpriteEdgeCursorY;
	return 1;
}

/* Steps the octant 6 walk a line as FlightSw_StepRotSpriteOctant4 does. Returns
 * 0 once the line start's column is under 0, else 1. */
// FUNCTION: XVT 0x423F30
int FlightSw_StepRotSpriteOctant6(void)
{
	--g_flightSwRotSpriteSecondaryEdgeX;
	g_flightSwRotSpriteDestLinePtr -= g_flightBytesPerPixel;
	--g_flightSwRotSpritePrimaryEdgeX;
	if (g_flightSwRotSpritePrimaryEdgeX < 0) {
		return 0;
	}
	if (g_flightSwRotSpritePrimaryEdgeX ==
	    g_flightSwRotSpriteViewportMaxX) {
		g_flightSwRotSpriteClipMinX = 0;
	} else if (g_flightSwRotSpritePrimaryEdgeX >=
			   g_flightSwRotSpriteViewportWidth &&
		   g_flightSwRotSpriteSecondaryEdgeX <
			   g_flightSwRotSpriteViewportWidth) {
		--g_flightSwRotSpriteClipMinRunIdx47;
		g_flightSwRotSpriteClipMinX -=
			g_flightSwRotSpriteCoeffs->runLengths
				[g_flightSwRotSpriteClipMinRunIdx47];
		if (g_flightSwRotSpriteClipMinX < 0) {
			g_flightSwRotSpriteClipMinX = 0;
		}
	}

	if (g_flightSwRotSpriteViewportMaxX ==
	    g_flightSwRotSpriteSecondaryEdgeX) {
		g_flightSwRotSpriteClipMaxX =
			g_flightSwRotSpriteCoeffs->scanCount - 1;
		g_flightSwRotSpriteClipMinRunIdx47 =
			g_flightSwRotSpriteCoeffs->runLengthCount - 1;
		g_flightSwRotSpriteClipMinX =
			g_flightSwRotSpriteClipMaxX -
			g_flightSwRotSpriteCoeffs->runLengths
				[g_flightSwRotSpriteClipMinRunIdx47] +
			1;
		if (g_flightSwRotSpriteClipMinX < 0) {
			g_flightSwRotSpriteClipMinX = 0;
			return 1;
		}
	} else if (g_flightSwRotSpriteSecondaryEdgeX < 0) {
		--g_flightSwRotSpriteClipMaxRunIdx47;
		g_flightSwRotSpriteClipMaxX -=
			g_flightSwRotSpriteCoeffs->runLengths
				[g_flightSwRotSpriteClipMaxRunIdx47];
		if (g_flightSwRotSpriteClipMaxX < 0) {
			g_flightSwRotSpriteClipMaxX = 0;
		}
	}
	return 1;
}

/* Sets up the walk for octant 7 (edge along y, flipX and flipY), with the line
 * start at the viewport's bottom. Returns 0 when the far end's column is at or
 * past the viewport width, else 1. */
// FUNCTION: XVT 0x424060
int FlightSw_InitRotSpriteOctant7(void)
{
	int16_t spanBaseOffset;
	int16_t *edgeDeltaXPtr;
	int16_t *edgeDeltaYPtr;
	int16_t clipValue;
	int16_t pointIndex;
	int16_t targetX;

	spanBaseOffset = 0;
	while (g_flightSwRotSpriteEdgeCursorY < 0) {
		g_flightSwRotSpriteEdgeCursorX +=
			g_flightSwRotSpriteCoeffs->edgePointsWithPredecessor[0]
				.x +
			1;
		spanBaseOffset += g_flightSwRotSpriteViewportHeight;
		g_flightSwRotSpriteEdgeCursorY +=
			g_flightSwRotSpriteCoeffs->edgePointsWithPredecessor[0]
				.y +
			1;
	}
	while (g_flightSwRotSpriteEdgeCursorY >=
	       g_flightSwRotSpriteViewportHeight) {
		spanBaseOffset -= g_flightSwRotSpriteViewportHeight;
		g_flightSwRotSpriteEdgeCursorX -=
			g_flightSwRotSpriteCoeffs->edgePointsWithPredecessor[0]
				.x +
			1;
		g_flightSwRotSpriteEdgeCursorY -=
			g_flightSwRotSpriteCoeffs->edgePointsWithPredecessor[0]
				.y +
			1;
	}

	edgeDeltaXPtr =
		&g_flightSwRotSpriteCoeffs->edgePointsWithPredecessor[0].x;
	g_flightSwRotSpriteEdgeCursorX +=
		g_flightSwRotSpriteCoeffs
			->edgePointsWithPredecessor
				[g_flightSwRotSpriteViewportMaxY -
				 g_flightSwRotSpriteEdgeCursorY + 1]
			.x;
	g_flightSwRotSpritePrimaryEdgeY = g_flightSwRotSpriteViewportMaxY;
	g_flightSwRotSpritePrimaryEdgeX = g_flightSwRotSpriteEdgeCursorX;
	edgeDeltaYPtr =
		&g_flightSwRotSpriteCoeffs->edgePointsWithPredecessor[0].y;
	g_flightSwRotSpriteSecondaryEdgeX =
		g_flightSwRotSpriteEdgeCursorX - *edgeDeltaXPtr;
	g_flightSwRotSpriteSecondaryEdgeY =
		g_flightSwRotSpriteViewportMaxY - *edgeDeltaYPtr;
	g_flightSwRotSpriteClipMinRunIdx47 = -1;
	g_flightSwRotSpriteClipMaxRunIdx47 = g_flightSwRotSpriteViewportWidth;

	clipValue = -1;
	if (g_flightSwRotSpriteEdgeCursorX >= 0) {
		if (g_flightSwRotSpriteEdgeCursorX <
		    g_flightSwRotSpriteViewportWidth) {
			clipValue = 0;
		} else {
			targetX = g_flightSwRotSpriteEdgeCursorX -
				  g_flightSwRotSpriteViewportMaxX;
			g_flightSwRotSpriteClipMinRunIdx47 = targetX - 1;
			if (*edgeDeltaXPtr >= targetX) {
				pointIndex = 0;
				if (*edgeDeltaYPtr >= 0) {
					while ((uint16_t)g_flightSwRotSpriteCoeffs
						       ->edgePointsWithPredecessor
							       [pointIndex + 1]
						       .x != targetX) {
						++pointIndex;
						if (pointIndex >
						    *edgeDeltaYPtr) {
							break;
						}
					}
				}
				if (pointIndex <= *edgeDeltaYPtr) {
					clipValue =
						g_flightSwRotSpriteCoeffs
							->edgePointsWithPredecessor
								[pointIndex + 1]
							.y;
				}
			}
		}
	}
	g_flightSwRotSpriteClipMinX = clipValue;

	if (g_flightSwRotSpriteSecondaryEdgeX >=
	    g_flightSwRotSpriteViewportWidth) {
		return 0;
	}
	if (g_flightSwRotSpriteSecondaryEdgeX >= 0) {
		clipValue = g_flightSwRotSpriteCoeffs->scanCount - 1;
	} else if (g_flightSwRotSpriteEdgeCursorX < 0) {
		clipValue = -1;
	} else {
		targetX = g_flightSwRotSpriteSecondaryEdgeX + *edgeDeltaXPtr;
		g_flightSwRotSpriteClipMaxRunIdx47 = targetX;
		pointIndex = *edgeDeltaYPtr;
		if (pointIndex >= 0) {
			while ((uint16_t)g_flightSwRotSpriteCoeffs
				       ->edgePointsWithPredecessor[pointIndex +
								   1]
				       .x != targetX) {
				--pointIndex;
				if (pointIndex < 0) {
					break;
				}
			}
		}
		clipValue = -1;
		if (pointIndex >= 0) {
			clipValue =
				g_flightSwRotSpriteCoeffs
					->edgePointsWithPredecessor[pointIndex +
								    1]
					.y;
		}
	}

	g_flightSwRotSpriteClipMaxX = clipValue;
	g_flightSwRotSpriteSpanBaseX = g_flightSwRotSpriteViewportMaxY -
				       g_flightSwRotSpriteEdgeCursorY +
				       spanBaseOffset;
	return 1;
}

/* Steps the octant 7 walk a line as FlightSw_StepRotSpriteOctant5 does. Returns
 * 0 once the far end's column reaches the viewport width, else 1. */
// FUNCTION: XVT 0x4242E0
int FlightSw_StepRotSpriteOctant7(void)
{
	g_flightSwRotSpriteDestLinePtr += g_flightBytesPerPixel;
	++g_flightSwRotSpritePrimaryEdgeX;
	if (g_flightSwRotSpritePrimaryEdgeX == 0) {
		g_flightSwRotSpriteClipMinX = 0;
		g_flightSwRotSpriteClipMaxX = -1;
		g_flightSwRotSpriteClipMaxRunIdx47 = -1;
	} else if (g_flightSwRotSpritePrimaryEdgeX >=
		   g_flightSwRotSpriteViewportWidth) {
		if (g_flightSwRotSpritePrimaryEdgeX ==
		    g_flightSwRotSpriteViewportWidth) {
			g_flightSwRotSpriteClipMinRunIdx47 = -1;
		}
		++g_flightSwRotSpriteClipMinRunIdx47;
		g_flightSwRotSpriteClipMinX +=
			g_flightSwRotSpriteCoeffs->runLengths
				[g_flightSwRotSpriteClipMinRunIdx47];
	}

	++g_flightSwRotSpriteSecondaryEdgeX;
	if (g_flightSwRotSpriteSecondaryEdgeX == 0) {
		g_flightSwRotSpriteClipMaxX =
			g_flightSwRotSpriteCoeffs->scanCount - 1;
		return 1;
	}
	if (g_flightSwRotSpriteSecondaryEdgeX >=
	    g_flightSwRotSpriteViewportWidth) {
		return 0;
	}
	if (g_flightSwRotSpriteSecondaryEdgeX < 0 &&
	    g_flightSwRotSpritePrimaryEdgeX >= 0) {
		++g_flightSwRotSpriteClipMaxRunIdx47;
		g_flightSwRotSpriteClipMaxX +=
			g_flightSwRotSpriteCoeffs->runLengths
				[g_flightSwRotSpriteClipMaxRunIdx47];
	}
	return 1;
}

/* Sets g_flightSwRotSpriteDestBuffer to bufferAddress and returns it. */
// FUNCTION: XVT 0x426C50
uint8_t *FlightSw_SetRotatedSpriteDestBuffer(uint8_t *bufferAddress)
{
	return g_flightSwRotSpriteDestBuffer = bufferAddress;
}

/* Sets the flight viewport to requestedWidth by requestedHeight at byte offset
 * requestedBaseOffset in the surface: the width, height, last column and row
 * and centers (halves, rounded down), g_flightVpBaseOffset, and g_flightVpY and
 * g_flightVpX from the offset and g_surfacePitch, x in pixels. Returns
 * g_flightVpX. Would halve the size and add 120 rows and 160 bytes to the
 * offset when g_flightViewportInsetX is 160, which it never is. Ignores
 * viewportMode. */
// FUNCTION: XVT 0x426C60
unsigned int SetFlightViewport(unsigned int requestedWidth,
			       unsigned int requestedHeight, int viewportMode,
			       unsigned int requestedBaseOffset)
{
	unsigned int width;
	unsigned int height;
	unsigned int baseOffset;
	int pitch;

	(void)viewportMode;

	if (g_flightViewportInsetX == 160) {
		width = requestedWidth >> 1;
		height = requestedHeight >> 1;
		pitch = g_surfacePitch;
		baseOffset = requestedBaseOffset + 120 * pitch + 160;
	} else {
		width = requestedWidth;
		height = requestedHeight;
		baseOffset = requestedBaseOffset;
		pitch = g_surfacePitch;
	}

	g_flightVpWidth = width;
	g_flightVpMaxX = width - 1;
	g_flightVpCenterX = width >> 1;
	g_flightVpHeight = height;
	g_flightVpMaxY = height - 1;
	g_flightVpCenterY = height >> 1;
	g_flightVpBaseOffset = baseOffset;
	g_flightVpY = baseOffset / pitch;
	return g_flightVpX =
		       baseOffset % pitch / (unsigned int)g_flightBytesPerPixel;
}

/* Copies g_flightVpHeight rows of g_flightVpWidth bytes, packed in srcPixels,
 * into the viewport on the frame buffer, g_surfacePitch apart. Nothing calls
 * this. */
// FUNCTION: XVT 0x426D50
void FlightSw_CopyLegacy8BitViewportToFramebuffer(const uint8_t *srcPixels)
{
	int row;
	uint8_t *dstPixels;
	uint16_t rowWidth;
	const uint8_t **srcCursor;
	unsigned int copyWidth;
	unsigned int advanceWidth;
	int viewportX;
	int viewportY;

	row = 0;
	srcCursor = &srcPixels;
	viewportX = g_flightVpX;
	viewportY = g_flightVpY;
	dstPixels = g_flightSwFramebufferBase;
	dstPixels += g_flightBytesPerPixel * viewportX;
	dstPixels += g_surfacePitch * viewportY;
	if (g_flightVpHeight != 0) {
		rowWidth = g_flightVpWidth;
		do {
			++row;
			copyWidth = rowWidth;
			memcpy(dstPixels, *srcCursor, copyWidth);
			dstPixels += g_surfacePitch;
			advanceWidth = rowWidth;
			*srcCursor += advanceWidth;
		} while (row < g_flightVpHeight);
	}
}

/* Saves the viewport and camera matrix in g_savedFlightViewport (the modern
 * build also saves its own camera state), sets the viewport to width by height
 * at baseOffset as SetFlightViewport does, without the inset, and sets
 * g_viewportSpanMaskOffset to the second mask, 0xE000. Returns g_flightVpX.
 * Ignores refreshSpanMask. */
// FUNCTION: XVT 0x426F40
unsigned int PushFlightViewport(uint16_t width, uint16_t height,
				int16_t refreshSpanMask,
				unsigned int baseOffset)
{
	(void)refreshSpanMask;
#ifdef XVT_MODERN
	XvtRenderCamera_SaveViewport();
#endif

	g_savedFlightViewport.width = g_flightVpWidth;
	g_savedFlightViewport.height = g_flightVpHeight;
	g_savedFlightViewport.baseOffset = (uint16_t)g_flightVpBaseOffset;
	g_savedFlightViewport.viewportY = (uint16_t)g_flightVpY;
	g_savedFlightViewport.viewportX = (uint16_t)g_flightVpX;
	g_savedFlightViewport.camMatR0_X = g_camMatR0_X;
	g_savedFlightViewport.camMatR1_X = g_camMatR1_X;
	g_savedFlightViewport.camMatR2_X = g_camMatR2_X;
	g_savedFlightViewport.camMatR0_Y = g_camMatR0_Y;
	g_savedFlightViewport.camMatR1_Y = g_camMatR1_Y;
	g_savedFlightViewport.camMatR2_Y = g_camMatR2_Y;
	g_savedFlightViewport.camMatR0_Z = g_camMatR0_Z;
	g_savedFlightViewport.camMatR1_Z = g_camMatR1_Z;
	g_savedFlightViewport.camMatR2_Z = g_camMatR2_Z;

	g_flightVpWidth = width;
	g_flightVpMaxX = width - 1;
	g_flightVpCenterX = width >> 1;
	g_flightVpHeight = height;
	g_flightVpMaxY = height - 1;
	{
		unsigned int remainder;
		int pitch;

		pitch = g_surfacePitch;
		g_flightVpCenterY = height >> 1;
		g_flightVpBaseOffset = baseOffset;
		g_flightVpY = baseOffset / (unsigned int)pitch;
		remainder = baseOffset % (unsigned int)pitch;
		g_flightVpX = remainder / (unsigned int)g_flightBytesPerPixel;
		g_viewportSpanMaskOffset = 0xE000;
	}
	return (unsigned int)g_flightVpX;
}

/* Puts back the camera matrix and viewport PushFlightViewport saved (the modern
 * build also restores its own camera state), with g_flightVpBaseOffset cut to
 * 16 bits, and sets g_viewportSpanMaskOffset back to 0xC000. Returns
 * g_flightVpX. */
// FUNCTION: XVT 0x427070
int PopFlightViewport(void)
{
	g_camMatR0_X = g_savedFlightViewport.camMatR0_X;
	g_camMatR1_X = g_savedFlightViewport.camMatR1_X;
	g_camMatR2_X = g_savedFlightViewport.camMatR2_X;
	g_camMatR0_Y = g_savedFlightViewport.camMatR0_Y;
	g_camMatR1_Y = g_savedFlightViewport.camMatR1_Y;
	g_camMatR2_Y = g_savedFlightViewport.camMatR2_Y;
	g_camMatR0_Z = g_savedFlightViewport.camMatR0_Z;
	g_camMatR1_Z = g_savedFlightViewport.camMatR1_Z;
	g_camMatR2_Z = g_savedFlightViewport.camMatR2_Z;
#ifdef XVT_MODERN
	XvtRenderCamera_RestoreViewport();
#endif
	g_flightVpWidth = g_savedFlightViewport.width;
	g_flightVpMaxX = g_savedFlightViewport.width - 1;
	g_flightVpCenterX = g_savedFlightViewport.width >> 1;
	g_flightVpHeight = g_savedFlightViewport.height;
	g_flightVpMaxY = g_savedFlightViewport.height - 1;
	g_flightVpCenterY = g_savedFlightViewport.height >> 1;
	g_flightVpBaseOffset = g_savedFlightViewport.baseOffset;
	g_viewportSpanMaskOffset = 0xC000;
	g_flightVpY = g_savedFlightViewport.viewportY;
	return g_flightVpX = g_savedFlightViewport.viewportX;
}

/* Copies a widthPixels by heightPixels block from sourceBase (sourcePitch bytes
 * per row, from sourceX, sourceY) to destinationX, destinationY on the frame
 * buffer, g_flightBytesPerPixel bytes per pixel. With transparentColorIndex
 * 0xFFFF every pixel is copied; otherwise pixels equal to that index, or at 16
 * bits to its palette color, are skipped. Does not clip. */
// FUNCTION: XVT 0x427150
void FlightSw_BlitRectToFlightSurface(
	uint8_t *sourceBase, uint16_t transparentColorIndex, uint16_t sourceX,
	uint16_t sourceY, uint16_t destinationX, uint16_t destinationY,
	uint16_t widthPixels, uint16_t heightPixels, uint16_t sourcePitch)
{
	unsigned int transparentColor;
	int destinationOffset;
	uint8_t *source;
	uint8_t *destination;
	int rowsRemaining;
	int columnsRemaining;

	destinationOffset = g_surfacePitch * destinationY +
			    g_flightBytesPerPixel * destinationX;
	if (g_flightBytesPerPixel == 1) {
		transparentColor = (transparentColorIndex << 24) |
				   (transparentColorIndex << 16) |
				   (transparentColorIndex << 8) |
				   transparentColorIndex;
	} else {
		transparentColor = g_flightPalette16Bpp[transparentColorIndex];
	}
	destination = g_flightSwFramebufferBase + destinationOffset;
	source = sourceBase + sourcePitch * sourceY +
		 g_flightBytesPerPixel * sourceX;
	if (transparentColorIndex == 0xFFFF) {
		if (heightPixels != 0) {
			rowsRemaining = heightPixels;
			do {
				memcpy(destination, source,
				       widthPixels * g_flightBytesPerPixel);
				destination += g_surfacePitch;
				source += sourcePitch;
				--rowsRemaining;
			} while (rowsRemaining != 0);
		}
	} else if (heightPixels != 0) {
		rowsRemaining = heightPixels;
		do {
			if (widthPixels != 0) {
				columnsRemaining = widthPixels;
				do {
					if (g_flightBytesPerPixel == 1) {
						if ((uint8_t)transparentColor !=
						    *source) {
							*destination = *source;
						}
					} else if (g_flightBytesPerPixel == 2 &&
						   (uint16_t)transparentColor !=
							   *(uint16_t *)
								   source) {
						*(uint16_t *)destination =
							*(uint16_t *)source;
					}
					source += g_flightBytesPerPixel;
					destination += g_flightBytesPerPixel;
					--columnsRemaining;
				} while (columnsRemaining != 0);
			}
			destination += g_surfacePitch -
				       widthPixels * g_flightBytesPerPixel;
			source += sourcePitch -
				  widthPixels * g_flightBytesPerPixel;
			--rowsRemaining;
		} while (rowsRemaining != 0);
	}
}

/* Copies a widthPixels by heightPixels block from srcX, srcY on the frame
 * buffer to dstX, dstY in dstPixels, dstPitchBytes per row. Nothing calls
 * this. */
// FUNCTION: XVT 0x4272D0
void FlightSw_CopyFramebufferRectToBuffer(uint8_t *dstPixels, uint16_t srcX,
					  uint16_t srcY, uint16_t dstX,
					  uint16_t dstY, uint16_t widthPixels,
					  uint16_t heightPixels,
					  uint16_t dstPitchBytes)
{
	uint8_t *source;
	uint8_t *destination;
	int rowsRemaining;

	source = g_flightSwFramebufferBase + g_surfacePitch * srcY +
		 g_flightBytesPerPixel * srcX;
	destination =
		dstPixels + dstPitchBytes * dstY + g_flightBytesPerPixel * dstX;
	if (heightPixels != 0) {
		rowsRemaining = heightPixels;
		do {
			memcpy(destination, source,
			       widthPixels * g_flightBytesPerPixel);
			source += g_surfacePitch;
			destination += dstPitchBytes;
			--rowsRemaining;
		} while (rowsRemaining != 0);
	}
}

/* Fills row y from xStart up to, not including, xEnd with colorIndex (its
 * palette color at 16 bits), all measured from the g_flightClip rectangle's top
 * left; draws nothing when xEnd is not past xStart. Does not clip. */
// FUNCTION: XVT 0x4377B0
void FlightSw_DrawHorizontalColorSpan(int xStart, int xEnd, int y,
				      uint8_t colorIndex)
{
	int framebufferXEnd;
	int framebufferXStart;
	int framebufferY;
	uint8_t *rowBase;

	framebufferXStart = g_flightClipLeft + xStart;
	framebufferXEnd = g_flightClipLeft + xEnd;
	framebufferY = g_flightClipTop + y;

	if (g_flightBytesPerPixel == 2) {
		uint16_t color;
		uint16_t *destination;
		uint8_t *framebufferBase;

		framebufferBase = g_flightSwFramebufferBase;
		rowBase = framebufferBase + g_surfacePitch * framebufferY;
		color = g_flightPalette16Bpp[colorIndex];
		if (framebufferXEnd <= framebufferXStart) {
			return;
		}
		destination = (uint16_t *)rowBase + framebufferXStart;
		while (framebufferXStart < framebufferXEnd) {
			*destination++ = color;
			++framebufferXStart;
		}
	} else {
		rowBase = g_flightSwFramebufferBase +
			  g_surfacePitch * framebufferY;
		if (framebufferXEnd <= framebufferXStart) {
			return;
		}
		memset(rowBase + framebufferXStart, colorIndex,
		       (size_t)(framebufferXEnd - framebufferXStart));
	}
}

/* Copies a span mask of height rows, width pixels each, from encodedMask into
 * g_flightAuxBuffer at g_viewportSpanMaskOffset, in the form
 * RenderScene_Initialize reads: per row a signed first byte, then run lengths,
 * where a 0 adds 255 to the next byte and 0, 0 adds 511. The source writes a
 * long run as 0 and a byte b, meaning 256 + b; when g_screenWidth is not 320, b
 * of 0 is followed by a byte c, meaning 512 + c, and b of 0xFF means 511. With
 * mirrorHorizontal each row's runs are reversed, and its first byte negated
 * when it has an even number of runs. Calls nullsub_2 in the hardware path. */
// FUNCTION: XVT 0x442090
void FlightSw_CopyViewportSpanMaskRle(const uint8_t *encodedMask,
				      uint16_t width, uint16_t height,
				      int16_t mirrorHorizontal)
{
	uint8_t *destination;
	uint8_t rowStartParity;
	uint16_t decodedWidth;
	uint8_t encodedRun;
	uint8_t extendedRun;
	int runLength;
	uint16_t mirrorDecodedWidth;
	uint8_t *tempWrite;
	uint16_t reversedDecodedWidth;
	uint8_t *tempRead;
	int16_t runCount;
	unsigned int reverseIndex;
	uint8_t reverseRun;
	uint16_t rowsRemaining;

	struct {
		uint8_t savedRowStartParity;
		uint8_t rowRuns[99];
	} mirrorRow;

	destination = g_flightAuxBuffer + g_viewportSpanMaskOffset;
	if (height != 0) {
		rowsRemaining = height;
		do {
			rowStartParity = *encodedMask;
			if (mirrorHorizontal == 0) {
				decodedWidth = 0;
				*destination++ = rowStartParity;
				++encodedMask;
				while (width > decodedWidth) {
					encodedRun = *encodedMask++;
					if (encodedRun == 0) {
						decodedWidth += 0xFFu;
						*destination++ = 0;
						extendedRun = *encodedMask++;
						runLength = (int)g_screenWidth;
						if (runLength == 320) {
							encodedRun =
								(uint8_t)(extendedRun +
									  1);
						} else if (extendedRun == 0) {
							decodedWidth += 0x100u;
							*destination++ = 0;
							extendedRun =
								*encodedMask++;
							encodedRun =
								(uint8_t)(extendedRun +
									  1);
						} else if (extendedRun ==
							   0xFFu) {
							decodedWidth += 0x100u;
							*destination++ = 0;
							encodedRun = 0;
						} else {
							encodedRun =
								(uint8_t)(extendedRun +
									  1);
						}
					}
					*destination++ = encodedRun;
					runLength = encodedRun;
					decodedWidth = (uint16_t)(decodedWidth +
								  runLength);
				}
			} else {
				mirrorDecodedWidth = 0;
				++encodedMask;
				tempWrite = mirrorRow.rowRuns;
				mirrorRow.savedRowStartParity = rowStartParity;
				while (width > mirrorDecodedWidth) {
					encodedRun = *encodedMask++;
					if (encodedRun == 0) {
						mirrorDecodedWidth += 0xFFu;
						*tempWrite++ = 0;
						extendedRun = *encodedMask++;
						runLength = (int)g_screenWidth;
						if (runLength == 320) {
							encodedRun =
								(uint8_t)(extendedRun +
									  1);
						} else if (extendedRun == 0) {
							mirrorDecodedWidth +=
								0x100u;
							*tempWrite++ = 0;
							extendedRun =
								*encodedMask++;
							encodedRun =
								(uint8_t)(extendedRun +
									  1);
						} else if (extendedRun ==
							   0xFFu) {
							mirrorDecodedWidth +=
								0x100u;
							*tempWrite++ = 0;
							encodedRun = 0;
						} else {
							encodedRun =
								(uint8_t)(extendedRun +
									  1);
						}
					}
					*tempWrite++ = encodedRun;
					runLength = encodedRun;
					mirrorDecodedWidth =
						(uint16_t)(mirrorDecodedWidth +
							   runLength);
				}

				reversedDecodedWidth = 0;
				tempRead = mirrorRow.rowRuns;
				*destination = mirrorRow.savedRowStartParity;
				runCount = 0;
				while (width > reversedDecodedWidth) {
					uint8_t *extensionPrefix;

					extensionPrefix = tempRead;
					reverseRun = *tempRead++;
					if (reverseRun == 0) {
						reversedDecodedWidth += 0xFFu;
						reverseRun = *tempRead;
						*extensionPrefix = reverseRun;
						*tempRead++ = 0;
						if (reverseRun == 0) {
							reversedDecodedWidth +=
								0x100u;
							reverseRun = *tempRead;
							*extensionPrefix =
								reverseRun;
							*tempRead++ = 0;
						}
					}
					++runCount;
					runLength = reverseRun;
					reversedDecodedWidth =
						(uint16_t)(reversedDecodedWidth +
							   runLength);
				}

				reverseIndex =
					(unsigned int)(tempRead -
						       mirrorRow.rowRuns);
				if ((runCount & 1) == 0) {
					*destination = (uint8_t)-*destination;
				}
				++destination;
				while (runCount != 0) {
					--reverseIndex;
					reverseRun =
						mirrorRow.rowRuns[reverseIndex];
					*destination++ = reverseRun;
					if (reverseRun == 0) {
						--reverseIndex;
						reverseRun =
							mirrorRow.rowRuns
								[reverseIndex];
						*destination++ = reverseRun;
						if (reverseRun == 0) {
							--reverseIndex;
							reverseRun =
								mirrorRow.rowRuns
									[reverseIndex];
							*destination++ =
								reverseRun;
						}
					}
					--runCount;
				}
			}
			--rowsRemaining;
		} while (rowsRemaining != 0);
	}

	if (g_useHardware3D != 0) {
		nullsub_2();
	}
}

/* Writes into g_flightAuxBuffer at g_viewportSpanMaskOffset a span mask of
 * height rows, each one unmasked run of width pixels (first byte 1), in the run
 * form FlightSw_CopyViewportSpanMaskRle writes. Calls nullsub_2 in the hardware
 * path. */
// FUNCTION: XVT 0x442250
void FlightSw_BuildFullViewportSpanMaskRle(uint16_t width, unsigned int height)
{
	uint16_t rowIndex;
	uint16_t widthCode;
	uint8_t *cursor;

	rowIndex = 0;
	cursor = g_flightAuxBuffer + g_viewportSpanMaskOffset;
	while (rowIndex < height) {
		widthCode = width;
		*cursor++ = 1;
		if (width >= 0x100u) {
			*cursor++ = 0;
			widthCode = (uint16_t)(width - 0xFFu);
			if (widthCode >= 0x100u) {
				widthCode = (uint16_t)(widthCode - 0x100u);
				*cursor++ = 0;
			}
		}
		++rowIndex;
		*cursor++ = (uint8_t)widthCode;
	}
	if (g_useHardware3D != 0) {
		nullsub_2();
	}
}

/* Returns y times the active row pitch plus x times g_flightBytesPerPixel. */
// FUNCTION: XVT 0x4498E0
int32_t FlightSw_ComputePixelOffset(int x, int y)
{
	return y * FlightSw_GetLinePitch() + x * g_flightBytesPerPixel;
}

/* Draws an RLE sprite at 16 bits without fading: sets g_flightSwRlePaletteShift
 * to 0 and calls FlightSw_BlitSpriteRleImpl16bpp. */
// FUNCTION: XVT 0x449900
void FlightSw_BlitSpriteRle16bpp(uint8_t *rleData, int x, int y,
				 int transparentColorIndex, int mirror)
{
	g_flightSwRlePaletteShift = 0;
	FlightSw_BlitSpriteRleImpl16bpp(rleData, x, y, transparentColorIndex,
					mirror, 0, 0);
}

/* Draws an RLE sprite at 16 bits, faded: sets g_flightSwRlePaletteShift to
 * paletteShift and calls FlightSw_BlitSpriteRleImpl16bpp without mirroring. */
// FUNCTION: XVT 0x449930
void FlightSw_BlitSpriteRleFaded16bpp(uint8_t *rleData, int x, int y,
				      int transparentColorIndex,
				      int8_t paletteShift, int16_t fadeAmount)
{
	unsigned int zeroExtendedFadeAmount;

	g_flightSwRlePaletteShift = paletteShift;
	zeroExtendedFadeAmount = (uint16_t)fadeAmount;
	FlightSw_BlitSpriteRleImpl16bpp(rleData, x, y, transparentColorIndex, 0,
					1, (int16_t)zeroExtendedFadeAmount);
}

/* The 16-bit form of FlightSw_BlitSpriteRleImpl8bpp: the same codes,
 * transparency and fading, each color drawn as its g_flightPalette16Bpp
 * entry. */
// FUNCTION: XVT 0x449970
void FlightSw_BlitSpriteRleImpl16bpp(uint8_t *rleData, int16_t x, int16_t y,
				     int transparentColorIndex, int mirror,
				     char isFaded, int16_t fadeAmount)
{
	unsigned int pixelOffset;
	uint8_t *source;
	uint8_t token;
	uint8_t color;
	int16_t alternatingPixelsRemaining;
	uint16_t runLength;
	int mirrorFlag;
	uint16_t *destination;

	g_flightSwRleTransparentColor = (uint8_t)transparentColorIndex;
	g_flightSwRleSpriteX = x;
	g_flightSwRleSpriteY = y;
	mirrorFlag = mirror;
	source = rleData;

	for (;;) {
		pixelOffset =
			FlightSw_GetLineOffset((uint16_t)g_flightSwRleSpriteY) +
			2 * (uint16_t)g_flightSwRleSpriteX;
#ifndef XVT_MODERN
		if (g_flightResolutionMode != FLIGHT_RESOLUTION_320X240 &&
		    g_flightSwFramebufferBase == g_swFramebufferBase) {
			unsigned int page;

			page = pixelOffset / g_vesaPageSizeBytes;
			pixelOffset %= g_vesaPageSizeBytes;
			RtsVga2_SetCurrentPage((uint8_t)g_vesaWindow,
					       (uint16_t)page);
		}
#endif
		destination =
			(uint16_t *)(g_flightSwFramebufferBase + pixelOffset);

		for (;;) {
			token = *source++;
			if (token < 0xFB) {
				runLength = token & 3;
				color = token >> 2;
				if (isFaded == 0) {
					color += g_flightSwRlePaletteShift;
				}
			} else {
				if (token > 0xFB) {
					if (token == 0xFC) {
						/* In this two-color dither run, runLength holds the second color's palette index,
						 * drawn on every other pixel. */
						color = source[0];
						if (isFaded != 0) {
							if (fadeAmount > 0) {
								color -= (uint8_t)
									fadeAmount;
								color += (uint8_t)
									g_flightSwRlePaletteShift;
								runLength =
									color;
								++runLength;
							} else {
								color = (uint8_t)
									g_flightSwRlePaletteShift;
								runLength =
									color;
							}
						} else {
							runLength = color;
							++runLength;
						}
						alternatingPixelsRemaining =
							(int16_t)source[1] + 1;
						source += 2;
						while (alternatingPixelsRemaining >
						       0) {
							*destination =
								g_flightPalette16Bpp
									[color];
							if (mirrorFlag == 0) {
								++destination;
							} else {
								--destination;
							}
							--alternatingPixelsRemaining;
							if (alternatingPixelsRemaining >
							    0) {
								*destination = g_flightPalette16Bpp
									[(uint8_t)
										 runLength];
								if (mirrorFlag ==
								    0) {
									++destination;
								} else {
									--destination;
								}
								--alternatingPixelsRemaining;
							}
						}
						continue;
					}
					if (token == 0xFD) {
						runLength = source[0];
						color = source[1];
						source += 2;
					} else {
						break;
					}
				} else {
					if (isFaded == 0) {
						g_flightSwRlePaletteShift =
							(int8_t)*source;
					}
					++source;
					continue;
				}
			}

			++runLength;
			if (color == transparentColorIndex) {
				if (mirrorFlag == 0) {
					destination += runLength;
				} else {
					destination -= runLength;
				}
				continue;
			}
			if (isFaded != 0) {
				if (fadeAmount > 0) {
					color -= (uint8_t)fadeAmount;
					color += (uint8_t)
						g_flightSwRlePaletteShift;
				} else {
					color = (uint8_t)
						g_flightSwRlePaletteShift;
				}
			}
			if (mirrorFlag == 0) {
				while (runLength > 0) {
					*destination++ =
						g_flightPalette16Bpp[color];
					--runLength;
				}
			} else {
				while (runLength > 0) {
					*destination-- =
						g_flightPalette16Bpp[color];
					--runLength;
				}
			}
		}

		if (token == 0xFF) {
			return;
		}
		++g_flightSwRleSpriteY;
	}
}

/* The 16-bit form of FlightSw_BlitMapIconRle: each color drawn as the
 * g_flightPalette16Bpp entry 4 above it. */
// FUNCTION: XVT 0x449BD0
void FlightSw_BlitMapIconRle16bpp(uint8_t *rleData, int x, int y,
				  int transparentIndex, int mirror)
{
	unsigned int pixelOffset;
	uint8_t token;
	uint8_t **source;
	XvtFramebufferAddress destination;
	uint16_t *paletteColor;
	uint16_t **palette;

	g_flightSwRleTransparentColor = (uint8_t)transparentIndex;
	g_flightSwRleSpriteX = (int16_t)x;
	g_flightSwRleSpriteY = (int16_t)y;
	source = &rleData;
	palette = &paletteColor;

	for (;;) {
		pixelOffset =
			FlightSw_GetLineOffset((uint16_t)g_flightSwRleSpriteY) +
			2 * (uint16_t)g_flightSwRleSpriteX;
#ifndef XVT_MODERN
		if (g_flightResolutionMode != FLIGHT_RESOLUTION_320X240 &&
		    XvtFramebufferAddress_IsLegacyBase(
			    g_flightSwFramebufferBase)) {
			unsigned int page;

			page = pixelOffset / g_vesaPageSizeBytes;
			pixelOffset %= g_vesaPageSizeBytes;
			RtsVga2_SetCurrentPage((uint8_t)g_vesaWindow,
					       (uint16_t)page);
		}
#endif
		memcpy(&destination, &g_flightSwFramebufferBase,
		       sizeof(destination));
		destination = XvtFramebufferAddress_FromBase(destination,
							     pixelOffset);

		for (;;) {
			uint16_t runLength;
			uint16_t pixelsRemaining;

			token = *(*source)++;
			if (token < 0xFB) {
				runLength = token;
				runLength &= 3;
				token >>= 2;
				token += (uint8_t)g_flightSwRlePaletteShift;
			} else {
				if (token > 0xFB) {
					if (token == 0xFC) {
						uint16_t paletteIndex;
						uint16_t nextPaletteIndex;

						paletteIndex = 0;
						memcpy(&paletteIndex,
						       (*source)++,
						       sizeof(**source));
						nextPaletteIndex = paletteIndex;
						++nextPaletteIndex;
						runLength = 0;
						memcpy(&runLength, (*source)++,
						       sizeof(**source));
						++runLength;
						if ((int16_t)runLength > 0) {
							*palette =
								&g_flightPalette16Bpp
									[paletteIndex +
									 4];
							do {
								XvtFramebufferAddress_Store16(
									&destination,
									**palette);
								if (mirror ==
								    0) {
									destination +=
										2;
								} else {
									destination -=
										2;
								}
								--runLength;
								if ((int16_t)
									    runLength >
								    0) {
									XvtFramebufferAddress_Store16(
										&destination,
										g_flightPalette16Bpp
											[(uint8_t)
												 nextPaletteIndex +
											 4]);
									if (mirror ==
									    0) {
										destination +=
											2;
									} else {
										destination -=
											2;
									}
								}
								--runLength;
							} while (
								(int16_t)
									runLength >
								0);
						}
						continue;
					}
					if (token == 0xFD) {
						runLength = 0;
						memcpy(&runLength, (*source)++,
						       sizeof(**source));
						memcpy(&token, (*source)++,
						       sizeof(token));
					} else {
						break;
					}
				} else {
					g_flightSwRlePaletteShift =
						(int8_t)*(*source)++;
					continue;
				}
			}

			++runLength;
			if (transparentIndex == token) {
				if (mirror == 0) {
					destination += 2 * runLength;
				} else {
					destination -= 2 * runLength;
				}
			} else if (mirror == 0) {
				if (runLength > 0) {
					memcpy(&pixelsRemaining, &runLength,
					       sizeof(pixelsRemaining));
					*palette = &g_flightPalette16Bpp[token +
									 4];
					do {
						XvtFramebufferAddress_Store16(
							&destination,
							**palette);
						destination += 2;
						--pixelsRemaining;
					} while (pixelsRemaining != 0);
				}
			} else {
				if (runLength > 0) {
					memcpy(&pixelsRemaining, &runLength,
					       sizeof(pixelsRemaining));
					*palette = &g_flightPalette16Bpp[token +
									 4];
					do {
						XvtFramebufferAddress_Store16(
							&destination,
							**palette);
						destination -= 2;
						--pixelsRemaining;
					} while (pixelsRemaining != 0);
				}
			}
		}

		if (token == 0xFF) {
			return;
		}
		++g_flightSwRleSpriteY;
	}
}

/* Writes the palette color of colorIndex at x, y on the 16-bit frame buffer
 * through the active row table. colorIndex is signed, so an index from 0x80 up
 * reads before g_flightPalette16Bpp. Does not clip.
 * FlightRender_InstallCallbacks stores it in g_flightDrawPixelFn, which nothing
 * calls. */
// FUNCTION: XVT 0x449EF0
void FlightSw_DrawPixel16bpp(uint16_t x, uint16_t y, int8_t colorIndex)
{
	unsigned int pixelOffset;
	uint16_t color;
	uint8_t *framebufferBase;
#ifndef XVT_MODERN
	unsigned int page;
#endif

	pixelOffset = FlightSw_GetLineOffset(y) + 2 * x;
#ifndef XVT_MODERN
	if (g_flightResolutionMode != FLIGHT_RESOLUTION_320X240 &&
	    g_flightSwFramebufferBase == g_swFramebufferBase) {
		page = pixelOffset / g_vesaPageSizeBytes;
		pixelOffset %= g_vesaPageSizeBytes;
		RtsVga2_SetCurrentPage((uint8_t)g_vesaWindow, (uint16_t)page);
	}
#endif
	color = g_flightPalette16Bpp[(int)colorIndex];
	framebufferBase = g_flightSwFramebufferBase;
	*(uint16_t *)(framebufferBase + pixelOffset) = color;
}

/* Fills the g_flightClip rectangle with g_flightTextBgColor at 16 bits, through
 * FlightSw_FillRectOrBorder16bpp. */
// FUNCTION: XVT 0x44A570
void FlightSw_FillClipRect16bpp(void)
{
	g_flightFillRectBottom16bpp = g_flightClipBottom;
	g_flightFillRectTop16bpp = g_flightClipTop;
	g_flightFillRectLeft16bpp = g_flightClipLeft;
	g_flightFillRectRight16bpp = g_flightClipRight;
	FlightSw_FillRectOrBorder16bpp(0);
}

/* The 16-bit form of FlightSw_FillRectOrBorder8bpp, filling with
 * g_flightPalette16Bpp[g_flightTextBgColor] and stepping the 16-bit
 * g_flightFillRect globals. */
// FUNCTION: XVT 0x44A5B0
void FlightSw_FillRectOrBorder16bpp(uint16_t borderThickness)
{
	int16_t row;
	unsigned int pixelOffset;
	uint16_t *destination;
	int16_t pixelsRemaining;
#ifndef XVT_MODERN
	unsigned int page;
#endif

	g_flightFillRectCurrentY16bpp = g_flightFillRectTop16bpp;
	g_flightFillRectRemainingRows16bpp =
		g_flightFillRectBottom16bpp - g_flightFillRectTop16bpp;
	if ((int16_t)(g_flightFillRectRight16bpp - g_flightFillRectLeft16bpp) <=
	    0) {
		return;
	}

	if (borderThickness != 0) {
		for (row = 0; row < borderThickness; ++row) {
			pixelOffset = FlightSw_GetLineOffset(
					      g_flightFillRectCurrentY16bpp) +
				      2 * g_flightFillRectLeft16bpp;
#ifndef XVT_MODERN
			if (g_flightResolutionMode !=
				    FLIGHT_RESOLUTION_320X240 &&
			    g_flightSwFramebufferBase == g_swFramebufferBase) {
				page = pixelOffset / g_vesaPageSizeBytes;
				pixelOffset %= g_vesaPageSizeBytes;
				RtsVga2_SetCurrentPage((uint8_t)g_vesaWindow,
						       (uint16_t)page);
			}
#endif
			destination = &(
				(uint16_t *)
					g_flightSwFramebufferBase)[pixelOffset /
								   2];
			pixelsRemaining = g_flightFillRectRight16bpp -
					  g_flightFillRectLeft16bpp;
			if (pixelsRemaining-- != 0) {
				do {
					*destination++ = g_flightPalette16Bpp
						[g_flightTextBgColor];
				} while (pixelsRemaining-- != 0);
			}
			--g_flightFillRectRemainingRows16bpp;
			++g_flightFillRectCurrentY16bpp;
		}

		while ((unsigned int)g_flightFillRectRemainingRows16bpp >
		       borderThickness) {
			int16_t centerWidth;
			int16_t leftPixelsRemaining;
			int16_t rightPixelsRemaining;

			pixelOffset = FlightSw_GetLineOffset(
					      g_flightFillRectCurrentY16bpp) +
				      2 * g_flightFillRectLeft16bpp;
#ifndef XVT_MODERN
			if (g_flightResolutionMode !=
				    FLIGHT_RESOLUTION_320X240 &&
			    g_flightSwFramebufferBase == g_swFramebufferBase) {
				page = pixelOffset / g_vesaPageSizeBytes;
				pixelOffset %= g_vesaPageSizeBytes;
				RtsVga2_SetCurrentPage((uint8_t)g_vesaWindow,
						       (uint16_t)page);
			}
#endif
			destination = &(
				(uint16_t *)
					g_flightSwFramebufferBase)[pixelOffset /
								   2];
			leftPixelsRemaining = borderThickness;
			rightPixelsRemaining = borderThickness;
			centerWidth = g_flightFillRectRight16bpp -
				      2 * borderThickness -
				      g_flightFillRectLeft16bpp;
			while (leftPixelsRemaining-- != 0) {
				*destination++ = g_flightPalette16Bpp
					[g_flightTextBgColor];
			}
			destination += centerWidth;
			while (rightPixelsRemaining-- != 0) {
				*destination++ = g_flightPalette16Bpp
					[g_flightTextBgColor];
			}
			--g_flightFillRectRemainingRows16bpp;
			++g_flightFillRectCurrentY16bpp;
		}

		for (row = 0; row < borderThickness; ++row) {
			pixelOffset = FlightSw_GetLineOffset(
					      g_flightFillRectCurrentY16bpp) +
				      2 * g_flightFillRectLeft16bpp;
#ifndef XVT_MODERN
			if (g_flightResolutionMode !=
				    FLIGHT_RESOLUTION_320X240 &&
			    g_flightSwFramebufferBase == g_swFramebufferBase) {
				page = pixelOffset / g_vesaPageSizeBytes;
				pixelOffset %= g_vesaPageSizeBytes;
				RtsVga2_SetCurrentPage((uint8_t)g_vesaWindow,
						       (uint16_t)page);
			}
#endif
			destination = &(
				(uint16_t *)
					g_flightSwFramebufferBase)[pixelOffset /
								   2];
			pixelsRemaining = g_flightFillRectRight16bpp -
					  g_flightFillRectLeft16bpp;
			if (pixelsRemaining-- != 0) {
				do {
					*destination++ = g_flightPalette16Bpp
						[g_flightTextBgColor];
				} while (pixelsRemaining-- != 0);
			}
			--g_flightFillRectRemainingRows16bpp;
			++g_flightFillRectCurrentY16bpp;
		}
		return;
	}

	while (g_flightFillRectRemainingRows16bpp != 0) {
		pixelOffset =
			FlightSw_GetLineOffset(g_flightFillRectCurrentY16bpp) +
			2 * g_flightFillRectLeft16bpp;
#ifndef XVT_MODERN
		if (g_flightResolutionMode != FLIGHT_RESOLUTION_320X240 &&
		    g_flightSwFramebufferBase == g_swFramebufferBase) {
			page = pixelOffset / g_vesaPageSizeBytes;
			pixelOffset %= g_vesaPageSizeBytes;
			RtsVga2_SetCurrentPage((uint8_t)g_vesaWindow,
					       (uint16_t)page);
		}
#endif
		destination = &(
			(uint16_t *)g_flightSwFramebufferBase)[pixelOffset / 2];
		pixelsRemaining =
			g_flightFillRectRight16bpp - g_flightFillRectLeft16bpp;
		if (pixelsRemaining <= 0) {
			return;
		}
		if (pixelsRemaining-- != 0) {
			do {
				*destination++ = g_flightPalette16Bpp
					[g_flightTextBgColor];
			} while (pixelsRemaining-- != 0);
		}
		--g_flightFillRectRemainingRows16bpp;
		++g_flightFillRectCurrentY16bpp;
	}
}

/* Clips the rectangle from x1, y1 to x2, y2 (ends not included) to the
 * g_flightClip rectangle, stores it in the 16-bit g_flightFillRect globals, and
 * fills it or its frame with FlightSw_FillRectOrBorder16bpp when anything is
 * left. */
// FUNCTION: XVT 0x44A980
void FlightSw_FillRectClipped16bpp(uint16_t x1, uint16_t y1, uint16_t x2,
				   uint16_t y2, uint16_t borderThickness)
{
	uint16_t clippedTop;
	uint16_t clippedBottom;

	g_flightFillRectLeft16bpp = x1;
	g_flightFillRectRight16bpp = x2;
	clippedTop = y1;
	clippedBottom = y2;
	if (x1 < g_flightClipLeft) {
		g_flightFillRectLeft16bpp = g_flightClipLeft;
	}
	if (x2 > g_flightClipRight) {
		g_flightFillRectRight16bpp = g_flightClipRight;
	}

	g_flightFillRectTop16bpp = y1;
	if (g_flightFillRectTop16bpp < g_flightClipTop) {
		clippedTop = g_flightClipTop;
	}
	g_flightFillRectBottom16bpp = y2;
	if (g_flightFillRectBottom16bpp > g_flightClipBottom) {
		clippedBottom = g_flightClipBottom;
	}
	g_flightFillRectBottom16bpp = clippedBottom;
	g_flightFillRectTop16bpp = clippedTop;
	if (clippedBottom > clippedTop &&
	    g_flightFillRectLeft16bpp < g_flightFillRectRight16bpp) {
		FlightSw_FillRectOrBorder16bpp(borderThickness);
	}
}

/* Copies height rows of width 16-bit pixels from x, y on the frame buffer into
 * buffer, row after row. Does not clip. */
// FUNCTION: XVT 0x44AB10
void FlightSw_SaveScreenRect16bpp(uint16_t *buffer, int x, int y, int16_t width,
				  int height)
{
	uint16_t *output;
	uint16_t *source;
	uint16_t pixel;
	unsigned int pixelOffset;
#ifndef XVT_MODERN
	unsigned int page;
#endif

	output = buffer;
	if (height == 0) {
		return;
	}
	do {
		pixelOffset = FlightSw_GetLineOffset(y) + 2 * x;
#ifndef XVT_MODERN
		if (g_flightResolutionMode != FLIGHT_RESOLUTION_320X240 &&
		    g_flightSwFramebufferBase == g_swFramebufferBase) {
			page = pixelOffset / g_vesaPageSizeBytes;
			pixelOffset %= g_vesaPageSizeBytes;
			RtsVga2_SetCurrentPage((uint8_t)g_vesaWindow,
					       (uint16_t)page);
		}
#endif
		g_savedRowPixelsRemaining = width;
		source = (uint16_t *)(g_flightSwFramebufferBase + pixelOffset);
		while (g_savedRowPixelsRemaining > 0) {
			pixel = *source++;
			*output++ = pixel;
			--g_savedRowPixelsRemaining;
		}
		--height;
		++y;
	} while (height != 0);
}

/* Writes height rows of width 16-bit pixels from buffer back at x, y. Does not
 * clip. */
// FUNCTION: XVT 0x44ABC0
void FlightSw_RestoreScreenRect16bpp(uint16_t *buffer, int x, int y,
				     int16_t width, int height)
{
	uint16_t *input;
	uint16_t *destination;
	uint16_t pixel;
	unsigned int pixelOffset;
#ifndef XVT_MODERN
	unsigned int page;
#endif

	input = buffer;
	if (height == 0) {
		return;
	}
	do {
		pixelOffset = FlightSw_GetLineOffset(y) + 2 * x;
#ifndef XVT_MODERN
		if (g_flightResolutionMode != FLIGHT_RESOLUTION_320X240 &&
		    g_flightSwFramebufferBase == g_swFramebufferBase) {
			page = pixelOffset / g_vesaPageSizeBytes;
			pixelOffset %= g_vesaPageSizeBytes;
			RtsVga2_SetCurrentPage((uint8_t)g_vesaWindow,
					       (uint16_t)page);
		}
#endif
		g_savedRowPixelsRemaining = width;
		destination =
			(uint16_t *)(g_flightSwFramebufferBase + pixelOffset);
		while (g_savedRowPixelsRemaining > 0) {
			pixel = *input;
			*destination = pixel;
			++destination;
			++input;
			--g_savedRowPixelsRemaining;
		}
		--height;
		++y;
	} while (height != 0);
}

/* The 16-bit form of FlightSw_DrawPointArray8bpp, testing for the palette color
 * of index 44 and with no second row at 640x480: a point not drawn has its
 * third word set to 0, and a drawn one keeps its color there. */
// FUNCTION: XVT 0x44AC70
void FlightSw_DrawPointArray16bpp(uint16_t *points, int16_t count)
{
	uint16_t x;
	uint16_t y;
	uint16_t *current;
	int16_t remaining;
	unsigned int pixelOffset;
	int16_t *destination;
#ifndef XVT_MODERN
	unsigned int page;
#endif

	current = points;
	remaining = count;
	if (remaining == 0) {
		return;
	}
	do {
		x = current[0];
		y = current[1];
		pixelOffset = FlightSw_GetLineOffset(y) + 2 * x;
#ifndef XVT_MODERN
		if (g_flightResolutionMode != FLIGHT_RESOLUTION_320X240 &&
		    g_swFramebufferBase == g_flightSwFramebufferBase) {
			page = pixelOffset / g_vesaPageSizeBytes;
			pixelOffset %= g_vesaPageSizeBytes;
			RtsVga2_SetCurrentPage((uint8_t)g_vesaWindow,
					       (uint16_t)page);
			RtsVga2_SetCurrentPage(1, (uint16_t)page);
		}
#endif
		destination =
			(int16_t *)(g_flightSwFramebufferBase + pixelOffset);
		if (*destination != g_flightPalette16Bpp[44]) {
			current[2] = 0;
		} else {
			*destination = (int16_t)
				g_flightPalette16Bpp[(uint8_t)current[2]];
		}
		--remaining;
		current += 3;
	} while (remaining != 0);
}

/* Writes the palette color of index 44 back at each point whose third word has
 * a nonzero low byte. */
// FUNCTION: XVT 0x44AD30
void FlightSw_ErasePointArray16bpp(uint16_t *points, int16_t count)
{
	unsigned int pixelOffset;
#ifndef XVT_MODERN
	unsigned int legacyResolutionMode;
#endif
	int16_t remaining;
	uint16_t *current;

#ifndef XVT_MODERN
	legacyResolutionMode = FLIGHT_RESOLUTION_320X240;
#endif
	remaining = count;
	if (remaining == 0) {
		return;
	}
	current = points;
	do {
		unsigned int x;
		unsigned int y;
		uint16_t *destination;
#ifndef XVT_MODERN
		unsigned int page;
#endif

		x = current[0];
		y = current[1];
		pixelOffset = FlightSw_GetLineOffset(y) + 2 * x;
#ifndef XVT_MODERN
		if (g_flightResolutionMode != legacyResolutionMode &&
		    g_swFramebufferBase == g_flightSwFramebufferBase) {
			page = pixelOffset / g_vesaPageSizeBytes;
			pixelOffset %= g_vesaPageSizeBytes;
			RtsVga2_SetCurrentPage((uint8_t)g_vesaWindow,
					       (uint16_t)page);
		}
#endif
		destination =
			(uint16_t *)(g_flightSwFramebufferBase + pixelOffset);
		if ((uint8_t)current[2] != 0) {
			*destination = g_flightPalette16Bpp[44];
		}

		--remaining;
		current += 3;
	} while (remaining != 0);
}

/* Draws the 10-pixel radar target marker of g_radarTargetMarkerShape16bpp at
 * g_radarTargetMarkerDrawX and g_radarTargetMarkerDrawY in the palette color of
 * index 206, saving the pixels under it. Does not clip. */
// FUNCTION: XVT 0x44ADD0
void FlightSw_DrawRadarTargetMarker16bpp(void)
{
	unsigned int pixelOffset;
	int16_t remaining;
	uint16_t offsetIndex;
	uint16_t savedPixelIndex;
	uint16_t *pixel;
#ifndef XVT_MODERN
	unsigned int page;
#endif

	remaining = 10;
	offsetIndex = 0;
	savedPixelIndex = 0;
	do {
		pixelOffset =
			FlightSw_GetLineOffset(
				g_radarTargetMarkerDrawY +
				g_radarTargetMarkerShape16bpp[offsetIndex +
							      1]) +
			2 * (g_radarTargetMarkerDrawX +
			     g_radarTargetMarkerShape16bpp[offsetIndex]);
#ifndef XVT_MODERN
		if (g_flightResolutionMode != FLIGHT_RESOLUTION_320X240 &&
		    g_flightSwFramebufferBase == g_swFramebufferBase) {
			page = pixelOffset / g_vesaPageSizeBytes;
			pixelOffset %= g_vesaPageSizeBytes;
			RtsVga2_SetCurrentPage((uint8_t)g_vesaWindow,
					       (uint16_t)page);
			RtsVga2_SetCurrentPage(1, (uint16_t)page);
		}
#endif
		offsetIndex += 2;
		pixel = (uint16_t *)(g_flightSwFramebufferBase + pixelOffset);
		g_radarTargetMarkerSavedPixels16bpp[savedPixelIndex++] = *pixel;
		*pixel = g_flightPalette16Bpp[206];
		--remaining;
	} while (remaining != 0);
}

/* Puts back the 10 pixels FlightSw_DrawRadarTargetMarker16bpp saved, at
 * g_radarTargetMarkerRestoreX and g_radarTargetMarkerRestoreY. */
// FUNCTION: XVT 0x44AEB0
void FlightSw_RestoreRadarTargetMarker16bpp(void)
{
	unsigned int pixelOffset;
	int16_t remaining;
	uint16_t offsetIndex;
	uint16_t savedPixelIndex;
	uint16_t pixel;
	uint8_t *framebufferBase;
#ifndef XVT_MODERN
	unsigned int page;
#endif

	offsetIndex = 0;
	remaining = 10;
	savedPixelIndex = 0;
	do {
		pixelOffset =
			FlightSw_GetLineOffset(
				g_radarTargetMarkerRestoreY +
				g_radarTargetMarkerShape16bpp[offsetIndex +
							      1]) +
			2 * (g_radarTargetMarkerRestoreX +
			     g_radarTargetMarkerShape16bpp[offsetIndex]);
#ifndef XVT_MODERN
		if (g_flightResolutionMode != FLIGHT_RESOLUTION_320X240 &&
		    g_swFramebufferBase == g_flightSwFramebufferBase) {
			page = pixelOffset / g_vesaPageSizeBytes;
			pixelOffset %= g_vesaPageSizeBytes;
			RtsVga2_SetCurrentPage((uint8_t)g_vesaWindow,
					       (uint16_t)page);
		}
#endif
		framebufferBase = g_flightSwFramebufferBase + pixelOffset;
		offsetIndex += 2;
		pixel = g_radarTargetMarkerSavedPixels16bpp[savedPixelIndex++];
		*(uint16_t *)framebufferBase = pixel;
		--remaining;
	} while (remaining != 0);
}

/* Draws the 7-pixel cross marker centered on x, y in the palette color of
 * colorIndex, saving the pixels under it; returns that color. Nothing calls
 * this. */
// FUNCTION: XVT 0x44AF70
uint16_t FlightSw_DrawCrossMarker16bpp(uint16_t x, uint16_t y,
				       uint8_t colorIndex)
{
	int16_t remaining;
	unsigned int coordinates[2];
	uint16_t offsetIndex;
	uint16_t savedPixelIndex;
	uint16_t *color;
	uint16_t result;

	remaining = 7;
	coordinates[0] = y;
	offsetIndex = 0;
	savedPixelIndex = 0;
	coordinates[1] = x;
	color = &g_flightPalette16Bpp[colorIndex];
	do {
		unsigned int pixelOffset;
		uint16_t *pixel;

		pixelOffset =
			FlightSw_GetLineOffset(
				coordinates[0] +
				((int8_t *)g_flightSwCrossMarkerOffsets16bpp)
					[offsetIndex + 1]) +
			2 * (coordinates[1] +
			     ((int8_t *)g_flightSwCrossMarkerOffsets16bpp)
				     [offsetIndex]);
#ifndef XVT_MODERN
		if (g_flightResolutionMode != FLIGHT_RESOLUTION_320X240 &&
		    g_flightSwFramebufferBase == g_swFramebufferBase) {
			unsigned int page;

			page = pixelOffset / g_vesaPageSizeBytes;
			pixelOffset %= g_vesaPageSizeBytes;
			RtsVga2_SetCurrentPage((uint8_t)g_vesaWindow,
					       (uint16_t)page);
			RtsVga2_SetCurrentPage(1, (uint16_t)page);
		}
#endif
		offsetIndex += 2;
		pixel = (uint16_t *)(g_flightSwFramebufferBase + pixelOffset);
		g_flightSwCrossMarkerSavedPixels16bpp[savedPixelIndex++] =
			*pixel;
		result = *color;
		*pixel = *color;
		--remaining;
	} while (remaining != 0);
	return result;
}

/* Puts back the 7 pixels saved around x, y and returns the last of them.
 * Nothing calls this. */
// FUNCTION: XVT 0x44B070
uint16_t FlightSw_RestoreCrossMarker16bpp(uint16_t x, uint16_t y)
{
	int16_t remaining;
	uint16_t offsetIndex;
	uint16_t savedPixelIndex;
	unsigned int pixelOffset;
	uint8_t *framebufferBase;
	uint16_t pixel;
#ifndef XVT_MODERN
	unsigned int page;
#endif

	remaining = 7;
	offsetIndex = 0;
	savedPixelIndex = 0;
	do {
		pixelOffset =
			FlightSw_GetLineOffset(
				y +
				((int8_t *)g_flightSwCrossMarkerOffsets16bpp)
					[offsetIndex + 1]) +
			2 * (x + ((int8_t *)g_flightSwCrossMarkerOffsets16bpp)
					 [offsetIndex]);
#ifndef XVT_MODERN
		if (g_flightResolutionMode != FLIGHT_RESOLUTION_320X240 &&
		    g_flightSwFramebufferBase == g_swFramebufferBase) {
			page = pixelOffset / g_vesaPageSizeBytes;
			pixelOffset %= g_vesaPageSizeBytes;
			RtsVga2_SetCurrentPage((uint8_t)g_vesaWindow,
					       (uint16_t)page);
		}
#endif
		offsetIndex += 2;
		framebufferBase = g_flightSwFramebufferBase;
		pixel = g_flightSwCrossMarkerSavedPixels16bpp
			[savedPixelIndex++];
		--remaining;
		*(uint16_t *)(framebufferBase + pixelOffset) = pixel;
	} while (remaining != 0);
	return pixel;
}

/* The 16-bit form of FlightSw_DrawLine8bpp, drawing in the palette color of
 * colorIdx with rows the active row pitch apart. The modern build also returns
 * from an upward line whose clipped end lies below its clipped start. */
// FUNCTION: XVT 0x44B140
void FlightSw_DrawLine16bpp(int x1, int y1, int x2, int y2, uint8_t colorIdx)
{
	uint16_t color;
	int deltaX;
	int deltaY;
	int startY;
	int endX;
	uint8_t *pixel;

	color = g_flightPalette16Bpp[colorIdx];
	endX = x2;
	deltaX = endX - x1;
	if (deltaX < 0) {
		int swapX;

		swapX = x1;
		startY = y2;
		y2 = y1;
		deltaX = -deltaX;
		x1 = endX;
		endX = swapX;
	} else {
		if (deltaX == 0) {
			int count;

			if (x1 < g_flightClipLeft) {
				return;
			}
			if (x1 >= g_flightClipRight) {
				return;
			}

			if (y2 < y1) {
				int swapY;

				swapY = y1;
				y1 = y2;
				y2 = swapY;
			}
			if (y1 < g_flightClipTop) {
				y1 = g_flightClipTop;
			}
			if (y2 >= g_flightClipBottom) {
				y2 = g_flightClipBottom - 1;
			}

			count = y2 - y1;
			if (count > 0) {
				pixel = g_flightSwFramebufferBase +
					y1 * FlightSw_GetLinePitch() +
					x1 * g_flightBytesPerPixel;
				while (count-- != 0) {
					*(uint16_t *)pixel = color;
					pixel += FlightSw_GetLinePitch();
				}
			}
			return;
		}
		startY = y1;
	}

	if (x1 < g_flightClipRight && endX >= g_flightClipLeft) {
		deltaY = y2 - startY;
		if (deltaY < 0) {
			deltaY = -deltaY;
			if (startY < g_flightClipTop) {
				return;
			}
			if (y2 >= g_flightClipBottom) {
				return;
			}

			if (startY >= g_flightClipBottom) {
				int advance;

				advance = MATH2_ABoverC32(
					startY - g_flightClipBottom + 1, deltaX,
					deltaY);
				x1 += advance;
				if (x1 >= g_flightClipRight) {
					return;
				}
				startY = g_flightClipBottom - 1;
			}
			if (x1 < g_flightClipLeft) {
				int advance;

				advance = MATH2_ABoverC32(g_flightClipLeft - x1,
							  deltaY, deltaX);
				startY -= advance;
				if (startY < g_flightClipTop) {
					return;
				}
				x1 = g_flightClipLeft;
			}
			if (endX >= g_flightClipRight) {
				endX = g_flightClipRight - 1;
			}
			if (y2 < g_flightClipTop) {
				y2 = g_flightClipTop;
			}

#ifdef XVT_MODERN
			if (y2 > startY) {
				return;
			}
#endif

			pixel = g_flightSwFramebufferBase +
				startY * FlightSw_GetLinePitch() +
				x1 * g_flightBytesPerPixel;
			if (deltaX >= deltaY) {
				int error;
				int ySteps;
				int xCount;

				error = deltaX >> 1;
				xCount = endX - x1;
				ySteps = startY - y2 + 1;
				while (xCount-- != 0) {
					*(uint16_t *)pixel = color;
					pixel += g_flightBytesPerPixel;
					error -= deltaY;
					if (error < 0) {
						error += deltaX;
						--ySteps;
						if (ySteps == 0) {
							return;
						}
						pixel -=
							FlightSw_GetLinePitch();
					}
				}
			} else {
				int error;
				int xSteps;
				int yCount;

				error = deltaY >> 1;
				xSteps = endX - x1 + 1;
				yCount = startY - y2;
				while (yCount-- != 0) {
					*(uint16_t *)pixel = color;
					pixel -= FlightSw_GetLinePitch();
					error -= deltaX;
					if (error < 0) {
						error += deltaY;
						--xSteps;
						if (xSteps == 0) {
							return;
						}
						pixel += g_flightBytesPerPixel;
					}
				}
			}
		} else if (deltaY > 0) {
			if (startY >= g_flightClipBottom) {
				return;
			}
			if (y2 < g_flightClipTop) {
				return;
			}

			if (startY < g_flightClipTop) {
				int advance;

				advance = MATH2_ABoverC32(g_flightClipTop -
								  startY,
							  deltaX, deltaY);
				x1 += advance;
				if (x1 >= g_flightClipRight) {
					return;
				}
				startY = g_flightClipTop;
			}
			if (x1 < g_flightClipLeft) {
				int advance;

				advance = MATH2_ABoverC32(g_flightClipLeft - x1,
							  deltaY, deltaX);
				startY += advance;
				if (startY >= g_flightClipBottom) {
					return;
				}
				x1 = g_flightClipLeft;
			}
			if (endX >= g_flightClipRight) {
				endX = g_flightClipRight - 1;
			}
			if (y2 >= g_flightClipBottom) {
				y2 = g_flightClipBottom - 1;
			}

			pixel = g_flightSwFramebufferBase +
				startY * FlightSw_GetLinePitch() +
				x1 * g_flightBytesPerPixel;
			if (deltaX >= deltaY) {
				int error;
				int ySteps;
				int xCount;

				error = deltaX >> 1;
				xCount = endX - x1;
				ySteps = y2 - startY + 1;
				while (xCount-- != 0) {
					*(uint16_t *)pixel = color;
					pixel += g_flightBytesPerPixel;
					error -= deltaY;
					if (error < 0) {
						error += deltaX;
						--ySteps;
						if (ySteps == 0) {
							return;
						}
						pixel +=
							FlightSw_GetLinePitch();
					}
				}
			} else {
				int error;
				int xSteps;
				int yCount;

				error = deltaY >> 1;
				xSteps = endX - x1 + 1;
				yCount = y2 - startY;
				while (yCount-- != 0) {
					*(uint16_t *)pixel = color;
					pixel += FlightSw_GetLinePitch();
					error -= deltaX;
					if (error < 0) {
						error += deltaY;
						--xSteps;
						if (xSteps == 0) {
							return;
						}
						pixel += g_flightBytesPerPixel;
					}
				}
			}
		} else if (startY >= g_flightClipTop &&
			   startY < g_flightClipBottom) {
			int count;

			if (x1 < g_flightClipLeft) {
				x1 = g_flightClipLeft;
			}
			if (endX >= g_flightClipRight) {
				endX = g_flightClipRight - 1;
			}

			count = endX - x1;
			if (count > 0) {
				pixel = g_flightSwFramebufferBase +
					startY * FlightSw_GetLinePitch() +
					x1 * g_flightBytesPerPixel;
				while (count-- != 0) {
					*(uint16_t *)pixel = color;
					pixel += g_flightBytesPerPixel;
				}
			}
		}
	}
}

/* Returns trig2_calcsinemagnitude(angle): the sine's magnitude with 65536
 * standing for 1, as a 16-bit pattern. */
// FUNCTION: XVT 0x46A3B0
int16_t FlightSw_LookupSpriteSineMagnitudeQ16(int16_t angle)
{
	return trig2_calcsinemagnitude(angle);
}

/* Copies what was drawn in the box from startX, startY up to, not including,
 * endX, endY from the rotated-sprite buffer at pDst to the screen, locking the
 * flight surface unless g_flightSurfaceAlreadyLocked is set. A drawn pixel has
 * 0x80 in its high byte at 16 bits, which is replaced by the sprite's palette
 * color, or is under 0x40 at 8 bits, which is mapped through
 * g_flightSwRotSpritePalette8. Each run of drawn pixels goes out through
 * sw3d_BlitOccludedSpan with g_projScaleInt / g_viewSpaceDepth as its inverse
 * depth. rowSkipBytes takes the pointer from a row's end to the next row's
 * start. */
// FUNCTION: XVT 0x486470
void FlightSw_BlitPreparedRotatedSpriteSpans(uint8_t *pDst, int rowSkipBytes,
					     int startX, int startY, int endX,
					     int endY)
{
	int scanY;
	uint8_t *pixel;
	int scanX;
	float spriteW;

	if (g_flightSurfaceAlreadyLocked == 0) {
		FlightSurface_Lock();
	}
	scanY = startY;
	spriteW = (float)(unsigned int)g_projScaleInt / (float)g_viewSpaceDepth;
	if (g_flightBytesPerPixel == 2) {
		if (endY > scanY) {
			pixel = pDst;
			do {
				scanX = startX;
				while (scanX < endX) {
					uint8_t *runStart;
					int runStartX;

					while (scanX < endX &&
					       pixel[1] != 0x80) {
						pixel += 2;
						++scanX;
					}
					if (scanX == endX) {
						break;
					}

					runStartX = scanX;
					runStart = pixel;
					while (scanX < endX &&
					       pixel[1] == 0x80) {
						pixel[1] =
							g_flightSwRotSpritePalette16High
								[pixel[0]];
						pixel[0] =
							g_flightSwRotSpritePalette16Low
								[pixel[0]];
						pixel += 2;
						++scanX;
					}
					sw3d_BlitOccludedSpan(runStart,
							      runStartX, scanX,
							      scanY, spriteW);
				}
				++scanY;
				pixel += rowSkipBytes;
			} while (scanY < endY);
		}
	} else if (endY > scanY) {
		pixel = pDst;
		do {
			scanX = startX;
			while (scanX < endX) {
				uint8_t *runStart;
				int runStartX;

				while (scanX < endX && *pixel >= 0x40) {
					++pixel;
					++scanX;
				}
				if (scanX == endX) {
					break;
				}

				runStartX = scanX;
				runStart = pixel;
				while (scanX < endX && *pixel < 0x40) {
					uint8_t color;

					color = *pixel;
					*pixel = g_flightSwRotSpritePalette8
						[color];
					++pixel;
					++scanX;
				}
				sw3d_BlitOccludedSpan(runStart, runStartX,
						      scanX, scanY, spriteW);
			}
			++scanY;
			pixel += rowSkipBytes;
		} while (scanY < endY);
	}
	if (g_flightSurfaceAlreadyLocked == 0) {
		FlightSurface_Unlock();
	}
}
