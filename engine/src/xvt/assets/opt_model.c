#include "xvt/assets/opt_model.h"

#ifdef XVT_MODERN
#include "xvt_runtime/log/log.h"
#include "xvt_runtime/snapshot/render_assets.h"
#endif
#include "xvt/assets/file.h"
#ifdef XVT_MODERN
#include "xvt_runtime/assets/opt_native.h"
#endif
#ifndef XVT_MODERN
#include "xvt/assets/inventor_ascii.h"
#endif
#include "xvt/assets/model_texture.h"
#include "xvt/flight/fediskio.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/flight_loading.h"
#include "xvt/math/math3d.h"
#include "xvt/render/color.h"
#include "xvt/render/flight_palette.h"
#include "xvt/render/image_quantizer.h"
#include "xvt/render/render_scene.h"
#include "xvt/render/renderer.h"
#include "xvt/util/debug_console.h"
#include "xvt/util/memory.h"
#include "xvt_runtime/log/log_both_builds.h"
#ifndef XVT_MODERN
int _access(const char *filename, int mode);
#else
int access(const char *filename, int mode);
#endif

#ifndef XVT_MODERN
struct inventor_field_record {
	int field_type; /* The inventor_field_type of the values in data. */
	/* Values in data: 1 for one value, the length of a list. */
	int item_count;
	/* The values, stored in the model's block; for a string, its
	 * characters. */
	void *data;
};

struct opt_external_tex_header {
	/* The 8 bytes before the sizes; nothing reads them. */
	uint8_t prefix[8];
	/* Compared with width times height: when they are equal,
	 * stored_payload_size holds the texel bytes. */
	int pixel_count;
	/* Texel bytes, used when pixel_count equals width times height. */
	int stored_payload_size;
	int width;  /* Texture width in texels. */
	int height; /* Texture height in texels. */
};

typedef enum inventor_field_type {
	INVENTOR_FIELD_STRING = 1,
	INVENTOR_FIELD_NODE = 3,
	INVENTOR_FIELD_NODE_LIST = 4,
	INVENTOR_FIELD_INTEGER = 5,
	INVENTOR_FIELD_INTEGER_LIST = 6,
	INVENTOR_FIELD_FLOAT_AS_INTEGER = 7,
	INVENTOR_FIELD_FLOAT = 8,
	INVENTOR_FIELD_BOOLEAN = 9,
	INVENTOR_FIELD_VECTOR3 = 11,
	INVENTOR_FIELD_VECTOR3_LIST = 12,
	INVENTOR_FIELD_COLOR = 13,
	INVENTOR_FIELD_COLOR_LIST = 14,
	INVENTOR_FIELD_MATRIX = 15,
	INVENTOR_FIELD_MATRIX_LIST = 16,
	INVENTOR_FIELD_VECTOR2 = 17,
	INVENTOR_FIELD_VECTOR2_LIST = 18,
	INVENTOR_FIELD_ROTATION = 19,
	INVENTOR_FIELD_ROTATION_LIST = 20,
	INVENTOR_FIELD_ENUM = 21,
	INVENTOR_FIELD_ENUM_LIST = 22,
} inventor_field_type;

struct inventor_enum_def {
	/* Entries a lookup scans in value_names and values. The component and
	 * hardpoint enums give 41 for tables of 32. */
	int value_count;
	/* Names a value may take in the file, matched ignoring case; a word
	 * matching none is read as a number. */
	const char *const *value_names;
	const int *values; /* The value stored for each name. */
};

struct inventor_field_def {
	/* Name as written in the file. A word matches when, ignoring case, it
	 * is this name or its start. */
	const char *field_name;
	inventor_field_type field_type; /* How the parser reads the value. */
	/* Names and values of an enum field; NULL for others. */
	const struct inventor_enum_def *enum_def;
	/* Type and item count the record takes when the node leaves the field
	 * out. */
	const struct inventor_field_record *default_record;
	/* Values copied into the model when the node leaves the field out. */
	const void *default_data;
	size_t default_data_size; /* Bytes of default_data. */
};

/* Default record of an integer-list field: 4 items. */
// GLOBAL: XVT 0x51AF28
static const struct inventor_field_record g_default_integer_list_record = {
	INVENTOR_FIELD_INTEGER_LIST, 4, NULL};
/* Default of an integer-list field: 0, 1, 2, -1. */
// GLOBAL: XVT 0x51AF38
static const int g_default_integer_list[4] = {0, 1, 2, -1};
/* Default record of an integer field: 1 item. */
// GLOBAL: XVT 0x51AF58
static const struct inventor_field_record g_default_integer_record = {
	INVENTOR_FIELD_INTEGER, 1, NULL};
/* Default of an integer field: 0. */
// GLOBAL: XVT 0x51AF64
static const int g_default_integer = 0;
/* Default record of a string field: 1 item. */
// GLOBAL: XVT 0x51AF48
static const struct inventor_field_record g_default_string_record = {
	INVENTOR_FIELD_STRING, 1, NULL};
/* Default of a string field: the empty string. */
// GLOBAL: XVT 0x51AF54
static const char g_default_string[1] = {'\0'};
/* Default record of a list of 3-vectors: 1 item. */
// GLOBAL: XVT 0x51AFB8
static const struct inventor_field_record g_default_vector3_list_record = {
	INVENTOR_FIELD_VECTOR3_LIST, 1, NULL};
/* Default record of a single 3-vector: 1 item. */
// GLOBAL: XVT 0x51AFF0
static const struct inventor_field_record g_default_vector3_record = {
	INVENTOR_FIELD_VECTOR3, 1, NULL};
/* Default of the point and vector lists: one (0, 0, 0). */
// GLOBAL: XVT 0x51AFC8
static const struct opt_vector g_default_zero_vector = {0.0f, 0.0f, 0.0f};
/* Default of translation, center, size, minvector, maxvector, groupcenter and
 * the three axes: (0, 0, 0). */
// GLOBAL: XVT 0x51B000
static const struct opt_vector g_default_zero_scalar_vector = {0.0f, 0.0f,
							       0.0f};
/* Default of scaleFactor: (1, 1, 1). */
// GLOBAL: XVT 0x51B020
static const struct opt_vector g_default_unit_vector = {1.0f, 1.0f, 1.0f};
/* Default record of a rotation: 1 item. */
// GLOBAL: XVT 0x51B030
static const struct inventor_field_record g_default_rotation_record = {
	INVENTOR_FIELD_ROTATION, 1, NULL};
/* Default of rotation and scaleOrientation: 0, 0, 1, 0. */
// GLOBAL: XVT 0x51B040
static const float g_default_rotation[4] = {0.0f, 0.0f, 1.0f, 0.0f};
/* Default record of a color list: 1 item. */
// GLOBAL: XVT 0x51B050
static const struct inventor_field_record g_default_color_list_record = {
	INVENTOR_FIELD_COLOR_LIST, 1, NULL};
/* Default record of a single color: 1 item. */
// GLOBAL: XVT 0x51B090
static const struct inventor_field_record g_default_color_record = {
	INVENTOR_FIELD_COLOR, 1, NULL};
/* Default of ambientColor: (0.2, 0.2, 0.2). */
// GLOBAL: XVT 0x51B060
static const struct opt_vector g_default_ambient_color = {0.2f, 0.2f, 0.2f};
/* Default of diffuseColor: (0.8, 0.8, 0.8). */
// GLOBAL: XVT 0x51B070
static const struct opt_vector g_default_diffuse_color = {0.8f, 0.8f, 0.8f};
/* Default of specularColor and emissiveColor: (0, 0, 0). */
// GLOBAL: XVT 0x51B080
static const struct opt_vector g_default_black_color = {0.0f, 0.0f, 0.0f};
/* Default of rgb and blendColor: (0.8, 0.8, 0.8). */
// GLOBAL: XVT 0x51B0A0
static const struct opt_vector g_default_rgb_color = {0.8f, 0.8f, 0.8f};
/* Default record of a float field: 1 item. */
// GLOBAL: XVT 0x51B0B0
static const struct inventor_field_record g_default_float_record = {
	INVENTOR_FIELD_FLOAT, 1, NULL};
/* Default of screenArea and transparency: 0. */
// GLOBAL: XVT 0x51B0BC
static const float g_default_zero_float = 0.0f;
/* Default of shininess: 0.2. */
// GLOBAL: XVT 0x51B0C0
static const float g_default_shininess = 0.2f;
/* Default record of a list of 2-vectors: 1 item. */
// GLOBAL: XVT 0x51AFD8
static const struct inventor_field_record g_default_vector2_list_record = {
	INVENTOR_FIELD_VECTOR2_LIST, 1, NULL};
/* Default of the 2D point list: one (0, 0). */
// GLOBAL: XVT 0x51AFE8
static const float g_default_vector2[2] = {0.0f, 0.0f};
/* Default record of an enum field: 1 item. */
// GLOBAL: XVT 0x51AF18
static const struct inventor_field_record g_default_enum_record = {
	INVENTOR_FIELD_ENUM, 1, NULL};
/* Default of an enum field: 0, the value of each enum's first name. */
// GLOBAL: XVT 0x51AF24
static const int g_default_enum = 0;

/* Names of the binding enum, the value field of materialBinding, normalBinding
 * and textureCoordinateBinding. */
// GLOBAL: XVT 0x51B320
static const char *const g_binding_names[9] = {
	"DEFAULT",	     "NONE",	 "OVERALL",	     "PER_PART",
	"PER_PART_INDEXED",  "PER_FACE", "PER_FACE_INDEXED", "PER_VERTEX",
	"PER_VERTEX_INDEXED"};
/* Values of the binding names: 0 to 8 in order. */
// GLOBAL: XVT 0x51B348
static const int g_binding_values[9] = {0, 1, 2, 3, 4, 5, 6, 7, 8};
/* The binding enum: 9 names. */
// GLOBAL: XVT 0x51B370
static const struct inventor_enum_def g_binding_enum = {9, g_binding_names,
							g_binding_values};
/* Names of the wrapS and wrapT enum. */
// GLOBAL: XVT 0x51B400
static const char *const g_wrap_names[2] = {"REPEAT", "CLAMP"};
/* Values of the wrap names: 0 and 1. */
// GLOBAL: XVT 0x51B408
static const int g_wrap_values[2] = {0, 1};
/* The wrap enum: 2 names. */
// GLOBAL: XVT 0x51B410
static const struct inventor_enum_def g_wrap_enum = {2, g_wrap_names,
						     g_wrap_values};
/* Names of the texture2 model enum. */
// GLOBAL: XVT 0x51B460
static const char *const g_texture_model_names[3] = {"MODULATE", "DECAL",
						     "BLEND"};
/* Values of the texture model names: 0 to 2. */
// GLOBAL: XVT 0x51B470
static const int g_texture_model_values[3] = {0, 1, 2};
/* The texture model enum: 3 names. */
// GLOBAL: XVT 0x51B480
static const struct inventor_enum_def g_texture_model_enum = {
	3, g_texture_model_names, g_texture_model_values};
/* Names of the componentInfo type enum; name n stands for value n. */
// GLOBAL: XVT 0x51B7A0
static const char *const g_component_type_names[32] = {
	"COMPTYPE_DEFAULT",	"COMPTYPE_MAINHULL",
	"COMPTYPE_WING",	"COMPTYPE_FUSELAGE",
	"COMPTYPE_GUNTURRET",	"COMPTYPE_SMALLGUN",
	"COMPTYPE_ENGINE",	"COMPTYPE_BRIDGE",
	"COMPTYPE_SHIELDGEN",	"COMPTYPE_ENERGYGEN",
	"COMPTYPE_LAUNCHER",	"COMPTYPE_COMMSYS",
	"COMPTYPE_BEAMSYS",	"COMPTYPE_COMMANDBEAM",
	"COMPTYPE_DOCKINGPLAT", "COMPTYPE_LANDINGPLAT",
	"COMPTYPE_HANGAR",	"COMPTYPE_CARGOPOD",
	"COMPTYPE_MISCHULL",	"COMPTYPE_ANTENNA",
	"COMPTYPE_ROTWING",	"COMPTYPE_ROTGUNTURRET",
	"COMPTYPE_ROTLAUNCHER", "COMPTYPE_ROTCOMMSYS",
	"COMPTYPE_ROTBEAMSYS",	"COMPTYPE_ROTCOMMANDBEAM",
	"COMPTYPE_CUSTOM1",	"COMPTYPE_CUSTOM2",
	"COMPTYPE_CUSTOM3",	"COMPTYPE_CUSTOM4",
	"COMPTYPE_CUSTOM5",	"COMPTYPE_CUSTOM6",
};
/* Values of the component type names: 0 to 31 in order. */
// GLOBAL: XVT 0x51B820
static const int g_component_type_values[32] = {
	0,  1,	2,  3,	4,  5,	6,  7,	8,  9,	10, 11, 12, 13, 14, 15,
	16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31};
/* The component type enum. It claims 41 names for a table of 32, so a word that
 * matches none of the 32 sends the lookup past the end of
 * g_component_type_names. */
// GLOBAL: XVT 0x51B8A0
static const struct inventor_enum_def g_component_type_enum = {
	41, g_component_type_names, g_component_type_values};
/* Names of the hardpoint type enum; name n stands for value n. */
// GLOBAL: XVT 0x51B958
static const char *const g_hardpoint_type_names[32] = {
	"HARDPOINT_NONE",
	"HARDPOINT_REBELLASER",
	"HARDPOINT_TURBOREBELLASER",
	"HARDPOINT_EMPIRELASER",
	"HARDPOINT_TURBOEMPIRELASER",
	"HARDPOINT_IONCANNON",
	"HARDPOINT_TURBOIONCANNON",
	"HARDPOINT_TORPEDO",
	"HARDPOINT_MISSILE",
	"HARDPOINT_SUPERREBELLASER",
	"HARDPOINT_SUPEREMPIRELASER",
	"HARDPOINT_SUPERIONCANNON",
	"HARDPOINT_SUPERTORPEDO",
	"HARDPOINT_SUPERMISSILE",
	"HARDPOINT_DUMBBOMB",
	"HARDPOINT_FIREDBOMB",
	"HARDPOINT_MAGPULSE",
	"HARDPOINT_TURBOMAGPULSE",
	"HARDPOINT_SUPERMAGPULSE",
	"HARDPOINT_NEWWEAPON1",
	"HARDPOINT_NEWWEAPON2",
	"HARDPOINT_NEWWEAPON3",
	"HARDPOINT_NEWWEAPON4",
	"HARDPOINT_NEWWEAPON5",
	"HARDPOINT_NEWWEAPON6",
	"HARDPOINT_INSIDEHANGAR",
	"HARDPOINT_OUTSIDEHANGAR",
	"HARDPOINT_DOCKFROMBIG",
	"HARDPOINT_DOCKFROMSMALL",
	"HARDPOINT_DOCKTOBIG",
	"HARDPOINT_DOCKTOSMALL",
	"HARDPOINT_COCKPIT",
};
/* Values of the hardpoint type names: 0 to 31 in order. */
// GLOBAL: XVT 0x51B9D8
static const int g_hardpoint_type_values[32] = {
	0,  1,	2,  3,	4,  5,	6,  7,	8,  9,	10, 11, 12, 13, 14, 15,
	16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31};
/* The hardpoint type enum. It claims 41 names for a table of 32, so a word that
 * matches none of the 32 sends the lookup past the end of
 * g_hardpoint_type_names. */
// GLOBAL: XVT 0x51BA58
static const struct inventor_enum_def g_hardpoint_type_enum = {
	41, g_hardpoint_type_names, g_hardpoint_type_values};

/* Field coordIndex: an integer list; the default is 0, 1, 2, -1. */
// GLOBAL: XVT 0x51B0C8
static const struct inventor_field_def g_field_coord_index = {
	"coordIndex",
	INVENTOR_FIELD_INTEGER_LIST,
	NULL,
	&g_default_integer_list_record,
	&g_default_integer_list,
	sizeof(g_default_integer_list)};
/* Field materialIndex: an integer list; the default is 0, 1, 2, -1. */
// GLOBAL: XVT 0x51B0E0
static const struct inventor_field_def g_field_material_index = {
	"materialIndex",
	INVENTOR_FIELD_INTEGER_LIST,
	NULL,
	&g_default_integer_list_record,
	&g_default_integer_list,
	sizeof(g_default_integer_list)};
/* Field normal_index: an integer list; the default is 0, 1, 2, -1. */
// GLOBAL: XVT 0x51B0F8
static const struct inventor_field_def g_field_normal_index = {
	"normalIndex",
	INVENTOR_FIELD_INTEGER_LIST,
	NULL,
	&g_default_integer_list_record,
	&g_default_integer_list,
	sizeof(g_default_integer_list)};
/* Field textureCoordIndex: an integer list; the default is 0, 1, 2, -1. */
// GLOBAL: XVT 0x51B110
static const struct inventor_field_def g_field_texture_coord_index = {
	"textureCoordIndex",
	INVENTOR_FIELD_INTEGER_LIST,
	NULL,
	&g_default_integer_list_record,
	&g_default_integer_list,
	sizeof(g_default_integer_list)};
/* Field numVertices: an integer list; the default is 0, 1, 2, -1. */
// GLOBAL: XVT 0x51B128
static const struct inventor_field_def g_field_num_vertices = {
	"numVertices",
	INVENTOR_FIELD_INTEGER_LIST,
	NULL,
	&g_default_integer_list_record,
	&g_default_integer_list,
	sizeof(g_default_integer_list)};
/* Field startIndex: an integer; the default is 0. */
// GLOBAL: XVT 0x51B140
static const struct inventor_field_def g_field_start_index = {
	"startIndex",
	INVENTOR_FIELD_INTEGER,
	NULL,
	&g_default_integer_record,
	&g_default_integer,
	sizeof(g_default_integer)};
/* Field verticesPerRow: an integer; the default is 0. */
// GLOBAL: XVT 0x51B158
static const struct inventor_field_def g_field_vertices_per_row = {
	"verticesPerRow",
	INVENTOR_FIELD_INTEGER,
	NULL,
	&g_default_integer_record,
	&g_default_integer,
	sizeof(g_default_integer)};
/* Field verticesPerColumn: an integer; the default is 0. */
// GLOBAL: XVT 0x51B170
static const struct inventor_field_def g_field_vertices_per_column = {
	"verticesPerColumn",
	INVENTOR_FIELD_INTEGER,
	NULL,
	&g_default_integer_record,
	&g_default_integer,
	sizeof(g_default_integer)};
/* Field point of textureCoordinate2: a list of 2-vectors; the default is one
 * (0, 0). */
// GLOBAL: XVT 0x51B1A0
static const struct inventor_field_def g_field_point2 = {
	"point",
	INVENTOR_FIELD_VECTOR2_LIST,
	NULL,
	&g_default_vector2_list_record,
	&g_default_vector2,
	sizeof(g_default_vector2)};
/* Field point of coordinate3 and hardpoint: a list of 3-vectors; the default is
 * one (0, 0, 0). */
// GLOBAL: XVT 0x51B1B8
static const struct inventor_field_def g_field_point3 = {
	"point",
	INVENTOR_FIELD_VECTOR3_LIST,
	NULL,
	&g_default_vector3_list_record,
	&g_default_zero_vector,
	sizeof(g_default_zero_vector)};
/* Field vector of normal: a list of 3-vectors; the default is one (0, 0, 0). */
// GLOBAL: XVT 0x51B1D0
static const struct inventor_field_def g_field_vector = {
	"vector",
	INVENTOR_FIELD_VECTOR3_LIST,
	NULL,
	&g_default_vector3_list_record,
	&g_default_zero_vector,
	sizeof(g_default_zero_vector)};
/* Field translation: a 3-vector; the default is (0, 0, 0). */
// GLOBAL: XVT 0x51B1E8
static const struct inventor_field_def g_field_translation = {
	"translation",
	INVENTOR_FIELD_VECTOR3,
	NULL,
	&g_default_vector3_record,
	&g_default_zero_scalar_vector,
	sizeof(g_default_zero_scalar_vector)};
/* Field scaleFactor: a 3-vector; the default is (1, 1, 1). */
// GLOBAL: XVT 0x51B200
static const struct inventor_field_def g_field_scale_factor = {
	"scaleFactor",
	INVENTOR_FIELD_VECTOR3,
	NULL,
	&g_default_vector3_record,
	&g_default_unit_vector,
	sizeof(g_default_unit_vector)};
/* Field center: a 3-vector; the default is (0, 0, 0). */
// GLOBAL: XVT 0x51B218
static const struct inventor_field_def g_field_center = {
	"center",
	INVENTOR_FIELD_VECTOR3,
	NULL,
	&g_default_vector3_record,
	&g_default_zero_scalar_vector,
	sizeof(g_default_zero_scalar_vector)};
/* Field rotation: four floats; the default is 0, 0, 1, 0. */
// GLOBAL: XVT 0x51B230
static const struct inventor_field_def g_field_rotation = {
	"rotation",
	INVENTOR_FIELD_ROTATION,
	NULL,
	&g_default_rotation_record,
	&g_default_rotation,
	sizeof(g_default_rotation)};
/* Field scaleOrientation: four floats; the default is 0, 0, 1, 0. */
// GLOBAL: XVT 0x51B248
static const struct inventor_field_def g_field_scale_orientation = {
	"scaleOrientation",
	INVENTOR_FIELD_ROTATION,
	NULL,
	&g_default_rotation_record,
	&g_default_rotation,
	sizeof(g_default_rotation)};
/* Field ambientColor: a color list; the default is one (0.2, 0.2, 0.2). */
// GLOBAL: XVT 0x51B260
static const struct inventor_field_def g_field_ambient_color = {
	"ambientColor",
	INVENTOR_FIELD_COLOR_LIST,
	NULL,
	&g_default_color_list_record,
	&g_default_ambient_color,
	sizeof(g_default_ambient_color)};
/* Field diffuseColor: a color list; the default is one (0.8, 0.8, 0.8). */
// GLOBAL: XVT 0x51B278
static const struct inventor_field_def g_field_diffuse_color = {
	"diffuseColor",
	INVENTOR_FIELD_COLOR_LIST,
	NULL,
	&g_default_color_list_record,
	&g_default_diffuse_color,
	sizeof(g_default_diffuse_color)};
/* Field specularColor: a color list; the default is one (0, 0, 0). */
// GLOBAL: XVT 0x51B290
static const struct inventor_field_def g_field_specular_color = {
	"specularColor",
	INVENTOR_FIELD_COLOR_LIST,
	NULL,
	&g_default_color_list_record,
	&g_default_black_color,
	sizeof(g_default_black_color)};
/* Field emissiveColor: a color list; the default is one (0, 0, 0). */
// GLOBAL: XVT 0x51B2A8
static const struct inventor_field_def g_field_emissive_color = {
	"emissiveColor",
	INVENTOR_FIELD_COLOR_LIST,
	NULL,
	&g_default_color_list_record,
	&g_default_black_color,
	sizeof(g_default_black_color)};
/* Field rgb of baseColor: a color; the default is (0.8, 0.8, 0.8). */
// GLOBAL: XVT 0x51B2C0
static const struct inventor_field_def g_field_rgb = {
	"rgb",
	INVENTOR_FIELD_COLOR,
	NULL,
	&g_default_color_record,
	&g_default_rgb_color,
	sizeof(g_default_rgb_color)};
/* Field screenArea of levelofdetail: read as a float, or a list of floats in
 * brackets; the default is 0. */
// GLOBAL: XVT 0x51B2D8
static const struct inventor_field_def g_field_screen_area = {
	"screenArea",
	INVENTOR_FIELD_FLOAT,
	NULL,
	&g_default_float_record,
	&g_default_zero_float,
	sizeof(g_default_zero_float)};
/* Field shininess: read as a float, or a list of floats in brackets; the
 * default is 0.2. */
// GLOBAL: XVT 0x51B2F0
static const struct inventor_field_def g_field_shininess = {
	"shininess",
	INVENTOR_FIELD_FLOAT,
	NULL,
	&g_default_float_record,
	&g_default_shininess,
	sizeof(g_default_shininess)};
/* Field transparency: read as a float, or a list of floats in brackets; the
 * default is 0. */
// GLOBAL: XVT 0x51B308
static const struct inventor_field_def g_field_transparency = {
	"transparency",
	INVENTOR_FIELD_FLOAT,
	NULL,
	&g_default_float_record,
	&g_default_zero_float,
	sizeof(g_default_zero_float)};
/* Field value of the three binding nodes: a binding enum; the default is 0,
 * DEFAULT. */
// GLOBAL: XVT 0x51B380
static const struct inventor_field_def g_field_binding_value = {
	"value",	 INVENTOR_FIELD_ENUM,
	&g_binding_enum, &g_default_enum_record,
	&g_default_enum, sizeof(g_default_enum)};
/* Field filename of use and texture2: a string; the default is empty. */
// GLOBAL: XVT 0x51B3E8
static const struct inventor_field_def g_field_filename = {
	"filename",
	INVENTOR_FIELD_STRING,
	NULL,
	&g_default_string_record,
	&g_default_string,
	sizeof(g_default_string)};
/* Field wrapS: a wrap enum; the default is 0, REPEAT. */
// GLOBAL: XVT 0x51B420
static const struct inventor_field_def g_field_wrap_s = {
	"wrapS",	 INVENTOR_FIELD_ENUM,
	&g_wrap_enum,	 &g_default_enum_record,
	&g_default_enum, sizeof(g_default_enum)};
/* Field wrapT: a wrap enum; the default is 0, REPEAT. */
// GLOBAL: XVT 0x51B448
static const struct inventor_field_def g_field_wrap_t = {
	"wrapT",	 INVENTOR_FIELD_ENUM,
	&g_wrap_enum,	 &g_default_enum_record,
	&g_default_enum, sizeof(g_default_enum)};
/* Field model of texture2: a texture model enum; the default is 0, MODULATE. */
// GLOBAL: XVT 0x51B490
static const struct inventor_field_def g_field_texture_model = {
	"model",
	INVENTOR_FIELD_ENUM,
	&g_texture_model_enum,
	&g_default_enum_record,
	&g_default_enum,
	sizeof(g_default_enum)};
/* Field blendColor: a color; the default is (0.8, 0.8, 0.8). */
// GLOBAL: XVT 0x51B4A8
static const struct inventor_field_def g_field_blend_color = {
	"blendColor",
	INVENTOR_FIELD_COLOR,
	NULL,
	&g_default_color_record,
	&g_default_rgb_color,
	sizeof(g_default_rgb_color)};
/* Field type of componentInfo: a component type enum; the default is 0,
 * COMPTYPE_DEFAULT. */
// GLOBAL: XVT 0x51B8B0
static const struct inventor_field_def g_field_component_type = {
	"type",
	INVENTOR_FIELD_ENUM,
	&g_component_type_enum,
	&g_default_enum_record,
	&g_default_enum,
	sizeof(g_default_enum)};
/* Field size: a 3-vector; the default is (0, 0, 0). */
// GLOBAL: XVT 0x51B8C8
static const struct inventor_field_def g_field_size = {
	"size",
	INVENTOR_FIELD_VECTOR3,
	NULL,
	&g_default_vector3_record,
	&g_default_zero_scalar_vector,
	sizeof(g_default_zero_scalar_vector)};
/* Field minvector: a 3-vector; the default is (0, 0, 0). */
// GLOBAL: XVT 0x51B8E0
static const struct inventor_field_def g_field_min_vector = {
	"minvector",
	INVENTOR_FIELD_VECTOR3,
	NULL,
	&g_default_vector3_record,
	&g_default_zero_scalar_vector,
	sizeof(g_default_zero_scalar_vector)};
/* Field maxvector: a 3-vector; the default is (0, 0, 0). */
// GLOBAL: XVT 0x51B8F8
static const struct inventor_field_def g_field_max_vector = {
	"maxvector",
	INVENTOR_FIELD_VECTOR3,
	NULL,
	&g_default_vector3_record,
	&g_default_zero_scalar_vector,
	sizeof(g_default_zero_scalar_vector)};
/* Field groupcenter: a 3-vector; the default is (0, 0, 0). */
// GLOBAL: XVT 0x51B910
static const struct inventor_field_def g_field_group_center = {
	"groupcenter",
	INVENTOR_FIELD_VECTOR3,
	NULL,
	&g_default_vector3_record,
	&g_default_zero_scalar_vector,
	sizeof(g_default_zero_scalar_vector)};
/* Field flags: an integer; the default is 0. */
// GLOBAL: XVT 0x51B928
static const struct inventor_field_def g_field_flags = {
	"flags",
	INVENTOR_FIELD_INTEGER,
	NULL,
	&g_default_integer_record,
	&g_default_integer,
	sizeof(g_default_integer)};
/* Field groupid: an integer; the default is 0. */
// GLOBAL: XVT 0x51B940
static const struct inventor_field_def g_field_group_id = {
	"groupid",
	INVENTOR_FIELD_INTEGER,
	NULL,
	&g_default_integer_record,
	&g_default_integer,
	sizeof(g_default_integer)};
/* Field type of hardpoint: a hardpoint type enum; the default is 0,
 * HARDPOINT_NONE. */
// GLOBAL: XVT 0x51BA68
static const struct inventor_field_def g_field_hardpoint_type = {
	"type",
	INVENTOR_FIELD_ENUM,
	&g_hardpoint_type_enum,
	&g_default_enum_record,
	&g_default_enum,
	sizeof(g_default_enum)};
/* Field axis1: a 3-vector; the default is (0, 0, 0). */
// GLOBAL: XVT 0x51BA80
static const struct inventor_field_def g_field_axis1 = {
	"axis1",
	INVENTOR_FIELD_VECTOR3,
	NULL,
	&g_default_vector3_record,
	&g_default_zero_scalar_vector,
	sizeof(g_default_zero_scalar_vector)};
/* Field axis2: a 3-vector; the default is (0, 0, 0). */
// GLOBAL: XVT 0x51BA98
static const struct inventor_field_def g_field_axis2 = {
	"axis2",
	INVENTOR_FIELD_VECTOR3,
	NULL,
	&g_default_vector3_record,
	&g_default_zero_scalar_vector,
	sizeof(g_default_zero_scalar_vector)};
/* Field axis3: a 3-vector; the default is (0, 0, 0). */
// GLOBAL: XVT 0x51BAB0
static const struct inventor_field_def g_field_axis3 = {
	"axis3",
	INVENTOR_FIELD_VECTOR3,
	NULL,
	&g_default_vector3_record,
	&g_default_zero_scalar_vector,
	sizeof(g_default_zero_scalar_vector)};

/* Fields of indexedFaceSet, in record order. */
// GLOBAL: XVT 0x51BAD8
static const struct inventor_field_def *const g_fields_indexed_face_set[] = {
	&g_field_coord_index, &g_field_material_index, &g_field_normal_index,
	&g_field_texture_coord_index};
/* Fields of transform, in record order. */
// GLOBAL: XVT 0x51BAF8
static const struct inventor_field_def *const g_fields_transform[] = {
	&g_field_translation, &g_field_rotation, &g_field_scale_factor,
	&g_field_scale_orientation, &g_field_center};
/* Fields of coordinate3. */
// GLOBAL: XVT 0x51BB1C
static const struct inventor_field_def *const g_fields_coordinate3[] = {
	&g_field_point3};
/* Fields of translation. */
// GLOBAL: XVT 0x51BB2C
static const struct inventor_field_def *const g_fields_translation[] = {
	&g_field_translation};
/* Fields of rotation. */
// GLOBAL: XVT 0x51BB3C
static const struct inventor_field_def *const g_fields_rotation[] = {
	&g_field_rotation};
/* Fields of scale. */
// GLOBAL: XVT 0x51BB4C
static const struct inventor_field_def *const g_fields_scale[] = {
	&g_field_scale_factor};
/* Fields of use. */
// GLOBAL: XVT 0x51BB5C
static const struct inventor_field_def *const g_fields_use[] = {
	&g_field_filename};
/* Fields of material, in record order. */
// GLOBAL: XVT 0x51BB80
static const struct inventor_field_def *const g_fields_material[] = {
	&g_field_ambient_color,	 &g_field_diffuse_color,
	&g_field_specular_color, &g_field_emissive_color,
	&g_field_shininess,	 &g_field_transparency};
/* Fields of the three binding nodes. */
// GLOBAL: XVT 0x51BBA4
static const struct inventor_field_def *const g_fields_binding[] = {
	&g_field_binding_value};
/* Fields of normal. */
// GLOBAL: XVT 0x51BBB4
static const struct inventor_field_def *const g_fields_normal[] = {
	&g_field_vector};
/* Fields of textureCoordinate2. */
// GLOBAL: XVT 0x51BBD4
static const struct inventor_field_def *const g_fields_texture_coordinate2[] = {
	&g_field_point2};
/* Fields of quadMesh, in record order. */
// GLOBAL: XVT 0x51BBF8
static const struct inventor_field_def *const g_fields_quad_mesh[] = {
	&g_field_start_index, &g_field_vertices_per_row,
	&g_field_vertices_per_column};
/* Fields of faceSet and triangleStripSet. */
// GLOBAL: XVT 0x51BC14
static const struct inventor_field_def *const g_fields_face_set[] = {
	&g_field_num_vertices};
/* Fields of baseColor. */
// GLOBAL: XVT 0x51BC44
static const struct inventor_field_def *const g_fields_base_color[] = {
	&g_field_rgb};
/* Fields of texture2, in record order. */
// GLOBAL: XVT 0x51BC58
static const struct inventor_field_def *const g_fields_texture2[] = {
	&g_field_filename, &g_field_wrap_s, &g_field_wrap_t,
	&g_field_texture_model, &g_field_blend_color};
/* Fields of levelofdetail. */
// GLOBAL: XVT 0x51BC7C
static const struct inventor_field_def *const g_fields_level_of_detail[] = {
	&g_field_screen_area};
/* Fields of hardpoint, in record order. */
// GLOBAL: XVT 0x51BC90
static const struct inventor_field_def *const g_fields_hardpoint[] = {
	&g_field_hardpoint_type, &g_field_point3};
/* Fields of pivot, in record order. */
// GLOBAL: XVT 0x51BCA8
static const struct inventor_field_def *const g_fields_pivot[] = {
	&g_field_center, &g_field_axis1, &g_field_axis2, &g_field_axis3};
/* Fields of componentInfo, in record order. */
// GLOBAL: XVT 0x51BCD8
static const struct inventor_field_def *const g_fields_component_info[] = {
	&g_field_component_type, &g_field_flags,	&g_field_size,
	&g_field_center,	 &g_field_min_vector,	&g_field_max_vector,
	&g_field_group_id,	 &g_field_group_center,
};

/* The node types the Inventor importer knows, one row per opt_node_type value in
 * order: the name in the file, the field count and the fields. */
// GLOBAL: XVT 0x51BAC8
static const struct inventor_node_def g_inventor_node_def_data[26] = {
	{"separator", 0, NULL},
	{"indexedFaceSet", 4, g_fields_indexed_face_set},
	{"transform", 5, g_fields_transform},
	{"coordinate3", 1, g_fields_coordinate3},
	{"translation", 1, g_fields_translation},
	{"rotation", 1, g_fields_rotation},
	{"scale", 1, g_fields_scale},
	{"use", 1, g_fields_use},
	{"def", 0, NULL},
	{"material", 6, g_fields_material},
	{"materialBinding", 1, g_fields_binding},
	{"normal", 1, g_fields_normal},
	{"normalBinding", 1, g_fields_binding},
	{"textureCoordinate2", 1, g_fields_texture_coordinate2},
	{"textureCoordinateBinding", 1, g_fields_binding},
	{"quadMesh", 3, g_fields_quad_mesh},
	{"faceSet", 1, g_fields_face_set},
	{"triangleStripSet", 1, g_fields_face_set},
	{"group", 0, NULL},
	{"baseColor", 1, g_fields_base_color},
	{"texture2", 5, g_fields_texture2},
	{"levelofdetail", 1, g_fields_level_of_detail},
	{"hardpoint", 2, g_fields_hardpoint},
	{"pivot", 4, g_fields_pivot},
	{"camoswitch", 0, NULL},
	{"componentInfo", 8, g_fields_component_info},
};

/* Pointers to the rows of g_inventor_node_def_data, the table
 * opt_model_parse_inventor_ascii_node searches in order. */
// GLOBAL: XVT 0x51BD08
const struct inventor_node_def *const g_inventor_node_defs[26] = {
	&g_inventor_node_def_data[0],  &g_inventor_node_def_data[1],
	&g_inventor_node_def_data[2],  &g_inventor_node_def_data[3],
	&g_inventor_node_def_data[4],  &g_inventor_node_def_data[5],
	&g_inventor_node_def_data[6],  &g_inventor_node_def_data[7],
	&g_inventor_node_def_data[8],  &g_inventor_node_def_data[9],
	&g_inventor_node_def_data[10], &g_inventor_node_def_data[11],
	&g_inventor_node_def_data[12], &g_inventor_node_def_data[13],
	&g_inventor_node_def_data[14], &g_inventor_node_def_data[15],
	&g_inventor_node_def_data[16], &g_inventor_node_def_data[17],
	&g_inventor_node_def_data[18], &g_inventor_node_def_data[19],
	&g_inventor_node_def_data[20], &g_inventor_node_def_data[21],
	&g_inventor_node_def_data[22], &g_inventor_node_def_data[23],
	&g_inventor_node_def_data[24], &g_inventor_node_def_data[25],
};

/* Per field of the node opt_model_parse_inventor_ascii_node is reading, 1 once the
 * file gave it; that function clears the node's entries before its fields and
 * fills the fields still at 0 from their defaults. Only that function writes
 * it. */
// GLOBAL: XVT 0x5505F8
uint8_t g_inventor_field_seen[256] = {0};
/* Scratch for one word read from an Inventor file, up to 256 characters and a
 * NUL, and for the model file name opt_model_load_handle tries. */
// GLOBAL: XVT 0x5506F8
char g_opt_model_load_scratch_buffer[257] = {0};
#endif

/* One row per model, indexed by model_index: names, flight and combat figures,
 * weapon groups and points; the starting values come from model_defs_data.inc.
 * fe_disk_io_build_model_def fills the bound sizes and the dock, hangar, primary
 * and weapon points from each loaded OPT model, and string_table_load_game_strings
 * sets each name_long. */
// GLOBAL: XVT 0x51C560
struct model_def g_model_defs[73] = {
/* drift-ok: include-midfile -- the table's rows, not a header */
#include "xvt/assets/model_defs_data.inc"
};
/* The float 1.0; the software renderer and this file's normal builders read
 * it. */
// GLOBAL: XVT 0x5181C0
const float g_sw3d_unit_float = 1.0f;
/* The float 3.0, the corner count sw3d_project_mesh_vertices divides by a
 * triangle's summed corner w values. */
// GLOBAL: XVT 0x5181C4
const float g_sw3d_triangle_corner_count = 3.0f;
/* The float 4.0, the corner count sw3d_project_mesh_vertices divides by a quad's
 * summed corner w values. */
// GLOBAL: XVT 0x5181C8
const float g_sw3d_quad_corner_count = 4.0f;
/* The float 0.0; the software renderer, render_scene_cull_mesh_faces_from_view and
 * this file's normal builders compare with it. */
// GLOBAL: XVT 0x5181B8
const float g_sw3d_zero_float = 0.0f;
/* The float 100000.0: sw3d_project_mesh_vertices_distant multiplies its projection
 * scale by it and adds it to each transformed vertex's z. */
// GLOBAL: XVT 0x5181CC
const float g_sw3d_distant_depth = 100000.0f;
/* 1 while the model walkers keep an OPT_NODEREF node's target in the node after
 * the first lookup: the original build in its pName, blanking the name, the
 * modern build through xvt_opt_resolve_cached. Nothing writes it, so it stays
 * 1. */
// GLOBAL: XVT 0x5270B0
int g_cache_resolved_opt_node_refs = 1;
/* 1 while the model being loaded came from a version 0 file, whose face records
 * carry no normal indices: 48 bytes each where version 1 has 64. Only
 * opt_model_load_file_to_handle writes it; the legacy converter reads it, and
 * opt_model_save_handle_to_file picks its version marker from it. */
// GLOBAL: XVT 0x5272B0
int g_opt_source_is_version0 = 0;
#ifndef XVT_MODERN
/* The extension "rgb", compared ignoring case by the texture loaders. */
// GLOBAL: XVT 0x51AE00
const char g_ext_rgb[4] = "rgb";
/* The extension "tex", compared ignoring case by the texture loaders. */
// GLOBAL: XVT 0x5272CC
const char g_ext_tex[4] = "tex";
#endif
/* 1 / n at index n, from 1 to 69, with 1.0 at index 0; read by the software
 * renderer, flight_starfield_render, render_scene_allocate_buffers and
 * opt_model_build_vertex_normals_from_faces. */
// GLOBAL: XVT 0x5270B8
const float g_sw3d_span_length_reciprocal[70] = {
	1.0f,	      1.0f,	    1.0f / 2.0f,  1.0f / 3.0f,	1.0f / 4.0f,
	1.0f / 5.0f,  1.0f / 6.0f,  1.0f / 7.0f,  1.0f / 8.0f,	1.0f / 9.0f,
	1.0f / 10.0f, 1.0f / 11.0f, 1.0f / 12.0f, 1.0f / 13.0f, 1.0f / 14.0f,
	1.0f / 15.0f, 1.0f / 16.0f, 1.0f / 17.0f, 1.0f / 18.0f, 1.0f / 19.0f,
	1.0f / 20.0f, 1.0f / 21.0f, 1.0f / 22.0f, 1.0f / 23.0f, 1.0f / 24.0f,
	1.0f / 25.0f, 1.0f / 26.0f, 1.0f / 27.0f, 1.0f / 28.0f, 1.0f / 29.0f,
	1.0f / 30.0f, 1.0f / 31.0f, 1.0f / 32.0f, 1.0f / 33.0f, 1.0f / 34.0f,
	1.0f / 35.0f, 1.0f / 36.0f, 1.0f / 37.0f, 1.0f / 38.0f, 1.0f / 39.0f,
	1.0f / 40.0f, 1.0f / 41.0f, 1.0f / 42.0f, 1.0f / 43.0f, 1.0f / 44.0f,
	1.0f / 45.0f, 1.0f / 46.0f, 1.0f / 47.0f, 1.0f / 48.0f, 1.0f / 49.0f,
	1.0f / 50.0f, 1.0f / 51.0f, 1.0f / 52.0f, 1.0f / 53.0f, 1.0f / 54.0f,
	1.0f / 55.0f, 1.0f / 56.0f, 1.0f / 57.0f, 1.0f / 58.0f, 1.0f / 59.0f,
	1.0f / 60.0f, 1.0f / 61.0f, 1.0f / 62.0f, 1.0f / 63.0f, 1.0f / 64.0f,
	1.0f / 65.0f, 1.0f / 66.0f, 1.0f / 67.0f, 1.0f / 68.0f, 1.0f / 69.0f,
};
/* Per object type, the Memory handle of its flight resource, a runtime model
 * from opt_model_load_handle or a texture block; 0 for none.
 * fe_disk_io_load_resources fills it, fe_disk_io_free_flight_resources clears it,
 * model_preview_load_model keeps the preview model in slot 0, and the modern
 * xvt_frontend_task_shutdown clears it. */
// GLOBAL: XVT 0x9A7ED0
uint16_t g_loaded_models[201] = {0};
/* Where opt_model_remap_vector_index starts its next search: the index it last
 * found, or the list length after a miss. Only that function writes it, and
 * nothing resets it between models. */
// GLOBAL: XVT 0x60F1E4
static int g_opt_convert_vector_search_cursor = 0;
/* Where opt_model_remap_tex_coord_index starts its next search: the index it last
 * found, or the list length after a miss. Only that function writes it, and
 * nothing resets it between models. */
// GLOBAL: XVT 0x60F1FC
static int g_opt_convert_tex_coord_search_cursor = 0;
/* Handle of the shared block a model file is loaded and converted in; kept and
 * regrown from one load to the next, 0 before the first. Only
 * opt_model_load_file_to_handle and opt_model_convert_legacy_model_to_optimized write
 * it. */
// GLOBAL: XVT 0x5272A8
static uint16_t g_load_opt_buf_handle = 0;
/* Bytes allocated for g_load_opt_buf_handle (the decoded size in the modern
 * build); written by the same two functions. */
// GLOBAL: XVT 0x5272AC
static int g_load_opt_buf_size = 0;
/* Handle of the copy opt_model_convert_legacy_model_to_optimized converts from; it
 * alone writes it, keeps it between calls, regrows it and never frees it. */
// GLOBAL: XVT 0x5272B4
static uint16_t g_opt_convert_source_handle = 0;
/* Bytes allocated for g_opt_convert_source_handle. */
// GLOBAL: XVT 0x5272B8
static unsigned int g_opt_convert_source_buf_size = 0;
/* Vertex count of the OPT_MESHVERTS node the model walkers last passed. Many
 * functions write it, chiefly this file's walkers and RenderScene's, which set
 * it to 0 before a walk. */
// GLOBAL: XVT 0x60F204
int g_cur_vertex_count = 0;
/* Vertex list of the mesh node the model walkers last passed; the converters
 * and the normal builders read it. Set to NULL before each walk, here and in
 * RenderScene's walkers. */
// GLOBAL: XVT 0x60F1C0
void *g_cur_mesh_vertices = NULL;
/* Material records of the OPT_MATERIAL node the model walkers last passed. Set
 * to NULL before each walk, here and in RenderScene's walkers. */
// GLOBAL: XVT 0x60F1D8
void *g_cur_mesh_materials = NULL;
/* Texture coordinates of the OPT_TEXCOORDS node the model walkers last passed.
 * Set to NULL before each walk, here and in RenderScene's walkers. */
// GLOBAL: XVT 0x60F1E8
void *g_cur_mesh_tex_coords = NULL;
/* Every model walker sets it to NULL before it walks; nothing sets it to
 * anything else or reads it. */
// GLOBAL: XVT 0x60F1F0
void *g_model_node_walk_unused_scratch2 = NULL;
/* Vertex normals of the OPT_VERTNORMALS node the model walkers last passed. Set
 * to NULL before each walk, here and in RenderScene's walkers. */
// GLOBAL: XVT 0x60F208
struct opt_vector *g_cur_vert_normals = NULL;
#ifndef XVT_MODERN
/* Vectors in the scratch block opt_model_convert_imported_handle_to_packed
 * allocates; that function sets it to 1 and nothing raises it. */
// GLOBAL: XVT 0x5272C0
int g_opt_import_scratch_vector_count = 0;
/* The scratch block opt_model_convert_imported_handle_to_packed allocates and frees
 * while it packs an import. Nothing reads it, and it keeps pointing at the
 * block after the block is freed. */
// GLOBAL: XVT 0x5272C4
struct opt_vector *g_opt_import_scratch_vectors = NULL;
/* When nonzero, opt_model_build_face_normal_tangent_data negates each face normal it
 * builds. Nothing writes it, so it stays 0. */
// GLOBAL: XVT 0x5271D0
int g_opt_model_invert_face_normals = 0;
/* Vertex normals opt_model_build_face_normal_tangent_data last built after a face
 * node's data: the vertex count when the mesh had no OPT_VERTNORMALS node, else
 * 0. opt_model_append_packed_face_derived_data steps over that many. */
// GLOBAL: XVT 0x60F1D4
int g_generated_vertex_normal_count = 0;
#endif
/* 1 once opt_model_append_converted_faces_for_node has reached the face node it
 * merges faces into; opt_model_append_converted_faces_for_current_mesh sets it to 0
 * first. */
// GLOBAL: XVT 0x5272BC
static int g_opt_convert_target_face_found = 0;
/* The OPT_FACEGROUP node the legacy converter last passed, whose children
 * opt_model_append_converted_faces_for_current_mesh searches for faces to merge. */
// GLOBAL: XVT 0x60F1C8
static struct opt_node *g_opt_convert_source_mesh_node = NULL;
/* The merged OPT_VERTNORMALS node the legacy converter built for the current
 * root; NULL before it. opt_model_convert_legacy_model_to_optimized clears it before
 * each root. */
// GLOBAL: XVT 0x60F1CC
static struct opt_node *g_opt_convert_vertex_normal_node = NULL;
/* The texture the legacy converter last passed, directly or through an
 * OPT_NODEREF; faces merge only with faces under the same texture. */
// GLOBAL: XVT 0x60F1DC
static struct opt_node *g_opt_convert_source_texture_node = NULL;
/* The merged OPT_TEXCOORDS node the legacy converter built for the current
 * root; NULL before it. opt_model_convert_legacy_model_to_optimized clears it before
 * each root. */
// GLOBAL: XVT 0x60F1F4
static struct opt_node *g_opt_convert_tex_coord_node = NULL;
/* The texture opt_model_append_converted_faces_for_node last passed while it
 * searches for faces to merge. */
// GLOBAL: XVT 0x60F1F8
static struct opt_node *g_opt_convert_face_texture_node = NULL;
/* The merged OPT_MESHVERTS node the legacy converter built for the current
 * root; NULL before it, and while NULL the next node with children builds the
 * merged nodes. opt_model_convert_legacy_model_to_optimized clears it before each
 * root. */
// GLOBAL: XVT 0x60F200
static struct opt_node *g_opt_convert_vertex_node = NULL;

/* Loads a model file and returns the Memory handle of its runtime copy
 * (opt_model_create_runtime_handle), or 0. The modern build returns 0 for a NULL
 * name or one of 257 or more characters, and when opt_model_load_file_to_handle
 * fails; it registers the copy with xvt_render_assets_register_opt. The original
 * build loads the OPT file when it opens. When it does not, it opens the same
 * name with ".iv" in place of everything from the first '.', checks the
 * "#Inventor" header and its format word, imports an "ascii" file with
 * opt_model_load_inventor_ascii_to_handle (a "binary" one with
 * opt_model_load_inventor_binary_to_handle, which gives 0, and packing then locks
 * handle 0), packs it, saves the packed model under the OPT name and returns
 * its runtime copy. It returns 0 when the .iv file does not open or its header
 * is wrong; only a wrong format word closes the file first. Both builds pulse
 * the loading screen before and after building the runtime copy of an OPT
 * file. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x411E00
uint16_t opt_model_load_handle(const char *model_filename)
{
#ifdef XVT_MODERN
	char file_name[257];

	if (!model_filename || strlen(model_filename) >= sizeof(file_name)) {
		return 0;
	}
	strcpy(file_name, model_filename);
	uint16_t file_handle = opt_model_load_file_to_handle(file_name);
	if (!file_handle) {
		return 0;
	}
	flight_loading_pulse_and_draw_progress_screen();
	uint16_t runtime_handle = opt_model_create_runtime_handle(file_handle);
	xvt_render_assets_register_opt(runtime_handle, model_filename);
	flight_loading_pulse_and_draw_progress_screen();
	return runtime_handle;
#else
	strcpy(g_opt_model_load_scratch_buffer, model_filename);
	fe_disk_io_open_global_stream(g_opt_model_load_scratch_buffer,
				      g_file_mode_read_binary, 0, 0);
	if (g_stream == NULL) {
		int extension_index;
		for (extension_index = 0;
		     g_opt_model_load_scratch_buffer[extension_index] != '.';
		     ++extension_index) {
		}
		g_opt_model_load_scratch_buffer[extension_index] = '\0';
		strcat(g_opt_model_load_scratch_buffer, ".iv");
		fe_disk_io_open_global_stream(g_opt_model_load_scratch_buffer,
					      g_file_mode_read_binary, 1, 0);
		xvt_file *stream = (xvt_file *)g_stream;
		if (stream == NULL) {
			return 0;
		}
		if (FILE_SCANF(stream, "%256s",
			       g_opt_model_load_scratch_buffer) != 1) {
			return 0;
		}
		int compare_result =
			_strnicmp(g_opt_model_load_scratch_buffer, "#inventor",
				  sizeof("#inventor") - 1);
		if (compare_result != 0) {
			return 0;
		}
		if (FILE_SCANF(stream, " %256s",
			       g_opt_model_load_scratch_buffer) != 1) {
			return 0;
		}
		if (FILE_SCANF(stream, " %256s",
			       g_opt_model_load_scratch_buffer) != 1) {
			return 0;
		}

		compare_result = _strnicmp(g_opt_model_load_scratch_buffer,
					   "ascii", sizeof("ascii") - 1);
		uint16_t imported_handle;
		if (compare_result == 0) {
			imported_handle =
				opt_model_load_inventor_ascii_to_handle(stream);
		} else {
			compare_result =
				_strnicmp(g_opt_model_load_scratch_buffer,
					  "binary", sizeof("binary") - 1);
			if (compare_result == 0) {
				imported_handle =
					opt_model_load_inventor_binary_to_handle(
						stream);
			} else {
				FILE_RAW_CLOSE(stream);
				return 0;
			}
		}

		FILE_RAW_CLOSE(stream);
		uint16_t packed_handle =
			opt_model_convert_imported_handle_to_packed(
				imported_handle);
		strcpy(g_opt_model_load_scratch_buffer, model_filename);
		opt_model_save_handle_to_file(g_opt_model_load_scratch_buffer,
					      packed_handle);
		return opt_model_create_runtime_handle(packed_handle);
	}

	FILE_RAW_CLOSE((xvt_file *)g_stream);
	uint16_t file_handle =
		opt_model_load_file_to_handle(g_opt_model_load_scratch_buffer);
	flight_loading_pulse_and_draw_progress_screen();
	uint16_t runtime_handle = opt_model_create_runtime_handle(file_handle);
	flight_loading_pulse_and_draw_progress_screen();
	return runtime_handle;
#endif
}

#ifndef XVT_MODERN
/* Returns 0: binary Inventor files are not read. Only the original build calls
 * this. */
// FUNCTION: XVT 0x412030
uint16_t opt_model_load_inventor_binary_to_handle(const xvt_file *stream)
{
	(void)stream;

	return 0;
}

/* Imports the rest of an ASCII Inventor file and returns the Memory handle of
 * the model. It parses every top-level node once with
 * opt_model_parse_inventor_ascii_node to count the roots and bytes, seeks back,
 * then parses them again into one block: an optimized_poly_object (self_marker its
 * address, reserved the handle), the root table, then the nodes. Does not check
 * the allocation. Only the original build calls this. */
// FUNCTION: XVT 0x412040
uint16_t opt_model_load_inventor_ascii_to_handle(xvt_file *stream)
{

	long stream_start_offset = FILE_RAW_TELL(stream);
	int root_node_count = 0;
	int node_payload_size = 0;
	int parsed_node_size;
	while (1) {
		parsed_node_size =
			opt_model_parse_inventor_ascii_node(stream, NULL, NULL);
		if (parsed_node_size == -1) {
			break;
		}
		if (parsed_node_size != 0) {
			node_payload_size += parsed_node_size;
			++root_node_count;
		}
	}

	int root_pointer_offset = 0;
	FILE_RAW_SEEK(stream, stream_start_offset, SEEK_SET);
	struct optimized_poly_object *model;
	uint16_t handle = memory_alloc_handle(
		sizeof(*model) + root_node_count * sizeof(struct opt_node *) +
			node_payload_size,
		0);
	model = (struct optimized_poly_object *)memory_get_handle_block(handle);
	char *node_write_cursor = (char *)model + sizeof(*model);
	model->root_node_count = root_node_count;
	int parsed_root_count = 0;
	model->self_marker = model;
	model->reserved = handle;
	model->root_nodes = (struct opt_node **)node_write_cursor;
	node_write_cursor += root_node_count * sizeof(struct opt_node *);

	for (; parsed_root_count < root_node_count;) {
		parsed_node_size = opt_model_parse_inventor_ascii_node(
			stream, node_write_cursor,
			(struct opt_node **)((char *)model->root_nodes +
					     root_pointer_offset));
		if (parsed_node_size != 0) {
			root_pointer_offset += sizeof(struct opt_node *);
			node_write_cursor += parsed_node_size;
			++parsed_root_count;
		}
	}

	memory_handle_block_done_stub(handle);
	return handle;
}

/* Reads one node of an ASCII Inventor file and returns its size in bytes, or -1
 * when no word is left. With node_storage NULL it only measures; otherwise it
 * writes there and stores the node's address in *out_node. A node starts with a
 * word naming a row of g_inventor_node_defs (a word matches when it is the row's
 * name or its start, ignoring case); "DEF name" first stores the name and makes
 * it the node's pName. The opt_node takes the row index as its nodeType and an
 * inventor_field_record per field as its payload, with payload_count the field
 * count. Inside the braces it reads fields in any order, at most the field
 * count of them, until '}' or a word that names none; fills the fields left out
 * from their defaults; then reads child nodes up to '}' in two passes, counting
 * first. A "use" node takes its quoted or bare name as its one string record
 * and has no braces or children. An unknown node type prints a message, skips a
 * following block up to its first '}', and returns 0, or the size of a DEF name
 * before it. A word starting with '#' skips the rest of its line and is then
 * treated as an unknown type. On a read error it prints a message and returns
 * the bytes counted so far. Writes g_inventor_field_seen and
 * g_opt_model_load_scratch_buffer. Only the original build calls this. */
// FUNCTION: XVT 0x412120
int opt_model_parse_inventor_ascii_node(xvt_file *stream, char *node_storage,
					struct opt_node **out_node)
{
	char *cursor = node_storage;
	if (cursor != NULL) {
		*out_node = (struct opt_node *)cursor;
	}
	int total_size = 0;
	if (FILE_SCANF(stream, " %256s", g_opt_model_load_scratch_buffer) !=
	    1) {
		return -1;
	}
	int compare_result = _strnicmp(g_opt_model_load_scratch_buffer, "#", 1);
	if (compare_result == 0) {
		inventor_ascii_skip_to_end_of_line(stream);
	}

	const struct inventor_node_def *const *node_def_slot;
	int node_type;
	for (node_type = 0; node_type < (int)(sizeof(g_inventor_node_defs) /
					      sizeof(g_inventor_node_defs[0]));
	     ++node_type) {
		node_def_slot = &g_inventor_node_defs[node_type];
		compare_result =
			_strnicmp(g_opt_model_load_scratch_buffer,
				  (*node_def_slot)->node_name,
				  strlen(g_opt_model_load_scratch_buffer));
		if (compare_result == 0) {
			break;
		}
	}

	char *node_name = NULL;
	int parsed_size;
	if (node_type == OPT_DEF) {
		if (FILE_SCANF(stream, " %256s",
			       g_opt_model_load_scratch_buffer) != 1) {
			printf("READ NODE ERROR!\n");
			return 0;
		}
		parsed_size = strlen(g_opt_model_load_scratch_buffer) + 1;
		total_size = parsed_size;
		if (cursor != NULL) {
			strcpy(cursor, g_opt_model_load_scratch_buffer);
			node_name = cursor;
			cursor += parsed_size;
			*out_node = (struct opt_node *)cursor;
		}
		if (FILE_SCANF(stream, " %256s",
			       g_opt_model_load_scratch_buffer) != 1) {
			printf("READ NODE ERROR!\n");
			return parsed_size;
		}
		for (node_type = 0;
		     node_type < (int)(sizeof(g_inventor_node_defs) /
				       sizeof(g_inventor_node_defs[0]));
		     ++node_type) {
			node_def_slot = &g_inventor_node_defs[node_type];
			compare_result = _strnicmp(
				g_opt_model_load_scratch_buffer,
				(*node_def_slot)->node_name,
				strlen(g_opt_model_load_scratch_buffer));
			if (compare_result == 0) {
				break;
			}
		}
	}

	int item_count;
	/* Besides holding each parsed integer, integer_value counts the floats
	 * of a matrix or rotation item and indexes an enum's name table while a
	 * name is looked up. */
	int integer_value;
	float float_value;
	float component0;
	float component1;
	float component2;
	if (node_type < (int)(sizeof(g_inventor_node_defs) /
			      sizeof(g_inventor_node_defs[0]))) {
		struct inventor_field_record *field_records = NULL;
		struct opt_node *node;
		if (cursor != NULL) {
			node = (struct opt_node *)cursor;
			node->p_name = node_name;
			node->node_type = (opt_node_type)node_type;
			cursor += sizeof(*node);
			node->payload_count = (*node_def_slot)->field_count;
		}
		total_size += sizeof(struct opt_node);
		if (cursor != NULL) {
			field_records = (struct inventor_field_record *)cursor;
			node->payload = field_records;
			cursor += (*node_def_slot)->field_count *
				  sizeof(*field_records);
		}
		total_size += (*node_def_slot)->field_count *
			      sizeof(struct inventor_field_record);

		char character;
		if (node_type != OPT_NODEREF) {
			inventor_ascii_skip_past_open_brace(stream);
			int field_index;
			for (field_index = 0;
			     field_index < (*node_def_slot)->field_count;
			     ++field_index) {
				g_inventor_field_seen[field_index] = 0;
			}

			long rewind_position;
			for (int field_scan_index = 0;
			     field_scan_index < (*node_def_slot)->field_count;
			     ++field_scan_index) {
				if (inventor_ascii_peek_next_is_close_brace(
					    stream) != 0) {
					break;
				}
				rewind_position = FILE_RAW_TELL(stream);
				if (FILE_SCANF(
					    stream, " %256s",
					    g_opt_model_load_scratch_buffer) !=
				    1) {
					printf("READ NODE ERROR!\n");
					return total_size;
				}
				for (field_index = 0;
				     field_index <
				     (*node_def_slot)->field_count;
				     ++field_index) {
					compare_result = _strnicmp(
						g_opt_model_load_scratch_buffer,
						(*node_def_slot)
							->field_defs
								[field_index]
							->field_name,
						strlen(g_opt_model_load_scratch_buffer));
					if (compare_result == 0) {
						break;
					}
				}
				if (field_index ==
				    (*node_def_slot)->field_count) {
					FILE_RAW_SEEK(stream, rewind_position,
						      SEEK_SET);
					break;
				}

				g_inventor_field_seen[field_index] = 1;
				struct inventor_field_record *field_record =
					field_records != NULL
						? &field_records[field_index]
						: NULL;
				switch ((*node_def_slot)
						->field_defs[field_index]
						->field_type) {
				case INVENTOR_FIELD_STRING:
					if (inventor_ascii_peek_next_is_quote(
						    stream) != 0) {
						inventor_ascii_skip_past_quote(
							stream);
						if (cursor != NULL) {
							field_record->data =
								cursor;
							field_record
								->item_count =
								1;
							field_record
								->field_type =
								INVENTOR_FIELD_STRING;
						}
						while (1) {
							character =
								(char)FILE_GETC(
									stream);
							if (character ==
							    (char)EOF) {
								printf("READ NODE ERROR!\n");
								return total_size;
							}
							++total_size;
							if (character == '"') {
								break;
							}
							if (cursor != NULL) {
								*cursor++ =
									character;
							}
						}
						if (cursor != NULL) {
							*cursor++ = '\0';
						}
					} else if (cursor != NULL) {
						field_record->data = cursor;
						field_record->item_count = 1;
						field_record->field_type =
							INVENTOR_FIELD_STRING;
						if (FILE_SCANF(stream, " %s",
							       cursor) != 1) {
							printf("READ NODE ERROR!\n");
							return total_size;
						}
						parsed_size =
							strlen(cursor) + 1;
						cursor += parsed_size;
						total_size += parsed_size;
					} else {
						if (FILE_SCANF(
							    stream, " %256s",
							    g_opt_model_load_scratch_buffer) !=
						    1) {
							printf("READ NODE ERROR!\n");
							return total_size;
						}
						total_size +=
							strlen(g_opt_model_load_scratch_buffer) +
							1;
					}
					break;

				case INVENTOR_FIELD_NODE:
					if (cursor != NULL) {
						parsed_size =
							opt_model_parse_inventor_ascii_node(
								stream, cursor,
								(struct
								 opt_node *
									 *)&field_record
									->data);
						cursor += parsed_size;
						field_record->item_count = 1;
						field_record->field_type =
							INVENTOR_FIELD_NODE;
						total_size += parsed_size;
					} else {
						total_size +=
							opt_model_parse_inventor_ascii_node(
								stream, NULL,
								NULL);
					}
					break;

				case INVENTOR_FIELD_NODE_LIST:
					if (inventor_ascii_peek_next_is_open_bracket(
						    stream) != 0) {
						inventor_ascii_skip_past_open_bracket(
							stream);
						rewind_position =
							FILE_RAW_TELL(stream);
						item_count = 0;
						while (inventor_ascii_peek_next_is_close_bracket(
							       stream) == 0) {
							parsed_size =
								opt_model_parse_inventor_ascii_node(
									stream,
									NULL,
									NULL);
							if (parsed_size != 0) {
								++item_count;
								inventor_ascii_skip_list_separator(
									stream);
							}
						}
						FILE_RAW_SEEK(stream,
							      rewind_position,
							      SEEK_SET);
						total_size +=
							item_count *
							sizeof(struct opt_node
								       *);
						if (cursor != NULL) {
							field_record->data =
								cursor;
							cursor +=
								item_count *
								sizeof(struct
								       opt_node
									       *);
							field_record
								->item_count =
								item_count;
							field_record
								->field_type =
								INVENTOR_FIELD_NODE_LIST;
						}
						int item_index = 0;
						while (inventor_ascii_peek_next_is_close_bracket(
							       stream) == 0) {
							if (cursor != NULL) {
								parsed_size = opt_model_parse_inventor_ascii_node(
									stream,
									cursor,
									&((struct
									   opt_node *
										   *)field_record
										  ->data)
										[item_index]);
								if (parsed_size !=
								    0) {
									cursor +=
										parsed_size;
									++item_index;
									total_size +=
										parsed_size;
									inventor_ascii_skip_list_separator(
										stream);
								}
							} else {
								parsed_size = opt_model_parse_inventor_ascii_node(
									stream,
									NULL,
									NULL);
								if (parsed_size !=
								    0) {
									total_size +=
										parsed_size;
									inventor_ascii_skip_list_separator(
										stream);
								}
							}
						}
						inventor_ascii_skip_past_close_bracket(
							stream);
					} else {
						total_size += sizeof(
							struct opt_node *);
						if (cursor != NULL) {
							field_record->data =
								cursor;
							cursor += sizeof(
								struct opt_node
									*);
							field_record
								->item_count =
								1;
							field_record
								->field_type =
								INVENTOR_FIELD_NODE_LIST;
							parsed_size = opt_model_parse_inventor_ascii_node(
								stream, cursor,
								&((struct
								   opt_node *
									   *)field_record
									  ->data)
									[0]);
							cursor += parsed_size;
							total_size +=
								parsed_size;
						} else {
							total_size +=
								opt_model_parse_inventor_ascii_node(
									stream,
									NULL,
									NULL);
						}
					}
					break;

				case INVENTOR_FIELD_INTEGER:
					parsed_size = FILE_SCANF(
						stream, " %li", &integer_value);
					if (parsed_size != 1) {
						printf("READ NODE ERROR!\n");
						return total_size;
					}
					if (cursor != NULL) {
						field_record->data = cursor;
						*(int *)cursor = integer_value;
						cursor += sizeof(integer_value);
						field_record->item_count = 1;
						field_record->field_type =
							INVENTOR_FIELD_INTEGER;
					}
					total_size += sizeof(integer_value);
					break;

				case INVENTOR_FIELD_INTEGER_LIST:
					if (inventor_ascii_peek_next_is_open_bracket(
						    stream) != 0) {
						inventor_ascii_skip_past_open_bracket(
							stream);
						if (cursor != NULL) {
							field_record->data =
								cursor;
						}
						item_count = 0;
						while (inventor_ascii_peek_next_is_close_bracket(
							       stream) == 0) {
							parsed_size = FILE_SCANF(
								stream, " %li",
								&integer_value);
							if (parsed_size != 1) {
								printf("READ NODE ERROR!\n");
								return total_size;
							}
							if (cursor != NULL) {
								*(int *)cursor =
									integer_value;
								cursor += sizeof(
									integer_value);
							}
							++item_count;
							total_size += sizeof(
								integer_value);
							inventor_ascii_skip_list_separator(
								stream);
						}
						if (cursor != NULL) {
							field_record
								->item_count =
								item_count;
							field_record
								->field_type =
								INVENTOR_FIELD_INTEGER_LIST;
						}
						inventor_ascii_skip_past_close_bracket(
							stream);
					} else {
						if (cursor != NULL) {
							field_record->data =
								cursor;
						}
						parsed_size = FILE_SCANF(
							stream, " %li",
							&integer_value);
						if (parsed_size != 1) {
							printf("READ NODE ERROR!\n");
							return total_size;
						}
						if (cursor != NULL) {
							*(int *)cursor =
								integer_value;
							cursor += sizeof(
								integer_value);
						}
						item_count = 1;
						total_size +=
							sizeof(integer_value);
						if (cursor != NULL) {
							field_record
								->item_count =
								item_count;
							field_record
								->field_type =
								INVENTOR_FIELD_INTEGER_LIST;
						}
					}
					break;

				case INVENTOR_FIELD_FLOAT_AS_INTEGER:
					if (FILE_SCANF(stream, " %e",
						       &float_value) != 1) {
						printf("READ NODE WARNING!\n");
					}
					if (cursor != NULL) {
						field_record->data = cursor;
						*(float *)cursor = float_value;
						cursor += sizeof(float_value);
						field_record->item_count = 0;
						field_record->field_type =
							INVENTOR_FIELD_INTEGER;
					}
					total_size += sizeof(float_value);
					break;

				case INVENTOR_FIELD_FLOAT:
					if (inventor_ascii_peek_next_is_open_bracket(
						    stream) != 0) {
						inventor_ascii_skip_past_open_bracket(
							stream);
						if (cursor != NULL) {
							field_record->data =
								cursor;
						}
						item_count = 0;
						while (inventor_ascii_peek_next_is_close_bracket(
							       stream) == 0) {
							if (FILE_SCANF(
								    stream,
								    " %e",
								    &float_value) !=
							    1) {
								printf("READ NODE WARNING!\n");
							}
							if (cursor != NULL) {
								*(float *)
									cursor =
									float_value;
								cursor += sizeof(
									float_value);
							}
							++item_count;
							total_size += sizeof(
								float_value);
							inventor_ascii_skip_list_separator(
								stream);
						}
						if (cursor != NULL) {
							field_record
								->item_count =
								item_count;
							field_record
								->field_type =
								INVENTOR_FIELD_FLOAT;
						}
						inventor_ascii_skip_past_close_bracket(
							stream);
					} else {
						if (cursor != NULL) {
							field_record->data =
								cursor;
						}
						if (FILE_SCANF(stream, " %e",
							       &float_value) !=
						    1) {
							printf("READ NODE WARNING!\n");
						}
						if (cursor != NULL) {
							*(float *)cursor =
								float_value;
							cursor += sizeof(
								float_value);
						}
						item_count = 1;
						total_size +=
							sizeof(float_value);
						if (cursor != NULL) {
							field_record
								->item_count =
								item_count;
							field_record
								->field_type =
								INVENTOR_FIELD_FLOAT;
						}
					}
					break;

				case INVENTOR_FIELD_BOOLEAN:
					if (FILE_SCANF(
						    stream, " %256s",
						    g_opt_model_load_scratch_buffer) !=
					    1) {
						printf("READ NODE ERROR!\n");
						return total_size;
					}
					/* Here character holds the parsed truth
					 * value, 1 or 0, not a character. */
					if (_strcmpi(
						    g_opt_model_load_scratch_buffer,
						    "true") == 0 ||
					    _strcmpi(
						    g_opt_model_load_scratch_buffer,
						    "1") == 0) {
						character = 1;
					} else if (
						_strcmpi(
							g_opt_model_load_scratch_buffer,
							"false") == 0 ||
						_strcmpi(
							g_opt_model_load_scratch_buffer,
							"0") == 0) {
						character = 0;
					} else {
						printf("READ NODE ERROR!\n");
						return total_size;
					}
					++total_size;
					if (cursor != NULL) {
						field_record->data = cursor;
						*cursor++ = character;
						field_record->item_count = 1;
						field_record->field_type =
							INVENTOR_FIELD_BOOLEAN;
					}
					break;

				case INVENTOR_FIELD_VECTOR3:
				case INVENTOR_FIELD_COLOR:
					if (FILE_SCANF(stream, " %e %e %e",
						       &component0, &component1,
						       &component2) != 3) {
						printf("READ NODE WARNING!\n");
					}
					if (cursor != NULL) {
						field_record->data = cursor;
						field_record->item_count = 1;
						field_record->field_type =
							(*node_def_slot)
								->field_defs
									[field_index]
								->field_type;
						((float *)cursor)[0] =
							component0;
						((float *)cursor)[1] =
							component1;
						((float *)cursor)[2] =
							component2;
						cursor += 3 * sizeof(float);
					}
					total_size += 3 * sizeof(float);
					break;

				case INVENTOR_FIELD_VECTOR3_LIST:
				case INVENTOR_FIELD_COLOR_LIST:
					if (inventor_ascii_peek_next_is_open_bracket(
						    stream) != 0) {
						inventor_ascii_skip_past_open_bracket(
							stream);
						if (cursor != NULL) {
							field_record->data =
								cursor;
						}
						item_count = 0;
						while (inventor_ascii_peek_next_is_close_bracket(
							       stream) == 0) {
							if (FILE_SCANF(
								    stream,
								    " %e %e %e",
								    &component0,
								    &component1,
								    &component2) !=
							    3) {
								printf("READ NODE WARNING!\n");
							}
							if (cursor != NULL) {
								((float *)
									 cursor)[0] =
									component0;
								((float *)
									 cursor)[1] =
									component1;
								((float *)
									 cursor)[2] =
									component2;
								cursor +=
									3 *
									sizeof(float);
							}
							++item_count;
							total_size +=
								3 *
								sizeof(float);
							inventor_ascii_skip_list_separator(
								stream);
						}
						if (cursor != NULL) {
							field_record
								->item_count =
								item_count;
							field_record
								->field_type =
								(*node_def_slot)
									->field_defs
										[field_index]
									->field_type;
						}
						inventor_ascii_skip_past_close_bracket(
							stream);
					} else {
						if (cursor != NULL) {
							field_record->data =
								cursor;
						}
						if (FILE_SCANF(
							    stream, " %e %e %e",
							    &component0,
							    &component1,
							    &component2) != 3) {
							printf("READ NODE WARNING!\n");
						}
						if (cursor != NULL) {
							((float *)cursor)[0] =
								component0;
							((float *)cursor)[1] =
								component1;
							((float *)cursor)[2] =
								component2;
							cursor += 3 *
								  sizeof(float);
						}
						item_count = 1;
						total_size += 3 * sizeof(float);
						if (cursor != NULL) {
							field_record
								->item_count =
								item_count;
							field_record
								->field_type =
								(*node_def_slot)
									->field_defs
										[field_index]
									->field_type;
						}
					}
					break;

				case INVENTOR_FIELD_MATRIX:
					if (cursor != NULL) {
						field_record->data = cursor;
						field_record->item_count = 1;
						field_record->field_type =
							(*node_def_slot)
								->field_defs
									[field_index]
								->field_type;
					}
					total_size += 16 * sizeof(float);
					for (item_count = 16; item_count > 0;
					     --item_count) {
						if (FILE_SCANF(stream, " %e",
							       &float_value) !=
						    1) {
							printf("READ NODE WARNING!\n");
						}
						if (cursor != NULL) {
							*(float *)cursor =
								float_value;
							cursor += sizeof(
								float_value);
						}
					}
					break;

				case INVENTOR_FIELD_MATRIX_LIST:
					if (inventor_ascii_peek_next_is_open_bracket(
						    stream) != 0) {
						inventor_ascii_skip_past_open_bracket(
							stream);
						if (cursor != NULL) {
							field_record->data =
								cursor;
						}
						item_count = 0;
						while (inventor_ascii_peek_next_is_close_bracket(
							       stream) == 0) {
							for (integer_value = 0;
							     integer_value < 16;
							     ++integer_value) {
								if (FILE_SCANF(
									    stream,
									    " %e",
									    &float_value) !=
								    1) {
									printf("READ NODE WARNING!\n");
								}
								if (cursor !=
								    NULL) {
									*(float *)
										cursor =
										float_value;
									cursor += sizeof(
										float_value);
								}
								total_size += sizeof(
									float_value);
							}
							++item_count;
							inventor_ascii_skip_list_separator(
								stream);
						}
						if (cursor != NULL) {
							field_record
								->item_count =
								item_count;
							field_record
								->field_type =
								(*node_def_slot)
									->field_defs
										[field_index]
									->field_type;
						}
						inventor_ascii_skip_past_close_bracket(
							stream);
					} else {
						if (cursor != NULL) {
							field_record->data =
								cursor;
						}
						for (integer_value = 0;
						     integer_value < 16;
						     ++integer_value) {
							if (FILE_SCANF(
								    stream,
								    " %e",
								    &float_value) !=
							    1) {
								printf("READ NODE WARNING!\n");
							}
							if (cursor != NULL) {
								*(float *)
									cursor =
									float_value;
								cursor += sizeof(
									float_value);
							}
							total_size += sizeof(
								float_value);
						}
						item_count = 1;
						if (cursor != NULL) {
							field_record
								->item_count =
								item_count;
							field_record
								->field_type =
								(*node_def_slot)
									->field_defs
										[field_index]
									->field_type;
						}
					}
					break;

				case INVENTOR_FIELD_ROTATION:
					if (cursor != NULL) {
						field_record->data = cursor;
						field_record->item_count = 1;
						field_record->field_type =
							(*node_def_slot)
								->field_defs
									[field_index]
								->field_type;
					}
					total_size += 4 * sizeof(float);
					for (item_count = 4; item_count > 0;
					     --item_count) {
						if (FILE_SCANF(stream, " %e",
							       &float_value) !=
						    1) {
							printf("READ NODE WARNING!\n");
						}
						if (cursor != NULL) {
							*(float *)cursor =
								float_value;
							cursor += sizeof(
								float_value);
						}
					}
					break;

				case INVENTOR_FIELD_ROTATION_LIST:
					if (inventor_ascii_peek_next_is_open_bracket(
						    stream) != 0) {
						inventor_ascii_skip_past_open_bracket(
							stream);
						if (cursor != NULL) {
							field_record->data =
								cursor;
						}
						item_count = 0;
						while (inventor_ascii_peek_next_is_close_bracket(
							       stream) == 0) {
							for (integer_value = 0;
							     integer_value < 4;
							     ++integer_value) {
								if (FILE_SCANF(
									    stream,
									    " %e",
									    &float_value) !=
								    1) {
									printf("READ NODE WARNING!\n");
								}
								if (cursor !=
								    NULL) {
									*(float *)
										cursor =
										float_value;
									cursor += sizeof(
										float_value);
								}
								total_size += sizeof(
									float_value);
							}
							++item_count;
							inventor_ascii_skip_list_separator(
								stream);
						}
						if (cursor != NULL) {
							field_record
								->item_count =
								item_count;
							field_record
								->field_type =
								(*node_def_slot)
									->field_defs
										[field_index]
									->field_type;
						}
						inventor_ascii_skip_past_close_bracket(
							stream);
					} else {
						if (cursor != NULL) {
							field_record->data =
								cursor;
						}
						for (integer_value = 0;
						     integer_value < 4;
						     ++integer_value) {
							if (FILE_SCANF(
								    stream,
								    " %e",
								    &float_value) !=
							    1) {
								printf("READ NODE WARNING!\n");
							}
							if (cursor != NULL) {
								*(float *)
									cursor =
									float_value;
								cursor += sizeof(
									float_value);
							}
							total_size += sizeof(
								float_value);
						}
						item_count = 1;
						if (cursor != NULL) {
							field_record
								->item_count =
								item_count;
							field_record
								->field_type =
								(*node_def_slot)
									->field_defs
										[field_index]
									->field_type;
						}
					}
					break;

				case INVENTOR_FIELD_VECTOR2:
					if (FILE_SCANF(stream, " %e %e",
						       &component0,
						       &component1) != 2) {
						printf("READ NODE WARNING!\n");
					}
					if (cursor != NULL) {
						field_record->data = cursor;
						field_record->item_count = 1;
						field_record->field_type =
							(*node_def_slot)
								->field_defs
									[field_index]
								->field_type;
						((float *)cursor)[0] =
							component0;
						((float *)cursor)[1] =
							component1;
						cursor += 2 * sizeof(float);
					}
					total_size += 2 * sizeof(float);
					break;

				case INVENTOR_FIELD_VECTOR2_LIST:
					if (inventor_ascii_peek_next_is_open_bracket(
						    stream) != 0) {
						inventor_ascii_skip_past_open_bracket(
							stream);
						if (cursor != NULL) {
							field_record->data =
								cursor;
						}
						item_count = 0;
						while (inventor_ascii_peek_next_is_close_bracket(
							       stream) == 0) {
							if (FILE_SCANF(
								    stream,
								    " %e %e",
								    &component0,
								    &component1) !=
							    2) {
								printf("READ NODE WARNING!\n");
							}
							if (cursor != NULL) {
								((float *)
									 cursor)[0] =
									component0;
								((float *)
									 cursor)[1] =
									component1;
								cursor +=
									2 *
									sizeof(float);
							}
							++item_count;
							total_size +=
								2 *
								sizeof(float);
							inventor_ascii_skip_list_separator(
								stream);
						}
						if (cursor != NULL) {
							field_record
								->item_count =
								item_count;
							field_record
								->field_type =
								(*node_def_slot)
									->field_defs
										[field_index]
									->field_type;
						}
						inventor_ascii_skip_past_close_bracket(
							stream);
					} else {
						if (cursor != NULL) {
							field_record->data =
								cursor;
						}
						if (FILE_SCANF(stream, " %e %e",
							       &component0,
							       &component1) !=
						    2) {
							printf("READ NODE WARNING!\n");
						}
						if (cursor != NULL) {
							((float *)cursor)[0] =
								component0;
							((float *)cursor)[1] =
								component1;
							cursor += 2 *
								  sizeof(float);
						}
						item_count = 1;
						total_size += 2 * sizeof(float);
						if (cursor != NULL) {
							field_record
								->item_count =
								item_count;
							field_record
								->field_type =
								(*node_def_slot)
									->field_defs
										[field_index]
									->field_type;
						}
					}
					break;

				case INVENTOR_FIELD_ENUM: {
					if (inventor_ascii_peek_next_is_close_brace(
						    stream) != 0) {
						strcpy(g_opt_model_load_scratch_buffer,
						       "DEFAULT");
					} else if (
						FILE_SCANF(
							stream, " %256s",
							g_opt_model_load_scratch_buffer) !=
						1) {
						printf("READ NODE ERROR!\n");
						return total_size;
					}
					const struct inventor_enum_def *enum_def =
						(*node_def_slot)
							->field_defs
								[field_index]
							->enum_def;
					const char *const *enum_value_names =
						enum_def->value_names;
					const int *enum_values =
						enum_def->values;
					for (integer_value = 0;
					     integer_value <
					     enum_def->value_count;
					     ++integer_value) {
						compare_result = _strcmpi(
							g_opt_model_load_scratch_buffer,
							enum_value_names
								[integer_value]);
						if (compare_result == 0) {
							break;
						}
					}
					if (integer_value ==
					    enum_def->value_count) {
						integer_value = atoi(
							g_opt_model_load_scratch_buffer);
					} else {
						integer_value = enum_values
							[integer_value];
					}
					if (cursor != NULL) {
						field_record->data = cursor;
						*(int *)cursor = integer_value;
						cursor += sizeof(integer_value);
						field_record->item_count = 1;
						field_record->field_type =
							INVENTOR_FIELD_ENUM;
					}
					total_size += sizeof(integer_value);
					break;
				}

				case INVENTOR_FIELD_ENUM_LIST: {
					const struct inventor_enum_def
						*enum_def;
					const char *const *enum_value_names;
					const int *enum_values;

					if (inventor_ascii_peek_next_is_open_bracket(
						    stream) != 0) {
						inventor_ascii_skip_past_open_bracket(
							stream);
						if (cursor != NULL) {
							field_record->data =
								cursor;
						}
						item_count = 0;
						while (inventor_ascii_peek_next_is_close_bracket(
							       stream) == 0) {
							if (FILE_SCANF(
								    stream,
								    " %256s",
								    g_opt_model_load_scratch_buffer) !=
							    1) {
								printf("READ NODE ERROR!\n");
								return total_size;
							}
							enum_def =
								(*node_def_slot)
									->field_defs
										[field_index]
									->enum_def;
							enum_value_names =
								enum_def->value_names;
							enum_values =
								enum_def->values;
							for (integer_value = 0;
							     integer_value <
							     enum_def->value_count;
							     ++integer_value) {
								compare_result = _strcmpi(
									g_opt_model_load_scratch_buffer,
									enum_value_names
										[integer_value]);
								if (compare_result ==
								    0) {
									break;
								}
							}
							if (integer_value ==
							    enum_def->value_count) {
								integer_value = atoi(
									g_opt_model_load_scratch_buffer);
							} else {
								integer_value = enum_values
									[integer_value];
							}
							if (cursor != NULL) {
								*(int *)cursor =
									integer_value;
								cursor += sizeof(
									integer_value);
							}
							total_size += sizeof(
								integer_value);
							++item_count;
							inventor_ascii_skip_list_separator(
								stream);
						}
						if (cursor != NULL) {
							field_record
								->item_count =
								item_count;
							field_record
								->field_type =
								(*node_def_slot)
									->field_defs
										[field_index]
									->field_type;
						}
						inventor_ascii_skip_past_close_bracket(
							stream);
					} else {
						if (cursor != NULL) {
							field_record->data =
								cursor;
						}
						if (FILE_SCANF(
							    stream, " %256s",
							    g_opt_model_load_scratch_buffer) !=
						    1) {
							printf("READ NODE ERROR!\n");
							return total_size;
						}
						enum_def =
							(*node_def_slot)
								->field_defs
									[field_index]
								->enum_def;
						enum_value_names =
							enum_def->value_names;
						enum_values = enum_def->values;
						for (integer_value = 0;
						     integer_value <
						     enum_def->value_count;
						     ++integer_value) {
							compare_result = _strcmpi(
								g_opt_model_load_scratch_buffer,
								enum_value_names
									[integer_value]);
							if (compare_result ==
							    0) {
								break;
							}
						}
						if (integer_value ==
						    enum_def->value_count) {
							integer_value = atoi(
								g_opt_model_load_scratch_buffer);
						} else {
							integer_value = enum_values
								[integer_value];
						}
						if (cursor != NULL) {
							*(int *)cursor =
								integer_value;
							cursor += sizeof(
								integer_value);
						}
						item_count = 1;
						total_size +=
							sizeof(integer_value);
						if (cursor != NULL) {
							field_record
								->item_count =
								item_count;
							field_record
								->field_type =
								(*node_def_slot)
									->field_defs
										[field_index]
									->field_type;
						}
					}
					break;
				}

				default:
					break;
				}
			}

			for (field_index = 0;
			     field_index < (*node_def_slot)->field_count;
			     ++field_index) {
				if (g_inventor_field_seen[field_index] == 0) {
					if (cursor != NULL) {
						const struct inventor_field_def *field_def =
							(*node_def_slot)
								->field_defs
									[field_index];
						field_records[field_index] =
							*field_def
								 ->default_record;
						field_records[field_index]
							.data = cursor;
						memcpy(cursor,
						       field_def->default_data,
						       field_def
							       ->default_data_size);
						cursor +=
							field_def
								->default_data_size;
					}
					total_size +=
						(*node_def_slot)
							->field_defs
								[field_index]
							->default_data_size;
				}
			}

			int child_count = 0;
			rewind_position = FILE_RAW_TELL(stream);
			while (inventor_ascii_peek_next_is_close_brace(
				       stream) == 0) {
				if (opt_model_parse_inventor_ascii_node(
					    stream, NULL, NULL) != 0) {
					++child_count;
				}
			}
			FILE_RAW_SEEK(stream, rewind_position, SEEK_SET);
			if (child_count != 0) {
				total_size +=
					child_count * sizeof(struct opt_node *);
				struct opt_node **child_slots = NULL;
				if (cursor != NULL) {
					node->p_children =
						(struct opt_node **)cursor;
					node->child_count = child_count;
					child_slots = node->p_children;
					cursor += child_count *
						  sizeof(struct opt_node *);
				}
				while (inventor_ascii_peek_next_is_close_brace(
					       stream) == 0) {
					if (cursor != NULL) {
						parsed_size =
							opt_model_parse_inventor_ascii_node(
								stream, cursor,
								child_slots);
						if (parsed_size != 0) {
							cursor += parsed_size;
							++child_slots;
							total_size +=
								parsed_size;
						}
					} else {
						parsed_size =
							opt_model_parse_inventor_ascii_node(
								stream, NULL,
								NULL);
						if (parsed_size != 0) {
							total_size +=
								parsed_size;
						}
					}
				}
			} else if (cursor != NULL) {
				node->p_children = NULL;
				node->child_count = 0;
			}
			inventor_ascii_skip_past_close_brace(stream);
			return total_size;
		} else {
			if (inventor_ascii_peek_next_is_quote(stream) != 0) {
				inventor_ascii_skip_past_quote(stream);
				if (cursor != NULL) {
					field_records[0].data = cursor;
					field_records[0].item_count = 1;
					field_records[0].field_type =
						INVENTOR_FIELD_STRING;
				}
				while (1) {
					character = (char)FILE_GETC(stream);
					if (character == (char)EOF) {
						break;
					}
					++total_size;
					if (character == '"') {
						if (cursor != NULL) {
							*cursor++ = '\0';
							node->p_children = NULL;
							node->child_count = 0;
						}
						return total_size;
					}
					if (cursor != NULL) {
						*cursor++ = character;
					}
				}
				printf("READ NODE ERROR!\n");
				return total_size;
			} else {
				if (FILE_SCANF(
					    stream, " %256s",
					    g_opt_model_load_scratch_buffer) !=
				    1) {
					printf("READ NODE ERROR!\n");
					return total_size;
				}
				parsed_size =
					strlen(g_opt_model_load_scratch_buffer) +
					1;
				if (cursor != NULL) {
					strcpy(cursor,
					       g_opt_model_load_scratch_buffer);
					field_records[0].data = cursor;
					cursor += parsed_size;
					field_records[0].item_count = 1;
					field_records[0].field_type =
						INVENTOR_FIELD_STRING;
				}
				total_size += parsed_size;
			}
			if (cursor != NULL) {
				node->p_children = NULL;
				node->child_count = 0;
			}
			return total_size;
		}
	}

	printf("Node Type Not Supported, ignored\n");
	if (inventor_ascii_peek_next_is_open_brace(stream) != 0) {
		inventor_ascii_skip_past_open_brace(stream);
		inventor_ascii_skip_past_close_brace(stream);
	}
	return total_size;
}
#endif

/* Adds translation, three floats, to every vertex of each OPT_MESHVERTS node at
 * or below node, following OPT_NODEREF links and stopping at one that does not
 * resolve; a node reached through two links moves twice. Only
 * opt_model_translate_vertices calls this, and nothing calls that. */
// FUNCTION: XVT 0x42ABA0
void opt_model_translate_node_vertices_recursive(
	const struct opt_node *node, struct optimized_poly_object *model,
	const float *translation)
{
	if (node != NULL) {
		while (node->node_type == OPT_NODEREF) {
			node = opt_model_resolve_node_ref(
				model, (const char *)node->payload);
			if (node == NULL) {
				return;
			}
		}

		const float *delta;
		if (node->node_type == OPT_MESHVERTS) {
			int vertex_count = node->payload_count;
			float *vertex = node->payload;
			delta = translation;
			if (vertex_count > 0) {
				do {
					vertex[0] += delta[0];
					vertex[1] += delta[1];
					vertex[2] += delta[2];
					vertex += 3;
					--vertex_count;
				} while (vertex_count != 0);
			}
		} else {
			delta = translation;
		}

		int child_index = 0;
		if (node->child_count > 0) {
			do {
				opt_model_translate_node_vertices_recursive(
					node->p_children[child_index], model,
					delta);
				++child_index;
			} while (node->child_count > child_index);
		}
	}
}

/* Runs opt_model_translate_node_vertices_recursive on each root of model. Nothing
 * calls this. */
// FUNCTION: XVT 0x42ACD0
void opt_model_translate_vertices(struct optimized_poly_object *model,
				  const float *translation)
{
	int root_index = 0;
	if (model->root_node_count > 0) {
		do {
			opt_model_translate_node_vertices_recursive(
				model->root_nodes[root_index], model,
				translation);
			++root_index;
		} while (model->root_node_count > root_index);
	}
}

#ifndef XVT_MODERN
/* Moves the pointers of an imported Inventor model by the distance its block
 * moved since self_marker was set: self_marker itself, the root table, each root
 * and, through opt_model_relocate_node_pointers_recursive, everything below. NULL
 * pointers stay NULL. Only the original build calls this, from
 * opt_model_convert_imported_handle_to_packed. */
// FUNCTION: XVT 0x471F00
void opt_model_relocate_loaded_pointers(struct optimized_poly_object *model)
{

	xvt_opt_value relocation_delta =
		(uint8_t *)model - (uint8_t *)model->self_marker;
	model->self_marker = (uint8_t *)model->self_marker + relocation_delta;
	if (model->root_nodes != NULL) {
		model->root_nodes =
			(struct opt_node **)((uint8_t *)model->root_nodes +
					     relocation_delta);
		for (int root_index = 0; root_index < model->root_node_count;
		     ++root_index) {
			struct opt_node **root_slot =
				&model->root_nodes[root_index];
			if (*root_slot != NULL) {
				*root_slot = (struct opt_node
						      *)((uint8_t *)*root_slot +
							 relocation_delta);
				opt_model_relocate_node_pointers_recursive(
					model->root_nodes[root_index],
					relocation_delta);
			}
		}
	}
}

/* Adds relocation_delta to node's name, payload and child table pointers, to the
 * data pointer of each of its payload_count InventorFieldRecords and to each
 * child pointer, then does the same below each child. NULL pointers stay NULL.
 * Only the original build calls this. */
// FUNCTION: XVT 0x471F60
void opt_model_relocate_node_pointers_recursive(struct opt_node *node,
						xvt_opt_value relocation_delta)
{

	if (node->p_name != NULL) {
		node->p_name += relocation_delta;
	}
	if (node->payload != NULL) {
		node->payload = (uint8_t *)node->payload + relocation_delta;
		struct inventor_field_record *records =
			(struct inventor_field_record *)node->payload;
		for (int param_index = 0; param_index < node->payload_count;
		     ++param_index) {
			if (records->data != NULL) {
				records->data = (uint8_t *)records->data +
						relocation_delta;
			}
			++records;
		}
	}
	if (node->p_children != NULL) {
		node->p_children =
			(struct opt_node **)((uint8_t *)node->p_children +
					     relocation_delta);
		for (int child_index = 0; child_index < node->child_count;
		     ++child_index) {
			struct opt_node **child_slot =
				&node->p_children[child_index];
			if (*child_slot != NULL) {
				*child_slot =
					(struct opt_node
						 *)((uint8_t *)*child_slot +
						    relocation_delta);
				opt_model_relocate_node_pointers_recursive(
					node->p_children[child_index],
					relocation_delta);
			}
		}
	}
}
#endif

/* Moves every pointer inside a packed model by the distance its block moved
 * since self_marker was recorded, and records the new address. The modern build
 * calls xvt_opt_relocate. The original build moves self_marker, the root table
 * and each root, then runs opt_model_adjust_optimized_node_pointers on each root.
 * Most callers call it only when self_marker is not the block's address. */
// FUNCTION: XVT 0x471FF0
void opt_model_adjust_optimized_poly_object_pointers(
	struct optimized_poly_object *model)
{
#ifdef XVT_MODERN
	xvt_opt_relocate(model);
#else

	xvt_opt_value relocation_delta =
		(uint8_t *)model - (uint8_t *)model->self_marker;
	model->self_marker = (uint8_t *)model->self_marker + relocation_delta;
	if (model->root_nodes != NULL) {
		model->root_nodes =
			(struct opt_node **)((uint8_t *)model->root_nodes +
					     relocation_delta);
		for (int root_index = 0; root_index < model->root_node_count;
		     ++root_index) {
			struct opt_node **root_slot =
				&model->root_nodes[root_index];
			if (*root_slot != NULL) {
				*root_slot = (struct opt_node
						      *)((uint8_t *)*root_slot +
							 relocation_delta);
				opt_model_adjust_optimized_node_pointers(
					model->root_nodes[root_index],
					relocation_delta);
			}
		}
	}

#endif
}

/* Adds relocation_delta to node's name, payload and child table pointers and to
 * each child pointer, and to the palette pointer of an OPT_TEXTURE node whose
 * inline_palette_count is 0, then does the same below each child. NULL pointers
 * stay NULL. The modern arm calls xvt_opt_relocate_node, but only the original
 * build calls this. */
// FUNCTION: XVT 0x472050
void opt_model_adjust_optimized_node_pointers(struct opt_node *node,
					      xvt_opt_value relocation_delta)
{
#ifdef XVT_MODERN
	xvt_opt_relocate_node(node, relocation_delta);
#else

	if (node->p_name != NULL) {
		node->p_name += relocation_delta;
	}
	if (node->payload != NULL) {
		node->payload = (uint8_t *)node->payload + relocation_delta;
	}
	if (node->node_type == OPT_TEXTURE) {
		struct opt_texture_data *texture_data =
			(struct opt_texture_data *)node->payload;
		if (texture_data->inline_palette_count == 0) {
			texture_data->palette =
				(uint16_t *)((uint8_t *)texture_data->palette +
					     relocation_delta);
		}
	}
	if (node->p_children != NULL) {
		node->p_children =
			(struct opt_node **)((uint8_t *)node->p_children +
					     relocation_delta);
		for (int child_index = 0; child_index < node->child_count;
		     ++child_index) {
			struct opt_node **child_slot =
				&node->p_children[child_index];
			if (*child_slot != NULL) {
				*child_slot =
					(struct opt_node
						 *)((uint8_t *)*child_slot +
						    relocation_delta);
				opt_model_adjust_optimized_node_pointers(
					node->p_children[child_index],
					relocation_delta);
			}
		}
	}

#endif
}

/* Loads an OPT file into g_load_opt_buf_handle, converts a version 0 or 1 model to
 * version 2 with opt_model_convert_legacy_model_to_optimized, and returns that
 * handle; it is reused by the next load. Then it measures each root with
 * opt_model_measure_node_and_raise_capacities, which raises the renderer's capacity
 * counts, after setting g_cur_mesh_vertices, g_cur_mesh_tex_coords,
 * g_cur_vert_normals, g_model_node_walk_unused_scratch2 and g_cur_mesh_materials to
 * NULL and g_cur_vertex_count to 0. The file starts with a 4-byte word: a
 * positive one is the body size of version 0; -1 or -2 (versions 1 and 2) is
 * followed by the size. Sets g_opt_source_is_version0. The modern build decodes
 * the file with xvt_opt_load, keeps the result as g_load_opt_buf_handle in place of
 * the one before, and ends the program through xvt_storage_fatal when that
 * fails. The original build returns 0 when the file does not open and regrows
 * g_load_opt_buf_handle when the body is bigger; running out of memory ends the
 * program through fe_disk_io_fatal_error. For a version 0 or 1 file it also writes
 * the body as read to the name with its last character changed to '0' or '1',
 * and, when the original name is writable, writes the converted model over it
 * with the marker -1 (from version 0) or -2 (from version 1). */
// FUNCTION: XVT 0x4742A0
uint16_t opt_model_load_file_to_handle(char *filename)
{
#ifdef XVT_MODERN
	unsigned int native_size = 0;
	int version = 0;
	uint16_t handle = xvt_opt_load(filename, &version, &native_size);
	if (!handle) {
		xvt_storage_fatal("Invalid required OPT model", 1);
		return 0;
	}
	if (g_load_opt_buf_handle) {
		memory_free_handle(g_load_opt_buf_handle);
	}
	g_load_opt_buf_handle = handle;
	g_load_opt_buf_size = (int)native_size;
	g_opt_source_is_version0 = version == 0;
	if (version < 2) {
		native_size = opt_model_convert_legacy_model_to_optimized(
			native_size);
		if (!native_size) {
			return 0;
		}
		handle = g_load_opt_buf_handle;
	}
	struct optimized_poly_object *model = memory_get_handle_block(handle);
	struct scene_mesh mesh_state;
	memset(&mesh_state, 0, sizeof(mesh_state));
	g_cur_mesh_vertices = NULL;
	g_cur_mesh_tex_coords = NULL;
	g_cur_vert_normals = NULL;
	g_model_node_walk_unused_scratch2 = NULL;
	g_cur_mesh_materials = NULL;
	g_cur_vertex_count = 0;
	for (int root_index = 0; root_index < model->root_node_count;
	     ++root_index) {
		opt_model_measure_node_and_raise_capacities(
			model->root_nodes[root_index], &mesh_state);
	}
	XVT_LOG_DEBUG(
		"models.file_loaded file=\"%s\" version=%d bytes=%u roots=%d edges=%d vertices=%d",
		filename, version, native_size, model->root_node_count,
		g_scene_edge_flags_capacity, g_vertex_remap_capacity);
	if (model->root_node_count == 0) {
		XVT_LOG_ERROR("models.no_parts file=\"%s\"", filename);
	}
	if (model->root_node_count > 51 ||
	    (model->root_node_count == 51 &&
	     model->root_nodes[0]->node_type != OPT_TEXTURE)) {
		XVT_LOG_WARN("models.parts_capped file=\"%s\" roots=%d",
			     filename, model->root_node_count);
	}
	memory_handle_block_done_stub(handle);
	return handle;
#else

	fe_disk_io_open_global_stream(filename, g_file_mode_read_binary, 1, 0);
	xvt_file *stream = g_stream;
	if (stream == NULL) {
		return 0;
	}
	g_opt_source_is_version0 = 0;
	int file_version;
	FILE_RAW_READ(&file_version, 1, sizeof(file_version), stream);
	size_t serialized_size;
	if (file_version > 0) {
		serialized_size = (size_t)file_version;
		file_version = 0;
		g_opt_source_is_version0 = 1;
	} else {
		file_version = -file_version;
		if (file_version == 1) {
			file_version = 0;
		}
		g_opt_source_is_version0 = 0;
		FILE_RAW_READ(&serialized_size, 1, sizeof(serialized_size),
			      stream);
	}
	if ((int)serialized_size > g_load_opt_buf_size &&
	    g_load_opt_buf_handle != 0) {
		unsigned int old_handle = g_load_opt_buf_handle;
		memory_free_handle((uint16_t)old_handle);
		g_load_opt_buf_handle = 0;
		g_load_opt_buf_size = 0;
	}
	if (g_load_opt_buf_handle == 0) {
		g_load_opt_buf_handle = memory_alloc_handle(serialized_size, 0);
		if (g_load_opt_buf_handle == 0) {
			fe_disk_io_fatal_error(
				FILE_ERROR_STR_NOT_ENOUGH_MEMORY);
		}
		g_load_opt_buf_size = (int)serialized_size;
	}
	uint16_t handle = g_load_opt_buf_handle;
	struct optimized_poly_object *model = memory_get_handle_block(handle);
	FILE_RAW_READ(model, 1, serialized_size, stream);
	FILE_RAW_CLOSE(stream);
	if (model->self_marker != model) {
		opt_model_adjust_optimized_poly_object_pointers(model);
	}

	if (file_version == 0) {
		char saved_version_char = filename[strlen(filename) - 1];
		if (g_opt_source_is_version0) {
			filename[strlen(filename) - 1] = '0';
		} else {
			filename[strlen(filename) - 1] = '1';
		}

		fe_disk_io_open_global_stream(filename, "wb", 0, 1);
		stream = g_stream;
		if (stream != NULL) {
			if (!g_opt_source_is_version0) {
				file_version = -1;
				FILE_RAW_WRITE(&file_version, 1,
					       sizeof(file_version), stream);
			}
			FILE_RAW_WRITE(&serialized_size, 1,
				       sizeof(serialized_size), stream);
			FILE_RAW_WRITE(model, 1, serialized_size, stream);
			FILE_RAW_CLOSE(stream);
		}

		filename[strlen(filename) - 1] = saved_version_char;
		memory_handle_block_done_stub(handle);
		serialized_size = opt_model_convert_legacy_model_to_optimized(
			(unsigned int)serialized_size);
		handle = g_load_opt_buf_handle;
		model = memory_get_handle_block(handle);
		if (model->self_marker != model) {
			opt_model_adjust_optimized_poly_object_pointers(model);
		}

		file_version = -1;
		if (!g_opt_source_is_version0) {
			file_version = -2;
		}
		if (_access(filename, 2) == 0) {
			fe_disk_io_open_global_stream(filename, "wb", 0, 1);
			stream = g_stream;
			if (stream != NULL) {
				FILE_RAW_WRITE(&file_version, 1,
					       sizeof(file_version), stream);
				FILE_RAW_WRITE(&serialized_size, 1,
					       sizeof(serialized_size), stream);
				FILE_RAW_WRITE(model, 1, serialized_size,
					       stream);
				FILE_RAW_CLOSE(stream);
			}
		}
		file_version = -file_version;
	}

	struct scene_mesh mesh_state;
	memset(&mesh_state, 0, sizeof(mesh_state));
	g_cur_mesh_vertices = NULL;
	g_cur_mesh_tex_coords = NULL;
	g_cur_vert_normals = NULL;
	g_model_node_walk_unused_scratch2 = NULL;
	g_cur_mesh_materials = NULL;
	g_cur_vertex_count = 0;
	serialized_size = sizeof(*model) +
			  sizeof(*model->root_nodes) * model->root_node_count;
	for (int root_index = 0; root_index < model->root_node_count;
	     ++root_index) {
		serialized_size += opt_model_measure_node_and_raise_capacities(
			model->root_nodes[root_index], &mesh_state);
	}
	memory_handle_block_done_stub(handle);
	return handle;

#endif
}

/* Converts the version 0 or 1 model of source_size bytes in g_load_opt_buf_handle
 * to version 2 in the same handle and returns the new size in bytes. It copies
 * the source into g_opt_convert_source_handle, regrowing that when smaller, makes
 * g_load_opt_buf_handle hold at least twice source_size, and rebuilds each root
 * there with opt_model_convert_legacy_node_to_optimized, setting
 * g_opt_convert_vertex_node, g_opt_convert_tex_coord_node and
 * g_opt_convert_vertex_normal_node to NULL before each. Sets g_cur_mesh_vertices,
 * g_cur_mesh_tex_coords, g_cur_vert_normals, g_model_node_walk_unused_scratch2 and
 * g_cur_mesh_materials to NULL and g_cur_vertex_count to 0 first. Running out of
 * memory ends the program through fe_disk_io_fatal_error. Does not check that the
 * result fits in twice the source size. */
// FUNCTION: XVT 0x474610
unsigned int
opt_model_convert_legacy_model_to_optimized(unsigned int source_size)
{
	if ((int)g_opt_convert_source_buf_size < (int)source_size &&
	    g_opt_convert_source_handle != 0) {
		memory_free_handle(g_opt_convert_source_handle);
		g_opt_convert_source_handle = 0;
		g_opt_convert_source_buf_size = 0;
	}
	if (g_opt_convert_source_handle == 0) {
		g_opt_convert_source_handle =
			memory_alloc_handle(source_size, 0);
		if (g_opt_convert_source_handle == 0) {
			fe_disk_io_fatal_error(
				FILE_ERROR_STR_NOT_ENOUGH_MEMORY);
		}
		g_opt_convert_source_buf_size = source_size;
	}
	/* Convert a legacy model stream into the optimized runtime representation. */
	struct optimized_poly_object *source_model =
		(struct optimized_poly_object *)memory_get_handle_block(
			g_opt_convert_source_handle);
	memcpy(source_model, memory_get_handle_block(g_load_opt_buf_handle),
	       source_size);
	if (source_model->self_marker != source_model) {
		opt_model_adjust_optimized_poly_object_pointers(source_model);
	}
	memory_handle_block_done_stub(g_load_opt_buf_handle);
	int destination_capacity = (int)(source_size * 2u);
	if (destination_capacity > g_load_opt_buf_size &&
	    g_load_opt_buf_handle != 0) {
		memory_free_handle(g_load_opt_buf_handle);
		g_load_opt_buf_handle = 0;
		g_load_opt_buf_size = 0;
	}
	if (g_load_opt_buf_handle == 0) {
		g_load_opt_buf_handle = memory_alloc_handle(
			(unsigned int)destination_capacity, 0);
		if (g_load_opt_buf_handle == 0) {
			fe_disk_io_fatal_error(
				FILE_ERROR_STR_NOT_ENOUGH_MEMORY);
		}
		g_load_opt_buf_size = destination_capacity;
	}
	struct optimized_poly_object *destination_model =
		(struct optimized_poly_object *)memory_get_handle_block(
			g_load_opt_buf_handle);
	memcpy(destination_model, source_model, sizeof(*destination_model));
	destination_model->self_marker = destination_model;
	destination_model->root_nodes =
		(struct opt_node **)((uint8_t *)destination_model +
				     sizeof(*destination_model));
	int root_node_count = source_model->root_node_count;
	uint8_t *destination_node =
		(uint8_t *)(destination_model->root_nodes + root_node_count);
	unsigned int serialized_size =
		(unsigned int)(sizeof(*destination_model) +
			       sizeof(struct opt_node *) *
				       (unsigned int)root_node_count);
	struct scene_mesh mesh_state;
	memset(&mesh_state, 0, sizeof(mesh_state));
	g_cur_mesh_vertices = NULL;
	g_cur_mesh_tex_coords = NULL;
	g_cur_vert_normals = NULL;
	g_model_node_walk_unused_scratch2 = NULL;
	g_cur_mesh_materials = NULL;
	g_cur_vertex_count = 0;
	destination_model->root_node_count = 0;
	int root_index = 0;
	if (root_node_count > 0) {
		do {
			++root_index;
			++destination_model->root_node_count;
			destination_model->root_nodes[root_index - 1] =
				(struct opt_node *)destination_node;
			g_opt_convert_vertex_node = NULL;
			g_opt_convert_tex_coord_node = NULL;
			g_opt_convert_vertex_normal_node = NULL;
			unsigned int node_size =
				opt_model_convert_legacy_node_to_optimized(
					destination_node,
					source_model
						->root_nodes[root_index - 1],
					source_model, destination_model,
					&mesh_state);
			destination_node += node_size;
			serialized_size += node_size;
		} while (root_node_count > root_index);
	}
	XVT_LOG_DEBUG(
		"models.converted roots=%d bytes=%u converted=%u capacity=%d",
		root_node_count, source_size, serialized_size,
		destination_capacity);
	if (serialized_size > (unsigned int)destination_capacity) {
		XVT_LOG_ERROR("models.convert_overflow bytes=%u capacity=%d",
			      serialized_size, destination_capacity);
	}
	return serialized_size;
}

/* Searches node and the nodes below it, depth first, for a texture whose own
 * palette holds the same 8192 bytes of RGB565 colors as texture_data does after
 * its first 4096 bytes, and returns the start of that palette, or NULL. When
 * stop_node is among a node's children, the search of that node ends there, but
 * the levels above go on to their later children. A texture with
 * inline_palette_count 0 counts only when its palette pointer is its own embedded
 * palette. A texture compared without a match passes texture_data 4096 bytes on
 * to its own children. */
// FUNCTION: XVT 0x474830
void *opt_model_find_shared_texture_data_in_node_before_target(
	const void *texture_data, const struct opt_node *node,
	const struct opt_node *stop_node)
{
	if (node == NULL) {
		return NULL;
	}

	if (node->node_type == OPT_TEXTURE) {
		struct opt_texture_data *node_texture = node->payload;
		uint8_t *node_texture_data =
			(uint8_t *)node_texture + sizeof(*node_texture);
		int texture_byte_count = node_texture->height;
		texture_byte_count *= node_texture->width;
		if (node_texture->texture_size == texture_byte_count) {
			texture_byte_count = node_texture->data_size;
		}
		node_texture_data += texture_byte_count;
		if (node_texture->inline_palette_count == 0) {
			if (node_texture->palette ==
			    (uint16_t *)node_texture_data) {
				texture_data =
					(const uint8_t *)texture_data + 4096;
				node_texture_data += 4096;
				if (memcmp(texture_data, node_texture_data,
					   8192) == 0) {
					node_texture_data -= 4096;
					return node_texture_data;
				}
			}
		} else {
			texture_data = (const uint8_t *)texture_data + 4096;
			node_texture_data += 4096;
			if (memcmp(texture_data, node_texture_data, 8192) ==
			    0) {
				node_texture_data -= 4096;
				return node_texture_data;
			}
		}
	}

	int child_offset = 0;
	int child_index = 0;
	if (node->child_count > 0) {
		do {
			struct opt_node *child =
				*(struct opt_node **)((uint8_t *)
							      node->p_children +
						      child_offset);
			if (stop_node == child) {
				return NULL;
			}
			void *result =
				opt_model_find_shared_texture_data_in_node_before_target(
					texture_data, child, stop_node);
			if (result != NULL) {
				return result;
			}
			child_offset += sizeof(*node->p_children);
			++child_index;
		} while (node->child_count > child_index);
	}

	return NULL;
}

/* Runs opt_model_find_shared_texture_data_in_node_before_target on each root of model
 * in order and returns its first match, or NULL; stops, returning NULL, at a
 * root that is stop_node. */
// FUNCTION: XVT 0x474910
void *
opt_model_find_earlier_shared_texture_data(const void *texture_data,
					   struct optimized_poly_object *model,
					   const struct opt_node *stop_node)
{
	struct optimized_poly_object *object = model;
	int root_index = 0;
	unsigned int root_offset = 0;
	if (object->root_node_count > 0) {
		do {
			struct opt_node *root_node = *(
				struct opt_node **)((uint8_t *)
							    object->root_nodes +
						    root_offset);
			if (stop_node == root_node) {
				return NULL;
			}
			void *result =
				opt_model_find_shared_texture_data_in_node_before_target(
					texture_data, root_node, stop_node);
			if (result != NULL) {
				return result;
			}
			root_offset += sizeof(*object->root_nodes);
			++root_index;
		} while (object->root_node_count > root_index);
	}

	return NULL;
}

/* Writes at dst the version 2 form of src_node and everything below it, and
 * returns the bytes written: 0 for a NULL node, and for a node the conversion
 * drops that has no children, whose slot in the parent's child table becomes
 * NULL. In the modern build every node starts aligned and the size is rounded
 * up.
 *
 * While g_opt_convert_vertex_node is NULL, as it is at each root, a node with
 * children gets three new first children: an OPT_MESHVERTS, an OPT_TEXCOORDS
 * and an OPT_VERTNORMALS node holding each distinct vertex, texture coordinate
 * and vertex normal found below it. When the merged vertices' last two are not
 * the bounding box's minimum and maximum corners, those two corners are
 * appended. These nodes become g_opt_convert_vertex_node, g_opt_convert_tex_coord_node
 * and g_opt_convert_vertex_normal_node. An OPT_FACEGROUP in that state gets them
 * through a new OPT_GROUP above it. From then on the source's own vertex,
 * texture coordinate and normal nodes are dropped, and each face node takes,
 * through opt_model_append_converted_faces_for_current_mesh, the faces of itself and
 * of the face nodes after it under the same texture, within the same child of
 * the last OPT_FACEGROUP passed, renumbered into the merged lists. Face nodes
 * whose faces were taken this way (edge count -1) are dropped.
 *
 * An OPT_FACEGROUP keeps its list of one float per child, padded with zeros or
 * cut to its child count. A texture with inline_palette_count 0 whose palette
 * lies outside its own data is pointed at an earlier texture with the same
 * colors (opt_model_find_earlier_shared_texture_data) or given a copy of the
 * palette. A face node met before any merged lists exist is copied: its edge
 * count and records, 16 more bytes per face taken again from the start of its
 * payload, its 36 bytes per face of normal and gradients, and, while mesh_state
 * has no vertex normal list, g_cur_vertex_count vertex normals. A dropped node
 * that has children becomes an OPT_GROUP. Other nodes are copied with their
 * payloads. Writes g_cur_mesh_vertices, g_cur_vertex_count, g_cur_mesh_materials,
 * g_cur_vert_normals and g_cur_mesh_tex_coords as it passes those nodes, and
 * g_opt_convert_source_texture_node and g_opt_convert_source_mesh_node. */
// FUNCTION: XVT 0x474960
unsigned int opt_model_convert_legacy_node_to_optimized(
	uint8_t *dst, struct opt_node *src_node,
	struct optimized_poly_object *src_model,
	struct optimized_poly_object *dst_model, struct scene_mesh *mesh_state)
{
	struct opt_node *destination_node;
	uint8_t *cursor;
	struct opt_node **source_node;
	struct scene_mesh child_mesh;
	struct opt_vector minimum;
	struct opt_vector maximum;
	unsigned int payload_size;
	int emit_node;
	int first_child_index;
	int child_index;

	if (src_node == NULL) {
		return 0;
	}

	cursor = dst;
	source_node = &src_node;
	emit_node = 1;
	payload_size = 0;
	destination_node = NULL;

	switch ((*source_node)->node_type) {
	case OPT_FACEDATA:
	case OPT_FACEDATA_QUAD_MESH:
	case OPT_FACEDATA_FACE_SET:
	case OPT_FACEDATA_TRIANGLE_STRIP_SET:
		if (*(int *)(*source_node)->payload < 0) {
			emit_node = 0;
			break;
		}
#ifdef XVT_MODERN
		cursor = xvt_opt_align_pointer(cursor);
#endif
		destination_node = (struct opt_node *)cursor;
		if (g_opt_convert_vertex_node != NULL) {
			cursor += sizeof(*destination_node);
			destination_node->node_type = (*source_node)->node_type;
			if ((*source_node)->p_name != NULL) {
				destination_node->p_name = (char *)cursor;
				strcpy((char *)cursor, (*source_node)->p_name);
				cursor += strlen((*source_node)->p_name) + 1;
			} else {
				destination_node->p_name = NULL;
			}
			destination_node->payload_count = 0;
#ifdef XVT_MODERN
			cursor = xvt_opt_align_pointer(cursor);
#endif
			destination_node->payload = cursor;
			*(int *)cursor = 0;
			opt_model_append_converted_faces_for_current_mesh(
				destination_node, (*source_node), src_model,
				mesh_state);
			emit_node = 0;
			cursor += sizeof(int) +
				  100 * destination_node->payload_count;
		} else {
			unsigned int face_record_size;
			unsigned int trailing_size;
			uint8_t *source_trailing_data;

			cursor += sizeof(*destination_node);
			destination_node->node_type = (*source_node)->node_type;
			if ((*source_node)->p_name != NULL) {
				destination_node->p_name = (char *)cursor;
				strcpy((char *)cursor, (*source_node)->p_name);
				cursor += strlen((*source_node)->p_name) + 1;
			} else {
				destination_node->p_name = NULL;
			}
			destination_node->payload_count =
				(*source_node)->payload_count;
#ifdef XVT_MODERN
			cursor = xvt_opt_align_pointer(cursor);
#endif
			destination_node->payload = cursor;
			if (g_opt_source_is_version0) {
				face_record_size =
					sizeof(int) +
					48 * (*source_node)->payload_count;
			} else {
				face_record_size =
					sizeof(int) +
					64 * (*source_node)->payload_count;
			}
			memcpy(cursor, (*source_node)->payload,
			       face_record_size);
			cursor += face_record_size;
			trailing_size = 16 * (*source_node)->payload_count;
			memcpy(cursor, (*source_node)->payload, trailing_size);
			cursor += trailing_size;
			payload_size = 36 * (*source_node)->payload_count;
			if (g_opt_source_is_version0) {
				source_trailing_data =
					(uint8_t *)(*source_node)->payload +
					sizeof(int) +
					48 * (*source_node)->payload_count;
			} else {
				source_trailing_data =
					(uint8_t *)(*source_node)->payload +
					sizeof(int) +
					64 * (*source_node)->payload_count;
			}
			memcpy(cursor, source_trailing_data, payload_size);
			cursor += payload_size;
			source_trailing_data += payload_size;
			if (mesh_state->p_vert_normals == NULL) {
				payload_size = sizeof(struct opt_vector) *
					       g_cur_vertex_count;
				memcpy(cursor, source_trailing_data,
				       payload_size);
				cursor += payload_size;
			}
			emit_node = 0;
		}
		break;

	case OPT_TRANSFORM:
		payload_size = 48;
		break;

	case OPT_MESHVERTS:
		g_cur_mesh_vertices = (*source_node)->payload;
		g_cur_vertex_count = (*source_node)->payload_count;
		if (g_opt_convert_vertex_node != NULL) {
			emit_node = 0;
		} else {
			payload_size = sizeof(struct opt_vector) *
				       (*source_node)->payload_count;
		}
		break;

	case OPT_TRANSLATION:
		payload_size = 12;
		break;

	case OPT_ROTATION:
		payload_size = 36;
		break;

	case OPT_SCALE:
		payload_size = 12;
		break;

	case OPT_NODEREF:
		destination_node = (*source_node);
		payload_size = (unsigned int)strlen(
				       (const char *)(*source_node)->payload) +
			       1;
		while (destination_node->node_type == OPT_NODEREF) {
			destination_node = opt_model_resolve_node_ref(
				src_model,
				(const char *)destination_node->payload);
			if (destination_node == NULL) {
				break;
			}
		}
		if (destination_node != NULL &&
		    destination_node->node_type == OPT_TEXTURE) {
			g_opt_convert_source_texture_node = destination_node;
		}
		break;

	case OPT_MATERIAL:
		payload_size = 56 * (*source_node)->payload_count;
		g_cur_mesh_materials = (*source_node)->payload;
		break;

	case OPT_VERTNORMALS:
		g_cur_vert_normals =
			(struct opt_vector *)(*source_node)->payload;
		mesh_state->p_vert_normals = g_cur_vert_normals;
		if (g_opt_convert_vertex_normal_node != NULL) {
			emit_node = 0;
		} else {
			payload_size = sizeof(struct opt_vector) *
				       (*source_node)->payload_count;
		}
		break;

	case OPT_TEXCOORDS:
		g_cur_mesh_tex_coords = (*source_node)->payload;
		if (g_opt_convert_tex_coord_node != NULL) {
			emit_node = 0;
		} else {
			payload_size = sizeof(struct opt_tex_coord) *
				       (*source_node)->payload_count;
		}
		break;

	case OPT_BASE_COLOR:
		payload_size = 12;
		break;

	case OPT_TEXTURE: {
		struct opt_texture_data *texture;
		uint16_t *embedded_palette;
		int texture_data_size;

		g_opt_convert_source_texture_node = (*source_node);
		texture = (struct opt_texture_data *)(*source_node)->payload;
		texture_data_size = texture->width * texture->height;
		if (texture_data_size == texture->texture_size) {
			payload_size = sizeof(*texture) + texture->data_size;
		} else {
			payload_size = sizeof(*texture) + texture_data_size;
		}
		if (texture->inline_palette_count != 0) {
			payload_size += 768 * texture->inline_palette_count;
		} else {
			embedded_palette = (uint16_t *)((uint8_t *)texture +
							sizeof(*texture));
			if (texture_data_size == texture->texture_size) {
				texture_data_size = texture->data_size;
			}
			embedded_palette =
				(uint16_t *)((uint8_t *)embedded_palette +
					     texture_data_size);
			if (texture->palette == embedded_palette) {
				payload_size += 12288;
			}
		}
		break;
	}

	case OPT_FACEGROUP:
#ifdef XVT_MODERN
		cursor = xvt_opt_align_pointer(cursor);
#endif
		destination_node = (struct opt_node *)cursor;
		g_opt_convert_source_mesh_node = (*source_node);
		if (g_opt_convert_vertex_node == NULL) {
			struct opt_node *vertex_node;
			struct opt_node *tex_coord_node;
			struct opt_node *normal_node;
			struct opt_node **generated_children;
			struct opt_vector *vectors;
			struct opt_vector *saved_normals;
			int saved_vertex_count;
			int remaining;

			destination_node->node_type = OPT_GROUP;
			cursor += sizeof(*destination_node);
			if ((*source_node)->p_name != NULL) {
				destination_node->p_name = (char *)cursor;
				strcpy((char *)cursor, (*source_node)->p_name);
				cursor += strlen((*source_node)->p_name) + 1;
			} else {
				destination_node->p_name = NULL;
			}
			destination_node->payload = NULL;
			destination_node->payload_count = 0;
			destination_node->child_count = 4;
#ifdef XVT_MODERN
			cursor = xvt_opt_align_pointer(cursor);
#endif
			destination_node->p_children =
				(struct opt_node **)cursor;
			generated_children = destination_node->p_children;
			cursor += sizeof(struct opt_node *) * 4;

#ifdef XVT_MODERN
			cursor = xvt_opt_align_pointer(cursor);
#endif
			vertex_node = (struct opt_node *)cursor;
			generated_children[0] = vertex_node;
			vertex_node->node_type = OPT_MESHVERTS;
			cursor += sizeof(*vertex_node);
			vertex_node->p_name = NULL;
			vertex_node->payload = cursor;
			vertex_node->payload_count = 0;
			vertex_node->child_count = 0;
			vertex_node->p_children = NULL;
			opt_model_collect_unique_vertices(
				vertex_node, (*source_node), src_model,
				mesh_state);
			g_opt_convert_vertex_node = vertex_node;

			vectors = (struct opt_vector *)vertex_node->payload;
			maximum.x = vectors->x;
			minimum.x = maximum.x;
			maximum.y = vectors->y;
			minimum.y = maximum.y;
			maximum.z = vectors->z;
			minimum.z = maximum.z;
			remaining = vertex_node->payload_count;
			if (remaining > 0) {
				do {
					if (vectors->x < minimum.x) {
						minimum.x = vectors->x;
					}
					if (vectors->y < minimum.y) {
						minimum.y = vectors->y;
					}
					if (vectors->z < minimum.z) {
						minimum.z = vectors->z;
					}
					if (vectors->x > maximum.x) {
						maximum.x = vectors->x;
					}
					if (vectors->y > maximum.y) {
						maximum.y = vectors->y;
					}
					if (vectors->z > maximum.z) {
						maximum.z = vectors->z;
					}
					++vectors;
					--remaining;
				} while (remaining != 0);
			}
			vectors -= 2;
			if (vectors[0].x != minimum.x ||
			    vectors[0].y != minimum.y ||
			    vectors[0].z != minimum.z ||
			    vectors[1].x != maximum.x ||
			    vectors[1].y != maximum.y ||
			    vectors[1].z != maximum.z) {
				vectors += 2;
				vectors[0].x = minimum.x;
				vectors[0].y = minimum.y;
				vectors[0].z = minimum.z;
				++vectors;
				vectors[0].x = maximum.x;
				vectors[0].y = maximum.y;
				vectors[0].z = maximum.z;
				vertex_node->payload_count += 2;
			}
			cursor += sizeof(struct opt_vector) *
				  vertex_node->payload_count;

#ifdef XVT_MODERN
			cursor = xvt_opt_align_pointer(cursor);
#endif
			tex_coord_node = (struct opt_node *)cursor;
			generated_children[1] = tex_coord_node;
			tex_coord_node->node_type = OPT_TEXCOORDS;
			cursor += sizeof(*tex_coord_node);
			tex_coord_node->p_name = NULL;
			tex_coord_node->payload = cursor;
			tex_coord_node->payload_count = 0;
			tex_coord_node->child_count = 0;
			tex_coord_node->p_children = NULL;
			opt_model_collect_unique_tex_coords(
				tex_coord_node, (*source_node), src_model,
				mesh_state);
			g_opt_convert_tex_coord_node = tex_coord_node;
			cursor += sizeof(struct opt_tex_coord) *
				  tex_coord_node->payload_count;

#ifdef XVT_MODERN
			cursor = xvt_opt_align_pointer(cursor);
#endif
			normal_node = (struct opt_node *)cursor;
			generated_children[2] = normal_node;
			normal_node->node_type = OPT_VERTNORMALS;
			cursor += sizeof(*normal_node);
			normal_node->p_name = NULL;
			normal_node->payload = cursor;
			normal_node->payload_count = 0;
			normal_node->child_count = 0;
			normal_node->p_children = NULL;
			saved_normals = mesh_state->p_vert_normals;
			saved_vertex_count = g_cur_vertex_count;
			mesh_state->p_vert_normals = NULL;
			opt_model_collect_unique_vertex_normals(
				normal_node, (*source_node), src_model,
				mesh_state);
			mesh_state->p_vert_normals = saved_normals;
			g_opt_convert_vertex_normal_node = normal_node;
			g_cur_vertex_count = saved_vertex_count;
			cursor += sizeof(struct opt_vector) *
				  normal_node->payload_count;

#ifdef XVT_MODERN
			cursor = xvt_opt_align_pointer(cursor);
#endif
			destination_node = (struct opt_node *)cursor;
			generated_children[3] = destination_node;
			destination_node->node_type = OPT_FACEGROUP;
			cursor += sizeof(*destination_node);
			destination_node->p_name = NULL;
#ifdef XVT_MODERN
			cursor = xvt_opt_align_pointer(cursor);
#endif
			destination_node->payload = cursor;
			destination_node->payload_count =
				(*source_node)->payload_count;
			memcpy(cursor, (*source_node)->payload,
			       sizeof(int) * (*source_node)->payload_count);
			cursor += sizeof(int) * (*source_node)->payload_count;
			if ((*source_node)->child_count >
			    (*source_node)->payload_count) {
				destination_node->payload_count =
					(*source_node)->child_count;
				memset(cursor, 0,
				       sizeof(int) *
					       ((*source_node)->child_count -
						(*source_node)->payload_count));
				cursor += sizeof(int) *
					  ((*source_node)->child_count -
					   (*source_node)->payload_count);
			} else if ((*source_node)->child_count <
				   (*source_node)->payload_count) {
				destination_node->payload_count =
					(*source_node)->child_count;
				cursor += sizeof(int) *
					  ((*source_node)->child_count -
					   (*source_node)->payload_count);
			}
		} else {
			destination_node->node_type = OPT_FACEGROUP;
			cursor += sizeof(*destination_node);
			destination_node->p_name = NULL;
#ifdef XVT_MODERN
			cursor = xvt_opt_align_pointer(cursor);
#endif
			destination_node->payload = cursor;
			destination_node->payload_count =
				(*source_node)->payload_count;
			memcpy(cursor, (*source_node)->payload,
			       sizeof(int) * (*source_node)->payload_count);
			cursor += sizeof(int) * (*source_node)->payload_count;
			if ((*source_node)->child_count >
			    (*source_node)->payload_count) {
				destination_node->payload_count =
					(*source_node)->child_count;
				memset(cursor, 0,
				       sizeof(int) *
					       ((*source_node)->child_count -
						(*source_node)->payload_count));
				cursor += sizeof(int) *
					  ((*source_node)->child_count -
					   (*source_node)->payload_count);
			} else if ((*source_node)->child_count <
				   (*source_node)->payload_count) {
				destination_node->payload_count =
					(*source_node)->child_count;
				cursor += sizeof(int) *
					  ((*source_node)->child_count -
					   (*source_node)->payload_count);
			}
		}
		emit_node = 0;
		break;

	case OPT_HARDPOINT:
		payload_size = 16;
		break;

	case OPT_ROTSCALE:
		payload_size = 48;
		break;

	case OPT_MESHDESC:
		payload_size = 72;
		break;

	default:
		break;
	}

	if (emit_node == 1) {
#ifdef XVT_MODERN
		cursor = xvt_opt_align_pointer(cursor);
#endif
		destination_node = (struct opt_node *)cursor;
		cursor += sizeof(*destination_node);
		destination_node->node_type = (*source_node)->node_type;
		if ((*source_node)->p_name != NULL) {
			destination_node->p_name = (char *)cursor;
			strcpy((char *)cursor, (*source_node)->p_name);
			cursor += strlen((*source_node)->p_name) + 1;
		} else {
			destination_node->p_name = NULL;
		}
		destination_node->payload_count = (*source_node)->payload_count;
#ifdef XVT_MODERN
		cursor = xvt_opt_align_pointer(cursor);
#endif
		destination_node->payload = cursor;
		memcpy(cursor, (*source_node)->payload, payload_size);
		cursor += payload_size;
		if ((*source_node)->node_type == OPT_TEXTURE) {
			struct opt_texture_data *source_texture;
			uint16_t *embedded_palette;
			int texture_data_size;

			source_texture =
				(struct opt_texture_data *)(*source_node)
					->payload;
			if (source_texture->inline_palette_count == 0) {
				embedded_palette =
					(uint16_t *)((uint8_t *)source_texture +
						     sizeof(*source_texture));
				texture_data_size = source_texture->width *
						    source_texture->height;
				if (source_texture->texture_size ==
				    texture_data_size) {
					texture_data_size =
						source_texture->data_size;
				}
				embedded_palette =
					(uint16_t *)((uint8_t *)
							     embedded_palette +
						     texture_data_size);
				if (source_texture->palette !=
				    embedded_palette) {
					void *shared_texture_data;

					shared_texture_data =
						opt_model_find_earlier_shared_texture_data(
							source_texture->palette,
							dst_model,
							destination_node);
					if (shared_texture_data != NULL) {
						struct opt_texture_data
							*destination_texture;

						destination_texture =
							(struct opt_texture_data
								 *)destination_node
								->payload;
						destination_texture->palette =
							shared_texture_data;
					} else {
						const void *source_palette;
						struct opt_texture_data
							*destination_texture;

						source_palette =
							source_texture->palette;
						destination_texture =
							(struct opt_texture_data
								 *)destination_node
								->payload;
						destination_texture->palette =
							(uint16_t *)cursor;
						memcpy(cursor, source_palette,
						       12288);
						cursor += 12288;
					}
				} else {
					struct opt_texture_data
						*destination_texture;
					uint16_t *destination_palette;

					destination_texture =
						(struct opt_texture_data *)
							destination_node
								->payload;
					destination_palette =
						(uint16_t
							 *)((uint8_t *)
								    destination_texture +
							    sizeof(*destination_texture));
					texture_data_size =
						destination_texture->width *
						destination_texture->height;
					if (destination_texture->texture_size ==
					    texture_data_size) {
						texture_data_size =
							destination_texture
								->data_size;
					}
					destination_palette =
						(uint16_t
							 *)((uint8_t *)
								    destination_palette +
							    texture_data_size);
					destination_texture->palette =
						destination_palette;
				}
			}
		}
	}

	if (destination_node == NULL) {
		if ((*source_node)->child_count == 0) {
			return 0;
		}
#ifdef XVT_MODERN
		cursor = xvt_opt_align_pointer(cursor);
#endif
		destination_node = (struct opt_node *)cursor;
		cursor += sizeof(*destination_node);
		destination_node->p_name = NULL;
		destination_node->node_type = OPT_GROUP;
		destination_node->payload_count = 0;
		destination_node->payload = NULL;
	}

	destination_node->child_count = 0;
	destination_node->p_children = NULL;
	if ((*source_node)->child_count != 0) {
		if (g_opt_convert_vertex_node == NULL) {
			struct opt_node *vertex_node;
			struct opt_node *tex_coord_node;
			struct opt_node *normal_node;
			struct opt_vector *vectors;
			struct opt_vector *saved_normals;
			int saved_vertex_count;
			int remaining;

			destination_node->child_count =
				(*source_node)->child_count + 3;
#ifdef XVT_MODERN
			cursor = xvt_opt_align_pointer(cursor);
#endif
			destination_node->p_children =
				(struct opt_node **)cursor;
			cursor += sizeof(struct opt_node *) *
				  destination_node->child_count;

#ifdef XVT_MODERN
			cursor = xvt_opt_align_pointer(cursor);
#endif
			vertex_node = (struct opt_node *)cursor;
			destination_node->p_children[0] = vertex_node;
			vertex_node->node_type = OPT_MESHVERTS;
			cursor += sizeof(*vertex_node);
			vertex_node->p_name = NULL;
			vertex_node->payload = cursor;
			vertex_node->payload_count = 0;
			vertex_node->child_count = 0;
			vertex_node->p_children = NULL;
			opt_model_collect_unique_vertices(
				vertex_node, (*source_node), src_model,
				mesh_state);
			g_opt_convert_vertex_node = vertex_node;

			vectors = (struct opt_vector *)vertex_node->payload;
			maximum.x = vectors->x;
			minimum.x = maximum.x;
			maximum.y = vectors->y;
			minimum.y = maximum.y;
			maximum.z = vectors->z;
			minimum.z = maximum.z;
			remaining = vertex_node->payload_count;
			if (remaining > 0) {
				do {
					if (vectors->x < minimum.x) {
						minimum.x = vectors->x;
					}
					if (vectors->y < minimum.y) {
						minimum.y = vectors->y;
					}
					if (vectors->z < minimum.z) {
						minimum.z = vectors->z;
					}
					if (vectors->x > maximum.x) {
						maximum.x = vectors->x;
					}
					if (vectors->y > maximum.y) {
						maximum.y = vectors->y;
					}
					if (vectors->z > maximum.z) {
						maximum.z = vectors->z;
					}
					++vectors;
					--remaining;
				} while (remaining != 0);
			}
			vectors -= 2;
			if (vectors[0].x != minimum.x ||
			    vectors[0].y != minimum.y ||
			    vectors[0].z != minimum.z ||
			    vectors[1].x != maximum.x ||
			    vectors[1].y != maximum.y ||
			    vectors[1].z != maximum.z) {
				vectors += 2;
				vectors[0].x = minimum.x;
				vectors[0].y = minimum.y;
				vectors[0].z = minimum.z;
				++vectors;
				vectors[0].x = maximum.x;
				vectors[0].y = maximum.y;
				vectors[0].z = maximum.z;
				vertex_node->payload_count += 2;
			}
			cursor += sizeof(struct opt_vector) *
				  vertex_node->payload_count;

#ifdef XVT_MODERN
			cursor = xvt_opt_align_pointer(cursor);
#endif
			tex_coord_node = (struct opt_node *)cursor;
			destination_node->p_children[1] = tex_coord_node;
			tex_coord_node->node_type = OPT_TEXCOORDS;
			cursor += sizeof(*tex_coord_node);
			tex_coord_node->p_name = NULL;
			tex_coord_node->payload = cursor;
			tex_coord_node->payload_count = 0;
			tex_coord_node->child_count = 0;
			tex_coord_node->p_children = NULL;
			opt_model_collect_unique_tex_coords(
				tex_coord_node, (*source_node), src_model,
				mesh_state);
			g_opt_convert_tex_coord_node = tex_coord_node;
			cursor += sizeof(struct opt_tex_coord) *
				  tex_coord_node->payload_count;

#ifdef XVT_MODERN
			cursor = xvt_opt_align_pointer(cursor);
#endif
			normal_node = (struct opt_node *)cursor;
			destination_node->p_children[2] = normal_node;
			normal_node->node_type = OPT_VERTNORMALS;
			cursor += sizeof(*normal_node);
			normal_node->p_name = NULL;
			normal_node->payload = cursor;
			normal_node->payload_count = 0;
			normal_node->child_count = 0;
			normal_node->p_children = NULL;
			saved_normals = mesh_state->p_vert_normals;
			saved_vertex_count = g_cur_vertex_count;
			mesh_state->p_vert_normals = NULL;
			opt_model_collect_unique_vertex_normals(
				normal_node, (*source_node), src_model,
				mesh_state);
			mesh_state->p_vert_normals = saved_normals;
			g_opt_convert_vertex_normal_node = normal_node;
			g_cur_vertex_count = saved_vertex_count;
			first_child_index = 3;
			cursor += sizeof(struct opt_vector) *
				  normal_node->payload_count;
		} else {
			first_child_index = 0;
			destination_node->child_count =
				(*source_node)->child_count;
#ifdef XVT_MODERN
			cursor = xvt_opt_align_pointer(cursor);
#endif
			destination_node->p_children =
				(struct opt_node **)cursor;
			cursor += sizeof(struct opt_node *) *
				  destination_node->child_count;
		}

		child_mesh = *mesh_state;
		for (child_index = 0; child_index < (*source_node)->child_count;
		     ++child_index) {
			unsigned int child_size;

#ifdef XVT_MODERN
			cursor = xvt_opt_align_pointer(cursor);
#endif
			destination_node
				->p_children[first_child_index + child_index] =
				(struct opt_node *)cursor;
			child_size = opt_model_convert_legacy_node_to_optimized(
				cursor, (*source_node)->p_children[child_index],
				src_model, dst_model, &child_mesh);
			if (child_size == 0) {
				destination_node->p_children[first_child_index +
							     child_index] =
					NULL;
			}
			cursor += child_size;
		}
	}

#ifdef XVT_MODERN
	return (unsigned int)xvt_opt_align_size((size_t)(cursor - dst));
#else
	return (unsigned int)(cursor - dst);
#endif
}

/* Appends to dst_vertex_node's list each vertex of every OPT_MESHVERTS node at or
 * below src_node that is not already in it, comparing the floats exactly, and
 * raises its payload_count. Follows OPT_NODEREF links and stops at one that does
 * not resolve. Does not check the list's room; mesh_state is unused. */
// FUNCTION: XVT 0x475740
void opt_model_collect_unique_vertices(struct opt_node *dst_vertex_node,
				       const struct opt_node *src_node,
				       struct optimized_poly_object *src_model,
				       struct scene_mesh *mesh_state)
{
	if (src_node == NULL) {
		return;
	}
	while (src_node->node_type == OPT_NODEREF) {
		src_node = opt_model_resolve_node_ref(
			src_model, (const char *)src_node->payload);
		if (src_node == NULL) {
			return;
		}
	}

	if (src_node->node_type == OPT_MESHVERTS) {
		float *source_vertex = (float *)src_node->payload;
		for (int source_index = 0;
		     source_index < src_node->payload_count; ++source_index) {
			float *destination_vertex =
				(float *)dst_vertex_node->payload;
			int destination_index = 0;
			int destination_count = dst_vertex_node->payload_count;
			while (destination_index < destination_count) {
				if (source_vertex[0] == destination_vertex[0] &&
				    source_vertex[1] == destination_vertex[1] &&
				    source_vertex[2] == destination_vertex[2]) {
					break;
				}
				destination_vertex += 3;
				++destination_index;
			}
			if (destination_index == destination_count) {
				destination_vertex[0] = source_vertex[0];
				destination_vertex[1] = source_vertex[1];
				destination_vertex[2] = source_vertex[2];
				++dst_vertex_node->payload_count;
			}
			source_vertex += 3;
		}
	}

	int child_index = 0;
	while (src_node->child_count > child_index) {
		opt_model_collect_unique_vertices(
			dst_vertex_node, src_node->p_children[child_index],
			src_model, mesh_state);
		++child_index;
	}
}

/* Appends to dst_tex_coord_node's list each texture coordinate of every
 * OPT_TEXCOORDS node at or below src_node that is not already in it, comparing
 * the floats exactly, and raises its payload_count. Follows OPT_NODEREF links
 * and stops at one that does not resolve. Does not check the list's room;
 * mesh_state is unused. */
// FUNCTION: XVT 0x475850
void opt_model_collect_unique_tex_coords(
	struct opt_node *dst_tex_coord_node, const struct opt_node *src_node,
	struct optimized_poly_object *src_model, struct scene_mesh *mesh_state)
{
	if (src_node == NULL) {
		return;
	}
	while (src_node->node_type == OPT_NODEREF) {
		src_node = opt_model_resolve_node_ref(
			src_model, (const char *)src_node->payload);
		if (src_node == NULL) {
			return;
		}
	}

	struct opt_node *destination_node;
	if (src_node->node_type == OPT_TEXCOORDS) {
		float *source_tex_coord = (float *)src_node->payload;
		destination_node = dst_tex_coord_node;
		int source_index = 0;
		while (source_index < src_node->payload_count) {
			float *destination_tex_coord =
				(float *)destination_node->payload;
			int destination_index = 0;
			int destination_count = destination_node->payload_count;
			while (destination_index < destination_count) {
				if (source_tex_coord[0] ==
					    destination_tex_coord[0] &&
				    source_tex_coord[1] ==
					    destination_tex_coord[1]) {
					break;
				}
				destination_tex_coord += 2;
				++destination_index;
			}
			if (destination_index == destination_count) {
				destination_tex_coord[0] = source_tex_coord[0];
				destination_tex_coord[1] = source_tex_coord[1];
				++destination_node->payload_count;
			}
			source_tex_coord += 2;
			++source_index;
		}
	} else {
		destination_node = dst_tex_coord_node;
	}

	int child_index = 0;
	while (child_index < src_node->child_count) {
		opt_model_collect_unique_tex_coords(
			destination_node, src_node->p_children[child_index],
			src_model, mesh_state);
		++child_index;
	}
}

/* Appends to dst_normal_node's list each vertex normal at or below src_node that
 * is not already in it, comparing the floats exactly, and raises its
 * payload_count. Normals come from OPT_VERTNORMALS nodes, which also become
 * mesh_state->p_vert_normals, and, while mesh_state has none, from the
 * g_cur_vertex_count normals stored after each face node's data. Sets
 * g_cur_vertex_count at each OPT_MESHVERTS node. For a version 0 source it clears
 * mesh_state->p_vert_normals after every node. Follows OPT_NODEREF links and stops
 * at one that does not resolve. Does not check the list's room. */
// FUNCTION: XVT 0x475940
void opt_model_collect_unique_vertex_normals(
	struct opt_node *dst_normal_node, struct opt_node *src_node,
	struct optimized_poly_object *src_model, struct scene_mesh *mesh_state)
{
	struct opt_node *node = src_node;
	if (node == NULL) {
		return;
	}
	while (node->node_type == OPT_NODEREF) {
		node = opt_model_resolve_node_ref(src_model,
						  (const char *)node->payload);
		if (node == NULL) {
			return;
		}
	}

	struct opt_node *destination_node = dst_normal_node;
	struct opt_vector *source_normal;
	int source_index;
	switch (node->node_type) {
	case OPT_FACEDATA:
	case OPT_FACEDATA_QUAD_MESH:
	case OPT_FACEDATA_FACE_SET:
	case OPT_FACEDATA_TRIANGLE_STRIP_SET:
		if (mesh_state->p_vert_normals == NULL) {
			struct opt_legacy_face_payload *face_data =
				(struct opt_legacy_face_payload *)node->payload;
			if (g_opt_source_is_version0) {
				source_normal =
					(struct opt_vector *)&(
						(struct
						 opt_legacy_face_payload_v0 *)
							face_data)
						->storage[node->payload_count];
			} else {
				source_normal =
					(struct opt_vector *)&face_data
						->storage[node->payload_count];
			}
			source_index = 0;
			if (g_cur_vertex_count > 0) {
				do {
					struct opt_vector *destination_normal =
						(struct opt_vector *)
							destination_node
								->payload;
					int destination_index = 0;
					int destination_count =
						destination_node->payload_count;
					if (destination_index <
					    destination_count) {
						do {
							if (destination_normal
								    ->x ==
							    source_normal->x) {
								if (source_normal->y ==
									    destination_normal
										    ->y &&
								    source_normal->z ==
									    destination_normal
										    ->z) {
									break;
								}
							}
							++destination_normal;
							++destination_index;
						} while (destination_index <
							 destination_count);
					}
					if (destination_index ==
					    destination_count) {
						destination_normal->x =
							source_normal->x;
						destination_normal->y =
							source_normal->y;
						destination_normal->z =
							source_normal->z;
						++destination_node
							  ->payload_count;
					}
					++source_normal;
					++source_index;
				} while (source_index < g_cur_vertex_count);
			}
		}
		break;

	case OPT_MESHVERTS:
		g_cur_vertex_count = node->payload_count;
		break;

	case OPT_VERTNORMALS:
		mesh_state->p_vert_normals = (struct opt_vector *)node->payload;
		source_normal = (struct opt_vector *)node->payload;
		source_index = 0;
		if (node->payload_count > 0) {
			do {
				struct opt_vector *destination_normal =
					(struct opt_vector *)
						destination_node->payload;
				int destination_index = 0;
				int destination_count =
					destination_node->payload_count;
				if (destination_index < destination_count) {
					do {
						if (destination_normal->x ==
						    source_normal->x) {
							if (source_normal->y ==
								    destination_normal
									    ->y &&
							    destination_normal
									    ->z ==
								    source_normal
									    ->z) {
								break;
							}
						}
						++destination_normal;
						++destination_index;
					} while (destination_index <
						 destination_count);
				}
				if (destination_index == destination_count) {
					destination_normal->x =
						source_normal->x;
					destination_normal->y =
						source_normal->y;
					destination_normal->z =
						source_normal->z;
					++destination_node->payload_count;
				}
				++source_normal;
				++source_index;
			} while (source_index < node->payload_count);
		}
		break;

	default:
		break;
	}

	if (g_opt_source_is_version0) {
		mesh_state->p_vert_normals = NULL;
	}
	int child_index = 0;
	while (child_index < node->child_count) {
		opt_model_collect_unique_vertex_normals(
			destination_node, node->p_children[child_index],
			src_model, mesh_state);
		++child_index;
	}
}

/* Returns the index in unique_vector_node's list of source_vectors[sourceIndex],
 * or -1 for a negative sourceIndex. The search starts at
 * g_opt_convert_vector_search_cursor minus (sourceIndex >> 1), or at 0 when that is
 * negative or over the list length, runs to the end, then runs over the whole
 * list from 0. Leaves g_opt_convert_vector_search_cursor at the index found.
 * Returns 0 when the vector is not in the list, leaving the cursor at the list
 * length. */
// FUNCTION: XVT 0x475B70
int opt_model_remap_vector_index(const struct opt_node *unique_vector_node,
				 const struct opt_vector *source_vectors,
				 int source_index)
{
	if (source_index < 0) {
		return -1;
	}

	source_vectors += source_index;
	const float *unique_vectors = unique_vector_node->payload;
	int cursor = g_opt_convert_vector_search_cursor;
	cursor -= source_index >> 1;
	g_opt_convert_vector_search_cursor = cursor;
	if (cursor < 0 || unique_vector_node->payload_count < cursor) {
		g_opt_convert_vector_search_cursor = 0;
		cursor = 0;
	}

	unique_vectors += 3 * g_opt_convert_vector_search_cursor;
	if (unique_vector_node->payload_count >
	    g_opt_convert_vector_search_cursor) {
		do {
			if (unique_vectors[0] != source_vectors->x ||
			    unique_vectors[1] != source_vectors->y ||
			    source_vectors->z != unique_vectors[2]) {
				unique_vectors += 3;
				cursor = g_opt_convert_vector_search_cursor;
				++cursor;
				g_opt_convert_vector_search_cursor = cursor;
			} else {
				return g_opt_convert_vector_search_cursor;
			}
		} while (unique_vector_node->payload_count >
			 g_opt_convert_vector_search_cursor);
	}

	g_opt_convert_vector_search_cursor = 0;
	unique_vectors = unique_vector_node->payload;
	if (unique_vector_node->payload_count > 0) {
		do {
			if (unique_vectors[0] != source_vectors->x ||
			    unique_vectors[1] != source_vectors->y ||
			    source_vectors->z != unique_vectors[2]) {
				unique_vectors += 3;
				cursor = g_opt_convert_vector_search_cursor;
				++cursor;
				g_opt_convert_vector_search_cursor = cursor;
			} else {
				return g_opt_convert_vector_search_cursor;
			}
		} while (unique_vector_node->payload_count >
			 g_opt_convert_vector_search_cursor);
	}

	return 0;
}

/* Returns the index in unique_tex_coord_node's list of
 * source_tex_coords[sourceIndex], or -1 for a negative sourceIndex. The search
 * starts at g_opt_convert_tex_coord_search_cursor minus (sourceIndex >> 1), or at 0
 * when that is negative or over the list length, runs to the end, then runs
 * over the whole list from 0. Leaves g_opt_convert_tex_coord_search_cursor at the
 * index found. Returns 0 when the coordinate is not in the list, leaving the
 * cursor at the list length. */
// FUNCTION: XVT 0x475C70
int opt_model_remap_tex_coord_index(
	const struct opt_node *unique_tex_coord_node,
	const struct opt_tex_coord *source_tex_coords, int source_index)
{
	if (source_index < 0) {
		return -1;
	}

	source_tex_coords += source_index;
	const float *unique_tex_coords = unique_tex_coord_node->payload;
	g_opt_convert_tex_coord_search_cursor -= source_index >> 1;
	if (g_opt_convert_tex_coord_search_cursor < 0 ||
	    unique_tex_coord_node->payload_count <
		    g_opt_convert_tex_coord_search_cursor) {
		g_opt_convert_tex_coord_search_cursor = 0;
	}

	unique_tex_coords += 2 * g_opt_convert_tex_coord_search_cursor;
	if (unique_tex_coord_node->payload_count >
	    g_opt_convert_tex_coord_search_cursor) {
		do {
			if (unique_tex_coords[0] != source_tex_coords->u ||
			    unique_tex_coords[1] != source_tex_coords->v) {
				unique_tex_coords += 2;
				++g_opt_convert_tex_coord_search_cursor;
			} else {
				return g_opt_convert_tex_coord_search_cursor;
			}
		} while (unique_tex_coord_node->payload_count >
			 g_opt_convert_tex_coord_search_cursor);
	}

	g_opt_convert_tex_coord_search_cursor = 0;
	unique_tex_coords = unique_tex_coord_node->payload;
	if (unique_tex_coord_node->payload_count > 0) {
		do {
			if (unique_tex_coords[0] != source_tex_coords->u ||
			    unique_tex_coords[1] != source_tex_coords->v) {
				unique_tex_coords += 2;
				++g_opt_convert_tex_coord_search_cursor;
			} else {
				return g_opt_convert_tex_coord_search_cursor;
			}
		} while (unique_tex_coord_node->payload_count >
			 g_opt_convert_tex_coord_search_cursor);
	}

	return 0;
}

/* Walks node and the nodes below it, following OPT_NODEREF links, and moves
 * into dst_face_node the faces of every face node from target_face_node on that has
 * a positive edge count while the last texture passed
 * (g_opt_convert_face_texture_node) is g_opt_convert_source_texture_node. Each moved
 * face's vertex, texture coordinate and normal indices are renumbered into the
 * merged lists (opt_model_remap_vector_index, opt_model_remap_tex_coord_index) and its
 * edge numbers raised by dst_face_node's edge count, a fourth edge of -1 kept.
 * The new faces go in front of dst_face_node's faces, in each of its three parts:
 * the 64-byte records, the face normals and the two texture gradient vectors. A
 * version 0 source has no normal indices, so its vertex indices serve. Adds the
 * moved node's face and edge counts to dst_face_node's and sets the moved node's
 * edge count to -1. Updates g_opt_convert_target_face_found,
 * g_opt_convert_face_texture_node, g_cur_mesh_vertices, g_cur_mesh_tex_coords and
 * mesh_state->p_vert_normals as it passes those nodes. Does not check
 * dst_face_node's room. */
// FUNCTION: XVT 0x475D50
void opt_model_append_converted_faces_for_node(
	struct opt_node *dst_face_node, struct opt_node *target_face_node,
	struct opt_node *node, struct optimized_poly_object *src_model,
	struct scene_mesh *mesh_state)
{
	if (node == NULL) {
		return;
	}

	while (node->node_type == OPT_NODEREF) {
		node = opt_model_resolve_node_ref(src_model,
						  (const char *)node->payload);
		if (node == NULL) {
			return;
		}
	}

	if (g_opt_convert_target_face_found == 0 && node == target_face_node) {
		g_opt_convert_target_face_found = 1;
	}

	switch (node->node_type) {
	case OPT_FACEDATA:
	case OPT_FACEDATA_QUAD_MESH:
	case OPT_FACEDATA_FACE_SET:
	case OPT_FACEDATA_TRIANGLE_STRIP_SET:
		if (g_opt_convert_target_face_found != 0 &&
		    g_opt_convert_face_texture_node ==
			    g_opt_convert_source_texture_node &&
		    *(int *)node->payload > 0) {
			uint8_t *destination_data = dst_face_node->payload;
			uint8_t *destination_bytes =
				destination_data + sizeof(int);
			int destination_edge_count = *(int *)destination_data;
#ifdef XVT_MODERN
			memmove(destination_bytes + 64 * node->payload_count,
				destination_bytes,
				100 * dst_face_node->payload_count);
#else
			memcpy(destination_bytes + 64 * node->payload_count,
			       destination_bytes,
			       100 * dst_face_node->payload_count);
#endif

			const int *source_cursor =
				(const int *)node->payload + 1;
			const struct opt_vector *source_vectors =
				mesh_state->p_vert_normals;
			if (source_vectors == NULL) {
				if (g_opt_source_is_version0) {
					source_vectors =
						(const struct opt_vector
							 *)((const uint8_t *)
								    source_cursor +
							    48 * node->payload_count +
							    36 * node->payload_count);
				} else {
					source_vectors =
						(const struct opt_vector
							 *)((const uint8_t *)
								    source_cursor +
							    64 * node->payload_count +
							    36 * node->payload_count);
				}
			}

			int *destination_cursor = (int *)destination_bytes;
			int face_index;
			for (face_index = 0; face_index < node->payload_count;
			     ++face_index) {
				*destination_cursor++ =
					opt_model_remap_vector_index(
						g_opt_convert_vertex_node,
						(const struct opt_vector *)
							g_cur_mesh_vertices,
						*source_cursor++);
				*destination_cursor++ =
					opt_model_remap_vector_index(
						g_opt_convert_vertex_node,
						(const struct opt_vector *)
							g_cur_mesh_vertices,
						*source_cursor++);
				*destination_cursor++ =
					opt_model_remap_vector_index(
						g_opt_convert_vertex_node,
						(const struct opt_vector *)
							g_cur_mesh_vertices,
						*source_cursor++);
				*destination_cursor++ =
					opt_model_remap_vector_index(
						g_opt_convert_vertex_node,
						(const struct opt_vector *)
							g_cur_mesh_vertices,
						*source_cursor++);

				*destination_cursor++ = destination_edge_count +
							*source_cursor++;
				*destination_cursor++ = destination_edge_count +
							*source_cursor++;
				*destination_cursor++ = destination_edge_count +
							*source_cursor++;
				int source_edge_index = *source_cursor++;
				if (source_edge_index == -1) {
					*destination_cursor++ = -1;
				} else {
					*destination_cursor++ =
						destination_edge_count +
						source_edge_index;
				}

				*destination_cursor++ =
					opt_model_remap_tex_coord_index(
						g_opt_convert_tex_coord_node,
						(const struct opt_tex_coord *)
							g_cur_mesh_tex_coords,
						*source_cursor++);
				*destination_cursor++ =
					opt_model_remap_tex_coord_index(
						g_opt_convert_tex_coord_node,
						(const struct opt_tex_coord *)
							g_cur_mesh_tex_coords,
						*source_cursor++);
				*destination_cursor++ =
					opt_model_remap_tex_coord_index(
						g_opt_convert_tex_coord_node,
						(const struct opt_tex_coord *)
							g_cur_mesh_tex_coords,
						*source_cursor++);
				*destination_cursor++ =
					opt_model_remap_tex_coord_index(
						g_opt_convert_tex_coord_node,
						(const struct opt_tex_coord *)
							g_cur_mesh_tex_coords,
						*source_cursor++);

				if (g_opt_source_is_version0) {
					source_cursor -= 12;
				}
				*destination_cursor++ =
					opt_model_remap_vector_index(
						g_opt_convert_vertex_normal_node,
						source_vectors,
						*source_cursor++);
				*destination_cursor++ =
					opt_model_remap_vector_index(
						g_opt_convert_vertex_normal_node,
						source_vectors,
						*source_cursor++);
				*destination_cursor++ =
					opt_model_remap_vector_index(
						g_opt_convert_vertex_normal_node,
						source_vectors,
						*source_cursor++);
				*destination_cursor++ =
					opt_model_remap_vector_index(
						g_opt_convert_vertex_normal_node,
						source_vectors,
						*source_cursor++);
				if (g_opt_source_is_version0) {
					source_cursor += 8;
				}
			}

			uint8_t *destination_trailing_bytes =
				destination_bytes +
				64 * (node->payload_count +
				      dst_face_node->payload_count);
			struct opt_vector *destination_face_normals =
				(struct opt_vector *)destination_trailing_bytes;
#ifdef XVT_MODERN
			memmove(destination_trailing_bytes +
					12 * node->payload_count,
				destination_trailing_bytes,
				36 * dst_face_node->payload_count);
#else
			memcpy(destination_trailing_bytes +
				       12 * node->payload_count,
			       destination_trailing_bytes,
			       36 * dst_face_node->payload_count);
#endif
			const struct opt_vector *source_face_normals;
			if (g_opt_source_is_version0) {
				source_face_normals =
					(const struct opt_vector
						 *)((const uint8_t *)
							    node->payload +
						    sizeof(int) +
						    48 * node->payload_count);
			} else {
				source_face_normals =
					(const struct opt_vector
						 *)((const uint8_t *)
							    node->payload +
						    sizeof(int) +
						    64 * node->payload_count);
			}
			for (face_index = 0; face_index < node->payload_count;
			     ++face_index) {
				destination_face_normals[face_index] =
					source_face_normals[face_index];
			}

			destination_trailing_bytes =
				(uint8_t *)&destination_face_normals
					[node->payload_count +
					 dst_face_node->payload_count];
			struct opt_vector *destination_texture_gradients =
				(struct opt_vector *)destination_trailing_bytes;
#ifdef XVT_MODERN
			memmove(destination_trailing_bytes +
					24 * node->payload_count,
				destination_trailing_bytes,
				24 * dst_face_node->payload_count);
#else
			memcpy(destination_trailing_bytes +
				       24 * node->payload_count,
			       destination_trailing_bytes,
			       24 * dst_face_node->payload_count);
#endif
			const struct opt_vector *source_texture_gradients =
				source_face_normals + node->payload_count;
			for (face_index = 0; face_index < node->payload_count;
			     ++face_index) {
				destination_texture_gradients[2 * face_index] =
					source_texture_gradients[2 *
								 face_index];
				destination_texture_gradients[2 * face_index +
							      1] =
					source_texture_gradients
						[2 * face_index + 1];
			}

			dst_face_node->payload_count += node->payload_count;
			*(int *)destination_data += *(int *)node->payload;
			*(int *)node->payload = -1;
		}
		break;

	case OPT_MESHVERTS:
		g_cur_mesh_vertices = node->payload;
		break;

	case OPT_VERTNORMALS:
		mesh_state->p_vert_normals = (struct opt_vector *)node->payload;
		break;

	case OPT_TEXCOORDS:
		g_cur_mesh_tex_coords = node->payload;
		break;

	case OPT_TEXTURE:
		g_opt_convert_face_texture_node = node;
		break;

	default:
		break;
	}

	for (int child_index = 0; child_index < node->child_count;
	     ++child_index) {
		opt_model_append_converted_faces_for_node(
			dst_face_node, target_face_node,
			node->p_children[child_index], src_model, mesh_state);
	}
}

/* Moves into dst_face_node, with opt_model_append_converted_faces_for_node, the faces
 * of target_face_node and of the later face nodes under the same texture, within
 * the child of g_opt_convert_source_mesh_node that holds target_face_node; children
 * before it are walked but give no faces. Sets g_opt_convert_face_texture_node to
 * g_opt_convert_source_texture_node and g_opt_convert_target_face_found to 0 first. */
// FUNCTION: XVT 0x476280
void opt_model_append_converted_faces_for_current_mesh(
	struct opt_node *dst_face_node, struct opt_node *target_face_node,
	struct optimized_poly_object *src_model, struct scene_mesh *mesh_state)
{
	g_opt_convert_face_texture_node = g_opt_convert_source_texture_node;
	int child_index = 0;
	g_opt_convert_target_face_found = 0;
	if (g_opt_convert_source_mesh_node->child_count > 0) {
		int child_offset = 0;
		do {
			opt_model_append_converted_faces_for_node(
				dst_face_node, target_face_node,
				*(struct opt_node *
					  *)((uint8_t *)
						     g_opt_convert_source_mesh_node
							     ->p_children +
					     child_offset),
				src_model, mesh_state);
			if (g_opt_convert_target_face_found != 0) {
				break;
			}
			child_offset += sizeof(struct opt_node *);
			++child_index;
		} while (g_opt_convert_source_mesh_node->child_count >
			 child_index);
	}
}

/* Builds a runtime copy of the packed model in source_handle and returns its new
 * Memory handle. It measures the copy with opt_model_build_runtime_node, allocates
 * it, builds it, then fixes its texture palette pointers with
 * opt_model_fixup_runtime_texture_pointers. Sets g_cur_mesh_vertices,
 * g_cur_mesh_tex_coords, g_cur_vert_normals, g_model_node_walk_unused_scratch2 and
 * g_cur_mesh_materials to NULL and g_cur_vertex_count to 0 first. A failed
 * allocation ends the program through fe_disk_io_fatal_error. The modern build
 * returns 0 for a source_handle of 0. */
// FUNCTION: XVT 0x4762F0
uint16_t opt_model_create_runtime_handle(unsigned int source_handle)
{
#ifdef XVT_MODERN
	if (!source_handle) {
		return 0;
	}
#endif
	struct optimized_poly_object *source_model =
		(struct optimized_poly_object *)memory_get_handle_block(
			source_handle);
	if (source_model->self_marker != source_model) {
		opt_model_adjust_optimized_poly_object_pointers(source_model);
	}
	struct scene_mesh mesh_state;
	memset(&mesh_state, 0, sizeof(mesh_state));
	g_cur_mesh_vertices = NULL;
	g_cur_mesh_tex_coords = NULL;
	g_cur_vert_normals = NULL;
	g_model_node_walk_unused_scratch2 = NULL;
	g_cur_mesh_materials = NULL;
	g_cur_vertex_count = 0;

	struct optimized_poly_object *runtime_model;
	unsigned int serialized_size =
		sizeof(struct opt_node *) *
			(unsigned int)source_model->root_node_count +
		sizeof(*runtime_model);

	int root_index;
	for (root_index = 0; root_index < source_model->root_node_count;
	     ++root_index) {
		serialized_size += opt_model_build_runtime_node(
			source_model->root_nodes[root_index], &mesh_state,
			NULL);
	}
	memory_handle_block_done_stub(source_handle);
	uint16_t runtime_handle = memory_alloc_handle(serialized_size, 0);
	if (runtime_handle == 0) {
		fe_disk_io_fatal_error(FILE_ERROR_STR_NOT_ENOUGH_MEMORY);
#ifdef XVT_MODERN
		return 0;
#endif
	}

#ifdef XVT_MODERN
	if (!source_handle) {
		return 0;
	}
#endif
	source_model = (struct optimized_poly_object *)memory_get_handle_block(
		source_handle);
	if (source_model->self_marker != source_model) {
		opt_model_adjust_optimized_poly_object_pointers(source_model);
	}
	runtime_model = (struct optimized_poly_object *)memory_get_handle_block(
		runtime_handle);
	memcpy(runtime_model, source_model, sizeof(*runtime_model));
	runtime_model->self_marker = runtime_model;
	runtime_model->root_nodes =
		(struct opt_node **)((uint8_t *)runtime_model +
				     sizeof(*runtime_model));
	uint8_t *node_storage = (uint8_t *)(runtime_model->root_nodes +
					    source_model->root_node_count);
	for (root_index = 0; root_index < source_model->root_node_count;
	     ++root_index) {
		runtime_model->root_nodes[root_index] =
			(struct opt_node *)node_storage;
		node_storage += opt_model_build_runtime_node(
			source_model->root_nodes[root_index], &mesh_state,
			node_storage);
	}
	for (root_index = 0; root_index < runtime_model->root_node_count;
	     ++root_index) {
		opt_model_fixup_runtime_texture_pointers(
			runtime_model->root_nodes[root_index], runtime_model,
			source_model);
	}
	XVT_LOG_DEBUG(
		"models.runtime_built handle=%u source=%u bytes=%u roots=%d bpp=%d hardware=%d mip=%d detail=%d",
		(unsigned)runtime_handle, source_handle, serialized_size,
		runtime_model->root_node_count, g_flight_bytes_per_pixel,
		g_use_hardware3d, g_mipmapping_enabled,
		g_texture_resolution_level);
	memory_handle_block_done_stub(runtime_handle);
	memory_handle_block_done_stub(source_handle);
	return runtime_handle;
}

/* Fixes the palette pointer of every texture at or below node in a runtime
 * copy, following OPT_NODEREF links in dst_model. A texture with an inline
 * palette gets inline_palette_count 0 and its pointer set to the palette copied
 * after its texels. A texture whose palette is not its own is pointed at the
 * palette copy of the texture that owned that palette in src_model, when
 * opt_model_find_corresponding_texture_node_in_model finds it. On a 16-bit display
 * each pointer is set 4096 bytes before the copy. */
// FUNCTION: XVT 0x476490
void opt_model_fixup_runtime_texture_pointers(
	struct opt_node *node, struct optimized_poly_object *dst_model,
	struct optimized_poly_object *src_model)
{
	struct opt_node *current_node = node;
	if (current_node != NULL) {
		while (current_node->node_type == OPT_NODEREF) {
			current_node = opt_model_resolve_node_ref(
				dst_model, (const char *)current_node->payload);
			if (current_node == NULL) {
				return;
			}
		}

		if (current_node->node_type == OPT_TEXTURE) {
			struct opt_texture_data *texture_data =
				(struct opt_texture_data *)
					current_node->payload;
			uint8_t *palette;
			int texture_data_size;
			if (texture_data->inline_palette_count != 0) {
				texture_data->inline_palette_count = 0;
				palette = (uint8_t *)texture_data +
					  sizeof(*texture_data);
				texture_data_size = texture_data->width *
						    texture_data->height;
				if (texture_data->texture_size ==
				    texture_data_size) {
					texture_data_size =
						texture_data->data_size;
				}
				palette += texture_data_size;
				if (g_flight_bytes_per_pixel == 2) {
					palette -= 4096;
				}
				texture_data->palette = (uint16_t *)palette;
			} else {
				uint16_t *source_palette =
					texture_data->palette;
				palette = (uint8_t *)source_palette;
				if (g_flight_bytes_per_pixel == 2) {
					palette += 4096;
				}
				palette -= sizeof(*texture_data);
				texture_data_size = texture_data->width *
						    texture_data->height;
				if (texture_data->texture_size ==
				    texture_data_size) {
					texture_data_size =
						texture_data->data_size;
				}
				palette -= texture_data_size;
				if (palette != (uint8_t *)texture_data) {
					struct opt_node *corresponding_node =
						opt_model_find_corresponding_texture_node_in_model(
							dst_model, src_model,
							source_palette);
					if (corresponding_node != NULL) {
						struct opt_texture_data *
							corresponding_texture_data =
								(struct
								 opt_texture_data
									 *)corresponding_node
									->payload;
						palette =
							(uint8_t *)
								corresponding_texture_data +
							sizeof(*corresponding_texture_data);
						texture_data_size =
							corresponding_texture_data
								->width *
							corresponding_texture_data
								->height;
						if (corresponding_texture_data
							    ->texture_size ==
						    texture_data_size) {
							texture_data_size =
								corresponding_texture_data
									->data_size;
						}
						palette += texture_data_size;
						if (g_flight_bytes_per_pixel ==
						    2) {
							palette -= 4096;
						}
						texture_data->palette =
							(uint16_t *)palette;
					}
				}
			}
		}

		int child_index = 0;
		int child_offset = 0;
		if (current_node->child_count > child_index) {
			int child_count;
			do {
				opt_model_fixup_runtime_texture_pointers(
					*(struct opt_node *
						  *)((uint8_t *)current_node
							     ->p_children +
						     child_offset),
					dst_model, src_model);
				child_offset +=
					sizeof(*current_node->p_children);
				++child_index;
				child_count = current_node->child_count;
			} while (child_count > child_index);
		}
	}
}

/* Walks src_node and dst_node side by side and returns the node of dst_node's tree
 * that stands where the source texture whose own palette is source_palette
 * stands, or NULL. A pair of nodes counts only when their types match, and
 * their children are searched only when their child counts match. Does not
 * follow OPT_NODEREF links. */
// FUNCTION: XVT 0x4765B0
struct opt_node *
opt_model_find_corresponding_texture_node(struct opt_node *src_node,
					  struct opt_node *dst_node,
					  const uint16_t *source_palette)
{
	struct opt_node *source = src_node;
	if (source == NULL) {
		return NULL;
	}
	struct opt_node *destination = dst_node;
	if (destination == NULL) {
		return NULL;
	}
	if (destination->node_type != source->node_type) {
		return NULL;
	}

	if (source->node_type == OPT_TEXTURE) {
		struct opt_texture_data *texture_data =
			(struct opt_texture_data *)source->payload;
		uint16_t *embedded_palette =
			(uint16_t *)((uint8_t *)texture_data +
				     sizeof(*texture_data));
		int texture_data_size =
			texture_data->width * texture_data->height;
		if (texture_data->texture_size == texture_data_size) {
			texture_data_size = texture_data->data_size;
		}
		embedded_palette = (uint16_t *)((uint8_t *)embedded_palette +
						texture_data_size);
		if ((texture_data->palette == embedded_palette ||
		     texture_data->inline_palette_count != 0) &&
		    source_palette == embedded_palette) {
			return destination;
		}
	}

	if (destination->child_count != source->child_count) {
		return NULL;
	}
	int child_index = 0;
	int child_offset = 0;
	if (source->child_count > 0) {
		do {
			struct opt_node *result =
				opt_model_find_corresponding_texture_node(
					*(struct opt_node *
						  *)((uint8_t *)source
							     ->p_children +
						     child_offset),
					*(struct opt_node *
						  *)((uint8_t *)destination
							     ->p_children +
						     child_offset),
					source_palette);
			if (result != NULL) {
				return result;
			}
			child_offset += sizeof(*source->p_children);
			++child_index;
		} while (source->child_count > child_index);
	}
	return NULL;
}

/* Runs opt_model_find_corresponding_texture_node on each pair of roots of src_model
 * and dst_model, in order, and returns its first match, or NULL. Uses src_model's
 * root count for both. */
// FUNCTION: XVT 0x476670
struct opt_node *opt_model_find_corresponding_texture_node_in_model(
	const struct optimized_poly_object *dst_model,
	const struct optimized_poly_object *src_model,
	const uint16_t *source_palette)
{
	int root_offset = 0;
	int root_index = 0;
	if (src_model->root_node_count > 0) {
		do {
			struct opt_node *result =
				opt_model_find_corresponding_texture_node(
					*(struct opt_node *
						  *)((uint8_t *)src_model
							     ->root_nodes +
						     root_offset),
					*(struct opt_node *
						  *)((uint8_t *)dst_model
							     ->root_nodes +
						     root_offset),
					source_palette);
			if (result != NULL) {
				return result;
			}
			root_offset += sizeof(*src_model->root_nodes);
			++root_index;
		} while (src_model->root_node_count > root_index);
	}
	return NULL;
}

#ifndef XVT_MODERN
/* Writes a packed model to filename: the marker -1 when g_opt_source_is_version0
 * is set, else -2, then the size, then that many bytes of the model's block.
 * The size is 4 bytes per root plus 14 plus what
 * opt_model_measure_node_and_raise_capacities gives for each root, which also raises
 * the renderer's capacity counts. Sets g_cur_mesh_vertices, g_cur_mesh_tex_coords,
 * g_cur_vert_normals, g_model_node_walk_unused_scratch2 and g_cur_mesh_materials to
 * NULL and g_cur_vertex_count to 0 first. Does nothing when the file does not
 * open. Only the original build calls this. */
// FUNCTION: XVT 0x4766C0
void opt_model_save_handle_to_file(const char *filename, uint16_t handle)
{
	fe_disk_io_open_global_stream(filename, "wb", 0, 1);
	xvt_file *stream = g_stream;
	if (stream != NULL) {
		struct optimized_poly_object *model =
			memory_get_handle_block(handle);
		if (model->self_marker != model) {
			opt_model_adjust_optimized_poly_object_pointers(model);
		}
		struct scene_mesh parent_state;
		memset(&parent_state, 0, sizeof(parent_state));
		int root_index = 0;
		g_cur_mesh_vertices = NULL;
		g_cur_mesh_tex_coords = NULL;
		g_cur_vert_normals = NULL;
		g_model_node_walk_unused_scratch2 = NULL;
		g_cur_mesh_materials = NULL;
		g_cur_vertex_count = 0;
		size_t serialized_size = 4 * model->root_node_count + 14;
		if (model->root_node_count > 0) {
			int root_offset = 0;
			do {
				serialized_size +=
					opt_model_measure_node_and_raise_capacities(
						*(struct opt_node *
							  *)((uint8_t *)model
								     ->root_nodes +
							     root_offset),
						&parent_state);
				root_offset += sizeof(*model->root_nodes);
				++root_index;
			} while (model->root_node_count > root_index);
		}
		int file_version = 1;
		if (!g_opt_source_is_version0) {
			file_version = 2;
		}
		file_version = -file_version;
		FILE_RAW_WRITE(&file_version, 1, sizeof(file_version), stream);
		file_version = -file_version;
		FILE_RAW_WRITE(&serialized_size, 1, sizeof(serialized_size),
			       stream);
		FILE_RAW_WRITE(model, 1, serialized_size, stream);
		FILE_RAW_CLOSE(stream);
		memory_handle_block_done_stub(handle);
	}
}
#endif

/* Returns the bytes node and everything below it take in a packed model: the
 * opt_node, its name and the payload of its type. A face node counts its edge
 * count word, 64 bytes per face for the records and 36 for the normal and
 * gradients, and room for g_cur_vertex_count vertex normals when
 * parent_state->p_vert_normals is NULL. Raises g_scene_edge_flags_capacity to the
 * largest face node edge count and g_vertex_remap_capacity to the largest vertex
 * count. Sets g_cur_vertex_count, g_cur_mesh_materials, g_cur_vert_normals and
 * parent_state->p_vert_normals at those nodes, and sets g_cur_mesh_vertices,
 * g_cur_mesh_tex_coords, g_cur_vert_normals, g_model_node_walk_unused_scratch2 and
 * g_cur_mesh_materials to NULL before a node's children. Returns 0 for a NULL
 * node. An OPT_TEXTURE node with a NULL payload is read through that NULL
 * pointer. */
// FUNCTION: XVT 0x476810
unsigned int
opt_model_measure_node_and_raise_capacities(const struct opt_node *node,
					    struct scene_mesh *parent_state)
{
	if (node == NULL) {
		return 0;
	}

	unsigned int serialized_size = sizeof(struct opt_node);

	if (node->p_name != NULL) {

		serialized_size = (unsigned int)strlen(node->p_name) +
				  sizeof(struct opt_node) + 1;
	}

	int *param_data = node->payload;
	opt_node_type node_type = node->node_type;
	if (param_data != NULL) {
		switch (node_type) {
		case OPT_FACEDATA:
		case OPT_FACEDATA_QUAD_MESH:
		case OPT_FACEDATA_FACE_SET:
		case OPT_FACEDATA_TRIANGLE_STRIP_SET:
			if (g_scene_edge_flags_capacity < param_data[0]) {
				g_scene_edge_flags_capacity = param_data[0];
			}
			serialized_size += 4;
			serialized_size += (unsigned int)node->payload_count
					   << 6;
			serialized_size += 36 * node->payload_count;
			if (parent_state->p_vert_normals == NULL) {
				serialized_size += 12 * g_cur_vertex_count;
			}
			break;

		case OPT_TRANSFORM:
			serialized_size += 48;
			break;

		case OPT_MESHVERTS:
			g_cur_vertex_count = node->payload_count;
			serialized_size += 12 * g_cur_vertex_count;
			if (g_vertex_remap_capacity < node->payload_count) {
				g_vertex_remap_capacity = node->payload_count;
			}
			break;

		case OPT_TRANSLATION:
			serialized_size += 12;
			break;

		case OPT_ROTATION:
			serialized_size += 36;
			break;

		case OPT_SCALE:
			serialized_size += 12;
			break;

		case OPT_NODEREF:
			serialized_size +=
				(unsigned int)strlen((const char *)param_data) +
				1;
			break;

		case OPT_MATERIAL: {
			int record_count = node->payload_count;
			g_cur_mesh_materials = node->payload;
			int scaled_record_count = record_count << 3;
			scaled_record_count -= record_count;
			serialized_size += scaled_record_count << 3;
			break;
		}

		case OPT_VERTNORMALS: {
			int vector_value_count = 3 * node->payload_count;
			g_cur_vert_normals = node->payload;
			serialized_size += 4 * vector_value_count;
			parent_state->p_vert_normals =
				(struct opt_vector *)param_data;
			break;
		}

		case OPT_TEXCOORDS:
			serialized_size += 8 * node->payload_count;
			break;

		case OPT_BASE_COLOR:
			serialized_size += 12;
			break;

		case OPT_TEXTURE: {
			struct opt_texture_data *texture_data = node->payload;
			serialized_size += sizeof(*texture_data);
			int texture_byte_count =
				texture_data->height * texture_data->width;
			if (texture_byte_count == texture_data->texture_size) {
				serialized_size += texture_data->data_size;
			} else {
				serialized_size += texture_byte_count;
			}
			if (texture_data->inline_palette_count != 0) {
				serialized_size +=
					768 *
					texture_data->inline_palette_count;
			} else {
				uint8_t *embedded_palette =
					(uint8_t *)(texture_data + 1);
				if (texture_byte_count ==
				    texture_data->texture_size) {
					embedded_palette +=
						texture_data->data_size;
				} else {
					embedded_palette += texture_byte_count;
				}
				if ((uint8_t *)texture_data->palette ==
				    embedded_palette) {
					serialized_size += 12288;
				}
			}
			break;
		}

		case OPT_FACEGROUP:
			serialized_size += 4 * node->payload_count;
			break;

		case OPT_HARDPOINT:
			serialized_size += 16;
			break;

		case OPT_ROTSCALE:
			serialized_size += 48;
			break;

		case OPT_MESHDESC:
			serialized_size += 72;
			break;

		default:
			break;
		}
	} else if (node_type == OPT_TEXTURE) {
		struct opt_texture_data *texture_data = node->payload;
		serialized_size += sizeof(*texture_data);
		int texture_byte_count =
			texture_data->height * texture_data->width;
		if (texture_byte_count == texture_data->texture_size) {
			serialized_size += texture_data->data_size;
		} else {
			serialized_size += texture_byte_count;
		}
		if (texture_data->inline_palette_count != 0) {
			serialized_size +=
				768 * texture_data->inline_palette_count;
		} else {
			uint8_t *embedded_palette =
				(uint8_t *)(texture_data + 1);
			if (texture_byte_count == texture_data->texture_size) {
				embedded_palette += texture_data->data_size;
			} else {
				embedded_palette += texture_byte_count;
			}
			if ((uint8_t *)texture_data->palette ==
			    embedded_palette) {
				serialized_size += 12288;
			}
		}
	}

	if (node->child_count != 0) {
		struct scene_mesh child_state = *parent_state;
		g_cur_mesh_vertices = NULL;
		g_cur_mesh_tex_coords = NULL;
		g_cur_vert_normals = NULL;
		g_model_node_walk_unused_scratch2 = NULL;
		g_cur_mesh_materials = NULL;
		int child_index = 0;

		serialized_size +=
			sizeof(struct opt_node *) * node->child_count;

		if (node->child_count > 0) {
			int child_offset = 0;
			do {
				serialized_size +=
					opt_model_measure_node_and_raise_capacities(
						*(struct opt_node *
							  *)((uint8_t *)node
								     ->p_children +
							     child_offset),
						&child_state);
				child_offset += sizeof(*node->p_children);
				++child_index;
			} while (node->child_count > child_index);
		}
	}
	return serialized_size;
}

/* Rewrites entry_count RGB565 entries of palette, in place, in the display's
 * 16-bit format at the current brightness (flight_palette_build16_bpp_range). With
 * g_use_hardware3d set it first runs model_texture_filter_hardware_palette on the
 * palette. Does not check entry_count against its 4096-entry buffer. */
// FUNCTION: XVT 0x476B90
void opt_model_prepare_texture_palette(uint16_t *palette, int entry_count)
{
	if (g_use_hardware3d != 0) {
		model_texture_filter_hardware_palette(palette);
	}

	struct rgb_triplet src_rgb[4096];
	if (entry_count > 0) {
		uint8_t *rgb_cursor = (uint8_t *)src_rgb;
		uint16_t *palette_entry = palette;
		int entries_remaining = entry_count;
		do {
			unsigned int packed_color = *palette_entry++;
			uint8_t blue = (uint8_t)(packed_color & 0x1Fu);
			packed_color >>= 5;
			rgb_cursor[2] = (uint8_t)(2 * blue);
			uint8_t green = (uint8_t)(packed_color & 0x3Fu);
			packed_color >>= 6;
			uint8_t red = (uint8_t)(packed_color & 0x1Fu);
			rgb_cursor[1] = green;
			rgb_cursor[0] = (uint8_t)(2 * red);
			rgb_cursor += 3;
		} while (--entries_remaining != 0);
	}

	flight_palette_build16_bpp_range(src_rgb, palette, 0, entry_count);
}

/* Returns the bytes src_node and everything below it take in a runtime model;
 * with dst NULL it only measures, otherwise it also writes them there. Copies
 * each node with its name, child table and payload. A texture whose texture_size
 * equals width times height, its data holding the mip levels, drops the top
 * level when g_texture_resolution_level is 0 and both sides are over 8. Any other
 * texture copies its top level and gets mip levels added until a side is 1.
 * With g_mipmapping_enabled set each new texel is the 2-by-2 average of the
 * colors 8192 bytes into the palette, matched back to the nearest of the 256
 * there; otherwise the levels' bytes are left unwritten. Palettes are converted
 * for the display: on an 8-bit display the 4096 RGB565 colors map through
 * g_active_rgb565_to_palette_index_lut to 4096 bytes, and with
 * g_generate_mission_palette set the texels feed
 * image_quantizer_classify_indexed_rgb565_image once per 256-color sub-palette; on
 * a 16-bit display the 8192 bytes are copied and repacked by
 * opt_model_prepare_texture_palette. A texture that uses another's palette copies
 * none. Raises g_scene_edge_flags_capacity and g_vertex_remap_capacity, sets
 * g_cur_vertex_count, g_cur_mesh_materials, g_cur_vert_normals and
 * mesh_state->p_vert_normals at those nodes, and sets g_cur_mesh_vertices,
 * g_cur_mesh_tex_coords, g_cur_vert_normals, g_model_node_walk_unused_scratch2 and
 * g_cur_mesh_materials to NULL before a node's children. A child of size 0 leaves
 * a NULL slot. The modern build aligns each node. */
// FUNCTION: XVT 0x476C20
unsigned int opt_model_build_runtime_node(const struct opt_node *src_node,
					  struct scene_mesh *mesh_state,
					  uint8_t *dst)
{
	enum {
		OPT_TEXTURE_PALETTE_ENTRY_COUNT = 4096,
		OPT_TEXTURE_SUBPALETTE_COUNT = 16,
		OPT_TEXTURE_SUBPALETTE_ENTRY_COUNT = 256,
		OPT_TEXTURE_FULL_RES_THRESHOLD = 8,
		RGB565_GREEN_SHIFT = 5,
		RGB565_GREEN_BITS = 6,
		RGB565_GREEN_MASK = 0x3f,
		RGB565_RED_BLUE_MASK = 0x1f,
	};

	if (src_node == NULL) {
		return 0;
	}
	struct opt_node *runtime_node;
	if (dst != NULL) {
		memcpy(dst, src_node, sizeof(*src_node));
		runtime_node = (struct opt_node *)dst;
		dst += sizeof(*src_node);
	}
	unsigned int total_size = sizeof(*src_node);
	if (src_node->p_name != NULL) {
		if (dst != NULL) {
			runtime_node->p_name = (char *)dst;
			strcpy((char *)dst, src_node->p_name);
			dst += strlen(src_node->p_name) + 1;
		}
		total_size = (unsigned int)strlen(src_node->p_name) +
			     sizeof(*src_node) + 1;
	}
#ifdef XVT_MODERN
	total_size = (unsigned int)xvt_opt_align_size(total_size);
	if (dst) {
		dst = xvt_opt_align_pointer(dst);
	}
#endif
	if (src_node->child_count != 0) {
		if (dst != NULL) {
			runtime_node->p_children = (struct opt_node **)dst;
			dst += sizeof(*src_node->p_children) *
			       (unsigned int)src_node->child_count;
		}
		total_size += sizeof(*src_node->p_children) *
			      (unsigned int)src_node->child_count;
	}

	void *source_payload = src_node->payload;
	unsigned int payload_size;
	switch (src_node->node_type) {
	case OPT_FACEDATA:
	case OPT_FACEDATA_QUAD_MESH:
	case OPT_FACEDATA_FACE_SET:
	case OPT_FACEDATA_TRIANGLE_STRIP_SET:
		if (g_scene_edge_flags_capacity <
		    *(const int *)source_payload) {
			g_scene_edge_flags_capacity =
				*(const int *)source_payload;
		}
		payload_size =
			sizeof(int) +
			(unsigned int)src_node->payload_count *
				(sizeof(struct opt_packed_face_record) + 36u);
		if (mesh_state->p_vert_normals == NULL) {
			payload_size += sizeof(struct opt_vector) *
					(unsigned int)g_cur_vertex_count;
		}
		if (dst != NULL) {
			runtime_node->payload = dst;
			memcpy(dst, src_node->payload, payload_size);
			dst += payload_size;
		}
		total_size += payload_size;
		break;
	case OPT_TRANSFORM:
		payload_size = 48;
		if (dst != NULL) {
			runtime_node->payload = dst;
			memcpy(dst, src_node->payload, payload_size);
			dst += payload_size;
		}
		total_size += payload_size;
		break;
	case OPT_MESHVERTS:
		payload_size = sizeof(struct opt_vector) *
			       (unsigned int)src_node->payload_count;
		if (dst != NULL) {
			runtime_node->payload = dst;
			memcpy(dst, src_node->payload, payload_size);
			dst += payload_size;
		}
		total_size += payload_size;
		g_cur_vertex_count = src_node->payload_count;
		if (src_node->payload_count > g_vertex_remap_capacity) {
			g_vertex_remap_capacity = src_node->payload_count;
		}
		break;
	case OPT_TRANSLATION:
		payload_size = 12;
		if (dst != NULL) {
			runtime_node->payload = dst;
			memcpy(dst, src_node->payload, payload_size);
			dst += payload_size;
		}
		total_size += payload_size;
		break;
	case OPT_ROTATION:
		payload_size = 36;
		if (dst != NULL) {
			runtime_node->payload = dst;
			memcpy(dst, src_node->payload, payload_size);
			dst += payload_size;
		}
		total_size += payload_size;
		break;
	case OPT_SCALE:
		payload_size = 12;
		if (dst != NULL) {
			runtime_node->payload = dst;
			memcpy(dst, src_node->payload, payload_size);
			dst += payload_size;
		}
		total_size += payload_size;
		break;
	case OPT_NODEREF:
		payload_size =
			(unsigned int)strlen((const char *)source_payload) + 1;
		if (dst != NULL) {
			runtime_node->payload = dst;
			memcpy(dst, src_node->payload, payload_size);
			dst += payload_size;
		}
		total_size += payload_size;
		break;
	case OPT_MATERIAL:
		payload_size = 56u * (unsigned int)src_node->payload_count;
		if (dst != NULL) {
			runtime_node->payload = dst;
			memcpy(dst, src_node->payload, payload_size);
			dst += payload_size;
		}
		g_cur_mesh_materials = source_payload;
		total_size += payload_size;
		break;
	case OPT_VERTNORMALS:
		payload_size = sizeof(struct opt_vector) *
			       (unsigned int)src_node->payload_count;
		if (dst != NULL) {
			runtime_node->payload = dst;
			memcpy(dst, src_node->payload, payload_size);
			dst += payload_size;
		}
		g_cur_vert_normals = (struct opt_vector *)source_payload;
		total_size += payload_size;
		mesh_state->p_vert_normals =
			(struct opt_vector *)source_payload;
		break;
	case OPT_TEXCOORDS:
		payload_size = 8u * (unsigned int)src_node->payload_count;
		if (dst != NULL) {
			runtime_node->payload = dst;
			memcpy(dst, src_node->payload, payload_size);
			dst += payload_size;
		}
		total_size += payload_size;
		break;
	case OPT_BASE_COLOR:
		payload_size = 12;
		if (dst != NULL) {
			runtime_node->payload = dst;
			memcpy(dst, src_node->payload, payload_size);
			dst += payload_size;
		}
		total_size += payload_size;
		break;
	case OPT_TEXTURE: {
		const struct opt_texture_data *source_texture =
			(const struct opt_texture_data *)src_node->payload;
		int palette_index;
		const uint8_t *source_palette;
		const uint16_t *source_palette16;
		const uint8_t *source_texels;
		if (dst != NULL && g_generate_mission_palette != 0 &&
		    g_flight_bytes_per_pixel == 1) {
			source_texels = (const uint8_t *)source_texture +
					sizeof(*source_texture);
			source_palette =
				source_texels +
				source_texture->height * source_texture->width;
			if ((unsigned int)source_texture->texture_size ==
			    (unsigned int)(source_texture->height *
					   source_texture->width)) {
				source_palette = source_texels +
						 source_texture->data_size;
			}
			source_palette16 =
				(const uint16_t
					 *)(source_palette +
					    OPT_TEXTURE_PALETTE_ENTRY_COUNT);
			for (palette_index = OPT_TEXTURE_SUBPALETTE_COUNT;
			     palette_index != 0; --palette_index) {
				image_quantizer_classify_indexed_rgb565_image(
					source_texels, source_palette16,
					(unsigned int)source_texture->width,
					(unsigned int)source_texture->height);
				source_palette16 +=
					OPT_TEXTURE_SUBPALETTE_ENTRY_COUNT;
			}
		}

		struct opt_texture_data *runtime_texture;
		unsigned int texture_payload_size;
		if ((unsigned int)source_texture->texture_size ==
		    (unsigned int)(source_texture->height *
				   source_texture->width)) {
			if (dst != NULL) {
				runtime_node->payload = dst;
				memcpy(dst, src_node->payload,
				       sizeof(*source_texture));
				source_texels =
					(const uint8_t *)source_texture +
					sizeof(*source_texture);
				dst += sizeof(*source_texture);
				runtime_texture = (struct opt_texture_data *)
							  runtime_node->payload;
				if (g_texture_resolution_level == 0 &&
				    runtime_texture->width >
					    OPT_TEXTURE_FULL_RES_THRESHOLD &&
				    runtime_texture->height >
					    OPT_TEXTURE_FULL_RES_THRESHOLD) {
					source_texels +=
						runtime_texture->height *
						runtime_texture->width;
					runtime_texture->data_size -=
						runtime_texture->height *
						runtime_texture->width;
					runtime_texture->width >>= 1;
					runtime_texture->height >>= 1;
					runtime_texture->texture_size =
						runtime_texture->width *
						runtime_texture->height;
				}
				memcpy(dst, source_texels,
				       (unsigned int)
					       runtime_texture->data_size);
				dst += runtime_texture->data_size;
				texture_payload_size =
					(unsigned int)
						runtime_texture->data_size +
					sizeof(*source_texture);
			} else {
				texture_payload_size =
					(unsigned int)
						source_texture->data_size +
					sizeof(*source_texture);
				if (g_texture_resolution_level == 0 &&
				    source_texture->width >
					    OPT_TEXTURE_FULL_RES_THRESHOLD &&
				    source_texture->height >
					    OPT_TEXTURE_FULL_RES_THRESHOLD) {
					texture_payload_size -=
						(unsigned int)(source_texture
								       ->height *
							       source_texture
								       ->width);
				}
			}
		} else {
			texture_payload_size =
				(unsigned int)(source_texture->height *
					       source_texture->width) +
				sizeof(*source_texture);
			int width;
			int height;
			if (dst != NULL) {
				runtime_node->payload = dst;
				memcpy(dst, src_node->payload,
				       texture_payload_size);
				dst += texture_payload_size;
				source_texels =
					(const uint8_t *)source_texture +
					sizeof(*source_texture);
				if (source_texture->inline_palette_count == 0) {
					source_palette =
						(const uint8_t *)
							source_texture->palette;
				} else {
					source_palette =
						source_texels +
						source_texture->height *
							source_texture->width;
				}
				source_palette16 =
					(const uint16_t
						 *)(source_palette +
						    2 * OPT_TEXTURE_PALETTE_ENTRY_COUNT);
				width = source_texture->width;
				height = source_texture->height;
				runtime_texture = (struct opt_texture_data *)
							  runtime_node->payload;
				runtime_texture->texture_size = width * height;
				runtime_texture->data_size = width * height;
				while (width > 1 && height > 1) {
					width >>= 1;
					height >>= 1;
					int mip_pixel_count = width * height;
					runtime_texture->data_size +=
						mip_pixel_count;
					if (g_mipmapping_enabled != 0 &&
					    height > 0) {
						int previous_width = 2 * width;
						int row_step = 4 * width;
						uint8_t *mip_row = dst;
						const uint8_t *mip_top_row =
							source_texels;
						const uint8_t *mip_bottom_row =
							source_texels +
							previous_width;
						for (int mip_y = 0;
						     mip_y < height; ++mip_y) {
							const uint8_t *top_texel =
								mip_top_row;
							const uint8_t *bottom_texel =
								mip_bottom_row;
							for (int mip_x = 0;
							     mip_x < width;
							     ++mip_x) {
								uint16_t packed_color = source_palette16
									[top_texel
										 [0]];
								int blue =
									packed_color &
									RGB565_RED_BLUE_MASK;
								packed_color >>=
									RGB565_GREEN_SHIFT;
								int green =
									packed_color &
									RGB565_GREEN_MASK;
								packed_color >>=
									RGB565_GREEN_BITS;
								int red =
									packed_color &
									RGB565_RED_BLUE_MASK;
								packed_color = source_palette16
									[top_texel
										 [1]];
								blue += packed_color &
									RGB565_RED_BLUE_MASK;
								packed_color >>=
									RGB565_GREEN_SHIFT;
								green +=
									packed_color &
									RGB565_GREEN_MASK;
								packed_color >>=
									RGB565_GREEN_BITS;
								red += packed_color &
								       RGB565_RED_BLUE_MASK;
								packed_color = source_palette16
									[bottom_texel
										 [0]];
								blue += packed_color &
									RGB565_RED_BLUE_MASK;
								packed_color >>=
									RGB565_GREEN_SHIFT;
								green +=
									packed_color &
									RGB565_GREEN_MASK;
								packed_color >>=
									RGB565_GREEN_BITS;
								red += packed_color &
								       RGB565_RED_BLUE_MASK;
								packed_color = source_palette16
									[bottom_texel
										 [1]];
								blue += packed_color &
									RGB565_RED_BLUE_MASK;
								packed_color >>=
									RGB565_GREEN_SHIFT;
								green +=
									packed_color &
									RGB565_GREEN_MASK;
								packed_color >>=
									RGB565_GREEN_BITS;
								red += packed_color &
								       RGB565_RED_BLUE_MASK;
								blue >>= 2;
								green >>= 2;
								red >>= 2;
								mip_row[mip_x] = color_find_nearest_rgb565_index(
									source_palette16,
									red,
									green,
									blue, 0,
									OPT_TEXTURE_SUBPALETTE_ENTRY_COUNT);
								top_texel += 2;
								bottom_texel +=
									2;
							}
							mip_row += width;
							mip_top_row += row_step;
							mip_bottom_row +=
								row_step;
						}
					}
					source_texels = dst;
					dst += mip_pixel_count;
					texture_payload_size +=
						(unsigned int)mip_pixel_count;
				}
			} else {
				width = source_texture->width;
				height = source_texture->height;
				while (width > 1 && height > 1) {
					width >>= 1;
					height >>= 1;
					texture_payload_size +=
						(unsigned int)(width * height);
				}
			}
		}

		source_texture =
			(const struct opt_texture_data *)src_node->payload;
		unsigned int source_palette_offset;
		if (source_texture->inline_palette_count != 0) {
			texture_payload_size +=
				(unsigned int)(source_texture
						       ->inline_palette_count *
					       g_flight_bytes_per_pixel) *
				OPT_TEXTURE_SUBPALETTE_ENTRY_COUNT;
			if (dst != NULL) {
				source_palette =
					(const uint8_t *)source_texture +
					sizeof(*source_texture);
				source_palette_offset =
					(unsigned int)(source_texture->height *
						       source_texture->width);
				if ((unsigned int)
					    source_texture->texture_size ==
				    source_palette_offset) {
					source_palette_offset =
						(unsigned int)source_texture
							->data_size;
				}
				source_palette += source_palette_offset;
				if (g_flight_bytes_per_pixel == 2) {
					source_palette +=
						(unsigned int)source_texture
							->inline_palette_count *
						OPT_TEXTURE_SUBPALETTE_ENTRY_COUNT;
				}
				if (g_flight_bytes_per_pixel == 1) {
					source_palette16 =
						(const uint16_t
							 *)(source_palette +
							    OPT_TEXTURE_PALETTE_ENTRY_COUNT);
					for (palette_index = 0;
					     palette_index <
					     OPT_TEXTURE_PALETTE_ENTRY_COUNT;
					     ++palette_index) {
						dst[palette_index] = g_active_rgb565_to_palette_index_lut
							[source_palette16
								 [palette_index]];
					}
				} else {
					memcpy(dst, source_palette,
					       (unsigned int)(g_flight_bytes_per_pixel *
							      OPT_TEXTURE_PALETTE_ENTRY_COUNT));
				}
				if (g_flight_bytes_per_pixel == 2) {
					if (src_node->p_name != NULL) {
#ifdef XVT_MODERN
						XVT_LOG_DEBUG(
							"models.node_palette node=\"%s\"",
							src_node->p_name);
#else
						debug_printf("%s:",
							     src_node->p_name);
#endif
					}
					opt_model_prepare_texture_palette(
						(uint16_t *)dst,
						OPT_TEXTURE_PALETTE_ENTRY_COUNT);
				}
				dst += (unsigned int)(source_texture
							      ->inline_palette_count *
						      g_flight_bytes_per_pixel) *
				       OPT_TEXTURE_SUBPALETTE_ENTRY_COUNT;
			}
		} else {
			source_palette = (const uint8_t *)source_texture +
					 sizeof(*source_texture);
			source_palette_offset =
				(unsigned int)(source_texture->height *
					       source_texture->width);
			if ((unsigned int)source_texture->texture_size ==
			    source_palette_offset) {
				source_palette_offset =
					(unsigned int)source_texture->data_size;
			}
			source_palette += source_palette_offset;
			if ((const uint8_t *)source_texture->palette ==
			    source_palette) {
				unsigned int palette_bytes =
					(unsigned int)g_flight_bytes_per_pixel *
					OPT_TEXTURE_PALETTE_ENTRY_COUNT;
				texture_payload_size += palette_bytes;
				if (dst != NULL) {
					if (g_flight_bytes_per_pixel == 2) {
						source_palette +=
							OPT_TEXTURE_PALETTE_ENTRY_COUNT;
					}
					if (g_flight_bytes_per_pixel == 1) {
						source_palette16 =
							(const uint16_t
								 *)(source_palette +
								    OPT_TEXTURE_PALETTE_ENTRY_COUNT);
						for (palette_index = 0;
						     palette_index <
						     OPT_TEXTURE_PALETTE_ENTRY_COUNT;
						     ++palette_index) {
							dst[palette_index] = g_active_rgb565_to_palette_index_lut
								[source_palette16
									 [palette_index]];
						}
					} else {
						memcpy(dst, source_palette,
						       palette_bytes);
					}
					if (g_flight_bytes_per_pixel == 2) {
						if (src_node->p_name != NULL) {
#ifdef XVT_MODERN
							XVT_LOG_DEBUG(
								"models.node_palette node=\"%s\"",
								src_node->p_name);
#else
							debug_printf(
								"%s:",
								src_node->p_name);
#endif
						}
						opt_model_prepare_texture_palette(
							(uint16_t *)dst,
							OPT_TEXTURE_PALETTE_ENTRY_COUNT);
					}
					dst += (unsigned int)
						       g_flight_bytes_per_pixel *
					       OPT_TEXTURE_PALETTE_ENTRY_COUNT;
				}
			}
		}
		total_size += texture_payload_size;
		break;
	}
	case OPT_FACEGROUP:
		payload_size = 4u * (unsigned int)src_node->payload_count;
		if (dst != NULL) {
			runtime_node->payload = dst;
			memcpy(dst, src_node->payload, payload_size);
			dst += payload_size;
		}
		total_size += payload_size;
		break;
	case OPT_HARDPOINT:
		payload_size = 16;
		if (dst != NULL) {
			runtime_node->payload = dst;
			memcpy(dst, src_node->payload, payload_size);
			dst += payload_size;
		}
		total_size += payload_size;
		break;
	case OPT_ROTSCALE:
		payload_size = 48;
		if (dst != NULL) {
			runtime_node->payload = dst;
			memcpy(dst, src_node->payload, payload_size);
			dst += payload_size;
		}
		total_size += payload_size;
		break;
	case OPT_MESHDESC:
		payload_size = 72;
		if (dst != NULL) {
			runtime_node->payload = dst;
			memcpy(dst, src_node->payload, payload_size);
			dst += payload_size;
		}
		total_size += payload_size;
		break;
	default:
		break;
	}
#ifdef XVT_MODERN
	total_size = (unsigned int)xvt_opt_align_size(total_size);
	if (dst) {
		dst = xvt_opt_align_pointer(dst);
	}
#endif
	if (src_node->child_count != 0) {
		struct scene_mesh child_mesh = *mesh_state;
		g_cur_mesh_vertices = NULL;
		g_cur_mesh_tex_coords = NULL;
		g_cur_vert_normals = NULL;
		g_model_node_walk_unused_scratch2 = NULL;
		g_cur_mesh_materials = NULL;
		for (int child_index = 0; child_index < src_node->child_count;
		     ++child_index) {
			if (dst != NULL) {
				((struct opt_node **)runtime_node
					 ->p_children)[child_index] =
					(struct opt_node *)dst;
			}
			payload_size = opt_model_build_runtime_node(
				src_node->p_children[child_index], &child_mesh,
				dst);
			if (dst != NULL) {
				if (payload_size == 0) {
					((struct opt_node **)runtime_node
						 ->p_children)[child_index] =
						NULL;
				}
				dst += payload_size;
			}
			total_size += payload_size;
		}
	}
#ifdef XVT_MODERN
	return (unsigned int)xvt_opt_align_size(total_size);
#else
	return total_size;
#endif
}

#ifndef XVT_MODERN
/* Packs an imported Inventor model into the model layout the game reads and
 * returns the new handle. It measures each root with
 * opt_model_calculate_packed_node_size_recursive, allocates the packed block and a
 * scratch block of g_opt_import_scratch_vector_count vectors (set to 1), packs each
 * root with opt_model_convert_imported_node_to_packed_recursive, frees source_handle
 * and the scratch block, then copies the packed bytes into a block of the exact
 * size, fixes its pointers with opt_model_adjust_optimized_poly_object_pointers and
 * frees the first block. The conversion state starts zeroed with identity view
 * matrices. Sets g_cur_mesh_vertices, g_cur_mesh_tex_coords, g_cur_vert_normals,
 * g_model_node_walk_unused_scratch2 and g_cur_mesh_materials to NULL and
 * g_cur_vertex_count to 0 before each pass. Does not check its allocations. Only
 * the original build calls this. */
// FUNCTION: XVT 0x477950
uint16_t opt_model_convert_imported_handle_to_packed(uint16_t source_handle)
{
	struct scene_mesh conversion_state;

	memset(&conversion_state, 0, sizeof(conversion_state));
	conversion_state.view_orient[0] = 1.0f;
	conversion_state.view_orient[1] = 0.0f;
	conversion_state.view_orient[2] = 0.0f;
	conversion_state.view_orient[3] = 0.0f;
	conversion_state.view_orient[4] = 1.0f;
	conversion_state.view_orient[5] = 0.0f;
	conversion_state.view_orient[6] = 0.0f;
	conversion_state.view_orient[7] = 0.0f;
	conversion_state.view_orient[8] = 1.0f;
	conversion_state.view_to_model_orient[0] = 1.0f;
	conversion_state.view_to_model_orient[1] = 0.0f;
	conversion_state.view_to_model_orient[2] = 0.0f;
	conversion_state.view_to_model_orient[3] = 0.0f;
	conversion_state.view_to_model_orient[4] = 1.0f;
	conversion_state.view_to_model_orient[5] = 0.0f;
	conversion_state.view_to_model_orient[6] = 0.0f;
	conversion_state.view_to_model_orient[7] = 0.0f;
	conversion_state.view_to_model_orient[8] = 1.0f;
	struct optimized_poly_object *source_model =
		memory_get_handle_block(source_handle);
	if (source_model->self_marker != source_model) {
		opt_model_relocate_loaded_pointers(source_model);
	}
	g_cur_mesh_vertices = NULL;
	g_cur_mesh_tex_coords = NULL;
	g_cur_vert_normals = NULL;
	g_model_node_walk_unused_scratch2 = NULL;
	g_cur_mesh_materials = NULL;
	g_cur_vertex_count = 0;
	g_opt_import_scratch_vector_count = 1;
	size_t allocated_size =
		sizeof(*source_model) + sizeof(*source_model->root_nodes) *
						source_model->root_node_count;
	int root_index;
	for (root_index = 0; root_index < source_model->root_node_count;
	     ++root_index) {
		allocated_size +=
			opt_model_calculate_packed_node_size_recursive(
				source_model,
				source_model->root_nodes[root_index],
				&conversion_state);
	}
	memory_handle_block_done_stub(source_handle);

	uint16_t packed_handle = memory_alloc_handle(allocated_size, 0);
	uint16_t scratch_handle =
		memory_alloc_handle(sizeof(*g_opt_import_scratch_vectors) *
					    g_opt_import_scratch_vector_count,
				    0);
	source_model = memory_get_handle_block(source_handle);
	if (source_model->self_marker != source_model) {
		opt_model_relocate_loaded_pointers(source_model);
	}
	struct optimized_poly_object *packed_model =
		memory_get_handle_block(packed_handle);
	g_opt_import_scratch_vectors = memory_get_handle_block(scratch_handle);
	packed_model->self_marker = packed_model;
	packed_model->reserved = packed_handle;
	packed_model->root_node_count = source_model->root_node_count;
	packed_model->root_nodes =
		(struct opt_node **)((uint8_t *)packed_model +
				     sizeof(*packed_model));
	uint8_t *packed_cursor = (uint8_t *)packed_model +
				 sizeof(*packed_model) +
				 sizeof(*packed_model->root_nodes) *
					 packed_model->root_node_count;
	size_t packed_size =
		sizeof(*packed_model) + sizeof(*packed_model->root_nodes) *
						packed_model->root_node_count;

	memset(&conversion_state, 0, sizeof(conversion_state));
	conversion_state.view_orient[0] = 1.0f;
	conversion_state.view_orient[1] = 0.0f;
	conversion_state.view_orient[2] = 0.0f;
	conversion_state.view_orient[3] = 0.0f;
	conversion_state.view_orient[4] = 1.0f;
	conversion_state.view_orient[5] = 0.0f;
	conversion_state.view_orient[6] = 0.0f;
	conversion_state.view_orient[7] = 0.0f;
	conversion_state.view_orient[8] = 1.0f;
	conversion_state.view_to_model_orient[0] = 1.0f;
	conversion_state.view_to_model_orient[1] = 0.0f;
	conversion_state.view_to_model_orient[2] = 0.0f;
	conversion_state.view_to_model_orient[3] = 0.0f;
	conversion_state.view_to_model_orient[4] = 1.0f;
	conversion_state.view_to_model_orient[5] = 0.0f;
	conversion_state.view_to_model_orient[6] = 0.0f;
	conversion_state.view_to_model_orient[7] = 0.0f;
	conversion_state.view_to_model_orient[8] = 1.0f;
	g_cur_mesh_vertices = NULL;
	g_cur_mesh_tex_coords = NULL;
	g_cur_vert_normals = NULL;
	g_model_node_walk_unused_scratch2 = NULL;
	g_cur_mesh_materials = NULL;
	g_cur_vertex_count = 0;
	for (root_index = 0; root_index < source_model->root_node_count;
	     ++root_index) {
		packed_model->root_nodes[root_index] =
			(struct opt_node *)packed_cursor;
		size_t node_size =
			opt_model_convert_imported_node_to_packed_recursive(
				source_model,
				source_model->root_nodes[root_index],
				&conversion_state, packed_cursor);
		packed_cursor += node_size;
		packed_size += node_size;
	}

	memory_handle_block_done_stub(source_handle);
	memory_handle_block_done_stub(packed_handle);
	memory_handle_block_done_stub(scratch_handle);
	memory_free_handle(source_handle);
	memory_free_handle(scratch_handle);
	uint16_t final_handle = memory_alloc_handle(packed_size, 0);
	void *packed_storage = memory_get_handle_block(packed_handle);
	struct optimized_poly_object *final_model =
		memory_get_handle_block(final_handle);
	memcpy(final_model, packed_storage, packed_size);
	opt_model_adjust_optimized_poly_object_pointers(final_model);
	memory_handle_block_done_stub(final_handle);
	memory_handle_block_done_stub(packed_handle);
	memory_free_handle(packed_handle);
	return final_handle;
}

/* Packs one imported node and everything below it at dest_buffer and returns the
 * bytes written. Each node gets its name, payload_count 1 and no payload bytes
 * unless it has records and the first holds data. A face node's polygons, ended
 * by -1, split into a fan of quads around their first vertex, the last one a
 * triangle or a quad; a faceSet splits polygons of consecutive vertices from 0
 * the same way, skipping a polygon under 3 vertices without stepping past its
 * vertices; a quadMesh gives a grid of quads and a triangleStripSet triangles
 * of alternating order, both from startIndex. Texture coordinate and normal
 * indices copy the vertex indices unless an indexedFaceSet gave lists of the
 * same length. Edges get numbers, an indexedFaceSet's shared ones found with
 * opt_model_find_unique_edge_index; then opt_model_append_packed_face_derived_data adds
 * the normals and gradients. A transform becomes an offset worked out from its
 * center, scaleOrientation, scaleFactor, rotation and translation, followed by
 * the rotation's 3-by-3 matrix (the scaled matrix it built first is
 * overwritten); a rotation becomes a 3-by-3 matrix. A material pads each of its
 * six lists to the longest by repeating the last entry. materialBinding and
 * textureCoordinateBinding store their value in payload_count. A texture loads
 * its file with model_texture_load_rgb_or_tex_file. A levelofdetail keeps each
 * screenArea value that differs from the one before and has a child, and
 * compacts the source node's children to match. Other nodes copy their values.
 * Writes g_cur_mesh_vertices, g_cur_vertex_count, g_cur_vert_normals,
 * g_cur_mesh_tex_coords, conversion_state->p_vert_normals, g_scene_edge_flags_capacity
 * and g_vertex_remap_capacity, and sets g_cur_vert_normals,
 * g_model_node_walk_unused_scratch2 and g_cur_mesh_materials to NULL before a node's
 * children. Only the original build calls this. */
// FUNCTION: XVT 0x477C80
size_t opt_model_convert_imported_node_to_packed_recursive(
	const struct optimized_poly_object *source_model,
	const struct opt_node *source_node, void *conversion_state,
	uint8_t *dest_buffer)
{
	if (source_node == NULL) {
		return 0;
	}

	opt_node_type node_type = source_node->node_type;
	struct opt_node *packed_node = (struct opt_node *)dest_buffer;
	uint8_t *dest = dest_buffer + sizeof(*packed_node);
	packed_node->node_type = node_type;
	if (source_node->p_name != NULL) {
		packed_node->p_name = (char *)dest;
		strcpy((char *)dest, source_node->p_name);
		dest += strlen(source_node->p_name) + 1;
	} else {
		packed_node->p_name = NULL;
	}
	packed_node->payload_count = 1;
	packed_node->payload = dest;
	packed_node->child_count = 0;
	packed_node->p_children = NULL;

	struct inventor_field_record *params = source_node->payload;
	if (params != NULL && source_node->payload_count != 0 &&
	    params->data != NULL) {
		switch (node_type) {
		case OPT_FACEDATA: {
			struct opt_packed_face_node *face_node =
				(struct opt_packed_face_node *)packed_node;
			struct opt_packed_face_data *face_data =
				(struct opt_packed_face_data *)dest;
			const int *vertex_indices = params[0].data;

			face_node->face_count = 0;
			dest += sizeof(face_data->edge_count);
			const int *tex_coord_indices = NULL;
			if (params[3].item_count == params[0].item_count) {
				tex_coord_indices = params[3].data;
			}
			const int *normal_indices = NULL;
			if (params[2].item_count == params[0].item_count) {
				normal_indices = params[2].data;
			}
			int data_index = 0;
			int edge_count = 0;
			while (data_index < params[0].item_count) {
				int polygon_start = data_index;
				int scan_index = data_index + 1;
				while (1) {
					struct opt_packed_face_record *face =
						&face_data->records
							 [face_node
								  ->face_count];

					scan_index += 2;
					face->vertex_indices[0] =
						vertex_indices[polygon_start];
					face->vertex_indices[1] =
						vertex_indices[scan_index - 2];
					face->vertex_indices[2] =
						vertex_indices[scan_index - 1];
					face->vertex_indices[3] =
						vertex_indices[scan_index];
					if (tex_coord_indices != NULL) {
						face->tex_coord_indices[0] =
							tex_coord_indices
								[polygon_start];
						face->tex_coord_indices[1] =
							tex_coord_indices
								[scan_index -
								 2];
						face->tex_coord_indices[2] =
							tex_coord_indices
								[scan_index -
								 1];
						face->tex_coord_indices[3] =
							tex_coord_indices
								[scan_index];
					} else {
						face->tex_coord_indices[0] =
							face->vertex_indices[0];
						face->tex_coord_indices[1] =
							face->vertex_indices[1];
						face->tex_coord_indices[2] =
							face->vertex_indices[2];
						face->tex_coord_indices[3] =
							face->vertex_indices[3];
					}
					if (normal_indices != NULL) {
						face->normal_indices[0] =
							normal_indices
								[polygon_start];
						face->normal_indices[1] =
							normal_indices
								[scan_index -
								 2];
						face->normal_indices[2] =
							normal_indices
								[scan_index -
								 1];
						face->normal_indices[3] =
							normal_indices
								[scan_index];
					} else {
						face->normal_indices[0] =
							face->vertex_indices[0];
						face->normal_indices[1] =
							face->vertex_indices[1];
						face->normal_indices[2] =
							face->vertex_indices[2];
						face->normal_indices[3] =
							face->vertex_indices[3];
					}

					int edge_index =
						opt_model_find_unique_edge_index(
							face_node,
							face->vertex_indices[0],
							face->vertex_indices
								[1]);
					face->edge_indices[0] = edge_index;
					if (edge_index == -1) {
						face->edge_indices[0] =
							edge_count++;
					}
					edge_index =
						opt_model_find_unique_edge_index(
							face_node,
							face->vertex_indices[1],
							face->vertex_indices
								[2]);
					face->edge_indices[1] = edge_index;
					if (edge_index == -1) {
						face->edge_indices[1] =
							edge_count++;
					}
					if (face->vertex_indices[3] == -1) {
						edge_index =
							opt_model_find_unique_edge_index(
								face_node,
								face->vertex_indices
									[2],
								face->vertex_indices
									[0]);
						face->edge_indices[2] =
							edge_index;
						if (edge_index == -1) {
							face->edge_indices[2] =
								edge_count++;
						}
						face->edge_indices[3] = -1;
					} else {
						edge_index =
							opt_model_find_unique_edge_index(
								face_node,
								face->vertex_indices
									[2],
								face->vertex_indices
									[3]);
						face->edge_indices[2] =
							edge_index;
						if (edge_index == -1) {
							face->edge_indices[2] =
								edge_count++;
						}
						edge_index =
							opt_model_find_unique_edge_index(
								face_node,
								face->vertex_indices
									[3],
								face->vertex_indices
									[0]);
						face->edge_indices[3] =
							edge_index;
						if (edge_index == -1) {
							face->edge_indices[3] =
								edge_count++;
						}
					}
					++face_node->face_count;
					if (vertex_indices[scan_index] == -1) {
						data_index = scan_index + 1;
						break;
					}
					if (vertex_indices[scan_index + 1] ==
					    -1) {
						data_index = scan_index + 2;
						break;
					}
				}
			}
			dest = (uint8_t *)
				opt_model_append_packed_face_derived_data(
					face_node, dest, conversion_state);
			face_data->edge_count = edge_count;
			if (edge_count > g_scene_edge_flags_capacity) {
				g_scene_edge_flags_capacity = edge_count;
			}
			break;
		}

		case OPT_TRANSFORM: {
			float *transform = (float *)dest;
			const struct opt_vector *pivot = params[4].data;
			dest += 12 * sizeof(float);
			if (pivot != NULL) {
				transform[0] = -pivot->x;
				float pivot_x = transform[0];
				transform[1] = -pivot->y;
				float pivot_y = transform[1];
				transform[2] = -pivot->z;
				float pivot_z = transform[2];
				if (params[3].data != NULL) {
					math3d_build_axis_angle_matrix(
						&transform[3], params[3].data);
					math3d_rotate_vec3(transform,
							   &transform[3]);
					transform[0] -= pivot_x;
					transform[1] -= pivot_y;
					transform[2] -= pivot_z;
					const struct opt_vector *scale =
						params[2].data;
					if (scale != NULL) {
						transform[3] *= scale->x;
						transform[4] *= scale->y;
						transform[5] *= scale->z;
						transform[6] *= scale->x;
						transform[7] *= scale->y;
						transform[8] *= scale->z;
						transform[9] *= scale->x;
						transform[10] *= scale->y;
						transform[11] *= scale->z;
						transform[0] *= scale->x;
						transform[1] *= scale->y;
						transform[2] *= scale->z;
						if (params[1].data != NULL) {
							transform[0] += pivot_x;
							transform[1] += pivot_y;
							transform[2] += pivot_z;
							math3d_build_axis_angle_matrix(
								&transform[3],
								params[1].data);
							math3d_rotate_vec3(
								transform,
								&transform[3]);
							transform[0] -= pivot_x;
							transform[1] -= pivot_y;
							transform[2] -= pivot_z;
							const struct opt_vector
								*translation =
									params[0]
										.data;
							if (translation !=
							    NULL) {
								transform[0] -=
									translation
										->x;
								transform[1] -=
									translation
										->y;
								transform[2] -=
									translation
										->z;
							}
						}
					}
				}
			}
			break;
		}

		case OPT_MESHVERTS:
			packed_node->payload_count = params[0].item_count;
			memcpy(dest, params[0].data,
			       sizeof(struct opt_vector) *
				       (size_t)packed_node->payload_count);
			g_cur_mesh_vertices = dest;
			g_cur_vertex_count = packed_node->payload_count;
			dest += sizeof(struct opt_vector) *
				(size_t)g_cur_vertex_count;
			if (packed_node->payload_count >
			    g_vertex_remap_capacity) {
				g_vertex_remap_capacity =
					packed_node->payload_count;
			}
			break;

		case OPT_TRANSLATION:
			*(struct opt_vector *)dest =
				*(const struct opt_vector *)params[0].data;
			dest += sizeof(struct opt_vector);
			break;

		case OPT_ROTATION: {
			float *matrix = (float *)dest;
			dest += 9 * sizeof(float);
			math3d_build_axis_angle_matrix(matrix, params[0].data);
			break;
		}

		case OPT_SCALE:
			*(struct opt_vector *)dest =
				*(const struct opt_vector *)params[0].data;
			dest += sizeof(struct opt_vector);
			break;

		case OPT_NODEREF: {
			const char *node_name = params[0].data;
			packed_node->payload_count = 1;
			packed_node->payload = dest;
			strcpy((char *)dest, node_name);
			dest += strlen(node_name) + 1;
			break;
		}

		case OPT_MATERIAL: {
			int max_record_count = params[0].item_count;
			if (max_record_count < params[1].item_count) {
				max_record_count = params[1].item_count;
			}
			if (max_record_count < params[2].item_count) {
				max_record_count = params[2].item_count;
			}
			if (max_record_count < params[3].item_count) {
				max_record_count = params[3].item_count;
			}
			if (max_record_count < params[4].item_count) {
				max_record_count = params[4].item_count;
			}
			if (max_record_count < params[5].item_count) {
				max_record_count = params[5].item_count;
			}
			packed_node->payload_count = max_record_count;

			memcpy(dest, params[0].data,
			       sizeof(struct opt_vector) *
				       (size_t)params[0].item_count);
			float *dest_values =
				(float *)dest + 3 * params[0].item_count;
			dest += sizeof(struct opt_vector) *
				(size_t)max_record_count;
			const float *source_values;
			int fill_count;
			if (params[0].item_count < max_record_count) {
				source_values = (const float *)params[0].data +
						3 * params[0].item_count - 3;
				fill_count =
					max_record_count - params[0].item_count;
				do {
					dest_values[0] = source_values[0];
					dest_values[1] = source_values[1];
					dest_values[2] = source_values[2];
					dest_values += 3;
				} while (--fill_count != 0);
			}
			if (params[1].data != NULL) {
				memcpy(dest, params[1].data,
				       sizeof(struct opt_vector) *
					       (size_t)params[1].item_count);
				dest_values = (float *)dest +
					      3 * params[1].item_count;
				dest += sizeof(struct opt_vector) *
					(size_t)max_record_count;
				if (params[1].item_count < max_record_count) {
					source_values =
						(const float *)params[1].data +
						3 * params[1].item_count - 3;
					fill_count = max_record_count -
						     params[1].item_count;
					do {
						dest_values[0] =
							source_values[0];
						dest_values[1] =
							source_values[1];
						dest_values[2] =
							source_values[2];
						dest_values += 3;
					} while (--fill_count != 0);
				}
				if (params[2].data != NULL) {
					memcpy(dest, params[2].data,
					       sizeof(struct opt_vector) *
						       (size_t)params[2]
							       .item_count);
					dest_values = (float *)dest +
						      3 * params[2].item_count;
					dest += sizeof(struct opt_vector) *
						(size_t)max_record_count;
					if (params[2].item_count <
					    max_record_count) {
						source_values =
							(const float *)params[2]
								.data +
							3 * params[2].item_count -
							3;
						fill_count =
							max_record_count -
							params[2].item_count;
						do {
							dest_values[0] =
								source_values
									[0];
							dest_values[1] =
								source_values
									[1];
							dest_values[2] =
								source_values
									[2];
							dest_values += 3;
						} while (--fill_count != 0);
					}
					if (params[3].data != NULL) {
						memcpy(dest, params[3].data,
						       sizeof(struct
							      opt_vector) *
							       (size_t)params[3]
								       .item_count);
						dest_values =
							(float *)dest +
							3 * params[3].item_count;
						dest += sizeof(struct
							       opt_vector) *
							(size_t)max_record_count;
						if (params[3].item_count <
						    max_record_count) {
							source_values =
								(const float
									 *)params[3]
									.data +
								3 * params[3].item_count -
								3;
							fill_count =
								max_record_count -
								params[3]
									.item_count;
							do {
								dest_values[0] = source_values
									[0];
								dest_values[1] = source_values
									[1];
								dest_values[2] = source_values
									[2];
								dest_values +=
									3;
							} while (--fill_count !=
								 0);
						}
						if (params[4].data != NULL) {
							memcpy(dest,
							       params[4].data,
							       sizeof(float) *
								       (size_t)params[4]
									       .item_count);
							dest_values =
								(float *)dest +
								params[4]
									.item_count;
							dest += sizeof(float) *
								(size_t)max_record_count;
							if (params[4]
								    .item_count <
							    max_record_count) {
								source_values =
									(const float
										 *)params
										[4]
											.data +
									params[4]
										.item_count -
									1;
								fill_count =
									max_record_count -
									params[4]
										.item_count;
								do {
									*dest_values++ =
										*source_values;
								} while (
									--fill_count !=
									0);
							}
							if (params[5].data !=
							    NULL) {
								memcpy(dest,
								       params[5]
									       .data,
								       sizeof(float) *
									       (size_t)params[5]
										       .item_count);
								dest_values =
									(float *)
										dest +
									params[5]
										.item_count;
								dest += sizeof(float) *
									(size_t)max_record_count;
								if (params[5]
									    .item_count <
								    max_record_count) {
									source_values =
										(const float
											 *)params
											[5]
												.data +
										params[5]
											.item_count -
										1;
									fill_count =
										max_record_count -
										params[5]
											.item_count;
									do {
										*dest_values++ =
											*source_values;
									} while (
										--fill_count !=
										0);
								}
							}
						}
					}
				}
			}
			break;
		}

		case OPT_MATERIAL_BINDING:
		case OPT_TEXCOORD_BINDING:
			packed_node->payload_count =
				*(const int *)params[0].data;
			break;

		case OPT_VERTNORMALS:
			packed_node->payload_count = params[0].item_count;
			memcpy(dest, params[0].data,
			       sizeof(struct opt_vector) *
				       (size_t)packed_node->payload_count);
			g_cur_vert_normals = (struct opt_vector *)dest;
			((struct scene_mesh *)conversion_state)
				->p_vert_normals = (struct opt_vector *)dest;
			dest += sizeof(struct opt_vector) *
				(size_t)packed_node->payload_count;
			break;

		case OPT_TEXCOORDS:
			packed_node->payload_count = params[0].item_count;
			memcpy(dest, params[0].data,
			       sizeof(struct opt_tex_coord) *
				       (size_t)packed_node->payload_count);
			g_cur_mesh_tex_coords = dest;
			dest += sizeof(struct opt_tex_coord) *
				(size_t)packed_node->payload_count;
			break;

		case OPT_FACEDATA_QUAD_MESH: {
			struct opt_packed_face_node *face_node =
				(struct opt_packed_face_node *)packed_node;
			struct opt_packed_face_data *face_data =
				(struct opt_packed_face_data *)dest;
			const int *width_data = params[1].data;
			const int *height_data = params[2].data;
			dest += sizeof(face_data->edge_count);
			if (width_data != NULL && height_data != NULL) {
				int width = *width_data;
				int height = *height_data;
				int first_vertex = *(const int *)params[0].data;
				int edge_count = 0;
				for (int row_index = 0; row_index < height - 1;
				     ++row_index) {
					for (int column_index = 0;
					     column_index < width - 1;
					     ++column_index) {
						struct opt_packed_face_record *face =
							&face_data->records
								 [row_index *
									  (width -
									   1) +
								  column_index];
						face->vertex_indices[0] =
							first_vertex +
							row_index * width +
							column_index;
						face->vertex_indices[1] =
							face->vertex_indices
								[0] +
							1;
						face->vertex_indices[2] =
							face->vertex_indices
								[1] +
							width;
						face->vertex_indices[3] =
							face->vertex_indices
								[0] +
							width;
						face->tex_coord_indices[0] =
							face->vertex_indices[0];
						face->tex_coord_indices[1] =
							face->vertex_indices[1];
						face->tex_coord_indices[2] =
							face->vertex_indices[2];
						face->tex_coord_indices[3] =
							face->vertex_indices[3];
						face->normal_indices[0] =
							face->vertex_indices[0];
						face->normal_indices[1] =
							face->vertex_indices[1];
						face->normal_indices[2] =
							face->vertex_indices[2];
						face->normal_indices[3] =
							face->vertex_indices[3];
						if (row_index == 0) {
							face->edge_indices[0] =
								edge_count++;
						} else if (row_index == 1) {
							face->edge_indices[0] =
								column_index +
								edge_count -
								3 * width + 4;
						} else {
							face->edge_indices[0] =
								edge_count -
								2 * width;
						}
						face->edge_indices[1] =
							edge_count++;
						face->edge_indices[2] =
							edge_count++;
						if (column_index == 0) {
							face->edge_indices[3] =
								edge_count++;
						} else {
							face->edge_indices[3] =
								edge_count - 4;
							if (row_index == 0) {
								--face->edge_indices
									  [3];
							}
							if (column_index == 1) {
								--face->edge_indices
									  [3];
							}
						}
					}
				}
				face_node->face_count =
					(height - 1) * (width - 1);
				dest = (uint8_t *)
					opt_model_append_packed_face_derived_data(
						face_node, dest,
						conversion_state);
				face_data->edge_count = edge_count;
				if (edge_count > g_scene_edge_flags_capacity) {
					g_scene_edge_flags_capacity =
						edge_count;
				}
			}
			break;
		}

		case OPT_FACEDATA_FACE_SET: {
			struct opt_packed_face_node *face_node =
				(struct opt_packed_face_node *)packed_node;
			struct opt_packed_face_data *face_data =
				(struct opt_packed_face_data *)dest;
			const int *polygon_vertex_counts = params[0].data;
			dest += sizeof(face_data->edge_count);
			face_node->face_count = 0;
			int vertex_cursor = 0;
			int edge_cursor = 0;
			for (int polygon_index = 0;
			     polygon_index < params[0].item_count;
			     ++polygon_index) {
				int polygon_vertex_count =
					polygon_vertex_counts[polygon_index];
				if (polygon_vertex_count >= 3) {
					int polygon_start = vertex_cursor;
					int first_edge = edge_cursor;
					++vertex_cursor;
					++edge_cursor;
					while (vertex_cursor - polygon_start <
					       polygon_vertex_count) {
						struct opt_packed_face_record *face =
							&face_data->records
								 [face_node
									  ->face_count];
						++face_node->face_count;
						face->vertex_indices[0] =
							polygon_start;
						face->vertex_indices[1] =
							vertex_cursor;
						face->vertex_indices[2] =
							++vertex_cursor;
						++vertex_cursor;
						if (vertex_cursor -
							    polygon_vertex_count ==
						    polygon_start) {
							face->vertex_indices
								[3] = -1;
							face->edge_indices[0] =
								first_edge;
							face->edge_indices[1] =
								edge_cursor;
							face->edge_indices[2] =
								++edge_cursor;
							++edge_cursor;
							face->edge_indices[3] =
								-1;
						} else {
							face->vertex_indices
								[3] =
								vertex_cursor;
							if (++vertex_cursor -
								    polygon_vertex_count !=
							    polygon_start) {
								--vertex_cursor;
							}
							face->edge_indices[0] =
								first_edge;
							face->edge_indices[1] =
								edge_cursor;
							face->edge_indices[2] =
								++edge_cursor;
							first_edge =
								++edge_cursor;
							face->edge_indices[3] =
								first_edge;
							++edge_cursor;
						}
						face->tex_coord_indices[0] =
							face->vertex_indices[0];
						face->tex_coord_indices[1] =
							face->vertex_indices[1];
						face->tex_coord_indices[2] =
							face->vertex_indices[2];
						face->tex_coord_indices[3] =
							face->vertex_indices[3];
						face->normal_indices[0] =
							face->vertex_indices[0];
						face->normal_indices[1] =
							face->vertex_indices[1];
						face->normal_indices[2] =
							face->vertex_indices[2];
						face->normal_indices[3] =
							face->vertex_indices[3];
					}
				}
			}
			dest = (uint8_t *)
				opt_model_append_packed_face_derived_data(
					face_node, dest, conversion_state);
			face_data->edge_count = edge_cursor;
			if (edge_cursor > g_scene_edge_flags_capacity) {
				g_scene_edge_flags_capacity = edge_cursor;
			}
			break;
		}

		case OPT_FACEDATA_TRIANGLE_STRIP_SET: {
			struct opt_packed_face_node *face_node =
				(struct opt_packed_face_node *)packed_node;
			struct opt_packed_face_data *face_data =
				(struct opt_packed_face_data *)dest;
			const int *strip_vertex_counts = params[1].data;
			int first_vertex = *(const int *)params[0].data;
			int edge_cursor = *(const int *)conversion_state;
			dest += sizeof(face_data->edge_count);
			if (strip_vertex_counts != NULL) {
				face_node->face_count = 0;
				for (int strip_index = 0;
				     strip_index < params[1].item_count;
				     ++strip_index) {
					int strip_vertex_count =
						strip_vertex_counts
							[strip_index];
					for (int vertex_index = 2;
					     vertex_index < strip_vertex_count;
					     ++vertex_index) {
						struct opt_packed_face_record *face =
							&face_data->records
								 [face_node
									  ->face_count];
						int current_vertex =
							first_vertex +
							vertex_index;
						if ((vertex_index & 1) != 0) {
							face->vertex_indices
								[0] =
								current_vertex;
							face->vertex_indices
								[1] =
								current_vertex -
								1;
							face->vertex_indices
								[2] =
								current_vertex -
								2;
							face->edge_indices[0] =
								edge_cursor;
							face->edge_indices[1] =
								edge_cursor - 2;
						} else {
							face->vertex_indices
								[0] =
								current_vertex -
								2;
							face->vertex_indices
								[1] =
								current_vertex -
								1;
							face->vertex_indices
								[2] =
								current_vertex;
							if (vertex_index == 2) {
								face->edge_indices
									[0] =
									edge_cursor++;
							} else {
								face->edge_indices
									[0] =
									edge_cursor -
									2;
							}
							face->edge_indices[1] =
								edge_cursor;
						}
						face->vertex_indices[3] = -1;
						face->edge_indices[2] =
							++edge_cursor;
						++edge_cursor;
						face->edge_indices[3] = -1;
						face->tex_coord_indices[0] =
							face->vertex_indices[0];
						face->tex_coord_indices[1] =
							face->vertex_indices[1];
						face->tex_coord_indices[2] =
							face->vertex_indices[2];
						face->tex_coord_indices[3] =
							face->vertex_indices[3];
						face->normal_indices[0] =
							face->vertex_indices[0];
						face->normal_indices[1] =
							face->vertex_indices[1];
						face->normal_indices[2] =
							face->vertex_indices[2];
						face->normal_indices[3] =
							face->vertex_indices[3];
						++face_node->face_count;
					}
					first_vertex += strip_vertex_count;
				}
				dest = (uint8_t *)
					opt_model_append_packed_face_derived_data(
						face_node, dest,
						conversion_state);
				face_data->edge_count = edge_cursor;
				if (edge_cursor > g_scene_edge_flags_capacity) {
					g_scene_edge_flags_capacity =
						edge_cursor;
				}
			}
			break;
		}

		case OPT_TEXTURE:
			packed_node->payload = dest;
			dest += model_texture_load_rgb_or_tex_file(
				dest, params[0].data);
			break;

		case OPT_FACEGROUP: {
			struct opt_node *mutable_source_node =
				(struct opt_node *)source_node;
			const float *source_values = params[0].data;
			packed_node->payload_count = 0;
			for (int source_index = 0;
			     source_index < params[0].item_count;
			     ++source_index) {
				if (mutable_source_node->p_children
						    [source_index] != NULL &&
				    (source_index == 0 ||
				     source_values[source_index - 1] !=
					     source_values[source_index])) {
					*(float *)dest =
						source_values[source_index];
					dest += sizeof(float);
					mutable_source_node->p_children
						[packed_node->payload_count] =
						mutable_source_node->p_children
							[source_index];
					++packed_node->payload_count;
				}
			}
			mutable_source_node->child_count =
				packed_node->payload_count;
			break;
		}

		case OPT_HARDPOINT: {
			const struct opt_vector *position = params[1].data;
			struct opt_hardpoint *hardpoint =
				(struct opt_hardpoint *)dest;
			hardpoint->hardpoint_type =
				*(const int *)params[0].data;
			hardpoint->position = *position;
			dest += sizeof(*hardpoint);
			break;
		}

		case OPT_ROTSCALE: {
			*(struct opt_vector *)dest =
				*(const struct opt_vector *)params[0].data;
			dest += sizeof(struct opt_vector);
			*(struct opt_vector *)dest =
				*(const struct opt_vector *)params[1].data;
			dest += sizeof(struct opt_vector);
			*(struct opt_vector *)dest =
				*(const struct opt_vector *)params[2].data;
			dest += sizeof(struct opt_vector);
			*(struct opt_vector *)dest =
				*(const struct opt_vector *)params[3].data;
			dest += sizeof(struct opt_vector);
			break;
		}

		case OPT_MESHDESC: {
			float *values = (float *)dest;
			dest += 18 * sizeof(float);
			values[0] = *(const float *)params[0].data;
			((int *)values)[1] = *(const int *)params[1].data;
			*(struct opt_vector *)&values[2] =
				*(const struct opt_vector *)params[2].data;
			*(struct opt_vector *)&values[5] =
				*(const struct opt_vector *)params[3].data;
			*(struct opt_vector *)&values[8] =
				*(const struct opt_vector *)params[4].data;
			*(struct opt_vector *)&values[11] =
				*(const struct opt_vector *)params[5].data;
			values[14] = *(const float *)params[6].data;
			*(struct opt_vector *)&values[15] =
				*(const struct opt_vector *)params[7].data;
			break;
		}

		default:
			break;
		}
	}

	if (source_node->child_count != 0) {
		struct scene_mesh child_state =
			*(struct scene_mesh *)conversion_state;
		g_cur_vert_normals = NULL;
		g_model_node_walk_unused_scratch2 = NULL;
		g_cur_mesh_materials = NULL;
		packed_node->child_count = source_node->child_count;
		packed_node->p_children = (struct opt_node **)dest;
		dest += sizeof(*packed_node->p_children) *
			(size_t)packed_node->child_count;
		for (int child_index = 0;
		     child_index < source_node->child_count; ++child_index) {
			packed_node->p_children[child_index] =
				(struct opt_node *)dest;
			dest += opt_model_convert_imported_node_to_packed_recursive(
				source_model,
				source_node->p_children[child_index],
				&child_state, dest);
		}
	}
	return (size_t)(dest - dest_buffer);
}

/* Returns the bytes opt_model_convert_imported_node_to_packed_recursive will need for
 * source_node and everything below it: a 24-byte node, its name, and its
 * payload. A face node counts its faces and adds room for g_cur_vertex_count
 * vertex normals. A material counts the longest of its first five lists only,
 * while the packer pads to the longest of all six. A texture asks
 * opt_model_get_external_texture_serialized_size. Writes g_cur_vertex_count,
 * g_cur_vert_normals and conversion_state->p_vert_normals as it passes those nodes,
 * and sets g_cur_vert_normals, g_model_node_walk_unused_scratch2 and
 * g_cur_mesh_materials to NULL before a node's children. Only the original build
 * calls this. */
// FUNCTION: XVT 0x478F50
size_t opt_model_calculate_packed_node_size_recursive(
	const struct optimized_poly_object *source_model,
	const struct opt_node *source_node, void *conversion_state)
{
	if (source_node == NULL) {
		return 0;
	}

	size_t packed_size = 24;

	opt_node_type node_type = source_node->node_type;
	if (source_node->p_name != NULL) {

		packed_size = strlen(source_node->p_name) + 25;
	}
	struct inventor_field_record *params = source_node->payload;
	if (params != NULL && source_node->payload_count != 0 &&
	    params->data != NULL) {
		int *data = params->data;
		int face_count;
		int data_index;
		int record_count;
		int marker;
		int max_record_count;
		switch (node_type) {
		case OPT_FACEDATA:
			face_count = 0;
			data_index = 0;
			record_count = params->item_count;
			if (record_count > 0) {
				do {
					int *scan_data = &data[data_index + 1];
					++data_index;
					while (1) {
						marker = scan_data[2];
						++face_count;
						scan_data += 2;
						data_index += 2;
						if (marker == -1) {
							++data_index;
							break;
						}
						if (scan_data[1] == -1) {
							data_index += 2;
							break;
						}
					}
				} while (data_index < record_count);
			}
			packed_size += 4;
			packed_size += (unsigned int)face_count << 6;
			packed_size += 36 * face_count;
			packed_size += 12 * g_cur_vertex_count;
			break;

		case OPT_TRANSFORM:
			packed_size += 48;
			break;

		case OPT_MESHVERTS:
			g_cur_vertex_count = params->item_count;
			packed_size += 12 * g_cur_vertex_count;
			break;

		case OPT_TRANSLATION:
			packed_size += 12;
			break;

		case OPT_ROTATION:
			packed_size += 36;
			break;

		case OPT_SCALE:
			packed_size += 12;
			break;

		case OPT_NODEREF:
			packed_size += strlen((const char *)params->data) + 1;
			break;

		case OPT_MATERIAL:
			max_record_count = params->item_count;
			marker = params[1].item_count;
			++params;
			if (marker > max_record_count) {
				max_record_count = marker;
			}
			marker = params[1].item_count;
			++params;
			if (marker > max_record_count) {
				max_record_count = marker;
			}
			marker = params[1].item_count;
			++params;
			if (marker > max_record_count) {
				max_record_count = marker;
			}
			marker = params[1].item_count;
			if (marker > max_record_count) {
				max_record_count = marker;
			}
			packed_size += 48 * max_record_count;
			packed_size += 8 * max_record_count;
			break;

		case OPT_VERTNORMALS:
			record_count = 3 * params->item_count;
			g_cur_vert_normals =
				(struct opt_vector *)source_node->payload;
			packed_size += 4 * record_count;
			((struct scene_mesh *)conversion_state)
				->p_vert_normals = (struct opt_vector *)params;
			break;

		case OPT_TEXCOORDS:
			packed_size += 8 * params->item_count;
			break;

		case OPT_FACEDATA_QUAD_MESH:
			data = params[1].data;
			++params;
			if (data != NULL) {
				record_count = *data;
				data = params[1].data;
				if (data != NULL) {
					face_count = (record_count - 1) *
						     (*data - 1);
					packed_size += 4;
					packed_size += (unsigned int)face_count
						       << 6;
					packed_size += 36 * face_count;
					packed_size += 12 * g_cur_vertex_count;
				}
			}
			break;

		case OPT_FACEDATA_FACE_SET:
			data_index = 0;
			face_count = 0;
			record_count = params->item_count;
			if (record_count > 0) {
				do {
					int polygon_vertex_count = *data;
					if (polygon_vertex_count >= 3) {
						int polygon_start =
							data_index++;
						while (data_index -
							       polygon_start <
						       polygon_vertex_count) {
							++face_count;
							data_index += 2;
							if (data_index -
								    polygon_vertex_count !=
							    polygon_start) {
								++data_index;
								if (data_index -
									    polygon_vertex_count ==
								    polygon_start) {
									break;
								}
								--data_index;
							}
						}
					}
					++data;
					--record_count;
				} while (record_count != 0);
			}
			packed_size += 4;
			packed_size += (unsigned int)face_count << 6;
			packed_size += 36 * face_count;
			packed_size += 12 * g_cur_vertex_count;
			break;

		case OPT_FACEDATA_TRIANGLE_STRIP_SET:
			data = params[1].data;
			++params;
			if (data != NULL) {
				face_count = 0;
				record_count = params->item_count;
				if (record_count > 0) {
					do {
						face_count += *data - 2;
						++data;
						--record_count;
					} while (record_count != 0);
				}
				packed_size += 4;
				packed_size += (unsigned int)face_count << 6;
				packed_size += 36 * face_count;
				packed_size += 12 * g_cur_vertex_count;
			}
			break;

		case OPT_TEXTURE:
			packed_size +=
				opt_model_get_external_texture_serialized_size(
					(const char *)params->data);
			break;

		case OPT_FACEGROUP:
			packed_size += 4 * params->item_count;
			break;

		case OPT_HARDPOINT:
			packed_size += 16;
			break;

		case OPT_ROTSCALE:
			packed_size += 48;
			break;

		case OPT_MESHDESC:
			packed_size += 72;
			break;

		default:
			break;
		}
	}

	int child_offset = 0;
	if (source_node->child_count != 0) {
		struct scene_mesh child_state =
			*(struct scene_mesh *)conversion_state;
		int child_index = 0;
		g_cur_vert_normals = NULL;
		g_model_node_walk_unused_scratch2 = NULL;
		g_cur_mesh_materials = NULL;

		packed_size += 4 * source_node->child_count;

		if (source_node->child_count > 0) {
			do {
				packed_size +=
					opt_model_calculate_packed_node_size_recursive(
						source_model,
						*(struct opt_node *
							  *)((uint8_t *)source_node
								     ->p_children +
							     child_offset),
						&child_state);
				child_offset +=
					sizeof(*source_node->p_children);
				++child_index;
			} while (source_node->child_count > child_index);
		}
	}
	return packed_size;
}

/* Returns the edge number of the one face, among the face_count already in
 * face_node, that has an edge from vertex_index_a to vertex_index_b in either
 * direction; -1 when no face has it, and also when two or more do. Only the
 * original build calls this. */
// FUNCTION: XVT 0x4792F0
int opt_model_find_unique_edge_index(
	const struct opt_packed_face_node *face_node, int vertex_index_a,
	int vertex_index_b)
{
	const struct opt_packed_face_record *face =
		face_node->face_data->records;
	int result = -1;
	int face_index = 0;
	while (face_index < face_node->face_count) {
		if (vertex_index_a == face->vertex_indices[0] &&
		    vertex_index_b == face->vertex_indices[1]) {
			result = face->edge_indices[0];
			break;
		}
		if (vertex_index_a == face->vertex_indices[1] &&
		    vertex_index_b == face->vertex_indices[0]) {
			result = face->edge_indices[0];
			break;
		}
		if (vertex_index_a == face->vertex_indices[1] &&
		    vertex_index_b == face->vertex_indices[2]) {
			result = face->edge_indices[1];
			break;
		}
		if (vertex_index_a == face->vertex_indices[2] &&
		    vertex_index_b == face->vertex_indices[1]) {
			result = face->edge_indices[1];
			break;
		}
		if (face->vertex_indices[3] == -1) {
			if (vertex_index_a == face->vertex_indices[0] &&
			    vertex_index_b == face->vertex_indices[2]) {
				result = face->edge_indices[2];
				break;
			}
			if (vertex_index_a == face->vertex_indices[2] &&
			    vertex_index_b == face->vertex_indices[0]) {
				result = face->edge_indices[2];
				break;
			}
		} else {
			if (vertex_index_a == face->vertex_indices[2] &&
			    vertex_index_b == face->vertex_indices[3]) {
				result = face->edge_indices[2];
				break;
			}
			if (vertex_index_a == face->vertex_indices[3] &&
			    vertex_index_b == face->vertex_indices[2]) {
				result = face->edge_indices[2];
				break;
			}
			if (vertex_index_a == face->vertex_indices[0] &&
			    vertex_index_b == face->vertex_indices[3]) {
				result = face->edge_indices[3];
				break;
			}
			if (vertex_index_a == face->vertex_indices[3] &&
			    vertex_index_b == face->vertex_indices[0]) {
				result = face->edge_indices[3];
				break;
			}
		}
		face++;
		face_index++;
	}

	if (result != -1) {
		face++;
		face_index++;
		while (face_index < face_node->face_count) {
			if (vertex_index_a == face->vertex_indices[0] &&
			    vertex_index_b == face->vertex_indices[1]) {
				return -1;
			}
			if (vertex_index_a == face->vertex_indices[1] &&
			    vertex_index_b == face->vertex_indices[0]) {
				return -1;
			}
			if (vertex_index_a == face->vertex_indices[1] &&
			    vertex_index_b == face->vertex_indices[2]) {
				return -1;
			}
			if (vertex_index_a == face->vertex_indices[2] &&
			    vertex_index_b == face->vertex_indices[1]) {
				return -1;
			}
			if (face->vertex_indices[3] == -1) {
				if (vertex_index_a == face->vertex_indices[0] &&
				    vertex_index_b == face->vertex_indices[2]) {
					return -1;
				}
				if (vertex_index_a == face->vertex_indices[2] &&
				    vertex_index_b == face->vertex_indices[0]) {
					return -1;
				}
			} else {
				if (vertex_index_a == face->vertex_indices[2] &&
				    vertex_index_b == face->vertex_indices[3]) {
					return -1;
				}
				if (vertex_index_a == face->vertex_indices[3] &&
				    vertex_index_b == face->vertex_indices[2]) {
					return -1;
				}
				if (vertex_index_a == face->vertex_indices[0] &&
				    vertex_index_b == face->vertex_indices[3]) {
					return -1;
				}
				if (vertex_index_a == face->vertex_indices[3] &&
				    vertex_index_b == face->vertex_indices[0]) {
					return -1;
				}
			}
			face++;
			face_index++;
		}
	}
	return result;
}

/* Writes after face_node's face records the per-face normals and texture
 * gradients and, when the mesh has no normal list, the per-vertex normals, with
 * opt_model_build_face_normal_tangent_data, and returns the address after them.
 * Steps 36 bytes per face and 12 per vertex normal counted in
 * g_generated_vertex_normal_count. Only the original build calls this. */
// FUNCTION: XVT 0x4794C0
float *opt_model_append_packed_face_derived_data(
	const struct opt_packed_face_node *face_node, uint8_t *dest,
	const void *conversion_state)
{
	dest += sizeof(struct opt_packed_face_record) * face_node->face_count;
	opt_model_build_face_normal_tangent_data(
		(float *)dest, face_node->face_data, face_node->face_count,
		conversion_state);
	dest += 3 * sizeof(struct opt_vector) * face_node->face_count;
	dest += 3 * sizeof(float) * g_generated_vertex_normal_count;
	return (float *)dest;
}

/* Writes at dest one unit normal per face, (v1 - v0) cross (v1 - v2), using v3
 * in place of v1 for a quad whose first result is zero length (a zero result
 * stays zero; g_opt_model_invert_face_normals would negate it); then, when
 * g_cur_mesh_tex_coords is set, two texture gradient vectors per face from its
 * positions and texture coordinates, trying other corners for a degenerate
 * face; then, when the conversion state has no vertex normal list, one averaged
 * normal per vertex (opt_model_build_vertex_normals_from_faces). Sets
 * g_generated_vertex_normal_count to the vertex count or 0. Returns at once,
 * writing nothing, when g_cur_mesh_vertices is NULL. Without texture coordinates
 * it skips 24 floats per face where the packed layout holds 6; the XVT_MODERN
 * arm that skips 6 sits inside this #ifndef XVT_MODERN block, so no build
 * compiles it. Only the original build calls this. */
// FUNCTION: XVT 0x479510
void opt_model_build_face_normal_tangent_data(
	float *dest, const struct opt_packed_face_data *face_data,
	int face_count, const void *conversion_state)
{
	if (g_cur_mesh_vertices == NULL) {
		return;
	}
	const struct opt_packed_face_record *records = face_data->records;
	float edge_ax;
	float edge_ay;
	float edge_az;
	float edge_bx;
	float edge_by;
	float edge_bz;
	if (face_count > 0) {
		const struct opt_packed_face_record *face = records;
		for (int face_index = 0; face_index < face_count;
		     ++face_index) {
			edge_ax =
				((const struct opt_vector *)g_cur_mesh_vertices)
					[face->vertex_indices[1]]
						.x -
				((const struct opt_vector *)g_cur_mesh_vertices)
					[face->vertex_indices[0]]
						.x;
			edge_ay =
				((const struct opt_vector *)g_cur_mesh_vertices)
					[face->vertex_indices[1]]
						.y -
				((const struct opt_vector *)g_cur_mesh_vertices)
					[face->vertex_indices[0]]
						.y;
			edge_az =
				((const struct opt_vector *)g_cur_mesh_vertices)
					[face->vertex_indices[1]]
						.z -
				((const struct opt_vector *)g_cur_mesh_vertices)
					[face->vertex_indices[0]]
						.z;
			edge_bx =
				((const struct opt_vector *)g_cur_mesh_vertices)
					[face->vertex_indices[1]]
						.x -
				((const struct opt_vector *)g_cur_mesh_vertices)
					[face->vertex_indices[2]]
						.x;
			edge_by =
				((const struct opt_vector *)g_cur_mesh_vertices)
					[face->vertex_indices[1]]
						.y -
				((const struct opt_vector *)g_cur_mesh_vertices)
					[face->vertex_indices[2]]
						.y;
			edge_bz =
				((const struct opt_vector *)g_cur_mesh_vertices)
					[face->vertex_indices[1]]
						.z -
				((const struct opt_vector *)g_cur_mesh_vertices)
					[face->vertex_indices[2]]
						.z;
			dest[0] = edge_bz * edge_ay - edge_by * edge_az;
			dest[1] = edge_bx * edge_az - edge_bz * edge_ax;
			dest[2] = edge_by * edge_ax - edge_bx * edge_ay;
			float normal_length_squared = dest[2] * dest[2] +
						      dest[0] * dest[0] +
						      dest[1] * dest[1];
			if (normal_length_squared == g_sw3d_zero_float) {
				if (face->vertex_indices[3] != -1) {
					edge_ax = ((const struct opt_vector *)
							   g_cur_mesh_vertices)
							  [face->vertex_indices
								   [3]]
								  .x -
						  ((const struct opt_vector *)
							   g_cur_mesh_vertices)
							  [face->vertex_indices
								   [0]]
								  .x;
					edge_ay = ((const struct opt_vector *)
							   g_cur_mesh_vertices)
							  [face->vertex_indices
								   [3]]
								  .y -
						  ((const struct opt_vector *)
							   g_cur_mesh_vertices)
							  [face->vertex_indices
								   [0]]
								  .y;
					edge_az = ((const struct opt_vector *)
							   g_cur_mesh_vertices)
							  [face->vertex_indices
								   [3]]
								  .z -
						  ((const struct opt_vector *)
							   g_cur_mesh_vertices)
							  [face->vertex_indices
								   [0]]
								  .z;
					edge_bx = ((const struct opt_vector *)
							   g_cur_mesh_vertices)
							  [face->vertex_indices
								   [3]]
								  .x -
						  ((const struct opt_vector *)
							   g_cur_mesh_vertices)
							  [face->vertex_indices
								   [2]]
								  .x;
					edge_by = ((const struct opt_vector *)
							   g_cur_mesh_vertices)
							  [face->vertex_indices
								   [3]]
								  .y -
						  ((const struct opt_vector *)
							   g_cur_mesh_vertices)
							  [face->vertex_indices
								   [2]]
								  .y;
					edge_bz = ((const struct opt_vector *)
							   g_cur_mesh_vertices)
							  [face->vertex_indices
								   [3]]
								  .z -
						  ((const struct opt_vector *)
							   g_cur_mesh_vertices)
							  [face->vertex_indices
								   [2]]
								  .z;
					dest[0] = edge_bz * edge_ay -
						  edge_by * edge_az;
					dest[1] = edge_bx * edge_az -
						  edge_bz * edge_ax;
					dest[2] = edge_by * edge_ax -
						  edge_bx * edge_ay;
					normal_length_squared =
						dest[2] * dest[2] +
						dest[0] * dest[0] +
						dest[1] * dest[1];
					if (normal_length_squared !=
					    g_sw3d_zero_float) {
						const float scale =
							(float)(g_sw3d_unit_float /
								sqrt(normal_length_squared));
						dest[0] *= scale;
						dest[1] *= scale;
						dest[2] *= scale;
					}
				}
			} else {
				const float scale =
					(float)(g_sw3d_unit_float /
						sqrt(normal_length_squared));
				dest[0] *= scale;
				dest[1] *= scale;
				dest[2] *= scale;
			}
			if (g_opt_model_invert_face_normals != 0) {
				dest[0] = -dest[0];
				dest[1] = -dest[1];
				dest[2] = -dest[2];
			}
			dest += 3;
			++face;
		}
	}
	records = face_data->records;
	if (g_cur_mesh_tex_coords != NULL) {
		if (face_count > 0) {
			for (int tangent_face_index = 0;
			     tangent_face_index < face_count;
			     ++tangent_face_index) {
				const struct opt_packed_face_record *face =
					records;
				const struct opt_vector *vertices =
					(const struct opt_vector *)
						g_cur_mesh_vertices;
				edge_ax = vertices[face->vertex_indices[0]].x -
					  vertices[face->vertex_indices[1]].x;
				edge_ay = vertices[face->vertex_indices[0]].y -
					  vertices[face->vertex_indices[1]].y;
				edge_az = vertices[face->vertex_indices[0]].z -
					  vertices[face->vertex_indices[1]].z;
				edge_bx = vertices[face->vertex_indices[0]].x -
					  vertices[face->vertex_indices[2]].x;
				edge_by = vertices[face->vertex_indices[0]].y -
					  vertices[face->vertex_indices[2]].y;
				edge_bz = vertices[face->vertex_indices[0]].z -
					  vertices[face->vertex_indices[2]].z;
				const struct opt_tex_coord *tex_coords =
					(const struct opt_tex_coord *)
						g_cur_mesh_tex_coords;
				float du_a =
					tex_coords[face->tex_coord_indices[0]]
						.u -
					tex_coords[face->tex_coord_indices[1]]
						.u;
				float dv_a =
					tex_coords[face->tex_coord_indices[0]]
						.v -
					tex_coords[face->tex_coord_indices[1]]
						.v;
				float du_b =
					tex_coords[face->tex_coord_indices[0]]
						.u -
					tex_coords[face->tex_coord_indices[2]]
						.u;
				float dv_b =
					tex_coords[face->tex_coord_indices[0]]
						.v -
					tex_coords[face->tex_coord_indices[2]]
						.v;
				float determinant = du_b * dv_a - du_a * dv_b;
				dest[0] = edge_bx * dv_a - dv_b * edge_ax;
				dest[1] = edge_by * dv_a - dv_b * edge_ay;
				dest[2] = edge_bz * dv_a - dv_b * edge_az;
				float length_squared = dest[2] * dest[2] +
						       dest[0] * dest[0] +
						       dest[1] * dest[1];
				if (determinant == 0.0f ||
				    length_squared == g_sw3d_zero_float) {
					if (face->vertex_indices[3] != -1) {
						vertices = (const struct
							    opt_vector *)
							g_cur_mesh_vertices;
						edge_ax =
							vertices[face->vertex_indices
									 [0]]
								.x -
							vertices[face->vertex_indices
									 [1]]
								.x;
						edge_ay =
							vertices[face->vertex_indices
									 [0]]
								.y -
							vertices[face->vertex_indices
									 [1]]
								.y;
						edge_az =
							vertices[face->vertex_indices
									 [0]]
								.z -
							vertices[face->vertex_indices
									 [1]]
								.z;
						edge_bx =
							vertices[face->vertex_indices
									 [0]]
								.x -
							vertices[face->vertex_indices
									 [3]]
								.x;
						edge_by =
							vertices[face->vertex_indices
									 [0]]
								.y -
							vertices[face->vertex_indices
									 [3]]
								.y;
						edge_bz =
							vertices[face->vertex_indices
									 [0]]
								.z -
							vertices[face->vertex_indices
									 [3]]
								.z;
						tex_coords = (const struct
							      opt_tex_coord *)
							g_cur_mesh_tex_coords;
						du_a = tex_coords
							       [face->tex_coord_indices
									[0]]
								       .u -
						       tex_coords
							       [face->tex_coord_indices
									[1]]
								       .u;
						dv_a = tex_coords
							       [face->tex_coord_indices
									[0]]
								       .v -
						       tex_coords
							       [face->tex_coord_indices
									[1]]
								       .v;
						du_b = tex_coords
							       [face->tex_coord_indices
									[0]]
								       .u -
						       tex_coords
							       [face->tex_coord_indices
									[3]]
								       .u;
						dv_b = tex_coords
							       [face->tex_coord_indices
									[0]]
								       .v -
						       tex_coords
							       [face->tex_coord_indices
									[3]]
								       .v;
						determinant = du_b * dv_a -
							      du_a * dv_b;
						dest[0] = edge_bx * dv_a -
							  dv_b * edge_ax;
						dest[1] = edge_by * dv_a -
							  dv_b * edge_ay;
						dest[2] = edge_bz * dv_a -
							  dv_b * edge_az;
						length_squared =
							dest[2] * dest[2] +
							dest[0] * dest[0] +
							dest[1] * dest[1];
						if (determinant == 0.0f ||
						    length_squared ==
							    g_sw3d_zero_float) {
							vertices = (const struct
								    opt_vector
									    *)
								g_cur_mesh_vertices;
							edge_ax =
								vertices[face->vertex_indices
										 [0]]
									.x -
								vertices[face->vertex_indices
										 [2]]
									.x;
							edge_ay =
								vertices[face->vertex_indices
										 [0]]
									.y -
								vertices[face->vertex_indices
										 [2]]
									.y;
							edge_az =
								vertices[face->vertex_indices
										 [0]]
									.z -
								vertices[face->vertex_indices
										 [2]]
									.z;
							edge_bx =
								vertices[face->vertex_indices
										 [0]]
									.x -
								vertices[face->vertex_indices
										 [3]]
									.x;
							edge_by =
								vertices[face->vertex_indices
										 [0]]
									.y -
								vertices[face->vertex_indices
										 [3]]
									.y;
							edge_bz =
								vertices[face->vertex_indices
										 [0]]
									.z -
								vertices[face->vertex_indices
										 [3]]
									.z;
							tex_coords = (const struct
								      opt_tex_coord
									      *)
								g_cur_mesh_tex_coords;
							du_a = tex_coords
								       [face->tex_coord_indices
										[0]]
									       .u -
							       tex_coords
								       [face->tex_coord_indices
										[2]]
									       .u;
							dv_a = tex_coords
								       [face->tex_coord_indices
										[0]]
									       .v -
							       tex_coords
								       [face->tex_coord_indices
										[2]]
									       .v;
							du_b = tex_coords
								       [face->tex_coord_indices
										[0]]
									       .u -
							       tex_coords
								       [face->tex_coord_indices
										[3]]
									       .u;
							dv_b = tex_coords
								       [face->tex_coord_indices
										[0]]
									       .v -
							       tex_coords
								       [face->tex_coord_indices
										[3]]
									       .v;
							determinant =
								du_b * dv_a -
								du_a * dv_b;
							dest[0] =
								edge_bx * dv_a -
								dv_b * edge_ax;
							dest[1] =
								edge_by * dv_a -
								dv_b * edge_ay;
							dest[2] =
								edge_bz * dv_a -
								dv_b * edge_az;
							length_squared =
								dest[2] *
									dest[2] +
								dest[0] *
									dest[0] +
								dest[1] *
									dest[1];
							if (determinant ==
								    0.0f ||
							    length_squared ==
								    g_sw3d_zero_float) {
								vertices = (const struct
									    opt_vector
										    *)
									g_cur_mesh_vertices;
								edge_ax =
									vertices[face->vertex_indices
											 [1]]
										.x -
									vertices[face->vertex_indices
											 [2]]
										.x;
								edge_ay =
									vertices[face->vertex_indices
											 [1]]
										.y -
									vertices[face->vertex_indices
											 [2]]
										.y;
								edge_az =
									vertices[face->vertex_indices
											 [1]]
										.z -
									vertices[face->vertex_indices
											 [2]]
										.z;
								edge_bx =
									vertices[face->vertex_indices
											 [1]]
										.x -
									vertices[face->vertex_indices
											 [3]]
										.x;
								edge_by =
									vertices[face->vertex_indices
											 [1]]
										.y -
									vertices[face->vertex_indices
											 [3]]
										.y;
								edge_bz =
									vertices[face->vertex_indices
											 [1]]
										.z -
									vertices[face->vertex_indices
											 [3]]
										.z;
								tex_coords =
									(const struct
									 opt_tex_coord
										 *)
										g_cur_mesh_tex_coords;
								du_a = tex_coords[face->tex_coord_indices
											  [1]]
									       .u -
								       tex_coords[face->tex_coord_indices
											  [2]]
									       .u;
								dv_a = tex_coords[face->tex_coord_indices
											  [1]]
									       .v -
								       tex_coords[face->tex_coord_indices
											  [2]]
									       .v;
								du_b = tex_coords[face->tex_coord_indices
											  [1]]
									       .u -
								       tex_coords[face->tex_coord_indices
											  [3]]
									       .u;
								dv_b = tex_coords[face->tex_coord_indices
											  [1]]
									       .v -
								       tex_coords[face->tex_coord_indices
											  [3]]
									       .v;
								determinant =
									du_b * dv_a -
									du_a * dv_b;
								dest[0] =
									edge_bx *
										dv_a -
									dv_b * edge_ax;
								dest[1] =
									edge_by *
										dv_a -
									dv_b * edge_ay;
								dest[2] =
									edge_bz *
										dv_a -
									dv_b * edge_az;
								length_squared =
									dest[2] *
										dest[2] +
									dest[0] *
										dest[0] +
									dest[1] *
										dest[1];
								if (determinant ==
									    0.0f ||
								    length_squared ==
									    g_sw3d_zero_float) {
									dest[0] =
										edge_bx;
									dest[1] =
										edge_by;
									dest[2] =
										edge_bz;
									determinant =
										1.0f;
									du_a = 0.0f;
									du_b = 1.0f;
								}
							}
						}
					} else {
						dest[0] = edge_bx;
						dest[1] = edge_by;
						dest[2] = edge_bz;
						determinant = 1.0f;
						du_a = 0.0f;
						du_b = 1.0f;
					}
				}
				{
					float reciprocal =
						g_sw3d_unit_float / determinant;
					dest[0] *= reciprocal;
					dest[1] *= reciprocal;
					dest[2] *= reciprocal;
					reciprocal = -reciprocal;
					dest[3] = (du_a * edge_bx -
						   du_b * edge_ax) *
						  reciprocal;
					dest[4] = (du_a * edge_by -
						   du_b * edge_ay) *
						  reciprocal;
					dest[5] = (du_a * edge_bz -
						   du_b * edge_az) *
						  reciprocal;
				}
				if (du_a == 0.0f) {
					if (du_b == 0.0f) {
						dest[4] = 1.0f;
					}
				}
				dest += 6;
				++records;
			}
		}
	} else {
#ifdef XVT_MODERN
		// The packed buffer reserves two tangent vectors per face.
		dest += 6 * face_count;
#else
		dest += 24 * face_count;
#endif
	}
	if (((const struct scene_mesh *)conversion_state)->p_vert_normals !=
	    NULL) {
		g_generated_vertex_normal_count = 0;
	} else {
		g_generated_vertex_normal_count = g_cur_vertex_count;
		opt_model_build_vertex_normals_from_faces(dest, face_data,
							  face_count);
	}
}

/* Writes at dest, for each of the g_cur_vertex_count vertices, the mean of the
 * normals of the faces that use it, read after face_data's records; leaves the
 * entry as it was when no face uses it. Does not normalize the mean. Only the
 * original build calls this. */
// FUNCTION: XVT 0x479CE0
void opt_model_build_vertex_normals_from_faces(
	float *dest, const struct opt_packed_face_data *face_data,
	int face_count)
{
	int vertex_index = 0;
	if (g_cur_vertex_count <= 0) {
		return;
	}
	int face_record_bytes =
		face_count * (int)sizeof(struct opt_packed_face_record);
	float scale;
	do {
		int incident_face_count = 0;
		const struct opt_packed_face_record *face = face_data->records;
		const float *face_normal =
			(const float *)((const uint8_t *)face_data +
					sizeof(face_data->edge_count) +
					face_record_bytes);
		int remaining_faces = face_count;
		if (remaining_faces > 0) {
			do {
				if (face->vertex_indices[0] == vertex_index ||
				    face->vertex_indices[1] == vertex_index ||
				    face->vertex_indices[2] == vertex_index ||
				    face->vertex_indices[3] == vertex_index) {
					++incident_face_count;
					if (incident_face_count == 1) {
						dest[0] = face_normal[0];
						dest[1] = face_normal[1];
						dest[2] = face_normal[2];
					} else {
						dest[0] = dest[0] +
							  face_normal[0];
						dest[1] = face_normal[1] +
							  dest[1];
						dest[2] = face_normal[2] +
							  dest[2];
					}
				}
				++face;
				face_normal += 3;
				--remaining_faces;
			} while (remaining_faces != 0);
		}
		if (incident_face_count > 1) {
			if (incident_face_count < 65) {
				scale = g_sw3d_span_length_reciprocal
					[incident_face_count];
			} else {
				scale = g_sw3d_unit_float / incident_face_count;
			}
			dest[0] *= scale;
			dest[1] *= scale;
			dest[2] *= scale;
		}
		dest += 3;
		++vertex_index;
	} while (vertex_index < g_cur_vertex_count);
}
#endif

/* Returns the first node, roots in order and each depth first, whose name is
 * name ignoring case (opt_model_find_node_by_name), or NULL. */
// FUNCTION: XVT 0x479DE0
struct opt_node *
opt_model_resolve_node_ref(const struct optimized_poly_object *object,
			   const char *name)
{
	for (int root_index = 0; root_index < object->root_node_count;
	     ++root_index) {
		struct opt_node *result = opt_model_find_node_by_name(
			object->root_nodes[root_index], name);
		if (result != NULL) {
			return result;
		}
	}

	return NULL;
}

/* Returns node or the first node below it, depth first, whose name is name
 * ignoring case, or NULL. Does not follow OPT_NODEREF links. */
// FUNCTION: XVT 0x479E20
struct opt_node *opt_model_find_node_by_name(struct opt_node *node,
					     const char *name)
{
	if (node == NULL) {
		return NULL;
	}
	int name_compare;
	if (node->p_name != NULL) {
#ifdef XVT_MODERN
		name_compare = strcasecmp(node->p_name, name);
#else
		name_compare = _strcmpi(node->p_name, name);
#endif
		if (name_compare == 0) {
			return node;
		}
	}

	for (int child_index = 0; child_index < node->child_count;
	     ++child_index) {
		struct opt_node *result = opt_model_find_node_by_name(
			node->p_children[child_index], name);
		if (result != NULL) {
			return result;
		}
	}

	return NULL;
}

#ifndef XVT_MODERN
/* Returns the bytes a packed texture for source_file_name will take. A name
 * ending in "rgb", ignoring case, is tried first as the same name ending in
 * "tex", then as the .rgb file; a name ending in "tex" is read directly. For a
 * .tex file it reads the header and returns 12312 plus the texel bytes:
 * stored_payload_size when pixel_count equals width times height, else width times
 * height. For an .rgb file it reads the big-endian width and height at bytes 6
 * and 8 and returns 3 times their product plus 13056. Returns 12376, the size
 * of a model_texture_default_texture in the original build, when no file opens or
 * the extension is neither. Only the original build calls this. */
// FUNCTION: XVT 0x47A430
int opt_model_get_external_texture_serialized_size(const char *source_file_name)
{
	char file_name[256];

	strcpy(file_name, source_file_name);
	char *extension = file_name + strlen(file_name) - 3;
	xvt_file *texture_stream;
	if (_strcmpi(extension, g_ext_rgb) == 0) {
		extension[0] = 't';
		extension[1] = 'e';
		extension[2] = 'x';
		fe_disk_io_open_global_stream(file_name,
					      g_file_mode_read_binary, 0, 0);
		texture_stream = g_stream;
		extension[0] = 'r';
		extension[1] = 'g';
		extension[2] = 'b';
		if (texture_stream == NULL) {
			fe_disk_io_open_global_stream(
				file_name, g_file_mode_read_binary, 0, 0);
			texture_stream = g_stream;
			if (texture_stream != NULL) {
				uint8_t rgb_header[32];
				FILE_RAW_READ(rgb_header, 16, 1,
					      texture_stream);
				int pixel_count =
					((unsigned int)rgb_header[6] << 8) +
					rgb_header[7];
				pixel_count *=
					((unsigned int)rgb_header[8] << 8) +
					rgb_header[9];
				FILE_RAW_CLOSE(texture_stream);
				int serialized_size = 3 * pixel_count + 13056;
				return serialized_size;
			}
			return 12376;
		}
	} else {
		if (_strcmpi(extension, g_ext_tex) != 0) {
			return 12376;
		}
		fe_disk_io_open_global_stream(file_name,
					      g_file_mode_read_binary, 0, 0);
		texture_stream = g_stream;
		if (texture_stream == NULL) {
			return 12376;
		}
	}
	struct opt_external_tex_header tex_header;
	FILE_RAW_READ(&tex_header, sizeof(tex_header), 1, texture_stream);
	FILE_RAW_CLOSE(texture_stream);
	int payload_size = tex_header.width * tex_header.height;
	if (tex_header.pixel_count == payload_size) {
		payload_size = tex_header.stored_payload_size;
	}
	return payload_size + 12312;
}
#endif
