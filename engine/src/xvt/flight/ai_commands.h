#ifndef XVT_FLIGHT_AI_COMMANDS_H
#define XVT_FLIGHT_AI_COMMANDS_H

/* A computer craft's commands from a player, for the pilot program: what the
 * wingman keys wrote into its record. Shift+W marks it waiting and Shift+G
 * ends the wait; Shift+E sets the evade command; the attack and cover orders
 * give the craft a target to attack, and the ignore order a target to leave
 * alone. The home command goes by the books (ai_books.h). The 1997 AI read
 * these fields in its plans; the program now does, and the engine only says
 * what they hold. */

#include <stdint.h>

#include "xvt_runtime/ai/ai_wire.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Writes into view the player's commands to the craft in object_index: the
 * wait mark and the evade command as command bits, the target to attack
 * (0xFFFF for none, and while the evade command stands) and the target to
 * leave alone (0xFFFF for none). A target to attack whose object slot is
 * empty is cleared first, as the 1997 AI cleared a candidate it could no
 * longer target. */
void ai_commands_view_craft(uint16_t object_index,
			    struct ai_wire_craft_view *view);

/* Ends the evade command of the craft in object_index, which the program
 * took up by dropping its target: its candidate target becomes 0xFFFF, as
 * the 1997 AI's evasive order left it. */
void ai_commands_evaded(uint16_t object_index);

#ifdef __cplusplus
}
#endif

#endif
