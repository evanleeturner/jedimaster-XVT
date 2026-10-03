#include "xvt_remaster/crt.h"
#include "aeron/scene/image_cache.h"
#include <stdlib.h>
#include <string.h>

static AeronShader *g_vs, *g_fs;
static AeronGraphicsPipeline *g_pipeline;
static AeronSampler *g_color_sampler, *g_mask_sampler;
static AeronTexture *g_mask, *g_color;

static struct {
	AeronTexture *texture;
	int width, height;
	unsigned size;
	uint8_t bytes[480];
} g_masks[3];

static float g_uniform[8];
static AeronRectI g_viewport;

static int crt_resources(void)
{
	if (g_pipeline) {
		return 1;
	}
	g_vs = Aeron_CreateShader(
		&(AeronShaderDesc){.name = "hud_crt.vert",
				   .stage = AERON_SHADER_STAGE_VERTEX,
				   .uniform_buffer_count = 1});
	g_fs = Aeron_CreateShader(
		&(AeronShaderDesc){.name = "hud_crt.frag",
				   .stage = AERON_SHADER_STAGE_FRAGMENT,
				   .sampler_count = 2});
	if (!g_vs || !g_fs) {
		return 0;
	}
	AeronColorTargetStateDesc color = {
		.format = AERON_TEXTURE_FORMAT_RGBA16_FLOAT,
		.blend = {.enabled = 1,
			  .src_color = AERON_BLEND_ONE,
			  .dst_color = AERON_BLEND_ONE_MINUS_SRC_ALPHA,
			  .color_op = AERON_BLEND_OP_ADD,
			  .src_alpha = AERON_BLEND_ONE,
			  .dst_alpha = AERON_BLEND_ONE_MINUS_SRC_ALPHA,
			  .alpha_op = AERON_BLEND_OP_ADD}};
	g_pipeline = Aeron_CreateGraphicsPipeline(&(AeronGraphicsPipelineDesc){
		.vertex_shader = g_vs,
		.fragment_shader = g_fs,
		.primitive_type = AERON_PRIMITIVE_TRIANGLE_STRIP,
		.cull_mode = AERON_CULL_NONE,
		.color_target_count = 1,
		.color_targets = &color});
	AeronSamplerDesc sampler = {.min_filter = AERON_FILTER_LINEAR,
				    .mag_filter = AERON_FILTER_LINEAR,
				    .address_u = AERON_ADDRESS_CLAMP_TO_EDGE,
				    .address_v = AERON_ADDRESS_CLAMP_TO_EDGE};
	g_color_sampler = Aeron_CreateSampler(&sampler);
	sampler.min_filter = sampler.mag_filter = AERON_FILTER_NEAREST;
	g_mask_sampler = Aeron_CreateSampler(&sampler);
	return g_pipeline && g_color_sampler && g_mask_sampler;
}

static int prepare_mask(AeronCommandBuffer *cmd, unsigned index,
			const uint8_t *bytes, unsigned size, int width,
			int height)
{
	if (g_masks[index].texture && width == g_masks[index].width &&
	    height == g_masks[index].height && size == g_masks[index].size &&
	    (!size || !memcmp(bytes, g_masks[index].bytes, size))) {
		return 1;
	}
	uint8_t *rgba = calloc((size_t)width * height, 4);
	if (!rgba) {
		return 0;
	}
	if (!size) {
		for (int i = 0; i < width * height; ++i) {
			rgba[i * 4 + 3] = 255;
		}
	} else {
		const uint8_t *p = bytes;
		const uint8_t *end = bytes + size;
		for (int y = 0; y < height; ++y) {
			if (p == end) {
				goto invalid;
			}
			int8_t parity = (int8_t)*p++;
			for (int x = 0; x < width;) {
				if (p == end) {
					goto invalid;
				}
				int run = *p++;
				if (!run) {
					if (p == end) {
						goto invalid;
					}
					run = *p++;
					if (!run) {
						if (p == end) {
							goto invalid;
						}
						run = (*p++) + 256;
					}
					run += 255;
				}
				int count = run < width - x ? run : width - x;
				if (parity >= 0) {
					for (int i = 0; i < count; ++i) {
						rgba[((size_t)y * width + x +
						      i) * 4 +
						     3] = 255;
					}
				}
				x += count;
				parity = (int8_t)-parity;
			}
		}
	}
	AeronTexture *mask = Aeron_ImageUploadRgba8(
		cmd, rgba, width, height, (size_t)width * 4,
		AERON_TEXTURE_FORMAT_RGBA8_UNORM, AERON_COLOR_SPACE_LINEAR_SRGB,
		AERON_IMAGE_ALPHA_STRAIGHT, false, "XvT CRT mask");
	free(rgba);
	if (!mask) {
		return 0;
	}
	Aeron_DestroyTexture(g_masks[index].texture);
	g_masks[index].texture = mask;
	g_masks[index].width = width;
	g_masks[index].height = height;
	g_masks[index].size = size;
	if (size) {
		memcpy(g_masks[index].bytes, bytes, size);
	}
	return 1;
invalid:
	free(rgba);
	return 0;
}

int xvt_crt_prepare_view(const struct xvt_snap_preview *preview,
			 const struct xvt_snap_cockpit_layout *layout,
			 AeronTexture *color, int width, int height,
			 float scale, float ox, float oy)
{
	g_color = NULL;
	if (!preview || !color) {
		return 1;
	}
	struct xvt_snap_rect r = preview->destination;
	unsigned index = preview->mask_index;
	unsigned size = index < 3 ? layout->mask_bytes[index] : 0;
	if (size > sizeof g_masks[0].bytes || r.width <= 0 || r.height <= 0 ||
	    r.width > preview->camera.screen_width ||
	    r.height > preview->camera.screen_height || width <= 0 ||
	    height <= 0) {
		return 0;
	}
	if (index >= 3 || !g_masks[index].texture ||
	    g_masks[index].width != r.width ||
	    g_masks[index].height != r.height || g_masks[index].size != size ||
	    memcmp(g_masks[index].bytes, layout->masks[index], size)) {
		return 0;
	}
	g_mask = g_masks[index].texture;
	float x = r.x * scale + ox, y = r.y * scale + oy, w = r.width * scale,
	      h = r.height * scale;
	g_uniform[0] = 2 * x / width - 1;
	g_uniform[1] = 1 - 2 * (y + h) / height;
	g_uniform[2] = 2 * w / width;
	g_uniform[3] = 2 * h / height;
	g_uniform[4] = g_uniform[5] = 0;
	g_uniform[6] = g_uniform[7] = 1;
	g_viewport = (AeronRectI){0, 0, width, height};
	g_color = color;
	return 1;
}

void xvt_crt_draw(AeronRenderPass *pass)
{
	if (!g_color) {
		return;
	}
	Aeron_SetViewport(pass, &g_viewport);
	Aeron_SetScissor(pass, &g_viewport);
	Aeron_BindGraphicsPipeline(pass, g_pipeline);
	Aeron_BindTextureSampler(pass, AERON_SHADER_STAGE_FRAGMENT, 0, g_color,
				 g_color_sampler);
	Aeron_BindTextureSampler(pass, AERON_SHADER_STAGE_FRAGMENT, 1, g_mask,
				 g_mask_sampler);
	Aeron_BindUniformData(pass, AERON_SHADER_STAGE_VERTEX, 0, g_uniform,
			      sizeof g_uniform);
	Aeron_Draw(pass, 4, 0);
}

void xvt_crt_shutdown(void)
{
	g_color = NULL;
	for (unsigned index = 0; index < 3; ++index) {
		Aeron_DestroyTexture(g_masks[index].texture);
	}
	memset(g_masks, 0, sizeof g_masks);
	g_mask = NULL;
	Aeron_DestroyGraphicsPipeline(g_pipeline);
	g_pipeline = NULL;
	Aeron_DestroyShader(g_vs);
	Aeron_DestroyShader(g_fs);
	g_vs = g_fs = NULL;
	Aeron_DestroySampler(g_color_sampler);
	Aeron_DestroySampler(g_mask_sampler);
	g_color_sampler = g_mask_sampler = NULL;
}

int xvt_crt_prepare_resources(AeronCommandBuffer *cmd,
			      const struct xvt_cockpit_resources *resources)
{
	if (!crt_resources()) {
		return 0;
	}
	for (unsigned index = 0; index < 3; ++index) {
		const struct xvt_snap_hud_element *element =
			&resources->definition.layout
				 .elements[index * HUD_INSTRUMENTS_PER_SET + 2];
		unsigned size = resources->definition.layout.mask_bytes[index];
		if (!element->selector || !element->color_index) {
			continue;
		}
		if (size > sizeof g_masks[index].bytes ||
		    !prepare_mask(cmd, index,
				  resources->definition.layout.masks[index],
				  size, element->selector,
				  element->color_index)) {
			return 0;
		}
	}
	return 1;
}
