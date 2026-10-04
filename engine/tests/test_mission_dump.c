#define _POSIX_C_SOURCE 200809L
/* Runs the mission dump tool (tools/mission_dump.c) on mission files this test writes itself, from the
 * layouts in xvt/flight/mission/mission.h, and checks the exit status and output its comment promises: no
 * game mission is read. The tool is built for this test as its own program, MISSION_DUMP_TOOL, with the
 * sanitizers on, and runs with its output in a fresh temporary folder. A sanitizer report in the tool
 * exits with status 86, which no check expects.
 *
 * Not run here: a close that fails, which a test cannot provoke on an ordinary file. */
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "test_assert.h"
#include "test_temp_folder.h"
#include "xvt/flight/mission/mission.h"

#ifndef MISSION_DUMP_TOOL
#error "MISSION_DUMP_TOOL must name the mission dump program to run"
#endif

static char g_folder[XVT_TEST_PATH_CAPACITY];

struct bytes {
	uint8_t *data;
	size_t size;
	size_t capacity;
};

/* Appends size bytes of data, or of zeros when data is NULL. */
static void put(struct bytes *bytes, const void *data, size_t size)
{
	if (bytes->size + size > bytes->capacity) {
		bytes->capacity = (bytes->size + size) * 2;
		bytes->data = realloc(bytes->data, bytes->capacity);
		XVT_ASSERT_TRUE(bytes->data != NULL);
	}
	if (data) {
		memcpy(bytes->data + bytes->size, data, size);
	} else {
		memset(bytes->data + bytes->size, 0, size);
	}
	bytes->size += size;
}

/* Appends a 16-bit little-endian word, as every multibyte field in a mission file is stored. */
static void put_word(struct bytes *bytes, unsigned value)
{
	uint8_t word[2] = {(uint8_t)value, (uint8_t)(value >> 8)};
	put(bytes, word, sizeof word);
}

/* Copies text into a fixed field of size bytes, which the caller has zeroed; no terminator when it fills
 * the field. */
static void set_text(char *field, size_t size, const char *text)
{
	if (text) {
		memcpy(field, text, strlen(text) < size ? strlen(text) : size);
	}
}

/* What a test mission holds. Every byte the fields do not name is zero. */
struct mission {
	unsigned version;
	unsigned groups;
	unsigned messages;
	unsigned message_index[65];
	unsigned team0_goals;
	int team0_record;
	const char *group_name;
	unsigned arrival_condition;
	const char *message_text;
	const char *goal_name;
	const char *team_name;
	const char *briefing_label;
	const char *override_text;
};

/* A small mission that names something in every part the tool prints. */
static struct mission plain(void)
{
	struct mission mission;
	memset(&mission, 0, sizeof mission);
	mission.version = 14;
	mission.groups = 1;
	mission.messages = 1;
	for (unsigned i = 0; i < 65; ++i) {
		mission.message_index[i] = i;
	}
	mission.team0_goals = 1;
	mission.team0_record = 1;
	mission.group_name = "Red";
	mission.message_text = "Hold position";
	mission.goal_name = "Survive";
	mission.team_name = "Rebels";
	mission.briefing_label = "Approach";
	mission.override_text = "Escort done";
	return mission;
}

/* An empty mission: no flight groups, messages, goals, teams or text. */
static struct mission empty(void)
{
	struct mission mission;
	memset(&mission, 0, sizeof mission);
	mission.version = 14;
	return mission;
}

/* Writes the mission's bytes into out, in the order the tool reads them. */
static void build(const struct mission *mission, struct bytes *out)
{
	out->size = 0;
	put_word(out, mission->version);

	uint8_t header[sizeof(struct mission_header)] = {0};
	header[offsetof(struct mission_header, num_flight_groups)] =
		(uint8_t)mission->groups;
	header[offsetof(struct mission_header, num_flight_groups) + 1] =
		(uint8_t)(mission->groups >> 8);
	header[offsetof(struct mission_header, num_messages)] =
		(uint8_t)mission->messages;
	header[offsetof(struct mission_header, num_messages) + 1] =
		(uint8_t)(mission->messages >> 8);
	put(out, header, sizeof header);

	for (unsigned i = 0; i < mission->groups; ++i) {
		struct xvt_flight_group group;
		memset(&group, 0, sizeof group);
		if (i == 0) {
			set_text(group.name, sizeof group.name,
				 mission->group_name);
			group.arrival_triggers[0].triggers[0].condition =
				(uint8_t)mission->arrival_condition;
		}
		put(out, &group, sizeof group);
	}

	for (unsigned i = 0; i < mission->messages; ++i) {
		struct mission_message message;
		memset(&message, 0, sizeof message);
		if (i == 0) {
			set_text(message.message, sizeof message.message,
				 mission->message_text);
		}
		put_word(out, mission->message_index[i]);
		put(out, &message, sizeof message);
	}

	for (unsigned team = 0; team < 10; ++team) {
		unsigned count = team == 0 ? mission->team0_goals : 0;
		put_word(out, count);
		for (unsigned j = 0; j < count; ++j) {
			struct global_goal goal;
			memset(&goal, 0, sizeof goal);
			if (j == 0) {
				set_text(goal.name, sizeof goal.name,
					 mission->goal_name);
			}
			put(out, &goal, sizeof goal);
		}
	}

	for (unsigned team = 0; team < 10; ++team) {
		int present = team == 0 && mission->team0_record;
		put_word(out, present ? 1 : 0);
		if (present) {
			struct team record;
			memset(&record, 0, sizeof record);
			set_text(record.name, sizeof record.name,
				 mission->team_name);
			put(out, &record, sizeof record);
		}
	}

	/* Eight briefings: an 820-byte script, then 32 labels and 32 texts, each a length and its bytes. */
	for (unsigned briefing = 0; briefing < 8; ++briefing) {
		put(out, NULL, 820);
		for (unsigned j = 0; j < 64; ++j) {
			const char *text = briefing == 0 && j == 0
						   ? mission->briefing_label
						   : NULL;
			put_word(out, text ? (unsigned)strlen(text) : 0);
			if (text) {
				put(out, text, strlen(text));
			}
		}
	}

	/* Goal text overrides: 8 goals per flight group, then 28 slots per team, 3 states of 64 bytes each. */
	for (unsigned i = 0; i < mission->groups + 10; ++i) {
		unsigned slots = i < mission->groups ? 8 : 28;
		for (unsigned j = 0; j < slots * 3; ++j) {
			char state[64] = {0};
			if (i == 0 && j == 0) {
				set_text(state, sizeof state,
					 mission->override_text);
			}
			put(out, state, sizeof state);
		}
	}
}

struct run {
	int status;
	char *out;
	size_t out_size;
	char *err;
};

/* Runs the tool with count arguments, its stdout and stderr in files in the folder, or its stdout on
 * /dev/full when to_full is set. Returns its exit status (-1 when it did not exit) and what it wrote. */
static struct run run_tool(int count, const char *const *args, int to_full)
{
	char out_path[XVT_TEST_PATH_CAPACITY];
	char err_path[XVT_TEST_PATH_CAPACITY];
	xvt_test_join(out_path, g_folder, "stdout.txt");
	xvt_test_join(err_path, g_folder, "stderr.txt");
	fflush(stdout);
	fflush(stderr);
	pid_t child = fork();
	XVT_ASSERT_TRUE(child >= 0);
	if (child == 0) {
		int out = open(to_full ? "/dev/full" : out_path,
			       O_WRONLY | O_CREAT | O_TRUNC, 0600);
		int err = open(err_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
		if (out < 0 || err < 0 || dup2(out, 1) < 0 ||
		    dup2(err, 2) < 0) {
			_exit(126);
		}
		char *argv[4] = {MISSION_DUMP_TOOL, NULL, NULL, NULL};
		for (int i = 0; i < count && i < 2; ++i) {
			argv[i + 1] = (char *)args[i];
		}
		execv(MISSION_DUMP_TOOL, argv);
		_exit(127);
	}
	int status;
	XVT_ASSERT_INT_EQ(waitpid(child, &status, 0), child);
	struct run run = {0};
	run.status = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
	if (!to_full) {
		run.out = xvt_test_read_file(g_folder, "stdout.txt",
					     &run.out_size);
	}
	run.err = xvt_test_read_file(g_folder, "stderr.txt", NULL);
	return run;
}

static void free_run(const struct run *run)
{
	free(run->out);
	free(run->err);
}

/* Writes bytes as the file name and runs the tool on it. */
static struct run dump_bytes(const char *name, const uint8_t *data, size_t size,
			     int to_full)
{
	char path[XVT_TEST_PATH_CAPACITY];
	xvt_test_write_file(g_folder, name, data, size);
	xvt_test_join(path, g_folder, name);
	const char *args[1] = {path};
	return run_tool(1, args, to_full);
}

/* Builds the mission, writes it and runs the tool on it. */
static struct run dump(const struct mission *mission)
{
	struct bytes bytes = {0};
	build(mission, &bytes);
	struct run run = dump_bytes("mission.tie", bytes.data, bytes.size, 0);
	free(bytes.data);
	return run;
}

/* Runs the tool on the mission and returns its exit status. A status of 1 with nothing on stderr comes
 * back as -2, so a refusal without a message fails a check that expects 1. */
static int outcome(const struct mission *mission)
{
	struct run run = dump(mission);
	int status = run.status == 1 && run.err[0] == 0 ? -2 : run.status;
	free_run(&run);
	return status;
}

/* The last line of text, without its newline, copied into line. */
static void last_line(const char *text, char *line, size_t capacity)
{
	size_t length = strlen(text);
	while (length > 0 && text[length - 1] == '\n') {
		--length;
	}
	size_t start = length;
	while (start > 0 && text[start - 1] != '\n') {
		--start;
	}
	XVT_ASSERT_TRUE(length - start < capacity);
	memcpy(line, text + start, length - start);
	line[length - start] = 0;
}

static void check_arguments(void)
{
	struct run run = run_tool(0, NULL, 0);
	XVT_ASSERT_INT_EQ(run.status, 2);
	free_run(&run);
	const char *two[2] = {"a.tie", "b.tie"};
	run = run_tool(2, two, 0);
	XVT_ASSERT_INT_EQ(run.status, 2);
	free_run(&run);

	char missing[XVT_TEST_PATH_CAPACITY];
	xvt_test_join(missing, g_folder, "missing.tie");
	const char *one[1] = {missing};
	run = run_tool(1, one, 0);
	XVT_ASSERT_INT_EQ(run.status, 1);
	XVT_ASSERT_TRUE(run.err[0] != 0);
	free_run(&run);
}

static void check_prints_every_part(void)
{
	struct mission mission = plain();
	struct run run = dump(&mission);
	XVT_ASSERT_INT_EQ(run.status, 0);
	/* The version line comes first. */
	char first[256];
	size_t length = strcspn(run.out, "\n");
	XVT_ASSERT_TRUE(length < sizeof first);
	memcpy(first, run.out, length);
	first[length] = 0;
	XVT_ASSERT_TRUE(strstr(first, "14") != NULL);
	/* Each part the comment lists, its strings quoted. */
	XVT_ASSERT_TRUE(strstr(run.out, "\"Red\"") != NULL);
	XVT_ASSERT_TRUE(strstr(run.out, "\"Hold position\"") != NULL);
	XVT_ASSERT_TRUE(strstr(run.out, "\"Survive\"") != NULL);
	XVT_ASSERT_TRUE(strstr(run.out, "\"Rebels\"") != NULL);
	XVT_ASSERT_TRUE(strstr(run.out, "\"Approach\"") != NULL);
	XVT_ASSERT_TRUE(strstr(run.out, "\"Escort done\"") != NULL);
	/* Every value in this mission is inside the name tables. */
	XVT_ASSERT_TRUE(strstr(run.out, "Unknown") == NULL);
	free_run(&run);
}

static void check_versions(void)
{
	struct mission mission = plain();
	static const unsigned accepted[] = {12, 13, 14};
	for (size_t i = 0; i < sizeof accepted / sizeof accepted[0]; ++i) {
		mission.version = accepted[i];
		XVT_ASSERT_INT_EQ(outcome(&mission), 0);
	}
	mission.version = 11;
	XVT_ASSERT_INT_EQ(outcome(&mission), 1);
	mission.version = 15;
	XVT_ASSERT_INT_EQ(outcome(&mission), 1);
}

static void check_counts(void)
{
	struct mission mission = plain();
	mission.groups = 48;
	XVT_ASSERT_INT_EQ(outcome(&mission), 0);
	mission.groups = 49;
	XVT_ASSERT_INT_EQ(outcome(&mission), 1);

	/* 64 messages, written in reverse index order, are accepted; 65 are refused. */
	mission = plain();
	mission.messages = 64;
	for (unsigned i = 0; i < 64; ++i) {
		mission.message_index[i] = 63 - i;
	}
	XVT_ASSERT_INT_EQ(outcome(&mission), 0);
	mission.messages = 65;
	XVT_ASSERT_INT_EQ(outcome(&mission), 1);
}

static void check_message_indices(void)
{
	struct mission mission = plain();
	mission.messages = 2;
	mission.message_index[0] = 3;
	mission.message_index[1] = 3;
	XVT_ASSERT_INT_EQ(outcome(&mission), 1);
	mission.messages = 1;
	mission.message_index[0] = 64;
	XVT_ASSERT_INT_EQ(outcome(&mission), 1);
	mission.message_index[0] = 63;
	XVT_ASSERT_INT_EQ(outcome(&mission), 0);
}

static void check_global_goals(void)
{
	struct mission mission = plain();
	mission.team0_goals = 7;
	XVT_ASSERT_INT_EQ(outcome(&mission), 0);
	mission.team0_goals = 8;
	XVT_ASSERT_INT_EQ(outcome(&mission), 1);
}

static void check_short_reads(void)
{
	struct mission mission = plain();
	struct bytes bytes = {0};
	build(&mission, &bytes);
	const size_t cuts[] = {0,
			       1,
			       2 + 10,
			       2 + sizeof(struct mission_header) + 100,
			       bytes.size / 2,
			       bytes.size - 1};
	for (size_t i = 0; i < sizeof cuts / sizeof cuts[0]; ++i) {
		struct run run =
			dump_bytes("short.tie", bytes.data, cuts[i], 0);
		XVT_ASSERT_INT_EQ(run.status, 1);
		XVT_ASSERT_TRUE(run.err[0] != 0);
		free_run(&run);
	}
	free(bytes.data);
}

static void check_unknown_names(void)
{
	/* A trigger condition past the end of the condition table prints as "Unknown". */
	struct mission mission = plain();
	mission.arrival_condition = 200;
	struct run run = dump(&mission);
	XVT_ASSERT_INT_EQ(run.status, 0);
	XVT_ASSERT_TRUE(strstr(run.out, "Unknown") != NULL);
	free_run(&run);
}

static void check_quoting(void)
{
	/* A quote, a backslash and a newline print as C escapes; an escape character never prints raw. */
	struct mission mission = plain();
	mission.group_name = "A\"B\\C\n\x1b";
	struct run run = dump(&mission);
	XVT_ASSERT_INT_EQ(run.status, 0);
	XVT_ASSERT_TRUE(strstr(run.out, "\"A\\\"B\\\\C\\n") != NULL);
	XVT_ASSERT_TRUE(memchr(run.out, 0x1b, run.out_size) == NULL);
	free_run(&run);
}

static void check_consumed_offset(void)
{
	struct mission mission = plain();
	struct bytes bytes = {0};
	build(&mission, &bytes);
	struct run run = dump_bytes("mission.tie", bytes.data, bytes.size, 0);
	XVT_ASSERT_INT_EQ(run.status, 0);
	char last[256];
	last_line(run.out, last, sizeof last);
	free_run(&run);

	/* The last line gives how far the file was read: here, all of it. */
	char decimal[32];
	char hex[32];
	snprintf(decimal, sizeof decimal, "%zu", bytes.size);
	snprintf(hex, sizeof hex, "%zx", bytes.size);
	XVT_ASSERT_TRUE(strstr(last, decimal) != NULL ||
			strstr(last, hex) != NULL);

	/* Bytes after the consumed part do not change it. */
	put(&bytes, "trailing editor data", 20);
	run = dump_bytes("trailer.tie", bytes.data, bytes.size, 0);
	XVT_ASSERT_INT_EQ(run.status, 0);
	char again[256];
	last_line(run.out, again, sizeof again);
	XVT_ASSERT_INT_EQ(strcmp(again, last), 0);
	free_run(&run);

	/* A longer briefing label moves it. */
	mission.briefing_label = "Approach slowly";
	run = dump(&mission);
	XVT_ASSERT_INT_EQ(run.status, 0);
	last_line(run.out, again, sizeof again);
	XVT_ASSERT_TRUE(strcmp(again, last) != 0);
	free_run(&run);
	free(bytes.data);
}

static void check_flush_failure(void)
{
	/* Output that cannot be written fails the run, where the system has a device that refuses writes. */
	if (access("/dev/full", W_OK) != 0) {
		return;
	}
	struct mission mission = empty();
	struct bytes bytes = {0};
	build(&mission, &bytes);
	struct run run = dump_bytes("empty.tie", bytes.data, bytes.size, 0);
	XVT_ASSERT_INT_EQ(run.status, 0);
	free_run(&run);
	run = dump_bytes("empty.tie", bytes.data, bytes.size, 1);
	XVT_ASSERT_INT_EQ(run.status, 1);
	free_run(&run);
	free(bytes.data);
}

/* Adds option to the sanitizer options variable name, which the tool inherits. */
static void add_option(const char *name, const char *option)
{
	const char *old = getenv(name);
	char value[1024];
	int length = snprintf(value, sizeof value, "%s%s%s", old ? old : "",
			      old && old[0] ? ":" : "", option);
	XVT_ASSERT_TRUE(length > 0 && (size_t)length < sizeof value);
	XVT_ASSERT_INT_EQ(setenv(name, value, 1), 0);
}

int main(void)
{
	add_option("ASAN_OPTIONS", "exitcode=86");
	add_option("UBSAN_OPTIONS", "exitcode=86");
	xvt_test_make_folder(g_folder);

	check_arguments();
	check_prints_every_part();
	check_versions();
	check_counts();
	check_message_indices();
	check_global_goals();
	check_short_reads();
	check_unknown_names();
	check_quoting();
	check_consumed_offset();
	check_flush_failure();

	xvt_test_remove_tree(g_folder);
	return 0;
}
