#ifndef XVT_RUNTIME_OPT_NATIVE_H
#define XVT_RUNTIME_OPT_NATIVE_H
#include "xvt/assets/opt_model.h"

#ifdef __cplusplus
extern "C" {
#endif
OptNode* XvtOpt_ResolveCached(const OptimizedPolyObject* model, OptNode* node);
void XvtOpt_Relocate(OptimizedPolyObject* model);
void XvtOpt_RelocateNode(OptNode* node, intptr_t delta);
/* Read takes ownership of the open VFS stream. */
uint16_t XvtOpt_Read(AeronFile* file, const char* label, int* version, unsigned int* native_size);
uint16_t XvtOpt_Load(const char* path, int* version, unsigned int* native_size);
/* Native contiguous model blocks are aligned to the widest pointer field. */
size_t XvtOpt_AlignSize(size_t size);
uint8_t* XvtOpt_AlignPointer(uint8_t* pointer);
#ifdef __cplusplus
}
#endif

#endif
