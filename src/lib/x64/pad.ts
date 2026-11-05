/* Game controllers for the x86 emulator: the browser Gamepad API (physical pads — Bluetooth/USB on Android, desktop) and the
   on-screen touch pad are merged into four XInput-layout slots that the emulator reads through x64_pad (XInput, winmm joystick,
   DirectInput8).  Rumble requests (XInputSetState) go back to the physical pad's vibration actuator. */

/** XINPUT_GAMEPAD_* button bits */
export const XB = {
  UP: 0x0001, DOWN: 0x0002, LEFT: 0x0004, RIGHT: 0x0008, START: 0x0010, BACK: 0x0020, LS: 0x0040, RS: 0x0080,
  LB: 0x0100, RB: 0x0200, GUIDE: 0x0400, A: 0x1000, B: 0x2000, X: 0x4000, Y: 0x8000,
} as const;

export interface PadState { connected: boolean; buttons: number; lt: number; rt: number; lx: number; ly: number; rx: number; ry: number }
export const emptyPad = (): PadState => ({ connected: false, buttons: 0, lt: 0, rt: 0, lx: 0, ly: 0, rx: 0, ry: 0 });

/* W3C "standard" mapping index → XInput bit */
const STD_BUTTONS: number[] = [XB.A, XB.B, XB.X, XB.Y, XB.LB, XB.RB, 0 /* LT */, 0 /* RT */, XB.BACK, XB.START, XB.LS, XB.RS, XB.UP, XB.DOWN, XB.LEFT, XB.RIGHT, XB.GUIDE];

const axis16 = (v: number) => Math.max(-32768, Math.min(32767, Math.round(v * 32767)));
const trig8 = (v: number) => Math.max(0, Math.min(255, Math.round(v * 255)));

/** physical gamepad → XInput slot (standard mapping; non-standard pads get a best-effort positional mapping) */
export function fromGamepad(g: Gamepad): PadState {
  const s = emptyPad(); s.connected = true;
  const b = g.buttons;
  const val = (i: number) => (b[i] ? (typeof b[i].value === 'number' ? b[i].value : b[i].pressed ? 1 : 0) : 0);
  for (let i = 0; i < STD_BUTTONS.length && i < b.length; i++) if (STD_BUTTONS[i] && (b[i].pressed || val(i) > 0.5)) s.buttons |= STD_BUTTONS[i];
  s.lt = trig8(val(6)); s.rt = trig8(val(7));
  const ax = g.axes;
  s.lx = axis16(ax[0] ?? 0); s.ly = axis16(-(ax[1] ?? 0)); s.rx = axis16(ax[2] ?? 0); s.ry = axis16(-(ax[3] ?? 0));   /* XInput: +Y is up */
  if (g.mapping !== 'standard' && b.length < 12 && ax.length >= 6) {   /* d-pad as a hat on axes 4/5 or 6/7 (older Android HID mappings) */
    const hx = ax[6] ?? ax[4] ?? 0, hy = ax[7] ?? ax[5] ?? 0;
    if (hx < -0.5) s.buttons |= XB.LEFT; if (hx > 0.5) s.buttons |= XB.RIGHT; if (hy < -0.5) s.buttons |= XB.UP; if (hy > 0.5) s.buttons |= XB.DOWN;
  }
  return s;
}

function merge(a: PadState, b: PadState): PadState {
  const pick = (x: number, y: number) => (Math.abs(x) >= Math.abs(y) ? x : y);
  return { connected: a.connected || b.connected, buttons: a.buttons | b.buttons, lt: Math.max(a.lt, b.lt), rt: Math.max(a.rt, b.rt), lx: pick(a.lx, b.lx), ly: pick(a.ly, b.ly), rx: pick(a.rx, b.rx), ry: pick(a.ry, b.ry) };
}

export type PadSink = (idx: number, s: PadState) => void;

/** polls navigator.getGamepads() (slots 0..3) and merges the on-screen pad into slot 0; call poll() once per host frame */
export class PadBridge {
  /** on-screen / keyboard-emulated pad (merged into slot 0; "connected" while it is shown) */
  virtual: PadState = emptyPad();
  /** physical pads seen in the last poll (index = XInput slot) */
  physical: (Gamepad | null)[] = [null, null, null, null];
  private last: string[] = ['', '', '', ''];
  private anyEver = false;
  constructor(private sink: PadSink) {}

  poll(): void {
    let pads: (Gamepad | null)[] = [];
    try { pads = typeof navigator !== 'undefined' && navigator.getGamepads ? Array.from(navigator.getGamepads()) : []; } catch { pads = []; }
    const live = pads.filter((g): g is Gamepad => !!g && g.connected);
    if (live.length) this.anyEver = true;
    if (!this.anyEver && !this.virtual.connected && !this.last.some((l) => l)) return;   /* nothing ever connected: no work per frame */
    for (let slot = 0; slot < 4; slot++) {
      const g = live[slot] ?? null; this.physical[slot] = g;
      let s = g ? fromGamepad(g) : emptyPad();
      if (slot === 0 && this.virtual.connected) s = merge(s, this.virtual);
      const key = s.connected ? `${s.buttons},${s.lt},${s.rt},${s.lx},${s.ly},${s.rx},${s.ry}` : '';
      if (key === this.last[slot]) continue;
      this.last[slot] = key; this.sink(slot, s);
    }
  }
  /** the on-screen pad changed */
  setVirtual(s: PadState): void { this.virtual = s; }
  /** XInputSetState → the physical pad's actuator (dual-rumble where supported; Chrome Android exposes vibrationActuator) */
  rumble(slot: number, lo: number, hi: number): void {
    const g = this.physical[slot]; if (!g) return;
    const act = (g as Gamepad & { vibrationActuator?: { playEffect?(type: string, p: Record<string, number>): Promise<string>; reset?(): Promise<string> } }).vibrationActuator;
    if (!act || !act.playEffect) return;
    const strong = lo / 65535, weak = hi / 65535;
    try { if (strong === 0 && weak === 0) { void act.reset?.(); return; } void act.playEffect('dual-rumble', { startDelay: 0, duration: 1000, strongMagnitude: strong, weakMagnitude: weak }).catch(() => {}); } catch { /* unsupported */ }
  }
  /** human-readable list for the status line */
  describe(): string {
    const names = this.physical.map((g, i) => (g ? `#${i} ${g.id.replace(/\s*\(.*$/, '').slice(0, 40)}` : null)).filter(Boolean) as string[];
    if (this.virtual.connected) names.push('on-screen pad');
    return names.join(', ');
  }
}
