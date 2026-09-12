/* XvT/BoP versions 12-14, using the layouts consumed by Mission_LoadFile.
 * Standalone: cc -std=c99 -Isrc -Iaeron/include tools/mission_dump.c -o mission_dump
 * All multibyte disk fields are decoded explicitly; no runtime initialization is needed. */
#include "xvt/flight/mission/mission.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct DumpMission {
	MissionHeader header;
	XvtFlightGroup groups[48];
	unsigned groupCount;
} DumpMission;

static const char* const variableNames[] = { "None",
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
											 "NotGlobalUnit" };
static const char* const conditionNames[] = { "AlwaysTrue",
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
											  "CapturedByDestination" };
static const char* const amountNames[] = { "All",
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
										   "33%" };
static const char* const orderNames[] = { "Hold",
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
										  "Null" };

static const char* lookup(const char* const* names, size_t count, unsigned value) {
	return value < count ? names[value] : "Unknown";
}

static unsigned u16(const void* value) {
	const unsigned char* p = value;
	return p[0] | ((unsigned)p[1] << 8);
}

static int read_exact(FILE* fp, void* data, size_t size) {
	long offset = ftell(fp);
	if (fread(data, 1, size, fp) == size)
		return 1;
	fprintf(stderr, "mission_dump: truncated/unreadable record at 0x%lx (wanted %zu bytes)\n", offset, size);
	return 0;
}

static int read_word(FILE* fp, unsigned* value) {
	unsigned char bytes[2];
	if (!read_exact(fp, bytes, sizeof(bytes)))
		return 0;
	*value = u16(bytes);
	return 1;
}

/* Quote fixed-length disk strings without reading beyond the field or emitting terminal controls. */
static void quote(const char* value, size_t size) {
	size_t i;
	putchar('"');
	for (i = 0; i < size && value[i] != '\0'; ++i) {
		unsigned char c = (unsigned char)value[i];
		if (c == '"' || c == '\\')
			printf("\\%c", c);
		else if (c == '\n')
			printf("\\n");
		else if (c == '\r')
			printf("\\r");
		else if (c < 32 || c >= 127)
			printf("\\x%02x", c);
		else
			putchar(c);
	}
	putchar('"');
}

static void target(const DumpMission* mission, unsigned type, unsigned value) {
	printf("%s(%u)=%u", lookup(variableNames, sizeof(variableNames) / sizeof(*variableNames), type), type,
		   value);
	if ((type == 1 || type == 15) && value < mission->groupCount) {
		putchar(' ');
		quote(mission->groups[value].name, sizeof(mission->groups[value].name));
	}
	if (type == 7) {
		const char* state = value == 2    ? "Boarded"
							: value == 4  ? "Disabled"
							: value == 12 ? "Operational"
							: value == 16 ? "NotBoarded"
										  : NULL;
		if (state != NULL)
			printf(" [%s]", state);
	}
}

static void trigger(const DumpMission* mission, const MissionTrigger* t) {
	printf("%s(%u) ", lookup(conditionNames, sizeof(conditionNames) / sizeof(*conditionNames), t->condition),
		   t->condition);
	target(mission, t->variableType, t->variable);
	printf(" amount=%s(%u)",
		   lookup(amountNames, sizeof(amountNames) / sizeof(*amountNames), (uint8_t)t->amount),
		   (uint8_t)t->amount);
}

static void pair(const DumpMission* mission, const MissionTriggerPair* p) {
	putchar('(');
	trigger(mission, &p->triggers[0]);
	printf(" %s[%u] ", p->trigger1OrTrigger2 == 1 ? "OR" : "AND", p->trigger1OrTrigger2);
	trigger(mission, &p->triggers[1]);
	putchar(')');
}

static void order(const DumpMission* mission, const MissionOrder* o, unsigned index) {
	printf("  Order[%u] %s(%u) throttle=%u vars=%u,%u,%u,%u speed=%u designation=", index,
		   lookup(orderNames, sizeof(orderNames) / sizeof(*orderNames), o->order), o->order, o->throttle,
		   o->variable1, o->variable2, o->variable3, o->variable4, o->speed);
	quote(o->designation, sizeof(o->designation));
	printf("\n    primary: ");
	target(mission, o->target1Type, o->target1);
	printf(" %s[%u] ", o->target1OrTarget2 == 1 ? "OR" : "AND", o->target1OrTarget2);
	target(mission, o->target2Type, o->target2);
	printf("\n    fallback: ");
	target(mission, o->secondaryTargetTypes[0], o->secondaryTargets[0]);
	printf(" %s[%u] ", o->target3OrTarget4 == 1 ? "OR" : "AND", o->target3OrTarget4);
	target(mission, o->secondaryTargetTypes[1], o->secondaryTargets[1]);
	putchar('\n');
}

static void teams_enabled(const uint8_t* teams) {
	unsigned i;
	for (i = 0; i < 10; ++i)
		if (teams[i] != 0)
			printf(" %u:%u", i, teams[i]);
}

static void group(const DumpMission* mission, unsigned index) {
	const XvtFlightGroup* fg = &mission->groups[index];
	unsigned i;
	printf("\nFG[%u] @0x%zx ", index, 2 + sizeof(MissionHeader) + index * sizeof(*fg));
	quote(fg->name, sizeof(fg->name));
	printf(" species=%u craft=%u waves(raw)=%u IFF=%u team=%u AI=%u globalGroup=%u globalUnit=%u\n",
		   fg->craftType, fg->numberOfCraft, fg->numberOfWaves, fg->iff, fg->team, fg->groupAI,
		   fg->globalGroup, fg->globalUnit);
	printf("  role=");
	quote(fg->craftRole, sizeof(fg->craftRole));
	printf(" cargo=");
	quote(fg->cargo, sizeof(fg->cargo));
	printf(" specialCargo=");
	quote(fg->specialCargo, sizeof(fg->specialCargo));
	printf("\n  status=%u,%u player=%u playerCraft=%u arriveOnlyIfHuman=%u difficulty=%u\n", fg->status1,
		   fg->status2, fg->playerNumber, fg->playerCraft, fg->arriveOnlyIfHuman, fg->arrivalDifficulty);
	printf("  arrival: ");
	pair(mission, &fg->arrivalTriggers[0]);
	printf(" %s[%u] ", fg->arrivals12OrArrivals34 == 1 ? "OR" : "AND", fg->arrivals12OrArrivals34);
	pair(mission, &fg->arrivalTriggers[1]);
	printf(" delay=%u:%02u random=%u:%02u\n", fg->arrivalDelayMinutes, fg->arrivalDelaySeconds,
		   fg->arrivalRandDelayMinutes, fg->arrivalRandDelaySeconds);
	printf("  departure: ");
	pair(mission, &fg->departureTrigger);
	printf(" delay=%u:%02u abort=%u\n", fg->departureDelayMinutes, fg->departureDelaySeconds,
		   fg->abortTrigger);
	printf("  motherships: arrival=%u method=%u departure=%u method=%u alternate=%u used=%u captured=%u "
		   "method=%u\n",
		   fg->arrivalMothership, fg->arrivalMethod, fg->departureMothership, fg->departureMethod,
		   fg->alternateMothership, fg->alternateMothershipUsed, fg->capturedDepartureMothership,
		   fg->capturedDepartViaMothership);
	for (i = 0; i < 4; ++i)
		order(mission, &fg->orders[i], i);
	printf("  SkipToOrder[3]: ");
	pair(mission, &fg->skipToOrder4);
	putchar('\n');
	for (i = 0; i < 8; ++i) {
		const FlightGroupGoal* g = &fg->goals[i];
		printf("  Goal[%u] type=%u condition=%s(%u) amount=%s(%u) points(raw)=%d teams:", i, g->type,
			   lookup(conditionNames, sizeof(conditionNames) / sizeof(*conditionNames), g->eventCondition),
			   g->eventCondition,
			   lookup(amountNames, sizeof(amountNames) / sizeof(*amountNames), (uint8_t)g->amount),
			   (uint8_t)g->amount, g->points);
		teams_enabled(g->enabledTeams);
		printf(" timeLimit5s=%u sequence=%u\n", g->timeLimit5s, g->activeSequence);
	}
	for (i = 0; i < 22; ++i)
		if (u16(&fg->missionPointEnabled[i]) != 0)
			printf("  Point[%u] x=%d y=%d z=%d enabled=%u\n", i, (int16_t)u16(&fg->missionPointX[i]),
				   (int16_t)u16(&fg->missionPointY[i]), (int16_t)u16(&fg->missionPointZ[i]),
				   u16(&fg->missionPointEnabled[i]));
}

static int messages_and_goals(FILE* fp, const DumpMission* mission, unsigned messageCount) {
	unsigned i, j, count, index;
	uint8_t seen[64] = { 0 };
	for (i = 0; i < messageCount; ++i) {
		MissionMessage m;
		if (!read_word(fp, &index))
			return 0;
		if (index >= 64 || seen[index]) {
			fprintf(stderr, "mission_dump: invalid/duplicate message index %u\n", index);
			return 0;
		}
		seen[index] = 1;
		if (!read_exact(fp, &m, sizeof(m)))
			return 0;
		printf("\nMessage[%u] ", index);
		quote(m.message, sizeof(m.message));
		printf(" delay(raw)=%u teams:", m.rawDelay);
		teams_enabled(m.sentToTeam);
		printf("\n  ");
		pair(mission, &m.triggerPairs[0]);
		printf(" %s[%u] ", m.triggerPair1OrTriggerPair2 == 1 ? "OR" : "AND", m.triggerPair1OrTriggerPair2);
		pair(mission, &m.triggerPairs[1]);
		putchar('\n');
	}
	for (i = 0; i < 10; ++i) {
		if (!read_word(fp, &count))
			return 0;
		if (count > 7) {
			fprintf(stderr, "mission_dump: invalid global goal count %u\n", count);
			return 0;
		}
		for (j = 0; j < count; ++j) {
			GlobalGoal g;
			if (!read_exact(fp, &g, sizeof(g)))
				return 0;
			printf("\nTeam[%u] GlobalGoal[%u] ", i, j);
			quote(g.name, sizeof(g.name));
			printf(" points(raw)=%d delay(raw)=%u\n  ", g.rawPoints, g.rawDelay);
			pair(mission, &g.triggerPairs[0]);
			printf(" %s[%u] ", g.triggerPair1OrTriggerPair2 == 1 ? "OR" : "AND",
				   g.triggerPair1OrTriggerPair2);
			pair(mission, &g.triggerPairs[1]);
			putchar('\n');
		}
	}
	for (i = 0; i < 10; ++i) {
		Team t;
		if (!read_word(fp, &count))
			return 0;
		if (count == 0)
			continue;
		if (!read_exact(fp, &t, sizeof(t)))
			return 0;
		printf("\nTeam[%u] ", i);
		quote(t.name, sizeof(t.name));
		printf(" allies:");
		teams_enabled(t.allies);
		putchar('\n');
		for (j = 0; j < 6; ++j) {
			printf("  EndMessage[%u] ", j);
			quote(t.endOfMissionMessages[j], sizeof(t.endOfMissionMessages[j]));
			putchar('\n');
		}
	}
	return 1;
}

static int text_tail(FILE* fp, unsigned groupCount) {
	unsigned i, j, k, n, length;
	char script[820], buffer[65536];
	for (i = 0; i < 8; ++i) {
		if (!read_exact(fp, script, sizeof(script)))
			return 0;
		for (j = 0; j < 64; ++j) {
			if (!read_word(fp, &length) || !read_exact(fp, buffer, length))
				return 0;
			if (length != 0) {
				printf("Briefing[%u] %s[%u] ", i, j < 32 ? "Label" : "Text", j % 32);
				quote(buffer, length);
				putchar('\n');
			}
		}
	}
	printf("\nGoal text overrides @0x%lx (raw display-state slots)\n", ftell(fp));
	for (i = 0; i < groupCount + 10; ++i) {
		n = i < groupCount ? 8 : 28;
		for (j = 0; j < n; ++j)
			for (k = 0; k < 3; ++k) {
				if (!read_exact(fp, buffer, 64))
					return 0;
				if (buffer[0] != '\0') {
					if (i < groupCount)
						printf("FG[%u] Goal[%u] state[%u] ", i, j, k);
					else
						printf("Team[%u] GlobalGoal[%u] Trigger[%u] state[%u] ", i - groupCount, j / 4, j % 4,
							   k);
					quote(buffer, 64);
					putchar('\n');
				}
			}
	}
	/* Later editor metadata is outside Mission_LoadFile's consumed prefix. */
	printf("Consumed mission runtime data through 0x%lx\n", ftell(fp));
	return 1;
}

static int dump(FILE* fp) {
	DumpMission mission;
	unsigned version, messageCount, i;
	if (!read_word(fp, &version))
		return 0;
	if (version != 12 && version != 13 && version != 14) {
		fprintf(stderr, "mission_dump: unsupported version %u (expected XvT/BoP 12, 13, or 14)\n", version);
		return 0;
	}
	if (!read_exact(fp, &mission.header, sizeof(mission.header)))
		return 0;
	mission.groupCount = u16(&mission.header.numFlightGroups);
	messageCount = u16(&mission.header.numMessages);
	if (mission.groupCount > 48 || messageCount > 64) {
		fprintf(stderr, "mission_dump: invalid counts: %u groups, %u messages\n", mission.groupCount,
				messageCount);
		return 0;
	}
	if (!read_exact(fp, mission.groups, mission.groupCount * sizeof(*mission.groups)))
		return 0;
	printf("XvT/BoP version=%u flightGroups=%u messages=%u missionType=%d goalsUnimportant=%u\n", version,
		   mission.groupCount, messageCount, mission.header.missionType, mission.header.goalsUnimportant);
	printf("Indices are zero-based. Order targets are predicates; fallback is tried if primary finds no "
		   "target.\n");
	for (i = 0; i < mission.groupCount; ++i)
		group(&mission, i);
	return messages_and_goals(fp, &mission, messageCount) && text_tail(fp, mission.groupCount);
}

int main(int argc, char** argv) {
	FILE* fp;
	int ok;
	if (argc != 2) {
		fprintf(stderr, "Usage: %s mission.tie\n", argv[0]);
		return 2;
	}
	fp = fopen(argv[1], "rb");
	if (fp == NULL) {
		fprintf(stderr, "mission_dump: %s: %s\n", argv[1], strerror(errno));
		return 1;
	}
	ok = dump(fp);
	if (fclose(fp) != 0)
		ok = 0;
	if (fflush(stdout) != 0)
		ok = 0;
	return ok ? 0 : 1;
}
