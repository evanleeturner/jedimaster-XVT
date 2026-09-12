/* XvT star axes with OpenTIE's TIE98 rounded, smoothly projected coverage. */
#include "xvt_remaster/sky_stars.h"
#include "aeron/aeron.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

enum { STAR_COUNT = 3072, STAR_GRID_SPAN = 32 };

typedef struct StarInstance {
	float axis[3];
	float intensity;
} StarInstance;

typedef struct StarVertexUniform {
	float view_proj[16];
	float pixel_to_clip[2];
	float half_size_px[2];
	float brightness;
	float padding[3];
} StarVertexUniform;

/* Match the float4 instance and uniform layouts consumed by HLSL. */
typedef char StarInstanceLayout[(sizeof(StarInstance) == 16) ? 1 : -1];
typedef char StarVertexUniformLayout[(sizeof(StarVertexUniform) == 96) ? 1 : -1];

struct XvtRemasterSkyStars {
	AeronShader* vertex_shader;
	AeronShader* fragment_shader;
	AeronGraphicsPipeline* pipeline;
	AeronSampleCount pipeline_samples;
	AeronBuffer* instances;
	StarVertexUniform vertex_uniform;
	uint8_t position_indices[STAR_COUNT];
	float intensities[STAR_COUNT];
	uint32_t instance_count;
	uint16_t density_divisor;
};

typedef struct StarRandomState {
	uint16_t seed, value;
} StarRandomState;

/* GameRand's LFSR with private state, as in OpenTIE: renderer initialization
 * must not consume simulation randomness. */
static uint16_t NextStarRandom(StarRandomState* rng) {
	for (int bit = 0; bit < 16; ++bit) {
		uint16_t feedback = (rng->seed >> 8) ^ (uint16_t)(2u * (rng->seed & 0xffu));
		rng->value = (uint16_t)((rng->value << 1) | (rng->seed >> 15));
		rng->seed = (uint16_t)(rng->seed * 2u + ((feedback & 0x80u) != 0));
	}
	return rng->value;
}

static void InitializeStarTables(XvtRemasterSkyStars* stars) {
	StarRandomState rng = { .seed = 0x2357u };
	for (unsigned i = 0; i < STAR_COUNT; ++i) {
		float shade = (float)((NextStarRandom(&rng) & 15u) + 8u) / 31.0f;
		/* All original levels are above the linear segment of sRGB. */
		stars->intensities[i] = powf((shade + 0.055f) / 1.055f, 2.4f);
	}
	for (unsigned i = 0; i < STAR_COUNT; ++i) {
		uint8_t index;
		do {
			index = (uint8_t)(NextStarRandom(&rng) & 127u);
		} while (index > 124);
		stars->position_indices[i] = index;
	}
}

static int UploadStarInstances(XvtRemasterSkyStars* stars, AeronCommandBuffer* cmd,
							   uint16_t density_divisor) {
	if (stars->density_divisor == density_divisor)
		return 1;
	unsigned grid_size = STAR_GRID_SPAN / density_divisor;
	float grid_step = 64.0f / (float)grid_size;
	static const unsigned column_axis[3] = { 0, 0, 1 };
	static const unsigned row_axis[3] = { 1, 2, 2 };
	StarInstance instances[STAR_COUNT];
	uint32_t count = 0;
	for (unsigned plane = 0; plane < 3; ++plane) {
		for (unsigned row = 0; row < grid_size; ++row) {
			for (unsigned column = 0; column < grid_size; ++column, ++count) {
				int index = stars->position_indices[count];
				StarInstance* star = &instances[count];
				star->axis[0] = (float)(-32 + index / 25 - 2);
				star->axis[1] = (float)(-32 + (index % 25) / 5 - 2);
				star->axis[2] = (float)(-32 + index % 5 - 2);
				star->axis[column_axis[plane]] += grid_step * (float)column;
				star->axis[row_axis[plane]] += grid_step * (float)row;
				star->intensity = stars->intensities[count];
			}
		}
	}
	if (!Aeron_UploadBufferDataCmd(cmd, stars->instances, 0, instances, count * sizeof instances[0]))
		return 0;
	stars->density_divisor = density_divisor;
	stars->instance_count = count;
	return 1;
}

static int PrepareStarPipeline(XvtRemasterSkyStars* stars, AeronSampleCount sample_count) {
	if (stars->pipeline && stars->pipeline_samples == sample_count)
		return 1;
	if (stars->pipeline) {
		Aeron_DestroyGraphicsPipeline(stars->pipeline);
		stars->pipeline = NULL;
	}
	AeronColorTargetStateDesc color_target = {
        .format = AERON_TEXTURE_FORMAT_RGBA16_FLOAT,
        .blend = {
            .enabled = 1,
            .src_color = AERON_BLEND_ONE,
            .dst_color = AERON_BLEND_ONE_MINUS_SRC_ALPHA,
            .color_op = AERON_BLEND_OP_ADD,
            .src_alpha = AERON_BLEND_ONE,
            .dst_alpha = AERON_BLEND_ONE_MINUS_SRC_ALPHA,
            .alpha_op = AERON_BLEND_OP_ADD,
        },
    };
	stars->pipeline = Aeron_CreateGraphicsPipeline(&(AeronGraphicsPipelineDesc) {
		.vertex_shader = stars->vertex_shader,
		.fragment_shader = stars->fragment_shader,
		.primitive_type = AERON_PRIMITIVE_TRIANGLES,
		.cull_mode = AERON_CULL_NONE,
		.depth_format = AERON_TEXTURE_FORMAT_D32_FLOAT,
		.depth = { .depth_test = 1, .depth_write = 0, .compare = AERON_COMPARE_GREATER_EQUAL },
		.color_target_count = 1,
		.color_targets = &color_target,
		.sample_count = sample_count,
	});
	stars->pipeline_samples = stars->pipeline ? sample_count : 0;
	return stars->pipeline != NULL;
}

XvtRemasterSkyStars* XvtRemasterSkyStars_Create(void) {
	XvtRemasterSkyStars* stars = calloc(1, sizeof *stars);
	if (!stars)
		return NULL;
	stars->vertex_shader = Aeron_CreateShader(&(AeronShaderDesc) {
		.name = "sky_stars.vert",
		.stage = AERON_SHADER_STAGE_VERTEX,
		.uniform_buffer_count = 1,
		.storage_buffer_count = 1,
	});
	stars->fragment_shader = Aeron_CreateShader(&(AeronShaderDesc) {
		.name = "sky_stars.frag",
		.stage = AERON_SHADER_STAGE_FRAGMENT,
	});
	stars->instances = Aeron_CreateBuffer(&(AeronBufferDesc) {
		.size = STAR_COUNT * sizeof(StarInstance),
		.usage = AERON_BUFFER_USAGE_STORAGE,
		.memory_usage = AERON_MEMORY_USAGE_GPU_ONLY,
		.debug_name = "xvt.sky.stars.instances",
	});
	if (!stars->vertex_shader || !stars->fragment_shader || !stars->instances) {
		Aeron_LogError("xvt.remaster", "starfield: GPU resource creation failed");
		XvtRemasterSkyStars_Destroy(stars);
		return NULL;
	}
	InitializeStarTables(stars);
	return stars;
}

void XvtRemasterSkyStars_Destroy(XvtRemasterSkyStars* stars) {
	if (!stars)
		return;
	if (stars->pipeline)
		Aeron_DestroyGraphicsPipeline(stars->pipeline);
	if (stars->vertex_shader)
		Aeron_DestroyShader(stars->vertex_shader);
	if (stars->fragment_shader)
		Aeron_DestroyShader(stars->fragment_shader);
	if (stars->instances)
		Aeron_DestroyBuffer(stars->instances);
	free(stars);
}

int XvtRemasterSkyStars_Prepare(XvtRemasterSkyStars* stars, AeronCommandBuffer* cmd,
								const AeronScene3D* scene, const XvtRemasterSkyStarsParams* params) {
	if (!stars || !cmd || !scene || !params || !params->density_divisor ||
		params->density_divisor > STAR_GRID_SPAN || params->classic_pixel_scale <= 0)
		return 0;
	const float* view_proj = AeronScene_JitteredViewProj(scene);
	int render_w, render_h, output_w, output_h;
	AeronScene_RenderDims(scene, &render_w, &render_h);
	AeronScene_RtDims(scene, &output_w, &output_h);
	if (!view_proj || render_w <= 0 || render_h <= 0 || output_w <= 0 || output_h <= 0)
		return 0;
	if (!UploadStarInstances(stars, cmd, params->density_divisor))
		return 0;
	StarVertexUniform* uniform = &stars->vertex_uniform;
	memcpy(uniform->view_proj, view_proj, sizeof uniform->view_proj);
	uniform->pixel_to_clip[0] = 2.0f / (float)render_w;
	uniform->pixel_to_clip[1] = 2.0f / (float)render_h;
	/* Fitted classic pixels, scaled independently into the internal target. */
	uniform->half_size_px[0] = 0.5f * params->classic_pixel_scale * render_w / output_w;
	uniform->half_size_px[1] = 0.5f * params->classic_pixel_scale * render_h / output_h;
	uniform->brightness = params->exposure * params->brightness;
	return 1;
}

void XvtRemasterSkyStars_Draw(AeronCommandBuffer* command_buffer, AeronRenderPass* render_pass, int rt_w,
							  int rt_h, void* user) {
	(void)rt_w;
	(void)rt_h;
	XvtRemasterSkyStars* stars = user;
	if (!stars || !render_pass || !stars->instance_count)
		return;
	if (!PrepareStarPipeline(stars, Aeron_RenderPassGetSampleCount(render_pass))) {
		Aeron_CommandBufferSetFailure(command_buffer, "Starfield pipeline preparation failed");
		return;
	}
	Aeron_BindGraphicsPipeline(render_pass, stars->pipeline);
	Aeron_BindStorageBuffer(render_pass, AERON_SHADER_STAGE_VERTEX, 0, stars->instances);
	Aeron_BindUniformData(render_pass, AERON_SHADER_STAGE_VERTEX, 0, &stars->vertex_uniform,
						  sizeof stars->vertex_uniform);
	Aeron_DrawInstanced(render_pass, 6, stars->instance_count, 0);
}
