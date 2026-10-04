/* XvT/BoP versions 12-14, using the layouts consumed by mission_load_file.
 * Standalone: cc -std=c99 -Isrc -Iaeron/include tools/mission_dump.c -o mission_dump
 * All multibyte disk fields are decoded explicitly; no runtime initialization is needed. */
/* Reads one mission file and prints its contents as text: the version line, then each flight group (name,
 * craft, waves, IFF, team, AI, roles and cargo, arrival and departure triggers with their delays,
 * motherships, the four orders with primary and fallback targets, the skip-to-order trigger, the eight
 * goals and the enabled waypoints), each message, each team's global goals, each team's name, allies and
 * end-of-mission messages, the nonempty briefing labels and texts, and the nonempty goal text overrides;
 * it ends by printing the offset consumed. Names for variables, conditions, amounts and orders come from
 * the tables below; a value outside a table prints as "Unknown". Strings print quoted with C escapes.
 * Refuses, with a message on stderr, a version other than 12 to 14, more than 48 flight groups or 64
 * messages, a repeated or out-of-range message index, a team with more than 7 global goals, and any short
 * read. Exit status: 0 on success; 1 for a refused file or a failed open, read, close or flush; 2 for the
 * wrong argument count. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xvt/flight/mission/mission.h"

struct dump_mission {
	struct mission_header header;
	struct xvt_flight_group groups[48];
	unsigned group_count;
};

static const char *const variable_names[] = {"None",
					     "FlightGroup",
					     "SpeciesMinusOne",
					     "ShipClass",
					     "ObjectFamily",
					     "IFF",
					     "ShipOrder",
					     "CraftWhen",
					     "GlobalGroup",
					     "AILevel",
					     "Status",
					     "AllCraft",
					     "Team",
					     "PlayerNumber",
					     "BeforeTime",
					     "NotFlightGroup",
					     "NotSpeciesMinusOne",
					     "NotShipClass",
					     "NotObjectFamily",
					     "NotIFF",
					     "NotGlobalGroup",
					     "NotTeam",
					     "NotPlayerNumber",
					     "GlobalUnit",
					     "NotGlobalUnit"};
static const char *const condition_names[] = {"AlwaysTrue",
					      "Arrived",
					      "Destroyed",
					      "Attacked",
					      "Captured",
					      "Inspected",
					      "Boarded",
					      "Docked",
					      "Disabled",
					      "Survived",
					      "NeverFalse",
					      "Unknown11",
					      "Departed",
					      "PrimaryComplete",
					      "PrimaryFailed",
					      "Unknown15",
					      "Unknown16",
					      "BonusComplete",
					      "BonusFailed",
					      "Unknown19",
					      "ReinforcementNotCalled",
					      "ShieldsDepleted",
					      "HullAbove50",
					      "NoWarheads",
					      "SystemDamaged",
					      "NotArrived",
					      "NotAttacked",
					      "NotDisabled",
					      "NotCaptured",
					      "NotInspected",
					      "CompletedMission",
					      "NotBoarded",
					      "FailedMission",
					      "NotDocked",
					      "ShieldsBelow50",
					      "ShieldsBelow25",
					      "HullAbove25",
					      "HullAbove75",
					      "AlwaysPending",
					      "NoCondition",
					      "Unknown40",
					      "PlayerConnected",
					      "PlayerDisconnected",
					      "DestroyedOrDeparted",
					      "OrderCompleted",
					      "DestroyedOrCaptured",
					      "CapturedByDestination"};
static const char *const amount_names[] = {"All",
					   "75%",
					   "50%",
					   "25%",
					   "AtLeastOne",
					   "AllButOne",
					   "AllSpecialCargo",
					   "AllNonSpecial",
					   "AllExceptPlayer",
					   "PlayerFG",
					   "AllOfSubset",
					   "75%OfSubset",
					   "50%OfSubset",
					   "25%OfSubset",
					   "AtLeastOneAlt",
					   "AllButOneOfSubset",
					   "66%",
					   "33%"};
static const char *const order_names[] = {"Hold",
					  "GoHome",
					  "Formation",
					  "FormationEvade",
					  "Rendezvous",
					  "Disabled",
					  "WaitForBoard",
					  "Attack",
					  "AttackEscorters",
					  "Respond",
					  "Escort",
					  "Disable",
					  "BoardGive",
					  "BoardTake",
					  "BoardExchange",
					  "BoardCapture",
					  "BoardDestroy",
					  "BoardPickup",
					  "DropOff",
					  "Wait",
					  "Wait",
					  "StarshipFormation",
					  "StarshipWaitReturn",
					  "StarshipWaitCreate",
					  "StarshipProtect",
					  "StarshipProtect",
					  "StarshipAttack",
					  "StarshipDisable",
					  "Hold",
					  "StarshipHyperspace",
					  "Hold",
					  "BoardContact",
					  "BoardRepair",
					  "Hold",
					  "Hold",
					  "Hold",
					  "SelfDestroy",
					  "Kamikaze",
					  "Hold",
					  "Null"};

static const char *lookup(const char *const *names, size_t count,
			  unsigned value)
{
	return value < count ? names[value] : "Unknown";
}

static unsigned u16(const void *value)
{
	const unsigned char *p = value;
	return p[0] | ((unsigned)p[1] << 8);
}

static int read_exact(FILE *fp, void *data, size_t size)
{
	long offset = ftell(fp);
	if (fread(data, 1, size, fp) == size) {
		return 1;
	}
	fprintf(stderr,
		"mission_dump: truncated/unreadable record at 0x%lx (wanted %zu bytes)\n",
		offset, size);
	return 0;
}

static int read_word(FILE *fp, unsigned *value)
{
	unsigned char bytes[2];
	if (!read_exact(fp, bytes, sizeof(bytes))) {
		return 0;
	}
	*value = u16(bytes);
	return 1;
}

/* Quote fixed-length disk strings without reading beyond the field or emitting terminal controls. */
static void quote(const char *value, size_t size)
{
	putchar('"');
	for (size_t i = 0; i < size && value[i] != '\0'; ++i) {
		unsigned char c = (unsigned char)value[i];
		if (c == '"' || c == '\\') {
			printf("\\%c", c);
		} else if (c == '\n') {
			printf("\\n");
		} else if (c == '\r') {
			printf("\\r");
		} else if (c < 32 || c >= 127) {
			printf("\\x%02x", c);
		} else {
			putchar(c);
		}
	}
	putchar('"');
}

static void target(const struct dump_mission *mission, unsigned type,
		   unsigned value)
{
	printf("%s(%u)=%u",
	       lookup(variable_names,
		      sizeof(variable_names) / sizeof(*variable_names), type),
	       type, value);
	if ((type == 1 || type == 15) && value < mission->group_count) {
		putchar(' ');
		quote(mission->groups[value].name,
		      sizeof(mission->groups[value].name));
	}
	if (type == 7) {
		const char *state = value == 2	  ? "Boarded"
				    : value == 4  ? "Disabled"
				    : value == 12 ? "Operational"
				    : value == 16 ? "NotBoarded"
						  : NULL;
		if (state != NULL) {
			printf(" [%s]", state);
		}
	}
}

static void trigger(const struct dump_mission *mission,
		    const struct mission_trigger *t)
{
	printf("%s(%u) ",
	       lookup(condition_names,
		      sizeof(condition_names) / sizeof(*condition_names),
		      t->condition),
	       t->condition);
	target(mission, t->variable_type, t->variable);
	printf(" amount=%s(%u)",
	       lookup(amount_names,
		      sizeof(amount_names) / sizeof(*amount_names),
		      (uint8_t)t->amount),
	       (uint8_t)t->amount);
}

static void pair(const struct dump_mission *mission,
		 const struct mission_trigger_pair *p)
{
	putchar('(');
	trigger(mission, &p->triggers[0]);
	printf(" %s[%u] ", p->trigger1_or_trigger2 == 1 ? "OR" : "AND",
	       p->trigger1_or_trigger2);
	trigger(mission, &p->triggers[1]);
	putchar(')');
}

static void order(const struct dump_mission *mission,
		  const struct mission_order *o, unsigned index)
{
	printf("  Order[%u] %s(%u) throttle=%u vars=%u,%u,%u,%u speed=%u designation=",
	       index,
	       lookup(order_names, sizeof(order_names) / sizeof(*order_names),
		      o->order),
	       o->order, o->throttle, o->variable1, o->variable2, o->variable3,
	       o->variable4, o->speed);
	quote(o->designation, sizeof(o->designation));
	printf("\n    primary: ");
	target(mission, o->target1_type, o->target1);
	printf(" %s[%u] ", o->target1_or_target2 == 1 ? "OR" : "AND",
	       o->target1_or_target2);
	target(mission, o->target2_type, o->target2);
	printf("\n    fallback: ");
	target(mission, o->secondary_target_types[0], o->secondary_targets[0]);
	printf(" %s[%u] ", o->target3_or_target4 == 1 ? "OR" : "AND",
	       o->target3_or_target4);
	target(mission, o->secondary_target_types[1], o->secondary_targets[1]);
	putchar('\n');
}

static void teams_enabled(const uint8_t *teams)
{
	for (unsigned i = 0; i < 10; ++i) {
		if (teams[i] != 0) {
			printf(" %u:%u", i, teams[i]);
		}
	}
}

static void group(const struct dump_mission *mission, unsigned index)
{
	const struct xvt_flight_group *fg = &mission->groups[index];
	printf("\nFG[%u] @0x%zx ", index,
	       2 + sizeof(struct mission_header) + index * sizeof(*fg));
	quote(fg->name, sizeof(fg->name));
	printf(" species=%u craft=%u waves(raw)=%u IFF=%u team=%u AI=%u globalGroup=%u globalUnit=%u\n",
	       fg->craft_type, fg->number_of_craft, fg->number_of_waves,
	       fg->iff, fg->team, fg->group_ai, fg->global_group,
	       fg->global_unit);
	printf("  role=");
	quote(fg->craft_role, sizeof(fg->craft_role));
	printf(" cargo=");
	quote(fg->cargo, sizeof(fg->cargo));
	printf(" specialCargo=");
	quote(fg->special_cargo, sizeof(fg->special_cargo));
	printf("\n  status=%u,%u player=%u playerCraft=%u arriveOnlyIfHuman=%u difficulty=%u\n",
	       fg->status1, fg->status2, fg->player_number, fg->player_craft,
	       fg->arrive_only_if_human, fg->arrival_difficulty);
	printf("  arrival: ");
	pair(mission, &fg->arrival_triggers[0]);
	printf(" %s[%u] ", fg->arrivals12_or_arrivals34 == 1 ? "OR" : "AND",
	       fg->arrivals12_or_arrivals34);
	pair(mission, &fg->arrival_triggers[1]);
	printf(" delay=%u:%02u random=%u:%02u\n", fg->arrival_delay_minutes,
	       fg->arrival_delay_seconds, fg->arrival_rand_delay_minutes,
	       fg->arrival_rand_delay_seconds);
	printf("  departure: ");
	pair(mission, &fg->departure_trigger);
	printf(" delay=%u:%02u abort=%u\n", fg->departure_delay_minutes,
	       fg->departure_delay_seconds, fg->abort_trigger);
	printf("  motherships: arrival=%u method=%u departure=%u method=%u alternate=%u used=%u captured=%u "
	       "method=%u\n",
	       fg->arrival_mothership, fg->arrival_method,
	       fg->departure_mothership, fg->departure_method,
	       fg->alternate_mothership, fg->alternate_mothership_used,
	       fg->captured_departure_mothership,
	       fg->captured_depart_via_mothership);
	unsigned i;
	for (i = 0; i < 4; ++i) {
		order(mission, &fg->orders[i], i);
	}
	printf("  SkipToOrder[3]: ");
	pair(mission, &fg->skip_to_order4);
	putchar('\n');
	for (i = 0; i < 8; ++i) {
		const struct flight_group_goal *g = &fg->goals[i];
		printf("  Goal[%u] type=%u condition=%s(%u) amount=%s(%u) points(raw)=%d teams:",
		       i, g->goal_kind,
		       lookup(condition_names,
			      sizeof(condition_names) /
				      sizeof(*condition_names),
			      g->event_condition),
		       g->event_condition,
		       lookup(amount_names,
			      sizeof(amount_names) / sizeof(*amount_names),
			      (uint8_t)g->amount),
		       (uint8_t)g->amount, g->points);
		teams_enabled(g->enabled_teams);
		printf(" timeLimit5s=%u sequence=%u\n", g->time_limit5s,
		       g->active_sequence);
	}
	for (i = 0; i < 22; ++i) {
		if (u16(&fg->mission_point_enabled[i]) != 0) {
			printf("  Point[%u] x=%d y=%d z=%d enabled=%u\n", i,
			       (int16_t)u16(&fg->mission_point_x[i]),
			       (int16_t)u16(&fg->mission_point_y[i]),
			       (int16_t)u16(&fg->mission_point_z[i]),
			       u16(&fg->mission_point_enabled[i]));
		}
	}
}

static int messages_and_goals(FILE *fp, const struct dump_mission *mission,
			      unsigned message_count)
{
	unsigned i;
	unsigned index;
	uint8_t seen[64] = {0};
	for (i = 0; i < message_count; ++i) {
		if (!read_word(fp, &index)) {
			return 0;
		}
		if (index >= 64 || seen[index]) {
			fprintf(stderr,
				"mission_dump: invalid/duplicate message index %u\n",
				index);
			return 0;
		}
		seen[index] = 1;
		struct mission_message m;
		if (!read_exact(fp, &m, sizeof(m))) {
			return 0;
		}
		printf("\nMessage[%u] ", index);
		quote(m.message, sizeof(m.message));
		printf(" delay(raw)=%u teams:", m.delay5s);
		teams_enabled(m.sent_to_team);
		printf("\n  ");
		pair(mission, &m.trigger_pairs[0]);
		printf(" %s[%u] ",
		       m.trigger_pair1_or_trigger_pair2 == 1 ? "OR" : "AND",
		       m.trigger_pair1_or_trigger_pair2);
		pair(mission, &m.trigger_pairs[1]);
		putchar('\n');
	}
	unsigned j;
	unsigned count;
	for (i = 0; i < 10; ++i) {
		if (!read_word(fp, &count)) {
			return 0;
		}
		if (count > 7) {
			fprintf(stderr,
				"mission_dump: invalid global goal count %u\n",
				count);
			return 0;
		}
		for (j = 0; j < count; ++j) {
			struct global_goal g;
			if (!read_exact(fp, &g, sizeof(g))) {
				return 0;
			}
			printf("\nTeam[%u] GlobalGoal[%u] ", i, j);
			quote(g.name, sizeof(g.name));
			printf(" points(raw)=%d delay(raw)=%u\n  ",
			       g.raw_points, g.raw_delay);
			pair(mission, &g.trigger_pairs[0]);
			printf(" %s[%u] ",
			       g.trigger_pair1_or_trigger_pair2 == 1 ? "OR"
								     : "AND",
			       g.trigger_pair1_or_trigger_pair2);
			pair(mission, &g.trigger_pairs[1]);
			putchar('\n');
		}
	}
	for (i = 0; i < 10; ++i) {
		if (!read_word(fp, &count)) {
			return 0;
		}
		if (count == 0) {
			continue;
		}
		struct team t;
		if (!read_exact(fp, &t, sizeof(t))) {
			return 0;
		}
		printf("\nTeam[%u] ", i);
		quote(t.name, sizeof(t.name));
		printf(" allies:");
		teams_enabled(t.allies);
		putchar('\n');
		for (j = 0; j < 6; ++j) {
			printf("  EndMessage[%u] ", j);
			quote(t.end_of_mission_messages[j],
			      sizeof(t.end_of_mission_messages[j]));
			putchar('\n');
		}
	}
	return 1;
}

static int text_tail(FILE *fp, unsigned group_count)
{
	unsigned i;
	unsigned j;
	unsigned length;
	char script[820];
	char buffer[65536];
	for (i = 0; i < 8; ++i) {
		if (!read_exact(fp, script, sizeof(script))) {
			return 0;
		}
		for (j = 0; j < 64; ++j) {
			if (!read_word(fp, &length) ||
			    !read_exact(fp, buffer, length)) {
				return 0;
			}
			if (length != 0) {
				printf("Briefing[%u] %s[%u] ", i,
				       j < 32 ? "Label" : "Text", j % 32);
				quote(buffer, length);
				putchar('\n');
			}
		}
	}
	printf("\nGoal text overrides @0x%lx (raw display-state slots)\n",
	       ftell(fp));
	for (i = 0; i < group_count + 10; ++i) {
		unsigned n = i < group_count ? 8 : 28;
		for (j = 0; j < n; ++j) {
			for (unsigned k = 0; k < 3; ++k) {
				if (!read_exact(fp, buffer, 64)) {
					return 0;
				}
				if (buffer[0] != '\0') {
					if (i < group_count) {
						printf("FG[%u] Goal[%u] state[%u] ",
						       i, j, k);
					} else {
						printf("Team[%u] GlobalGoal[%u] Trigger[%u] state[%u] ",
						       i - group_count, j / 4,
						       j % 4, k);
					}
					quote(buffer, 64);
					putchar('\n');
				}
			}
		}
	}
	/* Later editor metadata is outside mission_load_file's consumed prefix. */
	printf("Consumed mission runtime data through 0x%lx\n", ftell(fp));
	return 1;
}

static int dump(FILE *fp)
{
	unsigned version;
	if (!read_word(fp, &version)) {
		return 0;
	}
	if (version != 12 && version != 13 && version != 14) {
		fprintf(stderr,
			"mission_dump: unsupported version %u (expected XvT/BoP 12, 13, or 14)\n",
			version);
		return 0;
	}
	struct dump_mission mission;
	if (!read_exact(fp, &mission.header, sizeof(mission.header))) {
		return 0;
	}
	mission.group_count = u16(&mission.header.num_flight_groups);
	unsigned message_count = u16(&mission.header.num_messages);
	if (mission.group_count > 48 || message_count > 64) {
		fprintf(stderr,
			"mission_dump: invalid counts: %u groups, %u messages\n",
			mission.group_count, message_count);
		return 0;
	}
	if (!read_exact(fp, mission.groups,
			mission.group_count * sizeof(*mission.groups))) {
		return 0;
	}
	printf("XvT/BoP version=%u flightGroups=%u messages=%u missionType=%d goalsUnimportant=%u\n",
	       version, mission.group_count, message_count,
	       mission.header.mission_type, mission.header.goals_unimportant);
	printf("Indices are zero-based. Order targets are predicates; fallback is tried if primary finds no "
	       "target.\n");
	for (unsigned i = 0; i < mission.group_count; ++i) {
		group(&mission, i);
	}
	return messages_and_goals(fp, &mission, message_count) &&
	       text_tail(fp, mission.group_count);
}

int main(int argc, char **argv)
{
	if (argc != 2) {
		fprintf(stderr, "Usage: %s mission.tie\n", argv[0]);
		return 2;
	}
	FILE *fp = fopen(argv[1], "rb");
	if (fp == NULL) {
		fprintf(stderr, "mission_dump: %s: %s\n", argv[1],
			strerror(errno));
		return 1;
	}
	int ok = dump(fp);
	if (fclose(fp) != 0) {
		ok = 0;
	}
	if (fflush(stdout) != 0) {
		ok = 0;
	}
	return ok ? 0 : 1;
}
