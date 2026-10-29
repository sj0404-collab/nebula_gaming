import { useOS } from '../lib/os';
import { AppIcon } from './icons';
import { ShieldCheck } from 'lucide-react';

export default function Desktop() {
  const { openApp, windows } = useOS();
  const active = windows.length > 0;

  const icons: { id: string; label: string; onOpen: () => void }[] = [
    { id: 'pc', label: 'This PC', onOpen: () => openApp('explorer') },
    { id: 'recycle', label: 'Recycle Bin', onOpen: () => openApp('explorer') },
    { id: 'game', label: 'Nebula Strike', onOpen: () => openApp('nebula') },
    { id: 'x64', label: 'x64 Runner', onOpen: () => openApp('x64') },
    { id: 'steam', label: 'Steam', onOpen: () => openApp('steam') },
    { id: 'epic', label: 'Epic Games', onOpen: () => openApp('epic') },
    { id: 'terminal', label: 'Command Prompt', onOpen: () => openApp('terminal') },
    { id: 'opencode', label: 'opencode', onOpen: () => openApp('opencode') },
    { id: 'edge', label: 'Microsoft Edge', onOpen: () => openApp('edge') },
  ];

  return (
    <div className="absolute inset-0 overflow-hidden" onContextMenu={e => e.preventDefault()}>
      {/* wallpaper */}
      <img src="/assets/wallpaper.jpg" alt="Windows 10 Hero wallpaper" className="absolute inset-0 h-full w-full object-cover" draggable={false} />
      {/* subtle vignette */}
      <div className="absolute inset-0 bg-[radial-gradient(ellipse_at_center,transparent_55%,rgba(0,0,0,0.35)_100%)]" />

      {/* desktop icons */}
      <div className="absolute left-3 top-3 grid grid-flow-col grid-rows-5 gap-x-1 gap-y-4">
        {icons.map(icon => (
          <button
            key={icon.id + icon.label}
            className="group flex w-[76px] flex-col items-center gap-1 rounded border border-transparent p-1.5 outline-none transition-colors hover:border-white/25 hover:bg-white/10 focus:border-sky-400/60 focus:bg-sky-400/15"
            onDoubleClick={icon.onOpen}
            onPointerUp={e => { if (e.pointerType === 'touch' || e.pointerType === 'pen') icon.onOpen(); }}
            onKeyDown={e => { if (e.key === 'Enter') icon.onOpen(); }}
            data-desktop-icon={icon.id}
          >
            <AppIcon id={icon.id} size={38} className="drop-shadow-[0_2px_3px_rgba(0,0,0,0.8)]" />
            <span className="text-[10px] leading-tight text-white text-center [text-shadow:0_1px_3px_rgba(0,0,0,0.9),0_0_6px_rgba(0,0,0,0.8)] line-clamp-2">{icon.label}</span>
          </button>
        ))}
      </div>

      {/* bottom-left build tag */}
      <div className="absolute bottom-14 left-4 select-none text-[10px] leading-tight text-white/55 [text-shadow:0_1px_2px_rgba(0,0,0,0.8)]">
        <div className="flex items-center gap-1.5"><ShieldCheck size={11} className="text-sky-300/80" /> Windows 10 Pro · 22H2 · Build 19045.6466</div>
        <div>Activation: Windows is activated</div>
        {!active && <div className="mt-1 text-sky-200/70">double-click an icon — Nebula Strike is preinstalled</div>}
      </div>
    </div>
  );
}
