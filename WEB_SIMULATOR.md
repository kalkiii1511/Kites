# Kites Web Simulator (connected frontend + C++ API)

This project adds a small HTTP adapter and a React frontend on top of the existing Kites simulator. The web app does **not** emulate instructions in JavaScript: assemble, step, run, reset, register reads/writes, and memory access are delegated to the Kites C++ engine. The Kites engine is the existing backend this project builds on; the web adapter, frontend, and their integration are the project-specific layer.

## What is connected

- Assembly and program loading through `ProcessorManager::loadProgram`.
- Single-step execution through `ProcessorManager::step`.
- Visible instruction-by-instruction execution through repeated `ProcessorManager::step` calls, with adjustable playback speed.
- Per-stage pipeline state (IF/ID/EX/MEM/WB), stall/forwarding/control-hazard events, and a live Datapath view fed by those fields.
- Instruction addresses, encodings, and source-line mappings returned after assembly for the execution panel.
- A synchronous run-to-completion API endpoint through `ProcessorManager::run`.
- Reset through `ProcessorManager::reset`.
- All 32 GPRs, all 32 FPRs, CSRs, PC, and execution counters.
- A 2-bit saturating-counter branch predictor (64-entry BTB + BHT) on every RV5 pipeline variant, with a predictor selector, live accuracy stats, and a BTB inspector in the Datapath tab.
- A Cache tab connected to the instruction cache, L1 data cache, and L2 unified cache, with live lines, hit/miss stats, and supported geometry/write/replacement settings.
- Lazy memory pages (256 bytes per request; API caps a request at 4096 bytes) and byte writes through Kites' `MemoryController`.
- Existing Kites VM choices: `rvss`, `rv5-nh-nf`, `rv5-h-nf`, `rv5-nh-f`, `rv5-h-f`.

The backend uses Kites' sparse `MainMemory`; the web UI does not allocate or render the entire 64-bit address space. The address must still be valid according to the configured Kites memory range.

## Requirements

- C++20 compiler
- CMake 3.19+
- Qt 6.9.2 with Core, Widgets, Network, and Concurrent modules (the Kites project already depends on these)
- Node.js 20+ and npm

## 1. Build and run the Kites API (macOS)

From the `Kites-connected` directory:

```bash
cmake -S . -B build \
  -DCMAKE_PREFIX_PATH="$HOME/Qt/6.9.2/macos" \
  -DDISABLE_GUI=ON
cmake --build build --target kites_web_api -j
./build/kites-web-api 8080
```

Change `CMAKE_PREFIX_PATH` to the Qt kit installed on your machine. If Qt is already discoverable by CMake, omit that argument.

The frontend connects to `http://127.0.0.1:8080`. The API process currently binds to all network interfaces.

## 2. Run the web frontend

Open a second terminal:

```bash
cd frontend
npm ci
npm run dev
```

Open `http://localhost:5173`. Keep the C++ API process running in the first terminal.

If the API is hosted somewhere else, set `VITE_KITES_API` before starting Vite, for example:

```bash
VITE_KITES_API=http://127.0.0.1:8080 npm run dev
```

## HTTP API

- `GET /api/health`
- `GET /api/state` (includes `pipeline`, `processor`, five `stages` with `stage`/`pc`/`valid`, and a `hazard` object with `type`, `stalled`, `forwarded`, `stalls`, `sourcePc`, `consumerPc`, `register`, and `fpr`)
- `POST /api/diagnostics` JSON: `{ "source": "...assembly..." }` (returns line and column diagnostics)
- `POST /api/program/assemble` JSON: `{ "source": "...assembly...", "vm": "rvss", "branchPredictor": "2bit" }`
- `POST /api/simulator/step` JSON: `{ "count": 1 }` (count is clamped to 1–1000)
- `POST /api/simulator/run` JSON: `{}`
- `POST /api/simulator/reset` JSON: `{}`
- `GET /api/cache` (instruction, L1 data, and L2 unified cache snapshots)
- `POST /api/cache/config` JSON: cache level, set count, line size, ways, replacement, write, and allocation policies
- `GET /api/memory?address=0x10000000&length=256`
- `POST /api/memory/write` JSON: `{ "address": "0x10000000", "value": 42 }`

## Current integration boundaries

- The web UI animates execution by requesting one simulator step at a time. Its speed slider adjusts the delay between steps, and Stop halts between requests. The direct `POST /api/simulator/run` endpoint remains synchronous and blocks the API event loop until Kites stops or the program exits.
- The `rvss` single-cycle VM and all four RV5 pipeline modes support I, M, F, and D instructions. The pipeline modes differ in dependency detection and forwarding; see the mode descriptions in the UI.
- The **Datapath** tab is connected to per-cycle `stages`, `hazard`, and branch predictor state in `GET /api/state`. The **Cache** tab reads and configures the instruction, L1 data, and L2 unified caches through the API. The register, execution, and memory views are connected to the actual C++ simulator.
- The pipeline modes are intentionally different: `rv5-nh-nf` and `rv5-nh-f` do not detect data hazards, so dependent instructions need programmer-inserted NOPs. `rv5-h-nf` and `rv5-h-f` stall automatically; `rv5-h-f` also forwards available results. The 2-bit predictor is available on all four pipeline variants, but on the NH variants it inherits the same hazard-free-code requirement because branch operands are read in EX.
- Assembly diagnostics are returned with line and column information and appear in the editor with underlines and a diagnostic list. The editor also has lightweight local syntax highlighting; highlighting is not a full language server.
- This is a development API. Its current listener binds to all network interfaces and it has no authentication. Do not expose it beyond a trusted development environment without access controls, request limits, and deployment hardening.
