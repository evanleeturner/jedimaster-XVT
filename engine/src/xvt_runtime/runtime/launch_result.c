#include "xvt_runtime/runtime/launch_result.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "xvt_runtime/runtime/sith.h"

/* The status word of an exit code, or NULL. */
static const char *status_word(int exit_code)
{
	switch (exit_code) {
	case XVT_LAUNCH_EXIT_FLOWN:
		return "flown";
	case XVT_LAUNCH_EXIT_REFUSED:
		return "refused";
	case XVT_LAUNCH_EXIT_NOT_REACHED:
		return "not_reached";
	case XVT_LAUNCH_EXIT_FLIGHT_FAILED:
		return "failed";
	default:
		return NULL;
	}
}

/* The outcome word of an outcome code, or NULL. */
static const char *outcome_word(int outcome)
{
	switch (outcome) {
	case XVT_LAUNCH_OUTCOME_NONE:
		return "none";
	case XVT_LAUNCH_OUTCOME_COMPLETED:
		return "completed";
	case XVT_LAUNCH_OUTCOME_NOT_COMPLETED:
		return "not_completed";
	default:
		return NULL;
	}
}

/* Adds the sith object of r to root. Returns 0 when the setting is none of
 * the three or the object cannot be made. */
static int add_sith(cJSON *root, const struct xvt_launch_sith *s)
{
	if (s->setting < XVT_SITH_NONE || s->setting > XVT_SITH_LORD) {
		return 0;
	}
	int none = s->setting == XVT_SITH_NONE;
	cJSON *object = cJSON_AddObjectToObject(root, "sith");
	if (!object) {
		return 0;
	}
	cJSON_AddStringToObject(object, "setting",
				xvt_sith_setting_name(s->setting));
	cJSON_AddNumberToObject(object, "flight_group",
				none ? -1 : s->flight_group);
	cJSON_AddStringToObject(object, "flight_name",
				none ? "" : s->flight_name);
	cJSON_AddNumberToObject(object, "craft_type",
				none ? -1 : s->craft_type);
	cJSON_AddNumberToObject(object, "apprentices_flown",
				none ? 0 : s->apprentices_flown);
	cJSON_AddNumberToObject(object, "apprentices_lost",
				none ? 0 : s->apprentices_lost);
	cJSON_AddNumberToObject(object, "lord_flown", none ? 0 : s->lord_flown);
	cJSON_AddNumberToObject(object, "lord_lost", none ? 0 : s->lord_lost);
	cJSON_AddStringToObject(object, "why", none ? "" : s->why);
	return 1;
}

/* Adds to root the list players of r. Returns 0 when it cannot be made. */
static int add_players(cJSON *root, const struct xvt_launch_result *r)
{
	cJSON *list = cJSON_AddArrayToObject(root, "players");
	if (!list) {
		return 0;
	}
	for (int i = 0; i < r->player_count; ++i) {
		const struct xvt_launch_player *p = &r->players[i];
		cJSON *object = cJSON_CreateObject();
		if (!object || !cJSON_AddItemToArray(list, object)) {
			cJSON_Delete(object);
			return 0;
		}
		cJSON_AddNumberToObject(object, "entry", p->entry);
		cJSON_AddNumberToObject(object, "slot", p->slot);
		cJSON_AddStringToObject(object, "name", p->name);
		cJSON_AddNumberToObject(object, "team", p->team);
		cJSON_AddNumberToObject(object, "flight_group",
					p->flight_group);
		cJSON_AddNumberToObject(object, "rating", p->rating);
		cJSON_AddNumberToObject(object, "score", p->score);
		cJSON_AddNumberToObject(object, "kills", p->kills);
		cJSON_AddNumberToObject(object, "shared", p->shared);
		cJSON_AddNumberToObject(object, "assists", p->assists);
		cJSON_AddNumberToObject(object, "losses", p->losses);
		cJSON_AddNumberToObject(object, "shots", p->shots);
		cJSON_AddNumberToObject(object, "hits", p->hits);
		cJSON_AddNumberToObject(object, "warheads", p->warheads);
		cJSON_AddNumberToObject(object, "warhead_hits",
					p->warhead_hits);
		cJSON_AddBoolToObject(object, "has_left", p->has_left != 0);
	}
	return 1;
}

/* Adds to root the list teams of r. Returns 0 when it cannot be made. */
static int add_teams(cJSON *root, const struct xvt_launch_result *r)
{
	cJSON *list = cJSON_AddArrayToObject(root, "teams");
	if (!list) {
		return 0;
	}
	for (int i = 0; i < r->team_count; ++i) {
		const struct xvt_launch_team *t = &r->teams[i];
		cJSON *object = cJSON_CreateObject();
		if (!object || !cJSON_AddItemToArray(list, object)) {
			cJSON_Delete(object);
			return 0;
		}
		cJSON_AddNumberToObject(object, "team", t->team);
		cJSON_AddBoolToObject(object, "completed", t->completed != 0);
		cJSON_AddNumberToObject(object, "score", t->score);
		cJSON_AddNumberToObject(object, "kills", t->kills);
		cJSON_AddNumberToObject(object, "shared", t->shared);
		cJSON_AddNumberToObject(object, "losses", t->losses);
		cJSON_AddNumberToObject(object, "seconds", t->seconds);
	}
	return 1;
}

/* Adds to object the list name: one row per index below count at which one
 * of the column_count tables in columns holds a number other than 0, lowest
 * index first, each the index under key and then each table's number under
 * its name in names. Returns 0 when it cannot be made. */
static int add_rows(cJSON *object, const char *name, const char *key, int count,
		    const char *const *names, const int *const *columns,
		    int column_count)
{
	cJSON *list = cJSON_AddArrayToObject(object, name);
	if (!list) {
		return 0;
	}
	for (int i = 0; i < count; ++i) {
		int shown = 0;
		for (int c = 0; c < column_count; ++c) {
			shown |= columns[c][i] != 0;
		}
		if (!shown) {
			continue;
		}
		cJSON *row = cJSON_CreateObject();
		if (!row || !cJSON_AddItemToArray(list, row)) {
			cJSON_Delete(row);
			return 0;
		}
		cJSON_AddNumberToObject(row, key, i);
		for (int c = 0; c < column_count; ++c) {
			cJSON_AddNumberToObject(row, names[c], columns[c][i]);
		}
	}
	return 1;
}

/* Adds to object the detail's lists. Returns 0 when one cannot be made. */
static int add_detail_rows(cJSON *object, const struct xvt_launch_detail *d)
{
	static const char *const kill_names[] = {"kills", "shared", "assists"};
	static const char *const pair_names[] = {"kills", "shared"};
	static const char *const times_name[] = {"times"};
	const int *by_craft[] = {d->kills_by_craft, d->shared_by_craft,
				 d->assists_by_craft};
	const int *on_pilot[] = {d->kills_on_pilot_rank,
				 d->shared_on_pilot_rank,
				 d->assists_on_pilot_rank};
	const int *on_ai[] = {d->kills_on_ai_rank, d->shared_on_ai_rank,
			      d->assists_on_ai_rank};
	const int *by_pilot[] = {d->killed_by_pilot_rank};
	const int *by_ai[] = {d->killed_by_ai_rank};
	const int *on_players[] = {d->kills_on_player, d->shared_on_player};
	const int *from_players[] = {d->kills_from_player,
				     d->shared_from_player};
	const int *on_groups[] = {d->kills_on_flight_group,
				  d->shared_on_flight_group};
	const int *from_groups[] = {d->kills_from_flight_group,
				    d->shared_from_flight_group};
	return add_rows(object, "by_craft", "craft_type",
			XVT_LAUNCH_CRAFT_TYPES, kill_names, by_craft, 3) &&
	       add_rows(object, "on_pilot_ranks", "rank",
			XVT_LAUNCH_PILOT_RANKS, kill_names, on_pilot, 3) &&
	       add_rows(object, "on_ai_ranks", "rank", XVT_LAUNCH_AI_RANKS,
			kill_names, on_ai, 3) &&
	       add_rows(object, "killed_by_pilot_ranks", "rank",
			XVT_LAUNCH_PILOT_RANKS, times_name, by_pilot, 1) &&
	       add_rows(object, "killed_by_ai_ranks", "rank",
			XVT_LAUNCH_AI_RANKS, times_name, by_ai, 1) &&
	       add_rows(object, "on_players", "entry", XVT_LAUNCH_PLAYERS,
			pair_names, on_players, 2) &&
	       add_rows(object, "from_players", "entry", XVT_LAUNCH_PLAYERS,
			pair_names, from_players, 2) &&
	       add_rows(object, "on_flight_groups", "flight_group",
			XVT_LAUNCH_FLIGHT_GROUPS, pair_names, on_groups, 2) &&
	       add_rows(object, "from_flight_groups", "flight_group",
			XVT_LAUNCH_FLIGHT_GROUPS, pair_names, from_groups, 2);
}

/* Adds to root the object detail of r. Returns 0 when it cannot be made. */
static int add_detail(cJSON *root, const struct xvt_launch_detail *d)
{
	cJSON *object = cJSON_AddObjectToObject(root, "detail");
	if (!object) {
		return 0;
	}
	cJSON_AddNumberToObject(object, "shots", d->shots);
	cJSON_AddNumberToObject(object, "hits", d->hits);
	cJSON_AddNumberToObject(object, "warheads", d->warheads);
	cJSON_AddNumberToObject(object, "warhead_hits", d->warhead_hits);
	cJSON_AddNumberToObject(object, "losses", d->losses);
	cJSON_AddNumberToObject(object, "losses_to_collisions",
				d->losses_to_collisions);
	cJSON_AddNumberToObject(object, "losses_to_starships",
				d->losses_to_starships);
	cJSON_AddNumberToObject(object, "losses_to_mines", d->losses_to_mines);
	cJSON_AddNumberToObject(object, "friendlies_killed",
				d->friendlies_killed);
	cJSON_AddNumberToObject(object, "special_inspected",
				d->special_inspected);
	return add_detail_rows(object, d);
}

/* 1 when the counts and the local entry of r are in their ranges. */
static int counts_fit(const struct xvt_launch_result *r)
{
	return r->player_count >= 0 && r->player_count <= XVT_LAUNCH_PLAYERS &&
	       r->team_count >= 0 && r->team_count <= XVT_LAUNCH_TEAMS &&
	       r->local_entry >= -1 && r->local_entry < XVT_LAUNCH_PLAYERS;
}

size_t xvt_launch_result_format(const struct xvt_launch_result *r, char *out,
				size_t size)
{
	const char *status = status_word(r->exit_code);
	const char *outcome = outcome_word(r->outcome);
	if (!status || !outcome || size == 0 || !counts_fit(r)) {
		return 0;
	}
	cJSON *root = cJSON_CreateObject();
	if (!root) {
		return 0;
	}
	cJSON_AddNumberToObject(root, "schema_version", 4);
	cJSON_AddStringToObject(root, "status", status);
	cJSON_AddNumberToObject(root, "exit_code", r->exit_code);
	cJSON_AddStringToObject(root, "outcome", outcome);
	cJSON_AddNumberToObject(root, "score", r->score);
	cJSON_AddNumberToObject(root, "kills", r->kills);
	cJSON_AddNumberToObject(root, "losses", r->losses);
	cJSON_AddNumberToObject(root, "goals_met_seconds",
				r->goals_met_seconds);
	cJSON_AddStringToObject(root, "why", r->why);
	int made = add_sith(root, &r->sith);
	cJSON_AddNumberToObject(root, "local_entry", r->local_entry);
	made = made && add_players(root, r) && add_teams(root, r) &&
	       add_detail(root, &r->detail);
	if (!made) {
		cJSON_Delete(root);
		return 0;
	}
	char *text = cJSON_PrintUnformatted(root);
	cJSON_Delete(root);
	size_t length = text ? strlen(text) : 0;
	if (length == 0 || length + 2 > size) {
		length = 0;
	} else {
		memcpy(out, text, length);
		out[length++] = '\n';
		out[length] = '\0';
	}
	cJSON_free(text);
	return length;
}

int xvt_launch_result_write(const struct xvt_launch_result *r, const char *path)
{
	char *text = malloc(XVT_LAUNCH_RESULT_MAX);
	size_t length =
		text ? xvt_launch_result_format(r, text, XVT_LAUNCH_RESULT_MAX)
		     : 0;
	FILE *file = length ? fopen(path, "wb") : NULL;
	if (!file) {
		free(text);
		return 0;
	}
	int written = fwrite(text, 1, length, file) == length;
	free(text);
	return fclose(file) == 0 && written;
}
