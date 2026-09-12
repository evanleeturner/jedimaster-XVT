#ifndef XVT_RUNTIME_MOVIE_SYNC_H
#define XVT_RUNTIME_MOVIE_SYNC_H

#ifdef __cplusplus
extern "C" {
#endif

void XvtMovieSync_Begin(void);
void XvtMovieSync_Wait(void);
int XvtMovieSync_Tick(void);
void XvtMovieSync_Draw(int top_margin, int bottom_margin);

#ifdef __cplusplus
}
#endif

#endif
