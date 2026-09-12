#include "xvt/flight/object/damage.h"
#ifdef XVT_MODERN
#include "xvt_runtime/snapshot/cockpit_pages.h"
#endif
#include "xvt/assets/model_mesh.h"
#include "xvt/assets/object_type.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/flight_input.h"
#include "xvt/flight/hud/flight_text.h"
#include "xvt/flight/hud/hud.h"
#include "xvt/flight/hud/mfd.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/flight/transfm2.h"
#include "xvt/math/trig2.h"
#include "xvt/render/flight_palette.h"
#include "xvt/render/flight_sw.h"
#include "xvt/render/renderer.h"
#include "xvt/render/scene_billboard.h"

// GLOBAL: XVT 0x99F900
const char* g_strDamageSystemNames[DAMAGE_SYSTEM_ID_COUNT] = { 0 };
// GLOBAL: XVT 0x559790
int16_t g_damageMfdDamagedSystemCountCached = 0;
// GLOBAL: XVT 0x559794
int16_t g_damageMfdRedrawAllRows = 0;
// GLOBAL: XVT 0x559798
int16_t g_damageMfdLastSelectedSystemId = 0;
// GLOBAL: XVT 0x55979C
int16_t g_damageMfdSelectionChanged = 0;
// GLOBAL: XVT 0xA004D4
int16_t g_damageMfdCurrentSystemId = 0;
// GLOBAL: XVT 0x9EC60A
uint16_t g_damageBillboardMeshCount = 0;

// FLAGS: /O2 /G5
// FUNCTION: XVT 0x41FC20
void Damage_QueueCraftBillboards(uint16_t objectIndex) {
	uint16_t objectType = g_objectTable[objectIndex].objectType;
	Damage_QueueCraftBillboardsForObjectType(objectIndex, objectType);
}

// FUNCTION: XVT 0x41FC50
uint16_t Damage_QueueCraftBillboardsForObjectType(unsigned int objectIndex, int objectType) {
	uint16_t billboardAngle;
	uint16_t haveBillboardAngle;
	uint16_t savedRenderObjectRef;
	uint16_t meshIndex;

#ifdef XVT_MODERN
	haveBillboardAngle = 0;
#endif
	g_billboardObjectOrTypeIndex = (uint16_t)objectIndex;
	savedRenderObjectRef = g_renderObjectRef;
	g_billboardTargetSelectionState = 0;
	if ((uint16_t)objectIndex == g_localBeamTargetObjIdx) {
		g_billboardTargetSelectionState = 1;
		g_renderObjectRef = (uint16_t)objectIndex;
	}

	if (objectType < 73) {
		g_damageBillboardMeshCount = (uint16_t)g_objectTypeMeshCache[objectType].meshCount;
	} else {
		g_damageBillboardMeshCount = (uint16_t)ModelMesh_GetObjectTypeMeshCount(objectType);
	}

	meshIndex = 0;
	if (g_damageBillboardMeshCount > 0) {
		billboardAngle = haveBillboardAngle;
		do {
			int meshType;
			int meshTypeIndex;

			g_billboardModelNodeSwitchIndex = meshIndex;
			meshTypeIndex = meshIndex;
			if (objectType < 73) {
				if (meshTypeIndex < 0) {
					meshType = MESH_COMPONENT_00_HULL;
				} else {
					if (meshTypeIndex >= g_objectTypeMeshCache[objectType].meshCount) {
						meshTypeIndex = g_objectTypeMeshCache[objectType].meshCount - 1;
					}
					meshType = g_objectTypeMeshCache[objectType].meshTypes[meshTypeIndex];
				}
			} else {
				meshType = ModelMesh_GetObjectTypeMeshType(objectType, meshTypeIndex);
			}

			if (g_billboardTargetSelectionState == 2) {
				g_billboardTargetSelectionState = 0;
			}
			if (meshIndex == g_renderTargetComponentIdx && g_billboardTargetSelectionState == 0) {
				g_billboardTargetSelectionState = 2;
			}

			if (objectIndex < (unsigned int)g_activeRegionCraftObjectSlotEnd) {
				int16_t componentState;

				componentState = 0;
				if (g_objectTable[objectIndex].mobj != 0 && g_objectTable[objectIndex].mobj->pCraft != 0) {
					componentState = g_objectTable[objectIndex].mobj->pCraft->componentState[meshIndex];
				}
				if (componentState != 0) {
					continue;
				}
			}

			if ((int16_t)meshType == MESH_COMPONENT_03_FUSELAGE &&
				objectIndex < (unsigned int)g_activeRegionCraftObjectSlotEnd) {
				int16_t frame;
				uint16_t frameIndex;

				frameIndex = 0;
				if (g_objectTable[objectIndex].mobj != 0 && g_objectTable[objectIndex].mobj->pCraft != 0) {
					frameIndex =
						g_objectTable[objectIndex].mobj->pCraft->componentState[g_damageBillboardMeshCount];
				}
				frame = g_fuselageDamageTextureFrameSequence[frameIndex];
				if ((uint16_t)frame >= 0x8000 && (uint16_t)frame < 0xFF00) {
					int screenX;

					if (haveBillboardAngle == 0) {
						int matrixX;
						int matrixY;
						int absR0Z;
						int absR1Z;

						absR0Z = g_objViewMat_R0_Z;
						absR1Z = g_objViewMat_R1_Z;
						if (absR0Z < 0) {
							absR0Z = (int)(0u - (uint32_t)absR0Z);
						}
						if (absR1Z < 0) {
							absR1Z = (int)(0u - (uint32_t)absR1Z);
						}
						if (absR1Z > absR0Z) {
							matrixX = g_objViewMat_R0_X;
							matrixY = g_objViewMat_R0_Y;
						} else {
							matrixX = g_objViewMat_R1_X;
							matrixY = g_objViewMat_R1_Y;
						}

						if (matrixX < 0) {
							billboardAngle = (uint16_t)trig2_arctan(matrixY, (int)(0u - (uint32_t)matrixX));
						} else {
							billboardAngle = (uint16_t)-trig2_arctan(matrixY, matrixX);
						}
						haveBillboardAngle = 1;
					}

					billboardAngle = (uint16_t)(billboardAngle + g_objectTable[objectIndex].roll);
					screenX = TRANSFM2_ProjectScreenX(viewX, depthZ);
					if ((screenX & (int)0xFFFF0000) <= 0 && (screenX & (int)0xFFFF0000) >= -65536) {
						int screenY;

						screenY = TRANSFM2_ProjectScreenY(viewY, depthZ);
						if ((screenY & (int)0xFFFF0000) <= 0 && (screenY & (int)0xFFFF0000) >= -65536) {
							uint16_t viewportHalfHeight;

							viewportHalfHeight = g_flightVpHeight >> 1;
							screenY -= viewportHalfHeight;
							screenY = viewportHalfHeight - screenY;
							SceneBillboard_QueueProjectedTextured(g_billboardObjectOrTypeIndex,
																  (uint16_t)frame, 0x100, (int16_t)screenX,
																  (int16_t)screenY, depthZ, billboardAngle);
						}
					}
				}
			}

		} while (g_damageBillboardMeshCount > ++meshIndex);
	}

	g_renderObjectRef = savedRenderObjectRef;
	return savedRenderObjectRef;
}

// FUNCTION: XVT 0x46B650
int16_t Damage_DisplayMfdPage(void) {
	const int defaultWidth = 320;
	const int defaultHeight = 200;
	const int hudStateIndex = HUD_MFD_DAMAGE_ELEMENT;
	uint16_t systemIds[CRAFT_SUBSYSTEM_COUNT];
	CraftData* craft;
	int objectIndex;
	int16_t displaySlot;
	int16_t damagedSystemCount;
	int16_t allSystemsOk;
	int16_t sourceLeft;
	int16_t sourceTop;
	int16_t sourceRight;
	int16_t sourceBottom;
	uint16_t lineHeight;
	uint16_t rowTop;
	int16_t rowBottom;
	int pitchBytes;
	objectIndex = g_players[g_localPlayer].objectIndex;
	if (objectIndex == -1) {
		if (g_hudElementStateCache[g_hudInstrumentSetBaseIndex + hudStateIndex] !=
			g_mfdPageStates[MFD_PAGE_DAMAGE]) {
			pitchBytes = g_flight16bppBytesPerPixel;
			pitchBytes *= g_screenWidth;
			FlightSw_SetRenderTarget(g_flightOffscreenBuffer, g_screenWidth, g_screenHeight, pitchBytes);
			FlightText_SetFontTier(0);
			sourceLeft = g_mfdDamageBlitSourceX + 2;
			sourceTop = g_mfdDamageBlitSourceY + 2;
			sourceRight = g_mfdDamageBlitSourceX + g_mfdDamageBlitWidth - 2;
			sourceBottom = g_mfdDamageBlitSourceY + g_mfdDamageBlitHeight - 2;
			FlightText_SetBackgroundColor(g_flightColorEscapeBypassChar);
			FlightText_SetClipRect(sourceLeft - 2, sourceTop - 2, sourceRight + 2, sourceBottom + 2);
			g_flightFillClipRectFn();
			if (g_mfdPageStates[MFD_PAGE_DAMAGE] == MFD_PAGE_STATE_CLOSING &&
				g_mfdActivePage == MFD_PAGE_DAMAGE) {
				g_mfdActivePage = g_mfdSecondaryPage;
				g_mfdSecondaryPage = Mfd_FindSecondaryOpenPage();
			}
			FlightSw_SetRenderTarget(NULL, defaultWidth, defaultHeight, 0);
		}
		return 0;
	}
	craft = g_objectTable[objectIndex].mobj->pCraft;
	if (craft == NULL)
		return 0;

	for (displaySlot = 0; displaySlot < CRAFT_SUBSYSTEM_COUNT; ++displaySlot)
		systemIds[craft->systemDisplaySlotBySystem[displaySlot]] = (int16_t)displaySlot;
	displaySlot = 0;
	allSystemsOk = 1;
	damagedSystemCount = 0;
	for (; displaySlot < CRAFT_SUBSYSTEM_COUNT; ++displaySlot) {
		uint16_t systemId = systemIds[displaySlot];
		if (craft->systemHealth[systemId] == 0 && (g_subsystemIdToFlag[systemId] & craft->systemFlags) != 0) {
			allSystemsOk = 0;
			++damagedSystemCount;
		}
	}
	if (allSystemsOk != 0)
		g_mfdPageStates[MFD_PAGE_DAMAGE] = MFD_PAGE_STATE_CLOSED;

	pitchBytes = g_flight16bppBytesPerPixel;
	pitchBytes *= g_screenWidth;
	FlightSw_SetRenderTarget(g_flightOffscreenBuffer, g_screenWidth, g_screenHeight, pitchBytes);
	FlightText_SetFontTier(0);
	sourceLeft = g_mfdDamageBlitSourceX + 2;
	sourceTop = g_mfdDamageBlitSourceY + 2;
	sourceRight = g_mfdDamageBlitSourceX + g_mfdDamageBlitWidth - 2;
	sourceBottom = g_mfdDamageBlitSourceY + g_mfdDamageBlitHeight - 2;
#ifdef XVT_MODERN
	XvtCockpitPages_SetOrigin(MFD_PAGE_DAMAGE, g_mfdDamageBlitSourceX, g_mfdDamageBlitSourceY);
#endif
	rowTop = sourceTop;
	lineHeight = g_flightFontLineHeight + 1;
	if (g_hudElementStateCache[g_hudInstrumentSetBaseIndex + hudStateIndex] !=
		g_mfdPageStates[MFD_PAGE_DAMAGE]) {
#ifdef XVT_MODERN
		XvtCockpitPages_Clear(MFD_PAGE_DAMAGE);
#endif
		FlightText_SetBackgroundColor(g_flightColorEscapeBypassChar);
		FlightText_SetClipRect(sourceLeft - 2, sourceTop - 2, sourceRight + 2, sourceBottom + 2);
		g_flightFillClipRectFn();
		if (g_mfdPageStates[MFD_PAGE_DAMAGE] != MFD_PAGE_STATE_CLOSING && allSystemsOk == 0) {
			FlightText_SetBackgroundColor(g_flightColorEscapeBypassChar);
			FlightText_SetClearLineBackground(1);
			FlightText_SetColor(0x46);
			g_flightFillClipRectFn();
			FlightText_SetCursor(sourceLeft, sourceTop);
#ifdef XVT_MODERN
			XvtCockpitPages_RecordBackground(MFD_PAGE_DAMAGE);
			XvtCockpitPages_BeginSection(MFD_PAGE_DAMAGE, XVT_COCKPIT_PAGE_HEADER);
#endif
			FlightText_DrawStringCentered(g_strDamageSystemNames[DAMAGE_SYSTEM_10_DAMAGE_ASSESSMENT]);
#ifdef XVT_MODERN
			XvtCockpitPages_EndSection();
#endif
			g_damageMfdDamagedSystemCountCached = (int16_t)damagedSystemCount;
		} else {
			if (g_mfdActivePage == MFD_PAGE_DAMAGE) {
				g_mfdActivePage = g_mfdSecondaryPage;
				g_mfdSecondaryPage = Mfd_FindSecondaryOpenPage();
			}
			if (allSystemsOk != 0)
				g_hudElementStateCache[g_hudInstrumentSetBaseIndex + hudStateIndex] = MFD_PAGE_STATE_CLOSING;
			FlightSw_SetRenderTarget(NULL, defaultWidth, defaultHeight, 0);
			return 0;
		}
	} else {
		FlightText_SetBackgroundColor(g_flightColorEscapeBypassChar);
		FlightText_SetClearLineBackground(1);
	}
	{
		rowTop += lineHeight;
		rowBottom = rowTop + lineHeight * damagedSystemCount + 2;
		if (damagedSystemCount != g_damageMfdDamagedSystemCountCached) {
			FlightText_SetClipRect(sourceLeft - 2, rowTop, sourceRight + 2,
								   rowTop + lineHeight * (g_damageMfdDamagedSystemCountCached + 1));
			g_flightFillClipRectFn();
			g_damageMfdCurrentSystemId = -1;
			g_damageMfdDamagedSystemCountCached = (int16_t)damagedSystemCount;
		}
		if (g_players[g_localPlayer].mapCameraState == 0 && g_mfdActivePage == MFD_PAGE_NONE)
			g_mfdActivePage = MFD_PAGE_DAMAGE;
		if (g_mfdActivePage == MFD_PAGE_DAMAGE) {
			FlightText_SetBackgroundColor(0x46);
			FlightText_SetClipRect(sourceLeft - 2, sourceTop - 2, sourceRight + 2, rowBottom + 2);
#ifdef XVT_MODERN
			XvtCockpitPages_RecordBorder(MFD_PAGE_DAMAGE);
#endif
			g_flightFillRectClippedFn(sourceLeft - 2, sourceTop - 2, sourceRight + 2, rowBottom + 2, 1);
		} else if (g_mfdSecondaryPage == MFD_PAGE_DAMAGE) {
			FlightText_SetBackgroundColor(g_flightColorEscapeBypassChar);
			FlightText_SetClipRect(sourceLeft - 2, sourceTop - 2, sourceRight + 2, rowBottom + 2);
#ifdef XVT_MODERN
			XvtCockpitPages_RecordBorder(MFD_PAGE_DAMAGE);
#endif
			g_flightFillRectClippedFn(sourceLeft - 2, sourceTop - 2, sourceRight + 2, rowBottom + 2, 1);
		}
		FlightText_SetClipRect(sourceLeft, rowTop, sourceRight, rowBottom);
		FlightText_SetBackgroundColor(g_flightColorEscapeBypassChar);
		FlightText_SetColor(0x43);
		g_damageMfdRedrawAllRows = 1;
		if (g_hudElementStateCache[g_hudInstrumentSetBaseIndex + hudStateIndex] !=
			g_mfdPageStates[MFD_PAGE_DAMAGE]) {
			g_damageMfdCurrentSystemId = -1;
			g_damageMfdSelectionChanged = 1;
		} else if (g_mfdActivePage == MFD_PAGE_DAMAGE && g_flightPlayerCount == 1) {
			switch (g_currentActionKey) {
				case 0x0D: {
					uint8_t selectedSlot = craft->systemDisplaySlotBySystem[g_damageMfdCurrentSystemId];
					for (displaySlot = 0; displaySlot < CRAFT_SUBSYSTEM_COUNT; ++displaySlot) {
						uint8_t slot = craft->systemDisplaySlotBySystem[displaySlot];
						if (slot < selectedSlot)
							craft->systemDisplaySlotBySystem[displaySlot] = slot + 1;
					}
					craft->systemDisplaySlotBySystem[g_damageMfdCurrentSystemId] = 0;
					g_damageMfdSelectionChanged = 1;
					break;
				}
				case 0xA6: {
					int16_t adjacent = g_damageMfdCurrentSystemId;
					do {
						adjacent = Damage_FindAdjacentDamagedSystem(adjacent, (uint16_t)0xFFFF);
						if ((g_subsystemIdToFlag[adjacent] & craft->systemFlags) != 0 &&
							craft->systemHealth[adjacent] == 0)
							g_damageMfdCurrentSystemId = adjacent;
					} while ((g_subsystemIdToFlag[adjacent] & craft->systemFlags) == 0);
					g_damageMfdSelectionChanged = 1;
					break;
				}
				case 0xA7: {
					int16_t adjacent = g_damageMfdCurrentSystemId;
					do {
						adjacent = Damage_FindAdjacentDamagedSystem(adjacent, 1);
						if ((g_subsystemIdToFlag[adjacent] & craft->systemFlags) != 0 &&
							craft->systemHealth[adjacent] == 0)
							g_damageMfdCurrentSystemId = adjacent;
					} while ((g_subsystemIdToFlag[g_damageMfdCurrentSystemId] & craft->systemFlags) == 0);
					g_damageMfdSelectionChanged = 1;
					break;
				}
				default:
					break;
			}
		}
		for (displaySlot = 0; displaySlot < CRAFT_SUBSYSTEM_COUNT; ++displaySlot)
			systemIds[craft->systemDisplaySlotBySystem[displaySlot]] = (int16_t)displaySlot;
#ifdef XVT_MODERN
		XvtCockpitPages_BeginSection(MFD_PAGE_DAMAGE, XVT_COCKPIT_PAGE_BODY);
#endif
		for (displaySlot = 0; displaySlot < CRAFT_SUBSYSTEM_COUNT; ++displaySlot) {
			uint16_t systemId = systemIds[displaySlot];
			if (craft->systemHealth[systemId] == 0 &&
				(g_subsystemIdToFlag[systemId] & craft->systemFlags) != 0 &&
				rowTop + lineHeight < rowBottom) {
				FlightText_SetClipRect(sourceLeft, rowTop, sourceRight, rowTop + lineHeight);
				FlightText_SetCursor(sourceLeft, rowTop);
				if (g_damageMfdCurrentSystemId == -1) {
					g_damageMfdCurrentSystemId = (int16_t)systemId;
					g_damageMfdLastSelectedSystemId = (int16_t)systemId;
				}
				if (g_damageMfdCurrentSystemId == systemId)
					FlightText_SetBackgroundColor(0x33);
				else
					FlightText_SetBackgroundColor(g_flightColorEscapeBypassChar);
#ifdef XVT_MODERN
				XvtCockpitPages_RecordRow(systemId, g_damageMfdCurrentSystemId == systemId);
#endif
				if (g_damageMfdRedrawAllRows != 0 || g_damageMfdCurrentSystemId == systemId ||
					g_damageMfdLastSelectedSystemId == systemId)
					Damage_DrawMfdSystemStatusRow((DamageSystemId)systemId, (int16_t)rowTop,
												  (int16_t)sourceLeft);
				rowTop += lineHeight;
			}
		}
#ifdef XVT_MODERN
		XvtCockpitPages_EndSection();
		XvtCockpitPages_RecordScroll(MFD_PAGE_DAMAGE, 0, damagedSystemCount, g_damageMfdCurrentSystemId);
#endif
		g_damageMfdSelectionChanged = 0;
		g_damageMfdRedrawAllRows = 0;
		g_damageMfdLastSelectedSystemId = g_damageMfdCurrentSystemId;
		FlightSw_SetRenderTarget(NULL, defaultWidth, defaultHeight, 0);
		return 1;
	}
}

// FUNCTION: XVT 0x46BE10
int16_t Damage_FindAdjacentDamagedSystem(int16_t currentSystemIdx, int16_t directionStep) {
	uint8_t systemByDisplaySlot[CRAFT_SUBSYSTEM_COUNT];
	int systemIdx;
	CraftData* craft;
	int16_t previousDamagedSystem;
	int displaySlot;
	int16_t result;
	unsigned int promotedCurrentSystem;
	uint16_t displaySystem;

	craft = g_objectTable[g_players[g_localPlayer].objectIndex].mobj->pCraft;
	systemIdx = 0;
	do {
		systemByDisplaySlot[craft->systemDisplaySlotBySystem[systemIdx]] = systemIdx;
		++systemIdx;
	} while (systemIdx < CRAFT_SUBSYSTEM_COUNT);

	previousDamagedSystem = -1;
	displaySlot = 0;
	result = currentSystemIdx;
	for (;;) {
		displaySystem = systemByDisplaySlot[displaySlot];
		systemIdx = displaySystem;
		if (craft->systemHealth[systemIdx] == 0) {
			promotedCurrentSystem = (uint16_t)result;
			if (promotedCurrentSystem == (unsigned int)systemIdx) {
				break;
			}
			previousDamagedSystem = displaySystem;
		}
		++displaySlot;
		if (displaySlot >= CRAFT_SUBSYSTEM_COUNT) {
			return 0;
		}
	}

	if (directionStep == -1) {
		if (previousDamagedSystem == -1) {
			displaySlot = CRAFT_SUBSYSTEM_COUNT - 1;
			while (craft->systemHealth[systemByDisplaySlot[displaySlot]] == 0) {
				--displaySlot;
				if (displaySlot < 0) {
					return systemByDisplaySlot[CRAFT_SUBSYSTEM_COUNT - 1];
				}
			}
			return systemByDisplaySlot[displaySlot];
		} else {
			return previousDamagedSystem;
		}
	}

	while (++displaySlot < CRAFT_SUBSYSTEM_COUNT) {
		systemIdx = systemByDisplaySlot[displaySlot];
		if (craft->systemHealth[systemIdx] == 0) {
			result = systemByDisplaySlot[displaySlot];
			return result;
		}
	}
	for (displaySlot = 0; displaySlot < CRAFT_SUBSYSTEM_COUNT; ++displaySlot) {
		systemIdx = systemByDisplaySlot[displaySlot];
		if (craft->systemHealth[systemIdx] != 0) {
			return systemByDisplaySlot[displaySlot];
		}
	}
	displaySlot = 0;
	while (displaySlot < CRAFT_SUBSYSTEM_COUNT) {
		systemIdx = systemByDisplaySlot[displaySlot];
		if (craft->systemHealth[systemIdx] == 0) {
			return systemByDisplaySlot[displaySlot];
		}
		++displaySlot;
	}
	return result;
}

// FUNCTION: XVT 0x46BF90
void Damage_DrawMfdSystemStatusRow(DamageSystemId systemId, int16_t y, int16_t valueX) {
	MobileObject* mobileObject;
	CraftData* craft;
	uint16_t subsystemFlag;
	uint16_t health;
	uint16_t repairSeconds;
	uint8_t repairMinutes;
	uint8_t remainingSeconds;
	uint8_t minuteTens;
	uint8_t secondTens;
	uint16_t healthValue;
	int16_t healthTens;
	char statusText[8];

	mobileObject = g_objectTable[g_players[g_localPlayer].objectIndex].mobj;
	subsystemFlag = g_subsystemIdToFlag[(uint16_t)systemId];
	craft = mobileObject->pCraft;
	if ((craft->systemFlags & subsystemFlag) == 0) {
		FlightText_SetColor(0x41);
		statusText[0] = 'N';
		statusText[1] = '/';
		statusText[2] = 'A';
		statusText[3] = '\0';
	} else {
		health = craft->systemHealth[(uint16_t)systemId];
		if (health == 0) {
			FlightText_SetColor(0x4A);
			repairSeconds = craft->systemTimer[(uint16_t)systemId];
			repairMinutes = repairSeconds / 60;
			remainingSeconds = repairSeconds - 60 * repairMinutes;
			minuteTens = repairMinutes / 10;
			statusText[0] = minuteTens + '0';
			statusText[2] = ':';
			statusText[1] = repairMinutes - 10 * minuteTens + '0';
			secondTens = remainingSeconds / 10;
			statusText[3] = secondTens + '0';
			statusText[4] = remainingSeconds - 10 * secondTens + '0';
			statusText[5] = '\0';
		} else if (health == 100) {
			FlightText_SetColor(0x52);
			statusText[0] = '1';
			statusText[1] = '0';
			statusText[2] = '0';
			statusText[3] = '%';
			statusText[4] = '\0';
		} else {
			FlightText_SetColor(0x4E);
			healthTens = craft->systemHealth[(uint16_t)systemId] / 10;
			statusText[0] = healthTens + '0';
			healthValue = craft->systemHealth[(uint16_t)systemId];
			statusText[2] = '%';
			statusText[1] = healthValue - 10 * healthTens + '0';
			statusText[3] = '\0';
		}
	}

	g_flightFillClipRectFn();
	FlightText_DrawString(g_strDamageSystemNames[(uint16_t)systemId]);
	g_flightDrawCharFn(10);
	FlightText_SetCursor((uint16_t)valueX, (uint16_t)y);
	FlightText_DrawStringRightAligned(statusText);
}
