# OpenXvT context-budget findings, 2026-09-30

One day's measurement of the OpenXvT fork against the playbook's context
budget: which files and functions are too large for an agent to hold in
view at once. It is saved as one component of a later plan: a refactor of
the code elyosh wrote, done under the playbook's doctrine. This file
decides nothing by itself.

## The scope ruling

Owner's ruling, 2026-09-30:

- The decompiled 1997 game is not touched.
- The new code and the tools are the only targets.

The two are told apart by folder, and the folder split is backed by a tag.
Every function reimplemented from the original binary carries a comment
such as `// FUNCTION: XVT 0x47AC10` on the line above it: the function's
address in the 1997 program. The tag appears only in `src/xvt/`.

| Folder | Scope | Files | Lines | Tokens | Functions | Address-tagged |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| `src/xvt/` | out: decompiled | 248 | 154,624 | 1,673,009 | 1,797 | 1,779 |
| `src/xvt_runtime/` | in: new code | 151 | 23,923 | 257,673 | 925 | 0 |
| `src/xvt_remaster/` | in: new code | 66 | 8,834 | 103,111 | 336 | 0 |
| `src/xvt_app/` | in: new code | 28 | 3,744 | 111,525 | 119 | 0 |
| `tools/` | in: tools | 1 | 457 | 4,390 | 15 | 0 |
| total | both | 494 | 191,582 | 2,149,708 | 3,192 | 1,779 |

In scope: 246 files, 36,958 lines, 476,699 tokens, 1,395 functions.
Tokens are counted with tiktoken's `cl100k_base` encoding.

2 facts blur the folder line. Neither changes the ruling.

- 18 functions in `src/xvt/` carry no address tag. All 18 sit in header
  files, and the longest is 51 lines.
- 1,263 lines in 103 files of `src/xvt/` mention `XVT_MODERN`. They are
  `#ifdef` and `#ifndef` switches: new code and original code interleaved
  inside the same decompiled functions. They sit in the out-of-scope tree.

## What was measured

| Pin | Value |
| --- | --- |
| Subject | `evanleeturner/OpenXvT` at `ea5a0eb` (2026-09-29), a fork of `elyosh/OpenXvT` |
| Instrument | `contextgate` at `bd0aa9a` on `main`, the merge that added its C lane |
| Config | `openxvt_context_budget.toml`, beside this file |
| Command | `contextgate --root <fork> --config <config> --fail-on none` |

A finding is a warn past the first number and a hard past the second.

| Unit | Warn past | Hard past |
| --- | ---: | ---: |
| C file, in lines | 800 | 1,200 |
| C function, in lines | 80 | 120 |
| Nesting depth inside one function | 4 | 6 |
| YAML file, in lines | 400 | 800 |

The 3 C rows are a copy of the Python rows in the playbook's
`crowns/CONTEXT_BUDGET.md`. The playbook has no C rows yet, so these are a
stand-in, and the counts below move if the owner rules different numbers.
The YAML row is the default that contextgate ships.

Nesting depth counts `if`, `for`, `while`, `do` and `switch`. An else-if
ladder counts once. A body with no braces still counts.

## Headline

| Scope | Hard | Warn | Functions with a hard |
| --- | ---: | ---: | ---: |
| In: new code and tools | 20 | 28 | 18 |
| Out: decompiled 1997 game | 413 | 325 | 297 |
| Support files (Markdown, YAML, Python) | 0 | 1 | 0 |
| total | 433 | 354 | 315 |

The new code holds 19% of the lines and 5% of the hard findings.
No in-scope file is hard.

## In scope: every finding

20 hard and 28 warn, across 5 tables. Locations are `path:line` at the
pinned commit.

### Hard: function length, past 120 lines (17)

| Function | Location | Lines |
| --- | --- | ---: |
| `XvtFlightSim_UpdateEntity` | `src/xvt_runtime/runtime/flight_sim.c:39` | 215 |
| `Submit` | `src/xvt_remaster/engine_glows.c:70` | 205 |
| `XvtSnapshot_ChecksumPrefix` | `src/xvt_runtime/snapshot/world_state.c:313` | 196 |
| `XvtFlightSim_Advance` | `src/xvt_runtime/runtime/flight_sim.c:255` | 187 |
| `XvtFlightEntry_Prepare` | `src/xvt_runtime/runtime/flight_entry.c:39` | 185 |
| `XvtSnapshot_ApplyPresenceMap` | `src/xvt_runtime/snapshot/world_state.c:587` | 165 |
| `XvtFlightEntry_Configure` | `src/xvt_runtime/runtime/flight_entry.c:225` | 163 |
| `XvtSnapshot_LiveChecksum` | `src/xvt_runtime/snapshot/world_state.c:783` | 162 |
| `XvtFlightFrame_Render` | `src/xvt_runtime/runtime/flight_frame.c:135` | 160 |
| `XvtVideoPage_DrawControls` | `src/xvt_app/settings/video_page.c:8` | 150 |
| `XvtRemaster_Frame` | `src/xvt_remaster/xvt_remaster.c:56` | 149 |
| `XvtFlightNetwork_Control` | `src/xvt_runtime/runtime/flight_packets.c:5` | 148 |
| `XvtRemasterFlight_Render` | `src/xvt_remaster/flight_scene.c:76` | 133 |
| `XvtSnapshot_ValidatePrefix` | `src/xvt_runtime/snapshot/world_state.c:957` | 131 |
| `XvtFlightTask_Tick` | `src/xvt_runtime/runtime/flight_task.c:159` | 129 |
| `XvtOpt_Visit` | `src/xvt_runtime/assets/opt_native.c:161` | 121 |
| `XvtFlightLoading_Globals` | `src/xvt_runtime/runtime/flight_loading.c:47` | 121 |

### Hard: nesting depth, past 6 (3)

| Function | Location | Depth |
| --- | --- | ---: |
| `XvtSnapshot_ApplyPresenceMap` | `src/xvt_runtime/snapshot/world_state.c:587` | 8 |
| `XvtControllerSettings_RestoreModal` | `src/xvt_app/settings/controller_page.c:668` | 7 |
| `XvtSnapshot_ChecksumPrefix` | `src/xvt_runtime/snapshot/world_state.c:313` | 7 |

`XvtSnapshot_ApplyPresenceMap` and `XvtSnapshot_ChecksumPrefix` are hard on
both measures. That is why 20 hard findings are 18 functions.

### Warn: file length, past 800 lines (5)

| File | Lines | Tokens |
| --- | ---: | ---: |
| `src/xvt_runtime/snapshot/world_state.c` | 1,144 | 12,684 |
| `src/xvt_runtime/snapshot/records.c` | 994 | 16,774 |
| `src/xvt_app/window_icon.h` | 977 | 82,641 |
| `src/xvt_runtime/runtime/flight_network.c` | 876 | 8,994 |
| `src/xvt_app/settings/controller_page.c` | 801 | 8,646 |

`window_icon.h` is a generated byte table. Its first line reads "Generated
by packaging/icons/icon_tools.py - do not edit." The remedy there is a
`generated-file` marker, which makes the gate skip the file. It is not a
candidate for splitting.

### Warn: function length, 81 to 120 lines (11)

| Function | Location | Lines |
| --- | --- | ---: |
| `XvtRemasterFlight_Prepare` | `src/xvt_remaster/flight.c:32` | 120 |
| `XvtMissionDialogs_Resume` | `src/xvt_runtime/runtime/mission_dialogs.c:17` | 117 |
| `XvtNetworkSession_Tick` | `src/xvt_runtime/runtime/network_session.c:292` | 109 |
| `RenderOne` | `src/xvt_remaster/preview.c:231` | 103 |
| `XvtControllerConfig_ParseDigitalSource` | `src/xvt_runtime/config/controller_config.c:140` | 100 |
| `XvtFlightSim_StepToTime` | `src/xvt_runtime/runtime/flight_sim.c:448` | 100 |
| `XvtRenderMap_Capture` | `src/xvt_runtime/snapshot/render_map.c:128` | 100 |
| `XvtFlightFrame_NetworkTick` | `src/xvt_runtime/runtime/flight_frame.c:473` | 88 |
| `XvtResync_Tick` | `src/xvt_runtime/runtime/resync_task.c:404` | 85 |
| `XvtSnapshot_Encode` | `src/xvt_runtime/snapshot/world_state.c:101` | 85 |
| `XvtRemasterShip_SyncAssets` | `src/xvt_remaster/ship_assets.c:70` | 81 |

### Warn: nesting depth, 5 or 6 (12)

| Function | Location | Depth |
| --- | --- | ---: |
| `PrepareMask` | `src/xvt_remaster/crt.c:55` | 6 |
| `XvtFlightSim_UpdateEntity` | `src/xvt_runtime/runtime/flight_sim.c:39` | 6 |
| `XvtFlightSim_Advance` | `src/xvt_runtime/runtime/flight_sim.c:255` | 6 |
| `XvtNetworkTask_LoadPreview` | `src/xvt_runtime/runtime/network_task.c:84` | 6 |
| `Objects` | `src/xvt_remaster/flight_map.c:168` | 5 |
| `XvtRemasterShip_SyncAssets` | `src/xvt_remaster/ship_assets.c:70` | 5 |
| `XvtControllerMapping_Update` | `src/xvt_runtime/input/controller_mapping.c:324` | 5 |
| `XvtFlightEntry_Prepare` | `src/xvt_runtime/runtime/flight_entry.c:39` | 5 |
| `XvtFlightNetwork_Control` | `src/xvt_runtime/runtime/flight_packets.c:5` | 5 |
| `XvtNetworkMetadata_FromUtf8` | `src/xvt_runtime/runtime/network_metadata.c:38` | 5 |
| `XvtSnapshot_BuildPresenceMap` | `src/xvt_runtime/snapshot/world_state.c:510` | 5 |
| `text_tail` | `tools/mission_dump.c:371` | 5 |

### Where the in-scope findings cluster

- `src/xvt_runtime/snapshot/world_state.c` carries 6 of the 20 hard
  findings. At 1,144 lines the file sits 56 under the hard line.
- `src/xvt_runtime/runtime/` carries 8 more, in `flight_sim.c`,
  `flight_entry.c`, `flight_frame.c`, `flight_packets.c`, `flight_task.c`
  and `flight_loading.c`.
- `src/xvt_remaster/` carries 3, `src/xvt_app/` carries 2, and
  `src/xvt_runtime/assets/` carries 1.
- `tools/mission_dump.c` has 1 warn and no hard.

## Out of scope: the decompiled tree, for the record

These numbers are kept so nobody has to measure again to learn them.
They are not a work list. Each long function is a one-to-one copy of a
function in the 1997 program, and splitting one breaks that match.

| Unit | Hard | Warn |
| --- | ---: | ---: |
| File length | 33 | 14 |
| Function length | 286 | 127 |
| Nesting depth | 94 | 184 |
| total | 413 | 325 |

The 5 longest functions:

| Function | Location | Lines |
| --- | --- | ---: |
| `Flight_ProcessPlayerActions` | `src/xvt/flight/flight.c:3339` | 2,271 |
| `PilotRecord_DrawMissionAchievementsPage` | `src/xvt/frontend/pilot_record.c:1494` | 2,030 |
| `FeDiskIo_CommitFlightResults` | `src/xvt/flight/fediskio.c:116` | 1,953 |
| `MissionDebrief_Update` | `src/xvt/frontend/mission_debrief.c:123` | 1,341 |
| `MissionSetup_DrawMissionTypeControls` | `src/xvt/frontend/mission_setup.c:863` | 1,186 |

The 5 longest files:

| File | Lines | Tokens |
| --- | ---: | ---: |
| `src/xvt/frontend/mission_setup.c` | 10,842 | 126,733 |
| `src/xvt/flight/hud/hud.c` | 7,178 | 82,676 |
| `src/xvt/flight/mission/mission.c` | 6,884 | 81,044 |
| `src/xvt/flight/flight.c` | 6,609 | 72,638 |
| `src/xvt/assets/opt_model.c` | 5,657 | 56,310 |

The decompiled tree still matters to anyone who reads it. 22 files in the
whole tree are past 25,000 tokens, and 21 of them are here. An agent reads
those by function range, never whole. The frontend plan's Phase 0 reads the
net, mission and briefing code. `src/xvt/net/` holds 6 `.c` files, and its
4 largest are:

| File | Lines | Tokens |
| --- | ---: | ---: |
| `src/xvt/net/net.c` | 3,849 | 39,944 |
| `src/xvt/net/frontend_net.c` | 2,256 | 25,266 |
| `src/xvt/net/net_session.c` | 2,081 | 22,252 |
| `src/xvt/net/flight_net.c` | 2,260 | 21,055 |

The largest network file in the new code is
`src/xvt_runtime/runtime/flight_network.c`, at 876 lines and 8,994 tokens.

## Support files

1 warn: `resources/config.yaml` is 445 lines against a warn line of 400.
The 3 Markdown files, the 2 workflow files and the 1 Python file are
inside their budgets.

## How far to trust the numbers

The C lane was built the same day it was used, so it was checked 4 ways.

- On this tree it was compared, function by function, with an independent
  parser-based meter. The two found the same 3,192 functions. 1 start line
  differs by 1 line, in a function the parser had merged with its
  neighbor. 7 nesting depths differ, and in each the parser had read two
  alternative `#ifdef` arms as one piece of code.
- 94 tests pass in the package, 46 of them written for the C lane.
- 21 deliberate bugs were planted in a copy of the package. The tests
  caught all 21.
- It ran over about 6.7 million lines of other C with 0 crashes.

Its stated limits:

- Text under `#if 0` counts toward file length and is not read as
  functions. A 96-line function in `src/xvt/flight/hud/hud.c` is left out
  for this reason.
- C++ is not read.
- A macro that takes a block, such as `list_for_each(...) { }`, counts as
  one nesting level.

An earlier count from the same day, made with the parser-based meter, was
434 hard and 353 warn for C alone. The gate's C count is 433 hard and 353
warn. 1 nesting depth dropped from 7 to 6 and became a warn, and the
function under `#if 0` is no longer read. The YAML warn makes 354.

## What this file does not cover

- **Aeron.** `aeron/` is a git submodule pinned at `6df70b8` and was not
  checked out. It is elyosh's code and is unmeasured.
- **Shaders, CMake and shell.** The gate has no analyzer for them. The
  largest are `CMakeLists.txt` at 370 lines, a packaging script at 347
  lines, and a shader include at 102 lines.
- **Form.** Nothing here measures style. Read from the two config files,
  the tree's `.clang-format` and the playbook's C crown differ on at
  least 4 points: indent width 4 against 8, column limit 110 against 80,
  braces optional against braces on every body, and names such as
  `XvtFlightSim_Advance` against `snake_case`.

## Which playbook sets the tree maps to

By file extension only, from the playbook's hook table. This is step 1 of
a day-0 seeding interview, not a seeding.

| Files | Count | Playbook set |
| --- | ---: | --- |
| `*.c`, `*.h` | 494 | `languages/c/` |
| `*.hlsl` | 10 | `languages/shaders/` |
| `CMakeLists.txt`, `*.cmake` | 5 | `languages/cmake/` |
| `*.sh` | 4 | `languages/shell/` |
| `*.md` | 3 | `crowns/EVANS_MARKDOWN_FORMAT.md` |
| `resources/*.yaml` | 2 | `languages/config/`, as authored config |
| `.github/workflows/*.yml` | 2 | `pipelines/EVANS_GITHUB_ACTIONS.md` with `languages/config/` |
| `*.py` | 1 | `languages/python/` |
| `*.txt` | 1 | `languages/text/` |

No hook names `*.hlsli`, `*.inc`, `*.patch` or `*.in`, 7 files in all. The
playbook does not cover them by extension. The remaining tracked files are
images, icons, a font and git's own files.

## How to re-run

```bash
contextgate --root ../OpenXvT --config openxvt_context_budget.toml \
    --area new_code --fail-on none --group-by module
```

Run it from this repository, with the fork cloned beside it. The config
names 3 areas: `new_code`, `decompiled` and `support`. Leave out `--area`
to get all 3.

## Open questions

Each is the owner's call.

1. **C thresholds.** The playbook's budget crown has no C rows. The
   stand-in rows came from Python.
2. **A freeze mode.** Run with `--fail-on hard`, the gate fails on this
   fork every time, because of code nobody will change. A mode that records
   today's findings and fails only on growth would let it guard new work.
   The package does not have one.
3. **Aeron.** Whether the refactor plan reaches it, and if so a
   measurement of its own.
4. **The `XVT_MODERN` arms** inside decompiled functions: in scope or out.
5. **The route upstream.** The frontend plan's section 10 offers the
   readability pass to elyosh as per-subsystem pull requests. This file
   assumes nothing about that.
