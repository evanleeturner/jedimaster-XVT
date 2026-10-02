/* Checks OPT model relocation and the cached node-reference lookup (XvtOpt_Relocate, XvtOpt_RelocateNode
 * and XvtOpt_ResolveCached in xvt_runtime/assets/opt_native.h) against the promises in that header. The
 * model is one block this test builds in memory: no file and no game model is read. A copy of the block
 * at another address is the model moved; after relocation every internal pointer must sit at the same
 * offset into its block as in the original.
 *
 * Not run here: a graph more than 256 levels deep and running out of memory, which end the program
 * through XvtStorage_Fatal; that path shows a message box. */
#include "test_assert.h"
#include "xvt_runtime/assets/opt_native.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* The model block. Root 0 is the Hull group, with a texture whose palette type is 0, the Wing group and a
 * vertex node as children. Root 1 is the Wing group too, so it is reached from two parents. The Wing
 * group holds a texture whose palette type is 1 and a reference node naming "Hull". */
typedef struct Block {
	OptimizedPolyObject model;
	OptNode* roots[2];
	OptNode hull, texture0, wing, texture1, reference, vertices;
	OptNode* hullChildren[3];
	OptNode* wingChildren[2];
	OptTextureData textureData[2];
	uint16_t palettes[2][4];
	char names[3][8];
	uint8_t payload[24];
} Block;

/* Fills block as described above, with every pointer into block itself and the reference resolved. */
static void Build(Block* block) {
	memset(block, 0, sizeof *block);
	strcpy(block->names[0], "Hull");
	strcpy(block->names[1], "Wing");
	strcpy(block->names[2], "Hull");
	block->model.selfMarker = &block->model;
	block->model.rootNodeCount = 2;
	block->model.rootNodes = block->roots;
	block->roots[0] = &block->hull;
	block->roots[1] = &block->wing;

	block->hull.pName = block->names[0];
	block->hull.nodeType = OPT_GROUP;
	block->hull.childCount = 3;
	block->hull.pChildren = block->hullChildren;
	block->hullChildren[0] = &block->texture0;
	block->hullChildren[1] = &block->wing;
	block->hullChildren[2] = &block->vertices;

	block->texture0.nodeType = OPT_TEXTURE;
	block->texture0.payload = &block->textureData[0];
	block->textureData[0].inlinePaletteCount = 0;
	block->textureData[0].palette = block->palettes[0];

	block->wing.pName = block->names[1];
	block->wing.nodeType = OPT_GROUP;
	block->wing.childCount = 2;
	block->wing.pChildren = block->wingChildren;
	block->wingChildren[0] = &block->texture1;
	block->wingChildren[1] = &block->reference;

	block->texture1.nodeType = OPT_TEXTURE;
	block->texture1.payload = &block->textureData[1];
	block->textureData[1].inlinePaletteCount = 1;
	block->textureData[1].palette = block->palettes[1];

	block->reference.nodeType = OPT_NODEREF;
	block->reference.payload = block->names[2];
	block->reference.payloadCount = (XvtOptValue)&block->hull;

	block->vertices.nodeType = OPT_MESHVERTS;
	block->vertices.payloadCount = 2;
	block->vertices.payload = block->payload;
}

/* Returns a copy of block at a new address, its pointers still into block. */
static Block* Copy(const Block* block) {
	Block* copy = malloc(sizeof *copy);
	XVT_ASSERT_TRUE(copy != NULL);
	memcpy(copy, block, sizeof *copy);
	return copy;
}

/* How far pointer lies from the start of block, in bytes. */
static intptr_t Offset(const void* block, const void* pointer) {
	return (intptr_t)((uintptr_t)pointer - (uintptr_t)block);
}

#define XVT_ASSERT_SAME_OFFSET(original, moved, field)                                                       \
	XVT_ASSERT_INT_EQ(Offset((moved), (moved)->field), Offset((original), (original)->field))

static void CheckRelocateMovesEveryPointer(void) {
	Block* original = malloc(sizeof *original);
	XVT_ASSERT_TRUE(original != NULL);
	Build(original);
	Block* moved = Copy(original);
	XvtOpt_Relocate(&moved->model);

	XVT_ASSERT_TRUE(moved->model.selfMarker == &moved->model);
	XVT_ASSERT_SAME_OFFSET(original, moved, model.rootNodes);
	XVT_ASSERT_SAME_OFFSET(original, moved, roots[0]);
	XVT_ASSERT_SAME_OFFSET(original, moved, roots[1]);
	XVT_ASSERT_SAME_OFFSET(original, moved, hull.pName);
	XVT_ASSERT_SAME_OFFSET(original, moved, hull.pChildren);
	for (int i = 0; i < 3; ++i)
		XVT_ASSERT_SAME_OFFSET(original, moved, hullChildren[i]);
	XVT_ASSERT_SAME_OFFSET(original, moved, texture0.payload);
	XVT_ASSERT_SAME_OFFSET(original, moved, vertices.payload);
	XVT_ASSERT_SAME_OFFSET(original, moved, reference.payload);

	/* The Wing group is reached from two parents and moves once. */
	XVT_ASSERT_SAME_OFFSET(original, moved, wing.pName);
	XVT_ASSERT_SAME_OFFSET(original, moved, wing.pChildren);
	for (int i = 0; i < 2; ++i)
		XVT_ASSERT_SAME_OFFSET(original, moved, wingChildren[i]);
	XVT_ASSERT_SAME_OFFSET(original, moved, texture1.payload);

	/* A palette moves for palette type 0 only; for other types the old address stays. */
	XVT_ASSERT_SAME_OFFSET(original, moved, textureData[0].palette);
	XVT_ASSERT_TRUE(moved->textureData[1].palette == original->textureData[1].palette);

	/* Relocation clears the reference cache; a vertex count is not a pointer and stays. */
	XVT_ASSERT_INT_EQ(moved->reference.payloadCount, 0);
	XVT_ASSERT_INT_EQ(moved->vertices.payloadCount, 2);
	free(moved);
	free(original);
}

static void CheckRelocateLeavesUnmovedModel(void) {
	XvtOpt_Relocate(NULL);

	Block* block = malloc(sizeof *block);
	XVT_ASSERT_TRUE(block != NULL);
	Build(block);
	Block* before = Copy(block);
	XvtOpt_Relocate(&block->model);
	XVT_ASSERT_INT_EQ(memcmp(block, before, sizeof *block), 0);
	free(before);
	free(block);
}

static void CheckRelocateNode(void) {
	Block* original = malloc(sizeof *original);
	XVT_ASSERT_TRUE(original != NULL);
	Build(original);
	Block* moved = Copy(original);
	XvtOpt_RelocateNode(&moved->wing, (intptr_t)((uintptr_t)moved - (uintptr_t)original));

	/* The node and everything below it move. */
	XVT_ASSERT_SAME_OFFSET(original, moved, wing.pName);
	XVT_ASSERT_SAME_OFFSET(original, moved, wing.pChildren);
	for (int i = 0; i < 2; ++i)
		XVT_ASSERT_SAME_OFFSET(original, moved, wingChildren[i]);
	XVT_ASSERT_SAME_OFFSET(original, moved, texture1.payload);
	XVT_ASSERT_SAME_OFFSET(original, moved, reference.payload);
	XVT_ASSERT_TRUE(moved->textureData[1].palette == original->textureData[1].palette);
	XVT_ASSERT_INT_EQ(moved->reference.payloadCount, 0);

	/* Nothing above it or beside it moves, the model header included. */
	XVT_ASSERT_TRUE(moved->model.selfMarker == &original->model);
	XVT_ASSERT_TRUE(moved->model.rootNodes == original->model.rootNodes);
	XVT_ASSERT_TRUE(moved->hull.pChildren == original->hull.pChildren);
	XVT_ASSERT_TRUE(moved->hullChildren[0] == original->hullChildren[0]);
	XVT_ASSERT_TRUE(moved->texture0.payload == original->texture0.payload);
	XVT_ASSERT_TRUE(moved->vertices.payload == original->vertices.payload);
	free(moved);
	free(original);
}

static void CheckResolveCached(void) {
	Block* block = malloc(sizeof *block);
	XVT_ASSERT_TRUE(block != NULL);
	Build(block);
	block->reference.payloadCount = 0;

	/* The first use looks the name up and caches the node in payloadCount. */
	XVT_ASSERT_TRUE(XvtOpt_ResolveCached(&block->model, &block->reference) == &block->hull);
	XVT_ASSERT_TRUE(block->reference.payloadCount == (XvtOptValue)&block->hull);

	/* Later uses return the cached node, even once the name no longer leads there. */
	block->hull.pName = NULL;
	XVT_ASSERT_TRUE(XvtOpt_ResolveCached(&block->model, &block->reference) == &block->hull);

	/* A failed lookup returns NULL, caches nothing, and is tried again on the next call. */
	block->reference.payloadCount = 0;
	XVT_ASSERT_TRUE(XvtOpt_ResolveCached(&block->model, &block->reference) == NULL);
	XVT_ASSERT_INT_EQ(block->reference.payloadCount, 0);
	block->vertices.pName = block->names[0];
	XVT_ASSERT_TRUE(XvtOpt_ResolveCached(&block->model, &block->reference) == &block->vertices);
	free(block);
}

int main(void) {
	CheckRelocateMovesEveryPointer();
	CheckRelocateLeavesUnmovedModel();
	CheckRelocateNode();
	CheckResolveCached();
	return 0;
}
