# AGENTS.md

This file provides guidance to coding agents (Claude Code, etc.) when working with code in this repository.

## Project

Example code that connects to the **Anno 117** game via a Windows named pipe (`\\.\pipe\anno117`), reads
production-statistics data, and renders it live with Dear ImGui (Win32 + Direct3D 12 backend). Windows-only,
C++20. The pipe interface is an unofficial/experimental game feature (see README.md) — protocol details are
not guaranteed stable across game versions.

Activation: launch the game with the `/pipe` argument; statistics are then streamed periodically.

## Build

CMake + vcpkg in manifest mode, targeting the Visual Studio 17 2022 generator. vcpkg is vendored as a git
submodule at `external/vcpkg` (not a machine-global install or `VCPKG_ROOT`).

First-time setup:
```
git submodule update --init external/vcpkg
external\vcpkg\bootstrap-vcpkg.bat
```

Configure and build:
```
cmake --preset windows-vs2022
cmake --build --preset windows-vs2022-release   # or windows-vs2022-debug
```
Output binary: `build/windows-vs2022/<Release|Debug>/anno117_pipe.exe`.

Opening the folder directly in Visual Studio 2022 also works — it picks up `CMakePresets.json` automatically.

There is no test suite and no configured linter/formatter.

## Architecture

- **`src/main.cpp`** — entry point. Calls `SetupImgui`/`CleanupImgui` (from `external/imgui/imgui_impl.h`) to
  create the Win32 window and D3D12 device/swapchain, starts an `anno_server::StatisticsServer`, spawns a
  `std::jthread` running `anno_pipe::RunPipe` (`src/pipe.cpp`) to read the pipe on a background thread — wired
  to the server via `AreaStatisticsCallback`/`DisconnectCallback` — then runs the Win32 message pump and D3D12
  render loop on the main thread. Production data is shared between the pipe and UI threads via a
  mutex-guarded `std::map<std::string /*area*/, std::map<int32_t /*ProductGuid*/, std::deque<ProductionEntryData>>>`;
  `DrawImGui()` renders one `CollapsingHeader` per area with a `PlotLines` graph per product. Defines
  `WIN32_LEAN_AND_MEAN` before `<Windows.h>` so cpp-httplib's `<winsock2.h>` (pulled in transitively via
  `statistics_server.h`) doesn't collide with the legacy `<winsock.h>` `<Windows.h>` would otherwise include.

- **`src/pipe.h` / `src/pipe.cpp`** (namespace `anno_pipe`) — the wire protocol. Messages are little-endian
  binary, framed as `[int32 size][uint8 Message type][payload]`. `ReadInt32`/`ReadInt64`/`ReadFloat`/
  `ReadString` parse a `std::span<const unsigned char>` buffer by hand — there is no serialization library.
  `Message` types: `Version`, `SessionStart`, `SessionEnd`, `AreaProductionStatistics`. `ProductionEntryData`
  (declared in `pipe.h`, documented in README.md) is the per-product statistics struct. `RunPipe()` loops:
  wait for the named-pipe server, connect, read the version preamble, then read messages until
  `stop_requested()` or the pipe breaks — reconnecting automatically on disconnect. Per-product history is
  capped at 20 entries (oldest dropped first). `RunPipe()` also takes optional `AreaStatisticsCallback`/
  `DisconnectCallback`/`SessionStartCallback` hooks, invoked outside `productionDataMutex`, so callers
  (currently only `main.cpp`, wiring `StatisticsServer`) can mirror the stream without `pipe.cpp` knowing
  anything about HTTP. `SessionStartCallback` exists separately from `DisconnectCallback` because a
  `SessionStart` isn't guaranteed to always follow a `SessionEnd`, and unlike `SessionEnd`/broken-pipe it
  should reset session-scoped state (e.g. `StatisticsServer`'s replay cache) without forcing already-connected
  SSE clients to reconnect.

- **`src/statistics_server.h` / `src/statistics_server.cpp`** (namespace `anno_server`) — embedded local HTTP
  server (cpp-httplib) for the `anno-117-calculator` companion's `statistics.html` page. `GET /statistics` is
  a Server-Sent Events feed: `Broadcast(islandId, areaIndex, payload)` pushes one JSON message (schema in
  `docs/live-statistics-server-handoff.md` §5) to every connected client via a per-connection queue +
  condition variable, and caches it as the latest known snapshot per `(islandId, areaIndex)`; a newly
  connecting client is replayed every cached snapshot before it sees any live broadcast, so it isn't blank
  until the next pipe tick (see `docs/plans/2026-08-11-001-feat-island-stats-replay-cache-plan.md`).
  `ClearCache()` drops that cache and is wired to `SessionStart`/`SessionEnd`/pipe-disconnect so a client
  connecting between sessions never gets replayed stale data. `DisconnectAll()` ends every stream (drives the
  browser's `EventSource` `onerror`, since the client's Live/Reconnecting/Offline state comes from the
  connection lifecycle, not a data field). Optionally mounts a calculator checkout directory as static files
  at `/` — see README's "Local statistics server" section for the default path and `--calculator-dir`
  override. Binds `127.0.0.1:53117` by default; the port must match the calculator's
  `DEFAULT_STATISTICS_FEED_URL` if either side changes it.

- **`external/imgui`** — vendored, lightly modified Dear ImGui (core + `backends/imgui_impl_dx12`,
  `backends/imgui_impl_win32`). `imgui_impl.h` is *not* stock ImGui — it's a project-specific helper combining
  window creation, D3D12 device/swapchain setup, and ImGui init/render/cleanup (`SetupImgui`, `CleanupImgui`,
  `WaitForNextFrameResources`, `WndProc`). Read it alongside `main.cpp` to understand the render loop. Treat
  `external/` as third-party code.

- **`external/vcpkg`** — vendored vcpkg submodule (its own nested git repo); provides the manifest-mode
  toolchain, not application code.

- **`vcpkg.json`** — declares `cpp-httplib` (default features off, so no brotli/openssl/zlib/zstd — this app
  only needs plain loopback HTTP) and `nlohmann-json`, used by `statistics_server.h`/`.cpp`.
