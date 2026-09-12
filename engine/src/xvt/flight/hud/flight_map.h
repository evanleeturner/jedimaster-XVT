#ifndef XVT_FLIGHT_HUD_FLIGHT_MAP_H
#define XVT_FLIGHT_HUD_FLIGHT_MAP_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const uint8_t g_flightIcons640FrameByObjectType[106];
extern const uint8_t g_flightIcons640WidthByFrame[72];
extern const uint8_t g_flightIcons640HeightByFrame[72];
extern const uint8_t g_flightMapIcons480x360FrameByObjectType[106];
extern const uint8_t g_flightMapIcons480x360WidthByFrame[72];
extern const uint8_t g_flightMapIcons480x360HeightByFrame[72];
extern const uint8_t g_flightMapIcons320x240FrameByObjectType[106];
extern const uint8_t g_flightMapIcons320x240WidthByFrame[72];
extern const uint8_t g_flightMapIcons320x240HeightByFrame[72];
extern const char g_flightMapIcons320x240ResourcePath[22];
extern const char g_flightMapIcons480x360ResourcePath[22];
extern const char g_flightIcons640x480ResourcePath[22];
extern int g_flightIconFrameCount;
extern const char* g_flightIconResourcePath;
extern uint8_t** g_flightIconFrames;

static __inline int FlightMap_ComputeAimStep(int delta) {
	int step;

	step = delta / 2;
	if (step > 0) {
		if (step > 4096)
			step = 4096;
		if (step < 16)
			step = 16;
		if (step > delta)
			step = delta;
	} else {
		if (step < -4096)
			step = -4096;
		if (step > -16)
			step = -16;
		if (step < delta)
			step = delta;
	}
	return step;
}

typedef enum MapRoomStringId {
	MAP_ROOM_STR_HELP_KEY = 0x0,
	MAP_ROOM_STR_SLASH = 0x1,
	MAP_ROOM_STR_FOLLOW_MARKER = 0x2,
	MAP_ROOM_STR_PLUS = 0x3,
	MAP_ROOM_STR_MINUS = 0x4,
	MAP_ROOM_STR_ZOOM_KEY = 0x5,
	MAP_ROOM_STR_MODE_KEY = 0x6,
	MAP_ROOM_STR_CENTER_KEY = 0x7,
	MAP_ROOM_STR_SPACEBAR = 0x8,
	MAP_ROOM_STR_HELP_DESCRIPTION = 0x9,
	MAP_ROOM_STR_FOLLOW_TARGET_ON = 0xA,
	MAP_ROOM_STR_FOLLOW_TARGET_OFF = 0xB,
	MAP_ROOM_STR_TARGET_TRACKING_ON = 0xC,
	MAP_ROOM_STR_TARGET_TRACKING_OFF = 0xD,
	MAP_ROOM_STR_INSTANT_ZOOM = 0xE,
	MAP_ROOM_STR_RETURN_TO_COMBAT = 0xF,
	MAP_ROOM_STR_CENTER_TARGET_DESCRIPTION = 0x10,
	MAP_ROOM_STR_TOGGLE_MODE_DESCRIPTION = 0x11,
	MAP_ROOM_STR_FOLLOWING = 0x12,
	MAP_ROOM_STR_TRACKING = 0x13,
} MapRoomStringId;

void FlightMap_RenderView(void);
void FlightMap_UpdateCamera(int playerIdx);
void FlightMap_BuildRenderList(void);
void FlightMap_DrawObjectPass(int pass);
void FlightMap_DrawOtherPlayerObjectBox(int objectIdx);
void FlightMap_DrawObjectOverlay(int objectIdx);
void FlightMap_DrawObjectIconAtViewPos(int objectIdx, int viewX, int viewY, int viewZ);
void FlightMap_DrawObjectBoxCorners(int x, int y, int width, int height, unsigned int colorIndex);
void FlightMap_DrawGrid(void);
void nullsub_14(void);
int FlightMap_PickObjectNearestScreenCenter(int playerIdx);

#ifdef __cplusplus
}
#endif

#endif
