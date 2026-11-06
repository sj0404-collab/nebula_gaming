// Try a real third-party program or game through the Programs UI exactly as a user would on the phone, and keep screenshots.
//   node e2e/sitecheck.mjs <file.zip|setup.exe> [--tile "Name"] [--args "/S"] [--wait 20000] [--tag label]
// .zip → “Open .exe / .zip…” (portable install onto C:\Program Files\<top>), then Play on the library tile.
// .exe → “Open .exe / .zip…” runs it at once (an installer shows its own wizard; --args passes a command line).
// Output: e2e/shots/site-<tag>-*.png and a one-line verdict with the emulator's own status (running / exited / fault + report).
import { chromium } from 'playwright';
import { spawn } from 'node:child_process';
import { mkdirSync } from 'node:fs';
import { resolve, basename } from 'node:path';

const argv = process.argv.slice(2); const file = argv[0]; if (!file) { console.error('usage: node e2e/sitecheck.mjs <file> [--tile Name] [--args A] [--wait ms] [--tag t]'); process.exit(2); }
const opt = (k, d) => { const i = argv.indexOf(k); return i >= 0 ? argv[i + 1] : d; };
const tile = opt('--tile', null), args = opt('--args', ''), keys = opt('--keys', ''), wait = Number(opt('--wait', '20000')), tag = opt('--tag', basename(file).replace(/\.[^.]+$/, '').replace(/[^a-z0-9]+/gi, '_').toLowerCase());
const ROOT = resolve(import.meta.dirname, '..'); const SHOTS = resolve(ROOT, 'e2e', 'shots'); mkdirSync(SHOTS, { recursive: true });
const PORT = 4181; const url = process.env.E2E_URL || `http://127.0.0.1:${PORT}`;
let server = null;
if (!process.env.E2E_URL) {
  server = spawn('npx', ['vite', 'preview', '--host', '127.0.0.1', '--port', String(PORT), '--strictPort'], { cwd: ROOT, stdio: 'ignore', detached: true });
  const t0 = Date.now(); while (Date.now() - t0 < 30000) { try { const r = await fetch(url); if (r.ok) break; } catch { /* wait */ } await new Promise(r => setTimeout(r, 300)); }
}
const sleep = ms => new Promise(r => setTimeout(r, ms));
const browser = await chromium.launch({ args: ['--autoplay-policy=no-user-gesture-required'] });
const ctx = await browser.newContext({ viewport: { width: 1400, height: 900 } });
if (process.env.X64_JIT === '0') await ctx.addInitScript(() => { try { localStorage.setItem('nebula_jit', '0'); } catch { /* ignore */ } });
const page = await ctx.newPage();
const errors = []; page.on('pageerror', e => errors.push(e.message));
await page.goto(url); await page.waitForSelector('[aria-label="Start"]');
await page.locator('[data-desktop-icon="x64"]').dblclick(); await sleep(300);
const w = page.locator('[data-window][aria-label*="Programs"]');
await w.locator('[data-x64-disk]').waitFor({ timeout: 60000 });
const status = () => page.evaluate(() => document.querySelector('[data-x64-status]')?.getAttribute('data-x64-status'));
const bar = async () => (await w.locator('[data-x64-status]').innerText()).replace(/\s+/g, ' ').slice(0, 300);
const isZip = /\.zip$/i.test(file);
const t0 = Date.now();
await w.locator('[data-x64-open]').setInputFiles(resolve(file));   /* by path: Playwright refuses in-memory buffers above 50 MB */
if (isZip) {
  // install → tile appears
  const sel = tile ? `[data-x64-item="${tile}"]` : '[data-x64-item]';
  await w.locator(sel).first().waitFor({ timeout: 600000 });
  console.log(`installed in ${((Date.now() - t0) / 1000).toFixed(1)} s`);
  await sleep(800); await page.screenshot({ path: resolve(SHOTS, `site-${tag}-1-installed.png`) });
  const diskText = await w.locator('[data-x64-disk]').innerText(); console.log('disk:', diskText.replace(/\s+/g, ' '));
  if (args) { /* arguments for Play are not configurable from the tile; run through the exe list instead */ }
  await w.locator(sel).first().locator('button').first().click();
} else if (args) {
  await w.locator('[data-x64-stop]').click().catch(() => {}); await sleep(300);
  await w.locator('input[placeholder="arguments"]').fill(args); await w.locator('[data-x64-run]').click();
}
// let it run: wait for frames or for exit/fault
const tRun = Date.now();
await page.waitForFunction(() => { const s = document.querySelector('[data-x64-status]')?.getAttribute('data-x64-status'); const fr = Number(document.querySelector('[data-x64-screen]')?.getAttribute('data-frames')) || 0; return fr > 0 || s === 'exited' || s === 'fault' || s === 'stopped'; }, null, { timeout: 300000 }).catch(() => {});
const firstFrameMs = Date.now() - tRun;
// --keys "Enter@25000;Escape@40000": press keys in the guest screen at the given ms after the first frame (walks wizard pages); a shot is taken after each
let tKeys = 0;
for (const kv of keys.split(';').map(x => x.trim()).filter(Boolean)) {
  const [key, atRaw] = kv.split('@'); const at = Number(atRaw || 0); if (at > tKeys) await sleep(Math.min(at - tKeys, 900000)); tKeys = Math.max(tKeys, at);
  await w.locator('[data-x64-tab="screen"]').click().catch(() => {}); const scr = w.locator('[data-x64-screen]').first();
  if (key.startsWith('click:')) {   /* click:0.5,0.6 — fraction of the guest screen */
    const [fx, fy] = key.slice(6).split(',').map(Number); const bb = await scr.boundingBox(); if (bb) await page.mouse.click(bb.x + bb.width * fx, bb.y + bb.height * fy); else console.log('click failed: no screen box');
  } else {   /* the canvas is not focusable: focus its keyboard host (the parent div with tabIndex=0), then type */
    await scr.locator('xpath=..').focus().catch(e => console.log('focus failed:', e.message)); await page.keyboard.press(key).catch(e => console.log('key failed:', e.message));
  }
  await sleep(1500); await page.screenshot({ path: resolve(SHOTS, `site-${tag}-key-${key.replace(/[^a-z0-9]+/gi, '_')}-${at}.png`) }); console.log(`key ${key} @${at} → status=${await status()} bar=${await bar()}`);
}
await sleep(Math.max(0, Math.min(wait, 900000) - tKeys));
const st = await status();
const frames = await page.evaluate(() => Number(document.querySelector('[data-x64-screen]')?.getAttribute('data-frames')) || 0);
if (frames > 0) await w.locator('[data-x64-tab="screen"]').click().catch(() => {});
await sleep(500);
await page.screenshot({ path: resolve(SHOTS, `site-${tag}-2-run.png`) });
console.log(`status=${st} frames=${frames} first-frame-after=${firstFrameMs} ms`);
console.log('bar:', await bar());
if (st === 'fault' || st === 'exited') {
  await w.locator('[data-x64-tab="report"]').click().catch(() => {}); await sleep(300);
  const rep = await w.locator('[data-x64-report]').innerText().catch(() => '');
  console.log('report:', rep.replace(/\s+/g, ' ').slice(0, 1200));
  await page.screenshot({ path: resolve(SHOTS, `site-${tag}-3-report.png`) });
  await w.locator('[data-x64-tab="console"]').click().catch(() => {}); await sleep(200);
  const con = await w.locator('[data-x64-console]').innerText().catch(() => ''); if (con.trim()) console.log('console:', con.replace(/\s+/g, ' ').slice(-800));
}
{ // the Report tab always: MessageBox texts, the emulator log and the API summary are the first thing to read when something is off
  await w.locator('[data-x64-tab="report"]').click().catch(() => {}); await sleep(300);
  await page.evaluate(() => document.querySelectorAll('[data-x64-report] details').forEach(d => { d.open = true; })).catch(() => {});   // emulator log + API table are collapsed by default
  const rep = await w.locator('[data-x64-report]').innerText().catch(() => '');
  console.log('report (running):', rep.replace(/What this is:[\s\S]*$/, '').replace(/\s+/g, ' ').slice(0, 6000));
  { const m = rep.match(/Emulator log[\s\S]*?(?=What this is:|$)/); if (m) { const { writeFileSync } = await import('node:fs'); writeFileSync('/tmp/sc/browser_log.txt', m[0]); console.log('browser emulator log tail:'); console.log(m[0].split('\n').slice(-80).join('\n')); } }
  await page.screenshot({ path: resolve(SHOTS, `site-${tag}-4-report.png`) }); }
if (st === 'running') {   // still running: where is it? (console tail + the emulator's own inspector)
  await w.locator('[data-x64-tab="console"]').click().catch(() => {}); await sleep(200);
  const con = await w.locator('[data-x64-console]').innerText().catch(() => ''); if (con.trim()) console.log('console:', con.replace(/\s+/g, ' ').slice(-800));
  const insp = await page.evaluate(() => { const rt = window.__x64; try { return JSON.stringify(rt?.stats?.() ?? null).slice(0, 1500); } catch (e) { return String(e); } }); console.log('stats:', insp);
}
const mem = await page.evaluate(() => { const rt = window.__x64; return rt ? { guestMB: Math.round(rt.stats?.().memMB ?? 0), wasmMB: Math.round((rt.memoryBytes?.() ?? 0) / 1048576), cacheMB: Math.round((rt.cacheBytes?.() ?? 0) / 1048576), files: rt.vfsList().length } : null; });
console.log('memory:', JSON.stringify(mem), 'pageerrors:', errors.length);
await browser.close(); if (server) { try { process.kill(-server.pid); } catch { /* ignore */ } }
