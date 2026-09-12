#ifndef XVT_ASSETS_OBJECT_GENUS_H
#define XVT_ASSETS_OBJECT_GENUS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Four-byte classification enum in general/frontend structures. ModelTypeInfo stores the same values in a
 * packed one-byte field. */
/* Stored as uint32_t in the binary (IDB enum CraftGenus). */
typedef uint32_t CraftGenus;

enum {
	CRAFT_GENUS_STARFIGHTER = 0x0,
	CRAFT_GENUS_TRANSPORT = 0x1,
	CRAFT_GENUS_UTILITY_VEHICLE = 0x2,
	CRAFT_GENUS_FREIGHTER = 0x3,
	CRAFT_GENUS_STARSHIP = 0x4,
	CRAFT_GENUS_PLATFORM = 0x5,
	CRAFT_GENUS_PLAYER_PROJECTILE = 0x6,
	CRAFT_GENUS_OTHER_PROJECTILE = 0x7,
	CRAFT_GENUS_MINE = 0x8,
	CRAFT_GENUS_SATELLITE = 0x9,
	CRAFT_GENUS_NORMAL_DEBRIS = 0xA,
	CRAFT_GENUS_SMALL_DEBRIS = 0xB,
	CRAFT_GENUS_BACKDROP = 0xC,
	CRAFT_GENUS_EXPLOSION = 0xD,
	CRAFT_GENUS_OBSTACLE = 0xE,
	CRAFT_GENUS_SURFACE = 0xF,
	CRAFT_GENUS_PEOPLE = 0x10,
};

#ifdef __cplusplus
}
#endif

#endif
