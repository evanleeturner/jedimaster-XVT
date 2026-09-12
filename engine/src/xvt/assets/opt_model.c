#include "xvt/assets/opt_model.h"

#ifdef XVT_MODERN
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
#ifndef XVT_MODERN
int _access(const char* filename, int mode);
#else
int access(const char* filename, int mode);
#endif

#ifndef XVT_MODERN
typedef struct OptLegacyParamRecord {
	int value0;
	int value1;
	void* data;
} OptLegacyParamRecord;

typedef struct OptExternalTexHeader {
	uint8_t prefix[8];
	int pixelCount;
	int storedPayloadSize;
	int width;
	int height;
} OptExternalTexHeader;

typedef enum InventorFieldType {
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
} InventorFieldType;

typedef struct InventorEnumDef {
	int valueCount;
	const char* const* valueNames;
	const int* values;
} InventorEnumDef;

typedef struct InventorFieldDef {
	const char* fieldName;
	InventorFieldType fieldType;
	const InventorEnumDef* enumDef;
	const OptLegacyParamRecord* defaultRecord;
	const void* defaultData;
	size_t defaultDataSize;
} InventorFieldDef;

// GLOBAL: XVT 0x51AF28
static const OptLegacyParamRecord g_defaultIntegerListRecord = { INVENTOR_FIELD_INTEGER_LIST, 4, NULL };
// GLOBAL: XVT 0x51AF38
static const int g_defaultIntegerList[4] = { 0, 1, 2, -1 };
// GLOBAL: XVT 0x51AF58
static const OptLegacyParamRecord g_defaultIntegerRecord = { INVENTOR_FIELD_INTEGER, 1, NULL };
// GLOBAL: XVT 0x51AF64
static const int g_defaultInteger = 0;
// GLOBAL: XVT 0x51AF48
static const OptLegacyParamRecord g_defaultStringRecord = { INVENTOR_FIELD_STRING, 1, NULL };
// GLOBAL: XVT 0x51AF54
static const char g_defaultString[1] = { '\0' };
// GLOBAL: XVT 0x51AFB8
static const OptLegacyParamRecord g_defaultVector3ListRecord = { INVENTOR_FIELD_VECTOR3_LIST, 1, NULL };
// GLOBAL: XVT 0x51AFF0
static const OptLegacyParamRecord g_defaultVector3Record = { INVENTOR_FIELD_VECTOR3, 1, NULL };
// GLOBAL: XVT 0x51AFC8
static const OptVector g_defaultZeroVector = { 0.0f, 0.0f, 0.0f };
// GLOBAL: XVT 0x51B000
static const OptVector g_defaultZeroScalarVector = { 0.0f, 0.0f, 0.0f };
// GLOBAL: XVT 0x51B020
static const OptVector g_defaultUnitVector = { 1.0f, 1.0f, 1.0f };
// GLOBAL: XVT 0x51B030
static const OptLegacyParamRecord g_defaultRotationRecord = { INVENTOR_FIELD_ROTATION, 1, NULL };
// GLOBAL: XVT 0x51B040
static const float g_defaultRotation[4] = { 0.0f, 0.0f, 1.0f, 0.0f };
// GLOBAL: XVT 0x51B050
static const OptLegacyParamRecord g_defaultColorListRecord = { INVENTOR_FIELD_COLOR_LIST, 1, NULL };
// GLOBAL: XVT 0x51B090
static const OptLegacyParamRecord g_defaultColorRecord = { INVENTOR_FIELD_COLOR, 1, NULL };
// GLOBAL: XVT 0x51B060
static const OptVector g_defaultAmbientColor = { 0.2f, 0.2f, 0.2f };
// GLOBAL: XVT 0x51B070
static const OptVector g_defaultDiffuseColor = { 0.8f, 0.8f, 0.8f };
// GLOBAL: XVT 0x51B080
static const OptVector g_defaultBlackColor = { 0.0f, 0.0f, 0.0f };
// GLOBAL: XVT 0x51B0A0
static const OptVector g_defaultRgbColor = { 0.8f, 0.8f, 0.8f };
// GLOBAL: XVT 0x51B0B0
static const OptLegacyParamRecord g_defaultFloatRecord = { INVENTOR_FIELD_FLOAT, 1, NULL };
// GLOBAL: XVT 0x51B0BC
static const float g_defaultZeroFloat = 0.0f;
// GLOBAL: XVT 0x51B0C0
static const float g_defaultShininess = 0.2f;
// GLOBAL: XVT 0x51AFD8
static const OptLegacyParamRecord g_defaultVector2ListRecord = { INVENTOR_FIELD_VECTOR2_LIST, 1, NULL };
// GLOBAL: XVT 0x51AFE8
static const float g_defaultVector2[2] = { 0.0f, 0.0f };
// GLOBAL: XVT 0x51AF18
static const OptLegacyParamRecord g_defaultEnumRecord = { INVENTOR_FIELD_ENUM, 1, NULL };
// GLOBAL: XVT 0x51AF24
static const int g_defaultEnum = 0;

// GLOBAL: XVT 0x51B320
static const char* const g_bindingNames[9] = {
	"DEFAULT",          "NONE",       "OVERALL",           "PER_PART", "PER_PART_INDEXED", "PER_FACE",
	"PER_FACE_INDEXED", "PER_VERTEX", "PER_VERTEX_INDEXED"
};
// GLOBAL: XVT 0x51B348
static const int g_bindingValues[9] = { 0, 1, 2, 3, 4, 5, 6, 7, 8 };
// GLOBAL: XVT 0x51B370
static const InventorEnumDef g_bindingEnum = { 9, g_bindingNames, g_bindingValues };
// GLOBAL: XVT 0x51B400
static const char* const g_wrapNames[2] = { "REPEAT", "CLAMP" };
// GLOBAL: XVT 0x51B408
static const int g_wrapValues[2] = { 0, 1 };
// GLOBAL: XVT 0x51B410
static const InventorEnumDef g_wrapEnum = { 2, g_wrapNames, g_wrapValues };
// GLOBAL: XVT 0x51B460
static const char* const g_textureModelNames[3] = { "MODULATE", "DECAL", "BLEND" };
// GLOBAL: XVT 0x51B470
static const int g_textureModelValues[3] = { 0, 1, 2 };
// GLOBAL: XVT 0x51B480
static const InventorEnumDef g_textureModelEnum = { 3, g_textureModelNames, g_textureModelValues };
// GLOBAL: XVT 0x51B7A0
static const char* const g_componentTypeNames[32] = {
	"COMPTYPE_DEFAULT",    "COMPTYPE_MAINHULL",       "COMPTYPE_WING",        "COMPTYPE_FUSELAGE",
	"COMPTYPE_GUNTURRET",  "COMPTYPE_SMALLGUN",       "COMPTYPE_ENGINE",      "COMPTYPE_BRIDGE",
	"COMPTYPE_SHIELDGEN",  "COMPTYPE_ENERGYGEN",      "COMPTYPE_LAUNCHER",    "COMPTYPE_COMMSYS",
	"COMPTYPE_BEAMSYS",    "COMPTYPE_COMMANDBEAM",    "COMPTYPE_DOCKINGPLAT", "COMPTYPE_LANDINGPLAT",
	"COMPTYPE_HANGAR",     "COMPTYPE_CARGOPOD",       "COMPTYPE_MISCHULL",    "COMPTYPE_ANTENNA",
	"COMPTYPE_ROTWING",    "COMPTYPE_ROTGUNTURRET",   "COMPTYPE_ROTLAUNCHER", "COMPTYPE_ROTCOMMSYS",
	"COMPTYPE_ROTBEAMSYS", "COMPTYPE_ROTCOMMANDBEAM", "COMPTYPE_CUSTOM1",     "COMPTYPE_CUSTOM2",
	"COMPTYPE_CUSTOM3",    "COMPTYPE_CUSTOM4",        "COMPTYPE_CUSTOM5",     "COMPTYPE_CUSTOM6",
};
// GLOBAL: XVT 0x51B820
static const int g_componentTypeValues[32] = {
	0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,
	16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31
};
// GLOBAL: XVT 0x51B8A0
static const InventorEnumDef g_componentTypeEnum = { 41, g_componentTypeNames, g_componentTypeValues };
// GLOBAL: XVT 0x51B958
static const char* const g_hardpointTypeNames[32] = {
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
// GLOBAL: XVT 0x51B9D8
static const int g_hardpointTypeValues[32] = {
	0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,
	16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31
};
// GLOBAL: XVT 0x51BA58
static const InventorEnumDef g_hardpointTypeEnum = { 41, g_hardpointTypeNames, g_hardpointTypeValues };

// GLOBAL: XVT 0x51B0C8
static const InventorFieldDef g_fieldCoordIndex = {
	"coordIndex",          INVENTOR_FIELD_INTEGER_LIST, NULL, &g_defaultIntegerListRecord,
	&g_defaultIntegerList, sizeof(g_defaultIntegerList)
};
// GLOBAL: XVT 0x51B0E0
static const InventorFieldDef g_fieldMaterialIndex = {
	"materialIndex",       INVENTOR_FIELD_INTEGER_LIST, NULL, &g_defaultIntegerListRecord,
	&g_defaultIntegerList, sizeof(g_defaultIntegerList)
};
// GLOBAL: XVT 0x51B0F8
static const InventorFieldDef g_fieldNormalIndex = {
	"normalIndex",         INVENTOR_FIELD_INTEGER_LIST, NULL, &g_defaultIntegerListRecord,
	&g_defaultIntegerList, sizeof(g_defaultIntegerList)
};
// GLOBAL: XVT 0x51B110
static const InventorFieldDef g_fieldTextureCoordIndex = {
	"textureCoordIndex",         INVENTOR_FIELD_INTEGER_LIST, NULL,
	&g_defaultIntegerListRecord, &g_defaultIntegerList,       sizeof(g_defaultIntegerList)
};
// GLOBAL: XVT 0x51B128
static const InventorFieldDef g_fieldNumVertices = {
	"numVertices",         INVENTOR_FIELD_INTEGER_LIST, NULL, &g_defaultIntegerListRecord,
	&g_defaultIntegerList, sizeof(g_defaultIntegerList)
};
// GLOBAL: XVT 0x51B140
static const InventorFieldDef g_fieldStartIndex = {
	"startIndex",      INVENTOR_FIELD_INTEGER,  NULL, &g_defaultIntegerRecord,
	&g_defaultInteger, sizeof(g_defaultInteger)
};
// GLOBAL: XVT 0x51B158
static const InventorFieldDef g_fieldVerticesPerRow = {
	"verticesPerRow",        INVENTOR_FIELD_INTEGER, NULL,
	&g_defaultIntegerRecord, &g_defaultInteger,      sizeof(g_defaultInteger)
};
// GLOBAL: XVT 0x51B170
static const InventorFieldDef g_fieldVerticesPerColumn = {
	"verticesPerColumn",     INVENTOR_FIELD_INTEGER, NULL,
	&g_defaultIntegerRecord, &g_defaultInteger,      sizeof(g_defaultInteger)
};
// GLOBAL: XVT 0x51B1A0
static const InventorFieldDef g_fieldPoint2 = {
	"point",           INVENTOR_FIELD_VECTOR2_LIST, NULL, &g_defaultVector2ListRecord,
	&g_defaultVector2, sizeof(g_defaultVector2)
};
// GLOBAL: XVT 0x51B1B8
static const InventorFieldDef g_fieldPoint3 = { "point",
												INVENTOR_FIELD_VECTOR3_LIST,
												NULL,
												&g_defaultVector3ListRecord,
												&g_defaultZeroVector,
												sizeof(g_defaultZeroVector) };
// GLOBAL: XVT 0x51B1D0
static const InventorFieldDef g_fieldVector = { "vector",
												INVENTOR_FIELD_VECTOR3_LIST,
												NULL,
												&g_defaultVector3ListRecord,
												&g_defaultZeroVector,
												sizeof(g_defaultZeroVector) };
// GLOBAL: XVT 0x51B1E8
static const InventorFieldDef g_fieldTranslation = {
	"translation",           INVENTOR_FIELD_VECTOR3,     NULL,
	&g_defaultVector3Record, &g_defaultZeroScalarVector, sizeof(g_defaultZeroScalarVector)
};
// GLOBAL: XVT 0x51B200
static const InventorFieldDef g_fieldScaleFactor = {
	"scaleFactor",           INVENTOR_FIELD_VECTOR3, NULL,
	&g_defaultVector3Record, &g_defaultUnitVector,   sizeof(g_defaultUnitVector)
};
// GLOBAL: XVT 0x51B218
static const InventorFieldDef g_fieldCenter = { "center",
												INVENTOR_FIELD_VECTOR3,
												NULL,
												&g_defaultVector3Record,
												&g_defaultZeroScalarVector,
												sizeof(g_defaultZeroScalarVector) };
// GLOBAL: XVT 0x51B230
static const InventorFieldDef g_fieldRotation = {
	"rotation",         INVENTOR_FIELD_ROTATION,  NULL, &g_defaultRotationRecord,
	&g_defaultRotation, sizeof(g_defaultRotation)
};
// GLOBAL: XVT 0x51B248
static const InventorFieldDef g_fieldScaleOrientation = {
	"scaleOrientation",       INVENTOR_FIELD_ROTATION, NULL,
	&g_defaultRotationRecord, &g_defaultRotation,      sizeof(g_defaultRotation)
};
// GLOBAL: XVT 0x51B260
static const InventorFieldDef g_fieldAmbientColor = {
	"ambientColor",         INVENTOR_FIELD_COLOR_LIST,    NULL, &g_defaultColorListRecord,
	&g_defaultAmbientColor, sizeof(g_defaultAmbientColor)
};
// GLOBAL: XVT 0x51B278
static const InventorFieldDef g_fieldDiffuseColor = {
	"diffuseColor",         INVENTOR_FIELD_COLOR_LIST,    NULL, &g_defaultColorListRecord,
	&g_defaultDiffuseColor, sizeof(g_defaultDiffuseColor)
};
// GLOBAL: XVT 0x51B290
static const InventorFieldDef g_fieldSpecularColor = {
	"specularColor",           INVENTOR_FIELD_COLOR_LIST, NULL,
	&g_defaultColorListRecord, &g_defaultBlackColor,      sizeof(g_defaultBlackColor)
};
// GLOBAL: XVT 0x51B2A8
static const InventorFieldDef g_fieldEmissiveColor = {
	"emissiveColor",           INVENTOR_FIELD_COLOR_LIST, NULL,
	&g_defaultColorListRecord, &g_defaultBlackColor,      sizeof(g_defaultBlackColor)
};
// GLOBAL: XVT 0x51B2C0
static const InventorFieldDef g_fieldRgb = {
	"rgb", INVENTOR_FIELD_COLOR, NULL, &g_defaultColorRecord, &g_defaultRgbColor, sizeof(g_defaultRgbColor)
};
// GLOBAL: XVT 0x51B2D8
static const InventorFieldDef g_fieldScreenArea = {
	"screenArea",          INVENTOR_FIELD_FLOAT, NULL,
	&g_defaultFloatRecord, &g_defaultZeroFloat,  sizeof(g_defaultZeroFloat)
};
// GLOBAL: XVT 0x51B2F0
static const InventorFieldDef g_fieldShininess = {
	"shininess",           INVENTOR_FIELD_FLOAT, NULL,
	&g_defaultFloatRecord, &g_defaultShininess,  sizeof(g_defaultShininess)
};
// GLOBAL: XVT 0x51B308
static const InventorFieldDef g_fieldTransparency = {
	"transparency",        INVENTOR_FIELD_FLOAT, NULL,
	&g_defaultFloatRecord, &g_defaultZeroFloat,  sizeof(g_defaultZeroFloat)
};
// GLOBAL: XVT 0x51B380
static const InventorFieldDef g_fieldBindingValue = { "value",        INVENTOR_FIELD_ENUM,
													  &g_bindingEnum, &g_defaultEnumRecord,
													  &g_defaultEnum, sizeof(g_defaultEnum) };
// GLOBAL: XVT 0x51B3E8
static const InventorFieldDef g_fieldFilename = {
	"filename", INVENTOR_FIELD_STRING, NULL, &g_defaultStringRecord, &g_defaultString, sizeof(g_defaultString)
};
// GLOBAL: XVT 0x51B420
static const InventorFieldDef g_fieldWrapS = { "wrapS",        INVENTOR_FIELD_ENUM,
											   &g_wrapEnum,    &g_defaultEnumRecord,
											   &g_defaultEnum, sizeof(g_defaultEnum) };
// GLOBAL: XVT 0x51B448
static const InventorFieldDef g_fieldWrapT = { "wrapT",        INVENTOR_FIELD_ENUM,
											   &g_wrapEnum,    &g_defaultEnumRecord,
											   &g_defaultEnum, sizeof(g_defaultEnum) };
// GLOBAL: XVT 0x51B490
static const InventorFieldDef g_fieldTextureModel = {
	"model",        INVENTOR_FIELD_ENUM,  &g_textureModelEnum, &g_defaultEnumRecord,
	&g_defaultEnum, sizeof(g_defaultEnum)
};
// GLOBAL: XVT 0x51B4A8
static const InventorFieldDef g_fieldBlendColor = {
	"blendColor",          INVENTOR_FIELD_COLOR, NULL,
	&g_defaultColorRecord, &g_defaultRgbColor,   sizeof(g_defaultRgbColor)
};
// GLOBAL: XVT 0x51B8B0
static const InventorFieldDef g_fieldComponentType = {
	"type",         INVENTOR_FIELD_ENUM,  &g_componentTypeEnum, &g_defaultEnumRecord,
	&g_defaultEnum, sizeof(g_defaultEnum)
};
// GLOBAL: XVT 0x51B8C8
static const InventorFieldDef g_fieldSize = { "size",
											  INVENTOR_FIELD_VECTOR3,
											  NULL,
											  &g_defaultVector3Record,
											  &g_defaultZeroScalarVector,
											  sizeof(g_defaultZeroScalarVector) };
// GLOBAL: XVT 0x51B8E0
static const InventorFieldDef g_fieldMinVector = { "minvector",
												   INVENTOR_FIELD_VECTOR3,
												   NULL,
												   &g_defaultVector3Record,
												   &g_defaultZeroScalarVector,
												   sizeof(g_defaultZeroScalarVector) };
// GLOBAL: XVT 0x51B8F8
static const InventorFieldDef g_fieldMaxVector = { "maxvector",
												   INVENTOR_FIELD_VECTOR3,
												   NULL,
												   &g_defaultVector3Record,
												   &g_defaultZeroScalarVector,
												   sizeof(g_defaultZeroScalarVector) };
// GLOBAL: XVT 0x51B910
static const InventorFieldDef g_fieldGroupCenter = {
	"groupcenter",           INVENTOR_FIELD_VECTOR3,     NULL,
	&g_defaultVector3Record, &g_defaultZeroScalarVector, sizeof(g_defaultZeroScalarVector)
};
// GLOBAL: XVT 0x51B928
static const InventorFieldDef g_fieldFlags = {
	"flags",           INVENTOR_FIELD_INTEGER,  NULL, &g_defaultIntegerRecord,
	&g_defaultInteger, sizeof(g_defaultInteger)
};
// GLOBAL: XVT 0x51B940
static const InventorFieldDef g_fieldGroupId = {
	"groupid",         INVENTOR_FIELD_INTEGER,  NULL, &g_defaultIntegerRecord,
	&g_defaultInteger, sizeof(g_defaultInteger)
};
// GLOBAL: XVT 0x51BA68
static const InventorFieldDef g_fieldHardpointType = {
	"type",         INVENTOR_FIELD_ENUM,  &g_hardpointTypeEnum, &g_defaultEnumRecord,
	&g_defaultEnum, sizeof(g_defaultEnum)
};
// GLOBAL: XVT 0x51BA80
static const InventorFieldDef g_fieldAxis1 = { "axis1",
											   INVENTOR_FIELD_VECTOR3,
											   NULL,
											   &g_defaultVector3Record,
											   &g_defaultZeroScalarVector,
											   sizeof(g_defaultZeroScalarVector) };
// GLOBAL: XVT 0x51BA98
static const InventorFieldDef g_fieldAxis2 = { "axis2",
											   INVENTOR_FIELD_VECTOR3,
											   NULL,
											   &g_defaultVector3Record,
											   &g_defaultZeroScalarVector,
											   sizeof(g_defaultZeroScalarVector) };
// GLOBAL: XVT 0x51BAB0
static const InventorFieldDef g_fieldAxis3 = { "axis3",
											   INVENTOR_FIELD_VECTOR3,
											   NULL,
											   &g_defaultVector3Record,
											   &g_defaultZeroScalarVector,
											   sizeof(g_defaultZeroScalarVector) };

// GLOBAL: XVT 0x51BAD8
static const InventorFieldDef* const g_fieldsIndexedFaceSet[] = { &g_fieldCoordIndex, &g_fieldMaterialIndex,
																  &g_fieldNormalIndex,
																  &g_fieldTextureCoordIndex };
// GLOBAL: XVT 0x51BAF8
static const InventorFieldDef* const g_fieldsTransform[] = { &g_fieldTranslation, &g_fieldRotation,
															 &g_fieldScaleFactor, &g_fieldScaleOrientation,
															 &g_fieldCenter };
// GLOBAL: XVT 0x51BB1C
static const InventorFieldDef* const g_fieldsCoordinate3[] = { &g_fieldPoint3 };
// GLOBAL: XVT 0x51BB2C
static const InventorFieldDef* const g_fieldsTranslation[] = { &g_fieldTranslation };
// GLOBAL: XVT 0x51BB3C
static const InventorFieldDef* const g_fieldsRotation[] = { &g_fieldRotation };
// GLOBAL: XVT 0x51BB4C
static const InventorFieldDef* const g_fieldsScale[] = { &g_fieldScaleFactor };
// GLOBAL: XVT 0x51BB5C
static const InventorFieldDef* const g_fieldsUse[] = { &g_fieldFilename };
// GLOBAL: XVT 0x51BB80
static const InventorFieldDef* const g_fieldsMaterial[] = { &g_fieldAmbientColor,  &g_fieldDiffuseColor,
															&g_fieldSpecularColor, &g_fieldEmissiveColor,
															&g_fieldShininess,     &g_fieldTransparency };
// GLOBAL: XVT 0x51BBA4
static const InventorFieldDef* const g_fieldsBinding[] = { &g_fieldBindingValue };
// GLOBAL: XVT 0x51BBB4
static const InventorFieldDef* const g_fieldsNormal[] = { &g_fieldVector };
// GLOBAL: XVT 0x51BBD4
static const InventorFieldDef* const g_fieldsTextureCoordinate2[] = { &g_fieldPoint2 };
// GLOBAL: XVT 0x51BBF8
static const InventorFieldDef* const g_fieldsQuadMesh[] = { &g_fieldStartIndex, &g_fieldVerticesPerRow,
															&g_fieldVerticesPerColumn };
// GLOBAL: XVT 0x51BC14
static const InventorFieldDef* const g_fieldsFaceSet[] = { &g_fieldNumVertices };
// GLOBAL: XVT 0x51BC44
static const InventorFieldDef* const g_fieldsBaseColor[] = { &g_fieldRgb };
// GLOBAL: XVT 0x51BC58
static const InventorFieldDef* const g_fieldsTexture2[] = { &g_fieldFilename, &g_fieldWrapS, &g_fieldWrapT,
															&g_fieldTextureModel, &g_fieldBlendColor };
// GLOBAL: XVT 0x51BC7C
static const InventorFieldDef* const g_fieldsLevelOfDetail[] = { &g_fieldScreenArea };
// GLOBAL: XVT 0x51BC90
static const InventorFieldDef* const g_fieldsHardpoint[] = { &g_fieldHardpointType, &g_fieldPoint3 };
// GLOBAL: XVT 0x51BCA8
static const InventorFieldDef* const g_fieldsPivot[] = { &g_fieldCenter, &g_fieldAxis1, &g_fieldAxis2,
														 &g_fieldAxis3 };
// GLOBAL: XVT 0x51BCD8
static const InventorFieldDef* const g_fieldsComponentInfo[] = {
	&g_fieldComponentType, &g_fieldFlags,     &g_fieldSize,    &g_fieldCenter,
	&g_fieldMinVector,     &g_fieldMaxVector, &g_fieldGroupId, &g_fieldGroupCenter,
};

// GLOBAL: XVT 0x51BAC8
static const InventorNodeDef g_inventorNodeDefData[26] = {
	{ "separator", 0, NULL },
	{ "indexedFaceSet", 4, g_fieldsIndexedFaceSet },
	{ "transform", 5, g_fieldsTransform },
	{ "coordinate3", 1, g_fieldsCoordinate3 },
	{ "translation", 1, g_fieldsTranslation },
	{ "rotation", 1, g_fieldsRotation },
	{ "scale", 1, g_fieldsScale },
	{ "use", 1, g_fieldsUse },
	{ "def", 0, NULL },
	{ "material", 6, g_fieldsMaterial },
	{ "materialBinding", 1, g_fieldsBinding },
	{ "normal", 1, g_fieldsNormal },
	{ "normalBinding", 1, g_fieldsBinding },
	{ "textureCoordinate2", 1, g_fieldsTextureCoordinate2 },
	{ "textureCoordinateBinding", 1, g_fieldsBinding },
	{ "quadMesh", 3, g_fieldsQuadMesh },
	{ "faceSet", 1, g_fieldsFaceSet },
	{ "triangleStripSet", 1, g_fieldsFaceSet },
	{ "group", 0, NULL },
	{ "baseColor", 1, g_fieldsBaseColor },
	{ "texture2", 5, g_fieldsTexture2 },
	{ "levelofdetail", 1, g_fieldsLevelOfDetail },
	{ "hardpoint", 2, g_fieldsHardpoint },
	{ "pivot", 4, g_fieldsPivot },
	{ "camoswitch", 0, NULL },
	{ "componentInfo", 8, g_fieldsComponentInfo },
};

// GLOBAL: XVT 0x51BD08
const InventorNodeDef* const g_inventorNodeDefs[26] = {
	&g_inventorNodeDefData[0],  &g_inventorNodeDefData[1],  &g_inventorNodeDefData[2],
	&g_inventorNodeDefData[3],  &g_inventorNodeDefData[4],  &g_inventorNodeDefData[5],
	&g_inventorNodeDefData[6],  &g_inventorNodeDefData[7],  &g_inventorNodeDefData[8],
	&g_inventorNodeDefData[9],  &g_inventorNodeDefData[10], &g_inventorNodeDefData[11],
	&g_inventorNodeDefData[12], &g_inventorNodeDefData[13], &g_inventorNodeDefData[14],
	&g_inventorNodeDefData[15], &g_inventorNodeDefData[16], &g_inventorNodeDefData[17],
	&g_inventorNodeDefData[18], &g_inventorNodeDefData[19], &g_inventorNodeDefData[20],
	&g_inventorNodeDefData[21], &g_inventorNodeDefData[22], &g_inventorNodeDefData[23],
	&g_inventorNodeDefData[24], &g_inventorNodeDefData[25],
};

// GLOBAL: XVT 0x5505F8
uint8_t g_inventorFieldSeen[256] = { 0 };
// GLOBAL: XVT 0x5506F8
char g_optModelLoadScratchBuffer[257] = { 0 };
#endif

// GLOBAL: XVT 0x51C560
ModelDef g_modelDefs[73] = {
#include "xvt/assets/model_defs_data.inc"
};
// GLOBAL: XVT 0x5181C0
const float g_sw3dUnitFloat = 1.0f;
// GLOBAL: XVT 0x5181C4
const float g_sw3dTriangleCornerCount = 3.0f;
// GLOBAL: XVT 0x5181C8
const float g_sw3dQuadCornerCount = 4.0f;
// GLOBAL: XVT 0x5181B8
const float g_sw3dZeroFloat = 0.0f;
// GLOBAL: XVT 0x5181CC
const float g_sw3dDistantDepth = 100000.0f;
// GLOBAL: XVT 0x5270B0
int g_cacheResolvedOptNodeRefs = 1;
// GLOBAL: XVT 0x5272B0
int g_optSourceIsVersion0 = 0;
#ifndef XVT_MODERN
// GLOBAL: XVT 0x51AE00
const char g_extRgb[4] = "rgb";
// GLOBAL: XVT 0x5272CC
const char g_extTex[4] = "tex";
#endif
// GLOBAL: XVT 0x5270B8
const float g_sw3dSpanLengthReciprocal[70] = {
	1.0f,         1.0f,         1.0f / 2.0f,  1.0f / 3.0f,  1.0f / 4.0f,  1.0f / 5.0f,  1.0f / 6.0f,
	1.0f / 7.0f,  1.0f / 8.0f,  1.0f / 9.0f,  1.0f / 10.0f, 1.0f / 11.0f, 1.0f / 12.0f, 1.0f / 13.0f,
	1.0f / 14.0f, 1.0f / 15.0f, 1.0f / 16.0f, 1.0f / 17.0f, 1.0f / 18.0f, 1.0f / 19.0f, 1.0f / 20.0f,
	1.0f / 21.0f, 1.0f / 22.0f, 1.0f / 23.0f, 1.0f / 24.0f, 1.0f / 25.0f, 1.0f / 26.0f, 1.0f / 27.0f,
	1.0f / 28.0f, 1.0f / 29.0f, 1.0f / 30.0f, 1.0f / 31.0f, 1.0f / 32.0f, 1.0f / 33.0f, 1.0f / 34.0f,
	1.0f / 35.0f, 1.0f / 36.0f, 1.0f / 37.0f, 1.0f / 38.0f, 1.0f / 39.0f, 1.0f / 40.0f, 1.0f / 41.0f,
	1.0f / 42.0f, 1.0f / 43.0f, 1.0f / 44.0f, 1.0f / 45.0f, 1.0f / 46.0f, 1.0f / 47.0f, 1.0f / 48.0f,
	1.0f / 49.0f, 1.0f / 50.0f, 1.0f / 51.0f, 1.0f / 52.0f, 1.0f / 53.0f, 1.0f / 54.0f, 1.0f / 55.0f,
	1.0f / 56.0f, 1.0f / 57.0f, 1.0f / 58.0f, 1.0f / 59.0f, 1.0f / 60.0f, 1.0f / 61.0f, 1.0f / 62.0f,
	1.0f / 63.0f, 1.0f / 64.0f, 1.0f / 65.0f, 1.0f / 66.0f, 1.0f / 67.0f, 1.0f / 68.0f, 1.0f / 69.0f,
};
// GLOBAL: XVT 0x9A7ED0
uint16_t g_loadedModels[201] = { 0 };
// GLOBAL: XVT 0x60F1E4
int g_optConvertVectorSearchCursor = 0;
// GLOBAL: XVT 0x60F1FC
int g_optConvertTexCoordSearchCursor = 0;
// GLOBAL: XVT 0x5272A8
uint16_t g_loadOptBufHandle = 0;
// GLOBAL: XVT 0x5272AC
int g_loadOptBufSize = 0;
// GLOBAL: XVT 0x5272B4
uint16_t g_optConvertSourceHandle = 0;
// GLOBAL: XVT 0x5272B8
unsigned int g_optConvertSourceBufSize = 0;
// GLOBAL: XVT 0x60F204
int g_curVertexCount = 0;
// GLOBAL: XVT 0x60F1C0
void* g_modelNodeWalkUnusedScratch0 = NULL;
// GLOBAL: XVT 0x60F1D8
void* g_curMeshFlags = NULL;
// GLOBAL: XVT 0x60F1E8
void* g_modelNodeWalkUnusedScratch1 = NULL;
// GLOBAL: XVT 0x60F1F0
void* g_modelNodeWalkUnusedScratch2 = NULL;
// GLOBAL: XVT 0x60F208
OptVector* g_curVertNormals = NULL;
#ifndef XVT_MODERN
// GLOBAL: XVT 0x5272C0
int g_optImportScratchVectorCount = 0;
// GLOBAL: XVT 0x5272C4
OptVector* g_optImportScratchVectors = NULL;
// GLOBAL: XVT 0x5271D0
int g_optModelInvertFaceNormals = 0;
// GLOBAL: XVT 0x60F1D4
int g_generatedVertexNormalCount = 0;
#endif
// GLOBAL: XVT 0x5272BC
static int g_optConvertTargetFaceFound = 0;
// GLOBAL: XVT 0x60F1C8
OptNode* g_optConvertSourceMeshNode = NULL;
// GLOBAL: XVT 0x60F1CC
OptNode* g_optConvertVertexNormalNode = NULL;
// GLOBAL: XVT 0x60F1DC
OptNode* g_optConvertSourceTextureNode = NULL;
// GLOBAL: XVT 0x60F1F4
OptNode* g_optConvertTexCoordNode = NULL;
// GLOBAL: XVT 0x60F1F8
OptNode* g_optConvertFaceTextureNode = NULL;
// GLOBAL: XVT 0x60F200
OptNode* g_optConvertVertexNode = NULL;

// FLAGS: /O2 /G5
// FUNCTION: XVT 0x411E00
uint16_t OptModel_LoadHandle(const char* modelFilename) {
#ifdef XVT_MODERN
	char fileName[257];
	uint16_t fileHandle;
	uint16_t runtimeHandle;

	if (!modelFilename || strlen(modelFilename) >= sizeof(fileName))
		return 0;
	strcpy(fileName, modelFilename);
	fileHandle = OptModel_LoadFileToHandle(fileName);
	if (!fileHandle)
		return 0;
	FlightLoading_PulseAndDrawProgressScreen();
	runtimeHandle = OptModel_CreateRuntimeHandle(fileHandle);
	XvtRenderAssets_RegisterOpt(runtimeHandle, modelFilename);
	FlightLoading_PulseAndDrawProgressScreen();
	return runtimeHandle;
#else
	int extensionIndex;
	uint16_t importedHandle;
	XvtFile* stream;
	uint16_t packedHandle;
	uint16_t fileHandle;
	uint16_t runtimeHandle;
	int compareResult;

	strcpy(g_optModelLoadScratchBuffer, modelFilename);
	File_OpenGlobalStream(g_optModelLoadScratchBuffer, g_fileModeReadBinary, 0, 0);
	if (g_stream == NULL) {
		for (extensionIndex = 0; g_optModelLoadScratchBuffer[extensionIndex] != '.'; ++extensionIndex) {
		}
		g_optModelLoadScratchBuffer[extensionIndex] = '\0';
		strcat(g_optModelLoadScratchBuffer, ".iv");
		File_OpenGlobalStream(g_optModelLoadScratchBuffer, g_fileModeReadBinary, 1, 0);
		stream = (XvtFile*)g_stream;
		if (stream == NULL) {
			return 0;
		}
		if (File_Scanf(stream, "%256s", g_optModelLoadScratchBuffer) != 1) {
			return 0;
		}
		compareResult = _strnicmp(g_optModelLoadScratchBuffer, "#inventor", sizeof("#inventor") - 1);
		if (compareResult != 0) {
			return 0;
		}
		if (File_Scanf(stream, " %256s", g_optModelLoadScratchBuffer) != 1) {
			return 0;
		}
		if (File_Scanf(stream, " %256s", g_optModelLoadScratchBuffer) != 1) {
			return 0;
		}

		compareResult = _strnicmp(g_optModelLoadScratchBuffer, "ascii", sizeof("ascii") - 1);
		if (compareResult == 0) {
			importedHandle = OptModel_LoadInventorAsciiToHandle(stream);
		} else {
			compareResult = _strnicmp(g_optModelLoadScratchBuffer, "binary", sizeof("binary") - 1);
			if (compareResult == 0) {
				importedHandle = OptModel_LoadInventorBinaryToHandle(stream);
			} else {
				File_RawClose(stream);
				return 0;
			}
		}

		File_RawClose(stream);
		packedHandle = OptModel_ConvertImportedHandleToPacked(importedHandle);
		strcpy(g_optModelLoadScratchBuffer, modelFilename);
		OptModel_SaveHandleToFile(g_optModelLoadScratchBuffer, packedHandle);
		return OptModel_CreateRuntimeHandle(packedHandle);
	}

	File_RawClose((XvtFile*)g_stream);
	fileHandle = OptModel_LoadFileToHandle(g_optModelLoadScratchBuffer);
	FlightLoading_PulseAndDrawProgressScreen();
	runtimeHandle = OptModel_CreateRuntimeHandle(fileHandle);
	FlightLoading_PulseAndDrawProgressScreen();
	return runtimeHandle;
#endif
}

#ifndef XVT_MODERN
// FUNCTION: XVT 0x412030
uint16_t OptModel_LoadInventorBinaryToHandle(XvtFile* stream) {
	(void)stream;

	return 0;
}

// FUNCTION: XVT 0x412040
uint16_t OptModel_LoadInventorAsciiToHandle(XvtFile* stream) {

	int rootNodeCount;
	int rootPointerOffset;
	int nodePayloadSize;
	long streamStartOffset;
	int parsedNodeSize;
	int parsedRootCount;
	uint16_t handle;
	OptimizedPolyObject* model;
	char* nodeWriteCursor;

	streamStartOffset = File_RawTell(stream);
	rootNodeCount = 0;
	nodePayloadSize = 0;
	while (1) {
		parsedNodeSize = OptModel_ParseInventorAsciiNode(stream, NULL, NULL);
		if (parsedNodeSize == -1) {
			break;
		}
		if (parsedNodeSize != 0) {
			nodePayloadSize += parsedNodeSize;
			++rootNodeCount;
		}
	}

	rootPointerOffset = 0;
	File_RawSeek(stream, streamStartOffset, SEEK_SET);
	handle = Memory_AllocHandle(sizeof(*model) + rootNodeCount * sizeof(OptNode*) + nodePayloadSize, 0);
	model = (OptimizedPolyObject*)Memory_LockHandle(handle);
	nodeWriteCursor = (char*)model + sizeof(*model);
	model->rootNodeCount = rootNodeCount;
	parsedRootCount = 0;
	model->selfMarker = model;
	model->reserved = handle;
	model->rootNodes = (OptNode**)nodeWriteCursor;
	nodeWriteCursor += rootNodeCount * sizeof(OptNode*);

	for (; parsedRootCount < rootNodeCount;) {
		parsedNodeSize = OptModel_ParseInventorAsciiNode(
			stream, nodeWriteCursor, (OptNode**)((char*)model->rootNodes + rootPointerOffset));
		if (parsedNodeSize != 0) {
			rootPointerOffset += sizeof(OptNode*);
			nodeWriteCursor += parsedNodeSize;
			++parsedRootCount;
		}
	}

	Memory_UnlockHandle(handle);
	return handle;
}

// FUNCTION: XVT 0x412120
int OptModel_ParseInventorAsciiNode(XvtFile* stream, char* nodeStorage, OptNode** outNode) {
	char* cursor;
	char* nodeName;
	OptNode* node;
	OptLegacyParamRecord* fieldRecords;
	const InventorNodeDef* const* nodeDefSlot;
	int nodeType;
	int totalSize;
	int fieldScanIndex;
	int fieldIndex;
	int itemIndex;
	int itemCount;
	int childCount;
	int parsedSize;
	int integerValue;
	int compareResult;
	long rewindPosition;
	float floatValue;
	float component0;
	float component1;
	float component2;
	char character;

	cursor = nodeStorage;
	if (cursor != NULL) {
		*outNode = (OptNode*)cursor;
	}
	totalSize = 0;
	if (File_Scanf(stream, " %256s", g_optModelLoadScratchBuffer) != 1) {
		return -1;
	}
	compareResult = _strnicmp(g_optModelLoadScratchBuffer, "#", 1);
	if (compareResult == 0) {
		InventorAscii_SkipToEndOfLine(stream);
	}

	for (nodeType = 0; nodeType < (int)(sizeof(g_inventorNodeDefs) / sizeof(g_inventorNodeDefs[0]));
		 ++nodeType) {
		nodeDefSlot = &g_inventorNodeDefs[nodeType];
		compareResult = _strnicmp(g_optModelLoadScratchBuffer, (*nodeDefSlot)->nodeName,
								  strlen(g_optModelLoadScratchBuffer));
		if (compareResult == 0) {
			break;
		}
	}

	nodeName = NULL;
	if (nodeType == OPT_TYPE_8) {
		if (File_Scanf(stream, " %256s", g_optModelLoadScratchBuffer) != 1) {
			printf("READ NODE ERROR!\n");
			return 0;
		}
		parsedSize = strlen(g_optModelLoadScratchBuffer) + 1;
		totalSize = parsedSize;
		if (cursor != NULL) {
			strcpy(cursor, g_optModelLoadScratchBuffer);
			nodeName = cursor;
			cursor += parsedSize;
			*outNode = (OptNode*)cursor;
		}
		if (File_Scanf(stream, " %256s", g_optModelLoadScratchBuffer) != 1) {
			printf("READ NODE ERROR!\n");
			return parsedSize;
		}
		for (nodeType = 0; nodeType < (int)(sizeof(g_inventorNodeDefs) / sizeof(g_inventorNodeDefs[0]));
			 ++nodeType) {
			nodeDefSlot = &g_inventorNodeDefs[nodeType];
			compareResult = _strnicmp(g_optModelLoadScratchBuffer, (*nodeDefSlot)->nodeName,
									  strlen(g_optModelLoadScratchBuffer));
			if (compareResult == 0) {
				break;
			}
		}
	}

	if (nodeType < (int)(sizeof(g_inventorNodeDefs) / sizeof(g_inventorNodeDefs[0]))) {
		fieldRecords = NULL;
		if (cursor != NULL) {
			node = (OptNode*)cursor;
			node->pName = nodeName;
			node->nodeType = (OptNodeType)nodeType;
			cursor += sizeof(*node);
			node->param1 = (*nodeDefSlot)->fieldCount;
		}
		totalSize += sizeof(OptNode);
		if (cursor != NULL) {
			fieldRecords = (OptLegacyParamRecord*)cursor;
			node->param2 = fieldRecords;
			cursor += (*nodeDefSlot)->fieldCount * sizeof(*fieldRecords);
		}
		totalSize += (*nodeDefSlot)->fieldCount * sizeof(OptLegacyParamRecord);

		if (nodeType != OPT_NODEREF) {
			InventorAscii_SkipToOpenBrace(stream);
			for (fieldIndex = 0; fieldIndex < (*nodeDefSlot)->fieldCount; ++fieldIndex) {
				g_inventorFieldSeen[fieldIndex] = 0;
			}

			for (fieldScanIndex = 0; fieldScanIndex < (*nodeDefSlot)->fieldCount; ++fieldScanIndex) {
				OptLegacyParamRecord* fieldRecord;

				if (InventorAscii_PeekNextIsCloseBrace(stream) != 0) {
					break;
				}
				rewindPosition = File_RawTell(stream);
				if (File_Scanf(stream, " %256s", g_optModelLoadScratchBuffer) != 1) {
					printf("READ NODE ERROR!\n");
					return totalSize;
				}
				for (fieldIndex = 0; fieldIndex < (*nodeDefSlot)->fieldCount; ++fieldIndex) {
					compareResult = _strnicmp(g_optModelLoadScratchBuffer,
											  (*nodeDefSlot)->fieldDefs[fieldIndex]->fieldName,
											  strlen(g_optModelLoadScratchBuffer));
					if (compareResult == 0) {
						break;
					}
				}
				if (fieldIndex == (*nodeDefSlot)->fieldCount) {
					File_RawSeek(stream, rewindPosition, SEEK_SET);
					break;
				}

				g_inventorFieldSeen[fieldIndex] = 1;
				fieldRecord = fieldRecords != NULL ? &fieldRecords[fieldIndex] : NULL;
				switch ((*nodeDefSlot)->fieldDefs[fieldIndex]->fieldType) {
					case INVENTOR_FIELD_STRING:
						if (InventorAscii_PeekNextIsQuote(stream) != 0) {
							InventorAscii_SkipToQuote(stream);
							if (cursor != NULL) {
								fieldRecord->data = cursor;
								fieldRecord->value1 = 1;
								fieldRecord->value0 = INVENTOR_FIELD_STRING;
							}
							while (1) {
								character = (char)File_Getc(stream);
								if (character == (char)EOF) {
									printf("READ NODE ERROR!\n");
									return totalSize;
								}
								++totalSize;
								if (character == '"') {
									break;
								}
								if (cursor != NULL) {
									*cursor++ = character;
								}
							}
							if (cursor != NULL) {
								*cursor++ = '\0';
							}
						} else if (cursor != NULL) {
							fieldRecord->data = cursor;
							fieldRecord->value1 = 1;
							fieldRecord->value0 = INVENTOR_FIELD_STRING;
							if (File_Scanf(stream, " %s", cursor) != 1) {
								printf("READ NODE ERROR!\n");
								return totalSize;
							}
							parsedSize = strlen(cursor) + 1;
							cursor += parsedSize;
							totalSize += parsedSize;
						} else {
							if (File_Scanf(stream, " %256s", g_optModelLoadScratchBuffer) != 1) {
								printf("READ NODE ERROR!\n");
								return totalSize;
							}
							totalSize += strlen(g_optModelLoadScratchBuffer) + 1;
						}
						break;

					case INVENTOR_FIELD_NODE:
						if (cursor != NULL) {
							parsedSize = OptModel_ParseInventorAsciiNode(stream, cursor,
																		 (OptNode**)&fieldRecord->data);
							cursor += parsedSize;
							fieldRecord->value1 = 1;
							fieldRecord->value0 = INVENTOR_FIELD_NODE;
							totalSize += parsedSize;
						} else {
							totalSize += OptModel_ParseInventorAsciiNode(stream, NULL, NULL);
						}
						break;

					case INVENTOR_FIELD_NODE_LIST:
						if (InventorAscii_PeekNextIsOpenBracket(stream) != 0) {
							InventorAscii_SkipToOpenBracket(stream);
							rewindPosition = File_RawTell(stream);
							itemCount = 0;
							while (InventorAscii_PeekNextIsCloseBracket(stream) == 0) {
								parsedSize = OptModel_ParseInventorAsciiNode(stream, NULL, NULL);
								if (parsedSize != 0) {
									++itemCount;
									InventorAscii_SkipListSeparator(stream);
								}
							}
							File_RawSeek(stream, rewindPosition, SEEK_SET);
							totalSize += itemCount * sizeof(OptNode*);
							if (cursor != NULL) {
								fieldRecord->data = cursor;
								cursor += itemCount * sizeof(OptNode*);
								fieldRecord->value1 = itemCount;
								fieldRecord->value0 = INVENTOR_FIELD_NODE_LIST;
							}
							itemIndex = 0;
							while (InventorAscii_PeekNextIsCloseBracket(stream) == 0) {
								if (cursor != NULL) {
									parsedSize = OptModel_ParseInventorAsciiNode(
										stream, cursor, &((OptNode**)fieldRecord->data)[itemIndex]);
									if (parsedSize != 0) {
										cursor += parsedSize;
										++itemIndex;
										totalSize += parsedSize;
										InventorAscii_SkipListSeparator(stream);
									}
								} else {
									parsedSize = OptModel_ParseInventorAsciiNode(stream, NULL, NULL);
									if (parsedSize != 0) {
										totalSize += parsedSize;
										InventorAscii_SkipListSeparator(stream);
									}
								}
							}
							InventorAscii_SkipToCloseBracket(stream);
						} else {
							totalSize += sizeof(OptNode*);
							if (cursor != NULL) {
								fieldRecord->data = cursor;
								cursor += sizeof(OptNode*);
								fieldRecord->value1 = 1;
								fieldRecord->value0 = INVENTOR_FIELD_NODE_LIST;
								parsedSize = OptModel_ParseInventorAsciiNode(
									stream, cursor, &((OptNode**)fieldRecord->data)[0]);
								cursor += parsedSize;
								totalSize += parsedSize;
							} else {
								totalSize += OptModel_ParseInventorAsciiNode(stream, NULL, NULL);
							}
						}
						break;

					case INVENTOR_FIELD_INTEGER:
						parsedSize = File_Scanf(stream, " %li", &integerValue);
						if (parsedSize != 1) {
							printf("READ NODE ERROR!\n");
							return totalSize;
						}
						if (cursor != NULL) {
							fieldRecord->data = cursor;
							*(int*)cursor = integerValue;
							cursor += sizeof(integerValue);
							fieldRecord->value1 = 1;
							fieldRecord->value0 = INVENTOR_FIELD_INTEGER;
						}
						totalSize += sizeof(integerValue);
						break;

					case INVENTOR_FIELD_INTEGER_LIST:
						if (InventorAscii_PeekNextIsOpenBracket(stream) != 0) {
							InventorAscii_SkipToOpenBracket(stream);
							if (cursor != NULL) {
								fieldRecord->data = cursor;
							}
							itemCount = 0;
							while (InventorAscii_PeekNextIsCloseBracket(stream) == 0) {
								parsedSize = File_Scanf(stream, " %li", &integerValue);
								if (parsedSize != 1) {
									printf("READ NODE ERROR!\n");
									return totalSize;
								}
								if (cursor != NULL) {
									*(int*)cursor = integerValue;
									cursor += sizeof(integerValue);
								}
								++itemCount;
								totalSize += sizeof(integerValue);
								InventorAscii_SkipListSeparator(stream);
							}
							if (cursor != NULL) {
								fieldRecord->value1 = itemCount;
								fieldRecord->value0 = INVENTOR_FIELD_INTEGER_LIST;
							}
							InventorAscii_SkipToCloseBracket(stream);
						} else {
							if (cursor != NULL) {
								fieldRecord->data = cursor;
							}
							parsedSize = File_Scanf(stream, " %li", &integerValue);
							if (parsedSize != 1) {
								printf("READ NODE ERROR!\n");
								return totalSize;
							}
							if (cursor != NULL) {
								*(int*)cursor = integerValue;
								cursor += sizeof(integerValue);
							}
							itemCount = 1;
							totalSize += sizeof(integerValue);
							if (cursor != NULL) {
								fieldRecord->value1 = itemCount;
								fieldRecord->value0 = INVENTOR_FIELD_INTEGER_LIST;
							}
						}
						break;

					case INVENTOR_FIELD_FLOAT_AS_INTEGER:
						if (File_Scanf(stream, " %e", &floatValue) != 1) {
							printf("READ NODE WARNING!\n");
						}
						if (cursor != NULL) {
							fieldRecord->data = cursor;
							*(float*)cursor = floatValue;
							cursor += sizeof(floatValue);
							fieldRecord->value1 = 0;
							fieldRecord->value0 = INVENTOR_FIELD_INTEGER;
						}
						totalSize += sizeof(floatValue);
						break;

					case INVENTOR_FIELD_FLOAT:
						if (InventorAscii_PeekNextIsOpenBracket(stream) != 0) {
							InventorAscii_SkipToOpenBracket(stream);
							if (cursor != NULL) {
								fieldRecord->data = cursor;
							}
							itemCount = 0;
							while (InventorAscii_PeekNextIsCloseBracket(stream) == 0) {
								if (File_Scanf(stream, " %e", &floatValue) != 1) {
									printf("READ NODE WARNING!\n");
								}
								if (cursor != NULL) {
									*(float*)cursor = floatValue;
									cursor += sizeof(floatValue);
								}
								++itemCount;
								totalSize += sizeof(floatValue);
								InventorAscii_SkipListSeparator(stream);
							}
							if (cursor != NULL) {
								fieldRecord->value1 = itemCount;
								fieldRecord->value0 = INVENTOR_FIELD_FLOAT;
							}
							InventorAscii_SkipToCloseBracket(stream);
						} else {
							if (cursor != NULL) {
								fieldRecord->data = cursor;
							}
							if (File_Scanf(stream, " %e", &floatValue) != 1) {
								printf("READ NODE WARNING!\n");
							}
							if (cursor != NULL) {
								*(float*)cursor = floatValue;
								cursor += sizeof(floatValue);
							}
							itemCount = 1;
							totalSize += sizeof(floatValue);
							if (cursor != NULL) {
								fieldRecord->value1 = itemCount;
								fieldRecord->value0 = INVENTOR_FIELD_FLOAT;
							}
						}
						break;

					case INVENTOR_FIELD_BOOLEAN:
						if (File_Scanf(stream, " %256s", g_optModelLoadScratchBuffer) != 1) {
							printf("READ NODE ERROR!\n");
							return totalSize;
						}
						if (_strcmpi(g_optModelLoadScratchBuffer, "true") == 0 ||
							_strcmpi(g_optModelLoadScratchBuffer, "1") == 0) {
							character = 1;
						} else if (_strcmpi(g_optModelLoadScratchBuffer, "false") == 0 ||
								   _strcmpi(g_optModelLoadScratchBuffer, "0") == 0) {
							character = 0;
						} else {
							printf("READ NODE ERROR!\n");
							return totalSize;
						}
						++totalSize;
						if (cursor != NULL) {
							fieldRecord->data = cursor;
							*cursor++ = character;
							fieldRecord->value1 = 1;
							fieldRecord->value0 = INVENTOR_FIELD_BOOLEAN;
						}
						break;

					case INVENTOR_FIELD_VECTOR3:
					case INVENTOR_FIELD_COLOR:
						if (File_Scanf(stream, " %e %e %e", &component0, &component1, &component2) != 3) {
							printf("READ NODE WARNING!\n");
						}
						if (cursor != NULL) {
							fieldRecord->data = cursor;
							fieldRecord->value1 = 1;
							fieldRecord->value0 = (*nodeDefSlot)->fieldDefs[fieldIndex]->fieldType;
							((float*)cursor)[0] = component0;
							((float*)cursor)[1] = component1;
							((float*)cursor)[2] = component2;
							cursor += 3 * sizeof(float);
						}
						totalSize += 3 * sizeof(float);
						break;

					case INVENTOR_FIELD_VECTOR3_LIST:
					case INVENTOR_FIELD_COLOR_LIST:
						if (InventorAscii_PeekNextIsOpenBracket(stream) != 0) {
							InventorAscii_SkipToOpenBracket(stream);
							if (cursor != NULL) {
								fieldRecord->data = cursor;
							}
							itemCount = 0;
							while (InventorAscii_PeekNextIsCloseBracket(stream) == 0) {
								if (File_Scanf(stream, " %e %e %e", &component0, &component1, &component2) !=
									3) {
									printf("READ NODE WARNING!\n");
								}
								if (cursor != NULL) {
									((float*)cursor)[0] = component0;
									((float*)cursor)[1] = component1;
									((float*)cursor)[2] = component2;
									cursor += 3 * sizeof(float);
								}
								++itemCount;
								totalSize += 3 * sizeof(float);
								InventorAscii_SkipListSeparator(stream);
							}
							if (cursor != NULL) {
								fieldRecord->value1 = itemCount;
								fieldRecord->value0 = (*nodeDefSlot)->fieldDefs[fieldIndex]->fieldType;
							}
							InventorAscii_SkipToCloseBracket(stream);
						} else {
							if (cursor != NULL) {
								fieldRecord->data = cursor;
							}
							if (File_Scanf(stream, " %e %e %e", &component0, &component1, &component2) != 3) {
								printf("READ NODE WARNING!\n");
							}
							if (cursor != NULL) {
								((float*)cursor)[0] = component0;
								((float*)cursor)[1] = component1;
								((float*)cursor)[2] = component2;
								cursor += 3 * sizeof(float);
							}
							itemCount = 1;
							totalSize += 3 * sizeof(float);
							if (cursor != NULL) {
								fieldRecord->value1 = itemCount;
								fieldRecord->value0 = (*nodeDefSlot)->fieldDefs[fieldIndex]->fieldType;
							}
						}
						break;

					case INVENTOR_FIELD_MATRIX:
						if (cursor != NULL) {
							fieldRecord->data = cursor;
							fieldRecord->value1 = 1;
							fieldRecord->value0 = (*nodeDefSlot)->fieldDefs[fieldIndex]->fieldType;
						}
						totalSize += 16 * sizeof(float);
						for (itemCount = 16; itemCount > 0; --itemCount) {
							if (File_Scanf(stream, " %e", &floatValue) != 1) {
								printf("READ NODE WARNING!\n");
							}
							if (cursor != NULL) {
								*(float*)cursor = floatValue;
								cursor += sizeof(floatValue);
							}
						}
						break;

					case INVENTOR_FIELD_MATRIX_LIST:
						if (InventorAscii_PeekNextIsOpenBracket(stream) != 0) {
							InventorAscii_SkipToOpenBracket(stream);
							if (cursor != NULL) {
								fieldRecord->data = cursor;
							}
							itemCount = 0;
							while (InventorAscii_PeekNextIsCloseBracket(stream) == 0) {
								for (integerValue = 0; integerValue < 16; ++integerValue) {
									if (File_Scanf(stream, " %e", &floatValue) != 1) {
										printf("READ NODE WARNING!\n");
									}
									if (cursor != NULL) {
										*(float*)cursor = floatValue;
										cursor += sizeof(floatValue);
									}
									totalSize += sizeof(floatValue);
								}
								++itemCount;
								InventorAscii_SkipListSeparator(stream);
							}
							if (cursor != NULL) {
								fieldRecord->value1 = itemCount;
								fieldRecord->value0 = (*nodeDefSlot)->fieldDefs[fieldIndex]->fieldType;
							}
							InventorAscii_SkipToCloseBracket(stream);
						} else {
							if (cursor != NULL) {
								fieldRecord->data = cursor;
							}
							for (integerValue = 0; integerValue < 16; ++integerValue) {
								if (File_Scanf(stream, " %e", &floatValue) != 1) {
									printf("READ NODE WARNING!\n");
								}
								if (cursor != NULL) {
									*(float*)cursor = floatValue;
									cursor += sizeof(floatValue);
								}
								totalSize += sizeof(floatValue);
							}
							itemCount = 1;
							if (cursor != NULL) {
								fieldRecord->value1 = itemCount;
								fieldRecord->value0 = (*nodeDefSlot)->fieldDefs[fieldIndex]->fieldType;
							}
						}
						break;

					case INVENTOR_FIELD_ROTATION:
						if (cursor != NULL) {
							fieldRecord->data = cursor;
							fieldRecord->value1 = 1;
							fieldRecord->value0 = (*nodeDefSlot)->fieldDefs[fieldIndex]->fieldType;
						}
						totalSize += 4 * sizeof(float);
						for (itemCount = 4; itemCount > 0; --itemCount) {
							if (File_Scanf(stream, " %e", &floatValue) != 1) {
								printf("READ NODE WARNING!\n");
							}
							if (cursor != NULL) {
								*(float*)cursor = floatValue;
								cursor += sizeof(floatValue);
							}
						}
						break;

					case INVENTOR_FIELD_ROTATION_LIST:
						if (InventorAscii_PeekNextIsOpenBracket(stream) != 0) {
							InventorAscii_SkipToOpenBracket(stream);
							if (cursor != NULL) {
								fieldRecord->data = cursor;
							}
							itemCount = 0;
							while (InventorAscii_PeekNextIsCloseBracket(stream) == 0) {
								for (integerValue = 0; integerValue < 4; ++integerValue) {
									if (File_Scanf(stream, " %e", &floatValue) != 1) {
										printf("READ NODE WARNING!\n");
									}
									if (cursor != NULL) {
										*(float*)cursor = floatValue;
										cursor += sizeof(floatValue);
									}
									totalSize += sizeof(floatValue);
								}
								++itemCount;
								InventorAscii_SkipListSeparator(stream);
							}
							if (cursor != NULL) {
								fieldRecord->value1 = itemCount;
								fieldRecord->value0 = (*nodeDefSlot)->fieldDefs[fieldIndex]->fieldType;
							}
							InventorAscii_SkipToCloseBracket(stream);
						} else {
							if (cursor != NULL) {
								fieldRecord->data = cursor;
							}
							for (integerValue = 0; integerValue < 4; ++integerValue) {
								if (File_Scanf(stream, " %e", &floatValue) != 1) {
									printf("READ NODE WARNING!\n");
								}
								if (cursor != NULL) {
									*(float*)cursor = floatValue;
									cursor += sizeof(floatValue);
								}
								totalSize += sizeof(floatValue);
							}
							itemCount = 1;
							if (cursor != NULL) {
								fieldRecord->value1 = itemCount;
								fieldRecord->value0 = (*nodeDefSlot)->fieldDefs[fieldIndex]->fieldType;
							}
						}
						break;

					case INVENTOR_FIELD_VECTOR2:
						if (File_Scanf(stream, " %e %e", &component0, &component1) != 2) {
							printf("READ NODE WARNING!\n");
						}
						if (cursor != NULL) {
							fieldRecord->data = cursor;
							fieldRecord->value1 = 1;
							fieldRecord->value0 = (*nodeDefSlot)->fieldDefs[fieldIndex]->fieldType;
							((float*)cursor)[0] = component0;
							((float*)cursor)[1] = component1;
							cursor += 2 * sizeof(float);
						}
						totalSize += 2 * sizeof(float);
						break;

					case INVENTOR_FIELD_VECTOR2_LIST:
						if (InventorAscii_PeekNextIsOpenBracket(stream) != 0) {
							InventorAscii_SkipToOpenBracket(stream);
							if (cursor != NULL) {
								fieldRecord->data = cursor;
							}
							itemCount = 0;
							while (InventorAscii_PeekNextIsCloseBracket(stream) == 0) {
								if (File_Scanf(stream, " %e %e", &component0, &component1) != 2) {
									printf("READ NODE WARNING!\n");
								}
								if (cursor != NULL) {
									((float*)cursor)[0] = component0;
									((float*)cursor)[1] = component1;
									cursor += 2 * sizeof(float);
								}
								++itemCount;
								totalSize += 2 * sizeof(float);
								InventorAscii_SkipListSeparator(stream);
							}
							if (cursor != NULL) {
								fieldRecord->value1 = itemCount;
								fieldRecord->value0 = (*nodeDefSlot)->fieldDefs[fieldIndex]->fieldType;
							}
							InventorAscii_SkipToCloseBracket(stream);
						} else {
							if (cursor != NULL) {
								fieldRecord->data = cursor;
							}
							if (File_Scanf(stream, " %e %e", &component0, &component1) != 2) {
								printf("READ NODE WARNING!\n");
							}
							if (cursor != NULL) {
								((float*)cursor)[0] = component0;
								((float*)cursor)[1] = component1;
								cursor += 2 * sizeof(float);
							}
							itemCount = 1;
							totalSize += 2 * sizeof(float);
							if (cursor != NULL) {
								fieldRecord->value1 = itemCount;
								fieldRecord->value0 = (*nodeDefSlot)->fieldDefs[fieldIndex]->fieldType;
							}
						}
						break;

					case INVENTOR_FIELD_ENUM: {
						const InventorEnumDef* enumDef;
						const char* const* enumValueNames;
						const int* enumValues;

						if (InventorAscii_PeekNextIsCloseBrace(stream) != 0) {
							strcpy(g_optModelLoadScratchBuffer, "DEFAULT");
						} else if (File_Scanf(stream, " %256s", g_optModelLoadScratchBuffer) != 1) {
							printf("READ NODE ERROR!\n");
							return totalSize;
						}
						enumDef = (*nodeDefSlot)->fieldDefs[fieldIndex]->enumDef;
						enumValueNames = enumDef->valueNames;
						enumValues = enumDef->values;
						for (integerValue = 0; integerValue < enumDef->valueCount; ++integerValue) {
							compareResult =
								_strcmpi(g_optModelLoadScratchBuffer, enumValueNames[integerValue]);
							if (compareResult == 0) {
								break;
							}
						}
						if (integerValue == enumDef->valueCount) {
							integerValue = atoi(g_optModelLoadScratchBuffer);
						} else {
							integerValue = enumValues[integerValue];
						}
						if (cursor != NULL) {
							fieldRecord->data = cursor;
							*(int*)cursor = integerValue;
							cursor += sizeof(integerValue);
							fieldRecord->value1 = 1;
							fieldRecord->value0 = INVENTOR_FIELD_ENUM;
						}
						totalSize += sizeof(integerValue);
						break;
					}

					case INVENTOR_FIELD_ENUM_LIST: {
						const InventorEnumDef* enumDef;
						const char* const* enumValueNames;
						const int* enumValues;

						if (InventorAscii_PeekNextIsOpenBracket(stream) != 0) {
							InventorAscii_SkipToOpenBracket(stream);
							if (cursor != NULL) {
								fieldRecord->data = cursor;
							}
							itemCount = 0;
							while (InventorAscii_PeekNextIsCloseBracket(stream) == 0) {
								if (File_Scanf(stream, " %256s", g_optModelLoadScratchBuffer) != 1) {
									printf("READ NODE ERROR!\n");
									return totalSize;
								}
								enumDef = (*nodeDefSlot)->fieldDefs[fieldIndex]->enumDef;
								enumValueNames = enumDef->valueNames;
								enumValues = enumDef->values;
								for (integerValue = 0; integerValue < enumDef->valueCount; ++integerValue) {
									compareResult =
										_strcmpi(g_optModelLoadScratchBuffer, enumValueNames[integerValue]);
									if (compareResult == 0) {
										break;
									}
								}
								if (integerValue == enumDef->valueCount) {
									integerValue = atoi(g_optModelLoadScratchBuffer);
								} else {
									integerValue = enumValues[integerValue];
								}
								if (cursor != NULL) {
									*(int*)cursor = integerValue;
									cursor += sizeof(integerValue);
								}
								totalSize += sizeof(integerValue);
								++itemCount;
								InventorAscii_SkipListSeparator(stream);
							}
							if (cursor != NULL) {
								fieldRecord->value1 = itemCount;
								fieldRecord->value0 = (*nodeDefSlot)->fieldDefs[fieldIndex]->fieldType;
							}
							InventorAscii_SkipToCloseBracket(stream);
						} else {
							if (cursor != NULL) {
								fieldRecord->data = cursor;
							}
							if (File_Scanf(stream, " %256s", g_optModelLoadScratchBuffer) != 1) {
								printf("READ NODE ERROR!\n");
								return totalSize;
							}
							enumDef = (*nodeDefSlot)->fieldDefs[fieldIndex]->enumDef;
							enumValueNames = enumDef->valueNames;
							enumValues = enumDef->values;
							for (integerValue = 0; integerValue < enumDef->valueCount; ++integerValue) {
								compareResult =
									_strcmpi(g_optModelLoadScratchBuffer, enumValueNames[integerValue]);
								if (compareResult == 0) {
									break;
								}
							}
							if (integerValue == enumDef->valueCount) {
								integerValue = atoi(g_optModelLoadScratchBuffer);
							} else {
								integerValue = enumValues[integerValue];
							}
							if (cursor != NULL) {
								*(int*)cursor = integerValue;
								cursor += sizeof(integerValue);
							}
							itemCount = 1;
							totalSize += sizeof(integerValue);
							if (cursor != NULL) {
								fieldRecord->value1 = itemCount;
								fieldRecord->value0 = (*nodeDefSlot)->fieldDefs[fieldIndex]->fieldType;
							}
						}
						break;
					}

					default:
						break;
				}
			}

			for (fieldIndex = 0; fieldIndex < (*nodeDefSlot)->fieldCount; ++fieldIndex) {
				if (g_inventorFieldSeen[fieldIndex] == 0) {
					if (cursor != NULL) {
						const InventorFieldDef* fieldDef;

						fieldDef = (*nodeDefSlot)->fieldDefs[fieldIndex];
						fieldRecords[fieldIndex] = *fieldDef->defaultRecord;
						fieldRecords[fieldIndex].data = cursor;
						memcpy(cursor, fieldDef->defaultData, fieldDef->defaultDataSize);
						cursor += fieldDef->defaultDataSize;
					}
					totalSize += (*nodeDefSlot)->fieldDefs[fieldIndex]->defaultDataSize;
				}
			}

			childCount = 0;
			rewindPosition = File_RawTell(stream);
			while (InventorAscii_PeekNextIsCloseBrace(stream) == 0) {
				if (OptModel_ParseInventorAsciiNode(stream, NULL, NULL) != 0) {
					++childCount;
				}
			}
			File_RawSeek(stream, rewindPosition, SEEK_SET);
			if (childCount != 0) {
				OptNode** childSlots;

				totalSize += childCount * sizeof(OptNode*);
				childSlots = NULL;
				if (cursor != NULL) {
					node->pChildren = (OptNode**)cursor;
					node->childCount = childCount;
					childSlots = node->pChildren;
					cursor += childCount * sizeof(OptNode*);
				}
				while (InventorAscii_PeekNextIsCloseBrace(stream) == 0) {
					if (cursor != NULL) {
						parsedSize = OptModel_ParseInventorAsciiNode(stream, cursor, childSlots);
						if (parsedSize != 0) {
							cursor += parsedSize;
							++childSlots;
							totalSize += parsedSize;
						}
					} else {
						parsedSize = OptModel_ParseInventorAsciiNode(stream, NULL, NULL);
						if (parsedSize != 0) {
							totalSize += parsedSize;
						}
					}
				}
			} else if (cursor != NULL) {
				node->pChildren = NULL;
				node->childCount = 0;
			}
			InventorAscii_SkipToCloseBrace(stream);
			return totalSize;
		} else {
			if (InventorAscii_PeekNextIsQuote(stream) != 0) {
				InventorAscii_SkipToQuote(stream);
				if (cursor != NULL) {
					fieldRecords[0].data = cursor;
					fieldRecords[0].value1 = 1;
					fieldRecords[0].value0 = INVENTOR_FIELD_STRING;
				}
				while (1) {
					character = (char)File_Getc(stream);
					if (character == (char)EOF) {
						break;
					}
					++totalSize;
					if (character == '"') {
						if (cursor != NULL) {
							*cursor++ = '\0';
							node->pChildren = NULL;
							node->childCount = 0;
						}
						return totalSize;
					}
					if (cursor != NULL) {
						*cursor++ = character;
					}
				}
				printf("READ NODE ERROR!\n");
				return totalSize;
			} else {
				if (File_Scanf(stream, " %256s", g_optModelLoadScratchBuffer) != 1) {
					printf("READ NODE ERROR!\n");
					return totalSize;
				}
				parsedSize = strlen(g_optModelLoadScratchBuffer) + 1;
				if (cursor != NULL) {
					strcpy(cursor, g_optModelLoadScratchBuffer);
					fieldRecords[0].data = cursor;
					cursor += parsedSize;
					fieldRecords[0].value1 = 1;
					fieldRecords[0].value0 = INVENTOR_FIELD_STRING;
				}
				totalSize += parsedSize;
			}
			if (cursor != NULL) {
				node->pChildren = NULL;
				node->childCount = 0;
			}
			return totalSize;
		}
	}

	printf("Node Type Not Supported, ignored\n");
	if (InventorAscii_PeekNextIsOpenBrace(stream) != 0) {
		InventorAscii_SkipToOpenBrace(stream);
		InventorAscii_SkipToCloseBrace(stream);
	}
	return totalSize;
}
#endif

// FUNCTION: XVT 0x42ABA0
void OptModel_TranslateNodeVerticesRecursive(OptNode* node, OptimizedPolyObject* model,
											 const float* translation) {
	int vertexCount;
	float* vertex;
	const float* delta;
	int childIndex;

	if (node != NULL) {
		while (node->nodeType == OPT_NODEREF) {
			node = OptModel_ResolveNodeRef(model, (const char*)node->param2);
			if (node == NULL)
				return;
		}

		if (node->nodeType == OPT_MESHVERTS) {
			vertexCount = node->param1;
			vertex = node->param2;
			delta = translation;
			if (vertexCount > 0) {
				do {
					vertex[0] += delta[0];
					vertex[1] += delta[1];
					vertex[2] += delta[2];
					vertex += 3;
					--vertexCount;
				} while (vertexCount != 0);
			}
		} else {
			delta = translation;
		}

		childIndex = 0;
		if (node->childCount > 0) {
			do {
				OptModel_TranslateNodeVerticesRecursive(node->pChildren[childIndex], model, delta);
				++childIndex;
			} while (node->childCount > childIndex);
		}
	}
}

// FUNCTION: XVT 0x42ACD0
void OptModel_TranslateVertices(OptimizedPolyObject* model, const float* translation) {
	int rootIndex;

	rootIndex = 0;
	if (model->rootNodeCount > 0) {
		do {
			OptModel_TranslateNodeVerticesRecursive(model->rootNodes[rootIndex], model, translation);
			++rootIndex;
		} while (model->rootNodeCount > rootIndex);
	}
}

#ifndef XVT_MODERN
// FUNCTION: XVT 0x471F00
void OptModel_RelocateLoadedPointers(OptimizedPolyObject* model) {

	XvtOptValue relocationDelta;
	int rootIndex;

	relocationDelta = (uint8_t*)model - (uint8_t*)model->selfMarker;
	model->selfMarker = (uint8_t*)model->selfMarker + relocationDelta;
	if (model->rootNodes != NULL) {
		model->rootNodes = (OptNode**)((uint8_t*)model->rootNodes + relocationDelta);
		for (rootIndex = 0; rootIndex < model->rootNodeCount; ++rootIndex) {
			OptNode** rootSlot;

			rootSlot = &model->rootNodes[rootIndex];
			if (*rootSlot != NULL) {
				*rootSlot = (OptNode*)((uint8_t*)*rootSlot + relocationDelta);
				OptModel_RelocateNodePointersRecursive(model->rootNodes[rootIndex], relocationDelta);
			}
		}
	}
}

// FUNCTION: XVT 0x471F60
void OptModel_RelocateNodePointersRecursive(OptNode* node, XvtOptValue relocationDelta) {

	int paramIndex;
	int childIndex;

	if (node->pName != NULL)
		node->pName += relocationDelta;
	if (node->param2 != NULL) {
		OptLegacyParamRecord* records;

		node->param2 = (uint8_t*)node->param2 + relocationDelta;
		records = (OptLegacyParamRecord*)node->param2;
		for (paramIndex = 0; paramIndex < node->param1; ++paramIndex) {
			if (records->data != NULL)
				records->data = (uint8_t*)records->data + relocationDelta;
			++records;
		}
	}
	if (node->pChildren != NULL) {
		node->pChildren = (OptNode**)((uint8_t*)node->pChildren + relocationDelta);
		for (childIndex = 0; childIndex < node->childCount; ++childIndex) {
			OptNode** childSlot;

			childSlot = &node->pChildren[childIndex];
			if (*childSlot != NULL) {
				*childSlot = (OptNode*)((uint8_t*)*childSlot + relocationDelta);
				OptModel_RelocateNodePointersRecursive(node->pChildren[childIndex], relocationDelta);
			}
		}
	}
}
#endif

// FUNCTION: XVT 0x471FF0
void OptModel_AdjustOptimizedPolyObjectPointers(OptimizedPolyObject* model) {
#ifdef XVT_MODERN
	XvtOpt_Relocate(model);
#else

	XvtOptValue relocationDelta;
	int rootIndex;

	relocationDelta = (uint8_t*)model - (uint8_t*)model->selfMarker;
	model->selfMarker = (uint8_t*)model->selfMarker + relocationDelta;
	if (model->rootNodes != NULL) {
		model->rootNodes = (OptNode**)((uint8_t*)model->rootNodes + relocationDelta);
		for (rootIndex = 0; rootIndex < model->rootNodeCount; ++rootIndex) {
			OptNode** rootSlot;

			rootSlot = &model->rootNodes[rootIndex];
			if (*rootSlot != NULL) {
				*rootSlot = (OptNode*)((uint8_t*)*rootSlot + relocationDelta);
				OptModel_AdjustOptimizedNodePointers(model->rootNodes[rootIndex], relocationDelta);
			}
		}
	}

#endif
}

// FUNCTION: XVT 0x472050
void OptModel_AdjustOptimizedNodePointers(OptNode* node, XvtOptValue base) {
#ifdef XVT_MODERN
	XvtOpt_RelocateNode(node, base);
#else

	int childIndex;

	if (node->pName != NULL)
		node->pName += base;
	if (node->param2 != NULL)
		node->param2 = (uint8_t*)node->param2 + base;
	if (node->nodeType == OPT_TEXTURE) {
		OptTextureData* textureData;

		textureData = (OptTextureData*)node->param2;
		if (textureData->paletteType == 0)
			textureData->palette = (uint16_t*)((uint8_t*)textureData->palette + base);
	}
	if (node->pChildren != NULL) {
		node->pChildren = (OptNode**)((uint8_t*)node->pChildren + base);
		for (childIndex = 0; childIndex < node->childCount; ++childIndex) {
			OptNode** childSlot;

			childSlot = &node->pChildren[childIndex];
			if (*childSlot != NULL) {
				*childSlot = (OptNode*)((uint8_t*)*childSlot + base);
				OptModel_AdjustOptimizedNodePointers(node->pChildren[childIndex], base);
			}
		}
	}

#endif
}

// FUNCTION: XVT 0x4742A0
uint16_t OptModel_LoadFileToHandle(char* filename) {
#ifdef XVT_MODERN
	unsigned int nativeSize = 0;
	int version = 0;
	int rootIndex;
	OptimizedPolyObject* model;
	SceneMesh meshState;
	uint16_t handle = XvtOpt_Load(filename, &version, &nativeSize);
	if (!handle) {
		XvtStorage_Fatal("Invalid required OPT model", 1);
		return 0;
	}
	if (g_loadOptBufHandle)
		Memory_FreeHandle(g_loadOptBufHandle);
	g_loadOptBufHandle = handle;
	g_loadOptBufSize = (int)nativeSize;
	g_optSourceIsVersion0 = version == 0;
	if (version < 2) {
		nativeSize = OptModel_ConvertLegacyModelToOptimized(nativeSize);
		if (!nativeSize)
			return 0;
		handle = g_loadOptBufHandle;
	}
	model = Memory_LockHandle(handle);
	memset(&meshState, 0, sizeof(meshState));
	g_modelNodeWalkUnusedScratch0 = NULL;
	g_modelNodeWalkUnusedScratch1 = NULL;
	g_curVertNormals = NULL;
	g_modelNodeWalkUnusedScratch2 = NULL;
	g_curMeshFlags = NULL;
	g_curVertexCount = 0;
	for (rootIndex = 0; rootIndex < model->rootNodeCount; ++rootIndex)
		OptModel_GetSerializedNodeSize(model->rootNodes[rootIndex], &meshState);
	Memory_UnlockHandle(handle);
	return handle;
#else

	XvtFile* stream;
	uint16_t handle;
	OptimizedPolyObject* model;
	char savedVersionChar;
	int fileVersion;
	size_t serializedSize;
	SceneMesh meshState;
	int rootIndex;

	File_OpenGlobalStream(filename, g_fileModeReadBinary, 1, 0);
	stream = g_stream;
	if (stream == NULL)
		return 0;
	g_optSourceIsVersion0 = 0;
	File_RawRead(&fileVersion, 1, sizeof(fileVersion), stream);
	if (fileVersion > 0) {
		serializedSize = (size_t)fileVersion;
		fileVersion = 0;
		g_optSourceIsVersion0 = 1;
	} else {
		fileVersion = -fileVersion;
		if (fileVersion == 1)
			fileVersion = 0;
		g_optSourceIsVersion0 = 0;
		File_RawRead(&serializedSize, 1, sizeof(serializedSize), stream);
	}
	if ((int)serializedSize > g_loadOptBufSize && g_loadOptBufHandle != 0) {
		unsigned int oldHandle;

		oldHandle = g_loadOptBufHandle;
		Memory_FreeHandle((uint16_t)oldHandle);
		g_loadOptBufHandle = 0;
		g_loadOptBufSize = 0;
	}
	if (g_loadOptBufHandle == 0) {
		g_loadOptBufHandle = Memory_AllocHandle(serializedSize, 0);
		if (g_loadOptBufHandle == 0)
			FeDiskIo_FatalError(FILE_ERROR_STR_NOT_ENOUGH_MEMORY);
		g_loadOptBufSize = (int)serializedSize;
	}
	handle = g_loadOptBufHandle;
	model = Memory_LockHandle(handle);
	File_RawRead(model, 1, serializedSize, stream);
	File_RawClose(stream);
	if (model->selfMarker != model)
		OptModel_AdjustOptimizedPolyObjectPointers(model);

	if (fileVersion == 0) {
		savedVersionChar = filename[strlen(filename) - 1];
		if (g_optSourceIsVersion0)
			filename[strlen(filename) - 1] = '0';
		else
			filename[strlen(filename) - 1] = '1';

		File_OpenGlobalStream(filename, "wb", 0, 1);
		stream = g_stream;
		if (stream != NULL) {
			if (!g_optSourceIsVersion0) {
				fileVersion = -1;
				File_RawWrite(&fileVersion, 1, sizeof(fileVersion), stream);
			}
			File_RawWrite(&serializedSize, 1, sizeof(serializedSize), stream);
			File_RawWrite(model, 1, serializedSize, stream);
			File_RawClose(stream);
		}

		filename[strlen(filename) - 1] = savedVersionChar;
		Memory_UnlockHandle(handle);
		serializedSize = OptModel_ConvertLegacyModelToOptimized((unsigned int)serializedSize);
		handle = g_loadOptBufHandle;
		model = Memory_LockHandle(handle);
		if (model->selfMarker != model)
			OptModel_AdjustOptimizedPolyObjectPointers(model);

		fileVersion = -1;
		if (!g_optSourceIsVersion0)
			fileVersion = -2;
		if (_access(filename, 2) == 0) {
			File_OpenGlobalStream(filename, "wb", 0, 1);
			stream = g_stream;
			if (stream != NULL) {
				File_RawWrite(&fileVersion, 1, sizeof(fileVersion), stream);
				File_RawWrite(&serializedSize, 1, sizeof(serializedSize), stream);
				File_RawWrite(model, 1, serializedSize, stream);
				File_RawClose(stream);
			}
		}
		fileVersion = -fileVersion;
	}

	memset(&meshState, 0, sizeof(meshState));
	g_modelNodeWalkUnusedScratch0 = NULL;
	g_modelNodeWalkUnusedScratch1 = NULL;
	g_curVertNormals = NULL;
	g_modelNodeWalkUnusedScratch2 = NULL;
	g_curMeshFlags = NULL;
	g_curVertexCount = 0;
	serializedSize = sizeof(*model) + sizeof(*model->rootNodes) * model->rootNodeCount;
	for (rootIndex = 0; rootIndex < model->rootNodeCount; ++rootIndex)
		serializedSize += OptModel_GetSerializedNodeSize(model->rootNodes[rootIndex], &meshState);
	Memory_UnlockHandle(handle);
	return handle;

#endif
}

// FUNCTION: XVT 0x474610
unsigned int OptModel_ConvertLegacyModelToOptimized(unsigned int sourceSize) {
	/* Convert a legacy model stream into the optimized runtime representation. */
	OptimizedPolyObject* sourceModel;
	OptimizedPolyObject* destinationModel;
	SceneMesh meshState;
	uint8_t* destinationNode;
	unsigned int serializedSize;
	int destinationCapacity;
	int rootIndex;
	int rootNodeCount;

	if ((int)g_optConvertSourceBufSize < (int)sourceSize && g_optConvertSourceHandle != 0) {
		Memory_FreeHandle(g_optConvertSourceHandle);
		g_optConvertSourceHandle = 0;
		g_optConvertSourceBufSize = 0;
	}
	if (g_optConvertSourceHandle == 0) {
		g_optConvertSourceHandle = Memory_AllocHandle(sourceSize, 0);
		if (g_optConvertSourceHandle == 0)
			FeDiskIo_FatalError(FILE_ERROR_STR_NOT_ENOUGH_MEMORY);
		g_optConvertSourceBufSize = sourceSize;
	}
	sourceModel = (OptimizedPolyObject*)Memory_LockHandle(g_optConvertSourceHandle);
	memcpy(sourceModel, Memory_LockHandle(g_loadOptBufHandle), sourceSize);
	if (sourceModel->selfMarker != sourceModel)
		OptModel_AdjustOptimizedPolyObjectPointers(sourceModel);
	Memory_UnlockHandle(g_loadOptBufHandle);
	destinationCapacity = (int)(sourceSize * 2u);
	if (destinationCapacity > g_loadOptBufSize && g_loadOptBufHandle != 0) {
		Memory_FreeHandle(g_loadOptBufHandle);
		g_loadOptBufHandle = 0;
		g_loadOptBufSize = 0;
	}
	if (g_loadOptBufHandle == 0) {
		g_loadOptBufHandle = Memory_AllocHandle((unsigned int)destinationCapacity, 0);
		if (g_loadOptBufHandle == 0)
			FeDiskIo_FatalError(FILE_ERROR_STR_NOT_ENOUGH_MEMORY);
		g_loadOptBufSize = destinationCapacity;
	}
	destinationModel = (OptimizedPolyObject*)Memory_LockHandle(g_loadOptBufHandle);
	memcpy(destinationModel, sourceModel, sizeof(*destinationModel));
	destinationModel->selfMarker = destinationModel;
	destinationModel->rootNodes = (OptNode**)((uint8_t*)destinationModel + sizeof(*destinationModel));
	rootNodeCount = sourceModel->rootNodeCount;
	destinationNode = (uint8_t*)(destinationModel->rootNodes + rootNodeCount);
	serializedSize =
		(unsigned int)(sizeof(*destinationModel) + sizeof(OptNode*) * (unsigned int)rootNodeCount);
	memset(&meshState, 0, sizeof(meshState));
	g_modelNodeWalkUnusedScratch0 = NULL;
	g_modelNodeWalkUnusedScratch1 = NULL;
	g_curVertNormals = NULL;
	g_modelNodeWalkUnusedScratch2 = NULL;
	g_curMeshFlags = NULL;
	g_curVertexCount = 0;
	destinationModel->rootNodeCount = 0;
	rootIndex = 0;
	if (rootNodeCount > 0) {
		do {
			unsigned int nodeSize;
			++rootIndex;
			++destinationModel->rootNodeCount;
			destinationModel->rootNodes[rootIndex - 1] = (OptNode*)destinationNode;
			g_optConvertVertexNode = NULL;
			g_optConvertTexCoordNode = NULL;
			g_optConvertVertexNormalNode = NULL;
			nodeSize =
				OptModel_ConvertLegacyNodeToOptimized(destinationNode, sourceModel->rootNodes[rootIndex - 1],
													  sourceModel, destinationModel, &meshState);
			destinationNode += nodeSize;
			serializedSize += nodeSize;
		} while (rootNodeCount > rootIndex);
	}
	return serializedSize;
}

// FUNCTION: XVT 0x474830
void* OptModel_FindSharedTextureDataInNodeBeforeTarget(const void* textureData, OptNode* node,
													   const OptNode* stopNode) {
	int textureByteCount;
	OptTextureData* nodeTexture;
	uint8_t* nodeTextureData;
	int childIndex;
	int childOffset;
	OptNode* child;
	void* result;

	if (node == NULL)
		return NULL;

	if (node->nodeType == OPT_TEXTURE) {
		nodeTexture = node->param2;
		nodeTextureData = (uint8_t*)nodeTexture + sizeof(*nodeTexture);
		textureByteCount = nodeTexture->height;
		textureByteCount *= nodeTexture->width;
		if (nodeTexture->textureSize == textureByteCount)
			textureByteCount = nodeTexture->dataSize;
		nodeTextureData += textureByteCount;
		if (nodeTexture->paletteType == 0) {
			if (nodeTexture->palette == (uint16_t*)nodeTextureData) {
				textureData = (const uint8_t*)textureData + 4096;
				nodeTextureData += 4096;
				if (memcmp(textureData, nodeTextureData, 8192) == 0) {
					nodeTextureData -= 4096;
					return nodeTextureData;
				}
			}
		} else {
			textureData = (const uint8_t*)textureData + 4096;
			nodeTextureData += 4096;
			if (memcmp(textureData, nodeTextureData, 8192) == 0) {
				nodeTextureData -= 4096;
				return nodeTextureData;
			}
		}
	}

	childOffset = 0;
	childIndex = 0;
	if (node->childCount > 0) {
		do {
			child = *(OptNode**)((uint8_t*)node->pChildren + childOffset);
			if (stopNode == child)
				return NULL;
			result = OptModel_FindSharedTextureDataInNodeBeforeTarget(textureData, child, stopNode);
			if (result != NULL)
				return result;
			childOffset += sizeof(*node->pChildren);
			++childIndex;
		} while (node->childCount > childIndex);
	}

	return NULL;
}

// FUNCTION: XVT 0x474910
void* OptModel_FindEarlierSharedTextureData(const void* textureData, OptimizedPolyObject* model,
											const OptNode* stopNode) {
	OptimizedPolyObject* object;
	int rootIndex;
	unsigned int rootOffset;
	OptNode* rootNode;
	void* result;

	object = model;
	rootIndex = 0;
	rootOffset = 0;
	if (object->rootNodeCount > 0) {
		do {
			rootNode = *(OptNode**)((uint8_t*)object->rootNodes + rootOffset);
			if (stopNode == rootNode)
				return NULL;
			result = OptModel_FindSharedTextureDataInNodeBeforeTarget(textureData, rootNode, stopNode);
			if (result != NULL)
				return result;
			rootOffset += sizeof(*object->rootNodes);
			++rootIndex;
		} while (object->rootNodeCount > rootIndex);
	}

	return NULL;
}

// FUNCTION: XVT 0x474960
unsigned int OptModel_ConvertLegacyNodeToOptimized(uint8_t* dst, OptNode* srcNode,
												   OptimizedPolyObject* srcModel,
												   OptimizedPolyObject* dstModel, SceneMesh* meshState) {
	OptNode* destinationNode;
	uint8_t* cursor;
	OptNode** sourceNode;
	SceneMesh childMesh;
	OptVector minimum;
	OptVector maximum;
	unsigned int payloadSize;
	int emitNode;
	int firstChildIndex;
	int childIndex;

	if (srcNode == NULL)
		return 0;

	cursor = dst;
	sourceNode = &srcNode;
	emitNode = 1;
	payloadSize = 0;
	destinationNode = NULL;

	switch ((*sourceNode)->nodeType) {
		case OPT_FACEDATA:
		case OPT_FACEDATA_15:
		case OPT_FACEDATA_16:
		case OPT_FACEDATA_17:
			if (*(int*)(*sourceNode)->param2 < 0) {
				emitNode = 0;
				break;
			}
#ifdef XVT_MODERN
			cursor = XvtOpt_AlignPointer(cursor);
#endif
			destinationNode = (OptNode*)cursor;
			if (g_optConvertVertexNode != NULL) {
				cursor += sizeof(*destinationNode);
				destinationNode->nodeType = (*sourceNode)->nodeType;
				if ((*sourceNode)->pName != NULL) {
					destinationNode->pName = (char*)cursor;
					strcpy((char*)cursor, (*sourceNode)->pName);
					cursor += strlen((*sourceNode)->pName) + 1;
				} else {
					destinationNode->pName = NULL;
				}
				destinationNode->param1 = 0;
#ifdef XVT_MODERN
				cursor = XvtOpt_AlignPointer(cursor);
#endif
				destinationNode->param2 = cursor;
				*(int*)cursor = 0;
				OptModel_AppendConvertedFacesForCurrentMesh(destinationNode, (*sourceNode), srcModel,
															meshState);
				emitNode = 0;
				cursor += sizeof(int) + 100 * destinationNode->param1;
			} else {
				unsigned int faceRecordSize;
				unsigned int trailingSize;
				uint8_t* sourceTrailingData;

				cursor += sizeof(*destinationNode);
				destinationNode->nodeType = (*sourceNode)->nodeType;
				if ((*sourceNode)->pName != NULL) {
					destinationNode->pName = (char*)cursor;
					strcpy((char*)cursor, (*sourceNode)->pName);
					cursor += strlen((*sourceNode)->pName) + 1;
				} else {
					destinationNode->pName = NULL;
				}
				destinationNode->param1 = (*sourceNode)->param1;
#ifdef XVT_MODERN
				cursor = XvtOpt_AlignPointer(cursor);
#endif
				destinationNode->param2 = cursor;
				if (g_optSourceIsVersion0)
					faceRecordSize = sizeof(int) + 48 * (*sourceNode)->param1;
				else
					faceRecordSize = sizeof(int) + 64 * (*sourceNode)->param1;
				memcpy(cursor, (*sourceNode)->param2, faceRecordSize);
				cursor += faceRecordSize;
				trailingSize = 16 * (*sourceNode)->param1;
				memcpy(cursor, (*sourceNode)->param2, trailingSize);
				cursor += trailingSize;
				payloadSize = 36 * (*sourceNode)->param1;
				if (g_optSourceIsVersion0)
					sourceTrailingData =
						(uint8_t*)(*sourceNode)->param2 + sizeof(int) + 48 * (*sourceNode)->param1;
				else
					sourceTrailingData =
						(uint8_t*)(*sourceNode)->param2 + sizeof(int) + 64 * (*sourceNode)->param1;
				memcpy(cursor, sourceTrailingData, payloadSize);
				cursor += payloadSize;
				sourceTrailingData += payloadSize;
				if (meshState->pVertNormals == NULL) {
					payloadSize = sizeof(OptVector) * g_curVertexCount;
					memcpy(cursor, sourceTrailingData, payloadSize);
					cursor += payloadSize;
				}
				emitNode = 0;
			}
			break;

		case OPT_TYPE_2:
			payloadSize = 48;
			break;

		case OPT_MESHVERTS:
			g_modelNodeWalkUnusedScratch0 = (*sourceNode)->param2;
			g_curVertexCount = (*sourceNode)->param1;
			if (g_optConvertVertexNode != NULL)
				emitNode = 0;
			else
				payloadSize = sizeof(OptVector) * (*sourceNode)->param1;
			break;

		case OPT_TYPE_4:
			payloadSize = 12;
			break;

		case OPT_TYPE_5:
			payloadSize = 36;
			break;

		case OPT_TYPE_6:
			payloadSize = 12;
			break;

		case OPT_NODEREF:
			destinationNode = (*sourceNode);
			payloadSize = (unsigned int)strlen((const char*)(*sourceNode)->param2) + 1;
			while (destinationNode->nodeType == OPT_NODEREF) {
				destinationNode = OptModel_ResolveNodeRef(srcModel, (const char*)destinationNode->param2);
				if (destinationNode == NULL)
					break;
			}
			if (destinationNode != NULL && destinationNode->nodeType == OPT_TEXTURE)
				g_optConvertSourceTextureNode = destinationNode;
			break;

		case OPT_TYPE_9:
			payloadSize = 56 * (*sourceNode)->param1;
			g_curMeshFlags = (*sourceNode)->param2;
			break;

		case OPT_VERTNORMALS:
			g_curVertNormals = (OptVector*)(*sourceNode)->param2;
			meshState->pVertNormals = g_curVertNormals;
			if (g_optConvertVertexNormalNode != NULL)
				emitNode = 0;
			else
				payloadSize = sizeof(OptVector) * (*sourceNode)->param1;
			break;

		case OPT_TEXCOORDS:
			g_modelNodeWalkUnusedScratch1 = (*sourceNode)->param2;
			if (g_optConvertTexCoordNode != NULL)
				emitNode = 0;
			else
				payloadSize = sizeof(OptTexCoord) * (*sourceNode)->param1;
			break;

		case OPT_TYPE_19:
			payloadSize = 12;
			break;

		case OPT_TEXTURE: {
			OptTextureData* texture;
			uint16_t* embeddedPalette;
			int textureDataSize;

			g_optConvertSourceTextureNode = (*sourceNode);
			texture = (OptTextureData*)(*sourceNode)->param2;
			textureDataSize = texture->width * texture->height;
			if (textureDataSize == texture->textureSize)
				payloadSize = sizeof(*texture) + texture->dataSize;
			else
				payloadSize = sizeof(*texture) + textureDataSize;
			if (texture->paletteType != 0) {
				payloadSize += 768 * texture->paletteType;
			} else {
				embeddedPalette = (uint16_t*)((uint8_t*)texture + sizeof(*texture));
				if (textureDataSize == texture->textureSize)
					textureDataSize = texture->dataSize;
				embeddedPalette = (uint16_t*)((uint8_t*)embeddedPalette + textureDataSize);
				if (texture->palette == embeddedPalette)
					payloadSize += 12288;
			}
			break;
		}

		case OPT_FACEGROUP:
#ifdef XVT_MODERN
			cursor = XvtOpt_AlignPointer(cursor);
#endif
			destinationNode = (OptNode*)cursor;
			g_optConvertSourceMeshNode = (*sourceNode);
			if (g_optConvertVertexNode == NULL) {
				OptNode* vertexNode;
				OptNode* texCoordNode;
				OptNode* normalNode;
				OptNode** generatedChildren;
				OptVector* vectors;
				OptVector* savedNormals;
				int savedVertexCount;
				int remaining;

				destinationNode->nodeType = OPT_GROUP;
				cursor += sizeof(*destinationNode);
				if ((*sourceNode)->pName != NULL) {
					destinationNode->pName = (char*)cursor;
					strcpy((char*)cursor, (*sourceNode)->pName);
					cursor += strlen((*sourceNode)->pName) + 1;
				} else {
					destinationNode->pName = NULL;
				}
				destinationNode->param2 = NULL;
				destinationNode->param1 = 0;
				destinationNode->childCount = 4;
#ifdef XVT_MODERN
				cursor = XvtOpt_AlignPointer(cursor);
#endif
				destinationNode->pChildren = (OptNode**)cursor;
				generatedChildren = destinationNode->pChildren;
				cursor += sizeof(OptNode*) * 4;

#ifdef XVT_MODERN
				cursor = XvtOpt_AlignPointer(cursor);
#endif
				vertexNode = (OptNode*)cursor;
				generatedChildren[0] = vertexNode;
				vertexNode->nodeType = OPT_MESHVERTS;
				cursor += sizeof(*vertexNode);
				vertexNode->pName = NULL;
				vertexNode->param2 = cursor;
				vertexNode->param1 = 0;
				vertexNode->childCount = 0;
				vertexNode->pChildren = NULL;
				OptModel_CollectUniqueVertices(vertexNode, (*sourceNode), srcModel, meshState);
				g_optConvertVertexNode = vertexNode;

				vectors = (OptVector*)vertexNode->param2;
				minimum.x = maximum.x = vectors->x;
				minimum.y = maximum.y = vectors->y;
				minimum.z = maximum.z = vectors->z;
				remaining = vertexNode->param1;
				if (remaining > 0) {
					do {
						if (vectors->x < minimum.x)
							minimum.x = vectors->x;
						if (vectors->y < minimum.y)
							minimum.y = vectors->y;
						if (vectors->z < minimum.z)
							minimum.z = vectors->z;
						if (vectors->x > maximum.x)
							maximum.x = vectors->x;
						if (vectors->y > maximum.y)
							maximum.y = vectors->y;
						if (vectors->z > maximum.z)
							maximum.z = vectors->z;
						++vectors;
						--remaining;
					} while (remaining != 0);
				}
				vectors -= 2;
				if (vectors[0].x != minimum.x || vectors[0].y != minimum.y || vectors[0].z != minimum.z ||
					vectors[1].x != maximum.x || vectors[1].y != maximum.y || vectors[1].z != maximum.z) {
					vectors += 2;
					vectors[0].x = minimum.x;
					vectors[0].y = minimum.y;
					vectors[0].z = minimum.z;
					++vectors;
					vectors[0].x = maximum.x;
					vectors[0].y = maximum.y;
					vectors[0].z = maximum.z;
					vertexNode->param1 += 2;
				}
				cursor += sizeof(OptVector) * vertexNode->param1;

#ifdef XVT_MODERN
				cursor = XvtOpt_AlignPointer(cursor);
#endif
				texCoordNode = (OptNode*)cursor;
				generatedChildren[1] = texCoordNode;
				texCoordNode->nodeType = OPT_TEXCOORDS;
				cursor += sizeof(*texCoordNode);
				texCoordNode->pName = NULL;
				texCoordNode->param2 = cursor;
				texCoordNode->param1 = 0;
				texCoordNode->childCount = 0;
				texCoordNode->pChildren = NULL;
				OptModel_CollectUniqueTexCoords(texCoordNode, (*sourceNode), srcModel, meshState);
				g_optConvertTexCoordNode = texCoordNode;
				cursor += sizeof(OptTexCoord) * texCoordNode->param1;

#ifdef XVT_MODERN
				cursor = XvtOpt_AlignPointer(cursor);
#endif
				normalNode = (OptNode*)cursor;
				generatedChildren[2] = normalNode;
				normalNode->nodeType = OPT_VERTNORMALS;
				cursor += sizeof(*normalNode);
				normalNode->pName = NULL;
				normalNode->param2 = cursor;
				normalNode->param1 = 0;
				normalNode->childCount = 0;
				normalNode->pChildren = NULL;
				savedNormals = meshState->pVertNormals;
				savedVertexCount = g_curVertexCount;
				meshState->pVertNormals = NULL;
				OptModel_CollectUniqueVertexNormals(normalNode, (*sourceNode), srcModel, meshState);
				meshState->pVertNormals = savedNormals;
				g_optConvertVertexNormalNode = normalNode;
				g_curVertexCount = savedVertexCount;
				cursor += sizeof(OptVector) * normalNode->param1;

#ifdef XVT_MODERN
				cursor = XvtOpt_AlignPointer(cursor);
#endif
				destinationNode = (OptNode*)cursor;
				generatedChildren[3] = destinationNode;
				destinationNode->nodeType = OPT_FACEGROUP;
				cursor += sizeof(*destinationNode);
				destinationNode->pName = NULL;
#ifdef XVT_MODERN
				cursor = XvtOpt_AlignPointer(cursor);
#endif
				destinationNode->param2 = cursor;
				destinationNode->param1 = (*sourceNode)->param1;
				memcpy(cursor, (*sourceNode)->param2, sizeof(int) * (*sourceNode)->param1);
				cursor += sizeof(int) * (*sourceNode)->param1;
				if ((*sourceNode)->childCount > (*sourceNode)->param1) {
					destinationNode->param1 = (*sourceNode)->childCount;
					memset(cursor, 0, sizeof(int) * ((*sourceNode)->childCount - (*sourceNode)->param1));
					cursor += sizeof(int) * ((*sourceNode)->childCount - (*sourceNode)->param1);
				} else if ((*sourceNode)->childCount < (*sourceNode)->param1) {
					destinationNode->param1 = (*sourceNode)->childCount;
					cursor += sizeof(int) * ((*sourceNode)->childCount - (*sourceNode)->param1);
				}
			} else {
				destinationNode->nodeType = OPT_FACEGROUP;
				cursor += sizeof(*destinationNode);
				destinationNode->pName = NULL;
#ifdef XVT_MODERN
				cursor = XvtOpt_AlignPointer(cursor);
#endif
				destinationNode->param2 = cursor;
				destinationNode->param1 = (*sourceNode)->param1;
				memcpy(cursor, (*sourceNode)->param2, sizeof(int) * (*sourceNode)->param1);
				cursor += sizeof(int) * (*sourceNode)->param1;
				if ((*sourceNode)->childCount > (*sourceNode)->param1) {
					destinationNode->param1 = (*sourceNode)->childCount;
					memset(cursor, 0, sizeof(int) * ((*sourceNode)->childCount - (*sourceNode)->param1));
					cursor += sizeof(int) * ((*sourceNode)->childCount - (*sourceNode)->param1);
				} else if ((*sourceNode)->childCount < (*sourceNode)->param1) {
					destinationNode->param1 = (*sourceNode)->childCount;
					cursor += sizeof(int) * ((*sourceNode)->childCount - (*sourceNode)->param1);
				}
			}
			emitNode = 0;
			break;

		case OPT_HARDPOINT:
			payloadSize = 16;
			break;

		case OPT_ROTSCALE:
			payloadSize = 48;
			break;

		case OPT_MESHDESC:
			payloadSize = 72;
			break;

		default:
			break;
	}

	if (emitNode == 1) {
#ifdef XVT_MODERN
		cursor = XvtOpt_AlignPointer(cursor);
#endif
		destinationNode = (OptNode*)cursor;
		cursor += sizeof(*destinationNode);
		destinationNode->nodeType = (*sourceNode)->nodeType;
		if ((*sourceNode)->pName != NULL) {
			destinationNode->pName = (char*)cursor;
			strcpy((char*)cursor, (*sourceNode)->pName);
			cursor += strlen((*sourceNode)->pName) + 1;
		} else {
			destinationNode->pName = NULL;
		}
		destinationNode->param1 = (*sourceNode)->param1;
#ifdef XVT_MODERN
		cursor = XvtOpt_AlignPointer(cursor);
#endif
		destinationNode->param2 = cursor;
		memcpy(cursor, (*sourceNode)->param2, payloadSize);
		cursor += payloadSize;
		if ((*sourceNode)->nodeType == OPT_TEXTURE) {
			OptTextureData* sourceTexture;
			uint16_t* embeddedPalette;
			int textureDataSize;

			sourceTexture = (OptTextureData*)(*sourceNode)->param2;
			if (sourceTexture->paletteType == 0) {
				embeddedPalette = (uint16_t*)((uint8_t*)sourceTexture + sizeof(*sourceTexture));
				textureDataSize = sourceTexture->width * sourceTexture->height;
				if (sourceTexture->textureSize == textureDataSize)
					textureDataSize = sourceTexture->dataSize;
				embeddedPalette = (uint16_t*)((uint8_t*)embeddedPalette + textureDataSize);
				if (sourceTexture->palette != embeddedPalette) {
					void* sharedTextureData;

					sharedTextureData = OptModel_FindEarlierSharedTextureData(sourceTexture->palette,
																			  dstModel, destinationNode);
					if (sharedTextureData != NULL) {
						OptTextureData* destinationTexture;

						destinationTexture = (OptTextureData*)destinationNode->param2;
						destinationTexture->palette = sharedTextureData;
					} else {
						const void* sourcePalette;
						OptTextureData* destinationTexture;

						sourcePalette = sourceTexture->palette;
						destinationTexture = (OptTextureData*)destinationNode->param2;
						destinationTexture->palette = (uint16_t*)cursor;
						memcpy(cursor, sourcePalette, 12288);
						cursor += 12288;
					}
				} else {
					OptTextureData* destinationTexture;
					uint16_t* destinationPalette;

					destinationTexture = (OptTextureData*)destinationNode->param2;
					destinationPalette =
						(uint16_t*)((uint8_t*)destinationTexture + sizeof(*destinationTexture));
					textureDataSize = destinationTexture->width * destinationTexture->height;
					if (destinationTexture->textureSize == textureDataSize)
						textureDataSize = destinationTexture->dataSize;
					destinationPalette = (uint16_t*)((uint8_t*)destinationPalette + textureDataSize);
					destinationTexture->palette = destinationPalette;
				}
			}
		}
	}

	if (destinationNode == NULL) {
		if ((*sourceNode)->childCount == 0)
			return 0;
#ifdef XVT_MODERN
		cursor = XvtOpt_AlignPointer(cursor);
#endif
		destinationNode = (OptNode*)cursor;
		cursor += sizeof(*destinationNode);
		destinationNode->pName = NULL;
		destinationNode->nodeType = OPT_GROUP;
		destinationNode->param1 = 0;
		destinationNode->param2 = NULL;
	}

	destinationNode->childCount = 0;
	destinationNode->pChildren = NULL;
	if ((*sourceNode)->childCount != 0) {
		if (g_optConvertVertexNode == NULL) {
			OptNode* vertexNode;
			OptNode* texCoordNode;
			OptNode* normalNode;
			OptVector* vectors;
			OptVector* savedNormals;
			int savedVertexCount;
			int remaining;

			destinationNode->childCount = (*sourceNode)->childCount + 3;
#ifdef XVT_MODERN
			cursor = XvtOpt_AlignPointer(cursor);
#endif
			destinationNode->pChildren = (OptNode**)cursor;
			cursor += sizeof(OptNode*) * destinationNode->childCount;

#ifdef XVT_MODERN
			cursor = XvtOpt_AlignPointer(cursor);
#endif
			vertexNode = (OptNode*)cursor;
			destinationNode->pChildren[0] = vertexNode;
			vertexNode->nodeType = OPT_MESHVERTS;
			cursor += sizeof(*vertexNode);
			vertexNode->pName = NULL;
			vertexNode->param2 = cursor;
			vertexNode->param1 = 0;
			vertexNode->childCount = 0;
			vertexNode->pChildren = NULL;
			OptModel_CollectUniqueVertices(vertexNode, (*sourceNode), srcModel, meshState);
			g_optConvertVertexNode = vertexNode;

			vectors = (OptVector*)vertexNode->param2;
			minimum.x = maximum.x = vectors->x;
			minimum.y = maximum.y = vectors->y;
			minimum.z = maximum.z = vectors->z;
			remaining = vertexNode->param1;
			if (remaining > 0) {
				do {
					if (vectors->x < minimum.x)
						minimum.x = vectors->x;
					if (vectors->y < minimum.y)
						minimum.y = vectors->y;
					if (vectors->z < minimum.z)
						minimum.z = vectors->z;
					if (vectors->x > maximum.x)
						maximum.x = vectors->x;
					if (vectors->y > maximum.y)
						maximum.y = vectors->y;
					if (vectors->z > maximum.z)
						maximum.z = vectors->z;
					++vectors;
					--remaining;
				} while (remaining != 0);
			}
			vectors -= 2;
			if (vectors[0].x != minimum.x || vectors[0].y != minimum.y || vectors[0].z != minimum.z ||
				vectors[1].x != maximum.x || vectors[1].y != maximum.y || vectors[1].z != maximum.z) {
				vectors += 2;
				vectors[0].x = minimum.x;
				vectors[0].y = minimum.y;
				vectors[0].z = minimum.z;
				++vectors;
				vectors[0].x = maximum.x;
				vectors[0].y = maximum.y;
				vectors[0].z = maximum.z;
				vertexNode->param1 += 2;
			}
			cursor += sizeof(OptVector) * vertexNode->param1;

#ifdef XVT_MODERN
			cursor = XvtOpt_AlignPointer(cursor);
#endif
			texCoordNode = (OptNode*)cursor;
			destinationNode->pChildren[1] = texCoordNode;
			texCoordNode->nodeType = OPT_TEXCOORDS;
			cursor += sizeof(*texCoordNode);
			texCoordNode->pName = NULL;
			texCoordNode->param2 = cursor;
			texCoordNode->param1 = 0;
			texCoordNode->childCount = 0;
			texCoordNode->pChildren = NULL;
			OptModel_CollectUniqueTexCoords(texCoordNode, (*sourceNode), srcModel, meshState);
			g_optConvertTexCoordNode = texCoordNode;
			cursor += sizeof(OptTexCoord) * texCoordNode->param1;

#ifdef XVT_MODERN
			cursor = XvtOpt_AlignPointer(cursor);
#endif
			normalNode = (OptNode*)cursor;
			destinationNode->pChildren[2] = normalNode;
			normalNode->nodeType = OPT_VERTNORMALS;
			cursor += sizeof(*normalNode);
			normalNode->pName = NULL;
			normalNode->param2 = cursor;
			normalNode->param1 = 0;
			normalNode->childCount = 0;
			normalNode->pChildren = NULL;
			savedNormals = meshState->pVertNormals;
			savedVertexCount = g_curVertexCount;
			meshState->pVertNormals = NULL;
			OptModel_CollectUniqueVertexNormals(normalNode, (*sourceNode), srcModel, meshState);
			meshState->pVertNormals = savedNormals;
			g_optConvertVertexNormalNode = normalNode;
			g_curVertexCount = savedVertexCount;
			firstChildIndex = 3;
			cursor += sizeof(OptVector) * normalNode->param1;
		} else {
			firstChildIndex = 0;
			destinationNode->childCount = (*sourceNode)->childCount;
#ifdef XVT_MODERN
			cursor = XvtOpt_AlignPointer(cursor);
#endif
			destinationNode->pChildren = (OptNode**)cursor;
			cursor += sizeof(OptNode*) * destinationNode->childCount;
		}

		childMesh = *meshState;
		for (childIndex = 0; childIndex < (*sourceNode)->childCount; ++childIndex) {
			unsigned int childSize;

#ifdef XVT_MODERN
			cursor = XvtOpt_AlignPointer(cursor);
#endif
			destinationNode->pChildren[firstChildIndex + childIndex] = (OptNode*)cursor;
			childSize = OptModel_ConvertLegacyNodeToOptimized(cursor, (*sourceNode)->pChildren[childIndex],
															  srcModel, dstModel, &childMesh);
			if (childSize == 0)
				destinationNode->pChildren[firstChildIndex + childIndex] = NULL;
			cursor += childSize;
		}
	}

#ifdef XVT_MODERN
	return (unsigned int)XvtOpt_AlignSize((size_t)(cursor - dst));
#else
	return (unsigned int)(cursor - dst);
#endif
}

// FUNCTION: XVT 0x475740
void OptModel_CollectUniqueVertices(OptNode* dstVertexNode, OptNode* srcNode, OptimizedPolyObject* srcModel,
									SceneMesh* meshState) {
	float* sourceVertex;
	int childIndex;

	if (srcNode == NULL) {
		return;
	}
	while (srcNode->nodeType == OPT_NODEREF) {
		srcNode = OptModel_ResolveNodeRef(srcModel, (const char*)srcNode->param2);
		if (srcNode == NULL) {
			return;
		}
	}

	if (srcNode->nodeType == OPT_MESHVERTS) {
		int sourceIndex;

		sourceVertex = (float*)srcNode->param2;
		for (sourceIndex = 0; sourceIndex < srcNode->param1; ++sourceIndex) {
			float* destinationVertex;
			int destinationCount;
			int destinationIndex;

			destinationVertex = (float*)dstVertexNode->param2;
			destinationIndex = 0;
			destinationCount = dstVertexNode->param1;
			while (destinationIndex < destinationCount) {
				if (sourceVertex[0] == destinationVertex[0] && sourceVertex[1] == destinationVertex[1] &&
					sourceVertex[2] == destinationVertex[2]) {
					break;
				}
				destinationVertex += 3;
				++destinationIndex;
			}
			if (destinationIndex == destinationCount) {
				destinationVertex[0] = sourceVertex[0];
				destinationVertex[1] = sourceVertex[1];
				destinationVertex[2] = sourceVertex[2];
				++dstVertexNode->param1;
			}
			sourceVertex += 3;
		}
	}

	childIndex = 0;
	while (srcNode->childCount > childIndex) {
		OptModel_CollectUniqueVertices(dstVertexNode, srcNode->pChildren[childIndex], srcModel, meshState);
		++childIndex;
	}
}

// FUNCTION: XVT 0x475850
void OptModel_CollectUniqueTexCoords(OptNode* dstTexCoordNode, OptNode* srcNode,
									 OptimizedPolyObject* srcModel, SceneMesh* meshState) {
	OptNode* destinationNode;
	int childIndex;

	if (srcNode == NULL) {
		return;
	}
	while (srcNode->nodeType == OPT_NODEREF) {
		srcNode = OptModel_ResolveNodeRef(srcModel, (const char*)srcNode->param2);
		if (srcNode == NULL) {
			return;
		}
	}

	if (srcNode->nodeType == OPT_TEXCOORDS) {
		float* sourceTexCoord;
		int sourceIndex;

		sourceTexCoord = (float*)srcNode->param2;
		destinationNode = dstTexCoordNode;
		sourceIndex = 0;
		while (sourceIndex < srcNode->param1) {
			float* destinationTexCoord;
			int destinationIndex;
			int destinationCount;

			destinationTexCoord = (float*)destinationNode->param2;
			destinationIndex = 0;
			destinationCount = destinationNode->param1;
			while (destinationIndex < destinationCount) {
				if (sourceTexCoord[0] == destinationTexCoord[0] &&
					sourceTexCoord[1] == destinationTexCoord[1]) {
					break;
				}
				destinationTexCoord += 2;
				++destinationIndex;
			}
			if (destinationIndex == destinationCount) {
				destinationTexCoord[0] = sourceTexCoord[0];
				destinationTexCoord[1] = sourceTexCoord[1];
				++destinationNode->param1;
			}
			sourceTexCoord += 2;
			++sourceIndex;
		}
	} else {
		destinationNode = dstTexCoordNode;
	}

	childIndex = 0;
	while (childIndex < srcNode->childCount) {
		OptModel_CollectUniqueTexCoords(destinationNode, srcNode->pChildren[childIndex], srcModel, meshState);
		++childIndex;
	}
}

// FUNCTION: XVT 0x475940
void OptModel_CollectUniqueVertexNormals(OptNode* dstNormalNode, OptNode* srcNode,
										 OptimizedPolyObject* srcModel, SceneMesh* meshState) {
	OptNode* node;
	OptNode* destinationNode;
	OptVector* sourceNormal;
	int sourceIndex;
	int childIndex;

	node = srcNode;
	if (node == NULL) {
		return;
	}
	while (node->nodeType == OPT_NODEREF) {
		node = OptModel_ResolveNodeRef(srcModel, (const char*)node->param2);
		if (node == NULL) {
			return;
		}
	}

	destinationNode = dstNormalNode;
	switch (node->nodeType) {
		case OPT_FACEDATA:
		case OPT_FACEDATA_15:
		case OPT_FACEDATA_16:
		case OPT_FACEDATA_17:
			if (meshState->pVertNormals == NULL) {
				OptLegacyFacePayload* faceData;

				faceData = (OptLegacyFacePayload*)node->param2;
				if (g_optSourceIsVersion0) {
					sourceNormal = (OptVector*)&((OptLegacyFacePayloadV0*)faceData)->storage[node->param1];
				} else {
					sourceNormal = (OptVector*)&faceData->storage[node->param1];
				}
				sourceIndex = 0;
				if (g_curVertexCount > 0) {
					do {
						OptVector* destinationNormal;
						int destinationCount;
						int destinationIndex;

						destinationNormal = (OptVector*)destinationNode->param2;
						destinationIndex = 0;
						destinationCount = destinationNode->param1;
						if (destinationIndex < destinationCount) {
							do {
								if (destinationNormal->x == sourceNormal->x) {
									if (sourceNormal->y == destinationNormal->y &&
										sourceNormal->z == destinationNormal->z) {
										break;
									}
								}
								++destinationNormal;
								++destinationIndex;
							} while (destinationIndex < destinationCount);
						}
						if (destinationIndex == destinationCount) {
							destinationNormal->x = sourceNormal->x;
							destinationNormal->y = sourceNormal->y;
							destinationNormal->z = sourceNormal->z;
							++destinationNode->param1;
						}
						++sourceNormal;
						++sourceIndex;
					} while (sourceIndex < g_curVertexCount);
				}
			}
			break;

		case OPT_MESHVERTS:
			g_curVertexCount = node->param1;
			break;

		case OPT_VERTNORMALS:
			meshState->pVertNormals = (OptVector*)node->param2;
			sourceNormal = (OptVector*)node->param2;
			sourceIndex = 0;
			if (node->param1 > 0) {
				do {
					OptVector* destinationNormal;
					int destinationCount;
					int destinationIndex;

					destinationNormal = (OptVector*)destinationNode->param2;
					destinationIndex = 0;
					destinationCount = destinationNode->param1;
					if (destinationIndex < destinationCount) {
						do {
							if (destinationNormal->x == sourceNormal->x) {
								if (sourceNormal->y == destinationNormal->y &&
									destinationNormal->z == sourceNormal->z) {
									break;
								}
							}
							++destinationNormal;
							++destinationIndex;
						} while (destinationIndex < destinationCount);
					}
					if (destinationIndex == destinationCount) {
						destinationNormal->x = sourceNormal->x;
						destinationNormal->y = sourceNormal->y;
						destinationNormal->z = sourceNormal->z;
						++destinationNode->param1;
					}
					++sourceNormal;
					++sourceIndex;
				} while (sourceIndex < node->param1);
			}
			break;

		default:
			break;
	}

	if (g_optSourceIsVersion0) {
		meshState->pVertNormals = NULL;
	}
	childIndex = 0;
	while (childIndex < node->childCount) {
		OptModel_CollectUniqueVertexNormals(destinationNode, node->pChildren[childIndex], srcModel,
											meshState);
		++childIndex;
	}
}

// FUNCTION: XVT 0x475B70
int OptModel_RemapVectorIndex(const OptNode* uniqueVectorNode, const OptVector* sourceVectors,
							  int sourceIndex) {
	const float* uniqueVectors;
	int cursor;

	if (sourceIndex < 0)
		return -1;

	sourceVectors += sourceIndex;
	uniqueVectors = uniqueVectorNode->param2;
	cursor = g_optConvertVectorSearchCursor;
	cursor -= sourceIndex >> 1;
	g_optConvertVectorSearchCursor = cursor;
	if (cursor < 0 || uniqueVectorNode->param1 < cursor) {
		g_optConvertVectorSearchCursor = 0;
		cursor = 0;
	}

	uniqueVectors += 3 * g_optConvertVectorSearchCursor;
	if (uniqueVectorNode->param1 > g_optConvertVectorSearchCursor) {
		do {
			if (uniqueVectors[0] != sourceVectors->x || uniqueVectors[1] != sourceVectors->y ||
				sourceVectors->z != uniqueVectors[2]) {
				uniqueVectors += 3;
				cursor = g_optConvertVectorSearchCursor;
				++cursor;
				g_optConvertVectorSearchCursor = cursor;
			} else {
				return g_optConvertVectorSearchCursor;
			}
		} while (uniqueVectorNode->param1 > g_optConvertVectorSearchCursor);
	}

	g_optConvertVectorSearchCursor = 0;
	uniqueVectors = uniqueVectorNode->param2;
	if (uniqueVectorNode->param1 > 0) {
		do {
			if (uniqueVectors[0] != sourceVectors->x || uniqueVectors[1] != sourceVectors->y ||
				sourceVectors->z != uniqueVectors[2]) {
				uniqueVectors += 3;
				cursor = g_optConvertVectorSearchCursor;
				++cursor;
				g_optConvertVectorSearchCursor = cursor;
			} else {
				return g_optConvertVectorSearchCursor;
			}
		} while (uniqueVectorNode->param1 > g_optConvertVectorSearchCursor);
	}

	return 0;
}

// FUNCTION: XVT 0x475C70
int OptModel_RemapTexCoordIndex(const OptNode* uniqueTexCoordNode, const OptTexCoord* sourceTexCoords,
								int sourceIndex) {
	const float* uniqueTexCoords;

	if (sourceIndex < 0)
		return -1;

	sourceTexCoords += sourceIndex;
	uniqueTexCoords = uniqueTexCoordNode->param2;
	g_optConvertTexCoordSearchCursor -= sourceIndex >> 1;
	if (g_optConvertTexCoordSearchCursor < 0 ||
		uniqueTexCoordNode->param1 < g_optConvertTexCoordSearchCursor) {
		g_optConvertTexCoordSearchCursor = 0;
	}

	uniqueTexCoords += 2 * g_optConvertTexCoordSearchCursor;
	if (uniqueTexCoordNode->param1 > g_optConvertTexCoordSearchCursor) {
		do {
			if (uniqueTexCoords[0] != sourceTexCoords->u || uniqueTexCoords[1] != sourceTexCoords->v) {
				uniqueTexCoords += 2;
				++g_optConvertTexCoordSearchCursor;
			} else {
				return g_optConvertTexCoordSearchCursor;
			}
		} while (uniqueTexCoordNode->param1 > g_optConvertTexCoordSearchCursor);
	}

	g_optConvertTexCoordSearchCursor = 0;
	uniqueTexCoords = uniqueTexCoordNode->param2;
	if (uniqueTexCoordNode->param1 > 0) {
		do {
			if (uniqueTexCoords[0] != sourceTexCoords->u || uniqueTexCoords[1] != sourceTexCoords->v) {
				uniqueTexCoords += 2;
				++g_optConvertTexCoordSearchCursor;
			} else {
				return g_optConvertTexCoordSearchCursor;
			}
		} while (uniqueTexCoordNode->param1 > g_optConvertTexCoordSearchCursor);
	}

	return 0;
}

// FUNCTION: XVT 0x475D50
void OptModel_AppendConvertedFacesForNode(OptNode* dstFaceNode, OptNode* targetFaceNode, OptNode* node,
										  OptimizedPolyObject* srcModel, SceneMesh* meshState) {
	uint8_t* destinationBytes;
	uint8_t* destinationTrailingBytes;
	uint8_t* destinationData;
	int* destinationCursor;
	const int* sourceCursor;
	const OptVector* sourceVectors;
	const OptVector* sourceFaceNormals;
	const OptVector* sourceTextureGradients;
	OptVector* destinationFaceNormals;
	OptVector* destinationTextureGradients;
	int destinationEdgeCount;
	int faceIndex;
	int childIndex;
	if (node == NULL)
		return;

	while (node->nodeType == OPT_NODEREF) {
		node = OptModel_ResolveNodeRef(srcModel, (const char*)node->param2);
		if (node == NULL)
			return;
	}

	if (g_optConvertTargetFaceFound == 0 && node == targetFaceNode)
		g_optConvertTargetFaceFound = 1;

	switch (node->nodeType) {
		case OPT_FACEDATA:
		case OPT_FACEDATA_15:
		case OPT_FACEDATA_16:
		case OPT_FACEDATA_17:
			if (g_optConvertTargetFaceFound != 0 &&
				g_optConvertFaceTextureNode == g_optConvertSourceTextureNode && *(int*)node->param2 > 0) {
				destinationData = dstFaceNode->param2;
				destinationBytes = destinationData + sizeof(int);
				destinationEdgeCount = *(int*)destinationData;
#ifdef XVT_MODERN
				memmove(destinationBytes + 64 * node->param1, destinationBytes, 100 * dstFaceNode->param1);
#else
				memcpy(destinationBytes + 64 * node->param1, destinationBytes, 100 * dstFaceNode->param1);
#endif

				sourceCursor = (const int*)node->param2 + 1;
				sourceVectors = meshState->pVertNormals;
				if (sourceVectors == NULL) {
					if (g_optSourceIsVersion0)
						sourceVectors = (const OptVector*)((const uint8_t*)sourceCursor + 48 * node->param1 +
														   36 * node->param1);
					else
						sourceVectors = (const OptVector*)((const uint8_t*)sourceCursor + 64 * node->param1 +
														   36 * node->param1);
				}

				destinationCursor = (int*)destinationBytes;
				for (faceIndex = 0; faceIndex < node->param1; ++faceIndex) {
					int sourceEdgeIndex;

					*destinationCursor++ = OptModel_RemapVectorIndex(
						g_optConvertVertexNode, (const OptVector*)g_modelNodeWalkUnusedScratch0,
						*sourceCursor++);
					*destinationCursor++ = OptModel_RemapVectorIndex(
						g_optConvertVertexNode, (const OptVector*)g_modelNodeWalkUnusedScratch0,
						*sourceCursor++);
					*destinationCursor++ = OptModel_RemapVectorIndex(
						g_optConvertVertexNode, (const OptVector*)g_modelNodeWalkUnusedScratch0,
						*sourceCursor++);
					*destinationCursor++ = OptModel_RemapVectorIndex(
						g_optConvertVertexNode, (const OptVector*)g_modelNodeWalkUnusedScratch0,
						*sourceCursor++);

					*destinationCursor++ = destinationEdgeCount + *sourceCursor++;
					*destinationCursor++ = destinationEdgeCount + *sourceCursor++;
					*destinationCursor++ = destinationEdgeCount + *sourceCursor++;
					sourceEdgeIndex = *sourceCursor++;
					if (sourceEdgeIndex == -1)
						*destinationCursor++ = -1;
					else
						*destinationCursor++ = destinationEdgeCount + sourceEdgeIndex;

					*destinationCursor++ = OptModel_RemapTexCoordIndex(
						g_optConvertTexCoordNode, (const OptTexCoord*)g_modelNodeWalkUnusedScratch1,
						*sourceCursor++);
					*destinationCursor++ = OptModel_RemapTexCoordIndex(
						g_optConvertTexCoordNode, (const OptTexCoord*)g_modelNodeWalkUnusedScratch1,
						*sourceCursor++);
					*destinationCursor++ = OptModel_RemapTexCoordIndex(
						g_optConvertTexCoordNode, (const OptTexCoord*)g_modelNodeWalkUnusedScratch1,
						*sourceCursor++);
					*destinationCursor++ = OptModel_RemapTexCoordIndex(
						g_optConvertTexCoordNode, (const OptTexCoord*)g_modelNodeWalkUnusedScratch1,
						*sourceCursor++);

					if (g_optSourceIsVersion0)
						sourceCursor -= 12;
					*destinationCursor++ = OptModel_RemapVectorIndex(g_optConvertVertexNormalNode,
																	 sourceVectors, *sourceCursor++);
					*destinationCursor++ = OptModel_RemapVectorIndex(g_optConvertVertexNormalNode,
																	 sourceVectors, *sourceCursor++);
					*destinationCursor++ = OptModel_RemapVectorIndex(g_optConvertVertexNormalNode,
																	 sourceVectors, *sourceCursor++);
					*destinationCursor++ = OptModel_RemapVectorIndex(g_optConvertVertexNormalNode,
																	 sourceVectors, *sourceCursor++);
					if (g_optSourceIsVersion0)
						sourceCursor += 8;
				}

				destinationTrailingBytes = destinationBytes + 64 * (node->param1 + dstFaceNode->param1);
				destinationFaceNormals = (OptVector*)destinationTrailingBytes;
#ifdef XVT_MODERN
				memmove(destinationTrailingBytes + 12 * node->param1, destinationTrailingBytes,
						36 * dstFaceNode->param1);
#else
				memcpy(destinationTrailingBytes + 12 * node->param1, destinationTrailingBytes,
					   36 * dstFaceNode->param1);
#endif
				if (g_optSourceIsVersion0)
					sourceFaceNormals =
						(const OptVector*)((const uint8_t*)node->param2 + sizeof(int) + 48 * node->param1);
				else
					sourceFaceNormals =
						(const OptVector*)((const uint8_t*)node->param2 + sizeof(int) + 64 * node->param1);
				for (faceIndex = 0; faceIndex < node->param1; ++faceIndex)
					destinationFaceNormals[faceIndex] = sourceFaceNormals[faceIndex];

				destinationTrailingBytes =
					(uint8_t*)&destinationFaceNormals[node->param1 + dstFaceNode->param1];
				destinationTextureGradients = (OptVector*)destinationTrailingBytes;
#ifdef XVT_MODERN
				memmove(destinationTrailingBytes + 24 * node->param1, destinationTrailingBytes,
						24 * dstFaceNode->param1);
#else
				memcpy(destinationTrailingBytes + 24 * node->param1, destinationTrailingBytes,
					   24 * dstFaceNode->param1);
#endif
				sourceTextureGradients = sourceFaceNormals + node->param1;
				for (faceIndex = 0; faceIndex < node->param1; ++faceIndex) {
					destinationTextureGradients[2 * faceIndex] = sourceTextureGradients[2 * faceIndex];
					destinationTextureGradients[2 * faceIndex + 1] =
						sourceTextureGradients[2 * faceIndex + 1];
				}

				dstFaceNode->param1 += node->param1;
				*(int*)destinationData += *(int*)node->param2;
				*(int*)node->param2 = -1;
			}
			break;

		case OPT_MESHVERTS:
			g_modelNodeWalkUnusedScratch0 = node->param2;
			break;

		case OPT_VERTNORMALS:
			meshState->pVertNormals = (OptVector*)node->param2;
			break;

		case OPT_TEXCOORDS:
			g_modelNodeWalkUnusedScratch1 = node->param2;
			break;

		case OPT_TEXTURE:
			g_optConvertFaceTextureNode = node;
			break;

		default:
			break;
	}

	for (childIndex = 0; childIndex < node->childCount; ++childIndex) {
		OptModel_AppendConvertedFacesForNode(dstFaceNode, targetFaceNode, node->pChildren[childIndex],
											 srcModel, meshState);
	}
}

// FUNCTION: XVT 0x476280
void OptModel_AppendConvertedFacesForCurrentMesh(OptNode* dstFaceNode, OptNode* targetFaceNode,
												 OptimizedPolyObject* srcModel, SceneMesh* meshState) {
	int childOffset;
	int childIndex;

	g_optConvertFaceTextureNode = g_optConvertSourceTextureNode;
	childIndex = 0;
	g_optConvertTargetFaceFound = 0;
	if (g_optConvertSourceMeshNode->childCount > 0) {
		childOffset = 0;
		do {
			OptModel_AppendConvertedFacesForNode(
				dstFaceNode, targetFaceNode,
				*(OptNode**)((uint8_t*)g_optConvertSourceMeshNode->pChildren + childOffset), srcModel,
				meshState);
			if (g_optConvertTargetFaceFound != 0)
				break;
			childOffset += sizeof(OptNode*);
			++childIndex;
		} while (g_optConvertSourceMeshNode->childCount > childIndex);
	}
}

// FUNCTION: XVT 0x4762F0
uint16_t OptModel_CreateRuntimeHandle(unsigned int sourceHandle) {
	OptimizedPolyObject* sourceModel;
	OptimizedPolyObject* runtimeModel;
	SceneMesh meshState;
	unsigned int serializedSize;
	int rootIndex;
	uint16_t runtimeHandle;
	uint8_t* nodeStorage;

#ifdef XVT_MODERN
	if (!sourceHandle)
		return 0;
#endif
	sourceModel = (OptimizedPolyObject*)Memory_LockHandle(sourceHandle);
	if (sourceModel->selfMarker != sourceModel)
		OptModel_AdjustOptimizedPolyObjectPointers(sourceModel);
	memset(&meshState, 0, sizeof(meshState));
	g_modelNodeWalkUnusedScratch0 = NULL;
	g_modelNodeWalkUnusedScratch1 = NULL;
	g_curVertNormals = NULL;
	g_modelNodeWalkUnusedScratch2 = NULL;
	g_curMeshFlags = NULL;
	g_curVertexCount = 0;

	serializedSize = sizeof(OptNode*) * (unsigned int)sourceModel->rootNodeCount + sizeof(*runtimeModel);

	for (rootIndex = 0; rootIndex < sourceModel->rootNodeCount; ++rootIndex)
		serializedSize += OptModel_BuildRuntimeNode(sourceModel->rootNodes[rootIndex], &meshState, NULL);
	Memory_UnlockHandle(sourceHandle);
	runtimeHandle = Memory_AllocHandle(serializedSize, 0);
	if (runtimeHandle == 0) {
		FeDiskIo_FatalError(FILE_ERROR_STR_NOT_ENOUGH_MEMORY);
#ifdef XVT_MODERN
		return 0;
#endif
	}

#ifdef XVT_MODERN
	if (!sourceHandle)
		return 0;
#endif
	sourceModel = (OptimizedPolyObject*)Memory_LockHandle(sourceHandle);
	if (sourceModel->selfMarker != sourceModel)
		OptModel_AdjustOptimizedPolyObjectPointers(sourceModel);
	runtimeModel = (OptimizedPolyObject*)Memory_LockHandle(runtimeHandle);
	memcpy(runtimeModel, sourceModel, sizeof(*runtimeModel));
	runtimeModel->selfMarker = runtimeModel;
	runtimeModel->rootNodes = (OptNode**)((uint8_t*)runtimeModel + sizeof(*runtimeModel));
	nodeStorage = (uint8_t*)(runtimeModel->rootNodes + sourceModel->rootNodeCount);
	for (rootIndex = 0; rootIndex < sourceModel->rootNodeCount; ++rootIndex) {
		runtimeModel->rootNodes[rootIndex] = (OptNode*)nodeStorage;
		nodeStorage += OptModel_BuildRuntimeNode(sourceModel->rootNodes[rootIndex], &meshState, nodeStorage);
	}
	for (rootIndex = 0; rootIndex < runtimeModel->rootNodeCount; ++rootIndex)
		OptModel_FixupRuntimeTexturePointers(runtimeModel->rootNodes[rootIndex], runtimeModel, sourceModel);
	Memory_UnlockHandle(runtimeHandle);
	Memory_UnlockHandle(sourceHandle);
	return runtimeHandle;
}

// FUNCTION: XVT 0x476490
void OptModel_FixupRuntimeTexturePointers(OptNode* node, OptimizedPolyObject* dstModel,
										  OptimizedPolyObject* srcModel) {
	OptNode* currentNode;
	OptTextureData* textureData;
	uint8_t* palette;
	int textureDataSize;
	OptNode* correspondingNode;
	OptTextureData* correspondingTextureData;
	uint16_t* sourcePalette;
	int childCount;
	int childOffset;
	int childIndex;

	currentNode = node;
	if (currentNode != NULL) {
		while (currentNode->nodeType == OPT_NODEREF) {
			currentNode = OptModel_ResolveNodeRef(dstModel, (const char*)currentNode->param2);
			if (currentNode == NULL)
				return;
		}

		if (currentNode->nodeType == OPT_TEXTURE) {
			textureData = (OptTextureData*)currentNode->param2;
			if (textureData->paletteType != 0) {
				textureData->paletteType = 0;
				palette = (uint8_t*)textureData + sizeof(*textureData);
				textureDataSize = textureData->width * textureData->height;
				if (textureData->textureSize == textureDataSize)
					textureDataSize = textureData->dataSize;
				palette += textureDataSize;
				if (g_flight16bppBytesPerPixel == 2)
					palette -= 4096;
				textureData->palette = (uint16_t*)palette;
			} else {
				sourcePalette = textureData->palette;
				palette = (uint8_t*)sourcePalette;
				if (g_flight16bppBytesPerPixel == 2)
					palette += 4096;
				palette -= sizeof(*textureData);
				textureDataSize = textureData->width * textureData->height;
				if (textureData->textureSize == textureDataSize)
					textureDataSize = textureData->dataSize;
				palette -= textureDataSize;
				if (palette != (uint8_t*)textureData) {
					correspondingNode =
						OptModel_FindCorrespondingTextureNodeInModel(dstModel, srcModel, sourcePalette);
					if (correspondingNode != NULL) {
						correspondingTextureData = (OptTextureData*)correspondingNode->param2;
						palette = (uint8_t*)correspondingTextureData + sizeof(*correspondingTextureData);
						textureDataSize = correspondingTextureData->width * correspondingTextureData->height;
						if (correspondingTextureData->textureSize == textureDataSize)
							textureDataSize = correspondingTextureData->dataSize;
						palette += textureDataSize;
						if (g_flight16bppBytesPerPixel == 2)
							palette -= 4096;
						textureData->palette = (uint16_t*)palette;
					}
				}
			}
		}

		childIndex = 0;
		childOffset = 0;
		if (currentNode->childCount > childIndex) {
			do {
				OptModel_FixupRuntimeTexturePointers(
					*(OptNode**)((uint8_t*)currentNode->pChildren + childOffset), dstModel, srcModel);
				childOffset += sizeof(*currentNode->pChildren);
				++childIndex;
				childCount = currentNode->childCount;
			} while (childCount > childIndex);
		}
	}
}

// FUNCTION: XVT 0x4765B0
OptNode* OptModel_FindCorrespondingTextureNode(OptNode* srcNode, OptNode* dstNode,
											   const uint16_t* sourcePalette) {
	OptNode* destination;
	OptNode* source;
	OptTextureData* textureData;
	int textureDataSize;
	uint16_t* embeddedPalette;
	int childOffset;
	int childIndex;
	OptNode* result;

	source = srcNode;
	if (source == NULL)
		return NULL;
	destination = dstNode;
	if (destination == NULL)
		return NULL;
	if (destination->nodeType != source->nodeType)
		return NULL;

	if (source->nodeType == OPT_TEXTURE) {
		textureData = (OptTextureData*)source->param2;
		embeddedPalette = (uint16_t*)((uint8_t*)textureData + sizeof(*textureData));
		textureDataSize = textureData->width * textureData->height;
		if (textureData->textureSize == textureDataSize)
			textureDataSize = textureData->dataSize;
		embeddedPalette = (uint16_t*)((uint8_t*)embeddedPalette + textureDataSize);
		if ((textureData->palette == embeddedPalette || textureData->paletteType != 0) &&
			sourcePalette == embeddedPalette)
			return destination;
	}

	if (destination->childCount != source->childCount)
		return NULL;
	childIndex = 0;
	childOffset = 0;
	if (source->childCount > 0) {
		do {
			result = OptModel_FindCorrespondingTextureNode(
				*(OptNode**)((uint8_t*)source->pChildren + childOffset),
				*(OptNode**)((uint8_t*)destination->pChildren + childOffset), sourcePalette);
			if (result != NULL)
				return result;
			childOffset += sizeof(*source->pChildren);
			++childIndex;
		} while (source->childCount > childIndex);
	}
	return NULL;
}

// FUNCTION: XVT 0x476670
OptNode* OptModel_FindCorrespondingTextureNodeInModel(OptimizedPolyObject* dstModel,
													  OptimizedPolyObject* srcModel,
													  const uint16_t* sourcePalette) {
	int rootOffset;
	int rootIndex;
	OptNode* result;

	rootOffset = 0;
	rootIndex = 0;
	if (srcModel->rootNodeCount > 0) {
		do {
			result = OptModel_FindCorrespondingTextureNode(
				*(OptNode**)((uint8_t*)srcModel->rootNodes + rootOffset),
				*(OptNode**)((uint8_t*)dstModel->rootNodes + rootOffset), sourcePalette);
			if (result != NULL)
				return result;
			rootOffset += sizeof(*srcModel->rootNodes);
			++rootIndex;
		} while (srcModel->rootNodeCount > rootIndex);
	}
	return NULL;
}

#ifndef XVT_MODERN
// FUNCTION: XVT 0x4766C0
void OptModel_SaveHandleToFile(const char* filename, uint16_t handle) {
	XvtFile* stream;
	OptimizedPolyObject* model;
	int rootIndex;
	int rootOffset;
	int fileVersion;
	size_t serializedSize;
	SceneMesh parentState;

	File_OpenGlobalStream(filename, "wb", 0, 1);
	stream = g_stream;
	if (stream != NULL) {
		model = Memory_LockHandle(handle);
		if (model->selfMarker != model) {
			OptModel_AdjustOptimizedPolyObjectPointers(model);
		}
		memset(&parentState, 0, sizeof(parentState));
		rootIndex = 0;
		g_modelNodeWalkUnusedScratch0 = NULL;
		g_modelNodeWalkUnusedScratch1 = NULL;
		g_curVertNormals = NULL;
		g_modelNodeWalkUnusedScratch2 = NULL;
		g_curMeshFlags = NULL;
		g_curVertexCount = 0;
		serializedSize = 4 * model->rootNodeCount + 14;
		if (model->rootNodeCount > 0) {
			rootOffset = 0;
			do {
				serializedSize += OptModel_GetSerializedNodeSize(
					*(OptNode**)((uint8_t*)model->rootNodes + rootOffset), &parentState);
				rootOffset += sizeof(*model->rootNodes);
				++rootIndex;
			} while (model->rootNodeCount > rootIndex);
		}
		fileVersion = 1;
		if (!g_optSourceIsVersion0) {
			fileVersion = 2;
		}
		fileVersion = -fileVersion;
		File_RawWrite(&fileVersion, 1, sizeof(fileVersion), stream);
		fileVersion = -fileVersion;
		File_RawWrite(&serializedSize, 1, sizeof(serializedSize), stream);
		File_RawWrite(model, 1, serializedSize, stream);
		File_RawClose(stream);
		Memory_UnlockHandle(handle);
	}
}
#endif

// FUNCTION: XVT 0x476810
unsigned int OptModel_GetSerializedNodeSize(OptNode* node, SceneMesh* parentState) {
	unsigned int serializedSize;
	int* paramData;
	OptNodeType nodeType;
	int childIndex;
	int childOffset;
	SceneMesh childState;

	if (node == NULL)
		return 0;

	serializedSize = sizeof(OptNode);

	if (node->pName != NULL)

		serializedSize = (unsigned int)strlen(node->pName) + sizeof(OptNode) + 1;

	paramData = node->param2;
	nodeType = node->nodeType;
	if (paramData != NULL) {
		switch (nodeType) {
			case OPT_FACEDATA:
			case OPT_FACEDATA_15:
			case OPT_FACEDATA_16:
			case OPT_FACEDATA_17:
				if (g_sceneEdgeFlagsCapacity < paramData[0])
					g_sceneEdgeFlagsCapacity = paramData[0];
				serializedSize += 4;
				serializedSize += (unsigned int)node->param1 << 6;
				serializedSize += 36 * node->param1;
				if (parentState->pVertNormals == NULL)
					serializedSize += 12 * g_curVertexCount;
				break;

			case OPT_TYPE_2:
				serializedSize += 48;
				break;

			case OPT_MESHVERTS:
				g_curVertexCount = node->param1;
				serializedSize += 12 * g_curVertexCount;
				if (g_vertexRemapCapacity < node->param1)
					g_vertexRemapCapacity = node->param1;
				break;

			case OPT_TYPE_4:
				serializedSize += 12;
				break;

			case OPT_TYPE_5:
				serializedSize += 36;
				break;

			case OPT_TYPE_6:
				serializedSize += 12;
				break;

			case OPT_NODEREF:
				serializedSize += (unsigned int)strlen((const char*)paramData) + 1;
				break;

			case OPT_TYPE_9: {
				int recordCount;
				int scaledRecordCount;

				recordCount = node->param1;
				g_curMeshFlags = node->param2;
				scaledRecordCount = recordCount << 3;
				scaledRecordCount -= recordCount;
				serializedSize += scaledRecordCount << 3;
				break;
			}

			case OPT_VERTNORMALS: {
				int vectorValueCount;

				vectorValueCount = 3 * node->param1;
				g_curVertNormals = node->param2;
				serializedSize += 4 * vectorValueCount;
				parentState->pVertNormals = (OptVector*)paramData;
				break;
			}

			case OPT_TEXCOORDS:
				serializedSize += 8 * node->param1;
				break;

			case OPT_TYPE_19:
				serializedSize += 12;
				break;

			case OPT_TEXTURE: {
				OptTextureData* textureData;
				int textureByteCount;
				uint8_t* embeddedPalette;

				textureData = node->param2;
				serializedSize += sizeof(*textureData);
				textureByteCount = textureData->height * textureData->width;
				if (textureByteCount == textureData->textureSize)
					serializedSize += textureData->dataSize;
				else
					serializedSize += textureByteCount;
				if (textureData->paletteType != 0) {
					serializedSize += 768 * textureData->paletteType;
				} else {
					embeddedPalette = (uint8_t*)(textureData + 1);
					if (textureByteCount == textureData->textureSize)
						embeddedPalette += textureData->dataSize;
					else
						embeddedPalette += textureByteCount;
					if ((uint8_t*)textureData->palette == embeddedPalette)
						serializedSize += 12288;
				}
				break;
			}

			case OPT_FACEGROUP:
				serializedSize += 4 * node->param1;
				break;

			case OPT_HARDPOINT:
				serializedSize += 16;
				break;

			case OPT_ROTSCALE:
				serializedSize += 48;
				break;

			case OPT_MESHDESC:
				serializedSize += 72;
				break;

			default:
				break;
		}
	} else if (nodeType == OPT_TEXTURE) {
		OptTextureData* textureData;
		int textureByteCount;
		uint8_t* embeddedPalette;

		textureData = node->param2;
		serializedSize += sizeof(*textureData);
		textureByteCount = textureData->height * textureData->width;
		if (textureByteCount == textureData->textureSize)
			serializedSize += textureData->dataSize;
		else
			serializedSize += textureByteCount;
		if (textureData->paletteType != 0) {
			serializedSize += 768 * textureData->paletteType;
		} else {
			embeddedPalette = (uint8_t*)(textureData + 1);
			if (textureByteCount == textureData->textureSize)
				embeddedPalette += textureData->dataSize;
			else
				embeddedPalette += textureByteCount;
			if ((uint8_t*)textureData->palette == embeddedPalette)
				serializedSize += 12288;
		}
	}

	if (node->childCount != 0) {
		childState = *parentState;
		g_modelNodeWalkUnusedScratch0 = NULL;
		g_modelNodeWalkUnusedScratch1 = NULL;
		g_curVertNormals = NULL;
		g_modelNodeWalkUnusedScratch2 = NULL;
		g_curMeshFlags = NULL;
		childIndex = 0;

		serializedSize += sizeof(OptNode*) * node->childCount;

		if (node->childCount > 0) {
			childOffset = 0;
			do {
				serializedSize += OptModel_GetSerializedNodeSize(
					*(OptNode**)((uint8_t*)node->pChildren + childOffset), &childState);
				childOffset += sizeof(*node->pChildren);
				++childIndex;
			} while (node->childCount > childIndex);
		}
	}
	return serializedSize;
}

// FUNCTION: XVT 0x476B90
void OptModel_PrepareTexturePalette(uint16_t* palette, int entryCount) {
	RgbTriplet srcRgb[4096];
	uint8_t* rgbCursor;
	uint16_t* paletteEntry;
	int entriesRemaining;
	unsigned int packedColor;
	uint8_t blue;
	uint8_t green;
	uint8_t red;

	if (g_useHardware3D != 0)
		ModelTexture_FilterHardwarePalette(palette);

	if (entryCount > 0) {
		rgbCursor = (uint8_t*)srcRgb;
		paletteEntry = palette;
		entriesRemaining = entryCount;
		do {
			packedColor = *paletteEntry++;
			blue = (uint8_t)(packedColor & 0x1Fu);
			packedColor >>= 5;
			rgbCursor[2] = (uint8_t)(2 * blue);
			green = (uint8_t)(packedColor & 0x3Fu);
			packedColor >>= 6;
			red = (uint8_t)(packedColor & 0x1Fu);
			rgbCursor[1] = green;
			rgbCursor[0] = (uint8_t)(2 * red);
			rgbCursor += 3;
		} while (--entriesRemaining != 0);
	}

	FlightPalette_Build16BppRange(srcRgb, palette, 0, entryCount);
}

// FUNCTION: XVT 0x476C20
unsigned int OptModel_BuildRuntimeNode(const OptNode* srcNode, SceneMesh* meshState, uint8_t* dst) {
	enum {
		OPT_TEXTURE_PALETTE_ENTRY_COUNT = 4096,
		OPT_TEXTURE_SUBPALETTE_COUNT = 16,
		OPT_TEXTURE_SUBPALETTE_ENTRY_COUNT = 256,
		OPT_TEXTURE_FULL_RES_THRESHOLD = 8,
		RGB565_GREEN_SHIFT = 5,
		RGB565_GREEN_BITS = 6,
		RGB565_GREEN_MASK = 0x3f,
		RGB565_BLUE_MASK = 0x1f,
	};

	unsigned int totalSize;
	unsigned int payloadSize;
	int childIndex;
	void* sourcePayload;
	SceneMesh childMesh;
	OptNode* runtimeNode;

	if (srcNode == NULL)
		return 0;
	if (dst != NULL) {
		memcpy(dst, srcNode, sizeof(*srcNode));
		runtimeNode = (OptNode*)dst;
		dst += sizeof(*srcNode);
	}
	totalSize = sizeof(*srcNode);
	if (srcNode->pName != NULL) {
		if (dst != NULL) {
			runtimeNode->pName = (char*)dst;
			strcpy((char*)dst, srcNode->pName);
			dst += strlen(srcNode->pName) + 1;
		}
		totalSize = (unsigned int)strlen(srcNode->pName) + sizeof(*srcNode) + 1;
	}
#ifdef XVT_MODERN
	totalSize = (unsigned int)XvtOpt_AlignSize(totalSize);
	if (dst)
		dst = XvtOpt_AlignPointer(dst);
#endif
	if (srcNode->childCount != 0) {
		if (dst != NULL) {
			runtimeNode->pChildren = (OptNode**)dst;
			dst += sizeof(*srcNode->pChildren) * (unsigned int)srcNode->childCount;
		}
		totalSize += sizeof(*srcNode->pChildren) * (unsigned int)srcNode->childCount;
	}

	sourcePayload = srcNode->param2;
	switch (srcNode->nodeType) {
		case OPT_FACEDATA:
		case OPT_FACEDATA_15:
		case OPT_FACEDATA_16:
		case OPT_FACEDATA_17:
			if (g_sceneEdgeFlagsCapacity < *(const int*)sourcePayload)
				g_sceneEdgeFlagsCapacity = *(const int*)sourcePayload;
			payloadSize = sizeof(int) + (unsigned int)srcNode->param1 * (sizeof(OptPackedFaceRecord) + 36u);
			if (meshState->pVertNormals == NULL)
				payloadSize += sizeof(OptVector) * (unsigned int)g_curVertexCount;
			if (dst != NULL) {
				runtimeNode->param2 = dst;
				memcpy(dst, srcNode->param2, payloadSize);
				dst += payloadSize;
			}
			totalSize += payloadSize;
			break;
		case OPT_TYPE_2:
			payloadSize = 48;
			if (dst != NULL) {
				runtimeNode->param2 = dst;
				memcpy(dst, srcNode->param2, payloadSize);
				dst += payloadSize;
			}
			totalSize += payloadSize;
			break;
		case OPT_MESHVERTS:
			payloadSize = sizeof(OptVector) * (unsigned int)srcNode->param1;
			if (dst != NULL) {
				runtimeNode->param2 = dst;
				memcpy(dst, srcNode->param2, payloadSize);
				dst += payloadSize;
			}
			totalSize += payloadSize;
			g_curVertexCount = srcNode->param1;
			if (srcNode->param1 > g_vertexRemapCapacity)
				g_vertexRemapCapacity = srcNode->param1;
			break;
		case OPT_TYPE_4:
			payloadSize = 12;
			if (dst != NULL) {
				runtimeNode->param2 = dst;
				memcpy(dst, srcNode->param2, payloadSize);
				dst += payloadSize;
			}
			totalSize += payloadSize;
			break;
		case OPT_TYPE_5:
			payloadSize = 36;
			if (dst != NULL) {
				runtimeNode->param2 = dst;
				memcpy(dst, srcNode->param2, payloadSize);
				dst += payloadSize;
			}
			totalSize += payloadSize;
			break;
		case OPT_TYPE_6:
			payloadSize = 12;
			if (dst != NULL) {
				runtimeNode->param2 = dst;
				memcpy(dst, srcNode->param2, payloadSize);
				dst += payloadSize;
			}
			totalSize += payloadSize;
			break;
		case OPT_NODEREF:
			payloadSize = (unsigned int)strlen((const char*)sourcePayload) + 1;
			if (dst != NULL) {
				runtimeNode->param2 = dst;
				memcpy(dst, srcNode->param2, payloadSize);
				dst += payloadSize;
			}
			totalSize += payloadSize;
			break;
		case OPT_TYPE_9:
			payloadSize = 56u * (unsigned int)srcNode->param1;
			if (dst != NULL) {
				runtimeNode->param2 = dst;
				memcpy(dst, srcNode->param2, payloadSize);
				dst += payloadSize;
			}
			g_curMeshFlags = sourcePayload;
			totalSize += payloadSize;
			break;
		case OPT_VERTNORMALS:
			payloadSize = sizeof(OptVector) * (unsigned int)srcNode->param1;
			if (dst != NULL) {
				runtimeNode->param2 = dst;
				memcpy(dst, srcNode->param2, payloadSize);
				dst += payloadSize;
			}
			g_curVertNormals = (OptVector*)sourcePayload;
			totalSize += payloadSize;
			meshState->pVertNormals = (OptVector*)sourcePayload;
			break;
		case OPT_TEXCOORDS:
			payloadSize = 8u * (unsigned int)srcNode->param1;
			if (dst != NULL) {
				runtimeNode->param2 = dst;
				memcpy(dst, srcNode->param2, payloadSize);
				dst += payloadSize;
			}
			totalSize += payloadSize;
			break;
		case OPT_TYPE_19:
			payloadSize = 12;
			if (dst != NULL) {
				runtimeNode->param2 = dst;
				memcpy(dst, srcNode->param2, payloadSize);
				dst += payloadSize;
			}
			totalSize += payloadSize;
			break;
		case OPT_TEXTURE: {
			const OptTextureData* sourceTexture;
			int paletteIndex;
			const uint8_t* sourcePalette;
			const uint16_t* sourcePalette16;
			OptTextureData* runtimeTexture;
			const uint8_t* sourceTexels;
			const uint8_t* mipTopRow;
			const uint8_t* mipBottomRow;
			const uint8_t* topTexel;
			const uint8_t* bottomTexel;
			uint8_t* mipRow;
			unsigned int texturePayloadSize;
			unsigned int sourcePaletteOffset;
			unsigned int paletteBytes;
			int width;
			int height;
			int mipPixelCount;
			int previousWidth;
			int rowStep;
			int mipX;
			int mipY;
			uint16_t packedColor;
			int red;
			int green;
			int blue;

			sourceTexture = (const OptTextureData*)srcNode->param2;
			if (dst != NULL && g_generateMissionPalette != 0 && g_flight16bppBytesPerPixel == 1) {
				sourceTexels = (const uint8_t*)sourceTexture + sizeof(*sourceTexture);
				sourcePalette = sourceTexels + sourceTexture->height * sourceTexture->width;
				if ((unsigned int)sourceTexture->textureSize ==
					(unsigned int)(sourceTexture->height * sourceTexture->width))
					sourcePalette = sourceTexels + sourceTexture->dataSize;
				sourcePalette16 = (const uint16_t*)(sourcePalette + OPT_TEXTURE_PALETTE_ENTRY_COUNT);
				for (paletteIndex = OPT_TEXTURE_SUBPALETTE_COUNT; paletteIndex != 0; --paletteIndex) {
					ImageQuantizer_ClassifyIndexed16BppImage(sourceTexels, sourcePalette16,
															 (unsigned int)sourceTexture->width,
															 (unsigned int)sourceTexture->height);
					sourcePalette16 += OPT_TEXTURE_SUBPALETTE_ENTRY_COUNT;
				}
			}

			if ((unsigned int)sourceTexture->textureSize ==
				(unsigned int)(sourceTexture->height * sourceTexture->width)) {
				if (dst != NULL) {
					runtimeNode->param2 = dst;
					memcpy(dst, srcNode->param2, sizeof(*sourceTexture));
					sourceTexels = (const uint8_t*)sourceTexture + sizeof(*sourceTexture);
					dst += sizeof(*sourceTexture);
					runtimeTexture = (OptTextureData*)runtimeNode->param2;
					if (g_keepFullResTextures == 0 &&
						runtimeTexture->width > OPT_TEXTURE_FULL_RES_THRESHOLD &&
						runtimeTexture->height > OPT_TEXTURE_FULL_RES_THRESHOLD) {
						sourceTexels += runtimeTexture->height * runtimeTexture->width;
						runtimeTexture->dataSize -= runtimeTexture->height * runtimeTexture->width;
						runtimeTexture->width >>= 1;
						runtimeTexture->height >>= 1;
						runtimeTexture->textureSize = runtimeTexture->width * runtimeTexture->height;
					}
					memcpy(dst, sourceTexels, (unsigned int)runtimeTexture->dataSize);
					dst += runtimeTexture->dataSize;
					texturePayloadSize = (unsigned int)runtimeTexture->dataSize + sizeof(*sourceTexture);
				} else {
					texturePayloadSize = (unsigned int)sourceTexture->dataSize + sizeof(*sourceTexture);
					if (g_keepFullResTextures == 0 && sourceTexture->width > OPT_TEXTURE_FULL_RES_THRESHOLD &&
						sourceTexture->height > OPT_TEXTURE_FULL_RES_THRESHOLD)
						texturePayloadSize -= (unsigned int)(sourceTexture->height * sourceTexture->width);
				}
			} else {
				texturePayloadSize =
					(unsigned int)(sourceTexture->height * sourceTexture->width) + sizeof(*sourceTexture);
				if (dst != NULL) {
					runtimeNode->param2 = dst;
					memcpy(dst, srcNode->param2, texturePayloadSize);
					dst += texturePayloadSize;
					sourceTexels = (const uint8_t*)sourceTexture + sizeof(*sourceTexture);
					if (sourceTexture->paletteType == 0)
						sourcePalette = (const uint8_t*)sourceTexture->palette;
					else
						sourcePalette = sourceTexels + sourceTexture->height * sourceTexture->width;
					sourcePalette16 = (const uint16_t*)(sourcePalette + 2 * OPT_TEXTURE_PALETTE_ENTRY_COUNT);
					width = sourceTexture->width;
					height = sourceTexture->height;
					runtimeTexture = (OptTextureData*)runtimeNode->param2;
					runtimeTexture->textureSize = width * height;
					runtimeTexture->dataSize = width * height;
					while (width > 1 && height > 1) {
						width >>= 1;
						height >>= 1;
						mipPixelCount = width * height;
						runtimeTexture->dataSize += mipPixelCount;
						if (g_mipmappingEnabled != 0 && height > 0) {
							previousWidth = 2 * width;
							rowStep = 4 * width;
							mipRow = dst;
							mipTopRow = sourceTexels;
							mipBottomRow = sourceTexels + previousWidth;
							for (mipY = 0; mipY < height; ++mipY) {
								topTexel = mipTopRow;
								bottomTexel = mipBottomRow;
								for (mipX = 0; mipX < width; ++mipX) {
									packedColor = sourcePalette16[topTexel[0]];
									blue = packedColor & RGB565_BLUE_MASK;
									packedColor >>= RGB565_GREEN_SHIFT;
									green = packedColor & RGB565_GREEN_MASK;
									packedColor >>= RGB565_GREEN_BITS;
									red = packedColor & RGB565_BLUE_MASK;
									packedColor = sourcePalette16[topTexel[1]];
									blue += packedColor & RGB565_BLUE_MASK;
									packedColor >>= RGB565_GREEN_SHIFT;
									green += packedColor & RGB565_GREEN_MASK;
									packedColor >>= RGB565_GREEN_BITS;
									red += packedColor & RGB565_BLUE_MASK;
									packedColor = sourcePalette16[bottomTexel[0]];
									blue += packedColor & RGB565_BLUE_MASK;
									packedColor >>= RGB565_GREEN_SHIFT;
									green += packedColor & RGB565_GREEN_MASK;
									packedColor >>= RGB565_GREEN_BITS;
									red += packedColor & RGB565_BLUE_MASK;
									packedColor = sourcePalette16[bottomTexel[1]];
									blue += packedColor & RGB565_BLUE_MASK;
									packedColor >>= RGB565_GREEN_SHIFT;
									green += packedColor & RGB565_GREEN_MASK;
									packedColor >>= RGB565_GREEN_BITS;
									red += packedColor & RGB565_BLUE_MASK;
									blue >>= 2;
									green >>= 2;
									red >>= 2;
									mipRow[mipX] =
										Color_FindNearestRgb565Index(sourcePalette16, red, green, blue, 0,
																	 OPT_TEXTURE_SUBPALETTE_ENTRY_COUNT);
									topTexel += 2;
									bottomTexel += 2;
								}
								mipRow += width;
								mipTopRow += rowStep;
								mipBottomRow += rowStep;
							}
						}
						sourceTexels = dst;
						dst += mipPixelCount;
						texturePayloadSize += (unsigned int)mipPixelCount;
					}
				} else {
					width = sourceTexture->width;
					height = sourceTexture->height;
					while (width > 1 && height > 1) {
						width >>= 1;
						height >>= 1;
						texturePayloadSize += (unsigned int)(width * height);
					}
				}
			}

			sourceTexture = (const OptTextureData*)srcNode->param2;
			if (sourceTexture->paletteType != 0) {
				texturePayloadSize +=
					(unsigned int)(sourceTexture->paletteType * g_flight16bppBytesPerPixel) *
					OPT_TEXTURE_SUBPALETTE_ENTRY_COUNT;
				if (dst != NULL) {
					sourcePalette = (const uint8_t*)sourceTexture + sizeof(*sourceTexture);
					sourcePaletteOffset = (unsigned int)(sourceTexture->height * sourceTexture->width);
					if ((unsigned int)sourceTexture->textureSize == sourcePaletteOffset)
						sourcePaletteOffset = (unsigned int)sourceTexture->dataSize;
					sourcePalette += sourcePaletteOffset;
					if (g_flight16bppBytesPerPixel == 2)
						sourcePalette +=
							(unsigned int)sourceTexture->paletteType * OPT_TEXTURE_SUBPALETTE_ENTRY_COUNT;
					if (g_flight16bppBytesPerPixel == 1) {
						sourcePalette16 = (const uint16_t*)(sourcePalette + OPT_TEXTURE_PALETTE_ENTRY_COUNT);
						for (paletteIndex = 0; paletteIndex < OPT_TEXTURE_PALETTE_ENTRY_COUNT;
							 ++paletteIndex) {
							dst[paletteIndex] =
								g_activeRgb565ToPaletteIndexLut[sourcePalette16[paletteIndex]];
						}
					} else {
						memcpy(dst, sourcePalette,
							   (unsigned int)(g_flight16bppBytesPerPixel * OPT_TEXTURE_PALETTE_ENTRY_COUNT));
					}
					if (g_flight16bppBytesPerPixel == 2) {
						if (srcNode->pName != NULL)
							DebugPrintf("%s:", srcNode->pName);
						OptModel_PrepareTexturePalette((uint16_t*)dst, OPT_TEXTURE_PALETTE_ENTRY_COUNT);
					}
					dst += (unsigned int)(sourceTexture->paletteType * g_flight16bppBytesPerPixel) *
						   OPT_TEXTURE_SUBPALETTE_ENTRY_COUNT;
				}
			} else {
				sourcePalette = (const uint8_t*)sourceTexture + sizeof(*sourceTexture);
				sourcePaletteOffset = (unsigned int)(sourceTexture->height * sourceTexture->width);
				if ((unsigned int)sourceTexture->textureSize == sourcePaletteOffset)
					sourcePaletteOffset = (unsigned int)sourceTexture->dataSize;
				sourcePalette += sourcePaletteOffset;
				if ((const uint8_t*)sourceTexture->palette == sourcePalette) {
					paletteBytes = (unsigned int)g_flight16bppBytesPerPixel * OPT_TEXTURE_PALETTE_ENTRY_COUNT;
					texturePayloadSize += paletteBytes;
					if (dst != NULL) {
						if (g_flight16bppBytesPerPixel == 2)
							sourcePalette += OPT_TEXTURE_PALETTE_ENTRY_COUNT;
						if (g_flight16bppBytesPerPixel == 1) {
							sourcePalette16 =
								(const uint16_t*)(sourcePalette + OPT_TEXTURE_PALETTE_ENTRY_COUNT);
							for (paletteIndex = 0; paletteIndex < OPT_TEXTURE_PALETTE_ENTRY_COUNT;
								 ++paletteIndex) {
								dst[paletteIndex] =
									g_activeRgb565ToPaletteIndexLut[sourcePalette16[paletteIndex]];
							}
						} else {
							memcpy(dst, sourcePalette, paletteBytes);
						}
						if (g_flight16bppBytesPerPixel == 2) {
							if (srcNode->pName != NULL)
								DebugPrintf("%s:", srcNode->pName);
							OptModel_PrepareTexturePalette((uint16_t*)dst, OPT_TEXTURE_PALETTE_ENTRY_COUNT);
						}
						dst += (unsigned int)g_flight16bppBytesPerPixel * OPT_TEXTURE_PALETTE_ENTRY_COUNT;
					}
				}
			}
			totalSize += texturePayloadSize;
			break;
		}
		case OPT_FACEGROUP:
			payloadSize = 4u * (unsigned int)srcNode->param1;
			if (dst != NULL) {
				runtimeNode->param2 = dst;
				memcpy(dst, srcNode->param2, payloadSize);
				dst += payloadSize;
			}
			totalSize += payloadSize;
			break;
		case OPT_HARDPOINT:
			payloadSize = 16;
			if (dst != NULL) {
				runtimeNode->param2 = dst;
				memcpy(dst, srcNode->param2, payloadSize);
				dst += payloadSize;
			}
			totalSize += payloadSize;
			break;
		case OPT_ROTSCALE:
			payloadSize = 48;
			if (dst != NULL) {
				runtimeNode->param2 = dst;
				memcpy(dst, srcNode->param2, payloadSize);
				dst += payloadSize;
			}
			totalSize += payloadSize;
			break;
		case OPT_MESHDESC:
			payloadSize = 72;
			if (dst != NULL) {
				runtimeNode->param2 = dst;
				memcpy(dst, srcNode->param2, payloadSize);
				dst += payloadSize;
			}
			totalSize += payloadSize;
			break;
		default:
			break;
	}
#ifdef XVT_MODERN
	totalSize = (unsigned int)XvtOpt_AlignSize(totalSize);
	if (dst)
		dst = XvtOpt_AlignPointer(dst);
#endif
	if (srcNode->childCount != 0) {
		childMesh = *meshState;
		g_modelNodeWalkUnusedScratch0 = NULL;
		g_modelNodeWalkUnusedScratch1 = NULL;
		g_curVertNormals = NULL;
		g_modelNodeWalkUnusedScratch2 = NULL;
		g_curMeshFlags = NULL;
		for (childIndex = 0; childIndex < srcNode->childCount; ++childIndex) {
			if (dst != NULL)
				((OptNode**)runtimeNode->pChildren)[childIndex] = (OptNode*)dst;
			payloadSize = OptModel_BuildRuntimeNode(srcNode->pChildren[childIndex], &childMesh, dst);
			if (dst != NULL) {
				if (payloadSize == 0)
					((OptNode**)runtimeNode->pChildren)[childIndex] = NULL;
				dst += payloadSize;
			}
			totalSize += payloadSize;
		}
	}
#ifdef XVT_MODERN
	return (unsigned int)XvtOpt_AlignSize(totalSize);
#else
	return totalSize;
#endif
}

#ifndef XVT_MODERN
// FUNCTION: XVT 0x477950
uint16_t OptModel_ConvertImportedHandleToPacked(uint16_t sourceHandle) {
	OptimizedPolyObject* sourceModel;
	OptimizedPolyObject* packedModel;
	OptimizedPolyObject* finalModel;
	uint8_t* packedCursor;
	void* packedStorage;
	uint16_t packedHandle;
	uint16_t scratchHandle;
	uint16_t finalHandle;
	size_t allocatedSize;
	size_t packedSize;
	int rootIndex;
	SceneMesh conversionState;

	memset(&conversionState, 0, sizeof(conversionState));
	conversionState.viewOrient[0] = 1.0f;
	conversionState.viewOrient[1] = 0.0f;
	conversionState.viewOrient[2] = 0.0f;
	conversionState.viewOrient[3] = 0.0f;
	conversionState.viewOrient[4] = 1.0f;
	conversionState.viewOrient[5] = 0.0f;
	conversionState.viewOrient[6] = 0.0f;
	conversionState.viewOrient[7] = 0.0f;
	conversionState.viewOrient[8] = 1.0f;
	conversionState.orient[0] = 1.0f;
	conversionState.orient[1] = 0.0f;
	conversionState.orient[2] = 0.0f;
	conversionState.orient[3] = 0.0f;
	conversionState.orient[4] = 1.0f;
	conversionState.orient[5] = 0.0f;
	conversionState.orient[6] = 0.0f;
	conversionState.orient[7] = 0.0f;
	conversionState.orient[8] = 1.0f;
	sourceModel = Memory_LockHandle(sourceHandle);
	if (sourceModel->selfMarker != sourceModel)
		OptModel_RelocateLoadedPointers(sourceModel);
	g_modelNodeWalkUnusedScratch0 = NULL;
	g_modelNodeWalkUnusedScratch1 = NULL;
	g_curVertNormals = NULL;
	g_modelNodeWalkUnusedScratch2 = NULL;
	g_curMeshFlags = NULL;
	g_curVertexCount = 0;
	g_optImportScratchVectorCount = 1;
	allocatedSize = sizeof(*sourceModel) + sizeof(*sourceModel->rootNodes) * sourceModel->rootNodeCount;
	for (rootIndex = 0; rootIndex < sourceModel->rootNodeCount; ++rootIndex) {
		allocatedSize += OptModel_CalculatePackedNodeSizeRecursive(
			sourceModel, sourceModel->rootNodes[rootIndex], &conversionState);
	}
	Memory_UnlockHandle(sourceHandle);

	packedHandle = Memory_AllocHandle(allocatedSize, 0);
	scratchHandle = Memory_AllocHandle(sizeof(*g_optImportScratchVectors) * g_optImportScratchVectorCount, 0);
	sourceModel = Memory_LockHandle(sourceHandle);
	if (sourceModel->selfMarker != sourceModel)
		OptModel_RelocateLoadedPointers(sourceModel);
	packedModel = Memory_LockHandle(packedHandle);
	g_optImportScratchVectors = Memory_LockHandle(scratchHandle);
	packedModel->selfMarker = packedModel;
	packedModel->reserved = packedHandle;
	packedModel->rootNodeCount = sourceModel->rootNodeCount;
	packedModel->rootNodes = (OptNode**)((uint8_t*)packedModel + sizeof(*packedModel));
	packedCursor = (uint8_t*)packedModel + sizeof(*packedModel) +
				   sizeof(*packedModel->rootNodes) * packedModel->rootNodeCount;
	packedSize = sizeof(*packedModel) + sizeof(*packedModel->rootNodes) * packedModel->rootNodeCount;

	memset(&conversionState, 0, sizeof(conversionState));
	conversionState.viewOrient[0] = 1.0f;
	conversionState.viewOrient[1] = 0.0f;
	conversionState.viewOrient[2] = 0.0f;
	conversionState.viewOrient[3] = 0.0f;
	conversionState.viewOrient[4] = 1.0f;
	conversionState.viewOrient[5] = 0.0f;
	conversionState.viewOrient[6] = 0.0f;
	conversionState.viewOrient[7] = 0.0f;
	conversionState.viewOrient[8] = 1.0f;
	conversionState.orient[0] = 1.0f;
	conversionState.orient[1] = 0.0f;
	conversionState.orient[2] = 0.0f;
	conversionState.orient[3] = 0.0f;
	conversionState.orient[4] = 1.0f;
	conversionState.orient[5] = 0.0f;
	conversionState.orient[6] = 0.0f;
	conversionState.orient[7] = 0.0f;
	conversionState.orient[8] = 1.0f;
	g_modelNodeWalkUnusedScratch0 = NULL;
	g_modelNodeWalkUnusedScratch1 = NULL;
	g_curVertNormals = NULL;
	g_modelNodeWalkUnusedScratch2 = NULL;
	g_curMeshFlags = NULL;
	g_curVertexCount = 0;
	for (rootIndex = 0; rootIndex < sourceModel->rootNodeCount; ++rootIndex) {
		size_t nodeSize;
		packedModel->rootNodes[rootIndex] = (OptNode*)packedCursor;
		nodeSize = OptModel_ConvertImportedNodeToPackedRecursive(
			sourceModel, sourceModel->rootNodes[rootIndex], &conversionState, packedCursor);
		packedCursor += nodeSize;
		packedSize += nodeSize;
	}

	Memory_UnlockHandle(sourceHandle);
	Memory_UnlockHandle(packedHandle);
	Memory_UnlockHandle(scratchHandle);
	Memory_FreeHandle(sourceHandle);
	Memory_FreeHandle(scratchHandle);
	finalHandle = Memory_AllocHandle(packedSize, 0);
	packedStorage = Memory_LockHandle(packedHandle);
	finalModel = Memory_LockHandle(finalHandle);
	memcpy(finalModel, packedStorage, packedSize);
	OptModel_AdjustOptimizedPolyObjectPointers(finalModel);
	Memory_UnlockHandle(finalHandle);
	Memory_UnlockHandle(packedHandle);
	Memory_FreeHandle(packedHandle);
	return finalHandle;
}

// FUNCTION: XVT 0x477C80
size_t OptModel_ConvertImportedNodeToPackedRecursive(const OptimizedPolyObject* sourceModel,
													 const OptNode* sourceNode, void* conversionState,
													 uint8_t* destBuffer) {
	OptNode* packedNode;
	OptLegacyParamRecord* params;
	OptNodeType nodeType;
	uint8_t* dest;
	int childIndex;
	SceneMesh childState;

	if (sourceNode == NULL)
		return 0;

	nodeType = sourceNode->nodeType;
	packedNode = (OptNode*)destBuffer;
	dest = destBuffer + sizeof(*packedNode);
	packedNode->nodeType = nodeType;
	if (sourceNode->pName != NULL) {
		packedNode->pName = (char*)dest;
		strcpy((char*)dest, sourceNode->pName);
		dest += strlen(sourceNode->pName) + 1;
	} else {
		packedNode->pName = NULL;
	}
	packedNode->param1 = 1;
	packedNode->param2 = dest;
	packedNode->childCount = 0;
	packedNode->pChildren = NULL;

	params = sourceNode->param2;
	if (params != NULL && sourceNode->param1 != 0 && params->data != NULL) {
		switch (nodeType) {
			case OPT_FACEDATA: {
				OptPackedFaceNode* faceNode = (OptPackedFaceNode*)packedNode;
				OptPackedFaceData* faceData = (OptPackedFaceData*)dest;
				const int* vertexIndices = params[0].data;
				const int* normalIndices = NULL;
				const int* texCoordIndices = NULL;
				int dataIndex = 0;
				int edgeCount = 0;

				faceNode->faceCount = 0;
				dest += sizeof(faceData->edgeCount);
				if (params[3].value1 == params[0].value1)
					texCoordIndices = params[3].data;
				if (params[2].value1 == params[0].value1)
					normalIndices = params[2].data;
				while (dataIndex < params[0].value1) {
					int polygonStart = dataIndex;
					int scanIndex = dataIndex + 1;
					while (1) {
						OptPackedFaceRecord* face = &faceData->records[faceNode->faceCount];
						int edgeIndex;

						scanIndex += 2;
						face->vertexIndices[0] = vertexIndices[polygonStart];
						face->vertexIndices[1] = vertexIndices[scanIndex - 2];
						face->vertexIndices[2] = vertexIndices[scanIndex - 1];
						face->vertexIndices[3] = vertexIndices[scanIndex];
						if (texCoordIndices != NULL) {
							face->texCoordIndices[0] = texCoordIndices[polygonStart];
							face->texCoordIndices[1] = texCoordIndices[scanIndex - 2];
							face->texCoordIndices[2] = texCoordIndices[scanIndex - 1];
							face->texCoordIndices[3] = texCoordIndices[scanIndex];
						} else {
							face->texCoordIndices[0] = face->vertexIndices[0];
							face->texCoordIndices[1] = face->vertexIndices[1];
							face->texCoordIndices[2] = face->vertexIndices[2];
							face->texCoordIndices[3] = face->vertexIndices[3];
						}
						if (normalIndices != NULL) {
							face->normalIndices[0] = normalIndices[polygonStart];
							face->normalIndices[1] = normalIndices[scanIndex - 2];
							face->normalIndices[2] = normalIndices[scanIndex - 1];
							face->normalIndices[3] = normalIndices[scanIndex];
						} else {
							face->normalIndices[0] = face->vertexIndices[0];
							face->normalIndices[1] = face->vertexIndices[1];
							face->normalIndices[2] = face->vertexIndices[2];
							face->normalIndices[3] = face->vertexIndices[3];
						}

						edgeIndex = OptModel_FindUniqueEdgeIndex(faceNode, face->vertexIndices[0],
																 face->vertexIndices[1]);
						face->edgeIndices[0] = edgeIndex;
						if (edgeIndex == -1)
							face->edgeIndices[0] = edgeCount++;
						edgeIndex = OptModel_FindUniqueEdgeIndex(faceNode, face->vertexIndices[1],
																 face->vertexIndices[2]);
						face->edgeIndices[1] = edgeIndex;
						if (edgeIndex == -1)
							face->edgeIndices[1] = edgeCount++;
						if (face->vertexIndices[3] == -1) {
							edgeIndex = OptModel_FindUniqueEdgeIndex(faceNode, face->vertexIndices[2],
																	 face->vertexIndices[0]);
							face->edgeIndices[2] = edgeIndex;
							if (edgeIndex == -1)
								face->edgeIndices[2] = edgeCount++;
							face->edgeIndices[3] = -1;
						} else {
							edgeIndex = OptModel_FindUniqueEdgeIndex(faceNode, face->vertexIndices[2],
																	 face->vertexIndices[3]);
							face->edgeIndices[2] = edgeIndex;
							if (edgeIndex == -1)
								face->edgeIndices[2] = edgeCount++;
							edgeIndex = OptModel_FindUniqueEdgeIndex(faceNode, face->vertexIndices[3],
																	 face->vertexIndices[0]);
							face->edgeIndices[3] = edgeIndex;
							if (edgeIndex == -1)
								face->edgeIndices[3] = edgeCount++;
						}
						++faceNode->faceCount;
						if (vertexIndices[scanIndex] == -1) {
							dataIndex = scanIndex + 1;
							break;
						}
						if (vertexIndices[scanIndex + 1] == -1) {
							dataIndex = scanIndex + 2;
							break;
						}
					}
				}
				dest = (uint8_t*)OptModel_AppendPackedFaceDerivedData(faceNode, dest, conversionState);
				faceData->edgeCount = edgeCount;
				if (edgeCount > g_sceneEdgeFlagsCapacity)
					g_sceneEdgeFlagsCapacity = edgeCount;
				break;
			}

			case OPT_TYPE_2: {
				float* transform = (float*)dest;
				const OptVector* pivot = params[4].data;
				float pivotX;
				float pivotY;
				float pivotZ;
				dest += 12 * sizeof(float);
				if (pivot != NULL) {
					transform[0] = -pivot->x;
					pivotX = transform[0];
					transform[1] = -pivot->y;
					pivotY = transform[1];
					transform[2] = -pivot->z;
					pivotZ = transform[2];
					if (params[3].data != NULL) {
						const OptVector* scale;
						Math3D_BuildAxisAngleMatrix(&transform[3], params[3].data);
						Math3D_RotateVec3(transform, &transform[3]);
						transform[0] -= pivotX;
						transform[1] -= pivotY;
						transform[2] -= pivotZ;
						scale = params[2].data;
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
								const OptVector* translation;
								transform[0] += pivotX;
								transform[1] += pivotY;
								transform[2] += pivotZ;
								Math3D_BuildAxisAngleMatrix(&transform[3], params[1].data);
								Math3D_RotateVec3(transform, &transform[3]);
								transform[0] -= pivotX;
								transform[1] -= pivotY;
								transform[2] -= pivotZ;
								translation = params[0].data;
								if (translation != NULL) {
									transform[0] -= translation->x;
									transform[1] -= translation->y;
									transform[2] -= translation->z;
								}
							}
						}
					}
				}
				break;
			}

			case OPT_MESHVERTS:
				packedNode->param1 = params[0].value1;
				memcpy(dest, params[0].data, sizeof(OptVector) * (size_t)packedNode->param1);
				g_modelNodeWalkUnusedScratch0 = dest;
				g_curVertexCount = packedNode->param1;
				dest += sizeof(OptVector) * (size_t)g_curVertexCount;
				if (packedNode->param1 > g_vertexRemapCapacity)
					g_vertexRemapCapacity = packedNode->param1;
				break;

			case OPT_TYPE_4:
				*(OptVector*)dest = *(const OptVector*)params[0].data;
				dest += sizeof(OptVector);
				break;

			case OPT_TYPE_5: {
				float* matrix = (float*)dest;
				dest += 9 * sizeof(float);
				Math3D_BuildAxisAngleMatrix(matrix, params[0].data);
				break;
			}

			case OPT_TYPE_6:
				*(OptVector*)dest = *(const OptVector*)params[0].data;
				dest += sizeof(OptVector);
				break;

			case OPT_NODEREF: {
				const char* nodeName = params[0].data;
				packedNode->param1 = 1;
				packedNode->param2 = dest;
				strcpy((char*)dest, nodeName);
				dest += strlen(nodeName) + 1;
				break;
			}

			case OPT_TYPE_9: {
				int maxRecordCount = params[0].value1;
				const float* sourceValues;
				float* destValues;
				int fillCount;
				if (maxRecordCount < params[1].value1)
					maxRecordCount = params[1].value1;
				if (maxRecordCount < params[2].value1)
					maxRecordCount = params[2].value1;
				if (maxRecordCount < params[3].value1)
					maxRecordCount = params[3].value1;
				if (maxRecordCount < params[4].value1)
					maxRecordCount = params[4].value1;
				if (maxRecordCount < params[5].value1)
					maxRecordCount = params[5].value1;
				packedNode->param1 = maxRecordCount;

				memcpy(dest, params[0].data, sizeof(OptVector) * (size_t)params[0].value1);
				destValues = (float*)dest + 3 * params[0].value1;
				dest += sizeof(OptVector) * (size_t)maxRecordCount;
				if (params[0].value1 < maxRecordCount) {
					sourceValues = (const float*)params[0].data + 3 * params[0].value1 - 3;
					fillCount = maxRecordCount - params[0].value1;
					do {
						destValues[0] = sourceValues[0];
						destValues[1] = sourceValues[1];
						destValues[2] = sourceValues[2];
						destValues += 3;
					} while (--fillCount != 0);
				}
				if (params[1].data != NULL) {
					memcpy(dest, params[1].data, sizeof(OptVector) * (size_t)params[1].value1);
					destValues = (float*)dest + 3 * params[1].value1;
					dest += sizeof(OptVector) * (size_t)maxRecordCount;
					if (params[1].value1 < maxRecordCount) {
						sourceValues = (const float*)params[1].data + 3 * params[1].value1 - 3;
						fillCount = maxRecordCount - params[1].value1;
						do {
							destValues[0] = sourceValues[0];
							destValues[1] = sourceValues[1];
							destValues[2] = sourceValues[2];
							destValues += 3;
						} while (--fillCount != 0);
					}
					if (params[2].data != NULL) {
						memcpy(dest, params[2].data, sizeof(OptVector) * (size_t)params[2].value1);
						destValues = (float*)dest + 3 * params[2].value1;
						dest += sizeof(OptVector) * (size_t)maxRecordCount;
						if (params[2].value1 < maxRecordCount) {
							sourceValues = (const float*)params[2].data + 3 * params[2].value1 - 3;
							fillCount = maxRecordCount - params[2].value1;
							do {
								destValues[0] = sourceValues[0];
								destValues[1] = sourceValues[1];
								destValues[2] = sourceValues[2];
								destValues += 3;
							} while (--fillCount != 0);
						}
						if (params[3].data != NULL) {
							memcpy(dest, params[3].data, sizeof(OptVector) * (size_t)params[3].value1);
							destValues = (float*)dest + 3 * params[3].value1;
							dest += sizeof(OptVector) * (size_t)maxRecordCount;
							if (params[3].value1 < maxRecordCount) {
								sourceValues = (const float*)params[3].data + 3 * params[3].value1 - 3;
								fillCount = maxRecordCount - params[3].value1;
								do {
									destValues[0] = sourceValues[0];
									destValues[1] = sourceValues[1];
									destValues[2] = sourceValues[2];
									destValues += 3;
								} while (--fillCount != 0);
							}
							if (params[4].data != NULL) {
								memcpy(dest, params[4].data, sizeof(float) * (size_t)params[4].value1);
								destValues = (float*)dest + params[4].value1;
								dest += sizeof(float) * (size_t)maxRecordCount;
								if (params[4].value1 < maxRecordCount) {
									sourceValues = (const float*)params[4].data + params[4].value1 - 1;
									fillCount = maxRecordCount - params[4].value1;
									do {
										*destValues++ = *sourceValues;
									} while (--fillCount != 0);
								}
								if (params[5].data != NULL) {
									memcpy(dest, params[5].data, sizeof(float) * (size_t)params[5].value1);
									destValues = (float*)dest + params[5].value1;
									dest += sizeof(float) * (size_t)maxRecordCount;
									if (params[5].value1 < maxRecordCount) {
										sourceValues = (const float*)params[5].data + params[5].value1 - 1;
										fillCount = maxRecordCount - params[5].value1;
										do {
											*destValues++ = *sourceValues;
										} while (--fillCount != 0);
									}
								}
							}
						}
					}
				}
				break;
			}

			case OPT_TYPE_10:
			case OPT_TYPE_14:
				packedNode->param1 = *(const int*)params[0].data;
				break;

			case OPT_VERTNORMALS:
				packedNode->param1 = params[0].value1;
				memcpy(dest, params[0].data, sizeof(OptVector) * (size_t)packedNode->param1);
				g_curVertNormals = (OptVector*)dest;
				((SceneMesh*)conversionState)->pVertNormals = (OptVector*)dest;
				dest += sizeof(OptVector) * (size_t)packedNode->param1;
				break;

			case OPT_TEXCOORDS:
				packedNode->param1 = params[0].value1;
				memcpy(dest, params[0].data, sizeof(OptTexCoord) * (size_t)packedNode->param1);
				g_modelNodeWalkUnusedScratch1 = dest;
				dest += sizeof(OptTexCoord) * (size_t)packedNode->param1;
				break;

			case OPT_FACEDATA_15: {
				OptPackedFaceNode* faceNode = (OptPackedFaceNode*)packedNode;
				OptPackedFaceData* faceData = (OptPackedFaceData*)dest;
				const int* widthData = params[1].data;
				const int* heightData = params[2].data;
				int edgeCount = 0;
				int rowIndex;
				dest += sizeof(faceData->edgeCount);
				if (widthData != NULL && heightData != NULL) {
					int width = *widthData;
					int height = *heightData;
					int firstVertex = *(const int*)params[0].data;
					for (rowIndex = 0; rowIndex < height - 1; ++rowIndex) {
						int columnIndex;
						for (columnIndex = 0; columnIndex < width - 1; ++columnIndex) {
							OptPackedFaceRecord* face =
								&faceData->records[rowIndex * (width - 1) + columnIndex];
							face->vertexIndices[0] = firstVertex + rowIndex * width + columnIndex;
							face->vertexIndices[1] = face->vertexIndices[0] + 1;
							face->vertexIndices[2] = face->vertexIndices[1] + width;
							face->vertexIndices[3] = face->vertexIndices[0] + width;
							face->texCoordIndices[0] = face->vertexIndices[0];
							face->texCoordIndices[1] = face->vertexIndices[1];
							face->texCoordIndices[2] = face->vertexIndices[2];
							face->texCoordIndices[3] = face->vertexIndices[3];
							face->normalIndices[0] = face->vertexIndices[0];
							face->normalIndices[1] = face->vertexIndices[1];
							face->normalIndices[2] = face->vertexIndices[2];
							face->normalIndices[3] = face->vertexIndices[3];
							if (rowIndex == 0) {
								face->edgeIndices[0] = edgeCount++;
							} else if (rowIndex == 1) {
								face->edgeIndices[0] = columnIndex + edgeCount - 3 * width + 4;
							} else {
								face->edgeIndices[0] = edgeCount - 2 * width;
							}
							face->edgeIndices[1] = edgeCount++;
							face->edgeIndices[2] = edgeCount++;
							if (columnIndex == 0) {
								face->edgeIndices[3] = edgeCount++;
							} else {
								face->edgeIndices[3] = edgeCount - 4;
								if (rowIndex == 0)
									--face->edgeIndices[3];
								if (columnIndex == 1)
									--face->edgeIndices[3];
							}
						}
					}
					faceNode->faceCount = (height - 1) * (width - 1);
					dest = (uint8_t*)OptModel_AppendPackedFaceDerivedData(faceNode, dest, conversionState);
					faceData->edgeCount = edgeCount;
					if (edgeCount > g_sceneEdgeFlagsCapacity)
						g_sceneEdgeFlagsCapacity = edgeCount;
				}
				break;
			}

			case OPT_FACEDATA_16: {
				OptPackedFaceNode* faceNode = (OptPackedFaceNode*)packedNode;
				OptPackedFaceData* faceData = (OptPackedFaceData*)dest;
				const int* polygonVertexCounts = params[0].data;
				int vertexCursor = 0;
				int edgeCursor = 0;
				int polygonIndex;
				dest += sizeof(faceData->edgeCount);
				faceNode->faceCount = 0;
				for (polygonIndex = 0; polygonIndex < params[0].value1; ++polygonIndex) {
					int polygonVertexCount = polygonVertexCounts[polygonIndex];
					if (polygonVertexCount >= 3) {
						int polygonStart = vertexCursor;
						int firstEdge = edgeCursor;
						++vertexCursor;
						++edgeCursor;
						while (vertexCursor - polygonStart < polygonVertexCount) {
							OptPackedFaceRecord* face = &faceData->records[faceNode->faceCount];
							++faceNode->faceCount;
							face->vertexIndices[0] = polygonStart;
							face->vertexIndices[1] = vertexCursor;
							face->vertexIndices[2] = ++vertexCursor;
							++vertexCursor;
							if (vertexCursor - polygonVertexCount == polygonStart) {
								face->vertexIndices[3] = -1;
								face->edgeIndices[0] = firstEdge;
								face->edgeIndices[1] = edgeCursor;
								face->edgeIndices[2] = ++edgeCursor;
								++edgeCursor;
								face->edgeIndices[3] = -1;
							} else {
								face->vertexIndices[3] = vertexCursor;
								if (++vertexCursor - polygonVertexCount != polygonStart)
									--vertexCursor;
								face->edgeIndices[0] = firstEdge;
								face->edgeIndices[1] = edgeCursor;
								face->edgeIndices[2] = ++edgeCursor;
								firstEdge = ++edgeCursor;
								face->edgeIndices[3] = firstEdge;
								++edgeCursor;
							}
							face->texCoordIndices[0] = face->vertexIndices[0];
							face->texCoordIndices[1] = face->vertexIndices[1];
							face->texCoordIndices[2] = face->vertexIndices[2];
							face->texCoordIndices[3] = face->vertexIndices[3];
							face->normalIndices[0] = face->vertexIndices[0];
							face->normalIndices[1] = face->vertexIndices[1];
							face->normalIndices[2] = face->vertexIndices[2];
							face->normalIndices[3] = face->vertexIndices[3];
						}
					}
				}
				dest = (uint8_t*)OptModel_AppendPackedFaceDerivedData(faceNode, dest, conversionState);
				faceData->edgeCount = edgeCursor;
				if (edgeCursor > g_sceneEdgeFlagsCapacity)
					g_sceneEdgeFlagsCapacity = edgeCursor;
				break;
			}

			case OPT_FACEDATA_17: {
				OptPackedFaceNode* faceNode = (OptPackedFaceNode*)packedNode;
				OptPackedFaceData* faceData = (OptPackedFaceData*)dest;
				const int* stripVertexCounts = params[1].data;
				int firstVertex = *(const int*)params[0].data;
				int edgeCursor = *(const int*)conversionState;
				int stripIndex;
				dest += sizeof(faceData->edgeCount);
				if (stripVertexCounts != NULL) {
					faceNode->faceCount = 0;
					for (stripIndex = 0; stripIndex < params[1].value1; ++stripIndex) {
						int stripVertexCount = stripVertexCounts[stripIndex];
						int vertexIndex;
						for (vertexIndex = 2; vertexIndex < stripVertexCount; ++vertexIndex) {
							OptPackedFaceRecord* face = &faceData->records[faceNode->faceCount];
							int currentVertex = firstVertex + vertexIndex;
							if ((vertexIndex & 1) != 0) {
								face->vertexIndices[0] = currentVertex;
								face->vertexIndices[1] = currentVertex - 1;
								face->vertexIndices[2] = currentVertex - 2;
								face->edgeIndices[0] = edgeCursor;
								face->edgeIndices[1] = edgeCursor - 2;
							} else {
								face->vertexIndices[0] = currentVertex - 2;
								face->vertexIndices[1] = currentVertex - 1;
								face->vertexIndices[2] = currentVertex;
								if (vertexIndex == 2)
									face->edgeIndices[0] = edgeCursor++;
								else
									face->edgeIndices[0] = edgeCursor - 2;
								face->edgeIndices[1] = edgeCursor;
							}
							face->vertexIndices[3] = -1;
							face->edgeIndices[2] = ++edgeCursor;
							++edgeCursor;
							face->edgeIndices[3] = -1;
							face->texCoordIndices[0] = face->vertexIndices[0];
							face->texCoordIndices[1] = face->vertexIndices[1];
							face->texCoordIndices[2] = face->vertexIndices[2];
							face->texCoordIndices[3] = face->vertexIndices[3];
							face->normalIndices[0] = face->vertexIndices[0];
							face->normalIndices[1] = face->vertexIndices[1];
							face->normalIndices[2] = face->vertexIndices[2];
							face->normalIndices[3] = face->vertexIndices[3];
							++faceNode->faceCount;
						}
						firstVertex += stripVertexCount;
					}
					dest = (uint8_t*)OptModel_AppendPackedFaceDerivedData(faceNode, dest, conversionState);
					faceData->edgeCount = edgeCursor;
					if (edgeCursor > g_sceneEdgeFlagsCapacity)
						g_sceneEdgeFlagsCapacity = edgeCursor;
				}
				break;
			}

			case OPT_TEXTURE:
				packedNode->param2 = dest;
				dest += ModelTexture_LoadRgbOrTexFile(dest, params[0].data);
				break;

			case OPT_FACEGROUP: {
				OptNode* mutableSourceNode = (OptNode*)sourceNode;
				const float* sourceValues = params[0].data;
				int sourceIndex;
				packedNode->param1 = 0;
				for (sourceIndex = 0; sourceIndex < params[0].value1; ++sourceIndex) {
					if (mutableSourceNode->pChildren[sourceIndex] != NULL &&
						(sourceIndex == 0 || sourceValues[sourceIndex - 1] != sourceValues[sourceIndex])) {
						*(float*)dest = sourceValues[sourceIndex];
						dest += sizeof(float);
						mutableSourceNode->pChildren[packedNode->param1] =
							mutableSourceNode->pChildren[sourceIndex];
						++packedNode->param1;
					}
				}
				mutableSourceNode->childCount = packedNode->param1;
				break;
			}

			case OPT_HARDPOINT: {
				const OptVector* position = params[1].data;
				OptHardpoint* hardpoint = (OptHardpoint*)dest;
				hardpoint->hardpointType = *(const int*)params[0].data;
				hardpoint->position = *position;
				dest += sizeof(*hardpoint);
				break;
			}

			case OPT_ROTSCALE: {
				*(OptVector*)dest = *(const OptVector*)params[0].data;
				dest += sizeof(OptVector);
				*(OptVector*)dest = *(const OptVector*)params[1].data;
				dest += sizeof(OptVector);
				*(OptVector*)dest = *(const OptVector*)params[2].data;
				dest += sizeof(OptVector);
				*(OptVector*)dest = *(const OptVector*)params[3].data;
				dest += sizeof(OptVector);
				break;
			}

			case OPT_MESHDESC: {
				float* values = (float*)dest;
				dest += 18 * sizeof(float);
				values[0] = *(const float*)params[0].data;
				((int*)values)[1] = *(const int*)params[1].data;
				*(OptVector*)&values[2] = *(const OptVector*)params[2].data;
				*(OptVector*)&values[5] = *(const OptVector*)params[3].data;
				*(OptVector*)&values[8] = *(const OptVector*)params[4].data;
				*(OptVector*)&values[11] = *(const OptVector*)params[5].data;
				values[14] = *(const float*)params[6].data;
				*(OptVector*)&values[15] = *(const OptVector*)params[7].data;
				break;
			}

			default:
				break;
		}
	}

	if (sourceNode->childCount != 0) {
		childState = *(SceneMesh*)conversionState;
		g_curVertNormals = NULL;
		g_modelNodeWalkUnusedScratch2 = NULL;
		g_curMeshFlags = NULL;
		packedNode->childCount = sourceNode->childCount;
		packedNode->pChildren = (OptNode**)dest;
		dest += sizeof(*packedNode->pChildren) * (size_t)packedNode->childCount;
		for (childIndex = 0; childIndex < sourceNode->childCount; ++childIndex) {
			packedNode->pChildren[childIndex] = (OptNode*)dest;
			dest += OptModel_ConvertImportedNodeToPackedRecursive(
				sourceModel, sourceNode->pChildren[childIndex], &childState, dest);
		}
	}
	return (size_t)(dest - destBuffer);
}

// FUNCTION: XVT 0x478F50
size_t OptModel_CalculatePackedNodeSizeRecursive(const OptimizedPolyObject* sourceModel,
												 const OptNode* sourceNode, void* conversionState) {
	size_t packedSize;
	OptLegacyParamRecord* params;
	OptNodeType nodeType;
	int* data;
	int* scanData;
	int faceCount;
	int dataIndex;
	int recordCount;
	int marker;
	int maxRecordCount;
	int polygonVertexCount;
	int polygonStart;
	int childIndex;
	int childOffset;
	SceneMesh childState;

	if (sourceNode == NULL) {
		return 0;
	}

	packedSize = 24;

	nodeType = sourceNode->nodeType;
	if (sourceNode->pName != NULL) {

		packedSize = strlen(sourceNode->pName) + 25;
	}
	params = sourceNode->param2;
	if (params != NULL && sourceNode->param1 != 0 && params->data != NULL) {
		data = params->data;
		switch (nodeType) {
			case OPT_FACEDATA:
				faceCount = 0;
				dataIndex = 0;
				recordCount = params->value1;
				if (recordCount > 0) {
					do {
						scanData = &data[dataIndex + 1];
						++dataIndex;
						while (1) {
							marker = scanData[2];
							++faceCount;
							scanData += 2;
							dataIndex += 2;
							if (marker == -1) {
								++dataIndex;
								break;
							}
							if (scanData[1] == -1) {
								dataIndex += 2;
								break;
							}
						}
					} while (dataIndex < recordCount);
				}
				packedSize += 4;
				packedSize += (unsigned int)faceCount << 6;
				packedSize += 36 * faceCount;
				packedSize += 12 * g_curVertexCount;
				break;

			case OPT_TYPE_2:
				packedSize += 48;
				break;

			case OPT_MESHVERTS:
				g_curVertexCount = params->value1;
				packedSize += 12 * g_curVertexCount;
				break;

			case OPT_TYPE_4:
				packedSize += 12;
				break;

			case OPT_TYPE_5:
				packedSize += 36;
				break;

			case OPT_TYPE_6:
				packedSize += 12;
				break;

			case OPT_NODEREF:
				packedSize += strlen((const char*)params->data) + 1;
				break;

			case OPT_TYPE_9:
				maxRecordCount = params->value1;
				marker = params[1].value1;
				++params;
				if (marker > maxRecordCount) {
					maxRecordCount = marker;
				}
				marker = params[1].value1;
				++params;
				if (marker > maxRecordCount) {
					maxRecordCount = marker;
				}
				marker = params[1].value1;
				++params;
				if (marker > maxRecordCount) {
					maxRecordCount = marker;
				}
				marker = params[1].value1;
				if (marker > maxRecordCount) {
					maxRecordCount = marker;
				}
				packedSize += 48 * maxRecordCount;
				packedSize += 8 * maxRecordCount;
				break;

			case OPT_VERTNORMALS:
				recordCount = 3 * params->value1;
				g_curVertNormals = (OptVector*)sourceNode->param2;
				packedSize += 4 * recordCount;
				((SceneMesh*)conversionState)->pVertNormals = (OptVector*)params;
				break;

			case OPT_TEXCOORDS:
				packedSize += 8 * params->value1;
				break;

			case OPT_FACEDATA_15:
				data = params[1].data;
				++params;
				if (data != NULL) {
					recordCount = *data;
					data = params[1].data;
					if (data != NULL) {
						faceCount = (recordCount - 1) * (*data - 1);
						packedSize += 4;
						packedSize += (unsigned int)faceCount << 6;
						packedSize += 36 * faceCount;
						packedSize += 12 * g_curVertexCount;
					}
				}
				break;

			case OPT_FACEDATA_16:
				dataIndex = 0;
				faceCount = 0;
				recordCount = params->value1;
				if (recordCount > 0) {
					do {
						polygonVertexCount = *data;
						if (polygonVertexCount >= 3) {
							polygonStart = dataIndex++;
							while (dataIndex - polygonStart < polygonVertexCount) {
								++faceCount;
								dataIndex += 2;
								if (dataIndex - polygonVertexCount != polygonStart) {
									++dataIndex;
									if (dataIndex - polygonVertexCount == polygonStart) {
										break;
									}
									--dataIndex;
								}
							}
						}
						++data;
						--recordCount;
					} while (recordCount != 0);
				}
				packedSize += 4;
				packedSize += (unsigned int)faceCount << 6;
				packedSize += 36 * faceCount;
				packedSize += 12 * g_curVertexCount;
				break;

			case OPT_FACEDATA_17:
				data = params[1].data;
				++params;
				if (data != NULL) {
					faceCount = 0;
					recordCount = params->value1;
					if (recordCount > 0) {
						do {
							faceCount += *data - 2;
							++data;
							--recordCount;
						} while (recordCount != 0);
					}
					packedSize += 4;
					packedSize += (unsigned int)faceCount << 6;
					packedSize += 36 * faceCount;
					packedSize += 12 * g_curVertexCount;
				}
				break;

			case OPT_TEXTURE:
				packedSize += OptModel_GetExternalTextureSerializedSize((const char*)params->data);
				break;

			case OPT_FACEGROUP:
				packedSize += 4 * params->value1;
				break;

			case OPT_HARDPOINT:
				packedSize += 16;
				break;

			case OPT_ROTSCALE:
				packedSize += 48;
				break;

			case OPT_MESHDESC:
				packedSize += 72;
				break;

			default:
				break;
		}
	}

	childOffset = 0;
	if (sourceNode->childCount != 0) {
		childState = *(SceneMesh*)conversionState;
		childIndex = 0;
		g_curVertNormals = NULL;
		g_modelNodeWalkUnusedScratch2 = NULL;
		g_curMeshFlags = NULL;

		packedSize += 4 * sourceNode->childCount;

		if (sourceNode->childCount > 0) {
			do {
				packedSize += OptModel_CalculatePackedNodeSizeRecursive(
					sourceModel, *(OptNode**)((uint8_t*)sourceNode->pChildren + childOffset), &childState);
				childOffset += sizeof(*sourceNode->pChildren);
				++childIndex;
			} while (sourceNode->childCount > childIndex);
		}
	}
	return packedSize;
}

// FUNCTION: XVT 0x4792F0
int OptModel_FindUniqueEdgeIndex(const OptPackedFaceNode* faceNode, int vertexIndexA, int vertexIndexB) {
	int result;
	const OptPackedFaceRecord* face;
	int faceIndex;

	face = faceNode->faceData->records;
	result = -1;
	faceIndex = 0;
	while (faceIndex < faceNode->faceCount) {
		if (vertexIndexA == face->vertexIndices[0] && vertexIndexB == face->vertexIndices[1]) {
			result = face->edgeIndices[0];
			break;
		}
		if (vertexIndexA == face->vertexIndices[1] && vertexIndexB == face->vertexIndices[0]) {
			result = face->edgeIndices[0];
			break;
		}
		if (vertexIndexA == face->vertexIndices[1] && vertexIndexB == face->vertexIndices[2]) {
			result = face->edgeIndices[1];
			break;
		}
		if (vertexIndexA == face->vertexIndices[2] && vertexIndexB == face->vertexIndices[1]) {
			result = face->edgeIndices[1];
			break;
		}
		if (face->vertexIndices[3] == -1) {
			if (vertexIndexA == face->vertexIndices[0] && vertexIndexB == face->vertexIndices[2]) {
				result = face->edgeIndices[2];
				break;
			}
			if (vertexIndexA == face->vertexIndices[2] && vertexIndexB == face->vertexIndices[0]) {
				result = face->edgeIndices[2];
				break;
			}
		} else {
			if (vertexIndexA == face->vertexIndices[2] && vertexIndexB == face->vertexIndices[3]) {
				result = face->edgeIndices[2];
				break;
			}
			if (vertexIndexA == face->vertexIndices[3] && vertexIndexB == face->vertexIndices[2]) {
				result = face->edgeIndices[2];
				break;
			}
			if (vertexIndexA == face->vertexIndices[0] && vertexIndexB == face->vertexIndices[3]) {
				result = face->edgeIndices[3];
				break;
			}
			if (vertexIndexA == face->vertexIndices[3] && vertexIndexB == face->vertexIndices[0]) {
				result = face->edgeIndices[3];
				break;
			}
		}
		face++;
		faceIndex++;
	}

	if (result != -1) {
		face++;
		faceIndex++;
		while (faceIndex < faceNode->faceCount) {
			if (vertexIndexA == face->vertexIndices[0] && vertexIndexB == face->vertexIndices[1]) {
				return -1;
			}
			if (vertexIndexA == face->vertexIndices[1] && vertexIndexB == face->vertexIndices[0]) {
				return -1;
			}
			if (vertexIndexA == face->vertexIndices[1] && vertexIndexB == face->vertexIndices[2]) {
				return -1;
			}
			if (vertexIndexA == face->vertexIndices[2] && vertexIndexB == face->vertexIndices[1]) {
				return -1;
			}
			if (face->vertexIndices[3] == -1) {
				if (vertexIndexA == face->vertexIndices[0] && vertexIndexB == face->vertexIndices[2]) {
					return -1;
				}
				if (vertexIndexA == face->vertexIndices[2] && vertexIndexB == face->vertexIndices[0]) {
					return -1;
				}
			} else {
				if (vertexIndexA == face->vertexIndices[2] && vertexIndexB == face->vertexIndices[3]) {
					return -1;
				}
				if (vertexIndexA == face->vertexIndices[3] && vertexIndexB == face->vertexIndices[2]) {
					return -1;
				}
				if (vertexIndexA == face->vertexIndices[0] && vertexIndexB == face->vertexIndices[3]) {
					return -1;
				}
				if (vertexIndexA == face->vertexIndices[3] && vertexIndexB == face->vertexIndices[0]) {
					return -1;
				}
			}
			face++;
			faceIndex++;
		}
	}
	return result;
}

// FUNCTION: XVT 0x4794C0
float* OptModel_AppendPackedFaceDerivedData(OptPackedFaceNode* faceNode, uint8_t* dest,
											void* conversionState) {
	dest += sizeof(OptPackedFaceRecord) * faceNode->faceCount;
	OptModel_BuildFaceNormalTangentData((float*)dest, faceNode->faceData, faceNode->faceCount,
										conversionState);
	dest += 3 * sizeof(OptVector) * faceNode->faceCount;
	dest += 3 * sizeof(float) * g_generatedVertexNormalCount;
	return (float*)dest;
}

// FUNCTION: XVT 0x479510
void OptModel_BuildFaceNormalTangentData(float* dest, const OptPackedFaceData* faceData, int faceCount,
										 const void* conversionState) {
	const OptPackedFaceRecord* records;
	int faceIndex;
	float edgeAx, edgeAy, edgeAz, edgeBx, edgeBy, edgeBz;
	float duA, dvA, duB, dvB, determinant;
	float normalLengthSquared;

	if (g_modelNodeWalkUnusedScratch0 == NULL) {
		return;
	}
	records = faceData->records;
	if (faceCount > 0) {
		const OptPackedFaceRecord* face = records;
		for (faceIndex = 0; faceIndex < faceCount; ++faceIndex) {
			edgeAx = ((const OptVector*)g_modelNodeWalkUnusedScratch0)[face->vertexIndices[1]].x -
					 ((const OptVector*)g_modelNodeWalkUnusedScratch0)[face->vertexIndices[0]].x;
			edgeAy = ((const OptVector*)g_modelNodeWalkUnusedScratch0)[face->vertexIndices[1]].y -
					 ((const OptVector*)g_modelNodeWalkUnusedScratch0)[face->vertexIndices[0]].y;
			edgeAz = ((const OptVector*)g_modelNodeWalkUnusedScratch0)[face->vertexIndices[1]].z -
					 ((const OptVector*)g_modelNodeWalkUnusedScratch0)[face->vertexIndices[0]].z;
			edgeBx = ((const OptVector*)g_modelNodeWalkUnusedScratch0)[face->vertexIndices[1]].x -
					 ((const OptVector*)g_modelNodeWalkUnusedScratch0)[face->vertexIndices[2]].x;
			edgeBy = ((const OptVector*)g_modelNodeWalkUnusedScratch0)[face->vertexIndices[1]].y -
					 ((const OptVector*)g_modelNodeWalkUnusedScratch0)[face->vertexIndices[2]].y;
			edgeBz = ((const OptVector*)g_modelNodeWalkUnusedScratch0)[face->vertexIndices[1]].z -
					 ((const OptVector*)g_modelNodeWalkUnusedScratch0)[face->vertexIndices[2]].z;
			dest[0] = edgeBz * edgeAy - edgeBy * edgeAz;
			dest[1] = edgeBx * edgeAz - edgeBz * edgeAx;
			dest[2] = edgeBy * edgeAx - edgeBx * edgeAy;
			normalLengthSquared = dest[2] * dest[2] + dest[0] * dest[0] + dest[1] * dest[1];
			if (normalLengthSquared == g_sw3dZeroFloat) {
				if (face->vertexIndices[3] != -1) {
					edgeAx = ((const OptVector*)g_modelNodeWalkUnusedScratch0)[face->vertexIndices[3]].x -
							 ((const OptVector*)g_modelNodeWalkUnusedScratch0)[face->vertexIndices[0]].x;
					edgeAy = ((const OptVector*)g_modelNodeWalkUnusedScratch0)[face->vertexIndices[3]].y -
							 ((const OptVector*)g_modelNodeWalkUnusedScratch0)[face->vertexIndices[0]].y;
					edgeAz = ((const OptVector*)g_modelNodeWalkUnusedScratch0)[face->vertexIndices[3]].z -
							 ((const OptVector*)g_modelNodeWalkUnusedScratch0)[face->vertexIndices[0]].z;
					edgeBx = ((const OptVector*)g_modelNodeWalkUnusedScratch0)[face->vertexIndices[3]].x -
							 ((const OptVector*)g_modelNodeWalkUnusedScratch0)[face->vertexIndices[2]].x;
					edgeBy = ((const OptVector*)g_modelNodeWalkUnusedScratch0)[face->vertexIndices[3]].y -
							 ((const OptVector*)g_modelNodeWalkUnusedScratch0)[face->vertexIndices[2]].y;
					edgeBz = ((const OptVector*)g_modelNodeWalkUnusedScratch0)[face->vertexIndices[3]].z -
							 ((const OptVector*)g_modelNodeWalkUnusedScratch0)[face->vertexIndices[2]].z;
					dest[0] = edgeBz * edgeAy - edgeBy * edgeAz;
					dest[1] = edgeBx * edgeAz - edgeBz * edgeAx;
					dest[2] = edgeBy * edgeAx - edgeBx * edgeAy;
					normalLengthSquared = dest[2] * dest[2] + dest[0] * dest[0] + dest[1] * dest[1];
					if (normalLengthSquared != g_sw3dZeroFloat) {
						const float scale = (float)(g_sw3dUnitFloat / sqrt(normalLengthSquared));
						dest[0] *= scale;
						dest[1] *= scale;
						dest[2] *= scale;
					}
				}
			} else {
				const float scale = (float)(g_sw3dUnitFloat / sqrt(normalLengthSquared));
				dest[0] *= scale;
				dest[1] *= scale;
				dest[2] *= scale;
			}
			if (g_optModelInvertFaceNormals != 0) {
				dest[0] = -dest[0];
				dest[1] = -dest[1];
				dest[2] = -dest[2];
			}
			dest += 3;
			++face;
		}
	}
	records = faceData->records;
	if (g_modelNodeWalkUnusedScratch1 != NULL) {
		int tangentFaceIndex;
		if (faceCount > 0)
			for (tangentFaceIndex = 0; tangentFaceIndex < faceCount; ++tangentFaceIndex) {
				const OptPackedFaceRecord* face = records;
				float lengthSquared;
				const OptVector* vertices;
				const OptTexCoord* texCoords;
				vertices = (const OptVector*)g_modelNodeWalkUnusedScratch0;
				edgeAx = vertices[face->vertexIndices[0]].x - vertices[face->vertexIndices[1]].x;
				edgeAy = vertices[face->vertexIndices[0]].y - vertices[face->vertexIndices[1]].y;
				edgeAz = vertices[face->vertexIndices[0]].z - vertices[face->vertexIndices[1]].z;
				edgeBx = vertices[face->vertexIndices[0]].x - vertices[face->vertexIndices[2]].x;
				edgeBy = vertices[face->vertexIndices[0]].y - vertices[face->vertexIndices[2]].y;
				edgeBz = vertices[face->vertexIndices[0]].z - vertices[face->vertexIndices[2]].z;
				texCoords = (const OptTexCoord*)g_modelNodeWalkUnusedScratch1;
				duA = texCoords[face->texCoordIndices[0]].u - texCoords[face->texCoordIndices[1]].u;
				dvA = texCoords[face->texCoordIndices[0]].v - texCoords[face->texCoordIndices[1]].v;
				duB = texCoords[face->texCoordIndices[0]].u - texCoords[face->texCoordIndices[2]].u;
				dvB = texCoords[face->texCoordIndices[0]].v - texCoords[face->texCoordIndices[2]].v;
				determinant = duB * dvA - duA * dvB;
				dest[0] = edgeBx * dvA - dvB * edgeAx;
				dest[1] = edgeBy * dvA - dvB * edgeAy;
				dest[2] = edgeBz * dvA - dvB * edgeAz;
				lengthSquared = dest[2] * dest[2] + dest[0] * dest[0] + dest[1] * dest[1];
				if (determinant == 0.0f || lengthSquared == g_sw3dZeroFloat) {
					if (face->vertexIndices[3] != -1) {
						vertices = (const OptVector*)g_modelNodeWalkUnusedScratch0;
						edgeAx = vertices[face->vertexIndices[0]].x - vertices[face->vertexIndices[1]].x;
						edgeAy = vertices[face->vertexIndices[0]].y - vertices[face->vertexIndices[1]].y;
						edgeAz = vertices[face->vertexIndices[0]].z - vertices[face->vertexIndices[1]].z;
						edgeBx = vertices[face->vertexIndices[0]].x - vertices[face->vertexIndices[3]].x;
						edgeBy = vertices[face->vertexIndices[0]].y - vertices[face->vertexIndices[3]].y;
						edgeBz = vertices[face->vertexIndices[0]].z - vertices[face->vertexIndices[3]].z;
						texCoords = (const OptTexCoord*)g_modelNodeWalkUnusedScratch1;
						duA = texCoords[face->texCoordIndices[0]].u - texCoords[face->texCoordIndices[1]].u;
						dvA = texCoords[face->texCoordIndices[0]].v - texCoords[face->texCoordIndices[1]].v;
						duB = texCoords[face->texCoordIndices[0]].u - texCoords[face->texCoordIndices[3]].u;
						dvB = texCoords[face->texCoordIndices[0]].v - texCoords[face->texCoordIndices[3]].v;
						determinant = duB * dvA - duA * dvB;
						dest[0] = edgeBx * dvA - dvB * edgeAx;
						dest[1] = edgeBy * dvA - dvB * edgeAy;
						dest[2] = edgeBz * dvA - dvB * edgeAz;
						lengthSquared = dest[2] * dest[2] + dest[0] * dest[0] + dest[1] * dest[1];
						if (determinant == 0.0f || lengthSquared == g_sw3dZeroFloat) {
							vertices = (const OptVector*)g_modelNodeWalkUnusedScratch0;
							edgeAx = vertices[face->vertexIndices[0]].x - vertices[face->vertexIndices[2]].x;
							edgeAy = vertices[face->vertexIndices[0]].y - vertices[face->vertexIndices[2]].y;
							edgeAz = vertices[face->vertexIndices[0]].z - vertices[face->vertexIndices[2]].z;
							edgeBx = vertices[face->vertexIndices[0]].x - vertices[face->vertexIndices[3]].x;
							edgeBy = vertices[face->vertexIndices[0]].y - vertices[face->vertexIndices[3]].y;
							edgeBz = vertices[face->vertexIndices[0]].z - vertices[face->vertexIndices[3]].z;
							texCoords = (const OptTexCoord*)g_modelNodeWalkUnusedScratch1;
							duA =
								texCoords[face->texCoordIndices[0]].u - texCoords[face->texCoordIndices[2]].u;
							dvA =
								texCoords[face->texCoordIndices[0]].v - texCoords[face->texCoordIndices[2]].v;
							duB =
								texCoords[face->texCoordIndices[0]].u - texCoords[face->texCoordIndices[3]].u;
							dvB =
								texCoords[face->texCoordIndices[0]].v - texCoords[face->texCoordIndices[3]].v;
							determinant = duB * dvA - duA * dvB;
							dest[0] = edgeBx * dvA - dvB * edgeAx;
							dest[1] = edgeBy * dvA - dvB * edgeAy;
							dest[2] = edgeBz * dvA - dvB * edgeAz;
							lengthSquared = dest[2] * dest[2] + dest[0] * dest[0] + dest[1] * dest[1];
							if (determinant == 0.0f || lengthSquared == g_sw3dZeroFloat) {
								vertices = (const OptVector*)g_modelNodeWalkUnusedScratch0;
								edgeAx =
									vertices[face->vertexIndices[1]].x - vertices[face->vertexIndices[2]].x;
								edgeAy =
									vertices[face->vertexIndices[1]].y - vertices[face->vertexIndices[2]].y;
								edgeAz =
									vertices[face->vertexIndices[1]].z - vertices[face->vertexIndices[2]].z;
								edgeBx =
									vertices[face->vertexIndices[1]].x - vertices[face->vertexIndices[3]].x;
								edgeBy =
									vertices[face->vertexIndices[1]].y - vertices[face->vertexIndices[3]].y;
								edgeBz =
									vertices[face->vertexIndices[1]].z - vertices[face->vertexIndices[3]].z;
								texCoords = (const OptTexCoord*)g_modelNodeWalkUnusedScratch1;
								duA = texCoords[face->texCoordIndices[1]].u -
									  texCoords[face->texCoordIndices[2]].u;
								dvA = texCoords[face->texCoordIndices[1]].v -
									  texCoords[face->texCoordIndices[2]].v;
								duB = texCoords[face->texCoordIndices[1]].u -
									  texCoords[face->texCoordIndices[3]].u;
								dvB = texCoords[face->texCoordIndices[1]].v -
									  texCoords[face->texCoordIndices[3]].v;
								determinant = duB * dvA - duA * dvB;
								dest[0] = edgeBx * dvA - dvB * edgeAx;
								dest[1] = edgeBy * dvA - dvB * edgeAy;
								dest[2] = edgeBz * dvA - dvB * edgeAz;
								lengthSquared = dest[2] * dest[2] + dest[0] * dest[0] + dest[1] * dest[1];
								if (determinant == 0.0f || lengthSquared == g_sw3dZeroFloat) {
									dest[0] = edgeBx;
									dest[1] = edgeBy;
									dest[2] = edgeBz;
									determinant = 1.0f;
									duA = 0.0f;
									duB = 1.0f;
								}
							}
						}
					} else {
						dest[0] = edgeBx;
						dest[1] = edgeBy;
						dest[2] = edgeBz;
						determinant = 1.0f;
						duA = 0.0f;
						duB = 1.0f;
					}
				}
				{
					float reciprocal = g_sw3dUnitFloat / determinant;
					dest[0] *= reciprocal;
					dest[1] *= reciprocal;
					dest[2] *= reciprocal;
					reciprocal = -reciprocal;
					dest[3] = (duA * edgeBx - duB * edgeAx) * reciprocal;
					dest[4] = (duA * edgeBy - duB * edgeAy) * reciprocal;
					dest[5] = (duA * edgeBz - duB * edgeAz) * reciprocal;
				}
				if (duA == 0.0f) {
					if (duB == 0.0f) {
						dest[4] = 1.0f;
					}
				}
				dest += 6;
				++records;
			}
	} else {
#ifdef XVT_MODERN
		// The packed buffer reserves two tangent vectors per face.
		dest += 6 * faceCount;
#else
		dest += 24 * faceCount;
#endif
	}
	if (((const SceneMesh*)conversionState)->pVertNormals != NULL) {
		g_generatedVertexNormalCount = 0;
	} else {
		g_generatedVertexNormalCount = g_curVertexCount;
		OptModel_BuildVertexNormalsFromFaces(dest, faceData, faceCount);
	}
}

// FUNCTION: XVT 0x479CE0
void OptModel_BuildVertexNormalsFromFaces(float* dest, const OptPackedFaceData* faceData, int faceCount) {
	int vertexIndex;
	int incidentFaceCount;
	const OptPackedFaceRecord* face;
	const float* faceNormal;
	int remainingFaces;
	float scale;
	int faceRecordBytes;

	vertexIndex = 0;
	if (g_curVertexCount <= 0) {
		return;
	}
	faceRecordBytes = faceCount * (int)sizeof(OptPackedFaceRecord);
	do {
		incidentFaceCount = 0;
		face = faceData->records;
		faceNormal = (const float*)((const uint8_t*)faceData + sizeof(faceData->edgeCount) + faceRecordBytes);
		remainingFaces = faceCount;
		if (remainingFaces > 0) {
			do {
				if (face->vertexIndices[0] == vertexIndex || face->vertexIndices[1] == vertexIndex ||
					face->vertexIndices[2] == vertexIndex || face->vertexIndices[3] == vertexIndex) {
					++incidentFaceCount;
					if (incidentFaceCount == 1) {
						dest[0] = faceNormal[0];
						dest[1] = faceNormal[1];
						dest[2] = faceNormal[2];
					} else {
						dest[0] = dest[0] + faceNormal[0];
						dest[1] = faceNormal[1] + dest[1];
						dest[2] = faceNormal[2] + dest[2];
					}
				}
				++face;
				faceNormal += 3;
				--remainingFaces;
			} while (remainingFaces != 0);
		}
		if (incidentFaceCount > 1) {
			if (incidentFaceCount < 65) {
				scale = g_sw3dSpanLengthReciprocal[incidentFaceCount];
			} else {
				scale = g_sw3dUnitFloat / incidentFaceCount;
			}
			dest[0] *= scale;
			dest[1] *= scale;
			dest[2] *= scale;
		}
		dest += 3;
		++vertexIndex;
	} while (vertexIndex < g_curVertexCount);
}
#endif

// FUNCTION: XVT 0x479DE0
OptNode* OptModel_ResolveNodeRef(const OptimizedPolyObject* object, const char* name) {
	OptNode* result;
	int rootIndex;

	for (rootIndex = 0; rootIndex < object->rootNodeCount; ++rootIndex) {
		result = OptModel_FindNodeByName(object->rootNodes[rootIndex], name);
		if (result != NULL) {
			return result;
		}
	}

	return NULL;
}

// FUNCTION: XVT 0x479E20
OptNode* OptModel_FindNodeByName(OptNode* node, const char* name) {
	OptNode* result;
	int childIndex;
	int nameCompare;

	if (node == NULL) {
		return NULL;
	}
	if (node->pName != NULL) {
#ifdef XVT_MODERN
		nameCompare = strcasecmp(node->pName, name);
#else
		nameCompare = _strcmpi(node->pName, name);
#endif
		if (nameCompare == 0) {
			return node;
		}
	}

	for (childIndex = 0; childIndex < node->childCount; ++childIndex) {
		result = OptModel_FindNodeByName(node->pChildren[childIndex], name);
		if (result != NULL) {
			return result;
		}
	}

	return NULL;
}

#ifndef XVT_MODERN
// FUNCTION: XVT 0x47A430
int OptModel_GetExternalTextureSerializedSize(const char* sourceFileName) {
	char* extension;
	XvtFile* textureStream;
	int pixelCount;
	int payloadSize;
	int serializedSize;
	OptExternalTexHeader texHeader;
	uint8_t rgbHeader[32];
	char fileName[256];

	strcpy(fileName, sourceFileName);
	extension = fileName + strlen(fileName) - 3;
	if (_strcmpi(extension, g_extRgb) == 0) {
		extension[0] = 't';
		extension[1] = 'e';
		extension[2] = 'x';
		File_OpenGlobalStream(fileName, g_fileModeReadBinary, 0, 0);
		textureStream = g_stream;
		extension[0] = 'r';
		extension[1] = 'g';
		extension[2] = 'b';
		if (textureStream == NULL) {
			File_OpenGlobalStream(fileName, g_fileModeReadBinary, 0, 0);
			textureStream = g_stream;
			if (textureStream != NULL) {
				File_RawRead(rgbHeader, 16, 1, textureStream);
				pixelCount = ((unsigned int)rgbHeader[6] << 8) + rgbHeader[7];
				pixelCount *= ((unsigned int)rgbHeader[8] << 8) + rgbHeader[9];
				File_RawClose(textureStream);
				serializedSize = 3 * pixelCount + 13056;
				return serializedSize;
			}
			return 12376;
		}
	} else {
		if (_strcmpi(extension, g_extTex) != 0) {
			return 12376;
		}
		File_OpenGlobalStream(fileName, g_fileModeReadBinary, 0, 0);
		textureStream = g_stream;
		if (textureStream == NULL) {
			return 12376;
		}
	}
	File_RawRead(&texHeader, sizeof(texHeader), 1, textureStream);
	File_RawClose(textureStream);
	payloadSize = texHeader.width * texHeader.height;
	if (texHeader.pixelCount == payloadSize) {
		payloadSize = texHeader.storedPayloadSize;
	}
	return payloadSize + 12312;
}
#endif
