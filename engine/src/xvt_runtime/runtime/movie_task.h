#ifndef XVT_RUNTIME_MOVIE_TASK_H
#define XVT_RUNTIME_MOVIE_TASK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { XVT_MOVIE_PENDING = -1 };

int XvtMovieTask_Begin(const char* name, int synchronize);
void XvtMovieTask_Tick(void);
void XvtMovieTask_PausedFrame(void);
void XvtMovieTask_ReapFinished(void);
int XvtMovieTask_IsActive(void);
int XvtMovieTask_ContinuesWithoutFocus(void);
int XvtMovieTask_TakeResult(int* result);
void XvtMovieTask_Stop(void);
void XvtMovieTask_SuppressClassicSubtitles(void);
uint64_t XvtMovieTask_NextWakeDelayUs(void);
void XvtMovieTask_Shutdown(void);

#ifdef __cplusplus
}
#endif

#endif
