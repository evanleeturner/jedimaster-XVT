#include "xvt_runtime/input/flight_controls.h"
#include "xvt_runtime/runtime/flight_checkpoint.h"
#include "xvt_runtime/runtime/flight_internal.h"
#include "xvt_runtime/runtime/flight_messages.h"
#include "xvt_runtime/runtime/flight_network.h"
#include "xvt_runtime/runtime/flight_prediction.h"
#include "xvt_runtime/runtime/resync_task.h"
#include "xvt_runtime/snapshot/render_capture.h"
#include "xvt_runtime/timing/flight_timing.h"

enum {
	MINIMUM_FRAME_ADVANCE_TICKS = 4,
	FRAME_ADJUST_DIVISOR_SHIFT = 3,
	CLOCK_ADJUST_DIVISOR_SHIFT = 4,
	MAX_FINE_FRAME_ADJUSTMENT = 4,
	LAG_LEVEL_1_TICKS = 472,
	LAG_LEVEL_2_TICKS = 944,
	LAG_LEVEL_3_TICKS = 1416,
	HOST_TIMEOUT_TICKS = 7080,
	UPDATE_HISTOGRAM_BUCKETS = 20,
	LONG_UPDATE_TICKS = 8,
	PING_DROP_SCORE_STEP = 10,
	PING_LEVEL_2_SCORE = 10,
	PING_LEVEL_3_SCORE = 20,
};

enum { XVT_FRAME_WAIT, XVT_FRAME_ADVANCE };

static struct {
	int phase, savedInputTimestamp, frameTargetTimestamp;
	int loopStartTimestamp, frameStartTimestamp;
} g_frame;

typedef enum XvtConfirmationPhase {
	XVT_CONFIRM_IDLE,
	XVT_CONFIRM_APPLY,
	XVT_CONFIRM_REPLAY,
	XVT_CONFIRM_REBUILD,
	XVT_CONFIRM_TERMINAL
} XvtConfirmationPhase;

static struct {
	XvtFlightMessage message;
	XvtConfirmationPhase phase;
	int publish_floor, suspended, predicted_suspended;
	uint64_t iteration, deadline;
	unsigned steps;
	int budget_initialized;
} g_confirm;

extern uint32_t g_lastTickTime;

uint64_t XvtFlightTime_DelayForTicks(unsigned int ticks) {
	uint64_t now = XvtTime_GetElapsedUs();
	uint32_t ms = (uint32_t)(now / 1000);
	uint32_t base = g_lastTickTime ? g_lastTickTime : ms;
	uint64_t elapsed = (uint64_t)(uint32_t)(ms - base) * 1000 + now % 1000;
	uint64_t required = (uint64_t)ticks * 4000;
	return elapsed < required ? required - elapsed : 0;
}

void XvtFlightFrame_Begin(void) {
	memset(&g_frame, 0, sizeof(g_frame));
	memset(&g_confirm, 0, sizeof g_confirm);
	if (XvtFlightTiming_IsNetwork125())
		g_predictedFrameDelta = XVT_NETWORK_STEP_TICKS;
	g_flightLastStepTargetTimestamp = 0;
	g_lastLocalReplayInputTimestamp = 0;
	g_flightSfxSideEffectGate = 0;
	g_flightPingPrevHostDropCount = 0;
	g_flightPingDropScore = 0;
	memset(g_flightUpdateDurationHistogram, 0, sizeof(g_flightUpdateDurationHistogram));
}

static int XvtFlightFrame_Target(void) {
	int frameAdjustment, frameTargetTimestamp;
	if (XvtFlightTiming_IsUnlocked())
		return g_inputTimestamp;
	if (g_flightLastStepTargetTimestamp == 0) {
		frameTargetTimestamp = g_inputTimestamp;
	} else {
		frameTargetTimestamp = g_flightLastStepTargetTimestamp + g_predictedFrameDelta;
		if ((unsigned int)frameTargetTimestamp >= (unsigned int)g_inputTimestamp) {
			if ((unsigned int)frameTargetTimestamp > (unsigned int)g_inputTimestamp) {
				frameAdjustment = frameTargetTimestamp - g_inputTimestamp;
				if (frameAdjustment > g_predictedFrameDelta >> FRAME_ADJUST_DIVISOR_SHIFT) {
					frameAdjustment = g_predictedFrameDelta >> FRAME_ADJUST_DIVISOR_SHIFT;
				}
				if (frameAdjustment == 0) {
					frameAdjustment = 1;
				}
				if (frameAdjustment > MAX_FINE_FRAME_ADJUSTMENT) {
					frameAdjustment = frameTargetTimestamp - g_inputTimestamp;
				}
				frameTargetTimestamp -= frameAdjustment;
			}
		} else {
			frameAdjustment = g_inputTimestamp - frameTargetTimestamp;
			if (frameAdjustment > g_predictedFrameDelta >> FRAME_ADJUST_DIVISOR_SHIFT) {
				frameAdjustment = g_predictedFrameDelta >> FRAME_ADJUST_DIVISOR_SHIFT;
			}
			if (frameAdjustment == 0) {
				frameAdjustment = 1;
			}
			if (frameAdjustment > MAX_FINE_FRAME_ADJUSTMENT) {
				frameAdjustment = g_inputTimestamp - frameTargetTimestamp;
			}
			frameTargetTimestamp += frameAdjustment;
		}
	}
	if (frameTargetTimestamp - g_gameTime < MINIMUM_FRAME_ADVANCE_TICKS) {
		frameTargetTimestamp = g_gameTime + MINIMUM_FRAME_ADVANCE_TICKS;
	}
	return frameTargetTimestamp;
}

static void XvtFlightFrame_InvalidateRemoteTransforms(void) {
	if (!XvtFlightTiming_IsNetwork125() || !g_remotePlayerRenderSmoothingEnabled)
		return;
	/* Changing to/from a smoothed pose also invalidates its derived transforms.
	 * Network125 predicts again before the next authoritative restore. */
	for (unsigned player = 0; player < XVT_FLIGHT_PLAYERS; ++player) {
		int slot = g_players[player].objectIndex;
		if (player == (unsigned)g_localPlayer || !g_players[player].connectedFlag || slot < 0 ||
			slot >= g_regionMainObjectSlotEnd)
			continue;
		ObjectRecord* object = &g_objectTable[slot];
		if (!object->objectType || !object->mobj)
			continue;
		object->mobj->moveVectorDirty = 1;
		object->mobj->orientMatrixDirty = 1;
	}
}

static void XvtFlightFrame_Render(void) {
	int updateTicks, renderTicks, loopTicks, renderStartTimestamp;
	char overlayLine[180];
	g_inputTimestamp += Time_GetFrameDelta();
	updateTicks = g_inputTimestamp - g_frame.frameStartTimestamp;
	g_inputTimestamp += Time_GetFrameDelta();
	{
		int lagTicks;

		lagTicks = g_inputTimestamp - g_flightNetClockLeadAllowanceMs - g_serverTickTime;
		if (lagTicks < LAG_LEVEL_1_TICKS) {
			g_lagIndicator = 0;
		} else if (lagTicks < LAG_LEVEL_2_TICKS) {
			g_lagIndicator = 1;
		} else if (lagTicks < LAG_LEVEL_3_TICKS) {
			g_lagIndicator = 2;
		} else {
			g_lagIndicator = 3;
		}
	}

	if (g_flightPingPrevHostDropCount == 0) {
		g_pingIndicator = 0;
	} else {
		int hostDplayId;
		int hostDropCount;

		hostDplayId = NetSession_GetHostDplayId();
		hostDropCount = NetReliable_GetPeerPacketDropCountByDpid_0(hostDplayId);
		g_flightPingDropScore += PING_DROP_SCORE_STEP * (hostDropCount - g_flightPingPrevHostDropCount);
		if (g_flightPingDropScore == 0) {
			g_pingIndicator = 0;
		} else if (g_flightPingDropScore < PING_LEVEL_2_SCORE) {
			g_pingIndicator = 1;
		} else if (g_flightPingDropScore < PING_LEVEL_3_SCORE) {
			g_pingIndicator = 2;
		} else {
			g_pingIndicator = 3;
		}
		g_flightPingPrevHostDropCount = hostDropCount;
		if (g_flightPingDropScore != 0) {
			--g_flightPingDropScore;
		}
	}

	renderStartTimestamp = g_inputTimestamp;
	XvtRenderCapture_CompleteNetworkWorld();
	FlightSync_ApplyRemotePlayerRenderSmoothing();
	XvtFlightFrame_InvalidateRemoteTransforms();
	if (XvtFlightTiming_IsNetwork125()) {
		g_flightSfxSideEffectGate = 1;
	}
	FlightView_RenderFrame();
	g_flightSfxSideEffectGate = 0;
	Sound_FlushQueuedEffects();
	FlightSync_CaptureRemotePlayerRenderSamples();
	XvtFlightFrame_InvalidateRemoteTransforms();
	g_inputTimestamp += Time_GetFrameDelta();
	renderTicks = g_inputTimestamp - renderStartTimestamp;
	loopTicks = g_inputTimestamp - g_frame.loopStartTimestamp;
	if (loopTicks == 0) {
		loopTicks = 1;
	}

	if (g_flightConfTickCounter == 0) {
		g_flightTickOverlaySampleCount = 0;
		g_flightTickOverlayWindowTicks = 0;
	} else {
		unsigned int histogramTotal;
		int histogramIndex;
		int aiProjectileCount;
		int playerProjectileCount;
		int objectIndex;

		if (g_flightTickOverlayWindowTicks > LAG_LEVEL_2_TICKS) {
			g_flightTickOverlaySampleCount = 0;
			g_flightTickOverlayWindowTicks = 0;
		}
		g_flightTickOverlayLastLoopTicks = loopTicks;
		g_flightTickOverlayWindowTicks += loopTicks;
		++g_flightTickOverlaySampleCount;
		sprintf(overlayLine,
				"R:%-2d U:%-2d N:%-2d O:%-2d T:%-2d FR:%-2d NOW:%-7dL:%-7dS:%-7dW:%-3dD:%-3dA%d\n",
				renderTicks, updateTicks, 0, loopTicks - updateTicks - renderTicks, loopTicks,
				SIMULATION_TICKS_PER_SECOND / loopTicks, g_inputTimestamp, g_gameTime, g_serverTickTime,
				g_inputTimestamp - g_serverTickTime, g_flightNetClockLeadAllowanceMs,
				g_flightNetClockAdjustAccumTicks);
		if (updateTicks < 0) {
			updateTicks = 0;
		}
		if (updateTicks > UPDATE_HISTOGRAM_BUCKETS - 1) {
			++g_flightUpdateDurationHistogram[UPDATE_HISTOGRAM_BUCKETS - 1];
		} else {
			++g_flightUpdateDurationHistogram[updateTicks];
		}
		sprintf(g_missionDebugBuffer,
				"Raw  0:%2d  1:%2d  2:%2d  3:%2d  4:%2d  5:%2d  6:%2d  7:%2d  8:%2d   9:%2d\n",
				g_flightUpdateDurationHistogram[0], g_flightUpdateDurationHistogram[1],
				g_flightUpdateDurationHistogram[2], g_flightUpdateDurationHistogram[3],
				g_flightUpdateDurationHistogram[4], g_flightUpdateDurationHistogram[5],
				g_flightUpdateDurationHistogram[6], g_flightUpdateDurationHistogram[7],
				g_flightUpdateDurationHistogram[8], g_flightUpdateDurationHistogram[9]);
		sprintf(g_missionDebugBuffer,
				"Raw 10:%2d 11:%2d 12:%2d 13:%2d 14:%2d 15:%2d 16:%2d 17:%2d 18:%2d >18:%2d\n",
				g_flightUpdateDurationHistogram[10], g_flightUpdateDurationHistogram[11],
				g_flightUpdateDurationHistogram[12], g_flightUpdateDurationHistogram[13],
				g_flightUpdateDurationHistogram[14], g_flightUpdateDurationHistogram[15],
				g_flightUpdateDurationHistogram[16], g_flightUpdateDurationHistogram[17],
				g_flightUpdateDurationHistogram[18], g_flightUpdateDurationHistogram[19]);

		histogramTotal = 0;
		for (histogramIndex = 0; histogramIndex < UPDATE_HISTOGRAM_BUCKETS; ++histogramIndex) {
			histogramTotal += g_flightUpdateDurationHistogram[histogramIndex];
		}
		if (histogramTotal != 0) {
			sprintf(g_missionDebugBuffer,
					"Pct  0:%2d  1:%2d  2:%2d  3:%2d  4:%2d  5:%2d  6:%2d  7:%2d  8:%2d   9:%2d\n",
					100 * g_flightUpdateDurationHistogram[0] / histogramTotal,
					100 * g_flightUpdateDurationHistogram[1] / histogramTotal,
					100 * g_flightUpdateDurationHistogram[2] / histogramTotal,
					100 * g_flightUpdateDurationHistogram[3] / histogramTotal,
					100 * g_flightUpdateDurationHistogram[4] / histogramTotal,
					100 * g_flightUpdateDurationHistogram[5] / histogramTotal,
					100 * g_flightUpdateDurationHistogram[6] / histogramTotal,
					100 * g_flightUpdateDurationHistogram[7] / histogramTotal,
					100 * g_flightUpdateDurationHistogram[8] / histogramTotal,
					100 * g_flightUpdateDurationHistogram[9] / histogramTotal);
			sprintf(g_missionDebugBuffer,
					"Pct 10:%2d 11:%2d 12:%2d 13:%2d 14:%2d 15:%2d 16:%2d 17:%2d 18:%2d >18:%2d\n",
					100 * g_flightUpdateDurationHistogram[10] / histogramTotal,
					100 * g_flightUpdateDurationHistogram[11] / histogramTotal,
					100 * g_flightUpdateDurationHistogram[12] / histogramTotal,
					100 * g_flightUpdateDurationHistogram[13] / histogramTotal,
					100 * g_flightUpdateDurationHistogram[14] / histogramTotal,
					100 * g_flightUpdateDurationHistogram[15] / histogramTotal,
					100 * g_flightUpdateDurationHistogram[16] / histogramTotal,
					100 * g_flightUpdateDurationHistogram[17] / histogramTotal,
					100 * g_flightUpdateDurationHistogram[18] / histogramTotal,
					100 * g_flightUpdateDurationHistogram[19] / histogramTotal);
		}

		aiProjectileCount = 0;
		playerProjectileCount = 0;
		for (objectIndex = g_projectileObjectSlotStart; objectIndex < g_projectileObjectSlotEnd;
			 ++objectIndex) {
			if (g_objectTable[objectIndex].objectType != 0) {
				if (g_objectTable[objectIndex].genusId == 6) {
					++playerProjectileCount;
				} else {
					++aiProjectileCount;
				}
			}
		}
		if (updateTicks >= LONG_UPDATE_TICKS) {
			sprintf(g_missionDebugBuffer,
					"****** Long Update: %d ***** Warp: %d  *****  Player:  %d *****  AI:  %d\n", updateTicks,
					0, playerProjectileCount, aiProjectileCount);
		}
	}
}

static void XvtFlightFrame_AdjustClock(void) {
	int clockAdjustment;
	int cap = g_predictedFrameDelta >> FRAME_ADJUST_DIVISOR_SHIFT;
	if (cap < 1)
		cap = 1;
	char fellBehindLogLine[180];
	g_inputTimestamp += Time_GetFrameDelta();
	if ((unsigned int)(g_serverTickTime + g_flightNetClockLeadAllowanceMs) >=
		(unsigned int)g_inputTimestamp) {
		if ((unsigned int)(g_serverTickTime + g_flightNetClockLeadAllowanceMs) >
			(unsigned int)g_inputTimestamp) {
			clockAdjustment = (g_serverTickTime + g_flightNetClockLeadAllowanceMs - g_inputTimestamp) >>
							  CLOCK_ADJUST_DIVISOR_SHIFT;
			if (clockAdjustment == 0) {
				clockAdjustment = 1;
			}
			if (clockAdjustment > cap) {
				clockAdjustment = cap;
			}
			g_flightNetClockAdjustAccumTicks -= clockAdjustment;
			g_inputTimestamp += clockAdjustment;
		}
	} else {
		clockAdjustment = (g_inputTimestamp - g_flightNetClockLeadAllowanceMs - g_serverTickTime) >>
						  CLOCK_ADJUST_DIVISOR_SHIFT;
		if (clockAdjustment == 0) {
			clockAdjustment = 1;
		}
		if (clockAdjustment > cap) {
			clockAdjustment = cap;
		}
		g_flightNetClockAdjustAccumTicks += clockAdjustment;
		g_inputTimestamp -= clockAdjustment;
	}

	if (g_serverTickTime > g_inputTimestamp) {
		sprintf(fellBehindLogLine, "Fell Behind! tickcounter:%-7d serverticks:%-7d adjustment:%-4d\n",
				g_inputTimestamp, g_serverTickTime,
				g_serverTickTime + g_flightNetClockLeadAllowanceMs - g_inputTimestamp);
		clockAdjustment = g_serverTickTime + g_flightNetClockLeadAllowanceMs - g_inputTimestamp;
		g_inputTimestamp += clockAdjustment;
		g_flightNetClockAdjustAccumTicks -= clockAdjustment;
	}
}

static void XvtFlightFrame_StartAdvance(void) {
	g_frame.frameTargetTimestamp = XvtFlightFrame_Target();
	g_predictedFrameDelta = g_frame.frameTargetTimestamp - g_flightLastStepTargetTimestamp;
	g_frame.savedInputTimestamp = g_inputTimestamp;
	g_inputTimestamp = g_frame.frameTargetTimestamp;
	FlightNet_SampleAndSendInput();
	g_flightSimSideEffectsSuppressed = 0;
	dtMs = g_inputTimestamp - g_gameTime;
	g_frame.phase = XVT_FRAME_ADVANCE;
}

static int XvtFlightFrame_Advance(void) {
	if (!XvtFlightSim_StepToTime(g_frame.frameTargetTimestamp))
		return 0;
	g_gameTime = g_inputTimestamp;
	g_serverTickTime = g_inputTimestamp;
	g_flightLastStepTargetTimestamp = g_inputTimestamp;
	g_inputTimestamp += g_frame.savedInputTimestamp - g_frame.frameTargetTimestamp;
	Sound_FlushQueuedEffects();
	XvtFlightFrame_Render();
	g_frame.phase = XVT_FRAME_WAIT;
	return 0;
}

static void XvtFlightFrame_NetworkBudget(void) {
	uint64_t now = XvtTime_GetElapsedUs();
	if (!g_confirm.budget_initialized || g_confirm.iteration != now) {
		g_confirm.iteration = now;
		g_confirm.steps = 0;
		g_confirm.deadline = 0;
		g_confirm.budget_initialized = 1;
		XvtFlightNetwork_BeginIteration();
	}
}

static int XvtFlightFrame_HasBudget(void) {
	if (!g_confirm.deadline)
		g_confirm.deadline = Aeron_NowUs() + XVT_SIM_BUDGET_US;
	return g_confirm.steps < XVT_SIM_STEPS_PER_ITERATION && Aeron_NowUs() < g_confirm.deadline;
}

static void XvtFlightFrame_Checksum(void) {
	Flight_ChecksumWorldState(0, 0);
	g_flightNetWorldChecksumEpoch = (unsigned)g_serverTickTime;
	if (NetSession_GetLocalPlayerId())
		FlightNet_BroadcastWorldChecksum((const int*)g_worldChecksum, (const int*)g_peerChecksumRegionLengths,
										 16);
	FlightNet_SendWorldChecksumToLocalPlayer((const int*)g_worldChecksum,
											 (const int*)g_peerChecksumRegionLengths, 16);
	g_flightNetBufferWorldMessagesUntilChecksum = 1;
	FlightSync_SnapshotWorldStateForReplay();
	FlightSync_ResetWorldMessageBufferCursor();
}

static XvtFlightReplayResult XvtFlightFrame_Confirm(XvtFlightQueue queue) {
	if (g_confirm.phase != XVT_CONFIRM_APPLY && g_confirm.phase != XVT_CONFIRM_REPLAY) {
		if (!XvtFlightMessages_Peek(queue, &g_confirm.message, sizeof g_confirm.message))
			return XVT_REPLAY_IDLE;
		XvtFlightMessages_Pop(queue);
		int target = (int)(g_confirm.message.target_flags & INT32_MAX);
		if (target <= g_serverTickTime)
			return XVT_REPLAY_ADVANCED;
		if (target - g_serverTickTime != XVT_WORLD_MESSAGE_TICKS) {
			XvtFlightNetwork_RequestRecovery();
			return XVT_REPLAY_PENDING;
		}
		if (queue == XVT_QUEUE_PENDING && g_flightNetBufferWorldMessagesUntilChecksum &&
			!XvtFlightMessages_Enqueue(&g_confirm.message, XVT_QUEUE_REPLAY)) {
			XvtFlightNetwork_RequestRecovery();
			return XVT_REPLAY_PENDING;
		}
		if (queue == XVT_QUEUE_PENDING)
			g_confirm.publish_floor = XvtRenderCapture_LastViewTick();
		Flight_RestoreWorldState();
		if (g_flightMissionState.missionEndPending) {
			g_confirm.phase = XVT_CONFIRM_TERMINAL;
			return XVT_REPLAY_TERMINAL;
		}
		g_gameTime = g_serverTickTime;
		XvtFlightTiming_RestoreNetworkTick(g_gameTime);
		XvtFlightHistory_RestoreCheckpoint();
		XvtFlightCheckpoint_SetMask(g_confirm.message.mask);
		if (!XvtFlightNetwork_InsertWorld(&g_confirm.message))
			return XVT_REPLAY_PENDING;
		g_confirm.phase = queue == XVT_QUEUE_REPLAY ? XVT_CONFIRM_REPLAY : XVT_CONFIRM_APPLY;
	}
	int target = (int)(g_confirm.message.target_flags & INT32_MAX);
	g_flightSimSideEffectsSuppressed = 0;
	while (g_gameTime < target && XvtFlightFrame_HasBudget()) {
		XvtFlightStepResult result = XvtFlightSim_StepToTime(g_gameTime + XVT_NETWORK_STEP_TICKS);
		if (result == XVT_STEP_PENDING) {
			g_confirm.suspended = 1;
			return XVT_REPLAY_PENDING;
		}
		g_confirm.suspended = 0;
		++g_confirm.steps;
		if (result == XVT_STEP_TERMINAL || g_flightMissionState.missionEndPending) {
			g_confirm.phase = XVT_CONFIRM_TERMINAL;
			XvtFlightMessages_Clear(XVT_QUEUE_PENDING);
			XvtFlightMessages_Clear(XVT_QUEUE_REPLAY);
			return XVT_REPLAY_TERMINAL;
		}
		XvtRenderCapture_CheckNetworkCorrection();
	}
	if (g_gameTime < target)
		return XVT_REPLAY_PENDING;
	for (unsigned i = 0; i < XVT_FLIGHT_PLAYERS; ++i)
		if (g_players[i].connectedFlag && i != (unsigned)g_localPlayer)
			FlightView_UpdatePlayerCamera(i);
	FlightView_UpdatePlayerCamera(g_localPlayer);
	g_serverTickTime = g_gameTime;
	Sound_FlushQueuedEffects();
	Flight_SaveWorldState();
	if (queue == XVT_QUEUE_PENDING && (g_confirm.message.target_flags & XVT_WORLD_CHECKSUM_FLAG))
		XvtFlightFrame_Checksum();
	g_confirm.phase = queue == XVT_QUEUE_REPLAY ? XVT_CONFIRM_IDLE : XVT_CONFIRM_REBUILD;
	return XVT_REPLAY_ADVANCED;
}

XvtFlightReplayResult XvtFlightFrame_ReplayBuffered(void) {
	XvtFlightFrame_NetworkBudget();
	XvtFlightReplayResult result = XVT_REPLAY_ADVANCED;
	while (result == XVT_REPLAY_ADVANCED && XvtFlightFrame_HasBudget())
		result = XvtFlightFrame_Confirm(XVT_QUEUE_REPLAY);
	return result;
}

void XvtFlightFrame_ResetReplay(void) {
	memset(&g_confirm, 0, sizeof g_confirm);
	XvtFlightSim_Reset();
}

static int XvtFlightFrame_NetworkTick(void) {
	if (g_confirm.phase == XVT_CONFIRM_TERMINAL)
		return 1;
	XvtFlightFrame_NetworkBudget();
	if (!g_confirm.suspended && !g_confirm.predicted_suspended) {
		int elapsed = Time_GetFrameDelta();
		if ((int64_t)g_inputTimestamp + elapsed >= INT32_MAX - 258) {
			FlightNet_BroadcastLocalPlayerLeft();
			return 1;
		}
		g_inputTimestamp += elapsed;
		if (!NetSession_GetLocalPlayerId())
			g_flightNetHostTimeoutElapsedMs += elapsed;
		if (g_flightNetHostTimeoutElapsedMs > HOST_TIMEOUT_TICKS) {
			FlightNet_BroadcastPlayerAbort(g_localPlayer);
			return 1;
		}
		XvtFlightNetwork_FlushInput(g_inputTimestamp);
		XvtFlightNetwork_FlushWorld();
		FlightNet_ProcessIncomingPackets();
		if (!g_players[g_localPlayer].connectedFlag || g_flightNetHostAbortReceived)
			return 1;
		if (XvtResync_IsActive())
			return 0;
	}
	if (g_confirm.predicted_suspended) {
		XvtFlightStepResult step = XvtFlightSim_StepToTime(g_gameTime + XVT_NETWORK_STEP_TICKS);
		if (step == XVT_STEP_PENDING)
			return 0;
		if (step == XVT_STEP_TERMINAL)
			return 1;
		g_confirm.predicted_suspended = 0;
		++g_confirm.steps;
	}
	if (XvtFlightNetwork_NeedsRecovery()) {
		XvtResync_RequestState();
		return g_flightMissionState.missionEndPending != 0;
	}
	XvtFlightReplayResult result = XVT_REPLAY_ADVANCED;
	while (result == XVT_REPLAY_ADVANCED && XvtFlightFrame_HasBudget())
		result = XvtFlightFrame_Confirm(XVT_QUEUE_PENDING);
	if (result == XVT_REPLAY_TERMINAL)
		return 1;
	if (g_confirm.phase == XVT_CONFIRM_APPLY || result != XVT_REPLAY_IDLE)
		return 0;
	if (Flight_RecountPlayersAndCheckMissionEnd() && g_gameTime == g_serverTickTime) {
		if (g_radioMessageBackupEnabled) {
			msg_writeMessageLogFile();
			g_radioMessageBackupEnabled = 0;
		}
		return 1;
	}
	XvtFlightFrame_AdjustClock();
	if (g_inputTimestamp >= INT32_MAX - 258) {
		FlightNet_BroadcastLocalPlayerLeft();
		return 1;
	}
	int target = g_inputTimestamp & ~1;
	if (g_confirm.phase == XVT_CONFIRM_REBUILD && target < g_confirm.publish_floor)
		target = g_confirm.publish_floor;
	if (target > g_serverTickTime + XVT_PREDICTION_LEAD_TICKS)
		target = g_serverTickTime + XVT_PREDICTION_LEAD_TICKS;
	g_predictedFrameDelta = XVT_NETWORK_STEP_TICKS;
	while (g_gameTime < target && XvtFlightFrame_HasBudget()) {
		if (!XvtFlightNetwork_AdmitInput(g_gameTime + XVT_NETWORK_STEP_TICKS))
			break;
		if (!XvtFlightPrediction_Queue(g_gameTime + XVT_NETWORK_STEP_TICKS))
			break;
		g_flightSimSideEffectsSuppressed = 1;
		XvtFlightStepResult step = XvtFlightSim_StepToTime(g_gameTime + XVT_NETWORK_STEP_TICKS);
		if (step == XVT_STEP_TERMINAL)
			return 1;
		if (step == XVT_STEP_PENDING) {
			g_confirm.predicted_suspended = 1;
			return 0;
		}
		++g_confirm.steps;
		XvtRenderCapture_CheckNetworkCorrection();
	}
	g_flightLastStepTargetTimestamp = g_gameTime;
	XvtFlightNetwork_FlushInput(g_inputTimestamp);
	if (g_confirm.phase == XVT_CONFIRM_REBUILD && g_gameTime < g_confirm.publish_floor)
		return 0;
	g_confirm.phase = XVT_CONFIRM_IDLE;
	g_frame.frameStartTimestamp = g_frame.loopStartTimestamp = g_inputTimestamp;
	XvtFlightFrame_Render();
	return 0;
}

int XvtFlightFrame_Tick(void) {
	if (XvtFlightTiming_IsNetwork125())
		return XvtFlightFrame_NetworkTick();
	if (XvtFlightSim_IsPaused() && !XvtFlightSim_Resume())
		return 0;
	if (g_frame.phase == XVT_FRAME_ADVANCE)
		return XvtFlightFrame_Advance();
	g_inputTimestamp += Time_GetFrameDelta();
	g_frame.loopStartTimestamp = g_inputTimestamp;
	if (g_inputTimestamp - g_gameTime < (int)XvtFlightTiming_StepTicks())
		return 0;
	if (Flight_RecountPlayersAndCheckMissionEnd()) {
		if (g_radioMessageBackupEnabled) {
			msg_writeMessageLogFile();
			g_radioMessageBackupEnabled = 0;
		}
		return 1;
	}
	g_frame.frameStartTimestamp = g_inputTimestamp;
	g_inputTimestamp += Time_GetFrameDelta();
	XvtFlightFrame_StartAdvance();
	return XvtFlightFrame_Advance();
}

uint64_t XvtFlightFrame_NextWakeDelayUs(void) {
	if (XvtFlightTiming_IsNetwork125()) {
		int target = g_inputTimestamp & ~1;
		if (target > g_serverTickTime + XVT_PREDICTION_LEAD_TICKS)
			target = g_serverTickTime + XVT_PREDICTION_LEAD_TICKS;
		if (g_confirm.phase == XVT_CONFIRM_APPLY || g_confirm.phase == XVT_CONFIRM_REPLAY ||
			(g_confirm.phase == XVT_CONFIRM_REBUILD && g_gameTime < g_confirm.publish_floor) ||
			g_confirm.predicted_suspended || (g_gameTime < target && !XvtFlightNetwork_NeedsRecovery()))
			return 0;
		uint64_t wake = XvtFlightNetwork_NextWakeDelayUs(g_inputTimestamp);
		if (!XvtFlightNetwork_NeedsRecovery() && g_gameTime < g_serverTickTime + XVT_PREDICTION_LEAD_TICKS) {
			int remaining = g_gameTime + XVT_NETWORK_STEP_TICKS - g_inputTimestamp;
			uint64_t simulation = remaining > 0 ? XvtFlightTime_DelayForTicks((unsigned)remaining) : 0;
			if (simulation < wake)
				wake = simulation;
		}
		return wake;
	}
	if (XvtFlightSim_IsPaused())
		return UINT64_MAX;
	int remaining = (int)XvtFlightTiming_StepTicks() - (g_inputTimestamp - g_gameTime);
	return remaining > 0 ? XvtFlightTime_DelayForTicks((unsigned int)remaining) : 0;
}
