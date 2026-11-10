// Persistent virtual disk C:\ for the x64 emulator.
// The emulator keeps files in wasm memory; every file a program writes and closes (installers, saved documents,
// the registry hive C:\Windows\nebula.reg, Start Menu .lnk shortcuts…) is handed to this module through the
// host_fs_* imports (emu/x64/host_wasm.c → runtime.ts) and stored in IndexedDB. On the next start everything is
// put back on the disk before any program runs, so installed programs and games survive app restarts — like a
// real C: drive. Scratch paths (C:\tmp, *\Temp\*) are deliberately not persisted.

export interface DiskFile { path: string; data: Uint8Array | null; mtime: number }   // data === null → directory

const DB_NAME = 'nebula-x64-disk', STORE = 'files', VERSION = 1;

export function diskKey(path: string) { return path.replace(/\//g, '\\').toLowerCase(); }
export function isScratch(path: string) { const k = diskKey(path); return k.startsWith('c:\\tmp\\') || k === 'c:\\tmp' || k.includes('\\temp\\') || k.endsWith('\\temp') || k.startsWith('c:\\nebula\\'); }

const CHUNK_BYTES = 16 << 20, CHUNK_FILES = 64;

export class Disk {
  private db: IDBDatabase | null = null;
  private pending = new Map<string, DiskFile | null>();   // key → file to write, or null → delete
  private timer = 0;
  private flushing: Promise<void> | null = null;
  writes = 0; bytes = 0;                                   // counters for the UI ("disk activity")
  onActivity: (() => void) | null = null;
  available = typeof indexedDB !== 'undefined';
  private persistAsked = false;
  lastError: string | null = null;                         // last IndexedDB failure (e.g. QuotaExceededError), shown by the installers

  async open(): Promise<boolean> {
    if (this.db || !this.available) return !!this.db;
    try {
      // ask the browser/WebView not to evict the virtual disk under storage pressure (best effort, result ignored)
      if (!this.persistAsked) { this.persistAsked = true; try { void navigator.storage?.persist?.().catch(() => undefined); } catch { /* unsupported */ } }
      this.db = await new Promise<IDBDatabase>((res, rej) => {
        const r = indexedDB.open(DB_NAME, VERSION);
        r.onupgradeneeded = () => { const d = r.result; if (!d.objectStoreNames.contains(STORE)) d.createObjectStore(STORE); };
        r.onsuccess = () => res(r.result); r.onerror = () => rej(r.error); r.onblocked = () => rej(new Error('blocked'));
      });
      // another context wants to delete/upgrade the database (Settings → reset, tests): let go of the connection and reopen lazily
      this.db.onversionchange = () => { this.db?.close(); this.db = null; };
      return true;
    } catch (e) { this.lastError = String(e); this.available = false; return false; }
  }

  /** every stored entry (directories first, then files) */
  async loadAll(): Promise<DiskFile[]> {
    if (!(await this.open())) throw new Error('Не удалось открыть IndexedDB: ' + (this.lastError ?? 'хранилище недоступно'));
    const db = this.db!;
    const list = await new Promise<DiskFile[]>((res, rej) => {
      const out: DiskFile[] = []; const tx = db.transaction(STORE, 'readonly'); const req = tx.objectStore(STORE).openCursor();
      req.onsuccess = () => { const c = req.result; if (c) { const v = c.value as DiskFile; if (v && typeof v.path === 'string') out.push(v); c.continue(); } else res(out); };
      req.onerror = () => rej(req.error);
    });
    list.sort((a, b) => (a.data === null ? 0 : 1) - (b.data === null ? 0 : 1) || a.path.length - b.path.length);
    return list;
  }

  /** false for scratch paths: the emulator then keeps the node's `written` flag (session-only file) */
  put(path: string, data: Uint8Array): boolean { return this.putOwned(path, data.slice()); }
  /** like put, but the array is handed over (no copy): the caller keeps the same array as its in-page cache */
  putOwned(path: string, data: Uint8Array): boolean { if (isScratch(path)) return false; this.writes++; this.bytes += data.length; this.pending.set(diskKey(path), { path, data, mtime: Date.now() }); this.schedule(); return true; }
  mkdir(path: string) { if (isScratch(path)) return; this.pending.set(diskKey(path), { path, data: null, mtime: Date.now() }); this.schedule(); }
  remove(path: string) { if (isScratch(path)) return; this.pending.set(diskKey(path), null); this.schedule(); }
  async rename(from: string, to: string) {
    // the emulator renames the node in place; we move the stored bytes (or the pending ones) under the new key
    const fk = diskKey(from); const p = this.pending.get(fk);
    if (p !== undefined) { this.pending.delete(fk); if (p) this.pending.set(diskKey(to), { ...p, path: to }); this.pending.set(fk, null); this.schedule(); return; }
    if (!(await this.open())) return;
    const cur = await this.get(fk); this.pending.set(fk, null); if (cur) this.pending.set(diskKey(to), { ...cur, path: to }); this.schedule();
  }
  /** drop the IndexedDB connection (the owner is going away; an open connection with event handlers would keep it — and
   *  everything it references, including the emulator's memory — alive) */
  close() { try { this.db?.close(); } catch { /* ignore */ } this.db = null; }
  /** one stored entry (file or directory marker) by path */
  async read(path: string): Promise<DiskFile | null> { if (!(await this.open())) return null; return this.get(diskKey(path)); }
  private get(key: string): Promise<DiskFile | null> {
    return new Promise(res => { const req = this.db!.transaction(STORE, 'readonly').objectStore(STORE).get(key); req.onsuccess = () => res((req.result as DiskFile) ?? null); req.onerror = () => res(null); });
  }
  private schedule() { this.onActivity?.(); if (!this.timer) this.timer = window.setTimeout(() => { this.timer = 0; void this.flush(); }, 300); }

  /** write everything queued; resolves when the data is durable in IndexedDB */
  flush(): Promise<void> {
    if (this.flushing) return this.flushing.then(() => this.pending.size ? this.flush() : undefined);
    if (!this.pending.size) return Promise.resolve();
    const batch = this.pending; this.pending = new Map();
    this.flushing = (async () => {
      if (!(await this.open())) { for (const [k, v] of batch) if (!this.pending.has(k)) this.pending.set(k, v); return; }
      const db = this.db!;
      // Write in chunks of <= CHUNK_BYTES / CHUNK_FILES per transaction: a whole runtime pack (DirectX = 77 files, 93 MB) in one
      // transaction is rejected with QuotaExceededError by WebView/Chromium when the remaining quota is tight, even though the
      // files fit one by one. A failed chunk (and everything after it) is re-queued so flushChecked() reports the failure.
      const entries = [...batch]; let i = 0;
      while (i < entries.length) {
        const chunk: [string, DiskFile | null][] = []; let bytes = 0;
        while (i < entries.length && chunk.length < CHUNK_FILES && (chunk.length === 0 || bytes + (entries[i][1]?.data?.length ?? 0) <= CHUNK_BYTES)) { bytes += entries[i][1]?.data?.length ?? 0; chunk.push(entries[i++]); }
        const err = await new Promise<string | null>(res => {
          const tx = db.transaction(STORE, 'readwrite'); const st = tx.objectStore(STORE);
          for (const [k, v] of chunk) { if (v) st.put(v, k); else st.delete(k); }
          const fail = () => res(tx.error ? `${tx.error.name}: ${tx.error.message}` : 'transaction aborted');
          tx.oncomplete = () => res(null); tx.onerror = fail; tx.onabort = fail;
        });
        if (err) {
          this.lastError = err; console.warn('[disk] IndexedDB write failed:', err);
          for (const [k, v] of [...chunk, ...entries.slice(i)]) if (!this.pending.has(k)) this.pending.set(k, v);
          break;
        }
        this.onActivity?.();
      }
    })().finally(() => { this.flushing = null; });
    return this.flushing;
  }
  /** Only report an installation as durable after IndexedDB accepted its whole batch. */
  async flushChecked(): Promise<void> {
    await this.flush();
    if (!this.available || this.queued() !== 0) throw new Error('IndexedDB не сохранил файлы; проверьте доступность и свободное место' + (this.lastError ? ` (${this.lastError})` : ''));
  }
  queued() { return this.pending.size + (this.flushing ? 1 : 0); }

  /** wipe the whole disk (Settings → reset) */
  async clear() { this.pending.clear(); if (!(await this.open())) return; await new Promise<void>(res => { const tx = this.db!.transaction(STORE, 'readwrite'); tx.objectStore(STORE).clear(); tx.oncomplete = () => res(); tx.onerror = () => res(); }); }
  async usage(): Promise<{ files: number; bytes: number }> { const all = await this.loadAll(); let bytes = 0; for (const f of all) bytes += f.data?.length ?? 0; return { files: all.length, bytes }; }
}
