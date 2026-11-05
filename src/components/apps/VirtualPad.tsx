/* On-screen game controller for touch devices: a left analog stick (drag anywhere in its zone), d-pad, A/B/X/Y, LB/RB, LT/RT,
   Back/Start.  Produces an XInput-layout PadState that the emulator sees as controller 0 (XInput / DirectInput / winmm) —
   the same thing a Bluetooth pad would deliver — so games that only know about gamepads become playable on a phone. */
import { useCallback, useEffect, useRef, useState } from 'react';
import { XB, emptyPad, type PadState } from '../../lib/x64/pad';

interface Props { onChange(p: PadState): void; onClose(): void }

const BTN = 'pointer-events-auto select-none touch-none rounded-full border border-white/30 bg-black/45 text-[12px] font-bold text-white backdrop-blur-sm active:bg-indigo-500/80 flex items-center justify-center';

export function VirtualPad({ onChange, onClose }: Props) {
  const state = useRef<PadState>({ ...emptyPad(), connected: true });
  const held = useRef(new Map<number, number>());   /* pointerId → button bit (buttons) */
  const stick = useRef<{ id: number; cx: number; cy: number } | null>(null);
  const [knob, setKnob] = useState({ x: 0, y: 0 });
  const [lt, setLt] = useState(false); const [rt, setRt] = useState(false);
  const emit = useCallback(() => onChange({ ...state.current }), [onChange]);
  useEffect(() => { emit(); return () => onChange({ ...emptyPad() }); }, [emit, onChange]);

  /* buttons carry their XInput bit in data-bit; one handler pair serves them all (pointer capture keeps the release on the same element) */
  const btnDown = useCallback((e: React.PointerEvent<HTMLButtonElement>) => { e.preventDefault(); e.stopPropagation(); e.currentTarget.setPointerCapture(e.pointerId); const bit = Number(e.currentTarget.dataset.bit); held.current.set(e.pointerId, bit); state.current.buttons |= bit; emit(); }, [emit]);
  const btnUp = useCallback((e: React.PointerEvent<HTMLButtonElement>) => { const bit = held.current.get(e.pointerId); if (bit === undefined) return; held.current.delete(e.pointerId); if (![...held.current.values()].includes(bit)) state.current.buttons &= ~bit; emit(); }, [emit]);
  const trigDown = useCallback((e: React.PointerEvent<HTMLButtonElement>) => { e.preventDefault(); e.stopPropagation(); e.currentTarget.setPointerCapture(e.pointerId); const side = e.currentTarget.dataset.trig as 'lt' | 'rt'; state.current[side] = 255; (side === 'lt' ? setLt : setRt)(true); emit(); }, [emit]);
  const trigUp = useCallback((e: React.PointerEvent<HTMLButtonElement>) => { const side = e.currentTarget.dataset.trig as 'lt' | 'rt'; state.current[side] = 0; (side === 'lt' ? setLt : setRt)(false); emit(); }, [emit]);
  /* analog stick: the first touch in the zone becomes the centre; displacement up to R px maps to the full range */
  const R = 44;
  const stickDown = useCallback((e: React.PointerEvent) => { if (stick.current) return; e.preventDefault(); e.stopPropagation(); e.currentTarget.setPointerCapture(e.pointerId); stick.current = { id: e.pointerId, cx: e.clientX, cy: e.clientY }; setKnob({ x: 0, y: 0 }); }, []);
  const stickMove = useCallback((e: React.PointerEvent) => {
    const s = stick.current; if (!s || s.id !== e.pointerId) return; e.preventDefault();
    let dx = e.clientX - s.cx, dy = e.clientY - s.cy; const d = Math.hypot(dx, dy); if (d > R) { dx = dx * R / d; dy = dy * R / d; }
    setKnob({ x: dx, y: dy });
    const dead = 0.08; const nx = Math.abs(dx / R) < dead ? 0 : dx / R, ny = Math.abs(dy / R) < dead ? 0 : dy / R;
    state.current.lx = Math.round(nx * 32767); state.current.ly = Math.round(-ny * 32767); emit();
  }, [emit]);
  const stickUp = useCallback((e: React.PointerEvent) => { const s = stick.current; if (!s || s.id !== e.pointerId) return; stick.current = null; setKnob({ x: 0, y: 0 }); state.current.lx = 0; state.current.ly = 0; emit(); }, [emit]);

  return (
    <div data-x64-vpad className="pointer-events-none absolute inset-0 z-20 select-none" onContextMenu={e => e.preventDefault()}>
      {/* left: analog stick zone + d-pad above it */}
      <div className="pointer-events-auto absolute bottom-4 left-4 h-36 w-36 touch-none rounded-full border border-white/25 bg-black/30" data-x64-vpad-stick
        onPointerDown={stickDown} onPointerMove={stickMove} onPointerUp={stickUp} onPointerCancel={stickUp}>
        <div className="absolute h-14 w-14 rounded-full border border-white/40 bg-white/25" style={{ left: `calc(50% - 28px + ${knob.x}px)`, top: `calc(50% - 28px + ${knob.y}px)` }} />
      </div>
      <div className="absolute bottom-44 left-6 grid h-28 w-28 grid-cols-3 grid-rows-3 gap-0.5">
        <div /><button className={BTN} data-x64-vpad-btn="up" data-bit={XB.UP} onPointerDown={btnDown} onPointerUp={btnUp} onPointerCancel={btnUp}>▲</button><div />
        <button className={BTN} data-x64-vpad-btn="left" data-bit={XB.LEFT} onPointerDown={btnDown} onPointerUp={btnUp} onPointerCancel={btnUp}>◀</button><div /><button className={BTN} data-x64-vpad-btn="right" data-bit={XB.RIGHT} onPointerDown={btnDown} onPointerUp={btnUp} onPointerCancel={btnUp}>▶</button>
        <div /><button className={BTN} data-x64-vpad-btn="down" data-bit={XB.DOWN} onPointerDown={btnDown} onPointerUp={btnUp} onPointerCancel={btnUp}>▼</button><div />
      </div>
      {/* right: A B X Y diamond */}
      <div className="absolute bottom-6 right-4 grid h-36 w-36 grid-cols-3 grid-rows-3">
        <div /><button className={`${BTN} h-11 w-11 justify-self-center`} data-x64-vpad-btn="y" data-bit={XB.Y} onPointerDown={btnDown} onPointerUp={btnUp} onPointerCancel={btnUp}><span className="text-amber-300">Y</span></button><div />
        <button className={`${BTN} h-11 w-11 justify-self-center`} data-x64-vpad-btn="x" data-bit={XB.X} onPointerDown={btnDown} onPointerUp={btnUp} onPointerCancel={btnUp}><span className="text-sky-300">X</span></button><div /><button className={`${BTN} h-11 w-11 justify-self-center`} data-x64-vpad-btn="b" data-bit={XB.B} onPointerDown={btnDown} onPointerUp={btnUp} onPointerCancel={btnUp}><span className="text-red-300">B</span></button>
        <div /><button className={`${BTN} h-11 w-11 justify-self-center`} data-x64-vpad-btn="a" data-bit={XB.A} onPointerDown={btnDown} onPointerUp={btnUp} onPointerCancel={btnUp}><span className="text-green-300">A</span></button><div />
      </div>
      {/* shoulders / triggers */}
      <div className="absolute left-4 top-12 flex flex-col gap-1">
        <button className={`${BTN} h-8 w-16 rounded-lg ${lt ? 'bg-indigo-500/80' : ''}`} data-x64-vpad-btn="lt" data-trig="lt" onPointerDown={trigDown} onPointerUp={trigUp} onPointerCancel={trigUp}>LT</button>
        <button className={`${BTN} h-8 w-16 rounded-lg`} data-x64-vpad-btn="lb" data-bit={XB.LB} onPointerDown={btnDown} onPointerUp={btnUp} onPointerCancel={btnUp}>LB</button>
      </div>
      <div className="absolute right-4 top-12 flex flex-col gap-1">
        <button className={`${BTN} h-8 w-16 rounded-lg ${rt ? 'bg-indigo-500/80' : ''}`} data-x64-vpad-btn="rt" data-trig="rt" onPointerDown={trigDown} onPointerUp={trigUp} onPointerCancel={trigUp}>RT</button>
        <button className={`${BTN} h-8 w-16 rounded-lg`} data-x64-vpad-btn="rb" data-bit={XB.RB} onPointerDown={btnDown} onPointerUp={btnUp} onPointerCancel={btnUp}>RB</button>
      </div>
      {/* centre: back / start / hide */}
      <div className="absolute bottom-3 left-1/2 flex -translate-x-1/2 gap-2">
        <button className={`${BTN} h-8 w-14 rounded-lg text-[10px]`} data-x64-vpad-btn="back" data-bit={XB.BACK} onPointerDown={btnDown} onPointerUp={btnUp} onPointerCancel={btnUp}>BACK</button>
        <button className={`${BTN} h-8 w-14 rounded-lg text-[10px]`} data-x64-vpad-btn="start" data-bit={XB.START} onPointerDown={btnDown} onPointerUp={btnUp} onPointerCancel={btnUp}>START</button>
        <button className={`${BTN} h-8 w-10 rounded-lg text-[10px] opacity-70`} data-x64-vpad-hide onPointerDown={e => { e.preventDefault(); e.stopPropagation(); }} onClick={e => { e.preventDefault(); onClose(); }} title="Hide the on-screen controller">✕</button>
      </div>
    </div>
  );
}
