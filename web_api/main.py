"""Small HTTP adapter for the Kites command-line simulator."""

from __future__ import annotations

import json
import os
import platform
import shlex
import subprocess
import tempfile
import asyncio
import time
from pathlib import Path
from typing import Any

from fastapi import FastAPI, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel, Field
from fastapi.staticfiles import StaticFiles
from fastapi.responses import StreamingResponse


SUPPORTED_VMS = {"rvss", "rv5-nh-nf", "rv5-h-nf", "rv5-nh-f", "rv5-h-f"}
MAX_SOURCE_BYTES = 64 * 1024
_cli_setting = os.getenv("KITES_CLI")
if _cli_setting:
    CLI_COMMAND = [_cli_setting] if Path(_cli_setting).exists() else shlex.split(_cli_setting)
else:
    _built_cli = Path(__file__).resolve().parent.parent / "build" / "web_api" / (
        "kites-cli.exe" if os.name == "nt" else "kites-cli"
    )
    CLI_COMMAND = [str(_built_cli)] if _built_cli.exists() else ["kites-cli"]
SIMULATION_TIMEOUT = float(os.getenv("KITES_SIMULATION_TIMEOUT", "60"))

app = FastAPI(title="Kites Simulator API", version="0.1.0")
allowed_origins = [
    origin.strip()
    for origin in os.getenv("KITES_CORS_ORIGINS", "http://localhost:3000,http://localhost:5173").split(",")
    if origin.strip()
]
app.add_middleware(
    CORSMiddleware,
    allow_origins=allowed_origins,
    allow_methods=["GET", "POST"],
    allow_headers=["*"],
)


class SimulationRequest(BaseModel):
    source: str = Field(min_length=1)
    vm: str = "rvss"
    step_delay_ms: int = Field(default=450, ge=0, le=5000)
    breakpoints: list[int] = Field(default_factory=list)
    memory_dumps: list[tuple[str, int]] = Field(default_factory=list)


def _read_json(path: Path) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return None


@app.get("/api/health")
def health() -> dict[str, str]:
    return {"status": "ok"}


@app.post("/api/simulate")
def simulate(request: SimulationRequest) -> dict[str, Any]:
    if request.vm not in SUPPORTED_VMS:
        raise HTTPException(status_code=422, detail=f"Unsupported VM '{request.vm}'.")
    if len(request.source.encode("utf-8")) > MAX_SOURCE_BYTES:
        raise HTTPException(status_code=413, detail="Assembly source exceeds 64 KiB.")
    if len(request.memory_dumps) > 8:
        raise HTTPException(status_code=422, detail="At most 8 memory ranges may be requested.")
    if len(request.breakpoints) > 128 or any(address < 0 or address % 4 != 0 for address in request.breakpoints):
        raise HTTPException(status_code=422, detail="Breakpoints must be aligned instruction addresses (maximum 128).")

    with tempfile.TemporaryDirectory(prefix="kites-api-") as workdir:
        root = Path(workdir)
        source_file = root / "program.s"
        source_file.write_text(request.source, encoding="utf-8")
        command = [*CLI_COMMAND, str(source_file), "--vm", request.vm, "--step-delay", str(request.step_delay_ms)]
        for address in request.breakpoints:
            command.extend(["--breakpoint", hex(address)])
        for address, rows in request.memory_dumps:
            if rows < 1 or rows > 256:
                raise HTTPException(status_code=422, detail="Memory row count must be between 1 and 256.")
            command.extend(["--mem-dump", address, str(rows)])

        # Kites writes its result files under the current working directory on
        # Linux/Windows. Isolating each request also prevents concurrent calls
        # from overwriting one another's vm_state files. On macOS Kites uses
        # ~/Library/Application Support/Kites, so give each invocation a private HOME.
        child_env = os.environ.copy()
        if platform.system() == "Darwin":
            child_env["HOME"] = str(root)
        try:
            result = subprocess.run(
                command,
                cwd=root,
                capture_output=True,
                text=True,
                env=child_env,
                timeout=SIMULATION_TIMEOUT,
                check=False,
            )
        except FileNotFoundError as exc:
            raise HTTPException(
                status_code=503,
                detail="Kites CLI was not found. Set KITES_CLI to its executable path.",
            ) from exc
        except subprocess.TimeoutExpired as exc:
            raise HTTPException(status_code=408, detail="Simulation exceeded its time limit.") from exc

        state = root / "vm_state"
        if platform.system() == "Darwin":
            mac_state = root / "Library" / "Application Support" / "Kites" / "vm_state"
            if mac_state.exists():
                state = mac_state
        execution_trace: list[int] = []
        latest_registers: Any = None
        breakpoint: dict[str, int] | None = None
        visible_stdout: list[str] = []
        trace_truncated = False
        for line in result.stdout.splitlines():
            if line.startswith("[KITES_TRACE_PC]"):
                try:
                    execution_trace.append(int(line.removeprefix("[KITES_TRACE_PC]").strip(), 16))
                except ValueError:
                    continue
            elif line.startswith("[KITES_REGS]"):
                try:
                    latest_registers = json.loads(line.removeprefix("[KITES_REGS]").strip())
                except json.JSONDecodeError:
                    continue
            elif line.startswith("[KITES_BREAKPOINT]"):
                try:
                    breakpoint = {"pc": int(line.removeprefix("[KITES_BREAKPOINT]").strip(), 16)}
                except ValueError:
                    continue
            elif line.startswith("[KITES_TRACE_TRUNCATED]"):
                trace_truncated = line.endswith("true")
            else:
                visible_stdout.append(line)

        return {
            "success": result.returncode == 0,
            "exit_code": result.returncode,
            "stdout": "\n".join(visible_stdout),
            "stderr": result.stderr,
            "trace": execution_trace,
            "trace_truncated": trace_truncated,
            "breakpoint": breakpoint,
            "errors": _read_json(state / "errors_dump.json"),
            "registers": latest_registers if breakpoint is not None else (_read_json(state / "registers_dump.json") or latest_registers),
            "disassembly": (state / "disassembly.txt").read_text(encoding="utf-8")
            if (state / "disassembly.txt").exists()
            else None,
            "memory": _read_json(state / "memory_dump.json"),
        }


def _sse(event: str, payload: Any) -> str:
    return f"event: {event}\ndata: {json.dumps(payload)}\n\n"


@app.post("/api/simulate/stream")
async def simulate_stream(request: SimulationRequest) -> StreamingResponse:
    if request.vm not in SUPPORTED_VMS:
        raise HTTPException(status_code=422, detail=f"Unsupported VM '{request.vm}'.")
    if len(request.source.encode("utf-8")) > MAX_SOURCE_BYTES:
        raise HTTPException(status_code=413, detail="Assembly source exceeds 64 KiB.")
    if len(request.memory_dumps) > 8:
        raise HTTPException(status_code=422, detail="At most 8 memory ranges may be requested.")
    for _, rows in request.memory_dumps:
        if rows < 1 or rows > 256:
            raise HTTPException(status_code=422, detail="Memory row count must be between 1 and 256.")
    if len(request.breakpoints) > 128 or any(address < 0 or address % 4 != 0 for address in request.breakpoints):
        raise HTTPException(status_code=422, detail="Breakpoints must be aligned instruction addresses (maximum 128).")

    async def events():
        with tempfile.TemporaryDirectory(prefix="kites-api-") as workdir:
            root = Path(workdir)
            source_file = root / "program.s"
            source_file.write_text(request.source, encoding="utf-8")
            command = [*CLI_COMMAND, str(source_file), "--vm", request.vm, "--step-delay", str(request.step_delay_ms)]
            for address in request.breakpoints:
                command.extend(["--breakpoint", hex(address)])
            for address, rows in request.memory_dumps:
                command.extend(["--mem-dump", address, str(rows)])

            child_env = os.environ.copy()
            if platform.system() == "Darwin":
                child_env["HOME"] = str(root)
            try:
                process = await asyncio.create_subprocess_exec(
                    *command,
                    cwd=str(root),
                    env=child_env,
                    stdout=asyncio.subprocess.PIPE,
                    stderr=asyncio.subprocess.STDOUT,
                )
            except FileNotFoundError:
                yield _sse("error", {"detail": "Kites CLI was not found. Set KITES_CLI to its executable path."})
                return

            lines: list[str] = []
            trace: list[int] = []
            latest_registers: Any = None
            breakpoint: dict[str, int] | None = None
            trace_truncated = False
            deadline = time.monotonic() + SIMULATION_TIMEOUT
            timed_out = False
            while True:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    timed_out = True
                    process.kill()
                    break
                try:
                    raw_line = await asyncio.wait_for(process.stdout.readline(), timeout=remaining)
                except asyncio.TimeoutError:
                    timed_out = True
                    process.kill()
                    break
                if not raw_line:
                    break
                line = raw_line.decode("utf-8", errors="replace").rstrip("\r\n")
                if line == "[KITES_COMPILED]":
                    state = root / "vm_state"
                    if platform.system() == "Darwin":
                        state = root / "Library" / "Application Support" / "Kites" / "vm_state"
                    disassembly_path = state / "disassembly.txt"
                    disassembly = disassembly_path.read_text(encoding="utf-8") if disassembly_path.exists() else ""
                    yield _sse("compiled", {"disassembly": disassembly})
                elif line.startswith("[KITES_TRACE_PC]"):
                    try:
                        pc = int(line.removeprefix("[KITES_TRACE_PC]").strip(), 16)
                        trace.append(pc)
                        yield _sse("pc", {"pc": pc, "step": len(trace)})
                    except ValueError:
                        continue
                elif line.startswith("[KITES_REGS]"):
                    try:
                        latest_registers = json.loads(line.removeprefix("[KITES_REGS]").strip())
                        yield _sse("state", {"registers": latest_registers})
                    except json.JSONDecodeError:
                        continue
                elif line.startswith("[KITES_BREAKPOINT]"):
                    try:
                        breakpoint = {"pc": int(line.removeprefix("[KITES_BREAKPOINT]").strip(), 16)}
                        yield _sse("breakpoint", breakpoint)
                    except ValueError:
                        continue
                elif line.startswith("[KITES_TRACE_TRUNCATED]"):
                    trace_truncated = line.endswith("true")
                    yield _sse("trace_truncated", {"truncated": trace_truncated})
                elif line.startswith((
                    "Program Counter:",
                    "VM_STEP_COMPLETED",
                    "VM_LAST_INSTRUCTION_STEPPED",
                    "VM_PROGRAM_END",
                    "VM_EXIT",
                    "Processor clocked.",
                    "Setting line number to instruction type mapping",
                )):
                    continue
                else:
                    lines.append(line)

            if timed_out:
                await process.wait()
                yield _sse("error", {"detail": "Simulation exceeded its time limit."})
                return

            exit_code = await process.wait()
            state = root / "vm_state"
            if platform.system() == "Darwin":
                mac_state = root / "Library" / "Application Support" / "Kites" / "vm_state"
                if mac_state.exists():
                    state = mac_state
            disassembly_path = state / "disassembly.txt"
            final_payload = {
                "success": exit_code == 0,
                "exit_code": exit_code,
                "stdout": "\n".join(lines),
                "stderr": "",
                "trace": trace,
                "trace_truncated": trace_truncated,
                "breakpoint": breakpoint,
                "errors": _read_json(state / "errors_dump.json"),
                "registers": latest_registers if breakpoint is not None else (_read_json(state / "registers_dump.json") or latest_registers),
                "disassembly": disassembly_path.read_text(encoding="utf-8") if disassembly_path.exists() else None,
                "memory": _read_json(state / "memory_dump.json"),
            }
            yield _sse("done", final_payload)

    return StreamingResponse(events(), media_type="text/event-stream", headers={"Cache-Control": "no-cache", "X-Accel-Buffering": "no"})


app.mount("/", StaticFiles(directory=Path(__file__).resolve().parent.parent / "web", html=True), name="web")
