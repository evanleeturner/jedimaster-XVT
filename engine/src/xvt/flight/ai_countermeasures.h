#ifndef XVT_FLIGHT_AI_COUNTERMEASURES_H
#define XVT_FLIGHT_AI_COUNTERMEASURES_H

/* A computer craft's countermeasures: what the question tells the pilot
 * program about the chaff or flares the craft carries and the homing shots
 * aimed at it, and the launch the program may ask for. The 1997 AI decided
 * and launched in one place, at the end of its turret order; the program
 * now decides when, and the engine launches by the 1997 rules. */

#include <stdint.h>

#include "xvt_runtime/ai/ai_wire.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Writes into view the countermeasures of the craft in object_index: its
 * countermeasure type, the countermeasures it has left, whether its
 * countermeasure system works, its chaff seconds and countermeasure
 * cooldown, and the homing shots aimed at it, the first AI_WIRE_THREATS_MAX
 * in projectile slot order, each with its object type, its position and
 * whether a countermeasure already homes on it. */
void ai_countermeasures_view_craft(uint16_t object_index,
				   struct ai_wire_craft_view *view);

/* Launches the countermeasure of the computer craft in object_index, as the
 * 1997 AI did, while it is active, its countermeasure system works and it
 * has one left: chaff adds 10 simulated seconds when none are left and
 * uses a countermeasure unless its flight group's status1 or status2 is 21;
 * a flare launches through laser_createcountermeasureprojectile when the
 * cooldown is 0, which uses one and starts the cooldown. The reader let
 * through only a countermeasure the view showed ready. */
void ai_countermeasures_launch(uint16_t object_index);

#ifdef __cplusplus
}
#endif

#endif
