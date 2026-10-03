#include "xvt_runtime/runtime/flight_prediction.h"
#include "xvt/flight/player/player.h"
#include "xvt/net/flight_sync.h"
#include "xvt_runtime/runtime/flight_network.h"
#include "xvt_runtime/runtime/flight_sim.h"
#include <string.h>

struct ConfirmedControls {
	struct FlightInputFrameRecord input;
	int tick, slot;
	unsigned signature;
	int valid;
};

static struct ConfirmedControls g_confirmed[XVT_FLIGHT_PLAYERS];

void XvtFlightPrediction_Reset(void)
{
	memset(g_confirmed, 0, sizeof g_confirmed);
}

void XvtFlightPrediction_Confirm(unsigned player, int tick,
				 const struct FlightInputFrameRecord *input)
{
	if (player >= XVT_FLIGHT_PLAYERS || !input) {
		return;
	}
	/* Keep this outside the consumable input queue. Confirmation can prune the
	 * last record while prediction still needs its held controls. */
	g_confirmed[player] = (struct ConfirmedControls){
		*input, tick, g_players[player].objectIndex,
		g_players[player].boundObjectSignature, 1};
}

static int QueuePlayer(unsigned player, int tick)
{
	const struct ConfirmedControls *confirmed = &g_confirmed[player];
	const struct FlightInputFrameRecord *source = NULL;
	int source_tick = -1;
	if (confirmed->valid && confirmed->tick <= tick &&
	    confirmed->slot == g_players[player].objectIndex &&
	    confirmed->signature == g_players[player].boundObjectSignature) {
		source = &confirmed->input;
		source_tick = confirmed->tick;
	}
	int count = g_inputFrameCount[player];
	if (count < 0 || count > XVT_INPUT_HISTORY_CAPACITY) {
		XvtFlightNetwork_RequestRecovery();
		return 0;
	}
	for (int i = 0; i < count; ++i) {
		const struct InputFrame *frame = &g_inputHistory[player][i];
		if (frame->timestamp > tick) {
			break;
		}
		if (frame->inputSource == XVT_INPUT_PREDICTED) {
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
	struct FlightInputFrameRecord input = *source;
	input.key = 0;
	input.flags = 0;
	input.throttle = 0;
	/* Roll changes the meaning of the stick. Preserve it, but never invent
	 * speculative fire/target-button actions or repeat discrete commands. */
	input.keyMods &= 2;
	struct InputFrame *inserted;
	XvtInputInsertStatus status =
		XvtFlightHistory_Insert(player, tick, &input, &inserted);
	if (status == XVT_INPUT_FULL || status == XVT_INPUT_INVALID) {
		XvtFlightNetwork_RequestRecovery();
		return 0;
	}
	if (inserted) {
		inserted->inputSource = XVT_INPUT_PREDICTED;
		inserted->awaitingRelay = 0;
	}
	return 1;
}

int XvtFlightPrediction_Queue(int tick)
{
	if (!XvtFlightWire_ValidTick((unsigned)tick)) {
		return 0;
	}
	for (unsigned player = 0; player < XVT_FLIGHT_PLAYERS; ++player) {
		if (player != (unsigned)g_localPlayer &&
		    g_players[player].participationState &&
		    !QueuePlayer(player, tick)) {
			return 0;
		}
	}
	return 1;
}
