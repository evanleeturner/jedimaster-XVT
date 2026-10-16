/* Checks the --launch result file (xvt_runtime/runtime/launch_result.h)
 * against the promises in its header: the document each exit code makes, the
 * words for status and outcome, the players, teams and the pilot's own
 * tables, what does not fit, and the file written and read back from a
 * temporary folder. The module holds no state and reads no game data. The
 * battle's numbers are made up, each one different, so a member that takes
 * another's number shows. */
#include <limits.h>
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
	char text[4096];
	memset(text, 'x', sizeof(text));
	struct xvt_launch_result r =
		sample(XVT_LAUNCH_EXIT_FLOWN, XVT_LAUNCH_OUTCOME_COMPLETED);
	size_t length = xvt_launch_result_format(&r, text, sizeof(text));
	XVT_ASSERT_TRUE(length > 0);
	XVT_ASSERT_INT_EQ(length, strlen(text));
	XVT_ASSERT_INT_EQ(text[length - 1], '\n');
	XVT_ASSERT_TRUE(strstr(text, "\"schema_version\":4") != NULL);
	XVT_ASSERT_TRUE(strstr(text, "\"status\":\"flown\"") != NULL);
	XVT_ASSERT_TRUE(strstr(text, "\"exit_code\":0") != NULL);
	XVT_ASSERT_TRUE(strstr(text, "\"outcome\":\"completed\"") != NULL);
	XVT_ASSERT_TRUE(strstr(text, "\"score\":120") != NULL);
	XVT_ASSERT_TRUE(strstr(text, "\"kills\":4") != NULL);
	XVT_ASSERT_TRUE(strstr(text, "\"losses\":1") != NULL);
	XVT_ASSERT_TRUE(strstr(text, "\"goals_met_seconds\":305") != NULL);
	XVT_ASSERT_TRUE(strstr(text, "\"why\":\"\"") != NULL);
	/* No players, no teams, and the pilot's tables all 0: empty lists. */
	XVT_ASSERT_TRUE(
		strstr(text,
		       "\"local_entry\":0,\"players\":[],\"teams\":[],\"detail\":{\"shots\":0,\"hits\":0,\"warheads\":0,\"warhead_hits\":0,\"losses\":0,\"losses_to_collisions\":0,\"losses_to_starships\":0,\"losses_to_mines\":0,\"friendlies_killed\":0,\"special_inspected\":0,\"by_craft\":[],\"on_pilot_ranks\":[],\"on_ai_ranks\":[],\"killed_by_pilot_ranks\":[],\"killed_by_ai_ranks\":[],\"on_players\":[],\"from_players\":[],\"on_flight_groups\":[],\"from_flight_groups\":[]}}\n") !=
		NULL);
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
	char text[4096];
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
	char text[4096];
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
	char text[4096];
	struct xvt_launch_result r =
		sample(XVT_LAUNCH_EXIT_FLOWN, XVT_LAUNCH_OUTCOME_COMPLETED);
	size_t length = xvt_launch_result_format(&r, text, sizeof(text));
	char tight[4096];
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
	/* The local entry follows the sith object. */
	XVT_ASSERT_TRUE(strstr(text, "\"why\":\"\"},\"local_entry\":0,") !=
			NULL);

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

/* A battle shaped like a 1997 internet game's duty roster: seven players in
 * two flight groups of four seats, Rogue (0) and Gold (1), with Gold's last
 * seat empty, and one name of the full 12 characters. The pilot record's
 * entries need not be the flight's places: entry 3 flew in place 6, and
 * entry 5 had no place. Player i's numbers run from 100 * (i + 1) + 1, one
 * for each member, so none equals another. */
static struct xvt_launch_result roster_sample(void)
{
	static const char *const names[] = {
		"Kestrel", "vx_Moth",	   "Ardent", "newpilot",
		"Quillon", "Nightw1ng_42", "Tallow"};
	static const int entries[] = {0, 1, 2, 3, 4, 5, 6};
	static const int slots[] = {0, 1, 2, 6, 3, -1, 5};
	static const int ratings[] = {14, 12, 13, 0, 9, 7, 6};
	struct xvt_launch_result r =
		sample(XVT_LAUNCH_EXIT_FLOWN, XVT_LAUNCH_OUTCOME_COMPLETED);
	r.local_entry = 5;
	r.player_count = 7;
	for (int i = 0; i < 7; ++i) {
		struct xvt_launch_player *p = &r.players[i];
		int base = 100 * (i + 1);
		p->entry = entries[i];
		p->slot = slots[i];
		snprintf(p->name, sizeof(p->name), "%s", names[i]);
		p->team = slots[i] < 0 ? -1 : 0;
		p->flight_group = slots[i] < 0 ? 0 : i / 4;
		p->rating = ratings[i];
		p->score = base + 1;
		p->kills = base + 2;
		p->shared = base + 3;
		p->assists = base + 4;
		p->losses = base + 5;
		p->shots = base + 6;
		p->hits = base + 7;
		p->warheads = base + 8;
		p->warhead_hits = base + 9;
		p->has_left = i == 6;
	}
	r.team_count = 2;
	r.teams[0] = (struct xvt_launch_team){0, 1, 2601, 31, 7, 3, 645};
	r.teams[1] = (struct xvt_launch_team){1, 0, 412, 5, 2, 29, 0};
	return r;
}

/* Every player and team, in order, each member under its own name. */
static void check_players_and_teams(void)
{
	char text[8192];
	struct xvt_launch_result r = roster_sample();
	XVT_ASSERT_TRUE(xvt_launch_result_format(&r, text, sizeof(text)) > 0);
	XVT_ASSERT_TRUE(strstr(text, "\"local_entry\":5,\"players\":[{") !=
			NULL);
	XVT_ASSERT_TRUE(
		strstr(text,
		       "\"players\":[{\"entry\":0,\"slot\":0,\"name\":\"Kestrel\",\"team\":0,\"flight_group\":0,\"rating\":14,\"score\":101,\"kills\":102,\"shared\":103,\"assists\":104,\"losses\":105,\"shots\":106,\"hits\":107,\"warheads\":108,\"warhead_hits\":109,\"has_left\":false},") !=
		NULL);
	XVT_ASSERT_TRUE(
		strstr(text,
		       "{\"entry\":3,\"slot\":6,\"name\":\"newpilot\",\"team\":0,\"flight_group\":0,\"rating\":0,\"score\":401,") !=
		NULL);
	XVT_ASSERT_TRUE(
		strstr(text,
		       "{\"entry\":5,\"slot\":-1,\"name\":\"Nightw1ng_42\",\"team\":-1,\"flight_group\":0,\"rating\":7,\"score\":601,\"kills\":602,\"shared\":603,\"assists\":604,\"losses\":605,\"shots\":606,\"hits\":607,\"warheads\":608,\"warhead_hits\":609,\"has_left\":false},") !=
		NULL);
	XVT_ASSERT_TRUE(
		strstr(text,
		       "{\"entry\":6,\"slot\":5,\"name\":\"Tallow\",\"team\":0,\"flight_group\":1,\"rating\":6,\"score\":701,\"kills\":702,\"shared\":703,\"assists\":704,\"losses\":705,\"shots\":706,\"hits\":707,\"warheads\":708,\"warhead_hits\":709,\"has_left\":true}],\"teams\":[") !=
		NULL);
	/* Seven players, in the order given. */
	const char *at = text;
	for (int i = 0; i < 7; ++i) {
		char expected[64];
		snprintf(expected, sizeof(expected),
			 "{\"entry\":%d,\"slot\":", r.players[i].entry);
		at = strstr(at, expected);
		XVT_ASSERT_TRUE(at != NULL);
	}
	XVT_ASSERT_TRUE(strstr(at + 1, "{\"entry\":") == NULL);
	XVT_ASSERT_TRUE(
		strstr(text,
		       "\"teams\":[{\"team\":0,\"completed\":true,\"score\":2601,\"kills\":31,\"shared\":7,\"losses\":3,\"seconds\":645},{\"team\":1,\"completed\":false,\"score\":412,\"kills\":5,\"shared\":2,\"losses\":29,\"seconds\":0}],\"detail\":{") !=
		NULL);
	/* Only the first player_count and team_count are written. */
	r.player_count = 1;
	r.team_count = 0;
	XVT_ASSERT_TRUE(xvt_launch_result_format(&r, text, sizeof(text)) > 0);
	XVT_ASSERT_TRUE(strstr(text, "\"has_left\":false}],\"teams\":[],") !=
			NULL);
	XVT_ASSERT_TRUE(strstr(text, "\"entry\":1,") == NULL);
}

/* The pilot's tables: the counts, then each list with only its rows that
 * hold a number other than 0, the first and the last index included. */
static void check_detail(void)
{
	char text[8192];
	struct xvt_launch_result r = roster_sample();
	struct xvt_launch_detail *d = &r.detail;
	d->shots = 811;
	d->hits = 812;
	d->warheads = 813;
	d->warhead_hits = 814;
	d->losses = 815;
	d->losses_to_collisions = 816;
	d->losses_to_starships = 817;
	d->losses_to_mines = 818;
	d->friendlies_killed = 819;
	d->special_inspected = 820;
	d->kills_by_craft[0] = 21;
	d->shared_by_craft[0] = 22;
	d->assists_by_craft[0] = 23;
	d->assists_by_craft[41] = 24; /* a row with assists only */
	d->kills_by_craft[99] = 25;
	d->kills_on_pilot_rank[0] = 31;
	d->shared_on_pilot_rank[24] = 32;
	d->assists_on_pilot_rank[24] = 33;
	d->kills_on_ai_rank[5] = 41;
	d->shared_on_ai_rank[0] = 42;
	d->assists_on_ai_rank[3] = 43;
	d->killed_by_pilot_rank[24] = 51;
	d->killed_by_pilot_rank[0] = 52;
	d->killed_by_ai_rank[5] = 53;
	d->kills_on_player[0] = 61;
	d->shared_on_player[7] = 62;
	d->kills_from_player[6] = 63;
	d->shared_from_player[6] = 64;
	d->kills_on_flight_group[0] = 71;
	d->shared_on_flight_group[47] = 72;
	d->kills_from_flight_group[47] = 73;
	d->shared_from_flight_group[12] = 74;
	XVT_ASSERT_TRUE(xvt_launch_result_format(&r, text, sizeof(text)) > 0);
	XVT_ASSERT_TRUE(
		strstr(text,
		       "\"detail\":{\"shots\":811,\"hits\":812,\"warheads\":813,\"warhead_hits\":814,\"losses\":815,\"losses_to_collisions\":816,\"losses_to_starships\":817,\"losses_to_mines\":818,\"friendlies_killed\":819,\"special_inspected\":820,"
		       "\"by_craft\":[{\"craft_type\":0,\"kills\":21,\"shared\":22,\"assists\":23},{\"craft_type\":41,\"kills\":0,\"shared\":0,\"assists\":24},{\"craft_type\":99,\"kills\":25,\"shared\":0,\"assists\":0}],"
		       "\"on_pilot_ranks\":[{\"rank\":0,\"kills\":31,\"shared\":0,\"assists\":0},{\"rank\":24,\"kills\":0,\"shared\":32,\"assists\":33}],"
		       "\"on_ai_ranks\":[{\"rank\":0,\"kills\":0,\"shared\":42,\"assists\":0},{\"rank\":3,\"kills\":0,\"shared\":0,\"assists\":43},{\"rank\":5,\"kills\":41,\"shared\":0,\"assists\":0}],"
		       "\"killed_by_pilot_ranks\":[{\"rank\":0,\"times\":52},{\"rank\":24,\"times\":51}],"
		       "\"killed_by_ai_ranks\":[{\"rank\":5,\"times\":53}],"
		       "\"on_players\":[{\"entry\":0,\"kills\":61,\"shared\":0},{\"entry\":7,\"kills\":0,\"shared\":62}],"
		       "\"from_players\":[{\"entry\":6,\"kills\":63,\"shared\":64}],"
		       "\"on_flight_groups\":[{\"flight_group\":0,\"kills\":71,\"shared\":0},{\"flight_group\":47,\"kills\":0,\"shared\":72}],"
		       "\"from_flight_groups\":[{\"flight_group\":12,\"kills\":0,\"shared\":74},{\"flight_group\":47,\"kills\":73,\"shared\":0}]}}\n") !=
		NULL);
}

/* A count or a local entry outside its range has no document; the ends of
 * each range do. */
static void check_count_refusals(void)
{
	char text[8192];
	struct xvt_launch_result r = roster_sample();
	r.player_count = 9;
	XVT_ASSERT_INT_EQ(xvt_launch_result_format(&r, text, sizeof(text)), 0);
	r.player_count = -1;
	XVT_ASSERT_INT_EQ(xvt_launch_result_format(&r, text, sizeof(text)), 0);
	r = roster_sample();
	r.team_count = 9;
	XVT_ASSERT_INT_EQ(xvt_launch_result_format(&r, text, sizeof(text)), 0);
	r.team_count = -1;
	XVT_ASSERT_INT_EQ(xvt_launch_result_format(&r, text, sizeof(text)), 0);
	r = roster_sample();
	r.local_entry = 8;
	XVT_ASSERT_INT_EQ(xvt_launch_result_format(&r, text, sizeof(text)), 0);
	r.local_entry = -2;
	XVT_ASSERT_INT_EQ(xvt_launch_result_format(&r, text, sizeof(text)), 0);
	r.local_entry = -1;
	r.player_count = 8;
	r.team_count = 8;
	XVT_ASSERT_TRUE(xvt_launch_result_format(&r, text, sizeof(text)) > 0);
	XVT_ASSERT_TRUE(strstr(text, "\"local_entry\":-1,") != NULL);
	r.local_entry = 7;
	r.player_count = 0;
	r.team_count = 0;
	XVT_ASSERT_TRUE(xvt_launch_result_format(&r, text, sizeof(text)) > 0);
}

/* The longest document: every count at its end, every number the longest
 * an int prints, every text of characters JSON writes as six. It fits in
 * XVT_LAUNCH_RESULT_MAX, and the file takes it. The struct holds ints and
 * texts only, so it is filled as ints first and the texts set after. The
 * file goes in folder. */
static void check_largest_fits(const char *folder)
{
	struct xvt_launch_result r;
	memset(&r, 0, sizeof(r));
	int *numbers = (int *)&r;
	for (size_t i = 0; i < sizeof(r) / sizeof(int); ++i) {
		numbers[i] = INT_MIN;
	}
	r.exit_code = XVT_LAUNCH_EXIT_FLOWN;
	r.outcome = XVT_LAUNCH_OUTCOME_COMPLETED;
	r.local_entry = 7;
	r.player_count = XVT_LAUNCH_PLAYERS;
	r.team_count = XVT_LAUNCH_TEAMS;
	r.sith.setting = XVT_SITH_LORD;
	memset(r.why, 1, sizeof(r.why) - 1);
	r.why[sizeof(r.why) - 1] = '\0';
	memset(r.sith.flight_name, 1, sizeof(r.sith.flight_name) - 1);
	r.sith.flight_name[sizeof(r.sith.flight_name) - 1] = '\0';
	memset(r.sith.why, 1, sizeof(r.sith.why) - 1);
	r.sith.why[sizeof(r.sith.why) - 1] = '\0';
	for (int i = 0; i < XVT_LAUNCH_PLAYERS; ++i) {
		memset(r.players[i].name, 1, 12);
		r.players[i].name[12] = '\0';
	}
	char *text = malloc(XVT_LAUNCH_RESULT_MAX);
	XVT_ASSERT_TRUE(text != NULL);
	size_t length =
		xvt_launch_result_format(&r, text, XVT_LAUNCH_RESULT_MAX);
	XVT_ASSERT_TRUE(length > 0);
	XVT_ASSERT_TRUE(strstr(text, "\"by_craft\":[{\"craft_type\":0,") !=
			NULL);
	XVT_ASSERT_TRUE(strstr(text, "{\"craft_type\":99,") != NULL);
	char path[128];
	snprintf(path, sizeof(path), "%s/largest.json", folder);
	XVT_ASSERT_INT_EQ(xvt_launch_result_write(&r, path), 1);
	FILE *file = fopen(path, "rb");
	XVT_ASSERT_TRUE(file != NULL);
	char *found = malloc(XVT_LAUNCH_RESULT_MAX);
	XVT_ASSERT_TRUE(found != NULL);
	size_t read = fread(found, 1, XVT_LAUNCH_RESULT_MAX, file);
	fclose(file);
	XVT_ASSERT_INT_EQ(read, length);
	XVT_ASSERT_INT_EQ(memcmp(found, text, length), 0);
	free(found);
	free(text);
	unlink(path);
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
	char expected[4096];
	char found[4096] = {0};
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
	check_largest_fits(folder);
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
	check_players_and_teams();
	check_detail();
	check_count_refusals();
	check_written_file();
	return 0;
}
