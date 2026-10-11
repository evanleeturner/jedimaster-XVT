/* The engine's DEBUG lines as a check's answer key. A check opens a scene,
 * runs one step of the code under test (a key, a tick), and closes the scene
 * against the lines pinned in the test. A scene's lines are the step's own
 * lines in the order written, then the world.track line of each craft whose
 * track changed during the step: a craft that did not move, turn, or change
 * its speed, throttle, state, hull, shields or targets adds no line. The
 * tracks come from record points (xvt_flight_record_point) taken at tick 0
 * when the scene opens and when it closes.
 *
 * The pinned lines are recorded from a run, not typed: with XVT_RECORD_SCENES
 * set in the environment a closing scene prints its lines between "@@scene
 * NAME" and "@@end" on standard output and passes, and a script writes them
 * into the test's array under the comment "recorded NAME". Without it a
 * closing scene compares, and the first line that differs fails the check
 * with both lines and the scene's whole list.
 *
 * A program includes this header once and calls log_lines_start before its
 * first scene; from then on it owns SDL's log output and the DEBUG level. */
#ifndef XVT_TESTS_LOG_LINES_H
#define XVT_TESTS_LOG_LINES_H

#include <SDL3/SDL_log.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xvt_runtime/log/log.h"
#include "xvt_runtime/runtime/flight_record.h"

enum {
	LOG_LINES_CAPACITY = 1024,
	LOG_LINES_SIZE = 400,
	LOG_LINES_TRACKS = 64,
};

static char g_log_lines[LOG_LINES_CAPACITY][LOG_LINES_SIZE];
static int g_log_line_count;
/* Lines caught past the capacity: a scene that drops any fails. */
static int g_log_lines_dropped;
static char g_log_tracks_before[LOG_LINES_TRACKS][LOG_LINES_SIZE];
static int g_log_tracks_before_count;
/* The scene's lines: the step's own, then the changed tracks. */
static const char *g_scene_lines[LOG_LINES_CAPACITY];
static int g_scene_line_count;

static void log_lines_catch(void *userdata, int category,
			    SDL_LogPriority priority, const char *message)
{
	static const char prefix[] = "xvt: ";
	(void)userdata;
	(void)category;
	(void)priority;
	if (g_log_line_count >= LOG_LINES_CAPACITY) {
		++g_log_lines_dropped;
		return;
	}
	if (strncmp(message, prefix, sizeof prefix - 1) == 0) {
		message += sizeof prefix - 1;
	}
	snprintf(g_log_lines[g_log_line_count++], LOG_LINES_SIZE, "%s",
		 message);
}

/* Turns DEBUG lines on and catches every line the program writes. */
static void log_lines_start(void)
{
	xvt_log_set_level(AERON_LOG_DEBUG);
	SDL_SetLogPriorities(SDL_LOG_PRIORITY_VERBOSE);
	SDL_SetLogOutputFunction(log_lines_catch, NULL);
}

static void log_lines_clear(void)
{
	g_log_line_count = 0;
	g_log_lines_dropped = 0;
}

static int log_lines_is_track(const char *line)
{
	return strncmp(line, "world.track ", 12) == 0;
}

/* The " object=N " part of a track line, which names its craft slot. */
static const char *log_lines_track_object(const char *line)
{
	const char *object = strstr(line, " object=");
	return object != NULL ? object : line;
}

/* 1 when the record point taken as the scene opened held exactly this
 * track: the same craft with every field equal. */
static int log_lines_track_unchanged(const char *line)
{
	for (int i = 0; i < g_log_tracks_before_count; ++i) {
		if (strcmp(g_log_tracks_before[i], line) == 0) {
			return 1;
		}
	}
	return 0;
}

/* Opens a scene: takes the record point the scene's tracks compare against
 * and forgets every line caught so far. */
static void scene_open(void)
{
	log_lines_clear();
	xvt_flight_record_point(0);
	g_log_tracks_before_count = 0;
	for (int i = 0; i < g_log_line_count &&
			g_log_tracks_before_count < LOG_LINES_TRACKS;
	     ++i) {
		if (log_lines_is_track(g_log_lines[i])) {
			snprintf(g_log_tracks_before
					 [g_log_tracks_before_count++],
				 LOG_LINES_SIZE, "%s", g_log_lines[i]);
		}
	}
	log_lines_clear();
}

/* Fills the scene's list: the lines caught since the scene opened, then a
 * second record point's tracks that differ from the first. */
static void scene_collect(void)
{
	int step_lines = g_log_line_count;
	xvt_flight_record_point(0);
	g_scene_line_count = 0;
	for (int i = 0; i < step_lines; ++i) {
		g_scene_lines[g_scene_line_count++] = g_log_lines[i];
	}
	for (int i = step_lines; i < g_log_line_count; ++i) {
		const char *line = g_log_lines[i];
		if (log_lines_is_track(line) &&
		    !log_lines_track_unchanged(line)) {
			g_scene_lines[g_scene_line_count++] = line;
		}
	}
}

static void scene_print(const char *name)
{
	printf("@@scene %s\n", name);
	for (int i = 0; i < g_scene_line_count; ++i) {
		printf("%s\n", g_scene_lines[i]);
	}
	printf("@@end\n");
}

static void scene_fail(const char *name, int index, const char *expected,
		       const char *actual)
{
	fprintf(stderr,
		"scene %s: line %d differs\n  expected: %s\n  actual:   %s\n"
		"  the scene's lines (%d):\n",
		name, index, expected != NULL ? expected : "(no more lines)",
		actual != NULL ? actual : "(no more lines)",
		g_scene_line_count);
	for (int i = 0; i < g_scene_line_count; ++i) {
		fprintf(stderr, "    %s\n", g_scene_lines[i]);
	}
	exit(1);
}

/* Closes the scene named name against expected, a NULL-ended list. With
 * XVT_RECORD_SCENES set, prints the scene's lines for the recording script
 * instead and passes. */
static void scene_close(const char *name, const char *const *expected)
{
	if (g_log_lines_dropped > 0) {
		fprintf(stderr, "scene %s: %d lines past the capacity\n", name,
			g_log_lines_dropped);
		exit(1);
	}
	scene_collect();
	if (getenv("XVT_RECORD_SCENES") != NULL) {
		scene_print(name);
		log_lines_clear();
		return;
	}
	int i = 0;
	for (; expected[i] != NULL && i < g_scene_line_count; ++i) {
		if (strcmp(expected[i], g_scene_lines[i]) != 0) {
			scene_fail(name, i, expected[i], g_scene_lines[i]);
		}
	}
	if (expected[i] != NULL || i < g_scene_line_count) {
		scene_fail(name, i, expected[i],
			   i < g_scene_line_count ? g_scene_lines[i] : NULL);
	}
	log_lines_clear();
}

#endif
