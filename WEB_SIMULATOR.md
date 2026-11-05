# Kites Web Simulator (connected frontend + C++ API)

This folder adds a small HTTP API to the existing Kites simulator and a minimal React frontend. The web app does **not** emulate instructions in JavaScript: assemble, step, run, reset, register reads/writes, and memory access are delegated to the Kites C++ engine.

## What is connected

- Assembly and program loading through `ProcessorManager::loadProgram`.
- Single-step execution through `ProcessorManager::step`.
- Visible instruction-by-instruction execution through repeated `ProcessorManager::step` calls, with adjustable playback speed.
- Instruction addresses, encodings, and source-line mappings returned after assembly for the execution panel.
- A synchronous run-to-completion API endpoint through `ProcessorManager::run`.
- Reset through `ProcessorManager::reset`.
- All 32 GPRs, all 32 FPRs, CSRs, PC, and execution counters.
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

The API listens on `http://127.0.0.1:8080`.

## 2. Run the web frontend

Open a second terminal:

```bash
cd frontend
npm install
npm run dev
```

Open `http://localhost:5173`. Keep the C++ API process running in the first terminal.

If the API is hosted somewhere else, set `VITE_KITES_API` before starting Vite, for example:

```bash
VITE_KITES_API=http://127.0.0.1:8080 npm run dev
```

## HTTP API

- `GET /api/health`
- `GET /api/state`
- `POST /api/program/assemble` JSON: `{ "source": "...assembly...", "vm": "rvss" }`
- `POST /api/simulator/step` JSON: `{ "count": 1 }` (count is clamped to 1–1000)
- `POST /api/simulator/run` JSON: `{}`
- `POST /api/simulator/reset` JSON: `{}`
- `GET /api/memory?address=0x10000000&length=256`
- `POST /api/memory/write` JSON: `{ "address": "0x10000000", "value": 42 }`

## Current integration boundaries

- The web UI animates execution by requesting one simulator step at a time. Its speed slider adjusts the delay between steps, and Stop halts between requests. The direct `POST /api/simulator/run` endpoint remains synchronous and blocks the API event loop until Kites stops or the program exits.
- The Kites CLI documentation notes that the pipelined VMs currently support I/M only; F/D support is currently available in the single-cycle `rvss` VM. The UI offers the VM types Kites itself exposes and does not claim that every pipeline implements F/D.
- Datapath and cache visualizations are not yet wired to endpoints. The register, execution, and memory views are connected to the actual C++ simulator.
- Assembly diagnostics are returned as an API error message. Source-mapped diagnostic tables can be added after exposing Kites' assembler diagnostic structures through the API.
- This is a local development API. Do not expose it directly to the public internet without authentication, request limits, and deployment hardening.
