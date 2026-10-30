# Kites HTTP API

This service exposes the existing Kites CLI to a web client. On macOS with
Homebrew Qt installed, start both with:

```sh
brew install qt
./web_api/run.sh
```

The script configures and builds `kites-cli`, creates a local Python virtual
environment, installs the API dependencies, and starts Uvicorn. Qt 6.9.2 or
later and CMake are required. If Qt is installed somewhere other than the
Homebrew prefix, set `CMAKE_PREFIX_PATH` before running the script.

To start only the API after building and installing dependencies manually:

```sh
KITES_CLI="$PWD/build/web_api/kites-cli" .venv/bin/python -m uvicorn web_api.main:app --reload
```

Open `http://127.0.0.1:8000` for the web workbench. It includes the assembly
editor, processor picker, console/disassembly/memory tabs, and integer, floating
point, and CSR register banks. The API URL field in the top bar can point the
frontend at another running Kites API.

The compiled binary appears beside the editor as soon as assembly finishes. The
current PC streams from the CLI while the program runs and highlights the active
instruction in red. The pace selector controls the delay between instructions;
`Step by step` is the default, with slower, steadier, and instant modes available.
Drag the pace slider to set the delay from instant to 1.2 seconds per step. Click
the breakpoint circle beside any compiled instruction to toggle it; execution
stops before reaching a selected address and leaves the matching register state
visible.

The browser sends assembly to `POST http://localhost:8000/api/simulate`:

```json
{
  "source": "addi a0, zero, 42\n",
  "vm": "rvss",
  "memory_dumps": []
}
```

The JSON response includes the simulator exit status, assembly diagnostics,
register state, disassembly, optional memory dump, and captured CLI output.
`GET /api/health` is available for a connection check. Supported VM names are
`rvss`, `rv5-nh-nf`, `rv5-h-nf`, `rv5-nh-f`, and `rv5-h-f`.

Configure deployment with environment variables:

- `KITES_CLI`: CLI executable or command (defaults to `kites-cli` on `PATH`).
- `KITES_SIMULATION_TIMEOUT`: maximum execution time in seconds (defaults to 60).
- `KITES_CORS_ORIGINS`: comma-separated web origins (defaults to localhost ports
  3000 and 5173).
- `KITES_API_HOST` and `KITES_API_PORT`: bind address and port for the launcher
  (defaults to `127.0.0.1:8000`).

This initial adapter runs programs to completion. Interactive step, pause,
undo/redo, and persistent simulator sessions need an API around Kites' REPL or
`ProcessorManager`; they are not exposed by the batch CLI yet.
