# Web Simulation Using Kites API Extension

## Project Report

**Team:** B. Kalki Suvarn (ES24BTECH11011) and C. Hemish Reddy (ES24BTECH11012)  
**Date:** 22 November 2026

## Abstract

This project makes the Kites RISC-V simulator usable through a browser. A React and TypeScript interface sends assembly source and simulator actions to a small C++ HTTP adapter. The adapter calls the existing Kites C++ engine and returns processor state for display. The browser does not emulate the instructions itself.

The visible application includes an assembly editor, examples, diagnostics, a step and run workflow, register and memory inspection, a five-stage datapath, branch prediction views, and cache configuration and statistics. The report explains how these parts connect and how to run the project.

## Scope and ownership

The Kites simulator and its C++ execution backend are the existing foundation of this work. The team builds on that backend; this report does not claim authorship of the full Kites engine. The project-specific layer includes the browser interface, the HTTP adapter, and the integration that connects browser actions to Kites processor, memory, pipeline, predictor, and cache state. The repository also contains feature and test changes maintained on top of the Kites base.

The distinction matters when describing the system: Kites performs assembly and instruction execution; the web layer requests those operations and renders the returned state.

## System architecture

The browser and backend communicate over JSON HTTP requests. The frontend source is in `frontend/`; the C++ adapter is in `web_api/main.cpp`. The adapter uses `ProcessorManager` and other existing Kites classes to load programs, step or run a processor, and read state. The backend remains the source of truth for registers, memory, stages, hazards, predictor data, and cache state.

The main operations are assembly and load, single-step, run-to-completion, reset, state retrieval, memory reads and writes, diagnostics, and cache inspection/configuration. The detailed sequences below show what the browser actually sends and how the response travels back.

## What is visible in the web simulation

### Editor

The Editor tab provides assembly examples and an editor with syntax coloring for directives, instructions, registers, labels, numbers, strings, and comments. A lightweight client-side fallback identifies unknown instruction names, while the diagnostics API asks the Kites assembler diagnostic facility for line and column information. Errors are displayed in the editor as line markers, underlines, and a diagnostic list. Selecting an example places its assembly text in the editor; the user then chooses or accepts a VM and assembles/runs the source through the same workflow.

### Execution and datapath

The execution view displays decoded instructions and source line mappings. For pipelined processors it shows IF, ID, EX, MEM, and WB state, along with hazard notices and execution counters. The Datapath view exposes pipeline and branch predictor state, including BTB entries and two-bit counter labels. The RVSS option displays its single-cycle execution state.

### Registers and memory

The interface shows all 32 integer and 32 floating-point registers, control and status registers, and the program counter. The Memory tab reads and writes memory by address, returning a bounded range of bytes per request. The Kites backend uses sparse memory; the browser loads the requested range instead of allocating the full address space.

### Cache

The Cache tab exposes three backend caches: instruction cache, L1 data cache, and L2 unified cache. It shows capacity, hits, misses, hit rate, write-backs, and cache lines. Supported controls include cache geometry, LRU/FIFO replacement, write-through/write-back behavior, and write-allocate/no-write-allocate behavior. Exact selectable settings depend on the validation in the API.

## Assemble request lifecycle

Clicking Assemble calls the React `assemble()` handler. It sends the current editor text, selected VM, and branch predictor to the C++ HTTP adapter. The adapter chooses the manager's VM, applies the predictor, resets the current processor, and calls `ProcessorManager::loadProgram(source)`. The manager invokes the Kites assembler, then loads the resulting `AssembledProgram` into its current VM.

```mermaid
sequenceDiagram
    actor User
    participant UI as React App
    participant API as C++ HTTP adapter
    participant PM as ProcessorManager
    participant ASM as Kites assembler
    participant VM as Current VM
    User->>UI: Click Assemble
    UI->>API: POST /api/program/assemble {source, vm, branchPredictor}
    API->>PM: select VM and predictor; reset; loadProgram(source)
    PM->>ASM: assemble source text
    ASM-->>PM: AssembledProgram
    PM->>VM: LoadProgram(program)
    API->>PM: read state() via manager getters
    PM-->>API: registers, ProcessorState, counters, predictor
    API-->>UI: JSON state + instructions + source-line map
    UI->>User: setMachine(); setInstructions(); render
```

## Run and step lifecycle

The Run button assembles first, then repeats single-step requests. For every step, `ProcessorManager::step()` calls the current VM's `StepAndNotify()`. The VM executes a cycle, updates its registers, memory effects, PC, pipeline registers, hazard and counters, creates its `ProcessorState`, and notifies listeners. The adapter then reads the current values through `ProcessorManager` and returns a JSON snapshot. React stores the snapshot and redraws the views. This flow advances one stateful processor; it does not launch parallel processor instances.

```mermaid
sequenceDiagram
    actor User
    participant UI as React App
    participant API as C++ HTTP adapter
    participant PM as ProcessorManager
    participant VM as Current VM
    User->>UI: Click Run
    UI->>API: POST /api/program/assemble
    API->>PM: reset and load source into selected VM
    API-->>UI: initial state and instruction map
    loop until complete or stopped
        UI->>API: POST /api/simulator/step {count: 1}
        API->>PM: step()
        PM->>VM: StepAndNotify() then Step()
        VM-->>PM: updated machine and ProcessorState
        API->>PM: read registers, stages, hazards, metrics
        PM-->>API: current snapshot
        API-->>UI: JSON state and message
        UI->>UI: setMachine(); map active PC to source line
    end
```

The separate `POST /api/simulator/run` endpoint calls `ProcessorManager::run()` synchronously. The current Run button instead uses repeated `/api/simulator/step` calls so it can render each intermediate state.

## JSON request and response shape

The assembly request is shaped like this (with the full assembly source in the `source` field):

```json
{
  "source": ".text\n_start:\n  addi t0, zero, 3\n  ...",
  "vm": "rv5-h-f",
  "branchPredictor": "2bit"
}
```

A step request from the Run button is `{"count": 1}`. The step response contains a new `pc`, all GPR/FPR/CSR values, `cycles`, `instructionsRetired`, `stallCycles`, pipeline `stages`, a `hazard` object, predictor metrics and entries, completion flags, and a message. The assemble response additionally includes the instruction encoding/address/source-line map. The frontend reads `stages` and `hazard` to refresh the pipeline view, reads `gpr`/`fpr`/`csr` for register panels, and uses instruction source-line mappings to highlight the editor.

The app also polls `/api/health` and `/api/state` every three seconds. This refresh reads the current state without advancing the VM. During the Run loop, each `/api/simulator/step` response already carries a fresh state snapshot.

## Error and editor flow

As the user types, a 450 ms debounce posts source text to `/api/diagnostics`. The adapter calls Kites diagnostics and returns line/column/message objects; React draws line markers and underlines. If Assemble fails, the API returns a readable error and available diagnostics. This is separate from VM execution: a failed assembly does not enter the step loop.

## Five-stage pipeline

The RV5 processor uses five stages. Different modes let users compare automatic hazard handling and forwarding:

| Mode | Hazard detection | Forwarding | Programming behavior |
|---|---|---|---|
| `rv5-nh-nf` | No | No | Insert NOPs for dependencies. |
| `rv5-h-nf` | Yes | No | Stalls for detected read-after-write dependencies. |
| `rv5-nh-f` | No | Yes | Forwarding is available, but hazards that require a stall must be handled by the program. |
| `rv5-h-f` | Yes | Yes | Forwards available results and stalls on load-use dependencies. |

The pipeline base moves instruction and control information through pipeline registers. The hazard detection unit compares the current instruction's source registers with pending destinations. The forwarding modes select newer values from later pipeline stages when the register file has not yet been updated. Stores and branch operands also participate in dependency handling. The F and D extension instructions execute through the same five-stage modes; their integer and floating-point sources and destinations are classified separately so the proper hazards and write-back paths are used.

## Branch prediction

The pipeline offers static-not-taken and two-bit prediction. The two-bit predictor combines a 64-entry branch target buffer with a two-bit saturating counter table. An unseen conditional branch predicts not taken. Once a target is recorded, the counter predicts not taken in states `00` and `01`, and taken in states `10` and `11`. Actual outcomes move the counter one step toward the corresponding end state, saturating at `00` or `11`. Unconditional jumps use their recorded target. Prediction totals, accuracy, mispredictions, BTB hits, and entries are available in the interface.

## Memory hierarchy and cache behavior

Memory instructions pass through the Kites memory controller and the configured caches before reaching backing memory. The web API returns snapshots of the instruction cache, L1 data cache, and L2 unified cache, including configuration, statistics, and line contents. Reconfiguring a cache is validated for supported power-of-two dimensions and policy options. The Cache tab is an inspection and configuration view; its statistics reflect accesses made by the simulator.

## Verification

The repository uses GoogleTest through CMake. The recorded full-suite run for this working tree completed **1,628 tests with 1,628 passing**. Focused floating-point coverage completed **49 tests passing**, including arithmetic, conversion, memory, hazard/forwarding, and double-precision checks across all four RV5 modes. These are project test-binary results, not a count of GoogleTest features or independent production benchmarks.

Coverage includes assembler and instruction execution, integer operations, floating-point behavior, hazards/forwarding, branch prediction, cache behavior and policies, memory accesses, and UI/API-facing simulator integration. Test results demonstrate correctness for the included cases; they do not prove that every legal RISC-V program or hardware timing behavior is modeled.

## Running the web simulator

Requirements: C++20, CMake 3.19+, Qt 6.9.2+ with Core, Widgets, Network, and Concurrent, and Node.js 20+ with npm.

From the repository root, configure and build the API (replace the Qt path with the installed kit, or omit it if CMake already finds Qt):

```bash
cmake -S . -B build \
  -DCMAKE_PREFIX_PATH="/path/to/Qt/6.9.2/your-kit" \
  -DDISABLE_GUI=ON
cmake --build build --target kites_web_api -j2
```

Start the API in the first terminal:

```bash
KITES_DATA_DIR=.kites-data ./build/kites-web-api 8080
```

Start the frontend in another terminal:

```bash
cd frontend
npm ci
VITE_KITES_API=http://127.0.0.1:8080 npm run dev -- --port 5173
```

Open `http://localhost:5173`. Keep both processes running. The default frontend API URL is `http://127.0.0.1:8080`.

## Project deliverables and AI usage

The finished project provides an assembly code editor with example programs, syntax highlighting, and assembler diagnostics; a browser-based workflow to assemble, step, run, stop, and reset programs; execution path visualization with source mapping, pipeline stages, hazards, and forwarding; static-not-taken and two-bit branch prediction; and inspection of registers, memory, and instruction, L1 data, and L2 unified caches. The web interface connects to the existing Kites processor and memory implementation through this project's HTTP API adapter. The Kites backend is upstream software; this project builds the web simulation and integration on top of it.

**AI usage level: Moderate.** LLM tools were used to assist with code generation and automated testing. All design and implementation decisions were made by the project team; the team reviewed and tested the resulting work.

## Repository pointers

- `frontend/src/App.tsx`: web interface, requests, and visible tabs.
- `frontend/src/examples.ts` and `frontend/src/examples/`: example program catalog.
- `web_api/main.cpp`: HTTP adapter and JSON state serialization.
- `src/processor/rv5s/`: five-stage pipeline, hazard logic, and branch prediction.
- `src/processor/cache/` and `src/processor/memory_controller.*`: cache and memory implementation used by Kites.
- `test/`: GoogleTest coverage.
- `README.md` and `WEB_SIMULATOR.md`: setup and integration details.
