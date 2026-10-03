#include "xvt/net/flight_sync.h"
#ifdef XVT_MODERN
#include "xvt_runtime/input/flight_controls.h"
#include "xvt_runtime/runtime/flight_messages.h"
#include "xvt_runtime/runtime/flight_network.h"
#include "xvt_runtime/runtime/flight_sim.h"
#include "xvt_runtime/runtime/resync_task.h"
#include "xvt_runtime/timing/flight_timing.h"
#endif

#include "xvt/audio/sound.h"
#include "xvt/flight/fediskio.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/flight_view.h"
#include "xvt/flight/fview.h"
#include "xvt/flight/object/collide.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/math/math.h"
#include "xvt/net/flight_net.h"
#include "xvt/net/net_reliable.h"
#include "xvt/net/net_session.h"
#include "xvt/util/memory.h"
#include <string.h>

enum { INPUT_FRAME_PREDICTED = 2 };

/* Frames held in each player's row of g_inputHistory, 0 to 450. Six writers;
 * chiefly FlightSync_InsertInputFrame (XvtFlightHistory_Insert in the modern
 * build) and FlightSync_RemoveInputHistoryFrame. Flight start sets every
 * count to 0: Flight_MainLoop in the original build, XvtFlightLoading_Globals
 * in the modern one. */
// GLOBAL: XVT 0x9A8DB0
int g_inputFrameCount[8] = {0};
/* Each player's input frames in time stamp order, up to 450 per player, with
 * where each came from (inputSource) and whether it still awaits relay to the
 * other players. Many writers; chiefly FlightSync_InsertInputFrame and
 * FlightSync_RemoveInputHistoryFrame. */
// GLOBAL: XVT 0x9ED670
struct InputFrame g_inputHistory[8][450] = {{{0}}};
/* Set to 1 by FlightSync_ApplyResyncAndReplayWorldMessages as it loads a
 * resent world state; the next FlightSync_ApplyWorldMessagePacket, after
 * restoring that state, marks every live object's move vector and
 * orientation matrix for recomputing and sets it back to 0. Only the
 * original build sets or reads it. */
// GLOBAL: XVT 0x51BF40
int g_flightNetDirtyAllObjectTransformsAfterRestore = 0;
#ifndef XVT_MODERN
/* Bytes allocated for g_worldMessageBuffer. Only
 * FlightSync_BufferWorldMessagePacket writes it: it grows by 100 times the
 * size of a message that does not fit, and never shrinks. */
// GLOBAL: XVT 0x51BF48
static int g_worldMessageBufferCapacity;
/* Bytes still free at the end of g_worldMessageBuffer. Lowered by
 * FlightSync_BufferWorldMessagePacket; set back to the capacity by
 * FlightSync_ClearBufferedWorldMessages and
 * FlightSync_ReplayBufferedWorldMessages. */
// GLOBAL: XVT 0x51BF4C
static int g_worldMessageBufferBytesFree;
/* World messages held in g_worldMessageBuffer. Raised by
 * FlightSync_BufferWorldMessagePacket; FlightSync_ReplayBufferedWorldMessages
 * counts it down to 0, and FlightSync_ClearBufferedWorldMessages sets 0. */
// GLOBAL: XVT 0x51BF50
static int g_worldMessageBufferedCount;
/* Memory handle of g_worldMessageBuffer; 0 until the first message is
 * buffered. FlightSync_BufferWorldMessagePacket replaces it with a larger
 * one, freeing the old, when a message does not fit; nothing else frees it. */
// GLOBAL: XVT 0x51BF54
static uint16_t g_worldMessageBufferHandle = 0;
#endif
/* 1 when remote players' craft are drawn smoothed. Starts at 1; flight start
 * copies g_internetPlayEnabled into it: Flight_MainLoop in the original
 * build, XvtFlightLoading_Globals in the modern one. */
// GLOBAL: XVT 0x523430
int g_remotePlayerRenderSmoothingEnabled = 1;
/* Per player, the pose and motion of the remote craft as last drawn, taken by
 * FlightSync_CaptureSamplesAndRestorePoses after each frame is drawn;
 * FlightSync_ApplyRemotePlayerRenderSmoothing predicts the next drawn pose
 * from it. FlightSync_ResetRemotePlayerRenderSmoothing marks all invalid. */
// GLOBAL: XVT 0x550888
struct RemotePlayerRenderSample g_remotePlayerRenderSamples[8];
/* Per player, the simulated pose FlightSync_ApplyRemotePlayerRenderSmoothing
 * saves before it moves the craft to its drawn pose;
 * FlightSync_CaptureSamplesAndRestorePoses puts it back after drawing. */
// GLOBAL: XVT 0x550A08
struct RemotePlayerSavedSimPose g_remotePlayerSavedSimPoses[8];
#ifndef XVT_MODERN
/* Locked memory of g_worldMessageBufferHandle, where a client keeps the
 * server's world messages back to back while
 * g_flightNetBufferWorldMessagesUntilChecksum is 1, from a world checksum
 * until the checksum is confirmed or a resync replays them. NULL until the
 * first message is buffered. */
// GLOBAL: XVT 0x550B90
static uint8_t *g_worldMessageBuffer = NULL;
#endif

#ifndef XVT_MODERN
/* For every active remote player with any input frames, adds a predicted
 * frame at the last frame's time stamp plus predictedFrameDelta, with that
 * frame's two axes and no key or modifiers, marked predicted (inputSource 2)
 * and not awaiting relay, where FlightSync_InsertInputFrame accepts it. Does
 * nothing in internet play. Only the original build calls this. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x418500
void FlightSync_QueuePredictedRemoteInputFrames(int predictedFrameDelta)
{
	int playerIdx;
	struct FlightInputFrameRecord input;

	if (g_internetPlayEnabled != 0) {
		return;
	}
	memset(&input, 0, sizeof(input));
	for (playerIdx = 0; playerIdx < 8; ++playerIdx) {
		int count;
		struct InputFrame *lastFrame;
		struct InputFrame *predictedFrame;

		if (g_players[playerIdx].participationState == 0 ||
		    playerIdx == g_localPlayer) {
			continue;
		}
		count = g_inputFrameCount[playerIdx];
		if (count == 0) {
			continue;
		}
		lastFrame = &g_inputHistory[playerIdx][count - 1];
		input.axisX = lastFrame->input.axisX;
		input.axisY = lastFrame->input.axisY;
		predictedFrame = FlightSync_InsertInputFrame(
			playerIdx, lastFrame->timestamp + predictedFrameDelta,
			&input);
		if (predictedFrame != NULL) {
			predictedFrame->awaitingRelay = 0;
			predictedFrame->inputSource = INPUT_FRAME_PREDICTED;
		}
	}
}
#endif

/* Removes every predicted frame (inputSource 2, not awaiting relay) from the
 * input history of every active remote player; in internet play it does
 * nothing. Only the original build calls this. */
// FUNCTION: XVT 0x4185B0
void FlightSync_DiscardAllPredictedInputFrames(void)
{
	int playerIndex;

#ifndef XVT_MODERN
	if (g_internetPlayEnabled != 0) {
		return;
	}
#endif

	for (playerIndex = 0; playerIndex < 8; ++playerIndex) {
		if (g_players[playerIndex].participationState != 0 &&
		    playerIndex != g_localPlayer) {
			int frameIndex;
			struct InputFrame *frame;

			frame = g_inputHistory[playerIndex];
			frameIndex = 0;
			while (g_inputFrameCount[playerIndex] > frameIndex) {
				if (frame->awaitingRelay == 0 &&
				    frame->inputSource ==
					    INPUT_FRAME_PREDICTED) {
					FlightSync_RemoveInputHistoryFrame(
						playerIndex, frame);
				} else {
					++frame;
					++frameIndex;
				}
			}
		}
	}
}

/* Removes the predicted frames (inputSource 2, not awaiting relay) from one
 * player's input history. Does nothing for an inactive player or the local
 * one, nor, in the original build, in internet play. */
// FUNCTION: XVT 0x418650
void FlightSync_DiscardPredictedInputFrames(int playerIdx)
{
	int frameIndex;
	struct InputFrame *frame;

	if (
#ifndef XVT_MODERN
		g_internetPlayEnabled != 0 ||
#endif
		g_players[playerIdx].participationState == 0 ||
		playerIdx == g_localPlayer)
		return;

	frameIndex = 0;
	frame = g_inputHistory[playerIdx];
	while (frameIndex < g_inputFrameCount[playerIdx]) {
		if (frame->awaitingRelay == 0 &&
		    frame->inputSource == INPUT_FRAME_PREDICTED) {
			FlightSync_RemoveInputHistoryFrame(playerIdx, frame);
		} else {
			++frame;
			++frameIndex;
		}
	}
}

/* Removes the frame that frame points at from a player's input history,
 * moving later frames down one place and lowering g_inputFrameCount. Does
 * nothing when the history is empty or the pointer lies before the player's
 * row; does not check that it lies among the frames in use. */
// FUNCTION: XVT 0x4186E0
void FlightSync_RemoveInputHistoryFrame(int playerIdx, struct InputFrame *frame)
{
	int frameCount;
	int copyIndex;
	struct InputFrame *current;

	frameCount = g_inputFrameCount[playerIdx];
	if (frameCount != 0) {
		current = g_inputHistory[playerIdx];
		if (current <= frame) {
			--frameCount;
			g_inputFrameCount[playerIdx] = frameCount;
			copyIndex = 0;
			while (copyIndex < g_inputFrameCount[playerIdx]) {
				if (current >= frame) {
					*current = current[1];
				}
				++copyIndex;
				++current;
			}
		}
	}
}

/* Puts an input frame into a player's history, kept in time stamp order, and
 * returns it, or NULL when refused. The modern build hands the work to
 * XvtFlightHistory_Insert and, when the history is full under the network
 * timing (XvtFlightTiming_IsNetwork125), calls
 * XvtFlightNetwork_RequestRecovery. In the original build a new time stamp is
 * inserted, refused when all 450 frames are in use; a frame with the same
 * time stamp is overwritten unless it came from the server (inputSource 0)
 * or awaits relay, which refuses it. The frame gets inputSource 1 and
 * awaitingRelay 0; callers may change them afterwards. */
// FUNCTION: XVT 0x418760
struct InputFrame *
FlightSync_InsertInputFrame(int playerIdx, int timestamp,
			    const struct FlightInputFrameRecord *input)
{
#ifdef XVT_MODERN
	struct InputFrame *inserted;
	XvtInputInsertStatus status = XvtFlightHistory_Insert(
		(unsigned)playerIdx, timestamp, input, &inserted);
	if (status == XVT_INPUT_FULL && XvtFlightTiming_IsNetwork125()) {
		XvtFlightNetwork_RequestRecovery();
	}
	return inserted;
#else

	struct InputFrame *arrayEnd;
	int existingTimestamp;
	int frameCount;
	int frameIndex;
	struct InputFrame *frame;

	frameIndex = 0;
	frameCount = g_inputFrameCount[playerIdx];
	frame = g_inputHistory[playerIdx];
	arrayEnd = &frame[frameCount];

	while (frameIndex < frameCount && frame->timestamp < timestamp) {
		++frameIndex;
		++frame;
	}
	existingTimestamp = frame->timestamp;
	if (existingTimestamp > timestamp || frameIndex == frameCount) {
		if (frameCount == 450) {
			return NULL;
		}
		g_inputFrameCount[playerIdx] = frameCount + 1;
		if (arrayEnd > frame) {
			frameIndex = frameCount - frameIndex;
			do {
				--frameIndex;
				frame[frameIndex + 1] = frame[frameIndex];
			} while (frameIndex != 0);
		}
	} else if (existingTimestamp == timestamp) {
		if (frame->inputSource == 0) {
			return NULL;
		}
		if (frame->awaitingRelay == 1) {
			return NULL;
		}
	}
	frame->timestamp = timestamp;
	frame->inputSource = 1;
	frame->awaitingRelay = 0;
	frame->input = *input;
	return frame;

#endif
}

/* Returns the last frame in a player's input history that still awaits relay
 * (awaitingRelay nonzero), or NULL when none does. */
// FUNCTION: XVT 0x418890
struct InputFrame *FlightSync_FindLastUnrelayedInputFrame(int playerIdx)
{
	struct InputFrame *frame;
	int frameCount;
	struct InputFrame *result;

	frame = g_inputHistory[playerIdx];
	frameCount = g_inputFrameCount[playerIdx];
	result = 0;
	while (frameCount > 0) {
		if (frame->awaitingRelay != 0) {
			result = frame;
		}
		++frame;
		--frameCount;
	}
	return result;
}

/* Marks all eight render samples and saved simulated poses invalid. */
// FUNCTION: XVT 0x418950
void FlightSync_ResetRemotePlayerRenderSmoothing(void)
{
	int playerIndex;

	for (playerIndex = 0; playerIndex < 8; ++playerIndex) {
		g_remotePlayerRenderSamples[playerIndex].valid = 0;
		g_remotePlayerSavedSimPoses[playerIndex].valid = 0;
	}
}

/* Runs after a frame is drawn. For each active remote player whose craft
 * exists it records in g_remotePlayerRenderSamples the position and angles
 * the craft was drawn at, the change in each angle since the last sample
 * (none when there was no valid sample), and its move vector, speed and
 * simulation time stamp, recomputing the move vector first when it is
 * stale. It then puts back the simulated pose that
 * FlightSync_ApplyRemotePlayerRenderSmoothing saved. Every other player's
 * sample becomes invalid. Does nothing when
 * g_remotePlayerRenderSmoothingEnabled is 0. */
// FUNCTION: XVT 0x418970
void FlightSync_CaptureSamplesAndRestorePoses(void)
{
	int playerIndex;

	if (g_remotePlayerRenderSmoothingEnabled == 0) {
		return;
	}

	playerIndex = 0;
	do {
		struct PlayerData *player = &g_players[playerIndex];
		int sampleWasValid =
			g_remotePlayerRenderSamples[playerIndex].valid;
		g_remotePlayerRenderSamples[playerIndex].valid = 0;
		if (g_players[playerIndex].participationState != 0 &&
		    g_localPlayer != playerIndex && player->objectIndex != -1) {
			struct ObjectRecord *object =
				&g_objectTable[player->objectIndex];
			if (object->objectType != 0 && object->mobj != NULL) {
				if (sampleWasValid == 0) {
					g_remotePlayerRenderSamples[playerIndex]
						.roll = object->roll;
					g_remotePlayerRenderSamples[playerIndex]
						.pitch = object->pitch;
					g_remotePlayerRenderSamples[playerIndex]
						.yaw = object->yaw;
				}

				g_remotePlayerRenderSamples[playerIndex].valid =
					1;
				g_remotePlayerRenderSamples[playerIndex]
					.objectSignature =
					object->objectSignature;
				g_remotePlayerRenderSamples[playerIndex]
					.worldX = object->world_x;
				g_remotePlayerRenderSamples[playerIndex]
					.worldY = object->world_y;
				g_remotePlayerRenderSamples[playerIndex]
					.worldZ = object->world_z;
				g_remotePlayerRenderSamples[playerIndex]
					.rollDelta =
					object->roll -
					(uint16_t)g_remotePlayerRenderSamples
						[playerIndex]
							.roll;
				g_remotePlayerRenderSamples[playerIndex]
					.pitchDelta =
					object->pitch -
					(uint16_t)g_remotePlayerRenderSamples
						[playerIndex]
							.pitch;
				g_remotePlayerRenderSamples[playerIndex]
					.yawDelta =
					object->yaw -
					(uint16_t)g_remotePlayerRenderSamples
						[playerIndex]
							.yaw;
				g_remotePlayerRenderSamples[playerIndex].roll =
					object->roll;
				g_remotePlayerRenderSamples[playerIndex].pitch =
					object->pitch;
				g_remotePlayerRenderSamples[playerIndex].yaw =
					object->yaw;

				if (object->mobj->moveVectorDirty != 0) {
					FVIEW_calcrotatemove(object->pitch,
							     object->yaw,
							     object);
				}
				g_remotePlayerRenderSamples[playerIndex].moveX =
					object->mobj->moveX;
				g_remotePlayerRenderSamples[playerIndex].moveY =
					object->mobj->moveY;
				g_remotePlayerRenderSamples[playerIndex].moveZ =
					object->mobj->moveZ;
				g_remotePlayerRenderSamples[playerIndex]
					.speedMagnitude = object->mobj->speed;
				g_remotePlayerRenderSamples[playerIndex]
					.simStateTimestamp =
					object->mobj->simStateTimestamp;

				if (g_remotePlayerSavedSimPoses[playerIndex]
					    .valid != 0) {
					object->roll =
						g_remotePlayerSavedSimPoses
							[playerIndex]
								.roll;
					object->pitch =
						g_remotePlayerSavedSimPoses
							[playerIndex]
								.pitch;
					object->yaw =
						g_remotePlayerSavedSimPoses
							[playerIndex]
								.yaw;
					object->world_x =
						g_remotePlayerSavedSimPoses
							[playerIndex]
								.worldX;
					object->world_y =
						g_remotePlayerSavedSimPoses
							[playerIndex]
								.worldY;
					object->world_z =
						g_remotePlayerSavedSimPoses
							[playerIndex]
								.worldZ;
				}
			}
		}
		++playerIndex;
	} while (playerIndex < 8);
}

/* Runs before a frame is drawn. Moves each active remote player's craft from
 * its simulated pose to a smoothed one, after saving the simulated pose in
 * g_remotePlayerSavedSimPoses for FlightSync_CaptureSamplesAndRestorePoses to
 * put back. Leaves a craft as simulated when its sample is invalid or belongs
 * to another object, or when its simulation time stamp is older than the
 * sample's. The position is projected from the sampled one along the sampled
 * move vector, by a distance that grows with the sampled speed and the
 * simulation time since the sample, then moved toward the simulated position
 * by half the gap, or a smaller share when the gap is within 32 times that
 * distance. An angle that moved against the sampled turn is held at the
 * sampled angle. The step that compares the change with maxAngleChange sets
 * each angle to a value equal to itself modulo 65,536, so it changes
 * nothing. Does nothing when g_remotePlayerRenderSmoothingEnabled is 0. */
// FUNCTION: XVT 0x418B70
void FlightSync_ApplyRemotePlayerRenderSmoothing(void)
{
	int playerIndex;
	struct ObjectRecord *object;
	int predictedWorldX;
	int predictedWorldY;
	int predictedWorldZ;
	int positionDeltaX;
	int positionDeltaY;
	int positionDeltaZ;
	int elapsedTime;
	int predictionDistance;
	int roughDistance;
	int positionBlend;
	int maxAngleChange;
	int angleDifference;
	int signedAngleDifference;
	int candidateAngle;
	int candidateDifference;
	int blendedX;
	int blendedY;
	int blendedZ;

	if (g_remotePlayerRenderSmoothingEnabled == 0) {
		return;
	}

	for (playerIndex = 0; playerIndex < 8; ++playerIndex) {
		g_remotePlayerSavedSimPoses[playerIndex].valid = 0;
		if (g_players[playerIndex].participationState == 0 ||
		    g_players[playerIndex].objectIndex == -1) {
			continue;
		}

		object = &g_objectTable[g_players[playerIndex].objectIndex];
		if (object->objectType == 0 || object->mobj == NULL ||
		    g_remotePlayerRenderSamples[playerIndex].valid == 0 ||
		    playerIndex == g_localPlayer ||
		    g_remotePlayerRenderSamples[playerIndex].objectSignature !=
			    g_players[playerIndex].boundObjectSignature) {
			continue;
		}

		g_remotePlayerSavedSimPoses[playerIndex].roll = object->roll;
		g_remotePlayerSavedSimPoses[playerIndex].pitch = object->pitch;
		g_remotePlayerSavedSimPoses[playerIndex].yaw = object->yaw;
		g_remotePlayerSavedSimPoses[playerIndex].worldX =
			object->world_x;
		g_remotePlayerSavedSimPoses[playerIndex].worldY =
			object->world_y;
		g_remotePlayerSavedSimPoses[playerIndex].worldZ =
			object->world_z;
		g_remotePlayerSavedSimPoses[playerIndex].valid = 1;

		predictedWorldX =
			g_remotePlayerRenderSamples[playerIndex].worldX;
		predictedWorldY =
			g_remotePlayerRenderSamples[playerIndex].worldY;
		predictedWorldZ =
			g_remotePlayerRenderSamples[playerIndex].worldZ;

		elapsedTime = object->mobj->simStateTimestamp -
			      g_remotePlayerRenderSamples[playerIndex]
				      .simStateTimestamp;
		if (elapsedTime < 0) {
			continue;
		}

		predictionDistance = 0;
		if (elapsedTime > 0 &&
		    g_remotePlayerRenderSamples[playerIndex].speedMagnitude !=
			    0) {
			predictionDistance =
				elapsedTime *
				((4660 * g_remotePlayerRenderSamples
						  [playerIndex]
							  .speedMagnitude +
				  128) >>
				 8) /
				SIMULATION_TICKS_PER_SECOND;
			predictedWorldX += Math_MulQ15(
				g_remotePlayerRenderSamples[playerIndex].moveX,
				predictionDistance);
			predictedWorldY += Math_MulQ15(
				g_remotePlayerRenderSamples[playerIndex].moveY,
				predictionDistance);
			predictedWorldZ += Math_MulQ15(
				g_remotePlayerRenderSamples[playerIndex].moveZ,
				predictionDistance);
		}

		positionDeltaX = object->world_x - predictedWorldX;
		positionDeltaY = object->world_y - predictedWorldY;
		positionDeltaZ = object->world_z - predictedWorldZ;
		roughDistance = collide_roughdistance3d(
			positionDeltaX, positionDeltaY, positionDeltaZ);
		predictionDistance *= 32;
		if (predictionDistance >= roughDistance && roughDistance != 0) {
			positionBlend =
				(roughDistance << 14) / predictionDistance;
		} else {
			positionBlend = 0x4000;
		}
		blendedX = Math_MulQ15(positionBlend, positionDeltaX);
		blendedY = Math_MulQ15(positionBlend, positionDeltaY);
		blendedZ = Math_MulQ15(positionBlend, positionDeltaZ);
		predictedWorldX += blendedX;
		predictedWorldY += blendedY;
		predictedWorldZ += blendedZ;
		object->world_x = predictedWorldX;
		object->world_y = predictedWorldY;
		object->world_z = predictedWorldZ;

		angleDifference =
			(int16_t)(object->roll -
				  g_remotePlayerRenderSamples[playerIndex]
					  .roll);
		signedAngleDifference = angleDifference;
		if (g_remotePlayerRenderSamples[playerIndex].rollDelta > 0) {
			if (angleDifference < 0) {
				object->roll =
					g_remotePlayerRenderSamples[playerIndex]
						.roll;
				angleDifference = 0;
				signedAngleDifference = 0;
			}
		} else if (g_remotePlayerRenderSamples[playerIndex].rollDelta <
			   0) {
			if (signedAngleDifference > 0) {
				object->roll =
					g_remotePlayerRenderSamples[playerIndex]
						.roll;
				angleDifference = 0;
				signedAngleDifference = 0;
			}
		}
		if (angleDifference < 0) {
			angleDifference = -angleDifference;
		}
		maxAngleChange =
			6144 * elapsedTime / SIMULATION_TICKS_PER_SECOND;
		if (angleDifference > maxAngleChange) {
			candidateAngle =
				g_remotePlayerRenderSamples[playerIndex].roll +
				signedAngleDifference;
			candidateDifference = object->roll - candidateAngle;
			if (candidateDifference < 0) {
				candidateDifference = -candidateDifference;
			}
			if (8 * maxAngleChange > candidateDifference) {
				object->roll = candidateAngle;
			}
		}

		angleDifference =
			(int16_t)(object->pitch -
				  g_remotePlayerRenderSamples[playerIndex]
					  .pitch);
		signedAngleDifference = angleDifference;
		if (g_remotePlayerRenderSamples[playerIndex].pitchDelta > 0) {
			if (angleDifference < 0) {
				object->pitch =
					g_remotePlayerRenderSamples[playerIndex]
						.pitch;
				angleDifference = 0;
				signedAngleDifference = 0;
			}
		} else if (g_remotePlayerRenderSamples[playerIndex].pitchDelta <
			   0) {
			if (signedAngleDifference > 0) {
				object->pitch =
					g_remotePlayerRenderSamples[playerIndex]
						.pitch;
				angleDifference = 0;
				signedAngleDifference = 0;
			}
		}
		if (angleDifference < 0) {
			angleDifference = -angleDifference;
		}
		if (angleDifference > maxAngleChange) {
			candidateAngle =
				g_remotePlayerRenderSamples[playerIndex].pitch +
				signedAngleDifference;
			candidateDifference = object->pitch - candidateAngle;
			if (candidateDifference < 0) {
				candidateDifference = -candidateDifference;
			}
			if (8 * maxAngleChange > candidateDifference) {
				object->pitch = candidateAngle;
			}
		}

		angleDifference =
			(int16_t)(object->yaw -
				  g_remotePlayerRenderSamples[playerIndex].yaw);
		signedAngleDifference = angleDifference;
		if (g_remotePlayerRenderSamples[playerIndex].yawDelta > 0) {
			if (angleDifference < 0) {
				object->yaw =
					g_remotePlayerRenderSamples[playerIndex]
						.yaw;
				angleDifference = 0;
				signedAngleDifference = 0;
			}
		} else if (g_remotePlayerRenderSamples[playerIndex].yawDelta <
			   0) {
			if (signedAngleDifference > 0) {
				object->yaw =
					g_remotePlayerRenderSamples[playerIndex]
						.yaw;
				angleDifference = 0;
				signedAngleDifference = 0;
			}
		}
		if (angleDifference < 0) {
			angleDifference = -angleDifference;
		}
		if (angleDifference > maxAngleChange) {
			candidateAngle =
				g_remotePlayerRenderSamples[playerIndex].yaw +
				signedAngleDifference;
			candidateDifference = object->yaw - candidateAngle;
			if (candidateDifference < 0) {
				candidateDifference = -candidateDifference;
			}
			if (8 * maxAngleChange > candidateDifference) {
				object->yaw = candidateAngle;
			}
		}
	}
}

#ifndef XVT_MODERN
/* Applies one world message from the server. A client first copies it into
 * the replay buffer while g_flightNetBufferWorldMessagesUntilChecksum is 1.
 * The tick is word 1 without its top bit, which asks for a world checksum. A
 * tick not past g_serverTickTime is ignored; one that is not exactly
 * g_netUpdateIntervalTicks past it first empties the flight receive queue
 * (NetReliable_ResetRecvQueueState). It then drops predicted inputs, restores
 * the saved world state of g_serverTickTime (marking every live object's
 * transforms for recomputing when
 * g_flightNetDirtyAllObjectTransformsAfterRestore is set), inserts each
 * active player's inputs from the message as server frames (inputSource 0),
 * runs the simulation to the tick, updates the cameras, flushes queued
 * sounds and saves the new state; g_gameTime and g_serverTickTime become the
 * tick. When a checksum is asked for, it computes one, stores the tick in
 * g_flightNetWorldChecksumEpoch, sends it to the host (a host also broadcasts
 * it), turns buffering on, snapshots the state and empties the buffer. Only
 * the original build calls this. */
// FUNCTION: XVT 0x418F80
void FlightSync_ApplyWorldMessagePacket(uint8_t *packet)
{
	enum {
		PLAYER_SLOT_COUNT = 8,
		FULL_TIMESTAMP_CODE = 127,
		SHORT_DELTA_CODE = 126,
		BYTE_DELTA_CODE = 125,
		KEY_PRESENT_FLAG = 0x80,
		DELTA_CODE_MASK = 0x7F,
		WORLD_CHECKSUM_FLAG = INT32_MIN,
		WORLD_TIMESTAMP_MASK = 0x7FFFFFFFu
	};

	uint32_t rawPacketTick;
	int objectIndex;
	int playerIndex;
	int packetTick;
	int checksumRequested;
	struct FlightInputFrameRecord input;

	if (NetSession_IsLocalHost() == 0 &&
	    g_flightNetBufferWorldMessagesUntilChecksum == 1) {
		FlightSync_BufferWorldMessagePacket(packet);
	}

	rawPacketTick = ((const uint32_t *)packet)[1];
	packet += 2 * sizeof(int);
	packetTick = (int)(rawPacketTick & WORLD_TIMESTAMP_MASK);
	checksumRequested = (int)(rawPacketTick & WORLD_CHECKSUM_FLAG);
	if (packetTick <= g_serverTickTime) {
		return;
	}

	if (packetTick - g_netUpdateIntervalTicks != g_serverTickTime) {
		NetReliable_ResetRecvQueueState();
	}
	FlightSync_DiscardAllPredictedInputFrames();
	Flight_RestoreWorldState();
	g_gameTime = g_serverTickTime;

	if (g_flightNetDirtyAllObjectTransformsAfterRestore != 0) {
		for (objectIndex = 0; objectIndex < g_regionMainObjectSlotEnd;
		     ++objectIndex) {
			if (g_objectTable[objectIndex].objectType != 0 &&
			    g_objectTable[objectIndex].mobj != NULL) {
				g_objectTable[objectIndex]
					.mobj->moveVectorDirty = 1;
				g_objectTable[objectIndex]
					.mobj->orientMatrixDirty = 1;
			}
		}
		g_flightNetDirtyAllObjectTransformsAfterRestore = 0;
	}

	{
		uint8_t *cursor;
		int remainingPlayerBlocks;

		cursor = packet;
		remainingPlayerBlocks = *cursor++;
		for (playerIndex = 0; playerIndex < PLAYER_SLOT_COUNT;
		     ++playerIndex) {
			int frameCount;

			if (g_players[playerIndex].participationState == 0) {
				continue;
			}
			if (remainingPlayerBlocks == 0) {
				break;
			}

			--remainingPlayerBlocks;
			frameCount = *cursor++;
			while (frameCount > 0) {
				struct InputFrame *inserted;
				int timestampCode;
				int deltaCode;
				int timestamp;

				timestampCode = *cursor++;
				deltaCode = timestampCode & DELTA_CODE_MASK;
				if (deltaCode == FULL_TIMESTAMP_CODE) {
					timestamp = *(const int *)cursor;
					cursor += sizeof(timestamp);
				} else if (deltaCode == SHORT_DELTA_CODE) {
					timestamp = packetTick -
						    *(const uint16_t *)cursor;
					cursor += sizeof(uint16_t);
				} else {
					timestamp = packetTick;
					if (deltaCode == BYTE_DELTA_CODE) {
						timestamp -= *cursor++;
					}
					timestamp -= deltaCode;
				}

				if ((timestampCode & KEY_PRESENT_FLAG) != 0) {
					input.key = *cursor++;
				} else {
					input.key = 0;
				}
				input.axisX =
					(int8_t)(cursor[0] & (uint8_t)~1u);
				input.axisY =
					(int8_t)(cursor[1] & (uint8_t)~1u);
				input.keyMods = cursor[1] & 1u;
				input.keyMods = (uint8_t)(input.keyMods << 1);
				input.keyMods |= cursor[0] & 1u;
				cursor += 2;

				inserted = FlightSync_InsertInputFrame(
					playerIndex, timestamp, &input);
				if (inserted != NULL) {
					inserted->inputSource = 0;
					inserted->awaitingRelay = 0;
				}
				--frameCount;
			}
		}
	}

	g_flightSimSideEffectsSuppressed = 0;
	Flight_StepSimToTime(packetTick);
	for (playerIndex = 0; playerIndex < PLAYER_SLOT_COUNT; ++playerIndex) {
		if (g_players[playerIndex].participationState != 0 &&
		    playerIndex != g_localPlayer) {
			FlightView_UpdatePlayerCamera(playerIndex);
		}
	}
	FlightView_UpdatePlayerCamera(g_localPlayer);
	g_gameTime = packetTick;
	g_serverTickTime = packetTick;
	Sound_FlushQueuedEffects();
	Flight_SaveWorldState();

	if (checksumRequested != 0) {
		int checksumDwordCount = (int)(sizeof(g_worldChecksum) /
					       sizeof(g_worldChecksum[0]));

		Flight_ChecksumWorldState(0, 0);
		g_flightNetWorldChecksumEpoch = (unsigned int)g_serverTickTime;
		if (NetSession_IsLocalHost() != 0) {
			FlightNet_BroadcastWorldChecksum(
				(const int *)g_worldChecksum,
				(const int *)g_worldChecksumRegionLengths,
				checksumDwordCount);
		}
		FlightNet_SendWorldChecksumToHost(
			(const int *)g_worldChecksum,
			(const int *)g_worldChecksumRegionLengths,
			checksumDwordCount);
		g_flightNetBufferWorldMessagesUntilChecksum = 1;
		FlightSync_SnapshotWorldStateForReplay();
		FlightSync_ClearBufferedWorldMessages();
	}
}
#endif

/* Handles a player's world checksum for the epoch in
 * g_flightNetWorldChecksumEpoch (word 1); other epochs, and players that
 * aborted or are inactive, are ignored. When the 16 region checksums from
 * another player differ from the local ones, the original build sends that
 * player the snapshot in g_worldStateDupBuffer
 * (FlightNet_SendWorldStateResyncToPlayer, then
 * FlightNet_SendWorldStateResyncApplyRequest when that succeeds); the modern
 * build starts sending it with XvtResync_BeginSend and returns. On the host it
 * then records the player in g_flightNetWorldChecksumPeerStatus as matched
 * (1) or not (2), and turns buffering off once every active player has
 * matched. In the modern build word 34 is a request code: code 1 is taken in
 * any epoch, and on the host, from another player, starts sending the live
 * world state instead. The original build does not check that the sender has
 * a player slot; the lookup then returns 8, past the 8-entry tables. */
// FUNCTION: XVT 0x4193C0
void FlightSync_HandleWorldChecksumPacket(int senderDpid, const int *packet)
{
	enum {
		PACKET_EPOCH_INDEX = 1,
		PACKET_CHECKSUM_INDEX = 2,
		CHECKSUM_REGION_COUNT = 16,
		PACKET_REGION_LENGTH_INDEX =
			PACKET_CHECKSUM_INDEX + CHECKSUM_REGION_COUNT,
		PEER_STATUS_MISMATCHED = 2,
		PEER_STATUS_MATCHED = 1,
		ALL_PEER_STATUS_BITS = 3
	};

	int allPeerStatus;
	int checksumMismatch;
	int localWorldStateSize;
	int playerIndex;
	int remoteWorldStateSize;
	int senderPlayerIndex;

	if ((unsigned int)packet[PACKET_EPOCH_INDEX] !=
		    g_flightNetWorldChecksumEpoch
#ifdef XVT_MODERN
	    && packet[34] != XVT_CHECKSUM_REQUEST_STATE
#endif
	) {
		return;
	}

	senderPlayerIndex = NetSession_FindPlayerSlotByDpid(senderDpid);
	checksumMismatch = 0;
#ifdef XVT_MODERN
	if ((unsigned)senderPlayerIndex >= 8) {
		return;
	}
	if ((unsigned)packet[34] > 1) {
		return;
	}
	if (packet[34] == 1 && NetSession_IsLocalHost() &&
	    senderPlayerIndex != g_localPlayer) {
		XvtResync_BeginSend(senderDpid, g_worldStateBuffer,
				    g_worldStateSize);
		return;
	}
#endif
	if (g_playerAbortFlags[senderPlayerIndex] != 0 ||
	    g_players[senderPlayerIndex].participationState == 0) {
		return;
	}

	if (g_localPlayer != senderPlayerIndex) {
		const int *remoteChecksums = &packet[PACKET_CHECKSUM_INDEX];
		const int *remoteRegionLengths =
			&packet[PACKET_REGION_LENGTH_INDEX];

		localWorldStateSize = 0;
		remoteWorldStateSize = 0;
		/* playerIndex is reused here as a checksum region index. */
		for (playerIndex = 0; playerIndex < CHECKSUM_REGION_COUNT;
		     ++playerIndex) {
			remoteWorldStateSize +=
				remoteRegionLengths[playerIndex];
			localWorldStateSize +=
				(int)g_worldChecksumRegionLengths[playerIndex];
			if (g_worldChecksum[playerIndex] !=
			    (unsigned int)remoteChecksums[playerIndex]) {
				checksumMismatch = 1;
			}
		}
		(void)localWorldStateSize;
		(void)remoteWorldStateSize;

		if (checksumMismatch != 0) {
#ifdef XVT_MODERN
			XvtResync_BeginSend(senderDpid, g_worldStateDupBuffer,
					    g_worldStateDupSize);
			return;
#else
			if (FlightNet_SendWorldStateResyncToPlayer(
				    senderDpid, g_worldStateDupBuffer,
				    g_worldStateDupSize) != 0) {
				FlightNet_SendWorldStateResyncApplyRequest(
					senderDpid, g_worldStateDupSize);
			}
			checksumMismatch = 1;
#endif
		}
	}

	if (NetSession_IsLocalHost() == 0) {
		return;
	}

	g_flightNetWorldChecksumPeerStatus[senderPlayerIndex] =
		PEER_STATUS_MISMATCHED;
	if (checksumMismatch != 1) {
		g_flightNetWorldChecksumPeerStatus[senderPlayerIndex] =
			PEER_STATUS_MATCHED;
	}

	allPeerStatus = ALL_PEER_STATUS_BITS;
	for (playerIndex = 0;
	     playerIndex < (int)(sizeof(g_players) / sizeof(g_players[0]));
	     ++playerIndex) {
		if (g_players[playerIndex].participationState != 0) {
			allPeerStatus &=
				g_flightNetWorldChecksumPeerStatus[playerIndex];
		}
	}
	if ((allPeerStatus & PEER_STATUS_MATCHED) != 0) {
		g_flightNetBufferWorldMessagesUntilChecksum = 0;
	}
}

/* On a client, compares the 16 world checksum words the server sent for the
 * current epoch with the local ones; when all match, turns buffering off and
 * empties the world-message buffer. Ignored on the host and for any other
 * epoch. */
// FUNCTION: XVT 0x419510
void FlightSync_HandleServerChecksumPacket(uint8_t *packet)
{
	uint32_t *packetChecksum;
	unsigned int *localChecksum;
	int checksumMismatch;

	if (NetSession_IsLocalHost() != 0 ||
	    ((uint32_t *)packet)[1] != g_flightNetWorldChecksumEpoch) {
		return;
	}

	packetChecksum = (uint32_t *)packet + 2;
	checksumMismatch = 0;
	localChecksum = g_worldChecksum;
	do {
		if (*localChecksum != *packetChecksum) {
			checksumMismatch = 1;
		}
		++localChecksum;
		++packetChecksum;
	} while (localChecksum < g_worldChecksum + 16);

	if (checksumMismatch == 0) {
		g_flightNetBufferWorldMessagesUntilChecksum = 0;
		FlightSync_ClearBufferedWorldMessages();
	}
}

/* Copies size bytes of a resent world state to offset in
 * g_worldStateDupBuffer, with no bounds check. Only the original build calls
 * this. */
// FUNCTION: XVT 0x419570
void FlightSync_CopyWorldStateResyncChunk(const void *src, int offset,
					  unsigned int size)
{
	memcpy(&g_worldStateDupBuffer[offset], src, size);
}

#ifndef XVT_MODERN
#pragma intrinsic(memcpy)
#endif

#ifndef XVT_MODERN
/* Takes the world state a resync left in g_worldStateDupBuffer as the saved
 * state (g_worldStateBuffer and g_worldStateSize), sets g_serverTickTime to
 * serverTickTime and sets g_flightNetDirtyAllObjectTransformsAfterRestore.
 * Then it computes the world checksum and sends it to the host, snapshots
 * the state again, turns buffering off and replays the buffered world
 * messages. Only the original build calls this. */
// FUNCTION: XVT 0x4195A0
void FlightSync_ApplyResyncAndReplayWorldMessages(unsigned int worldStateBytes,
						  int serverTickTime)
{
	int checksumDwordCount;

	g_worldStateDupSize = (int)worldStateBytes;
	g_flightNetDirtyAllObjectTransformsAfterRestore = 1;
	memcpy(g_worldStateBuffer, g_worldStateDupBuffer, worldStateBytes);
	g_worldStateSize = (unsigned int)g_worldStateDupSize;
	g_serverTickTime = serverTickTime;
	Flight_ChecksumWorldState(0, 0);
	checksumDwordCount =
		(int)(sizeof(g_worldChecksum) / sizeof(g_worldChecksum[0]));
	FlightNet_SendWorldChecksumToHost(
		(const int *)g_worldChecksum,
		(const int *)g_worldChecksumRegionLengths, checksumDwordCount);
	FlightSync_SnapshotWorldStateForReplay();
	g_flightNetBufferWorldMessagesUntilChecksum = 0;
	FlightSync_ReplayBufferedWorldMessages();
}
#endif

/* Copies the saved world state (g_worldStateSize bytes of
 * g_worldStateBuffer) into g_worldStateDupBuffer and sets
 * g_worldStateDupSize: the copy a resync sends to a player whose checksum
 * differs. */
// FUNCTION: XVT 0x419620
void FlightSync_SnapshotWorldStateForReplay(void)
{
	unsigned int snapshotBytes;

	snapshotBytes = g_worldStateSize;
	memcpy(g_worldStateDupBuffer, g_worldStateBuffer, snapshotBytes);
	g_worldStateDupSize = (int)snapshotBytes;
}

#ifndef XVT_MODERN
/* Appends one world message to g_worldMessageBuffer, growing the buffer by
 * 100 times the message's size when it does not fit (a failed allocation is
 * a fatal error). The size is the 9-byte header plus each player's inputs,
 * walked by their time codes. The walk counts the 4 bytes after code 127 but
 * not the 2 after code 126 or the 1 after code 125, which
 * FlightSync_ApplyWorldMessagePacket reads, so a message using those is
 * stored short. Only the original build calls this. */
// FUNCTION: XVT 0x419650
void FlightSync_BufferWorldMessagePacket(uint8_t *packet)
{
	uint16_t oldHandle;
	int packetSize;
	uint8_t *packetStart;
	int playerBlockCount;

	packetSize = 9;
	packetStart = packet;
	playerBlockCount = packet[8];
	packet += 8;
	++packet;
	if (playerBlockCount > 0) {
		do {
			int frameCount;

			frameCount = *packet++;
			++packetSize;
			if (frameCount > 0) {
				do {
					int timestampCode;

					timestampCode = *packet++;
					++packetSize;
					if ((timestampCode & 0x7F) == 0x7F) {
						packet += 4;
						packetSize += 4;
					}
					if ((timestampCode & 0x80) != 0) {
						++packet;
						++packetSize;
					}
					packet += 2;
					packetSize += 2;
					--frameCount;
				} while (frameCount != 0);
			}
			--playerBlockCount;
		} while (playerBlockCount != 0);
	}

	if (g_worldMessageBufferBytesFree < packetSize) {
		unsigned int oldCapacity;
		int growth;
		uint8_t *oldBuffer;

		oldHandle = g_worldMessageBufferHandle;
		oldCapacity = g_worldMessageBufferCapacity;
		growth = 100 * packetSize;
		g_worldMessageBufferBytesFree += growth;
		g_worldMessageBufferCapacity += growth;
		g_worldMessageBufferHandle =
			Memory_AllocHandle(g_worldMessageBufferCapacity, 0);
		if (g_worldMessageBufferHandle == 0) {
			FeDiskIo_FatalError(FILE_ERROR_STR_NOT_ENOUGH_MEMORY);
		}
		g_worldMessageBuffer =
			Memory_GetHandleBlock(g_worldMessageBufferHandle);
		if (oldHandle != 0) {
			oldBuffer = Memory_GetHandleBlock(oldHandle);
			memcpy(g_worldMessageBuffer, oldBuffer, oldCapacity);
			Memory_HandleBlockDoneStub(oldHandle);
			Memory_FreeHandle(oldHandle);
		}
	}

	memcpy(&g_worldMessageBuffer[g_worldMessageBufferCapacity -
				     g_worldMessageBufferBytesFree],
	       packetStart, packetSize);
	g_worldMessageBufferBytesFree -= packetSize;
	++g_worldMessageBufferedCount;
}
#endif

/* Empties the world-message buffer: the modern build clears the replay queue
 * (XvtFlightMessages_Clear); the original keeps its memory and resets the
 * count and the free space. */
// FUNCTION: XVT 0x4197B0
void FlightSync_ClearBufferedWorldMessages(void)
{
#ifdef XVT_MODERN
	XvtFlightMessages_Clear(XVT_QUEUE_REPLAY);
#else
	g_worldMessageBufferedCount = 0;
	g_worldMessageBufferBytesFree = g_worldMessageBufferCapacity;
#endif
}

#ifndef XVT_MODERN
/* Applies every buffered world message in order through
 * FlightSync_ApplyWorldMessagePacket, after clearing each one's checksum
 * request bit, then empties the buffer. It steps from one message to the
 * next with the same short size count as FlightSync_BufferWorldMessagePacket.
 * Only the original build calls this. */
// FUNCTION: XVT 0x4197D0
void FlightSync_ReplayBufferedWorldMessages(void)
{
	enum {
		PACKET_PLAYER_COUNT_OFFSET = 2 * sizeof(int),
		FULL_TIMESTAMP_CODE = 0x7F,
		KEY_PRESENT_FLAG = 0x80,
		DELTA_CODE_MASK = 0x7F,
		INPUT_AXIS_BYTES = 2
	};

	int packetOffset;

	packetOffset = 0;
	while (g_worldMessageBufferedCount != 0) {
		uint8_t *cursor;
		uint8_t *packet;
		int playerSectionsRemaining;

		--g_worldMessageBufferedCount;
		packet = &g_worldMessageBuffer[packetOffset];
		packetOffset += PACKET_PLAYER_COUNT_OFFSET;
		cursor = packet + PACKET_PLAYER_COUNT_OFFSET;
		++packetOffset;
		playerSectionsRemaining = *cursor++;
		while (playerSectionsRemaining > 0) {
			int frameHeader;
			int framesRemaining;

			framesRemaining = *cursor++;
			++packetOffset;
			while (framesRemaining > 0) {
				frameHeader = *cursor++;
				++packetOffset;
				if ((frameHeader & DELTA_CODE_MASK) ==
				    FULL_TIMESTAMP_CODE) {
					cursor += sizeof(uint32_t);
					packetOffset += sizeof(uint32_t);
				}
				if ((frameHeader & KEY_PRESENT_FLAG) != 0) {
					++cursor;
					++packetOffset;
				}
				cursor += INPUT_AXIS_BYTES;
				packetOffset += INPUT_AXIS_BYTES;
				--framesRemaining;
			}
			--playerSectionsRemaining;
		}

		((uint32_t *)packet)[1] &= INT32_MAX;
		FlightSync_ApplyWorldMessagePacket(packet);
	}
	g_worldMessageBufferBytesFree = g_worldMessageBufferCapacity;
}
#endif

/* Returns what Sound_UnusedFourArgStub returns, 0. Nothing in the engine
 * calls this. */
// FUNCTION: XVT 0x419870
int FlightSync_UnusedFourArgForwarder(int arg1, int arg2, int arg3, int arg4)
{
	return Sound_UnusedFourArgStub(arg1, arg2, arg3, arg4);
}
