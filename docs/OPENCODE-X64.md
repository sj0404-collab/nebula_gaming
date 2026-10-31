# opencode (Bun standalone, Windows x64) inside Nebula x64

Reproducible recipe used for the screenshots in `docs/screenshots/` (native runner, 2 cores / 2 GB sandbox):

```bash
# 1. build the emulator
./emu/x64/nbuild.sh && ./emu/tests/run.sh          # 110 checks, ALL OK

# 2. get the real opencode binary (Bun 1.4.4 standalone, 211 MB)
curl -sL https://registry.npmjs.org/opencode-windows-x64-baseline/-/opencode-windows-x64-baseline-1.19.5.tgz | tar xz
# optional: a home directory with rg.exe so opencode does not download ripgrep
mkdir -p home/.cache/opencode/bin && cp public/x64/apps/rg.exe home/.cache/opencode/bin/

# 3. terminal query responder (OpenTUI asks the terminal for size/colours/capabilities over stdin)
mkfifo in.fifo
python3 emu/tools/tui_respond.py tui.out in.fifo &

# 4. run the TUI (first frame after ~9 min / 42 G instructions at ~65 MIPS)
X64_GUEST_ENV="OPENCODE_DISABLE_MODELS_FETCH=1;OPENCODE_DISABLE_AUTOUPDATE=1;OPENCODE_EXPERIMENTAL_DISABLE_FILEWATCHER=1;OPENCODE_DISABLE_FFF=1" \
X64_TAP='opencode\log' X64_LOG=1 \
./emu/x64/x64run --max-ms 3600000 --mount "$PWD/home=C:\Users\user" package/bin/opencode.exe > tui.out 2> tui.err 3>&2 < in.fifo

# 5. type into it (keyboard goes through the FIFO) and render the screen to PNG
printf 'Reply with exactly one short sentence: what is 2+2?' > in.fifo; sleep 30; printf '\r' > in.fifo
python3 emu/tools/vt_render.py tui.out 120 40 screen.png
```

Measured on 2026‑10‑31 (run #24): instance 08:36 → config 08:41 → `init` 08:41:31 → `init count=1` 08:47 → first frame 08:47:30 →
prompt typed 08:48:50 → session created 08:50:06 → request to `opencode.ai` (Big Pickle) 08:55:39 → **answer «2 + 2 equals 4.» rendered 08:57:33** (6 m 42 s on the opencode side).
Peak RSS 1.1 GB (JSC heap with 4 GB reported RAM), 13 guest threads, ~190 short‑lived libuv wait threads.

What had to be implemented for this (all in `emu/x64/`):
`Nt*` file/VM services and PEB `ProcessParameters` (Zig std + Bun), Winsock + IOCP + `select`/`WSAPoll` (libuv slow‑poll used by uSockets), `recv(s,NULL,0)` connection probe,
exe‑export lookup in `GetProcAddress` (Node‑API add‑on delay‑loads `napi_*` from `node.exe` = the host exe), waitable console input handle + `RegisterWaitForSingleObject`
(libuv raw tty reader), UTF‑8 → UTF‑16 key records, non‑blocking raw stdin in the native runner, `X64_TAP` (mirror guest file writes to stderr) for the opencode log.

Limits: in the browser (wasm64) there is no raw TCP, so the LLM request cannot leave the page, and the interpreter is ~2× slower than native — the TUI would need ~20 min to its first frame and >1 GB; the native runner is the practical host for it today.
