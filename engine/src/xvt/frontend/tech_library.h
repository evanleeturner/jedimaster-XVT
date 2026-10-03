#ifndef XVT_FRONTEND_TECH_LIBRARY_H
#define XVT_FRONTEND_TECH_LIBRARY_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One craft's entry in specdesc.txt, the text of the craft database. */
struct TechLibrarySpecText {
	char craftName[64];    /* Craft name, the entry's first line. */
	char manufacturer[64]; /* Shown after FRONTSTR_484_MANUFACTURER. */
	char inUseBy[64];      /* Shown after FRONTSTR_485_IN_USE_BY. */
	/* Description, drawn wrapped under the special characteristics
	 * heading. */
	char description[256];
	/* Crew, shown for every genus but starfighter, mine and satellite. */
	char crew[64];
};

extern struct TechLibrarySpecText *g_techLibrarySpecTextTable;
extern struct CraftTechStats g_techLibraryCraftStats;
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
