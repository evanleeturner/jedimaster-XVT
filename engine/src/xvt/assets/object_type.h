#ifndef XVT_ASSETS_OBJECT_TYPE_H
#define XVT_ASSETS_OBJECT_TYPE_H

#include "xvt/assets/object_genus.h"
#include "xvt/assets/opt_model.h"
#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Mission/frontend craft-type (species) identifier. Convert through g_craftTypeToObjectType before indexing
 * g_modelTypeTable; this is distinct from ModelIndex. */
/* Stored as uint8_t in the binary (IDB enum CraftSpecies). */
typedef uint8_t CraftSpecies;

enum {
	CRAFT_SPECIES_UNKNOWN = 0x0,
	CRAFT_SPECIES_X_WING = 0x1,
	CRAFT_SPECIES_Y_WING = 0x2,
	CRAFT_SPECIES_A_WING = 0x3,
	CRAFT_SPECIES_B_WING = 0x4,
	CRAFT_SPECIES_TIE_FIGHTER = 0x5,
	CRAFT_SPECIES_TIE_INTERCEPTOR = 0x6,
	CRAFT_SPECIES_TIE_BOMBER = 0x7,
	CRAFT_SPECIES_TIE_ADVANCED = 0x8,
	CRAFT_SPECIES_TIE_DEFENDER = 0x9,
	CRAFT_SPECIES_TIE_NEW_1 = 0xA,
	CRAFT_SPECIES_TIE_NEW_2 = 0xB,
	CRAFT_SPECIES_MISSILE_BOAT = 0xC,
	CRAFT_SPECIES_T_WING = 0xD,
	CRAFT_SPECIES_Z_95_HEADHUNTER = 0xE,
	CRAFT_SPECIES_R_41_STARCHASER = 0xF,
	CRAFT_SPECIES_ASSAULT_GUNBOAT = 0x10,
	CRAFT_SPECIES_SHUTTLE = 0x11,
	CRAFT_SPECIES_ESCORT_SHUTTLE = 0x12,
	CRAFT_SPECIES_SYSTEM_PATROL_CRAFT = 0x13,
	CRAFT_SPECIES_SCOUT_CRAFT = 0x14,
	CRAFT_SPECIES_STORMTROOPER_TRANSPORT = 0x15,
	CRAFT_SPECIES_ASSAULT_TRANSPORT = 0x16,
	CRAFT_SPECIES_ESCORT_TRANSPORT = 0x17,
	CRAFT_SPECIES_UTILITY_TUG = 0x18,
	CRAFT_SPECIES_COMBAT_UTILITY_VEHICLE = 0x19,
	CRAFT_SPECIES_CONTAINER_CLASS_A = 0x1A,
	CRAFT_SPECIES_CONTAINER_CLASS_B = 0x1B,
	CRAFT_SPECIES_CONTAINER_CLASS_C = 0x1C,
	CRAFT_SPECIES_CONTAINER_CLASS_D = 0x1D,
	CRAFT_SPECIES_HEAVY_LIFTER = 0x1E,
	CRAFT_SPECIES_BULK_BARGE_2 = 0x1F,
	CRAFT_SPECIES_BULK_FREIGHTER = 0x20,
	CRAFT_SPECIES_CARGO_FERRY = 0x21,
	CRAFT_SPECIES_MODULAR_CONVEYOR = 0x22,
	CRAFT_SPECIES_CONTAINER_TRANSPORT = 0x23,
	CRAFT_SPECIES_HEAVY_TRANSPORT = 0x24,
	CRAFT_SPECIES_MUURIAN_TRANSPORT = 0x25,
	CRAFT_SPECIES_CORELLIAN_TRANSPORT = 0x26,
	CRAFT_SPECIES_MILLENNIUM_FALCON = 0x27,
	CRAFT_SPECIES_CORELLIAN_CORVETTE = 0x28,
	CRAFT_SPECIES_MODIFIED_CORVETTE = 0x29,
	CRAFT_SPECIES_NEBULON_B_FRIGATE = 0x2A,
	CRAFT_SPECIES_MODIFIED_FRIGATE = 0x2B,
	CRAFT_SPECIES_PASSENGER_LINER = 0x2C,
	CRAFT_SPECIES_CARRACK_CRUISER = 0x2D,
	CRAFT_SPECIES_STRIKE_CRUISER = 0x2E,
	CRAFT_SPECIES_ESCORT_CARRIER = 0x2F,
	CRAFT_SPECIES_DREADNAUGHT = 0x30,
	CRAFT_SPECIES_CALAMARI_CRUISER = 0x31,
	CRAFT_SPECIES_LIGHT_CALAMARI_CRUISER = 0x32,
	CRAFT_SPECIES_INTERDICTOR = 0x33,
	CRAFT_SPECIES_VICTORY_STAR_DESTROYER = 0x34,
	CRAFT_SPECIES_IMPERIAL_STAR_DESTROYER = 0x35,
	CRAFT_SPECIES_SUPER_STAR_DESTROYER = 0x36,
	CRAFT_SPECIES_CONTAINER_CLASS_E = 0x37,
	CRAFT_SPECIES_CONTAINER_CLASS_F = 0x38,
	CRAFT_SPECIES_CONTAINER_CLASS_G = 0x39,
	CRAFT_SPECIES_CONTAINER_CLASS_H = 0x3A,
	CRAFT_SPECIES_CONTAINER_CLASS_I = 0x3B,
	CRAFT_SPECIES_XQ1_PLATFORM = 0x3C,
	CRAFT_SPECIES_XQ2_PLATFORM = 0x3D,
	CRAFT_SPECIES_XQ3_PLATFORM = 0x3E,
	CRAFT_SPECIES_XQ4_PLATFORM = 0x3F,
	CRAFT_SPECIES_XQ5_PLATFORM = 0x40,
	CRAFT_SPECIES_XQ6_PLATFORM = 0x41,
	CRAFT_SPECIES_ASTEROID_HANGAR = 0x42,
	CRAFT_SPECIES_ASTEROID_LASER_BATTERY = 0x43,
	CRAFT_SPECIES_ASTEROID_WARHEAD_LAUNCHER = 0x44,
	CRAFT_SPECIES_X7_FACTORY = 0x45,
	CRAFT_SPECIES_COMM_SAT_1 = 0x46,
	CRAFT_SPECIES_COMM_SAT_2 = 0x47,
	CRAFT_SPECIES_SAT_3 = 0x48,
	CRAFT_SPECIES_SAT_4 = 0x49,
	CRAFT_SPECIES_SAT_5 = 0x4A,
	CRAFT_SPECIES_MINE_TYPE_A = 0x4B,
	CRAFT_SPECIES_MINE_TYPE_B = 0x4C,
	CRAFT_SPECIES_MINE_TYPE_C = 0x4D,
	CRAFT_SPECIES_GUN_EMPLACEMENT = 0x4E,
	CRAFT_SPECIES_MINE_5 = 0x4F,
	CRAFT_SPECIES_PROBE = 0x50,
	CRAFT_SPECIES_PROBE_2 = 0x51,
	CRAFT_SPECIES_PROBE_3 = 0x52,
	CRAFT_SPECIES_NAV_BUOY_TYPE_1 = 0x53,
	CRAFT_SPECIES_NAV_BUOY_TYPE_2 = 0x54,
	CRAFT_SPECIES_PILOT = 0x55,
	CRAFT_SPECIES_ASTEROID = 0x56,
	CRAFT_SPECIES_PLANET = 0x57,
	CRAFT_SPECIES_OBSTACLE = 0x58,
	CRAFT_SPECIES_COMPONENT = 0x59,
	CRAFT_SPECIES_SHIP_YARD = 0x5A,   ///< Species 90: SHIPYARD.OPT and specdesc.txt record 90; fronttxt.txt
									  ///< reverses the two yard labels.
	CRAFT_SPECIES_REPAIR_YARD = 0x5B, ///< Species 91: REPAIRYD.OPT and specdesc.txt record 91; fronttxt.txt
									  ///< reverses the two yard labels.
	CRAFT_SPECIES_MODIFIED_STRIKE_CRUISER = 0x5C,
	CRAFT_SPECIES_UNUSED_93 = 0x5D,
	CRAFT_SPECIES_UNUSED_94 = 0x5E,
	CRAFT_SPECIES_UNUSED_95 = 0x5F,
	CRAFT_SPECIES_UNUSED_96 = 0x60,
	CRAFT_SPECIES_UNUSED_97 = 0x61,
	CRAFT_SPECIES_UNUSED_98 = 0x62,
	CRAFT_SPECIES_UNUSED_99 = 0x63,
	CRAFT_SPECIES_UNUSED_100 = 0x64,
};

/* Byte family ID stored in ModelTypeInfo. Values 0..2 have localized goal labels; 3..6 are internal families
 * displayed as placeholders by the goal text table. */
/* Stored as int8_t in the binary (IDB enum CraftFamily). */
typedef int8_t CraftFamily;

enum {
	CRAFT_FAMILY_SPACE_CRAFT = 0x0,
	CRAFT_FAMILY_WEAPON = 0x1,
	CRAFT_FAMILY_SATELLITE = 0x2,
	CRAFT_FAMILY_DEBRIS = 0x3,
	CRAFT_FAMILY_BACKDROP = 0x4,
	CRAFT_FAMILY_EXPLOSION = 0x5,
	CRAFT_FAMILY_OBSTACLE = 0x6,
};

struct ModelTypeInfo {
	uint8_t recordFlags;
	uint8_t assetFlags;
	CraftFamily familyId; ///< Object family classification; values are CraftFamily.
	uint8_t
		genusId; ///< Byte storage of CraftGenus for this ObjectTypeId; distinct from mission CraftSpecies.
	int maxBoundsExtent;
	int halfBoundsExtent;
	uint16_t curTexLevel;
	int16_t* textureFrameSequence;
	uint8_t* palette;
	uint8_t flags;
	uint8_t modelIndex; ///< Byte storage of optional ModelIndex into g_modelDefs[73]; 0xFF means
						///< MODEL_INDEX_NONE.
	uint8_t textureGroup;
	uint8_t frameCount;
};

extern int16_t g_modelType127TextureFrameSequence[13];
extern int16_t g_modelType131TextureFrameSequence[7];
extern int16_t g_modelType132TextureFrameSequence[8];
extern int16_t g_modelType133TextureFrameSequence[2];
extern int16_t g_modelType134TextureFrameSequence[2];
extern int16_t g_modelType157TextureFrameSequence[11];
extern int16_t g_fuselageDamageTextureFrameSequence[25];
extern int16_t g_modelType110TextureFrameSequence[9];
extern int16_t g_modelType111TextureFrameSequence[11];
extern int16_t g_modelType112TextureFrameSequence[11];
extern int16_t g_modelType113TextureFrameSequence[9];
extern int16_t g_modelType128TextureFrameSequence[13];
extern int16_t g_modelType129TextureFrameSequence[16];
extern int16_t g_modelType130TextureFrameSequence[15];
extern uint8_t g_modelTypePaletteRemaps[17][16];
extern uint8_t* g_backdropPaletteRemapByFlightGroupStatus[17];
extern uint8_t g_modelType110Palette[16];
extern uint8_t g_modelType111Palette[16];
extern uint8_t g_modelType112Palette[16];
extern uint8_t g_modelType113Palette[16];
extern ModelTypeInfo g_modelTypeTable[201];
extern uint8_t g_craftTypeToObjectType[96];

/* Index into g_modelDefs[73] (the strings.txt 'strings for specs' name/specification table); MODEL_INDEX_NONE
 * means the object type has no ModelDef. */
/* Stored as uint16_t in the binary (IDB enum ModelIndex). */
typedef uint16_t ModelIndex;

enum {
	MODEL_000_X_WING = 0x0,                   ///< strings.txt line 1874: m:X-wing
	MODEL_001_Y_WING = 0x1,                   ///< strings.txt line 1875: m:Y-wing
	MODEL_002_A_WING = 0x2,                   ///< strings.txt line 1876: m:A-wing
	MODEL_003_B_WING = 0x3,                   ///< strings.txt line 1877: m:B-wing
	MODEL_004_TIE_FIGHTER = 0x4,              ///< strings.txt line 1878: m:TIE Fighter
	MODEL_005_TIE_INTERCEPTOR = 0x5,          ///< strings.txt line 1879: m:TIE Interceptor
	MODEL_006_TIE_BOMBER = 0x6,               ///< strings.txt line 1880: m:TIE Bomber
	MODEL_007_TIE_ADVANCED = 0x7,             ///< strings.txt line 1881: m:TIE Advanced
	MODEL_008_TIE_DEFENDER = 0x8,             ///< strings.txt line 1882: m:TIE Defender
	MODEL_009_EMPTY_NAME = 0x9,               ///< strings.txt line 1883: m:
	MODEL_010_EMPTY_NAME = 0xA,               ///< strings.txt line 1884: m:
	MODEL_011_MISSILE_BOAT = 0xB,             ///< strings.txt line 1885: m:Missile Boat
	MODEL_012_T_WING = 0xC,                   ///< strings.txt line 1886: m:T-wing
	MODEL_013_Z_95_HEADHUNTER = 0xD,          ///< strings.txt line 1887: m:Z-95 Headhunter
	MODEL_014_R_41_STARCHASER = 0xE,          ///< strings.txt line 1888: m:R-41 Starchaser
	MODEL_015_ASSAULT_GUNBOAT = 0xF,          ///< strings.txt line 1889: m:Assault Gunboat
	MODEL_016_SHUTTLE = 0x10,                 ///< strings.txt line 1890: m:Shuttle
	MODEL_017_ESCORT_SHUTTLE = 0x11,          ///< strings.txt line 1891: m:Escort Shuttle
	MODEL_018_PATROL_CRAFT = 0x12,            ///< strings.txt line 1892: m:Patrol Craft
	MODEL_019_SCOUT_CRAFT = 0x13,             ///< strings.txt line 1893: m:Scout Craft
	MODEL_020_TRANSPORT = 0x14,               ///< strings.txt line 1894: m:Transport
	MODEL_021_ASSAULT_TRANS = 0x15,           ///< strings.txt line 1895: m:Assault Trans
	MODEL_022_ESCORT_TRANS = 0x16,            ///< strings.txt line 1896: m:Escort Trans
	MODEL_023_TUG = 0x17,                     ///< strings.txt line 1897: m:Tug
	MODEL_024_COMBAT_UTILITY_VEHICLE = 0x18,  ///< strings.txt line 1898: m:Combat Utility Vehicle
	MODEL_025_CONTAINER_A = 0x19,             ///< strings.txt line 1899: m:Container A
	MODEL_026_CONTAINER_B = 0x1A,             ///< strings.txt line 1900: m:Container B
	MODEL_027_CONTAINER_C = 0x1B,             ///< strings.txt line 1901: m:Container C
	MODEL_028_CONTAINER_D = 0x1C,             ///< strings.txt line 1902: m:Container D
	MODEL_029_HEAVY_LIFTER = 0x1D,            ///< strings.txt line 1903: m:Heavy Lifter
	MODEL_030_EMPTY_NAME = 0x1E,              ///< strings.txt line 1904: m:
	MODEL_031_FREIGHTER = 0x1F,               ///< strings.txt line 1905: m:Freighter
	MODEL_032_CARGO_FERRY = 0x20,             ///< strings.txt line 1906: m:Cargo Ferry
	MODEL_033_MODULAR_CONVEYOR = 0x21,        ///< strings.txt line 1907: m:Modular Conveyor
	MODEL_034_CONTAINER_TRANS = 0x22,         ///< strings.txt line 1908: m:Container Trans
	MODEL_035_MEDIUM_TRANSPORT = 0x23,        ///< strings.txt line 1909: m:Medium Transport
	MODEL_036_MUURIAN_TRANS = 0x24,           ///< strings.txt line 1910: m:Muurian Trans
	MODEL_037_CORELLIAN_TRANS = 0x25,         ///< strings.txt line 1911: m:Corellian Trans
	MODEL_038_EMPTY_NAME = 0x26,              ///< strings.txt line 1912: m:
	MODEL_039_CORELLIAN_CORVETTE = 0x27,      ///< strings.txt line 1913: m:Corellian Corvette
	MODEL_040_MOD_CORVETTE = 0x28,            ///< strings.txt line 1914: m:Mod. Corvette
	MODEL_041_NEBULON_B_FRIGATE = 0x29,       ///< strings.txt line 1915: m:Nebulon B Frigate
	MODEL_042_NEBULON_B_2_FRIGATE = 0x2A,     ///< strings.txt line 1916: m:Nebulon B-2 Frigate
	MODEL_043_C_3_PASSENGER_LINER = 0x2B,     ///< strings.txt line 1917: m:C-3 Passenger Liner
	MODEL_044_CARRACK_CRUISER = 0x2C,         ///< strings.txt line 1918: m:Carrack Cruiser
	MODEL_045_STRIKE_CRUISER = 0x2D,          ///< strings.txt line 1919: m:Strike Cruiser
	MODEL_046_ESCORT_CARRIER = 0x2E,          ///< strings.txt line 1920: m:Escort Carrier
	MODEL_047_DREADNAUGHT = 0x2F,             ///< strings.txt line 1921: m:Dreadnaught
	MODEL_048_CALAMARI_CRUISER = 0x30,        ///< strings.txt line 1922: m:Calamari Cruiser
	MODEL_049_LT_CALAMARI_CRUISER = 0x31,     ///< strings.txt line 1923: m:Lt Calamari Cruiser
	MODEL_050_INTERDICTOR = 0x32,             ///< strings.txt line 1924: m:Interdictor
	MODEL_051_VICTORY_STAR_DESTROYER = 0x33,  ///< strings.txt line 1925: m:Victory Star Destroyer
	MODEL_052_IMPERIAL_STAR_DESTROYER = 0x34, ///< strings.txt line 1926: m:Imperial Star Destroyer
	MODEL_053_SUPERSTAR_DESTROYER = 0x35,     ///< strings.txt line 1927: m:Superstar Destroyer
	MODEL_054_CONTAINER_E = 0x36,             ///< strings.txt line 1928: m:Container E
	MODEL_055_CONTAINER_F = 0x37,             ///< strings.txt line 1929: m:Container F
	MODEL_056_CONTAINER_G = 0x38,             ///< strings.txt line 1930: m:Container G
	MODEL_057_CONTAINER_H = 0x39,             ///< strings.txt line 1931: m:Container H
	MODEL_058_CONTAINER_I = 0x3A,             ///< strings.txt line 1932: m:Container I
	MODEL_059_PLATFORM_XQ1 = 0x3B,            ///< strings.txt line 1933: m:Platform XQ1
	MODEL_060_PLATFORM_XQ2 = 0x3C,            ///< strings.txt line 1934: m:Platform XQ2
	MODEL_061_PLATFORM_XQ3 = 0x3D,            ///< strings.txt line 1935: m:Platform XQ3
	MODEL_062_PLATFORM_XQ4 = 0x3E,            ///< strings.txt line 1936: m:Platform XQ4
	MODEL_063_PLATFORM_XQ5 = 0x3F,            ///< strings.txt line 1937: m:Platform XQ5
	MODEL_064_PLATFORM_XQ6 = 0x40,            ///< strings.txt line 1938: m:Platform XQ6
	MODEL_065_R_D_FACILITY = 0x41,            ///< strings.txt line 1939: m:R+D Facility
	MODEL_066_LASER_BATTERY = 0x42,           ///< strings.txt line 1940: m:Laser Battery
	MODEL_067_WARHEAD_LAUNCHER = 0x43,        ///< strings.txt line 1941: m:Warhead Launcher
	MODEL_068_X7_FACTORY = 0x44,              ///< strings.txt line 1942: m:X7 Factory
	MODEL_069_SHIP_YARD = 0x45,               ///< strings.txt line 1943: m:Ship Yard
	MODEL_070_REPAIR_YARD = 0x46,             ///< strings.txt line 1944: m:Repair Yard
	MODEL_071_GUN_PLATFORM = 0x47,            ///< strings.txt line 1945: m:Gun Platform
	MODEL_072_MOD_STRIKE_CRUISER = 0x48,      ///< strings.txt line 1946: m:Mod. Strike Cruiser
	MODEL_INDEX_NONE = 0xFF,                  ///< XVT no-model sentinel returned by GetModelIndexFromType.
};

/* Stored as int8_t in the binary (IDB enum CraftGender). */
typedef int8_t CraftGender;

enum {
	CRAFT_GENDER_MASCULINE = 0x0,
	CRAFT_GENDER_FEMININE = 0x1,
	CRAFT_GENDER_NEUTERED = 0x2,
};

/* Runtime object/model-table index (0..200). Mission CraftSpecies values are converted through
 * g_craftTypeToObjectType before spawning. */
typedef uint16_t ObjectTypeId;

#ifdef __cplusplus
}
#endif

#endif
