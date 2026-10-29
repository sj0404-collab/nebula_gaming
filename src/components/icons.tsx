// Windows 10 style SVG icon set — drawn to mimic the native Fluent/Metro glyphs.
import React from 'react';

export function AppIcon({ id, size = 24, className = '' }: { id: string; size?: number; className?: string }) {
  const s = { width: size, height: size };
  switch (id) {
    case 'edge':
      return (
        <svg viewBox="0 0 48 48" style={s} className={className}>
          <defs>
            <radialGradient id="edgeG" cx="35%" cy="35%" r="80%">
              <stop offset="0%" stopColor="#4fd2ff" /><stop offset="45%" stopColor="#1a9ad6" /><stop offset="100%" stopColor="#005a9e" />
            </radialGradient>
          </defs>
          <circle cx="24" cy="24" r="21" fill="url(#edgeG)" />
          <path d="M8 28c6-9 14-13 22-11-5 2-9 6-9 11 0 4 3 6 8 6 6 0 10-4 11-10-8 3-18 6-32 4z" fill="#bff0ff" opacity="0.92" />
        </svg>
      );
    case 'game':
      return (
        <svg viewBox="0 0 48 48" style={s} className={className}>
          <defs>
            <linearGradient id="gameG" x1="0" y1="0" x2="1" y2="1">
              <stop offset="0%" stopColor="#1b2a4a" /><stop offset="100%" stopColor="#050a18" />
            </linearGradient>
          </defs>
          <rect x="2" y="2" width="44" height="44" rx="9" fill="url(#gameG)" stroke="#3ec6ff" strokeWidth="1.6" />
          <path d="M24 8 L31 26 L24 22 L17 26 Z" fill="#3ec6ff" />
          <circle cx="24" cy="34" r="2.4" fill="#ffd166" />
        </svg>
      );
    case 'steam':
      return (
        <svg viewBox="0 0 48 48" style={s} className={className}>
          <circle cx="24" cy="24" r="22" fill="#1b2838" />
          <circle cx="18" cy="30" r="6.4" fill="none" stroke="#c7d5e0" strokeWidth="3" />
          <path d="M20 25 L34 12" stroke="#c7d5e0" strokeWidth="3" />
          <circle cx="35" cy="13" r="4.6" fill="none" stroke="#c7d5e0" strokeWidth="3" />
          <circle cx="18" cy="30" r="1.6" fill="#c7d5e0" />
        </svg>
      );
    case 'epic':
      return (
        <svg viewBox="0 0 48 48" style={s} className={className}>
          <rect x="2" y="2" width="44" height="44" rx="8" fill="#2f2d2e" />
          <path d="M18 12h12v9l-2.5 3v12L36 28.5V38L24 43 12 38v-9.5L20.5 36V24l-2.5-3z" fill="#fff" transform="translate(0 -2)" />
        </svg>
      );
    case 'terminal':
      return (
        <svg viewBox="0 0 48 48" style={s} className={className}>
          <rect x="3" y="6" width="42" height="36" rx="4" fill="#0c0c0c" stroke="#4b4b4b" strokeWidth="1.5" />
          <path d="M10 15l8 7-8 7" stroke="#cccccc" strokeWidth="2.4" fill="none" strokeLinecap="round" strokeLinejoin="round" />
          <path d="M21 30h12" stroke="#cccccc" strokeWidth="2.4" strokeLinecap="round" />
        </svg>
      );
    case 'opencode':
      return (
        <svg viewBox="0 0 48 48" style={s} className={className}>
          <rect x="3" y="6" width="42" height="36" rx="4" fill="#111318" stroke="#3dd68c" strokeWidth="1.6" />
          <circle cx="24" cy="20" r="5.2" fill="none" stroke="#3dd68c" strokeWidth="2.2" />
          <path d="M14 34l7-6M34 34l-7-6" stroke="#3dd68c" strokeWidth="2.2" strokeLinecap="round" />
        </svg>
      );
    case 'explorer':
      return (
        <svg viewBox="0 0 48 48" style={s} className={className}>
          <path d="M5 12a3 3 0 0 1 3-3h11l4 5h17a3 3 0 0 1 3 3v3H5z" fill="#f7d354" />
          <path d="M5 20h38v16a3 3 0 0 1-3 3H8a3 3 0 0 1-3-3z" fill="#f0c33c" />
          <path d="M5 20h16l4-4" stroke="#caa32a" fill="none" />
        </svg>
      );
    case 'notepad':
      return (
        <svg viewBox="0 0 48 48" style={s} className={className}>
          <rect x="9" y="4" width="30" height="40" rx="2.5" fill="#fdfdfd" stroke="#9db8d6" />
          <path d="M15 13h18M15 20h18M15 27h18M15 34h11" stroke="#4a76b4" strokeWidth="2" strokeLinecap="round" />
        </svg>
      );
    case 'settings':
      return (
        <svg viewBox="0 0 48 48" style={s} className={className}>
          <circle cx="24" cy="24" r="8.5" fill="none" stroke="#d7e3f0" strokeWidth="4.5" />
          {Array.from({ length: 8 }).map((_, i) => {
            const a = (i * Math.PI) / 4;
            const x1 = 24 + Math.cos(a) * 13.5, y1 = 24 + Math.sin(a) * 13.5;
            const x2 = 24 + Math.cos(a) * 20, y2 = 24 + Math.sin(a) * 20;
            return <line key={i} x1={x1} y1={y1} x2={x2} y2={y2} stroke="#d7e3f0" strokeWidth="4.5" strokeLinecap="round" />;
          })}
        </svg>
      );
    case 'downloads':
      return (
        <svg viewBox="0 0 48 48" style={s} className={className}>
          <circle cx="24" cy="24" r="21" fill="#1f6fb2" />
          <path d="M24 13v15" stroke="#fff" strokeWidth="3.4" strokeLinecap="round" />
          <path d="M16 24l8 9 8-9" stroke="#fff" strokeWidth="3.4" fill="none" strokeLinecap="round" strokeLinejoin="round" />
        </svg>
      );
    case 'x64':
      return (
        <svg viewBox="0 0 48 48" style={s} className={className}>
          <defs>
            <linearGradient id="x64G" x1="0" y1="0" x2="1" y2="1">
              <stop offset="0%" stopColor="#4f46e5" /><stop offset="100%" stopColor="#1e1b4b" />
            </linearGradient>
          </defs>
          <rect x="3" y="3" width="42" height="42" rx="8" fill="url(#x64G)" />
          <rect x="12" y="12" width="24" height="24" rx="3" fill="none" stroke="#c7d2fe" strokeWidth="2" />
          {[16, 22, 28, 32].map(p => <g key={p} stroke="#c7d2fe" strokeWidth="2"><path d={`M${p} 6v6`} /><path d={`M${p} 36v6`} /><path d={`M6 ${p}h6`} /><path d={`M36 ${p}h6`} /></g>)}
          <text x="24" y="28" textAnchor="middle" fontFamily="Segoe UI, sans-serif" fontWeight="700" fontSize="11" fill="#fff">x64</text>
        </svg>
      );
    case 'snipping':
      return (
        <svg viewBox="0 0 48 48" style={s} className={className}>
          <rect x="5" y="5" width="38" height="38" rx="4" fill="none" stroke="#e8e8e8" strokeWidth="2.6" strokeDasharray="7 5" />
          <circle cx="17" cy="17" r="2.8" fill="#e8e8e8" />
        </svg>
      );
    case 'recycle':
      return (
        <svg viewBox="0 0 48 48" style={s} className={className}>
          <path d="M15 18l3.4 20.5a2.5 2.5 0 0 0 2.4 2h6.4a2.5 2.5 0 0 0 2.4-2L33 18" fill="none" stroke="#e8f4ff" strokeWidth="2.6" strokeLinejoin="round" />
          <path d="M18 18l4-9h4l4 9" fill="none" stroke="#e8f4ff" strokeWidth="2.6" strokeLinejoin="round" />
          <path d="M12 18h24" stroke="#e8f4ff" strokeWidth="2.6" strokeLinecap="round" />
        </svg>
      );
    case 'pc':
      return (
        <svg viewBox="0 0 48 48" style={s} className={className}>
          <rect x="6" y="9" width="36" height="24" rx="2" fill="#252525" stroke="#5ac8fa" strokeWidth="2" />
          <path d="M4 40h40" stroke="#5ac8fa" strokeWidth="3" strokeLinecap="round" />
          <circle cx="24" cy="33" r="1.6" fill="#5ac8fa" />
        </svg>
      );
    case 'webgl':
      return (
        <svg viewBox="0 0 48 48" style={s} className={className}>
          <path d="M4 4h40L37 40 24 44 11 40Z" fill="#1f6fb2" />
          <path d="M24 11v23l9.5-3.6L38 11Z" fill="#7fd0ff" opacity="0.85" />
          <path d="M24 11v12h9.2" stroke="#fff" strokeWidth="2" fill="none" opacity="0.9" />
        </svg>
      );
    case 'wsl':
      return (
        <svg viewBox="0 0 48 48" style={s} className={className}>
          <rect x="4" y="8" width="40" height="32" rx="3.5" fill="#241f31" stroke="#e95420" strokeWidth="1.8" />
          <path d="M12 17l7 6.5L12 30" stroke="#e95420" strokeWidth="2.2" fill="none" strokeLinecap="round" />
          <path d="M22 31h11" stroke="#e95420" strokeWidth="2.2" strokeLinecap="round" />
        </svg>
      );
    case 'perfmon':
      return (
        <svg viewBox="0 0 48 48" style={s} className={className}>
          <circle cx="24" cy="27" r="15" fill="none" stroke="#7ef0a0" strokeWidth="3.4" />
          <path d="M24 27l8-8" stroke="#7ef0a0" strokeWidth="3.4" strokeLinecap="round" />
          <path d="M24 8v5" stroke="#7ef0a0" strokeWidth="3" strokeLinecap="round" />
        </svg>
      );
    case 'info':
      return (
        <svg viewBox="0 0 48 48" style={s} className={className}>
          <circle cx="24" cy="24" r="20" fill="#1f6fb2" />
          <path d="M24 21v12" stroke="#fff" strokeWidth="3.6" strokeLinecap="round" />
          <circle cx="24" cy="14.5" r="2.3" fill="#fff" />
        </svg>
      );
    case 'shield':
      return (
        <svg viewBox="0 0 48 48" style={s} className={className}>
          <path d="M24 3l17 6v12c0 11-7.5 19-17 24C14.5 40 7 32 7 21V9z" fill="#f4d03f" stroke="#b7950b" strokeWidth="1.6" />
        </svg>
      );
    case 'wifi':
      return (
        <svg viewBox="0 0 48 48" style={s} className={className}>
          <path d="M8 19a23 23 0 0 1 32 0" fill="none" stroke="#e8e8e8" strokeWidth="3" strokeLinecap="round" />
          <path d="M15 27a13.5 13.5 0 0 1 18 0" fill="none" stroke="#e8e8e8" strokeWidth="3" strokeLinecap="round" />
          <circle cx="24" cy="35" r="2.6" fill="#e8e8e8" />
        </svg>
      );
    case 'audio':
      return (
        <svg viewBox="0 0 48 48" style={s} className={className}>
          <path d="M14 18v12h7l9 8V10l-9 8z" fill="#e8e8e8" />
          <path d="M36 17a9 9 0 0 1 0 14" fill="none" stroke="#e8e8e8" strokeWidth="2.6" strokeLinecap="round" />
        </svg>
      );
    default:
      return (
        <svg viewBox="0 0 48 48" style={s} className={className}>
          <rect x="6" y="6" width="36" height="36" rx="7" fill="#3a6ea5" />
        </svg>
      );
  }
}

export function WindowsLogo({ size = 22, className = '' }: { size?: number; className?: string }) {
  return (
    <svg viewBox="0 0 32 32" width={size} height={size} className={className} aria-hidden>
      <path fill="#f25022" d="M2 6.5 13.6 4.9v9.8H2z" />
      <path fill="#7fba00" d="M15.6 4.7 30 2.6v12.1H15.6z" />
      <path fill="#00a4ef" d="M2 16.3h11.6v9.8L2 24.5z" />
      <path fill="#ffb900" d="M15.6 16.3H30v12l-14.4-2z" />
    </svg>
  );
}

// eslint-disable-next-line react-refresh/only-export-components
export const AppIdToName: Record<string, string> = {
  edge: 'Microsoft Edge', nebula: 'Nebula Strike', steam: 'Steam', epic: 'Epic Games',
  terminal: 'Command Prompt', opencode: 'opencode', explorer: 'File Explorer', notepad: 'Notepad',
  settings: 'Settings', downloads: 'Downloads', snipping: 'Snipping Tool', x64: 'x64 Runner',
};
