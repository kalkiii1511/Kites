# Kites RISC-V Simulator

Kites is a RISC-V simulator and assembly editor. The `rvss` single-cycle processor supports the I, M, F, and D extensions. The five-stage processor variants support I and M.

## Requirements

- C++20 compiler
- CMake 3.19 or newer
- Qt 6.9 or newer
- Node.js 20 or newer and npm (for the web simulator)

## Run the web simulator

The web interface uses the Kites C++ engine through a local HTTP API. Start the API and the frontend in two separate terminals from the project directory.

### 1. Build the API

Set the Qt path to the Qt installation for your platform and compiler. If CMake already finds Qt, omit `CMAKE_PREFIX_PATH`.

```bash
cmake -S . -B build \
  -DCMAKE_PREFIX_PATH="/path/to/Qt/6.9.2/your-kit" \
  -DDISABLE_GUI=ON
cmake --build build --target kites_web_api -j2
```

### 2. Start the API

```bash
KITES_DATA_DIR=.kites-data ./build/kites-web-api 8080
```

The API listens at `http://127.0.0.1:8080`. Keep this terminal open. On Windows, run `build\kites-web-api.exe 8080` and set `KITES_DATA_DIR` using the shell’s environment variable syntax.

### 3. Start the frontend

In a second terminal:

```bash
cd frontend
npm install
VITE_KITES_API=http://127.0.0.1:8080 npm run dev -- --port 5173
```

Open [http://localhost:5173](http://localhost:5173). Keep both the API and frontend running while using the simulator.

The Editor’s **Run** control assembles the current source and steps through its instructions. Use the speed slider to adjust playback. The execution panel shows each instruction’s address, encoding, and source line. Datapath and cache visualizations are not connected yet.

For API endpoints and integration details, see [WEB_SIMULATOR.md](WEB_SIMULATOR.md).

## Desktop application

Prebuilt Linux and Windows releases are available on the [Kites releases page](https://github.com/hghit-harshit/Kites/releases). To build the desktop application, install Qt 6.9 or newer, then configure and build with CMake:

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH="/path/to/Qt/6.x/your-kit"
cmake --build build -j2
```

## Other documentation

- [Testing](docs/TESTING.md)
- [CLI](docs/CLI.md)
