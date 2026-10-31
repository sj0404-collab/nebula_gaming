import sys, time, os, re
out_path, fifo = sys.argv[1], sys.argv[2]
w = open(fifo, 'wb', buffering=0)
seen = 0
# replies for terminal queries OpenTUI sends on startup
rules = [
    (b'\x1b[6n', b'\x1b[1;1R'),
    (b'\x1b[14t', b'\x1b[4;720;1440t'),
    (b'\x1b[18t', b'\x1b[8;40;120t'),
    (b'\x1b[c', b'\x1b[?62;22c'),
    (b'\x1b[>0q', b'\x1bP>|xterm(390)\x1b\\'),
    (b'\x1b[?u', b'\x1b[?0u'),
    (b'\x1b]10;?\x07', b'\x1b]10;rgb:ffff/ffff/ffff\x1b\\'),
    (b'\x1b]11;?\x07', b'\x1b]11;rgb:0000/0000/0000\x1b\\'),
]
counts = {k: 0 for k, _ in rules}
pos = 0
log = open(os.environ.get('TUI_RESPOND_LOG', '/tmp/respond.log'), 'ab', buffering=0)
while True:
    try:
        with open(out_path, 'rb') as f:
            f.seek(pos); data = f.read()
    except FileNotFoundError:
        time.sleep(0.5); continue
    if data:
        pos += len(data)
        for k, v in rules:
            n = data.count(k)
            for _ in range(n):
                w.write(v); log.write(b'[%s] reply for %r\n' % (time.strftime('%H:%M:%S').encode(), k))
        # DECRQM queries: ESC[?NNNN$p -> ESC[?NNNN;2$y (reset/supported)
        for m in re.finditer(rb'\x1b\[\?(\d+)\$p', data):
            w.write(b'\x1b[?' + m.group(1) + b';2$y')
    time.sleep(0.2)
