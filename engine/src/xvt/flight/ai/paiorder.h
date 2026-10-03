#ifndef XVT_FLIGHT_AI_PAIORDER_H
#define XVT_FLIGHT_AI_PAIORDER_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ai_order_progress {
	/* Per order slot: 2 once its plan's goal is met, 3 once a boarding
	 * order has no target left, else 0; set by
	 * paiorder_completegohomeorder. */
	uint8_t completion_state[4];
	/* Per order slot, progress toward the order's goal (laps of the
	 * waypoints, boardings, deliveries), compared with the order's
	 * variable1 or variable2. */
	uint8_t goal_progress[4];
};

typedef int16_t (*pai_order_func)(void);

extern uint8_t g_ai_under_attack_front_side_maneuver_choices[4];
extern uint8_t g_ai_under_attack_rear_maneuver_choices[8];
extern int g_ai_warhead_threat_range_by_skill[4];
extern pai_order_func g_order_table[48];

int16_t paiorder_playerinputorder(void);
int16_t paiorder_nullhandler(void);
int16_t paiorder_updatecourseorder(void);
int16_t paiorder_underattackorder(void);
int16_t paiorder_stillattackorder(void);
int16_t paiorder_flyhomeorder(void);
int16_t paiorder_enterhangarorder(void);
int16_t paiorder_waitrunorder(void);
int16_t paiorder_breakofforder(void);
int16_t paiorder_abortmissionorder(void);
int16_t paiorder_leaderdeadorder(void);
int16_t paiorder_ontailorder(void);
int16_t paiorder_alwaysorder(void);
int16_t paiorder_leadergohomeorder(void);
int16_t paiorder_hyperspaceorder(void);
int16_t paiorder_mothershiporder(void);
int16_t paiorder_lookforcrafttoboardorder(void);
int16_t paiorder_abortboardorder(void);
int16_t paiorder_returnboardorder(void);
int16_t paiorder_awaitboardorder(void);
int16_t paiorder_makedisabledorder(void);
int16_t paiorder_neartargetorder(void);
int16_t paiorder_rocketsonboardorder(void);
int16_t paiorder_avoidhitorder(void);
int16_t paiorder_waitforallreturnorder(void);
int16_t paiorder_waitforallcreateorder(void);
int16_t paiorder_evasiveorder(void);
int16_t paiorder_targetfromplayerorder(void);
int16_t paiorder_avoidstarshiporder(void);
int16_t paiorder_checkhyperorder(void);
int16_t paiorder_stopgohomeorder(void);
int16_t paiorder_completegohomeorder(void);
int16_t paiorder_completegootherorder(void);
int16_t paiorder_waitgootherorder(void);
int16_t paiorder_orderswitchorder(void);
int16_t paiorder_completefolloworder(void);
int16_t paiorder_killselforder(void);
int16_t paiorder_dropoffdestorder(void);
int16_t paiorder_abortmotherwaitorder(void);

#ifdef __cplusplus
}
#endif

#endif
