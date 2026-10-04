# Engine plan: owning the 1997 game code

**Status:** in progress
**Date:** 2026-10-03
**Scope:** `engine/src/xvt/`, the recovered game
**Sibling:** `OPENXVT-FRONTEND-PLAN.md`, the launcher this work is the base for
**License:** GPLv3 for the engine; `REUSE.toml` names every file's license

---

## 1. Why this plan exists

*X-Wing vs. TIE Fighter* shipped in 1997. OpenXvT recovered its code, and
this project imported that code into `engine/` with its full history.
Everything this project wants to build sits inside the recovered code:

| Code | Lines | What it holds |
| --- | --- | --- |
| `engine/src/xvt/`, the 1997 game | 219,049 | flight, computer-flown ships, missions, briefings, cockpit displays, networking |
| `engine/src/xvt_runtime/`, `xvt_remaster/`, `xvt_app/`, the new code | 50,905 | platform glue, rendering, settings, the current launcher |

The new code defines none of the game's ship behavior, mission rules or
briefings. Wingman orders, enemy tactics, mission scoring, flight physics
and the briefing room are all 1997 code. So every launcher goal (a modern
lobby, a server mode, new netcode, smarter ships, briefings outside the
game) requires reading and changing that code.

Recovered code is hard to change safely. Names such as `field0004` or
`unk58` say nothing. Comments are rare. The code holds 1,896 global
variables, and in 219 places it carries two versions of itself side by
side, the original behavior and a later fix. A person or an AI assistant
working in it today spends most of the effort on archaeology.

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
| Logging | the 1997 code has no lines on the project's log API; what it carries is the remains of its own debug output (section 4). INFO lines tell the readable story of a run; DEBUG lines record every value that moved; messages live in one catalog; a log line never changes behavior | the log catalog check |
| Tests | each test is harvested from a comment's promise; every module gets a test file or a written reason; tests run under AddressSanitizer and UndefinedBehaviorSanitizer; a test counts only once it has failed on a planted fault | CTest in `engine/tests/`, run on every change |

Structure follows from the layers: functions sized so a reader can hold
one in view, globals narrowed to the code that needs them, and one version
of each piece of code. Each of the 219 two-version places is retired once
it is understood. The original behavior stays available behind a switch
the host picks, "1997 rules" or "fixed rules", so the original game
remains playable.

## 3. The rule that holds throughout: the game plays the same

Every change carries its proof.

- A change meant to keep behavior is proven. The compiled object files are
  identical, or the code tokens are identical once whitespace, braces and
  comments are set aside, or tests reach every moved line and pass before
  and after.
- A change meant to alter behavior is named first, with the expected
  difference written down, and goes behind the rules switch.
- A bug found in the 1997 code is filed as an issue with the label "from
  the 1997 game", plus "original build only" when the modern build never
  runs that path. It is fixed only as a named change.
- The engine stays GPLv3 and a separate program. The launcher starts it
  and talks to it across a process boundary, and `tools/license_boundary.py`
  refuses any file outside `engine/` that reaches inside.

## 4. Where it stands, measured 2026-10-03

| Layer | Done | Left |
| --- | --- | --- |
| Form | the whole tree reformatted in October 2026 (564 files); `.git-blame-ignore-revs` hides that commit from blame | kept by the formatter on every change |
| Names | 1,410 names renamed in 4 batches (272 functions, 202 globals, 355 fields, the rest parameters and locals), each batch proven by object comparison | about 140 placeholder names; the naming layer the first pass set aside |
| Meaning | the whole 1997 code explained: every labelled function and global, 3,753 of 3,753, each claim checked against the code before it landed (the networking and flight code first, the rest in a second pass); the 2,155 comment lines the renames pushed past 100 columns wrapped again at 80 | nothing in the 1997 code |
| Logging | 0 lines on the project's log API. The 1997 code's own debug output remains: 110 `debug_printf` and 9 network trace calls whose functions are empty (the retail build compiled them to nothing), an in-game text console with a file dump nothing switches on, a `serverlog.txt` writer only the original build compiles, and 32 plain `printf` error lines in the model loader; none of it sits behind a debug-only compile switch | all of it: each empty call becomes a catalog event or goes, as a named change |
| Tests | 90 tests on the new code, all green on every platform | the 1997 code; first targets are the paths where the comments found bugs |
| Structure | measured: 367 functions over 120 lines (120 of them over 300), 95 nested deeper than 6, 42 files over 1,200 lines, among 1,779 functions; 1,896 globals; 219 two-version places | all of it, area by area, each function only after a test reaches it |
| Bugs | 17 filed from the networking code, 4 of them in the modern build; 52 candidates from the flight code under check | filing follows the checks |

## 5. Order of work

Six steps. Each one makes the next safe, and each lands with the proof
its row names.

| Step | What | Proof | Why here |
| --- | --- | --- | --- |
| 1. Names | the renames in progress: honest names, placeholder names gone | compiled objects identical with the renamed symbols mapped | the structure step needs names for the units it creates |
| 2. The rest of the house C standard, one landing | across the whole engine, the newer code and its tests too: include blocks in one place with the file's own header first; `static` on every function and file-scope object no header declares; snake_case throughout and camelCase never; no struct typedefs; variables declared where first used, one per line, with `const` on read-only pointer parameters; no commented-out code; the same sweep rewrites the comments that name identifiers | a form check for these rules, built first, so the landing is measured; compiled objects identical for every layer that only renames or re-forms, with renamed symbols and the changed `static` bindings mapped; where declarations move, a separate checker of the moving rules, the same warnings and tests on every platform, and the two-game run's world checksums | it touches every line, so it lands alone, with nothing else in flight, as the reformat did |
| 3. Meaning | the comment map for the remaining areas: frontend, render, assets, audio, math, input, util; then every comment the renames pushed past 100 columns, wrapped again | code tokens unchanged once comments are stripped; for the wrap, the words of every comment unchanged as well, and identical compiled objects | understand before touching; the comments surface the split candidates and the test targets |
| 4. Logging | catalog events in the 1997 code, the 119 empty debug calls first | a log line never changes behavior; the catalog check | the DEBUG traces become the oracles the tests read |
| 5. Tests | tests harvested from the comments and traces, module by module, bug sites first; the two-game run with its world checksums as the whole-game oracle | each test fails on a planted fault before it counts | the safety net goes up before the surgery |
| 6. Structure | functions split where reading needs it, globals narrowed, the two-version places retired, one function at a time | tests reach every moved line and pass before and after; identical objects where the compiler happens to agree | the first step that changes the code's shape, so it comes last |

Two rules hold across the steps.

- No function is restructured until a test reaches it and passes on the
  old code. That rule took the new code's 20 oversize findings to 0 in
  October 2026, and it holds here.
- Structure work goes area by area, on demand: the areas the launcher
  needs first (ship behavior, missions, networking, briefings), the rest
  measured and left until their turn. The size check reports on the
  whole tree and enforces only in the areas already done.

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
