---
artifact_contract: ce-unified-plan/v1
artifact_readiness: implementation-ready
product_contract_source: ce-brainstorm
execution: code
date: 2026-09-20
deepened: 2026-09-20
---

# Game Connector - Plan

> **Mirrored from `anno-117-calculator`'s `docs/plans/2026-09-20-001-feat-game-connector-plan.md`.**
> This copy is provided as a plan/reference file only — created by an agent working in the calculator
> repo, at the calculator maintainer's request, so this repo's own work on `calculator-connector` has the
> full cross-repo requirements in one place. No other file in this repo was touched to produce it, and
> nothing here should be read as a decision made on this repo's behalf — R6/R7 and the Open Questions below
> are explicitly this repo's call, not decided by the calculator side.
>
> **On "implementation-ready" below:** the calculator-side agent ran `ce-plan` to deepen R1–R5 into real
> implementation units in its own repo, where it has full research tooling. It cannot run that same
> research machinery against this repo — it only has what it directly read from this repo's source this
> session (`src/pipe.h`, `src/pipe.cpp`, `src/statistics_server.h`/`.cpp`, `src/main.cpp`, `README.md`).
> The "Proposed" sections below are therefore a **starting-point design, not a decision** — whoever works
> `calculator-connector` should accept, adapt, or replace them freely. Nothing in this file was written to
> this repo's actual code.

## Goal Capsule

- **Objective:** let the calculator's own persistent planning data (islands, factories, building counts)
  be populated and kept in sync from a running Anno 117 session, via `anno-117-pipe`'s local server —
  without disturbing the existing, separate, read-only Live Statistics page.
- **Product authority:** this document is the shared source of truth for both repos/branches involved.
  Neither side should invent requirements the other hasn't agreed to here.
- **Open blockers:** the exact local API/wire contract on the `anno-117-pipe` side is owned by whoever
  works this repo's `calculator-connector` branch — not decided in this document (see "Interface Contract"
  below for what the calculator side needs from it, not how it's built).

## Repo & Branch Map

| Repo | Branch | Status | Who edits it |
|---|---|---|---|
| `anno-117-calculator` | `game-connector` | Created from `main`; carries the already-shipped Live Statistics page (cherry-picked from `feat/live-statistics-page`, verified byte-identical tree, verified no commit touches `js/params.js`/`params.schema.json`/`types.config.ts`/`js/params-ref.js`) | The calculator-side agent/session |
| `anno-117-pipe` | `calculator-connector` | Not yet created as of this writing | This repo's own agent/session |

Both features (statistics page, game connector) live on the calculator's single `game-connector` branch
as **two separate sites** (`statistics.html` vs. `index.html`) — not merged UI, not sharing a page. The
statistics page's own scope, code, and design are frozen; this plan does not modify it.

## Problem & Outcome

Today, every building count in the calculator is entered by hand. A player who wants the calculator to
reflect their actual save has to recount buildings after every change. This repo's local game data
(protocol v2, see this repo's own `src/pipe.h`/`src/pipe.cpp` and the calculator's
`docs/pipe-live-data-integration.md`) already reports live building counts per building type per island.
The outcome: a "Connect" action that turns that live feed into the calculator's own building counts
automatically, for islands the player is actively playing.

## Scope

**In scope:**
- A single, global Connect/Disconnect control (new top-level UI element, not inside an existing dialog) —
  calculator-side.
- Matching live game islands to calculator islands (by name), auto-creating calculator islands that don't
  exist yet, and refusing to sync islands whose names collide — calculator-side.
- Writing live building counts into `Factory.buildings.constructed` for matched/created islands —
  calculator-side.
- Storing and displaying live `AverageProductivity` per factory, bracketed next to the calculator's own
  computed productivity — calculator-side.
- The interface contract the calculator needs from this app's local server (this repo decides the
  implementation; the calculator-side plan only states what it needs — see R6/R7).

**Out of scope:**
- Any change to the shipped Live Statistics page or its existing SSE transport
  (`src/statistics_server.cpp`/`.h`) — that remains as-is.
- `TotalMaintenance`/`TotalIncome`/`TotalProfit`/`WorkforceGUIDtoAmount` — no calculator domain model exists
  for currency or workforce-by-building today; not introduced by this feature.
- Merging the statistics page and the main calculator UI into one page.

## Requirements

### R1 — Connect control (calculator, `game-connector`)
A single global Connect/Disconnect control, in a new top-level UI element (e.g. navbar), shows connection
state (mirroring the statistics page's Live/Reconnecting/Offline vocabulary is a reasonable default, not
mandated). One connection serves every island the pipe reports — there is no per-island connect action.

### R2 — Identity & matching (calculator, `game-connector`)
On first successfully synced report for a game island, the calculator stores `islandID` and `areaIndex`
directly on the matched/created `Island`. No separate `sessionGUID` field is stored on the calculator
side — every calculator `Island` already carries a `Session` object whose `guid` is the same value this
repo's `sessionGUID` resolves to. Session identity for matching is read from the existing
`island.session.guid` at match time, never duplicated into a new stored field.

Matching order per incoming game island (calculator-side logic, listed here only so this repo knows what
data it must supply):
1. Existing calculator `Island` with stored `(islandID, areaIndex)` matching this report, and
   `session.guid` matching the report's `sessionGUID` → sync into it.
2. Otherwise, match by name against existing calculator islands → auto-link, store `islandID`/`areaIndex`.
3. No name match → auto-create a new `Island`, with its session resolved from this repo's `sessionGUID`.

**Implication for this repo:** `sessionGUID` must be supplied on every report so the calculator can do
step 1 and step 3 — it's the only field the calculator needs to resolve a session, given `resolveSession()`
in the calculator's `src/statistics-params.ts` already matches a numeric identifier against
`params.sessions[].guid` (the same GUID space this repo's protocol-v2 `sessionGUID` uses).

### R3 — Duplicate-name refusal (calculator, `game-connector`)
If two live game islands report the same name, neither is synced (calculator-side check; requires no
special handling from this repo beyond continuing to send each island's `areaName` faithfully).

### R4 — Live building-count sync (calculator, `game-connector`)
Calculator-side: a connector class receives each incoming report and, per entry's `BuildingGUIDtoAmount`
map, resolves the matching `Factory` by building GUID and calls `factory.buildings.constructed(count)`
directly. **Implication for this repo:** `BuildingGUIDtoAmount` (already in protocol v2) is exactly what
this needs — no further building-level breakdown is required from this repo for this feature.

Sync always overwrites, unconditionally, every tick — no acknowledgement or conflict-detection protocol is
needed from this repo; the calculator applies whatever the latest report says with no round-trip.

### R5 — Productivity display (calculator, `game-connector`)
Each synced entry's `AverageProductivity` (already in the protocol, product-level only) is shown bracketed
per factory row on the calculator side. No per-building-type productivity breakdown is requested from this
repo — the existing product-level field is sufficient as-is.

### R6 — Interface contract needed from this repo (this repo's decision)
The calculator side needs, per connection tick, per island: `sessionGUID`, `islandID`, `areaIndex`,
`areaName`, and entries carrying `ProductGuid`, `BuildingGUIDtoAmount`, and `AverageProductivity` at
minimum — the same shape already flowing for the statistics feature. **No new wire format is required
beyond protocol v2's existing fields** from the calculator's point of view. Whether this repo reuses the
existing statistics SSE stream, adds a second endpoint, or does something else entirely is this repo's own
call — not decided by the calculator-side plan.

### R7 — This repo hosts the full calculator (this repo's decision)
Per the stated overall goal, this repo becomes a standalone desktop app that embeds the calculator (e.g. as
a git submodule) and serves it same-origin via localhost — extending the existing `--calculator-dir`
sibling-serving precedent (currently only serving `statistics.html`/`dist/`) to serve the full app
(`index.html` included), so the calculator's new Connect control has a same-origin local API to call.
Implementation approach (submodule vs. other packaging, build integration, versioning) is entirely this
repo's decision.

## Resolved: Release/build coupling

The calculator's "`dist/calculator.bundle.js` only committed at release" convention is **not
special-cased** for this feature: `game-connector` gets its own release-and-publish pass on the calculator
side, the same as any other branch, and `dist/` is committed as part of that. This repo's submodule pin
(R7) should point at that released commit — this repo does **not** need to run the calculator's own build
(`npm run build`) on a submodule checkout; the pinned commit will already have a built `dist/`.

## Open Questions (this repo's / cross-repo — not resolved by the calculator-side plan)

1. Whether `calculator-connector` reuses the existing embedded HTTP/SSE server or introduces a separate
   mechanism for game-connector's write-path use case (R6) — this repo's call.
2. Exact UI placement/wording for the duplicate-name error (R3) is calculator-side and not this repo's
   concern, but is listed here for completeness since it was an open item in the source plan.

## Non-Goals

- No currency/maintenance/profit modeling (`TotalMaintenance`/`TotalIncome`/`TotalProfit`).
- No workforce-by-building modeling (`WorkforceGUIDtoAmount`) — not used by this feature.
- No changes to the Live Statistics page or its transport.
- No merging of `statistics.html` and `index.html` into one page.

---

## Proposed Design for R6/R7 (this repo's call — see disclaimer above)

### Finding: R7 is largely already satisfied by existing code

`StatisticsServer`'s constructor (`src/statistics_server.cpp:70`) calls
`server_.set_mount_point("/", staticRoot.string())` — this mounts the **entire** `--calculator-dir`
checkout generically, not a curated subset. `index.html` is already served today whenever
`--calculator-dir` points at a calculator checkout with a built `dist/`; the README's "serves
`statistics.html`, `dist/`, etc." undersold what the code already does. Once the calculator's `main`
release process commits `dist/` on a released `game-connector` commit (per "Resolved: Release/build
coupling" above), pointing `--calculator-dir` at that checkout already serves the full app, same-origin,
with **no server code change**.

What's actually missing for R7's "standalone desktop app" framing is packaging, not serving:
`ResolveCalculatorDir()` (`src/main.cpp`) today defaults to a **sibling directory** convention
(`../anno-117-calculator` relative to the built exe) — fine for a dev checkout, not for something
shipped to a player's machine, who won't have that sibling directory.

### Proposed Key Decisions

**KTD-P1 — Embed the calculator as a git submodule, default `--calculator-dir` to it.**
Add `anno-117-calculator` as a submodule (e.g. at `external/anno-117-calculator/`), pinned to a released
commit on its `game-connector` branch. `ResolveCalculatorDir()` gains a second candidate path (the
submodule, resolved relative to the exe) checked *before* falling back to the existing sibling-directory
default — preserving today's dev-mode convenience while giving a packaged build something to ship.
Updating the pin (new calculator release → new submodule commit) becomes the update mechanism; no
build-time `npm run build` invocation is needed on this repo's side (the pinned commit already has `dist/`
committed, per the resolved coupling above).

**KTD-P2 — Reuse the existing `/statistics` SSE endpoint for game-connector's data, don't add a second one
(recommended default; Option B below is the fallback).**
`BuildStatisticsPayload()` (`src/statistics_server.h:39`) already builds its JSON from the full
`ProductionEntryData` (confirm during implementation that `BuildingGUIDtoAmount`/`AverageProductivity`/
`sessionGUID` survive into the JSON it emits — the shared plan's R6 assumes they do, based on protocol v2
carrying them; this is a one-file check, not a re-design, before relying on it). If confirmed, no new wire
format or endpoint is needed: the calculator's `game-connector.ts` module (a second, independent SSE
client per the calculator-side plan) just also points at `GET /statistics` on
`127.0.0.1:53117` — the identical stream the statistics page already consumes, read by a second consumer.
- **Option A (recommended): reuse `/statistics` as-is.** Zero new server code. Both features are read
  from the same broadcast; game-connector's client simply also opens an `EventSource` to it.
- **Option B (fallback): add a second endpoint** (e.g. `GET /game-connector`) that broadcasts the same
  `BuildStatisticsPayload()` output via a second `StatisticsServer`-like connection pool. Only worth doing
  if this repo's maintainer wants the two features' clients decoupled at the transport level (e.g. so one
  feature's client bugs can't affect the other's connection state) — no functional requirement forces this.

### Proposed Implementation Units (starting point — adapt freely)

#### P-U1. Submodule-backed default calculator directory
**Goal:** KTD-P1 — ship a working `--calculator-dir` default without requiring a sibling checkout.
**Files:** `.gitmodules` (new), `src/main.cpp` (`ResolveCalculatorDir()`)
**Approach:** add the submodule; extend `ResolveCalculatorDir()`'s candidate list to check the submodule
path (relative to the exe) before the existing sibling-directory fallback. Command-line `--calculator-dir`
still overrides both, unchanged.
**Test scenarios:** a build with the submodule present and no `--calculator-dir` flag serves `index.html`
and `statistics.html` from the submodule path; an explicit `--calculator-dir` still overrides it; the
existing sibling-directory fallback still works for local dev when the submodule isn't checked out.
**Verification:** manual — fresh clone with `git submodule update --init`, build, launch without flags,
confirm `http://127.0.0.1:53117/` serves the full calculator.

#### P-U2. Confirm `/statistics`'s payload already carries what game-connector needs
**Goal:** validate KTD-P2's Option A assumption before the calculator side wires up its client to it.
**Files:** `src/statistics_server.cpp` (`BuildStatisticsPayload`) — read-only check, change only if a gap
is found.
**Approach:** trace `BuildStatisticsPayload()` against `ProductionEntryData` (`src/pipe.h`) field-by-field;
confirm `BuildingGUIDtoAmount`, `AverageProductivity`, and the header's `sessionGUID` all reach the emitted
JSON under stable key names. If any is missing or renamed unexpectedly, add/fix it — this is the one place
a genuine wire-format gap would surface.
**Test scenarios:** unit test (or extend an existing one, if `BuildStatisticsPayload` has coverage)
asserting the JSON output contains `buildingsByGuid` (or whatever key name is chosen) and
`averageProductivity` per entry, and `sessionGuid` at the top level, for a `ProductionEntryData` fixture
with non-empty `BuildingGUIDtoAmount`.
**Verification:** test passes; if this repo already has a JSON-shape test for the statistics payload,
extend it rather than duplicating.

#### P-U3 (only if Option B is chosen instead of Option A)
**Goal:** a second, decoupled broadcast endpoint for game-connector.
**Files:** `src/statistics_server.h`/`.cpp` (new endpoint + connection pool, or a small sibling class
following the same shape), `src/main.cpp` (wiring)
**Approach:** mirror `StatisticsServer`'s existing `Broadcast`/`DisconnectAll`/`ClearCache` shape for a
second mount point; feed it the same `BuildStatisticsPayload()` output from the same
`AreaStatisticsCallback` hook already firing in `RunPipe()` (`src/pipe.h`) — no new pipe-parsing code, just
a second consumer of the callback that already exists.
**Test scenarios:** a client connected to the new endpoint receives the same data a `/statistics` client
would, on the same tick; disconnecting from one endpoint doesn't affect the other's clients.
**Verification:** manual smoke test with two concurrent `EventSource`-equivalent connections, one per
endpoint.

### Open Questions, resolved to a recommendation (still this repo's call)

1. Reuse vs. new endpoint (former Open Question 1): **recommend Option A (reuse `/statistics`)** per
   KTD-P2, pending the P-U2 confirmation check. Only fall back to Option B if there's a concrete reason to
   decouple the two features' transport-level connection state.
2. Duplicate-name error UI placement (former Open Question 2): confirmed calculator-side only, no action
   needed in this repo.
