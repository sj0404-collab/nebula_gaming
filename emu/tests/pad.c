/* pad.exe: game controller bridge checks — XInput (xinput1_4), winmm joystick API and DirectInput8 game controller enumeration /
   state.  Run with x64run --pad '0:1,0x1010,255,0,16384,-8192,0,0@300' (A + START, LT full, LX half right, LY quarter down),
   then --pad '0:0,0,0,0,0,0,0,0@2500' to unplug.  Prints "ALL OK n/n". */
#define WIN32_LEAN_AND_MEAN
#define DIRECTINPUT_VERSION 0x0800
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <xinput.h>
#include <mmsystem.h>
#include <dinput.h>
#include <stdio.h>
static int ok_n, all_n;
static void check(const char *what, int cond) { all_n++; if (cond) ok_n++; printf("  %s %s\n", cond ? "ok  " : "FAIL", what); }
typedef DWORD (WINAPI *PFN_GetState)(DWORD, XINPUT_STATE *); typedef DWORD (WINAPI *PFN_GetCaps)(DWORD, DWORD, XINPUT_CAPABILITIES *); typedef DWORD (WINAPI *PFN_SetState)(DWORD, XINPUT_VIBRATION *);
static int npads, nobjs, naxes, nbtns, npov; static GUID padguid;
static BOOL CALLBACK enum_dev(const DIDEVICEINSTANCEA *d, void *ref) { (void)ref; printf("  device: %s type=%08lx usage=%u/%u\n", d->tszInstanceName, d->dwDevType, d->wUsagePage, d->wUsage); if (GET_DIDEVICE_TYPE(d->dwDevType) == DI8DEVTYPE_GAMEPAD) { npads++; padguid = d->guidInstance; } return DIENUM_CONTINUE; }
static BOOL CALLBACK enum_obj(const DIDEVICEOBJECTINSTANCEA *o, void *ref) { (void)ref; nobjs++; if (o->dwType & DIDFT_AXIS) naxes++; if (o->dwType & DIDFT_BUTTON) nbtns++; if (o->dwType & DIDFT_POV) npov++; return DIENUM_CONTINUE; }
int main(void) {
  HMODULE xi = LoadLibraryA("xinput1_4.dll"); if (!xi) xi = LoadLibraryA("xinput1_3.dll"); check("xinput1_4.dll loads", xi != 0);
  PFN_GetState GetState = (PFN_GetState)GetProcAddress(xi, "XInputGetState"); PFN_GetCaps GetCaps = (PFN_GetCaps)GetProcAddress(xi, "XInputGetCapabilities"); PFN_SetState SetState = (PFN_SetState)GetProcAddress(xi, "XInputSetState");
  PFN_GetState GetStateEx = (PFN_GetState)GetProcAddress(xi, (LPCSTR)100);
  check("XInputGetState/GetCapabilities/SetState/ordinal 100 exported", GetState && GetCaps && SetState && GetStateEx);
  XINPUT_STATE st; memset(&st, 0, sizeof st);
  DWORD r0 = GetState(0, &st); check("before the host plugs a pad in: ERROR_DEVICE_NOT_CONNECTED (1167)", r0 == ERROR_DEVICE_NOT_CONNECTED);
  check("slot 3 is also empty", GetState(3, &st) == ERROR_DEVICE_NOT_CONNECTED);
  check("slot 7 is invalid (not a crash)", GetState(7, &st) == ERROR_DEVICE_NOT_CONNECTED);
  DWORD t0 = GetTickCount(), r = 1167; while (GetTickCount() - t0 < 2000) { r = GetState(0, &st); if (r == ERROR_SUCCESS) break; Sleep(20); }
  check("pad appears at run time (host --pad at 300 ms) without re-creating anything", r == ERROR_SUCCESS);
  check("A and START pressed", (st.Gamepad.wButtons & (XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_START)) == (XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_START) && !(st.Gamepad.wButtons & XINPUT_GAMEPAD_B));
  check("left trigger 255, right 0", st.Gamepad.bLeftTrigger == 255 && st.Gamepad.bRightTrigger == 0);
  check("left stick X 16384, Y -8192", st.Gamepad.sThumbLX == 16384 && st.Gamepad.sThumbLY == -8192);
  check("packet number is non-zero", st.dwPacketNumber != 0);
  XINPUT_STATE st2; GetStateEx(0, &st2); check("XInputGetStateEx (ordinal 100) reports the same state", st2.Gamepad.wButtons == st.Gamepad.wButtons && st2.Gamepad.sThumbLX == 16384);
  XINPUT_CAPABILITIES caps; memset(&caps, 0, sizeof caps); check("XInputGetCapabilities: gamepad subtype with all buttons", GetCaps(0, XINPUT_FLAG_GAMEPAD, &caps) == ERROR_SUCCESS && caps.Type == XINPUT_DEVTYPE_GAMEPAD && caps.SubType == XINPUT_DEVSUBTYPE_GAMEPAD && (caps.Gamepad.wButtons & XINPUT_GAMEPAD_Y));
  XINPUT_VIBRATION vib = { 65535, 32768 }; check("XInputSetState (rumble) accepted", SetState(0, &vib) == ERROR_SUCCESS);
  /* winmm */
  check("joyGetNumDevs = 16 (as on Windows)", joyGetNumDevs() == 16);
  JOYINFOEX ji; memset(&ji, 0, sizeof ji); ji.dwSize = sizeof ji; ji.dwFlags = JOY_RETURNALL;
  MMRESULT jr = joyGetPosEx(0, &ji); check("joyGetPosEx(0) = JOYERR_NOERROR", jr == JOYERR_NOERROR);
  check("joystick X ~ 3/4 right (49151), Y ~ 5/8 down, button 1 (A) + button 8 (START)", ji.dwXpos > 49000 && ji.dwXpos < 49300 && ji.dwYpos > 40800 && ji.dwYpos < 41100 && (ji.dwButtons & 1) && (ji.dwButtons & 0x80) && !(ji.dwButtons & 2));
  check("POV centred", ji.dwPOV == JOY_POVCENTERED);
  check("joyGetPosEx(1) = JOYERR_UNPLUGGED", joyGetPosEx(1, &ji) == JOYERR_UNPLUGGED);
  JOYCAPSA jc; memset(&jc, 0, sizeof jc); check("joyGetDevCapsA: 5 axes, 10 buttons, POV", joyGetDevCapsA(0, &jc, sizeof jc) == JOYERR_NOERROR && jc.wNumAxes == 5 && jc.wNumButtons == 10 && (jc.wCaps & JOYCAPS_HASPOV) && jc.wMid == 0x045E);
  /* DirectInput8 */
  IDirectInput8A *di = 0; HRESULT hr = DirectInput8Create(GetModuleHandleA(0), DIRECTINPUT_VERSION, &IID_IDirectInput8A, (void **)&di, 0); check("DirectInput8Create", hr == DI_OK && di);
  hr = IDirectInput8_EnumDevices(di, DI8DEVCLASS_GAMECTRL, enum_dev, 0, DIEDFL_ATTACHEDONLY); check("EnumDevices(GAMECTRL) finds exactly one game pad", hr == DI_OK && npads == 1);
  IDirectInputDevice8A *dev = 0; hr = IDirectInput8_CreateDevice(di, &padguid, &dev, 0); check("CreateDevice(pad instance GUID)", hr == DI_OK && dev);
  DIDEVCAPS dc; memset(&dc, 0, sizeof dc); dc.dwSize = sizeof dc; hr = IDirectInputDevice8_GetCapabilities(dev, &dc); check("GetCapabilities: attached, 5 axes, 10 buttons, 1 POV", hr == DI_OK && (dc.dwFlags & DIDC_ATTACHED) && dc.dwAxes == 5 && dc.dwButtons == 10 && dc.dwPOVs == 1);
  hr = IDirectInputDevice8_EnumObjects(dev, enum_obj, 0, DIDFT_ALL); check("EnumObjects: 5 axes + 10 buttons + 1 POV through the callback", hr == DI_OK && naxes == 5 && nbtns == 10 && npov == 1 && nobjs == 16);
  hr = IDirectInputDevice8_SetDataFormat(dev, &c_dfDIJoystick2); check("SetDataFormat(c_dfDIJoystick2)", hr == DI_OK);
  DIPROPRANGE rg; memset(&rg, 0, sizeof rg); rg.diph.dwSize = sizeof rg; rg.diph.dwHeaderSize = sizeof rg.diph; rg.diph.dwHow = DIPH_DEVICE; rg.lMin = -1000; rg.lMax = 1000; hr = IDirectInputDevice8_SetProperty(dev, DIPROP_RANGE, &rg.diph); check("SetProperty(DIPROP_RANGE -1000..1000)", hr == DI_OK);
  hr = IDirectInputDevice8_Acquire(dev); check("Acquire", hr == DI_OK);
  IDirectInputDevice8_Poll(dev); DIJOYSTATE2 js; memset(&js, 0, sizeof js); hr = IDirectInputDevice8_GetDeviceState(dev, sizeof js, &js);
  check("GetDeviceState(DIJOYSTATE2)", hr == DI_OK);
  printf("  js: lX=%ld lY=%ld lZ=%ld pov=%lu b0=%02x b7=%02x\n", js.lX, js.lY, js.lZ, js.rgdwPOV[0], js.rgbButtons[0], js.rgbButtons[7]);
  check("axes in the requested range: X ~ +500, Y ~ +250 (DirectInput +Y is down), Z = triggers (+1000 LT)", js.lX >= 495 && js.lX <= 505 && js.lY >= 245 && js.lY <= 255 && js.lZ >= 995 && js.lZ <= 1000);
  check("button 0 (A) and button 7 (Start) 0x80, button 1 up, POV centred", js.rgbButtons[0] == 0x80 && js.rgbButtons[7] == 0x80 && js.rgbButtons[1] == 0 && js.rgdwPOV[0] == 0xFFFFFFFF);
  DIDEVICEOBJECTINSTANCEA oi; memset(&oi, 0, sizeof oi); oi.dwSize = sizeof oi; hr = IDirectInputDevice8_GetObjectInfo(dev, &oi, DIJOFS_Y, DIPH_BYOFFSET); check("GetObjectInfo(DIJOFS_Y) = Y Axis", hr == DI_OK && !strcmp(oi.tszName, "Y Axis") && IsEqualGUID(&oi.guidType, &GUID_YAxis));
  /* unplug at 2.5 s: XInput reports not connected again, DirectInput reports input lost */
  t0 = GetTickCount(); r = 0; while (GetTickCount() - t0 < 4000) { r = GetState(0, &st); if (r == ERROR_DEVICE_NOT_CONNECTED) break; Sleep(20); }
  check("unplug: XInputGetState = ERROR_DEVICE_NOT_CONNECTED again", r == ERROR_DEVICE_NOT_CONNECTED);
  hr = IDirectInputDevice8_GetDeviceState(dev, sizeof js, &js); check("unplug: DirectInput GetDeviceState = DIERR_INPUTLOST", hr == DIERR_INPUTLOST);
  IDirectInputDevice8_Release(dev); IDirectInput8_Release(di);
  printf("ALL OK %d/%d\n", ok_n, all_n); return ok_n == all_n ? 0 : 1;
}
