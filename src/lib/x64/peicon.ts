// Application icons straight from the PE resource section (RT_GROUP_ICON → RT_ICON), PE32 and PE32+ alike.
// Returns the best-fitting icon as a PNG data URL rendered through a canvas (PNG-compressed entries are passed
// through; classic DIB entries — 32/24/8/4/1 bpp with AND mask — are decoded by hand). No stand-in artwork:
// when an exe has no icon, null is returned and the UI shows a neutral tile.

const RT_ICON = 3, RT_GROUP_ICON = 14;

interface Section { va: number; vsize: number; raw: number; rawSize: number }
function u16(b: Uint8Array, o: number) { return b[o] | (b[o + 1] << 8); }
function u32(b: Uint8Array, o: number) { return (b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)) >>> 0; }

function rvaToOff(secs: Section[], rva: number): number {
  for (const s of secs) if (rva >= s.va && rva < s.va + Math.max(s.vsize, s.rawSize)) return s.raw + (rva - s.va);
  return -1;
}

/** every resource of one type: id/name → bytes */
function resources(b: Uint8Array, type: number): Map<number | string, Uint8Array> {
  const out = new Map<number | string, Uint8Array>();
  if (b.length < 0x40 || u16(b, 0) !== 0x5a4d) return out;
  const pe = u32(b, 0x3c); if (pe + 0x18 > b.length || u32(b, pe) !== 0x4550) return out;
  const nsec = u16(b, pe + 6), optSize = u16(b, pe + 20), magic = u16(b, pe + 24);
  const plus = magic === 0x20b; const ddOff = pe + 24 + (plus ? 112 : 96);
  const ndd = u32(b, pe + 24 + (plus ? 108 : 92)); if (ndd < 3) return out;
  const rsrcRva = u32(b, ddOff + 2 * 8), rsrcSize = u32(b, ddOff + 2 * 8 + 4); if (!rsrcRva || !rsrcSize) return out;
  const secs: Section[] = []; const st = pe + 24 + optSize;
  for (let i = 0; i < nsec; i++) { const o = st + i * 40; if (o + 40 > b.length) break; secs.push({ va: u32(b, o + 12), vsize: u32(b, o + 8), raw: u32(b, o + 20), rawSize: u32(b, o + 16) }); }
  const base = rvaToOff(secs, rsrcRva); if (base < 0) return out;
  const dirEntries = (dirOff: number): { id: number | string; off: number; isDir: boolean }[] => {
    const o = base + dirOff; if (o + 16 > b.length) return [];
    const named = u16(b, o + 12), ids = u16(b, o + 14); const res: { id: number | string; off: number; isDir: boolean }[] = [];
    for (let i = 0; i < named + ids && i < 4096; i++) {
      const e = o + 16 + i * 8; if (e + 8 > b.length) break;
      const nameOrId = u32(b, e), data = u32(b, e + 4);
      let id: number | string = nameOrId;
      if (nameOrId & 0x80000000) { const no = base + (nameOrId & 0x7fffffff); const len = u16(b, no); let s = ''; for (let k = 0; k < len && no + 2 + k * 2 + 1 < b.length; k++) s += String.fromCharCode(u16(b, no + 2 + k * 2)); id = s; }
      res.push({ id, off: data & 0x7fffffff, isDir: !!(data & 0x80000000) });
    }
    return res;
  };
  const typeEntry = dirEntries(0).find(e => e.id === type); if (!typeEntry || !typeEntry.isDir) return out;
  for (const name of dirEntries(typeEntry.off)) {
    const langs = name.isDir ? dirEntries(name.off) : [name]; const first = langs[0]; if (!first) continue;
    const leaf = base + first.off; if (leaf + 16 > b.length) continue;
    const dataRva = u32(b, leaf), size = u32(b, leaf + 4); const off = rvaToOff(secs, dataRva);
    if (off < 0 || off + size > b.length) continue;
    out.set(name.id, b.subarray(off, off + size));
  }
  return out;
}

export interface PeIconEntry { w: number; h: number; bpp: number; bytes: Uint8Array; png: boolean }

/** all images of the first (or the requested) icon group */
export function peIconGroup(exe: Uint8Array, groupIndex = 0): PeIconEntry[] {
  const groups = resources(exe, RT_GROUP_ICON); if (!groups.size) return [];
  const icons = resources(exe, RT_ICON);
  const keys = [...groups.keys()].sort((a, b) => typeof a === 'number' && typeof b === 'number' ? a - b : String(a).localeCompare(String(b)));
  const key = groupIndex < 0 ? keys.find(k => k === -groupIndex) ?? keys[0] : keys[Math.min(groupIndex, keys.length - 1)];
  const g = groups.get(key); if (!g || g.length < 6) return [];
  const n = u16(g, 4); const out: PeIconEntry[] = [];
  for (let i = 0; i < n; i++) {
    const e = 6 + i * 14; if (e + 14 > g.length) break;
    const w = g[e] || 256, h = g[e + 1] || 256, bpp = u16(g, e + 6), id = u16(g, e + 12);
    const bytes = icons.get(id); if (!bytes) continue;
    out.push({ w, h, bpp, bytes, png: bytes.length > 8 && bytes[0] === 0x89 && bytes[1] === 0x50 && bytes[2] === 0x4e && bytes[3] === 0x47 });
  }
  return out;
}

/** decode a DIB icon image (BITMAPINFOHEADER + colour table + XOR bitmap + AND mask) into RGBA */
function decodeDib(e: PeIconEntry): ImageData | null {
  const b = e.bytes; if (b.length < 40) return null;
  const hdr = u32(b, 0), w = u32(b, 4) | 0, hh = u32(b, 8) | 0, bpp = u16(b, 14), comp = u32(b, 16);
  if (hdr < 40 || comp !== 0 || w <= 0 || w > 1024) return null;
  const h = Math.abs(hh) / 2 | 0; if (h <= 0 || h > 1024) return null;   // height counts XOR + AND
  let clr = u32(b, 32); if (bpp <= 8 && !clr) clr = 1 << bpp;
  const pal = hdr + (bpp <= 8 ? clr * 4 : 0);
  const stride = ((w * bpp + 31) >> 5) << 2, maskStride = ((w + 31) >> 5) << 2;
  const xor = pal, and = xor + stride * h;
  if (xor + stride * h > b.length) return null;
  const hasMask = and + maskStride * h <= b.length;
  const img = new ImageData(w, h); const d = img.data; let anyAlpha = false;
  for (let y = 0; y < h; y++) {
    const row = xor + (h - 1 - y) * stride, mrow = and + (h - 1 - y) * maskStride;
    for (let x = 0; x < w; x++) {
      let r = 0, g = 0, bl = 0, a = 255;
      if (bpp === 32) { const o = row + x * 4; bl = b[o]; g = b[o + 1]; r = b[o + 2]; a = b[o + 3]; if (a) anyAlpha = true; }
      else if (bpp === 24) { const o = row + x * 3; bl = b[o]; g = b[o + 1]; r = b[o + 2]; }
      else if (bpp === 16) { const v = u16(b, row + x * 2); r = ((v >> 10) & 31) << 3; g = ((v >> 5) & 31) << 3; bl = (v & 31) << 3; }
      else {
        let idx = 0;
        if (bpp === 8) idx = b[row + x]; else if (bpp === 4) idx = (b[row + (x >> 1)] >> ((x & 1) ? 0 : 4)) & 15; else if (bpp === 1) idx = (b[row + (x >> 3)] >> (7 - (x & 7))) & 1;
        const po = hdr + idx * 4; bl = b[po]; g = b[po + 1]; r = b[po + 2];
      }
      if (hasMask && (b[mrow + (x >> 3)] >> (7 - (x & 7))) & 1) a = 0;
      const o = (y * w + x) * 4; d[o] = r; d[o + 1] = g; d[o + 2] = bl; d[o + 3] = a;
    }
  }
  if (bpp === 32 && !anyAlpha) {   // alpha channel unused (old-style 32bpp icon): the AND mask alone decides transparency
    for (let y = 0; y < h; y++) { const mrow = and + (h - 1 - y) * maskStride; for (let x = 0; x < w; x++) { const o = (y * w + x) * 4; d[o + 3] = hasMask && (b[mrow + (x >> 3)] >> (7 - (x & 7))) & 1 ? 0 : 255; } }
  }
  return img;
}

function pngToImage(bytes: Uint8Array): Promise<HTMLImageElement | null> {
  return new Promise(res => { const url = URL.createObjectURL(new Blob([bytes.slice() as BlobPart], { type: 'image/png' })); const im = new Image(); im.onload = () => { URL.revokeObjectURL(url); res(im); }; im.onerror = () => { URL.revokeObjectURL(url); res(null); }; im.src = url; });
}

/** the program's main icon as a PNG data URL (≈ `size` px), or null when the exe carries no usable icon */
export async function peIconDataUrl(exe: Uint8Array, size = 128, groupIndex = 0): Promise<string | null> {
  let entries: PeIconEntry[];
  try { entries = peIconGroup(exe, groupIndex); } catch { return null; }
  if (!entries.length) return null;
  // best: the smallest image ≥ size with the highest colour depth; else the largest available
  const score = (e: PeIconEntry) => (e.w >= size ? 0 : 1_000_000) + (e.w >= size ? e.w - size : size - e.w) * 64 - e.bpp - (e.png ? 1 : 0);
  entries.sort((a, b) => score(a) - score(b));
  for (const e of entries) {
    try {
      const canvas = document.createElement('canvas'); canvas.width = size; canvas.height = size;
      const ctx = canvas.getContext('2d'); if (!ctx) return null; ctx.imageSmoothingQuality = 'high';
      if (e.png) { const im = await pngToImage(e.bytes); if (!im) continue; ctx.drawImage(im, 0, 0, size, size); }
      else { const img = decodeDib(e); if (!img) continue; const tmp = document.createElement('canvas'); tmp.width = img.width; tmp.height = img.height; tmp.getContext('2d')!.putImageData(img, 0, 0); if (img.width < size) ctx.imageSmoothingEnabled = img.width < 48 ? false : true; ctx.drawImage(tmp, 0, 0, size, size); }
      return canvas.toDataURL('image/png');
    } catch { /* try the next image of the group */ }
  }
  return null;
}
