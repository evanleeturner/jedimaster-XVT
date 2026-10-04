#include "xvt_runtime/runtime/flight_prediction.h"

#include <string.h>

#include "xvt/flight/player/player.h"
#include "xvt/net/flight_sync.h"
#include "xvt_runtime/runtime/flight_network.h"
#include "xvt_runtime/runtime/flight_sim.h"

struct confirmed_controls {
	struct flight_input_frame_record input;
	int tick, slot;
	unsigned signature;
	int valid;
};

static struct confirmed_controls g_confirmed[XVT_FLIGHT_PLAYERS];

void xvt_flight_prediction_reset(void)
{
	memset(g_confirmed, 0, sizeof g_confirmed);
}

void xvt_flight_prediction_confirm(
	unsigned player, int tick,
	const struct flight_input_frame_record *input)
{
	if (player >= XVT_FLIGHT_PLAYERS || !input) {
		return;
	}
	/* Keep this outside the consumable input queue. Confirmation can prune the
	 * last record while prediction still needs its held controls. */
	g_confirmed[player] = (struct confirmed_controls){
		*input, tick, g_players[player].object_index,
		g_players[player].bound_object_signature, 1};
}

static int queue_player(unsigned player, int tick)
{
	const struct confirmed_controls *confirmed = &g_confirmed[player];
	const struct flight_input_frame_record *source = NULL;
	int source_tick = -1;
	if (confirmed->valid && confirmed->tick <= tick &&
	    confirmed->slot == g_players[player].object_index &&
	    confirmed->signature == g_players[player].bound_object_signature) {
		source = &confirmed->input;
		source_tick = confirmed->tick;
	}
	int count = g_input_frame_count[player];
	if (count < 0 || count > XVT_INPUT_HISTORY_CAPACITY) {
		xvt_flight_network_request_recovery();
		return 0;
	}
	for (int i = 0; i < count; ++i) {
		const struct input_frame *frame = &g_input_history[player][i];
		if (frame->timestamp > tick) {
			break;
		}
		if (frame->input_source == XVT_INPUT_PREDICTED) {
			continue;
		}
		/* Generic insertion may replace an unconsumed real record. Prediction
		 * must leave both real and authoritative samples at this tick intact. */
		if (frame->timestamp == tick) {
			return 1;
		}
		if (frame->timestamp >= source_tick) {
			source = &frame->input;
			source_tick = frame->timestamp;
		}
	}
	if (!source) {
		return 1;
	}
	struct flight_input_frame_record input = *source;
	input.key = 0;
	input.flags = 0;
	input.throttle = 0;
	/* Roll changes the meaning of the stick. Preserve it, but never invent
	 * speculative fire/target-button actions or repeat discrete commands. */
	input.key_mods &= 2;
	struct input_frame *inserted;
	xvt_input_insert_status status =
		xvt_flight_history_insert(player, tick, &input, &inserted);
	if (status == XVT_INPUT_FULL || status == XVT_INPUT_INVALID) {
		xvt_flight_network_request_recovery();
		return 0;
	}
	if (inserted) {
		inserted->input_source = XVT_INPUT_PREDICTED;
		inserted->awaiting_relay = 0;
	}
	return 1;
}

int xvt_flight_prediction_queue(int tick)
{
	if (!xvt_flight_wire_valid_tick((unsigned)tick)) {
		return 0;
	}
	for (unsigned player = 0; player < XVT_FLIGHT_PLAYERS; ++player) {
		if (player != (unsigned)g_local_player &&
		    g_players[player].participation_state &&
		    !queue_player(player, tick)) {
			return 0;
		}
	}
	return 1;
}
