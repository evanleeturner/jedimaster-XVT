#ifndef XVT_RENDER_RENDER_CLIP_H
#define XVT_RENDER_RENDER_CLIP_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One polygon corner as the clip functions read and write it; laid out as
 * ProjVertex, whose list RenderScene_DrawMeshFaces clips in place. */
struct RenderClipVertex {
	/* Screen x in pixels; view-space x while scaledInverseDepth is
	 * negative. */
	float x;
	float y; /* Screen y in pixels; view-space y while that is negative. */
	/* g_projScaleInt over the view depth; for a corner closer than depth 1,
	 * the depth minus 1, which is negative. */
	float scaledInverseDepth;
	/* Light level, 0 to 1 as RenderScene_ComputeVertexLighting sets it;
	 * RenderScene_EmitFlightVertex turns it into the corner's gray. */
	float lightIntensity;
	float u; /* Horizontal texture coordinate. */
	float v; /* Vertical texture coordinate. */
};

extern int g_clipIdxA[32];
extern int g_clipIdxB[32];
extern int g_clipCountA;
extern int g_clipCountB;
extern int g_clipVertCursor;
extern float g_invProjScale;

int RenderClip_ClipPolyTop(int prevVertIndex, int curVertIndex,
			   RenderClipVertex *vertices);
void RenderClip_ClipPolyBottom(int prevVertIndex, int curVertIndex,
			       RenderClipVertex *vertices);
int RenderClip_ClipPolyLeft(int prevVertIndex, int curVertIndex,
			    RenderClipVertex *vertices);
void RenderClip_ClipPolyRight(int prevVertIndex, int curVertIndex,
			      RenderClipVertex *vertices);
void RenderClip_ClipPolyNear(int prevVertIndex, int curVertIndex,
			     RenderClipVertex *vertices);

#ifdef __cplusplus
}
#endif

#endif
