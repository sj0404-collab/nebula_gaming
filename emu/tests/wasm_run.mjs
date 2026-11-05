#!/usr/bin/env node
// Runs a Windows exe inside the wasm64 build of the emulator under Node (>= 24: memory64 + table64), with the same
// host surface the browser runtime provides (src/lib/x64/runtime.ts) minus graphics/sound/disk.  Used to measure the
// wasm interpreter/JIT speed and to check JIT correctness without a browser:
//   node emu/tests/wasm_run.mjs [--max-ms N] [--jit 0|1] [--mount hostdir=C:\\guest] [--wasm path] exe [args...]
import fs from 'node:fs';
import path from 'node:path';
import { performance } from 'node:perf_hooks';

const argv = process.argv.slice(2);
let maxMs = 60000, jit = 1, wasmPath = new URL('../../public/x64/nebula_x64.wasm', import.meta.url).pathname, exe = '', verbose = 0;
const mounts = [];
for (let i = 0; i < argv.length; i++) {
  const a = argv[i];
  if (a === '--max-ms') maxMs = +argv[++i];
  else if (a === '--jit') jit = +argv[++i];
  else if (a === '--wasm') wasmPath = argv[++i];
  else if (a === '--mount') mounts.push(argv[++i]);
  else if (a === '-v') verbose = 1;
  else { exe = a; break; }
}
if (!exe) { console.error('usage: wasm_run.mjs [--max-ms N] [--jit 0|1] exe'); process.exit(2); }

const bytes = fs.readFileSync(wasmPath);
const dec = new TextDecoder(), enc = new TextEncoder();
let ex = null;
const mem = () => ex.memory.buffer;
const bytesAt = (p, n) => new Uint8Array(mem(), Number(p), n);
const cstr = (p) => { if (!p) return ''; const m = new Uint8Array(mem()); let e = Number(p); while (m[e]) e++; return dec.decode(m.subarray(Number(p), e)); };
const put = (b) => { const p = ex.x64_alloc(BigInt(b.length + 1)); bytesAt(p, b.length).set(b); return p; };
const putStr = (s) => { const b = enc.encode(s + '\0'); const p = ex.x64_alloc(BigInt(b.length)); bytesAt(p, b.length).set(b); return p; };
const MATH = [Math.sin, Math.cos, Math.tan, Math.exp, Math.log, Math.pow, Math.atan2, Math.asin, Math.acos, Math.atan, Math.sinh, Math.cosh, Math.tanh, Math.log10, Math.log2, Math.cbrt];
const t0 = performance.now();
let out = '';
let jitModules = 0, jitBytes = 0, jitMs = 0, jitFail = 0;
let table = null;
const imports = {
  env: {
    host_write: (fd, p, n) => { const t = dec.decode(bytesAt(p, Number(n))); if (fd === 3) { if (verbose) process.stderr.write(t); } else { out += t; process.stdout.write(t); } },
    host_now_ms: () => performance.now(),
    host_wall_ms: () => Date.now(),
    host_present: () => {},
    host_math: (op, a, b) => (MATH[op] ? MATH[op](a, b) : 0),
    host_fs_write: () => 1, host_fs_size: () => -1n, host_fs_read: () => 0, host_fs_mkdir: () => 1, host_fs_remove: () => 1, host_fs_rename: () => 1,
    host_gfx_init: () => 0, host_gfx_shader: () => 0, host_gfx_buffer: () => 0, host_gfx_texture: () => 0, host_gfx_clear: () => {}, host_gfx_clear_depth: () => {}, host_gfx_draw: () => {}, host_gfx_readback: () => {}, host_gfx_free: () => {},
    host_gl_init: () => 0, host_gl_probe: () => 0, host_gl_call: () => 0n, host_gl_present: () => {},
    host_audio_open: () => 0, host_audio_write: () => 0, host_audio_queued: () => 0,
    // JIT: compile a wasm module emitted by emu/x64/jit.c and publish its function in the main module's table
    host_jit_compile: (p, n, reuse) => {
      const t = performance.now();
      try {
        const mod = new WebAssembly.Module(bytesAt(p, Number(n)).slice());
        const inst = new WebAssembly.Instance(mod, { env: { memory: ex.memory, ...jitImports } });
        const idx = reuse ? BigInt(reuse) : table.grow(1n);
        table.set(idx, inst.exports.f);
        jitModules++; jitBytes += Number(n); jitMs += performance.now() - t;
        return Number(idx);
      } catch (e) {
        jitFail++;
        if (jitFail <= 3) { console.error('[jit] compile failed:', String(e)); if (process.env.JIT_DUMP) fs.writeFileSync(`/tmp/jitfail${jitFail}.wasm`, bytesAt(p, Number(n))); }
        return 0;
      }
    },
  },
};
const jitImports = {};
const mod = new WebAssembly.Module(bytes);
const needsJit = WebAssembly.Module.imports(mod).some((i) => i.name === 'host_jit_compile');
const inst = new WebAssembly.Instance(mod, imports);
ex = inst.exports;
table = ex.__indirect_function_table || null;
if (needsJit) {
  for (const [k, v] of Object.entries(ex)) if (k.startsWith('jit_') && typeof v === 'function') jitImports[k] = v;
  if (ex.x64_set_jit) ex.x64_set_jit(jit && table ? 1 : 0);
}
for (const m of mounts) {
  const [host, guest] = m.split('=');
  const walk = (h, g) => {
    for (const e of fs.readdirSync(h, { withFileTypes: true })) {
      const hp = path.join(h, e.name), gp = g + '\\' + e.name;
      if (e.isDirectory()) { ex.x64_vfs_mkdir(putStr(gp)); walk(hp, gp); }
      else { const b = fs.readFileSync(hp); ex.x64_vfs_add(putStr(gp), put(b), BigInt(b.length)); }
    }
  };
  ex.x64_vfs_mkdir(putStr(guest)); walk(host, guest);
}
const exeBytes = fs.readFileSync(exe);
let name = 'C:\\nebula\\' + path.basename(exe);
for (const m of mounts) { const [host, guest] = m.split('='); const rel = path.relative(path.resolve(host), path.resolve(exe)); if (!rel.startsWith('..')) name = guest + '\\' + rel.split(path.sep).join('\\'); }
const rc = ex.x64_load(put(exeBytes), BigInt(exeBytes.length), putStr(name), putStr([name, ...argv.slice(argv.indexOf(exe) + 1)].join(' ')));
if (rc !== 0) { console.error('load failed:', cstr(ex.x64_report())); process.exit(1); }
const RUN_OK = 0, RUN_EXIT = 1, RUN_FAULT = 2, RUN_YIELD = 3, RUN_BREAK = 4;
let r = RUN_OK, status = 'timeout';
const tRun = performance.now();
for (;;) {
  r = ex.x64_run(400000n);
  if (r === RUN_YIELD) {
    if (ex.x64_wants_stdin()) { ex.x64_stdin_eof(); continue; }
    const s = ex.x64_sleep_ms(); if (s > 0) { const until = performance.now() + Math.min(s, 50); while (performance.now() < until); }
    if (performance.now() - tRun > maxMs) { status = 'timeout'; break; }
    continue;
  }
  if (r === RUN_EXIT) { status = 'exit ' + ex.x64_exit_code(); break; }
  if (r === RUN_FAULT) { status = 'FAULT ' + cstr(ex.x64_error()) + ' rip=' + ex.x64_error_rip().toString(16); break; }
  if (r === RUN_BREAK) { status = 'break'; break; }
  if (performance.now() - tRun > maxMs) { status = 'timeout'; break; }
}
const ms = performance.now() - tRun, ic = Number(ex.x64_icount());
const stats = ex.x64_jit_stats ? cstr(ex.x64_jit_stats()) : '';
console.error(`[wasm_run] ${status}; ${(ic / 1e6).toFixed(1)} M insns in ${ms.toFixed(0)} ms = ${(ic / ms / 1000).toFixed(1)} MIPS; jit: ${jitModules} blocks, ${jitBytes} B, compile ${jitMs.toFixed(0)} ms, ${jitFail} failed ${stats}`);
process.exit(status.startsWith('exit 0') ? 0 : status.startsWith('exit') ? 3 : status.startsWith('FAULT') ? 102 : 4);
