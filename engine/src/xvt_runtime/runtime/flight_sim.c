#include "xvt_runtime/input/flight_controls.h"
#include "xvt_runtime/runtime/flight_checkpoint.h"
#include "xvt_runtime/runtime/flight_internal.h"
#include "xvt_runtime/runtime/flight_prediction.h"
#include "xvt_runtime/runtime/flight_protocol.h"
#include "xvt_runtime/runtime/port.h"
#include "xvt_runtime/snapshot/render_capture.h"
#include "xvt_runtime/timing/flight_timing.h"
#include "xvt_runtime/timing/reference_motion.h"

static struct {
	int paused, entityPending, replayPending, stepPending, zeroStep;
	int target, advanceTarget, stepGameTime, player, frameIndex, frameIteration, frameCount;
	int savedElapsed, savedScale, savedGameTime;
} g_sim;

void XvtFlightSim_Reset(void) {
	memset(&g_sim, 0, sizeof(g_sim));
	XvtFlightPrediction_Reset();
}

int XvtFlightSim_IsPaused(void) { return g_sim.paused; }

int XvtFlightSim_Resume(void) {
	if (!g_sim.paused)
		return 1;
	if (FlightInput_Read(-2) != FLIGHT_KEY_ALT_P)
		return 0;
	XvtFlightControls_Recover();
	Time_GetFrameDelta();
	msg_emitInFlightMessage(IFMSG_002_MISSION_RESUMED, g_sim.player);
	g_actionKey = 0;
	g_flightDisplayRebuildPending = 0;
	Flight_ResetUnusedResumeSlots();
	g_sim.paused = 0;
	return 1;
}

int XvtFlightSim_UpdateEntity(int playerIdx) {
	enum {
		PALETTED_BYTES_PER_PIXEL = 1,
		BRIGHTNESS_STEP_Q8 = 0x40,
		BRIGHTNESS_MIN_Q8 = 0x100,
		BRIGHTNESS_LIMIT_Q8 = 0x300,
		GRAPHICS_DETAIL_PRESET_COUNT = 4,
		GRAPHICS_DETAIL_MESSAGE_BASE = 102,
		FIRE_MODIFIER_MASK = 0xD,
		FIRE_MODIFIER = 1,
		TARGET_MODIFIER_MASK = 0xE,
		TARGET_MODIFIER = 2,
		TARGET_TAP_MAX_TICKS = 59,
		FLIGHT_INPUT_WAIT_FOR_ANY_KEY = -2,
	};

	int objectIndex;
	uint16_t savedKeyMods;
	uint16_t* keyModsHoldTimer;
	int16_t newTargetObjectIndex;

	if (!g_sim.entityPending) {
		if (g_flightMissionState.missionEndPending == 1)
			return 1;

		if (g_flightSimSideEffectsSuppressed == 0) {
			if (g_players[playerIdx].viewState.cameraFocusObjIdx != UINT16_MAX &&
				g_objectTable[g_players[playerIdx].viewState.cameraFocusObjIdx].objectType == 0) {
				if (g_players[playerIdx].mapCameraState != 0) {
					g_players[playerIdx].viewState.cameraFocusObjIdx = UINT16_MAX;
				} else {
					g_players[playerIdx].viewState.transitionTimer = 0;
					g_players[playerIdx].viewState.externalCameraActive = 0;
					g_players[playerIdx].viewState.playerInputBlocked = 0;
					g_players[playerIdx].viewState.cameraFocusObjIdx =
						(uint16_t)g_players[playerIdx].objectIndex;
					Hud_SetHudViewState(HUD_VIEW_FORWARD, playerIdx);
					g_players[playerIdx].viewState.hudAimX = 0;
					g_players[playerIdx].viewState.hudAimY = 0;
				}
			}
			if (g_players[playerIdx].mapCameraState != 0 &&
				g_players[playerIdx].viewState.aimTargetIdx != UINT16_MAX &&
				g_objectTable[g_players[playerIdx].viewState.aimTargetIdx].objectType == 0)
				g_players[playerIdx].viewState.aimTargetIdx = UINT16_MAX;
		}

		FlightInput_Read(playerIdx);
		FlightInput_ScaleAxesForFlight();
		if (playerIdx == g_localPlayer) {
			if (g_flightSimSideEffectsSuppressed == 0) {
				switch (g_currentActionKey) {
					case FLIGHT_KEY_SHIFT_L:
						if (g_radioMessageBackupEnabled != 0) {
							g_radioMessageBackupEnabled = 0;
							msg_writeMessageLogFile();
							msg_emitInFlightMessage(IFMSG_402_RADIO_MESSAGE_BACKUP_TURNED_OFF, playerIdx);
						} else {
							g_radioMessageBackupEnabled = 1;
							msg_emitInFlightMessage(IFMSG_403_RADIO_MESSAGE_BACKUP_TURNED_ON, playerIdx);
						}
						break;
					case FLIGHT_KEY_ALT_B:
						if (g_flight16bppBytesPerPixel == PALETTED_BYTES_PER_PIXEL) {
							g_flightBrightnessScaleQ8 += BRIGHTNESS_STEP_Q8;
							if (g_flightBrightnessScaleQ8 == BRIGHTNESS_LIMIT_Q8)
								g_flightBrightnessScaleQ8 = BRIGHTNESS_MIN_Q8;
							g_flightResetPaletteFn();
							g_msgArgTable[0] =
								(uint16_t)(((unsigned int)(g_flightBrightnessScaleQ8 - BRIGHTNESS_MIN_Q8) >>
											6) +
										   1);
							msg_emitInFlightMessage(IFMSG_287_BRIGHTNESS_SET_TO_LEVEL_ARG, playerIdx);
							fsfx_PlaySound(FLIGHT_SOUND_SMALL_CLICK, -1, playerIdx);
						}
						break;
					case FLIGHT_KEY_ALT_D:
						++g_flightGraphicsDetailPreset;
						if (g_flightGraphicsDetailPreset >= GRAPHICS_DETAIL_PRESET_COUNT)
							g_flightGraphicsDetailPreset = 0;
						Flight_ApplyGraphicsDetailPreset(g_flightGraphicsDetailPreset);
						msg_emitInFlightMessage(
							(InFlightMessageId)(g_flightGraphicsDetailPreset + GRAPHICS_DETAIL_MESSAGE_BASE),
							playerIdx);
						fsfx_PlaySound(FLIGHT_SOUND_SMALL_CLICK, -1, playerIdx);
						break;
					case FLIGHT_KEY_ALT_I:
						g_sw3dSkipOddScanlines = g_sw3dSkipOddScanlines == 0;
						break;
					case FLIGHT_KEY_ALT_M:
						if (g_flightAltLToggle != 0)
							g_flightAltLToggle = 0;
						else
							g_flightAltLToggle = 1;
						break;
					case FLIGHT_KEY_ALT_P:
						if (g_flightPlayerCount == 1 && !XvtPort_NetworkRequiresProgress()) {
							XvtFlightControls_Recover();
							g_inputTimestamp += Time_GetFrameDelta();
							fsfx_PlaySound(FLIGHT_SOUND_SMALL_CLICK, -1, playerIdx);
							/* Publish the pause text over the retained HD flight view. */
							XvtRenderCapture_BeginOverlay();
							msg_emitInFlightMessage(IFMSG_001_MISSION_PAUSED_PRESS_ANY_KEY_TO_CONTINUE,
													playerIdx);
							g_flightLockBackBufferForHudDraw = 0;
							FlightSurface_Lock();
							Hud_BlitSoftwareHudTextPanes();
							FlightSurface_Unlock();
							FlightDisplay_Flip();
							XvtRenderCapture_EndOverlay();
							g_flightLockBackBufferForHudDraw = 1;
							Sound_StopAllInstances();
							g_replayInputs[playerIdx].flags = 0;
							g_replayInputs[playerIdx].throttle = 0;
							g_sim.paused = 1;
							g_sim.entityPending = 1;
							g_sim.player = playerIdx;
							return 0;
						}
						break;
					case FLIGHT_KEY_ALT_S:
						if (g_systemMessageDisplayEnabled != 0) {
							g_systemMessageDisplayEnabled = 0;
							msg_emitInFlightMessage(IFMSG_400_SYSTEM_MESSAGE_DISPLAYING_TURNED_OFF,
													playerIdx);
						} else {
							g_systemMessageDisplayEnabled = 1;
							msg_emitInFlightMessage(IFMSG_401_SYSTEM_MESSAGE_DISPLAYING_TURNED_ON, playerIdx);
						}
						break;
					case FLIGHT_KEY_ALT_V:
						fsfx_PlaySound(FLIGHT_SOUND_SMALL_CLICK, -1, playerIdx);
						msg_emitInFlightMessage(IFMSG_000_X_WING_VS_TIE_FIGHTER_VER_1_10_05_11_97, playerIdx);
						break;
					case FLIGHT_KEY_SCREENSHOT:
						fsfx_PlaySound(FLIGHT_SOUND_SMALL_CLICK, -1, playerIdx);
						FlightScreenshot_Capture();
						break;
					default:
						break;
				}
			}
		}
	}
	g_sim.entityPending = 0;
	if (g_flightRuntimeStateInitialized > 1 && g_dormantFlightRegionSessionEarlyReturnFlag != 0)
		return 1;

	if (g_players[playerIdx].regionSessionId != 0) {
		if (g_flightSimSideEffectsSuppressed == 0) {
			objectIndex = g_players[playerIdx].objectIndex;
			if (objectIndex != -1 && g_objectTable[objectIndex].objectType == 0) {
				Mission_ProcessFlightGroupWaveCompletion(g_players[playerIdx].boundFlightGroupIdx);
				if (Player_BindToAvailableCraft(playerIdx, UINT32_MAX, 0, 0) != 0) {
					Player_EndFlightParticipation(playerIdx);
					Player_EmitRemotePlayerDepartedMessages(playerIdx);
				} else if (playerIdx == g_localPlayer) {
					msg_emitLocalPlayerCraftMessage(
						IFMSG_290_PREVIOUS_CRAFT_DESTROYED_NOW_PILOTING_ARG_ARG_ARG);
				}
			}
		}
		return 1;
	}

	if (g_players[playerIdx].hyperspacePhase == 0) {
		if ((g_flightKeyMods & FIRE_MODIFIER_MASK) == FIRE_MODIFIER &&
			g_players[playerIdx].viewState.playerInputBlocked == 0 &&
			g_players[playerIdx].mapCameraState == 0)
			laser_fireplayerweapon(playerIdx);

		savedKeyMods = g_players[playerIdx].savedKeyMods & TARGET_MODIFIER_MASK;
		if ((g_flightKeyMods & TARGET_MODIFIER_MASK) == TARGET_MODIFIER) {
			keyModsHoldTimer = &g_players[playerIdx].keyModsHoldTimer;
			if (savedKeyMods == TARGET_MODIFIER)
				*keyModsHoldTimer += g_elapsedTicks;
			else
				*keyModsHoldTimer = g_elapsedTicks;
			g_players[playerIdx].savedKeyMods = g_flightKeyMods;
			if (*keyModsHoldTimer < TARGET_TAP_MAX_TICKS)
				g_flightKeyMods &= (uint16_t)~TARGET_MODIFIER;
		} else {
			if (savedKeyMods == TARGET_MODIFIER &&
				g_players[playerIdx].keyModsHoldTimer < TARGET_TAP_MAX_TICKS) {
				if (g_players[playerIdx].mapCameraState != 0) {
					newTargetObjectIndex = FlightMap_PickObjectNearestScreenCenter(playerIdx);
					if (newTargetObjectIndex != -1)
						Player_SetTarget(newTargetObjectIndex, playerIdx);
				} else if (g_flightMissionState.provingGroundsModeActive == 0 &&
						   g_players[playerIdx].viewState.playerInputBlocked == 0) {
					newTargetObjectIndex = Player_PickTargetInSight(playerIdx);
					if (newTargetObjectIndex != -1)
						Player_SetTarget(newTargetObjectIndex, playerIdx);
				}
			}
			g_players[playerIdx].savedKeyMods = g_flightKeyMods;
			g_players[playerIdx].keyModsHoldTimer = 0;
		}
	}

	bool throttle_eligible = XvtFlightControls_ThrottleEligible((unsigned)playerIdx);
	int throttle_object = g_players[playerIdx].objectIndex;
	unsigned throttle_signature = g_players[playerIdx].boundObjectSignature;
	if (g_players[playerIdx].msgTypeId == FLIGHT_CHAT_RECIPIENT_INACTIVE)
		Flight_ProcessPlayerActions(playerIdx);
	else
		FlightChat_HandleInput(playerIdx);
	if (g_players[playerIdx].connectedFlag != 0)
		Player_UpdateFlightControlsAndCamera(playerIdx);
	/* Recorded throttle wins over same-tick key/modifier adjustments, never across a craft transition. */
	if (throttle_eligible && throttle_object == g_players[playerIdx].objectIndex &&
		throttle_signature == g_players[playerIdx].boundObjectSignature)
		XvtFlightControls_ApplyThrottle((unsigned)playerIdx, &g_replayInputs[playerIdx]);
	return 1;
}

int XvtFlightSim_Advance(int targetGameTime) {
	enum {
		PLAYER_COUNT = sizeof(g_inputFrameCount) / sizeof(g_inputFrameCount[0]),
		MINIMUM_REPLAY_TICKS = 4,
	};

	int suppressSideEffects;
	int playerIdx;
	int savedElapsedTicks;
	int savedSimStepScale;

	if (g_sim.replayPending)
		targetGameTime = g_sim.advanceTarget;
	else
		g_sim.advanceTarget = targetGameTime;
	suppressSideEffects = !XvtFlightTiming_IsNetwork125() ? 1 : g_flightSimSideEffectsSuppressed;
	for (playerIdx = g_sim.replayPending ? g_sim.player : 0; playerIdx < PLAYER_COUNT; ++playerIdx) {
		InputFrame* frame;
		int frameIteration;
		int frameCount;

		if (g_players[playerIdx].connectedFlag == 0)
			continue;

		frameCount = g_sim.replayPending ? g_sim.frameCount : g_inputFrameCount[playerIdx];
		frame = &g_inputHistory[playerIdx][g_sim.replayPending ? g_sim.frameIndex : 0];
		for (frameIteration = g_sim.replayPending ? g_sim.frameIteration : 0; frameIteration < frameCount;
			 ++frameIteration, ++frame) {
			int savedGameTime;
			uint8_t connectedFlag;

			if (!g_sim.replayPending) {
				if (!((suppressSideEffects != 0 && XvtFlightTiming_IsNetwork125()) ||
					  frame->timestamp > g_players[playerIdx].lockstepTimestamp || frame->applied != 0)) {
					FlightSync_RemoveInputHistoryFrame(playerIdx, frame);
					--frame;
					continue;
				}
				if (frame->timestamp > targetGameTime || (suppressSideEffects == 0 && frame->valid != 0))
					continue;
				if (g_players[playerIdx].lockstepTimestamp >= frame->timestamp)
					continue;

				savedGameTime = g_gameTime;
				if (g_gameTime >= frame->timestamp && g_players[playerIdx].objectIndex != -1) {
					ObjectRecord* object;
					MobileObject* mobileObject;

					object = &g_objectTable[g_players[playerIdx].objectIndex];
					mobileObject = object->mobj;
					if (mobileObject == NULL || mobileObject->pCraft == NULL)
						continue;
					if (g_players[playerIdx].savedFieldId == object->objectSignature &&
						g_players[playerIdx].savedRegion == g_players[playerIdx].regionSessionId) {
						if (mobileObject->simStateTimestamp > g_players[playerIdx].lockstepTimestamp) {
							XvtFlightCheckpoint_RestorePlayer(playerIdx);
							mobileObject->simStateTimestamp = g_players[playerIdx].lockstepTimestamp;
							g_objectTable[g_players[playerIdx].objectIndex].world_x =
								g_players[playerIdx].savedX;
							g_objectTable[g_players[playerIdx].objectIndex].world_y =
								g_players[playerIdx].savedY;
							g_objectTable[g_players[playerIdx].objectIndex].world_z =
								g_players[playerIdx].savedZ;
							g_objectTable[g_players[playerIdx].objectIndex].roll =
								g_players[playerIdx].savedRoll;
							g_objectTable[g_players[playerIdx].objectIndex].pitch =
								g_players[playerIdx].savedPitch;
							g_objectTable[g_players[playerIdx].objectIndex].yaw =
								g_players[playerIdx].savedYaw;
							g_objectTable[g_players[playerIdx].objectIndex].mobj->lifetimeTimer =
								g_players[playerIdx].savedLifetimeTimer;
							g_objectTable[g_players[playerIdx].objectIndex].mobj->rollImpulseRate =
								g_players[playerIdx].savedRollImpulseRate;
							g_objectTable[g_players[playerIdx].objectIndex].mobj->speed =
								g_players[playerIdx].savedSpeed;
							g_objectTable[g_players[playerIdx].objectIndex].mobj->speedRemainder =
								g_players[playerIdx].savedSpeedRemainder;
							g_objectTable[g_players[playerIdx].objectIndex].mobj->pCraft->pitch =
								g_players[playerIdx].savedPitch;
							g_objectTable[g_players[playerIdx].objectIndex].mobj->orientMatrixDirty = 1;
							g_objectTable[g_players[playerIdx].objectIndex].mobj->moveVectorDirty = 1;
						}
						if (g_gameTime >
							g_objectTable[g_players[playerIdx].objectIndex].mobj->simStateTimestamp)
							g_gameTime =
								g_objectTable[g_players[playerIdx].objectIndex].mobj->simStateTimestamp;
					} else {
						if (suppressSideEffects != 0)
							continue;
						XvtFlightCheckpoint_InvalidatePlayer(playerIdx);
						frame->timestamp = XvtFlightTiming_IsNetwork125() ? (g_gameTime & ~1) + 2
																		  : g_gameTime + MINIMUM_REPLAY_TICKS;
						g_players[playerIdx].lockstepTimestamp = g_gameTime;
					}
				}

				savedElapsedTicks = g_elapsedTicks;
				savedSimStepScale = g_simStepScale;
				if (g_players[playerIdx].objectIndex != -1) {
					g_singleObjectUpdateOverrideIdx = g_players[playerIdx].objectIndex;
					if (g_objectTable[g_singleObjectUpdateOverrideIdx].mobj != NULL) {
						ObjectRecord* object;

						g_elapsedTicks = (uint16_t)(frame->timestamp - g_gameTime);
						if (g_elapsedTicks == 0)
							g_simStepScale = SIMULATION_TICKS_PER_SECOND;
						else
							g_simStepScale = (uint16_t)(SIMULATION_TICKS_PER_SECOND / g_elapsedTicks);
						if (g_simStepScale == 0)
							g_simStepScale = 1;
						Flight_UpdateCraftSteeringAndSpeed();
						Object_UpdateLifetimeAndMovement();
						g_objectTable[g_singleObjectUpdateOverrideIdx].mobj->simStateTimestamp =
							frame->timestamp;
						object = &g_objectTable[g_singleObjectUpdateOverrideIdx];
						g_players[playerIdx].savedX = object->world_x;
						g_players[playerIdx].savedY = object->world_y;
						g_players[playerIdx].savedZ = object->world_z;
						g_players[playerIdx].savedRoll = object->roll;
						g_players[playerIdx].savedPitch = object->pitch;
						g_players[playerIdx].savedYaw = object->yaw;
						g_players[playerIdx].savedLifetimeTimer = object->mobj->lifetimeTimer;
						g_players[playerIdx].savedRollImpulseRate = object->mobj->rollImpulseRate;
						g_players[playerIdx].savedSpeed = object->mobj->speed;
						g_players[playerIdx].savedSpeedRemainder = object->mobj->speedRemainder;
						g_players[playerIdx].savedFieldId = object->objectSignature;
						g_players[playerIdx].savedRegion = g_players[playerIdx].regionSessionId;
						XvtFlightCheckpoint_SavePlayer(playerIdx, frame->timestamp);
					}
					g_singleObjectUpdateOverrideIdx = -1;
				}

				g_elapsedTicks = (uint16_t)(frame->timestamp - g_players[playerIdx].lockstepTimestamp);
				if (!XvtFlightTiming_IsUnlocked() && g_elapsedTicks < MINIMUM_REPLAY_TICKS)
					g_elapsedTicks = MINIMUM_REPLAY_TICKS;
				if (g_elapsedTicks == 0)
					g_simStepScale = SIMULATION_TICKS_PER_SECOND;
				else
					g_simStepScale = (uint16_t)(SIMULATION_TICKS_PER_SECOND / g_elapsedTicks);
				if (g_simStepScale == 0)
					g_simStepScale = 1;

				g_players[playerIdx].lockstepTimestamp = frame->timestamp;
				g_replayInputs[playerIdx] = frame->input;
				if (suppressSideEffects != 0 && frame->timestamp <= savedGameTime) {
					g_replayInputs[playerIdx].key = 0;
					g_replayInputs[playerIdx].keyMods = 0;
					g_replayInputs[playerIdx].flags = 0;
					g_replayInputs[playerIdx].throttle = 0;
				}
				if (XvtFlightTiming_IsNetwork125() && g_localPlayer == playerIdx) {
					g_flightSfxSideEffectGate = 2;
					if (frame->timestamp > g_lastLocalReplayInputTimestamp) {
						g_flightSfxSideEffectGate = 1;
						g_lastLocalReplayInputTimestamp = frame->timestamp;
					}
				}
				g_sim.savedElapsed = savedElapsedTicks;
				g_sim.savedScale = savedSimStepScale;
				g_sim.savedGameTime = savedGameTime;
			}
			if (!XvtFlightSim_UpdateEntity(playerIdx)) {
				g_sim.replayPending = 1;
				g_sim.player = playerIdx;
				g_sim.frameIndex = (int)(frame - g_inputHistory[playerIdx]);
				g_sim.frameIteration = frameIteration;
				g_sim.frameCount = frameCount;
				return 0;
			}
			g_sim.replayPending = 0;
			if (XvtFlightTiming_IsNetwork125() && !suppressSideEffects &&
				frame->valid == XVT_INPUT_AUTHORITATIVE)
				XvtFlightPrediction_Confirm(playerIdx, frame->timestamp, &frame->input);
			savedElapsedTicks = g_sim.savedElapsed;
			savedSimStepScale = g_sim.savedScale;
			savedGameTime = g_sim.savedGameTime;
			g_elapsedTicks = (uint16_t)savedElapsedTicks;
			g_simStepScale = (uint16_t)savedSimStepScale;
			connectedFlag = g_players[playerIdx].connectedFlag;
			g_gameTime = savedGameTime;
			g_flightSfxSideEffectGate = 0;
			if (connectedFlag == 0)
				break;
		}
	}
	return 1;
}

static XvtFlightStepResult XvtFlightSim_FinishedAdvance(void) {
	return !g_flightSimSideEffectsSuppressed && g_flightMissionState.missionEndPending ? XVT_STEP_TERMINAL
																					   : XVT_STEP_COMPLETE;
}

XvtFlightStepResult XvtFlightSim_StepToTime(int targetGameTime) {
	enum { MINIMUM_SIM_STEP_TICKS = 1 };

	int gameTime;

	if (!g_sim.stepPending) {
		g_sim.target = targetGameTime;
		g_sim.stepGameTime = g_gameTime;
		g_gunnerCollisionProbeCount = 0;
	}
	targetGameTime = g_sim.target;
	gameTime = g_sim.stepGameTime;
	if (g_sim.zeroStep) {
		if (!XvtFlightSim_Advance(gameTime + (uint16_t)g_elapsedTicks))
			return XVT_STEP_PENDING;
		g_sim.zeroStep = g_sim.stepPending = 0;
		return XvtFlightSim_FinishedAdvance();
	}
	for (;;) {
		if (!g_sim.stepPending) {
			g_elapsedTicks = (uint16_t)(targetGameTime - gameTime);
			if (g_elapsedTicks < MINIMUM_SIM_STEP_TICKS)
				break;
			if ((int)(uint16_t)g_elapsedTicks > XvtFlightTiming_SimulationMaximum())
				g_elapsedTicks = (uint16_t)XvtFlightTiming_SimulationMaximum();
			g_simStepScale = (uint16_t)(SIMULATION_TICKS_PER_SECOND / (int)(uint16_t)g_elapsedTicks);
			if (g_simStepScale == 0)
				g_simStepScale = MINIMUM_SIM_STEP_TICKS;
			g_gameTime = gameTime;
			g_sim.stepGameTime = gameTime;
			XvtFlightTiming_BeginAdvance(g_elapsedTicks);
		}
		if (!XvtFlightSim_Advance(gameTime + (uint16_t)g_elapsedTicks)) {
			g_sim.stepPending = 1;
			return XVT_STEP_PENDING;
		}
		g_sim.stepPending = 0;
		if (g_flightSimSideEffectsSuppressed == 0 && g_flightMissionState.missionEndPending == 1) {
			XvtFlightTiming_EndAdvance();
			return XVT_STEP_TERMINAL;
		}

		if (g_flightMissionState.provingGroundsModeActive == 0 && XvtFlightTiming_ReferenceDue()) {
			XvtFlightClock clock = XvtFlightTiming_EnterReference();
			Mission_UpdateFlightGroupArrivals();
			pai_UpdateAllCraftAI();
			XvtFlightTiming_RestoreClock(clock);
		}
		Flight_UpdateTimers();
		laser_weaponsfire();
		XvtReferenceMotion_CommitBoundary();
		Flight_UpdateCraftSteeringAndSpeed();
		if (g_debrisEnabled != 0 && g_flightMissionState.provingGroundsModeActive == 0 &&
			XvtFlightTiming_ReferenceDue())
			FlightObject_UpdateDebrisAndTransientAnimations();
		collide_collisions();
		if (g_flightSimSideEffectsSuppressed == 0 && g_flightMissionState.missionEndPending == 1) {
			XvtFlightTiming_EndAdvance();
			return XVT_STEP_TERMINAL;
		}

		Object_UpdateLifetimeAndMovement();
		FlightObject_UpdateSpecialBehavior();
		if (g_flightSimSideEffectsSuppressed == 0 && g_flightMissionState.missionEndPending == 1) {
			XvtFlightTiming_EndAdvance();
			return XVT_STEP_TERMINAL;
		}

		Player_ValidateAllCurrentTargets();
		Player_UpdateParticipationState();
		if (g_flightSimSideEffectsSuppressed == 0 && g_flightMissionState.missionEndPending == 1) {
			XvtFlightTiming_EndAdvance();
			return XVT_STEP_TERMINAL;
		}

		if (XvtFlightTiming_ReferenceDue() || g_flightMissionState.missionEndPending)
			Mission_UpdateLogic();
		Hud_UpdateFlightMessagePanes();
		Flight_UpdateDynamicMusicState();
		if (g_fsfxLoaded != 0) {
			fsfx_UpdateVoiceQueue();
			fsfx_UpdateFlightSfx();
		}
		gameTime = g_gameTime;
		gameTime += (uint16_t)g_elapsedTicks;
		g_gameTime = gameTime;
		XvtFlightTiming_EndAdvance();
		if (gameTime >= targetGameTime)
			return XvtFlightSim_FinishedAdvance();
	}

	g_gameTime = gameTime;
	XvtFlightTiming_EndAdvance();
	if (!XvtFlightSim_Advance(gameTime + (uint16_t)g_elapsedTicks)) {
		g_sim.stepGameTime = gameTime;
		g_sim.zeroStep = g_sim.stepPending = 1;
		return XVT_STEP_PENDING;
	}
	return XvtFlightSim_FinishedAdvance();
}

void XvtFlightHistory_RestoreCheckpoint(void) {
	for (unsigned player = 0; player < XVT_FLIGHT_PLAYERS; ++player) {
		int retained = 0;
		for (int i = 0; i < g_inputFrameCount[player]; ++i) {
			const InputFrame* frame = &g_inputHistory[player][i];
			if (!g_players[player].connectedFlag || frame->valid == XVT_INPUT_PREDICTED ||
				frame->timestamp <= g_players[player].lockstepTimestamp)
				continue;
			g_inputHistory[player][retained++] = *frame;
		}
		g_inputFrameCount[player] = retained;
	}
}

XvtInputInsertStatus XvtFlightHistory_Insert(unsigned player, int tick, const FlightInputFrameRecord* input,
											 InputFrame** out) {
	*out = NULL;
	if (player >= XVT_FLIGHT_PLAYERS || tick <= 0 || !input || (input->flags & ~XVT_INPUT_THROTTLE_PRESENT) ||
		(!(input->flags & XVT_INPUT_THROTTLE_PRESENT) && input->throttle))
		return XVT_INPUT_INVALID;
	int count = g_inputFrameCount[player], index = 0;
	if (count < 0 || count > XVT_INPUT_HISTORY_CAPACITY)
		return XVT_INPUT_INVALID;
	InputFrame* frames = g_inputHistory[player];
	while (index < count && frames[index].timestamp < tick)
		++index;
	if (index < count && frames[index].timestamp == tick) {
		if (!frames[index].valid || frames[index].applied == 1)
			return XVT_INPUT_DUPLICATE;
	} else {
		if (count == XVT_INPUT_HISTORY_CAPACITY)
			return XVT_INPUT_FULL;
		memmove(frames + index + 1, frames + index, (count - index) * sizeof *frames);
		++g_inputFrameCount[player];
	}
	InputFrame* frame = frames + index;
	frame->timestamp = tick;
	frame->valid = XVT_INPUT_REAL;
	frame->applied = 0;
	frame->input = *input;
	*out = frame;
	return XVT_INPUT_INSERTED;
}

XvtInputInsertStatus XvtFlightHistory_InsertReal(unsigned player, int tick,
												 const FlightInputFrameRecord* input, int authoritative) {
	InputFrame* frame;
	XvtInputInsertStatus status = XvtFlightHistory_Insert(player, tick, input, &frame);
	if (status == XVT_INPUT_FULL) {
		FlightSync_DiscardPredictedInputFrames(player);
		status = XvtFlightHistory_Insert(player, tick, input, &frame);
	}
	if (status == XVT_INPUT_FULL || status == XVT_INPUT_INVALID) {
		return status;
	}
	if (frame) {
		frame->valid = authoritative ? XVT_INPUT_AUTHORITATIVE : XVT_INPUT_REAL;
		frame->applied = !authoritative && NetSession_GetLocalPlayerId();
	} else if (authoritative) {
		for (int i = 0; i < g_inputFrameCount[player]; ++i) {
			InputFrame* old = &g_inputHistory[player][i];
			if (old->timestamp != tick)
				continue;
			if (old->input.key != input->key || old->input.keyMods != input->keyMods ||
				old->input.axisX != input->axisX || old->input.axisY != input->axisY ||
				old->input.axisR != input->axisR || old->input.flags != input->flags ||
				old->input.throttle != input->throttle) {
				Aeron_LogError("xvt.network", "conflicting authoritative input for player %u at %d", player,
							   tick);
				return XVT_INPUT_CONFLICT;
			}
			old->valid = old->applied = 0;
		}
	}
	return status;
}

void XvtFlightHistory_Recover(void) {
	XvtFlightPrediction_Reset();
	for (unsigned player = 0; player < XVT_FLIGHT_PLAYERS; ++player) {
		int retained = 0;
		for (int i = 0; i < g_inputFrameCount[player]; ++i) {
			const InputFrame* frame = &g_inputHistory[player][i];
			/* Host records will reconstruct peer input; preserve only future local samples. */
			if (player != (unsigned)g_localPlayer || !g_players[player].connectedFlag ||
				frame->timestamp <= g_gameTime || frame->valid == XVT_INPUT_PREDICTED)
				continue;
			g_inputHistory[player][retained++] = *frame;
		}
		g_inputFrameCount[player] = retained;
	}
}
