// =============================================================================
//  input_sdl.cpp  -  SDL2 GameController -> DIJOYSTATE[2] translation
//
//  Ported from Pad-Within's input_sdl.cpp (Warrior Within), generalized to
//  write into either DIJOYSTATE (32 buttons) or DIJOYSTATE2 (128 buttons)
//  via buttonsCapacity, since we don't yet know (no hardware trace done in
//  this environment - see README) which one PCDeviceJoystick actually uses.
//  Both formats share byte-identical axis/slider/POV layout in their first
//  36 bytes, so a single filler covers both.
// =============================================================================

#define WIN32_LEAN_AND_MEAN
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include <SDL.h>

#include "proxy.h"
#include "log.h"
#include "config.h"

static SDL_GameController* g_pad = nullptr;
static SDL_Joystick*       g_joy = nullptr;
#define MAX_PADS 8
static SDL_GameController* g_pads[MAX_PADS] = {};
static int                 g_padCount = 0;

void Proxy_LogJoystickCountOnce()
{
    static DWORD last = 0;
    DWORD now = GetTickCount();
    if (now - last < 1000) return;
    last = now;
    int nj = SDL_NumJoysticks();
    LOG("periodic: SDL_NumJoysticks()=%d, g_padCount=%d, g_pad=%p", nj, g_padCount, (void*)g_pad);
    for (int i = 0; i < nj; ++i) {
        LOG("  joy[%d] name='%s' isGC=%d", i, SDL_JoystickNameForIndex(i), SDL_IsGameController(i));
    }
}

// ---------------------------------------------------------------------------
void Proxy_InitInput()
{
    SDL_SetHint(SDL_HINT_JOYSTICK_THREAD, "1");
    SDL_SetHint(SDL_HINT_XINPUT_ENABLED, "1");
    // NOT forcing RAWINPUT on: on this SDL2 build it was grabbing the Switch
    // Pro Controller BEFORE the HIDAPI backend could claim it, exposing it
    // under the generic Windows HID name with no SDL_GameController mapping
    // (confirmed via log: SDL_IsGameController()==0, name='HID-compliant
    // game controller'). HIDAPI's Switch driver knows the real name/mapping;
    // let it take priority instead of forcing RAWINPUT to compete for it.
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    // DualSense/DualShock are NOT XInput devices - they only show up via the
    // HIDAPI backend (confirmed present in this SDL2.dll build, including a
    // full DualSense mapping). Force it on explicitly rather than relying on
    // whatever the build's compiled-in default is.
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS4, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS5, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS5_RUMBLE, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_SWITCH, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_JOY_CONS, "1");

    // HIDAPI's device-detection thread dispatches through the events
    // subsystem; without SDL_INIT_EVENTS explicitly up, the very first
    // enumeration (which we do a few lines below, synchronously, milliseconds
    // after SDL_Init) can race the thread and see zero devices.
    if (SDL_Init(SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK | SDL_INIT_EVENTS) != 0) {
        LOG("SDL_Init failed: %s", SDL_GetError());
        return;
    }

    // Give the HIDAPI background thread a moment to finish its first scan
    // before we enumerate. This runs once, at the game's very early startup
    // (first DirectInput8Create call), well before the game creates its
    // window, so a short blocking wait here is harmless.
    for (int attempt = 0; attempt < 10; ++attempt) {
        SDL_PumpEvents();
        if (SDL_NumJoysticks() > 0) break;
        SDL_Delay(100);
    }

    int nj = SDL_NumJoysticks();
    LOG("SDL sees %d joystick(s):", nj);
    for (int i = 0; i < nj; ++i)
        LOG("  joy[%d] name='%s' isGC=%d", i, SDL_JoystickNameForIndex(i), SDL_IsGameController(i));

    for (int i = 0; i < nj && g_padCount < MAX_PADS; ++i) {
        if (SDL_IsGameController(i)) {
            SDL_GameController* c = SDL_GameControllerOpen(i);
            if (c) {
                g_pads[g_padCount++] = c;
                LOG("opened GameController %d: %s", i, SDL_GameControllerName(c));
            } else {
                LOG("SDL_GameControllerOpen(%d) failed: %s", i, SDL_GetError());
            }
        }
    }

    if (g_cfg.controllerIndex >= 0 && g_cfg.controllerIndex < g_padCount) {
        g_pad = g_pads[g_cfg.controllerIndex];
        LOG("using controller by ini index %d: %s", g_cfg.controllerIndex, SDL_GameControllerName(g_pad));
    } else if (g_padCount > 0) {
        g_pad = g_pads[0];
        LOG("auto mode: defaulting to controller 0, will switch to active pad");
    }

    if (g_padCount == 0 && nj > 0) {
        g_joy = SDL_JoystickOpen(0);
        if (g_joy)
            LOG("opened raw Joystick 0: %s (axes=%d buttons=%d hats=%d)",
                SDL_JoystickName(g_joy), SDL_JoystickNumAxes(g_joy),
                SDL_JoystickNumButtons(g_joy), SDL_JoystickNumHats(g_joy));
    }
    if (!g_pad && !g_joy) LOG("no SDL device opened at init");
}

static void RescanIfNeeded()
{
    SDL_PumpEvents();
    int nj = SDL_NumJoysticks();

    if (g_padCount == 0) {
        static DWORD lastScan = 0;
        DWORD now = GetTickCount();
        if (now - lastScan >= 500) {
            lastScan = now;
            for (int i = 0; i < nj && g_padCount < MAX_PADS; ++i) {
                if (SDL_IsGameController(i)) {
                    SDL_GameController* c = SDL_GameControllerOpen(i);
                    if (c) {
                        g_pads[g_padCount++] = c;
                        if (!g_pad) g_pad = c;
                        LOG("rescan: opened GameController %d: %s", i, SDL_GameControllerName(c));
                    }
                }
            }
        }
    }

    // Raw fallback: some pads never get a GameController mapping (e.g. a
    // Switch Pro Controller whose HIDAPI driver couldn't claim the device).
    // OFF by default - it's a blind numeric guess, not a real mapping, and
    // masks the real problem (something else holding the HID handle, or a
    // whitelist gap in this SDL build). Opt in via AllowRawFallback=1 in
    // PadOfTime.ini only if you've exhausted the hidapi troubleshooting in
    // README and just want *something* working meanwhile.
    if (g_cfg.allowRawFallback && g_padCount == 0 && !g_joy && nj > 0) {
        static DWORD firstSeenUnmapped = 0;
        DWORD now = GetTickCount();
        if (firstSeenUnmapped == 0) firstSeenUnmapped = now;
        if (now - firstSeenUnmapped > 3000) {
            for (int i = 0; i < nj; ++i) {
                g_joy = SDL_JoystickOpen(i);
                if (g_joy) {
                    LOG("raw fallback: opened Joystick %d '%s' (axes=%d buttons=%d hats=%d) - "
                        "no GameController mapping available, using generic raw axis/button reading",
                        i, SDL_JoystickName(g_joy), SDL_JoystickNumAxes(g_joy),
                        SDL_JoystickNumButtons(g_joy), SDL_JoystickNumHats(g_joy));
                    break;
                }
            }
        }
    }
}

static void SelectActivePad()
{
    RescanIfNeeded();
    if (g_cfg.controllerIndex >= 0) return;
    if (g_padCount <= 1) return;
    for (int i = 0; i < g_padCount; ++i) {
        SDL_GameController* c = g_pads[i];
        if (!c) continue;
        for (int a = 0; a < SDL_CONTROLLER_AXIS_MAX; ++a) {
            if (abs(SDL_GameControllerGetAxis(c, (SDL_GameControllerAxis)a)) > 12000) {
                if (g_pad != c) LOG("switching active pad to %d: %s", i, SDL_GameControllerName(c));
                g_pad = c; return;
            }
        }
        for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX; ++b) {
            if (SDL_GameControllerGetButton(c, (SDL_GameControllerButton)b)) {
                if (g_pad != c) LOG("switching active pad to %d: %s", i, SDL_GameControllerName(c));
                g_pad = c; return;
            }
        }
    }
}

void Proxy_ShutdownInput()
{
    for (int i = 0; i < g_padCount; ++i)
        if (g_pads[i]) SDL_GameControllerClose(g_pads[i]);
    g_padCount = 0; g_pad = nullptr;
    if (g_joy) { SDL_JoystickClose(g_joy); g_joy = nullptr; }
    SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK);
}

void Proxy_LogRawPad()
{
    SDL_PumpEvents();
    SDL_GameControllerUpdate();
    SDL_JoystickUpdate();

    char buf[768]; int n = 0;
    n += snprintf(buf+n, sizeof(buf)-n, "  [gc] ");
    if (g_pad) {
        n += snprintf(buf+n, sizeof(buf)-n, "btn:");
        for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX; ++b)
            if (SDL_GameControllerGetButton(g_pad, (SDL_GameControllerButton)b))
                n += snprintf(buf+n, sizeof(buf)-n, " %s",
                    SDL_GameControllerGetStringForButton((SDL_GameControllerButton)b));
        n += snprintf(buf+n, sizeof(buf)-n, " axis:");
        for (int a = 0; a < SDL_CONTROLLER_AXIS_MAX; ++a) {
            int v = SDL_GameControllerGetAxis(g_pad, (SDL_GameControllerAxis)a);
            if (v > 8000 || v < -8000)
                n += snprintf(buf+n, sizeof(buf)-n, " %s=%d",
                    SDL_GameControllerGetStringForAxis((SDL_GameControllerAxis)a), v);
        }
    } else {
        n += snprintf(buf+n, sizeof(buf)-n, "(no GameController)");
    }
    LOG("%s", buf);
}

// ---------------------------------------------------------------------------
static void RadialDeadzone(float& x, float& y, float dz)
{
    float mag = std::sqrt(x*x + y*y);
    if (mag < dz) { x = 0.f; y = 0.f; return; }
    float scaled = (mag - dz) / (1.f - dz);
    if (scaled > 1.f) scaled = 1.f;
    x = (x / mag) * scaled;
    y = (y / mag) * scaled;
}

static void ApplyMaxInput(float& x, float& y, int maxInputPercent)
{
    if (maxInputPercent >= 100) return;
    float maxInput = maxInputPercent / 100.f;
    if (maxInput < 0.5f) maxInput = 0.5f;
    x = x / maxInput; if (x > 1.f) x = 1.f; if (x < -1.f) x = -1.f;
    y = y / maxInput; if (y > 1.f) y = 1.f; if (y < -1.f) y = -1.f;
}

static void AxisSnap(float& x, float& y)
{
    float ratio = g_cfg.axisSnapRatio;
    if (ratio <= 0.f) return;
    float ax = std::fabs(x), ay = std::fabs(y);
    if (ax < ay * ratio) x = 0.f;
    else if (ay < ax * ratio) y = 0.f;
}

static long AxisDI(float norm)
{
    if (norm < -1.f) norm = -1.f;
    if (norm >  1.f) norm =  1.f;
    long v = (long)lroundf(norm * 32767.f);
    if (v >  32767) v =  32767;
    if (v < -32768) v = -32768;
    return v;
}

// ---------------------------------------------------------------------------
// Standard predefined-format byte offsets (identical for DIJOYSTATE and
// DIJOYSTATE2 in their shared prefix):
//   lX=0 lY=4 lZ=8 lRx=12 lRy=16 lRz=20 rglSlider[2]=24,28 rgdwPOV[4]=32..47
//   rgbButtons[N]=48..  (N=32 for DIJOYSTATE, 128 for DIJOYSTATE2)
// ---------------------------------------------------------------------------
void Proxy_FillJoyBuffer(void* buf, int buttonsCapacity)
{
    if (!buf) return;
    SDL_GameControllerUpdate();
    SelectActivePad();
    unsigned char* base = reinterpret_cast<unsigned char*>(buf);

    if (!g_pad && !g_joy) return;   // buffer was already zeroed by the caller

    if (!g_pad && g_joy) {
        // ---- raw SDL_Joystick fallback (no GameController mapping) ----
        // Best-effort generic layout: axis0/1 = move stick, axis2/3 = camera
        // stick if present, hat0 = D-pad, button i -> game button i (identity).
        // This is a starting point, not a calibrated mapping - if buttons/axes
        // land wrong in-game, enable EnableLog=1 and watch the "[raw]" lines
        // below while pressing each button/moving each axis on the real pad
        // to see its actual index, then we can wire up a proper remap.
        SDL_JoystickUpdate();
        auto W = [&](int ofs, long val){ *reinterpret_cast<long*>(base + ofs) = val; };

        int nAxes = SDL_JoystickNumAxes(g_joy);
        float lx = nAxes > 0 ? SDL_JoystickGetAxis(g_joy, 0) / 32767.f : 0.f;
        float ly = nAxes > 1 ? SDL_JoystickGetAxis(g_joy, 1) / 32767.f : 0.f;
        ApplyMaxInput(lx, ly, g_cfg.moveMaxRange);
        RadialDeadzone(lx, ly, g_cfg.moveDeadzone);
        AxisSnap(lx, ly);
        if (g_cfg.invertMoveY) ly = -ly;
        W(0, AxisDI(lx));
        W(4, AxisDI(ly));

        float rx = nAxes > 2 ? SDL_JoystickGetAxis(g_joy, 2) / 32767.f : 0.f;
        float ry = nAxes > 3 ? SDL_JoystickGetAxis(g_joy, 3) / 32767.f : 0.f;
        float camMul = g_cfg.cameraSensitivity / 50.f;
        if (camMul < 0.f) camMul = 0.f;
        ApplyMaxInput(rx, ry, g_cfg.cameraMaxRange);
        RadialDeadzone(rx, ry, g_cfg.cameraDeadzone);
        AxisSnap(rx, ry);
        rx *= camMul; ry *= camMul;
        if (g_cfg.invertCameraX) rx = -rx;
        if (g_cfg.invertCameraY) ry = -ry;
        W(12, AxisDI(rx));
        W(16, AxisDI(ry));

        int nButtons = SDL_JoystickNumButtons(g_joy);
        for (int i = 0; i < nButtons && i < buttonsCapacity; ++i)
            if (SDL_JoystickGetButton(g_joy, i)) base[48 + i] = 0x80;

        DWORD pov = 0xFFFFFFFF;
        if (SDL_JoystickNumHats(g_joy) > 0) {
            Uint8 hat = SDL_JoystickGetHat(g_joy, 0);
            switch (hat) {
                case SDL_HAT_UP: pov = 0; break;
                case SDL_HAT_RIGHTUP: pov = 4500; break;
                case SDL_HAT_RIGHT: pov = 9000; break;
                case SDL_HAT_RIGHTDOWN: pov = 13500; break;
                case SDL_HAT_DOWN: pov = 18000; break;
                case SDL_HAT_LEFTDOWN: pov = 22500; break;
                case SDL_HAT_LEFT: pov = 27000; break;
                case SDL_HAT_LEFTUP: pov = 31500; break;
                default: break;
            }
        }
        *reinterpret_cast<DWORD*>(base + 32) = pov;

        if (g_cfg.enableLog) {
            static DWORD last = 0; DWORD now = GetTickCount();
            if (now - last > 250) {
                last = now;
                char btns[256] = {0}; int n = 0;
                for (int i = 0; i < nButtons && n < (int)sizeof(btns) - 8; ++i)
                    if (SDL_JoystickGetButton(g_joy, i)) n += snprintf(btns + n, sizeof(btns) - n, "%d,", i);
                LOG("  [raw] axes(0-3)=%.2f,%.2f,%.2f,%.2f pov=%lu pressed=[%s]", lx, ly, rx, ry, pov, btns);
            }
        }
        return;
    }

    // ---- normal path: SDL_GameController with a real mapping ----
    auto W = [&](int ofs, long val){ *reinterpret_cast<long*>(base + ofs) = val; };

    // --- movement: left stick -> X / Y ---
    float lx = SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_LEFTX) / 32767.f;
    float ly = SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_LEFTY) / 32767.f;
    ApplyMaxInput(lx, ly, g_cfg.moveMaxRange);
    RadialDeadzone(lx, ly, g_cfg.moveDeadzone);
    AxisSnap(lx, ly);
    if (g_cfg.invertMoveY) ly = -ly;
    W(0, AxisDI(lx));
    W(4, AxisDI(ly));

    // --- camera: right stick -> Rx / Ry ---
    float camMul = g_cfg.cameraSensitivity / 50.f;
    if (camMul < 0.f) camMul = 0.f;
    float rx = SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_RIGHTX) / 32767.f;
    float ry = SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_RIGHTY) / 32767.f;
    ApplyMaxInput(rx, ry, g_cfg.cameraMaxRange);
    RadialDeadzone(rx, ry, g_cfg.cameraDeadzone);
    AxisSnap(rx, ry);
    rx *= camMul;
    ry *= camMul;
    if (g_cfg.invertCameraX) rx = -rx;
    if (g_cfg.invertCameraY) ry = -ry;

    // --- triggers -> combined axis ---
    float rt = SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) / 32767.f;
    float lt = SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT)  / 32767.f;
    if (rt < 0.f) rt = 0.f;
    if (lt < 0.f) lt = 0.f;
    float z = g_cfg.swapTriggers ? (lt - rt) : (rt - lt);

    if (!g_cfg.cameraOnZRz) {
        W(12, AxisDI(rx));   // Rx
        W(16, AxisDI(ry));   // Ry
        W(8,  AxisDI(z));    // Z
    } else {
        W(8,  AxisDI(rx));
        W(20, AxisDI(ry));   // Rz
        W(12, AxisDI(z));    // Rx (triggers)
    }

    // --- buttons ---
    auto set = [&](SDL_GameControllerButton b, int idx){
        if (idx < 0 || idx >= buttonsCapacity) return;
        if (!SDL_GameControllerGetButton(g_pad, b)) return;
        int ofs = 48 + idx;
        base[ofs] = 0x80;
    };
    set(SDL_CONTROLLER_BUTTON_A,             g_cfg.btnA);
    set(SDL_CONTROLLER_BUTTON_B,             g_cfg.btnB);
    set(SDL_CONTROLLER_BUTTON_X,             g_cfg.btnX);
    set(SDL_CONTROLLER_BUTTON_Y,             g_cfg.btnY);
    set(SDL_CONTROLLER_BUTTON_LEFTSHOULDER,  g_cfg.btnLB);
    set(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, g_cfg.btnRB);
    set(SDL_CONTROLLER_BUTTON_START,         g_cfg.btnStart);
    set(SDL_CONTROLLER_BUTTON_BACK,          g_cfg.btnBack);
    set(SDL_CONTROLLER_BUTTON_LEFTSTICK,     g_cfg.btnLS);
    set(SDL_CONTROLLER_BUTTON_RIGHTSTICK,    g_cfg.btnRS);

    // --- D-pad -> POV hat ---
    bool up    = SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_DPAD_UP);
    bool down  = SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    bool left  = SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
    bool right = SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
    DWORD pov = 0xFFFFFFFF;
    if      (up && right) pov = 4500;
    else if (right&&down) pov = 13500;
    else if (down&&left)  pov = 22500;
    else if (left&&up)    pov = 31500;
    else if (up)          pov = 0;
    else if (right)       pov = 9000;
    else if (down)        pov = 18000;
    else if (left)        pov = 27000;
    *reinterpret_cast<DWORD*>(base + 32) = pov;

    if (g_cfg.enableLog) {
        static DWORD last = 0; DWORD now = GetTickCount();
        if (now - last > 250) {
            last = now;
            LOG("  [fill cap=%d] LX=%.2f LY=%.2f RX=%.2f RY=%.2f Z=%.2f pov=%lu",
                buttonsCapacity, lx, ly, rx, ry, z, pov);
        }
    }
}
