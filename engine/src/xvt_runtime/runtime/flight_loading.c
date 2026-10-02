#include "xvt_runtime/runtime/flight_internal.h"

enum {
	PLAYER_COUNT = sizeof(g_players) / sizeof(g_players[0]),
	PALETTE_COLOR_COUNT = 256,
	PALETTE_BYTES = PALETTE_COLOR_COUNT * sizeof(RgbTriplet),
	PALETTE_HALF_BYTES = PALETTE_BYTES / 2,
	PALETTE_LAST_COLOR_OFFSET = PALETTE_BYTES - sizeof(RgbTriplet),
	MISSION_PALETTE_FIRST_COLOR = 64,
	FLIGHT_RESOURCE_SCRATCH_BYTES = 1024,
	MISSION_EXTENSION_LENGTH = 3,
	MISSION_EXTENSION_FIRST = 0,
	MISSION_EXTENSION_SECOND = 1,
	MISSION_EXTENSION_THIRD = 2,
	PALETTE_CHANNEL_RED = 0,
	PALETTE_CHANNEL_GREEN = 1,
	PALETTE_CHANNEL_BLUE = 2,
	NOISE_TABLE_VALUE_LIMIT = 124,
	NO_VIEWPORT_INSET = 0,
	MUSIC_TRACK_FLIGHT = 2,
	MUSIC_START_CHOICE_COUNT = 4,
	MUSIC_VOLUME_MAX_LEVEL = 9,
	MUSIC_FADE_DIVISOR = 8,
	MUSIC_FADE_DURATION_MS = 1000,
	MILLISECONDS_PER_SECOND = 1000,
	MILLISECONDS_PER_MINUTE = 60000,
	PROVING_GROUNDS_DEFAULT_CRAFT = 2,
	PROVING_GROUNDS_DEFAULT_LEVEL = 4,
	PROVING_GROUNDS_SCORE_STEP_POINTS = 2000,
	PROVING_GROUNDS_SCORE_STEPS_PER_LEVEL = 5,
	RANDOM_SEED_XOR = 0xBEEF,
	ASTEROID_FIELD_RANDOM_SEED = -21267,
	DEFAULT_MODEL_LIGHT_DIRECTION = 18900,
};

void XvtFlightLoading_Reset(void)
{
	g_objectTableHandle = g_mobileObjectPoolHandle =
		g_mobileObjectCharDataHandle = 0;
	g_craftDataPoolHandle = g_warheadGuidancePoolHandle = 0;
	g_stringDataHandle = g_renderObjectListHandle = 0;
	g_flightSmallFontHandle = g_flightMicroFontHandle =
		g_flightMediumFontHandle = 0;
	g_flightScratchScreenBufferHandle = g_flightAuxBufferHandle =
		g_flightOffscreenBufferHandle = 0;
	g_hudPanelSpriteDataHandle = g_flightIconFramesHandle =
		g_messageLogHandle = 0;
	g_objectTable = NULL;
	g_mobileObjectPoolBase = NULL;
	g_mobileObjectCharDataPool = NULL;
	g_craftDataPoolBase = NULL;
	g_projectileGuidanceStates = NULL;
}

/* Copies this flight's game rules from the settings into the mission state. Multiplayer combat
 * engagements always fly at medium difficulty; only multiplayer takes the time limits and the AI
 * choice from the settings; a combat engagement inside a mission sequence has no random variation. */
static void XvtFlightLoading_MissionRules(void)
{
	if ((unsigned int)g_pilotData.numHumanPlayersLastMission > 1 &&
	    (unsigned int)g_pilotData.missionDirectoryId >=
		    MISSION_DIRECTORY_COMBAT_ENGAGEMENTS) {
		g_flightMissionState.difficulty = GAME_DIFFICULTY_MEDIUM;
	} else {
		g_flightMissionState.difficulty = g_gameConfig.difficulty;
		if (g_flightMissionState.difficulty > GAME_DIFFICULTY_HARD) {
			g_flightMissionState.difficulty = GAME_DIFFICULTY_EASY;
		}
	}
	g_flightMissionState.collisionsEnabled = g_gameConfig.collisions;
	g_flightMissionState.craftJumpingEnabled = g_gameConfig.craftJumping;
	g_flightMissionState.randomVariationEnabled = g_gameConfig.randomSetup;
	g_flightMissionState.battleLengthIndex = g_gameConfig.battleLengthIndex;
	g_flightMissionState.locatePlayersEnabled = g_gameConfig.locatePlayers;
	g_flightMissionState.playerFlightGroupWaveMode =
		g_gameConfig.craftWaves;
	if (g_pilotData.numHumanPlayersLastMission > 1) {
		g_flightMissionState.missionTimeLimitMinutes =
			g_gameConfig.missionTimeLimit;
		g_flightMissionState.teamVictoryTimeLimitMinutes =
			g_gameConfig.lastTeamTimeLimitMinutes;
		g_flightMissionState.aiOpponentsEnabled =
			g_gameConfig.aiOpponents;
	} else {
		g_flightMissionState.missionTimeLimitMinutes = UINT8_MAX;
		g_flightMissionState.teamVictoryTimeLimitMinutes = 0;
		g_flightMissionState.aiOpponentsEnabled = 1;
	}
	g_flightMissionState.craftImpactBounceEnabled = 1;
	if ((unsigned int)g_pilotData.missionDirectoryId >=
		    MISSION_DIRECTORY_COMBAT_ENGAGEMENTS &&
	    g_pilotData.missionSequenceActive == 1) {
		g_flightMissionState.randomVariationEnabled = 0;
	}
}

void XvtFlightLoading_Globals(void)
{
	int16_t abortPlayerIndex, disconnectPlayerIndex, connectPlayerIndex;
	XvtFlightLoading_Reset();
	Flight_PumpWindowMessages();
	g_packetDropIndicator = 0;
	g_lagIndicator = 0;
	g_sw3dSkipOddScanlines = 0;
	g_flightNetHostAbortReceived = 0;
	for (abortPlayerIndex = 0; abortPlayerIndex < PLAYER_COUNT;
	     ++abortPlayerIndex) {
		g_playerAbortFlags[abortPlayerIndex] = 0;
	}

	fsfx_ClearSfxNameTable();
	FlightSync_ResetRemotePlayerRenderSmoothing();
	FlightLoading_ResetProgressState();
	Time_ResetElapsedTicks();
	g_flightDisplaySurfacesActive = 1;
	g_flightDrawToHudLayer = 1;
	if (g_flightViewportInsetX == NO_VIEWPORT_INSET &&
	    g_surfaceWidth == 320) {
		g_flightResolutionMode = FLIGHT_RESOLUTION_320X240;
	} else if (g_flightViewportInsetX == NO_VIEWPORT_INSET &&
		   g_surfaceWidth == 480) {
		g_flightResolutionMode = FLIGHT_RESOLUTION_480X360;
	} else {
		g_flightResolutionMode = FLIGHT_RESOLUTION_640X480;
	}

	g_flightSimSideEffectsSuppressed = 0;
	g_unusedFlightSessionResetState = 0;
	g_unusedFlightTransientResetState = 0;
	g_localPlayer =
		NetSession_FindPlayerSlotByDpid(NetSession_GetLocalDplayId());
	g_activeFlightPlayerCount = NetSession_GetPlayerCount();
	g_flightPlayerCount = g_activeFlightPlayerCount;
	memset(g_replayInputs, 0, sizeof(g_replayInputs));
	memset(g_unusedFlightNetworkBlock, 0,
	       sizeof(g_unusedFlightNetworkBlock));
	memset(g_unusedFlightRuntimeBlock, 0,
	       sizeof(g_unusedFlightRuntimeBlock));
	memset(&g_currentInputFrame, 0, sizeof(g_currentInputFrame));
	g_remotePlayerRenderSmoothingEnabled = g_internetPlayEnabled;
	g_flightMissionState.connectedPlayerCount = g_activeFlightPlayerCount;
	g_flightMissionState.maxConnectedPlayerCountThisMission =
		g_activeFlightPlayerCount;

	XvtFlightLoading_MissionRules();

	if (g_activeFlightPlayerCount != 1) {
		g_gameRandFeedbackState = (int16_t)g_gameConfig.randomSeed;
	} else {
		uint16_t randomSeed;

		randomSeed = (uint16_t)timeGetTime();
		randomSeed ^= RANDOM_SEED_XOR;
		g_gameRandFeedbackState = (int16_t)randomSeed;
	}
	{
		uint32_t randomTime;

		randomTime = timeGetTime();
		g_asteroidFieldRandSeed = (uint16_t)ASTEROID_FIELD_RANDOM_SEED;
		g_gameRand2FeedbackState =
			(uint16_t)(randomTime + g_gameRandFeedbackState);
	}

	memset(g_players, 0, sizeof(g_players));
	{
		int16_t resetPlayerIndex;

		for (resetPlayerIndex = 0; resetPlayerIndex < PLAYER_COUNT;
		     ++resetPlayerIndex) {
			g_inputFrameCount[resetPlayerIndex] = 0;
			g_playerConnected[resetPlayerIndex] = 1;
			g_flightNetWorldChecksumPeerStatus[resetPlayerIndex] =
				0;
			g_players[resetPlayerIndex].lockstepTimestamp = 0;
			g_players[resetPlayerIndex].nextEngineWashCheckTime = 0;
			g_players[resetPlayerIndex].field_5B5 = 0;
		}
	}
	for (disconnectPlayerIndex = 0; disconnectPlayerIndex < PLAYER_COUNT;
	     ++disconnectPlayerIndex) {
		g_players[disconnectPlayerIndex].participationState = 0;
	}
	for (connectPlayerIndex = 0;
	     connectPlayerIndex < g_activeFlightPlayerCount;
	     ++connectPlayerIndex) {
		g_players[connectPlayerIndex].participationState = 1;
	}

	g_flightNetBufferWorldMessagesUntilChecksum = 0;
	g_flightNetWorldChecksumEpoch = 0;
	g_singleObjectUpdateOverrideIdx = -1;
	if (g_flightConfNoPilot == 0) {
		Mission_SyncPilotNetworkPlayersToSessionSlots();
	}
	pai_loadplans((char *)g_paiPlanResourceBaseName);
	pai_cacheBuiltinPlanIds();
	g_hudCockpitResourcesLoaded = 0;
	g_flightSwRotSpriteCoeffCacheValid = 0;
	g_unusedFlightStartupObjectPassState = 0;
	g_unusedFlightDebugLogFile = NULL;
	g_flightSwRotSpriteSpanRunsEnabled = 1;
}

void XvtFlightLoading_Palette(void)
{
	uint8_t resourceScratch[FLIGHT_RESOURCE_SCRATCH_BYTES];
	int16_t paletteByteOffset, missionExtensionOffset;
	char savedMissionExtensionPrefix[2], savedMissionExtensionThird;
	FlightSurface_Lock();
	FlightDisplay_ConfigureResolutionState();
	FlightSurface_Unlock();
	FlightRender_TransitionHookStub();
	FlightSurface_Lock();
	FlightSw_InitFramebuffer();
	FlightSurface_Unlock();
	nullsub_11();
	FlightDisplay_Flip();
	FlightRender_ConfigureCallbacksForResolution(3);
	FeDiskIo_ReadAllBytesOrFatal(g_flightPaletteResourceFileName,
				     resourceScratch);
	for (paletteByteOffset = 0; paletteByteOffset < PALETTE_HALF_BYTES;
	     paletteByteOffset += sizeof(RgbTriplet)) {
		uint8_t channel;

		channel = resourceScratch[paletteByteOffset +
					  PALETTE_CHANNEL_RED] >>
			  2;
		resourceScratch[paletteByteOffset + PALETTE_CHANNEL_RED] =
			resourceScratch[PALETTE_LAST_COLOR_OFFSET -
					paletteByteOffset +
					PALETTE_CHANNEL_RED] >>
			2;
		resourceScratch[PALETTE_LAST_COLOR_OFFSET - paletteByteOffset +
				PALETTE_CHANNEL_RED] = channel;
		channel = resourceScratch[paletteByteOffset +
					  PALETTE_CHANNEL_GREEN] >>
			  2;
		resourceScratch[paletteByteOffset + PALETTE_CHANNEL_GREEN] =
			resourceScratch[PALETTE_LAST_COLOR_OFFSET -
					paletteByteOffset +
					PALETTE_CHANNEL_GREEN] >>
			2;
		resourceScratch[PALETTE_LAST_COLOR_OFFSET - paletteByteOffset +
				PALETTE_CHANNEL_GREEN] = channel;
		channel = resourceScratch[paletteByteOffset +
					  PALETTE_CHANNEL_BLUE] >>
			  2;
		resourceScratch[paletteByteOffset + PALETTE_CHANNEL_BLUE] =
			resourceScratch[PALETTE_LAST_COLOR_OFFSET -
					paletteByteOffset +
					PALETTE_CHANNEL_BLUE] >>
			2;
		resourceScratch[PALETTE_LAST_COLOR_OFFSET - paletteByteOffset +
				PALETTE_CHANNEL_BLUE] = channel;
	}
	g_flightSetPaletteRangeFn((RgbTriplet *)resourceScratch, 0,
				  PALETTE_COLOR_COUNT);
	FlightPalette_ApplyToDisplay();
	FlightSurface_Lock();
	FeDiskIo_InitGlobalBuffers();
	FlightSurface_Unlock();
	FlightSurface_ClearToBlack();

	if (g_flightBytesPerPixel == 1) {
		missionExtensionOffset = (int)strlen(g_currentMissionFile) -
					 MISSION_EXTENSION_LENGTH;
		savedMissionExtensionPrefix[MISSION_EXTENSION_FIRST] =
			g_currentMissionFile[missionExtensionOffset +
					     MISSION_EXTENSION_FIRST];
		savedMissionExtensionPrefix[MISSION_EXTENSION_SECOND] =
			g_currentMissionFile[missionExtensionOffset +
					     MISSION_EXTENSION_SECOND];
		savedMissionExtensionThird =
			g_currentMissionFile[missionExtensionOffset +
					     MISSION_EXTENSION_THIRD];
		g_currentMissionFile[missionExtensionOffset +
				     MISSION_EXTENSION_FIRST] = 'p';
		g_currentMissionFile[missionExtensionOffset +
				     MISSION_EXTENSION_SECOND] = 'a';
		g_currentMissionFile[missionExtensionOffset +
				     MISSION_EXTENSION_THIRD] = 'l';
		if (FeDiskIo_OpenGlobalStream(g_currentMissionFile, "rb", 0,
					      0) == 0) {
			g_generateMissionPalette = 1;
		} else {
			g_generateMissionPalette = 0;
			FeDiskIo_CloseGlobalStream(0);
			FeDiskIo_ReadAllBytesOrFatal(g_currentMissionFile,
						     g_flightAuxBuffer);
			g_flightSetPaletteRangeFn(
				(RgbTriplet *)g_flightAuxBuffer,
				MISSION_PALETTE_FIRST_COLOR,
				PALETTE_COLOR_COUNT -
					MISSION_PALETTE_FIRST_COLOR);
		}
		g_currentMissionFile[missionExtensionOffset +
				     MISSION_EXTENSION_FIRST] =
			savedMissionExtensionPrefix[MISSION_EXTENSION_FIRST];
		g_currentMissionFile[missionExtensionOffset +
				     MISSION_EXTENSION_SECOND] =
			savedMissionExtensionPrefix[MISSION_EXTENSION_SECOND];
		g_currentMissionFile[missionExtensionOffset +
				     MISSION_EXTENSION_THIRD] =
			savedMissionExtensionThird;
	}
}

void XvtFlightLoading_MissionSetup(void)
{
	int16_t mfdIndex;
	if (g_flightConfTrainCourse != 0) {
		g_flightMissionState.provingGroundsCraftType =
			PROVING_GROUNDS_DEFAULT_CRAFT;
		g_flightMissionState.provingGroundsLevel =
			PROVING_GROUNDS_DEFAULT_LEVEL;
	} else {
		g_flightMissionState.provingGroundsCraftType = 0;
		g_flightMissionState.provingGroundsLevel = 0;
	}
	FlightSurface_Lock();
	FlightInput_ResetRuntimeState();
	FlightSurface_Unlock();
	{
		int16_t noiseIndex;

		for (noiseIndex = 0;
		     noiseIndex < (int)sizeof(g_flightNoiseTable) - 1;
		     noiseIndex += 2) {
			do {
				g_flightNoiseTable[noiseIndex] =
					(uint8_t)(rand() & 0x7F);
			} while (g_flightNoiseTable[noiseIndex] >
				 NOISE_TABLE_VALUE_LIMIT);
			g_flightNoiseTable[noiseIndex + 1] =
				(uint8_t)(rand() & 3);
		}
	}

	g_messageLogTotalCount = 0;
	g_unusedFlightMessageRuntimeState = 0;
	g_worldLightDirectionX = DEFAULT_MODEL_LIGHT_DIRECTION;
	g_worldLightDirectionY = DEFAULT_MODEL_LIGHT_DIRECTION;
	g_worldLightDirectionZ = DEFAULT_MODEL_LIGHT_DIRECTION;
	g_systemMessageDisplayEnabled = 1;
	g_readyMessagePaneLeft = -1;
	g_messageLogWriteIndex = UINT16_MAX;
	g_mfdActivePage = MFD_PAGE_NONE;
	g_mfdSecondaryPage = MFD_PAGE_NONE;
	g_mfdSavedActivePage = MFD_PAGE_NONE;
	g_mfdSavedSecondaryPage = MFD_PAGE_NONE;
	for (mfdIndex = 0; mfdIndex < (int)(sizeof(g_mfdPageStates) /
					    sizeof(g_mfdPageStates[0]));
	     ++mfdIndex) {
		g_savedMfdPageStates[mfdIndex] = MFD_PAGE_STATE_CLOSED;
		g_mfdPageStates[mfdIndex] = MFD_PAGE_STATE_CLOSED;
	}
	g_damageMfdCurrentSystemId = 0;
}

void XvtFlightLoading_Runtime(void)
{
	FlightSurface_Lock();
	Mission_InitFlightRuntimeState();
	FlightSurface_Unlock();
	g_dynamicMusicOutcomeLatched = 0;
	if (g_gameConfig.musicEnabled != 0 && g_gameConfig.musicVolume != 0 &&
	    MusicCd_Initialize() != 0) {
		int musicChoice;
		uint16_t musicVolume;
		uint32_t musicUpdateMs;

		musicVolume = UINT16_MAX * g_gameConfig.musicVolume /
			      MUSIC_VOLUME_MAX_LEVEL;
		MusicCd_SetAuxVolume(musicVolume);
		musicChoice = GameRand2() & (MUSIC_START_CHOICE_COUNT - 1);
		MusicCd_PlayTrackFromTime(
			MUSIC_TRACK_FLIGHT,
			g_dynamicMusicInitialStartMinuteChoices[musicChoice],
			g_dynamicMusicInitialStartSecondChoices[musicChoice]);
		g_dynamicMusicTrackRemainingMs =
			MusicCd_GetTrackLengthMs(MUSIC_TRACK_FLIGHT);
		g_dynamicMusicTrackRemainingMs -=
			MILLISECONDS_PER_MINUTE *
			g_dynamicMusicInitialStartMinuteChoices[musicChoice];
		g_dynamicMusicTrackRemainingMs -=
			MILLISECONDS_PER_SECOND *
			g_dynamicMusicInitialStartSecondChoices[musicChoice];
		musicUpdateMs = timeGetTime();
		g_dynamicMusicState = MUSIC_TRACK_FLIGHT;
		g_dynamicMusicLastUpdateMs = musicUpdateMs;
	} else {
		g_dynamicMusicTrackRemainingMs = INT32_MAX;
		g_dynamicMusicState = 0;
	}

	if (g_flightMissionState.provingGroundsModeActive != 0) {
		ProvingGrounds_InitCourseObjects();
		ProvingGrounds_StartLevel(
			g_flightMissionState.provingGroundsLevel);
		if (g_flightMissionState.provingGroundsModeActive != 0 &&
		    g_flightMissionState.provingGroundsLevel > 1) {
			g_msgArgTable[0] =
				g_flightMissionState.provingGroundsLevel - 1;
			msg_emitInFlightMessage(
				IFMSG_197_ARG_10000_POINTS_AWARDED_FOR_PREVIOUS_LEVELS,
				g_localPlayer);
			g_flightMissionState.provingGroundsScore =
				PROVING_GROUNDS_SCORE_STEP_POINTS *
				(PROVING_GROUNDS_SCORE_STEPS_PER_LEVEL *
					 g_flightMissionState
						 .provingGroundsLevel -
				 PROVING_GROUNDS_SCORE_STEPS_PER_LEVEL);
		}
	}
}
