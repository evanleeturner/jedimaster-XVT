#include "xvt/util/memory.h"

#ifdef XVT_MODERN
#include "xvt_runtime/snapshot/render_assets.h"
#endif

#include <stdlib.h>
#include <string.h>

#include "xvt_runtime/log/log_both_builds.h"

#ifndef XVT_MODERN
__declspec(dllimport) int __stdcall
VirtualProtect(void *address, size_t size, unsigned int new_protection,
	       unsigned int *old_protection);
#endif

/* 1 once the first memory_alloc_handle_internal call has cleared g_handle_tables;
 * only that function writes it, and nothing sets it back to 0. */
// GLOBAL: XVT 0x5280DC
uint8_t g_handle_allocator_initialized = 0;
/* Counts calls to memory_alloc_handle_internal, failed ones included. Only that
 * function writes it, and nothing reads it. */
// GLOBAL: XVT 0x5280E0
unsigned int g_handle_allocation_attempt_count = 0;
/* The handle allocator's tables, indexed by handle - 1: each block's pointer
 * and size, with a NULL pointer marking a free slot. Only
 * memory_alloc_handle_internal and memory_free_handle write it. The modern build
 * also reads it: xvt_frontend_task_shutdown frees every live handle, and
 * xvt_render_capture_capture_view reads the object table's size from it. */
// GLOBAL: XVT 0x622CE8
struct memory_handle_table_state g_handle_tables = {0};

/* In the original build, makes size bytes from address readable, writable and
 * executable (VirtualProtect with 0x40) and returns VirtualProtect's result:
 * nonzero on success, 0 on failure. The modern build changes nothing and
 * returns 1. Its one caller, render_scene_allocate_buffers, passes an address
 * 0x217 bytes into its own code and a size of 0x80000, and ignores the
 * result. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x4AC490
int memory_set_region_execute_read_write(void *address, size_t size)
{
#ifdef XVT_MODERN
	/* The source port does not patch its generated machine code. */
	(void)address;
	(void)size;
	return 1;
#else
	unsigned int old_protection;

	return VirtualProtect(address, size, 0x40u, &old_protection);
#endif
}

/* memory_alloc_handle_internal with the block filled with zeros. */
// FUNCTION: XVT 0x4AC5D0
uint16_t memory_alloc_handle_zeroed(size_t size, int legacy_tag)
{
	return memory_alloc_handle_internal(size, legacy_tag, 1);
}

/* memory_alloc_handle_internal without clearing: the block holds whatever malloc
 * left in it. */
// FUNCTION: XVT 0x4AC5F0
uint16_t memory_alloc_handle(size_t size, int legacy_tag)
{
	return memory_alloc_handle_internal(size, legacy_tag, 0);
}

/* Allocates size bytes with malloc and returns a handle to them, 1 to 32768:
 * one more than the first free slot of g_handle_tables. Returns 0 when all 32768
 * slots are in use or malloc fails, and the slot stays free. Fills the block
 * with zeros when clear_flag is nonzero. The first call clears both tables and
 * sets g_handle_allocator_initialized; every call adds 1 to
 * g_handle_allocation_attempt_count. Ignores legacy_tag. */
// FUNCTION: XVT 0x4AC610
uint16_t memory_alloc_handle_internal(size_t size, int legacy_tag,
				      int clear_flag)
{
	(void)legacy_tag;
	++g_handle_allocation_attempt_count;
	uint16_t table_index;
	if (g_handle_allocator_initialized == 0) {
		for (table_index = 0; table_index < 32768u; ++table_index) {
			g_handle_tables.ptr_table[table_index] = NULL;
			g_handle_tables.size_table[table_index] = 0;
		}
		g_handle_allocator_initialized = 1;
	}

	for (table_index = 0; table_index < 32768u; ++table_index) {
		if (g_handle_tables.ptr_table[table_index] == NULL) {
			break;
		}
	}
	if (table_index == 32768u) {
		XVT_LOG_ERROR("memory.handles_full bytes=%u", (unsigned)size);
		return 0;
	}

	unsigned int slot_index = table_index;
	void *block = malloc(size);
	g_handle_tables.ptr_table[slot_index] = block;
	if (block == NULL) {
		XVT_LOG_ERROR("memory.alloc_failed bytes=%u", (unsigned)size);
		return 0;
	}
	if (clear_flag != 0) {
		memset(block, 0, size);
	}
	g_handle_tables.size_table[slot_index] = size;
	XVT_LOG_DEBUG("memory.allocated handle=%u bytes=%u zeroed=%d",
		      table_index + 1u, (unsigned)size, clear_flag);
	return table_index + 1;
}

/* Frees a handle's block and marks its slot of g_handle_tables free (NULL
 * pointer, size 0); a slot already free is just cleared again. The modern build
 * first passes the handle to xvt_render_assets_retire_handle. Does not check the
 * handle: 0 or one over 32768 writes outside the tables. */
// FUNCTION: XVT 0x4AC6E0
void memory_free_handle(unsigned int handle)
{
#ifdef XVT_MODERN
	xvt_render_assets_retire_handle(handle);
#endif
	if (handle == 0 || handle > 32768u) {
		XVT_LOG_WARN("memory.free_invalid handle=%u", handle);
	}
	if (g_handle_tables.ptr_table[handle - 1] != NULL) {
		XVT_LOG_DEBUG("memory.freed handle=%u bytes=%u", handle,
			      (unsigned)g_handle_tables.size_table[handle - 1]);
		free(g_handle_tables.ptr_table[handle - 1]);
	}
	g_handle_tables.size_table[handle - 1] = 0;
	g_handle_tables.ptr_table[handle - 1] = NULL;
}

/* Returns a handle's block, NULL when its slot is free; the block keeps its
 * address until memory_free_handle. Does not check the handle: 0 or one over
 * 32768 reads outside the table. */
// FUNCTION: XVT 0x4AC720
void *memory_get_handle_block(uint16_t handle)
{
	return g_handle_tables.ptr_table[handle - 1];
}

/* Does nothing; no block is ever locked. Callers call it when they have
 * finished with a block from memory_get_handle_block. */
// FUNCTION: XVT 0x4AC740
void memory_handle_block_done_stub(uint16_t handle) { (void)handle; }
