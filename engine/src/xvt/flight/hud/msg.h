#ifndef XVT_FLIGHT_HUD_MSG_H
#define XVT_FLIGHT_HUD_MSG_H

#include "xvt/flight/mission/mission.h"
#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct HudInFlightMessageRecord {
	uint16_t stateOrMessageId;
	uint16_t voiceSfxId;
	uint16_t clockWord;
	uint8_t clockTick;
	uint8_t clockMinute;
	uint8_t clockHour;
	uint8_t paneType;
	uint16_t senderIff;
	uint8_t ageTicks;
	uint8_t showCount;
	char text[70];
};

extern uint16_t g_messageLogWriteIndex;
extern uint16_t g_messageLogTotalCount;
extern HudInFlightMessageRecord* g_messageLogRecords;
extern uint16_t g_messageLogWrapped;
extern uint16_t g_msgSenderIff;
extern uint16_t g_pendingHudMessageVoiceSfxId;
extern const char* g_strInFlightMessages[417];
extern uint16_t g_msgArgTable[4];
extern char g_flightSecondaryObjectNameBuffer[256];

/* Stored as int32_t in the binary (IDB enum InFlightMessageId). */
typedef int32_t InFlightMessageId;

enum {
	IFMSG_000_X_WING_VS_TIE_FIGHTER_VER_1_10_05_11_97 =
		0x0, ///< strings.txt line 1203: \06X-Wing vs. TIE Fighter Ver 1.10 05/11/97
	IFMSG_001_MISSION_PAUSED_PRESS_ANY_KEY_TO_CONTINUE =
		0x1,                         ///< strings.txt line 1204: \03Mission paused.  Press any key to continue
	IFMSG_002_MISSION_RESUMED = 0x2, ///< strings.txt line 1205: \03Mission resumed
	IFMSG_003_LASER_CANNONS_ARMED = 0x3,        ///< strings.txt line 1207: \03Laser cannons armed
	IFMSG_004_ION_CANNONS_ARMED = 0x4,          ///< strings.txt line 1208: \03Ion cannons armed
	IFMSG_005_CANNONS_SET_TO_SINGLE_FIRE = 0x5, ///< strings.txt line 1209: \03Cannons set to [single fire
	IFMSG_006_CANNONS_SET_TO_DUAL_FIRE = 0x6,   ///< strings.txt line 1210: \03Cannons set to [dual fire
	IFMSG_007_CANNONS_FIRE_LINKED = 0x7,        ///< strings.txt line 1211: \03Cannons [fire linked
	IFMSG_008_PROTON_TORPEDO_LAUNCHER_ARMED =
		0x8, ///< strings.txt line 1213: \03Proton torpedo launcher armed
	IFMSG_009_CONCUSSION_MISSILE_LAUNCHER_ARMED =
		0x9, ///< strings.txt line 1214: \03Concussion missile launcher armed
	IFMSG_010_ADV_PROTON_TORPEDO_LAUNCHER_ARMED =
		0xA, ///< strings.txt line 1215: \03Adv proton torpedo launcher armed
	IFMSG_011_ADV_CONCUSSION_MISSILE_LAUNCHER_ARMED =
		0xB, ///< strings.txt line 1216: \03Adv concussion missile launcher armed
	IFMSG_012_SPACE_BOMB_LAUNCHER_ARMED = 0xC,   ///< strings.txt line 1217: \03Space bomb launcher armed
	IFMSG_013_HEAVY_ROCKET_LAUNCHER_ARMED = 0xD, ///< strings.txt line 1218: \03Heavy rocket launcher armed
	IFMSG_014_MAGNETIC_PULSE_LAUNCHER_ARMED =
		0xE,                                  ///< strings.txt line 1219: \03Magnetic pulse launcher armed
	IFMSG_015_ION_PULSE_LAUNCHER_ARMED = 0xF, ///< strings.txt line 1220: \03Ion pulse launcher armed
	IFMSG_016_FUTURE_LAUNCHER_ARMED = 0x10,   ///< strings.txt line 1221: \03-future- launcher armed
	IFMSG_017_FUTURE_LAUNCHER_ARMED = 0x11,   ///< strings.txt line 1222: \03-future- launcher armed
	IFMSG_018_PROTON_TORPEDO_LAUNCHER_SET_TO_SINGLE_FIRE =
		0x12, ///< strings.txt line 1224: \03Proton torpedo launcher set to [single fire
	IFMSG_019_CONCUSSION_MISSILE_LAUNCHER_SET_TO_SINGLE_FIRE =
		0x13, ///< strings.txt line 1225: \03Concussion missile launcher set to [single fire
	IFMSG_020_ADV_PROTON_TORPEDO_LAUNCHER_SET_TO_SINGLE_FIRE =
		0x14, ///< strings.txt line 1226: \03Adv proton torpedo launcher set to [single fire
	IFMSG_021_ADV_CONCUSSION_MISSILE_LAUNCHER_SET_TO_SINGLE_FIRE =
		0x15, ///< strings.txt line 1227: \03Adv concussion missile launcher set to [single fire
	IFMSG_022_SPACE_BOMB_LAUNCHER_SET_TO_SINGLE_FIRE =
		0x16, ///< strings.txt line 1228: \03Space bomb launcher set to [single fire
	IFMSG_023_HEAVY_ROCKET_LAUNCHER_SET_TO_SINGLE_FIRE =
		0x17, ///< strings.txt line 1229: \03Heavy rocket launcher set to [single fire
	IFMSG_024_MAGNETIC_PULSE_LAUNCHER_SET_TO_SINGLE_FIRE =
		0x18, ///< strings.txt line 1230: \03Magnetic pulse launcher set to [single fire
	IFMSG_025_ION_PULSE_LAUNCHER_SET_TO_SINGLE_FIRE =
		0x19, ///< strings.txt line 1231: \03Ion pulse launcher set to [single fire
	IFMSG_026_FUTURE_LAUNCHER_SET_TO_SINGLE_FIRE =
		0x1A, ///< strings.txt line 1232: \03-future- launcher set to [single fire
	IFMSG_027_FUTURE_LAUNCHER_SET_TO_SINGLE_FIRE =
		0x1B, ///< strings.txt line 1233: \03-future- launcher set to [single fire
	IFMSG_028_PROTON_TORPEDO_LAUNCHER_SET_TO_DUAL_FIRE =
		0x1C, ///< strings.txt line 1235: \03Proton torpedo launcher set to [dual fire
	IFMSG_029_CONCUSSION_MISSILE_LAUNCHER_SET_TO_DUAL_FIRE =
		0x1D, ///< strings.txt line 1236: \03Concussion missile launcher set to [dual fire
	IFMSG_030_ADV_PROTON_TORPEDO_LAUNCHER_SET_TO_DUAL_FIRE =
		0x1E, ///< strings.txt line 1237: \03Adv proton torpedo launcher set to [dual fire
	IFMSG_031_ADV_CONCUSSION_MISSILE_LAUNCHER_SET_TO_DUAL_FIRE =
		0x1F, ///< strings.txt line 1238: \03Adv concussion missile launcher set to [dual fire
	IFMSG_032_SPACE_BOMB_LAUNCHER_SET_TO_DUAL_FIRE =
		0x20, ///< strings.txt line 1239: \03Space bomb launcher set to [dual fire
	IFMSG_033_HEAVY_ROCKET_LAUNCHER_SET_TO_DUAL_FIRE =
		0x21, ///< strings.txt line 1240: \03Heavy rocket launcher set to [dual fire
	IFMSG_034_MAGNETIC_PULSE_LAUNCHER_SET_TO_DUAL_FIRE =
		0x22, ///< strings.txt line 1241: \03Magnetic pulse launcher set to [dual fire
	IFMSG_035_ION_PULSE_LAUNCHER_SET_TO_DUAL_FIRE =
		0x23, ///< strings.txt line 1242: \03Ion pulse launcher set to [dual fire
	IFMSG_036_FUTURE_LAUNCHER_SET_TO_DUAL_FIRE =
		0x24, ///< strings.txt line 1243: \03-future- launcher set to [dual fire
	IFMSG_037_FUTURE_LAUNCHER_SET_TO_DUAL_FIRE =
		0x25, ///< strings.txt line 1244: \03-future- launcher set to [dual fire
	IFMSG_038_PROTON_TORPEDO_LAUNCHER_EMPTY =
		0x26, ///< strings.txt line 1246: \03Proton torpedo launcher empty
	IFMSG_039_CONCUSSION_MISSILE_LAUNCHER_EMPTY =
		0x27, ///< strings.txt line 1247: \03Concussion missile launcher empty
	IFMSG_040_ADV_PROTON_TORPEDO_LAUNCHER_EMPTY =
		0x28, ///< strings.txt line 1248: \03Adv proton torpedo launcher empty
	IFMSG_041_ADV_CONCUSSION_MISSILE_LAUNCHER_EMPTY =
		0x29, ///< strings.txt line 1249: \03Adv concussion missile launcher empty
	IFMSG_042_SPACE_BOMB_LAUNCHER_EMPTY = 0x2A,   ///< strings.txt line 1250: \03Space bomb launcher empty
	IFMSG_043_HEAVY_ROCKET_LAUNCHER_EMPTY = 0x2B, ///< strings.txt line 1251: \03Heavy rocket launcher empty
	IFMSG_044_MAGNETIC_PULSE_LAUNCHER_EMPTY =
		0x2C,                                  ///< strings.txt line 1252: \03Magnetic pulse launcher empty
	IFMSG_045_ION_PULSE_LAUNCHER_EMPTY = 0x2D, ///< strings.txt line 1253: \03Ion pulse launcher empty
	IFMSG_046_FUTURE_LAUNCHER_EMPTY = 0x2E,    ///< strings.txt line 1254: \03-future- launcher empty
	IFMSG_047_FUTURE_LAUNCHER_EMPTY = 0x2F,    ///< strings.txt line 1255: \03-future- launcher empty
	IFMSG_048_PROTON_TORPEDO_FIRED = 0x30,     ///< strings.txt line 1257: \03Proton torpedo fired
	IFMSG_049_CONCUSSION_MISSILE_FIRED = 0x31, ///< strings.txt line 1258: \03Concussion missile fired
	IFMSG_050_ADV_PROTON_TORPEDO_FIRED = 0x32, ///< strings.txt line 1259: \03Adv proton torpedo fired
	IFMSG_051_ADV_CONCUSSION_MISSILE_FIRED = 0x33, ///< strings.txt line 1260: \03Adv concussion missile fired
	IFMSG_052_SPACE_BOMB_RELEASED = 0x34,          ///< strings.txt line 1261: \03Space bomb released
	IFMSG_053_HEAVY_ROCKET_FIRED = 0x35,           ///< strings.txt line 1262: \03Heavy rocket fired
	IFMSG_054_MAGNETIC_PULSE_FIRED = 0x36,         ///< strings.txt line 1263: \03Magnetic pulse fired
	IFMSG_055_ION_PULSE_FIRED = 0x37,              ///< strings.txt line 1264: \03Ion pulse fired
	IFMSG_056_FUTURE_FIRED = 0x38,                 ///< strings.txt line 1265: \03-future- fired
	IFMSG_057_FUTURE_FIRED = 0x39,                 ///< strings.txt line 1266: \03-future- fired
	IFMSG_058_PROTON_TORPEDOES_FIRED = 0x3A,       ///< strings.txt line 1268: \03Proton torpedoes fired
	IFMSG_059_CONCUSSION_MISSILES_FIRED = 0x3B,    ///< strings.txt line 1269: \03Concussion missiles fired
	IFMSG_060_ADV_PROTON_TORPEDOES_FIRED = 0x3C,   ///< strings.txt line 1270: \03Adv proton torpedoes fired
	IFMSG_061_ADV_CONCUSSION_MISSILES_FIRED =
		0x3D,                                     ///< strings.txt line 1271: \03Adv concussion missiles fired
	IFMSG_062_SPACE_BOMBS_RELEASED = 0x3E,        ///< strings.txt line 1272: \03Space bombs released
	IFMSG_063_HEAVY_ROCKETS_FIRED = 0x3F,         ///< strings.txt line 1273: \03Heavy rockets fired
	IFMSG_064_DOUBLE_MAGNETIC_PULSE_FIRED = 0x40, ///< strings.txt line 1274: \03Double magnetic pulse fired
	IFMSG_065_DOUBLE_ION_PULSE_FIRED = 0x41,      ///< strings.txt line 1275: \03Double ion pulse fired
	IFMSG_066_FUTURE_FIRED = 0x42,                ///< strings.txt line 1276: \03-future- fired
	IFMSG_067_FUTURE_FIRED = 0x43,                ///< strings.txt line 1277: \03-future- fired
	IFMSG_068_SHIELDS_SET_FULLY_FORWARD = 0x44,   ///< strings.txt line 1279: \03Shields set [fully forward
	IFMSG_069_SHIELDS_SET_EVENLY_FORWARD_AND_AFT =
		0x45,                               ///< strings.txt line 1280: \03Shields set [evenly forward and aft
	IFMSG_070_SHIELDS_SET_FULLY_AFT = 0x46, ///< strings.txt line 1281: \03Shields set [fully aft
	IFMSG_071_CANNON_RECHARGE_FULLY_REDIRECTED_TO_ENGINES =
		0x47, ///< strings.txt line 1283: \03Cannon recharge [fully] redirected to engines
	IFMSG_072_CANNON_RECHARGE_PARTIALLY_REDIRECTED_TO_ENGINES =
		0x48, ///< strings.txt line 1284: \03Cannon recharge [partially] redirected to engines
	IFMSG_073_CANNON_RECHARGE_AT_MAINTENANCE_LEVEL =
		0x49, ///< strings.txt line 1285: \03Cannon recharge at [maintenance] level
	IFMSG_074_CANNON_RECHARGE_AT_INCREASED_RATE =
		0x4A, ///< strings.txt line 1286: \03Cannon recharge at [increased] rate
	IFMSG_075_CANNON_RECHARGE_AT_MAXIMUM_RATE =
		0x4B, ///< strings.txt line 1287: \03Cannon recharge at [maximum] rate
	IFMSG_076_SHIELD_RECHARGE_FULLY_REDIRECTED_TO_ENGINES =
		0x4C, ///< strings.txt line 1288: \03Shield recharge [fully] redirected to engines
	IFMSG_077_SHIELD_RECHARGE_PARTIALLY_REDIRECTED_TO_ENGINES =
		0x4D, ///< strings.txt line 1289: \03Shield recharge [partially] redirected to engines
	IFMSG_078_SHIELD_RECHARGE_AT_MAINTENANCE_LEVEL =
		0x4E, ///< strings.txt line 1290: \03Shield recharge at [maintenance] level
	IFMSG_079_SHIELD_RECHARGE_AT_INCREASED_RATE =
		0x4F, ///< strings.txt line 1291: \03Shield recharge at [increased] rate
	IFMSG_080_SHIELD_RECHARGE_AT_MAXIMUM_RATE =
		0x50, ///< strings.txt line 1292: \03Shield recharge at [maximum] rate
	IFMSG_081_BEAM_RECHARGE_FULLY_REDIRECTED_TO_ENGINES =
		0x51, ///< strings.txt line 1293: \03Beam recharge [fully] redirected to engines
	IFMSG_082_BEAM_RECHARGE_PARTIALLY_REDIRECTED_TO_ENGINES =
		0x52, ///< strings.txt line 1294: \03Beam recharge [partially] redirected to engines
	IFMSG_083_BEAM_RECHARGE_AT_MAINTENANCE_LEVEL =
		0x53, ///< strings.txt line 1295: \03Beam recharge at [maintenance] level
	IFMSG_084_BEAM_RECHARGE_AT_INCREASED_RATE =
		0x54, ///< strings.txt line 1296: \03Beam recharge at [increased] rate
	IFMSG_085_BEAM_RECHARGE_AT_MAXIMUM_RATE =
		0x55,                                 ///< strings.txt line 1297: \03Beam recharge at [maximum] rate
	IFMSG_086_ARG_SYSTEM_IS_ARG = 0x56,       ///< strings.txt line 1299: \03[*] system is *
	IFMSG_087_DAMAGED_AND_INOPERATIVE = 0x57, ///< strings.txt line 1300: damaged and inoperative
	IFMSG_088_REPAIRED = 0x58,                ///< strings.txt line 1301: repaired
	IFMSG_089_UNAVAILABLE = 0x59,             ///< strings.txt line 1303: unavailable
	IFMSG_090_ENGINE = 0x5A,                  ///< strings.txt line 1305: Engine
	IFMSG_091_FLIGHT_CONTROL = 0x5B,          ///< strings.txt line 1306: Flight control
	IFMSG_092_LASER = 0x5C,                   ///< strings.txt line 1307: Laser
	IFMSG_093_ION_CANNON = 0x5D,              ///< strings.txt line 1308: Ion Cannon
	IFMSG_094_WARHEAD_LAUNCHER = 0x5E,        ///< strings.txt line 1309: Warhead Launcher
	IFMSG_095_BEAM_WEAPON = 0x5F,             ///< strings.txt line 1310: Beam Weapon
	IFMSG_096_TARGETING_COMPUTER = 0x60,      ///< strings.txt line 1311: Targeting computer
	IFMSG_097_COUNTERMEASURE = 0x61,          ///< strings.txt line 1312: Countermeasure
	IFMSG_098_SHIELD = 0x62,                  ///< strings.txt line 1313: Shield
	IFMSG_099_HYPERDRIVE = 0x63,              ///< strings.txt line 1314: Hyperdrive
	IFMSG_100_LAUNCHER = 0x64,                ///< strings.txt line 1315: Launcher
	IFMSG_101_COMMUNICATION = 0x65,           ///< strings.txt line 1316: Communication
	IFMSG_102_GRAPHICS_DETAIL_SET_TO_LOWEST_LEVEL =
		0x66, ///< strings.txt line 1318: \06Graphics detail set to [lowest] level
	IFMSG_103_GRAPHICS_DETAIL_SET_TO_LOW_LEVEL =
		0x67, ///< strings.txt line 1319: \06Graphics detail set to [low] level
	IFMSG_104_GRAPHICS_DETAIL_SET_TO_HIGH_LEVEL =
		0x68, ///< strings.txt line 1320: \06Graphics detail set to [high] level
	IFMSG_105_GRAPHICS_DETAIL_SET_TO_HIGHEST_LEVEL =
		0x69, ///< strings.txt line 1321: \06Graphics detail set to [highest] level
	IFMSG_106_PREPARING_FOR_JUMP_TO_LIGHT_SPEED =
		0x6A, ///< strings.txt line 1323: \03Preparing for jump to light speed..
	IFMSG_107_HYPERSPACE_JUMP_ABORTED = 0x6B, ///< strings.txt line 1324: \03Hyperspace jump aborted
	IFMSG_108_ENTERING_HYPERSPACE = 0x6C,     ///< strings.txt line 1325: \03Entering hyperspace
	IFMSG_109_INTERDICTOR_PREVENTS_HYPERDRIVE_UNIT_FROM_FUNCTIONING =
		0x6D, ///< strings.txt line 1326: \03Interdictor prevents hyperdrive unit from functioning!
	IFMSG_110_NO_HYPERDRIVE_RETURN_TO_ARG = 0x6E, ///< strings.txt line 1327: \03No hyperdrive.  Return to [*]
	IFMSG_111_NO_HYPERDRIVE_RETURN_TO_ARG_OR_TO_ARG =
		0x6F, ///< strings.txt line 1328: \03No hyperdrive.  Return to [*] or to [*]
	IFMSG_112_OBJECT_DETECTED_IN_JUMP_PATH_HYPERSPACE_JUMP_ABORTED =
		0x70, ///< strings.txt line 1329: \03Object detected in jump path, hyperspace jump aborted
	IFMSG_113_ARG_IS_INITIATING_HYPERJUMP = 0x71, ///< strings.txt line 1330: \02[*] is initiating hyperjump
	IFMSG_114_NEW_CRAFT_ALERT_ARG_ARG_AT_ARG_KM =
		0x72, ///< strings.txt line 1332: \02New craft alert, [&\01 *] at [&\02] km
	IFMSG_115_NEW_CRAFT_ALERT_ARG_ARG_S_AT_ARG_KM =
		0x73, ///< strings.txt line 1333: \02New craft alert, [&\01 *s] at [&\02] km
	IFMSG_116_MISSILE_WARNING_KEY_TO_TARGET =
		0x74,                              ///< strings.txt line 1335: \04Missile Warning! [Key to target?
	IFMSG_117_MISSILE_WAS_TARGETED = 0x75, ///< strings.txt line 1336: \04Missile was targeted
	IFMSG_118_AN_ENEMY_CRAFT_IS_ATTEMPTING_MISSILE_LOCK_SHOULD_IT_BE_TARGETED =
		0x76, ///< strings.txt line 1337: \04An enemy craft is attempting missile lock, [should it be
			  ///< targeted?
	IFMSG_119_THROTTLE_SET_TO_NO_POWER = 0x77,   ///< strings.txt line 1339: \03Throttle set to [no] power
	IFMSG_120_THROTTLE_SET_TO_1_3_POWER = 0x78,  ///< strings.txt line 1340: \03Throttle set to [1/3] power
	IFMSG_121_THROTTLE_SET_TO_2_3_POWER = 0x79,  ///< strings.txt line 1341: \03Throttle set to [2/3] power
	IFMSG_122_THROTTLE_SET_TO_FULL_POWER = 0x7A, ///< strings.txt line 1342: \03Throttle set to [full] power
	IFMSG_123_CONFIGURATION_SAVED_TO_PRESET =
		0x7B, ///< strings.txt line 1343: \03Configuration saved to preset
	IFMSG_124_COMPONENT_TRACKING_IN_CMD_DISPLAY_TURNED_OFF =
		0x7C, ///< strings.txt line 1345: \03Component tracking in CMD display turned [OFF]
	IFMSG_125_COMPONENT_TRACKING_IN_CMD_DISPLAY_TURNED_ON =
		0x7D, ///< strings.txt line 1346: \03Component tracking in CMD display turned [ON]
	IFMSG_126_S_FOILS_OPENING = 0x7E, ///< strings.txt line 1348: \03S-Foils opening
	IFMSG_127_S_FOILS_CLOSING = 0x7F, ///< strings.txt line 1349: \03S-Foils closing
	IFMSG_128_S_FOILS_HAVE_REACHED_OPEN_POSITION =
		0x80, ///< strings.txt line 1350: \03S-Foils have reached [open] position
	IFMSG_129_S_FOILS_HAVE_REACHED_CLOSED_POSITION =
		0x81, ///< strings.txt line 1351: \03S-Foils have reached [closed] position
	IFMSG_130_CANNONS_CANNOT_FIRE_WITH_S_FOILS_CLOSED =
		0x82, ///< strings.txt line 1352: \03Cannons cannot fire with S-Foils closed
	IFMSG_131_TRANSFERRING_PARTIAL_POWER_FROM_SHIELDS_TO_CANNON_SYSTEM =
		0x83, ///< strings.txt line 1354: \03Transferring partial power from shields to [cannon system
	IFMSG_132_TRANSFERRING_PARTIAL_POWER_FROM_CANNONS_TO_SHIELDS =
		0x84, ///< strings.txt line 1355: \03Transferring partial power from cannons to [shields
	IFMSG_133_CRAFT_EVENT_WITH_NUMBER = 0x85,    ///< strings.txt line 1357: \02[* * &\02] *
	IFMSG_134_CRAFT_EVENT_WITHOUT_NUMBER = 0x86, ///< strings.txt line 1358: \02[* *] *
	IFMSG_135_HAS_ENTERED_HYPERSPACE = 0x87,     ///< strings.txt line 1360: has entered hyperspace
	IFMSG_136_HAS_BEEN_DESTROYED = 0x88,         ///< strings.txt line 1361: has been [destroyed
	IFMSG_137_HAS_BEEN_DISABLED = 0x89,          ///< strings.txt line 1362: has been [disabled
	IFMSG_138_HAS_BEEN_REPAIRED = 0x8A,          ///< strings.txt line 1363: has been [repaired
	IFMSG_139_HAS_BEEN_CAPTURED = 0x8B,          ///< strings.txt line 1364: has been [captured
	IFMSG_140_HAS_DOCKED = 0x8C,                 ///< strings.txt line 1365: has [docked
	IFMSG_141_HAS_ENTERED_HANGAR = 0x8D,         ///< strings.txt line 1366: has [entered hangar
	IFMSG_142_HAS_BEEN_INSPECTED = 0x8E,         ///< strings.txt line 1367: has been [inspected
	IFMSG_143_IS_INITIATING_HYPERJUMP = 0x8F,    ///< strings.txt line 1368: is initiating hyperjump
	IFMSG_144_FLIGHT_GROUP_EVENT_FROM_FLIGHT_GROUP =
		0x90,                            ///< strings.txt line 1370: \03[* &\01] from flight group [*] *
	IFMSG_145_FLIGHT_GROUP_EVENT = 0x91, ///< strings.txt line 1371: \03[* *] *
	IFMSG_146_CRAFT_NOT_RESPONDING_TO_ORDER =
		0x92,                                    ///< strings.txt line 1373: \02Craft not responding to order
	IFMSG_147_ROGER_CRAFT_WITH_NUMBER = 0x93,    ///< strings.txt line 1374: \02Roger, [* * &\02] *
	IFMSG_148_ROGER_CRAFT_WITHOUT_NUMBER = 0x94, ///< strings.txt line 1375: \02Roger, [* *] *
	IFMSG_149_HEADING_HOME = 0x95,               ///< strings.txt line 1376: heading home
	IFMSG_150_COMING_TO_YOUR_AID = 0x96,         ///< strings.txt line 1377: coming to your aid
	IFMSG_151_MAKING_EVASIVE_MANEUVER = 0x97,    ///< strings.txt line 1378: making evasive maneuver
	IFMSG_152_WAITING_FOR_FURTHER_ORDERS = 0x98, ///< strings.txt line 1379: waiting for further orders
	IFMSG_153_PROCEEDING_WITH_MISSION = 0x99,    ///< strings.txt line 1380: proceeding with mission
	IFMSG_154_USING_DESIGNATED_TARGET = 0x9A,    ///< strings.txt line 1381: using designated target
	IFMSG_155_IGNORING_TARGET = 0x9B,            ///< strings.txt line 1382: ignoring target
	IFMSG_156_REPORTS_DOCKING_OPERATION_COMPLETE =
		0x9C,                                  ///< strings.txt line 1384: reports docking operation complete
	IFMSG_157_CRAFT_REPORT_WITH_NUMBER = 0x9D, ///< strings.txt line 1385: \02[* * &\02] reporting in: *
	IFMSG_158_CRAFT_REPORT_WITHOUT_NUMBER = 0x9E, ///< strings.txt line 1386: \02[* *] reporting in: *
	IFMSG_159_HOLDING_STEADY = 0x9F,              ///< strings.txt line 1388: Holding steady
	IFMSG_160_PATROLLING = 0xA0,                  ///< strings.txt line 1389: Patrolling
	IFMSG_161_PROTECTING_CRAFT = 0xA1,            ///< strings.txt line 1390: Protecting craft
	IFMSG_162_PATROLLING_FOR_CRAFT_TO_ATTACK =
		0xA2,                                      ///< strings.txt line 1391: Patrolling for craft to attack
	IFMSG_163_PATROLLING_FOR_ESCORTERS = 0xA3,     ///< strings.txt line 1392: Patrolling for escorters
	IFMSG_164_ATTACKING_TARGET = 0xA4,             ///< strings.txt line 1393: Attacking target
	IFMSG_165_ATTACKING_TARGET = 0xA5,             ///< strings.txt line 1394: Attacking target
	IFMSG_166_EVADING = 0xA6,                      ///< strings.txt line 1395: Evading
	IFMSG_167_FOLLOWING_FLIGHT_LEADER = 0xA7,      ///< strings.txt line 1396: Following flight leader
	IFMSG_168_LOOKING_FOR_CRAFT_TO_DISABLE = 0xA8, ///< strings.txt line 1397: Looking for craft to disable
	IFMSG_169_ESCORTING = 0xA9,                    ///< strings.txt line 1398: Escorting
	IFMSG_170_LOOKING_FOR_CRAFT_TO_BOARD = 0xAA,   ///< strings.txt line 1399: Looking for craft to board
	IFMSG_171_LOOKING_FOR_CRAFT_TO_CAPTURE = 0xAB, ///< strings.txt line 1400: Looking for craft to capture
	IFMSG_172_LOOKING_FOR_CRAFT_TO_PICK_UP = 0xAC, ///< strings.txt line 1401: Looking for craft to pick up
	IFMSG_173_DOCKING = 0xAD,                      ///< strings.txt line 1402: Docking
	IFMSG_174_FLYING_TO_RENDEZVOUS_POINT = 0xAE,   ///< strings.txt line 1403: Flying to rendezvous point
	IFMSG_175_WAITING_FOR_BOARDING_CRAFT = 0xAF,   ///< strings.txt line 1404: Waiting for boarding craft
	IFMSG_176_AWAITING_BOARDING = 0xB0,            ///< strings.txt line 1405: Awaiting boarding
	IFMSG_177_HOLDING_STATION = 0xB1,              ///< strings.txt line 1406: Holding station
	IFMSG_178_FLYING_HOME = 0xB2,                  ///< strings.txt line 1407: Flying home
	IFMSG_179_ENTERING_HANGAR = 0xB3,              ///< strings.txt line 1408: Entering hangar
	IFMSG_180_EXITING_HANGAR = 0xB4,               ///< strings.txt line 1409: Exiting hangar
	IFMSG_181_HYPERING_OUT_OF_COMBAT_AREA = 0xB5,  ///< strings.txt line 1410: Hypering out of combat area
	IFMSG_182_HYPERING_INTO_COMBAT_AREA = 0xB6,    ///< strings.txt line 1411: Hypering into combat area
	IFMSG_183_FLYING_FLIGHT_PLAN = 0xB7,           ///< strings.txt line 1412: Flying flight plan
	IFMSG_184_AWAITING_RETURN_OF_CRAFT = 0xB8,     ///< strings.txt line 1413: Awaiting return of craft
	IFMSG_185_AWAITING_LAUNCH_OF_CRAFT = 0xB9,     ///< strings.txt line 1414: Awaiting launch of craft
	IFMSG_186_CHANGING_ORDERS = 0xBA,              ///< strings.txt line 1415: Changing orders
	IFMSG_187_AWAITING_FURTHER_ORDERS = 0xBB,      ///< strings.txt line 1416: Awaiting further orders
	IFMSG_188_WAITING = 0xBC,                      ///< strings.txt line 1417: Waiting
	IFMSG_189_DEPLOYING_CRAFT = 0xBD,              ///< strings.txt line 1418: Deploying craft
	IFMSG_190_SELF_DESTRUCTING = 0xBE,             ///< strings.txt line 1419: Self-destructing
	IFMSG_191_KAMIKAZE_ATTACK = 0xBF,              ///< strings.txt line 1420: Kamikaze attack
	IFMSG_192_ORBITTING = 0xC0,                    ///< strings.txt line 1421: Orbitting
	IFMSG_193_EXCELLENT_JOB_PRIMARY_MISSION_OBJECTIVES_COMPLETED =
		0xC1, ///< strings.txt line 1423: \02Excellent job!  Primary mission objectives completed
	IFMSG_194_SUPERB_WORK_SECONDARY_MISSION_OBJECTIVES_COMPLETED =
		0xC2, ///< strings.txt line 1424: \02Superb work! Secondary mission objectives completed
	IFMSG_195_SUPERLATIVE_JOB_BONUS_MISSION_OBJECTIVES_COMPLETED =
		0xC3, ///< strings.txt line 1425: \02Superlative job! Bonus mission objectives completed
	IFMSG_196_CODE_02_ARGUMENT = 0xC4, ///< strings.txt line 1426: \02*
	IFMSG_197_ARG_10000_POINTS_AWARDED_FOR_PREVIOUS_LEVELS =
		0xC5, ///< strings.txt line 1427: &\0010000 points awarded for previous levels
	IFMSG_198_CONGRATULATIONS_LEVEL_COMPLETED_TIME_LEFT_BONUS =
		0xC6, ///< strings.txt line 1428: \03Congratulations! Level Completed.  Time Left:       Bonus:
	IFMSG_199_BONUS_POINTS_AWARDED_ARG = 0xC7, ///< strings.txt line 1429: \03Bonus points awarded: [&\05]
	IFMSG_200_PENALTY_POINTS_DEDUCTED_ARG =
		0xC8,                                   ///< strings.txt line 1430: \10Penalty points deducted:[ &\05]
	IFMSG_201_MISSION_ENDS_IN_2_MINUTES = 0xC9, ///< strings.txt line 1431: \10Mission ends in [2] minutes
	IFMSG_202_MISSION_ENDS_IN_1_MINUTE = 0xCA,  ///< strings.txt line 1432: \10Mission ends in [1] minute
	IFMSG_203_MISSION_TIME_EXPIRING = 0xCB,     ///< strings.txt line 1433: \10Mission time expiring
	IFMSG_204_MISSION_TIME_LIMIT_IMPOSED_ARG_MINUTE_LEFT =
		0xCC, ///< strings.txt line 1434: \10Mission time limit imposed: [*] minute left
	IFMSG_205_MISSION_TIME_LIMIT_IMPOSED_ARG_MINUTES_LEFT =
		0xCD, ///< strings.txt line 1435: \10Mission time limit imposed: [*] minutes left
	IFMSG_206_MISSION_OBJECTIVES_CANNOT_BE_FINISHED_ABORT_MISSION =
		0xCE, ///< strings.txt line 1436: \02Mission objectives cannot be finished, abort mission!
	IFMSG_207_CODE_01_ARGUMENT = 0xCF, ///< strings.txt line 1437: \01*
	IFMSG_208_GOOD_WORK_OUR_FORCES_HAVE_BOUNCED_BACK_TO_PULL_OUT_A_DRAW =
		0xD0, ///< strings.txt line 1438: \02Good work. Our forces have bounced back to pull out a draw
	IFMSG_209_GOOD_WORK_THE_ALLIANCE_HAS_RALLIED_TO_GAIN_A_DRAW_FROM_THIS_MISSION =
		0xD1, ///< strings.txt line 1439: \02Good work. The Alliance has rallied to gain a draw from this
			  ///< mission
	IFMSG_210_WE_HAVE_LOST_OUR_VICTORY_THE_REBELS_HAVE_GAINED_A_DRAW =
		0xD2, ///< strings.txt line 1440: \02We have lost our victory.  The Rebels have gained a draw
	IFMSG_211_WE_HAVE_LOST_OUR_VICTORY_THE_EMPIRE_HAS_GAINED_A_DRAW =
		0xD3, ///< strings.txt line 1441: \02We have lost our victory.  The Empire has gained a draw
	IFMSG_212_YOU_HAVE_EJECTED_SAFELY = 0xD4, ///< strings.txt line 1443: \03You have ejected safely
	IFMSG_213_YOU_HAVE_DIED = 0xD5,           ///< strings.txt line 1444: \03You have died
	IFMSG_214_HIT_SPACE_TO_ACTIVATE_TRACTOR_BEAM_AND_ENTER_HANGAR =
		0xD6, ///< strings.txt line 1445: \04Hit [Space] to activate tractor beam and enter hangar
	IFMSG_215_PRESS_SPACE_TO_END_MISSION = 0xD7, ///< strings.txt line 1446: \04Press [Space] to end mission
	IFMSG_216_LEAVING_NOW_IS_2000_POINT_PENALTY_PRESS_SPACE_TO_QUIT_ANYWAY =
		0xD8, ///< strings.txt line 1447: \04Leaving now is [2000] point penalty! Press [Space] to quit anyway
	IFMSG_217_LEAVING_GAME_EARLY_SCORE_INCOMPLETE_PRESS_SPACE_TO_QUIT_ANYWAY =
		0xD9, ///< strings.txt line 1448: \04Leaving game early, score incomplete! Press [Space] to quit
			  ///< anyway
	IFMSG_218_WARNING_PRESSING_SPACE_WILL_DISCONNECT_FROM_THE_HOST =
		0xDA, ///< strings.txt line 1449: \04WARNING! Pressing [Space] will disconnect from the host!
	IFMSG_219_WARNING_YOU_ARE_THE_HOST_PRESSING_SPACE_WILL_ABORT_THIS_GAME =
		0xDB, ///< strings.txt line 1450: \04WARNING! You are the host! Pressing [Space] will abort this game!
	IFMSG_220_COLLISION_WITH_ANOTHER_CRAFT_HAS_OCCURRED =
		0xDC, ///< strings.txt line 1451: \03Collision with another craft has occurred!
	IFMSG_221_YOU_RE_TAKING_DAMAGE_FROM_ENGINE_WASH =
		0xDD, ///< strings.txt line 1452: \03You're taking damage from engine wash!
	IFMSG_222_NO_CRAFT_LEFT_TO_PILOT = 0xDE, ///< strings.txt line 1453: \03No craft left to pilot
	IFMSG_223_NO_CRAFT_TARGETED = 0xDF,      ///< strings.txt line 1455: \03No craft targeted
	IFMSG_224_THREAT_DISPLAY_TARGET_NO_LONGER_AVAILABLE =
		0xE0, ///< strings.txt line 1456: \03Threat display target no longer available
	IFMSG_225_AUTO_LOCATING_OF_PLAYERS_NOT_ENABLED_FOR_THIS_MISSION =
		0xE1, ///< strings.txt line 1457: \03Auto-locating of players not enabled for this mission
	IFMSG_226_YOUR_CRAFT_DOES_NOT_HAVE_A_SHIELD_SYSTEM =
		0xE2, ///< strings.txt line 1459: \03Your craft does not have a [shield] system
	IFMSG_227_YOUR_CRAFT_DOES_NOT_HAVE_A_BEAM_SYSTEM =
		0xE3, ///< strings.txt line 1460: \03Your craft does not have a [beam] system
	IFMSG_228_YOUR_CRAFT_DOES_NOT_HAVE_S_FOILS =
		0xE4, ///< strings.txt line 1461: \03Your craft does not have [S-Foils]
	IFMSG_229_YOUR_CRAFT_DOES_NOT_HAVE_A_SLAM_SYSTEM =
		0xE5, ///< strings.txt line 1462: \03Your craft does not have a [SLAM] system
	IFMSG_230_NO_REINFORCEMENTS_AVAILABLE = 0xE6, ///< strings.txt line 1464: \02No reinforcements available
	IFMSG_231_REQUEST_FOR_REINFORCEMENTS_ACKNOWLEDGED =
		0xE7, ///< strings.txt line 1465: \07Request for reinforcements acknowledged
	IFMSG_232_REINFORCEMENTS_ALREADY_SENT_NO_MORE_AVAILABLE =
		0xE8, ///< strings.txt line 1466: \02Reinforcements already sent, no more available
	IFMSG_233_HIT_SPACE_TO_CONFIRM_REINFORCEMENT_REQUEST =
		0xE9, ///< strings.txt line 1467: \04Hit [Space] to confirm reinforcement request
	IFMSG_234_ARG_HAS_DOCKED_WITH_ARG = 0xEA, ///< strings.txt line 1469: \02[*] has docked with [*]
	IFMSG_235_ARG_ARG_ENTERING_AREA_AT_ARG_KM =
		0xEB, ///< strings.txt line 1470: \02[* *] entering area at [&\02] km
	IFMSG_236_ARG_ARG_S_FROM_FG_ARG_ENTERING_AREA_AT_ARG_KM =
		0xEC, ///< strings.txt line 1471: \02[&\01 *s] from FG [*] entering area at [&\02] km
	IFMSG_237_TRACTOR_BEAM_SYSTEM_ACTIVATED =
		0xED, ///< strings.txt line 1473: \03[Tractor beam] system activated
	IFMSG_238_JAMMING_BEAM_SYSTEM_ACTIVATED =
		0xEE, ///< strings.txt line 1474: \03[Jamming beam] system activated
	IFMSG_239_DECOY_BEAM_SYSTEM_ACTIVATED = 0xEF, ///< strings.txt line 1475: \03[Decoy beam] system activated
	IFMSG_240_ENERGY_TRANSFER_BEAM_SYSTEM_ACTIVATED =
		0xF0, ///< strings.txt line 1476: \03[Energy transfer beam] system activated
	IFMSG_241_FUTURE1_BEAM_SYSTEM_ACTIVATED =
		0xF1, ///< strings.txt line 1477: \03[-future1- beam] system activated
	IFMSG_242_FUTURE2_BEAM_SYSTEM_ACTIVATED =
		0xF2, ///< strings.txt line 1478: \03[-future2- beam] system activated
	IFMSG_243_TRACTOR_BEAM_SYSTEM_DE_ACTIVATED =
		0xF3, ///< strings.txt line 1480: \03[Tractor beam] system de-activated
	IFMSG_244_JAMMING_BEAM_SYSTEM_DE_ACTIVATED =
		0xF4, ///< strings.txt line 1481: \03[Jamming beam] system de-activated
	IFMSG_245_DECOY_BEAM_SYSTEM_DE_ACTIVATED =
		0xF5, ///< strings.txt line 1482: \03[Decoy beam] system de-activated
	IFMSG_246_ENERGY_TRANSFER_BEAM_SYSTEM_DE_ACTIVATED =
		0xF6, ///< strings.txt line 1483: \03[Energy transfer beam] system de-activated
	IFMSG_247_FUTURE1_BEAM_SYSTEM_DE_ACTIVATED =
		0xF7, ///< strings.txt line 1484: \03[-future1- beam] system de-activated
	IFMSG_248_FUTURE2_BEAM_SYSTEM_DE_ACTIVATED =
		0xF8, ///< strings.txt line 1485: \03[-future2- beam] system de-activated
	IFMSG_249_TRACTOR_BEAM_SYSTEM_ACTIVATED_SELECT_TARGET =
		0xF9, ///< strings.txt line 1487: \03[Tractor beam] system activated.  Select target
	IFMSG_250_JAMMING_BEAM_SYSTEM_ACTIVATED_SELECT_TARGET =
		0xFA, ///< strings.txt line 1488: \03[Jamming beam] system activated.  Select target
	IFMSG_251_DECOY_BEAM_SYSTEM_ACTIVATED_SELECT_TARGET =
		0xFB, ///< strings.txt line 1489: \03[Decoy beam] system activated.  Select target
	IFMSG_252_ENERGY_TRANSFER_BEAM_SYSTEM_ACTIVATED_SELECT_TARGET =
		0xFC, ///< strings.txt line 1490: \03[Energy transfer beam] system activated.  Select target
	IFMSG_253_FUTURE1_BEAM_SYSTEM_ACTIVATED_SELECT_TARGET =
		0xFD, ///< strings.txt line 1491: \03[-future1- beam] system activated.  Select target
	IFMSG_254_FUTURE2_BEAM_SYSTEM_ACTIVATED_SELECT_TARGET =
		0xFE, ///< strings.txt line 1492: \03[-future2- beam] system activated.  Select target
	IFMSG_255_NO_ENERGY_FOR_BEAM_TO_ACTIVATE =
		0xFF, ///< strings.txt line 1494: \03No energy for beam to activate
	IFMSG_256_BEAM_DISRUPTED_BY_TARGET_S_COUNTERMEASURES =
		0x100, ///< strings.txt line 1495: \03Beam disrupted by target's countermeasures
	IFMSG_257_TARGET_ACQUISITION_BLOCKED_BY_DECOY_BEAM =
		0x101, ///< strings.txt line 1496: \03Target acquisition blocked by decoy beam
	IFMSG_258_RESUPPLIES_ARE_ON_THE_WAY = 0x102, ///< strings.txt line 1498: resupplies are on the way
	IFMSG_259_FLY_CLOSE_TO_RELOAD_CRAFT_AND_SET_YOUR_THROTTLE_TO_0 =
		0x103, ///< strings.txt line 1499: \02Fly close to reload craft and set your throttle to 0
	IFMSG_260_SET_YOUR_THROTTLE_TO_0_SO_RELOAD_CRAFT_CAN_DOCK_WITH_YOU =
		0x104, ///< strings.txt line 1500: \02Set your throttle to 0 so reload craft can dock with you!
	IFMSG_261_ABORTING_RELOAD_OPERATION = 0x105, ///< strings.txt line 1501: \02Aborting reload operation
	IFMSG_262_TIME_ACCELERATED_TO_ARG_X_NORMAL_TIME =
		0x106,                          ///< strings.txt line 1503: \06Time accelerated to [&\02]X normal time
	IFMSG_263_NORMAL_TIME_SET = 0x107,  ///< strings.txt line 1504: \06Normal time set
	IFMSG_264_OBJECT_TARGETED = 0x108,  ///< strings.txt line 1506: \07Object targeted
	IFMSG_265_OBJECT_DESTROYED = 0x109, ///< strings.txt line 1507: \07Object destroyed
	IFMSG_266_OBJECT_IGNORED = 0x10A,   ///< strings.txt line 1510: \07Object ignored
	IFMSG_267_WAITING_THROTTLE_SET_TO_NO_POWER =
		0x10B, ///< strings.txt line 1511: \07Waiting, throttle set to [no] power
	IFMSG_268_RESUMING_THROTTLE_SET_TO_FULL_POWER =
		0x10C, ///< strings.txt line 1512: \07Resuming, throttle set to [full] power
	IFMSG_269_MESSAGE_ACKNOWLEDGED_FLIGHT_GROUP_ARG_ARG =
		0x10D, ///< strings.txt line 1515: \02Message acknowledged: [Flight Group *] *
	IFMSG_270_FROM_ARG_ATTACK_MY_TARGET_HIT_SPACE_TO_TARGET =
		0x10E, ///< strings.txt line 1516: \04From [*].  Attack my target!  Hit [Space] to target
	IFMSG_271_FROM_ARG_IGNORE_MY_TARGET_HIT_SPACE_TO_IGNORE =
		0x10F, ///< strings.txt line 1519: \04From [*].  Ignore my target!  Hit [Space] to ignore
	IFMSG_272_FROM_ARG_HEAD_HOME_HIT_SPACE_TO_COMPLY =
		0x110, ///< strings.txt line 1520: \04From [*].  Head home!  Hit [Space] to comply
	IFMSG_273_FROM_ARG_WAIT_FOR_ORDERS_HIT_SPACE_TO_WAIT_FOR_ORDERS =
		0x111, ///< strings.txt line 1521: \04From [*].  Wait for orders!  Hit [Space] to wait for orders
	IFMSG_274_FROM_ARG_GO_AHEAD_HIT_SPACE_TO_PROCEED_WITH_MISSION =
		0x112, ///< strings.txt line 1522: \04From [*].  Go ahead!  Hit [Space] to proceed with mission
	IFMSG_275_FROM_ARG_EVADE_HIT_SPACE_TO_TARGET_ATTACKER =
		0x113, ///< strings.txt line 1523: \04From [*].  Evade!  Hit [Space] to target attacker
	IFMSG_276_FROM_ARG_REPORT_IN = 0x114, ///< strings.txt line 1524: \07From [*].  Report in!
	IFMSG_277_FROM_ARG_COVER_ME_HIT_SPACE_TO_TARGET_ATTACKER =
		0x115, ///< strings.txt line 1526: \04From [*].  Cover me!  Hit [Space] to target attacker
	IFMSG_278_FROM_ARG_COVER_ME_HIT_SPACE_TO_TARGET_ME =
		0x116, ///< strings.txt line 1527: \04From [*].  Cover me!  Hit [Space] to target me
	IFMSG_279_MATCHING_SPEED_WITH_TARGET = 0x117, ///< strings.txt line 1528: \03Matching speed with target
	IFMSG_280_TRYING_TO_MATCH_SPEED_WITH_TARGET_THROTTLE_SET_TO_FULL =
		0x118, ///< strings.txt line 1529: \03Trying to match speed with target, throttle set to full
	IFMSG_281_YOU_HAVE_DESTROYED_A_CRAFT_ON_YOUR_OWN_SIDE =
		0x119, ///< strings.txt line 1530: \02You have destroyed a craft on your own side
	IFMSG_282_MISSILE_DESTROYED = 0x11A, ///< strings.txt line 1531: \04Missile destroyed
	IFMSG_283_WARHEAD_IMPACT_DRAINED_ALL_CANNON_ENERGY =
		0x11B, ///< strings.txt line 1532: \03Warhead impact drained all cannon energy!
	IFMSG_284_ENGINE_OVERDRIVE_BOOSTERS_ENGAGED =
		0x11C, ///< strings.txt line 1534: \03Engine overdrive boosters [engaged]
	IFMSG_285_ENGINE_OVERDRIVE_BOOSTERS_DISENGAGED =
		0x11D, ///< strings.txt line 1535: \03Engine overdrive boosters [disengaged]
	IFMSG_286_ENGINE_OVERDRIVE_BOOSTERS_CANNOT_BE_ENGAGED =
		0x11E, ///< strings.txt line 1536: \03Engine overdrive boosters cannot be engaged
	IFMSG_287_BRIGHTNESS_SET_TO_LEVEL_ARG =
		0x11F, ///< strings.txt line 1538: \06Brightness set to level [&\01]
	IFMSG_288_GO_TO_CONFIG_SCREEN_TO_SET_BRIGHTNESS_IN_16_BIT_COLOR =
		0x120, ///< strings.txt line 1539: \06Go to config screen to set brightness in 16 bit color
	IFMSG_289_YOU_ARE_NOW_PILOTING_ARG_ARG_ARG =
		0x121, ///< strings.txt line 1541: \03You are now piloting [* * &\02]
	IFMSG_290_PREVIOUS_CRAFT_DESTROYED_NOW_PILOTING_ARG_ARG_ARG =
		0x122, ///< strings.txt line 1542: \03Previous craft destroyed, now piloting [* * &\02]
	IFMSG_291_PREVIOUS_CRAFT_HYPERSPACED_NOW_PILOTING_ARG_ARG_ARG =
		0x123, ///< strings.txt line 1543: \03Previous craft hyperspaced, now piloting [* * &\02]
	IFMSG_292_PREVIOUS_CRAFT_ENTERED_HANGAR_NOW_PILOTING_ARG_ARG_ARG =
		0x124, ///< strings.txt line 1544: \03Previous craft entered hangar, now piloting [* * &\02]
	IFMSG_293_NO_OTHER_CRAFT_TO_PILOT = 0x125, ///< strings.txt line 1545: \03No other craft to pilot
	IFMSG_294_CRAFT_JUMPING_NOT_ENABLED_FOR_THIS_MISSION =
		0x126,                 ///< strings.txt line 1546: \03Craft jumping not enabled for this mission
	IFMSG_295_FIRST = 0x127,   ///< strings.txt line 1548: first
	IFMSG_296_SECOND = 0x128,  ///< strings.txt line 1549: second
	IFMSG_297_THIRD = 0x129,   ///< strings.txt line 1550: third
	IFMSG_298_FOURTH = 0x12A,  ///< strings.txt line 1551: fourth
	IFMSG_299_FIFTH = 0x12B,   ///< strings.txt line 1552: fifth
	IFMSG_300_SIXTH = 0x12C,   ///< strings.txt line 1553: sixth
	IFMSG_301_SEVENTH = 0x12D, ///< strings.txt line 1554: seventh
	IFMSG_302_EIGHTH = 0x12E,  ///< strings.txt line 1555: eighth
	IFMSG_303_NINETH = 0x12F,  ///< strings.txt line 1556: nineth
	IFMSG_304_TENTH = 0x130,   ///< strings.txt line 1557: tenth
	IFMSG_305_YOU_ARE_THE_ARG_TO_COMPLETE_YOUR_MISSION_GOALS =
		0x131, ///< strings.txt line 1558: \01You are the [*] to complete your mission goals
	IFMSG_306_ARG_IS_THE_ARG_TO_COMPLETE_HIS_MISSION_GOALS =
		0x132, ///< strings.txt line 1559: \01[*] is the [*] to complete his mission goals
	IFMSG_307_TEAM_ARG_IS_THE_ARG_TO_COMPLETE_THEIR_MISSION_GOALS =
		0x133, ///< strings.txt line 1560: \01Team [*] is the [*] to complete their mission goals
	IFMSG_308_YOU_ARE_THE_ARG_TO_INSPECT_THIS_CRAFT =
		0x134,                            ///< strings.txt line 1561: \01You are the [*] to inspect this craft
	IFMSG_309_TARGET_DESCRIPTION = 0x135, ///< strings.txt line 1563: \10[*]: **.*
	IFMSG_310_YOUR_COMMAND_SHIP = 0x136,  ///< strings.txt line 1564: Your command ship
	IFMSG_311_BASE = 0x137,               ///< strings.txt line 1565: base
	IFMSG_312_STATION = 0x138,            ///< strings.txt line 1566: station
	IFMSG_313_MISSION_CRITICAL_CRAFT = 0x139,    ///< strings.txt line 1567: mission critical craft
	IFMSG_314_CONVOY_CRAFT = 0x13A,              ///< strings.txt line 1568: convoy craft
	IFMSG_315_STRIKE_CRAFT = 0x13B,              ///< strings.txt line 1569: strike craft
	IFMSG_316_REARMING_CRAFT = 0x13C,            ///< strings.txt line 1570: rearming craft
	IFMSG_317_PRIMARY_TARGET = 0x13D,            ///< strings.txt line 1571: Primary target
	IFMSG_318_SECONDARY_TARGET = 0x13E,          ///< strings.txt line 1572: Secondary target
	IFMSG_319_TERTIARY_TARGET = 0x13F,           ///< strings.txt line 1573: Tertiary target
	IFMSG_320_RES_CENTER = 0x140,                ///< strings.txt line 1574: res center
	IFMSG_321_FACILITY = 0x141,                  ///< strings.txt line 1575: facility
	IFMSG_322_FRIENDLY_CRAFT = 0x142,            ///< strings.txt line 1576: Friendly craft
	IFMSG_323_YOUR_WINGMAN = 0x143,              ///< strings.txt line 1577: Your wingman
	IFMSG_324_CRAFT = 0x144,                     ///< strings.txt line 1578: craft
	IFMSG_325_CARGO = 0x145,                     ///< strings.txt line 1579: cargo
	IFMSG_326_MINE = 0x146,                      ///< strings.txt line 1580: mine
	IFMSG_327_SATELLITE = 0x147,                 ///< strings.txt line 1581: satellite
	IFMSG_328_PROBE = 0x148,                     ///< strings.txt line 1582: probe
	IFMSG_329_NAV_BUOY = 0x149,                  ///< strings.txt line 1583: nav buoy
	IFMSG_330_WARHEAD = 0x14A,                   ///< strings.txt line 1584: warhead
	IFMSG_331_BLANK = 0x14B,                     ///< strings.txt line 1585:
	IFMSG_332_ENEMY = 0x14C,                     ///< strings.txt line 1586: Enemy
	IFMSG_333_FRIENDLY = 0x14D,                  ///< strings.txt line 1587: Friendly
	IFMSG_334_OUR = 0x14E,                       ///< strings.txt line 1588: Our
	IFMSG_335_AVOID_OR_DESTROY_IT = 0x14F,       ///< strings.txt line 1590:   Avoid or destroy it!
	IFMSG_336_STOP_ITS_ATTACK = 0x150,           ///< strings.txt line 1591:   Stop its attack!
	IFMSG_337_OTHERS_WILL_DESTROY_IT = 0x151,    ///< strings.txt line 1592:   Others will destroy it
	IFMSG_338_DESTROY_IT = 0x152,                ///< strings.txt line 1593:   [Destroy] it!
	IFMSG_339_OTHERS_WILL_ATTACK_IT = 0x153,     ///< strings.txt line 1594:   Others will attack it
	IFMSG_340_ATTACK_IT = 0x154,                 ///< strings.txt line 1595:   [Attack it]!
	IFMSG_341_TO_BE_CAPTURED = 0x155,            ///< strings.txt line 1596:   To be captured
	IFMSG_342_TO_BE_CAPTURED_DISABLE_IT = 0x156, ///< strings.txt line 1597:   To be captured. [Disable it]!
	IFMSG_343_INSPECT_IT = 0x157,                ///< strings.txt line 1598:   [Inspect it]!
	IFMSG_344_INSPECT_IT = 0x158,                ///< strings.txt line 1599:   [Inspect] it!
	IFMSG_345_TO_BE_BOARDED = 0x159,             ///< strings.txt line 1600:   To be boarded
	IFMSG_346_TO_BE_BOARDED_DISABLE_IT = 0x15A,  ///< strings.txt line 1601:   To be boarded. [Disable it]!
	IFMSG_347_OTHERS_WILL_DISABLE_IT = 0x15B,    ///< strings.txt line 1602:   Others will disable it
	IFMSG_348_DISABLE_IT = 0x15C,                ///< strings.txt line 1603:   [Disable it]!
	IFMSG_349_MUST_COMPLETE_MISSION = 0x15D,     ///< strings.txt line 1604:   Must complete mission.
	IFMSG_350_PROTECT_IT = 0x15E,                ///< strings.txt line 1605:   [Protect it]!
	IFMSG_351_MUST_DROP_OFF_ITS_CARGO = 0x15F,   ///< strings.txt line 1606:   Must drop off its cargo.
	IFMSG_352_PROTECT_IT = 0x160,                ///< strings.txt line 1607:   [Protect it]!
	IFMSG_353_TO_BE_RECOVERED = 0x161,           ///< strings.txt line 1608:   To be recovered.
	IFMSG_354_TO_BE_RECOVERED_DISABLE_IT =
		0x162, ///< strings.txt line 1609:   To be recovered.  [Disable it]!
	IFMSG_355_DESTRUCTION_ASSIGNMENT_COMPLETED =
		0x163, ///< strings.txt line 1611: \10[Destruction] assignment completed
	IFMSG_356_DISABLING_ASSIGNMENT_COMPLETED =
		0x164, ///< strings.txt line 1612: \10[Disabling] assignment completed
	IFMSG_357_INSPECTION_ASSIGNMENT_COMPLETED =
		0x165, ///< strings.txt line 1613: \10[Inspection] assignment completed
	IFMSG_358_INSPECTION_COMPLETED_SPECIAL_CARGO_FOUND =
		0x166, ///< strings.txt line 1614: \10[Inspection] completed.  Special cargo found!
	IFMSG_359_NO_WARHEADS_TARGETING_YOU = 0x167, ///< strings.txt line 1616: \03No warheads targeting you
	IFMSG_360_TRANSFERRING_ALL_LASER_ENERGY_TO_SHIELDS =
		0x168, ///< strings.txt line 1617: \03Transferring all laser energy to shields
	IFMSG_361_WEAPON_FIRING_JAMMED_BY_BEAM_SYSTEM =
		0x169, ///< strings.txt line 1618: \03Weapon firing jammed by beam system
	IFMSG_362_NO_COUNTERMEASURES_LOADED = 0x16A, ///< strings.txt line 1620: \03No countermeasures loaded
	IFMSG_363_CHAFF_MAGAZINE_EMPTY = 0x16B,      ///< strings.txt line 1621: \03[Chaff] magazine empty
	IFMSG_364_FLARE_MAGAZINE_EMPTY = 0x16C,      ///< strings.txt line 1622: \03[Flare] magazine empty
	IFMSG_365_CLUSTER_MINE_MAGAZINE_EMPTY =
		0x16D,                               ///< strings.txt line 1623: \03[Cluster mine] magazine empty
	IFMSG_366_FUTURE_MAGAZINE_EMPTY = 0x16E, ///< strings.txt line 1624: \03[- future-] magazine empty
	IFMSG_367_CHAFF_BURST_TRIGGERED = 0x16F, ///< strings.txt line 1625: \03Chaff burst triggered
	IFMSG_368_CHAFF_BURST_EXPENDED = 0x170,  ///< strings.txt line 1626: \03Chaff burst expended
	IFMSG_369_WARHEAD_SCATTERED_BY_CHAFF_NO_DAMAGE =
		0x171,                          ///< strings.txt line 1627: \03Warhead scattered by chaff.  No damage
	IFMSG_370_FLARE_FIRED = 0x172,      ///< strings.txt line 1628: \03Flare fired
	IFMSG_371_FLARE_SUCCESSFUL = 0x173, ///< strings.txt line 1629: \03Flare successful
	IFMSG_372_FLARE_MISSED = 0x174,     ///< strings.txt line 1630: \03Flare missed
	IFMSG_373_CLUSTER_MINES_RELEASED = 0x175,   ///< strings.txt line 1631: \03Cluster mines released
	IFMSG_374_FROM_ARG_ARG = 0x176,             ///< strings.txt line 1633: \02[From *:] *
	IFMSG_375_TEAM_MESSAGE_ARG = 0x177,         ///< strings.txt line 1634: \10[Team Message:] *
	IFMSG_376_ENEMY_MESSAGE_ARG = 0x178,        ///< strings.txt line 1635: \10[Enemy Message:] *
	IFMSG_377_MESSAGE_TO_ALL_ARG = 0x179,       ///< strings.txt line 1636: \10[Message to all:] *
	IFMSG_378_MESSAGE_SENT = 0x17A,             ///< strings.txt line 1637: \10Message sent
	IFMSG_379_MESSAGE_ABORTED = 0x17B,          ///< strings.txt line 1638: \10Message aborted
	IFMSG_380_ARG_HAS_QUIT_THE_MISSION = 0x17C, ///< strings.txt line 1640: \02* has quit the mission
	IFMSG_381_ARG_HAS_NO_MORE_CRAFT_AND_IS_OUT_OF_THE_MISSION =
		0x17D, ///< strings.txt line 1641: \02* has no more craft and is out of the mission
	IFMSG_382_THERE_ARE_ARG_PLAYERS_LEFT_INCLUDING_YOURSELF =
		0x17E, ///< strings.txt line 1642: \02There are &\01 players left (including yourself)
	IFMSG_383_YOU_ARE_THE_ONLY_PLAYER_LEFT =
		0x17F, ///< strings.txt line 1643: \02You are the only player left!
	IFMSG_384_YOU_MUST_WAIT_UNTIL_THE_OTHER_PLAYERS_ARE_FINISHED =
		0x180, ///< strings.txt line 1644: \02You must wait until the other players are finished
	IFMSG_385_ARG_ARG_WITHDRAWING_FROM_COMBAT_AREA =
		0x181, ///< strings.txt line 1646: \02[* *]: withdrawing from combat area
	IFMSG_386_FLIGHT_GROUP_ARG_ARG_WITHDRAWING_FROM_COMBAT_AREA =
		0x182, ///< strings.txt line 1647: \02[Flight group * *]: withdrawing from combat area
	IFMSG_387_WINGMAN_ARG_ARG_ABORTING_MISSION_ARG =
		0x183,                             ///< strings.txt line 1649: \02Wingman [* &\02] aborting mission: *
	IFMSG_388_SHIELDS_AT_50 = 0x184,       ///< strings.txt line 1650: shields at 50%
	IFMSG_389_SHIELDS_AT_25 = 0x185,       ///< strings.txt line 1651: shields at 25%
	IFMSG_390_SHIELDS_OUT = 0x186,         ///< strings.txt line 1652: shields out
	IFMSG_391_HULL_AT_75 = 0x187,          ///< strings.txt line 1653: hull at 75%
	IFMSG_392_HULL_AT_50 = 0x188,          ///< strings.txt line 1654: hull at 50%
	IFMSG_393_HULL_AT_25 = 0x189,          ///< strings.txt line 1655: hull at 25%
	IFMSG_394_WARHEADS_OUT = 0x18A,        ///< strings.txt line 1656: warheads out
	IFMSG_395_UNDER_ATTACK = 0x18B,        ///< strings.txt line 1657: under attack
	IFMSG_396_HAS_BEEN_IDENTIFIED = 0x18C, ///< strings.txt line 1658: has been identified
	IFMSG_397_ALL_SYSTEMS_OPERATIONAL = 0x18D, ///< strings.txt line 1660: \03All Systems Operational
	IFMSG_398_YOU_WERE_KILLED_BY_ARG = 0x18E,  ///< strings.txt line 1661: \10You were killed by [*]
	IFMSG_399_YOU_WERE_KILLED_BY_ARG_MOST_DAMAGE_WAS_DONE_BY_ARG =
		0x18F, ///< strings.txt line 1662: \10You were killed by [*], most damage was done by [*]
	IFMSG_400_SYSTEM_MESSAGE_DISPLAYING_TURNED_OFF =
		0x190, ///< strings.txt line 1665: \03System message displaying turned [OFF]
	IFMSG_401_SYSTEM_MESSAGE_DISPLAYING_TURNED_ON =
		0x191, ///< strings.txt line 1666: \03System message displaying turned [ON]
	IFMSG_402_RADIO_MESSAGE_BACKUP_TURNED_OFF =
		0x192, ///< strings.txt line 1667: \10Radio message backup turned [OFF]
	IFMSG_403_RADIO_MESSAGE_BACKUP_TURNED_ON =
		0x193,                             ///< strings.txt line 1668: \10Radio message backup turned [ON]
	IFMSG_404_TEAM_CHANNEL = 0x194,        ///< strings.txt line 1670: Team channel
	IFMSG_405_TARGET_CHANNEL = 0x195,      ///< strings.txt line 1671: Target channel
	IFMSG_406_ALL_PLAYERS_CHANNEL = 0x196, ///< strings.txt line 1672: All players channel
	IFMSG_407_ENEMY_CHANNEL = 0x197,       ///< strings.txt line 1673: Enemy Channel
	IFMSG_408_CHANGING_TO_ARG = 0x198,     ///< strings.txt line 1674: \03Changing to [*]
	IFMSG_409_OPENED_ARG = 0x199,          ///< strings.txt line 1675: \07Opened [*]
	IFMSG_410_CLOSED_ARG = 0x19A,          ///< strings.txt line 1676: \07Closed [*]
	IFMSG_411_VOICE_RECORDING_LIMIT_REACHED =
		0x19B, ///< strings.txt line 1677: \07Voice recording limit reached
	IFMSG_412_YOUR_TARGET_IS_NOT_A_PLAYER = 0x19C, ///< strings.txt line 1678: \07Your target is not a player
	IFMSG_413_VOICE_CHAT_IS_MULTIPLAYER_ONLY =
		0x19D, ///< strings.txt line 1679: \07Voice chat is multiplayer only
	IFMSG_414_UNABLE_TO_OPEN_RECORDING_DEVICE =
		0x19E,                                 ///< strings.txt line 1680: \07Unable to open recording device
	IFMSG_415_VOICE_CHANNEL_BUSY = 0x19F,      ///< strings.txt line 1681: \07Voice channel busy...
	IFMSG_416_VOICE_CHANNEL_AVAILABLE = 0x1A0, ///< strings.txt line 1682: \07Voice channel available
};

void msg_writeMessageLogFile(void);
void msg_emitInFlightMessage(InFlightMessageId messageId, int playerIdx);
void msg_reportfgcreation(uint16_t flightGroupIndex, uint16_t modelIndex);
void msg_addMessagePtr(uint16_t slot, const void* value);
void msg_emitCraftMessage(uint16_t objIdx, CraftData* craft, int16_t msgTemplateId);
void msg_radioMessage(uint16_t senderObjIdx, uint8_t* craftDescriptor, uint16_t commandId,
					  uint16_t responseIndex, int16_t multipleRecipients);
void msg_reportmessage(uint16_t objIdx, CraftData* craft, int16_t msgTemplateId);
int msg_BuildTargetDescription(uint16_t targetObjIdx, int playerIdx, int emitHudMessage,
							   int returnActionableOnly);
void msg_formatObjectName(uint16_t objIdx, uint16_t nameMode, char* outName);
void msg_AppendString(const char* source, char* destination);
void msg_AppendChar(char ch, char* destination);
void msg_emitLocalPlayerCraftMessage(InFlightMessageId messageId);

#ifdef __cplusplus
}
#endif

#endif
