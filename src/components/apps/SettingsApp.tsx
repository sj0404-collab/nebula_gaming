import { useState } from 'react';
import { useOS } from '../../lib/os';
import { WindowsLogo, AppIcon } from '../icons';
import { Monitor, Wifi, Volume2, ShieldCheck, Info, Palette, Gamepad2 } from 'lucide-react';

type Page = 'system' | 'display' | 'personalize' | 'network' | 'sound' | 'update' | 'gaming';
const PAGES: { id: Page; label: string; icon: React.ReactNode }[] = [
  { id: 'system', label: 'System', icon: <Monitor size={15} /> },
  { id: 'display', label: 'Display', icon: <Monitor size={15} /> },
  { id: 'personalize', label: 'Personalization', icon: <Palette size={15} /> },
  { id: 'network', label: 'Network & Internet', icon: <Wifi size={15} /> },
  { id: 'sound', label: 'Sound', icon: <Volume2 size={15} /> },
  { id: 'update', label: 'Update & Security', icon: <ShieldCheck size={15} /> },
  { id: 'gaming', label: 'Gaming', icon: <Gamepad2 size={15} /> },
];

export default function SettingsApp() {
  const { bestScore, soundEnabled, setSoundEnabled, openApp } = useOS();
  const [page, setPage] = useState<Page>('system');
  const [accent, setAccent] = useState('#0078d7');

  return (
    <div className="flex h-full bg-white text-[#1b1b1b] text-[12px]">
      {/* sidebar */}
      <div className="w-56 shrink-0 bg-[#f3f3f3] border-r border-slate-200 p-2 overflow-y-auto">
        <div className="flex items-center gap-2 px-2 py-2 mb-1">
          <WindowsLogo size={18} />
          <div><div className="text-[12px] font-semibold">BigPickle</div><div className="text-[9px] text-slate-500">BigPickle@bigpickle.dev</div></div>
        </div>
        {PAGES.map(p => (
          <button key={p.id} onClick={() => setPage(p.id)} className={`flex w-full items-center gap-2.5 rounded px-2.5 py-2 text-left transition-colors ${page === p.id ? 'bg-sky-100 text-sky-900 border-l-2 border-sky-600 pl-2' : 'hover:bg-slate-200'}`}>
            {p.icon}<span>{p.label}</span>
          </button>
        ))}
      </div>
      {/* content */}
      <div className="flex-1 min-w-0 overflow-y-auto p-5">
        {page === 'system' && (
          <>
            <h1 className="text-xl font-light">System</h1>
            <div className="mt-3 flex items-center gap-4 rounded-lg border border-slate-200 p-4">
              <WindowsLogo size={64} />
              <div>
                <div className="text-base font-semibold">Windows 10 Pro</div>
                <div className="text-slate-500">Version 22H2 (OS Build 19045.6466)</div>
                <div className="text-slate-500">Experience: Windows Feature Experience Pack 1000.19060.1000.0</div>
              </div>
            </div>
            <div className="mt-4 grid grid-cols-2 gap-3">
              <InfoTile label="Device name" value="DESKTOP-BIGPICKLE" />
              <InfoTile label="Processor" value="AMD Ryzen 9 7950X3D · 16C/32T · 5.7 GHz" />
              <InfoTile label="Installed RAM" value="64.0 GB (63.4 GB usable)" />
              <InfoTile label="System type" value="64-bit operating system, x64-based processor" />
              <InfoTile label="Display engine" value="WebGL 2.0 + WASM64 (memory64)" />
              <InfoTile label="Best Nebula Strike score" value={String(bestScore)} />
            </div>
          </>
        )}
        {page === 'display' && (
          <>
            <h1 className="text-xl font-light">Display</h1>
            <div className="mt-3 max-w-md space-y-3">
              <label className="block"><div className="mb-1 text-slate-600">Scale and layout</div><select className="w-full rounded border border-slate-300 bg-white p-2"><option>100% (Recommended)</option><option>125%</option><option>150%</option></select></label>
              <label className="block"><div className="mb-1 text-slate-600">Display resolution</div><select className="w-full rounded border border-slate-300 bg-white p-2"><option>800 × 450 (Nebula Strike native)</option><option>1920 × 1080</option><option>2560 × 1440</option></select></label>
              <label className="block"><div className="mb-1 text-slate-600">Refresh rate</div><select className="w-full rounded border border-slate-300 bg-white p-2"><option>60.00 Hz (fixed timestep)</option><option>120 Hz</option><option>144 Hz</option></select></label>
            </div>
          </>
        )}
        {page === 'personalize' && (
          <>
            <h1 className="text-xl font-light">Personalization</h1>
            <div className="mt-3">
              <div className="mb-2 text-slate-600">Accent color</div>
              <div className="flex gap-2">{['#0078d7', '#107c10', '#e81123', '#b4009e', '#00b7c3', '#ff8c00'].map(c => (
                <button key={c} onClick={() => setAccent(c)} className="h-9 w-9 rounded-full border-2 transition-transform hover:scale-110" style={{ background: c, borderColor: accent === c ? '#1b1b1b' : 'transparent' }} aria-label={c} />
              ))}</div>
              <div className="mt-4 h-28 rounded-lg border border-slate-200" style={{ background: `linear-gradient(120deg, ${accent}, #021a33)` }} />
              <p className="mt-2 text-slate-500 text-[11px]">Desktop wallpaper: Windows 10 “Hero” — light beams (set by system).</p>
            </div>
          </>
        )}
        {page === 'network' && (
          <>
            <h1 className="text-xl font-light">Network & Internet</h1>
            <div className="mt-3 max-w-md space-y-3">
              <div className="rounded-lg border border-emerald-200 bg-emerald-50 p-3 flex items-center gap-3"><Wifi className="text-emerald-600" size={26} /><div><div className="font-semibold text-emerald-800">Connected — BigPickle-5G</div><div className="text-[11px] text-emerald-700">IPv4 10.0.2.15 · DNS over HTTPS · 1 Gbps</div></div></div>
              <div className="rounded-lg border border-slate-200 p-3 text-[11px] text-slate-600">Windows programs run in the x86-64 emulator (Programs app) without network access: no raw TCP from a WebView page. Edge browses through the device's connection.</div>
            </div>
          </>
        )}
        {page === 'sound' && (
          <>
            <h1 className="text-xl font-light">Sound</h1>
            <div className="mt-3 max-w-md space-y-3">
              <div className="flex items-center justify-between rounded-lg border border-slate-200 p-3"><span>Game audio (Nebula Strike SFX)</span><button onClick={() => setSoundEnabled(!soundEnabled)} className={`relative h-6 w-11 rounded-full transition-colors ${soundEnabled ? 'bg-sky-600' : 'bg-slate-300'}`}><span className={`absolute top-0.5 h-5 w-5 rounded-full bg-white shadow transition-all ${soundEnabled ? 'left-[22px]' : 'left-0.5'}`} /></button></div>
              <InfoTile label="Output device" value="Speakers (Realtek(R) Audio)" />
              <InfoTile label="Sample rate" value="44.1 kHz, 16-bit" />
            </div>
          </>
        )}
        {page === 'update' && (
          <>
            <h1 className="text-xl font-light">Update & Security</h1>
            <div className="mt-3 max-w-md rounded-lg border border-slate-200 p-4 flex items-center gap-3"><ShieldCheck size={38} className="text-sky-600" /><div><div className="font-semibold">You're up to date</div><div className="text-[11px] text-slate-500">Last checked: Today, 21:14 · Windows Update · Definition 1.421.334.0</div></div></div>
            <button className="mt-3 flex items-center gap-1.5 rounded bg-sky-600 px-4 py-2 text-white hover:bg-sky-700">Check for updates</button>
          </>
        )}
        {page === 'gaming' && (
          <>
            <h1 className="text-xl font-light">Gaming</h1>
            <div className="mt-3 max-w-md rounded-lg border border-slate-200 p-4">
              <div className="flex items-center gap-3"><AppIcon id="game" size={44} /><div><div className="font-semibold">Nebula Strike</div><div className="text-[11px] text-slate-500">Big Pickle Studios · Xbox Game Bar ready · 60 FPS</div></div></div>
              <div className="mt-3 flex items-center justify-between"><span className="text-[11px] text-slate-600">Best score this machine</span><span className="font-bold">{bestScore}</span></div>
              <button onClick={() => openApp('nebula')} className="mt-3 w-full rounded bg-[#107c10] px-4 py-2 text-white font-semibold hover:bg-[#0e6e0e]">Launch Nebula Strike</button>
            </div>
          </>
        )}
      </div>
    </div>
  );
}

function InfoTile({ label, value }: { label: string; value: string }) {
  return (
    <div className="rounded-lg border border-slate-200 p-3">
      <div className="text-[10px] uppercase tracking-wide text-slate-400 flex items-center gap-1"><Info size={10} />{label}</div>
      <div className="mt-0.5 text-[11px] font-medium">{value}</div>
    </div>
  );
}
