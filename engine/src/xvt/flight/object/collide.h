#ifndef XVT_FLIGHT_OBJECT_COLLIDE_H
#define XVT_FLIGHT_OBJECT_COLLIDE_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const float g_collideZeroFloat;
extern int g_collisionSegmentStartWorldX;
extern int g_collisionSegmentStartWorldY;
extern int g_collisionSegmentStartWorldZ;
extern int g_collisionProbeWorldX;
extern int g_collisionProbeWorldY;
extern int g_collisionProbeWorldZ;
extern int g_collisionSweepStartX;
extern int g_collisionSweepStartY;
extern int g_collisionSweepStartZ;
extern int g_collisionSweepEndX;
extern int g_collisionSweepEndY;
extern int g_collisionSweepEndZ;
extern int g_approxDist;
extern int g_collisionIsAimPrediction;
extern int g_collisionHitOffsetX;
extern int g_collisionHitOffsetY;
extern int g_collisionHitOffsetZ;
extern int g_collideSweepRejectNearStartHits;
extern int g_turretFireHullMeshOrdinal;

void collide_PopulateMobileObjectProximityCandidates(
	struct MobileObjectProximityList *list, uint16_t ownerObjIdx);
void collide_collisions(void);
void collide_InsertMobileObjectProximityCandidate(
	struct MobileObjectProximityList *list, uint16_t ownerObjIdx,
	uint16_t candidateObjIdx);
int collide_GetMobileObjectProximitySpeedQ12(uint16_t objIdx);
void collide_ResetObjectProximityForSlot(uint16_t objIdx);
void collide_ResetNeighborProximityLists(uint16_t objectIndex);
void collide_RemoveMobileObjectProximityCandidate(
	struct MobileObjectProximityList *list, uint16_t candidateObjIdx);
void collide_applyCraftImpactBounce(uint16_t craftObjIdx, uint16_t otherObjIdx);
int16_t collide_TestSweptPairCollision(uint16_t sourceObjIdx,
				       uint16_t targetObjIdx);
int16_t collide_checkboxcollision(int radius);
int collide_WouldShotHitTarget(uint16_t sourceObjIdx, uint16_t targetObjIdx,
			       uint16_t hardpointIndex);
uint16_t collide_craftstarshipcollision(uint16_t sourceObjIdx,
					int16_t lookaheadSeconds);
void collide_laserhitcraft(uint16_t projectileObjIdx, uint16_t craftObjIdx,
			   int16_t hitMeshIndex);
int16_t collide_damagecraft(uint16_t victimObjIdx, int16_t hitMeshIndex,
			    uint16_t sourceObjIdx,
			    uint16_t hitSideOrDamageAmount);
int collide_ConvertObjectToExplosion(unsigned int objectIndex,
				     uint8_t explosionObjectType);
unsigned int collide_roughdistance3du(unsigned int abs_dx, unsigned int abs_dy,
				      unsigned int abs_dz);
int collide_roughdistance3d(int dx, int dy, int dz);
unsigned int collide_TestSegmentAgainstLegacyPackedOptNode(
	const uint8_t *nodeData, int startX, int startY, int startZ, int endX,
	int endY, int endZ, int stopOnFirstHit);
unsigned int collide_ComputeCraftDamageAmount(uint16_t victimObjIdx,
					      uint16_t sourceObjIdx);
int collide_IsLegacyProjectedEdgeCrossNonpositive(int pointDeltaU,
						  int edgeDeltaV,
						  int pointDeltaV,
						  int edgeDeltaU);
int collide_CheckSweptModelCollision(uint16_t sourceObjIdx,
				     uint16_t targetObjIdx);
int collide_TestSweepAgainstOptNode(struct OptimizedPolyObject *object,
				    struct OptNode *node);
int collide_IntersectSegmentWithFacePlane(const float *faceNormal,
					  const float *faceVertex,
					  const float *segmentStart,
					  const float *segmentEnd, float *outT);
int collide_PointInFacePolygon(const float *faceNormal,
			       const float *vertexCoords,
			       const int32_t *faceVertexIndices,
			       float *projectedPoint);
void collide_ApplyEngineWashDamage(int victimObjIdx, int sourceObjIdx);
void collide_ApplyHostileProximityWeaponDisruption(int ownerObjIdx,
						   int hostileObjIdx);

#ifdef __cplusplus
}
#endif

#endif
