#include "xvt/assets/object_type.h"

#include <stddef.h>

/* By OPT hardpoint type 0 to 31, how fe_disk_io_build_model_def groups a weapon
 * hardpoint: 1 into a laser group (types 1 to 6 and 9 to 11), 2 into a warhead
 * launcher (7, 8 and 12 to 18), 0 not at all. */
// GLOBAL: XVT 0x527640
uint8_t g_opt_hardpoint_weapon_group_kind_by_type[32] = {
	0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x02, 0x02, 0x01, 0x01,
	0x01, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

/* Object type (g_object_type_table index) of each mission craft type
 * (craft_species). Most map to the same number; 10 and 11 map to 8, 31 to 26, 39
 * to 38, 86 to 100, 88 to 236, past the 201 object types, and 89 and 93 to 95
 * to 0. */
// GLOBAL: XVT 0x524170
uint8_t g_craft_type_to_object_type[96] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x08, 0x08,
	0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
	0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1A, 0x20, 0x21, 0x22, 0x23,
	0x24, 0x25, 0x26, 0x26, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F,
	0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B,
	0x3C, 0x3D, 0x3E, 0x3F, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,
	0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F, 0x50, 0x51, 0x52, 0x53,
	0x54, 0x55, 0x64, 0x57, 0xEC, 0x00, 0x5A, 0x5B, 0x5C, 0x00, 0x00, 0x00,
};

/* Billboard frames of object type 127, read through its texture_frame_sequence.
 * An object's type_specific_byte[0] indexes it; each step of
 * flight_object_advance_texture_frame_sequence moves one entry on and acts on the
 * entry reached: 0xFFFF frees the object, 0xFFFD goes back to entry 0, 0xFFFE
 * holds, and another value of 0xFF00 or more, 0xFF00 + n, goes to entry n.
 * render_non_craft_scene_object draws an entry under 0x8000 as the model, one from
 * 0x8000 to 0xFEFF as that billboard frame, and nothing for the rest. Here:
 * 0xFFFE, then 0xFF00 back to entry 0, then frames 0xBF80 to 0xBF88 (0xBF86
 * twice), then 0xFFFF. */
// GLOBAL: XVT 0x51A030
int16_t g_object_type127_texture_frame_sequence[13] = {
	(int16_t)0xFFFE, (int16_t)0xFF00, (int16_t)0xBF80, (int16_t)0xBF81,
	(int16_t)0xBF82, (int16_t)0xBF83, (int16_t)0xBF84, (int16_t)0xBF85,
	(int16_t)0xBF86, (int16_t)0xBF86, (int16_t)0xBF87, (int16_t)0xBF88,
	(int16_t)0xFFFF,
};

/* Billboard frames of object type 131, coded as in
 * g_object_type127_texture_frame_sequence: 0xFFFE, 0xFF00, frames 0xC180 to 0xC183,
 * then 0xFFFF. */
// GLOBAL: XVT 0x51A050
int16_t g_object_type131_texture_frame_sequence[7] = {
	(int16_t)0xFFFE, (int16_t)0xFF00, (int16_t)0xC180, (int16_t)0xC181,
	(int16_t)0xC182, (int16_t)0xC183, (int16_t)0xFFFF,
};

/* Billboard frames of object type 132, coded as in
 * g_object_type127_texture_frame_sequence: 0xFFFE, 0xFF00, frames 0xC200 to 0xC204,
 * then 0xFFFF. */
// GLOBAL: XVT 0x51A060
int16_t g_object_type132_texture_frame_sequence[8] = {
	(int16_t)0xFFFE, (int16_t)0xFF00, (int16_t)0xC200, (int16_t)0xC201,
	(int16_t)0xC202, (int16_t)0xC203, (int16_t)0xC204, (int16_t)0xFFFF,
};

/* Billboard frames of object types 133 and 156, coded as in
 * g_object_type127_texture_frame_sequence: frame 0xC280, then 0xFF00 back to it. */
// GLOBAL: XVT 0x51A070
int16_t g_object_type133_texture_frame_sequence[2] = {
	(int16_t)0xC280,
	(int16_t)0xFF00,
};

/* Billboard frames of object type 134, coded as in
 * g_object_type127_texture_frame_sequence: frame 0xC300, then 0xFF00 back to it. */
// GLOBAL: XVT 0x51A074
int16_t g_object_type134_texture_frame_sequence[2] = {
	(int16_t)0xC300,
	(int16_t)0xFF00,
};

/* Billboard frames of object type 157, coded as in
 * g_object_type127_texture_frame_sequence: 0xFFFE, 0xFF00, frames 0xCE80 to 0xCE87,
 * then 0xFF02 back to the first frame. */
// GLOBAL: XVT 0x51A078
int16_t g_object_type157_texture_frame_sequence[11] = {
	(int16_t)0xFFFE, (int16_t)0xFF00, (int16_t)0xCE80, (int16_t)0xCE81,
	(int16_t)0xCE82, (int16_t)0xCE83, (int16_t)0xCE84, (int16_t)0xCE85,
	(int16_t)0xCE86, (int16_t)0xCE87, (int16_t)0xFF02,
};

/* Billboard frames of object type 136 and of a craft's fuselage damage, coded
 * as in g_object_type127_texture_frame_sequence: 0xFFFE, 0xFF00, frames 0xC380 to
 * 0xC389, then 0xC400 to 0xC405 each twice, then 0xFF0C back to the first
 * 0xC400. */
// GLOBAL: XVT 0x51A0B0
int16_t g_fuselage_damage_texture_frame_sequence[25] = {
	(int16_t)0xFFFE, (int16_t)0xFF00, (int16_t)0xC380, (int16_t)0xC381,
	(int16_t)0xC382, (int16_t)0xC383, (int16_t)0xC384, (int16_t)0xC385,
	(int16_t)0xC386, (int16_t)0xC387, (int16_t)0xC388, (int16_t)0xC389,
	(int16_t)0xC400, (int16_t)0xC400, (int16_t)0xC401, (int16_t)0xC401,
	(int16_t)0xC402, (int16_t)0xC402, (int16_t)0xC403, (int16_t)0xC403,
	(int16_t)0xC404, (int16_t)0xC404, (int16_t)0xC405, (int16_t)0xC405,
	(int16_t)0xFF0C,
};

/* Billboard frames of object type 110, coded as in
 * g_object_type127_texture_frame_sequence: 0xFFFE, 0xFF00, frames 0xB700 to 0xB705,
 * then 0xFF02 back to the first frame. */
// GLOBAL: XVT 0x51A0E8
int16_t g_object_type110_texture_frame_sequence[9] = {
	(int16_t)0xFFFE, (int16_t)0xFF00, (int16_t)0xB700,
	(int16_t)0xB701, (int16_t)0xB702, (int16_t)0xB703,
	(int16_t)0xB704, (int16_t)0xB705, (int16_t)0xFF02,
};

/* Billboard frames of object type 111, coded as in
 * g_object_type127_texture_frame_sequence: 0xFFFE, 0xFF00, frames 0xB780 to 0xB787,
 * then 0xFF02 back to the first frame. */
// GLOBAL: XVT 0x51A100
int16_t g_object_type111_texture_frame_sequence[11] = {
	(int16_t)0xFFFE, (int16_t)0xFF00, (int16_t)0xB780, (int16_t)0xB781,
	(int16_t)0xB782, (int16_t)0xB783, (int16_t)0xB784, (int16_t)0xB785,
	(int16_t)0xB786, (int16_t)0xB787, (int16_t)0xFF02,
};

/* Billboard frames of object type 112, coded as in
 * g_object_type127_texture_frame_sequence: 0xFFFE, 0xFF00, frames 0xB800 to 0xB807,
 * then 0xFF02 back to the first frame. */
// GLOBAL: XVT 0x51A118
int16_t g_object_type112_texture_frame_sequence[11] = {
	(int16_t)0xFFFE, (int16_t)0xFF00, (int16_t)0xB800, (int16_t)0xB801,
	(int16_t)0xB802, (int16_t)0xB803, (int16_t)0xB804, (int16_t)0xB805,
	(int16_t)0xB806, (int16_t)0xB807, (int16_t)0xFF02,
};

/* Billboard frames of object type 113, coded as in
 * g_object_type127_texture_frame_sequence: 0xFFFE, 0xFF00, frames 0xB880 to 0xB885,
 * then 0xFF02 back to the first frame. */
// GLOBAL: XVT 0x51A130
int16_t g_object_type113_texture_frame_sequence[9] = {
	(int16_t)0xFFFE, (int16_t)0xFF00, (int16_t)0xB880,
	(int16_t)0xB881, (int16_t)0xB882, (int16_t)0xB883,
	(int16_t)0xB884, (int16_t)0xB885, (int16_t)0xFF02,
};

/* Billboard frames of object type 128, coded as in
 * g_object_type127_texture_frame_sequence: 0xFFFE, 0xFF00, frames 0xC000 to 0xC009,
 * then 0xFFFF. */
// GLOBAL: XVT 0x51A148
int16_t g_object_type128_texture_frame_sequence[13] = {
	(int16_t)0xFFFE, (int16_t)0xFF00, (int16_t)0xC000, (int16_t)0xC001,
	(int16_t)0xC002, (int16_t)0xC003, (int16_t)0xC004, (int16_t)0xC005,
	(int16_t)0xC006, (int16_t)0xC007, (int16_t)0xC008, (int16_t)0xC009,
	(int16_t)0xFFFF,
};

/* Billboard frames of object type 129, coded as in
 * g_object_type127_texture_frame_sequence: 0xFFFE, 0xFF00, frames 0xC080 to 0xC08C,
 * then 0xFFFF. */
// GLOBAL: XVT 0x51A168
int16_t g_object_type129_texture_frame_sequence[16] = {
	(int16_t)0xFFFE, (int16_t)0xFF00, (int16_t)0xC080, (int16_t)0xC081,
	(int16_t)0xC082, (int16_t)0xC083, (int16_t)0xC084, (int16_t)0xC085,
	(int16_t)0xC086, (int16_t)0xC087, (int16_t)0xC088, (int16_t)0xC089,
	(int16_t)0xC08A, (int16_t)0xC08B, (int16_t)0xC08C, (int16_t)0xFFFF,
};

/* Billboard frames of object type 130, coded as in
 * g_object_type127_texture_frame_sequence: 0xFFFE, 0xFF00, frames 0xC100 to 0xC10B,
 * then 0xFFFF. */
// GLOBAL: XVT 0x51A188
int16_t g_object_type130_texture_frame_sequence[15] = {
	(int16_t)0xFFFE, (int16_t)0xFF00, (int16_t)0xC100, (int16_t)0xC101,
	(int16_t)0xC102, (int16_t)0xC103, (int16_t)0xC104, (int16_t)0xC105,
	(int16_t)0xC106, (int16_t)0xC107, (int16_t)0xC108, (int16_t)0xC109,
	(int16_t)0xC10A, (int16_t)0xC10B, (int16_t)0xFFFF,
};

/* Seventeen remaps of 16 palette indices, which object types and
 * g_backdrop_palette_remap_by_flight_group_status point at through
 * object_type_info.palette. The game code never reads them; the modern build's
 * capture copies them for its renderer. */
// GLOBAL: XVT 0x521D78
uint8_t g_object_type_palette_remaps[17][16] = {
	{0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF, 0xE0, 0xE1,
	 0xE2, 0xE3, 0xE4, 0xE5},
	{0x00, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xCB, 0xD6, 0xD7, 0xFF, 0xFF, 0xFF,
	 0xFF, 0xFF, 0xFF, 0xFF},
	{0x00, 0xD9, 0xDB, 0xDD, 0xDF, 0xE1, 0xE3, 0xD6, 0xD7, 0xFF, 0xFF, 0xFF,
	 0xFF, 0xFF, 0xFF, 0xFF},
	{0x00, 0xF4, 0xF6, 0x53, 0xF5, 0xB7, 0xAC, 0x4F, 0x66, 0x4D, 0x4C, 0x64,
	 0xB6, 0x5E, 0x5A, 0xB5},
	{0x00, 0xF5, 0xF6, 0xB3, 0xAF, 0xAB, 0x69, 0xB2, 0xA8, 0xF4, 0xF3, 0xE4,
	 0xAD, 0xAA, 0x00, 0x00},
	{0x00, 0xF6, 0xF5, 0xB3, 0xB2, 0xE4, 0xAD, 0xF4, 0xAB, 0x69, 0xAA, 0xA8,
	 0xAF, 0x00, 0x00, 0x00},
	{0x00, 0xF6, 0xB2, 0xF5, 0xAA, 0xF4, 0xAD, 0xA8, 0xAB, 0xB3, 0xAF, 0x00,
	 0x00, 0x00, 0x00, 0x00},
	{0x00, 0xF6, 0xF5, 0xE4, 0xAD, 0xAB, 0x40, 0xB3, 0xFF, 0xFF, 0xFF, 0xFF,
	 0xFF, 0xFF, 0xFF, 0xFF},
	{0x00, 0xF6, 0xF4, 0xB2, 0xAA, 0xAD, 0x80, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
	 0xFF, 0xFF, 0xFF, 0xFF},
	{0x00, 0xB8, 0xEE, 0x8E, 0x8C, 0x89, 0x8B, 0xF5, 0x87, 0x53, 0x8F, 0x86,
	 0x84, 0x5A, 0x00, 0x00},
	{0x00, 0xF4, 0xF3, 0x89, 0x82, 0x57, 0x54, 0x7E, 0x81, 0x87, 0x8B, 0x86,
	 0x58, 0x84, 0xEE, 0xB7},
	{0x00, 0xF6, 0xD0, 0xA6, 0xE3, 0xA4, 0xE2, 0xA5, 0xE1, 0x00, 0x00, 0x00,
	 0x00, 0x00, 0x00, 0x00},
	{0x00, 0xDE, 0xDF, 0x9E, 0xE2, 0xCF, 0xA6, 0xDB, 0xD9, 0xDA, 0xD0, 0x90,
	 0xDD, 0xD8, 0xBA, 0xB4},
	{0x00, 0xD0, 0xA7, 0xA6, 0xE3, 0xA1, 0xA0, 0x9E, 0x9D, 0xA5, 0xA4, 0x9C,
	 0x00, 0x00, 0x00, 0x00},
	{0x00, 0xED, 0xEB, 0xE9, 0xE8, 0xEC, 0xE7, 0x45, 0x44, 0xEA, 0x43, 0x42,
	 0x41, 0x4C, 0x00, 0x00},
	{0x00, 0xB9, 0xEC, 0xE9, 0xEB, 0x50, 0xE8, 0x60, 0x8F, 0x89, 0x81, 0xEE,
	 0x4C, 0xEA, 0xE7, 0xB8},
	{0x00, 0x4D, 0x5A, 0x7F, 0x5F, 0x58, 0x55, 0x5C, 0x4F, 0x52, 0xF2, 0xF1,
	 0xB8, 0xB9, 0xF0, 0xEF},
};

/* By a backdrop flight group's status1, the g_object_type_palette_remaps row
 * mission_init gives the backdrop's type as its palette: rows 9 to 16, then 13
 * to 16 twice, then 16. */
// GLOBAL: XVT 0x521E88
uint8_t *g_backdrop_palette_remap_by_flight_group_status[17] = {
	g_object_type_palette_remaps[9],  g_object_type_palette_remaps[10],
	g_object_type_palette_remaps[11], g_object_type_palette_remaps[12],
	g_object_type_palette_remaps[13], g_object_type_palette_remaps[14],
	g_object_type_palette_remaps[15], g_object_type_palette_remaps[16],
	g_object_type_palette_remaps[13], g_object_type_palette_remaps[14],
	g_object_type_palette_remaps[15], g_object_type_palette_remaps[16],
	g_object_type_palette_remaps[13], g_object_type_palette_remaps[14],
	g_object_type_palette_remaps[15], g_object_type_palette_remaps[16],
	g_object_type_palette_remaps[16],
};

/* Palette remap of object type 110; like g_object_type_palette_remaps, only the
 * modern build's capture reads it. */
// GLOBAL: XVT 0x521ED0
uint8_t g_object_type110_palette[16] = {
	0x00, 0x5C, 0x4C, 0xB5, 0x8F, 0x8C, 0xB8, 0xFB,
	0xE5, 0x63, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

/* Palette remap of object type 111; like g_object_type_palette_remaps, only the
 * modern build's capture reads it. */
// GLOBAL: XVT 0x521EE0
uint8_t g_object_type111_palette[16] = {
	0x00, 0x8E, 0x89, 0x8B, 0x88, 0xB8, 0x5A, 0x5B,
	0x64, 0x86, 0x5D, 0xFB, 0x00, 0x00, 0x00, 0x00,
};

/* Palette remap of object type 112; like g_object_type_palette_remaps, only the
 * modern build's capture reads it. */
// GLOBAL: XVT 0x521EF0
uint8_t g_object_type112_palette[16] = {
	0x00, 0x8E, 0xAE, 0x62, 0xB8, 0x77, 0x71, 0xFB,
	0x5A, 0x6A, 0x66, 0x5C, 0xB1, 0x59, 0x00, 0x00,
};

/* Palette remap of object type 113; like g_object_type_palette_remaps, only the
 * modern build's capture reads it. */
// GLOBAL: XVT 0x521F00
uint8_t g_object_type113_palette[16] = {
	0x00, 0xB8, 0x7B, 0x60, 0x5A, 0x5C, 0x5E, 0x53,
	0x8C, 0x89, 0x64, 0xFB, 0xB1, 0x00, 0x00, 0x00,
};

/* One object_type_info row per object type 0 to 200: flags, family, genus, bound
 * extents, frame sequence, palette remap, model index and resource list place.
 * fe_disk_io_load_resources fills resource_handle and fe_disk_io_free_flight_resources
 * clears it; fe_disk_io_build_model_def sets the extents of loaded models;
 * mission_init sets the required-asset bit and backdrop resource indices and
 * palettes. */
// GLOBAL: XVT 0x521F18
struct object_type_info g_object_type_table[201] = {
	/* 000 */ {0x00, 0x00, 0, 0, 0, 0, 0, NULL, NULL, 0x00,
		   MODEL_INDEX_NONE, 2, 0},
	/* 001 */ {0x03, 0x01, 0, 0, 508, 125, 0, NULL, NULL, 0x43, 0, 0, 0},
	/* 002 */ {0x03, 0x01, 0, 0, 651, 125, 0, NULL, NULL, 0x43, 1, 0, 1},
	/* 003 */ {0x03, 0x01, 0, 0, 391, 125, 0, NULL, NULL, 0x43, 2, 0, 2},
	/* 004 */ {0x03, 0x01, 0, 0, 680, 150, 0, NULL, NULL, 0x43, 3, 0, 3},
	/* 005 */ {0x03, 0x01, 0, 0, 325, 150, 0, NULL, NULL, 0x43, 4, 0, 4},
	/* 006 */ {0x03, 0x01, 0, 0, 391, 150, 0, NULL, NULL, 0x43, 5, 0, 5},
	/* 007 */ {0x03, 0x01, 0, 0, 356, 150, 0, NULL, NULL, 0x43, 6, 0, 6},
	/* 008 */ {0x03, 0x01, 0, 0, 450, 125, 0, NULL, NULL, 0x43, 7, 0, 7},
	/* 009 */ {0x03, 0x01, 0, 0, 400, 125, 0, NULL, NULL, 0x43, 8, 0, 8},
	/* 010 */ {0x00, 0x01, 0, 0, 356, 175, 0, NULL, NULL, 0x43, 9, 2, 0},
	/* 011 */ {0x00, 0x01, 0, 0, 356, 175, 0, NULL, NULL, 0x43, 10, 2, 0},
	/* 012 */ {0x03, 0x01, 0, 0, 356, 175, 0, NULL, NULL, 0x43, 11, 1, 15},
	/* 013 */ {0x03, 0x01, 0, 0, 356, 175, 0, NULL, NULL, 0x43, 12, 1, 9},
	/* 014 */ {0x03, 0x01, 0, 0, 500, 175, 0, NULL, NULL, 0x43, 13, 0, 9},
	/* 015 */ {0x03, 0x01, 0, 0, 356, 175, 0, NULL, NULL, 0x43, 14, 1, 10},
	/* 016 */ {0x03, 0x21, 0, 0, 620, 125, 0, NULL, NULL, 0x43, 15, 0, 10},
	/* 017 */ {0x03, 0x21, 0, 1, 930, 600, 0, NULL, NULL, 0x43, 16, 0, 11},
	/* 018 */ {0x03, 0x21, 0, 1, 930, 700, 0, NULL, NULL, 0x43, 17, 0, 12},
	/* 019 */ {0x03, 0x21, 0, 1, 2000, 700, 0, NULL, NULL, 0x43, 18, 0, 13},
	/* 020 */ {0x03, 0x21, 0, 1, 2000, 700, 0, NULL, NULL, 0x43, 19, 1, 13},
	/* 021 */ {0x03, 0x21, 0, 1, 600, 175, 0, NULL, NULL, 0x43, 20, 0, 14},
	/* 022 */ {0x03, 0x21, 0, 1, 800, 700, 0, NULL, NULL, 0x43, 21, 0, 15},
	/* 023 */ {0x03, 0x21, 0, 1, 930, 700, 0, NULL, NULL, 0x43, 22, 1, 7},
	/* 024 */ {0x03, 0x21, 0, 2, 210, 90, 0, NULL, NULL, 0x43, 23, 0, 16},
	/* 025 */ {0x03, 0x21, 0, 2, 186, 90, 0, NULL, NULL, 0x43, 24, 1, 14},
	/* 026 */ {0x03, 0x01, 0, 3, 3100, 800, 0, NULL, NULL, 0x43, 25, 0, 17},
	/* 027 */ {0x03, 0x01, 0, 3, 3100, 900, 0, NULL, NULL, 0x43, 26, 0, 18},
	/* 028 */ {0x03, 0x01, 0, 3, 3100, 900, 0, NULL, NULL, 0x43, 27, 0, 19},
	/* 029 */ {0x03, 0x01, 0, 3, 3100, 900, 0, NULL, NULL, 0x43, 28, 0, 20},
	/* 030 */ {0x03, 0x01, 0, 2, 3100, 900, 0, NULL, NULL, 0x43, 29, 0, 21},
	/* 031 */
	{0x00, 0x01, 0, 3, 3100, 900, 0, NULL, NULL, 0x43, MODEL_INDEX_NONE, 2,
	 0},
	/* 032 */
	{0x03, 0x21, 0, 3, 4960, 1200, 0, NULL, NULL, 0x43, 31, 0, 22},
	/* 033 */ {0x03, 0x21, 0, 3, 5000, 900, 0, NULL, NULL, 0x43, 32, 0, 23},
	/* 034 */ {0x03, 0x21, 0, 3, 7500, 900, 0, NULL, NULL, 0x43, 33, 0, 24},
	/* 035 */ {0x03, 0x21, 0, 3, 3100, 900, 0, NULL, NULL, 0x43, 34, 0, 74},
	/* 036 */
	{0x03, 0x21, 0, 3, 6200, 3100, 0, NULL, NULL, 0x43, 35, 0, 88},
	/* 037 */ {0x03, 0x21, 0, 3, 3100, 900, 0, NULL, NULL, 0x43, 36, 1, 11},
	/* 038 */ {0x03, 0x21, 0, 3, 1100, 900, 0, NULL, NULL, 0x43, 37, 0, 25},
	/* 039 */
	{0x00, 0x01, 0, 3, 1100, 900, 0, NULL, NULL, 0x43, MODEL_INDEX_NONE, 2,
	 0},
	/* 040 */
	{0x03, 0x21, 0, 3, 6200, 3100, 0, NULL, NULL, 0x43, 39, 0, 26},
	/* 041 */ {0x03, 0x21, 0, 3, 4175, 900, 0, NULL, NULL, 0x43, 40, 0, 27},
	/* 042 */
	{0x03, 0x21, 0, 4, 15000, 7500, 0, NULL, NULL, 0x43, 41, 0, 28},
	/* 043 */
	{0x03, 0x21, 0, 4, 15000, 7500, 0, NULL, NULL, 0x43, 42, 0, 73},
	/* 044 */ {0x03, 0x21, 0, 4, 3100, 900, 0, NULL, NULL, 0x43, 43, 1, 12},
	/* 045 */ {0x03, 0x21, 0, 4, 3100, 900, 0, NULL, NULL, 0x43, 44, 1, 4},
	/* 046 */ {0x03, 0x21, 0, 4, 3100, 900, 0, NULL, NULL, 0x43, 45, 1, 5},
	/* 047 */
	{0x03, 0x21, 0, 4, 10000, 900, 0, NULL, NULL, 0x43, 46, 0, 29},
	/* 048 */ {0x03, 0x21, 0, 4, 3100, 900, 0, NULL, NULL, 0x43, 47, 1, 6},
	/* 049 */
	{0x03, 0x21, 0, 4, 58000, 20000, 0, NULL, NULL, 0x43, 48, 0, 71},
	/* 050 */
	{0x03, 0x21, 0, 4, 58000, 20000, 0, NULL, NULL, 0x43, 49, 0, 72},
	/* 051 */
	{0x03, 0x21, 0, 4, 11000, 32000, 0, NULL, NULL, 0x43, 50, 0, 30},
	/* 052 */
	{0x03, 0x21, 0, 4, 64000, 32000, 0, NULL, NULL, 0x43, 51, 0, 70},
	/* 053 */
	{0x03, 0x21, 0, 4, 64000, 32000, 0, NULL, NULL, 0x43, 52, 0, 69},
	/* 054 */
	{0x03, 0x21, 0, 4, 64000, 32000, 0, NULL, NULL, 0x43, 53, 0, 87},
	/* 055 */ {0x03, 0x01, 0, 3, 3100, 900, 0, NULL, NULL, 0x43, 54, 0, 75},
	/* 056 */ {0x03, 0x01, 0, 3, 3100, 900, 0, NULL, NULL, 0x43, 55, 1, 0},
	/* 057 */ {0x03, 0x01, 0, 3, 3100, 900, 0, NULL, NULL, 0x43, 56, 1, 1},
	/* 058 */ {0x03, 0x01, 0, 3, 3100, 900, 0, NULL, NULL, 0x43, 57, 1, 2},
	/* 059 */ {0x03, 0x01, 0, 3, 3100, 900, 0, NULL, NULL, 0x43, 58, 1, 3},
	/* 060 */
	{0x03, 0x21, 0, 5, 9000, 32000, 0, NULL, NULL, 0x43, 59, 0, 77},
	/* 061 */
	{0x03, 0x21, 0, 5, 9000, 32000, 0, NULL, NULL, 0x43, 60, 0, 31},
	/* 062 */
	{0x03, 0x21, 0, 5, 9000, 32000, 0, NULL, NULL, 0x43, 61, 0, 78},
	/* 063 */
	{0x03, 0x21, 0, 5, 9000, 32000, 0, NULL, NULL, 0x43, 62, 0, 81},
	/* 064 */
	{0x03, 0x21, 0, 5, 9000, 32000, 0, NULL, NULL, 0x43, 63, 0, 82},
	/* 065 */
	{0x03, 0x21, 0, 5, 9000, 32000, 0, NULL, NULL, 0x43, 64, 0, 83},
	/* 066 */
	{0x03, 0x21, 0, 5, 9000, 32000, 0, NULL, NULL, 0x43, 65, 2, 0},
	/* 067 */
	{0x03, 0x21, 0, 5, 9000, 32000, 0, NULL, NULL, 0x43, 66, 2, 1},
	/* 068 */
	{0x03, 0x21, 0, 5, 9000, 32000, 0, NULL, NULL, 0x43, 67, 2, 2},
	/* 069 */
	{0x03, 0x21, 0, 5, 9000, 32000, 0, NULL, NULL, 0x43, 68, 1, 8},
	/* 070 */
	{0x03, 0x21, 2, 9, 300, 150, 0, NULL, NULL, 0x83, MODEL_INDEX_NONE, 0,
	 32},
	/* 071 */
	{0x03, 0x21, 2, 9, 300, 150, 0, NULL, NULL, 0x83, MODEL_INDEX_NONE, 0,
	 32},
	/* 072 */
	{0x00, 0x21, 2, 9, 300, 150, 0, NULL, NULL, 0x83, MODEL_INDEX_NONE, 2,
	 0},
	/* 073 */
	{0x00, 0x21, 2, 9, 300, 150, 0, NULL, NULL, 0x83, MODEL_INDEX_NONE, 2,
	 0},
	/* 074 */
	{0x00, 0x21, 2, 9, 300, 150, 0, NULL, NULL, 0x83, MODEL_INDEX_NONE, 2,
	 0},
	/* 075 */
	{0x03, 0x21, 1, 8, 500, 250, 0, NULL, NULL, 0x83, MODEL_INDEX_NONE, 0,
	 33},
	/* 076 */
	{0x03, 0x21, 1, 8, 500, 250, 0, NULL, NULL, 0x83, MODEL_INDEX_NONE, 0,
	 34},
	/* 077 */
	{0x03, 0x21, 1, 8, 500, 250, 0, NULL, NULL, 0x83, MODEL_INDEX_NONE, 0,
	 76},
	/* 078 */ {0x03, 0x21, 0, 5, 1000, 250, 0, NULL, NULL, 0x43, 71, 0, 91},
	/* 079 */
	{0x00, 0x00, 1, 8, 500, 250, 0, NULL, NULL, 0x83, MODEL_INDEX_NONE, 2,
	 0},
	/* 080 */
	{0x03, 0x21, 2, 9, 200, 100, 0, NULL, NULL, 0x83, MODEL_INDEX_NONE, 0,
	 35},
	/* 081 */
	{0x03, 0x21, 2, 9, 400, 100, 0, NULL, NULL, 0x83, MODEL_INDEX_NONE, 0,
	 36},
	/* 082 */
	{0x00, 0x21, 2, 9, 200, 100, 0, NULL, NULL, 0x83, MODEL_INDEX_NONE, 2,
	 0},
	/* 083 */
	{0x03, 0x01, 2, 9, 250, 125, 0, NULL, NULL, 0x83, MODEL_INDEX_NONE, 0,
	 36},
	/* 084 */
	{0x03, 0x01, 2, 9, 250, 125, 0, NULL, NULL, 0x83, MODEL_INDEX_NONE, 0,
	 36},
	/* 085 */
	{0x00, 0x00, 3, 11, 480, 240, 0, NULL, NULL, 0x83, MODEL_INDEX_NONE, 2,
	 0},
	/* 086 */
	{0x01, 0x00, 3, 10, 480, 240, 0, NULL, NULL, 0x80, MODEL_INDEX_NONE, 2,
	 0},
	/* 087 */
	{0x01, 0x00, 3, 12, 480, 240, 0, NULL, NULL, 0x20, MODEL_INDEX_NONE, 2,
	 47},
	/* 088 */
	{0x01, 0x00, 3, 14, 480, 240, 0, NULL, NULL, 0x40, MODEL_INDEX_NONE, 2,
	 0},
	/* 089 */
	{0x00, 0x00, 3, 11, 480, 240, 0, NULL, NULL, 0x40, MODEL_INDEX_NONE, 2,
	 0},
	/* 090 */
	{0x03, 0x21, 0, 5, 9000, 32000, 0, NULL, NULL, 0x43, 69, 0, 89},
	/* 091 */
	{0x03, 0x21, 0, 5, 9000, 32000, 0, NULL, NULL, 0x43, 70, 0, 90},
	/* 092 */ {0x03, 0x21, 0, 4, 3100, 900, 0, NULL, NULL, 0x43, 72, 0, 92},
	/* 093 */
	{0x00, 0x00, 3, 11, 480, 240, 0, NULL, NULL, 0x00, MODEL_INDEX_NONE, 2,
	 0},
	/* 094 */
	{0x00, 0x00, 3, 11, 480, 240, 0, NULL, NULL, 0x00, MODEL_INDEX_NONE, 2,
	 0},
	/* 095 */
	{0x00, 0x00, 3, 11, 480, 240, 0, NULL, NULL, 0x00, MODEL_INDEX_NONE, 2,
	 0},
	/* 096 */
	{0x00, 0x00, 3, 11, 480, 240, 0, NULL, NULL, 0x00, MODEL_INDEX_NONE, 2,
	 0},
	/* 097 */
	{0x00, 0x00, 3, 11, 480, 240, 0, NULL, NULL, 0x00, MODEL_INDEX_NONE, 2,
	 0},
	/* 098 */
	{0x03, 0x49, 6, 14, 480, 240, 0, NULL, NULL, 0x43, MODEL_INDEX_NONE, 0,
	 79},
	/* 099 */
	{0x03, 0x49, 6, 14, 480, 240, 0, NULL, NULL, 0x43, MODEL_INDEX_NONE, 0,
	 80},
	/* 100 */
	{0x03, 0x21, 3, 10, 6000, 3000, 0, NULL, NULL, 0x80, MODEL_INDEX_NONE,
	 0, 37},
	/* 101 */
	{0x03, 0x21, 3, 10, 6000, 3000, 0, NULL, NULL, 0x80, MODEL_INDEX_NONE,
	 0, 38},
	/* 102 */
	{0x03, 0x21, 3, 10, 6000, 3000, 0, NULL, NULL, 0x80, MODEL_INDEX_NONE,
	 0, 39},
	/* 103 */
	{0x03, 0x21, 3, 10, 6000, 3000, 0, NULL, NULL, 0x80, MODEL_INDEX_NONE,
	 0, 40},
	/* 104 */
	{0x03, 0x21, 3, 10, 6000, 3000, 0, NULL, NULL, 0x80, MODEL_INDEX_NONE,
	 0, 41},
	/* 105 */
	{0x03, 0x21, 3, 10, 6000, 3000, 0, NULL, NULL, 0x80, MODEL_INDEX_NONE,
	 0, 42},
	/* 106 */
	{0x00, 0x00, 3, 10, 480, 240, 0, NULL, NULL, 0x80, MODEL_INDEX_NONE, 2,
	 0},
	/* 107 */
	{0x00, 0x00, 3, 10, 480, 240, 0, NULL, NULL, 0x80, MODEL_INDEX_NONE, 2,
	 0},
	/* 108 */
	{0x00, 0x00, 3, 10, 480, 240, 0, NULL, NULL, 0x80, MODEL_INDEX_NONE, 2,
	 0},
	/* 109 */
	{0x00, 0x00, 3, 10, 480, 240, 0, NULL, NULL, 0x80, MODEL_INDEX_NONE, 2,
	 0},
	/* 110 */
	{0x03, 0x0A, 3, 11, 512, 256, 0,
	 g_object_type110_texture_frame_sequence, g_object_type110_palette,
	 0x80, MODEL_INDEX_NONE, 0, 43},
	/* 111 */
	{0x03, 0x0A, 3, 11, 512, 256, 0,
	 g_object_type111_texture_frame_sequence, g_object_type111_palette,
	 0x80, MODEL_INDEX_NONE, 0, 44},
	/* 112 */
	{0x03, 0x0A, 3, 11, 512, 256, 0,
	 g_object_type112_texture_frame_sequence, g_object_type112_palette,
	 0x80, MODEL_INDEX_NONE, 0, 45},
	/* 113 */
	{0x03, 0x0A, 3, 11, 512, 256, 0,
	 g_object_type113_texture_frame_sequence, g_object_type113_palette,
	 0x80, MODEL_INDEX_NONE, 0, 46},
	/* 114 */
	{0x03, 0x02, 4, 12, 480, 240, 0, NULL, g_object_type_palette_remaps[9],
	 0x20, MODEL_INDEX_NONE, 0, 47},
	/* 115 */
	{0x03, 0x02, 4, 12, 480, 240, 0, NULL, g_object_type_palette_remaps[10],
	 0x20, MODEL_INDEX_NONE, 0, 48},
	/* 116 */
	{0x03, 0x02, 4, 12, 480, 240, 0, NULL, g_object_type_palette_remaps[11],
	 0x20, MODEL_INDEX_NONE, 0, 49},
	/* 117 */
	{0x03, 0x0A, 4, 12, 480, 240, 0, NULL, g_object_type_palette_remaps[3],
	 0x20, MODEL_INDEX_NONE, 0, 55},
	/* 118 */
	{0x03, 0x0A, 4, 12, 480, 240, 0, NULL, g_object_type_palette_remaps[4],
	 0x20, MODEL_INDEX_NONE, 0, 56},
	/* 119 */
	{0x03, 0x0A, 4, 12, 480, 240, 0, NULL, g_object_type_palette_remaps[5],
	 0x20, MODEL_INDEX_NONE, 0, 57},
	/* 120 */
	{0x03, 0x0A, 4, 12, 480, 240, 0, NULL, g_object_type_palette_remaps[6],
	 0x20, MODEL_INDEX_NONE, 0, 58},
	/* 121 */
	{0x03, 0x0A, 4, 12, 480, 240, 0, NULL, g_object_type_palette_remaps[7],
	 0x20, MODEL_INDEX_NONE, 0, 55},
	/* 122 */
	{0x03, 0x0A, 4, 12, 480, 240, 0, NULL, g_object_type_palette_remaps[8],
	 0x20, MODEL_INDEX_NONE, 0, 56},
	/* 123 */
	{0x03, 0x0A, 4, 12, 480, 240, 0, NULL, g_object_type_palette_remaps[8],
	 0x20, MODEL_INDEX_NONE, 0, 57},
	/* 124 */
	{0x03, 0x0A, 4, 12, 480, 240, 0, NULL, g_object_type_palette_remaps[8],
	 0x20, MODEL_INDEX_NONE, 0, 58},
	/* 125 */
	{0x03, 0x0A, 4, 12, 480, 240, 0, NULL, g_object_type_palette_remaps[8],
	 0x20, MODEL_INDEX_NONE, 0, 59},
	/* 126 */
	{0x03, 0x0A, 4, 12, 480, 240, 0, NULL, g_object_type_palette_remaps[8],
	 0x20, MODEL_INDEX_NONE, 0, 60},
	/* 127 */
	{0x03, 0x0A, 5, 13, 3328, 240, 0,
	 g_object_type127_texture_frame_sequence,
	 g_object_type_palette_remaps[0], 0x40, MODEL_INDEX_NONE, 0, 62},
	/* 128 */
	{0x03, 0x0A, 5, 13, 3328, 240, 0,
	 g_object_type128_texture_frame_sequence,
	 g_object_type_palette_remaps[0], 0x40, MODEL_INDEX_NONE, 0, 63},
	/* 129 */
	{0x03, 0x0A, 5, 13, 3328, 240, 0,
	 g_object_type129_texture_frame_sequence,
	 g_object_type_palette_remaps[0], 0x40, MODEL_INDEX_NONE, 0, 61},
	/* 130 */
	{0x03, 0x0A, 5, 13, 3328, 240, 0,
	 g_object_type130_texture_frame_sequence,
	 g_object_type_palette_remaps[0], 0x40, MODEL_INDEX_NONE, 0, 85},
	/* 131 */
	{0x03, 0x0A, 5, 13, 1664, 240, 0,
	 g_object_type131_texture_frame_sequence,
	 g_object_type_palette_remaps[0], 0x40, MODEL_INDEX_NONE, 0, 64},
	/* 132 */
	{0x03, 0x0A, 5, 13, 1664, 240, 0,
	 g_object_type132_texture_frame_sequence,
	 g_object_type_palette_remaps[1], 0x40, MODEL_INDEX_NONE, 0, 65},
	/* 133 */
	{0x03, 0x0A, 5, 13, 1280, 240, 0,
	 g_object_type133_texture_frame_sequence,
	 g_object_type_palette_remaps[1], 0x40, MODEL_INDEX_NONE, 0, 86},
	/* 134 */
	{0x03, 0x0A, 5, 13, 1280, 240, 0,
	 g_object_type134_texture_frame_sequence,
	 g_object_type_palette_remaps[2], 0x40, MODEL_INDEX_NONE, 0, 66},
	/* 135 */
	{0x03, 0x0A, 5, 13, 1664, 240, 0, NULL, g_object_type_palette_remaps[0],
	 0x40, MODEL_INDEX_NONE, 0, 67},
	/* 136 */
	{0x03, 0x0A, 5, 13, 1664, 240, 0,
	 g_fuselage_damage_texture_frame_sequence,
	 g_object_type_palette_remaps[1], 0x40, MODEL_INDEX_NONE, 0, 68},
	/* 137 */
	{0x03, 0x09, 1, 7, 2048, 1024, 0, NULL, NULL, 0x40, MODEL_INDEX_NONE, 2,
	 3},
	/* 138 */
	{0x03, 0x09, 1, 7, 2048, 1024, 0, NULL, NULL, 0x40, MODEL_INDEX_NONE, 2,
	 6},
	/* 139 */
	{0x03, 0x09, 1, 7, 2048, 1024, 0, NULL, NULL, 0x40, MODEL_INDEX_NONE, 2,
	 4},
	/* 140 */
	{0x03, 0x09, 1, 7, 2048, 1024, 0, NULL, NULL, 0x40, MODEL_INDEX_NONE, 2,
	 7},
	/* 141 */
	{0x03, 0x09, 1, 7, 2048, 1024, 0, NULL, NULL, 0x40, MODEL_INDEX_NONE, 2,
	 5},
	/* 142 */
	{0x03, 0x09, 1, 7, 2048, 1024, 0, NULL, NULL, 0x40, MODEL_INDEX_NONE, 2,
	 8},
	/* 143 */
	{0x03, 0x09, 1, 7, 2048, 256, 0, NULL, NULL, 0x43, MODEL_INDEX_NONE, 2,
	 10},
	/* 144 */
	{0x03, 0x09, 1, 7, 2048, 256, 0, NULL, NULL, 0x43, MODEL_INDEX_NONE, 2,
	 9},
	/* 145 */
	{0x03, 0x09, 1, 7, 2048, 1024, 0, NULL, NULL, 0x00, MODEL_INDEX_NONE, 2,
	 6},
	/* 146 */
	{0x03, 0x09, 1, 7, 2048, 1024, 0, NULL, NULL, 0x00, MODEL_INDEX_NONE, 2,
	 7},
	/* 147 */
	{0x03, 0x09, 1, 7, 2048, 1024, 0, NULL, NULL, 0x00, MODEL_INDEX_NONE, 2,
	 8},
	/* 148 */
	{0x03, 0x09, 1, 7, 2048, 256, 0, NULL, NULL, 0x43, MODEL_INDEX_NONE, 2,
	 10},
	/* 149 */
	{0x03, 0x09, 1, 7, 2048, 256, 0, NULL, NULL, 0x43, MODEL_INDEX_NONE, 2,
	 9},
	/* 150 */
	{0x03, 0x29, 1, 7, 2048, 256, 0, NULL, NULL, 0x43, MODEL_INDEX_NONE, 0,
	 84},
	/* 151 */
	{0x03, 0x09, 1, 7, 2048, 256, 0, NULL, NULL, 0x43, MODEL_INDEX_NONE, 2,
	 11},
	/* 152 */
	{0x03, 0x09, 1, 7, 2048, 256, 0, NULL, NULL, 0x43, MODEL_INDEX_NONE, 2,
	 12},
	/* 153 */
	{0x03, 0x09, 1, 7, 2048, 256, 0, NULL, NULL, 0x43, MODEL_INDEX_NONE, 2,
	 12},
	/* 154 */
	{0x03, 0x09, 1, 7, 2048, 256, 0, NULL, NULL, 0x43, MODEL_INDEX_NONE, 2,
	 12},
	/* 155 */
	{0x03, 0x09, 1, 7, 597, 256, 0, NULL, NULL, 0x43, MODEL_INDEX_NONE, 2,
	 13},
	/* 156 */
	{0x03, 0x0A, 1, 7, 2048, 256, 0,
	 g_object_type133_texture_frame_sequence,
	 g_object_type_palette_remaps[1], 0x40, MODEL_INDEX_NONE, 0, 86},
	/* 157 */
	{0x03, 0x0A, 5, 13, 1280, 240, 0,
	 g_object_type157_texture_frame_sequence,
	 g_object_type_palette_remaps[1], 0x40, MODEL_INDEX_NONE, 2, 14},
	/* 158 */
	{0x01, 0x00, 1, 7, 2048, 256, 0, NULL, NULL, 0x00, MODEL_INDEX_NONE, 2,
	 0},
	/* 159 */
	{0x01, 0x00, 1, 7, 2048, 256, 0, NULL, NULL, 0x00, MODEL_INDEX_NONE, 2,
	 0},
	/* 160 */
	{0x01, 0x00, 1, 7, 2048, 256, 0, NULL, NULL, 0x00, MODEL_INDEX_NONE, 2,
	 0},
	/* 161 */ {0x00, 0x00, 0, 0, 0, 0, 0, NULL, NULL, 0x00, 0, 0, 0},
	/* 162 */ {0x00, 0x00, 0, 0, 0, 0, 0, NULL, NULL, 0x00, 0, 0, 0},
	/* 163 */ {0x00, 0x00, 0, 0, 0, 0, 0, NULL, NULL, 0x00, 0, 0, 0},
	/* 164 */ {0x00, 0x00, 0, 0, 0, 0, 0, NULL, NULL, 0x00, 0, 0, 0},
	/* 165 */ {0x00, 0x00, 0, 0, 0, 0, 0, NULL, NULL, 0x00, 0, 0, 0},
	/* 166 */ {0x00, 0x00, 0, 0, 0, 0, 0, NULL, NULL, 0x00, 0, 0, 0},
	/* 167 */ {0x00, 0x00, 0, 0, 0, 0, 0, NULL, NULL, 0x00, 0, 0, 0},
	/* 168 */ {0x00, 0x00, 0, 0, 0, 0, 0, NULL, NULL, 0x00, 0, 0, 0},
	/* 169 */ {0x00, 0x00, 0, 0, 0, 0, 0, NULL, NULL, 0x00, 0, 0, 0},
	/* 170 */ {0x00, 0x00, 0, 0, 0, 0, 0, NULL, NULL, 0x00, 0, 0, 0},
	/* 171 */ {0x00, 0x00, 0, 0, 0, 0, 0, NULL, NULL, 0x00, 0, 0, 0},
	/* 172 */ {0x00, 0x00, 0, 0, 0, 0, 0, NULL, NULL, 0x00, 0, 0, 0},
	/* 173 */ {0x00, 0x00, 0, 0, 0, 0, 0, NULL, NULL, 0x00, 0, 0, 0},
	/* 174 */ {0x00, 0x00, 0, 0, 0, 0, 0, NULL, NULL, 0x00, 0, 0, 0},
	/* 175 */ {0x00, 0x00, 0, 0, 0, 0, 0, NULL, NULL, 0x00, 0, 0, 0},
	/* 176 */ {0x00, 0x00, 0, 0, 0, 0, 0, NULL, NULL, 0x00, 0, 0, 0},
	/* 177 */ {0x00, 0x00, 0, 0, 0, 0, 0, NULL, NULL, 0x00, 0, 0, 0},
	/* 178 */ {0x00, 0x00, 0, 0, 0, 0, 0, NULL, NULL, 0x00, 0, 0, 0},
	/* 179 */ {0x00, 0x00, 0, 0, 0, 0, 0, NULL, NULL, 0x00, 0, 0, 0},
	/* 180 */ {0x00, 0x00, 0, 0, 0, 0, 0, NULL, NULL, 0x00, 0, 0, 0},
	/* 181 */ {0x00, 0x00, 0, 0, 0, 0, 0, NULL, NULL, 0x00, 0, 0, 0},
	/* 182 */ {0x00, 0x00, 0, 0, 0, 0, 0, NULL, NULL, 0x00, 0, 0, 0},
	/* 183 */ {0x00, 0x00, 0, 0, 0, 0, 0, NULL, NULL, 0x00, 0, 0, 0},
	/* 184 */ {0x00, 0x00, 0, 0, 0, 0, 0, NULL, NULL, 0x00, 0, 0, 0},
	/* 185 */ {0x00, 0x00, 0, 0, 0, 0, 0, NULL, NULL, 0x00, 0, 0, 0},
	/* 186 */ {0x00, 0x00, 0, 0, 0, 0, 0, NULL, NULL, 0x00, 0, 0, 0},
	/* 187 */ {0x00, 0x00, 0, 0, 0, 0, 0, NULL, NULL, 0x00, 0, 0, 0},
	/* 188 */ {0x00, 0x00, 0, 0, 0, 0, 0, NULL, NULL, 0x00, 0, 0, 0},
	/* 189 */ {0x00, 0x00, 0, 0, 0, 0, 0, NULL, NULL, 0x00, 0, 0, 0},
	/* 190 */ {0x00, 0x00, 0, 0, 0, 0, 0, NULL, NULL, 0x00, 0, 0, 0},
	/* 191 */ {0x00, 0x00, 0, 0, 0, 0, 0, NULL, NULL, 0x00, 0, 0, 0},
	/* 192 */ {0x00, 0x00, 0, 0, 0, 0, 0, NULL, NULL, 0x00, 0, 0, 0},
	/* 193 */ {0x00, 0x00, 0, 0, 0, 0, 0, NULL, NULL, 0x00, 0, 0, 0},
	/* 194 */ {0x00, 0x00, 0, 0, 0, 0, 0, NULL, NULL, 0x00, 0, 0, 0},
	/* 195 */ {0x00, 0x00, 0, 0, 0, 0, 0, NULL, NULL, 0x00, 0, 0, 0},
	/* 196 */ {0x00, 0x00, 0, 0, 0, 0, 0, NULL, NULL, 0x00, 0, 0, 0},
	/* 197 */ {0x00, 0x00, 0, 0, 0, 0, 0, NULL, NULL, 0x00, 0, 0, 0},
	/* 198 */ {0x00, 0x00, 0, 0, 0, 0, 0, NULL, NULL, 0x00, 0, 0, 0},
	/* 199 */ {0x00, 0x00, 0, 0, 0, 0, 0, NULL, NULL, 0x00, 0, 0, 0},
	/* 200 */ {0x00, 0x00, 0, 0, 0, 0, 0, NULL, NULL, 0x00, 0, 0, 0},
};
