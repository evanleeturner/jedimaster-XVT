/* The draw kind of opt_dump: what the game draws from a model, and the check
 * of that walk against the engine's own drawing (see opt_dump.c). */
#include <SDL3/SDL_log.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "asset_dump.h"
#include "opt_dump.h"
#include "xvt/assets/opt_model.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/transfm2.h"
#include "xvt/render/render_scene.h"
#include "xvt/render/renderer.h"
#include "xvt/render/sw3d.h"
#include "xvt/util/memory.h"
#include "xvt_runtime/assets/opt_native.h"

enum {
	NAME_CAPACITY = 1024,
	DRAW_SLOT = 1,
	MAX_LOGGED_MESHES = 4096,
};

struct logged_mesh {
	int faces;
	int vertices;
	int edges;
};

static struct logged_mesh g_engine_meshes[MAX_LOGGED_MESHES];
static int g_engine_mesh_count;
static int g_engine_mesh_overflow;
static int g_capturing;
static SDL_LogOutputFunction g_default_output;
static void *g_default_output_data;

/* Takes the drawing code's render.mesh_dropped lines while capturing; passes
 * every other line on to SDL's own output. */
static void SDLCALL capture_log(void *userdata, int category,
				SDL_LogPriority priority, const char *message)
{
	const char *event = strstr(message, "render.mesh_dropped ");
	if (g_capturing && event) {
		const char *faces = strstr(event, " mesh_faces=");
		const char *vertices = strstr(event, " mesh_vertices=");
		const char *edges = strstr(event, " mesh_edges=");
		if (g_engine_mesh_count >= MAX_LOGGED_MESHES || !faces ||
		    !vertices || !edges) {
			g_engine_mesh_overflow = 1;
			return;
		}
		struct logged_mesh *mesh =
			&g_engine_meshes[g_engine_mesh_count++];
		sscanf(faces, " mesh_faces=%d", &mesh->faces);
		sscanf(vertices, " mesh_vertices=%d", &mesh->vertices);
		sscanf(edges, " mesh_edges=%d", &mesh->edges);
		return;
	}
	g_default_output(userdata ? userdata : g_default_output_data, category,
			 priority, message);
}

void opt_draw_capture_log(void)
{
	SDL_GetLogOutputFunction(&g_default_output, &g_default_output_data);
	SDL_SetLogOutputFunction(capture_log, NULL);
}

/* The state of render_scene_draw_model_node's mesh this walk needs. */
struct walk_mesh {
	const struct opt_node *vertices; /* the OPT_MESHVERTS node */
	const struct opt_node *coords;	 /* the OPT_TEXCOORDS node */
	const struct opt_node *normals;	 /* the OPT_VERTNORMALS node */
	/* The texture node whose payload is the mesh's p_material; NULL with
	 * material_set for the engine's built-in white texture. */
	const struct opt_node *texture;
	int material_set;
};

/* One face, as text, and the walk's meshes. */
struct face_line {
	int component;
	char *text;
};

static struct face_line *g_faces;
static int g_face_count;
static int g_face_capacity;
static struct logged_mesh g_walk_meshes[MAX_LOGGED_MESHES];
static int g_walk_mesh_count;
static int g_walk_overflow;
/* The walk's copy of g_cur_texture_desc: the texture node last walked, NULL
 * for the built-in white texture. */
static const struct opt_node *g_walk_texture;
static int g_walk_component;
static int g_walk_lod;
static int g_walk_switch;

static void add_face(char *text)
{
	if (g_face_count == g_face_capacity) {
		int capacity = g_face_capacity ? g_face_capacity * 2 : 1024;
		struct face_line *grown =
			realloc(g_faces, (size_t)capacity * sizeof(*grown));
		if (!grown) {
			fprintf(stderr, "opt_dump: out of memory\n");
			exit(1);
		}
		g_faces = grown;
		g_face_capacity = capacity;
	}
	g_faces[g_face_count].component = g_walk_component;
	g_faces[g_face_count].text = text;
	++g_face_count;
}

struct text_buffer {
	char *bytes;
	size_t length;
	size_t capacity;
};

static void text_add(struct text_buffer *text, const char *format, ...)
	__attribute__((format(printf, 2, 3)));

static void text_add(struct text_buffer *text, const char *format, ...)
{
	va_list args;
	for (;;) {
		va_start(args, format);
		int wanted =
			vsnprintf(text->bytes + text->length,
				  text->capacity - text->length, format, args);
		va_end(args);
		if (wanted >= 0 &&
		    (size_t)wanted < text->capacity - text->length) {
			text->length += (size_t)wanted;
			return;
		}
		size_t capacity = text->capacity * 2 + (size_t)wanted + 64;
		char *grown = realloc(text->bytes, capacity);
		if (!grown) {
			fprintf(stderr, "opt_dump: out of memory\n");
			exit(1);
		}
		text->bytes = grown;
		text->capacity = capacity;
	}
}

static void text_vector(struct text_buffer *text, const struct opt_node *list,
			int index, int components)
{
	if (!list || !list->payload || index < 0 ||
	    index >= (int)list->payload_count) {
		text_add(text, "bad");
		return;
	}
	const float *values = (const float *)list->payload + index * components;
	for (int i = 0; i < components; ++i) {
		text_add(text, i ? ",%.9g" : "%.9g", (double)values[i]);
	}
}

/* Records one face node's draw: the mesh's counts and each face as text. */
static void walk_draw_faces(const struct opt_node *node,
			    const struct walk_mesh *mesh)
{
	const struct opt_packed_face_data *data = node->payload;
	int count = (int)node->payload_count;
	const struct opt_vector *face_normals =
		(const struct opt_vector *)&data->records[count];
	const struct face_texture_gradients *gradients =
		(const struct face_texture_gradients *)&face_normals[count];
	const struct opt_vector *generated = &gradients[count].u_axis;
	if (g_walk_mesh_count < MAX_LOGGED_MESHES) {
		struct logged_mesh *logged =
			&g_walk_meshes[g_walk_mesh_count++];
		logged->faces = count;
		logged->vertices =
			mesh->vertices ? (int)mesh->vertices->payload_count : 0;
		logged->edges = data->edge_count;
	} else {
		g_walk_overflow = 1;
	}
	for (int f = 0; f < count; ++f) {
		const struct opt_packed_face_record *face = &data->records[f];
		int corners = face->vertex_indices[3] == -1 ? 3 : 4;
		struct text_buffer text = {0};
		text_add(&text, "face tex=");
		const char *name = mesh->texture ? mesh->texture->p_name : NULL;
		if (!mesh->texture) {
			text_add(&text, "white");
		} else if (name) {
			text_add(&text, "\"%s\"", name);
		} else {
			text_add(&text, "-");
		}
		text_add(&text, " corners=%d p=", corners);
		for (int c = 0; c < corners; ++c) {
			text_add(&text, c ? ";" : "");
			text_vector(&text, mesh->vertices,
				    face->vertex_indices[c], 3);
		}
		text_add(&text, " uv=");
		for (int c = 0; c < corners; ++c) {
			text_add(&text, c ? ";" : "");
			text_vector(&text, mesh->coords,
				    face->tex_coord_indices[c], 2);
		}
		text_add(&text, " n=");
		for (int c = 0; c < corners; ++c) {
			text_add(&text, c ? ";" : "");
			int index = face->normal_indices[c];
			if (mesh->normals) {
				text_vector(&text, mesh->normals, index, 3);
			} else if (mesh->vertices && index >= 0 &&
				   index < (int)mesh->vertices->payload_count) {
				text_add(&text, "%.9g,%.9g,%.9g",
					 (double)generated[index].x,
					 (double)generated[index].y,
					 (double)generated[index].z);
			} else {
				text_add(&text, "bad");
			}
		}
		text_add(&text, " fn=%.9g,%.9g,%.9g", (double)face_normals[f].x,
			 (double)face_normals[f].y, (double)face_normals[f].z);
		add_face(text.bytes);
	}
}

/* The child a node switch picks for the walk's switch index, counted from 1:
 * the index plus 1, or the last child when there are fewer. */
static int walk_switch_pick(const struct opt_node *node)
{
	int pick = g_walk_switch + 1;
	return pick > node->child_count ? node->child_count : pick;
}

/* The child a face group picks for the walk's detail level, counted from 1:
 * the level itself (1 for level 0), or -1 when the group has fewer
 * children. */
static int walk_lod_pick(const struct opt_node *node)
{
	if (g_walk_lod == 0) {
		return 1;
	}
	return node->child_count < g_walk_lod ? -1 : g_walk_lod;
}

/* What render_scene_draw_model_node does with a node itself, before its
 * children: sets the walk's state, draws a face data node's faces, and gives
 * a face group's or node switch's pick (0 for none). */
static void walk_node_effect(struct opt_node *node, struct walk_mesh *mesh,
			     int *lod_pick, int *switch_pick)
{
	if (!node->payload) {
		if (node->node_type == OPT_TEXTURE) {
			/* A texture without data leaves the mesh without one,
			 * as the engine's NULL p_material does. */
			mesh->texture = NULL;
			mesh->material_set = 0;
		} else if (node->node_type == OPT_NODESWITCH) {
			*switch_pick = walk_switch_pick(node);
		}
		return;
	}
	switch (node->node_type) {
	case OPT_FACEDATA:
	case OPT_FACEDATA_QUAD_MESH:
	case OPT_FACEDATA_FACE_SET:
	case OPT_FACEDATA_TRIANGLE_STRIP_SET:
		if (!mesh->material_set) {
			mesh->texture = g_walk_texture;
			mesh->material_set = 1;
		}
		walk_draw_faces(node, mesh);
		break;
	case OPT_MESHVERTS:
		mesh->vertices = node;
		break;
	case OPT_VERTNORMALS:
		mesh->normals = node;
		break;
	case OPT_TEXCOORDS:
		mesh->coords = node;
		break;
	case OPT_TEXTURE:
		mesh->texture = node;
		mesh->material_set = 1;
		g_walk_texture = node;
		break;
	case OPT_FACEGROUP:
		*lod_pick = walk_lod_pick(node);
		break;
	case OPT_NODESWITCH:
		*switch_pick = walk_switch_pick(node);
		break;
	default:
		break;
	}
}

/* render_scene_draw_model_node's walk, keeping what decides the faces drawn:
 * the name references, the detail level and switch picks, and the vertex,
 * coordinate, normal and texture state, with the same copies of the mesh. */
static void walk_node(struct optimized_poly_object *model,
		      struct opt_node *node, struct walk_mesh *mesh)
{
	while (node && node->node_type == OPT_NODEREF) {
		node = xvt_opt_resolve_cached(model, node);
	}
	if (!node) {
		return;
	}
	int lod_pick = 0;
	int switch_pick = 0;
	walk_node_effect(node, mesh, &lod_pick, &switch_pick);
	if (node->child_count == 0) {
		return;
	}
	if (switch_pick != 0) {
		walk_node(model, node->p_children[switch_pick - 1], mesh);
	} else if (lod_pick > 0) {
		walk_node(model, node->p_children[lod_pick - 1], mesh);
	} else if (lod_pick == 0) {
		struct walk_mesh child = *mesh;
		for (int i = 0; i < node->child_count; ++i) {
			walk_node(model, node->p_children[i], &child);
		}
	}
}

static int compare_faces(const void *a, const void *b)
{
	const struct face_line *x = a;
	const struct face_line *y = b;
	if (x->component != y->component) {
		return x->component < y->component ? -1 : 1;
	}
	return strcmp(x->text, y->text);
}

static void max_children(const struct opt_node *node, int *lods, int *switches,
			 int depth)
{
	if (!node || depth > 256) {
		return;
	}
	if (node->node_type == OPT_FACEGROUP && node->child_count > *lods) {
		*lods = node->child_count;
	}
	if (node->node_type == OPT_NODESWITCH &&
	    node->child_count > *switches) {
		*switches = node->child_count;
	}
	for (int i = 0; i < node->child_count; ++i) {
		max_children(node->p_children[i], lods, switches, depth + 1);
	}
}

/* Runs one pass both ways and prints it; returns 0 when the walks differ. */
static int draw_pass(uint16_t handle, int components, int lod, int node_switch)
{
	struct optimized_poly_object *model = memory_get_handle_block(handle);
	struct mobile_object mobile;
	memset(&mobile, 0, sizeof(mobile));
	mobile.node_switch_index = (uint8_t)node_switch;
	struct object_record object;
	memset(&object, 0, sizeof(object));
	object.object_type = DRAW_SLOT;
	object.mobj = &mobile;
	g_loaded_models[DRAW_SLOT] = handle;
	g_forced_lod_level = lod;
	g_view_space_depth = 0;
	g_engine_mesh_count = 0;
	g_engine_mesh_overflow = 0;
	g_capturing = 1;
	render_scene_draw_object_model(&object);
	g_capturing = 0;

	g_face_count = 0;
	g_walk_mesh_count = 0;
	g_walk_overflow = 0;
	g_walk_texture = NULL;
	g_walk_lod = lod;
	g_walk_switch = node_switch;
	g_walk_component = -1;
	struct walk_mesh mesh;
	memset(&mesh, 0, sizeof(mesh));
	int component = 0;
	for (int i = 0; i < model->root_node_count; ++i) {
		struct opt_node *root = model->root_nodes[i];
		g_walk_component =
			root->node_type == OPT_TEXTURE ? -1 : component++;
		walk_node(model, root, &mesh);
	}
	int same = !g_engine_mesh_overflow && !g_walk_overflow &&
		   g_engine_mesh_count == g_walk_mesh_count &&
		   memcmp(g_engine_meshes, g_walk_meshes,
			  (size_t)g_walk_mesh_count *
				  sizeof(g_walk_meshes[0])) == 0;
	qsort(g_faces, (size_t)g_face_count, sizeof(g_faces[0]), compare_faces);
	printf("pass lod=%d switch=%d meshes=%d faces=%d walk=%s\n", lod,
	       node_switch, g_walk_mesh_count, g_face_count,
	       same ? "same" : "differs");
	int next = 0;
	for (int c = -1; c < components; ++c) {
		int first = next;
		while (next < g_face_count && g_faces[next].component == c) {
			++next;
		}
		if (c < 0 && next == first) {
			continue;
		}
		printf("component %d faces=%d\n", c, next - first);
		for (int f = first; f < next; ++f) {
			printf("%s\n", g_faces[f].text);
			free(g_faces[f].text);
		}
	}
	memory_handle_block_done_stub(handle);
	g_loaded_models[DRAW_SLOT] = 0;
	return same;
}

int opt_draw_dump(const char *name, const char *relative)
{
	int version = -1;
	unsigned int native_size = 0;
	/* opt_model_load_file_to_handle ends the program on a file the reader
	 * refuses; try the reader first. */
	uint16_t probe = xvt_opt_load(name, &version, &native_size);
	if (!probe) {
		printf("model ");
		asset_dump_quote(name, strlen(name));
		printf(" not_loaded version=%d\n", version);
		return 1;
	}
	memory_free_handle(probe);
	char file_name[NAME_CAPACITY];
	snprintf(file_name, sizeof(file_name), "%s", name);
	uint16_t file_handle = opt_model_load_file_to_handle(file_name);
	uint16_t handle = opt_model_create_runtime_handle(file_handle);
	if (!handle) {
		printf("model ");
		asset_dump_quote(name, strlen(name));
		printf(" not_loaded version=%d\n", version);
		return 1;
	}
	struct optimized_poly_object *model = memory_get_handle_block(handle);
	int lods = 1;
	int switches = 1;
	int components = 0;
	for (int i = 0; i < model->root_node_count; ++i) {
		max_children(model->root_nodes[i], &lods, &switches, 0);
		components += model->root_nodes[i]->node_type != OPT_TEXTURE;
	}
	int roots = model->root_node_count;
	memory_handle_block_done_stub(handle);
	printf("model ");
	asset_dump_quote(name, strlen(name));
	printf(" file=");
	asset_dump_quote(relative, strlen(relative));
	printf(" version=%d roots=%d components=%d lods=%d switches=%d\n",
	       version, roots, components, lods, switches);
	int same = 1;
	for (int lod = 1; lod <= lods; ++lod) {
		same &= draw_pass(handle, components, lod, 0);
	}
	for (int s = 1; s < switches; ++s) {
		same &= draw_pass(handle, components, 1, s);
	}
	memory_free_handle(handle);
	return same;
}
