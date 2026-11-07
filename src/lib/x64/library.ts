// The Programs & Games library: what is installed on the persistent C:\ drive.
// Sources of truth are the same ones Windows uses — Start Menu / Desktop shortcuts (.lnk files the installers
// created through IShellLink) and the registry's Uninstall keys (HKLM\…\CurrentVersion\Uninstall, written by
// NSIS / Inno Setup / MSI-style installers). Nothing is invented: a tile exists only when the installer left
// a shortcut or an Uninstall entry pointing at an .exe that is really on the disk.
import type { VfsEntry } from './runtime';

export interface Shortcut { path: string; target: string; args: string; workDir: string; iconPath: string; iconIndex: number; description: string; showCmd: number }

function u16(b: Uint8Array, o: number) { return b[o] | (b[o + 1] << 8); }
function u32(b: Uint8Array, o: number) { return (b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)) >>> 0; }

/** parse an MS-SHLLINK file (header, optional IDList/LinkInfo skipped, StringData read) */
export function parseLnk(path: string, b: Uint8Array): Shortcut | null {
  if (b.length < 0x4c || u32(b, 0) !== 0x4c) return null;
  const flags = u32(b, 20); const showCmd = u32(b, 60); const iconIndex = u32(b, 56) | 0;
  let off = 0x4c;
  if (flags & 1) { if (off + 2 > b.length) return null; off += 2 + u16(b, off); }                 // HasLinkTargetIDList
  let localPath = '';
  if (flags & 2) {                                                                                 // HasLinkInfo
    if (off + 4 > b.length) return null; const size = u32(b, off); const hdr = u32(b, off + 4);
    const lbpOff = u32(b, off + 16); if (lbpOff && off + lbpOff < b.length) { let s = ''; for (let i = off + lbpOff; i < b.length && b[i]; i++) s += String.fromCharCode(b[i]); localPath = s; }
    if (hdr >= 0x24) { const ulbp = u32(b, off + 28); if (ulbp && off + ulbp < b.length) { let s = ''; for (let i = off + ulbp; i + 1 < b.length && u16(b, i); i += 2) s += String.fromCharCode(u16(b, i)); if (s) localPath = s; } }
    off += size;
  }
  const unicode = !!(flags & 0x80);
  const str = (): string => {
    if (off + 2 > b.length) return ''; const n = u16(b, off); off += 2; let s = '';
    if (unicode) { for (let i = 0; i < n && off + 1 < b.length; i++, off += 2) s += String.fromCharCode(u16(b, off)); }
    else { for (let i = 0; i < n && off < b.length; i++, off++) s += String.fromCharCode(b[off]); }
    return s;
  };
  const description = flags & 4 ? str() : '';
  const relative = flags & 8 ? str() : '';
  const workDir = flags & 0x10 ? str() : '';
  const args = flags & 0x20 ? str() : '';
  const iconPath = flags & 0x40 ? str() : '';
  let target = localPath || relative;
  if (target && !/^[a-z]:\\/i.test(target)) {   // a relative path is relative to the .lnk's folder
    const dir = path.slice(0, path.lastIndexOf('\\') + 1); target = normalize(dir + target);
  }
  return { path, target, args, workDir, iconPath, iconIndex, description, showCmd };
}

export function normalize(p: string): string {
  const parts = p.replace(/\//g, '\\').split('\\'); const out: string[] = [];
  for (const s of parts) { if (s === '.' || s === '') { if (!out.length && s === '') out.push(''); continue; } if (s === '..') { if (out.length > 1) out.pop(); continue; } out.push(s); }
  return out.join('\\');
}

export interface UninstallEntry { key: string; displayName: string; displayIcon: string; installLocation: string; uninstallString: string; publisher: string; version: string }

/** the emulator's registry hive (C:\Windows\nebula.reg, text format "Nebula Registry Hive 1") → Uninstall entries */
export function parseUninstall(hive: Uint8Array | null): UninstallEntry[] {
  if (!hive) return [];
  const text = new TextDecoder().decode(hive); const out: UninstallEntry[] = []; let cur: UninstallEntry | null = null;
  const dec = (hex: string, type: number): string => {
    const bytes = new Uint8Array(hex.length >> 1); for (let i = 0; i < bytes.length; i++) bytes[i] = parseInt(hex.substr(i * 2, 2), 16);
    if (type === 4) return String(bytes.length >= 4 ? u32(bytes, 0) : 0);
    // REG_SZ written through the W API is UTF-16LE; through the A API it is narrow — tell them apart by the zero high bytes
    let wide = bytes.length >= 2 && bytes.length % 2 === 0; if (wide) { let zeros = 0; for (let i = 1; i < Math.min(bytes.length, 16); i += 2) if (bytes[i] === 0) zeros++; wide = zeros >= Math.min(bytes.length, 16) / 2 - 1; }
    let s = new TextDecoder(wide ? 'utf-16le' : 'utf-8').decode(bytes); const z = s.indexOf('\0'); if (z >= 0) s = s.slice(0, z); return s;
  };
  for (const line of text.split('\n')) {
    if (line.startsWith('[')) { const key = line.slice(1, line.lastIndexOf(']')); const m = /\\CurrentVersion\\Uninstall\\([^\\]+)$/i.exec(key); cur = m ? { key, displayName: '', displayIcon: '', installLocation: '', uninstallString: '', publisher: '', version: '' } : null; if (cur) out.push(cur); continue; }
    if (!cur) continue;
    const m = /^"([^"]*)"=(\d+):([0-9a-fA-F]*)$/.exec(line.trim()); if (!m) continue;
    const name = m[1].toLowerCase(), v = dec(m[3], Number(m[2]));
    if (name === 'displayname') cur.displayName = v; else if (name === 'displayicon') cur.displayIcon = v; else if (name === 'installlocation') cur.installLocation = v;
    else if (name === 'uninstallstring') cur.uninstallString = v; else if (name === 'publisher') cur.publisher = v; else if (name === 'displayversion') cur.version = v;
  }
  return out.filter(e => e.displayName);
}

export interface LibraryItem {
  id: string;            // stable: lowercase exe path
  name: string;          // shortcut / DisplayName
  exe: string;           // full path on C:\
  args: string;
  workDir: string;
  iconPath: string; iconIndex: number;
  shortcut?: string;     // the .lnk it came from
  uninstall?: string;    // UninstallString from the registry (run to uninstall)
  publisher?: string; version?: string;
  group: string;         // Start Menu folder (program group) or ''
  isGame: boolean;       // heuristics only used for the default tile style (never for behaviour)
  bytes: number;
}

const SKIP_NAME = /uninstall|удал|deinstall|remove|readme|help|manual|license|licence|website|homepage|changelog|документац|changes|release notes|what'?s new|faq|support|forum|donate|report a bug|user manual|command line|console$/i;
const SKIP_EXE = /\\(unins\w*|uninstall\w*|setup|install\w*|update\w*|crashreport\w*|crashpad\w*|dxsetup|vcredist\w*|vc_redist\w*|dotnet\w*)\.exe$/i;
const GAME_HINT = /game|игр|unity|unreal|steam_api|d3d|dx\d|directx|openal|fmod|sdl2|wwise|mono|love\.exe/i;

const SHORTCUT_ROOTS = [
  'c:\\programdata\\microsoft\\windows\\start menu\\programs\\',
  'c:\\users\\user\\appdata\\roaming\\microsoft\\windows\\start menu\\programs\\',
  'c:\\users\\public\\desktop\\', 'c:\\users\\user\\desktop\\',
];

function stripQuotes(s: string) { s = s.trim(); if (s.startsWith('"')) { const e = s.indexOf('"', 1); return e > 0 ? s.slice(1, e) : s.slice(1); } const sp = s.indexOf(' '); return sp > 0 && /\.exe$/i.test(s.slice(0, sp)) ? s.slice(0, sp) : s; }

/** scan the virtual disk → library items (sorted by name) */
export function scanLibrary(files: VfsEntry[], read: (path: string) => Uint8Array | null): LibraryItem[] {
  const byLower = new Map<string, VfsEntry>(); for (const f of files) byLower.set(f.path.toLowerCase(), f);
  const items = new Map<string, LibraryItem>();
  const exists = (p: string) => { const f = byLower.get(p.toLowerCase()); return f && !f.dir ? f : undefined; };
  const add = (it: LibraryItem) => { const prev = items.get(it.id); if (!prev || (it.shortcut && !prev.shortcut)) items.set(it.id, { ...prev, ...it }); else if (prev && !prev.uninstall && it.uninstall) prev.uninstall = it.uninstall; };
  // 1. shortcuts
  for (const f of files) {
    if (f.dir || !/\.lnk$/i.test(f.path)) continue; const lower = f.path.toLowerCase();
    const root = SHORTCUT_ROOTS.find(r => lower.startsWith(r)); if (!root) continue;
    const data = read(f.path); if (!data) continue; const sc = parseLnk(f.path, data); if (!sc || !sc.target || !/\.exe$/i.test(sc.target)) continue;
    const target = exists(sc.target); if (!target) continue;
    const base = f.path.slice(f.path.lastIndexOf('\\') + 1).replace(/\.lnk$/i, '');
    if (SKIP_NAME.test(base) || SKIP_EXE.test(sc.target) || sc.args.toLowerCase().includes('uninstall')) continue;
    const rel = f.path.slice(root.length); const group = rel.includes('\\') ? rel.slice(0, rel.lastIndexOf('\\')) : '';
    add({ id: sc.target.toLowerCase() + (sc.args ? ' ' + sc.args.toLowerCase() : ''), name: base, exe: sc.target, args: sc.args, workDir: sc.workDir || sc.target.slice(0, sc.target.lastIndexOf('\\')), iconPath: sc.iconPath || sc.target, iconIndex: sc.iconIndex, shortcut: f.path, group, isGame: GAME_HINT.test(sc.target + ' ' + group + ' ' + base), bytes: target.size });
  }
  // 2. Uninstall registry entries (programs that made no shortcut, and the UninstallString for the ones that did)
  for (const u of parseUninstall(read('C:\\Windows\\nebula.reg'))) {
    const icon = stripQuotes((u.displayIcon || '').replace(/,-?\d+$/, '')); const loc = u.installLocation.replace(/[\\/]+$/, '');
    let exe = icon && /\.exe$/i.test(icon) && !SKIP_EXE.test(icon) && exists(icon) ? icon : '';
    if (!exe && loc) {   // pick the largest non-helper exe in InstallLocation
      const cand = files.filter(f => !f.dir && f.path.toLowerCase().startsWith(loc.toLowerCase() + '\\') && /\.exe$/i.test(f.path) && !SKIP_EXE.test(f.path)).sort((a, b) => b.size - a.size);
      if (cand[0]) exe = cand[0].path;
    }
    const uninst = stripQuotes(u.uninstallString); const uninstall = uninst && exists(uninst) ? u.uninstallString : undefined;
    if (!exe) { // apply the uninstall string to an existing item of the same install folder
      if (uninstall) for (const it of items.values()) if (loc && it.exe.toLowerCase().startsWith(loc.toLowerCase() + '\\') && !it.uninstall) { it.uninstall = uninstall; it.publisher ||= u.publisher; it.version ||= u.version; }
      continue;
    }
    const sameFolder = [...items.values()].filter(it => it.exe.slice(0, it.exe.lastIndexOf('\\')).toLowerCase() === exe.slice(0, exe.lastIndexOf('\\')).toLowerCase());
    if (sameFolder.length) { for (const it of sameFolder) { it.uninstall ||= uninstall; it.publisher ||= u.publisher; it.version ||= u.version; } continue; }
    add({ id: exe.toLowerCase(), name: u.displayName.replace(/\s+\d+(\.\d+)+.*$/, '') || u.displayName, exe, args: '', workDir: exe.slice(0, exe.lastIndexOf('\\')), iconPath: exe, iconIndex: 0, uninstall, publisher: u.publisher, version: u.version, group: '', isGame: GAME_HINT.test(exe + ' ' + u.displayName), bytes: exists(exe)!.size });
  }
  // Executables copied by the user may have neither a shortcut nor an uninstall entry.
  for (const f of files) {
    const p = f.path.toLowerCase();
    if (f.dir || !/\.exe$/i.test(p) || SKIP_EXE.test(p) || !/^c:\\(?:users\\[^\\]+\\(?:desktop|downloads|documents)|program files(?: \(x86\))?|gog games|games)\\/i.test(p) || /\\(?:dotnet|windowsapps|common files)\\/i.test(p)) continue;
    if ([...items.values()].some(it => it.exe.toLowerCase() === p)) continue;
    const name = f.path.slice(f.path.lastIndexOf('\\') + 1).replace(/\.exe$/i, '');
    if (SKIP_NAME.test(name)) continue;
    add({ id: p, name, exe: f.path, args: '', workDir: f.path.slice(0, f.path.lastIndexOf('\\')), iconPath: f.path, iconIndex: 0, group: '', isGame: GAME_HINT.test(f.path), bytes: f.size });
  }
  return [...items.values()].sort((a, b) => a.name.localeCompare(b.name));
}

/** user choices per item (custom cover, hidden) — kept in localStorage, keyed by item id */
export interface LibraryPrefs { cover?: string; hidden?: boolean; lastRun?: number; runs?: number }
const PREFS_KEY = 'nebula.x64.library';
export function loadPrefs(): Record<string, LibraryPrefs> { try { return JSON.parse(localStorage.getItem(PREFS_KEY) || '{}') as Record<string, LibraryPrefs>; } catch { return {}; } }
export function savePrefs(p: Record<string, LibraryPrefs>) { try { localStorage.setItem(PREFS_KEY, JSON.stringify(p)); } catch { /* quota: covers are large; the library still works without them */ } }

/** what kind of file did the user pick: a setup program, a plain program, or something we cannot start */
export function classifyExe(name: string, bytes: Uint8Array): 'installer' | 'program' {
  const n = name.toLowerCase(); if (/setup|install|^npp\.|_installer|-installer|^7z\d{4}|\.msi$/.test(n) && !/portable/.test(n)) return 'installer';
  // installer stubs carry recognisable strings early in the image
  const head = new TextDecoder('latin1').decode(bytes.subarray(0, Math.min(bytes.length, 2 << 20)));
  if (/Nullsoft\.NSIS|NullsoftInst|Inno Setup Setup Data|InstallShield|Wise Installation|Squirrel/.test(head)) return 'installer';
  return 'program';
}

/** write an MS-SHLLINK file the same way the emulator's IShellLink::Save does (unicode StringData; the target in the path slot) */
export function makeLnk(target: string, workDir: string, description = ''): Uint8Array {
  const strs: [number, string][] = []; let flags = 0x80;
  if (description) { flags |= 4; strs.push([4, description]); }
  flags |= 8; strs.push([8, target]);
  if (workDir) { flags |= 0x10; strs.push([0x10, workDir]); }
  let len = 0x4c + 4; for (const [, s] of strs) len += 2 + s.length * 2;
  const b = new Uint8Array(len); const dv = new DataView(b.buffer);
  dv.setUint32(0, 0x4c, true); const clsid = [0x01, 0x14, 0x02, 0x00, 0, 0, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46]; for (let i = 0; i < 16; i++) b[4 + i] = clsid[i];
  dv.setUint32(20, flags, true); dv.setUint32(24, 0x20, true); dv.setUint32(60, 1, true);   // FILE_ATTRIBUTE_ARCHIVE, SW_SHOWNORMAL
  let off = 0x4c;
  for (const [, s] of strs) { dv.setUint16(off, s.length, true); off += 2; for (let i = 0; i < s.length; i++, off += 2) dv.setUint16(off, s.charCodeAt(i), true); }
  dv.setUint32(off, 0, true);
  return b;
}
