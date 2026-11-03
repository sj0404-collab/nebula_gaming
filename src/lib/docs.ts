// "Open this file in Notepad" requests from other desktop apps (File Explorer, the Programs window).
// Notepad is multi-instance: openApp('notepad') mounts a fresh window, which takes the pending request on mount.
export interface DocOpen { path: string }
let pending: DocOpen | null = null;
export function requestDocOpen(path: string) { pending = { path }; }
export function takeDocOpen() { const p = pending; pending = null; return p; }

/** decode a text file the way Notepad does: BOMs, UTF-16 LE/BE, otherwise UTF-8 (invalid bytes → U+FFFD, never a crash) */
export function decodeText(b: Uint8Array): { text: string; encoding: string } {
  if (b.length >= 2 && b[0] === 0xff && b[1] === 0xfe) return { text: new TextDecoder('utf-16le').decode(b.subarray(2)), encoding: 'UTF-16 LE' };
  if (b.length >= 2 && b[0] === 0xfe && b[1] === 0xff) return { text: new TextDecoder('utf-16be').decode(b.subarray(2)), encoding: 'UTF-16 BE' };
  if (b.length >= 3 && b[0] === 0xef && b[1] === 0xbb && b[2] === 0xbf) return { text: new TextDecoder('utf-8').decode(b.subarray(3)), encoding: 'UTF-8 with BOM' };
  // heuristics for BOM-less UTF-16 LE (every other byte zero in ASCII text — common for files written by Windows programs)
  if (b.length >= 4 && b[1] === 0 && b[3] === 0 && b[0] !== 0 && b[2] !== 0) return { text: new TextDecoder('utf-16le').decode(b), encoding: 'UTF-16 LE' };
  return { text: new TextDecoder('utf-8').decode(b), encoding: 'UTF-8' };
}
export function encodeText(text: string, encoding: string): Uint8Array {
  if (encoding.startsWith('UTF-16')) {
    const le = encoding !== 'UTF-16 BE'; const out = new Uint8Array(2 + text.length * 2); out[0] = le ? 0xff : 0xfe; out[1] = le ? 0xfe : 0xff;
    for (let i = 0; i < text.length; i++) { const c = text.charCodeAt(i); out[2 + i * 2] = le ? c & 255 : c >> 8; out[3 + i * 2] = le ? c >> 8 : c & 255; }
    return out;
  }
  const body = new TextEncoder().encode(text);
  if (encoding === 'UTF-8 with BOM') { const out = new Uint8Array(body.length + 3); out.set([0xef, 0xbb, 0xbf]); out.set(body, 3); return out; }
  return body;
}
export const TEXT_EXT = /\.(txt|ini|inf|log|md|markdown|bat|cmd|ps1|json|xml|reg|cfg|conf|csv|yml|yaml|html?|css|js|ts|c|h|cpp|hpp|cs|py|lua|sh|nfo|lst|url|properties|toml|gitignore)$/i;
