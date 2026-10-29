import { useOS } from '../lib/os';
import { AppIcon } from './icons';
import { Power, Search, ChevronRight, User, Settings, FolderOpen, Download } from 'lucide-react';
import { motion, AnimatePresence } from 'framer-motion';

const PINNED: { app: Parameters<ReturnType<typeof useOS>['openApp']>[0]; label: string; icon: string }[] = [
  { app: 'edge', label: 'Microsoft Edge', icon: 'edge' },
  { app: 'nebula', label: 'Nebula Strike', icon: 'game' },
  { app: 'steam', label: 'Steam', icon: 'steam' },
  { app: 'epic', label: 'Epic Games', icon: 'epic' },
  { app: 'terminal', label: 'Command Prompt', icon: 'terminal' },
  { app: 'opencode', label: 'opencode', icon: 'opencode' },
  { app: 'explorer', label: 'File Explorer', icon: 'explorer' },
  { app: 'notepad', label: 'Notepad', icon: 'notepad' },
  { app: 'settings', label: 'Settings', icon: 'settings' },
  { app: 'downloads', label: 'Downloads', icon: 'downloads' },
  { app: 'x64', label: 'x64 Runner', icon: 'x64' },
];

export default function StartMenu() {
  const { startOpen, setStartOpen, openApp, pushToast } = useOS();

  return (
    <AnimatePresence>
      {startOpen && (
        <>
          <div className="absolute inset-0 z-[9980]" onClick={() => setStartOpen(false)} />
          <motion.div
            initial={{ y: 40, opacity: 0 }}
            animate={{ y: 0, opacity: 1 }}
            exit={{ y: 30, opacity: 0 }}
            transition={{ duration: 0.18, ease: 'easeOut' }}
            className="absolute bottom-12 left-0 z-[9990] flex h-[540px] max-h-[calc(100vh-3rem)] w-[520px] max-w-[96vw] overflow-hidden rounded-tr-lg border border-white/15 bg-[#1b1b1b]/95 backdrop-blur-xl shadow-[0_20px_80px_rgba(0,0,0,0.7)]"
            data-startmenu
          >
            {/* rail */}
            <div className="flex w-12 flex-col items-center border-r border-white/5 py-3 gap-3">
              <button className="rounded p-2 hover:bg-white/10" onClick={() => openApp('settings')} title="Account"><User size={18} className="text-white/85" /></button>
              <button className="rounded p-2 hover:bg-white/10" onClick={() => openApp('explorer')} title="File Explorer"><FolderOpen size={17} className="text-white/85" /></button>
              <button className="rounded p-2 hover:bg-white/10" onClick={() => openApp('settings')} title="Settings"><Settings size={17} className="text-white/85" /></button>
              <button className="rounded p-2 hover:bg-white/10" onClick={() => openApp('downloads')} title="Downloads"><Download size={17} className="text-white/85" /></button>
              <button className="mt-auto rounded p-2 hover:bg-[#e81123]" title="Power" onClick={() => pushToast('Power', 'Sleep / Shut down / Restart (blocked in demo)', 'info')}><Power size={17} className="text-white/90" /></button>
            </div>
            {/* apps list */}
            <div className="flex-1 flex flex-col min-w-0">
              <div className="flex items-center gap-2 border-b border-white/5 px-4 py-3">
                <Search size={14} className="text-white/50" />
                <input placeholder="Type here to search" className="w-full bg-transparent text-[12px] text-white outline-none placeholder-white/40" autoFocus />
              </div>
              <div className="flex-1 overflow-y-auto p-2">
                <div className="px-2 py-1 text-[9px] font-bold uppercase tracking-widest text-white/40">Most used</div>
                {PINNED.slice(0, 6).map(p => (
                  <button key={p.app} onClick={() => openApp(p.app)} className="flex w-full items-center gap-2.5 rounded px-2 py-1.5 text-[11px] text-white/85 hover:bg-white/10">
                    <AppIcon id={p.icon} size={18} />{p.label}
                  </button>
                ))}
                <div className="mt-2 px-2 py-1 text-[9px] font-bold uppercase tracking-widest text-white/40">All apps</div>
                <div className="grid grid-cols-2">
                  {PINNED.slice(6).map(p => (
                    <button key={p.app} onClick={() => openApp(p.app)} className="flex items-center gap-2 rounded px-2 py-1.5 text-[11px] text-white/80 hover:bg-white/10">
                      <AppIcon id={p.icon} size={16} />{p.label}
                    </button>
                  ))}
                </div>
              </div>
            </div>
            {/* tiles */}
            <div className="hidden sm:flex w-56 flex-col gap-1.5 p-2 bg-black/20">
              <button onClick={() => openApp('nebula')} className="group relative h-32 overflow-hidden rounded text-left">
                <img src="/assets/game-capsule.jpg" alt="Nebula Strike" className="absolute inset-0 h-full w-full object-cover object-[70%_25%] group-hover:scale-105 transition-transform duration-500" />
                <div className="absolute inset-0 bg-gradient-to-t from-black/80 via-black/20 to-transparent" />
                <div className="relative p-2 flex h-full flex-col justify-end"><div className="text-[11px] font-black text-white">NEBULA STRIKE</div><div className="text-[8px] text-cyan-300">Big Pickle Studios · PLAY</div></div>
              </button>
              <div className="grid grid-cols-2 gap-1.5">
                <button onClick={() => openApp('edge')} className="flex h-20 flex-col items-center justify-center gap-1 rounded bg-[#0078d7] hover:brightness-110"><AppIcon id="edge" size={26} /><span className="text-[9px] text-white">Edge</span></button>
                <button onClick={() => openApp('steam')} className="flex h-20 flex-col items-center justify-center gap-1 rounded bg-[#1b2838] hover:brightness-110"><AppIcon id="steam" size={26} /><span className="text-[9px] text-white">Steam</span></button>
                <button onClick={() => openApp('epic')} className="flex h-20 flex-col items-center justify-center gap-1 rounded bg-[#2f2d2e] hover:brightness-110"><AppIcon id="epic" size={26} /><span className="text-[9px] text-white">Epic</span></button>
                <button onClick={() => openApp('terminal')} className="flex h-20 flex-col items-center justify-center gap-1 rounded bg-[#0c0c0c] hover:brightness-125"><AppIcon id="terminal" size={26} /><span className="text-[9px] text-white">cmd.exe</span></button>
              </div>
              <button onClick={() => openApp('opencode')} className="flex flex-1 min-h-[70px] flex-col items-start justify-end gap-1 rounded bg-[#111318] border border-[#3dd68c]/40 p-2 hover:border-[#3dd68c]">
                <AppIcon id="opencode" size={22} />
                <div className="text-[10px] font-bold text-[#3dd68c]">opencode · Big Pickle</div>
                <div className="text-[8px] text-white/50 flex items-center gap-0.5">local agent TUI <ChevronRight size={9} /></div>
              </button>
            </div>
          </motion.div>
        </>
      )}
    </AnimatePresence>
  );
}
