#ifndef XVT_RENDER_RENDER_CLIP_H
#define XVT_RENDER_RENDER_CLIP_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct RenderClipVertex {
	float x;
	float y;
	float z;
	float rhw;
	float u;
	float v;
};

extern int g_clipIdxA[32];
extern int g_clipIdxB[32];
extern int g_clipCountA;
extern int g_clipCountB;
extern int g_clipVertCursor;
extern float g_invProjScale;

int RenderClip_ClipPolyTop(int prevVertIndex, int curVertIndex, RenderClipVertex* vertices);
void RenderClip_ClipPolyBottom(int prevVertIndex, int curVertIndex, RenderClipVertex* vertices);
int RenderClip_ClipPolyLeft(int prevVertIndex, int curVertIndex, RenderClipVertex* vertices);
void RenderClip_ClipPolyRight(int prevVertIndex, int curVertIndex, RenderClipVertex* vertices);
void RenderClip_ClipPolyNear(int prevVertIndex, int curVertIndex, RenderClipVertex* vertices);

#ifdef __cplusplus
}
#endif

#endif
