#ifndef XVT_RUNTIME_CUTSCENE_TASK_H
#define XVT_RUNTIME_CUTSCENE_TASK_H

#ifdef __cplusplus
extern "C" {
#endif

/* Returns -1 while a movie is pending, otherwise the recovered cutscene result. */
int XvtCutsceneTask_Play(int phase);
void XvtCutsceneTask_Reset(void);

#ifdef __cplusplus
}
#endif

#endif
