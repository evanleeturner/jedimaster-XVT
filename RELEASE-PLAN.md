# Release plan: our own builds of the game, with its bugs fixed

**Status:** planned
**Date:** 2026-10-05
**Builds on:** `ENGINE-PLAN.md` (owning the 1997 code) and `OPENXVT-FRONTEND-PLAN.md` (the launcher)
**License:** GPLv3 for the engine; `REUSE.toml` names every file's license

---

## 1. Why our own builds are unavoidable

The launcher starts the engine that ships with it, and every player in a
game must run the same build, because each machine computes the whole
battle from the same inputs. So this project hands out compiled builds of
the game whatever else it does.

The bugs say the same. Reading the 1997 code to explain it turned up 189
open bug issues by 2026-10-05. 35 of them sit only in the original 1997
build's code, which no player's build compiles. The other 154 are in the
build players run:

| Kind | Example |
| --- | --- |
| hangs, crashes and memory errors | finishing a proving grounds level with 2 seconds or more left would hang the game (#195); level 20 and up reads past a 20-entry table (#201) |
| machines that disagree | a mine's decoy beam test differs between machines in a network game (#190); a view one machine refuses is set on all the others (#194) |
| gameplay a player can see | a corvette's hit on a Super Star Destroyer's bridge past 95% hull damage does no damage (#200) |

## 2. The original 1997 build is no longer a goal

Until now, the work kept the original 1997 build compiling exactly as it
did, and some changes were set aside for that reason alone. Since this
project ships its own build, that reason is gone, and the set-aside work
returns:

| Set aside | Why it was set aside | Now |
| --- | --- | --- |
| 219 places where the code keeps two versions of itself, the 1997 one and a later fix | the original build compiles the 1997 half | one version each; where a 1997 half holds gameplay worth keeping, it moves behind the "1997 rules" switch the host picks |
| 35 bugs in the original build only | that code is not in players' builds | they close with the code they live in |
| log lines written twice at 110 places, the new event and the 1997 call kept word for word | the original build keeps its own call | one plain event line each, once the original build retires |
| the 1997 code's own debug output in the original half: 38 places, the in-game debug console, two empty debug functions | it records what the 1997 program did | turned into events or removed |
| 58 globals of the 1997 code the modern build never uses | removing them changes the original build's output | removed, or made private to their file |

The proof rule stays: a change meant to keep behavior is proven by
identical compiled objects where the compiler agrees, and by tests and a
two-game run elsewhere. It now protects the game players run. A change meant to alter behavior is named first,
with the expected difference written down.

## 3. Which bugs a release fixes, and how

| Bin | Fix | Who notices |
| --- | --- | --- |
| hangs, crashes, memory errors | fixed outright | nobody wants them kept |
| machines that disagree | fixed outright; every player in a game runs the same build | network games stop drifting apart |
| gameplay a player can see | fixed behind the "1997 rules" or "fixed rules" switch the host picks | the host decides per game |
| original build only | closed when the original half retires | nobody: no player runs it |

The issues carry labels for each bin, so a release's notes list what it
fixed by kind.

## 4. What a release is

- **Builds for Windows and Linux**, made by the project's CI from a
  version tag. macOS builds in CI too and follows once it is tested.
- **A version number** on every build, in a scheme still to be chosen.
- **A checksum (SHA-256) for every file** in the release; the launcher
  checks it before it runs an engine, and the lobby compares builds so a
  game never mixes two.
- **License texts and notices inside every package**: the GPLv3, the
  notices of each library the game bundles (SDL3, zstd, libcurl, FFmpeg
  built under the LGPL, and the libraries inside Aeron), and where to get
  the source. Each release's page links the source of its exact tag.
- **Two channels**: stable, and a test channel players opt into.
- **No game content**: players supply their own copy of the game.

## 5. The check before every release

1. The unit tests, under AddressSanitizer and UndefinedBehaviorSanitizer.
2. A two-game run under "1997 rules": every world checksum equal between
   the machines, and the game playing the same as the build before.
3. A two-game run under "fixed rules", with each named change seen.
4. Compiler warnings no worse than the build before.
5. The license check: every file named in `REUSE.toml`, and every
   package's notices present.
6. The release workflow passing a security audit of its own.

## 6. Before the first release

1. Sort the open bugs into the four bins and label them.
2. Put the license texts and notices into every package; today the
   packages carry none.
3. Bring the release workflow up to the project's standard: actions
   pinned to exact commits, a fixed runner version, the least
   permissions, the project's own package names. The workflow came with
   the engine and builds on any tag starting with "v"; the repository
   has no tags or releases yet.
4. Choose the version scheme.
5. Decide how Windows builds are signed. Unsigned programs meet Windows'
   "Windows protected your PC" warning, and packaged programs often meet
   antivirus warnings. A bought certificate shows the project's name; the
   SignPath Foundation signs open-source projects for free, under its own
   name and its own rules, and may decline.
6. Retire the original build (section 2).
7. Ship the first release: hangs, crashes and machines that disagree
   fixed, "1997 rules" the default.

## 7. Known costs

- A fix that changes what the game computes makes old and new builds
  unable to play together, and a battle recorded on one build does not
  replay on another. The lobby's build check makes that a clear message
  instead of a drifting game.
- Every release runs the full check in section 5, so fixes go out in
  batches rather than one at a time.
- Each new library the game bundles adds its notice to the packages.

## 8. Credit

OpenXvT and Aeron are by Adrien Moulin (elyosh), under the GPLv3, and
have been modified in this project since October 2026. The game data is
not included: you need your own copy of *X-Wing vs. TIE Fighter*. *Star
Wars* and related marks are trademarks of Lucasfilm Ltd. This project is
not affiliated with or endorsed by Lucasfilm, Disney, or Totally Games.
