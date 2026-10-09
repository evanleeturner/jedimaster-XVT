# Engine plan: owning the 1997 game code

**Status:** in progress
**Date:** 2026-10-03, updated 2026-10-07
**Scope:** `engine/src/xvt/`, the recovered game
**Sibling:** `OPENXVT-FRONTEND-PLAN.md`, the launcher this work is the base for
**License:** GPLv3 for the engine; `REUSE.toml` names every file's license

---

## 1. Why this plan exists

*X-Wing vs. TIE Fighter* shipped in 1997. OpenXvT recovered its code, and
this project imported that code into `engine/` with its full history.
Everything this project wants to build sits inside the recovered code
(lines counted on 2026-10-06):

| Code | Lines | What it holds |
| --- | --- | --- |
| `engine/src/xvt/`, the 1997 game | 211,304 | flight, computer-flown ships, missions, briefings, cockpit displays, networking |
| `engine/src/xvt_runtime/`, `xvt_remaster/`, `xvt_app/`, the new code | 53,307 | platform glue, rendering, settings, the current launcher |

The new code defines none of the game's ship behavior, mission rules or
briefings. Wingman orders, enemy tactics, mission scoring, flight physics
and the briefing room are all 1997 code. So every launcher goal (a modern
lobby, a server mode, new netcode, smarter ships, briefings outside the
game) requires reading and changing that code.

Recovered code is hard to change safely. When this plan began, names
such as `field0004` or `unk58` said nothing and comments were rare. The
code held 1,896 global variables, and in 760 places it kept the original
1997 build's version of itself, beside the version the game runs or
alone. A person or an AI assistant working in it spent most of the effort
on archaeology.

This plan takes ownership of that code. By the end, it reads like code
this project wrote: one layout, names that say what things are, comments
that say what functions do, logs that say what happened, and tests that
say what must hold. That is the base the launcher work stands on.

## 2. What ownership means

Five layers, each with a standard a machine checks.

| Layer | What it means here | Checked by |
| --- | --- | --- |
| Form | K&R layout (the style of Kernighan and Ritchie's *The C Programming Language*) in the Linux kernel's form: 8-column tabs, 80-column lines, a brace on every control body; and the layer a formatter cannot decide: include order, `static` by default, snake_case names throughout, no struct typedefs, variables declared where first used | `engine/.clang-format` on every change; a form check for the second layer, built in step 2 |
| Names | every function, variable, field and constant says what it is, with its unit and its polarity; placeholder names go; a rename is proven by comparing the compiled objects with the symbols mapped | the rename map and the object comparison |
| Meaning | a comment above each function says what it does, what it returns on every path, which globals it writes, its units (a tick is 4 ms, a circle is 65,536) and what it does not check; a global's comment says what it holds and who writes it | the comment proof: code tokens unchanged once comments are stripped |
| Logging | INFO lines tell the readable story of a run; DEBUG lines record every value that moved; messages live in one catalog; a log line never changes behavior | the log catalog check |
| Tests | each test is harvested from a comment's promise; every module gets a test file or a written reason; tests run under AddressSanitizer and UndefinedBehaviorSanitizer; a test counts only once it has failed on a planted fault | CTest in `engine/tests/`, run on every change |

Structure follows from the layers: functions sized so a reader can hold
one in view, globals narrowed to the code that needs them, and one version
of each piece of code. The 760 places that kept the original build's
version went on 2026-10-06, each one read first. Where the 1997 game
played differently, an issue keeps the old behavior for a switch the host
would pick, "1997 rules" or "fixed rules"; the switch is not built yet
(`RELEASE-PLAN.md`).

## 3. The rule that holds throughout: the game plays the same

Every change carries its proof.

- A change meant to keep behavior is proven. The compiled object files are
  identical, or the code tokens are identical once whitespace, braces and
  comments are set aside, or tests reach every moved line and pass before
  and after.
- A change meant to alter behavior is named first, with the expected
  difference written down, and goes behind the rules switch.
- A bug found in the 1997 code is filed as an issue with the label "from
  the 1997 game". It is fixed only as a named change.
- The engine stays GPLv3 and a separate program. The launcher starts it
  and talks to it across a process boundary, and `tools/license_boundary.py`
  refuses any file outside `engine/` that reaches inside.

## 4. Where it stands, measured 2026-10-07

| Layer | Done | Left |
| --- | --- | --- |
| Form | the whole tree reformatted in October 2026 (564 files), then the rest of the house C standard across the whole engine (step 2); `.git-blame-ignore-revs` hides the layout-only commits from blame | kept by the formatter and the form check on every change |
| Names | 1,410 names renamed in 4 batches (272 functions, 202 globals, 355 fields, the rest parameters and locals), each batch proven by object comparison | about 60 placeholder names, most of them struct fields named by their offset; the naming layer the first pass set aside |
| Meaning | the whole 1997 code explained: every labelled function and global (3,753 when the map was drawn, 3,195 since the original build's code went), each claim checked against the code before it landed (the networking and flight code first, the rest in a second pass); the 2,155 comment lines the renames pushed past 100 columns wrapped again at 80 | nothing in the 1997 code |
| Logging | 2,035 log lines in the 1997 code, written area by area: networking, missions, briefings, flight, menus, rendering, models, sound, input; the catalog holds 1,404 events, written from 2,186 places across the engine. The 1997 code's own debug output is gone: each empty debug call became a catalog event or went, and the in-game console, the `serverlog.txt` writer and the model loader's `printf` lines went with the original build. The baseline run, 2026-10-06: two two-player games on build `bb4b12d`, clicked through the menus and judged from their logs alone, no screenshots; 12 minutes of flight with all 374 world checksums equal, a 1-minute mission played to its debriefing, 422 different events seen across the two runs, the battle lines identical on both machines, and every line each area's work listed for the run present | kept by the catalog check on every change; the lines the run cannot reach (a player's keys in flight, damage to a player, the fault paths) wait for the tests |
| Tests | 241 tests in 117 programs, all green on every platform. The launcher's four areas (ship behavior, missions, networking, briefings) are tested in the 1997 code file by file: 34 of its 118 files, and a 35th (`flight_hyperspace.c`) has a written reason; a test counted only once it failed on a fault planted in the line it guards. 120 of the tests show open bugs and are expected to fail until each fix lands. The flight test that needs no pictures: at every world checksum tick each game writes where every craft is, and the second baseline run, 2026-10-07, on build `66f668f`, was judged from those lines: 374 of 374 record points identical on both machines, the run's story told in about 2,000 tokens | the other 83 files of the 1997 code, until step 6 reaches them; world checksums never match between two runs, even of one build, so comparing two builds run against run needs the record points, and which of their fields drift between runs is not yet measured |
| Structure | the 760 places that kept the original build's version, and the 1997 code the game never reached (199 functions, 73 globals), removed on 2026-10-06 | 322 functions over 120 lines (112 of them over 300), 85 nested deeper than 6, 39 files over 1,200 lines, among 1,501 functions; 1,694 globals; area by area, each function only after a test reaches it |
| Bugs | 241 issues filed from the 1997 code as each area was read or tested; 36 closed, 35 of them when the original build was retired | 205 open (200 bugs, 5 "1997 rules" candidates); each fix is a named change (`RELEASE-PLAN.md`) |

## 5. Order of work

Six steps. Each one makes the next safe, and each lands with the proof
its row names.

| Step | What | Proof | Why here |
| --- | --- | --- | --- |
| 1. Names | the renames in progress: honest names, placeholder names gone | compiled objects identical with the renamed symbols mapped | the structure step needs names for the units it creates |
| 2. The rest of the house C standard, one landing | across the whole engine, the newer code and its tests too: include blocks in one place with the file's own header first; `static` on every function and file-scope object no header declares; snake_case throughout and camelCase never; no struct typedefs; variables declared where first used, one per line, with `const` on read-only pointer parameters; no commented-out code; the same sweep rewrites the comments that name identifiers | a form check for these rules, built first, so the landing is measured; compiled objects identical for every layer that only renames or re-forms, with renamed symbols and the changed `static` bindings mapped; where declarations move, a separate checker of the moving rules, the same warnings and tests on every platform, and the two-game run's world checksums | it touches every line, so it lands alone, with nothing else in flight, as the reformat did |
| 3. Meaning | the comment map for the remaining areas: frontend, render, assets, audio, math, input, util; then every comment the renames pushed past 100 columns, wrapped again | code tokens unchanged once comments are stripped; for the wrap, the words of every comment unchanged as well, and identical compiled objects | understand before touching; the comments surface the split candidates and the test targets |
| 4. Logging | catalog events in the 1997 code, the 119 empty debug calls first | a log line never changes behavior; the catalog check | the DEBUG traces become the oracles the tests read |
| 5. Tests | tests harvested from the comments and traces, module by module, bug sites first; the two-game run with its world checksums as the whole-game oracle; last, a flight test that needs no pictures: every ship's position logged as it flies, so a two-game run is judged from its logs alone | each test fails on a planted fault before it counts | the safety net goes up before the surgery |
| 6. Structure | first, every file over 1,200 lines split by job, each line moved unchanged and each new file given its own header; then a long function split, or a global narrowed, only when a change has to work inside it, its tests written in the same pass | for a file split, every line placed once in its old order and each function's machine code compared; for a function split, tests reach every moved line and pass before and after, identical objects where the compiler happens to agree | the first step that changes the code's shape, so it comes last; files go first because a file split is mechanical and already narrows what a reader must load |

Step 4 closed on 2026-10-06 with the baseline run in section 4's Logging
row; step 5 starts from it. Step 5 closed on 2026-10-07 with the second
baseline run in section 4's Tests row; step 6 starts from it.

One piece of step 6 came early. Before step 4 closed, on 2026-10-06,
the original 1997 build's code went (760 places, about 21,500 lines),
with the switch between the two builds and the 1997 code the game never
reached (199 functions, 73 globals); `RELEASE-PLAN.md`, section 2, has
the counts and the proofs.

Two rules hold across the steps.

- No function is restructured until a test reaches it and passes on the
  old code. That rule took the new code's 20 oversize findings to 0 in
  October 2026, and it holds here.
- Structure work goes file first, then on demand: every file over 1,200
  lines is split by job; a long function is split only when a change has
  to work inside it, the areas the launcher needs first (ship behavior,
  missions, networking, briefings). The size check reports on the whole
  tree and enforces only in the areas already done.

Each step is planned and reviewed before it opens, lands one commit per
area with its proof, and keeps the builds green on Windows, macOS and
Linux.

## 6. What this buys the launcher

`OPENXVT-FRONTEND-PLAN.md` lists what the launcher needs from the engine: a
headless server mode, join tokens, a status socket, briefing export, a
modern transport. With the engine readable, each of those becomes a change
an assistant can make and prove, with a test behind it. The launcher
itself stays a separate program under the MIT license, across the process
boundary this repository checks on every change.

## 7. Credit

OpenXvT and Aeron are by Adrien Moulin (elyosh), under the GPLv3, and have
been modified in this project since October 2026. The game data is not
included: you need your own copy of *X-Wing vs. TIE Fighter*. *Star Wars*
and related marks are trademarks of Lucasfilm Ltd. This project is not
affiliated with or endorsed by Lucasfilm, Disney, or Totally Games.
