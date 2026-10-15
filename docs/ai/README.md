# How the computer pilots fly

If you have played *Star Wars: X-Wing vs. TIE Fighter* (1997) and then
played it here, you may have seen the computer-flown craft behave
differently: they steer clear of big ships, swerve to shake off an enemy
behind them, and drop chaff at homing missiles. This page lists what changed, what stayed as in
1997, what is not built yet, and which problems are known. Each topic names
the files in this repository where you can check it.

jedimaster-XVT runs the game on an engine that came from
[OpenXvT](https://github.com/elyosh/OpenXvT), written by elyosh (Adrien
Moulin) and released under the GPLv3. In this project the 1997 computer
pilots were deleted and replaced by a new program written in Python, in the
folder `ai/`. This page calls it the pilot program. The engine still keeps
the mission's own records (who has left, which goal is met); the pilot
program decides what each craft does next. Where this page says "the
engine", it means the game's own code under `engine/`.

The page grows as new behaviors arrive, so it will keep getting longer.

## Words used on this page

| Word | What it means here |
| --- | --- |
| Order | The job a mission gives a flight group, such as Attack, Hold Steady or Go Home. There are 40 order codes. |
| Skill tier | The mission gives each computer craft a skill level from 0 to 5. The pilot program sorts them into 3 tiers: levels 0 and 1 are tier 0, level 2 is tier 1, levels 3 to 5 are tier 2. Tier 2 is the best. Which level the game menus call "novice" or "ace" is not covered here. |
| Hull, shields | Hull is the craft's body. A craft is at "75% hull" when a quarter of its hull is gone. Shields are counted separately and never count toward hull. |
| Chaff and flares | Two decoys a craft drops against homing missiles and torpedoes. |
| The weave | A short series of sideways swerves a craft flies to shake off an enemy behind it. The 1997 game called it the scissors. |
| Big ships | Freighters, starships and platforms. |
| Clearance | The distance a craft keeps from a big ship. For a target it is three quarters of the target's size plus 4,096 units. |
| Unit | The game's own length. 40,700 units are one kilometre, the same scale the game's heads-up display uses (`ai/jedimaster_ai/tactics.py`). Kilometres on this page are rounded. |
| Tick, game second | The game updates every 4 milliseconds (a tick, see `TICK.md`). The game counts 236 ticks as one game second, which lasts 944 milliseconds of real time. Times on this page are real seconds unless it says "game seconds". |
| Decision | The moment a craft's pilot looks at the situation and picks what to do next. |

## What this page covers

Each topic below has a short explanation and a table. The columns are what
the computer pilots do now, what the 1997 game did, whether the behavior was
kept, changed or is new, and the issues on GitHub that touch it. "Unknown"
in the 1997 column means this project has not established what the 1997
game did. The tables list a behavior only when this project has checked it
against the code.

## Fighting and dogfights

The pilot program fights with cannons and guided warheads. A pilot picks the nearest craft its
order names, turns toward it, waits for a good shot and fires in bursts.
Better tiers wait for a tighter aim and look for a nearer target more
often. The target-search timings follow the open-source game Starshatter
Open, used as a model; no code was copied from it. The code for this topic
is in `ai/jedimaster_ai/behaviors/attack.py` and `ai/jedimaster_ai/tactics.py`.

| What the computer pilots do now | In the 1997 game | Kept, changed or new | Related issues |
| --- | --- | --- | --- |
| Fight only craft the game marks hostile, active or disabled, and never their own team (`ai/jedimaster_ai/tactics.py`). | Unknown | New | |
| Pick the nearest craft the order names, and look for a nearer one every 6, 3 or 1 seconds (1,500, 750 or 250 ticks) in tier 0, 1 or 2 (`behaviors/attack.py`). | Unknown | New | |
| Fire when the target is inside a cone of 11, 8 or 6 degrees in tier 0, 1 or 2 (`behaviors/attack.py`). | A cone of 11 degrees at every tier. | Changed | |
| Fire from 24,576, 32,768 or 40,960 units (0.6, 0.8 or 1.0 km) in tier 0, 1 or 2. The range shrinks by 16,384 units (0.4 km) for a fast target flying the same way and by 8,192 (0.2 km) for one crossing, and grows by 24,576 against a starship or platform (`behaviors/attack.py`). | The same ranges and changes. | Kept | |
| Fire all cannons together inside 8,192 units (0.2 km), in pairs inside 16,384 (0.4 km), and one at a time farther out. Bursts are 3, 4 or 5 shots long by tier. Ion cannons hold: only a Disable order fires them, at its target, where every other cannon holds; ion turbolasers fire as other cannons. | The burst lengths are 1997's. How cannons were linked is unknown. The ion cannon fired only on the disable plan with a live target. | Kept | |
| Aim where the target is now and do not lead it. | The same. | Kept | |
| Decide every 16 ticks (64 milliseconds) at every skill level. Skill changes the aim, not the reaction time. | Every 29 to 708 ticks (0.1 to 2.8 seconds), slower at lower levels. | Changed | |
| Fly at full throttle in a fight, drop to two thirds once the target is caught (inside 8,192 units and dead ahead), and tier 0 flies at no more than seven tenths of full throttle. | Unknown | New | |
| Break off when too close: inside the target's clearance with the target within 45 degrees of the nose, or anywhere near a freighter, starship or platform. The craft turns a quarter turn away from the target's side and holds that heading until it is outside the clearance, for at most 1,000 ticks (4 seconds). The time limit stops a target that paces the craft from keeping it from firing. | Unknown | New | |
| When a second big ship blocks the way out of a break, head away from both, climbing or diving as they lie. Fighters used to break away from one freighter straight into the next (`behaviors/attack.py`, `behaviors/avoidance.py`). | Unknown | New | |
| Fire a guided warhead once the lock on the target is declared, lined up for the cannons or not. Concussion missiles go first at fighters, and the heaviest warhead first at freighters, starships and platforms. Space bombs, heavy rockets and type 154 never go at smaller craft; magnetic and ion pulses go only at smaller craft. A craft whose order names a big ship keeps its proton torpedoes for it. No shot while the homing warheads already flying at the target would destroy it, their damage divided by 16 at a starship or platform and by 4 at a freighter; a Disable order tests the target's shield instead (the Disable row below). At most 2 are in flight at a smaller craft and 16 at a big ship, and an attack run fires at most 1 at a smaller craft and 6 at a big ship. After a shot at a fighter or a transport, the lock must drop and be built again. One warhead at a time, never two together (`ai/jedimaster_ai/warheads.py`). | Fired only in a rocket attack, once the lock its tier needed was built (629 to 1,416 ticks). A warhead went only at targets of its class, so proton torpedoes and rockets went only at big ships. The same limit and caps. Two together at the top tier when hurt. | Changed | [#299](https://github.com/evanleeturner/jedimaster-XVT/issues/299), [#305](https://github.com/evanleeturner/jedimaster-XVT/issues/305) |
| Count a warhead lock on the craft's target by the player's rule: the same aim cone and lock range, chaff on the target taking half the time back, a full lock at 708 ticks (354 on a Missile Boat). The program is told the lock, each craft's skill level and the homing warheads already flying at each craft, and fires on the lock (row above; `engine/src/xvt/flight/ai_warheads.c`). | Computer craft counted a lock of their own, by skill tier. | Changed | [#299](https://github.com/evanleeturner/jedimaster-XVT/issues/299) |
| Your cockpit counts a computer craft as attacking you when its target is you, unless it is flying home to its mothership. Such a craft lights the attack light while its cannons are linked and the lock warning while its lock builds, and the E key picks it. The A key finds the craft attacking your target the same way. The warhead light of the craft you have targeted shows while it attacks anything. | The cockpit read the 1997 pilot's attack and warhead-run maneuvers. Since those pilots were removed, all of these stayed dark for computer craft. | Changed | [#304](https://github.com/evanleeturner/jedimaster-XVT/issues/304) |
| Tier 0 craft never get inside firing range of a Star Destroyer: the range is 49,152 units (1.2 km), measured to its centre, and the craft starts to break off at its clearance of 53,143 units (1.3 km). No tier reaches a Super Star Destroyer. | Unknown | New (a side effect) | [#302](https://github.com/evanleeturner/jedimaster-XVT/issues/302) |

### Throttle and turning, measured

Players of the 1997 game learned a trick: at one third throttle a fighter
turns fastest. This project measured it in this engine on 2026-10-15, for
craft a player flies and for craft the pilot program flies. A small test
program ran the game's own flight code without a game window and turned
14 fighters at 0%, 33%, 67% and 100% throttle. The rates below are
degrees per second of real time.

For a craft a player flies, the trick holds on all 14 fighters, for
turning left and right and for looping. The engine scales a player's turn
by throttle: a third of the craft's rate at 0%, all of it at 33%, five
sixths at 67% and two thirds at full throttle
(`engine/src/xvt/flight/player/player.c`).

A computer-flown craft turns at one rate at every throttle
(`engine/src/xvt/flight/flight.c`), so slowing down only makes its circle
smaller. Left and right it turns about as fast as a player at 33%, which
at full throttle is half again faster than a player. Its loop is under
half a player's best, so a player at one third throttle out-loops it by
about 2.5 to 1. This is the 1997 game's own turning; the pilot program
only picks the throttle.

| Fighter | Loop, player at 33% | Loop, player at 100% | Loop, computer | Turn, player at 33% | Turn, player at 100% | Turn, computer |
| --- | --- | --- | --- | --- | --- | --- |
| X-wing (X-W) | 73.8 | 49.1 | 29.7 | 56.9 | 37.9 | 59.6 |
| Y-wing (Y-W) | 59.1 | 39.3 | 23.7 | 44.3 | 29.5 | 47.6 |
| A-wing (A-W) | 88.6 | 59.1 | 35.7 | 75.7 | 50.6 | 74.3 |
| B-wing (B-W) | 66.4 | 44.3 | 26.8 | 53.7 | 35.9 | 53.6 |
| Z-95 Headhunter (Z-95) | 81.2 | 54.1 | 32.6 | 66.3 | 44.3 | 71.4 |
| R-41 Starchaser (R-41) | 77.6 | 51.7 | 31.1 | 55.2 | 36.9 | 58.0 |
| T-wing (T-W) | 81.2 | 54.1 | 32.6 | 56.9 | 37.9 | 56.5 |
| TIE Fighter (T/F) | 96.0 | 64.0 | 38.4 | 72.7 | 48.4 | 71.4 |
| TIE Interceptor (T/I) | 103.3 | 68.8 | 41.5 | 79.0 | 52.6 | 77.4 |
| TIE Bomber (T/B) | 81.2 | 54.1 | 32.6 | 66.3 | 44.3 | 65.4 |
| TIE Advanced (T/A) | 103.3 | 68.8 | 41.5 | 79.0 | 52.6 | 77.4 |
| TIE Defender (T/D) | 118.1 | 78.8 | 47.6 | 79.0 | 52.6 | 77.4 |
| Assault Gunboat (GUN) | 66.4 | 44.3 | 26.8 | 63.3 | 42.2 | 62.5 |
| Missile Boat (MIS) | 59.1 | 39.3 | 23.7 | 82.0 | 54.7 | 80.3 |

A fan-made stat sheet for the 1997 game timed each fighter's best turn at
"33% throttle, 100% (normal) engines" with a stopwatch
([X-Wing Vs. TIE Fighter Stats](https://home.cc.umanitoba.ca/~chrstie/gam_xvt.html)).
The engine's rates left and right fall within 15% of the sheet's for 11 of
its 12 fighters. The engine's loops come out 13% to 67% faster than the
sheet's for 7 of them.

Sith Apprentices and Sith Lords will use the trick. On a target's tail
they drop to one third throttle, and they will turn and loop by the
player's rule, so the trick works for them as it works for you. Every
other computer pilot keeps the 1997 turning.

## Defending yourself

An enemy behind a craft is the dangerous case. The weave is the main answer,
and every starfighter that carries chaff or flares now uses them against
homing shots. The 1997 weave used dice and ran long; the new one is short
and watches whether the enemy is still behind. The code is in
`ai/jedimaster_ai/weave.py` and the files in `ai/jedimaster_ai/behaviors/`
named in the table.

| What the computer pilots do now | In the 1997 game | Kept, changed or new | Related issues |
| --- | --- | --- | --- |
| Weave every time an enemy within 32,768 units (0.8 km) sits more than 135 degrees off the nose, is targeting the craft and is not disabled, and the craft's own shot is not lined up. The weave is up to 3 swerves of 67.5 degrees to either side over 5.7 to 7.5 seconds and ends the moment the enemy is no longer behind. After a full weave the craft fights for about 7.5 seconds before it weaves again (`ai/jedimaster_ai/weave.py`). | By dice: 1 chance in 4 with an enemy behind, 1 in 8 when attacked. 4 to 8 swerves over 10 to 17 game seconds, flown without looking at the chaser. | Changed | [#46](https://github.com/evanleeturner/jedimaster-XVT/issues/46) |
| Drop a flare at a swerve in tier 2, at the second and third swerve, 1 chance in 4, when a flare is ready. | At each swerve, level 3 or higher, 1 chance in 4. | Kept | |
| Drop chaff or flares against the first homing shot inside 2,048, 4,096 or 6,144 units (0.05, 0.10 or 0.15 km) by tier, and 3 times that for concussion missiles. Every starfighter that carries them does this on any order, going home included. Chaff goes out when none is running; a flare goes out when nothing is chasing that shot yet and the 472-tick cooldown (about 1.9 seconds) is over (`behaviors/countermeasures.py`). | The same ranges, but only some orders answered a homing shot. | Changed: wider | [#182](https://github.com/evanleeturner/jedimaster-XVT/issues/182), [#210](https://github.com/evanleeturner/jedimaster-XVT/issues/210) |
| Fire turrets, on every craft, at hostile craft within 65,536 units (1.6 km) that target it, last hit it, or are among its order's targets. Nearest first, shared over the turrets (`behaviors/turrets.py`). On SS Protect, also at the enemies attacking the craft it protects; on SS Patrol and Disable, ion shots at the order's targets that still work, and never at a disabled craft. | Turrets fired by the rules of each order. Hold Steady had no turret rule, and only starship orders used turrets offensively. | Changed | [#103](https://github.com/evanleeturner/jedimaster-XVT/issues/103) |
| When attacked on the way home, a small enemy (a fighter or a player) that hit the craft or sits behind it and targets it turns the craft round to fight, while its hull is at 75% or more. Below that it flies home weaving. A freighter, starship or platform firing at it never turns it. It goes home again once the enemy is dead, gone or aims elsewhere. At the door (jumping, turning in or docking) it carries on (`behaviors/home.py`). | Flew straight on under fire. It dodged warheads and turned off a starship it would hit. | Changed | |
| An Attack craft that is chased before its targets arrive fights the chaser at 75% hull or more, otherwise weaves, and then waits for its targets again. A disabled craft is never a chaser. | Unknown | New | |
| With no hyperdrive there is no jump on any order. With no jump and no mothership the craft fights the nearest enemy at any hull, if it has cannons and can move. A mothership that arrives takes it home. | A craft flew to its point and stayed. A starship jumped to hyperspace with no hyperdrive. | Changed | |
| Starships have no missile defense: nothing fires at homing warheads or attackers. | Starship launchers fired at homing warheads and at attackers. | Changed: not built yet | [#49](https://github.com/evanleeturner/jedimaster-XVT/issues/49) |

## Big ships and collisions

Gunboats rammed cruisers in test flights, and freighters and corvettes of
one group collided with each other. Every craft now steers clear of big
ships. The latest change (commit `9886841`) keeps a fighter that sits between two big ships from
turning into either one. The code is in `ai/jedimaster_ai/behaviors/avoidance.py`.

| What the computer pilots do now | In the 1997 game | Kept, changed or new | Related issues |
| --- | --- | --- | --- |
| Every craft steers clear of freighters, starships and platforms from 8,192 units (0.2 km) outside the clearance. It turns toward the edge of the ship on the side its nose points to. It never swings its nose across a big ship whose clearance it is inside; that turn goes the long way round. Between two big ships it picks the widest pass. A craft's own target counts as an obstacle during a break (`behaviors/avoidance.py`). | Go Home turned off a starship it would hit. | Changed | |
| Big ships steer clear of each other. A starship that stopped to turn round another moves on again (`behaviors/avoidance.py`, `behaviors/patrol.py`). | Followers were pushed into formation places. | Changed | [#296](https://github.com/evanleeturner/jedimaster-XVT/issues/296) |
| A craft going home targets its own mothership, so steering never turns it off its own hangar. Before this change, 1 of 12 launches in a networked test flight flew into its carrier. | Unknown | Changed | |

## Orders, formations and wingman commands

An order is the mission's instruction to a flight group. The pilot program
flies 23 of the 40 order codes today: Attack, Disable, Protect, Escort, Wait, Wait 2,
Go Home, SS Go Home, the four patrol orders, SS Protect, SS Protect 2, SS
Patrol and Disable, SS Await Return and the 7 Hold Steady codes. The code is in
`ai/jedimaster_ai/orders.py`. The rest keep their speed and heading
until someone builds them, so a craft on an unbuilt order flies straight on.
Disabled craft and craft on the null order start stopped. Rendezvous and
Attack Escorts are among the orders still unbuilt. Until the game engine
names the craft to escort and its station, an Escort craft holds its course
and fights only a chaser.

| What the computer pilots do now | In the 1997 game | Kept, changed or new | Related issues |
| --- | --- | --- | --- |
| An order with no behavior yet: the craft keeps its speed and heading (`ai/jedimaster_ai/orders.py`, `behaviors/drift.py`). | Flown by the 1997 pilot script for that order. | Changed, for now | |
| The 7 Hold Steady codes: throttle 0, no conditions, never moves on to a later order. The craft's turrets defend it (`behaviors/hold.py`). | The same, with no turret rule. | Kept, plus turrets | |
| Disable: Attack's dogfight at the nearest target whose subsystems still work, with the ion cannons alone; every other cannon holds, so a craft with none never fires a cannon. Guided warheads fire by 1997's Disable limit: only while the target's front shield, with a tenth of its hull once more than a tenth is damaged, outlasts the warheads flying at it and one more from each launcher. A chaser is fought back with every cannon but the ion cannons; a disabled one never (`behaviors/disable.py`). | The same target rule and cannon rule; the same warhead limit. | Kept | |
| Protect: Attack's dogfight at the nearest enemy attacking a craft the order names, once some team has hit that craft: an enemy aimed at it, or the one whose shot hit it last (`behaviors/protect.py`). | The same, where aimed at meant in an attack maneuver on it; hostility was checked only for a player's craft. | Kept, every target hostile | |
| Escort: keeps station beside the craft the game names, flying to the station as a wingman flies to its place; farther than 32,768 units (0.8 km) from that craft (131,072 units, 3.2 km, from one of size 3,000 or more), or with it disabled, flies straight at it, circling a disabled one. Fights, with Attack's dogfight, the nearest enemy within 262,144 units (6.4 km) that targets a craft of the escorted craft's flight group (`behaviors/escort.py`). | The same reaches and threat search; it matched the escorted craft's heading, pitch, roll and speed and pushed the craft to its station each tick; any craft after the group counted, hostile or not. | Kept, steering only; every threat hostile | [#45](https://github.com/evanleeturner/jedimaster-XVT/issues/45), [#204](https://github.com/evanleeturner/jedimaster-XVT/issues/204) |
| Wait and Wait 2: stop where it is and set the maneuver timer to the order's variable1 times 1,180 ticks, so the display shows the time left; done once the timer runs out. While it waits it defends itself as a craft told to wait with Shift+W does, and the timer runs on (`behaviors/waiting.py`). | Stopped with the same timer and never fought back. | Changed | |
| The engine keeps the mission's records by the 1997 rules and the pilot program acts on them: go home when aborted, when the departure is due or when the orders are done. A met goal marks the order done, then the next order starts. Attack, Disable and Protect are done when no target is left now or in a group still to come (`ai/jedimaster_ai/progress.py`). | The 1997 pilot script tested these conditions at each decision. | Kept | |
| The leader of a flight is the engine's pick: the live craft of the batch with the lowest number (Alpha 1). | The first craft of each batch. A follower took over a lost leader. | Kept | |
| Shift+A (attack) and Shift+C (cover): an Attack-order wingman takes the named craft while it is an enemy. A wingman that is holding station, in formation, going home evading, jumping or entering a hangar ignores the order. Other orders keep flying (`ai/jedimaster_ai/tactics.py`). | Wingmen took the target unless holding, in formation or going home. In the 1997 code the command's target search was broken. | Kept, bug fixed | [#50](https://github.com/evanleeturner/jedimaster-XVT/issues/50), [#51](https://github.com/evanleeturner/jedimaster-XVT/issues/51), [#52](https://github.com/evanleeturner/jedimaster-XVT/issues/52) |
| Shift+I (ignore): the wingman never picks that craft, but fights it if it sits behind. | The craft was skipped in target searches. | Kept | [#52](https://github.com/evanleeturner/jedimaster-XVT/issues/52) |
| Shift+E (evade): the wingman drops its target and weaves 3 swerves, chased or not (`behaviors/attack.py`). | Dropped its target and flew an evasive move that ignored the chaser. | Changed | |
| Shift+H (home): the wingman is marked aborted and flies home (`ai/jedimaster_ai/progress.py`). | The same. | Kept | |
| Shift+W (wait) and Shift+G (go): wait stops the craft and keeps its order, and the display reads "awaiting further orders". It fights a chaser at 75% hull or more, otherwise weaves; a disabled craft is never a chaser. Shift+G releases any craft, a starship too (`behaviors/wait.py`). | Wait stopped the craft and answered attackers; go resumed. | Kept, bug fixed | [#292](https://github.com/evanleeturner/jedimaster-XVT/issues/292) |
| Shift+B (resupply): always refused. Nothing yet marks a craft as boarding. | Switched the craft to another script. What else it did is unknown. | Changed: not built yet | [#301](https://github.com/evanleeturner/jedimaster-XVT/issues/301) |

## Waypoints, patrols and going home

The four patrol orders are Circle, Circle and Evade, SS Patrol Loop and SS
Patrol and Attack. A patrol visits a list of waypoints (points a mission
sets in space) in a loop and counts a round each time it comes back to the
start. Going home means flying to the mothership's hangar or jumping out.

| What the computer pilots do now | In the 1997 game | Kept, changed or new | Related issues |
| --- | --- | --- | --- |
| Patrol the waypoints 4 to 11 in a loop. A waypoint is reached within 4,096 units (0.1 km), or 8,192 (0.2 km) for a starship. The round count is kept per order (`behaviors/patrol.py`). | The same, with a round at the wrap. | Kept | |
| One waypoint: a small craft counts one round each time it returns; a starship or freighter stops there for good and counts one round. | A round counted at every decision near the waypoint. | Kept, bug fixed | [#205](https://github.com/evanleeturner/jedimaster-XVT/issues/205) |
| No waypoint: stop where it is and count one round. | Flew to waypoint 4's stored position and counted a round at every decision. | Changed | |
| Patrol at the throttle that gives 5 times the order's speed, never above full throttle. | 5 times the order's speed at full throttle, which could pass full. | Kept, capped | |
| A starship sets throttle 0 while it is 22.5 degrees or more off its course. The course is read past big ships in the way. | Throttle 0 while 22.5 degrees or more off course. | Kept | |
| Circle under attack: ignore attackers and leave them to the turrets. | The same. | Kept | |
| Circle and Evade under attack: weave while an enemy sits behind, then return to the round. Chaff and flares as for every starfighter. | Chaff or flare, a move chosen by the attacker's bearing, then back to the circle. | Changed | |
| SS Patrol and Attack: turrets attack the order's targets; the order is done when none is left. | The same. | Kept | |
| SS Patrol and Disable: patrol the waypoints as a starship on patrol does. Each turret first answers a craft attacking the starship, with normal shots; the turrets left over fire ion shots at the craft the order names that still work, nearest first. A disabled craft is never fired at (`behaviors/starship.py`, `behaviors/turrets.py`). Done when no target is left, disabled ones counted. | The same cruise and the same two turret rules; the turrets kept shooting a disabled craft that hit the starship last. | Changed | |
| SS Protect and SS Protect 2: patrol the waypoints as a starship on patrol does. Each turret first answers a craft that threatens the starship; the turrets left over take the enemies attacking a craft the order names once some team has hit it, nearest first (`behaviors/starship.py`, `behaviors/turrets.py`). Done when no target is left. | Cruised the waypoints; the turrets took the starship's own attackers, then the idle ones the nearest attacker of an order target. | Kept | |
| SS Await Return: throttle 0 until every flight group that goes home into its group has arrived, with no waves and no craft left, then the next order; its turrets defend it (`behaviors/starship.py`). | The stop maneuver, which also halted a turn at once; the same test. | Kept, a begun turn runs on | |
| A finished starship patrol goes to the next order, or jumps out. | The same. | Kept | |
| Go home at full throttle (`behaviors/home.py`). | Throttle was left as it was far out, and 113 order slots slowed to 5 times speed. | Changed | |
| On a patrol and on the way home, a follower flies to its place beside its leader with its own turns and throttle. It aims a little ahead of the place along the leader's heading, speeds up while the place lies ahead and slows to as little as half the leader's speed while it lies behind, never turning back to it (`behaviors/formation.py`). | Followers took the leader's heading, throttle, pitch and roll, plus a push into the place. | Changed | [#296](https://github.com/evanleeturner/jedimaster-XVT/issues/296), [#138](https://github.com/evanleeturner/jedimaster-XVT/issues/138) |
| A follower whose place lies more than 65,536 units (1.6 km) away, and not behind it, flies straight at the place at full throttle. For a starship the distance is 327,680 units (8 km). | The same distances, measured from the leader: the follower flew straight at its leader, so one whose place lay farther back than that overtook its place. | Changed | |
| A leader with followers cruises at seven eighths of full throttle, so they have speed to close up. Fighting or evading, it flies as fast as it needs. | Leaders kept their own throttle. | New | |
| Going home to a mothership, a follower flies in formation until the slow approach, then makes its own approach. With no way out, it stops beside its leader. | Followers followed until the leader was gone. | Changed | |
| Into the mothership: slow to speeds of 150, 100 and 75 within 65,536, 32,768 and 16,384 units (1.6, 0.8 and 0.4 km) of the hangar. Turn in within 2,048 units (0.05 km) at the mothership's speed plus 25, or 40 under a mothership slower than 25. | The same. | Kept | [#211](https://github.com/evanleeturner/jedimaster-XVT/issues/211), [#213](https://github.com/evanleeturner/jedimaster-XVT/issues/213) |
| Abort and departure rules: a craft aborts on shields out, cannons down, warheads out, hull at half, being attacked, shields at half or a quarter, or hull at a quarter or three quarters. The engine keeps these records (`engine/src/xvt/flight/ai_books.c`). | The same. | Kept | [#212](https://github.com/evanleeturner/jedimaster-XVT/issues/212), [#214](https://github.com/evanleeturner/jedimaster-XVT/issues/214) |

## Hyperspace

Craft arrive from hyperspace at set speeds and leave by jumping. The 1997
speeds are unchanged, with one exception: the leader no longer stops early
at its point. In the 1997 game that stop came first by under a second, an
overshoot of 120 to 240 metres, so the leader now ends its arrival on time
alone.

| What the computer pilots do now | In the 1997 game | Kept, changed or new | Related issues |
| --- | --- | --- | --- |
| Arrive from 524,280 units (12.9 km) back at speed 3,600, then 1,800 after 6 game seconds and 900 after 9. The leader's arrival ends after 11 game seconds and the followers end with it. The craft then flies at speed 250, or stops for the orders that start stopped (`behaviors/arrival.py`). | The same speeds, and a leader also stopped within 4,096 units (0.1 km) of its point. | Kept, minus the leader's stop | [#297](https://github.com/evanleeturner/jedimaster-XVT/issues/297) |
| Launch from a hangar and fly the order from the first decision at the craft's own throttle, with no dash. | A 2,360-tick exit (9.4 seconds), then the group formed up. The exit speed came from the script file and is unknown. | Changed | [#296](https://github.com/evanleeturner/jedimaster-XVT/issues/296), [#137](https://github.com/evanleeturner/jedimaster-XVT/issues/137) |
| Leave by hyperspace at full throttle to point 13 and jump inside 16,384 units (0.4 km) at speed 3,600 (point 12 for a captured craft). With no home point the craft jumps where it is. A group leaving by mothership jumps once the mothership is gone and none is to come. A starship order that leaves by mothership stops for good (`behaviors/home.py`). | The same. | Kept | |
| A flight leaves one craft at a time. A follower flies in formation behind a jumping leader and jumps at a moment of its own: near its point, or anywhere once its leader has begun its jump, and surely by the time the leader reaches speed 1,800 (`behaviors/home.py`). | A follower jumped when its leader did. | Changed | |

## Mission scoring

Many mission goals depend on counting which craft have left. The engine
counts them by the 1997 rules, so goals are met as they were. The code is in
`engine/src/xvt/flight/ai_books.c`.

| What the computer pilots do now | In the 1997 game | Kept, changed or new | Related issues |
| --- | --- | --- | --- |
| Count departures by the 1997 rules. A follower of a leader that skipped counts as departed (`engine/src/xvt/flight/ai_books.c`). | The same rules. | Kept | [#211](https://github.com/evanleeturner/jedimaster-XVT/issues/211), [#212](https://github.com/evanleeturner/jedimaster-XVT/issues/212), [#213](https://github.com/evanleeturner/jedimaster-XVT/issues/213) |
| A target hit by a decoy beam still counts toward its order. | A decoyed craft dropped out of the order's count beyond some range, so attackers went home while it lived. A few missions leaned on that. | Changed | |
| A craft waiting to be boarded gets no system restore when its group withdraws or aborts. | It got a full restore. A few missions leaned on that. | Changed: waits for the boarding orders | |

## Anything else

| What the computer pilots do now | In the 1997 game | Kept, changed or new | Related issues |
| --- | --- | --- | --- |
| Other craft (not fighting) decide every 708, 472, 236, 118, 59 or 29 ticks at levels 0 to 5 (`engine/src/xvt/flight/ai_brain.c`). | The same. | Kept | |
| Network play: every machine gets the same decisions. Random choices come from each craft's own dice, which are saved with the world, so the machines stay in step (`ai/jedimaster_ai/think.py`). | Every machine ran the battle. | Kept | [#298](https://github.com/evanleeturner/jedimaster-XVT/issues/298) |
| For a game started from the game's own screens, `config.yaml` names the pilot program (`ai.python` and `ai.program`; `python -m jedimaster ai-setup` fills both). If it is unset, computer craft sit still and one warning per run says so. A program that fails mid-flight ends the flight. | Not applicable. | New | |
| Mines hold fire. | Mines searched for targets and fired. | Changed: not built yet | [#300](https://github.com/evanleeturner/jedimaster-XVT/issues/300) |
| A disabled craft's status line reads "Holding steady". | Unknown | Changed | |

## What stayed exactly as in 1997

The original missions were tuned to these rules, so changing them would
change how missions play and score.

| What stayed | Where to check it |
| --- | --- |
| Mission goals and departure counting: a goal that needs a group home is met as it was. | `engine/src/xvt/flight/ai_books.c` |
| The mission's records: the departure clock and trigger, the abort triggers and the skip to order 4. The clock compares minutes and seconds only. | `engine/src/xvt/flight/ai_books.c` |
| Hold Steady never moves on to a later order, even where an original group has a later order it never reached. | `ai/jedimaster_ai/behaviors/hold.py` |
| Circle ignores attackers and leaves them to its turrets. | `ai/jedimaster_ai/behaviors/patrol.py` |
| Hyperspace arrival speeds 3,600, 1,800 and 900, and their times. | `ai/jedimaster_ai/behaviors/arrival.py` |
| A mission names its leaders. A lost leader's successor is the lowest-numbered live craft. | `ai/jedimaster_ai/behaviors/arrival.py` |
| The speeds and turn-in rules for flying into a hangar. | `ai/jedimaster_ai/behaviors/home.py` |
| Patrols: the waypoint loop, the distance that counts as reaching one, 5 times the order speed, a starship stopping to turn, and a finished starship patrol leaving. | `ai/jedimaster_ai/behaviors/patrol.py` |
| Firing ranges, their changes for fast and big targets, and the burst lengths. | `ai/jedimaster_ai/behaviors/attack.py` |
| When a craft answers a homing missile, and at what distances, now for every starfighter. | `ai/jedimaster_ai/behaviors/countermeasures.py` |
| The weave's swerve times and its flare odds. Its length and what sets it off changed. | `ai/jedimaster_ai/weave.py` |
| How often craft that are not fighting decide, by level. | `engine/src/xvt/flight/ai_brain.c` |
| Shift+H: the wingman is marked aborted and goes home. | `ai/jedimaster_ai/progress.py` |

## Known bugs

### Bugs in the 1997 game and what this project did

| The 1997 game | What this project did | Related issues |
| --- | --- | --- |
| A decoy beam hid a target from the order's count, so attackers went home while it lived. A few missions leaned on it. | The target always counts. Fixed in commit `e06ab29`. | |
| A starship jumped to hyperspace with no hyperdrive. | No hyperdrive, no jump; the starship fights. Fixed in commit `3635fec`. | |
| A wingman jumped the moment its leader did. | Each craft jumps on its own. Fixed in commit `e06ab29`. | |
| Craft going home flew straight on under fire. | They fight back or weave home. Fixed in commit `3635fec`. | |
| The scissors weave ran long and blind. | A short weave that watches the enemy behind. Fixed in commit `8589565`. | [#46](https://github.com/evanleeturner/jedimaster-XVT/issues/46) |
| A patrol over a single waypoint counted a round at every decision spent near it, so "fly N rounds" ended after N decisions. | Fixed in commit `654daef`. | [#205](https://github.com/evanleeturner/jedimaster-XVT/issues/205) |
| A follower that docked alone took every formation follower of its group into the hangar with it. | Fixed in commit `ae61462`. | [#211](https://github.com/evanleeturner/jedimaster-XVT/issues/211) |
| A follower took its leader's departure flag but not its departure time, so if it then led the group it withdrew at once. | Fixed in commit `ae61462`. | [#212](https://github.com/evanleeturner/jedimaster-XVT/issues/212) |
| A craft whose group left by hyperspace but used an alternate mothership flew to the alternate, then turned toward the departure mothership. | Fixed in commit `ae61462`. | [#213](https://github.com/evanleeturner/jedimaster-XVT/issues/213) |
| A computer wingman that aborted because its cannons were down told its player "warheads out". | Fixed in commit `ae61462`. | [#214](https://github.com/evanleeturner/jedimaster-XVT/issues/214) |
| Shift+G did not release a starship that Shift+W had put on hold. | Fixed in commit `bd95e2f`. | [#292](https://github.com/evanleeturner/jedimaster-XVT/issues/292) |
| A craft waiting to be boarded got a full system restore at withdrawal or abort. A few missions leaned on it. | Not done until the boarding orders are built. | |

Issues #205, #211, #212, #213, #214 and #292 are closed as fixed. Issue
#190, about a mine's decoy test, is also closed.

### Problems you can meet today

| What you may see | Related issues |
| --- | --- |
| Computer craft fire no warhead without a lock yet, so none at a big ship from far out, aimed ahead of it. | [#299](https://github.com/evanleeturner/jedimaster-XVT/issues/299) |
| Mines never fire. | [#300](https://github.com/evanleeturner/jedimaster-XVT/issues/300) |
| Shift+B resupply is always refused. | [#301](https://github.com/evanleeturner/jedimaster-XVT/issues/301) |
| Tier 0 pilots never get into firing range of a Star Destroyer. No tier reaches a Super Star Destroyer, and no issue covers that yet. | [#302](https://github.com/evanleeturner/jedimaster-XVT/issues/302) |
| Craft on an order that is not built yet keep flying straight at their current speed. | |
| A disabled craft's status line reads "Holding steady". | |
| Below about 31 frames a second, arrivals, goal checks and messages come late. The cause is in the engine and is older than the new pilots. | [#297](https://github.com/evanleeturner/jedimaster-XVT/issues/297) |
| A network host aborted once with a memory error. A replay of the game did not crash. | [#298](https://github.com/evanleeturner/jedimaster-XVT/issues/298) |
| Launched craft do not form up on their leader. | [#296](https://github.com/evanleeturner/jedimaster-XVT/issues/296) |
| A blown-up warhead keeps its homing data, so pilots may spend chaff or flares on it. Whether the new pilot program is still fooled has not been checked. | [#210](https://github.com/evanleeturner/jedimaster-XVT/issues/210) |
| A second flare chases the same warhead as the first. This is in the engine's flare code. | [#182](https://github.com/evanleeturner/jedimaster-XVT/issues/182) |
| Handing a player's craft back to the computer writes into another craft. | [#170](https://github.com/evanleeturner/jedimaster-XVT/issues/170) |
| A round whose mothership is gone is still used up, and a seventh craft in a group takes the next formation's place. | [#137](https://github.com/evanleeturner/jedimaster-XVT/issues/137), [#138](https://github.com/evanleeturner/jedimaster-XVT/issues/138) |
| Super Star Destroyer turrets aim at the wrong point on the hull. | [#103](https://github.com/evanleeturner/jedimaster-XVT/issues/103) |
| Wingman voice lines can come from the wrong speaker, and groups of more than 6 craft run past the voice tables. | [#94](https://github.com/evanleeturner/jedimaster-XVT/issues/94), [#255](https://github.com/evanleeturner/jedimaster-XVT/issues/255) |
| In melee games the computer team gets a skill boost and its team picks can be wrong, and a flight group's skill level is read without a check. | [#130](https://github.com/evanleeturner/jedimaster-XVT/issues/130), [#131](https://github.com/evanleeturner/jedimaster-XVT/issues/131), [#187](https://github.com/evanleeturner/jedimaster-XVT/issues/187) |
| Faults in the drop-off orders, which are not built yet. | [#124](https://github.com/evanleeturner/jedimaster-XVT/issues/124), [#136](https://github.com/evanleeturner/jedimaster-XVT/issues/136) |

### Issues filed against the 1997 pilot code

The 1997 pilot code was deleted. Issues about it stay open on GitHub until
the project's owner closes them. This table says what became of each one.

| Issues | What became of them |
| --- | --- |
| [#205](https://github.com/evanleeturner/jedimaster-XVT/issues/205), [#211](https://github.com/evanleeturner/jedimaster-XVT/issues/211) to [#214](https://github.com/evanleeturner/jedimaster-XVT/issues/214), [#292](https://github.com/evanleeturner/jedimaster-XVT/issues/292) | Fixed in the new code (see the table above). |
| [#50](https://github.com/evanleeturner/jedimaster-XVT/issues/50), [#51](https://github.com/evanleeturner/jedimaster-XVT/issues/51), [#52](https://github.com/evanleeturner/jedimaster-XVT/issues/52) | The wingman's attack order and target searches. The new search has a test for each case (`ai/jedimaster_ai/tactics.py`, `ai/tests/test_tactics.py`). |
| [#46](https://github.com/evanleeturner/jedimaster-XVT/issues/46), [#49](https://github.com/evanleeturner/jedimaster-XVT/issues/49) | No counterpart: the new pilots have no speed-away move and starships have no missile defense. |
| [#44](https://github.com/evanleeturner/jedimaster-XVT/issues/44), [#207](https://github.com/evanleeturner/jedimaster-XVT/issues/207), [#208](https://github.com/evanleeturner/jedimaster-XVT/issues/208), [#209](https://github.com/evanleeturner/jedimaster-XVT/issues/209) | The 1997 pilots read their behavior from script files. The new program has none. |
| [#45](https://github.com/evanleeturner/jedimaster-XVT/issues/45), [#204](https://github.com/evanleeturner/jedimaster-XVT/issues/204) | The pilot program flies the Escort order; the game engine, which picks the escorted craft and its station, gets a check for each when it sends them. |
| [#47](https://github.com/evanleeturner/jedimaster-XVT/issues/47), [#48](https://github.com/evanleeturner/jedimaster-XVT/issues/48), [#53](https://github.com/evanleeturner/jedimaster-XVT/issues/53), [#54](https://github.com/evanleeturner/jedimaster-XVT/issues/54), [#203](https://github.com/evanleeturner/jedimaster-XVT/issues/203), [#206](https://github.com/evanleeturner/jedimaster-XVT/issues/206), [#272](https://github.com/evanleeturner/jedimaster-XVT/issues/272) | Boarding, drop-off, self-destruct and reload orders are not built yet; each gets a check when it is. |

## Planned, not in the game yet

- Formation flying on the Attack order and for a player's wingmen. On a
  patrol and on the way home, followers fly their places today; on the
  Attack order, and beside a player, each follower still flies by itself.
  Related issues: [#296](https://github.com/evanleeturner/jedimaster-XVT/issues/296), [#138](https://github.com/evanleeturner/jedimaster-XVT/issues/138).
- The orders that still keep their speed and heading, in two groups. The
  next group of orders includes Escort, built in the pilot program but held on
  its course until the game engine names the craft to escort. The group after it holds
  the boarding orders, Pick up, Drop off, Self-destruct, Kamikaze, Disabled,
  Await Boarding, SS Board and SS Launch.
- A warhead fired without a lock at a big ship, aimed ahead of it, at skill Jedi and above.
- Two skill levels above Jedi, Sith Apprentice and Sith Lord (below).
- Possible later work that has not started: capital ships, advanced fighters and
  turbolasers, ion cannons, and aiming at parts of a ship.

### Two skill levels above Jedi: Sith Apprentice and Sith Lord

Partly built. The launcher's Sith setting (None, Apprentices or A Lord)
promotes the enemy's best computer fighter flight in a single-player
battle, and the message at the end of the battle says how its craft
fared. The cockpit's two target readouts mark these craft: "Sith"
after a Sith Apprentice's name, "Sith Lord" after a Sith Lord's.
Until the behaviors in the table below are built, Sith craft fly
with a Jedi's skill. A mission gives each computer craft a skill level
from 0 to 5, and the launcher's mission reader names them Rookie,
Officer, Veteran, Ace, Top Ace and Jedi, and now Sith Apprentice and
Sith Lord (`launcher/jedimaster/mission/names.py`). Two levels will sit above Jedi:
6, Sith Apprentice, and 7, Sith Lord. The engine's tables by skill level
already hold 8 entries, so both fit (`engine/src/xvt/flight/ai_brain.c`).

A Sith Apprentice is more experienced than a Jedi: it uses every behavior
planned on this page at full strength, and no more. A Sith Lord is meant to
be a nearly unbeatable foe that never retreats. In the story, the Sith use
their powers to predict your movements in space and time. In the game, the
pilot program gets a live record of every craft and every warhead in
flight, the same kind of record the engine writes to its debug log for its
tests, and uses it to the full.
The debug log now holds each warhead's, chaff's and flare's path in flight
every 64 ms, and a craft record every 472 ticks in single-player battles as
well as network ones.

| Behavior | Levels 0 to 5 (up to Jedi) | Sith Apprentice (6) | Sith Lord (7) |
| --- | --- | --- | --- |
| Aim and fire | the 3 skill tiers | a 4th tier, sharper than Jedi | a 5th, aimed from the live record |
| Pick the warhead to fit the target: concussion missiles at fighters, space bombs at big ships ([#299](https://github.com/evanleeturner/jedimaster-XVT/issues/299)) | yes | yes | yes |
| Fire a warhead with no lock at a big ship, aimed ahead of it ([#299](https://github.com/evanleeturner/jedimaster-XVT/issues/299)) | Jedi only | yes | yes |
| Fire a warhead with no lock at any craft, players' craft too | no | no | yes |
| Shoot warheads down with lasers | no | only warheads chasing itself | any warhead, anywhere |
| Use ion cannons to disable a craft whose shields are down | weaker at lower levels | full | full |
| Hit a big ship's shield generators and guns before its hull | higher levels | full | full |
| Drop to one third throttle on a target's tail, and turn and loop by a player's rule (see "Throttle and turning, measured") | no | yes | yes |
| Change speed at once at any moment, as craft arriving from hyperspace do | no | no | yes |
| Leave the fight when damaged: the mission's abort rules, weaving instead of fighting below 75% hull | yes | yes | never |
| A live record of every craft and warhead | no | no | yes |

Capital ships turning their working guns toward a threat is planned for
every capital ship, at every level.

## Open AI issues

<!-- ai-issues:start -->
On 2026-10-15 (UTC) the label `ai` was on 43 open issues and 8 closed ones. This list was made by `ai/tools/ai_guide.py --write`; run it again to refresh the list.

- [#2](https://github.com/evanleeturner/jedimaster-XVT/issues/2): Research: enemy pilots that fight as a pack
- [#3](https://github.com/evanleeturner/jedimaster-XVT/issues/3): Research: "Jev", a language-model commander for the enemy side
- [#44](https://github.com/evanleeturner/jedimaster-XVT/issues/44): paiorder_waitgootherorder looks up plan names by mission order code instead of plan id
- [#45](https://github.com/evanleeturner/jedimaster-XVT/issues/45): Escort AI tests object 255 instead of the escorted craft before holding station
- [#46](https://github.com/evanleeturner/jedimaster-XVT/issues/46): Speed-away weave stores a negated push in an unsigned 16-bit value, giving a huge +Z push
- [#47](https://github.com/evanleeturner/jedimaster-XVT/issues/47): Boarding approach never uses full throttle because a half-throttle store overwrites it
- [#48](https://github.com/evanleeturner/jedimaster-XVT/issues/48): Drop-off maneuver passes 255 as 'no basis craft' where the formation code expects 0xFFFF
- [#49](https://github.com/evanleeturner/jedimaster-XVT/issues/49): Missile defense attacker test compares a candidate's own attacker fields with itself
- [#50](https://github.com/evanleeturner/jedimaster-XVT/issues/50): Wingman attack order reads the object table at index 0xFFFF
- [#51](https://github.com/evanleeturner/jedimaster-XVT/issues/51): Follow-leader attack compares the ignored target with the loop counter, not the candidate
- [#52](https://github.com/evanleeturner/jedimaster-XVT/issues/52): Wingman target search over static objects stops at the first one it must skip
- [#53](https://github.com/evanleeturner/jedimaster-XVT/issues/53): Self-destruct delay of 56 or more units overflows the 16-bit lifetime timer
- [#54](https://github.com/evanleeturner/jedimaster-XVT/issues/54): AI boarding craft forgets its tenth docked target: slot 9 is written but never read
- [#94](https://github.com/evanleeturner/jedimaster-XVT/issues/94): Wingman voice line can be spoken by the very craft it is about
- [#103](https://github.com/evanleeturner/jedimaster-XVT/issues/103): Super Star Destroyer turret aim picks the wrong hull vertex for ranks above 0
- [#124](https://github.com/evanleeturner/jedimaster-XVT/issues/124): A drop-off order's flight group number is used unchecked as an index into the 48 group records
- [#130](https://github.com/evanleeturner/jedimaster-XVT/issues/130): Melee AI boost falls back to a team number taken from a count of player slots
- [#131](https://github.com/evanleeturner/jedimaster-XVT/issues/131): Melee tournament: the craft swap for an AI team tests the flight groups before that team's player group
- [#136](https://github.com/evanleeturner/jedimaster-XVT/issues/136): Each craft a drop-off delivers resets the delivered group's remaining rounds
- [#137](https://github.com/evanleeturner/jedimaster-XVT/issues/137): A round that cannot launch because its mothership is gone is still spent, once a second
- [#138](https://github.com/evanleeturner/jedimaster-XVT/issues/138): Formation places are read unchecked: a seventh craft takes the next formation's place
- [#170](https://github.com/evanleeturner/jedimaster-XVT/issues/170): Handing a player's craft back to the computer pilot writes its order slot and plan into another craft
- [#182](https://github.com/evanleeturner/jedimaster-XVT/issues/182): A second flare chases the same warhead as the first, because the "already chased" count tests the wrong shot's type
- [#187](https://github.com/evanleeturner/jedimaster-XVT/issues/187): A flight group's AI level and craft type, and an OPT hardpoint's type, index fixed tables unchecked
- [#203](https://github.com/evanleeturner/jedimaster-XVT/issues/203): A reload craft restores a player's damaged cockpit displays only on that player's own machine, so the machines' world states differ
- [#204](https://github.com/evanleeturner/jedimaster-XVT/issues/204): An escort order's station number is used unchecked as an index into the 28-entry station tables
- [#206](https://github.com/evanleeturner/jedimaster-XVT/issues/206): A craft boarded to be destroyed gets the boarding's 15-to-30-second self-destruct delay only when it already has a countdown running
- [#207](https://github.com/evanleeturner/jedimaster-XVT/issues/207): The AI plan loader trusts its files: sizes, word lengths and plan names are not checked, and a load that gives no plans is not noticed
- [#208](https://github.com/evanleeturner/jedimaster-XVT/issues/208): Modern build: the AI plan text reader keeps the carriage return of a CR LF line end, so such a plan text does not compile
- [#209](https://github.com/evanleeturner/jedimaster-XVT/issues/209): AI code: stale names, misleading names, and comments that misstate or leave out what the code does, for the next renaming pass
- [#210](https://github.com/evanleeturner/jedimaster-XVT/issues/210): A warhead that has blown up keeps its homing data, so computer pilots dodge it, spend chaff or flares on it, and fire defense warheads at it
- [#255](https://github.com/evanleeturner/jedimaster-XVT/issues/255): A flight group of more than 6 craft runs the wingman voice code past its tables
- [#272](https://github.com/evanleeturner/jedimaster-XVT/issues/272): Boarding a mission point reads the object table at the mission point's number, far past its end
- [#295](https://github.com/evanleeturner/jedimaster-XVT/issues/295): A Python layer over the flight engine's shared state, so players, lasers, collisions, craft and targeting can move into Python later
- [#296](https://github.com/evanleeturner/jedimaster-XVT/issues/296): Craft launched from a hangar form up on their leader before flying their order
- [#297](https://github.com/evanleeturner/jedimaster-XVT/issues/297): Below about 31 frames a second, unlocked timing slows six mission timers: arrivals, goal checks and messages come late
- [#298](https://github.com/evanleeturner/jedimaster-XVT/issues/298): Host aborted once with heap corruption in a network game (free(): corrupted unsorted chunks); a replay did not crash
- [#299](https://github.com/evanleeturner/jedimaster-XVT/issues/299): Computer-flown craft never fire missiles or torpedoes
- [#300](https://github.com/evanleeturner/jedimaster-XVT/issues/300): Mines never fire
- [#301](https://github.com/evanleeturner/jedimaster-XVT/issues/301): Shift+B resupply is always refused
- [#302](https://github.com/evanleeturner/jedimaster-XVT/issues/302): Lowest-tier computer pilots never get in firing range of a Star Destroyer
- [#305](https://github.com/evanleeturner/jedimaster-XVT/issues/305): Warhead fire: two 1997 rules need data the pilot program is not sent (pairs when hurt, pulses at big ships)
- [#306](https://github.com/evanleeturner/jedimaster-XVT/issues/306): Three more engine rules read the removed 1997 pilot's maneuver, so they never act for computer-flown craft
<!-- ai-issues:end -->

## The work behind it

These numbers are counted from this repository by `ai/tools/ai_guide.py`.
The pilot program's test run fails when they no longer match, and
`python ai/tools/ai_guide.py --write` refreshes them. The two commit counts
may trail the real count by 1, because a commit cannot name itself.

<!-- ai-numbers:start -->
| What it counts | Today | How to check it |
| --- | --- | --- |
| Commits that changed the pilot program | 39 | `git log --oneline -- ai/jedimaster_ai`, counted with `wc -l` |
| Commits whose title starts with `AI:` (the game engine's side too) | 52 | `git log --oneline --grep='^AI:'`, counted with `wc -l` |
| Lines of Python in the pilot program | 6,025 | the `.py` files in `ai/jedimaster_ai/` and `ai/jedimaster_ai/behaviors/`, counted with `wc -l` |
| Lines of Python in its tests | 8,229 | the `.py` files in `ai/tests/`, counted with `wc -l` |
| Test functions in those tests | 506 | `grep -rhE '^\s*def test_' ai/tests`, counted with `wc -l`; a test that runs on several inputs counts once here |
| Files that each hold one kind of behavior | 16 | the `.py` files in `ai/jedimaster_ai/behaviors/` except `__init__.py`, with the files of orders whose behavior is not written yet |
| Mission order codes that have a written behavior | 23 of 40 | `OrderCode` and `BEHAVIORS` in `ai/jedimaster_ai/orders.py`; the rest keep their speed and heading |
| Kinds of line the pilot program writes to its log | 43 | the entries in `ai/jedimaster_ai/events.json` |
| Lines of C in the engine's computer-pilot code | 4,764 in 21 files | the files named `ai_*.c` and `ai_*.h` under `engine/`, outside `engine/tests/` |
| Test files and checks for that engine code | 10 files, 97 checks | `engine/tests/test_ai_*.c`; a check is a function named `check_...` |
| Checks of the wingman commands (Shift+A and the others) | 13 | the `check_order_...` functions in `engine/tests/test_flight.c` |
| Lines of the 1997 pilot code that were deleted | 14,608 in 9 files | `engine/src/xvt/flight/ai/` just before commit `7d2a109` |
<!-- ai-numbers:end -->

## Credit and licence

The engine came from [OpenXvT](https://github.com/elyosh/OpenXvT) by elyosh
(Adrien Moulin), under the GPLv3. This page is licensed CC BY 4.0, as the
project's other documents are (`REUSE.toml`).
