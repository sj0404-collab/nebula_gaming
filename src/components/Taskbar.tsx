import { useEffect, useState } from 'react';
import { useOS } from '../lib/os';
import { AppIcon, WindowsLogo } from './icons';
import { Volume2, VolumeX, Wifi, ChevronUp, X } from 'lucide-react';

function Clock() {
  const [now, setNow] = useState(() => new Date());
  useEffect(() => {
    const t = window.setInterval(() => setNow(new Date()), 1000);
    return () => window.clearInterval(t);
  }, []);
  const hh = now.getHours();
  const mm = String(now.getMinutes()).padStart(2, '0');
  const ampm = hh >= 12 ? 'PM' : 'AM';
  const h12 = ((hh + 11) % 12) + 1;
  const date = `${now.getMonth() + 1}/${now.getDate()}/${now.getFullYear()}`;
  return (
    <div className="flex h-12 flex-col items-center justify-center px-3 text-[11px] leading-tight text-white select-none">
      <div>{h12}:{mm} {ampm}</div>
      <div>{date}</div>
    </div>
  );
}

export default function Taskbar() {
  const { windows, openApp, toggleMinimize, focusWindow, startOpen, setStartOpen, soundEnabled, setSoundEnabled, pushToast } = useOS();
  const [showTray, setShowTray] = useState(false);

  return (
    <div className="absolute bottom-0 left-0 right-0 z-[10000] flex h-12 items-stretch bg-[#101010]/95 backdrop-blur-md border-t border-white/10" data-taskbar>
      {/* Start button */}
      <button
        className={`group flex w-12 items-center justify-center transition-colors ${startOpen ? 'bg-[#0078d7]' : 'hover:bg-white/10'}`}
        onClick={() => setStartOpen(!startOpen)}
        aria-label="Start"
      >
        <WindowsLogo size={18} className={startOpen ? '' : 'group-hover:scale-110 transition-transform'} />
      </button>
      {/* Search */}
      <button className="hidden sm:flex h-12 w-48 items-center gap-2 px-3 text-[12px] text-white/70 hover:bg-white/10 border-r border-white/5" onClick={() => openApp('edge')}>
        <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2"><circle cx="11" cy="11" r="7"/><path d="m21 21-4-4"/></svg>
        Type here to search
      </button>
      {/* Task view */}
      <button className="hidden md:grid h-12 w-12 place-items-center hover:bg-white/10" title="Task view" onClick={() => pushToast('Task view', `${windows.length} window(s) open`, 'info')}>
        <div className="relative h-4 w-5"><div className="absolute left-0 top-0 h-3.5 w-3.5 rounded-[2px] border border-white/80"/><div className="absolute right-0 top-0.5 h-3 w-3 rounded-[2px] border border-white/50 bg-[#101010]"/></div>
      </button>
      {/* Pinned + running apps */}
      <div className="flex h-12 flex-1 items-stretch overflow-x-auto">
        {(['edge', 'nebula', 'x64', 'terminal', 'opencode'] as const).map(app => {
          const running = windows.filter(w => w.app === app);
          const isOpen = running.length > 0;
          return (
            <button
              key={app}
              className="relative flex h-12 w-12 shrink-0 items-center justify-center hover:bg-white/10 transition-colors"
              title={app}
              onClick={() => {
                if (!isOpen) { openApp(app); return; }
                const w = running[0];
                if (w.minimized) focusWindow(w.id); else toggleMinimize(w.id);
              }}
            >
              <AppIcon id={app === 'nebula' ? 'game' : app} size={22} />
              {isOpen && <span className="absolute bottom-[3px] h-[3px] w-5 rounded-full bg-[#76b9ed]" />}
            </button>
          );
        })}
        {windows.filter(w => !['edge', 'nebula', 'x64', 'terminal', 'opencode'].includes(w.app)).map(w => (
          <button key={w.id} className="relative flex h-12 w-12 shrink-0 items-center justify-center hover:bg-white/10" title={w.title} onClick={() => (w.minimized ? focusWindow(w.id) : toggleMinimize(w.id))}>
            <AppIcon id={w.icon} size={22} />
            {!w.minimized && <span className="absolute bottom-[3px] h-[3px] w-5 rounded-full bg-[#76b9ed]" />}
          </button>
        ))}
      </div>
      {/* Tray */}
      <div className="relative flex h-12 items-stretch border-l border-white/5">
        {showTray && (
          <div className="absolute bottom-12 right-0 w-56 rounded-md border border-white/15 bg-[#1f1f1f]/95 backdrop-blur-md p-2 text-[11px] text-white/90 shadow-2xl z-50">
            <div className="flex items-center gap-2 px-2 py-1.5 rounded hover:bg-white/10"><Wifi size={14} className="text-emerald-400" /> BigPickle-5G — Connected</div>
            <button className="flex w-full items-center gap-2 px-2 py-1.5 rounded hover:bg-white/10" onClick={() => setSoundEnabled(!soundEnabled)}>{soundEnabled ? <Volume2 size={14} className="text-sky-400" /> : <VolumeX size={14} className="text-red-400" />} Speakers {soundEnabled ? '(on)' : '(muted)'}</button>
            <div className="flex items-center gap-2 px-2 py-1.5"><span className="text-amber-300">⚠</span> Nebula Strike best: see game</div>
            <button className="mt-1 flex w-full items-center justify-end gap-1 text-[10px] text-white/50 hover:text-white" onClick={() => setShowTray(false)}><X size={10} /> close</button>
          </div>
        )}
        <button className="grid h-12 w-8 place-items-center hover:bg-white/10" onClick={() => setShowTray(s => !s)} aria-label="Show hidden icons"><ChevronUp size={14} className="text-white/80" /></button>
        <button className="grid h-12 w-9 place-items-center hover:bg-white/10" onClick={() => pushToast('Network', 'BigPickle-5G · 1 Gbps · IPv4 10.0.2.15', 'wifi')}><Wifi size={15} className="text-white/90" /></button>
        <button className="grid h-12 w-9 place-items-center hover:bg-white/10" onClick={() => setSoundEnabled(!soundEnabled)}>{soundEnabled ? <Volume2 size={15} className="text-white/90" /> : <VolumeX size={15} className="text-white/50" />}</button>
        <Clock />
        {/* Show desktop sliver */}
        <button className="w-2 border-l border-white/20 hover:bg-white/10" title="Show desktop" onClick={() => windows.forEach(w => !w.minimized && toggleMinimize(w.id))} aria-label="Show desktop" />
      </div>
    </div>
  );
}
