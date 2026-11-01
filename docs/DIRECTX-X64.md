# DirectX inside the Nebula x64 emulator (the "DirectX pack")

Games and tools built against the *DirectX SDK* (2005–2010) import helper DLLs that are **not part of Windows**:
`d3dx9_NN.dll`, `d3dx10_NN.dll`, `d3dx11_NN.dll`, `D3DCompiler_NN.dll`, `d3dcsx_NN.dll`, `X3DAudio1_N.dll`, `XAudio2_N.dll`,
`xinput1_N.dll`. On a PC the *DirectX End-User Runtimes* installer puts them into `C:\Windows\System32`; without them a
program stops with *"d3dx9_43.dll is missing"*. The emulator does not re-implement them: it runs the **real Microsoft
DLLs** (unmodified, taken from the official `directx_Jun2010_redist.exe`) as ordinary x64 code on top of its Win32
layer, exactly like the VC++ runtime pack and the .NET pack.

## What the pack is

`emu/tools/dx-pack.sh` downloads
`https://download.microsoft.com/download/8/4/A/84A35BF1-DAFE-4AE8-82AF-AD2AE20B6B14/directx_Jun2010_redist.exe` (95.6 MB),
extracts the `*_x64.cab` cabinets with `cabextract`, gzips every DLL and writes `public/x64/dx/<name>.dll.gz` +
`manifest.json` (`{pack, source, bundle_sha256, built, guest_dir: "C:\Windows\System32", encoding: "gzip",
files:[{path,size,gz,gzsize,sha256,version}]}` — the same layout as the .NET pack). CI job **DirectX pack** builds it
on every run (artifact `dx-pack`); the web build, the desktop/Android builds and the e2e tests download that artifact.

| family | files | what it is | status on the emulator |
|---|---|---|---|
| `d3dcompiler_33…36`, `D3DCompiler_37…43` | 11 | the HLSL compiler (`D3DCompile`, `D3DDisassemble`, `D3DReflect` …) | **works** — `D3DCompiler_43` compiles `vs_4_0`/`ps_4_0` to valid DXBC, which the D3D11 → WebGL2 layer then draws |
| `d3dx9_24…43` | 20 | D3DX for Direct3D 9: math, texture loaders, effects, meshes | math/utility exports **work** (`D3DXMatrixPerspectiveFovLH`, `D3DXMatrixInverse`, `D3DXVec3Normalize` …); anything that needs an `IDirect3DDevice9` cannot work because there is **no Direct3D 9 device** here (only D3D11 is translated to WebGL2) |
| `d3dx10`, `d3dx10_33…43` | 12 | D3DX for Direct3D 10 | loads and runs its `DllMain`; no D3D10 device |
| `d3dx11_42/43` | 2 | D3DX for Direct3D 11 (`D3DX11CompileFromMemory/File`, texture loaders) | **works** on top of the compiler + the D3D11 layer (`D3DX11CompileFromMemory` → 444 B DXBC in the test) |
| `d3dcsx_42/43` | 2 | DirectCompute helpers (FFT, scan) | loads; compute shaders are not translated to WebGL2 (no compute in WebGL2), so `D3DX11CreateFFT` fails honestly |
| `x3daudio1_2`, `X3DAudio1_3…1_7` | 6 | 3-D audio math (`X3DAudioInitialize`, `X3DAudioCalculate`) | **works** (emitter at +x → `left 0.000 right 1.000`) |

Total: **53 DLLs, 116.3 MB (49.5 MB gzipped)**. Everything is installed with one click (**DirectX** button in x64
Runner) or automatically when a program's import table names one of these DLLs; the files land in
`C:\Windows\System32` of the virtual disk and are found by `LoadLibrary`/the loader through the normal Windows search
order (exe directory → `System32` → cwd → `PATH`).

### Deliberately NOT shipped

* **XAudio2_0…2_7, xactengine2/3, XAPOFX1** — they create audio devices through MMDevice/WASAPI; the emulator has no
  audio device, so they would only fail later and less clearly. Not shipped; the honest answer at `XAudio2Create` is
  *unimplemented API … install the DirectX pack* → no.
* **xinput1_1…1_3, xinput9_1_0** — the real DLLs enumerate HID devices through `SetupAPI`; there is no game pad in a
  browser tab the way Windows sees it. The emulator answers XInput **itself**: `XInputGetState` → `ERROR_DEVICE_NOT_CONNECTED`
  (1167) for every slot, `XInputEnable` → ok, everything else → 1167. Games that poll XInput keep running with keyboard
  and mouse.
* **`d3dcompiler_47.dll`** — it is not in the 2010 redistributable (it ships with Windows 8.1+ / the Windows SDK). A
  program that asks only for `_47` gets `ERROR_MOD_NOT_FOUND` and the hint to use `D3DCompiler_43`; our own
  `d3dcompile.exe` tries `_47`, `_43`, `_42` in that order like most games do.

## What had to be added to the emulator

* CPU: `RCPSS`/`RSQRTSS`/`RCPPS`/`RSQRTPS` (`0F 52`/`0F 53`) — D3DX math uses them for `D3DXVec3Normalize` & co.
* msvcrt: `operator new/delete` in all mangled variants (`??2@YAPEAX_K@Z`, array, `nothrow`, sized delete),
  `std::exception` (ctor/copy/dtor/`what`, guest vtable), `_vsnwprintf`, `_fpclass`, `_mbstrlen`, `IsBadReadPtr`
  & co. (always "good"), `RegOpenKeyA`.
* Loader: DirectX names are **native-only** (`pe_dll_native_only`): no built-in shim pretends to be `d3dx9_43`; if the
  file is not on the virtual disk `LoadLibrary` fails with `ERROR_MOD_NOT_FOUND` (126) and the log says *install the
  DirectX pack*; a static import of such a DLL is reported by the PE inspector as *needs the DirectX pack* and x64 Runner
  installs it before starting the program. `GetModuleHandle("D3DCompiler_43.dll")` returns NULL until the DLL is really
  loaded (d3dx11 relies on that to `LoadLibrary` it), and `GetModuleFileName` of a pack DLL reports its real path
  `C:\Windows\System32\D3DCompiler_43.dll`.
* XInput ordinals 100–103 (`XInputGetStateEx`, `XInputWaitForGuideButton`, `XInputCancelGuideButtonWait`, `XInputPowerOffController`).

## Tests

* `emu/tests/d3dcompile.c` → `d3dcompile.exe` (bundled sample): `LoadLibrary` of the compiler, `D3DCompile` of a
  vertex and a pixel shader from HLSL text, `D3DDisassemble`, then a D3D11 device/swap chain, the DXBC passed to
  `CreateVertexShader`/`CreatePixelShader`, 3 frames drawn. Natively the draw stops with the honest *this host has no GPU
  back end* fault; in Chromium (e2e *DirectX pack*) the triangle is rendered through WebGL2 and the test reads the
  canvas back (orange/cyan/magenta corners + clear colour).
* `emu/tests/dxpack.c` → `dxpack.exe`: d3dx9_43 (`D3DXMatrixPerspectiveFovLH`, `D3DXMatrixInverse`,
  `D3DXMatrixMultiply`, `D3DXVec3Normalize`), d3dx11_43 (`D3DX11CompileFromMemory`), X3DAudio1_7
  (`X3DAudioInitialize` + `X3DAudioCalculate` with `X3DAUDIO_CALCULATE_MATRIX`), xinput1_3 (`XInputGetState` must
  return 1167). Exit code = number of failures.
* `emu/tests/run.sh` group **[5dx]** (needs `DX_PACK=<dir with the plain DLLs>`, CI unpacks the artifact to
  `/tmp/dx/sys32`): both programs + *every pack DLL loads and runs its DllMain* (53/53).

Run locally:

```sh
emu/tools/dx-pack.sh public/x64/dx /var/tmp/dx/pack          # downloads 95.6 MB once
cd emu && DX_PACK=/var/tmp/dx/pack ./tests/run.sh
./x64/x64run --no-stdin --mount '/var/tmp/dx/pack=C:\Windows\System32' tests/dxpack.exe
```

## Honest limits

* No Direct3D 9 / 10 device, no DirectDraw, no DirectInput, no DirectSound/XAudio: only **Direct3D 11 (feature level
  11.0 subset) → WebGL2** exists. A DX9 game will load `d3dx9_43.dll` fine and then stop at `Direct3DCreate9` —
  the log says so.
* The pack is downloaded into the virtual disk each session (49.5 MB; the browser's HTTP cache makes the second time
  fast) — it is not persisted in IndexedDB yet.
* Only the June 2010 versions are included (the last release of the redistributable). Programs asking for a newer
  `d3dcompiler_47` are told honestly.
* Performance is that of the interpreter (~40–55 MIPS): compiling the two test shaders with `D3DCompiler_43` takes
  about 45 M instructions ≈ 1 s.
