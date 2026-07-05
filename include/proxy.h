#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <unknwn.h>

// ---------------------------------------------------------------------------
// Vtable-patch entry point (implemented in hook_di.cpp). Patches CreateDevice
// and EnumDevices in-place on the REAL IDirectInput8 the game just got back
// from the system dinput8.dll - we do not create a separate wrapper object.
// ---------------------------------------------------------------------------
void Proxy_HookDirectInput8(void** ppvOut);

// ---------------------------------------------------------------------------
// SDL2 input backend (implemented in input_sdl.cpp).
// ---------------------------------------------------------------------------
void Proxy_InitInput();
void Proxy_ShutdownInput();
void Proxy_LogRawPad();
// Diagnostic: logs SDL_NumJoysticks() + per-joystick name/isGameController,
// throttled to once/second. Called unconditionally from Hook_GetDeviceState
// so we can tell, from a single log, whether SDL ever sees a given pad even
// when nothing gets synthesized.
void Proxy_LogJoystickCountOnce();

// Fill a standard-layout joystick state buffer from the current SDL pad.
// SoT's PCDeviceJoystick calls SetDataFormat with one of the two predefined
// DirectInput formats; which one is in play is detected at runtime in
// hook_di.cpp by matching cbData against sizeof(DIJOYSTATE)/sizeof(DIJOYSTATE2)
// - both share the same axis/POV layout in their first 36 bytes, only
// rgbButtons' length (32 vs 128) and the trailing velocity/accel/force
// fields differ. buttonsCapacity tells the filler how many rgbButtons
// slots it's allowed to write (32 or 128).
void Proxy_FillJoyBuffer(void* buf, int buttonsCapacity);
