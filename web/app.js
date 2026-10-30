const SAMPLE = `# Floating-point registers are visible in the register file →
.text
main:
    li      t0, 7
    li      t1, 6
    add     t2, t0, t1      # integer result: 13

    li      t3, 0x40200000  # IEEE-754 2.5f
    fmv.w.x ft0, t3
    li      t4, 0x3fc00000  # IEEE-754 1.5f
    fmv.w.x ft1, t4
    fadd.s  ft2, ft0, ft1   # floating result: 4.0f

    li      a0, 0
    li      a7, 10
    ecall`;

const ABI_NAMES = ["zero", "ra", "sp", "gp", "tp", "t0", "t1", "t2", "s0 / fp", "s1", "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7", "s2", "s3", "s4", "s5", "s6", "s7", "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6"];
const BANKS = {
  gp_registers: { label: "Integer registers · ABI names", prefix: "x", count: 32, names: ABI_NAMES },
  fp_registers: { label: "Floating-point registers · ABI names", prefix: "f", count: 32, names: ["ft0", "ft1", "ft2", "ft3", "ft4", "ft5", "ft6", "ft7", "fs0", "fs1", "fa0", "fa1", "fa2", "fa3", "fa4", "fa5", "fa6", "fa7", "fs2", "fs3", "fs4", "fs5", "fs6", "fs7", "fs8", "fs9", "fs10", "fs11", "ft8", "ft9", "ft10", "ft11"] },
  "control and status registers": { label: "Control and status registers", prefix: "", count: 0, names: [] },
};
const initialApi = localStorage.getItem("kites-api-url") || window.location.origin;
const apiInput = document.querySelector("#api-url");
const apiStatus = document.querySelector("#api-status");
const sourceEditor = document.querySelector("#source-editor");
const registerList = document.querySelector("#register-list");
const resultContent = document.querySelector("#result-content");
let activeBank = "gp_registers";
let activeResult = "console";
let lastResponse = null;
let previousRegisters = {};
let currentRegisterState = null;
const breakpointAddresses = new Set();

apiInput.value = initialApi;
sourceEditor.value = SAMPLE;

function baseUrl() { return apiInput.value.trim().replace(/\/+$/, ""); }

async function checkConnection() {
  apiStatus.className = "connection";
  apiStatus.lastElementChild.textContent = "Checking";
  try {
    const response = await fetch(`${baseUrl()}/api/health`, { signal: AbortSignal.timeout(3500) });
    if (!response.ok) throw new Error("API unavailable");
    apiStatus.className = "connection online";
    apiStatus.lastElementChild.textContent = "Connected";
  } catch {
    apiStatus.className = "connection offline";
    apiStatus.lastElementChild.textContent = "Offline";
  }
}

function updateEditorMeta() {
  const lines = sourceEditor.value.split("\n");
  document.querySelector("#line-numbers").textContent = lines.map((_, i) => i + 1).join("\n");
  document.querySelector("#line-count").textContent = `${lines.length} ${lines.length === 1 ? "line" : "lines"}`;
  const before = sourceEditor.value.slice(0, sourceEditor.selectionStart);
  const line = before.split("\n").length;
  const col = before.length - before.lastIndexOf("\n");
  document.querySelector("#cursor-position").innerHTML = `Ln ${line}, Col ${col} <b>·</b> UTF-8`;
}

function setBank(bank, values = null) {
  activeBank = bank;
  document.querySelectorAll(".register-tab").forEach(tab => {
    const selected = tab.dataset.bank === bank;
    tab.classList.toggle("active", selected);
    tab.setAttribute("aria-selected", String(selected));
  });
  const config = BANKS[bank];
  document.querySelector("#register-caption").textContent = config.label;
  const data = values || (bank === "gp_registers" || bank === "fp_registers" ? Object.fromEntries(Array.from({ length: config.count }, (_, i) => [`${config.prefix}${i}`, "0x0000000000000000"])) : {});
  document.querySelector("#register-count").textContent = bank === "control and status registers" ? `${Object.keys(data).length || 7} CSRs` : "32 × 64-bit";
  const entries = Object.entries(data);
  if (entries.length === 0) {
    registerList.innerHTML = `<div class="empty-registers">Run a program to load register values.</div>`;
    return;
  }
  registerList.innerHTML = entries.map(([name, value], index) => {
    const changed = previousRegisters[`${bank}:${name}`] !== undefined && previousRegisters[`${bank}:${name}`] !== value;
    const n = Number(name.slice(1));
    const abi = bank === "control and status registers" ? "" : config.names[n] || "";
    return `<div class="register-row${changed ? " changed" : ""}"><span class="register-name"><span class="register-index">${name}</span><span class="register-abi">${abi}</span></span><span class="register-value">${value}</span></div>`;
  }).join("");
  for (const [name, value] of entries) previousRegisters[`${bank}:${name}`] = value;
}

function setResult(tab) {
  activeResult = tab;
  document.querySelectorAll(".result-tab").forEach(button => button.classList.toggle("active", button.dataset.result === tab));
  if (!lastResponse) return;
  if (tab === "disassembly") resultContent.textContent = lastResponse.disassembly || "No disassembly was produced.";
  else if (tab === "memory") resultContent.textContent = lastResponse.memory ? JSON.stringify(lastResponse.memory, null, 2) : "No memory range requested. Add a memory dump in the API request to inspect memory.";
  else resultContent.textContent = lastResponse.stdout || lastResponse.stderr || (lastResponse.success ? "Program completed without console output." : "Program did not complete.");
}

function escapeHtml(text) {
  return String(text).replace(/[&<>"']/g, char => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" })[char]);
}

function renderExecutionFlow(disassembly, traceTruncated = false) {
  const flowList = document.querySelector("#flow-list");
  const instructions = (disassembly || "").split("\n").flatMap(line => {
    const match = line.match(/^\s*([\da-f]{1,16}):\s+([\da-f]{8}|\?{8})\s+(.*)$/i);
    return match ? [{ pc: Number.parseInt(match[1], 16), machine: match[2].toUpperCase(), assembly: match[3].trim() }] : [];
  });
  if (!instructions.length) {
    flowList.innerHTML = `<div class="flow-empty"><span class="flow-empty-glyph">{ }</span><strong>No compiled instructions</strong><span>Check the assembly diagnostics in the console.</span></div>`;
    document.querySelector("#flow-step").textContent = "Assembly did not produce a program";
    document.querySelector("#flow-count").textContent = "0 instructions";
    document.querySelector("#flow-status").className = "flow-state";
    document.querySelector("#flow-status").innerHTML = "<i></i> NO PROGRAM";
    document.querySelector("#flow-pc").textContent = "PC —";
    return;
  }

  flowList.innerHTML = instructions.map((item, index) => `<div class="flow-row${breakpointAddresses.has(item.pc) ? " has-breakpoint" : ""}" data-pc="${item.pc}"><button class="breakpoint-toggle" data-pc="${item.pc}" type="button" aria-label="Toggle breakpoint at address 0x${item.pc.toString(16)}" aria-pressed="${breakpointAddresses.has(item.pc)}" title="Toggle breakpoint">${breakpointAddresses.has(item.pc) ? "●" : "○"}</button><span class="flow-index">${String(index + 1).padStart(2, "0")}</span><span class="flow-address">0x${item.pc.toString(16).padStart(4, "0")}</span><span class="flow-machine">${escapeHtml(item.machine)}</span><span class="flow-assembly">${escapeHtml(item.assembly)}</span></div>`).join("");
  flowList.querySelectorAll(".breakpoint-toggle").forEach(button => button.addEventListener("click", () => toggleBreakpoint(Number(button.dataset.pc))));
  document.querySelector("#flow-count").textContent = `${instructions.length} instructions${traceTruncated ? " · trace capped" : ""}`;
  document.querySelector("#flow-status").className = "flow-state";
  document.querySelector("#flow-status").innerHTML = "<i></i> COMPILED";
  document.querySelector("#flow-step").textContent = "Compiled binary · waiting for first PC";
  document.querySelector("#flow-pc").textContent = "PC —";
}

function highlightLivePc(pc, step) {
  const flowList = document.querySelector("#flow-list");
  const row = flowList.querySelector(`.flow-row[data-pc="${Number(pc)}"]`);
  if (!row) return;
  flowList.querySelectorAll(".flow-current").forEach(active => {
    active.classList.remove("flow-current", "flow-finished");
    active.classList.add("flow-visited");
  });
  row.classList.remove("flow-visited");
  row.classList.add("flow-current");
  row.scrollIntoView({ block: "nearest", behavior: "smooth" });
  document.querySelector("#flow-status").className = "flow-state running";
  document.querySelector("#flow-status").innerHTML = "<i></i> RUNNING";
  document.querySelector("#flow-step").textContent = `Step ${step} · ${row.querySelector(".flow-assembly").textContent}`;
  document.querySelector("#flow-pc").textContent = `PC 0x${Number(pc).toString(16).padStart(4, "0")}`;
}

function toggleBreakpoint(pc) {
  if (breakpointAddresses.has(pc)) breakpointAddresses.delete(pc);
  else breakpointAddresses.add(pc);
  const row = document.querySelector(`.flow-row[data-pc="${pc}"]`);
  const button = row?.querySelector(".breakpoint-toggle");
  if (!row || !button) return;
  const enabled = breakpointAddresses.has(pc);
  row.classList.toggle("has-breakpoint", enabled);
  button.textContent = enabled ? "●" : "○";
  button.setAttribute("aria-pressed", String(enabled));
}

function updatePaceLabel() {
  const delay = Number(document.querySelector("#pace-slider").value);
  document.querySelector("#pace-value").textContent = delay === 0 ? "instant" : `${delay} ms`;
}

function clearBreakpointAddresses() {
  breakpointAddresses.clear();
  document.querySelectorAll(".breakpoint-toggle").forEach(button => {
    button.textContent = "○";
    button.setAttribute("aria-pressed", "false");
    button.closest(".flow-row")?.classList.remove("has-breakpoint");
  });
}

async function readSimulationStream(response) {
  if (!response.ok) {
    const error = await response.json();
    throw new Error(error.detail || `API error ${response.status}`);
  }
  if (!response.body) throw new Error("Streaming responses are not supported by this browser.");
  const reader = response.body.getReader();
  const decoder = new TextDecoder();
  let buffer = "";
  let finalResult = null;
  let breakpointFound = false;
  while (true) {
    const { value, done } = await reader.read();
    buffer += decoder.decode(value || new Uint8Array(), { stream: !done });
    const frames = buffer.split("\n\n");
    buffer = frames.pop() || "";
    for (const frame of frames) {
      const eventName = frame.match(/^event:\s*(.+)$/m)?.[1]?.trim();
      const dataLine = frame.split("\n").find(line => line.startsWith("data:"));
      if (!eventName || !dataLine) continue;
      const payload = JSON.parse(dataLine.slice(5).trim());
      if (eventName === "compiled") renderExecutionFlow(payload.disassembly);
      else if (eventName === "pc" && !breakpointFound) highlightLivePc(payload.pc, payload.step);
      else if (eventName === "state") {
        currentRegisterState = payload.registers;
        setBank(activeBank, currentRegisterState?.[activeBank]);
      }
      else if (eventName === "breakpoint") {
        breakpointFound = true;
        highlightLivePc(payload.pc, payload.step || 0);
        document.querySelector(`.flow-row[data-pc="${Number(payload.pc)}"]`)?.classList.add("flow-breakpoint");
        document.querySelector("#flow-status").className = "flow-state paused";
        document.querySelector("#flow-status").innerHTML = "<i></i> BREAKPOINT";
        document.querySelector("#flow-step").textContent = `Paused before PC 0x${Number(payload.pc).toString(16).padStart(4, "0")}`;
      }
      else if (eventName === "trace_truncated") document.querySelector("#flow-count").textContent += " · trace capped";
      else if (eventName === "error") throw new Error(payload.detail || "Simulator error");
      else if (eventName === "done") finalResult = payload;
    }
    if (done) break;
  }
  if (!finalResult) throw new Error("The simulator closed before returning its final state.");
  return finalResult;
}

async function runProgram() {
  const button = document.querySelector("#run-btn");
  const message = document.querySelector("#run-message");
  button.disabled = true;
  button.querySelector("span:nth-child(2)").textContent = "Running…";
  message.textContent = "Assembling and simulating";
  document.querySelector("#flow-status").className = "flow-state running";
  document.querySelector("#flow-status").innerHTML = "<i></i> SIMULATING";
  document.querySelector("#flow-step").textContent = "Compiling assembly and starting execution";
  document.querySelector("#flow-count").textContent = "—";
  document.querySelector("#flow-list").innerHTML = `<div class="flow-empty"><span class="flow-empty-glyph">…</span><strong>Waiting for compiled instructions</strong><span>The current PC will be highlighted as the program runs.</span></div>`;
  document.querySelector("#console-status").className = "console-badge";
  document.querySelector("#console-status").textContent = "RUNNING";
  try {
    const response = await fetch(`${baseUrl()}/api/simulate/stream`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ source: sourceEditor.value, vm: document.querySelector("#vm-select").value, step_delay_ms: Number(document.querySelector("#pace-slider").value), breakpoints: [...breakpointAddresses], memory_dumps: [] }),
    });
    const result = await readSimulationStream(response);
    lastResponse = result;
    currentRegisterState = result.registers;
    if (!document.querySelector("#flow-list .flow-row") && result.disassembly) renderExecutionFlow(result.disassembly, result.trace_truncated);
    if (result.registers) setBank(activeBank, result.registers[activeBank]);
    const success = result.success;
    const currentFlowRow = document.querySelector("#flow-list .flow-current");
    const breakpointHit = Boolean(result.breakpoint);
    if (currentFlowRow && !breakpointHit) currentFlowRow.classList.add("flow-finished");
    document.querySelector("#flow-status").className = `flow-state ${breakpointHit ? "paused" : (success ? "complete" : "")}`;
    document.querySelector("#flow-status").innerHTML = `<i></i> ${breakpointHit ? "BREAKPOINT" : (success ? "COMPLETE" : "STOPPED")}`;
    if (breakpointHit) {
      document.querySelector("#console-status").className = "console-badge paused";
      document.querySelector("#console-status").textContent = "BREAKPOINT";
      message.textContent = `Paused at breakpoint 0x${result.breakpoint.pc.toString(16)}`;
      document.querySelector("#flow-step").textContent = `Paused before PC 0x${result.breakpoint.pc.toString(16).padStart(4, "0")}`;
    } else {
      document.querySelector("#console-status").className = `console-badge ${success ? "success" : "error"}`;
      document.querySelector("#console-status").textContent = success ? "COMPLETE" : "ERROR";
      message.textContent = success ? "Simulation complete" : "Simulation finished with errors";
      if (result.trace?.length) document.querySelector("#flow-step").textContent = `Run complete · ${result.trace.length} executed steps${result.trace_truncated ? " · trace capped" : ""}`;
    }
    if (!success && result.errors?.errors?.length) {
      const errors = result.errors.errors.map(error => `Line ${error.line}: ${error.message}`).join("\n");
      result.stdout = [result.stdout, errors].filter(Boolean).join("\n");
    }
    setResult(activeResult);
  } catch (error) {
    message.textContent = error.message;
    document.querySelector("#console-status").className = "console-badge error";
    document.querySelector("#console-status").textContent = "OFFLINE";
    document.querySelector("#flow-status").className = "flow-state";
    document.querySelector("#flow-status").innerHTML = "<i></i> ERROR";
    document.querySelector("#flow-step").textContent = "Could not reach the simulator";
    resultContent.textContent = `Could not run the simulation.\n\n${error.message}\n\nCheck the API URL above and make sure the Kites API is running.`;
  } finally {
    button.disabled = false;
    button.querySelector("span:nth-child(2)").textContent = "Run program";
  }
}

function selectView(view) {
  const isProcessor = view === "processor";
  document.body.classList.toggle("processor-mode", isProcessor);
  document.querySelectorAll(".view-tab").forEach(tab => {
    const active = tab.dataset.view === view;
    tab.classList.toggle("active", active);
    tab.setAttribute("aria-selected", String(active));
  });
  document.querySelector("#flow-title").textContent = isProcessor ? "Instruction flow" : "Program viewer";
  document.querySelector("#flow-caption").textContent = isProcessor ? "LIVE PROGRAM COUNTER · EXECUTION TRACE" : "ASSEMBLED INSTRUCTIONS · HEX";
}

document.querySelectorAll(".register-tab").forEach(tab => tab.addEventListener("click", () => setBank(tab.dataset.bank, currentRegisterState?.[tab.dataset.bank] || lastResponse?.registers?.[tab.dataset.bank])));
document.querySelectorAll(".result-tab").forEach(tab => tab.addEventListener("click", () => setResult(tab.dataset.result)));
document.querySelectorAll(".view-tab").forEach(tab => tab.addEventListener("click", () => selectView(tab.dataset.view)));
document.querySelector("#run-btn").addEventListener("click", runProgram);
document.querySelector("#connect-btn").addEventListener("click", () => { localStorage.setItem("kites-api-url", baseUrl()); checkConnection(); });
document.querySelector("#example-btn").addEventListener("click", () => { sourceEditor.value = SAMPLE; clearBreakpointAddresses(); updateEditorMeta(); sourceEditor.focus(); });
document.querySelector("#clear-btn").addEventListener("click", () => { sourceEditor.value = ""; clearBreakpointAddresses(); updateEditorMeta(); sourceEditor.focus(); });
sourceEditor.addEventListener("input", updateEditorMeta);
sourceEditor.addEventListener("input", clearBreakpointAddresses);
sourceEditor.addEventListener("click", updateEditorMeta);
sourceEditor.addEventListener("keyup", updateEditorMeta);
sourceEditor.addEventListener("scroll", () => { document.querySelector("#line-numbers").scrollTop = sourceEditor.scrollTop; });
sourceEditor.addEventListener("keydown", event => {
  if (event.key === "Tab") {
    event.preventDefault();
    const start = sourceEditor.selectionStart;
    const end = sourceEditor.selectionEnd;
    sourceEditor.setRangeText("    ", start, end, "end");
    updateEditorMeta();
  }
  if ((event.metaKey || event.ctrlKey) && event.key === "Enter") { event.preventDefault(); runProgram(); }
});
apiInput.addEventListener("keydown", event => { if (event.key === "Enter") { localStorage.setItem("kites-api-url", baseUrl()); checkConnection(); } });

updateEditorMeta();
updatePaceLabel();
document.querySelector("#pace-slider").addEventListener("input", updatePaceLabel);
setBank("gp_registers");
checkConnection();
