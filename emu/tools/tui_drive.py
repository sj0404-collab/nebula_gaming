#!/usr/bin/env python3
"""Drive the opencode TUI running inside the Nebula x64 emulator (used by .github/workflows/opencode-x64.yml).

  tui_drive.py <tui-output-file> <stdin-fifo> <out-dir>

Waits for the first rendered frame, types a prompt, waits for the model's answer, then asks the agent to run a
shell command through its bash tool (child process → cmd.exe inside the emulator) and waits for that output.
Every stage writes a text dump + PNG of the terminal to <out-dir>; exit code 0 = first frame + answer seen,
2 = first frame seen but no answer, 3 = no frame at all."""
import sys, time, os, re, subprocess, pyte

out_path, fifo, outdir = sys.argv[1], sys.argv[2], sys.argv[3]
COLS, ROWS = 120, 40
HERE = os.path.dirname(os.path.abspath(__file__))
os.makedirs(outdir, exist_ok=True)
t0 = time.time()

def screen_text():
    data = open(out_path, 'rb').read()
    scr = pyte.Screen(COLS, ROWS); pyte.ByteStream(scr).feed(data)
    return '\n'.join(scr.display)

def snapshot(name):
    png = os.path.join(outdir, name + '.png')
    txt = subprocess.run([sys.executable, os.path.join(HERE, 'vt_render.py'), out_path, str(COLS), str(ROWS), png], capture_output=True, text=True).stdout
    open(os.path.join(outdir, name + '.txt'), 'w').write(txt)
    print(f'[drive +{time.time()-t0:6.0f}s] snapshot {name}', flush=True)

def wait_for(pred, timeout, label):
    end = time.time() + timeout
    while time.time() < end:
        try:
            s = screen_text()
        except FileNotFoundError:
            s = ''
        if pred(s): print(f'[drive +{time.time()-t0:6.0f}s] {label}: ok', flush=True); return True
        time.sleep(15)
    print(f'[drive +{time.time()-t0:6.0f}s] {label}: TIMEOUT', flush=True); return False

def send(b):
    with open(fifo, 'wb', buffering=0) as w: w.write(b)

# 1. first frame: the home screen has the big logo + prompt; look for a healthy amount of text
if not wait_for(lambda s: ('ctrl+p' in s or 'Ask anything' in s) and sum(1 for c in s if not c.isspace()) > 100, 40 * 60, 'first frame'):
    snapshot('no-frame'); sys.exit(3)
time.sleep(20); snapshot('1-home')

# 2. a prompt with a checkable answer
send(b'What is 123*457? Reply with just the number.'); time.sleep(20); snapshot('2-typed')
send(b'\r')
got = wait_for(lambda s: '56211' in s.replace(',', '').replace(' ', ''), 25 * 60, 'LLM answer 56211')
snapshot('3-answer')

# 3. bash tool: the agent must spawn a child process (cmd.exe inside the emulator) and show its output
send(b'Run the shell command: echo nebula-bash-ok   and tell me exactly what it printed.'); time.sleep(10); send(b'\r')
# the TUI shows the prompt (1), the tool call `$ echo nebula-bash-ok` (2), then the command output / the final
# answer (3+); the turn is finished when a second '▣  Build · <model> · <time>' status line appears
done_turn = lambda s: len(re.findall(r'▣\s+Build · [^\n·]+ · \d+(?:\.\d+)?[ms]', s)) >= 2
tool = wait_for(lambda s: s.count('nebula-bash-ok') >= 3 or done_turn(s), 25 * 60, 'bash tool output')
if tool: wait_for(done_turn, 10 * 60, 'bash turn finished')
snapshot('4-bash-tool')
print(f'RESULT first_frame=1 answer={int(got)} bash_tool={int(tool)}', flush=True)
sys.exit(0 if got else 2)
