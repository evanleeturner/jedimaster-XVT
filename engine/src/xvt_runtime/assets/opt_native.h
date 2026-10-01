#ifndef XVT_RUNTIME_OPT_NATIVE_H
#define XVT_RUNTIME_OPT_NATIVE_H
#include "xvt/assets/opt_model.h"

#ifdef __cplusplus
extern "C" {
#endif
/* Loads the game's OPT model files into one native block and keeps its internal pointers valid. Links
 * in the file are 32-bit addresses from a base the file records. Read checks them against the file and
 * rebuilds the model with native pointers in a zeroed Memory handle: the model header, the root table,
 * then nodes, child tables, payloads and textures, and last a copy of the file's body (everything after
 * its version marker and size), which node names point into. The model records its own address in
 * selfMarker, so Relocate can shift every internal pointer when the block moves. */

/* For an OPT_NODEREF node: the node its name refers to, looked up with OptModel_ResolveNodeRef on first
 * use and cached in node->param1. A failed lookup returns NULL and is tried again on the next call;
 * Read and relocation clear the cache. */
OptNode* XvtOpt_ResolveCached(const OptimizedPolyObject* model, OptNode* node);
/* Shifts every internal pointer by how far the model moved since selfMarker was recorded, then records
 * the new address; does nothing for NULL or a model that has not moved. A node reached from several
 * parents moves once. As in the original, a texture's palette pointer moves only for palette type 0;
 * for other types it goes stale, and readers find those palettes after the texels. Ends the program
 * through XvtStorage_Fatal when the graph is more than 256 levels deep or memory runs out. */
void XvtOpt_Relocate(OptimizedPolyObject* model);
/* Shifts the pointers inside node and everything below it by delta; node itself must already be at
 * its new address. Otherwise as Relocate. */
void XvtOpt_RelocateNode(OptNode* node, intptr_t delta);
/* Read takes ownership of the open VFS stream. */
/* Returns the model's Memory handle, or 0. The first 4 bytes give the version: a positive value is
 * version 0 and is itself the body size; -1 and -2 are versions 1 and 2, followed by the size. The body
 * must be 14 bytes to 128 MiB and fill exactly the rest of the file. Among its checks, it rejects a node
 * graph with a cycle, more than 65536 nodes or roots, or more than 256 levels, and any link, name, child
 * table or texture outside the file; a payload that starts inside the file but runs past its end is
 * copied as far as the file goes and zero after. *version is set once the marker is read; *native_size
 * only on success. The file is closed in every case, and a model whose close fails is freed and 0
 * returned. A failure logs an error naming label, except for a NULL file, which returns 0 silently. */
uint16_t XvtOpt_Read(AeronFile* file, const char* label, int* version, unsigned int* native_size);
/* Opens path for reading with XvtStorage_Open and passes it to Read, labelled with the last storage
 * path. A file that does not open returns 0 without a log. */
uint16_t XvtOpt_Load(const char* path, int* version, unsigned int* native_size);
/* Native contiguous model blocks are aligned to the widest pointer field. */
/* Rounds size up to a multiple of sizeof(void*). */
size_t XvtOpt_AlignSize(size_t size);
/* Rounds pointer up to a multiple of sizeof(void*). */
uint8_t* XvtOpt_AlignPointer(uint8_t* pointer);
#ifdef __cplusplus
}
#endif

#endif
