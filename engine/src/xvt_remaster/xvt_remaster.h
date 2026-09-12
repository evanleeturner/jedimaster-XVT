#ifndef XVT_REMASTER_H
#define XVT_REMASTER_H

#include <stdint.h>

struct AeronInputSnapshot;
struct AeronTexture;

#ifdef __cplusplus
extern "C" {
#endif

/* Aeron and snapshot storage must outlive the initialized driver. */
int XvtRemaster_Init(void);
/* Called before the port tick; scene readiness controls classic suppression. */
void XvtRemaster_BeginFrame(const struct AeronInputSnapshot* input);
/* Consume the committed snapshot after the port tick and before Aeron_Present. */
void XvtRemaster_Frame(int32_t delta_us);
void XvtRemaster_Shutdown(void);
/* Borrowed retained output; frontend/movie artwork is linear SDR content. */
struct AeronTexture* XvtRemaster_Output(void);
struct AeronTexture* XvtRemaster_MovieOverlay(void);

#ifdef __cplusplus
}
#endif

#endif
