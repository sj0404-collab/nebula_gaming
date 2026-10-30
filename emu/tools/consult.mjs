#!/usr/bin/env node
/**
 * Design-review helper: asks the free OpenCode Zen models (big-pickle and friends, the same
 * endpoint and request shape the in-app native agent uses) and the keyless Pollinations
 * OpenAI-compatible endpoint to review an emulator design note.
 *
 *   node emu/tools/consult.mjs <provider:model> <prompt-file> [> answer.md]
 *   provider: zen (opencode.ai, Bearer public) | pollinations (text.pollinations.ai/openai)
 *
 * The answers are advisory — everything they suggest is verified against the real binaries
 * by emu/tests/run.sh before it lands.
 */
import { readFileSync } from 'node:fs';
import { resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const [spec, promptFile] = process.argv.slice(2);
if (!spec || !promptFile) { console.error('usage: consult.mjs zen:big-pickle prompt.md'); process.exit(2); }
const [provider, model] = spec.split(':');
const prompt = readFileSync(promptFile, 'utf8');

// opencode identifiers (same scheme as src/lib/agent/core.ts)
const ID_CHARS = '0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz';
let idLast = 0, idCounter = 0;
function opencodeId(prefix, descending) {
  const ts = Date.now(); if (ts !== idLast) { idLast = ts; idCounter = 0; } idCounter++;
  const current = BigInt(ts) * 0x1000n + BigInt(idCounter); const value = descending ? ~current : current;
  const time = Array.from({ length: 6 }, (_, i) => Number((value >> BigInt(40 - 8 * i)) & 0xffn).toString(16).padStart(2, '0')).join('');
  const bytes = new Uint8Array(14); crypto.getRandomValues(bytes);
  return prefix + '_' + time + Array.from(bytes, b => ID_CHARS[b % 62]).join('');
}

const agentDir = resolve(here, '../../src/lib/agent');
const sysSrc = readFileSync(resolve(agentDir, 'opencode-system.ts'), 'utf8');
const systemTemplate = JSON.parse(sysSrc.slice(sysSrc.indexOf('"'), sysSrc.lastIndexOf('"') + 1));
const tools = JSON.parse(readFileSync(resolve(agentDir, 'opencode-tools.json'), 'utf8'));
const system = systemTemplate.replaceAll('{{cwd}}', '/home/user/nebula_gaming').replaceAll('{{platform}}', 'linux').replaceAll('{{model}}', model).replaceAll('{{date}}', new Date().toDateString());

const messages = [
  { role: 'system', content: system },
  { role: 'user', content: 'Do not call any tools. Answer directly in markdown, as a senior Windows-internals / emulator engineer doing a design review.\n\n' + prompt },
];

let url, headers, body;
if (provider === 'zen') {
  const sessionID = opencodeId('ses', true);
  url = 'https://opencode.ai/inference/openai/v1/chat/completions';
  headers = {
    authorization: 'Bearer public', 'content-type': 'application/json',
    'user-agent': 'opencode/1.19.5 ai-sdk/provider-utils/4.0.23 runtime/bun/1.4.4',
    'x-opencode-client': 'cli', 'x-opencode-project': 'global', 'x-opencode-request': opencodeId('msg', false),
    'x-opencode-session': sessionID, 'x-opencode-session-id': sessionID,
  };
  body = JSON.stringify({ model, max_tokens: 32000, messages, tools, tool_choice: 'auto', stream: true, stream_options: { include_usage: true } });
} else if (provider === 'pollinations') {
  url = 'https://text.pollinations.ai/openai';
  headers = { 'content-type': 'application/json' };
  body = JSON.stringify({ model, messages: messages.slice(1), stream: true });
} else if (provider === 'llm7') {      // keyless OpenAI-compatible proxy (api.llm7.io, "Bearer unused")
  url = 'https://api.llm7.io/v1/chat/completions';
  headers = { 'content-type': 'application/json', authorization: 'Bearer unused' };
  body = JSON.stringify({ model, messages: messages.slice(1), stream: true });
} else if (provider === 'airforce') {  // keyless OpenAI-compatible proxy (api.airforce)
  url = 'https://api.airforce/v1/chat/completions';
  headers = { 'content-type': 'application/json' };
  body = JSON.stringify({ model, messages: messages.slice(1), stream: true });
} else { console.error('unknown provider'); process.exit(2); }

const res = await fetch(url, { method: 'POST', headers, body });
if (res.status !== 200) { console.error(`HTTP ${res.status}: ${(await res.text()).slice(0, 500)}`); process.exit(1); }
const reader = res.body.getReader(); const dec = new TextDecoder(); let buf = '', out = '', reasoning = '';
for (;;) {
  const { value, done } = await reader.read(); if (done) break;
  buf += dec.decode(value, { stream: true });
  let nl; while ((nl = buf.indexOf('\n')) >= 0) {
    const line = buf.slice(0, nl).trim(); buf = buf.slice(nl + 1);
    if (!line.startsWith('data:')) continue; const data = line.slice(5).trim(); if (!data || data === '[DONE]') continue;
    try { const j = JSON.parse(data); const d = j.choices?.[0]?.delta; if (d?.content) { out += d.content; process.stdout.write(d.content); } if (d?.reasoning_content) reasoning += d.reasoning_content; if (j.error) console.error('\n[error]', j.error.message); } catch { /* keep-alive */ }
  }
}
process.stdout.write('\n');
if (!out) console.error('(empty answer; reasoning bytes: ' + reasoning.length + ')');
