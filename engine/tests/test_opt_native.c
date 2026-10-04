#define _POSIX_C_SOURCE 200809L
/* Checks the OPT model loader (xvt_runtime/assets/opt_native.h: Read, Load and the two alignment helpers)
 * against the promises in its header, on model files this test writes itself into a fresh temporary
 * folder; no game model is read.
 *
 * A file is a version marker, then for versions 1 and 2 the body size, then the body. The body records the
 * base its 32-bit links count from, a reserved word, the number of roots and the address of the root
 * table; after those 14 bytes come 24-byte node records (name, type, child count, child table, a
 * parameter, payload), child tables, names and payloads. Read closes the file in every case: a handle it
 * left open would show as a leak when the sanitizers check the heap at exit.
 *
 * Not run here: the 128 MiB body limit (it takes a file that large), more than 65536 nodes (a graph that
 * large takes too long to load under the sanitizers), and a close that fails. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "test_assert.h"
#include "test_temp_folder.h"
#include "xvt/util/memory.h"
#include "xvt_runtime/assets/opt_native.h"
#include "xvt_runtime/storage/storage.h"

/* The address the test's bodies count their links from. */
#define BASE 0x00400000u

static char g_folder[XVT_TEST_PATH_CAPACITY];
static AeronVfs *g_vfs;

struct body {
	uint8_t *bytes;
	uint32_t size;
	uint32_t capacity;
};

/* Appends size bytes of data, or of zeros when data is NULL, and returns the address of the first. */
static uint32_t append(struct body *body, const void *data, uint32_t size)
{
	if (body->size + size > body->capacity) {
		body->capacity = (body->size + size) * 2;
		body->bytes = realloc(body->bytes, body->capacity);
		XVT_ASSERT_TRUE(body->bytes != NULL);
	}
	if (data) {
		memcpy(body->bytes + body->size, data, size);
	} else {
		memset(body->bytes + body->size, 0, size);
	}
	body->size += size;
	return BASE + body->size - size;
}

/* Stores value little-endian at address, which must already be inside the body. */
static void put(struct body *body, uint32_t address, uint32_t value)
{
	uint8_t *at = body->bytes + (address - BASE);
	for (int i = 0; i < 4; ++i) {
		at[i] = (uint8_t)(value >> (8 * i));
	}
}

/* The address one past the body's last byte. */
static uint32_t end(const struct body *body) { return BASE + body->size; }

/* Empties the body and writes its header with roots links, all 0, in a table right after it. Returns the
 * table's address. */
static uint32_t begin(struct body *body, uint32_t roots)
{
	body->size = 0;
	append(body, NULL, 14);
	put(body, BASE, BASE);
	put(body, BASE + 6, roots);
	uint32_t table = append(body, NULL, roots * 4);
	put(body, BASE + 10, roots ? table : 0);
	return table;
}

/* Appends a node record and returns its address. The fields are at offsets 0, 4, 8, 12, 16 and 20. */
static uint32_t opt_native_node(struct body *body, uint32_t name, int32_t type,
				int32_t count, uint32_t children, int32_t param,
				uint32_t payload)
{
	uint32_t node = append(body, NULL, 24);
	put(body, node, name);
	put(body, node + 4, (uint32_t)type);
	put(body, node + 8, (uint32_t)count);
	put(body, node + 12, children);
	put(body, node + 16, (uint32_t)param);
	put(body, node + 20, payload);
	return node;
}

/* Appends text with its terminator and returns its address. */
static uint32_t text(struct body *body, const char *text)
{
	return append(body, text, (uint32_t)strlen(text) + 1);
}

/* Writes a model file: marker, then size_field when has_size, then count bytes of the body. */
static void write_raw(const char *name, int32_t marker, int has_size,
		      uint32_t size_field, const struct body *body,
		      uint32_t count)
{
	uint8_t *file = malloc(8 + (size_t)count);
	XVT_ASSERT_TRUE(file != NULL);
	uint32_t used = 0;
	uint32_t words[2] = {(uint32_t)marker, size_field};
	for (int w = 0; w < (has_size ? 2 : 1); ++w) {
		for (int i = 0; i < 4; ++i) {
			file[used++] = (uint8_t)(words[w] >> (8 * i));
		}
	}
	memcpy(file + used, body->bytes, count);
	xvt_test_write_file(g_folder, name, file, used + count);
	free(file);
}

/* Writes the body as a well-formed file of the given version, 0, 1 or 2. */
static void write_model(const char *name, int version, const struct body *body)
{
	if (version == 0) {
		write_raw(name, (int32_t)body->size, 0, 0, body, body->size);
	} else {
		write_raw(name, -version, 1, body->size, body, body->size);
	}
}

/* Opens the file name for reading and passes it to Read. */
static uint16_t read_file(const char *name, int *version, unsigned *native_size)
{
	AeronFile *file = NULL;
	XVT_ASSERT_INT_EQ(AeronVfs_Open(g_vfs, AERON_VFS_ROOT_USER, name,
					AERON_VFS_READ, &file),
			  1);
	return xvt_opt_read(file, name, version, native_size);
}

/* Writes the body as a version 1 file and returns whether Read accepts it; frees what Read returned. */
static int accepts(const struct body *body)
{
	int version = -9;
	unsigned native_size = 0;
	write_model("case.opt", 1, body);
	uint16_t handle = read_file("case.opt", &version, &native_size);
	if (handle) {
		memory_free_handle(handle);
	}
	return handle != 0;
}

/* Returns 1 when pointer lies inside the size bytes that start at block. */
static int inside(const void *block, unsigned size, const void *pointer)
{
	uintptr_t start = (uintptr_t)block;
	uintptr_t at = (uintptr_t)pointer;
	return at >= start && at < start + size;
}

static void check_align(void)
{
	const size_t word = sizeof(void *);
	static void *storage[8];
	uint8_t *aligned = (uint8_t *)storage;
	for (size_t size = 0; size <= 4 * word; ++size) {
		size_t rounded = xvt_opt_align_size(size);
		XVT_ASSERT_INT_EQ(rounded % word, 0);
		XVT_ASSERT_TRUE(rounded >= size && rounded - size < word);

		uint8_t *pointer = xvt_opt_align_pointer(aligned + size);
		XVT_ASSERT_INT_EQ((uintptr_t)pointer % word, 0);
		XVT_ASSERT_TRUE(pointer >= aligned + size &&
				(size_t)(pointer - (aligned + size)) < word);
	}
}

static void check_null_file(void)
{
	int version = 77;
	unsigned native_size = 77;
	XVT_ASSERT_INT_EQ(xvt_opt_read(NULL, "none", &version, &native_size),
			  0);
	XVT_ASSERT_INT_EQ(version, 77);
	XVT_ASSERT_INT_EQ(native_size, 77);
}

static void check_versions(void)
{
	/* A 14-byte body, the smallest allowed, has room for no root table: a model with no roots. */
	struct body body = {0};
	begin(&body, 0);
	for (int expected = 0; expected <= 2; ++expected) {
		int version = -9;
		unsigned native_size = 0;
		write_model("case.opt", expected, &body);
		uint16_t handle = read_file("case.opt", &version, &native_size);
		XVT_ASSERT_TRUE(handle != 0);
		XVT_ASSERT_INT_EQ(version, expected);
		struct optimized_poly_object *model =
			memory_get_handle_block(handle);
		XVT_ASSERT_TRUE(model->self_marker == model);
		XVT_ASSERT_INT_EQ(model->root_node_count, 0);
		/* The model header comes first and a copy of the body last. */
		XVT_ASSERT_TRUE(native_size >=
				sizeof(struct optimized_poly_object) +
					body.size);
		memory_handle_block_done_stub(handle);
		memory_free_handle(handle);
	}

	/* Markers other than a positive size, -1 or -2 are refused, and the size is left alone. */
	static const int32_t bad_markers[] = {0, -3, INT32_MIN};
	for (size_t i = 0; i < sizeof bad_markers / sizeof bad_markers[0];
	     ++i) {
		int version = -9;
		unsigned native_size = 12345;
		write_raw("case.opt", bad_markers[i], 1, body.size, &body,
			  body.size);
		XVT_ASSERT_INT_EQ(read_file("case.opt", &version, &native_size),
				  0);
		XVT_ASSERT_INT_EQ(native_size, 12345);
	}
	free(body.bytes);
}

static void check_body_size(void)
{
	struct body body = {0};
	begin(&body, 0);
	int version = -9;
	unsigned native_size = 12345;

	/* 13 bytes is too short, though it fills the file. The version is set once the marker is read. */
	write_raw("case.opt", -2, 1, 13, &body, 13);
	XVT_ASSERT_INT_EQ(read_file("case.opt", &version, &native_size), 0);
	XVT_ASSERT_INT_EQ(version, 2);
	XVT_ASSERT_INT_EQ(native_size, 12345);

	/* The body must fill exactly the rest of the file. */
	append(&body, NULL, 1);
	write_raw("case.opt", -1, 1, 14, &body, 15);
	XVT_ASSERT_INT_EQ(read_file("case.opt", &version, &native_size), 0);
	write_raw("case.opt", -1, 1, 15, &body, 14);
	XVT_ASSERT_INT_EQ(read_file("case.opt", &version, &native_size), 0);
	write_raw("case.opt", 15, 0, 0, &body, 14);
	XVT_ASSERT_INT_EQ(read_file("case.opt", &version, &native_size), 0);
	XVT_ASSERT_INT_EQ(native_size, 12345);
	free(body.bytes);
}

static void check_rebuild(void)
{
	static const uint8_t vertices[24] = {1,	 2,  3,	 4,  5,	 6,  7,	 8,
					     9,	 10, 11, 12, 13, 14, 15, 16,
					     17, 18, 19, 20, 21, 22, 23, 24};
	struct body body = {0};
	uint32_t table = begin(&body, 1);
	uint32_t hull_name = text(&body, "Hull");
	uint32_t payload = append(&body, vertices, sizeof vertices);
	uint32_t verts =
		opt_native_node(&body, 0, OPT_MESHVERTS, 0, 0, 2, payload);
	/* A reference node with a nonzero parameter: Read clears the resolved-node cache it holds. */
	uint32_t ref = opt_native_node(&body, 0, OPT_NODEREF, 0, 0, 5,
				       text(&body, "Hull"));
	uint32_t children = append(&body, NULL, 8);
	put(&body, children, verts);
	put(&body, children + 4, ref);
	uint32_t hull =
		opt_native_node(&body, hull_name, OPT_GROUP, 2, children, 0, 0);
	put(&body, table, hull);

	int version = -9;
	unsigned native_size = 0;
	write_model("case.opt", 2, &body);
	uint16_t handle = read_file("case.opt", &version, &native_size);
	XVT_ASSERT_TRUE(handle != 0);
	XVT_ASSERT_INT_EQ(version, 2);
	struct optimized_poly_object *model = memory_get_handle_block(handle);
	XVT_ASSERT_TRUE(model->self_marker == model);
	XVT_ASSERT_INT_EQ(model->root_node_count, 1);

	/* Every pointer is native and inside the model's block; names point into the copy at its end. */
	struct opt_node *root = model->root_nodes[0];
	XVT_ASSERT_TRUE(inside(model, native_size, root));
	XVT_ASSERT_INT_EQ(root->node_type, OPT_GROUP);
	XVT_ASSERT_INT_EQ(root->child_count, 2);
	XVT_ASSERT_TRUE(inside((uint8_t *)model + native_size - body.size,
			       body.size, root->p_name));
	XVT_ASSERT_INT_EQ(strcmp(root->p_name, "Hull"), 0);
	XVT_ASSERT_TRUE(inside(model, native_size, root->p_children));

	struct opt_node *vertex_node = root->p_children[0];
	XVT_ASSERT_TRUE(inside(model, native_size, vertex_node));
	XVT_ASSERT_INT_EQ(vertex_node->node_type, OPT_MESHVERTS);
	XVT_ASSERT_TRUE(inside(model, native_size, vertex_node->payload));
	XVT_ASSERT_INT_EQ(
		memcmp(vertex_node->payload, vertices, sizeof vertices), 0);

	struct opt_node *ref_node = root->p_children[1];
	XVT_ASSERT_TRUE(inside(model, native_size, ref_node));
	XVT_ASSERT_INT_EQ(ref_node->node_type, OPT_NODEREF);
	XVT_ASSERT_INT_EQ(ref_node->payload_count, 0);
	memory_handle_block_done_stub(handle);
	memory_free_handle(handle);
	free(body.bytes);
}

static void check_payload_past_end(void)
{
	/* A payload of two vertices, 24 bytes, that starts 12 bytes before the end of the file. */
	static const uint8_t tail[12] = {0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6,
					 0xA7, 0xA8, 0xA9, 0xAA, 0xAB, 0xAC};
	struct body body = {0};
	uint32_t table = begin(&body, 1);
	uint32_t verts = opt_native_node(&body, 0, OPT_MESHVERTS, 0, 0, 2, 0);
	put(&body, table, verts);
	put(&body, verts + 20, append(&body, tail, sizeof tail));

	int version = -9;
	unsigned native_size = 0;
	write_model("case.opt", 1, &body);
	uint16_t handle = read_file("case.opt", &version, &native_size);
	XVT_ASSERT_TRUE(handle != 0);
	struct optimized_poly_object *model = memory_get_handle_block(handle);
	const uint8_t *copied = model->root_nodes[0]->payload;
	XVT_ASSERT_TRUE(inside(model, native_size, copied) &&
			inside(model, native_size, copied + 23));
	XVT_ASSERT_INT_EQ(memcmp(copied, tail, sizeof tail), 0);
	for (int i = 12; i < 24; ++i) {
		XVT_ASSERT_INT_EQ(copied[i], 0);
	}
	memory_handle_block_done_stub(handle);
	memory_free_handle(handle);
	free(body.bytes);
}

/* Appends size payload bytes, none of them 0, then 16 filler bytes the payload must not take in, and returns
 * the payload's address. */
static uint32_t opt_native_payload(struct body *body, uint32_t size)
{
	uint32_t payload = append(body, NULL, size + 16);
	for (uint32_t i = 0; i < size + 16; ++i) {
		body->bytes[payload - BASE + i] =
			i < size ? (uint8_t)(0x80 | (i * 7)) : 0xEE;
	}
	return payload;
}

/* Reads the body as a file of the given version and checks the payload of its last root, the last node
 * read: its size bytes are copied from payload in the file, and only zeroed padding follows them before the
 * copy of the body, which comes last in the block. */
static void check_last_payload(const struct body *body, int version,
			       uint32_t payload, uint32_t size)
{
	int read = -9;
	unsigned native_size = 0;
	write_model("case.opt", version, body);
	uint16_t handle = read_file("case.opt", &read, &native_size);
	XVT_ASSERT_TRUE(handle != 0);
	struct optimized_poly_object *model = memory_get_handle_block(handle);
	const uint8_t *copied =
		model->root_nodes[model->root_node_count - 1]->payload;
	const uint8_t *body_copy =
		(const uint8_t *)model + native_size - body->size;
	XVT_ASSERT_TRUE(inside(model, native_size, copied));
	XVT_ASSERT_TRUE(copied + size <= body_copy &&
			(size_t)(body_copy - (copied + size)) < sizeof(void *));
	XVT_ASSERT_INT_EQ(memcmp(copied, body->bytes + (payload - BASE), size),
			  0);
	for (const uint8_t *pad = copied + size; pad < body_copy; ++pad) {
		XVT_ASSERT_INT_EQ(*pad, 0);
	}
	memory_handle_block_done_stub(handle);
	memory_free_handle(handle);
}

static void check_payload_sizes(void)
{
	/* The payload size the reader gives each node type, here with a parameter of 3. */
	static const struct {
		int32_t type;
		uint32_t size;
	} payloads[] = {
		{OPT_TRANSFORM, 48},	 {OPT_ROTSCALE, 48},
		{OPT_MESHVERTS, 3 * 12}, {OPT_VERTNORMALS, 3 * 12},
		{OPT_TRANSLATION, 12},	 {OPT_SCALE, 12},
		{OPT_BASE_COLOR, 12},	 {OPT_ROTATION, 36},
		{OPT_MATERIAL, 3 * 56},	 {OPT_TEXCOORDS, 3 * 8},
		{OPT_FACEGROUP, 3 * 4},	 {OPT_HARDPOINT, 16},
		{OPT_MESHDESC, 72},
	};

	struct body body = {0};
	for (size_t i = 0; i < sizeof payloads / sizeof payloads[0]; ++i) {
		uint32_t table = begin(&body, 1);
		uint32_t payload = opt_native_payload(&body, payloads[i].size);
		put(&body, table,
		    opt_native_node(&body, 0, payloads[i].type, 0, 0, 3,
				    payload));
		check_last_payload(&body, 1, payload, payloads[i].size);
	}

	/* Face data of 2 faces after a list of 5 vertices: a 4-byte count, then 84 bytes a face in version 0
	 * files and 100 in later ones, then 12 bytes a vertex unless a list of normals was read before it. */
	static const int32_t face_types[] = {
		OPT_FACEDATA, OPT_FACEDATA_QUAD_MESH, OPT_FACEDATA_FACE_SET,
		OPT_FACEDATA_TRIANGLE_STRIP_SET};
	for (int version = 0; version <= 2; ++version) {
		for (size_t t = 0; t < sizeof face_types / sizeof face_types[0];
		     ++t) {
			for (uint32_t normals = 0; normals <= 1; ++normals) {
				uint32_t table = begin(&body, 2 + normals);
				put(&body, table,
				    opt_native_node(
					    &body, 0, OPT_MESHVERTS, 0, 0, 5,
					    opt_native_payload(&body, 5 * 12)));
				if (normals) {
					put(&body, table + 4,
					    opt_native_node(
						    &body, 0, OPT_VERTNORMALS,
						    0, 0, 5,
						    opt_native_payload(
							    &body, 5 * 12)));
				}
				uint32_t size = 4 +
						2 * (version == 0 ? 84 : 100) +
						(normals ? 0 : 5 * 12);
				uint32_t payload =
					opt_native_payload(&body, size);
				put(&body, table + 4 * (1 + normals),
				    opt_native_node(&body, 0, face_types[t], 0,
						    0, 2, payload));
				check_last_payload(&body, version, payload,
						   size);
			}
		}
	}
	free(body.bytes);
}

/* Builds a chain of length group nodes, each the only child of the one before, under one root. */
static void chain(struct body *body, int length)
{
	uint32_t table = begin(body, 1);
	uint32_t below = opt_native_node(body, 0, OPT_GROUP, 0, 0, 0, 0);
	for (int i = 1; i < length; ++i) {
		uint32_t link = append(body, NULL, 4);
		put(body, link, below);
		below = opt_native_node(body, 0, OPT_GROUP, 1, link, 0, 0);
	}
	put(body, table, below);
}

static void check_graph_limits(void)
{
	struct body body = {0};

	/* A cycle through two nodes, and a node that is its own child. */
	uint32_t table = begin(&body, 1);
	uint32_t link_a = append(&body, NULL, 4);
	uint32_t link_b = append(&body, NULL, 4);
	uint32_t a = opt_native_node(&body, 0, OPT_GROUP, 1, link_a, 0, 0);
	uint32_t b = opt_native_node(&body, 0, OPT_GROUP, 1, link_b, 0, 0);
	put(&body, link_a, b);
	put(&body, link_b, a);
	put(&body, table, a);
	XVT_ASSERT_INT_EQ(accepts(&body), 0);
	put(&body, link_a, a);
	XVT_ASSERT_INT_EQ(accepts(&body), 0);

	/* 256 levels are allowed; 257 are refused. */
	chain(&body, 256);
	XVT_ASSERT_INT_EQ(accepts(&body), 1);
	chain(&body, 257);
	XVT_ASSERT_INT_EQ(accepts(&body), 0);

	/* 65536 roots are allowed; 65537 are refused. Every root links to the same node. */
	for (uint32_t roots = 65536; roots <= 65537; ++roots) {
		table = begin(&body, roots);
		uint32_t node =
			opt_native_node(&body, 0, OPT_GROUP, 0, 0, 0, 0);
		for (uint32_t i = 0; i < roots; ++i) {
			put(&body, table + 4 * i, node);
		}
		XVT_ASSERT_INT_EQ(accepts(&body), roots == 65536);
	}
	free(body.bytes);
}

typedef enum defect {
	DEFECT_NONE,
	DEFECT_ROOT_PAST_END,
	DEFECT_ROOT_BELOW_BASE,
	DEFECT_CHILDREN_OUTSIDE,
	DEFECT_NAME_OUTSIDE,
	DEFECT_NAME_UNTERMINATED,
	DEFECT_PAYLOAD_OUTSIDE,
	DEFECT_TEXTURE_OUTSIDE,
	DEFECT_REFERENCE_UNTERMINATED
} defect;

/* A named group with one vertex child, well formed, then spoiled by one defect. */
static void simple(struct body *body, defect defect)
{
	static const uint8_t vertex[12] = {0};
	uint32_t table = begin(body, 1);
	uint32_t name = text(body, "Hull");
	uint32_t verts = opt_native_node(body, 0, OPT_MESHVERTS, 0, 0, 1,
					 append(body, vertex, sizeof vertex));
	uint32_t link = append(body, NULL, 4);
	put(body, link, verts);
	uint32_t hull = opt_native_node(body, name, OPT_GROUP, 1, link, 0, 0);
	put(body, table, hull);
	switch (defect) {
	case DEFECT_ROOT_PAST_END:
		put(body, table, end(body) + 100);
		break;
	case DEFECT_ROOT_BELOW_BASE:
		put(body, table, BASE - 24);
		break;
	case DEFECT_CHILDREN_OUTSIDE:
		put(body, hull + 12, end(body) + 4);
		break;
	case DEFECT_NAME_OUTSIDE:
		put(body, hull, end(body) + 8);
		break;
	case DEFECT_NAME_UNTERMINATED:
		put(body, hull, append(body, "abc", 3));
		break;
	case DEFECT_PAYLOAD_OUTSIDE:
		put(body, verts + 20, end(body) + 4);
		break;
	case DEFECT_TEXTURE_OUTSIDE:
		put(body, verts + 4, OPT_TEXTURE);
		put(body, verts + 20, end(body) + 64);
		break;
	case DEFECT_REFERENCE_UNTERMINATED:
		put(body, verts + 4, OPT_NODEREF);
		put(body, verts + 20, append(body, "abc", 3));
		break;
	default:
		break;
	}
}

/* Returns whether Read accepts the simple model spoiled by defect. */
static int accepts_simple(defect defect)
{
	struct body body = {0};
	simple(&body, defect);
	int accepted = accepts(&body);
	free(body.bytes);
	return accepted;
}

static void check_links_inside_file(void)
{
	XVT_ASSERT_INT_EQ(accepts_simple(DEFECT_NONE), 1);
	XVT_ASSERT_INT_EQ(accepts_simple(DEFECT_ROOT_PAST_END), 0);
	XVT_ASSERT_INT_EQ(accepts_simple(DEFECT_ROOT_BELOW_BASE), 0);
	XVT_ASSERT_INT_EQ(accepts_simple(DEFECT_CHILDREN_OUTSIDE), 0);
	XVT_ASSERT_INT_EQ(accepts_simple(DEFECT_NAME_OUTSIDE), 0);
	XVT_ASSERT_INT_EQ(accepts_simple(DEFECT_NAME_UNTERMINATED), 0);
	XVT_ASSERT_INT_EQ(accepts_simple(DEFECT_PAYLOAD_OUTSIDE), 0);
	XVT_ASSERT_INT_EQ(accepts_simple(DEFECT_TEXTURE_OUTSIDE), 0);
	/* A reference node's payload is the name it refers to. */
	XVT_ASSERT_INT_EQ(accepts_simple(DEFECT_REFERENCE_UNTERMINATED), 0);
}

static void check_load(void)
{
	xvt_storage_bind(g_vfs);
	struct body body = {0};
	simple(&body, DEFECT_NONE);
	write_model("model.opt", 0, &body);

	int version = -9;
	unsigned native_size = 0;
	uint16_t handle = xvt_opt_load("model.opt", &version, &native_size);
	XVT_ASSERT_TRUE(handle != 0);
	XVT_ASSERT_INT_EQ(version, 0);
	XVT_ASSERT_TRUE(native_size >=
			sizeof(struct optimized_poly_object) + body.size);
	memory_free_handle(handle);

	/* A file that does not open returns 0 before any marker is read. */
	version = -9;
	native_size = 12345;
	XVT_ASSERT_INT_EQ(xvt_opt_load("missing.opt", &version, &native_size),
			  0);
	XVT_ASSERT_INT_EQ(version, -9);
	XVT_ASSERT_INT_EQ(native_size, 12345);
	xvt_storage_bind(NULL);
	free(body.bytes);
}

int main(void)
{
	xvt_test_make_folder(g_folder);
	AeronVfsConfig config = {0};
	config.asset_root = g_folder;
	config.resource_root = g_folder;
	config.user_root = g_folder;
	config.temp_root = g_folder;
	g_vfs = AeronVfs_Create(&config);
	XVT_ASSERT_TRUE(g_vfs != NULL);

	check_align();
	check_null_file();
	check_versions();
	check_body_size();
	check_rebuild();
	check_payload_past_end();
	check_payload_sizes();
	check_graph_limits();
	check_links_inside_file();
	check_load();

	AeronVfs_Destroy(g_vfs);
	xvt_test_remove_tree(g_folder);
	return 0;
}
