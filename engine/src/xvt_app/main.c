#include "aeron/main.h"

#include <stdio.h>

#include "xvt_app/application.h"
#include "xvt_app/log_sink.h"
#include "xvt_runtime/ai/ai_wire.h"

int main(int argc, char *argv[])
{
	struct xvt_launch_options options;
	int valid = xvt_launch_options_parse(argc, argv, &options);
	if (valid && options.show_version) {
		/* Two lines, no window, no config, no log file: the launcher reads
		 * the second to check the game and its AI program match. */
		printf("OpenXvT " OPENXVT_VERSION "\nai_wire=%d\n",
		       (int)AI_WIRE_VERSION);
		return 0;
	}
	if (!valid || options.show_help) {
		fprintf(valid ? stdout : stderr,
			"OpenXvT " OPENXVT_VERSION "\n"
			"Usage: OpenXvT [options]\n"
			"  --game-data <directory>     XvT root containing BalanceOfPower\n"
			"  --setup                     Choose and remember an installation\n"
			"  --skip-intro                Start at the pilot concourse\n"
			"  --save-config               Persist the supplied game-data path\n"
			"  --reset-config              Explicitly replace config.yaml with defaults\n"
			"  --import-config <path>      Import a config.cfg/config2.cfg relative to XvT root\n"
			"  --import-pilot <path>       Copy a selected .plt/.pl2 and companion into USER\n"
			"  --pilot-name <basename>     Resolve an import filename collision\n"
			"  --check-installation       Validate/setup from CLI and exit without a window\n"
			"  --resource-root <directory> Override packaged resources\n"
			"  --log-level <level>         Write log lines at debug, info, warn or error (default info)\n"
			"  --log-file <path>           Append log lines to this file (default: a new file per run\n"
			"                              in logs/ beside config.yaml; the newest 10 are kept)\n"
			"  --launch <file>             Fly one single-player battle described by a JSON file, then write its result file and quit\n"
			"  --record-launch <file>      Write the state a battle starts from to <file> and <file>.pilot, for comparing starts\n"
			"  --ai-python <path>          The Python that runs the AI program, given with --ai-program\n"
			"  --ai-program <folder>       The AI program every flight not started by --launch runs;\n"
			"                              with --save-config both are kept in config.yaml\n"
			"  --version                   Show the version and the AI version this game speaks\n"
			"  --help                      Show this help\n");
		return valid ? 0 : 2;
	}
	int exit_code = xvt_application_run(&options);
	xvt_log_sink_finish(exit_code);
	return exit_code;
}
