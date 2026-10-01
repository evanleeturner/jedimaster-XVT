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
#include "test_assert.h"
#include "test_temp_folder.h"
#include "xvt/util/memory.h"
#include "xvt_runtime/assets/opt_native.h"
#include "xvt_runtime/storage/storage.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* The address the test's bodies count their links from. */
#define BASE 0x00400000u

static char g_folder[XVT_TEST_PATH_CAPACITY];
static AeronVfs* g_vfs;

typedef struct Body {
	uint8_t* bytes;
	uint32_t size;
	uint32_t capacity;
} Body;

/* Appends size bytes of data, or of zeros when data is NULL, and returns the address of the first. */
static uint32_t Append(Body* body, const void* data, uint32_t size) {
	if (body->size + size > body->capacity) {
		body->capacity = (body->size + size) * 2;
		body->bytes = realloc(body->bytes, body->capacity);
		XVT_ASSERT_TRUE(body->bytes != NULL);
	}
	if (data)
		memcpy(body->bytes + body->size, data, size);
	else
		memset(body->bytes + body->size, 0, size);
	body->size += size;
	return BASE + body->size - size;
}

/* Stores value little-endian at address, which must already be inside the body. */
static void Put(Body* body, uint32_t address, uint32_t value) {
	uint8_t* at = body->bytes + (address - BASE);
	for (int i = 0; i < 4; ++i)
		at[i] = (uint8_t)(value >> (8 * i));
}

/* The address one past the body's last byte. */
static uint32_t End(const Body* body) { return BASE + body->size; }

/* Empties the body and writes its header with roots links, all 0, in a table right after it. Returns the
 * table's address. */
static uint32_t Begin(Body* body, uint32_t roots) {
	body->size = 0;
	Append(body, NULL, 14);
	Put(body, BASE, BASE);
	Put(body, BASE + 6, roots);
	uint32_t table = Append(body, NULL, roots * 4);
	Put(body, BASE + 10, roots ? table : 0);
	return table;
}

/* Appends a node record and returns its address. The fields are at offsets 0, 4, 8, 12, 16 and 20. */
static uint32_t Node(Body* body, uint32_t name, int32_t type, int32_t count, uint32_t children, int32_t param,
					 uint32_t payload) {
	uint32_t node = Append(body, NULL, 24);
	Put(body, node, name);
	Put(body, node + 4, (uint32_t)type);
	Put(body, node + 8, (uint32_t)count);
	Put(body, node + 12, children);
	Put(body, node + 16, (uint32_t)param);
	Put(body, node + 20, payload);
	return node;
}

/* Appends text with its terminator and returns its address. */
static uint32_t Text(Body* body, const char* text) { return Append(body, text, (uint32_t)strlen(text) + 1); }

/* Writes a model file: marker, then sizeField when hasSize, then count bytes of the body. */
static void WriteRaw(const char* name, int32_t marker, int hasSize, uint32_t sizeField, const Body* body,
					 uint32_t count) {
	uint8_t* file = malloc(8 + (size_t)count);
	XVT_ASSERT_TRUE(file != NULL);
	uint32_t used = 0;
	uint32_t words[2] = { (uint32_t)marker, sizeField };
	for (int w = 0; w < (hasSize ? 2 : 1); ++w)
		for (int i = 0; i < 4; ++i)
			file[used++] = (uint8_t)(words[w] >> (8 * i));
	memcpy(file + used, body->bytes, count);
	XvtTest_WriteFile(g_folder, name, file, used + count);
	free(file);
}

/* Writes the body as a well-formed file of the given version, 0, 1 or 2. */
static void WriteModel(const char* name, int version, const Body* body) {
	if (version == 0)
		WriteRaw(name, (int32_t)body->size, 0, 0, body, body->size);
	else
		WriteRaw(name, -version, 1, body->size, body, body->size);
}

/* Opens the file name for reading and passes it to Read. */
static uint16_t ReadFile(const char* name, int* version, unsigned* nativeSize) {
	AeronFile* file = NULL;
	XVT_ASSERT_INT_EQ(AeronVfs_Open(g_vfs, AERON_VFS_ROOT_USER, name, AERON_VFS_READ, &file), 1);
	return XvtOpt_Read(file, name, version, nativeSize);
}

/* Writes the body as a version 1 file and returns whether Read accepts it; frees what Read returned. */
static int Accepts(const Body* body) {
	int version = -9;
	unsigned nativeSize = 0;
	WriteModel("case.opt", 1, body);
	uint16_t handle = ReadFile("case.opt", &version, &nativeSize);
	if (handle)
		Memory_FreeHandle(handle);
	return handle != 0;
}

/* Returns 1 when pointer lies inside the size bytes that start at block. */
static int Inside(const void* block, unsigned size, const void* pointer) {
	uintptr_t start = (uintptr_t)block, at = (uintptr_t)pointer;
	return at >= start && at < start + size;
}

static void CheckAlign(void) {
	const size_t word = sizeof(void*);
	static void* storage[8];
	uint8_t* aligned = (uint8_t*)storage;
	for (size_t size = 0; size <= 4 * word; ++size) {
		size_t rounded = XvtOpt_AlignSize(size);
		XVT_ASSERT_INT_EQ(rounded % word, 0);
		XVT_ASSERT_TRUE(rounded >= size && rounded - size < word);

		uint8_t* pointer = XvtOpt_AlignPointer(aligned + size);
		XVT_ASSERT_INT_EQ((uintptr_t)pointer % word, 0);
		XVT_ASSERT_TRUE(pointer >= aligned + size && (size_t)(pointer - (aligned + size)) < word);
	}
}

static void CheckNullFile(void) {
	int version = 77;
	unsigned nativeSize = 77;
	XVT_ASSERT_INT_EQ(XvtOpt_Read(NULL, "none", &version, &nativeSize), 0);
	XVT_ASSERT_INT_EQ(version, 77);
	XVT_ASSERT_INT_EQ(nativeSize, 77);
}

static void CheckVersions(void) {
	/* A 14-byte body, the smallest allowed, has room for no root table: a model with no roots. */
	Body body = { 0 };
	Begin(&body, 0);
	for (int expected = 0; expected <= 2; ++expected) {
		int version = -9;
		unsigned nativeSize = 0;
		WriteModel("case.opt", expected, &body);
		uint16_t handle = ReadFile("case.opt", &version, &nativeSize);
		XVT_ASSERT_TRUE(handle != 0);
		XVT_ASSERT_INT_EQ(version, expected);
		OptimizedPolyObject* model = Memory_LockHandle(handle);
		XVT_ASSERT_TRUE(model->selfMarker == model);
		XVT_ASSERT_INT_EQ(model->rootNodeCount, 0);
		/* The model header comes first and a copy of the body last. */
		XVT_ASSERT_TRUE(nativeSize >= sizeof(OptimizedPolyObject) + body.size);
		Memory_UnlockHandle(handle);
		Memory_FreeHandle(handle);
	}

	/* Markers other than a positive size, -1 or -2 are refused, and the size is left alone. */
	static const int32_t badMarkers[] = { 0, -3, INT32_MIN };
	for (size_t i = 0; i < sizeof badMarkers / sizeof badMarkers[0]; ++i) {
		int version = -9;
		unsigned nativeSize = 12345;
		WriteRaw("case.opt", badMarkers[i], 1, body.size, &body, body.size);
		XVT_ASSERT_INT_EQ(ReadFile("case.opt", &version, &nativeSize), 0);
		XVT_ASSERT_INT_EQ(nativeSize, 12345);
	}
	free(body.bytes);
}

static void CheckBodySize(void) {
	Body body = { 0 };
	Begin(&body, 0);
	int version = -9;
	unsigned nativeSize = 12345;

	/* 13 bytes is too short, though it fills the file. The version is set once the marker is read. */
	WriteRaw("case.opt", -2, 1, 13, &body, 13);
	XVT_ASSERT_INT_EQ(ReadFile("case.opt", &version, &nativeSize), 0);
	XVT_ASSERT_INT_EQ(version, 2);
	XVT_ASSERT_INT_EQ(nativeSize, 12345);

	/* The body must fill exactly the rest of the file. */
	Append(&body, NULL, 1);
	WriteRaw("case.opt", -1, 1, 14, &body, 15);
	XVT_ASSERT_INT_EQ(ReadFile("case.opt", &version, &nativeSize), 0);
	WriteRaw("case.opt", -1, 1, 15, &body, 14);
	XVT_ASSERT_INT_EQ(ReadFile("case.opt", &version, &nativeSize), 0);
	WriteRaw("case.opt", 15, 0, 0, &body, 14);
	XVT_ASSERT_INT_EQ(ReadFile("case.opt", &version, &nativeSize), 0);
	XVT_ASSERT_INT_EQ(nativeSize, 12345);
	free(body.bytes);
}

static void CheckRebuild(void) {
	static const uint8_t vertices[24] = { 1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12,
										  13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24 };
	Body body = { 0 };
	uint32_t table = Begin(&body, 1);
	uint32_t hullName = Text(&body, "Hull");
	uint32_t payload = Append(&body, vertices, sizeof vertices);
	uint32_t verts = Node(&body, 0, OPT_MESHVERTS, 0, 0, 2, payload);
	/* A reference node with a nonzero parameter: Read clears the resolved-node cache it holds. */
	uint32_t ref = Node(&body, 0, OPT_NODEREF, 0, 0, 5, Text(&body, "Hull"));
	uint32_t children = Append(&body, NULL, 8);
	Put(&body, children, verts);
	Put(&body, children + 4, ref);
	uint32_t hull = Node(&body, hullName, OPT_GROUP, 2, children, 0, 0);
	Put(&body, table, hull);

	int version = -9;
	unsigned nativeSize = 0;
	WriteModel("case.opt", 2, &body);
	uint16_t handle = ReadFile("case.opt", &version, &nativeSize);
	XVT_ASSERT_TRUE(handle != 0);
	XVT_ASSERT_INT_EQ(version, 2);
	OptimizedPolyObject* model = Memory_LockHandle(handle);
	XVT_ASSERT_TRUE(model->selfMarker == model);
	XVT_ASSERT_INT_EQ(model->rootNodeCount, 1);

	/* Every pointer is native and inside the model's block; names point into the copy at its end. */
	OptNode* root = model->rootNodes[0];
	XVT_ASSERT_TRUE(Inside(model, nativeSize, root));
	XVT_ASSERT_INT_EQ(root->nodeType, OPT_GROUP);
	XVT_ASSERT_INT_EQ(root->childCount, 2);
	XVT_ASSERT_TRUE(Inside((uint8_t*)model + nativeSize - body.size, body.size, root->pName));
	XVT_ASSERT_INT_EQ(strcmp(root->pName, "Hull"), 0);
	XVT_ASSERT_TRUE(Inside(model, nativeSize, root->pChildren));

	OptNode* vertexNode = root->pChildren[0];
	XVT_ASSERT_TRUE(Inside(model, nativeSize, vertexNode));
	XVT_ASSERT_INT_EQ(vertexNode->nodeType, OPT_MESHVERTS);
	XVT_ASSERT_TRUE(Inside(model, nativeSize, vertexNode->param2));
	XVT_ASSERT_INT_EQ(memcmp(vertexNode->param2, vertices, sizeof vertices), 0);

	OptNode* refNode = root->pChildren[1];
	XVT_ASSERT_TRUE(Inside(model, nativeSize, refNode));
	XVT_ASSERT_INT_EQ(refNode->nodeType, OPT_NODEREF);
	XVT_ASSERT_INT_EQ(refNode->param1, 0);
	Memory_UnlockHandle(handle);
	Memory_FreeHandle(handle);
	free(body.bytes);
}

static void CheckPayloadPastEnd(void) {
	/* A payload of two vertices, 24 bytes, that starts 12 bytes before the end of the file. */
	static const uint8_t tail[12] = {
		0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xAB, 0xAC
	};
	Body body = { 0 };
	uint32_t table = Begin(&body, 1);
	uint32_t verts = Node(&body, 0, OPT_MESHVERTS, 0, 0, 2, 0);
	Put(&body, table, verts);
	Put(&body, verts + 20, Append(&body, tail, sizeof tail));

	int version = -9;
	unsigned nativeSize = 0;
	WriteModel("case.opt", 1, &body);
	uint16_t handle = ReadFile("case.opt", &version, &nativeSize);
	XVT_ASSERT_TRUE(handle != 0);
	OptimizedPolyObject* model = Memory_LockHandle(handle);
	const uint8_t* copied = model->rootNodes[0]->param2;
	XVT_ASSERT_TRUE(Inside(model, nativeSize, copied) && Inside(model, nativeSize, copied + 23));
	XVT_ASSERT_INT_EQ(memcmp(copied, tail, sizeof tail), 0);
	for (int i = 12; i < 24; ++i)
		XVT_ASSERT_INT_EQ(copied[i], 0);
	Memory_UnlockHandle(handle);
	Memory_FreeHandle(handle);
	free(body.bytes);
}

/* Builds a chain of length group nodes, each the only child of the one before, under one root. */
static void Chain(Body* body, int length) {
	uint32_t table = Begin(body, 1);
	uint32_t below = Node(body, 0, OPT_GROUP, 0, 0, 0, 0);
	for (int i = 1; i < length; ++i) {
		uint32_t link = Append(body, NULL, 4);
		Put(body, link, below);
		below = Node(body, 0, OPT_GROUP, 1, link, 0, 0);
	}
	Put(body, table, below);
}

static void CheckGraphLimits(void) {
	Body body = { 0 };

	/* A cycle through two nodes, and a node that is its own child. */
	uint32_t table = Begin(&body, 1);
	uint32_t linkA = Append(&body, NULL, 4);
	uint32_t linkB = Append(&body, NULL, 4);
	uint32_t a = Node(&body, 0, OPT_GROUP, 1, linkA, 0, 0);
	uint32_t b = Node(&body, 0, OPT_GROUP, 1, linkB, 0, 0);
	Put(&body, linkA, b);
	Put(&body, linkB, a);
	Put(&body, table, a);
	XVT_ASSERT_INT_EQ(Accepts(&body), 0);
	Put(&body, linkA, a);
	XVT_ASSERT_INT_EQ(Accepts(&body), 0);

	/* 256 levels are allowed; 257 are refused. */
	Chain(&body, 256);
	XVT_ASSERT_INT_EQ(Accepts(&body), 1);
	Chain(&body, 257);
	XVT_ASSERT_INT_EQ(Accepts(&body), 0);

	/* 65536 roots are allowed; 65537 are refused. Every root links to the same node. */
	for (uint32_t roots = 65536; roots <= 65537; ++roots) {
		table = Begin(&body, roots);
		uint32_t node = Node(&body, 0, OPT_GROUP, 0, 0, 0, 0);
		for (uint32_t i = 0; i < roots; ++i)
			Put(&body, table + 4 * i, node);
		XVT_ASSERT_INT_EQ(Accepts(&body), roots == 65536);
	}
	free(body.bytes);
}

typedef enum Defect {
	DEFECT_NONE,
	DEFECT_ROOT_PAST_END,
	DEFECT_ROOT_BELOW_BASE,
	DEFECT_CHILDREN_OUTSIDE,
	DEFECT_NAME_OUTSIDE,
	DEFECT_NAME_UNTERMINATED,
	DEFECT_PAYLOAD_OUTSIDE,
	DEFECT_TEXTURE_OUTSIDE
} Defect;

/* A named group with one vertex child, well formed, then spoiled by one defect. */
static void Simple(Body* body, Defect defect) {
	static const uint8_t vertex[12] = { 0 };
	uint32_t table = Begin(body, 1);
	uint32_t name = Text(body, "Hull");
	uint32_t verts = Node(body, 0, OPT_MESHVERTS, 0, 0, 1, Append(body, vertex, sizeof vertex));
	uint32_t link = Append(body, NULL, 4);
	Put(body, link, verts);
	uint32_t hull = Node(body, name, OPT_GROUP, 1, link, 0, 0);
	Put(body, table, hull);
	switch (defect) {
		case DEFECT_ROOT_PAST_END:
			Put(body, table, End(body) + 100);
			break;
		case DEFECT_ROOT_BELOW_BASE:
			Put(body, table, BASE - 24);
			break;
		case DEFECT_CHILDREN_OUTSIDE:
			Put(body, hull + 12, End(body) + 4);
			break;
		case DEFECT_NAME_OUTSIDE:
			Put(body, hull, End(body) + 8);
			break;
		case DEFECT_NAME_UNTERMINATED:
			Put(body, hull, Append(body, "abc", 3));
			break;
		case DEFECT_PAYLOAD_OUTSIDE:
			Put(body, verts + 20, End(body) + 4);
			break;
		case DEFECT_TEXTURE_OUTSIDE:
			Put(body, verts + 4, OPT_TEXTURE);
			Put(body, verts + 20, End(body) + 64);
			break;
		default:
			break;
	}
}

/* Returns whether Read accepts the simple model spoiled by defect. */
static int AcceptsSimple(Defect defect) {
	Body body = { 0 };
	Simple(&body, defect);
	int accepted = Accepts(&body);
	free(body.bytes);
	return accepted;
}

static void CheckLinksInsideFile(void) {
	XVT_ASSERT_INT_EQ(AcceptsSimple(DEFECT_NONE), 1);
	XVT_ASSERT_INT_EQ(AcceptsSimple(DEFECT_ROOT_PAST_END), 0);
	XVT_ASSERT_INT_EQ(AcceptsSimple(DEFECT_ROOT_BELOW_BASE), 0);
	XVT_ASSERT_INT_EQ(AcceptsSimple(DEFECT_CHILDREN_OUTSIDE), 0);
	XVT_ASSERT_INT_EQ(AcceptsSimple(DEFECT_NAME_OUTSIDE), 0);
	XVT_ASSERT_INT_EQ(AcceptsSimple(DEFECT_NAME_UNTERMINATED), 0);
	XVT_ASSERT_INT_EQ(AcceptsSimple(DEFECT_PAYLOAD_OUTSIDE), 0);
	XVT_ASSERT_INT_EQ(AcceptsSimple(DEFECT_TEXTURE_OUTSIDE), 0);
}

static void CheckLoad(void) {
	XvtStorage_Bind(g_vfs);
	Body body = { 0 };
	Simple(&body, DEFECT_NONE);
	WriteModel("model.opt", 0, &body);

	int version = -9;
	unsigned nativeSize = 0;
	uint16_t handle = XvtOpt_Load("model.opt", &version, &nativeSize);
	XVT_ASSERT_TRUE(handle != 0);
	XVT_ASSERT_INT_EQ(version, 0);
	XVT_ASSERT_TRUE(nativeSize >= sizeof(OptimizedPolyObject) + body.size);
	Memory_FreeHandle(handle);

	/* A file that does not open returns 0 before any marker is read. */
	version = -9;
	nativeSize = 12345;
	XVT_ASSERT_INT_EQ(XvtOpt_Load("missing.opt", &version, &nativeSize), 0);
	XVT_ASSERT_INT_EQ(version, -9);
	XVT_ASSERT_INT_EQ(nativeSize, 12345);
	XvtStorage_Bind(NULL);
	free(body.bytes);
}

int main(void) {
	XvtTest_MakeFolder(g_folder);
	AeronVfsConfig config = { 0 };
	config.asset_root = g_folder;
	config.resource_root = g_folder;
	config.user_root = g_folder;
	config.temp_root = g_folder;
	g_vfs = AeronVfs_Create(&config);
	XVT_ASSERT_TRUE(g_vfs != NULL);

	CheckAlign();
	CheckNullFile();
	CheckVersions();
	CheckBodySize();
	CheckRebuild();
	CheckPayloadPastEnd();
	CheckGraphLimits();
	CheckLinksInsideFile();
	CheckLoad();

	AeronVfs_Destroy(g_vfs);
	XvtTest_RemoveTree(g_folder);
	return 0;
}
