#ifndef XVT_RUNTIME_CAMPAIGN_TASK_H
#define XVT_RUNTIME_CAMPAIGN_TASK_H

#ifdef __cplusplus
extern "C" {
#endif

enum { XVT_CAMPAIGN_PENDING = -1 };

/* Entry prefixes return pending, cancelled (0), or ready for the screen tail (1). */
int XvtCampaignTask_EnterTeams(void);
int XvtCampaignTask_EnterDebrief(void);
int XvtCampaignTask_WaitPacket(int packet_type, int** packet);
int XvtCampaignTask_IsPending(void);
int XvtCampaignTask_ContinuesWithoutFocus(void);
void XvtCampaignTask_Reset(void);

#ifdef __cplusplus
}
#endif

#endif
