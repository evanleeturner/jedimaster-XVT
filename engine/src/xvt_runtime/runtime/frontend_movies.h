#ifndef XVT_RUNTIME_FRONTEND_MOVIES_H
#define XVT_RUNTIME_FRONTEND_MOVIES_H

#ifdef __cplusplus
extern "C" {
#endif

int XvtFrontendMovies_PlayViewer(const char* name);
int XvtFrontendMovies_ResumeViewer(void);
void XvtFrontendMovies_Reset(void);

#ifdef __cplusplus
}
#endif

#endif
