#!/usr/bin/env python3
"""Turn validated vendor archives into individually compressed, versioned virtual-disk packs."""
import gzip, hashlib, json, pathlib, sys, zipfile
src = pathlib.Path(sys.argv[1]); out = pathlib.Path(sys.argv[2]); vendor = json.loads((src / 'manifest.json').read_text())

def emit(key, title, archive, selected, provenance):
    dest = out / key; dest.mkdir(parents=True, exist_ok=True); files = []; seen = set()
    with zipfile.ZipFile(src / archive) as z:
        for original, rel in selected(z.namelist()):
            parts = rel.split('/')
            if any(not p or p in ('.', '..') or any(c in p for c in '\\:*?"<>|') for p in parts): raise ValueError('unsafe path: ' + rel)
            if rel.lower() in seen: raise ValueError('duplicate path: ' + rel)
            seen.add(rel.lower()); b = z.read(original)
            target = dest / (rel + '.nebula-asset.gz'); target.parent.mkdir(parents=True, exist_ok=True)
            with target.open('wb') as raw:
                with gzip.GzipFile(filename='', mode='wb', fileobj=raw, mtime=0) as f: f.write(b)
            files.append({'path': rel, 'size': len(b), 'gz': rel + '.nebula-asset.gz', 'sha256': hashlib.sha256(b).hexdigest()})
    if not files: raise ValueError('empty pack')
    man = {'id': key, 'pack': title, 'version': provenance['sha256'][:16], 'source': provenance['source'], 'archive_sha256': provenance['sha256'], 'checksum_verified_against': provenance['checksum_verified_against'], 'files': files}
    (dest / 'manifest.json').write_text(json.dumps(man, indent=2) + '\n')
    print(key, len(files), sum(f['size'] for f in files), man['version'])

def java(names):
    roots = {n.split('/')[0] for n in names if '/' in n}
    if len(roots) != 1: raise ValueError('JDK archive must have exactly one root')
    root = next(iter(roots)) + '/'
    return [(n, n[len(root):]) for n in names if n.startswith(root) and not n.endswith('/')]

def audio(arch):
    def select(names):
        dll = vendor['openal']['dlls'][arch]['path']
        # Include original licensing documentation with each DLL.
        docs = [n for n in names if not n.endswith('/') and ('copying' in n.lower() or 'license' in n.lower()) and '/' in n]
        return [(dll, 'OpenAL32.dll')] + [(n, 'licenses/' + str(i) + '-' + n.split('/')[-1]) for i, n in enumerate(docs)]
    return select
emit('jdk17-x64', 'Eclipse Temurin JDK 17 Windows x64', vendor['java']['file'], java, vendor['java'])
for arch, key in [('Win32', 'openal-x86'), ('Win64', 'openal-x64')]: emit(key, 'OpenAL Soft ' + arch, vendor['openal']['file'], audio(arch), vendor['openal'])
