import { useEffect, useState } from 'react';
import { diskList, diskRead, onDiskChange } from './diskbus';
import { parseLnk, type Shortcut } from './library';

/** Real shortcuts on the shared disk, refreshed after installers and Explorer change it. */
export function useShortcuts() {
  const [shortcuts, setShortcuts] = useState<Shortcut[]>([]);
  useEffect(() => {
    let alive = true, generation = 0;
    const refresh = async () => {
      const current = ++generation;
      try {
        const files = await diskList();
        const paths = new Set(files.filter(f => !f.dir).map(f => f.path.toLowerCase()));
        const links = files.filter(f => !f.dir && /\.lnk$/i.test(f.path) && /\\(?:desktop|start menu\\programs)\\/i.test(f.path));
        const result = await Promise.all(links.map(async f => { const b = await diskRead(f.path); return b ? parseLnk(f.path, b) : null; }));
        if (alive && current === generation) setShortcuts(result.filter((s): s is Shortcut => !!s && /\.exe$/i.test(s.target) && paths.has(s.target.toLowerCase())));
      } catch { /* disk unavailable */ }
    };
    void refresh(); const off = onDiskChange(() => { void refresh(); });
    return () => { alive = false; off(); };
  }, []);
  return shortcuts;
}
