# Ticks: how the game counts time

Everything in a flight is timed in ticks: steps, timers, network
messages, checksums. Two numbers sit close together in the code, 250 and
236, and they are easy to confuse. This page says what each one means,
where a tick comes from, and every tick length the engine names. It is
the reference: when a note, a comment or a plan disagrees with it, the
page and the code win.

## The short answer

| Unit | Ticks | Real time | Where it is defined |
| --- | --- | --- | --- |
| one tick | 1 | 4 ms | `engine/src/xvt/util/time.c` |
| one real second | 250 | 1,000 ms | the same file |
| one simulated second | 236 | 944 ms | `SIMULATION_TICKS_PER_SECOND`, `engine/src/xvt/flight/flight.h` |
| one reference step | 8 | 32 ms | `XVT_REFERENCE_TICKS`, `engine/src/xvt_runtime/runtime/flight_protocol.h` |
| one network step | 2 | 8 ms | `XVT_NETWORK_STEP_TICKS`, the same file |

Three rules follow from the table:

- To turn ticks into real time, multiply by 4 ms.
- To turn one of the game's own "per second" figures into ticks, use 236.
- Never use 236 to mean a real second. A real second is 250 ticks.

## Where a tick comes from

```text
SDL's clock, in nanoseconds
  -> Aeron_NowUs: microseconds
  -> Aeron_BeginFrame: the gap since the last frame, clamped to 250 ms
  -> xvt_port_update -> xvt_time_advance_host_clock: the host clock grows
     by the gap (skipped on paused, out-of-focus and rebase frames)
  -> timeGetTime: the host clock's whole milliseconds
  -> time_consume_elapsed_ticks: (now - last) >> 2, the rest carried
  -> g_input_timestamp grows by those ticks; the simulation steps to it
```

The host clock is `engine/src/xvt_runtime/timing/host_clock.c`. It
answers the 1997 code's calls to the Windows functions `timeGetTime` and
`GetTickCount`, so the old code reads the port's clock, not the
computer's.

`time_consume_elapsed_ticks` keeps the time of the last whole tick it
handed out. Each call returns the whole 4 ms pieces since then and moves
that time forward by 4 ms for each one, so the part under 4 ms waits for
the next call. Nothing drifts, however uneven the frames are. At 60
frames a second a frame gets 4 or 5 ticks; at 144 it gets 1 or 2.

## The two seconds

The 1997 game cuts its clock into 4 ms ticks, 250 to a second. Its
flight code, though, states rates per 236 ticks:

- acceleration and deceleration: the per-second figure times the step's
  ticks over 236 (`engine/src/xvt/flight/flight.c`);
- a shot's lifetime: 236 ticks for each second of its listed lifetime
  (`engine/src/xvt/flight/object/laser.c`);
- the flight's once-a-second work runs once per 236 ticks;
- the world checksum runs every 472 ticks, two simulated seconds.

So a simulated second lasts 944 ms of real time, and the flight runs
250 / 236 = 1.059 times as fast as its own per-second figures. A shot
listed as lasting 2 seconds lives 472 ticks, 1.888 real seconds. The
code does not say why the number is 236. This port keeps both numbers
because the shipped game's behavior depends on them.

## Simulation steps

A step moves the flight forward by `g_elapsed_ticks`. How many ticks a
step covers depends on the flight's timing profile, which
`engine/src/xvt_runtime/runtime/flight_task.c` picks at flight start:

| Profile | When | Ticks per step | Reference logic |
| --- | --- | --- | --- |
| native | a solo flight, the `flight_unlocked` setting off | at least 8, following the recent frame length | every step |
| offline unlocked | a solo flight, the `flight_unlocked` setting on | 1 | once per 8 ticks |
| network 125 | every other flight | 2 (8 ms, 125 steps a second) | once per 8 ticks |

Reference logic is code written for the 1997 step, such as the computer
pilots. The files that ask whether it is due include `flight.c`,
`mission.c`, `laser.c`, `collide.c` and `flight_sim.c`. In the unlocked
profiles it runs only on the step that crosses an 8-tick boundary, with the
step globals set to one 8-tick period while it runs
(`engine/src/xvt_runtime/timing/flight_timing.h`).

`g_sim_steps_per_second` is 236 divided by the step's ticks, cut to a
whole number and at least 1. For an 8-tick step it is 29, not 29.5.

## Every tick length the engine names

All in `engine/src/xvt_runtime/runtime/flight_protocol.h`, except the
last row:

| Name | Ticks | Real time | What it times |
| --- | --- | --- | --- |
| `XVT_OFFLINE_STEP_TICKS` | 1 | 4 ms | the unlocked solo step |
| `XVT_NETWORK_STEP_TICKS` | 2 | 8 ms | the network step |
| `XVT_INPUT_BATCH_TICKS` | 4 | 16 ms | one batch of player input |
| `XVT_NATIVE_STEP_TICKS` | 8 | 32 ms | the least a native step waits for |
| `XVT_REFERENCE_TICKS` | 8 | 32 ms | the 1997 step |
| `XVT_WORLD_MESSAGE_TICKS` | 8 | 32 ms | the network world message |
| `XVT_COMPONENT_TIMER_TICKS` | 29 | 116 ms | the special-behavior update timer's period |
| `XVT_COMPONENT_EVENT_TICKS` | 32 | 128 ms | the unit of the animation update count in network play |
| `XVT_RESYNC_RETRY_TICKS` | 236 | 944 ms | a resync retry, one simulated second |
| `XVT_PREDICTION_LEAD_TICKS` | 256 | 1,024 ms | how far prediction may run ahead |
| `XVT_WORLD_CHECKSUM_TICKS` | 472 | 1,888 ms | the world checksum, two simulated seconds |
| `g_net_update_interval_ticks`, `engine/src/xvt/net/flight_net.c` | 29 at start | 116 ms | the longest step outside network play; the frame loop resets it each step |

Three nearby names are not tick lengths:

- `XVT_FLIGHT_TICK_US` is 4,000: the tick in microseconds.
- `XVT_SIM_BUDGET_US` is 2,000 microseconds of computer time the
  simulation may use per host frame.
- `XVT_INPUT_HISTORY_CAPACITY` is 450 input records, not 450 ticks.

## When the clock slows or stops

- **A long frame.** The frame timer clamps one frame's gap to 250 ms.
  A frame that takes 1 second adds 250 ms, so the game runs at a quarter
  speed while frames are that slow.
- **The settings menu or a lost focus.** Outside network play, the host
  clock does not move while the settings menu is open or the window is
  out of focus. Network play keeps the clock moving.
- **A rebase frame.** On start-up and resume the port skips one frame's
  gap, so stale time does not arrive all at once.
- **Flight start.** `time_reset_elapsed_ticks` clears the last tick
  time. The next call returns 0 and counts from there.
- **The 32-bit wrap.** The host clock's milliseconds wrap after about
  49.7 days. The tick count's subtraction is unsigned, so the count stays
  right across the wrap.

## How this was measured

No game ran. A small C program linked the port's real host clock
(`host_clock.c`) and tick counter (`time.c`), fed them frames of a set
length, with the frame timer's 250 ms clamp applied, and counted the
ticks. Each row ran 60 seconds of frames, except the 1 microsecond row,
which ran 1 second.

| Frame length | Frames | Ticks | Ticks a second | Ticks per frame |
| --- | --- | --- | --- | --- |
| 1 microsecond | 1,000,000 | 250 | 250.0 | 0 or 1 |
| 3,999 microseconds | 15,004 | 15,000 | 250.0 | 0 or 1 |
| 6,944 microseconds (144 a second) | 8,641 | 15,000 | 250.0 | 1 or 2 |
| 7,321 microseconds | 8,196 | 15,000 | 250.0 | 1 or 2 |
| 16,667 microseconds (60 a second) | 3,600 | 15,000 | 250.0 | 4 or 5 |
| 33,333 microseconds (30 a second) | 1,801 | 15,008 | 250.0 | 8 or 9 |
| 1 second, clamped to 250 ms | 60 | 3,750 | 62.5 | 62 or 63 |

The 30-a-second row ran 33 ms past the minute, which is where its extra 8
ticks come from. The step lengths and named tick lengths above are read
from the source, at the lines named.
