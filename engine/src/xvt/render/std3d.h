#ifndef XVT_RENDER_STD3D_H
#define XVT_RENDER_STD3D_H

#include "aeron/compat/d3d.h"
#include "aeron/compat/ddraw.h"
#include "xvt/render/color.h"
#include "xvt/util/win32.h"
#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum std3d_render_state_flags {
	STD3D_RS_FOG_ENABLE = 0x40,
	STD3D_RS_TEXTURE_MAG_LINEAR = 0x80,
	STD3D_RS_TEXTURE_MIN_LINEAR = 0x100,
	STD3D_RS_ALPHA_BLEND = 0x200,
	STD3D_RS_TEXTURE_MODULATE_ALPHA = 0x400,
	STD3D_RS_Z_COMPARE_ENABLE = 0x800,
	STD3D_RS_Z_WRITE_ENABLE = 0x1000,
	STD3D_RS_TEXTURE_ADDRESS_CLAMP = 0x2000,
	STD3D_RS_MONO_DISABLE = 0x8000,
} std3d_render_state_flags;

/* One triangle of a hardware batch, as std3d_add_triangles writes it. */
struct std3d_render_tri {
	int vertex_index0; /* First corner: index into the batch's vertices. */
	int vertex_index1; /* Second corner. */
	int vertex_index2; /* Third corner. */
	/* Render-state bits set before it; neighbors with the same bits and
	 * texture share one instruction. */
	std3d_render_state_flags flags;
	struct std3d_tex_cache_node
		*texture; /* Texture to draw with; NULL for none. */
};

/* One texture in video memory and its place on the cache list. */
struct std3d_tex_cache_node {
	/* The Direct3D texture while cached; NULL otherwise. */
	IDirect3DTexture *p_cached_texture;
	IDirectDrawSurface *p_cached_surface; /* Its DirectDraw surface. */
	/* Description of the system-memory surface it was loaded from. */
	DDSURFACEDESC ddsd;
	/* Direct3D handle std3d_add_triangles sets as the texture render state;
	 * 0 when getting it failed. */
	unsigned int tex_handle;
	/* 1 while the texture is in video memory and on the cache list. */
	int b_cached;
	/* 1 when made in a format with alpha; nothing reads it. */
	int uses_alpha_format;
	unsigned int width;  /* Width as made; nothing reads it. */
	unsigned int height; /* Height as made; nothing reads it. */
	/* width times height; taken from the device's available_memory while
	 * cached. */
	unsigned int texel_count;
	/* g_std3d_texture_batch_tag when last used; a purge spares the textures
	 * of the current batch. */
	unsigned int cache_batch_tag;
	/* Neighbor toward g_p_tex_cache_head, the least recently used end. */
	struct std3d_tex_cache_node *p_prev;
	/* Neighbor toward g_p_tex_cache_tail, the most recently used end. */
	struct std3d_tex_cache_node *p_next;
};

/* What a Direct3D device can do, as std3d_enum_devices_callback reads it from
 * the device's description. */
struct std3d_device_caps {
	/* 1 for a hardware device: its hardware description has a color
	 * model. */
	int b_hardware;
	/* Perspective-correct texturing: the 0x1 bit of the triangle texture
	 * caps. */
	int b_texture_perspective;
	int b_has_z_buffer; /* 1 when any z-buffer bit depth is reported. */
	/* Color-keyed textures: the 0x8 bit of the triangle texture caps. */
	int b_color_key_texture;
	/* Alpha in textures: the 0x4 bit of the triangle texture caps. */
	int b_alpha_texture;
	/* 1 when the triangle shade caps have the 0x2000 bit and not the
	 * 0x1000 bit: stippled alpha in place of blended. */
	int b_stippled_shade;
	/* 1 when the texture blend caps have the 0x8 bit and the shade caps
	 * the 0x4000 bit, or b_stippled_shade is set. */
	int b_alpha_blend;
	/* Square textures only: the 0x20 bit of the triangle texture caps. */
	int b_square_only_texture;
	int b_clamp_supported; /* Never read or written. */
	/* Color models: 0x1 for mono, 0x2 for RGB. */
	unsigned int color_model_flags;
	/* Render bit depths from std3d_pack_render_bit_depths; only a debug print
	 * reads it. */
	unsigned int render_bit_depth_mask;
	/* Z compare functions, as the D3D compare caps bits; with the 0x10
	 * bit (greater) the z-buffer uses the greater test. */
	unsigned int z_cmp_caps_mask;
	unsigned int min_texture_width;	 /* 1; nothing reads it. */
	unsigned int min_texture_height; /* 1; nothing reads it. */
	unsigned int max_texture_width;	 /* 256; nothing reads it. */
	unsigned int max_texture_height; /* 256; nothing reads it. */
	/* Largest execute buffer in bytes; 0 when the device states none. */
	unsigned int max_buffer_size;
	/* Most vertices in an execute buffer; 0 when the device states none. */
	unsigned int max_vertex_count;
};

/* A Direct3D device description, copied whole by std3d_enum_devices_callback:
 * the hardware one when it has a color model, else the software one. The
 * fields marked unread are only copied. */
/* drift-ok: camelcase -- a copy of Direct3D's D3DDEVICEDESC */
struct std3d_device_desc {
	unsigned int dwSize;	     /* Unread. */
	unsigned int dwFlags;	     /* Unread. */
	unsigned int dcmColorModel;  /* Color models: 0x1 mono, 0x2 RGB. */
	unsigned int dwDevCaps;	     /* Unread. */
	uint8_t dtcTransformCaps[8]; /* Unread. */
	int bClipping;		     /* Unread. */
	uint8_t dlcLightingCaps[16]; /* Unread. */
	D3DPRIMCAPS dpcLineCaps;     /* Unread. */
	/* Triangle caps: texture, shade, texture blend and z compare bits. */
	D3DPRIMCAPS dpcTriCaps;
	unsigned int dwDeviceRenderBitDepth; /* Render bit depth flags. */
	/* Z-buffer bit depth flags; std3d_create_z_buffer takes 32, 16 or 8 bits
	 * from the 0x100, 0x400 and 0x800 bits, in that order. */
	unsigned int dwDeviceZBufferBitDepth;
	unsigned int dwMaxBufferSize;	 /* Largest execute buffer in bytes. */
	unsigned int dwMaxVertexCount;	 /* Most vertices per execute buffer. */
	unsigned int dwMinTextureWidth;	 /* Unread. */
	unsigned int dwMinTextureHeight; /* Unread. */
	unsigned int dwMaxTextureWidth;	 /* Unread. */
	unsigned int dwMaxTextureHeight; /* Unread. */
	unsigned int dwMinStippleWidth;	 /* Unread. */
	unsigned int dwMaxStippleWidth;	 /* Unread. */
	unsigned int dwMinStippleHeight; /* Unread. */
	unsigned int dwMaxStippleHeight; /* Unread. */
};

/* One Direct3D device found by std3d_startup, in g_std3d_devices. */
struct std3d_device {
	struct std3d_device_caps caps; /* What it can do, from d3d_desc. */
	/* Name from the enumeration, cut at 128 bytes without a terminator
	 * when it fills them. */
	char device_name[128];
	/* Description from the enumeration, cut the same way. */
	char device_description[128];
	/* Texture video memory in bytes, set by std3d_create_device. */
	unsigned int total_memory;
	/* Free texture memory: bytes when std3d_create_device sets it, then
	 * lowered by each cached texture's texel_count and raised when one
	 * leaves; std3d_flush_texture_cache sets it back to total_memory. Only a
	 * debug print reads it. */
	unsigned int available_memory;
	struct std3d_device_desc
		d3d_desc; /* The description it was found with. */
	/* Device GUID; std3d_create_device asks the render surface for this
	 * interface to make the device. */
	DxGuid guid;
};

/* Size and pixel format of a std3dv_buffer. From color_mode on it has
 * ColorInfo's layout and is filled by copying one in; the code reads only
 * color_mode and bpp of that part. */
struct std3d_raster_info {
	unsigned int width;  /* Pixels per row. */
	unsigned int height; /* Rows. */
	/* Nothing reads it; std3d_create_z_buffer's copy of the render target
	 * puts its size_bytes here. */
	unsigned int tile_factor;
	unsigned int row_pitch; /* Bytes from one row to the next. */
	/* Nothing reads it; that copy puts pitch_pixels here. */
	unsigned int unused10;
	/* Palette, RGB or RGBA. */
	std_color_mode
		color_mode; ///< Start of the flattened ColorInfo copied verbatim from std3d_tex_fmt.
	unsigned int bpp;	 /* Bits per pixel. */
	int red_bpp;		 /* Red bits; unread here. */
	int green_bpp;		 /* Green bits; unread here. */
	int blue_bpp;		 /* Blue bits; unread here. */
	int red_pos_shift;	 /* Red bit position; unread here. */
	int green_pos_shift;	 /* Green bit position; unread here. */
	int blue_pos_shift;	 /* Blue bit position; unread here. */
	int red_pos_shift_right; /* Shift from 8 bits to red's; unread here. */
	int green_pos_shift_right; /* Shift from 8 bits to green's; unread here. */
	int blue_pos_shift_right; /* Shift from 8 bits to blue's; unread here. */
	int alpha_bpp;		  /* Alpha bits; unread here. */
	int alpha_pos_shift;	  /* Alpha bit position; unread here. */
	int alpha_pos_shift_right; /* Shift from 8 bits to alpha's; unread here. */
};

/* A block of pixels the texture code copies from: memory of its own or a
 * DirectDraw surface. */
struct std3dv_buffer {
	/* 1 for a DirectDraw surface in dd_surface; 0 for plain memory. */
	int storage_type;
	/* std3d_lock_v_buffer calls not yet undone; a surface is locked on the
	 * first and unlocked when the last is undone. */
	int lock_count;
	/* Never read or written by name. */
	int in_video_memory; ///< Nonzero when the DirectDraw-backed record resides in video memory; zero for
	///< malloc-backed buffers.
	struct std3d_raster_info raster; /* Size, row pitch and pixel format. */
	int unused58;			 /* Never read or written. */
	/* The pixels: memory from std3d_alloc_v_buffer or the caller, or a
	 * surface's memory while it is locked. */
	void *pixels;
	/* Color key of an RGB buffer, used when the device has no alpha
	 * textures; nothing sets it but the callers' memset to 0. */
	unsigned int transparent_color;
	/* The surface when storage_type is 1; std3d_free_v_buffer releases it. */
	IDirectDrawSurface *dd_surface;
	/* Never read or written by name. */
	uint8_t reserved68
		[112]; ///< Reserved in ordinary software buffers. In the static z-buffer overlay, this
	///< tail is unused04 at +0x68 followed by DDSURFACEDESC at +0x6C.
};

/* One texture format the device offers, from std3d_enum_texture_formats. */
struct std3d_tex_fmt {
	/* Palette, RGB or RGBA, with each channel's bits and position read from
	 * the masks. */
	struct color_info color_info;
	/* The surface description as enumerated; textures in this format are
	 * created from a copy. */
	DDSURFACEDESC ddsd;
};

/* The surface Direct3D draws into, as std3d_init_render_target_desc fills
 * g_std3d_render_target_desc. */
struct std3d_render_target_desc {
	unsigned int width;	   /* Pixels per row. */
	unsigned int height;	   /* Rows. */
	unsigned int size_bytes;   /* pitch times height; nothing reads it. */
	int pitch;		   /* Bytes from one row to the next. */
	unsigned int pitch_pixels; /* pitch / 2; nothing reads it. */
	struct color_info color_info; /* Always 16-bit RGB, 5, 6 and 5 bits. */
};

/* Nothing uses this structure. */
struct std3d_surface_state {
	IDirectDrawSurface *surface; /* Never read or written. */
	/* Never read or written. */
	int unused04; ///< Unused slot between the DirectDraw surface pointer and DDSURFACEDESC.
	DDSURFACEDESC ddsd; /* Never read or written. */
};

/* A rectangle on the render target, in pixels. */
struct std3d_viewport_rect {
	int x;	    /* Left edge. */
	int y;	    /* Top edge. */
	int width;  /* Width. */
	int height; /* Height. */
};

/* One row of g_std3d_error_string_table. */
struct std3d_error_string_entry {
	int code;	     /* The result code. */
	const char *message; /* Its name as text. */
};

/* A record of the z-buffer that std3d_create_z_buffer fills and nothing else
 * reads. */
struct std3dz_buffer_target {
	int storage_type;   /* Set to 1. */
	int lock_count;	    /* Never read or written. */
	int b_video_memory; /* 1 when the surface is in video memory. */
	struct std3d_raster_info
		raster; /* A copy of the render target's record. */
	int unk58;	/* Set to 0. */
	void *pixels;	/* Set to NULL. */
};

extern unsigned int g_std3d_exec_buf_max_verts;
extern unsigned int g_std3d_num_devices;
extern unsigned int g_std3d_cur_device_idx;
extern struct std3d_device g_std3d_devices[4];
extern struct std3d_device *g_p_std3d_cur_device;
extern unsigned int g_std3d_texture_batch_tag;
extern int g_tex_cache_count;
extern struct std3d_tex_cache_node *g_p_tex_cache_head;
extern struct std3d_tex_cache_node *g_p_tex_cache_tail;
extern int g_d3d_buf_vert_count;
extern uint8_t *g_d3d_write_ptr;

/* The z-buffer surface std3d_create_z_buffer makes. */
struct std3dz_buffer_surface_block {
	/* The surface, attached to g_std3d_render_surface; std3d_clear_z_buffer
	 * fills it and std3d_close releases it. */
	IDirectDrawSurface *surface;
	int unused04; /* Never read or written. */
	/* What std3d_create_z_buffer asked for, then what the surface reports. */
	DDSURFACEDESC desc;
};

extern unsigned int g_std3d_render_option_flags;
extern struct std3d_tex_fmt *g_p_fmt_opaque_texture;
extern float g_std3d_color_overlay_red;
extern float g_std3d_color_overlay_green;
extern float g_std3d_color_overlay_blue;
extern int g_std3d_color_overlay_enabled;
extern int g_std3d_min_texture_width;
extern int g_std3dz_compare_cap;
extern int g_std3d_min_texture_height;
extern int g_std3d_max_texture_width;
extern int g_std3d_max_texture_height;

void std3d_copy_palette_to_scratch16(const uint16_t *palette, int color_count);
void std3d_convert_palette_to1555(const uint16_t *palette, int color_count);
int std3d_startup(void);
struct std3d_render_target_desc *
std3d_init_render_target_desc(unsigned int width, unsigned int height,
			      int pitch_bytes);
void std3d_shutdown(void);
int std3d_create_device(unsigned int device_idx, int b_use_z_buffer);
struct IDirectDrawSurface *
std3d_set_render_surface(struct IDirectDrawSurface *surface);
int std3d_set_color_overlay_params(float red, float green, float blue,
				   int enabled);
int std3d_log2_floor(int n);
const char *
std3d_lookup_error_string(int error_code,
			  const struct std3d_error_string_entry *entries,
			  int entry_count);
void std3d_build_colormap16(uint8_t *p_rgb888, uint16_t *p_out,
			    struct color_info *p_fmt, uint8_t default_alpha,
			    int color_key);
void std3d_build_colormap_opaque(uint8_t *p_rgb888, uint16_t *p_out,
				 struct color_info *p_fmt);
void std3d_build_colormap_color_key(uint8_t *p_rgb888, uint16_t *p_out,
				    struct color_info *p_fmt);
void std3d_build_colormap_alpha(uint8_t *p_rgb888, uint16_t *p_out,
				struct color_info *p_fmt, uint8_t alpha);
struct std3dv_buffer *
std3d_alloc_v_buffer(const struct std3d_raster_info *raster, ...);
void std3d_free_v_buffer(struct std3dv_buffer *vbuffer);
void std3d_lock_v_buffer(struct std3dv_buffer *vbuffer);
void std3d_unlock_v_buffer(struct std3dv_buffer *vbuffer);
void std3d_close(void);
void std3d_blit_v_buffer(struct std3dv_buffer *destination,
			 struct std3dv_buffer *source, int destination_x,
			 int destination_y, int source_x, int source_y);
unsigned int std3d_get_cap_flags(void);
void std3d_fill_v_buffer(struct std3dv_buffer *vbuffer,
			 unsigned int packed_color, int fill_mode);
void std3d_set_cap_flags(unsigned int cap_flags);
int std3d_set_fog_color8(unsigned int red8, unsigned int green8,
			 unsigned int blue8);
int std3d_set_fog_table_range_bits(unsigned int start_bits,
				   unsigned int end_bits);
int std3d_set_texture_size_caps(int min_width, int min_height, int max_width,
				int max_height);
void std3d_start_scene(void);
void std3d_end_scene(void);
int std3d_lock_execute_buffer(void);
int std3d_add_vertices(const D3DTLVERTEX *vertices, int count);
int std3d_begin_instructions(void);
int std3d_add_triangles(const struct std3d_render_tri *triangles,
			unsigned int count);
int std3d_execute_buffer(void);
void std3d_set_render_state(std3d_render_state_flags flags);
int std3d_set_palette_conversion_source(const void *palette_rgb888,
					uint8_t alpha);
void std3d_clamp_texture_dimensions(int src_width, int src_height,
				    int *out_width, int *out_height);
int std3d_add_to_texture_cache(struct std3dv_buffer *source,
			       struct std3d_tex_cache_node *node,
			       int color_keyed, int translucent);
void std3d_flush_texture_cache(void);
void std3d_cache_list_append(struct std3d_tex_cache_node *node);
void std3d_cache_list_remove(struct std3d_tex_cache_node *node);
int std3d_query_texture_vid_mem(unsigned int *total_bytes,
				unsigned int *free_bytes);
void std3d_cache_texture_surface(struct std3d_tex_cache_node *node);
int std3d_clear_z_buffer(void);
int std3d_select_best_device(struct std3d_device_caps *required_caps);
int std3d_find_closest_format(const struct color_info *match,
			      struct std3d_tex_fmt *formats,
			      unsigned int count);
void std3d_draw_color_overlay(void);
int std3d_build_viewport_quad(const struct std3d_viewport_rect *rect);
int std3d_set_initial_render_state(void);
int std3d_create_viewport(int width, int height);
int std3d_create_z_buffer(int width, int height);
HRESULT AERON_DXAPI std3d_enum_devices_callback(DxGuid *guid,
						char *device_description,
						char *device_name,
						D3DDEVICEDESC *hardware_desc,
						D3DDEVICEDESC *software_desc,
						void *context);
int AERON_DXAPI std3d_enum_texture_formats(DDSURFACEDESC *surface_desc,
					   void *context);
int std3d_pack_render_bit_depths(int ddbd_flags);
int std3d_mask_z_cmp_caps(unsigned int d3dpcmpcaps);
unsigned int std3d_map_z_cmp_func(unsigned int caps_mask);

#ifdef __cplusplus
}
#endif

#endif
