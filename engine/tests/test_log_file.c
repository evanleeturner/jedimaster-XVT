/* Checks the run log file module (xvt_app/log_file.h) against the promises in its header: the run name's
 * shape, which old files a folder keeps, how a log's end shows the way its run ended, and the append-only
 * handle every line goes through, including two threads writing at once. The file checks run in a temporary
 * folder; nothing is written elsewhere. */
#define _POSIX_C_SOURCE 200809L

#include <pthread.h>

#include "test_temp_folder.h"
#include "xvt_app/log_file.h"

#define THREAD_LINES 2000

static void check_format_name(void)
{
	char name[64];
	XVT_ASSERT_INT_EQ(xvt_log_file_format_name(name, sizeof name, 2026, 10,
						   1, 20, 51, 53, 0x1a2b3c4du),
			  1);
	XVT_ASSERT_INT_EQ(strcmp(name, "openxvt-20261001-205153-1a2b3c4d.log"),
			  0);
	XVT_ASSERT_INT_EQ(strlen(name) + 1, XVT_LOG_FILE_NAME_CAPACITY);
	XVT_ASSERT_INT_EQ(xvt_log_file_format_name(name, sizeof name, 7, 1, 2,
						   3, 4, 5, 0x2au),
			  1);
	XVT_ASSERT_INT_EQ(strcmp(name, "openxvt-00070102-030405-0000002a.log"),
			  0);
	XVT_ASSERT_INT_EQ(xvt_log_file_format_name(name,
						   XVT_LOG_FILE_NAME_CAPACITY,
						   9999, 12, 31, 23, 59, 59, 0),
			  1);

	/* Out-of-range fields and a short buffer write "" and return 0. */
	int bad[][6] = {{10000, 1, 1, 0, 0, 0}, {-1, 1, 1, 0, 0, 0},
			{2026, 0, 1, 0, 0, 0},	{2026, 13, 1, 0, 0, 0},
			{2026, 1, 0, 0, 0, 0},	{2026, 1, 32, 0, 0, 0},
			{2026, 1, 1, 24, 0, 0}, {2026, 1, 1, 0, 60, 0},
			{2026, 1, 1, 0, 0, 60}};
	for (size_t i = 0; i < sizeof bad / sizeof bad[0]; ++i) {
		strcpy(name, "x");
		XVT_ASSERT_INT_EQ(xvt_log_file_format_name(
					  name, sizeof name, bad[i][0],
					  bad[i][1], bad[i][2], bad[i][3],
					  bad[i][4], bad[i][5], 1),
				  0);
		XVT_ASSERT_INT_EQ(name[0], 0);
	}
	strcpy(name, "x");
	XVT_ASSERT_INT_EQ(
		xvt_log_file_format_name(name, XVT_LOG_FILE_NAME_CAPACITY - 1,
					 2026, 1, 1, 0, 0, 0, 1),
		0);
	XVT_ASSERT_INT_EQ(name[0], 0);
	/* Capacity 0 writes nothing. */
	strcpy(name, "x");
	XVT_ASSERT_INT_EQ(
		xvt_log_file_format_name(name, 0, 2026, 1, 1, 0, 0, 0, 1), 0);
	XVT_ASSERT_INT_EQ(name[0], 'x');
}

static void check_is_run_name(void)
{
	XVT_ASSERT_INT_EQ(xvt_log_file_is_run_name(
				  "openxvt-20261001-205153-1a2b3c4d.log"),
			  1);
	XVT_ASSERT_INT_EQ(xvt_log_file_is_run_name(
				  "openxvt-00000000-000000-00000000.log"),
			  1);
	XVT_ASSERT_INT_EQ(xvt_log_file_is_run_name(NULL), 0);
	XVT_ASSERT_INT_EQ(xvt_log_file_is_run_name(""), 0);
	XVT_ASSERT_INT_EQ(xvt_log_file_is_run_name(
				  "openxvt-20261001-205153-1A2B3C4D.log"),
			  0);
	XVT_ASSERT_INT_EQ(xvt_log_file_is_run_name(
				  "openxvt-2026100a-205153-1a2b3c4d.log"),
			  0);
	XVT_ASSERT_INT_EQ(xvt_log_file_is_run_name(
				  "openxvt-20261001-205153-1a2b3c4g.log"),
			  0);
	XVT_ASSERT_INT_EQ(xvt_log_file_is_run_name(
				  "openxvt-20261001-205153-1a2b3c4d.log.bak"),
			  0);
	XVT_ASSERT_INT_EQ(
		xvt_log_file_is_run_name("openxvt-20261001-205153-1a2b3c4d.lo"),
		0);
	XVT_ASSERT_INT_EQ(
		xvt_log_file_is_run_name("openxvt-20261001-205153-1a2b3c4.log"),
		0);
	XVT_ASSERT_INT_EQ(xvt_log_file_is_run_name(
				  "OpenXvT-20261001-205153-1a2b3c4d.log"),
			  0);
	XVT_ASSERT_INT_EQ(xvt_log_file_is_run_name(
				  "openxvt_20261001-205153-1a2b3c4d.log"),
			  0);
}

static void check_select_expired(void)
{
	/* Twelve run names, out of order, among two names the folder also holds. */
	const char *names[] = {
		"openxvt-20261001-120000-00000007.log",
		"notes.txt",
		"openxvt-20260930-235959-00000004.log",
		"openxvt-20261001-120000-00000003.log",
		"openxvt-20260101-000000-0000000b.log",
		"openxvt-20261002-000000-00000001.log",
		"openxvt-20261001-090000-00000002.log",
		"openxvt-20261001-120000-0000000A.log",
		"openxvt-20261001-130000-00000005.log",
		"openxvt-20261001-140000-00000006.log",
		"openxvt-20261001-150000-00000008.log",
		"openxvt-20261001-160000-00000009.log",
		"openxvt-20261001-170000-0000000a.log",
		"openxvt-20261001-180000-0000000c.log",
	};
	size_t count = sizeof names / sizeof names[0];
	XVT_ASSERT_INT_EQ(xvt_log_file_select_expired(names, count, 10), 3);
	const char *oldest[] = {"openxvt-20260101-000000-0000000b.log",
				"openxvt-20260930-235959-00000004.log",
				"openxvt-20261001-090000-00000002.log"};
	for (size_t i = 0; i < 3; ++i) {
		XVT_ASSERT_INT_EQ(strcmp(names[i], oldest[i]), 0);
	}
	/* The run names come first, sorted; the others follow. */
	for (size_t i = 0; i < 12; ++i) {
		XVT_ASSERT_INT_EQ(xvt_log_file_is_run_name(names[i]), 1);
		if (i > 0) {
			XVT_ASSERT_TRUE(strcmp(names[i - 1], names[i]) < 0);
		}
	}
	for (size_t i = 12; i < count; ++i) {
		XVT_ASSERT_INT_EQ(xvt_log_file_is_run_name(names[i]), 0);
	}

	/* keep - 1 run names already there: nothing goes; keep of them: one goes. keep 0 counts as 1. */
	XVT_ASSERT_INT_EQ(xvt_log_file_select_expired(names, 9, 10), 0);
	XVT_ASSERT_INT_EQ(xvt_log_file_select_expired(names, 10, 10), 1);
	XVT_ASSERT_INT_EQ(xvt_log_file_select_expired(names, 12, 1), 12);
	XVT_ASSERT_INT_EQ(xvt_log_file_select_expired(names, 12, 0), 12);
	XVT_ASSERT_INT_EQ(xvt_log_file_select_expired(names, 0, 10), 0);
}

#define HEADER                                                                 \
	"= OpenXvT run 5efc913e fmt 1 date 2026-10-01 tz utc\n= user \"~/x/\" res \"r\" cwd \"/\"\n"

/* Judges text with xvt_log_file_read_ending and checks the verdict and the last event. */
static void check_ending(const char *text, xvt_log_file_ending want,
			 const char *want_last)
{
	char last[64] = "untouched";
	xvt_log_file_ending ending =
		xvt_log_file_read_ending(text, strlen(text), last, sizeof last);
	if (ending != want || strcmp(last, want_last)) {
		fprintf(stderr, "text:\n%s\ngave %d \"%s\", wanted %d \"%s\"\n",
			text, (int)ending, last, (int)want, want_last);
		XVT_ASSERT_TRUE(0);
	}
}

static void check_read_ending(void)
{
	check_ending(
		HEADER
		"21:22:35.005 I app.log_file path=\"x\"\n21:22:36.000 I app.stop exit=0\n",
		XVT_LOG_FILE_ENDING_STOPPED, "app.stop");
	check_ending(
		HEADER
		"21:22:35.005 I app.ready\n21:22:40.000 D network.input tick=8 key=0\n",
		XVT_LOG_FILE_ENDING_CUT, "network.input");
	check_ending(
		HEADER
		"21:22:35.005 I app.ready\n21:22:40.000 C app.crash_signal signal=\"SIGSEGV\" code=1 addr=0x0\n"
		"21:22:40.000 C app.crash_frame n=0 module=\"OpenXvT\" offset=0x1a\n",
		XVT_LOG_FILE_ENDING_CRASHED, "app.crash_frame");
	check_ending(
		HEADER
		"21:22:40.000 C app.crash_exception code=0xc0000005 addr=0x0\n",
		XVT_LOG_FILE_ENDING_CRASHED, "app.crash_exception");
	/* A crash after the stop line, while the program exits, is still a crash. */
	check_ending(
		HEADER
		"21:22:36.000 I app.stop exit=0\n21:22:36.001 C app.crash_signal signal=\"SIGSEGV\" code=1 "
		"addr=0x8\n",
		XVT_LOG_FILE_ENDING_CRASHED, "app.crash_signal");

	/* In a log several runs appended to, only the newest run counts. */
	check_ending(HEADER "21:22:36.000 I app.stop exit=0\n" HEADER
			    "21:30:00.000 I app.ready\n",
		     XVT_LOG_FILE_ENDING_CUT, "app.ready");
	check_ending(
		HEADER
		"21:22:40.000 C app.crash_signal signal=\"SIGABRT\" code=-6 addr=0x0\n" HEADER
		"21:30:00.000 I app.stop exit=0\n",
		XVT_LOG_FILE_ENDING_STOPPED, "app.stop");

	/* A tail that starts part way through a line: the cut line has no stamp and does not count. */
	check_ending("7.000 I app.stop exit=0\n21:22:38.000 I app.ready\n",
		     XVT_LOG_FILE_ENDING_CUT, "app.ready");
	check_ending("ield=1\n21:22:39.000 I app.stop exit=1\n",
		     XVT_LOG_FILE_ENDING_STOPPED, "app.stop");

	/* A header alone, nothing at all, a last line with no newline, and an event that only starts alike. */
	check_ending(HEADER, XVT_LOG_FILE_ENDING_CUT, "");
	check_ending("", XVT_LOG_FILE_ENDING_NONE, "");
	check_ending(
		HEADER
		"21:22:35.005 E files.fatal message=\"Cannot open\" path=\"a\"",
		XVT_LOG_FILE_ENDING_CUT, "files.fatal");
	check_ending(HEADER "21:22:35.005 I app.stopped\n",
		     XVT_LOG_FILE_ENDING_CUT, "app.stopped");

	/* The last event is cut to the buffer; capacity 0 writes nothing. */
	char last[8] = "x";
	XVT_ASSERT_INT_EQ(
		xvt_log_file_read_ending(HEADER "21:22:35.005 I app.ready\n",
					 strlen(HEADER) + 25, last, 5),
		XVT_LOG_FILE_ENDING_CUT);
	XVT_ASSERT_INT_EQ(strcmp(last, "app."), 0);
	strcpy(last, "x");
	XVT_ASSERT_INT_EQ(xvt_log_file_read_ending("", 0, last, 0),
			  XVT_LOG_FILE_ENDING_NONE);
	XVT_ASSERT_INT_EQ(strcmp(last, "x"), 0);
}

/* Returns the whole file as a string the caller frees; the check fails when it cannot be read. */
static char *read_all(const char *path, size_t *length)
{
	FILE *file = fopen(path, "rb");
	XVT_ASSERT_TRUE(file != NULL);
	XVT_ASSERT_INT_EQ(fseek(file, 0, SEEK_END), 0);
	long size = ftell(file);
	XVT_ASSERT_TRUE(size >= 0);
	rewind(file);
	char *text = malloc((size_t)size + 1);
	XVT_ASSERT_TRUE(text != NULL);
	XVT_ASSERT_INT_EQ(fread(text, 1, (size_t)size, file), size);
	text[size] = 0;
	fclose(file);
	*length = (size_t)size;
	return text;
}

static void check_open_write_append(const char *folder)
{
	char path[XVT_TEST_PATH_CAPACITY];
	xvt_test_join(path, folder, "run.log");
	char error[128] = "";
	xvt_log_file_handle file = xvt_log_file_open(path, error, sizeof error);
	XVT_ASSERT_TRUE(file != XVT_LOG_FILE_NONE);
	XVT_ASSERT_INT_EQ(xvt_log_file_write(file, "a\n", 2), 1);
	XVT_ASSERT_INT_EQ(xvt_log_file_write(file, "bb\n", 3), 1);
	XVT_ASSERT_INT_EQ(xvt_log_file_write(file, "", 0), 1);
	xvt_log_file_close(file);
	struct stat info;
	XVT_ASSERT_INT_EQ(stat(path, &info), 0);
	XVT_ASSERT_INT_EQ(info.st_mode & 0777, 0600);

	/* A second open appends to what is there. */
	file = xvt_log_file_open(path, error, sizeof error);
	XVT_ASSERT_TRUE(file != XVT_LOG_FILE_NONE);
	XVT_ASSERT_INT_EQ(xvt_log_file_write(file, "c\n", 2), 1);
	xvt_log_file_close(file);
	size_t length;
	char *text = read_all(path, &length);
	XVT_ASSERT_INT_EQ(strcmp(text, "a\nbb\nc\n"), 0);
	free(text);

	/* A folder that does not exist: no handle, and the reason. */
	xvt_test_join(path, folder, "missing/run.log");
	XVT_ASSERT_TRUE(xvt_log_file_open(path, error, sizeof error) ==
			XVT_LOG_FILE_NONE);
	XVT_ASSERT_TRUE(error[0] != 0);
	XVT_ASSERT_TRUE(xvt_log_file_open(path, NULL, 0) == XVT_LOG_FILE_NONE);
	XVT_ASSERT_INT_EQ(xvt_log_file_write(XVT_LOG_FILE_NONE, "x\n", 2), 0);
	xvt_log_file_close(XVT_LOG_FILE_NONE);
}

struct writer_job {
	xvt_log_file_handle file;
	char mark;
};

/* Writes THREAD_LINES lines of 100 copies of the job's mark, each with one call. */
static void *write_lines(void *argument)
{
	const struct writer_job *job = argument;
	char line[101];
	memset(line, job->mark, 100);
	line[100] = '\n';
	for (int i = 0; i < THREAD_LINES; ++i) {
		XVT_ASSERT_INT_EQ(
			xvt_log_file_write(job->file, line, sizeof line), 1);
	}
	return NULL;
}

static void check_threads_never_interleave(const char *folder)
{
	char path[XVT_TEST_PATH_CAPACITY];
	xvt_test_join(path, folder, "threads.log");
	xvt_log_file_handle file = xvt_log_file_open(path, NULL, 0);
	XVT_ASSERT_TRUE(file != XVT_LOG_FILE_NONE);
	struct writer_job jobs[2] = {{file, 'a'}, {file, 'b'}};
	pthread_t threads[2];
	for (int i = 0; i < 2; ++i) {
		XVT_ASSERT_INT_EQ(pthread_create(&threads[i], NULL, write_lines,
						 &jobs[i]),
				  0);
	}
	for (int i = 0; i < 2; ++i) {
		XVT_ASSERT_INT_EQ(pthread_join(threads[i], NULL), 0);
	}
	xvt_log_file_close(file);

	size_t length;
	/* Every line is whole: 100 of one mark, then a newline. */
	char *text = read_all(path, &length);
	XVT_ASSERT_INT_EQ(length, 2 * THREAD_LINES * 101);
	int count[2] = {0, 0};
	for (size_t at = 0; at < length; at += 101) {
		char mark = text[at];
		XVT_ASSERT_TRUE(mark == 'a' || mark == 'b');
		for (size_t i = 1; i < 100; ++i) {
			XVT_ASSERT_INT_EQ(text[at + i], mark);
		}
		XVT_ASSERT_INT_EQ(text[at + 100], '\n');
		count[mark - 'a']++;
	}
	XVT_ASSERT_INT_EQ(count[0], THREAD_LINES);
	XVT_ASSERT_INT_EQ(count[1], THREAD_LINES);
	free(text);
}

int main(void)
{
	check_format_name();
	check_is_run_name();
	check_select_expired();
	check_read_ending();
	char folder[XVT_TEST_PATH_CAPACITY];
	xvt_test_make_folder(folder);
	check_open_write_append(folder);
	check_threads_never_interleave(folder);
	xvt_test_remove_tree(folder);
	return 0;
}
