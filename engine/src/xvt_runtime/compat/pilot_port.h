#ifndef XVT_RUNTIME_COMPAT_PILOT_PORT_H
#define XVT_RUNTIME_COMPAT_PILOT_PORT_H

/* Deletes a pilot file. Returns 1 when the file is gone: proven absent in USER by XvtStorage_Probe, or
 * found there and removed by XvtStorage_Remove. Returns 0 when its presence is unknown or the removal
 * fails. Remove places the file by extension; the two agree for .plt and .pl2 names. */
int Pilot_RemoveFileModern(const char *fileName);

#endif
