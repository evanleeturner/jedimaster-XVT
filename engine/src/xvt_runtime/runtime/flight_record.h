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

/* With DEBUG lines on and a craft at the Sith Lord rank flying (not breaking
 * up or exploding), writes one world.tracer_track line for each tracer shot
 * (ai_tracer_slot_is_tracer) born in the last window ticks (younger than
 * window) whose firer is a Sith Lord or a player's craft, in slot order: its
 * slot, firer, the firer's player (-1 for none), type, position, facing, speed
 * and age. A bolt flies straight, so the line that follows its birth gives its
 * whole path. Writes nothing with DEBUG off. xvt_flight_record_warhead_beat
 * calls it after the warhead lines, with the ticks since its last beat. */
void xvt_flight_record_tracers(int tick, int window);

/* Writes the warhead lines (as xvt_flight_record_warheads) with to_tick as
 * the tick when a step from from_tick to to_tick (0 <= from_tick) crossed or
 * landed on a multiple of XVT_WARHEAD_TRACK_TICKS, then the tracer lines (as
 * xvt_flight_record_tracers) for the bolts born since the last beat, so each
 * tracer is written once however many ticks a step covers: a slow frame can
 * step 60 ticks and more, and a fixed window of XVT_WARHEAD_TRACK_TICKS then
 * missed every bolt born early in the step. The first beat, and one whose
 * tick is not after the last (a new flight), takes XVT_WARHEAD_TRACK_TICKS,
 * and every window covers the step's own ticks at least. A step that crosses
 * two beats writes once. Writes nothing otherwise or with
 * DEBUG off. */
void xvt_flight_record_warhead_beat(int from_tick, int to_tick);

/* The record for a step of a flight that does not confirm world messages
 * (a solo battle): the warhead beat, and the record point (as
 * xvt_flight_record_point) when the step crossed or landed on a multiple of
 * XVT_WORLD_CHECKSUM_TICKS, each with to_tick as the tick. */
void xvt_flight_record_step(int from_tick, int to_tick);

#ifdef __cplusplus
}
#endif

#endif
