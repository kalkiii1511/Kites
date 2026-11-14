import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import { Play, Pause, SkipBack, SkipForward, RotateCcw, Search, ChevronDown, Upload, Download, Code2, Database, Cpu, MemoryStick, CircleHelp, Wifi, WifiOff, Square } from 'lucide-react';
type Tab = 'Editor' | 'Datapath' | 'Memory' | 'Cache';
type Reg = {
  name: string;
  abi: string;
  value: bigint;
};
type InstructionRow = {
  address: string;
  encoding: string;
  instruction: string;
  sourceLine: number;
};
type MachineState = {
  pc: string;
  gpr: Record<string, string>;
  fpr: Record<string, string>;
  csr: Record<string, string>;
  cycles: number;
  instructionsRetired: number;
  stallCycles: number;
  branchMispredictions: number;
  cpi: number;
  ipc: number;
  programLoaded: boolean;
};
const API = import.meta.env.VITE_KITES_API ?? 'http://127.0.0.1:8080';
const gAbi = ['zero', 'ra', 'sp', 'gp', 'tp', 't0', 't1', 't2', 's0', 's1', 'a0', 'a1', 'a2', 'a3', 'a4', 'a5', 'a6', 'a7', 's2', 's3', 's4', 's5', 's6', 's7', 's8', 's9', 's10', 's11', 't3', 't4', 't5', 't6'];
const fAbi = ['ft0', 'ft1', 'ft2', 'ft3', 'ft4', 'ft5', 'ft6', 'ft7', 'fs0', 'fs1', 'fa0', 'fa1', 'fa2', 'fa3', 'fa4', 'fa5', 'fa6', 'fa7', 'fs2', 'fs3', 'fs4', 'fs5', 'fs6', 'fs7', 'fs8', 'fs9', 'fs10', 'fs11', 'ft8', 'ft9', 'ft10', 'ft11'];
const initialCode = `# Kites web simulator — RV64IMAFD\n.text\n_start:\n    li   t0, 42\n    li   t1, 10\n    add  a0, t0, t1\n    li   t2, 0x10000000\n    sd   a0, 0(t2)\n    ld   a1, 0(t2)\n    fmv.d.x  ft0, a1\n    fadd.d   fa0, ft0, ft0\n`;
const zeroState = (): MachineState => ({ pc: '0x0000000000000000', gpr: {}, fpr: {}, csr: {}, cycles: 0, instructionsRetired: 0, stallCycles: 0, branchMispredictions: 0, cpi: 0, ipc: 0, programLoaded: false });
const hex = (n: bigint) => '0x' + n.toString(16).padStart(16, '0');
const parseAddress = (s: string) => {
  const v = s.trim(); if (!v)
    throw new Error('Enter an address'); return BigInt(v.toLowerCase().startsWith('0x') ? v : `0x${v}`);
};
const asBig = (s?: string) => BigInt(s ?? '0x0');

// --- generic api function ---
async function api<T>(path: string, init?: RequestInit): Promise<T> {
  const response = await fetch(`${API}${path}`, { ...init, headers: { 'Content-Type': 'application/json', ...(init?.headers ?? {}) } });
  const data = await response.json().catch(() => ({ error: `HTTP ${response.status}` }));
  if (!response.ok)
    throw new Error(data.error ?? `HTTP ${response.status}`);
  return data as T;
}
function App() {
  const [tab, setTab] = useState<Tab>('Editor');
  const [code, setCode] = useState(initialCode);
  const [regTab, setRegTab] = useState<'Integer' | 'Floating point'>('Integer');
  const [format, setFormat] = useState<'Hex' | 'Decimal' | 'Float' | 'Double'>('Hex');
  const [machine, setMachine] = useState<MachineState>(zeroState());
  const [address, setAddress] = useState('0x10000000');
  const [base, setBase] = useState(0x10000000n);
  const [memFormat, setMemFormat] = useState('Hex');
  const [segment, setSegment] = useState('Data Segment');
  const [memory, setMemory] = useState<Record<string, number>>({});
  const [loadedBytes, setLoadedBytes] = useState(0);
  const [loadingMemory, setLoadingMemory] = useState(false);
  const [memoryQuery, setMemoryQuery] = useState('');
  const [message, setMessage] = useState('Connecting to Kites…');
  const [connected, setConnected] = useState(false);
  const [busy, setBusy] = useState(false);
  const [vm, setVm] = useState('rvss');
  const [instructions, setInstructions] = useState<InstructionRow[]>([]);
  const [currentSourceLine, setCurrentSourceLine] = useState(0);
  const [currentAddress, setCurrentAddress] = useState('');
  const [speed, setSpeed] = useState(5);
  const [running, setRunning] = useState(false);
  const runActive = useRef(false);
  const speedRef = useRef(5);
  const editorRef = useRef<HTMLTextAreaElement>(null);
  const lineGutterRef = useRef<HTMLDivElement>(null);
  const scrollRef = useRef<HTMLDivElement>(null);
  const loadedEnd = useRef<bigint>(0n);
  const loadLock = useRef(false);
  const refreshState = useCallback(async () => { const s = await api<MachineState>('/api/state'); setMachine(s); }, []);
  const refreshHealth = useCallback(async () => {
    try {
      await api('/api/health');
      setConnected(true);
      setMessage('Connected to Kites');
      await refreshState();
    }
    catch (e) {
      setConnected(false);
      setMessage((e as Error).message);
    }
  }, [refreshState]);
  useEffect(() => { void refreshHealth(); const timer = window.setInterval(() => void refreshHealth(), 3000); return () => window.clearInterval(timer); }, [refreshHealth]);
  const visibleMemoryBytes = () => Math.max(256, Math.ceil((window.innerHeight - 200) / 25) * 16);

  //--- loads memory ---
  const loadMemory = useCallback(async (start: bigint, length = 256, append = false) => {
    if (loadLock.current)
      return;
    loadLock.current = true;
    setLoadingMemory(true);
    try {
      const q = new URLSearchParams({ address: hex(start), length: String(length) });
      const r = await api<{
        bytes: number[];
      }>(`/api/memory?${q}`);

      setMemory(old => { const next = append ? { ...old } : {}; r.bytes.forEach((v, i) => { next[(start + BigInt(i)).toString()] = v; }); return next; });
      if (!append) {
        setBase(start);
        setLoadedBytes(r.bytes.length);
        loadedEnd.current = start + BigInt(r.bytes.length);
      }
      else {
        setLoadedBytes(v => v + r.bytes.length);
        loadedEnd.current = start + BigInt(r.bytes.length);
      }
    }
    catch (e) {
      setMessage((e as Error).message);
    }
    finally {
      loadLock.current = false;
      setLoadingMemory(false);
    }
  }, []);
  useEffect(() => {
    if (tab === 'Memory' && loadedBytes === 0)
      void loadMemory(base, visibleMemoryBytes(), false);
  }, [tab, base, loadedBytes, loadMemory]);
  const regs = useMemo<Reg[]>(() => gAbi.map((abi, i) => ({ name: `x${i}`, abi, value: asBig(machine.gpr[`x${i}`]) })), [machine.gpr]);
  const fregs = useMemo<Reg[]>(() => fAbi.map((abi, i) => ({ name: `f${i}`, abi, value: asBig(machine.fpr[`f${i}`]) })), [machine.fpr]);
  const showValue = (r: Reg) => {
    if (format === 'Decimal')
      return r.value.toString(); if (format === 'Float') {
        const b = new ArrayBuffer(4);
        const d = new DataView(b);
        d.setUint32(0, Number(r.value & 0xffffffffn), true);
        return String(d.getFloat32(0, true));
      } if (format === 'Double') {
        const b = new ArrayBuffer(8);
        const d = new DataView(b);
        d.setBigUint64(0, r.value, true);
        return String(d.getFloat64(0, true));
      } return hex(r.value);
  };
  const activateInstruction = (row: InstructionRow) => {
    setCurrentSourceLine(row.sourceLine); setCurrentAddress(row.address); const lineTop = Math.max(0, (row.sourceLine - 1) * 21 - 80); if (editorRef.current)
      editorRef.current.scrollTop = lineTop; if (lineGutterRef.current)
      lineGutterRef.current.scrollTop = lineTop;
  };

  // assemble function 
  const assemble = async () => {
    setBusy(true); try {
      const s = await api<MachineState & {
        message: string;
        instructions: InstructionRow[];
      }>('/api/program/assemble', { method: 'POST', body: JSON.stringify({ source: code, vm }) });
      setMachine(s);
      setInstructions(s.instructions);
      setCurrentSourceLine(0);
      setCurrentAddress('');
      setMessage(s.message);
      setTab('Editor');
    }
      catch (e) {
        setMessage(`Assembly error: ${(e as Error).message}`);
      }

      finally {
      setBusy(false);
    }
  };
  const step = async (count = 1) => {
    setBusy(true); try {
      const active = instructions.find(row => BigInt(row.address) === BigInt(machine.pc));
      const s = await api<MachineState & {
        message: string;
      }>('/api/simulator/step', { method: 'POST', body: JSON.stringify({ count }) });
      setMachine(s);
      if (active)
        activateInstruction(active);
      setMessage(s.message);
    }
      catch (e) {
        setMessage((e as Error).message);
      }
      finally {
      setBusy(false);
    }
  };
  const run = async () => {
    if (runActive.current) {
      runActive.current = false;
      setRunning(false);
      setMessage('Execution stopped');
      return;
    } setBusy(true); runActive.current = true; setRunning(true); try {
      const assembled = await api<MachineState & {
        message: string;
        instructions: InstructionRow[];
      }>('/api/program/assemble', { method: 'POST', body: JSON.stringify({ source: code, vm }) });
      setMachine(assembled);
      setInstructions(assembled.instructions);
      setCurrentSourceLine(0);
      setCurrentAddress('');
      setMessage('Running…');
      setBusy(false);
      if (!assembled.instructions.length)
        throw new Error('No instructions to execute');
      const lastAddress = BigInt(assembled.instructions[assembled.instructions.length - 1].address);
      let state: MachineState = assembled;
      while (runActive.current) {
        const active = assembled.instructions.find(row => BigInt(row.address) === BigInt(state.pc));
        if (!active)
          break;
        activateInstruction(active);
        state = await api<MachineState & {
          message: string;
        }>('/api/simulator/step', { method: 'POST', body: JSON.stringify({ count: 1 }) });
        setMachine(state);
        if (BigInt(state.pc) > lastAddress && state.instructionsRetired >= assembled.instructions.length) {
          setMessage('Execution complete');
          break;
        }
        await new Promise(resolve => window.setTimeout(resolve, Math.max(0, Math.round(1000 / speedRef.current))));
      }

      if (!runActive.current)
        setMessage('Execution stopped');
      else if (state.instructionsRetired >= assembled.instructions.length)
        setMessage('Execution complete');
    }
    catch (e) {
      setMessage(`Run error: ${(e as Error).message}`);
    }
    finally {
      runActive.current = false;
      setRunning(false);
      setBusy(false);
    }
  };
  const reset = async () => {
    setBusy(true); try {
      const s = await api<MachineState & {
        message: string;
      }>('/api/simulator/reset', { method: 'POST', body: '{}' });
      setMachine(s);
      setCurrentSourceLine(0);
      setCurrentAddress('');
      setMessage(s.message);
    }
      catch (e) {
        setMessage((e as Error).message);
      }
      finally {
      setBusy(false);
    }
  };
  const jump = async () => {
    try {
      const a = parseAddress(address);
      setMemory({});
      setLoadedBytes(0);
      loadedEnd.current = a;
      setBase(a);
      if (scrollRef.current)
        scrollRef.current.scrollTop = 0;
      await loadMemory(a, visibleMemoryBytes(), false);
      setMessage(`Memory at ${hex(a)}`);
    }
    catch (e) {
      setMessage((e as Error).message);
    }
  };
  const searchMemory = async () => {
    try {
      const raw = memoryQuery.trim();
      if (!raw)
        throw new Error('Enter a byte value to search for');
      const value = Number.parseInt(raw.replace(/^0x/i, ''), raw.toLowerCase().startsWith('0x') || memFormat === 'Hex' ? 16 : 10);
      if (!Number.isInteger(value) || value < 0 || value > 255)
        throw new Error('Search value must be a byte (0–255)');
      const q = new URLSearchParams({ address: hex(base), length: '4096' });
      const result = await api<{
        bytes: number[];
      }>(`/api/memory?${q}`);
      setMemory(Object.fromEntries(result.bytes.map((byte, i) => [(base + BigInt(i)).toString(), byte])));
      setLoadedBytes(result.bytes.length);
      loadedEnd.current = base + BigInt(result.bytes.length);
      const offset = result.bytes.indexOf(value);
      if (offset < 0) {
        setMessage(`No matching byte found in the next 4 KB`);
        return;
      }
      const found = base + BigInt(offset);
      setAddress(hex(found));
      setMessage(`Found ${hex(found)} (${memFormat === 'Hex' ? '0x' + value.toString(16).padStart(2, '0') : value})`);
      if (scrollRef.current)
        scrollRef.current.scrollTop = Math.floor(offset / 16) * 26;
    }
    catch (e) {
      setMessage((e as Error).message);
    }
  };
  const onMemoryScroll = (e: React.UIEvent<HTMLDivElement>) => {
    const el = e.currentTarget; if (el.scrollTop + el.clientHeight >= el.scrollHeight - 100 && !loadingMemory && loadedBytes > 0)
      void loadMemory(loadedEnd.current, 256, true);
  };
  const writeMemory = async (addr: bigint, value: string) => {
    const n = memFormat === 'Hex' ? Number.parseInt(value.replace(/^0x/i, ''), 16) : Number(value); if (!Number.isInteger(n) || n < 0 || n > 255)
      return; try {
        await api('/api/memory/write', { method: 'POST', body: JSON.stringify({ address: hex(addr), value: n }) });
        setMemory(m => ({ ...m, [addr.toString()]: n }));
      }
    catch (e) {
      setMessage((e as Error).message);
    }
  };
  const exportCode = () => { const blob = new Blob([code], { type: 'text/plain' }); const a = document.createElement('a'); a.href = URL.createObjectURL(blob); a.download = 'program.s'; a.click(); URL.revokeObjectURL(a.href); };
  const importCode = (file?: File) => {
    if (!file)
      return; const reader = new FileReader(); reader.onload = () => setCode(String(reader.result ?? '')); reader.readAsText(file);
  };
  const rows = Array.from({ length: Math.ceil(loadedBytes / 16) }, (_, i) => i);
  return <div className="app">
    <header className="topbar"><div className="brand"><span className="brand-mark">⌁</span><b>RISC-V Simulator</b><span className="demo-tag">KITES</span></div><nav>{(['Editor', 'Datapath', 'Memory', 'Cache'] as Tab[]).map((x, i) => <button className={tab === x ? 'nav active' : 'nav'} onClick={() => setTab(x)} key={x}>{[<Code2 />, <Cpu />, <Database />, <MemoryStick />][i]}{x}</button>)}</nav><div className="controls"><button title={running ? "Stop execution" : "Run step by step"} disabled={(busy && !running) || !connected} onClick={() => void run()}>{running ? <Square /> : <Play />}</button><button title="Pause is not yet exposed by Kites HTTP API" disabled><Pause /></button><button title="Reset" disabled={busy || running} onClick={() => void reset()}><RotateCcw /></button><button title="Step" disabled={busy || running || !machine.programLoaded} onClick={() => void step()}><SkipForward /></button><select className="vm-select" disabled={busy || running} value={vm} onChange={e => setVm(e.target.value)}><option value="rvss">RVSS</option><option value="rv5-nh-nf">RV5 NH/NF</option><option value="rv5-h-nf">RV5 H/NF</option><option value="rv5-nh-f">RV5 NH/F</option><option value="rv5-h-f">RV5 H/F</option></select></div></header>
    <main className="workspace"><section className="main-panel">
      {tab === 'Editor' && <><div className="panel-head"><div className="panel-title">Code Editor</div><div className="actions"><label className="small-btn">Import<input type="file" accept=".s,.asm,.txt" hidden onChange={e => importCode(e.target.files?.[0])} /></label><button className="small-btn" onClick={exportCode}><Download size={12} /> Export</button><button className="small-btn" onClick={() => setCode(initialCode)}>Example <ChevronDown size={12} /></button><button className="primary" disabled={busy || running || !connected} onClick={() => void assemble()}>Assemble</button></div></div><div className="editor-wrap"><div className="line-nos" ref={lineGutterRef}>{code.split('\n').map((_, i) => <div className={currentSourceLine === i + 1 ? 'active' : ''} key={i}>{i + 1}</div>)}</div><textarea ref={editorRef} spellCheck={false} value={code} onChange={e => setCode(e.target.value)} aria-label="Assembly editor" style={currentSourceLine ? { backgroundImage: `linear-gradient(to bottom, transparent ${8 + (currentSourceLine - 1) * 21}px, rgba(59,130,246,.15) ${8 + (currentSourceLine - 1) * 21}px, rgba(59,130,246,.15) ${29 + (currentSourceLine - 1) * 21}px, transparent ${29 + (currentSourceLine - 1) * 21}px)`, backgroundAttachment: 'local' } : undefined} /></div><div className="result"><div className="result-head"><b>Execution flow</b><span>{message}</span><label className="speed-control">Speed <input type="range" min="1" max="20" value={speed} onChange={e => { const value = Number(e.target.value); setSpeed(value); speedRef.current = value; }} /><b>{speed} steps/s</b></label></div><div className="stats-strip"><span>PC <b>{machine.pc}</b></span><span>Cycles <b>{machine.cycles}</b></span><span>Retired <b>{machine.instructionsRetired}</b></span><span>CPI <b>{Number(machine.cpi).toFixed(2)}</b></span></div><div className="instruction-scroll"><table className="instruction-table"><thead><tr><th>ADDRESS</th><th>ENCODING</th><th>INSTRUCTION</th><th>LINE</th></tr></thead><tbody>{instructions.map(row => <tr key={row.address} className={row.address === currentAddress ? 'active' : ''}><td>{row.address}</td><td>{row.encoding}</td><td>{row.instruction}</td><td>{row.sourceLine}</td></tr>)}</tbody></table>{instructions.length === 0 && <div className="instruction-empty">Assemble a program to view its instruction flow.</div>}</div></div></>}
      {tab === 'Memory' && <><div className="memory-toolbar"><div className="segments">{['Data Segment', 'Text Segment', 'GP', 'SP'].map(s => <button className={segment === s ? 'selected' : ''} onClick={() => setSegment(s)} key={s}>{s}</button>)}</div><div className="memory-search"><Search size={14} /><input value={address} onChange={e => setAddress(e.target.value)} onKeyDown={e => e.key === 'Enter' && void jump()} placeholder="Address" /><button onClick={() => void jump()}>Go</button></div><div className="memory-search value-search"><Search size={14} /><input value={memoryQuery} onChange={e => setMemoryQuery(e.target.value)} onKeyDown={e => e.key === 'Enter' && void searchMemory()} placeholder={memFormat === 'Hex' ? 'Byte (e.g. 2a)' : 'Byte (0–255)'} /><button onClick={() => void searchMemory()}>Find</button></div><select value={memFormat} onChange={e => setMemFormat(e.target.value)}><option>Hex</option><option>Decimal</option></select><button className="icon-btn" title="Refresh" onClick={() => void loadMemory(base, Math.max(visibleMemoryBytes(), loadedBytes), false)}><RotateCcw size={14} /></button></div><div className="memory-scroll" ref={scrollRef} onScroll={onMemoryScroll}><table className="memory-table"><thead><tr><th>ADDRESS</th>{Array.from({ length: 16 }, (_, i) => <th key={i}>+{i.toString(16).padStart(2, '0').toUpperCase()}</th>)}</tr></thead><tbody>{rows.map(row => { const a = base + BigInt(row * 16); return <tr key={a.toString()}><th>{hex(a)}</th>{Array.from({ length: 16 }, (_, i) => { const addr = a + BigInt(i); const val = memory[addr.toString()] ?? 0; return <td key={i}><input key={`${addr.toString()}-${memFormat}`} aria-label={`Memory ${hex(addr)}`} defaultValue={memFormat === 'Decimal' ? String(val) : val.toString(16).padStart(2, '0')} onBlur={e => void writeMemory(addr, e.target.value)} /></td>; })}</tr>; })}</tbody></table>{loadingMemory && <div className="load-note">Loading memory…</div>}{!loadingMemory && <div className="load-note">Scroll down to load the next memory page</div>}</div></>}
      {tab === 'Datapath' && <div className="placeholder"><Cpu size={28} /><b>Datapath</b><span>Live pipeline visualization requires a per-cycle processor-state endpoint. Kites execution and register state are connected.</span></div>}
      {tab === 'Cache' && <div className="placeholder"><MemoryStick size={28} /><b>Cache</b><span>Cache visualization is not yet exposed by the Kites web API.</span></div>}
    </section><aside className="register-panel"><div className="reg-head"><b>Register</b><select value={format} onChange={e => setFormat(e.target.value)}><option value="Hex">Hexadecimal</option><option value="Decimal">Decimal</option><option value="Float">Float</option><option value="Double">Double</option></select></div><div className="reg-tabs"><button className={regTab === 'Integer' ? 'selected' : ''} onClick={() => setRegTab('Integer')}>Integer</button><button className={regTab === 'Floating point' ? 'selected' : ''} onClick={() => setRegTab('Floating point')}>Floating point</button></div><div className="reg-columns"><span>Name</span><span>Number</span><span>Value</span></div><div className="reg-list">{(regTab === 'Integer' ? regs : fregs).map((r, i) => <div className="reg-row" key={r.name}><b>{r.abi}</b><span>{i}</span><code>{showValue(r)}</code></div>)}</div><div className="pc-line"><span>PC</span><code>{machine.pc}</code></div><div className="pc-line"><span>FPR status</span><code>{machine.csr.fcsr ?? '—'}</code></div></aside></main>
    <footer><span className={connected ? 'status-dot' : 'status-dot offline'} />{message}<span className="footer-right">{connected ? <><Wifi size={12} /> Kites API connected</> : <><WifiOff size={12} /> API disconnected</>} · RV64IMAFD</span></footer>
  </div>;
}
export default App;