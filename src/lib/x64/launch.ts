// "Open the x64 Runner with this program" requests from other desktop apps (the Command Prompt's `real` command).
// If an x64 Runner is already mounted it starts the program at once; otherwise the request waits for the next one to initialise.
export interface X64Launch { exe: string; args: string }
let pending: X64Launch | null = null;
const listeners = new Set<(l: X64Launch) => void>();
export function requestX64Launch(exe: string, args = '') {
  const l = { exe, args };
  if (listeners.size) { for (const fn of listeners) fn(l); } else pending = l;
}
export function takeX64Launch() { const p = pending; pending = null; return p; }
export function onX64Launch(fn: (l: X64Launch) => void) { listeners.add(fn); return () => { listeners.delete(fn); }; }
