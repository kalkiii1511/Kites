# Kites project handoff notes

This note summarizes the web simulator work so future sessions can start here instead of re-reading the whole repository.

## Project layout

- `src/`: C++ RISC-V simulator and Qt desktop application.
- `web_api/main.cpp`: local HTTP API that uses the existing C++ simulator.
- `frontend/`: React + TypeScript + Vite browser interface.
- `WEB_SIMULATOR.md`: API and integration notes.
- `README.md`: project overview and build/run instructions.

## Changes made

- Added a browser-based simulator frontend connected to the C++ engine. Assembly, machine state, registers, memory reads/writes, and stepping come from the API.
- Added the `kites_web_api` CMake target and endpoints for health, assembly, state, stepping, run, reset, and memory.
- Run in the Editor assembles the current source and steps through instructions so execution can be animated. The speed slider adjusts playback from 1 to 20 steps per second. The active source line and instruction row are highlighted; instruction rows include address, encoding, assembly, and source line.
- The API returns assembled instruction metadata using the assembler's mapping and encoding data. Assembly resets the processor before loading the new program.
- Set the API step delay to 1 ms. A zero delay can cause the processor wait condition to block indefinitely during multi-instruction execution.
- Changed the sample assembly to use Kites-supported `.text` syntax. Removed the process-wide `exit(0)` from the RVSS exit syscall so the API process survives guest program exit.
- Added `KITES_DATA_DIR` to select a writable simulator data directory. The web API can run with `KITES_DATA_DIR=.kites-data` from the project root.
- Added address and byte-value search in the Memory tab. Memory rows load to fill the available panel height, while the register sidebar remains visible.
- Datapath and cache visualizations remain unchanged and are not connected to the web API.
- Updated README and web simulator documentation. `.gitignore` excludes `/build/`, frontend generated files, and local simulator state.

## Running the web simulator

Requirements: C++20, CMake 3.19+, Qt 6.9+, Node.js 20+, npm.

Build and start the API from the project root:

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH="/path/to/Qt/6.9.2/your-kit" -DDISABLE_GUI=ON
cmake --build build --target kites_web_api -j2
KITES_DATA_DIR=.kites-data ./build/kites-web-api 8080
```

In a second terminal:

```bash
cd frontend
npm install
VITE_KITES_API=http://127.0.0.1:8080 npm run dev -- --port 5173
```

Open `http://localhost:5173`. Keep both processes running. CMake can omit `CMAKE_PREFIX_PATH` when it already finds Qt.

## Build checks performed

- `cmake --build build --target kites_web_api -j2` succeeded.
- `npx vite build` in `frontend/` succeeded.
- The API was checked with the built-in 8-instruction example; it completed with `a0` and `a1` equal to `0x34`.
- `npm run build` may fail in an install missing `@types/react` and `@types/react-dom`; the Vite production bundle build succeeds.

## Git handoff

- Target remote: `https://github.com/kalkiii1511/Kites.git`
- Work was pushed to branch `frontend`.
- Latest earlier commit: `c58965f` (`Add connected web simulator frontend`).
