#include "xvt_runtime/assets/opt_native.h"
#include "xvt/util/memory.h"
#include "xvt_runtime/log/log.h"
#include "xvt_runtime/storage/storage.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define XVT_OPT_MAX_BYTES (128u * 1024u * 1024u)
#define XVT_OPT_MAX_DEPTH 256

typedef struct XvtOptEntry {
	uint32_t address;
	uint32_t name_address;
	uint32_t children_address;
	uint32_t payload_address;
	uint32_t palette_address;
	uint32_t embedded_palette_address;
	int32_t type, child_count, payload_count;
	size_t node_offset, children_offset, texture_offset, payload_offset, payload_size, payload_copy_size,
		palette_offset;
	int visiting;
} XvtOptEntry;

typedef struct XvtOptPalette {
	uint32_t address;
	size_t offset;
} XvtOptPalette;

typedef struct XvtOptDecode {
	uint8_t* bytes;
	uint32_t size, base;
	XvtOptEntry* nodes;
	size_t count, capacity, native_size;
	int failed;
	int version;
} XvtOptDecode;

size_t XvtOpt_AlignSize(size_t size) { return (size + sizeof(void*) - 1) & ~(sizeof(void*) - 1); }

uint8_t* XvtOpt_AlignPointer(uint8_t* pointer) {
	return (uint8_t*)((uintptr_t)(pointer + sizeof(void*) - 1) & ~(uintptr_t)(sizeof(void*) - 1));
}

static uint32_t XvtOpt_ReadU32(const void* memory) {
	const uint8_t* p = memory;
	return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static const uint8_t* XvtOpt_BytesAt(XvtOptDecode* decode, uint32_t address, size_t size) {
	if (!address || address < decode->base || address - decode->base > decode->size ||
		size > decode->size - (address - decode->base)) {
		decode->failed = 1;
		return NULL;
	}
	return decode->bytes + (address - decode->base);
}

static int XvtOpt_ValidateString(XvtOptDecode* decode, uint32_t address) {
	const uint8_t* value = XvtOpt_BytesAt(decode, address, 1);
	if (!value || !memchr(value, 0, decode->size - (address - decode->base))) {
		decode->failed = 1;
		return 0;
	}
	return 1;
}

static size_t XvtOpt_Reserve(XvtOptDecode* decode, size_t size) {
	size_t offset = XvtOpt_AlignSize(decode->native_size);
	if (size > XVT_OPT_MAX_BYTES || offset > XVT_OPT_MAX_BYTES - size) {
		decode->failed = 1;
		return 0;
	}
	decode->native_size = offset + size;
	return offset;
}

static int XvtOpt_Index(const XvtOptDecode* decode, uint32_t address) {
	for (size_t i = 0; i < decode->count; ++i)
		if (decode->nodes[i].address == address)
			return (int)i;
	return -1;
}

static int XvtOpt_ReserveTexture(XvtOptDecode* decode, XvtOptEntry* entry) {
	const uint8_t* raw = XvtOpt_BytesAt(decode, entry->payload_address, 24);
	int64_t pixels, bytes, palette_bytes;
	if (!raw)
		return 0;
	int32_t palette_type = (int32_t)XvtOpt_ReadU32(raw + 4);
	int32_t texture_size = (int32_t)XvtOpt_ReadU32(raw + 8);
	int32_t data_size = (int32_t)XvtOpt_ReadU32(raw + 12);
	int32_t width = (int32_t)XvtOpt_ReadU32(raw + 16);
	int32_t height = (int32_t)XvtOpt_ReadU32(raw + 20);
	entry->palette_address = XvtOpt_ReadU32(raw);
	pixels = (int64_t)width * height;
	bytes = texture_size == pixels ? data_size : pixels;
	palette_bytes = palette_type ? (int64_t)palette_type * 768
					: entry->palette_address == entry->payload_address + 24 + bytes ? 12288
																					: 0;
	if (width <= 0 || height <= 0 || width > 16384 || height > 16384 || palette_type < 0 ||
		palette_type > 16 || bytes < pixels || bytes > XVT_OPT_MAX_BYTES ||
		palette_bytes > XVT_OPT_MAX_BYTES ||
		!XvtOpt_BytesAt(decode, entry->payload_address, (size_t)(24 + bytes + palette_bytes)))
		return 0;
	if (!palette_type && !XvtOpt_BytesAt(decode, entry->palette_address, 12288))
		return 0;
	entry->texture_offset = XvtOpt_Reserve(decode, sizeof(OptTextureData) + (size_t)(bytes + palette_bytes));
	if (palette_bytes) {
		entry->embedded_palette_address = entry->payload_address + 24 + (uint32_t)bytes;
		entry->palette_offset = entry->texture_offset + sizeof(OptTextureData) + (size_t)bytes;
	}
	return !decode->failed;
}

static int XvtOpt_ComparePalette(const void* lhs, const void* rhs) {
	const XvtOptPalette* a = lhs;
	const XvtOptPalette* b = rhs;
	return (a->address > b->address) - (a->address < b->address);
}

static int XvtOpt_ResolvePalettes(XvtOptDecode* decode) {
	size_t count = 0;
	for (size_t i = 0; i < decode->count; ++i)
		if (decode->nodes[i].embedded_palette_address)
			++count;
	XvtOptPalette* palettes = count ? malloc(count * sizeof(*palettes)) : NULL;
	if (count && !palettes)
		return 0;
	size_t cursor = 0;
	for (size_t i = 0; i < decode->count; ++i) {
		const XvtOptEntry* entry = &decode->nodes[i];
		if (entry->embedded_palette_address)
			palettes[cursor++] = (XvtOptPalette) { entry->embedded_palette_address, entry->palette_offset };
	}
	if (count)
		qsort(palettes, count, sizeof(*palettes), XvtOpt_ComparePalette);
	/* Resolve after visiting every node: shared palettes may precede their owner.
	 * Runtime fixup identifies that owner by its embedded palette's exact address. */
	int valid = 1;
	for (size_t i = 0; i < decode->count; ++i) {
		XvtOptEntry* entry = &decode->nodes[i];
		if (entry->type != OPT_TEXTURE)
			continue;
		const uint8_t* raw = XvtOpt_BytesAt(decode, entry->payload_address, 24);
		if (XvtOpt_ReadU32(raw + 4) != 0) {
			if (entry->palette_address != entry->embedded_palette_address)
				entry->palette_offset = 0;
			continue;
		}
		XvtOptPalette key = { entry->palette_address, 0 };
		const XvtOptPalette* owner =
			count ? bsearch(&key, palettes, count, sizeof(*palettes), XvtOpt_ComparePalette) : NULL;
		if (!owner) {
			valid = 0;
			break;
		}
		entry->palette_offset = owner->offset;
	}
	free(palettes);
	return valid;
}

/* Writes to *size the size of the payload a node of entry's type carries, which can be 0, and returns 1.
 * Returns 0 for a type with no payload layout here, or for a reference whose name is not a string inside
 * the file. */
static int XvtOpt_PayloadSize(XvtOptDecode* decode, const XvtOptEntry* entry, int vertices, int has_normals,
							  size_t* size) {
	switch (entry->type) {
		case OPT_FACEDATA:
		case OPT_FACEDATA_QUAD_MESH:
		case OPT_FACEDATA_FACE_SET:
		case OPT_FACEDATA_TRIANGLE_STRIP_SET:
			*size = 4 + (size_t)entry->payload_count * (decode->version == 0 ? 84 : 100);
			if (!has_normals)
				*size += (size_t)vertices * 12;
			break;
		case OPT_TRANSFORM:
		case OPT_ROTSCALE:
			*size = 48;
			break;
		case OPT_MESHVERTS:
		case OPT_VERTNORMALS:
			*size = (size_t)entry->payload_count * 12;
			break;
		case OPT_TRANSLATION:
		case OPT_SCALE:
		case OPT_BASE_COLOR:
			*size = 12;
			break;
		case OPT_ROTATION:
			*size = 36;
			break;
		case OPT_NODEREF:
			if (!XvtOpt_ValidateString(decode, entry->payload_address))
				return 0;
			*size = strlen((const char*)XvtOpt_BytesAt(decode, entry->payload_address, 1)) + 1;
			break;
		case OPT_MATERIAL:
			*size = (size_t)entry->payload_count * 56;
			break;
		case OPT_TEXCOORDS:
			*size = (size_t)entry->payload_count * 8;
			break;
		case OPT_FACEGROUP:
			*size = (size_t)entry->payload_count * 4;
			break;
		case OPT_HARDPOINT:
			*size = 16;
			break;
		case OPT_MESHDESC:
			*size = 72;
			break;
		default:
			return 0;
	}
	return 1;
}

static int XvtOpt_Visit(XvtOptDecode* decode, uint32_t address, unsigned depth, int* vertices,
						int* has_normals) {
	if (!address)
		return 1;
	int index = XvtOpt_Index(decode, address);
	if (index >= 0)
		return !decode->nodes[index].visiting;
	const uint8_t* raw = XvtOpt_BytesAt(decode, address, 24);
	if (!raw || depth >= XVT_OPT_MAX_DEPTH || decode->count >= 65536)
		return 0;
	if (decode->count == decode->capacity) {
		size_t capacity = decode->capacity ? decode->capacity * 2 : 64;
		XvtOptEntry* grown = realloc(decode->nodes, capacity * sizeof(*grown));
		if (!grown)
			return 0;
		decode->nodes = grown;
		decode->capacity = capacity;
	}
	index = (int)decode->count++;
	XvtOptEntry* entry = &decode->nodes[index];
	memset(entry, 0, sizeof(*entry));
	entry->address = address;
	entry->name_address = XvtOpt_ReadU32(raw);
	entry->type = (int32_t)XvtOpt_ReadU32(raw + 4);
	entry->child_count = (int32_t)XvtOpt_ReadU32(raw + 8);
	entry->children_address = XvtOpt_ReadU32(raw + 12);
	entry->payload_count = (int32_t)XvtOpt_ReadU32(raw + 16);
	entry->payload_address = XvtOpt_ReadU32(raw + 20);
	entry->visiting = 1;
	/* The original default case preserves unrecognized nodes without a payload.
	 * State-only and unused (-1) nodes can retain stale allocation addresses. */
	int has_payload = entry->type >= OPT_GROUP && entry->type <= OPT_MESHDESC && entry->type != OPT_GROUP &&
					  entry->type != OPT_DEF && entry->type != OPT_MATERIAL_BINDING &&
					  entry->type != OPT_NORMAL_BINDING && entry->type != OPT_TEXCOORD_BINDING &&
					  entry->type != OPT_INVENTOR_GROUP && entry->type != OPT_NODESWITCH;
	if (!has_payload)
		entry->payload_address = 0;
	if (entry->child_count < 0 || entry->child_count > 65536 ||
		(has_payload && (entry->payload_count < 0 || entry->payload_count > 1000000)) ||
		(entry->name_address && !XvtOpt_ValidateString(decode, entry->name_address)))
		return 0;
	entry->node_offset = XvtOpt_Reserve(decode, sizeof(OptNode));
	entry->children_offset = XvtOpt_Reserve(decode, (size_t)entry->child_count * sizeof(OptNode*));
	if (entry->type == OPT_MESHVERTS)
		*vertices = entry->payload_count;
	if (entry->type == OPT_VERTNORMALS)
		*has_normals = 1;
	if (entry->type == OPT_TEXTURE) {
		if (!XvtOpt_ReserveTexture(decode, entry))
			return 0;
	} else if (entry->payload_address) {
		size_t size = 0;
		if (!XvtOpt_PayloadSize(decode, entry, *vertices, *has_normals, &size))
			return 0;
		if (!XvtOpt_BytesAt(decode, entry->payload_address, 0))
			return 0;
		size_t available = decode->size - (entry->payload_address - decode->base);
		/* The original can read beyond the serialized payload into its allocation.
		 * Copy available bytes and keep the native allocation's remainder zeroed. */
		entry->payload_copy_size = size < available ? size : available;
		entry->payload_size = size;
		entry->payload_offset = XvtOpt_Reserve(decode, size);
	} else if (has_payload) {
		return 0;
	}
	int count = entry->child_count;
	const uint8_t* children =
		count ? XvtOpt_BytesAt(decode, entry->children_address, (size_t)count * 4) : NULL;
	if (count && !children)
		return 0;
	int child_vertices = *vertices, child_has_normals = *has_normals;
	for (int i = 0; i < count; ++i)
		if (!XvtOpt_Visit(decode, XvtOpt_ReadU32(children + i * 4), depth + 1, &child_vertices,
						  &child_has_normals))
			return 0;
	decode->nodes[index].visiting = 0;
	return !decode->failed;
}

static void* XvtOpt_RawPointer(XvtOptDecode* decode, uint8_t* raw_copy, uint32_t address) {
	return address ? raw_copy + (address - decode->base) : NULL;
}

static OptNode* XvtOpt_NodePointer(XvtOptDecode* decode, uint8_t* native, uint32_t address) {
	int index = XvtOpt_Index(decode, address);
	return index < 0 ? NULL : (OptNode*)(native + decode->nodes[index].node_offset);
}

static void XvtOpt_Expand(XvtOptDecode* decode, uint8_t* native, uint8_t* raw_copy) {
	for (size_t i = 0; i < decode->count; ++i) {
		XvtOptEntry* entry = &decode->nodes[i];
		OptNode* node = (OptNode*)(native + entry->node_offset);
		node->pName = XvtOpt_RawPointer(decode, raw_copy, entry->name_address);
		node->nodeType = (OptNodeType)entry->type;
		node->childCount = entry->child_count;
		node->payloadCount = entry->type == OPT_NODEREF ? 0 : entry->payload_count;
		node->payload = entry->payload_size ? native + entry->payload_offset : NULL;
		if (entry->payload_copy_size)
			memcpy(node->payload, XvtOpt_BytesAt(decode, entry->payload_address, entry->payload_copy_size),
				   entry->payload_copy_size);
		node->pChildren = entry->child_count ? (OptNode**)(native + entry->children_offset) : NULL;
		const uint8_t* children = entry->child_count ? XvtOpt_BytesAt(decode, entry->children_address,
																	  (size_t)entry->child_count * 4)
													 : NULL;
		for (int j = 0; j < entry->child_count; ++j)
			node->pChildren[j] = XvtOpt_NodePointer(decode, native, XvtOpt_ReadU32(children + j * 4));
		if (entry->type == OPT_TEXTURE) {
			const uint8_t* raw = XvtOpt_BytesAt(decode, entry->payload_address, 24);
			OptTextureData* texture = (OptTextureData*)(native + entry->texture_offset);
			node->payload = texture;
			texture->inlinePaletteCount = (int32_t)XvtOpt_ReadU32(raw + 4);
			texture->textureSize = (int32_t)XvtOpt_ReadU32(raw + 8);
			texture->dataSize = (int32_t)XvtOpt_ReadU32(raw + 12);
			texture->width = (int32_t)XvtOpt_ReadU32(raw + 16);
			texture->height = (int32_t)XvtOpt_ReadU32(raw + 20);
			size_t bytes = (size_t)texture->width * texture->height;
			if (bytes == (size_t)texture->textureSize)
				bytes = (size_t)texture->dataSize;
			size_t palette_bytes = texture->inlinePaletteCount ? (size_t)texture->inlinePaletteCount * 768
								   : entry->palette_address == entry->payload_address + 24 + bytes ? 12288
																								   : 0;
			memcpy(texture + 1, raw + 24, bytes + palette_bytes);
			texture->palette = entry->palette_offset ? (uint16_t*)(native + entry->palette_offset) : NULL;
		}
	}
}

uint16_t XvtOpt_Read(AeronFile* file, const char* label, int* version, unsigned int* native_size) {
	XvtOptDecode decode = { 0 };
	uint8_t header[8];
	uint16_t handle = 0;
	if (!file)
		return 0;
	if (!AeronVfs_Read(file, header, 4, NULL))
		goto done;
	int32_t marker = (int32_t)XvtOpt_ReadU32(header);
	*version = marker > 0 ? 0 : marker == -1 ? 1 : marker == -2 ? 2 : -1;
	if (*version < 0)
		goto done;
	if (marker <= 0 && !AeronVfs_Read(file, header, 4, NULL))
		goto done;
	decode.size = XvtOpt_ReadU32(header);
	if (decode.size < 14 || decode.size > XVT_OPT_MAX_BYTES ||
		AeronVfs_GetSize(file) - AeronVfs_Tell(file) != decode.size)
		goto done;
	decode.bytes = malloc(decode.size);
	if (!decode.bytes || !AeronVfs_Read(file, decode.bytes, decode.size, NULL))
		goto done;
	decode.base = XvtOpt_ReadU32(decode.bytes);
	decode.version = *version;
	uint32_t roots = XvtOpt_ReadU32(decode.bytes + 6), root_table_address = XvtOpt_ReadU32(decode.bytes + 10);
	if (roots > 65536 || decode.base > UINT32_MAX - decode.size)
		goto done;
	const uint8_t* table = roots ? XvtOpt_BytesAt(&decode, root_table_address, (size_t)roots * 4) : NULL;
	if (roots && !table)
		goto done;
	decode.native_size = sizeof(OptimizedPolyObject) + (size_t)roots * sizeof(OptNode*);
	int vertices = 0, has_normals = 0;
	for (uint32_t i = 0; i < roots; ++i)
		if (!XvtOpt_Visit(&decode, XvtOpt_ReadU32(table + i * 4), 0, &vertices, &has_normals))
			goto done;
	if (!XvtOpt_ResolvePalettes(&decode))
		goto done;
	size_t raw_offset = XvtOpt_Reserve(&decode, decode.size);
	if (decode.failed)
		goto done;
	handle = Memory_AllocHandleZeroed(decode.native_size, 0);
	if (!handle)
		goto done;
	OptimizedPolyObject* model = Memory_LockHandle(handle);
	uint8_t* raw_copy = (uint8_t*)model + raw_offset;
	memcpy(raw_copy, decode.bytes, decode.size);
	model->selfMarker = model;
	model->reserved = (uint16_t)(decode.bytes[4] | decode.bytes[5] << 8);
	model->rootNodeCount = (int)roots;
	model->rootNodes = (OptNode**)(model + 1);
	XvtOpt_Expand(&decode, (uint8_t*)model, raw_copy);
	for (uint32_t i = 0; i < roots; ++i)
		model->rootNodes[i] = XvtOpt_NodePointer(&decode, (uint8_t*)model, XvtOpt_ReadU32(table + i * 4));
	*native_size = (unsigned int)decode.native_size;
	Memory_UnlockHandle(handle);
done:
	if (!AeronVfs_Close(file) && handle) {
		Memory_FreeHandle(handle);
		handle = 0;
	}
	free(decode.nodes);
	free(decode.bytes);
	if (!handle)
		XVT_LOG_ERROR("models.opt_invalid path=\"%s\"", label);
	return handle;
}

uint16_t XvtOpt_Load(const char* path, int* version, unsigned int* native_size) {
	AeronFile* file = XvtStorage_Open(path, "rb");
	return XvtOpt_Read(file, XvtStorage_LastPath(), version, native_size);
}
