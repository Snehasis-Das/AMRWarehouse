# AMR Warehouse — Edge-AI Distributed Fleet Coordination

**SIH26123** — Edge-AI Based Distributed Fleet Coordination for Autonomous Mobile Robots (AMRs) in Smart Warehouses.

This repository contains **two independent desktop applications** that together form a design → simulate pipeline for a warehouse full of autonomous mobile robots:

| App | Tech | Job |
|---|---|---|
| **Warehouse Editor** | Qt 6 (C++20) | Visually design a warehouse — grid size, Boxes, Stations, communication Nodes, initial AMRs, and Tasks — and export it as a `warehouse.json` file. |
| **AMR Simulator** | SFML 3.0.2 (C++20) | Load a `warehouse.json` and run a real-time, animated simulation of the fleet: task allocation, autonomous navigation, collision avoidance, battery/charging, and a live dashboard. |

The Editor's "Simulate" button launches the Simulator directly, passing it whatever warehouse you have open — so in normal use you only ever touch the Editor.

```
┌────────────────────┐        warehouse.json         ┌──────────────────────┐
│   Warehouse Editor │ ────────────────────────────▶ │     AMR Simulator    │
│   (Qt, design tool)│      (Simulate button)        │  (SFML, live sim)    │
└────────────────────┘                               └──────────────────────┘
```

---

## Table of Contents

1. [Overview](#1-overview)
2. [Intuition — Why It's Built This Way](#2-intuition--why-its-built-this-way)
3. [Repository Layout](#3-repository-layout)
4. [The Warehouse Editor (Qt)](#4-the-warehouse-editor-qt)
5. [The AMR Simulator (SFML)](#5-the-amr-simulator-sfml)
6. [The Simulation Algorithm — Deep Dive](#6-the-simulation-algorithm--deep-dive)
7. [The `warehouse.json` Format](#7-the-warehousejson-format)
8. [Getting Started](#8-getting-started)
9. [Controls Reference](#9-controls-reference)
10. [Validation & Testing Approach](#10-validation--testing-approach)
11. [Known Limitations & Honest Gaps](#11-known-limitations--honest-gaps)
12. [Extending the Project](#12-extending-the-project)

---

## 1. Overview

A warehouse is modeled as a 2D grid of cells. On that grid you can place:

- **Boxes** — pickup/drop-off points, each with a `task_timing` (how long an AMR must "work" there).
- **Stations** — AMR depots. Each has a spare-AMR count; the fleet can grow at runtime by *deploying* a fresh AMR from a Station, and AMRs return to a Station to recharge.
- **Nodes** — fixed communication relays. A Node can "hear" any AMR within `node_range` cells of itself and is how the system finds an available robot for a job, without any single entity knowing about every robot at once.
- **AMRs** — the robots themselves. You can place some upfront (already deployed) and/or let Stations deploy more as work demands it.
- **Tasks** — the actual work list: repeated `TakeAndPut` / `Operate` / `Goto` jobs referencing Boxes or raw coordinates.

You build this in the **Editor**, save it as JSON, and the **Simulator** brings it to life: a logical *Server* hands out work through *Nodes* to nearby idle *AMRs*, which then autonomously navigate, avoid each other, manage their own battery, and report back when done — while a live dashboard tracks fleet-wide statistics.

---

## 2. Intuition — Why It's Built This Way

The central design idea, straight from the problem statement, is:

> **The server does not micromanage every robot's movement.** It orchestrates *work*, not *motion*.

Concretely, there are three tiers of authority, each with a narrow, well-defined job:

- **Server** — the only thing with a view of the whole task list. It doesn't know or care where any AMR is; it just asks the nearest Node to "find me someone to do this," waits, and moves to the next thing once someone accepts.
- **Nodes** — local relays. A Node's only job is answering "is there an eligible idle AMR near me?" using its own `node_range`. It never sees the global task list, and it never talks to distant AMRs outside its own radius.
- **AMRs** — autonomous agents. Each one decides for itself whether it's *able* to take a job (battery safety margin), how to get where it's going (its own rolling path plan), and when it needs to go recharge. Nobody tells an AMR how to walk.

This hierarchy is what makes a *fleet* feel like a fleet instead of a puppet show — work is requested centrally, but motion and self-preservation are fully local decisions. (See [§11](#11-known-limitations--honest-gaps) for an honest note on how this compares to a literal peer-to-peer/no-server model.)

A few more intuitions that shape the code:

- **Atomic actions, not free-form motion.** Every job reduces to a sequence of exactly two primitive actions: **MOVE** (one grid cell) and **WORK** (a fixed timer at a Box). WORK is *non-preemptive* — once an AMR starts working a Box, it finishes, no matter what else is happening around it. This is what makes state tracking (accepted vs. completed) unambiguous.
- **Rolling-horizon planning, not full route pre-computation.** An AMR never solves "the whole trip" once and blindly executes it. Every time it's at rest deciding its *next* move, it re-plans a short path toward its current goal and commits to one cell at a time. This is what lets it react to a warehouse that's changing under it (other robots moving, cells freeing up) without expensive constant full replanning.
- **Static objects are real obstacles; other AMRs are not (in the plan).** A Box or Station is a permanent obstruction — an AMR's path planner routes *around* one unless it's the actual place it needs to be. Other AMRs, by contrast, are *not* baked into the path plan at all — they're handled reactively, every tick, at the point of actually claiming the next cell (see [§6.5](#65-collision-avoidance-the-cell-reservation-system)). This split exists because objects never move (so it's worth solving properly with a real search), while other AMRs move every tick (so baking them into a path would make it stale immediately).
- **Determinism.** Given the same warehouse and the same sequence of frame timesteps, the simulation always produces the same outcome — no random tie-breaking anywhere. Every "who goes first" decision (equal-distance nodes, simultaneous conflicts) falls back to AMR/Node ID order.
- **Safety over liveness.** The single most important invariant in the whole simulator is *two AMRs must never occupy the same cell*. Every conflict-resolution mechanism (reroute, wait, eviction) is built to guarantee that first, even at the cost of an occasional slow queue. There is deliberately no "force a robot through" mechanism, because that was tried during development and found to cause real overlaps (see [§10](#10-validation--testing-approach)).

---

## 3. Repository Layout

```
AMRWarehouse/
├── warehouse-editor/              Qt 6 design tool
│   ├── CMakeLists.txt
│   ├── configure_release.bat      cmake -S . -B build_release ...
│   ├── build_release.bat          cmake --build build_release ...
│   ├── deploy.bat                 windeployqt + copy exe → dist/
│   ├── resources/
│   │   └── resources.qrc          Qt resource file (palette icons, etc.)
│   └── src/
│       ├── main.cpp               entry point
│       ├── editor.h / .cpp        main window, toolbar, layout, save/load/simulate
│       ├── grid.h / .cpp          the warehouse canvas (drag&drop, painting, JSON I/O)
│       ├── palette.h / .cpp       draggable object palette (Box/Station/Node/AMR)
│       ├── infobox.h / .cpp       "inspector" panel for the selected/hovered cell
│       ├── taskwork.h / .cpp      the task list editor panel
│       ├── task.h                 Task struct + Subtask enum (shared shape with the simulator)
│       └── flow_layout.h / .cpp   generic Qt flow-layout widget (used by the palette)
│
└── amr-simulator/                  SFML 3.0.2 real-time simulator
    ├── CMakeLists.txt
    ├── configure_release.bat
    ├── build_release.bat
    ├── deploy.bat                  copies exe + SFML DLLs + resources/ → dist/
    ├── warehouse.json              a ready-made sample warehouse
    ├── resources/
    │   ├── fonts/GoogleSans.ttf
    │   └── images/{amr,box,station,node}.png
    ├── include/
    │   ├── structs.h               the shared data model (State, AMR, Box, Station, Node, Task, Dashboard)
    │   ├── json.hpp                 nlohmann::json (vendored, single header)
    │   ├── parser.h
    │   ├── application.h
    │   ├── renderer.h
    │   └── simulation.h             the fleet-coordination engine's public + internal types
    └── src/
        ├── main.cpp                 entry point: `AMRSimulator.exe <warehouse.json>`
        ├── parser.cpp                JSON → State
        ├── application.cpp           owns the SFML window + main loop
        ├── renderer.cpp              draws State every frame (never decides anything)
        └── simulation.cpp            the entire Server/Node/AMR coordination engine
```

Two completely separate CMake projects, two separate `dist/` outputs — see [§8](#8-getting-started) for exactly how they find each other at runtime.

---

## 4. The Warehouse Editor (Qt)

### 4.1 What it's for

A drag-and-drop 2D warehouse designer. You place objects on a grid, describe the work you want done, and export everything as the JSON file the Simulator consumes. It never simulates anything itself — its only output is a JSON file (and a launched Simulator process).

### 4.2 Window layout

```
┌───────────────────────────────────────────────────────────────────┐
│  Toolbar:  [New] [Save] [Load] [Simulate]                          │
├───────────┬──────────────────────────────────┬─────────────────────┤
│ Palette    │                                  │                     │
│ (drag      │           Grid canvas            │      InfoBox        │
│ objects)   │      (click/drag to place)        │  (selected object   │
│            │                                  │   details)          │
│ TaskWork   │                                  │                     │
│ (task list │                                  │                     │
│ editor)    │                                  │                     │
│            ├──────────────────────────────────┤                     │
│            │        Debug / log console        │                     │
└───────────┴──────────────────────────────────┴─────────────────────┘
```

`Editor::updateLayout()` recomputes all of these widget sizes on every resize, keeping the grid's aspect ratio matched to the warehouse's width:height and clamped to sensible min/max bounds, so the canvas never looks stretched.

### 4.3 File-by-file responsibilities

- **`editor.h` / `editor.cpp`** — the `QMainWindow`. Builds the toolbar and the four-pane layout, wires every child widget's signals together (Palette → Grid selection, Grid → InfoBox display, both → the debug log), and implements the four toolbar actions:
  - **New** — asks for a width/height (1–500 each) via `QInputDialog`, resets the Grid and clears the task list.
  - **Save** — serializes the current Grid + TaskWork state to a `warehouse.json` you choose via a file dialog.
  - **Load** — reads an existing `warehouse.json` back into the Grid/TaskWork (round-trips through the same schema the Simulator reads).
  - **Simulate** — if a file is currently loaded/saved, launches the Simulator on it directly; otherwise offers to save-then-simulate or choose-an-existing-file-then-simulate. See [§4.5](#45-how-simulate-actually-launches-the-simulator) for exactly how this works.

- **`grid.h` / `grid.cpp`** — the actual design surface. Holds the list of placed objects (`Box`/`Station`/`Node`/`AMR`, each with an auto-incrementing per-type ID), handles drag-and-drop placement, click-to-inspect, and click-to-erase. Also owns three pieces of computed, non-editable metadata that get baked into the saved JSON:
  - **`minimumNodeRange()`** — a multi-source BFS starting from *every* placed Node simultaneously, returning the largest distance from any cell to its nearest Node. In other words: *the smallest `node_range` that would still let your Nodes see the entire warehouse.* Returns `-1` if there are no Nodes yet (and the editor logs a warning rather than silently emitting a broken value).
  - **`amrsOutsideStations()` / `stationAmrCount()` / `totalAmrCount()`** — simple counts used to populate the informational `"fleet"` block in the JSON (this block is not read by the Simulator's parser at all — it's purely a human-readable summary for whoever opens the file).

- **`palette.h` / `palette.cpp`** — the small strip of draggable object icons (Box, Station, Node, AMR). Purely emits `objectSelected(type)` when you pick one; the Grid does the actual placement.

- **`infobox.h` / `infobox.cpp`** — a read-only "inspector" that shows the type/ID/position/metadata (task timing, AMR count, etc.) of whatever object you last clicked on the Grid.

- **`taskwork.h` / `taskwork.cpp`** — the task-list editor: add/remove `Task` entries, each with an `id`, a repeat `count`, a `subtask` kind, and its two integer parameters `a`/`b`. Serializes to/from the same JSON `"tasks"` array the Simulator parses. Semantics of `a`/`b` depend on `subtask` — see [§7](#7-the-warehousejson-format).

- **`task.h`** — just the `Subtask` enum and `Task` struct. This is the one piece of shape that's effectively duplicated (not literally shared code, but kept in lockstep) between the Editor and the Simulator, since they exchange this shape via JSON rather than a shared library.

- **`flow_layout.h` / `flow_layout.cpp`** — a generic, reusable Qt `QLayout` subclass that wraps its children left-to-right and flows to the next line when it runs out of width (the standard Qt "Flow Layout" example pattern). Used to lay out the Palette's icons.

### 4.4 What actually gets saved

`Editor::saveWarehouseTo()` assembles the root JSON object from three sources — `m_warehouseWidth`/`m_warehouseHeight` directly, `m_grid->toJson()` for the `"objects"` array, `m_taskWork->toJson()` for `"tasks"` — plus the computed `"fleet"` summary and `"minimum_node_range"` described above. See [§7](#7-the-warehousejson-format) for the full schema with a worked example.

### 4.5 How "Simulate" actually launches the Simulator

The Editor and Simulator are two independently-built CMake projects — nothing guarantees where `AMRSimulator.exe` ends up relative to `WarehouseEditor.exe`. `Editor::launchSimulator()` (in `editor.cpp`) handles this by trying every layout the two projects' own build/deploy scripts can actually produce, in the order a finished setup is most likely to look like:

1. **Same folder as the Editor's own executable** — the recommended way to package both apps together for a demo: copy `AMRSimulator.exe` (with its SFML DLLs and its `resources/` folder) right next to `WarehouseEditor.exe`.
2. **Both projects deployed separately, as sibling folders** — `…/warehouse-editor/dist/WarehouseEditor.exe` next to `…/amr-simulator/dist/AMRSimulator.exe`.
3. **Both projects merely built (no `deploy.bat` yet)** — straight out of `…/amr-simulator/build_release/Release/AMRSimulator.exe`.

It resolves each candidate relative to `QCoreApplication::applicationDirPath()` and uses the first one that actually exists. If none do, it shows a clear dialog telling you to build the simulator or place it next to the editor — it never fails silently.

One detail that matters a lot in practice: **`AMRSimulator` resolves its `resources/` folder relative to its own *current working directory* at runtime, not relative to its `.exe` path** (see `Renderer`'s font-loading code). Since `QProcess` otherwise inherits the *Editor's* working directory — which has nothing to do with where the Simulator lives — `launchSimulator()` explicitly sets the child process's working directory to the folder containing the resolved `AMRSimulator.exe`. Skipping this is the single most common way to get "it launched but the font/images look wrong or it exits immediately" bugs.

The warehouse JSON path is resolved to an absolute path before being passed as the simulator's single command-line argument, so it's unambiguous regardless of either process's working directory. The simulator is started **detached** (`QProcess::startDetached`), so it runs as its own independent window and keeps running even if you close the Editor.

---

## 5. The AMR Simulator (SFML)

### 5.1 Data flow

```
   warehouse.json
        │
        ▼
    parser.cpp  ──▶  State  (structs.h)
                       │
          ┌────────────┼────────────┐
          ▼                          ▼
     Simulation                  Renderer
   (updates State)          (only ever reads State)
          │                          │
          └────────────┬─────────────┘
                        ▼
                 Application::run()
              (owns the SFML window,
               the 60fps loop, input)
```

This separation is enforced throughout: **the Renderer never decides anything** (it draws whatever `State` currently says), and **the Simulation never draws anything** (it only mutates `State`). `Application` is the only thing that owns the actual OS window and event loop.

### 5.2 The shared data model (`structs.h`)

Everything both the parser and the renderer agree on lives in one small header:

- `State` — the whole simulation snapshot: `warehouse` (width/height), `node_range`, and vectors of `stations`, `nodes`, `boxes`, `amrs`, `tasks`, plus a 2D `grid` (occupancy, used mainly by the parser/editor side) and the `dashboard`.
- `AMR` — `id`, continuous `x`/`y` (float, for smooth rendering), `idle`, `heading` (degrees), `intended_path` (its current short rolling-horizon plan, for the renderer to draw), `transmitting` (a short visual "pulse" whenever it communicates), `battery` (0–100).
- `Box` — `id`, `duration` (the WORK time, called `task_timing` in JSON), `x`/`y`.
- `Station` — `id`, `count` (spare AMRs left to deploy), `x`/`y`.
- `Node` — `id`, `x`/`y`, `transmitting`.
- `Task` — `id`, `count` (how many times to repeat), `subtask` (`TakeAndPut`/`Operate`/`Goto`), `a`/`b` (meaning depends on `subtask`, see [§7](#7-the-warehousejson-format)).
- `Dashboard` — everything the on-screen dashboard shows: task progress, fleet counts (deployed/active/idle/transmitting), average battery, and the three coordination counters (`collisions`, `deadlocks`, `reroutes`).

### 5.3 `parser.cpp`

A straightforward, defensive JSON → `State` loader (using the vendored single-header `nlohmann::json`, `include/json.hpp`). It:

- Reads `width`/`height`/`minimum_node_range` and pre-sizes the occupancy `grid`.
- Walks the `"objects"` array, dispatching on `"type"` into the matching `Box`/`Station`/`AMR`/`Node` struct (an unrecognized `"type"` or a missing required field aborts parsing with a clear `stderr` message rather than guessing).
- Walks the optional `"tasks"` array the same way, mapping the `"subtask"` string to the `Subtask` enum.
- Computes `dashboard.tasks_total` as the **sum of every task's `count`** (i.e. it's a count of individual subtask *instances* across the whole task list, not a count of `Task` entries — this matters later, see [§6.2](#62-the-server-state-machine)).
- Resets every other dashboard counter to its starting value (`current_task = -1`, `average_battery = 100`, etc.).

### 5.4 `application.cpp` — the main loop

`Application::init()` parses the JSON, then sizes the SFML window from the *desktop's* available space and the warehouse's aspect ratio (leaving room for a fixed-width dashboard panel down one side), creates the window, and hands `State` off to both `Simulation::init()` and `Renderer::init()`.

`Application::run()` is a simple fixed-cadence loop (targeting 60 FPS via `sf::Clock` + `sf::sleep`):

```cpp
processEvents();
if (running && !simulationOver) update(dt);   // → simulation.simulate(dt)
draw();                                        // → renderer.render()
```

`update()` also checks `simulation.isOver()` every frame; the moment it flips to `true`, the app freezes the simulation (`simulationOver = true`, `running = false`) and tells the Renderer to show the "simulation over" overlay — the window stays open afterward so you can still look at the final state.

Input handling (also in `application.cpp`):

| Key | Effect |
|---|---|
| `Escape` | Close the window |
| `V` | Toggle run/pause (ignored once the simulation is over) |
| `↑` / `↓` | Scroll the dashboard panel |

### 5.5 `renderer.cpp` — purely a view

Draws, every frame, in this order: background → grid lines → Boxes → Stations → AMRs (with their aura, intended path, direction arrow, and a communication "pulse" ring while `transmitting`) → Nodes (with a similar pulse while `transmitting`) → text labels → the dashboard panel (task progress bar, fleet counts, average battery, the three coordination counters, elapsed time) → the "simulation over" overlay, if applicable. It reads `AMR::heading` (degrees, 0 = facing "up," increasing clockwise) directly to orient each AMR sprite, and `AMR::intended_path` to draw its short look-ahead line. It never computes or infers anything — every value it draws was already decided by `Simulation`.

---

## 6. The Simulation Algorithm — Deep Dive

This is `simulation.h` / `simulation.cpp` — the actual "fleet coordination" engine, and the heart of the project. Everything here runs **single-threaded and fully deterministically** inside one `Simulation::simulate(dt)` call per frame; there is no real networking or concurrency anywhere, by design (the master spec for this project was explicit that *simulating* distributed behavior deterministically is strongly preferred over introducing real threads/sockets purely to look distributed).

### 6.1 The three tiers, in code

| Concept | Lives as | Talks to |
|---|---|---|
| **Server** | `ServerState` (a phase-based state machine, advanced once per tick) | Nodes (task requests), Stations (deployment) |
| **Node** | `NodeRuntime` (one per `state->nodes[i]`, tracks a `busyUntil` timestamp) | AMRs (local discovery, by distance) |
| **AMR** | `AMRRuntime` (one per `state->amrs[i]`, tracks mode/phase/movement) | the grid (its own motion + battery) |

### 6.2 The Server state machine

The Server works through the task list **sequentially, one subtask instance at a time** (`ServerPhase::Idle → Requesting → WaitingNodeResponse → …`):

1. **Pick the next un-requested subtask instance.** Tasks are processed in JSON order; within a task, `count` identical instances are requested one after another — but only once the *previous* instance has been *accepted* (not necessarily completed — see [§6.3](#63-task--subtask-bookkeeping)).
2. **Sort every Node by distance** to that subtask's reference location (the source Box for `TakeAndPut`/`Operate`, or the raw `(a,b)` coordinate for `Goto`), closest first, ties broken by Node ID.
3. **Try the closest Node.** If the Node is currently free, the Server "sends" the request — the Node goes `transmitting = true` and becomes busy for a computed **communication timeout**:
   `timeout = COMM_BASE_MARGIN + (width + height) × COMM_TIME_PER_CELL` — i.e. a small fixed margin plus a warehouse-size-scaled travel time, never an arbitrary constant.
   If the Node is *already* busy (e.g. it's simultaneously mediating a collision — see [§6.6](#66-collision-resolution-safety-over-forcing)), the attempt is treated as "no response": one of up to **3 retries** on that same Node before giving up and moving to the next-closest one.
4. **Resolve.** Once the timeout elapses, the Node performs discovery (below) — either an AMR gets assigned (Server marks that instance `accepted` and moves on) or it comes back empty (Server tries the next Node).
5. **Fall back to Station deployment** if every Node comes back empty: try Stations closest-first, and if one still has spare capacity (`count > 0`), deploy a brand-new AMR there and restart the Node search (the freshly-deployed AMR is now discoverable).
6. **If deployment also has nothing left**, wait **30 simulated seconds** and retry from the top (this is specifically for "every AMR is alive but currently busy" — not "no AMRs exist at all," which is handled separately, see [§6.9](#69-completion--the-stuck-condition)).

### 6.3 Task / subtask bookkeeping

Every `Task` gets a `TaskExecution` with one `SubtaskInstance` per repetition (`count` of them), each tracking `accepted` and `completed` independently. This distinction is what lets the Server keep handing out new work the instant one instance is *accepted*, without prematurely believing the whole task is done — completion (needed for `dashboard.tasks_completed` and for ending the simulation) is only ever driven by the AMR actually finishing its physical work, never by acceptance.

### 6.4 Node discovery ("BFS-equivalent")

When a Node's request resolves, it looks at every AMR within its `node_range` (Manhattan distance — on this uniform, obstacle-free-for-AMRs grid, that's exactly what a real breadth-first search from the Node's cell would discover, layer by layer), sorted closest-first (ties by AMR ID), and picks the **first eligible** one:

- `AMRMode::Idle`
- `battery > 0`
- passes the **battery safety check** (below)

The chosen AMR is assigned immediately (mode → `Executing`, phase → `MoveToA`) and gets a short `transmitting` pulse for visual feedback; nobody else in range needs to do anything (there's no genuine uncertainty to negotiate away in a deterministic single-tick model, so this collapses the spec's ACCEPT/ACK/NAK handshake into one direct assignment rather than modeling it as three separate messages — see [§11](#11-known-limitations--honest-gaps)).

**Battery safety check (`eligible()`)** — before an AMR can be handed a job, the simulator estimates the *total* cost of that job (every MOVE and WORK it will involve, **plus** the trip back to the nearest Station afterward) and refuses the assignment unless the AMR's current battery covers it with real margin:

```
moveBudget = ceil((ideal move count) × 1.5)      // 50% slack for detours/congestion
totalCost  = moveBudget × BATTERY_DRAIN_PER_MOVE
           + workCount × BATTERY_DRAIN_PER_WORK
           + 10                                   // flat safety margin
eligible  ⟺ battery ≥ totalCost
```

This exists specifically so an AMR is never accepted into a job it can't safely finish — since WORK/MOVE are non-preemptive and the same AMR must see a `TakeAndPut` through end-to-end, a mid-task battery death would permanently strand that subtask instance. (This exact failure mode showed up during development with a smaller/flat margin — see [§10](#10-validation--testing-approach) — which is why the slack factor is generous.)

### 6.5 Rolling-horizon path planning (with real obstacle avoidance)

Every time an AMR is at rest and needs to move toward some target `(tx, ty)` (a Box, a Station, or a raw `Goto` coordinate), `computeHorizonPath()` runs a **BFS from the AMR's current cell**, treating every Box and Station cell as impassable — **except** the destination cell itself, which must always remain enterable (that's precisely where the AMR needs to end up, to pick up/drop off a Box or reach a Station). Only the first `PATH_HORIZON` (6) cells of that shortest path are kept and shown as `AMR::intended_path`; the AMR commits to one cell at a time and re-plans from scratch every time it's at rest again. If no route exists at all right now (fully boxed in), it simply returns an empty plan and the AMR waits one tick and retries — it never gets confused into thinking it has "arrived."

Other AMRs are **deliberately not part of this plan at all** — that's the reactive layer, next.

### 6.6 Collision avoidance: the cell-reservation system

Every tick, before any AMR makes a decision, `rebuildOccupancyMaps()` builds two lookup tables from scratch:

- **`restingOccupancy`** — which AMR (if any) is currently sitting still on each cell.
- **`inFlightClaims`** — which AMR (if any) is currently mid-transit *into* each cell.

When an AMR (at rest) wants to move into its next planned cell, `tryClaimCell()` checks both tables:

- **Free?** → claim it immediately (add to `inFlightClaims`), start the smooth cell-to-cell animation.
- **Blocked by a resting AMR that isn't doing anything urgent** (idle, or itself just standing there deciding its own next move, or returning to charge) → **evict** it: BFS outward from the blocker's own position for the nearest genuinely free, non-object cell, and instantly shuffle it there *before* handing the cell over. This is what stops a parked robot from being able to permanently seal off a busy Box forever (idle AMRs never move on their own otherwise).
- **Blocked by an AMR mid-WORK, or actively charging** → never evicted (those are the two truly non-preemptible states) — just wait; bounded by that action's own duration.
- **Blocked by anything else** (another AMR also mid-transit into the same cell, or genuinely can't be evicted right now) → try a **lossless reroute**: a neighboring cell that makes *exactly* as much progress toward the goal as the blocked move would have (never a net detour that would need "recovering" from later — that was a real source of a battery-draining bug during development, see [§10](#10-validation--testing-approach)). If no such cell exists, just wait.

Every one of these — collisions, the subset resolved by rerouting, and any conflict that's stayed active long enough to count as a genuine deadlock — feeds the dashboard's `collisions` / `reroutes` / `deadlocks` counters, each de-duplicated per ongoing episode (a single stuck pair only ever counts once, not once per frame it stays stuck).

### 6.7 Why there's no "force a robot through"

An earlier version of this logic *did* let a sufficiently long-stuck conflict force one side through by priority (lower AMR ID wins). During testing this turned out to be unsafe: it could let a robot claim a cell that another robot was still physically, legitimately occupying (e.g. mid-WORK), causing a real overlap. **Safety was prioritized over liveness** — the mechanism was removed entirely in favor of always resolving conflicts through either eviction (safe, since the occupant is physically relocated *first*) or waiting (bounded by whatever the occupant is doing). In practice, on an open grid, a true unbreakable deadlock essentially never arises, since a lossless reroute is almost always available; the very rare cases that can't resolve just produce a short, harmless stall rather than any risk of collision.

### 6.8 Battery & charging

- **Drain**: 1% per completed MOVE, 2% per completed WORK action (both applied only on actual completion of that atomic action, never continuously per-frame — keeping drain deterministic and tied to discrete progress rather than wall-clock time).
- **Proactive return**: an idle AMR whose battery drops to ≤25% (and isn't already standing on a Station) automatically switches to `ReturningToCharge` toward its nearest Station — this is the *only* reason an idle AMR ever moves on its own; a healthy idle AMR just stays put.
- **Post-task return**: the same 25% check runs the instant an AMR finishes a subtask, before it's allowed to go back to plain `Idle`.
- **Charging**: any AMR at rest exactly on a Station cell gains `100 × 0.1 × dt` battery every frame (i.e. ~10%/second), regardless of *why* it happens to be there, capped at 100 and accumulated with a small fractional buffer so it isn't lost to integer rounding.

### 6.9 Completion & the "stuck" condition

`isOver()` is true when either:

- **Every subtask instance across every task has been physically completed** — the normal, successful end state, or
- **The simulation is unrecoverably stuck**: at least one subtask instance remains incomplete, *and* there is no possible way to ever get more work done (no AMR anywhere has `battery > 0`, *and* no Station has any spare capacity left to deploy). This is checked explicitly every tick and is carefully distinguished from "every AMR is just currently busy" (which is not stuck — the Server's 30-second retry loop handles that) or "no AMRs were ever going to be needed" (an empty task list is trivially "complete," not "stuck").

### 6.10 Determinism, at a glance

Nowhere in this engine is there a random number generator. Every "who wins" decision has an explicit, fixed rule:

- Node/Station selection ties → lower ID first.
- AMR discovery ties (same distance) → lower ID first.
- Reroute candidate ties → whichever perpendicular direction is checked first in a fixed order.

Given the same `warehouse.json` and the same sequence of `dt` values, the simulation reproduces bit-for-bit identical outcomes every run (verified directly — see [§10](#10-validation--testing-approach)).

---

## 7. The `warehouse.json` Format

The Editor writes this; the Simulator's `parser.cpp` reads it. The Editor's `"fleet"` block is purely informational — the Simulator's parser never reads it, it's just a human-readable summary of what the JSON already contains.

```jsonc
{
  "width": 20,
  "height": 10,
  "minimum_node_range": 12,        // auto-computed by the Editor (see §4.3) — the
                                    // smallest node_range that still covers every cell
  "fleet": {                        // informational only — not read by the simulator
    "amrs_outside_stations": 3,
    "station_amrs": 10,
    "total_amrs": 13
  },
  "objects": [
    { "type": "Station", "id": 1, "x": 0,  "y": 4, "amr_count": 5 },
    { "type": "Node",    "id": 1, "x": 4,  "y": 3 },
    { "type": "AMR",     "id": 1, "x": 3,  "y": 6 },
    { "type": "Box",     "id": 1, "x": 6,  "y": 4, "task_timing": 5 }
  ],
  "tasks": [
    { "id": 1, "subtask": "TakeAndPut", "count": 5, "a": 1, "b": 7 }
  ]
}
```

| Object type | Required fields | Notes |
|---|---|---|
| `Box` | `id`, `x`, `y`, `task_timing` | `task_timing` = WORK duration (seconds) when an AMR is working this Box. |
| `Station` | `id`, `x`, `y`, `amr_count` | `amr_count` = spare AMRs available to *deploy* at runtime (decrements as the fleet grows; does **not** include AMRs placed directly on the grid). |
| `Node` | `id`, `x`, `y` | Its coverage radius is the *global* `minimum_node_range`, not per-node. |
| `AMR` | `id`, `x`, `y` | Placed AMRs start `idle`, at 100% battery. |

| `Task` field | Meaning |
|---|---|
| `id` | Arbitrary identifier (shown on the dashboard as the "current task"). |
| `count` | How many times this exact subtask repeats. |
| `subtask` | `"TakeAndPut"`, `"Operate"`, or `"Goto"`. |
| `a`, `b` | **`TakeAndPut`**: `a` = source Box ID, `b` = destination Box ID (MOVE→WORK→MOVE→WORK, same AMR the whole way). **`Operate`**: `a` = target Box ID, `b` unused (MOVE→WORK). **`Goto`**: `a` = x, `b` = y — a raw coordinate, no WORK step at all. |

---

## 8. Getting Started

Both apps currently target **Windows** with **Visual Studio 2022** (the `.bat` scripts hardcode the `"Visual Studio 17 2022"` CMake generator and an absolute SFML install path) — see [§11](#11-known-limitations--honest-gaps) for what it'd take to broaden that.

### 8.1 Prerequisites

- **CMake ≥ 3.28**
- **Visual Studio 2022** (with the C++ workload) — used purely as the compiler/generator; you don't need to open either project *in* the IDE.
- **Qt 6** (Core, Gui, Widgets) — for the Editor. Make sure `windeployqt` is on your `PATH` (it ships with Qt) for the deploy step.
- **SFML 3.0.2** — for the Simulator, installed at `C:\SFML-3.0.2-windows-vc17-64-bit\SFML-3.0.2` (matching the path baked into `amr-simulator/CMakeLists.txt`). If yours lives elsewhere, edit the `include_directories`/`link_directories` calls at the top of that file, and the `SFML_DIR` variable at the top of `deploy.bat`.

### 8.2 Build the Editor

```bat
cd warehouse-editor
configure_release.bat   REM cmake -S . -B build_release -G "Visual Studio 17 2022" -T host=x64 -A x64
build_release.bat       REM cmake --build build_release --config Release --target ALL_BUILD -j 12
deploy.bat              REM windeployqt + copy exe → dist\WarehouseEditor.exe
```

### 8.3 Build the Simulator

```bat
cd amr-simulator
configure_release.bat
build_release.bat
deploy.bat              REM copies AMRSimulator.exe + 3 SFML DLLs + resources\ → dist\
```

### 8.4 Run it

Simplest: run `warehouse-editor\dist\WarehouseEditor.exe`, design (or Load the included `amr-simulator\warehouse.json` sample), and click **Simulate**.

You can also run the Simulator completely standalone — it just needs a JSON path:

```bat
amr-simulator\dist\AMRSimulator.exe amr-simulator\warehouse.json
```

**For the smoothest "Simulate" button experience**, copy `AMRSimulator.exe`, its 3 SFML DLLs, and its `resources\` folder from `amr-simulator\dist\` into `warehouse-editor\dist\` (i.e. right next to `WarehouseEditor.exe`) before handing the build to anyone else — that's the first, and simplest, of the three layouts `launchSimulator()` looks for (see [§4.5](#45-how-simulate-actually-launches-the-simulator)).

---

## 9. Controls Reference

**Warehouse Editor** — drag an object type from the Palette onto the Grid to place it; click a placed object to inspect it in the InfoBox (or click empty space near it to erase, per `Grid`'s click handling); use the toolbar for New/Save/Load/Simulate; add/remove rows in the TaskWork panel to build the task list.

**AMR Simulator**

| Key | Action |
|---|---|
| `Escape` | Quit |
| `V` | Pause / resume the simulation |
| `↑` / `↓` | Scroll the dashboard panel |

---

## 10. Validation & Testing Approach

The Simulator can't easily be exercised headlessly through its real SFML window, so a throwaway console harness (parser + `Simulation` only, no rendering) was used during development to drive thousands of simulated ticks at once and check two hard invariants directly: **no two AMRs ever occupy the same cell**, and **no AMR ever enters a Box/Station cell that isn't its own current target**. That process caught and fixed three real bugs before the engine was considered solid:

1. **Battery deaths under congestion** — the original eligibility check under-budgeted for detour/wait overhead; fixed with the 50%-slack + flat-margin formula in [§6.4](#64-node-discovery-bfs-equivalent).
2. **Battery-draining "bad" reroutes** — an earlier reroute heuristic allowed net detours, which compounded under heavy congestion; fixed by requiring reroutes to be strictly lossless ([§6.6](#66-collision-avoidance-the-cell-reservation-system)).
3. **Real overlaps from forced deadlock-breaking** — described in [§6.7](#67-why-theres-no-force-a-robot-through); replaced with the always-safe eviction/wait model.

A dedicated regression test (a Box placed exactly on the straight-line path between two other Boxes, referenced by no task at all) confirms the pass-through fix directly: the AMR successfully detours around it and still completes its task. A 12-AMR, high-density stress scenario (well beyond the included sample warehouse's fleet size) completes fully with zero overlaps and identical results across repeated runs, confirming both safety and determinism under load.

---

## 11. Known Limitations & Honest Gaps

Worth knowing before presenting or grading this against the literal SIH26123 text:

- **Hierarchical, not literally peer-to-peer.** The problem statement asks for decentralized communication "without a central server." What's actually implemented is a deliberate simplification of that: a logical Server still orchestrates *task requests* (Nodes and AMRs never see the global task list), and AMR-to-AMR "awareness" is a shared reservation table inside one process rather than literal peer-to-peer messages each robot owns independently. This keeps the simulation tractable and fully deterministic, but a strict reading of "no central server" would flag it.
- **Node discovery collapses a 3-message handshake into one assignment.** The spec describes AMRs responding `ACCEPT` and a Node then sending `ACK`/`NAK`; since this simulation is single-threaded and deterministic (no genuine race to arbitrate), eligible-AMR selection is direct rather than modeled as three separate message events.
- **Static objects are obstacles; "blocked aisles" are not representable.** Boxes and Stations are real, un-enterable obstructions (as of the pass-through fix), but the warehouse JSON/Editor has no concept of a wall or a temporary blockage — only Boxes, Stations, Nodes, and AMRs exist. A path around a *permanently* blocked aisle (as opposed to around a Box) can't be modeled.
- **This is a simulation, not distributed edge deployment.** Everything runs in one process on one machine; there's no actual per-robot edge hardware, and "Nodes" don't run on separate compute.
- **No formal makespan benchmark.** The success criterion of "≥20% faster than naive stop-and-wait" has not been measured — there's currently no stop-and-wait baseline implementation to compare against.
- **Windows/MSVC-only toolchain**, as shipped (hardcoded SFML path, Visual Studio generator, `.bat` scripts). Porting to Linux/macOS would mean parameterizing the SFML path in CMake and replacing the `.bat` scripts with an equivalent shell/CMake-only flow (the C++ itself is standard and portable).

None of this changes the actual runtime guarantees that were tested directly: within the given architecture, the fleet reaches full task completion, deterministically, with zero AMR-AMR overlaps and zero AMR/object pass-through, across everything thrown at it during development.

---

## 12. Extending the Project

Some natural next steps, roughly in order of how self-contained they are:

- **Stop-and-wait baseline + benchmark harness** — reuse the console test harness from [§10](#10-validation--testing-approach), add a "dumb" fully-serialized movement mode, and produce the makespan comparison the PS's success criteria ask for.
- **Cross-platform build** — parameterize the SFML path as a CMake cache variable / `find_package`, and add a Linux/macOS build path alongside the existing `.bat` scripts.
- **Richer Node protocol** — if you want to more literally satisfy "decentralized communication," the natural next step is turning the `AMRMode`/eviction logic in `simulation.cpp` into an actual asynchronous message queue per AMR, rather than the shared-state reservation system described in [§6.6](#66-collision-avoidance-the-cell-reservation-system) — a bigger, riskier change than anything done so far, so it hasn't been attempted here.
- **Static obstacles beyond Boxes/Stations** — the Editor's `Grid`/JSON schema would need a new object type (e.g. `"Wall"`), and `Simulation::isObjectCell()` is the one place in the engine that would need to grow to recognize it.
