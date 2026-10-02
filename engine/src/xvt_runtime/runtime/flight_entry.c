#include "xvt_runtime/runtime/flight_internal.h"
#include "xvt_runtime/runtime/network_session.h"

enum {
	BUILTIN_ARGUMENT_COUNT = 2,
	PARSED_ARGUMENT_COUNT = 7,
	REQUIRED_ARGUMENT_COUNT = BUILTIN_ARGUMENT_COUNT + PARSED_ARGUMENT_COUNT,
	BRIGHTNESS_CONFIG_OFFSET = 4,
	BRIGHTNESS_CONFIG_SHIFT = 6,
	BRIGHTNESS_SCALE_MIN = 256,
	BRIGHTNESS_SCALE_MAX = 704,
	STAR_DENSITY_HIGH = 4,
	STAR_DENSITY_MEDIUM = 2,
	STAR_DENSITY_LOW = 1,
	LOD_CONFIG_OFFSET = 5,
	LOD_CONFIG_MAX_VALUE = 20,
	LOD_SCALE_INVERSION_NUMERATOR = 1,
	MIPMAPPING_DISABLED_VALUE = 19,
	DISPLAY_WIDTH_LOW = 320,
	DISPLAY_HEIGHT_LOW = 240,
	DISPLAY_WIDTH_MEDIUM = 512,
	DISPLAY_HEIGHT_MEDIUM = 384,
	DISPLAY_WIDTH_HIGH = 640,
	DISPLAY_HEIGHT_HIGH = 480,
	WINDOW_WIDTH_MEDIUM = 480,
	WINDOW_HEIGHT_MEDIUM = 360,
	DISPLAY_CONFIG_LOW = 0,
	DISPLAY_CONFIG_MEDIUM = 1,
	DISPLAY_CONFIG_HIGH = 2,
	PALETTED_BYTES_PER_PIXEL = 1,
	HIGH_COLOR_BYTES_PER_PIXEL = 2,
	DISPLAY_INIT_SOUND_ERROR = 13,
};

static int g_gameSessionStarted;
static int g_soundEngineStarted;
static int g_flightDevicesCreated;

/* Sets the flight's start-up flags: flicker (off when a flicker.txt file exists), laser timing,
 * the async option and the launch switches. Each switch is found by substring anywhere in the
 * mission command line, so a pilot or game name containing a switch's word sets it too. */
static void XvtFlightEntry_ReadLaunchSwitches(char* missionCmdLine) {
	XvtFile* flickerFile;
	char* optionMatch;

	flickerFile = File_Open("flicker.txt", "r");
	if (flickerFile != NULL) {
		File_Close(flickerFile);
		g_flightConfFlicker = 0;
	} else {
		g_flightConfFlicker = 1;
	}

	g_laserFireTimestampTrackingEnabled = 1;
	g_internetPlayEnabled = g_gameConfig.internetPlay;
	optionMatch = strstr(missionCmdLine, "traincourse");
	g_flightConfTrainCourse = 1;
	if (optionMatch == NULL) {
		g_flightConfTrainCourse = 0;
	}
	optionMatch = strstr(missionCmdLine, "nopilot");
	g_flightConfNoPilot = 1;
	if (optionMatch == NULL) {
		g_flightConfNoPilot = 0;
	}
	if (strstr(missionCmdLine, "nodinput") != NULL) {
		g_flightConfDirectInput = 0;
	} else if (strstr(missionCmdLine, "dinput") != NULL) {
		g_flightConfDirectInput = 1;
	} else {
		g_flightConfDirectInput = 1;
	}
	if (strstr(missionCmdLine, "nosfx") != NULL) {
		g_flightConfSfxEnabled = 0;
	} else if (strstr(missionCmdLine, "sfx") != NULL) {
		g_flightConfSfxEnabled = 1;
	} else {
		g_flightConfSfxEnabled = 1;
	}
	if (strstr(missionCmdLine, "nomusic") != NULL) {
		g_flightConfMusicEnabled = 0;
	} else if (strstr(missionCmdLine, "music") != NULL) {
		g_flightConfMusicEnabled = 1;
	} else {
		g_flightConfMusicEnabled = 1;
	}
	if (strstr(missionCmdLine, "novoice") != NULL) {
		g_flightConfVoiceEnabled = 0;
	} else if (strstr(missionCmdLine, "voice") != NULL) {
		g_flightConfVoiceEnabled = 1;
	} else {
		g_flightConfVoiceEnabled = 1;
	}
	if (strstr(missionCmdLine, "notickcounter") != NULL) {
		g_flightConfTickCounterEnabled = 0;
	} else if (strstr(missionCmdLine, "tickcounter") != NULL) {
		g_flightConfTickCounterEnabled = 1;
	} else {
		g_flightConfTickCounterEnabled = 0;
	}
	if (strstr(missionCmdLine, "nomipmaps") != NULL) {
		g_mipmappingEnabled = 0;
	} else if (strstr(missionCmdLine, "mipmaps") != NULL) {
		g_mipmappingEnabled = 1;
	} else {
		g_mipmappingEnabled = 1;
	}
	optionMatch = strstr(missionCmdLine, "inprogress");
	g_flightInProgressLaunch = 1;
	if (optionMatch == NULL) {
		g_flightInProgressLaunch = 0;
	}
	optionMatch = strstr(missionCmdLine, "newnet");
	g_flightConfNewNet = 1;
	if (optionMatch == NULL) {
		g_flightConfNewNet = 0;
	}
	optionMatch = strstr(missionCmdLine, "nolauncher");
	g_flightConfNoLauncher = 1;
	if (optionMatch == NULL) {
		g_flightConfNoLauncher = 0;
	}
	if (strstr(missionCmdLine, "nofullscreen") != NULL) {
		g_flightFullscreen = 0;
	} else if (strstr(missionCmdLine, "fullscreen") != NULL) {
		g_flightFullscreen = 1;
	}
	if (strstr(missionCmdLine, "nopageflip") != NULL) {
		g_flightPageFlip = 0;
	} else if (strstr(missionCmdLine, "pageflip") != NULL) {
		g_flightPageFlip = 1;
	}
	if (missionCmdLine[0] == '-') {
		g_flightStartedWithDashArg = 1;
	} else if (missionCmdLine[0] == '/' && missionCmdLine[1] == '+') {
		g_unusedFlightCmdLinePlusSwitchFlag = 1;
	}
}

int XvtFlightEntry_Prepare(char* missionCmdLine) {
	NetworkTransportType networkType;
	const char* connectionAddress;
	int argumentCount;
	int argumentIndex;
	int commandLineOffset;
	int quotedArgument;

	g_gameSessionStarted = g_soundEngineStarted = g_flightDevicesCreated = 0;
	ModelPreview_FreeResources();
	g_flightRenderToFrontend = 0;
	if (missionCmdLine == NULL) {
		return 0;
	}

	Config_Load();
	XvtFlightEntry_ReadLaunchSwitches(missionCmdLine);

	if (Flight_UpdateAndFocusMainWindow() == 0) {
		return 0;
	}

	commandLineOffset = 0;
	quotedArgument = 0;
	argumentCount = BUILTIN_ARGUMENT_COUNT;
	g_flightLaunchArgs.programName = "xtie";
	g_flightLaunchArgs.sentinel = "/trebla";
	if (missionCmdLine[0] != '\0') {
		for (argumentIndex = 0; argumentIndex < PARSED_ARGUMENT_COUNT; ++argumentIndex) {
			g_flightLaunchArgs.arguments[argumentIndex] = &missionCmdLine[commandLineOffset];
			while (1) {
				char character;

				character = missionCmdLine[commandLineOffset];
				if (character == ' ') {
					if (quotedArgument != 1) {
						break;
					}
				} else if (character == '\0') {
					break;
				}
				if (character == '~') {
					if (quotedArgument != 0) {
						quotedArgument = 0;
						missionCmdLine[commandLineOffset] = '\0';
						++commandLineOffset;
					} else {
						quotedArgument = 1;
						++commandLineOffset;
						g_flightLaunchArgs.arguments[argumentIndex] = &missionCmdLine[commandLineOffset];
					}
				} else {
					++commandLineOffset;
				}
			}
			++argumentCount;
			if (missionCmdLine[commandLineOffset] == '\0') {
				break;
			}
			missionCmdLine[commandLineOffset] = '\0';
			++commandLineOffset;
			if (missionCmdLine[commandLineOffset] == '\0') {
				break;
			}
		}
	}
	if (argumentCount < REQUIRED_ARGUMENT_COUNT) {
		return 0;
	}

	networkType = (NetworkTransportType)g_gameConfig.networkType;
	switch (networkType) {
		case NET_TRANSPORT_TCPIP:
			connectionAddress = g_gameConfig.ipAddress;
			break;
		case NET_TRANSPORT_MODEM:
			connectionAddress = g_gameConfig.phoneNumber;
			break;
		default:
		case NET_TRANSPORT_IPX:
			connectionAddress = NULL;
			break;
	}
	g_gameSessionStarted = 1;
	return NetSession_InitGameSession(g_flightLaunchArgs.arguments[FLIGHT_LAUNCH_ARG_FORMAL_NAME],
									  g_flightLaunchArgs.arguments[FLIGHT_LAUNCH_ARG_PILOT_NAME],
									  atoi(g_flightLaunchArgs.arguments[FLIGHT_LAUNCH_ARG_IS_HOST]),
									  g_flightLaunchArgs.arguments[FLIGHT_LAUNCH_ARG_MP_GAME_NAME],
									  networkType,
									  atoi(g_flightLaunchArgs.arguments[FLIGHT_LAUNCH_ARG_NUM_PLAYERS]),
									  g_flightInProgressLaunch, connectionAddress);
}

/* Sets the distance scale for model detail levels from the level-of-detail setting, capped at its
 * maximum, bent by the configured curve and inverted; clears any forced detail level. */
static void XvtFlightEntry_ConfigureLodDistance(void) {
	int lodConfigValue;

	lodConfigValue = g_gameConfig.lod[NetSession_GetPlayerCount() > 1] + LOD_CONFIG_OFFSET;
	g_lodDistanceScale = (float)lodConfigValue;
	if (g_lodDistanceScale > g_lodConfigMaxValue) {
		g_lodDistanceScale = (float)LOD_CONFIG_MAX_VALUE;
	}
	g_lodDistanceScale = g_lodDistanceScale * g_lodConfigScaleFactor;
	g_lodDistanceScale = g_lodDistanceScale * g_lodConfigCurveDouble;
	if (g_lodDistanceScale > g_lodConfigCurveThreshold) {
		g_lodDistanceScale = g_lodConfigCurveThreshold / (g_lodConfigCurveDouble - g_lodDistanceScale);
	}
	g_forcedLodLevel = 0;
	g_lodDistanceScale = (float)LOD_SCALE_INVERSION_NUMERATOR / g_lodDistanceScale;
}

/* Turns mipmapping off when the mipmap setting is at its disabled value; otherwise turns it on and
 * sets the mip distance scale from the setting through the same curve and inversion as the
 * detail levels. */
static void XvtFlightEntry_ConfigureMipmaps(void) {
	int mipmapConfigOption;

	mipmapConfigOption = g_gameConfig.mipmap[NetSession_GetPlayerCount() > 1];
	if (mipmapConfigOption != MIPMAPPING_DISABLED_VALUE) {
		int64_t mipmapConfigValue;

		mipmapConfigValue = g_gameConfig.mipmap[NetSession_GetPlayerCount() > 1];
		g_mipLodScale = (float)mipmapConfigValue;
		g_mipLodScale = g_mipLodScale * g_mipmapConfigScaleFactor;
		g_mipLodScale = g_mipLodScale * g_lodConfigCurveDouble;
		if (g_mipLodScale > g_lodConfigCurveThreshold) {
			g_mipLodScale = g_lodConfigCurveThreshold / (g_lodConfigCurveDouble - g_mipLodScale);
		}
		g_mipmappingEnabled = 1;
		g_mipLodScale = (float)LOD_SCALE_INVERSION_NUMERATOR / g_mipLodScale;
	} else {
		g_mipmappingEnabled = 0;
	}
}

/* Sets the render size from the screen resolution setting and the window surface size from the
 * window size setting, then the render target width. */
static void XvtFlightEntry_ConfigureDisplaySize(void) {
	switch (g_gameConfig.screenRes[NetSession_GetPlayerCount() > 1]) {
		case DISPLAY_CONFIG_LOW:
			g_displayModeWidth = DISPLAY_WIDTH_LOW;
			g_displayModeHeight = DISPLAY_HEIGHT_LOW;
			break;
		case DISPLAY_CONFIG_MEDIUM:
			g_displayModeWidth = DISPLAY_WIDTH_MEDIUM;
			g_displayModeHeight = DISPLAY_HEIGHT_MEDIUM;
			break;
		default:
			g_displayModeWidth = DISPLAY_WIDTH_HIGH;
			g_displayModeHeight = DISPLAY_HEIGHT_HIGH;
			break;
	}
	switch (g_gameConfig.windowSize[NetSession_GetPlayerCount() > 1]) {
		case DISPLAY_CONFIG_LOW:
			g_surfaceWidth = DISPLAY_WIDTH_LOW;
			g_surfaceHeight = DISPLAY_HEIGHT_LOW;
			break;
		case DISPLAY_CONFIG_MEDIUM:
			g_surfaceWidth = WINDOW_WIDTH_MEDIUM;
			g_surfaceHeight = WINDOW_HEIGHT_MEDIUM;
			break;
		default:
			g_surfaceWidth = DISPLAY_WIDTH_HIGH;
			g_surfaceHeight = DISPLAY_HEIGHT_HIGH;
			break;
	}
	g_renderTargetWidth = g_displayModeWidth;
}

static void XvtFlightEntry_Configure(void) {
	int brightnessLimit;
	g_flightBrightnessScaleQ8 =
		(g_gameConfig.brightness[NetSession_GetPlayerCount() > 1] + BRIGHTNESS_CONFIG_OFFSET)
		<< BRIGHTNESS_CONFIG_SHIFT;
	brightnessLimit = BRIGHTNESS_SCALE_MIN;
	if ((unsigned int)g_flightBrightnessScaleQ8 < BRIGHTNESS_SCALE_MIN) {
		g_flightBrightnessScaleQ8 = brightnessLimit;
	} else {
		brightnessLimit = BRIGHTNESS_SCALE_MAX;
		if ((unsigned int)g_flightBrightnessScaleQ8 > BRIGHTNESS_SCALE_MAX) {
			g_flightBrightnessScaleQ8 = brightnessLimit;
		}
	}
	g_backdropsEnabled = g_gameConfig.backdrop[NetSession_GetPlayerCount() > 1];
	g_debrisEnabled = g_gameConfig.debris[NetSession_GetPlayerCount() > 1];
	switch (g_gameConfig.starDensity[NetSession_GetPlayerCount() > 1]) {
		case 0:
			g_starGridDivisor = STAR_DENSITY_HIGH;
			break;
		case 1:
			g_starGridDivisor = STAR_DENSITY_MEDIUM;
			break;
		case 2:
			g_starGridDivisor = STAR_DENSITY_LOW;
			break;
		default:
			break;
	}
	g_useHardware3D = g_gameConfig.use3dHardware[NetSession_GetPlayerCount() > 1];
	g_bilinearEnabled = g_gameConfig.bilinear[NetSession_GetPlayerCount() > 1];
	{
		int bppConfigValue;

		bppConfigValue = g_gameConfig.colorDepthChoice[NetSession_GetPlayerCount() > 1];
		switch (bppConfigValue) {
			case DISPLAY_CONFIG_LOW:
				g_flightBytesPerPixel = PALETTED_BYTES_PER_PIXEL;
				break;
			case DISPLAY_CONFIG_MEDIUM:
				g_flightBytesPerPixel = HIGH_COLOR_BYTES_PER_PIXEL;
				break;
			default:
				g_flightBytesPerPixel = PALETTED_BYTES_PER_PIXEL;
				break;
		}
	}
	NetSession_GetPlayerCount();
	XvtFlightEntry_ConfigureLodDistance();
	XvtFlightEntry_ConfigureMipmaps();
	switch (g_gameConfig.textureRes[NetSession_GetPlayerCount() > 1]) {
		case 0:
			g_textureResolutionLevel = 0;
			break;
		case 1:
			g_textureResolutionLevel = 1;
			break;
		default:
			g_textureResolutionLevel = 2;
			break;
	}
	{
		int localLightsEnabled;

		localLightsEnabled = g_gameConfig.localLights[NetSession_GetPlayerCount() > 1];
		g_localLightsEnabled = 1;
		if (localLightsEnabled == 0) {
			g_localLightsEnabled = 0;
		}
	}
	{
		int specularEnabled;

		specularEnabled = g_gameConfig.specular[NetSession_GetPlayerCount() > 1];
		g_specularEnabled = 1;
		if (specularEnabled == 0) {
			g_specularEnabled = 0;
		}
	}
	{
		int diffuseLightingEnabled;

		diffuseLightingEnabled = g_gameConfig.diffuse[NetSession_GetPlayerCount() > 1];
		g_dirLightingEnabled = 1;
		if (diffuseLightingEnabled == 0) {
			g_dirLightingEnabled = 0;
		}
	}
	{
		int ditheringEnabled;

		ditheringEnabled = g_gameConfig.dither[NetSession_GetPlayerCount() > 1];
		g_ditheringEnabled = 1;
		if (ditheringEnabled == 0) {
			g_ditheringEnabled = 0;
		}
	}
	XvtFlightEntry_ConfigureDisplaySize();
	g_requestedFlightBytesPerPixel = g_flightBytesPerPixel;
	g_requestedFlightHardware3D = g_useHardware3D;
}

int XvtFlightEntry_CreateDevices(void) {
	XvtFlightEntry_Configure();
	g_flightDevicesCreated = 1;
	if (FlightDisplay_Init() == 0) {
		return 0;
	}

	switch (g_displayModeWidth) {
		case DISPLAY_WIDTH_LOW:
			g_gameConfig.screenRes[NetSession_GetPlayerCount() > 1] = DISPLAY_CONFIG_LOW;
			break;
		case DISPLAY_WIDTH_MEDIUM:
			g_gameConfig.screenRes[NetSession_GetPlayerCount() > 1] = DISPLAY_CONFIG_MEDIUM;
			break;
		case DISPLAY_WIDTH_HIGH:
			g_gameConfig.screenRes[NetSession_GetPlayerCount() > 1] = DISPLAY_CONFIG_HIGH;
			break;
		default:
			break;
	}
	switch (g_flightBytesPerPixel) {
		case PALETTED_BYTES_PER_PIXEL:
			g_gameConfig.colorDepthChoice[NetSession_GetPlayerCount() > 1] = DISPLAY_CONFIG_LOW;
			break;
		case HIGH_COLOR_BYTES_PER_PIXEL:
			g_gameConfig.colorDepthChoice[NetSession_GetPlayerCount() > 1] = DISPLAY_CONFIG_MEDIUM;
			break;
		default:
			break;
	}
	g_gameConfig.use3dHardware[NetSession_GetPlayerCount() > 1] = (uint8_t)g_useHardware3D;
	DebugPrintf("Init Dinput\n");
	if (g_flightConfDirectInput != 0 && DInput_Init() == 0) {
		g_flightConfDirectInput = 0;
	}
	DebugPrintf("Init Dsound\n");
	g_flightSoundInitStartTimeMs = timeGetTime();
	g_soundEngineStarted = 1;
	if (Sound_Init_Sound_Engine(g_flightMainWindowHandle) == 0) {
		FlightDisplay_CleanupAndReportError(DISPLAY_INIT_SOUND_ERROR);
		return 0;
	}

	strcpy(g_currentMissionFile, g_flightLaunchArgs.arguments[FLIGHT_LAUNCH_ARG_MISSION_PATH]);
	return 1;
}

void XvtFlightEntry_Cleanup(void) {
	g_sw3dSkipOddScanlines = 0;
	if (g_soundEngineStarted)
		Sound_Shutdown_Sound_Engine();
	g_soundEngineStarted = 0;
	if (g_flightDevicesCreated) {
		DInput_Shutdown();
	}
	if (g_gameSessionStarted) {
		NetSession_Shutdown();
		XvtNetworkSession_EndFlight();
	}
	g_gameSessionStarted = 0;
	if (g_flightDevicesCreated && g_useHardware3D != 0) {
		std3D_DetachAndReleaseZBufferSurface();
		std3D_Close();
		std3D_Shutdown();
	}
	if (g_flightDevicesCreated && g_flightFullscreen != 0) {
		if (g_flightPrimarySurface)
			FlightDisplay_ClearSurface(g_flightPrimarySurface);
		if (g_flightPageFlip != 0) {
			if (g_flightBackBuffer)
				FlightDisplay_ClearSurface(g_flightBackBuffer);
			if (g_flightOffscreenSurface)
				FlightDisplay_ClearSurface(g_flightOffscreenSurface);
		}
	}
	while (FlightSurface_GetLockCount() > 0)
		FlightSurface_Unlock();
	if (g_flightBackBuffer) {
		g_flightBackBuffer->lpVtbl->Release(g_flightBackBuffer);
		g_flightBackBuffer = NULL;
	}
	if (g_flightPrimarySurface != NULL) {
		g_flightPrimarySurface->lpVtbl->Release(g_flightPrimarySurface);
		g_flightPrimarySurface = NULL;
	}
	if (g_flightPalette != NULL) {
		g_flightPalette->lpVtbl->Release(g_flightPalette);
		g_flightPalette = NULL;
	}
	if (g_flightPageFlip != 0 && g_flightOffscreenSurface != NULL) {
		g_flightOffscreenSurface->lpVtbl->Release(g_flightOffscreenSurface);
		g_flightOffscreenSurface = NULL;
	}
	g_flightRenderToFrontend = 1;
	g_useHardware3D = 0;
	g_flightDevicesCreated = 0;
	g_flightRenderSurface = NULL;
}
