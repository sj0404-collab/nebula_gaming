/* On-screen mouse buttons for touch devices (Winlator-style): a left-button cluster on the left edge and a right cluster
   (scroll up ▲ / middle button ● / scroll down ▼ + right button) on the right edge.  Both clusters are semi-transparent,
   can be dragged anywhere by their grip (⋮⋮) and remember their position.  Buttons hold while the finger stays down, so
   "hold LMB here + move on the touchpad with another finger" is a drag; ▲/▼ auto-repeat wheel notches while held. */
import { useCallback, useEffect, useRef, useState } from 'react';

export type MouseBtn = 'l' | 'm' | 'r';
interface Props { onButton(b: MouseBtn, down: boolean): void; onWheel(dir: -1 | 1): void; onHome?(): void }
interface Pos { x: number; y: number }   /* fraction of the container (0..1) of the cluster's top-left */

const KEY = 'nebula_x64_touchmouse';
const DEFAULT: { l: Pos; r: Pos } = { l: { x: 0.02, y: 0.55 }, r: { x: 0.9, y: 0.35 } };
function load(): { l: Pos; r: Pos } { try { const v = JSON.parse(localStorage.getItem(KEY) || ''); if (v && v.l && v.r) return v; } catch { /* defaults */ } return DEFAULT; }

const BTN = 'pointer-events-auto select-none touch-none flex items-center justify-center rounded-2xl border border-white/30 bg-black/35 text-white/90 backdrop-blur-sm active:bg-indigo-500/70 aria-pressed:bg-indigo-500/70';

export function TouchMouse({ onButton, onWheel, onHome }: Props) {
  const [pos, setPos] = useState(load);
  const [held, setHeld] = useState<Record<string, boolean>>({});
  const root = useRef<HTMLDivElement | null>(null);
  const drag = useRef<{ id: number; side: 'l' | 'r'; dx: number; dy: number } | null>(null);
  const repeat = useRef<Map<number, number>>(new Map());   /* pointerId → interval id (wheel auto-repeat) */
  const downBtn = useRef<Map<number, MouseBtn>>(new Map());
  useEffect(() => () => { for (const b of downBtn.current.values()) onButton(b, false); for (const t of repeat.current.values()) clearInterval(t); }, [onButton]);
  useEffect(() => { try { localStorage.setItem(KEY, JSON.stringify(pos)); } catch { /* ignore */ } }, [pos]);

  const press = useCallback((e: React.PointerEvent<HTMLButtonElement>) => {
    e.preventDefault(); e.stopPropagation(); e.currentTarget.setPointerCapture(e.pointerId);
    const b = e.currentTarget.dataset.btn as MouseBtn; downBtn.current.set(e.pointerId, b); setHeld(h => ({ ...h, [b]: true })); onButton(b, true);
    if (navigator.vibrate) navigator.vibrate(8);
  }, [onButton]);
  const release = useCallback((e: React.PointerEvent<HTMLButtonElement>) => {
    const b = downBtn.current.get(e.pointerId); if (!b) return; downBtn.current.delete(e.pointerId);
    if (![...downBtn.current.values()].includes(b)) { setHeld(h => ({ ...h, [b]: false })); onButton(b, false); }
  }, [onButton]);
  const wheelDown = useCallback((e: React.PointerEvent<HTMLButtonElement>) => {
    e.preventDefault(); e.stopPropagation(); e.currentTarget.setPointerCapture(e.pointerId);
    const dir = Number(e.currentTarget.dataset.dir) as -1 | 1; onWheel(dir);
    const t = window.setInterval(() => onWheel(dir), 110); repeat.current.set(e.pointerId, t);
  }, [onWheel]);
  const wheelUp = useCallback((e: React.PointerEvent<HTMLButtonElement>) => { const t = repeat.current.get(e.pointerId); if (t !== undefined) { clearInterval(t); repeat.current.delete(e.pointerId); } }, []);

  /* grips: drag the whole cluster; the position is kept as a fraction so it survives rotation */
  const gripDown = useCallback((e: React.PointerEvent<HTMLDivElement>) => {
    e.preventDefault(); e.stopPropagation(); const side = e.currentTarget.dataset.side as 'l' | 'r'; const r = root.current?.getBoundingClientRect(); if (!r) return;
    e.currentTarget.setPointerCapture(e.pointerId); const p = pos[side]; drag.current = { id: e.pointerId, side, dx: e.clientX - (r.left + p.x * r.width), dy: e.clientY - (r.top + p.y * r.height) };
  }, [pos]);
  const gripMove = useCallback((e: React.PointerEvent<HTMLDivElement>) => {
    const d = drag.current; const r = root.current?.getBoundingClientRect(); if (!d || d.id !== e.pointerId || !r) return; e.preventDefault();
    const x = Math.max(0, Math.min(0.92, (e.clientX - d.dx - r.left) / r.width)), y = Math.max(0, Math.min(0.85, (e.clientY - d.dy - r.top) / r.height));
    setPos(p => ({ ...p, [d.side]: { x, y } }));
  }, []);
  const gripUp = useCallback((e: React.PointerEvent<HTMLDivElement>) => { if (drag.current?.id === e.pointerId) drag.current = null; }, []);
  const grip = (side: 'l' | 'r') => (
    <div data-side={side} data-x64-tm-grip={side} className="pointer-events-auto touch-none cursor-move select-none rounded-lg bg-white/15 px-2 py-0.5 text-center text-[10px] leading-none text-white/70" title="Drag to move"
      onPointerDown={gripDown} onPointerMove={gripMove} onPointerUp={gripUp} onPointerCancel={gripUp} onDoubleClick={() => { setPos(DEFAULT); onHome?.(); }}>⋮⋮</div>
  );
  const at = (p: Pos): React.CSSProperties => ({ left: `${p.x * 100}%`, top: `${p.y * 100}%` });

  return (
    <div ref={root} data-x64-touchmouse className="pointer-events-none absolute inset-0 z-20 select-none" onContextMenu={e => e.preventDefault()}>
      {/* left cluster: the left mouse button */}
      <div className="absolute flex flex-col items-center gap-1" style={at(pos.l)}>
        {grip('l')}
        <button data-btn="l" data-x64-tm-btn="l" aria-pressed={!!held.l} className={`${BTN} h-20 w-16 text-[11px] font-bold`} onPointerDown={press} onPointerUp={release} onPointerCancel={release} title="Left mouse button (hold for drag)">
          <svg width="22" height="30" viewBox="0 0 22 30" fill="none" stroke="currentColor" strokeWidth="1.6"><rect x="1" y="1" width="20" height="28" rx="9" /><path d="M11 1v11M1 12h20" /><path d="M2 2.5h8.5V12H1.5Z" fill="currentColor" opacity="0.85" stroke="none" /></svg>
        </button>
      </div>
      {/* right cluster: wheel up / middle / wheel down, right button */}
      <div className="absolute flex flex-col items-center gap-1" style={at(pos.r)}>
        {grip('r')}
        <div className="flex items-start gap-1">
          <div className="flex flex-col gap-1">
            <button data-dir="1" data-x64-tm-btn="wu" className={`${BTN} h-10 w-12 text-[14px]`} onPointerDown={wheelDown} onPointerUp={wheelUp} onPointerCancel={wheelUp} title="Scroll up (wheel)">▲</button>
            <button data-btn="m" data-x64-tm-btn="m" aria-pressed={!!held.m} className={`${BTN} h-10 w-12 text-[14px]`} onPointerDown={press} onPointerUp={release} onPointerCancel={release} title="Middle mouse button (hold)">●</button>
            <button data-dir="-1" data-x64-tm-btn="wd" className={`${BTN} h-10 w-12 text-[14px]`} onPointerDown={wheelDown} onPointerUp={wheelUp} onPointerCancel={wheelUp} title="Scroll down (wheel)">▼</button>
          </div>
          <button data-btn="r" data-x64-tm-btn="r" aria-pressed={!!held.r} className={`${BTN} h-20 w-16`} onPointerDown={press} onPointerUp={release} onPointerCancel={release} title="Right mouse button">
            <svg width="22" height="30" viewBox="0 0 22 30" fill="none" stroke="currentColor" strokeWidth="1.6"><rect x="1" y="1" width="20" height="28" rx="9" /><path d="M11 1v11M1 12h20" /><path d="M11.5 2.5H20v9.5h-8.5Z" fill="currentColor" opacity="0.85" stroke="none" /></svg>
          </button>
        </div>
      </div>
    </div>
  );
}
