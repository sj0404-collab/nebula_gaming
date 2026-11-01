# Sound on Nebula x64 — the device's audio output is the emulator's sound card

Since v1.19 every Windows audio API of the emulated x86-64 program ends up in **one mixer** inside the wasm64 emulator,
and the mixer's output goes to **the device the browser runs on** — on Android that is the phone's speaker, Bluetooth
headset or USB DAC, through Web Audio. Nothing is faked: a program that plays a 440 Hz tone with `waveOutWrite`
produces a 440 Hz tone in the headphones, with the timing of a real sound card (buffers complete at the rate they are
played, position counters advance, notifications fire at the positions asked for).

## What a program can use

| API / DLL | Implemented in | State |
|---|---|---|
| **winmm.dll** `waveOutGetNumDevs/GetDevCaps(A/W)/Open/Close/Prepare/Unprepare/Write/Pause/Restart/Reset/BreakLoop/GetPosition/GetVolume/SetVolume/GetPitch/SetPitch/GetPlaybackRate/SetPlaybackRate/GetID/Message/GetErrorText(A/W)` | `emu/x64/win32_audio.inc` | **works**: PCM 8/16/24/32-bit, float, `WAVE_FORMAT_EXTENSIBLE`, 1–8 channels, any rate (linear resampling to the device rate); `WAVE_FORMAT_QUERY`; callbacks **EVENT**, **FUNCTION** (`WOM_OPEN/DONE/CLOSE` on a helper thread, like Windows), **WINDOW** and **THREAD** (`MM_WOM_OPEN/DONE/CLOSE` posted to the process's message queue — the emulator has one queue per process) |
| **winmm** `waveIn*`, `midiOut*/midiIn*`, `mixer*`, `mciSendString/mciSendCommand`, `auxGetNumDevs` | same | **honest no-device**: `waveInGetNumDevs` = 0, `waveInOpen` → `MMSYSERR_BADDEVICEID`, `midiOutOpen` → `MMSYSERR_NODRIVER`, `mixerGetNumDevs` = 0, MCI → `MCIERR_DEVICE_NOT_INSTALLED`. Programs that *optionally* record or play MIDI keep running |
| **winmm** `PlaySound(A/W)`, `sndPlaySound(A/W)` | same | **works**: `SND_MEMORY` (a RIFF/WAVE in memory), file names (read from the virtual disk), `SND_SYNC` (returns when the clip has been consumed), `SND_ASYNC`, `SND_LOOP`, `SND_PURGE`/`NULL` (stop). `SND_ALIAS`/`SND_RESOURCE` system sounds → nothing to play, returns TRUE |
| **dsound.dll** `DirectSoundCreate`, `DirectSoundCreate8`, `DirectSoundEnumerate(A/W)`, `GetDeviceID`; `IDirectSound8` (`SetCooperativeLevel`, `GetCaps`, `CreateSoundBuffer`, `DuplicateSoundBuffer`, `GetSpeakerConfig`, `Initialize`, `VerifyCertification`); `IDirectSoundBuffer8` (`Lock/Unlock`, `Play/Stop`, `GetCurrentPosition/SetCurrentPosition`, `GetStatus`, `GetFormat/SetFormat`, `Get/SetVolume`, `Get/SetPan`, `Get/SetFrequency`, `GetCaps`, `Restore`, `AcquireResources`, `GetObjectInPath`); `IDirectSoundNotify::SetNotificationPositions` (offsets + `DSBPN_OFFSETSTOP`); `IDirectSound3DListener` / `IDirectSound3DBuffer` | same | **works** for the 2-D path (secondary streaming/static buffers, looping, notifications, cursors). Primary buffer: `SetFormat/Play` accepted, writing to it is a no-op (Windows ≥ Vista behaves the same). 3-D: parameters stored, panning not applied yet. Capture (`DirectSoundCaptureCreate`) → `DSERR_NODRIVER` |
| **mmdevapi / audioclient (WASAPI)**: `IMMDeviceEnumerator` (`EnumAudioEndpoints`, `GetDefaultAudioEndpoint`, `GetDevice`, notifications registered/ignored), `IMMDeviceCollection`, `IMMDevice` (`Activate`, `OpenPropertyStore`, `GetId`, `GetState`), `IMMEndpoint`, `IPropertyStore` (`PKEY_Device_FriendlyName`, `PKEY_AudioEngine_DeviceFormat`, `PKEY_AudioEndpoint_PhysicalSpeakers/GUID`), `IAudioClient/2/3` (`GetMixFormat`, `IsFormatSupported`, `GetDevicePeriod`, `GetSharedModeEnginePeriod`, `Initialize` shared + exclusive, event-driven or polling, `InitializeSharedAudioStream`, `GetBufferSize`, `GetCurrentPadding`, `GetStreamLatency`, `SetEventHandle`, `Start/Stop/Reset`, `GetService`), `IAudioRenderClient` (`GetBuffer/ReleaseBuffer`, `AUDCLNT_BUFFERFLAGS_SILENT`), `IAudioClock/2`, `ISimpleAudioVolume`, `IAudioStreamVolume`, `IAudioSessionControl/2`, `IAudioEndpointVolume` | same | **works** — one render endpoint `{0.0.0.00000000}.{6f1a3c5e-…}` "Nebula Audio Output (host)", mix format 48 kHz float stereo (or whatever the device reports), period 10 ms; `eCapture` → `E_NOTFOUND`, `IAudioCaptureClient` → `AUDCLNT_E_UNSUPPORTED_FORMAT` |
| **ole32** `CoCreateInstance` | same | `CLSID_MMDeviceEnumerator`, `CLSID_DirectSound(8)` built in; **`CLSID_XAudio2` 2.0…2.7 (+ debug CLSIDs) and XACT3 engines → the real DLL from the DirectX pack**: `LoadLibrary` (DllMain through the loader continuation) → `DllGetClassObject` → `IClassFactory::CreateInstance` → `Release`, all as guest code; other classes → `REGDB_E_CLASSNOTREG` with a log line |
| **XAudio2_0…2_7.dll, XAPOFX1_0…1_5, xactengine3_0…3_7** (DirectX pack) | real Microsoft DLLs | **XAudio2 2.7 plays** (`audio.exe` test 5: engine → `IMMDeviceEnumerator` → `IAudioClient::Initialize` event-driven → render thread → 523 Hz tone in the host mix). XACT3 DLLs load and run their `DllMain`; not exercised further |
| **avrt.dll** `AvSetMmThreadCharacteristics(A/W)`, `AvRevertMmThreadCharacteristics`, `AvSetMmThreadPriority` | same | accepted (no scheduling classes in the emulator) |

## How it works

```
guest program ──waveOut/DirectSound/WASAPI/XAudio2──► AStream[ ] (ring / queue / WASAPI buffer, any PCM format)
                                                         │  aud_mix_block(): resample + convert + volume/pan → float32 stereo, 10 ms
scheduler (api.c x64_run) ── w32_audio_pump() ──────────►│  keeps ~100 ms queued at the host; fires WASAPI period events,
                                                         │  DirectSound notifications, waveOut WHDR_DONE (+ callbacks)
host_audio_write() ──► browser: src/lib/x64/audio.ts ── AudioContext, one AudioBufferSourceNode per block, sample-accurate start times
                   ──► native runner: wall-clock "player" (+ X64_AUDIO_WAV=<file> dumps the mix as a WAV for the tests)
```

* **Timing is the host's clock.** `host_audio_queued()` says how many frames are still waiting at the device; the mixer
  tops the queue up to ~100 ms and never further, so a program that writes 4 × 100 ms of audio sees the buffers complete
  one by one over ~300 ms — not instantly and not late. `waveOutGetPosition`, `GetCurrentPosition`, `IAudioClock` are
  derived from the same counters.
* **Callbacks happen where Windows would do them.** `CALLBACK_FUNCTION` runs on a short-lived helper thread of the owning
  process (the scheduler switches to that process first — `w32_audio_deliver`), `CALLBACK_WINDOW` posts `MM_WOM_DONE`,
  `CALLBACK_EVENT` signals the event, WASAPI event mode signals once per 10 ms block, DirectSound notifications signal
  their events when the play cursor crosses the offset.
* **Background tabs.** While sound is active the frame loop switches from `requestAnimationFrame` (which stops in hidden
  tabs) to a short timer; Chrome does not throttle timers of audible tabs, so music keeps playing with the screen off.
* **Autoplay policy.** Browsers may refuse to start audio before the first tap/click. The emulator opens the device
  anyway; while the context is *suspended* a wall-clock stand-in keeps the timing honest (the program runs, the sound is
  dropped) and the status bar shows `🔊 … (tap to unmute)`. The first pointer/key event resumes it.
* **Formats.** 8-bit unsigned, 16/24/32-bit signed, 32-bit float, `WAVE_FORMAT_EXTENSIBLE` with PCM/float sub-formats,
  1–8 channels (down-mixed to stereo: mono duplicated, >2 channels averaged into L/R), any sample rate (linear resampling).

## Tests

* `emu/tests/audio.c` → `audio.exe` (zig cc, `-lwinmm -lole32`): **[1]** waveOut event + function callbacks, position,
  real-time check, waveIn honest, **[2]** DirectSound 8 secondary buffer with half-way and stop notifications, volume/pan,
  duplicate + looping status, **[3]** WASAPI enumerator → property store → `IAudioClient` event-driven 48 kHz float, 23
  period wake-ups for 250 ms, clock, **[4]** `PlaySound(SND_MEMORY | SND_SYNC)` 400 ms, **[5]** `CoCreateInstance(CLSID_XAudio2 2.7)`
  → mastering + source voice → 300 ms buffer → `BuffersQueued == 0` after ~300 ms. Exit code = number of failures.
* `emu/tests/run.sh` section **[5au]** runs it on the native runner with `X64_AUDIO_WAV=/tmp/x64audio.wav` and checks
  the dump: ≥ 100 loud 10 ms blocks (result: `wav 2.36s, 218 loud 10ms blocks of 235, peak 0.77`).
* `e2e/run.mjs` (DirectX pack scenario) runs `audio.exe` in headless Chromium after the pack install, expects all five
  `… OK` lines and `data-x64-audio` (frames handed to Web Audio) > 48000.

## Honest limits

* **Output only.** No microphone / line-in (`waveIn`, `eCapture`, `IAudioCaptureClient`, `DirectSoundCapture` all say
  "no device" the way Windows does when nothing is plugged in). Browser capture (`getUserMedia`) could be bridged later.
* **~100 ms output latency** plus the device's own; fine for games and players, not for rhythm-critical software.
  Buffers complete when the mixer consumes them, i.e. ~100 ms before the speaker is done with them.
* DirectSound **3-D** (`IDirectSound3DBuffer`) stores parameters but does not pan/attenuate yet; effects (`SetFX`) are not
  applied. `DSBCAPS_LOCHARDWARE` requests are served from software like modern Windows.
* **No MIDI synthesizer**, no ACM/MF codecs (`acmStreamOpen`, `MFStartup` → not implemented → a program decoding MP3/AAC
  through Windows codecs must bring its own decoder; most games do).
* **XAudio2 2.8/2.9** (`xaudio2_8.dll`, `xaudio2_9.dll`, `XAudio2_9redist.dll`) are Windows 8+/NuGet components, not in
  the June 2010 redistributable, so a game that imports them still stops at `LoadLibrary` with an honest message.
  Candidates for a later pack.
* Exclusive mode is accepted but behaves like shared mode; `IAudioClient::GetStreamLatency` reports 10 ms.
* The native test runner has no sound device: it only keeps the clock and (optionally) writes the WAV.
