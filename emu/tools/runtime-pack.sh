#!/usr/bin/env bash
# runtime-pack.sh — builds the Windows "runtime packs" served at /x64/sys32/ (→ C:\Windows\System32 / SysWOW64 inside the
# emulator).  Every pack is extracted at build time from the OFFICIAL Microsoft redistributable (nothing is committed):
#
#   /x64/sys32/*.dll + manifest.json          VC++ 2015–2022 x64 (the original pack; kept flat for compatibility)
#   /x64/sys32/<pack>/*.dll + manifest.json   vc2005-x86/x64, vc2008-x86/x64, vc2010-x86/x64, vc2012-x86/x64, vc2013-x86/x64,
#                                             vc2015-x86
#   /x64/sys32/packs.json                     index: every pack with label, arch, guest dir, file list, versions, source URL
#
# The DLLs are identified by their own VERSIONINFO (OriginalFilename) and PE machine type, not by the compressed-file names
# inside the installers (2005/2008: cab inside an IExpress exe + MSI; 2010: vc_red.msi + vc_red.cab in a self-extractor;
# 2012/2013/2015+: a Burn bundle whose payloads are MSIs + cabinets).  The emulator prefers these real DLLs over its built-in
# shims for the vendor-runtime names (pe_dll_native_ok in emu/x64/pe.c); core OS DLLs stay built-in.
#
# Usage: emu/tools/runtime-pack.sh [outdir]      (needs curl, cabextract, 7z, msiextract (msitools), python3)
set -euo pipefail
OUT=${1:-public/x64/sys32}
URL=${VC_REDIST_URL:-https://aka.ms/vs/17/release/vc_redist.x64.exe}
work=$(mktemp -d); trap 'rm -rf "$work"' EXIT
mkdir -p "$OUT"

fetch() {   # fetch <url> <file>
  echo "runtime-pack: downloading $1"
  curl -fL --retry 8 --retry-delay 5 --retry-all-errors -C - -o "$2" "$1" || curl -fL --retry 3 -o "$2" "$1"
  ls -la "$2"
}
# extract <file> <dir>: unpack an installer / cabinet / MSI and everything nested inside it (3 levels)
extract() {
  local src=$1 dst=$2 depth=${3:-0}; mkdir -p "$dst"
  local kind; kind=$(file -b "$src" 2>/dev/null || true)
  case "$kind" in
    *"Cabinet archive"*) cabextract -q -d "$dst" "$src" 2>/dev/null || 7z x -y -o"$dst" "$src" >/dev/null 2>&1 || true ;;
    *"Composite Document File"*|*"MSI Installer"*) msiextract -C "$dst" "$src" >/dev/null 2>&1 || 7z x -y -o"$dst" "$src" >/dev/null 2>&1 || true ;;
    *PE32*) if cabextract -t "$src" >/dev/null 2>&1; then cabextract -q -d "$dst" "$src" 2>/dev/null || true; fi
            7z x -y -o"$dst/7z" "$src" >/dev/null 2>&1 || true ;;
    *) 7z x -y -o"$dst" "$src" >/dev/null 2>&1 || true ;;
  esac
  [ "$depth" -ge 3 ] && return 0
  local f list=()
  mapfile -d '' list < <(find "$dst" -maxdepth 4 -type f -size +4k -print0)   # the list is taken before anything nested is unpacked
  for f in "${list[@]}"; do
    case "$(file -b "$f" 2>/dev/null || true)" in
      *"Cabinet archive"*|*"Composite Document File"*|*"MSI Installer"*) extract "$f" "$f.d" $((depth + 1)) ;;
      *PE32*) if cabextract -t "$f" >/dev/null 2>&1; then extract "$f" "$f.d" $((depth + 1)); fi ;;
    esac
  done
}

# ---- 1. VC++ 2015–2022 x64: the original flat pack (the .dll_amd64 payload names are reliable here) ----
fetch "$URL" "$work/vc_redist.x64.exe"
cabextract -q -d "$work/s1" "$work/vc_redist.x64.exe"
for f in "$work"/s1/*; do cabextract -q -d "$work/s2" "$f" 2>/dev/null || true; done
n=0
for f in "$work"/s2/*.dll_amd64; do b=$(basename "$f"); cp "$f" "$OUT/${b%_amd64}"; n=$((n + 1)); done
[ "$n" -gt 0 ] || { echo "runtime-pack: no amd64 DLLs found in the redistributable" >&2; exit 1; }

# ---- 2. every other pack: download, unpack recursively, pick the DLLs by their own identity ----
# id|label|arch|guest dir|url
PACKS='
vc2015-x86|Microsoft Visual C++ 2015-2022 Redistributable (x86)|x86|C:\Windows\SysWOW64|https://aka.ms/vs/17/release/vc_redist.x86.exe
vc2013-x86|Microsoft Visual C++ 2013 Redistributable (x86) 12.0.40664|x86|C:\Windows\SysWOW64|https://aka.ms/highdpimfc2013x86enu
vc2013-x64|Microsoft Visual C++ 2013 Redistributable (x64) 12.0.40664|x64|C:\Windows\System32|https://aka.ms/highdpimfc2013x64enu
vc2012-x86|Microsoft Visual C++ 2012 Redistributable (x86) Update 4|x86|C:\Windows\SysWOW64|https://download.microsoft.com/download/1/6/B/16B06F60-3B20-4FF2-B699-5E9B7962F9AE/VSU_4/vcredist_x86.exe
vc2012-x64|Microsoft Visual C++ 2012 Redistributable (x64) Update 4|x64|C:\Windows\System32|https://download.microsoft.com/download/1/6/B/16B06F60-3B20-4FF2-B699-5E9B7962F9AE/VSU_4/vcredist_x64.exe
vc2010-x86|Microsoft Visual C++ 2010 SP1 Redistributable (x86)|x86|C:\Windows\SysWOW64|https://download.microsoft.com/download/1/6/5/165255E7-1014-4D0A-B094-B6A430A6BFFC/vcredist_x86.exe
vc2010-x64|Microsoft Visual C++ 2010 SP1 Redistributable (x64)|x64|C:\Windows\System32|https://download.microsoft.com/download/1/6/5/165255E7-1014-4D0A-B094-B6A430A6BFFC/vcredist_x64.exe
vc2008-x86|Microsoft Visual C++ 2008 SP1 Redistributable (x86)|x86|C:\Windows\SysWOW64|https://download.microsoft.com/download/5/D/8/5D8C65CB-C849-4025-8E95-C3966CAFD8AE/vcredist_x86.exe
vc2008-x64|Microsoft Visual C++ 2008 SP1 Redistributable (x64)|x64|C:\Windows\System32|https://download.microsoft.com/download/5/D/8/5D8C65CB-C849-4025-8E95-C3966CAFD8AE/vcredist_x64.exe
vc2005-x86|Microsoft Visual C++ 2005 SP1 Redistributable (x86)|x86|C:\Windows\SysWOW64|https://download.microsoft.com/download/8/B/4/8B42259F-5D70-43F4-AC2E-4B208FD8D66A/vcredist_x86.EXE
vc2005-x64|Microsoft Visual C++ 2005 SP1 Redistributable (x64)|x64|C:\Windows\System32|https://download.microsoft.com/download/8/B/4/8B42259F-5D70-43F4-AC2E-4B208FD8D66A/vcredist_x64.EXE
'
echo "$PACKS" | while IFS='|' read -r id label arch gdir url; do
  [ -n "$id" ] || continue
  d="$work/$id"; mkdir -p "$d"
  fetch "$url" "$d/setup.exe"
  extract "$d/setup.exe" "$d/x"
  python3 - "$d/x" "$OUT/$id" "$id" "$label" "$arch" "$gdir" "$url" "$d/setup.exe" <<'PY'
import sys, os, re, json, hashlib, struct, time, shutil
src, out, pid, label, arch, gdir, url, setup = sys.argv[1:9]
want_machine = 0x8664 if arch == 'x64' else 0x14c
def pe_machine(d):
    if len(d) < 0x40 or d[:2] != b'MZ': return 0
    pe = struct.unpack_from('<I', d, 0x3c)[0]
    if pe + 6 > len(d) or d[pe:pe+4] != b'PE\0\0': return 0
    return struct.unpack_from('<H', d, pe + 4)[0]
def is_dll(d):
    pe = struct.unpack_from('<I', d, 0x3c)[0]; ch = struct.unpack_from('<H', d, pe + 22)[0]; return bool(ch & 0x2000)
def ver_strings(d):
    # VS_VERSION_INFO: fixed info (dwFileVersion) + StringFileInfo (OriginalFilename / InternalName), UTF-16LE
    key = 'VS_VERSION_INFO'.encode('utf-16-le'); i = d.find(key); fv = ''; names = {}
    if i < 0: return fv, names
    j = d.find(b'\xbd\x04\xef\xfe', i, i + 64)
    if j > 0:
        ms, ls = struct.unpack_from('<II', d, j + 8); fv = f'{ms >> 16}.{ms & 0xffff}.{ls >> 16}.{ls & 0xffff}'
    for k in ('OriginalFilename', 'InternalName'):
        kk = k.encode('utf-16-le'); p = d.find(kk, i, i + 4096)
        if p > 0:
            p += len(kk) + 2
            while p < len(d) and d[p:p+2] == b'\0\0': p += 2
            e = d.find(b'\0\0', p, p + 256)
            if e > 0 and (e - p) % 2: e += 1
            try: names[k] = d[p:e].decode('utf-16-le', 'ignore').strip('\0 ')
            except Exception: pass
    return fv, names
skip = re.compile(r'^mfc\d+(chs|cht|deu|enu|esn|esp|fra|ita|jpn|kor|rus)\.dll$', re.I)   # MFC resource-only language DLLs
# the runtime DLLs proper; not the installer's own helpers (setup*.dll, setupengine, sqmapi, msdia*)
want = re.compile(r'^(msvcr\d+(_\w+)?|msvcp\d+(_\w+)?|msvcm\d+|mfc\d+u?|mfcm\d+u?|atl\d+|vcomp\d*|vccorlib\d+|vcamp\d+|concrt\d+|vcruntime\d+(_\w+)?|ucrtbase|api-ms-win-[\w.-]+|vcomp\d+d?|msvcirt|msvcp\d+d|vcclrit)\.dll$', re.I)
def name_of(fname, names):
    # 1. a real file name (msiextract output, .dll_x86 payload names); 2. cabinet member keys F_CENTRAL_<name>_<arch>; 3. VERSIONINFO
    b = os.path.basename(fname).lower()
    m = re.match(r'^(.*\.dll)(_x86|_amd64|_ia64|_arm64)?$', b)
    if m: return m.group(1)
    m = re.match(r'^f_(?:central|nocentral|[a-z]+)_(.+?)_(x86|x64|amd64|ia64)$', b)
    if m: return m.group(1) + '.dll'
    n = (names.get('OriginalFilename') or names.get('InternalName') or '').lower()
    if n and not n.endswith('.dll'): n += '.dll'
    return os.path.basename(n.replace('\\', '/')) if n else ''
picked = {}
for root, _, files in os.walk(src):
    for n in files:
        p = os.path.join(root, n)
        try: d = open(p, 'rb').read()
        except Exception: continue
        m = pe_machine(d)
        if m != want_machine or not is_dll(d): continue
        fv, names = ver_strings(d)
        name = name_of(n, names)
        if not name or skip.match(name) or not want.match(name): continue
        vt = tuple(int(x) for x in fv.split('.')) if fv else (0,)
        cur = picked.get(name)
        if cur is None or vt > cur[0]: picked[name] = (vt, p, fv, d)
if not picked:
    print(f'runtime-pack: {pid}: no {arch} DLLs found under {src}', file=sys.stderr); sys.exit(1)
need = {'vc2005': ['msvcr80.dll', 'msvcp80.dll', 'mfc80.dll'], 'vc2008': ['msvcr90.dll', 'msvcp90.dll', 'mfc90.dll'], 'vc2010': ['msvcr100.dll', 'msvcp100.dll', 'mfc100.dll'],
        'vc2012': ['msvcr110.dll', 'msvcp110.dll', 'mfc110.dll'], 'vc2013': ['msvcr120.dll', 'msvcp120.dll', 'mfc120.dll'], 'vc2015': ['msvcp140.dll', 'vcruntime140.dll', 'concrt140.dll', 'mfc140.dll']}[pid.split('-')[0]]
missing = [n for n in need if n not in picked]
if missing:
    print(f'runtime-pack: {pid}: required DLLs missing: {missing} (found: {sorted(picked)})', file=sys.stderr); sys.exit(1)
os.makedirs(out, exist_ok=True)
files = []
for name in sorted(picked):
    _, p, fv, d = picked[name]
    open(os.path.join(out, name), 'wb').write(d)
    files.append({'name': name, 'size': len(d), 'sha256': hashlib.sha256(d).hexdigest(), 'version': fv})
man = {'id': pid, 'pack': label, 'arch': arch, 'guest_dir': gdir, 'source': url, 'bundle_sha256': hashlib.sha256(open(setup, 'rb').read()).hexdigest(),
       'built': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()), 'files': files}
json.dump(man, open(os.path.join(out, 'manifest.json'), 'w'), indent=1)
print(f"runtime-pack: {pid}: {len(files)} DLLs, {sum(f['size'] for f in files) / 1048576:.1f} MB")
for f in files: print(f"  {f['name']:32} {f['size']:>9}  {f['version']}")
PY
  rm -rf "$d"
done

# ---- 3. manifests: the flat x64 pack and the index of every pack ----
python3 - "$OUT" "$URL" "$work/vc_redist.x64.exe" <<'EOF'
import sys, os, json, hashlib, struct, time
out, url, bundle = sys.argv[1], sys.argv[2], sys.argv[3]
def file_version(data):
    key = 'VS_VERSION_INFO'.encode('utf-16-le')
    i = data.find(key)
    while i >= 0:
        j = data.find(b'\xbd\x04\xef\xfe', i, i + 64)
        if j > 0:
            ms, ls = struct.unpack_from('<II', data, j + 8)
            return f'{ms >> 16}.{ms & 0xffff}.{ls >> 16}.{ls & 0xffff}'
        i = data.find(key, i + 1)
    return ''
files = []
for name in sorted(os.listdir(out)):
    if not name.lower().endswith('.dll'): continue
    d = open(os.path.join(out, name), 'rb').read()
    files.append({ 'name': name, 'size': len(d), 'sha256': hashlib.sha256(d).hexdigest(), 'version': file_version(d) })
man = { 'id': 'vc2015-x64', 'pack': 'Microsoft Visual C++ 2015-2022 Redistributable (x64)', 'arch': 'x64', 'source': url, 'bundle_sha256': hashlib.sha256(open(bundle, 'rb').read()).hexdigest(),
        'built': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()), 'guest_dir': 'C:\\Windows\\System32', 'files': files }
json.dump(man, open(os.path.join(out, 'manifest.json'), 'w'), indent=1)
print(f"runtime-pack: vc2015-x64: {len(files)} DLLs, {sum(f['size'] for f in files) / 1048576:.1f} MB → {out}")
for f in files: print(f"  {f['name']:32} {f['size']:>9}  {f['version']}")
# the index: newest first, x64 before x86
order = ['vc2015-x64', 'vc2015-x86', 'vc2013-x64', 'vc2013-x86', 'vc2012-x64', 'vc2012-x86', 'vc2010-x64', 'vc2010-x86', 'vc2008-x64', 'vc2008-x86', 'vc2005-x64', 'vc2005-x86']
packs = []
for pid in order:
    mp = os.path.join(out, 'manifest.json') if pid == 'vc2015-x64' else os.path.join(out, pid, 'manifest.json')
    if not os.path.exists(mp): continue
    m = json.load(open(mp))
    packs.append({ 'id': pid, 'label': m['pack'], 'arch': m['arch'], 'guest_dir': m['guest_dir'], 'base': '' if pid == 'vc2015-x64' else pid + '/',
                   'source': m['source'], 'files': [{ 'name': f['name'], 'size': f['size'], 'version': f['version'] } for f in m['files']] })
json.dump({ 'built': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()), 'packs': packs }, open(os.path.join(out, 'packs.json'), 'w'), indent=1)
print('runtime-pack: packs.json: ' + ', '.join(f"{p['id']} ({len(p['files'])} files, {sum(f['size'] for f in p['files']) / 1048576:.1f} MB)" for p in packs))
EOF
