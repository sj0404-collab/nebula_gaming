/** Versioned optional vendor components. Never overwrite game files or a previous version. */
import type { X64Runtime } from './runtime';
export type OfficialId = 'openal-x86' | 'openal-x64';
/** Like the official OpenAL installer (oalinst.exe), the router DLL also goes into the Windows system directory of its bitness, so a game
 *  that imports OpenAL32.dll without shipping it finds it; a DLL next to the game's exe still wins (normal search order). Never overwrites. */
export const OFFICIAL_SYSTEM_COPY: Partial<Record<OfficialId, { file: string; dir: string }>> = {
  'openal-x86': { file: 'OpenAL32.dll', dir: 'C:\\Windows\\SysWOW64\\' },
  'openal-x64': { file: 'OpenAL32.dll', dir: 'C:\\Windows\\System32\\' },
};
export interface OfficialManifest {
  id: OfficialId; pack: string; version: string; source: string; archive_sha256: string;
  checksum_verified_against: string | null;
  files: { path: string; size: number; gz: string; sha256: string }[];
}
const ROOT = 'C:\\ProgramData\\Nebula\\Components\\';
const validPath = (s: string) => s.length < 700 && s.split('/').every(p => !!p && p !== '.' && p !== '..' && !/[\\:*?"<>|]/.test(p) && [...p].every(c => c.charCodeAt(0) >= 32));
export function validateOfficial(m: OfficialManifest, id: OfficialId): void {
  if (!m || m.id !== id || !/^[a-f0-9]{16}$/.test(m.version) || !/^[a-f0-9]{64}$/.test(m.archive_sha256) || m.version !== m.archive_sha256.slice(0, 16) || !Array.isArray(m.files) || !m.files.length || m.files.length > 10000) throw new Error('Invalid component manifest');
  const seen = new Set<string>();
  for (const f of m.files) {
    if (!validPath(f.path) || f.gz !== f.path + '.nebula-asset.gz' || !Number.isSafeInteger(f.size) || f.size < 0 || f.size > 400 * 1024 * 1024 || !/^[a-f0-9]{64}$/.test(f.sha256) || seen.has(f.path.toLowerCase())) throw new Error('Invalid component file: ' + f.path);
    seen.add(f.path.toLowerCase());
  }
}
export function officialDir(m: OfficialManifest) { return ROOT + m.id + '\\' + m.version + '\\'; }
export function officialComplete(rt: X64Runtime, m: OfficialManifest): boolean {
  const root = officialDir(m); const list = new Map(rt.vfsList().map(f => [f.path.toLowerCase(), f.size]));
  return list.has((root + '.nebula-complete.json').toLowerCase()) && m.files.every(f => list.get((root + f.path.replace(/\//g, '\\')).toLowerCase()) === f.size);
}
export async function installOfficial(rt: X64Runtime, m: OfficialManifest, fetchFile: (f: OfficialManifest['files'][number]) => Promise<Uint8Array>, progress: (n: number) => void): Promise<void> {
  validateOfficial(m, m.id); const root = officialDir(m);
  const marker = root + '.nebula-complete.json';
  rt.vfsRemove(marker); rt.disk.remove(marker); await rt.disk.flushChecked();
  for (let i = 0; i < m.files.length; i++) {
    const f = m.files[i]; const b = await fetchFile(f);
    if (b.length !== f.size) throw new Error(`${f.path}: incorrect size`);
    const copy = new Uint8Array(b); const digest = await crypto.subtle.digest('SHA-256', copy.buffer);
    const hash = Array.from(new Uint8Array(digest), x => x.toString(16).padStart(2, '0')).join('');
    if (hash !== f.sha256) throw new Error(`${f.path}: SHA-256 mismatch`);
    if (!rt.vfsAddPersistent(root + f.path.replace(/\//g, '\\'), b)) throw new Error(`${f.path}: disk refused file`);
    const sys = OFFICIAL_SYSTEM_COPY[m.id];
    if (sys && f.path === sys.file && rt.vfsSize(sys.dir + sys.file) < 0 && !rt.vfsAddPersistent(sys.dir + sys.file, b)) throw new Error(`${sys.dir}${sys.file}: disk refused file`);
    // Limit pending memory and surface quota failures early. Completion marker is written only after all files are durable.
    if (i % 16 === 15) await rt.disk.flushChecked();
    progress(i + 1);
  }
  await rt.disk.flushChecked();
  if (!rt.vfsAddPersistent(marker, new TextEncoder().encode(JSON.stringify(m)))) throw new Error('Could not write completion marker');
  try { await rt.disk.flushChecked(); } catch (e) { rt.vfsRemove(marker); rt.disk.remove(marker); throw e; }
}
