/* Prints what the engine reads from the game's 3D model files (.opt), what its
 * drawing walk draws from them, and which model file each object type uses,
 * so that another reader of the same files can be checked against the engine.
 * Build with -DXVT_BUILD_TOOLS=ON; it links the engine library.
 *
 * Usage: opt_dump ROOT file < NAMES
 *        opt_dump ROOT draw < NAMES
 *        opt_dump ROOT objects
 *
 * ROOT is bound as the asset folder, as the game's install is: a game name
 * resolves under ROOT/BalanceOfPower first, then under ROOT, in any letter
 * case. NAMES holds one game name per line (ivfiles\xwing.opt); empty lines
 * are skipped. Each model starts with
 *
 *   model "NAME" file="PATH"      or      model "NAME" missing | not_loaded
 *
 * file     xvt_opt_load on the file: the model as the engine's reader
 *          rebuilds it, before any conversion. Nodes are numbered in the order
 *          the reader first reaches them: the roots in order, each node's
 *          children in order, depth first; a node reached again keeps its
 *          number. "node" lines give the type, the name, the children's
 *          numbers ("-" for an empty slot) and payload_count as the engine
 *          keeps it (0 for a name reference, whose count the engine uses as a
 *          cache). Payload lines follow by type: values for small payloads,
 *          64-bit FNV-1a hashes (offset basis 0xcbf29ce484222325, prime
 *          0x100000001b3, 16 hex digits) of the bytes for lists.
 * draw     opt_model_load_file_to_handle, then
 *          opt_model_create_runtime_handle, as opt_model_load_handle does:
 *          the model the game draws, converted to version 2. It is walked once
 *          per pass, by render_scene_draw_model_node's rules: each detail
 *          level L from 1 to the most children any face group has, with node
 *          switch 0, then each switch index S from 1 to one less than the most
 *          children any node switch has, at level 1. Each pass prints, per
 *          component (each root that is not a texture, numbered from 0), every
 *          face drawn, sorted as text, with its texture's name and, at each
 *          corner, the position, texture coordinate and vertex normal, and the
 *          face normal. Floats print with %.9g. The pass line's walk=same says
 *          the engine's own render_scene_draw_object_model, run on the same
 *          model, level and switch, handed the drawing code the same meshes in
 *          the same order (faces, vertices and edges of each, read from its
 *          render.mesh_dropped log lines: the tool never sets up the mesh
 *          queue, so every mesh is logged and dropped).
 * objects  the engine's object type table (record and asset flags, model
 *          index, spec list and line), its craft type map, and the lines of
 *          the six spec lists as fe_disk_io_load_resources reads them.
 *
 * The engine's log runs at DEBUG and goes to stderr, as SDL writes it, except
 * the render.mesh_dropped lines the draw kind reads. Exit status: 0 when every
 * line printed; 1 when a pass's walk differs from the engine's, a table
 * cannot be allocated or a write fails; 2 for bad arguments. A missing or
 * refused model is printed, not judged. */
#include "opt_dump.h"

#include <SDL3/SDL_log.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "asset_dump.h"
#include "xvt/assets/model_texture.h"
#include "xvt/assets/object_type.h"
#include "xvt/assets/opt_model.h"
#include "xvt/render/flight_palette.h"
#include "xvt/render/renderer.h"
#include "xvt/util/memory.h"
#include "xvt_runtime/assets/opt_native.h"
#include "xvt_runtime/log/log.h"
#include "xvt_runtime/storage/file_io.h"
#include "xvt_runtime/storage/storage.h"

enum {
	NAME_CAPACITY = 1024,
	MAX_NODES = 65536,
	PALETTE_BLOCK_BYTES = 12288,
	SHADE_TABLE_BYTES = 4096,
	SUB_PALETTES = 16,
	SUB_PALETTE_COLORS = 256,
	INLINE_PALETTE_BYTES = 768,
	LIST_LINE_CAPACITY = 256,
	CRAFT_TYPES = 96,
	OBJECT_TYPES = 201,
	GLOW_FIRST_SLOT = 256,
	GLOW_COUNT_SLOT = 2304,
};

static const uint64_t FNV_OFFSET_BASIS = 0xcbf29ce484222325u;
static const uint64_t FNV_PRIME = 0x100000001b3u;

static uint64_t fnv1a(const void *memory, size_t count)
{
	const uint8_t *bytes = memory;
	uint64_t hash = FNV_OFFSET_BASIS;
	for (size_t i = 0; i < count; ++i) {
		hash ^= bytes[i];
		hash *= FNV_PRIME;
	}
	return hash;
}

static void print_hash(const char *label, const void *memory, size_t count)
{
	printf(" %s=%016llx", label,
	       (unsigned long long)(memory ? fnv1a(memory, count) : 0));
}

static void print_name(const char *name)
{
	if (name) {
		asset_dump_quote(name, strlen(name));
	} else {
		printf("-");
	}
}

/* ---- the file kind ---------------------------------------------------- */

/* The nodes of one model, numbered in the reader's first-visit order. */
static const struct opt_node *g_nodes[MAX_NODES];
static int g_node_count;

static int node_number(const struct opt_node *node)
{
	for (int i = 0; i < g_node_count; ++i) {
		if (g_nodes[i] == node) {
			return i;
		}
	}
	return -1;
}

/* Numbers node and those below it as the engine's reader first reaches them,
 * keeping for each the vertex count and normal state the reader's payload
 * sizes are worked out from. */
static int g_node_vertices[MAX_NODES];
static int g_node_has_normals[MAX_NODES];

static void number_nodes(const struct opt_node *node, int *vertices,
			 int *has_normals)
{
	if (!node || node_number(node) >= 0 || g_node_count >= MAX_NODES) {
		return;
	}
	int index = g_node_count++;
	g_nodes[index] = node;
	if (node->node_type == OPT_MESHVERTS) {
		*vertices = (int)node->payload_count;
	}
	if (node->node_type == OPT_VERTNORMALS) {
		*has_normals = 1;
	}
	g_node_vertices[index] = *vertices;
	g_node_has_normals[index] = *has_normals;
	int child_vertices = *vertices;
	int child_has_normals = *has_normals;
	for (int i = 0; i < node->child_count; ++i) {
		number_nodes(node->p_children[i], &child_vertices,
			     &child_has_normals);
	}
}

static void print_floats(const char *label, const float *values, int count)
{
	printf(" %s=", label);
	for (int i = 0; i < count; ++i) {
		printf(i ? ",%.9g" : "%.9g", (double)values[i]);
	}
}

/* The bytes of a texture's texels and its own palette: the reader's rule. */
static size_t texture_texel_bytes(const struct opt_texture_data *texture)
{
	size_t pixels = (size_t)texture->width * (size_t)texture->height;
	return pixels == (size_t)texture->texture_size
		       ? (size_t)texture->data_size
		       : pixels;
}

/* The start of the palette a texture carries after its texels, or NULL. */
static const uint8_t *embedded_palette(const struct opt_texture_data *texture)
{
	const uint8_t *after =
		(const uint8_t *)(texture + 1) + texture_texel_bytes(texture);
	return after;
}

static void print_palette_block(const uint8_t *palette);

static void print_texture(const struct opt_node *node)
{
	const struct opt_texture_data *texture = node->payload;
	if (!texture) {
		printf("  texture none\n");
		return;
	}
	size_t texels = texture_texel_bytes(texture);
	printf("  texture width=%d height=%d texture_size=%d data_size=%d "
	       "inline_palettes=%d",
	       texture->width, texture->height, texture->texture_size,
	       texture->data_size, texture->inline_palette_count);
	print_hash("top", texture + 1,
		   (size_t)texture->width * (size_t)texture->height);
	print_hash("texels", texture + 1, texels);
	if (texture->inline_palette_count) {
		/* An inline palette lies after the texels whatever the palette
		 * address says, and is laid out as the block is: 256 shade
		 * bytes per sub-palette, then 256 colors per sub-palette. */
		const uint8_t *palette = embedded_palette(texture);
		printf(" palette=inline");
		if (texture->inline_palette_count != SUB_PALETTES) {
			print_hash("block", palette,
				   (size_t)texture->inline_palette_count *
					   INLINE_PALETTE_BYTES);
			printf("\n");
			return;
		}
		print_palette_block(palette);
		return;
	}
	const uint8_t *palette = (const uint8_t *)texture->palette;
	if (!palette) {
		printf(" palette=none\n");
		return;
	}
	int owner = -1;
	for (int i = 0; i < g_node_count; ++i) {
		const struct opt_node *other = g_nodes[i];
		if (other->node_type == OPT_TEXTURE && other->payload &&
		    !((const struct opt_texture_data *)other->payload)
			     ->inline_palette_count &&
		    embedded_palette(other->payload) == palette) {
			owner = i;
			break;
		}
	}
	printf(" palette=%d", owner);
	print_palette_block(palette);
}

/* Prints the hashes of a palette block: the shade bytes, each sub-palette's
 * colors, and the glow colors the 3D card path makes from them. */
static void print_palette_block(const uint8_t *palette)
{
	print_hash("shade", palette, SHADE_TABLE_BYTES);
	for (int i = 0; i < SUB_PALETTES; ++i) {
		char label[16];
		snprintf(label, sizeof(label), "colors%d", i);
		print_hash(label,
			   palette + SHADE_TABLE_BYTES +
				   i * SUB_PALETTE_COLORS * 2,
			   SUB_PALETTE_COLORS * 2);
	}
	/* The 3D card's glow colors: model_texture_filter_hardware_palette on
	 * a copy of the colors, at the highest texture detail, where it marks
	 * them. */
	static uint16_t colors[SUB_PALETTES * SUB_PALETTE_COLORS];
	memcpy(colors, palette + SHADE_TABLE_BYTES, sizeof(colors));
	int saved_level = g_texture_resolution_level;
	g_texture_resolution_level = 2;
	model_texture_filter_hardware_palette(colors);
	g_texture_resolution_level = saved_level;
	printf(" glow_count=%u glow_first=%u", colors[GLOW_COUNT_SLOT],
	       colors[GLOW_FIRST_SLOT]);
	print_hash("glow", colors, SUB_PALETTE_COLORS * 2);
	printf("\n");
}

/* Prints a face data node's payload line: its edge count and the hashes of
 * its parts, the extra vertex normals sized as the reader sized them. */
static void print_face_payload(int index)
{
	const struct opt_node *node = g_nodes[index];
	const uint8_t *payload = node->payload;
	size_t count = (size_t)node->payload_count;
	int edges;
	memcpy(&edges, payload, sizeof(edges));
	size_t record_bytes = g_opt_source_is_version0 ? 48 : 64;
	const uint8_t *records = payload + 4;
	const uint8_t *normals = records + count * record_bytes;
	const uint8_t *gradients = normals + count * 12;
	const uint8_t *extra = gradients + count * 24;
	int extra_count =
		g_node_has_normals[index] ? 0 : g_node_vertices[index];
	printf("  faces edges=%d", edges);
	print_hash("records", records, count * record_bytes);
	print_hash("normals", normals, count * 12);
	print_hash("gradients", gradients, count * 24);
	printf(" extra_normals=%d", extra_count);
	print_hash("extra", extra, (size_t)extra_count * 12);
	printf("\n");
}

/* Prints a list payload's line: its label and the hash of its bytes. */
static void print_list_payload(const char *label, const void *payload,
			       size_t bytes)
{
	printf("  %s", label);
	print_hash("hash", payload, bytes);
	printf("\n");
}

/* Prints a payload of count floats as a "values" line. */
static void print_values_payload(const void *payload, int count)
{
	printf("  values");
	print_floats("v", payload, count);
	printf("\n");
}

/* Prints a mesh descriptor's fields. */
static void print_descriptor_payload(const uint8_t *payload)
{
	int values[2];
	memcpy(values, payload, sizeof(values));
	printf("  descriptor mesh_type=%d flags=%d", values[0], values[1]);
	print_floats("span", (const float *)(payload + 8), 3);
	print_floats("center", (const float *)(payload + 20), 3);
	print_floats("min", (const float *)(payload + 32), 3);
	print_floats("max", (const float *)(payload + 44), 3);
	int target;
	memcpy(&target, payload + 56, sizeof(target));
	printf(" target_id=%d", target);
	print_floats("target", (const float *)(payload + 60), 3);
	printf("\n");
}

/* Prints node index's payload line, by its type; nothing for a node without
 * a payload or of a type with none. */
static void print_payload(int index)
{
	const struct opt_node *node = g_nodes[index];
	const uint8_t *payload = node->payload;
	size_t count = (size_t)node->payload_count;
	if (node->node_type == OPT_TEXTURE) {
		print_texture(node);
		return;
	}
	if (!payload) {
		return;
	}
	switch (node->node_type) {
	case OPT_FACEDATA:
	case OPT_FACEDATA_QUAD_MESH:
	case OPT_FACEDATA_FACE_SET:
	case OPT_FACEDATA_TRIANGLE_STRIP_SET:
		print_face_payload(index);
		break;
	case OPT_MESHVERTS:
	case OPT_VERTNORMALS:
		print_list_payload("vectors", payload, count * 12);
		break;
	case OPT_TEXCOORDS:
		print_list_payload("coords", payload, count * 8);
		break;
	case OPT_MATERIAL:
		print_list_payload("materials", payload, count * 56);
		break;
	case OPT_FACEGROUP:
		printf("  levels");
		print_floats("distances", (const float *)payload, (int)count);
		printf("\n");
		break;
	case OPT_HARDPOINT: {
		int type;
		memcpy(&type, payload, sizeof(type));
		printf("  hardpoint type=%d", type);
		print_floats("at", (const float *)(payload + 4), 3);
		printf("\n");
		break;
	}
	case OPT_MESHDESC:
		print_descriptor_payload(payload);
		break;
	case OPT_TRANSFORM:
	case OPT_ROTSCALE:
		print_values_payload(payload, 12);
		break;
	case OPT_ROTATION:
		print_values_payload(payload, 9);
		break;
	case OPT_TRANSLATION:
	case OPT_SCALE:
	case OPT_BASE_COLOR:
		print_values_payload(payload, 3);
		break;
	case OPT_NODEREF:
		printf("  ref ");
		print_name((const char *)payload);
		printf("\n");
		break;
	default:
		break;
	}
}

static void dump_file(const char *name, const char *relative)
{
	int version = -1;
	unsigned int native_size = 0;
	uint16_t handle = xvt_opt_load(name, &version, &native_size);
	if (!handle) {
		printf("model ");
		asset_dump_quote(name, strlen(name));
		printf(" not_loaded version=%d\n", version);
		return;
	}
	struct optimized_poly_object *model = memory_get_handle_block(handle);
	g_opt_source_is_version0 = version == 0;
	g_node_count = 0;
	int vertices = 0;
	int has_normals = 0;
	for (int i = 0; i < model->root_node_count; ++i) {
		number_nodes(model->root_nodes[i], &vertices, &has_normals);
	}
	printf("model ");
	asset_dump_quote(name, strlen(name));
	printf(" file=");
	asset_dump_quote(relative, strlen(relative));
	printf(" version=%d roots=%d nodes=%d reserved=%u\n", version,
	       model->root_node_count, g_node_count, model->reserved);
	for (int i = 0; i < model->root_node_count; ++i) {
		printf("root %d node=%d\n", i,
		       node_number(model->root_nodes[i]));
	}
	for (int i = 0; i < g_node_count; ++i) {
		const struct opt_node *node = g_nodes[i];
		printf("node %d type=%d name=", i, (int)node->node_type);
		print_name(node->p_name);
		printf(" children=");
		for (int c = 0; c < node->child_count; ++c) {
			int child = node_number(node->p_children[c]);
			printf(c ? "," : "");
			if (child < 0) {
				printf("-");
			} else {
				printf("%d", child);
			}
		}
		printf(" payload_count=%lld\n", (long long)node->payload_count);
		print_payload(i);
	}
	memory_free_handle(handle);
}

/* ---- the objects kind ------------------------------------------------- */

static void dump_list(int group, const char *resolution)
{
	static const char *const prefixes[3] = {"SPEC", "SPEC2", "SPEC3"};
	char path[64];
	snprintf(path, sizeof(path), "ivfiles\\%s%s.LST", prefixes[group],
		 resolution);
	char relative[NAME_CAPACITY];
	printf("list %d %s ", group, resolution);
	asset_dump_quote(path, strlen(path));
	if (!asset_dump_resolve(path, relative, sizeof(relative))) {
		printf(" missing\n");
		return;
	}
	printf(" file=");
	asset_dump_quote(relative, strlen(relative));
	printf("\n");
	AeronFile *stream = xvt_storage_open(path, "rb");
	if (!stream) {
		printf("list %d %s not_opened\n", group, resolution);
		return;
	}
	char line[LIST_LINE_CAPACITY];
	int entry = 0;
	/* fe_disk_io_load_resources's reading of a list line. */
	while (xvt_file_gets(line, sizeof(line), stream) != NULL) {
		int end = 0;
		for (; line[end] != '\0' && line[end] != '\n'; ++end) {
			if (line[end] == '\r') {
				break;
			}
		}
		line[end] = '\0';
		if (line[0] == '\0') {
			continue;
		}
		printf("entry %d %s %d ", group, resolution, entry++);
		asset_dump_quote(line, strlen(line));
		printf("\n");
	}
	AeronVfs_Close(stream);
}

static void dump_objects(void)
{
	for (int t = 0; t < OBJECT_TYPES; ++t) {
		const struct object_type_info *info = &g_object_type_table[t];
		printf("object %d record_flags=0x%02x asset_flags=0x%02x "
		       "model_index=%u texture_group=%u resource_index=%u\n",
		       t, info->record_flags, info->asset_flags,
		       info->model_index, info->texture_group,
		       info->resource_index);
	}
	for (int c = 0; c < CRAFT_TYPES; ++c) {
		printf("craft %d object=%u\n", c,
		       g_craft_type_to_object_type[c]);
	}
	for (int group = 0; group < 3; ++group) {
		dump_list(group, "640");
		dump_list(group, "320");
	}
}

/* ---- main ------------------------------------------------------------- */

int main(int argc, char **argv)
{
	int kind_file = argc == 3 && strcmp(argv[2], "file") == 0;
	int kind_draw = argc == 3 && strcmp(argv[2], "draw") == 0;
	int kind_objects = argc == 3 && strcmp(argv[2], "objects") == 0;
	if (!kind_file && !kind_draw && !kind_objects) {
		fprintf(stderr,
			"Usage: %s ROOT file|draw < NAMES\n       %s ROOT objects\n",
			argv[0], argv[0]);
		return 2;
	}
	xvt_log_set_level(AERON_LOG_DEBUG);
	SDL_SetLogPriorities(SDL_LOG_PRIORITY_VERBOSE);
	opt_draw_capture_log();
	if (!asset_dump_bind_root("opt_dump", argv[1])) {
		return 1;
	}
	/* The display opt_model_create_runtime_handle builds for: 16 bits per
	 * pixel at full brightness, the software path, every texture level
	 * kept. */
	g_flight_bytes_per_pixel = 2;
	g_flight_brightness_scale_q8 = 256;
	g_use_hardware3d = 0;
	g_mipmapping_enabled = 0;
	g_texture_resolution_level = 1;
	if (kind_objects) {
		dump_objects();
		return fflush(stdout) != 0;
	}
	int status = 0;
	char line[NAME_CAPACITY];
	while (fgets(line, sizeof(line), stdin) != NULL) {
		line[strcspn(line, "\r\n")] = '\0';
		if (line[0] == '\0') {
			continue;
		}
		char relative[NAME_CAPACITY];
		if (!asset_dump_resolve(line, relative, sizeof(relative))) {
			printf("model ");
			asset_dump_quote(line, strlen(line));
			printf(" missing\n");
			continue;
		}
		if (kind_file) {
			dump_file(line, relative);
		} else if (!opt_draw_dump(line, relative)) {
			status = 1;
		}
	}
	return fflush(stdout) != 0 ? 1 : status;
}
