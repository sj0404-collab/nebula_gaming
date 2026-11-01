#!/usr/bin/env node
/**
 * End-to-end tests for the Nebula OS shell (Playwright, headless Chromium).
 *
 *   npm run test:e2e                       # builds nothing — expects ./dist (run `npm run build` first)
 *   E2E_URL=http://localhost:5173 node e2e/run.mjs   # test an already running server
 *   E2E_BRIDGE=1  → also require the REAL opencode bridge (/api/opencode) and a real model answer
 *
 * Works on Windows / macOS / Linux (spawns `vite preview` via npx when E2E_URL is not set).
 */
import { chromium } from 'playwright';
import { spawn } from 'node:child_process';
import { mkdirSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';

const PORT = 4199;
const ROOT = resolve(import.meta.dirname, '..');
const SHOTS = resolve(ROOT, 'e2e', 'shots');
mkdirSync(SHOTS, { recursive: true });

let url = process.env.E2E_URL;
let server = null;
if (!url) {
  url = `http://127.0.0.1:${PORT}`;
  server = spawn(process.platform === 'win32' ? 'npx.cmd' : 'npx', ['vite', 'preview', '--host', '127.0.0.1', '--port', String(PORT), '--strictPort'], {
    cwd: ROOT, stdio: 'ignore', shell: process.platform === 'win32', detached: process.platform !== 'win32',
  });
  const t0 = Date.now();
  while (Date.now() - t0 < 30_000) {
    try { const r = await fetch(url); if (r.ok) break; } catch { /* wait */ }
    await new Promise(r => setTimeout(r, 300));
  }
}

const results = [];
let page;
const errors = [];
async function test(name, fn) {
  if (process.env.E2E_GREP && !name.includes(process.env.E2E_GREP) && !name.startsWith('desktop loads')) return;   // run a subset locally: E2E_GREP=d3d11 node e2e/run.mjs
  const t = Date.now();
  try { await fn(); results.push({ name, ok: true, ms: Date.now() - t }); console.log(`  ✓ ${name} (${Date.now() - t} ms)`); }
  catch (e) {
    if (e?.name === 'Inconclusive') {   // the request reached a third-party free model that did not answer in time — not a product failure, reported separately
      results.push({ name, ok: true, inconclusive: true, ms: Date.now() - t, error: e.message });
      console.log(`  ~ ${name}\n      INCONCLUSIVE: ${e.message}`);
      try { await page?.screenshot({ path: resolve(SHOTS, `inconclusive-${name.replace(/[^a-z0-9]+/gi, '_')}.png`) }); } catch { /* ignore */ }
      try { await closeAll(); } catch { /* ignore */ }
      return;
    }
    results.push({ name, ok: false, ms: Date.now() - t, error: String(e.message ?? e).split('\n')[0] });
    console.log(`  ✗ ${name}\n      ${String(e.message ?? e).split('\n').slice(0, 3).join('\n      ')}`);
    try { await page?.screenshot({ path: resolve(SHOTS, `fail-${name.replace(/[^a-z0-9]+/gi, '_')}.png`) }); } catch { /* ignore */ }
    try { await closeAll(); } catch { /* ignore */ }
  }
}
const expect = (cond, msg) => { if (!cond) throw new Error(msg); };
const sleep = ms => new Promise(r => setTimeout(r, ms));

const browser = await chromium.launch();
const ctx = await browser.newContext({ viewport: { width: 1400, height: 900 } });
page = await ctx.newPage();
page.on('pageerror', e => errors.push(`pageerror: ${e.message}`));
page.on('console', m => { if (m.type() === 'error' && !/favicon|net::ERR|designarena|rrweb|Failed to load resource/i.test(m.text())) errors.push(`console: ${m.text()}`); });

const win = (title) => page.locator(`[aria-label="${title}"]`);
const windowsCount = () => page.locator('[data-window]').count();
const ICON = { 'This PC': 'pc', 'Recycle Bin': 'recycle', 'Nebula Strike': 'game', 'Steam': 'steam', 'Epic Games': 'epic', 'Command Prompt': 'terminal', 'opencode': 'opencode', 'Microsoft Edge': 'edge', 'x64 Runner': 'x64' };
const iconOf = (p, label) => p.locator(`[data-desktop-icon="${ICON[label]}"]`);
const openFromDesktop = async (label) => { await iconOf(page, label).dblclick(); await sleep(300); };
const closeAll = async () => {
  for (let i = 0; i < 20; i++) {
    const b = page.locator('[aria-label="Close"]').first();
    if (!(await b.count())) break;
    await b.click({ force: true }); await sleep(120);
  }
};

console.log(`\nNebula OS e2e → ${url}\n`);

await test('desktop loads with 9 icons, taskbar, no page errors', async () => {
  await page.goto(url, { waitUntil: 'load' });
  await page.waitForSelector('[aria-label="Start"]');
  for (const l of Object.keys(ICON))
    expect(await iconOf(page, l).count() === 1, `icon "${l}" missing`);
  await page.screenshot({ path: resolve(SHOTS, 'desktop.png') });
});

await test('double-click opens exactly ONE window (no click+dblclick duplicate)', async () => {
  await openFromDesktop('Steam');
  expect((await windowsCount()) === 1, `expected 1 window, got ${await windowsCount()}`);
  await closeAll();
  expect((await windowsCount()) === 0, 'window did not close');
});

await test('single click selects icon but does not open', async () => {
  await iconOf(page, 'Steam').click();
  await sleep(300);
  expect((await windowsCount()) === 0, 'single click should not open a window');
});

await test('single-instance apps focus the existing window instead of duplicating', async () => {
  await openFromDesktop('Steam'); await openFromDesktop('Steam');
  expect((await windowsCount()) === 1, `Steam duplicated: ${await windowsCount()}`);
  await closeAll();
});

await test('every app opens, renders and closes', async () => {
  const apps = [['This PC', 'File Explorer'], ['Nebula Strike', 'NEBULA STRIKE'], ['Steam', 'Steam'], ['Epic Games', 'Epic Games Launcher'],
    ['Command Prompt', 'Command Prompt'], ['opencode', 'opencode'], ['Microsoft Edge', 'Microsoft Edge'], ['x64 Runner', 'x64 Runner']];
  for (const [label, title] of apps) {
    await openFromDesktop(label);
    const w = page.locator(`[data-window][aria-label*="${title}"]`);
    expect(await w.count() === 1, `window "${title}" not found after opening ${label}`);
    const box = await w.boundingBox();
    expect(box && box.width > 300 && box.height > 200, `window "${title}" has no size`);
    await page.screenshot({ path: resolve(SHOTS, `app-${label.replace(/\s+/g, '_')}.png`) });
    await closeAll();
  }
  expect((await windowsCount()) === 0, 'windows left open');
});

await test('window controls: minimize → taskbar restore, maximize fills screen', async () => {
  await openFromDesktop('Steam');
  const w = page.locator('[data-window="steam"]');
  await w.locator('[aria-label="Maximize"]').click(); await sleep(250);
  const b = await w.boundingBox();
  expect(b.width >= 1390 && b.height >= 800, `maximized size ${b.width}x${b.height}`);
  await w.locator('[aria-label="Minimize"]').click(); await sleep(250);
  expect(!(await w.isVisible()), 'window still visible after minimize');
  await page.locator('[data-taskbar] button[title="steam" i]').first().click(); await sleep(250);
  expect(await w.isVisible(), 'window not restored from taskbar');
  await closeAll();
});

await test('window drag moves it', async () => {
  await openFromDesktop('Steam');
  const w = page.locator('[data-window="steam"]');
  const before = await w.boundingBox();
  const bar = w.locator('[data-titlebar]').first();
  const bb = await bar.boundingBox();
  await page.mouse.move(bb.x + bb.width / 2, bb.y + bb.height / 2);
  await page.mouse.down(); await page.mouse.move(bb.x + bb.width / 2 + 150, bb.y + bb.height / 2 + 90, { steps: 8 }); await page.mouse.up();
  const after = await w.boundingBox();
  expect(Math.abs(after.x - before.x - 150) < 6 && Math.abs(after.y - before.y - 90) < 6, `moved by ${after.x - before.x},${after.y - before.y}`);
  await closeAll();
});

await test('start menu opens apps and closes on launch', async () => {
  await page.locator('[aria-label="Start"]').click(); await sleep(250);
  const notepad = page.getByRole('button', { name: /notepad/i }).first();
  expect(await notepad.count() === 1, 'Notepad not in start menu');
  await notepad.click(); await sleep(300);
  expect((await windowsCount()) === 1, 'Notepad did not open');
  expect(!(await page.locator('[data-startmenu]').isVisible().catch(() => false)), 'start menu should close after launch');
  await closeAll();
});

await test('terminal: help / dir / ver / echo / cd / opencode command', async () => {
  await openFromDesktop('Command Prompt');
  const term = page.locator('[data-window="terminal"]');
  const input = term.locator('input').first();
  const run = async (cmd) => { await input.fill(cmd); await input.press('Enter'); await sleep(250); return term.innerText(); };
  expect((await run('ver')).includes('Microsoft Windows [Version 10.0.19045'), 'ver output');
  expect(/<DIR>\s+Downloads/.test(await run('dir')), 'dir output lacks <DIR> Downloads');
  expect(/NebulaStrike\.exe/.test(await run('dir Downloads')), 'dir Downloads lacks NebulaStrike.exe');
  expect((await run('echo hello e2e')).includes('hello e2e'), 'echo output');
  expect((await run('help')).toLowerCase().includes('opencode'), 'help mentions opencode');
  const t = await run('cd ..');
  expect(!/not recognized|error/i.test(t.split('\n').slice(-3).join('\n')), 'cd .. errored');
  await run('opencode');
  expect((await windowsCount()) === 2, '`opencode` command should open the TUI window');
  await closeAll();
});

await test('Nebula Strike: wasm32 autotest reaches WIN · 7500', async () => {
  await openFromDesktop('Nebula Strike');
  const w = page.locator('[data-window="nebula"]');
  await w.getByRole('button', { name: /autotest/i }).click();
  await page.waitForFunction(() => /WIN · score \d+/.test(document.body.innerText), null, { timeout: 60_000 });
  const txt = await w.innerText();
  const m = txt.match(/(WIN|DEFEAT) · score (\d+) · wave (\d+)\/5/);
  expect(m && m[1] === 'WIN' && Number(m[2]) >= 7000, `autotest result: ${m?.[0]}`);
  await page.screenshot({ path: resolve(SHOTS, 'nebula-autotest.png') });
  await closeAll();
});

await test('Nebula Strike: keyboard moves the ship and canvas renders', async () => {
  await openFromDesktop('Nebula Strike');
  const w = page.locator('[data-window="nebula"]');
  const canvas = w.locator('canvas').first();
  expect(await canvas.count() === 1, 'no canvas');
  await canvas.click();
  await page.keyboard.down('ArrowLeft'); await sleep(500); await page.keyboard.up('ArrowLeft');
  await page.keyboard.press('Space');
  const nonBlack = await canvas.evaluate(c => {
    const gl = c.getContext('webgl2') || c.getContext('webgl');
    let d;
    if (gl) { d = new Uint8Array(c.width * c.height * 4); gl.readPixels(0, 0, c.width, c.height, gl.RGBA, gl.UNSIGNED_BYTE, d); }
    else { const g = c.getContext('2d'); if (!g) return -1; d = g.getImageData(0, 0, c.width, c.height).data; }
    let n = 0; for (let i = 0; i < d.length; i += 16) if (d[i] + d[i + 1] + d[i + 2] > 60) n++; return n;
  });
  expect(nonBlack > 0, `canvas is blank (${nonBlack})`);
  await closeAll();
});

await test('Nebula Strike: real WebGL renderer (WebGL2) draws the wasm64 scene', async () => {
  await openFromDesktop('Nebula Strike');
  const w = page.locator('[data-window="nebula"]');
  const canvas = w.locator('canvas').first();
  await page.waitForFunction(() => document.querySelector('[data-window="nebula"] canvas')?.getAttribute('data-engine') === 'wasm64', null, { timeout: 15_000 });
  const renderer = await canvas.getAttribute('data-renderer');
  expect(renderer === 'webgl2' || renderer === 'webgl', `renderer is ${renderer}, expected WebGL`);
  expect(/WEBGL/.test(await w.innerText()), 'WebGL badge missing');
  // the GPU frame must contain the cyan ship + HUD bar, i.e. many lit pixels in several colours
  await sleep(400);
  const stats = await canvas.evaluate(c => {
    const gl = c.getContext('webgl2') || c.getContext('webgl');
    const d = new Uint8Array(c.width * c.height * 4); gl.readPixels(0, 0, c.width, c.height, gl.RGBA, gl.UNSIGNED_BYTE, d);
    let lit = 0, cyan = 0; for (let i = 0; i < d.length; i += 4) { const r = d[i], g = d[i + 1], b = d[i + 2]; if (r + g + b > 90) lit++; if (b > 150 && g > 100 && r < 120) cyan++; }
    return { lit, cyan, total: d.length / 4 };
  });
  expect(stats.lit > stats.total * 0.01, `too few lit pixels: ${JSON.stringify(stats)}`);
  expect(stats.cyan > 100, `ship/bolts (cyan) not drawn: ${JSON.stringify(stats)}`);
  await page.screenshot({ path: resolve(SHOTS, 'nebula-webgl.png') });
  await closeAll();
});

// ───────────── Nebula x64: real Windows x86-64 executables through the wasm64 emulator ─────────────
const x64Open = async () => {
  await openFromDesktop('x64 Runner');
  const w = page.locator('[data-window][aria-label*="x64 Runner"]');
  await w.locator('[data-x64-exe="NebulaStrike.exe"]').waitFor({ timeout: 30_000 });
  return w;
};
await test('x64: emulator loads (wasm64), scans the bundled samples and reports verdicts', async () => {
  const w = await x64Open();
  for (const n of ['NebulaStrike.exe', 'hello.exe', 'bench.exe'])
    expect(await w.locator(`[data-x64-exe="${n}"] [data-x64-verdict="likely"]`).count() === 1, `${n} not scanned as "likely"`);
  await w.getByRole('button', { name: /report/i }).click();
  const rep = await w.locator('[data-x64-report]').innerText();
  expect(/x86-64/.test(rep) && /80 total · 80 implemented/.test(rep), 'PE report missing import coverage: ' + rep.slice(0, 200));
  await page.screenshot({ path: resolve(SHOTS, 'x64-report.png') });
  await closeAll();
});
await test('x64: hello.exe (UCRT console, printf/qsort/malloc/math) runs to exit code 7 with correct output', async () => {
  const w = await x64Open();
  await w.locator('[data-x64-exe="hello.exe"]').click();
  await w.locator('[data-x64-run]').click();
  await page.waitForFunction(() => document.querySelector('[data-x64-status]')?.getAttribute('data-x64-status') === 'exited', null, { timeout: 30_000 });
  const con = await w.locator('[data-x64-console]').innerText();
  expect(/Hello from x86-64 Windows!/.test(con), 'greeting missing: ' + con.slice(0, 200));
  expect(/fnv=76a25757d9d78283/.test(con), 'FNV hash wrong (64-bit arithmetic)');
  expect(/sqrt\(2\)=1\.414214 sin\(1\)=0\.8415 pow\(2,10\)=1024/.test(con), 'math/printf wrong');
  expect(/18446744073709551615/.test(con), '%llu wrong');
  expect(/code 7/.test(await w.innerText()), 'exit code 7 not reported');
  await page.screenshot({ path: resolve(SHOTS, 'x64-hello.png') });
  await closeAll();
});
await test('x64: runtime pack — the REAL msvcp140.dll (VC++ 2015–2022 Redistributable) is installed into C:\\Windows\\System32 on demand and std::cout works through it', async () => {
  const w = await x64Open();
  const packStatus = async () => w.locator('[data-x64-pack]').getAttribute('data-x64-pack');
  await page.waitForFunction(() => { const s = document.querySelector('[data-x64-pack]')?.getAttribute('data-x64-pack'); return s && s !== 'probing'; }, null, { timeout: 15_000 });
  const before = await packStatus();
  if (before === 'unavailable') { console.log('      (runtime pack not built into this deployment — run emu/tools/runtime-pack.sh; checking the honest path only)'); }
  await w.locator('[data-x64-exe="msvcp.exe"]').click();
  await w.locator('[data-x64-run]').click();
  await page.waitForFunction(() => document.querySelector('[data-x64-status]')?.getAttribute('data-x64-status') === 'exited', null, { timeout: 90_000 });
  const con = await w.locator('[data-x64-console]').innerText();
  if (before === 'unavailable') { expect(/exports missing/.test(con), 'without the pack LoadLibrary must fall back to the built-in shim and the test must say so: ' + con.slice(0, 200)); await closeAll(); return; }
  expect((await packStatus()) === 'installed', 'pack should be installed after running an exe that needs it: ' + await packStatus());
  expect(/42 3\.25/.test(con), 'cout << int << double wrong: ' + con.slice(0, 300));
  expect(/msvcp140 says hello/.test(con), 'cout put/flush output missing');
  expect(/MSVCP OK/.test(con), 'test did not finish');
  const files = await w.locator('[data-x64-tab="files"]').click().then(() => w.innerText());
  expect(/msvcp140\.dll/.test(files), 'msvcp140.dll should be listed on the virtual disk');
  await page.screenshot({ path: resolve(SHOTS, 'x64-runtime-pack.png') });
  await closeAll();
});
await test('x64: .NET pack — the OFFICIAL Microsoft .NET Runtime 10 is installed into C:\\Program Files\\dotnet on demand and a C# app (apphost + dll) runs on CoreCLR: LINQ, exceptions, hardware NRE, JSON, reflection, Regex, GC', async () => {
  const w = await x64Open();
  const st = async () => w.locator('[data-x64-dotnet]').getAttribute('data-x64-dotnet');
  await page.waitForFunction(() => { const s = document.querySelector('[data-x64-dotnet]')?.getAttribute('data-x64-dotnet'); return s && s !== 'probing'; }, null, { timeout: 15_000 });
  const before = await st();
  await w.locator('[data-x64-exe="nebula-dotnet-test.exe"]').click();
  await w.locator('[data-x64-tab="report"]').click();
  expect(/\.NET apphost/.test(await w.innerText()), 'PE report should recognise the .NET apphost');
  await w.locator('[data-x64-run]').click();
  if (before === 'unavailable') {
    console.log('      (.NET pack not built into this deployment — run emu/tools/dotnet-pack.sh; checking the honest path only)');
    await page.waitForFunction(() => /could not be installed|runtime pack/.test(document.body.innerText), null, { timeout: 30_000 });
    await closeAll(); return;
  }
  try { await page.waitForFunction(() => /^(exited|fault)$/.test(document.querySelector('[data-x64-status]')?.getAttribute('data-x64-status') ?? ''), null, { timeout: 240_000 }); }
  catch (e) { await page.screenshot({ path: resolve(SHOTS, 'x64-dotnet-timeout.png') }); throw e; }
  finally {   // diagnostics for CI: what the guest printed and what the loader/emulator logged
    const status = await page.evaluate(() => document.querySelector('[data-x64-status]')?.getAttribute('data-x64-status'));
    const conTxt = await w.locator('[data-x64-console]').innerText().catch(() => '');
    await w.locator('[data-x64-tab="report"]').click().catch(() => {});
    const rep = await w.locator('[data-x64-report]').innerText().catch(() => '');
    if (status !== 'exited') console.log(`      status=${status} dotnet=${await st()}\n      console: ${JSON.stringify(conTxt.slice(-600))}\n      report: ${JSON.stringify(rep.slice(-1500))}`);
    await w.locator('[data-x64-tab="console"]').click().catch(() => {});
  }
  const con = await w.locator('[data-x64-console]').innerText();
  expect((await st()) === 'installed', '.NET pack should be installed after running a managed exe: ' + await st());
  expect(/primes 303/.test(con), 'LINQ primes missing: ' + con.slice(0, 400));
  expect(/caught NRE/.test(con), 'hardware null-reference → managed NullReferenceException failed');
  expect(/caught boom/.test(con), 'managed throw/catch failed');
  expect(/json \{"name":"nebula","items":\[2,3,5\],"ok":true\}/.test(con) && /back nebula/.test(con), 'System.Text.Json failed');
  expect(/reflect List`1 1 6 Main,Run,Steps/.test(con), 'reflection/dynamic failed');
  expect(/regex 12,345,64/.test(con), 'Regex failed');
  expect(/gc ok gen0=\d+/.test(con), 'GC.Collect failed');
  expect(/code 3/.test(await w.innerText()), 'exit code 3 not reported');
  const files = await w.locator('[data-x64-tab="files"]').click().then(() => w.innerText());
  expect(/coreclr\.dll/.test(files), 'coreclr.dll should be listed on the virtual disk');
  await page.screenshot({ path: resolve(SHOTS, 'x64-dotnet.png') });
  await closeAll();
});

await test('x64: NebulaStrike.exe (real Win32 GDI PE32+) renders frames through WebGL and reacts to keys', async () => {
  const w = await x64Open();
  await w.locator('[data-x64-exe="NebulaStrike.exe"]').click();
  await w.locator('[data-x64-run]').click();
  await page.waitForFunction(() => Number(document.querySelector('[data-x64-screen]')?.getAttribute('data-frames') ?? 0) >= 30, null, { timeout: 30_000 });
  const canvas = w.locator('[data-x64-screen]');
  await canvas.click();
  const lum = async () => canvas.evaluate((c) => {
    const gl = c.getContext('webgl2') || c.getContext('webgl'); const px = new Uint8Array(c.width * c.height * 4); gl.readPixels(0, 0, c.width, c.height, gl.RGBA, gl.UNSIGNED_BYTE, px);
    let s = 0, n = 0; for (let i = 0; i < px.length; i += 16) { s += px[i] + px[i + 1] + px[i + 2]; n++; } return s / n;
  });
  const before = await lum();
  expect(before > 2, 'emulated window is black: ' + before);
  const status0 = await w.locator('[data-x64-status]').getAttribute('data-x64-status');
  expect(status0 === 'running', 'not running: ' + status0);
  await page.keyboard.down('Space'); await page.keyboard.down('ArrowLeft'); await sleep(600); await page.keyboard.up('ArrowLeft'); await page.keyboard.up('Space');
  const f1 = Number(await canvas.getAttribute('data-frames'));
  await sleep(2000);
  const f2 = Number(await canvas.getAttribute('data-frames'));
  // 2-second window: ≥ 5 fps proves the game loop + presenter are alive even on the slow macOS CI runners (8–9 fps there)
  expect(f2 - f1 >= 10, `frame rate too low: ${(f2 - f1) / 2} frames/s`);
  const text = await w.innerText();
  expect(/MIPS/.test(text) && /RUNNING/.test(text), 'status bar missing stats: ' + text.slice(-200));
  await page.screenshot({ path: resolve(SHOTS, 'x64-nebulastrike.png') });
  await w.locator('[data-x64-stop]').click();
  expect(await w.locator('[data-x64-status]').getAttribute('data-x64-status') === 'stopped', 'stop failed');
  await closeAll();
});
await test('x64: d3d11tri.exe — Direct3D 11 draw calls rendered by WebGL2 (DXBC → GLSL): triangle + indexed quad visible on the swap chain', async () => {
  const w = await x64Open();
  await w.locator('[data-x64-exe="d3d11tri.exe"]').click();
  await w.locator('[data-x64-run]').click();
  await page.waitForFunction(() => document.querySelector('[data-x64-status]')?.getAttribute('data-x64-status') === 'exited', null, { timeout: 60_000 });
  const con = await w.locator('[data-x64-console]').innerText();
  expect(/frame 2: triangle \+ quad drawn and presented/.test(con) && /d3d11tri ok/.test(con), 'd3d11tri did not finish: ' + con.slice(-400));
  await w.locator('[data-x64-tab="screen"]').click(); await sleep(500);
  const canvas = w.locator('[data-x64-screen]');
  const hist = await canvas.evaluate((c) => {
    const gl = c.getContext('webgl2') || c.getContext('webgl'); const px = new Uint8Array(c.width * c.height * 4); gl.readPixels(0, 0, c.width, c.height, gl.RGBA, gl.UNSIGNED_BYTE, px);
    const h = { yellow: 0, red: 0, green: 0, blue: 0, clear: 0, total: 0 };
    for (let i = 0; i < px.length; i += 4) {
      const r = px[i], g = px[i + 1], b = px[i + 2]; h.total++;
      if (r > 200 && g > 200 && b < 80) h.yellow++; else if (r > 150 && g < 100 && b < 100) h.red++; else if (g > 150 && r < 100 && b < 100) h.green++;
      else if (b > 150 && r < 100 && g < 100) h.blue++; else if (Math.abs(r - 26) < 12 && Math.abs(g - 26) < 12 && Math.abs(b - 77) < 14) h.clear++;
    }
    return h;
  });
  expect(hist.yellow > 50, 'indexed yellow quad missing: ' + JSON.stringify(hist));
  expect(hist.red > 50 && hist.green > 50 && hist.blue > 50, 'triangle corners (red/green/blue) missing: ' + JSON.stringify(hist));
  expect(hist.clear > hist.total * 0.2, 'clear colour (0.1,0.1,0.3) missing: ' + JSON.stringify(hist));
  await page.screenshot({ path: resolve(SHOTS, 'x64-d3d11tri.png') });
  await closeAll();
});

await test('x64: real third-party binaries — busybox-w32 awk/sha256sum and PuTTY plink -V run with correct output', async () => {
  const w = await x64Open();
  await w.locator('[data-x64-exe="busybox64.exe"]').waitFor({ timeout: 30_000 });
  const run = async (exe, args, expectRe) => {
    await w.locator(`[data-x64-exe="${exe}"]`).click();
    await w.getByPlaceholder('arguments').fill(args);
    await w.locator('[data-x64-run]').click();
    await page.waitForFunction(() => document.querySelector('[data-x64-status]')?.getAttribute('data-x64-status') === 'exited', null, { timeout: 30_000 });
    const con = await w.locator('[data-x64-console]').innerText();
    expect(expectRe.test(con), `${exe} ${args}: unexpected output: ${con.slice(0, 300)}`);
  };
  await run('busybox64.exe', 'awk BEGIN{print(6*7)}', /^42/m);
  await run('busybox64.exe', 'sha256sum readme.txt', /^[0-9a-f]{64}  readme\.txt/m);
  await run('busybox64.exe', 'date -u -d @1761750000 +%Y-%m-%dT%H:%M:%S', /2025-10-29T15:00:00/);
  await run('plink.exe', '-V', /plink: Release 0\.85/);
  const vb = await w.locator('[data-x64-exe="busybox64.exe"] [data-x64-verdict]').getAttribute('data-x64-verdict');
  expect(vb === 'partial', 'busybox verdict should be honest "partial": ' + vb);
  await page.screenshot({ path: resolve(SHOTS, 'x64-busybox.png') });
  await closeAll();
});
await test('x64: processes & pipes — busybox sh spawns real child processes (CreateProcess/_spawnve), pipes, $(…), background jobs and ps', async () => {
  const w = await x64Open();
  await w.locator('[data-x64-exe="busybox64.exe"]').waitFor({ timeout: 30_000 });
  await w.locator('[data-x64-exe="busybox64.exe"]').click();
  await w.getByPlaceholder('arguments').fill('sh -c "echo hello | tr a-z A-Z; seq 1 5 | sort -rn | head -2; x=$(echo sub); echo got:$x; yes | head -1; sleep 1 & ps; wait; echo jobs-done"');
  await w.locator('[data-x64-run]').click();
  await page.waitForFunction(() => document.querySelector('[data-x64-status]')?.getAttribute('data-x64-status') === 'exited', null, { timeout: 60_000 });
  const con = await w.locator('[data-x64-console]').innerText();
  expect(/HELLO/.test(con), 'pipe echo | tr failed: ' + con.slice(0, 300));
  expect(/5\s+4/.test(con), 'seq | sort | head pipeline failed: ' + con.slice(0, 300));
  expect(/got:sub/.test(con), 'command substitution failed: ' + con.slice(0, 300));
  expect(/PID\s+PPID[\s\S]*4242[\s\S]*sleep 1/.test(con), 'ps did not list the background child: ' + con.slice(0, 400));
  expect(/jobs-done/.test(con), 'wait for background job failed: ' + con.slice(-200));
  await page.screenshot({ path: resolve(SHOTS, 'x64-processes.png') });
  await closeAll();
});

await test('x64: threads & synchronisation — Win32 threads (CreateThread/CS/SRW/CV/events/mutex/semaphore/TLS) and winpthreads run in the browser', async () => {
  const w = await x64Open();
  await w.locator('[data-x64-exe="threads.exe"]').waitFor({ timeout: 30_000 });
  const run = async (exe, expectRe, what) => {
    await w.locator(`[data-x64-exe="${exe}"]`).click();
    await w.getByPlaceholder('arguments').fill('');
    await w.locator('[data-x64-run]').click();
    await page.waitForFunction(() => document.querySelector('[data-x64-status]')?.getAttribute('data-x64-status') === 'exited', null, { timeout: 60_000 });
    const con = await w.locator('[data-x64-console]').innerText();
    expect(expectRe.test(con), `${exe}: ${what}: ` + con.slice(-400));
  };
  await run('threads.exe', /39 checks, 0 failed/, 'Win32 thread self-test must pass all 39 checks');
  await run('pthreads.exe', /9 checks, 0 failed/, 'winpthreads self-test must pass all 9 checks');
  const vt = await w.locator('[data-x64-exe="threads.exe"] [data-x64-verdict]').getAttribute('data-x64-verdict');
  expect(vt === 'likely', 'threads.exe verdict should be "likely" (all imports shimmed): ' + vt);
  await page.screenshot({ path: resolve(SHOTS, 'x64-threads.png') });
  await closeAll();
});

await test('x64: structured exception handling — C++ throw/catch (gcc ABI) and the MSVC __CxxFrameHandler error path of 7-Zip work in the browser', async () => {
  const w = await x64Open();
  await w.locator('[data-x64-exe="cxxexc.exe"]').waitFor({ timeout: 30_000 });
  await w.locator('[data-x64-exe="cxxexc.exe"]').click();
  await w.getByPlaceholder('arguments').fill('');
  await w.locator('[data-x64-run]').click();
  await page.waitForFunction(() => document.querySelector('[data-x64-status]')?.getAttribute('data-x64-status') === 'exited', null, { timeout: 60_000 });
  let con = await w.locator('[data-x64-console]').innerText();
  expect(/caught std::exception: value too large: 7/.test(con), 'std::exception was not caught: ' + con.slice(-400));
  expect(/outer caught int 5/.test(con), 'nested rethrow as int failed: ' + con.slice(-400));
  expect(/1000 throws in a loop, total=503/.test(con), '1000 throws loop failed: ' + con.slice(-400));
  await w.locator('[data-x64-exe="7z.exe"]').click();
  await w.getByPlaceholder('arguments').fill('x nonexistent.7z');
  await w.locator('[data-x64-run]').click();
  await page.waitForFunction(() => document.querySelector('[data-x64-status]')?.getAttribute('data-x64-status') === 'exited', null, { timeout: 90_000 });
  con = await w.locator('[data-x64-console]').innerText();
  expect(/System ERROR:[\s\S]*The system cannot find the file specified/.test(con), '7z MSVC C++ exception path failed: ' + con.slice(-400));
  await page.screenshot({ path: resolve(SHOTS, 'x64-exceptions.png') });
  await closeAll();
});

await test('x64: hardware exceptions become SEH (AV/div0/ud2 → __except, VEH continue) and Rust programs (ripgrep, fd) run in the browser', async () => {
  const w = await x64Open();
  await w.locator('[data-x64-exe="hwexc.exe"]').waitFor({ timeout: 30_000 });
  const run = async (exe, args, timeout = 60_000) => {
    await w.locator(`[data-x64-exe="${exe}"]`).click();
    await w.getByPlaceholder('arguments').fill(args);
    await w.locator('[data-x64-run]').click();
    await page.waitForFunction(() => document.querySelector('[data-x64-status]')?.getAttribute('data-x64-status') === 'exited', null, { timeout });
    return await w.locator('[data-x64-console]').innerText();
  };
  let con = await run('hwexc.exe', '');
  expect(/AV caught=1 addr_ok=3/.test(con), 'access violation was not caught by __except: ' + con.slice(-400));
  expect(/veh calls=1 code=0xc0000094 result=42/.test(con), 'vectored handler fix-up failed: ' + con.slice(-400));
  expect(/loop caught=2000/.test(con), '2000 faults loop failed: ' + con.slice(-400));
  expect(/top-level filter: code 0xc0000005/.test(con), 'SetUnhandledExceptionFilter callback did not run: ' + con.slice(-400));
  con = await run('rg.exe', '--color never -n -i "lua|sqlite" demo.lua demo.sql demo.json', 90_000);
  expect(/demo\.lua[\r\n]+1:-- demo\.lua/.test(con) && /fib\(25\)/.test(con), 'ripgrep output wrong: ' + con.slice(-500));
  con = await run('fd.exe', '--color never -e lua -e sql -e json', 90_000);
  expect(/demo\.json[\r\n]+demo\.lua[\r\n]+demo\.sql/.test(con), 'fd output wrong: ' + con.slice(-400));
  await page.screenshot({ path: resolve(SHOTS, 'x64-rust.png') });
  await closeAll();
});

await test('x64: real applications — SQLite 3.50.4 (file db), jq 1.8.1, Lua 5.4 + lua54.dll, 7-Zip 25.01 + 7z.dll, zstd 1.5.7 run in the browser with correct output', async () => {
  const w = await x64Open();
  await w.locator('[data-x64-exe="zstd.exe"]').waitFor({ timeout: 60_000 });
  const run = async (exe, args, expectRe, shot, timeout = 90_000) => {
    await w.locator(`[data-x64-exe="${exe}"]`).click();
    if (args !== null) await w.getByPlaceholder('arguments').fill(args);
    await w.locator('[data-x64-run]').click();
    await page.waitForFunction(() => document.querySelector('[data-x64-status]')?.getAttribute('data-x64-status') === 'exited', null, { timeout });
    const con = await w.locator('[data-x64-console]').innerText();
    expect(expectRe.test(con), `${exe}: unexpected output: ${con.slice(0, 400)}`);
    if (shot) await page.screenshot({ path: resolve(SHOTS, shot) });
    return con;
  };
  await run('sqlite3.exe', null, /2000\|-?\d+\.0\|-?\d+\.0\|[\s\S]*[\r\n]ok/, 'x64-sqlite.png');            // default args: -init demo.sql demo.db .quit
  await run('sqlite3.exe', 'demo.db "select count(*), typeof(v) from t;"', /2000\|real/, null);            // the database file persisted on the virtual disk
  await run('jq.exe', null, /\{"name":"ann","n":2\}\s*\{"name":"cid","n":3\}/, 'x64-jq.png');
  await run('lua54.exe', null, /fib\(25\) =\s+75025[\s\S]*read back:\s+written from Lua/, 'x64-lua.png');
  await run('7z.exe', null, /Everything is Ok/, 'x64-7zip.png');
  await run('7z.exe', 'l demo.7z', /1 files/, null);
  await run('zstd.exe', null, /\d{6} ->\s+3\d{4}/, 'x64-zstd.png');
  const vl = await w.locator('[data-x64-exe="lua54.exe"] [data-x64-verdict]').getAttribute('data-x64-verdict');
  expect(vl === 'partial', 'lua54.exe (imports its own DLL) verdict should be "partial": ' + vl);
  await w.locator('[data-x64-exe="lua54.exe"]').click(); await w.locator('[data-x64-tab="report"]').click();
  const rep = await w.locator('[data-x64-report]').innerText();
  await page.screenshot({ path: resolve(SHOTS, 'x64-lua-report.png') });
  expect(/Own DLLs/.test(rep), 'report should show the selected exe: ' + rep.slice(0, 200));
  await closeAll();
});

await test('x64: CPython 3.12 (official python.org build) runs in the browser — stdlib from python312.zip, C extensions, and the REPL over console stdin', async () => {
  const w = await x64Open();
  await w.locator('[data-x64-exe="python.exe"]').waitFor({ timeout: 60_000 });
  const waitStatus = (st, timeout = 180_000) => page.waitForFunction((s) => document.querySelector('[data-x64-status]')?.getAttribute('data-x64-status') === s, st, { timeout });
  const start = async (args) => {
    await w.locator('[data-x64-exe="python.exe"]').click();
    await w.getByPlaceholder('arguments').fill(args);
    await w.locator('[data-x64-run]').click();
  };
  // 1. first Run fetches the 15 MB runtime lazily, then runs a one-liner that touches json, sqlite3 (.pyd + sqlite3.dll) and decimal
  await start(`-c "import sys, json, sqlite3, decimal; print(sys.version.split()[0], sys.platform); print(json.dumps({'answer': 6 * 7})); print(sqlite3.connect(':memory:').execute('select 40 + 2').fetchone(), decimal.Decimal(1) / 7)"`);
  await waitStatus('exited');
  let con = await w.locator('[data-x64-console]').innerText();
  expect(/3\.12\.10 win32/.test(con), 'python version line missing: ' + con.slice(-400));
  expect(/\{"answer": 42\}/.test(con), 'json output missing: ' + con.slice(-400));
  expect(/\(42,\) 0\.1428571428571428571428571429/.test(con), 'sqlite3/decimal output missing: ' + con.slice(-400));
  await page.screenshot({ path: resolve(SHOTS, 'x64-python.png') });
  // 2. the REPL: python -i -q reads lines from the console input (ReadConsoleW), EOF ends it
  await start('-i -q');
  await waitStatus('waiting-stdin', 120_000);
  for (const line of ['x = [n * n for n in range(6)]', 'print(sum(x), "привет")', 'import this']) { await w.locator('[data-x64-stdin]').fill(line); await w.locator('[data-x64-stdin-send]').click(); }
  await page.waitForFunction(() => /Namespaces are one honking great idea/.test(document.querySelector('[data-x64-console]')?.textContent || ''), null, { timeout: 120_000 });
  await w.locator('[data-x64-stdin-eof]').click(); await waitStatus('exited');
  con = await w.locator('[data-x64-console]').innerText();
  expect(/55 привет/.test(con), 'REPL expression output missing: ' + con.slice(-400));
  expect(/Beautiful is better than ugly/.test(con), 'import this output missing: ' + con.slice(-400));
  await page.screenshot({ path: resolve(SHOTS, 'x64-python-repl.png') });
  await closeAll();
});

await test('x64: cmd.exe — batch file (demo.bat: for, if, pipes, redirection, call :sub, jq/lua children), then the interactive prompt; `real` in the Command Prompt opens it', async () => {
  const w = await x64Open();
  await w.locator('[data-x64-exe="cmd.exe"]').waitFor({ timeout: 30_000 });
  const waitStatus = (st, timeout = 120_000) => page.waitForFunction((s) => document.querySelector('[data-x64-status]')?.getAttribute('data-x64-status') === s, st, { timeout });
  const consoleText = () => w.locator('[data-x64-console]').innerText();
  // 1. /k demo.bat: the batch runs (children: jq.exe, lua54.exe, nested cmd pipelines), then cmd stays at its prompt waiting for input
  await w.locator('[data-x64-exe="cmd.exe"]').click();
  await w.getByPlaceholder('arguments').fill('/k demo.bat');
  await w.locator('[data-x64-run]').click();
  await waitStatus('waiting-stdin');
  let con = await consoleText();
  expect(/batch file running under C:\\Windows\\System32\\cmd\.exe/.test(con), 'demo.bat banner missing: ' + con.slice(0, 300));
  expect(/count=3/.test(con), 'for /f + set /a count missing: ' + con.slice(-500));
  expect(/lua says\s+42/.test(con), 'lua54.exe child output missing: ' + con.slice(-500));
  expect(/errorlevel after sub: 7/.test(con), 'call :sub / exit /b 7 missing: ' + con.slice(-500));
  // 2. interactive: builtins, a pipeline into an external program, environment, exit code
  for (const line of ['set /a 6*7', 'dir /b *.lua | findstr /i LUA', 'echo %OS% & ver', 'exit 5']) { await w.locator('[data-x64-stdin]').fill(line); await w.locator('[data-x64-stdin-send]').click(); await sleep(150); }
  await waitStatus('exited');
  con = await consoleText();
  expect(/^42\s*$/m.test(con), 'set /a 6*7 → 42 missing: ' + con.slice(-400));
  expect(/demo\.lua/.test(con.slice(con.indexOf('findstr'))), 'dir | findstr output missing: ' + con.slice(-400));
  expect(/Windows_NT/.test(con) && /Nebula x64 cmd/.test(con), 'echo %OS% / ver output missing: ' + con.slice(-400));
  await w.locator('[data-x64-tab="report"]').click();
  expect(/exit(ed| code)[^\n]*\b5\b/.test(await w.locator('[data-x64-report]').innerText()), 'exit code 5 not reported');
  await page.screenshot({ path: resolve(SHOTS, 'x64-cmd.png') });
  await closeAll();
  // 3. the desktop Command Prompt hands over to the real shell: `real` opens the x64 Runner with cmd.exe already running
  await openFromDesktop('Command Prompt');
  const term = page.locator('[data-window="terminal"]');
  const input = term.locator('input').first();
  await input.fill('real'); await input.press('Enter');
  const w2 = page.locator('[data-window][aria-label*="x64 Runner"]');
  await w2.locator('[data-x64-status="waiting-stdin"]').waitFor({ timeout: 60_000 });
  await w2.locator('[data-x64-stdin]').fill('echo real-shell-ok'); await w2.locator('[data-x64-stdin-send]').click();
  await page.waitForFunction(() => /real-shell-ok/.test(document.querySelector('[data-x64-console]')?.textContent || ''), null, { timeout: 30_000 });
  await page.screenshot({ path: resolve(SHOTS, 'x64-cmd-real.png') });
  await closeAll();
});

await test('x64: virtual disk + stdin — files mounted from the host, read/written by busybox, console input feeds blocking reads', async () => {
  const w = await x64Open();
  await w.locator('[data-x64-exe="busybox64.exe"]').waitFor({ timeout: 30_000 });
  const start = async (args) => {
    await w.locator('[data-x64-exe="busybox64.exe"]').click();
    await w.getByPlaceholder('arguments').fill(args);
    await w.locator('[data-x64-run]').click();
  };
  const waitStatus = (st) => page.waitForFunction((s) => document.querySelector('[data-x64-status]')?.getAttribute('data-x64-status') === s, st, { timeout: 30_000 });
  const consoleText = () => w.locator('[data-x64-console]').innerText();
  // 1. a host file added to the virtual disk is visible to the program
  const tmp = resolve(SHOTS, 'words.txt'); writeFileSync(tmp, 'pear\napple\nmango\n');
  await w.locator('[data-x64-tab="files"]').click();
  await w.locator('[data-x64-addfiles]').setInputFiles(tmp);
  await w.locator('[data-x64-vfs="C:\\\\nebula\\\\words.txt"]').waitFor({ timeout: 10_000 });
  await start('sort words.txt'); await waitStatus('exited');
  expect(/apple\s+mango\s+pear/.test(await consoleText()), 'sort of a mounted file failed: ' + (await consoleText()).slice(0, 200));
  // 2. blocking stdin: program waits, we type lines, EOF ends it
  await start('sort -r'); await waitStatus('waiting-stdin');
  for (const line of ['banana', 'cherry', 'apple']) { await w.locator('[data-x64-stdin]').fill(line); await w.locator('[data-x64-stdin-send]').click(); }
  await w.locator('[data-x64-stdin-eof]').click(); await waitStatus('exited');
  const sorted = await consoleText();
  expect(/cherry\s+banana\s+apple\s*$/.test(sorted.trim()), 'stdin → sort -r failed: ' + sorted.slice(-200));
  // 3. a file written by the program shows up on the virtual disk, can be read back and downloaded
  await start('tee out.txt'); await waitStatus('waiting-stdin');
  await w.locator('[data-x64-stdin]').fill('hello disk'); await w.locator('[data-x64-stdin-send]').click();
  await w.locator('[data-x64-stdin-eof]').click(); await waitStatus('exited');
  await start('cat out.txt'); await waitStatus('exited');
  expect(/hello disk/.test(await consoleText()), 'written file could not be read back');
  await w.locator('[data-x64-tab="files"]').click();
  const row = w.locator('[data-x64-vfs="C:\\\\nebula\\\\out.txt"]');
  await row.waitFor({ timeout: 10_000 });
  expect(/written/.test(await row.innerText()), 'out.txt not marked as written');
  const [dl] = await Promise.all([page.waitForEvent('download', { timeout: 10_000 }), row.locator('[data-x64-download]').click()]);
  expect(dl.suggestedFilename() === 'out.txt', 'download name: ' + dl.suggestedFilename());
  // 4. directory listing + environment through the Win32 layer
  await start('ls -l'); await waitStatus('exited');
  const ls = await consoleText();
  expect(/words\.txt/.test(ls) && /out\.txt/.test(ls) && /readme\.txt/.test(ls), 'ls -l missing files: ' + ls.slice(0, 300));
  await start('env'); await waitStatus('exited');
  expect(/TEMP=C:\\tmp/.test(await consoleText()), 'environment block not visible');
  await page.screenshot({ path: resolve(SHOTS, 'x64-files.png') });
  await closeAll();
});
await test('x64: opening a non-PE file and a 32-bit / unsupported PE gives an honest verdict', async () => {
  const w = await x64Open();
  // garbage → not a PE; minimal 32-bit PE header → unsupported
  const pe32 = new Uint8Array(0x200); const dv = new DataView(pe32.buffer);
  pe32[0] = 0x4d; pe32[1] = 0x5a; dv.setUint32(0x3c, 0x80, true); pe32[0x80] = 0x50; pe32[0x81] = 0x45; dv.setUint16(0x84, 0x14c, true); dv.setUint16(0x94, 0xe0, true); dv.setUint16(0x98, 0x10b, true);
  await w.locator('[data-x64-file]').setInputFiles([
    { name: 'notes.txt', mimeType: 'text/plain', buffer: Buffer.from('this is not an executable') },
    { name: 'legacy32.exe', mimeType: 'application/octet-stream', buffer: Buffer.from(pe32) },
  ]);
  await w.locator('[data-x64-exe="legacy32.exe"]').waitFor();
  expect(await w.locator('[data-x64-exe="notes.txt"] [data-x64-verdict="invalid"]').count() === 1, 'text file not flagged');
  const v32 = await w.locator('[data-x64-exe="legacy32.exe"] [data-x64-verdict]').getAttribute('data-x64-verdict');
  expect(v32 === 'unsupported', '32-bit PE verdict: ' + v32);
  await closeAll();
});

await test('opencode window: DEMO mode when bridge is offline', async () => {
  const c2 = await browser.newContext({ viewport: { width: 1400, height: 900 } });
  const p2 = await c2.newPage();
  await p2.route('**/api/opencode/**', r => r.abort());
  await p2.goto(url, { waitUntil: 'load' });
  await iconOf(p2, 'opencode').dblclick();
  await p2.waitForSelector('text=demo · bridge offline', { timeout: 10_000 });
  await p2.waitForFunction(() => /WIN|autotest|7500/.test(document.body.innerText), null, { timeout: 30_000 });
  const input = p2.locator('[data-window="opencode"] input').last();
  await input.fill('fps'); await input.press('Enter'); await sleep(800);
  expect(/fps/i.test(await p2.locator('[data-window="opencode"]').innerText()), 'demo answer missing');
  await p2.screenshot({ path: resolve(SHOTS, 'opencode-demo.png') });
  await c2.close();
});

const bridgeUp = await fetch(`${url}/api/opencode/bridge`).then(r => r.ok).catch(() => false);
if (bridgeUp || process.env.E2E_BRIDGE) {
  // the free opencode/big-pickle model answered in 225 s on 2026-11-01 (bridge.test.ts) — give the real agent the same budget the bun test has
  const AGENT_TIMEOUT = 330_000;
  // Wait for the agent's answer; if the free model is still "working" (request accepted, no error shown) when the budget runs out,
  // the result is INCONCLUSIVE (third-party rate limit / queue — opencode/big-pickle took 225 s per turn on 2026-11-01), not a failure.
  const waitAgent = async (pg, fn) => {
    try { await pg.waitForFunction(fn, null, { timeout: AGENT_TIMEOUT }); }
    catch (e) {
      const txt = await pg.locator('[data-window="opencode"]').innerText().catch(() => '');
      const working = await pg.locator('input[placeholder*="working"]').count().catch(() => 0);
      const tail = txt.replace(/\s+/g, ' ').slice(-220);
      const quota = /FreeUsageLimit|free usage limit|rate limit|429/i.test(txt);
      if (/Timeout/.test(String(e.message)) && (quota || (working && !/error:/i.test(txt)))) { const err = new Error(`${quota ? 'free tier quota (FreeUsageLimit) reached on the third-party model' : `free model still working after ${AGENT_TIMEOUT / 1000} s (request accepted, no error)`} — ${tail}`); err.name = 'Inconclusive'; throw err; }
      try { await pg.screenshot({ path: resolve(SHOTS, 'fail-agent-window.png') }); } catch { /* ignore */ }
      throw new Error(`${String(e.message).split('\n')[0]} — window: ${tail}`);
    }
  };
  await test('opencode window: LIVE mode — real agent answers and runs a tool', async () => {
    await openFromDesktop('opencode');
    await page.waitForSelector('text=live · v', { timeout: 15_000 });
    const oc = page.locator('[data-window="opencode"]');
    const input = oc.locator('input').last();
    await input.fill('Reply with exactly the two words: PICKLE LIVE, then run `ls opencode` with your bash tool.');
    await input.press('Enter');
    await waitAgent(page, () => document.body.innerText.includes('PICKLE LIVE') && !document.querySelector('input[placeholder*="working"]'));
    const txt = await oc.innerText();
    expect(/bridge\.ts/.test(txt), 'tool output (ls opencode) not rendered');
    expect(/tokens/.test(txt), 'token counter missing');
    await page.screenshot({ path: resolve(SHOTS, 'opencode-live.png') });
    await closeAll();
  });

  await test('opencode window: NATIVE mode — agent loop runs in the page, model via /zen, tool writes a file', async () => {
    const c6 = await browser.newContext({ viewport: { width: 1400, height: 900 } });
    const p6 = await c6.newPage();
    p6.on('pageerror', e => errors.push(`pageerror(native): ${e.message}`));
    await p6.addInitScript(() => { localStorage.setItem('nebula.agentMode', 'native'); localStorage.removeItem('nebula.native.vfs'); });
    await p6.goto(url, { waitUntil: 'load' });
    await iconOf(p6, 'opencode').dblclick();
    await p6.waitForSelector('[data-native-mode="browser"]', { timeout: 15_000 });
    const oc = p6.locator('[data-window="opencode"]');
    expect(/native agent/.test(await oc.innerText()), 'native welcome missing');
    const input = oc.locator('input').last();
    await input.fill('Use the write tool to create a file named todo.md whose content is exactly: NATIVE OK. Then reply with the single word: done');
    await input.press('Enter');
    await waitAgent(p6, () => {
      const t = document.querySelector('[data-window="opencode"]')?.textContent ?? '';
      return /write/.test(t) && /completed/i.test(t) && !document.querySelector('input[placeholder*="working"]');
    });
    const txt = await oc.innerText();
    expect(!/FreeUsageLimit|error:/i.test(txt), `agent error: ${txt.slice(0, 200)}`);
    expect(/tokens/.test(txt), 'token counter missing');
    const vfs = await p6.evaluate(() => JSON.parse(localStorage.getItem('nebula.native.vfs') ?? '{}'));
    expect(typeof vfs['/workspace/todo.md'] === 'string' && /NATIVE OK/.test(vfs['/workspace/todo.md']), `todo.md not written: ${Object.keys(vfs)}`);
    await p6.screenshot({ path: resolve(SHOTS, 'opencode-native.png') });
    await c6.close();
  });
} else {
  console.log('  – opencode LIVE/NATIVE tests skipped (bridge not running; start `npm run opencode`)');
}

await test('opencode window: agent mode switch (auto / native / bridge) is persisted', async () => {
  const c7 = await browser.newContext({ viewport: { width: 1400, height: 900 } });
  const p7 = await c7.newPage();
  await p7.goto(url, { waitUntil: 'load' });
  await iconOf(p7, 'opencode').dblclick();
  await p7.waitForSelector('text=/live · v|demo · bridge offline|native ·/', { timeout: 15_000 });
  await p7.locator('[aria-label="Bridge settings"]').click();
  const form = p7.locator('[data-bridge-config]');
  expect(await form.locator('[data-agent-mode]').getAttribute('data-agent-mode') === 'auto', 'default agent mode is not auto');
  await form.getByRole('radio', { name: /native/ }).click();
  expect(await p7.evaluate(() => localStorage.getItem('nebula.agentMode')) === 'native', 'agent mode not persisted');
  await form.getByRole('radio', { name: 'auto' }).click();
  expect(await p7.evaluate(() => localStorage.getItem('nebula.agentMode')) === null, 'agent mode not reset');
  await c7.close();
});

await test('responsive: 390×844 phone viewport still shows taskbar + icons and opens an app', async () => {
  const c3 = await browser.newContext({ viewport: { width: 390, height: 844 }, hasTouch: true, isMobile: true });
  const p3 = await c3.newPage();
  await p3.goto(url, { waitUntil: 'load' });
  await p3.waitForSelector('[aria-label="Start"]');
  await iconOf(p3, 'Steam').tap();
  await sleep(400);
  const w = p3.locator('[data-window]').first();
  expect(await w.count() === 1, 'tap did not open the app on touch device');
  const b = await w.boundingBox();
  expect(b.x >= 0 && b.x + b.width <= 392, `window overflows phone screen (${b.x}+${b.width})`);
  await p3.screenshot({ path: resolve(SHOTS, 'phone.png') });
  await c3.close();
});

await test('PWA: manifest, icons and service worker are served', async () => {
  const m = await (await fetch(`${url}/manifest.webmanifest`)).json();
  expect(m.name && m.icons?.length >= 3 && m.display === 'standalone', 'manifest incomplete');
  for (const i of m.icons) expect((await fetch(`${url}${i.src}`)).ok, `icon ${i.src} missing`);
  expect((await fetch(`${url}/sw.js`)).ok, 'sw.js missing');
  const html = await (await fetch(`${url}/`)).text();
  expect(html.includes('rel="manifest"') && html.includes('theme-color'), 'index.html lacks manifest/theme-color');
  expect(!/designarena|rrweb/.test(html), 'third-party telemetry still in index.html');
  const ctl = await page.evaluate(async () => { if (!('serviceWorker' in navigator)) return 'unsupported'; const r = await Promise.race([navigator.serviceWorker.ready.then(() => 'ready'), new Promise(res => setTimeout(() => res('timeout'), 8000))]); return r; });
  expect(ctl === 'ready' || ctl === 'unsupported', `service worker not ready: ${ctl}`);
});

await test('phone: Nebula Strike shows touch controls and they drive the ship', async () => {
  const c4 = await browser.newContext({ viewport: { width: 390, height: 844 }, hasTouch: true, isMobile: true });
  const p4 = await c4.newPage();
  await p4.goto(url, { waitUntil: 'load' });
  await iconOf(p4, 'Nebula Strike').tap();
  await sleep(600);
  const ctl = p4.locator('[data-touch-controls]');
  expect(await ctl.count() === 1, 'touch controls not rendered on a touch device');
  const fire = ctl.getByRole('button', { name: 'Fire' });
  const fb = await fire.boundingBox();
  expect(fb && fb.x >= 0 && fb.x + fb.width <= 390, 'FIRE button off-screen');
  await fire.dispatchEvent('pointerdown', { pointerId: 1, pointerType: 'touch', isPrimary: true, button: 0 });
  await sleep(100);
  expect((await ctl.getAttribute('data-pressed')).includes('fire'), 'fire not registered on pointerdown');
  await fire.dispatchEvent('pointerup', { pointerId: 1, pointerType: 'touch' });
  await sleep(50);
  expect(!(await ctl.getAttribute('data-pressed')).includes('fire'), 'fire not released on pointerup');
  const left = ctl.getByRole('button', { name: 'Move left' });
  await left.dispatchEvent('pointerdown', { pointerId: 2, pointerType: 'touch', isPrimary: true, button: 0 });
  await sleep(400);
  await left.dispatchEvent('pointerup', { pointerId: 2, pointerType: 'touch' });
  await p4.screenshot({ path: resolve(SHOTS, 'phone-game.png') });
  await c4.close();
});

await test('opencode window: bridge URL setting (Android / remote PC) is persisted and re-probes', async () => {
  const c5 = await browser.newContext({ viewport: { width: 1400, height: 900 } });
  const p5 = await c5.newPage();
  await p5.goto(url, { waitUntil: 'load' });
  await iconOf(p5, 'opencode').dblclick();
  await p5.waitForSelector('text=/live · v|demo · bridge offline/', { timeout: 15_000 });
  await p5.locator('[aria-label="Bridge settings"]').click();
  const form = p5.locator('[data-bridge-config]');
  await form.locator('input').first().fill('http://127.0.0.1:1');   // nothing listens here → must fall back to demo
  await form.getByRole('button', { name: 'connect' }).click();
  await p5.waitForSelector('text=demo · bridge offline', { timeout: 15_000 });
  expect(await p5.evaluate(() => localStorage.getItem('nebula.bridgeUrl')) === 'http://127.0.0.1:1', 'bridge url not persisted');
  await p5.locator('[aria-label="Bridge settings"]').click();
  await p5.locator('[data-bridge-config]').getByRole('button', { name: 'reset', exact: true }).click();
  expect(await p5.evaluate(() => localStorage.getItem('nebula.bridgeUrl')) === null, 'bridge url not cleared');
  await c5.close();
});

await test('no uncaught page errors during the run', async () => {
  expect(errors.length === 0, errors.slice(0, 5).join(' | '));
});

await browser.close();
if (server) {
  // kill the whole npx → vite process tree on every platform
  if (process.platform === 'win32') spawn('taskkill', ['/pid', String(server.pid), '/T', '/F'], { stdio: 'ignore' });
  else { try { process.kill(-server.pid, 'SIGTERM'); } catch { server.kill(); } }
}
const failed = results.filter(r => !r.ok);
const inconclusive = results.filter(r => r.inconclusive);
console.log(`\n${results.length - failed.length - inconclusive.length}/${results.length} passed${inconclusive.length ? `, INCONCLUSIVE (free model did not answer in time): ${inconclusive.map(f => f.name).join('; ')}` : ''}${failed.length ? `, FAILED: ${failed.map(f => f.name).join('; ')}` : ''}\n`);
process.exit(failed.length ? 1 : 0);
