#include "xvt/render/render_scene.h"
#ifdef XVT_MODERN
#include "aeron/compat/host.h"
#include "xvt_runtime/assets/opt_native.h"
#endif

#include "xvt/assets/model_mesh.h"
#include "xvt/assets/model_preview.h"
#include "xvt/assets/model_texture.h"
#include "xvt/assets/opt_model.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/fediskio.h"
#include "xvt/flight/flight_display.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/flight/targeting.h"
#include "xvt/flight/transfm2.h"
#include "xvt/math/math.h"
#include "xvt/math/math3d.h"
#include "xvt/render/flight_light.h"
#include "xvt/render/flight_palette.h"
#include "xvt/render/flight_sw.h"
#include "xvt/render/render_clip.h"
#include "xvt/render/render_texture.h"
#include "xvt/render/renderer.h"
#include "xvt/render/scene_billboard.h"
#include "xvt/render/std3d.h"
#include "xvt/render/sw3d.h"
#include "xvt/util/debug_console.h"
#include "xvt/util/memory.h"
#include <string.h>
#ifndef XVT_MODERN
#include <float.h>
#endif

enum {
	OPT_INDEXED_SHADE_TABLE_SIZE = 4096,
	DEFAULT_WHITE_TEXTURE_DIMENSION = 8,
	DEFAULT_WHITE_TEXTURE_RGB_SIZE = DEFAULT_WHITE_TEXTURE_DIMENSION *
					 DEFAULT_WHITE_TEXTURE_DIMENSION * 3,
	B_WING_OBJECT_TYPE = 4,
	COMPONENT_OBJECT_TYPE = 89,
};

/* 1 while FlightView_Render calls sw3d_DrawVisibleFacesToSurface, so that
 * RenderScene_FlushGeometry, which that calls in the hardware path, also draws
 * the queued target markers and target boxes; else 0. Only FlightView_Render
 * writes it. */
// GLOBAL: XVT 0x51A520
int g_sceneFlushDrawTargetMarkers = 0;

/* Index of the B-wing model's bridge mesh, which RenderScene_DrawObjectModel
 * looks up with ModelMesh_FindBridgeIndex when it draws a craft of object type
 * 4, and keeps once found; -1 until then. Only that function writes it. */
// GLOBAL: XVT 0x5272A0
int g_bwingBridgeMeshIndexCache = -1;
/* 1 when RenderScene_ComputeVertexLighting asks whether the object's own model
 * blocks a light from a vertex. Starts at 0; only
 * RenderScene_ToggleVertexLightOcclusion changes it, and nothing calls that, so
 * the test never runs. */
// GLOBAL: XVT 0x5272A4
static int g_vertexLightOcclusionEnabled;
/* Radians per unit of a craft's meshRotation byte: 2 pi over 256, to float
 * precision. */
// GLOBAL: XVT 0x5181D0
const float g_meshRotationByteToRadiansScale = 0.024543673f;
/* 1 over 32767: turns the Q15 camera and object matrices into floats. */
// GLOBAL: XVT 0x5181D8
const float g_renderMatrixQ15ToFloatScale = 0.000030518509f;
/* 1 over 32768: turns a rotate-and-scale node's axis into floats. */
// GLOBAL: XVT 0x5181DC
const float g_optAxisQ15ToFloatScale = 0.000030517578125f;
/* 100000: RenderScene_ProjectDistantMeshVertices adds it to every vertex's
 * depth and multiplies the projection by it. */
// GLOBAL: XVT 0x51807C
const float g_renderDistantDepth = 100000.0f;
/* 0.0; RenderScene_ProjectMeshVertices compares a face's texture area with it
 * to take its magnitude. */
// GLOBAL: XVT 0x518054
const float g_renderProjectionZeroFloat = 0.0f;
/* 1.0: the view depth of the near plane in RenderScene_ProjectMeshVertices and
 * RenderClip_ClipPolyNear, and a constant 1 elsewhere. */
// GLOBAL: XVT 0x518060
const float g_renderUnitFloat = 1.0f;
/* 0.5: RenderScene_DrawMeshFaces scales a texture coordinate by it once for
 * each doubling that makes a texture square. */
// GLOBAL: XVT 0x518070
const float g_renderTextureUvHalfScale = 0.5f;
/* 3.0, divided by the sum of a triangle's corner scaledInverseDepth values to
 * give the reciprocal of their mean. */
// GLOBAL: XVT 0x518074
const float g_renderTriangleCornerCount = 3.0f;
/* 4.0, divided by the sum of a quad's corner scaledInverseDepth values to give
 * the reciprocal of their mean. */
// GLOBAL: XVT 0x518078
const float g_renderQuadCornerCount = 4.0f;
/* 1 over 2048: RenderScene_EmitFlightVertex writes 1 / (depth * this + 1) as a
 * vertex's z-buffer value. */
// GLOBAL: XVT 0x518080
const float g_invDepthProjScale = 0.00048828125f;
/* 1 over 32768: turns the Q15 light direction in g_objectLightDirectionX, Y and
 * Z into floats. */
// GLOBAL: XVT 0x518098
const float g_renderLightDirectionUnitScale = 0.000030517578125f;
/* 0.8: RenderScene_ComputeVertexLighting multiplies the directional light's dot
 * product with the normal by it. */
// GLOBAL: XVT 0x51809C
const float g_renderDirectionalLightIntensityScale = 0.80000001f;
/* 0.0 for the lighting code: in RenderScene_ComputeVertexLighting a point light
 * adds to a vertex only when its contribution is over it. */
// GLOBAL: XVT 0x5180A0
const float g_renderZeroFloat = 0.0f;
/* 0.4. Nothing reads it: RenderScene_ComputeVertexLighting writes the same 0.4
 * as a literal when directional light is off. */
// GLOBAL: XVT 0x5180A4
const float g_renderAmbientLightIntensity = 0.40000001f;
/* 0.2941: the lighting code's rough distance is the largest component of the
 * offset plus the other two times this, in place of a square root. */
// GLOBAL: XVT 0x5180A8
const float g_renderRoughDistanceScale = 0.29409999f;
/* -0.3: in the hardware path a point light lights a vertex only when the dot
 * product of the normal and the offset to the light, over the rough distance,
 * is not under this. */
// GLOBAL: XVT 0x5180AC
const float g_renderPointLightFacingThreshold = -0.30000001f;
/* 0.5: in the hardware path RenderScene_ComputeVertexLighting puts half the
 * rough distance in place of the normal's dot product with the offset to a
 * light. */
// GLOBAL: XVT 0x5180B0
const double g_renderHalfDouble = 0.5;
/* 0.5 for the lighting code: RenderScene_ComputeVertexLighting halves the
 * normal's dot product with the specular half vector by it, and adds no
 * specular light when the resulting cosine is under it. */
// GLOBAL: XVT 0x5180B8
const float g_renderHalfFloat = 0.5f;
/* 0.1936: weight of the two smaller components in the rough length of the
 * specular half vector. */
// GLOBAL: XVT 0x5180BC
const float g_renderSpecularApproxOtherComponentsScale = 0.1936f;
/* 0.4632: weight of the largest component in the rough length of the specular
 * half vector. */
// GLOBAL: XVT 0x5180C0
const float g_renderSpecularApproxMaxComponentScale = 0.4632f;
/* The z-buffer surface attached to g_flightBackBuffer: Renderer_InitD3DDevice
 * gets it; std3D_DetachAndReleaseZBufferSurface releases it and sets it to
 * NULL. */
// GLOBAL: XVT 0xA68748
IDirectDrawSurface *g_std3DZBufferSurface;
/* 1 from RenderScene_InitHardwareFrame until the first mesh face or rotated
 * sprite of the frame is queued. While it is 1, RenderScene_EmitFlightVertex
 * gives vertices alpha 0xFE instead of 0xFF, RenderQuad_DrawRotatedSprite gives
 * its sprite the color 0xFEFFFFFF, and RenderScene_DrawMeshFaces adds the
 * alpha-blend flag to its first triangle; those two then set it to 0. */
// GLOBAL: XVT 0x51A54C
int g_capVertexAlpha = 1;
/* Set to 0 by RenderScene_InitHardwareFrame; nothing reads it. */
// GLOBAL: XVT 0x51A550
int g_d3dVertexAlphaStateResetSlot = 0;
/* Triangles g_triBuffer may hold before a batch is drawn, at most 256;
 * RenderScene_InitHardwareFrame sets it from the span buffer's size and the
 * device's buffer size. */
// GLOBAL: XVT 0x52F868
int g_maxBatchTris = 0;
/* Vertices of the hardware path's current batch; RenderScene_InitHardwareFrame
 * points it at the start of the span buffer, g_sceneSpanDataBase. */
// GLOBAL: XVT 0x53F970
D3DTLVERTEX *g_flightVertexBuffer = NULL;
/* Screen y of the viewport's top in the display mode: g_flightVpY plus half of
 * g_displayModeHeight minus g_surfaceHeight; added to every emitted vertex.
 * Only RenderScene_InitHardwareFrame writes it. */
// GLOBAL: XVT 0x53F974
float g_flightVpOriginY = 0.0f;
/* Triangles of the hardware path's current batch; RenderScene_InitHardwareFrame
 * points it at the second half of the span buffer. */
// GLOBAL: XVT 0x54F988
struct Std3DRenderTri *g_triBuffer = NULL;
/* The mesh's vertBaseIndex (0 in the hardware path) plus its projected vertex
 * count: RenderScene_DrawMeshFaces starts g_clipVertCursor here, and emits a
 * vertex below it once per mesh but a clip vertex once per use. Only that
 * function writes it. */
// GLOBAL: XVT 0x54F98C
int g_clipInputProjVertEndIndex = 0;
/* Vertices g_flightVertexBuffer may hold before a batch is drawn: at most 256
 * and the device's maxVertexCount; RenderScene_InitHardwareFrame sets it from
 * the span buffer's size. */
// GLOBAL: XVT 0x54F990
int g_maxBatchVerts = 0;
/* Vertices in g_flightVertexBuffer for the current batch. Many functions write
 * it, chiefly RenderScene_EmitFlightVertex, which adds one;
 * RenderScene_InitHardwareFrame and a batch drawn early in
 * RenderScene_DrawMeshHardware set it to 0. */
// GLOBAL: XVT 0x54F994
int g_d3dVertexCount = 0;
/* Triangles in g_triBuffer for the current batch. Many functions write it,
 * chiefly RenderScene_DrawMeshFaces; RenderScene_InitHardwareFrame and a batch
 * drawn early in RenderScene_DrawMeshHardware set it to 0. */
// GLOBAL: XVT 0x54F9A0
int g_d3dTriangleCount = 0;
/* Screen x of the viewport's left in the display mode: g_flightVpX plus half of
 * g_displayModeWidth minus g_surfaceWidth; added to every emitted vertex. Only
 * RenderScene_InitHardwareFrame writes it. */
// GLOBAL: XVT 0x54F9A4
float g_flightVpOriginX = 0.0f;
/* Locked memory of g_sceneSpanDataHandle, g_sceneSpanDataCapacity spans; the
 * hardware path also keeps its vertex and triangle buffers there.
 * RenderScene_Initialize locks it; RenderScene_UnlockBuffers sets it to
 * NULL. */
// GLOBAL: XVT 0x999420
static struct SceneSpan *g_sceneSpanDataBase = NULL;
/* Nothing writes it, so it stays 0 and the distant-mesh paths that test it
 * (RenderScene_ProjectDistantMeshVertices, sw3d_ProjectMeshVerticesDistant and
 * the far eye in RenderScene_CullMeshFacesFromView) never run. */
// GLOBAL: XVT 0x5270B4
static uint8_t g_bBackdropMeshMode = 0;
/* Counter raised before each node the model walk visits;
 * RenderScene_CullMeshFacesFromView puts it in faceAndLayerId, which nothing
 * reads. Nothing resets it. */
// GLOBAL: XVT 0x5271D4
int g_curLayerId = 0;
/* Header of the built-in white texture a mesh with no texture node uses: NULL
 * until the first RenderScene_DrawObjectModel or
 * RenderScene_DrawSelectedRootNode fills g_defaultWhiteTexture and points it
 * there. */
// GLOBAL: XVT 0x5271D8
struct OptTextureData *g_defaultWhiteTextureDescPtr = NULL;
/* The built-in 8x8 white texture as 24-bit color, every byte 0xFF.
 * g_defaultWhiteTexture's texels are built from it, and
 * ModelTexture_LoadRgbOrTexFile uses it when a texture file does not open. */
// GLOBAL: XVT 0x5271E0
uint8_t g_defaultWhiteTextureRgb24[DEFAULT_WHITE_TEXTURE_RGB_SIZE] = {
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
};
/* The texture of the last texture node the model walk met, given to a mesh with
 * none of its own; each model's draw starts it at
 * g_defaultWhiteTextureDescPtr. */
// GLOBAL: XVT 0x60F1EC
struct OptTextureData *g_curTextureDesc = NULL;
/* The built-in white texture: an 8x8 header with 16 inline palette entries and
 * texels built from g_defaultWhiteTextureRgb24 on first use. */
// GLOBAL: XVT 0x60F210
struct ModelTextureDefaultTexture g_defaultWhiteTexture = {0};
/* Next row of g_sceneLightSampleData that RenderScene_CullMeshFacesFromView
 * gives a face, 0 to 199; it stops rising at 199, so later faces share that
 * row. RenderScene_Initialize sets it to 0 on a reset. */
// GLOBAL: XVT 0x999446
static int g_lightSampleSlotIndex = 0;
/* Light samples per row: g_flightVpWidth over g_sw3dLightSampleBlockSize,
 * rounded up; set by RenderScene_Initialize. */
// GLOBAL: XVT 0x999410
static int g_lightSampleSlotStride = 0;
/* Memory handle of the span buffer; RenderScene_AllocateBuffers allocates it,
 * RenderScene_FreeBuffers frees it and sets it to 0. */
// GLOBAL: XVT 0x999424
uint16_t g_sceneSpanDataHandle = 0;
/* Spans the span buffer holds: 20000, set by RenderScene_AllocateBuffers. */
// GLOBAL: XVT 0x999426
int g_sceneSpanDataCapacity = 0;
/* Next free span in g_sceneSpanDataBase. On a reset RenderScene_Initialize
 * starts it at the buffer's start and takes the cockpit's spans;
 * sw3d_InsertSpan takes one per span and, once it reaches g_pSceneSpanDataEnd,
 * keeps handing out the span just before it. */
// GLOBAL: XVT 0x99942A
struct SceneSpan *g_pSceneSpanDataCur = NULL;
/* The buffer's last span, g_sceneSpanDataCapacity - 1; set by
 * RenderScene_Initialize on a reset. */
// GLOBAL: XVT 0x99942E
struct SceneSpan *g_pSceneSpanDataEnd = NULL;
/* Locked memory of g_sceneSpanPtrListHandle, 20000 span pointers;
 * sw3d_ScanConvertFace gives each face one per row from the end, through
 * g_sceneSpanPtrAvail. */
// GLOBAL: XVT 0x999432
struct SceneSpan **g_sceneSpanPtrList = NULL;
/* Memory handle of g_sceneSpanPtrList; allocated by
 * RenderScene_AllocateBuffers, freed by RenderScene_FreeBuffers. */
// GLOBAL: XVT 0x999436
uint16_t g_sceneSpanPtrListHandle = 0;
/* Pointers g_sceneSpanPtrList holds: 20000, set by
 * RenderScene_AllocateBuffers. */
// GLOBAL: XVT 0x999438
static int g_sceneSpanPtrCapacity = 0;
/* Pointers still free at the front of g_sceneSpanPtrList; sw3d_ScanConvertFace
 * lowers it by a face's row count when the count is under it.
 * RenderScene_Initialize sets it to the capacity on a reset. */
// GLOBAL: XVT 0x99943C
int g_sceneSpanPtrAvail = 0;
/* Locked memory of g_sceneLightSampleDataHandle: 0x25800 bytes of 12-byte light
 * samples, one row per face slot. */
// GLOBAL: XVT 0x999440
static uint8_t *g_sceneLightSampleData = NULL;
/* Memory handle of g_sceneLightSampleData; allocated by
 * RenderScene_AllocateBuffers, freed by RenderScene_FreeBuffers. */
// GLOBAL: XVT 0x999444
uint16_t g_sceneLightSampleDataHandle = 0;
/* Locked memory of g_visFaceListHandle: up to 5000 faces that passed the cull,
 * appended by RenderScene_CullMeshFacesFromView. */
// GLOBAL: XVT 0x99944A
struct SceneFace *g_visFaceList = NULL;
/* 1 when the next RenderScene_Initialize from FlightMap_DrawObjectPass should
 * reset the scene: FlightMap_RenderView sets it, and FlightMap_DrawObjectPass
 * clears it after that call. */
// GLOBAL: XVT 0x55635C
int g_renderSceneResetPending = 0;
/* Faces in g_visFaceList. RenderScene_CullMeshFacesFromView adds each that
 * passes; RenderScene_DrawMeshHardware puts it back after each mesh, as that
 * path draws at once; RenderScene_Initialize sets it to 0 on a reset. */
// GLOBAL: XVT 0x999450
int g_visFaceCount = 0;
/* First face of the current pass in g_visFaceList, where
 * sw3d_DrawVisibleFacesToSurface starts: RenderScene_Initialize sets it to 0 on
 * a reset, else to g_visFaceCount. */
// GLOBAL: XVT 0x999454
int g_visFacePassStart = 0;
/* Faces g_visFaceList holds: 5000, set by RenderScene_AllocateBuffers; a mesh
 * that would pass it is not drawn. */
// GLOBAL: XVT 0x999458
static int g_sceneFaceMax = 0;
/* Memory handle of g_visFaceList; allocated by RenderScene_AllocateBuffers,
 * freed by RenderScene_FreeBuffers. */
// GLOBAL: XVT 0x99944E
uint16_t g_visFaceListHandle = 0;
/* Locked memory of g_projVertListHandle: the meshes' projected vertices,
 * g_projVertMax of them. */
// GLOBAL: XVT 0x99945C
struct ProjVertex *g_projVertList = NULL;
/* Vertices in use in g_projVertList. Many functions write it, chiefly the
 * projection functions, which add each mesh's count; RenderScene_DrawSceneMesh
 * and RenderScene_DrawMeshHardware set it to 0 before each mesh. */
// GLOBAL: XVT 0x999462
int g_projVertCount = 0;
/* Vertices g_projVertList holds: twice g_vertexRemapCapacity, set by
 * RenderScene_AllocateBuffers; a mesh with more vertices is not drawn. */
// GLOBAL: XVT 0x999466
static int g_projVertMax = 0;
/* Memory handle of g_projVertList; allocated by RenderScene_AllocateBuffers,
 * freed by RenderScene_FreeBuffers. */
// GLOBAL: XVT 0x999460
uint16_t g_projVertListHandle = 0;
/* Locked memory of g_sceneEdgeListHandle: g_sceneEdgeMax edges the software
 * renderer writes. RenderScene_DrawMeshFaces borrows it as a table of the
 * emitted index of each projected vertex. */
// GLOBAL: XVT 0x99946A
struct SceneEdge *g_sceneEdgeList = NULL;
/* Memory handle of g_sceneEdgeList, allocated for g_sceneEdgeMax edges by
 * RenderScene_AllocateBuffers; freed by RenderScene_FreeBuffers. */
// GLOBAL: XVT 0x99946E
uint16_t g_sceneEdgeListHandle = 0;
/* Edges in use in g_sceneEdgeList: sw3d_RasterizeMeshFaces adds each mesh's,
 * and RenderScene_DrawSceneMesh and RenderScene_DrawMeshHardware set it to 0
 * before each mesh. */
// GLOBAL: XVT 0x999470
int g_sceneEdgeCursor = 0;
/* Edges g_sceneEdgeList holds: twice g_sceneEdgeFlagsCapacity, set by
 * RenderScene_AllocateBuffers; a mesh with more edges is not drawn. */
// GLOBAL: XVT 0x999474
static int g_sceneEdgeMax = 0;
/* Locked memory of g_vertexRemapHandle: for each model vertex of the mesh being
 * projected, its index among the mesh's projected vertices, -1 until
 * projected. */
// GLOBAL: XVT 0x999478
int *g_vertexRemap = NULL;
/* Memory handle of g_vertexRemap; allocated by RenderScene_AllocateBuffers,
 * freed by RenderScene_FreeBuffers. */
// GLOBAL: XVT 0x99947C
uint16_t g_vertexRemapHandle = 0;
/* Entries g_vertexRemap is allocated with: the most vertices of any mesh the
 * model loading code (opt_model.c) has measured; FeDiskIo_LoadResources sets it
 * to 0 before loading. */
// GLOBAL: XVT 0x99947E
int g_vertexRemapCapacity = 0;
/* Locked memory of g_sceneEdgeFlagsHandle: for each model edge, the index of
 * the scene edge sw3d_RasterizeMeshFaces made for it, -1 before it is set up
 * and -2 when it was rejected. */
// GLOBAL: XVT 0x999482
int *g_sceneEdgeFlags = NULL;
/* Memory handle of g_sceneEdgeFlags; allocated by RenderScene_AllocateBuffers,
 * freed by RenderScene_FreeBuffers. */
// GLOBAL: XVT 0x999486
uint16_t g_sceneEdgeFlagsHandle = 0;
/* Entries g_sceneEdgeFlags is allocated with: the most edges of any mesh the
 * model loading code has measured; FeDiskIo_LoadResources sets it to 0 before
 * loading. */
// GLOBAL: XVT 0x999488
int g_sceneEdgeFlagsCapacity = 0;
/* Locked memory of g_sceneSclEdgeListHandle, 768 edge pointers; nothing reads
 * it. */
// GLOBAL: XVT 0x99948C
static struct SceneEdge **g_sceneSclEdgeList = NULL;
/* Memory handle of g_sceneSclEdgeList; allocated by
 * RenderScene_AllocateBuffers, freed by RenderScene_FreeBuffers. */
// GLOBAL: XVT 0x999490
uint16_t g_sceneSclEdgeListHandle = 0;
/* Locked memory of g_scanlineSpanHeadsHandle: for each viewport row, up to 768,
 * the first span of the software renderer's list. RenderScene_Initialize fills
 * it on a reset with the spans the cockpit covers. */
// GLOBAL: XVT 0x999492
struct SceneSpan **g_scanlineSpanHeads = NULL;
/* Memory handle of g_scanlineSpanHeads; allocated by
 * RenderScene_AllocateBuffers, freed by RenderScene_FreeBuffers. */
// GLOBAL: XVT 0x999496
uint16_t g_scanlineSpanHeadsHandle = 0;
/* Locked memory of g_meshQueueHandle: 500 SceneMesh copies, where the draw
 * functions keep each mesh while its faces sit in g_visFaceList. */
// GLOBAL: XVT 0x999498
static struct SceneMesh *g_meshQueue = NULL;
/* Memory handle of g_meshQueue; allocated by RenderScene_AllocateBuffers, freed
 * by RenderScene_FreeBuffers. */
// GLOBAL: XVT 0x99949C
uint16_t g_meshQueueHandle = 0;
/* Entries g_meshQueue holds: 500, set by RenderScene_AllocateBuffers. */
// GLOBAL: XVT 0x99949E
static int g_meshQueueMax = 0;
/* Next free entry in g_meshQueue: RenderScene_DrawSceneMesh's software path
 * advances it, the hardware path reuses the entry, and RenderScene_Initialize
 * sets it to 0 on a reset. At g_meshQueueMax no mesh is drawn. */
// GLOBAL: XVT 0x9994A2
static int g_meshQueueIndex = 0;
/* Eye position in the current mesh's model space, set by
 * RenderScene_CullMeshFacesFromView from the mesh; the cull and the specular
 * lighting read it. */
// GLOBAL: XVT 0x9994B0
struct OptVector g_meshEyePos = {0.0f, 0.0f, 0.0f};
/* Table from a 16-bit color to a palette index that model loading maps texture
 * palettes through; FeDiskIo_InitResources points it at
 * g_rgb565ToPaletteIndexLut. */
// GLOBAL: XVT 0x9994BC
uint8_t *g_activeRgb565ToPaletteIndexLut = NULL;

/* Projects the corners of the mesh's visible faces for the hardware path. Sets
 * vertBaseIndex to g_projVertCount and g_vertexRemap to -1 for the mesh's
 * vertices, and appends each corner not yet projected to g_projVertList:
 * viewport x and y with g_projScaleInt over depth, or, for a corner closer than
 * view depth 1, its view-space x and y with depth minus 1, which also sets the
 * face's nearClipState to -1; then its light from
 * RenderScene_ComputeVertexLighting and its texture coordinates. Sets each
 * face's largest and smallest scaledInverseDepth (a corner closer than 1
 * counting as g_projScaleInt) and its texture gradients; when the mesh has
 * texture coordinates, also turns them into the planes in viewport x and y and
 * sets texelsPerPixelQ8 from the texture's size, the planes' area and the
 * corners' depths (the reciprocal of their mean scaledInverseDepth, times
 * g_projScaleInt). Adds the new vertices to g_projVertCount. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x4084E0
void RenderScene_ProjectMeshVertices(struct SceneMesh *mesh)
{
	struct SceneFace *face = &g_visFaceList[mesh->faceBaseIndex];
	struct ProjVertex *output;
	int vertexIndex;
	int faceIndex;

	mesh->vertBaseIndex = g_projVertCount;
	output = &g_projVertList[mesh->vertBaseIndex];
	mesh->projVertCursor = 0;
	for (vertexIndex = 0; vertexIndex < mesh->vertexCount; ++vertexIndex) {
		g_vertexRemap[vertexIndex] = -1;
	}
	for (faceIndex = 0; faceIndex < mesh->visFaceCount;
	     ++face, ++faceIndex) {
		struct OptVector transformed;
		const struct FaceRecord *geometry;
		float totalW;
		float c00;
		float c01;
		float c02;
		float c10;
		float c11;
		float c12;
		float c20;
		float c21;
		float c22;
		float inverse;
		float scaled;
		float area;
		int cornerIndex;

		RenderScene_TransformFaceTextureGradients(
			face, &mesh->pFaceTexturing[face->faceIndex],
			&mesh->viewPosX);
		geometry = &mesh->pFaceGeom[face->faceIndex];
		face->maxScaledInverseDepth = 0.0f;
		totalW = 0.0f;
		face->minScaledInverseDepth =
			(float)(unsigned int)g_projScaleInt;
		for (cornerIndex = 0; cornerIndex < 4; ++cornerIndex) {
			const int modelVertexIndex =
				geometry->vertexIdx[cornerIndex];
			const int uvIndex = geometry->uvIdx[cornerIndex];
			const int normalIndex =
				geometry->normalIdx[cornerIndex];
			int remappedVertex;
			float vertexScaledInverseDepth;

			if (modelVertexIndex == -1) {
				break;
			}
			remappedVertex = g_vertexRemap[modelVertexIndex];
			if (remappedVertex == -1) {
				g_vertexRemap[modelVertexIndex] =
					mesh->projVertCursor;
				++mesh->projVertCursor;
				transformed.x =
					mesh->pModelVerts[modelVertexIndex].x;
				transformed.y =
					mesh->pModelVerts[modelVertexIndex].y;
				transformed.z =
					mesh->pModelVerts[modelVertexIndex].z;
				Math3D_RotateVec3(&transformed.x,
						  mesh->viewOrient);
				transformed.x += mesh->viewPosX;
				transformed.y += mesh->viewPosY;
				transformed.z += mesh->viewPosZ;
				if (transformed.z < g_renderUnitFloat) {
					output->scaledInverseDepth =
						transformed.z -
						g_renderUnitFloat;
					output->sx = transformed.x;
					output->sy = transformed.y;
					face->nearClipState = -1;
					vertexScaledInverseDepth =
						(float)(unsigned int)
							g_projScaleInt;
				} else {
					output->scaledInverseDepth =
						(float)(unsigned int)
							g_projScaleInt /
						transformed.z;
					output->sx =
						output->scaledInverseDepth *
						transformed.x;
					output->sy =
						output->scaledInverseDepth *
						transformed.y;
					output->sx +=
						(float)(g_flightVpWidth >> 1);
					output->sy +=
						(float)(g_projOffsetY +
							(g_flightVpHeight >>
							 1));
					vertexScaledInverseDepth =
						output->scaledInverseDepth;
				}
				RenderScene_ComputeVertexLighting(
					mesh, output,
					&mesh->pVertNormals[normalIndex],
					&mesh->pModelVerts[modelVertexIndex],
					&g_meshEyePos);
				output->tu = mesh->pUVs[uvIndex].u;
				output->tv = mesh->pUVs[uvIndex].v;
				++output;
			} else {
				const struct ProjVertex *projected =
					&g_projVertList[mesh->vertBaseIndex +
							remappedVertex];
				if (projected->scaledInverseDepth < 0.0f) {
					face->nearClipState = -1;
					vertexScaledInverseDepth =
						(float)(unsigned int)
							g_projScaleInt;
				} else {
					vertexScaledInverseDepth =
						projected->scaledInverseDepth;
				}
			}
			totalW += vertexScaledInverseDepth;
			if (face->maxScaledInverseDepth <
			    vertexScaledInverseDepth) {
				face->maxScaledInverseDepth =
					vertexScaledInverseDepth;
			}
			if (face->minScaledInverseDepth >
			    vertexScaledInverseDepth) {
				face->minScaledInverseDepth =
					vertexScaledInverseDepth;
			}
		}

		if (mesh->pUVs != NULL) {
			const int uvIndex = geometry->uvIdx[0];
			const int modelVertexIndex = geometry->vertexIdx[0];

			transformed.x = mesh->pModelVerts[modelVertexIndex].x;
			transformed.y = mesh->pModelVerts[modelVertexIndex].y;
			transformed.z = mesh->pModelVerts[modelVertexIndex].z;
			Math3D_RotateVec3(&transformed.x, mesh->viewOrient);
			transformed.x += mesh->viewPosX;
			transformed.y += mesh->viewPosY;
			transformed.z += mesh->viewPosZ;
			face->gradients[6] =
				transformed.x -
				mesh->pUVs[uvIndex].v * face->gradients[3] -
				mesh->pUVs[uvIndex].u * face->gradients[0];
			face->gradients[7] =
				transformed.y -
				mesh->pUVs[uvIndex].v * face->gradients[4] -
				mesh->pUVs[uvIndex].u * face->gradients[1];
			face->gradients[8] =
				transformed.z -
				mesh->pUVs[uvIndex].v * face->gradients[5] -
				mesh->pUVs[uvIndex].u * face->gradients[2];
			c00 = face->gradients[8] * face->gradients[4] -
			      face->gradients[5] * face->gradients[7];
			c01 = face->gradients[5] * face->gradients[6] -
			      face->gradients[8] * face->gradients[3];
			c02 = face->gradients[7] * face->gradients[3] -
			      face->gradients[4] * face->gradients[6];
			c10 = face->gradients[2] * face->gradients[7] -
			      face->gradients[8] * face->gradients[1];
			c11 = face->gradients[8] * face->gradients[0] -
			      face->gradients[2] * face->gradients[6];
			c12 = face->gradients[6] * face->gradients[1] -
			      face->gradients[7] * face->gradients[0];
			c20 = face->gradients[5] * face->gradients[1] -
			      face->gradients[2] * face->gradients[4];
			c21 = face->gradients[2] * face->gradients[3] -
			      face->gradients[5] * face->gradients[0];
			c22 = face->gradients[4] * face->gradients[0] -
			      face->gradients[1] * face->gradients[3];
			if (c20 == 0.0f && c21 == 0.0f && c22 == 0.0f) {
				c22 = 1.0f;
			}
			inverse =
				g_renderUnitFloat / (c20 * face->gradients[6] +
						     c21 * face->gradients[7] +
						     c22 * face->gradients[8]);
			scaled = inverse * g_invProjScale;
			face->gradients[0] = scaled * c00;
			face->gradients[1] = scaled * c01;
			face->gradients[2] = inverse * c02;
			face->gradients[3] = scaled * c10;
			face->gradients[4] = scaled * c11;
			face->gradients[5] = inverse * c12;
			face->gradients[6] = scaled * c20;
			face->gradients[7] = scaled * c21;
			face->gradients[8] = inverse * c22;
			face->gradients[2] -= (float)(g_flightVpWidth >> 1) *
					      face->gradients[0];
			face->gradients[2] -= (float)(g_projOffsetY +
						      (g_flightVpHeight >> 1)) *
					      face->gradients[1];
			face->gradients[5] -= (float)(g_flightVpWidth >> 1) *
					      face->gradients[3];
			face->gradients[5] -= (float)(g_projOffsetY +
						      (g_flightVpHeight >> 1)) *
					      face->gradients[4];
			face->gradients[8] -= (float)(g_flightVpWidth >> 1) *
					      face->gradients[6];
			face->gradients[8] -= (float)(g_projOffsetY +
						      (g_flightVpHeight >> 1)) *
					      face->gradients[7];
			area = face->gradients[0] * face->gradients[4] -
			       face->gradients[3] * face->gradients[1];
			if (area < g_renderProjectionZeroFloat) {
				area = -area;
			}
			{
				const struct OptTextureData *material =
					(const struct OptTextureData *)
						mesh->pMaterial;
				float meanViewDepth;

				/* totalW, the sum of the corners' scaled inverse depths, becomes the corner count over
				 * that sum: the reciprocal of their mean. */
				if (geometry->vertexIdx[3] == -1) {
					totalW = g_renderTriangleCornerCount /
						 totalW;
				} else {
					totalW = g_renderQuadCornerCount /
						 totalW;
				}
				meanViewDepth =
					(float)(unsigned int)g_projScaleInt *
					totalW;
				face->texelsPerPixelQ8 =
					(int)((float)((material->width *
						       material->height)
						      << 8) *
					      (area * (meanViewDepth *
						       meanViewDepth)));
			}
		}
	}
	g_projVertCount += mesh->projVertCursor;
}

/* The distant form of RenderScene_ProjectMeshVertices: adds
 * g_renderDistantDepth to every corner's depth and scales the projection by
 * g_projScaleInt / viewPosZ * g_renderDistantDepth, with no near test and no
 * texture planes. Only RenderScene_DrawMeshHardware calls it, while
 * g_bBackdropMeshMode is set, and nothing sets that. */
// FUNCTION: XVT 0x408BC0
void RenderScene_ProjectDistantMeshVertices(struct SceneMesh *mesh)
{
	float projectionScale;
	struct SceneFace *face;
	int vertexBaseIndex;
	struct ProjVertex *output;
	int vertexIndex;
	int faceIndex;

	projectionScale = (float)((double)(unsigned int)g_projScaleInt /
				  mesh->viewPosZ * g_renderDistantDepth);
	face = &g_visFaceList[mesh->faceBaseIndex];
	vertexBaseIndex = g_projVertCount;
	mesh->vertBaseIndex = vertexBaseIndex;
	output = &g_projVertList[vertexBaseIndex];
	mesh->projVertCursor = 0;
	for (vertexIndex = 0; mesh->vertexCount > vertexIndex; ++vertexIndex) {
		g_vertexRemap[vertexIndex] = -1;
	}
	for (faceIndex = 0; mesh->visFaceCount > faceIndex;
	     ++face, ++faceIndex) {
		const struct FaceRecord *geometry =
			&mesh->pFaceGeom[face->faceIndex];
		int cornerIndex;

		face->maxScaledInverseDepth = 0.0f;
		face->minScaledInverseDepth = (float)g_projScaleInt;
		for (cornerIndex = 0; cornerIndex < 4; ++cornerIndex) {
			const int modelVertexIndex =
				geometry->vertexIdx[cornerIndex];
			const int uvIndex = geometry->uvIdx[cornerIndex];
			const int normalIndex =
				geometry->normalIdx[cornerIndex];
			float vertexScaledInverseDepth;

			if (modelVertexIndex == -1) {
				break;
			}
			if (g_vertexRemap[modelVertexIndex] == -1) {
				struct OptVector transformed;

				g_vertexRemap[modelVertexIndex] =
					mesh->projVertCursor++;
				transformed.x =
					mesh->pModelVerts[modelVertexIndex].x;
				transformed.y =
					mesh->pModelVerts[modelVertexIndex].y;
				transformed.z =
					mesh->pModelVerts[modelVertexIndex].z;
				Math3D_RotateVec3(&transformed.x,
						  mesh->viewOrient);
				transformed.x += mesh->viewPosX;
				transformed.y += mesh->viewPosY;
				transformed.z += mesh->viewPosZ;
				transformed.z += g_renderDistantDepth;
				output->scaledInverseDepth =
					projectionScale / transformed.z;
				output->sx = output->scaledInverseDepth *
					     transformed.x;
				output->sy = output->scaledInverseDepth *
					     transformed.y;
				output->sx += (float)(g_flightVpWidth >> 1);
				output->sy += (float)(g_projOffsetY +
						      (g_flightVpHeight >> 1));
				vertexScaledInverseDepth =
					output->scaledInverseDepth;
				RenderScene_ComputeVertexLighting(
					mesh, output,
					&mesh->pVertNormals[normalIndex],
					&mesh->pModelVerts[modelVertexIndex],
					&g_meshEyePos);
				output->tu = mesh->pUVs[uvIndex].u;
				output->tv = mesh->pUVs[uvIndex].v;
				++output;
			} else {
				vertexScaledInverseDepth =
					g_projVertList
						[mesh->vertBaseIndex +
						 g_vertexRemap
							 [modelVertexIndex]]
							.scaledInverseDepth;
			}
			if (face->maxScaledInverseDepth <
			    vertexScaledInverseDepth) {
				face->maxScaledInverseDepth =
					vertexScaledInverseDepth;
			}
			if (face->minScaledInverseDepth >
			    vertexScaledInverseDepth) {
				face->minScaledInverseDepth =
					vertexScaledInverseDepth;
			}
		}
	}
	g_projVertCount += mesh->projVertCursor;
}

/* Clips and queues the mesh's visible faces for the hardware path. For each
 * face it lists the 3 or 4 corners in g_clipIdxA, adding a copy at
 * g_clipVertCursor of any corner whose stored texture coordinates differ from
 * this face's (scaled for a square texture when the device takes only square
 * ones); runs RenderClip_ClipPolyNear when nearClipState is -1, then the top,
 * bottom, left and right clips; and emits the result with
 * RenderScene_EmitFlightVertex, once per mesh for a projected vertex and on
 * every use for a clip vertex. With 3 or more corners left it picks a mip level
 * when the texture's textureSize is width times height: from texelsPerPixelQ8 *
 * g_mipLodScale, it shifts the value down 2 bits and takes the next level while
 * the value is over 256 and neither side is 8. When the texels changed it gets
 * the opaque texture, and a color-key texture when the palette marks a
 * transparent entry: for a projectile it clears that mark instead, and when
 * none can be made at full size it puts '_' at the start of the texture's name.
 * Then queues a fan of triangles into g_triBuffer: first, with a color-key
 * texture, copies of the corners in white with that texture and alpha blending,
 * then the opaque ones. Returns at once when the modern build suppresses
 * classic flight rendering. Does not check the batch limits;
 * RenderScene_DrawMeshHardware does. */
// FUNCTION: XVT 0x408E70
void RenderScene_DrawMeshFaces(const struct SceneMesh *mesh)
{
	struct RenderClipVertex *vertices;
	int *emittedVertexByProjection;
	int *clipOutput;
	struct SceneFace *face;
	const uint8_t *previousTexels;
	struct Std3DTexCacheNode *opaqueTexture;
	struct Std3DTexCacheNode *colorKeyTexture;
	int faceIndex;
	int vertexIndex;
	int clipIndex;
	int previousVertexIndex;
	int currentVertexIndex;
	int textureWidth;
	int textureHeight;
	int texelOffset;
	int texelsPerPixelQ8;
	int triangleCorner;
	int colorKeyVertexBase;

	enum {
		TRIANGLE_FIRST_NEW_CORNER = 2,
		MIP_LEVEL_REDUCTION_SHIFT = 2,
		MIP_LEVEL_REDUCTION_THRESHOLD = 256,
		MIP_MINIMUM_DIMENSION = 8,
		OPAQUE_PALETTE_OFFSET = 2048,
		PALETTE_TRANSPARENT_INDEX_SLOT = 256,
		BASE_MESH_TRIANGLE_FLAGS = 0x9813,
		BILINEAR_TRIANGLE_FLAGS = STD3D_RS_TEXTURE_MAG_LINEAR |
					  STD3D_RS_TEXTURE_MIN_LINEAR,
		COLOR_KEY_TRIANGLE_FLAGS = STD3D_RS_ALPHA_BLEND
	};

#ifdef XVT_MODERN
	/* Suppress before texture lookup so hidden classic draws do not refill the cache. */
	if (AeronDx5_IsClassicFlightRenderingSuppressed()) {
		return;
	}
#endif

	vertexIndex = mesh->vertBaseIndex;
	previousTexels = NULL;
	vertices = (struct RenderClipVertex *)&g_projVertList[vertexIndex];
	g_clipInputProjVertEndIndex = vertexIndex + mesh->projVertCursor;
	g_clipVertCursor = g_clipInputProjVertEndIndex;
	face = &g_visFaceList[mesh->faceBaseIndex];
	emittedVertexByProjection = (int *)g_sceneEdgeList;
	for (vertexIndex = 0; vertexIndex < g_clipInputProjVertEndIndex;
	     ++vertexIndex) {
		emittedVertexByProjection[vertexIndex] = -1;
	}
#ifdef XVT_MODERN
	opaqueTexture = NULL;
	colorKeyTexture = NULL;
#endif

	faceIndex = 0;
	if (mesh->visFaceCount <= 0) {
		return;
	}
	do {
		struct SceneFace *currentFace;
		const struct FaceRecord *geometry;
		int cornerCount;

		currentFace = face++;
		geometry = &mesh->pFaceGeom[currentFace->faceIndex];
		cornerCount = geometry->edgeIdx[3] == -1 ? 3 : 4;
		g_clipCountA = cornerCount;
		if (g_pStd3DCurDevice->caps.bSquareOnlyTexture != 0) {
			const struct OptTextureData *material;
			float uvScale;
			int scaledWidth;
			int scaledHeight;

			material =
				(const struct OptTextureData *)mesh->pMaterial;
			uvScale = 1.0f;
			scaledWidth = material->width;
			scaledHeight = material->height;
			if (scaledHeight < scaledWidth) {
				while (scaledHeight < scaledWidth) {
					uvScale *= g_renderTextureUvHalfScale;
					scaledHeight *= 2;
				}
				scaledHeight = material->height;
			} else if (scaledHeight > scaledWidth) {
				while (scaledWidth < scaledHeight) {
					uvScale *= g_renderTextureUvHalfScale;
					scaledWidth *= 2;
				}
				scaledWidth = material->width;
			}
			clipOutput = g_clipIdxA;
			for (vertexIndex = 0; vertexIndex < cornerCount;
			     ++vertexIndex) {
				struct OptTexCoord uv;
				struct RenderClipVertex *source;
				struct RenderClipVertex *duplicate;
				int projectedVertexIndex;

				uv = mesh->pUVs[geometry->uvIdx[vertexIndex]];
				if (scaledHeight < scaledWidth) {
					uv.v *= uvScale;
				} else if (scaledHeight > scaledWidth) {
					uv.u *= uvScale;
				}
				projectedVertexIndex = g_vertexRemap
					[geometry->vertexIdx[vertexIndex]];
				*clipOutput = projectedVertexIndex;
				source = &vertices[projectedVertexIndex];
				if (source->u != uv.u || source->v != uv.v) {
					duplicate = &vertices[g_clipVertCursor];
					duplicate->x = source->x;
					duplicate->y = source->y;
					duplicate->lightIntensity =
						source->lightIntensity;
					duplicate->scaledInverseDepth =
						source->scaledInverseDepth;
					duplicate->u = uv.u;
					duplicate->v = uv.v;
					*clipOutput = g_clipVertCursor++;
				}
				++clipOutput;
			}
		} else {
			for (vertexIndex = 0; vertexIndex < cornerCount;
			     ++vertexIndex) {
				const struct OptTexCoord *uv;
				struct RenderClipVertex *source;
				struct RenderClipVertex *duplicate;
				int projectedVertexIndex;

				projectedVertexIndex = g_vertexRemap
					[geometry->vertexIdx[vertexIndex]];
				g_clipIdxA[vertexIndex] = projectedVertexIndex;
				uv = &mesh->pUVs[geometry->uvIdx[vertexIndex]];
				source = &vertices[projectedVertexIndex];
				if (source->u != uv->u || source->v != uv->v) {
					duplicate = &vertices[g_clipVertCursor];
					duplicate->x = source->x;
					duplicate->y = source->y;
					duplicate->lightIntensity =
						source->lightIntensity;
					duplicate->scaledInverseDepth =
						source->scaledInverseDepth;
					duplicate->u =
						mesh->pUVs
							[geometry->uvIdx
								 [vertexIndex]]
								.u;
					duplicate->v =
						mesh->pUVs
							[geometry->uvIdx
								 [vertexIndex]]
								.v;
					g_clipIdxA[vertexIndex] =
						g_clipVertCursor++;
				}
			}
		}

		if (currentFace->nearClipState == -1) {
			if (g_clipCountA > 0) {
				memcpy(g_clipIdxB, g_clipIdxA,
				       (size_t)g_clipCountA *
					       sizeof(g_clipIdxA[0]));
			}
			g_clipCountB = g_clipCountA;
			g_clipCountA = 0;
			if (g_clipCountB > 0) {
				previousVertexIndex =
					g_clipIdxB[g_clipCountB - 1];
				for (clipIndex = 0; clipIndex < g_clipCountB;
				     ++clipIndex) {
					currentVertexIndex =
						g_clipIdxB[clipIndex];
					RenderClip_ClipPolyNear(
						previousVertexIndex,
						currentVertexIndex, vertices);
					previousVertexIndex =
						currentVertexIndex;
				}
			}
		}

		currentFace->nearClipState = g_flightVpHeight;
		g_clipCountB = 0;
		if (g_clipCountA > 0) {
			previousVertexIndex = g_clipIdxA[g_clipCountA - 1];
			for (clipIndex = 0; clipIndex < g_clipCountA;
			     ++clipIndex) {
				currentVertexIndex = g_clipIdxA[clipIndex];
				RenderClip_ClipPolyTop(previousVertexIndex,
						       currentVertexIndex,
						       vertices);
				previousVertexIndex = currentVertexIndex;
			}
		}
		g_clipCountA = 0;
		if (g_clipCountB > 0) {
			previousVertexIndex = g_clipIdxB[g_clipCountB - 1];
			for (clipIndex = 0; clipIndex < g_clipCountB;
			     ++clipIndex) {
				currentVertexIndex = g_clipIdxB[clipIndex];
				RenderClip_ClipPolyBottom(previousVertexIndex,
							  currentVertexIndex,
							  vertices);
				previousVertexIndex = currentVertexIndex;
			}
		}
		g_clipCountB = 0;
		if (g_clipCountA > 0) {
			previousVertexIndex = g_clipIdxA[g_clipCountA - 1];
			for (clipIndex = 0; clipIndex < g_clipCountA;
			     ++clipIndex) {
				currentVertexIndex = g_clipIdxA[clipIndex];
				RenderClip_ClipPolyLeft(previousVertexIndex,
							currentVertexIndex,
							vertices);
				previousVertexIndex = currentVertexIndex;
			}
		}
		g_clipCountA = 0;
		if (g_clipCountB > 0) {
			previousVertexIndex = g_clipIdxB[g_clipCountB - 1];
			for (clipIndex = 0; clipIndex < g_clipCountB;
			     ++clipIndex) {
				currentVertexIndex = g_clipIdxB[clipIndex];
				RenderClip_ClipPolyRight(previousVertexIndex,
							 currentVertexIndex,
							 vertices);
				previousVertexIndex = currentVertexIndex;
			}
		}

		for (clipIndex = 0; clipIndex < g_clipCountA; ++clipIndex) {
			int emittedVertexIndex;

			currentVertexIndex = g_clipIdxA[clipIndex];
			if (currentVertexIndex < g_clipInputProjVertEndIndex) {
				if (emittedVertexByProjection
					    [currentVertexIndex] == -1) {
					emittedVertexByProjection
						[currentVertexIndex] =
							RenderScene_EmitFlightVertex(
								currentVertexIndex,
								vertices,
								currentFace);
				}
				emittedVertexIndex = emittedVertexByProjection
					[currentVertexIndex];
			} else {
				emittedVertexIndex =
					RenderScene_EmitFlightVertex(
						currentVertexIndex, vertices,
						currentFace);
			}
			g_clipIdxA[clipIndex] = emittedVertexIndex;
		}

		if (g_clipCountA > TRIANGLE_FIRST_NEW_CORNER) {
			const struct OptTextureData *material;
			const uint8_t *texels;

			material =
				(const struct OptTextureData *)mesh->pMaterial;
			textureWidth = material->width;
			textureHeight = material->height;
			texelOffset = 0;
			if (textureWidth * textureHeight ==
			    material->textureSize) {
				texelsPerPixelQ8 =
					(int)((float)currentFace
						      ->texelsPerPixelQ8 *
					      g_mipLodScale);
				while (texelsPerPixelQ8 >
					       MIP_LEVEL_REDUCTION_THRESHOLD &&
				       textureWidth != MIP_MINIMUM_DIMENSION &&
				       textureHeight != MIP_MINIMUM_DIMENSION) {
					texelsPerPixelQ8 >>=
						MIP_LEVEL_REDUCTION_SHIFT;
					texelOffset +=
						textureWidth * textureHeight;
					textureWidth >>= 1;
					textureHeight >>= 1;
				}
			}
			texels = (const uint8_t *)mesh->pTexels + texelOffset;
			if (texels != previousTexels) {
				uint16_t *opaquePalette;

				previousTexels = texels;
				opaquePalette = mesh->pColorKeyPalette +
						OPAQUE_PALETTE_OFFSET;
				opaqueTexture = RenderTexture_GetOrCreateOpaque(
					textureWidth, textureHeight,
					opaquePalette, texels);
				colorKeyTexture = NULL;
				if (opaquePalette
					    [PALETTE_TRANSPARENT_INDEX_SLOT] !=
				    0) {
					uint8_t genusId;

					genusId = mesh->pObject->genusId;
					if (genusId ==
						    CRAFT_GENUS_PLAYER_PROJECTILE ||
					    genusId ==
						    CRAFT_GENUS_OTHER_PROJECTILE) {
						opaquePalette
							[PALETTE_TRANSPARENT_INDEX_SLOT] =
								0;
					} else if (mesh->pTextureName != NULL &&
						   *mesh->pTextureName != '_') {
						colorKeyTexture =
							RenderTexture_GetOrCreateColorKey(
								textureWidth,
								textureHeight,
								mesh->pColorKeyPalette,
								texels);
						if (colorKeyTexture == NULL &&
						    material->width ==
							    textureWidth &&
						    material->height ==
							    textureHeight) {
							*mesh->pTextureName =
								'_';
						}
					}
				}
			}
		}

		if (colorKeyTexture != NULL) {
			for (vertexIndex = 0; vertexIndex < g_clipCountA;
			     ++vertexIndex) {
				g_flightVertexBuffer[g_d3dVertexCount +
						     vertexIndex] =
					g_flightVertexBuffer
						[g_clipIdxA[vertexIndex]];
				g_flightVertexBuffer[g_d3dVertexCount +
						     vertexIndex]
					.color = UINT32_MAX;
			}
			colorKeyVertexBase = g_d3dVertexCount;
			triangleCorner = TRIANGLE_FIRST_NEW_CORNER;
			g_d3dVertexCount += g_clipCountA;
			if (g_clipCountA > triangleCorner) {
				do {
					g_triBuffer[g_d3dTriangleCount]
						.vertexIndex0 =
						colorKeyVertexBase;
					g_triBuffer[g_d3dTriangleCount]
						.vertexIndex1 =
						colorKeyVertexBase +
						triangleCorner - 1;
					g_triBuffer[g_d3dTriangleCount]
						.vertexIndex2 =
						colorKeyVertexBase +
						triangleCorner;
					g_triBuffer[g_d3dTriangleCount]
						.texture = colorKeyTexture;
					g_triBuffer[g_d3dTriangleCount]
						.flags = (Std3DRenderStateFlags)
						BASE_MESH_TRIANGLE_FLAGS;
					if (g_bilinearEnabled != 0) {
						g_triBuffer[g_d3dTriangleCount]
							.flags +=
							BILINEAR_TRIANGLE_FLAGS;
					}
					++triangleCorner;
					g_triBuffer[g_d3dTriangleCount].flags +=
						COLOR_KEY_TRIANGLE_FLAGS;
					++g_d3dTriangleCount;
				} while (triangleCorner < g_clipCountA);
			}
		}

		triangleCorner = TRIANGLE_FIRST_NEW_CORNER;
		if (g_clipCountA > triangleCorner) {
			int *triangleVertex = &g_clipIdxA[1];

			do {
				g_triBuffer[g_d3dTriangleCount].vertexIndex0 =
					g_clipIdxA[0];
				g_triBuffer[g_d3dTriangleCount].vertexIndex1 =
					*triangleVertex++;
				g_triBuffer[g_d3dTriangleCount].vertexIndex2 =
					*triangleVertex;
				g_triBuffer[g_d3dTriangleCount].texture =
					opaqueTexture;
				g_triBuffer[g_d3dTriangleCount].flags =
					(Std3DRenderStateFlags)
						BASE_MESH_TRIANGLE_FLAGS;
				if (g_bilinearEnabled != 0) {
					g_triBuffer[g_d3dTriangleCount].flags +=
						BILINEAR_TRIANGLE_FLAGS;
				}
				if (g_capVertexAlpha != 0) {
					g_triBuffer[g_d3dTriangleCount].flags +=
						COLOR_KEY_TRIANGLE_FLAGS;
					g_capVertexAlpha = 0;
				}
				++triangleCorner;
				++g_d3dTriangleCount;
			} while (triangleCorner < g_clipCountA);
		}
		++faceIndex;
	} while (faceIndex < mesh->visFaceCount);
}

/* Draws one mesh through the hardware path. Sets g_projVertCount and
 * g_sceneEdgeCursor to 0; does nothing more when g_meshQueue is full or the
 * mesh has more faces, vertices or edges than the buffers hold. Otherwise
 * copies it into g_meshQueue at g_meshQueueIndex, without advancing it, and
 * culls it; when faces are left, first draws the batch so far if 8 vertices and
 * 2 triangles per face would not fit, then projects the mesh (distant while
 * g_bBackdropMeshMode is set), queues its faces and puts g_visFaceCount back as
 * it was. */
// FUNCTION: XVT 0x40B010
void RenderScene_DrawMeshHardware(const struct SceneMesh *mesh)
{
	struct SceneMesh *queuedMesh;
	int previousVisibleFaceCount;

	g_projVertCount = 0;
	previousVisibleFaceCount = g_visFaceCount;
	g_sceneEdgeCursor = 0;
	if (g_meshQueueIndex == g_meshQueueMax ||
	    g_visFaceCount + mesh->faceCount > g_sceneFaceMax ||
	    mesh->vertexCount > g_projVertMax ||
	    mesh->edgeCount > g_sceneEdgeMax) {
		return;
	}
	memcpy(&g_meshQueue[g_meshQueueIndex], mesh, sizeof(struct SceneMesh));
	queuedMesh = &g_meshQueue[g_meshQueueIndex];
	RenderScene_CullMeshFacesFromView(queuedMesh);
	if (queuedMesh->visFaceCount == 0) {
		return;
	}
	if (g_d3dVertexCount + 8 * queuedMesh->visFaceCount > g_maxBatchVerts ||
	    g_d3dTriangleCount + 2 * queuedMesh->visFaceCount >
		    g_maxBatchTris) {
		Math_SetFpuExtendedPrecisionMode();
		std3D_StartScene();
		std3D_LockExecuteBuffer();
		std3D_AddVertices(g_flightVertexBuffer, g_d3dVertexCount);
		std3D_BeginInstructions();
		std3D_AddTriangles(g_triBuffer,
				   (unsigned int)g_d3dTriangleCount);
		std3D_ExecuteBuffer();
		std3D_EndScene();
		Math_SetFpuSinglePrecisionMode();
		g_d3dTriangleCount = 0;
		g_d3dVertexCount = 0;
	}
	if (g_bBackdropMeshMode != 0) {
		RenderScene_ProjectDistantMeshVertices(queuedMesh);
	} else {
		RenderScene_ProjectMeshVertices(queuedMesh);
	}
	RenderScene_DrawMeshFaces(queuedMesh);
	g_visFaceCount = previousVisibleFaceCount;
}

/* Starts a frame of the hardware path: sets g_flightVpOriginX and
 * g_flightVpOriginY, sets g_d3dVertexCount, g_d3dTriangleCount and
 * g_d3dVertexAlphaStateResetSlot to 0 and g_capVertexAlpha to 1, and points
 * g_flightVertexBuffer at the start of the span buffer and g_triBuffer at its
 * middle. The batch limits come from the span buffer's bytes: g_maxBatchVerts
 * is bytes >> 7, then no more than the device's maxVertexCount and 256;
 * g_maxBatchTris is bytes / sizeof(Std3DRenderTri) >> 2, no more than 256, then
 * no more than (maxBufferSize - 64 * g_maxBatchVerts) / sizeof(SceneSpan). */
// FUNCTION: XVT 0x40B180
void RenderScene_InitHardwareFrame(void)
{
	unsigned int spanBytes;
	int viewportOriginX;
	int viewportOriginY;

	viewportOriginX = g_displayModeWidth - g_surfaceWidth;
	viewportOriginY = g_displayModeHeight - g_surfaceHeight;
	viewportOriginX = g_flightVpX + ((unsigned int)viewportOriginX >> 1);
	viewportOriginY = g_flightVpY + ((unsigned int)viewportOriginY >> 1);
	g_flightVpOriginX = (float)(unsigned int)viewportOriginX;
	g_d3dTriangleCount = 0;
	g_flightVpOriginY = (float)(unsigned int)viewportOriginY;
	g_d3dVertexCount = 0;
	g_d3dVertexAlphaStateResetSlot = 0;
	g_capVertexAlpha = 1;

	spanBytes = sizeof(struct SceneSpan) * g_sceneSpanDataCapacity;
	g_maxBatchVerts = spanBytes >> 7;
	g_maxBatchTris = spanBytes / sizeof(struct Std3DRenderTri) >> 2;
	if (g_pStd3DCurDevice->caps.maxVertexCount <
	    (unsigned int)g_maxBatchVerts) {
		g_maxBatchVerts = g_pStd3DCurDevice->caps.maxVertexCount;
	}
	if (g_maxBatchVerts > 256) {
		g_maxBatchVerts = 256;
	}
	if (g_maxBatchTris > 256) {
		g_maxBatchTris = 256;
	}
	if ((int)((g_pStd3DCurDevice->caps.maxBufferSize -
		   ((unsigned int)g_maxBatchVerts << 6)) /
		  sizeof(struct SceneSpan)) < g_maxBatchTris) {
		g_maxBatchTris = (g_pStd3DCurDevice->caps.maxBufferSize -
				  ((unsigned int)g_maxBatchVerts << 6)) /
				 sizeof(struct SceneSpan);
	}
	g_flightVertexBuffer = (D3DTLVERTEX *)g_sceneSpanDataBase;
	g_triBuffer =
		(struct Std3DRenderTri
			 *)&g_sceneSpanDataBase[g_sceneSpanDataCapacity / 2];
}

/* Draws the queued billboards with SceneBillboard_RenderQueuedTextured, with
 * target markers and Targeting_DrawSceneObjectBoxes while
 * g_sceneFlushDrawTargetMarkers is set, and sets g_sceneBillboardQueueCount to
 * 0. Then, when g_d3dVertexCount and g_d3dTriangleCount are both nonzero, draws
 * the hardware batch in one execute buffer in extended x87 precision, returning
 * to single after. Leaves both counts as they are. */
// FUNCTION: XVT 0x40B2C0
void RenderScene_FlushGeometry(void)
{
	enum {
		SKIP_TARGET_MARKERS = 0,
		DRAW_TARGET_MARKERS = 1,
	};

	if (g_sceneFlushDrawTargetMarkers != 0) {
		SceneBillboard_RenderQueuedTextured(DRAW_TARGET_MARKERS);
		Targeting_DrawSceneObjectBoxes();
	} else {
		SceneBillboard_RenderQueuedTextured(SKIP_TARGET_MARKERS);
	}
	g_sceneBillboardQueueCount = 0;

	if (g_d3dVertexCount == 0 || g_d3dTriangleCount == 0) {
		return;
	}
	Math_SetFpuExtendedPrecisionMode();
	std3D_StartScene();
	std3D_LockExecuteBuffer();
	std3D_AddVertices(g_flightVertexBuffer, g_d3dVertexCount);
	std3D_BeginInstructions();
	std3D_AddTriangles(g_triBuffer, g_d3dTriangleCount);
	std3D_ExecuteBuffer();
	std3D_EndScene();
	Math_SetFpuSinglePrecisionMode();
}

/* Appends vertex vertexIndex of vertices to g_flightVertexBuffer and returns
 * its index there, adding 1 to g_d3dVertexCount. Adds g_flightVpOriginX and
 * g_flightVpOriginY to x and y, takes a negative scaledInverseDepth as
 * g_projScaleInt, writes that as rhw and 1 / (depth * g_invDepthProjScale + 1)
 * as z, or 1 minus that when g_std3DZCompareCap is 2. The color is a gray of
 * (int)(light * 320) + 48, at most 255, with alpha 0xFE while g_capVertexAlpha
 * is set and 0xFF otherwise; no specular. Ignores face. Does not check
 * g_maxBatchVerts. */
// FUNCTION: XVT 0x40B350
int RenderScene_EmitFlightVertex(int vertexIndex,
				 const struct RenderClipVertex *vertices,
				 const struct SceneFace *face)
{
	const struct RenderClipVertex *source;
	uint32_t zBits;
	float x;
	float y;
	float scaledInverseDepth;
	float lightIntensity;
	float u;
	float v;
	float depth;
	int intensity;
	uint32_t color;

	(void)face;
	source = &vertices[vertexIndex];
	lightIntensity = source->lightIntensity;
	x = source->x;
	y = source->y;
	scaledInverseDepth = source->scaledInverseDepth;
	u = source->u;
	v = source->v;
	memcpy(&zBits, &scaledInverseDepth, sizeof(zBits));
	if (zBits > 0x80000000u) {
		scaledInverseDepth = (float)(unsigned int)g_projScaleInt;
	}
	depth = 1.0f / ((float)(unsigned int)g_projScaleInt /
				scaledInverseDepth * g_invDepthProjScale +
			1.0f);
	if (g_std3DZCompareCap == 2) {
		depth = 1.0f - depth;
	}
	g_flightVertexBuffer[g_d3dVertexCount].sx = x + g_flightVpOriginX;
	g_flightVertexBuffer[g_d3dVertexCount].sy = y + g_flightVpOriginY;
	g_flightVertexBuffer[g_d3dVertexCount].sz = depth;
	g_flightVertexBuffer[g_d3dVertexCount].rhw = scaledInverseDepth;
	g_flightVertexBuffer[g_d3dVertexCount].tu = u;
	g_flightVertexBuffer[g_d3dVertexCount].tv = v;
	intensity = (int)(lightIntensity * 320.0f) + 48;
	if (intensity > 255) {
		intensity = 255;
	}
	if (g_capVertexAlpha != 0) {
		color = 65793 * intensity - 0x2000000;
	} else {
		color = 65793 * intensity - 0x1000000;
	}
	g_flightVertexBuffer[g_d3dVertexCount].color = color;
	g_flightVertexBuffer[g_d3dVertexCount].specular = 0;
	return g_d3dVertexCount++;
}

/* Does nothing. FlightSw_CopyViewportSpanMaskRle and
 * FlightSw_BuildFullViewportSpanMaskRle call it. */
// FUNCTION: XVT 0x40B520
void nullsub_2(void) {}

/* Copies the viewport span mask into the hardware z-buffer. Locks
 * g_std3DZBufferSurface, trying again while it is still drawing and returning
 * on any other failure, then decodes the mask at g_viewportSpanMaskOffset in
 * g_flightAuxBuffer for g_flightVpHeight rows, writing 2 bytes per pixel: one
 * value for the runs the mask marks (a negative run type) and the other for the
 * rest, 0xFF and 0 when g_std3DZCompareCap is 16, else 0 and 0xFF. The viewport
 * starts g_flightVpX pixels and g_flightVpY rows past half the margin between
 * display mode and surface. Only Hud_Update3DCrt calls it. */
// FUNCTION: XVT 0x40B530
void std3D_FillZBufferFromViewportMask(void)
{
	DDSURFACEDESC surfaceDesc;
	HRESULT lockResult;
	uint8_t foregroundByte;
	uint8_t backgroundByte;
	uint8_t *lockedSurface;
	uint8_t *destinationRow;
	uint8_t *destination;
	uint8_t *maskCursor;
	int runLength;
	int8_t runType;
	unsigned int decodedWidth;
	unsigned int row;

	if (g_std3DZCompareCap == 16) {
		foregroundByte = 0xFF;
		backgroundByte = 0;
	} else {
		foregroundByte = 0;
		backgroundByte = 0xFF;
	}

	memset(&surfaceDesc, 0, sizeof(surfaceDesc));
	surfaceDesc.dwSize = sizeof(surfaceDesc);
	while (1) {
		lockResult = g_std3DZBufferSurface->lpVtbl->Lock(
			g_std3DZBufferSurface, NULL, &surfaceDesc, 0, NULL);
		if (lockResult == 0) {
			break;
		}
		if (lockResult != DX_DDERR_WASSTILLDRAWING) {
			DebugPrintf("ERROR!(%x) failed to lock D3D z buffer!\n",
				    lockResult);
			return;
		}
	}

	destinationRow = surfaceDesc.lpSurface;
	lockedSurface = surfaceDesc.lpSurface;
	memset(&surfaceDesc, 0, sizeof(surfaceDesc));
	surfaceDesc.dwSize = sizeof(surfaceDesc);
	g_std3DZBufferSurface->lpVtbl->GetSurfaceDesc(g_std3DZBufferSurface,
						      &surfaceDesc);
	destinationRow +=
		((g_displayModeWidth - g_surfaceWidth) & ~1) + 2 * g_flightVpX;
	destinationRow +=
		((unsigned int)(g_displayModeHeight - g_surfaceHeight) / 2 +
		 g_flightVpY) *
		surfaceDesc.lPitch;
	maskCursor = g_flightAuxBuffer + g_viewportSpanMaskOffset;

	for (row = 0; row < g_flightVpHeight; ++row) {
		destination = destinationRow;
		runType = (int8_t)*maskCursor++;
		decodedWidth = 0;
		while (decodedWidth < g_flightVpWidth) {
			runLength = *maskCursor++;
			if (runLength == 0) {
				runLength = *maskCursor++;
				if (runLength == 0) {
					runLength = *maskCursor++ + 256;
				}
				runLength += 255;
			}
			if (runType < 0) {
				memset(destination, foregroundByte,
				       2 * runLength);
			} else {
				memset(destination, backgroundByte,
				       2 * runLength);
			}
			destination += 2 * runLength;
			runType = -runType;
			decodedWidth += runLength;
		}
		destinationRow += surfaceDesc.lPitch;
	}

	g_std3DZBufferSurface->lpVtbl->Unlock(g_std3DZBufferSurface,
					      lockedSurface);
}

/* Fills g_flightBackBuffer with
 * g_flightPalette16Bpp[g_flightTransparentColorIndex] by a color-fill blit and
 * returns what std3D_ClearZBuffer returns. */
// FUNCTION: XVT 0x40B750
int RenderScene_ClearFrameBuffers(void)
{
	DDBLTFX effects;

	memset(&effects, 0, sizeof(effects));
	effects.dwSize = sizeof(effects);
	effects.dwROP = DDROP_SRCCOPY;
	effects.dwFillColor =
		g_flightPalette16Bpp[g_flightTransparentColorIndex];
	g_flightBackBuffer->lpVtbl->Blt(g_flightBackBuffer, NULL, NULL, NULL,
					DDBLT_WAIT | DDBLT_COLORFILL, &effects);
	return std3D_ClearZBuffer();
}

/* When g_std3DZBufferSurface is set, detaches it from g_flightBackBuffer,
 * releases it and sets it to NULL. */
// FUNCTION: XVT 0x40BBC0
void std3D_DetachAndReleaseZBufferSurface(void)
{
	if (g_std3DZBufferSurface != 0) {
		g_flightBackBuffer->lpVtbl->DeleteAttachedSurface(
			g_flightBackBuffer, 0, g_std3DZBufferSurface);
		g_std3DZBufferSurface->lpVtbl->Release(g_std3DZBufferSurface);
		g_std3DZBufferSurface = 0;
	}
}

/* Sets outVert's light level, 0 to 1. A projectile
 * (CRAFT_GENUS_OTHER_PROJECTILE or CRAFT_GENUS_PLAYER_PROJECTILE) gets 1.
 * Otherwise it starts, with g_dirLightingEnabled, at 0.8 times the normal's dot
 * product with the light direction, 0 when that is negative or the model blocks
 * the light; without it, at 0.4. Each point light in g_objectPointLights the
 * model does not block then adds its intensity times diffuse plus specular,
 * when that sum is over 0. Diffuse is the normal's dot product with the offset
 * to the light over the rough distance squared. Specular, with
 * g_specularEnabled, is c to the 48th power when c is 0.5 or more, else 0,
 * where c is half the normal's dot product with the half vector (eye offset
 * plus light offset) over that vector's rough length. Stops at 1. In the
 * software path a light behind the vertex is skipped; in the hardware path one
 * is skipped when that first dot product over the rough distance is under -0.3,
 * and diffuse is 0.5 over the rough distance instead. The blocking test,
 * RenderScene_IsSegmentOccludedByObjectModel, always says no. */
// FUNCTION: XVT 0x4201F0
void RenderScene_ComputeVertexLighting(struct SceneMesh *mesh,
				       struct ProjVertex *outVert,
				       const struct OptVector *normal,
				       const struct OptVector *pos,
				       const struct OptVector *eyePos)
{
	struct OptVector lightPosition;
	int genusId;
	int lightIndex;

	genusId = mesh->pObject->genusId;
	if (genusId == CRAFT_GENUS_OTHER_PROJECTILE ||
	    genusId == CRAFT_GENUS_PLAYER_PROJECTILE) {
		outVert->lightIntensity = 1.0f;
		return;
	}
	if (g_dirLightingEnabled != 0) {
		float lightDirectionY;
		float lightDirectionZ;
		float lightDirectionX;

		lightDirectionY = (float)g_objectLightDirectionY *
				  g_renderLightDirectionUnitScale;
		lightDirectionZ = (float)g_objectLightDirectionZ *
				  g_renderLightDirectionUnitScale;
		lightDirectionX = (float)g_objectLightDirectionX *
				  g_renderLightDirectionUnitScale;
		outVert->lightIntensity =
			(lightDirectionZ * normal->z +
			 (lightDirectionX * normal->x +
			  lightDirectionY * normal->y)) *
			g_renderDirectionalLightIntensityScale;
		if (outVert->lightIntensity < 0.0f) {
			outVert->lightIntensity = 0.0f;
		} else {
			lightPosition.x =
				(float)g_objectLightDirectionX + pos->x;
			lightPosition.y =
				(float)g_objectLightDirectionY + pos->y;
			lightPosition.z =
				(float)g_objectLightDirectionZ + pos->z;
			if (RenderScene_IsSegmentOccludedByObjectModel(
				    mesh->pObject, pos, &lightPosition)) {
				outVert->lightIntensity = 0.0f;
			}
		}
	} else {
		outVert->lightIntensity = 0.40000001f;
	}

	for (lightIndex = 0; g_objectPointLightCount > lightIndex;
	     ++lightIndex) {
		const struct ObjectPointLight *light =
			&g_objectPointLights[lightIndex];
		float dx;
		float dy;
		float dz;
		float lightDot;
		float componentX;
		float componentY;
		float componentZ;
		float distance;
		float diffuse;
		float specular;
		float contribution;

		{
			componentX = (float)light->x;
			componentY = (float)light->y;
			componentZ = (float)light->z;
			dx = componentX - pos->x;
			dy = componentY - pos->y;
			dz = componentZ - pos->z;
			lightDot = normal->z * dz +
				   (normal->y * dy + normal->x * dx);

			if (g_useHardware3D == 0 && lightDot < 0.0f) {
				continue;
			}
			lightPosition.x = componentX;
			lightPosition.y = componentY;
			lightPosition.z = componentZ;
		}
		if (RenderScene_IsSegmentOccludedByObjectModel(
			    mesh->pObject, pos, &lightPosition)) {
			continue;
		}
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
		if (componentX >= componentY && componentX >= componentZ) {
			distance =
				componentX + (componentY + componentZ) *
						     g_renderRoughDistanceScale;
		} else if (componentY >= componentX &&
			   componentY >= componentZ) {
			distance =
				componentY + (componentX + componentZ) *
						     g_renderRoughDistanceScale;
		} else {
			distance =
				componentZ + (componentX + componentY) *
						     g_renderRoughDistanceScale;
		}
		if (g_useHardware3D != 0) {
			if (lightDot / distance <
			    g_renderPointLightFacingThreshold) {
				continue;
			}
			/* With 3D hardware lightDot is replaced by half the distance, so the diffuse term below
			 * becomes 0.5 over the distance and the facing term is dropped. */
			lightDot = (float)(distance * g_renderHalfDouble);
		}
		diffuse = lightDot / (distance * distance);
		if (g_specularEnabled != 0) {
			float halfX;
			float halfY;
			float halfZ;
			float halfDot;
			float cosine;

			halfX = eyePos->x - pos->x + dx;
			halfY = eyePos->y - pos->y + dy;
			halfZ = eyePos->z - pos->z + dz;
			halfDot = (normal->z * halfZ +
				   (normal->y * halfY + normal->x * halfX)) *
				  g_renderHalfFloat;
			componentX = halfX;
			componentY = halfY;
			componentZ = halfZ;
			if (halfX < 0.0f) {
				componentX = -halfX;
			}
			if (halfY < 0.0f) {
				componentY = -halfY;
			}
			if (halfZ < 0.0f) {
				componentZ = -halfZ;
			}
			if (componentX >= componentY &&
			    componentX >= componentZ) {
				distance =
					componentX *
						g_renderSpecularApproxMaxComponentScale +
					(componentY + componentZ) *
						g_renderSpecularApproxOtherComponentsScale;
			} else if (componentY >= componentX &&
				   componentY >= componentZ) {
				distance =
					componentY *
						g_renderSpecularApproxMaxComponentScale +
					(componentX + componentZ) *
						g_renderSpecularApproxOtherComponentsScale;
			} else {
				distance =
					componentZ *
						g_renderSpecularApproxMaxComponentScale +
					(componentX + componentY) *
						g_renderSpecularApproxOtherComponentsScale;
			}
			cosine = halfDot / distance;
			if (cosine >= g_renderHalfFloat) {
				specular = cosine * cosine * cosine;
				specular *= specular;
				specular *= specular;
				specular *= specular;
				specular *= specular;
			} else {
				specular = 0.0f;
			}
		} else {
			specular = 0.0f;
		}
		contribution = diffuse + specular;
		if (contribution > g_renderZeroFloat) {
			outVert->lightIntensity +=
				(float)light->intensity * contribution;
			if (outVert->lightIntensity >= 1.0f) {
				outVert->lightIntensity = 1.0f;
				return;
			}
		}
	}
}

/* Copies the face's u and v axes from faceTexGradients into gradients[0] to [2]
 * and [3] to [5], each turned by the orientation at viewPosAndOrient + 3 with
 * Math3D_RotateVec3. */
// FUNCTION: XVT 0x420DB0
void RenderScene_TransformFaceTextureGradients(
	struct SceneFace *face,
	const struct FaceTextureGradients *faceTexGradients,
	const float *viewPosAndOrient)
{
	face->gradients[0] = faceTexGradients->uAxis.x;
	face->gradients[1] = faceTexGradients->uAxis.y;
	face->gradients[2] = faceTexGradients->uAxis.z;
	Math3D_RotateVec3(&face->gradients[0], viewPosAndOrient + 3);

	face->gradients[3] = faceTexGradients->vAxis.x;
	face->gradients[4] = faceTexGradients->vAxis.y;
	face->gradients[5] = faceTexGradients->vAxis.z;
	Math3D_RotateVec3(&face->gradients[3], viewPosAndOrient + 3);
}

/* Carries a model point into view space (the orientation at viewPosAndOrient +
 * 3, then the position at viewPosAndOrient) and projects it: outProjected[2] is
 * g_projScaleInt over depth, [0] and [1] viewport x and y. Does not check for a
 * depth of 0 or less. Nothing calls this. */
// FUNCTION: XVT 0x420E10
void RenderScene_TransformProjectLegacyPoint(float outProjected[3],
					     const float point[3],
					     const float viewPosAndOrient[12])
{
	float viewPoint[3];

	viewPoint[0] = point[0];
	viewPoint[1] = point[1];
	viewPoint[2] = point[2];
	Math3D_RotateVec3(viewPoint, viewPosAndOrient + 3);
	viewPoint[0] += viewPosAndOrient[0];
	viewPoint[1] += viewPosAndOrient[1];
	viewPoint[2] += viewPosAndOrient[2];

	outProjected[2] = (float)((double)g_projScaleInt / viewPoint[2]);
	outProjected[0] = outProjected[2] * viewPoint[0];
	outProjected[1] = outProjected[2] * viewPoint[1];
	outProjected[0] += (int)(g_flightVpWidth >> 1);
	outProjected[1] += g_projOffsetY + (int)(g_flightVpHeight >> 1);
}

/* The distant form of RenderScene_TransformProjectLegacyPoint: adds 100000 to
 * the depth and scales the projection by g_projScaleInt / viewPosAndOrient[2] *
 * 100000. Nothing calls this. */
// FUNCTION: XVT 0x420EE0
void RenderScene_TransformProjectLegacyDistantPoint(
	float outProjected[3], const float point[3],
	const float viewPosAndOrient[12])
{
	float viewPoint[3];
	float distantProjectScale;

	distantProjectScale =
		(float)g_projScaleInt / viewPosAndOrient[2] * (float)100000.0;
	viewPoint[0] = point[0];
	viewPoint[1] = point[1];
	viewPoint[2] = point[2];
	Math3D_RotateVec3(viewPoint, viewPosAndOrient + 3);
	viewPoint[0] += viewPosAndOrient[0];
	viewPoint[1] += viewPosAndOrient[1];
	viewPoint[2] += viewPosAndOrient[2];
	viewPoint[2] += (float)100000.0;

	outProjected[2] = distantProjectScale / viewPoint[2];
	outProjected[0] = outProjected[2] * viewPoint[0];
	outProjected[1] = outProjected[2] * viewPoint[1];
	outProjected[0] += (int)(g_flightVpWidth >> 1);
	outProjected[1] += g_projOffsetY + (int)(g_flightVpHeight >> 1);
}

/* Appends to g_visFaceList each face of the mesh whose normal's dot product
 * with the offset from its first corner to the eye is 0 or more, setting its
 * faceIndex, pMesh, light-sample row, faceAndLayerId and a NULL pScanEdge, and
 * raising g_lightSampleSlotIndex up to 199. Sets the mesh's faceBaseIndex and
 * visFaceCount, g_visFaceCount, and g_meshEyePos: the mesh's eye, or, while
 * g_bBackdropMeshMode is set, that eye 100000 back along view z. Does not check
 * g_sceneFaceMax; the callers do. */
// FUNCTION: XVT 0x470140
void RenderScene_CullMeshFacesFromView(struct SceneMesh *mesh)
{
	struct OptVector *faceNormal;
	struct FaceRecord *faceRecord;
	struct SceneFace *outFace;
	float *modelVerts;
	int faceIndex;

	mesh->faceBaseIndex = g_visFaceCount;
	g_meshEyePos.x = mesh->eyeModelSpaceX;
	g_meshEyePos.y = mesh->eyeModelSpaceY;
	g_meshEyePos.z = mesh->eyeModelSpaceZ;
	if (g_bBackdropMeshMode) {
		g_meshEyePos.x = 0.0f;
		g_meshEyePos.y = 0.0f;
		g_meshEyePos.z = -100000.0f;
		Math3D_RotateVec3(&g_meshEyePos.x, mesh->viewToModelOrient);
		g_meshEyePos.x += mesh->eyeModelSpaceX;
		g_meshEyePos.y += mesh->eyeModelSpaceY;
		g_meshEyePos.z += mesh->eyeModelSpaceZ;
	}

	faceNormal = mesh->pFaceNormals;
	faceRecord = mesh->pFaceGeom;
	modelVerts = &mesh->pModelVerts->x;
	outFace = &g_visFaceList[g_visFaceCount];
	faceIndex = 0;
	if (mesh->faceCount > 0) {
		do {
			float viewVec[3];
			int lightSampleOffset;
			int vertexIndex;

			vertexIndex = 3 * faceRecord[faceIndex].vertexIdx[0];
			viewVec[0] = g_meshEyePos.x - modelVerts[vertexIndex];
			viewVec[1] =
				g_meshEyePos.y - modelVerts[vertexIndex + 1];
			viewVec[2] =
				g_meshEyePos.z - modelVerts[vertexIndex + 2];
			if (Math3D_Dot3(viewVec, &faceNormal[faceIndex].x) >=
			    g_sw3dZeroFloat) {
				outFace->faceIndex = faceIndex;
				outFace->pMesh = mesh;
				lightSampleOffset = g_lightSampleSlotStride;
				lightSampleOffset *= g_lightSampleSlotIndex;
				outFace->pLightSamples =
					g_sceneLightSampleData +
					12 * lightSampleOffset;
				if (g_lightSampleSlotIndex < 199) {
					++g_lightSampleSlotIndex;
				}
				outFace->faceAndLayerId =
					faceIndex + (g_curLayerId << 16);
				outFace->pScanEdge = NULL;
				++outFace;
				++g_visFaceCount;
			}

			++faceIndex;
		} while (mesh->faceCount > faceIndex);
	}

	mesh->visFaceCount = g_visFaceCount - mesh->faceBaseIndex;
}

/* Draws one mesh: in the hardware path through RenderScene_DrawMeshHardware.
 * Otherwise sets g_projVertCount and g_sceneEdgeCursor to 0 and, when
 * g_meshQueue has room and the mesh's faces, vertices and edges fit, copies it
 * into g_meshQueue and culls it; when faces are left, projects it with sw3d
 * (distant while g_bBackdropMeshMode is set), turns its faces into spans with
 * sw3d_RasterizeMeshFaces and advances g_meshQueueIndex. */
// FUNCTION: XVT 0x471E00
void RenderScene_DrawSceneMesh(struct SceneMesh *mesh)
{
	struct SceneMesh *queuedMesh;

	if (g_useHardware3D != 0) {
		RenderScene_DrawMeshHardware(mesh);
		return;
	}
	g_projVertCount = 0;
	g_sceneEdgeCursor = 0;
	if (g_meshQueueIndex != g_meshQueueMax &&
	    g_visFaceCount + mesh->faceCount <= g_sceneFaceMax &&
	    mesh->vertexCount <= g_projVertMax &&
	    mesh->edgeCount <= g_sceneEdgeMax) {
		memcpy(&g_meshQueue[g_meshQueueIndex], mesh,
		       sizeof(struct SceneMesh));
		queuedMesh = &g_meshQueue[g_meshQueueIndex];
		RenderScene_CullMeshFacesFromView(queuedMesh);
		if (queuedMesh->visFaceCount != 0) {
			if (g_bBackdropMeshMode != 0) {
				sw3d_ProjectMeshVerticesDistant(queuedMesh);
			} else {
				sw3d_ProjectMeshVertices(queuedMesh);
			}
			sw3d_RasterizeMeshFaces(queuedMesh);
			++g_meshQueueIndex;
		}
	}
}

/* Turns the mesh by the B-wing bridge's meshRotation byte, times
 * g_meshRotationByteToRadiansScale, about the axis (0, -1, 0): multiplies
 * viewToModelOrient by that rotation, turns eyeModelSpace by it, and multiplies
 * viewOrient by its transpose from the left. Ignores unusedModel. */
// FUNCTION: XVT 0x472360
void RenderScene_ApplyBwingBridgeRotation(
	struct OptimizedPolyObject *unusedModel, struct ObjectRecord *obj,
	struct SceneMesh *mesh, int bridgeMeshIndex)
{
	int bridgeRotationByte;
	float axisAngle[4];
	float rotationMatrix[16];

	(void)unusedModel;

	bridgeRotationByte = obj->mobj->pCraft->meshRotation[bridgeMeshIndex];
	axisAngle[3] = bridgeRotationByte * g_meshRotationByteToRadiansScale;
	axisAngle[0] = 0.0f;
	axisAngle[2] = 0.0f;
	axisAngle[1] = -1.0f;
	Math3D_BuildAxisAngleMatrix(rotationMatrix, axisAngle);
	Math3D_MulMatrix3x3(mesh->viewToModelOrient, rotationMatrix);
	Math3D_RotateVec3(&mesh->eyeModelSpaceX, rotationMatrix);
	Math3D_PreMulTransposedMatrix3x3(mesh->viewOrient, rotationMatrix);
}

/* Draws an object's whole model. Locks g_loadedModels[objectType], fixing its
 * pointers when the model has moved, and sets g_nodeSwitchIndex from the object
 * (0 without a mobile record). The mesh starts at the object's offset from the
 * camera turned by the Q15 camera matrix, with orientations from the
 * g_objViewMat matrix and the eye in model space; the built-in white texture is
 * made on first use. Each root node is walked with RenderScene_DrawModelNode,
 * raising g_curLayerId. For a craft, each root other than a texture is one
 * component: a component whose componentState is set is skipped, and its
 * meshRotation byte gives rotAngle; for a B-wing whose bridge meshRotation is
 * nonzero, the bridge rotation is applied to the mesh for that root and taken
 * back after. Clears the model walk's g_cur globals first and unlocks the model
 * at the end. */
// FUNCTION: XVT 0x472400
void RenderScene_DrawObjectModel(struct ObjectRecord *obj)
{
	uint16_t modelHandle;
	struct OptimizedPolyObject *model;
	int restoreMesh;
	float objectViewR0X;
	float objectViewR0Y;
	float objectViewR0Z;
	float objectViewR1X;
	float objectViewR1Y;
	float objectViewR1Z;
	struct OptNode *node;
	float objectViewR2X;
	float objectViewR2Y;
	float objectViewR2Z;
	struct SceneMesh mesh;
	struct SceneMesh savedMesh;
	int meshOrdinal;
	int rootIndex;

	modelHandle = g_loadedModels[obj->objectType];
	if (obj->mobj != NULL) {
		g_nodeSwitchIndex = obj->mobj->nodeSwitchIndex;
	} else {
		g_nodeSwitchIndex = 0;
	}
	model = (struct OptimizedPolyObject *)Memory_GetHandleBlock(
		modelHandle);
	if (model->selfMarker != model) {
		OptModel_AdjustOptimizedPolyObjectPointers(model);
	}

	memset(&mesh, 0, sizeof(mesh));
	mesh.pObject = obj;
	mesh.viewPosX =
		(float)(obj->world_x -
			g_players[g_localPlayer].viewState.cameraWorldX);
	mesh.viewPosY =
		(float)(obj->world_y -
			g_players[g_localPlayer].viewState.cameraWorldY);
	mesh.viewPosZ =
		(float)(obj->world_z -
			g_players[g_localPlayer].viewState.cameraWorldZ);
	mesh.viewOrient[0] =
		(float)g_camMatR0_X * g_renderMatrixQ15ToFloatScale;
	mesh.viewOrient[1] =
		(float)g_camMatR1_X * g_renderMatrixQ15ToFloatScale;
	mesh.viewOrient[2] =
		(float)g_camMatR2_X * g_renderMatrixQ15ToFloatScale;
	mesh.viewOrient[3] =
		(float)g_camMatR0_Y * g_renderMatrixQ15ToFloatScale;
	mesh.viewOrient[4] =
		(float)g_camMatR1_Y * g_renderMatrixQ15ToFloatScale;
	mesh.viewOrient[5] =
		(float)g_camMatR2_Y * g_renderMatrixQ15ToFloatScale;
	mesh.viewOrient[6] =
		(float)g_camMatR0_Z * g_renderMatrixQ15ToFloatScale;
	mesh.viewOrient[7] =
		(float)g_camMatR1_Z * g_renderMatrixQ15ToFloatScale;
	mesh.viewOrient[8] =
		(float)g_camMatR2_Z * g_renderMatrixQ15ToFloatScale;
	Math3D_RotateVec3(&mesh.viewPosX, mesh.viewOrient);

	objectViewR0X =
		(float)g_objViewMat_R0_X * g_renderMatrixQ15ToFloatScale;
	objectViewR0Y =
		(float)g_objViewMat_R0_Y * g_renderMatrixQ15ToFloatScale;
	mesh.viewOrient[0] = objectViewR0X;
	mesh.viewOrient[1] = objectViewR0Y;
	objectViewR0Z =
		(float)g_objViewMat_R0_Z * g_renderMatrixQ15ToFloatScale;
	mesh.viewOrient[2] = objectViewR0Z;
	objectViewR1X =
		(float)g_objViewMat_R1_X * g_renderMatrixQ15ToFloatScale;
	objectViewR1Y =
		(float)g_objViewMat_R1_Y * g_renderMatrixQ15ToFloatScale;
	objectViewR1Z =
		(float)g_objViewMat_R1_Z * g_renderMatrixQ15ToFloatScale;
	mesh.viewOrient[3] = objectViewR1X;
	mesh.viewOrient[4] = objectViewR1Y;
	mesh.viewOrient[5] = objectViewR1Z;
	objectViewR2X =
		(float)g_objViewMat_R2_X * g_renderMatrixQ15ToFloatScale;
	mesh.viewOrient[6] = objectViewR2X;
	objectViewR2Y =
		(float)g_objViewMat_R2_Y * g_renderMatrixQ15ToFloatScale;
	objectViewR2Z =
		(float)g_objViewMat_R2_Z * g_renderMatrixQ15ToFloatScale;
	mesh.eyeModelSpaceX = -mesh.viewPosX;
	mesh.eyeModelSpaceY = -mesh.viewPosY;
	mesh.eyeModelSpaceZ = -mesh.viewPosZ;
	mesh.viewToModelOrient[0] = objectViewR0X;
	mesh.viewOrient[7] = objectViewR2Y;
	mesh.viewToModelOrient[1] = objectViewR1X;
	mesh.viewToModelOrient[2] = objectViewR2X;
	mesh.viewOrient[8] = objectViewR2Z;
	mesh.viewToModelOrient[3] = objectViewR0Y;
	mesh.viewToModelOrient[4] = objectViewR1Y;
	mesh.viewToModelOrient[5] = objectViewR2Y;
	mesh.viewToModelOrient[6] = objectViewR0Z;
	mesh.viewToModelOrient[7] = objectViewR1Z;
	mesh.viewToModelOrient[8] = objectViewR2Z;
	Math3D_RotateVec3(&mesh.eyeModelSpaceX, mesh.viewToModelOrient);

	g_curMeshVertices = NULL;
	if (g_defaultWhiteTextureDescPtr == NULL) {
		g_defaultWhiteTextureDescPtr = &g_defaultWhiteTexture.header;
		g_defaultWhiteTexture.header.height =
			DEFAULT_WHITE_TEXTURE_DIMENSION;
		g_defaultWhiteTextureDescPtr->width =
			DEFAULT_WHITE_TEXTURE_DIMENSION;
		g_defaultWhiteTextureDescPtr->inlinePaletteCount = 16;
		g_defaultWhiteTextureDescPtr->palette =
			(uint16_t *)(uintptr_t)256;
		ModelTexture_BuildPalettedShadeTable(
			g_defaultWhiteTexture.data.baseTexels,
			g_defaultWhiteTextureRgb24,
			DEFAULT_WHITE_TEXTURE_DIMENSION,
			DEFAULT_WHITE_TEXTURE_DIMENSION);
	}
	g_curMeshTexCoords = NULL;
	g_curVertNormals = NULL;
	g_modelNodeWalkUnusedScratch2 = NULL;
	restoreMesh = 0;
	g_curTextureDesc = g_defaultWhiteTextureDescPtr;
	rootIndex = 0;
	g_curMeshMaterials = NULL;
	g_curVertexCount = 0;
	meshOrdinal = 0;

	for (; rootIndex < model->rootNodeCount; ++rootIndex) {
		mesh.rotAngle = 0.0f;
		node = model->rootNodes[rootIndex];
		if (node->nodeType != OPT_TEXTURE) {
			struct CraftData *craft;
			int rotationByte;

			++meshOrdinal;
			if (obj->mobj != NULL && obj->mobj->pCraft != NULL) {
				craft = obj->mobj->pCraft;
				if (craft->componentState[meshOrdinal - 1] !=
				    0) {
					continue;
				}
				rotationByte =
					craft->meshRotation[meshOrdinal - 1];
				if (obj->objectType == B_WING_OBJECT_TYPE) {
					if (g_bwingBridgeMeshIndexCache == -1) {
						g_bwingBridgeMeshIndexCache =
							ModelMesh_FindBridgeIndex(
								model);
					}
					if (g_bwingBridgeMeshIndexCache != -1 &&
					    obj->mobj->pCraft->meshRotation
							    [g_bwingBridgeMeshIndexCache] !=
						    0) {
						savedMesh = mesh;
						restoreMesh = 1;
						RenderScene_ApplyBwingBridgeRotation(
							model, obj, &mesh,
							g_bwingBridgeMeshIndexCache);
					}
				}
				mesh.rotAngle =
					rotationByte *
					g_meshRotationByteToRadiansScale;
			}
		}
		++g_curLayerId;
		RenderScene_DrawModelNode(model, node, &mesh);
		if (restoreMesh != 0) {
			mesh = savedMesh;
			restoreMesh = 0;
		}
	}
	Memory_HandleBlockDoneStub(modelHandle);
}

/* Draws one root node of an object's model, set up as
 * RenderScene_DrawObjectModel sets up the whole; a component object (type 89)
 * uses its source object's model. Texture roots are walked as well, and each
 * one met raises rootNodeIndex by 1, so texture roots ahead of the wanted one
 * do not count. No component is skipped and no rotation is applied. */
// FUNCTION: XVT 0x4728D0
void RenderScene_DrawSelectedRootNode(struct ObjectRecord *obj,
				      int rootNodeIndex)
{
	int objectType;
	uint16_t modelHandle;
	struct OptimizedPolyObject *model;
	float objectViewR0X;
	float objectViewR0Y;
	float objectViewR0Z;
	float objectViewR1X;
	float objectViewR1Y;
	float objectViewR1Z;
	float objectViewR2X;
	float objectViewR2Y;
	float objectViewR2Z;
	struct SceneMesh mesh;
	int rootIndex;

	objectType = obj->objectType;
	if (objectType == COMPONENT_OBJECT_TYPE && obj->mobj != NULL) {
		objectType = obj->mobj->sourceObjectType;
	}
	if (obj->mobj != NULL) {
		g_nodeSwitchIndex = obj->mobj->nodeSwitchIndex;
	} else {
		g_nodeSwitchIndex = 0;
	}

	modelHandle = g_loadedModels[objectType];
	model = (struct OptimizedPolyObject *)Memory_GetHandleBlock(
		modelHandle);
	if (model->selfMarker != model) {
		OptModel_AdjustOptimizedPolyObjectPointers(model);
	}

	memset(&mesh, 0, sizeof(mesh));
	mesh.pObject = obj;
	mesh.viewPosX =
		(float)(obj->world_x -
			g_players[g_localPlayer].viewState.cameraWorldX);
	mesh.viewPosY =
		(float)(obj->world_y -
			g_players[g_localPlayer].viewState.cameraWorldY);
	mesh.viewPosZ =
		(float)(obj->world_z -
			g_players[g_localPlayer].viewState.cameraWorldZ);
	mesh.viewOrient[0] =
		(float)g_camMatR0_X * g_renderMatrixQ15ToFloatScale;
	mesh.viewOrient[1] =
		(float)g_camMatR1_X * g_renderMatrixQ15ToFloatScale;
	mesh.viewOrient[2] =
		(float)g_camMatR2_X * g_renderMatrixQ15ToFloatScale;
	mesh.viewOrient[3] =
		(float)g_camMatR0_Y * g_renderMatrixQ15ToFloatScale;
	mesh.viewOrient[4] =
		(float)g_camMatR1_Y * g_renderMatrixQ15ToFloatScale;
	mesh.viewOrient[5] =
		(float)g_camMatR2_Y * g_renderMatrixQ15ToFloatScale;
	mesh.viewOrient[6] =
		(float)g_camMatR0_Z * g_renderMatrixQ15ToFloatScale;
	mesh.viewOrient[7] =
		(float)g_camMatR1_Z * g_renderMatrixQ15ToFloatScale;
	mesh.viewOrient[8] =
		(float)g_camMatR2_Z * g_renderMatrixQ15ToFloatScale;
	Math3D_RotateVec3(&mesh.viewPosX, mesh.viewOrient);

	objectViewR0X =
		(float)g_objViewMat_R0_X * g_renderMatrixQ15ToFloatScale;
	objectViewR0Y =
		(float)g_objViewMat_R0_Y * g_renderMatrixQ15ToFloatScale;
	mesh.viewOrient[0] = objectViewR0X;
	mesh.viewOrient[1] = objectViewR0Y;
	objectViewR0Z =
		(float)g_objViewMat_R0_Z * g_renderMatrixQ15ToFloatScale;
	mesh.viewOrient[2] = objectViewR0Z;
	objectViewR1X =
		(float)g_objViewMat_R1_X * g_renderMatrixQ15ToFloatScale;
	objectViewR1Y =
		(float)g_objViewMat_R1_Y * g_renderMatrixQ15ToFloatScale;
	objectViewR1Z =
		(float)g_objViewMat_R1_Z * g_renderMatrixQ15ToFloatScale;
	mesh.viewOrient[3] = objectViewR1X;
	mesh.viewOrient[4] = objectViewR1Y;
	mesh.viewOrient[5] = objectViewR1Z;
	objectViewR2X =
		(float)g_objViewMat_R2_X * g_renderMatrixQ15ToFloatScale;
	mesh.viewOrient[6] = objectViewR2X;
	objectViewR2Y =
		(float)g_objViewMat_R2_Y * g_renderMatrixQ15ToFloatScale;
	objectViewR2Z =
		(float)g_objViewMat_R2_Z * g_renderMatrixQ15ToFloatScale;
	mesh.eyeModelSpaceX = -mesh.viewPosX;
	mesh.eyeModelSpaceY = -mesh.viewPosY;
	mesh.eyeModelSpaceZ = -mesh.viewPosZ;
	mesh.viewToModelOrient[0] = objectViewR0X;
	mesh.viewOrient[7] = objectViewR2Y;
	mesh.viewToModelOrient[1] = objectViewR1X;
	mesh.viewToModelOrient[2] = objectViewR2X;
	mesh.viewOrient[8] = objectViewR2Z;
	mesh.viewToModelOrient[3] = objectViewR0Y;
	mesh.viewToModelOrient[4] = objectViewR1Y;
	mesh.viewToModelOrient[5] = objectViewR2Y;
	mesh.viewToModelOrient[6] = objectViewR0Z;
	mesh.viewToModelOrient[7] = objectViewR1Z;
	mesh.viewToModelOrient[8] = objectViewR2Z;
	Math3D_RotateVec3(&mesh.eyeModelSpaceX, mesh.viewToModelOrient);

	g_curMeshVertices = NULL;
	if (g_defaultWhiteTextureDescPtr == NULL) {
		g_defaultWhiteTextureDescPtr = &g_defaultWhiteTexture.header;
		g_defaultWhiteTexture.header.height =
			DEFAULT_WHITE_TEXTURE_DIMENSION;
		g_defaultWhiteTextureDescPtr->width =
			DEFAULT_WHITE_TEXTURE_DIMENSION;
		g_defaultWhiteTextureDescPtr->inlinePaletteCount = 16;
		g_defaultWhiteTextureDescPtr->palette =
			(uint16_t *)(uintptr_t)256;
		ModelTexture_BuildPalettedShadeTable(
			g_defaultWhiteTexture.data.baseTexels,
			g_defaultWhiteTextureRgb24,
			DEFAULT_WHITE_TEXTURE_DIMENSION,
			DEFAULT_WHITE_TEXTURE_DIMENSION);
	}
	g_curTextureDesc = g_defaultWhiteTextureDescPtr;
	g_curMeshTexCoords = NULL;
	g_curVertNormals = NULL;
	g_modelNodeWalkUnusedScratch2 = NULL;
	g_curMeshMaterials = NULL;
	g_curVertexCount = 0;

	for (rootIndex = 0; rootIndex < model->rootNodeCount; ++rootIndex) {
		struct OptNode *rootNode = model->rootNodes[rootIndex];

		if (rootNode->nodeType == OPT_TEXTURE) {
			++rootNodeIndex;
			++g_curLayerId;
			RenderScene_DrawModelNode(model, rootNode, &mesh);
			continue;
		}
		if (rootIndex == rootNodeIndex) {
			++g_curLayerId;
			RenderScene_DrawModelNode(model, rootNode, &mesh);
		}
	}
	Memory_HandleBlockDoneStub(modelHandle);
}

/* Walks one model node and those below it, updating mesh and drawing face data.
 * Follows node references first (in the modern build through the cached
 * resolver; in the original, while g_cacheResolvedOptNodeRefs is set, through a
 * pointer cached in the node) and returns when one resolves to nothing. By
 * type: face data sets the mesh's faces, normals and texture axes, takes
 * g_curTextureDesc when the mesh has no texture, and calls
 * RenderScene_DrawSceneMesh, with the generated vertex normals when the mesh
 * has none; transform, translation, rotation and scale nodes update viewPos,
 * viewOrient, viewToModelOrient and eyeModelSpace; vertex, normal and
 * texture-coordinate nodes set those arrays; a texture node sets the texture,
 * palettes and g_curTextureDesc; a material binding copies g_curMeshMaterials
 * into the field its payload count picks; a rotate-and-scale node turns the
 * mesh by rotAngle about its axis through its pivot; a face group picks a child
 * by distance, or by g_forcedLodLevel, and a node switch picks child
 * g_nodeSwitchIndex + 1, at most the last. Then walks the picked child, or none
 * when a face group's thresholds all fail, or with no pick every child with a
 * copy of the mesh after clearing the walk's g_cur globals. Raises g_curLayerId
 * before each child. */
// FUNCTION: XVT 0x472C90
void RenderScene_DrawModelNode(struct OptimizedPolyObject *model,
			       struct OptNode *node, struct SceneMesh *mesh)
{
	struct ModelNodeSelectionState {
		float lodThreshold;
		int nodeSwitchSelection;
	} selection;

	struct OptNode *currentNode;
	void *nodeData;
	int lodChildSelection;
	float axisAngle[4];
	float rotationMatrix[16];
	struct SceneMesh childMesh;
	int childIndex;

	currentNode = node;
	if (currentNode == NULL) {
		return;
	}
	lodChildSelection = 0;
	selection.nodeSwitchSelection = 0;
	while (currentNode->nodeType == OPT_NODEREF) {
		if (g_cacheResolvedOptNodeRefs != 0) {
#ifdef XVT_MODERN
			currentNode = XvtOpt_ResolveCached(model, currentNode);
#else

			char **referenceName;

			referenceName = (char **)&currentNode->payload;
			if (**referenceName == '\0') {
				currentNode =
					(struct OptNode *)currentNode->pName;
			} else {
				currentNode->pName =
					(char *)OptModel_ResolveNodeRef(
						model, *referenceName);
				**referenceName = '\0';
				currentNode =
					(struct OptNode *)currentNode->pName;
			}
#endif
		} else {
			currentNode = OptModel_ResolveNodeRef(
				model, (const char *)currentNode->payload);
		}
		if (currentNode == NULL) {
			return;
		}
	}

	nodeData = currentNode->payload;
	if (nodeData != NULL) {
		struct OptVector *parameters;

		parameters = (struct OptVector *)nodeData;
		switch (currentNode->nodeType) {
		case OPT_FACEDATA:
		case OPT_FACEDATA_QUAD_MESH:
		case OPT_FACEDATA_FACE_SET:
		case OPT_FACEDATA_TRIANGLE_STRIP_SET: {
			struct OptPackedFaceData *faceData;
			struct FaceRecord *faceGeometry;
			struct OptVector *faceNormals;
			struct FaceTextureGradients *faceTexturing;
			struct OptVector *generatedNormals;

			faceData = (struct OptPackedFaceData *)nodeData;
			mesh->faceCount = currentNode->payloadCount;
			mesh->edgeCount = faceData->edgeCount;
			faceGeometry = (struct FaceRecord *)faceData->records;
			mesh->pFaceGeom = faceGeometry;
			faceNormals = (struct OptVector *)&faceGeometry
				[currentNode->payloadCount];
			mesh->pFaceNormals = faceNormals;
			faceTexturing =
				(struct FaceTextureGradients *)&faceNormals
					[currentNode->payloadCount];
			mesh->pFaceTexturing = faceTexturing;
			generatedNormals =
				&faceTexturing[currentNode->payloadCount].uAxis;
			if (mesh->pMaterial == NULL) {
				int paletteOffset;

				mesh->pMaterial = g_curTextureDesc;
				mesh->pTexels = mesh->pMaterial;
				mesh->pTexels = (uint8_t *)mesh->pTexels +
						sizeof(struct OptTextureData);
				if (g_curTextureDesc->inlinePaletteCount != 0) {
					mesh->pPalette = mesh->pTexels;
					paletteOffset =
						((struct OptTextureData *)
							 mesh->pMaterial)
							->width *
						((struct OptTextureData *)
							 mesh->pMaterial)
							->height;
					if (((struct OptTextureData *)
						     mesh->pMaterial)
						    ->textureSize ==
					    paletteOffset) {
						mesh->pPalette =
							(uint8_t *)
								mesh->pTexels +
							((struct OptTextureData
								  *)mesh
								 ->pMaterial)
								->dataSize;
					} else {
						mesh->pPalette =
							(uint8_t *)
								mesh->pTexels +
							paletteOffset;
					}
				} else {
					mesh->pPalette =
						g_curTextureDesc->palette;
				}
				mesh->pPalette = (uint8_t *)mesh->pPalette +
						 OPT_INDEXED_SHADE_TABLE_SIZE;
				mesh->pColorKeyPalette =
					(uint16_t *)mesh->pPalette;
				mesh->pPalette = (uint8_t *)mesh->pPalette -
						 OPT_INDEXED_SHADE_TABLE_SIZE;
			}
			if (mesh->pVertNormals == NULL) {
				mesh->pVertNormals = generatedNormals;
				RenderScene_DrawSceneMesh(mesh);
				mesh->pVertNormals = NULL;
			} else {
				RenderScene_DrawSceneMesh(mesh);
			}
			break;
		}
		case OPT_TRANSFORM:
			Math3D_MulMatrix3x3(mesh->viewOrient, &parameters[1].x);
			Math3D_RotateVec3(&mesh->viewPosX, &parameters[1].x);
			mesh->viewPosX += parameters->x;
			mesh->viewPosY += parameters->y;
			mesh->viewPosZ += parameters->z;
			Math3D_PreMulTransposedMatrix3x3(
				mesh->viewToModelOrient, &parameters[1].x);
			mesh->eyeModelSpaceX -= Math3D_RotateVec3X(
				&parameters->x, mesh->viewToModelOrient);
			mesh->eyeModelSpaceY -= Math3D_RotateVec3Y(
				&parameters->x, mesh->viewToModelOrient);
			mesh->eyeModelSpaceZ -= Math3D_RotateVec3Z(
				&parameters->x, mesh->viewToModelOrient);
			break;
		case OPT_MESHVERTS:
			mesh->vertexCount = currentNode->payloadCount;
			mesh->pModelVerts = parameters;
			break;
		case OPT_TRANSLATION:
			mesh->viewPosX += parameters->x;
			mesh->viewPosY += parameters->y;
			mesh->viewPosZ += parameters->z;
			mesh->eyeModelSpaceX -= Math3D_RotateVec3X(
				&parameters->x, mesh->viewToModelOrient);
			mesh->eyeModelSpaceY -= Math3D_RotateVec3Y(
				&parameters->x, mesh->viewToModelOrient);
			mesh->eyeModelSpaceZ -= Math3D_RotateVec3Z(
				&parameters->x, mesh->viewToModelOrient);
			break;
		case OPT_ROTATION:
			Math3D_MulMatrix3x3(mesh->viewOrient,
					    (const float *)nodeData);
			Math3D_RotateVec3(&mesh->viewPosX,
					  (const float *)nodeData);
			Math3D_PreMulTransposedMatrix3x3(
				mesh->viewToModelOrient,
				(const float *)nodeData);
			break;
		case OPT_SCALE: {
			float *scaleX;
			float *scaleY;
			float *scaleZ;
			float inverseScale;

			scaleX = &parameters->x;
			scaleY = &parameters->y;
			scaleZ = &parameters->z;
			mesh->viewOrient[0] = mesh->viewOrient[0] * *scaleX;
			mesh->viewOrient[1] = mesh->viewOrient[1] * *scaleY;
			mesh->viewOrient[2] = mesh->viewOrient[2] * *scaleZ;
			mesh->viewOrient[3] = mesh->viewOrient[3] * *scaleX;
			mesh->viewOrient[4] = mesh->viewOrient[4] * *scaleY;
			mesh->viewOrient[5] = mesh->viewOrient[5] * *scaleZ;
			mesh->viewOrient[6] = mesh->viewOrient[6] * *scaleX;
			mesh->viewOrient[7] = mesh->viewOrient[7] * *scaleY;
			mesh->viewOrient[8] = mesh->viewOrient[8] * *scaleZ;
			mesh->viewPosX = mesh->viewPosX * *scaleX;
			mesh->viewPosY = mesh->viewPosY * *scaleY;
			mesh->viewPosZ = mesh->viewPosZ * *scaleZ;

			inverseScale = 1.0f / *scaleX;
			mesh->viewToModelOrient[0] =
				mesh->viewToModelOrient[0] * inverseScale;
			mesh->viewToModelOrient[1] =
				mesh->viewToModelOrient[1] * inverseScale;
			mesh->viewToModelOrient[2] =
				mesh->viewToModelOrient[2] * inverseScale;
			inverseScale = 1.0f / *scaleY;
			mesh->viewToModelOrient[3] =
				mesh->viewToModelOrient[3] * inverseScale;
			mesh->viewToModelOrient[4] =
				mesh->viewToModelOrient[4] * inverseScale;
			mesh->viewToModelOrient[5] =
				mesh->viewToModelOrient[5] * inverseScale;
			inverseScale = 1.0f / *scaleZ;
			mesh->viewToModelOrient[6] =
				mesh->viewToModelOrient[6] * inverseScale;
			mesh->viewToModelOrient[7] =
				mesh->viewToModelOrient[7] * inverseScale;
			mesh->viewToModelOrient[8] =
				mesh->viewToModelOrient[8] * inverseScale;
			break;
		}
		case OPT_MATERIAL_BINDING:
			if (currentNode->payloadCount == 8 ||
			    currentNode->payloadCount == 7) {
				memcpy(&mesh->perVertexMaterials,
				       &g_curMeshMaterials,
				       sizeof(mesh->perVertexMaterials));
			} else if (currentNode->payloadCount == 6 ||
				   currentNode->payloadCount == 5) {
				memcpy(&mesh->perFaceMaterials,
				       &g_curMeshMaterials,
				       sizeof(mesh->perFaceMaterials));
			} else {
				memcpy(&mesh->baseColorAndMaterials[3],
				       &g_curMeshMaterials,
				       sizeof(mesh->baseColorAndMaterials[3]));
			}
			break;
		case OPT_VERTNORMALS:
			g_curVertNormals = parameters;
			mesh->pVertNormals = parameters;
			break;
		case OPT_TEXCOORDS:
			mesh->pUVs = (struct OptTexCoord *)nodeData;
			break;
		case OPT_BASE_COLOR:
			mesh->baseColorAndMaterials[0] = ((int *)nodeData)[0];
			mesh->baseColorAndMaterials[1] = ((int *)nodeData)[1];
			mesh->baseColorAndMaterials[2] = ((int *)nodeData)[2];
			break;
		case OPT_TEXTURE: {
			int paletteOffset;

			mesh->pTextureName = currentNode->pName;
			mesh->pMaterial = currentNode->payload;
			g_curTextureDesc =
				(struct OptTextureData *)mesh->pMaterial;
			mesh->pTexels = mesh->pMaterial;
			mesh->pTexels = (uint8_t *)mesh->pTexels +
					sizeof(struct OptTextureData);
			if (g_curTextureDesc->inlinePaletteCount != 0) {
				mesh->pPalette = mesh->pTexels;
				paletteOffset = ((struct OptTextureData *)
							 mesh->pMaterial)
							->width *
						((struct OptTextureData *)
							 mesh->pMaterial)
							->height;
				if (((struct OptTextureData *)mesh->pMaterial)
					    ->textureSize == paletteOffset) {
					paletteOffset =
						((struct OptTextureData *)
							 mesh->pMaterial)
							->dataSize;
				}
				mesh->pPalette = (uint8_t *)mesh->pTexels +
						 paletteOffset;
			} else {
				mesh->pPalette = g_curTextureDesc->palette;
			}
			mesh->pPalette = (uint8_t *)mesh->pPalette +
					 OPT_INDEXED_SHADE_TABLE_SIZE;
			mesh->pColorKeyPalette = (uint16_t *)mesh->pPalette;
			mesh->pPalette = (uint8_t *)mesh->pPalette -
					 OPT_INDEXED_SHADE_TABLE_SIZE;
			break;
		}
		case OPT_FACEGROUP:
			if (g_viewSpaceDepth <= 0 || g_forcedLodLevel != 0) {
				lodChildSelection = g_forcedLodLevel;
				if (g_forcedLodLevel == 0) {
					lodChildSelection = 1;
				} else if (currentNode->childCount <
					   g_forcedLodLevel) {
					lodChildSelection = -1;
				}
			} else {
				selection.lodThreshold = 1.0f;
				if (g_lodDistanceScale > 0.0f) {
					selection.lodThreshold =
						g_sw3dUnitFloat /
						((float)g_viewSpaceDepth *
						 g_lodDistanceScale);
				}
				lodChildSelection = 1;
				while (lodChildSelection <=
					       currentNode->childCount &&
				       ((float *)nodeData)[lodChildSelection -
							   1] >
					       selection.lodThreshold) {
					++lodChildSelection;
				}
				if (lodChildSelection >
				    currentNode->childCount) {
					lodChildSelection = -1;
				}
			}
			break;
		case OPT_ROTSCALE:
			if (mesh->rotAngle != 0.0f) {
				struct OptVector *pivot;
				struct OptVector *axis;
				float *pivotY;
				float *pivotZ;

				pivot = parameters;
				axis = pivot + 1;
				pivotY = &pivot->y;
				pivotZ = &pivot->z;
				mesh->eyeModelSpaceX -= pivot->x;
				mesh->eyeModelSpaceY -= *pivotY;
				mesh->eyeModelSpaceZ -= *pivotZ;
				mesh->viewPosX += Math3D_RotateVec3X(
					&pivot->x, mesh->viewOrient);
				mesh->viewPosY += Math3D_RotateVec3Y(
					&pivot->x, mesh->viewOrient);
				mesh->viewPosZ += Math3D_RotateVec3Z(
					&pivot->x, mesh->viewOrient);
				axisAngle[0] =
					axis->x * g_optAxisQ15ToFloatScale;
				axisAngle[1] =
					axis->y * g_optAxisQ15ToFloatScale;
				axisAngle[2] =
					axis->z * g_optAxisQ15ToFloatScale;
				axisAngle[3] = mesh->rotAngle;
				Math3D_BuildAxisAngleMatrix(rotationMatrix,
							    axisAngle);
				Math3D_MulMatrix3x3(mesh->viewToModelOrient,
						    rotationMatrix);
				Math3D_RotateVec3(&mesh->eyeModelSpaceX,
						  rotationMatrix);
				Math3D_PreMulTransposedMatrix3x3(
					mesh->viewOrient, rotationMatrix);
				mesh->eyeModelSpaceX += pivot->x;
				mesh->eyeModelSpaceY += *pivotY;
				mesh->eyeModelSpaceZ += *pivotZ;
				mesh->viewPosX -= Math3D_RotateVec3X(
					&pivot->x, mesh->viewOrient);
				mesh->viewPosY -= Math3D_RotateVec3Y(
					&pivot->x, mesh->viewOrient);
				mesh->viewPosZ -= Math3D_RotateVec3Z(
					&pivot->x, mesh->viewOrient);
			}
			break;
		case OPT_NODESWITCH:
			selection.nodeSwitchSelection = g_nodeSwitchIndex + 1;
			if (selection.nodeSwitchSelection >
			    currentNode->childCount) {
				selection.nodeSwitchSelection =
					currentNode->childCount;
			}
			break;
		default:
			break;
		}
	} else {
		switch (currentNode->nodeType) {
		case OPT_MATERIAL_BINDING:
			if (currentNode->payloadCount == 8 ||
			    currentNode->payloadCount == 7) {
				memcpy(&mesh->perVertexMaterials,
				       &g_curMeshMaterials,
				       sizeof(mesh->perVertexMaterials));
			} else if (currentNode->payloadCount == 6 ||
				   currentNode->payloadCount == 5) {
				memcpy(&mesh->perFaceMaterials,
				       &g_curMeshMaterials,
				       sizeof(mesh->perFaceMaterials));
			} else {
				memcpy(&mesh->baseColorAndMaterials[3],
				       &g_curMeshMaterials,
				       sizeof(mesh->baseColorAndMaterials[3]));
			}
			break;
		case OPT_TEXTURE: {
			int paletteOffset;

			mesh->pTextureName = currentNode->pName;
			mesh->pMaterial = currentNode->payload;
			g_curTextureDesc =
				(struct OptTextureData *)mesh->pMaterial;
			mesh->pTexels = mesh->pMaterial;
			mesh->pTexels = (uint8_t *)mesh->pTexels +
					sizeof(struct OptTextureData);
			if (g_curTextureDesc->inlinePaletteCount != 0) {
				mesh->pPalette = mesh->pTexels;
				paletteOffset = ((struct OptTextureData *)
							 mesh->pMaterial)
							->width *
						((struct OptTextureData *)
							 mesh->pMaterial)
							->height;
				if (((struct OptTextureData *)mesh->pMaterial)
					    ->textureSize == paletteOffset) {
					paletteOffset =
						((struct OptTextureData *)
							 mesh->pMaterial)
							->dataSize;
				}
				mesh->pPalette = (uint8_t *)mesh->pTexels +
						 paletteOffset;
			} else {
				mesh->pPalette = g_curTextureDesc->palette;
			}
			mesh->pPalette = (uint8_t *)mesh->pPalette +
					 OPT_INDEXED_SHADE_TABLE_SIZE;
			mesh->pColorKeyPalette = (uint16_t *)mesh->pPalette;
			mesh->pPalette = (uint8_t *)mesh->pPalette -
					 OPT_INDEXED_SHADE_TABLE_SIZE;
			break;
		}
		case OPT_NODESWITCH:
			selection.nodeSwitchSelection = g_nodeSwitchIndex + 1;
			if (selection.nodeSwitchSelection >
			    currentNode->childCount) {
				selection.nodeSwitchSelection =
					currentNode->childCount;
			}
			break;
		case OPT_TEXCOORD_BINDING:
		default:
			break;
		}
	}

	if (currentNode->childCount == 0) {
		return;
	}
	if (selection.nodeSwitchSelection != 0) {
		++g_curLayerId;
		RenderScene_DrawModelNode(
			model,
			currentNode
				->pChildren[selection.nodeSwitchSelection - 1],
			mesh);
	} else if (lodChildSelection != 0) {
		if (lodChildSelection != -1) {
			++g_curLayerId;
			RenderScene_DrawModelNode(
				model,
				currentNode->pChildren[lodChildSelection - 1],
				mesh);
		}
	} else {
		childMesh = *mesh;
		g_curMeshVertices = NULL;
		g_curMeshTexCoords = NULL;
		g_curVertNormals = NULL;
		g_modelNodeWalkUnusedScratch2 = NULL;
		g_curMeshMaterials = NULL;
		g_curVertexCount = 0;
		for (childIndex = 0; childIndex < currentNode->childCount;
		     ++childIndex) {
			++g_curLayerId;
			RenderScene_DrawModelNode(
				model, currentNode->pChildren[childIndex],
				&childMesh);
		}
	}
}

/* Flips g_vertexLightOcclusionEnabled between 0 and 1. Nothing calls this. */
// FUNCTION: XVT 0x473550
void RenderScene_ToggleVertexLightOcclusion(void)
{
	g_vertexLightOcclusionEnabled = !g_vertexLightOcclusionEnabled;
}

/* Returns g_vertexLightOcclusionEnabled. Nothing calls this. */
// FUNCTION: XVT 0x473570
int RenderScene_GetVertexLightOcclusionEnabled(void)
{
	return g_vertexLightOcclusionEnabled;
}

/* Returns 1 when the segment from segmentStart to segmentEnd crosses a face of
 * the object's model (RenderScene_TestSegmentAgainstModelNode on each root),
 * else 0. Returns 0 at once while g_vertexLightOcclusionEnabled is 0, which it
 * always is. Otherwise unlocks the model's handle and locks it again, and
 * clears the model walk's g_cur globals. */
// FUNCTION: XVT 0x473580
int RenderScene_IsSegmentOccludedByObjectModel(
	struct ObjectRecord *object, const struct OptVector *segmentStart,
	const struct OptVector *segmentEnd)
{
	uint16_t modelHandle;
	struct OptimizedPolyObject *model;
	struct SceneMesh mesh;
	int rootIndex;

	if (!g_vertexLightOcclusionEnabled) {
		return 0;
	}
	modelHandle = g_loadedModels[object->objectType];
	Memory_HandleBlockDoneStub(modelHandle);
	model = (struct OptimizedPolyObject *)Memory_GetHandleBlock(
		modelHandle);
	if (model->selfMarker != model) {
		OptModel_AdjustOptimizedPolyObjectPointers(model);
	}
	memset(&mesh, 0, sizeof(mesh));
	mesh.pObject = object;
	mesh.viewOrient[0] = 1.0f;
	mesh.viewOrient[1] = 0.0f;
	mesh.viewOrient[2] = 0.0f;
	mesh.viewOrient[3] = 0.0f;
	mesh.viewOrient[4] = 1.0f;
	mesh.viewOrient[5] = 0.0f;
	mesh.viewOrient[6] = 0.0f;
	mesh.viewOrient[7] = 0.0f;
	mesh.viewOrient[8] = 1.0f;
	mesh.viewToModelOrient[0] = 1.0f;
	mesh.viewToModelOrient[1] = 0.0f;
	mesh.viewToModelOrient[2] = 0.0f;
	mesh.viewToModelOrient[3] = 0.0f;
	mesh.viewToModelOrient[4] = 1.0f;
	mesh.viewToModelOrient[5] = 0.0f;
	mesh.viewToModelOrient[6] = 0.0f;
	mesh.viewToModelOrient[7] = 0.0f;
	mesh.viewToModelOrient[8] = 1.0f;
	g_curMeshVertices = NULL;
	g_curMeshTexCoords = NULL;
	g_curVertNormals = NULL;
	g_modelNodeWalkUnusedScratch2 = NULL;
	g_curMeshMaterials = NULL;
	g_curVertexCount = 0;
	for (rootIndex = 0; rootIndex < model->rootNodeCount; ++rootIndex) {
		if (RenderScene_TestSegmentAgainstModelNode(
			    model, model->rootNodes[rootIndex], &mesh,
			    segmentStart, segmentEnd)) {
			return 1;
		}
	}
	return 0;
}

/* Returns 1 when the segment crosses a face at this node or below it, else 0.
 * Follows node references, returning 0 when one resolves to nothing; applies
 * transform, translation, rotation and scale nodes to mesh as
 * RenderScene_DrawModelNode does and sets vertices and normals; tests face data
 * with RenderScene_TestSegmentAgainstMeshFaces; then tests every child with a
 * copy of the mesh. It makes no face-group or node-switch choice, so every
 * child is tested, and the test reads the stored vertices, so the transforms it
 * applies do not move them. */
// FUNCTION: XVT 0x4736B0
int RenderScene_TestSegmentAgainstModelNode(
	struct OptimizedPolyObject *model, struct OptNode *node,
	struct SceneMesh *mesh, const struct OptVector *segmentStart,
	const struct OptVector *segmentEnd)
{
	struct OptVector *nodePayload;
	int childIndex;
	struct SceneMesh childMesh;

	if (node == NULL) {
		return 0;
	}
	while (node->nodeType == OPT_NODEREF) {
		node = OptModel_ResolveNodeRef(model,
					       (const char *)node->payload);
		if (node == NULL) {
			return 0;
		}
	}
	nodePayload = (struct OptVector *)node->payload;
	if (nodePayload != NULL) {
		switch (node->nodeType) {
		case OPT_FACEDATA:
		case OPT_FACEDATA_QUAD_MESH:
		case OPT_FACEDATA_FACE_SET:
		case OPT_FACEDATA_TRIANGLE_STRIP_SET: {
			struct OptPackedFaceData *faceData =
				(struct OptPackedFaceData *)nodePayload;
			struct FaceRecord *faceGeometry;
			int hit;
			struct OptVector *faceNormals;
			struct FaceTextureGradients *texturing;
			struct OptVector *generatedNormals;
			struct OptVector **vertexNormals;

			mesh->faceCount = node->payloadCount;
			mesh->edgeCount = faceData->edgeCount;
			nodePayload = (struct OptVector *)faceData->records;
			faceGeometry = (struct FaceRecord *)nodePayload;
			mesh->pFaceGeom = faceGeometry;
			faceNormals =
				(struct OptVector
					 *)&faceGeometry[node->payloadCount];
			mesh->pFaceNormals = faceNormals;
			texturing = (struct FaceTextureGradients
					     *)&faceNormals[node->payloadCount];
			mesh->pFaceTexturing = texturing;
			generatedNormals = &texturing[node->payloadCount].uAxis;
			vertexNormals = &mesh->pVertNormals;
			if (*vertexNormals == NULL) {
				mesh->pVertNormals = generatedNormals;
				hit = RenderScene_TestSegmentAgainstMeshFaces(
					mesh, segmentStart, segmentEnd);
				if (hit) {
					return 1;
				}
				mesh->pVertNormals = NULL;
			} else {
				hit = RenderScene_TestSegmentAgainstMeshFaces(
					mesh, segmentStart, segmentEnd);
				if (hit) {
					return 1;
				}
			}
			break;
		}
		case OPT_TRANSFORM:
			Math3D_MulMatrix3x3(mesh->viewOrient,
					    &nodePayload[1].x);
			Math3D_RotateVec3(&mesh->viewPosX, &nodePayload[1].x);
			mesh->viewPosX = RenderScene_AddTranslation(
				mesh->viewPosX, nodePayload->x);
			mesh->viewPosY += nodePayload->y;
			mesh->viewPosZ += nodePayload->z;
			Math3D_PreMulTransposedMatrix3x3(
				mesh->viewToModelOrient, &nodePayload[1].x);
			mesh->eyeModelSpaceX -= Math3D_RotateVec3X(
				&nodePayload->x, mesh->viewToModelOrient);
			mesh->eyeModelSpaceY -= Math3D_RotateVec3Y(
				&nodePayload->x, mesh->viewToModelOrient);
			mesh->eyeModelSpaceZ -= Math3D_RotateVec3Z(
				&nodePayload->x, mesh->viewToModelOrient);
			break;
		case OPT_MESHVERTS:
			mesh->vertexCount = node->payloadCount;
			mesh->pModelVerts = nodePayload;
			break;
		case OPT_TRANSLATION:
			mesh->viewPosX = RenderScene_AddTranslation(
				mesh->viewPosX, nodePayload->x);
			mesh->viewPosY += nodePayload->y;
			mesh->viewPosZ += nodePayload->z;
			mesh->eyeModelSpaceX -= Math3D_RotateVec3X(
				&nodePayload->x, mesh->viewToModelOrient);
			mesh->eyeModelSpaceY -= Math3D_RotateVec3Y(
				&nodePayload->x, mesh->viewToModelOrient);
			mesh->eyeModelSpaceZ -= Math3D_RotateVec3Z(
				&nodePayload->x, mesh->viewToModelOrient);
			break;
		case OPT_ROTATION:
			Math3D_MulMatrix3x3(mesh->viewOrient,
					    (const float *)nodePayload);
			Math3D_RotateVec3(&mesh->viewPosX, &nodePayload->x);
			Math3D_PreMulTransposedMatrix3x3(
				mesh->viewToModelOrient, &nodePayload->x);
			break;
		case OPT_SCALE: {
			float *scaleX = &nodePayload->x;
			float *scaleY = &nodePayload->y;
			float *scaleZ = &nodePayload->z;
			float *orientation = mesh->viewToModelOrient;
			float inverseScale;

			mesh->viewOrient[0] *= *scaleX;
			mesh->viewOrient[1] *= *scaleY;
			mesh->viewOrient[2] *= *scaleZ;
			mesh->viewOrient[3] *= *scaleX;
			mesh->viewOrient[4] *= *scaleY;
			mesh->viewOrient[5] *= *scaleZ;
			mesh->viewOrient[6] *= *scaleX;
			mesh->viewOrient[7] *= *scaleY;
			mesh->viewOrient[8] *= *scaleZ;
			mesh->viewPosX *= *scaleX;
			mesh->viewPosY *= *scaleY;
			mesh->viewPosZ *= *scaleZ;
			inverseScale = 1.0f / *scaleX;
			mesh->viewToModelOrient[0] =
				orientation[0] * inverseScale;
			mesh->viewToModelOrient[1] =
				orientation[1] * inverseScale;
			mesh->viewToModelOrient[2] =
				orientation[2] * inverseScale;
			inverseScale = 1.0f / *scaleY;
			mesh->viewToModelOrient[3] =
				orientation[3] * inverseScale;
			mesh->viewToModelOrient[4] =
				orientation[4] * inverseScale;
			mesh->viewToModelOrient[5] =
				orientation[5] * inverseScale;
			inverseScale = 1.0f / *scaleZ;
			mesh->viewToModelOrient[6] =
				orientation[6] * inverseScale;
			mesh->viewToModelOrient[7] =
				orientation[7] * inverseScale;
			mesh->viewToModelOrient[8] =
				orientation[8] * inverseScale;
			break;
		}
		case OPT_VERTNORMALS:
			g_curVertNormals = nodePayload;
			mesh->pVertNormals = nodePayload;
			break;
		default:
			break;
		}
	}

	if (node->childCount != 0) {
		childMesh = *mesh;
		g_curMeshVertices = NULL;
		g_curMeshTexCoords = NULL;
		g_curVertNormals = NULL;
		g_modelNodeWalkUnusedScratch2 = NULL;
		g_curMeshMaterials = NULL;
		g_curVertexCount = 0;
		childIndex = 0;
		if (node->childCount > 0) {
			do {
				if (RenderScene_TestSegmentAgainstModelNode(
					    model, node->pChildren[childIndex],
					    &childMesh, segmentStart,
					    segmentEnd)) {
					return 1;
				}
				++childIndex;
			} while (childIndex < node->childCount);
		}
	}
	return 0;
}

/* Returns 1 when the segment crosses one of the mesh's faces, else 0. Skips a
 * face whose corners all lie at or beyond both ends on one side on x, y or z.
 * Then needs the start 40 or more from the face's plane, measured along its
 * normal, and the end strictly on the other side. Puts the crossing at start +
 * (end - start) * (-ds / de), ds and de being the ends' distances from the
 * plane; drops one axis, picked by comparing the normal's components as signed
 * values (z when x and y are both under z, else y when x is under y and y is
 * over z, else x); and counts the point inside when the cross products with all
 * the edges have the same sign, 0 counting as positive. A face whose
 * vertexIdx[3] is -1 is a triangle. */
// FUNCTION: XVT 0x473AD0
int RenderScene_TestSegmentAgainstMeshFaces(
	const struct SceneMesh *mesh, const struct OptVector *segmentStart,
	const struct OptVector *segmentEnd)
{
	/* A face with vertexIdx[3] == -1 is a triangle, so its scaled base index is -3. */
	const struct FaceRecord *faces = mesh->pFaceGeom;
	const struct OptVector *normals = mesh->pFaceNormals;
	const float *coordinates = &mesh->pModelVerts[0].x;
	struct OptVector start;
	struct OptVector end;
	int faceIndex;

	start.x = segmentStart->x;
	start.y = segmentStart->y;
	start.z = segmentStart->z;
	end.x = segmentEnd->x;
	end.y = segmentEnd->y;
	end.z = segmentEnd->z;

	for (faceIndex = 0; faceIndex < mesh->faceCount; ++faceIndex, ++faces) {
		int base0 = faces->vertexIdx[0] * 3;
		int base1 = faces->vertexIdx[1] * 3;
		int base2 = faces->vertexIdx[2] * 3;
		int base3 = faces->vertexIdx[3] * 3;
		const struct OptVector *normal = normals++;
		float distanceStart;
		float distanceEnd;
		int vIndex0;
		int vIndex1;
		int vIndex2;
		int vIndex3;
		float hitU;
		float hitV;
		float cross0;
		float cross1;
		float cross2;
		float cross3;

		if (coordinates[base0] <= start.x &&
		    coordinates[base0] <= end.x) {
			if (coordinates[base1] <= start.x &&
			    coordinates[base1] <= end.x &&
			    coordinates[base2] <= start.x &&
			    coordinates[base2] <= end.x &&
			    (base3 == -3 || (coordinates[base3] <= start.x &&
					     coordinates[base3] <= end.x))) {
				continue;
			}
		} else if (coordinates[base0] >= start.x &&
			   coordinates[base0] >= end.x &&
			   coordinates[base1] >= start.x &&
			   coordinates[base1] >= end.x &&
			   coordinates[base2] >= start.x &&
			   coordinates[base2] >= end.x &&
			   (base3 == -3 || (coordinates[base3] >= start.x &&
					    coordinates[base3] >= end.x))) {
			continue;
		}
		++base0;
		++base1;
		++base2;
		++base3;
		if (coordinates[base0] <= start.y &&
		    coordinates[base0] <= end.y) {
			if (coordinates[base1] <= start.y &&
			    coordinates[base1] <= end.y &&
			    coordinates[base2] <= start.y &&
			    coordinates[base2] <= end.y &&
			    (base3 == -2 || (coordinates[base3] <= start.y &&
					     coordinates[base3] <= end.y))) {
				continue;
			}
		} else if (coordinates[base0] >= start.y &&
			   coordinates[base0] >= end.y &&
			   coordinates[base1] >= start.y &&
			   coordinates[base1] >= end.y &&
			   coordinates[base2] >= start.y &&
			   coordinates[base2] >= end.y &&
			   (base3 == -2 || (coordinates[base3] >= start.y &&
					    coordinates[base3] >= end.y))) {
			continue;
		}
		++base0;
		++base1;
		++base2;
		++base3;
		if (coordinates[base0] <= start.z &&
		    coordinates[base0] <= end.z) {
			if (coordinates[base1] <= start.z &&
			    coordinates[base1] <= end.z &&
			    coordinates[base2] <= start.z &&
			    coordinates[base2] <= end.z &&
			    (base3 == -1 || (coordinates[base3] <= start.z &&
					     coordinates[base3] <= end.z))) {
				continue;
			}
		} else if (coordinates[base0] >= start.z &&
			   coordinates[base0] >= end.z &&
			   coordinates[base1] >= start.z &&
			   coordinates[base1] >= end.z &&
			   coordinates[base2] >= start.z &&
			   coordinates[base2] >= end.z &&
			   (base3 == -1 || (coordinates[base3] >= start.z &&
					    coordinates[base3] >= end.z))) {
			continue;
		}

		distanceStart = (start.x - coordinates[base0 - 2]) * normal->x +
				normal->z * (start.z - coordinates[base0]) +
				normal->y * (start.y - coordinates[base0 - 1]);
		distanceEnd = (end.x - coordinates[base0 - 2]) * normal->x +
			      normal->z * (end.z - coordinates[base0]) +
			      normal->y * (end.y - coordinates[base0 - 1]);
		if (distanceStart >= 0.0f) {
			if (distanceStart < 40.0f || distanceEnd >= 0.0f) {
				continue;
			}
		} else if (distanceStart > -40.0f || distanceEnd <= 0.0f) {
			continue;
		}

		/* distanceStart becomes the factor used below to place the hit point between start and end. */
		distanceStart = (-distanceStart) / distanceEnd;

		if (normal->x < normal->z && normal->y < normal->z) {
			hitU = (end.x - start.x) * distanceStart + start.x;
			hitV = (end.y - start.y) * distanceStart + start.y;
			base0 -= 2;
			base1 -= 2;
			base2 -= 2;
			base3 -= 2;
			vIndex0 = base0 + 1;
			vIndex1 = base1 + 1;
			vIndex2 = base2 + 1;
			vIndex3 = base3 + 1;
		} else if (normal->x < normal->y && normal->y > normal->z) {
			hitU = (end.x - start.x) * distanceStart + start.x;
			hitV = (end.z - start.z) * distanceStart + start.z;
			base0 -= 2;
			base1 -= 2;
			base2 -= 2;
			base3 -= 2;
			vIndex0 = base0 + 2;
			vIndex1 = base1 + 2;
			vIndex2 = base2 + 2;
			vIndex3 = base3 + 2;
		} else {
			hitU = (end.y - start.y) * distanceStart + start.y;
			hitV = (end.z - start.z) * distanceStart + start.z;
			base0 -= 1;
			base1 -= 1;
			base2 -= 1;
			base3 -= 1;
			vIndex0 = base0 + 1;
			vIndex1 = base1 + 1;
			vIndex2 = base2 + 1;
			vIndex3 = base3 + 1;
		}

		cross0 = (hitU - coordinates[base0]) *
				 (coordinates[vIndex1] - coordinates[vIndex0]) -
			 (coordinates[base1] - coordinates[base0]) *
				 (hitV - coordinates[vIndex0]);
		cross1 = (hitU - coordinates[base1]) *
				 (coordinates[vIndex2] - coordinates[vIndex1]) -
			 (coordinates[base2] - coordinates[base1]) *
				 (hitV - coordinates[vIndex1]);
		if (cross0 < 0.0f) {
			if (cross1 >= 0.0f) {
				continue;
			}
		} else if (cross1 < 0.0f) {
			continue;
		}
		if (base3 < 0) {
			cross2 = (hitU - coordinates[base2]) *
					 (coordinates[vIndex0] -
					  coordinates[vIndex2]) -
				 (coordinates[base0] - coordinates[base2]) *
					 (hitV - coordinates[vIndex2]);
			if (cross0 < 0.0f) {
				if (cross2 >= 0.0f) {
					continue;
				}
			} else if (cross2 < 0.0f) {
				continue;
			}
		} else {
			cross2 = (hitU - coordinates[base2]) *
					 (coordinates[vIndex3] -
					  coordinates[vIndex2]) -
				 (coordinates[base3] - coordinates[base2]) *
					 (hitV - coordinates[vIndex2]);
			if (cross0 < 0.0f) {
				if (cross2 >= 0.0f) {
					continue;
				}
			} else if (cross2 < 0.0f) {
				continue;
			}
			cross3 = (hitU - coordinates[base3]) *
					 (coordinates[vIndex0] -
					  coordinates[vIndex3]) -
				 (coordinates[base0] - coordinates[base3]) *
					 (hitV - coordinates[vIndex3]);
			if (cross0 < 0.0f) {
				if (cross3 >= 0.0f) {
					continue;
				}
			} else if (cross3 < 0.0f) {
				continue;
			}
		}
		return 1;
	}
	return 0;
}

/* Allocates the scene's memory handles, calling
 * FeDiskIo_FatalError(FILE_ERROR_STR_NOT_ENOUGH_MEMORY) when one fails: 20000
 * spans, 20000 span pointers, 5000 faces, twice g_vertexRemapCapacity projected
 * vertices, twice g_sceneEdgeFlagsCapacity edges, g_vertexRemapCapacity remap
 * entries, g_sceneEdgeFlagsCapacity edge flags, 768 edge pointers, 768 row
 * heads, 0x25800 bytes of light samples and 500 queued meshes. Sets those
 * capacities and the sw3d light-sample block, 16 pixels. In the original build
 * it then makes 0x80000 bytes from 0x217 bytes past its own start readable,
 * writable and executable; the modern build's Memory_SetRegionExecuteReadWrite
 * does nothing. */
// FUNCTION: XVT 0x485CD0
void RenderScene_AllocateBuffers(void)
{
	void *codeAddress[1];
	void *codeAddressValue;
	int edgeMax;
#ifndef XVT_MODERN
	int savedEdgeMax;
#endif

	g_sceneSpanDataCapacity = 20000;
	g_sceneSpanDataHandle = Memory_AllocHandle(
		sizeof(struct SceneSpan) * g_sceneSpanDataCapacity, 0);
	if (g_sceneSpanDataHandle == 0) {
		FeDiskIo_FatalError(FILE_ERROR_STR_NOT_ENOUGH_MEMORY);
	}

	g_sceneSpanPtrCapacity = 20000;
	g_sceneSpanPtrListHandle = Memory_AllocHandle(
		sizeof(struct SceneSpan *) * g_sceneSpanPtrCapacity, 0);
	if (g_sceneSpanPtrListHandle == 0) {
		FeDiskIo_FatalError(FILE_ERROR_STR_NOT_ENOUGH_MEMORY);
	}

	g_sceneFaceMax = 5000;
	g_visFaceListHandle = Memory_AllocHandle(
		sizeof(struct SceneFace) * g_sceneFaceMax, 0);
	if (g_visFaceListHandle == 0) {
		FeDiskIo_FatalError(FILE_ERROR_STR_NOT_ENOUGH_MEMORY);
	}

	g_projVertMax = 2 * g_vertexRemapCapacity;
	g_projVertListHandle = Memory_AllocHandle(
		sizeof(struct ProjVertex) * g_projVertMax, 0);
	if (g_projVertListHandle == 0) {
		FeDiskIo_FatalError(FILE_ERROR_STR_NOT_ENOUGH_MEMORY);
	}

	edgeMax = 2 * g_sceneEdgeFlagsCapacity;
#ifdef XVT_MODERN
	g_sceneEdgeMax = edgeMax;
	g_sceneEdgeListHandle = Memory_AllocHandle(
		sizeof(*g_sceneEdgeList) * g_sceneEdgeMax, 0);
#else
	savedEdgeMax = edgeMax;
	g_sceneEdgeMax = edgeMax;
	/* From here edgeMax holds the list's size in bytes, 28 per edge, not a count. */
	edgeMax <<= 3;
	edgeMax -= savedEdgeMax;
	edgeMax <<= 2;
	g_sceneEdgeListHandle = Memory_AllocHandle(edgeMax, 0);
#endif
	if (g_sceneEdgeListHandle == 0) {
		FeDiskIo_FatalError(FILE_ERROR_STR_NOT_ENOUGH_MEMORY);
	}

	g_vertexRemapHandle = Memory_AllocHandle(
		sizeof(*g_vertexRemap) * g_vertexRemapCapacity, 0);
	if (g_vertexRemapHandle == 0) {
		FeDiskIo_FatalError(FILE_ERROR_STR_NOT_ENOUGH_MEMORY);
	}

	g_sceneEdgeFlagsHandle = Memory_AllocHandle(
		sizeof(*g_sceneEdgeFlags) * g_sceneEdgeFlagsCapacity, 0);
	if (g_sceneEdgeFlagsHandle == 0) {
		FeDiskIo_FatalError(FILE_ERROR_STR_NOT_ENOUGH_MEMORY);
	}

	g_sceneSclEdgeListHandle =
		Memory_AllocHandle(sizeof(*g_sceneSclEdgeList) * 768, 0);
	if (g_sceneSclEdgeListHandle == 0) {
		FeDiskIo_FatalError(FILE_ERROR_STR_NOT_ENOUGH_MEMORY);
	}

	g_scanlineSpanHeadsHandle =
		Memory_AllocHandle(sizeof(*g_scanlineSpanHeads) * 768, 0);
	if (g_scanlineSpanHeadsHandle == 0) {
		FeDiskIo_FatalError(FILE_ERROR_STR_NOT_ENOUGH_MEMORY);
	}

	g_sw3dLightSampleBlockSize = 16;
	g_sw3dLightSampleInvBlockSize = g_sw3dSpanLengthReciprocal[16];
	g_sw3dLightSampleBlockShift = 4;
	g_sw3dLightSampleBlockMask = 15;
	g_sw3dLightSampleBlockSizeFloat = 16.0f;
	g_sceneLightSampleDataHandle = Memory_AllocHandle(0x25800, 0);
	if (g_sceneLightSampleDataHandle == 0) {
		FeDiskIo_FatalError(FILE_ERROR_STR_NOT_ENOUGH_MEMORY);
	}

	g_meshQueueMax = 500;
	g_meshQueueHandle = Memory_AllocHandle(
		sizeof(struct SceneMesh) * g_meshQueueMax, 0);
	if (g_meshQueueHandle == 0) {
		FeDiskIo_FatalError(FILE_ERROR_STR_NOT_ENOUGH_MEMORY);
	}

	/* The original inline assembly captured the address of the following code label. */
	codeAddressValue =
		(uint8_t *)(void *)RenderScene_AllocateBuffers + 0x217;
	memcpy(codeAddress, &codeAddressValue, sizeof(codeAddressValue));
	Memory_SetRegionExecuteReadWrite(codeAddress[0], 0x80000);
}

/* Starts a pass of scene drawing. The original build saves the x87 control word
 * in g_sw3dInitializeSceneSavedFpuControl and sets extended precision; the
 * modern build clears the 0x300 bits of g_sw3dFpuControlWordScratch. Gives
 * g_sw3dCockpitMaskSentinelFace a scaledInverseDepth and a 1-over-depth plane
 * of 1e32, and locks every scene buffer. With resetSceneState nonzero it
 * empties the face list, span pointers, light-sample rows and mesh queue, and
 * rebuilds g_scanlineSpanHeads from the viewport span mask: one span with the
 * sentinel face for each run the mask marks. With 0 it starts the new pass at
 * g_visFaceCount. Then sets g_lightSampleSlotStride, adds g_flightVpHeight to
 * g_sw3dLightSampleCacheSceneStampBase, sets g_invProjScale, and in the
 * hardware path calls RenderScene_InitHardwareFrame. */
// FUNCTION: XVT 0x485F00
void RenderScene_Initialize(int resetSceneState)
{
	uint8_t *mask;
	int8_t runType;
	struct SceneSpan *previousSpan;
	unsigned int scanX;
	unsigned int scanY;
	int scanline;

#ifndef XVT_MODERN
	g_sw3dInitializeSceneSavedFpuControl = _control87(0, 0);
	_control87(0, 0x30000);
#else
	g_sw3dFpuControlWordScratch &= 0xFFFFFCFF;
#endif
	g_sw3dCockpitMaskSentinelFace.maxScaledInverseDepth = 1.0e32f;
	g_sw3dCockpitMaskSentinelFace.minScaledInverseDepth = 1.0e32f;
	g_sw3dCockpitMaskSentinelFace.gradients[8] = 1.0e32f;
	g_sw3dCockpitMaskSentinelFace.gradients[6] = 0.0f;
	g_sw3dCockpitMaskSentinelFace.gradients[7] = 0.0f;
	g_sceneSpanDataBase = Memory_GetHandleBlock(g_sceneSpanDataHandle);
	g_sceneSpanPtrList = Memory_GetHandleBlock(g_sceneSpanPtrListHandle);
	g_visFaceList = Memory_GetHandleBlock(g_visFaceListHandle);
	g_projVertList = Memory_GetHandleBlock(g_projVertListHandle);
	g_sceneEdgeList = Memory_GetHandleBlock(g_sceneEdgeListHandle);
	g_vertexRemap = Memory_GetHandleBlock(g_vertexRemapHandle);
	g_sceneEdgeFlags = Memory_GetHandleBlock(g_sceneEdgeFlagsHandle);
	g_sceneSclEdgeList = Memory_GetHandleBlock(g_sceneSclEdgeListHandle);
	g_scanlineSpanHeads = Memory_GetHandleBlock(g_scanlineSpanHeadsHandle);
	g_sceneLightSampleData =
		Memory_GetHandleBlock(g_sceneLightSampleDataHandle);
	g_meshQueue = Memory_GetHandleBlock(g_meshQueueHandle);
	if (resetSceneState != 0) {
		g_visFacePassStart = 0;
		g_sceneSpanPtrAvail = g_sceneSpanPtrCapacity;
		g_pSceneSpanDataCur = g_sceneSpanDataBase;
		g_visFaceCount = 0;
		g_lightSampleSlotIndex = 0;
		g_meshQueueIndex = 0;
		mask = &g_flightAuxBuffer[g_viewportSpanMaskOffset];
		scanY = 0;
		g_pSceneSpanDataEnd =
			&g_sceneSpanDataBase[g_sceneSpanDataCapacity - 1];
		if (g_flightVpHeight != 0) {
			scanline = 0;
			do {
				scanX = 0;
				g_scanlineSpanHeads[scanline] = NULL;
				runType = (int8_t)*mask++;
				previousSpan = g_scanlineSpanHeads[scanline];
				if (g_flightVpWidth != 0) {
					do {
						int runLength;

						runLength = *mask++;
						if (runLength == 0) {
							runLength = *mask++;
							if (runLength == 0) {
								runLength =
									*mask++ +
									256;
							}
							runLength += 255;
						}
						if (runType < 0) {
							if (previousSpan !=
							    NULL) {
								previousSpan
									->next =
									g_pSceneSpanDataCur;
							} else {
								g_scanlineSpanHeads
									[scanline] =
										g_pSceneSpanDataCur;
							}
							previousSpan =
								g_pSceneSpanDataCur++;
							previousSpan->xStart =
								scanX;
							previousSpan->xEnd =
								scanX +
								runLength;
							previousSpan->face =
								&g_sw3dCockpitMaskSentinelFace;
							previousSpan->next =
								NULL;
						}
						runType = -runType;
						scanX += runLength;
					} while (scanX < g_flightVpWidth);
				}
				++scanline;
				++scanY;
			} while (scanY < g_flightVpHeight);
		}
	} else {
		g_visFacePassStart = g_visFaceCount;
	}
	g_lightSampleSlotStride = ((unsigned int)g_flightVpWidth +
				   g_sw3dLightSampleBlockSize - 1) /
				  g_sw3dLightSampleBlockSize;
	g_sw3dLightSampleCacheSceneStampBase += g_flightVpHeight;
	g_invProjScale = 1.0f / (float)(unsigned int)g_projScaleInt;
	if (g_useHardware3D != 0) {
		RenderScene_InitHardwareFrame();
	}
}

/* Unlocks the eleven scene buffers and sets their pointers to NULL. Returns
 * 0. */
// FUNCTION: XVT 0x486200
int RenderScene_UnlockBuffers(void)
{
	Memory_HandleBlockDoneStub(g_sceneSpanDataHandle);
	Memory_HandleBlockDoneStub(g_sceneSpanPtrListHandle);
	Memory_HandleBlockDoneStub(g_visFaceListHandle);
	Memory_HandleBlockDoneStub(g_projVertListHandle);
	Memory_HandleBlockDoneStub(g_sceneEdgeListHandle);
	Memory_HandleBlockDoneStub(g_vertexRemapHandle);
	Memory_HandleBlockDoneStub(g_sceneEdgeFlagsHandle);
	Memory_HandleBlockDoneStub(g_sceneSclEdgeListHandle);
	Memory_HandleBlockDoneStub(g_scanlineSpanHeadsHandle);
	Memory_HandleBlockDoneStub(g_sceneLightSampleDataHandle);
	Memory_HandleBlockDoneStub(g_meshQueueHandle);
	g_sceneSpanDataBase = NULL;
	g_sceneSpanPtrList = NULL;
	g_visFaceList = NULL;
	g_projVertList = NULL;
	g_sceneEdgeList = NULL;
	g_vertexRemap = NULL;
	g_sceneEdgeFlags = NULL;
	g_sceneSclEdgeList = NULL;
	g_scanlineSpanHeads = NULL;
	g_sceneLightSampleData = NULL;
	g_meshQueue = NULL;
	return 0;
}

/* Frees each of the eleven scene memory handles that is nonzero and sets all
 * eleven to 0. */
// FUNCTION: XVT 0x4862E0
void RenderScene_FreeBuffers(void)
{
	if (g_sceneSpanDataHandle != 0) {
		Memory_FreeHandle(
			RenderScene_GetMemoryHandle(&g_sceneSpanDataHandle));
	}
	g_sceneSpanDataHandle = 0;
	if (g_sceneSpanPtrListHandle != 0) {
		Memory_FreeHandle(
			RenderScene_GetMemoryHandle(&g_sceneSpanPtrListHandle));
	}
	g_sceneSpanPtrListHandle = 0;
	if (g_visFaceListHandle != 0) {
		Memory_FreeHandle(
			RenderScene_GetMemoryHandle(&g_visFaceListHandle));
	}
	g_visFaceListHandle = 0;
	if (g_projVertListHandle != 0) {
		Memory_FreeHandle(
			RenderScene_GetMemoryHandle(&g_projVertListHandle));
	}
	g_projVertListHandle = 0;
	if (g_sceneEdgeListHandle != 0) {
		Memory_FreeHandle(
			RenderScene_GetMemoryHandle(&g_sceneEdgeListHandle));
	}
	g_sceneEdgeListHandle = 0;
	if (g_vertexRemapHandle != 0) {
		Memory_FreeHandle(
			RenderScene_GetMemoryHandle(&g_vertexRemapHandle));
	}
	g_vertexRemapHandle = 0;
	if (g_sceneEdgeFlagsHandle != 0) {
		Memory_FreeHandle(
			RenderScene_GetMemoryHandle(&g_sceneEdgeFlagsHandle));
	}
	g_sceneEdgeFlagsHandle = 0;
	if (g_sceneSclEdgeListHandle != 0) {
		Memory_FreeHandle(
			RenderScene_GetMemoryHandle(&g_sceneSclEdgeListHandle));
	}
	g_sceneSclEdgeListHandle = 0;
	if (g_scanlineSpanHeadsHandle != 0) {
		Memory_FreeHandle(RenderScene_GetMemoryHandle(
			&g_scanlineSpanHeadsHandle));
	}
	g_scanlineSpanHeadsHandle = 0;
	if (g_sceneLightSampleDataHandle != 0) {
		Memory_FreeHandle(RenderScene_GetMemoryHandle(
			&g_sceneLightSampleDataHandle));
	}
	g_sceneLightSampleDataHandle = 0;
	if (g_meshQueueHandle != 0) {
		Memory_FreeHandle(
			RenderScene_GetMemoryHandle(&g_meshQueueHandle));
	}
	g_meshQueueHandle = 0;
}
