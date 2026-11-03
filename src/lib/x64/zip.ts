// Minimal ZIP reader (central directory + stored/deflate entries) for installing portable programs and games
// from an archive on the device — on Android there is no folder picker, a .zip of the game folder is the natural
// unit. Deflate is inflated by the browser's own DecompressionStream('deflate-raw'); no stand-in code paths.

export interface ZipEntry { name: string; size: number; csize: number; method: number; offset: number; dir: boolean }

function u16(b: Uint8Array, o: number) { return b[o] | (b[o + 1] << 8); }
function u32(b: Uint8Array, o: number) { return (b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)) >>> 0; }

export function zipEntries(b: Uint8Array): ZipEntry[] {
  // End of central directory record: scan back for the signature (the comment may be up to 64 KB)
  let eocd = -1; for (let i = b.length - 22; i >= Math.max(0, b.length - 65557); i--) if (u32(b, i) === 0x06054b50) { eocd = i; break; }
  if (eocd < 0) throw new Error('not a zip file (no central directory)');
  let count = u16(b, eocd + 10), cdOff = u32(b, eocd + 16);
  if (count === 0xffff || cdOff === 0xffffffff) {   // ZIP64
    const loc = eocd - 20; if (loc < 0 || u32(b, loc) !== 0x07064b50) throw new Error('zip64 locator missing');
    const z64 = Number(new DataView(b.buffer, b.byteOffset).getBigUint64(loc + 8, true)); if (u32(b, z64) !== 0x06064b50) throw new Error('zip64 record missing');
    const dv = new DataView(b.buffer, b.byteOffset); count = Number(dv.getBigUint64(z64 + 32, true)); cdOff = Number(dv.getBigUint64(z64 + 48, true));
  }
  const out: ZipEntry[] = []; let p = cdOff; const dec = new TextDecoder('utf-8'), cp437 = new TextDecoder('latin1');
  for (let i = 0; i < count && p + 46 <= b.length; i++) {
    if (u32(b, p) !== 0x02014b50) break;
    const flags = u16(b, p + 8), method = u16(b, p + 10); let csize = u32(b, p + 20), size = u32(b, p + 24);
    const nlen = u16(b, p + 28), xlen = u16(b, p + 30), clen = u16(b, p + 32); let offset = u32(b, p + 42);
    const nameBytes = b.subarray(p + 46, p + 46 + nlen); const name = (flags & 0x800 ? dec : cp437).decode(nameBytes).replace(/\\/g, '/');
    if (size === 0xffffffff || csize === 0xffffffff || offset === 0xffffffff) {   // ZIP64 extra field
      let q = p + 46 + nlen; const end = q + xlen; const dv = new DataView(b.buffer, b.byteOffset);
      while (q + 4 <= end) { const id = u16(b, q), sz = u16(b, q + 2); if (id === 1) { let r = q + 4; if (size === 0xffffffff) { size = Number(dv.getBigUint64(r, true)); r += 8; } if (csize === 0xffffffff) { csize = Number(dv.getBigUint64(r, true)); r += 8; } if (offset === 0xffffffff) offset = Number(dv.getBigUint64(r, true)); break; } q += 4 + sz; }
    }
    out.push({ name, size, csize, method, offset, dir: name.endsWith('/') });
    p += 46 + nlen + xlen + clen;
  }
  return out;
}

export async function zipRead(b: Uint8Array, e: ZipEntry): Promise<Uint8Array> {
  if (u32(b, e.offset) !== 0x04034b50) throw new Error(`${e.name}: bad local header`);
  const nlen = u16(b, e.offset + 26), xlen = u16(b, e.offset + 28); const start = e.offset + 30 + nlen + xlen;
  const raw = b.subarray(start, start + e.csize);
  if (e.method === 0) return raw.slice();
  if (e.method !== 8) throw new Error(`${e.name}: unsupported compression method ${e.method}`);
  if (typeof DecompressionStream === 'undefined') throw new Error('this WebView has no DecompressionStream (deflate)');
  const ds = new DecompressionStream('deflate-raw');
  const stream = new Blob([raw.slice() as BlobPart]).stream().pipeThrough(ds);
  const out = new Uint8Array(await new Response(stream).arrayBuffer());
  if (e.size && out.length !== e.size) throw new Error(`${e.name}: inflated ${out.length} bytes, expected ${e.size}`);
  return out;
}
