// profsum.mjs — summarize a V8 .cpuprofile (node --cpu-prof) by self time: which emulator functions and how much JIT-generated
// wasm the run spent its time in.  Functions of the main module keep their names when the wasm is built without --strip-all;
// every function of the per-region JIT modules shows up as "wasm-function[0]" in a distinct wasm:// url and is folded into one row.
//   node emu/tools/profsum.mjs prof/CPU.*.cpuprofile [top=30]
import { readFileSync } from 'node:fs';
const file = process.argv[2]; const top = Number(process.argv[3] || 30);
const p = JSON.parse(readFileSync(file, 'utf8'));
const byId = new Map(p.nodes.map(n => [n.id, n]));
const self = new Map(); let total = 0;
const dt = p.timeDeltas || []; const samples = p.samples || [];
for (let i = 0; i < samples.length; i++) { const n = byId.get(samples[i]); const d = dt[i] || 0; total += d; if (!n) continue;
  const cf = n.callFrame; let name = cf.functionName || '(anonymous)'; const url = cf.url || '';
  if (url.startsWith('wasm://')) { if (/wasm-function\[\d+\]/.test(name) && !/nebula_x64/.test(url) && url !== mainUrl(p)) name = 'JIT region code (all wasm-function[*] of generated modules)'; else name = `wasm ${name}`; }
  else if (url) name = `${name} (${url.split('/').pop()}:${cf.lineNumber + 1})`;
  self.set(name, (self.get(name) || 0) + d); }
function mainUrl(prof) { if (prof.__main !== undefined) return prof.__main; let best = '', bestN = 0; const cnt = new Map();
  for (const n of prof.nodes) { const u = n.callFrame.url || ''; if (u.startsWith('wasm://') && !/wasm-function\[0\]$/.test(n.callFrame.functionName)) cnt.set(u, (cnt.get(u) || 0) + 1); }
  for (const [u, c] of cnt) if (c > bestN) { bestN = c; best = u; } prof.__main = best; return best; }
const rows = [...self.entries()].sort((a, b) => b[1] - a[1]);
console.log(`total sampled ${(total / 1e6).toFixed(1)} s, ${rows.length} distinct frames; top ${top} by self time:`);
for (const [name, us] of rows.slice(0, top)) console.log(`${(100 * us / total).toFixed(1).padStart(5)}%  ${(us / 1e6).toFixed(2).padStart(7)} s  ${name}`);
