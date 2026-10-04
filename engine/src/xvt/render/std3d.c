#include "xvt/render/std3d.h"
#ifdef XVT_MODERN
#include "xvt_runtime/log/log.h"
#endif

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "xvt/render/renderer.h"
#include "xvt/util/debug_console.h"

struct std3d_unknown;

/* drift-ok: camelcase -- COM's IUnknown method table */
struct std3d_unknown_vtbl {
	void *QueryInterface;
	void *AddRef;
	uint32_t(AERON_DXAPI *Release)(struct std3d_unknown *self);
};

/* drift-ok: camelcase -- a COM object: its table pointer */
struct std3d_unknown {
	const struct std3d_unknown_vtbl *lpVtbl;
};

/* Render options in the bits of std3d_render_state_flags, plus 0x1
 * perspective-correct texturing, 0x2 dither, 0x4 specular, 0x8 antialias, and
 * 0x10 and 0x20 subpixel, as std3d_set_initial_render_state reads them.
 * std3d_startup sets 0x19B3: perspective, dither, both subpixel bits, linear
 * filtering both ways, z compare and z write. The only other writer,
 * std3d_set_cap_flags, has no caller. */
// GLOBAL: XVT 0x528C50
unsigned int g_std3d_render_option_flags;
/* Render-state bits last written into the execute buffer; std3d_set_render_state
 * writes only the states whose bits differ from them, then stores the new bits.
 * std3d_set_initial_render_state sets it to g_std3d_render_option_flags. */
// GLOBAL: XVT 0x528C54
static std3d_render_state_flags g_d3d_state_flags = 0;
/* Devices in g_std3d_devices, 0 to 4: std3d_startup sets 0 and
 * std3d_enum_devices_callback adds each. */
// GLOBAL: XVT 0x528C58
unsigned int g_std3d_num_devices = 0;
/* Index in g_std3d_devices of the device std3d_create_device opened. */
// GLOBAL: XVT 0xA90A68
unsigned int g_std3d_cur_device_idx = 0;
/* The device std3d_create_device opened, in g_std3d_devices; NULL before. */
// GLOBAL: XVT 0x528C5C
struct std3d_device *g_p_std3d_cur_device = 0;
/* Formats in g_std3d_texture_formats, 0 to 8: std3d_create_device sets 0 and
 * std3d_enum_texture_formats adds each it keeps. */
// GLOBAL: XVT 0x528C60
static int g_std3d_num_texture_formats = 0;
/* The texture format closest to the render target's 16-bit RGB, picked by
 * std3d_create_device. */
// GLOBAL: XVT 0x528C64
struct std3d_tex_fmt *g_p_fmt_opaque_texture;
/* Index of g_p_fmt_opaque_texture in g_std3d_texture_formats; only debug prints read
 * it. */
// GLOBAL: XVT 0xA90B58
static int g_fmt_idx_opaque_texture = 0;
/* The texture format closest to RGBA with 5, 5, 5 and 1 bits, picked by
 * std3d_create_device when the device has alpha textures; NULL otherwise. */
// GLOBAL: XVT 0x528C68
static struct std3d_tex_fmt *g_p_fmt_rgba1555 = 0;
/* Index of g_p_fmt_rgba1555 in g_std3d_texture_formats; only debug prints read
 * it. */
// GLOBAL: XVT 0xA91B88
static int g_fmt_idx_rgba1555 = 0;
/* The texture format closest to RGBA with 4 bits each, picked by
 * std3d_create_device when the device has alpha textures but no alpha blending;
 * NULL otherwise. */
// GLOBAL: XVT 0x528C6C
static struct std3d_tex_fmt *g_p_fmt_rgba4444 = 0;
/* Index of g_p_fmt_rgba4444 in g_std3d_texture_formats; only debug prints read
 * it. */
// GLOBAL: XVT 0xA90A64
static int g_fmt_idx_rgba4444 = 0;
/* 1 after std3d_startup succeeds, 0 after std3d_shutdown; nothing reads it. */
// GLOBAL: XVT 0x528CB8
static int g_std3d_startup_done = 0;
/* 1 when the open device draws with a z-buffer: std3d_create_device sets it when
 * asked to, when the device has one, and when g_std3d_render_option_flags has a
 * 0x1800 bit. Starts at 1, and std3d_startup sets 1. */
// GLOBAL: XVT 0x528CB0
static int g_std3dz_buffer_enabled = 1;
/* The DirectDraw object, which std3d_startup takes from
 * renderer_get_direct_draw. */
// GLOBAL: XVT 0x528CB4
static IDirectDraw *g_std3d_direct_draw = 0;
/* The texture formats the open device offers, filled by
 * std3d_enum_texture_formats, which skips 4-bit palette formats. */
// GLOBAL: XVT 0xA90C00
static struct std3d_tex_fmt g_std3d_texture_formats[8] = {0};
#ifndef XVT_MODERN
/* Message std3d_shutdown passes to debug_printf in the 1997 build; the modern
 * build writes d3d.shutdown instead. */
// GLOBAL: XVT 0x529168
static const char g_std3d_shutdown_succeeded_message[] =
	"Shutdown Succeeded.\n";
#endif
/* What std3d_lookup_error_string returns for a code the table lacks. */
// GLOBAL: XVT 0x5293B0
static const char g_std3d_unknown_error_message[] = "Unknown Error";
#ifndef XVT_MODERN
/* The next three are formats the 1997 build passes to debug_printf; the modern
 * build writes d3d.call_failed with the step instead. This one when a scene
 * does not begin. */
// GLOBAL: XVT 0x529470
static const char g_std3d_begin_scene_error_format[] =
	"Error %s beginning scene.\n";
/* When a scene does not end. */
// GLOBAL: XVT 0x52948C
static const char g_std3d_end_scene_error_format[] = "Error %s ending scene.\n";
/* When the execute buffer does not lock. */
// GLOBAL: XVT 0x5294A4
static const char g_std3d_lock_execute_buffer_error_format[] =
	"Error %s locking D3D Execute buffer.\n";
#endif
/* Rows of a result code and its name, for the Direct3D and then the DirectDraw
 * codes, that std3d_lookup_error_string searches. */
// GLOBAL: XVT 0x528CC0
static const struct std3d_error_string_entry g_std3d_error_string_table[121] = {
	{0, "D3D_OK"},
	{-2005531972, "D3DERR_BADMAJORVERSION"},
	{-2005531971, "D3DERR_BADMINORVERSION"},
	{-2005531961, "D3DERR_EXECUTE_DESTROY_FAILED"},
	{-2005531960, "D3DERR_EXECUTE_LOCK_FAILED"},
	{-2005531959, "D3DERR_EXECUTE_UNLOCK_FAILED"},
	{-2005531958, "D3DERR_EXECUTE_LOCKED"},
	{-2005531957, "D3DERR_EXECUTE_NOT_LOCKED"},
	{-2005531955, "D3DERR_EXECUTE_CLIPPED_FAILED"},
	{-2005531951, "D3DERR_TEXTURE_CREATE_FAILED"},
	{-2005531950, "D3DERR_TEXTURE_DESTROY_FAILED"},
	{-2005531949, "D3DERR_TEXTURE_LOCK_FAILED"},
	{-2005531948, "D3DERR_TEXTURE_UNLOCK_FAILED"},
	{-2005531947, "D3DERR_TEXTURE_LOAD_FAILED"},
	{-2005531946, "D3DERR_TEXTURE_SWAP_FAILED"},
	{-2005531945, "D3DERR_TEXTURE_LOCKED"},
	{-2005531944, "D3DERR_TEXTURE_NOT_LOCKED"},
	{-2005531943, "D3DERR_TEXTURE_GETSURF_FAILED"},
	{-2005531941, "D3DERR_MATRIX_DESTROY_FAILED"},
	{-2005531940, "D3DERR_MATRIX_SETDATA_FAILED"},
	{-2005531939, "D3DERR_MATRIX_GETDATA_FAILED"},
	{-2005531938, "D3DERR_SETVIEWPORTDATA_FAILED"},
	{-2005531931, "D3DERR_MATERIAL_DESTROY_FAILED"},
	{-2005531930, "D3DERR_MATERIAL_SETDATA_FAILED"},
	{-2005531929, "D3DERR_MATERIAL_GETDATA_FAILED"},
	{-2005531911, "D3DERR_SCENE_NOT_IN_SCENE"},
	{-2005531910, "D3DERR_SCENE_BEGIN_FAILED"},
	{-2005531909, "D3DERR_SCENE_END_FAILED"},
	{0, "DD_OK"},
	{-2005532667, "DDERR_ALREADYINITIALIZED"},
	{-2005532662, "DDERR_CANNOTATTACHSURFACE"},
	{-2005532652, "DDERR_CANNOTDETACHSURFACE"},
	{-2005532632, "DDERR_CURRENTLYNOTAVAIL"},
	{-2005532617, "DDERR_EXCEPTION"},
	{-2147467259, "DDERR_GENERIC"},
	{-2005532582, "DDERR_HEIGHTALIGN"},
	{-2005532577, "DDERR_INCOMPATIBLEPRIMARY"},
	{-2005532572, "DDERR_INVALIDCAPS"},
	{-2005532562, "DDERR_INVALIDCLIPLIST"},
	{-2005532552, "DDERR_INVALIDMODE"},
	{-2005532542, "DDERR_INVALIDOBJECT"},
	{-2147024809, "DDERR_INVALIDPARAMS"},
	{-2005532527, "DDERR_INVALIDPIXELFORMAT"},
	{-2005532522, "DDERR_INVALIDRECT"},
	{-2005532512, "DDERR_LOCKEDSURFACES"},
	{-2005532502, "DDERR_NO3D"},
	{-2005532492, "DDERR_NOALPHAHW"},
	{-2005532467, "DDERR_NOCLIPLIST"},
	{-2005532462, "DDERR_NOCOLORCONVHW"},
	{-2005532460, "DDERR_NOCOOPERATIVELEVELSET"},
	{-2005532457, "DDERR_NOCOLORKEY"},
	{-2005532452, "DDERR_NOCOLORKEYHW"},
	{-2005532450, "DDERR_NODIRECTDRAWSUPPORT"},
	{-2005532447, "DDERR_NOEXCLUSIVEMODE"},
	{-2005532442, "DDERR_NOFLIPHW"},
	{-2005532432, "DDERR_NOGDI"},
	{-2005532422, "DDERR_NOMIRRORHW"},
	{-2005532417, "DDERR_NOTFOUND"},
	{-2005532412, "DDERR_NOOVERLAYHW"},
	{-2005532392, "DDERR_NORASTEROPHW"},
	{-2005532382, "DDERR_NOROTATIONHW"},
	{-2005532362, "DDERR_NOSTRETCHHW"},
	{-2005532356, "DDERR_NOT4BITCOLOR"},
	{-2005532355, "DDERR_NOT4BITCOLORINDEX"},
	{-2005532352, "DDERR_NOT8BITCOLOR"},
	{-2005532342, "DDERR_NOTEXTUREHW"},
	{-2005532337, "DDERR_NOVSYNCHW"},
	{-2005532332, "DDERR_NOZBUFFERHW"},
	{-2005532322, "DDERR_NOZOVERLAYHW"},
	{-2005532312, "DDERR_OUTOFCAPS"},
	{-2147024882, "DDERR_OUTOFMEMORY"},
	{-2005532292, "DDERR_OUTOFVIDEOMEMORY"},
	{-2005532290, "DDERR_OVERLAYCANTCLIP"},
	{-2005532288, "DDERR_OVERLAYCOLORKEYONLYONEACTIVE"},
	{-2005532285, "DDERR_PALETTEBUSY"},
	{-2005532272, "DDERR_COLORKEYNOTSET"},
	{-2005532262, "DDERR_SURFACEALREADYATTACHED"},
	{-2005532252, "DDERR_SURFACEALREADYDEPENDENT"},
	{-2005532242, "DDERR_SURFACEBUSY"},
	{-2005532232, "DDERR_SURFACEISOBSCURED"},
	{-2005532222, "DDERR_SURFACELOST"},
	{-2005532212, "DDERR_SURFACENOTATTACHED"},
	{-2005532202, "DDERR_TOOBIGHEIGHT"},
	{-2005532192, "DDERR_TOOBIGSIZE"},
	{-2005532182, "DDERR_TOOBIGWIDTH"},
	{-2147467263, "DDERR_UNSUPPORTED"},
	{-2005532162, "DDERR_UNSUPPORTEDFORMAT"},
	{-2005532152, "DDERR_UNSUPPORTEDMASK"},
	{-2005532135, "DDERR_VERTICALBLANKINPROGRESS"},
	{-2005532132, "DDERR_WASSTILLDRAWING"},
	{-2005532112, "DDERR_XALIGN"},
	{-2005532111, "DDERR_INVALIDDIRECTDRAWGUID"},
	{-2005532110, "DDERR_DIRECTDRAWALREADYCREATED"},
	{-2005532109, "DDERR_NODIRECTDRAWHW"},
	{-2005532108, "DDERR_PRIMARYSURFACEALREADYEXISTS"},
	{-2005532107, "DDERR_NOEMULATION"},
	{-2005532106, "DDERR_REGIONTOOSMALL"},
	{-2005532105, "DDERR_CLIPPERISUSINGHWND"},
	{-2005532104, "DDERR_NOCLIPPERATTACHED"},
	{-2005532103, "DDERR_NOHWND"},
	{-2005532102, "DDERR_HWNDSUBCLASSED"},
	{-2005532101, "DDERR_HWNDALREADYSET"},
	{-2005532100, "DDERR_NOPALETTEATTACHED"},
	{-2005532099, "DDERR_NOPALETTEHW"},
	{-2005532098, "DDERR_BLTFASTCANTCLIP"},
	{-2005532097, "DDERR_NOBLTHW"},
	{-2005532096, "DDERR_NODDROPSHW"},
	{-2005532095, "DDERR_OVERLAYNOTVISIBLE"},
	{-2005532094, "DDERR_NOOVERLAYDEST"},
	{-2005532093, "DDERR_INVALIDPOSITION"},
	{-2005532092, "DDERR_NOTAOVERLAYSURFACE"},
	{-2005532091, "DDERR_EXCLUSIVEMODEALREADYSET"},
	{-2005532090, "DDERR_NOTFLIPPABLE"},
	{-2005532089, "DDERR_CANTDUPLICATE"},
	{-2005532088, "DDERR_NOTLOCKED"},
	{-2005532087, "DDERR_CANTCREATEDC"},
	{-2005532086, "DDERR_NODC"},
	{-2005532085, "DDERR_WRONGMODE"},
	{-2005532084, "DDERR_IMPLICITLYCREATED"},
	{-2005532083, "DDERR_NOTPALETTIZED"},
	{-2005532082, "DDERR_UNSUPPORTEDMODE"},
};
/* Fog table end that std3d_set_render_state writes when fog turns on. Only
 * std3d_set_fog_table_range_bits writes it, and nothing calls that, so it stays
 * 0. */
// GLOBAL: XVT 0x6644F0
static unsigned int g_std3d_fog_table_end_bits;
/* Blue of the fog color, 0 to 255, that std3d_set_render_state writes when fog
 * turns on. Only std3d_set_fog_color8 writes it, and nothing calls that, so it
 * stays 0. */
// GLOBAL: XVT 0x6644F4
static unsigned int g_std3d_fog_color_blue8;
/* Fog table start, written and read as g_std3d_fog_table_end_bits is; it stays
 * 0. */
// GLOBAL: XVT 0x664544
static unsigned int g_std3d_fog_table_start_bits;
/* Red of the fog color, written and read as g_std3d_fog_color_blue8 is; it stays
 * 0. */
// GLOBAL: XVT 0x664750
static unsigned int g_std3d_fog_color_red8;
/* 16-bit colors of the palette being uploaded in the opaque format:
 * std3d_copy_palette_to_scratch16 copies them in. std3d_add_to_texture_cache looks up
 * 8-bit texels through it, and takes entry 0 as the color key when the device
 * has no alpha textures. */
// GLOBAL: XVT 0x664770
uint16_t g_std3d_palette_scratch16[256];
/* The palette in the RGBA 4-bit format, for translucent textures. Only
 * std3d_set_palette_conversion_source fills it, and nothing calls that, so it
 * stays 0. */
// GLOBAL: XVT 0x6642F0
static uint16_t g_tex_conv_buf4444[256] = {0};
/* The palette in the RGBA 5, 5, 5 and 1 bit format, for color-keyed textures on
 * a device with alpha textures; std3d_convert_palette_to1555 fills it. */
// GLOBAL: XVT 0x664550
static uint16_t g_tex_conv_buf1555[256] = {0};
/* A copy of the last palette given to std3d_set_palette_conversion_source, which
 * nothing calls; nothing reads it. */
// GLOBAL: XVT 0x664980
static uint8_t g_std3d_palette_conversion_source_rgb[768] = {0};
/* Green of the fog color, written and read as g_std3d_fog_color_blue8 is; it stays
 * 0. */
// GLOBAL: XVT 0x664978
static unsigned int g_std3d_fog_color_green8;
/* The Direct3D interface std3d_startup gets from the DirectDraw object;
 * std3d_shutdown releases it, and the modern build then sets it to NULL. */
// GLOBAL: XVT 0x664970
static IDirect3D *g_lp_d3d = 0;
/* The Direct3D viewport std3d_create_viewport makes; std3d_close releases it and
 * sets it to NULL. */
// GLOBAL: XVT 0x664974
static IDirect3DViewport *g_d3d_viewport = 0;
/* Released by std3d_close when set, but nothing sets it, so it stays NULL. */
// GLOBAL: XVT 0x66497C
static struct std3d_unknown *g_d3d_viewport_material = 0;
/* The Direct3D device std3d_create_device gets from the render surface;
 * std3d_close releases it and sets it to NULL. */
// GLOBAL: XVT 0x664548
IDirect3DDevice *g_d3d_device;
/* The surface Direct3D draws into, set by std3d_set_render_surface;
 * renderer_init_d3d_device passes g_flight_back_buffer. */
// GLOBAL: XVT 0xA91B14
struct IDirectDrawSurface *g_std3d_render_surface;
/* The Direct3D devices std3d_enum_devices_callback found, up to 4. */
// GLOBAL: XVT 0xA91120
struct std3d_device g_std3d_devices[4] = {{0}};
/* Red of the full-viewport color overlay. Set by std3d_set_color_overlay_params,
 * which renderer_init_d3d_device calls with 0 and off; read by
 * std3d_draw_color_overlay, which nothing calls. */
// GLOBAL: XVT 0xA90B10
float g_std3d_color_overlay_red;
/* Green of the color overlay, set and read as g_std3d_color_overlay_red is. */
// GLOBAL: XVT 0xA90B14
float g_std3d_color_overlay_green;
/* Blue of the color overlay, set and read as g_std3d_color_overlay_red is. */
// GLOBAL: XVT 0xA90B18
float g_std3d_color_overlay_blue;
/* Nonzero when the color overlay is on; set and read as g_std3d_color_overlay_red
 * is. */
// GLOBAL: XVT 0xA90B1C
int g_std3d_color_overlay_enabled;
/* The z test of the open device: 16 (greater) when its compare caps have the
 * 0x10 bit, else 2 (less); std3d_create_device sets it when the z-buffer is
 * enabled, else it stays 0. With 2, render_scene_emit_flight_vertex writes 1 minus
 * its z value, so that a nearer point has the smaller value. */
// GLOBAL: XVT 0xA90B20
int g_std3dz_compare_cap = 0;
/* Two triangles over the viewport quad in g_std3d_quad_verts, alpha-blended with
 * mono off; std3d_build_viewport_quad sets them and std3d_draw_color_overlay draws
 * them. */
// GLOBAL: XVT 0xA90B30
static struct std3d_render_tri g_std3d_viewport_quad_triangles[2] = {{0}};
/* The viewport's corners, clockwise from the top left, set by
 * std3d_build_viewport_quad with every other field 0. */
// GLOBAL: XVT 0xA90B60
static D3DTLVERTEX g_std3d_quad_verts[4] = {{0}};
/* The viewport rectangle std3d_build_viewport_quad was given: 0, 0 and the render
 * target's size. std3d_clear_z_buffer clears this area. */
// GLOBAL: XVT 0xA90BF0
static struct std3d_viewport_rect g_std3d_quad_rect = {0};
/* The texture std3d_draw_color_overlay uploads and removes again each time it
 * draws without alpha blending. */
// GLOBAL: XVT 0xA90A70
static struct std3d_tex_cache_node g_std3d_color_overlay_tex_node = {0};
/* The z-buffer surface and its description, made by std3d_create_z_buffer. */
// GLOBAL: XVT 0xA91A34
static struct std3dz_buffer_surface_block g_std3dz_buffer_surface_block = {0};
/* A record of the z-buffer that std3d_create_z_buffer fills; only its debug print
 * reads it. */
// GLOBAL: XVT 0xA919D0
static struct std3dz_buffer_target g_std3dz_buffer_target = {0};
/* Only std3d_set_texture_size_caps writes it, and nothing calls that, so it stays
 * 0. */
// GLOBAL: XVT 0x528C70
int g_std3d_min_texture_width;
/* Only std3d_set_texture_size_caps writes it, and nothing calls that, so it stays
 * 0. */
// GLOBAL: XVT 0x528C74
int g_std3d_min_texture_height;
/* Only std3d_set_texture_size_caps writes it, and nothing calls that, so it stays
 * 0. */
// GLOBAL: XVT 0x528C78
int g_std3d_max_texture_width;
/* Only std3d_set_texture_size_caps writes it, and nothing calls that; nothing
 * reads it. */
// GLOBAL: XVT 0x528C7C
int g_std3d_max_texture_height;
/* Most vertices one execute buffer takes: the device's max_vertex_count, at most
 * 512, or 512 when it states none; set by std3d_create_device. std3d_add_vertices
 * refuses more. */
// GLOBAL: XVT 0x528C80
unsigned int g_std3d_exec_buf_max_verts = 0;
/* Counter each std3d_lock_execute_buffer raises; a texture used in the current
 * execute buffer carries it in cache_batch_tag. std3d_create_device and
 * std3d_flush_texture_cache set it to 1. */
// GLOBAL: XVT 0x528C84
unsigned int g_std3d_texture_batch_tag = 1;
/* Textures on the cache list. */
// GLOBAL: XVT 0x528C88
int g_tex_cache_count = 0;
/* Least recently used texture on the cache list; NULL when it is empty. */
// GLOBAL: XVT 0x528C8C
struct std3d_tex_cache_node *g_p_tex_cache_head = 0;
/* Most recently used texture on the cache list; NULL when it is empty. */
// GLOBAL: XVT 0x528C90
struct std3d_tex_cache_node *g_p_tex_cache_tail = 0;
/* A 32 by 32 buffer in the RGBA 4-bit format, made by std3d_create_device when
 * the device has alpha textures but no alpha blending; std3d_draw_color_overlay
 * fills it and std3d_close frees it. */
// GLOBAL: XVT 0x528C94
static struct std3dv_buffer *g_p_std3dv_buffer = 0;
/* The execute buffer every batch is written into; std3d_create_device makes it
 * and std3d_close releases it. */
// GLOBAL: XVT 0x528C98
static IDirect3DExecuteBuffer *g_d3d_execute_buffer = 0;
/* Size of the execute buffer in bytes: the device's maxBufferSize, or 0x10000
 * when it states none. */
// GLOBAL: XVT 0x528C9C
static unsigned int g_std3d_exec_buf_size = 0;
/* Vertices in the execute buffer since std3d_lock_execute_buffer set it to 0. */
// GLOBAL: XVT 0x528CA0
int g_d3d_buf_vert_count = 0;
/* Triangles added since the last lock; nothing reads it. */
// GLOBAL: XVT 0x528CA4
static unsigned int g_std3d_exec_buf_tri_count = 0;
/* The texture the execute buffer last selected. std3d_lock_execute_buffer sets it
 * to 1, which no texture is, so the first triangle group always writes a
 * texture state. */
// GLOBAL: XVT 0x528CA8
static struct std3d_tex_cache_node *g_d3d_cur_texture =
	(struct std3d_tex_cache_node *)1;
/* Points at g_std3dz_buffer_surface_block once std3d_create_z_buffer runs; NULL
 * before. */
// GLOBAL: XVT 0x528CAC
static struct std3dz_buffer_surface_block *g_p_std3dz_buffer_state = NULL;
/* 1 while a device is open: std3d_create_device sets it on success and
 * std3d_close clears it. */
// GLOBAL: XVT 0x528CBC
static int g_std3d_device_open = 0;
/* Start of the locked execute buffer. */
// GLOBAL: XVT 0x664754
static uint8_t *g_d3d_exec_buf_base = 0;
/* Description of the execute buffer: std3d_create_device sets its size, and each
 * lock fills in where it lies. */
// GLOBAL: XVT 0x664758
static D3DEXECUTEBUFFERDESC g_d3d_exec_buf_desc = {0};
/* Where the next vertex or instruction goes in the locked execute buffer; all
 * bits set until the first lock. */
// GLOBAL: XVT 0x664C80
uint8_t *g_d3d_write_ptr = (uint8_t *)(uintptr_t)-1;
/* Where the instructions start, after the vertices; set by
 * std3d_begin_instructions. */
// GLOBAL: XVT 0x664C84
static uint8_t *g_d3d_instr_start = 0;
/* The render target's description, filled by std3d_init_render_target_desc. */
// GLOBAL: XVT 0x6644F8
static struct std3d_render_target_desc g_std3d_render_target_desc = {0};
/* Points at g_std3d_render_target_desc once std3d_init_render_target_desc runs; NULL
 * before. */
// GLOBAL: XVT 0xA90BE0
static struct std3d_render_target_desc *g_p_std3d_render_target = 0;

/* Copies colorCount 16-bit colors into g_std3d_palette_scratch16. Does not check
 * colorCount against its 256 entries. */
// FUNCTION: XVT 0x4B0B60
void std3d_copy_palette_to_scratch16(const uint16_t *palette, int color_count)
{
	memcpy(g_std3d_palette_scratch16, palette,
	       (size_t)color_count * sizeof(*palette));
}

/* Fills g_tex_conv_buf1555 from colorCount colors in the opaque texture format: a
 * plain copy when that format is the 1555 one; otherwise each channel is taken
 * up to 8 bits and down into the 1555 format, and every entry but 0 gets full
 * alpha, so entry 0 is transparent. Does not check colorCount against 256. */
// FUNCTION: XVT 0x4B0B90
void std3d_convert_palette_to1555(const uint16_t *palette, int color_count)
{
	if (g_p_fmt_opaque_texture == g_p_fmt_rgba1555) {
		memcpy(g_tex_conv_buf1555, palette,
		       (size_t)color_count * sizeof(*palette));
	} else {
		struct color_info *source_format =
			&g_p_fmt_opaque_texture->color_info;
		struct color_info *target_format =
			&g_p_fmt_rgba1555->color_info;
		for (int color_index = 0; color_index < color_count;
		     ++color_index) {
			uint8_t channel =
				(uint8_t)((palette[color_index] >>
					   source_format->red_pos_shift)
					  << source_format
						     ->red_pos_shift_right);
			g_tex_conv_buf1555[color_index] =
				(uint16_t)((channel >>
					    target_format->red_pos_shift_right)
					   << target_format->red_pos_shift);
			channel = (uint8_t)((palette[color_index] >>
					     source_format->green_pos_shift)
					    << source_format
						       ->green_pos_shift_right);
			g_tex_conv_buf1555[color_index] |=
				(uint16_t)((channel >>
					    target_format
						    ->green_pos_shift_right)
					   << target_format->green_pos_shift);
			channel = (uint8_t)((palette[color_index] >>
					     source_format->blue_pos_shift)
					    << source_format
						       ->blue_pos_shift_right);
			g_tex_conv_buf1555[color_index] |=
				(uint16_t)((channel >>
					    target_format->blue_pos_shift_right)
					   << target_format->blue_pos_shift);
			if (color_index != 0) {
				channel = 0xff;
				g_tex_conv_buf1555[color_index] |=
					(uint16_t)((channel >>
						    target_format
							    ->alpha_pos_shift_right)
						   << target_format
							      ->alpha_pos_shift);
			}
		}
	}
}

/* Takes the DirectDraw object, sets g_std3d_render_option_flags to 0x19B3 and
 * g_std3dz_buffer_enabled to 1, gets the Direct3D interface into g_lp_d3d and
 * enumerates the devices into g_std3d_devices. Returns 1, setting
 * g_std3d_startup_done, when at least one device was found; 0 when there is no
 * DirectDraw object, getting the interface or the enumeration fails, or no
 * device is found. */
// FUNCTION: XVT 0x4B0DB0
int std3d_startup(void)
{
	g_std3d_direct_draw = renderer_get_direct_draw();
	if (g_std3d_direct_draw == NULL) {
#ifdef XVT_MODERN
		XVT_LOG_ERROR("d3d.failed reason=\"no_ddraw_device\"");
#else
		debug_printf("DDraw device not created yet!\n", 0, 0, 0, 0);
#endif
		return 0;
	}

	g_std3d_render_option_flags = 0x19b3;
	g_std3dz_buffer_enabled = 1;
#ifdef XVT_MODERN
	XVT_LOG_DEBUG("d3d.step step=\"create_interface\"");
#else
	debug_printf("Creating D3D interface object.\n", 0, 0, 0, 0);
#endif
	int result = g_std3d_direct_draw->lpVtbl->QueryInterface(
		g_std3d_direct_draw, &CLSID_IDirect3D, (void **)&g_lp_d3d);
	if (result != 0) {
#ifdef XVT_MODERN
		XVT_LOG_ERROR(
			"d3d.call_failed step=\"create_interface\" error=\"%s\"",
			std3d_lookup_error_string(
				result, g_std3d_error_string_table, 121));
#else
		debug_printf("Error %s creating Direct3D interface object.\n",
			     std3d_lookup_error_string(
				     result, g_std3d_error_string_table, 121),
			     0, 0, 0);
#endif
		return 0;
	}

#ifdef XVT_MODERN
	XVT_LOG_DEBUG("d3d.step step=\"enumerate_devices\"");
#else
	debug_printf("Enumerating D3D devices.\n", 0, 0, 0, 0);
#endif
	g_std3d_num_devices = 0;
	result = g_lp_d3d->lpVtbl->EnumDevices(
		g_lp_d3d, std3d_enum_devices_callback, NULL);
	if (result != 0) {
#ifdef XVT_MODERN
		XVT_LOG_ERROR(
			"d3d.call_failed step=\"enumerate_devices\" error=\"%s\"",
			std3d_lookup_error_string(
				result, g_std3d_error_string_table, 121));
#else
		debug_printf("Error %s when enumerating D3D devices.\n",
			     std3d_lookup_error_string(
				     result, g_std3d_error_string_table, 121),
			     0, 0, 0);
#endif
		return 0;
	}
	if (g_std3d_num_devices == 0) {
		return 0;
	}

#ifdef XVT_MODERN
	XVT_LOG_DEBUG("d3d.devices_found count=%u", g_std3d_num_devices);
#else
	debug_printf("%d D3D devices found.\n", g_std3d_num_devices,
		     std3d_lookup_error_string(result,
					       g_std3d_error_string_table, 121),
		     0, 0);
#endif
	g_std3d_startup_done = 1;
#ifdef XVT_MODERN
	XVT_LOG_INFO("d3d.started devices=%u", g_std3d_num_devices);
#else
	debug_printf("Startup Succeeded.\n", 0, 0, 0, 0);
#endif
	return 1;
}

/* Fills g_std3d_render_target_desc for a 16-bit RGB target (5, 6 and 5 bits) of
 * width by height pixels and pitch_bytes per row, with pitch_pixels of pitch_bytes
 * / 2 and size_bytes of pitch times height; points g_p_std3d_render_target at it
 * and returns it. */
// FUNCTION: XVT 0x4B0EF0
struct std3d_render_target_desc *
std3d_init_render_target_desc(unsigned int width, unsigned int height,
			      int pitch_bytes)
{
	memset(&g_std3d_render_target_desc, 0,
	       sizeof(g_std3d_render_target_desc));
	struct std3d_render_target_desc **render_target =
		&g_p_std3d_render_target;
	*render_target = &g_std3d_render_target_desc;
	g_std3d_render_target_desc.width = width;
	(*render_target)->height = height;
	g_p_std3d_render_target->pitch = pitch_bytes;
	g_p_std3d_render_target->pitch_pixels = pitch_bytes / 2;
	g_p_std3d_render_target->size_bytes = g_p_std3d_render_target->pitch *
					      g_p_std3d_render_target->height;
	g_p_std3d_render_target->color_info.color_mode = STDCOLOR_RGB;
	g_p_std3d_render_target->color_info.bpp = 16;
	g_p_std3d_render_target->color_info.red_bpp = 5;
	g_p_std3d_render_target->color_info.green_bpp = 6;
	g_p_std3d_render_target->color_info.blue_bpp = 5;
	g_p_std3d_render_target->color_info.red_pos_shift = 11;
	g_p_std3d_render_target->color_info.green_pos_shift = 5;
	g_p_std3d_render_target->color_info.blue_pos_shift = 0;
	g_p_std3d_render_target->color_info.red_pos_shift_right = 3;
	g_p_std3d_render_target->color_info.green_pos_shift_right = 2;
	g_p_std3d_render_target->color_info.blue_pos_shift_right = 3;
	g_p_std3d_render_target->color_info.alpha_bpp = 0;
	g_p_std3d_render_target->color_info.alpha_pos_shift = 0;
	struct std3d_render_target_desc *result = g_p_std3d_render_target;
	result->color_info.alpha_pos_shift_right = 0;
	return result;
}

/* Releases g_lp_d3d when it is set (the modern build then sets it to NULL) and
 * sets g_std3d_startup_done to 0. */
// FUNCTION: XVT 0x4B0FF0
void std3d_shutdown(void)
{
	if (g_lp_d3d != 0) {
		g_lp_d3d->lpVtbl->Release(g_lp_d3d);
#ifdef XVT_MODERN
		g_lp_d3d = NULL;
#endif
	}
#ifdef XVT_MODERN
	XVT_LOG_INFO("d3d.shutdown");
#else
	debug_printf(g_std3d_shutdown_succeeded_message, 0, 0, 0, 0);
#endif
	g_std3d_startup_done = 0;
}

/* Opens device device_idx of g_std3d_devices on the render surface. Returns 0
 * when a device is already open, the index is out of range, or a step fails:
 * the z-buffer, made when b_use_z_buffer is set, the device has one and
 * g_std3d_render_option_flags has a 0x1800 bit (it also sets g_std3dz_compare_cap);
 * the device; the texture formats, none found counting as a failure; the
 * viewport; or the initial render state. Then makes the execute buffer (a
 * failure there is only printed), empties the texture cache, picks the opaque
 * texture format and, with alpha textures, the 1555 format and, without alpha
 * blending, the 4444 format with a 32 by 32 buffer in it, reads the texture
 * memory, sets g_std3d_device_open and returns 1. */
// FUNCTION: XVT 0x4B1030
int std3d_create_device(unsigned int device_idx, int b_use_z_buffer)
{
	if (g_std3d_device_open != 0) {
#ifdef XVT_MODERN
		XVT_LOG_ERROR("d3d.failed reason=\"already_open\"");
#else
		debug_printf("Error: Multiple Opens Attempted.\n", 0, 0, 0, 0);
#endif
		return 0;
	}
	if (g_std3d_num_devices <= device_idx) {
		return 0;
	}

	g_std3d_cur_device_idx = device_idx;
	g_p_std3d_cur_device = &g_std3d_devices[device_idx];
	g_std3dz_buffer_enabled =
		b_use_z_buffer != 0 &&
		g_p_std3d_cur_device->caps.b_has_z_buffer != 0 &&
		(g_std3d_render_option_flags & 0x1800) != 0;
	if (g_std3dz_buffer_enabled != 0) {
		if (std3d_create_z_buffer(g_p_std3d_render_target->width,
					  g_p_std3d_render_target->height) ==
		    0) {
#ifdef XVT_MODERN
			XVT_LOG_ERROR("d3d.failed reason=\"z_buffer\"");
#else
			debug_printf("Error creating Z buffer.\n", 0, 0, 0, 0);
#endif
			return 0;
		}
		g_std3dz_compare_cap = 16;
		if ((g_p_std3d_cur_device->caps.z_cmp_caps_mask & 0x10) == 0) {
			g_std3dz_compare_cap = 2;
		}
#ifdef XVT_MODERN
		XVT_LOG_DEBUG("d3d.z_compare mode=\"%s\"",
			      g_std3dz_compare_cap == 16 ? "greater" : "less");
#else
		debug_printf("Z compare: %s\n",
			     g_std3dz_compare_cap == 16 ? "Greater" : "Less", 0,
			     0, 0);
#endif
	}

#ifdef XVT_MODERN
	XVT_LOG_DEBUG("d3d.device_creating device=%u", g_std3d_cur_device_idx);
#else
	debug_printf("Creating D3D device #%d.\n", g_std3d_cur_device_idx, 0, 0,
		     0);
#endif
	HRESULT result = g_std3d_render_surface->lpVtbl->QueryInterface(
		g_std3d_render_surface, &g_p_std3d_cur_device->guid,
		(void **)&g_d3d_device);
	const char *error_string;
	if (result != 0) {
		error_string = std3d_lookup_error_string(
			result, g_std3d_error_string_table, 121);
#ifdef XVT_MODERN
		XVT_LOG_ERROR(
			"d3d.call_failed step=\"create_device\" error=\"%s\"",
			error_string);
#else
		debug_printf("Error %s creating Direct3D device.\n",
			     error_string, 0, 0, 0);
#endif
		return 0;
	}

	g_std3d_num_texture_formats = 0;
	result = g_d3d_device->lpVtbl->EnumTextureFormats(
		g_d3d_device, (void *)std3d_enum_texture_formats, NULL);
	if (result != 0) {
		error_string = std3d_lookup_error_string(
			result, g_std3d_error_string_table, 121);
#ifdef XVT_MODERN
		XVT_LOG_ERROR(
			"d3d.call_failed step=\"enumerate_texture_formats\" error=\"%s\"",
			error_string);
#else
		debug_printf(
			"Error %s when enumerating D3D device texture formats.\n",
			error_string, 0, 0, 0);
#endif
		return 0;
	}
	if (g_std3d_num_texture_formats == 0) {
#ifdef XVT_MODERN
		XVT_LOG_ERROR("d3d.failed reason=\"no_texture_formats\"");
#else
		debug_printf("Error: no texture formats found.\n", 0, 0, 0, 0);
#endif
		return 0;
	}

	error_string =
		std3d_lookup_error_string(0, g_std3d_error_string_table, 121);
#ifdef XVT_MODERN
	XVT_LOG_DEBUG("d3d.texture_formats_found count=%d",
		      g_std3d_num_texture_formats);
#else
	debug_printf("%d texture formats found.\n", g_std3d_num_texture_formats,
		     error_string, 0);
#endif
	if (std3d_create_viewport(g_p_std3d_render_target->width,
				  g_p_std3d_render_target->height) == 0) {
#ifdef XVT_MODERN
		XVT_LOG_ERROR("d3d.failed reason=\"viewport\"");
#else
		debug_printf("Error creating viewport.\n", 0, 0, 0, 0);
#endif
		return 0;
	}
	if (std3d_set_initial_render_state() == 0) {
#ifdef XVT_MODERN
		XVT_LOG_ERROR("d3d.failed reason=\"render_state\"");
#else
		debug_printf("Error initializing render state.\n", 0, 0, 0, 0);
#endif
		return 0;
	}

#ifdef XVT_MODERN
	XVT_LOG_DEBUG("d3d.step step=\"create_execute_buffer\"");
#else
	debug_printf("Creating Execute buffer.\n", 0, 0, 0, 0);
#endif
	unsigned int max_buffer_size =
		g_p_std3d_cur_device->caps.max_buffer_size;
	g_std3d_exec_buf_size = 0x10000;
	if (max_buffer_size != 0) {
		g_std3d_exec_buf_size = max_buffer_size;
	}
	g_d3d_exec_buf_desc.dwFlags = 0;
	g_d3d_exec_buf_desc.dwCaps = 0;
	g_d3d_exec_buf_desc.dwBufferSize = 0;
	g_d3d_exec_buf_desc.lpData = NULL;
	g_d3d_exec_buf_desc.dwSize = 20;
	g_d3d_exec_buf_desc.dwFlags = 1;
	g_d3d_exec_buf_desc.dwBufferSize = g_std3d_exec_buf_size;
	unsigned int max_vertex_count =
		g_p_std3d_cur_device->caps.max_vertex_count;
	g_std3d_exec_buf_max_verts =
		max_vertex_count == 0
			? 512
			: (max_vertex_count < 512 ? max_vertex_count : 512);
#ifdef XVT_MODERN
	XVT_LOG_DEBUG("d3d.execute_buffer bytes=%u", g_std3d_exec_buf_size);
#else
	debug_printf("Execute buffer size: %d.\n", g_std3d_exec_buf_size, 0, 0,
		     0);
#endif
#ifdef XVT_MODERN
	XVT_LOG_DEBUG("d3d.execute_buffer_vertices max=%u",
		      g_std3d_exec_buf_max_verts);
#else
	debug_printf("Max vertices: %d.\n", g_std3d_exec_buf_max_verts, 0, 0,
		     0);
#endif
	result = g_d3d_device->lpVtbl->CreateExecuteBuffer(
		g_d3d_device, &g_d3d_exec_buf_desc, &g_d3d_execute_buffer,
		NULL);
	if (result != 0) {
		error_string = std3d_lookup_error_string(
			result, g_std3d_error_string_table, 121);
#ifdef XVT_MODERN
		XVT_LOG_ERROR(
			"d3d.call_failed step=\"create_execute_buffer\" error=\"%s\"",
			error_string);
#else
		debug_printf("Error %s creating D3D Execute buffer.\n",
			     error_string, 0, 0, 0);
#endif
	}

	g_tex_cache_count = 0;
	g_p_tex_cache_head = NULL;
	g_p_tex_cache_tail = NULL;
	g_std3d_texture_batch_tag = 1;
	g_fmt_idx_opaque_texture = std3d_find_closest_format(
		&g_p_std3d_render_target->color_info, g_std3d_texture_formats,
		g_std3d_num_texture_formats);
	g_p_fmt_opaque_texture =
		&g_std3d_texture_formats[g_fmt_idx_opaque_texture];
	if (g_p_std3d_cur_device->caps.b_alpha_texture != 0) {
		struct color_info alpha_format;
		alpha_format.color_mode = STDCOLOR_RGBA;
		alpha_format.bpp = 16;
		alpha_format.red_bpp = 5;
		alpha_format.green_bpp = 5;
		alpha_format.blue_bpp = 5;
		alpha_format.alpha_bpp = 1;
		g_fmt_idx_rgba1555 = std3d_find_closest_format(
			&alpha_format, g_std3d_texture_formats,
			g_std3d_num_texture_formats);
		g_p_fmt_rgba1555 = &g_std3d_texture_formats[g_fmt_idx_rgba1555];
		if (g_p_std3d_cur_device->caps.b_alpha_blend == 0) {
			alpha_format.red_bpp = 4;
			alpha_format.green_bpp = 4;
			alpha_format.blue_bpp = 4;
			alpha_format.alpha_bpp = 4;
			g_fmt_idx_rgba4444 = std3d_find_closest_format(
				&alpha_format, g_std3d_texture_formats,
				g_std3d_num_texture_formats);
			g_p_fmt_rgba4444 =
				&g_std3d_texture_formats[g_fmt_idx_rgba4444];
			struct std3d_raster_info raster;
			raster.width = 32;
			raster.height = 32;
			memcpy(&raster.color_mode,
			       &g_p_fmt_rgba4444->color_info,
			       sizeof(g_p_fmt_rgba4444->color_info));
			g_p_std3dv_buffer =
				std3d_alloc_v_buffer(&raster, 0, 0, 0);
		}
	}

	std3d_query_texture_vid_mem(&g_p_std3d_cur_device->total_memory,
				    &g_p_std3d_cur_device->available_memory);
#ifdef XVT_MODERN
	XVT_LOG_DEBUG("d3d.texture_ram total=%u free=%u",
		      g_p_std3d_cur_device->total_memory,
		      g_p_std3d_cur_device->available_memory);
#else
	debug_printf("Texture Ram  Total: %d bytes  Free: %d bytes.\n",
		     g_p_std3d_cur_device->total_memory,
		     g_p_std3d_cur_device->available_memory, 0, 0);
#endif
#ifdef XVT_MODERN
	XVT_LOG_DEBUG("d3d.device_opened device=%u", g_std3d_cur_device_idx);
#else
	debug_printf("Device #%d opened successfully.\n",
		     g_std3d_cur_device_idx, 0, 0, 0);
#endif
#ifdef XVT_MODERN
	XVT_LOG_INFO("d3d.opened device=%u", g_std3d_cur_device_idx);
#else
	debug_printf("std3D opened.\n", 0, 0, 0, 0);
#endif
	g_std3d_device_open = 1;
	return 1;
}

/* Sets g_std3d_render_surface to surface and returns it. */
// FUNCTION: XVT 0x4B15A0
struct IDirectDrawSurface *
std3d_set_render_surface(struct IDirectDrawSurface *surface)
{
	return g_std3d_render_surface = surface;
}

/* Sets the color overlay's red, green, blue and on flag; returns enabled. */
// FUNCTION: XVT 0x4B15B0
int std3d_set_color_overlay_params(float red, float green, float blue,
				   int enabled)
{
	g_std3d_color_overlay_red = red;
	g_std3d_color_overlay_green = green;
	g_std3d_color_overlay_blue = blue;
	return g_std3d_color_overlay_enabled = enabled;
}

/* Returns how many times n halves before it is 1 or less: log2 of n rounded
 * down for n of 1 or more, 0 for n under 2. */
// FUNCTION: XVT 0x4B15E0
int std3d_log2_floor(int n)
{
	int result = 0;

	while (n > 1) {
		n >>= 1;
		++result;
	}
	return result;
}

/* Returns the message of the first of entry_count entries whose code is
 * errorCode, or g_std3d_unknown_error_message when none is. */
// FUNCTION: XVT 0x4B1600
const char *
std3d_lookup_error_string(int error_code,
			  const struct std3d_error_string_entry *entries,
			  int entry_count)
{
	const char *result = g_std3d_unknown_error_message;
	int entry_index = 0;
	if (entry_count > 0) {
		const struct std3d_error_string_entry *entry = entries;
		do {
			if (entry->code == error_code) {
				result = entries[entry_index].message;
				break;
			}
			++entry;
			++entry_index;
		} while (entry_index < entry_count);
	}
	return result;
}

/* Fills 256 16-bit colors in p_fmt's format from 256 RGB triples, each channel
 * shifted down from 8 bits and into place. For a format with alpha bits the
 * alpha is default_alpha, or with color_key 0 for a color that comes out 0 and
 * 0xFF for any other; but a 1-bit format first replaces the value with 0xFF
 * minus it, and without color_key that change carries into the next entry, so
 * the alpha alternates. With color_key and 1-bit alpha, a color that comes out 0
 * is therefore opaque and every other transparent. */
// FUNCTION: XVT 0x4B1640
void std3d_build_colormap16(uint8_t *p_rgb888, uint16_t *p_out,
			    struct color_info *p_fmt, uint8_t default_alpha,
			    int color_key)
{
	uint16_t *output = p_out;
	struct color_info *format = p_fmt;
	uint8_t *rgb888 = p_rgb888;

	int entries_remaining = 256;
	do {
		*output = (uint16_t)((uint8_t)(rgb888[0] >>
					       format->red_pos_shift_right)
				     << format->red_pos_shift);
		*output |= (uint16_t)((uint8_t)(rgb888[1] >>
						format->green_pos_shift_right)
				      << format->green_pos_shift);
		*output |= (uint16_t)((uint8_t)(rgb888[2] >>
						format->blue_pos_shift_right)
				      << format->blue_pos_shift);
		if ((uint8_t)color_key != 0) {
			default_alpha = (uint8_t)((*output == 0) - 1);
		}
		int alpha_bpp = format->alpha_bpp;
		if (alpha_bpp == 1) {
			default_alpha = (uint8_t)(0xFF - default_alpha);
		}
		if (alpha_bpp != 0) {
			*output |=
				(uint16_t)((uint8_t)(default_alpha >>
						     format->alpha_pos_shift_right)
					   << format->alpha_pos_shift);
		}
		rgb888 += 3;
		++output;
		--entries_remaining;
	} while (entries_remaining != 0);
}

/* std3d_build_colormap16 with alpha 0xFF and no color key. */
// FUNCTION: XVT 0x4B1710
void std3d_build_colormap_opaque(uint8_t *p_rgb888, uint16_t *p_out,
				 struct color_info *p_fmt)
{
	std3d_build_colormap16(p_rgb888, p_out, p_fmt, 0xFF, 0);
}

/* std3d_build_colormap16 with alpha 0xFF and the color key. */
// FUNCTION: XVT 0x4B1730
void std3d_build_colormap_color_key(uint8_t *p_rgb888, uint16_t *p_out,
				    struct color_info *p_fmt)
{
	std3d_build_colormap16(p_rgb888, p_out, p_fmt, 0xFF, 1);
}

/* std3d_build_colormap16 with alpha and no color key. */
// FUNCTION: XVT 0x4B1750
void std3d_build_colormap_alpha(uint8_t *p_rgb888, uint16_t *p_out,
				struct color_info *p_fmt, uint8_t alpha)
{
	std3d_build_colormap16(p_rgb888, p_out, p_fmt, alpha, 0);
}

/* Allocates a buffer in plain memory with a copy of raster and width * height *
 * (bpp >> 3) bytes of pixels, sets its rowPitch to width * (bpp >> 3) and
 * returns it. Does not check either allocation; ignores any further
 * arguments. */
// FUNCTION: XVT 0x4B1770
struct std3dv_buffer *
std3d_alloc_v_buffer(const struct std3d_raster_info *raster, ...)
{
	struct std3dv_buffer *vbuffer;

	vbuffer = (struct std3dv_buffer *)malloc(sizeof(*vbuffer));
	memset(vbuffer, 0, sizeof(*vbuffer));
	vbuffer->storage_type = 0;
	memcpy(&vbuffer->raster, raster, sizeof(vbuffer->raster));
	vbuffer->pixels =
		malloc(raster->width * raster->height * (raster->bpp >> 3));
	vbuffer->raster.row_pitch = raster->width * (raster->bpp >> 3);
	return vbuffer;
}

/* Releases a surface buffer's surface (storage_type 1) or frees a plain buffer's
 * pixels, then zeroes and frees the buffer. */
// FUNCTION: XVT 0x4B17D0
void std3d_free_v_buffer(struct std3dv_buffer *vbuffer)
{
	if (vbuffer->storage_type == 1) {
		if (vbuffer->dd_surface != NULL) {
			vbuffer->dd_surface->lpVtbl->Release(
				vbuffer->dd_surface);
		}
	} else {
		free(vbuffer->pixels);
	}
	memset(vbuffer, 0, sizeof(*vbuffer));
	free(vbuffer);
}

/* Adds a lock to the buffer. On the first lock of a surface buffer it locks the
 * surface, waiting, and takes its pixels and pitch; when that fails it returns
 * without counting the lock. */
// FUNCTION: XVT 0x4B1810
void std3d_lock_v_buffer(struct std3dv_buffer *vbuffer)
{
	if (vbuffer->storage_type == 1 && vbuffer->lock_count == 0) {
		DDSURFACEDESC surface_desc;
		memset(&surface_desc, 0, sizeof(surface_desc));
		surface_desc.dwSize = sizeof(surface_desc);
		HRESULT result = vbuffer->dd_surface->lpVtbl->Lock(
			vbuffer->dd_surface, NULL, &surface_desc, DDLOCK_WAIT,
			NULL);
		if (result != 0) {
#ifdef XVT_MODERN
			XVT_LOG_ERROR(
				"d3d.buffer_call_failed op=\"lock\" result=%#x buffer=%p surface=%p",
				(unsigned int)result, (void *)vbuffer,
				(void *)vbuffer->dd_surface);
#else
			debug_printf("Error %x locking buffer %x, surface %x\n",
				     result, vbuffer, vbuffer->dd_surface);
#endif
			return;
		}
		vbuffer->pixels = surface_desc.lpSurface;
		vbuffer->raster.row_pitch = surface_desc.lPitch;
	}

	++vbuffer->lock_count;
}

/* Undoes a lock; a buffer with none is left alone. On the last lock of a
 * surface buffer it unlocks the surface; when that fails it returns without
 * undoing the count. */
// FUNCTION: XVT 0x4B1890
void std3d_unlock_v_buffer(struct std3dv_buffer *vbuffer)
{
	if ((unsigned int)vbuffer->lock_count < 1) {
#ifdef XVT_MODERN
		XVT_LOG_WARN("d3d.unlock_unlocked buffer=%p", (void *)vbuffer);
#else
		debug_printf("Unlock Warning: buffer %x, not locked\n",
			     vbuffer);
#endif
		return;
	}

	if (vbuffer->lock_count == 1 && vbuffer->storage_type == 1) {
		HRESULT result = vbuffer->dd_surface->lpVtbl->Unlock(
			vbuffer->dd_surface, vbuffer->pixels);
		if (result != 0) {
#ifdef XVT_MODERN
			XVT_LOG_ERROR(
				"d3d.buffer_call_failed op=\"unlock\" result=%#x buffer=%p surface=%p",
				(unsigned int)result, (void *)vbuffer,
				(void *)vbuffer->dd_surface);
#else
			debug_printf(
				"Error %x unlocking buffer %x, surface %x\n",
				result, vbuffer, vbuffer->dd_surface);
#endif
			return;
		}
	}

	--vbuffer->lock_count;
}

/* Closes the open device: frees g_p_std3dv_buffer, releases the execute buffer,
 * empties the texture cache, and releases the viewport, the viewport material,
 * the z-buffer surface and the device, setting each to NULL; then clears
 * g_std3d_device_open. With no device open it only makes a debug print. */
// FUNCTION: XVT 0x4B18F0
void std3d_close(void)
{
	if (!g_std3d_device_open) {
#ifdef XVT_MODERN
		XVT_LOG_ERROR("d3d.failed reason=\"already_closed\"");
#else
		debug_printf("Error: Multiple Closes Attempted.\n", 0, 0, 0, 0);
#endif
		return;
	}

	if (g_p_std3dv_buffer != NULL) {
		std3d_free_v_buffer(g_p_std3dv_buffer);
		g_p_std3dv_buffer = NULL;
	}

	g_d3d_execute_buffer->lpVtbl->Release(g_d3d_execute_buffer);
	std3d_flush_texture_cache();

	if (g_d3d_viewport != NULL) {
		g_d3d_viewport->lpVtbl->Release(g_d3d_viewport);
		g_d3d_viewport = NULL;
	}

	if (g_d3d_viewport_material != NULL) {
		g_d3d_viewport_material->lpVtbl->Release(
			g_d3d_viewport_material);
		g_d3d_viewport_material = NULL;
	}

	if (g_std3dz_buffer_surface_block.surface != NULL) {
		g_std3dz_buffer_surface_block.surface->lpVtbl->Release(
			g_std3dz_buffer_surface_block.surface);
		g_std3dz_buffer_surface_block.surface = NULL;
	}

	if (g_d3d_device != NULL) {
		g_d3d_device->lpVtbl->Release(g_d3d_device);
		g_d3d_device = NULL;
	}

#ifdef XVT_MODERN
	XVT_LOG_INFO("d3d.closed");
#else
	debug_printf("std3D closed.\n", 0, 0, 0, 0);
#endif
	g_std3d_device_open = 0;
}

/* Copies all of source into destination with its top left at destination_x,
 * destination_y, row by row, locking both. Ignores source_x and source_y and does
 * not clip. */
// FUNCTION: XVT 0x4B19E0
void std3d_blit_v_buffer(struct std3dv_buffer *destination,
			 struct std3dv_buffer *source, int destination_x,
			 int destination_y, int source_x, int source_y)
{
	(void)source_x;
	(void)source_y;

	std3d_lock_v_buffer(destination);
	std3d_lock_v_buffer(source);

	uint8_t *source_pixels = (uint8_t *)source->pixels;
	uint8_t *destination_pixels =
		(uint8_t *)destination->pixels +
		destination_x * (destination->raster.bpp >> 3) +
		destination_y * destination->raster.row_pitch;
	unsigned int row_bytes =
		source->raster.width * (source->raster.bpp >> 3);
	for (unsigned int row_index = 0; row_index < source->raster.height;
	     ++row_index) {
		memcpy(destination_pixels, source_pixels, row_bytes);
		destination_pixels += destination->raster.row_pitch;
		source_pixels += source->raster.row_pitch;
	}

	std3d_unlock_v_buffer(destination);
	std3d_unlock_v_buffer(source);
}

/* Returns g_std3d_render_option_flags. Nothing calls this. */
// FUNCTION: XVT 0x4B1A80
unsigned int std3d_get_cap_flags(void) { return g_std3d_render_option_flags; }

/* Fills the whole buffer with packed_color at 8, 16 or 32 bits per pixel,
 * locking it; leaves a buffer of any other depth alone. Ignores fillMode. */
// FUNCTION: XVT 0x4B1A90
void std3d_fill_v_buffer(struct std3dv_buffer *vbuffer,
			 unsigned int packed_color, int fill_mode)
{
	(void)fill_mode;
	std3d_lock_v_buffer(vbuffer);
	uint8_t *row_pixels = vbuffer->pixels;
	unsigned int row_index;
	switch (vbuffer->raster.bpp) {
	case 8:
		row_index = 0;
		if (vbuffer->raster.height > row_index) {
			do {
				memset(row_pixels, (uint8_t)packed_color,
				       vbuffer->raster.width);
				row_pixels += vbuffer->raster.row_pitch;
				++row_index;
			} while (vbuffer->raster.height > row_index);
		}
		break;
	case 16:
		row_index = 0;
		if (vbuffer->raster.height > row_index) {
			do {
				unsigned int column_index = 0;
				if (vbuffer->raster.width > column_index) {
					uint16_t *destination16 =
						(uint16_t *)row_pixels;
					do {
						*destination16 =
							(uint16_t)packed_color;
						++destination16;
						++column_index;
					} while (vbuffer->raster.width >
						 column_index);
				}
				row_pixels += vbuffer->raster.row_pitch;
				++row_index;
			} while (vbuffer->raster.height > row_index);
		}
		break;
	case 32: {
		unsigned int row32 = 0;
		if (vbuffer->raster.height > row32) {
			do {
				unsigned int column32 = 0;
				if (vbuffer->raster.width > column32) {
					uint32_t *destination32 =
						(uint32_t *)row_pixels;
					do {
						*destination32 = packed_color;
						++destination32;
						++column32;
					} while (vbuffer->raster.width >
						 column32);
				}
				row_pixels += vbuffer->raster.row_pitch;
				++row32;
			} while (vbuffer->raster.height > row32);
		}
		break;
	}
	default:
		break;
	}

	std3d_unlock_v_buffer(vbuffer);
}

/* Sets g_std3d_render_option_flags and writes the initial render state again.
 * Nothing calls this. */
// FUNCTION: XVT 0x4B1B70
void std3d_set_cap_flags(unsigned int cap_flags)
{
	g_std3d_render_option_flags = cap_flags;
	int result = std3d_set_initial_render_state();
	if (result == 0) {
#ifdef XVT_MODERN
		XVT_LOG_ERROR("d3d.failed reason=\"render_state\"");
#else
		debug_printf("Error initializing render state.\n", 0, 0, 0, 0);
#endif
	}
}

/* Sets the fog color, 0 to 255 for each channel, and returns red8. Nothing
 * calls this. */
// FUNCTION: XVT 0x4B1BA0
int std3d_set_fog_color8(unsigned int red8, unsigned int green8,
			 unsigned int blue8)
{
	g_std3d_fog_color_red8 = red8;
	g_std3d_fog_color_green8 = green8;
	g_std3d_fog_color_blue8 = blue8;
	return red8;
}

/* Sets the fog table start and end and returns start_bits. Nothing calls
 * this. */
// FUNCTION: XVT 0x4B1BC0
int std3d_set_fog_table_range_bits(unsigned int start_bits,
				   unsigned int end_bits)
{
	g_std3d_fog_table_start_bits = start_bits;
	g_std3d_fog_table_end_bits = end_bits;
	return start_bits;
}

/* Sets the four texture size limits and returns maxHeight. Nothing calls
 * this. */
// FUNCTION: XVT 0x4B1BE0
int std3d_set_texture_size_caps(int min_width, int min_height, int max_width,
				int max_height)
{
	g_std3d_min_texture_width = min_width;
	g_std3d_min_texture_height = min_height;
	g_std3d_max_texture_width = max_width;
	return g_std3d_max_texture_height = max_height;
}

/* Begins a Direct3D scene on g_d3d_device; a failure is only printed. */
// FUNCTION: XVT 0x4B1C10
void std3d_start_scene(void)
{
	int result = g_d3d_device->lpVtbl->BeginScene(g_d3d_device);
	if (result != 0) {
#ifdef XVT_MODERN
		XVT_LOG_ERROR(
			"d3d.call_failed step=\"begin_scene\" error=\"%s\"",
			std3d_lookup_error_string(
				result, g_std3d_error_string_table, 121));
#else
		debug_printf(g_std3d_begin_scene_error_format,
			     std3d_lookup_error_string(
				     result, g_std3d_error_string_table, 121),
			     0, 0, 0);
#endif
	}
}

/* Ends the Direct3D scene; a failure is only printed. */
// FUNCTION: XVT 0x4B1C50
void std3d_end_scene(void)
{
	int result = g_d3d_device->lpVtbl->EndScene(g_d3d_device);
	if (result != 0) {
#ifdef XVT_MODERN
		XVT_LOG_ERROR("d3d.call_failed step=\"end_scene\" error=\"%s\"",
			      std3d_lookup_error_string(
				      result, g_std3d_error_string_table, 121));
#else
		debug_printf(g_std3d_end_scene_error_format,
			     std3d_lookup_error_string(
				     result, g_std3d_error_string_table, 121),
			     0, 0, 0);
#endif
	}
}

/* Starts a batch: sets the vertex and triangle counts to 0, raises
 * g_std3d_texture_batch_tag, marks no texture as selected, and locks the execute
 * buffer, pointing g_d3d_exec_buf_base and g_d3d_write_ptr at its start. Returns 1,
 * or 0 when the lock fails. */
// FUNCTION: XVT 0x4B1C90
int std3d_lock_execute_buffer(void)
{
	g_d3d_buf_vert_count = 0;
	g_std3d_exec_buf_tri_count = 0;
	++g_std3d_texture_batch_tag;
	g_d3d_cur_texture = (struct std3d_tex_cache_node *)1;
	int result = g_d3d_execute_buffer->lpVtbl->Lock(g_d3d_execute_buffer,
							&g_d3d_exec_buf_desc);
	if (result != 0) {
#ifdef XVT_MODERN
		XVT_LOG_ERROR(
			"d3d.call_failed step=\"lock_execute_buffer\" error=\"%s\"",
			std3d_lookup_error_string(
				result, g_std3d_error_string_table, 121));
#else
		debug_printf(g_std3d_lock_execute_buffer_error_format,
			     std3d_lookup_error_string(
				     result, g_std3d_error_string_table, 121),
			     0, 0, 0);
#endif
		return 0;
	}
	g_d3d_exec_buf_base = (uint8_t *)g_d3d_exec_buf_desc.lpData;
	g_d3d_write_ptr = g_d3d_exec_buf_base;
	return 1;
}

/* Copies count vertices to g_d3d_write_ptr, unless they are already there, and
 * advances it; returns 1, or 0 without copying when g_d3d_buf_vert_count would
 * pass g_std3d_exec_buf_max_verts. Does not check the buffer's size. */
// FUNCTION: XVT 0x4B1D10
int std3d_add_vertices(const D3DTLVERTEX *vertices, int count)
{
	int previous_vertex_count = g_d3d_buf_vert_count;
	if ((unsigned int)(count + g_d3d_buf_vert_count) >
	    g_std3d_exec_buf_max_verts) {
		return 0;
	}
	uint8_t *write_ptr = g_d3d_write_ptr;
	if ((const void *)write_ptr != (const void *)vertices) {
		memcpy(write_ptr, vertices, (size_t)count * sizeof(*vertices));
	}
	g_d3d_buf_vert_count = count + previous_vertex_count;
	g_d3d_write_ptr = write_ptr + (size_t)count * sizeof(*vertices);
	return 1;
}

/* Writes, after the vertices, the instruction to copy all g_d3d_buf_vert_count of
 * them, records it as the start of the instructions, and returns 1. */
// FUNCTION: XVT 0x4B1D70
int std3d_begin_instructions(void)
{
	g_d3d_instr_start = g_d3d_write_ptr;
	((D3DINSTRUCTION *)g_d3d_write_ptr)->bOpcode = D3DOP_PROCESSVERTICES;
	((D3DINSTRUCTION *)g_d3d_write_ptr)->bSize = sizeof(D3DPROCESSVERTICES);
	((D3DINSTRUCTION *)g_d3d_write_ptr)->wCount = 1;
	g_d3d_write_ptr += sizeof(D3DINSTRUCTION);

	((D3DPROCESSVERTICES *)g_d3d_write_ptr)->dwFlags =
		D3DPROCESSVERTICES_COPY;
	((D3DPROCESSVERTICES *)g_d3d_write_ptr)->wStart = 0;
	((D3DPROCESSVERTICES *)g_d3d_write_ptr)->wDest = 0;
	((D3DPROCESSVERTICES *)g_d3d_write_ptr)->dwCount =
		(uint32_t)g_d3d_buf_vert_count;
	((D3DPROCESSVERTICES *)g_d3d_write_ptr)->dwReserved = 0;
	g_d3d_write_ptr += sizeof(D3DPROCESSVERTICES);
	return 1;
}

/* Writes count triangles as instructions, grouping neighbors with the same
 * texture and flags: for each group the changed render states
 * (std3d_set_render_state), a texture handle state when the texture differs from
 * the one last selected (handle 0 for none), then one triangle instruction with
 * every edge enabled. Returns 1. Does not check the buffer's size. */
// FUNCTION: XVT 0x4B1DE0
int std3d_add_triangles(const struct std3d_render_tri *triangles,
			unsigned int count)
{
	int group_count;
	unsigned int triangle_index;

	for (unsigned int group_start = 0; group_start < count;
	     group_start += group_count) {
		struct std3d_tex_cache_node *texture =
			triangles[group_start].texture;
		group_count = 0;
		if (texture == NULL) {
			std3d_render_state_flags flags =
				triangles[group_start].flags;

			for (triangle_index = group_start;
			     triangle_index < count; ++triangle_index) {
				if (triangles[triangle_index].texture != NULL ||
				    triangles[triangle_index].flags != flags) {
					break;
				}
				++group_count;
			}

			std3d_set_render_state(flags);
			if (g_d3d_cur_texture != NULL) {
				((D3DINSTRUCTION *)g_d3d_write_ptr)->bOpcode =
					D3DOP_STATERENDER;
				((D3DINSTRUCTION *)g_d3d_write_ptr)->bSize =
					sizeof(D3DSTATE);
				((D3DINSTRUCTION *)g_d3d_write_ptr)->wCount = 1;
				g_d3d_write_ptr += sizeof(D3DINSTRUCTION);
				((D3DSTATE *)g_d3d_write_ptr)->dwState =
					D3DRENDERSTATE_TEXTUREHANDLE;
				((D3DSTATE *)g_d3d_write_ptr)->dwArg = 0;
				g_d3d_cur_texture = NULL;
				g_d3d_write_ptr += sizeof(D3DSTATE);
			}

			((D3DINSTRUCTION *)g_d3d_write_ptr)->bOpcode =
				D3DOP_TRIANGLE;
			((D3DINSTRUCTION *)g_d3d_write_ptr)->bSize =
				sizeof(D3DTRIANGLE);
			((D3DINSTRUCTION *)g_d3d_write_ptr)->wCount =
				(uint16_t)group_count;
			g_d3d_write_ptr += sizeof(D3DINSTRUCTION);
			for (triangle_index = 0;
			     triangle_index < (unsigned int)group_count;
			     ++triangle_index) {
				((D3DTRIANGLE *)g_d3d_write_ptr)->v1 =
					(uint16_t)triangles[group_start +
							    triangle_index]
						.vertex_index0;
				((D3DTRIANGLE *)g_d3d_write_ptr)->v2 =
					(uint16_t)triangles[group_start +
							    triangle_index]
						.vertex_index1;
				((D3DTRIANGLE *)g_d3d_write_ptr)->v3 =
					(uint16_t)triangles[group_start +
							    triangle_index]
						.vertex_index2;
				((D3DTRIANGLE *)g_d3d_write_ptr)->wFlags =
					D3DTRIFLAG_EDGEENABLE1 |
					D3DTRIFLAG_EDGEENABLE2 |
					D3DTRIFLAG_EDGEENABLE3;
				g_d3d_write_ptr += sizeof(D3DTRIANGLE);
			}
		} else {
			std3d_render_state_flags flags =
				triangles[group_start].flags;

			for (triangle_index = group_start;
			     triangle_index < count; ++triangle_index) {
				if (triangles[triangle_index].texture !=
					    texture ||
				    triangles[triangle_index].flags != flags) {
					break;
				}
				++group_count;
			}

			std3d_set_render_state(flags);
			if (g_d3d_cur_texture != texture) {
				((D3DINSTRUCTION *)g_d3d_write_ptr)->bOpcode =
					D3DOP_STATERENDER;
				((D3DINSTRUCTION *)g_d3d_write_ptr)->bSize =
					sizeof(D3DSTATE);
				((D3DINSTRUCTION *)g_d3d_write_ptr)->wCount = 1;
				g_d3d_write_ptr += sizeof(D3DINSTRUCTION);
				((D3DSTATE *)g_d3d_write_ptr)->dwState =
					D3DRENDERSTATE_TEXTUREHANDLE;
				((D3DSTATE *)g_d3d_write_ptr)->dwArg =
					texture->tex_handle;
				g_d3d_write_ptr += sizeof(D3DSTATE);
				g_d3d_cur_texture = texture;
			}

			((D3DINSTRUCTION *)g_d3d_write_ptr)->bOpcode =
				D3DOP_TRIANGLE;
			((D3DINSTRUCTION *)g_d3d_write_ptr)->bSize =
				sizeof(D3DTRIANGLE);
			((D3DINSTRUCTION *)g_d3d_write_ptr)->wCount =
				(uint16_t)group_count;
			g_d3d_write_ptr += sizeof(D3DINSTRUCTION);
			for (triangle_index = 0;
			     triangle_index < (unsigned int)group_count;
			     ++triangle_index) {
				((D3DTRIANGLE *)g_d3d_write_ptr)->v1 =
					(uint16_t)triangles[group_start +
							    triangle_index]
						.vertex_index0;
				((D3DTRIANGLE *)g_d3d_write_ptr)->v2 =
					(uint16_t)triangles[group_start +
							    triangle_index]
						.vertex_index1;
				((D3DTRIANGLE *)g_d3d_write_ptr)->v3 =
					(uint16_t)triangles[group_start +
							    triangle_index]
						.vertex_index2;
				((D3DTRIANGLE *)g_d3d_write_ptr)->wFlags =
					D3DTRIFLAG_EDGEENABLE1 |
					D3DTRIFLAG_EDGEENABLE2 |
					D3DTRIFLAG_EDGEENABLE3;
				g_d3d_write_ptr += sizeof(D3DTRIANGLE);
			}
		}
	}

	g_std3d_exec_buf_tri_count += count;
	return 1;
}

/* Ends the instructions, unlocks the execute buffer and executes it, unclipped,
 * on the viewport. Returns 1, or 0 when the unlock or the execute fails. */
// FUNCTION: XVT 0x4B2020
int std3d_execute_buffer(void)
{
	((D3DINSTRUCTION *)g_d3d_write_ptr)->bOpcode = D3DOP_EXIT;
	((D3DINSTRUCTION *)g_d3d_write_ptr)->bSize = 0;
	((D3DINSTRUCTION *)g_d3d_write_ptr)->wCount = 0;
	g_d3d_write_ptr += sizeof(D3DINSTRUCTION);

	int result = g_d3d_execute_buffer->lpVtbl->Unlock(g_d3d_execute_buffer);
	if (result != 0) {
#ifdef XVT_MODERN
		XVT_LOG_ERROR(
			"d3d.call_failed step=\"unlock_execute_buffer\" error=\"%s\"",
			std3d_lookup_error_string(
				result, g_std3d_error_string_table, 121));
#else
		debug_printf("Error %s unlocking D3D Execute buffer.\n",
			     std3d_lookup_error_string(
				     result, g_std3d_error_string_table, 121),
			     0, 0, 0);
#endif
		return 0;
	}

	D3DEXECUTEDATA execute_data;
	memset(&execute_data, 0, sizeof(execute_data));
	execute_data.dwSize = sizeof(execute_data);
	execute_data.dwVertexCount = g_d3d_buf_vert_count;
	execute_data.dwInstructionOffset =
		(uint32_t)(g_d3d_instr_start - g_d3d_exec_buf_base);
	execute_data.dwInstructionLength =
		(uint32_t)(g_d3d_write_ptr - g_d3d_instr_start);
	g_d3d_execute_buffer->lpVtbl->SetExecuteData(g_d3d_execute_buffer,
						     &execute_data);
	result = g_d3d_device->lpVtbl->Execute(
		g_d3d_device, g_d3d_execute_buffer, g_d3d_viewport,
		D3DEXECUTE_UNCLIPPED);
	if (result != 0) {
#ifdef XVT_MODERN
		XVT_LOG_ERROR(
			"d3d.call_failed step=\"execute_buffer\" error=\"%s\"",
			std3d_lookup_error_string(
				result, g_std3d_error_string_table, 121));
#else
		debug_printf("Error %s executing buffer.\n",
			     std3d_lookup_error_string(
				     result, g_std3d_error_string_table, 121),
			     0, 0, 0);
#endif
		return 0;
	}
	return 1;
}

/* Writes render-state instructions for the groups of bits in flags that differ
 * from g_d3d_state_flags, then stores flags there; does nothing when they are
 * equal. Mono is off with STD3D_RS_MONO_DISABLE. With alpha blend or
 * modulate-alpha: source blend 5, destination blend 6, texture blend 4 with
 * modulate-alpha else 2, blending on; with neither: source blend 2, destination
 * blend 1, the same texture blend, blending off. The z compare is
 * std3d_map_z_cmp_func(g_std3dz_compare_cap) with STD3D_RS_Z_COMPARE_ENABLE, else 8;
 * z writes follow STD3D_RS_Z_WRITE_ENABLE. Each filter is 2 (linear) with its
 * bit, else 1. Fog is on with the fog color, table mode 3 and the table start
 * and end, or off. STD3D_RS_TEXTURE_ADDRESS_CLAMP is not written. */
// FUNCTION: XVT 0x4B2130
void std3d_set_render_state(std3d_render_state_flags flags)
{
	if (g_d3d_state_flags == flags) {
		return;
	}

	if (((unsigned int)(flags ^ g_d3d_state_flags) &
	     STD3D_RS_MONO_DISABLE) != 0) {
		((D3DINSTRUCTION *)g_d3d_write_ptr)->bOpcode =
			D3DOP_STATERENDER;
		((D3DINSTRUCTION *)g_d3d_write_ptr)->bSize = sizeof(D3DSTATE);
		((D3DINSTRUCTION *)g_d3d_write_ptr)->wCount = 1;
		g_d3d_write_ptr += sizeof(D3DINSTRUCTION);
		((D3DSTATE *)g_d3d_write_ptr)->dwState =
			D3DRENDERSTATE_MONOENABLE;
		if ((flags & STD3D_RS_MONO_DISABLE) != 0) {
			((D3DSTATE *)g_d3d_write_ptr)->dwArg = 0;
		} else {
			((D3DSTATE *)g_d3d_write_ptr)->dwArg = 1;
		}
		g_d3d_write_ptr += sizeof(D3DSTATE);
	}

	if (((unsigned int)(flags ^ g_d3d_state_flags) &
	     (STD3D_RS_ALPHA_BLEND | STD3D_RS_TEXTURE_MODULATE_ALPHA)) != 0) {
		((D3DINSTRUCTION *)g_d3d_write_ptr)->bOpcode =
			D3DOP_STATERENDER;
		((D3DINSTRUCTION *)g_d3d_write_ptr)->bSize = sizeof(D3DSTATE);
		((D3DINSTRUCTION *)g_d3d_write_ptr)->wCount = 4;
		if ((flags & (STD3D_RS_ALPHA_BLEND |
			      STD3D_RS_TEXTURE_MODULATE_ALPHA)) != 0) {
			g_d3d_write_ptr += sizeof(D3DINSTRUCTION);
			((D3DSTATE *)g_d3d_write_ptr)->dwState =
				D3DRENDERSTATE_SRCBLEND;
			((D3DSTATE *)g_d3d_write_ptr)->dwArg = 5;
			g_d3d_write_ptr += sizeof(D3DSTATE);
			((D3DSTATE *)g_d3d_write_ptr)->dwState =
				D3DRENDERSTATE_DESTBLEND;
			((D3DSTATE *)g_d3d_write_ptr)->dwArg = 6;
			g_d3d_write_ptr += sizeof(D3DSTATE);
			((D3DSTATE *)g_d3d_write_ptr)->dwState =
				D3DRENDERSTATE_TEXTUREMAPBLEND;
			if ((flags & STD3D_RS_TEXTURE_MODULATE_ALPHA) != 0) {
				((D3DSTATE *)g_d3d_write_ptr)->dwArg = 4;
			} else {
				((D3DSTATE *)g_d3d_write_ptr)->dwArg = 2;
			}
			g_d3d_write_ptr += sizeof(D3DSTATE);
			((D3DSTATE *)g_d3d_write_ptr)->dwState =
				D3DRENDERSTATE_BLENDENABLE;
			((D3DSTATE *)g_d3d_write_ptr)->dwArg = 1;
		} else {
			g_d3d_write_ptr += sizeof(D3DINSTRUCTION);
			((D3DSTATE *)g_d3d_write_ptr)->dwState =
				D3DRENDERSTATE_SRCBLEND;
			((D3DSTATE *)g_d3d_write_ptr)->dwArg = 2;
			g_d3d_write_ptr += sizeof(D3DSTATE);
			((D3DSTATE *)g_d3d_write_ptr)->dwState =
				D3DRENDERSTATE_DESTBLEND;
			((D3DSTATE *)g_d3d_write_ptr)->dwArg = 1;
			g_d3d_write_ptr += sizeof(D3DSTATE);
			((D3DSTATE *)g_d3d_write_ptr)->dwState =
				D3DRENDERSTATE_TEXTUREMAPBLEND;
			if ((flags & STD3D_RS_TEXTURE_MODULATE_ALPHA) != 0) {
				((D3DSTATE *)g_d3d_write_ptr)->dwArg = 4;
			} else {
				((D3DSTATE *)g_d3d_write_ptr)->dwArg = 2;
			}
			g_d3d_write_ptr += sizeof(D3DSTATE);
			((D3DSTATE *)g_d3d_write_ptr)->dwState =
				D3DRENDERSTATE_BLENDENABLE;
			((D3DSTATE *)g_d3d_write_ptr)->dwArg = 0;
		}
		g_d3d_write_ptr += sizeof(D3DSTATE);
	}

	if (((unsigned int)(flags ^ g_d3d_state_flags) &
	     (STD3D_RS_Z_COMPARE_ENABLE | STD3D_RS_Z_WRITE_ENABLE)) != 0) {
		((D3DINSTRUCTION *)g_d3d_write_ptr)->bOpcode =
			D3DOP_STATERENDER;
		((D3DINSTRUCTION *)g_d3d_write_ptr)->bSize = sizeof(D3DSTATE);
		((D3DINSTRUCTION *)g_d3d_write_ptr)->wCount = 2;
		g_d3d_write_ptr += sizeof(D3DINSTRUCTION);
		((D3DSTATE *)g_d3d_write_ptr)->dwState = D3DRENDERSTATE_ZFUNC;
		if ((flags & STD3D_RS_Z_COMPARE_ENABLE) != 0) {
			((D3DSTATE *)g_d3d_write_ptr)->dwArg =
				std3d_map_z_cmp_func(g_std3dz_compare_cap);
		} else {
			((D3DSTATE *)g_d3d_write_ptr)->dwArg = 8;
		}
		g_d3d_write_ptr += sizeof(D3DSTATE);
		((D3DSTATE *)g_d3d_write_ptr)->dwState =
			D3DRENDERSTATE_ZWRITEENABLE;
		if ((flags & STD3D_RS_Z_WRITE_ENABLE) != 0) {
			((D3DSTATE *)g_d3d_write_ptr)->dwArg = 1;
		} else {
			((D3DSTATE *)g_d3d_write_ptr)->dwArg = 0;
		}
		g_d3d_write_ptr += sizeof(D3DSTATE);
	}

	if (((unsigned int)(flags ^ g_d3d_state_flags) &
	     (STD3D_RS_TEXTURE_MAG_LINEAR | STD3D_RS_TEXTURE_MIN_LINEAR)) !=
	    0) {
		((D3DINSTRUCTION *)g_d3d_write_ptr)->bOpcode =
			D3DOP_STATERENDER;
		((D3DINSTRUCTION *)g_d3d_write_ptr)->bSize = sizeof(D3DSTATE);
		((D3DINSTRUCTION *)g_d3d_write_ptr)->wCount = 2;
		g_d3d_write_ptr += sizeof(D3DINSTRUCTION);
		((D3DSTATE *)g_d3d_write_ptr)->dwState =
			D3DRENDERSTATE_TEXTUREMAG;
		int texture_filter =
			(flags & STD3D_RS_TEXTURE_MAG_LINEAR) != 0 ? 2 : 1;
		((D3DSTATE *)g_d3d_write_ptr)->dwArg = texture_filter;
		g_d3d_write_ptr += sizeof(D3DSTATE);
		((D3DSTATE *)g_d3d_write_ptr)->dwState =
			D3DRENDERSTATE_TEXTUREMIN;
		texture_filter =
			(flags & STD3D_RS_TEXTURE_MIN_LINEAR) != 0 ? 2 : 1;
		((D3DSTATE *)g_d3d_write_ptr)->dwArg = texture_filter;
		g_d3d_write_ptr += sizeof(D3DSTATE);
	}

	if (((unsigned int)(flags ^ g_d3d_state_flags) & STD3D_RS_FOG_ENABLE) !=
	    0) {
		((D3DINSTRUCTION *)g_d3d_write_ptr)->bOpcode =
			D3DOP_STATERENDER;
		((D3DINSTRUCTION *)g_d3d_write_ptr)->bSize = sizeof(D3DSTATE);
		if ((flags & STD3D_RS_FOG_ENABLE) != 0) {
			((D3DINSTRUCTION *)g_d3d_write_ptr)->wCount = 5;
			g_d3d_write_ptr += sizeof(D3DINSTRUCTION);
			((D3DSTATE *)g_d3d_write_ptr)->dwState =
				D3DRENDERSTATE_FOGENABLE;
			((D3DSTATE *)g_d3d_write_ptr)->dwArg = 1;
			g_d3d_write_ptr += sizeof(D3DSTATE);
			((D3DSTATE *)g_d3d_write_ptr)->dwState =
				D3DRENDERSTATE_FOGCOLOR;
			((D3DSTATE *)g_d3d_write_ptr)->dwArg =
				(g_std3d_fog_color_green8 << 8) |
				(g_std3d_fog_color_red8 << 16) |
				g_std3d_fog_color_blue8;
			g_d3d_write_ptr += sizeof(D3DSTATE);
			((D3DSTATE *)g_d3d_write_ptr)->dwState =
				D3DRENDERSTATE_FOGTABLEMODE;
			((D3DSTATE *)g_d3d_write_ptr)->dwArg = 3;
			g_d3d_write_ptr += sizeof(D3DSTATE);
			((D3DSTATE *)g_d3d_write_ptr)->dwState =
				D3DRENDERSTATE_FOGTABLESTART;
			((D3DSTATE *)g_d3d_write_ptr)->dwArg =
				g_std3d_fog_table_start_bits;
			g_d3d_write_ptr += sizeof(D3DSTATE);
			((D3DSTATE *)g_d3d_write_ptr)->dwState =
				D3DRENDERSTATE_FOGTABLEEND;
			((D3DSTATE *)g_d3d_write_ptr)->dwArg =
				g_std3d_fog_table_end_bits;
			g_d3d_write_ptr += sizeof(D3DSTATE);
		} else {
			((D3DINSTRUCTION *)g_d3d_write_ptr)->wCount = 1;
			g_d3d_write_ptr += sizeof(D3DINSTRUCTION);
			((D3DSTATE *)g_d3d_write_ptr)->dwState =
				D3DRENDERSTATE_FOGENABLE;
			((D3DSTATE *)g_d3d_write_ptr)->dwArg = 0;
			g_d3d_write_ptr += sizeof(D3DSTATE);
		}
	}

	g_d3d_state_flags = flags;
}

/* Copies 768 bytes of RGB palette and builds from it the opaque palette when
 * that format is 16-bit RGB, and, with alpha textures, the color-keyed 1555
 * palette and, without alpha blending, the 4444 palette with alpha. Returns 1.
 * Nothing calls this. */
// FUNCTION: XVT 0x4B2540
int std3d_set_palette_conversion_source(const void *palette_rgb888,
					uint8_t alpha)
{
	memcpy(g_std3d_palette_conversion_source_rgb, palette_rgb888,
	       sizeof(g_std3d_palette_conversion_source_rgb));
	if (g_p_fmt_opaque_texture->color_info.color_mode == STDCOLOR_RGB &&
	    g_p_fmt_opaque_texture->color_info.bpp == 16) {
		std3d_build_colormap_opaque(
			(uint8_t *)palette_rgb888, g_std3d_palette_scratch16,
			&g_p_fmt_opaque_texture->color_info);
	}
	if (g_p_std3d_cur_device->caps.b_alpha_texture != 0) {
		if (g_p_fmt_rgba1555->color_info.color_mode == STDCOLOR_RGBA &&
		    g_p_fmt_rgba1555->color_info.bpp == 16) {
			std3d_build_colormap_color_key(
				(uint8_t *)palette_rgb888, g_tex_conv_buf1555,
				&g_p_fmt_rgba1555->color_info);
		}
		if (g_p_std3d_cur_device->caps.b_alpha_blend == 0 &&
		    g_p_fmt_rgba4444->color_info.color_mode == STDCOLOR_RGBA &&
		    g_p_fmt_rgba4444->color_info.bpp == 16) {
			std3d_build_colormap_alpha(
				(uint8_t *)palette_rgb888, g_tex_conv_buf4444,
				&g_p_fmt_rgba4444->color_info, alpha);
		}
	}
	return 1;
}

/* Writes the size a texture of srcWidth by srcHeight would get: each side at
 * least 1 and at most g_std3d_max_texture_width; then, when under the minimums or
 * not square on a square-only device, both sides made the larger, or each
 * raised to its minimum. With the limits at 0, as they stay, it writes 0 by 0.
 * Nothing calls this. */
// FUNCTION: XVT 0x4B25E0
void std3d_clamp_texture_dimensions(int src_width, int src_height,
				    int *out_width, int *out_height)
{
	unsigned int width;

	if ((uint32_t)src_width >= 1) {
		width = (uint32_t)g_std3d_max_texture_width;
		if (width >= (uint32_t)src_width) {
			width = (uint32_t)src_width;
		}
	} else {
		width = 1;
	}

	unsigned int height;
	if ((uint32_t)src_height >= 1) {
		/* The height is clamped by the width limit, as in the original;
		 * g_std3d_max_texture_height is never read. */
		height = (uint32_t)g_std3d_max_texture_width;
		if (height >= (uint32_t)src_height) {
			height = (uint32_t)src_height;
		}
	} else {
		height = 1;
	}

	if (width < (unsigned int)g_std3d_min_texture_width ||
	    height < (unsigned int)g_std3d_min_texture_height ||
	    (g_p_std3d_cur_device->caps.b_square_only_texture &&
	     width != height)) {
		if (g_p_std3d_cur_device->caps.b_square_only_texture &&
		    width != height) {
			if (height <= width) {
				height = width;
			}
			width = height;
		} else {
			unsigned int min_width =
				(unsigned int)g_std3d_min_texture_width;
			if (width <= min_width) {
				width = min_width;
			}
			if (height <= (uint32_t)g_std3d_min_texture_height) {
				height = (uint32_t)g_std3d_min_texture_height;
			}
		}
		*out_width = (int)width;
		*out_height = (int)height;
		return;
	}

	*out_width = (int)width;
	*out_height = (int)height;
}

/* Uploads source as a texture for node and puts the node at the most recently
 * used end of the cache list. Returns 1, or 0 with node cleared after any
 * failure. The size is the source's, cut to 256 on each side; a texture under
 * the minimum size, or not square on a square-only device, is first tiled into
 * a temporary buffer by whole copies, their number rounded to nearest. The
 * format is the 1555 one for a color-keyed texture on a device with alpha
 * textures, the 4444 one for a translucent texture, else the opaque one. It
 * fills a system-memory surface (8-bit texels through g_tex_conv_buf4444,
 * g_tex_conv_buf1555 or g_std3d_palette_scratch16; 16-bit rows copied as they are),
 * sets a color key for a color-keyed texture on a device without alpha textures
 * (palette entry 0, or the buffer's transparent_color), then creates the texture
 * in video memory and loads it. When video memory runs out it releases cached
 * textures from the least recently used end, stopping at the first used in the
 * current batch, until as many texels are freed, and tries again; when that
 * frees too few it fails. Sets the node's handle, size, texel count and batch
 * tag. */
// FUNCTION: XVT 0x4B2680
int std3d_add_to_texture_cache(struct std3dv_buffer *source,
			       struct std3d_tex_cache_node *node,
			       int color_keyed, int translucent)
{
	IDirectDrawSurface *source_surface = NULL;
	IDirectDrawSurface *destination_surface = NULL;
	IDirect3DTexture *source_texture = NULL;
	IDirect3DTexture *destination_texture = NULL;
	struct std3dv_buffer *upload_buffer = source;
	struct std3dv_buffer *temporary_buffer = NULL;
	unsigned int width = source->raster.width;
	if (width >= 1) {
		if (width >= 256) {
			width = 256;
		}
	} else {
		width = 1;
	}
	unsigned int height = source->raster.height;
	if (height >= 1) {
		if (height >= 256) {
			height = 256;
		}
	} else {
		height = 1;
	}

	if (width < (unsigned int)g_std3d_min_texture_width ||
	    height < (unsigned int)g_std3d_min_texture_height ||
	    (g_p_std3d_cur_device->caps.b_square_only_texture &&
	     width != height)) {
		struct std3d_raster_info resized_raster = source->raster;
		int target_width;
		int target_height;
		if (g_p_std3d_cur_device->caps.b_square_only_texture &&
		    width != height) {
			target_width = (int)width;
			if ((unsigned int)target_width <= height) {
				target_width = (int)height;
			}
			target_height = target_width;
		} else {
			target_width = (int)width;
			if ((unsigned int)target_width <=
			    (unsigned int)g_std3d_min_texture_width) {
				target_width = g_std3d_min_texture_width;
			}
			target_height = g_std3d_min_texture_height;
			if ((unsigned int)target_height <= height) {
				target_height = (int)height;
			}
		}
		unsigned int horizontal_copies =
			(unsigned int)((double)(unsigned int)target_width /
					       (double)width +
				       0.5);
		unsigned int vertical_copies =
			(unsigned int)((double)(unsigned int)target_height /
					       (double)height +
				       0.5);
		resized_raster.width = horizontal_copies * resized_raster.width;
		resized_raster.height = vertical_copies * resized_raster.height;
		temporary_buffer =
			std3d_alloc_v_buffer(&resized_raster, 0, 0, 0);
		unsigned int destination_y = 0;
		while (vertical_copies != 0) {
			unsigned int destination_x = 0;
			/* From here targetWidth counts down the copies left in
			 * this row, not a width. */
			for (target_width = horizontal_copies;
			     target_width != 0; --target_width) {
				std3d_blit_v_buffer(temporary_buffer, source,
						    (int)destination_x,
						    (int)destination_y, 0, 1);
				destination_x += width;
			}
			destination_y += height;
			--vertical_copies;
		}
		upload_buffer = temporary_buffer;
		width = resized_raster.width;
		height = resized_raster.height;
	}

	unsigned int texel_count = width * height;
	DDSURFACEDESC surface_desc;
	if (color_keyed && g_p_std3d_cur_device->caps.b_alpha_texture) {
		node->uses_alpha_format = 1;
		if (translucent) {
			surface_desc = g_p_fmt_rgba4444->ddsd;
#ifdef XVT_MODERN
			XVT_LOG_DEBUG(
				"d3d.texture_format format=%d kind=\"rgba4444\"",
				g_fmt_idx_rgba4444);
#else
			debug_printf("Using D3D texture format #%d.\n",
				     g_fmt_idx_rgba4444, 0, 0, 0);
#endif
		} else {
			surface_desc = g_p_fmt_rgba1555->ddsd;
#ifdef XVT_MODERN
			XVT_LOG_DEBUG(
				"d3d.texture_format format=%d kind=\"rgba1555\"",
				g_fmt_idx_rgba1555);
#else
			debug_printf("Using D3D texture format #%d.\n",
				     g_fmt_idx_rgba1555, 0, 0, 0);
#endif
		}
	} else if (translucent) {
		node->uses_alpha_format = 1;
		surface_desc = g_p_fmt_rgba4444->ddsd;
#ifdef XVT_MODERN
		XVT_LOG_DEBUG("d3d.texture_format format=%d kind=\"rgba4444\"",
			      g_fmt_idx_rgba4444);
#else
		debug_printf("Using D3D texture format #%d.\n",
			     g_fmt_idx_rgba4444, 0, 0, 0);
#endif
	} else {
		node->uses_alpha_format = 0;
		surface_desc = g_p_fmt_opaque_texture->ddsd;
#ifdef XVT_MODERN
		XVT_LOG_DEBUG("d3d.texture_format format=%d kind=\"opaque\"",
			      g_fmt_idx_opaque_texture);
#else
		debug_printf("Using D3D texture format #%d.\n",
			     g_fmt_idx_opaque_texture, 0, 0, 0);
#endif
	}
	surface_desc.dwWidth = width;
	surface_desc.dwHeight = height;
	surface_desc.dwSize = sizeof(surface_desc);
	surface_desc.dwFlags =
		DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT;
	surface_desc.ddsCaps.dwCaps = DDSCAPS_TEXTURE | DDSCAPS_SYSTEMMEMORY;
	unsigned int texture_handle;
	DDCOLORKEY color_key;
	DDSURFACEDESC locked_desc;
	int result;
	do {
		result = g_std3d_direct_draw->lpVtbl->CreateSurface(
			g_std3d_direct_draw, &surface_desc, &source_surface,
			NULL);
		if (result) {
#ifdef XVT_MODERN
			XVT_LOG_ERROR(
				"d3d.call_failed step=\"create_source_surface\" error=\"%s\"",
				std3d_lookup_error_string(
					result, g_std3d_error_string_table,
					121));
#else
			debug_printf(
				"Error %s when creating the DirectDraw source surface.\n",
				std3d_lookup_error_string(
					result, g_std3d_error_string_table,
					121),
				0, 0, 0);
#endif
			source_surface = NULL;
			break;
		}

		memset(&locked_desc, 0, sizeof(locked_desc));
		locked_desc.dwSize = sizeof(locked_desc);
		result = source_surface->lpVtbl->Lock(
			source_surface, NULL, &locked_desc, DDLOCK_WAIT, NULL);
		if (result) {
#ifdef XVT_MODERN
			XVT_LOG_ERROR(
				"d3d.call_failed step=\"lock_source_surface\" error=\"%s\"",
				std3d_lookup_error_string(
					result, g_std3d_error_string_table,
					121));
#else
			debug_printf(
				"Error %s when locking the DDSurface source buffer.\n",
				std3d_lookup_error_string(
					result, g_std3d_error_string_table,
					121),
				0, 0, 0);
#endif
			break;
		}

		switch (upload_buffer->raster.color_mode) {
		case STDCOLOR_PAL: {
			std3d_lock_v_buffer(upload_buffer);
			unsigned int row;
			if (color_keyed &&
			    g_p_std3d_cur_device->caps.b_alpha_texture) {
				if (translucent) {
					for (row = 0; row < height; ++row) {
						uint8_t *source_pixels =
							(uint8_t *)upload_buffer
								->pixels +
							row * upload_buffer
									->raster
									.row_pitch;
						uint16_t *destination_pixels =
							(uint16_t
								 *)((uint8_t *)locked_desc
									    .lpSurface +
								    row * locked_desc
										    .lPitch);
						if (width != 0) {
							unsigned int remaining =
								width;
							do {
								*destination_pixels++ = g_tex_conv_buf4444
									[*source_pixels++];
								--remaining;
							} while (remaining !=
								 0);
						}
					}
				} else {
					for (row = 0; row < height; ++row) {
						uint8_t *source_pixels =
							(uint8_t *)upload_buffer
								->pixels +
							row * upload_buffer
									->raster
									.row_pitch;
						uint16_t *destination_pixels =
							(uint16_t
								 *)((uint8_t *)locked_desc
									    .lpSurface +
								    row * locked_desc
										    .lPitch);
						if (width != 0) {
							unsigned int remaining =
								width;
							do {
								*destination_pixels++ = g_tex_conv_buf1555
									[*source_pixels++];
								--remaining;
							} while (remaining !=
								 0);
						}
					}
				}
			} else if (translucent) {
				for (row = 0; row < height; ++row) {
					uint8_t *source_pixels =
						(uint8_t *)
							upload_buffer->pixels +
						row * upload_buffer->raster
								.row_pitch;
					uint16_t *destination_pixels =
						(uint16_t
							 *)((uint8_t *)locked_desc
								    .lpSurface +
							    row * locked_desc
									    .lPitch);
					if (width != 0) {
						unsigned int remaining = width;
						do {
							*destination_pixels++ = g_tex_conv_buf4444
								[*source_pixels++];
							--remaining;
						} while (remaining != 0);
					}
				}
			} else {
				for (row = 0; row < height; ++row) {
					uint8_t *source_pixels =
						(uint8_t *)
							upload_buffer->pixels +
						row * upload_buffer->raster
								.row_pitch;
					uint16_t *destination_pixels =
						(uint16_t
							 *)((uint8_t *)locked_desc
								    .lpSurface +
							    row * locked_desc
									    .lPitch);
					if (width != 0) {
						unsigned int remaining = width;
						do {
							*destination_pixels++ = g_std3d_palette_scratch16
								[*source_pixels++];
							--remaining;
						} while (remaining != 0);
					}
				}
			}
			std3d_unlock_v_buffer(upload_buffer);
			break;
		}
		case STDCOLOR_RGB:
		case STDCOLOR_RGBA: {
			std3d_lock_v_buffer(upload_buffer);
			for (unsigned int row = 0; row < height; ++row) {
				const uint8_t *source_pixels =
					(const uint8_t *)upload_buffer->pixels +
					row * upload_buffer->raster.row_pitch;
				uint8_t *destination_pixels =
					(uint8_t *)locked_desc.lpSurface +
					row * locked_desc.lPitch;
				memcpy(destination_pixels, source_pixels,
				       width * 2);
			}
			std3d_unlock_v_buffer(upload_buffer);
			break;
		}
		default:
			break;
		}

		result = source_surface->lpVtbl->Unlock(source_surface, NULL);
		if (result) {
#ifdef XVT_MODERN
			XVT_LOG_ERROR(
				"d3d.call_failed step=\"unlock_source_surface\" error=\"%s\"",
				std3d_lookup_error_string(
					result, g_std3d_error_string_table,
					121));
#else
			debug_printf(
				"Error %s when unlocking the DDSurface source buffer.\n",
				std3d_lookup_error_string(
					result, g_std3d_error_string_table,
					121),
				0, 0, 0);
#endif
			break;
		}

		if (color_keyed &&
		    !g_p_std3d_cur_device->caps.b_alpha_texture) {
			switch (upload_buffer->raster.color_mode) {
			case STDCOLOR_PAL:
				color_key.dwColorSpaceLowValue =
					g_std3d_palette_scratch16[0];
				color_key.dwColorSpaceHighValue =
					g_std3d_palette_scratch16[0];
				break;
			case STDCOLOR_RGB:
				color_key.dwColorSpaceLowValue =
					upload_buffer->transparent_color;
				color_key.dwColorSpaceHighValue =
					upload_buffer->transparent_color;
				break;
#ifdef XVT_MODERN
			default:
				color_key.dwColorSpaceLowValue = 0;
				color_key.dwColorSpaceHighValue = 0;
				break;
#endif
			}
			(void)source_surface->lpVtbl->SetColorKey(
				source_surface, DDCKEY_SRCBLT, &color_key);
		}

		result = source_surface->lpVtbl->QueryInterface(
			source_surface, &CLSID_IDirect3DTexture,
			(void **)&source_texture);
		if (result) {
#ifdef XVT_MODERN
			XVT_LOG_ERROR(
				"d3d.call_failed step=\"create_source_texture\" error=\"%s\"",
				std3d_lookup_error_string(
					result, g_std3d_error_string_table,
					121));
#else
			debug_printf(
				"Error %s creating Direct3D source texture.\n",
				std3d_lookup_error_string(
					result, g_std3d_error_string_table,
					121),
				0, 0, 0);
#endif
			source_texture = NULL;
			break;
		}
		result = source_surface->lpVtbl->GetSurfaceDesc(source_surface,
								&surface_desc);
		if (result) {
#ifdef XVT_MODERN
			XVT_LOG_ERROR(
				"d3d.call_failed step=\"get_surface_description\" error=\"%s\"",
				std3d_lookup_error_string(
					result, g_std3d_error_string_table,
					121));
#else
			debug_printf("Error %s get surface description.\n",
				     std3d_lookup_error_string(
					     result, g_std3d_error_string_table,
					     121),
				     0, 0, 0);
#endif
#ifndef XVT_MODERN
			source_texture = NULL;
#endif
			break;
		}

		node->ddsd = surface_desc;
		surface_desc.dwFlags =
			DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT;
		surface_desc.ddsCaps.dwCaps = DDSCAPS_TEXTURE |
					      DDSCAPS_VIDEOMEMORY |
					      DDSCAPS_ALLOCONLOAD;
		result = g_std3d_direct_draw->lpVtbl->CreateSurface(
			g_std3d_direct_draw, &surface_desc,
			&destination_surface, NULL);
		if (result) {
			if (result != -2005532292) {
#ifdef XVT_MODERN
				XVT_LOG_ERROR(
					"d3d.call_failed step=\"create_texture_surface\" error=\"%s\"",
					std3d_lookup_error_string(
						result,
						g_std3d_error_string_table,
						121));
#else
				debug_printf(
					"Error %s creating texture surface.\n",
					std3d_lookup_error_string(
						result,
						g_std3d_error_string_table,
						121),
					0, 0, 0);
#endif
				break;
			}
#ifdef XVT_MODERN
			XVT_LOG_ERROR(
				"d3d.call_failed step=\"create_surface\" error=\"%s\"",
				std3d_lookup_error_string(
					result, g_std3d_error_string_table,
					121));
#else
			debug_printf("Error %s Creating surface.\n",
				     std3d_lookup_error_string(
					     result, g_std3d_error_string_table,
					     121),
				     0, 0, 0);
#endif
#ifdef XVT_MODERN
			XVT_LOG_WARN("d3d.texture_purge");
#else
			debug_printf(
				"Assuming texture ram overflow - PURGING.\n", 0,
				0, 0, 0);
#endif
			{
				int created = 0;
				struct std3d_tex_cache_node *candidate =
					g_p_tex_cache_head;

				while (!created) {
					unsigned int freed = 0;

					while (freed < texel_count &&
					       candidate != NULL &&
					       candidate->cache_batch_tag !=
						       g_std3d_texture_batch_tag) {
						candidate->p_cached_surface
							->lpVtbl->Release(
								candidate
									->p_cached_surface);
						candidate->p_cached_texture
							->lpVtbl->Release(
								candidate
									->p_cached_texture);
						candidate->b_cached = 0;
						freed += candidate->texel_count;
						std3d_cache_list_remove(
							candidate);
						candidate = candidate->p_next;
					}
					if (freed < texel_count) {
#ifdef XVT_MODERN
						XVT_LOG_WARN(
							"d3d.scene_texture_overflow");
#else
						debug_printf(
							"WARNING: Scene texture overflow occurred!!!.\n",
							0, 0, 0, 0);
#endif
						destination_surface = NULL;
						break;
					}
					result =
						g_std3d_direct_draw->lpVtbl
							->CreateSurface(
								g_std3d_direct_draw,
								&surface_desc,
								&destination_surface,
								NULL);
					if (!result) {
						created = 1;
#ifdef XVT_MODERN
						XVT_LOG_DEBUG(
							"d3d.texture_purge_recovered");
#else
						debug_printf(
							"Success adding new texture after purge.\n",
							0, 0, 0, 0);
#endif
					} else if (result != -2005532292) {
#ifdef XVT_MODERN
						XVT_LOG_ERROR(
							"d3d.call_failed step=\"create_texture_surface\" error=\"%s\"",
							std3d_lookup_error_string(
								result,
								g_std3d_error_string_table,
								121));
#else
						debug_printf(
							"Error %s creating texture surface.\n",
							std3d_lookup_error_string(
								result,
								g_std3d_error_string_table,
								121),
							0, 0, 0);
#endif
						destination_surface = NULL;
						break;
					}
				}
			}
			if (destination_surface == NULL) {
				break;
			}
		}

		result = destination_surface->lpVtbl->QueryInterface(
			destination_surface, &CLSID_IDirect3DTexture,
			(void **)&destination_texture);
		if (result) {
#ifdef XVT_MODERN
			XVT_LOG_ERROR(
				"d3d.call_failed step=\"create_dest_texture\" error=\"%s\"",
				std3d_lookup_error_string(
					result, g_std3d_error_string_table,
					121));
#else
			debug_printf(
				"Error %s creating Direct3D dest texture.\n",
				std3d_lookup_error_string(
					result, g_std3d_error_string_table,
					121),
				0, 0, 0);
#endif
			destination_texture = NULL;
			break;
		}
		result = destination_texture->lpVtbl->Load(destination_texture,
							   source_texture);
		if (result) {
#ifdef XVT_MODERN
			XVT_LOG_ERROR(
				"d3d.call_failed step=\"load_dest_texture\" error=\"%s\"",
				std3d_lookup_error_string(
					result, g_std3d_error_string_table,
					121));
#else
			debug_printf(
				"Error %s loading Direct3D dest texture from source.\n",
				std3d_lookup_error_string(
					result, g_std3d_error_string_table,
					121),
				0, 0, 0);
#endif
			break;
		}
		result = destination_texture->lpVtbl->GetHandle(
			destination_texture, g_d3d_device, &texture_handle);
		if (result) {
#ifdef XVT_MODERN
			XVT_LOG_ERROR(
				"d3d.call_failed step=\"get_texture_handle\" error=\"%s\"",
				std3d_lookup_error_string(
					result, g_std3d_error_string_table,
					121));
#else
			debug_printf("Error %s when getting texture handle.\n",
				     std3d_lookup_error_string(
					     result, g_std3d_error_string_table,
					     121),
				     0, 0, 0);
#endif
			texture_handle = 0;
		}

		source_texture->lpVtbl->Release(source_texture);
		source_texture = NULL;
		source_surface->lpVtbl->Release(source_surface);
		source_surface = NULL;
		if (temporary_buffer != NULL) {
			std3d_free_v_buffer(temporary_buffer);
		}
		node->p_cached_texture = destination_texture;
		node->p_cached_surface = destination_surface;
		node->tex_handle = texture_handle;
		node->width = width;
		node->height = height;
		node->b_cached = 1;
		node->texel_count = texel_count;
		node->cache_batch_tag = g_std3d_texture_batch_tag;
		std3d_cache_list_append(node);
		return 1;
	} while (0);

	if (source_surface != NULL) {
		source_surface->lpVtbl->Release(source_surface);
	}
	if (source_texture != NULL) {
		source_texture->lpVtbl->Release(source_texture);
	}
	if (temporary_buffer != NULL) {
		std3d_free_v_buffer(temporary_buffer);
	}
	if (destination_surface != NULL) {
		destination_surface->lpVtbl->Release(destination_surface);
	}
	if (destination_texture != NULL) {
		destination_texture->lpVtbl->Release(destination_texture);
	}
	node->p_cached_texture = NULL;
	node->p_cached_surface = NULL;
	node->tex_handle = 0;
	node->b_cached = 0;
	node->cache_batch_tag = 0;
#ifdef XVT_MODERN
	XVT_LOG_ERROR("d3d.failed reason=\"texture_add\"");
#else
	debug_printf("Done error exit from std3D_AddToTextureCache.\n", 0, 0, 0,
		     0);
#endif
	return 0;
}

/* Releases every cached texture and unlinks the list; sets g_tex_cache_count to 0
 * and g_std3d_texture_batch_tag to 1, and sets the device's available_memory back
 * to its total_memory. */
// FUNCTION: XVT 0x4B3070
void std3d_flush_texture_cache(void)
{
	struct std3d_tex_cache_node *node = g_p_tex_cache_head;
	while (node != NULL) {
		if (node->p_cached_surface != NULL) {
			node->p_cached_surface->lpVtbl->Release(
				node->p_cached_surface);
			node->p_cached_surface = NULL;
		}
		if (node->p_cached_texture != NULL) {
			node->p_cached_texture->lpVtbl->Release(
				node->p_cached_texture);
			node->p_cached_texture = NULL;
		}
		struct std3d_tex_cache_node *current = node;
		struct std3d_tex_cache_node **next_link = &node->p_next;
		node->b_cached = 0;
		node->cache_batch_tag = 0;
		node = *next_link;
		*next_link = NULL;
		current->p_prev = NULL;
	}

	g_p_tex_cache_head = NULL;
	g_p_tex_cache_tail = NULL;
	g_tex_cache_count = 0;
	g_p_std3d_cur_device->available_memory =
		g_p_std3d_cur_device->total_memory;
	g_std3d_texture_batch_tag = 1;
}

/* Puts node at the most recently used end of the cache list, raises
 * g_tex_cache_count, and takes the node's texel_count from available_memory. */
// FUNCTION: XVT 0x4B30F0
void std3d_cache_list_append(struct std3d_tex_cache_node *node)
{
	if (g_p_tex_cache_head == 0) {
		g_p_tex_cache_tail = node;
		g_p_tex_cache_head = node;
		node->p_prev = 0;
		node->p_next = 0;
	} else {
		g_p_tex_cache_tail->p_next = node;
		struct std3d_tex_cache_node *previous_tail = g_p_tex_cache_tail;
		node->p_next = 0;
		node->p_prev = previous_tail;
		g_p_tex_cache_tail = node;
	}

	++g_tex_cache_count;
	g_p_std3d_cur_device->available_memory -= node->texel_count;
}

/* Takes node off the cache list, lowers g_tex_cache_count, and gives its
 * texel_count back to available_memory. Does not check that node is on the
 * list. */
// FUNCTION: XVT 0x4B3160
void std3d_cache_list_remove(const struct std3d_tex_cache_node *node)
{
	if (node == g_p_tex_cache_head) {
		g_p_tex_cache_head = node->p_next;
		if (g_p_tex_cache_head != NULL) {
			g_p_tex_cache_head->p_prev = NULL;
			if (g_p_tex_cache_head->p_next == NULL) {
				g_p_tex_cache_tail = g_p_tex_cache_head;
			}
		} else {
			g_p_tex_cache_tail = NULL;
		}
	} else if (node == g_p_tex_cache_tail) {
		g_p_tex_cache_tail = node->p_prev;
		g_p_tex_cache_tail->p_next = NULL;
	} else {
		node->p_prev->p_next = node->p_next;
		node->p_next->p_prev = node->p_prev;
	}

	--g_tex_cache_count;
	g_p_std3d_cur_device->available_memory += node->texel_count;
}

/* Asks DirectDraw for the total and free texture video memory in bytes. Returns
 * 1, or 0 when it cannot; when only the memory query fails, the IDirectDraw2
 * interface it got is not released. */
// FUNCTION: XVT 0x4B3210
int std3d_query_texture_vid_mem(unsigned int *total_bytes,
				unsigned int *free_bytes)
{
	IDirectDraw *direct_draw2 = 0;

	if (g_std3d_direct_draw->lpVtbl->QueryInterface(
		    g_std3d_direct_draw, &CLSID_IDirectDraw2,
		    (void **)&direct_draw2) != DX_DD_OK) {
		return 0;
	}

	DDSCAPS caps;
	caps.dwCaps = DDSCAPS_TEXTURE;
	if (direct_draw2->lpVtbl->GetAvailableVidMem(
		    direct_draw2, &caps, total_bytes, free_bytes) != DX_DD_OK) {
		return 0;
	}

	direct_draw2->lpVtbl->Release(direct_draw2);
	return 1;
}

/* Marks node as used in the current batch and moves it to the most recently
 * used end of the cache list. */
// FUNCTION: XVT 0x4B3280
void std3d_cache_texture_surface(struct std3d_tex_cache_node *node)
{
	node->cache_batch_tag = g_std3d_texture_batch_tag;
	std3d_cache_list_remove(node);
	std3d_cache_list_append(node);
}

/* Fills g_std3d_quad_rect of the z-buffer with 0 when g_std3dz_compare_cap is 16,
 * else 0xFFFF, restoring a lost surface and trying again. Returns 1, or 0 when
 * the fill or the restore fails. */
// FUNCTION: XVT 0x4B32B0
int std3d_clear_z_buffer(void)
{
	DDBLTFX effects;

	memset(&effects, 0, sizeof(effects));
	effects.dwSize = sizeof(effects);
	effects.dwFillDepth = 0;
	if (g_std3dz_compare_cap != 16) {
		effects.dwFillDepth = 0xFFFF;
	}
	int rect[4];
	rect[0] = g_std3d_quad_rect.x;
	rect[1] = g_std3d_quad_rect.y;
	rect[2] = g_std3d_quad_rect.x + g_std3d_quad_rect.width;
	rect[3] = g_std3d_quad_rect.y + g_std3d_quad_rect.height;

	for (;;) {
		int result = g_std3dz_buffer_surface_block.surface->lpVtbl->Blt(
			g_std3dz_buffer_surface_block.surface, rect, NULL, NULL,
			DDBLT_WAIT | DDBLT_DEPTHFILL, &effects);
		if (result == DX_DD_OK) {
			return 1;
		}
		if (result == DX_DDERR_SURFACELOST) {
			result = g_std3dz_buffer_surface_block.surface->lpVtbl
					 ->Restore(g_std3dz_buffer_surface_block
							   .surface);
		}
		if (result != DX_DD_OK) {
#ifdef XVT_MODERN
			XVT_LOG_ERROR(
				"d3d.call_failed step=\"clear_z_buffer\" error=\"%s\"",
				std3d_lookup_error_string(
					result, g_std3d_error_string_table,
					121));
#else
			debug_printf("Error %s clearing zbuffer.\n",
				     std3d_lookup_error_string(
					     result, g_std3d_error_string_table,
					     121),
				     0, 0, 0);
#endif
			return 0;
		}
	}
}

/* Returns the index of the first device that matches required_caps on
 * perspective texturing and z-buffer (each only when required), shares a color
 * model with it and has the same bHardware. Without one, returns the first
 * device that matches the most of those, in that order. Returns 0 when there is
 * no device. */
// FUNCTION: XVT 0x4B3380
int std3d_select_best_device(const struct std3d_device_caps *required_caps)
{
	if (g_std3d_num_devices == 0) {
		return 0;
	}
	int best_match_quality = 0;
	struct std3d_device *device = g_std3d_devices;
	int device_index = 0;
	int best_device_index = 0;
	if (g_std3d_num_devices > (unsigned int)device_index) {
		int required_perspective = required_caps->b_texture_perspective;
		do {
			int match_quality = 0;
			if (required_perspective == 0 ||
			    device->caps.b_texture_perspective ==
				    required_perspective) {
				match_quality = 1;
				int required_z_buffer =
					required_caps->b_has_z_buffer;
				if (required_z_buffer == 0 ||
				    device->caps.b_has_z_buffer ==
					    required_z_buffer) {
					match_quality = 2;
					if ((required_caps->color_model_flags &
					     device->caps.color_model_flags) !=
					    0) {
						match_quality = 3;
						if (device->caps.b_hardware ==
						    required_caps->b_hardware) {
#ifdef XVT_MODERN
							XVT_LOG_DEBUG(
								"d3d.device_chosen device=%d match=\"exact\"",
								device_index);
#else
							debug_printf(
								"Found a perfect device match #%d!\n",
								device_index, 0,
								0, 0);
#endif
							return device_index;
						}
					}
				}
			}
			if (best_match_quality < match_quality) {
				best_match_quality = match_quality;
				best_device_index = device_index;
			}
			++device;
			++device_index;
		} while (g_std3d_num_devices > (unsigned int)device_index);
	}
#ifdef XVT_MODERN
	XVT_LOG_DEBUG("d3d.device_chosen device=%d match=\"closest\"",
		      best_device_index);
#else
	debug_printf("Settling for a closest match #%d..\n", best_device_index,
		     0, 0, 0);
#endif
	return best_device_index;
}

/* Returns the index of the first format that matches match exactly: the same
 * mode and bpp and, for RGB, the same red, green and blue bits, for RGBA the
 * alpha bits too, and for a palette mode nothing more. Without one, the first
 * with the best score: 1 for the mode, 2 for the mode and bpp, 3 for an RGBA
 * mode and bpp. Returns 0 when count is 0. */
// FUNCTION: XVT 0x4B3450
int std3d_find_closest_format(const struct color_info *match,
			      struct std3d_tex_fmt *formats, unsigned int count)
{
	if (count == 0) {
		return 0;
	}
	int best_match_score = 0;
	int best_format_index = 0;
	struct std3d_tex_fmt *format = formats;
	unsigned int match_score;
	for (int format_index = 0; (unsigned int)format_index < count;
	     ++format_index) {
		match_score = 0;
		if (format->color_info.color_mode == match->color_mode) {
			++match_score;
			if (format->color_info.bpp == match->bpp) {
				++match_score;
				switch (match->color_mode) {
				case STDCOLOR_RGB:
					if (format->color_info.red_bpp ==
						    match->red_bpp &&
					    format->color_info.green_bpp ==
						    match->green_bpp &&
					    format->color_info.blue_bpp ==
						    match->blue_bpp) {
#ifdef XVT_MODERN
						XVT_LOG_DEBUG(
							"d3d.mode_chosen format=%d match=\"exact\"",
							format_index);
#else
						debug_printf(
							"Found a perfect mode match #%d!\n",
							format_index, 0, 0, 0);
#endif
						return format_index;
					}
					break;
				case STDCOLOR_RGBA:
					if (format->color_info.color_mode ==
					    STDCOLOR_RGBA) {
						++match_score;
					}
					if (format->color_info.red_bpp ==
						    match->red_bpp &&
					    format->color_info.green_bpp ==
						    match->green_bpp &&
					    format->color_info.blue_bpp ==
						    match->blue_bpp &&
					    format->color_info.alpha_bpp ==
						    match->alpha_bpp) {
#ifdef XVT_MODERN
						XVT_LOG_DEBUG(
							"d3d.mode_chosen format=%d match=\"exact\"",
							format_index);
#else
						debug_printf(
							"Found a perfect mode match #%d!\n",
							format_index, 0, 0, 0);
#endif
						return format_index;
					}
					break;
				default:
#ifdef XVT_MODERN
					XVT_LOG_DEBUG(
						"d3d.mode_chosen format=%d match=\"exact\"",
						format_index);
#else
					debug_printf(
						"Found a perfect mode match #%d!\n",
						format_index, 0, 0, 0);
#endif
					return format_index;
				}
			}
		}
		if ((int)match_score > best_match_score) {
			best_format_index = format_index;
			best_match_score = match_score;
		}
		++format;
	}
#ifdef XVT_MODERN
	XVT_LOG_DEBUG("d3d.mode_chosen format=%d match=\"closest\"",
		      best_format_index);
#else
	debug_printf("Settling for a closest match #%d..\n", best_format_index,
		     0, 0, 0);
#endif
	return best_format_index;
}

/* Draws the full-viewport color overlay when it is on and not all 0. Each
 * channel is its share of the largest, times 255; alpha is the largest times
 * 0.736 with stippled alpha, else times 0.9, held to 0 to 255, and an alpha of
 * 0 draws nothing. With alpha blending it draws the quad in that color;
 * without, it fills g_p_std3dv_buffer in the 4444 format, uploads it as a
 * translucent texture, draws the quad with it in white, and releases it.
 * Nothing calls this. */
// FUNCTION: XVT 0x4B3590
void std3d_draw_color_overlay(void)
{
	if (g_std3d_color_overlay_enabled == 0 ||
	    (g_std3d_color_overlay_red == 0.0f &&
	     g_std3d_color_overlay_green == 0.0f &&
	     g_std3d_color_overlay_blue == 0.0f)) {
		return;
	}
	float maximum = g_std3d_color_overlay_red >= g_std3d_color_overlay_green
				? g_std3d_color_overlay_red
				: g_std3d_color_overlay_green;
	maximum = g_std3d_color_overlay_blue >= maximum
			  ? g_std3d_color_overlay_blue
			  : maximum;
	uint8_t red = (uint8_t)(g_std3d_color_overlay_red / maximum * 255.0f);
	uint8_t green =
		(uint8_t)(g_std3d_color_overlay_green / maximum * 255.0f);
	uint8_t blue = (uint8_t)(g_std3d_color_overlay_blue / maximum * 255.0f);
	uint8_t alpha;
	if (g_p_std3d_cur_device->caps.b_stippled_shade != 0) {
		float alpha_scale = maximum * 0.736f;
		if (alpha_scale >= 0.0f) {
			float upper_clamped_alpha;
			if (alpha_scale > 255.0f) {
				upper_clamped_alpha = 255.0f;
			} else {
				upper_clamped_alpha = alpha_scale;
			}
			alpha_scale = upper_clamped_alpha;
		} else {
			alpha_scale = 0.0f;
		}
		alpha = (uint8_t)alpha_scale;
	} else {
		float alpha_scale = maximum * 0.9f;
		if (alpha_scale >= 0.0f) {
			float upper_clamped_alpha;
			if (alpha_scale > 255.0f) {
				upper_clamped_alpha = 255.0f;
			} else {
				upper_clamped_alpha = alpha_scale;
			}
			alpha_scale = upper_clamped_alpha;
		} else {
			alpha_scale = 0.0f;
		}
		alpha = (uint8_t)alpha_scale;
	}
	if (alpha == 0) {
		return;
	}
	if (g_p_std3d_cur_device->caps.b_alpha_blend != 0) {
		uint32_t packed_color =
			(uint32_t)blue | ((uint32_t)red << 16) |
			(((uint32_t)green | ((uint32_t)alpha << 16)) << 8);
		g_std3d_quad_verts[0].color = packed_color;
		g_std3d_quad_verts[1].color = packed_color;
		g_std3d_quad_verts[2].color = packed_color;
		g_std3d_quad_verts[3].color = packed_color;
		std3d_start_scene();
		std3d_lock_execute_buffer();
		std3d_add_vertices(g_std3d_quad_verts, 4);
		std3d_begin_instructions();
		std3d_add_triangles(g_std3d_viewport_quad_triangles, 2);
		std3d_execute_buffer();
		std3d_end_scene();
	} else {
		uint16_t color =
			(uint16_t)(green >> g_p_fmt_rgba4444->color_info
						    .green_pos_shift_right)
			<< g_p_fmt_rgba4444->color_info.green_pos_shift;
		color |= (uint16_t)(red >> g_p_fmt_rgba4444->color_info
						   .red_pos_shift_right)
			 << g_p_fmt_rgba4444->color_info.red_pos_shift;
		color |= (uint16_t)(alpha >> g_p_fmt_rgba4444->color_info
						     .alpha_pos_shift_right)
			 << g_p_fmt_rgba4444->color_info.alpha_pos_shift;
		uint16_t blue_color =
			(uint16_t)(blue >> g_p_fmt_rgba4444->color_info
						   .blue_pos_shift_right)
			<< g_p_fmt_rgba4444->color_info.blue_pos_shift;
		std3d_fill_v_buffer(g_p_std3dv_buffer,
				    (uint16_t)(color | blue_color), 0);
		std3d_start_scene();
		std3d_lock_execute_buffer();
		std3d_add_to_texture_cache(g_p_std3dv_buffer,
					   &g_std3d_color_overlay_tex_node, 0,
					   1);
		g_std3d_quad_verts[0].color = UINT32_MAX;
		g_std3d_quad_verts[1].color = UINT32_MAX;
		g_std3d_quad_verts[2].color = UINT32_MAX;
		g_std3d_quad_verts[3].color = UINT32_MAX;
		g_std3d_viewport_quad_triangles[0].texture =
			&g_std3d_color_overlay_tex_node;
		g_std3d_viewport_quad_triangles[1].texture =
			&g_std3d_color_overlay_tex_node;
		std3d_add_vertices(g_std3d_quad_verts, 4);
		std3d_begin_instructions();
		std3d_add_triangles(g_std3d_viewport_quad_triangles, 2);
		std3d_execute_buffer();
		std3d_end_scene();
		g_std3d_color_overlay_tex_node.p_cached_surface->lpVtbl
			->Release(g_std3d_color_overlay_tex_node
					  .p_cached_surface);
		g_std3d_color_overlay_tex_node.p_cached_texture->lpVtbl
			->Release(g_std3d_color_overlay_tex_node
					  .p_cached_texture);
		g_std3d_color_overlay_tex_node.b_cached = 0;
		std3d_cache_list_remove(&g_std3d_color_overlay_tex_node);
	}
}

/* Stores rect in g_std3d_quad_rect, sets g_std3d_quad_verts to its corners and
 * g_std3d_viewport_quad_triangles to two untextured alpha-blended triangles over
 * it. Returns 0. */
// FUNCTION: XVT 0x4B3890
int std3d_build_viewport_quad(const struct std3d_viewport_rect *rect)
{
	g_std3d_quad_rect = *rect;
	memset(g_std3d_quad_verts, 0, sizeof(g_std3d_quad_verts));

	g_std3d_quad_verts[0].sx = (float)g_std3d_quad_rect.x;
	g_std3d_quad_verts[0].sy = (float)g_std3d_quad_rect.y;
	g_std3d_quad_verts[1].sx =
		(float)(g_std3d_quad_rect.x + g_std3d_quad_rect.width);
	g_std3d_quad_verts[1].sy = (float)g_std3d_quad_rect.y;
	g_std3d_quad_verts[2].sx =
		(float)(g_std3d_quad_rect.x + g_std3d_quad_rect.width);
	g_std3d_quad_verts[2].sy =
		(float)(g_std3d_quad_rect.y + g_std3d_quad_rect.height);
	g_std3d_quad_verts[3].sx = (float)g_std3d_quad_rect.x;
	g_std3d_quad_verts[3].sy =
		(float)(g_std3d_quad_rect.y + g_std3d_quad_rect.height);

	g_std3d_viewport_quad_triangles[0].vertex_index1 = 1;
	g_std3d_viewport_quad_triangles[0].vertex_index0 = 0;
	g_std3d_viewport_quad_triangles[0].vertex_index2 = 2;
	g_std3d_viewport_quad_triangles[0].texture = NULL;
	g_std3d_viewport_quad_triangles[0].flags =
		STD3D_RS_ALPHA_BLEND | STD3D_RS_MONO_DISABLE;
	g_std3d_viewport_quad_triangles[1].vertex_index0 = 0;
	g_std3d_viewport_quad_triangles[1].vertex_index1 = 2;
	g_std3d_viewport_quad_triangles[1].texture = NULL;
	g_std3d_viewport_quad_triangles[1].vertex_index2 = 3;
	g_std3d_viewport_quad_triangles[1].flags =
		STD3D_RS_ALPHA_BLEND | STD3D_RS_MONO_DISABLE;

	return 0;
}

/* Writes the starting render states from g_std3d_render_option_flags into an
 * execute buffer of its own, 4096 bytes, executes it inside a scene, releases
 * it, and sets g_d3d_state_flags to the flags. The states: perspective, the
 * filters, subpixel, no wrapping, blending (as std3d_set_render_state sets it for
 * the 0x600 bits), alpha test with compare 6, stippled alpha when the device
 * stipples, shade mode 2, mono, specular, fog, fill mode 3, dither, antialias,
 * z test and z writes when g_std3dz_buffer_enabled is set, the z compare from
 * g_std3dz_compare_cap, and cull mode 1. Returns 1; failures are only printed,
 * and it goes on when making or locking the buffer fails. */
// FUNCTION: XVT 0x4B3980
int std3d_set_initial_render_state(void)
{
	IDirect3DExecuteBuffer *execute_buffer = NULL;
	D3DEXECUTEBUFFERDESC descriptor;
	memset(&descriptor, 0, sizeof(descriptor));
	descriptor.dwSize = 20;
	descriptor.dwFlags = 1;
	descriptor.dwBufferSize = 4096;
	int result = g_d3d_device->lpVtbl->CreateExecuteBuffer(
		g_d3d_device, &descriptor, &execute_buffer, NULL);
	if (result != 0) {
#ifdef XVT_MODERN
		XVT_LOG_ERROR(
			"d3d.call_failed step=\"create_execute_buffer\" error=\"%s\"",
			std3d_lookup_error_string(
				result, g_std3d_error_string_table, 121));
#else
		debug_printf("Error %s creating D3D Execute buffer.\n",
			     std3d_lookup_error_string(
				     result, g_std3d_error_string_table, 121),
			     0, 0, 0);
#endif
	}
	result = execute_buffer->lpVtbl->Lock(execute_buffer, &descriptor);
	if (result != 0) {
#ifdef XVT_MODERN
		XVT_LOG_ERROR(
			"d3d.call_failed step=\"lock_execute_buffer\" error=\"%s\"",
			std3d_lookup_error_string(
				result, g_std3d_error_string_table, 121));
#else
		debug_printf(g_std3d_lock_execute_buffer_error_format,
			     std3d_lookup_error_string(
				     result, g_std3d_error_string_table, 121),
			     0, 0, 0);
#endif
	}

	memset(descriptor.lpData, 0, 4096);
	uint8_t *base = (uint8_t *)descriptor.lpData;
	D3DINSTRUCTION *instruction = (D3DINSTRUCTION *)base;
	instruction->bOpcode = D3DOP_STATERENDER;
	instruction->bSize = sizeof(D3DSTATE);
	instruction->wCount = 25;
	D3DSTATE *state = (D3DSTATE *)(instruction + 1);

	state->dwState = D3DRENDERSTATE_TEXTUREPERSPECTIVE;
	state->dwArg = g_std3d_render_option_flags & 1;
	++state;
	state->dwState = D3DRENDERSTATE_TEXTUREMAG;
	state->dwArg = (g_std3d_render_option_flags & 0x80) != 0 ? 2 : 1;
	++state;
	state->dwState = D3DRENDERSTATE_TEXTUREMIN;
	state->dwArg = (g_std3d_render_option_flags & 0x100) != 0 ? 2 : 1;
	++state;
	state->dwState = D3DRENDERSTATE_SUBPIXEL;
	state->dwArg = (g_std3d_render_option_flags & 0x10) != 0;
	++state;
	state->dwState = D3DRENDERSTATE_SUBPIXELX;
	state->dwArg = (g_std3d_render_option_flags & 0x20) != 0;
	++state;
	state->dwState = D3DRENDERSTATE_WRAPU;
	state->dwArg = 0;
	++state;
	state->dwState = D3DRENDERSTATE_WRAPV;
	state->dwArg = 0;
	++state;
	state->dwState = D3DRENDERSTATE_BLENDENABLE;
	state->dwArg = (g_std3d_render_option_flags & 0x600) != 0;
	++state;
	if ((g_std3d_render_option_flags & 0x600) != 0) {
		if ((g_std3d_render_option_flags & 0x400) != 0) {
			state->dwState = D3DRENDERSTATE_TEXTUREMAPBLEND;
			state->dwArg = 4;
		} else {
			state->dwState = D3DRENDERSTATE_TEXTUREMAPBLEND;
			state->dwArg = 2;
		}
		++state;
		state->dwState = D3DRENDERSTATE_SRCBLEND;
		state->dwArg = 5;
		++state;
		state->dwState = D3DRENDERSTATE_DESTBLEND;
		state->dwArg = 6;
		++state;
	} else {
		state->dwState = D3DRENDERSTATE_TEXTUREMAPBLEND;
		state->dwArg = 2;
		++state;
		state->dwState = D3DRENDERSTATE_SRCBLEND;
		state->dwArg = 2;
		++state;
		state->dwState = D3DRENDERSTATE_DESTBLEND;
		state->dwArg = 1;
		++state;
	}
	state->dwState = D3DRENDERSTATE_ALPHATESTENABLE;
	state->dwArg = 1;
	++state;
	state->dwState = D3DRENDERSTATE_ALPHAFUNC;
	state->dwArg = 6;
	++state;
	if (g_p_std3d_cur_device->caps.b_stippled_shade != 0) {
		state->dwState = D3DRENDERSTATE_STIPPLEDALPHA;
		state->dwArg = 1;
	} else {
		state->dwState = D3DRENDERSTATE_STIPPLEDALPHA;
		state->dwArg = 0;
	}
	++state;
	state->dwState = D3DRENDERSTATE_SHADEMODE;
	state->dwArg = 2;
	++state;
	int z_enabled = 1;
	state->dwState = D3DRENDERSTATE_MONOENABLE;
	state->dwArg = (g_std3d_render_option_flags & 0x8000) == 0;
	++state;
	state->dwState = D3DRENDERSTATE_SPECULARENABLE;
	state->dwArg = (g_std3d_render_option_flags & 4) != 0;
	++state;
	state->dwState = D3DRENDERSTATE_FOGENABLE;
	state->dwArg = (g_std3d_render_option_flags & 0x40) != 0;
	++state;
	state->dwState = D3DRENDERSTATE_FILLMODE;
	state->dwArg = 3;
	++state;
	state->dwState = D3DRENDERSTATE_DITHERENABLE;
	state->dwArg = (g_std3d_render_option_flags & 2) != 0;
	++state;
	state->dwState = D3DRENDERSTATE_ANTIALIAS;
	state->dwArg = (g_std3d_render_option_flags & 8) != 0;
	++state;

	if (g_std3dz_buffer_enabled != 0) {
#ifdef XVT_MODERN
		XVT_LOG_DEBUG("d3d.z_buffer_state state=\"on\"");
#else
		debug_printf("Enabling Z buffer render state.\n", 0, 0, 0, 0);
#endif
	} else {
		z_enabled = 0;
#ifdef XVT_MODERN
		XVT_LOG_DEBUG("d3d.z_buffer_state state=\"off\"");
#else
		debug_printf("Disabling Z buffer render state.\n", 0, 0, 0, 0);
#endif
	}
	state->dwState = D3DRENDERSTATE_ZENABLE;
	state->dwArg = z_enabled;
	++state;
	state->dwState = D3DRENDERSTATE_ZWRITEENABLE;
	state->dwArg = z_enabled;
	++state;
	state->dwState = D3DRENDERSTATE_ZFUNC;
	state->dwArg = std3d_map_z_cmp_func(g_std3dz_compare_cap);
	++state;
	state->dwState = D3DRENDERSTATE_CULLMODE;
	state->dwArg = 1;
	++state;

	instruction = (D3DINSTRUCTION *)state;
	instruction->bOpcode = D3DOP_EXIT;
	instruction->bSize = 0;
	instruction->wCount = 0;
	uint8_t *cursor = (uint8_t *)(instruction + 1);
	result = execute_buffer->lpVtbl->Unlock(execute_buffer);
	if (result != 0) {
#ifdef XVT_MODERN
		XVT_LOG_ERROR(
			"d3d.call_failed step=\"unlock_execute_buffer\" error=\"%s\"",
			std3d_lookup_error_string(
				result, g_std3d_error_string_table, 121));
#else
		debug_printf("Error %s unlocking D3D Execute buffer.\n",
			     std3d_lookup_error_string(
				     result, g_std3d_error_string_table, 121),
			     0, 0, 0);
#endif
	}

	D3DEXECUTEDATA execute_data;
	memset(&execute_data, 0, sizeof(execute_data));
	execute_data.dwSize = sizeof(execute_data);
	execute_data.dwInstructionOffset = 0;
	execute_data.dwInstructionLength = (uint32_t)(cursor - base);
	execute_buffer->lpVtbl->SetExecuteData(execute_buffer, &execute_data);
	result = g_d3d_device->lpVtbl->BeginScene(g_d3d_device);
	if (result != 0) {
#ifdef XVT_MODERN
		XVT_LOG_ERROR(
			"d3d.call_failed step=\"begin_scene\" error=\"%s\"",
			std3d_lookup_error_string(
				result, g_std3d_error_string_table, 121));
#else
		debug_printf(g_std3d_begin_scene_error_format,
			     std3d_lookup_error_string(
				     result, g_std3d_error_string_table, 121),
			     0, 0, 0);
#endif
	}
	result = g_d3d_device->lpVtbl->Execute(g_d3d_device, execute_buffer,
					       g_d3d_viewport,
					       D3DEXECUTE_UNCLIPPED);
	if (result != 0) {
#ifdef XVT_MODERN
		XVT_LOG_ERROR(
			"d3d.call_failed step=\"execute_buffer\" error=\"%s\"",
			std3d_lookup_error_string(
				result, g_std3d_error_string_table, 121));
#else
		debug_printf("Error %s executing buffer.\n",
			     std3d_lookup_error_string(
				     result, g_std3d_error_string_table, 121),
			     0, 0, 0);
#endif
	}
	result = g_d3d_device->lpVtbl->EndScene(g_d3d_device);
	if (result != 0) {
#ifdef XVT_MODERN
		XVT_LOG_ERROR("d3d.call_failed step=\"end_scene\" error=\"%s\"",
			      std3d_lookup_error_string(
				      result, g_std3d_error_string_table, 121));
#else
		debug_printf(g_std3d_end_scene_error_format,
			     std3d_lookup_error_string(
				     result, g_std3d_error_string_table, 121),
			     0, 0, 0);
#endif
	}
	execute_buffer->lpVtbl->Release(execute_buffer);
	g_d3d_state_flags =
		(std3d_render_state_flags)g_std3d_render_option_flags;
#ifdef XVT_MODERN
	XVT_LOG_DEBUG("d3d.render_state_set");
#else
	debug_printf("Initial render state set.\n", 0, 0, 0, 0);
#endif
	return 1;
}

/* Makes the Direct3D viewport, adds it to the device and sets it to width by
 * height at 0, 0 with scales of half the width and height, then builds the
 * viewport quad. Returns 1, or 0 when a step fails. */
// FUNCTION: XVT 0x4B3E30
int std3d_create_viewport(int width, int height)
{
	int result = g_lp_d3d->lpVtbl->CreateViewport(g_lp_d3d, &g_d3d_viewport,
						      NULL);
	if (result != 0) {
#ifdef XVT_MODERN
		XVT_LOG_ERROR(
			"d3d.call_failed step=\"create_viewport\" error=\"%s\"",
			std3d_lookup_error_string(
				result, g_std3d_error_string_table, 121));
#else
		debug_printf("Error %s when creating D3D viewport.\n",
			     std3d_lookup_error_string(
				     result, g_std3d_error_string_table, 121),
			     0, 0, 0);
#endif
		return 0;
	}
	result =
		g_d3d_device->lpVtbl->AddViewport(g_d3d_device, g_d3d_viewport);
	if (result != 0) {
#ifdef XVT_MODERN
		XVT_LOG_ERROR(
			"d3d.call_failed step=\"add_viewport\" error=\"%s\"",
			std3d_lookup_error_string(
				result, g_std3d_error_string_table, 121));
#else
		debug_printf(
			"Error %s when adding the D3D viewport to the device.\n",
			std3d_lookup_error_string(
				result, g_std3d_error_string_table, 121),
			0, 0, 0);
#endif
		return 0;
	}

	D3DVIEWPORT viewport;
	memset(&viewport, 0, sizeof(viewport));
	viewport.dwY = 0;
	viewport.dwX = 0;
	viewport.dwWidth = width;
	viewport.dwHeight = height;
	viewport.dwSize = sizeof(viewport);
	viewport.dvScaleX = (float)(unsigned int)width * 0.5f;
	viewport.dvScaleY = (float)(unsigned int)height * 0.5f;
	viewport.dvMaxX =
		(float)(unsigned int)width / (viewport.dvScaleX * 2.0f);
	viewport.dvMaxY =
		(float)(unsigned int)height / (viewport.dvScaleY * 2.0f);
	result = g_d3d_viewport->lpVtbl->SetViewport(g_d3d_viewport, &viewport);
	if (result != 0) {
#ifdef XVT_MODERN
		XVT_LOG_ERROR(
			"d3d.call_failed step=\"create_viewport\" error=\"%s\"",
			std3d_lookup_error_string(
				result, g_std3d_error_string_table, 121));
#else
		debug_printf("Error %s when creating D3D viewport.\n",
			     std3d_lookup_error_string(
				     result, g_std3d_error_string_table, 121),
			     0, 0, 0);
#endif
		return 0;
	}

	struct std3d_viewport_rect rect;
	rect.x = 0;
	rect.y = 0;
	rect.width = width;
	rect.height = height;
	std3d_build_viewport_quad(&rect);
#ifdef XVT_MODERN
	XVT_LOG_DEBUG("d3d.viewport_created");
#else
	debug_printf("Viewport created successfully.\n", 0, 0, 0, 0);
#endif
	return 1;
}

/* Makes a z-buffer surface of width by height, in video memory on a hardware
 * device and system memory otherwise, at 32, 16 or 8 bits, the first the
 * device's depth flags allow (0x100, 0x400, 0x800); attaches it to
 * g_std3d_render_surface and reads back its description. Also fills
 * g_std3dz_buffer_target and points g_p_std3dz_buffer_state at
 * g_std3dz_buffer_surface_block. Returns 1, or 0 when no depth fits or a step
 * fails. */
// FUNCTION: XVT 0x4B3FC0
int std3d_create_z_buffer(int width, int height)
{
	g_std3dz_buffer_target.storage_type = 1;
	g_std3dz_buffer_target.b_video_memory = 0;
	memcpy(&g_std3dz_buffer_target.raster, g_p_std3d_render_target,
	       sizeof(g_std3dz_buffer_target.raster));
	g_std3dz_buffer_target.unk58 = 0;
	g_p_std3dz_buffer_state = &g_std3dz_buffer_surface_block;
	g_std3dz_buffer_target.pixels = NULL;

	memset(&g_p_std3dz_buffer_state->desc, 0,
	       sizeof(g_p_std3dz_buffer_state->desc));
	g_p_std3dz_buffer_state->desc.dwSize =
		sizeof(g_p_std3dz_buffer_state->desc);
	g_p_std3dz_buffer_state->desc.dwFlags =
		DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | 0x40;
	g_p_std3dz_buffer_state->desc.ddsCaps.dwCaps = DDSCAPS_ZBUFFER;
	g_p_std3dz_buffer_state->desc.dwWidth = width;
	g_p_std3dz_buffer_state->desc.dwHeight = height;
	if (g_p_std3d_cur_device->caps.b_hardware != 0) {
		g_p_std3dz_buffer_state->desc.ddsCaps.dwCaps |=
			DDSCAPS_VIDEOMEMORY;
	} else {
		g_p_std3dz_buffer_state->desc.ddsCaps.dwCaps |=
			DDSCAPS_SYSTEMMEMORY;
	}

	unsigned int z_buffer_bit_depth =
		g_p_std3d_cur_device->d3d_desc.dwDeviceZBufferBitDepth;
	if ((z_buffer_bit_depth & 0x100) != 0) {
		g_p_std3dz_buffer_state->desc.dwZBufferBitDepth = 32;
	} else if ((z_buffer_bit_depth & 0x400) != 0) {
		g_p_std3dz_buffer_state->desc.dwZBufferBitDepth = 16;
	} else if ((z_buffer_bit_depth & 0x800) != 0) {
		g_p_std3dz_buffer_state->desc.dwZBufferBitDepth = 8;
	} else {
#ifdef XVT_MODERN
		XVT_LOG_ERROR("d3d.failed reason=\"z_buffer_depth\"");
#else
		debug_printf("Error: unsupported zbuffer bit depth!\n", 0, 0, 0,
			     0);
#endif
		return 0;
	}
#ifdef XVT_MODERN
	XVT_LOG_DEBUG("d3d.z_buffer_depth bits=%u",
		      g_p_std3dz_buffer_state->desc.dwZBufferBitDepth);
#else
	debug_printf("ZBuffer depth: %d.\n",
		     g_p_std3dz_buffer_state->desc.dwZBufferBitDepth, 0, 0, 0);
#endif

	HRESULT result = g_std3d_direct_draw->lpVtbl->CreateSurface(
		g_std3d_direct_draw, &g_p_std3dz_buffer_state->desc,
		&g_p_std3dz_buffer_state->surface, NULL);
	if (result != 0) {
#ifdef XVT_MODERN
		XVT_LOG_ERROR(
			"d3d.call_failed step=\"create_z_buffer_surface\" error=\"%s\"",
			std3d_lookup_error_string(
				result, g_std3d_error_string_table, 121));
#else
		debug_printf("Error %s when creating zBuffer DDraw surface.\n",
			     std3d_lookup_error_string(
				     result, g_std3d_error_string_table, 121),
			     0, 0, 0);
#endif
		return 0;
	}

	result = g_std3d_render_surface->lpVtbl->AddAttachedSurface(
		g_std3d_render_surface, g_p_std3dz_buffer_state->surface);
	if (result != 0) {
#ifdef XVT_MODERN
		XVT_LOG_ERROR(
			"d3d.call_failed step=\"attach_z_buffer\" error=\"%s\"",
			std3d_lookup_error_string(
				result, g_std3d_error_string_table, 121));
#else
		debug_printf("Error %s when attaching zbuffer to backbuffer.\n",
			     std3d_lookup_error_string(
				     result, g_std3d_error_string_table, 121),
			     0, 0, 0);
#endif
		return 0;
	}

	result = g_p_std3dz_buffer_state->surface->lpVtbl->GetSurfaceDesc(
		g_p_std3dz_buffer_state->surface,
		&g_p_std3dz_buffer_state->desc);
	if (result != 0) {
#ifdef XVT_MODERN
		XVT_LOG_ERROR(
			"d3d.call_failed step=\"get_z_buffer_description\" error=\"%s\"",
			std3d_lookup_error_string(
				result, g_std3d_error_string_table, 121));
#else
		debug_printf(
			"Error %s when getting zbuffer surface description.\n",
			std3d_lookup_error_string(
				result, g_std3d_error_string_table, 121),
			0, 0, 0);
#endif
		return 0;
	}

	if ((g_p_std3dz_buffer_state->desc.ddsCaps.dwCaps &
	     DDSCAPS_VIDEOMEMORY) != 0) {
		g_std3dz_buffer_target.b_video_memory = 1;
	}
#ifdef XVT_MODERN
	XVT_LOG_DEBUG("d3d.z_buffer_memory memory=\"%s\"",
		      g_std3dz_buffer_target.b_video_memory != 0 ? "video"
								 : "system");
#else
	debug_printf("ZBuffer in %s memory.\n",
		     g_std3dz_buffer_target.b_video_memory != 0 ? "VIDEO"
								: "SYSTEM",
		     0, 0, 0);
#endif
#ifdef XVT_MODERN
	XVT_LOG_DEBUG("d3d.z_buffer_created");
#else
	debug_printf("ZBuffer created successfully.\n", 0, 0, 0, 0);
#endif
	return 1;
}

/* Records one enumerated device in g_std3d_devices while fewer than 4 are
 * recorded: its GUID, name and description, its hardware description when that
 * has a color model, else the software one, and the caps read from it (texture
 * sizes 1 to 256). Returns 1 to go on, or 0 once 4 are recorded. */
// FUNCTION: XVT 0x4B4210
HRESULT AERON_DXAPI std3d_enum_devices_callback(DxGuid *guid,
						char *device_description,
						char *device_name,
						D3DDEVICEDESC *hardware_desc,
						D3DDEVICEDESC *software_desc,
						void *context)
{
	(void)context;

	if (g_std3d_num_devices < 4) {

		struct std3d_device *device =
			&g_std3d_devices[g_std3d_num_devices];
		memcpy(&device->guid, guid, sizeof(device->guid));
		strncpy(device->device_description, device_description,
			sizeof(device->device_description));
		strncpy(device->device_name, device_name,
			sizeof(device->device_name));
		if (hardware_desc->dcmColorModel != 0) {
			device->caps.b_hardware = 1;
			memcpy(&device->d3d_desc, hardware_desc,
			       sizeof(device->d3d_desc));
		} else {
			device->caps.b_hardware = 0;
			memcpy(&device->d3d_desc, software_desc,
			       sizeof(device->d3d_desc));
		}

		device->caps.color_model_flags = 0;
		if ((device->d3d_desc.dcmColorModel & 2) != 0) {
			device->caps.color_model_flags = 2;
		}
		if ((device->d3d_desc.dcmColorModel & 1) != 0) {
			device->caps.color_model_flags |= 1;
		}
		device->caps.b_texture_perspective =
			device->d3d_desc.dpcTriCaps.dwTextureCaps & 1;
		device->caps.b_has_z_buffer =
			device->d3d_desc.dwDeviceZBufferBitDepth != 0;
		device->caps.b_square_only_texture =
			(device->d3d_desc.dpcTriCaps.dwTextureCaps & 0x20) != 0;
		device->caps.b_alpha_texture =
			(device->d3d_desc.dpcTriCaps.dwTextureCaps & 4) != 0;
		unsigned int shade_caps =
			device->d3d_desc.dpcTriCaps.dwShadeCaps;
		device->caps.b_stippled_shade = (shade_caps & 0x1000) == 0 &&
						(shade_caps & 0x2000) != 0;
		device->caps.b_alpha_blend =
			((device->d3d_desc.dpcTriCaps.dwTextureBlendCaps & 8) !=
				 0 &&
			 (device->d3d_desc.dpcTriCaps.dwShadeCaps & 0x4000) !=
				 0) ||
			device->caps.b_stippled_shade != 0;
		device->caps.b_color_key_texture =
			(device->d3d_desc.dpcTriCaps.dwTextureCaps & 8) != 0;
		device->caps.render_bit_depth_mask =
			std3d_pack_render_bit_depths(
				device->d3d_desc.dwDeviceRenderBitDepth);
		device->caps.z_cmp_caps_mask = std3d_mask_z_cmp_caps(
			device->d3d_desc.dpcTriCaps.dwZCmpCaps);
		device->caps.min_texture_width = 1;
		device->caps.min_texture_height = 1;
		device->caps.max_texture_width = 256;
		device->caps.max_texture_height = 256;
		device->caps.max_buffer_size = device->d3d_desc.dwMaxBufferSize;
		device->caps.max_vertex_count =
			device->d3d_desc.dwMaxVertexCount;

#ifdef XVT_MODERN
		XVT_LOG_DEBUG(
			"d3d.device_found hardware=%d mono=%d rgb=%d z_buffer=%d",
			device->caps.b_hardware != 0,
			(device->caps.color_model_flags & 1) != 0,
			(device->caps.color_model_flags & 2) != 0,
			device->caps.b_has_z_buffer != 0);
#else
		debug_printf("Found |%s|%s|%s|%s| D3D Device\n",
			     device->caps.b_hardware != 0 ? "HW" : "SW",
			     device->caps.color_model_flags & 1 ? "MONO" : "",
			     device->caps.color_model_flags & 2 ? "RGB" : "",
			     device->caps.b_has_z_buffer != 0 ? "Z" : "Non-Z");
#endif
#ifdef XVT_MODERN
		XVT_LOG_DEBUG(
			"d3d.device_caps alpha=%d stippled=%d color_key=%d depths=%#x",
			device->caps.b_alpha_texture != 0,
			device->caps.b_stippled_shade != 0,
			device->caps.b_color_key_texture != 0,
			device->caps.render_bit_depth_mask);
#else
		debug_printf("      |%s|%s|%s| %dbpp\n",
			     device->caps.b_alpha_texture != 0 ? "Alpha"
							       : "No Alpha",
			     device->caps.b_stippled_shade != 0 ? "Stippled"
								: "Blend",
			     device->caps.b_color_key_texture != 0
				     ? "Colorkey"
				     : "No Colorkey",
			     device->caps.render_bit_depth_mask);
#endif
#ifdef XVT_MODERN
		XVT_LOG_DEBUG("d3d.device_name name=\"%s\" description=\"%s\"",
			      device->device_name, device->device_description);
#else
		debug_printf("Description: %s [%s]\n", device->device_name,
			     device->device_description, 0, 0);
#endif
		++g_std3d_num_devices;
		return 1;
	}
	return 0;
}

/* Records one enumerated texture format in g_std3d_texture_formats while fewer
 * than 8 are recorded: an 8-bit palette format, or RGB or RGBA with each
 * channel's position, bits and shift from 8 bits read from its mask. Skips a
 * 4-bit palette format (flag 0x8). Returns 1 to go on, or 0 once 8 are
 * recorded. Does not check for an empty mask, on which its bit search never
 * ends. */
// FUNCTION: XVT 0x4B4490
int AERON_DXAPI std3d_enum_texture_formats(DDSURFACEDESC *surface_desc,
					   void *context)
{
	(void)context;
	if ((unsigned int)g_std3d_num_texture_formats < 8) {
		struct std3d_tex_fmt *format =
			&g_std3d_texture_formats[g_std3d_num_texture_formats];
		memcpy(&format->ddsd, surface_desc, sizeof(format->ddsd));
		/* Each of these four first counts its mask's trailing zeros
		 * (the channel's position), then is reset to count the mask's
		 * set bits (the channel's width, stored as its BPP). */
		int red_shift;
		int green_shift;
		int blue_shift;
		unsigned int mask;
		if ((surface_desc->ddpfPixelFormat.dwFlags &
		     DDPF_PALETTEINDEXED8) != 0) {
			format->color_info.color_mode = STDCOLOR_PAL;
			format->color_info.bpp = 8;
			format->color_info.red_pos_shift = 0;
			format->color_info.red_pos_shift_right = 0;
			format->color_info.red_bpp = 0;
			format->color_info.green_pos_shift = 0;
			format->color_info.green_pos_shift_right = 0;
			format->color_info.green_bpp = 0;
			format->color_info.blue_pos_shift = 0;
			format->color_info.blue_pos_shift_right = 0;
			format->color_info.blue_bpp = 0;
			format->color_info.alpha_pos_shift = 0;
			format->color_info.alpha_pos_shift_right = 0;
			format->color_info.alpha_bpp = 0;
#ifdef XVT_MODERN
			XVT_LOG_DEBUG("d3d.palette_format bits=%u",
				      format->color_info.bpp);
#else
			debug_printf("Found %dbpp palettized tex format.\n",
				     format->color_info.bpp, 0, 0, 0);
#endif
		} else if ((surface_desc->ddpfPixelFormat.dwFlags & 8) != 0) {
			return 1;
		} else if ((surface_desc->ddpfPixelFormat.dwFlags &
			    DDPF_ALPHAPIXELS) != 0) {
			format->color_info.color_mode = STDCOLOR_RGBA;
			format->color_info.bpp =
				surface_desc->ddpfPixelFormat.dwRGBBitCount;

			red_shift = 0;
			mask = surface_desc->ddpfPixelFormat.dwRBitMask;
			while ((mask & 1) == 0) {
				++red_shift;
				mask >>= 1;
			}
			format->color_info.red_pos_shift = red_shift;
			format->color_info
				.red_pos_shift_right = std3d_log2_floor(
				0xFFu /
				(surface_desc->ddpfPixelFormat.dwRBitMask >>
				 red_shift));
			red_shift = 0;
			while ((mask & 1) != 0) {
				++red_shift;
				mask >>= 1;
			}
			format->color_info.red_bpp = red_shift;

			green_shift = 0;
			mask = surface_desc->ddpfPixelFormat.dwGBitMask;
			while ((mask & 1) == 0) {
				++green_shift;
				mask >>= 1;
			}
			format->color_info.green_pos_shift = green_shift;
			format->color_info
				.green_pos_shift_right = std3d_log2_floor(
				0xFFu /
				(surface_desc->ddpfPixelFormat.dwGBitMask >>
				 green_shift));
			green_shift = 0;
			while ((mask & 1) != 0) {
				++green_shift;
				mask >>= 1;
			}
			format->color_info.green_bpp = green_shift;

			blue_shift = 0;
			mask = surface_desc->ddpfPixelFormat.dwBBitMask;
			while ((mask & 1) == 0) {
				++blue_shift;
				mask >>= 1;
			}
			format->color_info.blue_pos_shift = blue_shift;
			format->color_info
				.blue_pos_shift_right = std3d_log2_floor(
				0xFFu /
				(surface_desc->ddpfPixelFormat.dwBBitMask >>
				 blue_shift));
			blue_shift = 0;
			while ((mask & 1) != 0) {
				++blue_shift;
				mask >>= 1;
			}
			format->color_info.blue_bpp = blue_shift;

			int alpha_shift = 0;
			mask = surface_desc->ddpfPixelFormat.dwRGBAlphaBitMask;
			while ((mask & 1) == 0) {
				++alpha_shift;
				mask >>= 1;
			}
			format->color_info.alpha_pos_shift = alpha_shift;
			format->color_info.alpha_pos_shift_right =
				std3d_log2_floor(0xFFu /
						 (surface_desc->ddpfPixelFormat
							  .dwRGBAlphaBitMask >>
						  alpha_shift));
			alpha_shift = 0;
			while ((mask & 1) != 0) {
				++alpha_shift;
				mask >>= 1;
			}
			format->color_info.alpha_bpp = alpha_shift;
#ifdef XVT_MODERN
			XVT_LOG_DEBUG(
				"d3d.rgba_format red=%d green=%d blue=%d alpha=%d",
				format->color_info.red_bpp,
				format->color_info.green_bpp,
				format->color_info.blue_bpp,
				format->color_info.alpha_bpp);
#else
			debug_printf("Found RGBA tex format (%d:%d:%d:%d).\n",
				     format->color_info.red_bpp,
				     format->color_info.green_bpp,
				     format->color_info.blue_bpp,
				     format->color_info.alpha_bpp);
#endif
			++g_std3d_num_texture_formats;
			return 1;
		} else {
			format->color_info.color_mode = STDCOLOR_RGB;
			format->color_info.bpp =
				surface_desc->ddpfPixelFormat.dwRGBBitCount;

			red_shift = 0;
			mask = surface_desc->ddpfPixelFormat.dwRBitMask;
			while ((mask & 1) == 0) {
				++red_shift;
				mask >>= 1;
			}
			format->color_info.red_pos_shift = red_shift;
			format->color_info
				.red_pos_shift_right = std3d_log2_floor(
				0xFFu /
				(surface_desc->ddpfPixelFormat.dwRBitMask >>
				 red_shift));
			red_shift = 0;
			while ((mask & 1) != 0) {
				++red_shift;
				mask >>= 1;
			}
			format->color_info.red_bpp = red_shift;

			green_shift = 0;
			mask = surface_desc->ddpfPixelFormat.dwGBitMask;
			while ((mask & 1) == 0) {
				++green_shift;
				mask >>= 1;
			}
			format->color_info.green_pos_shift = green_shift;
			format->color_info
				.green_pos_shift_right = std3d_log2_floor(
				0xFFu /
				(surface_desc->ddpfPixelFormat.dwGBitMask >>
				 green_shift));
			green_shift = 0;
			while ((mask & 1) != 0) {
				++green_shift;
				mask >>= 1;
			}
			format->color_info.green_bpp = green_shift;

			blue_shift = 0;
			mask = surface_desc->ddpfPixelFormat.dwBBitMask;
			while ((mask & 1) == 0) {
				++blue_shift;
				mask >>= 1;
			}
			format->color_info.blue_pos_shift = blue_shift;
			format->color_info
				.blue_pos_shift_right = std3d_log2_floor(
				0xFFu /
				(surface_desc->ddpfPixelFormat.dwBBitMask >>
				 blue_shift));
			blue_shift = 0;
			while ((mask & 1) != 0) {
				++blue_shift;
				mask >>= 1;
			}
			format->color_info.blue_bpp = blue_shift;
			format->color_info.alpha_pos_shift = 0;
			format->color_info.alpha_pos_shift_right = 0;
			format->color_info.alpha_bpp = 0;
#ifdef XVT_MODERN
			XVT_LOG_DEBUG("d3d.rgb_format red=%d green=%d blue=%d",
				      format->color_info.red_bpp,
				      format->color_info.green_bpp,
				      format->color_info.blue_bpp);
#else
			debug_printf("Found RGB tex format (%d:%d:%d).\n",
				     format->color_info.red_bpp,
				     format->color_info.green_bpp,
				     format->color_info.blue_bpp, 0);
#endif
		}

		++g_std3d_num_texture_formats;
		return 1;
	}
	return 0;
}

/* Maps DirectDraw bit-depth flags to bits: 0x4000 (1 bit) to 0x01, 0x2000 to
 * 0x02, 0x1000 to 0x04, 0x800 to 0x08, 0x400 to 0x10, 0x200 to 0x20 and 0x100
 * (32 bits) to 0x40. */
// FUNCTION: XVT 0x4B47E0
int std3d_pack_render_bit_depths(int ddbd_flags)
{
	int result = 0;
	if ((ddbd_flags & 0x4000) != 0) {
		result |= 0x01;
	}
	if ((ddbd_flags & 0x2000) != 0) {
		result |= 0x02;
	}
	if ((ddbd_flags & 0x1000) != 0) {
		result |= 0x04;
	}
	if ((ddbd_flags & 0x0800) != 0) {
		result |= 0x08;
	}
	if ((ddbd_flags & 0x0400) != 0) {
		result |= 0x10;
	}
	if ((ddbd_flags & 0x0200) != 0) {
		result |= 0x20;
	}
	if ((ddbd_flags & 0x0100) != 0) {
		result |= 0x40;
	}
	return result;
}

/* Returns d3dpcmpcaps & 0xFF: each of the eight compare caps bits is copied
 * to the same place. */
// FUNCTION: XVT 0x4B4880
int std3d_mask_z_cmp_caps(unsigned int d3dpcmpcaps)
{
	int result = 0;

	if (d3dpcmpcaps & 1) {
		result = 1;
	}
	if (d3dpcmpcaps & 4) {
		result |= 4;
	}
	if (d3dpcmpcaps & 2) {
		result |= 2;
	}
	if (d3dpcmpcaps & 8) {
		result |= 8;
	}
	if (d3dpcmpcaps & 0x10) {
		result |= 0x10;
	}
	if (d3dpcmpcaps & 0x20) {
		result |= 0x20;
	}
	if (d3dpcmpcaps & 0x40) {
		result |= 0x40;
	}
	if (d3dpcmpcaps & 0x80) {
		result |= 0x80;
	}
	return result;
}

/* Returns the compare function for one compare caps bit: 0x1 gives 1, 0x2 gives
 * 2, 0x4 gives 3, 0x8 gives 4, 0x10 gives 5, 0x20 gives 6, 0x40 gives 7 and
 * 0x80 gives 8. With several bits set it ORs their values together; with none
 * it returns 0. */
// FUNCTION: XVT 0x4B48D0
unsigned int std3d_map_z_cmp_func(unsigned int caps_mask)
{
	unsigned int result = 0;

	if (caps_mask & 1) {
		result = 1;
	}
	if (caps_mask & 4) {
		result |= 3;
	}
	if (caps_mask & 2) {
		result |= 2;
	}
	if (caps_mask & 8) {
		result |= 4;
	}
	if (caps_mask & 0x10) {
		result |= 5;
	}
	if (caps_mask & 0x20) {
		result |= 6;
	}
	if (caps_mask & 0x40) {
		result |= 7;
	}
	if (caps_mask & 0x80) {
		result |= 8;
	}
	return result;
}
