# Kites RISC-V Simulator

Kites is a RISC-V simulator and assembly editor. The `rvss` single-cycle processor and five-stage pipeline variants support the I, M, F, and D extensions. Hazard detection and forwarding behavior depends on the selected five-stage mode.

## Requirements

- C++20 compiler
- CMake 3.19 or newer
- Qt 6.9.2 or newer
- Node.js 20 or newer and npm (for the web simulator)

## Run the web simulator

The web interface uses the existing Kites C++ simulator through a small HTTP API adapter in this project. The Kites engine is the upstream backend this project builds on; the web adapter, frontend, and their integration are the project-specific layer. Start the API and the frontend in two separate terminals from the repository root.

### 1. Build the API

Set the Qt path to the Qt installation for your platform and compiler. If CMake already finds Qt, omit `CMAKE_PREFIX_PATH`.

```bash
cmake -S . -B build \
  -DCMAKE_PREFIX_PATH="/path/to/Qt/6.9.2/your-kit" \
  -DDISABLE_GUI=ON
cmake --build build --target kites_web_api -j2
```

If this repository's test preset or another CMake configuration already created `build`, configure it again with `-DDISABLE_GUI=ON` before building the API. CMake still needs the Qt Core, Widgets, Network, and Concurrent development modules.

### 2. Start the API

```bash
KITES_DATA_DIR=.kites-data ./build/kites-web-api 8080
```

The frontend connects to `http://127.0.0.1:8080`. Keep this terminal open. The current API listener binds to all network interfaces and has no authentication, so use it only in a trusted development environment. On Windows, run `build\kites-web-api.exe 8080` and set `KITES_DATA_DIR` using the shell’s environment variable syntax.

### 3. Start the frontend

In a second terminal:

```bash
cd frontend
npm ci
VITE_KITES_API=http://127.0.0.1:8080 npm run dev -- --port 5173
```

Open [http://localhost:5173](http://localhost:5173). Keep both the API and frontend running while using the simulator.

The **Editor** includes assembly highlighting, diagnostics, examples, and run/step controls. **Datapath** shows the live IF/ID/EX/MEM/WB stages, hazards, forwarding, branch-prediction state, and execution counters. **Memory** inspects and edits memory bytes. **Cache** exposes the instruction cache, L1 data cache, and L2 unified cache, including cache lines, hit/miss statistics, and supported geometry and policy settings.

## Project scope

This repository builds on the existing Kites simulator and its C++ execution engine; the team does not claim authorship of the underlying Kites backend. The project-specific work is the browser interface, the HTTP adapter that connects it to Kites, and the frontend-to-simulator integration and extensions maintained in this repository. See [the integration guide](WEB_SIMULATOR.md) for the endpoint and feature boundaries.

## Desktop application

Prebuilt Linux and Windows releases are available on the [Kites releases page](https://github.com/hghit-harshit/Kites/releases). To build the desktop application, install Qt 6.9.2 or newer, then configure a separate GUI-enabled build directory:

```bash
cmake -S . -B build-desktop \
  -DCMAKE_PREFIX_PATH="/path/to/Qt/6.9.2/your-kit" \
  -DDISABLE_GUI=OFF
cmake --build build-desktop -j2
```

## Other documentation

- [Testing](docs/TESTING.md)
- [CLI](docs/CLI.md)
