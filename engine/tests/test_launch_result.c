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
	r.flight_seconds = 305;
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
	XVT_ASSERT_TRUE(strstr(text, "\"schema_version\":1") != NULL);
	XVT_ASSERT_TRUE(strstr(text, "\"status\":\"flown\"") != NULL);
	XVT_ASSERT_TRUE(strstr(text, "\"exit_code\":0") != NULL);
	XVT_ASSERT_TRUE(strstr(text, "\"outcome\":\"completed\"") != NULL);
	XVT_ASSERT_TRUE(strstr(text, "\"score\":120") != NULL);
	XVT_ASSERT_TRUE(strstr(text, "\"kills\":4") != NULL);
	XVT_ASSERT_TRUE(strstr(text, "\"losses\":1") != NULL);
	XVT_ASSERT_TRUE(strstr(text, "\"flight_seconds\":305") != NULL);
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
	check_written_file();
	return 0;
}
