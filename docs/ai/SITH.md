# Sith pilots: two skill levels above Jedi

If you can beat the best computer pilots in *Star Wars: X-Wing vs. TIE
Fighter* (1997), the Jedi, this project gives you two harder ones to fly
against: the Sith Apprentice and the Sith Lord. They exist only in
jedimaster-XVT; no 1997 mission holds them. This page says how to turn them
on, what each one does better than a Jedi, the numbers behind that, and
what is not built yet. Each part names the files where you can check it.

The main page on the computer pilots is
[How the computer pilots fly](README.md). In the story, the Sith use their
powers to predict your movements in space and time. In the game, that
means sharper aim and faster reactions, and for the Lord, more information
about the battle than any other computer pilot gets.

## Words used on this page

| Word | What it means here |
| --- | --- |
| Pilot program | The program in Python, in the folder `ai/`, that flies every computer craft. The game's own code, under `engine/`, is called the engine. |
| Skill level | Each computer craft's rank, set by the mission: 0 Rookie, 1 Officer, 2 Veteran, 3 Ace, 4 Top Ace, 5 Jedi. Level 6 is the Sith Apprentice and level 7 the Sith Lord. |
| Flight group | The craft a mission names together, such as "Alpha" or "Delta". All its craft share one skill level. |
| Wave | A group that flies more than once sends its craft again after the first ones are gone. The first craft of the first wave leads the group. |
| Distance | Kilometres and metres on this page are rounded from the game's own length unit: 40,700 units are 1 kilometre. |

## Turning them on

The launcher's battle settings have a Sith setting with 3 choices: None
(the default), Apprentices, and A Lord
(`launcher/jedimaster/launch/options.py`). For now it works in
single-player battles only.

When the battle starts, the engine picks one enemy flight group to become
the Sith flight. It looks at the groups no player can fly, on a team
hostile to yours, whose craft are starfighters, and takes the one with the
highest skill level; on a tie, the first one in the mission. With
Apprentices, that whole group flies at level 6. With A Lord, the group
flies at level 6 and its leader, the first craft of its first wave, flies
at level 7. Later waves of that group are Apprentices. When the mission
has no such group, nothing changes, and the message at the end of the
battle says so. The same mission and settings pick the same group every
time (`engine/src/xvt_runtime/runtime/sith.c`).

You can tell which craft are Sith in 3 places:

- **In the cockpit.** The targeting computer and the target camera add
  "Sith" after a Sith Apprentice's name and "Sith Lord" after a Sith
  Lord's (`engine/src/xvt_runtime/runtime/sith.c`).
- **When the battle starts.** The launch message adds "Sith Apprentices
  fly with the enemy." or "A Sith Lord flies with the enemy."
  (`launcher/jedimaster/page/launching.py`).
- **When it ends.** The message at the end names the Sith flight and its
  craft type, and says how the Lord and the Apprentices fared: destroyed,
  gone to hyperspace, or still flying (`launcher/jedimaster/launch/cli.py`).

Your pilot record counts a Sith kill as a Jedi kill, so the record keeps
its 1997 format (`engine/src/xvt/flight/ai_brain.c`).

## The same ships with better pilots

A Sith craft is an ordinary craft of its type: the same shields, hull,
lasers and warheads. Only the pilot is better. In the engine's tables,
levels 6 and 7 have a Jedi's skill value and think as often as a Jedi
(`engine/src/xvt/flight/ai_brain.c`). Whether a Sith Lord's craft should
also be tougher will be decided by test battles that count how often Sith
craft survive.

The table puts the three top ranks side by side. The sections below explain
each row.

| What | Jedi (5) | Sith Apprentice (6) | Sith Lord (7) |
| --- | --- | --- | --- |
| Decides what to do in a fight, every | 64 ms | 64 ms | 32 ms |
| Fires only when its aim is within this angle of its nose | 5.6 degrees | 4.2 degrees | 2.8 degrees |
| Aims ahead of its target by | the whole lead | the whole lead | the whole lead, along the target's turn, corrected by its tracers |
| Keeps a target this long before it checks for a nearer one | 1 second | 0.5 seconds | 0.26 seconds |
| Drops chaff or a flare at a homing warhead from | 0.15 km | 0.20 km | 0.25 km |
| Swerves from a homing warhead from | 0.15 km | 0.20 km | 0.20 km |
| Slows to one third throttle on a fighter's tail | no | yes | yes |
| Turns by the rule your own craft turns by | no | yes | yes |
| Changes speed at once | no | no | yes |
| Fires a warhead without a lock at | freighters, starships and platforms | freighters, starships and platforms | any craft, yours too |
| Retreats when badly damaged | yes | yes | never |

Sources: `ai/jedimaster_ai/behaviors/attack.py` for the decisions, the
target checks and the tail trick; `ai/jedimaster_ai/cannons.py` for the
angle and the lead; `ai/jedimaster_ai/behaviors/countermeasures.py`
for chaff and flares; `ai/jedimaster_ai/lord.py` and
`ai/jedimaster_ai/warheads.py` for the Lord's own rules.

## How they aim

Every computer pilot aims its lasers ahead of its target, at the point
where a shot fired now would meet the target if it flew straight on. That
distance ahead is called the lead. A Rookie takes none of it, each level
above takes a fifth more, and a Jedi and a Sith Apprentice take all of it
(`ai/jedimaster_ai/cannons.py`). The Apprentice also waits for a better
shot than a Jedi before it fires: its aim must be within 4.2 degrees of its
nose, up and down and left and right, where a Jedi fires within 5.6.

A Sith Lord aims better still at fighters and other small craft. The engine
sends the pilot program how each craft turned and changed speed over the
last moment, so the Lord aims where its shot meets the target on the path
it is turning along. Then it walks its shots in. About half of a Lord's
laser shots are tracers (those that land in an even-numbered slot in the
engine's list of shots), and the pilot program sees where each tracer
flies.
Each tracer that passed within 50 metres of the target since the Lord last
looked shows how far behind, beside or below the target it went. Half of
their average miss moves the Lord's aim, at most about 15 metres each way,
and the correction is kept in the target's own directions, so it turns
with the target. A new target starts with no correction. At a freighter,
starship or platform, the Lord aims as a Jedi does
(`ai/jedimaster_ai/lord.py`).

Against a fighter turning as hard as it can at 0.8 km, a Jedi's perfectly
aimed shot misses by about 21 metres and a Sith Lord's by under 1 metre
(`ai/jedimaster_ai/lord.py`).

The project measured the aim with an offline test program that ran each
skill level against an X-wing flying straight, turning, jinking (changing
direction suddenly) and looping, from 0.2 to 1.0 km. These are the shares
of cannon shots that hit:

| Skill level | Shots that hit |
| --- | --- |
| Rookie | 14% |
| Officer | 18% |
| Veteran | 28% |
| Ace | 36% |
| Top Ace | 44% |
| Jedi | 67% |
| Sith Apprentice | 60% |
| Sith Lord | 81% |

The Sith Apprentice hits less often than the Jedi in this test, and the
cause is known. On a fighter's tail, a Sith craft slows to one third
throttle to turn tighter (the next section). Against a target flying
straight or jinking, slowing down lets the target pull away, and shots
from farther back miss more. With that slowing switched off in the test,
the Apprentice hit 67% of its shots, the same as a Jedi. The same cause
makes the Sith Lord hit a jinking target less often than a Jedi does. The
test counts only hits per shot, so it cannot show what the slowing is for:
staying behind a target that turns hard. Test flights in the game have not
checked these numbers yet; this section will be updated with what they
find.

## The tail trick, and turning by your rules

Players of the 1997 game learned that a fighter turns fastest at one third
throttle. The engine scales your turn by your throttle: a third of your
craft's rate at 0%, all of it at 33%, five sixths at 67% and two thirds at
full throttle (`engine/src/xvt/flight/player/player.c`). Computer pilots
below the Sith ranks turn at one rate at every throttle
(`engine/src/xvt/flight/flight.c`), so slowing down does not help them
turn faster.

Sith Apprentices and Sith Lords turn, loop and bank by your rule: at each
throttle, at the rate your craft of the same type reaches with the stick
pushed all the way over and the power balanced, and never slower than
other computer pilots of their type
(`engine/src/xvt_runtime/runtime/sith.c`). And they use the trick. When a
Sith craft sits within 0.4 km behind a starfighter it is attacking, within
45 degrees of straight behind it, and the target is not disabled, it drops
to one third throttle. It throttles up again once the target breaks away
(`ai/jedimaster_ai/behaviors/attack.py`).

So a Sith X-wing loops at about 74 degrees a second at one third throttle,
where other computer X-wings loop at about 30. Left and right, a Sith craft
turns as fast as other computer craft, or up to 2.4% faster near one third
throttle, and it banks 6% faster there. A Sith craft whose engines are shot
out turns as at zero throttle, and still never slower than other computer
pilots. The measurements for all 14 fighters are in the main page's
[Throttle and turning, measured](README.md#throttle-and-turning-measured).

## What only a Sith Lord does

- **It changes speed at once.** Other craft speed up and slow down over
  time; when a Sith Lord sets its throttle, it is at that speed straight
  away, up to the top speed any craft can reach by speeding up. A Lord
  whose engines are out, or that a tractor beam holds, slows as any craft
  does (`engine/src/xvt/flight/flight.c`).
- **It never retreats.** Other pilots below three quarters of their hull
  weave away from an enemy behind them instead of fighting it. A Sith Lord
  fights every enemy after it however damaged it is, and its flight
  group's abort rules never send it home or end a Drop Off order early.
  It still leaves when the mission's time to leave comes, or when its
  orders are done (`ai/jedimaster_ai/lord.py`).
- **It fires warheads without a lock at any craft**, your fighter too,
  once its shot lines up. A Jedi or a Sith Apprentice fires them without a
  lock only at freighters, starships and platforms. Against a fighter the
  Lord fires one such shot in each attack run, the same limit every pilot
  keeps for warheads at small craft (`ai/jedimaster_ai/warheads.py`).
- **It decides twice as often in a fight**: every 32 milliseconds, where
  every other pilot decides every 64 (`ai/jedimaster_ai/behaviors/attack.py`).

## Answering your missiles

A computer starfighter with chaff or a flare ready drops one when a homing
warhead comes within a range set by its skill; a flare goes only at a
warhead that nothing has drawn off yet. A Jedi answers from 0.15
km, a Sith Apprentice from 0.20 km and a Sith Lord from 0.25 km. Each
answers concussion missiles from 3 times as far: 0.45, 0.60 and 0.75 km
(`ai/jedimaster_ai/behaviors/countermeasures.py`).

When an enemy is on its tail, a craft weaves: it swerves from side to
side, at most 3 times, to shake the enemy off. A homing warhead coming at
it sets off the same weave: a Jedi swerves from 0.15 km, a Sith Apprentice
and a Sith Lord from 0.20 km, and each from 3 times as far for a concussion
missile. From Ace up, Sith included,
a craft that carries flares may drop one at its second and third swerve, 1
time in 4 (`ai/jedimaster_ai/weave.py`). While it weaves, a Jedi or a Sith
craft moves all its shield recharge power to its engines and gains an
eighth of its top speed (`engine/src/xvt/flight/ai_brain.c`).

## Not built yet

Until these are built, Sith pilots do these things with a Jedi's skill, or
not at all:

- **Shooting warheads down with lasers.** Planned: a Sith Apprentice at
  warheads chasing itself, a Sith Lord at any warhead.
- **Ion cannons and a big ship's weak points.** Planned for every level,
  at full strength for both Sith ranks: ion cannons to disable a craft
  whose shields are down, and shooting a big ship's shield generators and
  guns before its hull.
- **The Lord's full picture of the battle.** Today a Lord uses each
  craft's last turn and change of speed, and its own tracers. Planned: the
  position and speed of every craft and every warhead in flight.
- **Sith in network battles.** The setting works in single-player battles
  only.
- **Tougher craft.** Only if test battles show Sith Lords die too easily.

## Where to check

| File | What it holds |
| --- | --- |
| `launcher/jedimaster/launch/options.py` | the Sith setting and its 3 choices |
| `engine/src/xvt_runtime/runtime/sith.c` | which group is promoted, the rank names in the cockpit, the turning rule |
| `engine/src/xvt/flight/ai_brain.c` | the skill tables by level, a Sith kill counted as a Jedi kill |
| `engine/src/xvt/flight/flight.c` | the Lord's speed change |
| `ai/jedimaster_ai/tactics.py` | how the pilot program sorts levels 6 and 7 into its two top tiers |
| `ai/jedimaster_ai/cannons.py` | aim angle, range and lead by skill |
| `ai/jedimaster_ai/behaviors/attack.py` | decisions, target checks, the tail trick, fighting back |
| `ai/jedimaster_ai/lord.py` | the Lord's turn-following aim, tracers and never retreating |
| `ai/jedimaster_ai/warheads.py` | warheads without a lock |
| `ai/jedimaster_ai/behaviors/countermeasures.py` | chaff and flares by skill |
