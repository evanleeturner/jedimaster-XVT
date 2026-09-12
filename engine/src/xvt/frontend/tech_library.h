#ifndef XVT_FRONTEND_TECH_LIBRARY_H
#define XVT_FRONTEND_TECH_LIBRARY_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct TechLibrarySpecText {
	char designation[64];
	char manufacturer[64];
	char inUseBy[64];
	char description[256];
	char crew[64];
};

extern TechLibrarySpecText* g_techLibrarySpecTextTable;
extern CraftTechStats g_techLibraryCraftStats;
extern int g_techLibrarySelectedShipListIdx;
extern int g_techLibraryLightX;
extern float g_techLibraryPreviewYawDeg;
extern int g_techLibraryLightY;
extern int g_techLibraryLightZ;
extern float g_techLibraryPreviewPitchDeg;

int TechLibrary_Update(int frameCounter);
int TechLibrary_UpdateModelControls(void);
int TechLibrary_DrawCraftSpecPanel(void);
int TechLibrary_LoadSpecTextTable(void);

#ifdef __cplusplus
}
#endif

#endif
