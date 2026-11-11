#!/usr/bin/env python3
"""Fetch official Windows runtimes for later integration; never overwrite a game's files."""
import hashlib, json, os, pathlib, struct, sys, urllib.request, zipfile

# JAVA_FEATURE=none (default): OpenAL Soft only — the JDK (~190 MB) is not shipped in the APK; 17/21 fetch the Temurin JDK as well.
feature = os.environ.get('JAVA_FEATURE', 'none')
if feature not in {'none', '17', '21'}:
    raise SystemExit('JAVA_FEATURE must be none, 17 or 21')
out = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else 'official-components')
out.mkdir(parents=True, exist_ok=True)

def fetch(url, path):
    req = urllib.request.Request(url, headers={'User-Agent': 'NebulaOS-component-builder'})
    with urllib.request.urlopen(req, timeout=120) as response, path.open('wb') as dst:
        while chunk := response.read(1024 * 1024): dst.write(chunk)

def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as src:
        while chunk := src.read(1024 * 1024): h.update(chunk)
    return h.hexdigest()

def pe_machine(blob):
    if blob[:2] != b'MZ' or len(blob) < 64: raise ValueError('not an MZ image')
    p = struct.unpack_from('<I', blob, 0x3c)[0]
    if p + 6 > len(blob) or blob[p:p+4] != b'PE\0\0': raise ValueError('not a PE image')
    return struct.unpack_from('<H', blob, p+4)[0]

java_entry = None
if feature != 'none':
  api = f'https://api.adoptium.net/v3/assets/latest/{feature}/hotspot?architecture=x64&image_type=jdk&os=windows&vendor=eclipse'
  meta = out / 'adoptium-api.json'
  fetch(api, meta)
  assets = json.loads(meta.read_text())
  if not assets: raise SystemExit('Adoptium returned no matching Windows x64 JDK')
  pkg = assets[0]['binary']['package']
  url = pkg['link']
  if not url.startswith(f'https://github.com/adoptium/temurin{feature}-binaries/releases/download/'):
      raise SystemExit('Unexpected JDK download origin')
  expected = pkg['checksum'].lower()
  if len(expected) != 64: raise SystemExit('Missing upstream SHA-256')
  jre = out / pkg['name']
  fetch(url, jre)
  actual = digest(jre)
  if actual != expected: raise SystemExit('JDK SHA-256 does not match Adoptium')
  with zipfile.ZipFile(jre) as z:
      java = [n for n in z.namelist() if n.endswith('/bin/java.exe')]
      if len(java) != 1 or pe_machine(z.read(java[0])) != 0x8664:
          raise SystemExit('JDK archive does not contain exactly one Windows x64 java.exe')
      if not any(n.endswith('/release') for n in z.namelist()): raise SystemExit('JDK release metadata missing')
  java_entry = {'feature': feature, 'source_api': api, 'source': url, 'file': jre.name, 'sha256': actual, 'checksum_verified_against': 'Adoptium API', 'java_exe': java[0]}

# Project's official binary distribution. The archive hash below identifies the received
# file; unlike the JDK checksum, it is NOT an independently supplied publisher digest.
openal_url = 'https://openal-soft.org/openal-binaries/openal-soft-1.25.2-bin.zip'
openal = out / 'openal-soft-1.25.2-bin.zip'
fetch(openal_url, openal)
with zipfile.ZipFile(openal) as z:
    names = z.namelist()
    checks = {}
    for arch, machine in [('Win32', 0x14c), ('Win64', 0x8664)]:
        hits = [n for n in names if n.lower().endswith(f'/bin/{arch.lower()}/soft_oal.dll')]
        if len(hits) != 1 or pe_machine(z.read(hits[0])) != machine: raise SystemExit(f'OpenAL {arch} DLL missing or wrong architecture')
        checks[arch] = {'path': hits[0], 'sha256': hashlib.sha256(z.read(hits[0])).hexdigest()}
    if not any('copying' in n.lower() or 'license' in n.lower() for n in names): raise SystemExit('OpenAL license missing from archive')
manifest = {'java': java_entry,
            'openal': {'source': openal_url, 'file': openal.name, 'sha256': digest(openal), 'checksum_verified_against': None, 'dlls': checks},
            'note': 'Unmodified vendor archives; not installed, not connected to the launcher, no files in any game are replaced.'}
(out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
print(json.dumps(manifest, indent=2))
