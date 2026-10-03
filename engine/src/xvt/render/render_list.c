#include "xvt/render/render_list.h"

#include "xvt/flight/flight_view.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/flight/transfm2.h"

/* Entries queued in g_renderObjectListEntries since the last RenderList_Reset,
 * 0 to 296; only RenderList_QueueObject and RenderList_Reset write it. */
// GLOBAL: XVT 0x9A7B5C
static int g_renderObjectListCount;
/* First entry of the render list, linked through each entry's next; NULL when
 * the list is empty. RenderList_QueueObject puts each new entry first, the two
 * sorts reorder the list and RenderList_Reset sets NULL;
 * FlightMap_DrawObjectPass walks the list by moving it on and puts it back
 * after. */
// GLOBAL: XVT 0x9A8C1C
struct RenderObjectListEntry *g_renderListHead;
/* Storage for the render list, 296 entries (RENDER_OBJECT_LIST_CAPACITY) used
 * in the order they are queued; FeDiskIo_InitGlobalBuffers and
 * FeDiskIo_LockGlobalBuffers lock it from its memory handle. */
// GLOBAL: XVT 0x9EC5F8
struct RenderObjectListEntry *g_renderObjectListEntries = 0;

/* Adds an object to the front of the render list with its sort depth, taking
 * the next of the 296 entries. Does nothing when all 296 are used. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x4362A0
void RenderList_QueueObject(int objectIdx, int sortDepth)
{
	if (g_renderObjectListCount < 296) {
		g_renderObjectListEntries[g_renderObjectListCount].sortDepth =
			sortDepth;
		g_renderObjectListEntries[g_renderObjectListCount].objectIdx =
			objectIdx;
		g_renderObjectListEntries[g_renderObjectListCount].next =
			g_renderListHead;
		g_renderListHead =
			&g_renderObjectListEntries[g_renderObjectListCount];
		++g_renderObjectListCount;
	}
}

/* Empties the render list: g_renderObjectListCount to 0 and g_renderListHead to
 * NULL. */
// FUNCTION: XVT 0x436310
void RenderList_Reset(void)
{
	g_renderObjectListCount = 0;
	g_renderListHead = 0;
}

/* Tells whether an object's bounds can be in the view of player
 * playerIdx's camera. Stores the object's offset from the camera in
 * g_camRelWorldX, Y and Z, its view depth in g_viewSpaceDepth and its view
 * X in g_viewSpaceX, and its view Y in g_viewSpaceY once the X test
 * passes. With far = depth + boundsRadius and r the larger of boundsRadius
 * and far >> 4, it returns 0 when far is negative, when the size of view X
 * less r exceeds far, or when the size of view Y less r does; else 1. */
// FUNCTION: XVT 0x436470
int RenderList_ProjectObjectBoundsForCulling(int objectIdx,
					     unsigned int boundsRadius,
					     int playerIdx)
{
	struct ObjectRecord *object;
	int cameraWorldY;
	int cameraWorldZ;
	int absViewCoord;
	int farZ;
	int cullRadius;

	object = &g_objectTable[objectIdx];
	cameraWorldY = g_players[playerIdx].viewState.cameraWorldY;
	g_camRelWorldX =
		object->world_x - g_players[playerIdx].viewState.cameraWorldX;
	cameraWorldZ = g_players[playerIdx].viewState.cameraWorldZ;
	g_camRelWorldY = object->world_y - cameraWorldY;
	g_camRelWorldZ = object->world_z - cameraWorldZ;
	g_viewSpaceDepth = TRANSFM2_CamMatDotRow2(
		g_camRelWorldX, g_camRelWorldY, g_camRelWorldZ);
	cullRadius = (int)boundsRadius;
	farZ = (int)((unsigned int)g_viewSpaceDepth + (unsigned int)cullRadius);
	if (farZ < 0) {
		return 0;
	}
	if ((unsigned int)(farZ >> 4) > (unsigned int)cullRadius) {
		cullRadius = farZ >> 4;
	}

	g_viewSpaceX = TRANSFM2_CamMatDotRow0(g_camRelWorldX, g_camRelWorldY,
					      g_camRelWorldZ);
	absViewCoord = g_viewSpaceX;
	if (absViewCoord < 0) {
		absViewCoord = (int)(0U - (unsigned int)absViewCoord);
	}
	absViewCoord =
		(int)((unsigned int)absViewCoord - (unsigned int)cullRadius);
	if (farZ < absViewCoord) {
		return 0;
	}

	g_viewSpaceY = TRANSFM2_CamMatDotRow1(g_camRelWorldX, g_camRelWorldY,
					      g_camRelWorldZ);
	absViewCoord = g_viewSpaceY;
	if (absViewCoord < 0) {
		absViewCoord = (int)(0U - (unsigned int)absViewCoord);
	}
	absViewCoord =
		(int)((unsigned int)absViewCoord - (unsigned int)cullRadius);
	return farZ >= absViewCoord;
}

/* Sorts the render list by sortDepth, largest first, merging runs of 1, 2, 4
 * and so on in place; entries of equal depth keep their order. Only
 * FlightMap_RenderView calls it. */
// FUNCTION: XVT 0x436580
void RenderList_SortDepthDescending(void)
{
	struct RenderObjectListEntry *left;
	struct RenderObjectListEntry *right;
	struct RenderObjectListEntry *previous;
	struct RenderObjectListEntry *leftTail;
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
					while (rightRunCount < runLength &&
					       right != 0) {
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

/* Sorts the render list by sortDepth, smallest first, the same way as
 * RenderList_SortDepthDescending. Only FlightView_Render calls it. */
// FUNCTION: XVT 0x436680
void RenderList_SortDepthAscending(void)
{
	int runLength;
	struct RenderObjectListEntry *leftTail;
	struct RenderObjectListEntry *right;
	struct RenderObjectListEntry *previous;
	struct RenderObjectListEntry *left;
	int leftRunCount;
	int rightRunCount;
	int processedCount;

	for (runLength = 1; runLength < g_renderObjectListCount;
	     runLength *= 2) {
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
				while (rightRunCount < runLength &&
				       right != 0) {
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
