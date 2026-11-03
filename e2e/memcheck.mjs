// Memory check: open the Programs window, load the samples, run hello.exe, close — 7 times — and print the renderer RSS after each
// cycle. A closed window must not pin its emulator (wasm memory); RSS must plateau, not grow by ~100 MB per cycle. Usage: node e2e/memcheck.mjs
import { chromium } from 'playwright';
import { spawn, execSync } from 'node:child_process';
const PORT = 4179; const url = `http://127.0.0.1:${PORT}`;
const server = spawn('npx', ['vite', 'preview', '--host', '127.0.0.1', '--port', String(PORT), '--strictPort'], { cwd: process.cwd(), stdio: 'ignore', detached: true });
const t0 = Date.now(); while (Date.now() - t0 < 30000) { try { const r = await fetch(url); if (r.ok) break; } catch {} await new Promise(r => setTimeout(r, 300)); }
const browser = await chromium.launch(); const ctx = await browser.newContext({ viewport: { width: 1400, height: 900 } }); const page = await ctx.newPage();
await page.goto(url); await page.waitForSelector('[aria-label="Start"]');
const rss = () => { try { return execSync("ps -eo rss,args | grep -a 'type=renderer' | grep -v grep | sort -n | tail -1 | awk '{print int($1/1024)}'").toString().trim(); } catch { return '?'; } };
const sleep = ms => new Promise(r => setTimeout(r, ms));
console.log('idle renderer RSS MB', rss());
for (let i = 1; i <= 7; i++) {
  await page.locator('[data-desktop-icon="x64"]').dblclick(); await sleep(300);
  const w = page.locator('[data-window][aria-label*="Programs"]');
  await w.locator('[data-x64-tests]').click();
  await w.locator('[data-x64-samples="ready"]').waitFor({ timeout: 120000 });
  await w.locator('[data-x64-exe="hello.exe"]').click(); await w.locator('[data-x64-run]').click();
  await page.waitForFunction(() => document.querySelector('[data-x64-status]')?.getAttribute('data-x64-status') === 'exited', null, { timeout: 60000 });
  const before = rss();
  await w.locator('[aria-label="Close"]').click(); await sleep(1500);
  const live = await page.evaluate(() => (window.__x64 ? 1 : 0));
  console.log(`cycle ${i}: open+run RSS ${before} MB → after close ${rss()} MB (gc wait)`);
  await page.evaluate(() => new Promise(r => setTimeout(r, 2000)));
  console.log(`   after 2 s: ${rss()} MB`);
}
await browser.close(); try { process.kill(-server.pid); } catch {}
