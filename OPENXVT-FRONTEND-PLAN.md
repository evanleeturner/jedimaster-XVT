# Project Plan: Discord-Native Frontend for OpenXvT

**Working name:** TBD (must not be "OpenXvT" — see §7)
**Status:** Concept / pre-alpha
**Date:** 2026-09-29
**Engine dependency:** [OpenXvT](https://github.com/elyosh/OpenXvT) (GPLv3, elyosh)

---

## 1. Vision

*X-Wing vs. TIE Fighter* was designed as a multiplayer game in 1997, when the
surrounding infrastructure (modems, DirectPlay, the MSN Gaming Zone) could not
support it. The netcode itself — deterministic lockstep on player input with
client-side prediction — was ahead of its time. What failed was everything
around it: the lobby, the transport, and the lack of a dedicated server.

OpenXvT has already solved the "can it run" problem. This project solves the
"can people actually play together" problem by moving the entire social and
pre-flight experience out of the game binary and into Discord, and by giving
the engine a modern transport and a server mode.

**Design principle:** OpenXvT is the engine. This project is the frontend
people launch. Same pattern as RetroArch over emulator cores or a launcher
over DOSBox.

---

## First playable (MVP definition of done)

> Two people with **no retail game files** go from a Discord message to a
> 1v1 dogfight in identical fighters in under a minute.

- `/host dogfight` posts a lobby card; second player clicks Join.
- Launcher fetches OpenXvT and the free pack if not installed.
- One hand-written `.tie` mission, one original fighter used by both sides.
- Stock engine, `--game-data <freepack>`, no engine patches beyond the
  join-token hook (or a manual fallback if the hook isn't merged yet).
- Status webhook updates the card: in briefing → in flight → complete.

Everything else in this plan is what comes *after* this works.

---

## 2. Goals

1. **Zero-friction multiplayer.** Paste a link in Discord, click, you're in
   the briefing. No IPs, no port forwarding, no third-party lobby apps.
2. **Pre-launch experience in Discord.** Host, join, pick side and craft,
   watch the briefing together, ready up — all before the game opens.
3. **Web briefing player.** Render XvT briefings (map animation, captions,
   voice) in a browser from mission data, not from the game binary.
4. **Player-run infrastructure.** Anyone can host the bot, the relay, and a
   dedicated server. No single point of shutdown (the Squadrons lesson).
5. **Support existing custom content.** Parse original and community
   missions from disk/ISO/install, plus new content, through one pipeline.
6. **Stay mergeable.** Engine changes are small, upstreamable hooks. The
   fork exists as a staging area for PRs, not as a permanent divergence.

## Non-goals

- Rewriting or restructuring the OpenXvT engine.
- Shipping any original LucasArts game content, ever.
- A new game *engine*. New original assets are in scope (see §7), but only
  as a data pack the existing engine consumes.
- Dependence on Steam, EA, or any vendor backend.

---

## 3. Architecture

```
┌─────────────────────────────── Discord ────────────────────────────────┐
│  Bot (slash commands, lobby embeds)     Activity (web app in voice ch.) │
│  - /host /join /ready /launch           - shared briefing player        │
│  - lobby card w/ buttons                - roster, side & craft picker   │
│  - mints join tokens                    - ready state                   │
│  - receives status webhooks             - (later) briefing editor       │
└──────────────┬──────────────────────────────────┬───────────────────────┘
               │ join link  openxvt://join/<token> │ session data (in-memory only)
               ▼                                   ▼
┌─────────────────────────┐          ┌──────────────────────────────┐
│  Companion / Launcher   │◄────────►│  Signaling relay (tiny)      │
│  (per player, local)    │  WebRTC  │  - STUN/TURN/ICE brokering   │
│  - parses local install │          │  - no game assets stored     │
│  - streams briefing data│          └──────────────────────────────┘
│    to Activity (session)│
│  - launches OpenXvT     │
└──────────┬──────────────┘
           │ CLI args + local socket
           ▼
┌─────────────────────────────────────────────────────────────────────┐
│  OpenXvT (GPLv3, separate process)                                  │
│  - existing sim, renderer, mission execution                        │
│  + hooks contributed upstream: --headless / --server, --join-token, │
│    status endpoint, briefing-export                                 │
└─────────────────────────────────────────────────────────────────────┘
```

### Components

| Component | Language / stack (proposed) | License | Owner |
|---|---|---|---|
| Discord bot | TypeScript or Python; self-hostable, one-command deploy | Permissive (MIT/Apache) | This project |
| Discord Activity | Web app (Embedded App SDK), canvas briefing renderer | Permissive | This project |
| Mission parser | Library + CLI; `.tie` → JSON (mission + briefing + assets manifest) | Permissive | This project |
| Companion / launcher | Small native or Node/Python process; WebRTC via `libdatachannel` or equivalent | Permissive | This project |
| Signaling relay | Minimal service; STUN/TURN config; self-hostable | Permissive | This project |
| Dedicated server mode | Engine flag + headless sim loop | GPLv3 | `engine/` in this repository |
| Engine hooks | `--join-token`, status socket, `briefing-export` | GPLv3 | `engine/` in this repository |

---

## 4. The licensing boundary (read this before writing code)

Two independent IP layers:

1. **elyosh's copyright (GPLv3)** covers the engine. Anything derived from
   his source is GPL. Separate programs that launch the engine or talk to it
   over a socket are **not** derivative works and may carry any license.
2. **Disney/Lucasfilm IP** covers the game name, assets, text, voice,
   models, icons. OpenXvT's legal posture is "we ship no game content."
   This project must hold the same line.

Rules that follow:

- **Process boundary, not library boundary.** Never link against `engine/`.
  Launch it with arguments; communicate over a socket or URL scheme.
  `REUSE.toml` names every file's license, and `tools/license_boundary.py`
  refuses a file outside `engine/` that includes an engine header or links
  an engine library.
- **Engine changes go upstream.** Keep them minimal and generally useful.
- **No asset hosting.** The bot and Activity servers never store mission
  assets. Briefing icons/audio/text stream from a player's own install for
  the duration of a session, in memory only, then are dropped.
- **Custom content is different.** Original missions, icons, and voice that
  the author owns can be hosted freely. This is the incentive for new content.
- **Never commit game data** to any repo, public or private.
- **Distinct name and clear credit.** "Built on OpenXvT by elyosh (GPLv3)."

---

## 5. Data pipeline: missions and briefings

The XvT briefing is data, not code: a timed event stream (camera moves, icon
placement/motion, tags, captions) inside the `.tie` mission file, plus
external voice WAVs. Reference implementation of the format: Michael Gaisser's
YOGEME / `Idmr.Platform` library.

**Parser responsibilities**

- Locate content: GOG/Steam install, mounted ISO, or a user-specified dir.
- Extract mission metadata: name, side(s), player slots, allowed craft,
  goals, campaign position.
- Extract briefing: event timeline → JSON; asset manifest (icons, WAVs) as
  references to local files, never embedded in server-side data.
- Ingest "new" content: community and author-created missions in the same
  format, plus an optional sidecar for extras (custom voice, original icons).
- Output a stable JSON schema consumed by the bot (metadata only) and the
  Activity (full briefing, session-scoped).

**Upstream candidate:** a `briefing-export` / `mission-info` command in the
engine, or a standalone tool in `tools/`. Pure data tooling is the easiest
kind of PR to get merged.

---

## 6. Multiplayer modernization

What 1997 had: input-only lockstep, host-relayed, dead-reckoning prediction,
custom UDP reliability, no dedicated server, no late join, fixed timestep
that made catch-up expensive.

**Step 0 — read elyosh's current net layer** before assuming anything.
The README states 8-player cross-platform with an in-game browser, so the
transport and lobby are presumably already new. Whether sim sync is still
prediction-and-smoothing is unknown until verified.

**Upgrades, in dependency order**

1. **Transport: WebRTC data channels.** Unreliable/unordered channel for
   input, reliable channel for lobby, chat, snapshots. NAT traversal solved
   by ICE/STUN/TURN. Player-hosted relay for signaling only.
2. **Dedicated server mode.** Headless authoritative sim. The 1997 blocker
   was a licensing term forbidding player-run servers; irrelevant for a GPL
   engine. Halves latency vs. peer host; enables everything below.
3. **Late join / reconnect** via state snapshot from the server.
4. **Desync detection** — per-tick state checksums across all peers,
   automatic resync, and logging that makes divergence bugs findable.
5. **Rollback (GGPO-style)** to replace forward prediction + smoothing.
   Precondition already met: deterministic, input-driven sim. Requires
   cheap state save/restore and re-simulating N frames — affordable on a
   modern CPU in a reimplementation, unaffordable in 1997.

Items 2–5 are engine work and belong upstream. Item 1 can live largely in
the companion process, with the engine consuming a local socket.

---

## 7. Free data pack ("no ISO required")

**Problem.** Most people don't have the game files, and many who own the CD
have no drive to read it. OpenXvT already accepts any directory via
`--game-data`, so a free pack is just a directory with the same layout.

**The rule that makes this survivable (the Freedoom rule).** The free pack
fills the same *data slots* so the engine runs unmodified, but it is a
different universe. What is free to reuse is what copyright does not reach:
stats (speed, hull, shields, weapon rates are numbers), roles (light
interceptor, heavy assault fighter, corvette, cruiser), the mission format,
and the gameplay. What is **not** reusable is any Lucasfilm design: no
X-wing-that-isn't, no TIE with different panels, no Star Wars names, factions,
insignia, music, or voice. Same feel, different silhouette. The pack gets its
own name and is never described as Star Wars.

"It's a novelty, nobody will care" is not a defense; Lucasfilm enforces on
designs, not on popularity. Original designs cost nothing extra and remove
the risk entirely.

**Start very small.** Goal of v0.1 is *one ship, one mission, flyable*.

| Milestone | Scope | Notes |
|---|---|---|
| **Pack 0.1** | 1 player fighter (original design), 1 enemy fighter, 1 mission, placeholder cockpit, stock engine effects | Prove the slot-filling works end to end |
| Pack 0.2 | 2 sides × 2 craft each, 1 capital ship, generic HUD/UI text, 3 missions | Enough for a dogfight and an escort |
| Pack 0.3 | Original sound set, 1–2 music tracks, voice via TTS for briefings | Replace last dependency on retail audio |
| Pack 1.0 | Full roster parity by role (not by design) with the retail game | Long tail; community-driven |

**Pipeline**

- Model in Blender → glTF → OPT converter (write it if none exists; XvT's OPT
  format is documented by the community tooling ecosystem).
- Directory layout mirrors the retail install exactly, so `--game-data
  <freepack>` and `--game-data <retail>` are interchangeable.
- Two-pack switch in the launcher: use retail if found, free pack otherwise.
  Missions declare which pack they target via the parser's sidecar; a
  mission written against the free roster only references free craft.
- Pack is its own repo under a content license (CC-BY-SA or CC0), separate
  from the frontend code.

**Not required to develop against.** GOG sells the retail game for ~$10;
having both packs available is fine and expected.

---

## 8. Infinite mission generator

Writes valid `.tie` mission files, so it runs on stock OpenXvT with zero
engine changes, works with either data pack, and plugs into the Discord flow
(`/host random <seed>` → generator → parser → Activity briefing → launch).
Fully original content, no IP exposure.

**Two layers**

1. **Parametric core (deterministic, seeded).**
   - Archetype: dogfight, escort, intercept, strike, recon, capital-ship
     assault, ambush/rescue.
   - Parameters: sides, craft pool (from the active pack), difficulty,
     player count and slots, duration target, objective count.
   - Structural rules so every mission is *winnable* and *interesting*:
     at least one primary goal, reinforcement waves gated on triggers,
     arrival/departure timing that avoids dead air, reasonable force ratios
     per difficulty, hyperspace-in vectors that don't spawn on top of the player.
   - Emits: flight groups, orders, waypoints, arrival/departure triggers,
     goals, and a briefing timeline (icons, camera, captions).
   - Seed = shareable mission ID. Same seed + same pack version = same mission.
2. **Optional narrative layer (LLM + TTS).**
   - Generates briefing prose, mission name, and a light campaign thread
     across consecutive seeds.
   - TTS produces the briefing voice line; stored with the mission, not
     served from the bot.
   - Runs locally (Ollama) or via a user-supplied API key; never required.

**Milestones**

- Gen 0.1: single archetype (dogfight), fixed roster, one seed → valid `.tie`
  that loads in OpenXvT and passes the parser's validation.
- Gen 0.2: all archetypes; difficulty curve; briefing timeline generated.
- Gen 0.3: narrative layer; campaign mode (seed chain with persisted outcome).
- Gen 1.0: `/host random` in Discord; community ratings on seeds.

**Validation.** Every generated mission is loaded headless in the engine and
run with AI-only pilots for N ticks to catch spawn collisions, unreachable
goals, and trigger deadlocks before a human ever sees it.

---

## 9. Phases

### Phase 0 — Groundwork (weeks)
- [ ] Read OpenXvT `src/` net, mission, and briefing code; document findings.
- [ ] Introduce the project on the TotallyOpen Discord; describe the
      frontend concept and the intended upstream hooks. Ask, don't surprise.
- [ ] Pick the working name. Register the `openxvt://` (or project-specific)
      URL scheme handling on all three platforms.
- [ ] Build the behavioral oracle: record deterministic runs of stock
      missions; replay + diff harness for verifying any engine change.
- [ ] Stand up a pristine `upstream` branch; all work rebases onto it.

### Phase 1 — Parser and briefing player (first visible win)
- [ ] Mission parser: install/ISO discovery, metadata JSON, briefing JSON.
- [ ] Web briefing renderer (canvas): timeline playback, captions, voice
      sync, scrub. Runs standalone in a browser from local files.
- [ ] Validate against a sample of stock missions and Evan's back catalog.

### Phase 2 — Discord bot (lobby without the Activity)
- [ ] `/host`, `/join`, side/craft buttons, ready state, `/launch`.
- [ ] Join-token minting; deep-link handoff to the companion.
- [ ] Companion launches OpenXvT with `--game-data` + token.
- [ ] Status webhook from game → bot (in briefing / in flight / complete).
- [ ] Rich Presence.
- [ ] Self-host docs: one-command deploy.

**Upstream PRs needed by end of Phase 2:** `--join-token` consumption,
status endpoint, optional `mission-info` export.

### Phase 3 — Discord Activity
- [ ] Embed the briefing player in a voice-channel Activity.
- [ ] Shared playback, roster, craft picker, ready-up.
- [ ] Session-scoped asset streaming from host's companion over WebRTC.
- [ ] Fallback: bot-only flow still works if Activity unavailable.

### Phase 4 — Transport and server
- [ ] WebRTC transport in companion; engine consumes local socket.
- [ ] Dedicated server mode (upstream PR).
- [ ] Late join / reconnect (upstream PR).
- [ ] Desync checksums (upstream PR).

### Phase 4b — Free pack 0.1 and generator 0.1 (can run in parallel from Phase 1)
- [ ] One original fighter modeled, converted to OPT, flyable in OpenXvT.
- [ ] One hand-authored mission using only free-pack craft.
- [ ] Generator emits one dogfight seed that loads and validates headless.
- [ ] Launcher pack switch: retail if present, free pack otherwise.

### Phase 5 — Rollback and polish
- [ ] Rollback netcode (upstream PR; large; coordinate with elyosh).
- [ ] Briefing editor mode in the Activity for authoring new missions.
- [ ] Post-mission stats back into Discord.

### Phase ∞ — Divergence (optional, later)
Gameplay changes as **data** (missions, config) first. Engine-level gameplay
changes only if unavoidable, and then under the project's own name with
GPL §5 modification notices.

---

## 10. Upstream contribution list

Small, generally useful, easy to review. Each ships with oracle evidence
that behavior is unchanged.

| Hook | Purpose | Size |
|---|---|---|
| `--join-token <t>` | Skip in-game lobby, connect with pre-set slot | S |
| Local status socket | Emit lifecycle events (briefing/flight/complete) | S |
| `mission-info` / `briefing-export` | Metadata + briefing JSON for tooling | M |
| `--server` / headless mode | Authoritative sim without renderer | L |
| Late join / reconnect | Snapshot join | M–L |
| Desync checksums | Per-tick hash + resync | M |
| Rollback | Replace prediction/smoothing | XL |

Separately, the **AI-readability / invariant-annotation pass** using the
agentic playbook: offer it as a series of per-subsystem PRs after discussing
with elyosh. The annotations ("why this constant, matches original, do not
change") are the higher-value gift; restructuring is optional and at his
discretion.

---

## 11. Risks

| Risk | Mitigation |
|---|---|
| Disney takedown of upstream | Off-GitHub mirror (`git clone --mirror` → self-hosted Gitea/Forgejo). Frontend project is independent code and survives. |
| Accidental asset hosting | Session-only streaming; no persistence; CI check that repos contain no game data. |
| Engine PRs rejected | Frontend designed to work with minimal hooks; worst case, a thin patch set maintained on the fork and rebased. |
| Discord dependency | In-game browser remains as fallback path; bot/relay are self-hostable. |
| Agent-driven refactor breaks reverse-engineered quirks | Behavioral oracle gates every engine change; invariant annotations make quirks explicit. |
| Determinism across platforms | Desync checksums from day one; float/ordering audit. |
| Free-pack designs drift toward Star Wars look-alikes | Written design rule (§7); review every model against it; original names and factions from day one. |
| Scope creep | Phases 1–2 are the MVP. Everything after is optional. |

---

## 12. Open questions

- What does elyosh's current net layer actually do? (Phase 0 task.)
- Which WebRTC library fits the companion best across Win/macOS/Linux?
- Bot language: TypeScript (Discord ecosystem default) vs. Python (playbook
  fit)? Decide by who will maintain it.
- Should the in-game briefing eventually become a webview of the same
  renderer (one implementation) — divergence-side decision, not upstream.
- Name.

---

## 13. Credits and licensing statement (draft)

> Built on **OpenXvT** by elyosh, licensed under the GNU GPLv3. This project
> is a separate frontend that launches and communicates with OpenXvT and is
> licensed under the MIT License for code and CC BY 4.0 for documents. It
> contains no content from *Star Wars: X-Wing vs. TIE Fighter*; a legitimate
> installation is required. *Star Wars* and
> related marks are trademarks of Lucasfilm Ltd. This project is not
> affiliated with or endorsed by Lucasfilm, Disney, or Totally Games.
