// Record a short video WITH SOUND of a real game running in the x64 emulator (browser build, JIT on), on a GitHub runner.
//   node e2e/record.mjs <game.zip> [--tile "Name"] [--seconds 15] [--settle-frames 400] [--max-wait-ms 1500000] [--out e2e/video]
// Flow: Programs → “Open .exe / .zip…” (portable install onto C:\Program Files\<top>) → Play → wait until the game presents
// `--settle-frames` frames (title screen) → capture the screen canvas ~8–10×/s for `--seconds` seconds while every audio block
// the emulator schedules on the AudioContext is captured (hook on AudioBufferSourceNode.start) → out/frames/*.png,
// out/audio.wav, out/concat.txt (exact per-frame durations), out/title.png. ffmpeg muxes them into out/game.mp4 (workflow).
import { chromium } from 'playwright';
import { spawn } from 'node:child_process';
import { mkdirSync, writeFileSync } from 'node:fs';
import { resolve, basename } from 'node:path';

const argv = process.argv.slice(2); const file = argv[0];
if (!file) { console.error('usage: node e2e/record.mjs <game.zip> [--tile Name] [--seconds 15] [--settle-frames 400] [--max-wait-ms 1500000] [--out dir]'); process.exit(2); }
const opt = (k, d) => { const i = argv.indexOf(k); return i >= 0 ? argv[i + 1] : d; };
const tile = opt('--tile', null), seconds = Number(opt('--seconds', '15')), settleFrames = Number(opt('--settle-frames', '400')), maxWait = Number(opt('--max-wait-ms', '1500000'));
const ROOT = resolve(import.meta.dirname, '..'); const OUT = resolve(ROOT, opt('--out', 'e2e/video')); const FRAMES = resolve(OUT, 'frames');
mkdirSync(FRAMES, { recursive: true });
const PORT = 4183; const url = process.env.E2E_URL || `http://127.0.0.1:${PORT}`;
let server = null;
if (!process.env.E2E_URL) {
  server = spawn('npx', ['vite', 'preview', '--host', '127.0.0.1', '--port', String(PORT), '--strictPort'], { cwd: ROOT, stdio: 'ignore', detached: true });
  const t0 = Date.now(); while (Date.now() - t0 < 30000) { try { const r = await fetch(url); if (r.ok) break; } catch { /* wait */ } await new Promise(r => setTimeout(r, 300)); }
}
const sleep = ms => new Promise(r => setTimeout(r, ms));
const browser = await chromium.launch({ args: ['--autoplay-policy=no-user-gesture-required', '--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'] });
const ctx = await browser.newContext({ viewport: { width: 1400, height: 900 } });
// audio capture: every AudioBuffer the emulator schedules, with its start time on the context clock
await ctx.addInitScript(() => {
  const W = window;
  W.__pcm = []; W.__acs = [];
  const AC = W.AudioContext;
  W.AudioContext = class extends AC { constructor(...a) { super(...a); W.__acs.push(this); } };
  const start = AudioBufferSourceNode.prototype.start;
  AudioBufferSourceNode.prototype.start = function (when, ...rest) {
    try {
      const b = this.buffer;
      if (b) {
        const l = Float32Array.from(b.getChannelData(0)), r = Float32Array.from(b.numberOfChannels > 1 ? b.getChannelData(1) : b.getChannelData(0));
        const at = when ?? this.context.currentTime; W.__pcm.push({ when: at, rate: b.sampleRate, l, r });
        // keep only the last 90 s of context time (the game may run for many minutes before the recording starts)
        while (W.__pcm.length && W.__pcm[0].when < at - 90) W.__pcm.shift();
      }
    } catch { /* ignore */ }
    return start.call(this, when, ...rest);
  };
});
if (opt('--jit', '1') === '0') await ctx.addInitScript(() => { try { localStorage.setItem('nebula_jit', '0'); } catch { /* ignore */ } });
const page = await ctx.newPage();
const errors = []; page.on('pageerror', e => errors.push(e.message));
await page.goto(url); await page.waitForSelector('[aria-label="Start"]');
await page.locator('[data-desktop-icon="x64"]').dblclick(); await sleep(300);
const w = page.locator('[data-window][aria-label*="Programs"]');
await w.locator('[data-x64-disk]').waitFor({ timeout: 60000 });
// maximise the window so the screen canvas is as large as possible
await w.locator('[aria-label="Maximize"]').click().catch(() => {}); await sleep(300);
const status = () => page.evaluate(() => document.querySelector('[data-x64-status]')?.getAttribute('data-x64-status'));
const framesNow = () => page.evaluate(() => Number(document.querySelector('[data-x64-screen]')?.getAttribute('data-frames')) || 0);
const t0 = Date.now();
await w.locator('[data-x64-open]').setInputFiles(resolve(file));
const sel = tile ? `[data-x64-item="${tile}"]` : '[data-x64-item]';
await w.locator(sel).first().waitFor({ timeout: 900000 });
console.log(`installed in ${((Date.now() - t0) / 1000).toFixed(1)} s`);
await sleep(500);
await w.locator(sel).first().locator('button').first().click();
const tRun = Date.now();
// user gesture for the AudioContext + wait for the game loop
await page.mouse.click(700, 450).catch(() => {});
let st = 'running', fr = 0, lastLog = 0;
while (Date.now() - tRun < maxWait) {
  st = await status(); fr = await framesNow();
  if (st === 'fault' || st === 'exited') break;
  if (fr >= settleFrames) break;
  if (Date.now() - lastLog > 30000) { lastLog = Date.now(); console.log(`t=${((Date.now() - tRun) / 1000).toFixed(0)}s status=${st} frames=${fr}`); }
  if (fr > 0 && fr % 50 < 5) await page.mouse.click(700, 450).catch(() => {});
  await sleep(2000);
}
console.log(`game loop: status=${st} frames=${fr} after ${((Date.now() - tRun) / 1000).toFixed(0)} s; audio contexts=${await page.evaluate(() => window.__acs.map(a => a.state).join(','))}`);
await w.locator('[data-x64-tab="screen"]').click().catch(() => {}); await sleep(300);
await page.mouse.click(700, 450).catch(() => {}); await sleep(1500);
await page.screenshot({ path: resolve(OUT, 'title.png') });
const screen = page.locator('[data-x64-screen]');
await screen.screenshot({ path: resolve(OUT, 'title-screen.png') }).catch(() => {});
if (st === 'fault' || st === 'exited') {
  await w.locator('[data-x64-tab="report"]').click().catch(() => {}); await sleep(300);
  console.log('report:', (await w.locator('[data-x64-report]').innerText().catch(() => '')).slice(0, 2000));
}
// ---- record ----
const acT0 = await page.evaluate(() => (window.__acs[0] ? window.__acs[0].currentTime : 0));
const pcmStartIdx = await page.evaluate(() => window.__pcm.length);
await page.evaluate(() => { window.__pcmKeep = window.__pcm.length; }); // blocks before this index may be trimmed by the ring; we only read from here on
const tStart = Date.now(); const stamps = []; let n = 0;
while (Date.now() - tStart < seconds * 1000) {
  const t = Date.now() - tStart;
  const name = `f${String(n).padStart(5, '0')}.png`;
  try { await screen.screenshot({ path: resolve(FRAMES, name), timeout: 5000 }); stamps.push({ name, t }); n++; } catch { /* skip */ }
  // a little interaction so the recording is not static: move the pointer over the title
  if (n === 20) await page.mouse.move(650, 420).catch(() => {});
  if (n === 40) await page.mouse.move(760, 480).catch(() => {});
}
const acT1 = await page.evaluate(() => (window.__acs[0] ? window.__acs[0].currentTime : 0));
const wallSec = (Date.now() - tStart) / 1000;
// per-frame durations for ffmpeg's concat demuxer
let concat = 'ffconcat version 1.0\n';
for (let i = 0; i < stamps.length; i++) { const d = ((i + 1 < stamps.length ? stamps[i + 1].t : wallSec * 1000) - stamps[i].t) / 1000; concat += `file 'frames/${stamps[i].name}'\nduration ${Math.max(d, 0.01).toFixed(3)}\n`; }
if (stamps.length) concat += `file 'frames/${stamps[stamps.length - 1].name}'\n`;
writeFileSync(resolve(OUT, 'concat.txt'), concat);
// audio: blocks scheduled in [acT0, acT1] → WAV (stereo 16-bit) over exactly the recorded span
const blocks = await page.evaluate((t0) => window.__pcm.filter(b => b.when >= t0 - 0.05).map(b => ({ when: b.when, rate: b.rate, l: Array.from(b.l), r: Array.from(b.r) })), acT0);
void pcmStartIdx;
const rate = blocks[0]?.rate || 48000; const total = Math.max(1, Math.round(wallSec * rate));
const pcm = new Int16Array(total * 2);
let placed = 0;
for (const b of blocks) {
  const off = Math.round((b.when - acT0) * rate); if (off + b.l.length <= 0 || off >= total) continue;
  for (let i = 0; i < b.l.length; i++) { const k = off + i; if (k < 0 || k >= total) continue; pcm[2 * k] = Math.max(-32768, Math.min(32767, Math.round(b.l[i] * 32767))); pcm[2 * k + 1] = Math.max(-32768, Math.min(32767, Math.round(b.r[i] * 32767))); placed++; }
}
const hdr = Buffer.alloc(44); const dataLen = pcm.length * 2;
hdr.write('RIFF', 0); hdr.writeUInt32LE(36 + dataLen, 4); hdr.write('WAVE', 8); hdr.write('fmt ', 12); hdr.writeUInt32LE(16, 16); hdr.writeUInt16LE(1, 20); hdr.writeUInt16LE(2, 22);
hdr.writeUInt32LE(rate, 24); hdr.writeUInt32LE(rate * 4, 28); hdr.writeUInt16LE(4, 32); hdr.writeUInt16LE(16, 34); hdr.write('data', 36); hdr.writeUInt32LE(dataLen, 40);
writeFileSync(resolve(OUT, 'audio.wav'), Buffer.concat([hdr, Buffer.from(pcm.buffer)]));
let peak = 0; for (let i = 0; i < pcm.length; i++) { const v = Math.abs(pcm[i]); if (v > peak) peak = v; }
console.log(`recorded ${stamps.length} frames in ${wallSec.toFixed(1)} s (${(stamps.length / wallSec).toFixed(1)} fps); audio: ${blocks.length} blocks, ${placed} samples placed, rate ${rate}, peak ${(peak / 32767).toFixed(3)}, context clock ${acT0.toFixed(2)}→${acT1.toFixed(2)} s`);
console.log(`final status=${await status()} frames=${await framesNow()} errors=${errors.length}`);
// emulator console (GL / audio / API log lines) — the tail, plus every [gl] line, for diagnosing a black screen
await w.locator('[data-x64-tab="console"]').click().catch(() => {}); await sleep(300);
const con = await page.evaluate(() => document.querySelector('[data-x64-console]')?.textContent || '');
const gl = con.split('\n').filter(l => /\[gl\]|\[audio\]|GL_|wgl|fault|exception/i.test(l));
console.log(`console: ${con.length} chars; ${gl.length} gl/audio lines:\n${gl.slice(0, 80).join('\n')}\n--- console tail ---\n${con.slice(-5000)}`);
writeFileSync(resolve(OUT, 'console.txt'), con);
await browser.close();
if (server) { try { process.kill(-server.pid); } catch { /* ignore */ } }
process.exit(stamps.length > 0 ? 0 : 1);
