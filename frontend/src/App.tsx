import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import type { CSSProperties, UIEvent } from 'react';
import {
  AlertTriangle, ChevronDown, Code2, Cpu, Database, Download, Eye, EyeOff, MemoryStick, Play,
  RotateCcw, Search, SkipForward, Square, Upload, Wifi, WifiOff, X,
} from 'lucide-react';
import { examples } from './examples';
import type { Example } from './examples';

type Tab = 'Editor' | 'Datapath' | 'Memory' | 'Cache';
type StageName = 'IF' | 'ID' | 'EX' | 'MEM' | 'WB';
type StageState = { stage: StageName; pc: string; valid: boolean };
type HazardType = 'none' | 'data' | 'load-use' | 'control';
type HazardState = {
  active: boolean;
  type: HazardType;
  stalled: boolean;
  forwarded: boolean;
  stalls: number;
  sourcePc: string;
  consumerPc: string;
  register: number;
  fpr: boolean;
};
type BtbEntry = {
  index: number;
  pc: string;
  target: string;
  counter: number;
  stateLabel: string;
  unconditional: boolean;
};
type Reg = { name: string; abi: string; value: bigint };
type InstructionRow = { address: string; encoding: string; instruction: string; sourceLine: number };
type MachineState = {
  pc: string;
  gpr: Record<string, string>;
  fpr: Record<string, string>;
  csr: Record<string, string>;
  cycles: number;
  instructionsRetired: number;
  stallCycles: number;
  branchMispredictions: number;
  branchPredictor: '2bit' | 'static-nt';
  branchPredictions: number;
  btbHits: number;
  branchAccuracy: number;
  btbEntries: BtbEntry[];
  cpi: number;
  ipc: number;
  programLoaded: boolean;
  programComplete: boolean;
  processor: string;
  pipeline: boolean;
  stages: StageState[];
  hazard: HazardState;
};
type AssembleResponse = MachineState & { message: string; instructions: InstructionRow[] };
type StepResponse = MachineState & { message: string };
type ToastTone = 'danger' | 'forward' | 'control';
type HazardView = { title: string; detail: string; tone: ToastTone };
type HazardLogEntry = HazardView & { id: string; cycle: number };
type Toast = HazardView & { id: string };

const API = import.meta.env.VITE_KITES_API ?? 'http://127.0.0.1:8080';
const gAbi = ['zero', 'ra', 'sp', 'gp', 'tp', 't0', 't1', 't2', 's0', 's1', 'a0', 'a1', 'a2', 'a3', 'a4', 'a5', 'a6', 'a7', 's2', 's3', 's4', 's5', 's6', 's7', 's8', 's9', 's10', 's11', 't3', 't4', 't5', 't6'];
const fAbi = ['ft0', 'ft1', 'ft2', 'ft3', 'ft4', 'ft5', 'ft6', 'ft7', 'fs0', 'fs1', 'fa0', 'fa1', 'fa2', 'fa3', 'fa4', 'fa5', 'fa6', 'fa7', 'fs2', 'fs3', 'fs4', 'fs5', 'fs6', 'fs7', 'fs8', 'fs9', 'fs10', 'fs11', 'ft8', 'ft9', 'ft10', 'ft11'];
const stageOrder: StageName[] = ['IF', 'ID', 'EX', 'MEM', 'WB'];
const stageColors: Record<StageName, string> = {
  IF: '#7c3aed',
  ID: '#0284c7',
  EX: '#d97706',
  MEM: '#059669',
  WB: '#dc2626',
};
const processorModes = [
  { id: 'rvss', label: 'RVSS · Single cycle', short: 'Single cycle', pipeline: false, note: 'One instruction completes per cycle. Supports the I, M, F, and D extensions.' },
  { id: 'rv5-nh-nf', label: 'RV5 · NH / NF', short: 'No hazard detection · No forwarding', pipeline: true, note: 'Five-stage pipeline with no hazard detection and no forwarding. Insert NOPs between dependent instructions yourself; otherwise the pipeline reads stale registers.' },
  { id: 'rv5-h-nf', label: 'RV5 · H / NF', short: 'Hazard detection · No forwarding', pipeline: true, note: 'The hazard detection unit inserts bubbles automatically. Every register dependency stalls the pipeline until write-back.' },
  { id: 'rv5-nh-f', label: 'RV5 · NH / F', short: 'No hazard detection · Forwarding', pipeline: true, note: 'The forwarding unit supplies ALU results around the register file, but a load followed immediately by a dependent instruction is not detected and will read stale data.' },
  { id: 'rv5-h-f', label: 'RV5 · H / F', short: 'Hazard detection · Forwarding', pipeline: true, note: 'Forwarding removes most data stalls and the hazard unit inserts a bubble for load-use dependencies. Branch flushes are handled automatically.' },
] as const;

const initialCode = `# Kites web simulator — RV64IMAFD
.text
_start:
    li   t0, 42
    li   t1, 10
    add  a0, t0, t1
    li   t2, 0x10000000
    sd   a0, 0(t2)
    ld   a1, 0(t2)
    fmv.d.x  ft0, a1
    fadd.d   fa0, ft0, ft0
`;

const zeroHazard = (): HazardState => ({
  active: false, type: 'none', stalled: false, forwarded: false, stalls: 0,
  sourcePc: '', consumerPc: '', register: 0, fpr: false,
});
const zeroState = (): MachineState => ({
  pc: '0x0000000000000000', gpr: {}, fpr: {}, csr: {}, cycles: 0,
  instructionsRetired: 0, stallCycles: 0, branchMispredictions: 0,
  branchPredictor: 'static-nt', branchPredictions: 0, btbHits: 0, branchAccuracy: 100, btbEntries: [],
  cpi: 0, ipc: 0,
  programLoaded: false, programComplete: false, processor: '', pipeline: false,
  stages: [], hazard: zeroHazard(),
});
const normalizeState = (state: MachineState): MachineState => ({
  ...zeroState(),
  ...state,
  stages: state.stages ?? [],
  hazard: state.hazard ?? zeroHazard(),
  btbEntries: state.btbEntries ?? [],
});
const hex = (n: bigint) => '0x' + n.toString(16).padStart(16, '0');
const shortPc = (pc: string) => {
  if (!pc)
    return '—';
  try {
    return '0x' + BigInt(pc).toString(16).padStart(4, '0');
  }
  catch {
    return pc;
  }
};
const addressKey = (address: string) => {
  try {
    return BigInt(address).toString();
  }
  catch {
    return address;
  }
};
const registerLabel = (hazard: HazardState) => hazard.fpr
  ? (fAbi[hazard.register] ?? `f${hazard.register}`)
  : (gAbi[hazard.register] ?? `x${hazard.register}`);
// Disassembly carries the branch target's label in angle brackets, e.g.
// "bne x5, x0, -28 <loop>". Split it out so the BTB can show both.
const splitBranchLabel = (row?: InstructionRow) => {
  if (!row)
    return { instruction: '', label: '' };
  const match = /^(.*?)\s*<([^>]+)>\s*$/.exec(row.instruction);
  return match
    ? { instruction: match[1].trim(), label: match[2] }
    : { instruction: row.instruction, label: '' };
};
const parseAddress = (s: string) => {
  const v = s.trim();
  if (!v)
    throw new Error('Enter an address');
  return BigInt(v.toLowerCase().startsWith('0x') ? v : `0x${v}`);
};
const asBig = (s?: string) => BigInt(s ?? '0x0');

function describeHazard(hazard: HazardState, rows: Map<string, InstructionRow>): HazardView | null {
  if (!hazard?.active)
    return null;
  const rowFor = (pc: string) => {
    if (!pc)
      return undefined;
    try {
      return rows.get(BigInt(pc).toString());
    }
    catch {
      return undefined;
    }
  };
  const label = (pc: string) => {
    const row = rowFor(pc);
    if (row)
      return `${row.instruction} (line ${row.sourceLine})`;
    return pc ? shortPc(pc) : 'an unknown instruction';
  };
  const reg = registerLabel(hazard);
  const cycles = Math.max(1, hazard.stalls);
  const cycleWord = cycles === 1 ? 'cycle' : 'cycles';
  if (hazard.type === 'control') {
    return {
      title: 'Control hazard',
      detail: `Branch at ${label(hazard.sourcePc)} was taken. The two instructions behind it were flushed and refetched.`,
      tone: 'control',
    };
  }
  if (hazard.type === 'load-use') {
    return {
      title: 'Load-use hazard',
      detail: `${label(hazard.consumerPc)} depends on ${reg} loaded by ${label(hazard.sourcePc)}. Stalling ${cycles} ${cycleWord}.`,
      tone: 'danger',
    };
  }
  if (hazard.forwarded) {
    return {
      title: 'Data hazard — forwarded',
      detail: `${reg} was forwarded from ${label(hazard.sourcePc)} to ${label(hazard.consumerPc)}. No stall needed.`,
      tone: 'forward',
    };
  }
  return {
    title: 'Data hazard',
    detail: `${label(hazard.consumerPc)} reads ${reg} written by ${label(hazard.sourcePc)}. Stalling ${cycles} ${cycleWord}.`,
    tone: 'danger',
  };
}

async function api<T>(path: string, init?: RequestInit): Promise<T> {
  const response = await fetch(`${API}${path}`, {
    ...init,
    headers: { 'Content-Type': 'application/json', ...(init?.headers ?? {}) },
  });
  const data = await response.json().catch(() => ({ error: `HTTP ${response.status}` }));
  if (!response.ok)
    throw new Error(data.error ?? `HTTP ${response.status}`);
  return data as T;
}

function StageCard({ stage, active, pc, instruction, sourceLine, hazard }: {
  stage: StageName;
  active: boolean;
  pc: string;
  instruction?: string;
  sourceLine?: number;
  hazard: boolean;
}) {
  return <div
    className={`stage-card ${active ? 'active' : 'idle'} ${hazard ? 'hazard' : ''}`}
    style={{ '--stage-color': stageColors[stage] } as CSSProperties}
  >
    <div className="stage-card-head">
      <span className="stage-name">{stage}</span>
      <span className="stage-dot" />
    </div>
    <code className="stage-pc">{active ? shortPc(pc) : '—'}</code>
    <span className="stage-instruction">{active ? (instruction ?? 'bubble') : 'empty'}</span>
    <span className="stage-line">{active && sourceLine ? `line ${sourceLine}` : '\u00a0'}</span>
  </div>;
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
  const [predictor, setPredictor] = useState<'2bit' | 'static-nt'>('2bit');
  // Branch prediction needs a variant that resolves branch conditions in EX.
  const predictorSupported = vm === 'rv5-h-f' || vm === 'rv5-h-nf';
  const [instructions, setInstructions] = useState<InstructionRow[]>([]);
  const [currentSourceLine, setCurrentSourceLine] = useState(0);
  const [currentAddress, setCurrentAddress] = useState('');
  const [speed, setSpeed] = useState(5);
  const [running, setRunning] = useState(false);
  const [examplesOpen, setExamplesOpen] = useState(false);
  const examplesWrapRef = useRef<HTMLDivElement>(null);
  const [toast, setToast] = useState<Toast | null>(null);
  const [hazardLog, setHazardLog] = useState<HazardLogEntry[]>([]);
  // Hazard callouts, log, popups and stage rings can be hidden on demand; the choice
  // is remembered so a reload keeps the user's preference.
  const [showHazards, setShowHazards] = useState<boolean>(() => {
    try {
      return window.localStorage.getItem('kites.showHazards') !== 'false';
    }
    catch {
      return true;
    }
  });
  const toggleHazards = () => {
    const next = !showHazards;
    setShowHazards(next);
    try {
      window.localStorage.setItem('kites.showHazards', String(next));
    }
    catch {
      // Storage can be unavailable (private mode); the toggle still works for this session.
    }
    if (!next)
      setToast(null);
  };
  const runActive = useRef(false);
  const speedRef = useRef(5);
  const lastHazardKey = useRef('');
  const editorRef = useRef<HTMLTextAreaElement>(null);
  const lineGutterRef = useRef<HTMLDivElement>(null);
  const scrollRef = useRef<HTMLDivElement>(null);
  const loadedEnd = useRef<bigint>(0n);
  const loadLock = useRef(false);

  const mode = processorModes.find(item => item.id === vm) ?? processorModes[0];
  const refreshState = useCallback(async () => {
    const s = await api<MachineState>('/api/state');
    setMachine(normalizeState(s));
  }, []);
  const refreshHealth = useCallback(async () => {
    try {
      await api('/api/health');
      setConnected(true);
      // Only claim the connection state while the footer has nothing more important
      // to show; otherwise an assembly/run error would vanish after three seconds.
      setMessage(previous =>
        previous === 'Connecting to Kites…' || previous === 'Connected to Kites'
          ? 'Connected to Kites'
          : previous);
      await refreshState();
    }
    catch (e) {
      setConnected(false);
      setMessage(previous =>
        previous === 'Connecting to Kites…' || previous === 'Connected to Kites'
          ? (e as Error).message
          : previous);
    }
  }, [refreshState]);
  useEffect(() => {
    void refreshHealth();
    const timer = window.setInterval(() => void refreshHealth(), 3000);
    return () => window.clearInterval(timer);
  }, [refreshHealth]);
  const visibleMemoryBytes = () => Math.max(256, Math.ceil((window.innerHeight - 200) / 25) * 16);

  //--- loads memory ---
  const loadMemory = useCallback(async (start: bigint, length = 256, append = false) => {
    if (loadLock.current)
      return;
    loadLock.current = true;
    setLoadingMemory(true);
    try {
      const q = new URLSearchParams({ address: hex(start), length: String(length) });
      const r = await api<{ bytes: number[] }>(`/api/memory?${q}`);
      setMemory(old => {
        const next = append ? { ...old } : {};
        r.bytes.forEach((v, i) => { next[(start + BigInt(i)).toString()] = v; });
        return next;
      });
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
      return r.value.toString();
    if (format === 'Float') {
      const b = new ArrayBuffer(4);
      const d = new DataView(b);
      d.setUint32(0, Number(r.value & 0xffffffffn), true);
      return String(d.getFloat32(0, true));
    }
    if (format === 'Double') {
      const b = new ArrayBuffer(8);
      const d = new DataView(b);
      d.setBigUint64(0, r.value, true);
      return String(d.getFloat64(0, true));
    }
    return hex(r.value);
  };

  const rowByAddress = useMemo(() => {
    const map = new Map<string, InstructionRow>();
    instructions.forEach(row => map.set(addressKey(row.address), row));
    return map;
  }, [instructions]);
  const stageRows = useMemo(() => (machine.stages ?? [])
    .filter(stage => stage.valid && stage.pc)
    .map(stage => ({
      stage: stage.stage,
      pc: stage.pc,
      row: rowByAddress.get(addressKey(stage.pc)),
    })), [machine.stages, rowByAddress]);
  const stagesByRow = useMemo(() => {
    const map = new Map<string, StageName[]>();
    stageRows.forEach(item => {
      if (!item.row)
        return;
      const key = addressKey(item.row.address);
      const list = map.get(key) ?? [];
      if (!list.includes(item.stage))
        list.push(item.stage);
      map.set(key, list);
    });
    return map;
  }, [stageRows]);
  const stagesByLine = useMemo(() => {
    const map = new Map<number, StageName[]>();
    stageRows.forEach(item => {
      if (!item.row)
        return;
      const list = map.get(item.row.sourceLine) ?? [];
      if (!list.includes(item.stage))
        list.push(item.stage);
      map.set(item.row.sourceLine, list);
    });
    return map;
  }, [stageRows]);
  const hazardView = useMemo(() => describeHazard(machine.hazard, rowByAddress), [machine.hazard, rowByAddress]);
  const hazardStages = useMemo(() => {
    const stages = new Set<StageName>();
    if (!machine.hazard.active)
      return stages;
    const keys = [machine.hazard.sourcePc, machine.hazard.consumerPc]
      .filter(Boolean)
      .map(addressKey);
    (machine.stages ?? []).forEach(stage => {
      if (stage.valid && stage.pc && keys.includes(addressKey(stage.pc)))
        stages.add(stage.stage);
    });
    return stages;
  }, [machine.stages, machine.hazard]);
  const highlightStyle = useMemo(() => {
    if (stagesByLine.size === 0)
      return undefined;
    const gradients: string[] = [];
    stagesByLine.forEach((stages, line) => {
      const top = 8 + (line - 1) * 21;
      const color = stageColors[stages[0]];
      gradients.push(`linear-gradient(to bottom, transparent ${top}px, ${color}26 ${top}px, ${color}26 ${top + 21}px, transparent ${top + 21}px)`);
    });
    return {
      backgroundImage: gradients.join(', '),
      backgroundAttachment: 'local',
    } as CSSProperties;
  }, [stagesByLine]);

  useEffect(() => {
    if (!hazardView) {
      lastHazardKey.current = '';
      return;
    }
    const key = [
      machine.hazard.type,
      machine.hazard.sourcePc,
      machine.hazard.consumerPc,
      machine.hazard.register,
      machine.hazard.stalled ? 'stall' : 'forward',
    ].join('|');
    if (key === lastHazardKey.current)
      return;
    lastHazardKey.current = key;
    const id = `${key}|${machine.cycles}`;
    if (showHazards)
      setToast({ id, ...hazardView });
    setHazardLog(log => [{ id, cycle: machine.cycles, ...hazardView }, ...log].slice(0, 8));
  }, [hazardView, machine.cycles, machine.hazard, showHazards]);

  useEffect(() => {
    if (!toast)
      return;
    const timer = window.setTimeout(() => {
      setToast(current => current?.id === toast.id ? null : current);
    }, 4200);
    return () => window.clearTimeout(timer);
  }, [toast]);

  const activateState = useCallback((state: MachineState) => {
    const rows = (state.stages ?? [])
      .filter(stage => stage.valid && stage.pc)
      .map(stage => ({ stage: stage.stage, row: rowByAddress.get(addressKey(stage.pc)) }))
      .filter(item => item.row) as { stage: StageName; row: InstructionRow }[];
    const primary = rows.find(item => item.stage === 'EX') ?? rows[0];
    if (!primary)
      return;
    setCurrentSourceLine(primary.row.sourceLine);
    setCurrentAddress(primary.row.address);
    const lineTop = Math.max(0, (primary.row.sourceLine - 1) * 21 - 80);
    if (editorRef.current)
      editorRef.current.scrollTop = lineTop;
    if (lineGutterRef.current)
      lineGutterRef.current.scrollTop = lineTop;
  }, [rowByAddress]);

  // assemble function
  const assemble = async () => {
    setBusy(true);
    try {
      const s = await api<AssembleResponse>('/api/program/assemble', {
        method: 'POST',
        body: JSON.stringify({ source: code, vm, branchPredictor: predictor }),
      });
      setMachine(normalizeState(s));
      setInstructions(s.instructions);
      setCurrentSourceLine(0);
      setCurrentAddress('');
      setHazardLog([]);
      setToast(null);
      lastHazardKey.current = '';
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
  const step = async () => {
    setBusy(true);
    try {
      const s = await api<StepResponse>('/api/simulator/step', {
        method: 'POST',
        body: JSON.stringify({ count: 1 }),
      });
      setMachine(normalizeState(s));
      activateState(s);
      setMessage(s.programComplete ? 'Execution complete' : s.message);
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
    }
    setBusy(true);
    runActive.current = true;
    setRunning(true);
    try {
      const assembled = await api<AssembleResponse>('/api/program/assemble', {
        method: 'POST',
        body: JSON.stringify({ source: code, vm, branchPredictor: predictor }),
      });
      setMachine(normalizeState(assembled));
      setInstructions(assembled.instructions);
      setCurrentSourceLine(0);
      setCurrentAddress('');
      setHazardLog([]);
      setToast(null);
      lastHazardKey.current = '';
      setMessage('Running…');
      setBusy(false);
      if (!assembled.instructions.length)
        throw new Error('No instructions to execute');
      let state: MachineState = assembled;
      let steps = 0;
      while (runActive.current) {
        state = await api<StepResponse>('/api/simulator/step', {
          method: 'POST',
          body: JSON.stringify({ count: 1 }),
        });
        setMachine(normalizeState(state));
        activateState(state);
        if (state.programComplete) {
          setMessage('Execution complete');
          break;
        }
        if (++steps >= 500000) {
          setMessage('Stopped after 500000 steps');
          break;
        }
        await new Promise(resolve => window.setTimeout(resolve, Math.max(0, Math.round(1000 / speedRef.current))));
      }
      if (!runActive.current)
        setMessage('Execution stopped');
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
    setBusy(true);
    try {
      const s = await api<StepResponse>('/api/simulator/reset', { method: 'POST', body: '{}' });
      setMachine(normalizeState(s));
      setCurrentSourceLine(0);
      setCurrentAddress('');
      setHazardLog([]);
      setToast(null);
      lastHazardKey.current = '';
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
      const result = await api<{ bytes: number[] }>(`/api/memory?${q}`);
      setMemory(Object.fromEntries(result.bytes.map((byte, i) => [(base + BigInt(i)).toString(), byte])));
      setLoadedBytes(result.bytes.length);
      loadedEnd.current = base + BigInt(result.bytes.length);
      const offset = result.bytes.indexOf(value);
      if (offset < 0) {
        setMessage('No matching byte found in the next 4 KB');
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
  const onMemoryScroll = (e: UIEvent<HTMLDivElement>) => {
    const el = e.currentTarget;
    if (el.scrollTop + el.clientHeight >= el.scrollHeight - 100 && !loadingMemory && loadedBytes > 0)
      void loadMemory(loadedEnd.current, 256, true);
  };
  const writeMemory = async (addr: bigint, value: string) => {
    const n = memFormat === 'Hex' ? Number.parseInt(value.replace(/^0x/i, ''), 16) : Number(value);
    if (!Number.isInteger(n) || n < 0 || n > 255)
      return;
    try {
      await api('/api/memory/write', { method: 'POST', body: JSON.stringify({ address: hex(addr), value: n }) });
      setMemory(m => ({ ...m, [addr.toString()]: n }));
    }
    catch (e) {
      setMessage((e as Error).message);
    }
  };
  const exportCode = () => {
    const blob = new Blob([code], { type: 'text/plain' });
    const a = document.createElement('a');
    a.href = URL.createObjectURL(blob);
    a.download = 'program.s';
    a.click();
    URL.revokeObjectURL(a.href);
  };
  const importCode = (file?: File) => {
    if (!file)
      return;
    const reader = new FileReader();
    reader.onload = () => setCode(String(reader.result ?? ''));
    reader.readAsText(file);
  };
  useEffect(() => {
    if (!examplesOpen)
      return;
    const closeOnOutsideClick = (event: MouseEvent) => {
      if (!examplesWrapRef.current?.contains(event.target as Node))
        setExamplesOpen(false);
    };
    const closeOnEscape = (event: KeyboardEvent) => {
      if (event.key === 'Escape')
        setExamplesOpen(false);
    };
    document.addEventListener('mousedown', closeOnOutsideClick);
    document.addEventListener('keydown', closeOnEscape);
    return () => {
      document.removeEventListener('mousedown', closeOnOutsideClick);
      document.removeEventListener('keydown', closeOnEscape);
    };
  }, [examplesOpen]);

  const loadExample = (example: Example) => {
    setCode(example.source);
    if (example.vm) setVm(example.vm);
    if (example.predictor) setPredictor(example.predictor);
    setExamplesOpen(false);
    editorRef.current?.focus();
  };
  const rows = Array.from({ length: Math.ceil(loadedBytes / 16) }, (_, i) => i);
  const stripStages: StageName[] = machine.pipeline ? stageOrder : ['EX'];
  const hazardSummary = machine.hazard.active
    ? machine.hazard.type === 'control'
      ? 'control'
      : machine.hazard.forwarded
        ? 'forwarded'
        : machine.hazard.type === 'load-use'
          ? 'load-use'
          : 'data'
    : '—';

  const renderHazard = () => hazardView
    ? <div className={`hazard-callout ${hazardView.tone}`}>
        <AlertTriangle size={14} />
        <div>
          <b>{hazardView.title}</b>
          <span>{hazardView.detail}</span>
        </div>
      </div>
    : null;

  return <div className="app">
    <header className="topbar">
      <div className="brand"><span className="brand-mark">⌁</span><b>RISC-V Simulator</b><span className="demo-tag">KITES</span></div>
      <nav>
        {(['Editor', 'Datapath', 'Memory', 'Cache'] as Tab[]).map((x, i) => <button className={tab === x ? 'nav active' : 'nav'} onClick={() => setTab(x)} key={x}>{[<Code2 />, <Cpu />, <Database />, <MemoryStick />][i]}{x}</button>)}
      </nav>
      <div className="controls">
        <button title={running ? 'Stop execution' : 'Run step by step'} disabled={(busy && !running) || !connected} onClick={() => void run()}>{running ? <Square /> : <Play />}</button>
        <button title="Pause is not yet exposed by Kites HTTP API" disabled><span className="pause-mark" /></button>
        <button title="Reset" disabled={busy || running} onClick={() => void reset()}><RotateCcw /></button>
        <button title="Step one cycle" disabled={busy || running || !machine.programLoaded || machine.programComplete} onClick={() => void step()}><SkipForward /></button>
        <select className="vm-select" disabled={busy || running} value={vm} onChange={e => setVm(e.target.value)} title={mode.note}>
          {processorModes.map(item => <option value={item.id} key={item.id}>{item.label}</option>)}
        </select>
        <select
          className="vm-select predictor-select"
          disabled={busy || running || !predictorSupported}
          value={predictorSupported ? predictor : 'static-nt'}
          onChange={e => setPredictor(e.target.value as '2bit' | 'static-nt')}
          title={predictorSupported ? 'Branch predictor used by the RV5 H_* variants' : 'Branch prediction needs an RV5 H/NF or H/F variant (branch resolved in EX)'}
        >
          <option value="2bit">2-bit predictor</option>
          <option value="static-nt">Static not taken</option>
        </select>
      </div>
    </header>
    <main className="workspace">
      <section className="main-panel">
        {tab === 'Editor' && <>
          <div className="panel-head">
            <div className="panel-title">Code Editor</div>
            <div className="actions">
              <label className="small-btn">Import<input type="file" accept=".s,.asm,.txt" hidden onChange={e => importCode(e.target.files?.[0])} /></label>
              <button className="small-btn" onClick={exportCode}><Download size={12} /> Export</button>
              <div className="examples-menu-wrap" ref={examplesWrapRef}>
                <button className="small-btn" aria-haspopup="menu" aria-expanded={examplesOpen} onClick={() => setExamplesOpen(open => !open)}>Examples <ChevronDown size={12} /></button>
                {examplesOpen && <div className="examples-menu-row"><div className="examples-menu" role="menu" aria-label="Assembly examples">
                  {examples.map(example => <button className="example-option" key={example.title} role="menuitem" onClick={() => loadExample(example)}>
                    <b>{example.title}</b>
                    <span>{example.description}</span>
                  </button>)}
                </div></div>}
              </div>
              <button className="primary" disabled={busy || running || !connected} onClick={() => void assemble()}>Assemble</button>
            </div>
          </div>
          <div className="editor-wrap">
            <div className="line-nos" ref={lineGutterRef}>
              {code.split('\n').map((_, i) => {
                const stages = stagesByLine.get(i + 1) ?? [];
                const color = stages.length ? stageColors[stages[0]] : undefined;
                return <div
                  className={currentSourceLine === i + 1 ? 'active' : ''}
                  key={i}
                  title={stages.length ? `Active stage: ${stages.join(', ')}` : undefined}
                  style={color ? { boxShadow: `inset 3px 0 ${color}`, background: `${color}14`, color } : undefined}
                >{i + 1}</div>;
              })}
            </div>
            <textarea
              ref={editorRef}
              spellCheck={false}
              value={code}
              onChange={e => setCode(e.target.value)}
              onScroll={e => {
                // Keep the line-number gutter locked to the code's vertical scroll.
                if (lineGutterRef.current)
                  lineGutterRef.current.scrollTop = e.currentTarget.scrollTop;
              }}
              aria-label="Assembly editor"
              style={highlightStyle}
            />
          </div>
          <div className="result">
            <div className="result-head">
              <b>Execution flow</b>
              <span className={/error|failed|cannot|unable/i.test(message) ? 'flow-message error' : 'flow-message'}>{message}</span>
              <button
                className={`hazard-toggle compact ${showHazards ? 'on' : 'off'}`}
                onClick={toggleHazards}
                title={showHazards ? 'Hide hazard details' : 'Show hazard details'}
                aria-pressed={showHazards}
              >
                {showHazards ? <EyeOff size={12} /> : <Eye size={12} />}
                Hazards {showHazards ? 'on' : 'off'}
              </button>
              <label className="speed-control">Speed <input type="range" min="1" max="20" value={speed} onChange={e => { const value = Number(e.target.value); setSpeed(value); speedRef.current = value; }} /><b>{speed} steps/s</b></label>
            </div>
            <div className={`pipeline-strip ${machine.pipeline ? 'five-stage' : 'single-stage'}`}>
              {stripStages.map(stage => {
                const state = (machine.stages ?? []).find(item => item.stage === stage);
                const row = state?.valid && state.pc ? rowByAddress.get(addressKey(state.pc)) : undefined;
                return <StageCard
                  key={stage}
                  stage={stage}
                  active={!!(state?.valid && state.pc)}
                  pc={state?.pc ?? ''}
                  instruction={row?.instruction}
                  sourceLine={row?.sourceLine}
                  hazard={showHazards && hazardStages.has(stage)}
                />;
              })}
            </div>
            {showHazards && renderHazard()}
            <div className="stats-strip">
              <span>PC <b>{machine.pc}</b></span>
              <span>Cycles <b>{machine.cycles}</b></span>
              <span>Retired <b>{machine.instructionsRetired}</b></span>
              <span>CPI <b>{Number(machine.cpi).toFixed(2)}</b></span>
              <span>IPC <b>{Number(machine.ipc).toFixed(2)}</b></span>
              <span>Stalls <b>{machine.stallCycles}</b></span>
              <span>Branch misses <b>{machine.branchMispredictions}</b></span>
              <span>Predictions <b>{machine.branchPredictions}</b></span>
              <span>BTB hits <b>{machine.btbHits}</b></span>
              <span>Pred accuracy <b>{Number(machine.branchAccuracy).toFixed(1)}%</b></span>
              {showHazards && <span className={machine.hazard.active ? 'hazard-stat active' : 'hazard-stat'}>Hazard <b>{hazardSummary}</b></span>}
            </div>
            <div className="mode-note"><b>{mode.short}</b><span>{mode.note}</span></div>
            <div className="instruction-scroll">
              <table className="instruction-table">
                <thead><tr><th>ADDRESS</th><th>ENCODING</th><th>INSTRUCTION</th><th>LINE</th><th>STAGE</th></tr></thead>
                <tbody>
                  {instructions.map(row => {
                    const rowStages = stagesByRow.get(addressKey(row.address)) ?? [];
                    const color = rowStages.length ? stageColors[rowStages[0]] : undefined;
                    return <tr
                      key={row.address}
                      className={`${rowStages.length ? 'active' : ''} ${row.address === currentAddress ? 'focused' : ''}`}
                      style={color ? { boxShadow: `inset 3px 0 ${color}` } : undefined}
                    >
                      <td>{row.address}</td><td>{row.encoding}</td><td>{row.instruction}</td><td>{row.sourceLine}</td>
                      <td><div className="stage-pills">{rowStages.length
                        ? rowStages.map(stage => <span className="stage-pill" key={stage} style={{ color: stageColors[stage], background: `${stageColors[stage]}18`, borderColor: `${stageColors[stage]}44` }}>{stage}</span>)
                        : <span className="stage-pills-empty">—</span>}</div></td>
                    </tr>;
                  })}
                </tbody>
              </table>
              {instructions.length === 0 && <div className="instruction-empty">Assemble a program to view its instruction flow.</div>}
            </div>
          </div>
        </>}
        {tab === 'Datapath' && <div className="datapath">
          <div className="datapath-head">
            <div>
              <div className="panel-title">Live processor datapath</div>
              <span className="datapath-sub">{machine.processor || mode.label}</span>
            </div>
            <div className="datapath-head-actions">
              <button
                className={`hazard-toggle ${showHazards ? 'on' : 'off'}`}
                onClick={toggleHazards}
                title={showHazards ? 'Hide hazard details' : 'Show hazard details'}
                aria-pressed={showHazards}
              >
                {showHazards ? <EyeOff size={12} /> : <Eye size={12} />}
                {showHazards ? 'Hide hazards' : 'Show hazards'}
              </button>
              <span className={`pipeline-state ${machine.pipeline ? 'pipeline' : 'single'}`}>{machine.pipeline ? '5-stage pipeline' : 'single-cycle'}</span>
            </div>
          </div>
          <div className="datapath-body">
            <div className={`datapath-flow ${machine.pipeline ? 'five' : 'one'}`}>
              {stripStages.map(stage => {
                const state = (machine.stages ?? []).find(item => item.stage === stage);
                const row = state?.valid && state.pc ? rowByAddress.get(addressKey(state.pc)) : undefined;
                return <StageCard
                  key={stage}
                  stage={stage}
                  active={!!(state?.valid && state.pc)}
                  pc={state?.pc ?? ''}
                  instruction={row?.instruction}
                  sourceLine={row?.sourceLine}
                  hazard={showHazards && hazardStages.has(stage)}
                />;
              })}
            </div>
            {showHazards
              ? hazardView ? renderHazard() : <div className="datapath-idle"><span className="idle-dot" />No hazard in the current cycle.</div>
              : <div className="hazard-hidden-note"><EyeOff size={13} /><span>Hazard callouts and activity are hidden.</span><button onClick={toggleHazards}>Show hazards</button></div>}
            <div className="mode-card">
              <div className="mode-card-title">{mode.label}</div>
              <p>{mode.note}</p>
            </div>
            {showHazards && <div className="hazard-log">
              <div className="hazard-log-head"><b>Hazard activity</b><span>{hazardLog.length} recent event{hazardLog.length === 1 ? '' : 's'}</span></div>
              {hazardLog.length === 0
                ? <div className="hazard-log-empty">Stalls, forwarded values, and branch flushes will appear here as they happen.</div>
                : hazardLog.map(entry => <div className={`hazard-log-row ${entry.tone}`} key={entry.id}>
                    <span className="hazard-log-cycle">cycle {entry.cycle}</span>
                    <b>{entry.title}</b>
                    <span>{entry.detail}</span>
                  </div>)}
            </div>}
            <div className="predictor-card">
              <div className="predictor-card-head">
                <b>Branch prediction</b>
                <span>{machine.branchPredictor === '2bit' ? '2-bit saturating counters · 64-entry BTB' : 'static not taken'}</span>
              </div>
              {machine.branchPredictor === '2bit' ? <>
                <div className="predictor-metrics">
                  <span>Predictions <b>{machine.branchPredictions}</b></span>
                  <span>BTB hits <b>{machine.btbHits}</b></span>
                  <span>Mispredicts <b>{machine.branchMispredictions}</b></span>
                  <span>Accuracy <b>{Number(machine.branchAccuracy).toFixed(1)}%</b></span>
                </div>
                {machine.btbEntries.length
                  ? <div className="predictor-table-wrap"><table className="predictor-table">
                      <thead><tr><th>INDEX</th><th>BRANCH PC</th><th>INSTRUCTION</th><th>LABEL</th><th>TARGET</th><th>COUNTER</th><th>TYPE</th></tr></thead>
                      <tbody>{machine.btbEntries.map(entry => {
                        const branch = splitBranchLabel(rowByAddress.get(addressKey(entry.pc)));
                        return <tr key={entry.index}>
                          <td>{entry.index}</td>
                          <td>{shortPc(entry.pc)}</td>
                          <td title={branch.instruction ? `Branch at ${shortPc(entry.pc)}` : 'Instruction not in the current program'}>
                            {branch.instruction || '—'}
                          </td>
                          <td>{branch.label
                            ? <span className="predictor-label" title={`Branch target label: ${branch.label}`}>{branch.label}</span>
                            : '—'}</td>
                          <td>{shortPc(entry.target)}</td>
                          <td><span className={`predictor-counter counter-${entry.counter}`}>{entry.stateLabel}</span></td>
                          <td>{entry.unconditional ? 'jump' : 'conditional'}</td>
                        </tr>;
                      })}</tbody>
                    </table></div>
                  : <div className="predictor-empty">No branches executed yet. Load the <b>Branch Prediction · 2-bit BTB</b> example, assemble with an H/* VM, then run it.</div>}
              </> : <div className="predictor-empty">This VM uses static not-taken prediction and keeps no BTB/BHT. Pick RV5 H/NF or H/F to use the 2-bit predictor.</div>}
            </div>
            <div className="stage-legend">
              {stageOrder.map(stage => <span key={stage} style={{ color: stageColors[stage] }}><i style={{ background: stageColors[stage] }} />{stage}</span>)}
            </div>
          </div>
        </div>}
        {tab === 'Memory' && <>
          <div className="memory-toolbar">
            <div className="segments">{['Data Segment', 'Text Segment', 'GP', 'SP'].map(s => <button className={segment === s ? 'selected' : ''} onClick={() => setSegment(s)} key={s}>{s}</button>)}</div>
            <div className="memory-search"><Search size={14} /><input value={address} onChange={e => setAddress(e.target.value)} onKeyDown={e => e.key === 'Enter' && void jump()} placeholder="Address" /><button onClick={() => void jump()}>Go</button></div>
            <div className="memory-search value-search"><Search size={14} /><input value={memoryQuery} onChange={e => setMemoryQuery(e.target.value)} onKeyDown={e => e.key === 'Enter' && void searchMemory()} placeholder={memFormat === 'Hex' ? 'Byte (e.g. 2a)' : 'Byte (0–255)'} /><button onClick={() => void searchMemory()}>Find</button></div>
            <select value={memFormat} onChange={e => setMemFormat(e.target.value)}><option>Hex</option><option>Decimal</option></select>
            <button className="icon-btn" title="Refresh" onClick={() => void loadMemory(base, Math.max(visibleMemoryBytes(), loadedBytes), false)}><RotateCcw size={14} /></button>
          </div>
          <div className="memory-scroll" ref={scrollRef} onScroll={onMemoryScroll}>
            <table className="memory-table">
              <thead><tr><th>ADDRESS</th>{Array.from({ length: 16 }, (_, i) => <th key={i}>+{i.toString(16).padStart(2, '0').toUpperCase()}</th>)}</tr></thead>
              <tbody>
                {rows.map(row => {
                  const a = base + BigInt(row * 16);
                  return <tr key={a.toString()}>
                    <th>{hex(a)}</th>
                    {Array.from({ length: 16 }, (_, i) => {
                      const addr = a + BigInt(i);
                      const val = memory[addr.toString()] ?? 0;
                      return <td key={i}><input key={`${addr.toString()}-${memFormat}`} aria-label={`Memory ${hex(addr)}`} defaultValue={memFormat === 'Decimal' ? String(val) : val.toString(16).padStart(2, '0')} onBlur={e => void writeMemory(addr, e.target.value)} /></td>;
                    })}
                  </tr>;
                })}
              </tbody>
            </table>
            {loadingMemory && <div className="load-note">Loading memory…</div>}
            {!loadingMemory && <div className="load-note">Scroll down to load the next memory page</div>}
          </div>
        </>}
        {tab === 'Cache' && <div className="placeholder"><MemoryStick size={28} /><b>Cache</b><span>Cache visualization is not yet exposed by the Kites web API.</span></div>}
      </section>
      <aside className="register-panel">
        <div className="reg-head"><b>Register</b><select value={format} onChange={e => setFormat(e.target.value as typeof format)}><option value="Hex">Hexadecimal</option><option value="Decimal">Decimal</option><option value="Float">Float</option><option value="Double">Double</option></select></div>
        <div className="reg-tabs"><button className={regTab === 'Integer' ? 'selected' : ''} onClick={() => setRegTab('Integer')}>Integer</button><button className={regTab === 'Floating point' ? 'selected' : ''} onClick={() => setRegTab('Floating point')}>Floating point</button></div>
        <div className="reg-columns"><span>Name</span><span>Number</span><span>Value</span></div>
        <div className="reg-list">{(regTab === 'Integer' ? regs : fregs).map((r, i) => <div className="reg-row" key={r.name}><b>{r.abi}</b><span>{i}</span><code>{showValue(r)}</code></div>)}</div>
        <div className="pc-line"><span>PC</span><code>{machine.pc}</code></div>
        <div className="pc-line"><span>FPR status</span><code>{machine.csr.fcsr ?? '—'}</code></div>
      </aside>
    </main>
    <footer>
      <span className={connected ? 'status-dot' : 'status-dot offline'} />{message}
      <span className="footer-right">{connected ? <><Wifi size={12} /> Kites API connected</> : <><WifiOff size={12} /> API disconnected</>} · RV64IMAFD</span>
    </footer>
    {showHazards && toast && <div className={`hazard-toast ${toast.tone}`} role="status">
      <div className="toast-icon"><AlertTriangle size={17} /></div>
      <div className="toast-copy"><b>{toast.title}</b><span>{toast.detail}</span></div>
      <button className="toast-close" aria-label="Dismiss hazard alert" onClick={() => setToast(null)}><X size={14} /></button>
    </div>}
  </div>;
}
export default App;
