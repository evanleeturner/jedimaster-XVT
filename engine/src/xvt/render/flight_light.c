#include "xvt/render/flight_light.h"
#include "xvt/render/renderer.h"

#include "xvt/assets/opt_model.h"
#include "xvt/flight/ai/pai.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/fview.h"
#include "xvt/flight/object/collide.h"
#include "xvt/flight/object/object.h"
#include "xvt/math/math.h"
#include "xvt/math/math3d.h"
#include "xvt/render/flight_palette.h"
#include "xvt/render/render_clip.h"
#include "xvt/render/render_scene.h"

/* Point lights in g_objectPointLights for the object being lit, 0 to 8.
 * FlightLight_SetupObjectLighting sets it;
 * FlightLight_SetupObjectLightingByIndex, FlightView_Render and
 * FlightMap_DrawObjectPass set 0. */
// GLOBAL: XVT 0x5235F0
int g_objectPointLightCount = 0;
/* Explosions lighting the object being lit, in its own axes, as
 * FlightLight_SetupObjectLighting found them; it fills up to 8 of the 10
 * entries. */
// GLOBAL: XVT 0x9FD3B0
struct ObjectPointLight g_objectPointLights[10] = {{0}};

/* Object whose lights FlightLight_ComputeSoftwareFaceSampleIntensity has
 * cached; FlightLight_ResetSoftwareFaceSampleCache sets NULL. */
// GLOBAL: XVT 0x51C00C
static struct ObjectRecord *g_swFaceLightCachedObject;
/* Face whose view-space normal FlightLight_ComputeSoftwareFaceSampleIntensity
 * has cached in g_swFaceLightFaceNormal;
 * FlightLight_ResetSoftwareFaceSampleCache sets NULL. */
// GLOBAL: XVT 0x51C014
static struct SceneFace *g_swFaceLightCachedFace;
/* g_objectPointLightCount when the cached object's lights were taken. */
// GLOBAL: XVT 0x51C010
static int g_swFaceLightCachedPointLightCount = 0;
/* The cached object's point lights in view space, turned by its mesh's view
 * orientation and moved by its view position. */
// GLOBAL: XVT 0x550C10
static struct OptVector g_swFaceLightPointPositions[10] = {{0.0f, 0.0f, 0.0f}};
/* Intensity of each cached point light, as a float. */
// GLOBAL: XVT 0x550BE0
static float g_swFaceLightPointIntensities[10] = {0.0f};
/* The cached object's light direction in view space, g_objectLightDirection
 * scaled from Q15 and turned by its mesh's view orientation. */
// GLOBAL: XVT 0x550C00
static struct OptVector g_swFaceLightDir = {0.0f, 0.0f, 0.0f};
/* View-space normal of the cached face. */
// GLOBAL: XVT 0x550C70
static struct OptVector g_swFaceLightFaceNormal = {0.0f, 0.0f, 0.0f};

/* Clears the face lighting cache: g_swFaceLightCachedObject and
 * g_swFaceLightCachedFace to NULL. sw3d_DrawVisibleFacesToSurface calls it
 * before drawing. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x4206A0
void FlightLight_ResetSoftwareFaceSampleCache(void)
{
	g_swFaceLightCachedObject = 0;
	g_swFaceLightCachedFace = 0;
}

/* Light intensity, 0 to 1, at one screen sample of a face for the software
 * renderer; 0 for a face with no mesh. When the face's object is not the cached
 * one it lights it with FlightLight_SetupObjectLighting and caches its point
 * lights and light direction in view space; it caches the face's view-space
 * normal per face. The sample's view position is z = 1 / reciprocalDepth and x
 * and y = (screen coordinate - viewport middle) * z * g_invProjScale, y also
 * less g_projOffsetY. With g_specularEnabled it starts from 0.7 times a
 * specular term of the light direction, ((light + eye) . normal / 2) to the
 * 48th power, and returns 1 when that reaches 1. Each point light the face
 * turns toward adds its intensity times (normal . offset) / d^2, d being a
 * rough length of its offset from the sample, plus, with specular on, (normal .
 * v / 2 / e)^48 / e when that is not negative, v being the offset less the
 * sample position and e a rough length of v; the sum stops at 1. There is no
 * diffuse term from the light direction. */
// FUNCTION: XVT 0x4206B0
float FlightLight_ComputeSoftwareFaceSampleIntensity(struct SceneFace *face,
						     int screenX, int screenY,
						     float reciprocalDepth)
{
	struct SceneMesh *mesh;
	struct OptVector normal;
	struct OptVector vector;
	float screenToViewScale;
	float sampleX;
	float sampleY;
	float sampleZ;
	float intensity;
	float dx;
	float dy;
	float dz;
	float specular;
	float contribution;
	float componentX;
	float componentY;
	float componentZ;
	float distance;
	float reciprocal;
	float lightDot;
	int lightIndex;

	mesh = face->pMesh;
	if (mesh == NULL) {
		return g_renderZeroFloat;
	}
	if (mesh->pObject != g_swFaceLightCachedObject) {
		FlightLight_SetupObjectLighting(mesh->pObject);
		g_swFaceLightCachedPointLightCount = g_objectPointLightCount;
		g_swFaceLightCachedObject = mesh->pObject;
		for (lightIndex = 0; g_objectPointLightCount > lightIndex;
		     ++lightIndex) {
			struct OptVector *position =
				&g_swFaceLightPointPositions[lightIndex];

			position->x = (float)g_objectPointLights[lightIndex].x;
			position->y = (float)g_objectPointLights[lightIndex].y;
			position->z = (float)g_objectPointLights[lightIndex].z;
			Math3D_RotateVec3(&position->x, mesh->viewOrient);
			position->x += mesh->viewPosX;
			position->y += mesh->viewPosY;
			position->z += mesh->viewPosZ;
			g_swFaceLightPointIntensities[lightIndex] =
				(float)g_objectPointLights[lightIndex]
					.intensity;
		}
		g_swFaceLightDir.x = (float)g_objectLightDirectionX *
				     g_renderLightDirectionUnitScale;
		g_swFaceLightDir.y = (float)g_objectLightDirectionY *
				     g_renderLightDirectionUnitScale;
		g_swFaceLightDir.z = (float)g_objectLightDirectionZ *
				     g_renderLightDirectionUnitScale;
		Math3D_RotateVec3(&g_swFaceLightDir.x, mesh->viewOrient);
	}

	sampleZ = 1.0f / reciprocalDepth;
	screenToViewScale = sampleZ * g_invProjScale;
	sampleX = (float)(screenX - (g_flightVpWidth >> 1)) * screenToViewScale;
	sampleY = (float)(screenY - (g_flightVpHeight >> 1) - g_projOffsetY) *
		  screenToViewScale;
	if (face != g_swFaceLightCachedFace) {
		g_swFaceLightCachedFace = face;
		vector = mesh->pFaceNormals[face->faceIndex];
		Math3D_RotateVec3(&vector.x, mesh->viewOrient);
		normal = vector;
		g_swFaceLightFaceNormal = vector;
	} else {
		normal = g_swFaceLightFaceNormal;
	}
	intensity = 0.0f;

	if (g_specularEnabled != 0) {
		/* vector is reused here for the direction from the sample point to the eye, normalized by an
		 * estimated length, for the specular half vector. */
		vector.x = -sampleX;
		componentX = vector.x;
		vector.y = -sampleY;
		componentY = vector.y;
		vector.z = -sampleZ;
		componentZ = vector.z;
		if (vector.x < 0.0f) {
			componentX = -vector.x;
		}
		if (vector.y < 0.0f) {
			componentY = -vector.y;
		}
		if (vector.z < 0.0f) {
			componentZ = -vector.z;
		}
		if (componentY <= componentX && componentZ <= componentX) {
			distance = componentX * 0.92640001f +
				   (componentZ + componentY) * 0.3872f;
		} else if (componentY >= componentX &&
			   componentZ <= componentY) {
			distance = componentY * 0.92640001f +
				   (componentZ + componentX) * 0.3872f;
		} else {
			distance = componentZ * 0.92640001f +
				   (componentY + componentX) * 0.3872f;
		}
		reciprocal = 1.0f / distance;
		vector.x *= reciprocal;
		vector.y *= reciprocal;
		vector.z *= reciprocal;
		specular = (g_swFaceLightDir.x + vector.x) * normal.x +
			   (g_swFaceLightDir.y + vector.y) * normal.y +
			   (g_swFaceLightDir.z + vector.z) * normal.z;
		if (specular > g_renderZeroFloat) {
			specular *= g_renderHalfFloat;
			specular = specular * specular * specular;
			specular *= specular;
			specular *= specular;
			specular *= specular;
			specular *= specular;
		} else {
			specular = 0.0f;
		}
		contribution = specular * 0.7f;
		if (contribution > g_renderZeroFloat) {
			intensity = contribution;
			if (intensity >= 1.0f) {
				return 1.0f;
			}
		}
	}

	for (lightIndex = 0; lightIndex < g_swFaceLightCachedPointLightCount;
	     ++lightIndex) {
		const struct OptVector *position =
			&g_swFaceLightPointPositions[lightIndex];

		dx = position->x - sampleX;
		dy = position->y - sampleY;
		dz = position->z - sampleZ;
		lightDot = normal.x * dx + normal.y * dy + normal.z * dz;
		if (lightDot > g_renderZeroFloat) {
			componentX = dx;
			componentY = dy;
			componentZ = dz;
			if (dx < 0.0f) {
				componentX = -dx;
			}
			if (dy < 0.0f) {
				componentY = -dy;
			}
			if (dz < 0.0f) {
				componentZ = -dz;
			}
			if (componentY <= componentX &&
			    componentZ <= componentX) {
				distance = componentX +
					   (componentZ + componentY) *
						   g_renderRoughDistanceScale;
			} else if (componentY >= componentX &&
				   componentZ <= componentY) {
				distance = componentY +
					   (componentZ + componentX) *
						   g_renderRoughDistanceScale;
			} else {
				distance = componentZ +
					   (componentY + componentX) *
						   g_renderRoughDistanceScale;
			}
			lightDot = lightDot / (distance * distance);
			if (g_specularEnabled != 0) {
				float halfDot;
				float cosine;

				dx -= sampleX;
				dy -= sampleY;
				dz -= sampleZ;
				halfDot = (normal.x * dx + normal.y * dy +
					   normal.z * dz) *
					  g_renderHalfFloat;
				componentX = dx;
				componentY = dy;
				componentZ = dz;
				if (dx < 0.0f) {
					componentX = -dx;
				}
				if (dy < 0.0f) {
					componentY = -dy;
				}
				if (dz < 0.0f) {
					componentZ = -dz;
				}
				if (componentY <= componentX &&
				    componentZ <= componentX) {
					distance =
						componentX *
							g_renderSpecularApproxMaxComponentScale +
						(componentZ + componentY) *
							g_renderSpecularApproxOtherComponentsScale;
				} else if (componentY >= componentX &&
					   componentZ <= componentY) {
					distance =
						componentY *
							g_renderSpecularApproxMaxComponentScale +
						(componentZ + componentX) *
							g_renderSpecularApproxOtherComponentsScale;
				} else {
					distance =
						componentZ *
							g_renderSpecularApproxMaxComponentScale +
						(componentY + componentX) *
							g_renderSpecularApproxOtherComponentsScale;
				}
				reciprocal = 1.0f / distance;
				cosine = halfDot * reciprocal;
				if (cosine >= g_renderZeroFloat) {
					specular = cosine * cosine * cosine;
					specular *= specular;
					specular *= specular;
					specular *= specular;
					specular *= specular;
					specular *= reciprocal;
				} else {
					specular = 0.0f;
				}
			} else {
				specular = 0.0f;
			}
			contribution = lightDot + specular;
			if (contribution > g_renderZeroFloat) {
				intensity += g_swFaceLightPointIntensities
						     [lightIndex] *
					     contribution;
				if (intensity >= 1.0f) {
					intensity = 1.0f;
					break;
				}
			}
		}
	}
	return intensity;
}

/* Collects the explosions that light an object into g_objectPointLights and
 * g_objectPointLightCount: every object in a main slot of the explosion genus
 * within maxBoundsExtent + 0x4000 of the object, by collide_roughdistance3d, up
 * to 8. Each light's position is its offset in the object's axes, (offset .
 * side, -(offset . forward), offset . up) in Q15; for an object without a mobj
 * the axes come from FVIEW_SetObjectTransform, which also rewrites the current
 * object matrix. Intensity by explosion type and frame (typeSpecificByte[0]):
 * types 127 to 130 give 192 at frame 2, 320 at 3, 480 at 4, 320 at 5 to 8, 192
 * at 9, 96 at 10, 48 at 11 and 16 at any other, times (effectSize + 4) / 4 when
 * the explosion's effectSize is 4 or more; types 131 and 132 give 48 at frame
 * 2, 96 at 3, 64 at 4, 32 at 5 and 16 at any other; any other type gives
 * g_flightBrightnessScaleQ8 - 256. Every intensity is then multiplied by 8.
 * Sets the count to 0 and stops when g_localLightsEnabled is 0. */
// FUNCTION: XVT 0x44F880
void FlightLight_SetupObjectLighting(struct ObjectRecord *object)
{
	int lightCount;
	int objectIdx;
	unsigned int maxDistance;
	int worldX;
	int worldY;
	int worldZ;
	struct ObjectRecord *lightObject;

	g_objectPointLightCount = 0;
	if (g_localLightsEnabled == 0) {
		return;
	}
	maxDistance =
		g_objectTypeTable[object->objectType].maxBoundsExtent + 0x4000;
	worldX = object->world_x;
	worldY = object->world_y;
	worldZ = object->world_z;
	lightObject = g_objectTable;
	lightCount = 0;
	objectIdx = 0;
	if (g_regionMainObjectSlotEnd > 0) {
		do {
			int deltaX;
			int deltaY;
			int deltaZ;

			if (lightObject->objectType != 0 &&
			    lightObject->genusId == CRAFT_GENUS_EXPLOSION) {
				deltaX = lightObject->world_x - worldX;
				deltaY = lightObject->world_y - worldY;
				deltaZ = lightObject->world_z - worldZ;
				if ((unsigned int)collide_roughdistance3d(
					    deltaX, deltaY, deltaZ) <
				    maxDistance) {
					if (object->mobj != NULL) {
						g_objectPointLights[lightCount]
							.x = Math_Dot3Q15(
							deltaX, deltaY, deltaZ,
							object->mobj
								->cachedSideX,
							object->mobj
								->cachedSideY,
							object->mobj
								->cachedSideZ);
						g_objectPointLights[lightCount]
							.y = -Math_Dot3Q15(
							deltaX, deltaY, deltaZ,
							object->mobj
								->cachedFwdX,
							object->mobj
								->cachedFwdY,
							object->mobj
								->cachedFwdZ);
						g_objectPointLights[lightCount]
							.z = Math_Dot3Q15(
							deltaX, deltaY, deltaZ,
							object->mobj->cachedUpX,
							object->mobj->cachedUpY,
							object->mobj
								->cachedUpZ);
					} else {
						FVIEW_SetObjectTransform(
							object->roll,
							object->pitch,
							object->yaw, 0, NULL);
						g_objectPointLights[lightCount]
							.x = Math_Dot3Q15(
							deltaX, deltaY, deltaZ,
							g_fviewSideX_Q15,
							g_fviewSideY_Q15,
							g_fviewSideZ_Q15);
						g_objectPointLights[lightCount]
							.y = -Math_Dot3Q15(
							deltaX, deltaY, deltaZ,
							g_fviewForwardX_Q15,
							g_fviewForwardY_Q15,
							g_fviewForwardZ_Q15);
						g_objectPointLights[lightCount]
							.z = Math_Dot3Q15(
							deltaX, deltaY, deltaZ,
							g_fviewUpX_Q15,
							g_fviewUpY_Q15,
							g_fviewUpZ_Q15);
					}

					g_objectPointLights[lightCount]
						.intensity = 16;
					switch (lightObject->objectType) {
					case 127:
					case 128:
					case 129:
					case 130:
						switch (lightObject
								->typeSpecificByte
									[0]) {
						case 2:
							g_objectPointLights
								[lightCount]
									.intensity =
								192;
							break;
						case 3:
							g_objectPointLights
								[lightCount]
									.intensity =
								320;
							break;
						case 4:
							g_objectPointLights
								[lightCount]
									.intensity =
								480;
							break;
						case 5:
						case 6:
						case 7:
						case 8:
							g_objectPointLights
								[lightCount]
									.intensity =
								320;
							break;
						case 9:
							g_objectPointLights
								[lightCount]
									.intensity =
								192;
							break;
						case 10:
							g_objectPointLights
								[lightCount]
									.intensity =
								96;
							break;
						case 11:
							g_objectPointLights
								[lightCount]
									.intensity =
								48;
							break;
						default:
							break;
						}
						if (lightObject->mobj != NULL &&
						    lightObject->mobj
								    ->effectSize >=
							    4) {
							g_objectPointLights
								[lightCount]
									.intensity *=
								(lightObject
									 ->mobj
									 ->effectSize +
								 4) /
								4;
						}
						break;
					case 131:
					case 132:
						switch (lightObject
								->typeSpecificByte
									[0]) {
						case 2:
							g_objectPointLights
								[lightCount]
									.intensity =
								48;
							break;
						case 3:
							g_objectPointLights
								[lightCount]
									.intensity =
								96;
							break;
						case 4:
							g_objectPointLights
								[lightCount]
									.intensity =
								64;
							break;
						case 5:
							g_objectPointLights
								[lightCount]
									.intensity =
								32;
							break;
						default:
							break;
						}
						break;
					default:
						g_objectPointLights[lightCount]
							.intensity =
							g_flightBrightnessScaleQ8 -
							256;
						break;
					}
					g_objectPointLights[lightCount]
						.intensity *= 8;
					++lightCount;
					if (lightCount == 8) {
						break;
					}
				}
			}
			++objectIdx;
			++lightObject;
		} while (objectIdx < g_regionMainObjectSlotEnd);
	}
	g_objectPointLightCount = lightCount;
}

/* Sets g_objectPointLightCount to 0, then, with g_localLightsEnabled set and
 * objectIndex under g_regionMainObjectSlotEnd + g_regionStaticObjectSlotCount,
 * calls FlightLight_SetupObjectLighting for that object. Hud_Update3DCrt is its
 * only caller. */
// FUNCTION: XVT 0x44FDF0
void FlightLight_SetupObjectLightingByIndex(unsigned int objectIndex)
{
	g_objectPointLightCount = 0;
	if (g_localLightsEnabled != 0 &&
	    (unsigned int)(g_regionMainObjectSlotEnd +
			   g_regionStaticObjectSlotCount) > objectIndex) {
		FlightLight_SetupObjectLighting(&g_objectTable[objectIndex]);
	}
}
