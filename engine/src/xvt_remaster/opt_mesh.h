#ifndef XVT_REMASTER_OPT_MESH_H
#define XVT_REMASTER_OPT_MESH_H
#include "aeron/asset/flight_model.h"
#include "aeron/vfs.h"
#include "xvt_runtime/config/settings.h"

#ifdef __cplusplus
extern "C" {
#endif
/* Builds a flight model from an original OPT file with the model settings,
 * applying the per-texture alpha-mode overrides of the resource file
 * opt_alpha_overrides.yaml, a database loaded once at Init. */

/* Loads and validates the override database: version 1 with a materials
 * sequence of up to 256 entries, each a model path, a texture name, a mode of
 * opaque, mask or blend, and an optional alpha_cutoff in 0 to 1 (0.5 when
 * absent); paths are uppercased with forward slashes, and a duplicate
 * model-texture pair is refused. Returns false with error written for a NULL
 * vfs or an unavailable or invalid database. The verdict is latched: a later
 * call repeats it without reloading. */
bool xvt_remaster_opt_mesh_init(AeronVfs *vfs, char *error, size_t capacity);
/* Forgets the database, so the next Init loads it again. */
void xvt_remaster_opt_mesh_shutdown(void);
/* Zeroes out, reads the OPT at resolved_path under the asset root (up to 64
 * MiB) and builds it with the settings' smoothing angle and emissive strength,
 * emissive on, and every override whose model matches the path (uppercased; a
 * BALANCEOFPOWER/ prefix is also tried stripped). Returns false with error
 * written for a NULL or empty argument, a database not loaded or invalid, an
 * unreadable file, or a failed conversion, with the converter's message. */
bool xvt_remaster_opt_mesh_build(AeronVfs *vfs, const char *resolved_path,
				 const struct xvt_model_settings *settings,
				 AeronFlightModel *out, char *error,
				 size_t capacity);
#ifdef __cplusplus
}
#endif
#endif
