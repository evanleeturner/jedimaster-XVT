/* Checks the --launch result file (xvt_runtime/runtime/launch_result.h)
 * against the promises in its header: the document each exit code makes, the
 * words for status and outcome, what does not fit, and the file written and
 * read back from a temporary folder. The module holds no state and reads no
 * game data. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "test_assert.h"
#include "xvt_runtime/runtime/launch_result.h"
#include "xvt_runtime/runtime/sith.h"

/* The folder for temporary folders: $TMPDIR, else the working folder. */
static const char *tmp_base(void)
{
	const char *base = getenv("TMPDIR");
	return base && base[0] ? base : ".";
}

static struct xvt_launch_result sample(int exit_code, int outcome)
{
	struct xvt_launch_result r;
	memset(&r, 0, sizeof(r));
	r.exit_code = exit_code;
	r.outcome = outcome;
	r.score = 120;
	r.kills = 4;
	r.losses = 1;
	r.goals_met_seconds = 305;
	return r;
}

static void check_flown(void)
{
	char text[1024];
	memset(text, 'x', sizeof(text));
	struct xvt_launch_result r =
		sample(XVT_LAUNCH_EXIT_FLOWN, XVT_LAUNCH_OUTCOME_COMPLETED);
	size_t length = xvt_launch_result_format(&r, text, sizeof(text));
	XVT_ASSERT_TRUE(length > 0);
	XVT_ASSERT_INT_EQ(length, strlen(text));
	XVT_ASSERT_INT_EQ(text[length - 1], '\n');
	XVT_ASSERT_TRUE(strstr(text, "\"schema_version\":3") != NULL);
	XVT_ASSERT_TRUE(strstr(text, "\"status\":\"flown\"") != NULL);
	XVT_ASSERT_TRUE(strstr(text, "\"exit_code\":0") != NULL);
	XVT_ASSERT_TRUE(strstr(text, "\"outcome\":\"completed\"") != NULL);
	XVT_ASSERT_TRUE(strstr(text, "\"score\":120") != NULL);
	XVT_ASSERT_TRUE(strstr(text, "\"kills\":4") != NULL);
	XVT_ASSERT_TRUE(strstr(text, "\"losses\":1") != NULL);
	XVT_ASSERT_TRUE(strstr(text, "\"goals_met_seconds\":305") != NULL);
	XVT_ASSERT_TRUE(strstr(text, "\"why\":\"\"") != NULL);
}

static void check_each_status_and_outcome(void)
{
	static const struct {
		int code;
		const char *word;
	} statuses[] = {{XVT_LAUNCH_EXIT_FLOWN, "flown"},
			{XVT_LAUNCH_EXIT_REFUSED, "refused"},
			{XVT_LAUNCH_EXIT_NOT_REACHED, "not_reached"},
			{XVT_LAUNCH_EXIT_FLIGHT_FAILED, "failed"}};
	static const char *const outcomes[] = {"none", "completed",
					       "not_completed"};
	char text[1024];
	char expected[64];
	for (size_t index = 0; index < 4; ++index) {
		struct xvt_launch_result r =
			sample(statuses[index].code, XVT_LAUNCH_OUTCOME_NONE);
		snprintf(r.why, sizeof(r.why), "Because \"quoted\" \\ text.");
		XVT_ASSERT_TRUE(
			xvt_launch_result_format(&r, text, sizeof(text)) > 0);
		snprintf(expected, sizeof(expected), "\"status\":\"%s\"",
			 statuses[index].word);
		XVT_ASSERT_TRUE(strstr(text, expected) != NULL);
		XVT_ASSERT_TRUE(
			strstr(text, "Because \\\"quoted\\\" \\\\ text.") !=
			NULL);
	}
	for (int outcome = 0; outcome < 3; ++outcome) {
		struct xvt_launch_result r =
			sample(XVT_LAUNCH_EXIT_FLOWN, outcome);
		XVT_ASSERT_TRUE(
			xvt_launch_result_format(&r, text, sizeof(text)) > 0);
		snprintf(expected, sizeof(expected), "\"outcome\":\"%s\"",
			 outcomes[outcome]);
		XVT_ASSERT_TRUE(strstr(text, expected) != NULL);
	}
}

static void check_refusals_to_format(void)
{
	char text[1024];
	struct xvt_launch_result r = sample(1, XVT_LAUNCH_OUTCOME_NONE);
	XVT_ASSERT_INT_EQ(xvt_launch_result_format(&r, text, sizeof(text)), 0);
	r = sample(2, XVT_LAUNCH_OUTCOME_NONE);
	XVT_ASSERT_INT_EQ(xvt_launch_result_format(&r, text, sizeof(text)), 0);
	r = sample(6, XVT_LAUNCH_OUTCOME_NONE);
	XVT_ASSERT_INT_EQ(xvt_launch_result_format(&r, text, sizeof(text)), 0);
	r = sample(-1, XVT_LAUNCH_OUTCOME_NONE);
	XVT_ASSERT_INT_EQ(xvt_launch_result_format(&r, text, sizeof(text)), 0);
	r = sample(XVT_LAUNCH_EXIT_FLOWN, 3);
	XVT_ASSERT_INT_EQ(xvt_launch_result_format(&r, text, sizeof(text)), 0);
	r = sample(XVT_LAUNCH_EXIT_FLOWN, -1);
	XVT_ASSERT_INT_EQ(xvt_launch_result_format(&r, text, sizeof(text)), 0);
	r = sample(XVT_LAUNCH_EXIT_FLOWN, XVT_LAUNCH_OUTCOME_COMPLETED);
	XVT_ASSERT_INT_EQ(xvt_launch_result_format(&r, text, 0), 0);
}

static void check_size_limit(void)
{
	char text[1024];
	struct xvt_launch_result r =
		sample(XVT_LAUNCH_EXIT_FLOWN, XVT_LAUNCH_OUTCOME_COMPLETED);
	size_t length = xvt_launch_result_format(&r, text, sizeof(text));
	char tight[1024];
	XVT_ASSERT_INT_EQ(xvt_launch_result_format(&r, tight, length + 1),
			  length);
	XVT_ASSERT_INT_EQ(xvt_launch_result_format(&r, tight, length), 0);
	XVT_ASSERT_INT_EQ(xvt_launch_result_format(&r, tight, 10), 0);
}

static struct xvt_launch_sith sith_sample(int setting)
{
	struct xvt_launch_sith s;
	memset(&s, 0, sizeof(s));
	s.setting = setting;
	s.flight_group = 2;
	snprintf(s.flight_name, sizeof(s.flight_name), "Beta \"One\" \\ x");
	s.craft_type = 7;
	s.apprentices_flown = 3;
	s.apprentices_lost = 2;
	s.lord_flown = 1;
	s.lord_lost = 1;
	return s;
}

/* The sith object, per setting: always every member; with none the members
 * hold their empty values whatever the struct holds. */
static void check_sith_block(void)
{
	char text[2048];
	struct xvt_launch_result r =
		sample(XVT_LAUNCH_EXIT_FLOWN, XVT_LAUNCH_OUTCOME_COMPLETED);
	XVT_ASSERT_TRUE(xvt_launch_result_format(&r, text, sizeof(text)) > 0);
	XVT_ASSERT_TRUE(
		strstr(text,
		       "\"sith\":{\"setting\":\"none\",\"flight_group\":-1,\"flight_name\":\"\",\"craft_type\":-1,\"apprentices_flown\":0,\"apprentices_lost\":0,\"lord_flown\":0,\"lord_lost\":0,\"why\":\"\"}") !=
		NULL);
	r.sith = sith_sample(XVT_SITH_NONE);
	snprintf(r.sith.why, sizeof(r.sith.why), "ignored");
	XVT_ASSERT_TRUE(xvt_launch_result_format(&r, text, sizeof(text)) > 0);
	XVT_ASSERT_TRUE(strstr(text, "\"flight_group\":-1,") != NULL);
	XVT_ASSERT_TRUE(strstr(text, "\"lord_lost\":0,\"why\":\"\"}") != NULL);

	r.sith = sith_sample(XVT_SITH_LORD);
	XVT_ASSERT_TRUE(xvt_launch_result_format(&r, text, sizeof(text)) > 0);
	XVT_ASSERT_TRUE(
		strstr(text,
		       "\"sith\":{\"setting\":\"lord\",\"flight_group\":2,\"flight_name\":\"Beta \\\"One\\\" \\\\ x\",\"craft_type\":7,\"apprentices_flown\":3,\"apprentices_lost\":2,\"lord_flown\":1,\"lord_lost\":1,\"why\":\"\"}") !=
		NULL);
	/* The sith object ends the document. */
	XVT_ASSERT_TRUE(strstr(text, "}}\n") != NULL);

	r.sith = sith_sample(XVT_SITH_APPRENTICES);
	XVT_ASSERT_TRUE(xvt_launch_result_format(&r, text, sizeof(text)) > 0);
	XVT_ASSERT_TRUE(strstr(text, "\"setting\":\"apprentices\"") != NULL);
}

/* Nothing promoted: the reason travels, the group and craft read -1. */
static void check_sith_none_promoted(void)
{
	char text[2048];
	struct xvt_launch_result r =
		sample(XVT_LAUNCH_EXIT_FLOWN, XVT_LAUNCH_OUTCOME_NOT_COMPLETED);
	memset(&r.sith, 0, sizeof(r.sith));
	r.sith.setting = XVT_SITH_LORD;
	r.sith.flight_group = -1;
	r.sith.craft_type = -1;
	snprintf(r.sith.why, sizeof(r.sith.why),
		 "none of those groups flies starfighters");
	XVT_ASSERT_TRUE(xvt_launch_result_format(&r, text, sizeof(text)) > 0);
	XVT_ASSERT_TRUE(
		strstr(text,
		       "\"sith\":{\"setting\":\"lord\",\"flight_group\":-1,\"flight_name\":\"\",\"craft_type\":-1,\"apprentices_flown\":0,\"apprentices_lost\":0,\"lord_flown\":0,\"lord_lost\":0,\"why\":\"none of those groups flies starfighters\"}") !=
		NULL);
}

/* A setting that is none of the three has no words. */
static void check_sith_refusals(void)
{
	char text[2048];
	struct xvt_launch_result r =
		sample(XVT_LAUNCH_EXIT_FLOWN, XVT_LAUNCH_OUTCOME_COMPLETED);
	r.sith.setting = 3;
	XVT_ASSERT_INT_EQ(xvt_launch_result_format(&r, text, sizeof(text)), 0);
	r.sith.setting = -1;
	XVT_ASSERT_INT_EQ(xvt_launch_result_format(&r, text, sizeof(text)), 0);
}

static void check_written_file(void)
{
	char folder[64];
	snprintf(folder, sizeof(folder), "%s/xvt_launch_result_XXXXXX",
		 tmp_base());
	XVT_ASSERT_TRUE(mkdtemp(folder) != NULL);
	char path[128];
	snprintf(path, sizeof(path), "%s/result.json", folder);
	struct xvt_launch_result r =
		sample(XVT_LAUNCH_EXIT_REFUSED, XVT_LAUNCH_OUTCOME_NONE);
	snprintf(r.why, sizeof(r.why), "The pilot is missing.");
	XVT_ASSERT_INT_EQ(xvt_launch_result_write(&r, path), 1);
	char expected[1024];
	char found[1024] = {0};
	XVT_ASSERT_TRUE(
		xvt_launch_result_format(&r, expected, sizeof(expected)) > 0);
	FILE *file = fopen(path, "rb");
	XVT_ASSERT_TRUE(file != NULL);
	size_t read = fread(found, 1, sizeof(found) - 1, file);
	fclose(file);
	XVT_ASSERT_INT_EQ(read, strlen(expected));
	XVT_ASSERT_INT_EQ(strcmp(found, expected), 0);
	/* A folder that is not there, and a number that has no words. */
	char missing[160];
	snprintf(missing, sizeof(missing), "%s/no/such/result.json", folder);
	XVT_ASSERT_INT_EQ(xvt_launch_result_write(&r, missing), 0);
	r.exit_code = 9;
	XVT_ASSERT_INT_EQ(xvt_launch_result_write(&r, path), 0);
	unlink(path);
	rmdir(folder);
}

int main(void)
{
	check_flown();
	check_each_status_and_outcome();
	check_refusals_to_format();
	check_size_limit();
	check_sith_block();
	check_sith_none_promoted();
	check_sith_refusals();
	check_written_file();
	return 0;
}
