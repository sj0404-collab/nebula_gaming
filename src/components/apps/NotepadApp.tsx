// Notepad on the real disk: opens and saves files on the persistent C:\ (the same drive the emulator's Windows
// programs use — a file saved here opens in Notepad++ or 7-Zip inside Programs, and files those programs write open here).
import { useCallback, useEffect, useRef, useState } from 'react';
import { useOS } from '../../lib/os';
import { Save, FilePlus, FolderOpen, Download, X, Loader2 } from 'lucide-react';
import { takeDocOpen, decodeText, encodeText } from '../../lib/docs';
import { diskRead, diskWrite, diskAvailable, baseName } from '../../lib/x64/diskbus';

const DOCS = 'C:\\Users\\user\\Documents';

export default function NotepadApp() {
  const { pushToast } = useOS();
  const [path, setPath] = useState<string | null>(null);      // null → Untitled (not yet on the disk)
  const [text, setText] = useState('');
  const [encoding, setEncoding] = useState('UTF-8');
  const [dirty, setDirty] = useState(false);
  const [busy, setBusy] = useState<string | null>(null);
  const [saveAs, setSaveAs] = useState<string | null>(null);   // Save As dialog: the proposed full path
  const taRef = useRef<HTMLTextAreaElement>(null);
  const title = path ? baseName(path) : 'Untitled';

  const openPath = useCallback(async (p: string) => {
    setBusy(`Opening ${p}…`);
    const b = await diskRead(p);
    setBusy(null);
    if (!b) { pushToast('Notepad', `${p} was not found on the disk`, 'notepad'); return; }
    const d = decodeText(b); setText(d.text); setEncoding(d.encoding); setPath(p); setDirty(false);
    setTimeout(() => taRef.current?.focus(), 30);
  }, [pushToast]);
  useEffect(() => { const want = takeDocOpen(); if (!want) return; const t = window.setTimeout(() => { void openPath(want.path); }, 0); return () => window.clearTimeout(t); }, [openPath]);

  const writeTo = useCallback(async (p: string) => {
    setBusy(`Saving ${p}…`);
    const ok = await diskWrite(p, encodeText(text, encoding));
    setBusy(null);
    if (!ok) { pushToast('Notepad', `${p} is a temporary location and is not kept on the disk`, 'notepad'); return; }
    setPath(p); setDirty(false); pushToast('Notepad', `${baseName(p)} saved to ${p.slice(0, p.lastIndexOf('\\'))}`, 'notepad');
  }, [text, encoding, pushToast]);
  const save = useCallback(() => { if (path) void writeTo(path); else setSaveAs(`${DOCS}\\document.txt`); }, [path, writeTo]);

  /** import a text file from the device (Android file picker) — it is saved into Documents on C:\ as well */
  const openFromDevice = async (f: File | undefined) => {
    if (!f) return;
    const b = new Uint8Array(await f.arrayBuffer()); const d = decodeText(b);
    const p = `${DOCS}\\${f.name}`; setBusy(`Importing ${f.name}…`);
    await diskWrite(p, b); setBusy(null);
    setText(d.text); setEncoding(d.encoding); setPath(p); setDirty(false);
  };
  /** export to the device's Downloads (a real file download) */
  const download = () => {
    const bytes = encodeText(text, encoding); const blob = new Blob([bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength) as ArrayBuffer], { type: 'text/plain' });
    const a = document.createElement('a'); a.href = URL.createObjectURL(blob); a.download = title; a.click(); setTimeout(() => URL.revokeObjectURL(a.href), 2000);
  };

  const [caret, setCaret] = useState(0);
  const lines = text.split('\n');
  const before = text.slice(0, caret); const ln = before.split('\n').length; const col = caret - before.lastIndexOf('\n');

  return (
    <div className="relative flex h-full flex-col bg-white text-[#1b1b1b]" data-notepad-path={path ?? ''}>
      <div className="flex h-9 shrink-0 items-center gap-1 border-b border-slate-200 bg-[#f3f3f3] px-2 text-[11px] text-slate-600">
        <span className="font-semibold">File</span><span className="font-semibold">Edit</span><span className="font-semibold">Format</span><span className="font-semibold">View</span><span className="font-semibold">Help</span>
        <button onClick={() => { if (dirty && !confirm('Discard unsaved changes?')) return; setText(''); setPath(null); setEncoding('UTF-8'); setDirty(false); }} title="New" className="ml-2 rounded p-1.5 hover:bg-slate-200"><FilePlus size={14} /></button>
        <label title="Open a text file from this device" className="cursor-pointer rounded p-1.5 hover:bg-slate-200"><FolderOpen size={14} /><input type="file" className="hidden" accept=".txt,.ini,.log,.md,.json,.xml,.reg,.bat,.cfg,.csv,text/*" onChange={e => { void openFromDevice(e.target.files?.[0]); e.target.value = ''; }} /></label>
        <button onClick={save} title="Save (Ctrl+S)" className="rounded p-1.5 hover:bg-slate-200" data-notepad-save><Save size={14} /></button>
        <button onClick={() => setSaveAs(path ?? `${DOCS}\\document.txt`)} title="Save As… (a path on C:\)" className="rounded px-1.5 py-1 hover:bg-slate-200">Save As…</button>
        <button onClick={download} title="Download to this device" className="rounded p-1.5 hover:bg-slate-200"><Download size={14} /></button>
        <span className="ml-auto flex items-center gap-2 text-[10px] text-slate-400">{busy && <Loader2 size={10} className="animate-spin" />}{busy ?? (dirty ? '● unsaved' : path ? 'saved' : 'new')} · {encoding}</span>
      </div>
      {!diskAvailable() && <div className="border-b border-amber-200 bg-amber-50 px-3 py-1 text-[10px] text-amber-800">The persistent disk (IndexedDB) is not available in this browser context — files can only be downloaded.</div>}
      <textarea
        ref={taRef}
        value={text}
        onChange={e => { setText(e.target.value); setDirty(true); setCaret(e.target.selectionStart ?? 0); }}
        onSelect={e => setCaret((e.target as HTMLTextAreaElement).selectionStart ?? 0)}
        onKeyDown={e => { if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 's') { e.preventDefault(); save(); } }}
        className="flex-1 min-h-0 resize-none bg-white p-3 font-mono text-[12px] leading-relaxed outline-none"
        spellCheck={false}
        autoFocus
        data-notepad-text
      />
      <div className="flex h-6 shrink-0 items-center justify-between border-t border-slate-200 bg-[#f3f3f3] px-3 text-[10px] text-slate-500">
        <span>Ln {ln}, Col {col} · {lines.length} lines · {text.length} chars</span>
        <span className="flex items-center gap-2 truncate"><X size={10} /> {path ?? 'Untitled'} · Notepad</span>
      </div>
      {saveAs !== null && (
        <div className="absolute inset-0 z-10 flex items-center justify-center bg-black/30" onClick={() => setSaveAs(null)}>
          <form className="w-[min(92%,420px)] rounded-lg border border-slate-300 bg-white p-4 shadow-xl" onClick={e => e.stopPropagation()}
            onSubmit={e => { e.preventDefault(); const p = saveAs.trim(); if (!/^[a-z]:\\.+/i.test(p)) { pushToast('Notepad', 'Enter a full path like C:\\Users\\user\\Documents\\notes.txt', 'notepad'); return; } setSaveAs(null); void writeTo(p); }}>
            <div className="mb-2 text-[12px] font-semibold">Save As</div>
            <label className="block text-[10px] text-slate-500">File name (full path on C:\)</label>
            <input autoFocus value={saveAs} onChange={e => setSaveAs(e.target.value)} className="mt-1 w-full rounded border border-slate-300 px-2 py-1 font-mono text-[11px] outline-none focus:border-sky-500" data-notepad-saveas spellCheck={false} />
            <label className="mt-2 block text-[10px] text-slate-500">Encoding</label>
            <select value={encoding} onChange={e => setEncoding(e.target.value)} className="mt-1 rounded border border-slate-300 px-2 py-1 text-[11px]">
              {['UTF-8', 'UTF-8 with BOM', 'UTF-16 LE', 'UTF-16 BE'].map(x => <option key={x}>{x}</option>)}
            </select>
            <div className="mt-3 flex justify-end gap-2 text-[11px]">
              <button type="button" onClick={() => setSaveAs(null)} className="rounded border border-slate-300 px-3 py-1 hover:bg-slate-50">Cancel</button>
              <button type="submit" className="rounded bg-sky-600 px-3 py-1 font-semibold text-white hover:bg-sky-700" data-notepad-saveas-ok>Save</button>
            </div>
          </form>
        </div>
      )}
    </div>
  );
}
