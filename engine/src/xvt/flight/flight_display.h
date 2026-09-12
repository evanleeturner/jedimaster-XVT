#ifndef XVT_FLIGHT_FLIGHT_DISPLAY_H
#define XVT_FLIGHT_FLIGHT_DISPLAY_H

#include "aeron/compat/ddraw.h"
#include "aeron/compat/win_types.h"
#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern IDirectDrawSurface* g_flightPrimarySurface;
extern IDirectDrawSurface* g_flightRenderSurface;
extern IDirectDrawSurface* g_flightBackBuffer;
extern int g_flightPrimaryPitch[2];
extern int g_flightConfFlicker;
extern int g_flightFullscreen;
extern int g_renderTargetWidth;
extern int g_unusedFlightDisplayBytesPerPixelMirror;
extern int g_unusedFlightDisplayHardware3DMirror;
extern IDirectDrawPalette* g_flightPalette;
extern uint8_t g_flightHudStagingBuffer[640 * 480 * 2];
extern uint8_t g_flightSoftwareFramebuffer[640 * 480 * 2];
extern char g_hudCockpitResolutionDirectory[7];

void FlightDisplay_ConfigureResolutionState(void);
int FlightDisplay_ApplyResolutionMode(int resolutionMode);
int FlightDisplay_PostPrimarySurfaceCreateOrRestoreStub(void);
int FlightDisplay_Init(void);
uint8_t FlightDisplay_SetPaletteEntries(const uint8_t* rgbData, int firstEntry, int entryCount);
int FlightDisplay_CleanupAndReportError(int errorCode);
int FlightDisplay_GetPrimarySurfacePitch(void);
HRESULT FlightDisplay_Flip(void);
void nullsub_11(void);
int FlightDisplay_BlitRenderSurface(void);
void FlightDisplay_ApplyResolutionModeBackendStub(int resolutionMode);
void FlightDisplay_ClearBackBuffer(void);
void FlightDisplay_ClearSurface(IDirectDrawSurface* surface);
int FlightDisplay_RestorePrimarySurface(void);
int Display_IsPixelFormat555(void);
int FlightDisplay_ApplyResolutionModeInternalStub(int resolutionMode, int flags);
uint8_t FlightDisplay_WriteVgaPaletteEntries(const uint8_t* rgbEntries, int16_t firstEntry,
											 int16_t entryCount);

#ifdef __cplusplus
}
#endif

#endif
