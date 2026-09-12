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

// GLOBAL: XVT 0x5235F0
int g_objectPointLightCount = 0;
// GLOBAL: XVT 0x9FD3B0
ObjectPointLight g_objectPointLights[10] = { { 0 } };

// GLOBAL: XVT 0x51C00C
static ObjectRecord* g_swFaceLightCachedObject;
// GLOBAL: XVT 0x51C014
static SceneFace* g_swFaceLightCachedFace;
// GLOBAL: XVT 0x51C010
static int g_swFaceLightCachedPointLightCount = 0;
// GLOBAL: XVT 0x550C10
static OptVector g_swFaceLightPointPositions[10] = { { 0.0f, 0.0f, 0.0f } };
// GLOBAL: XVT 0x550BE0
static float g_swFaceLightPointIntensities[10] = { 0.0f };
// GLOBAL: XVT 0x550C00
static OptVector g_swFaceLightDir = { 0.0f, 0.0f, 0.0f };
// GLOBAL: XVT 0x550C70
static OptVector g_swFaceLightFaceNormal = { 0.0f, 0.0f, 0.0f };

// FLAGS: /O2 /G5
// FUNCTION: XVT 0x4206A0
void FlightLight_ResetSoftwareFaceSampleCache(void) {
	g_swFaceLightCachedObject = 0;
	g_swFaceLightCachedFace = 0;
}

// FUNCTION: XVT 0x4206B0
float FlightLight_ComputeSoftwareFaceSampleIntensity(SceneFace* face, int screenX, int screenY,
													 float projectedDepth) {
	SceneMesh* mesh;
	OptVector normal;
	OptVector vector;
	float inverseDepth;
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
		for (lightIndex = 0; g_objectPointLightCount > lightIndex; ++lightIndex) {
			OptVector* position = &g_swFaceLightPointPositions[lightIndex];

			position->x = (float)g_objectPointLights[lightIndex].x;
			position->y = (float)g_objectPointLights[lightIndex].y;
			position->z = (float)g_objectPointLights[lightIndex].z;
			Math3D_RotateVec3(&position->x, mesh->viewOrient);
			position->x += mesh->viewPosX;
			position->y += mesh->viewPosY;
			position->z += mesh->viewPosZ;
			g_swFaceLightPointIntensities[lightIndex] = (float)g_objectPointLights[lightIndex].intensity;
		}
		g_swFaceLightDir.x = (float)g_objectLightDirectionX * g_renderLightDirectionUnitScale;
		g_swFaceLightDir.y = (float)g_objectLightDirectionY * g_renderLightDirectionUnitScale;
		g_swFaceLightDir.z = (float)g_objectLightDirectionZ * g_renderLightDirectionUnitScale;
		Math3D_RotateVec3(&g_swFaceLightDir.x, mesh->viewOrient);
	}

	sampleZ = 1.0f / projectedDepth;
	inverseDepth = sampleZ * g_invProjScale;
	sampleX = (float)(screenX - (g_flightVpWidth >> 1)) * inverseDepth;
	sampleY = (float)(screenY - (g_flightVpHeight >> 1) - g_projOffsetY) * inverseDepth;
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
			distance = componentX * 0.92640001f + (componentZ + componentY) * 0.3872f;
		} else if (componentY >= componentX && componentZ <= componentY) {
			distance = componentY * 0.92640001f + (componentZ + componentX) * 0.3872f;
		} else {
			distance = componentZ * 0.92640001f + (componentY + componentX) * 0.3872f;
		}
		reciprocal = 1.0f / distance;
		vector.x *= reciprocal;
		vector.y *= reciprocal;
		vector.z *= reciprocal;
		specular = (g_swFaceLightDir.x + vector.x) * normal.x + (g_swFaceLightDir.y + vector.y) * normal.y +
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

	for (lightIndex = 0; lightIndex < g_swFaceLightCachedPointLightCount; ++lightIndex) {
		const OptVector* position = &g_swFaceLightPointPositions[lightIndex];

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
			if (componentY <= componentX && componentZ <= componentX) {
				distance = componentX + (componentZ + componentY) * g_renderRoughDistanceScale;
			} else if (componentY >= componentX && componentZ <= componentY) {
				distance = componentY + (componentZ + componentX) * g_renderRoughDistanceScale;
			} else {
				distance = componentZ + (componentY + componentX) * g_renderRoughDistanceScale;
			}
			lightDot = lightDot / (distance * distance);
			if (g_specularEnabled != 0) {
				float halfDot;
				float cosine;

				dx -= sampleX;
				dy -= sampleY;
				dz -= sampleZ;
				halfDot = (normal.x * dx + normal.y * dy + normal.z * dz) * g_renderHalfFloat;
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
				if (componentY <= componentX && componentZ <= componentX) {
					distance = componentX * g_renderSpecularApproxMaxComponentScale +
							   (componentZ + componentY) * g_renderSpecularApproxOtherComponentsScale;
				} else if (componentY >= componentX && componentZ <= componentY) {
					distance = componentY * g_renderSpecularApproxMaxComponentScale +
							   (componentZ + componentX) * g_renderSpecularApproxOtherComponentsScale;
				} else {
					distance = componentZ * g_renderSpecularApproxMaxComponentScale +
							   (componentY + componentX) * g_renderSpecularApproxOtherComponentsScale;
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
				intensity += g_swFaceLightPointIntensities[lightIndex] * contribution;
				if (intensity >= 1.0f) {
					intensity = 1.0f;
					break;
				}
			}
		}
	}
	return intensity;
}

// FUNCTION: XVT 0x44F880
void FlightLight_SetupObjectLighting(ObjectRecord* object) {
	int lightCount;
	int objectIdx;
	unsigned int maxDistance;
	int worldX;
	int worldY;
	int worldZ;
	ObjectRecord* lightObject;

	g_objectPointLightCount = 0;
	if (g_localLightsLevel == 0)
		return;
	maxDistance = g_modelTypeTable[object->objectType].maxBoundsExtent + 0x4000;
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

			if (lightObject->objectType != 0 && lightObject->genusId == CRAFT_GENUS_EXPLOSION) {
				deltaX = lightObject->world_x - worldX;
				deltaY = lightObject->world_y - worldY;
				deltaZ = lightObject->world_z - worldZ;
				if ((unsigned int)collide_roughdistance3d(deltaX, deltaY, deltaZ) < maxDistance) {
					if (object->mobj != NULL) {
						g_objectPointLights[lightCount].x =
							Math_Dot3Q15(deltaX, deltaY, deltaZ, object->mobj->cachedSideX,
										 object->mobj->cachedSideY, object->mobj->cachedSideZ);
						g_objectPointLights[lightCount].y =
							-Math_Dot3Q15(deltaX, deltaY, deltaZ, object->mobj->cachedFwdX,
										  object->mobj->cachedFwdY, object->mobj->cachedFwdZ);
						g_objectPointLights[lightCount].z =
							Math_Dot3Q15(deltaX, deltaY, deltaZ, object->mobj->cachedUpX,
										 object->mobj->cachedUpY, object->mobj->cachedUpZ);
					} else {
						FVIEW_SetObjectTransform(object->roll, object->pitch, object->yaw, 0, NULL);
						g_objectPointLights[lightCount].x = Math_Dot3Q15(
							deltaX, deltaY, deltaZ, g_fviewSideX_Q15, g_fviewSideY_Q15, g_fviewSideZ_Q15);
						g_objectPointLights[lightCount].y =
							-Math_Dot3Q15(deltaX, deltaY, deltaZ, g_fviewForwardX_Q15, g_fviewForwardY_Q15,
										  g_fviewForwardZ_Q15);
						g_objectPointLights[lightCount].z = Math_Dot3Q15(
							deltaX, deltaY, deltaZ, g_fviewUpX_Q15, g_fviewUpY_Q15, g_fviewUpZ_Q15);
					}

					g_objectPointLights[lightCount].intensity = 16;
					switch (lightObject->objectType) {
						case 127:
						case 128:
						case 129:
						case 130:
							switch (lightObject->typeSpecificByte[0]) {
								case 2:
									g_objectPointLights[lightCount].intensity = 192;
									break;
								case 3:
									g_objectPointLights[lightCount].intensity = 320;
									break;
								case 4:
									g_objectPointLights[lightCount].intensity = 480;
									break;
								case 5:
								case 6:
								case 7:
								case 8:
									g_objectPointLights[lightCount].intensity = 320;
									break;
								case 9:
									g_objectPointLights[lightCount].intensity = 192;
									break;
								case 10:
									g_objectPointLights[lightCount].intensity = 96;
									break;
								case 11:
									g_objectPointLights[lightCount].intensity = 48;
									break;
								default:
									break;
							}
							if (lightObject->mobj != NULL && lightObject->mobj->lightIntensityScale >= 4)
								g_objectPointLights[lightCount].intensity *=
									(lightObject->mobj->lightIntensityScale + 4) / 4;
							break;
						case 131:
						case 132:
							switch (lightObject->typeSpecificByte[0]) {
								case 2:
									g_objectPointLights[lightCount].intensity = 48;
									break;
								case 3:
									g_objectPointLights[lightCount].intensity = 96;
									break;
								case 4:
									g_objectPointLights[lightCount].intensity = 64;
									break;
								case 5:
									g_objectPointLights[lightCount].intensity = 32;
									break;
								default:
									break;
							}
							break;
						default:
							g_objectPointLights[lightCount].intensity = g_flightBrightnessScaleQ8 - 256;
							break;
					}
					g_objectPointLights[lightCount].intensity *= 8;
					++lightCount;
					if (lightCount == 8)
						break;
				}
			}
			++objectIdx;
			++lightObject;
		} while (objectIdx < g_regionMainObjectSlotEnd);
	}
	g_objectPointLightCount = lightCount;
}

// FUNCTION: XVT 0x44FDF0
void FlightLight_SetupObjectLightingByIndex(unsigned int objectIndex) {
	g_objectPointLightCount = 0;
	if (g_localLightsLevel != 0 &&
		(unsigned int)(g_regionMainObjectSlotEnd + g_regionStaticObjectSlotCount) > objectIndex)
		FlightLight_SetupObjectLighting(&g_objectTable[objectIndex]);
}
