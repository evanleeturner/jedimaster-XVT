#ifndef XVT_RUNTIME_FLIGHT_RECORD_H
#define XVT_RUNTIME_FLIGHT_RECORD_H

#ifdef __cplusplus
extern "C" {
#endif

/* A record point: at a world checksum tick every machine in a flight holds
 * the same confirmed world, so a record of the craft taken there matches
 * from machine to machine line for line, and a script can judge a flight
 * from its logs without pictures. */

/* With DEBUG lines on, writes one record.point line for tick, then one
 * world.track line for each craft slot holding a craft with a craft record,
 * in slot order: its identity, flight group, player and team, position,
 * facing, speed, throttle, state, hull, shields and targets. A slot in use
 * without a craft record is counted in record.point and not tracked. Reads
 * the world only; writes nothing with DEBUG off. */
void xvt_flight_record_point(int tick);

/* With DEBUG lines on, writes one world.warhead_track line for each warhead
 * in flight, in slot order: its identity, firer, target, homing tier,
 * position, facing and speed. A warhead is a shot whose type has a nonzero
 * warhead_class, so laser bolts are left out and countermeasures are in.
 * Only the shot slots are read. Writes nothing with DEBUG off. */
void xvt_flight_record_warheads(int tick);

/* Writes the warhead lines (as xvt_flight_record_warheads) with to_tick as
 * the tick when a step from from_tick to to_tick (0 <= from_tick) crossed or
 * landed on a multiple of XVT_WARHEAD_TRACK_TICKS. A step that crosses two
 * beats writes once. Writes nothing otherwise or with DEBUG off. */
void xvt_flight_record_warhead_beat(int from_tick, int to_tick);

#ifdef __cplusplus
}
#endif

#endif
