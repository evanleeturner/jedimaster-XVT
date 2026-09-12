#include "xvt/render/render_list.h"

#include "xvt/flight/flight_view.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/flight/transfm2.h"

// GLOBAL: XVT 0x9A7B5C
static int g_renderObjectListCount;
// GLOBAL: XVT 0x9A8C1C
RenderObjectListEntry* g_renderListHead;
// GLOBAL: XVT 0x9EC5F8
RenderObjectListEntry* g_renderObjectListEntries = 0;

// FLAGS: /O2 /G5
// FUNCTION: XVT 0x4362A0
void RenderList_QueueObject(int objectIdx, int sortDepth) {
	if (g_renderObjectListCount < 296) {
		g_renderObjectListEntries[g_renderObjectListCount].sortDepth = sortDepth;
		g_renderObjectListEntries[g_renderObjectListCount].objectIdx = objectIdx;
		g_renderObjectListEntries[g_renderObjectListCount].next = g_renderListHead;
		g_renderListHead = &g_renderObjectListEntries[g_renderObjectListCount];
		++g_renderObjectListCount;
	}
}

// FUNCTION: XVT 0x436310
void RenderList_Reset(void) {
	g_renderObjectListCount = 0;
	g_renderListHead = 0;
}

// FUNCTION: XVT 0x436470
int RenderList_ProjectObjectBoundsForCulling(int objectIdx, unsigned int boundsRadius, int playerIdx) {
	ObjectRecord* object;
	int savedTargetY;
	int savedTargetZ;
	int absViewCoord;
	int farZ;
	int cullRadius;

	object = &g_objectTable[objectIdx];
	savedTargetY = g_players[playerIdx].viewState.savedTargetY;
	g_camRelWorldX = object->world_x - g_players[playerIdx].viewState.savedTargetX;
	savedTargetZ = g_players[playerIdx].viewState.savedTargetZ;
	g_camRelWorldY = object->world_y - savedTargetY;
	g_camRelWorldZ = object->world_z - savedTargetZ;
	depthZ = TRANSFM2_CamMatDotRow2(g_camRelWorldX, g_camRelWorldY, g_camRelWorldZ);
	cullRadius = (int)boundsRadius;
	farZ = (int)((unsigned int)depthZ + (unsigned int)cullRadius);
	if (farZ < 0) {
		return 0;
	}
	if ((unsigned int)(farZ >> 4) > (unsigned int)cullRadius) {
		cullRadius = farZ >> 4;
	}

	viewX = TRANSFM2_CamMatDotRow0(g_camRelWorldX, g_camRelWorldY, g_camRelWorldZ);
	absViewCoord = viewX;
	if (absViewCoord < 0) {
		absViewCoord = (int)(0U - (unsigned int)absViewCoord);
	}
	absViewCoord = (int)((unsigned int)absViewCoord - (unsigned int)cullRadius);
	if (farZ < absViewCoord) {
		return 0;
	}

	viewY = TRANSFM2_CamMatDotRow1(g_camRelWorldX, g_camRelWorldY, g_camRelWorldZ);
	absViewCoord = viewY;
	if (absViewCoord < 0) {
		absViewCoord = (int)(0U - (unsigned int)absViewCoord);
	}
	absViewCoord = (int)((unsigned int)absViewCoord - (unsigned int)cullRadius);
	return farZ >= absViewCoord;
}

// FUNCTION: XVT 0x436580
void RenderList_SortDepthDescending(void) {
	RenderObjectListEntry* left;
	RenderObjectListEntry* right;
	RenderObjectListEntry* previous;
	RenderObjectListEntry* leftTail;
	int runLength;
	int leftRunCount;
	int rightDepth;
	int rightRunCount;
	int processedCount;
	int objectCount;
	int leftDepth;

	objectCount = g_renderObjectListCount;
	runLength = 1;
	if (objectCount > runLength) {
		do {
			right = g_renderListHead;
			previous = 0;
			left = g_renderListHead;
			processedCount = 0;
			while (processedCount < g_renderObjectListCount) {
				leftRunCount = 0;
				while (leftRunCount < runLength && right != 0) {
					leftTail = right;
					++leftRunCount;
					right = right->next;
				}
				if (right == 0) {
					break;
				}

				rightRunCount = 0;
				while (rightRunCount < runLength) {
					rightDepth = right->sortDepth;
					leftDepth = left->sortDepth;
					while (leftDepth >= rightDepth) {
						previous = left;
						left = left->next;
						if (leftTail == previous) {
							break;
						}
						leftDepth = left->sortDepth;
					}
					if (leftTail == previous) {
						break;
					}

					leftTail->next = right->next;
					if (previous != 0) {
						previous->next = right;
						previous = right;
						right->next = left;
					} else {
						g_renderListHead = right;
						right->next = left;
						previous = g_renderListHead;
					}
					right = leftTail->next;
					if (right == 0) {
						break;
					}
					++rightRunCount;
				}

				if (leftTail == previous) {
					while (rightRunCount < runLength && right != 0) {
						leftTail = right;
						++rightRunCount;
						right = right->next;
					}
				}
				left = right;
				previous = leftTail;
				if (right == 0) {
					break;
				}
				processedCount += 2 * runLength;
			}
			runLength *= 2;
			objectCount = g_renderObjectListCount;
		} while (objectCount > runLength);
	}
}

// FUNCTION: XVT 0x436680
void RenderList_SortDepthAscending(void) {
	int runLength;
	RenderObjectListEntry* leftTail;
	RenderObjectListEntry* right;
	RenderObjectListEntry* previous;
	RenderObjectListEntry* left;
	int leftRunCount;
	int rightRunCount;
	int processedCount;

	for (runLength = 1; runLength < g_renderObjectListCount; runLength *= 2) {
		right = g_renderListHead;
		previous = 0;
		left = g_renderListHead;
		processedCount = 0;
		while (processedCount < g_renderObjectListCount) {
			leftRunCount = 0;
			while (leftRunCount < runLength && right != 0) {
				leftTail = right;
				++leftRunCount;
				right = right->next;
			}
			if (right == 0) {
				break;
			}

			rightRunCount = 0;
			while (rightRunCount < runLength) {
				while (left->sortDepth <= right->sortDepth) {
					previous = left;
					left = left->next;
					if (leftTail == previous) {
						break;
					}
				}
				if (leftTail == previous) {
					break;
				}

				leftTail->next = right->next;
				if (previous != 0) {
					previous->next = right;
					previous = right;
					right->next = left;
				} else {
					g_renderListHead = right;
					right->next = left;
					previous = g_renderListHead;
				}
				right = leftTail->next;
				if (right == 0) {
					break;
				}
				++rightRunCount;
			}

			if (leftTail == previous) {
				while (rightRunCount < runLength && right != 0) {
					leftTail = right;
					++rightRunCount;
					right = right->next;
				}
			}
			left = right;
			previous = leftTail;
			if (right == 0) {
				break;
			}
			processedCount += 2 * runLength;
		}
	}
}
