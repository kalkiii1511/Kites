# Kites RISC-V Simulator

Kites is a RISC-V simulator and assembly editor. The web simulator provides an
assembly editor, a live instruction execution view, register visualization, and
a console. It supports integer and floating-point registers and the RV64IMFD
single-cycle processor, as well as the available five-stage processor models.

## Run the web simulator

The FastAPI backend serves the frontend, so no separate Node.js or frontend
build step is needed. The launcher builds the headless Kites CLI, installs the
Python API dependencies in `.venv`, and starts the web server.

### macOS with Homebrew

Install Qt 6.9.2 or later, CMake, and Python 3.10 or later, then run:

```sh
brew install qt cmake
./web_api/run.sh
```

Open [http://127.0.0.1:8000](http://127.0.0.1:8000). Stop the server with
`Ctrl+C` in the terminal.

If Qt is installed outside Homebrew, set `CMAKE_PREFIX_PATH` to the Qt
installation prefix before starting the launcher:

```sh
CMAKE_PREFIX_PATH=/path/to/Qt/6.9.2/macos ./web_api/run.sh
```

### Start the API after building it

If the CLI and Python dependencies are already installed, start the API
directly from the repository root:

```sh
KITES_CLI="$PWD/build/web_api/kites-cli" \
  .venv/bin/python -m uvicorn web_api.main:app --host 127.0.0.1 --port 8000
```

For backend auto-reload during development, add `--reload`. The API health
check is at `/api/health`; interactive API documentation is at `/docs`.

## Use the web simulator

1. Start the API and open `http://127.0.0.1:8000`.
2. Edit the assembly in **Editor**, or use **Load example**.
3. Choose a processor model and set the execution pace with the slider.
4. Select **Run program**. The compiled instruction list appears beside the
   editor, with the current instruction highlighted as the program executes.
5. Click an instruction's breakpoint marker to pause before that address.
6. View integer, floating-point, and CSR values in the register panel. The
   execution output panel includes console, disassembly, and memory results.

The API URL field in the top bar can point the frontend at a different running
Kites API. The web frontend files are in [`web/`](web/), and the API and launch
script are in [`web_api/`](web_api/).

## Build the desktop application

The desktop application requires Qt 6.9.2 or later. Configure CMake with the Qt
installation prefix, then build from the configured build directory:

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH="/path/to/Qt/6.9.2/<platform>"
cmake --build build
```

## CLI, tests, and contribution

- [CLI usage](docs/CLI.md)
- [Testing guide](docs/TESTING.md)
- [Contributing](CONTRIBUTING.md)
- [Web API details](web_api/README.md)

Prebuilt desktop releases are available on the
[GitHub Releases page](https://github.com/hghit-harshit/Kites/releases).
