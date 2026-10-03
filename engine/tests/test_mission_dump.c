#define _POSIX_C_SOURCE 200809L
/* Runs the mission dump tool (tools/mission_dump.c) on mission files this test writes itself, from the
 * layouts in xvt/flight/mission/mission.h, and checks the exit status and output its comment promises: no
 * game mission is read. The tool is built for this test as its own program, MISSION_DUMP_TOOL, with the
 * sanitizers on, and runs with its output in a fresh temporary folder. A sanitizer report in the tool
 * exits with status 86, which no check expects.
 *
 * Not run here: a close that fails, which a test cannot provoke on an ordinary file. */
#include "test_assert.h"
#include "test_temp_folder.h"
#include "xvt/flight/mission/mission.h"

#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef MISSION_DUMP_TOOL
#error "MISSION_DUMP_TOOL must name the mission dump program to run"
#endif

static char g_folder[XVT_TEST_PATH_CAPACITY];

struct Bytes {
	uint8_t *data;
	size_t size;
	size_t capacity;
};

/* Appends size bytes of data, or of zeros when data is NULL. */
static void Put(struct Bytes *bytes, const void *data, size_t size)
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
static void PutWord(struct Bytes *bytes, unsigned value)
{
	uint8_t word[2] = {(uint8_t)value, (uint8_t)(value >> 8)};
	Put(bytes, word, sizeof word);
}

/* Copies text into a fixed field of size bytes, which the caller has zeroed; no terminator when it fills
 * the field. */
static void SetText(char *field, size_t size, const char *text)
{
	if (text) {
		memcpy(field, text, strlen(text) < size ? strlen(text) : size);
	}
}

/* What a test mission holds. Every byte the fields do not name is zero. */
struct Mission {
	unsigned version;
	unsigned groups;
	unsigned messages;
	unsigned messageIndex[65];
	unsigned team0Goals;
	int team0Record;
	const char *groupName;
	unsigned arrivalCondition;
	const char *messageText;
	const char *goalName;
	const char *teamName;
	const char *briefingLabel;
	const char *overrideText;
};

/* A small mission that names something in every part the tool prints. */
static struct Mission Plain(void)
{
	struct Mission mission;
	memset(&mission, 0, sizeof mission);
	mission.version = 14;
	mission.groups = 1;
	mission.messages = 1;
	for (unsigned i = 0; i < 65; ++i) {
		mission.messageIndex[i] = i;
	}
	mission.team0Goals = 1;
	mission.team0Record = 1;
	mission.groupName = "Red";
	mission.messageText = "Hold position";
	mission.goalName = "Survive";
	mission.teamName = "Rebels";
	mission.briefingLabel = "Approach";
	mission.overrideText = "Escort done";
	return mission;
}

/* An empty mission: no flight groups, messages, goals, teams or text. */
static struct Mission Empty(void)
{
	struct Mission mission;
	memset(&mission, 0, sizeof mission);
	mission.version = 14;
	return mission;
}

/* Writes the mission's bytes into out, in the order the tool reads them. */
static void Build(const struct Mission *mission, struct Bytes *out)
{
	out->size = 0;
	PutWord(out, mission->version);

	uint8_t header[sizeof(struct MissionHeader)] = {0};
	header[offsetof(struct MissionHeader, numFlightGroups)] =
		(uint8_t)mission->groups;
	header[offsetof(struct MissionHeader, numFlightGroups) + 1] =
		(uint8_t)(mission->groups >> 8);
	header[offsetof(struct MissionHeader, numMessages)] =
		(uint8_t)mission->messages;
	header[offsetof(struct MissionHeader, numMessages) + 1] =
		(uint8_t)(mission->messages >> 8);
	Put(out, header, sizeof header);

	for (unsigned i = 0; i < mission->groups; ++i) {
		struct XvtFlightGroup group;
		memset(&group, 0, sizeof group);
		if (i == 0) {
			SetText(group.name, sizeof group.name,
				mission->groupName);
			group.arrivalTriggers[0].triggers[0].condition =
				(uint8_t)mission->arrivalCondition;
		}
		Put(out, &group, sizeof group);
	}

	for (unsigned i = 0; i < mission->messages; ++i) {
		struct MissionMessage message;
		memset(&message, 0, sizeof message);
		if (i == 0) {
			SetText(message.message, sizeof message.message,
				mission->messageText);
		}
		PutWord(out, mission->messageIndex[i]);
		Put(out, &message, sizeof message);
	}

	for (unsigned team = 0; team < 10; ++team) {
		unsigned count = team == 0 ? mission->team0Goals : 0;
		PutWord(out, count);
		for (unsigned j = 0; j < count; ++j) {
			struct GlobalGoal goal;
			memset(&goal, 0, sizeof goal);
			if (j == 0) {
				SetText(goal.name, sizeof goal.name,
					mission->goalName);
			}
			Put(out, &goal, sizeof goal);
		}
	}

	for (unsigned team = 0; team < 10; ++team) {
		int present = team == 0 && mission->team0Record;
		PutWord(out, present ? 1 : 0);
		if (present) {
			struct Team record;
			memset(&record, 0, sizeof record);
			SetText(record.name, sizeof record.name,
				mission->teamName);
			Put(out, &record, sizeof record);
		}
	}

	/* Eight briefings: an 820-byte script, then 32 labels and 32 texts, each a length and its bytes. */
	for (unsigned briefing = 0; briefing < 8; ++briefing) {
		Put(out, NULL, 820);
		for (unsigned j = 0; j < 64; ++j) {
			const char *text = briefing == 0 && j == 0
						   ? mission->briefingLabel
						   : NULL;
			PutWord(out, text ? (unsigned)strlen(text) : 0);
			if (text) {
				Put(out, text, strlen(text));
			}
		}
	}

	/* Goal text overrides: 8 goals per flight group, then 28 slots per team, 3 states of 64 bytes each. */
	for (unsigned i = 0; i < mission->groups + 10; ++i) {
		unsigned slots = i < mission->groups ? 8 : 28;
		for (unsigned j = 0; j < slots * 3; ++j) {
			char state[64] = {0};
			if (i == 0 && j == 0) {
				SetText(state, sizeof state,
					mission->overrideText);
			}
			Put(out, state, sizeof state);
		}
	}
}

struct Run {
	int status;
	char *out;
	size_t outSize;
	char *err;
};

/* Runs the tool with count arguments, its stdout and stderr in files in the folder, or its stdout on
 * /dev/full when toFull is set. Returns its exit status (-1 when it did not exit) and what it wrote. */
static struct Run RunTool(int count, const char *const *args, int toFull)
{
	char outPath[XVT_TEST_PATH_CAPACITY], errPath[XVT_TEST_PATH_CAPACITY];
	XvtTest_Join(outPath, g_folder, "stdout.txt");
	XvtTest_Join(errPath, g_folder, "stderr.txt");
	fflush(stdout);
	fflush(stderr);
	pid_t child = fork();
	XVT_ASSERT_TRUE(child >= 0);
	if (child == 0) {
		int out = open(toFull ? "/dev/full" : outPath,
			       O_WRONLY | O_CREAT | O_TRUNC, 0600);
		int err = open(errPath, O_WRONLY | O_CREAT | O_TRUNC, 0600);
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
	struct Run run = {0};
	run.status = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
	if (!toFull) {
		run.out =
			XvtTest_ReadFile(g_folder, "stdout.txt", &run.outSize);
	}
	run.err = XvtTest_ReadFile(g_folder, "stderr.txt", NULL);
	return run;
}

static void FreeRun(struct Run *run)
{
	free(run->out);
	free(run->err);
}

/* Writes bytes as the file name and runs the tool on it. */
static struct Run DumpBytes(const char *name, const uint8_t *data, size_t size,
			    int toFull)
{
	char path[XVT_TEST_PATH_CAPACITY];
	XvtTest_WriteFile(g_folder, name, data, size);
	XvtTest_Join(path, g_folder, name);
	const char *args[1] = {path};
	return RunTool(1, args, toFull);
}

/* Builds the mission, writes it and runs the tool on it. */
static struct Run Dump(const struct Mission *mission)
{
	struct Bytes bytes = {0};
	Build(mission, &bytes);
	struct Run run = DumpBytes("mission.tie", bytes.data, bytes.size, 0);
	free(bytes.data);
	return run;
}

/* Runs the tool on the mission and returns its exit status. A status of 1 with nothing on stderr comes
 * back as -2, so a refusal without a message fails a check that expects 1. */
static int Outcome(const struct Mission *mission)
{
	struct Run run = Dump(mission);
	int status = run.status == 1 && run.err[0] == 0 ? -2 : run.status;
	FreeRun(&run);
	return status;
}

/* The last line of text, without its newline, copied into line. */
static void LastLine(const char *text, char *line, size_t capacity)
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

static void CheckArguments(void)
{
	struct Run run = RunTool(0, NULL, 0);
	XVT_ASSERT_INT_EQ(run.status, 2);
	FreeRun(&run);
	const char *two[2] = {"a.tie", "b.tie"};
	run = RunTool(2, two, 0);
	XVT_ASSERT_INT_EQ(run.status, 2);
	FreeRun(&run);

	char missing[XVT_TEST_PATH_CAPACITY];
	XvtTest_Join(missing, g_folder, "missing.tie");
	const char *one[1] = {missing};
	run = RunTool(1, one, 0);
	XVT_ASSERT_INT_EQ(run.status, 1);
	XVT_ASSERT_TRUE(run.err[0] != 0);
	FreeRun(&run);
}

static void CheckPrintsEveryPart(void)
{
	struct Mission mission = Plain();
	struct Run run = Dump(&mission);
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
	FreeRun(&run);
}

static void CheckVersions(void)
{
	struct Mission mission = Plain();
	static const unsigned accepted[] = {12, 13, 14};
	for (size_t i = 0; i < sizeof accepted / sizeof accepted[0]; ++i) {
		mission.version = accepted[i];
		XVT_ASSERT_INT_EQ(Outcome(&mission), 0);
	}
	mission.version = 11;
	XVT_ASSERT_INT_EQ(Outcome(&mission), 1);
	mission.version = 15;
	XVT_ASSERT_INT_EQ(Outcome(&mission), 1);
}

static void CheckCounts(void)
{
	struct Mission mission = Plain();
	mission.groups = 48;
	XVT_ASSERT_INT_EQ(Outcome(&mission), 0);
	mission.groups = 49;
	XVT_ASSERT_INT_EQ(Outcome(&mission), 1);

	/* 64 messages, written in reverse index order, are accepted; 65 are refused. */
	mission = Plain();
	mission.messages = 64;
	for (unsigned i = 0; i < 64; ++i) {
		mission.messageIndex[i] = 63 - i;
	}
	XVT_ASSERT_INT_EQ(Outcome(&mission), 0);
	mission.messages = 65;
	XVT_ASSERT_INT_EQ(Outcome(&mission), 1);
}

static void CheckMessageIndices(void)
{
	struct Mission mission = Plain();
	mission.messages = 2;
	mission.messageIndex[0] = 3;
	mission.messageIndex[1] = 3;
	XVT_ASSERT_INT_EQ(Outcome(&mission), 1);
	mission.messages = 1;
	mission.messageIndex[0] = 64;
	XVT_ASSERT_INT_EQ(Outcome(&mission), 1);
	mission.messageIndex[0] = 63;
	XVT_ASSERT_INT_EQ(Outcome(&mission), 0);
}

static void CheckGlobalGoals(void)
{
	struct Mission mission = Plain();
	mission.team0Goals = 7;
	XVT_ASSERT_INT_EQ(Outcome(&mission), 0);
	mission.team0Goals = 8;
	XVT_ASSERT_INT_EQ(Outcome(&mission), 1);
}

static void CheckShortReads(void)
{
	struct Mission mission = Plain();
	struct Bytes bytes = {0};
	Build(&mission, &bytes);
	const size_t cuts[] = {0,
			       1,
			       2 + 10,
			       2 + sizeof(struct MissionHeader) + 100,
			       bytes.size / 2,
			       bytes.size - 1};
	for (size_t i = 0; i < sizeof cuts / sizeof cuts[0]; ++i) {
		struct Run run = DumpBytes("short.tie", bytes.data, cuts[i], 0);
		XVT_ASSERT_INT_EQ(run.status, 1);
		XVT_ASSERT_TRUE(run.err[0] != 0);
		FreeRun(&run);
	}
	free(bytes.data);
}

static void CheckUnknownNames(void)
{
	/* A trigger condition past the end of the condition table prints as "Unknown". */
	struct Mission mission = Plain();
	mission.arrivalCondition = 200;
	struct Run run = Dump(&mission);
	XVT_ASSERT_INT_EQ(run.status, 0);
	XVT_ASSERT_TRUE(strstr(run.out, "Unknown") != NULL);
	FreeRun(&run);
}

static void CheckQuoting(void)
{
	/* A quote, a backslash and a newline print as C escapes; an escape character never prints raw. */
	struct Mission mission = Plain();
	mission.groupName = "A\"B\\C\n\x1b";
	struct Run run = Dump(&mission);
	XVT_ASSERT_INT_EQ(run.status, 0);
	XVT_ASSERT_TRUE(strstr(run.out, "\"A\\\"B\\\\C\\n") != NULL);
	XVT_ASSERT_TRUE(memchr(run.out, 0x1b, run.outSize) == NULL);
	FreeRun(&run);
}

static void CheckConsumedOffset(void)
{
	struct Mission mission = Plain();
	struct Bytes bytes = {0};
	Build(&mission, &bytes);
	struct Run run = DumpBytes("mission.tie", bytes.data, bytes.size, 0);
	XVT_ASSERT_INT_EQ(run.status, 0);
	char last[256];
	LastLine(run.out, last, sizeof last);
	FreeRun(&run);

	/* The last line gives how far the file was read: here, all of it. */
	char decimal[32], hex[32];
	snprintf(decimal, sizeof decimal, "%zu", bytes.size);
	snprintf(hex, sizeof hex, "%zx", bytes.size);
	XVT_ASSERT_TRUE(strstr(last, decimal) != NULL ||
			strstr(last, hex) != NULL);

	/* Bytes after the consumed part do not change it. */
	Put(&bytes, "trailing editor data", 20);
	run = DumpBytes("trailer.tie", bytes.data, bytes.size, 0);
	XVT_ASSERT_INT_EQ(run.status, 0);
	char again[256];
	LastLine(run.out, again, sizeof again);
	XVT_ASSERT_INT_EQ(strcmp(again, last), 0);
	FreeRun(&run);

	/* A longer briefing label moves it. */
	mission.briefingLabel = "Approach slowly";
	run = Dump(&mission);
	XVT_ASSERT_INT_EQ(run.status, 0);
	LastLine(run.out, again, sizeof again);
	XVT_ASSERT_TRUE(strcmp(again, last) != 0);
	FreeRun(&run);
	free(bytes.data);
}

static void CheckFlushFailure(void)
{
	/* Output that cannot be written fails the run, where the system has a device that refuses writes. */
	if (access("/dev/full", W_OK) != 0) {
		return;
	}
	struct Mission mission = Empty();
	struct Bytes bytes = {0};
	Build(&mission, &bytes);
	struct Run run = DumpBytes("empty.tie", bytes.data, bytes.size, 0);
	XVT_ASSERT_INT_EQ(run.status, 0);
	FreeRun(&run);
	run = DumpBytes("empty.tie", bytes.data, bytes.size, 1);
	XVT_ASSERT_INT_EQ(run.status, 1);
	FreeRun(&run);
	free(bytes.data);
}

/* Adds option to the sanitizer options variable name, which the tool inherits. */
static void AddOption(const char *name, const char *option)
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
	AddOption("ASAN_OPTIONS", "exitcode=86");
	AddOption("UBSAN_OPTIONS", "exitcode=86");
	XvtTest_MakeFolder(g_folder);

	CheckArguments();
	CheckPrintsEveryPart();
	CheckVersions();
	CheckCounts();
	CheckMessageIndices();
	CheckGlobalGoals();
	CheckShortReads();
	CheckUnknownNames();
	CheckQuoting();
	CheckConsumedOffset();
	CheckFlushFailure();

	XvtTest_RemoveTree(g_folder);
	return 0;
}
